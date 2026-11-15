#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
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

    // Indicator lights around the knob, for hardware that has them. The
    // caller repeats the call that applies; drivers only act on a change.
    virtual void showKnobPosition() {}            // lights follow the knob (menu open)
    virtual void showLevel(float /*fraction*/) {} // bar graph, 0.0 to 1.0 (status screen)

    // Separate transmit and receive lamps, for hardware that has them.
    virtual void showState(bool /*transmitting*/, bool /*sync*/) {}
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

    std::string leds = "level";  // "level", "follow" or "off", see the example config
    int ledBrightness = 24;      // 1 to 64
    // The module's TX_EN and RX_EN lamps are wired to GPIO lines, not to I2C.
    // -1 leaves a lamp alone.
    int txLedLine = 22;          // header pin 15
    int rxLedLine = 23;          // header pin 16
    bool stateLedsActiveLow = false;

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

// Turns the Rack UI module's free-running counters into events, and rejects
// readings that cannot be trusted.
//
// The module is a microcontroller that stretches the I2C clock while it
// prepares each byte. The Raspberry Pi's I2C controller does not handle clock
// stretching reliably, so a read now and then returns wrong bytes without any
// error. Taken at face value, such a read looks like the knob was turned or
// pressed. Two rules keep those out:
//   - a changed reading only counts once a second read returns the same values
//   - a confirmed change larger than a hand can produce is taken as a module
//     reset: the counters are adopted, no events are reported
class RackUiDecoder
{
public:
    struct Reading
    {
        int16_t count = 0;  // registers 0x04-0x05
        uint8_t pushes = 0; // register 0x06
        bool down = false;  // register 0x07
        bool sameCounters(const Reading& other) const { return count == other.count && pushes == other.pushes; }
    };

    explicit RackUiDecoder(int longPressMs) : press_(longPressMs) {}

    void reset(const Reading& reading) { last_ = reading; }

    // first: this poll's reading. reread: reads the module again, false on a bus error.
    MenuEvents update(const Reading& first, const std::function<bool(Reading&)>& reread,
                      std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now());

    unsigned rejected() const { return rejected_; } // readings discarded so far

private:
    PressTracker press_;
    Reading last_;
    unsigned rejected_ = 0;
};

// Brightness for the module's 12 LEDs (registers 0x10 to 0x1B) showing a level
// as a bar. It uses the same arc as the module's own gauge animation: nine
// LEDs starting at LED 8, the other three stay off. The LED at the tip of the
// bar is dimmed in proportion, so the bar moves smoothly.
std::array<uint8_t, 12> rackUiLevelFrame(float fraction, int brightness);

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
