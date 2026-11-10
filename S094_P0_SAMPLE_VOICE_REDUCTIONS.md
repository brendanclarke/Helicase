# DRAFT — Sample and Voice Reductions

- **Status:** draft proposal for review (S093, after P8). **No firmware code is
  changed by this document.** Every figure marked *measured* comes from a
  linked scratch build of the shipped P8 tree (§6). Figures marked *estimate*
  are not gated and must be measured before any schedule is written (the P7
  lesson, `S093_P8_WAVETABLE_REASSESSMENT.md` §1).
- **Scope (user, 2026-11-10):**
  1. remove the built-in crash wave and put a substantially similar sample
     on the card, for the user to install if wanted (§2);
  2. why the wavetable sync blend is so expensive; reductions, or dropping it
     where it is not needed (§3);
  3. Alkaline reductions that would bring it under budget, with room for a
     post distortion (§4).
- **Context:** the Alkaline wavetable frame blend with a slot (P8 revision 3,
  `S093_P8_WAVETABLE_REASSESSMENT.md` §14) is accepted slightly over budget
  for now. The crash wave stays until this draft is decided.
- **Binding rules that apply:**
  - S089: no per-sample cost may rise without an equal or larger measured
    saving; the worst-case Scene is set by the dearest settings;
  - constant per-sample cost: the longest path counts, per block too;
  - `MEMORY.md` document rule: no step in this draft or in a schedule made
    from it edits `MEMORY.md` or `knowledge_files/`. Statements it would make
    out of date are listed in §5 for the user to log.

---

## 0. Summary

| Item | What it frees (measured unless marked) | Sound / feature cost | Recommendation |
|---|---|---|---|
| **§2 Remove crash** | Drum −3, Snare −3, Cymbal/HiHat −6 per sample; each interpolation slot −3 (pool −6); 32,769 B flash; ≈ 540 B ITCM plus the crash path inlined in `calcNextOscSampleBlock` | `Cym` leaves the stock list; the same sound comes back as a card sample under `slp` / `l` | Do it, with the card sample generated from the ROM data (bit-exact points) |
| **§3 Sync blend** | Nothing in any worst case: the only place it runs (Drum `snc` with a slot) totals 52 against Drum's dearest 81. Dropping it frees 536 B ITCM | Frame steps instead of blends in Drum `snc` on a wavetable | Keep it. Drop only if ITCM is needed |
| **§4 Alkaline** | Needs ≈ 8–10 per sample for a separate distortion pass, ≈ 1–4 for a fused one (estimates). Candidates total ≈ 3–13 per sample (estimates) | Staggered filter control (64-sample control rate) is the only large lever | Measure-first spike: the control candidates and both distortion placements |
| **§4.7 Alkaline as Advanced** | Advanced ceiling measured at **≈ 292.9** (Cymbal/HiHat dearest): headroom **36.4** per sample (no slot), **21.4** (slot held); the slot over-budget disappears | At most two Advanced per Scene, shared with Cymbal/HiHat; the brief was Basic | Any division-free shaper fits with no reductions; the engines' own `dist` fits on instructions but is marginal on the cycle proxy |
| **§4.8 Fold, phaser, flanger as Advanced** | Wavefolder 15–25 (measured); 4-stage phaser ≈ 32 and flanger ≈ 38 (loops measured standalone) against a binding headroom of 32.7 (no slot) / 17.7 (slot held) | The phaser and a 256-point flanger fit in the slot's free RAM (≈ 700 B); a chorus does not | Without reductions: any fold, or a 4-stage phaser (just), with the slot case removed by making effect and blend exclusive. With §4.4's reductions: 6-stage phaser or flanger too |

**One finding changes how the budget is read:** the per-voice worst cases are
set by the `slp` loops and the crash loop, not by the wavetable readers.
P8's shorter wavetable readers lower no voice's worst case. The interpolation
pool's per-slot ceiling is set by the `itp` fade (45), not by any blend (§1).

---

## 1. The budget today (P8 image, measured)

Linked loop counts, `elf_looppath.py --cond-heads --in-range`. "Dearest" is
the most expensive choice an oscillator can make.

### 1.1 Per voice, no slot held

| Voice | Oscillator section (dearest) | Whole voice | Without crash |
|---|---|---:|---:|
| Drum (`fm`, `slp` carrier, the Basic ceiling) | carrier `osc_slpFmRampBlock` 51 + modulator 30 (crash) | **261.3** | 258.3 (modulator 27, `slp`) |
| Snare | noise + tonal 30 (crash) | not frozen | −3 |
| Cymbal / HiHat | modulators 30 + 30 (crash) + carrier `osc_slpFmBlock` 50 = 110 | not frozen (S093 P0 estimated ≈ 270 with 27 + 27; ≈ 276 with crash) | −6 (104) |
| Alkaline (`wtb`, revision 3) | fused ladder loop 213 | **256.5** | unchanged |

