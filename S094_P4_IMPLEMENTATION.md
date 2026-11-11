# S094 P4 — PWM Law and Alkaline Waves: Implementation Schedule

- **Status:** implemented (S094, 2026-11-11). The schedule below is the
  contract; the record in §12 holds what was measured, including the one
  correction (S9's metric). This document itself changes no code. Every
  change is cited by file, function and line in the tree as it was before
  the step. Each comes with the comment block that went in with it, which is
  the documentation in place.
- **Source:** `S094_P4_PWM_OSC_INVESTIGATION.md` (decisions §6, schedule
  outline §9).
- **Decisions this implements (user, 2026-11-11):**

| # | Decision |
|---|---|
| P4-1 | `pos` 0 = the unmodified wave (50 %), `pos` 127 = the narrowest; 128 distinct widths, linear in width, from `pos`-indexed tables. The per-sample loops are unchanged |
| P4-2 | Narrowest width: **Rec 0.39 %** (and the Alkaline `pwm` Sqr, the same pulse); Saw 2.54 %; Tri 2.54 %; Sin 6.25 % |
| P4-3 / P4-4 / P4-6 | Alkaline `wav`: add **Tri** only (no Rec, Sin or Noi) |
| P4-5 | Alkaline `pwm`: add **Tri** only, gated by a measured spike. **No Sin (user, revised):** the ladder filters highs so strongly that a warped sine and a warped triangle would sound practically the same. Rec is the existing Sqr; no Noi |
| P4-7 | Append: `wav` and `pwm` both Saw 0, Sqr 1, Tri 2 |

- **Binding rules that apply:**
  - **S089 / S094 P2 §21:** the Alkaline's Basic share (19,620 cycles per
    block, cold, with a stereo send) is set by its dearest path, `wtb` with
    a slot (loop 234, whole voice 276.9). No new path may be dearer.
  - **No drum-instrument loop changes:** `elf_osc_check` and the P4 loop
    dumps stay unchanged, apart from the per-block plan.
  - **RAM and ITCM:** none added. Flash only (§11).
  - **No backward compatibility:** stored `pos` under `typ pwm` changes
    meaning, with no migration (standing rule).
  - **`MEMORY.md` and `knowledge_files/`:** no step edits them (§13 lists
    what goes out of date).
  - **Commits** belong to the user.

---

## 0. Order and gates

| Step | What | Gate before the next step |
|---|---|---|
| **S1** | Tables: generator and generated files | `gen_pwm_tables.py --check`; table checks T1-T9 |
| **S2** | The shared warp header; `Oscillator.c` plan on the new tables | firmware builds; P4 loop dumps: every `osc_pwm*Block` loop unchanged |
| **S3** | Oscillator host laws re-based; new laws and a break | `make -C tools/dsp_test oscillator` passes; breaks caught |
| **S4** | **Spike (gate):** the Alkaline warp mode (Tri) in a scratch copy | the WT loop ≤ 234 per sample linked; whole voice ≤ 276.9. Otherwise **stop and report** |
| **S5** | `DiodeLadder.c/.h`: fused mode 4 (WT) and one wrapper | builds; the existing wrappers' loops unchanged |
| **S6** | `AlkalineVoice.h/.c`: the wave constant, `wav` Tri, `pwm` dispatch, the list size | builds |
| **S7** | Menu and InstrumentManager: the wave list and domain | `special_tags`, `make_init_alk --check` |
| **S8** | Alkaline bench: the real triangle, AL-TY / AL-PW updated, AL-XT and AL-PWW new, breaks; `control_cost.py` | `make -C tools/dsp_test alkaline` passes |
| **S9** | The Tri scale, by measurement | bench prints within ±0.5 dB of the saw |
| **S10** | Build, ledger, the S094 P4 plots regenerated, the record | `make all && make img`; `link_budget.py`; host `all` |

- **S1-S3** change the law for every instrument.
- **S4-S9** are the Alkaline.
- **If S4 fails:**
  - S1-S3 and the Alkaline `wav` Tri still go in (S6 part A, S7, S8
    AL-XT);
  - the Alkaline `pwm` Tri waits for the user.

---

## 1. Inventory (today's tree)

