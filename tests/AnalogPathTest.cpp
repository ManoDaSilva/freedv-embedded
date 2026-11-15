// Analog path: pass-through at equal rates, sample count and tone level when
// resampling, drop when full, clear.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "audio/AnalogPath.h"

using namespace fdve;

static int failures = 0;
#define CHECK(cond) \
    do { if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); failures++; } } while (0)

int main()
{
    // Equal rates: samples come out unchanged and in order.
    {
        AnalogPath path(48000, 48000);
        std::vector<short> in(960), out(960);
        for (int i = 0; i < 960; i++) in[i] = static_cast<short>(i * 3 - 1000);
        CHECK(!path.read(out.data(), 1));
        path.write(in.data(), 960);
        CHECK(path.available() == 960);
        CHECK(path.read(out.data(), 960) && out == in);
        CHECK(path.available() == 0);

        // Capacity is 500 ms. Writing 2 s keeps at most that and never blocks.
        for (int i = 0; i < 100; i++) path.write(in.data(), 960);
        CHECK(path.available() == 24000);
        path.clear();
        CHECK(path.available() == 0);
    }

    // 48 kHz -> 44.1 kHz: one second in gives about 44100 out, and a 1 kHz
    // tone keeps its level.
    {
        AnalogPath path(48000, 44100, 2000);
        std::vector<short> block(960);
        int produced = 0;
        double sumSquares = 0;
        std::vector<short> out(4096);
        for (int b = 0; b < 50; b++)
        {
            for (int i = 0; i < 960; i++)
                block[i] = static_cast<short>(10000 * std::sin(2 * M_PI * 1000 * (b * 960 + i) / 48000.0));
            path.write(block.data(), 960);
            int n = path.available();
            CHECK(path.read(out.data(), n));
            if (b >= 5) for (int i = 0; i < n; i++) sumSquares += static_cast<double>(out[i]) * out[i];
            if (b >= 5) produced += n;
        }
        const double rms = std::sqrt(sumSquares / produced);
        std::printf("resampled: %d samples for 0.9 s, rms %.0f (expected about %.0f)\n",
                    produced, rms, 10000 / std::sqrt(2.0));
        CHECK(std::abs(produced - 39690) < 200);
        CHECK(std::abs(rms - 7071) < 150);
    }

    std::printf(failures == 0 ? "PASS\n" : "FAILED\n");
    return failures == 0 ? 0 : 1;
}
