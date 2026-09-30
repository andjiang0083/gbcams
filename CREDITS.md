# Credits & third-party licenses / 署名与第三方许可

GBCAMS 自身代码以 **MIT** 发布（见 [LICENSE](LICENSE)）。固件链接/内置了下列第三方组件 ——
发布二进制时请一并保留这些署名。

## Libraries linked into the firmware

| Component | License | Author / source |
|---|---|---|
| **M5Unified** | MIT | M5Stack — https://github.com/m5stack/M5Unified |
| **M5GFX** (incl. LovyanGFX) | MIT | M5Stack / lovyan03 — https://github.com/m5stack/M5GFX |
| **M5Cardputer** | MIT | M5Stack — https://github.com/m5stack/M5Cardputer |
| **IRremote** (pulled in by M5Cardputer) | MIT | Ken Shirriff and contributors — https://github.com/Arduino-IRremote/Arduino-IRremote |
| **esp32-camera** | Apache-2.0 | Espressif Systems — https://github.com/espressif/esp32-camera |
| **ESP32_NOW** (`lib/ESP32_NOW`, vendored) | part of the Arduino ESP32 core | copied from `arduino-esp32/libraries/ESP_NOW` — https://github.com/espressif/arduino-esp32 |
| **Arduino ESP32 core** | LGPL-2.1 (core) / Apache-2.0 (ESP-IDF parts) | Espressif Systems — https://github.com/espressif/arduino-esp32 |
| **ESP-IDF** | Apache-2.0 | Espressif Systems — https://github.com/espressif/esp-idf |

## Fonts and image codecs bundled by M5GFX

| Component | License | Author / source |
|---|---|---|
| **glcdfont** (the built-in 5×7 `Font0` used by the sidebar HUD and the cover generator) | 2-clause BSD | Adafruit Industries — https://github.com/adafruit/Adafruit-GFX-Library |
| **efont** (the `efontCN_12` Chinese font used by the help pages) | 3-clause BSD | The Electronic Font Open Laboratory — `lgfx/Fonts/efont/COPYRIGHT.txt` in M5GFX |
| **GFXFF fonts / TomThumb** | 2-clause BSD / 3-clause BSD | Adafruit; Brian J. Swetland, Vassilii Khachaturov, Dan Marks |
| **TFT_eSPI fonts 2,4,6,7,8** | FreeBSD | Bodmer |
| **IPA font** | IPA Font License v1.0 | Information-technology Promotion Agency, Japan |
| **TJpgDec** | ChaN's terms: free to use for education, research and commercial development, no warranty | ChaN — http://elm-chan.org/fsw/tjpgd/00index.html |

## Palette / aesthetic references

The pixel filters reproduce **colour values and quantisation levels** of Nintendo Game Boy family
hardware (DMG `#0F380F`/`#306230`/`#8BAC0F`/`#9BBC0F`, GBC/GBA 15-bit colour). No Nintendo code,
font, BIOS or artwork is included or required.

## Images in this repository

Everything under `docs/screenshots/` and `release/*/cover-320x200.png` is **rendered**, not
photographed: the UI screens are drawn by `tools/make_m5burner_covers.py` (which parses the device's
own font data so titles match the hardware), and the camera image inside the mock viewfinder is a
synthetic scene, not a capture. They are not evidence of on-device behaviour.
