#include "display/ConsoleDisplay.h"

#include <cstdio>

namespace fdve {

std::string ConsoleDisplay::format(const ModemStatus& s)
{
    char snr[16] = "  --";
    if (s.sync) std::snprintf(snr, sizeof(snr), "%4d", s.snrDb);

    char line[160];
    std::snprintf(line, sizeof(line), "%-6s %-6s sync=%s snr=%s dB  af=%4.0f dBFS  mic=%4.0f dBFS  call=%s",
                  toString(s.state), s.analog ? "ANALOG" : s.modeName.c_str(), s.sync ? "yes" : "no ", snr,
                  s.radioInDbfs, s.micDbfs, s.rxCallsign.empty() ? "-" : s.rxCallsign.c_str());
    std::string result = line;
    if (s.rigConfigured)
    {
        char rig[48] = "  rig=offline";
        if (s.rigConnected && s.rigFrequencyHz > 0)
            std::snprintf(rig, sizeof(rig), "  rig=%.4f MHz", static_cast<double>(s.rigFrequencyHz) / 1e6);
        else if (s.rigConnected)
            std::snprintf(rig, sizeof(rig), "  rig=connected");
        result += rig;
    }
    return result;
}

void ConsoleDisplay::update(const ModemStatus& status)
{
    std::string line = format(status);
    if (line == last_) return;
    last_ = line;
    std::fprintf(stderr, "%s\n", line.c_str());
}

} // namespace fdve
