#include "display/StatusScreen.h"

#include <algorithm>
#include <string>

namespace fdve {

namespace {

constexpr float kBarFloorDbfs = -60.0f;
constexpr int kBarY = 34;
constexpr int kBarHeight = 12;

// Level bar from -60 to 0 dBFS, with a mark at -10 dBFS.
void levelBar(Framebuffer& fb, const char* label, float dbfs)
{
    const int x = 24;
    const int w = Framebuffer::Width - x;
    fb.text(0, kBarY + 2, label);
    fb.rect(x, kBarY, w, kBarHeight);

    const float fraction = std::clamp((dbfs - kBarFloorDbfs) / -kBarFloorDbfs, 0.0f, 1.0f);
    const int inner = w - 4;
    fb.fillRect(x + 2, kBarY + 2, static_cast<int>(fraction * inner + 0.5f), kBarHeight - 4);

    const int mark = x + 2 + static_cast<int>((-10.0f - kBarFloorDbfs) / -kBarFloorDbfs * inner);
    fb.fillRect(mark, kBarY - 2, 1, 2);
    fb.fillRect(mark, kBarY + kBarHeight, 1, 2);
}

void centred(Framebuffer& fb, int y, const std::string& text, int scale, bool on = true)
{
    fb.text((Framebuffer::Width - Framebuffer::textWidth(text, scale)) / 2, y, text, scale, on);
}

} // namespace

void renderStatusScreen(const ModemStatus& s, Framebuffer& fb)
{
    fb.clear();
    const bool transmitting = s.state != ModemState::Receive;

    // Header line, small: mode on the left, sync or over state on the right.
    fb.text(0, 0, s.analog ? "ANALOG" : s.modeName);
    std::string flag;
    if (s.state == ModemState::TransmitEnding) flag = "END";
    else if (!transmitting && !s.analog && s.sync) flag = "SYNC";
    fb.text(Framebuffer::Width - Framebuffer::textWidth(flag), 0, flag);

    // Main area, large. Receiving: the SNR. Transmitting: an inverted TX band.
    if (transmitting)
    {
        fb.fillRect(0, 9, Framebuffer::Width, 23);
        centred(fb, 10, "TX", 3, false);
    }
    else if (s.analog)
    {
        centred(fb, 13, "RX", 2);
    }
    else
    {
        fb.text(0, 17, "SNR");
        const std::string value = s.sync ? std::to_string(s.snrDb) : "--";
        const int right = 104;
        fb.text(right - Framebuffer::textWidth(value, 3), 10, value, 3);
        fb.text(right + 4, 24, "dB");
    }

    // One level bar: what comes from the radio while receiving, the
    // microphone while transmitting (the radio input carries nothing then).
    if (transmitting) levelBar(fb, "MIC", s.micDbfs);
    else levelBar(fb, "AF", s.radioInDbfs);

    // Bottom: callsign, large, centred.
    std::string callsign = transmitting ? s.ownCallsign : s.rxCallsign;
    if (callsign.size() > 10) callsign.resize(10);
    if (!callsign.empty()) centred(fb, 50, callsign, 2);
    else if (!s.analog && !transmitting) fb.text(0, 55, "no callsign heard yet");
}

} // namespace fdve
