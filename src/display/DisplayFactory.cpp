#include <stdexcept>

#include "display/ConsoleDisplay.h"
#include "display/IDisplay.h"
#include "display/Ssd1306Display.h"

namespace fdve {

namespace {

// Zero, one or several displays behind one interface.
class DisplaySet : public IDisplay
{
public:
    void add(std::unique_ptr<IDisplay> display) { displays_.push_back(std::move(display)); }
    void update(const ModemStatus& status) override
    {
        for (auto& display : displays_) display->update(status);
    }

private:
    std::vector<std::unique_ptr<IDisplay>> displays_;
};

} // namespace

std::unique_ptr<IDisplay> createDisplay(const DisplayConfig& config)
{
    auto set = std::make_unique<DisplaySet>();
    for (const auto& type : config.types)
    {
        if (type == "console") set->add(std::make_unique<ConsoleDisplay>());
        else if (type == "ssd1306") set->add(std::make_unique<Ssd1306Display>(config));
        else if (type != "none") throw std::runtime_error("unknown display type '" + type + "'");
    }
    return set;
}

} // namespace fdve
