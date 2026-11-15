#pragma once

#include <memory>
#include <string>

#include "modem/ModemStatus.h"

namespace fdve {

// A display gets a status snapshot at a fixed interval, from the control
// thread. A driver that is slow (I2C OLED) can simply block in update().
class IDisplay
{
public:
    virtual ~IDisplay() = default;
    virtual void update(const ModemStatus& status) = 0;
};

struct DisplayConfig
{
    std::string type = "console"; // "console" or "none"
    int refreshMs = 500;
};

// Add new displays (SSD1306 over I2C, character LCD, ...) here.
std::unique_ptr<IDisplay> createDisplay(const DisplayConfig& config);

} // namespace fdve
