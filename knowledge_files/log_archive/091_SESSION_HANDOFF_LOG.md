# Session 091 — CrumpBit Effect Expansion

## End of session

```
DATE: 2026-11-06
SESSION GOAL: Expand CrumpBit from a single tape delay into a five sub-type
  multi-engine (dly/cmb/pit/grn/rvb) with per-sub-type lane maps, granular
  rework, efficiency pass to the 170-instruction/frame cap, and parameter
  cleanup.
COMPLETED: All six phases (P1–P6) implemented, host-tested, and
  hardware-verified. CrumpBit is now a five-engine effect with elastic arena,
  DC blocker, tone tilt, soft knee, stereo output filter, sub-type dispatch,
  and full parameter UI. Every sub-type under the 170-cap. Flash 712,684 B
  (headroom 40,980 B). Runtime 100 B (within +32 B approval over 76 B base).
VERIFIED ON HARDWARE: Yes — all phases tested on target STM32F765VIH6 and
  passed.

CHANGES THIS SESSION:
- CrumpBitEffect.h / .c: five sub-type engines, elastic arena, Q14 tap read,
  one-pole output filter, grain rework, tap fade, labels hook, all efficiency
  changes
- CrumpBitParameters.h / .c: 15 → 24 rows, per-sub-type lane maps and masks,
  labels hook, dpn→spn rename, fin row, grn lane remap
- CrumpBitTables.h / .c: 6 new P3 tables + onePoleK + dlyTime + grainTime +
  grainDensity + grainStopCc + grainFraction retuned
- EffectsManager.h / .c: labels hook replaces name3, effective_max takes
  runtime, lane_map vtable, BUFFER_DEPENDENT re-apply, self-check 11
- menuEffects.c / .h: labels hook display, per-sub-type layouts and value
  text, page order changes, pit fine display
- menu.c: labels in Effect row rendering
- storageTypes.c: lane keys take record, grn lanes saved
- gen_crumpbit_tables.py: all new table generators and checks
- elf_looppath.py: --cond-heads, --in-range, cycle-pruned search, --rank
- elf_blockpath.py (new): per-block longest path
- crumpbit_cpu.py (new): 170-cap gate
- tools/dsp_test/Makefile: 25 break builds, all ELF and CPU gates
- tools/dsp_test/modules/crumpbit.c: host laws CB1–CB23, 25 breaks

KNOWN ISSUES INTRODUCED: None.
KNOWN ISSUES RESOLVED: None pre-existing; P4 age defect (grains aging 31
  frames too fast on dropped triggers) found and fixed within P5.

NEXT SESSION RECOMMENDED GOAL: New feature work or other module improvements.
  CrumpBit is complete and within budget.
BLOCKERS: None.

CRITICAL REMINDERS FOR NEXT SESSION:
- CrumpBitRuntime is 100 B. User approved +32 B over 76 B base; 24 B used.
  8 B remain before the next arena granule is lost.
- grn margin is 3.5 instructions/frame under the 170 cap; any per-frame
  addition to grn must be measured.
- G3 (grain block-aligned ends) relies on 32-frame blocks from mixer.c;
  future variable block size would need the lower clamp restored.
- X2 (bof/biv 256-byte lookup table) rebuild cost (~48 per frame for one
  block) is outside the 170 cap by design.
- The 170-cap gate is crumpbit_cpu.py, enforced by the Makefile.
```

---

## §1  Session overview

Session 091 was a six-phase expansion of the CrumpBit Effect on branch
`dev-ph7-voice_expansion`. Base commit `5c9a92f9` (S091 P1 post-implementation).
Target: STM32F765VIH6, Cortex-M7, 216 MHz, `-O2 -flto` (DSP files `-Ofast`).

| Phase | Commit | Summary |
|-------|--------|---------|
| P1 | `5c9a92f9` | CPU reductions (bit-identical) |
| P2 | `080b1507` | Elastic delay, DC blocker, tone, soft knee, SVF output filter |
| P3 | `b0ed5701` | Five sub-types, per-block dispatch, lane maps |
| P4 | `cc8ca99a` | Granular rework (flat-top, density, freeze, steal) |
| P5 | `8a986c65` | Efficiency pass to 170-instruction cap |
| P6 | `155f6806` | Parameter cleanup (labels, dly curve, grn refinements) |

### Final measured costs (instructions per frame, worst case)

