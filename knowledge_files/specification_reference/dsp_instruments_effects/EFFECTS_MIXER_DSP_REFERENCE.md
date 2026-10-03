# Effects and Mixer DSP Reference

What happens to audio after an instrument voice renders it: decimation,
gain, pan and routing, the FX bus and the Effect's DSP, the Effect return,
the master bus compressor, and the path to the two DACs. It covers the
arithmetic, the number formats, what each stage costs, and how to change or
extend it.

- **Current as of:** Session 075 close (2026-10-03; F3 changed nothing in
  the mixer or Effect DSP). S074 added CrumpBit
  (§4.4, the first type using the arena), the master bus compressor (§5A),
  the `xfd` fader mode (§3.2) and the closure of the arena gaps (§5.3).
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
  else: return ramp origin ← 0 (S074)                                         │
                                                                            ▼
  output buffers (DTCM): audioOutBuffer (`output`, DAC2 pair, St2) and
                         audioOutBuffer2 (`output2`, DAC1 pair, St1)
  master bus compressor on St1 or St2, in place (§5A; nothing while `cmp` off)

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
   `rate[slot]` per sample and takes a new sample when it passes 1.0.
   `rate[slot]` comes from the instrument's `instrument_decimation`
   (`valueShaperI2F(v, −0.7)`). The Scene-wide multiplier `rate[6]` (global
   `srt`) was removed in S075; `mixer_decimation_rate[]` has six entries, and
   a Scene at the former neutral 127 is bit-identical (×1.0f is exact). At
   rate 1.0 every sample passes (the loop still runs: the constant-CPU rule
   and audit item 11).
3. **Fader gains** (`mixer_faderGains()`):
   - `volume` = the instrument's `vol` (0..1); `fader` = the slider;
     `send` = the effective FX send amount / 127
     (`preset_getEffectiveFxSendAmount()`: the step override, else, since
     S075 F2, the voice's Normal and Morph send endpoints interpolated by
     its resolved Morph amount, `round(n + (m − n)·a/255)`; read every
     block);
   - mode `pre`: dry = volume × fader, send = send × fader;
   - mode `pst`: dry = volume × fader, send = send;
   - mode `fx`: dry = volume, send = send × fader;
   - mode `xfd` (3, S074): dry = volume × fader, send = send × fader′, where
     fader′ is the slider taper read from the other end, `taper(1 − x)`.
     - `adc_sliderGainMirrored(g)` (`adcPots.c`, beside the taper) computes it
       in closed form. The taper is `g = (R − m)/(1 − m)` with
       `R = 10^((x − 1)·D/20)`, `m = 10^(−D/20)` and `D` =
       `SLIDER_LOG_TAPER_DB` (30). So `P = m + g·(1 − m)` and
       `g′ = m·(1 − P)/(P·(1 − m))`.
     - One `vdiv.f32` and no `powf` (the `m` terms fold at compile time).
       It matches `taper(1 − x)` to 5.6 × 10⁻¹⁶. Guards give exact endpoints
       (`g ≤ 0 → 1`, `g ≥ 1 → 0`); a linear taper gives `1 − g`.
     - Values: bottom = send 0 dB, no dry; 0.25 = dry −27.0 / send −7.9 dB;
       centre = both −16.4 dB (log taper on each side, not constant power);
       0.75 = dry −7.9 / send −27.0 dB; top = dry 0 dB, no send.
     - Cost: one division per `xfd` slot per block; otherwise the same as
       `fx`. With an Effect active the one-pass dry + send runs at every
       position except the very top (send exactly 0). The worst case is
       unchanged.
     - `MIXER_FADER_XFD 3u` (`mixer.h`) is asserted equal to
       `SCENE_FADER_SETTING_MAX` (`SceneData.h`).
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
   - stereo-output type (balance, unity at stored centre 63):
     `gL = level · (pan ≤ 63 ? 1 : (127 − pan)/64)`,
     `gR = level · (pan ≥ 63 ? 1 : pan/63)`;
   - mono-output type (constant power): `gL = level · squareRootLut[127 − pan]`,
     `gR = level · squareRootLut[pan]`.
5. `mixer_addFxReturnToOutput()` ramps both gains per sample from the
   previous block's (`mixer_fx_return_last_gain[2]`) and adds into the
   Effect's own route (resolved by the same jack rules). A single-output
   route receives a stereo return as `0.5 · (L·gL + R·gR)`. Each value goes
   through `mixer_floatToMx()`: clamp to ±255, × 8,388,352, saturating add.
