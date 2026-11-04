# P091 P2 — CrumpBit Expansion: Expanded Plan

- **Date:** 2026-11-04. **Branch:** `dev-ph7-voice_expansion`, HEAD
  `5c9a92f9` (the S091 P1 reductions, implemented and assessed).
- **Status:** plan. No code is changed. Implementation schedule:
  `S091_P2_Implementation.md` (2026-11-04). **Both drives (`drv`, `tdr`)
  are deferred** to a later addition, after the user has heard the
  undriven Effect (user, 2026-11-04); §0 lists what that changes.
- **Inputs:**
  - `S091_P1_CrumpBit_Expansion.md`: the original plan; its §0A decisions
    and the §10 hardware result.
  - `S091_P1_Reductions_Implementation.md` §9: the reduction assessment.
  - User comments of 2026-11-04 (§1).
- **This plan replaces** §3, §5 and §6 of `S091_P1_CrumpBit_Expansion.md`
  (§11 lists what changed). §0A of that plan still applies wherever this
  plan does not override it.
- **Binding constraint:** `S089_P2_WAVEFOLDER_CHAIN.md` §8. No per-sample
  DSP cost may rise unless an equal or larger *measured* saving pays for it.
  The measured saving is the S091 P1 result: the `cpu` widget reads 3–4 %
  lower with `cbt`, about 150–200 cycles per frame (§7).
- **RAM rule:** the target is 0 B growth, keeping the Effect union at 76 B
  (§8). The earlier +8 B approval stays in reserve.

---

## 0. Update 2026-11-04: drive deferred (user)

"Let's not implement the drive yet, let me see how it sounds first, leave it
in the plan as a potential later addition." The design in §5.3–§5.5 stays as
the plan for that addition. The first implementation
(`S091_P2_Implementation.md`) differs as follows; this section wins over
§1–§11 where they disagree.

| Topic | First implementation | Later addition (unchanged plan) |
|---|---|---|
| Rows | 11 `ton`, 12 `typ`, 13 `frq`, 14 `res` (`CRUMPBIT_PARAM_COUNT` 15) | 15 `drv`, 16 `tdr`, appended |
| Lanes | 4 of the 6 free lanes; 2 stay free | the drive rows take the last 2 |
| Page s3 | `frq res – –` | `frq res drv tdr` |
| Soft knee (§0A D7) | fixed, division-free cubic on the feedback return: `y = v − (4/27)v³` on `v` clamped to ±1.5 (slope 1 at 0, ±1 with zero slope at ±1.5) | the `tdr` knee of §5.3 replaces it |
| Output filter input | undriven: `off` is `cx = 1` on the input, so `c_dry` and `c_x` are one coefficient (3-coefficient mix) | §5.4 drive and the 4-coefficient mix |
| Shared division (§5.5) | none: no Padé saturator, no per-sample VDIV | as planned |
| Decisions | Q1 trims (76 B), Q2 reversed Rate, Q3 unity at 100, Q4 Q ≤ 8, Q7 sub-type byte waits, Q9 LP2 = LP: applied as recommended | Q5 (`tdr` token), Q6 (drive cost), Q8 (drive level), Q10 (`off` bypasses drive) wait for the drive |
| RAM | `CrumpBitRuntime` 76 B (73 B used + alignment), union unchanged | +2 B for the drive rows fits the 3 B of alignment |
| CPU (linked, measured in the scratch build) | stage-2 loop 126, output-filter loop 31, per block ≈ 15–19: **≈ 172–176 per frame**, against ≈ 191 before S091 | §7 estimate ≈ +46 |

---

## 1. Decisions taken (user, 2026-11-04)

