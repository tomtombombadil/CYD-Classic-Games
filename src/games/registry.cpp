#include "registry.h"

#include <cstring>

// The PC preview can point this at a longer test list (picker paging).
#ifndef CYD_GAMES_DEF
#define CYD_GAMES_DEF "games.def"
#endif

namespace games {

// Each game provides `const GameOps <id>_ops` in its own folder.
#define GAME(id, title, cat, modes, blurb) extern const GameOps id##_ops;
#include CYD_GAMES_DEF
#undef GAME

namespace {

const GameInfo kGames[] = {
#define GAME(id, title, cat, modes, blurb) {#id, title, Category::cat, static_cast<uint8_t>(modes), blurb, &id##_ops},
#include CYD_GAMES_DEF
#undef GAME
};

constexpr int kCount = sizeof kGames / sizeof kGames[0];

} // namespace

const char* category_name(Category c)
{
    switch (c) {
        case Category::Puzzles:  return "Puzzles";
        case Category::Strategy: return "Strategy";
        case Category::Word:     return "Word";
        case Category::Dice:     return "Dice";
    }
    return "";
}

int             count()          { return kCount; }
const GameInfo& get(int index)   { return kGames[index]; }

int find(const char* id)
{
    if (!id || !*id) return -1;
    for (int i = 0; i < kCount; ++i)
        if (strcmp(kGames[i].id, id) == 0) return i;
    return -1;
}

} // namespace games