| Sub-type | P1 | P2 | P3 | P4 | P5 | P6 |
|----------|-----|------|------|------|------|------|
| dly | 155 | 173.3 | 171.8 | 174.2 | 160.0 | **157.6** |
| cmb | — | — | 167.8 | 170.3 | 153.8 | **153.8** |
| pit | — | — | 181.2 | 183.6 | 153.6→154.1 | **154.1** |
| grn | — | — | 191.5 | 203.0 | 166.5→165.3 | **165.3** |
| rvb | — | — | 192.4 | 194.9 | 163.9 | **163.9** |

### Final image

| Metric | Value |
|--------|-------|
| Flash used | 712,684 B |
| Flash headroom | 40,980 B |
| `.bss` | 413,284 B |
| ITCM | 15,280 B |
| DTCM statics | 4,396 B |
| FXBUF arena | 126,656 B at `0x20001140` |
| `CrumpBitRuntime` | 100 B (56 B shared + 44 B union) |
| `effects_runtime_t` | 100 B |
| Row count | 24 (`CRUMPBIT_PARAM_COUNT`) |
| Host test laws | CB1–CB23, 25 break builds |

---

## §2  P1 — CPU Reductions (bit-identical)

**Plan:** `S091_P1_CrumpBit_Expansion.md`
**Schedule:** `S091_P1_Reductions_Implementation.md`

Four changes to the existing single-engine CrumpBit, all producing bit-identical
output confirmed by CB4 over 82,800 blocks of random input.

### R1 — Binary-search Sync walk

Replaced `crumpBit_rateLimit()` (linear scan of `rateTable[128]`, 101
instructions) with `crumpBit_tableLimit()` (binary search, 63 instructions,
`CRUMPBIT_NOINLINE`). Saves −38 instructions per call; called once per block
when Sync is on.

### R2 — Saturating ADC

`crumpBit_adc()` uses `__USAT(code + 128, 8)` — one ARM instruction replacing
a branch-and-clamp sequence. Saves −4 in stage 1 per frame.

### R3 — Hoisted `io->frames`

`io->frames` loaded to a local variable once at the top of `crumpBit_process()`
instead of dereferencing the pointer each iteration. Minor register pressure
reduction.

### R4 — AC coupling on integer code differences

Stage 1 AC coupling computed on `int16_t` code differences rather than
converting to float first. Cheaper arithmetic path with identical rounding.

### Measured results

- Stage-2 loop: 126 → 111 instructions
- Per-frame: 173 → 155
- Hardware: 3–4 % CPU reduction confirmed
- Flash: 694,156 → 693,504 B (−652 B)
- No RAM change

---

## §3  P2 — CrumpBit Expansion (elastic delay, output filter)

**Plan:** `S091_P2_CrumpBit_Expanded_Plan.md`
**Schedule:** `S091_P2_Implementation.md`

### Signal chain (as built)

1. **Stage 1 — ADC/AC:** 8-bit sample acquisition, crush (`bof`/`biv`), AC
   coupling
2. **Stage 2 — Tape delay loop:** elastic on the whole FxBuffer arena (up to
   126,688 B at the time). Read position glides to target via one-pole
   (`CRUMPBIT_DELAY_GLIDE_K`). Feedback raised to 1.20 (was 0.98).
3. **Return chain:** DC blocker (first-order high-pass, `CRUMPBIT_DC_BLOCK_K`),
   tone tilt (low-shelf/flat/high-shelf via `crumpBit_toneSetup()`), cubic
   soft-knee saturation, feedback multiply
4. **Mix stage:** `out = h + mix * (wet − h)`
5. **Stage 4 — Output filter:** Stereo resonant TPT SVF (LP/HP/BP/UBP/LP2/
   notch/peak/off), 31 instructions per frame per channel. Tables:
   `crumpBit_filterG[128]`, `crumpBit_filterR[128]`, `crumpBit_filterPan[128]`

### Runtime changes

- `CrumpBitRuntime`: 56 → 76 B (+20 B)
- New fields: `ac_y_l`, `ac_y_r` (AC coupling outputs), `svf_l1/l2/r1/r2`
  (SVF integrators), `ac_c_l/ac_c_r` (previous codes), tone/filter/aux params,
  `active_subtype`, ramp origins, `primed`, `drv_reserved`
- `effects_runtime_t`: 56 → 76 B

### Host tests

- CB7: DC blocker and tone tilt sweep
- CB8: elastic delay — arena shrink/grow, position tracking
- CB9: lane map (9 shared + 6 pool for dly)
- 5 break builds

### Measured results

- Stage-2 loop: 126 instructions
- Output filter loop: 31 instructions
- Whole Effect worst: ≈172–176/frame
- Flash: 693,504 → 694,156 B (+652 B)
- Arena: 126,688 B

