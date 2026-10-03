// Runs a computer opponent's search off the UI: on the board a FreeRTOS
// task on core 0 (the UI runs on core 1) at idle priority, so core 0's idle
// task still runs and the task watchdog stays fed however long the search
// takes; in the PC preview straight away.
// One job at a time. The job gets a stop flag it should check now and then;
// ai_stop() sets it and waits for the job to end (used when the player
// leaves the game).
#pragma once

#include <cstdint>

using AiJob = void (*)(void* ctx, volatile bool* stop);

// false if a job is still running. stack_bytes: the search's needs.
bool ai_start(AiJob job, void* ctx, uint32_t stack_bytes = 8192);
bool ai_busy();
void ai_stop();                        // ask the job to stop, wait until it has
