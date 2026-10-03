// 2.8" "Cheap Yellow Display": silkscreen ESP32-2432S028 (sometimes with R).
// Two panel versions exist on the same PCB layout and pinout:
//   - ILI9341 (original, single micro-USB)
//   - ST7789  (micro-USB + USB-C, and some later single-USB runs)
//
// Pin reference: witnessmenow/ESP32-Cheap-Yellow-Display, PINS.md
//   Display on HSPI : SCLK 14, MOSI 13, MISO 12, CS 15, DC 2, RST = EN
//   Backlight       : GPIO 21 (PWM)
//   Touch XPT2046   : own pins on VSPI: SCLK 25, MOSI 32, MISO 39, CS 33, IRQ 36
//   SD card (VSPI)  : SCK 18, MISO 19, MOSI 23, CS 5   (conflicts with touch's VSPI use)
//   RGB LED (active low) : R 4, G 16, B 17
//   LDR 34, speaker amp 26, BOOT button 0
//
// Do not include directly - include boards/board_select.h.
#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#define BOARD_TOUCH_RESISTIVE 1
#define BOARD_PIN_BOOT_BTN    0
#define BOARD_PIN_LED_R       4
#define BOARD_PIN_LED_G       16
#define BOARD_PIN_LED_B       17
#define BOARD_LED_ACTIVE_LOW  1
#define BOARD_PIN_LDR         34
#define BOARD_PIN_SPEAKER     26   // speaker connector (through the amp)
#define BOARD_PIN_SD_CS       5
// The SD slot (VSPI pins 18/19/23/5) can't be used yet: touch also runs on
// the VSPI controller (pins 25/32/39/33) and LovyanGFX's XPT2046 driver has
// no software-SPI option. Needs a bit-banged touch driver first.
#define BOARD_SD_USABLE       0

template <class PanelT, bool kInvert>
class LGFX_Esp32_2432S028 : public lgfx::LGFX_Device
{
    PanelT              _panel;
    lgfx::Bus_SPI       _bus;
    lgfx::Light_PWM     _light;
    lgfx::Touch_XPT2046 _touch;

public:
    LGFX_Esp32_2432S028()
    {
        {   // Display bus: HSPI, display only
            auto cfg = _bus.config();
            cfg.spi_host    = HSPI_HOST;
            cfg.spi_mode    = 0;
            cfg.freq_write  = CYD_SPI_WRITE_HZ;
            cfg.freq_read   = 16000000;
            cfg.spi_3wire   = false;
            cfg.use_lock    = true;
            cfg.dma_channel = SPI_DMA_CH_AUTO;
            cfg.pin_sclk    = 14;
            cfg.pin_mosi    = 13;
            cfg.pin_miso    = 12;
            cfg.pin_dc      = 2;
            _bus.config(cfg);
            _panel.setBus(&_bus);
        }
        {   // Panel: native portrait 240x320
            auto cfg = _panel.config();
            cfg.pin_cs           = 15;
            cfg.pin_rst          = -1;   // tied to EN
            cfg.pin_busy         = -1;
            cfg.panel_width      = 240;
            cfg.panel_height     = 320;
            cfg.memory_width     = 240;
            cfg.memory_height    = 320;
            cfg.offset_x         = 0;
            cfg.offset_y         = 0;
            cfg.offset_rotation  = 0;
            cfg.readable         = true;
            cfg.invert           = kInvert;
            cfg.rgb_order        = false;  // false = BGR in LovyanGFX
            cfg.dlen_16bit       = false;
            cfg.bus_shared       = false;
            _panel.config(cfg);
        }
        {
            auto cfg = _light.config();
            cfg.pin_bl      = 21;
            cfg.invert      = false;
            cfg.freq        = 12000;
            cfg.pwm_channel = 7;
            _light.config(cfg);
            _panel.setLight(&_light);
        }
        {   // Touch on its own pins (VSPI). Raw limits are only a starting
            // point; the saved calibration replaces them.
            auto cfg = _touch.config();
            cfg.x_min           = 300;
            cfg.x_max           = 3900;
            cfg.y_min           = 200;
            cfg.y_max           = 3750;
            cfg.pin_int         = 36;
            cfg.bus_shared      = false;
            cfg.offset_rotation = 0;
            cfg.spi_host        = VSPI_HOST;
            cfg.freq            = 1000000;
            cfg.pin_sclk        = 25;
            cfg.pin_mosi        = 32;
            cfg.pin_miso        = 39;
            cfg.pin_cs          = 33;
            _touch.config(cfg);
            _panel.setTouch(&_touch);
        }
        setPanel(&_panel);
    }
};
