#include "ptt/ConsoleKeys.h"

#include <unistd.h>

#include <cstdio>
#include <stdexcept>

namespace fdve {

std::shared_ptr<ConsoleKeys> ConsoleKeys::instance()
{
    static std::weak_ptr<ConsoleKeys> shared;
    auto keys = shared.lock();
    if (!keys)
    {
        keys = std::shared_ptr<ConsoleKeys>(new ConsoleKeys());
        shared = keys;
    }
    return keys;
}

ConsoleKeys::ConsoleKeys()
{
    if (!isatty(STDIN_FILENO) || tcgetattr(STDIN_FILENO, &saved_) != 0)
        throw std::runtime_error("input type \"console\" needs a terminal on standard input");

    // Deliver keys one at a time, without echo and without waiting. Ctrl+C
    // keeps working because signal keys stay enabled.
    struct termios raw = saved_;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    std::fprintf(stderr, "console keys: space = transmit on/off, a = analog on/off\n");
}

ConsoleKeys::~ConsoleKeys()
{
    tcsetattr(STDIN_FILENO, TCSANOW, &saved_);
}

void ConsoleKeys::poll()
{
    char key;
    while (read(STDIN_FILENO, &key, 1) == 1)
    {
        if (key == ' ') transmit_ = !transmit_;
        if (key == 'a' || key == 'A') analogPresses_++;
    }
}

bool ConsoleKeys::analogToggled()
{
    poll();
    if (analogPresses_ == 0) return false;
    analogPresses_--;
    return true;
}

} // namespace fdve
