#pragma once

#include <memory>
#include <string>

#include "audio/IAudioStream.h"

namespace fdve {

enum class StreamDirection { Capture, Playback };

struct AudioStreamConfig
{
    std::string type = "alsa"; // "alsa" or "null"
    std::string device;        // ALSA PCM name, for example "plughw:CARD=Device,DEV=0"
    int sampleRate = 48000;
    int periodMs = 20;
};

// Add new audio backends (I2S codec with its own driver, network audio, ...) here.
std::unique_ptr<IAudioStream> createAudioStream(const AudioStreamConfig& config, StreamDirection direction);

} // namespace fdve
