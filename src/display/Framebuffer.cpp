#include "display/Framebuffer.h"

#include "display/Font5x7.h"

namespace fdve {

void Framebuffer::clear()
{
    data_.fill(0);
}

void Framebuffer::setPixel(int x, int y, bool on)
{
    if (x < 0 || x >= Width || y < 0 || y >= Height) return;
    uint8_t& byte = data_[x + (y / 8) * Width];
    const uint8_t mask = static_cast<uint8_t>(1u << (y % 8));
    if (on) byte |= mask;
    else byte &= static_cast<uint8_t>(~mask);
}

bool Framebuffer::pixel(int x, int y) const
{
    if (x < 0 || x >= Width || y < 0 || y >= Height) return false;
    return (data_[x + (y / 8) * Width] >> (y % 8)) & 1;
}

void Framebuffer::fillRect(int x, int y, int w, int h, bool on)
{
    for (int yy = y; yy < y + h; yy++)
        for (int xx = x; xx < x + w; xx++) setPixel(xx, yy, on);
}

void Framebuffer::rect(int x, int y, int w, int h)
{
    fillRect(x, y, w, 1);
    fillRect(x, y + h - 1, w, 1);
    fillRect(x, y, 1, h);
    fillRect(x + w - 1, y, 1, h);
}

int Framebuffer::textWidth(const std::string& text, int scale)
{
    if (text.empty()) return 0;
    return (static_cast<int>(text.size()) * 6 - 1) * scale;
}

void Framebuffer::text(int x, int y, const std::string& text, int scale, bool on)
{
    for (char c : text)
    {
        const unsigned char code = static_cast<unsigned char>(c);
        const uint8_t* glyph = kFont5x7[(code < 32 || code > 126) ? ('?' - 32) : (code - 32)];
        for (int col = 0; col < 5; col++)
            for (int row = 0; row < 7; row++)
                if ((glyph[col] >> row) & 1) fillRect(x + col * scale, y + row * scale, scale, scale, on);
        x += 6 * scale;
    }
}

std::string Framebuffer::toPbm() const
{
    std::string out = "P1\n" + std::to_string(Width) + " " + std::to_string(Height) + "\n";
    for (int y = 0; y < Height; y++)
    {
        for (int x = 0; x < Width; x++) out += pixel(x, y) ? '1' : '0';
        out += '\n';
    }
    return out;
}

} // namespace fdve
