#pragma once

#include <string>
#include <string_view>

#include "audio/AudioFactory.h"
#include "display/IDisplay.h"
#include "ptt/PttFactory.h"
#include "rig/IRig.h"
#include "ui/MenuInput.h"
#include "util/Log.h"

namespace fdve {

struct SoundCardConfig
{
    std::string type = "alsa";
    std::string captureDevice = "default";
    std::string playbackDevice = "default";
    int sampleRate = 48000;

    // Mixer levels in percent, applied at start-up and set from the menu.
    // -1 leaves the sound card as it is.
    int playbackVolume = -1;
    int captureVolume = -1;
    // Names of the mixer controls to use, as shown by alsamixer. Empty picks one.
    std::string playbackMixer;
    std::string captureMixer;
};

struct Config
{
    LogLevel logLevel = LogLevel::Info;

    std::string mode = "RADEV1";
    std::string callsign;
    bool startAnalog = false; // mode at start-up
    ModeSwitchConfig modeSwitch;

    SoundCardConfig radio;
    SoundCardConfig headset;
    int periodMs = 20;
    int prebufferMs = 160;

    RigConfig rig;

    PttInputConfig pttInput;
    PttOutputConfig pttOutput;
    int txDelayMs = 0;  // PTT out asserted -> modem signal starts
    int txTailMs = 150; // last modem sample queued -> PTT out released

    DisplayConfig display;
    MenuInputConfig menu;
};

// Both throw std::runtime_error with a readable message on any problem,
// including unknown keys (so a typo does not silently fall back to a default).
Config parseConfig(std::string_view toml);
Config loadConfig(const std::string& path);

} // namespace fdve