| Where | What it does today | Changed by |
|---|---|---|
| `tools/plots/gen_pwm_tables.py` | writes `oscPwmRecip[128]`, `oscPwmLevelQ14[64]`, `oscPwmLift[128]` (indexed by `p` and `128 − p`) | S1 |
| `Core/DSPAudio/oscPwmTables.h/.c` | generated | S1 |
| `Oscillator.c:3590-3634` | the PWM section header comment (forms D/WT/WS, `W = p << 25`) | S2 |
| `Oscillator.c:3666-3736` | `osc_pwmPlan()`: `p = clamp(positionApplied, 1, 127)`, `m`, `wPh = p << 25`, `ra/rb = recip[p] / recip[128 − p]`, `d = p << 3`, level `[m]`, lift `[p]` | S2 |
| `Oscillator.c:3741-3761` | `osc_pwmWarp()` (static inline) | S2 (moved to a shared header) |
| `Oscillator.c:3842-4240` | the 15 PWM loops (`osc_pwm{D,Wt,Ws}{Plain,Fm,FmRamp,Ring,Sync}Block`), ITCM `.itcm.s094p1` | **unchanged** |
| `Oscillator.c:1585-1610` | `osc_typedPositionApply()` PWM slew (≤ 2 steps per block, linear) | unchanged (comment only) |
| `Oscillator.h:248-250` | `position` comment: "pulse width (1..127; 0 acts as 1)" | S2 (comment) |
| `Oscillator.h:108-121` | `OSC_PWM_WAVE_COUNT`, `OSC_PWM_SLEW_STEPS` | unchanged |
| `DiodeLadder.c:302-560` | `dl_renderB2Body()`: fused 0-3; mode 3 = the two-read pulse/sum | S5 (mode 4; three parameters) |
| `DiodeLadder.c:556-638` | wrappers `…B2`, `…B2Osc`, `…B2Osc2`, `…B2OscPwm` | S5 (pass the new zero arguments; one new wrapper) |
| `DiodeLadder.h:195-230` | prototypes | S5 |
| `AlkalineVoice.h:60-61` | `ALK_WAVE_SAW 0u`, `ALK_WAVE_SQUARE 1u` | S6 |
| `AlkalineVoice.c:14-42` | `ALK_SAW_SCALE_V`, `ALK_PWM_SQR_SCALE_V` | S6 (the Tri scale added) |
| `AlkalineVoice.c:526-575` | `alk_renderWav()`: Saw/Sqr fused read | S6 (Tri) |
| `AlkalineVoice.c:664-713` | `alk_renderPwm()`: D form on `sawTable`, `p = clamp(pos, 1, 127)` | S6 (law, dispatch) |
| `AlkalineVoice.c:746-777` | `Alkaline_setOscType()`: list size `ALK_WAVE_SQUARE + 1` for `wav` and `pwm` | S6 |
| `AlkalineVoice.c:317-326, 432-455` | trigger snap, async slew under `pwm` (comments say "sawTable rows") | S6 (comments) |
| `MenuText.h:255-268` | `alkWaveNames` {2}: Saw, Sqr | S7 |
| `menu.c:12168-12190` | `menu_formatWaveCell()` Alkaline branch | S7 (comment) |
| `menu.c:12545-12548` | assert: `alkWaveNames` lists 2 | S7 |
| `InstrumentManager.c:823-867` | `instrumentManager_waveDomainMax()`: Alkaline `wav`/`pwm` → `ALK_WAVE_SQUARE` | S7 |
| `tools/dsp_test/modules/oscillator.c:1318-1622` | PW1-PW8 | S3 |
| `tools/dsp_test/modules/alkaline.c` | AL-X (Saw/Sqr fused), AL-TY, AL-PW; zero `sine_table` / `triTable` stand-ins (l. 97-99) | S8 (the real `triTable`) |
| `tools/dsp_test/Makefile:72-80, 109-125, 377-…, 536` | host extraction rules; PWM breaks; `alkaline`; `pwm_tables` | S3, S8 |
| `tools/dsp_test/osc_host.h:59-62` | includes `Oscillator.host.h`, `oscPwmTables.host.h` | S3 |
| `tools/alkaline/control_cost.py:53-75, 236-247` | ITERS and the fused-loop guard | S8 |
| `tools/dsp_test/modules/pwm_dump.c`, `tools/plots/plot_s094p4_pwm.py` | the S094 P4 plots | S10 (re-run; the law plot's text) |

**Two facts the schedule relies on (checked):**
1. **`pos` 0 under the new law is bit-exact with today's `p` 64.** *d* =
   512, level 16,384, *W* = 2^31, `ra = rb = 67,108,863` (=
   ⌊(2^32 − 1)/64⌋ = today's `oscPwmRecip[64]`), lift 0. So every PW1-type
   identity carries over to `pos` 0 (law PW9).
2. **The warp needs no whole `p`.** `osc_pwmWarp()` computes
   `umulh(ph, ra) << 6`.
   - With `ra = ⌊(2^32 − 1) · 2^25 / W⌋` this is `ph × 2^31 / W` for any
     *W*.
   - The half-cycle ends land within 64 phase units of 2^31 and 2^32, the
     same accuracy as today: computed at `pos` 127 for both Tri and Sin.
   - So the loops are unchanged.

---

## 2. S1 — Tables (`tools/plots/gen_pwm_tables.py`, generated `oscPwmTables.h/.c`)

### 2.1 The law (generator constants and functions)

Replace `recip()`, `level()` and `lift()` and the module docstring's
"What" with the `pos`-indexed law.

```python
"""Generate the PWM oscillator tables (S094 P1 S3; S094 P4 law).

What:       writes Core/DSPAudio/oscPwmTables.h and oscPwmTables.c: every
            table is indexed by pos (0..127, the applied width,
            OscInfo.positionApplied). Under the S094 P4 law pos 0 is the
            unmodified wave (width 1/2) and pos 127 the narrowest; the width
            is linear in pos between the form's two ends:
              D difference (Rec; Alkaline pwm Sqr): d = 512 - 4 pos points of
                  1,024 (exact; 512 .. 4, 0.39 % at pos 127, user P4-2);
              D sum (Saw): d = round(512 - pos x 486 / 127) (512 .. 26,
                  2.54 %);
              WT (Tri): w = 1/2 - pos (1/2 - 26/1,024) / 127 (.. 2.54 %);
              WS (Sin): w = 1/2 - pos (1/2 - 1/16) / 127 (.. 6.25 %).
            Tables:
              oscPwmDRec[128], oscPwmDSaw[128]   uint16 d (points)
              oscPwmLevelRecQ14[128], oscPwmLevelSawQ14[128]
                                 int16 round(16384 x 512 / (1024 - d)): the
                                 level that keeps the two-saw difference and
                                 sum at the table's peak (a ramp difference at
                                 width w peaks at 2 max(w, 1 - w));
              oscPwmTriW[128], oscPwmSinW[128]  uint32 W = round(w x 2^32), the
                                 warp's split point;
              oscPwmTriRa/Rb[128], oscPwmSinRa/Rb[128]
                                 uint32 floor((2^32 - 1) 2^25 / W) and the
                                 same for 2^32 - W: umulh(ph, ra) << 6 is
                                 ph x 2^31 / W (the first half-cycle onto
                                 [0, 2^31)), no division per block;
              oscPwmTriLift[128] uint8 round(log2(1/2 / w)): the octave rows
                                 the warped triangle moves up for its
                                 compressed half-cycle.
Why:        S094 P4 (user): pos 0 = 50 %, pos 127 = the narrowest, no wrap.
            The S094 P1 law (width = pos/128, 64 the centre) mirrored about
            64: p and 128 - p played the same spectrum (P4 investigation
            §2.2). Linear in width keeps every d a distinct whole point (both
            D reads share one fraction, so d cannot be fractional); a curve
            would repeat widths at the narrow end. pos 0 equals the S094 P1
            p = 64 constants exactly (T6), so the identity laws carry over.
Checks:     T1 dRec = 512 - 4 pos; dSaw strictly decreasing 512 .. 26, all
            128 distinct; T2 levels 16384 at pos 0, strictly decreasing;
            T3 W strictly decreasing, TriW[127] = 26 x 2^22, SinW[127] = 2^28;
            T4 ra x W <= (2^32 - 1) 2^25 < (ra + 1) x W, the same for rb
            with 2^32 - W (the floor); T5 the half-cycle ends: 2^31 -
            (umulh(W - 1, ra) << 6) <= 64 and 2^32 - ((umulh(2^32 - 1 - W,
            rb) << 6) + 2^31) <= 64 at every pos; T6 pos 0: d 512, levels
            16384, W 2^31, ra = rb = 67,108,863, lift 0 (= S094 P1 p 64);
            T7 lift non-decreasing, 0 .. 4; T8 --check compares both files
            byte for byte; T9 every value fits its type. Exit 1 on any
            failure.
Inputs:     none.
Outputs:    the two files, or a pass/fail line with --check.
Accessors:  tools/dsp_test/Makefile target pwm_tables.
Affiliates: Oscillator.c osc_pwmPlan(); AlkalineVoice.c alk_renderPwm();
            tools/dsp_test/modules/oscillator.c group pwm; modules/alkaline.c
            AL-PW / AL-PWW; S094_P4_IMPLEMENTATION.md §2.
"""
N = 128
D_REC_STEP = 4            # points per pos step: 512 -> 4 (0.39 %)
D_SAW_END = 26            # 2.54 %
TRI_W_END = 26.0 / 1024.0 # 2.54 %
SIN_W_END = 1.0 / 16.0    # 6.25 %
M25 = (2**32 - 1) << 25


def d_rec():
    return [512 - D_REC_STEP * k for k in range(N)]


def d_saw():
    return [int(round(512 - k * (512 - D_SAW_END) / 127.0)) for k in range(N)]


def level(ds):
    return [int(round(16384.0 * 512.0 / (1024 - d))) for d in ds]


def split(w_end):
    """W, ra, rb for a warp form whose width falls linearly from 1/2."""
    ws = [0.5 - k * (0.5 - w_end) / 127.0 for k in range(N)]
    W = [int(round(w * 2**32)) for w in ws]
    ra = [M25 // x for x in W]
    rb = [M25 // (2**32 - x) for x in W]
    return ws, W, ra, rb


def lift(ws):
    return [int(round(math.log2(0.5 / w))) for w in ws]
```

### 2.2 The checks

`checks()` takes every table and implements T1-T7 and T9 exactly as the
docstring states. T5 uses Python integers:
`((W - 1) * ra >> 32) << 6` and `((2**32 - 1 - W) * rb >> 32) << 6`.

### 2.3 The generated header (`render_h()`)

```c
/* Generated by tools/plots/gen_pwm_tables.py (S094 P1 S3; S094 P4 law). Do not edit. */
#ifndef OSC_PWM_TABLES_H_
#define OSC_PWM_TABLES_H_

#include <stdint.h>

/*
 * PWM oscillator constants (S094 P1 S3; S094 P4 law), flash.
 *
 * What:       every table is indexed by pos = positionApplied (0..127):
 *             pos 0 is the unmodified wave (width 1/2), pos 127 the
 *             narrowest; the width falls linearly in pos.
 *               oscPwmDRec / oscPwmDSaw: the D form's second-read offset in
 *                 points of 1,024 (Rec 512 .. 4, Saw 512 .. 26);
 *               oscPwmLevelRecQ14 / oscPwmLevelSawQ14: 16384 x 512 /
 *                 (1024 - d), the D level that keeps the table's peak;
 *               oscPwmTriW / oscPwmSinW: the warp's split point W (Tri down
 *                 to 2.54 %, Sin down to 6.25 % of the cycle);
 *               oscPwmTriRa/Rb, oscPwmSinRa/Rb: floor((2^32 - 1) 2^25 / W)
 *                 and the same for 2^32 - W (osc_pwmWarp()'s half-cycle
 *                 scales);
 *               oscPwmTriLift: octave rows the warped triangle moves up.
 * Why:        S094 P4 (user): pos 0 = 50 %, pos 127 = the narrowest, no
 *             wrap; per-block lookups, no division, loops unchanged. pos 0
 *             equals the S094 P1 width-1/2 constants exactly.
 * Accessors:  Oscillator.c osc_pwmPlan(); AlkalineVoice.c alk_renderPwm().
 * Affiliates: oscPwmWarp.h (the warp law these scales serve);
 *             tools/plots/gen_pwm_tables.py (the law and checks T1-T9).
 */
#define OSC_PWM_POS_COUNT 128u
extern const uint16_t oscPwmDRec[OSC_PWM_POS_COUNT];
extern const uint16_t oscPwmDSaw[OSC_PWM_POS_COUNT];
extern const int16_t  oscPwmLevelRecQ14[OSC_PWM_POS_COUNT];
extern const int16_t  oscPwmLevelSawQ14[OSC_PWM_POS_COUNT];
extern const uint32_t oscPwmTriW[OSC_PWM_POS_COUNT];
extern const uint32_t oscPwmTriRa[OSC_PWM_POS_COUNT];
extern const uint32_t oscPwmTriRb[OSC_PWM_POS_COUNT];
extern const uint8_t  oscPwmTriLift[OSC_PWM_POS_COUNT];
extern const uint32_t oscPwmSinW[OSC_PWM_POS_COUNT];
extern const uint32_t oscPwmSinRa[OSC_PWM_POS_COUNT];
extern const uint32_t oscPwmSinRb[OSC_PWM_POS_COUNT];

#endif /* OSC_PWM_TABLES_H_ */
```

`render_c()` writes each array with a one-line comment giving its formula,
in the existing style (`rows()`). `oscPwmRecip`, `oscPwmLevelQ14` and
`oscPwmLift` are removed: §1 lists every reader, and S2, S3 and S6 replace
each one.

**Reference values** (checked in §1): Tri `pos` 127: *W* = 109,051,904,
ra = 1,321,528,398, rb = 34,428,595, lift 4. Sin `pos` 127: *W* =
268,435,456, ra = 536,870,911, rb = 35,791,394, lift (unused) 3.

---

## 3. S2 — The warp header and the oscillator plan

### 3.1 New file `Core/DSPAudio/oscPwmWarp.h`

`osc_pwmWarp()` moves here verbatim from `Oscillator.c:3741-3761`, so that
`Oscillator.c` and `DiodeLadder.c` share one law. The loops' generated code
is unchanged: the same `always_inline` function, the same operands.

```c
/*
 * oscPwmWarp.h — the pwm phase warp, shared (S094 P1 S3; S094 P4).
 *
 * What:       osc_pwmWarp() maps the oscillator phase ph to the read phase
 *             psi:
 *               ph <  W: psi = umulh(ph, ra) << 6              (0 .. 2^31)
 *               ph >= W: psi = (umulh(ph - W, rb) << 6) + 2^31 (2^31 .. 2^32)
 *             With ra = floor((2^32 - 1) 2^25 / W) and rb the same for
 *             2^32 - W (oscPwmTables.h), the first half-cycle of the table
 *             plays over [0, W) and the second over [W, 2^32), each to
 *             within 64 phase units (gen_pwm_tables.py T5). At W = 2^31 psi
 *             is ph to within 64 units.
 * Why:        the warp forms (WT: the triangle row; WS: the sine) of the
 *             drum-instrument pwm loops (Oscillator.c) and the Alkaline's
 *             fused ladder mode 4 (WT, DiodeLadder.c) must be the same law. Both halves are computed and one selected, so every
 *             width runs the same instructions (constant cost); the << 6 is
 *             exact for every reader (WT reads psi >> 7 and above, WS
 *             psi >> 5 and above).
 * Inputs:     ph, W (wPh), ra, rb.  Outputs: psi.
 * Accessors:  Oscillator.c (the WT and WS loops); DiodeLadder.c
 *             dl_renderB2Body() mode 4.
 * Affiliates: oscPwmTables.h (W, ra, rb per pos).
 */
#ifndef OSC_PWM_WARP_H_
#define OSC_PWM_WARP_H_

#include <stdint.h>

static inline __attribute__((always_inline)) uint32_t osc_pwmWarp(
	const uint32_t ph, const uint32_t wPh, const uint32_t ra, const uint32_t rb)
{
	/* first half-cycle: x 2^31 / W */
	const uint32_t lo = (uint32_t)(((uint64_t)ph * ra) >> 32) << 6;
	/* second half-cycle: x 2^31 / (2^32 - W), shifted up by half a cycle */
	const uint32_t hi = ((uint32_t)(((uint64_t)(ph - wPh) * rb) >> 32) << 6)
	                    + 0x80000000u;

	return (ph < wPh) ? lo : hi;
}

#endif /* OSC_PWM_WARP_H_ */
```

### 3.2 `Oscillator.c`

**(a) Include** (after `#include "oscPwmTables.h"`, l. 54):

```c
#include "oscPwmWarp.h"     /* S094 P4: the warp law shared with DiodeLadder.c */
```

**(b) Remove** `osc_pwmWarp()` and its comment block (l. 3741-3761). Leave
in its place:

```c
/* The warp (form W): osc_pwmWarp() in oscPwmWarp.h (S094 P4: shared with the
 * Alkaline's fused ladder mode 4). */
```

**(c) The section header** (l. 3598-3609). Replace the D / WT / WS lines:

```c
**               D   two reads of the descending saw row, d points apart
**                   (oscPwmDRec / oscPwmDSaw[pos]), summed (Saw, sign +1) or
**                   differenced (Rec, sign -1): a band-limited two-saw or
**                   pulse, because each read is a band-limited saw;
**               WT  the triangle row, phase-warped: the rising half-cycle
**                   over [0, W), the falling one over [W, 2^32),
**                   W = oscPwmTriW[pos], with the octave lifted by
**                   oscPwmTriLift[pos] for the compressed half;
**               WS  the same warp on sine_table (W = oscPwmSinW[pos]; a
**                   -cos: trough first, so its halves are the rising and
**                   falling ones and the warped wave has no DC; plan
**                   finding 1).
**             S094 P4 law: pos 0 is the unmodified wave (width 1/2), pos 127
**             the narrowest (Rec 0.39 %, Saw and Tri 2.54 %, Sin 6.25 %),
**             linear in width (oscPwmTables.h). Under S094 P1 the width was
**             pos/128 about 64, and p and 128 - p played the same spectrum.
```

**(d) `osc_pwmPlan_t`'s comment** (l. 3643-3654). Replace the "What" line
for `wPh`, `ra`, `rb` and `d`:

```c
 * What:       row (D, WT: the 1,024-point row; WS: unused), wPh (the warp's
 *             split point W), ra / rb (the half-cycle scales, oscPwmTables.h),
 *             d (D: the second read's offset in points of 1,024), sign (D:
 *             +1 sum, -1 difference), gQ14 (the store gain in Q14: level x
 *             render gain), form. Every value comes from the pos-indexed
 *             tables (S094 P4); fields a form does not read are left 0.
```

**(e) `osc_pwmPlan()`** (l. 3666-3736). The comment block and body are
replaced; the signature and attributes are unchanged.

```c
/*
 * Build one pwm block's plan (S094 P1 S3; S094 P4 law).
 *
 * What:       k = positionApplied (0..127, the width after the slew); by wav:
 *               Sin -> WS: W, ra, rb from oscPwmSin*[k], at gain;
 *               Tri -> WT on triTable[min(tableOffset + oscPwmTriLift[k],
 *                      10)], W, ra, rb from oscPwmTri*[k], at gain;
 *               Saw -> D sum on sawTable[tableOffset], d = oscPwmDSaw[k], at
 *                      gain x oscPwmLevelSawQ14[k] / 16384;
 *               Rec (and any larger value) -> D difference, d =
 *                      oscPwmDRec[k], at gain x oscPwmLevelRecQ14[k] / 16384.
 * Why:        S094 P4 (user): pos 0 is the unmodified wave and pos 127 the
 *             narrowest, with no wrap: every constant is a lookup by k, so
 *             the law lives in one generator (gen_pwm_tables.py) and the
 *             loops are unchanged. Rec narrows to 0.39 %, Saw and Tri to
 *             2.54 %, Sin to 6.25 % (its warp has no octave rows; a narrower
 *             half-cycle aliases). The S094 P1 clamp (pos 0 acts as 1) and
 *             the mirrored index (128 - p) are gone: pos 0 is a width of its
 *             own, bit-exact with the S094 P1 p = 64 (gen T6, law PW9).
 * Inputs:     osc (waveform, tableOffset from osc_userBind(), positionApplied
 *             from osc_typedPositionApply()), gain (0..1 at every call site).
 * Outputs:    *pl.
 * Cost:       per block, flash: one form's table loads and one multiply; no
 *             division.
 * Accessors:  the five osc_pwmRender*() roles.
 * Affiliates: oscPwmTables.h; oscPwmWarp.h; AlkalineVoice.c alk_renderPwm()
 *             (the same tables for the Alkaline).
 */
static __attribute__((noinline, noclone)) void osc_pwmPlan(const OscInfo* osc,
                                                           float gain,
                                                           osc_pwmPlan_t* pl)
{
	/* the applied width index, 0..127 (S094 P4: no clamp, no mirror) */
	const uint32_t k = (uint32_t)(osc->positionApplied & 127u);

	pl->wPh = 0u;
	pl->ra = 0u;
	pl->rb = 0u;
	pl->d = 0u;
	pl->sign = 0;
	switch (osc->waveform) {
	case SINE:
		pl->form = OSC_PWM_FORM_WS;
		pl->row = 0;
		pl->wPh = oscPwmSinW[k];
		pl->ra = oscPwmSinRa[k];
		pl->rb = oscPwmSinRb[k];
		pl->gQ14 = (int32_t)(gain * 16384.0f + 0.5f);
		break;
	case TRI: {
		/* the octave row, lifted for the compressed half-cycle */
		uint32_t r = (uint32_t)osc->tableOffset + oscPwmTriLift[k];

		if (r > 10u)
			r = 10u;
		pl->form = OSC_PWM_FORM_WT;
		pl->row = triTable[r];
		pl->wPh = oscPwmTriW[k];
		pl->ra = oscPwmTriRa[k];
		pl->rb = oscPwmTriRb[k];
		pl->gQ14 = (int32_t)(gain * 16384.0f + 0.5f);
		break; }
	case SAW:
		pl->form = OSC_PWM_FORM_D;
		pl->row = sawTable[osc->tableOffset];
		pl->d = oscPwmDSaw[k];
		pl->sign = 1;
		pl->gQ14 = (int32_t)(gain * (float)oscPwmLevelSawQ14[k] + 0.5f);
		break;
	default:   /* REC; a stored value past Rec plays Rec (S087 r8) */
		pl->form = OSC_PWM_FORM_D;
		pl->row = sawTable[osc->tableOffset];
		pl->d = oscPwmDRec[k];
		pl->sign = -1;
		pl->gQ14 = (int32_t)(gain * (float)oscPwmLevelRecQ14[k] + 0.5f);
		break;
	}
}
```

**(f) `osc_typedPositionApply()`'s PWM comment** (l. 1586-1597): add one
line to "What":

```c
	 *             S094 P4: pos 0..127 is a monotonic width index (0 = the
	 *             unmodified wave, 127 = the narrowest; osc_pwmPlan()).
```

### 3.3 `Oscillator.h`

**`position` comment** (l. 248-250): replace "under typ pwm (S094 P1) it is
the pulse width (1..127; 0 acts as 1)" with:

```c
	 *             under typ pwm it is the width index (S094 P4: 0 = the
	 *             unmodified wave, 127 = the narrowest; oscPwmTables.h).
```

### 3.4 Check after S2

- `make all`;
- `make -C tools/dsp_test elf_p4_check` (or the `P4_OSC_SYMS` dump against
  the S094 P2 image): every `osc_pwm*Block` loop's listing is unchanged;
- `osc_pwmPlan` grows by the larger switch (flash only).

---

## 4. S3 — Oscillator host laws (`tools/dsp_test/`)

### 4.1 Host build of the new header

`Makefile` (beside the `oscPwmTables.host.h` rule, l. 77):

```make
# S094 P4: the shared pwm warp (Oscillator.c and DiodeLadder.c include it).
$(GEN)/oscPwmWarp.host.h: $(ROOT)/Core/DSPAudio/oscPwmWarp.h | $(GEN)
	$(EX) --src $< --body --out $@
```

- Add `$(GEN)/oscPwmWarp.host.h` to the prerequisites of `oscillator`,
  `alkaline` and `pwm_dump`.
- `osc_host.h` (l. 60): include it before `Oscillator.host.h`:

```c
/* S094 P4: the shared pwm warp (oscPwmWarp.h), used by Oscillator.c. */
#include "oscPwmWarp.host.h"
```

### 4.2 The laws (`modules/oscillator.c`, group `pwm`)

Replace the group's header comment (l. 1318-1346) with:

```c
/* ================================================================ pwm --
 * S094 P1 S3 laws for typ pwm, re-based on the S094 P4 law (pos 0 = the
 * unmodified wave, pos 127 = the narrowest; oscPwmTables.h).
 *   PW1  pos 0: WS equals calcSineBlock() and WT equals the stock table read
 *        (calcWavetableOscBlock()) within 2 LSB (integer against float
 *        interpolation) at gain 1;
 *   PW2  D, both signs, pos in {0, 1, 16, 63, 64, 100, 127}, three phases
 *        and increments: bit-exact against an independent reader written
 *        here, which takes d from the wave's table (oscPwmDRec / DSaw);
 *   PW3  orientation on an ideal descending saw: the difference is high for
 *        the first d points and low after, pos {0, 64, 120}; the sum and the
 *        difference peak at the table's level, pos {0, 64, 127};
 *   PW4  WT on an ideal triangle: the peak sits at oscPwmTriW[pos] (within
 *        one table point), pos {0, 32, 64, 96, 127};
 *   PW5  the width slew (unchanged);
 *   PW6  depth 0 identities (unchanged);
 *   PW7  routing and the itp old carrier (unchanged);
 *   PW8  type and bind (unchanged);
 *   PW9  S094 P4: the tables at pos 0 are the S094 P1 width-1/2 constants
 *        (d 512, levels 16384, W 2^31, ra = rb = 0xFFFFFFFF / 64, lift 0);
 *   PW10 S094 P4: the law is monotonic and not mirrored: on the shape
 *        tables, the Rec high fraction falls strictly over pos {0, 32, 64,
 *        96, 127} from 1/2 to 4/1,024 (within one point), and the Sin peak
 *        moves strictly earlier;
 * Break builds: break_pwm1 (forward second read), break_pwm2 (unlimited
 * slew), break_pwm3 (S094 P4: Rec planned on the Saw width table; fails
 * PW2 and PW10).
 */
```

**Code changes:**
- **`pwm_refD()`** (l. 1360): the offset becomes
  `const uint32_t w = (sign < 0) ? oscPwmDRec[width] : oscPwmDSaw[width];`
  with no clamp.
- **`pwm_d_exact()`:**
  - widths `{ 0u, 1u, 16u, 63u, 64u, 100u, 127u }`;
  - `g = (s ? oscPwmLevelRecQ14 : oscPwmLevelSawQ14)[widths[w]]`. The `m`
    line goes.
- **`pwm_shapes()`:**
  - PW1 at `pos` 0 (`pwm_osc(SINE, 0u, …)`, `pwm_osc(TRI, 0u, …)`);
  - PW3 orientation: widths `{ 0u, 64u, 120u }`, with
    `edge = (uint32_t)oscPwmDRec[widths[w]] << 22`;
  - PW3 peak: widths `{ 0u, 64u, 127u }`;
  - PW4: widths `{ 0u, 32u, 64u, 96u, 127u }`, with the expected point
    `oscPwmTriW[widths[w]] >> 22`.
- **`pwm_law()`, new (PW9, PW10)**, called from `run_pwm()` after
  `pwm_shapes()`:

```c
/*
 * S094 P4 laws: the pos-0 identity and the monotonic, unmirrored law.
 *
 * What:       PW9 compares the tables at pos 0 with the S094 P1 constants of
 *             width 1/2. PW10 renders Rec at 1,024 samples per cycle on the
 *             ideal saw and measures the fraction of the cycle above zero at
 *             pos 0, 32, 64, 96, 127: it must fall strictly, from 512 to 4
 *             points (within one point each); Sin's peak phase must move
 *             strictly earlier over the same pos values.
 * Why:        the user's complaint (P4 investigation §0): the S094 P1 law
 *             folded at 64. A mirror (p against 128 - p) fails PW10.
 * Inputs:     the shape tables (fill_shape_tables()).
 * Accessors:  run_pwm().
 */
static void pwm_law(void)
```

### 4.3 Break build `break_pwm3`

In the Makefile, beside `break_pwm1` / `break_pwm2`, in the same pattern:

```make
# S094 P4 break: Rec planned on the Saw width table must fail PW2 and PW10.
	mkdir -p build/break_pwm3
	sed 's/pl->d = oscPwmDRec\[k\];/pl->d = oscPwmDSaw[k];/' \
	  $(GEN)/Oscillator.host.c > build/break_pwm3/Oscillator.host.c
	@grep -q 'pl->d = oscPwmDSaw\[k\];' build/break_pwm3/Oscillator.host.c || \
	  { echo "oscillator_break_pwm3: sed did not apply"; exit 1; }
	$(HOSTCC) -Ibuild/break_pwm3 $(HOSTFLAGS) $(OSC_HOST_INC) modules/oscillator.c \
	  $(CAT_HOST_SRC) -o build/oscillator_break_pwm3 -lm
	@if ./build/oscillator_break_pwm3 >/dev/null 2>&1; then \
	  echo "oscillator_break_pwm3: FAILED (Rec width table break not caught)"; exit 1; \
	else echo "oscillator_break_pwm3: caught (laws PW2/PW10 fail as expected)"; fi
```

`break_pwm1`'s `sed` target (`(ia - d) & 0x3FFu` in `osc_pwmReadD()`) is
unchanged.

---

## 5. S4 — Spike: the Alkaline warp mode (gate)

- **Where:** a scratch copy of the tree after S1-S3. It carries S5's mode 4
  and its wrapper, and S6's `alk_renderPwm()` dispatch, with the Tri scale
  at `ALK_SAW_SCALE_V`.
- **Measure:**
  - `elf_looppath.py --cond-heads --in-range` on
    `DiodeLadder_renderB2OscPwmWt` (the audio loop, rank 1, 3 VDIV);
  - `tools/alkaline/control_cost.py --elf` for the per block (with S8's
    ITERS entry);
  - the existing wrappers (`…Osc` 221, `…Osc2` 234, `…OscPwm` 215, from
    the S094 P2 §1 base).
- **Pass:** the new loop ≤ 234 per sample, and loop + per block / 32 ≤
  276.9. The existing three loops must be unchanged.
- **Estimate (not measured):** mode 1's read (221) plus the warp (two
  `umull`, a subtract, an add, a compare and select: about 6-8) is about
  227-229. It is at risk only if the three warp constants spill (the S4
  lesson: three live values cost +8).
- **If it fails:**
  1. **First fallback:** hold `ra` and `rb` in one 64-bit pair.
  2. **Second:** the triangle read truncated (as mode 3's form 2): −5 to −8.
  3. **If both fail:** report to the user before S5. The Alkaline `pwm` Tri
     is dropped from this build; `wav` Tri still goes in.
- **Record:** the counts go into `tools/dsp_test/baselines/s094p4/spike.txt`.

---

## 6. S5 — `DiodeLadder.c/.h`: fused mode 4

### 6.1 `DiodeLadder.c`

**(a) Include** (after `#include "stm32f4xx.h"`, l. 50):

```c
#include "oscPwmWarp.h"   /* S094 P4: the pwm warp, as the drum loops */
```

**(b) `dl_renderB2Body()` signature** (l. 302-311): three parameters are
appended.

```c
	const uint32_t pwD, const float pwSign,
	const uint32_t wPh, const uint32_t wRa, const uint32_t wRb)
```

**(c) The `fused` comment** (l. 313-324): append mode 4 and the parameter
rule.

```c
	 *   4  S094 P4: pwm Tri, the warp: the 1,024-point row (the caller's
	 *      lifted triTable row) read at psi = osc_pwmWarp(ph, wPh, wRa, wRb),
	 *      interpolated in float (DiodeLadder_renderB2OscPwmWt()).
	 * rowB, ff0 and ffStep are read only under 2, pwD and pwSign only under
	 * 3, wPh, wRa and wRb only under 4; the other wrappers pass 0 / 0.0f and
	 * the compiler drops them with the branch. */
```

**(d) The loop:** one branch goes after `fused == 3` (l. 397-435) and
before `else if (fused)`.

```c
		} else if (fused == 4) {
			/*
			 * S094 P4: the Alkaline pwm Tri, the warp.
			 *
			 * What:       psi = osc_pwmWarp(ph, wPh, wRa, wRb) (oscPwmWarp.h,
			 *             the drum loops' law): the rising half-cycle of the
			 *             row plays over [0, W), the falling one over
			 *             [W, 2^32). The row is read at psi >> 22 and its
			 *             successor (point 1,023 wraps to 0), with the
			 *             fraction from the low 22 bits, interpolated in float
			 *             with no int16 truncation (as mode 2: no bit-identity
			 *             law here); s = value x sScale.
			 * Why:        the Alkaline's pwm Tri (user, S094 P4): the drum
			 *             instruments' WT form inside the fused loop, so the
			 *             voice keeps one loop and no buffer. The caller
			 *             passes the lifted row (oscPwmTriLift). Sin is not
			 *             offered on the Alkaline (user): the ladder's
			 *             low-pass makes a warped sine and a warped triangle
			 *             practically the same.
			 * Cost:       measured in S4 (gate: <= 234 per sample, the wtb
			 *             slot path's loop).
			 */
			const uint32_t ps = osc_pwmWarp(ph, wPh, wRa, wRb);
			const uint32_t itg = ps >> 22;
			const float a = (float)row[itg];
			const float b = (float)row[(itg + 1u) & 0x000003FFu];
			const float fr = (float)(ps & 0x003FFFFFu) * 2.384185791015625e-07f;

			ph += phaseInc;
			s = (a + fr * (b - a)) * sScale;
```

**(e) The existing wrappers** pass the three new arguments as zero:
- `DiodeLadder_renderB2()` (l. 557): `…, 0u, 0.0f, 0u, 0u, 0u);`
- `DiodeLadder_renderB2Osc()` (l. 568): `…, 0u, 0.0f, 0u, 0u, 0u);`
- `DiodeLadder_renderB2Osc2()` (l. 605): `…, 0u, 0.0f, 0u, 0u, 0u);`
- `DiodeLadder_renderB2OscPwm()` (l. 637): `…, pwD, pwSign, 0u, 0u, 0u);`

**(f) The new wrapper,** after `DiodeLadder_renderB2OscPwm()`:

```c
/*
 * B2R with the pwm warp fused: Tri (S094 P4, fused mode 4).
 *
 * What:       the fused loop with the input s_j = T(psi_j) x sScale: T the
 *             1,024-point row (the caller's lifted triTable row) linearly
 *             interpolated in float at psi_j = osc_pwmWarp(ph_j, wPh, wRa,
 *             wRb); the phase advances by phaseInc per sample and is written
 *             back.
 * Why:        the Alkaline's pwm Tri (user, S094 P4): the drum instruments'
 *             WT form in the ladder loop. noinline, as the other wrappers:
 *             its registers stay out of the caller and its only loop is the
 *             audio loop (control_cost.py weight 0).
 * Inputs:     dl (the ladder; DiodeLadder_setNet() and DiodeLadder_setPost()
 *             ran this block), buf (output only), row (a 1,024-point row),
 *             phase (in/out), phaseInc, wPh / wRa / wRb (oscPwmTriW / Ra /
 *             Rb[pos]), sScale (the scale x the drive), iLadEnd, iAbcEnd (as
 *             the other B2R wrappers).
 * Outputs:    buf (int16, the ladder's toned VCA output); *phase; the ladder
 *             state.
 * Cost:       S4 measurement (gate <= 234 per sample).
 * Accessors:  AlkalineVoice.c alk_renderPwm().
 * Affiliates: oscPwmWarp.h; bench law AL-PWW.
 */
__attribute__((noinline)) void DiodeLadder_renderB2OscPwmWt(DiodeLadder *dl, int16_t *buf,
                              const int16_t *row, uint32_t *phase,
                              const uint32_t phaseInc, const uint32_t wPh,
                              const uint32_t wRa, const uint32_t wRb,
                              const float sScale,
                              const float iLadEnd, const float iAbcEnd)
{
	dl_renderB2Body(dl, buf, sScale, iLadEnd, iAbcEnd, 4, row, phase, phaseInc,
	                0, 0.0f, 0.0f, 0u, 0.0f, wPh, wRa, wRb);
}
```

### 6.2 `DiodeLadder.h`

After the `DiodeLadder_renderB2OscPwm()` prototype (l. 223-226):

```c
/*
 * B2R with the pwm warp fused (S094 P4): Tri (mode 4), a 1,024-point row
 * read at psi = osc_pwmWarp(ph, wPh, wRa, wRb) (oscPwmWarp.h) and
 * interpolated in float, s = value x sScale. Contract in DiodeLadder.c.
 * Accessors: AlkalineVoice.c alk_renderPwm().
 */
void DiodeLadder_renderB2OscPwmWt(DiodeLadder *dl, int16_t *buf, const int16_t *row,
                                  uint32_t *phase, uint32_t phaseInc, uint32_t wPh,
                                  uint32_t wRa, uint32_t wRb, float sScale,
                                  float iLadEnd, float iAbcEnd);
```

---

## 7. S6 — `AlkalineVoice.h/.c`

### 7.1 `AlkalineVoice.h` (l. 57-61)

```c
/*
 * osc.waveform: the Alkaline wave list (S093 P8 row osc1_wave; S094 P4).
 *
 * What:       under typ wav: 0 Saw, 1 Sqr (the 303 square table), 2 Tri (the
 *             stock triangle rows). Under typ pwm: 0 Saw (two-saw sum), 1 Sqr
 *             (the band-limited pulse: the stock Rec's pwm form), 2 Tri (the
 *             warped triangle). ALK_WAVE_COUNT entries under both. Under typ
 *             wtb the same byte is the wavetable index.
 * Why:        S094 P4 (user): the triangle is added under both types; no
 *             Rec, Sin or Noi under wav, and no Sin under pwm (the ladder's
 *             low-pass makes a warped sine and a warped triangle practically
 *             the same). Rec under pwm is Sqr; noise has no pwm form.
 *             Appended, so stored Kits keep their wave; a type switch keeps
 *             the value (one list size).
 * Accessors:  alk_renderWav(), alk_renderPwm(), Alkaline_setOscType(),
 *             InstrumentManager.c instrumentManager_waveDomainMax(),
 *             MenuText.h alkWaveNames (ALK_WAVE_COUNT names).
 */
#define ALK_WAVE_SAW          0u
#define ALK_WAVE_SQUARE       1u
#define ALK_WAVE_TRI          2u
#define ALK_WAVE_COUNT        3u   /* Saw Sqr Tri, under wav and pwm */
```

**The typ comment** (l. 64-75): "0 wav (Saw/Sqr, the 303's VCO)" becomes
"0 wav (Saw/Sqr, the 303's VCO; S094 P4 + Tri)", and "2 pwm (S094 P1:
Saw = two-saw sum, Sqr = pulse, pos = width)" becomes "2 pwm (Saw two-saw
sum, Sqr pulse, S094 P4 + Tri; pos = width index, 0 = 50 %)".

### 7.2 `AlkalineVoice.c`: the Tri scale (after `ALK_PWM_SQR_SCALE_V`, l. 42)

```c
/*
 * The triangle's input scale (S094 P4).
 *
 * What:       volts per LSB into the ladder for Tri (typ wav and pwm): the
 *             saw's scale x the RMS ratio, so a full-scale Tri drives the
 *             ladder as hard as the Saw does. The triangle's RMS is
 *             1/sqrt(3) of its peak, the saw's own, so the factor starts at
 *             1.0; confirmed by the bench print (AL-PW: Tri against Saw
 *             under wav, within 0.5 dB; S094 P4 S9).
 * Why:        the drive, the overdrive and the ladder's own nonlinearity
 *             react to level; a matched RMS keeps every wave's knob ranges
 *             alike (as ALK_PWM_SQR_SCALE_V did for the pulse).
 * Accessors:  alk_renderWav(), alk_renderPwm().
 */
#define ALK_TRI_SCALE_V  (ALK_SAW_SCALE_V * 1.0f)
```

### 7.3 `alk_renderWav()` (l. 526-575): Tri under `wav`

**The comment block's "What":** "the octave's row of the stock Saw or of
the D8 square" becomes:

```c
 * What:       the pre-P8 sync block body: the octave's row of the stock Saw,
 *             of the D8 square, or (S094 P4) of the stock triangle
 *             (tableOffset, set by osc_setTableFreq() in the async), each at
 *             its own scale, times the drive. Saw and Sqr are unchanged bit
 *             for bit (AL-X); Tri uses the same fused loop (AL-XT).
```

**The body:**

```c
static __attribute__((noinline)) void alk_renderWav(AlkalineVoice *voice, int16_t *buf)
{
	/* S093 P8: Saw/Sqr is osc.waveform under typ wav; S094 P4: Tri (2) */
	const uint8_t wave = voice->osc.waveform;
	const int16_t (*const table)[1024] =
		(wave == ALK_WAVE_SQUARE) ? alkSqrTable
		: ((wave == ALK_WAVE_TRI) ? (const int16_t (*)[1024])triTable
		                          : (const int16_t (*)[1024])sawTable);
	const float scale = (wave == ALK_WAVE_SQUARE) ? ALK_SQR_SCALE_V
	                  : ((wave == ALK_WAVE_TRI) ? ALK_TRI_SCALE_V : ALK_SAW_SCALE_V);

#if ALK_FUSED_OSC
	DiodeLadder_renderB2Osc(&voice->ladder, buf, table[voice->osc.tableOffset],
	                        &voice->osc.phase, voice->osc.phaseInc,
	                        scale * voice->driveGain,
	                        voice->iLadEnd, voice->iAbcEnd * voice->makeup);
#else
	calcWavetableOscBlock(&voice->osc, table, buf, OUTPUT_DMA_SIZE, 1.0f);
	DiodeLadder_renderB2(&voice->ladder, buf, scale * voice->driveGain,
	                     voice->iLadEnd, voice->iAbcEnd * voice->makeup);
#endif
}
```

The two existing comments inside the body (B2R, and S093 P3) are kept
above the `#if`, unchanged.

**Check:**
- AL-X must stay bit-identical for Saw and Sqr.
- `control_cost.py` per block for `alk_renderWav` (it is per block; the
  select is two compares).
- The S093 P8 note on this wrapper (the loop moved 214 → 216 when the row
  became a pointer read from the oscillator) concerns the loop, which stays
  out of line in `DiodeLadder_renderB2Osc()`. Re-check that the
  `DiodeLadder_renderB2Osc` loop is unchanged (221).

### 7.4 `alk_renderPwm()` (l. 664-713): the law and the dispatch

```c
/*
 * One block of typ pwm (S094 P1 S5; S094 P4 law and Tri).
 *
 * What:       k = positionApplied (0..127: 0 the unmodified wave, 127 the
 *             narrowest; oscPwmTables.h). By wave:
 *               Saw -> mode 3 (DiodeLadder_renderB2OscPwm()) on the octave's
 *                      sawTable row, d = oscPwmDSaw[k], summed, at
 *                      ALK_SAW_SCALE_V x oscPwmLevelSawQ14[k] / 16384;
 *               Sqr -> mode 3, d = oscPwmDRec[k] (down to 0.39 %),
 *                      differenced, at ALK_PWM_SQR_SCALE_V x
 *                      oscPwmLevelRecQ14[k] / 16384;
 *               Tri -> mode 4 (DiodeLadder_renderB2OscPwmWt()) on
 *                      triTable[min(tableOffset + oscPwmTriLift[k], 10)],
 *                      W / ra / rb = oscPwmTri*[k], at ALK_TRI_SCALE_V;
 *             each times the drive.
 * Why:        S094 P4 (user): the same law as the drum instruments (pos 0 =
 *             50 %, no wrap; Sqr to 0.39 %, Saw and Tri to 2.54 %) and the
 *             triangle's warp form. No Sin: the ladder's low-pass makes a
 *             warped sine and a warped triangle practically the same (user).
 *             noinline, as alk_renderWav() and alk_renderWtb(): each type's
 *             render keeps its registers to itself.
 * Inputs:     voice (the async ran this block: phaseInc, tableOffset,
 *             positionApplied, driveGain, makeup, the post-filter controls);
 *             buf (output).
 * Outputs:    buf; osc.phase; the ladder state.
 * Accessors:  Alkaline_calcVoiceSyncBlock().
 * Affiliates: DiodeLadder_renderB2OscPwm/Wt(); oscPwmTables.h; the drum
 *             instruments' osc_pwmPlan() (the same tables); bench laws AL-PW,
 *             AL-PWW.
 */
static __attribute__((noinline)) void alk_renderPwm(AlkalineVoice *voice, int16_t *buf)
{
	OscInfo *const osc = &voice->osc;
	/* the applied width index, 0..127 (S094 P4: no clamp, no mirror) */
	const uint32_t k = (uint32_t)(osc->positionApplied & 127u);
	const float iAbc = voice->iAbcEnd * voice->makeup;

	if (osc->waveform == ALK_WAVE_TRI) {
		uint32_t r = (uint32_t)osc->tableOffset + oscPwmTriLift[k];

		if (r > 10u)
			r = 10u;
		DiodeLadder_renderB2OscPwmWt(&voice->ladder, buf, triTable[r],
		                             &osc->phase, osc->phaseInc,
		                             oscPwmTriW[k], oscPwmTriRa[k], oscPwmTriRb[k],
		                             ALK_TRI_SCALE_V * voice->driveGain,
		                             voice->iLadEnd, iAbc);
	} else {
		/* Saw (sum) or Sqr (difference): the S094 P1 S5 mode 3 */
		const uint8_t square = (uint8_t)(osc->waveform == ALK_WAVE_SQUARE);
		/* the level 0.5..1 (Q14 / 16384: a multiply, never a VDIV) */
		const float level = (float)(square ? oscPwmLevelRecQ14[k]
		                                   : oscPwmLevelSawQ14[k]) * (1.0f / 16384.0f);

		DiodeLadder_renderB2OscPwm(&voice->ladder, buf, sawTable[osc->tableOffset],
		                           &osc->phase, osc->phaseInc,
		                           square ? oscPwmDRec[k] : oscPwmDSaw[k],
		                           square ? -1.0f : 1.0f,
		                           (square ? ALK_PWM_SQR_SCALE_V : ALK_SAW_SCALE_V)
		                               * level * voice->driveGain,
		                           voice->iLadEnd, iAbc);
	}
}
```

- **The level mapping:** `ALK_PWM_SQR_SCALE_V` (×1.40) was set at the old
  `pos` 64. The new `pos` 0 is that width, so the AL-PW level law moves to
  `pos` 0 unchanged.
- **The P-D6 read-form note** (`DiodeLadder.c:86-110`) still applies to
  mode 3. `pwD` now ranges 4..512, where the old comment says
  "8..1,016 points" (`DiodeLadder.c:622`). Correct that line to: "pwD
  (oscPwmDRec / DSaw[pos], 4..512 points)".

### 7.5 `Alkaline_setOscType()` (l. 746-777): the list size

Replace the `count` lines and their comment:

```c
	/* the number of entries of the new type's wav list (S094 P4): Saw Sqr
	 * Tri under wav and pwm, the installed tables under wtb; a stored byte
	 * past the list clamps to its last entry */
	const uint8_t count = (type == OSC_TYPE_WTB) ? sampleMemory_getNumWavetables()
	                    : (uint8_t)ALK_WAVE_COUNT;
```

### 7.6 Comments that name "sawTable rows" / "Saw/Sqr" under `pwm`

- **Trigger** (l. 317-322): "S094 P1 S5: typ pwm snaps the width to pos"
  becomes "typ pwm snaps the width index to pos (S094 P4: 0 = 50 %)".
- **Async** (l. 434-445):
  - "osc_setTableFreq() (phaseInc and the octave row, as wav: the pwm
    reads are sawTable rows)" becomes "(phaseInc and the octave row, as
    wav: the octave row of sawTable, or of triTable before its lift)";
  - add "pos is a monotonic width index (S094 P4)".
