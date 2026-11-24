# S103 P4 — Katran: toolchain check, cores A / B, the stereo rotation, the CPU cut

**Date:** 2026-11-24. **Branch:** `dev-ph7-alkaline`, base `b79707b3`
("katran dual-half rate engine"). **Status:** implemented, built and checked
on the host. **Uncommitted. Not run on the device.**

**Your instructions (2026-11-24):**
- check S103 P1/P2/P3 now that the toolchain is available;
- rename the two cores from L / R to **A / B**, and add an LFO that rotates
  and spreads them in the stereo field, so neither is tied to a side;
- "knock it down, I'm not testing something over the cpu budget. get it 5%
  or more below";
- look at scale folding, or lower-quality noise on the half-rate core.

---

## 0. Summary

| Item | Result |
|---|---|
| **P1/P2/P3 as committed** | Builds; Katran host laws and both break builds pass; tags and `init.kat` check out. **CPU 406.3 instructions a sample: over the 380 gate** |
| **CPU now** | **363.3** all-in. At 1.485–1.53 CPI: ≈ **17.3–17.8k cycles a block**, **9.3–12 % under** the 19,620 Basic share (the 5 % line is 18,639) |
| **How** | OSC A's sync reset made branch-free; scale folding in both core loops; the output stage reduced to one ramped 2×2 matrix. **No feature cut.** Core B's noise is unchanged: it was not needed |
| **Cores** | Named **A** (full rate) and **B** (half rate) throughout: code, comments, function names, host laws, CPU script |
| **Rotation** | New row `kat_rotate` (`rot`, `mod` page cell 3, index 57, appended). Cores A and B always sit opposite each other and turn round the field at 0.01–4 Hz; `chs` jitters each turn; `wid` sets the spread. `init.kat` value 34: one turn in about 21 s |
| **RAM** | `sizeof(KatranVoice)` **1,084 B** (was 1,092): four dead members removed pay for the new state. No other RAM |
| **Flash / ITCM** | Image 695,992 B (+288 B); ITCM unchanged (15,424 B) |
| **Mixer** | G-CPU3: 40 hot loops equal, 1 shrank, **0 grew** |
| **Host laws** | All pass, including two new groups (**KA-FOLD**, **KA-ROT**), each with a break build that is caught |

---

## 1. The check of P1 / P2 / P3 (before any change)

| Gate | Result |
|---|---|
| `make all && make img` | passes; no Katran warnings |
| `make -C tools/dsp_test katran` | all laws pass; `katran_break_dly` and `katran_break_pwm` caught |
| `special_tags`, `check_special_tags.py` | pass |
| `elf_s099_check` | 15/15 exact |
| `elf_p4_check` | 17 missing / 5 renamed: the same pre-existing drift S103 P1 §15.2 recorded |
| **`katran_cpu_check`** | **406.3 (gate 380): FAIL.** Inside P2 §5's "> 395 after §6" zone |
| `make -k -C tools/dsp_test all` | two failures, **both older than Katran**: `voice_control` 14 failures (identical on `ef3d3802`, `c811821f`, `36e6d9e5`); `alkaline` AL-OD0 (`baselines/s094p1/alk_golden.bin` is gitignored by `*.bin`, so it is absent from this checkout) |

**The 406.3, by loop** (instructions per iteration): OSC B 30 (PWM D);
OSC A 56 core A / 57 core B (PWM D); delay 18 / 18; core 101 / 111; output
42; per block 2,122 / 32 = 66.3.

**What the loop dumps showed:**
- **OSC A's sync.** The P2 `if (cur && prev == 0) { … }` compiled into a
  loop with several paths. Its longest path cost 26 instructions more than
  OSC B's identical form without sync.
- **The core loops** spent their time on format traffic: a 1/32768 multiply
  after each of five int16 loads, the exciter's three gains every sample,
  the ring crossfade's `(1 − rk)` and `rk × bLvl × B`, and a 32,767 or
  32,768 on each store. On top of that, about ten stack spills an iteration.

