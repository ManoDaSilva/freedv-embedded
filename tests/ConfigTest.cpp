#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

#include "app/Config.h"

static int failures = 0;

#define CHECK(cond) \
    do { if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); failures++; } } while (0)

static bool rejects(const char* toml, const char* expectedText)
{
    try
    {
        fdve::parseConfig(toml);
    }
    catch (const std::runtime_error& e)
    {
        return std::string(e.what()).find(expectedText) != std::string::npos;
    }
    return false;
}

int main()
{
    // The shipped example must parse and hold the documented values.
    auto example = fdve::loadConfig(EXAMPLE_CONFIG);
    CHECK(example.mode == "RADEV1");
    CHECK(example.radio.captureDevice == "plughw:CARD=Device,DEV=0");
    CHECK(example.headset.playbackDevice == "plughw:CARD=Device_1,DEV=0");
    CHECK(example.pttInput.type == "gpio");
    CHECK(example.pttInput.gpio.line == 17);
    CHECK(example.pttInput.gpio.activeLow);
    CHECK(example.pttInput.gpio.bias == fdve::GpioLine::Bias::PullUp);
    CHECK(example.pttInput.gpio.debounceMs == 20);
    CHECK(example.pttOutput.type == "none");
    CHECK(example.txTailMs == 150);
    CHECK(example.display.refreshMs == 250);
    CHECK(example.display.types == std::vector<std::string>{"console"});
    CHECK(example.logLevel == fdve::LogLevel::Info);
    CHECK(!example.startAnalog);
    CHECK(example.modeSwitch.type == "none");

    // Values override defaults.
    auto c = fdve::parseConfig(R"(
        [modem]
        callsign = "N0CALL"
        [audio.radio]
        sample_rate = 44100
        [ptt.input]
        type = "none"
        [ptt.output]
        type = "gpio"
        line = 27
        tx_delay_ms = 50
    )");
    CHECK(c.callsign == "N0CALL");
    CHECK(c.radio.sampleRate == 44100);
    CHECK(c.headset.sampleRate == 48000);
    CHECK(c.pttOutput.gpio.line == 27);
    CHECK(c.txDelayMs == 50);

    CHECK(fdve::parseConfig("[ptt.input]\ntype = \"console\"\n").pttInput.type == "console");
    CHECK(rejects("[ptt.input]\ntype = \"keyboard\"\n", "ptt.input.type"));

    // Log level, analog mode, mode switch, display list.
    const char* quiet = "[ptt.input]\ntype = \"none\"\n";
    auto d = fdve::parseConfig(std::string(quiet) + R"(
        [log]
        level = "debug"
        [modem]
        start_mode = "analog"
        [mode_switch]
        type = "gpio"
        line = 22
        action = "level"
        bias = "pull-up"
        [display]
        type = ["ssd1306", "console"]
        i2c_address = 0x3D
        rotate = true
    )");
    CHECK(d.logLevel == fdve::LogLevel::Debug);
    CHECK(d.startAnalog);
    CHECK(d.modeSwitch.type == "gpio" && d.modeSwitch.gpio.line == 22 && d.modeSwitch.action == "level");
    CHECK(d.modeSwitch.gpio.bias == fdve::GpioLine::Bias::PullUp);
    CHECK((d.display.types == std::vector<std::string>{"ssd1306", "console"}));
    CHECK(d.display.i2cAddress == 0x3D && d.display.rotate);
    CHECK(fdve::parseConfig(std::string(quiet) + "[display]\ntype = \"none\"\n").display.types.size() == 1);
    auto q = [&](const char* extra) { return std::string(quiet) + extra; };
    CHECK(rejects(q("[log]\nlevel = \"verbose\"\n").c_str(), "log.level"));
    CHECK(rejects(q("[modem]\nstart_mode = \"fm\"\n").c_str(), "modem.start_mode"));
    CHECK(rejects(q("[mode_switch]\ntype = \"gpio\"\n").c_str(), "'mode_switch.line' is required"));
    CHECK(rejects(q("[mode_switch]\naction = \"hold\"\n").c_str(), "mode_switch.action"));
    CHECK(rejects(q("[display]\ntype = [\"console\", 3]\n").c_str(), "only strings"));
    CHECK(rejects(q("[display]\ni2c_address = 0x80\n").c_str(), "between"));

    // Mistakes are errors, not silent defaults.
    CHECK(rejects("[modem]\nmod = \"RADEV1\"\n", "unknown key 'modem.mod'"));
    CHECK(rejects("[modme]\n", "unknown section 'modme'"));
    CHECK(rejects("[modem]\nmode = \"700D\"\n", "modem.mode"));
    CHECK(rejects("[audio.radio]\nsample_rate = \"48000\"\n", "wrong type"));
    CHECK(rejects("[audio.radio]\nsample_rate = 10\n", "between"));
    CHECK(rejects("[ptt.input]\ntype = \"gpio\"\n", "'ptt.input.line' is required"));
    CHECK(rejects("[ptt.input]\ntype = \"none\"\n[ptt.output]\ntype = \"hamlib\"\n", "ptt.output.type"));
    CHECK(rejects("[ptt.input]\ntype = \"none\"\n[display]\ntype = \"oled\"\n", "display.type"));
    CHECK(rejects("[modem\n", "line 1"));

    std::printf(failures == 0 ? "PASS\n" : "FAILED\n");
    return failures == 0 ? 0 : 1;
}