| # | Comment | Applied as |
|---|---|---|
| U1 | Keep parameter names aligned with the voices: cutoff `frq`, filter type `typ` | The output-filter rows are `typ`, `frq`, `res` and `drv`, with the voice / StereoFilter keys and labels (`filter_type` "Type", `filter_freq` "Frequncy", `filter_reso` "Resnance", `filter_drive` "Overdriv"), under CrumpBit file keys (§2) |
| U2 | Make `typ` morphable | `typ` is Morph- and Pattern-capable, exactly like StereoFilter's `typ` (`INSTRUMENT_PARAM_FLAG_MORPHABLE \| AUTOMATABLE`, `EFFECT_MOD_NONE`). Morph steps through the type list, and the output-mix coefficients glide over one block, so a step does not click (§5.4) |
| U3 | `ton` belongs to the `sub` sub-type (`CrumpBitSubType`); delay is the only sub-type today, but more may follow | `ton` and its drive `tdr` are delay sub-type rows, like `mix`/`fbk`/`rte`/`snc`/`dpn`. The code and data split keeps the sub-type stage separate from the shared stages (§4) |
| U4 | Menu: `ton` replaces `mix` on the bit page and on the standard page; then `mix` after `dpn`, then type, cutoff, resonance | §3 |
| U5 | Is there room for the voice-filter drive on the output filter, and on `ton`? | **Yes, both**, as one shared stage design (§5.3, §5.4). The cost is real: drive is the largest new item, and the stage that can be dropped if the board disagrees (§7, §9 Q6) |

Earlier answers (§0A of the original plan) still apply:
- the tempo-synced divisions are kept, and the free Rate curve is
  re-scaled;
- one-pole tone on the feedback return;
- stereo TPT SVF as the last element;
- feedback maximum 1.2;
- soft-knee limiter;
- native sample rate.

---

## 2. Parameters

Rows are appended after the existing rows (indices are permanent), so
`CRUMPBIT_PARAM_COUNT` goes from 11 to 17. The Effect limit is 64 rows
(`EFFECT_PARAM_COUNT`).

| Index | Token | Key | Long label | Group | Range / display | Default | Morph | LFO | Pattern |
|---|---|---|---|---|---|---|---|---|---|
| 11 | `ton` | `crump_tone` | Tone | Delay sub-type | `DTYPE_PM63`: −63..+64, stored 63 = flat | 63 (flat) | yes | yes | yes |
| 12 | `typ` | `crump_filter_type` | Type | Output filter | `MENU_FILTER`: LP HP BP UBP Nch Pek LP2 off (0..7) | 7 (`off`) | yes (U2) | no | yes |
| 13 | `frq` | `crump_filter_freq` | Frequncy | Output filter | 0..127, voice curve | 64 | yes | yes | yes |
| 14 | `res` | `crump_filter_reso` | Resnance | Output filter | 0..127, voice law, Q capped (§9 Q4) | 0 | yes | yes | yes |
| 15 | `drv` | `crump_filter_drive` | Overdriv | Output filter | 0..127, voice drive law | 0 | yes | yes | yes |
| 16 | `tdr` | `crump_tone_drive` | ToneDrv | Delay sub-type | 0..127, voice drive law, level-matched (§5.3) | 0 | yes | yes | yes |

Rows 15 and 16 are **deferred** (§0): the first implementation adds rows
11–14 only.

- **Defaults:** frq 64, res 0 and drv 0 match StereoFilter. `typ` defaults
  to `off` so that a Scene's output is unfiltered until a type is chosen.
- **The token `tdr` is a proposal** (§9 Q5). The voices use `drv` for
  filter drive, and `drv` must stay with the filter (U1).
- **Lanes:** 6 FX lanes are free (the registry row has 16; 10 are in use).
  The six new rows take all of them, in page order: `ton`, `typ`, `frq`,
  `res`, `drv`, `tdr`. After this, CrumpBit has **no free lane** (see §4,
  point 6).
- **Rows that change behaviour** (no new row; saved Scenes change, and
  there is no migration):

  | Row | Change |
  |---|---|
  | `rte` | Reversed: a higher value means a longer delay. ≈ 20 ms … ≈ 2.87 s (full arena). Clamped by the live share (§5.1). |
  | `fbk` | 0..100 → 0..1.0, then 100..127 → 1.0..1.2 (today 127 = 0.99). |
  | `snc` | Unchanged control. It offers every division that fits the live share. |

---

## 3. Effect page (U4)

SELECT 2 grows from 3 to 4 screens. That is the layout maximum
(`cells[8][4][4]`), so all 16 cells are used.

| Screen | Cell 1 | Cell 2 | Cell 3 | Cell 4 |
|---|---|---|---|---|
| s0, home overlay (bit row on top) | **`ton`** | `fbk` | `rte` | `mrp` |
| s1, standard page | **`ton`** | `fbk` | `rte` | `sub` |
| s2 | `snc` | `dpn` | **`mix`** | **`typ`** |
| s3 (new) | **`frq`** | **`res`** | **`drv`** | **`tdr`** |

