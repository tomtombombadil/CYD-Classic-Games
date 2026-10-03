// Tones on the CYD speaker connector: square wave from the LEDC PWM on the
// board's speaker pin (through its amplifier). Non-blocking: speaker_play()
// queues a short sequence and speaker_loop() steps through it. A new
// sequence replaces whatever is playing.
#pragma once

#include <cstdint>

struct SpeakerTone {
    uint16_t hz;           // 0 = rest
    uint16_t ms;
};

void speaker_begin();
void speaker_play(const SpeakerTone* tones, int n);
void speaker_stop();
void speaker_loop();       // call from loop()
