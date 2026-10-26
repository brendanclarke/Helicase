# S084 P4 — Voice Architecture Remediation (plan)

**Status:** plan only. Each item needs a decision or a hardware measurement
before code is written.

**Context:**
- **What this covers:** the work deferred from the P3 review
  (S084_P3_IMPLEMENTATION.md §17) and remediation (§18). The direct fixes
  are in §18; everything here is non-obvious, architectural, or risky
  enough to need your input first.
- **The filter drive's second parameter:** it has its own plan,
  S084_P5_FILTER_DRIVE_TYPE_REMEDIATION.md. P5 waits for item 1 here,
  because selecting a drive type also freezes the interface.

## 1. Order

| # | Item | Why it is first or later | Needs |
|---|---|---|---|
| 1 | Find the interface freeze | It blocks every other hardware result, including P5 | the board; a DEV build |
| 2 | Measure CPU on the target | The caps and the disabled-type decisions rest on instruction counts, not cycles | item 1 resolved |
| 3 | Decide on the disabled types | Re-enable, rework or drop each | item 2 |
| 4 | Selection order on Scene/Instrument apply | Correctness of loads that mix `buf` and `Ec2` | your choice of rule |
| 5 | Growth policy (C3) | `Ech`→`Ec2` continuation almost never happens now | your choice |
| 6 | Control ramps (C2) | Zipper on LFO or automated insert controls | after item 3 |
| 7 | CrumpBit compaction cost | A possible underrun during a voice-unit shrink | item 2 |
| 8 | Transient option selection timing (C10) | Morph or automation of `twv` under `buf` | your choice |
| 9 | Deviations to ratify | POST placement (D12), Room families (O1) | your choice |
| 10 | DSP models for every insert type | No P3 type was simulated before it was written | before item 3's re-enables |
| 11 | Tests still missing | G2 bit-identity, full C1–C16 with audio | throughout |
| 12 | Small loose ends | The stale engine `insertType`, label wording | any time |

---

## 2. Item 1 — The interface freeze

### 2.1 Symptom

On the first hardware test, selecting a buffer user (the Mix buffer types
and transient `buf`) locks the interface, and so does selecting a filter
drive type other than `sft`. The P1 oscillator work behaves.

### 2.2 Already ruled out (P3 §17.2)

- **Menu:** the menu loops, the cell text and the label hooks.
- **DSP loops:** unbounded loops in the shaper and the shaped ZDF.
- **Memory:** the ITCM contents and copy, the stack (106 KB free) and the
  MPU.
- **Concurrency:** an ISR race (all writers and the render run in the
  foreground).
- **Preset passes:** they are balanced.
- **Steady CPU:** cost estimates put it near 50 %.

### 2.3 Measurements to take, in order

**1. One question:** does audio continue while the interface is locked?
- **Audio plays:** the render loop never yields, so the cause is render
  overrun or a long foreground stall. Go to step 3.
- **Silence, or a repeating buzz:** a fault. Go to step 2.

**2. Fault capture** (new, DEV-only, about 40 lines, no production cost):
- **Handlers:** `HardFault_Handler`, `BusFault_Handler`,
  `UsageFault_Handler` and `MemManage_Handler`, written in `main.c` or a new
  `Core/Hardware/devFault.c`.
- **What they save:** CFSR, HFSR, BFAR, MMFAR and the stacked PC, LR and
  xPSR, into a `.devwdg_noinit` record (the section and pattern of the IWDG
  boot capsule, `filesystem.c:504`). Then they reset.
- **Next boot:** if the record is valid, show it on the DEV boot screen
  (`DEV_MODE_DIAGNOSTIC`) and log it through the existing bootlog capsule
  path.
- **Reading it:** `arm-none-eabi-addr2line -e build/lxr02.elf <pc>` names
  the line.
- **Risk:** low. It only runs after a fault has already happened.

**3. Render overrun capture** (DEV-only):
- **Measure:** time each `mixer_calcNextSampleBlock()` with DWT CYCCNT,
  which `AudioCodecManager.c:259` already enables.
