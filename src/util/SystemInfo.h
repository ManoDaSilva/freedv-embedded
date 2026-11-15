#pragma once

#include <string>

namespace fdve {

// Readings about the computer the program runs on, formatted for a small
// display (15 characters at most). Each returns "n/a" when the system does
// not provide the value.
class SystemInfo
{
public:
    std::string ipAddress() const;   // first IPv4 address that is not loopback
    std::string cpuLoad();           // busy share of all cores since the previous call
    std::string memory() const;      // used / total, in MB
    std::string temperature() const; // CPU temperature

    // For tests: read from other files than /proc and /sys.
    std::string statPath = "/proc/stat";
    std::string meminfoPath = "/proc/meminfo";
    std::string temperaturePath = "/sys/class/thermal/thermal_zone0/temp";

private:
    unsigned long long lastBusy_ = 0;
    unsigned long long lastTotal_ = 0;
    bool haveSample_ = false;
};

} // namespace fdve