---

## §4  P3 — Sub-Type Expansions

**Plan:** `S091_P3_CrumpBit_SubType_Expansions.md`
**Schedule:** `S091_P3_Implementation.md`

### Architecture

Five sub-types behind a per-block `switch` in `crumpBit_process()`:

| ID | Key | Engine function | Description |
|----|-----|----------------|-------------|
| 0 | `dly` | `crumpBit_runDelay()` | Tape delay (P2 engine) |
| 1 | `cmb` | `crumpBit_runComb()` | Comb filter / resonator / flanger |
| 2 | `pit` | `crumpBit_runPitch()` | Pitch shifter |
| 3 | `grn` | `crumpBit_runGrain()` | Granular |
| 4 | `rvb` | `crumpBit_runReverb()` | Lo-fi FDN reverb (4 lines) |

Each engine is `static CRUMPBIT_NOINLINE void` — separate ELF symbols for
per-engine instruction gating. Four shared stages: (1) dry ADC/AC, (2) per-
engine, (3) mix, (4) output filter.

### Runtime reorganisation

- Shared part: 56 B (parameters, AC state, SVF state, ramp origins)
- Sub-type union `u` (32 B, `grn` largest):
  - `CrumpBitTapState tap` (dly, cmb): 28 B — `length`, `write_pos`, `valid`,
    `delay` (float), `dc_x/dc_y` (DC blocker), `tone_s` (tilt state)
  - `CrumpBitPitchState pit`: 28 B — `length`, `write_pos`, `valid`, `phase`
    (uint32), `dc_x/dc_y`, `tone_s`
  - `CrumpBitGrainState grn`: 32 B — `write_pos`, `length`, `valid`, `age`,
    `pos_a/pos_b` (16.16 fixed), `inc_a/inc_b` (Q3.12), `dc_x/dc_y`,
    `amp_a/amp_b/pan_a/pan_b`
  - `CrumpBitReverbState rvb`: 28 B — `write_pos`, `length`, `valid`,
    `damp[4]` (float)
- Total: 56 + 32 = 88 B

### Sub-type switching

`crumpBit_enter()` memsets the union, reinterprets write position and valid
count from the previous engine's geometry. `crumpBit_seatActive()` seats per-
engine geometry on arena share changes. `crumpBit_bytePosition()` provides a
format-free write-position/valid read for the transition.

### Lane maps

9 shared lanes identical in every map: 0 Morph, 1 bof, 2 biv, 3 mix, 7 vol,
8 pan, 11 typ, 12 frq, 13 res.

7-lane pool per sub-type:

| Lane | dly | cmb | pit | grn | rvb |
|------|-----|-----|-----|-----|-----|
| 4 | fbk | fbk | fbk | st2 | fbk |
| 5 | rte | rte | rte | st3 | rte |
| 6 | snc | snc | snc | snc(frz) | snc(frz) |
| 9 | dpn | dpn | dpn | dpn(pos) | dpn(wid) |
| 10 | ton | ton | ton | ton(sze) | ton(dmp) |
| 14 | — | — | aux(int) | aux(st1) | — |
| 15 | — | — | — | spr | — |

`sub` is Pattern-automatable only, never LFO, never Morph, never an FX lane.
Lane 3 (mix) is never freeable. `dly` map equals the P2 table byte-for-byte.

### New rows (15–21)

| Index | Key | Label | Type | Default | Max |
|-------|-----|-------|------|---------|-----|
| 15 | `crump_aux` | aux | DTYPE_PM63 | 63 | 127 |
| 16 | `crump_grain_st2` | st2 | DTYPE_PM63 | 63 | 127 |
| 17 | `crump_grain_st3` | st3 | DTYPE_PM63 | 63 | 127 |
| 18 | `crump_grain_spread` | spr | 0..127 | 0 | 127 |
| 19 | `crump_grain_density` | den | 0..127 | 127 | 127 |
| 20 | `crump_grain_amp` | amp | 0..127 | 0 | 127 |
| 21 | `crump_grain_width` | wid | 0..127 | 0 | 127 |

### Per-sub-type row names (via `name3` hook)

- cmb: `not` (rte), `pol` (snc)
- pit: `win` (rte), `snp` (snc), `int` (aux)
- grn: `div` (rte), `frz` (snc), `pos` (dpn), `sze` (ton), `st1` (aux)
- rvb: `dcy` (fbk), `siz` (rte), `frz` (snc), `wid` (dpn), `dmp` (ton)

### Per-sub-type Rate value text

