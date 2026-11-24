# S103 P5 — Katran tweaks: `wid` bipolar, a spread that does not collapse

**Date:** 2026-11-24. **Branch:** `dev-ph7-alkaline`, on top of S103 P4
(`S103_P4_KATRAN_AB_ROTATION_CPU.md`). **Status:** implemented, built and
checked on the host. **Uncommitted. Not run on the device.**

**Your instruction (2026-11-24):** "give me wid as bipolar. 0 is full stereo
spread, no collapse. above zero is your previous 'wider' 64-127 but without
the stereo collapse. 0 to -12 shifts them back so -12 is 'full spread, *with*
the collapse', and -12 and below narrows down to mono."

---

## 0. Summary

| Item | Result |
|---|---|
| **`wid`** | `kat_width` is now **bipolar** (`DTYPE_PM63`, stored 63 = display 0, the one-pan rule). `init.kat` and `Katran_initVoice()` set 63, which is display 0 |
| **The law** | 0 = full spread, no collapse; + up to +64 = wider, no collapse; 0 → −12 = blend to the S103 P4 collapsing law; −12 → −63 = that law narrowing to mono |
| **CPU** | **366.6** all-in (P4 363.3; gate 380). ≈ 17.4–17.9k cycles a block, **8.5–11.2 % under** the 19,620 Basic share |
| **RAM** | `sizeof(KatranVoice)` 1,100 B (+16 B: the matrix now has four gains, not two), under the 1,200 B assert |
| **Flash / ITCM / mixer** | image 696,256 B (+264 B); ITCM unchanged; mixer hot loops 0 grew |
| **Host laws** | all pass; KA-ROT rewritten for the new law; `katran_break_rot` now puts `wid 0` on the collapsing law and is **caught** |

---

## 1. What "collapse" is, and the no-collapse form

The P4 rotation panned the two cores in opposite directions with an
equal-power law:

```
L = gL·a + gR·b        R = gR·a + gL·b        gL = √((1 − p)/2), gR = √((1 + p)/2)
```

Here `p = −cos(2π·phase)` is core A's pan. Twice a turn, both cores pass
through the centre (gL = gR = 0.707). L and R are then the same signal, and
the image collapses to mono for a moment.

**The no-collapse form** changes the sign of one term:

```
L = gL·a + gR·b        R = −gR·a + gL·b
```

- **Why it does not collapse:** the two rows are orthogonal and of equal
  norm, so the matrix is a rotation of the (A, B) pair. For two different
  cores (uncorrelated, similar level), L and R stay uncorrelated at every
  phase, and the stereo width never narrows.
- **At the centre crossing** (phase 0.25) it gives L = 0.707 (a + b) and
  R = 0.707 (b − a): a sum and a difference, which are two different
  signals, not mono.
- **At phase 0.5** core A is fully right with its polarity inverted:
  L = b, R = −a. A source's absolute polarity is not audible on its own.
- **At phase 0**, L = a and R = b, the same as before.

---

## 2. The `wid` law (display d, stored d + 63)

The matrix is `[L, R] = W · [L0, R0]`. The base rows are:
- `L0 = gL a + gR b`;
- `R0 = k gR a + r1 b`, with `r1 = √(1 − k² gR²)`.

`W` is the M/S width `L = m + w·s`, `R = m − w·s`.

