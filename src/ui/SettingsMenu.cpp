#include "ui/SettingsMenu.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "audio/AudioFactory.h"
#include "ui/TomlPatcher.h"
#include "util/Log.h"

namespace fdve {

namespace {

MenuItem submenu(const std::string& label, std::vector<MenuItem> children)
{
    MenuItem item;
    item.kind = MenuItem::Kind::Submenu;
    item.label = label;
    item.children = std::move(children);
    return item;
}

MenuItem actionItem(const std::string& label, std::function<void()> action)
{
    MenuItem item;
    item.kind = MenuItem::Kind::Action;
    item.label = label;
    item.action = std::move(action);
    return item;
}

const char* biasName(GpioLine::Bias bias)
{
    switch (bias)
    {
        case GpioLine::Bias::PullUp: return "pull-up";
        case GpioLine::Bias::PullDown: return "pull-down";
        case GpioLine::Bias::None: break;
    }
    return "none";
}

// "plughw:CARD=Device,DEV=0" -> "Device", for a 128 pixel wide screen.
std::string shortDeviceName(const std::string& device)
{
    std::string name = device;
    const size_t card = name.find("CARD=");
    if (card != std::string::npos) name = name.substr(card + 5);
    else if (name.find(':') != std::string::npos) name = name.substr(name.find(':') + 1);
    const size_t comma = name.find(',');
    if (comma != std::string::npos) name = name.substr(0, comma);
    return name;
}

} // namespace

AudioHardware AudioHardware::real()
{
    AudioHardware hardware;
    hardware.list = [](bool capture) { return listAlsaDevices(capture); };
    hardware.rigCheck = [](const RigConfig& rig) { return checkRigConfig(rig); };
    hardware.serialPorts = [] {
        std::vector<std::string> ports;
        std::error_code ignored;
        // Names under by-id stay the same whichever USB socket is used.
        for (const char* dir : {"/dev/serial/by-id", "/dev"})
        {
            for (const auto& entry : std::filesystem::directory_iterator(dir, ignored))
            {
                const std::string name = entry.path().filename().string();
                const bool wanted = std::string(dir) != "/dev" || name.rfind("ttyUSB", 0) == 0 ||
                                    name.rfind("ttyACM", 0) == 0 || name.rfind("ttyAMA", 0) == 0;
                if (wanted) ports.push_back(entry.path().string());
            }
        }
        std::sort(ports.begin(), ports.end());
        return ports;
    };
    hardware.probe = [](const std::string& device, bool capture, int sampleRate) -> std::string {
        try
        {
            AudioStreamConfig stream;
            stream.type = "alsa";
            stream.device = device;
            stream.sampleRate = sampleRate;
            createAudioStream(stream, capture ? StreamDirection::Capture : StreamDirection::Playback);
            return "";
        }
        catch (const std::exception& e)
        {
            return e.what();
        }
    };
    return hardware;
}

SettingsMenu::SettingsMenu(const Config& config, std::string configPath, AudioHardware hardware)
    : saved_(config), edited_(config), configPath_(std::move(configPath)), hardware_(std::move(hardware)),
      menu_(MenuItem{})
{
    if (config.radio.type == "alsa")
        radioMixer_ = std::make_unique<Mixer>(mixerCardForDevice(config.radio.playbackDevice),
                                              config.radio.playbackMixer, config.radio.captureMixer);
    if (config.headset.type == "alsa")
        headsetMixer_ = std::make_unique<Mixer>(mixerCardForDevice(config.headset.playbackDevice),
                                                config.headset.playbackMixer, config.headset.captureMixer);
    menu_.setRoot(build());
}

void SettingsMenu::applyStoredVolumes()
{
    auto apply = [](Mixer* mixer, const SoundCardConfig& card) {
        if (mixer == nullptr) return;
        if (card.playbackVolume >= 0) mixer->setPlaybackPercent(card.playbackVolume);
        if (card.captureVolume >= 0) mixer->setCapturePercent(card.captureVolume);
    };
    apply(radioMixer_.get(), saved_.radio);
    apply(headsetMixer_.get(), saved_.headset);
}

int SettingsMenu::adjustHeadphones(int steps)
{
    if (!headsetMixer_ || !headsetMixer_->hasPlayback()) return -1;
    // Stepped in the control's own units: adding 2 % to a rounded percentage
    // can land on the same step of a coarse control, and then nothing moves.
    const int percent = headsetMixer_->stepPlayback(steps);
    edited_.headset.playbackVolume = percent;
    set("audio.headset", "playback_volume", std::to_string(percent), false);
    return percent;
}

void SettingsMenu::saveVolumeChanges()
{
    // Other unsaved settings wait for "Save & restart"; the volume then goes
    // into the file together with them.
    if (pending_.empty() || restartNeeded_) return;
    std::string error;
    if (!save(error)) FDVE_WARN("settings: volume not saved: %s", error.c_str());
}

void SettingsMenu::set(const std::string& section, const std::string& key, const std::string& literal, bool needsRestart)
{
    pending_[{section, key}] = literal;
    restartNeeded_ = restartNeeded_ || needsRestart;
}

// ---- item builders -------------------------------------------------------------

MenuItem SettingsMenu::choice(const std::string& label, const std::string& section, const std::string& key,
                              std::string& field, std::vector<std::string> choices, std::vector<std::string> labels)
{
    MenuItem item;
    item.kind = MenuItem::Kind::Choice;
    item.label = label;
    item.choices = std::move(choices);
    item.choiceLabels = std::move(labels);
    item.get = [&field] { return field; };
    item.set = [this, &field, section, key](const std::string& value) {
        if (value == field) return;
        field = value;
        set(section, key, tomlString(value));
    };
    return item;
}

MenuItem SettingsMenu::toggle(const std::string& label, const std::string& section, const std::string& key, bool& field)
{
    MenuItem item;
    item.kind = MenuItem::Kind::Choice;
    item.label = label;
    item.choices = {"off", "on"};
    item.get = [&field] { return std::string(field ? "on" : "off"); };
    item.set = [this, &field, section, key](const std::string& value) {
        const bool on = value == "on";
        if (on == field) return;
        field = on;
        set(section, key, on ? "true" : "false");
    };
    return item;
}

MenuItem SettingsMenu::number(const std::string& label, const std::string& section, const std::string& key, int& field,
                              int min, int max, int step, const std::string& unit)
{
    MenuItem item;
    item.kind = MenuItem::Kind::Number;
    item.label = label;
    item.min = std::min(min, field); // a value from the file outside the usual range stays reachable
    item.max = std::max(max, field);
    item.step = step;
    item.unit = unit;
    item.getInt = [&field] { return field; };
    item.setInt = [this, &field, section, key](int value) {
        if (value == field) return;
        field = value;
        set(section, key, std::to_string(value));
    };
    return item;
}

MenuItem SettingsMenu::biasChoice(const std::string& label, const std::string& section, GpioLine::Bias& field)
{
    MenuItem item;
    item.kind = MenuItem::Kind::Choice;
    item.label = label;
    item.choices = {"none", "pull-up", "pull-down"};
    item.get = [&field] { return std::string(biasName(field)); };
    item.set = [this, &field, section](const std::string& value) {
        GpioLine::Bias bias = GpioLine::Bias::None;
        if (value == "pull-up") bias = GpioLine::Bias::PullUp;
        if (value == "pull-down") bias = GpioLine::Bias::PullDown;
        if (bias == field) return;
        field = bias;
        set(section, "bias", tomlString(value));
    };
    return item;
}

MenuItem SettingsMenu::deviceChoice(const std::string& label, const std::string& section, const std::string& key,
                                    std::string& field, bool capture)
{
    // The devices present now that work in this direction, plus the
    // configured one if it is not among them.
    std::vector<std::string> devices;
    std::vector<std::string> labels;
    for (const auto& entry : hardware_.list(capture))
    {
        devices.push_back(entry.device);
        labels.push_back(entry.label);
    }
    if (std::find(devices.begin(), devices.end(), field) == devices.end())
    {
        devices.push_back(field);
        labels.push_back(shortDeviceName(field));
    }
    return choice(label, section, key, field, devices, labels);
}

// Before saving: a sound device that was changed must open, with its sample
// rate. Otherwise the program would restart into a config it cannot run.
bool SettingsMenu::changedDevicesOpen(std::string& error) const
{
    struct Use { const char* name; const std::string& now; const std::string& before; bool capture; int rate; bool alsa; };
    const Use uses[] = {
        {"Radio in", edited_.radio.captureDevice, saved_.radio.captureDevice, true, edited_.radio.sampleRate, edited_.radio.type == "alsa"},
        {"Radio out", edited_.radio.playbackDevice, saved_.radio.playbackDevice, false, edited_.radio.sampleRate, edited_.radio.type == "alsa"},
        {"Mic", edited_.headset.captureDevice, saved_.headset.captureDevice, true, edited_.headset.sampleRate, edited_.headset.type == "alsa"},
        {"Headphones", edited_.headset.playbackDevice, saved_.headset.playbackDevice, false, edited_.headset.sampleRate, edited_.headset.type == "alsa"},
    };
    for (const Use& use : uses)
    {
        if (!use.alsa || use.now == use.before) continue;
        // A device this program has open right now in the same direction
        // (the two cards being swapped) cannot be opened a second time, and
        // is known to work.
        const bool inUseHere = use.capture
            ? (use.now == saved_.radio.captureDevice || use.now == saved_.headset.captureDevice)
            : (use.now == saved_.radio.playbackDevice || use.now == saved_.headset.playbackDevice);
        if (inUseHere) continue;

        const std::string reason = hardware_.probe(use.now, use.capture, use.rate);
        if (!reason.empty())
        {
            error = std::string(use.name) + ": " + reason;
            return false;
        }
    }
    return true;
}

MenuItem SettingsMenu::rateChoice(const std::string& label, const std::string& section, int& field)
{
    MenuItem item;
    item.kind = MenuItem::Kind::Choice;
    item.label = label;
    item.choices = {"8000", "16000", "32000", "44100", "48000"};
    const std::string current = std::to_string(field);
    if (std::find(item.choices.begin(), item.choices.end(), current) == item.choices.end())
        item.choices.push_back(current);
    item.get = [&field] { return std::to_string(field); };
    item.set = [this, &field, section](const std::string& value) {
        const int rate = std::stoi(value);
        if (rate == field) return;
        field = rate;
        set(section, "sample_rate", value);
    };
    return item;
}

MenuItem SettingsMenu::volume(const std::string& label, const std::string& section, const std::string& key, int& field,
                              Mixer& mixer, bool playback)
{
    MenuItem item;
    item.kind = MenuItem::Kind::Number;
    item.label = label;
    item.min = 0;
    item.max = 100;
    item.step = 2;
    item.unit = "%";
    item.live = true;
    item.getInt = [&mixer, playback] { return playback ? mixer.playbackPercent() : mixer.capturePercent(); };
    item.setInt = [this, &mixer, &field, playback, section, key](int value) {
        if (playback) mixer.setPlaybackPercent(value);
        else mixer.setCapturePercent(value);
        field = value;
        set(section, key, std::to_string(value), false);
    };
    return item;
}

// ---- the tree --------------------------------------------------------------------

MenuItem SettingsMenu::build()
{
    Config& c = edited_;
    std::vector<MenuItem> root;

    // Serial ports present now, by their stable names where the system
    // provides them, plus the configured one.
    auto portChoices = [this](const std::string& current, std::vector<std::string>& ports,
                              std::vector<std::string>& labels) {
        auto shorten = [](const std::string& port) {
            // Show the end of the name: that is where the ports differ.
            return port.size() > 15 ? port.substr(port.size() - 15) : port;
        };
        for (const auto& port : hardware_.serialPorts())
        {
            ports.push_back(port);
            labels.push_back(shorten(port));
        }
        if (std::find(ports.begin(), ports.end(), current) == ports.end())
        {
            ports.push_back(current);
            labels.push_back(current.empty() ? "(not set)" : shorten(current));
        }
    };


    // Volume: acts at once, saved when leaving this submenu.
    {
        std::vector<MenuItem> items;
        if (radioMixer_ && radioMixer_->hasCapture())
            items.push_back(volume("Radio in", "audio.radio", "capture_volume", c.radio.captureVolume, *radioMixer_, false));
        if (radioMixer_ && radioMixer_->hasPlayback())
            items.push_back(volume("Radio out", "audio.radio", "playback_volume", c.radio.playbackVolume, *radioMixer_, true));
        if (headsetMixer_ && headsetMixer_->hasCapture())
            items.push_back(volume("Mic", "audio.headset", "capture_volume", c.headset.captureVolume, *headsetMixer_, false));
        if (headsetMixer_ && headsetMixer_->hasPlayback())
            items.push_back(volume("Headphones", "audio.headset", "playback_volume", c.headset.playbackVolume, *headsetMixer_, true));
        if (items.empty())
            items.push_back(actionItem("No volume controls", [this] {
                menu_.showMessage("Volume", "No mixer controls were found on the sound cards.");
            }));
        MenuItem volumeMenu = submenu("Volume", std::move(items));
        volumeMenu.onLeave = [this] {
            // Only volume changed: store it now, no restart needed.
            if (pending_.empty() || restartNeeded_) return;
            std::string error;
            if (!save(error)) menu_.showMessage("Not saved", error);
        };
        root.push_back(std::move(volumeMenu));
    }

    // [modem]
    {
        MenuItem callsign;
        callsign.kind = MenuItem::Kind::Text;
        callsign.label = "Callsign";
        callsign.maxLength = 8;
        callsign.get = [&c] { return c.callsign; };
        callsign.set = [this, &c](const std::string& value) {
            if (value == c.callsign) return;
            c.callsign = value;
            set("modem", "callsign", tomlString(value));
        };
        root.push_back(submenu("Modem", {
            std::move(callsign),
            choice("Mode", "modem", "mode", c.mode, {"RADEV1"}),
            toggle("Start analog", "modem", "start_mode", c.startAnalog),
        }));
        // start_mode is a word in the file, not a boolean.
        root.back().children.back().set = [this, &c](const std::string& value) {
            const bool analog = value == "on";
            if (analog == c.startAnalog) return;
            c.startAnalog = analog;
            set("modem", "start_mode", tomlString(analog ? "analog" : "digital"));
        };
    }

    // [mode_switch]
    root.push_back(submenu("Mode switch", {
        choice("Type", "mode_switch", "type", c.modeSwitch.type, {"none", "gpio", "console"}),
        number("GPIO line", "mode_switch", "line", c.modeSwitch.gpio.line, 0, 40, 1, ""),
        toggle("Active low", "mode_switch", "active_low", c.modeSwitch.gpio.activeLow),
        biasChoice("Bias", "mode_switch", c.modeSwitch.gpio.bias),
        choice("Action", "mode_switch", "action", c.modeSwitch.action, {"toggle", "level"}),
        number("Debounce", "mode_switch", "debounce_ms", c.modeSwitch.gpio.debounceMs, 0, 200, 5, "ms"),
    }));

    // [audio]
    root.push_back(submenu("Audio", {
        deviceChoice("Radio in", "audio.radio", "capture_device", c.radio.captureDevice, true),
        deviceChoice("Radio out", "audio.radio", "playback_device", c.radio.playbackDevice, false),
        rateChoice("Radio rate", "audio.radio", c.radio.sampleRate),
        deviceChoice("Mic", "audio.headset", "capture_device", c.headset.captureDevice, true),
        deviceChoice("Headphones", "audio.headset", "playback_device", c.headset.playbackDevice, false),
        rateChoice("Headset rate", "audio.headset", c.headset.sampleRate),
        number("Period", "audio", "period_ms", c.periodMs, 5, 100, 5, "ms"),
        number("Prebuffer", "audio", "prebuffer_ms", c.prebufferMs, 0, 500, 20, "ms"),
    }));

    // [ptt.input] and [ptt.output]
    root.push_back(submenu("PTT", {
        choice("In type", "ptt.input", "type", c.pttInput.type, {"gpio", "console", "none"}),
        number("In line", "ptt.input", "line", c.pttInput.gpio.line, 0, 40, 1, ""),
        toggle("In act. low", "ptt.input", "active_low", c.pttInput.gpio.activeLow),
        biasChoice("In bias", "ptt.input", c.pttInput.gpio.bias),
        number("In debounce", "ptt.input", "debounce_ms", c.pttInput.gpio.debounceMs, 0, 200, 5, "ms"),
        choice("Out type", "ptt.output", "type", c.pttOutput.type, {"none", "gpio", "rig"}),
        number("Out line", "ptt.output", "line", c.pttOutput.gpio.line, 0, 40, 1, ""),
        toggle("Out act. low", "ptt.output", "active_low", c.pttOutput.gpio.activeLow),
        number("TX delay", "ptt.output", "tx_delay_ms", c.txDelayMs, 0, 2000, 10, "ms"),
        number("TX tail", "ptt.output", "tx_tail_ms", c.txTailMs, 0, 2000, 10, "ms"),
    }));

    // [rig]
    {
        // The model is a number of up to five digits; entered digit by digit.
        MenuItem model;
        model.kind = MenuItem::Kind::Text;
        model.label = "Model";
        model.maxLength = 5;
        model.get = [&c] { return std::to_string(c.rig.model); };
        model.set = [this, &c](const std::string& value) {
            if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos)
            {
                menu_.showMessage("Model", "Digits only: the Hamlib model number.");
                return;
            }
            const int number = std::stoi(value);
            if (number == c.rig.model) return;
            c.rig.model = number;
            set("rig", "model", std::to_string(number));
        };

        std::vector<std::string> ports;
        std::vector<std::string> labels;
        portChoices(c.rig.device, ports, labels);

        MenuItem baud;
        baud.kind = MenuItem::Kind::Choice;
        baud.label = "Baud";
        baud.choices = {"0", "4800", "9600", "19200", "38400", "57600", "115200"};
        baud.choiceLabels = {"default", "4800", "9600", "19200", "38400", "57600", "115200"};
        if (std::find(baud.choices.begin(), baud.choices.end(), std::to_string(c.rig.baud)) == baud.choices.end())
        {
            baud.choices.push_back(std::to_string(c.rig.baud));
            baud.choiceLabels.push_back(std::to_string(c.rig.baud));
        }
        baud.get = [&c] { return std::to_string(c.rig.baud); };
        baud.set = [this, &c](const std::string& value) {
            const int rate = std::stoi(value);
            if (rate == c.rig.baud) return;
            c.rig.baud = rate;
            set("rig", "baud", value);
        };

        root.push_back(submenu("Rig", {
            choice("Type", "rig", "type", c.rig.type, {"none", "hamlib"}),
            std::move(model),
            choice("Port", "rig", "device", c.rig.device, ports, labels),
            std::move(baud),
            choice("PTT via", "rig", "ptt", c.rig.ptt, {"cat", "rts", "dtr"}),
        }));
    }

