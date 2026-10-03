#include "panel_prefs.h"

#include <Arduino.h>
#include <LittleFS.h>
#include "storage.h"

namespace {

constexpr const char* kPath  = "/panel_prefs.bin";
// Bump when board color defaults change, so fixes saved against the old
// defaults are ignored instead of double-applied.
constexpr uint32_t    kMagic = 0x50504632;  // "PPF2"

struct PrefsFile {
    uint32_t magic;
    uint8_t  invert;
    uint8_t  swap_rb;
    uint8_t  reserved[2];
};

PanelPrefs current;
bool base_rgb_order = false;   // board file's rgb_order, captured once

void apply(LGFX& gfx)
{
    gfx.waitDMA();                 // never send commands mid screen transfer
    // Inversion: LovyanGFX XORs this with the board file's cfg.invert.
    gfx.invertDisplay(current.invert);

    // Color order lives in the panel config (MADCTL); re-sending the
    // rotation writes MADCTL again with the new BGR/RGB bit.
    auto* panel = gfx.getPanel();
    auto cfg = panel->config();
    cfg.rgb_order = base_rgb_order ^ current.swap_rb;
    panel->config(cfg);
    gfx.setRotation(gfx.getRotation());
}

void save()
{
    if (!storage_begin()) return;
    PrefsFile f{kMagic, current.invert, current.swap_rb, {0, 0}};
    File file = LittleFS.open(kPath, "w");
    if (!file) return;
    file.write(reinterpret_cast<const uint8_t*>(&f), sizeof(f));
    file.close();
}

} // namespace

void panel_prefs_begin(LGFX& gfx)
{
    base_rgb_order = gfx.getPanel()->config().rgb_order;

    if (storage_begin() && LittleFS.exists(kPath)) {
        File file = LittleFS.open(kPath, "r");
        PrefsFile f{};
        if (file && file.read(reinterpret_cast<uint8_t*>(&f), sizeof(f)) == sizeof(f)
            && f.magic == kMagic) {
            current.invert  = f.invert;
            current.swap_rb = f.swap_rb;
        }
        file.close();
    }
    if (current.invert || current.swap_rb) {
        Serial.printf("[panel_prefs] invert %d  swap R/B %d\n", current.invert, current.swap_rb);
        apply(gfx);
    }
}

const PanelPrefs& panel_prefs_get() { return current; }

void panel_prefs_flash(LGFX& gfx, bool on)
{
    gfx.waitDMA();
    gfx.invertDisplay(current.invert ^ on);
}

void panel_prefs_set(LGFX& gfx, const PanelPrefs& prefs)
{
    current = prefs;
    apply(gfx);
    save();
}
