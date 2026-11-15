#pragma once

#include <termios.h>

#include <memory>

namespace fdve {

// Keyboard input for bench tests. One instance owns the terminal, so the PTT
// input and the mode switch can both use the keyboard:
//   space  toggles transmit
//   a      toggles analog mode
// These are toggles because a terminal reports key presses, not releases.
class ConsoleKeys
{
public:
    static std::shared_ptr<ConsoleKeys> instance();
    ~ConsoleKeys();

    bool transmit() { poll(); return transmit_; }
    bool analogToggled(); // true once per press of "a"

private:
    ConsoleKeys();
    void poll();
    struct termios saved_;
    bool transmit_ = false;
    int analogPresses_ = 0;
};

} // namespace fdve
