#include "app/Controller.h"

#include <chrono>
#include <mutex>
#include <thread>

#include "audio/AnalogPath.h"
#include "audio/AudioPump.h"
#include "display/StatusScreen.h"
#include "modem/Modem.h"
#include "ui/SettingsMenu.h"
#include "util/Log.h"

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

// The user interface thread: reads the knob, runs the settings menu and feeds
// the displays. It is separate from the control loop so that a slow display
// (an OLED on I2C takes tens of milliseconds per frame) or a busy I2C bus
// never delays PTT handling.
class UiThread
{
public:
    UiThread(const Config& config, const std::string& configPath)
        : display_(createDisplay(config.display)),
          input_(createMenuInput(config.menu)),
          settings_(config, configPath),
          refreshMs_(config.display.refreshMs),
          timeoutS_(config.menu.timeoutS),
          thread_(&UiThread::run, this)
    {
    }

    ~UiThread()
    {
        running_.store(false);
        thread_.join();
    }

    void publish(const ModemStatus& status)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        status_ = status;
    }

    bool restartRequested() const { return settings_.restartRequested(); }

private:
    void run()
    {
        settings_.applyStoredVolumes();
        Menu& menu = settings_.menu();
        Framebuffer frame;
        auto nextRefresh = Clock::now();
        auto lastInput = Clock::now();

        while (running_.load())
        {
            const auto now = Clock::now();
            const MenuEvents events = input_->poll();
            bool redraw = false;
            if (events.any())
                FDVE_DEBUG("menu input: steps=%d clicks=%d backs=%d (menu %s, on \"%s\")", events.steps, events.clicks,
                           events.backs, menu.active() ? "open" : "closed", menu.selectedLabel().c_str());

            if (menu.active())
            {
                if (events.any())
                {
                    redraw = menu.handle(events);
                    lastInput = now;
                }
                else if (now - lastInput > std::chrono::seconds(timeoutS_))
                {
                    menu.close(); // unsaved changes stay pending
                    redraw = true;
                }
            }
            else if (events.clicks > 0)
            {
                menu.open(); // a press on the status screen opens the menu
                lastInput = now;
                redraw = true;
            }

            const bool tick = now >= nextRefresh;
            if (tick || redraw)
            {
                ModemStatus snapshot;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    snapshot = status_;
                }
                if (tick)
                {
                    nextRefresh = now + milliseconds(refreshMs_);
                    display_->update(snapshot);
                }
                if (menu.active()) menu.render(frame);
                else renderStatusScreen(snapshot, frame);
                display_->show(frame);
            }
            std::this_thread::sleep_for(milliseconds(10));
        }
    }

    std::unique_ptr<IDisplay> display_;
    std::unique_ptr<IMenuInput> input_;
    SettingsMenu settings_;
    int refreshMs_;
    int timeoutS_;
    std::mutex mutex_;
    ModemStatus status_;
    std::atomic<bool> running_{true};
    std::thread thread_;
};

} // namespace

