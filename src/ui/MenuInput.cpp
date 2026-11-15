#include "ui/MenuInput.h"

#include <fcntl.h>
#include <linux/gpio.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <algorithm>
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

// ---- RackUiDecoder ----------------------------------------------------------

MenuEvents RackUiDecoder::update(const Reading& first, const std::function<bool(Reading&)>& reread,
                                 std::chrono::steady_clock::time_point now)
{
    MenuEvents events;
    Reading reading = first;
    int presses = 0;

    if (!reading.sameCounters(last_))
    {
        Reading second;
        if (!reread(second) || !second.sameCounters(reading))
        {
            // Not confirmed: a corrupted read, or the knob moved between the
            // two reads. Either way, decide on the next poll.
            rejected_++;
            return events;
        }
        const int turned = static_cast<int16_t>(reading.count - last_.count); // wraps correctly
        const int pressed = static_cast<uint8_t>(reading.pushes - last_.pushes);
        last_ = reading;
        // More than a hand can do between two polls: the module restarted.
        if (turned > 40 || turned < -40 || pressed > 3) return events;
        events.steps = turned;
        presses = pressed;
    }
    press_.update(presses, reading.down, events, now);
    return events;
}

std::array<uint8_t, 12> rackUiLevelFrame(float fraction, int brightness)
{
    constexpr int kArcStart = 8;
    constexpr int kArcLeds = 9;
    std::array<uint8_t, 12> frame{};
    const float lit = std::clamp(fraction, 0.0f, 1.0f) * kArcLeds;
    for (int i = 0; i < kArcLeds; i++)
    {
        const float share = std::clamp(lit - static_cast<float>(i), 0.0f, 1.0f);
        frame[(kArcStart + i) % 12] = static_cast<uint8_t>(share * static_cast<float>(brightness) + 0.5f);
    }
    return frame;
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
    explicit RackUiInput(const MenuInputConfig& config) : config_(config), decoder_(config.longPressMs)
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
        RackUiDecoder::Reading a, b;
        bool ok = readRegisters(0x00, version, 2);
        // Start from a reading that was seen twice.
        for (int attempt = 0; ok && attempt < 5; attempt++)
        {
            ok = readState(a) && readState(b);
            if (ok && a.sameCounters(b)) break;
        }
        if (!ok)
        {
            close(fd_);
            throw std::runtime_error("rackui: no answer on " + config.i2cDevice + " (check wiring and address)");
        }
        decoder_.reset(b);
        FDVE_INFO("rackui: firmware %d.%d", version[0], version[1]);
        if (config.leds == "off") setAnimation(kAnimationAllOff);
    }

    void showKnobPosition() override
    {
        if (config_.leds == "off") return;
        setAnimation(kAnimationFollowing);
    }

    void showLevel(float fraction) override
    {
        if (config_.leds != "level") return showKnobPosition();
        const auto frame = rackUiLevelFrame(fraction, config_.ledBrightness);
        const auto now = std::chrono::steady_clock::now();
        const bool entering = animation_ != kAnimationIdle;
        // At most 25 updates per second, and only when something changed.
        if (!entering && (frame == leds_ || now - lastLedWrite_ < std::chrono::milliseconds(40))) return;
        setAnimation(kAnimationIdle); // the LED registers are ours in this mode
        uint8_t message[13];
        message[0] = 0x10;
        std::copy(frame.begin(), frame.end(), message + 1);
        if (write(fd_, message, sizeof(message)) == static_cast<ssize_t>(sizeof(message))) leds_ = frame;
        lastLedWrite_ = now;
    }

    ~RackUiInput() override { close(fd_); }

    MenuEvents poll() override
    {
        RackUiDecoder::Reading reading;
        if (!readState(reading))
        {
            if (!failed_) FDVE_WARN("rackui: I2C read failed");
            failed_ = true;
            return {};
        }
        if (failed_)
        {
            // Back after a failure: resynchronise, report nothing.
            failed_ = false;
            decoder_.reset(reading);
            return {};
        }

        checkModuleState();

        const unsigned rejectedBefore = decoder_.rejected();
        MenuEvents events = decoder_.update(reading, [this](RackUiDecoder::Reading& again) { return readState(again); });
        if (decoder_.rejected() != rejectedBefore)
        {
            FDVE_DEBUG("rackui: unconfirmed reading discarded (count=%d pushes=%d)", reading.count, reading.pushes);
            // Say so at 1, 10, 100, ... so a noisy bus is visible without flooding the log.
            unsigned n = decoder_.rejected();
            while (n >= 10 && n % 10 == 0) n /= 10;
            if (n == 1) FDVE_WARN("rackui: %u unconfirmed reading(s) discarded so far", decoder_.rejected());
        }
        if (config_.reverse) events.steps = -events.steps;
        return events;
    }

