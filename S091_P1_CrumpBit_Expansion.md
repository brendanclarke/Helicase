# S091 P1 — CrumpBit Expansion: Initial Plan and CPU Assessment

- **Date:** 2026-11-03. **Branch:** `dev-ph7-voice_expansion`, HEAD `93f35799`.
- **Status:** assessment and direction plan; decisions taken 2026-11-03
  (§0A). No firmware code is changed. The prototype loops behind the §2–§3
  numbers are scratch files outside the repository (§9 says how to
  reproduce them).
- **First implementation:** the CPU reductions only (§2), with nothing
  added. Schedule: `S091_P1_Reductions_Implementation.md`. The features
  (§3) follow after the reductions pass on hardware.
- **Request (user):** add to CrumpBit (`cbt`)
  1. a delay that uses the whole buffer, elastic against a possible 50 ms or
     100 ms voice reservation;
  2. feedback above unity and self-oscillation;
  3. a tone shape / band-pass filter in the delay line;
  4. a stereo multi-mode filter as the last element of the Effect.
- **Constraint (binding):** `S089_P2_WAVEFOLDER_CHAIN.md` §8. No per-sample
  DSP cost may rise, and that includes the Effects. A new feature must be paid
  for by an equal or larger *measured* saving. The worst-case Scene reads
  98–100 % on the `cpu` widget with CrumpBit (user, 2026-11-03).
- **Supersedes:** the CPU sections of
  `knowledge_files/drafts/CRUMPBIT_EXPANSION_DONE.md` (§2, §4, §5). Its design
  notes (§3.1 onset behaviour, §3.2 tone placement, §6 page) still apply
  except where this plan says otherwise.

---

## 0A. Decisions taken (user, 2026-11-03)

The user answered the decision table of the draft
(`CRUMPBIT_EXPANSION_DONE.md` §7, D1–D8). This plan's own §7 numbers differ,
so each answer is recorded here with the topic it settles. Where an answer
replaces something in §1–§8 below, this section wins.

