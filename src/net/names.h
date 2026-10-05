// Player names for wireless play (Tom, 2026-10-04): two words, one from
// each list below, picked on the board (Random or Pick From List) - never
// typed. Children may use these boards, and nothing a player writes may
// travel between boards (CLAUDE.md, the hard rule), so a name goes on the
// air as two numbers and every board turns them back into words.
//
// The lists are APPEND-ONLY: a word's number never changes and no word is
// ever removed or reworded, so every firmware version shows the same name.
// Each word is at most kWordMax letters, so a name fits a 240-px list row
// next to "- available". Kid-safe and silly; check new words for unhappy
// pairings with every word of the other list.
//
// Plain C++, host-tested (tools/host_tests/test_net.cpp).
#pragma once

#include <cstddef>
#include <cstdint>

namespace names {

constexpr size_t kWordMax = 8;                 // letters in one word
constexpr size_t kNameMax = 2 * kWordMax + 1;  // "Wobbly Llama"

int  first_count();                            // words in the first list (describing words)
int  second_count();                           // words in the second list (things and creatures)
const char* first(int i);                      // "?" when out of range (a newer board's word)
const char* second(int i);

// The name for a pair of numbers, e.g. "Wobbly Llama"
void format(uint16_t a, uint16_t b, char* out, size_t cap);
// A pair picked from `seed` (a board's starting name, or the Random key)
void random_pair(uint32_t seed, uint16_t* a, uint16_t* b);
// The numbers of a name made by format(); false if it isn't one
bool parse(const char* name, uint16_t* a, uint16_t* b);

} // namespace names
