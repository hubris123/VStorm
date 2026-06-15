# Voice Changer — Solder Pinout & Wiring

Board: Waveshare ESP32-S3-Touch-LCD-1.47 · Power: USB-C only
Jacks: RLECS 3.5 mm 4-pole (TRRS) panel-mount, gold-plated

> **HARDWARE DIRECTION (decided 2026-06-12): moving to premade modules on a custom
> carrier PCB.** Mic = **MAX4466** (manual gain) into ADC GPIO4. Output = **PCM5102A
> I2S DAC** (line level) — replaces the sigma-delta path; firmware output must be
> rewritten to I2S. Custom PCB stacks behind the Waveshare board via the P1 header,
> jacks edge-mounted. Design both a module-carrier (Option A, prototype) and an
> integrated-chip board (Option B, production). The discrete-passive wiring below is
> the LEGACY sigma-delta path — kept as fallback only.
>
> **New module wiring:**
> - MAX4466: VCC→3V3 (P1 pin 8), GND→GND (pin 3/4/6), OUT→GPIO4 (P1 pin 17)
> - PCM5102A I2S: VIN→3V3 or 5V, GND→GND, BCK→GPIO5 (pin 19), LRCK→GPIO6 (pin 21),
>   DIN→GPIO7 (pin 22), SCK→GND (use internal PLL). Module L/R out → output jack tip,
>   GND → sleeve. (3.5mm jack wiring unchanged: Tip+R1+R2=signal, Sleeve=GND.)

---
## LEGACY sigma-delta discrete wiring (fallback)

## Where to solder on the board — P1 header (verified from Waveshare schematic)

All connections go to the 22-pin P1 header. Relevant pins:

| P1 pin | Signal | Used for |
|---|---|---|
| 3, 4, 6 | GND | mic + output grounds |
| 8 | 3V3 | mic bias supply |
| 17 | **GPIO 4 (IO4)** | MIC IN |
| 19 | **GPIO 5 (IO5)** | AUDIO OUT |

(Full P1 order: 1 VBUS, 2 VBAT, 3 GND, 4 GND, 5 TXD, 6 GND, 7 RXD, 8 3V3, 9 EN,
10 SCL, 11 IO1, 12 SDA, 13 IO2, 14 IO11, 15 IO3, 16 IO10, 17 IO4, 18 IO9,
19 IO5, 20 IO8, 21 IO6, 22 IO7 — odd pins one row, even pins the other; verify
the printed silkscreen numbers before soldering.)

Buttons confirmed from schematic: Key1 = chip EN (reset), Key2 = GPIO 0 (our BOOT
multifunction button).

## Jack lug identification (do this first!)

The 4 solder lugs are unlabeled and order varies. Map them with a multimeter:
1. Insert a 3.5 mm plug (any TRRS cable) into the jack.
2. Continuity-beep each lug against the plug: **Tip** (very end), **Ring1**, **Ring2**,
   **Sleeve** (longest, nearest the cable).
3. Mark the Tip and Sleeve lugs with a sharpie.

**Both jacks:** solder Ring1 + Ring2 lugs bridged to the Tip lug, so TRRS, TRS, and
mono TS plugs all work.

Tip: solder with a plug inserted to keep contacts aligned while hot. You have spares.

## MIC INPUT jack (electret boom mic)

| Jack lug | Connects to |
|---|---|
| Tip (+R1+R2) | Mic signal node (see circuit) |
| Sleeve | Board **GND** |

```
3V3 ────2.2kΩ────┬───────────────► jack TIP (mic + / bias)
                 │
              1 µF cap (+ side toward jack if polarized)
                 │
   GPIO 4 ───────┼───10kΩ─── 3V3
                 │
                 ├───10kΩ─── GND
                 │
              10 µF ── GND   (bias decoupling)
GND ─────────────────────────► jack SLEEVE
```
- GPIO 4 = ADC1_CH3. The two 10k resistors center the audio at ~1.65 V.
- Keep mic wiring short / twisted; it's the noise-sensitive side.

## AUDIO OUTPUT jack (line level → amplifier)

| Jack lug | Connects to |
|---|---|
| Tip (+R1+R2) | Output of RC filter (see circuit) |
| Sleeve | Board **GND** |

```
GPIO 5 ───1kΩ───┬───10 µF(+ toward GPIO side)───► jack TIP (line out)
                │
             100 nF
                │
               GND
GND ────────────────────────────────────────────► jack SLEEVE
```
- GPIO 5 carries the sigma-delta modulated signal; the RC pair turns it into analog.
- The 10 µF blocks DC so the amp sees a clean AC line-level signal (~1 Vpp).

## Board pins used (do not repurpose)

| GPIO | Function |
|---|---|
| 4 | **MIC IN (you solder)** |
| 5 | **AUDIO OUT (you solder)** |
| 0 | BOOT button (mute / lock / factory reset) |
| 38,39,21,45,40,46 | LCD (SCK, MOSI, CS, DC, RST, backlight) |
| 41,42 | I2C SCL/SDA (touch, IMU) |
| 47,48 | Touch INT/RST |
| 13–18 | SD card slot |

Spare GPIOs if 4 or 5 turn out not to be broken out on your board revision:
1, 2, 3, 6, 7 (all ADC1-capable; any GPIO works for sigma-delta out).
