#include "keyboard.h"

#include <cstdio>
#include <cstring>
#include <lvgl.h>
#include "sound.h"
#include "theme.h"
#include "widgets.h"

namespace ui {

namespace {

constexpr size_t kMax = 32;
char        text[kMax + 1];
size_t      limit = kMax;
bool        digits = false;
const char* title_ = "";
void      (*done_fn)(const char*) = nullptr;
lv_obj_t*   label = nullptr;

void build();

void show()
{
    if (!label) return;
    char t[kMax + 4];
    snprintf(t, sizeof t, "%s_", text);
    lv_label_set_text(label, t);
}

void key_cb(lv_event_t* e)
{
    const int c = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    size_t n = strlen(text);
    if (c == 1) { if (n) text[n - 1] = 0; show(); return; }               // backspace
    if (c == 2) { digits = !digits; build(); return; }                    // ABC / 123
    if (c == 3) {                                                         // Done
        while (n && text[n - 1] == ' ') text[--n] = 0;
        const char* s = text;
        while (*s == ' ') ++s;
        char out[kMax + 1];
        snprintf(out, sizeof out, "%s", s);
        if (done_fn) done_fn(out);
        return;
    }
    if (n >= limit) { sound(Sound::Error); return; }
    char ch = char(c);
    // Auto capitals: the first letter of each word
    if (ch >= 'a' && ch <= 'z' && (n == 0 || text[n - 1] == ' ')) ch = char(ch - 'a' + 'A');
    text[n] = ch;
    text[n + 1] = 0;
    show();
}

lv_obj_t* key_row(int kh)
{
    lv_obj_t* row = lv_obj_create(overlay());
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), kh);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 4, 0);
    lv_obj_set_scrollable(row, false);
    return row;
}

void build()
{
    overlay_begin(title_, [] { label = nullptr; });
    label = lv_label_create(overlay());
    lv_obj_set_width(label, lv_pct(100));
    lv_obj_set_style_text_font(label, metrics().large ? &lv_font_montserrat_28 : &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(label, pal().ink, 0);
    lv_obj_set_style_bg_color(label, pal().cell, 0);
    lv_obj_set_style_bg_opa(label, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(label, 4, 0);
    show();
    static const char* const kLetters = "abcdefghijklmnopqrstuvwxyz'-";
    static const char* const kDigits = "1234567890+-'#&.";
    const char* keys = digits ? kDigits : kLetters;
    const int n = int(strlen(keys)), per = 6;
    const int kh = menu_btn_h();
    for (int i = 0; i < n; i += per) {
        lv_obj_t* row = key_row(kh);
        for (int k = i; k < i + per; ++k) {
            if (k < n) {
                char lab[2] = {keys[k] >= 'a' && keys[k] <= 'z' ? char(keys[k] - 'a' + 'A') : keys[k], 0};
                lv_obj_t* b = make_key(row, 10, kh, key_cb, intptr_t(keys[k]));
                lv_obj_set_flex_grow(b, 1);
                key_label(b, lab, menu_font());
            } else if (k == n) {
                lv_obj_t* b = make_key(row, 10, kh, key_cb, 1);
                lv_obj_set_flex_grow(b, per - (n % per) == 0 ? 1 : per - (n % per));
                key_label(b, LV_SYMBOL_BACKSPACE, menu_font());
                break;
            }
        }
        if (n % per == 0 && i + per >= n) {                // full last row: backspace on its own
            lv_obj_t* r2 = key_row(kh);
            lv_obj_t* b = make_key(r2, lv_pct(100), kh, key_cb, 1);
            key_label(b, LV_SYMBOL_BACKSPACE, menu_font());
        }
    }
    lv_obj_t* row = key_row(kh);
    lv_obj_set_ignore_layout(row, true);
    lv_obj_align(row, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_t* tg = make_key(row, 10, kh, key_cb, 2);
    lv_obj_set_flex_grow(tg, 1);
    key_label(tg, digits ? "ABC" : "123", menu_font());
    lv_obj_t* sp = make_key(row, 10, kh, key_cb, ' ');
    lv_obj_set_flex_grow(sp, 2);
    key_label(sp, "Space", menu_font());
    lv_obj_t* dn = make_key(row, 10, kh, key_cb, 3);
    lv_obj_set_flex_grow(dn, 1);
    lv_obj_add_state(dn, LV_STATE_CHECKED);
    key_label(dn, "Done", menu_font());
}

} // namespace

void keyboard_open(const char* title, const char* initial, size_t max_len, void (*done)(const char* text))
{
    title_ = title;
    limit = max_len < kMax ? max_len : kMax;
    snprintf(text, sizeof text, "%s", initial ? initial : "");
    if (strlen(text) > limit) text[limit] = 0;
    digits = false;
    done_fn = done;
    build();
}

} // namespace ui
