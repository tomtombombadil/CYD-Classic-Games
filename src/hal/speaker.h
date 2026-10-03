// Tones on the CYD speaker connector: square wave from the LEDC PWM on the
// board's speaker pin (through its amplifier). Non-blocking: speaker_play()
// queues a short sequence and speaker_loop() steps through it. A new
// sequence replaces whatever is playing.
//
// Volume (0..100 %) sets the PWM duty. The loudness of a square wave's tone
// goes with sin(pi * duty), so the duty is picked to give an amplitude of
// (volume / 100)^2 - an even-feeling slider - and 100 % is the plain 50 %
// duty square wave. 0 % plays nothing.
#pragma once

#include <cstdint>

struct SpeakerTone {
    uint16_t hz;           // 0 = rest
    uint16_t ms;
};

void speaker_begin();
void speaker_play(const SpeakerTone* tones, int n, uint8_t volume);
void speaker_stop();
void speaker_loop();       // call from loop()
