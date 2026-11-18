#pragma once

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "reporting/ReportPolicy.h"

namespace fdve {

struct ReporterSettings
{
    std::string callsign;
    std::string gridSquare;
    std::string message;
    std::string software;  // name and version, for example "freedv-embedded 0.1.0"
    std::string mode;      // "RADEV1"
    bool rxOnly = false;
    bool pskReporter = false;
};

// Spotting on FreeDV Reporter (qso.freedv.org), and optionally PSK Reporter.
// The control loop hands it the station's state with update(); the reports
// themselves are made on a separate thread, so a slow or absent network never
// holds up audio or PTT.
class Reporter
{
public:
    explicit Reporter(const ReporterSettings& settings);
    // For tests: report into this sink instead of the network.
    Reporter(const ReporterSettings& settings, std::unique_ptr<IReportSink> sink);
    ~Reporter();

    void update(const ReportInput& input); // cheap, any thread
    std::string status() const;            // "connecting", "connected", ...

private:
    void run();

    ReporterSettings settings_;
    std::unique_ptr<IReportSink> sink_;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    ReportInput input_;
    bool running_ = true;
    std::thread thread_;
};

// The sink that reports to the real services. Defined in BackendSink.cpp.
std::unique_ptr<IReportSink> createBackendSink(const ReporterSettings& settings);

} // namespace fdve
