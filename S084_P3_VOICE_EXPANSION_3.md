# S084 P3 — Voice Expansion 3: Elastic Effects, Voice Buffers and Tails, Insert Effects, Transient Buffers

**Session:** 084, phase P3 (general plan; a full implementation schedule is
written when P3 starts, after P2 is on hardware)
**Branch:** `dev-ph6-midi`
**Date:** 2026-10-25
**Plan of record:** `S084_P1_VOICE_EXPANSIONS.md` (r7). "Plan §x" refers to
it.
**Depends on:**

- P1: the deferred swap, the render pool, `OscSection`.
- P2: the transient type row with `buf` reserved; the catalogue.

---

## 1. Goal

- Give every voice a selectable end-of-chain effect (Mix FX) and
  buffer-based transient options.
- Tails are never cut, and no voice runs dry.
- The voice buffers and the Effect bus share the DTCM arena elastically.

---

## 2. Work packages

### P3-A Elastic Effects and the CrumpBit update (plan §5.4)

- **`FxBuffer.c/h`:**
  - **Lease units:** up to 12 positions below the regular 12, owner "tail of
    slot s".
  - **The Effect floor:** 20,608 B.
  - **Coalesced share changes:** `beginPass`/`endPass` give one
    `buffer_changed()` per apply pass, delivered before the voices write.
  - **Invariants:** regular + lease ≤ 24 units; share ≥ floor.
  - **A DEV owner dump.**
- **`EffectsManager.c/h`:**
  - the elastic contract (`buffer_min_bytes` ≤ floor; shrinks keep the
    newest audio; grows may be lazy);
  - a registry self-check.
- **`CrumpBit/CrumpBitEffect.c`, `CrumpBitParameters.c`:**
  - `buffer_min_bytes` = 20,608, preferred 70,592;
  - shrink by compacting the newest audio (≤ 70,592 B `memmove`, about
    0.16 ms, once per coalesced change);
  - grow at the write wrap (`pending_length`, +4 B inside the existing
    union);
  - a one-block crossfade of the read position when the current delay no
    longer fits (Q31);
  - sync divisions limited to the current loop;
  - the Rate row `BUFFER_DEPENDENT` with `effective_max` (Q32).
- **This lands before anything reserves voice units.**

### P3-B Voice buffer machinery (plan §5.3)

**New `Core/DSP/InsertEffects/VoiceBuffer.c/h`:**

- **Records:** one per slot per stage (transient, Mix FX), outside the
  runtime-slot union.
  - Fields: `use_id`, units, positions, `dsp_state` ≤ 48 B, parameter
    now/target, flags, energy, owner.
  - 12 active + 2 runner records ≈ 1,120 B, plus ≈ 64 B of FxBuffer lease
    bookkeeping.
  - Region: DTCM `.dtcmz` (approved, Q21).
