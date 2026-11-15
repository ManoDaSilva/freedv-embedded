#pragma once

#include <functional>
#include <string>
#include <vector>

namespace fdve {

// Readings about the computer the program runs on, formatted for a small
// display (15 characters at most). Each returns "n/a" when the system does
// not provide the value.
class SystemInfo
{
public:
    struct Address
    {
        std::string interface; // "eth0", "wlan0", ...
        std::string ip;
    };
    // IPv4 addresses of the network interfaces that are up, without loopback.
    // An interface that is down, or up without an address, is not listed.
    std::vector<Address> addresses() const;
    std::string addressOf(const std::string& interface) const; // "down" when it has none now
    std::string cpuLoad();           // busy share of all cores since the previous call
    std::string memory() const;      // used / total, in MB
    std::string temperature() const; // CPU temperature

    // For tests: replaces the query to the operating system.
    std::function<std::vector<Address>()> addressSource;

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
