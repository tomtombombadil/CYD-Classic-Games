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

bool ai_start(AiJob job, void* ctx, uint32_t stack_bytes)
{
    if (busy) return false;
    job_fn = job;
    job_ctx = ctx;
    stop_flag = false;
    busy = true;
    // The stack is allocated for this job only and freed when it ends
    if (xTaskCreatePinnedToCore(runner, "ai", stack_bytes, nullptr, 1, nullptr, 0) != pdPASS) {
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
