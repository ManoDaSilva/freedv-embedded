#pragma once

#include <memory>
#include <string>

#include "ptt/GpioLine.h"
#include "ptt/IPtt.h"
#include "rig/IRig.h"

namespace fdve {

struct PttInputConfig
{
    std::string type = "gpio"; // "gpio", "console" or "none"
    GpioLine::Config gpio;
};

struct PttOutputConfig
{
    std::string type = "none"; // "none" (radio VOX), "gpio" or "rig" (CAT, see [rig])
    GpioLine::Config gpio;
};

struct ModeSwitchConfig
{
    std::string type = "none";     // "none", "gpio" or "console"
    GpioLine::Config gpio;
    std::string action = "toggle"; // gpio only: "toggle" (push button) or "level" (latching switch)
};

// Add new PTT sources (CMX108, serial RTS/DTR, ...) and sinks (hamlib,
// rigctld, CAT, ...) here.
std::unique_ptr<IPttInput> createPttInput(const PttInputConfig& config);
// rig: the radio connection, used by type "rig".
std::unique_ptr<IPttOutput> createPttOutput(const PttOutputConfig& config, std::shared_ptr<IRig> rig);

// startAnalog is the mode at start-up. A gpio switch with action "level"
// ignores it: the switch position decides.
std::unique_ptr<IModeSwitch> createModeSwitch(const ModeSwitchConfig& config, bool startAnalog);

} // namespace fdve