- **`Alkaline_calcVoiceSyncBlock()`** (l. 722-726): "pwm ->
  alk_renderPwm()" becomes "pwm -> alk_renderPwm() (Saw/Sqr mode 3, Tri
  mode 4)".

---

## 8. S7 — Menu and InstrumentManager

### 8.1 `MenuText.h` (l. 255-268)

```c
/*
 * Alkaline waveform names (S093 P4; S093 P8: the osc1_wave cell under typ
 * wav; S094 P4: Tri, under wav and pwm).
 *
 * What:       [0] = count (3), then Saw, Sqr, Tri: ALK_WAVE_SAW 0,
 *             ALK_WAVE_SQUARE 1, ALK_WAVE_TRI 2, the same list under typ wav
 *             and pwm.
 * Why:        an Alkaline slot's wave list; menu_formatWaveCell() names it
 *             and instrumentManager_waveDomainMax() limits the range to it.
 *             The count must equal ALK_WAVE_COUNT (asserted in menu.c).
 * Accessors:  menu_formatWaveCell() (menu.c).
 */
static const char alkWaveNames[][4] = {
    {3}, {"Saw"},{"Sqr"},{"Tri"},
};
```

### 8.2 `menu.c`

**The assert** (l. 12545-12548):

```c
/* S093 P4 / S094 P4: the Alkaline waveform names list every stored value:
 * Saw, Sqr, Tri (ALK_WAVE_*, AlkalineVoice.h). */
_Static_assert((sizeof(alkWaveNames) / sizeof(alkWaveNames[0])) - 1u ==
                   ALK_WAVE_COUNT,
               "alkWaveNames must list Saw, Sqr and Tri");
```

