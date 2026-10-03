#include "sdcard.h"

#include <Arduino.h>
#include "boards/board_select.h"

#if BOARD_SD_USABLE

#include <SD.h>
#include <SPI.h>

namespace {
SPIClass sd_spi(VSPI);
bool     mounted = false;
bool     spi_started = false;
}

bool sd_begin()
{
    if (mounted) return true;
    if (!spi_started) {
        sd_spi.begin(BOARD_PIN_SD_SCK, BOARD_PIN_SD_MISO, BOARD_PIN_SD_MOSI, BOARD_PIN_SD_CS);
        spi_started = true;
    }
    mounted = SD.begin(BOARD_PIN_SD_CS, sd_spi, 20000000);
    if (mounted) {
        Serial.printf("[sd] card mounted, %llu MB\n", (unsigned long long)(SD.cardSize() >> 20));
    }
    return mounted;
}

fs::FS& sd_fs() { return SD; }

void sd_lost()
{
    if (!mounted) return;
    SD.end();
    mounted = false;
    Serial.println("[sd] card unavailable");
}

#else  // no usable SD slot on this board

#include <LittleFS.h>

bool sd_begin() { return false; }
fs::FS& sd_fs() { return LittleFS; }   // never used: sd_begin() is false
void sd_lost() {}

#endif
