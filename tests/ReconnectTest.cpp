// A sound device that disappears and comes back: the stream must carry on
// with paced silence, try to reopen, and report each change once.

#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <vector>

#include "audio/ReconnectingStream.h"

using namespace fdve;
using Clock = std::chrono::steady_clock;

static int failures = 0;
#define CHECK(cond) \
    do { if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); failures++; } } while (0)

// A device that works for a number of blocks, then fails like an unplugged card.
class FakeDevice : public IAudioStream
{
public:
    explicit FakeDevice(int blocksUntilFailure) : left_(blocksUntilFailure) {}
    int sampleRate() const override { return 8000; }
    void read(short* samples, int count) override
    {
        if (left_-- <= 0) throw std::runtime_error("Input/output error");
        for (int i = 0; i < count; i++) samples[i] = 1234;
    }
    void write(const short*, int) override
    {
        if (left_-- <= 0) throw std::runtime_error("Input/output error");
    }

private:
    int left_;
};

int main()
{
    int opens = 0;
    int refusals = 2; // the device stays away for two attempts
    int lost = 0;
    int back = 0;
    std::string lastReason;

    ReconnectingStream stream(
        "Radio in", 8000,
        [&]() -> std::unique_ptr<IAudioStream> {
            opens++;
            if (refusals-- > 0) throw std::runtime_error("No such device");
            return std::make_unique<FakeDevice>(1000);
        },
        std::make_unique<FakeDevice>(3),
        [&](bool isLost, const std::string& reason) { isLost ? lost++ : back++; if (isLost) lastReason = reason; },
        50);

    std::vector<short> block(160); // 20 ms
    // Three good blocks.
    for (int i = 0; i < 3; i++)
    {
        stream.read(block.data(), 160);
        CHECK(block[0] == 1234 && stream.present());
    }

    // The fourth read fails inside the device. The caller gets silence, no exception.
    stream.read(block.data(), 160);
    CHECK(block[0] == 0 && block[159] == 0 && !stream.present());
    CHECK(lost == 1 && back == 0 && lastReason == "Input/output error");

    // While it is away: silence, at the pace of a real device (20 ms per block).
    const auto start = Clock::now();
    int silentBlocks = 0;
    while (!stream.present() && silentBlocks < 200)
    {
        stream.read(block.data(), 160);
        if (!stream.present() || block[0] == 0) silentBlocks++;
    }
    const auto away = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
    std::printf("device away for %lld ms, %d silent blocks, %d open attempts\n", static_cast<long long>(away),
                silentBlocks, opens);
    CHECK(stream.present());
    CHECK(opens == 3);                         // two refusals, then success
    CHECK(away >= 120 && away < 400);          // three retry intervals of 50 ms, not a busy loop
    CHECK(silentBlocks >= 5 && silentBlocks <= 20);
    CHECK(lost == 1 && back == 1);             // each change reported once

    // And it delivers audio again.
    stream.read(block.data(), 160);
    CHECK(block[0] == 1234);

    // Playback behaves the same, and a stream can start without its device.
    int playbackOpens = 0;
    ReconnectingStream playback(
        "Radio out", 8000,
        [&]() -> std::unique_ptr<IAudioStream> { playbackOpens++; return std::make_unique<FakeDevice>(2); },
        nullptr, {}, 30);
    CHECK(!playback.present());
    for (int i = 0; i < 20; i++) playback.write(block.data(), 160); // never throws
    CHECK(playbackOpens >= 2); // opened, failed after two blocks, opened again

    std::printf(failures == 0 ? "PASS\n" : "FAILED\n");
    return failures == 0 ? 0 : 1;
}
