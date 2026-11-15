#pragma once

#include <string>
#include <vector>

namespace fdve {

struct AlsaCard
{
    std::string id;   // short id, as in "plughw:CARD=<id>,DEV=0"
    std::string name; // descriptive name
};

// Sound cards present right now. Empty when built without ALSA.
std::vector<AlsaCard> listAlsaCards();

struct AlsaDevice
{
    std::string device; // name to open, for example "plughw:CARD=Device,DEV=0"
    std::string label;  // short name for a small screen
};

// Devices that can record (capture = true) or play (capture = false). A card
// that only plays, such as the Pi's HDMI output, is not offered for recording.
std::vector<AlsaDevice> listAlsaDevices(bool capture);

// "plughw:CARD=Device,DEV=0" -> "hw:CARD=Device", "plughw:1,0" -> "hw:1".
// Returns "" when the name does not point at one card (for example "default").
std::string mixerCardForDevice(const std::string& pcmDevice);

// Volume controls of one sound card, the same ones alsamixer shows.
// Percentages are of the control's raw range, like "amixer set X 80%".
class Mixer
{
public:
    // card: as returned by mixerCardForDevice(). playbackControl and
    // captureControl name the controls to use; empty picks the first sensible one.
    Mixer(const std::string& card, const std::string& playbackControl = "", const std::string& captureControl = "");
    ~Mixer();
    Mixer(const Mixer&) = delete;
    Mixer& operator=(const Mixer&) = delete;

    bool hasPlayback() const { return playback_ != nullptr; }
    bool hasCapture() const { return capture_ != nullptr; }
    std::string playbackControl() const;
    std::string captureControl() const;

    int playbackPercent();
    int capturePercent();
    void setPlaybackPercent(int percent); // also unmutes when above zero
    void setCapturePercent(int percent);

private:
    void* handle_ = nullptr;   // snd_mixer_t*
    void* playback_ = nullptr; // snd_mixer_elem_t*
    void* capture_ = nullptr;
};

} // namespace fdve
