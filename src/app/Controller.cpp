#include "app/Controller.h"

#include <chrono>
#include <mutex>
#include <thread>

#include "audio/AnalogPath.h"
#include "audio/AudioPump.h"
#include "audio/ReconnectingStream.h"
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

    // Shows a message on the display until the knob is pressed, or until
    // clearNotice() says its reason has gone.
    void notify(const std::string& title, const std::string& text)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        noticeTitle_ = title;
        noticeText_ = text;
        noticeCleared_ = false;
    }

    // The problem a notice reported is over: take the message off the display.
    void clearNotice()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        noticeTitle_.clear();
        noticeCleared_ = true;
    }

    // Short presses on the status screen since the last call.
    int takeModeToggles() { return modeToggles_.exchange(0); }

    bool restartRequested() const { return settings_.restartRequested(); }
    bool shutdownRequested() const { return settings_.shutdownRequested(); }

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
            // Nothing in here may end the program: a display or lamp that
            // stops answering is reported and tried again.
            try
            {
                step(menu, frame, nextRefresh, lastInput);
                failing_ = false;
            }
            catch (const std::exception& e)
            {
                if (!failing_) FDVE_WARN("user interface: %s", e.what());
                failing_ = true;
            }
            std::this_thread::sleep_for(milliseconds(10));
        }
    }

    void step(Menu& menu, Framebuffer& frame, Clock::time_point& nextRefresh, Clock::time_point& lastInput)
    {
        {
            const auto now = Clock::now();
            const MenuEvents events = input_->poll();
            bool redraw = false;

            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (!noticeTitle_.empty())
                {
                    // Remember whether the notice opened the menu, so that
                    // taking it away again leaves things as they were.
                    noticeOpenedMenu_ = !menu.active();
                    menu.showMessage(noticeTitle_, noticeText_);
                    noticeTitle_.clear();
                    noticeShowing_ = true;
                    lastInput = now;
                    redraw = true;
                }
                else if (noticeCleared_)
                {
                    noticeCleared_ = false;
                    if (noticeShowing_ && menu.dismissMessage())
                    {
                        if (noticeOpenedMenu_) menu.close();
                        redraw = true;
                    }
                    noticeShowing_ = false;
                }
            }
            if (events.any())
                FDVE_DEBUG("menu input: steps=%d clicks=%d backs=%d (menu %s, on \"%s\")", events.steps, events.clicks,
                           events.backs, menu.active() ? "open" : "closed", menu.selectedLabel().c_str());

            if (menu.active())
            {
                if (events.any())
                {
                    redraw = menu.handle(events);
                    lastInput = now;
                    noticeShowing_ = false; // the operator took over
                }
                else if (now - lastInput > std::chrono::seconds(timeoutS_))
                {
                    menu.close(); // unsaved changes stay pending
                    redraw = true;
                }
            }
            else if (events.backs > 0)
            {
                menu.open(); // a long press on the status screen opens the menu
                lastInput = now;
                redraw = true;
            }
            else
            {
                // A short press on the status screen switches between digital
                // and analog. The control loop does the switching.
                if (events.clicks > 0) modeToggles_.fetch_add(events.clicks);

                // Turning the knob on the status screen sets the headphone
                // volume. The level is shown for a moment, and written to the
                // config file once the knob has been still for a while.
                if (events.steps != 0)
                {
                    const int percent = settings_.adjustHeadphones(events.steps);
                    if (percent >= 0)
                    {
                        volumePercent_ = percent;
                        volumeShownUntil_ = now + milliseconds(1500);
                        volumeSaveAt_ = now + milliseconds(3000);
                        volumeUnsaved_ = true;
                        redraw = true;
                    }
                    else if (!volumeWarned_)
                    {
                        FDVE_INFO("the knob cannot set the volume: the headset card has no playback volume control");
                        volumeWarned_ = true;
                    }
                }
            }
            if (volumeUnsaved_ && now >= volumeSaveAt_)
            {
                settings_.saveVolumeChanges();
                volumeUnsaved_ = false;
            }
            const bool showVolume = !menu.active() && now < volumeShownUntil_;
            if (volumeWasShown_ && !showVolume) redraw = true; // back to the status screen at once
            volumeWasShown_ = showVolume;

            ModemStatus snapshot;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                snapshot = status_;
            }
            settings_.setRigStatus(snapshot.rigConfigured, snapshot.rigConnected, snapshot.rigFrequencyHz);

            // Lights around the knob: its position while the menu is open,
            // otherwise the same level as the bar on the status screen.
            if (menu.active()) input_->showKnobPosition();
            else if (showVolume) input_->showLevel(static_cast<float>(volumePercent_) / 100.0f);
            else input_->showLevel(statusLevelFraction(snapshot));
            input_->showState(snapshot.state != ModemState::Receive, snapshot.sync);

            const bool tick = now >= nextRefresh;
            if (tick || redraw)
            {
                if (tick)
                {
                    nextRefresh = now + milliseconds(refreshMs_);
                    display_->update(snapshot);
                }
                if (menu.active()) menu.render(frame);
                else if (showVolume) renderVolumeScreen(volumePercent_, frame);
                else renderStatusScreen(snapshot, frame);
                display_->show(frame);
            }
        }
    }

    std::unique_ptr<IDisplay> display_;
    std::unique_ptr<IMenuInput> input_;
    SettingsMenu settings_;
    int refreshMs_;
    int timeoutS_;
    std::mutex mutex_;
    ModemStatus status_;
    std::string noticeTitle_;
    std::string noticeText_;
    bool noticeCleared_ = false;     // set by clearNotice(), under the mutex
    bool noticeShowing_ = false;     // user interface thread only
    bool noticeOpenedMenu_ = false;  // user interface thread only
    bool failing_ = false;
    int volumePercent_ = 0;
    Clock::time_point volumeShownUntil_{};
    Clock::time_point volumeSaveAt_{};
    bool volumeUnsaved_ = false;
    bool volumeWasShown_ = false;
    bool volumeWarned_ = false;
    std::atomic<int> modeToggles_{0};
    std::atomic<bool> running_{true};
    std::thread thread_;
};

} // namespace

