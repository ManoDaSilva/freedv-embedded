#include "ptt/PttFactory.h"

#include <stdexcept>

#include "ptt/ConsoleKeys.h"

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

class ConsolePttInput : public IPttInput
{
public:
    ConsolePttInput() : keys_(ConsoleKeys::instance()) {}
    bool pressed() override { return keys_->transmit(); }

private:
    std::shared_ptr<ConsoleKeys> keys_;
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

class FixedModeSwitch : public IModeSwitch
{
public:
    explicit FixedModeSwitch(bool analog) : analog_(analog) {}
    bool analog() override { return analog_; }

private:
    bool analog_;
};

class GpioModeSwitch : public IModeSwitch
{
public:
    GpioModeSwitch(const GpioLine::Config& config, bool toggle, bool startAnalog)
        : line_(config, GpioLine::Direction::Input, "freedv-mode"), toggle_(toggle), analog_(startAnalog) {}

    bool analog() override
    {
        const bool active = line_.get();
        if (!toggle_) return active;
        if (active && !wasActive_) analog_ = !analog_; // each press flips the mode
        wasActive_ = active;
        return analog_;
    }

private:
    GpioLine line_;
    bool toggle_;
    bool analog_;
    bool wasActive_ = false;
};

class ConsoleModeSwitch : public IModeSwitch
{
public:
    explicit ConsoleModeSwitch(bool startAnalog) : keys_(ConsoleKeys::instance()), analog_(startAnalog) {}
    bool analog() override
    {
        while (keys_->analogToggled()) analog_ = !analog_;
        return analog_;
    }

private:
    std::shared_ptr<ConsoleKeys> keys_;
    bool analog_;
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

std::unique_ptr<IModeSwitch> createModeSwitch(const ModeSwitchConfig& config, bool startAnalog)
{
    if (config.type == "none") return std::make_unique<FixedModeSwitch>(startAnalog);
    if (config.type == "gpio")
        return std::make_unique<GpioModeSwitch>(config.gpio, config.action == "toggle", startAnalog);
    if (config.type == "console") return std::make_unique<ConsoleModeSwitch>(startAnalog);
    throw std::runtime_error("unknown mode_switch type '" + config.type + "'");
}

} // namespace fdve