    // [display]. The display type and its I2C address are left out on
    // purpose: a wrong value there would switch off this very menu.
    {
        MenuItem console;
        console.kind = MenuItem::Kind::Choice;
        console.label = "Console line";
        console.choices = {"off", "on"};
        auto hasConsole = [&c] {
            return std::find(c.display.types.begin(), c.display.types.end(), "console") != c.display.types.end();
        };
        console.get = [hasConsole] { return std::string(hasConsole() ? "on" : "off"); };
        console.set = [this, &c, hasConsole](const std::string& value) {
            if ((value == "on") == hasConsole()) return;
            auto& types = c.display.types;
            if (value == "on") types.push_back("console");
            else types.erase(std::remove(types.begin(), types.end(), "console"), types.end());
            std::string literal = "[";
            for (size_t i = 0; i < types.size(); i++) literal += (i ? ", " : "") + tomlString(types[i]);
            set("display", "type", literal + "]");
        };
        root.push_back(submenu("Display", {
            number("Refresh", "display", "refresh_ms", c.display.refreshMs, 50, 2000, 50, "ms"),
            number("Contrast", "display", "contrast", c.display.contrast, 0, 255, 15, ""),
            toggle("Rotate 180", "display", "rotate", c.display.rotate),
            std::move(console),
        }));
    }

