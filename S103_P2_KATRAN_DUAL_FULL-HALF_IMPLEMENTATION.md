# S103 P2 — Katran: bug fixes, the full-rate / half-rate core pair, the short-label merge

**Date:** 2026-11-24. **Branch:** `dev-ph7-alkaline`, base `0669d578`
("katran implementation"). **Status:** implemented, uncommitted.
**Not built, not run:** the machine this was written on has no compiler of
any kind (no ARM toolchain, no host gcc/clang, no make). Every change below
was reviewed by reading only. The first build, the host laws and the CPU
count are yours (§7).

**Your instructions (2026-11-24):**
- "do the bugfixes and the migration to the dual full-half cores yourself,
  keep the implementation direct and targeted and make sure that every code
  change is accompanied by a detailed comment-block description";
- "Keep detailed implementation notes on the bugfixes and migration in
  'S103_P2_KATRAN_DUAL_FULL-HALF_IMPLEMENTATION.md'";
- "Also implement S103_P3_SHORT_NAMES_MERGE.md".

**Sources:**
- `S103_P1_IMPLEMENTATION.md` §15 (the implementer's stop at the CPU gate)
  and §16 (the review: bugs B1–B4, savings O1–O6, options K1–K4);
- the full + half option as answered in the session (≈ 385 instructions a
  sample after O1–O5, estimated);
- `S103_P3_SHORT_NAMES_MERGE.md` (all tiers, A–D).

---

## 0. Summary

| Part | What changed | Files |
|---|---|---|
| **Bug fixes** | B1 delay read (negative index, wrong interpolation); B2 PWM D sum wrapping; B3 arena store wrapping; B4 the re-arm pushing the run gain past 1. Also B5 (new, found while restructuring): a sync reset left the PWM D second phase behind | `KatranVoice.c`, `KatranOsc.c` |
| **Migration** | Core R now runs at half rate (16 samples a block at fs / 2): its delay, OSC A copy, TORCIDO / F1 / F2 loop and ring write; its output is interpolated 2× into `right[]`. Core L is unchanged at the full rate | `KatranVoice.h/.c`, `KatranOsc.h/.c` |
| **Savings applied with it** | O1 (fixed-point delay read: fixes B1); O3 (one function per core, no per-sample core select); O4 in part (SSAT for every int16 store); O5 (oscillator-loop state in registers). Not applied: O2 as specified (core R's own half-rate RNG read gives 1.5 reads a sample instead) and O4's scale folding (§6) | same |
| **Tooling** | `katran_cpu.py` for the new structure: one loop per function, half-rate loops counted at half weight, only running forms' entries; its baseline regex fixed. Host laws KA-DLY, KA-RANGE, KA-HALF; two break builds | `tools/dsp_test/katran_cpu.py`, `modules/katran.c`, `Makefile` |
| **Short labels (S103 P3)** | Tiers A–D: 26 ids removed (249 → 223), every use repointed | `menu.h`, `MenuLabels.c`, `menu.c`, 7 parameter tables |
| **CPU (est.)** | ≈ 385–400 instructions a sample ≈ **18.3–19.5k** cycles a block: **0.6–6.7 % under** the 19,620 Basic share. Inside your 5–10 % band only at the low end; the measurement decides (§5) | — |
| **RAM** | +4 B in `KatranVoice` (`hPrev`), inside the slot reserve; the 1,200 B static assert still gates it. No other RAM | `KatranVoice.h` |

---

## 1. Bug fixes

### B1 — the delay read (`KatranVoice.c`, `kat_delayBody()`)

- **Defect:** the P1 read computed `idx = (w + n) − t` in float and took
  `(int32_t)idx`, which truncates toward zero. Whenever the write index was
  behind the delay time (most of the ring cycle at long times), `idx` was
  negative. The integer part was then the ceiling, `frac` was negative, and
  the read extrapolated along the next sample's slope instead of
  interpolating.
- **Fix:** a Q16 fixed-point position:
  `pos = ((w + n + KAT_RING) << 16) − Tq`, with `Tq = T × 65,536`.
  - `i0 = (pos >> 16) & mask` is always the floor sample. The unsigned
    position wraps modulo 2³², a multiple of the ring, so this holds even
    without the `+ KAT_RING`, which is kept for clarity.
  - The 15-bit fraction `(pos >> 1) & 0x7FFF` keeps `(b − a) × frac`
    inside int32.
  - The read is all integer. The P1 form converted int16 to float three
    times a sample plus the index, and measured 53 instructions.
- **Law:** KA-DLY. A non-linear ring with `w = 10 < T = 100.37` (core L) and
  `w = 5 < T = 60.5` (core R) must equal the true linear interpolation plus
  the dry tap, halved, within 2 LSB.
- **Break build:** `katran_break_dly` drops the fraction, so KA-DLY must fail.

### B2 — the PWM D sum (`KatranOsc.c`, `kat_bPwmD()`, `kat_aBody()` PWMD)

- **Defect:** `kat_read(p) + sign × kat_read(q)` reaches ±65,534 and was
  stored as `(int16_t)`, so it wrapped. This hit the exciter and the ring
  modulator under `typ pwm` (the `init.kat` setting).
- **Fix:** the sum is halved (`>> 1`) before the store. The Colloid avoids
  the wrap differently: it keeps the same sum in int32 buses
  (`ColloidOsc.c` `col_pwmPairStore()`).
- **Sound:** −6 dB on the exciter and the ring's modulator under pwm. Both are
  set by rows (`lvA`, `lvB`); retune `init.kat` by ear if needed (§7).
- **Law:** KA-RANGE part 1. Over 50 blocks of moving width, OSC B's every
  stored sample equals the halved int32 sum (LSB aside).
- **Break build:** `katran_break_pwm` removes the halving, so KA-RANGE must
  fail.

### B3 — the arena store (`KatranOsc.c`, `kat_bArena()`, `kat_aBody()` ARENA)

- **Defect:** the conditioned arena value was stored with a bare
  `(int16_t)` cast. The DC block's high-pass of a full-scale step overshoots
  to ≈ 2× before the guard, a block later, acts, so the value wrapped to the
  opposite sign.
- **Fix:** `kat_osat16()` saturates: one SSAT on the device, the two-sided
  compare on the host.
- **Law:** KA-RANGE part 2. A −32,767 → +32,767 arena step through core L's
  arena read (damping open, guard 1) must store > 30,000 at the step.

### B4 — the re-arm ceiling (`KatranVoice.c`, `kat_runControl()`)

- **Defect:** the K1 re-arm added `KAT_REARM_STEP` with no ceiling, and
  `grStep` was computed from the unclamped value. A kick into a sounding
  drone ramped the loop gain above 1 for one block.
- **Fix:** `newGr = min(newGr, 1)` before `grStep`.
- **Law:** none new. The fix is one comparison, and KA-RUN2 / KA-KICK cover
  the paths around it.

### B5 (new) — the PWM D phase across a sync reset (`KatranOsc.c`, `kat_aBody()`)

- **Defect:** on a reset the P1 loop set `ph = 0` but left the second read's
  phase `q` running. The pulse width was wrong for the rest of that block,
  until the next block recomputed `q`.
- **Fix:** `q −= ph` before `ph = 0`, so `q` keeps its offset and the pulse
  its width.
- **Law:** none (it is an inside-a-block glitch). KA-SYNC still checks the
  resets.

---

## 2. The migration: core R at half rate

### 2.1 What runs where

| Stage | Core L (the L output) | Core R (the R output) |
|---|---|---|
| Delay read | `kat_delayReadL()`: 32 samples, dry tap 32 | `kat_delayReadH()`: 16 half-rate samples, dry tap 16 (`KAT_TAP_DRY_H`) |
| Delay time | `tNow[0]`, full-rate samples | `tNow[1]`, half-rate samples = core L's law / 2 (the same comb pitch); minimum 16 (`KAT_TIME_MIN_H`) |
| OSC A copy | `kat_a*L()`: 32 samples, `incA[0]` | `kat_a*H()`: 16 samples, phase step `2 × incA[1]`; sync from `blkD[1][k]` and OSC B's wrap ORed over `blkB[2k]`, `blkB[2k + 1]` |
| OSC A table rows / cap | octave from fA; cap 0.45 fs | octave from `2 × fAR` (one octave up); cap 0.45 (fs / 2) (`KAT_F_MAX_H`) |
| TORCIDO, F1, F2, ring write | `kat_coreRenderL()`: 32 samples | `kat_coreRenderH()`: 16 samples |
| F1 coefficients | prewarp π / fs, ceiling 15.96 kHz, 32-sample ramp | prewarp π / (fs / 2), ceiling 8.1 kHz (`KAT_CUT_ST_MAX_H`: tan()'s Padé form stays finite), 16-sample ramp |
| F2 coefficient | `f2K` | `f2K (2 − f2K)`: the same one-pole corner at half the rate |
| Cross-feedback input | core R's block held (`blkD[1][n >> 1]`) | core L's block decimated (`blkD[0][2k]`) |
| OSC B (ring modulator) | `blkB[n]` | `blkB[2k]` |
| Noise | one RNG read a sample | one RNG read a half-rate sample |
| Output | `buf[n] = (r + g2 t1)` | `o = r (1 + g1)`; `right[2k] = (hPrev + o) / 2`, `right[2k + 1] = o` |
| Ramps (gr, fb, rk, VCA) | step per sample | step × 2 per half-rate sample (both reach the block's end values) |

OSC B, the output stage (F3, width, CE1), the per-block control and every
row are unchanged and full rate.

### 2.2 Sound changes to listen for (§7 device step)

- **Core R's loop band stops at about 11 kHz** (its F1 at 8.1 kHz at most).
  The R output is darker, and its drive aliases less.
- **The shortest R comb is 0.73 ms, the same as L's:** 16 half-rate samples
  equal 32 full-rate samples. Pitch-matched delay times mean the same `tim`
  gives the same comb pitch on both cores.
- **The 2× linear interpolation** adds a gentle top-octave roll-off and half
  a full-rate sample of delay on R.
- **The cross-feedback between the cores** is sample-held one way and
  decimated the other, which adds a little high-frequency coupling noise at
  high `spr` coupling (`spr` < 63).
- **Not taken:** core R could reach a 93 ms (10.7 Hz) comb with the same
  ring. It is pitch-matched instead, so `tim` means one thing. Unmatching it
  is a one-line change in `kat_delayControl()` if you want it.

### 2.3 Code structure

- **`KatranOsc.c`:** one `always_inline` body, `kat_aBody()`, serves all
  eight OSC A wrappers (four forms × two rates). `form` and `half` are
  constants in each wrapper, so the compiler removes the other forms and the
  other rate.
- **`KatranVoice.c`:** `kat_delayBody()` and `kat_coreBody()` do the same for
  the delay and core loops.
- **One `noinline` function per loop**, so `katran_cpu.py` counts each by
  name, one sample of one core per count. This settles the review's M1.
- **Header changes:** `KatranOsc.h` declares the eight A wrappers, replacing
  the four `(v, c, th)` loops. `KatranVoice.h` adds `KAT_HALF_N`,
  `KAT_TAP_DRY_H`, `KAT_TIME_MIN_H` and `hPrev`, and documents `tNow[1]`'s
  units.

---

## 3. Change list (every edit carries its comment block in the source)

| File | Change |
|---|---|
| `Core/DSP/Instruments/Katran/KatranVoice.h` | file header: the S103 P2 note; constants `KAT_HALF_N`, `KAT_TAP_DRY_H`, `KAT_TIME_MIN_H` with a block on the half-rate core; `tNow` comment (units); `float hPrev` with its block |
| `Core/DSP/Instruments/Katran/KatranVoice.c` | constants `KAT_PI_OVER_FS_H`, `KAT_CUT_ST_MAX_H`, `KAT_F_MAX_H`; `kat_sat16()` as SSAT; `kat_svfControl()` / `kat_svfInit()` take the core's prewarp, ceiling and ramp length; B4 in `kat_runControl()`; `kat_oscControl()` core R's cap and rows (`fAR`, `fAR2`); `kat_delayControl()` core R in half-rate samples; `kat_delayBody()` + `kat_delayReadL/H()` (B1, O1); `kat_coreBody()` + `kat_coreRenderL/H()` (the migration, O3, the interpolation); `kat_guardUpdate()` takes the block's valid length; `Katran_initVoice()` core R's time, `hPrev`, per-rate F1 init; `Katran_calcVoiceAsync()` per-rate F1; `Katran_calcVoiceSyncBlock()` the new order and `ringPrev` |
| `Core/DSP/Instruments/Katran/KatranOsc.h` | the eight `*L` / `*H` declarations with their block; header comment |
| `Core/DSP/Instruments/Katran/KatranOsc.c` | `kat_osat16()` (B3); `kat_condStep()` (state in registers) and `kat_cond()` kept as its wrapper (host law); B loops: B2 halving, B3 saturation, row switch as a select, state in locals; `kat_aBody()` (B2, B3, B5, the half rate, O5); the eight wrappers |
| `tools/dsp_test/modules/katran.c` | KA-DLY, KA-RANGE, KA-HALF; `kat_guardUpdate()`'s new argument; KA-SPR in half-rate units; every group counted (the P1 `main()` listed groups by hand) |
| `tools/dsp_test/Makefile` | `katran_break_dly`, `katran_break_pwm` in the `katran` target; its comment |
| `tools/dsp_test/katran_cpu.py` | the new composition (§5), per-iteration print-out, baseline regex fix |
| Short labels (S103 P3) | §4 |

---

## 4. The short-label merge (`S103_P3_SHORT_NAMES_MERGE.md`, all tiers)

**Method:** a script removed each id from `enum shortNamesEnum` (`menu.h`)
and the string at the same position from `shortNames[]` (`MenuLabels.c`),
then repointed every use. It verified that both lists stay the same length
and that every remaining id still maps to its old string.

| Tier | Removed id → kept id | Rows repointed |
|---|---|---|
| A (no visible change) | `FXR`→`RTE`, `FXM`→`MIX`, `MODE`→`MOD`, `ROLL`→`ROLL_RATE`, `EG2` (unused) | `AlkalineParameters.c` fx_rate / fx_mix; `ColloidParameters.c` fx_rate / fx_mix; `menu.c` valueNames ×3 |
| B (Katran) | `LFR`→`FREQ`, `LFM`→`MOD`, `LFL`→`FXD`, `VCA`→`BAL`, `EXC`→`LVA`, `NSE`→`NOISE`, `STH`→`SYNC`, `WET`→`BLN`, `DRF`→`WOW`, `LVL`→`LVB` | `KatranParameters.c` ×10; its pages comment |
| C | `DCY`→`DECAY`, `LNG`→`LENGTH`, `STM`→`MICROTIMING`, `SFB`→`FBK`, `SDT`→`DTN`, `STO`→`TON` | `SpaceParameters.c` ×2; `CrumpBitParameters.c` ×1; `TetraktParameters.c` ×4 |
| D | `FAT`→`ATTACK`, `FDE`→`DECAY`, `FSL`→`SLV`, `FRE`→`REL`, `FAM`→`FM_AMNT` | `ColloidParameters.c` col_fenv ×5 |

- **Result:** 249 → **223** short ids (of the 256 the `MenuLabels.c` assert
  allows).
- **No stored data changes:** labels are compile-time ids.
- **Comment blocks:** at each removal point in `menu.h` and `MenuLabels.c`,
  and above each repointed row group.

**Displayed text changes (Tiers C, D):**
- Space reverb / plate decay `dcy` → `dec`;
- CrumpBit grain length `lng` → `len`;
- Tetrakt space `stm sfb sdt sto` → `tim fbk dtn ton`;
- Colloid filter envelope `fat fde fsl fre fam` → `atk dec slv rel amt`.

---

## 5. CPU (est.)

**From the P1 measured per-loop counts** (`S103_P1_IMPLEMENTATION.md` §15.2),
with the changes above. Instructions per full-rate output sample:

| Loop | P1 measured | Now (est.) | Basis |
|---|---:|---:|---|
| Delay read L | 53 (one core or both: M1) | ≈ 22 | integer Q16 read |
| Delay read R (÷ 2) | — | ≈ 11 | the same at 16 iterations |
| Core L | 111 | ≈ 103 | no per-sample select, one VCA ramp, SSAT stores |
| Core R (÷ 2) | 111 | ≈ 56 | ≈ 112 an iteration (the interpolation's extra store) |
| Output | 47 | ≈ 43 | SSAT stores |
| OSC B (dearest) | 30 | ≈ 31 | the halving |
| OSC A L (dearest) | 53 | ≈ 45 | state in registers |
| OSC A R (dearest, ÷ 2) | 53 | ≈ 24 | ≈ 48 an iteration (the paired wrap OR) |
| **Loops** | **469** | **≈ 335** | |
| Per block ÷ 32 | 65.7 | ≈ 60–64 | two more loop entries, unused form entries no longer counted |
| **Total** | **534.7** | **≈ 395–400** | |
| **Device, at 1.485–1.53** | ≈ 25.4–26.2k | **≈ 18.8–19.5k** | **0.6–4 % under** 19,620 |

If the scale folding of §6 is applied (≈ −10 to −15), the total is ≈ 385,
or ≈ 18.3–18.9k: 3.7–6.7 % under. Either way the result sits at or just
under the lower edge of your 5–10 % band, so **the linked count decides**:

- **≤ 380 linked (G-CPU1):** in band; go to the device step.
- **380–395:** apply §6, then re-count.
- **> 395 after §6:** the next step is a cut on the half-rate core (its 32-
  sample dry tap, or its noise). That is your decision; it is not applied.

---

## 6. Not applied (candidates, in order)

| # | Change | Saves (est.) | Why not now |
|---|---|---:|---|
| S1 | **Scale folding:** the int16 → float loads in the core and output loops multiply by 1/32,768 after the convert. Fold the scale into the per-block gains (`tg`, `exc`, `bLvl`, `xfb`, `fb`, the output scale), or check whether GCC already emits the fixed-point `vcvt #15` for `(float)x × 2⁻¹⁵` | 10–15 | It changes many gains at once; the linked count should say whether it is needed |
| S2 | O2 as specified: core L reads a 32-bit RNG word and hands core R the top half | ≈ 2 + half a peripheral stall | Core R at half rate already needs only half the reads; the hand-off would need a 16-sample buffer, or a rule tying core R's sample k to core L's sample 2k |
| S3 | A small cut on the half-rate core (its dry tap ≈ −4, its noise ≈ −3) | ≈ 7 | A feature change, your call |

---

## 7. What you need to run

1. **Build:** `make all && make img`. Watch for:
   - the `InstrumentManager.c` asserts (`sizeof(KatranVoice) <= 1200`; it
     grew 4 B);
   - the `MenuLabels.c:188` / `:194` label asserts.
2. **Host laws:** `make -C tools/dsp_test katran` must show:
   - "all laws pass", including KA-DLY, KA-RANGE and KA-HALF;
   - `katran_break_dly` and `katran_break_pwm` both "caught".
3. **Everything else on the host:** `make -C tools/dsp_test all`, and
   `special_tags` (the label merge touched seven parameter tables; keys are
   unchanged).
4. **CPU:** `make -C tools/dsp_test katran_cpu_check`. It prints the
   all-in figure and the per-iteration counts for each loop; compare against
   §5. Then `elf_s099_check`, `elf_p4_check`, and the mixer hot-loop diff
   against `tools/dsp_test/baselines/s103/mixer_base.txt` (G-CPU3).
5. **Device** (once the count is ≤ 380):
   - the `p` probe on one Katran at its dearest settings;
   - listen: R darker than L; no wraps under pwm; the arena form clipping
     cleanly on garbage; kicks into a drone with no jump;
   - the label changes on the Space, CrumpBit, Tetrakt, Colloid and Katran
     pages.
6. **`init.kat` levels:** B2 halved the pwm exciter and ring levels (−6 dB).
   Retune `kat_level_a` / `kat_b_level` in `init.kat` by ear if needed; the
   generator is `tools/katran/make_init_kat.py`.

**Line endings:** the scripts used here rewrote these files with LF line
endings. The repository stores LF and `core.autocrlf` is true, so diffs and
commits are unaffected:
- `KatranVoice.c`;
- `menu.h`, `MenuLabels.c`, `menu.c`;
- the seven parameter tables;
- `tools/dsp_test/Makefile`.

---

## 8. Statements this work makes out of date (for you to log)

- `S103_P1_IMPLEMENTATION.md`:
  - §4 N-2 / N-4: the delay read, the core loop and the OSC A loops are now
    the full / half pair;
  - §9: three new laws;
  - §15–§16: superseded for the CPU figures once re-measured.
- `S103_KATRAN_NOISE_SYNTH.md`: the dual core is now full + half rate (K-D1
  revised).
- `S103_P3_SHORT_NAMES_MERGE.md`: implemented (status line).
- `MENU_UI_SPEC.md`, `SPACE_GROUP_EFFECTS_MODULE_REFERENCE.md`,
  `EFFECTS_MODULES_DSP_REFERENCE.md`, `TETRAKT_MODULE_REFERENCE.md`,
  `COLLOID_MODULE_REFERENCE.md`: the label changes of §4.
- `MEMORY.md`: the short-label count (223 / 256), and the Katran's
  full / half-rate core pair once it is accepted on the device.
