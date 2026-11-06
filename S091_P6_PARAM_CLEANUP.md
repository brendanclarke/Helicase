# S091 P6 — CrumpBit Parameter Names, Page Order and the `dly` Delay Time

**Status:** **implemented** 2026-11-05 (§8), with all decisions taken
(Q1–Q6 §7.1; Q7–Q11 accepted as recommended, §7.2); revised 2026-11-06
(§8.8 `dly` end-row labels; §8.9 `grn` below `/64` and the `grn` tap
fade). Host gates pass; the hardware checks (§5, §8.6, §8.9.6) are still
to run. Not committed.
**Base:** the working tree on `dev-ph7-voice_expansion` after S091 P5 and
its follow-ups (`S091_P5_IMPLEMENTATION.md` §11 pitch rows ±36 and the
`pit` window glide, §12 the `pit` fine row), uncommitted at the time of
writing.
**Scope:**
- names, labels and page order for every CrumpBit sub-type;
- `snc` removed from `dly`, and `dly`'s Rate rebuilt as one tempo-relative
  delay-time curve with absolute ends (fixing the Rate glitch, §3.5).

No stored key changes, no RAM change.

---

## 1. Requests (user, 2026-11-05)

| # | Request |
|---|---|
| R1 | In general `dpn` should appear **after `ton`** when both are on a page, and be named **`spn`, "SubPan"** |
| R2 | ~~Is there any point in a `snc` parameter for `dly`?~~ **Decided: remove `snc` from `dly`** ("overly complicated to have two controls") |
| R3 | On `pit`, **swap `win` and `fin`** so the fine pitch sits next to the coarse pitch (the interval), and **label the interval `pit`, category "Shift", long name "SemiPtch"** |
| R4 | The category and long names of most sub-types' rows still show the delay names: on `cmb`, `not` (should be **`nte`**) shows **"Delay Rate"** when opened; it should be **"Comb Note"**, and so on |
| R5 | **A new delay-time table for `dly`** instead of re-using the StepScale table. Keep the log-derived natural curve. Add **4 values below 1/64 and 4 above /2**, scaling at 0 to the **absolute minimum** and at 127 to the **absolute maximum** delay time possible. **Clamp** any value that tempo or the available buffer makes unreachable |
| R6 | **The `dly` Rate control is glitched:** it jumps in steps of 2, and there is adjustment beyond the displayed value that takes extra knob travel to recover from (travel past the minimum/maximum does not change the sound) |

---

## 2. How names reach the screen today

Every CrumpBit row has one descriptor (`crumpBit_descriptors`,
CrumpBitParameters.c) with a fixed **short name** (3 characters), **long
name** (≤ 8) and **category** (≤ 8). Only the short name can vary by
sub-type, and only on the Effect page:

| Where | Field | Source | Per sub-type? |
|---|---|---|---|
| Effect page cells (compact) | short | `crumpBit_ui.name3` (`crumpBit_subtypeRowNames`), via `menuEffects_shortName()` (menuEffects.c:579) | **yes** |
| Effect page, a cell opened (full view header) | category + long | `cell.descriptor->category` / `long_name` (menu.c:15038–15045) | no |
| STEP automation target (detail row) | category + long | `menu_stepAutomationEffectLabel()` (menu.c:14081–14096) | no |
| Modulation / LFO target, short | short | `effect_descriptor->base.short_name` (menu.c:6228) | no |
| Modulation / LFO target, full | category + long | menu.c:6266–6280 | no |

So `cmb`'s Rate cell shows `not` on the Effect page (hook) and **"Delay"
"Rate"** when opened (descriptor), which is R4. The same happens on every
sub-type row outside `dly`, and on every automation and LFO page.

The voices already solve the same problem for the wavefolder chain:
`menu_instrumentCellLabels()` (menu.c:12243–12280) swaps category and long
name by context. P6 gives Effects the equivalent through the type's hooks.

The current descriptor names of the sub-type rows (all written for `dly`):

| Row | Short | Long | Category |
|---|---|---|---|
| 5 `mix` (shared MIX stage) | `mix` | Mix | **Delay** |
| 6 `fbk` | `fbk` | Feedback | Delay |
| 7 `rte` | `rte` | Rate | Delay |
| 9 `snc` | `snc` | Sync | Delay |
| 10 `dpn` | `dpn` | DlyPan | Delay |
| 11 `ton` | `ton` | Tone | Delay |
| 15 `aux` | `aux` | Aux | CrumpBit |
| 23 `fin` | `fin` | Fine | Pitch |

`mix` is the shared MIX stage (binding, S091 P3), not a delay control, so
its "Delay" category is wrong in every sub-type, `dly` included.

---

## 3. Findings and proposals

### 3.1 R1 — `dpn` → `spn` "SubPan", after `ton`

**What `dpn` is per sub-type:** the wet's stereo balance in `dly`, `cmb` and
`pit` (S075 F2-E law); Position in `grn`; Width in `rvb`. "SubPan" fits the
first three. `grn` and `rvb` keep their own names (`pos`, `wid`).

**Proposal:**
- Base descriptor: short `spn`, long "SubPan", category "CrumpBit" (§3.4).
- Page order: `ton` before `dpn` wherever both are on one screen.

| Sub-type | Screen 3 now | Screen 3 after |
|---|---|---|
| `dly` | `snc dpn ton -` | `ton spn - -` (`snc` removed, §3.2) |
| `cmb` | `pol dpn ton -` | `pol ton spn -` |
| `rvb` | `frz wid dmp -` | `frz dmp wid -` |
| `pit` | `fbk fin dpn ton` | `fbk win ton spn` (with R3, §3.3) |
| `grn` | no `ton` on its page | unchanged (`pos` stays on screen 2) |

**Not changed:**
- The file key `crump_dly_pan` (stored keys are permanent identities).
- FX lane 9 (lanes are numbers, not page positions).

The code identifier becomes `CRUMPBIT_PARAM_SUB_PAN` (Q4, decided).

### 3.2 R2 — `snc` removed from `dly` (decided)

- `dly` no longer reads `snc`. The row stays: it is polarity in `cmb` and
  freeze in `grn`/`rvb`, and FX lane 6 in those sub-types.
- `dly`'s page loses the `snc` cell (§3.1). Its FX lane 6 becomes empty
  (Q9): the CrumpBit registry row's `lanes[]` (EffectsManager.c:127) must
  equal the `dly` map (self-check code 11), so both change together.
- A stored `snc` value in a `dly` Scene is kept (the row is shared) and
  ignored by `dly`. Tempo sync now lives in the Rate curve itself (§3.5).
