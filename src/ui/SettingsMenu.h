#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>

#include "app/Config.h"
#include "audio/Mixer.h"
#include "ui/Menu.h"
#include "util/SystemInfo.h"

namespace fdve {

// The settings menu: one submenu per section of the config file, plus volume.
//
// Changes are collected first and written to the config file on "Save &
// restart". The file is patched value by value, so comments and layout
// survive, and the result is parsed before it replaces the file: a
// combination the program would refuse at start-up is never saved.
//
// Volume is the exception. It acts on the sound card at once, like
// alsamixer, and is saved when leaving the volume menu, without a restart.
// How the settings menu looks at the hardware: sound devices and serial
// ports. The defaults ask the system; tests supply their own.
struct AudioHardware
{
    // Devices that can record (true) or play (false).
    std::function<std::vector<AlsaDevice>(bool capture)> list;
    // Tries to open a device. Returns "" when it opens, the reason otherwise.
    std::function<std::string(const std::string& device, bool capture, int sampleRate)> probe;

    // Serial ports a radio's control connection could be on.
    std::function<std::vector<std::string>()> serialPorts;

    static AudioHardware real();
};

class SettingsMenu
{
public:
    SettingsMenu(const Config& config, std::string configPath, AudioHardware hardware = AudioHardware::real());

    Menu& menu() { return menu_; }

    bool restartRequested() const { return restartRequested_.load(); }
    bool shutdownRequested() const { return shutdownRequested_.load(); }
    SystemInfo& system() { return system_; }

    // Applies the volumes stored in the config to the sound cards. Called at start-up.
    void applyStoredVolumes();

    // Headphone volume from the knob on the status screen: changes it by
    // "steps" detents and returns the new level in percent, or -1 when the
    // headset card has no playback volume control. The change acts at once;
    // saveVolumeChanges() writes it to the config file.
    int adjustHeadphones(int steps);
    void saveVolumeChanges();

private:
    MenuItem build();
    void set(const std::string& section, const std::string& key, const std::string& literal, bool needsRestart = true);
    bool save(std::string& error);
    void discard();

    // Builders for the common item types. Each records the change under its TOML key.
    MenuItem choice(const std::string& label, const std::string& section, const std::string& key, std::string& field,
                    std::vector<std::string> choices, std::vector<std::string> labels = {});
    MenuItem toggle(const std::string& label, const std::string& section, const std::string& key, bool& field);
    MenuItem number(const std::string& label, const std::string& section, const std::string& key, int& field,
                    int min, int max, int step, const std::string& unit);
    MenuItem biasChoice(const std::string& label, const std::string& section, GpioLine::Bias& field);
    MenuItem deviceChoice(const std::string& label, const std::string& section, const std::string& key,
                          std::string& field, bool capture);
    bool changedDevicesOpen(std::string& error) const;
    MenuItem rateChoice(const std::string& label, const std::string& section, int& field);
    MenuItem volume(const std::string& label, const std::string& section, const std::string& key, int& field,
                    Mixer& mixer, bool playback);

    Config saved_;  // what is in the file
    Config edited_; // with the pending changes
    std::string configPath_;
    std::map<std::pair<std::string, std::string>, std::string> pending_; // (section, key) -> TOML literal
    bool restartNeeded_ = false;
    std::atomic<bool> restartRequested_{false};
    std::atomic<bool> shutdownRequested_{false};
    SystemInfo system_;

    AudioHardware hardware_;
    std::unique_ptr<Mixer> radioMixer_;
    std::unique_ptr<Mixer> headsetMixer_;
    Menu menu_;
};

} // namespace fdve
