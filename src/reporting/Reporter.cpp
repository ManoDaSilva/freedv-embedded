#include "reporting/Reporter.h"

#include <chrono>

#include "util/Log.h"

namespace fdve {

using Clock = std::chrono::steady_clock;

Reporter::Reporter(const ReporterSettings& settings) : Reporter(settings, createBackendSink(settings)) {}

Reporter::Reporter(const ReporterSettings& settings, std::unique_ptr<IReportSink> sink)
    : settings_(settings), sink_(std::move(sink))
{
    FDVE_INFO("reporting: %s at %s as \"%s\"%s%s", settings.callsign.c_str(), settings.gridSquare.c_str(),
              settings.software.c_str(), settings.rxOnly ? ", receive only" : "",
              settings.pskReporter ? ", with PSK Reporter" : "");
    thread_ = std::thread(&Reporter::run, this);
}

Reporter::~Reporter()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        running_ = false;
    }
    wake_.notify_all();
    thread_.join();
}

void Reporter::update(const ReportInput& input)
{
    std::lock_guard<std::mutex> lock(mutex_);
    input_ = input;
}

std::string Reporter::status() const
{
    return sink_->status();
}

void Reporter::run()
{
    ReportPolicy policy(*sink_, settings_.mode);
    for (;;)
    {
        ReportInput input;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait_for(lock, std::chrono::milliseconds(100), [&] { return !running_; });
            if (!running_) return;
            input = input_;
        }
        policy.step(input, Clock::now());
    }
}

} // namespace fdve
