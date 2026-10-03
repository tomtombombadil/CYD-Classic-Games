#include "themes_store.h"

#include <Arduino.h>
#include <LittleFS.h>
#include "hal/storage.h"

namespace {
constexpr const char* kPath  = "/themes.bin";
constexpr const char* kTmp   = "/themes.tmp";
constexpr uint32_t    kMagic = 0x31485454;  // "TTH1"

struct File1 {
    uint32_t         magic;
    ui::CustomThemes themes;
};
} // namespace

ui::CustomThemes themes_store_load()
{
    ui::CustomThemes t;
    if (!storage_begin() || !LittleFS.exists(kPath)) return t;
    File f = LittleFS.open(kPath, "r");
    static File1 d;
    const bool ok = f && f.read(reinterpret_cast<uint8_t*>(&d), sizeof d) == sizeof d && d.magic == kMagic;
    f.close();
    if (ok) t = d.themes;
    return t;
}

void themes_store_save(const ui::CustomThemes& t)
{
    if (!storage_begin()) return;
    static File1 d;
    d.magic = kMagic;
    d.themes = t;
    File f = LittleFS.open(kTmp, "w");
    if (!f) return;
    const size_t w = f.write(reinterpret_cast<const uint8_t*>(&d), sizeof d);
    f.close();
    if (w != sizeof d) { LittleFS.remove(kTmp); return; }
    if (!LittleFS.rename(kTmp, kPath)) { LittleFS.remove(kPath); LittleFS.rename(kTmp, kPath); }
}
