#include "app/Controller.h"

#include <chrono>
#include <cstdio>
#include <thread>

#include "audio/AudioPump.h"
#include "modem/Modem.h"

namespace fdve {

using Clock = std::chrono::steady_clock;
using std::chrono::milliseconds;

namespace {

AudioStreamConfig streamConfig(const SoundCardConfig& card, const std::string& device, int periodMs)
{
    AudioStreamConfig c;
    c.type = card.type;
    c.device = device;
    c.sampleRate = card.sampleRate;
    c.periodMs = periodMs;
    return c;
}

} // namespace

int runController(const Config& config, const std::atomic<bool>& stop)
{
    // Open the hardware first: these fail fast with a clear message.
    auto pttIn = createPttInput(config.pttInput);
    auto pttOut = createPttOutput(config.pttOutput);
    auto display = createDisplay(config.display);

    auto radioIn = createAudioStream(streamConfig(config.radio, config.radio.captureDevice, config.periodMs), StreamDirection::Capture);
    auto radioOut = createAudioStream(streamConfig(config.radio, config.radio.playbackDevice, config.periodMs), StreamDirection::Playback);
    auto micIn = createAudioStream(streamConfig(config.headset, config.headset.captureDevice, config.periodMs), StreamDirection::Capture);
    auto speakerOut = createAudioStream(streamConfig(config.headset, config.headset.playbackDevice, config.periodMs), StreamDirection::Playback);

    Modem::Params params;
    params.mode = config.mode;
    params.callsign = config.callsign;
    params.radioSampleRate = config.radio.sampleRate;
    params.headsetSampleRate = config.headset.sampleRate;
    Modem modem(params);
    modem.start();

    CapturePump radioCapture("radio", std::move(radioIn), config.periodMs,
        [&](const short* s, int n) { modem.writeRadioIn(s, n); });
    CapturePump micCapture("mic", std::move(micIn), config.periodMs,
        [&](const short* s, int n) { modem.writeMic(s, n); });
    PlaybackPump radioPlayback("radio", std::move(radioOut), config.periodMs, config.prebufferMs,
        {[&] { return modem.radioOutAvailable(); }, [&](short* s, int n) { return modem.readRadioOut(s, n); }});
    PlaybackPump speakerPlayback("speaker", std::move(speakerOut), config.periodMs, config.prebufferMs,
        {[&] { return modem.speakerAvailable(); }, [&](short* s, int n) { return modem.readSpeaker(s, n); }});

    ModemState state = ModemState::Receive;
    Clock::time_point endingDeadline;
    auto nextDisplay = Clock::now();
    int exitCode = 0;

    std::fprintf(stderr, "freedv-embedded running, mode %s\n", config.mode.c_str());

    while (!stop.load())
    {
        const bool pressed = pttIn->pressed();

        switch (state)
        {
            case ModemState::Receive:
                if (pressed)
                {
                    pttOut->set(true);
                    std::this_thread::sleep_for(milliseconds(config.txDelayMs));
                    modem.startTx();
                    state = ModemState::Transmit;
                }
                break;

            case ModemState::Transmit:
                if (!pressed)
                {
                    modem.endTx();
                    endingDeadline = Clock::now() + milliseconds(3000);
                    state = ModemState::TransmitEnding;
                }
                break;

            case ModemState::TransmitEnding:
                if (modem.txDrained() || Clock::now() > endingDeadline)
                {
                    // The last samples are now in the sound card's buffer.
                    // Wait for them to be played before unkeying.
                    std::this_thread::sleep_for(milliseconds(config.txTailMs));
                    modem.finishTx();
                    pttOut->set(false);
                    state = ModemState::Receive;
                }
                break;
        }

        if (Clock::now() >= nextDisplay)
        {
            nextDisplay += milliseconds(config.display.refreshMs);
            ModemStatus status;
            status.state = state;
            status.sync = state == ModemState::Receive && modem.sync();
            status.snrDb = modem.snrDb();
            status.radioInDbfs = radioCapture.peakDbfs();
            status.micDbfs = micCapture.peakDbfs();
            status.rxCallsign = modem.lastRxCallsign();
            display->update(status);
        }

        if (radioCapture.failed() || micCapture.failed() || radioPlayback.failed() || speakerPlayback.failed())
        {
            std::fprintf(stderr, "an audio device failed, exiting\n");
            exitCode = 1;
            break;
        }

        std::this_thread::sleep_for(milliseconds(5));
    }

    // Never leave the radio keyed.
    modem.finishTx();
    pttOut->set(false);
    return exitCode;
    // Pumps are destroyed before the modem (reverse declaration order), so no
    // audio thread touches the modem after it stops.
}

} // namespace fdve
