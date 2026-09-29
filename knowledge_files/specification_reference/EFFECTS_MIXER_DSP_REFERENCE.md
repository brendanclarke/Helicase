# Effects and Mixer DSP Reference

What happens to audio after an instrument voice renders it: decimation,
gain, pan and routing, the FX bus and the Effect's DSP, the Effect return,
and the path to the two DACs. It covers the arithmetic, the number formats,
what each stage costs, and how to change or extend it.

- **Current as of:** Session 073 close (2026-09-29).
- **Scope split with `EFFECTS_BUS_REFERENCE.md`:** that document is the
  authority for the Effect system's control side: the type registry,
  parameters and resolution order, the Effect page, the FX sequencer,
  automation and LFO targets, storage and AutoSave, and the add-a-type
  tutorial. This document covers the signal path and its DSP.
- **Related:** `INSTRUMENTS_DSP_REFERENCE.md` (the voices and modulation),
  `STORAGE_SRAM_MANIFEST.md` (memory), `CPU_USE_DSP_AUDIT.md` (cost audit),
  `tools/dsp_test/DSP_TEST.md` (host test bench).

---

## 1. The whole path in one picture

```
per 32-frame block (main loop, mixer_calcNextSampleBlock):
  6 × voice render (int16, pre-volume)
      → decimator (sample and hold)
      → fader gains (dry / send by fader mode)
      → dry:  gain ramp → int16 → sample_mx_t → pan → route → saturating add ─┐
      → send: gain ramp → float × 256 → sample_mx_t → FX bus (L or L+R) ─┐     │
  if the Effect is not `off`:                                            │     │
      FX bus (sample_mx_t) → float (÷ 8,388,352) → effects_process() ←──┘     │
      → return gains (level, pan) → ramp → sample_mx_t (±255 clamp) → route ─┤
                                                                            ▼
  output buffers (DTCM): audioOutBuffer (DAC2 pair) and audioOutBuffer2 (DAC1 pair)

per 96 frames: commit the slot to the 2-slot ready queue

DMA1 Stream 4 HT/TC ISR (priority 4):
  pack_audio_half(): clamp to signed 24 bit → one rotated 32-bit word per channel
  → dma_buffer (→ SPI2/I2S2 → DAC2, U24) and dma_buffer2 (→ SPI3/I2S3 → DAC1, U23)
  → DSB
```

Timing (44,108 Hz, 32-frame blocks, the 4,897-cycle-per-frame budget) and the
main-loop render model are in `INSTRUMENTS_DSP_REFERENCE.md` §1. **The
constant-CPU rule applies here too:** no stage may save CPU by skipping work
because a gain, send or Effect parameter is at zero (`MEMORY.md`, DSP CPU
Policy).

---

## 2. Number formats and headroom

| Where | Format | Full scale |
|---|---|---|
| Voice blocks | `int16_t`, 32 samples | ±32,767 |
| Mixer and output buffers | `sample_mx_t` = `int32_t` holding a signed 24-bit value; `sampleMix_fromInt16(x) = x << 8` | ±8,388,352 for an int16 full-scale voice |
| FX bus while voices sum | `sample_mx_t` | as above |
| FX bus while the Effect runs | `float`, normalised: `mx × (1/8,388,352)` | 1.0 = int16 full scale; values above 1.0 are kept |
| DMA buffers | 24-bit data in 32-bit I2S frames, sent as two 16-bit halfwords (MSW, LSW) | ±8,388,607 |

- **Saturation points:** each voice's int16 stages saturate or truncate in
  the engine; every add into an output or bus buffer is
  `bufferTool_satAdd32()` (64-bit sum clamped to int32); the Effect return is
  clamped to ±255 (full scale × 255) before conversion
  (`mixer_floatToMx()`); `pack_half()` clamps each sample to signed 24 bit
  (`sampleMix_toS24()`).
- **Effective output clip:** the sum of all voices and the return clips at
  the pack, at int16 full scale. The int32 buses have about 48 dB of internal
  headroom, so six loud voices cannot wrap.
- **Effects work in float and must not truncate to int16.** The float bus
  keeps summed overshoot until the return (this is why the filter has a float
  twin).

---

## 3. Mixer (`Core/DSPAudio/mixer.c`)