| Draft # | Topic | Decision | Consequence for this plan |
|---|---|---|---|
| D1 | Measuring against StereoFilter (`flt`) | **No `flt` tests.** `flt` was built only to test the FX bus and will probably be removed. | §1.2 is background only. Step 0 measures `off` and `cbt` only. This plan's D1 (`flt` as the Effect ceiling) is closed: the reductions pay for the features (§2). |
| D2 | Longer delay; tone stage | **Delay:** keep the same tempo-synced divisions; re-scale the free Rate curve to the longer time; as voice reservations shrink the share, clamp the settable values and clamp the DSP target. **Tone stage:** start with the one-pole and judge it by ear. | §3.1: the Rate curve's top becomes the full-arena time. Sync uses the same StepScale divisions, tested against the live maximum. A reservation clamps both the page/pot range and the DSP target. See note N1. |
| D3 | Half-rate tape mode (L3) | **Rejected.** Keep the native sample rate. A lower rate would not be correct for this Effect, and the plan must not argue from a supposed "theme". | L3 is dropped. Nothing in this plan changes the sample rate. |
| D4 | Output filter | **F1:** a stereo resonant TPT state-variable filter as the last element. | §3.4 as written. L/R spread is not requested. |
| D5 | Tone control | **On the feedback return.** One bipolar `tone` control centred on 0: negative values cut more and more high content, positive values cut more and more low content, and 0 is flat. | §3.3 changes: one-pole tilt on the return (`write = input + fb·tone(wet)`). The first echo is unfiltered; repeat *n* is shaped *n* − 1 times. At 0 the tilt is flat, so it is **not** a DC blocker: a separate in-loop DC blocker is still required for feedback above unity (§3.2). Estimate: tilt ≈ +4–5 per frame, DC blocker ≈ +2–3, to be measured at the feature step. |
| D6 | Feedback curve | **Maximum 1.2.** Saved Scenes are not preserved and there is no migration. | §3.2: piecewise curve, unity at 100 (the draft's default, which the user did not contest; confirm at the feature step), 1.2 at 127. |
| D7 | Limiter | **Soft knee.** | §3.2: soft knee on the loop write (≈ +4–5, to be measured). |
| D8 | RAM | **+8 B approved** (union 76 → 84 B, owner `CrumpBitRuntime` in `effects_runtime_t`, DTCM). | §5: the trims become optional. The link map must confirm that +8 B stays inside the 12 B pad before `_sfxbuf`, so the arena does not move. The reduction step needs 0 B. |

**Still open (this plan's numbering):**
- D8: keep the delay tail on a share change.
- D9: a DEV render-cycle readout.
- D10: an `expf` table.

**N1 — the Rate clamp runs the other way from `effective_max`.** A higher
`rte` means a shorter delay, so a shrinking share removes the *lowest*
values. The existing hook `effective_max(index, share)` can only clamp from
the top. The feature step must choose one of two fixes:
- reverse the Rate direction so that a higher value means a longer time
  (old Scenes need not be kept, D6);
- add a minimum to the hook.

The Sync label and the Rate cell must show the clamped value.

---

## 0. Summary

- **The draft under-counted CrumpBit's cost by about 70 %.** Its per-sample
  figure is right: the linked loop is **111** instructions per stereo frame.
  But it missed the per-block Sync division walk. That walk runs all **128**
  StepScale positions every block, at 12–19 instructions each (S078 grew it
  from 14 positions; audit item 27 says "not re-measured"). That is up to
  **2,430 instructions per block, ≈ 76 per frame**. In the worst case,
  CrumpBit today costs **≈ 191 per frame**, not 111.
- **The draft's headroom picture no longer exists.** It assumed 94–95 % and
  "4 % left". Today the reading is 98–100 %, the §8 no-increase rule applies,
  and the widget is saturated, so it cannot show how much margin remains (§1.3).
- **The features can be paid for inside CrumpBit.** Bit-identical (S0)
  changes, measured on the real source, are:
  - a binary-search Sync walk (−68 per frame in the worst case);
  - a saturating 8-bit ADC, a hoisted frame count, and AC coupling on code
    differences (−12 per frame).

  Together they take CrumpBit from ≈ 191 to ≈ 111 per frame.
- **Measured feature costs** (added to the 99-instruction S0 loop):

  | Feature | Cost per frame |
  |---|---:|
  | Elastic delay | 0 |
  | Feedback curve above unity | 0 |
  | One-pole band-pass in the loop | +6 |
  | Resonant SVF band-pass in the loop (instead) | +15 (+3 for its DC block) |
  | Stereo multi-mode SVF output | +26 |

- **Recommended package (A + F): ≈ 151 per frame worst case, about 40 below
  today's CrumpBit.** It is still below StereoFilter (≈ 212–252). No
  Cymbal/HiHat or other voice work is needed. §4 keeps those as fallbacks.
- **RAM: 0 B.** The runtime grows from 56 B to about 68–72 B, which fits the
  existing 76 B union (§5).
- **First step: hardware baseline, no code.** Measure the worst-case Scene
  with `off`, `flt` and `cbt`. Then apply the S0 savings alone (step 1). That
  step should lower the `cbt` reading before any feature lands.

---

## 1. What CrumpBit costs today (measured)

### 1.1 Linked ELF (`build/lxr02.elf`, 2026-11-01, matches HEAD)

| Part | Instructions | Per frame (÷ 32) | Notes |
|---|---:|---:|---|
| Per-sample loop, loop filled (`valid` path) | 111 per frame | **111** | 21 `vmov`, 10 `vcvt`, 0 VDIV. Same as the draft. |
| Per-sample loop while the loop is still filling | 101 per frame | — | The `valid` mute compiled to a branch. The worst case is the filled path. |
| Sync walk `crumpBit_divisionFor()` (inlined) | 128 × 12–19 = 1,536–2,432 per block | **48–76** | Paths: length > max 12, ≤ target 17, > target 19. At ≥ 75 BPM every division fits 1.6 s, so the 17/19 paths are the normal case. |
| Other per-block work | `expf` + 2 VDIV ≈ 90–130 per block | ≈ 3–4 | `crumpBit_rateSamples()`, `per_tick`, `per_frame` |
| **Total, worst case** | | **≈ 191** | about 3.9 % of the 4,897-cycle frame |

The walk's figures are instructions. In cycles it costs more: each step has
two `vcmpe`/`vmrs` pairs (the FPSCR transfer stalls) and data-dependent
branches.

### 1.2 Against StereoFilter (`flt`)

`SVF_calcBlockZDFFloat` linked loops are 80–93 instructions per channel with
2 VDIV (one type has 3). By the S089 rule (instructions + 13 per VDIV), both
channels cost **≈ 212–252 per frame**.

- **The draft compared 111 against 215–290** and concluded that `flt` sets
  the Effect ceiling by 100–180 cycles. The real gap is ≈ 20–60 today. It
  grows to ≈ 60–100 after the §2 savings and the recommended package.
- **This plan does not rely on that gap** (§8 covers the Effects). Whether
  `flt` may count as the Effect ceiling is decision D1.

### 1.3 Why the `cpu` widget cannot settle this

- **What it measures:** `audioCodec_getQueueFreePercent()` reports the share
  of time the 2-slot ready queue was *not full*. After each DMA consumption,
  the queue stays not-full for the main-loop delay before the render starts
  **plus** the render time.
- **What follows:** the reading is DSP load plus foreground latency, and it
  saturates at 100 %. Underruns need the queue to *empty*, which is about one
  more period away.
- **So at 98–100 %** it cannot rank `flt` against `cbt`, and it cannot show
  how far a Scene is from underrunning.
- **Measurement:** the step-0 baseline uses the widget plus the underrun
  count (`audioCodec_underrunCount`; the `UNR` LCD snippet in `main.c` is
  ready to enable in a DEV build).
- **Option D9:** a DEV-only render-cycle readout. DWT `CYCCNT` already runs.
  It would show the max and mean cycles of the three-block render per second,
  in about 8–12 B of DEV-only RAM. Note: S073 F-5 declined a per-stage
  profiler, and this would be a single figure, not that.

---

## 2. Savings inside CrumpBit (S0, bit-identical)

| # | Change | Saving per frame (worst case) | Proof |
|---|---|---:|---|
| S0-1 | **Binary-search Sync walk.** StepScale ticks are strictly ascending (`stepScale_q8Table`), so "longest ≤ target" and "shortest > target" among the fitting prefix are two upper-bound searches: 2 × 7 fixed steps instead of 128. It uses the same float expression for each length, so the comparisons and the chosen index are identical. Constant cost (always 14 steps). The UI label `crumpBit_syncDivision()` shares it. | **−68** (76 → ≈ 6–8) | host: all 128 rates × BPM 1..999 × both max lengths, index-identical |
| S0-2 | **Saturating ADC:** `__usat((int32_t)(x·128 + 128.5), 8)` replaces two clamps, the convert and `code − (code >> 8)`. Inputs below −1 truncate to ≤ 0 → 0; ≥ +1 gives ≥ 256 → 255: the same codes. ×3 per frame (L, R, write). | −10 together with S0-3 (111 → 101, measured) | host: exhaustive over float ranges near every code edge |
| S0-3 | **Hoist `io->frames`.** A float store may alias a `uint8_t` (character type), so today the loop reloads `frames` and zero-extends `n` every sample. | (in S0-2) | codegen only |
| S0-4 | **AC coupling on code differences:** `h = (c − c_prev)/128 + R·h`. `(c − 128)/128 − (c_prev − 128)/128` is exact in float, so this is the same FMA input. The `−128`, one `vmul` and one `vsub` per channel go; the stored state becomes the previous int code (−4 B). Uses a fixed-point `vcvt #7` (inline asm, 2 instructions). | −2 (101 → 99, measured) | host: identical output |

- **Optional, S1:** a 128-entry `expf` table for the Rate curve (512 B
  flash). It saves ≈ 50–100 cycles per block (2–3 per frame) at rounding
  level only. Not needed for the budget.
- **Not taken:** making the `valid` mute per-block (S3: the glide can move
  the delay faster than `valid` grows). Reordering the mix and pan ramps (S1,
  ≈ −1).

**Result: CrumpBit ≈ 111 per frame (99 loop + ≈ 8 walk + ≈ 4 per block),
about 80 below today, with an identical output.**

**Pre-verified in the linked image (2026-11-03):** R1–R4 were built from a
copy of HEAD (`S091_P1_Reductions_Implementation.md`).
- Per-sample loop 111 → **98**.
- The 128-step walk became a 117-instruction straight-line search.
- Mixer loops are unchanged.
- The host laws are bit-exact.

---

## 3. The four features

All costs below are measured on top of the 99-instruction S0 loop (§9). They
exclude per-block coefficient work, which is ≈ 1–2 per frame (table reads,
one `tan` lookup and one division per block).

### 3.1 Delay on the whole buffer, elastic (0 per sample)

- **Arena today:** 126,688 B from `0x20001120` (the references still say
  126,592 B from `0x20001180`; the S089 P2 layout shift moved it). At one
  byte per sample that is **2.872 s**.
- **Voice units: one unit is 4,416 B.** That is 50 ms for a 16-bit voice
  buffer, but **100 ms of CrumpBit delay** (8-bit). A 50 ms reservation costs
  100 ms of delay; a 100 ms reservation (2 units) costs 200 ms.

  | Units claimed | 0 | 1 | 2 | 4 | 6 | 12 (all) |
  |---|---:|---:|---:|---:|---:|---:|
  | Share (B) | 126,688 | 122,272 | 117,856 | 109,024 | 100,192 | 73,696 |
  | Max delay (s) | 2.872 | 2.772 | 2.672 | 2.472 | 2.271 | 1.671 |

- **Design (L1 + L4 of the draft):**
  - The loop length is the live share minus the 2-sample guard.
  - The Rate curve has a fixed top, `CRUMPBIT_DELAY_CURVE_MAX`, set at about
    the full arena. It is a constant, so a small future DTCM growth does not
    retune Scenes.
  - The target is clamped to `length − 2`, as today.
  - A clamp change glides, so it becomes a pitch bend, not a click.
- **Registry:** `buffer_pref_bytes` becomes the whole arena;
  `buffer_min_bytes` stays at the minimum share.
- **Sync:** candidate divisions are tested against the live maximum, not
  `CRUMPBIT_DELAY_MAX_SAMPLES`. The Effect-page label reads the same share
  through `fxbuf_effectShare()` (foreground). At 120 BPM `1br` (2.0 s)
  becomes available.
- **Keep the tail on a share change (recommended, D8):**
  - Units are taken from the top, so bytes `[0, write_pos)` are still the
    newest audio in order.
  - Re-seat with `valid = min(valid, write_pos)` when `write_pos` is inside
    the new length. Today the re-seat mutes the whole loop.
  - Otherwise fall back to today's full re-seat.
- **Rate curve shape (D7):** R1 keeps the exponential shape with the new top,
  so every saved `rte` value plays a different time; R2 uses two segments,
  giving the lower half of the knob to ≥ 0.25 s. There is no migration either
  way (standing rule).
- **Cost:** 0 per sample. The walk covers more candidates at low tempo, so
  it sits at its fixed worst case more often. After S0-1 that worst case is
  only ≈ 6–8.

### 3.2 Feedback above unity and self-oscillation (0 per sample)

- **Curve, per block (D4):** piecewise, 0..100 → 0..1.0 and
  100..127 → 1.0..1.5. Saved Scenes with a high `fbk` change.
- **DC inside the loop (mandatory):** at a loop gain above 1, any offset
  latches to a rail. With tone option A (§3.3) the high-pass is the in-loop
  DC blocker, so it costs nothing extra. Option B needs a separate one
  (+3) when its shape can reach low-pass.
- **Limiter (D5):**
  - **Hard clip (0):** the 8-bit ADC on the write already clips at ±1, so
    oscillation is bounded and `wet` stays within ±1.
  - **Soft knee (≈ +4–5, not prototyped):** smoother howl; Scenes below
    unity stay unchanged.
- **Behaviour:** as draft §3.1. The ½-LSB dead zone means quiet tails hang
  and loud material blooms. Rate during oscillation gives tape-speed sweeps.

### 3.3 Tone shape / band-pass in the delay line

Placed on the whole write `(½(hL + hR) + fb·wet)`, so every echo, the first
included, is shaped (D3). Placing it on the feedback return costs the same.

| Option | What | Measured cost |
|---|---|---:|
| **A — one-pole band pair** (recommended) | LP `lp += kl·(w − lp)` then HP `hs += kh·(lp − hs)`, output `lp − hs`. Two rows: `ton` (LP, ≈ 150 Hz–18 kHz) and `lct` (low cut, ≈ 20 Hz–2 kHz). Non-resonant, tape/BBD-style darkening and thinning. The HP is the in-loop DC blocker. | **+6** (99 → 105) |
| B — resonant SVF | TPT (Zavalishin/Simper) state-variable filter, shape morph LP → BP → HP with resonance. Ringing, dub-style repeats; resonance raises the in-band loop gain. | **+15** (99 → 114), +3 DC block |

### 3.4 Stereo multi-mode filter as the last element

- **F — TPT SVF per channel, after the dry/wet mix.** It filters the crushed
  dry signal and the delay together, at the last stage before the return.
  - Per-block coefficients: `g = tan(π·fc/fs)` from a table, one division
    per block. No per-sample division.
  - Modes LP / BP / HP / notch, from one `(n0, n1, n2)` output mix.
  - Stable under per-block LFO steps (1,378 Hz control rate, like the voice
    filters).
- **Cost: +26** (99 → 125). 16 B of state.
- **Bound the resonance (D6):** the linear SVF does not self-oscillate, but
  its peak gain is Q. Its output is unbounded until the return's ±255 clamp.
  Suggested Q ≤ 8.
- **Separate code:** this is its own `static inline` in `CrumpBitEffect.c`,
  not a third copy of the ResonantFilter twins. `flt`'s ZDF filter twice
  would be +160–206 and 4–6 VDIV, which does not fit.

### 3.5 Totals (worst case, per frame)

| Package | Loop | Per block | **Total** | vs today (≈ 191) |
|---|---:|---:|---:|---:|
| Today | 111 | ≈ 80 | **≈ 191** | — |
| S0 savings only (§2) | 99 | ≈ 12 | **≈ 111** | **−80** |
| **A + F + §3.1 + §3.2 (recommended)** | 137 | ≈ 14 | **≈ 151** | **−40** |
| A + F + soft knee | ≈ 142 (est.) | ≈ 14 | ≈ 156 | ≈ −35 |
| B + F (+ DC block) | 148 + 3 | ≈ 14 | ≈ 165 | ≈ −26 |
| Reference: `flt` | 2 × 80–93 + VDIV | — | ≈ 212–252 | — |

- **The recommended package lands about 40 per frame below today**, about
  0.8 % of the frame budget, and below `flt`.
- **B + F also fits**, with a thinner margin (prototype counts move ±2–3 with
  register allocation).
- **Cycles, not only instructions.**
  - The removed walk had `vmrs` stalls and branches, so its real saving is
    likely larger than counted.
  - The added filters are straight-line FMA chains: M7 dual issue helps, but
    the dependency latency can cost a little more than counted.
  - Hardware decides (§8).

---

## 4. Fallback savings outside CrumpBit (only if hardware disagrees)

Ranked by leverage per frame. A saving in shared voice code counts six times,
Cymbal/HiHat at most twice (the two-Advanced limit), and CrumpBit once.

| # | Candidate | Size per frame | Status / risk |
|---|---|---:|---|
| X1 | Integer S&H in `mixer_decimateBlock()` (Q31 phase) | −18 (6 × 3) plus six `vcmpe`/`vmrs` stalls | Built and measured in S089 P0 §8, then reverted by user preference. Sound class S3 (latch phase). Would need the user to reverse that call. |
| X2 | Voice ZDF filter (`SVF_calcBlockZDF`, ×6) | each instruction −6 | The two VDIVs per sample sit on one dependent chain, so batching them needs new algebra. The int16/float twin mandate applies. A deep dive of its own. |
| X3 | Cymbal/HiHat: fold the second modulator into an accumulating render (drops the saturating-add pass, ≈ 4–5 per sample) | ≈ −8 to −10 (×2 voices) | Needs add variants of every modulator loop in ITCM. Only 1,144 B are left after S089 P3, so it fits poorly. |
| X4 | The untyped Drum `fm` depth-ramp pass (10 per sample) | 0 for the ceiling | It sits below the typed worst case (≈ 81) and does not lower the worst-case Scene. |

**Recommendation:** none of these is needed for this plan. X1 is the
cheapest to bring back if one is needed.

---

## 5. RAM

The 76 B `effects_runtime_t` union is pinned by an assert. It is followed by
a 12 B alignment pad (`_edtcmz` `0x20001114`, `_sfxbuf` `0x20001120`).

| Layout | Bytes |
|---|---:|
| Today | 56 |
| Remove the four ramp-origin floats; recompute them from the previous raw rows (draft T1; S0, except the first block after `init`, which ramps from the computed origin instead of 0) | −16 |
| Previous `mix_raw`, `feedback_raw`, `pan_raw` | +3 |
| AC state as two int16 previous codes instead of two floats (S0-4) | −4 |
| Tone A state (`lp`, `hs`) | +8 |
| Output SVF state (`l1`, `l2`, `r1`, `r2`) | +16 |
| New rows: `ton`, `lct`, `cut`, `res`, `fmd` (A + F) | +5 |
| **Total (A + F)** | **68** (B + F: 69 → 72) |

- **Result:** the union stays 76 B and the arena does not move.
- **Changes needed:** the `sizeof(CrumpBitRuntime) == 56u` assert becomes
  the new figure, and `STORAGE_SRAM_MANIFEST.md` records it.
- **Coefficients:** recomputed every block from the raw rows, so they need
  no storage.
- **Flash:** about +1.5–3 KB (two 512 B coefficient tables, the new code).
  Flash headroom is ≈ 62 KB (text 690,120 B in the 736 KB application
  window).

---

## 6. Parameters and the Effect page

- **New rows, append only** (indices are permanent; file keys `crump_*`):
  - A + F: `ton` 11, `lct` 12, `cut` 13, `res` 14, `fmd` 15 (filter mode
    enum).
  - B + F: `bpf`, `bpq`, `shp`, `cut`, `res`, `fmd`.
  - Do not use `flt` as a label: it is StereoFilter's type token.
- **Page:** SELECT 2 grows from 3 to 4 screens. SELECT 3..8 stay at zero
  screens (bit toggles).
  - A + F: screen 3 `snc dpn ton lct`; screen 4 `cut res fmd –`.
  - B + F: screen 3 `snc dpn bpf bpq`; screen 4 `shp cut res fmd`.
- **Lanes:** 6 are free, enough for either set.
- **Modulation:**
  - Continuous rows: Morph, LFO and Pattern.
  - `fmd`: Pattern and sequenceable, not Morph.
  - `write_param` keeps storing raw bytes, so an LFO costs no more per block.

---

## 7. Decisions for the user

**Resolved 2026-11-03: see §0A.** The user answered the draft's table, and
§0A maps those answers onto the rows below. Rows D8 (tail), D9 and D10 are
still open. The table is kept as the record of what was asked.

| # | Question | Suggested default |
|---|---|---|
| D1 | May `flt` count as the Effect ceiling (`cbt` may grow up to `flt` without an offset)? | **No.** Pay inside CrumpBit (§2); keep D1 in reserve |
| D2 | Tone stage: A (one-pole band pair) or B (resonant SVF) | **A** |
| D3 | Tone placement: whole write, or feedback return only | Whole write |
| D4 | Feedback curve: unity at 100, max 1.5; accept that saved high-`fbk` Scenes change | Yes |
| D5 | Limiter: ADC hard clip or soft knee (+4–5) | Hard clip first, then listen |
| D6 | Output filter: 4-mode enum or continuous morph; maximum Q | Enum LP/BP/HP/notch, Q ≤ 8 |
| D7 | Delay: elastic (§3.1), or a fixed reservation of 1–2 units (−100/−200 ms); Rate curve R1 or R2 | Elastic, R2 |
| D8 | Keep the tail on a share change (`valid = min(valid, wp)`) | Yes |
| D9 | DEV-only render-cycle readout (DWT), given the widget saturates | Ask; not required |
| D10 | `expf` table (S1, ≈ −3 per frame) | Only if hardware needs it |

---

## 8. Order and verification

0. **Hardware baseline (no code):** the worst-case Scene with Effect `off`
   and `cbt` (every send open). No `flt` (§0A D1). Record the `cpu` widget
   and the underrun count over a fixed time.
1. **S0 savings only (§2).**
   - Host: bit-identical output against today's `crumpBit_process()` over
     random input, parameter jumps, share changes, mono `io->r == NULL`, and
     every rate × BPM for the walk.
   - ARM: loop 111 → ≤ 99; walk ≤ 8 per frame.
   - Hardware: `cbt` reads at or below step 0 and sounds identical.
   - This step lowers the worst case on its own.
2. **Elastic delay and feedback curve (§3.1, §3.2):** 0 per sample. Use
   `DEV_FXBUF_FORCE_VOICE_UNITS` to test clamps and tail-keeping as the share
   changes.
3. **Tone stage (§3.3).**
4. **Output filter (§3.4).**

For steps 2–4:

- **Host** (`DSP_TEST.md` §6.3, a CrumpBit runner):
  - output bounded at `fb` 1.5 with silence, DC and full-scale input;
  - no DC lock-up over 60 s of oscillation;
  - no NaN;
  - output-filter peak within the D6 bound.
- **ARM:** per-sample loop and per-block counts at or below §3.5.
  `elf_hotloops.py` shows no mixer loop grew (CrumpBit runs through the ops
  pointer, not inlined).
- **Build:** `make all`, `arm-none-eabi-size`, `tools/link_budget.py`
  (union 76 B, `_sfxbuf` unchanged).
- **Hardware:**
  - The worst-case Scene with `cbt` at its heaviest settings and LFOs on
    cutoff, tone and feedback.
  - Pass: the `cpu` widget reads at or below step 0, and there are no
    underruns.
  - Keep the monitor level low for self-oscillation.
- **Afterwards:**
  - `EFFECTS_MIXER_DSP_REFERENCE.md`: §4.4, the arena figures in §5.1, and
    §6.2a with the measured costs.
  - `CPU_USE_DSP_AUDIT.md`: a new item that also corrects item 27.

---

## 9. How the numbers were measured

- **Linked figures:** `build/lxr02.elf` (2026-11-01; the only later commit
  removes test scratch files), `objdump` of `crumpBit_process.lto_priv.0`.
  Every path from each loop head back to itself was walked, and the longest
  and shortest were counted. `SVF_calcBlockZDFFloat` loops come from
  `tools/dsp_test/elf_loopdump.py`.
- **Prototypes:**
  - Built from copies of `CrumpBitEffect.c`, compiled with the firmware DSP
    flags (`-mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -Ofast`,
    no LTO), and counted the same way.
  - **Method check:** the unmodified copy measures 111 / 101 (loop) and 20
    per walk step, the same as the linked image.
  - New filter state was kept in a file-scope array, loaded before the loop
    and stored after it. The real code keeps it in the runtime, and counts
    may move ±2–3 with register allocation.
- **Not prototyped:** the soft knee (estimated), and per-block coefficient
  code (estimated ≈ 1–2 per frame).
