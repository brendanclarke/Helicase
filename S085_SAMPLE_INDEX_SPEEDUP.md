# S085 sample and wavetable speedup: precomputed indexing and pooled wavetable blending

**Scope:** the per-sample cost of the `smp`/`slp` and `wtb` oscillator
loops:
- what install-time ("upload") preparation can remove;
- what it cannot;
- the user's rule for wavetable frame blending.

**Sources:** the failed S085 sub-repository at
`Helicase-dev-ph7-voice-pwm-spike/`, cited below as `spike/`.

**Cost figures** are static instruction counts per sample (`osccost.py`),
used here only to show where the work is. They are not cycles. Hardware
showed static counts badly understate real load (`S085_POST_MORTEM.md` §1).
No saving below is quantified; each must be measured on hardware with the
worst-case Scene before it is accepted.

---

## 1. Why samples and wavetables cost more than the built-in waves

| Loop | Static count | Δ vs table wave |
|---|---:|---:|
| Built-in table wave (Tri/Saw/Rec) | 25 | — |
| `smp` | 40 | +15 |
| `slp` | 47 | +22 |
| `wtb` 8-bit / 16-bit | 40 / 43 | +15 / +18 |

### 1.1 Built-in table wave

Every table is exactly 1,024 points and one cycle is the full 32-bit phase.
- The index is one shift (`phase >> 22`).
- Wrapping to the next point is one mask (`& 0x3FF`).
- It does 2 reads and 1 interpolation.

Source: `spike/Core/DSPAudio/Oscillator.c:810`, `calcWavetableOscCore()`.

### 1.2 `smp` / `slp`

The loop is in `spike/Core/DSPAudio/OscillatorSample.c`,
`oscSample_block()`. A sample has an arbitrary length and an adjustable
start, so no shift or mask applies. Every sample, the loop:
- clamps the read index to the start;
- tests the index against the end;
- `slp`: wraps the index into `[start, end)`; `smp`: holds the last frame;
- chooses the next point (wrap to start, hold, or +1);
- selects 0 once a one-shot has played out;
- advances the phase with its own wrap or hold test.

All of these tests run every sample, to keep the cost constant.

### 1.3 `wtb`

The loop is in `spike/Core/DSPAudio/OscillatorWavetable.c`,
`oscWavetable_block()`.
- **Frame length:** frames can be any length, so instead of a shift the
  loop does a 64-bit multiply (`phase × frame_len`) for the index and
  fraction, plus a compare to wrap the next point.
- **Frame blend:** it always blends two neighbouring frames: 4 reads and
  3 interpolations, plus a per-sample step of the frame-blend fraction.
- **8-bit frames:** these add sign-extend and shift steps.

### 1.4 Both

- **The sync-test cost (counted):** the `pwm`, sample and wavetable loops
  share one loop between plain and hard-sync playback (`OSC_SHARED_LOOP`)
  to save ITCM. Each pays a 2-instruction test every sample.
- **Flash stalls (not counted):** sample and wavetable data are large and
  live in sample flash, so reads can miss the 16 KB D-cache and wait on
  flash. The built-in tables are small and stay cached.

---

## 2. What install-time preparation can remove

### 2.1 Wavetables: power-of-two frame lengths

**The change:** every wavetable's frame length is a power of two (64, 128,
256, 512 or 1,024 points), guaranteed at install.
- The bind computes `shift = 32 − log2(len)` and `mask = len − 1` once,
  when the table is selected.
- The loop then indexes like the built-in waves:
  `idx = phase >> shift; nx = (idx + 1) & mask`.

**Removed:** the 64-bit multiply and the next-point wrap compare.

**Most of this already exists:**
- `tools/sampleoptimize.py` already picks the smallest power-of-two length
  that "plays the same". The S084 `ak_hvox` set came out at 256 points.
- But when no shorter length passes, it **keeps the original length**
  (e.g. 600), and the firmware accepts any length. That is why the multiply
  exists.
- **The fix:** always round up to the next power of two, in the tool, the
  installer, or both. The installer already requires one length per table.

**Side benefit:** a short power-of-two frame pair (256 points × 16 bits ×
2 frames = 1 KB) stays in the D-cache.

**Not removed:** the frame blend. See §3.

### 2.2 Samples: end padding

