# GLIDE

**A continuous-pitch polyphonic slide instrument that lives in your pocket.**
Firmware for the M5Stack Cardputer ADV.

This is the second incarnation of the instrument from the brainstorm — the
"pitch touch bar," the "digital slide whistle," STRATA-1's little sibling.
The touchscreen prototype proved that sliding chords on a continuous-pitch
surface is *absolutely sick*. This build answers a different question:
can the soul of that instrument survive on a device with 56 mechanical keys
and a one-watt speaker?

It can. Here's the trick.

## The translation

The Cardputer's keyboard is a 4×14 matrix with staggered rows — physically,
it's already a tiny fretboard. So GLIDE treats it like one:

- **Rows are strings.** Four of them, tuned a fourth apart (configurable),
  bottom row lowest. Columns step up the scale. It's an isomorphic grid —
  the same layout idea as the LinnStrument, the closest living relative of
  the instrument we sketched.
- **Continuous pitch lives in time instead of space.** On glass you slid
  your finger; here, the *notes slide themselves*. Every legato transition
  portamentos: hold a chord shape, re-finger the same shape three columns
  over, and every voice glides continuously to its new target. That's the
  chord slide — the spark from the original conversation — preserved.
- **Bend keys** (`[` and `]`) push the pitch continuously up or down while
  held, like bending a string. Between glide and bend you can hit anything
  between the twelve western notes. Microtonal, fretless, the whole point.

## The keymap

```
 string 3 (hi) |  1  2  3  4  5  6  7  8  9  0  |  - oct-   = oct+   bksp PANIC
 string 2      |  q  w  e  r  t  y  u  i  o  p  |  [ bend-  ] bend+  \  hold latch
 string 1      |  a  s  d  f  g  h  j  k  l  ;  |  ' scale lock      enter tilt
 string 0 (lo) |  z  x  c  v  b  n  m  ,  .  /  |  space sustain

 `     exit                 fn (hold)    quick-edit layer
 tab   settings             shift (hold) momentary chromatic
 ctrl/opt octave -/+ (left thumb)        alt sustain (left thumb)

 fn + q..p         : switch between the ten sounds, live
 fn + shift + q..p : save your current tweaks over that slot
 fn + 1..0         : pick a parameter, [ ] to adjust