6. **While the Effect is `off`** (S074, closing S072 debt 8): an `else` of
   `if (fx_active)` sets `mixer_fx_return_last_gain[0..1] = 0` every block,
   two stores. The next type's return therefore fades in from 0 over its
   first block instead of ramping from the last active type's stale gains.
   This matters for types that output sound on their first block:
   CrumpBit's AC-coupled 8-bit output is never exactly silent. The
   active-Effect path is bit-identical.

After the Effect return, the master bus compressor (§5A) processes one
output pair in place. It is the last mixer stage before the output buffers
are queued for the DMA pack.

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
  (`.dtcmz`), 76 B (StereoFilter is the largest; CrumpBit is 56 B). The
  manager zeroes it and calls the type's `init()` when the type becomes
  active. It does not zero or re-`init` on a same-type Scene switch: the
  runtime, and with it any tail, keeps running.
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

### 4.4 CrumpBit (`cbt`, Session 074)

Files: `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c/.h` (compiled `-Ofast`
through an explicit Makefile rule). Page, rows and lanes are in
`EFFECTS_BUS_REFERENCE.md` §4.2a and §8.6. Design record:
`074_SESSION_HANDOFF_LOG.md` §7.

**What it is.** The stereo send is converted as a bipolar 8-bit audio ADC
would. The eight data lines can each be forced off or inverted, and an 8-bit
DAC decodes the result. AC coupling removes the DC the masks create. The two
channels also feed a mono 8-bit tape-style delay kept in the arena, and a Mix
control crossfades the bit-crushed signal with the delay.

```
per sample, both channels:
  code  = adc(x)                          x clamped to ±1; code = (u32)(x·128 + 128.5); code −= code>>8
  code' = (code & keep) ^ invert          keep = ~(bit_off & ~bit_invert) & 0xFF   (invert wins)
  y     = (code' − 128)/128               8-bit DAC, −1 … +127/128
  h     = y − y[n−1] + 0.9985755·h[n−1]   AC coupling, ~10 Hz one-pole high-pass
per sample, the tape loop (mono):
  delay += (target − delay)·1.5113e-4     tape glide, τ 150 ms
  rp     = wp − delay (wrap by compare)   i0 = ⌊rp⌋, i1 = i0+1 (wrap), frac = rp − i0
  wet    = (loop[i0] + frac·(loop[i1] − loop[i0]))/128,   muted unless delay + 2 ≤ valid
  loop[wp] = adc(½(hL + hR) + fb·wet) − 128   (int8; re-quantised every pass, masks not re-applied)
  wp++ (wrap), valid++ (saturating at the loop length)
  out    = h + mix·(g·wet − h)             per channel; g = balance-law delay pan
```

- **Offset binary** (user decision): code 128 is silence, the transfer is
  mid-tread, and one LSB is 1/128 of full scale. With every bit normal the
  type is still an 8-bit quantiser: it is never transparent, and quiet sends
  use few codes.
- **What the data lines do:**
  - bit 7 off or inverted moves even silence to −1.0 (full-scale DC);
  - lower bits off remove weight 2ⁿ (coarser steps, DC about −2ⁿ⁻¹);
  - lower bits inverted add a signal-correlated ±2ⁿ pattern, and silence
    becomes +2ⁿ/128.
  - The AC coupling returns the output to 0 in tens of ms. It cannot hide
    the step when a high bit switches (τ ≈ 16 ms, a thump). Without it the
    DC would also build to `DC/(1 − fb)` in the loop and pin it at a rail.
- **Rate and Sync (per block):**
  - `free = 70,573·e^(−rate·ln80/127)` samples: 1.60 s at rate 0, 20 ms at
    127, about 180 ms at 64 (`crumpBit_rateSamples()`).
  - `synced` = the StepScale division nearest in log time, at `seq_getBpm()`
    (96-PPQ ticks × 27,567.5 / BPM samples), among divisions ≤ 1.60 s. The
    bracketing pair is chosen with the geometric-mean test
    `target² < low·high`, so no logarithms (`crumpBit_divisionFor()`).
  - Both are computed every block and `sync` selects one (constant cost), so
    tempo changes glide the delay. The target is clamped to `length − 2`.