**`smp` (one-shot):**
- **The change:** append a run of silent frames at install.
- **Removed:** reading past the end returns 0 by itself, so the
  "played-out → 0" select and the next-point hold test go away.
- **What stays:** one cheap clamp on the phase. At high pitch the read can
  run past any fixed padding.

**`slp` (loop):**
- **The condition:** padding works only if the loop region is **fixed at
  install**, for example the whole sample or the file's `smpl` loop points.
- **The change:** append copies of the loop's first points after the end.
- **Removed:** the next-point read never needs a wrap test.
- **What stays:** the phase wrap itself (a compare and a subtract).

**Conflicts with the current design:** in the spike the loop starts wherever
`mod` (the start) puts it, which changes at runtime, so the padding cannot
be precomputed. A fixed loop region means `mod` only moves the initial
playback position, not the loop start. **This is a user decision.**

### 2.3 Not removable by upload-time preparation

| Item | Why it stays |
|---|---|
| The start clamp | It exists only because FM can push the read before the start. It can be dropped from the **non-FM** loop, but that is a code split, not an upload change. |
| The per-sample fraction and interpolation | Samples play at arbitrary pitch. |
| The sample gain | Already applied once per block, not per sample. Nothing to save. |
| The 2-instruction sync test | Comes from sharing one loop between plain and sync playback. Separate loops remove it if ITCM allows. |
| Flash stalls on long samples | Inherent to long data. |

### 2.4 Not related to this document

`pwm` (42–58 static per sample) is what drove the CPU to 100 % on hardware.
Nothing here changes it.

---

## 3. Wavetable frame blending uses the render pool (user rule)

### 3.1 The rule

- **Blending uses a pool slot.** Blending between two wavetable frames uses
  one of the **two dynamic interpolation slots** (the render pool).
- **It competes for that slot** with:
  - other wavetables' blends;
  - the `itp` run mode's crossfades;
  - `wav`-type waveform interpolation.
- **Fallback:** when no slot is free, the wavetable falls back to **simple
  stepping**, which plays a single frame.

### 3.2 What the spike did instead (finding)

**No.** The spike's `wtb` never used the pool:
- **The blend was built into the loop.** `oscWavetable_block()` always picks
  a frame pair (`oscWavetable_pair()`) and blends it every sample. Every
  `wtb` oscillator paid the 4-read, 3-interpolation cost at all times, with
  no fallback.
- **Pool claims existed in only two places:**
  - waveform interpolation, for the `wav` type only
    (`spike/Core/DSPAudio/modulationNode.c:776`:
    `osc->oscType == OSC_TYPE_WAV && oscRenderPool_claim(1u)`);
  - the `itp` crossfade (`spike/Core/DSPAudio/OscSection.c:89`, `:103`).

### 3.3 What a re-implementation needs

1. **Base `wtb` loop: one frame.**
   - 2 reads and 1 interpolation, with the power-of-two indexing of §2.1.
   - The position selects one frame per block ("simple stepping").
   - This is the cost every `wtb` oscillator pays, always.
2. **Blend unit.**
   - When a slot is granted, the oscillator renders the frame pair and its
     blend for that block, as in the spike.
   - It is granted per block through `oscRenderPool_claim(1u)`.
3. **Claim order** (the spike's order, with `wtb` added):
   1. ongoing `itp` crossfades (never cut);
   2. waveform interpolation: `wav`-type wave blends **and `wtb` frame
      blends**;
   3. new `itp` crossfades, lowest slot first.
4. **Budget.**
   - The pool stays budgeted as **always full**: 2 × the dearest unit
     render.
   - Measure whether a `wtb` blend unit is dearer than the current dearest
     unit (the `itp` old-carrier copy, ≤ 82 + blend 23 static). If it is,
     the pool's worst case rises.
   - The win: the per-oscillator `wtb` worst case drops to the one-frame
     loop. Today every `wtb` oscillator in every voice pays the blend.
5. **Decisions still open (user):**
   - **When a `wtb` oscillator claims:** every block, only while its
     position is moving, or only when the position falls between frames.
     The worst case is the same in all three, since the pool is budgeted
     full.
   - **Fallback stepping:** snap to the nearest frame of the target at each
     block start, or move one frame per block toward it. Snapping jumps;
     one-frame-per-block glides with steps.
   - **Losing a slot mid-glide:** whether a blending oscillator that loses
     its slot between blocks snaps to the nearest frame. This is audible as
     a step.
