# Session 095 — Space Group Effects (Delay/Reverb/Echo), Colloid Instrument, And Effects Bus Tweaks

```
DATE: 2026-11-12 / 2026-11-20
SESSION GOAL: Implement the three Space group effects (Delay, Reverb, Echo)
  with elastic seating, feed-through handover, and drain-into-OFF; implement
  the Colloid instrument (6th type, Basic class); apply Colloid follow-ups
  (filesystem fixes, 4 PWM forms, pitch slew, intervals, filter redesign,
  post drive/tone); then clean up the effects bus (remove StereoFilter,
  remove Space mix, improve compressor and drive).
COMPLETED: All four phases. P1: Space effects with arena prefix, elastic
  seating, feed-through, drain, slot lock. P2: Colloid instrument with
  1024-point sine, pair loops, TPT SVF, ADSR. P3: 11 Colloid follow-ups
  (filesystem fix, 4 PWM forms, pitch slew, intervals, range expansions,
  filter redesign, post drive/tone, 50 rows / 8 pages). P4: StereoFilter
  removal, Space mix removal, value text cleanup, stepped rows, fbk unity
  at 100, CrumpBit rvb damping, compressor threshold/makeup/drive
  improvements, distortion makeup table, Colloid device measurements.
VERIFIED ON HARDWARE: Yes. P1 device-tested (Space effects auditioned),
  P2 device-tested (Colloid basic operation), P3 device-tested (clipping
  fix, all PWM forms, filter, drive/tone), P4 device-tested (compressor,
  Colloid worst-case measurement 17,433 cycles/block).

CHANGES THIS SESSION:
  (per-file details in each phase section below)

KNOWN ISSUES INTRODUCED:
  - Flash at 720,084 B (+856 B past the P2 budget line of +8,192 B).
  - ITCM at 15,232 / 16,384 B (93.0 %). Very limited headroom for future
    ITCM consumers.
  - Colloid voice struct is 1,112 B after P3 (was 1,032 B after P2).
  - ATS fixes needed re-test at HEAD dd81f19c.

KNOWN ISSUES RESOLVED:
  - filesystem_createBootIndex_tick() case 6 read uninitialised `entry`
    variable (P3 F1).
  - filesystem_scanInstruments_tick() case 0 did not set op_close_status
    (P3 F1).
  - Colloid clipping (COL_HEADROOM ×0.35, COL_MAKEUP ×1.43) (P3 device).
  - StereoFilter removed (−3.7 KB flash; type never reached production
    quality) (P4 T3).

NEXT SESSION RECOMMENDED GOAL: Device re-test of ATS fixes at HEAD
  dd81f19c. Continue with next planned firmware work.
BLOCKERS: None.

CRITICAL REMINDERS FOR NEXT SESSION:
  - Sine table is now 1,024 points (TABLESIZE 1024). Readers shift >> 22,
    fraction mask 0x3fffff. Do not revert to 4,096.
  - Effect type IDs renumbered: OFF 0, CrumpBit 1, Delay 2, Reverb 3,
    Echo 4, count 5. StereoFilter does not exist.
  - Arena prefix is 512 B (SK_PREFIX_BYTES). The control block is at the
    start. Do not allocate ring memory in the prefix region.
  - Space effects have no mix parameter. 100% wet send bus.
  - fbk unity is parameter value 100 for Space effects.
  - Colloid sustain holds only while transport runs.
  - Handoff record fxbuf_handoff_t is 196 B.
  - ITCM 15,232 / 16,384 B — sign-off required for any ITCM addition.
```

---

## Phase 1 — Space Group Effects (Delay, Reverb, Echo)

### P1 Summary

Three new effect types sharing a common DSP kit, an arena prefix layout,
elastic seating, feed-through handover, drain-into-OFF, and slot lock.

### P1 Architecture

**New folder:** `Core/DSP/Effects/Space/` with four files:
- `SpaceKit.h/.c` — shared DSP primitives, control block, seating, feed API
- `SpaceEffect.c` — init/process entry points for Delay, Reverb, Echo
- `SpaceParameters.c` — registry, parameter table, labels, format helpers
- `SpaceTables.c` — delay-time curve, halfband coefficients, pitch-shifter
  tables (all `const` flash)

