#pragma once

#include "display/Framebuffer.h"
#include "modem/ModemStatus.h"

namespace fdve {

// Draws the status screen for a 128x64 display. It only depends on
// ModemStatus and Framebuffer, so the layout can be worked on and tested
// without a display attached.
//
//   +--------------------------------+
//   | RX  RADEV1          12dB       |   state, mode, sync, SNR
//   |     SYNC                       |
//   | AF  [=========        ]        |   level from the radio
//   | MIC [====             ]        |   microphone level
//   |          ON6RF                 |   RX: last callsign heard, TX: own callsign
//   +--------------------------------+
void renderStatusScreen(const ModemStatus& status, Framebuffer& fb);

} // namespace fdve