**`menu_formatWaveCell()`'s Alkaline comment** (l. 12168-12181): "value
0 -> "Saw", 1 -> "Sqr"" becomes "value 0 -> "Saw", 1 -> "Sqr", 2 -> "Tri"
(S094 P4)". The one-line comment above the `if` ("S094 P1 S5: under pwm
the Alkaline's wav list is still Saw, Sqr") becomes:

```c
    /* S094 P4: the Alkaline lists Saw Sqr Tri under both wav and pwm */
```

The code is unchanged: it already indexes `alkWaveNames` by `value <
count`, under `wav` and `pwm`.

### 8.3 `InstrumentManager.c` `instrumentManager_waveDomainMax()` (l. 823-858)

The function comment's "an Alkaline under wav: ALK_WAVE_SQUARE (Saw, Sqr)"
becomes "an Alkaline under wav or pwm: ALK_WAVE_COUNT − 1 (Saw, Sqr,
Tri)". The branch keeps its condition; the comment and the return change:

```c
            /* S093 P8 / S094 P4: an Alkaline slot under wav or pwm lists
             * Saw, Sqr, Tri. The LFO/velocity range, menu clamp, edit count
             * and typ switch clamp all take it from here. */
            if (osc && (osc->oscType == OSC_TYPE_WAV || osc->oscType == OSC_TYPE_PWM) &&
                instrumentManager_slotType(slot) == INSTRUMENT_TYPE_ALK)
                return (uint8_t)(ALK_WAVE_COUNT - 1u);
```

