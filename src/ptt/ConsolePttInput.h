#pragma once

#include <termios.h>

#include "ptt/IPtt.h"

namespace fdve {

// PTT from the keyboard, for bench tests: the space bar toggles transmit.
// It is a toggle because a terminal reports key presses but not key releases.
// Needs the program to run in a terminal (not under systemd).
class ConsolePttInput : public IPttInput
{
public:
    ConsolePttInput();
    ~ConsolePttInput() override;
    bool pressed() override;

private:
    struct termios saved_;
    bool transmit_ = false;
};

} // namespace fdve