- `mix` leaves s0 and s1; it now sits after `dpn` (U4).
- **`drv` and `tdr` are placed last** because U4 gives no position for
  them. Moving them is a layout-table change only.
- **Code:** `crumpBit_layout` in `CrumpBitParameters.c` (`screen_count`
  `{0, 4, …}` and the cell table). `custom_row0` stays on s0.

---

## 4. The sub-type structure (U3)

Today `sub` (index 8, `crump_subtype`) has a maximum of 0 (`dly`), and
`write_param()` ignores it. The rows "behind" it (`mix`, `fbk`, `rte`,
`snc`, `dpn`) are simply the delay's rows. The plan keeps that model and
makes it explicit, so that a second sub-type does not break anything.

1. **Three stages, one owner each.**

   ```
   ADC/bit lines/DAC + AC  →  sub-type stage  →  output stage
   (shared: bof, biv)         (delay: rte fbk     (shared: typ frq res drv,
                               snc dpn mix ton     then level/pan/route)
                               tdr)
   ```

   - The **output filter is not part of the sub-type.** It is the last
     element of the Effect whatever the sub-type is.
   - **`ton` and `tdr` are part of the delay sub-type,** because they sit on
     the delay's feedback return.
2. **Row grouping is recorded in one table.** `CrumpBitParameters.c` gets a
   const per-sub-type row list (delay: 5, 6, 7, 9, 10, 11, 16). It is the
   single place that says which rows belong to which sub-type. The
   descriptor comment, the page format hook and a host law all read it.
   The category of `ton`/`tdr` is "Delay", like `mix`/`fbk`.
3. **DSP dispatch is per block, never per sample.** `crumpBit_process()`
   keeps one loop. The delay body is written as one clearly bounded block
   (`/* sub-type: delay */`) between the shared ADC part and the shared
   output part. A future sub-type gets its own loop or body, chosen once per
   block from the stored sub-type. That keeps the constant-CPU rule:
   everything that runs, runs every frame.
4. **The runtime is grouped by owner** (§8): shared fields, then delay
   fields, then output-filter fields. A future sub-type can overlay the
   delay group inside `CrumpBitRuntime` without moving the shared or output
   fields.
5. **`write_param()` stores the sub-type** in a byte (a 0 that is never
   read today), so a second sub-type needs no layout change.
   - **Note:** this costs 1 B, which §8 does not count. If RAM is tight it
     can wait until a second sub-type exists. §9 Q7.
6. **Lanes are full after this plan.** A future sub-type cannot get FX
   lanes for new rows unless it reuses the delay rows' lanes. That is
   possible when its controls map onto the same rows (`mix`, `fbk`, `rte`,
   `ton`, `tdr`). This plan records the limit; it decides nothing.
7. **Label hook:** `sub` keeps its `dly` label from `crumpBit_uiFormatValue3()`.
   Any new sub-type label goes there, not into a `DTYPE_MENU` table (all
   16 ids are used).

---

## 5. DSP design

### 5.1 Elastic delay and the Rate curve (0 per sample)

- **Loop length** = the live share minus the 2-sample guard. The registry
  `buffer_pref_bytes` becomes the whole arena (126,688 B). `buffer_min_bytes`
  stays at the guaranteed minimum.
