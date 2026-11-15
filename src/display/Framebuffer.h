#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace fdve {

// 128x64 one bit canvas. The memory layout is the one SSD1306-type
// controllers use (8 pages of 128 bytes, bit 0 = top row of the page), so a
// driver can send it as is. Nothing here touches hardware.
class Framebuffer
{
public:
    static constexpr int Width = 128;
    static constexpr int Height = 64;
    static constexpr int Pages = Height / 8;

    void clear();
    void setPixel(int x, int y, bool on = true);
    bool pixel(int x, int y) const;
    void fillRect(int x, int y, int w, int h, bool on = true);
    void rect(int x, int y, int w, int h);

    // 5x7 font in a 6x8 cell, scaled by an integer factor.
    static int textWidth(const std::string& text, int scale = 1);
    void text(int x, int y, const std::string& text, int scale = 1, bool on = true);

    const uint8_t* page(int index) const { return data_.data() + index * Width; }
    std::string toPbm() const; // portable bitmap, for tests and screenshots

private:
    std::array<uint8_t, Width * Pages> data_{};
};

} // namespace fdve
