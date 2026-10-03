#include "stats_store.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <cctype>
#include <cstdio>
#include <cstring>
#include "hal/sdcard.h"
#include "hal/storage.h"

namespace {

constexpr const char* kSdDir      = "/CYD-Classic-Games";
constexpr const char* kFlashDir   = "/stats";
constexpr uint32_t    kFlashKeep  = 250;   // records kept on flash after a trim
constexpr uint32_t    kFlashLimit = 300;   // trim when the flash file passes this
constexpr size_t      kLineMax    = 128;

bool on_sd = false;

void sd_path(const char* id, char* out, size_t cap)    { snprintf(out, cap, "%s/%s.csv", kSdDir, id); }
void flash_path(const char* id, char* out, size_t cap) { snprintf(out, cap, "%s/%s.csv", kFlashDir, id); }

// A record line starts with its sequence number; anything else (header,
// blank, junk) isn't one. Returns the body after "seq," or nullptr.
const char* record_body(const char* line)
{
    if (!line || !isdigit(static_cast<unsigned char>(line[0]))) return nullptr;
    const char* p = line;
    while (isdigit(static_cast<unsigned char>(*p))) ++p;
    return *p == ',' ? p + 1 : nullptr;
}

// Calls fn(line) for every line of the file (without the newline).
template <class Fn>
bool for_each_line(fs::FS& fs, const char* path, Fn fn)
{
    File f = fs.open(path, "r");
    if (!f) return false;
    char chunk[256], line[kLineMax];
    size_t len = 0;
    for (;;) {
        const int n = f.read(reinterpret_cast<uint8_t*>(chunk), sizeof chunk);
        if (n <= 0) break;
        for (int k = 0; k < n; ++k) {
            const char c = chunk[k];
            if (c == '\n' || c == '\r') {
                if (len) { line[len] = 0; fn(line); len = 0; }
            } else if (len < sizeof line - 1) {
                line[len++] = c;
            }
        }
    }
    if (len) { line[len] = 0; fn(line); }
    f.close();
    return true;
}

uint32_t count_records(fs::FS& fs, const char* path)
{
    uint32_t n = 0;
    for_each_line(fs, path, [&](const char* l) { if (record_body(l)) ++n; });
    return n;
}

// The file's own header line (first line that isn't a record).
void read_header(fs::FS& fs, const char* path, char* out, size_t cap)
{
    out[0] = 0;
    for_each_line(fs, path, [&](const char* l) {
        if (!out[0] && !record_body(l)) snprintf(out, cap, "%s\n", l);
    });
}

bool append_line(fs::FS& fs, const char* path, const char* header, const char* text)
{
    const bool fresh = !fs.exists(path);
    File f = fs.open(path, "a");
    if (!f) return false;
    bool ok = true;
    if (fresh && header && *header) ok = f.print(header) > 0;
    ok = ok && f.print(text) > 0;
    f.close();
    return ok;
}

bool append_record(fs::FS& fs, const char* path, const char* header, uint32_t seq, const char* body)
{
    char line[kLineMax + 16];
    const int n = snprintf(line, sizeof line, "%lu,%s", (unsigned long)seq, body);
    if (n <= 0 || size_t(n) >= sizeof line) return false;
    if (line[n - 1] != '\n') { line[n] = '\n'; line[n + 1] = 0; }
    return append_line(fs, path, header, line);
}

// Move the game's records saved on flash onto the card, numbering them after
// the card's own records, then delete the flash copy.
void migrate_flash_to_sd(const char* id)
{
    char fpath[48], spath[48], header[kLineMax];
    flash_path(id, fpath, sizeof fpath);
    sd_path(id, spath, sizeof spath);
    if (!storage_begin() || !LittleFS.exists(fpath)) return;
    fs::FS& sd = sd_fs();
    read_header(LittleFS, fpath, header, sizeof header);
    uint32_t seq = count_records(sd, spath);
    bool ok = true;
    for_each_line(LittleFS, fpath, [&](const char* l) {
        const char* body = record_body(l);
        if (ok && body) ok = append_record(sd, spath, header, ++seq, body);
    });
    if (ok) {
        LittleFS.remove(fpath);
        Serial.printf("[stats] moved %s records from flash to SD card\n", id);
    }
}

// Picks the card if there is one, else flash. Returns the file system and
// the game's file path there.
fs::FS* target(const char* id, char* path, size_t cap)
{
    if (sd_begin()) {
        fs::FS& sd = sd_fs();
        if (!sd.exists(kSdDir)) sd.mkdir(kSdDir);
        migrate_flash_to_sd(id);
        on_sd = true;
        sd_path(id, path, cap);
        return &sd;
    }
    on_sd = false;
    if (!storage_mkdir(kFlashDir)) return nullptr;
    flash_path(id, path, cap);
    return &LittleFS;
}

// Replace `path` with `tmp`. Some file systems refuse to rename over an
// existing file, so fall back to remove + rename.
bool replace_file(fs::FS& fs, const char* tmp, const char* path)
{
    if (fs.rename(tmp, path)) return true;
    fs.remove(path);
    return fs.rename(tmp, path);
}

// Rewrite the file keeping records number `first`..`last` (1-based,
// inclusive), renumbered from 1. Empty range = header only.
bool rewrite_range(fs::FS& fs, const char* path, uint32_t first, uint32_t last)
{
    char tmp[56], header[kLineMax];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    read_header(fs, path, header, sizeof header);
    File out = fs.open(tmp, "w");
    if (!out) return false;
    if (header[0]) out.print(header);
    uint32_t seen = 0, kept = 0;
    for_each_line(fs, path, [&](const char* l) {
        const char* body = record_body(l);
        if (!body) return;
        ++seen;
        if (seen >= first && seen <= last) out.printf("%lu,%s\n", (unsigned long)++kept, body);
    });
    out.close();
    return replace_file(fs, tmp, path);
}

// Keep the flash file small: rewrite it with only the newest records.
void trim_flash(const char* path, uint32_t total)
{
    if (total <= kFlashLimit) return;
    rewrite_range(LittleFS, path, total - kFlashKeep + 1, total);
}

} // namespace

