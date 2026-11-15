#pragma once

#include <string>

#include "audio/AudioFactory.h"
#include "audio/IAudioStream.h"

typedef struct _snd_pcm snd_pcm_t;

namespace fdve {

class AlsaStream : public IAudioStream
{
public:
    AlsaStream(const std::string& device, StreamDirection direction, int sampleRate, int periodMs);
    ~AlsaStream() override;
    int sampleRate() const override { return sampleRate_; }
    void read(short* samples, int count) override;
    void write(const short* samples, int count) override;

private:
    std::string device_;
    int sampleRate_;
    snd_pcm_t* pcm_ = nullptr;
};

} // namespace fdve
