#include "display/StatusScreen.h"

#include <algorithm>
#include <string>

namespace fdve {

namespace {

constexpr float kBarFloorDbfs = -60.0f;

// Level bar from -60 to 0 dBFS, with a notch at -10 dBFS as a "getting hot" mark.
void levelBar(Framebuffer& fb, int y, const char* label, float dbfs)
{
    const int x = 24;
    const int w = Framebuffer::Width - x;
    const int h = 8;
    fb.text(0, y, label);
    fb.rect(x, y, w, h);

    const float fraction = std::clamp((dbfs - kBarFloorDbfs) / -kBarFloorDbfs, 0.0f, 1.0f);
    const int inner = w - 4;
    fb.fillRect(x + 2, y + 2, static_cast<int>(fraction * inner + 0.5f), h - 4);

    const int notch = x + 2 + static_cast<int>((-10.0f - kBarFloorDbfs) / -kBarFloorDbfs * inner);
    fb.setPixel(notch, y - 1);
    fb.setPixel(notch, y + h);
}

} // namespace

void renderStatusScreen(const ModemStatus& s, Framebuffer& fb)
{
    fb.clear();
    const bool transmitting = s.state != ModemState::Receive;

    // Top band: RX/TX, large. Inverted while transmitting.
    if (transmitting) fb.fillRect(0, 0, 27, 16);
    fb.text(2, 1, transmitting ? "TX" : "RX", 2, !transmitting);

    // Mode and sync, small, next to it.
    fb.text(32, 0, s.analog ? "ANALOG" : s.modeName);
    if (!s.analog)
    {
        if (s.state == ModemState::TransmitEnding) fb.text(32, 9, "END");
        else if (!transmitting && s.sync) fb.text(32, 9, "SYNC");
    }

    // SNR, large, right aligned. Only meaningful with sync.
    if (!s.analog && !transmitting && s.sync)
    {
        const std::string snr = std::to_string(s.snrDb) + "dB";
        fb.text(Framebuffer::Width - Framebuffer::textWidth(snr, 2), 1, snr, 2);
    }

    levelBar(fb, 22, "AF", s.radioInDbfs);
    levelBar(fb, 35, "MIC", s.micDbfs);

    // Bottom: callsign, large, centred.
    std::string callsign = transmitting ? s.ownCallsign : s.rxCallsign;
    if (callsign.size() > 10) callsign.resize(10);
    if (callsign.empty())
    {
        if (!s.analog && !transmitting) fb.text(0, 53, "no callsign heard yet");
    }
    else
    {
        fb.text((Framebuffer::Width - Framebuffer::textWidth(callsign, 2)) / 2, 49, callsign, 2);
    }
}

} // namespace fdve
