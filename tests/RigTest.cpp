// The rig layer against one of Hamlib's built-in dummy radios (model 6),
// which behaves like a radio without needing one. Checks keying, frequency
// read-out, and that an unreachable radio is survived.

#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <thread>

#include "ptt/PttFactory.h"
#include "rig/IRig.h"

using namespace fdve;
using Clock = std::chrono::steady_clock;

static int failures = 0;
#define CHECK(cond) \
    do { if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); failures++; } } while (0)

static bool throws(const RigConfig& config, const char* expected)
{
    try
    {
        createRig(config);
    }
    catch (const std::runtime_error& e)
    {
        return std::string(e.what()).find(expected) != std::string::npos;
    }
    return false;
}

int main()
{
    // No rig configured: a harmless stand-in.
    auto none = createRig(RigConfig{});
    CHECK(!none->connected() && !none->setPtt(true, 10) && none->frequencyHz() == 0);

    RigConfig dummy;
    dummy.type = "hamlib";
    dummy.model = 6; // "Dummy No VFO": accepts PTT over CAT, unlike model 1
    dummy.pollMs = 50;
    {
        auto rig = createRig(dummy);

        // Keying is confirmed well inside the time allowed.
        const auto start = Clock::now();
        CHECK(rig->setPtt(true, 2000));
        const auto took = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
        std::printf("dummy rig: connected and keyed in %lld ms\n", static_cast<long long>(took));
        CHECK(rig->connected());
        CHECK(rig->setPtt(true, 100)); // already on: confirmed at once
        CHECK(rig->setPtt(false, 2000));

        // The frequency is polled while receiving. The dummy radio reports 145 MHz.
        for (int i = 0; i < 40 && rig->frequencyHz() == 0; i++) std::this_thread::sleep_for(std::chrono::milliseconds(50));
        std::printf("dummy rig: frequency %lld Hz\n", rig->frequencyHz());
        CHECK(rig->frequencyHz() == 145000000);

        // The PTT output of type "rig" keys through it.
        PttOutputConfig out;
        out.type = "rig";
        auto ptt = createPttOutput(out, rig);
        ptt->set(true);
        ptt->set(false);
        CHECK(rig->connected());
    } // destroying it must return promptly, with the radio unkeyed

    // A radio whose model has no PTT over CAT (Hamlib's model 1): keying is
    // refused by Hamlib, which is reported as "not confirmed", without blocking.
    RigConfig noPtt = dummy;
    noPtt.model = 1;
    {
        auto rig = createRig(noPtt);
        CHECK(!rig->setPtt(true, 500));
        rig->setPtt(false, 100);
    }

    // A wrong model number is reported at once.
    RigConfig unknown = dummy;
    unknown.model = 99999;
    CHECK(throws(unknown, "does not know model 99999"));
    RigConfig badPtt = dummy;
    badPtt.ptt = "vox";
    CHECK(throws(badPtt, "ptt must be"));
    RigConfig badType;
    badType.type = "flrig";
    CHECK(throws(badType, "unknown rig type"));

    // The check used before saving settings: no connection is made.
    CHECK(checkRigConfig(RigConfig{}).empty());
    CHECK(checkRigConfig(dummy).empty());
    CHECK(checkRigConfig(unknown).find("does not know model 99999") != std::string::npos);
    CHECK(checkRigConfig(badPtt).find("ptt must be") != std::string::npos);
    CHECK(checkRigConfig(badType).find("unknown rig type") != std::string::npos);
    RigConfig notPluggedIn = dummy;
    notPluggedIn.model = 3085; // IC-705
    notPluggedIn.device = "/dev/ttyDoesNotExist";
    CHECK(checkRigConfig(notPluggedIn).empty()); // a radio that is off is not a config error

    // A radio that is not there (IC-7300 on a serial port that does not
    // exist): creating it works, keying times out instead of blocking, and
    // shutting down is quick.
    RigConfig absent;
    absent.type = "hamlib";
    absent.model = 3073;
    absent.device = "/dev/ttyDoesNotExist";
    {
        const auto start = Clock::now();
        auto rig = createRig(absent);
        CHECK(!rig->setPtt(true, 300));
        CHECK(!rig->connected() && rig->frequencyHz() == 0);
        rig.reset();
        const auto took = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
        std::printf("absent rig: handled in %lld ms\n", static_cast<long long>(took));
        CHECK(took < 5000);
    }

    std::printf(failures == 0 ? "PASS\n" : "FAILED\n");
    return failures == 0 ? 0 : 1;
}
