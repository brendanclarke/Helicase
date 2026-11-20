# Space Group Effects Modelling Reference

Design rationale and DSP equations for the Space group effects: Delay,
Reverb, and Echo. These are three effect types sharing a common DSP kit
(`SpaceKit`) and operating within the LXR-02's DTCM arena (126,656 B).
This document covers the arena architecture, shared primitives, per-type
signal paths, the feed-through handover mechanism, drain-into-OFF, and
elastic seating. For the firmware implementation, see
`SPACE_GROUP_EFFECTS_MODULE_REFERENCE.md`; for the effects bus architecture,
see `EFFECTS_MODULES_DSP_REFERENCE.md`.

- **Sessions:** S095 P1 (implementation), S095 P4 (bus tweaks).
- **Design origin:** `S095_P1_FX_DELAY_RVB_ECHO.md` (the plan, deleted
  after logging) and its decisions D1–D22 plus implementation decisions
  SD1–SD10.

---

## 1. Architecture Overview

### 1.1 Arena layout

The DTCM `.dtcm_fxbuf` section provides **126,656 B** of fast SRAM,
shared between the active Effect type and voice-unit delay lines. The
arena is divided into a fixed prefix and a variable elastic region:

```
Offset 0                    512               126,655
  ┌──────────────────────┬───────────────────────────────────┐
  │   Prefix (512 B)     │   Usable ring memory              │
  │ ┌─────────┬────────┐ │   (divided by elastic seating)    │
  │ │ctl (64) │hdr(448)│ │                                   │
  │ └─────────┴────────┘ │                                   │
  └──────────────────────┴───────────────────────────────────┘
```

- **Control block** (`sk_ctl_t`, 64 B): magic|owner, span boundaries,
  feed state, dry ramp, slot-lock flag.
- **Type state header** (448 B): type-specific runtime variables (filter
  states, LFO phases, write pointers, ring masks, etc.).
- **Ring memory** (126,144 B): 16-bit sample ring(s) for the active type.

The prefix is at a fixed address (`SK_PREFIX_BYTES` = 512). No ring data
is stored in the prefix. This simplifies span arithmetic: the usable
region is always `[SK_PREFIX_BYTES, share->bytes)`.

### 1.2 100% wet send bus

After S095 P4 (T1), Space effects operate as a **100% wet send bus**:
- The `mix` parameter was removed.
- The dry path is kept internally only for the feed-through handover.
- `vol` (output volume) stays at 127.
- The dry signal is passed through separately by the EffectsManager.

### 1.3 Effect type enumeration

After S095 P4 renumbering: OFF 0, CrumpBit 1, Delay 2, Reverb 3, Echo 4,
count 5. StereoFilter was removed.

---

## 2. Shared Primitives (SpaceKit)

All Space effects share a set of DSP building blocks, implemented as
`static inline` functions or small `.c` bodies in `SpaceKit.h/.c`.

### 2.1 Ring buffer operations

**Ring index** (`sk_ringIdx`):
```
idx = (write_ptr - offset) & mask
```
Power-of-two ring addressing (FV-1 style). The write pointer counts
**upward** (decision SD7), and every read is `offset` samples behind.
This means content is read forward (oldest first), which matches the
handoff read rule (D24): segments export in the order they were written.

**Hermite interpolation** (`sk_readHerm`):
4-point Hermite interpolation on a 16-bit ring buffer. Provides smooth
fractional-sample reads for varispeed delay lines. Cost: ~12 instructions
per read.

### 2.2 Filters

**One-pole** (`sk_onePole`):
```
z += c · (x − z)
```
Used for damping in feedback loops, tone controls, and smoothing. 3
instructions per sample.

**Two-band shelf** (`sk_shelf2`):
```
lp = one_pole(z, x, kL)
y  = lp + kH · (x − lp)
```
Low and high shelf in one operation. Used for frequency-dependent
damping and tone shaping. ~6 instructions.

