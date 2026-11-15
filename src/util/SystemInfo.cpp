#include "util/SystemInfo.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>

#include <cstdio>
#include <fstream>
#include <sstream>

namespace fdve {

std::vector<SystemInfo::Address> SystemInfo::addresses() const
{
    if (addressSource) return addressSource();

    std::vector<Address> result;
    struct ifaddrs* list = nullptr;
    if (getifaddrs(&list) != 0) return result;
    for (struct ifaddrs* entry = list; entry != nullptr; entry = entry->ifa_next)
    {
        if (entry->ifa_addr == nullptr || entry->ifa_addr->sa_family != AF_INET) continue;
        if ((entry->ifa_flags & IFF_LOOPBACK) || !(entry->ifa_flags & IFF_UP)) continue;
        char text[INET_ADDRSTRLEN];
        const auto* address = reinterpret_cast<const struct sockaddr_in*>(entry->ifa_addr);
        if (inet_ntop(AF_INET, &address->sin_addr, text, sizeof(text)) == nullptr) continue;
        result.push_back({entry->ifa_name, text});
    }
    freeifaddrs(list);
    return result;
}

std::string SystemInfo::addressOf(const std::string& interface) const
{
    for (const auto& address : addresses())
        if (address.interface == interface) return address.ip;
    return "down";
}

std::string SystemInfo::cpuLoad()
{
    // First line of /proc/stat: "cpu user nice system idle iowait irq softirq steal ..."
    std::ifstream in(statPath);
    std::string label;
    unsigned long long value[8] = {0};
    if (!(in >> label) || label != "cpu") return "n/a";
    for (auto& v : value)
        if (!(in >> v)) return "n/a";

    unsigned long long total = 0;
    for (auto v : value) total += v;
    const unsigned long long idle = value[3] + value[4];
    const unsigned long long busy = total - idle;

    std::string result = "...";
    if (haveSample_ && total > lastTotal_)
        result = std::to_string((busy - lastBusy_) * 100 / (total - lastTotal_)) + " %";
    else if (haveSample_)
        return "..."; // called again before the counters moved: keep the sample
    lastBusy_ = busy;
    lastTotal_ = total;
    haveSample_ = true;
    return result;
}

std::string SystemInfo::memory() const
{
    std::ifstream in(meminfoPath);
    std::string key;
    unsigned long long kilobytes = 0;
    std::string unit;
    unsigned long long total = 0;
    unsigned long long available = 0;
    while (in >> key >> kilobytes)
    {
        std::getline(in, unit);
        if (key == "MemTotal:") total = kilobytes;
        if (key == "MemAvailable:") available = kilobytes;
    }
    if (total == 0) return "n/a";
    return std::to_string((total - available) / 1024) + "/" + std::to_string(total / 1024) + " MB";
}

std::string SystemInfo::temperature() const
{
    std::ifstream in(temperaturePath);
    long milliDegrees = 0;
    if (!(in >> milliDegrees)) return "n/a";
    char text[16];
    std::snprintf(text, sizeof(text), "%.1f C", static_cast<double>(milliDegrees) / 1000.0);
    return text;
}

} // namespace fdve
