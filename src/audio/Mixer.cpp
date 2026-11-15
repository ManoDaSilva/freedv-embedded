#include "audio/Mixer.h"

#include <algorithm>

#ifdef FDVE_WITH_ALSA
#include <alsa/asoundlib.h>
#endif

namespace fdve {

std::string mixerCardForDevice(const std::string& pcmDevice)
{
    const size_t colon = pcmDevice.find(':');
    if (colon == std::string::npos) return "";
    const std::string plugin = pcmDevice.substr(0, colon);
    if (plugin != "hw" && plugin != "plughw") return "";
    std::string card = pcmDevice.substr(colon + 1);
    card = card.substr(0, card.find(','));
    return card.empty() ? "" : "hw:" + card;
}

#ifdef FDVE_WITH_ALSA

namespace {

snd_mixer_elem_t* findControl(snd_mixer_t* mixer, const std::string& wanted, bool playback)
{
    // Order of preference when no name is given.
    static const char* const kPlayback[] = {"Master", "Speaker", "Headphone", "PCM"};
    static const char* const kCapture[] = {"Mic", "Capture", "Line"};

    snd_mixer_elem_t* firstUsable = nullptr;
    snd_mixer_elem_t* best = nullptr;
    int bestRank = 1000;
    for (snd_mixer_elem_t* e = snd_mixer_first_elem(mixer); e != nullptr; e = snd_mixer_elem_next(e))
    {
        if (!snd_mixer_selem_is_active(e)) continue;
        const bool usable = playback ? snd_mixer_selem_has_playback_volume(e) : snd_mixer_selem_has_capture_volume(e);
        if (!usable) continue;
        const std::string name = snd_mixer_selem_get_name(e);
        if (!wanted.empty())
        {
            if (name == wanted) return e;
            continue;
        }
        if (firstUsable == nullptr) firstUsable = e;
        for (int rank = 0; rank < (playback ? 4 : 3); rank++)
        {
            if (name == (playback ? kPlayback[rank] : kCapture[rank]) && rank < bestRank)
            {
                best = e;
                bestRank = rank;
            }
        }
    }
    return best != nullptr ? best : firstUsable;
}

int toPercent(long value, long min, long max)
{
    if (max <= min) return 0;
    return static_cast<int>((value - min) * 100 / (max - min));
}

long fromPercent(int percent, long min, long max)
{
    percent = std::clamp(percent, 0, 100);
    return min + (max - min) * percent / 100;
}

} // namespace

std::vector<AlsaCard> listAlsaCards()
{
    std::vector<AlsaCard> cards;
    int index = -1;
    while (snd_card_next(&index) == 0 && index >= 0)
    {
        snd_ctl_t* ctl = nullptr;
        const std::string name = "hw:" + std::to_string(index);
        if (snd_ctl_open(&ctl, name.c_str(), 0) < 0) continue;
        snd_ctl_card_info_t* info;
        snd_ctl_card_info_alloca(&info);
        if (snd_ctl_card_info(ctl, info) == 0)
            cards.push_back({snd_ctl_card_info_get_id(info), snd_ctl_card_info_get_name(info)});
        snd_ctl_close(ctl);
    }
    return cards;
}

std::vector<AlsaDevice> listAlsaDevices(bool capture)
{
    std::vector<AlsaDevice> devices;
    int index = -1;
    while (snd_card_next(&index) == 0 && index >= 0)
    {
        snd_ctl_t* ctl = nullptr;
        if (snd_ctl_open(&ctl, ("hw:" + std::to_string(index)).c_str(), 0) < 0) continue;
        snd_ctl_card_info_t* card;
        snd_ctl_card_info_alloca(&card);
        if (snd_ctl_card_info(ctl, card) == 0)
        {
            const std::string id = snd_ctl_card_info_get_id(card);
            int device = -1;
            while (snd_ctl_pcm_next_device(ctl, &device) == 0 && device >= 0)
            {
                // Asking for the PCM in one direction fails if it does not exist.
                snd_pcm_info_t* pcm;
                snd_pcm_info_alloca(&pcm);
                snd_pcm_info_set_device(pcm, device);
                snd_pcm_info_set_subdevice(pcm, 0);
                snd_pcm_info_set_stream(pcm, capture ? SND_PCM_STREAM_CAPTURE : SND_PCM_STREAM_PLAYBACK);
                if (snd_ctl_pcm_info(ctl, pcm) < 0) continue;
                devices.push_back({"plughw:CARD=" + id + ",DEV=" + std::to_string(device),
                                   device == 0 ? id : id + "," + std::to_string(device)});
            }
        }
        snd_ctl_close(ctl);
    }
    return devices;
}

Mixer::Mixer(const std::string& card, const std::string& playbackControl, const std::string& captureControl)
{
    if (card.empty()) return;
    snd_mixer_t* mixer = nullptr;
    if (snd_mixer_open(&mixer, 0) < 0) return;
    if (snd_mixer_attach(mixer, card.c_str()) < 0 || snd_mixer_selem_register(mixer, nullptr, nullptr) < 0 ||
        snd_mixer_load(mixer) < 0)
    {
        snd_mixer_close(mixer);
        return;
    }
    handle_ = mixer;
    playback_ = findControl(mixer, playbackControl, true);
    capture_ = findControl(mixer, captureControl, false);
}

Mixer::~Mixer()
{
    if (handle_ != nullptr) snd_mixer_close(static_cast<snd_mixer_t*>(handle_));
}

std::string Mixer::playbackControl() const
{
    return playback_ ? snd_mixer_selem_get_name(static_cast<snd_mixer_elem_t*>(playback_)) : "";
}

std::string Mixer::captureControl() const
{
    return capture_ ? snd_mixer_selem_get_name(static_cast<snd_mixer_elem_t*>(capture_)) : "";
}

int Mixer::playbackPercent()
{
    if (!playback_) return 0;
    auto e = static_cast<snd_mixer_elem_t*>(playback_);
    snd_mixer_handle_events(static_cast<snd_mixer_t*>(handle_)); // pick up changes made elsewhere
    long min = 0, max = 0, value = 0;
    snd_mixer_selem_get_playback_volume_range(e, &min, &max);
    snd_mixer_selem_get_playback_volume(e, SND_MIXER_SCHN_FRONT_LEFT, &value);
    return toPercent(value, min, max);
}

int Mixer::capturePercent()
{
    if (!capture_) return 0;
    auto e = static_cast<snd_mixer_elem_t*>(capture_);
    snd_mixer_handle_events(static_cast<snd_mixer_t*>(handle_));
    long min = 0, max = 0, value = 0;
    snd_mixer_selem_get_capture_volume_range(e, &min, &max);
    snd_mixer_selem_get_capture_volume(e, SND_MIXER_SCHN_FRONT_LEFT, &value);
    return toPercent(value, min, max);
}

void Mixer::setPlaybackPercent(int percent)
{
    if (!playback_) return;
    auto e = static_cast<snd_mixer_elem_t*>(playback_);
    long min = 0, max = 0;
    snd_mixer_selem_get_playback_volume_range(e, &min, &max);
    snd_mixer_selem_set_playback_volume_all(e, fromPercent(percent, min, max));
    if (percent > 0 && snd_mixer_selem_has_playback_switch(e)) snd_mixer_selem_set_playback_switch_all(e, 1);
}

void Mixer::setCapturePercent(int percent)
{
    if (!capture_) return;
    auto e = static_cast<snd_mixer_elem_t*>(capture_);
    long min = 0, max = 0;
    snd_mixer_selem_get_capture_volume_range(e, &min, &max);
    snd_mixer_selem_set_capture_volume_all(e, fromPercent(percent, min, max));
    if (percent > 0 && snd_mixer_selem_has_capture_switch(e)) snd_mixer_selem_set_capture_switch_all(e, 1);
}

#else // no ALSA: nothing to control

std::vector<AlsaCard> listAlsaCards() { return {}; }
std::vector<AlsaDevice> listAlsaDevices(bool) { return {}; }
Mixer::Mixer(const std::string&, const std::string&, const std::string&) {}
Mixer::~Mixer() {}
std::string Mixer::playbackControl() const { return ""; }
std::string Mixer::captureControl() const { return ""; }
int Mixer::playbackPercent() { return 0; }
int Mixer::capturePercent() { return 0; }
void Mixer::setPlaybackPercent(int) {}
void Mixer::setCapturePercent(int) {}

#endif

} // namespace fdve