- **Rate, reversed (§9 Q2):** `L(r) = L_min · exp(r · S / 127)`.
  - `L_min` = 882 samples (≈ 20 ms, today's shortest).
  - `L_max` = `CRUMPBIT_DELAY_CURVE_MAX` = 126,686 samples (≈ 2.872 s).
  - The span is `S = ln(L_max / L_min)` ≈ 4.967.
  - `L_max` is a constant, so a future small DTCM change does not retune
    saved Scenes. Default `rte` 64 ≈ 0.24 s.
- **Clamp by share (N1 resolved by the reversal):**
  - `rte` gets `EFFECT_PARAM_FLAG_BUFFER_DEPENDENT`, and CrumpBit gets an
    `effective_max(index, share)` hook. It returns the largest `rte` whose
    length fits `share − 2`.
  - Because higher now means longer, the existing top-down clamp is
    correct.
  - The DSP still clamps its target to `length − 2`. A clamp change glides,
    so it is heard as a pitch bend, not a click.
  - *To verify at schedule time:* whether the page's edit range also reads
    `effective_max`, so the pot stops at the clamp.
- **Sync:** `crumpBit_divisionFor()` takes the live maximum as a parameter
  in place of the constant: `key = min(target, max)`. The search is
  otherwise unchanged. The Effect-page label passes
  `fxbuf_effectShare() − 2`. At 120 BPM `1br` (2.0 s) becomes available.
- **Keep the tail on a share change (D8):** when `write_pos` lies inside the
  new length, re-seat with `valid = min(valid, write_pos)`. Otherwise fall
  back to today's full re-seat.

### 5.2 Feedback curve (0 per sample)

The curve is computed per block, piecewise:
- `fb = v / 100` for `v ≤ 100`;
- `fb = 1 + 0.2 · (v − 100) / 27` above that.

The value 127 gives 1.2 (§0A D6). §9 Q3 asks to confirm unity at 100.

### 5.3 Feedback return: DC blocker → tone → drive knee

```
wet ─► DC block ─► tone tilt (ton) ─► × fb ─► drive knee (tdr) ─┐
                                                                ▼
          ½(hL + hR) ─────────────────────────────────────────► + ─► ADC ─► loop
```

The first echo comes from `wet` itself, so it is heard unfiltered: repeat
*n* is shaped *n* − 1 times (§0A D5). The output mix keeps using the raw
`wet`.

- **DC blocker (mandatory above unity):** `y = w − w_prev + R_dc · y_prev`,
  about 5 Hz (`R_dc` ≈ 0.99929). It is always on. Below unity its effect is
  very slight. 8 B of state.
- **Tone tilt (`ton`): one shared one-pole state.**
  - The filter is `s += k · (v − s)`, and the output is `a · s + b · v`.
  - Per block, `ton` selects the coefficients:
    - **below flat:** low-pass `(a, b) = (1, 0)`, cutoff falling to about
      150 Hz at −63;
    - **flat:** `k = 1`, `(1, 0)`. Then `s = v` exactly, so the stage is a
      bit-exact pass-through;
    - **above flat:** high-pass `(a, b) = (−1, 1)`, cutoff rising from
      about 20 Hz to about 2 kHz at +64.
  - `k` comes from a 128-entry flash table (512 B), with no per-block
    `expf`. The ranges are judged by ear (§0A D2).
- **Drive knee (`tdr`): the voice drive law and curve, level-matched.**
  - The gain is `g_t = 0.4 + (d/127)² · 6`, the voice law of
    `SVF_setDrive()`.
  - The knee is `knee(v) = softClipTwo(g_t · v) / g_t`, the voice soft clip
    (`v · tanhXdX(v/2)`, a Padé ratio).
  - The makeup `1/g_t`, computed per block, keeps the small-signal loop
    gain at exactly `fb`. So `fbk` keeps its meaning at every `tdr`, and
    `tdr` only moves the point where the loop starts to saturate.
  - The curve saturates at `2/g_t`:

    | `tdr` | `g_t` | Saturation | Effect |
    |---|---|---|---|
    | 0 | 0.4 | 5.0 | Nearly transparent below the rail: −0.1 dB at full scale (≈ 1.7 LSB), less at lower levels. The 8-bit ADC still clips first. |
    | 127 | 6.4 | 0.31 | A dense, compressed howl, about −10 dB below the rail. |

  - **This stage is the D7 soft knee.** It also gives self-oscillation a
    stable level, because the loop gain falls below 1 at high level.
- **Cost:** see §7. The Padé division is shared with the output filter
  (§5.5).

### 5.4 Output filter (`typ frq res drv`): drive → TPT SVF → type mix

- **Drive:** `xd = softClipTwo(g · x)` per channel, with
  `g = 0.4 + (drv/127)² · 6`.
  - The law, curve and placement are the voice `LP2` path's (input
    saturation). There is no makeup, as in the voices. So `drv` 0 is the
    voice level (−8 dB into the filter) — §9 Q8.
  - **Difference from a voice ZDF filter:** the voice LP/HP/BP types
    saturate inside their integrators. That costs two VDIVs per sample per
    channel, the same as StereoFilter (≈ 212–252 per frame alone), and does
    not fit. Here the resonance itself is linear and only the input is
    driven.
