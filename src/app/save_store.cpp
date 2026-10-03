#include "save_store.h"

#include <Arduino.h>
#include <LittleFS.h>
#include "hal/storage.h"

namespace {
constexpr const char* kDir = "/games";

void paths(const char* id, char* path, char* tmp, size_t cap)
{
    snprintf(path, cap, "%s/%s.bin", kDir, id);
    snprintf(tmp, cap, "%s/%s.tmp", kDir, id);
}
} // namespace

size_t save_store_load(const char* id, uint8_t* buf, size_t cap)
{
    char path[48], tmp[48];
    paths(id, path, tmp, sizeof path);
    if (!storage_begin() || !LittleFS.exists(path)) return 0;
    File f = LittleFS.open(path, "r");
    if (!f) return 0;
    const size_t n = f.read(buf, cap);
    f.close();
    Serial.printf("[save] %s: %u bytes\n", path, (unsigned)n);
    return n;
}

void save_store_save(const char* id, const uint8_t* buf, size_t len)
{
    char path[48], tmp[48];
    paths(id, path, tmp, sizeof path);
    if (!len || !storage_mkdir(kDir)) return;
    // Write a temp file, then rename over the old save, so a power cut
    // mid-write never leaves a half-written game.
    File f = LittleFS.open(tmp, "w");
    if (!f) return;
    const size_t w = f.write(buf, len);
    f.close();
    if (w != len) { LittleFS.remove(tmp); return; }
    // LittleFS renames over an existing file atomically; if this build's
    // VFS refuses, fall back to remove + rename.
    if (!LittleFS.rename(tmp, path)) {
        LittleFS.remove(path);
        LittleFS.rename(tmp, path);
    }
}