    // [menu]. The input type and its wiring are left out for the same reason.
    root.push_back(submenu("Controls", {
        toggle("Reverse knob", "menu", "reverse", c.menu.reverse),
        choice("LEDs", "menu", "leds", c.menu.leds, {"level", "follow", "off"}),
        number("LED bright.", "menu", "led_brightness", c.menu.ledBrightness, 1, 64, 4, ""),
        number("Long press", "menu", "long_press_ms", c.menu.longPressMs, 300, 2000, 100, "ms"),
        number("Menu timeout", "menu", "timeout_s", c.menu.timeoutS, 5, 300, 5, "s"),
    }));

    // [log]
    {
        MenuItem level;
        level.kind = MenuItem::Kind::Choice;
        level.label = "Level";
        level.choices = {"debug", "info", "warn", "error"};
        level.get = [&c] { return level_name(c.logLevel); };
        level.set = [this, &c](const std::string& value) {
            LogLevel parsed;
            if (!parseLogLevel(value.c_str(), parsed) || parsed == c.logLevel) return;
            c.logLevel = parsed;
            set("log", "level", tomlString(value));
        };
        root.push_back(submenu("Log", {std::move(level)}));
    }

    root.push_back(actionItem("Save & restart", [this] {
        if (pending_.empty())
        {
            menu_.showMessage("Save", "Nothing was changed.");
            return;
        }
        std::string error;
        if (!save(error))
        {
            menu_.showMessage("Not saved", error);
            return;
        }
        menu_.showMessage("Saved", "Restarting with the new settings...");
        restartRequested_.store(true);
    }));
    root.push_back(actionItem("Discard changes", [this] {
        const bool hadChanges = !pending_.empty();
        discard();
        menu_.showMessage("Discard", hadChanges ? "Changes dropped." : "Nothing was changed.");
    }));