- **Filter:** a linear TPT (Simper) state-variable filter per channel, with
  `L`/`R` sharing the coefficients.
  - **Per block:** `f = valueShaperF2F(frq/127, FILTER_SHAPER) · 0.45` and
    `g_c = fastTan(π f)`, exactly as `SVF_directSetFilterValue()` does.
    `R` follows `SVF_setReso()` (`1 − res/127`, with a floor). Then
    `a1 = 1/(1 + g_c(g_c + 2R))`: one division per block.
  - **State:** 16 B. It is stable under per-block steps: the TPT form is
    unconditionally stable.
- **Type mix: one 4-coefficient output, the same arithmetic for every
  type** (constant CPU). The output is
  `out = c_dry · x + c_x · xd + c_b · bp + c_l · lp`, using the voice
  definitions from `ResonantFilter.c`:

  | `typ` | `c_dry` | `c_x` | `c_b` | `c_l` | Voice form |
  |---|---:|---:|---:|---:|---|
  | LP | 0 | 0 | 0 | 1 | `y1` |
  | HP | 0 | 1 | −2R | −1 | `x − 2R·y0 − y1` |
  | BP | 0 | 0 | 1 | 0 | `y0` |
  | UBP | 0 | 0 | 2R | 0 | `2R·y0` |
  | Nch | 0 | 1 | −2R | 0 | `x − 2R·y0` |
  | Pek | 0 | −1 | 2R | 2 | `y1 − h` |
  | LP2 | 0 | 0 | 0 | 1 | Aliased to LP (§9 Q9) |
  | off | 1 | 0 | 0 | 0 | Undriven input: exact pass-through |

  - The voices' LP output `fastTanh` and their `FILTER_GAIN` (0.883) scaling
    are not copied. The effect output is linear up to the mixer return's
    final saturation.
- **Morph on `typ` (U2):** Morph resolves to an integer, so it steps through
  the list in order (LP → HP → … → off).
  - The four coefficients ramp linearly across each block, from the
    previous block's set to the new one. A type step, a `res` step and a
    Pattern type lock each become a one-block crossfade (≈ 0.73 ms), not a
    click.
  - The ramp origins are recomputed from the previous `typ`/`res` raw bytes
    (2 B), not stored as floats.
- **Resonance bound (§9 Q4):** the voice law reaches Q = 25 (`R` = 0.02).
  With a linear filter nothing limits that peak, which reaches +28 dB at
  the mixer clamp. Recommended: floor `R` at 0.0625 (Q ≤ 8).

### 5.5 One division for all three saturators

Each `softClipTwo()` is a Padé ratio, `N(a)/D(a)` with `D ≥ 945`, so each
call costs one VDIV. The tone knee, output L and output R all have their
inputs ready at the same point in the frame: after `wet` and `h`, before the
loop write.

So the frame computes the three denominators, then **one** reciprocal:
- `inv = 1 / (D_t · D_l · D_r)`;
- then `1/D_t = inv · D_l · D_r`, and likewise for the other two.

This is the S073 batching method already used in the voice ZDF filter. The
denominators are normalised as in `svf_padeDen()`, so the product stays far
inside float range. Cost: one VDIV plus about 8 multiplies, instead of three
VDIVs.

---

## 6. Unchanged

- **The shared stage:** ADC, bit lines, DAC and AC coupling, as after S091
  P1. The R4 shape (`vcvt #7` + VFMA) is kept.
- **Delay mechanics:** the interpolated read, the glide, the `valid` mute,
  the `dpn` pan and the `mix` crossfade.
- **Level, pan and route:** the common rows 0..2, applied after the output
  filter by the manager as today.

---

## 7. CPU budget (instruction-equivalents per stereo frame; VDIV = 13)

| Item | Per frame | Source |
|---|---:|---|
| S0 loop after S091 P1 | 98 | measured, linked |
| S0 per-block work (search, `expf`, 2 VDIV) | ≈ 8 | measured / plan §1.1 |
| §5.1 elastic delay, Rate, Sync, tail; §5.2 feedback curve | 0 per sample, ≈ +1 per block | — |
| DC blocker | +3 | estimate |
| Tone tilt (one state, two coefficients) | +4 | estimate (the one-pole pair prototype was +6) |
| Tone drive knee (gain, Padé N and D, makeup) | +9 | estimate |
| Output TPT SVF × 2, 4-coefficient mix | +28 | prototype +26 with 3 coefficients, +2 |
| Output drive × 2 (gain, Padé N and D) | +16 | estimate |
| Shared division (§5.5): 1 VDIV + ≈ 8 multiplies | +21 | estimate |
| Output-coefficient ramp (4 adds, shared L/R) | +4 | estimate |
| New per-block work (`fastTan`, `a1`, `1/g_t`, tables, origins) | ≈ +6 | estimate |
| **Total, everything** | **≈ 197** | |
| Total without both drives (no Padé, no division) | ≈ 151 | |
| Pre-S091 CrumpBit, for the rule | ≈ 191 | measured instructions |

