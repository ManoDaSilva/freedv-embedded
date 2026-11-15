#include "display/StatusScreen.h"

#include <algorithm>
#include <string>

namespace fdve {

namespace {

// Two-colour OLED modules have a strip of 16 rows in one colour (usually
// yellow) above 48 rows in another (usually blue). The layout keeps a header
// inside the strip and everything else below it. Nothing is drawn on row 16,
// so no element straddles the colour change. On a single colour display the
// same layout simply reads as a header and a body.
constexpr int kHeaderHeight = 16;
constexpr int kBodyTop = kHeaderHeight + 1;

constexpr float kBarFloorDbfs = -60.0f;
constexpr int kBarY = 45;
constexpr int kBarHeight = 14;

float levelFraction(float dbfs)
{
    return std::clamp((dbfs - kBarFloorDbfs) / -kBarFloorDbfs, 0.0f, 1.0f);
}

// Level bar from -60 to 0 dBFS, with a mark at -10 dBFS.
void levelBar(Framebuffer& fb, const char* label, float dbfs)
{
    const int x = 24;
    const int w = Framebuffer::Width - x;
    fb.text(0, kBarY + 3, label);
    fb.rect(x, kBarY, w, kBarHeight);

    const float fraction = levelFraction(dbfs);
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

void renderVolumeScreen(int percent, Framebuffer& fb)
{
    fb.clear();
    percent = std::clamp(percent, 0, 100);
    fb.text(0, 1, "Volume", 2); // header strip

    const std::string value = std::to_string(percent);
    const int width = Framebuffer::textWidth(value, 3);
    const int x = (Framebuffer::Width - width - 10) / 2;
    fb.text(x, kBodyTop + 1, value, 3);
    fb.text(x + width + 4, kBodyTop + 15, "%");

    fb.rect(0, kBarY, Framebuffer::Width, kBarHeight);
    fb.fillRect(2, kBarY + 2, percent * (Framebuffer::Width - 4) / 100, kBarHeight - 4);
}

float statusLevelFraction(const ModemStatus& s)
{
    return levelFraction(s.state != ModemState::Receive ? s.micDbfs : s.radioInDbfs);
}

void renderStatusScreen(const ModemStatus& s, Framebuffer& fb)
{
    fb.clear();
    const bool transmitting = s.state != ModemState::Receive;

    // ---- Header strip (rows 0 to 15) ----------------------------------------
    // RX/TX on the left, inverted while transmitting. Callsign on the right:
    // the last one heard while receiving, our own while transmitting.
    if (transmitting) fb.fillRect(0, 0, 27, kHeaderHeight);
    fb.text(2, 1, transmitting ? "TX" : "RX", 2, !transmitting);

    std::string callsign = transmitting ? s.ownCallsign : s.rxCallsign;
    if (callsign.size() > 8) callsign.resize(8);
    fb.text(Framebuffer::Width - Framebuffer::textWidth(callsign, 2), 1, callsign, 2);

    // ---- Body (rows 17 to 63) -------------------------------------------------
    // Main area, large. Receiving: the SNR. Transmitting: an inverted TX band.
    if (transmitting)
    {
        fb.fillRect(0, kBodyTop, Framebuffer::Width, 23);
        centred(fb, kBodyTop + 1, "TX", 3, false);
        if (s.state == ModemState::TransmitEnding) fb.text(2, kBodyTop + 8, "END", 1, false);
    }
    else if (s.analog)
    {
        centred(fb, kBodyTop + 4, "ANALOG", 2);
    }
    else
    {
        // Mode and sync, small, on the left. SNR, large, on the right.
        fb.text(0, kBodyTop + 2, s.modeName);
        if (s.sync) fb.text(0, kBodyTop + 12, "SYNC");
        const std::string value = s.sync ? std::to_string(s.snrDb) : "--";
        const int right = 108;
        fb.text(right - Framebuffer::textWidth(value, 3), kBodyTop + 1, value, 3);
        fb.text(right + 4, kBodyTop + 15, "dB");
    }

    // One level bar: what comes from the radio while receiving, the
    // microphone while transmitting (the radio input carries nothing then).
    if (transmitting) levelBar(fb, "MIC", s.micDbfs);
    else levelBar(fb, "AF", s.radioInDbfs);
}

} // namespace fdve
