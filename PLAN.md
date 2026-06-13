# Stormtrooper Voice Changer — Project Plan

**Board:** Waveshare ESP32-S3-Touch-LCD-1.47 (ST7789/JD9853 172×320, AXS5106 touch, QMI8658 IMU)
**Framework:** ESP-IDF v5.5.4 · LVGL 8 · Power: USB-C
**Project location:** `firmware/` in repo root, BSP copied from the Waveshare `01_factory` demo.

---

## Product summary

A prop-grade voice changer. Speak into a cheap electret boom mic; the device applies a
"stormtrooper comm" effect (band-limited radio voice, distortion/bitcrush, static bed)
and outputs line level to an external amplifier. VOX keying: when you start talking it
"keys up" with a mic-click/pop, and when you stop it keys down with the classic squelch
click. Touchscreen shows an Imperial-prop UI with live waveform, gain sliders, and a
tap-to-lock screen that needs 5 rapid taps to unlock.

---

## Hardware & Pinout (the part you solder)

### Audio input — electret boom mic (female 3.5mm TRS jack suggested)

| Signal | Pin | Notes |
|---|---|---|
| MIC audio | **GPIO 4** (ADC1_CH3) | Through 1µF coupling cap |
| MIC bias | 3V3 via **2.2kΩ** resistor to mic + | Electret needs bias power |
| Mid-rail bias | 2× 10kΩ divider (3V3→GND) + 10µF to GND, joined to GPIO 4 side | Centers signal at ~1.65V |
| GND | GND | Jack sleeve |

```
3V3 ──2.2k──┬────────────── mic tip (+)
            │
           1µF
            │
GPIO4 ──────┴──┬── 10k ── 3V3
               └── 10k ── GND   (plus 10µF from this node to GND)
GND ─────────────────────────── mic sleeve (−)
```

### Audio output — line level to amplifier (female 3.5mm jack)

| Signal | Pin | Notes |
|---|---|---|
| AUDIO OUT | **GPIO 5** (sigma-delta out) | Through RC filter below |
| GND | GND | Jack sleeve |

```
GPIO5 ──1kΩ──┬──10µF(+)──── jack tip (line out)
             │
            100nF
             │
            GND
```
This gives ~1Vpp line level after the coupling cap. (Future upgrade: PCM5102A I2S DAC
on GPIO 5/6/7 — software switch already planned for.)

### Reserved / existing pins (do not use)
LCD: 38, 39, 21, 45, 40, 46 · I2C (touch/IMU): 41, 42 · Touch INT/RST: 47, 48 ·
SD slot: 13–18 · Buttons: **GPIO 0 (BOOT)** + power/reset button.
**Milestone 0 verifies GPIO 4/5 are actually broken out on the pad row against the schematic; fallback candidates: GPIO 1, 2, 3, 6, 7.**

### Buttons
- **BOOT (GPIO 0):** short press = mute/un-mute output (panic mute); long press (~3 s)
  = force-lock screen; very long hold (~10 s) = **factory reset**, which raises a
  touchscreen confirmation popup ("RESTORE FACTORY SETTINGS?" CONFIRM/CANCEL) before
  wiping NVS (callsign, gains, VOX settings) and rebooting. Works even when locked —
  it's the escape hatch if someone forgets the unlock gesture.
- **PWR/RESET:** stays hardware reset.

---

## Audio architecture

```
ADC (continuous DMA, 16 kHz) ──► input gain ──► HPF 300 Hz ──► LPF 3 kHz
  ──► soft-clip distortion ──► bitcrush (subtle) ──► static/noise bed mix
  ──► VOX gate (key-up click / key-down squelch click) ──► output gain
  ──► sigma-delta DAC task
```

- 16 kHz mono, block size 256 samples (~16 ms latency per stage, target <50 ms total).
- VOX: RMS envelope with attack/hang; threshold + hang time adjustable in UI.
- Click/pop: short pre-rendered PCM assets supplied by user, organized as two pools
  (key-up variants, key-down variants). On each key event the firmware picks one at
  random — never the same one twice in a row — so keying sounds dynamic. Assets are
  WAV (16 kHz mono 16-bit preferred; other rates auto-converted offline), embedded as
  C arrays. Pool size flexible (3–6 each is plenty).
- Static bed: low-level white noise through same band-pass, only while keyed.
- Clipping detection flags any sample > ~95% full scale → waveform turns red.

## UI design (320×172 **landscape**, LVGL 8 tileview, swipe between screens)

1. **Main screen:** live waveform running the full width (center), input gain and
   output gain sliders, VOX "TX" lamp, clip indicator, mute state.
2. **Settings screen:** simple sliders — VOX threshold, effect intensity (distortion,
   static level), click volume, **screen brightness** (backlight PWM on GPIO 46,
   persisted) — plus a button that opens the callsign page.
