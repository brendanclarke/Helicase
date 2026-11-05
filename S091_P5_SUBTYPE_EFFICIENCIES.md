# S091 P5 — CrumpBit Sub-Type Efficiencies

**Status:** plan, every open question decided by the user (2026-11-05, §10),
ready for an implementation schedule.
**Base:** the S091 P4 working tree on `dev-ph7-voice_expansion` (P4
implemented, uncommitted at the time of writing; HEAD `cc8ca99a`).
**Goal:** every CrumpBit sub-type at or below **170 instructions per frame**
(whole Effect, worst case), measured on the linked image.

---

## 1. Why

- The user's worst-case Scene (FluxMax, CrumpBit in `dly`) crashed on the
  S091 P4 image from DSP overload.
- On S091 P3 the same Scene read 100 on the `cpu` widget, right on the limit.
  P4 added about 1.5 per frame to `dly`, enough to tip it.
- The voice DSP and the 86 mixer loops are identical between P3 and P4, so
  the fix lives in CrumpBit.
- The user set the cap at **170 per frame** for the whole Effect in every
  sub-type, a little below P3's `dly` (172.5).

One instruction per frame is about 0.02 % of the CPU (≈ 4,900 cycles per frame
at 216 MHz / 44.1 kHz).

---

## 2. Result

### 2.1 Whole CrumpBit Effect, worst case, instructions per frame

Every column is a measured build.

| Sub-type | P3 (`b0ed5701`) | P4 as built | + A, B | + P1 R1 G1 G3 S1 | **P5 final** | Cap 170 |
|---|---:|---:|---:|---:|---:|---|
| `dly` | 172.5 | 174.2 | 163.0 | 158.8 | **158.3** | ✓ |
| `cmb` | 168.6 | 170.3 | 159.1 | 153.9 | **152.2** | ✓ |
| `pit` | 181.9 | 183.6 | 182.6 | 154.4 | **151.6** | ✓ |
| `grn` | 192.3 | 203.0 | 201.2 | 195.9 | **167.5** | ✓ |
| `rvb` | 193.2 | 194.9 | 193.9 | 180.4 | **163.1** | ✓ |

"P5 final" is the combined prototype (scratch build `ab6`):
- every change accepted at the time: §4, plus G1, G7 and a stepped `mix`,
  which the decisions later dropped (§2.1a, §10);
- the resonant output filter for `dly`/`cmb`/`pit`;
- the one-pole output filter for `grn`/`rvb`;
- R3 and X3 left out (§5).

### 2.1a With the decisions (projected, not yet measured)

The `ab6` prototype differs from the decided plan in three ways:
- **Q9:** the `mix` ramp stays per frame. Measured about +1 per frame when it
  was put back.
- **Q1:** G1 is dropped: `grn` +0.3.
- **Q6:** the one-pole `k` comes from a table, not two divides per block.
  Expected to be neutral or slightly lower; not counted.

| Sub-type | P5 final (`ab6`) | Projected | Margin to 170 |
|---|---:|---:|---:|
| `dly` | 158.3 | ≈ 159.3 | 10.7 |
| `cmb` | 152.2 | ≈ 153.2 | 16.8 |
| `pit` | 151.6 | ≈ 152.6 | 17.4 |
| `grn` | 167.5 | ≈ 168.8 | 1.2 |
| `rvb` | 163.1 | ≈ 164.1 | 5.9 |

`grn` is the tight one. If G6 is dropped after its isolated measurement (Q3),
`grn` gives back less than 0.5, so at most about 169.3. The CPU gate (§8.4)
decides; the implemented tree must be measured.

### 2.2 P5 final, by component

| | `dly` | `cmb` | `pit` | `grn` | `rvb` |
|---|---:|---:|---:|---:|---:|
| Engine frame loop | 109 | 107 | 104 | 118 | 130 |
| Output filter loop | 31 (SVF) | 31 (SVF) | 31 (SVF) | **16 (one-pole)** | **16 (one-pole)** |
| Engine per block | 154 + 113 Sync search | 136 | 215 | 779 | 255 |
| `process` per block (filter path) | 231 | 231 | 231 | 207 | 207 |
| Manager clamps per block (`effectiveMax` 23 + `tableLimit` 63) | 86 | 86 | 86 | 86 | 86 |
| **Total per frame** | **158.3** | **152.2** | **151.6** | **167.5** | **163.1** |

Total = engine loop + filter loop + (engine per block + `process` per block +
manager) / 32.

### 2.3 Image (prototype `ab6` against the P4 working tree)

| | P4 | P5 prototype | Change |
|---|---:|---:|---:|
| text | 709,440 | 709,408 | −32 |
| data | 420 | 428 | +8 |
| bss | 413,016 | 413,288 | +272 (the X2 table, 256 B, plus prototype statics) |
| ITCM | 15,328 | 15,280 | −48 |
| DTCM statics | 4,392 | 4,396 | +4 |
| FXBUF | 126,656 at `0x20001140` | unchanged | 0 |
| Flash used | 709,860 | 709,836 | −24 |

The prototype keeps a few values in file statics (§4, implementation notes).
The implementation moves them into the runtime or `.bss` deliberately (§8).

---

## 3. Measurement method

**Instructions, not cycles.**
- Counts are instructions on the longest path. A `VDIV` counts as 1 here, but
  costs about 14 cycles.
- None of the CrumpBit frame loops contain a `VDIV`. Every per-block divide
  sits in the per-block numbers.

**Tools:**