---

## 2. The CPU cut (no feature removed)

| # | Change | Where | Measured |
|---|---|---|---|
| C1 | **Branch-free sync reset**: `rst = cur & ~prev`, `keep = rst − 1`, `q −= ph & ~keep`, `ph &= keep`, `row = rst ? rowNext : row`. Same behaviour (the B5 width carry included; the init value −1 still means "no reset on the first sample") | `KatranOsc.c` `kat_aBody()` | OSC A PWM D 56 → 45 (core A), 57 → 49 (core B) |
| C2 | **Scale folding** (P2 §6 S1): the loads stay unscaled; the 1/32768s, `tGain`, `tGain2 × exc × (1 − nse)`, `tGain2 × exc × nse`, `LV1 × tWet`, `LV1 × (1 − tWet)`, `xfb`, `fb` and the ring write's 32,768 are folded into per-block gains. Each core's output, `t1 × ((1 − rk) + rk bLvl B) + g2 t1` (core A) or `… × (1 + g1)` (core B), times 32,767, becomes `t1 (c0 + c1 B)`, with `c0`/`c1` ramped (`kat_outGains()`). OSC B's wrap LSB is no longer masked off (one LSB of the modulator, −90 dB) | `KatranVoice.c` `kat_coreBody()`, `kat_outGains()` | core 101 → 85 (A), 111 → 93 (B) |
| C3 | **The output stage as one 2×2 matrix**: `L = P a + Q b`, `R = Q a + P b`. It carries the rotation, the width, CE1's gain, `KAT_OUT_NORM` and the int16 scales. It replaces P2's M/S width, its CE1 ramp and its per-side scales | `kat_outRender()`, `kat_stereoTargets()`, `kat_stereoControl()` | output 42 → 35 |
| | Per block: + the rotation and matrix targets (one `alk_exp2f`, one table read, two VSQRT), + the four output-gain ramps | | 66.3 → 70.3 a sample |
| | **Total** | | **406.3 → 363.3** |

