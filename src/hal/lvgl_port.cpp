#include "lvgl_port.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include "panel_prefs.h"
#include "touch_cal.h"

namespace {

LGFX gfx;
lv_display_t* disp = nullptr;

// ---- Display flush --------------------------------------------------------
// Two partial draw buffers in DMA-capable RAM: LVGL renders into one while
// the other is being sent to the panel. Before starting a new transfer we wait
// for the previous one, so a buffer is never overwritten mid-DMA.
void flush_cb(lv_display_t* d, const lv_area_t* area, uint8_t* px_map)
{
    const uint32_t w = lv_area_get_width(area);
    const uint32_t h = lv_area_get_height(area);
    gfx.waitDMA();
    gfx.pushImageDMA(area->x1, area->y1, w, h,
                     reinterpret_cast<lgfx::swap565_t*>(px_map));
    lv_display_flush_ready(d);
}

// ---- Touch ---------------------------------------------------------------
// LovyanGFX already applies calibration and the current rotation, so the
// coordinates go straight to LVGL (LVGL's own rotation is left at 0).
//
// Resistive panels read wrong at the instant of contact: the layers are only
// lightly touching and the reading is pulled off target. LVGL acts on the
// first PRESSED point, so that bad reading would pick the wrong cell or
// button. So a press only starts once two consecutive readings agree, and a
// release needs two consecutive "no touch" readings (resistive panels drop
// out for a frame mid-press, which would otherwise look like a second tap).
constexpr int kAgreePx   = 8;   // two readings this close = steady
constexpr int kReleaseN  = 2;   // empty readings needed to end a press

struct TouchFilter {
    bool    down = false;
    bool    have_candidate = false;
    int16_t cx = 0, cy = 0;     // candidate first reading
    int16_t x = 0, y = 0;       // last reported point
    uint8_t empty = 0;
} tf;

void touch_read_cb(lv_indev_t*, lv_indev_data_t* data)
{
    // On boards where touch shares the display's SPI bus, the bus must be
    // idle before LovyanGFX briefly hands it to the touch controller.
    gfx.waitDMA();
    lgfx::touch_point_t tp;
    const bool raw = gfx.getTouch(&tp, 1);

#ifdef CYD_TOUCH_DEBUG
    if (raw) Serial.printf("[touch] %d,%d size %d\n", tp.x, tp.y, tp.size);
#endif

#if BOARD_TOUCH_RESISTIVE
    if (raw) {
        tf.empty = 0;
        if (tf.down) {
            tf.x = tp.x;
            tf.y = tp.y;
        } else if (tf.have_candidate && abs(tp.x - tf.cx) <= kAgreePx
                                     && abs(tp.y - tf.cy) <= kAgreePx) {
            tf.down = true;                    // steady: start the press here
            tf.x = tp.x;
            tf.y = tp.y;
        } else {
            tf.have_candidate = true;          // first (or unsteady) reading
            tf.cx = tp.x;
            tf.cy = tp.y;
        }
    } else {
        tf.have_candidate = false;
        if (tf.down && ++tf.empty >= kReleaseN) tf.down = false;
    }
    data->point.x = tf.x;
    data->point.y = tf.y;
    data->state   = tf.down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
#else
    if (raw) {
        data->point.x = tp.x;
        data->point.y = tp.y;
        data->state   = LV_INDEV_STATE_PRESSED;
    } else {
        data->state   = LV_INDEV_STATE_RELEASED;
    }
#endif
}

uint32_t tick_cb() { return millis(); }

void log_cb(lv_log_level_t, const char* msg) { Serial.print(msg); }

} // namespace

LGFX& lvgl_port_gfx() { return gfx; }

void lvgl_port_set_brightness(uint8_t level) { gfx.setBrightness(level); }

void lvgl_port_set_rotation(uint8_t rotation)
{
    gfx.waitDMA();
    gfx.setRotation(rotation);       // touch follows: LovyanGFX maps it per rotation
    if (disp) {
        lv_obj_invalidate(lv_screen_active());
        lv_obj_invalidate(lv_layer_top());
    }
}

bool lvgl_port_raw_touch(int16_t* x, int16_t* y)
{
    gfx.waitDMA();
    lgfx::touch_point_t tp;
    if (!gfx.getTouch(&tp, 1)) return false;
    *x = tp.x;
    *y = tp.y;
    return true;
}

lv_display_t* lvgl_port_init(uint8_t rotation)
{
    gfx.init();
    gfx.initDMA();
    gfx.setRotation(rotation);
    gfx.setBrightness(200);
    panel_prefs_begin(gfx);
    gfx.fillScreen(TFT_BLACK);

    // Uses plain LovyanGFX drawing, so it must run before LVGL owns the bus.
    touch_cal_begin(gfx);

    lv_init();
    lv_tick_set_cb(tick_cb);
    lv_log_register_print_cb(log_cb);

    const int32_t w = gfx.width();
    const int32_t h = gfx.height();

    // 1/10 of the screen per buffer: 15 KB each at 320x240, 30 KB at 480x320.
    const uint32_t buf_bytes = w * (h / 10) * sizeof(uint16_t);
    void* buf1 = heap_caps_malloc(buf_bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    void* buf2 = heap_caps_malloc(buf_bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!buf1 || !buf2) {
        Serial.printf("[lvgl_port] draw buffer alloc failed (%lu bytes each)\n", (unsigned long)buf_bytes);
        return nullptr;
    }

    disp = lv_display_create(w, h);
    // LVGL renders RGB565 with bytes pre-swapped, which is the order the SPI
    // panel expects, so the flush is a straight DMA copy.
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565_SWAPPED);
    lv_display_set_flush_cb(disp, flush_cb);
    lv_display_set_buffers(disp, buf1, buf2, buf_bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);

    lv_indev_t* touch = lv_indev_create();
    lv_indev_set_type(touch, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(touch, touch_read_cb);
    lv_indev_set_display(touch, disp);

    // Keep the display SPI transaction open between flushes. On boards where
    // touch shares the bus, LovyanGFX ends/resumes it around each touch read.
    gfx.startWrite();

    Serial.printf("[lvgl_port] %s  %ldx%ld  rot %u  buffers 2x%lu B  free heap %lu\n",
                  BOARD_NAME, (long)w, (long)h, rotation, (unsigned long)buf_bytes,
                  (unsigned long)ESP.getFreeHeap());
    return disp;
}

uint32_t lvgl_port_loop()
{
    return lv_timer_handler();
}
