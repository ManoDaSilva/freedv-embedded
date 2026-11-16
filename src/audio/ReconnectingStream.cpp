#include "audio/ReconnectingStream.h"

#include <cstring>
#include <exception>
#include <thread>

#include "util/Log.h"

namespace fdve {

using Clock = std::chrono::steady_clock;

ReconnectingStream::ReconnectingStream(std::string name, int sampleRate, Opener opener,
                                       std::unique_ptr<IAudioStream> stream, Notice notice, int retryMs)
    : name_(std::move(name)), sampleRate_(sampleRate), opener_(std::move(opener)), stream_(std::move(stream)),
      notice_(std::move(notice)), retryInterval_(retryMs), nextRetry_(Clock::now() + retryInterval_),
      nextBlock_(Clock::now())
{
}

void ReconnectingStream::lose(const std::string& reason)
{
    FDVE_ERROR("audio: %s lost: %s. Trying again every %lld s.", name_.c_str(), reason.c_str(),
               static_cast<long long>(retryInterval_.count() / 1000));
    stream_.reset();
    nextRetry_ = Clock::now() + retryInterval_;
    nextBlock_ = Clock::now();
    if (notice_) notice_(true, reason);
}

void ReconnectingStream::retry()
{
    if (Clock::now() < nextRetry_) return;
    nextRetry_ = Clock::now() + retryInterval_;
    try
    {
        stream_ = opener_();
        FDVE_INFO("audio: %s is back", name_.c_str());
        if (notice_) notice_(false, "");
    }
    catch (const std::exception&)
    {
        // Still missing. Opening takes time, so do not let the silence fall behind.
        nextBlock_ = Clock::now();
    }
}

// Without a device, time has to pass as if there were one: the pump threads
// rely on the stream to set their pace.
void ReconnectingStream::pace(int count)
{
    nextBlock_ += std::chrono::microseconds(1000000LL * count / sampleRate_);
    std::this_thread::sleep_until(nextBlock_);
}

void ReconnectingStream::read(short* samples, int count)
{
    if (stream_)
    {
        try
        {
            stream_->read(samples, count);
            return;
        }
        catch (const std::exception& e)
        {
            lose(e.what());
        }
    }
    else
    {
        retry();
        if (stream_) return read(samples, count);
    }
    std::memset(samples, 0, sizeof(short) * count);
    pace(count);
}

void ReconnectingStream::write(const short* samples, int count)
{
    if (stream_)
    {
        try
        {
            stream_->write(samples, count);
            return;
        }
        catch (const std::exception& e)
        {
            lose(e.what());
        }
    }
    else
    {
        retry();
        if (stream_) return write(samples, count);
    }
    pace(count);
}

} // namespace fdve
