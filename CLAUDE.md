# GLIDE — agent notes

Synth firmware for the M5Stack Cardputer ADV. Read README.md first; it
carries the design intent (the keymap, the philosophy, the audio-path facts).

## Build / test

```
pio run                    # instrument -> dist/GLIDE.bin (Launcher SD: /apps/)
pio run -e phase0-probe    # hardware risk probe (audio gapless + key rollover)
pio run -e native          # host build of dsp/ + tests
.pio/build/native/program  # run the tests — must pass before any dsp/ change lands
```

On this machine `pio` lives at `~\.platformio\penv\Scripts\pio.exe` and the
native env needs `~\.platformio\packages\toolchain-gccmingw32\bin` on PATH.

## Hard rules

1. **`src/dsp/` is pure C++.** No Arduino.h, no M5*, no ESP-IDF, no
   `millis()`. It must keep compiling in `env:native` (gnu++14, and keep it
   C++11-friendly: no inline variables, no aggregate-init of structs with
   default member initializers — use `NoteEvent::make()`). This is the
   porting boundary for future dedicated hardware.
2. **Audio = M5Unified playRaw only, never raw I2S.** M5Unified owns the
   ES8311's undocumented power-up sequence. `playRaw` stores the *pointer*
   (no copy) and queues 2/channel → the 3-buffer rotation in
   `io/audio_engine.cpp` is load-bearing; don't reduce it.
3. **Failures must be visible.** Audio init failure → full-screen red error
   (`fatalAudio` in main.cpp). Never let the instrument be silently dead.
4. **Keyboard is read positionally** (`keyList()`, codes `y*14+x`), never via
   the char `word` — chars mutate under shift, which would break the
   momentary-chromatic gesture.
5. **Tilt is never pitch bend.** Rejected by the humans, on tape.
6. Library versions are pinned (`m5stack/M5Cardputer@^1.1.1`,
   `espressif32@6.12.0`) to match the verified Speaker_Class source. Don't
   bump without re-running the phase0 probe on hardware.

## Conventions (inherited from the sibling firmwares in ../CardPuter Custom)

- `` ` `` = exit, full-frame M5Canvas pushed once per frame (~30 fps), NVS
  keys ≤15 chars, Preferences namespace "glide", dist binary via
  support/copy_dist.py.

## Adding a sound parameter (the expansion-safe way)

Patches are a **tagged format** (`storage/patch_codec.{h,cpp}`), so adding a
`SynthParams` field never wipes saved patches. To add one:
1. Add the field to `dsp::SynthParams` with a **neutral default** (so the stock
   tone is unchanged and `test_dsp.cpp` keeps passing).
2. Give it a **new, never-reused tag** in `patch_codec.cpp`'s `Tag` enum + a row
   in `buildTable` (append-only — never renumber a tag).
3. Persist the live value as a flat NVS key in `glide_config.cpp`
   `begin()`/`persistNow()` (absent key → the neutral default).
4. Add a `format`/`adjust` pair + a `kItems[]` row in `settings_screen.cpp`.
5. Consume it in `synth.cpp` (per-block is ~free; avoid per-sample).
The modulation matrix (LFOs / mod-env / 6 routing slots) and the multimode
filter were both added this way — copy them as the pattern.