3. **Callsign page (own dedicated screen):** **code-cylinder dials** — each character
   position is a vertical roller (LVGL roller, restyled) flicked through A–Z, 0–9,
   dash, like an Imperial code cylinder / combination lock. Landscape suits this:
   up to ~10 rollers side by side across 320 px. +/− to add/remove positions, COMMIT
   to save (NVS), BACK to cancel. Default "TK-421". Styled as an encryption-key
   entry console.
3. **Lock screen (Imperial prop mode):** single tap anywhere on main screen header → locks.
   Star Wars font, fake comm readouts: channel = user's callsign (default "TK-421",
   editable in settings), rotating "ENCRYPTION" hex,
   signal-strength bars, blinking status lamps, scrolling Aurebesh-style ticker —
   all decorative. Live waveform stays real. **Unlock: 5–7 rapid taps** (5 within ~2.5 s
   unlocks; 1–2 accidental extra taps are harmlessly absorbed; a subtle blip acknowledges
   each tap; wrong timing resets silently). When locking, a **confirmation popup** appears
   first: "LOCK CONSOLE — unlock with 5 rapid taps" with CONFIRM/CANCEL, so the user
   always learns the gesture before being locked out. Audio keeps running while locked.

**Fonts:** Unlocked UI uses LVGL's built-in Montserrat at sizes suited to the 172×320
panel (14 pt body, 18–20 pt headers, larger for the waveform screen labels). Locked UI
switches to **Aurebesh** from `providedassests/font/` — `Aurek-Besh Narrow.ttf` preferred
for the narrow panel, regular as fallback. Aurebesh maps Latin A–Z/0–9, so the same
label text renders as in-universe glyphs when locked (callsign, readouts, ticker all
become "Imperial script" — unreadable to bystanders, which sells the prop). Converted
offline with `lv_font_conv` to C arrays at 2–3 sizes; range A–Z, 0–9, dash, space.
No Latin hint needed on the lock screen — the lock-confirmation popup teaches the
gesture, and a 10 s BOOT hold offers a confirmed factory reset as the escape hatch.

---

## Milestones (each requires your approval before the next begins)

### M0 — Project skeleton & hardware verification *(no soldering needed yet)*
- Create `firmware/` IDF v5.5.4 project, copy/port BSP from `01_factory`.
- Verify it builds and flashes; LCD + touch + buttons working (smoke-test screen).
- Confirm GPIO 4/5 availability against the board schematic; **deliver final solder pinout**.
- ✅ Gate: you see a test UI on the device and approve the pinout to solder.

### M1 — Audio I/O proven
- ADC continuous capture task + sigma-delta output task, straight passthrough.
- Tone generator self-test mode (verifies output path without mic).
- ✅ Gate: you hear your voice (raw, latency-checked) through the amp.

### M2 — Stormtrooper DSP
- Band-pass, soft-clip distortion, bitcrush, static bed; tunable parameters.
- VOX gate with key-up/key-down click assets.
- ✅ Gate: it sounds like a stormtrooper to you; clicks feel right.

### M3 — Main UI
- Waveform widget (shared by input/output view) with red clip indication.
- Input/output gain sliders wired to DSP; TX lamp, mute button.
- ✅ Gate: you can drive all audio controls from the touchscreen.

### M4 — Lock screen & Imperial prop polish
- Star Wars font conversion (you supply TTF/OTF), 5-tap unlock, decorative readouts.
- Settings tile (VOX threshold, effect intensity, brightness).
- ✅ Gate: full product walkthrough on-device.

### M5 — Hardening & ship
- Settings persisted to NVS, brown-out/pop-free startup, long-run stability test,
  final pinout/wiring doc (`docs/PINOUT.md`), build/flash instructions in README.
- Bottom-sticker copy for the user to print: unlock gesture (5 rapid taps) and
  factory reset (hold BOOT 10 s, confirm on screen).
- ✅ Gate: final approval.

---

## Open items needed from you
1. ~~Star Wars font~~ ✅ Received: Aurebesh TTFs in `providedassests/font/`.
1a. Key-up and key-down click sound files (a few variants of each, WAV preferred) — needed by M2.
2. ~~Connector type~~ ✅ RLECS 3.5 mm 4-pole (TRRS) panel-mount jacks, gold-plated, with nut.
   Wiring (both jacks): **Tip = signal, Sleeve = GND, Ring1+Ring2 = bridge to Tip** so
   TRRS, TRS, and mono TS plugs all work. Mic jack tip carries bias+audio; out jack tip
   is line out. Exact lug identification goes in `docs/PINOUT.md` at M0 (these jacks'
   solder lugs aren't always labeled — I'll include a continuity-test step: plug in a
   plug, beep tip lug against the plug tip).
3. After M0: solder per the verified pinout before M1 testing.
