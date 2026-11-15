#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>

#include "audio/IAudioStream.h"

namespace fdve {

// A pump owns one stream and one thread and moves fixed-size blocks between
// the device and a callback. It also measures the peak level of what passes.

class CapturePump
{
public:
    using Sink = std::function<void(const short* samples, int count)>;

    CapturePump(std::string name, std::unique_ptr<IAudioStream> stream, int blockMs, Sink sink);
    ~CapturePump();
    float peakDbfs() const { return peakDbfs_.load(); }
    bool failed() const { return failed_.load(); }

private:
    void run(std::string name, int blockMs, Sink sink);
    std::unique_ptr<IAudioStream> stream_;
    std::atomic<bool> running_{true};
    std::atomic<bool> failed_{false};
    std::atomic<float> peakDbfs_{-99.0f};
    std::thread thread_;
};

class PlaybackPump
{
public:
    struct Source
    {
        std::function<int()> available;                     // samples ready to read
        std::function<bool(short* samples, int count)> read; // false: nothing read
    };

    // prebufferMs: after the source runs dry, play silence until this much
    // audio is queued again. The modem produces audio in 120 ms bursts, so
    // without this every burst boundary would be a gap.
    PlaybackPump(std::string name, std::unique_ptr<IAudioStream> stream, int blockMs, int prebufferMs, Source source);
    ~PlaybackPump();
    bool failed() const { return failed_.load(); }

private:
    void run(std::string name, int blockMs, int prebufferMs, Source source);
    std::unique_ptr<IAudioStream> stream_;
    std::atomic<bool> running_{true};
    std::atomic<bool> failed_{false};
    std::thread thread_;
};

} // namespace fdve