**Effect type enumeration** (after P4 renumbering):
OFF 0, CrumpBit 1, Delay 2, Reverb 3, Echo 4, count 5.

**Arena layout:** 126,656 B DTCM (the `.dtcm_fxbuf` section). First 512 B
(`SK_PREFIX_BYTES`) is the **arena prefix**:
- Bytes 0–63: `sk_ctl_t` control block (64 B) — magic|owner, span offset
  and length, feed state (sides, segments, gains, positions, frac, left,
  stride, wrap, bits, end-fade), dry ramp, slot-lock flag.
- Bytes 64–511: type state header (448 B) — holds type-specific runtime
  variables (filter states, LFO phases, write pointers, ring masks,
  accumulated phases, etc.) without occupying any ring memory.
- Bytes 512–126,655: usable ring memory, divided between the Effect and
  voice units by elastic seating.

**Elastic seating** (`sk_seat()`): the EffectsManager divides the DTCM
arena between voice-unit delay lines (CrumpBit sub-types, etc.) and the
active Effect. When the Effect type changes, the arena is rebalanced.
Growth happens at the wrap point: a ring's length changes only when its
write head crosses the old end boundary, so live reads are never
invalidated.

### P1 Shared Primitives (SpaceKit)

All primitives in `SpaceKit.h` are `static inline` or small `.c` bodies:

| Primitive | Purpose | Cost (approx.) |
|-----------|---------|----------------|
| `sk_ringIdx(p, off, mask)` | Power-of-two ring read (FV-1 style, pointer counts UP, read `off` behind) | 1 instruction |
| `sk_readHerm(ring, idx, mask)` | 4-point Hermite interpolation on a 16-bit ring | ~12 |
| `sk_onePole(z, x, c)` | One-pole low-pass: z += c (x − z) | 3 |
| `sk_shelf2(st, x, kL, kH)` | Two-band shelf: LP → low shelf, (x − LP) → high shelf | ~6 |
| `sk_svfTone(st, x, g, k)` | Division-free TPT SVF (low-pass output) | ~8 |
| `sk_softClip(x)` | Cubic soft clip: x − x³/3 for |x| ≤ 1, ±2/3 beyond | ~4 |
| `sk_dcBlock(z, x)` | DC blocker: y = x − z; z += y × α | 3 |
| `sk_lfo(phase, rate)` | Per-block LFO: phase += rate, parabolic sine approx | ~6 |
| `sk_exp2Neg(x)` | Fast 2^x for x ≤ 0 (integer exponent + cubic fraction, |err| < 1e-4) | ~8 |
| `sk_envFollow(e, x, ka, kr)` | Envelope follower: attack/release asymmetric one-pole | 3 |
| `sk_toS16(x)` / `sk_fromS16(v)` | Float ↔ int16 with SSAT saturation | 2 each |
| `sk_hbDown_t` / `sk_hbUp_t` | 11-tap halfband decimator/interpolator (Kaiser β 4.1; −0.1 dB @ 5.5 kHz, −3 dB @ 9.5 kHz, images < −47 dB above 17 kHz) | ~6 down, ~7/ch up |
| `sk_shifter_t` | Two-head rotating pitch shifter (Hermite reads, triangle crossfade, variable ratio including negative for reverse) | ~24/frame |

**Control block** (`sk_ctl_t`, 64 B):
- `uint32_t magic_owner` — identifies the owning type
- `uint32_t span_off, span_len` — elastic seat boundaries
- Feed state: sides, segment descriptors (start/end/side/lane × 4),
  gains, positions, fractional accumulators, samples-left counters,
  stride, wrap offset/length, bits (8/16), end-fade ramp
- `uint16_t dry_uq15` — initial dry ramp from `effect_dry_uq15`
- `uint8_t feeding, slot_locked` — feed and slot-lock flags

### P1 Delay (Stereo 16-bit Tape)

A stereo delay line using the full available ring (minus prefix). Key
parameters and their effect:

| Row | Range | Effect |
|-----|-------|--------|
| `dly` | 0..127 | Delay time (CrumpBit's `dly` curve, non-linear) |
| `fbk` | 0..127 | Feedback (0 = none, 100 = unity, 127 = 1.20×) |
| `hed` | 0..6 (stepped) | Head count / mode (single, multi-head, reverse) |
| `ton` | 0..127 | Tone (LP/HP balance on feedback path) |
| `dck` | 0..127 | Ducking amount (input envelope reduces wet level) |
| `wid` | 0..127 | Stereo width (mid/side balance on output) |
| `vol` | 0..127 | Output volume (stays 127 for send-bus operation) |

Varispeed: the read head uses Hermite-interpolated 16-bit reads
(`sk_readHerm`) at a fractional position that smoothly chases the target
delay time. Feedback path includes a one-pole tone filter (`sk_onePole`)
and soft clip (`sk_softClip`) for saturation. Two-head pitch shifter
(`sk_shifter_t`) provides `hed` effects (per-head gains, negative ratio
for reverse). Freeze writes zeros to the ring (infinite hold). Ducking
uses `sk_envFollow` to reduce wet gain when input is loud.

### P1 Reverb (Half-Rate Dattorro Plate)

A Dattorro figure-8 plate reverberator running at **half sample rate**
(22.05 kHz) to double effective ring times within the 64 KB ring budget.

| Row | Range | Effect |
|-----|-------|--------|
| `siz` | 0..127 | Room size (scales all delay taps) |
| `dcy` | 0..127 | Decay time (feedback coefficient) |
| `dmp` | 0..127 | Damping (one-pole LP in the feedback loop) |
| `shm` | 0..127 | Shimmer amount (pitch shifter mix) |
| `frz` | 0..2 (stepped) | Freeze mode (off / hold / hold+shimmer) |
| `ton` | 0..127 | Pre-filter tone |
| `vol` | 0..127 | Output volume |

**Halfband filter:** 11-tap Kaiser design (coefficients in `SpaceTables.c`).
Decimator (`sk_hbDownPush2`) takes two 44.1 kHz input samples, outputs one
22.05 kHz sample. Interpolator (`sk_hbUpPush`) takes one 22.05 kHz sample,
outputs two 44.1 kHz samples per channel. Response: ≤ 0.1 dB ripple to
5 kHz (relaxed from the original R-6 spec), −3 dB at 9.5 kHz, aliasing
images below −47 dB above 17.06 kHz.

**Plate structure:** Four allpass stages feeding two cross-coupled delay
lines (the Dattorro figure-8 topology). Shimmer uses two pitch shifters
at octave-up and octave+fifth ratios, mixed into the feedback loop by
`shm`. Freeze sets feedback to 1.0 and mutes input. Ring is always 64 KB
(decision SD5: no 32 KB mode). Plate rings cleared synchronously at init
(decision SD6: foreground, not background).

### P1 Echo (Mono Tape + Plate)

A mono tape delay feeding into a simplified plate reverb. The tape
provides the initial echoes; the plate adds a diffuse tail.

| Row | Range | Effect |
|-----|-------|--------|
| `dly` | 0..127 | Tape delay time |
| `fbk` | 0..127 | Tape feedback |
| `dmp` | 0..127 | Damping across both tape and plate |
| `rfl` | 0..127 | Plate reflection density |
| `ton` | 0..127 | Pre-filter tone |
| `vol` | 0..127 | Output volume |

Mono buffer: the tape delay is mono, freeing ring space for the plate
allpasses. The plate in Echo is simpler than the standalone Reverb (fewer
taps, no shimmer, no freeze).

### P1 Feed-Through Handover

When the effect type changes (e.g., Delay → Reverb), the old effect's
residual audio (its "tail") must not be lost. The feed-through mechanism:

1. **At type change**, the EffectsManager records the outgoing type's ring
   state into the `fxbuf_handoff_t` record (196 B, +16 B from pre-S095):
   segment descriptors (start, end, side, lane) for up to 4 segments,
   wrap boundaries, sample format (8/16 bit), and the
   `FXBUF_STATE_EFFECT_WRITTEN` flag.

2. **At new type init** (`sk_feedBegin()`):
   - If no written effect audio (flag clear) or no segments: span = whole
     usable share; no feed.
   - Otherwise, collect segments, fold sides to incoming shape (stereo:
     L ← L+BOTH, R ← R+BOTH; mono: one side ← everything).
   - Clamp each segment to the 6 s ceiling at its read rate.
   - **Pack** when a folded side has > 1 segment and the packed window
     fits `SK_FEED_PACK_MAX`: sum segments START-ALIGNED (oldest with
     oldest) into a window at the share top; shift = ceil(log2(count))
     halves per doubling to prevent clipping; gain carries 2^shift back.
   - **Span**: largest contiguous region outside the feed window, if ≥
     `seat_min`. If not, the feed window is cut to free space.
   - Fill the control block and set the slot lock.

3. **During rendering** (`sk_feedRead()`): compiled loop variants (by
   8/16 bit × one/two sides) read interpolated samples from the old ring,
   applying gain and end-fade ramp. When all sides reach `left = 0`, the
   feed ends and the slot lock clears.

4. **Feed cut** (`sk_feedCut()`): if a share change removes coverage of
   the feed window, the feed ends immediately and the slot lock clears.

### P1 Drain Into OFF

When the effect type changes to OFF, the outgoing effect does not stop
immediately. Instead:
- The EffectsManager keeps the **physical type** (the old effect) running
  while the **logical type** becomes OFF.
- The old effect renders with muted input (dry = 0).
- The drain ends when the output tail reaches −80 dBFS or the 6 s ceiling.
- At drain end, the physical type becomes OFF and the arena is released.

### P1 Slot Lock

One of the two interpolation slots (shared with wavetable blending) is
locked during a feed, preventing wavetable frame transitions from claiming
it. The lock is set by `effects_setFeedSlotLock(1)` during `sk_feedBegin`
and cleared when the feed ends. The locked slot's cost is paid by the
Advanced budget.

### P1 Resource Impact

| Resource | Before P1 | After P1 | Delta |
|----------|-----------|----------|-------|
| Flash (text) | ~700 KB | ~720 KB | +19,932 B |
| SRAM1 (bss) | — | — | +16 B (handoff) |
| DTCM arena | 126,656 B | 126,656 B (unchanged geometry) | 0 |
| ITCM | 14,032 B | ~14,032 B (no ITCM additions in P1) | 0 |

### P1 Decisions

| ID | Decision | Rationale |
|----|----------|-----------|
| SD1 | Fixed 512 B arena prefix instead of moving header | Simplifies span arithmetic; header never overlaps ring data |
| SD2 | `init` gains `const fx_share_t *share` parameter | Types need share info at construction time |
| SD5 | Reverb ring always 64 KB (no 32 KB mode) | Complexity of two ring sizes not justified; 64 KB gives adequate plate times at half rate |
| SD6 | Plate rings cleared synchronously at init (foreground) | Simpler than background clear; init is between blocks anyway |
| SD7 | Write pointer counts UP; read is `(p - off) & mask` | Forward content order matches the handoff read rule (D24) |
| SD8 | Feed injection via compiled loop variants (no-feed and feed) | Avoids per-sample branch in the hot loop |
| SD9 | Growth at the wrap: length changes when write head crosses old end | Live reads never invalidated; ring grows seamlessly |
| SD10 | 11-tap halfband, R-6 relaxed to ≤ 0.1 dB to 5 kHz | 6 instructions per frame down; images below −47 dB |
| D24 | Handoff read rule: segments read forward (oldest first) | Consistent with the UP write pointer |

---

## Phase 2 — Colloid Instrument

### P2 Summary

The 6th instrument type. Basic class, stack of 12 free-running oscillators
(or 6 PWM) with integer pair loops, division-free TPT SVF filters, mid/side
stereo positioning, ADSR envelopes with automatable sustain switch.

### P2 Architecture

**New folder:** `Core/DSP/Instruments/Colloid/` with files:
- `ColloidVoice.h/.c` — voice struct (1,032 B after P2; 1,112 B after P3),
  pair-loop render, filter, envelope, parameter apply
- `ColloidOsc.h/.c` — integer pair loops in ITCM (`.itcm.s095p2`),
  `col_read()` interpolated read, `col_pulse()` PWM two-read, four loop
  functions
- `ColloidParameters.h/.c` — parameter table, registry, labels, format
  helpers
- `ColloidTables.h/.c` — position tables, interval tables, format tables

**Registry:** `INSTRUMENT_TYPE_COLLOID` added to the instrument type
enumeration. Flags: `INSTRUMENT_FLAG_BASIC`. File extension: `.col`.

### P2 Oscillator Engine

**Integer pair loops:** Each oscillator pair (o, o+1) renders two samples
per iteration using ARM DSP instructions:
- `SMULWB` — signed multiply word by halfword bottom
- `SMLAWB` — signed multiply-accumulate word by halfword bottom
- `SMLAWT` — signed multiply-accumulate word by halfword top

These handle the stereo position (mid/side bus accumulation) without any
floating-point work per sample.

Four ITCM loop functions (`.itcm.s095p2`):
- `col_oscPairStore(busM, busS, ...)` — first pair, stores to buses
- `col_oscPairAdd(busM, busS, ...)` — subsequent pairs, accumulates
- `col_pwmPairStore(busM, busS, ...)` — first PWM pair
- `col_pwmPairAdd(busM, busS, ...)` — subsequent PWM pairs

**Wave read:** `col_read(table, phase)` performs integer interpolated read
from a 1,024-point table (after the sine table reduction). `col_pulse()`
performs the two-read PWM operation (read at phase, read at phase + width,
subtract).

**Stereo position:** Each oscillator has a mid/side position. The pair
loops accumulate into `busM` (mid) and `busS` (side) buses. Final stereo
conversion: `L = M + 2S`, `R = M − 2S` (or the equivalent scaled form).
Position uses a linear mid/side pan law (decision C-D13).

### P2 Filters

Division-free TPT SVF (trapezoidal integrator, no division per sample):
- State-variable filter providing LP/BP/HP modes
- Per-sample coefficient ramps + per-block cutoff slew (decision C-D9)
- Cubic drive (division-free, no tanh)
- 72 evaluations per sample in parallel mode (two filters, 12 oscillators
  feeding each)

After P3 redesign (F9): one filter with offsets (`frq res typ rte fro reo
eno dro`); `str` = stereo parallel (each filter on one side of the stereo
bus), other modes = mono series. `typ`: LP, BP, HP, Notch.

### P2 Envelopes

Lightweight ADSR with sustain switch:
- `atk` / `dec` / `slv` (sustain level) / `rel` — standard ADSR shape
- `sus` — automatable sustain switch (on/off). When on **and** transport
  is running, the envelope holds at the sustain level. When off or
  transport stops, the envelope enters release (decision C-D19).
- `dst` / `amt` — velocity/modulation destination and amount
- `slw` — slew rate for the modulation target

### P2 1,024-Point Sine Table

A cross-engine change applied as step 0 of P2:
- `TABLESIZE` reduced from 4,096 to 1,024
- Readers shift `>> 22` instead of `>> 20`
- Fraction mask `0x3fffff` instead of `0xfffff`
- **Saves 6,144 B flash** (3,072 fewer float entries × 4 B each, minus
  the smaller table)
- Affects all engines that read `sine_table[]`: Drum, Snare, Cymbal,
  HiHat, Alkaline, Colloid, LFOs, CrumpBit

### P2 Resource Impact

| Resource | Before P2 | After P2 | Delta |
|----------|-----------|----------|-------|
| Flash | ~720 KB (post P1) | 711,036 + 5,784 = ~717 KB | +5,784 B net (−6,144 sine + 2,536 step 1 + 9,392 step 2) |
| ITCM | 14,032 B | 14,880 B | +848 B |
| Voice struct | — | 1,032 B | new |
| SRAM1 | — | 0 B new | (voice struct is in tagged runtime slots) |

### P2 Decisions

| ID | Decision | Rationale |
|----|----------|-----------|
| C-D1 | Basic class | Fits within 19,620 cycles/block budget |
| C-D2 | 12 plain / 6 PWM oscillators (device-measured) | Maximises polyphony within budget |
| C-D8 | Sustain switch (not gate length) | Simpler, automatable, works with drum machine UI |
| C-D10 | 1,024-point sine (all engines) | −6,144 B flash; perceptual quality acceptable |
| C-D11 | Mixer-level pan on stereo pair | Reuses existing mixer infrastructure |
| C-D13 | Linear mid/side pan law | Simple, no sqrt per sample |
| C-D19 | Sustain holds only while transport runs | Transport stop = hands off keys; natural behaviour |
| C-D20 | Platform typ/wav/pos shape; one spread row psp | Consistent with Drum/Snare/Cymbal/HiHat parameter organisation |
| C-D21 | PWM: Rec pulse only (at P2; expanded in P3) | Simplest starting point; expanded to 4 forms in F2 |

---

## Phase 3 — Colloid Follow-ups

### P3 Summary

Eleven follow-up items (F1–F11) refining the Colloid instrument and fixing
a filesystem bug discovered during testing.

### F1: Filesystem Freeze Fix

Two bugs in `filesystem.c`:
- `filesystem_createBootIndex_tick()` case 6: reads uninitialised `entry`
  variable. Fix: fetch `entry` from the registry before use.
- `filesystem_scanInstruments_tick()` case 0: did not set `op_close_status`.
  Fix: set `op_close_status` at the start of the case.

### F2: Four PWM Waveforms

Expanded from Rec-only (C-D21) to four PWM waveforms:
- **Rec** (rectangle/pulse) and **Saw**: share a two-read loop with a
  sign parameter. The sign flips the second read for Saw (subtractive vs
  additive pulse).
- **Sin** and **Tri**: share warp pair loops in ITCM (`.itcm.s095p3`).
  Phase warping generates the PWM effect from the base waveform.

New ITCM section `.itcm.s095p3` for the Sin/Tri/Saw warp loops.

### F3: Pitch Slew Replaces Portamento

Per-oscillator `pitchCur[o]` slewed in log2 Hz domain using `alk_exp2f`
(the same fast exp2 from the Alkaline engine). The slew rate is controlled
by a single parameter.

Removed: `por` (portamento time), `pmd` (portamento mode), `lgt` (legato).
These were draft-era parameters that mapped poorly to the free-running
oscillator bank architecture.

### F4: Three Intervals

`in1` / `in2` / `in3` — PM63 semitones (−63..+63 around centre 63).
Oscillators are assigned to intervals by `o mod 4` grouping:
- Group 0: unison (no interval)
- Group 1: `in1`
- Group 2: `in2`
- Group 3: `in3`

Removed: `colIntervals[]`, `colMember[]`, `colRatio[]` (the draft's
table-based interval system, replaced by direct semitone offsets).

### F5: Detune ×5

`COL_DETUNE_MAX_LN` expanded to ±250 cents (was ±50). The detune amount
is applied per oscillator as a pitch offset in the log2 Hz domain, spread
evenly across the oscillator bank.

### F6: Spread/Motion ×5 with Soft-Edge Position Law

Stereo spread (`spr`) and motion (`mot`) ranges expanded ×5. Position law
uses a soft edge to prevent hard panning at extremes: oscillators near the
edge of the stereo field have their position gently curved toward centre.

### F7: PSP ×5 with Power Curve

Wave position spread (`psp`) range expanded ×5. Applied with a `psp^1.393`
power curve to give more resolution in the musically useful lower range.

### F8: Envelope Pages

- **AEG page**: `atk dec slv rel` (attack, decay, sustain level, release)
- **MOD page**: `dst amt sus slw` (destination, amount, sustain switch,
  slew)

### F9: One Filter with Offsets

Redesigned from two independent filters to one filter with offsets:
- Primary parameters: `frq` (frequency), `res` (resonance), `typ` (filter
  type: LP/BP/HP/Notch), `rte` (rate/tracking)
- Offset parameters: `fro` (frequency offset), `reo` (resonance offset),
  `eno` (envelope offset), `dro` (drive offset)
- `str` = stereo parallel mode (each filter on one side of the stereo bus)
- All other modes = mono series

### F10: Post-Filter OTA Tanh Drive and One-Pole Tone

Added after the SVF filter output:
- OTA-style tanh drive (reuses Alkaline's drive tables from
  `AlkalinePostTables.c/.h`)
- One-pole low-pass tone control (also from Alkaline's existing tone
  implementation)

### F11: 50 Rows, 8 Pages

Final parameter layout: 50 rows across 8 menu pages:
- **OSC** — oscillator type, wave, position, spread, intervals, detune,
  motion, count
- **AEG** — attack, decay, sustain level, release
- **MOD** — destination, amount, sustain switch, slew
- **FM** — FM depth, FM source, cross-modulation
- **Click** — transient click, click level
- **FILTER** — frequency, resonance, type, rate, offsets, drive
- **LFO** — LFO parameters
- **MIX** — volume, pan, tone, drive, send

### P3 Resource Impact

| Resource | After P2 | After P3 | Delta |
|----------|----------|----------|-------|
| Flash | ~717 KB | 720,084 B | +2,568 B (856 past P2 budget line) |
| ITCM | 14,880 B | 15,232 B | +352 B |
| Voice struct | 1,032 B | 1,112 B | +80 B |

### P3 Device Measurements

| Configuration | Cost (instr/sample) |
|---------------|---------------------|
| plain 12, stereo | 382.1 |
| pwm 6, Saw/Rec | 324.1 |
| pwm 6, Sin/Tri | 326.1 |

Clipping fix applied on device: `COL_HEADROOM` multiplied by 0.35,
`COL_MAKEUP` multiplied by 1.43. `init.col` defaults rebased to match.

---

## Phase 4 — Effects Bus Tweaks

### P4 Summary

Cleanup and improvement pass on the effects bus: StereoFilter removed,
Space effects simplified, compressor and drive improved, Colloid device
measurements taken.

### T1: Space Types Lose Mix

Space effect types (Delay, Reverb, Echo) now operate as a **100% wet send
bus**. The `mix` parameter is removed. The dry path is kept internally
only for feed-through handover. `vol` stays at 127 (output level).

Implementation: `SpaceRuntime.mix` replaced with `.wet`; dry starts at 0.
Parameter enum indices shifted down by one.

### T2: Plain Value Text and Stepped Rows

17 rows converted to plain (no special formatting). `fbk` unity moved to
parameter value 100 (was 106 for Space effects). CrumpBit `rvb` damping
mapped 0..127 across the full `toneK[ton >> 1]` table. `hed` stepped 0..6,
`frz` stepped 0..2, `rtg` stepped 0..2 (using `space_step()` replacing
`space_stop()` zones).

### T3: StereoFilter Removed

The StereoFilter effect type was removed entirely:
- 4 files deleted (StereoFilter.c/.h, StereoFilterParameters.c/.h)
- Makefile source/object paths updated
- Effect type IDs renumbered: CrumpBit 1, Delay 2, Reverb 3, Echo 4
- **−3.7 KB flash**

### T4: Compressor Improvements

Four changes to the master bus compressor:

**C1 — Deeper threshold and lower makeup reference:**
- Threshold range: −8..−40 dBFS (was −3..−30 dBFS)
- Makeup reference: −20 dBFS (was −12 dBFS)
- Gives more headroom for loud Scenes before compression engages.

**C2 — Filter drive floor raised:**
- Floor: 1.0 (was 0.4)
- Curve: `1.0 + 5.4x²` (was `0.4 + 6x²`)
- Prevents the near-zero drive at low settings that caused thinning.

**C3 — Half makeup on voice distortion:**
- New `distortion_makeup[128]` lookup table generated by
  `tools/gen_dist_makeup.py`
- Distortion gain compensation is halved, preventing the makeup gain
  from over-compensating at high distortion settings and causing perceived
  loudness jumps.

**C5 — Live GR readout:**
- Gain reduction is now visible in real time on the global compressor page.

### P4 Colloid Device Measurements

Final Colloid device measurements from §7:
- **Worst case:** 17,433 cycles/block (wavetable 12 oscillators, stereo
  filter)
- **Cost per instruction:** 1.37–1.41 cycles/instr

This is well within the Basic budget of 19,620 cycles/block.

### P4 Resource Impact

| Resource | After P3 | After P4 | Delta |
|----------|----------|----------|-------|
| Flash | 720,084 B | ~716 KB | −3.7 KB (StereoFilter removal) |

---

## Files Changed This Session

### New files (P1)
- `Core/DSP/Effects/Space/SpaceKit.h`
- `Core/DSP/Effects/Space/SpaceKit.c`
- `Core/DSP/Effects/Space/SpaceEffect.c`
- `Core/DSP/Effects/Space/SpaceParameters.c`
- `Core/DSP/Effects/Space/SpaceTables.c`

### New files (P2)
- `Core/DSP/Instruments/Colloid/ColloidVoice.h`
- `Core/DSP/Instruments/Colloid/ColloidVoice.c`
- `Core/DSP/Instruments/Colloid/ColloidOsc.h`
- `Core/DSP/Instruments/Colloid/ColloidOsc.c`
- `Core/DSP/Instruments/Colloid/ColloidParameters.h`
- `Core/DSP/Instruments/Colloid/ColloidParameters.c`
- `Core/DSP/Instruments/Colloid/ColloidTables.h`
- `Core/DSP/Instruments/Colloid/ColloidTables.c`

### New files (P4)
- `tools/gen_dist_makeup.py` — generates `distortion_makeup[128]` table

### Deleted files (P4)
- `Core/DSP/Effects/StereoFilter/StereoFilter.c`
- `Core/DSP/Effects/StereoFilter/StereoFilter.h`
- `Core/DSP/Effects/StereoFilter/StereoFilterParameters.c`
- `Core/DSP/Effects/StereoFilter/StereoFilterParameters.h`

### Modified files (across all phases)
- `Core/DSP/Effects/EffectsManager.c/.h` — drain logic, logical/physical
  type, slot lock API, type enumeration
- `Core/DSP/Effects/FxBuffer.h/.c` — handoff record +16 B, arena prefix,
  `SK_PREFIX_BYTES`
- `Core/DSP/Effects/CrumpBit/CrumpBit.c` — `rvb` damping, format changes
- `Core/DSP/Effects/CrumpBit/CrumpBitParameters.c` — label/format updates
- `Core/DSP/Instruments/InstrumentManager.c/.h` — Colloid type registration,
  Colloid writers
- `Core/DSPAudio/Oscillator.c` — 1,024-point sine table shift changes
- `Core/DSPAudio/distortion.h/.c` — `distortion_makeup[128]` table, half
  makeup on voice distortion
- `Core/DSPAudio/mixer.c` — compressor threshold/makeup, `mixer_addVoiceStereo*()`
  for Colloid
- `Core/DSPAudio/lfo.c` — 1,024-point sine shift
- `Core/Menu/menu.c` — Colloid parameter pages, compressor GR readout
- `Core/Hardware/SD/filesystem.c` — F1 fixes (boot index case 6, scan
  instruments case 0), Colloid `.col` extension
- `Makefile` — Space and Colloid source paths, StereoFilter removal
- `STM32F765VIHx_FLASH.ld` — ITCM `.itcm.s095p2`, `.itcm.s095p3` sections
- Oscillator table files — `TABLESIZE` 4096 → 1024

---

## CPU Budget After S095

| Share | Cycles/block |
|-------|-------------|
| Basic instrument (×4) | 19,620 |
| Advanced instrument (×2) | 21,803 |
| Effect | 16,634 |
| Shared mixer | 12,784 |
| Outside-mixer | 4,701 |
| Slack | 500 |
| **Total (97% of capacity)** | **156,705** |

Colloid worst case: 17,433 cycles/block (within Basic budget of 19,620).