The P8 wavetable readers fell 1–5 per sample (plain 26 → 22, blend 38 → 33,
FM blend ramp 47 → 42), but none was ever the dearest choice for its
oscillator, so no row above changed between the P6 and P8 images.

### 1.2 The interpolation pool (2 slots)

What one slot can add on top of its voice's dearest no-slot setting:

| Slot user | P8 | P8 without crash |
|---|---:|---:|
| Wavetable blend on a plain oscillator (Drum modulator or `mix`, Snare tonal, Cymbal/HiHat modulators) | 33 − 30 = **3** | 33 − 27 = **6** |
| Wavetable blend on an FM, ring or sync carrier | **0** (never dearer than the voice's own dearest) | 0 |
| **`itp` fade** (old carrier rendered plain + the 15-instruction crossfade) | 30 + 15 = **45** | 27 + 15 = **42** |
| Alkaline wavetable blend (revision 3) | 228 − 213 = **15** | 15 |

The pool's worst case is two slots at the dearest user: **90** (84 without
crash), set by the `itp` fade on both images. The accepted over-budget is the
Alkaline voice holding a slot: **271.5 against 261.3**.

---

## 2. Remove the crash wave; put it on the card

### 2.1 What crash is today

- **Data:** `crashSample[32769]` (`Core/DSPAudio/Samples.c` l. 67,
  `Samples.h` l. 52): unsigned 8-bit, 32,768 points plus a guard point equal
  to point 0. Measured range 15..254.
- **Playback:** a `<<17` point index over the whole 32-bit phase, so it loops
  the whole sample continuously; converted per sample as `(u − 127) × 256`.
- **Pitch law:** `osc_calcSampleFreqValue()`, f/fs × 2^27 in `<<17` units:
  **1,024 points per nominal period.**
- **Loops (measured, P8 image; all in ITCM):**

  | Mode | Function | Per sample | Size |
  |---|---|---:|---:|
  | plain | crash path inside `calcNextOscSampleBlock` (the `ldrb` loop) | **30** | part of 816 B |
  | FM | `calcSampleOscFmBlock` | 32 | 168 B |
  | ring | `calcSampleOscRingBlock` | 33 | 196 B |
  | sync slave | `calcSampleOscSyncBlock` | 36 | 176 B |

- **Why it matters:** the plain crash loop (30) is the dearest plain read in
  the system. Every modulator and every plain oscillator is budgeted at 30
  because of it; the next dearest is `slp` at 27. Its FM, ring and sync copies
  are never the dearest (the `slp` copies are 50/51, 42 and 41).

### 2.2 What removing it frees