| Display d | k | w | What you hear |
|---|---|---|---|
| **+1 … +64** | −1 | 1 + d/64 (to 2.0) | wider than full spread (out-of-phase side boost, as P4's 64..127), still no collapse |
| **0** (default) | −1 | 1 | **full spread, no collapse** |
| **−1 … −12** | −1 → +1 linearly (k = −1 + 2·(−d)/12) | 1 | the collapse fades in as d goes down |
| **−12** | +1 | 1 | **full spread with the collapse**: exactly the P4 law |
| **−13 … −63** | +1 | (63 + d)/51, 1 → 0 | the P4 law narrowing; **−63 is mono** |

- **Why `r1` and not `gL`:** with a plain `gL`, R's core-A term would pass
  through zero half-way (k = 0). At phase 0.5 (gL = 0) that left R
  **silent**. `r1` keeps R's row at full power for every k, and equals `gL`
  at k = ±1, so both ends are exactly the forms above. A host law checks
  this: it fails with the plain `gL` (verified).
- **Blend constant:** `KAT_WID_BLEND` 12 (`KatranVoice.h`).
- **Above 0:** the mid part (L + R) is unchanged and the side part (L − R)
  is scaled by w. At +64 the side is doubled (a host law checks it), and L·R
  stays ≤ 0 (anti-correlated), so it never collapses.
- **The rotation itself is unchanged** (`rot`, 0.01–4 Hz, chaos jitter).
  `rot 0` at phase 0 gives L = core A, R = core B for every `wid` ≥ −12.

---

## 3. Cost

| Part | P4 | P5 |
|---|---:|---:|
| Output loop (per sample) | 35 | **37**: four ramped gains (`L = m0 a + m1 b`, `R = m2 a + m3 b`) instead of the symmetric pair. The no-collapse form is not symmetric |
| Per block | 2,251 | 2,290: the law's branches, one more VSQRT (`r1`), four steps |
| **All-in** | 363.3 | **366.6** |

---

## 4. Change list

| File | Change |
|---|---|
| `Core/DSP/Instruments/Katran/KatranVoice.h` | `KAT_WID_BLEND` with its block; `mP, mPStep, mQ, mQStep` → `mx[4], mxStep[4]` (comment updated, +16 B) |
| `Core/DSP/Instruments/Katran/KatranVoice.c` | `kat_stereoTargets()` rewritten (the law above, full comment block); `kat_stereoControl()` and `kat_outRender()` on four gains; init `width` 63 and the matrix at rest |
| `Core/DSP/Instruments/Katran/KatranParameters.c` | `kat_width` → `DTYPE_PM63` / `MOD_PM63`, with a comment |
| `tools/katran/make_init_kat.py`, `SD_CARD/Instrument/Katran/init.kat` | `kat_width=63` (regenerated) |
| `tools/dsp_test/modules/katran.c` | KA-ROT rewritten (§5); KA-BND reads `mx[]` |
| `tools/dsp_test/Makefile` | `katran_break_rot`: `wid 0` on the collapsing law (k = +1) |
| `tools/dsp_test/baselines/s103/katran_cpu_p5.txt`, `README.md` | the P5 record |

---

## 5. KA-ROT, as it now stands

1. Phase 0, d 0: L = g·a, R = g·b.
2. Phase 0.5, d 0: L = g·b, R = −g·a.
3. d 0 at all 1,024 phases: the rows are orthogonal, each with norm g (no
   collapse). At phase 0.25 the L/R correlation is exactly 0.
4. d −12 at all phases: the P4 law (symmetric, equal power), centred at
   phase 0.25.
5. d −6 at all phases: both rows keep norm g (no silent side during the
   blend).
6. d −63 at all phases: mono.
7. d +64 at all phases: L·R ≤ 0 (no collapse); the side part is double
   that of d 0 and the mid part unchanged.
8. `rot 0` holds the phase; `rot 127` advances it at 4 Hz.
9. The output loop is `L = m0 a + m1 b`, `R = m2 a + m3 b` within 1 LSB.

**Break build:** `katran_break_rot` sets k = +1 at d ≥ 0, so `wid 0`
collapses. **Caught.**

---

## 6. Gates (all run)

| Gate | Result |
|---|---|
| `make all && make img` | passes, no Katran warnings |
| `make -C tools/dsp_test katran` | all laws pass; four break builds caught |
| `katran_cpu_check` | 366.6 (gate 380) |
| G-CPU3 mixer hot loops | 40 equal, 1 shrank, 0 grew |
| `special_tags`, `check_special_tags.py` | pass (489 rows) |
| `elf_s099_check` | 15/15 exact |
| `make -k -C tools/dsp_test all` | only the two pre-existing failures (`voice_control` 14; AL-OD0, the absent gitignored golden file) |

---

## 7. On the device

1. Flash `build/LXRV2_lxr02.img`. Copy the new
   `SD_CARD/Instrument/Katran/init.kat`: it carries `kat_width=63`.
   - A P4 `init.kat` has `kat_width=64`, which now reads as **+1** (one
     step wider, barely different).
   - Any Katran file saved before this reads its old `wid` byte under the
     new law, with no migration (the standing rule).
2. **Listen, with `rot` around 34:**
   - **`wid 0`:** the two cores keep their full spread as they turn; no
     moment of mono.
   - **`wid −12`:** the P4 behaviour, narrowing twice a turn.
   - **`wid −1 … −11`:** a gradual return of that narrowing.
   - **`wid −63`:** mono.
   - **`wid +64`:** wide, still without collapse.
   - A mono check (sum L + R) at `wid 0`: core A's weight in the sum is
     (gL − gR), which reaches zero where the cores cross the centre (phases
     0.25 and 0.75). Summed to mono, core A fades out twice a turn; core B
     stays. That is the price of the no-collapse stereo. Use `wid −12` or
     below if the voice must sum to mono cleanly.
3. **The `p` probe:** expect a median ≈ 17.4–17.9k (device gate 18.6k).

---

## 8. Statements this makes out of date (for you to log)

- `S103_P4_KATRAN_AB_ROTATION_CPU.md`:
  - §4, the "what to listen for" collapse note: the default no longer
    collapses;
  - §0 / §2: the CPU figure is now 366.6;
  - §5: `sizeof` is now 1,100 B;
  - §6: KA-ROT is as in §5 of this file.
- `S103_P1_IMPLEMENTATION.md` N-8 / `S103_KATRAN_NOISE_SYNTH.md` §8.2:
  `kat_width` is bipolar (`DTYPE_PM63`), default 63, with the law of §2.
- `MEMORY.md` (when Katran is accepted on the device):
  - `wid` bipolar with a no-collapse default;
  - 366.6 instructions a sample;
  - `sizeof` 1,100 B.
