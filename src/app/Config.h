#pragma once

#include <string>
#include <string_view>

#include "audio/AudioFactory.h"
#include "display/IDisplay.h"
#include "ptt/PttFactory.h"

namespace fdve {

struct SoundCardConfig
{
    std::string type = "alsa";
    std::string captureDevice = "default";
    std::string playbackDevice = "default";
    int sampleRate = 48000;
};

struct Config
{
    std::string mode = "RADEV1";
    std::string callsign;

    SoundCardConfig radio;
    SoundCardConfig headset;
    int periodMs = 20;
    int prebufferMs = 160;

    PttInputConfig pttInput;
    PttOutputConfig pttOutput;
    int txDelayMs = 0;  // PTT out asserted -> modem signal starts
    int txTailMs = 150; // last modem sample queued -> PTT out released

    DisplayConfig display;
};

// Both throw std::runtime_error with a readable message on any problem,
// including unknown keys (so a typo does not silently fall back to a default).
Config parseConfig(std::string_view toml);
Config loadConfig(const std::string& path);

} // namespace fdve