**Reading the rule.**
- **Counted in instructions,** the full package sits at about the
  pre-S091 figure (≈ 197 vs ≈ 191).
- **In cycles it should not.**
  - The S091 saving measured on the board was 3–4 %, about 150–200
    cycles, against 85 instructions. The old walk cost about 2 cycles per
    instruction.
  - The new code is straight-line FMA work plus one VDIV, about 1–1.2
    cycles per instruction-equivalent: roughly 90–110 cycles.
  - The expected net is about 40–110 cycles per frame **below** the
    pre-S091 reading.
- **The drives are the swing item:** ≈ 46 of the ≈ 91 added.
- **Hardware decides:**
  - Each stage is measured on the board against the pre-S091 image, and
    the `cpu` widget must read at or below it with no underruns.
  - If stage C fails, the fallbacks in order are:
    1. a division-free polynomial clip on the output (≈ −21, but not the
       voice curve);
    2. no output drive (≈ −37);
    3. no tone drive (≈ −9; the knee falls back to the ADC hard clip).
- **Before the schedule is written,** the drive and division code (§5.3–5.5)
  are prototyped on a scratch copy and counted the same way as the S091
  figures. They are the only parts not yet measured.

---

## 8. RAM: `CrumpBitRuntime` stays 76 B (union unchanged)

| Change | Bytes |
|---|---:|
| Today (after S091 P1) | 56 |
| Remove the four ramp-origin floats (`mix`, `feedback`, `gain_l`, `gain_r`); recompute them from the previous raw rows | −16 |
| Previous `mix_raw`, `feedback_raw`, `pan_raw` | +3 |
| AC codes `ac_c_l`/`ac_c_r` from `int32_t` to `int16_t` (values 0..255; loaded and stored once per block) | −4 |
| Tone state `s` | +4 |
| DC blocker `w_prev`, `y_prev` | +8 |
| Output SVF states (2 × 2 floats) | +16 |
| New rows `ton typ frq res drv tdr` | +6 |
| Previous `typ`/`res` raw (output-coefficient ramp origins) | +2 |
| **Total** | **75 → 76 with alignment** |

- **Effect:** the 76 B union and the FXBUF arena do not move. The
  `sizeof(CrumpBitRuntime) == 56u` guard becomes 76.
  `STORAGE_SRAM_MANIFEST.md` records the new layout.
- **The ramp-origin recompute is exact.** Each origin is, by construction,
  the previous block's target, and that target is a pure function of the
  previous raw row. The only difference is the first block after `init`:
  it ramps from the origin computed from zeroed rows instead of from 0.
- **Fallback:** the approved +8 B (union 84 B). It is needed only if the
  sub-type byte (§4, point 5) or a later field must be added now.
- **Flash:** about +2–3 KB (the tone `k` table 512 B, the new code).
  Headroom is 63,084 B.

---

## 9. Open decisions

