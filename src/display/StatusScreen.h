#pragma once

#include "display/Framebuffer.h"
#include "modem/ModemStatus.h"

namespace fdve {

// Draws the status screen for a 128x64 display. It only depends on
// ModemStatus and Framebuffer, so the layout can be worked on and tested
// without a display attached.
//
//   +--------------------------------+
//   | RX                      VK5DGR |   rows 0-15: state, callsign (heard in RX, own in TX)
//   |--------------------------------|   colour change on two-colour displays
//   | RADEV1          12 dB          |   SNR, large; an inverted "TX" band when transmitting
//   | SYNC                           |
//   | AF  [=============|     ]      |   level from the radio (microphone when transmitting)
//   +--------------------------------+
//
// The top 16 rows form a self-contained header and row 16 stays empty, so the
// layout also works on two-colour OLEDs (16 rows yellow, 48 rows blue).
void renderStatusScreen(const ModemStatus& status, Framebuffer& fb);

} // namespace fdve
