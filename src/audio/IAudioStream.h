#pragma once

namespace fdve {

// One direction of one sound device, 16 bit mono. Calls block until the
// device has consumed or produced the samples, which is what paces the pumps.
class IAudioStream
{
public:
    virtual ~IAudioStream() = default;
    virtual int sampleRate() const = 0;
    virtual void read(short* samples, int count) = 0;        // capture streams
    virtual void write(const short* samples, int count) = 0; // playback streams
};

} // namespace fdve
