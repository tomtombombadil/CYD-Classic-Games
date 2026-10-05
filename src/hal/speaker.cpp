#include "speaker.h"

#include <Arduino.h>
#include <math.h>
#include "boards/board_select.h"

#ifdef BOARD_PIN_SPEAKER

namespace {

constexpr int      kMaxTones = 24;
constexpr uint8_t  kBits     = 12;           // duty resolution: fine steps at low volume
constexpr uint32_t kFull     = 1u << kBits;
constexpr uint32_t kAmpOffMs = 60;           // amp stays on briefly after the last tone

SpeakerTone queue[kMaxTones];
int         count = 0, pos = 0;
uint32_t    step_end = 0, idle_since = 0;
bool        ready = false, playing = false, amp_on = false;
uint32_t    duty = 0;

uint32_t duty_for(uint8_t volume)
{
    if (!volume) return 0;
    if (volume > 100) volume = 100;
    const float a = (volume / 100.0f) * (volume / 100.0f);      // amplitude, 0..1
    const uint32_t d = static_cast<uint32_t>(asinf(a) / float(M_PI) * kFull + 0.5f);
    return d ? d : 1;                                          // 100 % -> kFull / 2
}

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
        ledcChangeFrequency(BOARD_PIN_SPEAKER, t.hz, kBits);
        ledcWrite(BOARD_PIN_SPEAKER, duty);
    } else {
        ledcWrite(BOARD_PIN_SPEAKER, 0);
    }
    step_end = millis() + t.ms;
}

} // namespace

void speaker_begin()
{
    ready = ledcAttach(BOARD_PIN_SPEAKER, 1000, kBits);
    if (ready) ledcWrite(BOARD_PIN_SPEAKER, 0);
    amp_on = true;
    amp(false);
}

void speaker_play(const SpeakerTone* tones, int n, uint8_t volume)
{
    if (!ready || n <= 0) return;
    duty = duty_for(volume);
    if (!duty) { speaker_stop(); return; }
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
void speaker_play(const SpeakerTone*, int, uint8_t) {}
void speaker_stop() {}
void speaker_loop() {}

#endif
