// Reports through the FreeDV backend's clients for FreeDV Reporter and
// PSK Reporter. This is the only file that includes their headers.

#include <atomic>
#include <chrono>
#include <memory>

#include "FreeDVReporter.h"
#include "pskreporter.h"
#include "reporting/Reporter.h"
#include "util/Log.h"

namespace fdve {

namespace {

constexpr auto kPskReporterInterval = std::chrono::minutes(5); // as the upstream programs do

class BackendSink : public IReportSink
{
public:
    explicit BackendSink(const ReporterSettings& settings)
    {
        // Report-only connection ("write only": we do not need the list of
        // other stations), over TLS to the default server.
        freedv_ = std::make_unique<FreeDVReporter>("", settings.callsign, settings.gridSquare, settings.software,
                                                   settings.rxOnly, true, true);
        freedv_->setOnReporterConnectFn([this] { status_.store(Status::Connecting); });
        freedv_->setConnectionSuccessfulFn([this] {
            if (status_.exchange(Status::Connected) != Status::Connected) FDVE_INFO("reporting: connected to FreeDV Reporter");
        });
        freedv_->setOnReporterDisconnectFn([this] {
            if (status_.exchange(Status::Disconnected) == Status::Connected)
                FDVE_WARN("reporting: FreeDV Reporter connection lost, reconnecting");
        });
        freedv_->connect();
        if (!settings.message.empty()) freedv_->updateMessage(settings.message);

        if (settings.pskReporter)
        {
            psk_ = std::make_unique<PskReporter>(settings.callsign, settings.gridSquare, settings.software);
            nextPsk_ = std::chrono::steady_clock::now() + kPskReporterInterval;
        }
    }

    ~BackendSink() override
    {
        if (psk_) psk_->send(); // do not lose what was heard since the last upload
        freedv_.reset();        // before the members its callbacks use
    }

    void frequency(uint64_t hz) override { freedv_->freqChange(hz); }
    void transmit(const std::string& mode, bool tx) override { freedv_->transmit(mode, tx); }
    void analog(bool inAnalog) override { freedv_->inAnalogMode(inAnalog); }

    void received(const std::string& callsign, const std::string& mode, uint64_t hz, int snr, bool pskToo) override
    {
        if (!callsign.empty()) FDVE_INFO("reporting: heard %s, SNR %d dB", callsign.c_str(), snr);
        freedv_->addReceiveRecord(callsign, mode, hz, static_cast<signed char>(snr));
        if (psk_ && pskToo && !callsign.empty()) psk_->addReceiveRecord(callsign, mode, hz, static_cast<signed char>(snr));
    }

    void tick(std::chrono::steady_clock::time_point now) override
    {
        if (!psk_ || now < nextPsk_) return;
        nextPsk_ = now + kPskReporterInterval;
        psk_->send();
    }

    std::string status() const override
    {
        switch (status_.load())
        {
            case Status::Connected: return "connected";
            case Status::Disconnected: return "offline";
            case Status::Connecting: break;
        }
        return "connecting";
    }

private:
    enum class Status { Connecting, Connected, Disconnected };
    std::unique_ptr<FreeDVReporter> freedv_;
    std::unique_ptr<PskReporter> psk_;
    std::chrono::steady_clock::time_point nextPsk_{};
    std::atomic<Status> status_{Status::Connecting};
};

} // namespace

std::unique_ptr<IReportSink> createBackendSink(const ReporterSettings& settings)
{
    return std::make_unique<BackendSink>(settings);
}

} // namespace fdve
