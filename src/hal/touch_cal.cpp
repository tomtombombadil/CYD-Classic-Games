#include "touch_cal.h"

#include <Arduino.h>
#include <LittleFS.h>
#include "storage.h"

namespace {

constexpr const char* kCalPath = "/touch_cal.bin";
constexpr uint32_t    kMagic   = 0x43414C31;  // "CAL1"

struct CalFile {
    uint32_t magic;
    uint32_t board_hash;   // calibration from a different board file is ignored
    uint16_t params[8];    // LovyanGFX calibrateTouch() output
};

// FNV-1a of the board name, so reflashing a different board build invalidates
// an old calibration instead of silently mis-mapping touches.
uint32_t board_hash()
{
    uint32_t h = 2166136261u;
    for (const char* p = BOARD_NAME; *p; ++p) {
        h ^= static_cast<uint8_t>(*p);
        h *= 16777619u;
    }
    return h;
}

bool load(uint16_t out[8])
{
    if (!storage_begin() || !LittleFS.exists(kCalPath)) return false;
    File f = LittleFS.open(kCalPath, "r");
    if (!f) return false;
    CalFile c{};
    const bool ok = f.read(reinterpret_cast<uint8_t*>(&c), sizeof(c)) == sizeof(c)
                 && c.magic == kMagic && c.board_hash == board_hash();
    f.close();
    if (ok) memcpy(out, c.params, sizeof(c.params));
    return ok;
}

void save(const uint16_t params[8])
{
    if (!storage_begin()) return;
    CalFile c{kMagic, board_hash(), {}};
    memcpy(c.params, params, sizeof(c.params));
    File f = LittleFS.open(kCalPath, "w");
    if (!f) { Serial.println("[touch_cal] could not write calibration"); return; }
    f.write(reinterpret_cast<const uint8_t*>(&c), sizeof(c));
    f.close();
}

bool boot_button_held()
{
#ifdef BOARD_PIN_BOOT_BTN
    pinMode(BOARD_PIN_BOOT_BTN, INPUT_PULLUP);
    delay(5);
    return digitalRead(BOARD_PIN_BOOT_BTN) == LOW;
#else
    return false;
#endif
}

} // namespace

void touch_cal_run(LGFX& gfx)
{
    uint16_t params[8];
    gfx.fillScreen(TFT_BLACK);
    gfx.setTextColor(TFT_WHITE, TFT_BLACK);
    gfx.setTextDatum(textdatum_t::middle_center);
    gfx.setFont(&fonts::Font2);
    gfx.drawString("Touch calibration", gfx.width() / 2, gfx.height() / 2 - 12);
    gfx.drawString("Tap each arrow tip precisely", gfx.width() / 2, gfx.height() / 2 + 12);
    gfx.calibrateTouch(params, TFT_WHITE, TFT_BLACK, 15);
    save(params);
    gfx.fillScreen(TFT_BLACK);
    Serial.println("[touch_cal] new calibration saved");
}

void touch_cal_begin(LGFX& gfx)
{
#if BOARD_TOUCH_RESISTIVE
    uint16_t params[8];
    if (!boot_button_held() && load(params)) {
        gfx.setTouchCalibrate(params);
        Serial.println("[touch_cal] loaded saved calibration");
        return;
    }
    touch_cal_run(gfx);
#else
    (void)gfx;   // capacitive touch is factory-calibrated
#endif
}
