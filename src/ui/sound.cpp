#include "sound.h"

#include "shell.h"

namespace ui {

namespace {

struct Seq { const Tone* t; int n; };

constexpr Tone kTap[]    = {{2400, 5}};
constexpr Tone kPlace[]  = {{1480, 22}};
constexpr Tone kMove[]   = {{880, 22}, {1320, 28}};
constexpr Tone kSelect[] = {{1180, 16}};
constexpr Tone kError[]  = {{220, 90}, {0, 30}, {175, 140}};
constexpr Tone kHint[]   = {{1320, 60}, {1760, 100}};
constexpr Tone kWin[]    = {{523, 110}, {659, 110}, {784, 110}, {1047, 280}};
constexpr Tone kLose[]   = {{392, 170}, {330, 170}, {262, 340}};
constexpr Tone kDraw[]   = {{587, 140}, {0, 40}, {587, 200}};
constexpr Tone kTurn[]   = {{990, 26}};

template <int N> constexpr Seq seq(const Tone (&t)[N]) { return {t, N}; }

const Seq kSounds[] = {
    seq(kTap), seq(kPlace), seq(kMove), seq(kSelect), seq(kError),
    seq(kHint), seq(kWin), seq(kLose), seq(kDraw), seq(kTurn),
};

} // namespace

void sound(Sound s)
{
    const Shell& H = shell();
    if (!settings().sound || !H.play_tones) return;
    const Seq& q = kSounds[static_cast<int>(s)];
    H.play_tones(q.t, q.n);
}

} // namespace ui