    // System: readings about the Pi, and a clean shutdown.
    {
        auto info = [](const std::string& label, std::function<std::string()> read) {
            MenuItem item;
            item.kind = MenuItem::Kind::Info;
            item.label = label;
            item.get = std::move(read);
            return item;
        };
        // The CPU figure is the load since the previous reading. Take one
        // per second at most, however often the screen is redrawn.
        auto cpu = [this, last = std::string("..."), when = std::chrono::steady_clock::time_point()]() mutable {
            const auto now = std::chrono::steady_clock::now();
            if (now - when >= std::chrono::seconds(1))
            {
                last = system_.cpuLoad();
                when = now;
            }
            return last;
        };
        // The network rows are made when the menu is entered: one per
        // interface that is up and has an address (eth0, wlan0, ...), each
        // labelled with the interface name. With none up, one row says so.
        MenuItem systemMenu = submenu("System", {});
        systemMenu.onEnter = [this, info, cpu](MenuItem& menu) {
            menu.children.clear();
            for (const auto& address : system_.addresses())
            {
                const std::string name = address.interface;
                menu.children.push_back(info(name, [this, name] { return system_.addressOf(name); }));
            }
            if (menu.children.empty()) menu.children.push_back(info("Network", [] { return std::string("none"); }));
            // The radio, when a control connection is set up: its frequency,
            // or why there is none.
            if (rigConfigured_)
            {
                menu.children.push_back(info("Rig", [this]() -> std::string {
                    if (!rigConnected_) return "offline";
                    if (rigFrequencyHz_ <= 0) return "connected";
                    char text[24];
                    std::snprintf(text, sizeof(text), "%.4f MHz", static_cast<double>(rigFrequencyHz_) / 1e6);
                    return text;
                }));
            }
            menu.children.push_back(info("CPU", cpu));
            menu.children.push_back(info("RAM", [this] { return system_.memory(); }));
            menu.children.push_back(info("Temp", [this] { return system_.temperature(); }));
        };
        root.push_back(std::move(systemMenu));

        // Confirmation: the first press only opens this submenu, with "back"
        // highlighted. Shutting down takes a turn and a second press.
        MenuItem confirm = submenu("Shut down", {
            actionItem("Yes, shut down", [this] {
                menu_.showMessage("Shutting down", "Wait for the Pi's green light to stop before removing power.");
                shutdownRequested_.store(true);
            }),
        });
        confirm.startOnBack = true;
        root.push_back(std::move(confirm));
    }

