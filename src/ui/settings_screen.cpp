// Settings: theme, sound, brightness, panel color fixes, touch calibration
// and the touch test (from Diagnostics). Shared by every game and the picker.
#include <cstdio>
#include <cstring>
#include <new>
#include <lvgl.h>
#include "shell.h"
#include "sound.h"
#include "theme.h"
#include "widgets.h"
#include "games/common/wplay.h"

namespace ui {

namespace {

void (*back_fn)() = nullptr;


void brightness_cb(lv_event_t* e)
{
    lv_obj_t* sl = lv_event_get_target_obj(e);
    settings().brightness = static_cast<uint8_t>(lv_slider_get_value(sl));
    if (shell().set_brightness) shell().set_brightness(settings().brightness);
    if (lv_event_get_code(e) == LV_EVENT_RELEASED) save_settings();
}

// Volume: 0..100 %, 0 = silent ("Muted"). A tap anywhere left of the track
// (the "Volume" label included) lands on 0. On release it plays one sound at
// the new level and saves.
lv_obj_t* volume_label = nullptr;
lv_obj_t* volume_slider = nullptr;

void volume_text() { if (volume_label) lv_label_set_text(volume_label, settings().volume ? "Volume" : "Muted"); }

void volume_cb(lv_event_t* e)
{
    lv_obj_t* sl = lv_event_get_target_obj(e);
    settings().volume = static_cast<uint8_t>(lv_slider_get_value(sl));
    volume_text();
    if (lv_event_get_code(e) == LV_EVENT_RELEASED) {
        save_settings();
        sound(Sound::Place);
    }
}

// One row: label (fixed width, so the sliders line up) and a slider
lv_obj_t* slider_row(const char* text, int label_w, int lo, int hi, int value,
                     lv_event_cb_t cb, lv_obj_t** label_out)
{
    const bool large = metrics().large;
    lv_obj_t* row = lv_obj_create(overlay());
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), menu_btn_h());
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, large ? 14 : 10, 0);
    lv_obj_set_scrollable(row, false);
    lv_obj_t* bl = lv_label_create(row);
    lv_label_set_text(bl, text);
    lv_obj_set_width(bl, label_w);
    lv_obj_set_style_text_font(bl, menu_font(), 0);
    lv_obj_set_style_text_color(bl, pal().ink, 0);
    if (label_out) *label_out = bl;
    lv_obj_t* sl = lv_slider_create(row);
    lv_obj_remove_style_all(sl);
    lv_obj_set_flex_grow(sl, 1);
    lv_obj_set_height(sl, large ? 12 : 10);
    lv_obj_set_style_margin_right(sl, large ? 14 : 10, 0);  // room for the knob
    lv_obj_set_style_bg_color(sl, pal().key_border, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(sl, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(sl, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(sl, pal().key_on, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(sl, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(sl, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(sl, pal().ink, LV_PART_KNOB);
    lv_obj_set_style_bg_opa(sl, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_radius(sl, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_set_style_pad_all(sl, large ? 9 : 7, LV_PART_KNOB);   // knob size
    lv_obj_set_ext_click_area(sl, large ? 16 : 12);               // easy to grab
    lv_slider_set_range(sl, lo, hi);
    lv_slider_set_value(sl, value, LV_ANIM_OFF);
    lv_obj_add_event_cb(sl, cb, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_add_event_cb(sl, cb, LV_EVENT_RELEASED, nullptr);
    return sl;
}

// ---- Touch test -------------------------------------------------------------
// Plots every raw reading during a tap (first reading red, the rest blue) and
// reports how far the first reading was from where the tap settled. Raw =
// calibrated but not filtered, so this shows what the hardware reports.
constexpr int kMaxDots = 160;
constexpr int kMaxSamples = 64;
// The big buffers live on the heap while the test is open (~1.1 KB that
// would otherwise be static RAM all the time)
struct TouchTest {
    lv_obj_t* dots[kMaxDots];
    int16_t   x[kMaxSamples], y[kMaxSamples];
    char      log[4][48];
};
TouchTest*  TT = nullptr;
int         tt_dot_next = 0;
lv_obj_t*   tt_info = nullptr;
lv_timer_t* tt_timer = nullptr;
int         tt_n = 0;
int         tt_misses = 0;           // empty readings since the last touch
int         tt_log_n = 0;

void tt_dot(int16_t x, int16_t y, bool first)
{
    lv_obj_t* ov = overlay();
    if (!ov || !TT) return;
    lv_obj_t*& d = TT->dots[tt_dot_next];
    tt_dot_next = (tt_dot_next + 1) % kMaxDots;
    if (!d) {
        d = lv_obj_create(ov);
        lv_obj_remove_style_all(d);
        lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
        lv_obj_set_clickable(d, false);
        lv_obj_set_ignore_layout(d, true);
    }
    const int r = first ? 4 : 2;
    // Positions are relative to the overlay's content area: remove padding
    const int px = lv_obj_get_style_pad_left(ov, LV_PART_MAIN);
    const int py = lv_obj_get_style_pad_top(ov, LV_PART_MAIN);
    lv_obj_set_size(d, 2 * r + 1, 2 * r + 1);
    lv_obj_set_pos(d, x - r - px, y - r - py);
    lv_obj_set_style_bg_color(d, first ? pal().conflict : pal().entry, 0);
    if (first) lv_obj_move_foreground(d);
}

void tt_finish_tap()
{
    if (tt_n == 0 || !TT) return;
    // "Settled" position = median of the second half of the readings
    int16_t xs[kMaxSamples], ys[kMaxSamples];
    const int from = tt_n / 2, n = tt_n - from;
    for (int k = 0; k < n; ++k) { xs[k] = TT->x[from + k]; ys[k] = TT->y[from + k]; }
    for (int a = 0; a < n; ++a) for (int b = a + 1; b < n; ++b) {
        if (xs[b] < xs[a]) { int16_t t = xs[a]; xs[a] = xs[b]; xs[b] = t; }
        if (ys[b] < ys[a]) { int16_t t = ys[a]; ys[a] = ys[b]; ys[b] = t; }
    }
    const int sx = xs[n / 2], sy = ys[n / 2];
    for (int k = 3; k > 0; --k) memcpy(TT->log[k], TT->log[k - 1], sizeof TT->log[0]);
    snprintf(TT->log[0], sizeof TT->log[0], "First off by %+d,%+d (%d reads)",
             TT->x[0] - sx, TT->y[0] - sy, tt_n);
    if (tt_log_n < 4) ++tt_log_n;
    char text[220];
    int len = snprintf(text, sizeof text, "Red dot = first reading of a tap.");
    for (int k = 0; k < tt_log_n && len < (int)sizeof text; ++k)
        len += snprintf(text + len, sizeof text - len, "\n%s", TT->log[k]);
    lv_label_set_text(tt_info, text);
    tt_n = 0;
}

void tt_timer_cb(lv_timer_t*)
{
    if (!TT) return;
    int16_t x, y;
    if (shell().raw_touch && shell().raw_touch(&x, &y)) {
        tt_misses = 0;
        if (tt_n < kMaxSamples) { TT->x[tt_n] = x; TT->y[tt_n] = y; }
        tt_dot(x, y, tt_n == 0);
        if (tt_n < kMaxSamples) ++tt_n;
    } else if (tt_n && ++tt_misses >= 3) {   // 30 ms without touch = tap over
        tt_finish_tap();
    }
}

void tt_stop()
{
    if (tt_timer) {
        lv_timer_delete(tt_timer);
        tt_timer = nullptr;
    }
    delete TT;                               // the dots are children of the overlay
    TT = nullptr;
}

void tt_done_cb(lv_event_t*) { settings_open_touch(); }

} // namespace

void settings_open_touch_test()
{
    overlay_begin("Touch Test", tt_stop);
    tt_info = overlay_text("Tap anywhere. Red dot = first reading of a tap, blue = the rest.", true);
    tt_n = tt_misses = tt_log_n = tt_dot_next = 0;
    delete TT;
    TT = new (std::nothrow) TouchTest();       // zeroed: no dots yet
    overlay_back(tt_done_cb, 0);                // the header's arrow: back to Touch
    tt_timer = lv_timer_create(tt_timer_cb, 10, nullptr);
}

namespace {

// ---- Pages ----------------------------------------------------------------------
// Settings: Display, Sound, Touch, Play, About - each its own page; the
// header's back arrow goes up one level (Tom, 2026-10-04)
enum Action : intptr_t {
    kDisplay = 100, kSound, kTouch, kPlay, kAbout,
    kTheme, kInvert, kSwapRb, kFlip, kMute, kTouchTest, kRecal, kMainBack, kPageBack,
};

uint8_t unmute_to = kDefaultVolume;         // Mute off goes back to this

void display_page();
void sound_page();
void touch_page();

void action_cb(lv_event_t* e)
{
    const Shell& H = shell();
    lv_obj_t* key = lv_event_get_target_obj(e);
    switch (reinterpret_cast<intptr_t>(lv_event_get_user_data(e))) {
        case kDisplay: display_page(); break;
        case kSound:   sound_page(); break;
        case kTouch:   touch_page(); break;
        case kPlay:    wplay::open_menu(settings_reopen); break;
        case kAbout:   diagnostics_open(); break;
        case kTheme:   theme_open(); break;
        case kInvert:  if (H.toggle_invert) H.toggle_invert(); break;
        case kSwapRb:  if (H.toggle_swap_rb) H.toggle_swap_rb(); break;
        case kFlip:
            if (!H.set_flip) break;
            settings().flip = !settings().flip;
            H.set_flip(settings().flip);
            save_settings();
            set_checked(key, settings().flip);
            break;
        case kMute:
            if (settings().volume) { unmute_to = settings().volume; settings().volume = 0; }
            else settings().volume = unmute_to ? unmute_to : kDefaultVolume;
            save_settings();
            if (volume_slider) lv_slider_set_value(volume_slider, settings().volume, LV_ANIM_OFF);
            volume_text();
            set_checked(key, settings().volume == 0);
            if (settings().volume) sound(Sound::Place);
            break;
        case kTouchTest: settings_open_touch_test(); break;
        case kRecal:
            app_save_current();
            if (H.recalibrate_touch) H.recalibrate_touch();
            break;
        case kPageBack: settings_reopen(); break;
        case kMainBack:
            if (back_fn) back_fn();
            else close_overlays();
            break;
    }
}

lv_obj_t* toggle_key(const char* text, intptr_t id, bool on)
{
    lv_obj_t* k = overlay_button(overlay(), text, action_cb, id);
    set_checked(k, on);
    return k;
}

void display_page()
{
    volume_label = volume_slider = nullptr;
    overlay_begin("Display");
    lv_point_t sz;
    lv_text_get_size(&sz, "Brightness", menu_font(), 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    slider_row("Brightness", sz.x, kMinBrightness, 255, settings().brightness, brightness_cb, nullptr);
    overlay_button(overlay(), "Invert Colors", action_cb, kInvert);
    overlay_button(overlay(), "Swap Red/Blue", action_cb, kSwapRb);
    // A toggle, lit while the screen is turned: the USB cord can leave either end
    toggle_key("Rotate 180", kFlip, settings().flip);
    overlay_button(overlay(), "Themes", action_cb, kTheme);
    overlay_text("Invert Colors and Swap Red/Blue fix panels that show colors wrong.", true);
    overlay_back(action_cb, kPageBack);
}

void sound_page()
{
    volume_label = volume_slider = nullptr;
    overlay_begin("Sound");
    lv_point_t sz;
    lv_text_get_size(&sz, "Volume", menu_font(), 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    volume_slider = slider_row("Volume", sz.x, 0, 100, settings().volume, volume_cb, &volume_label);
    volume_text();
    toggle_key("Mute", kMute, settings().volume == 0);
    overlay_text("Sounds play for moves, the other side's reply, mistakes and the end of a game. "
                 "Keys are silent.", true);
    overlay_back(action_cb, kPageBack);
}

void touch_page()
{
    overlay_begin("Touch");
    overlay_button(overlay(), "Touch Test", action_cb, kTouchTest);
    overlay_button(overlay(), "Recalibrate", action_cb, kRecal);
    overlay_text("Recalibrate shows a target in each corner: tap each tip with the stylus. "
                 "Touch Test shows where taps land.", true);
    overlay_back(action_cb, kPageBack);
}

} // namespace

void settings_reopen() { settings_open(back_fn); }
void settings_open_display() { display_page(); }
void settings_open_touch() { touch_page(); }

void settings_open(void (*back)())
{
    volume_label = volume_slider = nullptr;
    back_fn = back;
    overlay_begin("Settings");
    overlay_button(overlay(), "Display", action_cb, kDisplay);
    overlay_button(overlay(), "Sound", action_cb, kSound);
    overlay_button(overlay(), "Touch", action_cb, kTouch);
    overlay_button(overlay(), "Play", action_cb, kPlay);
    overlay_button(overlay(), "About", action_cb, kAbout);
    overlay_back(action_cb, kMainBack);
}

} // namespace ui
