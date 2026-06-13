# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Overview

This repository contains demo code for the **Waveshare ESP32-S3 Touch LCD 1.47"** development board. All source is under `docs/ESP32-S3-Touch-LCD-1.47-Demo/` with two parallel tracks:

- **ESP-IDF** (`docs/.../ESP-IDF/`) — Espressif's native framework, requires ESP-IDF v5.4.0+
- **Arduino** (`docs/.../Arduino/`) — Arduino IDE / arduino-cli based examples

## ESP-IDF Build Commands

Each numbered subfolder (`01_factory`, `02_sd_card_test`, `03_lvgl_example`, `04_lvgl_image`, `05_lvgl_sd_image`) is a standalone ESP-IDF project. From within a project directory:

```bash
idf.py set-target esp32s3      # first time only
idf.py menuconfig               # optional — configure sdkconfig
idf.py build
idf.py flash monitor            # flash and open serial monitor
idf.py flash -p COMx            # Windows: specify port
```

To build a single component or run a specific test:
```bash
idf.py build -C docs/ESP32-S3-Touch-LCD-1.47-Demo/ESP-IDF/03_lvgl_example
```

## Arduino Build Commands

Use the Arduino IDE 2.x or arduino-cli. Required board package: `esp32` by Espressif. Required libraries (pre-included under `Arduino/libraries/`):
- `GFX_Library_for_Arduino`
- `lvgl` (v8)

With arduino-cli:
```bash
arduino-cli compile --fqbn esp32:esp32:esp32s3 docs/ESP32-S3-Touch-LCD-1.47-Demo/Arduino/examples/01_gfx_helloworld/
arduino-cli upload -p COMx --fqbn esp32:esp32:esp32s3 docs/ESP32-S3-Touch-LCD-1.47-Demo/Arduino/examples/01_gfx_helloworld/
```

## Hardware Pin Map (common across all examples)

| Function | GPIO |
|----------|------|
| LCD DC   | 45   |
| LCD CS   | 21   |
| LCD SCK  | 38   |
| LCD MOSI | 39   |
| LCD RST  | 47 (ESP-IDF) / 40 (Arduino) |
| Backlight| 46   |
| I2C SDA  | 8    |
| I2C SCL  | 9    |

Display: ST7789 / JD9853, 172×320, SPI interface. Touch: AXS5106 over I2C. IMU: QMI8658 over I2C.

## Architecture

### ESP-IDF project layout (each numbered folder)

```
main/           — app_main entry point, LVGL filesystem port
components/
  esp_bsp/      — Board Support Package: LCD init, I2C, touch, IMU drivers
  esp_lcd_jd9853/       — JD9853 LCD panel driver (esp_lcd API)
  esp_lcd_touch_axs5106/ — AXS5106 touch driver
  lvgl_ui/      — LVGL 8 UI: tileview with tiles for camera, WiFi, IMU, RGB, system
    tileview/   — individual tile source files (camera_tile, wifi_tile, qmi8658_tile, etc.)
    images/     — LVGL image data (C arrays)
```

The BSP layer (`esp_bsp`) wraps ESP-IDF's `esp_lcd` and `esp_lcd_touch` APIs and is shared (with minor differences) across all five examples.

### LVGL integration

- LVGL tick is driven via `esp_timer` periodic callback
- LVGL flush callback uses `esp_lcd_panel_draw_bitmap`
- Touch read callback polls the AXS5106 via I2C through `esp_lcd_touch`
- `lv_fs_port.c` (in `main/`) maps LVGL's filesystem API to SPIFFS/SD

### Arduino examples

Each `.ino` directly calls `Arduino_GFX_Library` for display and raw SPI. The LVGL Arduino example (`03_lvgl_arduino_v8`) uses the bundled `lvgl` library with the same flush/tick pattern as ESP-IDF.
