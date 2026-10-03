#include "ai_task.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace {

AiJob         job_fn = nullptr;
void*         job_ctx = nullptr;
volatile bool busy = false;
volatile bool stop_flag = false;

void runner(void*)
{
    job_fn(job_ctx, &stop_flag);
    busy = false;
    vTaskDelete(nullptr);
}

} // namespace

bool ai_start(AiJob job, void* ctx)
{
    if (busy) return false;
    job_fn = job;
    job_ctx = ctx;
    stop_flag = false;
    busy = true;
    // 8 KB stack: searches recurse a few dozen levels with small frames
    if (xTaskCreatePinnedToCore(runner, "ai", 8192, nullptr, 1, nullptr, 0) != pdPASS) {
        busy = false;
        return false;
    }
    return true;
}

bool ai_busy() { return busy; }

void ai_stop()
{
    if (!busy) return;
    stop_flag = true;
    while (busy) vTaskDelay(pdMS_TO_TICKS(5));
}
