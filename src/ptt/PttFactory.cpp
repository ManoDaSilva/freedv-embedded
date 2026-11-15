#include "ptt/PttFactory.h"

#include <stdexcept>

#include "ptt/ConsolePttInput.h"

namespace fdve {

namespace {

class NonePttInput : public IPttInput
{
public:
    bool pressed() override { return false; }
};

class NonePttOutput : public IPttOutput
{
public:
    void set(bool) override {}
};

class GpioPttInput : public IPttInput
{
public:
    explicit GpioPttInput(const GpioLine::Config& config)
        : line_(config, GpioLine::Direction::Input, "freedv-ptt-in") {}
    bool pressed() override { return line_.get(); }

private:
    GpioLine line_;
};

class GpioPttOutput : public IPttOutput
{
public:
    explicit GpioPttOutput(const GpioLine::Config& config)
        : line_(config, GpioLine::Direction::Output, "freedv-ptt-out") {}
    ~GpioPttOutput() override
    {
        try { line_.set(false); } catch (...) {}
    }
    void set(bool transmit) override { line_.set(transmit); }

private:
    GpioLine line_;
};

} // namespace

std::unique_ptr<IPttInput> createPttInput(const PttInputConfig& config)
{
    if (config.type == "none") return std::make_unique<NonePttInput>();
    if (config.type == "gpio") return std::make_unique<GpioPttInput>(config.gpio);
    if (config.type == "console") return std::make_unique<ConsolePttInput>();
    throw std::runtime_error("unknown ptt.input type '" + config.type + "'");
}

std::unique_ptr<IPttOutput> createPttOutput(const PttOutputConfig& config)
{
    if (config.type == "none") return std::make_unique<NonePttOutput>();
    if (config.type == "gpio") return std::make_unique<GpioPttOutput>(config.gpio);
    throw std::runtime_error("unknown ptt.output type '" + config.type + "'");
}

} // namespace fdve