- **Mix, feedback, pan:** `mix = row/127`; `fb = row·0.99/127` (always < 1);
  delay pan `gL = p ≤ 63 ? 1 : (127 − p)/64`, `gR = p ≥ 63 ? 1 : p/63`. All
  four ramp linearly across the block and are stored as the exact targets at
  the end.
- **Arena use:** one byte per sample. The loop length is
  `min(share bytes, 70,592)` (1.60 s plus a 2-sample guard, 32-byte
  rounded). It is declared as `buffer_min_bytes = buffer_pref_bytes`, and it
  fits the minimum share (73,600 B) so a Scene sounds identical whatever
  voice units are claimed.
- **"Clear unless you adopt" by masking:**
  - `init` marks the loop unseated (`length = 0`) and unprimed;
  - the first block seats it (`write_pos = 0`, `valid = 0`) and starts the
    delay on its target, with no glide;
  - reads further back than `valid` are muted;
  - `buffer_changed` re-seats and zeroes `valid` if the length changed;
  - it never adopts a previous owner's content;
  - a same-type Scene switch keeps everything, so tails ring on.
- **Runtime (56 B):** `length`, `write_pos`, `valid`, `delay`, the four ramp
  values, four AC states, and the raw row bytes (`bit_off`, `bit_invert`,
  `mix_raw`, `feedback_raw`, `rate`, `sync`, `pan_raw`) plus `primed`.
  `write_param` only stores raw bytes; conversions happen once per block, so
  an LFO costs nothing extra.
- **Constant CPU:** every stage runs every sample whatever the settings (the
  delay at Mix 0, feedback at 0, the bit logic with all bits normal). There
  is no per-sample division or transcendental; per block there is one `expf`,
  a 14-step division walk and one divide.
- **Cost:** estimated 75–95 instructions per sample for both channels plus
  the loop, against 160–206 for StereoFilter's two ZDF channels. Not
  measured on hardware separately; the user accepted the type on the
  worst-case listening checks. Flash: `crumpBit_process` 4,804 B and
  `crumpBit_syncDivision` 3,684 B (`-Ofast` unrolling), 11.6 KB for the
  whole type.

## 5. The shared DTCM arena for buffer-using Effects

CrumpBit (§4.4) is the first type that uses the arena; StereoFilter uses
none.

### 5.1 Geometry

- `.dtcm_fxbuf`: **126,592 B from `0x20001180`** to the end of DTCM (S074;
  it was 126,624 B from `0x20001160` until the bus compressor's 32 B DTCM
  state was added), 32-byte aligned, NOLOAD (never copied, never zeroed).
  DTCM is single-cycle, uncached and not reachable by DMA, which suits delay
  lines.
- **Effect share:** one contiguous region from the arena bottom up to the
  lowest claimed voice unit, `fxbuf_effectShare(&share)` → `{base, offset,
  bytes}`. Base is 32-byte aligned; `bytes` is a multiple of 32.
- **Voice units** (for future buffer-using instruments): 4,416 B each
  (2,208 16-bit samples, 50.06 ms), at most two per slot, twelve in total,
  allocated from the top. With all twelve claimed the Effect keeps 73,600 B.
- **What fits:**

  | Share | 16-bit mono | 16-bit stereo | 8-bit mono | 8-bit stereo |
  |---|---:|---:|---:|---:|
  | Whole arena, 126,592 B | 1.44 s | 0.72 s | 2.87 s | 1.44 s |
  | Minimum, 73,600 B | 0.83 s | 0.42 s | 1.67 s | 0.83 s |
  | CrumpBit's loop, 70,592 B | — | — | 1.60 s (+ guard) | — |

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

### 5.3 Gaps closed with the first buffer type (Session 074)

1. **Same-type Scene switch:** `effects_activateScene()` now refreshes the
   handoff (`effects_exportHandoff()`) without `init` when the type stays the
   same.
2. **FX return ramp while `off`:** the mixer's `off` branch zeroes
   `mixer_fx_return_last_gain[]` every block (§3.3 item 6).
