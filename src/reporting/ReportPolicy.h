#pragma once

#include <chrono>
#include <cstdint>
#include <string>

namespace fdve {

// Where reports go. The real one talks to FreeDV Reporter and PSK Reporter
// through the FreeDV backend; tests use a recording one.
class IReportSink
{
public:
    virtual ~IReportSink() = default;
    virtual void frequency(uint64_t hz) = 0;                       // 0: unknown
    virtual void transmit(const std::string& mode, bool tx) = 0;
    virtual void analog(bool inAnalog) = 0;
    // An empty callsign means "receiving a signal, callsign not decoded yet".
    // pskToo: also send it to PSK Reporter.
    virtual void received(const std::string& callsign, const std::string& mode, uint64_t hz, int snr,
                          bool pskToo) = 0;
    virtual void tick(std::chrono::steady_clock::time_point) {} // periodic work, such as PSK Reporter uploads
    virtual std::string status() const = 0;                      // "connecting", "connected", ...
};

// What the station is doing right now, as seen by the reporter.
struct ReportInput
{
    bool transmitting = false;
    bool analog = false;
    bool sync = false;
    int snrDb = 0;
    std::string rxCallsign;          // last callsign decoded
    unsigned rxCallsignCount = 0;    // goes up by one with every decode, also of the same callsign
    uint64_t frequencyHz = 0;        // 0: unknown (radio offline)
};

// Decides what to report, and when. No threads, no network: given the
// station's state at each step, it calls the sink.
//
//   - frequency: when it changes, at most once a second
//   - transmit state: when it changes
//   - analog mode: when it changes
//   - a decoded callsign: once per decode, to FreeDV Reporter, and to PSK
//     Reporter when the frequency is known
//   - receiving with sync: an anonymous receive report once a second, so the
//     station shows as hearing a signal before a callsign is decoded
class ReportPolicy
{
public:
    ReportPolicy(IReportSink& sink, std::string mode) : sink_(sink), mode_(std::move(mode)) {}

    void step(const ReportInput& in, std::chrono::steady_clock::time_point now);

private:
    IReportSink& sink_;
    std::string mode_;
    bool first_ = true;
    bool transmitting_ = false;
    bool analog_ = false;
    uint64_t sentFrequency_ = 0;
    std::chrono::steady_clock::time_point lastFrequency_{};
    std::chrono::steady_clock::time_point lastSync_{};
    unsigned callsignCount_ = 0;
};

} // namespace fdve