**TPT SVF tone** (`sk_svfTone`):
Division-free trapezoidal SVF (low-pass output only). Used where
resonance is needed. ~8 instructions.

### 2.3 Waveshaping

**Soft clip** (`sk_softClip`):
```
y = x − x³/3     for |x| ≤ 1
y = ±2/3          for |x| > 1
```
Cubic soft clipper for feedback-loop saturation. ~4 instructions.

**DC blocker** (`sk_dcBlock`):
```
y = x − z
z += y · α
```
High-pass at ~20 Hz. Prevents DC accumulation in feedback loops. 3
instructions.

### 2.4 Modulation

**LFO** (`sk_lfo`):
Per-block parabolic-sine LFO. Phase accumulator with rate parameter.
Used for chorus/modulation effects on delay times. ~6 instructions.

**Envelope follower** (`sk_envFollow`):
```
e += (|x| > e ? ka : kr) · (|x| − e)
```
Asymmetric attack/release envelope detector. Used for ducking (Delay)
and gate/bloom effects (Reverb). Attack 2 ms, release 200 ms typical.
3 instructions.

### 2.5 Sample conversion

**Float ↔ int16** (`sk_toS16` / `sk_fromS16`):
```
sk_toS16(x)  = SSAT((int32_t)(x × 32768), 16)
sk_fromS16(v) = (float)v × (1/32768)
```
Saturating conversion for ring buffer storage.

### 2.6 Fast math

**Fast 2^x for x ≤ 0** (`sk_exp2Neg`):
Integer part goes into the IEEE754 exponent, cubic polynomial covers the
fraction. |rel err| < 1e-4. Used for RT60 decay laws, gain curves. The
CrumpBit twin is `crumpBit_exp2Neg()`, file-static. ~8 instructions.

### 2.7 Halfband filter

11-tap Kaiser design (β = 4.1), implemented as decimator and interpolator:

**Decimator** (`sk_hbDownPush2`):
Takes two 44.1 kHz input samples, outputs one 22.05 kHz sample.
Coefficients `SPACE_HB_C1`/`C3`/`C5` from `gen_space_tables.py`,
normalised so DC is unity.
```
y[m] = c5·(x[2m] + x[2m−10]) + c3·(x[2m−2] + x[2m−8])
     + c1·(x[2m−4] + x[2m−6]) + 0.5·x[2m−5]
```
State: 6 even samples + 3 odd samples (36 B). ~6 instructions per output.

**Interpolator** (`sk_hbUpPush`):
Takes one 22.05 kHz sample, outputs two 44.1 kHz samples per channel.
```
z[2m]   = 2·(c5·(u[m]+u[m−5]) + c3·(u[m−1]+u[m−4]) + c1·(u[m−2]+u[m−3]))
z[2m+1] = u[m−2]    // centre tap = pure delay
```
State: 6 samples per channel (24 B). ~7 instructions per output channel.

**Response:** ≤ 0.1 dB ripple to 5.5 kHz, −1 dB at 7.6 kHz, −3 dB at
9.5 kHz, aliasing images below −47 dB above 17.06 kHz (decision SD10:
relaxed from the original R-6 spec).

### 2.8 Pitch shifter

Two-head rotating pitch shifter (`sk_shifter_t`):

```
delay_a = phase × W
delay_b = (phase + 0.5) × W
gain_a  = 1 − |2·phase − 1|      // triangle crossfade
gain_b  = 1 − gain_a

output = gain_a · hermite_read(ring, wp − delay_a)
       + gain_b · hermite_read(ring, wp − delay_b)

phase += (1 − ratio) / W          // per sample
```

