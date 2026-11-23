# S101 P3 — A stereo spatializer voice chain for Drum, Snare, Cymbal and HiHat

**Date:** 2026-11-22. **Branch:** `dev-ph7-alkaline` (the S101 tree, with the
S101 P2 working-tree changes). **Status:** **decided and built** (revision
5, 2026-11-22). The build, its measurements and its gates are logged in
`S101_P3_IMPLEMENTATION.md`; the device CPU test is yours (§8 Step 3).

**Revision 5 (your instruction, 2026-11-22):** "write that in as decided
and build the spatializer with the cheaper LP on hi-hat and cymbal […]
*Don't* label the new LP as 'LP1' in cym/hi-hat, keep the label 'LP', same
position as the 'LP' as previous, same in the list as Drum and Snare."
Everything in §7 is now decided (the defaults taken where you had not ruled
are marked there). `LP1` below is the code's name only; on the panel the
type is `LP`.

**Your decisions (2026-11-22), folded in:**
1. **`Spc`** is a third `chn` value on all four engines (Drum, Snare,
   Cymbal, HiHat): a stereo spatializer in the Tetrakt's S1 law. It
   replaces the transient, the post-chain drive and the SRR. It takes five
   cells: three on the click sub-page and the `srt`/`drv` cells of the mix
   page.
2. **Cymbal and HiHat get a cheaper low-pass in place of the ZDF LP,
   always** (every chain, no fallback). Drum and Snare keep today's filter.
   **It is labelled `LP`, in the same list position as on Drum and Snare**
   (revision 5); `LP1` is only its name in the code (`voiceSpace_lp1()`).
3. **The budget rule:** a Basic slot's worst case stays under the
   Tetrakt's validated worst case, by a margin.
4. **The parameter layout** of revision 3a stands.
5. **Under `Spc`, the two mix-page cells read `spt`** (spatializer tone, the
   `srt` cell) **and `spc`** (spatializer level, the `drv` cell), never
   empty.
6. **A cheaper Peak for Cymbal** was to be tried, so that the Tetrakt's law
   (two interpolated taps) fits. **The result is in §5.3: it does not get
   there on its own.**

**Earlier revisions:**
- 1: stereo, over budget; withdrawn.
- 2: mono only.
- 3: the whole filter replaced under `Spc`.
- 3a: per-type filter costs, and LP-only fallbacks.

Their measurements are kept below where they still apply.

**Labels used below:**
- **Measured (image):** today's linked `build/lxr02.elf`, with
  `tools/dsp_test/elf_loopdump.py`, `elf_blockpath.py` or `tetrakt_cpu.py`.
- **Measured (prototype):** scratch C compiled with the project's DSP flags
  (`-mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -Ofast`; `-g`
  where loops had to be named) and counted the same way (Appendix A).
  Nothing was added to the tree.
- **Device:** the S100/S101 probe traces and `S101_P2_IMPLEMENTATION.md` §9.

---

## 0. Summary

1. **The ceiling** is the Tetrakt as built, **18.6k** cycles a block
   (`S101_P2_IMPLEMENTATION.md` §7.2: Tetrakt 18.6k, Colloid 19.0k).
   - It was validated on the device: 4 Tetrakts + 2 Advanced Colloids ran at
     143.3k, under the 144k cap (§9.1).
   - It sits 1.0k under the 19,620 Basic share.
2. **LP1 on Cymbal/HiHat lowers their worst case in every chain** by
   **−0.72k** (harsh) / −0.85k (expected).
   - Their dearest filter type becomes Peak (92 instructions, 2 VDIV) instead
     of LP (101, 3 VDIV).
   - LP1 is 29 + 3 a sample with no division, so it never sets the worst
     case.
3. **`Spc` with the Tetrakt's law (form E)**, estimated dearest against the
   18.6k ceiling:

   | Engine | Today's dearest (T+D, estimate) | With LP1 / `Spc` E, harsh | Expected |
   |---|---:|---:|---:|
   | Snare | 13.2–13.9k | 14.4–15.1k | 13.2–13.9k |
   | Drum | 16.0–17.0k | 16.9–17.9k | 15.8–16.8k |
   | **Cymbal / HiHat** | 17.9–18.7k | **18.1–18.9k** | **16.9–17.7k** |

   - Drum and Snare fit with room.
   - Cymbal/HiHat fit at the expected rate, but the top of the harsh range
     is **0.3k over** the ceiling. It is still 0.7k under the share.
4. **A cheaper Peak does not close that 0.3k** (§5.3, measured).
   - Peak is only 3 instructions over Notch and 4 over HP, and every type
     shares the same solver (≈ 85 instructions and 2 VDIV).
   - **Same-sound rewrite** of Peak's output arithmetic: **−1 instruction**
     (≈ −0.04k).
   - **TPT Peak:** Notch becomes the worst type: −3 (≈ −0.13k).
   - **Peak, Notch and HP all TPT:** −5 (≈ −0.22k).
   - **Only replacing every Cymbal/HiHat type** with its TPT form changes the
     hot case by a lot (−2.7k), and that changes every Cymbal/HiHat filter
     sound.
