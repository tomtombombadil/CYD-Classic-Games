// How To Play text for a game: a few short pages, each with a heading and a
// body. Each game defines `extern const games::Help <id>_help` in
// src/games/<id>/<id>_help.cpp (plain data, no LVGL), and the registry
// hooks it up from games.def. kit::how_to_play() shows it, one page per
// screen with < > keys (no scrolling: every page must fit a 240x320 screen;
// the PC preview renders every page at both sizes and reports overflow).
#pragma once

#include <cstdint>

namespace games {

struct HelpPage {
    const char* title;     // short heading, Title Case ("The Idea")
    const char* text;      // body; "\n" for line breaks, "\xE2\x80\xA2 " bullets
};

struct Help {
    const HelpPage* pages;
    uint8_t         count;
};

} // namespace games

#define CYD_HELP(id, pages_array)                                                         \
    namespace games {                                                                     \
    extern const Help id##_help;                                                          \
    const Help id##_help = {pages_array, sizeof pages_array / sizeof pages_array[0]};     \
    }
