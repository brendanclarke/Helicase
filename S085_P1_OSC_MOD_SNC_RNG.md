# S085 P1 — Drum `snc` (hard sync) and `rng` (ring mod): plan

**Session:** 085, phase P1 (planning only; no firmware code changed)
**Branch:** `dev-ph7-voice_expansion` at `1ea234ea` (code = the S084 roll
fix-up)
**Date:** 2026-10-27

**Revisions:**
- **r1:** first plan; nine open questions.
- **r2:** Q1 = (a); Q2 = no probe; Q7b = (a). New question Q10 (what `amt`
  does in `snc`).
- **r3:**
  - Q10 = sync depth; Q6b = leave the trigger as it is; Q5 =
    interpolation supported.
  - Q4, Q6, Q8 and Q9 accepted; RAM accepted.
  - A zero-crossing index for crash and user samples.
- **r4 (this revision, user):**
  - **No index for the crash:** the crash is treated as noise.
  - **P1 enables sync for the stock waveforms only.**
  - The sample and wavetable sync rules move to a future-planning section
    (§12):
    - the bitmap is generated at upload only for files the optimizer has
      flagged;
    - otherwise one-shots act as noise, loops reset when the sample loops,
      and wavetables reset when the frame loops.

**Scope:** add two values to the Drum `mod` row (`osc2_mod_type`), giving
`fm mix snc rng`. **Drum only.** Sync is enabled for the stock waveforms
`Sin Tri Saw Rec Noi Cym`. Snare, Cymbal and HiHat are not touched.

**The rule for this session (user):** there can be absolutely **no increase
in CPU cycles overall**. Gate in §5.

**Inputs:**
- `S085_OSC_MOD_CHERRYPICK.md` and `S085_P1_OSC_MOD_PLAN.md` (root): the
  cherry-pick plan written before the failed attempt.
- **The spike:** `Helicase-dev-ph7-spike/` (repo root) (`26fb7c3`).
- **The rollback:** `Helicase-dev-ph7-voice-pwm-spike/` (`4ad2f850`,
  `ef7d5445`). This is the failed S085 tree, reference only. The relevant
  documents are:
  - `S085_POST_MORTEM.md`;
  - `S085_POST_MORTEM_OSC_RUN.md`;
  - `S085_P1_IMPLEMENTATION.md` (C10.x, A2, §23).

  Also `tools/sampleoptimize.py` and `filesystem_parseWav()` (for §12).
- **The current tree:**
  - `Core/DSP/Instruments/Drum/DrumVoice.c/.h`;
  - `Core/DSP/Instruments/Drum/DrumParameters.c`;
  - `Core/DSPAudio/Oscillator.c/.h`;
  - `Core/DSPAudio/BufferTools.c`;
  - `Core/Menu/menu.c`.

**Note on the run modes.** The run modes (`rst fre itp`), the slew, the
render pool and `OscSection` exist only in the two failed trees. None of
them is in the current tree or in P1. Trigger behaviour here means the
current `Drum_triggerVoice()`, which P1 does not change.

---

## 0. Summary

- **Build `snc` and `rng` fresh against the current tree.** Both earlier
  versions cost more than Drum `fm`:

  | Mode | Rollback static count | Drum `fm` in the same tree |
  |---|---:|---:|
  | `snc` | 162 | 120 (135 with F-A) |
  | `rng` | 135 | 120 (135 with F-A) |

- **`rng`** reuses the `mix` structure:
  - the modulator render is unchanged;
  - `mix`'s saturating-add pass is replaced by one integer ring pass;
  - expected cost: slightly above `mix`, below `fm`.
