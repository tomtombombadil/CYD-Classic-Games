// Diagnostics (Settings -> Diagnostics): board facts, the touch test and the
// device log, paged with < > keys (no scrolling).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <lvgl.h>
#include "shell.h"
#include "theme.h"
#include "widgets.h"

namespace ui {

namespace {

enum : intptr_t { kTouch = 1, kLog, kBack, kPrev, kNext, kClear, kCopy };

// The log while its screen is up: lines joined with '\n', page starts.
char*  text = nullptr;
size_t text_len = 0, text_cap = 0;
int*   starts = nullptr;                   // byte offset of each page
int    pages = 0;
int    page_now = 0;
const char* status = nullptr;              // after Copy To SD

void free_text()
{
    free(text);
    free(starts);
    text = nullptr;
    starts = nullptr;
    text_len = text_cap = 0;
    pages = 0;
}

void add_line(const char* line, void*)
{
    const size_t n = strlen(line);
    if (text_len + n + 2 > text_cap) {
        const size_t cap = (text_len + n + 2) * 2 < 4096 ? 4096 : (text_len + n + 2) * 2;
        char* t = static_cast<char*>(realloc(text, cap));
        if (!t) return;
        text = t;
        text_cap = cap;
    }
    memcpy(text + text_len, line, n);
    text_len += n;
    text[text_len++] = '\n';
    text[text_len] = 0;
}

const lv_font_t* log_font() { return metrics().large ? &lv_font_montserrat_14 : &lv_font_montserrat_12; }

// Splits the log into pages that fill `height` at `width`. Pages are cut
// from the end, so the newest page (the one that opens) is full.
void paginate(int width, int height)
{
    free(starts);
    starts = nullptr;
    pages = 0;
    if (!text_len) return;
    int lines = 0;
    for (size_t k = 0; k < text_len; ++k) lines += text[k] == '\n';
    if (text[text_len - 1] != '\n') ++lines;
    // starts[] first holds every line's offset, heights[] its height
    starts = static_cast<int*>(malloc(sizeof(int) * lines));
    int16_t* heights = static_cast<int16_t*>(malloc(sizeof(int16_t) * lines));
    if (!starts || !heights) { free(heights); free(starts); starts = nullptr; return; }
    const lv_font_t* f = log_font();
    char line[200];
    int n_lines = 0;
    for (size_t pos = 0; pos < text_len && n_lines < lines;) {
        const char* nl = static_cast<const char*>(memchr(text + pos, '\n', text_len - pos));
        const size_t end = nl ? size_t(nl - text) : text_len;
        const size_t n = end - pos < sizeof line - 1 ? end - pos : sizeof line - 1;
        memcpy(line, text + pos, n);
        line[n] = 0;
        lv_point_t sz;
        lv_text_get_size(&sz, n ? line : " ", f, 0, 0, width, LV_TEXT_FLAG_NONE);
        starts[n_lines] = int(pos);
        heights[n_lines++] = int16_t(sz.y);
        pos = end + 1;
    }
    // Walk back from the newest line; keep the first line of each page
    int out = n_lines;                     // page starts collected from the top end down
    int used = 0;
    for (int k = n_lines - 1; k >= 0; --k) {
        if (used && used + heights[k] > height) { starts[--out] = starts[k + 1]; used = 0; }
        used += heights[k];
    }
    starts[--out] = 0;
    pages = n_lines - out;
    memmove(starts, starts + out, sizeof(int) * pages);
    free(heights);
}

void key_cb(lv_event_t* e);

lv_obj_t* key(lv_obj_t* r, const char* label, intptr_t id, bool grow, bool on)
{
    lv_obj_t* b = make_key(r, metrics().large ? 56 : 48, menu_btn_h(), on ? key_cb : nullptr, id);
    if (grow) lv_obj_set_flex_grow(b, 1);
    key_label(b, label, menu_font());
    if (!on) set_dim(b, true);
    return b;
}

lv_obj_t* key_row()
{
    lv_obj_t* r = lv_obj_create(overlay());
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, lv_pct(100), menu_btn_h());
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(r, 6, 0);
    lv_obj_set_scrollable(r, false);
    lv_obj_set_ignore_layout(r, true);
    return r;
}

void key_cb(lv_event_t* e)
{
    const Shell& H = shell();
    switch (reinterpret_cast<intptr_t>(lv_event_get_user_data(e))) {
        case kTouch: settings_open_touch_test(); break;
        case kLog:   status = nullptr; device_log_open(); break;
        case kBack:
            if (text) diagnostics_open();                    // from the log
            else settings_reopen();
            break;
        case kPrev:  device_log_open(page_now - 1); break;
        case kNext:  device_log_open(page_now + 1); break;
        case kClear:
            if (H.log_clear) H.log_clear();
            free_text();
            status = nullptr;
            device_log_open();
            break;
        case kCopy:
            status = H.log_copy_sd && H.log_copy_sd()
                         ? "Copied to the SD card: /CYD-Classic-Games/log.txt"
                         : "No SD card found.";
            device_log_open(page_now);
            break;
    }
}

} // namespace

