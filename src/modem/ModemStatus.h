#pragma once

#include <string>

namespace fdve {

enum class ModemState { Receive, Transmit, TransmitEnding };

// Snapshot handed to the display. Everything a front panel needs is here, so a
// display driver never has to reach into the modem or the audio code.
struct ModemStatus
{
    ModemState state = ModemState::Receive;
    bool sync = false;          // receiver is locked to a signal
    int snrDb = 0;              // valid when sync is true
    float radioInDbfs = -99.0f; // AF level from the radio (peak, dBFS)
    float micDbfs = -99.0f;     // microphone level (peak, dBFS)
    std::string rxCallsign;     // last callsign decoded from an end-of-over frame
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
