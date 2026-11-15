#include "ui/MenuInput.h"

#include <fcntl.h>
#include <linux/gpio.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <stdexcept>

#include "ptt/ConsoleKeys.h"
#include "util/Log.h"

namespace fdve {

// ---- PressTracker ------------------------------------------------------------

void PressTracker::update(int pressesSeen, bool down, MenuEvents& events,
                          std::chrono::steady_clock::time_point now)
{
    for (int i = 0; i < pressesSeen; i++)
    {
        if (pending_) events.clicks++; // the earlier press was released unseen
        pending_ = true;
        since_ = now;
    }
    if (!pending_) return;

    if (!down)
    {
        events.clicks++;
        pending_ = false;
    }
    else if (now - since_ >= std::chrono::milliseconds(longPressMs_))
    {
        events.backs++;
        pending_ = false;
    }
}

// ---- QuadratureDecoder -------------------------------------------------------

int QuadratureDecoder::update(bool a, bool b)
{
    // Index: previous state (2 bits) then new state (2 bits). Invalid jumps count as 0.
    // Clockwise is "A closes first": 00 -> 10 -> 11 -> 01 -> 00 on (A, B).
    static const int kDelta[16] = {0, -1, +1, 0, +1, 0, 0, -1, -1, 0, 0, +1, 0, +1, -1, 0};
    const int next = (a ? 2 : 0) | (b ? 1 : 0);
    accumulated_ += kDelta[(state_ << 2) | next];
    state_ = next;

    int detents = 0;
    if (accumulated_ >= stepsPerDetent_) { detents = 1; accumulated_ -= stepsPerDetent_; }
    else if (accumulated_ <= -stepsPerDetent_) { detents = -1; accumulated_ += stepsPerDetent_; }
    // Both contacts open is the rest position of a detented encoder. Drop any
    // partial count there so contact bounce cannot add up over time.
    if (next == 0 && stepsPerDetent_ == 4) accumulated_ = 0;
    return detents;
}

namespace {

// ---- none ----------------------------------------------------------------------

class NoMenuInput : public IMenuInput
{
public:
    MenuEvents poll() override { return {}; }
};

// ---- console -------------------------------------------------------------------

class ConsoleMenuInput : public IMenuInput
{
public:
    ConsoleMenuInput() : keys_(ConsoleKeys::instance()) {}
    MenuEvents poll() override
    {
        MenuEvents events;
        keys_->takeMenuKeys(events.steps, events.clicks, events.backs);
        return events;
    }

private:
    std::shared_ptr<ConsoleKeys> keys_;
};

// ---- gpio ----------------------------------------------------------------------

// Uses kernel edge events instead of polling the levels: the kernel time-stamps
// and queues every edge, so fast turns are not lost between two polls.
class GpioEncoderInput : public IMenuInput
{
public:
    explicit GpioEncoderInput(const MenuInputConfig& config)
        : config_(config), decoder_(config.stepsPerDetent), press_(config.longPressMs)
    {
        if (config.lineA < 0 || config.lineB < 0 || config.lineButton < 0)
            throw std::runtime_error("menu: line_a, line_b and line_button are required for type \"gpio\"");

        int chipFd = open(config.chip.c_str(), O_RDWR | O_CLOEXEC);
        if (chipFd < 0)
            throw std::runtime_error("menu: cannot open " + config.chip + ": " + std::strerror(errno));

        struct gpio_v2_line_request req;
        std::memset(&req, 0, sizeof(req));
        req.offsets[0] = config.lineA;
        req.offsets[1] = config.lineB;
        req.offsets[2] = config.lineButton;
        req.num_lines = 3;
        req.event_buffer_size = 256;
        std::strncpy(req.consumer, "freedv-menu", sizeof(req.consumer) - 1);
        req.config.flags = GPIO_V2_LINE_FLAG_INPUT | GPIO_V2_LINE_FLAG_EDGE_RISING | GPIO_V2_LINE_FLAG_EDGE_FALLING;
        if (config.activeLow) req.config.flags |= GPIO_V2_LINE_FLAG_ACTIVE_LOW;
        if (config.bias == "pull-up") req.config.flags |= GPIO_V2_LINE_FLAG_BIAS_PULL_UP;
        if (config.bias == "pull-down") req.config.flags |= GPIO_V2_LINE_FLAG_BIAS_PULL_DOWN;
        // Debounce: 1 ms for the encoder contacts, 10 ms for the button.
        req.config.num_attrs = 2;
        req.config.attrs[0].mask = 0b011;
        req.config.attrs[0].attr.id = GPIO_V2_LINE_ATTR_ID_DEBOUNCE;
        req.config.attrs[0].attr.debounce_period_us = 1000;
        req.config.attrs[1].mask = 0b100;
        req.config.attrs[1].attr.id = GPIO_V2_LINE_ATTR_ID_DEBOUNCE;
        req.config.attrs[1].attr.debounce_period_us = 10000;

        int rc = ioctl(chipFd, GPIO_V2_GET_LINE_IOCTL, &req);
        int err = errno;
        close(chipFd);
        if (rc < 0)
            throw std::runtime_error("menu: cannot request the encoder lines on " + config.chip + ": " + std::strerror(err));
        fd_ = req.fd;
        fcntl(fd_, F_SETFL, fcntl(fd_, F_GETFL) | O_NONBLOCK);

        struct gpio_v2_line_values values;
        values.mask = 0b111;
        values.bits = 0;
        if (ioctl(fd_, GPIO_V2_LINE_GET_VALUES_IOCTL, &values) == 0)
        {
            a_ = values.bits & 1;
            b_ = values.bits & 2;
            down_ = values.bits & 4;
        }
        decoder_.update(a_, b_);
    }