**Not applied:** S3 (cutting core B's dry tap or noise). It was not needed
to reach the target, so core B keeps its full-quality RNG noise.

**Ramps:** still seven per core sample, as in P2. Products of ramps
(`(1 − rk)(1 + g1)`) are now ramped linearly as a product. They differ from
P2's product of two ramps by less than one ramp step inside a block.

---

## 3. Cores A and B

| Was | Now |
|---|---|
| core L (full rate), core R (half rate) | **core A** (full rate), **core B** (half rate) |
| `kat_delayReadL/H`, `kat_coreRenderL/H` | `kat_delayReadA/B`, `kat_coreRenderA/B` |
| `kat_a{Plain,PwmD,Warp,Arena}L/H` | `kat_a{Plain,PwmD,Warp,Arena}CoreA/CoreB` |
| `Katran_initVoice(…, ringL, ringR)` | `Katran_initVoice(…, ringA, ringB)` (and the `InstrumentManager.c` call site) |
| `rowL/rowR`, `posL/posR` (OSC A's two copies) | `rowCA/rowCB`, `posCA/posCB` |

**Naming clash to be aware of:** the oscillators were already called
**OSC A** (the exciter, one copy per core) and **OSC B** (the shared ring
modulator). The code therefore says "OSC A's core-A copy" / "core B's copy",
and the function suffix is `CoreA` / `CoreB` rather than a bare `A` / `B`. If
you would rather have different core names (for example "1" / "2"), it is a
rename only.

`buf` and `right[]` hold core A's and core B's blocks through the render.
They become L and R only in `kat_outRender()`. The VCA pair's asymmetry
(AMP2 on core A, AMP1 on core B) stays with the core, so it rotates with it.

---

## 4. The rotation (`kat_rotate`)

| | |
|---|---|
| **Row** | `kat_rotate`, label `rot` (`SHORT_ROTATION` / `LONG_ROTATION` reused from Euklid under `CAT_VOICE`: no new label id), `DTYPE_0B127`, morphable / modulatable / automatable. **Index 57, appended** after `lfo_target_param_2` (append-only rule). Indices 58–59 stay free; 60–63 stay reserved for the effect |
| **Page** | `mod`: `dst amt chs rot` \| `frq mod dpt bal` (the empty cell 3) |
| **Rate** | 0 = still (the phase holds); 1..127 = 0.01 Hz × 400^((rot − 1)/126), i.e. 0.01 Hz (100 s a turn) to 4 Hz. `init.kat`: 34 ≈ 0.048 Hz |
| **Chaos** | at each new turn the rate is scaled by 2^(±0.5 × chs') |
| **Placement** | core A's pan p = −cos(2π·phase), core B at −p (always opposite). Equal-power gains `sqrt((1 ∓ p)/2)` |
| **Spread** | `wid` (unchanged row): 0 mono, 64 = full opposite placement, above 64 wider (out of phase), the same law as P2's M/S width |
| **Free-running** | no trigger, kick, transport or Scene event resets it. The phase starts at 0 (core A left), so `rot 0` with `wid 64` is exactly P2's output |

**What to listen for:** as the cores cross the centre (twice a turn) the
image narrows to near-mono for a moment, at constant level (equal power).
That is what a two-channel rotation of two opposite sources does. If you want
the image never to collapse, the next step would be a minimum separation,
or a phase offset between A and B so they are not always mirror images. That
is a law change in `kat_stereoTargets()`, with no extra per-sample cost.

---

## 5. Memory

| Change | Bytes |
|---|---:|
| removed (dead: written at init, never read): `dcY[3]`, `f2y[2]`, `f3R1`, `f3R2` | −28 |
| removed: `rk`, `rkStep`, `g1`, `g1Step`, `g2`, `g2Step`, `ceg`, `cegStep`, `wid` | −36 |
| added: `cA0/1`, `cB0/1` and their steps (8 floats); `mP`, `mQ` and steps (4); `rotPh`, `rotJit` (2) | +56 |
| added: row byte `rotate` (+ padding) | +0 |
| **`sizeof(KatranVoice)`** | **1,092 → 1,084** |

No other RAM, no ITCM.

---

## 6. Host laws (`tools/dsp_test/modules/katran.c`)

- **Renamed throughout:** core A / core B, the new function names, `ringA` /
  `ringB`.
- **KA-VCA** now reads `g1` / `g2` back from the folded output gains:
  `cA0 = g2 × 32767` and `cB1 = bLvl (1 + g1) × 32767 / 32768` at the block
  end.
- **KA-BND** checks `mP` / `mQ` instead of `ceg`.
- **New: KA-FOLD.**
  - Part 1: after 100 blocks of a sounding drone (noise 60), each core's
    folded loop is compared with a double-precision reference written in the
    P2 form (unfolded gains, normalised units, the noise replayed). Every
    ring write and output sample must agree within 3 LSB, on both cores.
  - Part 2: `kat_outGains()` against the P2 ring / VCA formula, over a grid
    of g1, g2, bLevel, t1 and B.
  - Break build `katran_break_fold` (the dry gain without its 1/32768):
    **caught**.
- **New: KA-ROT.**
  - Phase 0 gives L = core A, R = core B; phase 0.5 swaps them; phase 0.25
    centres both at g/√2.
  - Equal power (P² + Q² = g²) at all 1,024 phases; width 0 gives P = Q
    (mono).
  - `rot 0` holds the phase over 1,000 blocks; `rot 127` advances it at
    4 Hz.
  - The output loop computes L = P a + Q b and R = Q a + P b within 1 LSB.
  - Break build `katran_break_rot` (core A given the same side gain on both
    outputs): **caught**.

---

## 7. Change list

| File | Change |
|---|---|
| `Core/DSP/Instruments/Katran/KatranOsc.c` | branch-free sync (C1, comment block); the eight wrappers renamed `*CoreA` / `*CoreB`; comments |
| `Core/DSP/Instruments/Katran/KatranOsc.h` | the eight declarations renamed, with a block on the A/B naming; comments |
| `Core/DSP/Instruments/Katran/KatranVoice.h` | header (A/B, the matrix); `KAT_ROT_*`; struct: dead members removed, the output gains, matrix and rotation state, the `rotate` byte; comments on `right[]`, `blkD`, `phA`, `pwD`, `tNow`, `f1`, `f3`, `hPrev`; `Katran_initVoice(…, ringA, ringB)` |
| `Core/DSP/Instruments/Katran/KatranVoice.c` | `KAT_INV_32768`; `kat_coreBody()` folded (C2); `kat_coreRenderA/B`, `kat_delayReadA/B`; `kat_stereoTargets()`, `kat_stereoControl()`, `kat_outRender()` as the matrix (C3); `kat_outGains()`; init (rings A/B, rot 34, output gains, matrix at rest); async (output-gain steps, `kat_stereoControl()`); sync (renamed calls, output-gain advances); A/B wording in every comment |
| `Core/DSP/Instruments/Katran/KatranParameters.c/.h` | `KAT_PARAM_ROTATE` (57) and its row; `mod` page cell 3; counts 57 → 58; comments |
| `Core/DSP/Instruments/InstrumentManager.c` | the Katran init call's local `ringL` → `ringA`, with a comment |
| `tools/katran/make_init_kat.py`, `SD_CARD/Instrument/Katran/init.kat` | `kat_rotate=34` (regenerated; both sections, last in table order) |
| `tools/dsp_test/modules/katran.c` | renames; KA-VCA, KA-BND updated; KA-FOLD, KA-ROT |
| `tools/dsp_test/Makefile` | `katran_break_fold`, `katran_break_rot` |
| `tools/dsp_test/katran_cpu.py` | new symbol names; `kat_stereoTargets` counted (out of line); labels |
| `tools/dsp_test/baselines/s103/katran_cpu_p4.txt`, `README.md` | the P4 record |

---

## 8. What you need to do on the device

1. Flash `build/LXRV2_lxr02.img`. Copy the new
   `SD_CARD/Instrument/Katran/init.kat` to the card: it has the `kat_rotate`
   key, and a file without it loads `rot 0` (still).
2. **The `p` probe** on one Katran at its dearest settings (`typ pwm`, Rec
   or Saw, `src off`). Expect a median ≈ 17.3–17.8k, against the 18.6k
   device gate.
3. **Listen:**
   - A and B travel round the field, opposite each other, about one turn in
     21 s at `rot 34`;
   - `rot 0` holds; `rot 127` is a fast auto-pan;
   - high `chs` makes the turns uneven;
   - `wid 0` is mono, `wid 127` wider;
   - the level stays even as they cross the centre.
4. **Level:** the folding is exact to within 3 LSB (KA-FOLD), so the
   sound and level should match P2. Only the placement is new.

---

## 9. Statements this work makes out of date (for you to log)

- `S103_P2_KATRAN_DUAL_FULL-HALF_IMPLEMENTATION.md`:
  - "core L / core R" are now core A / core B;
  - §5's CPU estimate is superseded by the measured 406.3 (before) and
    363.3 (after);
  - §6 S1 is applied.
- `S103_P1_IMPLEMENTATION.md`:
  - N-8: 58 rows; `mod` cell 3 is `rot`;
  - §9: laws KA-FOLD and KA-ROT;
  - §15 / §16: the CPU figures.
- `S103_KATRAN_NOISE_SYNTH.md` §8.1 / §8.2: 37 own rows; free indices
  58–59; the `mod` page layout; the L/R output description (now a rotating
  A/B pair).
- `MEMORY.md`, once Katran is accepted on the device:
  - Katran as the eighth type;
  - its full / half-rate core pair named A / B;
  - the rotation row;
  - 363 instructions a sample;
  - `sizeof` 1,084 B.
- The pre-existing `voice_control` 14 failures and the absent
  `alk_golden.bin` (AL-OD0) are not Katran's. They are worth an Open Items
  entry.
