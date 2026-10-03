#include "speaker.h"

#include <Arduino.h>
#include "boards/board_select.h"

#ifdef BOARD_PIN_SPEAKER

namespace {

constexpr int      kMaxTones = 16;
constexpr uint32_t kDuty     = 128;          // 50 % of 8 bits: a plain square wave
constexpr uint32_t kAmpOffMs = 60;           // amp stays on briefly after the last tone

SpeakerTone queue[kMaxTones];
int         count = 0, pos = 0;
uint32_t    step_end = 0, idle_since = 0;
bool        ready = false, playing = false, amp_on = false;

void amp(bool on)
{
#ifdef BOARD_PIN_AUDIO_EN
    if (on == amp_on) return;
    digitalWrite(BOARD_PIN_AUDIO_EN, on ? LOW : HIGH);   // low = amplifier on
#endif
    amp_on = on;
}

void start_step()
{
    const SpeakerTone& t = queue[pos];
    if (t.hz) {
        ledcChangeFrequency(BOARD_PIN_SPEAKER, t.hz, 8);
        ledcWrite(BOARD_PIN_SPEAKER, kDuty);
    } else {
        ledcWrite(BOARD_PIN_SPEAKER, 0);
    }
    step_end = millis() + t.ms;
}

} // namespace

void speaker_begin()
{
    ready = ledcAttach(BOARD_PIN_SPEAKER, 1000, 8);
    if (ready) ledcWrite(BOARD_PIN_SPEAKER, 0);
    amp_on = true;
    amp(false);
}

void speaker_play(const SpeakerTone* tones, int n)
{
    if (!ready || n <= 0) return;
    if (n > kMaxTones) n = kMaxTones;
    for (int k = 0; k < n; ++k) queue[k] = tones[k];
    count = n;
    pos = 0;
    playing = true;
    amp(true);
    start_step();
}

void speaker_stop()
{
    if (!ready) return;
    ledcWrite(BOARD_PIN_SPEAKER, 0);
    playing = false;
    idle_since = millis();
}

void speaker_loop()
{
    if (!ready) return;
    const uint32_t now = millis();
    if (playing && int32_t(now - step_end) >= 0) {
        if (++pos < count) start_step();
        else speaker_stop();
    }
    if (!playing && amp_on && now - idle_since >= kAmpOffMs) amp(false);
}

#else   // board without a speaker pin

void speaker_begin() {}
void speaker_play(const SpeakerTone*, int) {}
void speaker_stop() {}
void speaker_loop() {}

#endif
