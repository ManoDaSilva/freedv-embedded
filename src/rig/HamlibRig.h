#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "rig/IRig.h"

namespace fdve {

// A radio controlled through Hamlib. Everything that talks to the radio
// happens on one worker thread:
//   - opening the connection, and trying again every few seconds while it is down
//   - applying the wanted PTT state whenever it differs from what was last sent
//   - reading the frequency at an interval
// If the connection drops while transmitting, the worker unkeys the radio as
// soon as it is back, unless transmit is still wanted.
class HamlibRig : public IRig
{
public:
    explicit HamlibRig(const RigConfig& config);
    ~HamlibRig() override;

    bool setPtt(bool transmit, int timeoutMs) override;
    bool connected() const override { return connected_.load(); }
    long long frequencyHz() const override { return frequency_.load(); }

private:
    void run();
    bool open();
    void close();

    RigConfig config_;
    void* rig_ = nullptr; // RIG*, used by the worker thread only

    mutable std::mutex mutex_;
    std::condition_variable wake_;    // for the worker
    std::condition_variable applied_; // for setPtt() callers
    bool wantPtt_ = false;
    bool sentPtt_ = false;            // what the radio was last told, valid while connected
    bool running_ = true;

    std::atomic<bool> connected_{false};
    std::atomic<long long> frequency_{0};
    std::thread thread_;
};

} // namespace fdve
