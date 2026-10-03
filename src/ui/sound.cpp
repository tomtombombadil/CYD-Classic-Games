#include "sound.h"

#include "shell.h"

namespace ui {

namespace {

struct Seq { const Tone* t; int n; };

constexpr Tone kPlace[]  = {{1480, 22}};
constexpr Tone kMove[]   = {{880, 22}, {1320, 28}};
// "Aww": a soft high note, then a lower one - disappointed, not a buzzer (Tom)
constexpr Tone kError[]  = {{660, 110}, {0, 25}, {440, 220}};
constexpr Tone kHint[]   = {{1320, 60}, {1760, 100}};
constexpr Tone kWin[]    = {{523, 110}, {659, 110}, {784, 110}, {1047, 280}};
constexpr Tone kLose[]   = {{392, 170}, {330, 170}, {262, 340}};
constexpr Tone kDraw[]   = {{587, 140}, {0, 40}, {587, 200}};
constexpr Tone kTurn[]   = {{990, 26}};
// G C E G, a bounce back to E, and a held G: bright and a little silly
constexpr Tone kFanfare[] = {{784, 90}, {0, 25}, {1047, 90}, {0, 25}, {1319, 90}, {0, 25},
                             {1568, 150}, {0, 50}, {1319, 90}, {1568, 340}};

template <int N> constexpr Seq seq(const Tone (&t)[N]) { return {t, N}; }

// Up C E G, then a quick G-C trill: short and pleased with itself
constexpr Tone kTrill[]   = {{1047, 60}, {1319, 60}, {1568, 70}, {2093, 45}, {1568, 45},
                             {2093, 45}, {1568, 45}, {2093, 160}};

const Seq kSounds[] = {
    seq(kPlace), seq(kMove), seq(kError),
    seq(kHint), seq(kWin), seq(kLose), seq(kDraw), seq(kTurn), seq(kFanfare), seq(kTrill),
};

} // namespace

void sound(Sound s)
{
    const Shell& H = shell();
    if (!settings().volume || !H.play_tones) return;
    const Seq& q = kSounds[static_cast<int>(s)];
    H.play_tones(q.t, q.n);
}

} // namespace ui
