# S088 P0 — Oscillator Run Modes: Plan

**Session:** 088, phase P0 (plan and open questions; no firmware code)
**Branch:** `dev-ph7-voice_expansion` (base `2ab359eb`, post-S087 closeout)
**Date:** 2026-10-29

**Inputs:**
- `Helicase-dev-ph7-voice-pwm-spike/S085_POST_MORTEM_OSC_RUN.md` — the
  three run modes, the pool, instruction costs, and the P2 amp-retrigger
  design.
- `Helicase-dev-ph7-voice-pwm-spike/S085_P2_OSC_RESET_FOLLOWUP.md` — gain-
  matched retrigger, one-block ramp, post-chain changes, engine changes.
- `Helicase-dev-ph7-voice-pwm-spike/S085_P1_OSC_MOD_PLAN.md` — the full
  P1 evaluation, including the carrier-only `itp` decision (Q14) and the
  2-unit pool.
- `Helicase-dev-ph7-spike/S084_POST-MORTEM.md` — the S084 rollback root
  cause: no hardware CPU gate.
- `Helicase-dev-ph7-voice-pwm-spike/S085_POST_MORTEM.md` — the S085
  rollback: `pwm` worst case exceeded the budget.
- The current tree: `2ab359eb`, S087 closeout.

---

## 0. Starting state (measured 2026-10-29)

### 0.1 What exists

| Item | State |
|------|-------|
| Oscillator types | `wav`/`smp`/`slp`/`wtb` (S087 stages B–E, committed) |
| `osc1_type` row | ROW_SELECT_SPECIAL, step-automatable, not morphable (S087 B) |
| Wavetable interpolation slots | 2 slots via `modNode_interpClaim()` (S087 E), sticky generation stamps, used by `wtb` frame blending |
| Drum mod modes | `fm`/`mix`/`snc`/`rng` (S085 P1, committed) |
| `pos` | continuous oscillator position, phase-bend per block (S086 P3/P4) |
| Voice post-chains | fused S073 chains; no gain ramp (single gain endpoint) |
| `SlopeEg2` | `slopeEg2_trigger()` only; no `retriggerFrom()`, no audible-gain test |
| Run modes | **none** — no `fre`, no `itp`, no run parameter |
| Render pool | **none** — the interpolation slots do not carry old-carrier copies |

### 0.2 Voice struct sizes (S087 close, from `STORAGE_SRAM_MANIFEST.md`)

| Struct | Size | Runtime slot | Margin |
|--------|-----:|-------------:|-------:|
| `DrumVoice` | 596 B | 1,192 B | 596 B |
| `SnareVoice` | ~480 B (est.) | 1,192 B | ~712 B |
| `CymbalVoice` | ~520 B (est.) | 1,192 B | ~672 B |
| `HiHatVoice` | ~530 B (est.) | 1,192 B | ~662 B |

Exact sizes will be measured (C-sizes) before any struct change.

### 0.3 Oscillator cost ceiling (§12 Binding Mandates)

The Drum `fm` ceiling with user samples is ≈ 92 instructions per sample
(linked ELF, frozen at S085 close). The mandate: "the dearest setting of a
voice in any mode must not exceed that voice's dearest setting before the
change."

The post-chain costs are: Drum 32, Snare 30, Cymbal/HiHat 25 (S073).

---

## 1. The three run modes

### 1.1 `rst` (reset) — the default

Exactly today's behaviour. Every trigger resets the oscillator phase, clears
the filter, restarts the amp envelope via `slopeEg2_trigger()`. No new code
path runs when the mode is `rst`.

**Rule:** `rst` output must be bit-identical to the pre-S088 tree. All new
per-sample arithmetic must run at flat (zero-cost) endpoints when `rst` is
active, matching the constant-CPU mandate.

### 1.2 `fre` (free-run)

On a retrigger in `fre`:

| Component | Behaviour |
|-----------|-----------|
| Main oscillator phase | **Not reset.** The oscillator continues from its current phase. |
| Pitch envelope (Drum, Snare) and snap envelope | Restart (they change frequency, not amplitude). The oscillator's existing pitch then slews to the new target through `osc_setFreq()`. |
| Mod envelope (Drum `fm`/`snc`/`rng`) | Restarts. The Drum FM depth ramp (S085 P2 F-A) covers the depth jump on the armed block. |
| Amp envelope | Gain-matched retrigger: `slopeEg2_retriggerFrom()` sets the raw value so the attack curve returns the current gain. When the gain is audible (≥ 1/32768), the next block ramps from the previous gain to the new one (one block, 32 samples, 0.73 ms). When fully decayed, a fresh attack from silence. |
| Velocity | A new velocity steps the level; the armed retrigger block ramps from the previous velocity gain (Drum) or from the previous amp gain (Snare/Cymbal/HiHat). |
| Filter | Keeps ringing. The SVF state is not cleared. Cutoff and resonance slew from their current coefficients toward the new target (filter coefficient slew, `SVF_recalcFreqFrom()`). |
| Transient sample | Restarts (the intended attack sound). |

