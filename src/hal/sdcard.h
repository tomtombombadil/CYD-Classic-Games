// microSD card, on boards where the slot is usable (BOARD_SD_USABLE).
#pragma once

#include <FS.h>

// Mounts the card if one is inserted. Cheap once mounted; if no card was
// found it tries again, so a card inserted later is picked up the next time
// something is saved. Returns false on boards without a usable slot.
bool sd_begin();

// The mounted card. Only valid after sd_begin() returned true.
fs::FS& sd_fs();

// Call after a failed read/write: unmounts so the next sd_begin() retries.
void sd_lost();