### 8.4 Check

- `make -C tools/dsp_test special_tags` (the descriptors are unchanged);
- `make_init_alk --check`: the init Kit keeps Saw / `wav`, so it is
  unchanged.

---

## 9. S8 — Alkaline bench and tools

### 9.1 The real triangle in the bench (`Makefile` `alkaline`, `modules/alkaline.c`)

The bench's `triTable` is a zero stand-in today (`alkaline.c` l. 97-99).
The Tri laws need the real one. In the `alkaline` recipe, beside the
`sawTable` extraction (l. 390-391):

```make
# S094 P4: the real triangle for the Tri laws (AL-XT, AL-PWW).
	$(EX) --src $(ROOT)/Core/DSPAudio/wavetable.c --object triTable --out $(GEN)/alk_triTable.raw.c
	sed 's/const int16_t triTable/int16_t triTable/' $(GEN)/alk_triTable.raw.c > $(GEN)/alk_triTable.c
```

In `alkaline.c`, the stand-ins (l. 96-99) become:

```c
/* the tables the whole Oscillator.c references: the real triangle since
 * S094 P4 (the Tri laws); sine_table and recTable stay zero stand-ins */
int16_t sine_table[TABLESIZE + 1];
#include "alk_triTable.c"
int16_t recTable[11][1024];
```

