# VStorm virtual screen

Runs the **real firmware UI and voice DSP** in a browser (WebAssembly), so screens and the
effect can be worked on without flashing a board. Same LVGL version (8.4.0) and 16-bit colour
as the device.

| Target | Board | UI source | Notes |
|---|---|---|---|
| `fw3i` (default) | ESP32-S3-Touch-AMOLED-1.8, 368×448 | `sim/ui_imperial/ui_imperial.c` | **New Imperial console UI** (`docs/UI-DESIGN.md`). Background skin `design/skins/default/*.png` + live LVGL layer. Main, 8 settings pages, callsign code cylinder, hold-to-erase reset, lock + 5-tap unlock, voice-reactive. Settings persist in localStorage. |
| `fw3` | ESP32-S3-Touch-AMOLED-1.8, 368×448 | `firmware3/main/ui.c` | What's flashed today. Boots MUTED, BOOT toggles LIVE. Effect sliders print the 4 DSP lines for `firmware3/main/audio.c`. |
| `fw1` | ESP32-S3-Touch-LCD-1.47, 320×172 | `firmware/main/ui.c` | Original build (settings / callsign / lock screens). 64 KB LVGL pool like that firmware. |

All use `firmware/main/dsp.c` for the voice effect. `firmware3/main/audio.c` carries the same
math inline; if you change the DSP math there, mirror it into `dsp.c`.

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
Open http://localhost:8000/web/ . The `web/vstorm-*.wasm` builds are committed, so no build is needed
just to run it. Microphone input works here (use headphones).

## Rebuild after changing firmware code

Needs `clang` + `wasm-ld` (LLVM 15+) and `python3`. No Emscripten, no ESP-IDF.
On Windows use LLVM from https://releases.llvm.org or MSYS2, or run it in WSL / a cloud session.

```bash
./sim/build.sh fw3i          # -> sim/web/vstorm-fw3i.wasm
./sim/build.sh               # fw3 -> sim/web/vstorm-fw3.wasm (fetches LVGL v8.4.0 into sim/.deps once)
./sim/build.sh fw1           # or: ./sim/build.sh all
node sim/tools/bundle.mjs    # -> sim/dist/vstorm-console.html, one file you can send to anyone
```

## Scripted screenshots (no browser)

```bash
node sim/tools/headless.mjs sim/tools/tour-fw3i.txt out/ fw3i    # new Imperial UI
node sim/tools/headless.mjs sim/tools/tour-fw3.txt out/          # AMOLED-1.8 as flashed today
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

## Skins

`design/skins/concepts/make_concepts.py` (skins 0-20) and `make_concepts2.py` (skins 21-35,
shown as 22-36) are the single source for every skin: they draw the mock-ups and export
`design/skins/packs/` (`skins.json` recipes + per-skin background layers + carousel
thumbnails). Run `make_concepts2.py`; it exports both sets. `sim/tools/gen_skins.py` turns
that into `skins_gen.c` at build time (lossless RLE backgrounds).

A recipe is a palette, a key style and a **list of parts** (`skin.h`, up to 12): scope (5
frame styles), round scope, tall scope, target view, waterfall, meter (segments, dots, twin,
horizontal, arc), battery bar, lamps (cluster, switcher bank, domes, pills, diamonds,
matrix), edge lamp strip, Aurebesh readout, rotor (Scomp, reticle, square brackets, twin,
radar, cog), bar graph, ring meter, big gauge, hex table, temp tag. Parts can repeat (two
scopes, two rotors). The UI code (`ui_imperial.c`) builds the live widgets from the list;
the background art holds everything that never moves. Layout rules: `docs/UI-DESIGN.md` 5a.
Settings page 14 (SKIN) switches between them. On the product the same recipes + art come
from signed `.vskin` files on the SD card (`docs/PRODUCT-NOTES.md`).

To add a skin: add an entry to `CONCEPTS2` in `make_concepts2.py` (a part list), run it, rebuild.

On the device the per-part state (`console_t`, ~60 KB for both screens) and the bar-graph
canvases belong in PSRAM.

## Voice chain (fw3i)

The fw3i build runs the new voice chain from `firmware3/main/` (shared with the device):
`voicefx.c` (mic gain + soft clip -> tail cut (room echo after words) -> voice shift -> radio voicing matched on a real voice to the trooper references: steep cuts below ~700 Hz and above ~3 kHz before and after the drive, near-flat presence trims, bitcrush, static; VOX gate; checked with `tools/voice_analysis/compare.py`),
`vshift.c` (learned pitch + tone shift: real-time PSOLA with per-grain resampling, 32 ms
latency while on; learning = median pitch + LPC formants -> ratios onto the target) and
`keysnd.c` (synthesised radio key-up / key-down sounds, six kinds). The VOICE page (settings
page 2) learns from whatever plays in: mic, a recording, or Demo voice. Inside claude.ai the
page cannot use the microphone; open the console file directly in Chrome for that.

Offline tests (native gcc, `tools/dsp_test/`): `make_test_voices.py` (synthetic male / female
/ child voices), `voice_test.c` (learn + shift + cost), `chain_test.c` (whole chain before /
after WAV), `render_keys.c` (key sounds). Targets and key-sound numbers come from reference
recordings measured with `tools/voice_analysis/` (the recordings stay in the git-ignored
`voice_refs/`; only numbers are kept). Tours: `tools/tour-voice.txt`.

## Moving the Imperial UI into firmware3 (later)

`ui_imperial.c` only uses LVGL 8.4, `esp_random()`, firmware3's `audio.h` and the hooks in
`ui_imperial.h`. To port: add `audio_is_keyed()` to `firmware3/main/audio.c` (the VOX state
is already there), implement the `ui_platform_*` hooks (NVS + DSP params + ES8311
volume + SH8601 brightness; `ui_platform_battery()` from the AXP2101 and `ui_platform_tilt()`
from the QMI8658 - Waveshare's `01_AXP2101` / `03_QMI8658` examples show both), enable Montserrat 12/14/16/18/20/22/28 and Tiny TTF in sdkconfig,
embed the fonts and the skin as RGB565 **byte-swapped** (firmware3 uses LV_COLOR_16_SWAP=y),
raise the flash size to 16 MB, and pin audio and LVGL to different cores.
Voice chain: add `voicefx.c`, `vshift.c`, `keysnd.c` to `firmware3/main/CMakeLists.txt` and
replace the per-sample DSP in `audio_task` with `voicefx_process()` (it already returns Q15 and
flags clipping); `audio_is_keyed()` = `voicefx_keyed()`. Save the VOICE fields of
`ui_settings_t` in NVS. The UI calls `vlearn_*` from the LVGL task while audio runs on the
other core: plain flags and counters, fine for learning, but keep the two on separate cores.
Regenerate the skin with `python3 design/skins/make_default_skin.py`.
