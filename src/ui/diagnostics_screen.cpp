// Diagnostics (Settings -> Diagnostics): board facts, the touch test, the
// device log (paged with < > keys, no scrolling) and Send Log: the log,
// compressed into a QR code a phone camera opens as a page that emails it
// (log_pack.h, web/l/index.html).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <lvgl.h>
#include "log_pack.h"
#include "shell.h"
#include "src/libs/qrcode/qrcodegen.h"
#include "theme.h"
#include "widgets.h"

namespace ui {

namespace {

enum : intptr_t { kTouch = 1, kLog, kBack, kPrev, kNext, kClear, kCopy, kSend, kRecal };

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

void load_text()
{
    if (text) return;
    text_cap = 4096;
    text = static_cast<char*>(malloc(text_cap));
    if (!text) text_cap = 0;
    else text[0] = 0;
    text_len = 0;
    if (text && shell().log_read) shell().log_read(add_line, nullptr);
}

// ---- Send Log: the QR code ------------------------------------------------------------
uint8_t* qr = nullptr;                     // qrcodegen symbol while the screen is up
int      qr_mod = 2;                       // pixels per module

void free_qr() { free(qr); qr = nullptr; }

// Dark modules as horizontal runs on a white square with a 2-module margin
void qr_draw_cb(lv_event_t* e)
{
    if (!qr) return;
    lv_area_t o;
    lv_obj_get_coords(lv_event_get_target_obj(e), &o);
    const int n = qrcodegen_getSize(qr), m = qr_mod;
    const int side = (n + 4) * m;                  // centred in the full-width object
    lv_area_t a{o.x1 + (lv_area_get_width(&o) - side) / 2, o.y1, 0, o.y1 + side - 1};
    a.x2 = a.x1 + side - 1;
    lv_layer_t* layer = lv_event_get_layer(e);
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = lv_color_white();
    lv_draw_rect(layer, &d, &a);
    d.bg_color = lv_color_black();
    const int x0 = a.x1 + 2 * m, y0 = a.y1 + 2 * m;
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n;) {
            if (!qrcodegen_getModule(qr, x, y)) { ++x; continue; }
            int end = x;
            while (end < n && qrcodegen_getModule(qr, end, y)) ++end;
            lv_area_t r{x0 + x * m, y0 + y * m, x0 + end * m - 1, y0 + (y + 1) * m - 1};
            lv_draw_rect(layer, &d, &r);
            x = end;
        }
}

// Encodes URL + packed text as the smallest QR code up to `maxv`; false if
// it doesn't fit. `out` (qrcode) and `tmp` hold BUFFER_LEN_FOR_VERSION(maxv).
bool encode(const char* report, size_t n, int maxv, bool final_mask, uint8_t* out, uint8_t* tmp)
{
    const size_t work_cap = n + 64, b43_cap = (n + 64) * 3 / 2 + 4;
    uint8_t* work = static_cast<uint8_t*>(malloc(work_cap));
    char* b43 = static_cast<char*>(malloc(b43_cap));
    bool ok = false;
    const size_t len = work && b43 ? logpack::pack(report, n, work, work_cap, b43, b43_cap) : 0;
    if (len) {
        const size_t url_n = strlen(logpack::kUrl);
        uint8_t* ub = static_cast<uint8_t*>(malloc(qrcodegen_calcSegmentBufferSize(qrcodegen_Mode_BYTE, url_n)));
        uint8_t* ab = static_cast<uint8_t*>(malloc(qrcodegen_calcSegmentBufferSize(qrcodegen_Mode_ALPHANUMERIC, len)));
        if (ub && ab) {
            qrcodegen_Segment segs[2] = {
                qrcodegen_makeBytes(reinterpret_cast<const uint8_t*>(logpack::kUrl), url_n, ub),
                qrcodegen_makeAlphanumeric(b43, ab)};
            ok = qrcodegen_encodeSegmentsAdvanced(segs, 2, qrcodegen_Ecc_LOW, qrcodegen_VERSION_MIN, maxv,
                                                  final_mask ? qrcodegen_Mask_AUTO : qrcodegen_Mask_0,
                                                  final_mask, tmp, out);
        }
        free(ub);
        free(ab);
    }
    free(work);
    free(b43);
    return ok;
}

// The report: a short header, then the newest `lines` lines of the log
size_t build_report(char* out, size_t cap, int lines, int total)
{
    const Shell& H = shell();
    int n = snprintf(out, cap, "CYD Classic Games log\nBoard: %s\nFirmware: %s%s%s%s\nLines: newest %d of %d\n",
                     H.board_name ? H.board_name : "?", H.firmware_version ? H.firmware_version : "?",
                     H.firmware_build && *H.firmware_build ? " (" : "", H.firmware_build ? H.firmware_build : "",
                     H.firmware_build && *H.firmware_build ? ")" : "", lines, total);
    if (n < 0 || size_t(n) >= cap) return 0;
    // Start of the newest `lines` lines (each line ends with '\n')
    size_t from = text_len;
    if (lines > 0) {
        from = 0;
        int seen = 0;
        for (size_t k = text_len - 1; k > 0; --k)
            if (text[k - 1] == '\n' && ++seen == lines) { from = k; break; }
    }
    const size_t len = text_len - from;
    if (size_t(n) + len + 1 > cap) return 0;
    memcpy(out + n, text + from, len);
    out[n + len] = 0;
    return n + len;
}

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
        case kSend:  send_log_open(); break;
        case kRecal:
            app_save_current();
            if (H.recalibrate_touch) H.recalibrate_touch();
            break;
        case kBack:
            if (text) diagnostics_open();                    // from the log
            else settings_reopen();
            break;
        case kPrev:  status = nullptr; device_log_open(page_now - 1); break;
        case kNext:  status = nullptr; device_log_open(page_now + 1); break;
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
    // Touch: test and recalibrate side by side; then the log
    overlay_pair(H.raw_touch ? "Touch Test" : nullptr, key_cb, kTouch, "Recalibrate", key_cb, kRecal);
    if (H.log_read) overlay_pair("Device Log", key_cb, kLog, "Send Log", key_cb, kSend);