| # | Question | Recommendation |
|---|---|---|
| Q1 | RAM: fit everything into 76 B with the §8 trims, or spend the approved +8 B instead of trimming | **Trims (76 B)**; keep +8 B in reserve |
| Q2 | Rate: reverse `rte` (higher = longer) with the exponential curve from ≈ 20 ms to the full arena | **Yes** (resolves N1; the top-down clamp then works) |
| Q3 | Feedback: unity at 100, 1.2 at 127, linear segments | **Yes** |
| Q4 | Output resonance: the voice `res` law exactly (Q to 25), or with `R` floored at 0.0625 (Q ≤ 8), because the linear filter does not limit its own peak | **Floor at Q 8** |
| Q5 | Token for the tone drive (the voices' `drv` stays with the filter) | **`tdr`**, long label "ToneDrv" |
| Q6 | Drive cost: accept ≈ +46 per frame for both drives, prototyped before the schedule, with the §7 fallbacks if the board disagrees | **Yes** |
| Q7 | Store the sub-type byte now (+1 B, then 77 → 80 B within the approval), or wait for the second sub-type | **Wait**; §4 keeps a marked place for it |
| Q8 | Output drive level: voice-identical (`drv` 0 = 0.4 gain, −8 dB into the filter), or level-matched like the tone drive | **Voice-identical** (U1: aligned with voices and StereoFilter) |
| Q9 | `LP2` on the output filter: alias it to LP (the voice LP2 is a different naive 2-pole, which would break constant CPU here), or remove it from this row's range | **Alias to LP**, keeping the `MENU_FILTER` table and values identical to the voices |
| Q10 | Should the output filter's `off` also bypass `drv`? | **Yes** (`c_dry` = 1 takes the undriven input) |

Already decided and applied: D8 keep tail (yes); D9 cycle readout (no); D10
`expf` table (not needed; the tone uses its own table).

---

## 10. Stages and verification

The work is one schedule in three stages, with a hardware check after
each. Every stage must pass all of the following:
- **Host:** a CrumpBit runner (`tools/dsp_test/modules/crumpbit.c`
  extended).
- **ARM:** `crumpbit_elf_check` limits raised to the stage's measured
  count.
- **Mixer:** loops unchanged (G5).
- **Link:** `link_budget.py` (union 76 B, `_sfxbuf` unchanged).
- **Board:** the `cpu` widget at or below the pre-S091 reading, with no
  underruns.

| Stage | Contents | Host laws (new) | Hardware |
|---|---|---|---|
| **A** | §5.1, §5.2, §8 trims; no new rows | Rate curve endpoints and monotonicity; `effective_max` = largest fitting `rte` for every share; Sync against the live maximum; tail kept when `write_pos` fits; ramp-origin recompute equals the stored-origin form after the first block | `DEV_FXBUF_FORCE_VOICE_UNITS` 0/1/2/12: clamp, glide and tail; self-oscillation at `fbk` 127 (low monitor level) |
| **B** | §4 sub-type grouping; `ton`, `tdr`; DC blocker, tilt, knee; page s0/s1 (`ton` replaces `mix`) | `ton` flat is a bit-exact pass-through of the return; output bounded and no NaN at `fb` 1.2 with silence, DC and full-scale input; no DC lock-up over 60 s of oscillation; `tdr` 0 within 2 LSB of the stage A loop bytes below the ADC rail (the knee's −0.1 dB at full scale); sub-type row table matches the descriptors | Listening: tilt range, knee, howl; no thump on entry |
| **C** | `typ`, `frq`, `res`, `drv`; drive, TPT SVF, type mix, shared division; page s2/s3 | `typ` `off` with a settled ramp is a bit-exact pass-through; each type matches a float reference of the §5.4 formula; peak gain ≤ Q cap; `typ` Morph sweep has no step larger than one block's ramp; the batched reciprocal matches three separate divisions within 2 ulp | Listening: types and Morph sweep; LFO on `frq`; `drv` against a voice filter at the same settings; CPU worst case |

**Afterwards:**
- `EFFECTS_MIXER_DSP_REFERENCE.md`: §4.4, §5.1 (arena use) and §6.2a.
- `CPU_USE_DSP_AUDIT.md`: a new item with the measured costs.
- `STORAGE_SRAM_MANIFEST.md`: the runtime layout.
- `MEMORY.md`: a binding note on the sub-type grouping (§4) and the
  full-lane state.

---

## 11. What this plan changes in `S091_P1_CrumpBit_Expansion.md`

| Original | Now |
|---|---|
| §3.3 options A/B: two tone rows `ton`/`lct` on the whole write | One bipolar `ton` on the feedback return (§0A D5), plus `tdr` (§5.3) |
| §3.2 soft knee "≈ +4–5, not prototyped" | The knee is the `tdr` drive stage with makeup (§5.3) |
| §3.4 output SVF with modes LP/BP/HP/notch, row `fmd` | `typ` with the eight voice types, morphable; `frq`, `res`, `drv` with the voice laws (§5.4) |
| §5 RAM 68–72 B | 76 B with the §8 trims; 6 new rows instead of 5 |
| §6 rows `ton lct cut res fmd`; screens 3/4 `snc dpn ton lct` / `cut res fmd –` | §2 and §3 |
| §3.5 recommended package ≈ 151 per frame | ≈ 151 without drives; ≈ 197 with both drives (§7) |
