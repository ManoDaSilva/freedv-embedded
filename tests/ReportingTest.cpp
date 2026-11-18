// What is reported to FreeDV Reporter and PSK Reporter, and when. Uses a
// recording sink instead of the network.

#include <chrono>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "reporting/Reporter.h"

using namespace fdve;
using Clock = std::chrono::steady_clock;
using std::chrono::milliseconds;

static int failures = 0;
#define CHECK(cond) \
    do { if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); failures++; } } while (0)

class RecordingSink : public IReportSink
{
public:
    std::vector<std::string> log;
    mutable std::mutex mutex;

    void frequency(uint64_t hz) override { add("freq " + std::to_string(hz)); }
    void transmit(const std::string& mode, bool tx) override { add("tx " + mode + (tx ? " on" : " off")); }
    void analog(bool inAnalog) override { add(inAnalog ? "analog on" : "analog off"); }
    void received(const std::string& callsign, const std::string& mode, uint64_t hz, int snr, bool pskToo) override
    {
        add("rx '" + callsign + "' " + mode + " " + std::to_string(hz) + " " + std::to_string(snr) +
            (pskToo ? " psk" : ""));
    }
    std::string status() const override { return "connected"; }

    std::vector<std::string> take()
    {
        std::lock_guard<std::mutex> lock(mutex);
        auto out = log;
        log.clear();
        return out;
    }

private:
    void add(const std::string& line)
    {
        std::lock_guard<std::mutex> lock(mutex);
        log.push_back(line);
    }
};

static bool same(const std::vector<std::string>& got, const std::vector<std::string>& want)
{
    if (got == want) return true;
    std::printf("  got:");
    for (const auto& line : got) std::printf(" [%s]", line.c_str());
    std::printf("\n  want:");
    for (const auto& line : want) std::printf(" [%s]", line.c_str());
    std::printf("\n");
    return false;
}

static void testPolicy()
{
    RecordingSink sink;
    ReportPolicy policy(sink, "RADEV1");
    const auto t0 = Clock::now();
    auto at = [&](int ms) { return t0 + milliseconds(ms); };

    ReportInput in;
    in.frequencyHz = 14236000;
    in.rxCallsign = "OLD1";
    in.rxCallsignCount = 4; // heard before reporting started: not reported

    // First step: the current state, once.
    policy.step(in, at(0));
    CHECK(same(sink.take(), {"analog off", "tx RADEV1 off", "freq 14236000"}));
    policy.step(in, at(100));
    CHECK(sink.take().empty());

    // Receiving with sync, no callsign yet: an anonymous report once a second.
    in.sync = true;
    in.snrDb = 7;
    for (int ms = 200; ms <= 2100; ms += 100) policy.step(in, at(ms));
    CHECK(same(sink.take(), {"rx '' RADEV1 14236000 7", "rx '' RADEV1 14236000 7"}));

    // A decoded callsign: reported once, to PSK Reporter too.
    in.rxCallsign = "VK5DGR";
    in.rxCallsignCount = 5;
    in.snrDb = 9;
    policy.step(in, at(2200));
    policy.step(in, at(2300));
    CHECK(same(sink.take(), {"rx 'VK5DGR' RADEV1 14236000 9 psk"}));
    // The same callsign again in a new over counts again.
    in.rxCallsignCount = 6;
    policy.step(in, at(2400));
    CHECK(same(sink.take(), {"rx 'VK5DGR' RADEV1 14236000 9 psk"}));

    // Transmitting: reported on change; no receive reports meanwhile.
    in.sync = false;
    in.transmitting = true;
    for (int ms = 2500; ms <= 4500; ms += 100) policy.step(in, at(ms));
    CHECK(same(sink.take(), {"tx RADEV1 on"}));
    in.transmitting = false;
    policy.step(in, at(4600));
    CHECK(same(sink.take(), {"tx RADEV1 off"}));

    // Frequency: on change, at most once a second, so turning the dial does
    // not flood the server. The last value always gets through.
    in.frequencyHz = 14237000;
    policy.step(in, at(4700));
    in.frequencyHz = 14238000;
    policy.step(in, at(4800));
    in.frequencyHz = 14239000;
    policy.step(in, at(5200));
    CHECK(same(sink.take(), {"freq 14237000"}));
    policy.step(in, at(5800));
    CHECK(same(sink.take(), {"freq 14239000"}));

    // Radio offline: frequency unknown, reported as 0 (nothing). A callsign
    // heard meanwhile goes to FreeDV Reporter, but not to PSK Reporter,
    // which needs a frequency.
    in.frequencyHz = 0;
    in.rxCallsign = "ON4AA";
    in.rxCallsignCount = 7;
    policy.step(in, at(7000));
    CHECK(same(sink.take(), {"freq 0", "rx 'ON4AA' RADEV1 0 9"}));

    // Analog mode: reported on change, and no anonymous receive reports.
    in.analog = true;
    in.sync = true;
    for (int ms = 7100; ms <= 9000; ms += 100) policy.step(in, at(ms));
    CHECK(same(sink.take(), {"analog on"}));

    // SNR is limited to what the protocol carries.
    in.analog = false;
    in.rxCallsign = "ZZ0ZZZ";
    in.rxCallsignCount = 8;
    in.snrDb = 500;
    policy.step(in, at(9100));
    CHECK(same(sink.take(), {"analog off", "rx 'ZZ0ZZZ' RADEV1 0 127"}));
}

static void testReporterThread()
{
    auto owned = std::make_unique<RecordingSink>();
    RecordingSink* sink = owned.get();
    ReporterSettings settings;
    settings.callsign = "ON6RF";
    settings.gridSquare = "JO20";
    settings.software = "freedv-embedded 0.1.0";
    settings.mode = "RADEV1";
    {
        Reporter reporter(settings, std::move(owned));
        ReportInput in;
        in.frequencyHz = 7177000;
        reporter.update(in);
        std::this_thread::sleep_for(milliseconds(350));
        in.transmitting = true;
        reporter.update(in);
        std::this_thread::sleep_for(milliseconds(350));
        CHECK(reporter.status() == "connected");
        const auto got = sink->take();
        CHECK(same(got, {"analog off", "tx RADEV1 off", "freq 7177000", "tx RADEV1 on"}));
    } // stops promptly
}

int main()
{
    testPolicy();
    testReporterThread();
    std::printf(failures == 0 ? "PASS\n" : "FAILED\n");
    return failures == 0 ? 0 : 1;
}