**CPU rule:** `fre` must not exceed the CPU cost of `rst`. The new per-
sample arithmetic (the slew add, the post-chain gain ramp) runs at flat
endpoints in `rst`, so the constant-CPU cost is paid in every mode. The
per-trigger gain match is control-rate (one divide). The filter slew is a
per-block `SVF_recalcFreqFrom()` call in place of `SVF_recalcFreq()`, at
the same cost.

### 1.3 `itp` (interpolate)

On a retrigger in `itp`:

| Component | Behaviour |
|-----------|-----------|
| Main oscillator phase | **Reset** (as `rst`). |
| Old carrier copy | If the amp envelope is **not fully decayed** (gain ≥ 1/32768): the carrier's phase and waveform state are snapshotted. One of the two interpolation slots is claimed for the old copy. The old copy is rendered alongside the new carrier, and a linear crossfade (132 samples, ≈ 3 ms) blends from old to new. |
| Fully decayed | If the amp is fully decayed (gain < 1/32768): **behaves exactly as `rst`**. No snapshot, no slot claim, no crossfade. |
| Modulators (Drum osc2, Cymbal/HiHat osc2/osc3) | **Not reset** in `itp`. They slew to the new pitch as in `fre`. The old carrier copy is rendered **plain** (carrier-only, S085 P1 Q14) — no FM/sync/ring applied to the old copy. |
| Amp envelope | As `fre`: gain-matched retrigger, one-block ramp when audible. |
| Velocity | As `fre`. |
| Filter | As `fre`: keeps state, coefficient slew. |
| Transient sample | Restarts. |

**Interpolation slot sharing:**

The two slots (`OSC_WAVE_INTERP_MAX_ACTIVE` = 2) are shared between `wtb`
frame blending and `itp` crossfades. Claim order per block:

1. **Ongoing `itp` crossfades** (never cut mid-fade).
2. **Wavetable frame blending** (`wtb` oscillators).
3. **New `itp` crossfades**, lowest slot number first.

A refused `itp` trigger (no free slot) behaves as `fre`: the carrier phase
is not reset, the oscillator slews to the new pitch. No click, but no
crossfade.

An oscillator set to `run` = `itp` and also using a wavetable that blends
at the moment of a retrigger may lock both slots simultaneously.

**CPU rule:** one `itp` crossfade = one extra carrier render (≤ the dearest
oscillator loop) + 23 instructions per sample for the blend. This is the
same budget as one wavetable interpolation slot. The pool is budgeted as
always full (2 units), so the cost is constant and independent of how many
crossfades are active.

---

## 2. Parameter: `osc1_run`

### 2.1 Storage

| Property | Value |
|----------|-------|
| SD key | `osc1_run` |
| Short label | `run` |
| Long label | `Run Mode` |
| Category | `Oscilltr` |
| Values | 0 = `rst`, 1 = `fre`, 2 = `itp` |
| Flags | `AUTOMATABLE \| SELECTOR` (not `MORPHABLE`, not `MODULATABLE`) |
| Morph | Not morphable. SHIFT Morph view shows/edits the Normal value (S078 rule). |
| LFO/velocity | Not a target. |
| Step automation | Yes: one value per step, no ramp. |
| Default | 0 (`rst`): a file without the key loads today's sound. |
| Descriptor macro | `ROW_SELECT_ONOFF_SPECIAL` with `IM_SPECIAL_OSC_RUN \| IM_SPECIAL_OSC1` |

**Dtype mechanism (D12 accepted):** All 16 `DTYPE_MENU` ids and all 16
dtype enum values are taken (S078 binding mandate). The `run` parameter
uses a **high-nibble tag on `DTYPE_ON_OFF`** — the same packing pattern
`DTYPE_MENU` uses for its menu id, applied to a non-MENU dtype whose high
nibble was previously unused.

**Concrete design:**

1. `ONOFF_TAG_OSC_RUN = 1u` (defined near `oscRunNames` in `MenuText.h`).
2. The descriptor's dtype byte = `DTYPE_ON_OFF | (ONOFF_TAG_OSC_RUN << 4)`
   = `0x16`. The low nibble `& 0x0f` = 6 = `DTYPE_ON_OFF`; the high nibble
   `>> 4` = 1.
