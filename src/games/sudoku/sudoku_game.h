// Sudoku game state and rules on top of the sudoku core.
// No LVGL/Arduino here either, so the rules can be tested on a PC.
#pragma once

#include <cstddef>
#include <cstdint>
#include "sudoku_core.h"

namespace sudoku {

// One cell's previous state, recorded before every change. All changes made
// by one player action share a `group`, and undo restores a whole group, so
// undoing a placement also brings back the notes it auto-cleared.
struct Change {
    uint8_t  idx;
    uint8_t  value;
    uint16_t notes;
    uint16_t group;
};

class Game {
public:
    static constexpr int kUndoCap = 600;

    void start(Difficulty d, sudoku::Rng& rng);
    // Start from a puzzle made elsewhere (e.g. the background puzzle stock)
    void start_with(Difficulty d, const sudoku::Grid& puzzle, const sudoku::Grid& solution);
    void restart();                       // clear all entries, keep the puzzle

    // ---- Queries -----------------------------------------------------------
    bool        active() const        { return active_; }
    Difficulty  difficulty() const    { return diff_; }
    uint8_t     value(int i) const    { return value_[i]; }
    bool        given(int i) const    { return puzzle_.c[i] != 0; }
    bool        hinted(int i) const   { return hinted_[i] != 0; }   // filled by a hint
    bool        locked(int i) const   { return given(i) || hinted(i); }
    int         hints_used() const    { return hints_used_; }
    uint16_t    notes(int i) const    { return notes_[i]; }   // bit d = note d
    bool        has_note(int i, int d) const { return notes_[i] & (1u << d); }
    // Value clashes with the same digit in its row, column or box.
    bool        conflict(int i) const;
    // How many of digit d are placed correctly (9 = digit finished).
    int         placed_correct(int d) const;
    // How many cells hold digit d right now, right or wrong, givens included.
    int         count(int d) const;
    bool        solved() const;
    bool        can_undo() const      { return undo_n_ > 0; }
    uint32_t    elapsed_s() const     { return elapsed_s_; }
    void        add_second()          { if (active_ && !solved()) ++elapsed_s_; }

    // ---- Player actions (return true if the board changed) -----------------
    // Value mode: put d in the cell, or clear it if it already holds d.
    //             Removes d from notes in the cell's row, column and box.
    // Notes mode: toggle note d (only on empty cells).
    bool enter(int i, int d, bool notes_mode);
    bool erase(int i);                    // clear value and notes
    bool undo();

    // ---- Hints ---------------------------------------------------------------
    // Which cell a hint should point at: `preferred` (the selected cell) if it
    // is empty or wrong; otherwise any wrong entry (a mistake blocks progress);
    // otherwise the empty cell with the fewest candidates, i.e. the easiest
    // next move. -1 if there is nothing to hint (solved).
    int  hint_target(int preferred) const;
    // Fill cell i with its correct digit (locks it, counts as a hint,
    // auto-clears notes like a normal placement, undoable).
    bool apply_hint(int i);

    // ---- Persistence ---------------------------------------------------------
    // Serialized form is a flat byte image; see save layout in game.cpp.
    size_t serialize(uint8_t* buf, size_t cap) const;
    bool   deserialize(const uint8_t* buf, size_t len);
    static size_t max_serialized_size();

private:
    void record(int i);                   // push cell i's current state
    void clear_peer_notes(int i, int d);  // remove note d around cell i (recorded)
    bool needs_hint(int i) const;
    void begin_action()                   { ++group_; }

    bool          active_ = false;
    Difficulty    diff_   = Difficulty::Easy;
    sudoku::Grid  puzzle_{};
    sudoku::Grid  solution_{};
    uint8_t       value_[N]{};
    uint16_t      notes_[N]{};
    uint32_t      elapsed_s_ = 0;
    uint8_t       hinted_[N]{};
    uint8_t       hints_used_ = 0;
    Change        undo_[kUndoCap];
    uint16_t      undo_n_ = 0;
    uint16_t      group_  = 0;
};

} // namespace sudoku
