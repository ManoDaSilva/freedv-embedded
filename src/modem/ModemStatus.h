#pragma once

#include <string>

namespace fdve {

enum class ModemState { Receive, Transmit, TransmitEnding };

// Snapshot handed to the display. Everything a front panel needs is here, so a
// display driver never has to reach into the modem or the audio code.
struct ModemStatus
{
    ModemState state = ModemState::Receive;
    bool analog = false;        // analog (pass-through) mode, modem bypassed
    std::string modeName;       // digital mode name, for example "RADEV1"
    bool sync = false;          // receiver is locked to a signal (digital only)
    int snrDb = 0;              // valid when sync is true
    float radioInDbfs = -99.0f; // AF level from the radio (peak, dBFS)
    float micDbfs = -99.0f;     // microphone level (peak, dBFS)
    std::string rxCallsign;     // last callsign decoded from an end-of-over frame
    std::string ownCallsign;    // from the config file, may be empty
};

inline const char* toString(ModemState s)
{
    switch (s)
    {
        case ModemState::Receive: return "RX";
        case ModemState::Transmit: return "TX";
        case ModemState::TransmitEnding: return "TX-END";
    }
    return "?";
}

} // namespace fdve
