#pragma once

#include <atomic>

#include "app/Config.h"

namespace fdve {

// Builds everything from the config and runs the PTT state machine:
//
//   Receive --PTT pressed--> Transmit --PTT released--> TransmitEnding --drained--> Receive
//
// TransmitEnding exists because RADE sends an end-of-over frame (it carries
// the callsign) after the last speech frame; the radio must stay keyed for it.
//
// Returns 0 when "stop" was set, 1 when an audio device failed.
int runController(const Config& config, const std::atomic<bool>& stop);

} // namespace fdve
