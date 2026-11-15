#include "audio/AudioFactory.h"

#include <stdexcept>

#include "audio/NullStream.h"
#ifdef FDVE_WITH_ALSA
#include "audio/AlsaStream.h"
#endif

namespace fdve {

std::unique_ptr<IAudioStream> createAudioStream(const AudioStreamConfig& config, StreamDirection direction)
{
    if (config.type == "null") return std::make_unique<NullStream>(config.sampleRate);
#ifdef FDVE_WITH_ALSA
    if (config.type == "alsa")
        return std::make_unique<AlsaStream>(config.device, direction, config.sampleRate, config.periodMs);
#endif
    throw std::runtime_error("unknown audio type '" + config.type + "'");
}

} // namespace fdve
