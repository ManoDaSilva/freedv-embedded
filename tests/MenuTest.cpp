// Knob decoding, the config file patcher, the menu engine and the settings
// menu, all without hardware. With a directory as argument it also writes
// the menu screens as PBM images.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "app/Config.h"
#include "ui/Menu.h"
#include "ui/SettingsMenu.h"
#include "ui/TomlPatcher.h"

using namespace fdve;

static int failures = 0;
#define CHECK(cond) \
    do { if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); failures++; } } while (0)

static std::string readFile(const std::string& path)
{
    std::ifstream in(path);
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

static bool contains(const std::string& text, const std::string& part)
{
    return text.find(part) != std::string::npos;
}

static MenuEvents turn(int steps) { MenuEvents e; e.steps = steps; return e; }
static MenuEvents click() { MenuEvents e; e.clicks = 1; return e; }
static MenuEvents longPress() { MenuEvents e; e.backs = 1; return e; }

// Moves the highlight to the entry with this label. False if there is none.
static bool select(Menu& menu, const std::string& label)
{
    menu.handle(turn(-100));
    for (int i = 0; i < 40; i++)
    {
        if (menu.selectedLabel() == label) return true;
        menu.handle(turn(1));
    }
    return false;
}

static std::string outDir;
static void snapshot(const Menu& menu, const char* name)
{
    if (outDir.empty()) return;
    Framebuffer fb;
    menu.render(fb);
    std::ofstream(outDir + "/" + name + ".pbm") << fb.toPbm();
}

static void testKnob()
{
    // One detent clockwise is the sequence 00 -> 10 -> 11 -> 01 -> 00 on (A, B).
    QuadratureDecoder decoder(4);
    int steps = 0;
    for (int n = 0; n < 3; n++)
        for (auto [a, b] : {std::pair{true, false}, {true, true}, {false, true}, {false, false}})
            steps += decoder.update(a, b);
    CHECK(steps == 3);
    for (auto [a, b] : {std::pair{false, true}, {true, true}, {true, false}, {false, false}})
        steps += decoder.update(a, b);
    CHECK(steps == 2);
    // Contact bounce on one phase must not count.
    for (int n = 0; n < 10; n++) steps += decoder.update(n % 2 == 0, false);
    CHECK(steps == 2);

    using namespace std::chrono;
    const auto t0 = steady_clock::now();
    PressTracker press(600);
    MenuEvents e;
    press.update(1, true, e, t0);                         // went down
    CHECK(e.clicks == 0 && e.backs == 0);
    press.update(0, false, e, t0 + milliseconds(100));    // released early: a click
    CHECK(e.clicks == 1 && e.backs == 0);
    e = {};
    press.update(1, true, e, t0 + seconds(1));
    press.update(0, true, e, t0 + seconds(1) + milliseconds(500));
    CHECK(!e.any());
    press.update(0, true, e, t0 + seconds(1) + milliseconds(700)); // held: back, once
    press.update(0, true, e, t0 + seconds(2));
    press.update(0, false, e, t0 + seconds(3));
    CHECK(e.clicks == 0 && e.backs == 1);
    e = {};
    press.update(2, false, e, t0 + seconds(4));           // two quick taps between polls
    CHECK(e.clicks == 2);
}

static void testRackUiDecoder()
{
    using Reading = RackUiDecoder::Reading;
    using namespace std::chrono;
    const auto t0 = steady_clock::now();
    auto ms = [&](int n) { return t0 + milliseconds(n); };
    auto same = [](Reading value) { return [value](Reading& out) { out = value; return true; }; };

    RackUiDecoder decoder(600);
    decoder.reset(Reading{100, 7, false});

    // Nothing changed: no events, and no second read is made.
    bool reread = false;
    auto noReread = [&](Reading&) { reread = true; return true; };
    CHECK(!decoder.update(Reading{100, 7, false}, noReread, ms(0)).any() && !reread);

    // A turn of three, confirmed by the second read.
    MenuEvents e = decoder.update(Reading{103, 7, false}, same(Reading{103, 7, false}), ms(10));
    CHECK(e.steps == 3 && e.clicks == 0);

    // A corrupted read: the second read shows the real, unchanged values.
    // Nothing is reported, now or on the following clean polls.
    e = decoder.update(Reading{-7937, 255, true}, same(Reading{103, 7, false}), ms(20));
    CHECK(!e.any() && decoder.rejected() == 1);
    e = decoder.update(Reading{103, 7, false}, noReread, ms(30));
    CHECK(!e.any());
    // Two different wrong reads in a row are not a confirmation either.
    e = decoder.update(Reading{104, 9, false}, same(Reading{98, 7, false}), ms(40));
    CHECK(!e.any() && decoder.rejected() == 2);
    // A corrupted button state alone must not start a press.
    e = decoder.update(Reading{103, 7, true}, noReread, ms(50));
    e = decoder.update(Reading{103, 7, true}, noReread, ms(900));
    CHECK(!e.any());

    // A real press: counted when confirmed, reported as a click on release.
    e = decoder.update(Reading{103, 8, true}, same(Reading{103, 8, true}), ms(1000));
    CHECK(!e.any());
    e = decoder.update(Reading{103, 8, false}, noReread, ms(1100));
    CHECK(e.clicks == 1 && e.backs == 0);
    // Held: a long press, and no click when it is released.
    e = decoder.update(Reading{103, 9, true}, same(Reading{103, 9, true}), ms(2000));
    e = decoder.update(Reading{103, 9, true}, noReread, ms(2700));
    CHECK(e.backs == 1 && e.clicks == 0);
    CHECK(!decoder.update(Reading{103, 9, false}, noReread, ms(2800)).any());

    // The 16 bit counter wraps: still a turn of two.
    decoder.reset(Reading{32767, 9, false});
    e = decoder.update(Reading{-32767, 9, false}, same(Reading{-32767, 9, false}), ms(3000));
    CHECK(e.steps == 2);

    // The module restarted and its counters went back to zero: adopted, no events.
    e = decoder.update(Reading{0, 0, false}, same(Reading{0, 0, false}), ms(3100));
    CHECK(!e.any());
    e = decoder.update(Reading{-1, 0, false}, same(Reading{-1, 0, false}), ms(3200));
    CHECK(e.steps == -1);
    CHECK(!reread);

    // LED bar: nine LEDs from LED 8 round to LED 4; LEDs 5, 6 and 7 stay off.
    auto sum = [](const std::array<uint8_t, 12>& f) { int t = 0; for (auto v : f) t += v; return t; };
    auto none = rackUiLevelFrame(0.0f, 32);
    auto full = rackUiLevelFrame(1.0f, 32);
    auto over = rackUiLevelFrame(2.5f, 32);
    CHECK(sum(none) == 0 && sum(full) == 9 * 32 && over == full);
    CHECK(full[5] == 0 && full[6] == 0 && full[7] == 0 && full[8] == 32 && full[4] == 32);
    // A third of the way: the first three LEDs of the arc, in order.
    auto third = rackUiLevelFrame(1.0f / 3.0f, 32);
    CHECK(third[8] == 32 && third[9] == 32 && third[10] == 32 && third[11] == 0 && third[0] == 0);
    // In between, the LED at the tip is dimmed in proportion.
    auto half = rackUiLevelFrame(0.5f, 32); // 4.5 LEDs
    CHECK(half[8] == 32 && half[11] == 32 && half[0] == 16 && half[1] == 0);
    CHECK(rackUiLevelFrame(-1.0f, 32) == none);
}

static void testSystemInfo()
{
    // Fixed input files, so the formatting can be checked exactly.
    const std::string dir = TEST_TMP_DIR;
    SystemInfo info;
    info.statPath = dir + "/stat";
    info.meminfoPath = dir + "/meminfo";
    info.temperaturePath = dir + "/temp";

    std::ofstream(info.meminfoPath) << "MemTotal:        3884000 kB\nMemFree:          100000 kB\n"
                                       "MemAvailable:    3372000 kB\nBuffers:           50000 kB\n";
    CHECK(info.memory() == "500/3792 MB");
    std::ofstream(info.temperaturePath) << "48312\n";
    CHECK(info.temperature() == "48.3 C");

    // CPU: busy share between two readings. 600 of 1000 ticks were idle or waiting.
    std::ofstream(info.statPath) << "cpu  1000 0 500 8000 100 0 0 0 0 0\ncpu0 1 2 3 4 5 6 7 8 9 10\n";
    CHECK(info.cpuLoad() == "...");
    std::ofstream(info.statPath) << "cpu  1300 0 600 8550 150 0 0 0 0 0\n";
    CHECK(info.cpuLoad() == "40 %");
    CHECK(info.cpuLoad() == "..."); // no new ticks yet: no division by zero

    // Missing files are reported, not fatal.
    info.meminfoPath = info.temperaturePath = info.statPath = dir + "/does-not-exist";
    CHECK(info.memory() == "n/a" && info.temperature() == "n/a" && info.cpuLoad() == "n/a");

    // Network: an interface that lost its address reads "down".
    info.addressSource = [] { return std::vector<SystemInfo::Address>{{"eth0", "10.0.0.5"}}; };
    CHECK(info.addressOf("eth0") == "10.0.0.5" && info.addressOf("wlan0") == "down");
    // The real system: no loopback, and every address fits the display.
    for (const auto& address : SystemInfo().addresses())
        CHECK(address.interface != "lo" && !address.ip.empty() && address.ip.size() <= 15);
    CHECK(SystemInfo().memory().find(" MB") != std::string::npos);

    for (const char* name : {"/stat", "/meminfo", "/temp"}) std::remove((dir + name).c_str());
}

static void testVolumeArithmetic()
{
    // A coarse control, as on many USB headsets: 38 steps, 0 to 37.
    const long min = 0, max = 37;

    // Reading a level and writing it back must never move it.
    for (long v = min; v <= max; v++) CHECK(volumeFromPercent(volumeToPercent(v, min, max), min, max) == v);
    CHECK(volumeToPercent(0, min, max) == 0 && volumeToPercent(37, min, max) == 100);
    CHECK(volumeFromPercent(150, min, max) == 37 && volumeFromPercent(-5, min, max) == 0);

    // Every click of the knob moves it, in the direction turned, up to the ends.
    long v = 20;
    for (int i = 0; i < 5; i++)
    {
        const long next = volumeStepped(v, min, max, +1);
        CHECK(next == v + 1);
        v = next;
    }
    CHECK(volumeStepped(v, min, max, -1) == v - 1);
    CHECK(volumeStepped(36, min, max, +3) == 37 && volumeStepped(1, min, max, -3) == 0);
    int clicksToTop = 0;
    for (v = 0; v < max; v = volumeStepped(v, min, max, +1)) clicksToTop++;
    CHECK(clicksToTop == 37);

    // A fine control (0 to 65535): one click is 2 % of the range.
    CHECK(volumeStepped(0, 0, 65535, 1) == 1311);
    CHECK(volumeToPercent(volumeStepped(32768, 0, 65535, 5), 0, 65535) == 60);
    // A control with an offset range, and one with no range at all.
    CHECK(volumeToPercent(-20, -40, 0) == 50 && volumeFromPercent(50, -40, 0) == -20);
    CHECK(volumeToPercent(5, 5, 5) == 0 && volumeStepped(5, 5, 5, 3) == 5);
}

static void testPatcher()
{
    const std::string text =
        "# top comment\n"
        "[modem]\n"
        "mode = \"RADEV1\"        # only RADEV1 for now\n"
        "callsign = \"\"          # sent at the end # of each over\n"
        "\n"
        "[ptt.output]\n"
        "type = \"none\"\n"
        "# line = 27\n"
        "tx_tail_ms = 150\n"
        "\n"
        "[display]\n"
        "type = [\n"
        "  \"ssd1306\",  # the OLED\n"
        "  \"console\",\n"
        "]\n"
        "refresh_ms = 250\n";

    // Replace a value: the comment stays, in its column.
    std::string out = setTomlValue(text, "modem", "callsign", tomlString("ON6RF"));
    CHECK(contains(out, "callsign = \"ON6RF\"     # sent at the end # of each over\n"));
    CHECK(contains(out, "# top comment\n[modem]\nmode = \"RADEV1\"        # only RADEV1 for now\n"));

    // A "#" inside a string is not a comment.
    out = setTomlValue("[a]\nk = \"x # y\"  # real\n", "a", "k", "1");
    CHECK(out == "[a]\nk = 1        # real\n"); // the comment keeps its column

    // A missing key goes after the last key of its section. The commented
    // example line is left alone.
    out = setTomlValue(text, "ptt.output", "line", "27");
    CHECK(contains(out, "# line = 27\ntx_tail_ms = 150\nline = 27\n\n[display]"));

    // A missing section is added at the end.
    out = setTomlValue(text, "log", "level", tomlString("debug"));
    CHECK(contains(out, "refresh_ms = 250\n\n[log]\nlevel = \"debug\"\n"));

    // A value spread over several lines is replaced as a whole.
    out = setTomlValue(text, "display", "type", "[\"ssd1306\"]");
    CHECK(contains(out, "[display]\ntype = [\"ssd1306\"]\nrefresh_ms = 250\n"));

    // Keys that only start the same are not confused.
    out = setTomlValue("[a]\nline_a = 1\nline = 2\n", "a", "line", "9");
    CHECK(out == "[a]\nline_a = 1\nline = 9\n");

    // The shipped example survives a round of edits and still parses.
    std::string example = readFile(EXAMPLE_CONFIG);
    const size_t commentsBefore = std::count(example.begin(), example.end(), '#');
    example = setTomlValue(example, "modem", "callsign", tomlString("ON6RF"));
    example = setTomlValue(example, "audio.radio", "sample_rate", "44100");
    example = setTomlValue(example, "audio.headset", "playback_volume", "70");
    example = setTomlValue(example, "mode_switch", "type", tomlString("console"));
    example = setTomlValue(example, "display", "type", "[\"ssd1306\", \"console\"]");
    Config parsed = parseConfig(example);
    CHECK(parsed.callsign == "ON6RF" && parsed.radio.sampleRate == 44100 && parsed.headset.playbackVolume == 70);
    CHECK(parsed.headset.sampleRate == 48000 && parsed.modeSwitch.type == "console");
    CHECK(parsed.display.types.size() == 2);
    CHECK(static_cast<size_t>(std::count(example.begin(), example.end(), '#')) == commentsBefore);
}

static void testEngine()
{
    std::string colour = "red";
    int level = 40;
    int applied = 0;
    std::string name = "AB";
    int left = 0;

    MenuItem c;
    c.kind = MenuItem::Kind::Choice;
    c.label = "Colour";
    c.choices = {"red", "green", "blue"};
    c.get = [&] { return colour; };
    c.set = [&](const std::string& v) { colour = v; };

    MenuItem n;
    n.kind = MenuItem::Kind::Number;
    n.label = "Level";
    n.min = 0; n.max = 100; n.step = 5; n.unit = "%"; n.live = true;
    n.getInt = [&] { return level; };
    n.setInt = [&](int v) { level = v; applied++; };

    MenuItem t;
    t.kind = MenuItem::Kind::Text;
    t.label = "Name";
    t.maxLength = 4;
    t.get = [&] { return name; };
    t.set = [&](const std::string& v) { name = v; };

    MenuItem sub;
    sub.kind = MenuItem::Kind::Submenu;
    sub.label = "Sub";
    sub.children = {c, n, t};
    sub.onLeave = [&] { left++; };

    MenuItem root;
    root.kind = MenuItem::Kind::Submenu;
    root.label = "Root";
    root.children = {sub};

    Menu menu(root);
    CHECK(!menu.active() && !menu.handle(click()));
    menu.open();
    CHECK(menu.active() && menu.selectedLabel() == "Sub");
    menu.handle(click());
    CHECK(menu.selectedLabel() == "Colour");

    // Choice: confirm changes it, a long press cancels.
    menu.handle(click());
    menu.handle(turn(2));
    menu.handle(click());
    CHECK(colour == "blue" && !menu.editing());
    menu.handle(click());
    menu.handle(turn(-2));
    menu.handle(longPress());
    CHECK(colour == "blue" && !menu.editing());

    // Live number: applied while turning, clamped, restored on cancel.
    CHECK(select(menu, "Level"));
    menu.handle(click());
    menu.handle(turn(3));
    CHECK(level == 55);
    menu.handle(turn(100));
    CHECK(level == 100);
    menu.handle(longPress());
    CHECK(level == 40 && !menu.editing());
    menu.handle(click());
    menu.handle(turn(-2));
    menu.handle(click());
    CHECK(level == 30);

    // Text: turn picks the letter, press moves on, press on a blank ends.
    CHECK(select(menu, "Name"));
    menu.handle(click());      // cursor on "A"
    menu.handle(turn(2));      // A -> C
    menu.handle(click());      // cursor on "B"
    menu.handle(click());      // cursor on the blank after it
    menu.handle(turn(1));      // blank -> A
    menu.handle(click());      // cursor on the fourth position, blank
    menu.handle(click());      // ends
    CHECK(name == "CBA" && !menu.editing());
    // The last position ends the entry without needing a blank.
    menu.handle(click());
    for (int i = 0; i < 3; i++) menu.handle(click());
    menu.handle(turn(4));      // blank -> D
    menu.handle(click());
    CHECK(name == "CBAD" && !menu.editing());

    // Back entry and long press both leave; onLeave runs each time.
    menu.handle(turn(-100));
    CHECK(menu.selectedLabel().empty());
    menu.handle(click());
    CHECK(left == 1 && menu.selectedLabel() == "Sub");
    menu.handle(click());
    menu.handle(longPress());
    CHECK(left == 2);
    menu.handle(longPress());
    CHECK(!menu.active());

    // Closing from deep inside still runs onLeave.
    menu.open();
    menu.handle(click());
    menu.close();
    CHECK(left == 3);
}

static void testSettings()
{
    // A private copy of the example file, with sound and inputs that need no hardware.
    const std::string path = std::string(TEST_TMP_DIR) + "/menu-test.toml";
    std::string text = readFile(EXAMPLE_CONFIG);
    text = setTomlValue(text, "audio.radio", "type", tomlString("null"));
    text = setTomlValue(text, "audio.headset", "type", tomlString("null"));
    std::ofstream(path) << text;
    const size_t commentsBefore = std::count(text.begin(), text.end(), '#');

    SettingsMenu settings(loadConfig(path), path);
    Menu& menu = settings.menu();
    menu.open();
    snapshot(menu, "menu_1_root");

    // The knob's volume control needs a mixer control. This config runs on
    // null audio, so there is none: it reports that and changes nothing.
    CHECK(settings.adjustHeadphones(3) == -1);
    settings.saveVolumeChanges();
    CHECK(readFile(path) == text);

    // Every section of the file has a submenu, plus volume.
    for (const char* label : {"Volume", "Modem", "Mode switch", "Audio", "Rig", "PTT", "Display", "Controls", "Log",
                              "Save & restart", "Discard changes", "System", "Shut down"})
        CHECK(select(menu, label));

    // System: read-only rows. One network row per interface that is up,
    // named after the interface, made when the menu is entered.
    using Address = SystemInfo::Address;
    std::vector<Address> network = {{"eth0", "192.168.1.42"}, {"wlan0", "192.168.100.123"}};
    settings.system().addressSource = [&] { return network; };
    CHECK(select(menu, "System"));
    menu.handle(click());
    for (const char* label : {"eth0", "wlan0", "CPU", "RAM", "Temp"}) CHECK(select(menu, label));
    CHECK(!select(menu, "Network"));
    CHECK(!select(menu, "Rig")); // no radio control set up: no row
    snapshot(menu, "menu_9_system");
    // Pressing a row does nothing.
    CHECK(select(menu, "Temp"));
    menu.handle(click());
    CHECK(!menu.editing() && menu.selectedLabel() == "Temp");
    menu.handle(longPress());

    // Only Wi-Fi up: the eth0 row is gone on the next visit.
    network = {{"wlan0", "192.168.100.123"}};
    menu.handle(click());
    CHECK(select(menu, "wlan0") && !select(menu, "eth0") && select(menu, "CPU"));
    menu.handle(longPress());

    // With a radio set up, its row shows the frequency, or why there is none.
    auto rigRow = [&] {
        Framebuffer fb;
        menu.handle(click());
        const bool found = select(menu, "Rig");
        snapshot(menu, "menu_12_system_rig");
        menu.handle(longPress());
        return found;
    };
    settings.setRigStatus(true, true, 14236000);
    CHECK(rigRow());
    settings.setRigStatus(true, false, 0);
    CHECK(rigRow());
    settings.setRigStatus(false, false, 0);
    CHECK(!rigRow());

    // Nothing up: one row says so.
    network.clear();
    menu.handle(click());
    CHECK(select(menu, "Network") && !select(menu, "wlan0") && select(menu, "Temp"));
    menu.handle(longPress());
    CHECK(menu.selectedLabel() == "System");

    // Shut down: opens on "back", so two presses in a row do nothing.
    CHECK(select(menu, "Shut down"));
    menu.handle(click());
    snapshot(menu, "menu_10_shutdown");
    CHECK(menu.selectedLabel().empty());
    menu.handle(click());
    CHECK(!settings.shutdownRequested() && menu.selectedLabel() == "Shut down");
    // Turning to "Yes" and pressing asks for the shutdown. The test only
    // checks the request; nothing here powers anything off.
    menu.handle(click());
    menu.handle(turn(1));
    CHECK(menu.selectedLabel() == "Yes, shut down" && !settings.shutdownRequested());
    menu.handle(click());
    CHECK(settings.shutdownRequested());
    menu.handle(click());      // dismiss the message
    menu.handle(longPress());  // leave the confirmation menu

    // Nothing changed yet: saving says so and does not ask for a restart.
    CHECK(select(menu, "Save & restart"));
    menu.handle(click());
    menu.handle(click());
    CHECK(!settings.restartRequested());

    // Set the callsign to "ON6RF".
    CHECK(select(menu, "Modem"));
    menu.handle(click());
    CHECK(select(menu, "Callsign"));
    menu.handle(click());
    const std::string charset = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789/-";
    for (char ch : std::string("ON6RF"))
    {
        menu.handle(turn(static_cast<int>(charset.find(ch))));
        if (ch == '6') snapshot(menu, "menu_3_callsign");
        menu.handle(click());
    }
    menu.handle(click()); // blank: end
    snapshot(menu, "menu_2_modem");
    menu.handle(longPress());

    // Pick another sample rate for the radio card.
    CHECK(select(menu, "Audio"));
    menu.handle(click());
    snapshot(menu, "menu_4_audio");
    CHECK(select(menu, "Radio rate"));
    menu.handle(click());
    menu.handle(turn(-10));
    menu.handle(turn(3)); // 8000, 16000, 32000, 44100
    menu.handle(click());
    CHECK(select(menu, "Prebuffer"));
    menu.handle(click());
    menu.handle(turn(2)); // 160 -> 200
    snapshot(menu, "menu_5_number");
    menu.handle(click());
    menu.handle(longPress());

    // Nothing is written before saving.
    CHECK(readFile(path) == text);

    CHECK(select(menu, "Save & restart"));
    menu.handle(click());
    snapshot(menu, "menu_6_saved");
    CHECK(settings.restartRequested());

    const std::string saved = readFile(path);
    Config after = loadConfig(path);
    CHECK(after.callsign == "ON6RF");
    CHECK(after.radio.sampleRate == 44100 && after.headset.sampleRate == 48000);
    CHECK(after.prebufferMs == 200);
    CHECK(contains(saved, "callsign = \"ON6RF\""));
    CHECK(static_cast<size_t>(std::count(saved.begin(), saved.end(), '#')) == commentsBefore);

    // A change the program would refuse at start-up is not saved.
    SettingsMenu second(after, path);
    Menu& menu2 = second.menu();
    menu2.open();
    CHECK(select(menu2, "Mode switch"));
    menu2.handle(click());
    CHECK(select(menu2, "Type"));
    menu2.handle(click());
    menu2.handle(turn(-10));
    menu2.handle(turn(1)); // "gpio", but no line is set
    menu2.handle(click());
    menu2.handle(longPress());
    CHECK(select(menu2, "Save & restart"));
    menu2.handle(click());
    snapshot(menu2, "menu_7_refused");
    CHECK(!second.restartRequested());
    CHECK(readFile(path) == saved);

    // Discard drops it; then there is nothing to save.
    menu2.handle(click());
    CHECK(select(menu2, "Discard changes"));
    menu2.handle(click());
    menu2.handle(click());
    CHECK(select(menu2, "Save & restart"));
    menu2.handle(click());
    CHECK(!second.restartRequested() && readFile(path) == saved);

    // Sound devices: the list depends on the direction, and a newly chosen
    // device must open before the change is saved.
    AudioHardware fake;
    fake.list = [](bool capture) {
        std::vector<AlsaDevice> devices = {{"plughw:CARD=Device,DEV=0", "Device"}, {"plughw:CARD=Other,DEV=0", "Other"}};
        if (!capture) devices.push_back({"plughw:CARD=vc4hdmi0,DEV=0", "vc4hdmi0"}); // plays only
        return devices;
    };
    std::string rigAnswer;
    fake.rigCheck = [&](const RigConfig&) { return rigAnswer; };
    fake.serialPorts = [] { return std::vector<std::string>{"/dev/serial/by-id/usb-Silicon_Labs_CP2102_IC-7300-if00-port0", "/dev/ttyUSB0"}; };
    std::string probed;
    std::string probeAnswer = "cannot open ALSA device: busy";
    fake.probe = [&](const std::string& device, bool capture, int rate) {
        probed = device + (capture ? " capture " : " playback ") + std::to_string(rate);
        return probeAnswer;
    };
    // The test file runs on null audio; devices only matter for ALSA cards.
    auto withAlsa = [&] {
        Config config = loadConfig(path);
        config.radio.type = config.headset.type = "alsa";
        return config;
    };
    SettingsMenu third(withAlsa(), path, fake);
    Menu& menu3 = third.menu();
    auto pickDevice = [&](const char* item, int position) {
        menu3.open();
        CHECK(select(menu3, "Audio"));
        menu3.handle(click());
        CHECK(select(menu3, item));
        menu3.handle(click());
        menu3.handle(turn(-10));
        menu3.handle(turn(position));
        menu3.handle(click());
        menu3.handle(longPress());
    };
    auto saveNow = [&] {
        CHECK(select(menu3, "Save & restart"));
        menu3.handle(click());
    };

    // The playback-only card is offered for "Radio out" (third entry) but
    // not for "Radio in", where the list ends after two.
    pickDevice("Radio in", 2);
    saveNow();
    CHECK(probed == "plughw:CARD=Other,DEV=0 capture 44100"); // second entry: the list had only two
    snapshot(menu3, "menu_8_device_refused");
    CHECK(!third.restartRequested() && readFile(path) == saved); // did not open: nothing saved
    menu3.handle(click());

    // Once it opens, the same change is saved.
    probeAnswer = "";
    saveNow();
    CHECK(third.restartRequested());
    CHECK(loadConfig(path).radio.captureDevice == "plughw:CARD=Other,DEV=0");

    // Swapping to a device this program already has open is not probed: it
    // could not be opened twice, and it is known to work.
    SettingsMenu fourth(withAlsa(), path, fake);
    Menu& menu4 = fourth.menu();
    probed.clear();
    probeAnswer = "must not be asked";
    menu4.open();
    CHECK(select(menu4, "Audio"));
    menu4.handle(click());
    CHECK(select(menu4, "Mic"));
    menu4.handle(click());
    menu4.handle(turn(-10));
    menu4.handle(turn(1)); // "Other": in use as the radio's capture device
    menu4.handle(click());
    menu4.handle(longPress());
    CHECK(select(menu4, "Save & restart"));
    menu4.handle(click());
    CHECK(probed.empty() && fourth.restartRequested());

    // Rig: set up an IC-7300 from the menu and key it over CAT.
    SettingsMenu fifth(loadConfig(path), path, fake);
    Menu& menu5 = fifth.menu();
    menu5.open();
    CHECK(select(menu5, "Rig"));
    menu5.handle(click());
    CHECK(select(menu5, "Type"));
    menu5.handle(click());
    menu5.handle(turn(1)); // hamlib
    menu5.handle(click());
    CHECK(select(menu5, "Model"));
    menu5.handle(click()); // starts at "0"
    const std::string digits = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789/-";
    const std::string before = "0";
    const std::string wanted = "3073";
    for (size_t i = 0; i < wanted.size(); i++)
    {
        const char from = i < before.size() ? before[i] : ' ';
        menu5.handle(turn(static_cast<int>(digits.find(wanted[i])) - static_cast<int>(digits.find(from))));
        menu5.handle(click());
    }
    menu5.handle(click()); // blank: end
    CHECK(select(menu5, "Port"));
    menu5.handle(click());
    menu5.handle(turn(-10)); // first port in the list
    menu5.handle(click());
    snapshot(menu5, "menu_11_rig");
    menu5.handle(longPress());
    CHECK(select(menu5, "PTT"));
    menu5.handle(click());
    CHECK(select(menu5, "Out type"));
    menu5.handle(click());
    menu5.handle(turn(5)); // last: rig
    menu5.handle(click());
    menu5.handle(longPress());
    // Radio control that cannot work (here: no Hamlib in the build) is
    // refused on the display, and nothing is written.
    const std::string beforeRig = readFile(path);
    rigAnswer = "This program was built without Hamlib.";
    CHECK(select(menu5, "Save & restart"));
    menu5.handle(click());
    snapshot(menu5, "menu_13_rig_refused");
    CHECK(!fifth.restartRequested() && readFile(path) == beforeRig);
    menu5.handle(click());
    rigAnswer.clear();

    CHECK(select(menu5, "Save & restart"));
    menu5.handle(click());
    CHECK(fifth.restartRequested());
    Config withRig = loadConfig(path);
    CHECK(withRig.rig.type == "hamlib" && withRig.rig.model == 3073);
    CHECK(withRig.rig.device == "/dev/serial/by-id/usb-Silicon_Labs_CP2102_IC-7300-if00-port0");
    CHECK(withRig.pttOutput.type == "rig");

    std::remove(path.c_str());
}

int main(int argc, char** argv)
{
    if (argc > 1) outDir = argv[1];
    testKnob();
    testRackUiDecoder();
    testSystemInfo();
    testVolumeArithmetic();
    testPatcher();
    testEngine();
    testSettings();
    std::printf(failures == 0 ? "PASS\n" : "FAILED\n");
    return failures == 0 ? 0 : 1;
}
