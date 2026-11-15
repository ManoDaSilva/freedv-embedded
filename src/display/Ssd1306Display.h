#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "display/Framebuffer.h"
#include "display/IDisplay.h"

namespace fdve {

// 128x64 SSD1306 OLED on I2C (the common 0.96" modules). Also works with
// controllers that accept the same commands, such as SSD1309.
class Ssd1306Display : public IDisplay
{
public:
    explicit Ssd1306Display(const DisplayConfig& config);
    ~Ssd1306Display() override;
    void update(const ModemStatus& status) override;

private:
    void command(std::initializer_list<uint8_t> bytes);
    void sendPage(int page, const uint8_t* data);

    int fd_ = -1;
    Framebuffer fb_;
    std::array<uint8_t, Framebuffer::Width * Framebuffer::Pages> sent_{};
    bool firstFrame_ = true;
    bool failed_ = false;
};

} // namespace fdve
