#include "splash.h"

#include <Arduino.h>

#if BOARD_PORTRAIT_W == 320
#include "assets/splash_320x480.h"
#else
#include "assets/splash_240x320.h"
#endif

namespace {

bool touched(LGFX& gfx)
{
    gfx.waitDMA();                       // touch may share the display's bus
    lgfx::touch_point_t tp;
    return gfx.getTouch(&tp, 1) > 0;
}

// Wait until `want` (touching or not) has held for `n` readings 20 ms apart.
// Resistive panels flicker at the edges of a press, so one reading isn't
// enough either way.
void (*idle_fn)() = nullptr;

void wait_for(LGFX& gfx, bool want, int n)
{
    int run = 0;
    while (run < n) {
        run = (touched(gfx) == want) ? run + 1 : 0;
        if (idle_fn) idle_fn();
        delay(20);
    }
}

} // namespace

int splash_count() { return kSplashCount; }

void splash_show(LGFX& gfx, int index, void (*idle)())
{
    idle_fn = idle;
    const SplashImage& img = kSplashImages[((index % kSplashCount) + kSplashCount) % kSplashCount];
    gfx.fillScreen(TFT_BLACK);
    const uint32_t t0 = millis();
    gfx.drawJpg(img.data, img.len, 0, 0, gfx.width(), gfx.height());
    Serial.printf("[splash] image %d drawn in %lu ms\n", index % kSplashCount,
                  (unsigned long)(millis() - t0));
    wait_for(gfx, false, 5);             // a finger/stylus still down from calibration
    wait_for(gfx, true, 2);              // the tap
    wait_for(gfx, false, 3);             // ...and its release
    idle_fn = nullptr;
}
