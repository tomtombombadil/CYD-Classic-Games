// Per-game save files on LittleFS: /games/<id>.bin, written via a temp file
// + rename so a power cut mid-write never leaves a half-written save. What
// goes in the file is the game's business (format tag + version first).
#pragma once

#include <cstddef>
#include <cstdint>

// Read the save into buf. Returns the byte count, 0 if there is none.
size_t save_store_load(const char* id, uint8_t* buf, size_t cap);
void   save_store_save(const char* id, const uint8_t* buf, size_t len);
