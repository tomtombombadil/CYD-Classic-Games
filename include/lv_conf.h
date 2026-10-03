/**
 * LVGL configuration for CYD Classic Games (LVGL 9.6).
 *
 * Only settings that differ from LVGL's defaults are listed here; everything
 * else falls back to lv_conf_internal.h. The full list of options with
 * descriptions is in .pio/libdeps/<env>/lvgl/lv_conf_template.h.
 */
#ifndef LV_CONF_H
#define LV_CONF_H

/* ---- Color and memory ---------------------------------------------------- */
#define LV_COLOR_FORMAT_DEFAULT LV_COLOR_FORMAT_RGB565  /* 16-bit: what every CYD panel takes */

/* On the device LVGL allocates from the ESP32 heap (malloc). A fixed pool
 * (LV_STDLIB_BUILTIN) has to fit in the static DRAM region, which overflowed
 * once the pool needed to grow past 64 KB (the Stats screen ran it out).
 * The PC preview (tools/preview, -DCYD_PREVIEW) keeps a fixed pool so it can
 * measure how much each screen uses. */
#ifdef CYD_PREVIEW
#define LV_USE_STDLIB_MALLOC LV_STDLIB_BUILTIN
#define LV_MEM_SIZE (88 * 1024U)
#else
#define LV_USE_STDLIB_MALLOC LV_STDLIB_CLIB
#endif

/* Only formats we actually render; trims flash. */
#define LV_DRAW_SW_SUPPORT_RGB565          1
#define LV_DRAW_SW_SUPPORT_RGB565_SWAPPED  1
#define LV_DRAW_SW_SUPPORT_RGB565A8        1
#define LV_DRAW_SW_SUPPORT_RGB888          0
#define LV_DRAW_SW_SUPPORT_XRGB8888        0
#define LV_DRAW_SW_SUPPORT_ARGB8888        1   /* needed for anti-aliased images/fonts blending */
#define LV_DRAW_SW_SUPPORT_ARGB8888_PREMULTIPLIED 0
#define LV_DRAW_SW_SUPPORT_L8              0
#define LV_DRAW_SW_SUPPORT_AL88            0
#define LV_DRAW_SW_SUPPORT_A8              1
#define LV_DRAW_SW_SUPPORT_I1              0

/* ---- Timing -------------------------------------------------------------- */
#define LV_DEF_REFR_PERIOD 16             /* ms between screen refreshes (~60 fps cap) */

/* ---- Logging (Serial, 115200) ------------------------------------------- */
#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF 0                   /* routed through lv_log_register_print_cb in lvgl_port.cpp */

/* ---- Fonts --------------------------------------------------------------- */
/*  8: pencil marks on 240-px-wide screens
 * 10: pencil marks on 320-px-wide screens
 * 12: "remaining" counts on digit keys (large screens)
 * 14: small labels, tool buttons on small screens
 * 20: digits on small screens, buttons and menus
 * 28: digits on large screens, titles */
#define LV_FONT_MONTSERRAT_8  1
#define LV_FONT_MONTSERRAT_10 1
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_DEFAULT LV_FONT_DEFAULT_MONTSERRAT_20

/* ---- Widgets used besides the defaults ----------------------------------- */
#define LV_USE_BAR    1                   /* needed by the slider */
#define LV_USE_SLIDER 1                   /* brightness */

/* ---- Features not used --------------------------------------------------- */
#define LV_USE_LOVYAN_GFX 0               /* we use our own glue in lvgl_port.cpp */
#define LV_BUILD_EXAMPLES 0
#define LV_BUILD_DEMOS    0

#endif /* LV_CONF_H */
