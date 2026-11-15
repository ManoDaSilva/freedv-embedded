#pragma once

namespace fdve {

// PTT input: "the operator wants to transmit" (button, later a CMX108 or similar).
class IPttInput
{
public:
    virtual ~IPttInput() = default;
    virtual bool pressed() = 0; // polled every few milliseconds
};

// PTT output: keys the radio. With "none" the radio keys itself (sound card VOX).
class IPttOutput
{
public:
    virtual ~IPttOutput() = default;
    virtual void set(bool transmit) = 0;
};

} // namespace fdve
