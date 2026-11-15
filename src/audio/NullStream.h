#pragma once

#include <chrono>

#include "audio/IAudioStream.h"

namespace fdve {

// Silence in, nothing out, paced like a real device. For running without hardware.
class NullStream : public IAudioStream
{
public:
    explicit NullStream(int sampleRate);
    int sampleRate() const override { return sampleRate_; }
    void read(short* samples, int count) override;
    void write(const short* samples, int count) override;

private:
    void pace(int count);
    int sampleRate_;
    std::chrono::steady_clock::time_point next_;
};

} // namespace fdve