3. **Diagnostic `fxbuf_init()` order:** `fxbuf_handoffResetAll()` runs
   directly after `fxbuf_clearOwners()`, so diagnostic forced voice units
   carry valid rates and the minimum-share test
   (`DEV_MODE_DIAGNOSTIC 1`, `DEV_FXBUF_FORCE_VOICE_UNITS 12`) is valid.

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
  scaling; decide deliberately whether it should sound raw. CrumpBit
  re-quantises its loop to 8 bits on every pass on purpose: that is its
  character, and it bounds the feedback.

---

## 5A. Master bus compressor (`cmp cam ctm csc`, Session 074)

Files: `Core/DSPAudio/BusCompressor.c/.h` (`-Ofast` through `DSP_SRCS`).
Settings: `scene_settings_t::bus_comp[4]` (SceneData). Design record:
`074_SESSION_HANDOFF_LOG.md` §8–§9.

**What it is.** A Scene-owned, soft-knee, RMS, optical (LA-2A-flavoured)
compressor with a trigger sidechain, on **one** output pair. It is **not an
Effect type**: no registry row, no send, no lanes, no arena. It is the one
DSP stage the user allowed to do no work when it is off. With `cmp` on, its
cost counts against the worst-case Scene.

### 5A.1 Placement

- `busComp_processBlock(output2, output, scene)` is the last stage of
  `mixer_calcNextSampleBlock()`, after all voices and the Effect return (or
  its `off` branch) have been summed, and before the pack.
- **St1 = `output2`** (DAC1: MAIN jacks and headphones); **St2 = `output`**
  (DAC2: OUT2). An early draft had these reversed.
- **No jack fallback:** `cmp St2` always processes the DAC2 buffer; with
  nothing in OUT2 the mixer has already moved St2 routes to DAC1, so the
  compressor then sees only what remains on DAC2.
- **Stereo-linked:** one detector and one gain for both channels.
- **Sidechain timing:** `voiceControl_triggerNow()` (`MidiVoiceControl.c`)
  calls `busComp_sidechainTrigger(track, velocity)`. `main.c` drains the
  trigger queue immediately before each block, so the duck lands in its
  voice's own block.

### 5A.2 Controls and macros

`a = cam/127`, `t = ctm/127`, block `T_b` = 0.7255 ms. One-pole coefficient
per block: `k(τ) = T_b/(τ + T_b/2)` (bilinear, within 0.2 % of
`1 − e^(−T_b/τ)` for τ ≥ 5 ms; constants fold at compile time).

| Control | Range | Default | Effect |
|---|---|---|---|
| `cmp` | `off` / `St1` / `St2` | off | target pair |
| `cam` | 0..127 | 48 | threshold `T = −3 − 27a` dBFS; ratio `R = 1 + 3a + 4a³`; makeup `M = −GR(−12 dBFS)·(1 − 0.3a)`; drive `d = 1 + 0.5a²` |
| `ctm` | 0..127 | 48 | attack `5 + 10t` ms; fast release `60 + 540t²` ms; memory release `500 + 4,500t²` ms |
| `csc` | `off` / `1`..`6` | off | sidechain voice; track 7 counts as voice 6 (BC11, unconfirmed) |

Worked values at the −12 dBFS reference:

| `cam` | T | R | Static GR | Makeup | `d` | Cubic ceiling `1/d` |
|---:|---:|---:|---:|---:|---:|---:|
| 0 | −3.0 | 1.00 | 0 | 0 dB | 1.00 | 0 dBFS |
| 48 | −13.2 | 2.35 | −1.2 dB | +1.1 dB | 1.07 | −0.6 dBFS |
| 64 | −16.6 | 3.02 | −3.1 dB | +2.7 dB | 1.13 | −1.0 dBFS |
| 96 | −23.4 | 5.00 | −9.1 dB | +7.1 dB | 1.29 | −2.2 dBFS |
| 127 | −30.0 | 8.00 | −15.8 dB | +11.0 dB | 1.50 | −3.5 dBFS |

At `cam` 0 the compressor applies no gain change; only the sidechain duck
and the full-scale ceiling remain. Tuning revision 1 (user, after the first
listen) reduced the top-end makeup (it was the full `−GR`, +15.8 dB at 127)
and raised the drive from `0.4a²`.

### 5A.3 Per block (control path)

1. **Off check.** If both the active pair and `cmp` are `off`: clear any
   pending sidechain weight and return.