| Where | Saving |
|---|---:|
| Drum ceiling (the modulator) | 261.3 → **258.3** |
| Snare (tonal oscillator) | **−3** |
| Cymbal / HiHat (two modulators) | **−6** |
| Interpolation pool (`itp` fade's old carrier, per slot) | 45 → 42, pool 90 → **84** |
| Program flash | **32,769 B** (data) + the scalar `calcSampleOsc()` / `calcSampleOscFm()` |
| ITCM | **≈ 540 B** (FM, ring, sync copies) + the crash case inlined in `calcNextOscSampleBlock` |

Under S089 these savings are what pays for new per-sample work. The Alkaline
(§4) gains nothing directly: it never reads crash.

### 2.3 The card sample

**Proposed file:** `SD_CARD/samples/02_crash.wav` (display name `crash`;
`01_cut.wav` is already there, so it lists as `s02` / `l02`).

| Property | Value | Why |
|---|---|---|
| Format | PCM 16-bit mono (D10: 8-bit sources are refused) | |
| Frames | **32,769**: the 32,768 points + the guard point | the seam interpolates into point 0, as crash does |
| Conversion | `s = (u − 127) × 256`, exact | range 15..254 gives −28,416..+32,512: no clamp, no overflow |
| `smpl` loop | start 0, end 32,767 (inclusive) | the loop is the whole cycle; the guard is the next point at the seam |
| Sample rate field | 44,100 | not used for pitch (below) |
| Size | ≈ 65.6 kB on the card; ≈ 65.5 kB of sample flash when installed (2 B × 32,769 + two pad frames) | |

- **Same pitch:** the user-sample law (`f × 2^22 / REAL_FS`, Q.12) is also
  1,024 points per nominal period. Played as `slp` (a carrier `typ`) or as an
  `l` entry (a modulator), the card sample runs at crash's pitch for the same
  note.
- **Same points:** each stored point is crash's point after its own
  `(u − 127) × 256`. Between points the `slp` reader interpolates the 16-bit
  values; crash interpolates the 8-bit values then scales. They differ only
  by int16 truncation order (inaudible; a host law can bound it).
- **Generated, not hand-made (proposed):** a host script, for example
  `tools/samples/make_crash_wav.py`, reads `crashSample` from `Samples.c` and
  writes the WAV with its `smpl` chunk. The ROM array is the single source;
  the script runs once before the array is deleted, and its output is kept on
  the card.
- **Where it can play:** every oscillator that offers `Cym` today can select
  user samples:
  - carriers through `typ` `slp` (`osc1_type`, FILESYSTEM_SPEC);
  - modulators (`OSC_TYPE_MODLIST`: Drum osc2, Cymbal/HiHat osc2/osc3)
    through `l` entries, which `osc_renderUserList()` plays with the `slp`
    plain loop.

**Behaviour differences to accept or check:**
- **Cost per use:** on an FM carrier the sample runs the `slp` FM loop (50/51)
  instead of crash FM (32 + the separate ramp pass). This is within the
  ceilings, which are already set by `slp`.
- **`pos`:** crash takes `pos` as a phase offset (`osc_positionIsPhase()`,
  `Oscillator.h` l. 686); a `slp` item follows the typed `pos` rule. Re-check
  the rule at schedule time.
- **`snc`:** as a sync master, `osc_syncClockInc()` scales a `slp` master
  `<<10` (crash `<<5` from its own units); both clock once per nominal
  period. As a slave, `osc_slpSyncBlock` (41) replaces crash's copy (36).
- **The sound needs installing:** it uses one of the 128 sample slots and
  sample flash, and is absent until the card is installed.

### 2.4 Code removal scope (for a later schedule)

| File | Change |
|---|---|
| `Core/DSPAudio/Oscillator.h` | `CRASH` (l. 54) removed; `OSC_STOCK_LAST` → `NOISE` (l. 68); `OSC_SAMPLE_START` 6 → 5 (l. 69), as S093 P3 did for Re3/Sw3 (l. 55–66); `osc_positionIsPhase()` loses the `CRASH` term (l. 686); prototypes l. 378, 381, 417, 419 |
| `Core/DSPAudio/Oscillator.c` | the `CRASH` cases: FM dispatch (l. 292), scalar FM (l. 331), plain dispatch (l. 421), scalar (l. 456), plain gain-1 (l. 790), sync dispatch (l. 992), ring dispatch (l. 1189), `osc_setFreq` (l. 1333); `osc_syncClockInc()` branch (l. 819); the functions `calcSampleOscFmBlock` (l. 517), `calcSampleOscFm` (l. 544), `calcSampleOscBlock` (l. 568), `calcSampleOsc` (l. 593), `calcSampleOscSyncBlock` (l. 936), `calcSampleOscRingBlock` (l. 1136) |
| `Core/DSPAudio/Samples.c` / `.h` | `crashSample` (l. 67 / l. 52) |
| `Core/Menu/MenuText.h` | `waveformNames`: `Cym` removed, count 6 → 5 (l. 149); the `menu.c` assert follows `OSC_SAMPLE_START` |

- **Stored values:** a modulator's user entries move down one index (5 was
  `Cym`). No backward compatibility, as for Re3/Sw3: kits and instruments on
  the card that use `Cym` or a modulator user entry are re-pointed by hand
  or re-saved. The schedule scans `SD_CARD/Kit`, `Instrument`, `Bank` and
  `Scene` for affected values and lists them.
- **Gates:** `elf_p4_check` reports the crash loops MISSING by design and
  re-baselines `osc.txt`; the mixer stays 0 grew; ITCM shrinks; host tests
  that select `Cym` are rewritten to the card sample or removed.

### 2.5 Decisions

| # | Question | Recommendation |
|---|---|---|
| C-D1 | Remove crash | Yes, with the card sample |
| C-D2 | Card file | `02_crash.wav`, 32,769 frames, `smpl` loop 0..32,767, generated from the ROM array |
| C-D3 | Re-pointing stored values | No remap code; the schedule lists the affected card files |

---

## 3. The wavetable sync blend

### 3.1 Where it runs

Only one place: **Drum osc1 in `snc`, on a wavetable, holding a slot**
(`osc_renderTypedSync()`, called from `DrumVoice.c` l. 720). No other
instrument has a sync slave, and modulators never sync.

### 3.2 Why it costs 45 (measured, inner loop at `0x2598` of the P8 image)

| Part | Instructions |
|---|---:|
| One frame read with interpolation (as `osc_wtbPlainBlock`) | 22 |
| `osc_syncPull()`: `over[i]` load, `umull` (ratio), `smull` (depth), shifts, conditional add | ≈ 10 (sync 32 − plain 22) |
| Second frame read, its interpolation, the frame mix, the `ff` ramp | ≈ 11 (as plain blend 33 − 22) |
| **Two stack reloads** (`inc`, `ratioQ16`): the pull's constants plus two frame pointers exceed the integer registers | 2 |
| **Total** | **45** |