    char info[200];
    int n = snprintf(info, sizeof info, "Board: %s\nFirmware: %s%s%s%s",
                     H.board_name ? H.board_name : "?", H.firmware_version ? H.firmware_version : "?",
                     H.firmware_build && *H.firmware_build ? " (" : "",
                     H.firmware_build ? H.firmware_build : "",
                     H.firmware_build && *H.firmware_build ? ")" : "");
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
    overlay_text("The log records each start and any crash. Send Log shows it "
                 "as a QR code: a phone camera opens it, ready to email.", true);
    overlay_bottom_button("Back", key_cb, kBack);
}

void device_log_open(int page)
{
    const Shell& H = shell();
    load_text();
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

namespace ui {

void send_log_open()
{
    const Shell& H = shell();
    free_qr();
    load_text();
    if (H.log_copy_sd) H.log_copy_sd();    // a copy on the SD card too, when there is one
    overlay_begin("Send Log", free_qr);
    const Metrics& M = metrics();

    // Largest code that keeps modules >= 2 px across the screen (with the
    // 2-module margin); short logs get a smaller code with bigger modules
    // Room: the width, and the height between the title and the caption +
    // Back key
    const int gap = M.large ? 10 : 6;
    const int avail_w = M.w - 2 * (M.large ? 16 : 10);
    const int avail_h = M.h - 2 * (M.large ? 16 : 10) - lv_font_get_line_height(title_font())
                      - lv_font_get_line_height(&lv_font_montserrat_14) - menu_btn_h() - 3 * gap;
    const int avail = avail_w < avail_h ? avail_w : avail_h;
    int maxv = (avail / 2 - 4 - 17) / 4;
    if (maxv > qrcodegen_VERSION_MAX) maxv = qrcodegen_VERSION_MAX;
    const size_t qlen = qrcodegen_BUFFER_LEN_FOR_VERSION(maxv);
    qr = static_cast<uint8_t*>(malloc(qlen));
    uint8_t* tmp = static_cast<uint8_t*>(malloc(qlen));
    const size_t rcap = text_len + 256;
    char* report = static_cast<char*>(malloc(rcap));

    int total = 0;
    for (size_t k = 0; k < text_len; ++k) total += text[k] == '\n';
    // As many of the newest lines as fit (binary search on the line count)
    int lo = 0, hi = total, best = -1;
    if (qr && tmp && report) {
        while (lo <= hi) {
            const int mid = (lo + hi) / 2;
            const size_t n = build_report(report, rcap, mid, total);
            const bool fit = n && encode(report, n, maxv, false, qr, tmp);
            if (fit) { best = mid; lo = mid + 1; }
            else hi = mid - 1;
        }
    }
    bool ok = false;
    if (best >= 0) {
        const size_t n = build_report(report, rcap, best, total);
        ok = n && encode(report, n, maxv, true, qr, tmp);
    }
    free(tmp);
    free(report);
    if (!ok) free_qr();

    if (qr) {
        const int size = qrcodegen_getSize(qr) + 4;
        qr_mod = avail / size;
        if (qr_mod < 1) qr_mod = 1;
        lv_obj_t* o = lv_obj_create(overlay());
        lv_obj_remove_style_all(o);
        lv_obj_set_size(o, lv_pct(100), size * qr_mod);
        lv_obj_add_event_cb(o, qr_draw_cb, LV_EVENT_DRAW_MAIN, nullptr);
        char info[64];
        snprintf(info, sizeof info, "Newest %d of %d lines", best, total);
        lv_obj_t* cap = overlay_text(info, true);
        lv_obj_set_style_text_font(cap, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_align(cap, LV_TEXT_ALIGN_CENTER, 0);
    } else {
        overlay_text("The log could not be packed into a QR code (out of memory?). "
                     "Use the web flasher page's Read Log over USB instead.", false);
    }
    overlay_bottom_button("Back", key_cb, kBack);
}

} // namespace ui