    return submenu("Settings", std::move(root));
}

// ---- saving ------------------------------------------------------------------------

bool SettingsMenu::save(std::string& error)
{
    std::ifstream in(configPath_);
    if (!in)
    {
        error = "Cannot read " + configPath_;
        return false;
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    std::string text = buffer.str();
    in.close();

    for (const auto& [where, literal] : pending_) text = setTomlValue(text, where.first, where.second, literal);

    // The patched text must be a config the program accepts.
    try
    {
        parseConfig(text);
    }
    catch (const std::exception& e)
    {
        error = e.what();
        return false;
    }

    if (!changedDevicesOpen(error)) return false;

    // Radio control must be usable too: Hamlib built in, a model it knows.
    // Otherwise the program would restart into a config it stops on.
    const std::string rigProblem = hardware_.rigCheck(edited_.rig);
    if (!rigProblem.empty())
    {
        error = "Rig: " + rigProblem;
        return false;
    }

    // Write a new file and rename it over the old one, so a power cut cannot
    // leave half a file. If the directory is not writable, write in place.
    const std::string temp = configPath_ + ".new";
    std::ofstream out(temp, std::ios::trunc);
    const bool viaTemp = static_cast<bool>(out);
    if (!viaTemp) out.open(configPath_, std::ios::trunc);
    out << text;
    out.close();
    if (!out)
    {
        error = "Cannot write " + configPath_ + ": " + std::strerror(errno);
        if (viaTemp) std::remove(temp.c_str());
        return false;
    }
    if (viaTemp && std::rename(temp.c_str(), configPath_.c_str()) != 0)
    {
        error = "Cannot replace " + configPath_ + ": " + std::strerror(errno);
        std::remove(temp.c_str());
        return false;
    }

    FDVE_INFO("settings: saved %zu change(s) to %s", pending_.size(), configPath_.c_str());
    saved_ = edited_;
    pending_.clear();
    restartNeeded_ = false;
    return true;
}

void SettingsMenu::discard()
{
    // Volume was applied to the sound card while turning; put the saved levels back.
    edited_ = saved_;
    pending_.clear();
    restartNeeded_ = false;
    applyStoredVolumes();
}

} // namespace fdve
