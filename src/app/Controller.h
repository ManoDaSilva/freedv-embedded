#pragma once

#include <atomic>
#include <string>

#include "app/Config.h"

namespace fdve {

// Builds everything from the config and runs the PTT state machine:
//
//   Receive --PTT pressed--> Transmit --PTT released--> TransmitEnding --drained--> Receive
//
// Digital mode: TransmitEnding exists because RADE sends an end-of-over frame
// (it carries the callsign) after the last speech frame; the radio must stay
// keyed for it.
//
// Analog mode: the modem is bypassed. Microphone audio goes straight to the
// radio while transmitting, radio audio goes straight to the headset while
// receiving. PTT handling is the same. The mode only changes while receiving.
//
// A settings menu on the display can change the config file. Saving there ends
// the run with Restart, and the caller starts over with the new file.
enum class RunResult { Stopped, Failed, Restart };

RunResult runController(const Config& config, const std::string& configPath, const std::atomic<bool>& stop);

} // namespace fdve
