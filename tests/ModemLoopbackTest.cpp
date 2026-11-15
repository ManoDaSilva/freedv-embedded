// End-to-end check of the Modem wrapper against the real backend:
// speech-band audio -> TX -> modem signal -> RX -> sync + callsign.

#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>
#include <vector>

#include "modem/Modem.h"

using Clock = std::chrono::steady_clock;
using std::chrono::milliseconds;

int main()
{
    const int rate = 48000;
    const int block = rate / 50; // 20 ms

    fdve::Modem::Params params;
    params.radioSampleRate = rate;
    params.headsetSampleRate = rate;
    params.callsign = "N0CALL";
    fdve::Modem modem(params);
    modem.start();

    // --- Transmit 4 seconds of a voice-like signal and capture the modem output.
    std::vector<short> mic(4 * rate);
    for (size_t i = 0; i < mic.size(); i++)
    {
        double t = static_cast<double>(i) / rate;
        double envelope = 0.5 + 0.5 * std::sin(2 * M_PI * 3 * t);
        mic[i] = static_cast<short>(8000 * envelope *
            (std::sin(2 * M_PI * 150 * t) + 0.5 * std::sin(2 * M_PI * 450 * t) + 0.3 * std::sin(2 * M_PI * 1200 * t)));
    }

    std::vector<short> onAir;
    std::vector<short> buffer(block);
    auto drainRadio = [&] {
        while (modem.radioOutAvailable() >= block && modem.readRadioOut(buffer.data(), block))
            onAir.insert(onAir.end(), buffer.begin(), buffer.end());
    };

    modem.startTx();
    auto deadline = Clock::now() + milliseconds(60000);
    for (size_t pos = 0; pos + block <= mic.size() && Clock::now() < deadline;)
    {
        if (modem.writeMic(mic.data() + pos, block)) pos += block;
        else std::this_thread::sleep_for(milliseconds(2));
        drainRadio();
    }
    modem.endTx();
    while (Clock::now() < deadline)
    {
        drainRadio();
        int rest = modem.radioOutAvailable();
        if (rest > 0 && rest < block && modem.readRadioOut(buffer.data(), rest))
            onAir.insert(onAir.end(), buffer.begin(), buffer.begin() + rest);
        if (modem.txDrained()) break;
        std::this_thread::sleep_for(milliseconds(2));
    }
    const bool drained = modem.txDrained();
    modem.finishTx();

    int peak = 0;
    for (short s : onAir) peak = std::max(peak, std::abs(static_cast<int>(s)));
    std::printf("TX: %.2f s of modem signal, peak %d, drained=%d\n",
                static_cast<double>(onAir.size()) / rate, peak, drained);

    // --- Receive it, followed by silence.
    onAir.resize(onAir.size() + 2 * rate, 0);
    bool sawSync = false;
    size_t speechSamples = 0;
    deadline = Clock::now() + milliseconds(60000);
    for (size_t pos = 0; pos + block <= onAir.size() && Clock::now() < deadline;)
    {
        if (modem.writeRadioIn(onAir.data() + pos, block)) pos += block;
        else std::this_thread::sleep_for(milliseconds(2));
        sawSync = sawSync || modem.sync();
        while (modem.speakerAvailable() >= block && modem.readSpeaker(buffer.data(), block))
            speechSamples += block;
    }
    std::this_thread::sleep_for(milliseconds(500));
    sawSync = sawSync || modem.sync();

    const std::string callsign = modem.lastRxCallsign();
    std::printf("RX: sync=%d snr=%d dB, %.2f s of speech, callsign='%s'\n",
                sawSync, modem.snrDb(), static_cast<double>(speechSamples) / rate, callsign.c_str());
    modem.stop();

    const bool ok = drained && peak > 1000 && sawSync && speechSamples > static_cast<size_t>(2 * rate) && callsign == "N0CALL";
    std::printf(ok ? "PASS\n" : "FAILED\n");
    return ok ? 0 : 1;
}
