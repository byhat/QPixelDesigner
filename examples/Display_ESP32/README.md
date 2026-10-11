# Display_ESP32 — ST7735S on ESP32 WROOM-32

An Arduino sketch that draws a 128x160 RGB565 image on an ST7735 1.8" SPI
TFT. It demonstrates the Pixel UI Designer workflow end to end: design a
screen, export the RGB565 C array, show it on real hardware. The driver
talks to the display directly over hardware VSPI — no display library
required.

## Generating image_data.h

Design the screen in [Pixel UI Designer](../..) with the 128x160 (ST7735)
preset, then press "RGB565 C array…":

- array name: `max_icon_vertical` (the name used by the sketch) or edit
  the `tft_draw_bitmap` call;
- byte order: big-endian (`rgb565be`);
- adjust contrast/saturation/brightness if needed.

The exported header matches the display: 128x160, 40960 bytes, two bytes
per pixel, MSB first.

Equivalent ffmpeg pipeline, if the source is a PNG:

```bash
ffmpeg -i icon.png -vf "scale=128:160:flags=neighbor,eq=contrast=1.3:saturation=1.5" \
       -f rawvideo -pix_fmt rgb565be icon.raw
xxd -i icon.raw > image_data.h
```

Then rename the array to `max_icon_vertical` and add `const` and `PROGMEM`
to the declaration, as in the current `image_data.h`.

## Wiring

On the DevKit V1 board the silk screen shows signal names (D23, D18, D5,
3V3...), not GPIO numbers: D18 is GPIO18. All pins used below are in the
right row when the board stands vertically (antenna up, USB down).

| Display pin (ST7735 1.8") | Board label | Location |
|---|---|---|
| VCC | 3V3 | bottom of the board |
| GND | GND | bottom of the board |
| SCL / SCK / SPC | **D18** | right row, between D19 and D5 |
| SDA / MOSI / DIN | **D23** | top pin of the right row |
| RES / RST | **D4** | right row, between RX2 and D2 |
| A0 (= DC) | **D2** | right row, between D4 and D15 (also the blue LED pin) |
| CS / SS | **D5** | right row, right below D18 |
| BLK / BL (backlight) | 3V3 | bottom of the board |

Pins of the left row (D32, D33, D34...) cannot be used for SPI here:
SCK and MOSI are hard-wired to D18 and D23. Both the ESP32 and the ST7735
are 3.3 V devices, no level shifting is needed.

## Build

1. Arduino IDE, board **ESP32 Dev Module** (`esp32 by Espressif` package).
2. Open `Display_ESP32.ino`.
3. Upload.

If the image breaks up or shows stripes, lower `SPI_FREQ` at the top of
the sketch (40 MHz -> 27 MHz -> 10 MHz); long wires are the usual culprit.
