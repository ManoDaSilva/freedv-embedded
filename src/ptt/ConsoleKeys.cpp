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
    std::fprintf(stderr, "console keys: space = transmit, a = analog, b = open menu/back, arrows = turn, enter = press\n");
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
        // Arrow keys arrive as ESC [ A (up) and ESC [ B (down).
        if (escape_ == 1) { escape_ = key == '[' ? 2 : 0; continue; }
        if (escape_ == 2)
        {
            if (key == 'A') menuSteps_--;
            if (key == 'B') menuSteps_++;
            escape_ = 0;
            continue;
        }
        if (key == 27) { escape_ = 1; continue; }

        if (key == ' ') transmit_ = !transmit_;
        if (key == 'a' || key == 'A') analogPresses_++;
        if (key == '-') menuSteps_--;
        if (key == '+' || key == '=') menuSteps_++;
        if (key == '\n' || key == '\r') menuClicks_++;
        if (key == 'b' || key == 'B' || key == 127) menuBacks_++;
    }
}

void ConsoleKeys::takeMenuKeys(int& steps, int& clicks, int& backs)
{
    poll();
    steps += menuSteps_;
    clicks += menuClicks_;
    backs += menuBacks_;
    menuSteps_ = menuClicks_ = menuBacks_ = 0;
}

bool ConsoleKeys::analogToggled()
{
    poll();
    if (analogPresses_ == 0) return false;
    analogPresses_--;
    return true;
}

} // namespace fdve
