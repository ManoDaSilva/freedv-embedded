// Renders the status screen for a few situations and checks the parts that
// carry information. With a directory as argument it also writes each screen
// as a PBM image, to look at the layout without a display.

#include <cstdio>
#include <fstream>
#include <string>

#include "display/Framebuffer.h"
#include "display/StatusScreen.h"

using namespace fdve;

static int failures = 0;
#define CHECK(cond) \
    do { if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); failures++; } } while (0)

static int litPixels(const Framebuffer& fb, int x, int y, int w, int h)
{
    int count = 0;
    for (int yy = y; yy < y + h; yy++)
        for (int xx = x; xx < x + w; xx++) count += fb.pixel(xx, yy);
    return count;
}

// Number of lit pixels along the middle row of the level bar's inside.
static int barFill(const Framebuffer& fb)
{
    return litPixels(fb, 26, 52, 100, 1);
}

int main(int argc, char** argv)
{
    const std::string outDir = argc > 1 ? argv[1] : "";
    auto render = [&](const char* name, const ModemStatus& status) {
        Framebuffer fb;
        renderStatusScreen(status, fb);
        if (!outDir.empty()) std::ofstream(outDir + "/" + name + ".pbm") << fb.toPbm();
        return fb;
    };

    ModemStatus idle;
    idle.modeName = "RADEV1";
    idle.ownCallsign = "ON6RF";
    auto fbIdle = render("1_rx_idle", idle);

    ModemStatus rx = idle;
    rx.sync = true;
    rx.snrDb = 12;
    rx.radioInDbfs = -15;
    rx.rxCallsign = "VK5DGR";
    auto fbRx = render("2_rx_sync", rx);

    ModemStatus tx = idle;
    tx.state = ModemState::Transmit;
    tx.micDbfs = -6;
    tx.radioInDbfs = -40;
    auto fbTx = render("3_tx", tx);

    ModemStatus analog = idle;
    analog.analog = true;
    analog.radioInDbfs = -25;
    auto fbAnalog = render("4_analog_rx", analog);

    // Framebuffer basics.
    Framebuffer fb;
    fb.setPixel(0, 0);
    fb.setPixel(127, 63);
    fb.setPixel(500, 500); // out of range is ignored
    CHECK(fb.pixel(0, 0) && fb.pixel(127, 63) && !fb.pixel(1, 0));
    CHECK(fb.page(0)[0] == 0x01 && fb.page(7)[127] == 0x80);
    CHECK(Framebuffer::textWidth("AB") == 11 && Framebuffer::textWidth("AB", 2) == 22);
    fb.clear();
    fb.text(0, 0, "I");
    CHECK(litPixels(fb, 0, 0, 6, 8) == 11); // 7 for the stem, 2 + 2 for the serifs

    // The bar follows the radio level while receiving, the microphone while
    // transmitting, and clamps at both ends.
    CHECK(barFill(fbIdle) == 0);
    CHECK(barFill(fbRx) == 75);  // AF -15 dBFS of a -60..0 range, 100 px wide
    CHECK(barFill(fbTx) == 90);  // mic -6 dBFS, not the radio's -40
    ModemStatus hot = idle;
    hot.radioInDbfs = 3;
    CHECK(barFill(render("hot", hot)) == 100);

    // The level offered to other indicators is the one the bar shows.
    CHECK(statusLevelFraction(idle) == 0.0f);
    CHECK(statusLevelFraction(rx) == 0.75f);        // AF -15 dBFS
    CHECK(statusLevelFraction(tx) == 0.9f);         // mic -6 dBFS while transmitting
    CHECK(statusLevelFraction(hot) == 1.0f);

    // Transmitting: inverted TX box in the header and an inverted band in
    // the body, both mostly lit.
    CHECK(litPixels(fbTx, 0, 0, 27, 16) > 27 * 16 / 2);
    CHECK(litPixels(fbRx, 0, 0, 27, 16) < 27 * 16 / 2);
    CHECK(litPixels(fbTx, 0, 17, 128, 23) > 128 * 23 / 2);
    CHECK(litPixels(fbRx, 0, 17, 128, 23) < 128 * 23 / 2);

    // The SNR is the largest thing on the screen. With sync it shows the
    // value ("12" lights more pixels than "--"), in analog mode it is absent.
    const int snrRx = litPixels(fbRx, 44, 18, 64, 21);
    const int snrIdle = litPixels(fbIdle, 44, 18, 64, 21);
    CHECK(snrIdle > 0 && snrRx > 2 * snrIdle);
    CHECK(litPixels(fbIdle, 112, 32, 16, 8) > 0);   // "dB"
    CHECK(litPixels(fbAnalog, 112, 32, 16, 8) == 0);

    // "SYNC" appears under the mode name only with sync.
    CHECK(litPixels(fbRx, 0, 29, 24, 8) > 0);
    CHECK(litPixels(fbIdle, 0, 29, 24, 8) == 0);

    // Callsign in the header: heard callsign in RX, own callsign in TX,
    // nothing before a callsign was heard.
    CHECK(litPixels(fbRx, 34, 0, 94, 16) > 0);
    CHECK(litPixels(fbTx, 34, 0, 94, 16) > 0);
    CHECK(litPixels(fbRx, 34, 0, 94, 16) != litPixels(fbTx, 34, 0, 94, 16));
    CHECK(litPixels(fbIdle, 34, 0, 94, 16) == 0);

    // Two-colour displays change colour between rows 15 and 16. Row 16 must
    // stay empty in every situation, so nothing straddles the change.
    ModemStatus ending = tx;
    ending.state = ModemState::TransmitEnding;
    ModemStatus analogTx = analog;
    analogTx.state = ModemState::Transmit;
    ModemStatus longCall = rx;
    longCall.rxCallsign = "VK5DGR/QRP12";
    longCall.snrDb = -12;
    for (const auto& fbCheck : {fbIdle, fbRx, fbTx, fbAnalog, render("5_tx_end", ending),
                                render("6_analog_tx", analogTx), render("7_long", longCall)})
        CHECK(litPixels(fbCheck, 0, 16, 128, 1) == 0);

    // A long callsign and a negative SNR must not run into their neighbours.
    auto fbLong = render("7_long", longCall);
    CHECK(litPixels(fbLong, 27, 0, 7, 16) == 0);  // gap between RX and the callsign
    CHECK(litPixels(fbLong, 36, 18, 5, 21) == 0); // gap between the mode name and the SNR

    std::printf(failures == 0 ? "PASS\n" : "FAILED\n");
    return failures == 0 ? 0 : 1;
}
