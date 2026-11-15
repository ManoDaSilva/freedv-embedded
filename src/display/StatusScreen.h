#pragma once

#include "display/Framebuffer.h"
#include "modem/ModemStatus.h"

namespace fdve {

// Draws the status screen for a 128x64 display. It only depends on
// ModemStatus and Framebuffer, so the layout can be worked on and tested
// without a display attached.
//
//   +--------------------------------+
//   | RADEV1                    SYNC |   mode, sync
//   | SNR            12 dB           |   large; an inverted "TX" band when transmitting
//   | AF  [=============|     ]      |   level from the radio (microphone when transmitting)
//   |           VK5DGR               |   RX: last callsign heard, TX: own callsign
//   +--------------------------------+
void renderStatusScreen(const ModemStatus& status, Framebuffer& fb);

} // namespace fdve
