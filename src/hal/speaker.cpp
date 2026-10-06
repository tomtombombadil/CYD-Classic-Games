#include "speaker.h"

#include <Arduino.h>
#include <math.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "boards/board_select.h"

#ifdef BOARD_PIN_SPEAKER

// The tones are stepped by an esp_timer (its own high-priority task), not by
// loop(): a slow frame (a big redraw) used to stretch the notes, so the same
// sound could play slower one time than the next (Tom, 2026-10-05: the
// computer's shells whistled slower than his own).
namespace {

constexpr int      kMaxTones = 24;
constexpr uint8_t  kBits     = 12;           // duty resolution: fine steps at low volume
constexpr uint32_t kFull     = 1u << kBits;
constexpr uint32_t kAmpOffMs = 60;           // amp stays on briefly after the last tone

SpeakerTone       queue[kMaxTones];
int               count = 0, pos = 0;
bool              ready = false, playing = false, amp_on = false;
uint32_t          duty = 0;
esp_timer_handle_t timer = nullptr;
SemaphoreHandle_t  lock = nullptr;

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

void arm(uint32_t ms)
{
    esp_timer_stop(timer);                               // (fails harmlessly when not running)
    esp_timer_start_once(timer, uint64_t(ms ? ms : 1) * 1000);
}

// (lock held)
void start_step()
{
    const SpeakerTone& t = queue[pos];
    if (t.hz) {
        ledcChangeFrequency(BOARD_PIN_SPEAKER, t.hz, kBits);
        ledcWrite(BOARD_PIN_SPEAKER, duty);
    } else {
        ledcWrite(BOARD_PIN_SPEAKER, 0);
    }
    arm(t.ms);
}

// (lock held)
void stop_locked()
{
    ledcWrite(BOARD_PIN_SPEAKER, 0);
    if (playing) arm(kAmpOffMs);                         // then the amp goes off
    playing = false;
}

void on_timer(void*)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    if (playing) {
        if (++pos < count) start_step();
        else stop_locked();
    } else {
        amp(false);
    }
    xSemaphoreGive(lock);
}

} // namespace

void speaker_begin()
{
    ready = ledcAttach(BOARD_PIN_SPEAKER, 1000, kBits);
    if (ready) ledcWrite(BOARD_PIN_SPEAKER, 0);
    amp_on = true;
    amp(false);
    if (!ready) return;
    lock = xSemaphoreCreateMutex();
    esp_timer_create_args_t args = {};
    args.callback = on_timer;
    args.name = "speaker";
    if (!lock || esp_timer_create(&args, &timer) != ESP_OK) ready = false;
}

void speaker_play(const SpeakerTone* tones, int n, uint8_t volume)
{
    if (!ready || n <= 0) return;
    const uint32_t d = duty_for(volume);
    if (!d) { speaker_stop(); return; }
    if (n > kMaxTones) n = kMaxTones;
    xSemaphoreTake(lock, portMAX_DELAY);
    duty = d;
    for (int k = 0; k < n; ++k) queue[k] = tones[k];
    count = n;
    pos = 0;
    playing = true;
    amp(true);
    start_step();
    xSemaphoreGive(lock);
}

void speaker_stop()
{
    if (!ready) return;
    xSemaphoreTake(lock, portMAX_DELAY);
    stop_locked();
    xSemaphoreGive(lock);
}

void speaker_loop() {}     // (the timer does it all now)

#else   // board without a speaker pin

void speaker_begin() {}
void speaker_play(const SpeakerTone*, int, uint8_t) {}
void speaker_stop() {}
void speaker_loop() {}

#endif
