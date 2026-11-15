#pragma once

#include <chrono>
#include <memory>
#include <string>

namespace fdve {

// What the operator did since the last poll.
struct MenuEvents
{
    int steps = 0;  // encoder detents, positive = clockwise
    int clicks = 0; // short presses
    int backs = 0;  // long presses
    bool any() const { return steps != 0 || clicks != 0 || backs != 0; }
};

// Rotary encoder with push button, from whatever hardware provides it.
class IMenuInput
{
public:
    virtual ~IMenuInput() = default;
    virtual MenuEvents poll() = 0; // called every few milliseconds, must not block for long
};

struct MenuInputConfig
{
    std::string type = "none"; // "none", "gpio", "rackui" or "console"

    // gpio: encoder phases A and B and the push button, all on one chip
    std::string chip = "/dev/gpiochip0";
    int lineA = -1;
    int lineB = -1;
    int lineButton = -1;
    bool activeLow = true;       // contacts to ground
    std::string bias = "pull-up";
    int stepsPerDetent = 4;      // quadrature transitions per click of the knob: 4, 2 or 1

    // rackui: Rack UI front panel module on I2C
    std::string i2cDevice = "/dev/i2c-1";
    int i2cAddress = 0x36;

    bool reverse = false;        // swap the turning direction
    int longPressMs = 600;       // hold this long for "back"
    int timeoutS = 30;           // leave the menu after this long without input
};

std::unique_ptr<IMenuInput> createMenuInput(const MenuInputConfig& config);

// Turns "button went down / is down / went up" into clicks and long presses.
// A click is reported on release, so that a long press is not also a click.
class PressTracker
{
public:
    explicit PressTracker(int longPressMs) : longPressMs_(longPressMs) {}

    // pressesSeen: presses counted by the hardware since the last call.
    // down: whether the button is down right now.
    void update(int pressesSeen, bool down, MenuEvents& events,
                std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now());

private:
    int longPressMs_;
    bool pending_ = false; // a press that is neither a click nor a long press yet
    std::chrono::steady_clock::time_point since_;
};

// Quadrature decoding, shared by encoder drivers and testable on its own.
class QuadratureDecoder
{
public:
    explicit QuadratureDecoder(int stepsPerDetent) : stepsPerDetent_(stepsPerDetent) {}
    int update(bool a, bool b); // returns detents completed: -1, 0 or +1

private:
    int stepsPerDetent_;
    int state_ = 0;
    int accumulated_ = 0;
};

} // namespace fdve
