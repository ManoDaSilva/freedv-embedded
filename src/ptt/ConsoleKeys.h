#pragma once

#include <termios.h>

#include <memory>

namespace fdve {

// Keyboard input for bench tests. One instance owns the terminal, so the PTT
// input and the mode switch can both use the keyboard:
//   space        toggles transmit
//   a            toggles analog mode
//   up / down    menu: turn the knob (also "-" and "+")
//   enter        knob: short press (switch mode; select in the menu)
//   b            knob: long press (open the menu; back in the menu)
// These are toggles because a terminal reports key presses, not releases.
class ConsoleKeys
{
public:
    static std::shared_ptr<ConsoleKeys> instance();
    ~ConsoleKeys();

    bool transmit() { poll(); return transmit_; }
    bool analogToggled(); // true once per press of "a"
    void takeMenuKeys(int& steps, int& clicks, int& backs); // adds what was typed since the last call

private:
    ConsoleKeys();
    void poll();
    struct termios saved_;
    bool transmit_ = false;
    int analogPresses_ = 0;
    int menuSteps_ = 0;
    int menuClicks_ = 0;
    int menuBacks_ = 0;
    int escape_ = 0; // progress through an "ESC [ x" arrow key sequence
};

} // namespace fdve
