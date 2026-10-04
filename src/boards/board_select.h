// Selects the hardware definition for the board chosen in platformio.ini.
// This is the ONLY file that tests CYD_BOARD_* flags.
//
// Boards are named by what a user can identify: screen size, display driver,
// touch type. BOARD_NAME says the same in words (e.g. 3.2" ST7789 Resistive);
// firmware files are TTB-CYD-CG_<size>in_<DRIVER>_<touch>.bin (platformio.ini).
//
// Every board must end up defining:
//   class LGFX : public lgfx::LGFX_Device   display + touch + backlight
//   BOARD_NAME                              e.g. "3.2\" ST7789 Resistive"
//   BOARD_TOUCH_RESISTIVE                   1 = XPT2046 (calibrate, no gestures)
//   BOARD_PIN_BOOT_BTN                      held at power-up to recalibrate touch
//   BOARD_SD_USABLE                         1 = SD slot works (needs BOARD_PIN_SD_*)
//   BOARD_PORTRAIT_W                        panel width in portrait: 240 or 320
//                                           (picks the splash images to build in)
// Optional: BOARD_PIN_LED_*, BOARD_LED_ACTIVE_LOW, BOARD_PIN_LDR, BOARD_PIN_SPEAKER,
//           BOARD_PIN_SD_CS/SCK/MISO/MOSI, BOARD_PIN_AUDIO_EN, BOARD_PIN_BATTERY_ADC
//
// Colors: each board's default inversion is a best guess. If a particular
// unit shows a negative image or swapped red/blue, fix it on the device
// (Settings > Invert Colors / Swap Red/Blue, saved to flash) - no rebuild needed.
#pragma once

// Display SPI clock. 40 MHz is safe on every board; many run at 55-80 MHz.
#ifndef CYD_SPI_WRITE_HZ
#define CYD_SPI_WRITE_HZ 40000000
#endif

// ---- 2.8" ESP32-2432S028 (the original CYD layout) ---------------------------
#if defined(CYD_BOARD_28_ILI9341_RES)
  #include "esp32_2432s028.hpp"
  #define BOARD_NAME "2.8\" ILI9341 Resistive"  // not yet tested on hardware
  #define BOARD_PORTRAIT_W 240
  class LGFX : public LGFX_Esp32_2432S028<lgfx::Panel_ILI9341, false> {};

#elif defined(CYD_BOARD_28_ST7789_RES)
  #include "esp32_2432s028.hpp"
  #define BOARD_NAME "2.8\" ST7789 Resistive"   // confirmed on hardware
  #define BOARD_PORTRAIT_W 240
  class LGFX : public LGFX_Esp32_2432S028<lgfx::Panel_ST7789, false> {};

// ---- "ESP32-32E" display boards ---------------------------------------------
#elif defined(CYD_BOARD_32_ST7789_RES)
  #include "esp32_32e_display.hpp"
  #define BOARD_NAME "3.2\" ST7789 Resistive"
  #define BOARD_PORTRAIT_W 240
  // IPS panel: inversion on, RGB order (both confirmed on hardware)
  class LGFX : public LGFX_Esp32_32E<lgfx::Panel_ST7789P3, 240, 320, true, true> {};

#elif defined(CYD_BOARD_35_ST7796_RES)
  #include "esp32_32e_display.hpp"
  #define BOARD_NAME "3.5\" ST7796 Resistive"
  #define BOARD_PORTRAIT_W 320
  // Not yet tested on hardware; same settings as the confirmed 4.0"
  class LGFX : public LGFX_Esp32_32E<lgfx::Panel_ST7796, 320, 480, false, false> {};

#elif defined(CYD_BOARD_40_ST7796_RES)
  #include "esp32_32e_display.hpp"
  #define BOARD_NAME "4.0\" ST7796 Resistive"
  #define BOARD_PORTRAIT_W 320
  // Confirmed on hardware with these defaults
  class LGFX : public LGFX_Esp32_32E<lgfx::Panel_ST7796, 320, 480, false, false> {};

#elif defined(CYD_BOARD_NM_CYD_C5)
  #error "NM-CYD-C5 is listed in platformio.ini but its board file is not written yet."

#else
  #error "No CYD_BOARD_* flag set. Build one of the [env:...] targets in platformio.ini."
#endif
