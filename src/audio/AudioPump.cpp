#include "audio/AudioPump.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <vector>

#include "util/Log.h"

namespace fdve {

namespace {

float peakToDbfs(const short* samples, int count)
{
    int peak = 0;
    for (int i = 0; i < count; i++) peak = std::max(peak, std::abs(static_cast<int>(samples[i])));
    if (peak == 0) return -99.0f;
    return 20.0f * std::log10(peak / 32768.0f);
}

} // namespace

CapturePump::CapturePump(std::string name, std::unique_ptr<IAudioStream> stream, int blockMs, Sink sink)
    : stream_(std::move(stream))
{
    thread_ = std::thread(&CapturePump::run, this, std::move(name), blockMs, std::move(sink));
}

CapturePump::~CapturePump()
{
    running_.store(false);
    if (thread_.joinable()) thread_.join();
}

void CapturePump::run(std::string name, int blockMs, Sink sink)
{
    const int block = stream_->sampleRate() * blockMs / 1000;
    std::vector<short> buffer(block);
    float held = -99.0f;
    try
    {
        while (running_.load())
        {
            stream_->read(buffer.data(), block);
            // Peak meter with a slow decay, so a display sampling a few times
            // per second still sees short peaks.
            held = std::max(peakToDbfs(buffer.data(), block), held - 0.5f);
            peakDbfs_.store(held);
            sink(buffer.data(), block);
        }
    }
    catch (const std::exception& e)
    {
        FDVE_ERROR("audio capture '%s' stopped: %s", name.c_str(), e.what());
        failed_.store(true);
    }
}

PlaybackPump::PlaybackPump(std::string name, std::unique_ptr<IAudioStream> stream, int blockMs, int prebufferMs, Source source)
    : stream_(std::move(stream))
{
    thread_ = std::thread(&PlaybackPump::run, this, std::move(name), blockMs, prebufferMs, std::move(source));
}

PlaybackPump::~PlaybackPump()
{
    running_.store(false);
    if (thread_.joinable()) thread_.join();
}

void PlaybackPump::run(std::string name, int blockMs, int prebufferMs, Source source)
{
    const int block = stream_->sampleRate() * blockMs / 1000;
    const int prebuffer = stream_->sampleRate() * prebufferMs / 1000;
    std::vector<short> buffer(block);
    bool primed = false;
    try
    {
        while (running_.load())
        {
            if (!primed && source.available() >= std::max(prebuffer, block)) primed = true;
            if (!primed || !source.read(buffer.data(), block))
            {
                // Play out a partial last block (the tail of a transmission)
                // instead of dropping it.
                int rest = primed ? std::min(source.available(), block) : 0;
                if (rest > 0 && !source.read(buffer.data(), rest)) rest = 0;
                std::memset(buffer.data() + rest, 0, sizeof(short) * (block - rest));
                primed = false;
            }
            stream_->write(buffer.data(), block);
        }
    }
    catch (const std::exception& e)
    {
        FDVE_ERROR("audio playback '%s' stopped: %s", name.c_str(), e.what());
        failed_.store(true);
    }
}

} // namespace fdve
