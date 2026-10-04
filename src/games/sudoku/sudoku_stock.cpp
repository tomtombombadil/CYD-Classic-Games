#include "sudoku_stock.h"

#include <new>

#include <Arduino.h>
#include <LittleFS.h>
#include <esp_random.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include "hal/storage.h"

namespace {

constexpr int         kPerLevel = 2;
constexpr const char* kPath     = "/games/sudoku_stock.bin";
constexpr const char* kTmp      = "/games/sudoku_stock.tmp";
constexpr uint32_t    kMagic    = 0x314B5453;  // "STK1"

struct Slot { sudoku::Grid puzzle, solution; };

struct StockFile {
    uint32_t magic;
    uint8_t  count[4];
    Slot     slot[4][kPerLevel];
};

StockFile         stock{};            // guarded by `lock` (~1.3 KB)
SemaphoreHandle_t lock = nullptr;
TaskHandle_t      worker = nullptr;
volatile bool     dirty = false;
volatile bool     stop_req = false;   // ask the worker to end itself
volatile bool     running = false;    // worker task exists
bool              loaded = false;

bool valid_slot(const Slot& s)
{
    // Cheap sanity check of a loaded puzzle: clues agree with the solution
    // and the solution is a full grid.
    for (int i = 0; i < 81; ++i) {
        if (s.solution.c[i] < 1 || s.solution.c[i] > 9) return false;
        if (s.puzzle.c[i] && s.puzzle.c[i] != s.solution.c[i]) return false;
    }
    return true;
}

void load()
{
    if (!storage_begin() || !LittleFS.exists(kPath)) return;
    File f = LittleFS.open(kPath, "r");
    StockFile* tmp = new (std::nothrow) StockFile;     // heap: not kept as static RAM
    const bool ok = tmp && f && f.read(reinterpret_cast<uint8_t*>(tmp), sizeof *tmp) == sizeof *tmp
                 && tmp->magic == kMagic;
    f.close();
    if (ok)
        for (int d = 0; d < 4; ++d) {
            stock.count[d] = 0;
            for (int k = 0; k < tmp->count[d] && k < kPerLevel; ++k)
                if (valid_slot(tmp->slot[d][k])) stock.slot[d][stock.count[d]++] = tmp->slot[d][k];
        }
    delete tmp;
}

void save()
{
    if (!storage_begin() || !storage_mkdir("/games")) return;
    StockFile* copy_p = new (std::nothrow) StockFile;  // heap: not kept as static RAM
    if (!copy_p) return;
    StockFile& copy = *copy_p;
    xSemaphoreTake(lock, portMAX_DELAY);
    copy = stock;
    dirty = false;
    xSemaphoreGive(lock);
    File f = LittleFS.open(kTmp, "w");
    if (!f) { delete copy_p; return; }
    const size_t w = f.write(reinterpret_cast<const uint8_t*>(&copy), sizeof copy);
    f.close();
    delete copy_p;
    if (w != sizeof copy) { LittleFS.remove(kTmp); return; }
    if (!LittleFS.rename(kTmp, kPath)) { LittleFS.remove(kPath); LittleFS.rename(kTmp, kPath); }
}

// The worker ends itself when asked: between generation attempts (here) or
// while it sleeps with a full stock. It never holds `lock` at these points.
void end_if_asked()
{
    if (!stop_req) return;
    running = false;
    vTaskDelete(nullptr);
}

void yield_cb()
{
    end_if_asked();
    vTaskDelay(1);
}

// Background task: fill the emptiest level first, sleep when all are full.
void worker_task(void*)
{
    sudoku::Rng rng(esp_random());
    for (;;) {
        end_if_asked();
        int need = -1, fewest = kPerLevel;
        xSemaphoreTake(lock, portMAX_DELAY);
        for (int d = 0; d < 4; ++d)
            if (stock.count[d] < fewest) { fewest = stock.count[d]; need = d; }
        xSemaphoreGive(lock);
        if (need < 0) { ulTaskNotifyTake(pdTRUE, portMAX_DELAY); continue; }

        Slot made;                                    // on the worker's 12 KB stack
        const uint32_t t0 = millis();
        sudoku::generate(static_cast<sudoku::Difficulty>(need), rng, made.puzzle, made.solution, yield_cb);
        xSemaphoreTake(lock, portMAX_DELAY);
        if (stock.count[need] < kPerLevel) stock.slot[need][stock.count[need]++] = made;
        dirty = true;
        xSemaphoreGive(lock);
        Serial.printf("[stock] %s puzzle ready (%lu ms)\n",
                      sudoku::difficulty_name(static_cast<sudoku::Difficulty>(need)),
                      (unsigned long)(millis() - t0));
    }
}

} // namespace

void sudoku_stock_begin()
{
    if (!lock) lock = xSemaphoreCreateMutex();
    if (!loaded) {
        loaded = true;
        stock.magic = kMagic;
        load();
    }
    // A worker told to stop a moment ago may still be finishing its current
    // attempt; let it end before starting a fresh one.
    while (running) vTaskDelay(pdMS_TO_TICKS(10));
    stop_req = false;
    running = true;
    // Core 0 (the Arduino loop and UI run on core 1); lowest priority so it
    // only uses spare time.
    if (xTaskCreatePinnedToCore(worker_task, "puzzles", 12288, nullptr, tskIDLE_PRIORITY, &worker, 0) != pdPASS) {
        running = false;
        worker = nullptr;
    }
}

void sudoku_stock_end()
{
    if (running) {
        stop_req = true;
        if (worker) xTaskNotifyGive(worker);   // wake it if it sleeps
    }
    worker = nullptr;
    if (dirty) save();
}

bool sudoku_stock_take(sudoku::Difficulty d, sudoku::Grid& puzzle, sudoku::Grid& solution)
{
    if (!lock) return false;
    const int i = static_cast<int>(d);
    bool got = false;
    xSemaphoreTake(lock, portMAX_DELAY);
    if (stock.count[i] > 0) {
        const Slot& s = stock.slot[i][--stock.count[i]];
        puzzle = s.puzzle;
        solution = s.solution;
        got = true;
        dirty = true;
    }
    xSemaphoreGive(lock);
    if (worker) xTaskNotifyGive(worker);
    return got;
}

void sudoku_stock_loop()
{
    // Save at most every 5 s, so a burst of refills is one flash write.
    static uint32_t last = 0;
    if (dirty && millis() - last > 5000) {
        last = millis();
        save();
    }
}
