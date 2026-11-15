# S096 P2 — Instrument fixes (2026-11-15)

Six items from playing the S096 P1 and P2 images. **All six are done** in
the code.
- **F1** (the Delay's wow popping) was first measured and held for a CPU
  decision (§1.5). The user then chose to fix the extra heads and the
  reverse head in both the Delay and the Echo (§1.6). The device has since
  measured it (§1.7).
- **F5** (light popping on a Colloid PWM sweep) came from a later run
  (§5).
- **ITCM +136 B** for the Colloid's gliding loops was approved by the user
  (2026-11-15, §5.4).
- **F6** (the compressor's sidechain duck popping) followed: its attack now
  rounds off with `ctm` (§6).

| # | Item | State |
|---|---|---|
| F1 | Delay: quiet popping behind the delay output as wow goes up | Done: the Delay's D/3, 2D/3 and reverse heads and the Echo's D/3, 2D/3 heads read linearly; caps raised (§1.6). Measured on the device: worst 15,647 of 16,634 (§1.7) |
| F2 | Colloid: the filter envelope can't sweep the whole range at full depth | Done |
| F3 | Colloid: no `UBP` or `off` filter type | Done |
| F4 | Tetrakt: the spatializer's ranges should go higher | Done (feedback, detune, width; time unchanged) |
| F5 | Colloid: light popping on the tails of PWM sweeps (Scene 1, slot 5) | Done: Saw/Rec back to 6 oscillators; the pulse's second read and the warp glide across each block (§5). ITCM +136 B approved |
| F6 | Bus compressor: the sidechain duck pops at longer `ctm` | Done: the duck's attack eases in over 0 … 10 ms as `ctm` goes from 5 to 45 (§6) |

---

## 1. F1 — the Delay's wow popping

### 1.1 Cause

The Delay's tape has three heads: D (the main delay), D/3 and 2D/3
(`hed`, seven sets A B C AB AC BC ABC; A = D/3, B = 2D/3, C = D). Only the
main head is interpolated. The D/3 and 2D/3 heads, and the two windows of
the reverse head, read the **nearest frame** (`SpaceEffect.c:454–461`,
`space_near()` at `:250` for the Echo's heads at `:540`).

A nearest-frame read is clean only while its position moves exactly one
frame per sample. Wow, flutter and drift change the delay continuously
(±0.5 % at full `wow`), so each head's read speed becomes 1 ± ε. Every 1/ε
samples the nearest frame repeats or skips one. Each slip is a step in the
output, heard as a faint tick, and more `wow` means more ticks per second.
The main head is linearly interpolated, so it moves smoothly.

Your card's Delay (`SD_CARD_TEKT/.hcprms1`, Scene 0 Effect) has:
- **`hed` = 4, "AC"**: the D/3 head plus D;
- **`wow` 127**;
- `tim` 90, `fbk` 14, `deg` 59, `dck` 54.

So the D/3 head's nearest-frame read was in the output.

### 1.2 Measurement (host, the real `SpaceEffect.c`)

Setup: a 440 Hz sine at 0.5 into the Delay, feedback 0, 4 s captured after
the glides have settled. The figure is the largest third difference of the
output. A smooth sine (or a smoothly wobbled one) gives ≈ 1.2 × 10⁻⁴. A
one-frame slip of this sine gives ≈ 0.06 … 0.09.

| `hed` | `rev` | `tim` | `wow` 0 | `wow` 127 |
|---|---|---|---|---|
| C (D only, the default) | off | 64 | 0.0021 | 0.0042 |
| ABC | off | 64 | 0.0039 | **0.070** |
| C | **on** | 64 | **0.064** | **0.074** |

- **D only:** wow doubles the small residue of the linear read, with no
  slips. That residue is the moving interpolation fraction, spread evenly
  over the block (no block-boundary step).
- **With A or B heads and wow:** slips about 17 times larger than the
  default's. That is the popping.
- **Reverse:** it slips even without wow, because its windows always move.
- **Long delays:** at delays longer than the tape, the tape slows
  (va < 1), so the extra heads' read positions move by fractions even
  without wow. The same ticks are expected there by the same mechanism
  (not measured).

### 1.3 The fix and its cost

The fix is to interpolate those heads like the main one: a second frame
load and an interpolation per head and channel. I implemented it for the
Delay's D/3 and 2D/3 heads, measured it, and **reverted it**:

| | Delay (`space_cpu.py`, per frame) | Cap |
|---|---|---|
| Now | 194.6 | 202.9 (8.3 to spare) |
| D/3 and 2D/3 interpolated | **224.7 (+30)** | 202.9 — **over by 21.8** |

A packed 16-bit version (L and R are one 32-bit tape word) was estimated
at about +20, because the second load and the interpolation are the cost,
not the float conversions. It's still over the cap. The Echo's heads would
cost the same against 6.2 to spare (193.0 / 199.2). Interpolating the
reverse head is a further similar cost.

### 1.4 Device data that bears on the cap

The cap is a host model: (16,634 − service) / 67.5 cycles per
instruction-frame. With your final settings, the §18 device run measured
the Effect section at **12,406 cycles per block on average over the last
100 windows (highest window 12,615)**, against the 16,634 share. That's ≈ 4,000 cycles below the share. The model predicts
≈ 16,070 at 194.6.

At the device's ≈ 2 cycles per instruction, +30 per frame is about
+1,900 cycles per block, so ≈ 14,500, which is inside the share.

### 1.5 The options (as put to the user)

| Option | Effect | Cost |
|---|---|---|
| **a. Interpolate D/3 and 2D/3 (Delay and Echo)** | the popping with A/B heads goes away | +30 per frame each type; the host caps rise to match (device ≈ 14,500 / 16,634, to confirm with a probe) |
| b. As a, plus the reverse head | also removes the reverse head's ticks | about +20 … 30 more per frame (estimate) |
| c. Leave it | heads A/B and reverse tick under wow and at long delays; head C alone stays clean | 0 |

The recommendation was a, with b as a separate step. **The user chose a
and b together** (2026-11-15).

### 1.6 The fix (done)

**`Core/DSP/Effects/Space/SpaceEffect.c`.**
- **REMOVE `space_near()`; ADD `space_lin1()` and `space_lin2()`**
  (`:248–289`, comment block What / Why / Cost):
  - `space_lin1()`: the Echo's mono int16 tape, `t[i] + f (t[i+1] − t[i])`;
  - `space_lin2()`: the Delay's stereo frames (L low, R high in one 32-bit
    word), the same per channel, with one load for frame i and one for
    i + 1.
  - `i + 1` never wraps: `space_idx()` returns 1 … n, and the guard frames
    n … n + 2 copy frames 0 … 2.
- **`space_delayRun()`:**
  - the reverse head's two windows read through `space_lin2()` (they were
    two nearest-frame loads);
  - the crossfade is unchanged, `yl = b + wl (a − b)`;
  - the D/3 and 2D/3 heads read through `space_lin2()`;
  - the comment block's head list says so.
- **`space_echoTapeRun()`:** the D/3 and 2D/3 heads read through
  `space_lin1()`. The Echo has no reverse head. Its comment says so.

**Sound.** The heads' timing, gains and crossfades are unchanged. Only the
frame slips are gone. The A/B heads and the reverse head now sound like the
main head: a smooth tape read under wow and at long delays.

**Measured after the fix** (§1.2's method, now law SP11):

| Case | Before | After |
|---|---|---|
| Delay heads ABC, `wow` 127 | 0.070 | **0.0048** |
| Delay reverse (heads C), `wow` 0 | 0.064 | **0.0040** |
| Delay reverse (heads C), `wow` 127 | 0.074 | **0.0041** |
| Echo heads ABC, `wow` 127 | 0.034 | **0.0053** |

Every case is now at the main head's own level (0.0042 at `wow` 127).

**CPU (`space_cpu.py`, instructions per frame).**

| Type | Before | After | Old cap | New cap |
|---|---|---|---|---|
| Delay | 194.6 | **251.6** (+57: the heads +30, the reverse windows +27 on the reverse path) | 202.9 | **252.0** |
| Echo | 193.0 | **210.0** (+17) | 199.2 | **210.5** |
| Reverb | 189.5 | 189.5 | 191.8 | 191.8 |

The feed caps stay at cap + 20: 272.0 and 230.5, against totals of 271.6
and 229.9.

Two cheaper forms were tried for the stereo read and measured:
- an integer difference before the conversion: 237, no better;
- a packed 16-bit form (SIMD): estimated about −2 per head, not worth the
  complexity.

**The caps** (`tools/dsp_test/space_cpu.py`): `DLY_CAP_P2` 252.0 and
`ECH_CAP_P2` 210.5 replace `CAP(15)` and `CAP(17)` for those two types. The
Why block gives the reason and the device evidence.
- **Device estimate:** the model's 67.5 cycles per frame-instruction is
  about 25 % above the device. Scaling the measured 12,406 cycles at 194.6
  to 251.6 gives about 16,040 cycles per block for the Delay's worst path,
  inside the 16,634 share by about 600, which is tight.
- **Your settings** (reverse off) take the main head's path, about
  +30 per frame, ≈ 14,300.
- **Echo:** +17 per frame is about +1,100 cycles per block.

**Tests.**
- **Law SP11** (`tools/dsp_test/modules/space.c`) runs the four cases above
  and requires each to stay below 0.012, between the clean level (≈ 0.005)
  and a slip (0.06 … 0.09).
- **Verified against the old code:** run on the S095 `SpaceEffect.c`, all
  four cases fail (0.034 … 0.074).
- **Two break builds** (`tools/dsp_test/Makefile`, `space` target):
  `space_break_lin2` (the stereo read without its fraction) and
  `space_break_lin1` (the mono read without it). Both fail SP11 as
  required.

### 1.7 Device result (`SD_CARD_DEL_COL`, 2026-11-15)

**The run.** The card's image is byte-identical to the F1 build. Two boots
(records 823,591 and 855,016) give 1,854 probe windows. The Scene is
Tetrakt, Drum, Alkaline, Alkaline, Colloid, HiHat, with the Delay. The
Delay's final rows are `tim` 89, `spr` 66, `fbk` 32, `xfb` 79, `ton` 66,
`deg` 15, `wow` 127, `hld` 0, `rev` 0, `hed` 4 (AC), `dck` 75. `rev` was
switched on at record 843,606 and again at 856,987.

| Delay | Effect section, cycles per block | Against the 16,634 share |
|---|---|---|
| Before F1 (the earlier boots on the same card, 3,257 windows) | 12,410 … 12,500 average | — |
| F1, `rev` off (1,832 windows) | **13,636** average, highest window 14,376 | 2,258 spare at the highest |
| F1, `rev` on (22 windows) | **15,464** average, highest window **15,647** | **987 spare** |

- **Against the estimates:** reverse off is +1,170 over the old Delay,
  against an estimated +1,900. Reverse on is 15,647 at the highest, against
  an estimated 16,040. So F1 fits on the device with margin.
- **Mixer overruns:** a block over 100 % happened in 11 of 1,854 windows,
  against 16 of 3,257 before F1, about the same rate. These come from this
  Scene's total load, not the Delay.

---

## 2. F2 — the Colloid's filter envelope depth (done)

**Cause.** `COL_FENV_ST` was 72 semitones (6 octaves) at full `fam`, while
the cutoff range is 115.7 semitones (20 Hz … 15,963 Hz, `COL_CUT_ST_MAX`).
From cutoff 0, a full positive envelope stopped near 1.3 kHz.

**Changes.**

`Core/DSP/Instruments/Colloid/ColloidVoice.c`:
- **The depth:** `COL_FENV_ST` is now `COL_CUT_ST_MAX`, so full `fam`
  sweeps the whole range from either end, with the comment updated. The
  target is still clamped to 0 … 115.7.
- **The async's comment** in `Colloid_calcVoiceAsync()` now names
  `COL_FENV_ST` instead of "72 semitones".
- **The right / second filter at zero offsets.** The larger range exposed a
  float rounding difference, so this filter no longer equalled the left
  bit for bit at zero offsets (law CO-RTE: 26 failures).
  - **Cause, part 1:** `(x + offset) − 63/127` does not always give back
    `x`. Instrumenting the law showed one ulp in `cutSt` (110.29583 against
    110.295822).
  - **Cause, part 2:** `fam × 127 + eno × 127 − 126` fused into FMAs that
    left a residue of about 4 × 10⁻⁶.
  - **Fix:** each right value is now written as `left + (offset − 63/127)`,
    and `famR` as fam's byte value + `(eno − 63/127) × 127`. The bracket is
    exactly 0 at the offset's zero, so the right value is then the left one
    bit for bit. The instruction count is the same (Colloid CPU below).

**Sound.** A saved sound with `fam` ≠ 63 now sweeps 1.61 times as far
(115.7 / 72). To keep an old sweep, set fam to 63 + (fam − 63) × 0.622.

**Tests.** `tools/dsp_test/modules/colloid.c`, CO-SVF, now also checks
that:
- a full `+fam` from cutoff 0 reaches 115.7 semitones;
- a full `−fam` from cutoff 1 reaches 0.

The CO-RTE break build is retargeted at the new line
(`tools/dsp_test/Makefile`, `break_col_rte`) and is caught.

---

## 3. F3 — the Colloid's `UBP` and `off` filter types (done)

**Changes.**

`Core/DSP/Instruments/Colloid/ColloidVoice.h`:
- ADD `COL_FT_UBP` 5 and `COL_FT_OFF` 6; `COL_FT_COUNT` 5 → 7.
- They are appended, so saved Colloid scenes keep their types.
  `COL_RTE_COUNT` follows: 6 → 8, so a mono route's second filter can also
  be UBP or off. A mono route with `off` second is a single filter.

`ColloidVoice.c`, `col_svfMix()`:
- **UBP** = (mH, mB, mL) (0, k, 0): the drum filter's unity-gain band-pass
  (`ResonantFilter.c` `FILTER_UNITY_BP`, 2R·y0 = k·v1), with peak gain 1
  at any resonance.
- **off** = (1, 0, 0): the input passes unchanged. The filter loop still
  runs, so the cost stays constant.

`Core/Menu/MenuText.h`: `colFiltTypeNames` and `colRouteNames` gain `UBP`
and `off`.
- The menu clamps the row to the name count.
- `menu.c`'s asserts tie both lists to the counts.

**Tests.**
- CO-SVF's random stress now draws from all seven types (`% COL_FT_COUNT`).
- New exact checks: `off` gives the input bit for bit; UBP's peak at its
  centre is 0.5 for a 0.5 input, within 1 %, at resonance 0.2 and 0.9.

---

## 4. F4 — the Tetrakt's spatializer ranges (done)

**Time is not changed.** The line is one FxBuffer voice unit
(2,208 samples, about 50 ms).

| Constant (`TetraktVoice.c`) | Was | Now | Effect at the row's top |
|---|---|---|---|
| `TKT_FB_MAX` (`sfb`) | 0.9 | **0.97** | longer repeats. The loop gain stays below 1 (with the loop's one-pole), so it stays bounded, and the line write still saturates |
| `TKT_SDT_SPAN` (`sdt`) | 0.15 | **0.5** | the right tap up to 50 % earlier than the left (was 15 %) |
| `TKT_BUS_AP` (W2, `wid`) | +0.4 | **−0.6** | the allpass's 90° point moves from 16 kHz to 3.4 kHz (see below) |

**W2's coefficient.** At +0.4 the allpass shifted the phase by only 3.5° at
1 kHz, so the width barely reached the audible band.

| Coefficient | 90° point | At 200 Hz | At 1 kHz | At 5 kHz |
|---|---|---|---|---|
| +0.4 (old) | 16.4 kHz | −0.7° | −3.5° | −18° |
| **−0.6 (new)** | **3.4 kHz** | −6.5° | −32° | −112° |
| −0.8 (not chosen) | 1.55 kHz | −15° | −65° | −147° |

I stopped at −0.6 because beyond it the allpassed right side starts to
cancel in a mono sum above a few kHz: at −0.8, a mono sum would lose about
11 dB at 5 kHz. The width curve, `wid` × (1 − env)², is unchanged, so the
transient stays centred.

**Also possible, not done:**
- The width curve could be (1 − env) instead of its square, so the width
  arrives earlier in the decay.
- The space level `slv` and the per-layer sends are at unity at their tops.
  Raising them would mean gain above unity before the int16 pack.

**Cost.** None: these are block-rate constants. The Tetrakt CPU figure is
unchanged at 382.7.

**Tests.** TK-S1 checks the right tap at `tapL × (1 − TKT_SDT_SPAN)`
instead of a literal 0.85. TK-W2 and TK-RING are unaffected: W2's mix law
and the ring are unchanged.

---

## 5. F5 — the Colloid's PWM sweep popping (done)

### 5.1 Cause

Slot 5 of Scene 1 (`SD_CARD_DEL_COL/.hcprms1`) is a Colloid on typ `pwm`,
form Saw:
- **Oscillators:** 8 then, `psp` 97, `dtn` 58, `coa` 31 (a low note).
- **Modulation:** LFO 1 at amount 127, rate 13, on `osc1_position`, which
  under pwm is the **pulse width**. LFO 2 is on `dtn`.
- **Filter:** the new UBP, `frq` 31, `res` 53, a long envelope sweep
  (`fat` 91, `fde` 114, `fsl` 0, `fre` 103, `fam` 99).

**The pulse forms (Saw, Rec).** Each oscillator's pulse is the saw row read
at its phase plus or minus a second read `D` table points earlier
(`col_pulse()`).
- **How `D` changes:** it was a whole number of points, held for the whole
  block. The second read shared the first's fraction. The width index slews
  by up to 2 a block, and each index is 3–4 points (`oscPwmDSaw/Rec`), so
  a moving width changed `D` by up to 8 points in one jump at each block
  boundary.
- **Why it clicks:** the pulse's second edge jumped with it, and whenever
  that edge passed the sample being read the output jumped by up to the
  edge's height.

**The warp forms (Sin, Tri).** Their width moves the warp's half-cycle
boundary W (`osc_pwmWarp()`, with `ra`, `rb`), and that also changed in one
jump at each block start.

**Why you heard it on the tails.** With the filter open the clicks are
masked. On the tails the envelope closes the UBP to about 100 Hz and the
level drops, so they show.

**Not the cause** (checked):
- the filter envelope: it never jumps. With the sustain switch off it runs
  decay → release, and the hand-off at 0.01 changes only the rate; its end
  at 1e-4 is 0.007 semitones on this sweep;
- the new UBP mix;
- P2's full-range envelope;
- the stack's level compensation: it steps with the width too, but by at
  most 0.07 dB per index, and holding it constant did not remove the clicks
  (table below).

### 5.2 Measurement (host, the real voice)

Setup: slot 5's oscillator settings, the filter `off`, note 31. The width
is still (pos 100), swept by a slow sine (0.4 Hz, the whole range), or held
at the full slew. The figure is the largest third difference of the output.

| Form | Width still | Swept, before | Swept, after | Full slew, after |
|---|---|---|---|---|
| Sin | 0.0001 | 0.033 | **0.0006** | 0.0006 |
| Tri | 0.0005 | 0.063 | **0.0023** | 0.0014 |
| Saw | 0.033 | **0.507** | **0.039** | 0.059 |
| Rec | 0.028 | **0.492** | **0.037** | 0.065 |

- **Before:** with the width swept, 21 of 23 of the large values fell on a
  block's first samples.
- **Ruling out the alternatives** (Saw, before the fix): holding `D`
  constant while the width moved brought it to 0.035; holding the gain
  constant instead left 0.547. With the UBP filter as in slot 5, the
  figures were 0.022 still and 0.204 swept.
- **After:** Saw/Rec under a sweep are at the still level. The extra at the
  full slew is the edge genuinely moving fast, spread across the block.
  What remains for Sin/Tri at block starts is a change of slope (the glide
  is linear per block), not a jump.

### 5.3 The fix (user decision, 2026-11-15: Saw/Rec back to 6, both glides)

**`Core/DSP/Instruments/Colloid/ColloidVoice.h`.**
- `COL_PWM_COUNT` 8 → **6**. The count note "S096 P2 F5" gives the reason
  and the cost.
- ADD `pwD0[COL_OSC_MAX]` and `pwPos0[COL_OSC_MAX]`: the pulse width and
  the width index at the block's start. That's 36 B; `sizeof(ColloidVoice)`
  is 1,148 B (measured), inside the 1,264 B slot reserve.
- The `COL_PWM_FORM_COUNT()` comment is updated.

**`Core/DSP/Instruments/Colloid/ColloidVoice.c`.**
- `COL_NORM_PWM`: 1/√8 → **1/√6**. The assert is `COL_PWM_COUNT == 6`.
  The headroom note now gives √6 × 0.5 = 1.22.
- **`col_oscControl()`:** before each oscillator's width moves, its old
  index and `D` become the block's start (`pwPos0`, `pwD0`). On a first
  note (`jump`) or a rejoin (`i ≥ live`) the start equals the new value, so
  those still land at once. The comment block says so.
- **`Colloid_triggerVoice()`:** a new note sets `pwD` and both starts from
  the new widths, so its first block doesn't glide from the last note's
  widths (the S095 rule "no slew across a new note").
- **`Colloid_calcVoiceSyncBlock()`:**
  - **warp:** each oscillator gets the start index's W, `ra`, `rb` and
    steps `(end − start) / 32`;
  - **pulse:** the pair loops get `pwD0` as well as `pwD`.
  - A static assert ties `busS` to `busM + 32`, because the warp loops now
    take one bus.

**`Core/DSP/Instruments/Colloid/ColloidOsc.c` / `ColloidOsc.h`.**
- **`col_pwmPairStore/Add`:** the second read B has its own phase `q`. At
  the block's start q = p − (width0 << 22). Per sample it adds
  `inc − (width − width0) × 2^17`, so after 32 samples it is exactly
  p − (width << 22), where the next block starts it.
  - B is interpolated with its own fraction. `col_pulse()` (the shared
    fraction) is removed, and its comment block now describes the glide.
  - With a still width the output equals the S095 pulse to within 1 per
    oscillator (the two reads round separately).
  - 66 instructions per pair per sample (was 50/51).
- **`col_warpStore/Add`:** W, `ra` and `rb` add `dW`, `dRa`, `dRb` per
  sample.
  - **Why linear is enough:** the linear glide keeps W·`ra` = 2^57 only at
    the block's ends. Between them the half-cycle boundary is off by at most
    ¼ (32 dW / W)²: 0.3 % of a half-cycle for Sin and 2.2 % for Tri, at the
    narrowest widths at the full slew. That is far below the jump it
    replaces.
  - **The bus:** the loops take one 64-word bus (mid then side), so the
    glide's steps stay in registers. Separate pointers cost 7 more per
    sample in spills.
  - The comment blocks say all of this.

**Not changed:**
- the Tri form's row lift (`oscPwmTriLift[width]`), which switches rows at
  a few width thresholds;
- the per-block level compensation (≤ 0.14 dB a block).

Neither showed in the measurement.

### 5.4 Cost

`colloid_cpu.py`, instructions per sample:

| Mode | Before (8 / 6) | Now | Device estimate |
|---|---|---|---|
| pwm Saw/Rec (`pwm8-par` → `pwm6-par`) | 387.6 (18,479 measured in slot 5) | **376.5** | ≈ 17,950 (1.49 cycles per instruction, slot 5's) |
| pwm Sin/Tri (`pwmw6-par`) | 326.6 | **379.6** | ≈ 17,920 (1.475, S095's pwm Sin 6) |
| plain 12 (`plain12-par`) | 383.5 | 390.6 (see below) | unchanged |

- **Fit:** both pwm modes are inside the Basic 19,620 with about 1,700 to
  spare, and the Saw/Rec voice is now cheaper than the clicking 8 was.
- **Why `plain12` rises:** the tool counts the control loop at its longest
  path, which includes the PWM branch, and its documented upper bound
  charges that branch to every mode. The new start-of-block bookkeeping
  lands there. The wav/wtb modes never run that branch.
- **ITCM: 15,232 → 15,368 B (+136 B)** for the four gliding loops
  (`col_pwmPairStore/Add` 0x14a / 0x140 B, `col_warpStore/Add` 0xa4 B
  each).
  - **Approved** (user, 2026-11-15). ITCM counts as RAM in this project,
    so it was held open until then. 1,016 B of ITCM remain.

### 5.5 Tests

`tools/dsp_test/modules/colloid.c`:
- **CO-OSC (pulse):** checked against the restated gliding law
  (`ref_pulseG()`: A at p, B at its own gliding q), with a still width and
  with a glide. With a still width the result must equal the S095
  shared-fraction pulse to within 1 per oscillator.
- **CO-PWM (warp):** checked against the restated law with W, `ra`, `rb`
  stepping as the loop does, still and gliding. The test uses one 64-word
  bus, the new contract.
- **CO-PWM's voice-level form check** now uses the voice's own `pwD0` /
  `pwD` with the gliding law.
- **CO-JOIN:** with 6 for both forms, Sin → Saw has no rejoining
  oscillator. pwm → wav (6…11) is measured against oscillator 0's settled
  pitch.
- **New law CO-PWG:** for every form, a slow sweep and the full slew must
  each stay within 3 × the still width's largest third difference + 0.002.
  The S095 code fails it by 6–15 times (§5.2).
- **Two break builds** (`tools/dsp_test/Makefile`):
  - `colloid_break_pwg`: the second read without its glide;
  - `colloid_break_wpg`: the warp's W without its glide.

  Both fail CO-PWG as required.

## 6. F6 — the compressor's sidechain duck (done)

### 6.1 Cause

A `csc` trigger ducks the compressor's fast cell by (2 + 19 `cam`) ×
(v/127)³ dB. `busComp_processBlock()` set the cell straight to that depth
on the trigger's next block (`gr_fast_db = duck_db`). The only smoothing
left was the one-block gain ramp, so the whole duck happened in 32 samples
(0.73 ms) at every `ctm`. `ctm` shapes only the program-driven attack
(5 … 15 ms) and the release.

Your Scene 0 (`SD_CARD_DEL_COL/.hcprms1`, Scene parameters 41 … 44) has:
- `cmp` 1 (St1), `cam` 48, **`ctm` 41**, `csc` 2 (the Drum in slot 2);
- so a full-velocity trigger ducked 9.2 dB in 0.73 ms: the pop.

### 6.2 The fix

**`Core/DSPAudio/BusCompressor.c`.**
- **The constants** (comment block What / Why / State / Cost):
  `BUS_COMP_SC_ROUND_FROM` 5, `BUS_COMP_SC_ROUND_TO` 45 (the range is the
  user's, 2026-11-15), `BUS_COMP_SC_ATTACK_MAX_MS` 10,
  `BUS_COMP_SC_EASE_FLOOR` 0.25, `BUS_COMP_SC_EASE_NORM` 1.5207.
- **ADD `busComp_scAttackBlocks()`:** the attack in blocks for a `ctm`
  byte, 10 ms × smoothstep((`ctm` − 5) / 40). It is 0 up to `ctm` 5 and the
  maximum from 45 on. The smoothstep starts slowly.
- **The sidechain branch:**
  - with n ≤ 1 block (`ctm` up to about 12) the duck lands at once, exactly
    as before;
  - otherwise the cell moves toward the duck each block by
    depth × shape(x) × 1.5207 / n, where x is the duck's progress (0 … 1)
    and shape(x) = 0.25 + 0.75 × 4x(1 − x);
  - this is an eased S-curve in dB: a quarter of full speed at the start
    and the end, full speed half way. 1.5207 is the integral of 1 / shape,
    so the duck arrives in about n blocks;
  - `sc_pending` stays set until the duck reaches its depth, and a new
    trigger only raises it.
- **State:** none added (the state is approved for 32 B). The
  compressor's per-sample loop is unchanged (P4 gate: exact).

| `ctm` | Attack |
|---|---|
| 0 … 12 | one block (0.73 ms), as before |
| 15 | 1.6 ms |
| 20 | 3.2 ms |
| 25 | 5.0 ms |
| 30 | 6.8 ms |
| 35 | 8.4 ms |
| 41 (your Scene) | 9.7 ms |
| 45 … 127 | 10 ms |

**Measured** (host, the real `BusCompressor.c`, `cam` 48, velocity 127,
the fast cell's dB per 0.73 ms block after a trigger):

| `ctm` | Blocks 1 … 8 after the trigger | Arrives |
|---|---|---|
| 10 | −9.18 at once | block 1 |
| 20 | −0.80, −2.35, −4.94, −8.09, −9.18 | block 5 |
| 41 | −0.26, −0.61, −1.05, −1.62, −2.33, −3.16, −4.11, −5.11 … | −9.18 at block 14 |
| 64 | −0.25, −0.59, −1.02, −1.57, −2.24, −3.05, −3.96, −4.95 … | −9.16 at block 14 |

**Tunable.** The 10 ms maximum and the 0.25 starting speed are the
`BUS_COMP_SC_*` constants. Longer would soften the pump's punch.

### 6.3 Tests

**New host module `tools/dsp_test/modules/buscomp.c`, law BC-SC** (the
compressor had no host law before). It builds the real `BusCompressor.c`
over host Scene stand-ins. After the detector settles at the floor, a
velocity-127 trigger on voice 2 must:
- at `ctm` 0 and 10: duck by the full 2 + 19 `cam` dB in one block;
- at `ctm` 25, 41, 45 and 127:
  - fall monotonically;
  - take a first step of at most 0.35 of its largest;
  - take no step above 1.05 × depth × 1.5207 / n;
  - reach 99 % of the depth between 0.7 n and 1.4 n blocks.

**Makefile.** The target `buscomp` is in `all`. Its break build
(`buscomp_break_sc`, the attack forced to 0, so the duck always snaps)
fails BC-SC as required.

## 7. Gates

The tree is S096 P1 §19 plus this step's changes, F1, F5 and F6 included.
Earlier runs are recorded in `tools/dsp_test/baselines/s096p2/` (F2–F4),
`…/s096p2/f1/` (with F1) and `…/s096p2/f5/` (with F5). The final run, below,
is in `…/s096p2/f6/`.

| Gate | Result |
|---|---|
| `make all && make img` | exit 0; `build/LXRV2_lxr02.img` written |
| Flash | 729,868 B used, headroom 23,796 B (+1,432 vs §19; F1 +296: `space_delayRun` 0x570 B, `space_echoTapeRun` 0x268 B; F6 +208; F5 the rest) |
| ITCM | **15,368 B (+136, F5; approved by the user 2026-11-15, §5.4)** |
| DTCM / FXBUF | 4,396 / 126,656 B, unchanged |
| `sizeof(ColloidVoice)` | 1,148 B (+36, F5; ≤ 1,264) |
| Host `make -C tools/dsp_test all` | exit 0. Every law 0 failures (Colloid 18 groups, with CO-SVF's new checks and the new **CO-PWG**; Tetrakt 11 groups; Space SP1 … **SP11**; the new **BC-SC**). Every break build caught (74 lines), including the retargeted `colloid_break_rte` and the new `space_break_lin2` / `space_break_lin1`, `colloid_break_pwg` / `colloid_break_wpg` and `buscomp_break_sc`. The `voice_control failures: 6` line is the deliberate break build |
| Colloid CPU | pwm6-par **376.5**, pwmw6-par **379.6** (F5, §5.4); plain12-par 390.6 (the tool's PWM-branch upper bound; the wav/wtb path is unchanged). F2/F3 added at most 1 per block |
| Tetrakt CPU (worst block) | 382.7 (unchanged; gate 390 passed) |
| Space CPU | dly **251.6** (cap 252.0), ech **210.0** (cap 210.5), rvb 189.5 (cap 191.8): §1.6 |
| CrumpBit CPU | 182.7 ≤ 184 (feed 202.7 ≤ 204), unchanged |
| `elf_p4_check P4BASE=baselines/s096p1/before` | oscillators 94 / 94 exact (`busComp_processBlock`'s per-sample loop among them, unchanged by F6); mixer 52 equal, 6 grew (the §19 result, unchanged) |

Records: `tools/dsp_test/baselines/s096p2/f6/` (`host_all.txt`,
`link_budget.txt`), `…/s096p2/f5/` (`colloid_cpu.txt`; unchanged by F6),
`…/s096p2/f1/` (`space_cpu.txt`, `crumpbit_cpu.txt`; unchanged by F5 and
F6).

**Note on the target.** The Colloid's exact-at-zero form relies on the
bracket being evaluated as written. The host test is built at `-O2`. The
firmware's DSP objects use `-Ofast`, which allows reassociation, so on the
device the right filter may differ from the left by an ulp at zero
offsets. That is inaudible and harmless; it only matters to the host law.

---

## 8. Statements this makes out of date (for you to update)

- **S095 Colloid documents and the Colloid section of
  `knowledge_files/`:**
  - the filter envelope's depth (72 semitones, 6 octaves) → the whole range
    (115.7 semitones);
  - the filter types (LP BP HP Nch Pek) → plus UBP and off;
  - the route list → `str` plus seven types.
- **`S096_P1_TETRAKT_SAMPLE_PLAYER.md` §6.1 / §6.2:**
  - "sdt × 0.15" → × 0.5;
  - the feedback's 0.9 → 0.97;
  - W2's allpass coefficient → −0.6, with its 90° point at 3.4 kHz.
- **`S096_P1_IMPLEMENTATION.md` §19:** the Tetrakt constants above.
- **The Space documents (S095 P1 §S18, O4 / O6) and the Space section of
  `knowledge_files/`:**
  - "D/3 and 2D/3 as nearest-frame heads" and "two nearest-frame
    [reverse] heads" → linear reads;
  - the Delay cap 202.9 → 252.0, the Echo cap 199.2 → 210.5 (feeds + 20);
  - the Delay's 194.6 → 251.6, the Echo's 193.0 → 210.0 instructions per
    frame.
- **S095 P5 (`S095_P4_IMPLEMENTATION.md` §17/§18, `ColloidVoice.h`'s count
  note) and the Colloid section of `knowledge_files/`:**
  - "12 plain / 8 PWM Saw/Rec" → 12 / 6 / 6;
  - `COL_NORM_PWM` 1/√8 → 1/√6;
  - the pulse read "shares one fraction, d whole points" → B has its own
    gliding phase (§5.3);
  - the warp's W/`ra`/`rb` "per block" → gliding.
- **The bus compressor documents (`S074_BUS_COMP.md`, S095 P4 C1) and the
  compressor section of `knowledge_files/`:** "a `csc` trigger lights the
  fast cell at once" → at once up to `ctm` 12, then an eased attack growing
  to 10 ms at `ctm` 45 (§6).
- **The open question from §19** (re-snapshot the P4 base from the reverted
  LTO layout) is still open. Nothing in P2 changes it.
