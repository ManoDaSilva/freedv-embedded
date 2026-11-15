#pragma once

#include <atomic>
#include <vector>

namespace fdve {

// Straight audio path between the two sound cards, used in analog mode:
// one capture thread writes, one playback thread reads. Resamples when the
// two cards run at different sample rates.
class AnalogPath
{
public:
    AnalogPath(int inputSampleRate, int outputSampleRate, int capacityMs = 500);
    ~AnalogPath();
    AnalogPath(const AnalogPath&) = delete;
    AnalogPath& operator=(const AnalogPath&) = delete;

    void write(const short* samples, int count); // capture thread; drops when full

    // Playback thread only.
    int available() const;
    bool read(short* samples, int count); // false: not enough data
    void clear();                         // drop everything queued

private:
    void push(const short* samples, int count);

    std::vector<short> ring_;
    std::atomic<unsigned> writePos_{0};
    std::atomic<unsigned> readPos_{0};

    double ratio_;
    void* resampler_ = nullptr; // SRC_STATE*, null when the rates are equal
    std::vector<float> floatIn_;
    std::vector<float> floatOut_;
    std::vector<short> converted_;
};

} // namespace fdve
