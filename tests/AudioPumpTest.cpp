// Runs the pumps on null devices: capture delivers paced blocks, playback
// waits for the prebuffer level and then drains the source completely.

#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <thread>

#include "audio/AudioFactory.h"
#include "audio/AudioPump.h"

using namespace fdve;
using std::chrono::milliseconds;

int main()
{
    AudioStreamConfig config;
    config.type = "null";
    config.sampleRate = 8000;
    const int block = 160; // 20 ms

    std::atomic<int> captured{0};
    std::atomic<int> queued{0};
    std::atomic<int> played{0};
    bool ok = true;

    {
        CapturePump capture("test", createAudioStream(config, StreamDirection::Capture), 20,
            [&](const short*, int n) { captured += n; });
        PlaybackPump playback("test", createAudioStream(config, StreamDirection::Playback), 20, 100,
            {[&] { return queued.load(); },
             [&](short*, int n) { if (queued < n) return false; queued -= n; played += n; return true; }});

        // Below the 100 ms prebuffer (800 samples): nothing may be played.
        queued = 3 * block;
        std::this_thread::sleep_for(milliseconds(150));
        if (played != 0) { std::printf("FAIL: played %d before prebuffer was reached\n", played.load()); ok = false; }

        // Above it: everything is played, including a partial last block.
        queued += 4 * block + 50;
        std::this_thread::sleep_for(milliseconds(400));
        if (queued != 0 || played != 7 * block + 50)
        {
            std::printf("FAIL: queued=%d played=%d\n", queued.load(), played.load());
            ok = false;
        }
        if (capture.failed() || playback.failed()) ok = false;
    }

    // About 550 ms at 8 kHz is 4400 samples; allow scheduling slack.
    if (captured < 3200 || captured > 5600) { std::printf("FAIL: captured %d\n", captured.load()); ok = false; }

    std::printf(ok ? "PASS\n" : "FAILED\n");
    return ok ? 0 : 1;
}
