# Session Handoff — Stormtrooper Voice Changer

_Last updated: 2026-06-12_

## TL;DR
Firmware + UI are **done and verified** on the original board. The project is now
**picking a new off-the-shelf board** that has audio onboard, because the original
1.47" board needs external audio circuitry. Three candidate schematics are in `docs/`
and need analysis (see "Next Task"). No custom PCB — that path was rejected.

---

## Where things stand

### Done & verified (on ESP32-S3-Touch-LCD-1.47, current `firmware/`)
- Full Imperial-console UI: main screen, settings carousel (SETTINGS → AUDIO →
  CALLSIGN → BRIGHTNESS via top arrows, wraps to main), mirrored lock screen,
  popups. All self-tested via webcam + serial-tap rig.
- Stormtrooper DSP: 300Hz–3kHz bandpass → soft-clip → bitcrush → static bed, VOX
  4-state gate w/ key-up/down click bursts (synth placeholders; user's real WAVs pending).
- Audio engine: ADC mic (GPIO4) → ring buffer → sigma-delta out (GPIO5). Modes
  PASS/TONE/MUTE. **Never heard** — no jacks soldered yet.
- NVS settings persistence; BOOT button (click=mute, 3s=lock, 10s=factory reset).
- Fonts: Montserrat (UI), Aurebesh-English hybrid (callsign, home+lock only),
  pure Aurebesh (lock decorations).
- Serial tap test rig (`ui_debug.c`, debug-only — remove for production).
- Git backup: https://github.com/hubris123/VStorm

### Build / flash
IDF **5.5.4 only**: `& C:\esp\v5.5.4\esp-idf\export.ps1` then
`idf.py build flash -p COM5`. Webcam frames in `capture\current\` (newest;
rotates fast — older in `capture\archive\` by timestamp).

---

## THE PIVOT — choosing a new board

The original 1.47 has no onboard audio. Rather than design a custom PCB (rejected)
or add bulky USB-audio dongles (rejected — see below), the plan is to switch to a
Waveshare board that has an **ES8311 audio codec onboard** (mic ADC + speaker DAC in
one chip) plus a speaker connector.

**Hard constraints (from the user):**
- Space is **very tight** and **requires an external boom mic** (onboard mic won't do).
- Prefer to attach the boom mic **without removing a component**.
- Output quality doesn't matter (megaphone/radio effect, dirty signal fine; will trim
  level so it doesn't overdrive a secondary amp). No line-level requirement —
  amplified speaker output is acceptable.
- **No custom board design.**

**Use S3, not C6** — C6 is single-core RISC-V, weak for DSP; all firmware is S3-based.

### Rejected paths (do not re-chase)
- Custom carrier/integrated PCB (MAX4466 + PCM5102A) — user doesn't want to design a board.
- USB-C audio dongle / passthrough — bulkiest option (fights tight space), host-vs-device
  port conflict loses serial-while-audio, highest latency, least proven. Set aside.

---

## NEXT TASK — analyze the three candidate schematics

Files (read **one at a time** to control context cost):
- `docs/ESP32-S3-Touch-LCD-1.83/ESP32-S3-Touch-LCD-1.83-schematic.pdf`
  (partially analyzed: ES8311 codec U9 + NS4150B amp U13 + ES7210 4ch ADC U12 +
  onboard mics MIC1/MIC2 + **J3 speaker connector**)
- `docs/ESP32-S3-Touch-AMOLED-1.8/ESP32-S3-Touch-AMOLED-1.8.pdf`
- `docs/ESP32-S3-Touch-AMOLED-2.16-Schematic/ESP32-S3-Touch-AMOLED-2.16-Schematic.pdf`

For **each** board, determine:
1. **Boom-mic attach point** — is the ES8311 `MIC1P/MIC1N` input, or a spare ES7210
   channel, broken out to an accessible pad/test-point/header so an external electret
   can attach **without removing the onboard mic**? (The user's key question.)
2. **Full-duplex** — confirm simultaneous mic-ADC + speaker-DAC on the ES8311 (it's an
   intercom codec, so expected — but a GitHub issue questioned mic+speaker on the
   AMOLED-2.06; verify the board wiring).
3. **Output connector** — type, and amplified-speaker (NS4150-class) vs line. Either is
   fine for this use.
4. **Display + touch controllers** — confirm the BSP can be adapted from Waveshare's demo.

Also pull each board's **GitHub demo repo** (github.com/waveshareteam/...) to confirm
the actual codec I2S/I2C pin assignments and that the demo does full-duplex.

**Deliverable:** a ranked recommendation of which board best fits (cleanest boom-mic
attach, confirmed full-duplex, drivable display).

---

## After the board is chosen (carries over from 1.47 work)
- Firmware audio path → ES8311 I2S codec (adapt Waveshare demo; don't write from scratch).
- New BSP for the chosen display/touch (adapt Waveshare demo).
- UI re-layout for the new resolution — all screens/logic/fonts carry over, just
  repositioned.
- Then resume original milestones: listening test, real click WAVs, M5 hardening
  (soak test, sticker copy, README; note 2 hardware LEDs that must be masked/desoldered).

## Efficiency rules (user is quota-conscious)
Batch changes into one flash; read a webcam frame only for risky changes (not color/size
tweaks); single frame unless verifying motion; keep replies short; prefer fresh sessions
when context gets large. IDF stays 5.5.4. Always adapt Waveshare demo code, never from scratch.