- **Reservations:** 50/100 ms per option, at most 100 ms per voice. The
  menus hide options that do not fit. Reservations are made only at option
  apply (including the restored Scene's apply), never at boot as a blanket
  assignment.
- **Change handling:**
  - same `use_id` → continue and ramp the parameters; delay time glides
    (Q23);
  - different use → the old record becomes a tail on a lease, and fresh
    units start the new option at once.
  - Tails returning within their life are re-adopted (Q26).
  - Separate tails, not chained (Q25).
- **Decay limit** (§5.3.4):
  - feedback ceiling `g_max(d) = 10^(−d)`, with controls scaled to it (Q29);
  - reverb RT60 ≤ 3 s;
  - expiry below −80 dBFS for 50 ms or 3.5 s after the input stopped, then a
    20 ms residue fade.
- **Tail runners:** 2, budgeted always busy (≈ 1.8 %).
  - Overflow uses drain lanes (≈ 2.5 %) if the P1 baseline shows the
    headroom, otherwise hold (Q22).
- **Deferred-swap integration:** buffer records live outside the slot, so a
  P1 swap never touches a tail. The swap level is measured at the insert
  stage (output for non-buffer types, input for buffer types).
- **Proof:** with `Ech` through every case C1–C16 before any other type.

### P3-C Insert effects: Mix FX (plan §4.6, §5.8.2)

**New `Core/DSP/InsertEffects/`:**

- `InsertEffect.h` — the type contract;
- `InsertEffectsManager.c/h` — the registry, per-slot stage, option hiding,
  labels, the ×1.25 input;
- one file per type (O1: variant pairs share one file):
  - `InsertDriveSrr.c` (`D+S`);
  - `InsertFold.c` (`Fld`);
  - `InsertCrush.c` (`Crs`);
  - `InsertRing.c` (`Rng`);
  - `InsertPhaser.c` (`Pha`);
  - `InsertFlanger.c` (`Fla`);
  - `InsertChorus.c` (`Cho`);
  - `InsertEcho.c` (`Ech`, `Ec2`);
  - `InsertComb.c` (`Cmb`);
  - `InsertRoom.c` (`Rev`, `Rv2`).

**Rows:**

- `mix_fx_type` `fxt`: not automatable, not modulatable;
- `dpt` on the `instrument_drive` cell;
- `rte` on the `instrument_decimation` cell (its `SLOT_DECIMATION` binding
  becomes an engine field; the LFO/velocity supplemental destinations are
  re-pointed);
- `amt` (new).

**Moves:**

- the decimator leaves `mixer.c` for `InsertDriveSrr.c`;
- each engine's fused post-chain loses distortion to the stage;
- `D+S` must stay bit-identical to today's chain (host test).

**Caps:** every type ≤ the P1-measured decimator + distortion share
(`fpseq.py`).

### P3-D Transient buffer types (plan §4.3.2, O2)

- **New `Core/DSP/InsertEffects/Transient/`:**
  - `TransientResonator.c` (`Res`);
  - `TransientPluck.c` (`Plk`);
  - `TransientTube.c` (`Tub`);
  - `TransientFlam.c` (`Flm`);
  - `TransientStutter.c` (`Bzz`);
  - `TransientReverse.c` (`Rvs`);
  - `TransientSpring.c` (`Spr`).
- All are single-unit (50 ms).
- Placement: pre- or post-filter per option. Engines gain the missing call
  site.
- Cap ≤ 22 instructions, 0 divisions.
- The transient type `buf` becomes selectable. Automation stays inside the
  type.

### P3-E Shared helpers

- `Core/DSPAudio/DelayLine.h`: ring wrap, interpolated tap, one-pole,
  allpass, `delay_feedbackCeiling()`.
- Labels and names live in the type files (O3).

---

## 3. Descriptor budget after P3

| Instrument | After P2 | `fxt` + `amt` | After P3 (cap 64) |
|---|---:|---:|---:|
| Drum | 46 | +2 | 48 |
| Snare | 43 | +2 | 45 |
| Cymbal | 48 | +2 | 50 |
| HiHat | 48 | +2 | 50 |

---

## 4. Memory (to approve at schedule time)

| Item | Region | Estimate |
|---|---|---|
| Voice buffer records + lease bookkeeping | DTCM `.dtcmz` (approved) | ≈ 1,184 B |
| Voice units and leases | DTCM arena, elastic | 4,416 B per unit while in use |
| CrumpBit `pending_length` | existing Effect union | 0 new |
| Insert type state | inside the records (`dsp_state`) | 0 static |
| Insert effects, transient buffers, VoiceBuffer, FxBuffer, CrumpBit | program flash | ≈ 25–35 KB |

---

## 5. Risks

| Risk | Mitigation |
|---|---|
| Buffer machinery complexity (leases, runners, drains, coalescing, compaction) | One foreground owner; asserted invariants; the DEV owner dump; a host test per case C1–C16; a hardware matrix of Scene switches with ringing tails while CrumpBit runs |
| CrumpBit's delay range shrinks while voices hold buffers | D12; the Rate display shows the reachable maximum |
| Moving SRR into the engines re-routes `srt`/`rte` | `D+S` S0 test; checks of LFO/velocity on `rte` |
| Per-voice reverb quality inside the cap | Half-rate lo-fi design; the clean reverb stays on the bus |
| CPU: runners, drains, stages at their caps | The P1 baseline; `fpseq.py`; the worst-case Scene after each type |

---

## 6. Verification (outline)

- **Host:**
  - `D+S` bit-identical;
  - each type ≤ cap and within the 3 s decay limit at every control
    extreme;
  - CrumpBit keeps its newest audio across shrinks and grows;
  - every case C1–C16: tails continue to expiry, nothing dry, units and
    leases return, share ≥ floor, one share change per pass.
- **Hardware:**
  - every Mix FX and transient buffer type by ear;
  - Scene switches, Instrument loads and previews with ringing echoes and
    reverbs while CrumpBit runs (nothing cut, nothing dry, CrumpBit
    continuous);
  - the worst-case Scene CPU.

---

## 7. Open items for the P3 schedule

- Choose the overflow path (drains or hold) from the P1 baseline.
- Confirm the half-rate reverb by ear before committing `Rev`/`Rv2`.
- Specifications: `EFFECTS_BUS_REFERENCE.md` (elastic contract),
  `EFFECTS_MIXER_DSP_REFERENCE.md` (SRR moved, leases),
  `INSTRUMENTS_DSP_REFERENCE.md`, `STORAGE_SRAM_MANIFEST.md`,
  `MODULE_INTERCHANGE_SPEC.md`.
