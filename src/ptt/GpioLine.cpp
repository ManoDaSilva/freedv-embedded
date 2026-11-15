#include "ptt/GpioLine.h"

#include <fcntl.h>
#include <linux/gpio.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <stdexcept>

namespace fdve {

GpioLine::GpioLine(const Config& config, Direction direction, const char* consumer)
{
    if (config.line < 0) throw std::runtime_error("gpio: 'line' is not set");

    int chipFd = open(config.chip.c_str(), O_RDWR | O_CLOEXEC);
    if (chipFd < 0)
        throw std::runtime_error("gpio: cannot open " + config.chip + ": " + std::strerror(errno));

    struct gpio_v2_line_request req;
    std::memset(&req, 0, sizeof(req));
    req.offsets[0] = config.line;
    req.num_lines = 1;
    std::strncpy(req.consumer, consumer, sizeof(req.consumer) - 1);

    if (direction == Direction::Input)
    {
        req.config.flags = GPIO_V2_LINE_FLAG_INPUT;
        if (config.bias == Bias::PullUp) req.config.flags |= GPIO_V2_LINE_FLAG_BIAS_PULL_UP;
        if (config.bias == Bias::PullDown) req.config.flags |= GPIO_V2_LINE_FLAG_BIAS_PULL_DOWN;
        if (config.debounceMs > 0)
        {
            req.config.num_attrs = 1;
            req.config.attrs[0].mask = 1;
            req.config.attrs[0].attr.id = GPIO_V2_LINE_ATTR_ID_DEBOUNCE;
            req.config.attrs[0].attr.debounce_period_us = config.debounceMs * 1000;
        }
    }
    else
    {
        req.config.flags = GPIO_V2_LINE_FLAG_OUTPUT; // starts inactive
    }
    if (config.activeLow) req.config.flags |= GPIO_V2_LINE_FLAG_ACTIVE_LOW;

    int rc = ioctl(chipFd, GPIO_V2_GET_LINE_IOCTL, &req);
    int err = errno;
    close(chipFd);
    if (rc < 0)
        throw std::runtime_error("gpio: cannot request line " + std::to_string(config.line) + " on " +
                                 config.chip + ": " + std::strerror(err));
    fd_ = req.fd;
}

GpioLine::~GpioLine()
{
    if (fd_ >= 0) close(fd_);
}

bool GpioLine::get()
{
    struct gpio_v2_line_values values;
    values.mask = 1;
    values.bits = 0;
    if (ioctl(fd_, GPIO_V2_LINE_GET_VALUES_IOCTL, &values) < 0)
        throw std::runtime_error(std::string("gpio: read failed: ") + std::strerror(errno));
    return (values.bits & 1) != 0;
}

void GpioLine::set(bool active)
{
    struct gpio_v2_line_values values;
    values.mask = 1;
    values.bits = active ? 1 : 0;
    if (ioctl(fd_, GPIO_V2_LINE_SET_VALUES_IOCTL, &values) < 0)
        throw std::runtime_error(std::string("gpio: write failed: ") + std::strerror(errno));
}

} // namespace fdve
