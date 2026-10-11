// ST7735S 128x160 SPI TFT on ESP32 WROOM-32 (Arduino-ESP32 core).
// Ported from the AVR version: the HAL was rewritten for hardware VSPI,
// the driver logic is unchanged.

#include <Arduino.h>
#include <SPI.h>
#include "image_data.h"

#define TFT_WIDTH  128
#define TFT_HEIGHT 160

#define COLOR_BLACK 0x0000

#define ST7735_SWRESET 0x01
#define ST7735_SLPOUT  0x11
#define ST7735_DISPON  0x29
#define ST7735_CASET   0x2A
#define ST7735_RASET   0x2B
#define ST7735_RAMWR   0x2C
#define ST7735_COLMOD  0x3A
#define ST7735_MADCTL  0x36

// =====================================================================
// Pinout. SCK and MOSI are fixed hardware VSPI pins; CS/DC/RST can be
// moved to any free GPIO.
//   SCK  -> GPIO18
//   MOSI -> GPIO23
// =====================================================================
#define TFT_CS   5
#define TFT_DC   2
#define TFT_RST  4

// 10 MHz is verified for this display and is enough for a static image
// (a full-screen blit takes ~30 ms). Try 27000000/40000000 for more
// speed; step back down on signal integrity issues.
#define SPI_FREQ 10000000

// =====================================================================
// 1. HAL
// =====================================================================

void hal_init() {
    SPI.begin();  // default VSPI: SCK=18, MOSI=23 (MISO unused)
    SPI.beginTransaction(SPISettings(SPI_FREQ, MSBFIRST, SPI_MODE0));

    pinMode(TFT_DC, OUTPUT);
    pinMode(TFT_RST, OUTPUT);
    pinMode(TFT_CS, OUTPUT);
    digitalWrite(TFT_CS, HIGH);
}

static inline void hal_set_dc(uint8_t is_data) {
    digitalWrite(TFT_DC, is_data ? HIGH : LOW);
}

static inline void hal_set_rst(uint8_t is_high) {
    digitalWrite(TFT_RST, is_high ? HIGH : LOW);
}

static inline void hal_set_cs(uint8_t is_active) {
    digitalWrite(TFT_CS, is_active ? LOW : HIGH);
}

static inline void hal_spi_write(uint8_t data) {
    SPI.transfer(data);
}

// =====================================================================
// 2. ST7735S driver
// =====================================================================

void tft_write_command(uint8_t cmd) {
    hal_set_dc(0);
    hal_set_cs(1);
    hal_spi_write(cmd);
    hal_set_cs(0);
}

void tft_write_data(uint8_t data) {
    hal_set_dc(1);
    hal_set_cs(1);
    hal_spi_write(data);
    hal_set_cs(0);
}

void tft_init() {
    hal_init();

    hal_set_rst(1); delay(5);
    hal_set_rst(0); delay(10);
    hal_set_rst(1); delay(120);

    tft_write_command(ST7735_SWRESET); delay(150);
    tft_write_command(ST7735_SLPOUT);  delay(200);

    tft_write_command(ST7735_COLMOD);  tft_write_data(0x05);   delay(10);
    tft_write_command(ST7735_MADCTL);  tft_write_data(0x00);   delay(10);
    tft_write_command(ST7735_DISPON);  delay(100);
}

void tft_set_window(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1) {
    tft_write_command(ST7735_CASET);
    tft_write_data(0x00); tft_write_data(x0);
    tft_write_data(0x00); tft_write_data(x1);

    tft_write_command(ST7735_RASET);
    tft_write_data(0x00); tft_write_data(y0);
    tft_write_data(0x00); tft_write_data(y1);

    tft_write_command(ST7735_RAMWR);
}

// =====================================================================
// 3. Graphics primitives
// =====================================================================

void tft_fill_screen(uint16_t color) {
    uint8_t row[TFT_WIDTH * 2];
    for (uint8_t i = 0; i < TFT_WIDTH; i++) {
        row[i * 2]     = color >> 8;
        row[i * 2 + 1] = color & 0xFF;
    }
    tft_set_window(0, 0, TFT_WIDTH - 1, TFT_HEIGHT - 1);
    hal_set_dc(1);
    hal_set_cs(1);
    for (uint8_t y = 0; y < TFT_HEIGHT; y++) {
        SPI.writeBytes(row, sizeof(row));
    }
    hal_set_cs(0);
}

void tft_draw_bitmap(uint8_t x, uint8_t y, uint8_t w, uint8_t h, const uint8_t *bitmap) {
    tft_set_window(x, y, x + w - 1, y + h - 1);
    hal_set_dc(1);
    hal_set_cs(1);
    // const arrays live in flash on ESP32 and can be read directly;
    // a single block write keeps VSPI at full speed.
    SPI.writeBytes(bitmap, (uint32_t)w * h * 2);
    hal_set_cs(0);
}

// =====================================================================
// 4. Entry points
// =====================================================================

void setup() {
    tft_init();
    tft_fill_screen(COLOR_BLACK);
    tft_draw_bitmap(0, 0, 128, 160, max_icon_vertical);
}

void loop() {
}
