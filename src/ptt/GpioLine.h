#pragma once

#include <string>

namespace fdve {

// One GPIO line through the Linux GPIO character device (uAPI v2, kernel 5.10+).
// It talks to the kernel directly, so there is no libgpiod version to match.
class GpioLine
{
public:
    enum class Direction { Input, Output };
    enum class Bias { None, PullUp, PullDown };

    struct Config
    {
        std::string chip = "/dev/gpiochip0";
        int line = -1;
        bool activeLow = false;
        Bias bias = Bias::None; // inputs only
        int debounceMs = 0;     // inputs only
    };

    GpioLine(const Config& config, Direction direction, const char* consumer);
    ~GpioLine();
    GpioLine(const GpioLine&) = delete;
    GpioLine& operator=(const GpioLine&) = delete;

    bool get();           // logical value (active-low already applied)
    void set(bool active);

private:
    int fd_ = -1;
};

} // namespace fdve
