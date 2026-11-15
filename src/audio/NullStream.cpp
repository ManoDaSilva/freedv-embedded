#include "audio/NullStream.h"

#include <cstring>
#include <thread>

namespace fdve {

NullStream::NullStream(int sampleRate)
    : sampleRate_(sampleRate), next_(std::chrono::steady_clock::now())
{
}

void NullStream::pace(int count)
{
    next_ += std::chrono::microseconds(1000000LL * count / sampleRate_);
    std::this_thread::sleep_until(next_);
}

void NullStream::read(short* samples, int count)
{
    std::memset(samples, 0, sizeof(short) * count);
    pace(count);
}

void NullStream::write(const short*, int count)
{
    pace(count);
}

} // namespace fdve