void diagnostics_open()
{
    const Shell& H = shell();
    free_text();
    overlay_begin("Diagnostics");
    if (H.raw_touch)
        overlay_pair("Touch Test", key_cb, kTouch, H.log_read ? "Device Log" : nullptr, key_cb, kLog);
    else if (H.log_read)
        overlay_pair("Device Log", key_cb, kLog, nullptr, nullptr, 0);

    char info[200];
    int n = snprintf(info, sizeof info, "Board: %s\nFirmware: %s",
                     H.board_name ? H.board_name : "?", H.firmware_version ? H.firmware_version : "?");
    if (H.memory) {
        uint32_t fr = 0, big = 0;
        H.memory(&fr, &big);
        n += snprintf(info + n, sizeof info - n, "\nMemory: %lu KB free, largest block %lu KB",
                      (unsigned long)(fr / 1024), (unsigned long)(big / 1024));
    }
    const uint32_t s = lv_tick_get() / 1000;
    snprintf(info + n, sizeof info - n, "\nOn for %lu:%02lu:%02lu", (unsigned long)(s / 3600),
             (unsigned long)(s / 60 % 60), (unsigned long)(s % 60));
    overlay_text(info, false);
    overlay_text("The log records each start and any crash. It also goes to the "
                 "USB serial port (115200 baud).", true);
    overlay_bottom_button("Back", key_cb, kBack);
}

void device_log_open(int page)
{
    const Shell& H = shell();
    if (!text) {
        text_cap = 4096;
        text = static_cast<char*>(malloc(text_cap));
        if (!text) text_cap = 0;
        else text[0] = 0;
        text_len = 0;
        if (text && H.log_read) H.log_read(add_line, nullptr);
    }
    overlay_begin("Device Log", nullptr);
    const Metrics& M = metrics();

    lv_obj_t* sub = lv_label_create(overlay());
    lv_obj_set_style_text_font(sub, log_font(), 0);
    lv_obj_set_style_text_color(sub, pal().muted, 0);
    lv_label_set_long_mode(sub, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(sub, lv_pct(100));

    lv_obj_t* body = lv_label_create(overlay());
    lv_obj_set_style_text_font(body, log_font(), 0);
    lv_obj_set_style_text_color(body, pal().ink, 0);
    lv_label_set_long_mode(body, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(body, lv_pct(100));
    lv_label_set_text(body, "");

    // Bottom: [<] [Back] [>], above it [Clear Log] [Copy To SD]
    const int gap = M.large ? 10 : 6;
    lv_obj_t* nav = key_row();
    lv_obj_align(nav, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_t* acts = key_row();
    lv_obj_align_to(acts, nav, LV_ALIGN_OUT_TOP_MID, 0, -gap);
    key(acts, "Clear Log", kClear, true, H.log_clear != nullptr && text_len);
    if (H.log_copy_sd) key(acts, "Copy To SD", kCopy, true, text_len > 0);

    lv_label_set_text(sub, status ? status : "Page 1 / 1");
    lv_obj_update_layout(overlay());
    lv_area_t b, a;
    lv_obj_get_coords(body, &b);
    lv_obj_get_coords(acts, &a);
    paginate(lv_obj_get_content_width(overlay()), a.y1 - gap - b.y1);

    if (page < 0 || page >= pages) page = pages ? pages - 1 : 0;
    page_now = page;
    if (pages) {
        const int from = starts[page];
        int to = page + 1 < pages ? starts[page + 1] : int(text_len);
        if (to > from && text[to - 1] == '\n') --to;
        const char saved = text[to];
        text[to] = 0;
        lv_label_set_text(body, text + from);
        text[to] = saved;
    } else {
        lv_label_set_text(body, "The log is empty.");
    }
    if (!status) {
        char pg[48];
        snprintf(pg, sizeof pg, "Page %d / %d, newest at the end", page + 1, pages ? pages : 1);
        lv_label_set_text(sub, pg);
    }
    key(nav, LV_SYMBOL_LEFT, kPrev, false, page > 0);
    lv_obj_t* back = key(nav, "Back", kBack, true, true);
    lv_obj_add_state(back, LV_STATE_CHECKED);
    key(nav, LV_SYMBOL_RIGHT, kNext, false, page + 1 < pages);
}

} // namespace ui