- The plain blend and the plain sync copy have no reloads; only their
  combination spills. That is why its premium is +13 against +11 for the
  plain and FM blends.
- The reader also has a per-segment cycle (73, shortest 14) when a block
  crosses a frame; it runs at most twice per block.

### 3.3 What it costs the budget

- **Nothing in any worst case.** Drum `snc` with a slot is the sync list
  builder (7) + 45 = 52, against Drum's dearest 81 (FM carrier + modulator).
  Its slot excess is 0 (§1.2).
- Under the S087 convention (premium over the same reader without a blend),
  it is the largest premium, +13. That convention overstates the pool: the
  `itp` fade sets the pool's ceiling.

### 3.4 Reductions tried (measured)

| Variant | Loop |
|---|---:|
| P8 (two frame pointers) | 45 |
| One pointer, frame B read at `+2,048 B` (possible since every P8 row is 1,024 points) | **45** (no change) |

A real reduction would mean removing the spill: fewer live values in the
pull (for example a 32-bit ratio). That changes the S085 sync law and its
host proofs, for no worst-case gain.

### 3.5 Options

| | What | Frees | Sound |
|---|---|---|---|
| **S-A (recommended)** | Keep | — | Frame blending in `snc` |
| S-B | Drop the sync blend: `snc` + wavetable plays the nearest frame (`osc_wtbSyncBlock`, 32) and never claims | 536 B ITCM; one claim site. No CPU in any worst case | Frame steps in Drum `snc` only |
| S-C | Drop the blends on all carriers that are never dearest (FM 528 B, FM ramp 520 B, ring 572 B, sync 536 B) | ≈ 2.2 KB ITCM. No CPU in any worst case | Frame steps on Drum FM/ring/sync and Cymbal/HiHat carriers |

Dropping blends buys ITCM only. Choose S-B or S-C only if a later feature
needs ITCM (12,128 / 16,384 B used today).

---

## 4. Alkaline: under budget, with room for a post distortion

### 4.1 Where it stands (measured, revision 3 build)

| | Figure |
|---|---:|
| Loop, plain / blend | 213 / 228 per sample, 3 VDIV |
| Per block (`control_cost.py --elf`) | async 970 + sync block 407 + dispatch 14 = **1,391** (43.5 per sample), 9 VDIV |
| Whole voice, no slot / slot held | **256.5** / **271.5** |
| Ceiling | 261.3, **3 VDIV** (`baselines/s093p4/drum_ceiling.txt`) |

The Alkaline already has 3 + 9/32 VDIV per sample.

### 4.2 What a post distortion costs

- **The engines' curve** (`distortion_curveSampleF()`, `distortion.h`
  l. 78): `x(1+k)/(1+k|x|)`, **one VDIV per sample**. The Cymbal/HiHat gain +
  distortion pass (`voicePost_gainDist()`) is **21** per sample linked.
  Any division-based shaper puts the Alkaline over the VDIV ceiling,
  whatever the instruction count.
- **So the shaper must be division-free** (estimate): for example a clamped
  cubic soft clip, `x' = clamp(g·x, ±1.5)`, `y = x' − (4/27)·x'³`, with the
  drive `g` and a make-up gain per block. About 6–8 instructions per sample
  in registers. It is a new curve, not the engines' `dist`.
- **Placement:**

  | | Per sample (estimate) | Reductions needed (target 261.3, no slot) |
  |---|---:|---:|
  | Separate pass over `buf` (load, convert, shape, convert, store) | ≈ 12–15 | ≈ 8–10 |
  | Fused at the ladder output, before the int16 conversion | ≈ 5–8 if it adds no spill | ≈ 1–4 |

  Fused is cheaper on paper, but the B2R loop has 14 stack references per
  sample, and every new live value has cost spills before (P8 reassessment
  §1: a 3-instruction change cost +7). Both are measured in the spike.

### 4.3 Where the per-block time goes (standalone counts, `control_cost.py`)

| Stage | Instructions | VDIV |
|---|---:|---:|
| `alkMeg_step` (main envelope: Newton step, 2 `lnf`, 1 `expf`) | 247 | 2 |
| `dl_slowUpdate` (network rows, in the render) | 228 | 0 |
| `alk_expoCurrent` (two Thevenin pairs, `expf`, two `lnf` passes) | 190 | 3 |
| `alkAcc_step` (one solve already, `ALK_ACC_SOLVES` 1) | 129 | 3 |
| `alkMeg_n53` | 73 | 0 |
| `alkVeg_step` | 45 | 0 |
| `alk_prewarp` (rational) | 36 | 1 |
| `DiodeLadder_setNet` | 32 | 0 |
| Rest of the async (pitch, glide, two `alk_exp2f` for drive, table frequency, wavetable control) | ≈ 340 | 0 |
| Render entry/exit beyond `dl_slowUpdate` | ≈ 100 | 0 |