RunResult runController(const Config& config, const std::string& configPath, const std::atomic<bool>& stop)
{
    // Open the hardware first: these fail fast with a clear message.
    auto pttIn = createPttInput(config.pttInput);
    // Radio control. As with the sound devices further down: with a settings
    // menu available, a problem here must not end the program, or it could
    // not be fixed from the menu. Carry on without radio control and say so
    // on the display.
    std::shared_ptr<IRig> rig;
    std::string rigProblem;
    try
    {
        rig = createRig(config.rig);
    }
    catch (const std::exception& e)
    {
        if (config.menu.type == "none") throw;
        rigProblem = e.what();
        FDVE_ERROR("%s. Running without radio control.", e.what());
        rig = createRig(RigConfig{});
    }
    auto pttOut = createPttOutput(config.pttOutput, rig);
    auto modeSwitch = createModeSwitch(config.modeSwitch, config.startAnalog);
    UiThread ui(config, configPath);
    if (!rigProblem.empty()) ui.notify("Rig error", rigProblem);

    // Sound devices. Each one sits behind a stream that survives the device
    // going away: unplugging the radio's USB cable takes its sound card with
    // it, and that must not end the program. While a device is missing, its
    // direction runs on silence and the device is tried again every 2 seconds.
    //
    // At start-up the rule is: with a settings menu available, a device that
    // does not open is treated the same way, so another one can be picked in
    // the menu. Without a menu, failing at once is more useful.
    std::string audioProblems;
    std::atomic<int> devicesMissing{0};
    auto openStream = [&](const SoundCardConfig& card, const std::string& device, StreamDirection direction,
                          const char* name) -> std::unique_ptr<IAudioStream> {
        const AudioStreamConfig streamSettings = streamConfig(card, device, config.periodMs);
        auto opener = [streamSettings, direction] { return createAudioStream(streamSettings, direction); };
        std::unique_ptr<IAudioStream> stream;
        try
        {
            stream = opener();
        }
        catch (const std::exception& e)
        {
            if (config.menu.type == "none") throw;
            FDVE_ERROR("%s: %s. Running without it; it is tried again every 2 seconds.", name, e.what());
            audioProblems += std::string(audioProblems.empty() ? "" : ", ") + name;
            devicesMissing.fetch_add(1);
        }
        const std::string label = name;
        return std::make_unique<ReconnectingStream>(
            label, card.sampleRate, opener, std::move(stream),
            [&ui, &devicesMissing, label](bool lost, const std::string&) {
                if (lost)
                {
                    devicesMissing.fetch_add(1);
                    ui.notify("Audio error", label + " is gone. It is tried again every 2 seconds.");
                }
                else if (devicesMissing.fetch_sub(1) == 1)
                {
                    ui.clearNotice(); // the last missing device is back
                }
            });
    };
    auto radioIn = openStream(config.radio, config.radio.captureDevice, StreamDirection::Capture, "Radio in");
    auto radioOut = openStream(config.radio, config.radio.playbackDevice, StreamDirection::Playback, "Radio out");
    auto micIn = openStream(config.headset, config.headset.captureDevice, StreamDirection::Capture, "Mic");
    auto speakerOut = openStream(config.headset, config.headset.playbackDevice, StreamDirection::Playback, "Headphones");
    if (!audioProblems.empty())
        ui.notify("Audio error", "Cannot open: " + audioProblems + ". Pick a device under Audio, or plug it in." +
                                     (rigProblem.empty() ? "" : " Also: " + rigProblem));

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
    bool knobFlip = false;
    const bool levelSwitch = config.modeSwitch.type == "gpio" && config.modeSwitch.action == "level";
    Clock::time_point endingDeadline;
    RunResult result = RunResult::Stopped;

    FDVE_INFO("freedv-embedded running, mode %s", analog.load() ? "analog" : config.mode.c_str());

    while (!stop.load())
    {
        const bool pressed = pttIn->pressed();

        // The mode comes from the mode switch, and a short press on the menu
        // knob flips it. A latching switch ("level") is the exception: its
        // position is the mode, so the knob does not override it.
        if (ui.takeModeToggles() % 2 != 0)
        {
            if (levelSwitch) FDVE_INFO("mode: the knob press is ignored, the mode switch sets the mode");
            else knobFlip = !knobFlip;
        }
        const bool wantAnalog = modeSwitch->analog() != knobFlip;

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
        status.rigConfigured = config.rig.type != "none";
        status.rigConnected = rig->connected();
        status.rigFrequencyHz = rig->frequencyHz();
        ui.publish(status);

        if (radioCapture.failed() || micCapture.failed() || radioPlayback.failed() || speakerPlayback.failed())
        {
            FDVE_ERROR("an audio device failed, exiting");
            result = RunResult::Failed;
            break;
        }

        // "Shut down" was chosen in the menu. Like a restart, between overs only.
        if (ui.shutdownRequested() && state == ModemState::Receive)
        {
            std::this_thread::sleep_for(milliseconds(1500)); // let the message be read
            result = RunResult::Shutdown;
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