2. **Macros** from the current Scene's `cam`/`ctm` (above).
3. **Transitions (one-block fades):**
   - `off` → on: seed the cell as if the bus sat at the reference
     (`P = 0.0631`, `G_f = GR(−12 dBFS)`, `G_m = 0`, gain origin 1,
     crossover states 0), then fade dry → wet across the block;
   - a target change fades the old pair wet → dry, marks it off, and next
     block reseeds and fades the new pair in, so St1 ↔ St2 never steps
     either pair;
   - the same target with changed settings continues, with the gain ramping.
4. **Detector:** `level = 3.0103·log2(P + 10⁻¹²)` dBFS, using the power
   smoothed up to the previous block (one-block lag, so a single pass reads
   and writes the buffer; no look-ahead).
5. **Soft knee:** `W = 12 dB`, `over = level − T`, `s = 1 − 1/R`:
   GR = 0 for `over ≤ −6`, `−s·(over + 6)²/24` inside, `−s·over` above.
6. **Optical cell:**
   - `G_f` moves toward the target with the attack coefficient if the target
     is lower, else the release coefficient;
   - a pending sidechain weight `w` sets `G_f = min(G_f, target − (2 + 19a)·w)`
     and then clears. The duck is measured from the static target, so fast
     repeated triggers do not accumulate;
   - `G_m` moves toward `0.5·G_f`, charging with τ 300 ms and releasing with
     the `ctm` memory release;
   - the applied reduction is `min(G_f, G_m)`: brief peaks release fast,
     sustained compression leaves up to half the reduction lingering for
     seconds.
7. **Gain:** `g = exp2((min(G_f, G_m) + M)·0.1660964)`, ramped per sample
   from the previous block's `g`.

Sidechain weight: `busComp_sidechainTrigger()` records the largest
`(v/127)³` since the last block when the track matches the active Scene's
`csc` and velocity > 0. The `cam` depth is applied at block time. Depth at
velocity 127: 2 dB at `cam` 0, 9.2 dB at 48, 21 dB at 127; at velocity 64
it is one eighth of that.

### 5A.4 Per sample (audio path, band-split saturation)

```
x      = bus sample (int32 units)          detector: sum += xL² + xR²  (input, before gain)
L      = LP(x)                             one-pole crossover, 2 kHz, k = 0.2479 (compile-time)
H      = x − L                             exact complement
u      = clamp((L + 0.25·H)·gk, −1, 1)     gk = ramped g · d/1.5 / FS
wet    = u·(c_out − c_out/3·u²) + H·gk·c_out·0.75     c_out = 1.5·FS/d
wet    = knee(wet)                         identity below 0.75 FS; C1 bend to exactly FS at 1.25 FS; held above
out    = x + w·(wet − x)                   w = the one-block fade weight (1 when steady)
```

- **Small signals keep unity gain** (`1.5/d · d/1.5 = 1`). Where the cubic is
  linear, `wet = (L + 0.25H + 0.75H)·g = x·g`: the split is inaudible on
  clean material.
- **Why the split (S074 saturation update, accepted):** a memoryless cubic at
  44.1 kHz makes a 3rd harmonic that folds back inharmonically for content
  above fs/6 (≈ 7.35 kHz: hats, cymbals). The kick also intermodulates the
  hats through the same curve. Saturating the low band fully and only a
  quarter of the high band cut those products by 10–14 dB on kick-plus-hat
  mixes (26 dB on a folded hat 3rd harmonic), with low-end saturation
  unchanged. Hats come out 0.4–0.8 dB brighter at high `cam`.
  - Rejected: first-order ADAA (dulls the whole bus by 2–6 dB at
    10–15 kHz); pre/de-emphasis (re-amplifies high distortion, +16 B);
    2× oversampling (+150–250 cycles/frame); a smoother curve alone (still
    makes the 3rd harmonic).
- **The knee** restores "never reaches the pack's hard 24-bit clip": the
  bypassed highs skip the cubic's `1/d` ceiling. It is branchless, per the
  constant-CPU rule (`fabsf`, clamps, one multiply-add, `fminf`,
  `copysignf`).
- **The dry path is exact:** `float(int32)` is exact to ±2²⁴, above the
  24-bit pack range.

### 5A.5 State, storage and cost

- **State** `bus_comp_state_t busComp` (`INDTCMZ`, 32 B, asserted ≤ 32):
  power, fast and memory GR (dB), previous gain, pending sidechain weight,
  `split_lp[2]`, active pair. It shrank the FX arena by 32 B.