### 4.4 Candidates (estimates, to be measured)

| # | Change | Est. per sample | Sound / risk |
|---|---|---:|---|
| **A1** | **Staggered control.** The filter chain (MEG → accent → expo → prewarp → `setNet`, ≈ 630 per block) runs on even blocks; the pitch, VEG, drive and wavetable control on both; the ladder ramps its currents across 64 samples instead of 32. The longest block, not the average, is gated | **≈ −8 to −10** | Filter control at 1.45 ms instead of 0.73 ms (the N1 network already runs at 64 samples). Needs the loop's per-block ramp extended to two blocks. The only large lever |
| A2 | The knob-only terms of `alk_expoCurrent` (cut: `vth3`, `a3`; env: `(R61+r5)/(R61+VR)`, `rth5`) from 128-entry tables, indexed by the parameter | ≈ −1 to −1.5 | 2 KB flash. Divisions stay |
| A3 | Drive gain and make-up from a 128-entry table (replaces two `alk_exp2f` per block) | ≈ −1 | 1 KB flash |
| A4 | `ALK_EXPO_PASSES` 2 → 1 | ≈ −0.6 | Accuracy of the expo current; a host law bounds the error |
| A5 | Render entry/exit: the callee-saved float registers and state loads (≈ 100) | unknown | Inspect first |
| A6 | The blend loop itself (measured, §6) | one pointer **+2** (230); constant mix per block **−1** (227) | Not worth it |

- **Without A1:** A2–A4 total ≈ −3 per sample. That covers a fused shaper
  only if it costs ≈ 7 or less.
- **With A1:** about −11 to −13, enough for either placement. The no-slot
  voice with a fused shaper would be ≈ 249–254; with a separate pass
  ≈ 256–261.
- **The slot-held figure** stays about 15 above the no-slot one (≈ 264–276
  with a shaper). That +15 is charged to the pool, whose ceiling is the `itp`
  fade's 45 (§1.2); it stays over the per-voice 261.3, as accepted now.

### 4.5 Proposed next step

A linked spike outside the tree, as P8-1:
- A1–A4 one at a time, then combined: `control_cost.py --elf` (the longest
  block for A1), `elf_looppath.py` on the loops;
- the shaper in both placements: loop count, VDIV, `elf_p4_check` (the mixer
  must stay 0 grew; the Alkaline render moved the mixer twice in P8);
- host laws: the stagger's filter response against the per-block reference,
  and the shaper's curve.

Only if the spike passes is a schedule written.

### 4.6 Decisions