- **Record:** the maximum cycles per block and the count of blocks above
  the budget (4,897 × 32 = 156,700 cycles).
- **Show:** both on the DEV page, next to `audioCodec_underrunCount`.
- **Optional:** per-stage timestamps (voice render, insert stage, Effect,
  mixer) for the slot under test.

**4. Isolation matrix** (one change from a fresh boot each time; note audio
and UI):

| Case | Expected if the fault is in … |
|---|---|
| `fxt` = `Fld` (non-buffer) | the insert manager or VoiceBuffer render path |
| `fxt` = `Cmb` with Effect `off` | the FxBuffer share callback, if `Cmb` with CrumpBit freezes but this does not |
| `fxt` = `Cmb` with CrumpBit active | the CrumpBit compaction |
| transient `buf` = `Flm` | the transient stage |
| `dty` = `rct` on one Drum slot, nothing else changed | the shaped filter (P2 code), independent of P3 |
| the same as the previous case, on Snare / Cymbal / HiHat | one engine's `driveFields` / `shaper` binding |

**5. Fix:** fix whatever 2–4 point to, in the targeted style of §18. If it
is architectural, bring it back as a decision.

### 2.4 Hypotheses worth checking first in the capture

1. **The share callback path.** A buffer claim moves the Effect share. That
   path never ran on the target before P3 (P1/P2 never claimed units), so
   inspect `effects_onShareChanged()` → `buffer_changed` with
   StereoFilter, CrumpBit and `off`.
2. **The shaped filter on the target.** `SVF_calcBlockZDFShaped` passed on
   the host only. The P2 measured costs (107–134 instructions/sample
   against `sft` 100) are not enough alone, but a pathological input such
   as a NaN from a tracker period would show in step 3 as a cost spike or
   in step 2 as a fault.
3. **A writer side effect.** Both the `dty` writer and the Mix/transient
   writers run on menu commit. Check whether something downstream of the
   commit (MIDI out, live record, autosave dirty marking) behaves
   differently for rows at descriptor indices ≥ 44. Only the new rows sit
   there, and the P1 rows that work sit below 44.

---

## 3. Item 2 — CPU on the target

**Why:** the §18.4 caps are static instruction counts with a 25 %
allowance. Cycle cost depends on flash wait states, the ART and I-cache,
DTCM versus SRAM access and dual issue.

**Plan:**
- **Per-stage cycles:** with the item-1 DWT probe, measure cycles per
  block for each stage on one voice. The set:
  - `D+S`, `Fld`, `Crs`, `Rng`, `Pha`, `Ech`, `Cmb`, `Flm`, `Bzz`;
  - the disabled set, by a DEV build that clears the flag;
  - the shaped filters.
- **Worst-case Scene:** six slots, each with the worst kept Mix type plus
  transient `buf`, two ringing tails, CrumpBit active and the worst P2
  voice (`lsw` drive, `wtb` FM, PCM transient). Record the maximum cycles
  per block and the underruns over 10 minutes.

**Decision for you:** keep the per-type caps as they are, or set one CPU
budget for the worst-case Scene (for example ≤ 85 % of 156,700 cycles per
block) and let individual types exceed the `D+S` share if the total fits.
The second choice could re-enable several types without code changes.

---

## 4. Item 3 — The disabled types

**Status:** disabled by `INSERT_FLAG_DISABLED` (P3 §18.4). Their code is
corrected and kept.

| Type | Measured | Possible route back | Recommendation |
|---|---:|---|---|
| `Fla` | 57 | Block-constant tap with a per-sample ramp is still fractional and moving; try integer delay with linear crossfade between two integer taps updated per block (about 35–40) | try; include if it lands ≤ 41 and sounds right |
| `Cho` | 51 | Same as `Fla`; no feedback, so cheaper | try |
| `Rev`/`Rv2` | ≈50 | Drop the two allpasses (−10), keep half-rate combs; or run the network at quarter rate for `Rv2` only | try only after a Python model confirms the sound (item 10) |
| `Res`/`Tub` | 53 | Share one optimised damped comb (the `Cmb` loop plus one one-pole, about 40); `Tub` is the same with negative feedback | needs a transient cap decision: 40 is over 27 |
| `Spr` | 58 | Allpass plus colour on the `Cmb` loop structure, about 42 | same |
| `Plk` | 75 | KS with a block-constant tap: one tap plus a two-point average on adjacent samples (about 30) | try; the seed burst is one block of setup |
| `Rvs` | ≈38 | Precompute a fade ramp (gain += step, clamp) and use an integer add (about 28) | try |