- dly: division name with Sync on, else raw number
- cmb: MIDI note name (`crumpBit_combNote`: 112 − min(rte, 96))
- pit: window in ms (10 × 20^(rte/127))
- grn: step division (always snapped to stops)
- rvb: raw number

### New tables (CrumpBitTables.c)

| Table | Entries | Size | Law |
|-------|---------|------|-----|
| `crumpBit_combTable[128]` | 128 | 512 B | fs/f(112−min(n,96)) |
| `crumpBit_pitchWindow[128]` | 128 | 512 B | 10ms × 20^(r/127) × fs |
| `crumpBit_semitoneRatio[49]` | 49 | 196 B | 2^((s−24)/12) |
| `crumpBit_grainFraction[65]` | 65 | 260 B | 2^(−5(1−d/64)) |
| `crumpBit_reverbSize[128]` | 128 | 512 B | 0.25 × 10^(r/127) |
| `crumpBit_reverbDecay[128]` | 128 | 512 B | −3log2(10)/(T×fs) |

### EffectsManager changes

- `effect_type_ops_t`: `effective_max` now takes `const void *rt` (live
  runtime). New fields: `lane_map` function pointer, `lane_variant_row`,
  `lane_variant_count`.
- `effect_ui_hooks`: new `name3`, `layout`, `format_target3` members
- `effects_laneTable()`, `effects_formatTargetValue3()` new API
- `effects_laneFileKey()`, `effects_laneByFileKey()`, `effects_laneOfParam()`
  now take `record` instead of `type`
- Self-check code 11: variant-0 map must equal registry `lanes[]`
- BUFFER_DEPENDENT re-apply: `rte` written every block (M16)

### Host tests added

- CB10: mix-stage binding (bit-exact dry at mix 0 for every sub-type)
- CB11: switch storm (30 s, random sub changes, state invariants, guard band)
- CB12: cmb tuning (first echo at combTable[rte] ±2 samples)
- CB13: pit ratio (441 Hz → 882/220.5 Hz energy shift, ≥10×)
- CB14: grn candidates, width, freeze, stability
- CB15: rvb extreme combinations, RT60, freeze
- 9 break builds (breaks 1–9)

### Measured results

| Sub-type | Loop | Whole Effect worst/frame |
|----------|------|--------------------------|
| dly | 124 | 171.8 |
| cmb | 124 | 167.8 |
| pit | 135 | 181.2 |
| grn | 131/125 | 191.5 |
| rvb | 145 | 192.4 |

- Flash: 694,156 → 707,020 B (+12,864 B)
- Runtime: 76 → 88 B
- Arena retuned: 126,688 → 126,656 B (lost one 32 B granule at runtime 88 B)

---

## §5  P4 — Granular Rework

**Plan:** `S091_P4_GRN_TWEAKS.md`
**Schedule:** `S091_P4_IMPLEMENTATION.md`

### Grain state redesign (CrumpBitGrainState: 32 → 44 B)

Old (P3): `write_pos`, `length`, `valid`, `age`, `pos_a/b`, `inc_a/b`,
`dc_x/dc_y`, `amp_a/b`, `pan_a/b` = 32 B.

New: `pos_a/b` (uint32, 16.16), `countdown` (uint32, frames to next trigger),
`dc_x/dc_y` (float, DC blocker), `inc_a/b` (int16, Q3.12), `age_a/b` (uint16,
per-voice), `end_a/b` (uint16, voice duration = L + 128), `write_pos`,
`length`, `valid`, `anchor` (uint16, freeze head, `0xFFFF` = not frozen),
`pan_a/b` (uint8), `pending` (uint8, stolen voice deferred trigger), `spare`
(uint8) = 44 B.

Removed: shared `age`, `amp_a/b`. Added: per-voice `age_a/b`, `end_a/b`,
`countdown`, `anchor`, `pending`.

### Runtime: 88 → 100 B

- Shared part: 56 B unchanged
- Union: 32 → 44 B (grn largest)
- `grain_amp` (uint8) → `grain_length` (uint8, row 20)
- `pad` (uint8) → `filter_bal` (uint8, row 22, shared balance)
- Row count: 22 → 23

### Grain engine replacement (474 → 647 lines)

**Candidate picking (`crumpBit_grainPick`):**
- 5 uniforms per block from one hardware RNG word via xorshift32
- Position: 63 = LAG0 (2 frames behind write head); above 63 = rhythmic stop ×
  per_tick; below 63 = uniform scatter
- Length: exponential 8 octaves (was 5), magnitude from `grainFraction[]`
  (retuned to 2^(−8(1−m/64))), minimum 220 frames (~5 ms)
