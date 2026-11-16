#include "rig/HamlibRig.h"

#include <hamlib/rig.h>

#include <chrono>
#include <stdexcept>

#include "util/Log.h"

namespace fdve {

using Clock = std::chrono::steady_clock;
using std::chrono::milliseconds;

namespace {

RIG* handle(void* rig)
{
    return static_cast<RIG*>(rig);
}

// Short texts for Hamlib's error codes. Hamlib's own rigerror() appends its
// debug trace to the message, which does not belong in a log line.
std::string errorText(int code)
{
    switch (-code)
    {
        case RIG_EINVAL: return "invalid parameter";
        case RIG_ECONF: return "invalid configuration (port, speed)";
        case RIG_ENIMPL: return "not implemented for this radio";
        case RIG_ETIMEOUT: return "no answer (timeout)";
        case RIG_EIO: return "input/output error on the port";
        case RIG_EPROTO: return "protocol error";
        case RIG_ERJCTED: return "command rejected by the radio";
        case RIG_ENAVAIL: return "not available on this radio";
        case RIG_BUSERROR: return "bus error";
        case RIG_BUSBUSY: return "collision on the bus";
        default: return "Hamlib error " + std::to_string(code);
    }
}

// Settings go through Hamlib's named configuration parameters. Those names
// are stable across Hamlib versions; the struct fields behind them are not.
void configure(RIG* rig, const char* name, const std::string& value)
{
    const token_t token = rig_token_lookup(rig, name);
    if (token == RIG_CONF_END) throw std::runtime_error(std::string("rig: Hamlib has no setting '") + name + "'");
    const int rc = rig_set_conf(rig, token, value.c_str());
    if (rc != RIG_OK)
        throw std::runtime_error(std::string("rig: cannot set ") + name + " to '" + value + "': " + errorText(rc));
}

} // namespace

HamlibRig::HamlibRig(const RigConfig& config) : config_(config)
{
    // Hamlib prints its own diagnostics. Show them only when debugging.
    rig_set_debug(logLevel() == LogLevel::Debug ? RIG_DEBUG_WARN : RIG_DEBUG_NONE);

    // Check the settings now, so a wrong model number or value is reported at
    // start-up. Whether the radio answers is found out by the worker.
    RIG* rig = rig_init(config.model);
    if (rig == nullptr)
        throw std::runtime_error("rig: Hamlib does not know model " + std::to_string(config.model) +
                                 " (list the models with \"rigctl -l\")");
    try
    {
        if (!config.device.empty()) configure(rig, "rig_pathname", config.device);
        if (config.baud > 0) configure(rig, "serial_speed", std::to_string(config.baud));
        if (config.civAddress > 0) configure(rig, "civaddr", std::to_string(config.civAddress));
        if (config.ptt == "rts") configure(rig, "ptt_type", "RTS");
        else if (config.ptt == "dtr") configure(rig, "ptt_type", "DTR");
        else if (config.ptt != "cat") throw std::runtime_error("rig: ptt must be \"cat\", \"rts\" or \"dtr\"");
    }
    catch (...)
    {
        rig_cleanup(rig);
        throw;
    }
    FDVE_INFO("rig: %s %s (Hamlib model %d) on %s", rig->caps->mfg_name, rig->caps->model_name, config.model,
              config.device.empty() ? "its default port" : config.device.c_str());
    rig_ = rig;
    thread_ = std::thread(&HamlibRig::run, this);
}

HamlibRig::~HamlibRig()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        running_ = false;
        wantPtt_ = false;
    }
    wake_.notify_all();
    thread_.join();
    // The worker has stopped. Never leave the radio keyed.
    if (connected_.load())
    {
        rig_set_ptt(handle(rig_), RIG_VFO_CURR, RIG_PTT_OFF);
        rig_close(handle(rig_));
    }
    rig_cleanup(handle(rig_));
}