bool stats_store_append(const char* id, const char* header, const char* body)
{
    char path[48];
    fs::FS* fs = target(id, path, sizeof path);
    if (!fs) return false;
    const uint32_t seq = count_records(*fs, path) + 1;
    if (!append_record(*fs, path, header, seq, body)) {
        if (on_sd) {                       // card pulled? fall back to flash
            sd_lost();
            on_sd = false;
            char fpath[48];
            flash_path(id, fpath, sizeof fpath);
            if (storage_mkdir(kFlashDir))
                return append_record(LittleFS, fpath, header, count_records(LittleFS, fpath) + 1, body);
        }
        return false;
    }
    if (!on_sd) trim_flash(path, seq);
    Serial.printf("[stats] %s %lu,%s", id, (unsigned long)seq, body);
    return true;
}

bool stats_store_read(const char* id, void (*fn)(const char* line, void* ctx), void* ctx)
{
    char path[48];
    fs::FS* fs = target(id, path, sizeof path);
    if (!fs) return false;
    return for_each_line(*fs, path, [&](const char* l) { fn(l, ctx); });
}

bool stats_store_delete_last(const char* id)
{
    char path[48];
    fs::FS* fs = target(id, path, sizeof path);
    if (!fs) return false;
    const uint32_t n = count_records(*fs, path);
    if (n == 0) return false;
    return rewrite_range(*fs, path, 1, n - 1);
}

bool stats_store_clear(const char* id)
{
    bool ok = true;
    char path[48];
    if (sd_begin()) {
        fs::FS& sd = sd_fs();
        sd_path(id, path, sizeof path);
        if (sd.exists(path)) ok = sd.remove(path) && ok;
    }
    flash_path(id, path, sizeof path);
    if (storage_begin() && LittleFS.exists(path)) ok = LittleFS.remove(path) && ok;
    return ok;
}

const char* stats_store_location() { return on_sd ? "SD card" : "board memory"; }
