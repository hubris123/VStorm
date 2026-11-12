# Board Analysis - choosing the new audio-capable board

_Analyzed 2026-06-15 from the three schematics in `docs/`. Goal: pick the Waveshare
S3 board that best fits a tight-space stormtrooper prop with an external boom mic._

All three boards share the same audio building blocks: **ES8311** mono codec (the
full-duplex part: simultaneous mic-ADC + speaker-DAC), **NS4150B** class-D speaker
amp, and the AMOLED/LCD-2.16 and LCD-1.83 add an **ES7210** 4-channel ADC for an
onboard mic array. All are ESP32-S3R8 (correct - not C6). Full-duplex is structurally
fine on every board: DAC -> amp -> speaker is a separate path from mic -> ADC.

---

## Ranked recommendation

### 1. ESP32-S3-Touch-AMOLED-1.8  ← recommended

**Why it wins: it is the only board that breaks the ES8311 differential mic input out
to dedicated named solder pads.** Net `MIC_P` -> pad **P1**, net `MIC_N` -> pad **P2**
(confirmed in netlist). The onboard electret MIC1 sits on the same differential net
through a 0R jumper (`L5`). So a boom mic attaches three ways, all without touching the
mic itself:
  - solder boom electret to P1/P2 and leave MIC1 live (both mics in parallel; boom
    dominates up close - fine since output quality does not matter), or
  - lift the 0R jumper `L5` (not the mic) to fully isolate the onboard mic.

This directly answers the user's key question better than either other board.

- **Codec:** ES8311 only (simplest full-duplex; no ES7210 to configure).
- **Output:** ES8311 OUTP/OUTN -> 0.1uF -> 150k -> NS4150B (U10) -> **onboard speaker
  H2 (8 ohm 1 W, soldered)**. Amplified, dirty - fine for the megaphone effect. Note:
  speaker is soldered, not a connector; PA_OUTL pads are tappable if an external
  speaker/amp is wanted.
- **Exact pins (from netlist):** I2S MCLK=GPIO16, BCLK=GPIO9, WS/LRCK=GPIO45,
  DOUT->codec(DSDIN)=GPIO8, DIN<-codec(ASDOUT)=GPIO10. PA enable=GPIO46.
  Codec I2C on shared bus SCL=GPIO14, SDA=GPIO15.
- **Display/touch:** QSPI AMOLED + a **TCA9554 I/O expander** (drives LCD_RESET,
  TP_RESET, DSI_PWR_EN) + I2C touch. This is the most involved BSP of the three
  (QSPI panel + expander), but Waveshare ships a demo to adapt.
- **Size:** compact 1.8" - good for tight space.

**Trade-off:** more BSP work (QSPI + expander) in exchange for the cleanest mic attach.

### 2. ESP32-S3-Touch-LCD-1.83

- **Best display path:** a plain **SPI LCD** (ST7789/JD9853-class, like the current
  1.47 board) + I2C touch. Closest to the existing BSP, least porting effort.
- **Audio:** ES8311 (speaker DAC) + ES7210 4ch ADC (two onboard mics + AEC) +
  NS4150B. Has a real **2-pin speaker connector J3** (nicer than the 1.8's soldered
  speaker).
- **Mic attach - the weak point:** no dedicated external-mic pad. A boom mic must be
  soldered onto the tiny 0402 mic coupling caps (ES7210 MIC1 via 0R `L4`, or the
  ES8311 mic node) and the onboard mic isolated by lifting its 0R jumper. Workable but
  fiddlier than P1/P2 pads.
- **Pins:** I2S on GPIO8/9/16/45 + PA_CTRL=GPIO46 (graphical schematic - confirm exact
  mapping against the demo).
- **Size:** compact 1.83" round.

**Use this if** the QSPI/expander BSP on the 1.8 proves too costly and we would rather
solder to mic caps to keep the easy SPI display.

### 3. ESP32-S3-Touch-AMOLED-2.16

- Same audio topology as the 1.83 (ES8311 + ES7210 4ch + NS4150B), with a **2-pin
  speaker connector P10**. Exact pins: I2S MCLK=GPIO42, BCLK=GPIO9, WS=GPIO45,
  DOUT=GPIO8, DIN=GPIO10; PA_CTRL=GPIO46; I2C SCL=GPIO14/SDA=GPIO15.
- **Display:** QSPI AMOLED, 24-pin FPC (J4). No I/O expander, so simpler than the 1.8's
  display, but it is a **large 2.16" round panel** - likely too big for a tight-space
  prop, and the most pixels to re-layout the UI for.
- **Mic attach:** same as the 1.83 - no dedicated pad, tap caps + lift 0R.

**Likely ruled out by physical size.** Largest board/display of the three.

---

## Bottom line

If the cleanest boom-mic attach is the deciding factor (it is, per the handoff), pick
**AMOLED-1.8**: P1/P2 pads on the ES8311 mic input, compact, single-codec full-duplex.
Accept the QSPI + TCA9554 display BSP as the cost.

If we would rather minimize display porting and are willing to solder onto mic coupling
caps, **LCD-1.83** is the fallback (easy SPI display, speaker connector).

**Next confirmation step:** pull the Waveshare demo repos (github.com/waveshareteam)
for the chosen board to confirm the codec I2S/I2C pin map above and that the demo runs
ES8311 full-duplex before committing.

---

## DECISION (2026-06-15): AMOLED-1.8 chosen + solder plan

User picked **ESP32-S3-Touch-AMOLED-1.8** and is ordering it to desolder/probe and take
close-up pad photos. Final solder plan, both taps reusing onboard circuitry:

- **Boom mic -> the onboard mic's two pads** (MIC_P/MIC_N into the ES8311). Existing
  codec bias + input network is reused. Optionally lift 0R jumper `L5` to isolate the
  onboard mic; leaving it in parallel is fine (output quality does not matter).
- **Output -> the onboard speaker's two pads** (PA_OUTL+/- from NS4150B via 0R jumpers
  L3/L4). **Bridged class-D: both wires hot, neither GND** - feed the secondary amp
  differentially, never ground one side; trim level (it is ~1W speaker drive). Onboard
  speaker may stay (parallel 8R) or be lifted.

Notes from investigation:
- The board's broken-out castellated header carries only power/UART/I2C/GPIO + USB - NOT
  the audio signals. Mic/speaker taps are on the board body, not the header.
- The Waveshare 3D STEP (`ESP32-S3-Touch-AMOLED-1.8-3D.stp`) confirms component bodies
  (MIC1, L5, L3, L4, U10=NS4150B) and their locations, but STEP carries no copper, pads,
  silkscreen, or net names. Waveshare publishes no Gerbers. Exact pad marking therefore
  waits on macro photos of the physical board.