Also add `#include "oscPwmWarp.host.h"` before `DiodeLadder.host.h`, with
the comment "S094 P4: the warp DiodeLadder.c mode 4 uses".

### 9.2 Law changes

**AL-TY** (`test_typ()`, l. 1427-1467). Wav and pwm both now clamp 9 →
Tri, and a new check covers a type switch keeping Tri:

```c
	if (v.osc.waveform != ALK_WAVE_TRI) fail(G_TY, "wav clamps to Tri", v.osc.waveform, 0);
	…
	if (v.osc.waveform != ALK_WAVE_TRI) fail(G_TY, "pwm clamps to Tri", v.osc.waveform, 0);
	/* S094 P4: Tri (2) is in both lists: pwm Tri -> wav stays Tri */
	v.osc.waveform = ALK_WAVE_TRI;
	Alkaline_setOscType(&v, ALK_TYP_WAV);
	if (v.osc.waveform != ALK_WAVE_TRI) fail(G_TY, "pwm Tri -> wav Tri", v.osc.waveform, 0);
```

The header comment (l. 70-73) becomes "wav and pwm clamp to Tri, wtb to the
last table".

**AL-PW** (`test_pwm()`, l. 1641-1735):
- the bounds loop: waves Saw, Sqr, Tri at `pos` {0, 64, 127};
- the level law at `pos` 0 (was 64): `pw_rms(ALK_WAVE_SQUARE, ALK_TYP_PWM,
  0u)`;
- new print and check: Tri under `wav` against Saw under `wav`, within
  ±0.5 dB after S9 (the scale);
- the P-D6 reference render (`pw_render()`, Sqr `pos` 32 / Saw `pos` 96)
  is unchanged: both builds use the new law.

The header comment (l. 1600-1612) gets matching lines.

**AL-XT, new** (Tri under `wav`; beside AL-X):

```c
/* ---------------------------------------------------------------- AL-XT
 * S094 P4: the Tri wav read. The fused loop (DiodeLadder_renderB2Osc on the
 * triTable row) against the separate path (calcWavetableOscBlock() on
 * triTable, then DiodeLadder_renderB2): bit-identical output, phase and
 * ladder state, as AL-X for Saw and Sqr, over the same pattern, drive x1
 * and x8; and alk_renderWav() under ALK_WAVE_TRI equals the fused call. */
```

**AL-PWW, new** (Tri under `pwm`):

```c
/* ---------------------------------------------------------------- AL-PWW
 * S094 P4: the Alkaline pwm Tri (fused mode 4).
 *   - against an unfused reference: osc_pwmRenderPlain() (the drum
 *     instruments' WT loop at gain 1 on the same lifted row, W, ra, rb) into
 *     a buffer, then DiodeLadder_renderB2() at the same scale: the
 *     difference is at least 40 dB below the reference RMS at pos 0, 64 and
 *     127 (the reference interpolates in integers and the fused read in
 *     float, so they are not bit-identical);
 *   - pos 0 against the unwarped wave: Tri under pwm at pos 0 against Tri
 *     under wav (the fused one-row read), within 40 dB, the same way;
 *   - bounded and sounding at pos 0, 64 and 127, and the slew (as AL-PW).
 * A swapped ra / rb, a wrong lift or a wrong half-cycle offset moves the
 * peak and fails the first check at pos 64 and 127. */
```

