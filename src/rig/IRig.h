#pragma once

#include <memory>
#include <string>

namespace fdve {

// The radio's control connection (CAT). One connection serves several uses:
// keying the transmitter now, reading the frequency for the display and for
// FreeDV Reporter later.
//
// Every call returns quickly. The radio is talked to on a separate thread, so
// a slow or disconnected radio cannot hold up audio or PTT handling.
class IRig
{
public:
    virtual ~IRig() = default;

    // Keys or unkeys the transmitter. Waits up to timeoutMs for the radio to
    // take the command and returns whether it did. On false the request
    // stays in force and is applied as soon as the radio answers.
    virtual bool setPtt(bool transmit, int timeoutMs) = 0;

    virtual bool connected() const = 0;
    virtual long long frequencyHz() const = 0; // 0 while unknown
};

struct RigConfig
{
    std::string type = "none"; // "none" or "hamlib"

    // hamlib
    int model = 0;             // Hamlib model number, from "rigctl -l"
    std::string device;        // serial port, or "host:port" for a network rig such as rigctld
    int baud = 0;              // 0 = the model's default
    std::string ptt = "cat";   // how Hamlib keys the radio: "cat", "rts" or "dtr"
    int civAddress = 0;        // Icom only; 0 = the model's default
    int pollMs = 1000;         // how often the frequency is read
};

// Checks a configuration without connecting to the radio: is Hamlib built
// in, does the model exist, are the settings accepted. Returns "" when it is
// usable, the reason otherwise. createRig() throws for the same reasons.
std::string checkRigConfig(const RigConfig& config);

// Never returns null: with type "none" the result is a rig that is never connected.
std::shared_ptr<IRig> createRig(const RigConfig& config);

} // namespace fdve