- **`snc`:**
  - **Only the slave (the carrier) is heard.** The FM-page oscillator is
    the master and is **not rendered**. Its phase acts as a clock at `frq`
    and is advanced arithmetically.
  - **`amt` is the sync depth:** 0 = free-running, 127 = hard sync.
  - **P1 rules:**
    - every stock master resets the slave once per nominal period at
      `frq`; the crash is treated as noise;
    - user samples (today's `wav` tail) act as noise when they are the
      master, and render unsynced when they are the slave.
  - **Cost:** one oscillator loop plus about 10–13 per sample. That is
    below `fm`, which renders two oscillators.
- **Existing settings are unchanged:**
  - `fm`/`mix` loops: identical (§5);
  - trigger: unchanged;
  - descriptor rows, keys, automation IDs, NRPN IDs, Drum struct RAM,
    sample flash: no change.
- **Precondition:** MEMORY.md says the S084 hardware check (V1–V7) gates
  S085. That check is still open (R1).

---

## 1. General staged plan (oscillator features)

Each stage is its own change set with its own CPU check on the device.
**The CPU ceiling is the current tree's worst case.**

| Stage | Content | Ceiling |
|---|---|---|
| **P1 (this document)** | Drum `snc` and `rng` for the stock waveforms | the dearest Drum setting in any mode ≤ today's dearest Drum `fm` |
| Later (to be planned) | oscillator types (`pwm`, `smp`/`slp`, `wtb`, stock waves), the sample/wavetable re-implementation with the sync rules of §12, run modes, the render pool | per oscillator: ≤ the dearest loop it can select today, the user-sample loop plain and FM, in the voice with the most oscillators (Cymbal/HiHat, three) |

P1 keeps the `snc` structure open for those later masters: a later master
kind adds a reset-list builder, not new slave loops (§4.3.1, Q11).

---

## 2. Starting state

### 2.1 Drum oscillator section today (`DrumVoice.c:296-330`)

```
modBuf = modOsc  rendered plain, gain amt              (call 1, every mode)
mix:  buf = osc  rendered plain, gain (1 - amt)
      buf = QADD16(buf, modBuf)                        (bufferTool_addBuffersSaturating)
fm:   buf = osc  rendered FM from modBuf, gain 1       (osc.fmMod = amt x pitch EG)
```

- **The mode byte:** `DrumVoice.mixOscs` (`uint8_t`), renamed `modMode` in
  P1 (Q8). The descriptor row is `osc2_mod_type`: `DTYPE_MIX_FM`,
  `MOD_NONE`, `FLAGS_IMAGE` (morphable, automatable).
- **The mode test:** the render tests it as a boolean, so values 2 and 3
  would render as `mix` today.
- **`amt` (`osc2_mod_amount`):** `MOD_0_127`, so LFO, velocity, Morph and
  automation can all move it.
- **Pitch EG and snap EG:** both modulate only `osc`, the carrier.
- **The FM-page frequency:** `frq` (`osc2_pitch_coarse`) only; Drum has no
  osc2 fine row.
- **The modulator's phase increment is constant within a block.**
  `osc_setFreq()` runs once per block, and there is no per-sample slew.

### 2.2 Trigger (unchanged in P1, user r3)

| Oscillator | Reset on trigger? |
|---|---|
| `osc` (carrier) | yes, every trigger (`DrumVoice.c:196-200`) |
| `modOsc` (FM page) | **no**, never; it runs free |
| `startPhase` (`Drum_setPhaseVoice()`) | stored, but nothing calls the function and nothing reads the field |

The user's decision: no `modOsc` reset is added for `snc` or `rng` unless
the user tests it and asks for it (R8).

### 2.3 How each wave type's phase behaves

| `wav` | Loop | One nominal period | Increment from f | P1 role in `snc` |
|---|---|---|---|---|
| `Sin` | `calcSineBlock` | 2^32 | f/fs × 2^32 | master (clock); slave (sync) |
| `Tri Saw Rec` | `calcWavetableOscBlock` | 2^32 | f/fs × 2^32 | master (clock); slave (sync) |
| `Noi` | `calcNoiseBlock` | 2^32 | f/fs × 2^32 | master (clock); slave **unsynced** |
| `Cym` | `calcSampleOscBlock` (32,768-point, 8-bit sample; 32 KB) | 2^27 (the phase wraps every 32 periods) | f/fs × 2^27 | master **as noise** (clock, `<< 5`); slave (sync) |
| user sample (v1 tail) | `calcUserSampleOscBlock` | 2^27 | f/fs × 2^27 | master **as noise** (clock, `<< 5`); slave **unsynced** |

### 2.4 Tooling

| Tool | What it gives | Limit |
|---|---|---|
| `tools/dsp_test/fpseq.py --report --all` | per-sample instruction totals of compiled loops; `MATCH`/`DIFF` | static counts, not cycles |
| The `cpu` widget, `audioCodec_underrunCount` | whole-system load on the device | the acceptance check (user) |
| `tools/link_budget.py` | flash/ITCM/DTCM. Today: ITCM 4,168 / 16,384 B; flash headroom 143,952 B | — |

---

## 3. What the spike and the rollback did, and what P1 keeps

### 3.1 Comparison

| | Spike `26fb7c3` | Rollback (failed S085) | **P1 r4** |
|---|---|---|---|
| Master | rendered and mixed in | rendered and mixed in | **not rendered**; a phase clock |
| Where resets fall | the master's cycle wrap; a sample master at its span, by modulo | the same, via a lane; a modulo on every sample | the clock wrap, once per nominal period at `frq`, for every P1 master |
| How the slave applies them | **one renderer call per wrap segment** | a lane (`wrap[32]`, `over[32]`) + shared plain/sync loops | a reset list (`over[32]`, with a sentinel) + **separate** sync copies of each slave loop |
| Reset law | hard | hard | **depth `amt`** toward `overshoot × ratio` |
| Output | `slave·(1−amt) + master·amt` | the same, as a 16-instruction float pass | slave only |
| `rng` | float pass | float pass, 19 instructions | integer pass on the already-gained `modBuf` |
| Plain loops | untouched | +2 per sample on `pwm`/`smp`/`wtb` | **untouched** |
| Cost | over the cap (A2) | `snc` 162, `rng` 135 vs `fm` 120 | below `fm` (§4.4) |

### 3.2 Carried over

| ID | Decision | Source |
|---|---|---|
| K1 | Stored values: 0 `fm`, 1 `mix`, 2 `snc`, 3 `rng`; display `fm mix snc rng` | cherry-pick §6 |
| K2 | `snc`: `modOsc` (FM page) is the master; `osc` (carrier) is the slave | spike, rollback |
| K4 | The slave renders through sample `i`, and the reset lands on `i + 1` | rollback D13 |
| K5 | `rng`: `car·(1−amt) + car·mod·amt` | rollback |
| K6 | Constant cost: reset arithmetic every sample, select by flag | rollback A2 |

**Replaced:**
- **K3** (the `snc` mix law) is replaced by the depth law (Q10).
- **K7** ("no resets after a one-shot master ends") moves to §12. It does
  not arise in P1, because sample masters act as noise.

### 3.3 Left behind

- `OscSection`, the run modes, the slew, the F-A ramp, the render pool.
- Shared plain/sync loops.
- The run-time sample-cycle modulo.
- The master render in `snc`.
- The r3 crash crossing table (user r4).

---

## 4. P1 design

### 4.1 Dispatch (`Drum_calcVoiceSyncBlock()`)

```
switch (modMode)
  fm  (0, default): unchanged
  mix (1):          unchanged
  rng (3): modBuf = modOsc rendered plain, gain amt       (call 1, unchanged)
           buf    = osc rendered plain, gain 1
           buf    = ring(buf, modBuf, K = (1 - amt) x 32768)
  snc (2): over[] = reset list for this block from the modOsc clock (§4.3.1)
           buf    = osc rendered SYNC from over[], depth amt, gain 1
           modOsc.phase += size x modOsc.phaseInc          (as its render would have)
```

### 4.2 `rng`

- **Law:** `buf[i] = SSAT16((buf[i] × (K + modBuf[i])) >> 15)`, where
  `modBuf = mod × amt` (call 1, unchanged) and `K = round(32768 × (1 −
  amt))`.
  - This equals K5.
  - At `amt` 0 the output is the carrier exactly.
  - At `amt` 1 the output is the pure ring product. The SSAT catches the
    single (−32768)² case.
- **Where:** `bufferTool_ringBlock()` in `BufferTools.c`. It uses the
  packed (`SMULBB`/`SMULTT`) form or the scalar form, whichever
  `fpseq --report` counts lower.
- **Types:** all `wav` values, including user samples, through the existing
  plain renders.

### 4.3 `snc`

#### 4.3.1 The reset list (built once per block)

`over[i]` holds the clock's overshoot past its wrap after sample `i`, or
the sentinel `0xFFFFFFFF` for "no reset". It is 32 × 4 = 128 B on the
stack.

**P1 has one builder, the clock:**

```
scale = (modOsc.waveform >= CRASH) ? 5 : 0     (crash and user samples act as noise: one wrap per nominal period)
clock = modOsc.phase << scale;  inc = modOsc.phaseInc << scale
for i:  next = clock + inc            (ADDS)
        over[i] = carry ? next : NONE
        clock = next
```

- **Noise and noise-like masters wrap once per nominal period at `frq`:**
  - `Noi`, from its own phase;
  - `Cym` and user samples, from their phase `<< 5`.

  The pattern the master's audio would make is not used (user r4).
- **Why a list rather than testing the carry inside the slave loop.** The
  in-loop carry would cost about 2 fewer instructions per sample. But the
  later master kinds need different detection, so each would then need its
  own copy of every slave loop:
  - looped samples reset when the loop wraps;
  - wavetables reset when the frame wraps;
  - bitmap crossings (§12).

  With the list, each of those adds only a builder (Q11).

#### 4.3.2 The slave sync loop (a copy of each plain loop)

```
for i:
    out        = read(slavePhase)                   (unchanged)
    buf[i]     = out × gain                         (unchanged)
    slavePhase += slaveInc                          (unchanged)
    o          = over[i]
    target     = o × ratioQ16 >> 16                 (overshoot correction, Q4)
    pulled     = slavePhase + depth × (int32)(target − slavePhase)   (SMMUL + add)
    slavePhase = (o != NONE) ? pulled : slavePhase  (K4, K6)
```

- **Ratio:** `slaveInc / inc` (the scaled clock increment), once per block.
  If `inc` = 0, ratio = 0 and the builder produces no resets.
  - `o < inc`, so `target < slaveInc`, with no overflow.
  - The signed difference takes the short way round the phase circle.
- **Depth:** `amt` maps to Q31.
  - `amt` 0: bit-identical to the plain loop.
  - `amt` 127: within 1 phase LSB of the target (hard sync).
- **Copies to write:**

  | Slave `wav` | Sync copy |
  |---|---|
  | `Sin` | `calcSineSyncBlock` |
  | `Tri Saw Rec` | `calcWavetableOscSyncBlock` (the table is an argument) |
  | `Cym` | `calcSampleOscSyncBlock` (a reset restarts the cymbal read) |
  | `Noi` | none: renders plain (the FM precedent) |
  | user sample | none in P1: renders plain (user r4; §12) |
  | waveform interpolation | `calcPeriodicInterpSyncBlock`: two renders through the sync dispatcher from the same list + the existing blend (Q5). A user-sample partner renders plain. |

- **Placement:** the dispatcher `calcNextOscSampleSyncBlock()`, the
  builder, the sync copies and the ring pass go in ITCM (Q6). The existing
  loops are not edited.

#### 4.3.3 How it sounds

- **`amt` 127:** hard sync. Sweep the slave (coarse or pitch EG) against
  `frq`.
- **Intermediate `amt`:** a continuous move from the free slave tone to
  the hard-sync tone. An LFO or velocity on `amt` sweeps the sync.
- **The FM-page `wav` has no audible effect in P1 `snc`.** Every master is
  a clock at `frq` (R7).

### 4.4 Cost estimate (static, per sample; supporting evidence only)

| Mode | Composition | Against `fm` (M_plain + C_fm) |
|---|---|---|
| `mix` | M_plain + C_plain + QADD (≈ 2–3) | lower |
| `rng` | M_plain + C_plain + ring (≈ 4–7) | lower: C_fm − C_plain is about 8–11 for tables and crash. The sine and user-sample margins are confirmed by `fpseq` (R4) |
| `snc` | builder (≈ 4–6) + C_sync (C_plain + 5–7); per block, one divide (≈ 0.5) | lower by about M_plain − 12 |

The dearest P1 `snc` is a crash slave at about 30 + 13 static. The dearest
Drum `fm` (user-sample modulator + user-sample FM carrier) stays the
ceiling. Gate: Q1 (a); the per-same-waves comparison is reported.

### 4.5 Menu, automation, MIDI, Morph

| Site | Change |
|---|---|
| `MenuText.h` | a 4-entry `mixFmNames` (`fm mix snc rng`) |
| `menu.c` `DTYPE_MIX_FM`, six sites (3327, 6397, 6655, 12917, 12958, 14028) | names for 2 and 3; clamp `> 3 → 3`; automation max 3 |
| MIDI | clamps to the cell domain (`MIDI_SPEC.md` §5.5): CC/NRPN 2 becomes `snc`, 3–127 become `rng` (today 2–127 give `mix`). Documented (R5). |
| Morph | stays morphable (Q7b): `fm` → `rng` steps through `mix` and `snc` |
| `check_special_tags.py`, converters | unchanged |

### 4.6 Stored data and compatibility

- No new rows, keys, automation IDs, NRPN IDs or Morph slots.
- Old kits load with the same meaning.
- No sample-flash change.

---

## 5. The CPU gate

| Gate | Who / how | Pass |
|---|---|---|
| G-A **fm/mix unchanged** | build-time: `objdump -d` of every existing `calc*Block` function (all `constprop` clones) and of `Drum_calcVoiceSyncBlock`, before and after; `fpseq.py` `MATCH`; host output bit-identical against a frozen copy | identical apart from addresses and veneers |
| G-B **static** | build-time: `fpseq --report --all` on the builder, the sync copies and the ring pass, composed as in §4.4 | (a) holds with margin; per-same-waves reported |
| G-C **device (user)** | six Drum slots at their dearest `fm` settings (user-sample modulator and carrier, an interpolated carrier), then the same with all `snc` (crash slave, interpolated slave) and all `rng` | `cpu` no worse than the `fm` reading; zero underruns; the interface responsive |

---

## 6. Order of work

1. **Baseline:**
   - save the disassembly of the existing loops;
   - run `fpseq --report` on them (including the user-sample and sine FM
     loops);
   - the user records the six-slot `fm` `cpu` reading on the S084 build.
2. **`rng`:** ring pass, dispatch, menu values, `modMode` rename. Host
   test; G-A; G-B.
3. **`snc`:** the clock builder, the sync copies (sine, table, crash,
   interpolation), the dispatcher, depth, the dispatch. Host test; G-A;
   G-B.
4. **Device check G-C and the listening matrix (§7.3)** (user).
5. **Specifications:**
   - `INSTRUMENTS_DSP_REFERENCE.md`: the Drum section, the four modes, the
     depth law, the P1 master/slave table (§2.3), and §12 as the planned
     extension;
   - `CPU_USE_DSP_AUDIT.md`: counts and `cpu` readings;
   - `STORAGE_SRAM_MANIFEST.md`: ITCM;
   - `MODULE_INTERCHANGE_SPEC.md`: `osc2_mod_type` 0–3;
   - `MIDI_SPEC.md`: the value list, if it lists one.

---

## 7. Tests

### 7.1 Host (`tools/dsp_test`, new `test_osc_mod.c`)

| Case | Check | A break that must fail it |
|---|---|---|
| fm/mix identity | the Drum section output against a frozen copy of today's code | any edit to a plain or FM loop |
| `rng` law | `amt` 0 gives the carrier exactly; `amt` 1 gives `(car·mod)>>15` ± 1 LSB; mid values ± 2 LSB | `>> 14`; K from `amt` |
| `snc` depth 0 | bit-identical to the plain render, with resets present | the pull applied at depth 0 |
| `snc` depth 127 | the reset lands on `i + 1` at `overshoot × ratio` ± 1 LSB, for sine, table and crash slaves | reset at `i`; ratio inverted |
| `snc` depth mid | the phase moves `amt × (target − phase)` the short way round | unsigned difference |
| clock builder | one reset per nominal period for `Tri`, `Noi`, `Cym` and a user sample at the same `frq` (`<< 5` for the last two); `modOsc.phase` advances by `size × inc` | the scaling missing |
| unsynced slaves | `Noi` and user-sample slaves equal their plain renders | — |
| interpolation slave | both partners follow the same list; a user-sample partner renders plain | — |

### 7.2 Static

- G-A disassembly diff.
- G-B `fpseq --report` on every new loop.

### 7.3 Device matrix (user)

| Row | Check |
|---|---|
| H1 | `rng`: `amt` sweep, every `wav` on both oscillators |
| H2 | `snc`: `amt` 0 → 127; slave sweep (coarse, pitch EG); each stock slave; LFO on `amt` |
| H3 | `snc` with each FM-page wave at one `frq` (`Sin Tri Saw Rec Noi Cym`, a user sample): the same reset rate |
| H4 | `fm`/`mix` A/B against the S084 build: they sound the same |
| H5 | the row: names, clamps, step automation 0–3, MIDI 0–3 and 127, Morph across the row, kit save/reload |
| H6 | G-C six-slot `cpu` comparison; underruns |

---

## 8. Resource ledger (estimates; RAM approved by the user, r3)

| Resource | Change | Note |
|---|---|---|
| Drum struct | 0 B | `mixOscs` renamed `modMode`, same byte |
| Static RAM | 0 B | |
| Stack | ≈ 128 B + a few words | the reset list |
| ITCM | ≈ +1.0–1.5 KB | three sync copies + interpolation, the builder, the dispatcher, the ring pass; today 4,168 / 16,384 |
| Program flash | ≈ +2–3 KB | code + veneers; headroom 143,952 B |
| Sample flash | 0 | |

---

## 9. Risks

| # | Risk | Likelihood / impact | Mitigation |
|---|---|---|---|
| R1 | The S084 hardware check (V1–V7) is still open, and MEMORY.md says it gates S085 | Certain / medium | Run V1–V7 first, or confirm that P1 may proceed alongside it |
| R2 | Static estimates mislead again | Medium / high | G-C on the device is the acceptance test |
| R3 | LTO re-clones the existing loops once new callers exist | Medium / high | separate functions; G-A disassembly diff; `noinline` on the new loops |
| R4 | The `rng` pass exceeds the sine or user-sample `fm` margin at the same waves | Low / low | G-B; packed form; gate (a) unaffected |
| R5 | MIDI CC 2–127 on `mod` changes meaning | Certain / low | `MIDI_SPEC.md` |
| R6 | Out-of-range mode bytes now render as `fm`, not `mix` | Low / low | the clamp keeps values ≤ 3 |
| R7 | In P1 `snc` the FM-page `wav` cell has no audible effect | Certain / low | documented; §12 gives samples and wavetables their own reset patterns later |
| R8 | `modOsc` never resets on a trigger (left as is, user r3), so the first `snc` reset after a hit falls at a different place each hit | Certain / low | the user tests; a reset is a later, separately requested change |

---

## 10. Decisions and resolved ambiguities

| # | Point | Resolution |
|---|---|---|
| Q1 | What must not increase | (a): the dearest Drum setting in any mode ≤ today's dearest Drum `fm`; per-same-waves reported (r2) |
| Q2 | A cycle probe | none; the user checks on the device (r2) |
| Q3 | Sample masters | P1: act as noise. Later: §12 (r4) |
| Q4 | Overshoot correction | kept (r3) |
| Q5 | Interpolation on the `snc` slave | supported (r3) |
| Q6 | ITCM placement; RAM | ITCM; RAM accepted (r3) |
| Q6b | `modOsc` reset on a trigger | unchanged in every mode (r3); R8 |
| Q7 | Out-of-range bytes | `switch` default `fm`; clamp to 3 |
| Q7b | Morph across the row | stays morphable (r2) |
| Q8 | Rename `mixOscs` | `modMode` (r3) |
| Q9 | Later-stage ceiling | the dearest loop each oscillator can select today (r3) |
| Q10 | `amt` in `snc` | sync depth, slave only, master not rendered (r3) |
| A1 | Crash as the master | as noise: one reset per nominal period at `frq` (r4) |
| A2 | Crash as the slave | synced (a reset restarts the cymbal read). "Treat it as noise" is read as applying to the master role only; see Q12 |
| A3 | Noise as the slave | unsynced (the FM precedent) |
| A4 | User sample (v1 tail) | master: as noise; slave: unsynced (r4: stock waveforms only in P1) |
| A5 | Table waves as the master | the clock wrap; their zero crossing is once per cycle too, so a crossing source would only shift the resets by a constant phase of an unheard wave |
| A6 | Snare, Cymbal, HiHat | untouched |

---

## 11. Open questions for P1

**Q11 — The reset list, or a carry test inside the slave loop?**
- **The list (§4.3.1):**
  - about 2 instructions per sample and 128 B of stack more than an
    in-loop carry;
  - later master kinds (§12) each add only a builder.
- **The in-loop carry:**
  - the cheapest P1;
  - each later master kind then needs another copy of every slave loop,
    or this structure is replaced at that stage.
- **Recommendation: the list.** Both forms are well under `fm`, and the
  list sets the shape §12 needs.

**Q12 — Crash as the slave.** A2 reads "treat it as noise" as the master
role only.
- **Options:**
  - (a) the crash slave syncs (a reset restarts the cymbal read): a
    metallic, gated sound;
  - (b) the crash slave renders unsynced, like noise. This also saves one
    sync copy (≈ 200 B ITCM).
- **Recommendation: (a)**, unless you meant both roles.

---

## 12. Future planning: sample and wavetable sync (not in P1)

This applies when the sample (`smp`/`slp`) and wavetable (`wtb`)
re-implementation lands. **Nothing here is built in P1.** Each item becomes
a builder for the P1 reset list.

### 12.1 Rules (user r4)

| FM-page (master) source | Optimizer flag present | No flag |
|---|---|---|
| One-shot sample (`smp`) | resets at the sample's **zero crossings** (bitmap) | **as noise**: one reset per nominal period at `frq` (the P1 crash rule) |
| Looped sample (`slp`) | zero crossings (bitmap) | **reset when the sample loops** (each pass through the loop end) |
| Wavetable (`wtb`) | zero crossings (bitmap, per frame) | **reset when the frame loops** (each pass through the frame's cycle) |

### 12.2 The bitmap

- **Format:** 1 bit per point, set where an upward zero crossing starts,
  with hysteresis. It is stored after the item in sample flash: 6.25 % of a
  16-bit item. No crossings are dropped, and the position is to the
  nearest point.
- **Play time:** read the two words covering the clock's step, find the
  first set bit with `RBIT` + `CLZ`, and derive the overshoot. That is
  about 12–15 instructions per sample at constant cost. There is no search
  and no divide.
- **A step longer than 32 points per sample** looks only at the first 32
  points. Dense material resets on nearly every sample there anyway.

### 12.3 The optimizer flag: is it possible? Yes

- **Where the flag lives:** a small private RIFF chunk, e.g. `lxrO`, with a
  version byte and a flags byte (bit 0 = "generate the crossing bitmap"),
  written by `tools/sampleoptimize.py`. RIFF readers skip unknown chunks,
  so other software still plays the file.
- **Upload:** the installer's WAV walker recognises the chunk, sets a flag
  bit in the catalogue entry, and generates the bitmap after the item from
  the installed data (memory-mapped flash; no RAM buffer).
- **What it takes:**
  - **Chunk order:** the rollback's `filesystem_parseWav()` stops walking
    once it has both `fmt ` and `data`. The optimizer must write its chunk
    **before** `data`, or the walker must continue to the end of the file.
  - **The optimizer must write every file it outputs.** Today it copies
    files that need no change byte for byte, and Python's `wave` module
    cannot write custom chunks, so it needs a small RIFF writer.
  - **Wavetables:** the flag is read per frame file. The table is flagged
    when all its frames are.
  - **Catalogue v2:** an entry flag bit and the bitmap location. This
    belongs to the catalogue v2 work.
- **A stale flag is harmless.** Suppose a flagged file is edited
  afterwards, and the editor keeps unknown chunks. The bitmap is still
  computed from the data actually installed, so the flag only decides
  whether the work is done.

### 12.4 Costs to settle at that stage

- **Loop-wrap builder (`slp`, unflagged):**
  - per sample: compare the master position with the loop end and
    subtract the loop length, at constant cost;
  - one modulo per block only when the step exceeds the loop length.

  This is not the rollback's per-sample modulo.
- **Frame-wrap builder (`wtb`, unflagged):** the frame cycle is the
  oscillator's phase wrap, the same as the P1 clock.
- **Bitmap builder:** §12.2. With interpolated frames, the bitmap of the
  nearer frame is chosen once per block (to be confirmed then).
- **One-shot end:** no crossings after the last point, and the master
  position holds. With no `modOsc` trigger reset (R8), a one-shot master
  drives resets only on its first pass after selection. The same applies
  in `fm`/`mix` today. Revisit if the trigger reset is ever requested.
