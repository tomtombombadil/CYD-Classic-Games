// Settings: theme, sound, brightness, panel color fixes, touch calibration
// and the touch test (from Diagnostics). Shared by every game and the picker.
#include <cstdio>
#include <cstring>
#include <lvgl.h>
#include "shell.h"
#include "sound.h"
#include "theme.h"
#include "widgets.h"

namespace ui {

namespace {

void (*back_fn)() = nullptr;

enum Action : intptr_t { kTheme, kInvert, kSwapRb, kFlip, kRecal, kDiagnostics, kBack };

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

void volume_mute_cb(lv_event_t*)
{
    settings().volume = 0;
    if (volume_slider) lv_slider_set_value(volume_slider, 0, LV_ANIM_OFF);
    volume_text();
    save_settings();
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
lv_obj_t*   tt_dots[kMaxDots] = {};
int         tt_dot_next = 0;
lv_obj_t*   tt_info = nullptr;
lv_timer_t* tt_timer = nullptr;
int16_t     tt_x[kMaxSamples], tt_y[kMaxSamples];
int         tt_n = 0;
int         tt_misses = 0;           // empty readings since the last touch
char        tt_log[4][48];
int         tt_log_n = 0;

void tt_dot(int16_t x, int16_t y, bool first)
{
    lv_obj_t* ov = overlay();
    if (!ov) return;
    lv_obj_t*& d = tt_dots[tt_dot_next];
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
    if (tt_n == 0) return;
    // "Settled" position = median of the second half of the readings
    int16_t xs[kMaxSamples], ys[kMaxSamples];
    const int from = tt_n / 2, n = tt_n - from;
    for (int k = 0; k < n; ++k) { xs[k] = tt_x[from + k]; ys[k] = tt_y[from + k]; }
    for (int a = 0; a < n; ++a) for (int b = a + 1; b < n; ++b) {
        if (xs[b] < xs[a]) { int16_t t = xs[a]; xs[a] = xs[b]; xs[b] = t; }
        if (ys[b] < ys[a]) { int16_t t = ys[a]; ys[a] = ys[b]; ys[b] = t; }
    }
    const int sx = xs[n / 2], sy = ys[n / 2];
    for (int k = 3; k > 0; --k) memcpy(tt_log[k], tt_log[k - 1], sizeof tt_log[0]);
    snprintf(tt_log[0], sizeof tt_log[0], "First off by %+d,%+d (%d reads)",
             tt_x[0] - sx, tt_y[0] - sy, tt_n);
    if (tt_log_n < 4) ++tt_log_n;
    char text[220];
    int len = snprintf(text, sizeof text, "Red dot = first reading of a tap.");
    for (int k = 0; k < tt_log_n && len < (int)sizeof text; ++k)
        len += snprintf(text + len, sizeof text - len, "\n%s", tt_log[k]);
    lv_label_set_text(tt_info, text);
    tt_n = 0;
}

void tt_timer_cb(lv_timer_t*)
{
    int16_t x, y;
    if (shell().raw_touch && shell().raw_touch(&x, &y)) {
        tt_misses = 0;
        if (tt_n < kMaxSamples) { tt_x[tt_n] = x; tt_y[tt_n] = y; }
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
    for (auto& d : tt_dots) d = nullptr;     // children of the overlay
}

void tt_done_cb(lv_event_t*) { diagnostics_open(); }

} // namespace

void settings_open_touch_test()
{
    overlay_begin("Touch Test", tt_stop);
    tt_info = overlay_text("Tap anywhere. Red dot = first reading of a tap, blue = the rest.", true);
    tt_n = tt_misses = tt_log_n = tt_dot_next = 0;
    for (auto& d : tt_dots) d = nullptr;
    overlay_bottom_button("Done", tt_done_cb, 0);
    tt_timer = lv_timer_create(tt_timer_cb, 10, nullptr);
}

namespace {

// ---- Buttons ------------------------------------------------------------------
void action_cb(lv_event_t* e)
{
    const Shell& H = shell();
    switch (reinterpret_cast<intptr_t>(lv_event_get_user_data(e))) {
        case kTheme: theme_open(); break;
        case kInvert: if (H.toggle_invert) H.toggle_invert(); break;
        case kSwapRb: if (H.toggle_swap_rb) H.toggle_swap_rb(); break;
        case kFlip:
            if (!H.set_flip) break;
            settings().flip = !settings().flip;
            H.set_flip(settings().flip);
            save_settings();
            set_checked(lv_event_get_target_obj(e), settings().flip);
            break;
        case kRecal:
            app_save_current();
            if (H.recalibrate_touch) H.recalibrate_touch();
            break;
        case kDiagnostics: diagnostics_open(); break;
        case kBack:
            if (back_fn) back_fn();
            else close_overlays();
            break;
    }
}

} // namespace

void settings_reopen() { settings_open(back_fn); }

void settings_open(void (*back)())
{
    volume_label = volume_slider = nullptr;
    back_fn = back;
    const UiSettings& S = settings();
    overlay_begin("Settings");
    overlay_pair("Theme", action_cb, kTheme, "Invert Colors", action_cb, kInvert);

    // Brightness and Volume: applied while dragging, saved on release
    lv_point_t sz;
    lv_text_get_size(&sz, "Brightness", menu_font(), 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    slider_row("Brightness", sz.x, kMinBrightness, 255, S.brightness, brightness_cb, nullptr);
    volume_slider = slider_row("Volume", sz.x, 0, 100, S.volume, volume_cb, &volume_label);
    lv_obj_set_clickable(volume_label, true);   // tap the label = mute
    lv_obj_add_event_cb(volume_label, volume_mute_cb, LV_EVENT_CLICKED, nullptr);
    volume_text();

    // Full width: "Swap Red/Blue" doesn't fit half a row at the menu font.
    overlay_pair("Swap Red/Blue", action_cb, kSwapRb, nullptr, nullptr, 0);
    // A toggle, lit while the screen is turned: the USB cord can leave
    // either end of the board (board and firmware moved to Diagnostics)
    lv_obj_t* flip = overlay_button(overlay(), "Rotate Screen 180", action_cb, kFlip);
    set_checked(flip, S.flip);
    overlay_pair("Recalibrate", action_cb, kRecal, "Diagnostics", action_cb, kDiagnostics);
    overlay_button(overlay(), "Back", action_cb, kBack, true);
}

} // namespace ui
