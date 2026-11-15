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
    return litPixels(fb, 26, 40, 100, 1);
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

    // Transmitting shows an inverted band across the main area: mostly lit.
    CHECK(litPixels(fbTx, 0, 9, 128, 23) > 128 * 23 / 2);
    CHECK(litPixels(fbRx, 0, 9, 128, 23) < 128 * 23 / 2);

    // The SNR is the largest thing on the screen. With sync it shows the
    // value ("12" lights more pixels than "--"), in analog mode it is absent.
    const int snrRx = litPixels(fbRx, 40, 10, 64, 21);
    const int snrIdle = litPixels(fbIdle, 40, 10, 64, 21);
    CHECK(snrIdle > 0 && snrRx > 2 * snrIdle);
    CHECK(litPixels(fbAnalog, 0, 17, 18, 7) == 0); // no "SNR" label

    // "SYNC" appears at the top right only with sync.
    CHECK(litPixels(fbRx, 104, 0, 24, 8) > 0);
    CHECK(litPixels(fbIdle, 104, 0, 24, 8) == 0);

    // Callsign area: heard callsign in RX, own callsign in TX, differ.
    CHECK(litPixels(fbRx, 0, 50, 128, 14) > 0);
    CHECK(litPixels(fbTx, 0, 50, 128, 14) > 0);
    CHECK(litPixels(fbRx, 0, 50, 128, 14) != litPixels(fbTx, 0, 50, 128, 14));

    std::printf(failures == 0 ? "PASS\n" : "FAILED\n");
    return failures == 0 ? 0 : 1;
}