5. **What decides it is the measurement we do not have.**
   - The Cymbal/HiHat range is an estimate from a HiHat measured at a
     cheaper setting (16.0k).
   - **Step 0** (§8) measures their dearest T+D on today's image.
   - **The Tetrakt law fits if that figure is ≤ 18.43k** (today's full LP),
     i.e. ≤ 17.71k once LP1 is in.
6. **If it does not,** in order of least change (§5.4):
   1. **form H on all four:** the time tap interpolated, the spread tap
      whole-sample. Harsh 17.8–18.6k on Cymbal/HiHat.
   2. TPT Peak (+ Notch, HP) on Cymbal/HiHat.
   3. TPT for every Cymbal/HiHat type.

---

## 1. The rules this proposal is held to

| Rule | Source | How it applies |
|---|---|---|
| **Basic worst case under the Tetrakt's validated worst case, by a margin** | you, 2026-11-22 | Ceiling **18.6k** (the Tetrakt as built). Each engine's dearest setting in any chain, priced the harshest way (§3), is checked against it; the device gate is the same (§8). |
| The Basic share, 19,620 cycles a block | S094 §21 | Hard limit; the ceiling is 1.0k under it. |
| A chain's worst case is the engine's worst case | S089 P1 | The engine's dearest = max over T+D, WFd, `Spc`. |
| Constant CPU | `CPU_USE_DSP_AUDIT.md` §Binding Mandates | Every stage runs every block, every type and setting at a fixed cost. |
| Per-block work counts | S093 (binding) | Loops + per-block ÷ 32; VDIV separately. |
| One 50 ms buffer | brief | One FxBuffer voice unit (2,208 int16 = 50.06 ms). |
| T+D and WFd unchanged except LP1 on Cymbal/HiHat | your decision 2 | Gates in §8. |
| RAM sign-off | `MEMORY.md` | §6.8. |
| No backward compatibility | standing | No migration; a Cymbal/HiHat sound stored with LP plays LP1. |

---

## 2. What exists

### 2.1 The Tetrakt spatializer (S1 + W2)

**Code:** `tkt_spaceControl()` (`TetraktVoice.c:388`), `tkt_spaceOut()`
(`TetraktVoice.c:796`).

**Per sample:**
- two interpolated taps (`tapR = tapL × (1 − 0.5 sdt)`);
- their mean through the one-pole `tktFbLpK[sto]`;
- the ring takes `sat16(send + 0.97 × sfb/127 × lp)`;
- `l/r = bus + tap × slv`;
- W2 on `r`, and DC blockers on both sides.

**Measured (image):** frame 64 (`tetrakt_cpu.py`); the output frame (W2, DC
blockers, stores) is ≈ 33 of it.

### 2.2 The four engines today

| Engine | Pre-filter | Filter | Post-chain (linked loop) | After the voice |
|---|---|---|---|---|
| Drum | osc (+ itp) → T+D: transient + add / WFd: fold (`DrumVoice.c:915`) | ZDF | `voicePost_drum()` 25, 1 VDIV | mixer SRR (`mixer.c:1555`) |
| Snare | noise → ZDF → T+D: + transient (`Snare.c:360`); tone osc (WFd: fold) | ZDF (noise) | `voicePost_mixAddGainDist()` 30, 1 VDIV | mixer SRR |
| Cymbal / HiHat | modulators → carrier (WFd: fold, `HiHat.c:480`) | ZDF | T+D `voicePost_addGainDist()` 25 / WFd `voicePost_gainDist()` 21, 1 VDIV | mixer SRR |

**Measured costs:**

| Stage | Instructions a sample | Source |
|---|---:|---|
| Transient (`transient_calcUserBlock`) | 20 | image |
| Post-chain drive (`distortion_curveSampleF()`) | 7 + 1 VDIV | prototype: the post-chains compile standalone to the image's 25 / 21 / 30, and without the drive to 18 / 14 / 23 |
| SRR (`mixer_decimateBlock()`) | 10 | image |
| Drum/Snare transient mix-in | counted as 0 | inlined; not attributable |
| Cymbal/HiHat mix-in | 4 | image (25 − 21) |

**The engines' relative cost:**

| Engine | Model dearest a sample (S093 P8, less the S094/S101 removals) | Device at Scene settings |
|---|---:|---:|
| Cymbal / HiHat | ≈ 285 | 14.5k / 15.5k |
| Drum | ≈ 258 | 14.6k |
| Snare | ≈ 190 | 11.7k |

### 2.3 The ZDF filter, per type (measured)

The loops were named by building `ResonantFilter.c` standalone with `-g`
and the DSP flags; the standalone counts sit 0–2 below the image's.

| Type | Image: instructions a sample | VDIV | Notes |
|---|---:|---:|---|
| **LP** | **101** | **3** | the solver (87 to the range test) + `fastTanh()`'s division block **outside the loop body** (10) + the store (4); two taken branches. The loop tool shows only its 91-instruction clamped path. |
| **Peak** | **92** | 2 | |
| Notch | 89 | 2 | |
| HP | 88 | 2 | |
| BP, UBP | 87 | 2 | |
| LP2 | 37–51 | 1 | its own naive two-pole path |
| off | — | — | leaves after one sample |

**Every full type runs the same Padé solver** (`ResonantFilter.c:361`–`392`):
three tanh pairs and two reciprocals, ≈ 85 instructions and 2 VDIV. The
types differ only in their few output instructions, and LP in its output
saturator.

### 2.4 The per-block oscillator work (measured, image; for §5.4)

| Function | Longest path | Calls a block on Cymbal/HiHat |
|---|---:|---:|
| `osc_setFreq()` | 134 | 3 |
| `osc_userBind()` (the `slp`/`smp`/`wtb` path, reached through `osc_setFreq()`'s ≈ 30-instruction prefix) | 103 | 3 |

The rebind path (catalogue lookup, loop modulo) can run on any block that
changes a selection. It is a real worst-case item. Its saving is a lead,
not a proposal; it needs a prototype first.

### 2.5 The advanced chorus: not a fit

+2.3–2.7k a block (device; −0.4k at half rate); no feedback path.

---

## 3. The pricing (the harshest the evidence supports)

1. **Only compiled loops count.**
2. **Everything added:**
   - **1.9 cycles an instruction** (the HiHat's effective device ratio);
   - **+3 instructions on every new loop** (the calibration copy of the
     Tetrakt frame compiles to 62; the image has 64);
   - the whole per-block body (prototype static count), + 10 a call;
   - the mixer's stereo path, **+7 a sample** (measured, image: dry 26 → 31,
     send to a stereo-input Effect 40 → 47).
3. **Everything removed:**
   - **1.4 cycles an instruction**;
   - each VDIV at **10 cycles**;
   - anything not attributable in the image at **0**.
4. **No credit for overlap:** the amp ramp moves into the new loop (charged
   at 1.9) and the old post-chain is credited at 1.4.
5. **Expected:** 1.5 cycles both ways, VDIV 13. Shown for reference;
   decisions use the harsh figure.

---

## 4. The forms (measured, prototype)

### 4.1 The ring: mirrored, one unit, no segments

- **Layout:** one voice unit = **2,176 ring** (68 × 32) **+ 32 mirror** =
  2,208.
- **The write** advances 32 a block, so it wraps only at a block boundary.
- **A read block** `[start − 1, start + 31]`, with `start` in `[1, 2,176]`,
  never leaves the unit.
- **The mirror** (`ring[2,176..] = ring[0..31]`) is a 64 B `ldm`/`stm` copy
  after the one block in 68 that writes `ring[0..31]`.
- **The condition:** both taps ≥ 32 samples (the time floor is the
  Tetrakt's 1 ms, 44 samples; the spread tap is clamped at 32).
- **Range:** 1–49.3 ms.
- **What it saves:** the Tetrakt's segment logic (57 × 4 a block) is gone.

### 4.2 The space stage

| Form | Taps | Loop | Function (static) | Charged a sample: loop + 3 + (static − loop + 10)/32 + 7 mixer |
|---|---|---:|---:|---:|
| **E** (Drum, Cymbal, HiHat) | both interpolated (the Tetrakt's law) | **46** | 176 | **60.4** |
| **E**, Snare (noise × mix + tone, then the ramp) | both interpolated | **54** | 191 | **68.6** |
| **H** | time interpolated, spread whole-sample | **41** (Snare 49) | 170 (183) | 55.3 (63.5) |
| B | both whole-sample | 36 (44) | 149 (164) | 50.3 (58.1) |

**What the stage does:**
- the amp × velocity ramp is fused in;
- `smlawb`/`smlawt` on Q15 coefficients packed in halfword pairs, `smull`
  for the ramp;
- no float and no VDIV in the loop;
- a stereo form below these could not be found: the half-rate and packed
  forms spill (75 and 69 per sample pair).

### 4.3 LP1 (measured, prototype)

- **The form:** the TPT state-variable filter (`ic1`, `ic2`), LP output,
  with the Tetrakt/Colloid clamped cubic as its input drive on `sft`.
- **Loop 29, function 56, no division** (21 without the drive).
- **Per block:** `a1 = 1/(1 + g(g + k))`, one division, from the slot's
  existing `filter.f` (g) and `filter.q` (k = 2R, with a floor so the loop
  never reaches zero damping).

---

## 5. The costs (thousands of cycles a block, harsh / expected)

### 5.1 `Spc` against the same engine's T+D (filter unchanged)

- **Removed:** transient 20 + post-chain + SRR 10, at 1.4, + the post-chain
  VDIV:
  - Drum, Cymbal, HiHat (post-chain 25): **2.78k**;
  - Snare (post-chain 30): **3.01k**.
- **Added:** the §4.2 stage at 1.9 × 32.

| Form | Drum, Cymbal, HiHat | Snare |
|---|---:|---:|
| **E** | **+0.89 / −0.16** | **+1.16 / −0.00** |
| H | +0.58 / −0.40 | +0.85 / −0.25 |
| B | +0.28 / −0.64 | +0.52 / −0.50 |

### 5.2 LP1 on Cymbal/HiHat, every chain

**LP (101 + 3 VDIV) → Peak sets the worst (92 + 2 VDIV): −0.72 / −0.85.**

**The engines' dearest settings with LP1 and `Spc` E:**

| Engine | T+D | `Spc` E | Engine worst (the larger) | Against 18.6k |
|---|---|---|---|---|
| Snare | 13.2–13.9 | 14.4–15.1 / 13.2–13.9 | `Spc` | 3.5k+ under |
| Drum | 16.0–17.0 | 16.9–17.9 / 15.8–16.8 | `Spc` | 0.7k+ under |
| Cymbal / HiHat | 17.2–18.0 / 17.1–17.9 | **18.1–18.9** / 16.9–17.7 | `Spc` (harsh), T+D (expected) | harsh top **0.3k over**; expected 0.9k+ under |

### 5.3 A cheaper Peak for Cymbal/HiHat (measured)

| Change on Cymbal/HiHat | Worst type after it | Instructions saved against Peak | Harsh | Sound |
|---|---|---:|---:|---|
| Peak's output rewritten (`y1 − h` = `2y1 + 2R y0 − x`, gain folded in): standalone 90 → 89 (Notch 87 → 86 with the same rewrite) | Peak ≈ 91 | 1 | −0.04 | identical to rounding |
| **Peak → a TPT peak** (LP1's machinery, peak output) | Notch 89 (88 rewritten) | 3–4 | **−0.13 to −0.18** | Peak loses the ZDF character |
| Peak, Notch and HP → TPT | BP / UBP 87 | 5 | −0.22 | three types change |
| **Every type → TPT** | the TPT ≈ 32 + per block | — | **−2.68** | every Cymbal/HiHat filter changes |

**Why a cheaper Peak cannot do it alone:**
- the solver is ≈ 85 of every type's 87–92;
- Peak's lead over the next type is 3 instructions;
- the next types (Notch, HP, BP, UBP) are 1–2 apart.

**What the Tetrakt law needs on Cymbal/HiHat at the harsh top:** ≈ 0.3k.
Only the every-type swap reaches it unconditionally.

### 5.4 Choosing the stereo form: the Step 0 rule

`Spc` E on Cymbal/HiHat (harsh) = today's measured T+D − 0.72 + 0.89.

| Step 0 measures Cymbal/HiHat's dearest T+D (today's image, full LP) at | Then |
|---|---|
| **≤ 18.43k** | **E (the Tetrakt's law) on all four.** Recommended outcome. |
| 18.43–18.56k | E, with Peak → TPT on Cymbal/HiHat (−0.13k) |
| 18.56–18.65k | E, with Peak, Notch and HP → TPT on Cymbal/HiHat (−0.22k) |
| 18.43–18.74k, if you prefer no more filter changes | **H on all four** (time stays interpolated; only the spread tap steps) |
| above that | E with every Cymbal/HiHat type → TPT, or B |

**Drum and Snare** take whichever form Cymbal/HiHat take, so `Spc` reads
the same everywhere. Every form fits them.

---

## 6. The design

### 6.1 Signal flow (`chn` = 2, `Spc`)

| Engine | Chain under `Spc` |
|---|---|
| Drum | osc (+ itp) → ZDF (today's types; `sft` as today) → **space** (amp × velocity ramp, two taps) → L/R |
| Snare | noise → ZDF; tone osc (+ itp) → **space** (noise × mix + tone, amp ramp, two taps) |
| Cymbal / HiHat | modulators → carrier (+ itp) → filter (**LP1** for LP, ZDF for the others) → **space** (amp ramp, two taps) |

**Every chain branch tests the chain value explicitly:**
- today `Snare.c:360`, `CymbalVoice.c:463` and `HiHat.c:487` test
  `!= VOICE_CHAIN_WAVEFOLDER`, and `DrumVoice.c:915` uses an `else`;
- under a third value all four would run the transient, so they become
  `== VOICE_CHAIN_DEFAULT`;
- the `Snp`/`Ofs` guards (`DrumVoice.c:316`, `:332`, `:485`) are already
  correct.

**Under `Spc`:**
- **No transient, no fold, no post-chain** (its drive goes).
- **`instrumentManager_slotUsesSrr()`** (`InstrumentManager.c:2964`)
  returns 0; its comment already reserves this test.

**The code:**
- **`voiceSpace.c`** (new, `-Ofast -fno-lto`, like the Tetrakt):
  - `voiceSpace_process()`, two forms (one input; Snare);
  - `voiceSpace_lp1()`.

  All `noinline`, so no new loop is inlined into
  `mixer_calcNextSampleBlock()` (S089 P0 F3).
- **`SVF_calcBlockZDF()` is unchanged.** Cymbal/HiHat call
  `voiceSpace_lp1()` when their type is LP, and `SVF_calcBlockZDF()`
  otherwise: one per-block branch. The `filter_type` writer stores which
  one, never the render.

### 6.2 LP1 (Cymbal, HiHat; every chain)

**Per block:**
- `g = filter.f`;
- `k = max(2R, k_min)`;
- `a1 = 1/(1 + g(g + k))`, `a2 = g a1`, `a3 = g a2`;
- `d` from `filter.drive` (`sft`).

**Per sample:**

```
u  = clamp(in × d, ±1.5);  x = (u − 4/27 u³) × FS
v3 = x − ic2;  v1 = a1 ic1 + a2 v3;  v2 = ic2 + a2 ic1 + a3 v3
ic1 = 2 v1 − ic1;  ic2 = 2 v2 − ic2
out = sat16(GAIN_LP1 × v2)
```

**Notes on LP1:**
- **State:** `ic1`/`ic2` reuse the voice's `filter.s1`/`s2`, cleared when
  the type changes to or from LP.
- **The slews are unchanged:** `fre`/`itp` (`SVF_recalcFreqSlew()`) and the
  filter LFO/velocity targets.
- **The level** is matched to the ZDF LP at drive 0 and low resonance (host
  law LP1-RESP).
- **The sound:** a clean resonance where the ZDF LP saturates. **It has
  headroom for character at no cost to the worst case:** its budget is
  Peak's path (92 + 2 VDIV) and it uses 29 + 3. A cubic on the band state,
  for a saturating resonance, is ≈ 6. This is the one listening decision
  in LP1 (Q2).
- **Display (revision 5, decided):** the type list on Cymbal/HiHat is
  unchanged: it reads `LP`, in the same position as on Drum and Snare
  (`filterTypes`, `MenuText.h:448`). No name hook.

### 6.3 The space law (the Tetrakt S1, integer)

**Per block:**
- `tL = time(frq)` in samples, clamped to `[44, 2,175]` (exponential law,
  Q3);
- `tR = max(tL × (1 − 0.5 × spread/127), 32)`;
- `fL`, `fR` (Q15, packed; form H: `fR` unused);
- `fb` = (`vol` − 63)/64 × 0.97 (signed);
- `fbK = tktFbLpK[tone]` (reused);
- `slv` = level/127;
- the ramp `g0`, `g1` in Q30, step `(g1 − g0)/31` by multiply.

**Per sample:**

```
x  = in × g;  g += dg                    (Snare: in = sat16(noise × mix + tone))
tl = aL + fL (bL − aL);  tr = aR + fR (bR − aR)      (H: tr = aR)
lp += fbK ((tl + tr)/2 − lp)
ring[w] = sat16(x + fb lp)
L = sat16(x + slv tl);  R = sat16(x + slv tr)
```

**Notes on the space:**
- **Dry is at unity.** Level 0 gives exactly the ramped dry signal in both
  channels, and the loop still runs.
- **Mono sums keep the comb.**
- **DC:** with positive feedback the line's DC gain reaches ≈ 33 (the write
  saturates), as on the Tetrakt; negative feedback cancels it.
- **The taps move per block,** as on the Tetrakt.
- **The ramp:** the Drum's amp × velocity product is ramped linearly
  between the exact endpoints; on an armed block only, this differs slightly
  from today's two-ramp product.

### 6.4 Controls

| Page / cell | Stored row (key unchanged) | Short label under `Spc` | Long label | Law | Morph | LFO / velocity | Automation |
|---|---|---|---|---|---|---|---|
| Click (5th sub-page), cell 1 | `transient_wave` | `spr` | Spread | 0..127 → right tap up to 50 % earlier | yes | no | yes |
| Click, cell 2 | `transient_vol` | `fbk` | Feedback | −0.97..+0.97, stored 63 = 0 | yes | yes | yes |
| Click, cell 3 | `transient_freq` | `tim` | Time | 1–49.3 ms, exponential | yes | yes | yes |
| Click, cell 4 | `voice_chain` | `chn` | Chain | `T+D` / `WFd` / `Spc` | — | — | **no** (Q4) |
| Mix, cell 3 | `instrument_decimation` | **`spt`** | Spatializer tone | the Tetrakt `sto` law, 127 = open | yes | yes | yes |
| Mix, cell 4 | `instrument_drive` | **`spc`** | Spatializer level | 0..127 wet, dry at unity | yes | yes | yes |

**Behaviour of the reused cells:**
- **Under T+D and WFd** every cell reads and does what it does today.
- **Stored bytes are never rewritten,** so switching chains restores them.
- **Switching an existing sound to `Spc`:** `drv` usually 0 → level 0, and
  `srt` usually 127 → tone open, so nothing changes until the level is
  raised.

**The writers:**
- the rows keep their current stores;
- they also store the byte in the voice's space state;
- they are `IM_SPECIAL_TRANSIENT_FREQ` (`InstrumentManager.c:4981`),
  `IM_SPECIAL_INSTRUMENT_DRIVE`, the `INSTRUMENT_BIND_SLOT_DECIMATION` bind
  (`:5491`), and the generic store.

**Menu:**
- **Per-chain short and long labels.** The existing hook
  (`menu_instrumentCellLabels()`, `menu.c:12761`) changes only long names
  today, so the short-name display needs the same chain test.
- `voiceChainNames` gains `Spc` (`VOICE_CHAIN_COUNT` 2 → 3, asserted in
  `menu.c`).
- `spr` clamps to 127 as a number (beside `menu_cellIsFoldWave()`).
- `fbk` shows the one bipolar law.

### 6.5 The unit and the stereo block

**The unit** (precedent: the Advanced `fx` row, `instrumentManager_chorusBind()`,
`InstrumentManager.c:4767`):
- **When it is held:** one voice unit per slot while `chn` = `Spc`.
- **Type assignment:** `instrumentManager_unitsWanted()` (`:543`) returns 1
  for DRM/SNR/CYM/HAT when the incoming `voice_chain` is 2.
- **The `chn` writer** (`:5160`) binds or releases the unit in the
  foreground and clears the ring.
- **Units in use:** at most six (the six-Tetrakt ceiling).
- **The cost to the Effect:** the unit comes out of the Effect's share (the
  Delay/Echo maximum time follows it, `space_timeLimit()`,
  `SpaceEffect.c:1174`). A change during play reseats the bus Effect, the
  Tetrakt path.
- **`chn` becomes a fixed selector** (Q4), as `fx` did (S099 P1 Q-8).

**The stereo block:**
- `right[32]` in the voice;
- `instrumentManager_slotRightUpdate()` (`:595`) returns it while the unit
  is held (the Alkaline's `choUnit ? right : 0`);
- the mixer's existing stereo path takes it (`mixer.c:1580`–`1605`). **No
  mixer change.**

### 6.6 Constant CPU

- Filter, control, space loop and mirror copy run every block at every type
  and setting.
- The copy runs in one block in 68 and is counted in every block.
- LP1 and the ZDF types are each a fixed-cost loop; the type is a per-block
  choice.

### 6.7 Storage

- No new keys; `voice_chain` gains 2.
- An older image clamps 2 to WFd.
- A Cymbal/HiHat sound stored with LP plays LP1 (no migration).

### 6.8 RAM, flash, ITCM

**RAM (sign-off requested):**

| Item | Bytes | Region | Owner | Lifetime |
|---|---:|---|---|---|
| Per drum-family voice: `right[32]` 64, line pointer 4, `wi` 2, five row bytes, `lp` 4, the LP1 flag 1, padding | **≈ 82** | SRAM1 `.bss`, inside `runtime_slots[]` | the engine's voice struct | the slot's runtime |
| The 50 ms line | 4,416 per `Spc` slot | DTCM, one existing FxBuffer voice unit | the slot (FxBuffer claim) | while `chn` = `Spc` |
| **New allocation** | **0**, with the reserve pinned | | | |

**The slot reserve:**
- it is `2 × sizeof(DrumVoice)` (`InstrumentManager.c:286`–`287`);
- the sizes measured with the ARM compiler today:

  | Struct | Bytes |
  |---|---:|
  | DrumVoice | 632 |
  | SnareVoice | 452 |
  | CymbalVoice | 508 |
  | HiHatVoice | 520 |

- unpinned, ≈ 82 B on DrumVoice would cost +984 B SRAM1;
- **pinned at today's 1,264 B,** the union stays 7,584 B.

**Flash:** an estimated **+2.5–3.5 KB**:

| Part | Estimate |
|---|---:|
| Space forms and control | ≈ 1.0 KB |
| LP1 | ≈ 0.3 KB |
| InstrumentManager | ≈ 0.4 KB |
| Menu (labels) | ≈ 0.7 KB |
| The time table | 0.5 KB |

**ITCM:** 0 B.

---

## 7. Decisions

All decided (revision 5). "Default" marks a point you had not ruled on,
taken as recommended with the build instruction; each is one line to change.

| Q | Question | Decision |
|---|---|---|
| Q1 | `Spc` on all four engines; five cells as §6.4 | **decided** |
| — | The cheaper LP (TPT) replaces the ZDF LP on Cymbal/HiHat in every chain | **decided**; **labelled `LP`**, same position as Drum/Snare (revision 5) |
| — | Mix-page labels `spt` (tone) and `spc` (level) under `Spc` | **decided** |
| — | Click-page short labels `spr` / `fbk` / `tim` | **decided** (revision 5: "write that in as decided") |
| Q2 | LP1's resonance: clean, or a cubic on the band state | **clean** (default). The input and output cubics of the ZDF LP are kept (§6.2); the saturating band state is not built |
| Q3 | Time law exponential (a 512 B table, 0 instructions) | **exponential** (default) |
| Q4 | `chn` loses AUTOMATABLE on all four engines | **yes** (default; the unit claim must be foreground) |
| Q5 | The stereo form | **E, the Tetrakt's law** (your "we can use the Tetrakt law option"). Form H is one compile switch (`-DVSPACE_SPREAD_INTERP=0`), measured and tested, for the device result (§5.4) |
| Q6 | RAM: ≈ 80 B per drum-family voice inside a reserve pinned at 1,264 B (0 B new) | **approved** with the build instruction |

## 8. Plan and proof

**Step 0 — measure today's image (no code; device).** This decides Q5.
1. **The test Scene:** one slot each of Drum, Snare, Cymbal and HiHat at the
   dearest T+D settings:
   - Cymbal/HiHat: carrier `slp` FM, both modulators `slp`;
   - Drum: `fm` with `slp` on both oscillators;
   - Snare: tone `slp`;
   - all four: a sample transient, `srt` < 127, **filter type LP** (the
     dearest, §2.3);
   - the Delay on, sends > 0, the compressor on.
2. **The probe:** S1 with `MIXER_PROBE_MAX` on: each slot's window median
   and maximum, and the SRR and send-add sections.
3. **The record:** stored under `tools/dsp_test/baselines/s101p3/`.

**Step 1 — host laws** (`tools/dsp_test/modules/`).

| Law | What it checks |
|---|---|
| SP-RING | the mirrored ring equals a plain modulo ring bit for bit, every tap pair, all 68 block phases |
| SP-TAP | delay and fraction accuracy |
| SP-FB | bounded decay at ±0.97 for every tone; polarity |
| SP-DC | DC in → bounded (positive), ≤ 1× (negative) |
| SP-DRY | level 0 → L = R = the ramped input exactly |
| SP-GAIN | the ramp hits both endpoints; flat when equal; every engine's endpoints fit Q30 |
| SP-MIN | taps ≥ 32 / 44; no read of an unrefreshed mirror |
| LP1-RESP | the TPT LP against the analytic response; level against the ZDF LP at drive 0, low resonance |
| LP1-STAB | bounded at the resonance floor, full-scale input, full drive |
| CH-MAP | every cell reads and writes its T+D/WFd meaning under those chains and its `Spc` meaning under `Spc`; no byte rewritten |

**Break builds:** mirror copy removed; copy before the loop; tap floor
removed; LP1 resonance floor removed. Each must fail its law.

**Step 2 — build** (§6). The gates:
- `elf_p4_check` and `elf_s099_check` exact;
- mixer hot loops equal, with the inlined-set diff (T+D and WFd on Drum and
  Snare, and every non-LP type, unchanged);
- **the linked loops within the table's allowance:**

  | Loop | Gate |
  |---|---:|
  | LP1 | ≤ 32 |
  | Space E (Drum, Cymbal, HiHat) | ≤ 49 |
  | Space E (Snare) | ≤ 57 |
  | Space H (if chosen) | ≤ 44 / 52 |
  | Per block | ≤ the counted bodies |

- `link_budget.py`.

**Above any gate, stop and re-plan.**

**Step 3 — device.** The Step 0 Scene again, with `chn` = `Spc` on all four,
and again with T+D. **The gate:**
- every engine's dearest slot + SRR + send-add sections ≤ **18.6k**
  (median) in every chain;
- its highest window ≤ 19,620;
- Cymbal/HiHat T+D below their Step 0 figure by ≥ 0.6k (LP1).

---

## 9. Risks

| Risk | Mitigation |
|---|---|
| The loops grow in the image | +3 allowance; the Step 2 gates |
| The call sites in the inlined engine renders move mixer register allocation (S086 P3, S093 P8 item 79) | `noinline` + `-fno-lto`; mixer hot-loop and inlined-set gates |
| LP1 sounds too clean on Cymbal/HiHat | Q2 (a saturating band state, free in the worst case) |
| Cymbal/HiHat over the ceiling with E | the Step 0 rule (§5.4) |
| A chain change during play glitches the Delay | the Tetrakt precedent; foreground only (Q4) |
| DC-heavy samples with positive feedback | saturating write; SP-DC; bipolar feedback |

---

## 10. Statements this makes out of date (for you to act on)

**Already out of date today:**
- `S101_P2_REDUCTIONS.md` §A.2 used Scene-setting figures (HiHat 15.5k, Drum
  14.6k, Cymbal 14.5k) as those types' worst cases. The estimated dearest
  Cymbal/HiHat (17.9–18.7k) is at or above the Tetrakt's 18.6k. Step 0 gives
  the figure.
- `InstrumentManager.c:280`'s comment ("the 1,176 B reserve"). It is 1,264 B.

**If built:**
- `MEMORY.md` "Voice chain" bullet (two chains; SRR "for the four types'
  default chain"; `chn` automatable).
- `INSTRUMENTS_DSP_REFERENCE.md` §5 chain table; the filter section (LP1 on
  Cymbal/HiHat); §6.5–6.6.
- `MIXER_SYSTEM_DSP_REFERENCE.md` §3.2 item 2 (`slotUsesSrr()`).
- `TETRAKT_MODULE_REFERENCE.md` §8 and the `FxBuffer.h` header: the
  voice-unit users.
- `MENU_UI_SPEC.md`: the cell meanings and labels under `Spc`; `chn` not
  automatable.
- The filter descriptions in `INSTRUMENTS_DSP_REFERENCE.md`: Cymbal/HiHat's
  `LP` is now a TPT low-pass (`voiceSpace_lp1()`), not the ZDF.
- `MIDI_NRPN_TABLE.md`: the `voice_chain` range.

---

## Appendix A — the prototypes and how they were counted

**Compile and count:**

```
arm-none-eabi-gcc -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -Ofast [-g] -std=gnu11 -c x.c -o x.o
python3 tools/dsp_test/elf_loopdump.py --elf x.o --sym <function> --out loops.txt
python3 tools/dsp_test/elf_blockpath.py --elf build/lxr02.elf --sym <function>
arm-none-eabi-objdump -d [-l] --no-show-raw-insn x.o      (static counts; line info names the ZDF loops)
```

**The LP path's real cost** comes from following the LP loop's two exits in
`SVF_calcBlockZDF.constprop.0` in the image:
- the solver to the range test, 87;
- the out-of-body `fastTanh()` division block, 10 with 1 VDIV;
- the store tail, 4.

**Space E** (loop 46, 5 stack reloads; function 176):

```c
#define MRING 2176u                          /* 68 x 32; + 32 mirror = one unit */
#define smulwb(a, b) __smlawb((a), (b), 0)
#define smulwt(a, b) __smlawt((a), (b), 0)
#define smmul(a, b)  ((int32_t)(((int64_t)(a) * (b)) >> 32))
do {
	const int32_t x  = smmul((int32_t)*p << 2, g);           /* the amp ramp, Q30 */
	const int32_t al = rl[0], ar = rr[0];
	const int32_t tl = al + smulwb((rl[-1] - al) << 1, fLR); /* fL bottom half */
	const int32_t tr = ar + smulwt((rr[-1] - ar) << 1, fLR); /* H: tr = ar */
	rl++; rr++; g += dg;
	lp += smulwb(((tl + tr) << 14) - lp, fbK) << 1;          /* the tone */
	*ww++ = (int16_t)__ssat(x + (smulwb(lp, fbslv) >> 14), 16);
	*p++  = (int16_t)__ssat(x + smulwt(tl << 1, fbslv), 16);
	*q++  = (int16_t)__ssat(x + smulwt(tr << 1, fbslv), 16);
} while (p != end);
if (wi == 0u) {                                              /* the mirror, ldm/stm */
	typedef struct { uint32_t w[16]; } Blk;
	*(Blk *)(void *)(ring + MRING) = *(const Blk *)(const void *)ring;
}
```

**The Snare form** replaces the first line with
`x = smmul(__ssat(((*p * mq) >> 15) + *tp++, 16) << 2, g)`: loop 54.

**LP1** (loop 29, no division; function 56):

```c
for (uint32_t i = 0; i < 32; i++) {
	float u = (float)buf[i] * d;
	u = fminf(fmaxf(u, -1.5f), 1.5f);
	const float x  = (u - (4.0f / 27.0f) * u * u * u) * 32767.0f;
	const float v3 = x - ic2;
	const float v1 = a1 * ic1 + a2 * v3;
	const float v2 = ic2 + a2 * ic1 + a3 * v3;
	ic1 = 2.0f * v1 - ic1;
	ic2 = 2.0f * v2 - ic2;
	buf[i] = (int16_t)fminf(fmaxf(gain * v2, -32768.0f), 32767.0f);   /* the prototype ran a 3-output mix here */
}
```

**The Peak rewrite** (§5.3) replaced the Peak, Notch and HP outputs in a
copy of `ResonantFilter.c` with the per-block constants `gp`, `gp2`, `gpR`.

**Also counted:**
- the calibration copy of `tkt_spaceOut()` (62; the image has 64);
- the post-chains with and without the drive;
- the stereo forms A, B, H and the half-rate forms;
- the mono forms of revision 2.

The fixed-point scaling above is the prototype's. Step 1 sets the final
scaling and headroom under the host laws.
