#include "audio/AlsaStream.h"

#include <alsa/asoundlib.h>

#include <cstring>
#include <stdexcept>

namespace fdve {

AlsaStream::AlsaStream(const std::string& device, StreamDirection direction, int sampleRate, int periodMs)
    : device_(device), sampleRate_(sampleRate), capture_(direction == StreamDirection::Capture)
{
    const auto stream = direction == StreamDirection::Capture ? SND_PCM_STREAM_CAPTURE : SND_PCM_STREAM_PLAYBACK;
    int rc = snd_pcm_open(&pcm_, device.c_str(), stream, 0);
    if (rc < 0)
        throw std::runtime_error("cannot open ALSA device '" + device + "': " + snd_strerror(rc));

    // Mono S16. Use a "plughw:" device name so ALSA converts channel count and
    // rate when the card does not support them natively (most USB cards are
    // stereo-only on playback).
    const unsigned int latencyUs = 4u * periodMs * 1000u;
    rc = snd_pcm_set_params(pcm_, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED,
                            1, sampleRate, 1, latencyUs);
    if (rc < 0)
    {
        snd_pcm_close(pcm_);
        throw std::runtime_error("cannot configure ALSA device '" + device + "': " + snd_strerror(rc));
    }
    if (capture_) startCapture();
}

void AlsaStream::startCapture()
{
    // snd_pcm_set_params() sets the start threshold to a full buffer. A
    // capture stream then only starts by itself on a read of at least that
    // size. Our reads are one period, so start it explicitly; otherwise the
    // first read waits, times out and fails with EIO.
    int rc = snd_pcm_start(pcm_);
    if (rc < 0)
        throw std::runtime_error("cannot start ALSA capture on '" + device_ + "': " + snd_strerror(rc));
}

AlsaStream::~AlsaStream()
{
    if (pcm_ != nullptr) snd_pcm_close(pcm_);
}

void AlsaStream::read(short* samples, int count)
{
    int done = 0;
    while (done < count)
    {
        snd_pcm_sframes_t n = snd_pcm_readi(pcm_, samples + done, count - done);
        if (n < 0)
        {
            // Overrun or suspend: recover, restart and hand back silence for the gap.
            int rc = snd_pcm_recover(pcm_, n, 1);
            if (rc < 0)
                throw std::runtime_error("ALSA capture failed on '" + device_ + "': " + snd_strerror(rc));
            startCapture();
            std::memset(samples + done, 0, sizeof(short) * (count - done));
            return;
        }
        done += n;
    }
}

void AlsaStream::write(const short* samples, int count)
{
    int done = 0;
    while (done < count)
    {
        snd_pcm_sframes_t n = snd_pcm_writei(pcm_, samples + done, count - done);
        if (n < 0)
        {
            int rc = snd_pcm_recover(pcm_, n, 1);
            if (rc < 0)
                throw std::runtime_error("ALSA playback failed on '" + device_ + "': " + snd_strerror(rc));
            continue;
        }
        done += n;
    }
}

} // namespace fdve
