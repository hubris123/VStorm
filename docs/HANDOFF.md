# Session Handoff - Stormtrooper Voice Changer

_Last updated: 2026-11-11_

## TL;DR

The board pivot is done and the **AMOLED port works**. Active firmware is
**`firmware3/`** (ESP32-S3-Touch-AMOLED-1.8). Display, full Imperial UI, and
audio (ES8311) all run on-device. Audio is confirmed end-to-end: mic captures,
DSP runs, speaker outputs. Mic + speaker are now **soldered on with the speaker
separated** from the mic, so the **first real listening test** is the next step.

Repo: https://github.com/hubris123/VStorm (branch `main`).

---

## Build / flash

IDF **5.5.4 only**. From `firmware3/`:
```
& C:\esp\v5.5.4\esp-idf\export.ps1
idf.py build
idf.py flash -p COM8          # board is on COM8
idf.py monitor -p COM8        # needs a real TTY; background jobs fail
```
`build/` and `managed_components/` are gitignored - run `idf.py reconfigure`
once after clone to let the component manager restore them (`dependencies.lock`
is committed so versions pin).

---

## The one thing that matters most: the display path

This unit's `board_variant_detect()` reports **SH8601 + FT3168** (not
CO5300+CST816). The working display path is the stock `05_LVGL_WITH_RAM`
example's **`app_main_legacy_sh8601()`**: raw `esp_lcd` SH8601 driver + manual
`esp_lcd_panel_draw_bitmap` flush + **LVGL v8.4**.

**Do NOT use `bsp_display_start()` / the `esp_lv_adapter` (CO5300) path.** It
renders garbage under LVGL v9 and a black screen under v8 on this unit. That was
the entire multi-hour battle. `firmware2/` is the abandoned v9/adapter attempt -
ignore it (kept only as a record of the dead end).

Pin LVGL to `8.4.*`.

---

## firmware3 layout

```
firmware3/
  main/
    example_qspi_with_ram.c  - board bringup (stock 05 example, lightly edited):
                               board_variant_detect -> SH8601 legacy path ->
                               ui_init() + audio_init(touch_i2c_bus) +
                               start_mute_button(). Brightness via cmd 0x51 (=140).
    ui.c / ui.h              - LVGL v8 Imperial console UI (our code)
    audio.c / audio.h        - ES8311 full-duplex + stormtrooper DSP (our code)
  components/
    esp32_s3_touch_amoled_1_8 - BSP (touch init made non-fatal, see below)
    espressif__es8311         - VENDORED es8311 (registry 1.x uses old i2c_port_t
                                API; this copy from 06_I2SCodec takes the bus handle)
    board_variant, esp_lcd_sh8601, ...
  sdkconfig                   - committed (hand-tuned: perf monitor off, etc.)
```

## What works (firmware3, verified on hardware 2026-06)

- Display clean, no garbage. Dimmed to ~55% (cmd 0x51 = 140) for readability.
- Full UI: top bar (callsign + gear), scrolling scope, segmented level meter,
  animated status-light cluster, rotating dual-arc knob, bottom bar
  (LOCK / TX / CLIP / LIVE-or-MUTED). Corner-safe insets (see dead-zone note).
- Audio: ES8311 full-duplex 16kHz. Mic path is I2S through the ES8311 (analog
  mic, `es8311_microphone_config(h, false)`). DSP ported verbatim from the 1.47
  firmware: 300Hz-3kHz bandpass -> soft-clip -> bitcrush -> static bed, VOX
  4-state gate with key-up/down click bursts.
- Output boots **MUTED** (safe). **GPIO0 / BOOT button toggles MUTED <-> LIVE**
  (touch is dead on this unit so the button is the control). Muted still drives
  the meter/scope from live mic level.
- Safe gains: ES8311 voice vol 55, mic gain 18dB, VOX thresh 22, static 15.

## Known issues / decisions

- **Touch**: CST816 does not ACK on this unit; touch init made **non-fatal** in
  the BSP (`bsp_display_indev_init`). Input is GPIO0 button + VOX. Not needed for
  core function.
- **Feedback**: onboard mic + speaker sit adjacent -> acoustic feedback when
  LIVE. Fixed by physical separation (the now-soldered remote speaker). Not a bug.