### 9.3 Alkaline break builds (`Makefile` `alkaline`, the existing pattern)

- **`break_alk_pwmw`:** in the generated `DiodeLadder.host.c`, replace
  `osc_pwmWarp(ph, wPh, wRa, wRb)` with `osc_pwmWarp(ph, wPh, wRb, wRa)`.
  It must fail AL-PWW.
- **`break_alk_tri`:** in the generated `AlkalineVoice.host.c`, replace
  `(wave == ALK_WAVE_TRI) ? (const int16_t (*)[1024])triTable` with
  `(wave == ALK_WAVE_TRI) ? (const int16_t (*)[1024])sawTable`. It must
  fail AL-XT.

Each uses `grep -q` for the `sed` and the "caught" message, as
`break_alk` / `break_alk_wtb` do.

### 9.4 `tools/alkaline/control_cost.py`

**ITERS** (after `("DiodeLadder_renderB2OscPwm", 1): 0,`, l. 70):

```python
    # S094 P4: the pwm Tri warp copy (alk_renderPwm(), mode 4), noinline like
    # OscPwm: the only loop is the audio loop; weight 0, checked by the same
    # guard
    ("DiodeLadder_renderB2OscPwmWt", 1): 0,
```

**The guard tuple** (l. 242-243) gains `"DiodeLadder_renderB2OscPwmWt"`.

---

## 10. S9 — The Tri scale (measurement)

1. **Tri under `wav` against Saw under `wav`:** AL-PW prints the pattern's
   RMS ratio. At `ALK_TRI_SCALE_V = ALK_SAW_SCALE_V × 1.0` the expectation
   is about 0 dB, since the triangle's RMS equals the saw's. Adjust the
   factor to land within ±0.5 dB.
2. **Tri under `pwm` at `pos` 0** uses the same scale. AL-PWW checks that
   it matches `wav` Tri.
3. Record the factor and the printed ratio in §12 of this document. The
   final call is by ear on the device: the factor is one constant.

---

## 11. S10 — Build, gates and record

| Check | Expectation |
|---|---|
| `make all && make img` | builds; no new warnings |
| Flash | **about +5.2 KB** of the 66,636 B headroom (§11.1) |
| RAM, ITCM | unchanged (`link_budget.py`; ITCM 13,984 B) |
| P4 loop dumps | every `osc_pwm*Block` unchanged |
| Alkaline loops | `…Osc` 221, `…Osc2` 234, `…OscPwm` 215 unchanged; `…PwmWt` as S4 |
| `control_cost.py --elf` | Alkaline per block ≤ the S094 P2 figure + the dispatch (a few) |
| Host `make -C tools/dsp_test all` | 0 failures; breaks `break_pwm1-3`, `break_alk_pwmw`, `break_alk_tri` caught |
| `pwm_tables --check` | OK |
| Plots | `make -C tools/dsp_test pwm_dump && (cd tools/plots && python3 plot_s094p4_pwm.py)` re-render with the new law. The script's titles that assert the mirror (figure 2's suptitle) and the law figure's "today / proposed" are made data-driven or updated to "S094 P1 (old) / S094 P4 (implemented)" |
| Record | this document §12: S4's counts, S9's factors, the flash figures, the host summary |

### 11.1 Flash (estimate; S10 records the linked figure)

| Item | Bytes |
|---|---:|
| The eleven `pos` tables (option A, user: all tabled, no per-block division): Rec and Saw width and level 4 × 256; Tri split, two scales and lift 3 × 512 + 128; Sin split and two scales 3 × 512 | 4,224 |
| Less the three S094 P1 tables (`oscPwmRecip` 512, `oscPwmLevelQ14` 128, `oscPwmLift` 128) | −768 |
| **Tables, net** | **+3,456** |
| One ladder copy, `DiodeLadder_renderB2OscPwmWt()` (the existing copies: `…OscPwm` 1,368 B, `…Osc2` 1,484 B) | ≈ +1,400 |
| The larger `osc_pwmPlan()`, `alk_renderPwm()` and `alk_renderWav()` (per-block code) | ≈ +300 |
| **Total** | **≈ +5.2 KB** (headroom 66,636 → ≈ 61.5 KB) |

- **The Sin tables stay:** 1,536 B serve the drum instruments' `pwm` Sin.
  Dropping the Alkaline Sin removes only its ladder copy (about 1.4 KB) and
  a small branch.
- **This corrects the investigation's estimate:** P4 §4 / §9 said about
  +1.6 KB for the tables. The per-form limits need the eleven tables
  above.

**Device test (one session, user):**
- a width sweep `pos` 0 → 127 on each wave, on a drum instrument and on
  the Alkaline: no fold at 64, the narrowest at 127;
- the Alkaline `wav` Tri and `pwm` Tri;
- an LFO sweep on the width;
- the worst Scene with an Alkaline on `pwm` Tri, with the probe, against
  the Basic share (19,620).

---

## 12. Record (filled in during implementation)

| Step | Result |
|---|---|
| S1 | **done.** `tools/plots/gen_pwm_tables.py` rewritten to the pos-indexed law (D difference 512..4, D sum 512..26, Tri `w` to 26/1,024, Sin `w` to 1/16; 11 tables). `python3 tools/plots/gen_pwm_tables.py` writes `oscPwmTables.h/.c`; `--check` OK (T1-T7, T9 all pass). Reference values confirmed: Tri pos 127 W = 109,051,904, ra = 1,321,528,398, rb = 34,428,595, lift 4; Sin pos 127 W = 268,435,456, ra = 536,870,911, rb = 35,791,394. Pos 0 is bit-exact with S094 P1 `p` 64 (T6). |
| S2 | **done.** `Core/DSPAudio/oscPwmWarp.h` added (the shared warp, verbatim from `Oscillator.c`); `Oscillator.c` includes it, the local copy is gone, and `osc_pwmPlan()` is the pos-indexed switch on `oscPwmDRec`/`oscPwmDSaw`/`oscPwmLevelRecQ14`/`oscPwmLevelSawQ14`/`oscPwmTri*`/`oscPwmSin*` (no clamp, no mirror). Section header, `osc_pwmPlan_t` and `Oscillator.h`'s `position` comment updated. Firmware builds (with S5/S6 below, which the removed S094 P1 tables require). The fifteen `osc_pwm*Block` loops are untouched (S2 loop dump against `baselines/s094p1/s5`: 95 exact, 0 renamed, 0 missing). |
| S3 | **done.** Host build rule for `oscPwmWarp.host.h`; `osc_host.h` includes it; group `pwm` re-based (PW1/PW2/PW3/PW4 at the new tables, new `pwm_law()` PW9/PW10, new `break_pwm3`). `make -C tools/dsp_test oscillator`: 0 failures; `break_pwm1-3`, `break_fmr1-2` all caught. |
| S4 (gate) | **passed.** On the linked image: `DiodeLadder_renderB2OscPwmWt` audio loop **227** per sample, 3 VDIV (gate ≤ 234); the existing loops are unchanged (`...Osc` 221, `...Osc2` 234, `...OscPwm` 215). `control_cost.py --elf`: async 937 + sync block 416 + dispatch 14 = 1,367 per block (42.7 per sample), so the pwm Tri path is 227 + 42.7 = **269.7** per sample, below the 276.9 ceiling. No fallback needed. Record: `tools/dsp_test/baselines/s094p4/spike.txt`. |
| S5-S7 | **done.** `DiodeLadder.c/.h`: mode 4 in `dl_renderB2Body()`, the three new parameters, one new wrapper `DiodeLadder_renderB2OscPwmWt()`; existing wrappers pass zeros. `AlkalineVoice.h/.c`: `ALK_WAVE_TRI 2` / `ALK_WAVE_COUNT 3`, `ALK_TRI_SCALE_V`, Tri under `wav` (same fused loop) and `pwm` (mode 4), the list size `ALK_WAVE_COUNT`, comments updated. `MenuText.h`: `alkWaveNames` Saw/Sqr/Tri. `menu.c`: assert 3 and the Alkaline naming comment. `InstrumentManager.c`: `waveDomainMax` returns `ALK_WAVE_COUNT - 1` for an Alkaline under `wav`/`pwm`. Firmware builds. |
| S8 | **done.** Bench: real `triTable` extracted into `build/gen/alk_triTable.c`; AL-TY clamps both lists to Tri and checks pwm Tri -> wav Tri; AL-PW covers Saw/Sqr/Tri at pos 0/64/127; new AL-XT (Tri wav fused == separate, bit for bit, and `alk_renderWav()` == the fused call) and AL-PWW (fused mode 4 against the drum WT loop then the unfused ladder, and pos 0 against wav Tri). New breaks `break_alk_pwmw` (swapped ra/rb) and `break_alk_tri` (Tri read on the Saw row) both caught. `control_cost.py` ITERS/guard updated. Printouts: AL-PWW **-70.6 / -70.7 / -76.8 dB** (pos 0/64/127), pos 0 vs wav **-69.0 dB**. |
| S9 | **done, with one measured correction.** `ALK_TRI_SCALE_V = ALK_SAW_SCALE_V x 1.0` is kept. Measured at the point the scale is defined (the ladder input at the same octave row): **+0.13 dB** Tri against Saw (AL-PW, within the 0.5 dB bound). The *voice* RMS at the golden knobs is **-5.69 dB**, not ~0 dB: the ladder colours the two spectra differently (Res sweep: +1.07 dB at Res 0, -2.42 dB at Res 0.5, -5.69 dB at Res 0.75). Compensating with a +5.7 dB scale would overdrive Tri at low resonance and high drive. AL-PW now measures the ladder input for the scale bound and prints the voice ratio for the record; the `ALK_TRI_SCALE_V` comment says this. |
| S10 | **done.** `make clean; make all; make img`: builds (only pre-existing warnings), image written. Flash **691,100 / 753,664 B, headroom 62,564 B** (clean pre-change: 686,116 / 67,548 B; net **+4,984 B**, against the estimate's ~+5.2 KB). ITCM **13,984 B unchanged**; DTCM 4,396 B; FXBUF unchanged. `elf_p4_check P4BASE=baselines/s094p4`: **96/96 oscillator loops exact**, mixer **0 grew** (fresh baseline from the clean pre-change build). Alkaline loops 221 / 234 / 215 unchanged, new mode 4 **227**. `control_cost.py --elf`: 1,365 per block, 9 VDIV. `pwm_tables --check` OK; `special_tags` OK; `make_init_alk --check` OK; `make -C tools/dsp_test all`: 0 failures in every law, 47 break builds caught (incl. `break_pwm3`, `break_alk_pwmw`, `break_alk_tri`). Plots re-rendered with the implemented law; `plot_s094p4_pwm.py` titles now say S094 P1 (old) / S094 P4 (implemented), and the Rec high fractions print 0.498 / 0.377 / 0.252 / 0.127 / 0.010 at pos 0/32/64/96/127. Records in `tools/dsp_test/baselines/s094p4/`: `osc.txt`, `mixer.txt`, `spike.txt`, `link_budget.txt`, `alk_loops.txt`, `control_cost.txt`. |

---

## 13. Statements this makes out of date (for the user to log)

- **`S094_P0_PWM_IMPLEMENTATION.md`:** §1.1-1.2 (the width law, `p` 64 as
  the centre, "narrow high → square → narrow low") and P-D3.
- **`S094_P1_IMPLEMENTATION.md` / `S094_P1_VOICE_DSP_ADJUSTMENTS.md`:**
  - the PWM law;
  - `oscPwmRecip` / `oscPwmLevelQ14` / `oscPwmLift`;
  - PW1-PW4 as written there;
  - AL-PW at `pos` 64;
  - the Alkaline `pwm` "Saw/Sqr only".
- **`S094_P4_PWM_OSC_INVESTIGATION.md`:** §9's flash estimate (+1.6 KB →
  the S10 figure).
