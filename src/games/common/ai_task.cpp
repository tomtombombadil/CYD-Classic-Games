#include "ai_task.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "ui/shell.h"

namespace {

AiJob         job_fn = nullptr;
void*         job_ctx = nullptr;
volatile bool busy = false;
volatile bool stop_flag = false;
uint32_t      job_stack = 0;

void runner(void*)
{
    job_fn(job_ctx, &stop_flag);
    // How close the job came to its stack's end (ESP-IDF counts bytes). A
    // tight one goes in the device log, so a size can be raised in time.
    const uint32_t spare = uxTaskGetStackHighWaterMark(nullptr);
    if (spare < job_stack / 8) ui::log_event("AI stack tight: %lu of %lu bytes unused",
                                             (unsigned long)spare, (unsigned long)job_stack);
    else ui::log_step("ai done, %lu stack bytes unused", (unsigned long)spare);
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
    job_stack = stack_bytes;
    busy = true;
    ui::log_step("ai start, %lu byte stack, %lu heap", (unsigned long)stack_bytes,
                 (unsigned long)ESP.getFreeHeap());
    // The stack is allocated for this job only and freed when it ends.
    // Idle priority: FreeRTOS then time-slices the job with core 0's idle
    // task, which must run to feed the task watchdog - a long search at a
    // higher priority starved it and the watchdog rebooted the board (Tom's
    // 2.8" during Solitaire's "Shuffling...").
    if (xTaskCreatePinnedToCore(runner, "ai", stack_bytes, nullptr, tskIDLE_PRIORITY, nullptr, 0) != pdPASS) {
        busy = false;
        ui::log_event("AI task not started: %lu bytes of stack wanted, largest block %lu",
                      (unsigned long)stack_bytes, (unsigned long)ESP.getMaxAllocHeap());
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
