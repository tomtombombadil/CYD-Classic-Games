// A small on-screen keyboard for names (RPG Dice presets, the wireless
// player name): an overlay with the text, letters A-Z (each word starts
// with a capital by itself), a digits/symbols page, Space, backspace and
// Done. Big keys, taps only.
#pragma once

#include <cstddef>

namespace ui {

// Opens the keyboard over the screen with `initial` as the text (at most
// `max_len` characters). Done calls `done` with the text, spaces trimmed
// at both ends (it may be empty); `done` decides what to open next.
void keyboard_open(const char* title, const char* initial, size_t max_len, void (*done)(const char* text));

} // namespace ui