| Tool | What it counts | Status |
|---|---|---|
| `tools/dsp_test/elf_looppath.py --sym F` (with `--cond-heads --in-range` for `crumpBit_runGrain`) | frame loop, longest path | in repo |
| `blockpath.py` | per-block longest path through a function, loops weighted 0, calls listed | scratch script, to be added to the repo (§8.4) |
| `throughpath.py` | per-block longest path forced through given addresses | scratch script, to be added (§8.4) |

`throughpath.py` exists because the longest path through `process` can
otherwise combine the SVF coefficient code with the one-pole loop. Both
branches test the same flag, but the path counter doesn't know that.

**Rules learnt in this session:**
- **Estimates from source were unreliable.** These loops are bound by register
  pressure on the 32 single-precision registers and the integer registers.
  Removing work only pays if it frees registers. Several estimates were wrong
  by 3–25 in either direction (§5).
- **Only linked-image measurements count.**
- **Measure the symbol that actually runs.** LTO clones engines
  (`crumpBit_runPitch.constprop.0`) and can outline the shared tape loop
  (`crumpBit_tapLoop.isra.0`) when its body grows.

### 3.1 Hardware cycle test (V4)

Measured on hardware, FluxMax Scene, Cymbal and Hi-Hat on the WFd chain (1,380
blocks per 0.5 s). Values are cycles per sample of the voice post-chain loop,
with timing overhead removed.

| | Divide (production) | Reciprocal table, flash | DTCM | ITCM |
|---|---:|---:|---:|---:|
| Per-block minimum | 47.5 | 57.8 | 57.8 | 57.8 |
| Average | 58.3–59.5 | 58.1–58.2 | 59.6 | 58.0 |

The M7 overlaps `VDIV` with other work, so replacing it gives nothing. V4 is
rejected (§5).

---

## 4. Accepted changes

Each entry gives:
- **What** the change is;
- **Measured** effect;
- **Sound / behaviour** change;
- **Implementation notes**, including any prototype shortcut that must be done
  properly;
- **Host laws** affected.

### A — the `grn` Position clamp moves into the engine

- **What:**
  - `dpn` (row 10) loses `EFFECT_PARAM_FLAG_BUFFER_DEPENDENT`.
  - `crumpBit_effectiveMax()` no longer handles `dpn`.
  - `crumpBit_grainPick()` limits the rhythmic Position stop to the last stop
    whose lag fits the line, `len − CRUMPBIT_LOOP_GUARD`, at the current tempo.
- **Measured:**
  - Manager per block 3.7 → 2.7 per frame for every sub-type.
  - `grn` engine per block +55 (+1.7 per frame) before G6.
- **Sound / behaviour:**
  - The same Position as P4's manager clamp.
  - A stop too long for the line now plays the longest stop that fits, instead
    of waiting. That's the P4 page behaviour, now also for LFO and Pattern
    values.
- **Implementation:**
  - The Position label code in `CrumpBitParameters.c` keeps calling
    `crumpBit_grainPosLimit()`, so the page shows the clamped stop. No label
    change (Q11).
  - With G6 the fit search runs once per change, not per block.
- **Host laws:**
  - **CB6:** the `grn` Position limit law becomes "`effective_max(dpn)` = 255
    in every sub-type, and the engine clamp equals `crumpBit_grainPosLimit()`".
  - **CB14 (c):** a stop longer than the line plays the last fitting stop.

### B — integer (Q14) tap read for `dly` and `cmb`

- **What:** `crumpBit_tapLoop()` reads the loop as `pit` does.
  - The glided delay is converted once per frame to Q14 (`vcvt.s32.f32 #14`).
  - `rp = (wp << 14) − d14` is wrapped by the sign bit.
  - `crumpBit_readQ14()` interpolates.
  - One scaling `vcvt.f32.s32 #21` converts the result back to float.
  - The `valid` gate becomes an integer compare, `d14 + 2·2¹⁴ ≤ valid << 14`,
    applied as a mask.
- **Measured:** loop 124 → 114 (−10); per block −6.
- **Sound:**
  - The same linear interpolation. Position resolution is 1/16,384 of a sample,
    finer than float at long delays.
  - Over 60 s of random host input against P2, without S1: output difference
    RMS ≈ 0.05 of an 8-bit step, maximum 1.9 steps. 1.9 % of loop bytes differ
    by a rounding flip.
- **Implementation:**
  - `crumpBit_readQ14()` moves above `crumpBit_tapLoop()`.
  - Add `crumpBit_floatToQ14()` (Q14) and `crumpBit_q21ToFloat()` as
    `crumpBit_q20ToFloat()` is written: inline asm on ARM, C on the host.
  - `crumpBit_lineRead()` loses its tape callers.
- **Host laws:** **CB4** is no longer bit-exact to the P2 reference. Its
  reference model is replaced (Q8).

### P1 — `pit` heads read without interpolation

- **What:** `crumpBit_headRead()` returns `loop[rp >> 14] << 14`, after the
  sign-bit wrap. There's no second sample and no `i1` wrap.
- **Measured:** P1 and S1 together: loop 135 → 107, per block 228 → 221
  (−28.2 per frame). The isolated P1 share wasn't measured; S1 is about −4.
- **Sound:** grittier, more aliased shifting, mostly at large intervals. `grn`
  already reads this way.
- **Host laws:** **CB13**, the `pit` ratio, still holds (the frequency is
  unchanged). Any `pit` bit-exact expectation needs a new reference.

### R1 — `rvb` damping is low-pass only

- **What:** the per-line `ton` tilt becomes the one-pole low-pass state alone,
  `z_k = s_k`.
  - `k = crumpBit_toneK[min(ton, 63)]`.
  - `crumpBit_toneK[63] = 1.0`, so 63 is an exact pass-through.
  - The `ton` values above 63 (the high-pass side) play as flat in `rvb`.