- The `snc` label in `dly` is `---` in any view that still shows the row.
- **Removed with it:**
  - the Sync search `crumpBit_divisionFor()` (113 instructions per block;
    `dly` was its only user);
  - `crumpBit_syncDivision()` (the label's twin);
  - `crumpBit_divisionLength()`;
  - `CRUMPBIT_DIVISION_NONE`;
  - the `dly` call path through `crumpBit_targetSamples()`;
  - the StepScale dependency of CrumpBitEffect.c, if nothing else needs it.

### 3.3 R3 — `pit`: `fin` next to the interval; the interval named `pit` "Shift" "SemiPtch"

**Page now:** screen 2 `mix sub int win`, screen 3 `fbk fin dpn ton`.

**Page after** (R3, with R1's order):

| Screen | Cells |
|---|---|
| 2 | `mix sub pit fin` |
| 3 | `fbk win ton spn` |

**Labels:**
- The interval (`aux` in `pit`): short **`pit`**, category **"Shift"**, long
  **"SemiPtch"**.
- `fin` in `pit`: short `fin`, category "Shift", long **"FinePtch"** (Q3,
  decided).

The interval's short name `pit`, next to the `sub` cell's value `pit`, is
accepted (Q3).

### 3.4 R4 — per-sub-type category and long names, everywhere

**Mechanism (Q5, decided: one table replaces `name3`):**
- A new hook in `effect_ui_hooks_t`:
  `uint8_t (*labels)(uint8_t index, uint8_t variant, const char **short3,
  const char **category, const char **long_name)`. It returns nonzero and
  overrides any of the three it sets. `variant` is the value of the type's
  `lane_variant_row` (CrumpBit: `sub`) in the record being shown.
- An EffectsManager helper,
  `effects_rowLabels(scene, type, index, &short3, &category, &long_name)`.
  It starts from the descriptor, reads the variant from that Scene's
  record, and applies the hook. Every site in §2 calls it.
- The Effect page uses the shown record (`menuEffects_shownParam()`, as
  `name3` does now), so a held step or the Morph view names rows by the
  sub-type shown.
- An automation or LFO target in another Scene uses that Scene's stored
  `sub`.
- `name3` is removed (only CrumpBit used it).

**The table** (category / long / short; ≤ 8 / ≤ 8 / 3 characters; bold =
differs from today):

| Row | `dly` | `cmb` | `pit` | `grn` | `rvb` |
|---|---|---|---|---|---|
| `fbk` (6) | Delay / Feedback / `fbk` | **Comb / Feedback / `fbk`** | **Shift / Feedback / `fbk`** | **Grain / EchoFdbk / `fbk`** | **Reverb / Decay / `dcy`** |
| `rte` (7) | **Delay / Time / `tim`** (Q10) | **Comb / Note / `nte`** | **Shift / Window / `win`** | **Grain / EchoTime / `tim`** | **Reverb / Size / `siz`** |
| `snc` (9) | **(not read; `---`)** | **Comb / Polarity / `pol`** | (off the page; `---`) | **Grain / Freeze / `frz`** | **Reverb / Freeze / `frz`** |
| `dpn` (10) | **Delay / SubPan / `spn`** | **Comb / SubPan / `spn`** | **Shift / SubPan / `spn`** | **Grain / Position / `pos`** | **Reverb / Width / `wid`** |
| `ton` (11) | Delay / Tone / `ton` | **Comb / Tone / `ton`** (Q6) | **Shift / Tone / `ton`** | (not on its page) | **Reverb / Damping / `dmp`** |
| `aux` (15) | (no meaning; `---`) | (no meaning; `---`) | **Shift / SemiPtch / `pit`** | **Grain / Pitch1 / `p1`** | (no meaning; `---`) |
| `fin` (23) | — | — | **Shift / FinePtch / `fin`** | — | — |

**Categories per sub-type:** `dly` "Delay", `cmb` "Comb", `pit` "Shift",
`grn` "Grain", `rvb` "Reverb".

**Shared rows** keep their names, except `mix`: category "Delay" →
**"CrumpBit"**.

**Base descriptor names** (Q2, decided; shown where no sub-type applies),
all category "CrumpBit":

| Row | Long name |
|---|---|
| `fbk` | Feedback |
| `rte` | Rate |
| `snc` | **Switch** |
| `dpn` | SubPan |
| `ton` | Tone |
| `aux` | Aux |
| `fin` | FinePtch |

### 3.5 R5, R6 — the `dly` delay-time curve, and the Rate glitch

#### 3.5.1 The glitch (R6), measured

`dly`'s Rate today is two curves:
- **`snc` off:** the free-running `crumpBit_rateTable` (20 ms .. 2.871 s,
  log);
- **`snc` on:** the same `rte` value, snapped to the StepScale position
  nearest in log time (`crumpBit_divisionFor()`). The label then shows that
  position: a stop's symbol, else its raw CC number (`stepScale_formatShort()`).

The two curves have different ranges and densities. Measured on the host at
120 BPM with the whole arena (`crumpBit_syncDivision()` for every `rte`):

| `rte` | Plays (with `snc` on) |
|---|---|
| 0..11 | **all CC 0** (`/64`, 31 ms): the free curve starts at 20 ms, below it |
| 12..99 | CC 10..127, **30 steps jumping 2 positions, one jumping 3, 40 steps not changing** |
| 100..127 | **all CC 127** (`/2`, 1 s): the free curve continues to 2.871 s, past StepScale's top |

That is the report exactly:
- the shown value (the CC number) jumps by 2;
- 11 steps at the bottom (`rte` 0..11) and 27 at the top (100..127) are
  dead travel that changes neither the sound nor the display;
- recovering from the top takes 27 steps of turning before anything moves.

With `snc` off the label is the plain number and there is no glitch, but
also no tempo sync.

#### 3.5.2 The new curve (R5)

One 128-step table, **`crumpBit_delayTime`**, every step a distinct delay
time, so no step is dead and every step changes the display:

| Rows | Law | Anchors |
|---|---|---|
| **0..3** (4) | geometric from the absolute minimum to just below 1/64: `t(k) = t_min · (t_/64 / t_min)^(k/4)` | row 0 = `t_min`; row 4 = `/64` |
| **4..123** (120) | the "log-derived natural curve": the 14 StepScale stops `/64 32t /32 16t d32 /16 8Tr d16 /8 4Tr d/8 /4 d/4 /2` (6..192 ticks) pinned at exact tick counts, equal ratios between neighbouring stops (the `grn` delay-time construction, `grain_time_table()`); positions by the stop's log position over 6..192 ticks | row 4 = `/64` (6 ticks), row 123 = `/2` (192 ticks) |
| **124..127** (4) | geometric from `/2` to the absolute maximum: `t(k) = t_/2 · (t_max / t_/2)^((k − 123)/4)` | row 127 = `t_max` |

- **Tempo:**
  - rows 4..123 are in 96-PPQ ticks, so their time follows BPM, as `grn`'s
    `tim` does (tempo sync, without a second control);
  - rows 0..3 and 124..127 bridge from the tempo's `/64` and `/2` to the
    absolute ends, so they move with tempo too, but their endpoints do not.
- **`t_max`:** the longest delay the whole arena holds,
  `CRUMPBIT_DELAY_CURVE_MAX` (126,654 samples, 2.871 s).
- **`t_min`:** the engine's shortest usable delay. Q7: the guard, 2
  samples (0.045 ms), recommended.
- **Ordering:** at slow tempos `/2` can exceed `t_max` (below 41.8 BPM:
  `/2` = 192 ticks × 27,567.5 / BPM samples). Then every row whose time
  exceeds `t_max` plays `t_max`, and rows 124..127 are `t_max`. At 300 BPM,
  `/64` is 551 samples, still far above `t_min`, so the bottom segment always
  rises.
- **Clamping (R5: "if tempo or buffer makes a value unreachable,
  clamp").** The time is clamped to the live share's longest delay, as
  today (`len − guard`).
  - **The knob also stops there** (Q8, recommended): `rte` stays
    BUFFER_DEPENDENT, and `effective_max` returns the last row whose time
    fits the share at the current tempo. The page, pots, lanes and the DSP
    then stop at the same row, so no knob travel is dead. This is the clamp
    `grn`'s `tim` already uses (`crumpBit_subtypeRateLimit()`, the grn case).
  - Rows 0..3 always fit.
- **Labels (Q10):**
  - a stop row shows the stop's name (`/64` … `/2`, the first 14 of
    `crumpBit_grainStopNames`);
  - every other row in 4..123 shows its row number, as `grn`'s `tim` does;
  - rows 0..3 and 124..127 show their time: ms below 1 s (`  2` … `999`),
    else seconds (`1.4`, `2.8`).
- **DSP:**
  - `crumpBit_runDelay()` computes the target per block from the table at
    the current tempo (ticks × samples per tick for rows 4..123; the end
    segments by `t^(k/4)` with two square roots), clamps it to `len −
    guard`, and glides as today (150 ms tape glide, unchanged);
  - `crumpBit_targetSamples()` and the Sync search go;
  - cost: per block lower than today by about the Sync search (113 + its
    three length evaluations), roughly −4 per frame for `dly`; the frame
    loop is untouched.
- **Generator:** `gen_crumpbit_tables.py` gains `crumpBit_delayTicks`
  (rows 4..123 in ticks, with the stop positions as
  `crumpBit_delayStopCc[14]`); rows 0..3 and 124..127 are computed per block
  (they depend on tempo and `t_max`). Check: strictly increasing; the stops
  exact; row 4 = 6, row 123 = 192.
- **Existing Scenes:** a stored `dly` `rte` keeps its number but now means
  a position on the new curve (no backward compatibility, user rule).

---

## 4. Implementation outline

| File | Change |
|---|---|
| `Core/DSP/Effects/EffectsManager.h` | `labels` in `effect_ui_hooks_t` (appended, so other types' hook tables keep their positional fields; `name3` removed); `effects_rowLabels()` declaration |
| `Core/DSP/Effects/EffectsManager.c` | `effects_rowLabels()`; CrumpBit registry `lanes[]` lane 6 → none (§3.2) |
| `Core/Menu/menuEffects.c` | `menuEffects_shortName()` (579) through the helper (shown record) |
| `Core/Menu/menu.c` | the four sites of §2: the full-view header (15038), the STEP automation label (14081), the modulation target short (6228) and full (6266) |
| `Core/DSP/Effects/CrumpBit/CrumpBitParameters.h` | `CRUMPBIT_PARAM_DLY_PAN` → `_SUB_PAN`; the layout comment |
| `Core/DSP/Effects/CrumpBit/CrumpBitParameters.c` | the per-sub-type label table and `crumpBit_uiLabels()` (replacing `crumpBit_subtypeRowNames`/`crumpBit_uiName3()`); the base names (§3.4); `crumpBit_layout` split: `dly` (screen 3 `ton spn`) and `cmb`/`rvb` (screen 3 `snc ton dpn`); `crumpBit_layoutPitch` (screen 2 `mix sub aux fin`, screen 3 `fbk rte ton dpn`); the `dly` Rate label (§3.5.2) in place of the Sync division; `snc` `---` in `dly` |
| `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c/.h` | `dly` target from `crumpBit_delayTicks` (§3.5.2); the Sync search and its helpers removed; the `dly` Rate limit (`crumpBit_subtypeRateLimit()` default case) on the new curve at the current tempo; the `dly` lane 6 → none; `_SUB_PAN` rename; the comments |
| `Core/DSP/Effects/CrumpBit/CrumpBitTables.c/.h`, `tools/plots/gen_crumpbit_tables.py` | `crumpBit_delayTicks[120]`, `crumpBit_delayStopCc[14]`; regenerate; `--check`. `crumpBit_rateTable` stays only if something else still reads it (the `dly` free curve was its user; checked at implementation) |
| `Core/DSP/Effects/StereoFilter/StereoFilterParameters.c` | none (its hook table gets the appended field as NULL) |
| `tools/dsp_test/modules/crumpbit.c` | CB1 (the Sync search law) replaced by the `dly` curve law: every row distinct and increasing at 30/120/300 BPM; the stops exact; rows 0/127 at `t_min`/`t_max`; the clamp equals `effective_max`. The CB4 reference: the new target law (no Sync). CB6: the `dly` Rate limit. CB9: the `dly` map (lane 6 none), the registry equality |
| `tools/dsp_test/Makefile` | break 1 (the Sync key) replaced by a `dly` curve break; `crumpbit_elf_check` without `crumpBit_divisionFor` |
| `tools/dsp_test/crumpbit_cpu.py` | `dly` without the Sync search term |
| Docs | `EFFECTS_BUS_REFERENCE.md` §4.2a (hooks, `dly` lanes) and §8.6 (row table, page orders, names, the `dly` Rate); `EFFECTS_MIXER_DSP_REFERENCE.md` §4.4 (the `dly` target law) and §6.2a (`dly` CPU); `DSP_TEST.md`; the S091 P5 §11.6/§12.6 lists |

**Compile-time checks:**
- The label table stores fixed arrays (`char [4]` short, `char [9]`
  category and long), so a name over its LCD width fails to compile.
- A `_Static_assert` that the table covers every sub-type.

**Host tests:** `CrumpBitParameters.c` is not compiled on the host (Menu
dependencies), so the labels are checked on hardware (§5). The `dly` curve,
its clamp and its DSP target are host laws.

**CPU / RAM:** no RAM. `dly` per block drops (no Sync search); every other
sub-type is unchanged; the labels are foreground flash strings.

---

## 5. Hardware checks

| # | Check | Expected |
|---|---|---|
| P6-1 | Each sub-type's Effect page: names and order | §3.1, §3.3, §3.4 tables; `ton` before `spn`/`wid`; `pit` screen 2 `mix sub pit fin`; `dly` screen 3 `ton spn` (no `snc`) |
| P6-2 | Open every sub-type row (full view) | its sub-type's category and long name (e.g. `cmb` `nte`: "Comb Note") |
| P6-3 | STEP automation and LFO target pages on an Effect row, Scenes on different sub-types | each named by its own Scene's `sub` |
| P6-4 | Held step / Morph view on the Effect page | names follow the shown sub-type |
| P6-5 | StereoFilter pages | unchanged |
| P6-6 | `dly` `tim` swept slowly over its whole range at 120 BPM | every step changes the display and the delay; no step of 2; no dead travel at either end; stops show their names |
| P6-7 | `dly` with a stop selected, tempo changed | the echo stays on the beat (the glide bends it there) |
| P6-8 | `dly` at a slow tempo (e.g. 40 BPM) and with voice units claimed | the knob stops at the last reachable row; nothing beyond it to recover from |

---

## 6. Order of work

1. The `labels` hook, the helper, the four menu sites, `name3` removed.
2. The CrumpBit label table, the base names, the layouts (R1, R3, R4).
3. The `dly` curve: the generator table, the DSP target, the clamp, the
   label; `snc` and the Sync search removed from `dly` (R2, R5, R6).
4. Host laws, breaks, gates; then the documentation.

---

## 7. Decisions and open questions

### 7.1 Decided (user, 2026-11-05)

| # | Question | Decision |
|---|---|---|
| Q1 | `snc` in `dly` | **Remove it** (two controls are overly complicated); tempo sync moves into the new delay-time curve (§3.2, §3.5) |
| Q2 | Base names | category "CrumpBit", neutral long names (Feedback, Rate, Switch, SubPan, Tone, Aux, FinePtch) |
| Q3 | `pit` labels | the interval `pit` next to `sub`'s value `pit` is fine; `fin` long name "FinePtch" |
| Q4 | `CRUMPBIT_PARAM_DLY_PAN` → `_SUB_PAN` | yes, code only; the file key stays |
| Q5 | Fold `name3` into `labels` | yes |
| Q6 | `cmb` `ton` | keep "Tone" (`ton`) |

### 7.2 From R5/R6 — accepted as recommended (user, 2026-11-05)

**Q7. The absolute minimum delay `t_min` (row 0).**
- **Recommendation: 2 samples** (0.045 ms; the interpolation guard, the
  same margin the maximum keeps): the literal engine minimum, as asked.
- Rows 0..3 then step through comb and flanger territory to `/64` (at
  120 BPM about 2, 10, 52 and 268 samples, then 1,378).
- Alternative: 1 ms (44 samples), which keeps rows 0..3 out of the comb
  region.

**Q8. What "clamp" means for unreachable rows.**
- **Recommendation: the knob stops at the last reachable row.** `rte`
  stays BUFFER_DEPENDENT and `effective_max` follows the tempo and the
  share, as `grn`'s `tim` does. That leaves no dead travel, which is R6's
  complaint.
- Alternative: every row stays settable and plays the clamped time. That
  re-creates dead travel at slow tempos and with a small share.

**Q9. `dly` FX lane 6** (it drove `snc`).
- **Recommendation: empty.** No other `dly` row fits it.
- Alternative: `ton` or `spn` already have lanes (10, 9), so there is
  nothing to move there.

**Q10. The `dly` Rate's name and labels.** (Labels revised by the user
after implementation: the end rows show their number, §8.8.)
- **Recommendation:** "Delay / Time / `tim`", as `grn`'s delay time is
  named. Stops show their names, other core rows their number, the
  8 end rows their time (ms below 1 s, else seconds with one decimal).
- Alternative: keep "Rate / `rte`".

**Q11. `crumpBit_rateTable`'s other users.**
- The free 20 ms .. 2.871 s curve fed `dly` and the `dly` Rate limit. If
  nothing else reads it after P6, remove it and its generator law (512 B
  flash).
- **Recommendation:** remove it if unused; check at implementation.

---

## 8. Implementation (2026-11-05)

Line numbers are for the working tree after P6. "Before" is the pre-P6 tree
(S091 P5 and its §11/§12 follow-ups, uncommitted).

### 8.1 What was built, and where it differs from the plan

Built as planned: R1–R6, Q1–Q11. Five details differ from §3/§4 or were
added at implementation:

| # | Plan | Built | Why |
|---|---|---|---|
| D1 | one helper `effects_rowLabels(scene, type, index, …)` reading the Scene's record | three helpers: `effects_rowLabels(type, variant, index, …)` (descriptor names, then the hook); `effects_targetLabels(scene, id, use, …)` (a target id in a given Scene: automation, LFO); `effects_recordVariant(record)` (a record's clamped `sub`). The Effect page passes the **shown** record's variant through `menuEffects_cellLabels()` | the Effect page names rows by the shown record (held step, Morph view), which is not a Scene's stored record; one helper per source keeps that explicit |
| D2 | label table of fixed `char[4]`/`char[9]` arrays | pointers to string literals, each wrapped in `CB_S3()`/`CB_S8()`, which fail to compile (negative array size) when a name is longer than 3 or 8 characters (checked: a 4-character short name and a 9-character long name both stop the build) | NULL is needed for "keep the descriptor's name"; the length check is the same |
| D3 | a `_Static_assert` that the table covers every sub-type | the table is dimensioned `[CRUMPBIT_SUBTYPE_COUNT]` with designated initialisers; a sub-type added later without an entry falls back to the base names ("CrumpBit / Feedback", …) | C11 cannot assert on pointer contents; the fallback is safe and readable |
| D4 | the knob stops at the last reachable row (Q8) | the same, and **never past the first row that plays `t_max`** | below 41.8 BPM `/2` exceeds `t_max`, so every row from the first one at `t_max` up plays `t_max`: with the whole arena that was dead travel (R6), 5 rows at 40 BPM, 15 at 30 BPM, 29 at 20 BPM. The knob now stops at row 122 at 40 BPM, row 112 at 30 BPM, row 98 at 20 BPM |
| D5 | the end rows by `t^(k/4)` | `crumpBit_delayRow()` is **loop-free**: `q^k` for k = 0..3 is `q` and `q²` picked by k's two bits, and row 127 is `t_max` itself (exact by construction, not `t_/2 · q_hi⁴` rounded) | an exact top row; and the limit search (eight probes of the curve) then has no loop, so the CPU gate's per-block path bounds it (§8.4) |

The table the plan called `crumpBit_delayTime` is `crumpBit_delayTicks`
(rows 4..123 in ticks, generated). Rows 0..3 and 124..127 depend on tempo,
so they are computed per call (§3.5.2, "Generator").

### 8.2 Changes by file

**Labels (R1, R3, R4; Q2–Q5)**

| File | Lines | Change |
|---|---|---|
| `Core/DSP/Effects/EffectsManager.h` | 158–167, 185–186 | **modify** `effect_ui_hooks_t`: `name3` removed; `labels(index, variant, &short3, &category, &long_name)` added, with its doc |
| | 548–575 | **add** the declarations and docs of `effects_rowLabels()`, `effects_targetLabels()`, `effects_recordVariant()` |
| `Core/DSP/Effects/EffectsManager.c` | 432–446 | **add** `effects_recordVariant()`: `record->normal[lane_variant_row]`, clamped to `lane_variant_count − 1` (0 for a type without variants) |
| | 447–473 | **add** `effects_rowLabels()`: the descriptor's short/category/long, then `entry->ui->labels` |
| | 474–486 | **add** `effects_targetLabels()`: a target id in one Scene → its type, its record's variant, then `effects_rowLabels()` |
| `Core/Menu/menuEffects.h` | 100–110 | **add** `menuEffects_cellLabels()` declaration and doc |
| `Core/Menu/menuEffects.c` | 572–583 | **modify** `menuEffects_shortName()` (PARAM case): through `menuEffects_cellLabels()` instead of `name3` |
| | 592–611 | **add** `menuEffects_cellLabels()`: the variant from the **shown** record (`menuEffects_shownParam(lane_variant_row)`), clamped, then `effects_rowLabels()` |
| `Core/Menu/menu.c` | 6226–6233 | **modify** modulation target, short name: `effects_targetLabels()` (active Scene) |
| | 6277–6289 | **modify** modulation target, category + long: `effects_targetLabels()` |
| | 14091–14110 | **modify** `menu_stepAutomationEffectLabel(scene, target)`: `effects_targetLabels(…, INSTRUMENT_TARGET_AUTOMATION, …)`; the doc |
| | 14790 | **modify** its caller (passes the Scene) |
| | 15059–15061 | **add** full-view header: `menuEffects_cellLabels(&cell.fx, NULL, &cat, &lng)` for an Effect cell |
| `Core/DSP/Effects/CrumpBit/CrumpBitParameters.h` | 68, 154 | **modify** `CRUMPBIT_PARAM_DLY_PAN` → `CRUMPBIT_PARAM_SUB_PAN` (Q4; the file key `crump_dly_pan` is unchanged) |
| `Core/DSP/Effects/CrumpBit/CrumpBitParameters.c` | 108–191 | **modify** base names (Q2): category "CrumpBit" on `mix fbk rte snc spn ton aux fin`; `snc` "Switch"; `dpn` → `spn` "SubPan"; `fin` "FinePtch" |
| | 351–431 | **add** `CB_S3`/`CB_S8`/`CB_LABEL`/`CB_CAT`, `crumpbit_label_t`, `crumpBit_labelRows[7]`, `crumpBit_labels[5][7]` (the §3.4 table); **remove** `crumpBit_subtypeRowNames` |
| | 737–775 | **add** `crumpBit_uiLabels()` (the hook); **remove** `crumpBit_uiName3()` |
| | 806–904 | **modify** layouts: `crumpBit_layout` (`dly`) screen 3 `ton spn - -`; **add** `crumpBit_layoutCombReverb` (`cmb`/`rvb`) screen 3 `snc ton spn -` (`pol ton spn`, `frz dmp wid`); `crumpBit_layoutPitch` screen 2 `mix sub aux fin` (`mix sub pit fin`), screen 3 `fbk rte ton spn` (`fbk win ton spn`) |
| | 906–911 | **modify** `crumpBit_uiLayout()`: `cmb`, `rvb` → `crumpBit_layoutCombReverb` |
| | 939 | **modify** `crumpBit_ui`: `.labels = crumpBit_uiLabels` |
| `tools/dsp_test/modules/crumpbit.c` | (rename) | `CRUMPBIT_PARAM_SUB_PAN` |

**The `dly` delay time (R2, R5, R6; Q1, Q7–Q11)**

| File | Lines | Change |
|---|---|---|
| `tools/plots/gen_crumpbit_tables.py` | 93, 139–170 | **add** `DELAY_STOP_TICKS`, `DELAY_LOW_ROWS`/`DELAY_CORE_ROWS`, `delay_stop_cc()`, `delay_ticks_table()`; check C1 replaced (ticks strictly increasing, 6 at row 4, 192 at row 123, every stop exact at its row); **remove** `rate_table()` and the `RATE_*` constants (Q11) |
| `Core/DSP/Effects/CrumpBit/CrumpBitTables.c` | 5–41 | regenerated: **add** `crumpBit_delayTicks[120]`, `crumpBit_delayStopCc[14]` = {4, 14, 28, 38, 42, 52, 61, 66, 75, 85, 89, 99, 113, 123}; **remove** `crumpBit_rateTable[128]` (no other user, Q11) |
| `Core/DSP/Effects/CrumpBit/CrumpBitTables.h` | 20–28, 67–85 | **add** `CRUMPBIT_DELAY_LOW_ROWS` 4, `_CORE_ROWS` 120, `_STOPS` 14 and the externs; **remove** the `crumpBit_rateTable` extern; docs |
| `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c` | 46–59 | **modify** the `CRUMPBIT_LOOP_GUARD` doc (it is also `t_min`, Q7) |
| | 219–224 | **modify** the 128-row assert's message; **add** `_Static_assert(LOW_ROWS + CORE_ROWS + 4 == 128)` |
| | 296–346 | **add** `CRUMPBIT_DELAY_MIN` (= guard, 2 samples), `CRUMPBIT_DELAY_HIGH0` (123), `crumpbit_delay_curve_t` {per_tick, t2, q_lo, q_hi}, `crumpBit_delayCurve()`: `t_/64`, `t_/2` (each held at `t_max`), `q_lo = (t_/64 / 2)^¼`, `q_hi = (t_max / t_/2)^¼` (two `sqrtf`) |
| | 348–378 | **add** `crumpBit_delayRow()` (D5) |
| | 380–386 | **add** `crumpBit_delaySamples(rate, bpm)` |
| | 437–473 | **add** `crumpBit_delayLimit(max_samples, bpm)` (D4): seven halving probes and an eighth for the rows below `m = min(max, t_max)`, then the first row at or above `m` when it fits |
| | 517–519 | **modify** `crumpBit_subtypeRateLimit()` default case (`dly`): `crumpBit_delayLimit(max, seq_getBpm())` instead of the rate-table search |
| | 1423–1445 | **modify** `crumpBit_runDelay()` and its doc: `target = min(crumpBit_delaySamples(rate, bpm), len − 2)`; the glide and the loop are unchanged |
| | 3183 | **modify** the `dly` lane map: lane 6 `EFFECT_LANE_NONE` (Q9); the lane-map comment table |
| | (removed) | `crumpBit_rateSamples()`, `crumpBit_rateLimit()`, `crumpBit_divisionLength()`, `crumpBit_divisionFor()`, `crumpBit_syncDivision()`, `crumpBit_targetSamples()`, `CRUMPBIT_DIVISION_NONE`, the `STEP_SCALE_COUNT` assert, `#include "StepScale.h"` |
| `Core/DSP/Effects/CrumpBit/CrumpBitEffect.h` | 21–23, 276–329 | **add** the `crumpBit_delaySamples()` declaration and doc; **remove** `crumpBit_rateSamples()`, `crumpBit_rateLimit()`, `crumpBit_syncDivision()`; docs of `CRUMPBIT_DELAY_CURVE_MAX`, `crumpBit_shareMaxSamples()`, `crumpBit_subtypeRateLimit()` |
| `Core/DSP/Effects/EffectsManager.c` | 127–130 | **modify** the CrumpBit registry `lanes[]`: lane 6 `EFFECT_LANE_NONE` (it must equal the `dly` map, boot self-check code 11) |
| `Core/DSP/Effects/CrumpBit/CrumpBitParameters.c` | 456–475 | **add** `crumpBit_formatDelayTime()` (Q10 as revised in §8.8: a stop's name, else the row number) |
| | 609–637 | **modify** `crumpBit_uiFormatValue3()` `rte`: `dly` shows `crumpBit_formatDelayTime()` of the clamped row (the Sync division label is gone) |
| | 656–664 | **modify** `snc` shows `---` in `dly` (as in `pit`) |
| | (removed) | `#include "StepScale.h"` |
| `Core/Sequencer/StepScale.c` | 20–23 | **modify** the affiliates comment (`crumpBit_divisionFor()` is gone; the CrumpBit curves pin its 14 stops) |

**Host tests and gates**

| File | Change |
|---|---|
| `tools/dsp_test/modules/crumpbit.c` | header: CB1, CB4, CB6 docs. **Remove** `ref_divisionFor()`, `ref_rateLimit()`, the reference's `sync` field. **Add** `ref_delayRow()` (the §3.5.2 law in double precision), `ref_delayLimit()` (linear scan, stops at the first `t_max` row), `ref_shareMax()`. **CB1 replaced** (§8.5). CB4: the reference target is `min(crumpBit_delaySamples(rate, bpm), len − 2)` at CB4's random tempos, `snc` written and ignored; the Rate clamp is checked against `ref_delayLimit()`. CB6: the `dly` `effective_max` against the scan at 120 BPM for every share in 32-byte steps (127 for the whole arena); the rate-table checks removed; `ref_subtypeLimit()` `dly` case is the scan. CB9: the `dly` map's lane 6 `EFFECT_LANE_NONE` |
| `tools/dsp_test/Makefile` | break 1 replaced: the top step ratio with one `sqrtf` (rows 125..127 all `t_max`) → CB1. **Add** break 22: the limit search on `<=` (counts the `t_max` plateau) → CB1. `crumpbit_elf_check`: `crumpBit_divisionFor --straight --max 117` → `elf_blockpath.py crumpBit_delayLimit --max 152`; `crumpBit_runDelay --max 111` → **109** (measured). Comments: 22 breaks |
| `tools/dsp_test/crumpbit_cpu.py` | per sub-type limit search: `dly` adds `crumpBit_delayLimit()`'s per-block path (it fails if that function ever gains a loop); the others keep `crumpBit_tableLimit()` (straight count); the Sync term is gone (`dly`'s target is inlined in its engine path) |

### 8.3 The `dly` curve as built

Rows 4..123 follow tempo (ticks × 27,567.5 / BPM samples); the stops are
exact at rows 4 `/64`, 14 `32t`, 28 `/32`, 38 `16t`, 42 `d32`, 52 `/16`, 61
`8Tr`, 66 `d16`, 75 `/8`, 85 `4Tr`, 89 `d/8`, 99 `/4`, 113 `d/4`, 123 `/2`.
The end rows in samples (label):

| BPM | rows 0..3 | `/64` (row 4) | `/2` (row 123) | rows 124..127 | knob stops at (whole arena) |
|---|---|---|---|---|---|
| 300 | 2, 8, 33, 135 | 551 | 17,643 | 28,879, 47,271, 77,376, 126,654 | 127 |
| 120 | 2, 10, 53, 269 | 1,378 | 44,108 | 57,417, 74,743, 97,296, 126,654 | 127 |
| 60 | 2, 12, 74, 452 | 2,757 | 88,216 | 96,564, 105,702, 115,705, 126,654 | 127 |
| 42 | 2, 13, 89, 591 | 3,938 | 126,023 | 126,180 … 126,654 | 127 |
| 40 | 2, 13, 91, 613 | 4,135 | `t_max` | `t_max` | **122** (D4) |
| 30 | 2, 14, 105, 761 | 5,514 | `t_max` | `t_max` | **112** (D4) |

Labels (§8.8): a stop shows its name; every other row, the end rows
included, shows its row number (`  0` … `  3`, `124` … `127`). The times
above are for reference only, not shown. With a smaller share the knob
stops lower (the same search, against `len − 2`).

### 8.4 Measured (repository build)

| | Before (pre-P6) | After |
|---|---:|---:|
| `dly` per frame | 160.0 (loop 111; per block engine 242 incl. the Sync search 117, manager 86) | **157.6** (loop **109**; engine 140 incl. the inlined curve, manager 175 = `effectiveMax` 23 + `crumpBit_delayLimit` 152) |
| `cmb` / `pit` / `grn` / `rvb` per frame | 153.8 / 154.1 / 166.5 / 163.9 | unchanged |
| Worst (`crumpbit_cpu_check`) | 166.5 | **166.5** ≤ 170 |
| `.text` | 710,456 | **712,688** (+2,232 B: the curve-limit search 864 (unrolled), `crumpBit_delaySamples()` 256, the label table 420, the delay-time formatter and the label helpers; less the 512 B rate table, the 120 B row-name table and the Sync search) |
| Flash used (headroom) | 710,876 (42,788) | **713,108 (40,556)** |
| `.data` / `.bss` / ITCM / DTCM statics / FXBUF | 420 / 413,284 / 15,280 / 4,396 / 126,656 B at `0x20001140` | **unchanged** (no RAM change) |
| Warnings (clean build) | 20 | 20, the same set |
| `mixer_calcNextSampleBlock` | — | same instruction sequence, address-normalised disassembly: only 3 alignment `nop`s, one branch encoding (`b.w` → `b.n`) and one load offset (a relocated global) differ; no frame-loop change. (`elf_looppath.py` on this function did not finish in 17 min this time, so the comparison is by disassembly.) |
| Image | — | `build/LXRV2_lxr02.img` written OK |

### 8.5 Gates

- `make all` (repository root, clean): exit 0; the same 20 warnings.
- `crumpbit_tables`: OK (C1 is the new delay-ticks check).
- `crumpbit`: **0 failures**; all **22 breaks caught**:
  - break 1 (one `sqrtf`) fails CB1 from 42 BPM up (the top rows leave
    the law);
  - break 22 (plateau counted) fails CB1's whole-arena limit below 42 BPM;
  - break 5 (the table search without its eighth probe) still fails CB6
    (on the `cmb` limit now).
- CB1 now checks:
  - the tick table: 6 .. 192, strictly increasing, the 14 stops exact at
    `crumpBit_delayStopCc` and each a StepScale division;
  - at every BPM 0..1000, every row: within 4·10⁻⁶ of the double-precision
    law; the core rows bit-exact (ticks × samples per tick); every row
    below `t_max` a new delay, then only `t_max`; row 0 = 2.0, row 127 =
    `t_max`; `t_max` first at row 127 from 42 BPM up;
  - `crumpBit_subtypeRateLimit(dly)` equals the scan for shares in 4 KiB
    steps and the whole arena (= the first `t_max` row), and for 200,000
    random tempos and shares at row lengths ±1 byte.
- `crumpbit_elf_check`: exit 0 (`runDelay` 109, `delayLimit` 152 per
  block, the rest unchanged).
- `crumpbit_cpu_check`: exit 0, worst 166.5.

### 8.6 Hardware checks

§5 P6-1 … P6-8 stand. Added by D4 and the curve:

| # | Check | Expected |
|---|---|---|
| P6-9 | `dly` at 40 BPM, whole arena: turn `tim` to the top | it stops at `122` (`t_max`); one step down is `121` |
| P6-10 | `dly` at 120 BPM: rows 0..4 and 123..127 | `0 1 2 3 /64` and `/2 124 125 126 127`; row 0 is a 2-sample comb |
| P6-11 | Boot | no self-check code 11 (the registry lanes equal the `dly` map) |

### 8.7 Open items

- **Reference docs not updated yet:**
  - `EFFECTS_BUS_REFERENCE.md` §4.2a (the `labels` hook, the `dly` lanes)
    and §8.6 (rows, page orders, names, the `dly` Rate);
  - `EFFECTS_MIXER_DSP_REFERENCE.md` §4.4 (the `dly` target law) and §6.2a
    (`dly` CPU 157.6);
  - `DSP_TEST.md` (CB1, breaks 1 and 22);
  - the S091 P5 §11.6/§12.6 lists.
- **Existing Scenes:** a stored `dly` `rte` keeps its number but now plays
  that row of the new curve. A stored `snc` in a `dly` Scene is kept and
  ignored. A `dly` FX lane 6 recorded before P6 is no longer played (the
  lane is empty). No backward compatibility (user rule).

### 8.8 Revision (user, 2026-11-06): the end rows show their number

**Request:** remove the time labels on `dly` `tim` rows 0..3 and 124..127.
They show `0`, `1`, `2`, `3` and `124`, `125`, `126`, `127`, like every
other non-stop row.

| File | Lines | Change |
|---|---|---|
| `Core/DSP/Effects/CrumpBit/CrumpBitParameters.c` | 456–475 | **modify** `crumpBit_formatDelayTime()`: a stop row shows its name (`/64` … `/2`), every other row `numtostrpu(row)`. **Removed:** the ms/s time branch (and its `crumpBit_delaySamples()`/`seq_getBpm()` call per paint) and `crumpBit_format999()` |
| | 523, 552, 592 | **modify** the comments (the `uiFormatValue3` doc, its affiliates, the `dly` case) |
| `S091_P6_PARAM_CLEANUP.md` | §7.2 Q10, §8.2, §8.3, §8.6 P6-9/P6-10, §8.7 | time labels removed; the 42–44 BPM `2.9` item dropped (no longer applies) |

**Measured:** `make all` exit 0, no new warnings in the recompiled files;
Flash 713,108 → **712,460 B** (−648 B, headroom 41,204 B); RAM unchanged;
`build/LXRV2_lxr02.img` written OK. The DSP, the curve and the knob limit
are unchanged, so the host laws, the ELF gate and the CPU gate (worst
166.5) are unaffected. `CrumpBitParameters.c` is not compiled on the host,
so the label is a hardware check (P6-10).

### 8.9 Follow-up (user, 2026-11-06): `grn` below `/64`, and the tap fade

#### 8.9.1 Requests and decisions

| # | Request | Decision |
|---|---|---|
| G-R1 | `grn`'s delay time (`tim`) should go down to the minimum possible, below `/64` | as `dly` (§3.5.2): **4 rows below `/64`**, row 0 the 2-sample minimum, rows 1..3 the same geometric bridge to `/64` at the current tempo; the 16 stops move up onto rows 4..127 |
| G-R2 | smooth the clicks when `tim` changes | **a one-block fade of the delay tap** (the option that fits the 170 cap; a crossfade, about +7 per frame, or a tape glide, about +10, do not) |

#### 8.9.2 What was built

**Delay time:**
- The `grn` curve is rows 0..3 (new) and 4..127 (the S091 P4 six-octave
  tick curve, `/64` 6 ticks to `1br` 384 ticks, re-spread from 128 rows to
  124).
- Rows 0..3 use the `dly` law through one shared helper,
  `crumpBit_lowRowSamples(q, k)` = 2 · q^k with q = (t_/64 / 2)^¼ (two
  `sqrtf`), so both sub-types' bottoms are the same.
- `crumpBit_grainTime` keeps 128 entries: rows 0..3 hold 0.0 (they depend
  on tempo, so the engine computes them per block). Their 0.0 keeps them
  inside the knob-limit search, which is exact because they always fit:
  row 3 is at most 9,754 samples (at 1 BPM), and the smallest share (every
  voice unit claimed) gives a 36,830-frame line. A `_Static_assert` holds
  that.
- At very slow tempos `/64` itself can exceed the line (1 BPM: 165,412
  samples); the knob then stops at row 3, by the existing search.
- Position and Density pick stops by stop number, so they are unchanged.

| Stop | `/64` | `32t` | `/32` | `16t` | `d32` | `/16` | `8Tr` | `d16` | `/8` | `4Tr` | `d/8` | `/4` | `d/4` | `/2` | `d/2` | `1br` |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Row before | 0 | 9 | 21 | 30 | 34 | 42 | 51 | 55 | 64 | 72 | 76 | 85 | 97 | 106 | 118 | 127 |
| **Row now** | **4** | **13** | **24** | **33** | **36** | **45** | **54** | **57** | **66** | **74** | **77** | **86** | **98** | 106 | 118 | 127 |

Rows 0..3 in frames (labels `0 1 2 3`): 300 BPM 2, 8, 33, 135 (`/64`
551); 120 BPM 2, 10, 52, 269 (`/64` 1,378); 30 BPM 2, 14, 105, 760
(`/64` 5,513).

**The tap fade (one block, about 0.73 ms each way):**
- The tap state is `tap_d` (the D being read) and `tap_on` (its gain at
  the block start, 1 or 0).
- Per block:
  - a silent tap (`tap_on` 0) takes the asked-for D at once;
  - a sounding tap keeps its D;
  - the tap ends the block on (gain 1) only if it reads the asked-for D
    and that D is recorded;
  - the gain ramps linearly over the block from its start value to its
    end value.
- So a change fades the tap out over one block at the old D, switches
  while silent, and fades it in over the next block at the new D.
- Changes on consecutive blocks skip the in-between delays. Turning fast
  gives a series of short dips instead of clicks.
- The re-record into the delay line takes the faded tap, so the echo's
  feedback does not recirculate a step.
- The gain replaces the former gate mask: an unrecorded D stays at 0, and
  a D that becomes unrecorded (an unfreeze restart) is cut at once, as
  before.
- **Line guards:** a reseat sets `tap_on` 0 (the tap restarts silent at
  the new line's D), and each block holds `tap_d` inside the line. Either
  alone keeps the read index in range.

**RAM:** none. `tap_d`/`tap_on` (+1 spare byte) take the free 4 B
`reserved` word (G4's former `dc_y`): the grain state stays 44 B and the
runtime 100 B (both `_Static_assert`ed).

#### 8.9.3 Changes by file

| File | Lines | Change |
|---|---|---|
| `tools/plots/gen_crumpbit_tables.py` | 59–73, 107–110 | docs: the curve on rows 4..127, rows 0..3 0.0; check C6 |
| | 150–154 | **add** `GRAIN_LOW_ROWS = 4` |
| | 236–259 | **modify** `grain_stop_cc()` (stops over rows 4..127: 4 + round(123 · log position)) and `grain_time_table()` (rows 0..3 0.0) |
| | 382–387 | **modify** check C6: rows 0..3 0.0, strictly increasing over 4..127, 6 at row 4, 384 at 127, stops at rows 4 and 127 |
| `Core/DSP/Effects/CrumpBit/CrumpBitTables.c` | (generated) | `crumpBit_grainTime` rows 0..3 0.0 and the curve re-spread; `crumpBit_grainStopCc` as above |
| `Core/DSP/Effects/CrumpBit/CrumpBitTables.h` | 50–57, 86 | **modify** the `crumpBit_grainTime` doc; **add** `CRUMPBIT_GRAIN_LOW_ROWS` 4 |
| `Core/DSP/Effects/CrumpBit/CrumpBitEffect.h` | 119–127, 151–152, 160–163 | **modify** the grain state: `reserved` → `uint16_t tap_d; uint8_t tap_on; uint8_t tap_spare;`, with their doc; the tap doc ("reads exactly tap_d", no longer "has no state") |
| `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c` | 219–233 | **add** `_Static_assert`s: rows 0..3 fit the smallest share's line; `CRUMPBIT_GRAIN_LOW_ROWS == CRUMPBIT_DELAY_LOW_ROWS` |
| | 367–379, 390 | **add** `crumpBit_lowRowSamples()`; `crumpBit_delayRow()` uses it (`dly` unchanged, the same expression) |
| | 2023, 2042 | **modify** the run state: `float tap_g` (the gain ramp); block constants: `float tap_inc` in place of `int32_t gate` |
| | 2058–2065, 2129–2140 | **modify** `crumpBit_grainSpan()`: tap = code × `tap_g`, `tap_g += tap_inc` (in place of the gate mask); its doc |
| | 2160–2168 | **modify** the `crumpBit_runGrain()` doc (rows 0..3, the fade) |
| | 2268–2310 | **modify** `crumpBit_runGrain()` delay time: rows 0..3 by `crumpBit_lowRowSamples()`, else ticks × samples per tick, clamped to [2, N − 2]; then the fade (the `tap_d` update, the end gain `on`, the start gain, `tap_inc = (on − start) / frames`, `tap_on = on`) |
| | 2813–2814 | **modify** `crumpBit_seatActive()` (`grn`): a reseat sets `tap_on` 0 |
| `Core/DSP/Effects/CrumpBit/CrumpBitParameters.c` | 439–443 | **modify** the `crumpBit_formatGrainTime()` doc (stops on rows 4..127; rows 0..3 show their numbers, as in `dly`). No code change: a non-stop row already shows its number |
| `tools/dsp_test/modules/crumpbit.c` | 150–160 | header: CB23 |
| | 1171–1181, 1205 | **modify** CB6: `/64` at row 4, `1br` at 127, rows 0..3 0.0, ascending from row 4 |
| | 2071–2084, 2620–2642 | **modify** CB14 (e) copy identity and CB18: `/64` is row 4 (`rte` 4, `grainTime[4]`) |
| | 3131–3312, 3319 | **add** law CB23 (§8.9.5) and its call |
| `tools/dsp_test/Makefile` | header, break docs, 601–631 | **add** breaks 23–25; `crumpbit_elf_check` `runGrain --max 116` → **114** (measured); 25 breaks |

#### 8.9.4 Measured (repository build)

| | Before (§8.8) | After |
|---|---:|---:|
| `grn` frame loop | 116 | **114** (the gain multiply replaced the gate's AND and the mask; the compiler also scheduled the loop 2 shorter) |
| `grn` per block (engine) | 815 | **840** (+25: the low-row branch with two `sqrtf`, the fade's compares and ramp step) |
| `grn` per frame | 166.5 | **165.3** |
| `dly` / `cmb` / `pit` / `rvb` per frame | 157.6 / 153.8 / 154.1 / 163.9 | unchanged |
| Worst (`crumpbit_cpu_check`) | 166.5 (`grn`) | **165.3** (`grn`) ≤ 170 |
| Flash used (headroom) | 712,460 (41,204) | **712,684 (40,980)**: +224 B |
| `.data` / `.bss` / ITCM / DTCM statics; runtime / union | 420 / 413,284 / 15,280 / 4,396; 100 / 44 B | **unchanged** |
| Warnings (recompiled files) | — | none new |
| Image | — | `build/LXRV2_lxr02.img` written OK |

#### 8.9.5 Gates

- `crumpbit_tables`: OK (C6 updated).
- `crumpbit`: **0 failures**; all **25 breaks caught**. The new breaks:
  - break 23, the fade removed (`tap_inc` 0, so the gain holds its
    block-start value): CB23 (b), 91,623 frames off;
  - break 24, rows 0..3 all at 2 samples: CB23 (a);
  - break 25, both line guards removed: CB23 (c), D 44,108 past a
    20,000-frame line.
- **CB23** (new):
  - (a) rows 0..4 at 1, 30, 120 and 300 BPM: row 0 is 2 frames, rows 1..3
    the `dly` bottom law within ±1 frame, row 4 `/64`, strictly rising; at
    1 BPM `/64` exceeds the line and the knob stops at row 3;
  - (b) the copy identity (`fbk` 0) with 2,046 random `tim` changes
    (rows 0..3 and 4..106, many on consecutive blocks; 2,982 ramps): every
    frame equals rec[n−2] + g · rec[n−2−D] for an independent model of the
    fade rule (worst error 1.2·10⁻⁷, tolerance 2·10⁻⁶), and the firmware's
    `tap_d`/`tap_on` match the model after every block;
  - (c) a reseat to a 40,000-byte share with the tap sounding at `/2`
    (44,108 frames): D stays inside the new 20,000-frame line.
- The updated laws pass: CB6 (the grain table), CB14 (e) (the copy identity
  at `/64`, now row 4), CB18 (the echo decays, at row 4).
- `crumpbit_elf_check`: exit 0 (`runGrain` 114).
- `crumpbit_cpu_check`: exit 0, worst 165.3.

#### 8.9.6 Hardware checks

| # | Check | Expected |
|---|---|---|
| P6-12 | `grn` `tim` at the bottom (120 BPM) | `0 1 2 3 /64 …`; rows 0..3 are short combs (2 samples … 6 ms), `/64` (31 ms) at row 4 |
| P6-13 | `grn` `tim` stops | `/64` 4, `/2` 106, `1br` 127 as listed in §8.9.2; Position and Density stops unchanged |
| P6-14 | `grn` `tim` turned slowly and fast with the echo sounding (`fbk` up) | no clicks: each step is a short dip in the echo (about 1.5 ms); fast turning flutters but does not click |
| P6-15 | Voice units claimed or released while the `grn` echo sounds | no fault; the echo restarts silent on the new line |

#### 8.9.7 Open items

- **Existing Scenes:** a stored `grn` `tim` now plays a shorter delay
  than before: the curve moved up by four rows, so a stored row lands
  about four rows lower on it (stored `0`, formerly `/64`, is now the
  2-sample minimum; `106` and up are unchanged). No backward
  compatibility (user rule).
- **Found, not changed:** the tap's "recorded" test needs
  `valid ≥ D + frames`, and `valid` stops at N. A D within 32 frames of
  the line's end (N − 32 < D ≤ N − 2) is therefore never on: the echo is
  silent for the rows whose D clamps there. Today that is only a row
  whose time lands in the last 0.7 ms of the line (the knob stops at the
  last row that fits N − 2). It was there before P6; say if it should be
  fixed (e.g. test `valid ≥ min(D + frames, N)`).
- The reference docs (§8.7) also need the `grn` curve's rows 0..3, the
  fade, and `grn` CPU 165.3.

