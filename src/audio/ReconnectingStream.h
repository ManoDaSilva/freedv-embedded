#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <string>

#include "audio/IAudioStream.h"

namespace fdve {

// A sound device that may go away and come back, for example a radio whose
// USB cable is unplugged. While the device is missing, this stream behaves
// like silence at the right pace and tries to open the device again at an
// interval. The rest of the program never sees the failure.
class ReconnectingStream : public IAudioStream
{
public:
    using Opener = std::function<std::unique_ptr<IAudioStream>()>; // throws when the device cannot be opened
    using Notice = std::function<void(bool lost, const std::string& reason)>;

    // stream: the device if it is already open, or null to start without it.
    // notice: called once when the device is lost and once when it is back.
    ReconnectingStream(std::string name, int sampleRate, Opener opener, std::unique_ptr<IAudioStream> stream,
                       Notice notice = {}, int retryMs = 2000);

    int sampleRate() const override { return sampleRate_; }
    void read(short* samples, int count) override;
    void write(const short* samples, int count) override;

    bool present() const { return stream_ != nullptr; }

private:
    void lose(const std::string& reason);
    void retry();
    void pace(int count);

    std::string name_;
    int sampleRate_;
    Opener opener_;
    std::unique_ptr<IAudioStream> stream_;
    Notice notice_;
    std::chrono::milliseconds retryInterval_;
    std::chrono::steady_clock::time_point nextRetry_;
    std::chrono::steady_clock::time_point nextBlock_;
};

} // namespace fdve