State: `phase` (1 float, 4 B). The two heads always sum to 1 (triangle
crossfade), so there is no level dip at any phase. A negative ratio reads
backward through the window (Delay's `rev` option). Cost: ~24 instructions
per frame.

---

## 3. Delay (Stereo 16-bit Tape)

### 3.1 Topology

```
Input L/R → tone filter → stereo ring buffer (16-bit)
                                   ↓
                            varispeed read (Hermite)
                                   ↓
                            feedback path:
                              tone filter → soft clip → write back
                                   ↓
                            pitch shifter (hed modes)
                                   ↓
                            ducking envelope → wet gain
                                   ↓
                            Output L/R (100% wet)
```

### 3.2 Key parameters

| Parameter | Range | Mapping |
|-----------|-------|---------|
| `dly` | 0..127 | CrumpBit's delay-time curve (non-linear, long tails at high values) |
| `fbk` | 0..127 | 0 = none; 100 = unity (decision P4 T2); 127 = 1.20× |
| `hed` | 0..6 (stepped) | Head count / mode: single, multi-head (2–4 heads), reverse, ping-pong |
| `ton` | 0..127 | Tone (LP/HP balance on feedback path, `sk_onePole`) |
| `dck` | 0..127 | Ducking amount (input envelope reduces wet level, `sk_envFollow`) |
| `wid` | 0..127 | Stereo width (mid/side balance on output) |
| `vol` | 0..127 | Output volume (stays 127 for send bus) |

### 3.3 Feedback gain law

`fbk` parameter 100 = unity (changed from 106 in P4 T2). The gain law:
```
gain = space_fbkGain(fbk)    // maps 0..127 to 0.0..1.20
```
Values above 100 provide runaway feedback (1.0–1.20×), self-oscillating
at high settings. Below 100 is standard decaying feedback.

### 3.4 Varispeed

The read position tracks the target delay time smoothly using a fractional
accumulator. When the delay time changes, the read head slides (not jumps)
to the new position, producing a pitch-shift artifact (the "tape" effect).
Hermite interpolation (`sk_readHerm`) provides clean fractional-sample
reads.

### 3.5 Freeze

Freeze (`frz` stepped 0..2) writes zeros to the ring, holding the current
content indefinitely. Input audio is discarded during freeze. Feedback is
set to exactly 1.0 (perfect hold). Two freeze modes: hold only, and
hold + continue writing.

### 3.6 Ducking

The ducking envelope (`sk_envFollow`, attack 2 ms, release 200 ms) tracks
the input level and reduces the wet output proportionally. This keeps the
dry signal prominent during performance and lets the delays bloom in
quieter passages.

---

## 4. Reverb (Half-Rate Dattorro Plate)

### 4.1 Topology

```
Input L/R → halfband decimator (44.1 → 22.05 kHz)
                     ↓
              input diffusion (4 allpass stages)
                     ↓
              figure-8 plate:
                ┌─ delay line A → AP → damping → ×decay ─┐
                │                                          │
                └─ delay line B → AP → damping → ×decay ─┘
                            (cross-coupled)
                     ↓
              shimmer: 2 pitch shifters (oct up, oct+5th)
                     ↓ (mixed by shm)
              output taps (stereo from decorrelated positions)
                     ↓
              halfband interpolator (22.05 → 44.1 kHz)
                     ↓
              Output L/R (100% wet)
```

### 4.2 Half-rate operation

The Reverb runs at **22,050 Hz** (half the 44,100 Hz sample rate) using
the 11-tap halfband filter pair. This doubles the effective delay times
within the 64 KB ring budget:
- At 44.1 kHz, 64 KB of 16-bit samples = 0.74 s
- At 22.05 kHz, 64 KB of 16-bit samples = 1.48 s

The halfband decimator takes 2 input samples per output sample. The
interpolator produces 2 output samples per input sample per channel.

Decision SD5: the Reverb ring is always 64 KB (no 32 KB option). The
simplification was judged worth the fixed memory cost.

### 4.3 Key parameters

| Parameter | Range | Mapping |
|-----------|-------|---------|
| `siz` | 0..127 | Room size (scales all delay-line tap lengths) |
| `dcy` | 0..127 | Decay time (feedback coefficient in the cross-coupled loop) |
| `dmp` | 0..127 | Damping (one-pole LP in feedback path, `sk_onePole`) |
| `shm` | 0..127 | Shimmer amount (pitch-shifter mix into feedback loop) |
| `frz` | 0..2 (stepped) | Freeze mode: off / hold / hold+shimmer |
| `ton` | 0..127 | Pre-filter tone |
| `vol` | 0..127 | Output volume |

### 4.4 Dattorro figure-8

The classic Dattorro plate topology: two delay lines cross-coupled through
allpass stages and damping filters. Each delay line feeds the other's input
(the "figure-8" or "figure-of-eight" topology). The decay coefficient
controls the feedback amount; at 1.0 the reverb rings forever (freeze).

Allpass stages provide input diffusion (4 stages before the plate) and
in-loop diffusion (1 stage per delay line). All allpass delay times are
derived from `siz` using prime-number-based ratios to avoid metallic
resonances.

### 4.5 Shimmer

Two pitch shifters (`sk_shifter_t`) in the feedback loop:
- Shifter 1: octave up (ratio = 0.5, reads at half speed)
- Shifter 2: octave + perfect fifth up (ratio ≈ 0.333)

The `shm` parameter crossfades between the unshifted feedback and the
shifted output. At `shm` = 0, no shimmer; at `shm` = 127, full shimmer
(crystalline, rising overtones). During freeze+shimmer mode, the shimmer
continues to transform the frozen content.

### 4.6 Synchronous plate clearing

Decision SD6: plate rings are cleared synchronously at init (foreground,
between blocks). This avoids the complexity of background clearing but
means the init takes a brief moment (~0.1 ms for 64 KB memset).

---

## 5. Echo (Mono Tape + Plate)

### 5.1 Topology

```
Input (summed to mono) → mono tape delay ring (16-bit)
                                  ↓
                          varispeed read (Hermite)
                                  ↓
                          feedback path:
                            damping → soft clip → write back
                                  ↓
                          → simplified plate reverb
                            (fewer taps, no shimmer, no freeze)
                                  ↓
                          Output (stereo from plate taps)
```

### 5.2 Key parameters

| Parameter | Range | Mapping |
|-----------|-------|---------|
| `dly` | 0..127 | Tape delay time |
| `fbk` | 0..127 | Tape feedback (unity at 100) |
| `dmp` | 0..127 | Damping across both tape and plate |
| `rfl` | 0..127 | Plate reflection density |
| `ton` | 0..127 | Pre-filter tone |
| `vol` | 0..127 | Output volume |

### 5.3 Design rationale

Echo combines the rhythmic echoes of a tape delay with the diffuse tail
of a plate reverb. The mono tape buffer saves ring space (half the memory
of stereo), which is shared with the plate allpasses. The plate in Echo
is simpler than the standalone Reverb: fewer allpass stages, no shimmer,
no freeze mode, and no halfband filter (the plate runs at full rate but
with shorter delay lines).

---

## 6. Feed-Through Handover

### 6.1 Problem

When the effect type changes (e.g., Delay → Reverb), the old effect's
residual ring content would normally be lost. The feed-through mechanism
preserves this audio by reading it from the old ring and injecting it
into the new effect's processing loop.

### 6.2 Handoff record

The `fxbuf_handoff_t` record (196 B, in SRAM1) captures the outgoing
effect's ring state at the moment of type change:
- Up to 4 segment descriptors (start offset, end offset, side, lane)
- Wrap boundaries (offset and byte length)
- Sample format (8-bit or 16-bit)
- `FXBUF_STATE_EFFECT_WRITTEN` flag

The 16 B growth from pre-S095 (180 B → 196 B) accommodates the additional
feed-through state fields.

### 6.3 Feed begin (`sk_feedBegin`)

Called at the new effect's `init`:

1. **No feed** when: handoff has no written effect audio (flag clear),
   no segments, or wrap bytes = 0. Result: span = whole usable share.

2. **Collect segments** from the handoff record.

3. **Fold sides** to the incoming type's shape:
   - Stereo incoming: L ← L and BOTH segments, R ← R and BOTH
   - Mono incoming: one side ← everything

4. **Clamp** each segment to the 6 s ceiling at its read rate.

5. **Pack** (optional): when a folded side has > 1 segment and the packed
   window fits `SK_FEED_PACK_MAX`, sum the segments START-ALIGNED (oldest
   with oldest) into a window at the share top. The shift =
   ceil(log2(count)) halves per doubling prevents clipping; the gain
   carries 2^shift back. Sources are read before any window byte is
   written.

6. **Compute span**: the largest contiguous region of the usable share
   outside the feed window, if ≥ `seat_min`. If too small, the feed
   window is cut to free space (the region losing fewer fed samples is
   freed; cut segments fade over min(882 samples, ¼ length)).

7. **Fill control block**: magic|owner, span, feed fields, dry ramp, and
   set the slot lock.

### 6.4 Feed read (`sk_feedRead`)

Called every block during the feed:

Four compiled loop variants by (8/16 bit) × (1 side / 2 sides):
- Linear interpolation between consecutive ring samples
- Gain and end-fade ramp applied per sample
- `frac += step`; carry advances read position with wrap
- When `left = 0` for all sides, the feed ends and slot lock clears

The caller passes the feed output to `sk_feedRead` and uses the return
value (1 = feed active, 0 = no feed) to select between the feed-injection
loop variant and the normal no-feed loop.

### 6.5 Feed cut (`sk_feedCut`)

If a share change (elastic reseating) removes coverage of the feed
window, the feed ends immediately. This prevents reading from memory now
owned by a voice unit.

---

## 7. Drain Into OFF

When the effect type changes to OFF:

1. The EffectsManager sets the **logical type** to OFF but keeps the
   **physical type** (the outgoing effect) running.
2. The outgoing effect renders with muted input (dry = 0).
3. The EffectsManager monitors the output level.
4. The drain ends when: output tail ≤ −80 dBFS, **or** 6 s elapsed.
5. At drain end: physical type → OFF, arena released.

This prevents abrupt tail cutoff on type changes and gives a natural
fade-out.

---

## 8. Elastic Seating

### 8.1 Concept

The 126,656 B arena is divided between the Effect's ring(s) and voice
unit delay lines (CrumpBit sub-types, etc.). The boundary is not fixed:
it adjusts when the Effect type changes or when voice units are
added/removed.

### 8.2 Growth at the wrap

Decision SD9: a ring's length changes only when its write head crosses
the old end boundary. This means:
- Live reads from existing ring content are never invalidated
- The ring grows seamlessly without audible artifacts
- The write head sees new memory on its next wrap

### 8.3 `sk_seat()` function

Performs elastic seating: divides the arena between the Effect and voice
units based on each type's minimum requirements. The Effect's `seat_min`
parameter declares the minimum ring size it needs. Voice units declare
their own minimums. The remaining space is allocated proportionally or
given to the highest-priority consumer.

---

## 9. Slot Lock

One of the two interpolation slots (shared with wavetable frame blending)
is locked during a feed-through. This prevents a wavetable blend from
claiming the slot while the feed needs it for fractional-sample reads.

- Set by `effects_setFeedSlotLock(1)` during `sk_feedBegin`
- Cleared when the feed ends (all sides at `left = 0`) or on feed cut
- The locked slot's CPU cost is charged to the Advanced budget

---

## 10. CPU Budget

| Component | Cycles/block |
|-----------|-------------|
| Effect share (total) | 16,634 |
| Delay (estimated, full stereo) | ~12,000 |
| Reverb (estimated, half-rate plate) | ~15,000 |
| Echo (estimated, mono tape + plate) | ~13,000 |

All types fit within the 16,634 effect budget. The Reverb is the most
expensive due to the allpass stages and halfband filter, but the half-rate
operation halves the per-frame cost.