- **Rounded display**: 368x448 is a round-rect; outer ~45px corners are
  physically clipped. Keep content clear of corners. Guide image:
  `design/screen_template.png` (shows screen outline, corner dead zones, and the
  live-widget window rects from `ui.c`).

---

## Next steps

1. **Listening test** (now unblocked): power on -> boots muted -> confirm meter
   tracks voice -> press BOOT for LIVE -> listen for the trooper effect with the
   speaker separated from the mic.
2. Tune DSP to taste (drive / static / VOX / volume / mic gain in `audio.c`).
3. Aurebesh tiny_ttf fonts (have `aurebesh.ttf` + english-aurebesh in the 1.47
   `firmware/`); then restyle to the Imperial MFD look.
4. Settings / lock / callsign screens, 5-tap unlock, mask/desolder the 2 hardware LEDs.
5. **Voice shift + new key sounds** are built and running in the sim (fw3i): learned pitch
   + tone shift onto the TROOPER target, VOICE page (settings page 2), synthesised radio
   key sounds matched to the reference set. Port steps: `sim/README.md` (Voice chain).
   CLONE target comes after TROOPER is signed off.

## Product vision (future, captured so it isn't lost)

Skinnable UI: a "skin" = a 368x448 background PNG with dark window rects where
the live LVGL widgets sit on top. Delivery: companion phone app scans a **QR =
signed single-use download URL** from a website, downloads the skin, pushes it to
the device over **BLE** (ESP32-S3 is BLE-only; device never goes online).
Device shows its ID on a settings screen; user types it into the website at
registration (air-gapped). Licenses are **device-bound** (signed for the unit's
eFuse ID) so a copied skin won't load on another device. Marketplace sells
**original** sci-fi glyph skins (literal aurebesh only for free/personal
sideload - Disney IP risk on paid Star Wars art).

---

## Product notes

`docs/PRODUCT-NOTES.md` - to-dos for the shipping product: signed + encrypted skin packs on
the SD card (`.vskin`), device-bound paid skins, Secure Boot / Flash Encryption checklist,
16 MB flash + skins partition.

## New Imperial UI (designed, running in the sim, not yet on the device)

Design spec `docs/UI-DESIGN.md`; mock-ups `design/mockups/`; skin (background layer)
`design/skins/default/`; working LVGL code `sim/ui_imperial/ui_imperial.c` (target `fw3i`,
the default in the virtual console). Port steps are at the end of `sim/README.md`.

## Virtual screen (`sim/`) - check UI and DSP changes here before flashing

`sim/` compiles the real `firmware3/main/ui.c` (and the old `firmware/` UI) plus the voice
DSP to WebAssembly with LVGL 8.4.0. Browser: `cd sim && python3 -m http.server` -> `/web/`.
Shows the 368x448 rounded screen, BOOT = MUTED/LIVE, and has Effect/VOX/Static/Click sliders
that print the four lines for `firmware3/main/audio.c`. Feed it the demo voice, a recording
of your own voice, or the mic (local only). Headless screenshots:
`node sim/tools/headless.mjs sim/tools/tour-fw3.txt out/`. Rebuild: `sim/build.sh all`
(clang only, no ESP-IDF). Details: `sim/README.md`.

Findings from the sim on the old 1.47 `firmware/` (fixed there; firmware3 already has the
VOX fix): un-normalised VOX envelope held the gate open on ADC noise; 48 KB LVGL heap ran out
going callsign page -> lock (raised to 64 KB); callsign dial's selected letter doesn't draw
(theme padding wider than the 27 px dial) - check this when porting the callsign screen.

---

## Reference material in this repo

- `docs/ESP32-S3-Touch-AMOLED-1.8/code examples/` - Waveshare demos. The two that
  matter: **`05_LVGL_WITH_RAM`** (the proven display path) and **`06_I2SCodec`**
  (ES8311 audio + the vendored es8311 component). We built firmware3 from these.
- `docs/ESP32-S3-Touch-AMOLED-1.8/ESP32-S3-Touch-AMOLED-1.8_schematic.pdf`
- `docs/BOARD-ANALYSIS.md` - why this board was chosen.
- `firmware/` - the original, fully-working **1.47-board** firmware (ADC mic +
  sigma-delta out). Source of the DSP, UI design, and fonts.
