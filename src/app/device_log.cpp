#include "device_log.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <esp_attr.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstdio>
#include <cstring>
#include "hal/sdcard.h"
#include "hal/storage.h"

namespace {

constexpr const char* kLog    = "/log.txt";
constexpr const char* kOld    = "/log.old";
constexpr size_t      kMax    = 8 * 1024;     // per file; two files kept
constexpr const char* kSdDir  = "/CYD-Classic-Games";
constexpr const char* kSdFile = "/CYD-Classic-Games/log.txt";
constexpr uint32_t    kMagic  = 0x43594431;   // "CYD1"
constexpr int         kText   = 48;
constexpr int         kTrace  = 8;

// Kept in RTC memory, which survives a crash and the restart after it (not
// a power cut). The crash handler may only write memory, so it fills this
// and the next boot logs it.
struct Crash {
    uint32_t magic;
    int32_t  core;
    uint32_t pc;
    uint32_t n;
    uint32_t trace[kTrace];
    char     reason[kText];
    char     task[16];
    char     step[kText];                     // the last step before the crash
};
RTC_NOINIT_ATTR Crash crash;
RTC_NOINIT_ATTR char  last_step[kText];
RTC_NOINIT_ATTR uint32_t step_magic;

TaskHandle_t main_task = nullptr;
bool ready = false;

// Lines from other tasks wait here for the main loop (file writes and
// their stack stay on the UI task).
constexpr int kPending = 4;
char pending[kPending][kText + 16];
bool pending_file[kPending];
int  pending_n = 0;
portMUX_TYPE pending_mux = portMUX_INITIALIZER_UNLOCKED;

void copy(char* dst, const char* src, int cap)
{
    int k = 0;
    if (src)
        for (; k < cap - 1 && src[k]; ++k) dst[k] = src[k];
    dst[k] = 0;
}

void on_panic(arduino_panic_info_t* info, void*)
{
    crash.core = info->core;
    crash.pc = reinterpret_cast<uint32_t>(info->pc);
    crash.n = info->backtrace_len < kTrace ? info->backtrace_len : kTrace;
    for (uint32_t k = 0; k < crash.n; ++k) crash.trace[k] = info->backtrace[k];
    copy(crash.reason, info->reason, kText);
    copy(crash.task, pcTaskGetName(nullptr), sizeof crash.task);
    if (step_magic == kMagic) copy(crash.step, last_step, kText);
    else crash.step[0] = 0;
    crash.magic = kMagic;
}

// "h:mm:ss" since boot
void stamp(char* out, size_t cap)
{
    const uint32_t s = millis() / 1000;
    snprintf(out, cap, "%lu:%02lu:%02lu", (unsigned long)(s / 3600),
             (unsigned long)(s / 60 % 60), (unsigned long)(s % 60));
}

void write_line(const char* text, bool to_file)
{
    char t[16];
    stamp(t, sizeof t);
    Serial.printf("[log %s] %s\n", t, text);
    if (!to_file || !ready) return;
    File f = LittleFS.open(kLog, "a");
    if (!f) return;
    f.printf("%s %s\n", t, text);
    const size_t size = f.size();
    f.close();
    if (size > kMax) {
        LittleFS.remove(kOld);
        LittleFS.rename(kLog, kOld);
    }
}

const char* reset_text(esp_reset_reason_t r)
{
    switch (r) {
        case ESP_RST_POWERON:   return "power on";
        case ESP_RST_EXT:       return "reset pin";
        case ESP_RST_SW:        return "restart by the firmware";
        case ESP_RST_PANIC:     return "CRASH";
        case ESP_RST_INT_WDT:   return "CRASH (interrupt watchdog)";
        case ESP_RST_TASK_WDT:  return "CRASH (task watchdog)";
        case ESP_RST_WDT:       return "CRASH (watchdog)";
        case ESP_RST_DEEPSLEEP: return "wake from sleep";
        case ESP_RST_BROWNOUT:  return "POWER DIP (brownout)";
        case ESP_RST_SDIO:      return "SDIO";
        default:                return "unknown";
    }
}

bool for_each_line(fs::FS& fs, const char* path, void (*fn)(const char*, void*), void* ctx)
{
    File f = fs.open(path, "r");
    if (!f) return false;
    char chunk[128], line[160];
    size_t len = 0;
    for (;;) {
        const int n = f.read(reinterpret_cast<uint8_t*>(chunk), sizeof chunk);
        if (n <= 0) break;
        for (int k = 0; k < n; ++k) {
            if (chunk[k] == '\n') { line[len] = 0; fn(line, ctx); len = 0; }
            else if (len < sizeof line - 1) line[len++] = chunk[k];
        }
    }
    if (len) { line[len] = 0; fn(line, ctx); }
    f.close();
    return true;
}

} // namespace