- **Measured:** R1 and S1 together: loop 145 → 132, per block 269 → 255
  (−13.4 per frame).
- **Sound:** `ton` darkens the tail when below centre. Above centre it does
  nothing in `rvb`.
- **Implementation:** in `rvb` the `ton` label shows values above 63 as
  flat (`0`); the stored value is kept and the row is not limited (Q10).
- **Host laws:** **CB15** decay and freeze measured unchanged in the prototype
  (15.35 dB / 2.04 dB) at flat. Add an R1 law: values above 63 equal 63
  bit for bit.

### G3 — grain window without the lower clamp

- **What:**
  - Grains end on a block boundary: the span is rounded down so that the event
    frame plus the span lands on a block end.
  - An idle voice's `out` ramp is parked at 0 with slope 0 by the per-block
    setup.
  - `w = min(min(in, out), 1)` can then never go below 0, so the
    `w < 0 → 0` clamp per voice goes.
  - Steal fade-outs already end on the event split.
- **Measured:** G1, G3 and S1 together: `grn` loop 136 → 131. (G1 is
  dropped, §5; it is a per-block change and doesn't touch the loop.)
- **Prototype shortcut:**
  - The prototype only removed the clamp; the measurement doesn't depend on the
    rest.
  - Without the block-aligned end and the idle park, CB14's copy identity
    fails (191,744 frames off). The full form is required.
- **Sound:** a grain is up to 31 frames (0.7 ms) shorter. The fades are
  unchanged.
- **Host laws:** **CB14** copy identity, fades and read safety must pass
  unchanged.

### S1 — feedback and pan ramps step once per block (`mix` keeps its ramp)

- **What:** `fbk` (gain) and the `dpn` balance gains take their target at the
  block start instead of ramping per frame.
  - In `dly`/`cmb`/`pit`: `fb`, `gain_l`, `gain_r`.
  - In `grn`: `fb`.
  - In `rvb`: nothing (its only S1 target was `mix`).
  - **`mix` keeps its per-frame ramp in every sub-type (Q9, user).**
  - The output filter's type crossfade keeps its per-frame ramp.
- **Measured (prototype, `mix` also stepped):** `dly` −4.2, `cmb` −5.2. It is
  also part of the P1, R1 and G3 figures. Putting the `mix` ramp back was
  measured at about +1 per frame (§2.1a).
- **Sound:** 0.7 ms steps instead of ramps on `fbk` and `dpn`. Possible zipper
  on fast moves (LFO or knob), judged on hardware (HW3). `mix`, the most
  exposed one, stays smooth.
- **Host laws:**
  - **CB4** differs from P2 by up to full scale on random per-block `fbk` and
    `dpn` changes. That's the step itself; the new reference steps them too
    (Q8).
  - **CB10** (mix stage bit-exact) is unchanged: the `mix` ramp is the P4 one.

### X2 — `bof`/`biv` through a 256-byte table

- **What:**
  - Stage 1's `(adc & keep) ^ invert` becomes `lut[adc]`, one `LDRB` per
    channel.
  - The table is rebuilt only when `bit_off` or `bit_invert` changes. Neither
    is Morph or LFO driven; FX lanes change them per step.
- **Measured:**
  - In the loops: `dly` −1, `cmb` −2, `pit` −3, `rvb` −2. Part of `grn`'s
    131 → 118.
  - The key check costs about +0.3 per block.
  - A change costs **one block about +48 per frame** while the 256 entries
    rebuild.
- **Sound:** none; bit-identical codes.
- **Implementation:**
  - **RAM approved (Q4):** the 256 B table in `.bss` (SRAM1), owner
    `CrumpBitEffect.c`. No FXBUF cost.
  - The key (`bit_off | bit_invert << 8`) sits beside the table in `.bss` as
    a `uint32_t` (4 B; 260 B in all). Both rows span 0..255, so every 16-bit
    value is a real key, and a zeroed `.bss` key would match `bof` 0 / `biv` 0
    against an all-zero table. `crumpBit_init()` sets the key to an
    out-of-range sentinel (`0xFFFFFFFF`), so the first block builds the table.
- **Host laws:** the CB4/CB10 stage-1 output is identical. Add a law: the table
  equals `(c & keep) ^ inv` for all 256 codes after every change.

### G4 — `grn` echo: plain feedback, capped below unity

- **What:**
  - The `grn` delay-line return drops the DC blocker and the cubic soft knee:
    `write = adc(½(grains_l + grains_r) + fb · tap)`.
  - `fb` reaches at most 0.98 (`CRUMPBIT_GRAIN_FB_MAX`), so the line always
    decays. The ADC clamps any peak.
  - **`grn` feedback curve (Q7):** `fb = raw · (CRUMPBIT_GRAIN_FB_MAX / 127)`,
    linear over the whole row, so 127 = 0.98. A `grn`-only mapping of the same
    stored row; `crumpBit_feedbackGain()` (0..1.2) is unchanged for the other
    sub-types. No table, and it replaces the prototype's per-block `min`.
- **Measured:** the main share of the `grn` loop drop 131 → 118, with X2. The
  isolated split wasn't measured.
- **Sound:** no echo feedback above about unity; no soft saturation in the
  echo. Without gain above 1, DC decays on its own.
- **Implementation:**
  - `g->dc_x`/`dc_y` are no longer needed (4 + 4 B freed in the `grn` union
    member); G6 reuses them (Q3).
  - The `fbk` label in `grn` follows the new curve (0..98, or the display the
    row already uses scaled to 0.98).
- **Host laws:** **CB14** stability; **CB8** (stability at 1.2) no longer
  applies to `grn`. Add a law: `grn` echo decays at every `fbk` value, and
  `fbk` 127 gives exactly `CRUMPBIT_GRAIN_FB_MAX`.

### G6 — Position fit worked out only on a change

- **What:** the Position fit (the 5-probe search from A) is computed when tempo
  or line length changes, and cached.
- **Measured:** not isolated. `grn` per block went 781 → 779 in the round that
  also carried G4 and G7.
- **Prototype shortcut:** the cache and key are file statics.
- **Implementation (Q3):**
  - The fit (0..16) goes in `CrumpBitGrainState.spare`; the key (tempo and
    line length, 4 B) in the bytes G4 frees (`dc_x`/`dc_y`). No runtime growth.
  - The key must be invalidated wherever the `grn` state is (re)initialised,
    so a fresh `grn` entry recomputes the fit.
  - **Measure G6 isolated on the implemented tree. Keep it if it saves
    ≥ 0.5 per frame; otherwise drop it** and leave the probe search per block.
- **Host laws:** CB14 (c) through a tempo change and a share change.

### F1 — one-pole output filter for `grn` and `rvb`

- **What:** in `grn` and `rvb` the stage-4 filter is a one-pole per channel,
  not the resonant TPT SVF.
  - Per frame and channel: `s += k·(x − s)`; `y = cx·x + cl·s`.
  - `k` comes from the same `frq` index law, with `bal` still spreading the
    channels.
  - The type crossfade (`cx`, `cl`) still ramps per frame.
  - `res` has no effect in these two sub-types.
  - `dly`/`cmb`/`pit` keep the resonant filter unchanged.
- **Decided (Q5, Q6):**
  - `typ` mapping: `LP`, `LP2`, `BP`, `UBP` → low (0, 1); `HP`, `notch` →
    high (1, −1); `peak`, `off` → flat (1, 0). The stored value is kept, so a
    value set in another sub-type still does something sensible.
  - In `grn`/`rvb` the page shows `typ` as its mapped meaning (`LP`/`HP`/
    `off`) and `res` as `---`. Both rows stay stored for the other sub-types.
  - `k` comes from a generated table, `crumpBit_onePoleK[CRUMPBIT_TABLE_SIZE]`
    (128 floats, 512 B flash), in `CrumpBitTables.c` from
    `gen_crumpbit_tables.py`. Each entry is matched so the one-pole's −3 dB
    point follows `frq` as the SVF does at `res` 0. No divide per block.
    Re-measure the 207.
- **Measured (combined build):**
  - Filter loop 31 → **16**.
  - `process` per block on the one-pole path 207, against 231 on the SVF path.
  - The branch costs the SVF sub-types about +0.1 per frame against a
    single-filter build (227 → 231 per block).
- **Prototype law:**
  - `k = g / (½ + g)` from `crumpBit_filterG[]`: a divide per channel per
    block.
  - Type mapping: `LP`, `LP2`, `BP`, `UBP` → low (0, 1); `HP`, `notch` →
    high (1, −1); `peak`, `off` → flat (1, 0).
  - See Q5 and Q6.
- **Sub-type switches:**
  - Switching between an SVF sub-type and `grn`/`rvb` changes the filter's
    structure.
  - Seed the one-pole state from the SVF low integrator (`svf_*2`) when
    entering, and the SVF low integrator from the one-pole state (band state 0)
    when leaving.
  - Do this in `crumpBit_enter()`, so no runtime field and no per-frame cost.
    The prototype used a static flag (Q12).
  - No crossfade of the sub-type change itself. S091 P3 S7 stands: the switch
    glitch is wanted.
- **Host laws:**
  - New law: the one-pole response matches `k` (−3 dB point within tolerance
    of the SVF's `frq` at `res` 0).
  - `res` has no effect in `grn`/`rvb`.
  - Type mapping as decided.
  - `bal` spread (**CB16** extended to the one-pole path).
  - State seeding on switch: no step above a bound in a switch storm (**CB11**).

---

## 5. Measured and rejected (or not adopted)

| # | Change | Measured result | Why not |
|---|---|---|---|
| X3 | Run the output filter inside each engine loop | **Worse everywhere:** `dly` +7, `cmb` +13, `pit` +6.6, `grn` +7.1, `rvb` +9.2 per frame | The engine loops run out of registers, and the filter state spills |
| R3 | Mirror the start of the `rvb` word buffer so line reads never wrap | Loop 132 → 132, per block +24: **+1.1** | The four line lengths are reloaded from the stack anyway |
| V2 | Cymbal/Hi-Hat second modulator renders by adding into the first | The add pass is 2 instructions per sample (unrolled `QADD16`); add-variants cost the same | No saving, and about 2.6 KB of loop copies against 1,060 B free ITCM |
| V4 | Distortion divide replaced by a reciprocal table plus one Newton step (all six voices) | Hardware, §3.1: best case **10 cycles per sample slower**, average equal | `VDIV` overlaps other work on the M7 |
| — | Drop P3 (third pitch slot) from `grn` | `grn` per block 779 → 769: **−0.3** | Not worth losing P3 |
| X1 | Drop the output filter type crossfade | −3.8 (measured, P4 tree) | Not needed; `typ` changes would click |
| X4 | Drop `bal` | −0.6 | Not needed |
| — | Remove the output filter entirely | −33.8 | F1 keeps a filter for 15.75 |
| R2, R4, G2, G5 | Damping on the input, mono reverb wet, mono grains, no grain echo line | not measured | Not needed after F1 |
| — | AC coupling or bit masks removed outright | −9 to −11.5 / −6 to −8 (measured) | Core CrumpBit character |
| V1 | Linear voice filter for Cymbal/Hi-Hat | not measured | Outside CrumpBit; see Q13 |
| G1 | Grain pick: the five uniforms (`u_pos u_slot u_tune u_pan u_side`) from two xorshift words split into fields (24, 12, 12, 12, 1 bits) | `grn` per block −10: **−0.3** | Dropped (Q1): changes every random sequence (CB14 statistics, CB11 scripts) and lowers the uniforms to 12 bits for 0.3 |
| G7 | One `grn` frame loop with the grain event (trigger or steal) applied inside at its frame | **Neutral:** GCC re-split the loop at the event frame (two heads, 118 each) | Dropped (Q2): adds an event struct and indirection for nothing; keep the two-span form |
| S1 (`mix`) | Step `mix` once per block with the other S1 ramps | Putting the ramp back: about **+1** | Not adopted (Q9, user): every sub-type stays under 170 with the ramp |

---

## 6. Code modules to touch

| File | Changes |
|---|---|
| `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c` | A (`crumpBit_grainPick`, `crumpBit_effectiveMax`); B (`crumpBit_tapLoop`, new Q14/Q21 helpers, `crumpBit_readQ14` moved up); P1 (`crumpBit_headRead`); R1 (`crumpBit_runReverb`); G3 (`crumpBit_grainPick` span rounding, `crumpBit_runGrain` idle park, `crumpBit_grainSpan` clamp); S1 (`crumpBit_tapLoop`, `crumpBit_runPitch`, `crumpBit_runGrain`/`crumpBit_grainSpan`; `fb` and pan gains only, the `mix` ramps stay); X2 (`.bss` table and key, `crumpbit_block_t`, `crumpbit_grain_k_t`, `crumpBit_stage1`, `crumpBit_process`, sentinel in `crumpBit_init`); G4 (`crumpBit_grainSpan`, `crumpBit_runGrain`, `CRUMPBIT_GRAIN_FB_MAX`, the `grn` feedback mapping); G6 (`crumpBit_runGrain`, `crumpBit_grainPick`; kept only if ≥ 0.5); F1 (`crumpBit_process` coefficient branch from `crumpBit_onePoleK[]` and second filter loop, `crumpBit_enter` seeding). Comments and the file's header blocks to match. |
| `Core/DSP/Effects/CrumpBit/CrumpBitEffect.h` | `CrumpBitGrainState` loses `dc_x`/`dc_y` (G4); their bytes hold the G6 key and `spare` the fit (Q3). No runtime growth: `sizeof(CrumpBitRuntime)` assert (`CrumpBitEffect.c:154`) stays at 100. |
| `Core/DSP/Effects/CrumpBit/CrumpBitParameters.c` | A: the `dpn` row without `BUFFER_DEPENDENT` (and its comment). Labels: `ton` above 63 shown flat in `rvb` (R1, Q10); `typ` as `LP`/`HP`/`off` and `res` as `---` in `grn`/`rvb` (F1, Q5); `fbk` on the `grn` curve (G4, Q7). |
| `Core/DSP/Effects/CrumpBit/CrumpBitTables.c/.h` and `tools/plots/gen_crumpbit_tables.py` | New `crumpBit_onePoleK[CRUMPBIT_TABLE_SIZE]` (128 floats, 512 B flash), −3 dB point matched to the SVF's `frq` law at `res` 0 (Q6). Regenerate; `--check`. The `grn` feedback curve needs no table (Q7). |
| `Core/Menu/menuEffects.c` | Only if `res`/`typ` presentation in `grn`/`rvb` needs more than the label hooks (Q5). |
| `Core/DSP/Effects/EffectsManager.c` | Expected none. A changes only a row flag, and the manager's BUFFER_DEPENDENT loop is generic. Check the runtime-union assert if the runtime grows. |
| `tools/dsp_test/modules/crumpbit.c` | CB4 reference (Q8), CB6, CB10, CB11, CB13, CB14, CB15, CB16 updates; new laws for X2, R1, G4 and F1 (§7). |
| `tools/dsp_test/Makefile` | `crumpbit_elf_check` limits (§7.2); break builds whose `sed` patterns change (§7.3); a new `crumpbit_cpu_check` (§8.4). |
| `tools/dsp_test/elf_blockpath.py` (new) | `blockpath.py` and `throughpath.py` from this session as a repo tool (§8.4). |
| `tools/dsp_test/DSP_TEST.md` | The `crumpbit` and `crumpbit_elf_check` rows; the new tool and gate. |
| `knowledge_files/.../EFFECTS_MIXER_DSP_REFERENCE.md` | §4.4 output filter (F1); §4.4a engines (B, P1, R1, G3, G4, S1, X2); §6.2a CPU table (§2 of this plan). |
| `knowledge_files/.../EFFECTS_BUS_REFERENCE.md` | `dpn` no longer BUFFER_DEPENDENT; `ton` in `rvb`; `res`/`typ` in `grn`/`rvb`; `fbk` in `grn`. Also fix the two stale P4 lines: line 69 "126,688 B" and line 535 `format_target3`. |
| `knowledge_files/.../CPU_USE_DSP_AUDIT.md` | Item 65 (S091 P5) with the §2 tables and the §5 rejections, including the hardware V4 data. |
| `knowledge_files/.../STORAGE_SRAM_MANIFEST.md` | The X2 table and key (256 + 4 B, `.bss` SRAM1, owner `CrumpBitEffect.c`, approved Q4). Runtime unchanged at 100 B. |
| `MEMORY.md` | After hardware: status line, and the CrumpBit binding line (one-pole filter in `grn`/`rvb`, the 170 cap). |

---

## 7. Tests and gates

### 7.1 Host laws (`make -C tools/dsp_test crumpbit`)

| Law | Change |
|---|---|
| CB4 | New reference model: P2 with the Q14 tap read (B) and per-block stepped `fb`/pan gains (S1), `mix` still ramped; bit-exact against that (Q8) |
| CB6 | `grn` Position: `effective_max(dpn)` = 255 everywhere; the engine's fit equals `crumpBit_grainPosLimit()` |
| CB8 | Stability at 1.2 for `dly`/`cmb`/`pit`; `grn`'s echo by the new G4 law |
| CB10 | Unchanged: the `mix` ramp is kept (Q9) |
| CB11 | Switch storm including SVF ↔ one-pole switches: no output step above a bound; seat law unchanged |
| CB13 | `pit` ratio unchanged (P1) |
| CB14 | (c) Position, longest fitting stop; copy identity with G3 done fully; fades and read safety unchanged; random statistics unchanged (G1 dropped) |
| CB15 | `rvb` stability, decay, freeze unchanged; `ton` above 63 equals 63 (R1) |
| CB16 | `bal` on both the SVF and the one-pole path |
| New | X2 table equals `(c & keep) ^ inv` after every change, including the first block after init; G4 echo decays at every `fbk` and 127 = `CRUMPBIT_GRAIN_FB_MAX`; F1 response from the `k` table, `typ` mapping, `res` inert, seeding |

### 7.2 ELF gate (`make -C tools/dsp_test crumpbit_elf_check`)

Limits measured on the P5 prototype, where `mix` was stepped. The kept `mix`
ramp (Q9) puts about one instruction back into each engine loop, so the
engine-loop limits below are a floor: set them from the implemented tree.
The filter loops and the helpers are unaffected.

| Symbol | Limit |
|---|---|
| `crumpBit_runDelay` loop | 109 |
| `crumpBit_runComb` loop | 107 |
| `crumpBit_runPitch` loop | 104 |
| `crumpBit_runGrain` loops (two heads, `--cond-heads --in-range`) | 118 |
| `crumpBit_runReverb` loop | 130 |
| `process` SVF loop | 31 |
| `process` one-pole loop | 16 |
| `crumpBit_divisionFor` | ≤ 117 (113 straight-line) |
| `crumpBit_tableLimit` | 63 |
| VDIV in any frame loop | 0 |

The `elf_looppath.py --sym` lookup matches the `.constprop`/`.isra` clones.
Gate on the symbol `process` actually calls.

### 7.3 Break builds

These `sed` patterns target code that changes:

| Break | Targets | Action |
|---|---|---|
| break3 | `crumpBit_toneSetup` flat | Still valid for `dly`/`cmb`/`pit` |
| break6 | `rvb` left-channel mix line | Re-check the pattern after R1 (S1 no longer touches `rvb`) |
| break10 | `s->out_a -= CRUMPBIT_GRAIN_FADE_K` | Re-check after G3 |
| break11 | `crumpBit_filterCoeffs(... fl ...)` | Still valid on the SVF path; add a one-pole `bal` break |
| break12 | the overwrite clamp | Unchanged |

New breaks:
- an X2 table built from the wrong mask;
- the `grn` feedback on the shared 0..1.2 curve instead of the G4 mapping;
- F1 seeding skipped.

### 7.4 Hardware

| # | Check | Expected |
|---|---|---|
| HW1 | FluxMax (the worst-case Scene), CrumpBit in each sub-type in turn; `cpu` widget, DEV `UNR` | Below P3's `dly` reading in every sub-type; no underruns |
| HW2 | `dly`/`cmb` A/B against P4 by ear | Same character; no audible change from the Q14 read |
| HW3 | Fast LFO on `fbk`, `dpn` (S1); `mix` as a control | `mix` smooth as on P4; judge zipper on `fbk`/`dpn` |
| HW4 | `pit` at ±12/±24 st | Grittier, acceptable (P1) |
| HW5 | `rvb` `ton` sweep | Dark below centre, flat above (R1) |
| HW6 | `grn` `fbk` sweep to max | Whole knob range usable; echo decays at max; no runaway (G4, Q7) |
| HW7 | `typ`/`frq`/`bal` in `grn`/`rvb`; switch sub-types under audio | One-pole response; `typ` shows `LP`/`HP`/`off`, `res` shows `---`; no click beyond the wanted switch glitch (F1) |
| HW8 | `bof`/`biv` from the page and from FX lanes at fast steps | No audible change from P4; no underruns on a change (X2 rebuild) |

---

## 8. Implementation notes

### 8.1 Order

1. A, B, S1 (`fb` and pan gains; `mix` keeps its ramp): shared plumbing and
   the tape loop.
2. P1, R1.
3. X2.
4. F1: the `crumpBit_onePoleK[]` table (generator, `--check`), the one-pole
   path, `crumpBit_enter()` seeding, the `typ`/`res` labels.
5. G3, done fully; G4 with the `grn` feedback mapping; G6, measured
   isolated and kept only if it saves ≥ 0.5 per frame.
6. Host laws, the ELF gate, the CPU gate.
7. Docs.

Measure after each step on the linked image. The numbers interact: register
pressure moves when code moves.

### 8.2 Keep `crumpBit_tapLoop()` inlined

With a larger body, LTO outlined it once (`crumpBit_tapLoop.isra.0`, a shared
loop with runtime glide/feedback). The prototype used
`__attribute__((always_inline))`. Keep it, so `dly` and `cmb` each get their
own constant-folded loop.

### 8.3 Prototype shortcuts to replace

| Item | Prototype | Implementation |
|---|---|---|
| G3 | clamp removed only | Block-aligned grain end and idle-voice park; CB14 copy identity must pass |
| G6 | file-static cache and key | Fit in `CrumpBitGrainState.spare`, key in G4's freed `dc_x`/`dc_y` bytes; invalidated on `grn` state init; kept only if ≥ 0.5 (Q3) |
| X2 | file-static table and key | `.bss` (SRAM1) table 256 B + `uint32_t` key 4 B, approved; key sentinel set in `crumpBit_init()` (Q4) |
| F1 seeding | file-static flag in `process` | `crumpBit_enter()` (Q12) |
| F1 coefficient | divide per channel per block | Generated `crumpBit_onePoleK[]` lookup (Q6) |
| S1 | `mix` stepped too; `fb_inc`/`gain_*_inc` left unused (warnings) | Keep the `mix` ramp as in P4 (Q9); remove the `fb`/pan per-frame ramp variables; zero new warnings (the static warning gate compares the full warning list) |
| G1, G7 | applied | Not carried over (Q1, Q2): keep P4's five draws and the two-span loop |
| R1 | `tone_a`/`tone_b` set but unused in `rvb` (warnings) | Remove |

### 8.4 CPU gate

Add `tools/dsp_test/elf_blockpath.py` from this session's `blockpath.py` and
`throughpath.py`:
- iterative DFS;
- loops weighted 0;
- calls listed;
- an optional "through these addresses" constraint.

Then add `crumpbit_cpu_check`. For each sub-type it computes engine loop +
filter loop + (engine per block + `process` per block on that filter's path +
86) / 32, with `dly`'s Sync search (113) added to its per-block term, and
fails above **170**.

### 8.5 Prototype source

The measured prototype is in the session scratch directory and may not survive
a reboot:

```
/private/tmp/claude-501/-Users-bc-Helicase-Project-Helicase/59ffafbd-b981-4a03-8014-6cc235047417/scratchpad/
  ab6_effect.c        P5 final prototype CrumpBitEffect.c (all of §4, F1 combined;
                      also G1, G7 and the stepped `mix`, which the decisions drop)
  ab/                 the tree it was built in (CrumpBitParameters.c has A's row change)
  ab6.elf             the measured image
  blockpath.py, throughpath.py
  accepted.py, accepted2.py, g7.py   the patch scripts
```

The appendix keeps the essential code in case the scratch directory is gone.

---

## 9. Session state at close (hardware)

- **The SD card holds the S092 cycle-test image** (`LXRV2_lxr02.img`, SHA-256
  `f0b93b0c…`). It runs the production audio path plus extra timed loops
  (about +3 % CPU per Cymbal/Hi-Hat).
- To return to normal:
  1. Copy the repo's `build/LXRV2_lxr02.img` (the P4 image) to the card.
  2. Flash it.
- **AutoSave was edited on the card** to boot the test image:
  - In `.hcprms2` (the winning record, generation 1194), active Scene 4
    ("FluxTime"), the Effect token changed from `cbt` to `off`, and the CRC was
    recomputed. Nothing else changed.
  - Backups of the original `.hcprms1`, `.hcprms2` and `.hcnames` are in
    `scratchpad/sd_backup/` and in `SD_CARD_TEST_1/`.
  - Switch the Effect back to CrumpBit on the page. Its settings may come back
    at defaults.
  - Don't restore the old records once the board has run: AutoSave has written
    newer generations since.
- **The S091 P4 hardware checks H1–H9 remain open.**

---

## 10. Decisions (user, 2026-11-05)

Q9 was decided against the recommendation; every other recommendation was
accepted as written. The sections above already carry each decision.

| # | Question | Decision | Where |
|---|---|---|---|
| Q1 | Keep G1 (two-word grain randomness)? | **Drop.** −0.3 isn't worth changing every random sequence and lowering the uniforms to 12 bits | §5 |
| Q2 | Keep G7 (single `grn` loop span)? | **Drop.** Measured neutral; keep the two-span form | §5 |
| Q3 | G6 storage, and is it worth it? | **Implement** in the bytes G4 frees (fit in `spare`, key in `dc_x`/`dc_y`), no runtime growth. **Measure isolated; keep only if ≥ 0.5 per frame** | §4 G6 |
| Q4 | X2 table placement and RAM | **`.bss` (SRAM1), approved:** 256 B table + 4 B key, owner `CrumpBitEffect.c`. No FXBUF cost | §4 X2 |
| Q5 | `typ`/`res` in `grn`/`rvb` | Mapping `LP`/`LP2`/`BP`/`UBP` → low, `HP`/`notch` → high, `peak`/`off` → flat. Page shows `typ` as `LP`/`HP`/`off`, `res` as `---`; both rows stay stored | §4 F1 |
| Q6 | One-pole coefficient law | **Generated table** `crumpBit_onePoleK[]` (128 floats, 512 B flash), −3 dB point matched to the SVF's `frq` law at `res` 0. No per-block divide | §4 F1 |
| Q7 | `fbk` in `grn` with the 0.98 cap | **Rescale:** `grn` `fb = raw · 0.98/127`, so the whole knob is usable. Other sub-types keep the 0..1.2 curve | §4 G4 |
| Q8 | CB4 reference after B and S1 | **Replace** the P2 reference with P2 + Q14 tap read + stepped `fb`/pan gains (ramped `mix`); keep CB4 bit-exact | §7.1 |
| Q9 | S1 zipper risk | **Keep the `mix` ramp** (user). About +1 per frame; the user expects no sub-type to cross 170 (projection §2.1a: `grn` ≈ 168.8). `fbk` and `dpn` gains still step per block | §4 S1, §2.1a |
| Q10 | `ton` above 63 in `rvb` | Label shows values above 63 as flat (`0`); stored value kept; row not limited | §4 R1 |
| Q11 | Position label after A | **No change**: the label already shows the clamped stop | §4 A |
| Q12 | F1 state seeding | In `crumpBit_enter()` from the old and new sub-type. SVF → one-pole: `svf_l1 ← svf_l2` (the one-pole state uses the `svf_*1` fields). One-pole → SVF: `svf_*2 ← one-pole state`, `svf_*1 ← 0`. Covers menu, Pattern and Scene switches | §4 F1 |
| Q13 | Cymbal/Hi-Hat linear filter (V1) | **Not part of P5.** A separate task if the Scene needs more headroom; V2 and V4 are closed | §5 |
| Q14 | Sub-type switch crossfade | **None.** S091 P3 S7 (the switch glitch is wanted) stands; F1 seeding prevents an extra filter click | §4 F1 |
| Q15 | Tooling in the repo | **Add** `elf_blockpath.py` and `crumpbit_cpu_check` in the same change, so the 170 cap is a gate | §8.4 |

---

## Appendix — essential prototype code

**B: tap read (inside `crumpBit_tapLoop()`, per frame).**
```c
delay += (target - delay) * glide_k;
d14 = crumpBit_floatToQ14(delay);                 /* vcvt.s32.f32 #14 */
wet = crumpBit_q21ToFloat(                        /* vcvt.f32.s32 #21 */
    crumpBit_readQ14(loop, len, lim14, (int32_t)(wp << 14) - d14) &
    -(int32_t)((uint32_t)d14 + (2u << 14) <= (valid << 14)));
```

**P1: `pit` head.**
```c
int32_t rp = wpg - (int32_t)(((uint64_t)phase * w14) >> 32);
rp += (int32_t)(lim14 & (uint32_t)(rp >> 31));
return (int32_t)loop[(uint32_t)rp >> 14] << 14;
```

**R1: `rvb` damping.**
```c
tone_k = crumpBit_toneK[(rt->tone_raw > CRUMPBIT_TONE_FLAT)
                        ? CRUMPBIT_TONE_FLAT : rt->tone_raw];
/* per frame: s_k += tone_k * (y_k - s_k); z_k = s_k; */
```

**S1: tape loop (before the frame loop; per-frame `+=` removed).** As
decided (Q9), `mix` is not stepped: the prototype's `mix` line below is not
carried over, and `mix += mix_inc` stays in the frame loop.
```c
mix += mix_inc * (float)frames;   /* prototype only; dropped by Q9 */
fb = fb_to;
gain_l += gain_l_inc * (float)frames;
gain_r += gain_r_inc * (float)frames;
```

**X2: stage 1 and the per-block table.**
```c
/* stage 1 */
const int32_t code_l = (int32_t)lut[crumpBit_adc(xl)];
const int32_t code_r = (int32_t)lut[crumpBit_adc(xr)];
/* process, per block */
if (key != lut_key) {               /* key = bit_off | bit_invert << 8 */
    const uint32_t inv = rt->bit_invert;
    const uint32_t keep = (uint32_t)~((uint32_t)rt->bit_off & ~inv) & 0xFFu;
    for (c = 0u; c < 256u; c++)
        lut[c] = (uint8_t)((c & keep) ^ inv);
    lut_key = key;
}
```

**G4: `grn` echo write.**
```c
k->dline[s->wp] = (int8_t)((int32_t)crumpBit_adc(
    0.5f * (grains_l + grains_r) + s->fb * tap) - 128);
/* prototype, per block: s.fb = min(s.fb, CRUMPBIT_GRAIN_FB_MAX);  0.98
 * decided (Q7): fb = raw * (CRUMPBIT_GRAIN_FB_MAX / 127.0f), no min */
```

**G6: cached Position fit (per block in `crumpBit_runGrain()`).**
```c
if (((uint32_t)bpm | (len << 16)) != fit_key) {
    const float cap = (float)(len - CRUMPBIT_LOOP_GUARD) / per_tick;
    uint32_t fit = 0u;
#define T(i) (crumpBit_grainTime[crumpBit_grainStopCc[(i)]])
    fit += (T(7u) <= cap) ? 8u : 0u;
    fit += (T(fit + 3u) <= cap) ? 4u : 0u;
    fit += (T(fit + 1u) <= cap) ? 2u : 0u;
    fit += (T(fit) <= cap) ? 1u : 0u;
    fit += (T(fit) <= cap) ? 1u : 0u;
#undef T
    grain_fit = fit;
    fit_key = (uint32_t)bpm | (len << 16);
}
/* grainPick, pos > 63: stop = min(posStop(pos), fit - 1); fit 0 -> lag 2 */
```

**F1: one-pole coefficients (per block) and loop (16 instructions).**
```c
/* per block, grn/rvb */
a1l = gl / (0.5f + gl);   a1r = gr / (0.5f + gr);   /* Q6: crumpBit_onePoleK[] */
cx = op_mix[type_prev][0]; cl = op_mix[type_prev][1];
cx_inc = (op_mix[type][0] - cx) * per_frame;
cl_inc = (op_mix[type][1] - cl) * per_frame;
/* per frame */
cx += cx_inc;  cl += cl_inc;
sl += a1l * (xl - sl);   sr += a1r * (xr - sr);
l[n] = cx * xl + cl * sl;  r[n] = cx * xr + cl * sr;
```
