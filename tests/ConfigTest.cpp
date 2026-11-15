#include <cstdio>
#include <stdexcept>
#include <string>

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
    CHECK(example.display.refreshMs == 500);

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