RunResult runController(const Config& config, const std::string& configPath, const std::atomic<bool>& stop)
{
    // Open the hardware first: these fail fast with a clear message.
    auto pttIn = createPttInput(config.pttInput);
    auto pttOut = createPttOutput(config.pttOutput);
    auto modeSwitch = createModeSwitch(config.modeSwitch, config.startAnalog);
    UiThread ui(config, configPath);

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

    // Analog mode paths: mic -> radio and radio -> headset, without the modem.
    AnalogPath analogTx(config.headset.sampleRate, config.radio.sampleRate);
    AnalogPath analogRx(config.radio.sampleRate, config.headset.sampleRate);

    // Read by the audio threads, written only by this thread.
    std::atomic<bool> analog{config.startAnalog};
    std::atomic<bool> micOpen{false};      // state is Transmit
    std::atomic<bool> transmitting{false}; // state is not Receive

    CapturePump radioCapture("radio", std::move(radioIn), config.periodMs,
        [&](const short* s, int n) {
            if (!analog.load()) modem.writeRadioIn(s, n);
            else if (!transmitting.load()) analogRx.write(s, n);
        });
    CapturePump micCapture("mic", std::move(micIn), config.periodMs,
        [&](const short* s, int n) {
            if (!analog.load()) modem.writeMic(s, n);
            else if (micOpen.load()) analogTx.write(s, n);
        });

    // The playback side picks its source by mode. The unused analog path is
    // emptied from here (the reading thread), so nothing stale is played
    // after the next mode change.
    PlaybackPump radioPlayback("radio", std::move(radioOut), config.periodMs, config.prebufferMs,
        {[&] {
             if (analog.load()) return analogTx.available();
             analogTx.clear();
             return modem.radioOutAvailable();
         },
         [&](short* s, int n) { return analog.load() ? analogTx.read(s, n) : modem.readRadioOut(s, n); }});
    PlaybackPump speakerPlayback("speaker", std::move(speakerOut), config.periodMs, config.prebufferMs,
        {[&] {
             if (analog.load() && !transmitting.load()) return analogRx.available();
             analogRx.clear();
             return analog.load() ? 0 : modem.speakerAvailable();
         },
         [&](short* s, int n) {
             if (analog.load()) return !transmitting.load() && analogRx.read(s, n);
             return modem.readSpeaker(s, n);
         }});

    ModemState state = ModemState::Receive;
    Clock::time_point endingDeadline;
    RunResult result = RunResult::Stopped;

    FDVE_INFO("freedv-embedded running, mode %s", analog.load() ? "analog" : config.mode.c_str());

    while (!stop.load())
    {
        const bool pressed = pttIn->pressed();
        const bool wantAnalog = modeSwitch->analog();

        // The mode only changes between overs.
        if (state == ModemState::Receive && wantAnalog != analog.load())
        {
            analog.store(wantAnalog);
            FDVE_INFO("mode: %s", wantAnalog ? "analog" : config.mode.c_str());
        }
        const bool isAnalog = analog.load();

        switch (state)
        {
            case ModemState::Receive:
                if (pressed)
                {
                    pttOut->set(true);
                    std::this_thread::sleep_for(milliseconds(config.txDelayMs));
                    if (!isAnalog) modem.startTx();
                    state = ModemState::Transmit;
                    FDVE_DEBUG("PTT pressed, transmitting");
                }
                break;

            case ModemState::Transmit:
                if (!pressed)
                {
                    if (!isAnalog) modem.endTx();
                    endingDeadline = Clock::now() + milliseconds(3000);
                    state = ModemState::TransmitEnding;
                    FDVE_DEBUG("PTT released, ending the over");
                }
                break;

            case ModemState::TransmitEnding:
            {
                const bool drained = isAnalog ? analogTx.available() == 0 : modem.txDrained();
                if (drained || Clock::now() > endingDeadline)
                {
                    if (!drained) FDVE_WARN("transmit audio did not drain in time");
                    // The last samples are now in the sound card's buffer.
                    // Wait for them to be played before unkeying.
                    std::this_thread::sleep_for(milliseconds(config.txTailMs));
                    if (!isAnalog) modem.finishTx();
                    pttOut->set(false);
                    state = ModemState::Receive;
                    FDVE_DEBUG("back to receive");
                }
                break;
            }
        }
        micOpen.store(state == ModemState::Transmit);
        transmitting.store(state != ModemState::Receive);

        ModemStatus status;
        status.state = state;
        status.analog = isAnalog;
        status.modeName = config.mode;
        status.sync = !isAnalog && state == ModemState::Receive && modem.sync();
        status.snrDb = modem.snrDb();
        status.radioInDbfs = radioCapture.peakDbfs();
        status.micDbfs = micCapture.peakDbfs();
        status.rxCallsign = modem.lastRxCallsign();
        status.ownCallsign = config.callsign;
        ui.publish(status);

        if (radioCapture.failed() || micCapture.failed() || radioPlayback.failed() || speakerPlayback.failed())
        {
            FDVE_ERROR("an audio device failed, exiting");
            result = RunResult::Failed;
            break;
        }

        // Settings were saved from the menu. Restart between overs only.
        if (ui.restartRequested() && state == ModemState::Receive)
        {
            std::this_thread::sleep_for(milliseconds(700)); // let the "Saved" message be read
            result = RunResult::Restart;
            break;
        }

        std::this_thread::sleep_for(milliseconds(5));
    }

    // Never leave the radio keyed.
    modem.finishTx();
    pttOut->set(false);
    return result;
    // Destruction runs in reverse declaration order: the pumps, then the
    // analog paths and the modem, then the user interface thread. No audio
    // thread touches the modem after it stops.
}

} // namespace fdve