3. `oscRunNames[][4]` in `MenuText.h`:
   `{3}, {"rst"}, {"fre"}, {"itp"}` (same pattern as `mixFmNames`).
4. Six `DTYPE_ON_OFF` switch sites in `menu.c` gain one branch: if
   `(dtype >> 4) != 0`, dispatch to `oscRunNames` instead of `menuText_on`/
   `menuText_off`. Sites: `menu_formatCellValue3()` (line 3346),
   `menu_repaintGeneric()` (6491), `menu_clampCellValue()` (6759),
   `menu_automationValueMax()` (13257), `menu_formatAutomationValue3()`
   (13298), step-automation edit display (14417).
5. `seq_rampDtypeOk()` already excludes `DTYPE_ON_OFF` — no change.
6. No new dtype values, no new MENU ids, no mask widening.

**Verified:** all `>> 4` reads of the dtype byte occur only inside
`case DTYPE_MENU:` branches. The high nibble of non-MENU dtypes is never
read by existing code.

### 2.2 Menu placement

**Osc sub-page, second screen, after `typ`.**

The Drum osc page (line 328 of `DrumParameters.c`) currently has:
- Screen 1 (cells 0–3): `coa fin wav pos`
- Screen 2 (cells 4–7): `typ` + 3 empty

After S088:
- Screen 2 (cells 4–7): `typ run` + 2 empty

The same applies to Snare, Cymbal and HiHat.

### 2.3 Descriptor row