```

**How you play it:**

- Press keys. It sounds good immediately — scale lock is on by default
  (A minor pentatonic, degree-mapped: *every key is a scale tone, there are
  no dead keys, and sliding a shape sideways is a diatonic transposition*.
  You can't really hit a wrong note. That's deliberate — it's the same thing
  that happens when you connect the pentatonic boxes across a guitar neck).
- **Hammer-on:** press a new key on the same row while holding one — the
  voice *glides* there. **Pull-off:** release it — the voice glides back.
  Each row behaves like a real string.
- **Slide a chord:** hold a shape across rows, then re-finger it elsewhere
  while the old notes still ring. Every voice glides. This is the thing.
- **Hold `shift` to break out of the scale** — pure chromatic semitones,
  only while held. This is the skill gate: the scale keeps beginners safe,
  shift is how you earn the notes in between.
- **`fn` + top row** selects a parameter (glide, ADSR, wave, cutoff, voices,
  bend range, volume); `[` `]` adjust it live. Nothing is hardcoded — every
  sound parameter has a control, and everything persists across reboots.
- The **oscilloscope** is live — that's the actual output waveform, with a
  phosphor afterglow. The note readout tracks the lead voice in cents,
  *through* glides and bends, so you can see exactly where you are between
  the notes.

## The sounds

Ten instruments on `fn`+`q`..`p`, each with its own tilt personality.
A `*` in the status bar means you've saved your own version over the slot
(`fn`+`shift`+letter); *Sound reset* in settings restores factory.

| key | sound | character | tilt does |
|-----|-------|-----------|-----------|
| q | **GLIDE** | the signature saw | filter |
| w | **WHISTLE** | the digital slide whistle itself — sine, always gliding | vibrato |
| e | **PLUCK** | filter-envelope pluck, slides on overlap | filter |
| r | **BASS** | square + heavy sub-octave, dark | filter |
| t | **ACID** | resonant squelch — *lean into it, tilt is the wah* | filter (full) |
| y | **PAD** | fat detuned wash, slow bloom | filter (gentle) |
| u | **LEAD** | driven, singing vibrato | vibrato |
| i | **ORGAN** | square + sub, instant — *tilt is the swell pedal* | volume |
| o | **GHOST** | breathy sine + noise, everything slow | filter |
| p | **PERC** | pitched noise chirp, a drum you can slide | filter |

Under the hood these ride five engine additions: a paraphonic **filter
envelope** (retriggered by fresh attacks, never by legato hand-offs — your
slides stay smooth), a **sub-oscillator**, env-gated **noise**, **drive**
into the soft clipper, and built-in **vibrato**. All of it editable live
(quick-edit + settings) and saveable per slot.

## Tilt

The gyro debate, resolved as agreed — and then promoted, because in
practice it's fantastic. Tilt is an *assignable* effects modulator, toggled
with `enter`, **never pitch bend** (nobody wants to lean the instrument
over again). What's new:

- **Per-sound personality**: every patch ships with its own route and depth
  — ACID tilts into a full wah, ORGAN tilts into a swell pedal, WHISTLE and
  LEAD tilt into vibrato. Saving a slot saves its tilt setup too.
- **Depth** (settings): how hard the motion drives the effect, 0–100%.
- **Center calibration** (settings → *Tilt center*): "flat" becomes
  wherever *you* naturally hold the thing, not wherever gravity says.
  Adjust the item while holding the instrument in playing position.

## The layering jam (drones)

The brainstorm's "one hand plays the backing, the other solos over it,"
solved the way continuous-pitch instruments have always solved it — not
with a second chord interface, but with **drones** (sitar, bagpipes,
hurdy-gurdy lineage). Settings → *Jam rows*:

- The bottom one or two rows become **tap-to-latch drones**: tap a key and
  it rings an octave down, hands-free, until you tap it again. Lay down a
  root, or root+fifth, and solo on the rows above.
- Drones are protected: they don't count against the lead's voice cap and
  chord-slide stealing can never grab them. Your backing survives anything
  your solo hand does.
- When you release one, it fades with a long drawn-out tail instead of
  stopping dead under your solo.
- Octave shifts sweep the drones along with everything else; panic (bksp)
  clears them.

Off by default — the uniform grid is still the instrument you learn on.
Jam rows are an arrangement you switch on when it's time to perform.

## The philosophy, encoded

- **The skill gap is the product.** Basic play takes minutes (scale lock +
  degree mapping = first session sounds good). Mastery — clean legato
  overlaps, accurate shape re-fingering, controlled bends into chromatic
  passing tones, two-row voice management under the 4-lane limit — takes
  honest practice. The gap between what you hear in your head and what your
  fingers can do closes slowly, the way it's supposed to.
- **Nothing hardcoded.** 20+ parameters, all editable on-device (quick-edit
  layer for the performance-critical ten, settings screen for the rest),
  all persisted to NVS with debounced writes.
- **Failures are visible.** If the audio path can't start you get a red
  AUDIO INIT FAILED screen with the reason — never a silently dead
  instrument. Rejected changes (octave ceiling, etc.) flash red in the HUD.
- **Minimal onboard effects.** A lowpass filter with resonance, soft
  saturation, and a speaker-protecting highpass. Loop and process
  externally, like we said — the instrument's identity is its sounds and
  how it plays, not an effects rack. (Omnichord rule.)

## Quick start

**Via Launcher (recommended):** flash [bmorcelli's Launcher](https://github.com/bmorcelli/Launcher)
once, copy `dist/GLIDE.bin` to `/apps/` on the microSD, boot → SD → GLIDE.

**Direct USB (overwrites Launcher):**
```
pio run -t upload
```
Entry procedure: power OFF, hold G0, plug USB-C, release G0.

No WiFi, no accounts, no setup. Power on → splash (the boot chime is a
single note gliding up an octave, played through the synth itself) → play.

**Persistence & reset:** everything you touch — sounds, tweaks, octave,
scale, tilt setup, jam rows — saves to flash moments after you change it
and survives reboots *and* firmware updates (NVS lives outside the app
partition). Three ways back:
- settings → *Sound reset* — current slot back to factory
- settings → *Reset defaults* — all settings back to factory (your saved
  sounds are kept)
- **hold backspace while powering on** — full factory reset, settings AND
  saved sounds, even if stored state ever wedges the UI

## Before you trust it: the Phase 0 probe

Two hardware assumptions need validating on *your* unit before the
instrument's behavior can be trusted. The probe firmware tests both —
flash it the same way as the instrument (copy `dist/GLIDE-probe.bin` to
`/apps/` on the SD for Launcher), or direct:

```
pio run -e phase0-probe -t upload
```

1. **Gapless audio** — streams a 440 Hz sine through the same 3-buffer
   playRaw loop the instrument uses. The `STARVED` counter must stay 0
   (green) for minutes. Press `space` to inject a deliberate 6 ms stall and
   prove the DMA headroom is real.
2. **Key rollover** — mash chords; every key the keyboard controller
   reports lights green on the 4×14 grid, and `max seen` records your
   ceiling. The ADV's TCA8418 should do much better than the old matrix's
   ~3 keys. **Whatever your ceiling is, set `voices` (fn+8) at or below
   it.** Findings worth recording here:

   | unit | starved (5 min) | max rollover | date |
   |------|-----------------|--------------|------|
   | _your Cardputer ADV_ | _?_ | _?_ | _?_ |

## Building

```
pio run                    # instrument -> dist/GLIDE.bin
pio run -e phase0-probe    # hardware probe
pio run -e native          # pure-DSP host tests (no hardware needed)
.pio/build/native/program  # run them
```

PlatformIO, `espressif32@6.12.0`, `m5stack/M5Cardputer@^1.1.1`. Serial
monitor at 115200 (`pio device monitor`).

## Architecture (why it's split this way)

```
src/
├── dsp/        PURE C++ — no Arduino, no M5, no ESP-IDF. The instrument:
│               voices, glide engine, wavetables, filter, pitch math,
│               degree mapping. Compiles and tests on a PC (env:native).
├── io/         The hardware boundary: M5Unified playRaw streaming
│               (render task on core 0), positional keyboard reader,
│               IMU tilt. The ONLY code that knows it's on an ESP32.
├── ui/         Perform screen (scope/readout/grid-map/HUD), settings,
│               splash. Core 1, ~30 fps canvas.
└── storage/    NVS persistence, debounced.
```

The `dsp/` purity rule is the point: when this instrument grows into real
hardware — Daisy Seed brain, force-sensing strips, the deformable surface —
the entire musical core moves over unchanged. The Cardputer is incarnation
two; it won't be the last.

Audio path facts (verified against M5Unified source, not vibes):
`playRaw` keeps a *pointer* (no copy) and queues 2 per channel, so GLIDE
rotates 3 buffers and paces on `isPlaying()`. 32 kHz / 128-sample blocks =
4 ms cadence, ~12 ms output latency, under 25 ms key-to-ear. M5Unified owns
the ES8311 codec's undocumented power-up sequence, which is why this
firmware never touches raw I2S and why the library versions are pinned.

## Parameters

| param | range | default | where |
|---|---|---|---|
| glide time | 0–2000 ms | 120 | fn+1 |
| attack / decay / sustain / release | 0–2s / 0–2s / 0–100% / 0–3s | 5ms / 120ms / 70% / 250ms | fn+2..5 |
| waveform | sine, tri, saw, sqr, fat | saw | fn+6 |
| cutoff / resonance | 80–12k Hz / 0–95% | 4k / 30% | fn+7 / settings |
| voices | 1–8 | 6 | fn+8 |
| bend range / bend time | 1–12 st / 50–1000 ms | 2 st / 250 ms | fn+9 / settings |
| volume | 0–100% | 70% | fn+0 |
| root / scale / row interval | C–B / 8 scales / 1–12 st | A / min pent / 4th | settings |
| glide mode | legato-only / always | legato-only | settings (per sound) |
| allocation | strings (mono rows) / free poly | strings | settings |
| jam rows (drones) | off / bottom / bottom 2 | off | settings |
| octave keys | sweep (glide) / re-strike | sweep | settings |
| sound slots | 10, factory + user overrides | GLIDE | fn+q..p, fn+shift+q..p |
| filter env (atk/dec/depth) | 1ms–2s / 10ms–2s / 0–3+ oct | per sound | settings*/saved in sound |
| sub / noise / drive / auto-vib | 0–1 / 0–1 / 1–8 / cents | per sound | saved in sound |
| tilt routing | off / cutoff / vibrato / volume | per sound | settings, enter toggles |
| tilt depth | 0–100% | per sound | settings |
| tilt center | calibrated "flat" | 0 | settings (hold + set) |

---

*"What fosters the most creativity? I think that's probably the way we
should go."* — that was the closing question, and this is the answer we
keep testing against: an instrument cheap enough for anyone, easy enough to
sound good tonight, deep enough to be worth twenty years.
