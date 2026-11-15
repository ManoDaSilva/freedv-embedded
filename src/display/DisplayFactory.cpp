#include <stdexcept>

#include "display/ConsoleDisplay.h"
#include "display/IDisplay.h"

namespace fdve {

namespace {

class NullDisplay : public IDisplay
{
public:
    void update(const ModemStatus&) override {}
};

} // namespace

std::unique_ptr<IDisplay> createDisplay(const DisplayConfig& config)
{
    if (config.type == "none") return std::make_unique<NullDisplay>();
    if (config.type == "console") return std::make_unique<ConsoleDisplay>();
    throw std::runtime_error("unknown display type '" + config.type + "'");
}

} // namespace fdve