| # | Question | Recommendation |
|---|---|---|
| V-D1 | A division-free shaper for the Alkaline (not the engines' `dist`) | Yes; curve to be chosen by listening |
| V-D2 | Staggered filter control (A1) | Spike it; decide on the measured figure and a listening test |
| V-D3 | Placement | Decided by the spike |
| V-D4 | Class: Basic with reductions (§4.4), or Advanced (§4.7) | See §4.7.4 |
| V-D5 | Under Advanced, which stage: distortion, wavefolder (after or before the ladder), phaser, flanger | Spike the post-ladder fold and the 4-stage phaser first (§4.8.5) |
| V-D6 | Effect and frame blend exclusive (no slot claimed while an effect is on) | Yes, if a phaser or flanger is chosen |

### 4.7 If the Alkaline becomes Advanced

#### 4.7.1 The Advanced ceiling (measured, P8 image)

No Advanced ceiling had been frozen. S087's "≈ 131" was the oscillator
section only, and S093 P0's "≈ 270" was an estimate built with `slp`
modulators (27) and no per-block count. Measured here with the
`drum_ceiling.txt` method:
- linked loops: `elf_looppath.py --cond-heads --in-range`;
- mixer-inlined loops: source-attributed on a `-g` twin of the P8 image
  (same code);
- per block: `elf_blockpath.py`, with the sync-block entry counted
  standalone (`-Ofast`, no LTO) because LTO inlines it into the mixer.

**Dearest Cymbal (HiHat is the same loops; its carrier runs at gain 0.5):
T+D chain, both modulators on crash, `slp` FM carrier.**

| Per-sample loops | Instructions | VDIV |
|---|---:|---:|
| Modulator 1, crash (`calcNextOscSampleBlock`) | 30 | 0 |
| Modulator 2, crash | 30 | 0 |
| Modulator sum (`bufferTool_addBuffersSaturating`, unrolled `QADD16`) | ≈ 2.5 | 0 |
| Carrier, `slp` FM (`osc_slpFmBlock`) | 50 | 0 |
| ZDF filter (`SVF_calcBlockZDF.constprop.0`, dearest type) | 92 | 2 |
| Transient click (mixer-inlined) | 22 | 0 |
| Post-chain, add + gain + distortion (`voicePost_addGainDist`, mixer-inlined) | 25 | 1 |
| **Loops** | **251.5** | **3** |

| Per block | Instructions |
|---|---:|
| `Cymbal_calcAsyncVoice` | 62 |
| `osc_setFreq` × 3 (carrier and two modulators) | 384 |
| Dispatch (`instrumentManager_calcSlotAsync`) | 14 |
| Filter recalculation (`fastTan`; the S093 P3 figure) | ≈ 30 |
| `Cymbal_calcSyncBlockVoice` entry (standalone) | 133 |
| `SVF_calcBlockZDF` entry | 161 |
| `calcNextOscSampleBlock` entry × 2 | 34 |
| `osc_slpFmBlock` entry + `osc_renderTypedFm` (≈ 12, P3 figure) | 83 |
| `osc_typedPositionApply` (73; `__aeabi_ldivmod` not counted, as in P3) + restore (≈ 11, P3) | 84 |
| `transient_calcBlock` (≈ 21, P3 figure) | 21 |
| **Per block** | **≈ 1,006 → 31.4 per sample** |

| | Per sample |
|---|---:|
| Loops + per block + SRR (`mixer_decimateBlock`, 10; Cymbal/HiHat use it) | **≈ 292.9, 3 VDIV** |
| Without crash (modulators 27 + 27) | ≈ 286.9 |
| The WFd chain instead of T+D (fold ≤ 22 + gain/distortion 21, against 22 + 25) | 4 lower |

#### 4.7.2 The headroom for the Alkaline

Two measures. Instructions, as the gates count them. A cycle proxy (S093 P0
§6: a VDIV ≈ 14 cycles, everything else 1), because the Alkaline already has
3 + 9/32 VDIV against the ceiling's 3.

| | Instructions | Cycle proxy |
|---|---:|---:|
| Advanced ceiling (with crash) | 292.9 | 331.9 |
| Alkaline today, no slot | 256.5 | 299.2 |
| Alkaline today, slot held | 271.5 | 314.2 |
| **Headroom, no slot** | **36.4** | **32.7** |
| **Headroom, slot held** | **21.4** | **17.7** |

The accepted over-budget (D13 of the reassessment) disappears: the
slot-held Alkaline is 21.4 under the Advanced ceiling.

#### 4.7.3 What distortion fits

| Option | Per sample | No slot (instr. / proxy) | Slot held (instr. / proxy) | Fits? |
|---|---|---:|---:|---|
| **D-a** The engines' `dist` (same curve and `dist` row as the other instruments), a separate gain + distortion pass like `voicePost_gainDist()` | 21, 1 VDIV (measured on Cymbal/HiHat) | 277.5 / 333.2 | 292.5 / 348.2 | On instructions, yes (0.4 to spare when a slot is held). On the proxy, over by 1.3 (no slot) and 16.3 (slot). With A2–A4 (≈ −3, §4.4) the no-slot case fits both |
| **D-b** Division-free shaper, separate pass | ≈ 12–15 (estimate) | ≈ 269–272 / ≈ 311–314 | ≈ 284–287 / ≈ 326–329 | **Yes, both measures**, slot included |
| **D-c** Division-free shaper, fused into the ladder output | ≈ 5–8 (estimate, if no spill) | ≈ 262–265 | ≈ 277–280 | Yes, with ≈ 13–16 left for something else (for example a tone stage before or after the shaper) |

- **Without crash** the Advanced ceiling is 286.9, and D-a with a slot
  (292.5) no longer fits on instructions. D-b and D-c still do.
- **Recommended under Advanced:** D-a if matching the other instruments'
  `dist` matters more than the slot case's proxy margin; otherwise D-b. Both
  need the measured spike of §4.5. No control reductions are required for
  D-b or D-c.

#### 4.7.4 What Advanced costs

- **At most two Advanced instruments per Scene**, shared with Cymbal and
  HiHat (`instrumentManager_typeSelectableForSceneSlot()`,
  `InstrumentManager.c` l. 443–467). So at most two Alkalines per Scene, and
  none when two Cymbal/HiHat slots are already used.
- **The brief was Basic.** P7's move to Advanced was not approved; this
  section only prices the option.
- **A type is Basic or Advanced, never both** (the same function, l. 460–461).
  A middle way is two registry entries sharing the engine:
  - the Basic Alkaline, without distortion (today);
  - an Advanced variant with distortion.
  
  That costs a type id, a file kind, menu and registry rows, and an Instrument
  Load rule. It is not measured or scheduled.
- **The worst-case Scene:** an Alkaline in an Advanced place replaces a
  Cymbal/HiHat at most, so the Scene's worst case does not rise as long as
  the Alkaline stays at or below 292.9 (and the cycle proxy at or below
  331.9).

### 4.8 Beyond distortion, as Advanced: wavefolder, phaser, flanger

#### 4.8.1 The binding headroom

None of the stages below divides, so each adds the same to the instruction
count and to the cycle proxy. The proxy is the tighter measure, because the
Alkaline already carries 9/32 VDIV above the ceiling's 3 (≈ 3.7 cycles):

| | No slot | Slot held |
|---|---:|---:|
| Headroom, cycle proxy (binding) | **32.7** | **17.7** |
| Headroom, instructions | 36.4 | 21.4 |

#### 4.8.2 What each stage costs

All run as a separate pass over the voice's output block, after the ladder
(post-filter), unless marked otherwise.

| Stage | Per-sample loop | Per block | Total per sample | Source |
|---|---:|---:|---:|---|
| **Wavefolder** (the S089 `voiceFold` loops, reused): Tri/TrD/TrI 12, Chb 14, Fd2 20, Sin/SnD 22, Asn 22 | 12–22 | 100 (`voiceFold_process()`) | **15.1–25.1** | measured, P8 image |
| **Phaser, 4 stages** (first-order allpasses, feedback, 50/50 mix, coefficient ramped per sample) | 29 | ≈ 27 + sweep control ≈ 60–100 | **≈ 32** | loop measured (standalone spike); sweep control estimated |
| Phaser, 6 stages | 37 | as above | ≈ 40 | as above |
| Phaser, 8 stages | 45 | as above | ≈ 48 | as above |
| **Flanger** (256-point int16 line, fractional read, feedback) | 35 | ≈ 13 + sweep control ≈ 60–100 | **≈ 38** | as above |
| Division-free distortion (§4.7.3 D-b) | ≈ 12–15 | small | ≈ 12–15 | estimate |
| **Wavefolder before the ladder** (the oscillator folded, then filtered; the oscillator is unfused from the ladder: table read ≈ 24 + fold 12–22 + the plain `renderB2` loop 198, against the fused 213) | ≈ 234–244 in place of 213 | + an oscillator entry ≈ 20 | **≈ +22 to +32** | estimate from standalone counts (§12 of the reassessment) |

- **Phaser cost:** four instructions per allpass stage. The loop spike held
  every state in registers (no stack references at 4 stages).
- **Sweep control** per block: the coefficient from the sweep frequency (one
  `tan` approximation and one division per block, 1/32 VDIV per sample), and
  an LFO step or an envelope read.

#### 4.8.3 What fits

**Without other reductions:**

| Stage | No slot (≤ 32.7) | Slot held (≤ 17.7) |
|---|---|---|
| Wavefolder after the ladder, any type | **Yes** | Tri/TrD/TrI and Chb only (15.1, 17.1) |
| Phaser, 4 stages | **Yes, just** (≈ 32; the sweep control must be measured) | No |
| Phaser, 6 or 8 stages | No | No |
| Flanger | No (≈ 38) | No |
| Division-free distortion | Yes | Yes |
| Wavefolder before the ladder | Yes for the cheap types (estimate) | No |
| Two stages together (for example fold + distortion) | Only a cheap fold (Tri/Chb) + distortion, ≈ 27–32 | No |

**With the Alkaline reductions of §4.4** (A1 staggered control + A2–A4,
≈ +11 to +13 of headroom, estimates): the no-slot headroom becomes
≈ 44–46 and the slot-held ≈ 29–31.
- Phaser with 6 stages: yes (no slot).
- Flanger: yes (no slot).
- Phaser with 4 stages, slot held: borderline.
- Any fold + distortion: yes (no slot).

**A rule that removes the slot case:** make the effect and the frame blend
exclusive. While a phaser or flanger is on, the wavetable does not claim a
slot and plays the nearest frame. Only the no-slot column then applies. It
costs one test in `alk_wtbControl()` per block.

#### 4.8.4 RAM

| Stage | State | Where |
|---|---|---|
| Wavefolder | the fold controls and decay envelope `voiceFold_process()` takes (a `TransientGenerator` and a `SnapEg`, which the Alkaline does not have); or a small Alkaline wrapper with the same loops | inside the slot |
| Phaser | ≤ 8 allpass states + coefficient, feedback, last output: ≈ 44 B | inside the slot |
| Flanger, 256 points | 512 B line + write index | inside the slot |
| Chorus (15–30 ms: ≈ 660–1,320 points) | 1.3–2.6 KB | **does not fit the slot** |

- **Each instrument slot reserves 1,264 B** (`INSTRUMENT_RUNTIME_SLOT_BYTES`,
  `InstrumentManager.c` l. 202–212: 2 × `sizeof(DrumVoice)`). The Alkaline
  uses 548 B, so about 700 B are free in every slot at **no RAM cost**.
  - A phaser fits easily.
  - A 256-point flanger line fits: 5.8 ms at 44,108 Hz, enough for flanging
    (≈ 0.1–5 ms).
- **A chorus or a longer delay** would need RAM outside the slot. Growing
  the reserve costs × 6 (every slot). A shared pool of two buffers would do
  instead, since at most two Advanced instruments exist per Scene. FXBUF's
  margin is 3,776 B. Either is a RAM sign-off.

#### 4.8.5 Notes for the choice

- **Sound:**
  - a fold after the ladder folds the resonance peak;
  - a fold before the ladder folds the oscillator, which the 303 filter then
    shapes (the costlier one, the more classic sound);
  - a phaser or flanger after the ladder is an effect on the voice;
  - a phaser swept by the Alkaline's own envelope (MEG or accent) instead of
    an LFO would track each note at no extra cost per sample.
- **The crash interaction:** removing crash (§2) lowers the Advanced ceiling
  by 6, to 286.9. Measured against today's worst case (292.9), the crash
  saving and these stages draw on the same 6. The S089 rule allows a new
  feature paid for by a saving, so the order of the decisions matters.
- **Gates:** each stage is a new loop in the Alkaline render. The mixer gate
  (`elf_p4_check`) has moved twice in P8 for Alkaline render changes, so
  every variant is linked and gated, not only counted standalone.
- **Recommended for the spike (§4.5):**
  - the post-ladder fold (reused loops);
  - the 4-stage phaser with its sweep control;
  - the exclusivity rule;
  - then the flanger and the 6-stage phaser if A1 is taken.

---

## 5. Statements this draft would make out of date (for the user to log, if adopted)

- `INSTRUMENTS_DSP_REFERENCE.md` §8.1 (crash rows), §8.2 (per-voice
  compositions), the S087 note "Per-slot wavetable blend cost: +10..+13"
  (the pool's ceiling is the `itp` fade, §1.2).
- `CPU_USE_DSP_AUDIT.md` item 47 (S087 blend premium) and the worst-case
  Scene text (two interpolated oscillators).
- `OSC_INTERP_AUDIT.md` (Crash in the interpolation and sync notes).
- `FILESYSTEM_SPEC.md`: the stock wave list (`Cym`), the sample folder
  (the new card sample).
- `S093_P0_ALKALINE_C_FUNCTIONS_CODE.md` Cymbal/HiHat estimate (≈ 270 used
  27 + 27 and no per-block count); measured here at ≈ 292.9 (§4.7.1). No
  Advanced ceiling is frozen in `tools/dsp_test/baselines/`; freezing one
  would be a schedule step (a baseline file, not a document edit).
- `MEMORY.md`: the stock wave list and any Alkaline budget figure.

---

## 6. Measurement record (2026-11-10)

All builds are scratch copies of the shipped P8 tree, linked `-Ofast -flto`,
clean-built with `make all`.

| Build | What | Result |
|---|---|---|
| P6 image (`p8base`) and P8 image (`live`) | every oscillator loop, `elf_looppath.py --cond-heads --in-range --top 6` | the per-voice and pool tables of §1 |
| `v8` | revision-3 Alkaline blend (claim in the async) | loop 213 / 228; per block 1,391; `elf_p4_check` 77/77 exact, mixer 32 equal |
| `v9a` | Alkaline blend, one pointer (row B at +1,024 points) | 230 (+2) |
| `v9b` | `v9a` with a constant mix per block | 227 (−1 against `v8`) |
| `v9c` | Drum sync blend, one pointer (frame B at +2,048 B) | 45 (no change) |
| P8 image | crash loop identified by its `ldrb` loads and `(u − 127) × 256`; `crashSample` range | 30; 15..254, guard = point 0 |
| `control_cost.py` (standalone) | Alkaline per-stage counts | §4.3 |
| `liveg` (P8 tree + `-g`) | mixer-inlined Cymbal/HiHat loops by source (`elf_loopsrc` helpers, cyclic cost) | post-chain 25 (1 VDIV), WFd gain/distortion 21 (1 VDIV), click 22 |
| P8 image | `voiceFold_*Loop` and `voiceFold_process()` | loops 12 / 14 / 20 / 22 / 22, per block 100 |
| `fxspike/fx.c` (standalone, `-Ofast`, Cortex-M7 flags of the Makefile) | phaser 4 / 6 / 8 stages, flanger 256 points | loops 29 / 37 / 45 / 35, no VDIV, no stack references at 4 stages; per block 25–27 / 13 (without sweep control) |
| P8 image + standalone `CymbalVoice.o` / `HiHat.o` (`-Ofast`, no LTO) | `elf_blockpath.py`: async 62, `osc_setFreq` 128, SVF entry 161, `osc_slpFmBlock` entry 71, `calcNextOscSampleBlock` entry 17, `osc_typedPositionApply` 73, sync-block entry 133 | §4.7.1 |

The mixer stayed 32 equal in `v9a`, `v9b` and `v9c`.