bool HamlibRig::setPtt(bool transmit, int timeoutMs)
{
    std::unique_lock<std::mutex> lock(mutex_);
    wantPtt_ = transmit;
    wake_.notify_all();
    return applied_.wait_for(lock, milliseconds(timeoutMs),
                             [&] { return connected_.load() && sentPtt_ == wantPtt_; });
}

bool HamlibRig::open()
{
    const int rc = rig_open(handle(rig_));
    if (rc != RIG_OK)
    {
        FDVE_DEBUG("rig: cannot open: %s", errorText(rc).c_str());
        return false;
    }
    // The state of the radio is unknown after (re)connecting, and it may
    // have been left keyed when the connection dropped. Start from "off".
    rig_set_ptt(handle(rig_), RIG_VFO_CURR, RIG_PTT_OFF);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        sentPtt_ = false;
    }
    connected_.store(true);
    FDVE_INFO("rig: connected");
    return true;
}

void HamlibRig::close()
{
    rig_close(handle(rig_));
    connected_.store(false);
    frequency_.store(0);
    FDVE_WARN("rig: connection lost, trying again every 3 seconds");
}

void HamlibRig::run()
{
    auto nextOpen = Clock::now();
    auto nextPoll = Clock::now();
    bool reportedDown = false;
    bool pttFailureReported = false;
    int failures = 0;

    for (;;)
    {
        bool want;
        {
            // Sleep until there is something to do: a PTT change, the next
            // frequency read, or the next attempt to connect.
            std::unique_lock<std::mutex> lock(mutex_);
            const auto deadline = connected_.load() ? nextPoll : nextOpen;
            wake_.wait_until(lock, deadline, [&] {
                return !running_ || (connected_.load() && wantPtt_ != sentPtt_);
            });
            if (!running_) return;
            want = wantPtt_;
        }

        if (!connected_.load())
        {
            if (Clock::now() < nextOpen) continue;
            if (!open())
            {
                if (!reportedDown) FDVE_WARN("rig: no answer from the radio, trying again every 3 seconds");
                reportedDown = true;
                nextOpen = Clock::now() + milliseconds(3000);
                continue;
            }
            reportedDown = false;
            failures = 0;
            nextPoll = Clock::now();
        }

        bool ok = true;
        bool sent;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            sent = sentPtt_;
        }
        if (want != sent)
        {
            const int rc = rig_set_ptt(handle(rig_), RIG_VFO_CURR, want ? RIG_PTT_ON : RIG_PTT_OFF);
            if (rc == RIG_OK)
            {
                std::lock_guard<std::mutex> lock(mutex_);
                sentPtt_ = want;
                applied_.notify_all();
                pttFailureReported = false;
            }
            else
            {
                // Say it once, not at every retry.
                if (!pttFailureReported)
                    FDVE_WARN("rig: PTT %s failed: %s", want ? "on" : "off", errorText(rc).c_str());
                pttFailureReported = true;
                ok = false;
            }
        }
        else if (config_.pollMs > 0 && Clock::now() >= nextPoll && !want)
        {
            // Not while transmitting: the PTT commands must not queue behind a poll.
            freq_t frequency = 0;
            const int rc = rig_get_freq(handle(rig_), RIG_VFO_CURR, &frequency);
            if (rc == RIG_OK) frequency_.store(static_cast<long long>(frequency + 0.5));
            else ok = false;
            nextPoll = Clock::now() + milliseconds(config_.pollMs);
        }
        else if (Clock::now() >= nextPoll)
        {
            nextPoll = Clock::now() + milliseconds(config_.pollMs > 0 ? config_.pollMs : 1000);
        }

        // One failed command can be a glitch. Several in a row mean the
        // radio is gone: close, and reconnect from scratch.
        failures = ok ? 0 : failures + 1;
        if (failures >= 3)
        {
            close();
            failures = 0;
            nextOpen = Clock::now() + milliseconds(3000);
            reportedDown = true;
        }
        else if (!ok)
        {
            std::this_thread::sleep_for(milliseconds(100)); // do not hammer a radio that just failed
        }
    }
}

} // namespace fdve
