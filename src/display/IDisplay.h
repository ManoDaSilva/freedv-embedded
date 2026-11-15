#pragma once

#include <memory>
#include <string>
#include <vector>

#include "display/Framebuffer.h"
#include "modem/ModemStatus.h"

namespace fdve {

// A display is fed from the user interface thread. A slow driver (I2C OLED)
// can simply block in its calls.
class IDisplay
{
public:
    virtual ~IDisplay() = default;
    // Text displays use the status.
    virtual void update(const ModemStatus& status) = 0;
    // Graphical displays show the frame: the status screen or the menu.
    virtual void show(const Framebuffer&) {}
};

struct DisplayConfig
{
    std::vector<std::string> types = {"console"}; // any of "console", "ssd1306"; empty = none
    int refreshMs = 250;

    // ssd1306
    std::string i2cDevice = "/dev/i2c-1";
    int i2cAddress = 0x3C;
    bool rotate = false; // turn the picture 180 degrees
    int contrast = 0xCF;
};

// Add new displays here.
std::unique_ptr<IDisplay> createDisplay(const DisplayConfig& config);

} // namespace fdve