### 3.1 Per-block setup

- Instrument control-rate work runs first (LFOs, filter coefficients, async
  updates), then `effects_service()` resolves the Effect's parameters
  (§4.1).
- **Routing snapshot:** `effectiveRouting[slot] =
  mixer_checkOutJackAvailable(mixer_audioRouting[slot])`, once per block.
  Jack detect pins (PD6/PD7 for OUT1 L/R, PB4/PB6 for OUT2 L/R) are sampled
  by the 500 Hz foreground service.

  | Requested route | Used when |
  |---|---|
  | DAC1 stereo | stays unless no MAIN jack is in and an OUT2 jack is: then DAC2 stereo; nothing plugged in: DAC1 (headphones) |
  | DAC2 stereo | stays if an OUT2 jack is in; otherwise DAC1 stereo |
  | A single output (DAC1 L/R, DAC2 L/R) | stays if its jack is in; otherwise the other side of the same pair, then the other pair; nothing plugged in: the DAC1 side |

- The two output buffers are cleared; if the Effect is active the FX bus is
  cleared.
- **Effect I/O shape:** `effects_activeIoFlags()` gives the live type's
  flags. `off` has none, so the whole bus path is skipped (an existing
  conditional path kept by user decision).

### 3.2 Per slot

1. **Render** (`instrumentManager_calcSlotSyncBlock()`): 32 pre-volume int16
   samples.
2. **Decimate** (`mixer_decimateBlock()`): a sample-and-hold counter adds
   `rate[slot] × rate[6]` per sample and takes a new sample when it passes
   1.0. `rate[slot]` comes from the instrument's `instrument_decimation`
   (`valueShaperI2F(v, −0.7)`); `rate[6]` is the Scene-wide decimation. At
   rate 1.0 every sample passes (the loop still runs: the constant-CPU rule
   and audit item 11).
3. **Fader gains** (`mixer_faderGains()`):
   - `volume` = the instrument's `vol` (0..1); `fader` = the slider;
     `send` = the effective FX send amount / 127;
   - mode `pre`: dry = volume × fader, send = send × fader;
   - mode `pst`: dry = volume × fader, send = send;
   - mode `fx`: dry = volume, send = send × fader.
4. **Pan:** `panL = squareRootLut[127 − pan]`, `panR = squareRootLut[pan]`
   (constant power; a 512 B DTCM table).