- **`INSTRUMENTS_DSP_REFERENCE.md`:**
  - the `pwm` type's `pos` meaning and limits;
  - the Alkaline wave lists (`wav` and `pwm` + Tri);
  - the Alkaline fused mode 4.
- **`CPU_USE_DSP_AUDIT.md`:** the Alkaline `pwm` warp loops (S4's counts).
- **`MEMORY.md`:** the Alkaline wave lists and `pwm` `pos` meaning, if
  recorded there.
- **`tools/plots/README.md`:** rows for `plot_s094p4_pwm.py` and the
  `pwm_dump` target.

---

## 14. Assessment of the implementation (2026-11-11)

**Verdict: implemented as scheduled, and every gate passes on an
independent rebuild. Ready for the device test (§11).**
- Two stale comments remain (§14.3, F1-F2).
- One sound result needs the user's ear: the Tri level (F3).
- The implementer also fixed a gap in the schedule (F4).

### 14.1 What was re-run (not taken from §12)

| Check | Result |
|---|---|
| `make clean && make all && make img` | builds; only the pre-existing warnings. Flash **691,100 B** (headroom 62,564 B); ITCM 13,984 B; DTCM 4,396 B; FXBUF unchanged |
| Host `make -C tools/dsp_test all` | exit 0; every law 0 failures, including PW1-PW10, AL-TY, AL-PW, AL-XT, AL-PWW, CB19; **47 break builds caught**, including `break_pwm3`, `break_alk_pwmw`, `break_alk_tri`. The six FAIL lines are `voice_control_break`'s, as expected |
| `pwm_tables --check`, `special_tags`, `make_init_alk --check`, `crumpbit_tables` | OK |
| `elf_p4_check P4BASE=baselines/s094p4` | **96/96 oscillator loops exact**; mixer 59 hot loops equal, **0 grew** |
| Alkaline loops (`elf_looppath.py`) | `…Osc` 221, `…Osc2` 234, `…OscPwm` 215 (unchanged); **`…OscPwmWt` 227**, 3 VDIV (gate ≤ 234) |
| `control_cost.py --elf` | 1,365 per block (42.7 per sample), 9 VDIV; `pwm` Tri voice **269.7** against the 276.9 ceiling |
| `crumpbit_cpu_check` (cap 181), `crumpbit_elf_check` | worst 180.8 (`grn`); every loop at its limit |
| The plots (`pwm_dump` + `plot_s094p4_pwm.py`, re-run) | the shapes figure shows the law. `pos` 0 is the unmodified wave in every row, and each narrows steadily to 127 with no fold. Rec high fraction 0.498 / 0.377 / 0.252 / 0.127 / 0.010 at `pos` 0/32/64/96/127. The narrowest is 4 points of 1,024, measured as 5 samples of 512 including its band-limited edges |

**The flash baseline is sound.** §12's "pre-change" build restored the
P4-touched files from `HEAD`, which is commit `50c7436d`:
- it holds S094 P1 and P2;
- it does not hold P3 or the CrumpBit change (S094 P2 §23);
- neither of those touches a P4 file.

So **+4,984 B is P4 alone**, against the ≈ +5.2 KB estimate. Since the P3
image (687,028 B), the tree is +4,072 B: P4's +4,984 B, less about 0.9 KB
from the CrumpBit one-pole removal.

### 14.2 Code reviewed against the schedule

| Area | Finding |
|---|---|
| `gen_pwm_tables.py`, `oscPwmTables.h/.c` | as §2: eleven `pos` tables, checks T1-T9; reference values confirmed (§12); header comment as scheduled |
| `oscPwmWarp.h` | `osc_pwmWarp()` moved verbatim; only its two inline comments changed (`64/p` → `2^31 / W`), as scheduled |
| `Oscillator.c` `osc_pwmPlan()` | as §3.2(e) line for line; the 15 loops untouched (dump 96/96) |
| `DiodeLadder.c/.h` | mode 4 and `DiodeLadder_renderB2OscPwmWt()` as §6; the four existing wrappers pass zeros; the `pwD` range comment corrected (4..512) |
| `AlkalineVoice.h/.c` | `ALK_WAVE_TRI` / `ALK_WAVE_COUNT`, `alk_renderWav()` (Tri), `alk_renderPwm()` (Tri mode 4, Saw/Sqr on the new tables), `Alkaline_setOscType()` (one list size), comments: as §7 |
| `MenuText.h`, `menu.c`, `InstrumentManager.c` | as §8 |
| Tests | PW9 pins the `pos`-0 identity; PW10 checks each width against the table and pins both ends independently (512 by PW9, 4 explicitly), so a mirrored or folded law fails. AL-XT is bit-exact; AL-PWW is −70.6 / −70.7 / −76.8 dB against the unfused reference, and `pos` 0 against `wav` Tri is −69.0 dB |
| Makefile / `osc_host.h` / `control_cost.py` | as §4.1, §4.3, §9.3, §9.4. The warp header rule is a prerequisite of `oscillator`, `alkaline` and `pwm_dump` (checked: `pwm_dump` rebuilds it when it is missing) |
| Scope | every changed file is on the schedule (CrumpBit's are S094 P2 §23, separate) |

### 14.3 Findings

| # | Kind | Where | Finding | Action |
|---|---|---|---|---|
| **F1** | stale comment | `AlkalineVoice.c:36-40` (`ALK_PWM_SQR_SCALE_V`) | "1.40 makes **pos 64** under pwm about as loud as Sqr under wav" and "(its sum at **pos 64** is the octave-up saw …)". Under the S094 P4 law that width is **`pos` 0** (AL-PW now measures `pos` 0, +1.14 dB) | replace "pos 64" with "pos 0 (S094 P4; pos 64 under the S094 P1 law)" in both lines |
| **F2** | stale comment | `Oscillator.c:3804` (`osc_pwmReadD()`) | "**p = 64** gives a band-limited square of the Rec orientation" | replace with "pos 0 (d = 512) gives a band-limited square of the Rec orientation" |
| **F3** | listening decision | `ALK_TRI_SCALE_V` (×1.0) | The scale matches Tri to Saw at the **ladder input** (+0.13 dB), as the Sqr scale was matched (×1.40, at the input). At the **voice output** Tri is +1.1 dB at Res 0, −2.4 dB at Res 0.5 and **−5.7 dB at Res 0.75** (the golden knobs). The saw excites the resonance far more than the triangle's weak upper harmonics do. The implementer moved §10's check to the input and documented why: compensating at the output would overdrive Tri at low resonance and high drive. That is consistent with the Sqr precedent and is accepted here | by ear on the device: if Tri reads too quiet at high Res, raise the factor (×1.4 ≈ +3 dB splits the difference). One constant |
| **F4** | schedule gap, fixed | `menu.c:68` | §8.2's assert uses `ALK_WAVE_COUNT`, but `menu.c` did not include `AlkalineVoice.h`; the schedule did not say to add it (the mid-implementation build failure noted in S094 P2 §23). The implementer added `#include "AlkalineVoice.h"` with a comment | none (correct) |
| F5 | as decided, for listening | Rec at `pos` 127 | 0.39 %: RMS about −24 dB against the square; band-limited at every pitch (a quiet blip at high notes) | listen; the floor is one generator constant (`D_REC_STEP`) |
| F6 | as decided, for listening | Sin at `pos` 127 | 6.25 %: the squeezed half-cycle is 8× the fundamental, with no octave rows | listen at high notes for aliasing; the floor is `SIN_W_END` |

**F1 and F2 are not applied here** (this pass only assesses). They are
comment-only and change no code or test.

### 14.4 Device test (unchanged from §11)

- a width sweep `pos` 0 → 127 on each wave, on a drum instrument and on
  the Alkaline (no fold; the narrowest at 127);
- the Alkaline `wav` Tri and `pwm` Tri, listening at Res 0, 0.5 and 0.75
  for F3;
- an LFO on the width;
- the worst Scene with an Alkaline on `pwm` Tri, with the probe, against
  the Basic share (19,620);
- Kits saved with `typ pwm` before S094 P4 now play a different width (no
  migration, standing rule).

### 14.5 Still to log (user; §13)

§13's list stands. In addition:
- `tools/plots/README.md` needs rows for `plot_s094p4_pwm.py` and the
  `pwm_dump` target;
- `S094_P4_PWM_OSC_INVESTIGATION.md` §2's plots are now the S094 P4 render,
  not S094 P1's. The S094 P1 figures quoted in its §2 text (the mirror)
  describe the old law.