- Density: 0 = off; 64 = one per buffer; positive = rhythmic stops (longest
  first); negative = random mean
- Width: 0 = mono; 1–64 = bipolar weighted; 65–127 = random uniform spread
- Pitch: P1/P2/P3 absolute semitones via `crumpBit_rowSemitones()`, spread-
  weighted selection (1 : a : b), detune ±1 st at spr 127

**Flat-top windows (128-frame fade):**
- `w = clamp(min(in, out), 0, 1); in += 1/128; out -= 1/128`
- 128-frame linear fade-in and fade-out with a flat sustain between

**Voice stealing:**
- If both voices busy and no steal running: steal older voice
- Stolen voice gets `end = age + 128` (128-frame fade-out)
- New voice deferred via `pending` (starts after the steal's fade)

**Overwrite clamp:**
- No read past recorded audio; clamped per `gr` (growth rate)
- Frozen full line wraps freely

**Freeze with anchor:**
- Anchor set to `write_pos` on first frozen block
- Released on unfreeze (set to `0xFFFF`)
- Unfreeze before line full: `valid` restarted at 0
- Frozen Position-0 pick starts at `anchor − 2`
- Record write redirected to delay line slot

### Delay time curve

- `crumpBit_grainTime[128]`: 6–384 ticks in 96-PPQ, continuous log with 16
  pinned stops (/64 through 1br)
- `crumpBit_grainStopCc[16]`: stop CC positions
- `crumpBit_grainDensity[64]`: mean interval fractions

### Balance (`bal`) added to shared output filter

- `filter_bal` row (index 22): DTYPE_PM63, default 63 (centre)
- `tilt = filter_bal − 63`; left cutoff = `frq − tilt`, right = `frq + tilt`
- Per-channel SVF coefficients via `crumpBit_filterCoeffs()`
- Bit-identical to P2 at centre (both indices = frq)
- Stage 4 loop stays 31 instructions (coefficients set before loop)

### Lane map changes (grn)

Lane 4: `den` (was `st2`), lane 5: `rte` (delay time), lane 10: `lng` (was
`ton`). Lane-less in grn: P2, P3, width, feedback. `bal` lane-less everywhere.

### Host tests

- CB16 (new): balance law — A(frq, bal≠63) vs B(frq−tilt, bal=63) per-channel
  bit-exact
- CB14 rewritten: pitch slots, width, position, read safety, copy identity,
  density, freeze, stability (8 sub-laws)
- 12 break builds (breaks 1–12)

### Measured results

- grn worst: ≈201/frame (exceeded 170 cap, triggering P5)
- dly/cmb/pit/rvb: +0.7/frame each (bal coefficients)
- Runtime: 88 → 100 B
- Arena: 126,656 B (unchanged; N1 retune in P3 already assumed this)
- Flash: 707,020 → 709,860 B (+2,840 B)

---

## §6  P5 — Efficiency Pass (170-instruction cap)

**Plan:** `S091_P5_SUBTYPE_EFFICIENCIES.md`
**Schedule:** `S091_P5_IMPLEMENTATION.md`

### B — Integer Q14 tap read (dly/cmb)

Replaced float tape read (`crumpBit_lineRead()`, removed) with integer Q14
arithmetic. New helpers: `crumpBit_readQ14()` (Q14 position, integer wrap,
linear interpolation in Q14 code units), `crumpBit_floatToQ14()` (one
`VCVT.S32.F32 #14`), `crumpBit_q21ToFloat()` (one `VCVT.F32.S32 #21`).
Gate mask: `d14 + 2×2^14 ≤ valid << 14` (integer compare). Saves ~12
instructions per frame in the read path; loop 124 → 111. Same linear
interpolation to 1/16,384 sample resolution.

### P1 — Pitch head read without interpolation

`crumpBit_headRead()` reads `loop[rp >> 14] << 14` — integer part only, no
second sample, no interpolation multiply-add. Position computed as
`rp = wpg − (phase × w14) >> 32` (one UMULL). `len` parameter removed from
signature. Loop 135 → 106. Grittier, more aliased sound at large intervals.

### R1 — rvb damping low-pass only

Damping changed from tilt (LP/flat/HP) to low-pass only: `tone_k =
crumpBit_toneK[min(ton, 63)]` with `toneK[63] = 1.0` (flat at centre and
above, shown as `0`). Removed `tone_a`, `tone_b`, tilt outputs. Loop 145 → 131.

### S1 — Block-rate gain stepping (feedback/pan)

`fbk` and `dpn` gains applied as one value per block (0.7 ms steps at
44.1 kHz/32 frames). `mix` keeps its per-frame ramp (user decision). Removed
`feedback_prev` and `pan_prev` from runtime (2 bytes freed, padding absorbs).

### X2 — bof/biv 256-byte lookup table

`crumpBit_dataLines[256]` maps code `c` to `(c & keep) ^ inv`. Built by
`crumpBit_buildDataLines()` on `bof`/`biv` change only (key check per block).
Stage 1 reads one LDRB per channel instead of per-sample AND/XOR. +256 B `.bss`
(SRAM1) + 4 B key. Frees a register in every engine loop.

### G3 — Grain window without lower clamp

Removed `wa < 0 ? 0 : wa` and same for `wb`. Safety: every grain end lands on
a 32-frame block boundary via rounding. P4 age defect fixed (ages restored to
block-start values on dropped triggers). Static assert: `CRUMPBIT_GRAIN_MIN_PERIOD`
is power of two.

### G4 — Plain grain echo (gain 0..0.98)

Echo write: `adc(0.5*(grains_l + grains_r) + fb * tap)` — no DC blocker, no
soft knee. Removed `dc_x/dc_y` from grain state (replaced by `fit_key` and
`reserved` for G6). `CRUMPBIT_GRAIN_FB_MAX` = 0.98f. Loop cost dropped
significantly (136 → 116 with other changes).

### G6 — Position fit cached

`fit_key` (uint32, `BPM | N << 16`) and `fit` (uint8) cached in the freed
grain-state bytes. Cache miss only on tempo or line-length change. Saves −1.5
per frame on hit.

### A — Position clamp moved into engine

`dpn` row loses `BUFFER_DEPENDENT`; `crumpBit_effectiveMax()` loses `dpn` case.
Engine clamps Position using cached `fit`. Helpers: `crumpBit_grainStopFit()`,
`crumpBit_grainMaxTicks()`.

### F1 — One-pole output filter for grn/rvb

Stage 4 is a one-pole `s += k(x − s), out = cx*x + cl*s` for grn/rvb instead
of the resonant TPT SVF. New table `crumpBit_onePoleK[128]` (512 B flash): k
per `frq` matched so −3 dB point equals SVF's at `res` 0. Mix table
`crumpBit_onePoleMixTable[8][2]`: (cx, cl) per filter type (LP→(0,1),
HP→(1,−1), peak/off→(1,0)). Loop: 31 → 16 instructions. `process` per block:
249 → 204 (one-pole branch). Hand-over on sub-type switch: SVF→one-pole copies
`svf_l2→svf_l1`, `svf_r2→svf_r1`; reverse adds zeros. `res` shows `---` for
grn/rvb.

### Follow-up — Pitch rows ±36 st and window glide

All four pitch rows (`aux`, `st2`, `st3`, `fin`) changed to ±36 st (one
semitone per step, stored 27..99, centre 63). New `EFFECT_MOD_RANGE(lo, hi)`
macro and `effects_descriptorMin()` inline. Pitch window glide: 20 ms one-pole
per block (`CRUMPBIT_PITCH_WIN_GLIDE` = 0.03563), ramped linearly in Q14.
State: `CrumpBitPitchState.window` (28 → 32 B, union stays 44 B, runtime
stays 100 B). `snp` in pit shows `---` (inert).

### Follow-up — Fine pitch control (`fin` row)

New row 23 `crump_pit_fine` (DTYPE_PM63, 0..127, centre 63, shows −63..+64).
Uses `midiPitch_detune()` (voice law, ≈±52 cents). DSP: `dphi` multiplied by
`crumpBit_ratioOf(semis) * midiPitch_detune(pit_fine)`. Page: SELECT 2 screen 3
`fbk fin dpn ton`. FX lane 6 in pit (was `snc`, dead in pit). `pit_fine` byte
takes one of 2 padding bytes freed by S1. Row count: 23 → 24.

### New host test laws

- CB17: data-line table correctness
- CB18: grain echo feedback gain and peak stability
- CB19: one-pole −3 dB law, type mapping, res inert, hand-over
- CB20: (from existing, renumbered)
- CB21: pitch ±36 st, fine pitch
- CB22: band centre ratio law

### New tools

- `elf_blockpath.py`: longest entry-to-exit path with frame loops weighted 0
- `crumpbit_cpu.py`: 170-cap gate, requires exactly 2 frame loops in
  `crumpBit_process()`

### Measured results (after all follow-ups)

| Sub-type | Loop | Per block | Process | Whole Effect/frame |
|----------|------|-----------|---------|--------------------|
| dly | 111 | 129+113 Sync | 249 (SVF) | 160.0 |
| cmb | 109 | 105 | 249 (SVF) | 153.8 |
| pit | 106 | 195 | 249 (SVF) | 153.6→154.1 |
| grn | 116 | 814 | 204 (1-pole) | 166.5 |
| rvb | 131 | 252 | 204 (1-pole) | 163.9 |

- Flash: 709,860 → 710,876 B (after all follow-ups)
- `.bss`: +268 B (table 256 + key 4 + 8 B align)
- 22 host laws, 21 break builds

---

## §7  P6 — Parameter Cleanup

**Plan:** `S091_P6_PARAM_CLEANUP.md`

### Labels hook replaces `name3`

New `effect_ui_hooks.labels` function pointer replaces `name3`. The labels hook
receives the current sub-type and returns a pointer to a static table of
per-sub-type row labels (3-character strings or NULL for default). Menu
rendering calls `effects_labelsHook()` instead of `effects_name3Hook()`.
`name3` removed from `effect_ui_hooks`. Files changed: `EffectsManager.h`,
`EffectsManager.c`, `menuEffects.c`, `menuEffects.h`, `menu.c`,
`CrumpBitParameters.c`.

### `dpn` → `spn` ("SubPan")

The delay-pan parameter key renamed from `dpn` to `spn` with display name
"SubPan". Applies to all sub-types. No backward compatibility (standing rule).

### `snc` removed from dly lanes

`snc` (Sync) removed from the `dly` sub-type lane pool. The Sync row is still
functional but no longer occupies an FX lane in `dly`.

### dly delay time curve

New 128-step delay time table `crumpBit_dlyTime[128]` with tempo-relative
curve and 13 pinned stops. Replaces the old Sync binary-search path and
`rateLimit` call for `dly`. The `rte` row in `dly` now uses this table directly
(value → samples = `dlyTime[rte] × per_tick`). Stop positions and sample counts
vary with BPM. Generator: `gen_crumpbit_tables.py`.

### pit interval labelling

The `aux` row in `pit` labelled "pit" / "Shift" / "SemiPtch" for the page
header, row name, and automation display.

### grn below /64 rows

4 row positions below the lowest stop (/64) in the Position/Density row,
showing their row number instead of a stop name. Provides finer control at the
lowest granular time divisions.

### Tap fade (grn)

One-block linear fade on time-parameter changes in grn. State: `tap_d` (current
delay in samples), `tap_on` (fade gate, 0 or 1). When `rte` changes and the
new delay differs from `tap_d`, the gate opens for one block, linearly
cross-fading from the old delay position to the new one. Prevents clicks on
delay-time changes.

### New host test law

- CB23: tap fade — delay change produces no click above threshold, fade
  completes within one block
- Breaks 23–25
- `runGrain` ELF gate: 114

### Measured results

- dly: 157.6/frame (Sync search eliminated)
- cmb: 153.8 (unchanged)
- pit: 154.1 (unchanged)
- grn: 165.3 (tap fade cost offset by grain loop 116→114)
- rvb: 163.9 (unchanged)
- Flash: 710,876 → 712,684 B (+1,808 B)
- Headroom: 40,980 B
- RAM: unchanged from P5

---

## §8  Files changed (complete list across all phases)

### Core DSP

| File | Phases | Summary |
|------|--------|---------|
| `Core/DSP/Effects/CrumpBit/CrumpBitEffect.h` | P2–P6 | Runtime structs, state unions, constants, inline helpers |
| `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c` | P1–P6 | Five engine functions, Q14 read, one-pole filter, tap fade, all efficiency changes |
| `Core/DSP/Effects/CrumpBit/CrumpBitParameters.h` | P3–P6 | Row enums, masks, lane maps, labels hook |
| `Core/DSP/Effects/CrumpBit/CrumpBitParameters.c` | P3–P6 | Descriptors, write_param, value text, layouts, lane tables |
| `Core/DSP/Effects/CrumpBit/CrumpBitTables.h` | P3–P6 | Table externs and constants |
| `Core/DSP/Effects/CrumpBit/CrumpBitTables.c` | P3–P6 | Generated tables (all) |
| `Core/DSP/Effects/EffectsManager.h` | P3, P5, P6 | ops_t signatures, hooks struct, API |
| `Core/DSP/Effects/EffectsManager.c` | P3–P6 | Runtime guard, lane vtable, labels hook, self-check 11 |
| `Core/DSP/Effects/StereoFilterEffect.c` | P3 | NULL fields for new ops (no behaviour change) |

### Menu

| File | Phases | Summary |
|------|--------|---------|
| `Core/Menu/menu.c` | P3, P6 | Labels in Effect row rendering |
| `Core/Menu/menuEffects.c` | P3, P4, P6 | Layouts, value text, page orders, labels hook display |
| `Core/Menu/menuEffects.h` | P6 | Labels hook prototype |

### Storage

| File | Phases | Summary |
|------|--------|---------|
| `Core/Hardware/SD/storageTypes.c` | P3 | Lane keys take record, grn lanes saved |

### Tools

| File | Phases | Summary |
|------|--------|---------|
| `tools/plots/gen_crumpbit_tables.py` | P3–P6 | All table generators and checks |
| `tools/dsp_test/elf_looppath.py` | P3–P5 | --cond-heads, --in-range, cycle-pruned search, --rank |
| `tools/dsp_test/elf_blockpath.py` | P5 (new) | Per-block longest path |
| `tools/dsp_test/crumpbit_cpu.py` | P5 (new) | 170-cap gate |
| `tools/dsp_test/Makefile` | P3–P6 | Break builds, ELF gates, CPU gate |
| `tools/dsp_test/modules/crumpbit.c` | P3–P6 | Host laws CB1–CB23, 25 breaks |

---

## §9  Host test summary

### Laws (CB1–CB23)

| Law | Added | What |
|-----|-------|------|
| CB1–CB3 | pre-S091 | Existing laws |
| CB4 | P1 (updated P5) | Bit-exact reference: P2 + Q14 + block-rate fb/balance, mix ramp |
| CB5 | P1 (updated P3–P5) | Default state validation |
| CB6 | P1 (extended P3–P5) | Table laws: all tables, effective_max, fit, one-pole k |
| CB7 | P2 | DC blocker and tone tilt sweep |
| CB8 | P2 | Elastic delay: arena shrink/grow, position tracking |
| CB9 | P2 (extended P3) | Lane map completeness: masks disjoint, shared lanes, pool lanes |
| CB10 | P3 | Mix-stage binding: bit-exact dry at mix 0 for every sub-type |
| CB11 | P3 (extended P4–P5) | Switch storm: random sub changes, state invariants, seat law |
| CB12 | P3 | cmb tuning: first echo at combTable[rte] ±2 |
| CB13 | P3 (changed P5) | pit ratio: band energies and crossfade sidebands |
| CB14 | P3 (rewritten P4, extended P5) | grn: pitch, width, position, read safety, copy identity, density, freeze, stability, block-aligned ends |
| CB15 | P3 (extended P5) | rvb: extreme combos, RT60, freeze, ton flat above 63 |
| CB16 | P4 (extended P5) | Balance: per-channel bit-exact on SVF (dly) and one-pole (rvb) |
| CB17 | P5 | Data-line table (X2) |
| CB18 | P5 | Grain echo feedback gain and peak stability (G4) |
| CB19 | P5 | One-pole −3 dB law, type mapping, res inert, hand-over (F1) |
| CB20 | P5 | (Existing, renumbered) |
| CB21 | P5 | Pitch ±36 st and fine pitch |
| CB22 | P5 | Band centre ratio law |
| CB23 | P6 | Tap fade: no click, fade within one block |

### Break builds (25 total)

Breaks 1–9 (P3), 10–12 (P4), 13–18 (P5), 19–22 (P5 follow-ups), 23–25 (P6).
Each break targets a specific law and fails it by sed-injecting a one-line
defect into the source.

---

## §10  Binding rules established or confirmed

1. **170-instruction/frame cap**: every CrumpBit sub-type gated by
   `crumpbit_cpu.py --cap 170` in the Makefile
2. **CrumpBitRuntime 100 B**: +24 B of the approved +32 B over 76 B used;
   8 B remain
3. **sub is Pattern-only**: never LFO, never Morph, never an FX lane (values
   above 4 clamp to rvb)
4. **mix is shared**: always lane 3, never freeable, same law every sub-type
5. **No backward compatibility**: older grn Scene files loaded by newer firmware
   map to new lanes; no migration
6. **BUFFER_DEPENDENT**: `rte` written every block; `dpn` no longer flagged
   (clamp moved into grn engine)
7. **32-frame block assumption**: G3's block-aligned grain ends require
   `OUTPUT_DMA_SIZE` = 32
8. **X2 rebuild outside cap**: data-line table rebuild (~48/frame for one
   block) is not counted against the 170 cap
9. **One-pole for grn/rvb**: resonance inert, `res` shows `---`
10. **Labels hook**: replaces `name3` for per-sub-type row names
