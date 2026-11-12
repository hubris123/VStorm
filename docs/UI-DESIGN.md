# VStorm UI design spec — Imperial console style

Step 1 of the UI plan: research → rules we build to. Steps 3–4 demo and build against this.
Target: ESP32-S3-Touch-AMOLED-1.8, 368 × 448 px, LVGL 8.4, touch assumed working.

---

## 1. What the Imperial look is

**From sources** (search results, Nov 2026):
- The 1977 Death Star briefing graphics (Larry Cuba, on a Vector General vector display) used
  only four "colours": black, white, light grey and dark grey — wireframe, no shading.
  ([CDM](https://cdm.link/2008/04/23/larry-cuba-star-wars-death-star-cg-arabesque-and-the-dawn-of-computer-animation/), [Wikipedia](https://en.wikipedia.com/wiki/Larry_Cuba))
- Rogue One's screens (BLIND Ltd) took the most memorable original-trilogy screens and
  elaborated on them, deliberately kept minimal, and sit between modern sci-fi UI trends and
  the 1977 look. In-universe there are no keyboards. BLIND also did Andor's Imperial screens.
  ([Laughing Squid](https://laughingsquid.com/the-beautiful-user-interface-design-of-screens-featured-in-rogue-one-a-star-wars-story/), [Core77](https://www.core77.com/posts/59149/Rogue-Ones-Killer-UI-Designs), [HUDS+GUIS](https://hudsandguis.com/home/2026/andor-s2))

**Observed in the films** (our own reading, to check against the reference stills in §9):
- **Black field, sparse content.** Most of a screen is empty. One job per screen.
- **One hue per screen.** Red-on-black targeting, green or blue wireframe, amber text readouts.
  Colour is a status signal, not decoration.
- **Wall lamp panels.** Grey panels with rows of small square/rectangular lamps in red, green,
  blue and white; some blink slowly. This is the most recognisable Imperial motif — the
  current UI's light cluster is already this.
- **Hard geometry.** Square corners, chamfered (notched) corners, thin outline frames,
  segmented bars, tick marks, crosshairs, concentric arcs. No rounded "app" buttons, no
  gradients, no shadows, no glass.
- **Microtext.** Columns of small Aurebesh/number readouts that tick and scroll; they read as
  "the machine is working" without needing to be read.
- **Mechanical motion.** Things step, snap, blink and sweep. No bouncy easing.
- **Imperial vs Rebel.** Imperial: ordered, cold, grey/black with red, symmetric, clean.
  Rebel: scrappy, warm orange/yellow, cluttered, retrofitted. Stay on the Imperial side.

## 2. Palette (AMOLED: black is free and burn-in safe)

| Token | Hex | Use |
|---|---|---|
| `BG` | `#000000` | Everything. Pixels off. |
| `PANEL` | `#0C0F12` | Key faces, panel fills |
| `FRAME` | `#5C6166` | Outlines, ticks, inactive text |
| `TEXT` | `#C9CDD0` | Primary readable text (never pure white: burn-in, glare) |
| `RED` | `#C4262E` | Alert, TX/keyed, LIVE output, destructive |
| `GREEN` | `#3C8A44` | Safe/ok, meter low range |
| `BLUE` | `#459FC2` | Selection, active control, info |
| `AMBER` | `#E0A526` | Readouts, popups, values being edited |

Rules: a screen uses `FRAME`/`TEXT` plus **one** accent for its main state; lamps may use all
four. Red always means "transmitting / live / danger". Keep large areas black.

## 3. Type

| Role | Face | Size |
|---|---|---|
| Labels, keys | Condensed square sans, all caps, +1–2 px letter spacing | 20–24 px |
| Big readout (values, callsign) | Same face, tabular digits | 32–40 px |
| Callsign (top bar, main + lock) | Aurebesh-English hybrid | 42 px; steps down to 34 / 26 / 22 px so long callsigns end before the TX label (x 46–242). TX lamp x 276, CLIP lamp x 340 (right edge 354 = battery bar) |
| Microtext / decoration | Aurebesh (Tiny TTF, already in repo), letter spacing ≈ 0.22 em + 15 % horizontal stretch, letters only (its digits render as tiny marks) | 12–16 px |
| Fallback | Montserrat (built in) | — |

- Candidate Latin faces (OFL, free for commercial use; convert with `lv_font_conv`):
  **Oxanium**, **Chakra Petch**, **Share Tech Mono**. Pick in the Step 3 demo.
- Minimum readable size on this panel: 16 px (≈1.3 mm cap height). Anything the user must
  read at arm's length: 20 px+.
- Aurebesh is decoration and the lock screen only — never the only label on a control.
- Key labels: main word 18 px; sub-label 14 px in light grey; a label too wide for its key wraps onto two lines (e.g. "TAP FOR / LIVE") rather than shrinking, 12 px only as a last resort,
  `#A9B0B5`, never the dark `FRAME` grey. Other notes ≥ 13 px. Arrows and − / + are drawn
  as shapes about 22 px across, not font glyphs.

## 4. Touch rules (the screen is small)

Panel: 1.8" diagonal, 368 × 448 → **≈322 ppi, 12.7 px per mm**, ≈29 × 35 mm visible.

| Rule | Value |
|---|---|
| Primary key (LIVE, LOCK, − / +) | **≥ 9 mm → ≥ 114 px** on the short side |
| Secondary key | ≥ 7 mm → ≥ 90 px |
| Gap between keys | ≥ 2 mm → ≥ 24 px (or one key's dead border) |
| Keys per row | ≤ 3 (3 × 104 px + 2 × 12 gaps + 2 × 16 margins = 368) |
| Corner dead zone | ≈ 45 px radius — nothing tappable or readable there |
| Edge keys | Keep ≥ 14 px off straight edges |

Sources: Material/Android 48 dp ≈ 9 mm, Apple 44 pt ≈ 7 mm, small-touchscreen research
9.2–9.6 mm, edges/corners need more (Hoober: 9 mm edges, 12 mm corners).
([Android](https://support.google.com/accessibility/android/answer/7101858), [W3C](https://www.w3.org/WAI/GL/mobile-a11y-tf/wiki/Proposed_SC_Target_Size), [web.dev](https://web.dev/articles/accessible-tap-targets))

Layout budget (see `design/touch_grid.png`, generated by `design/make_touch_grid.py`):
- Two rows of 3 primary keys (104 × 114 px) use the whole bottom half. A screen holds about
  **5–6 keys at most**; everything else is display.
- Bottom row on EVERY screen (main, lock, settings, callsign): housings x 16 / 132 / 248, 104 × 114 at y 320–434, so keys never jump between screens; the row above (settings −/+, callsign ▲▼, wide keys) is y 204–308. Housings (bottom 14 px off the edge), so the outer keys' 9 px corners sit ≈ 6 px inside the panel's 45 px corner curve and follow it.
- The bottom row's outer keys sit beside the rounded corners. Prefer **2 wide keys** in the
  bottom row (e.g. LOCK | LIVE, each ≈ 160 × 114 px), and lift 3-key rows above it.

Interaction rules that follow:
- **No thin sliders.** Values change with big − / + keys and a segmented bar readout.
  Hold − / + to repeat.
- **Hit area ≥ drawn area.** A key may look like a 90 px frame but its touch area fills the cell
  (LVGL `ext_click_area`).
- **Press feedback within one frame:** key face fills with its accent while pressed.
- **Keys are console hardware:** housing radius ≈ 9 px, cap radius ≈ 6 px, bevelled cap,
  panel screws; lit keys glow around the edge (never over the label). Display frames stay
  square/notched.
- **Destructive = hold.** Factory reset and similar: hold 2 s with a filling bar, no tiny
  confirm buttons.
- **One-thumb reach:** the most-used keys sit in the bottom half.
- **Never touch-only for the essentials.** BOOT keeps MUTED ↔ LIVE so it works blind/gloved.

**Key labels always fit.** Every key label stays at least 7 px inside its cap. Several words
that don't fit go on two lines (the main label moves up to make room); a single word that
doesn't fit is squeezed horizontally (same height, narrower), never shrunk to a smaller size.
Check with `sim/tools/tour-keys.txt` (`capcheck` reports any label past the margin).

## 5. Screens

| Screen | Job | Main controls |
|---|---|---|
| **Main** | Am I live? Am I heard? | Scope + TX lamp (top), big LIVE/MUTED key, LOCK key, SETTINGS key |
| **Settings** (paged) | Tune effect | One value per page: big name, segmented bar, − / + keys, ◀ ▶ to page. Pages (14): VOLUME, VOICE, MIC GAIN (0-30 dB boost before everything; device default 0, sim +12 for browser mics), EFFECT, SOFT CLIP (0-100: rounds loud peaks instead of cutting them square - on the boosted mic, after the drive, and on the scope trace), VOX, STATIC, CLICK, BRIGHTNESS, CALLSIGN, FACTORY RESET (red, hold 2 s), DISPLAY OFF (yellow, tap), SENSORS (toggles), SKIN (carousel) |
| **Callsign** | Set "TK-421" | Code-cylinder: 1 character at a time, big ▲ ▼ for the character, ◀ ▶ for position, COMMIT |
| **Battery bar** (main + lock) | Charge at a glance | Unlabelled 8 px bar right of the voice meter (x 346–354, same span y 49–178). Drains from the top; green, yellow ≤ 30 %, red ≤ 15 %. Hidden on USB with no battery. AXP2101 |
| **Tilt lines** (scope, main + lock) | Motion, "alive" | One vertical + one horizontal line in the scope grid, `#46525A` (a shade whiter than the grid), behind the trace, gliding with roll (x) / pitch (y); ±45° reaches the edge. QMI8658 |
| **Secret temp** (main + lock) | Only the owner knows | Readout line 1 (top, over SYS) is pinned to "TEMP" + unit in Aurebesh with the number in the callsign's TK-style digits (Aurebesh-English 20 px, sharing the Aurebesh baseline: digits and letters both 13 px tall, same pixel rows), e.g. TEMP 88 F. Board temperature from the QMI8658 (reads warmer than the air - "suit temp"). The other three lines keep changing |
| **Voice** (settings page 2 of 14) | Sound like a trooper | Panel: AUTO VOICE state, a pitch map (70-320 Hz) with the target band, YOU and the shift arrow, the saved PITCH (semitones) and TONE (%). The TROOPER band on the map is ±1 semitone around 125 Hz. The shift also narrows the wearer's pitch swings by 30 % around their learned pitch (VOICE_RANGE 0.7), so they stay in the band but still sound alive. Keys: AUTO (unavailable, "LEARN FIRST", until a learn is accepted), TARGET (TROOPER; CLONE later), LEARN. LEARN opens a screen: a scrolling script (the next line comes once you have read one and paused) while the shift builds up live (heard even while LIVE is muted), STOP / PAUSE / DONE. No fixed reading time: a LOCKING ON bar fills as the estimate settles, and it finishes by itself once 3 s of speech have been heard and the last 3 s of estimates agree (pitch within 0.6 st, tone within 2 %); the setting is the average of those last 3 s; then VOICE LEARNED (you vs target, the shift) with RETRY / ACCEPT / ADJUST; ADJUST = PITCH ±0.5 st and TONE ±2 %, heard at once, BACK / SAVE / RESET. Script lines name no characters; the word on screen is TROOPER |
| **Sensors** (settings page 13) | Turn them on/off | Top row: TEMP on/off, C/F unit (big amber F or C, sub-label "UNIT", never lit), readings panel (PWR / TILT / TEMP). Row A: BATTERY (14 px to fit), TILT / GRID and QUIET / ONLY on two lines (toggle names 16 px) (default on: tilt lines fade out while the VOX gate is open). All keys in the bottom-row columns. Defaults: all on, °F |
| **Sensor read rates** | Keep I2C quiet | Temperature every 30 s, battery every 10 s (cached; also read at start-up, when the Sensors page opens, and when a toggle changes). Tilt every frame (25/s) because the lines animate. Labels redraw only when the shown value changes |
| **Skin** (settings page 14) | Pick a look | Thumbnail carousel (previous / current / next; tap a side thumbnail or use < SKIN >), skin number + name, APPLY lights green when the shown skin isn't active (ACTIVE when it is). 36 built in: Imperial Standard, the 20 concepts (2-21) and 15 free-layout skins (22-36). A skin restyles MAIN + LOCK; settings keep the standard look |
| **Display off** | Blank the panel, keep working | All black except one dim amber line in the middle, "TAP 5 TIMES TO WAKE DISPLAY", drifting a few px every 20 s (burn-in). 5 quick taps wake to Main. Audio and BOOT keep working |
| **Lock** | Prop mode, ignore stray taps | Full-screen decoration (Aurebesh, lamps, live scope). Unlock: 5 rapid taps, as on the 1.47 |
| **Confirm** | Dangerous actions | Hold-to-confirm bar |

The 1.47 firmware's callsign roller (10 × 27 px dials) breaks the touch rules at this size;
that is why the callsign screen becomes one-character-at-a-time.

## 5a. Skin layout rules

Every skin keeps the top bar (callsign, TX, CLIP) and the LOCK / SYS / LIVE keys where they
are (colour and key style may change). The console area between them is free: any mix of
parts (skin.h): scope, round scope, tall scope, target view, waterfall, voice meter, bar
graph, ring meter, big gauge, battery bar, lamps, edge lamp strips, Aurebesh readout, rotors,
hex table, temp tag. Rules:

- **At least one live voice display** (scope, meter, bar graph, ring, gauge, waterfall...).
- **Small parts in the row above the keys are centred on the key centres** (x 68 / 184 / 300,
  within 3 px). Misaligned small parts look broken; big parts (gauges, tall scopes,
  waterfalls) may stand on their own.
- **The battery bar gets clear space**: at least 5 px from every other part.
- **The secret temperature lives in exactly one place per skin**: readout line 1, a gauge
  plate, a tag (plain digits, "RNG 88", "CH 88"), a rotor hub, or a hex cell (in hex: 88 F
  reads 58). It follows the TEMP on/off and F/C settings like the readout version.
  A temp tag is subtle but never touches other art or a trace's path: at least 4 px clear.
- **Radar / reticle blips drift** slowly (about 1 px/s) and stay inside their circle; a
  sweep's fading trail (or radar wedge) always trails the arm and swings over when it reverses.

## 6. Motion

- Lamps: slow drift between colours (existing), red pulse ≈ 6 s.
- Scomp link (spinning rings): outer 3-segment ring, inner rotor and blue tip each turn at
  their own speed and **reverse direction every 8–16 s (random interval, guaranteed
  reversal) at a new random speed (1–3×)**, independently, never stopping. Use a decent
  PRNG (xorshift32), not the low bits of an LCG.
- Readouts: lines hold still; about every 1.5–3 s ONE line changes to a new word. No scrolling,
  no whole-block flicker. (Hex codes on the lock screen may change faster.)
- Screen change: hard cut or a 120 ms horizontal wipe with a scan line. No fades.
- Key press: instant fill, 80 ms release.
- Respect frame rate: the SH8601 path redraws over QSPI; keep animated areas small.

**Soft landing:** voice displays (scopes, meters, bar graphs, waterfalls, gauges) rise with
the voice at once but float down: the shown level eases off over about 0.2 s instead of
dropping instantly, which reads as a natural voice trace.

## 6a. Reacting to the voice (VOX keyed)

Trigger is the **VOX gate** (open while speaking + ~350 ms hang), not the raw level, so it follows
speech, not room noise, and lines up with the key-up / key-down clicks. Changes **ramp**:
ease up over ~1.5 s, wind down over 2–3 s (kept calm: talking should not make the console busy). Nothing flashes per syllable (the meter and scope
already show the raw voice).

| Element | Idle | Keyed while MUTED | Keyed while LIVE (transmitting) |
|---|---|---|---|
| Scope frame | grey | **amber** | **red** (with a soft glow) |
| Voice trace (scopes, tall scope, bar graph) | **grey** while the gate is shut: the voice is heard but is below the VOX cut-off, so it isn't going out | trace colour | trace colour |
| Scomp rings | slow drift, random reversals | ~1.15× speed | ~1.5× speed; reversals continue |
| Lamp cluster | one change every 20–40 s | unchanged | a change every 6–12 s, one lamp flips at key-up |
| TX lamp (real) | off | off | steady red |
| Readout text | one line every few s | — | new line at each key-up ("logging the transmission") |

Demo: `design/mockups/main_alive.gif` (idle → talking muted → BOOT → talking LIVE → quiet).

## 6b. Rendering: background image + live layer

To keep redraws cheap on the SH8601 QSPI panel:
- **Background layer** — every static part of a screen (key housings + screws + empty cap
  wells, scope panel + grid, meter wells, Scomp socket plate + ticks, fixed labels like TX /
  CLIP / LOCK) is one pre-rendered 368 × 448 RGB565 image (≈ 330 KB). It is the default
  **skin**: later skins replace this image only.
- **Live layer** — only what changes is an LVGL object on top: callsign, TX/CLIP lamps, scope
  trace and frame, meter segments, lamp cluster, readout text, Scomp rings, lock cover/switch,
  key caps (pressed / lit). See `design/mockups/08_main_live_areas.png`.
- A change redraws only its own rectangle; behind it LVGL copies the matching patch of the
  background (a straight copy) instead of re-drawing gradients, rounded corners and glows.
- **Key caps as images**: off / lit / pressed are small pre-drawn images; a state change is one
  copy. Glows are baked into the lit image, never drawn live on animated objects.
- **Rates**: rings ≤ 20 updates/s, scope 25/s (40 ms), lamps ≤ 10/s, readout ≤ 1/s.
- **Memory**: images live in flash and are copied to PSRAM at boot (ESP32-S3R8: 8 MB octal
  PSRAM, enabled in firmware3). firmware3's sdkconfig says 4 MB flash with a 3 MB app, but the
  board ships with 16 MB (Waveshare factory images are 16 MB) — raise the flash size and add an
  assets partition when images go in.
- Measure on hardware with LVGL's performance monitor (currently off in firmware3).
- Background art is produced by `design/mockups/make_mockups.py`
  (`07_main_background_layer.png` is the main screen's layer).

## 7. AMOLED care (static UI = burn-in risk)

- Black background everywhere; static frames in `FRAME` grey, not white.
- Dim to ~30 % after 60 s with no voice and no touch; wake on VOX or tap.
- Shift static art by 1–2 px every few minutes (whole screen offset).
- Lock screen animates continuously (ticker, lamps) so nothing sits still for hours.

## 8. Keeping it ours (going to market)

A look and feel isn't protected, but these are Lucasfilm/Disney marks: the names "Star Wars"
and "stormtrooper", the Imperial crest/cog emblem, helmet silhouettes, and film logos.
Aurebesh comes from the films, and fan fonts carry their own licences.
- Product UI: no crest, no helmet, no franchise names. Use our own glyph set or an original
  "Imperial-style" script for the shipped version; keep Aurebesh for personal/free skins
  (matches the skins note in HANDOFF).
- Check the licence of `Aurek-Besh` / `Aurebesh-English` before shipping them in firmware.

## 9. Reference stills to collect (`design/refs/`)

Screenshots you take yourself, for internal reference only (not shipped):
1. Death Star briefing wireframe (A New Hope) — vector style, 4 greys.
2. Death Star corridor/control-room wall lamp panels.
3. X-wing/TIE targeting computer — red/amber on black, crosshair, range numbers.
4. Star Destroyer bridge consoles (Empire Strikes Back).
5. Rogue One: Scarif data vault / Imperial security consoles.
6. Andor: Imperial office terminals (ISB), TIE cockpit displays.
7. Any prop/console photo you already matched (the "console badge" the 1.47 colours came from).

## 10. Step 3 demo directions (all within this spec)

| Direction | Character | Main-screen idea |
|---|---|---|
| **A · Wall Panel** | Death Star corridor | Grey panel blocks, lamp grid, keys look like lit panel switches |
| **B · Targeting** | Red-on-black scope | Big circular/arc scope, crosshair, minimal keys outlined in red |
| **C · Bridge Readout** | Star Destroyer console | Thin grey frames, amber data columns, blue selection, Aurebesh microtext |

Each is built as real LVGL in the simulator with a 9 mm finger-check overlay; you pick one
(or mix), then Step 4 builds it into `firmware3/main/ui.c`.
