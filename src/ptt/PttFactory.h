#pragma once

#include <memory>
#include <string>

#include "ptt/GpioLine.h"
#include "ptt/IPtt.h"

namespace fdve {

struct PttInputConfig
{
    std::string type = "gpio"; // "gpio" or "none"
    GpioLine::Config gpio;
};

struct PttOutputConfig
{
    std::string type = "none"; // "none" (radio VOX) or "gpio"
    GpioLine::Config gpio;
};

// Add new PTT sources (CMX108, serial RTS/DTR, ...) and sinks (hamlib,
// rigctld, CAT, ...) here.
std::unique_ptr<IPttInput> createPttInput(const PttInputConfig& config);
std::unique_ptr<IPttOutput> createPttOutput(const PttOutputConfig& config);

} // namespace fdve