Appended as the last row of each engine's descriptor array (after
`osc1_type`), so every existing descriptor index, automation ID, LFO/
velocity token, NRPN ID and MIDI Learn row is unchanged. A file without
the key loads 0 (`rst`, today's sound).

New macro (one per engine parameters file):
```c
#define ROW_SELECT_ONOFF_SPECIAL(key_, cat_, long_, short_, tag_, member_, type_, special_) \
    { key_, short_, long_, cat_, (uint8_t)(DTYPE_ON_OFF | ((tag_) << 4)), \
      (INSTRUMENT_PARAM_FLAG_AUTOMATABLE | INSTRUMENT_PARAM_FLAG_SELECTOR), \
      MOD_NONE, BIND_SPECIAL(member_, type_, special_) }
```

Usage (Drum; Snare/Cymbal/HiHat identical except the struct member path):
```c
ROW_SELECT_ONOFF_SPECIAL("osc1_run", "Oscilltr", "Run Mode", "run",
    ONOFF_TAG_OSC_RUN, osc.runMode, TYPE_UINT8,
    IM_SPECIAL_OSC_RUN | IM_SPECIAL_OSC1),
```

### 2.4 Runtime binding

The value is stored in a new `uint8_t runMode` field on each engine struct
(inside the oscillator section state). The special writer
`IM_SPECIAL_OSC_RUN` clamps the value to 0..2 and stores it. The engines
read it at trigger time and at the start of each render block.

### 2.5 Scene storage

One byte per voice, stored through the existing supplemental selector path
(as `osc1_type`). Applied before the Morph image. Survives a type round
trip and a step-automation release.

---

## 3. Implementation layers

The work is ordered so each layer compiles against the one before it, with
host tests at each gate.

### Layer 1: The parameter (`run`) — menu, storage, preset

Add the `osc1_run` descriptor to all four engines. Add the
`ONOFF_TAG_OSC_RUN` constant, the `oscRunNames` table, the
`IM_SPECIAL_OSC_RUN` writer (value 22 in the enum; clamps to 0..2,
stores to engine `runMode`), the `ROW_SELECT_ONOFF_SPECIAL` macro, the
six `DTYPE_ON_OFF` special-case branches in `menu.c`, the preset selector
path, and the osc sub-page cell. Build clean; the value is stored and
displayed but has no DSP effect. **Gate:** `make all`, `special_tags`,
the existing suite unchanged.

### Layer 2: `fre` — the free-run oscillator

#### 2a: Amp envelope gain-matched retrigger

- Add `slopeEg2_retriggerFrom()` and `slopeEg2_isAudibleGain()` to
  `SlopeEg2.c/.h` (the S085 P2 design).
- In each engine's trigger: when `runMode != OSC_RUN_RST`, call
  `slopeEg2_retriggerFrom()` instead of `slopeEg2_trigger()`, and set
  `ampRampArm` when the gain is audible.
- Add `lastVeloGain`/`ampRampArm` to Drum; `lastAmpGain`/`ampRampArm` to
  Snare/Cymbal/HiHat. These sit inside the runtime slot margin.

**Gate:** `ampretrig` host test (gain match, audible test, ramp wiring,
no-step). `rst` must produce 0 differing samples against the pre-S088
post-chain golden.

#### 2b: Post-chain gain ramps

- Modify `voicePost_drum()`, `voicePost_mixAddGainDist()`,
  `voicePost_addGainDist()` to accept `last` gain endpoints and ramp
  across the block.
- With equal endpoints (every `rst` call and every non-retrigger block),
  the ramp is `last + frac·(gain − last) = gain`: bit-identical.

**Gate:** `postchain` host test: 0 differing samples. `armcheck-postchain`
cost report: Drum ≤ 35, Snare ≤ 35, Cymbal/HiHat ≤ 30.

#### 2c: Phase continuity and pitch slew

- In each engine's trigger: when `runMode == OSC_RUN_FRE` or
  `runMode == OSC_RUN_ITP`, skip the main oscillator phase reset. The
  oscillator continues running; `osc_setFreq()` in the next async block
  glides the increment.
- In `fre`: also skip `SVF_reset()` (Drum) or the equivalent filter
  clear; the filter rings through.

**Gate:** host test `osc_run_fre`: a retrigger with `fre` produces
continuous output; the largest sample-to-sample step is bounded by the
attack-block ramp and the pitch-envelope restart.

#### 2d: Filter coefficient slew

- Add `SVF_recalcFreqFrom()` to `ResonantFilter.c/.h` (a four-line
  function that computes the new coefficients by stepping from the current
  ones). In `fre`/`itp`, call it instead of `SVF_recalcFreq()`.
- The slew rate: 0.28 per block (≈ 2 ms), matching the spike design.
- In `rst`: `SVF_recalcFreq()` unchanged.

**Gate:** host test covers the filter-slew path; `rst` unchanged.

#### 2e: Drum FM depth ramp (F-A)

- On the armed block in `fre`, the modulator block is scaled from
  `oldDepth / newDepth` to 1 across the block.
- Flat on every other block, under an active `itp` crossfade, and in
  `mix`/`snc`/`rng`.
- Cost: +15 instructions per sample on Drum `fm`, every block.

**Gate:** `armcheck` cost probe. The Drum `fm` ceiling must hold.

### Layer 3: `itp` — the interpolate mode

#### 3a: Carrier snapshot and crossfade

- On an `itp` trigger with the amp still audible: snapshot the carrier's
  phase and waveform state into an `OscCarrierSnapshot` struct (one per
  engine, in the engine struct).
- Claim one interpolation slot via a separate counter/flag in the engine
  struct (D2 accepted: separate counter, not the `interpStamp` path).
  The claim order is enforced by the mixer's block-start sequence:
  ongoing `itp` first, then `modNode_resetTargets()` for wavetable.
- For 132 samples (≈ 3 ms, D10 accepted), render the old copy alongside
  the new carrier and blend: `out = (1 − t)·old + t·new`.
- **Carrier-only crossfade** (S085 P1 Q14): the old copy is rendered as
  a plain carrier — no FM, no sync, no ring mod — regardless of the
  engine's mod mode. The modulators continue for the new voice only.
  This keeps the cost to one plain carrier render + 23 blend per sample.
- Full-block blend (D4 accepted): no partial-block early termination.
  When the counter expires mid-block, the old carrier renders the full
  block (its tail samples are at or near zero gain). Simpler, constant
  cost per crossfade block.
- The crossfade counter lives in the engine struct. When it reaches 0 the
  snapshot is deactivated and the slot is released.

**Snapshot state:**

| Field | Type | Size |
|-------|------|-----:|
| `phase` | `uint32_t` | 4 |
| `phaseInc` | `uint32_t` | 4 |
| `waveform` | `uint8_t` | 1 |
| `tableOffset` | `uint8_t` | 1 |
| `active` | `uint8_t` | 1 |
| `counter` | `uint8_t` | 1 |
| (sample cache fields for smp/slp/wtb) | | ~16 |

Estimated: ≤ 28 B per engine. All four engines carry one snapshot whether
or not they use `itp`, to keep the struct size constant (the constant-CPU
mandate applies to RAM too, since the slot reserve is fixed).

#### 3b: Slot priority integration

- The engine struct carries a `uint8_t itpSlotActive` flag (D2 accepted:
  separate counter, not the `modNode_interpClaim()` stamp path). The
  mixer's block-start sequence checks active `itp` flags first, then
  calls `modNode_resetTargets()` for wavetable blending. Claim order:
  1. ongoing `itp` crossfades (already active);
  2. wavetable frame blending (`modNode_interpClaim()` path);
  3. new `itp` crossfades, lowest slot first.
- A refused `itp` claim (both slots occupied) falls back to `fre`
  behaviour (no phase reset, no crossfade, amp ramp from current state).

**Gate:** host test `osc_run_itp`: a retrigger with `itp` and amp still
audible crossfades; a retrigger with `itp` and amp fully decayed behaves
as `rst`. Slot-exhaustion test: all slots occupied → `fre` fallback.

#### 3c: `itp` render helper (D3 accepted: helper function)

- A single helper `oscSection_renderOldCarrier()` dispatches the old
  carrier render:
  ```c
  void oscSection_renderOldCarrier(OscInfo *snapshot, int16_t *buf,
                                    uint8_t size, float gain);
  ```
  It calls `osc_renderTyped()` when `osc_isTyped(snapshot)`, else
  `calcNextOscSampleBlock()`. One dispatch site for all four engines.
- Each engine's render block, when `snapshot.active`: calls the helper
  into a scratch buffer, then blends with the live carrier output.
- The old carrier renders **plain** (carrier-only, S085 P1 Q14). The
  modulators (FM, sync, ring) are not applied to the old copy. They
  slew to their new pitch (as in `fre`), never reset, and drive only
  the new carrier.

**Gate:** cost probe: one `itp` crossfade ≤ the dearest plain carrier
loop + 23 blend instructions per sample.

### Layer 4: Tests, proofs, specifications

- Break proofs for every new host test.
- `elf_osc_check` / `elf86_mixer_check` against the pre-S088 snapshot.
- Update `INSTRUMENTS_DSP_REFERENCE.md`, `CPU_USE_DSP_AUDIT.md`,
  `STORAGE_SRAM_MANIFEST.md`, `FILESYSTEM_SPEC.md`, `MEMORY.md`.
- Hardware matrix.

---

## 4. Cost analysis

### 4.1 Per-sample costs (from S085 P2 measurements)

| Addition | Cost | When paid |
|----------|------|-----------|
| Post-chain gain ramp: Drum | +3 per sample (32 → 35) | every mode, every block |
| Post-chain gain ramp: Snare | +5 per sample (30 → 35) | every mode, every block |
| Post-chain gain ramp: Cymbal/HiHat | +5 per sample (25 → 30) | every mode, every block |
| Drum FM depth ramp (F-A) | +15 per sample | Drum `fm`, every block |
| `itp` crossfade (one slot) | ≤ dearest carrier render + 23 blend | while a crossfade runs |
| Pool (2 slots), system-wide | 2 × ≤ (dearest carrier + 23) | budgeted always full |

### 4.2 Per-trigger costs (control rate)

| Addition | Cost |
|----------|------|
| Gain match (`slopeEg2_retriggerFrom()`) | ~15–20 instructions, one VDIV |
| Carrier snapshot (`itp`) | a few stores |

### 4.3 The constant-CPU question

**Post-chain ramps:** +3/+5/+5 per sample runs in every mode (flat
endpoints in `rst`). System worst case: 6 × 5 = +30 per sample, about
0.6 % of the 4,897-cycle sample period.

**Drum F-A ramp:** +15 per sample on Drum `fm`, every block. This is
within the existing Drum `fm` ceiling of ≈ 92 only if the current Drum
`fm` linked cost is ≤ 77. **Must be measured.**

**`itp` pool:** budgeted always full. Cost = 2 × (dearest carrier loop +
23). The dearest carrier is the same render the voice already runs; the
pool adds a second copy of it. This is the same budget the wavetable
interpolation slots already carry (S087 E). **Must be measured to confirm
the pool worst case stays within the frame.**

### 4.4 `fre` vs `rst` CPU

`fre` adds no per-sample cost beyond what `rst` already pays (the flat
ramp endpoints). Per-trigger: one VDIV for the gain match. Per-block: the
filter slew function replaces the existing recalc at the same cost.

**Conclusion:** `fre` does not exceed `rst`.

---

## 5. RAM analysis

### 5.1 Engine struct growth

| Struct | Added fields | Estimated growth |
|--------|-------------|:----------------:|
| DrumVoice | `uint8_t runMode; uint8_t ampRampArm; float lastVeloGain; OscCarrierSnapshot snapshot;` | +4 (ramp) + ~28 (snapshot) = ~32 B |
| SnareVoice | `uint8_t runMode; uint8_t ampRampArm; float lastAmpGain; OscCarrierSnapshot snapshot;` | +8 (ramp) + ~28 (snapshot) = ~36 B |
| CymbalVoice | same as Snare | ~36 B |
| HiHatVoice | same as Snare | ~36 B |

All within the current runtime slot margin (smallest: Drum at ~596 B in a
1,192 B slot, leaving ~596 B).

### 5.2 Static RAM

No new static allocations. The interpolation slot count stays at 2
(`OSC_WAVE_INTERP_MAX_ACTIVE`). The old-carrier render writes into a
scratch block that can be a `static` local in the engine render (SRAM2
stack) or a small static buffer. **No DTCM or SRAM1 growth needed.**

---

## 6. Risks

| # | Risk | Likelihood / Impact | Mitigation |
|---|------|---------------------|------------|
| R1 | The post-chain +3/+5 pushes the frame over budget | Low / Medium | Measure on hardware before accepting. The spike's figures (35/35/30) were confirmed on ARM. |
| R2 | Drum F-A +15 pushes Drum `fm` over its ceiling | Medium / Medium | Measure before implementing. If it exceeds the ceiling, the F-A ramp can be made conditional on `fre`/`itp`, but that breaks constant-CPU. Present the trade-off. |
| R3 | `itp` snapshot struct size exceeds the slot margin | Low / Low | Measure the snapshot at ~28 B; the tightest margin (Drum) has ~596 B free. |
| R4 | The `itp` crossfade render exceeds the pool budget | Medium / Medium | The pool is budgeted as always full. The extra render is the same loop the voice already runs. Measure the blend cost. |
| R5 | `itp` claim priority conflicts with `wtb` blending: both slots locked by crossfades, `wtb` steps for 3 ms | Medium / Low | Accepted by design (S085 P1 Q14). `wtb` stepping for 3 ms is inaudible. An `itp` + `wtb` voice can lock both simultaneously: this is the stated specification. |
| R6 | Carrier-only `itp` has never been heard in this tree (new code, not carried from the spike) | Medium / Low | Host test + hardware listening matrix. |
| R7 | A retrigger less than 3 ms into an active crossfade re-snapshots the live carrier, stepping by (1−t)·(old−new) | Certain on fast rolls / Low | Accepted (S085 P1 F6). |
| R8 | The `runMode` byte adds to every engine struct whether or not `itp` is used | None / None | Constant-CPU mandate: the struct size is fixed, not mode-dependent. |
| R9 | Filter slew in `fre`/`itp` at deep resonance rings indefinitely | Low / Low | The slew converges. A resonance of 127 with a stationary filter already rings; `fre` just doesn't clear it. |

---

## 7. Ambiguities

| # | Point | Assumption | Status |
|---|-------|------------|:------:|
| A1 | **`itp` crossfade length.** 132 samples (≈ 3 ms). | 132 samples | **D10 accepted** |
| A2 | **Filter slew rate.** 0.28 per block. Tuning constant. | 0.28 per block | **Hardware tuning** |
| A3 | **`itp` when fully decayed = exactly `rst`.** Phase reset, filter clear, `slopeEg2_trigger()` all fire. | Yes | **D6 confirmed** |
| A4 | **Drum `modOsc` in `fre`/`itp`.** No change to modOsc restart. | No change | **Confirmed** |
| A5 | **Drum FM depth ramp (F-A).** +15/sample on Drum `fm`, every block, constant (D1). Not yet measured against ceiling. | Include (click is audible without it) | **Gated on measurement** |
| A6 | **`itp` on typed oscillators (`smp`/`slp`/`wtb`).** Snapshot includes bind fields. Finished `smp` = silence. `wtb` keeps frame. | All types in `itp` | **Confirmed** |
| A7 | **Snare noise in `fre`/`itp`.** Slew, no snapshot. | Slew only | **Confirmed** |
| A8 | **`rng` depth: "ramp always" in `fre`/`itp`.** §12 mandate. | Implement | **D7 confirmed** |
| A9 | **HiHat open/closed decay in `fre`/`itp`.** Matched attack decays at the new rate. | Listen on hardware | **Hardware listening** |
| A10 | **Repeat mode with `fre`/`itp`.** `retriggerFrom()` falls back to `trigger()` in repeat; one-block ramp covers the jump. | Keep S085 P2 design | **Confirmed** |

---

## 8. Architectural questions (all resolved)

### Q1. The Drum F-A ramp: constant or conditional?

**Resolved (D1): constant.** The F-A ramp costs +15 per sample on Drum
`fm` in every mode. The ceiling rises from ≈92 to ≈107. Gated on
hardware measurement: if the ceiling breaks, D1 is revisited.

### Q2. `itp` claim integration

**Resolved (D2): separate counter/flag in the engine struct.** The claim
order is enforced by the mixer's block-start sequence — ongoing `itp`
flags first, then `modNode_resetTargets()` for wavetable, then new `itp`.
This avoids overloading the `interpStamp` path.

### Q3. Old carrier render location

**Resolved (D3): a helper `oscSection_renderOldCarrier()`.** One dispatch
site: `osc_renderTyped()` when `osc_isTyped()`, else
`calcNextOscSampleBlock()`. Always the **plain carrier** path — no FM,
sync, or ring on the old copy (carrier-only, S085 P1 Q14). All four
engines call it identically when `snapshot.active`.

### Q4. Crossfade partial-block handling

**Resolved (D4): full-block blend.** When the counter expires mid-block,
the old carrier renders the full 32 samples and the blend runs to the
end. Simpler, constant cost per crossfade block; the tail samples are at
or near zero gain.

### Q5. Snapshot state per oscillator type

Unchanged from the plan; included in 3a's snapshot table. Key points:
- `wav`: phase, phaseInc, waveform, tableOffset
- `smp`/`slp`/`wtb`: + sample cache / frame bind fields
- `noise`: phase only (hardware RNG not copyable — accepted, D5)

### Q6. `pos` in the snapshot

`pos` is baked into the phase at trigger time, so the snapshot carries it
implicitly. `positionBendStep` does NOT apply to the old copy — the old
carrier renders at its frozen `phaseInc`.

### Q7. Build order

**Resolved (D8): ramps first.** Post-chain ramps (Layer 2b) land as the
first change with flat endpoints everywhere, gated by the `postchain`
host test. Run modes build on top.

### Q8. `runMode` storage location

**Resolved (D9): `uint8_t runMode` in each engine struct** (Drum, Snare,
Cymbal, HiHat), written by `IM_SPECIAL_OSC_RUN`. Not in `OscInfo` —
run mode has no per-oscillator meaning for modulators.

---

## 9. Decisions (all accepted)

| # | Question | Accepted | Gate |
|---|----------|----------|------|
| D1 | F-A ramp | **(a) constant**, +15/sample on Drum `fm` every mode | Ceiling measurement |
| D2 | `itp` claim | **(a) separate counter** in engine struct | — |
| D3 | Old carrier render | **(b) helper** `oscSection_renderOldCarrier()`, plain carrier only | — |
| D4 | Crossfade block | **Full-block blend**, no partial-block early stop | — |
| D5 | `itp` on noise | **Accept** — two noise streams blending is "noise" | — |
| D6 | Fully decayed = `rst` | **Confirmed** — phase reset, filter clear, `slopeEg2_trigger()` | — |
| D7 | `rng` "ramp always" | **Confirmed** — `fre`/`itp` ramp smoothly, no snap | — |
| D8 | Build order | **(a) ramps first** — Layer 2b lands before run modes | — |
| D9 | `runMode` location | **(a) engine struct** `uint8_t runMode` per engine | — |
| D10 | Crossfade length | **132 samples** (≈ 3 ms) | — |
| D11 | RAM growth | **Approved** — ~32–36 B per engine, within slot margin | — |
| D12 | Dtype mechanism | **(c) `DTYPE_ON_OFF` high-nibble tag** `ONOFF_TAG_OSC_RUN` | — |

---

## 10. Hardware matrix (preliminary)

| # | Check |
|---|-------|
| H1 | `rst` A/B against the pre-S088 build, every engine: sounds identical |
| H2 | `fre`, Drum `fm`, retrigger halfway through the decay at attack 0, 20, 30, 64, 127 and slope 0, 32, 64, 96, 127: no click; the level continues and rises |
| H3 | `fre`, Drum `mix`/`snc`/`rng`: no click on retrigger |
| H4 | `fre`, Snare/Cymbal/HiHat: no click on retrigger |
| H5 | `fre`, Drum FM at high amount: no sporadic click (the F-A ramp) |
| H6 | `fre`, retrigger after the sound has fully decayed: a normal attack from silence |
| H7 | `itp`, Drum/Snare/Cymbal/HiHat: retrigger with level left produces a smooth crossfade, no click |
| H8 | `itp`, retrigger after full decay: exactly like `rst` |
| H9 | `itp`, fast retriggers (rolls): the 3 ms re-snapshot limit is audible but not a click |
| H10 | `itp` + `wtb` oscillator: both slots can lock simultaneously; wtb stepping is inaudible |
| H11 | `itp`, Drum `fm`: the carrier-only crossfade sounds smooth (old carrier renders plain, no FM on old copy) |
| H12 | HiHat open/closed in `fre`/`itp`: the decay switch sounds right |
| H13 | `volumeMod` on, alternating velocities on fast retriggers in `fre`/`itp`: no click |
| H14 | Repeat mode with `fre`/`itp`: the jump to full is ramped |
| H15 | Step automation of `run`: switching between `rst`/`fre`/`itp` per step |
| H16 | Worst-case Scene (all six voices, dearest types, pool full): `cpu` figure, no underruns |
| H17 | `fre`/`itp` on `smp`/`slp`/`wtb` osc types |

---

## 11. What this plan does not cover

- **New oscillator waveforms or types.** This is run modes only.
- **PWM.** The S085 post-mortem shows PWM exceeded the CPU budget. PWM is
  a separate scope item and is not part of S088.
- **Pitch slew per oscillator.** The spike added `osc_armSlew()` for 132-
  sample pitch glides. The current tree has `osc_setFreq()` which updates
  `phaseInc` once per block. For `fre`/`itp`, the pitch change from one
  block to the next is already gradual because the pitch envelope decays
  smoothly and `osc_setFreq()` runs every block. A per-sample slew would
  be smoother but is not in scope unless the block-rate step is audible on
  hardware.
- **LFO one-shot waves, deferred instrument swap.** These are S084
  deferred items and are not part of S088.

---

## 12. References

- S085 P2 amp retrigger design: `Helicase-dev-ph7-voice-pwm-spike/S085_P2_OSC_RESET_FOLLOWUP.md`
- S085 run-mode costs: `Helicase-dev-ph7-voice-pwm-spike/S085_POST_MORTEM_OSC_RUN.md`
- S085 carrier-only `itp` decision: `Helicase-dev-ph7-voice-pwm-spike/S085_P1_OSC_MOD_PLAN.md` §4.3.7
- S085 post-mortem (CPU failure): `Helicase-dev-ph7-voice-pwm-spike/S085_POST_MORTEM.md`
- S084 post-mortem: `Helicase-dev-ph7-spike/S084_POST-MORTEM.md`
- DSP reference: `knowledge_files/specification_reference/dsp_instruments_effects/INSTRUMENTS_DSP_REFERENCE.md`
- CPU audit: `knowledge_files/specification_reference/dsp_instruments_effects/CPU_USE_DSP_AUDIT.md`
- SRAM manifest: `knowledge_files/specification_reference/STORAGE_SRAM_MANIFEST.md`

---

## 13. Remaining open items after D1–D12

All architectural questions (Q1–Q8) and user decisions (D1–D12) are
resolved. The following items remain gated on hardware or are
implementation details to be settled during coding.

### Hardware-gated

| # | Item | When | Blocks |
|---|------|------|--------|
| G1 | **Drum `fm` ceiling with F-A ramp.** D1 accepted constant +15. The ceiling rises from ≈92 to ≈107 per sample. Must fit within the frame budget. | Layer 2e `armcheck` | Layer 2e acceptance; if it fails, D1 is revisited (conditional ramp). |
| G2 | **Filter slew rate tuning (A2).** 0.28 per block is the starting value. May need adjustment on hardware. | Layer 2d hardware test | Cosmetic; does not block other layers. |
| G3 | **HiHat open/closed decay in `fre`/`itp` (A9).** The gain-matched retrigger selects the new decay rate. Must sound right. | H12 hardware check | Cosmetic; may need a conditional decay-switch path if the jump is audible. |
| G4 | **Post-chain ramp cost confirmation (R1).** +3/+5/+5 per sample. The spike measured 35/35/30 on ARM. Must be re-confirmed in the current tree. | Layer 2b `armcheck-postchain` | Layer 2b acceptance. |
| G5 | **`itp` pool worst case (R4).** Two plain-carrier renders + 2×23 blend. Must fit in the frame with all six voices active. | Layer 3 `armcheck` | Layer 3 acceptance. |

### Implementation details (settled during coding)

| # | Item | Notes |
|---|------|-------|
| I1 | **Scratch buffer for old carrier render.** A `static int16_t[OUTPUT_DMA_SIZE]` in the engine render or a shared static in `oscSection_renderOldCarrier()`. SRAM2 stack or `.bss`. No DTCM growth. |
| I2 | **`itp` slot counter: block-level or sample-level?** The counter counts down by `size` each block (block-level). 132 samples = 4.125 blocks → the last block runs 4 extra samples of blend (D4: full-block, accepted). The counter can be `uint8_t` (max 255 > 132). |
| I3 | **`itpSlotActive` bookkeeping.** The mixer's pre-render pass reads `itpSlotActive` from each engine struct, counts active slots, and passes the remaining count to `modNode_resetTargets()`. A new `itp` trigger during the render checks the remaining count. |
| I4 | **`IM_SPECIAL_OSC_RUN` writer body.** Clamp to `OSC_RUN_ITP` (2), store to engine `runMode`. No side effects on the oscillator state — the mode takes effect on the next trigger. |
| I5 | **`oscRunNames` short-name entry.** "run" already exists in `shortNames` (index for the track play-mode cell). No conflict: short names are display labels, not unique keys; the descriptor `key` ("osc1_run") is unique. |
| I6 | **Preset / Scene selector path.** Follows the `osc1_type` precedent: supplemental selector written before the Morph image, so the run mode is available at trigger time. |
