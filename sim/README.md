# VStorm virtual screen

Runs the **real firmware UI (`firmware/main/ui.c`) and voice DSP (`firmware/main/dsp.c`)**
in a browser, so screens and the effect can be worked on without flashing a board.
Same LVGL version (8.4.0), same 16-bit colour, same 64 KB LVGL heap as the device.

| Firmware piece | In the simulator |
|---|---|
| `ui.c`, `dsp.c` | compiled unmodified (fw1 `ui.c` has one `#ifdef UI_SIM` for how fonts are linked) |
| LCD + touch (`esp_bsp`) | `<canvas>` + mouse/touch, `src/sim_main.c` |
| `audio.c` (fw3: ES8311 I2S, fw1: ADC→SDM) | Web Audio blocks → same DSP → speakers, `src/sim_hal_fw3.c` / `sim_hal_fw1.c` |
| `settings.c` (fw1 NVS) | browser `localStorage` |
| BOOT button | fw3: tap = MUTED/LIVE. fw1: tap = mute, hold 3 s = lock, 10 s = factory reset |
| backlight | CSS brightness on the canvas (fw3: SH8601 0x51 = 140) |
| AMOLED rounded corners | CSS radius on the canvas (≈45 px, as `design/screen_template.png`) |

## Run it

```bash
cd sim && python3 -m http.server 8000
```
Open http://localhost:8000/web/ . `web/vstorm-fw3.wasm` and `vstorm-fw1.wasm` are committed, so no build is needed
just to run it. Microphone input works here (use headphones).

## Rebuild after changing firmware code

Needs `clang` + `wasm-ld` (LLVM 15+) and `python3`. No Emscripten, no ESP-IDF.
On Windows use LLVM from https://releases.llvm.org or MSYS2, or run it in WSL / a cloud session.

```bash
./sim/build.sh               # fw3 -> sim/web/vstorm-fw3.wasm (fetches LVGL v8.4.0 into sim/.deps once)
./sim/build.sh fw1           # or: ./sim/build.sh all
node sim/tools/bundle.mjs    # -> sim/dist/vstorm-console.html, one file you can send to anyone
```

## Scripted screenshots (no browser)

```bash
node sim/tools/headless.mjs sim/tools/tour-fw3.txt out/          # AMOLED-1.8
node sim/tools/headless.mjs sim/tools/tour-fw1.txt out/ fw1      # original 1.47
```
Script commands: `size W H`, `wait MS`, `tap X Y`, `drag X1 Y1 X2 Y2`,
`boot click|lock|reset`, `signal AMPLITUDE MS`, `shot NAME`. Every shot prints LVGL heap use.
This is the loop for UI work: edit `ui.c` → `build.sh` → run a script → look at the PNGs.

## Limits

- Timing is not the device's: no SPI flush cost, no FreeRTOS tasks. Check performance on hardware.
- Touch is a mouse. (On the fw3 bench unit touch is dead, so only BOOT and VOX matter there.)
- The browser mic is not the ES8311 with 18 dB PGA gain: levels differ, so set VOX on the device.
  Effect, static and click amounts carry over directly.
- Audio rate is 16 kHz like the firmware when the browser allows it; otherwise the DSP runs
  at the browser's rate (shown under Readouts).
- Hosted (claude.ai) copies can't use the microphone or save WAVs; run it locally for those.
