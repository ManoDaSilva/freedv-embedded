#include "ptt/ConsolePttInput.h"

#include <fcntl.h>
#include <unistd.h>

#include <cstdio>
#include <stdexcept>

namespace fdve {

ConsolePttInput::ConsolePttInput()
{
    if (!isatty(STDIN_FILENO) || tcgetattr(STDIN_FILENO, &saved_) != 0)
        throw std::runtime_error("ptt.input type \"console\" needs a terminal on standard input");

    // Deliver keys one at a time, without echo and without waiting. Ctrl+C
    // keeps working because signal keys stay enabled.
    struct termios raw = saved_;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    std::fprintf(stderr, "console PTT: press space to start and stop transmitting\n");
}

ConsolePttInput::~ConsolePttInput()
{
    tcsetattr(STDIN_FILENO, TCSANOW, &saved_);
}

bool ConsolePttInput::pressed()
{
    char key;
    while (read(STDIN_FILENO, &key, 1) == 1)
    {
        if (key == ' ') transmit_ = !transmit_;
    }
    return transmit_;
}

} // namespace fdve