**What to leave out:** anything whose model (item 10) does not show a
clear sonic reason to exist next to a kept type, for example `Tub` next to
`Res`.

---

## 5. Item 4 — Selection order on apply (R7b)

**Problem:** `preset_applySelectorRows()` applies selector rows in
descriptor order, so `trt` comes before the appended `fxt`.
- An incoming `trt = buf` is tested against the outgoing Mix units. Old
  `Ec2` + incoming `buf` drops the `buf`.
- The reverse order has the mirror problem: an outgoing `buf` makes an
  incoming `Ec2` downgrade although it would fit once `buf` is released.

**Options:**
1. **Release-first two-phase apply.** Read both incoming stage selections
   (`fxt`, `trt`, `twv`) first, apply the stage that needs fewer units than
   it holds, then the other. It needs a small helper in
   InsertEffectsManager taking both incoming values (`insertFx_selectBoth`)
   and one call from the pre-pass. **Recommended.**
2. **Fixed priority.** The transient `buf` always wins over `Ec2`/`Rv2`
   (the plan's §5.3.1 hiding rule, applied at load). Simple, but a load can
   still downgrade unnecessarily.
3. **Defer both stage selections** to the end of the pass, with the WAVE
   value known. This also removes the TYPE→WAVE double selection
   altogether (R7a only made it cheap).

**Test:** a host test of the pre-pass with the four combinations (old/new ×
`Ec2`/`buf`), asserting the final types and unit counts.

---

## 6. Item 5 — Growth policy (C3)

**Problem:** growth now claims the higher-address neighbour and keeps the
ring base (§18.1 R3). Units are packed first-free from position 0, so that
neighbour is almost always taken. In practice `Ech`→`Ec2` tails the `Ech`
and starts `Ec2` fresh, which needs a free runner, otherwise Hold.

**Options:**
1. **Accept it.** It is correct and simple; the cost is a restart instead
   of an extension. **Recommended unless you want C3 as planned.**
2. **Buddy layout.** One-unit claims take odd positions (1, 3, …, 11) so
   each keeps its even neighbour free for growth; pairs take even/odd pairs.
   Positions 0–11 never overlap CrumpBit's 70,592 B loop, so this costs
   nothing until more than 12 positions are in use. It is a policy change
   in `fxbuf_unitAcquire()` only, but it changes which positions the share
   shrinks into, so re-run H1.
3. **Downward growth with an offset.** Claim the lower neighbour and give
   the record a ring-offset (reuse the now-unused `pending_type` byte) until
   the write wrap. It touches every buffer type's indexing. Not
   recommended.

---

## 7. Item 6 — Control ramps (C2, R16)

**Problem:** `voiceBuffer_render()` passes each type the previous block's
target with no ramp. `Cmb` pitch and the `Ech` feedback step once per
block.

**Option:** give `insert_block_ctx_t` start and step values (`c0`, `dc0`,
…) and let each type ramp the controls it uses inside its loop (+1–2
instructions per ramped control). Or one-pole the controls per block in
VoiceBuffer (zero per-sample cost, a 32-sample stair remains).

**Recommendation:** per-block one-pole now (no cost); per-sample ramps only
for `Cmb` pitch if a zipper is audible.

---

## 8. Item 7 — CrumpBit compaction cost (R20)

**Problem:** the rotate path does three full-length byte reversals (about
70 k swaps) in the foreground pass that delivers a shrink. That is
estimated at 1.5–2.8 ms against about 4.4 ms of queue slack, plus whatever
else the apply pass does.

**Plan:** measure with the item-1 DWT probe on a forced shrink (DEV
forced-units build) while CrumpBit runs. If more than 1 ms, rotate in
place with a block-swap algorithm using `memmove` and a 64 B scratch, or
split the compaction across the next blocks: CrumpBit can read from the
old geometry until the move completes, at the cost of one extra
`write_pos` offset.

---

## 9. Item 8 — Transient option timing (C10)

**Problem:** the WAVE writer re-selects the transient buffer stage on every
write.
- **Step automation:** close enough to "at the trigger".
- **Morph or an LFO on `twv` under `buf`:** each step is a different use.
  With R7a an unrendered ring costs nothing, but a rendered one tails.

**Options:**
1. Latch the requested option and apply it at `insertFx_transientTrigger()`
   (the plan's C10 wording). **Recommended.**
2. Make `twv` non-Morphable under `buf`; not possible per row value with
   the current descriptor flags.

---

## 10. Item 9 — Deviations to ratify

- **D12, POST placement:** the POST transient stage runs after the amp and
  velocity gain, not right after the filter. Tails outlive the amp EG,
  which suits `Flm`/`Bzz`. Keep it (and update D12), or move it before the
  gain.
- **O1, Room families:** `Rev` and `Rv2` are separate families (§18.2),
  because a split ring cannot grow in place. Only relevant if Room is
  re-enabled.

---

## 11. Item 10 — DSP models before code

**The gap:**
- **P1/P2:** every P1/P2 DSP element had a Python model first:
  `plot_waveshapers.py`, `plot_pwm.py`, `plot_stock_waves.py`,
  `plot_wavetable_formats.py` and `plot_current_drive.py`. The shaper
  model also feeds the host test through `--export-ref`.
- **P3:** no P3 insert type had a model. The unstable Room, the cancelled
  pluck, the stutter and reverse semantics and the 2–5× cost errors would
  all have shown in a model or a cost probe before implementation.

**Plan:**
- **The model:** `tools/plots/plot_insert_types.py`. For each type, at
  several control corners, it plots:
  - the impulse response;
  - the decay envelope against the 3 s line;
  - the level against `D+S`;
  - the spectrum of a drum hit through the type.
- **References:** `--export-ref frozen/insert/` writes reference outputs.
  `test_insert_types` compares the C output to them, at a tolerance of a
  few LSB, as `test_shaper` does.
- **Cost model:** a per-type instruction budget line, filled from
  `armcheck-vx3`.
- **When:** before any type in item 3 is re-enabled.

---

## 12. Item 11 — Tests still missing

- **`test_insert_ds` (Gate G2):**
  - **Comparison:** the frozen P2 post-chain plus the mixer decimator,
    against the P3 split (post-chain gain + `D+S` stage).
  - **Expected:** 0 differing samples in steady state; differences only
    inside declick blocks (D9).
- **C1–C16 with audio:** extend `test_voicebuffer_cases` with real types
  (`Ech`, `Cmb`). Assert that a tail continues sample-exact from the
  active output at the change (no gap, no restart) and decays to expiry.
  Assert one share callback per apply pass.
- **Manager integration:** `InsertEffectsManager` + VoiceBuffer with real
  descriptors, covering:
  - disabled-type fallback;
  - hidden masks;
  - held retry;
  - downgrade (C13).

---

## 13. Item 12 — Loose ends

- **Stale engine field:** the engine `insertType` field is written by the
  MIX_FX_TYPE writer but never read. After a held selection completes in
  `insertFx_service()` it is stale. Either drop the field (it backs the
  descriptor binding, so keep it but refresh it from the manager on
  completion) or document it as "requested, not applied".
- **Labels:** the label wording per type (P3 §15 item 5).
- **`D+S` and `amt`:** `D+S` has no `amt` control (`---`). Decide whether
  it becomes a wet mix (P3 §15 item 6).

---

## 14. What should stay out of P4

- **New insert types:** none until item 10's models exist for the current
  set.
- **More runners or lease units:** the floor and the D2 approval depend on
  16 positions. Only revisit if item 2 shows CPU headroom and you want
  fewer Holds.
- **Float rings:** they double the RAM per unit; not approved.
