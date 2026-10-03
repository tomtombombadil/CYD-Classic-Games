// CYD Classic Games - entry point.
#include <Arduino.h>
#include <esp_random.h>
#include <lvgl.h>
#include "app/device_log.h"
#include "app/legacy_import.h"
#include "app/save_store.h"
#include "app/settings_store.h"
#include "app/stats_store.h"
#include "app/themes_store.h"
#include "hal/lvgl_port.h"
#include "boards/board_select.h"
#include "hal/panel_prefs.h"
#include "hal/speaker.h"
#include "hal/splash.h"
#include "hal/touch_cal.h"
#include "ui/shell.h"

#ifndef CYD_GAMES_VERSION
#define CYD_GAMES_VERSION "dev"    // CI sets this from the git tag / commit
#endif

#ifndef CYD_ROTATION
#define CYD_ROTATION 0             // 0 = portrait
#endif

namespace {

// esp_random() is weaker with the radio off; mixing in the microsecond
// timer (when the player tapped) makes repeated puzzles even less likely.
uint32_t hw_seed() { return esp_random() ^ (micros() * 2654435761u); }

// Put the on-board extras in a known, quiet state: RGB LED off and the
// audio amplifier disabled (floating pins can light the LED or make a
// connected speaker hiss).
void quiet_peripherals()
{
#ifdef BOARD_PIN_LED_R
    const uint8_t off = BOARD_LED_ACTIVE_LOW ? HIGH : LOW;
    const uint8_t leds[3] = {BOARD_PIN_LED_R, BOARD_PIN_LED_G, BOARD_PIN_LED_B};
    for (uint8_t p : leds) { pinMode(p, OUTPUT); digitalWrite(p, off); }
#endif
#ifdef BOARD_PIN_AUDIO_EN
    pinMode(BOARD_PIN_AUDIO_EN, OUTPUT);
    digitalWrite(BOARD_PIN_AUDIO_EN, HIGH);   // high = amplifier off
#endif
}

// Panel color fixes change how the hardware shows every pixel; the image in
// LVGL is unchanged, so a full redraw pushes it out again.
void redraw_all()
{
    lv_obj_invalidate(lv_screen_active());
    lv_obj_invalidate(lv_layer_top());
}

void toggle_invert()
{
    PanelPrefs p = panel_prefs_get();
    p.invert = !p.invert;
    panel_prefs_set(lvgl_port_gfx(), p);
    redraw_all();
}

void toggle_swap_rb()
{
    PanelPrefs p = panel_prefs_get();
    p.swap_rb = !p.swap_rb;
    panel_prefs_set(lvgl_port_gfx(), p);
    redraw_all();
}

void flash_invert(bool on) { panel_prefs_flash(lvgl_port_gfx(), on); }

void play_tones(const ui::Tone* t, int n)
{
    static_assert(sizeof(ui::Tone) == sizeof(SpeakerTone), "tone layouts differ");
    speaker_play(reinterpret_cast<const SpeakerTone*>(t), n, ui::settings().volume);
}

void set_flip(bool flipped) { lvgl_port_set_rotation((CYD_ROTATION + (flipped ? 2 : 0)) & 3); }

void recalibrate()
{
    // Calibration draws with LovyanGFX directly; restarting afterwards gives
    // LVGL a clean screen. The open game was saved by the caller.
    LGFX& gfx = lvgl_port_gfx();
    gfx.waitDMA();
    gfx.endWrite();
    touch_cal_run(gfx);
    ESP.restart();
}

// The Device Log screen reads the log through this; it also goes to the
// serial port then, for anyone with the Serial Monitor open.
bool log_read(void (*line)(const char*, void*), void* ctx)
{
    struct Both { void (*line)(const char*, void*); void* ctx; } both{line, ctx};
    Serial.println("---- device log ----");
    const bool ok = device_log_read([](const char* text, void* p) {
        Serial.println(text);
        Both* b = static_cast<Both*>(p);
        b->line(text, b->ctx);
    }, &both);
    Serial.println("---- end of log ----");
    return ok;
}

} // namespace

void setup()
{
    Serial.begin(115200);
    delay(50);
    Serial.println("\nCYD Classic Games " CYD_GAMES_VERSION);
    quiet_peripherals();

    if (!lvgl_port_init(CYD_ROTATION)) {
        Serial.println("Display init failed - halting");
        while (true) delay(1000);
    }

    device_log_begin(CYD_GAMES_VERSION, BOARD_NAME);
    legacy_import();
    speaker_begin();

    // Splash: a different one of the title images each boot, then a tap
    ui::UiSettings settings = settings_store_load();
    if (settings.flip) set_flip(true);       // before the splash, so it shows the right way up
    lvgl_port_set_brightness(settings.brightness);
    const int shown = settings.splash_next % splash_count();
    settings.splash_next = static_cast<uint8_t>((shown + 1) % splash_count());
    settings_store_save(settings);
    splash_show(lvgl_port_gfx(), shown);

    ui::Shell sh{};
    sh.random_seed       = hw_seed;
    sh.load_game         = save_store_load;
    sh.save_game         = save_store_save;
    sh.save_settings     = settings_store_save;
    sh.save_themes       = themes_store_save;
    sh.play_tones        = play_tones;
    sh.toggle_invert     = toggle_invert;
    sh.toggle_swap_rb    = toggle_swap_rb;
    sh.flash_invert      = flash_invert;
    sh.set_brightness    = lvgl_port_set_brightness;
    sh.set_flip          = set_flip;
    sh.recalibrate_touch = recalibrate;
    sh.raw_touch         = lvgl_port_raw_touch;
    sh.stats_append      = stats_store_append;
    sh.stats_read        = stats_store_read;
    sh.stats_delete_last = stats_store_delete_last;
    sh.stats_clear       = stats_store_clear;
    sh.stats_location    = stats_store_location;
    sh.log               = device_log;
    sh.log_read          = log_read;
    sh.log_clear         = device_log_clear;
#if BOARD_SD_USABLE
    sh.log_copy_sd       = device_log_copy_sd;
#endif
    sh.memory            = device_memory;
    sh.firmware_version  = CYD_GAMES_VERSION;
    sh.board_name        = BOARD_NAME;
    ui::app_begin(sh, settings, themes_store_load());
    Serial.printf("[app] picker up, free heap %lu\n", (unsigned long)ESP.getFreeHeap());
}

void loop()
{
    const uint32_t wait_ms = lvgl_port_loop();
    ui::app_tick(millis());
    speaker_loop();
    device_log_loop();
    delay(wait_ms < 5 ? wait_ms : 5);
}
