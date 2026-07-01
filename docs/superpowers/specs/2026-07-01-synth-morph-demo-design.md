# Synth morph + demo mode — design

Date: 2026-07-01. Design refined interactively; final shape below is what the
user approved ("its for the last sound before the current one … and G0
controls it if configured" — yes; the fn-hold lean gesture was cut because fn
mutes the play grid, so you couldn't play the notes being morphed).

## Synth morph — glide for timbre

**Mental model:** you're always ON one sound; morph leans toward the sound you
were just on. The pair is (current, previous) — snapshotted in
`applyPatchData`, the choke point every sound change (slot switch, roll, SD
load, undo/redo) already passes through. Invalid until the first sound change,
so morph can never reach for a sound that was never there.

**Controls (no new gestures):**
- **G0** with *Trigger action: synth morph* — hold to lean toward the previous
  sound (by *Trigger depth*), release to come home. Latch mode = tap to become
  it / tap to return.
- **Sound switches** (`fn`+q..p) arrive by the same blend played in reverse:
  state lands on the new sound instantly (names/saves/dirty semantics all
  unchanged), the audible blend starts at the old sound and glides home.
- One new setting: **Morph time** (LAYOUT, 0–2000 ms, default 300; 0 = the old
  snap). One time constant for the switch transition and the G0 sweep rate.

**Feedback:** a strip top-center whenever the blend is off-center —
`current ◄▓▓░ previous` — current in green (the live side), previous in amber.
The pair is always named on screen, never remembered.

**Semantics:** morph is performance state like bend/tilt — computed per-frame
into the lead param copy, never persisted, never baked into a save; the
jam/backing bed never blends. Interpolation is pure dsp
(`dsp/morph.{h,cpp}`, host-tested): geometric lerp for Hz/time params, linear
for mixes, discretes switch at the midpoint, mod slots crossfade depth
out/swap/in, voiceCount and live-mod fields always ride from the current side.

## Demo mode

Settings → SYSTEM → *Demo mode* ("play itself"). Zero config; uses the live
key, scale and tempo.

- Spells I-IV-V-IV on the REAL progression engine (via `keys::progAppendStep`)
  if no bed exists; an existing progression is reused.
- Improvises over it with `dsp/demo_gen.{h,cpp}` — seeded, deterministic,
  host-tested phrases: stepwise motion, ~half the moves legato slides
  (Retarget), leaps, breathing rests. Melody id 200 (clear of all lanes),
  pitched via `gridToMidi` on "string 2" so it's always in key above the pad.
- Every 4 bars it wanders the curated bank (q..i) with the real hot-swap
  (backing holds its sound), the identity card shows the new face, and the
  synth morph glides the transition — the features compose into the showcase.
- Blinking DEMO badge + hint line "playing itself - any key takes over".
- **The exit is the feature:** any grid key stops the melody instantly but the
  bed keeps looping — you take over the jam. Opening settings also stops the
  melody (the bed survives).

## Verification

Native tests cover morphParams (endpoints exact, geometric cutoff, midpoint
discretes, slot crossfade, live-field ownership) and DemoMelody (determinism,
range bounds, durations, slide/rest distribution). Device build compiles
clean; feel (morph sweep musicality, demo phrasing) is judged on hardware.