- **Settings:** SceneData `bus_comp[4]`, AutoSave Scene parameters 41..44,
  `sceneset.scg` keys `bus_comp_mode/amount/time/sidechain`, page mirrors
  `PAR_BUS_COMP_*` (58..61). Not modulatable in v1.
- **Cost while on:**
  - per stereo frame, the loop is 71 instructions (63 floating point) with
    the band split. It was 34 with the plain cubic;
  - per block, one `log2f`, one `exp2f`, the knee, the cell and a few
    divisions (about 350–450 cycles);
  - total about **1.4–1.8 %** of 216 MHz, constant, no data-dependent
    branches;
  - 0 while off. Not yet read on the `cpu` widget.

### 5A.6 How to change it

- The model constants are named at the top of `BusCompressor.c`
  (`BUS_COMP_KNEE_DB`, `_REF_LEVEL_DB`, `_RMS_MS`, `_CHARGE_MS`,
  `_MEMORY_SHARE`, `_SAT_SPAN`, `_SPLIT_HZ`, `_HF_SAT_SHARE`,
  `_CEIL_START`). The `cam`/`ctm` curves sit beside their formulas in
  `busComp_processBlock()`.
- **Untried tuning:** `BUS_COMP_HF_SAT_SHARE` 0.35 keeps more snare and
  cymbal bite (simulated −8 to −11 dB instead of −10 to −14). The knee could
  start at 0.85 FS at the same cost.
- **BC11:** to exclude track 7 from `csc 6`, change the one `voice =` line
  in `busComp_sidechainTrigger()`.
- **Any new state** beyond the approved 32 B shrinks the arena: RAM approval
  first.
- **The settings page must stay last** (`MENU_GLOBAL_SCENE_SUBPAGE`,
  `menu.h`; the BC18 diagnostic check). The four ids never commit through
  `menu_parseGlobalParam()`.
- Verify with a host model first (the S074 checks used a Python replica of
  the exact loop), then listen on hardware and read the `cpu` widget with
  `cmp` on against off.

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

### 6.2a Session 074 additions

| Stage | Cost | Notes |
|---|---|---|
| CrumpBit `process` | est. 75–95 instructions per sample (both channels + loop); per block one `expf`, a 14-step walk, one divide | constant whatever the settings; not measured separately on hardware |
| Master bus compressor (on) | 71 instructions per stereo frame (was 34 before the band split) + about 350–450 cycles per block; ≈ 1.4–1.8 % of the CPU | 0 while `cmp` is off; `cpu` widget reading still to be taken |
| `xfd` fader mode | one division per `xfd` slot per block | otherwise the same as `fx` |
| Mixer `off` branch | two stores per block | the return ramp reset |

### 6.3 Paths whose cost varies (kept by user decision)

- The whole FX bus when the Effect is `off`.
- The per-voice send when the send ramp is zero (the dry-only function runs).
- `write_param` only when a value changed.
- The master bus compressor while `cmp` is `off` (S074, approved by the
  user): the stage returns after one settings read. Its cost must be counted
  against the worst-case Scene with it on.

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
4. Buffer use: §5.2. `CrumpBitEffect.c` is the worked example: it seats its
   loop from `io->share`, masks unwritten reads with a fill counter, wraps
   by compare-and-subtract, ramps every gain across the block, and keeps
   feedback bounded by re-quantising.
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
- **St1 is `output2`, St2 is `output`.** The buffer names follow the DAC
  numbering of the codec wiring, not the jack labels; a routing or
  compressor change that assumes otherwise processes the wrong pair.
- **The compressor ends above the pack.** Anything added after it runs on
  compressed, made-up audio and must not push past full scale (the knee is
  the last guarantee).
- **Stale comments** at the S074 close: `BusCompressor.h` still says the
  state is 24 B (it is 32 B), and the loop comment in `BusCompressor.c` says
  "~+0.45 %" (measured +0.6–0.8 %). `CrumpBitEffect.h` still quotes the old
  73,632 B minimum share (now 73,600 B).

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
- Session 074: CrumpBit, the first arena type; the mixer `off`-branch
  return-ramp reset; the master bus compressor with tuning revision 1 and
  band-split saturation; the `xfd` fader mode (`074_SESSION_HANDOFF_LOG.md`
  §7–§10).
