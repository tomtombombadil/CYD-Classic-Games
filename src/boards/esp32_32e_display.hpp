// "ESP32-32E" display boards. The PCB has no model number, only silkscreen
// text like: "3.2" LCD Display, ESP32-32E, 240x320, Resistive Touch".
//   3.2"  ST7789P3  240x320  (IPS)   resistive
//   3.5"  ST7796U   320x480          resistive
//   4.0"  ST7796S   320x480          resistive
//
// Pin reference: the boards' datasheets on lcdwiki.com (an information site,
// not the maker). Pins are identical across the family:
//   Display on HSPI : SCLK 14, MOSI 13, MISO 12, CS 15, DC 2, RST = EN
//   Backlight       : GPIO 27 (high = on)
//   Touch XPT2046   : SHARES the display's SPI bus, CS 33, IRQ 36
//   SD card on VSPI : SCK 18, MISO 19, MOSI 23, CS 5   (independent of display/touch)
//   RGB LED (common anode, low = on) : R 22, G 16, B 17
//   Audio amp enable 4 (low = on), DAC out 26
//   Battery voltage ADC 34, BOOT button 0
//
// Do not include directly - include boards/board_select.h.
#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#define BOARD_TOUCH_RESISTIVE   1
#define BOARD_PIN_BOOT_BTN      0
#define BOARD_PIN_LED_R         22
#define BOARD_PIN_LED_G         16
#define BOARD_PIN_LED_B         17
#define BOARD_LED_ACTIVE_LOW    1
#define BOARD_PIN_SD_CS         5
#define BOARD_PIN_SD_SCK        18
#define BOARD_PIN_SD_MISO       19
#define BOARD_PIN_SD_MOSI       23
#define BOARD_SD_USABLE         1    // SD has VSPI to itself here
#define BOARD_PIN_AUDIO_EN      4    // low = amplifier on
#define BOARD_PIN_SPEAKER     26   // speaker connector (through the amp)
#define BOARD_PIN_BATTERY_ADC   34

// kRgbOrder: LovyanGFX rgb_order (false = BGR, true = RGB)
template <class PanelT, int kWidth, int kHeight, bool kInvert, bool kRgbOrder>
class LGFX_Esp32_32E : public lgfx::LGFX_Device
{
    PanelT              _panel;
    lgfx::Bus_SPI       _bus;
    lgfx::Light_PWM     _light;
    lgfx::Touch_XPT2046 _touch;

public:
    LGFX_Esp32_32E()
    {
        {   // HSPI, shared by the display and the touch controller
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
        {   // Panel: native portrait
            auto cfg = _panel.config();
            cfg.pin_cs           = 15;
            cfg.pin_rst          = -1;   // tied to EN
            cfg.pin_busy         = -1;
            cfg.panel_width      = kWidth;
            cfg.panel_height     = kHeight;
            cfg.memory_width     = kWidth;
            cfg.memory_height    = kHeight;
            cfg.offset_x         = 0;
            cfg.offset_y         = 0;
            cfg.offset_rotation  = 0;
            cfg.readable         = true;
            cfg.invert           = kInvert;
            cfg.rgb_order        = kRgbOrder;
            cfg.dlen_16bit       = false;
            cfg.bus_shared       = true;   // touch is on this bus too
            _panel.config(cfg);
        }
        {
            auto cfg = _light.config();
            cfg.pin_bl      = 27;
            cfg.invert      = false;
            cfg.freq        = 12000;
            cfg.pwm_channel = 7;
            _light.config(cfg);
            _panel.setLight(&_light);
        }
        {   // Touch on the display's bus. LovyanGFX pauses the display
            // transaction around each touch read (bus_shared = true).
            auto cfg = _touch.config();
            cfg.x_min           = 300;
            cfg.x_max           = 3900;
            cfg.y_min           = 200;
            cfg.y_max           = 3750;
            cfg.pin_int         = 36;
            cfg.bus_shared      = true;
            cfg.offset_rotation = 0;
            cfg.spi_host        = HSPI_HOST;
            // 1 MHz, not the XPT2046's 2.5 MHz max: the larger panels need
            // the extra settling time per sample for accurate readings.
            cfg.freq            = 1000000;
            cfg.pin_sclk        = 14;
            cfg.pin_mosi        = 13;
            cfg.pin_miso        = 12;
            cfg.pin_cs          = 33;
            _touch.config(cfg);
            _panel.setTouch(&_touch);
        }
        setPanel(&_panel);
    }
};