private:
    static constexpr int kAnimationIdle = 0x00;      // LEDs set by the host
    static constexpr int kAnimationFollowing = 0x80; // LEDs follow the knob
    static constexpr int kAnimationAllOff = 0xFF;

    void setAnimation(int id)
    {
        if (animation_ == id) return;
        const uint8_t message[2] = {0x0E, static_cast<uint8_t>(id)};
        if (write(fd_, message, 2) == 2) animation_ = id;
        leds_.fill(0xFF); // unknown again: resend the bar when it is next shown
    }

    // Once a second, make sure the module is still set up the way this driver
    // left it. A write can be corrupted on the bus like a read can, and the
    // module can restart (its watchdog, a power dip). Two things matter:
    //   - the config register must be 0: its "clear on read" bits would make
    //     the counters jump and look like knob movement
    //   - the LED animation must be the one selected here
    void checkModuleState()
    {
        const auto now = std::chrono::steady_clock::now();
        if (now - lastCheck_ < std::chrono::seconds(1)) return;
        lastCheck_ = now;

        uint8_t configRegister = 0;
        uint8_t animation = 0;
        if (!readRegisters(0x03, &configRegister, 1) || !readRegisters(0x0E, &animation, 1)) return;

        if (configRegister != 0)
        {
            FDVE_WARN("rackui: config register was 0x%02x, setting it back to 0", configRegister);
            const uint8_t message[2] = {0x03, 0x00};
            if (write(fd_, message, 2) != 2) return;
            RackUiDecoder::Reading a, b;
            if (readState(a) && readState(b) && a.sameCounters(b)) decoder_.reset(b);
        }
        if (animation_ >= 0 && animation != animation_)
        {
            FDVE_DEBUG("rackui: LED animation was 0x%02x, expected 0x%02x", animation, animation_);
            const int wanted = animation_;
            animation_ = -1;
            setAnimation(wanted);
        }
    }

    // One transaction with a repeated start: register pointer, then the data.
    bool readRegisters(uint8_t first, uint8_t* out, size_t count)
    {
        struct i2c_msg messages[2];
        messages[0].addr = static_cast<__u16>(config_.i2cAddress);
        messages[0].flags = 0;
        messages[0].len = 1;
        messages[0].buf = &first;
        messages[1].addr = static_cast<__u16>(config_.i2cAddress);
        messages[1].flags = I2C_M_RD;
        messages[1].len = static_cast<__u16>(count);
        messages[1].buf = out;
        struct i2c_rdwr_ioctl_data transfer;
        transfer.msgs = messages;
        transfer.nmsgs = 2;
        return ioctl(fd_, I2C_RDWR, &transfer) == 2;
    }

    bool readState(RackUiDecoder::Reading& reading)
    {
        uint8_t raw[4];
        if (!readRegisters(0x04, raw, sizeof(raw))) return false;
        reading.count = static_cast<int16_t>((raw[0] << 8) | raw[1]);
        reading.pushes = raw[2];
        reading.down = raw[3] == 0x01;
        return true;
    }

    MenuInputConfig config_;
    RackUiDecoder decoder_;
    int fd_ = -1;
    bool failed_ = false;
    int animation_ = -1; // what the module was last told; -1 = not told yet
    std::array<uint8_t, 12> leds_{};
    std::chrono::steady_clock::time_point lastLedWrite_{};
    std::chrono::steady_clock::time_point lastCheck_{};
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
