// Device log: a small text log in the board's flash (/log.txt, the newest
// ~16 KB kept across /log.txt and /log.old), echoed to the serial port.
//
// It records each boot (firmware, board, why the board last reset, free
// memory) and, after a crash, what the crash handler saved: the reason, the
// code address, a short backtrace (decode with the build's firmware.elf, see
// README) and the last "step" the firmware noted before it.
//
// Settings -> Diagnostics -> Device Log shows it; the PlatformIO Serial
// Monitor (115200 baud) shows the same lines live.
#pragma once

#include <cstdint>

// After LittleFS is up: installs the crash handler and logs the boot.
void device_log_begin(const char* firmware, const char* board);
// From loop(): writes lines other tasks queued.
void device_log_loop();

// One line. to_file = false only prints it and makes it the last step
// (a crash report names it) - for frequent events. Any task may call it.
void device_log(const char* text, bool to_file);

// Whole log, oldest line first.
bool device_log_read(void (*line)(const char* text, void* ctx), void* ctx);
void device_log_clear();
// Copies it to the SD card as /CYD-Classic-Games/log.txt. false = no card.
bool device_log_copy_sd();

void device_memory(uint32_t* free_bytes, uint32_t* largest_block);
