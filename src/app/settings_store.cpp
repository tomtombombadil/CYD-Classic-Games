#include "settings_store.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <cstddef>
#include <cstring>
#include "hal/storage.h"

namespace {
constexpr const char* kPath   = "/ui_settings.bin";
constexpr uint32_t    kMagic1 = 0x31534955;  // "UIS1" (CYD-Sudoku v1.0.0)
constexpr uint32_t    kMagic2 = 0x32534955;  // "UIS2" adds the last game

struct SettingsFile {
    uint32_t magic;
    uint8_t  theme;
    uint8_t  input;
    uint8_t  brightness;    // 0 = not set (files from before the slider)
    uint8_t  splash;        // UIS2: next splash image (was reserved)
    uint8_t  volume;        // UIS2: volume + 1 (0 = not set: 50 %). Was sound
                            // off, 1 = silent, which still reads as volume 0
    uint8_t  card_back;     // UIS2: card back + 1 (0 = not set: the default)
    uint8_t  flags;         // UIS2: bit 0 = screen turned 180 degrees (was reserved, 0)
    uint8_t  reserved[1];   // room for later settings without a format change
    char     last_game[16]; // UIS2: registry id of the last game opened
};
constexpr size_t kSize1 = offsetof(SettingsFile, last_game);   // UIS1 file size
} // namespace

ui::UiSettings settings_store_load()
{
    ui::UiSettings s;
    if (!storage_begin() || !LittleFS.exists(kPath)) return s;
    File f = LittleFS.open(kPath, "r");
    SettingsFile d{};
    const size_t n = f ? f.read(reinterpret_cast<uint8_t*>(&d), sizeof d) : 0;
    f.close();
    const bool v1 = n >= kSize1 && d.magic == kMagic1;
    const bool v2 = n == sizeof d && d.magic == kMagic2;
    if (!v1 && !v2) return s;
    if (d.theme < ui::kThemeCount) s.theme = static_cast<ui::Theme>(d.theme);
    if (d.input <= 1) s.input = static_cast<ui::InputMode>(d.input);
    if (d.brightness >= ui::kMinBrightness) s.brightness = d.brightness;
    if (v2) {
        s.splash_next = d.splash;
        if (d.volume) s.volume = d.volume > 101 ? 100 : d.volume - 1;
        if (d.card_back) s.card_back = d.card_back - 1;
        s.flip = d.flags & 1;
        d.last_game[sizeof d.last_game - 1] = 0;
        memcpy(s.last_game, d.last_game, sizeof s.last_game);
    } else {
        // Settings left by CYD-Sudoku: its game becomes the one to continue
        strcpy(s.last_game, "sudoku");
    }
    return s;
}

void settings_store_save(const ui::UiSettings& s)
{
    if (!storage_begin()) return;
    SettingsFile d{};
    d.magic = kMagic2;
    d.theme = static_cast<uint8_t>(s.theme);
    d.input = static_cast<uint8_t>(s.input);
    d.brightness = s.brightness;
    d.splash = s.splash_next;
    d.volume = (s.volume > 100 ? 100 : s.volume) + 1;
    d.card_back = static_cast<uint8_t>(s.card_back + 1);
    d.flags = s.flip ? 1 : 0;
    memcpy(d.last_game, s.last_game, sizeof d.last_game);
    d.last_game[sizeof d.last_game - 1] = 0;
    File f = LittleFS.open(kPath, "w");
    if (!f) return;
    f.write(reinterpret_cast<const uint8_t*>(&d), sizeof d);
    f.close();
}
