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

    // Every section of the file has a submenu, plus volume.
    for (const char* label : {"Volume", "Modem", "Mode switch", "Audio", "PTT", "Display", "Controls", "Log",
                              "Save & restart", "Discard changes"})
        CHECK(select(menu, label));

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

    std::remove(path.c_str());
}

int main(int argc, char** argv)
{
    if (argc > 1) outDir = argv[1];
    testKnob();
    testPatcher();
    testEngine();
    testSettings();
    std::printf(failures == 0 ? "PASS\n" : "FAILED\n");
    return failures == 0 ? 0 : 1;
}