void device_memory(uint32_t* free_bytes, uint32_t* largest_block)
{
    *free_bytes = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    *largest_block = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
}

void device_log_begin(const char* firmware, const char* board)
{
    main_task = xTaskGetCurrentTaskHandle();
    ready = storage_begin();

    char line[120];
    snprintf(line, sizeof line, "=== Boot: firmware %s, %s", firmware, board);
    write_line(line, true);
    const esp_reset_reason_t why = esp_reset_reason();
    snprintf(line, sizeof line, "Last reset: %s", reset_text(why));
    write_line(line, true);

    const bool crashed = why == ESP_RST_PANIC || why == ESP_RST_INT_WDT ||
                         why == ESP_RST_TASK_WDT || why == ESP_RST_WDT;
    if (crashed && crash.magic == kMagic) {
        crash.reason[kText - 1] = crash.task[sizeof crash.task - 1] = crash.step[kText - 1] = 0;
        snprintf(line, sizeof line, "Crash: %s", crash.reason);
        write_line(line, true);
        snprintf(line, sizeof line, "Task %s on core %ld at 0x%08lx", crash.task,
                 (long)crash.core, (unsigned long)crash.pc);
        write_line(line, true);
        // Three addresses a line, so each fits the screen
        const uint32_t n = crash.n <= kTrace ? crash.n : kTrace;
        for (uint32_t k = 0; k < n; k += 3) {
            int len = snprintf(line, sizeof line, k ? "  " : "Backtrace:");
            for (uint32_t j = k; j < n && j < k + 3; ++j)
                len += snprintf(line + len, sizeof line - len, " %08lx", (unsigned long)crash.trace[j]);
            write_line(line, true);
        }
        if (crash.step[0]) {
            snprintf(line, sizeof line, "Last step: %s", crash.step);
            write_line(line, true);
        }
    }
    crash.magic = 0;
    step_magic = kMagic;
    last_step[0] = 0;

    uint32_t fr, big;
    device_memory(&fr, &big);
    snprintf(line, sizeof line, "Memory: %lu KB free, largest block %lu KB",
             (unsigned long)(fr / 1024), (unsigned long)(big / 1024));
    write_line(line, true);

    set_arduino_panic_handler(on_panic, nullptr);
}

void device_log(const char* text, bool to_file)
{
    copy(last_step, text, kText);
    if (xTaskGetCurrentTaskHandle() == main_task) { write_line(text, to_file); return; }
    portENTER_CRITICAL(&pending_mux);
    if (pending_n < kPending) {
        copy(pending[pending_n], text, sizeof pending[0]);
        pending_file[pending_n++] = to_file;
    }
    portEXIT_CRITICAL(&pending_mux);
}

void device_log_loop()
{
    while (pending_n) {
        char text[sizeof pending[0]];
        bool to_file;
        portENTER_CRITICAL(&pending_mux);
        memcpy(text, pending[0], sizeof text);
        to_file = pending_file[0];
        for (int k = 1; k < pending_n; ++k) {
            memcpy(pending[k - 1], pending[k], sizeof pending[0]);
            pending_file[k - 1] = pending_file[k];
        }
        --pending_n;
        portEXIT_CRITICAL(&pending_mux);
        write_line(text, to_file);
    }
}

bool device_log_read(void (*line)(const char* text, void* ctx), void* ctx)
{
    if (!ready) return false;
    const bool a = for_each_line(LittleFS, kOld, line, ctx);
    const bool b = for_each_line(LittleFS, kLog, line, ctx);
    return a || b;
}

void device_log_clear()
{
    if (!ready) return;
    LittleFS.remove(kOld);
    LittleFS.remove(kLog);
}

bool device_log_copy_sd()
{
    if (!ready || !sd_begin()) return false;
    fs::FS& sd = sd_fs();
    if (!sd.exists(kSdDir)) sd.mkdir(kSdDir);
    File out = sd.open(kSdFile, "w");
    if (!out) { sd_lost(); return false; }
    struct Ctx { File* f; bool ok; } c{&out, true};
    device_log_read([](const char* text, void* p) {
        Ctx* cx = static_cast<Ctx*>(p);
        if (cx->f->printf("%s\n", text) == 0) cx->ok = false;
    }, &c);
    out.close();
    if (!c.ok) sd_lost();
    return c.ok;
}