    ~GpioEncoderInput() override { close(fd_); }

    MenuEvents poll() override
    {
        MenuEvents events;
        int presses = 0;
        struct gpio_v2_line_event batch[32];
        ssize_t n;
        while ((n = read(fd_, batch, sizeof(batch))) > 0)
        {
            for (size_t i = 0; i < n / sizeof(batch[0]); i++)
            {
                const bool active = batch[i].id == GPIO_V2_LINE_EVENT_RISING_EDGE;
                const int offset = static_cast<int>(batch[i].offset);
                if (offset == config_.lineButton)
                {
                    if (active && !down_) presses++;
                    down_ = active;
                    continue;
                }
                if (offset == config_.lineA) a_ = active;
                else b_ = active;
                events.steps += decoder_.update(a_, b_);
            }
        }
        if (config_.reverse) events.steps = -events.steps;
        press_.update(presses, down_, events);
        return events;
    }

private:
    MenuInputConfig config_;
    QuadratureDecoder decoder_;
    PressTracker press_;
    int fd_ = -1;
    bool a_ = false;
    bool b_ = false;
    bool down_ = false;
};

// ---- rackui --------------------------------------------------------------------

// Rack UI front panel module (https://github.com/Jankyneering/rack-ui): an I2C
// peripheral with a register pointer. Registers used here:
//   0x04-0x05  encoder count, 16 bit big-endian signed
//   0x06       push count, 8 bit
//   0x07       button state, 1 while pressed
// The counters are left free-running and differences are taken here, so the
// module's own LED animation that follows the encoder keeps working.
class RackUiInput : public IMenuInput
{
public:
    explicit RackUiInput(const MenuInputConfig& config) : config_(config), press_(config.longPressMs)
    {
        fd_ = open(config.i2cDevice.c_str(), O_RDWR | O_CLOEXEC);
        if (fd_ < 0)
            throw std::runtime_error("rackui: cannot open " + config.i2cDevice + ": " + std::strerror(errno));
        if (ioctl(fd_, I2C_SLAVE, config.i2cAddress) < 0)
        {
            const int err = errno;
            close(fd_);
            throw std::runtime_error("rackui: cannot select I2C address: " + std::string(std::strerror(err)));
        }
        uint8_t version[2];
        if (!readRegisters(0x00, version, 2) || !readState(count_, pushes_, down_))
        {
            close(fd_);
            throw std::runtime_error("rackui: no answer on " + config.i2cDevice + " (check wiring and address)");
        }
        FDVE_INFO("rackui: firmware %d.%d", version[0], version[1]);
    }

    ~RackUiInput() override { close(fd_); }

    MenuEvents poll() override
    {
        MenuEvents events;
        int16_t count;
        uint8_t pushes;
        bool down;
        if (!readState(count, pushes, down))
        {
            if (!failed_) FDVE_WARN("rackui: I2C read failed");
            failed_ = true;
            return events;
        }
        if (failed_)
        {
            // Back after a failure or a module reset: resynchronise, report nothing.
            failed_ = false;
            count_ = count;
            pushes_ = pushes;
            return events;
        }

        const int16_t turned = static_cast<int16_t>(count - count_); // wraps correctly
        const uint8_t pressed = static_cast<uint8_t>(pushes - pushes_);
        count_ = count;
        pushes_ = pushes;
        // A jump this large is a module reset, not a hand on the knob.
        if (turned > 200 || turned < -200) return events;

        events.steps = config_.reverse ? -turned : turned;
        press_.update(pressed, down, events);
        return events;
    }

private:
    bool readRegisters(uint8_t first, uint8_t* out, size_t count)
    {
        return write(fd_, &first, 1) == 1 && read(fd_, out, count) == static_cast<ssize_t>(count);
    }

    bool readState(int16_t& count, uint8_t& pushes, bool& down)
    {
        uint8_t raw[4];
        if (!readRegisters(0x04, raw, sizeof(raw))) return false;
        count = static_cast<int16_t>((raw[0] << 8) | raw[1]);
        pushes = raw[2];
        down = raw[3] != 0;
        return true;
    }

    MenuInputConfig config_;
    PressTracker press_;
    int fd_ = -1;
    int16_t count_ = 0;
    uint8_t pushes_ = 0;
    bool down_ = false;
    bool failed_ = false;
};

} // namespace

std::unique_ptr<IMenuInput> createMenuInput(const MenuInputConfig& config)
{
    if (config.type == "none") return std::make_unique<NoMenuInput>();
    if (config.type == "console") return std::make_unique<ConsoleMenuInput>();
    if (config.type == "gpio") return std::make_unique<GpioEncoderInput>(config);
    if (config.type == "rackui") return std::make_unique<RackUiInput>(config);
    throw std::runtime_error("unknown menu type '" + config.type + "'");
}

} // namespace fdve