5. **Dry and send:**
   - If the Effect is active and the send ramp is non-zero
     (`sendGain > 0` or last block's > 0): `mixer_addVoiceInt16ToOutputAndFx()`
     does both in one pass (S073 Step 5).
   - Otherwise `mixer_addVoiceInt16ToOutput()` does the dry path only.
   - Both end-of-block gains (`mixer_voice_last_gain[slot]`,
     `mixer_send_last_gain[slot]`) are stored every block, even when the send
     is skipped. Re-enabling an Effect relies on the send ramp origin being
     current.

**Dry path, per sample:**

```
g   = lastGain + i · (1/31) · (gain − lastGain)       // click-free ramp
s16 = (int16)(data[i] · g)                            // truncation
sm  = s16 << 8                                        // sample_mx_t
stereo route: out_L += (sample_mx_t)(sm · panL);  out_R += (sample_mx_t)(sm · panR)
single route: out_X += sm                              // no pan
```

**Send path, per sample:**

```
g      = sendLastGain + i · (1/31) · (sendGain − sendLastGain)
sample = data[i] · g · 256                            // no int16 truncation, on purpose
stereo-input Effect: bus_L += (sample_mx_t)(sample · panL); bus_R += (sample_mx_t)(sample · panR)
mono-input Effect:   bus_L += (sample_mx_t)sample    // unpanned
```

- The send taps the decimated, **pre-volume** block, so it includes the
  voice's distortion but not its level, and a voice's `vol` never changes
  its send.
- The combined function keeps each expression's operand order, so its
  output is bit-identical to running the two old functions. A routing value
  outside the six cases still accumulates the send (the `default` case).

### 3.3 FX bus, process and return

When the Effect is active:

1. Convert the bus in place to float: `f = mx × (1/8,388,352)` for both
   channels. For a mono-input, stereo-output type the right channel is
   zeroed so the Effect can write a second output.
2. Build `effect_io_t`: `l` (always), `r` (stereo-in, or the zeroed channel
   for mono-in/stereo-out; otherwise NULL), `frames = 32`, `channels` = the
   input channel count, `share` = the current arena share (§5).
3. `effects_process(&io)`: the type's `process()` works in place.
4. **Return gains** (`level` = the common `vol` row / 127):
   - stereo-output type (balance, unity at centre):
     `gL = level · (pan ≤ 64 ? 1 : (127 − pan)/63)`,
     `gR = level · (pan ≥ 64 ? 1 : pan/64)`;
   - mono-output type (constant power): `gL = level · squareRootLut[127 − pan]`,
     `gR = level · squareRootLut[pan]`.
5. `mixer_addFxReturnToOutput()` ramps both gains per sample from the
   previous block's (`mixer_fx_return_last_gain[2]`) and adds into the
   Effect's own route (resolved by the same jack rules). A single-output
   route receives a stereo return as `0.5 · (L·gL + R·gR)`. Each value goes
   through `mixer_floatToMx()`: clamp to ±255, × 8,388,352, saturating add.

### 3.4 Output buffers and the DMA pack

- `audioOutBuffer[2][192]` and `audioOutBuffer2[2][192]` (DTCM, 1,536 B
  each) hold two 96-frame interleaved stereo render slots each. The render
  loop fills one slot as three 32-frame blocks, then
  `audioCodec_commitRenderBuffer()` queues it (a 2-slot single-producer,
  single-consumer ready queue).
- The DMA1 Stream 4 half/complete ISR (NVIC priority 4) calls
  `pack_audio_half()`: it takes the next ready slot (or, on an underrun,
  repeats the last one and increments `audioCodec_underrunCount`), packs it
  into the finished half of both DMA buffers, and ends with `dsb`.
  Stream 7 (DAC1) is a slave that only clears its flags.
- **Pack (S073 Step 3a):** each channel sample becomes one 32-bit store,
  `pack_frameWord(s24) = ror16((s24 & 0xFFFFFF) << 8)`, whose little-endian
  image is exactly the MSW-then-LSW halfword pair the halfword-mode DMA
  sends. Buffers are 4-byte aligned; a `may_alias` word type keeps the view
  legal. Do not add a `>> 8` in `sampleMix_toS24()` and do not zero the LSW
  (Session 022 loudness regression).
- **MPU (S073 Step 3b):** the DMA buffers live in `.dma_nocache` (first 4 KB
  of SRAM1), which MPU region 1 marks **Normal non-cacheable** (TEX=001, C=0,
  B=0, S=1, XN=1). Stores go through the write buffer; nothing is cached, so
  DMA sees them. The final `dsb` makes the half complete before the ISR
  returns. The slider ADC buffer shares the region.
- **Buffer to DAC mapping:** `output` / `audioOutBuffer` → `dma_buffer` →
  I2S2 → DAC2 (U24); `output2` / `audioOutBuffer2` → `dma_buffer2` → I2S3 →
  DAC1 (U23). In the mixer, `MIXER_ROUTING_DAC1_*` writes the `outL2`/`outR2`
  pointers (`output2`).
- **The `cpu` widget** (`audioCodec_getQueueFreePercent()`, averaged in
  `menu.c`) is the percentage of time the ready queue was not full, measured
  with the DWT cycle counter. It is audio-queue pressure, not general CPU
  load. The two DMA streams are not synchronised with each other (DAC1 and
  DAC2 can drift by up to one ISR period; a known, accepted limitation).

---

## 4. Effect DSP

### 4.1 Resolution before processing

`effects_service()` (every block, foreground) rescans every descriptor of
the active type: Morph base → interpolation → FX-sequencer lock → Pattern
overlay → LFO → clamps (`EFFECTS_BUS_REFERENCE.md` §6). It calls the type's
`write_param(rt, index, value)` only when a row's effective value changed,
and applies the common rows (route, level, pan) itself.

- **Cost:** about 10–15 cycles per row per block (about 100 for the 7-row
  `flt`, up to about 1,000 for a 64-row type), plus whatever `write_param`
  does on a change.
- **An LFO or a moving lock on a parameter makes `write_param` run every
  block.** Budget coefficient recomputation as if it always runs.

### 4.2 Runtime state

- `effects_runtime` is a union of every type's runtime struct in DTCM
  (`.dtcmz`), 76 B today (StereoFilter). The manager zeroes it and calls the
  type's `init()` when the type becomes active.
- **A larger runtime struct grows the union and shrinks the arena** by the
  same amount (the linker ASSERT keeps the arena ≥ 120 KiB). It needs RAM
  approval (`STORAGE_SRAM_MANIFEST.md` §10). Large audio state belongs in
  the arena share, not in the union.

### 4.3 StereoFilter (`flt`)

- Stereo in, stereo out. Two `ResonantFilter` states (left, right) share
  linked coefficients: `stereoFilter_linkCoefficients()` copies `f`, `g`, `q`
  and `drive` from left to right after every coefficient write; the state
  variables (`s1`, `s2`, `a`, `b`, `zi`) stay per channel.
- `write_param` uses the same shaping as a voice filter:
  `filter_freq` → `SVF_directSetFilterValue(valueShaperF2F(v/127, −0.9))`,
  `filter_reso` → `SVF_setReso(v/127)`, `filter_drive` → `SVF_setDrive(v)`,
  `filter_type` → `v + 1`.
- `process` runs `SVF_calcBlockZDFFloat()` on `io->l`, and on `io->r` when
  there are two channels.
- **`SVF_calcBlockZDFFloat()`** is the float twin of the voice filter: the
  same batched two-division solver (S073), normalised float in and out, no
  `__SSAT`, float literals. Types 1..7 filter; any other value passes the
  block through. Its arithmetic must stay identical to `SVF_calcBlockZDF()`;
  both are described in `INSTRUMENTS_DSP_REFERENCE.md` §6.2. An `#error`
  guards the shaper configuration it does not mirror.

---

## 5. The shared DTCM arena for buffer-using Effects

No Effect type uses the arena yet. The first one is Session 074's goal
(`S074_EFFECT_BUGS_BUFFER_USE.md`).

### 5.1 Geometry

- `.dtcm_fxbuf`: 126,624 B from `0x20001160` to the end of DTCM, 32-byte
  aligned, NOLOAD (never copied, never zeroed). DTCM is single-cycle,
  uncached and not reachable by DMA, which suits delay lines.
- **Effect share:** one contiguous region from the arena bottom up to the
  lowest claimed voice unit, `fxbuf_effectShare(&share)` → `{base, offset,
  bytes}`. Base is 32-byte aligned; `bytes` is a multiple of 32.
- **Voice units** (for future buffer-using instruments): 4,416 B each
  (2,208 16-bit samples, 50.06 ms), at most two per slot, twelve in total,
  allocated from the top. With all twelve claimed the Effect keeps 73,632 B.
- **What fits:**

  | Share | 16-bit mono | 16-bit stereo | 8-bit mono | 8-bit stereo |
  |---|---:|---:|---:|---:|
  | Whole arena, 126,624 B | 1.44 s | 0.72 s | 2.87 s | 1.44 s |
  | Minimum, 73,632 B | 0.83 s | 0.42 s | 1.67 s | 0.83 s |

  (Seconds at 44,108 Hz, before any other use of the share.)

### 5.2 The contract a buffer-using type must meet

| Registry / ops field | Duty |
|---|---|
| `buffer_min_bytes`, `buffer_pref_bytes` | Declare the need (0 = no arena use). The type must still work at the minimum share. |
| `init(rt, handoff)` | Clear every region it will read, **unless** it deliberately adopts content that `handoff->state_flags` marks valid for it (`FXBUF_STATE_EFFECT_WRITTEN`) and whose type, channels, bits and rate match. Boot content is undefined. |
| `export_handoff(rt, out)` | On exit, describe what it leaves: type, channels, bits, rate, share bounds, up to four read/write offsets (entries 12..15, arena-relative), and set `FXBUF_STATE_EFFECT_WRITTEN` if the region holds real audio. |
| `buffer_changed(rt, share)` | Called synchronously when voice units are claimed or released and the share moves or shrinks. Re-seat positions and lengths; a forced re-resolution follows. |
| `effective_max(index, share)` | For `EFFECT_PARAM_FLAG_BUFFER_DEPENDENT` rows (for example a delay time): the largest value the current share allows. It clamps at runtime only; stored values are never rewritten. |

- **"Clear unless you adopt":** the system never clears the arena, even on a
  type change. A delay can keep its tail across a same-type Scene switch,
  and the next owner decides what is valid.
- **Everything is foreground.** `FxBuffer` and `EffectsManager` must never be
  called from an ISR. Share changes happen between render blocks.

### 5.3 Known gaps to close with the first buffer type

1. **Same-type Scene switch:** `effects_activateScene()` does not refresh the
   handoff when the type stays the same (`EFFECTS_BUS_REFERENCE.md` §13
   item 1). With a buffer type this matters: add a refresh without `init`.
2. **FX return ramp while `off`:** `mixer_fx_return_last_gain[]` is not reset
   while the Effect is `off` (S072 debt 8). A type that outputs sound on its
   first block after `init` starts its return ramp from stale gains.
3. **Diagnostic `fxbuf_init()` order** (S072 debt 1): forced diagnostic
   voice units carry handoff rate 0. Fix it before testing minimum shares
   with `DEV_FXBUF_FORCE_VOICE_UNITS`.

### 5.4 DSP guidance for delay-style Effects

- **Constant cost:** the per-sample work must not depend on parameter values.
  Delay time changes the read position, not the amount of work; feedback,
  filtering and interpolation run every sample whatever their settings.
- **Wrap without division:** the share is a multiple of 32 bytes, not a power
  of two. Keep read and write indices in range with a compare-and-subtract,
  which costs the same every sample.
- **Parameter changes arrive once per block.** Ramp delay time (or crossfade
  two taps) across the block to avoid clicks and pitch jumps; ramp gains as
  the mixer does.
- **Bounded feedback:** clamp or soft-clip inside the loop so the float state
  cannot run away; the bus float is unbounded until the return's ±255 clamp.
- **Mono input:** with `EFFECT_IO_MONO_IN`, `io->r` is NULL (or the zeroed
  output channel for mono-in/stereo-out).
- **8-bit storage** halves memory but needs explicit quantisation and
  scaling; decide deliberately whether it should sound raw.

---

## 6. Cost

### 6.1 Measured instruction counts (S073)

Per-sample loop instruction totals (`fpseq.py --report --all`, standalone
`-Ofast` objects). Instructions, not cycles.

| Stage | Instructions per sample |
|---|---:|
| Decimator (per slot) | 12–18 |
| Dry only: single-output / stereo route | 28 / 46 |
| Send only: mono-input / stereo-input | 27 / 41 |
| Combined dry + send, single-output dry: mono / stereo send | 44 / 59 (were 55 / 69) |
| Combined dry + send, stereo dry: mono / stereo send | 63 / 78 (were 73 / 87) |
| Float ZDF filter per channel (StereoFilter runs two) | 80–103, 2 divisions (3 for LP) |
| DMA pack, per frame per DAC buffer | about 33 (two word stores); both buffers about 66 |

### 6.2 Share of the budget (audit estimates, ±50 %)

From `CPU_USE_DSP_AUDIT.md` (before the S073 refactor): the mixer dry path
and decimation 2.5–3 % of the CPU; the FX send, bus conversion and return
2–3 %; the StereoFilter Effect 4–5 %; the DMA pack ISR 1–1.6 %. S073 made the
send ride along with the dry pass, cut the filter's divisions from 5 to 2,
and halved the ISR's stores into bufferable memory. On hardware the user
measured about 10 % less CPU on the worst-case Scene with the stereo filter
Effect.

### 6.3 Paths whose cost varies (kept by user decision)

- The whole FX bus when the Effect is `off`.
- The per-voice send when the send ramp is zero (the dry-only function runs).
- `write_param` only when a value changed.

These are existing paths. The user budgets the Effect manually against the
worst case: **measure a new type with every send open and every modulated
parameter moving** (worst-case Scene, underrun count, `cpu` widget).

---

## 7. How to modify

Rules for every change here:

- Constant CPU; RAM approval for any new or larger allocation (including the
  Effect runtime union, which shrinks the arena); foreground only; no VLAs.
- `-Ofast` applies to `mixer.c` (by the `Core/DSPAudio/` pattern rule) and to
  each Effect DSP file only through an explicit Makefile rule (see §7.1).
- Verify with `make clean && make all`, `arm-none-eabi-size`,
  `python3 tools/link_budget.py arm-none-eabi-nm build/lxr02.elf`, the
  `tools/dsp_test` targets that cover the code, and on hardware the
  worst-case Scene and a listening check.

### 7.1 Add an Effect type (DSP side)

Follow the tutorial in `EFFECTS_BUS_REFERENCE.md` §14 for the registry,
descriptors, lanes, page layout and storage. For the DSP:

1. `Core/DSP/Effects/<Type>/<Type>Effect.c/.h`: the runtime struct and the
   `effect_type_ops_t`. Keep the struct small; add it to the
   `effects_runtime_t` union with a `_Static_assert`.
2. `process(rt, io)`: float, in place, `io->frames` samples, respect
   `io->channels` and a NULL `io->r`. Never truncate to int16.
3. `write_param(rt, index, value)`: rows 3..63 only; convert here; expect it
   to run every block under an LFO.
4. Buffer use: §5.2.
5. Makefile: add `<Type>Effect.c` to `DSP_SRCS` **and** an explicit `-Ofast`
   rule like StereoFilter's (the pattern rule only covers `Core/DSPAudio/`);
   `<Type>Parameters.c` goes into the normal sources; add the include path.
6. Test on the host first (`DSP_TEST.md` §6.3: stability, bounded output,
   mono/stereo I/O, parameter jumps, cost), then on hardware with the
   diagnostic registry self-check (`FxBf` screen, `DEV_MODES.md`).

### 7.2 Change the mixer's arithmetic, routing or gain laws

- Any change to `mixer_addVoiceInt16ToOutput()`,
  `mixer_addVoiceInt16ToOutputAndFx()`, the return, or the gain laws alters
  the sound of every Scene. Treat a restructuring as S0 and prove it with the
  `mixer` and `armcheck-mixer` targets (`DSP_TEST.md` §6.1).
- The dry and send expressions differ on purpose (int16 truncation on the dry
  path only). Keep them different unless the user decides otherwise.
- Keep the last-gain updates unconditional.
- Routing fallbacks live in `mixer_checkOutJackAvailable()`; the Effect
  return uses the same function.

### 7.3 Change the output pipeline

- The pack must keep the exact memory image the halfword DMA expects; prove a
  change with the `pack` target and check the linked ELF's `pack_half` code.
- Keep `pack_audio_half()`'s final `dsb` while region 1 is bufferable.
- `.dma_nocache` must stay ≤ 4 KB (MPU region 1 size; linker ASSERT).
- DMA cannot reach DTCM: DMA buffers stay in SRAM1; render buffers stay in
  DTCM.

---

## 8. Pitfalls

- **Do not apply voice volume twice.** Engines render pre-volume; the mixer
  applies `vol`.
- **The send ramp needs its origin.** Never skip the
  `mixer_send_last_gain[]` update, even when the Effect is `off`.
- **`off` does no bus work at all.** Code that runs "every block" in an
  Effect does not run while `off`; `init()` handles re-entry.
- **Float overshoot is legal on the bus** and is clamped only at ±255 at the
  return. Keep the Effect's own feedback bounded.
- **The duplicated comment line** near the `effects_service()` call in
  `mixer_calcNextSampleBlock()` is cosmetic debt (S072), not a missing
  statement.

---

## 9. History

- Session 008: SPSC audio queue; Session 010: render stays in the main loop.
- Session 022: 24-bit output path (`sample_mx_t`, `int16 << 8`).
- Session 023: the dry path fused into one loop (gain ramp, conversion,
  pan/route).
- Session 072: voice volume moved to the mixer; the FX bus, fader modes,
  Effect return, StereoFilter and the DTCM arena
  (`072_SESSION_HANDOFF_LOG.md`).
- Session 073: one-pass dry + send, word-store pack, Normal non-cacheable DMA
  region, batched float filter (`073_SESSION_HANDOFF_LOG.md` §6).
