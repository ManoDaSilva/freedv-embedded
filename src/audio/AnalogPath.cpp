#include "audio/AnalogPath.h"

#include <samplerate.h>

#include <algorithm>
#include <stdexcept>

namespace fdve {

AnalogPath::AnalogPath(int inputSampleRate, int outputSampleRate, int capacityMs)
    : ring_(static_cast<size_t>(outputSampleRate) * capacityMs / 1000 + 1),
      ratio_(static_cast<double>(outputSampleRate) / inputSampleRate)
{
    if (inputSampleRate != outputSampleRate)
    {
        int error = 0;
        resampler_ = src_new(SRC_SINC_FASTEST, 1, &error);
        if (resampler_ == nullptr)
            throw std::runtime_error(std::string("cannot create resampler: ") + src_strerror(error));
    }
}

AnalogPath::~AnalogPath()
{
    if (resampler_ != nullptr) src_delete(static_cast<SRC_STATE*>(resampler_));
}

void AnalogPath::write(const short* samples, int count)
{
    if (resampler_ == nullptr)
    {
        push(samples, count);
        return;
    }

    floatIn_.resize(count);
    floatOut_.resize(static_cast<size_t>(count * ratio_) + 16);
    src_short_to_float_array(samples, floatIn_.data(), count);

    SRC_DATA data{};
    data.data_in = floatIn_.data();
    data.input_frames = count;
    data.data_out = floatOut_.data();
    data.output_frames = static_cast<long>(floatOut_.size());
    data.src_ratio = ratio_;
    if (src_process(static_cast<SRC_STATE*>(resampler_), &data) != 0) return;

    converted_.resize(data.output_frames_gen);
    src_float_to_short_array(floatOut_.data(), converted_.data(), static_cast<int>(data.output_frames_gen));
    push(converted_.data(), static_cast<int>(converted_.size()));
}

void AnalogPath::push(const short* samples, int count)
{
    const unsigned size = static_cast<unsigned>(ring_.size());
    const unsigned w = writePos_.load(std::memory_order_relaxed);
    const unsigned r = readPos_.load(std::memory_order_acquire);
    const unsigned used = (w + size - r) % size;
    if (static_cast<unsigned>(count) > size - 1 - used) return; // full: drop the block
    for (int i = 0; i < count; i++) ring_[(w + i) % size] = samples[i];
    writePos_.store((w + count) % size, std::memory_order_release);
}

int AnalogPath::available() const
{
    const unsigned size = static_cast<unsigned>(ring_.size());
    return static_cast<int>((writePos_.load(std::memory_order_acquire) + size -
                             readPos_.load(std::memory_order_relaxed)) % size);
}

bool AnalogPath::read(short* samples, int count)
{
    if (available() < count) return false;
    const unsigned size = static_cast<unsigned>(ring_.size());
    const unsigned r = readPos_.load(std::memory_order_relaxed);
    for (int i = 0; i < count; i++) samples[i] = ring_[(r + i) % size];
    readPos_.store((r + count) % size, std::memory_order_release);
    return true;
}

void AnalogPath::clear()
{
    readPos_.store(writePos_.load(std::memory_order_acquire), std::memory_order_release);
}

} // namespace fdve
