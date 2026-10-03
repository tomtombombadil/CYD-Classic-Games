#include "legacy_import.h"

#include <Arduino.h>
#include <LittleFS.h>
#include "hal/sdcard.h"
#include "hal/storage.h"

namespace {

void move_flash(const char* from, const char* dir, const char* to)
{
    if (!LittleFS.exists(from)) return;
    if (LittleFS.exists(to) || !storage_mkdir(dir)) return;   // never overwrite
    if (LittleFS.rename(from, to)) Serial.printf("[import] %s -> %s\n", from, to);
}

// The card's history stays where CYD-Sudoku keeps it; this firmware gets a
// copy (once, if it has none yet).
void copy_sd(const char* from, const char* dir, const char* to)
{
    fs::FS& sd = sd_fs();
    if (!sd.exists(from) || sd.exists(to)) return;
    if (!sd.exists(dir) && !sd.mkdir(dir)) return;
    File in = sd.open(from, "r");
    File out = sd.open(to, "w");
    bool ok = in && out;
    uint8_t buf[256];
    while (ok) {
        const int n = in.read(buf, sizeof buf);
        if (n <= 0) break;
        ok = out.write(buf, n) == size_t(n);
    }
    if (in) in.close();
    if (out) out.close();
    if (ok) Serial.printf("[import] copied %s -> %s\n", from, to);
    else    sd.remove(to);
}

} // namespace

void legacy_import()
{
    if (!storage_begin()) return;
    move_flash("/game.bin",         "/games", "/games/sudoku.bin");
    move_flash("/puzzle_stock.bin", "/games", "/games/sudoku_stock.bin");
    move_flash("/stats.csv",        "/stats", "/stats/sudoku.csv");
    if (sd_begin()) copy_sd("/CYD-Sudoku/stats.csv", "/CYD-Classic-Games", "/CYD-Classic-Games/sudoku.csv");
}
