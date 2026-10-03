// Saves each game's play history (stats) as CSV, one file per game.
//
// With a usable SD card: /CYD-Classic-Games/<id>.csv on the card, full
// history, opens in Excel. Without one: /stats/<id>.csv on the board's flash,
// the most recent records only. Records made on flash are moved to the card
// the first time a card is found.
//
// The store doesn't know any game's columns. A file is a header line, then
// records "seq,<body>": the store numbers them (and renumbers after a trim or
// delete), the game formats and parses the body.
#pragma once

bool        stats_store_append(const char* id, const char* header, const char* body);
// Calls fn(line, ctx) for every line of the game's file (header included).
// False if nothing could be read.
bool        stats_store_read(const char* id, void (*fn)(const char* line, void* ctx), void* ctx);
// Remove the game's most recent record (e.g. a game recorded by mistake).
bool        stats_store_delete_last(const char* id);
// Remove all of the game's records (card and board memory).
bool        stats_store_clear(const char* id);
// Where the last read/append went: "SD card" or "board memory".
const char* stats_store_location();
