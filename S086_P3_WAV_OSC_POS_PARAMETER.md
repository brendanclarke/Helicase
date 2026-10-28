# S086 P3 — The `pos` parameter on the first oscillator page (plan)

**Session:** 086, phase P3 (the rest of stage S1 in
`S086_P1_OSC_TYPE_EXPANSION.md`, without `typ`)
**Branch:** `dev-ph7-voice_expansion`, after S086 P2 (Re3/Sw3; uncommitted)
**Written:** 2026-10-27; **revised 2026-10-28**: `pos` is now continuous
(user), §2.3–§2.5.
**Status:** reviewed (user, 2026-10-28). Every recommendation in §9 is
accepted. Q2 is split: `Cym` is planned in `S086_P4_SAMPLE_CYM_POS.md`
(implemented together with this step), and the v1 user samples go to S2.
No code yet.

---

## 0. Summary

**What:** add one row, `pos` (`Oscilltr` / `Position`), as the **fourth cell
of the first oscillator screen** on every instrument:

| Instrument | Page 1, screen 1, today | After |
|---|---|---|
| Drum | `coa fin wav –` | `coa fin wav pos` |
| Cymbal | `coa fin wav –` | `coa fin wav pos` |
| HiHat | `coa fin wav –` | `coa fin wav pos` |
| Snare | `coa fin noi mix` / `wav` | `coa fin wav pos` / `noi mix` (Q3) |

**What it does with today's waveforms:** `pos` is a **continuous phase
offset** on the main oscillator (`osc1`), in 1/128ths of a cycle.
- **At a trigger,** the oscillator starts at today's start phase + `pos`.
- **While it plays,** a change of `pos` (knob, LFO, velocity, automation,
  Morph) moves the play position by the change. The move is spread evenly
  over the next audio block, so the phase glides and never jumps (§2.3).
- **`pos` 0 and an unchanging `pos`** add nothing per block. `pos` 0 is bit
  for bit today's sound, and old kits load 0.
- **It acts on the periodic waves `Sin Tri Saw Rec Re3 Sw3`,** and on
  `Cym` by the same rule (`S086_P4_SAMPLE_CYM_POS.md`: `pos`/128 of the
  crash sample's length).
- **It does nothing on `Noi`, or on the v1 user samples** until S2 (Q2).

**What it will do later:** the same byte becomes the type's control:
- `smp`/`slp`: the start position, with the same continuous rule in sample
  units;
- `wtb`: the position across the frames;
- possibly the `pwm` width.

That is why it is stored as a plain 0–127 byte (§3).

**Cost:**
- **No change to `Oscillator.c` or any per-sample loop.**
- **At a trigger:** a shift and an add.
- **Per block, per `osc1`:** a few instructions. The phase increment is
  bent for the block and restored afterwards.
- **No RAM:** two bytes in existing `OscInfo` padding.
- **Four descriptor rows,** appended, so existing IDs are unchanged.

---

## 1. What the S086 plan says about `pos`

From `S086_P1_OSC_TYPE_EXPANSION.md`:

| Where | What it says |
|---|---|
| §3.1 | `pos` (`Oscilltr` `Position`) is the continuous control, 0–127. For `smp`/`slp` it is the start position, for `wtb` the position across the frames, and possibly the phase offset for the stock waves (Q2). It replaces the rollback's `mod`/`src` row, so `mod` means only the Drum mod mode. |
| §3.2 | The user rule: the first oscillator screen's fourth cell is `pos`. The modulators' `pos` cells sit on the FM / Mod Osc pages' second screen (with `typ`). |
| §3.3 | For stock waves, `pos` adds `pos`/128 × 2³² to today's start phase. Default 0 = today. **Trigger time only.** It acts only where the engine resets the oscillator, and does nothing under the S087 `fre` run mode. |
| §3.4 | New rows are appended; a missing key loads 0. Key `oscN_position` (Q4). `pos` is an ordinary morphable row: LFO and velocity target, automatable. |
| §4.2 / §4.3 | `smp`/`slp`: `pos` = start position (linear 0..127), with `slp` looping over a fixed region (Q7). `wtb`: `pos` = position across the frames (Q8), blending through a pool slot. |
| §4.5 | `pwm` candidate (b): `pos` selects the width-table pair. |
| §2.3 / Q5 | The Drum FM oscillator never resets on a trigger, so its `pos` would do nothing for a one-shot type. |
| Rollback display rules (`S085_POST_MORTEM_MENU.md` §3) | A position value shows as the number only. In the single-parameter view it is shown once, on the left, with no extra text. The type says what it means. |

**Changed by this plan (user, 2026-10-28):** §3.3's "trigger time only"
becomes **continuous** (§2.3). The trigger rule is kept as the start of the
continuous rule.

**This step** covers the existing waveforms, on `osc1` only. The row is
designed so that S2–S5 reuse it unchanged.

---

## 2. Behaviour with today's waveforms

### 2.1 Phase units

For the periodic waves, one cycle is 2³² of phase:
- Sine: 4,096 points at `<< 20`;
- the tables: 1,024 points at `<< 22`.

So `pos << 25` is `pos`/128 of a cycle for every periodic wave and every
octave level (Q5).

### 2.2 At a trigger

After today's start phase is set:

```
osc1.phase          += (uint32_t)pos << 25
osc1.positionApplied = pos
```

**Today's start phases** stay as they are; `pos` adds to them.

| Engine | Sine | Table waves (`osc_isTableWave()`) | Other |
|---|---|---|---|
| Drum | `1024 + (0x3FF<<20 − 1024) × offset` | `0xFF<<20 × offset` | 0 |
| Snare, Cymbal, HiHat | `0x3FF<<20 × offset` | `0xFF<<20 × offset` | 0 |

Here `offset` = 1, or 1 − transient volume in the transient's offset mode.
`pos` is added **after** that scaling: it is an absolute phase shift, and
offset mode keeps its present meaning (Q6). The hit itself never glides: it
starts exactly at start phase + `pos`.

### 2.3 While playing: the per-block bend

The oscillator plays at "free-running phase + `pos`" at every moment. Each
audio block, before `osc1` renders:

```
d    = circular difference (pos − positionApplied), in −64..+63
bend = d << 20                       /* (d << 25) / 32: exact for 32-sample blocks */
osc1.phaseInc += bend
render osc1 (whatever the mode)
osc1.phaseInc -= bend
positionApplied = pos
```

**Why a bend and not a jump.** Modulation values arrive once per block:
- the LFO is computed once per block (`instrumentManager_dispatchRuntimeLfos()`
  at the start of `mixer_calcNextSampleBlock()`);
- knob, automation, velocity and Morph values also land between blocks.

So `pos` is a staircase with one step per 32 samples. Adding a whole step to
the phase at the block boundary would be a phase jump: an audible click, or
a zipper under an LFO.

Spreading the same step evenly over the block's 32 samples is the same as
raising the phase increment by Δ/32 for that one block. Every oscillator
loop already adds `phaseInc` once per sample, so:
- no loop is edited or copied;
- there is no per-sample cost.

**What that gives:**
- **No jumps.** The phase is continuous: it glides to the new offset and
  lands exactly on it at the end of the block. The arithmetic is exact
  (`d << 20` × 32 = `d << 25`), so the offset never drifts.
- **Pitch-modulation smoothness.** The pitch envelope and a pitch LFO also
  change `phaseInc` once per block. A modulated `pos` is exactly as smooth
  as a modulated pitch is today.
- **No zipper from the 7-bit steps.** `pos` moves in 1/128-cycle steps
  (about 2.8°). Each step is now a short glide, a tiny frequency step,
  instead of a phase jump.
- **The bend is a pitch change while `pos` moves.** A changing phase offset
  is a frequency change, so an LFO on `pos` gives phase modulation, heard
  as vibrato. For example, a 4 Hz LFO with ¼-cycle depth gives about
  ±6 Hz. A held `pos` is a pure phase shift.

**Timing: no added latency.** The new value starts acting on the same
sample a pitch change would: the first sample of the block in which it
arrives. The only difference from pitch is what is being moved.
- For pitch, the frequency itself has its new value from that sample.
- For `pos`, the phase offset is what changes. A ramp starts moving at
  sample 0 and completes at sample 32. Against an (unwanted) jump at the
  block start, the ramp trails by 0 samples at the start of the block and
  32 at the end, about 16 on average (about 0.36 ms).
- Since modulation only exists once per block, this is also what a
  per-sample-smoothed modulation path would produce (linear interpolation
  between successive block values). Nothing is lost against it.

**Circular difference.** Phase wraps, so 127 → 0 is a step of +1/128 of a
cycle, not −127/128. Without the wrap, a value crossing the 0/127 edge would
bend the pitch hard for one block. In code:

```
d = ((pos − positionApplied + 64) & 127) − 64
```

**Large changes (Q11).**
- A big step arriving at once is squeezed into 0.7 ms: a fast knob turn,
  velocity at a hit, a step-automation change, a Morph jump.
- For example, 64 (half a cycle) gives a short pitch blip rather than a
  click.
- **Aliasing.** The octave table is chosen from the oscillator's frequency
  (`osc_setFreq()`), not from the bent increment. During a large bend the
  wave briefly plays faster than its table's band limit allows, so the
  0.7 ms blip also aliases. Small steps (1/128 cycle per block adds about
  11 Hz) are inaudible in this respect.
- A cap on |d| per block would turn a large change into a glide over several
  blocks, like portamento: `positionApplied` moves toward `pos` by at most
  the cap per block.
- Recommendation: build the cap as one constant, start with it off, and
  decide by ear at H3/H5.

**Backward motion.** A falling `pos` can make the bent increment smaller
than zero at low pitch. As `uint32_t` arithmetic it wraps, and the phase
correctly runs backwards for that block. The Sine and table loops index by
the top bits of the phase, so they handle it. The one reader that does not
is the sync ratio, so `snc` does not bend (§2.4).

**Restore after rendering.** `osc_setFreq()` skips its recalculation when
the frequency is unchanged (the S073 cache), so a bend left in `phaseInc`
would persist. The bend is removed after the render, in the same function.

**Waves without a phase rule** (`Noi`, v1 samples; `Cym` gets one in P4):
no bend.
`positionApplied` still follows `pos`, so switching `wav` later does not
release a stored step.

### 2.4 Per waveform and per mode

| `wav` / mode | At a trigger | While playing |
|---|---|---|
| `Sin Tri Saw Rec Re3 Sw3` | start phase + `pos` | bend |
| interpolated pair (`wav` LFO between two waves) | the base wave (`osc1.waveform`) decides | both partners render with the bent `phaseInc`. `calcPeriodicInterp*` ends the block at start + `phaseInc` × size, which matches. |
| `Noi` | no effect (no phase) | no effect |
| `Cym` | start + `pos`/128 of the sample (P4) | bend (P4) |
| v1 user samples (`wav` 8+) | no effect (S2, Q2) | no effect (S2, Q2) |
| Drum `fm` (osc1 = carrier) | as above | bend: the carrier's phase moves against the modulator's |
| Drum `rng` (osc1 = carrier) | as above | bend |
| Drum `mix` | as above | bend |
| Drum **`snc`** (osc1 = slave) | as above | **no bend (Q12).** Each master reset sets the slave's phase to the reset target, which does not contain `pos`, so the offset is erased at the first reset anyway. Also, `osc_syncRatioQ16()` reads the slave's `phaseInc`, and a bent (possibly backward) increment would distort that block's reset targets. |

**Drum `snc` and triggers:** the master clock (`DrumVoice.syncClock`) is
zeroed only at init, not on a trigger. So `pos` changes where the slave
starts relative to a free-running master, until the first reset.

### 2.5 How each modulation source behaves

The main loop order per block (`main.c`):
1. `voiceControl_processPending()` (triggers);
2. `seq_drainPendingAutomation()` (step values);
3. `mixer_calcNextSampleBlock()`, which itself dispatches the LFOs first,
   then the async voice code, then renders.

| Source | What happens |
|---|---|
| **Knob / menu edit** | the play position glides to the new value within a block; a fast turn gives a pitch blip (Q11) |
| **Velocity** | `modNode_updateValue(&velocityModulators[…])` runs inside each `*_triggerVoice()` before the phase is set, so the hit **starts exactly** at the velocity-modulated `pos` |
| **LFO** | written once per block, before rendering, so each block glides to the LFO's current value: continuous phase modulation. Depth and polarity work as on any 0–127 row. The result is **clamped** at 0 and 127, not wrapped: a large depth near an edge sticks there instead of continuing round the cycle (Q13). |
| **LFO with retrigger** | the hit resets the LFO's phase, but the trigger runs before that block's LFO write. So the hit starts at the previous value and glides to the restarted LFO's value over the first block (0.7 ms). With the trigger-only rule of the first draft, the restarted value would only have reached the next hit. |
| **Step automation** | a step's first trigger restores the Morph base (`seq_restoreAutomatedParameters()`) and fires *before* the step's values are drained. So the hit starts at the base `pos` and glides to the step's `pos` over the first block. It is not exact at sample 0 (Q14). Whether the sequencer can publish a step's values a tick before its trigger has not been traced. |
| **Morph** | the morphed byte is the value; changes glide like any other |

**Large values at the hit.** Velocity is exact at the hit. LFO-retrigger and
step-automation changes arrive one block late and are glided, so a large
change there is a 0.7 ms pitch blip at the start of the hit. The cap (Q11)
and the ordering fix (Q14) are the two ways to remove it.

### 2.6 Where it acts

| Oscillator | Reset on trigger today | `pos` (this step) |
|---|---|---|
| Drum `osc1` | yes | **yes** |
| Snare `osc1` | yes | **yes** |
| Cymbal `osc1` | yes | **yes** |
| HiHat `osc1` | yes | **yes** |
| Drum FM `modOsc` | never | no row in this step (§6) |
| Cymbal `modOsc`, `modOsc2` | yes, to phase 0 | no row in this step (§6) |
| HiHat `modOsc`, `modOsc2` | **never** | no row in this step (§6) |

**HiHat correction to the S086 plan:** S086 P1 §3.3 says the Cymbal/HiHat
modulators are reset. In the code only `Cymbal_triggerVoice()` resets them
(`CymbalVoice.c:160–161`). `HiHat_triggerVoice()` does not.

**With continuous `pos`, the modulator rows would do something even on
oscillators that never reset:**
- a **change** of `pos` is audible (phase shift or pitch bend while it
  moves);
- a **held** value is not, because their absolute phase is arbitrary.

See Q10.

---

## 3. Storage: two bytes in `OscInfo`, not `startPhase`

**S086 P1 Q2 proposed** storing the offset in `OscInfo.startPhase`, the
unused `uint32_t` that the never-called `Drum_setPhaseVoice()` writes.

**This plan proposes instead** two new bytes in `OscInfo`, placed after
`tableOffset`:
- `uint8_t position;`: the row's value (0..127);
- `uint8_t positionApplied;`: the part of `pos` already in the phase
  (§2.3). Runtime only; never stored in a file.

The reasons:

1. **Free.** `OscInfo` has 2 bytes of padding at offsets 18–19, between
   `tableOffset` (17) and `pitchMod` (20). `sizeof(OscInfo)`, `DrumVoice`
   (596 B) and `runtime_slots` (7,152 B) do not change. The build checks
   this (§7). There are 3 more bytes after `waveInterpNext`, if needed.
2. **An ordinary row.**
   - `ROW("osc1_position", "Oscilltr", "Position", "pos", DTYPE_0B127,
     MOD_0_127, osc.position, TYPE_UINT8)` is the same shape as other 0–127
     byte rows.
   - No special writer and no new `IM_SPECIAL_*` tag.
   - `check_special_tags.py` needs no new rule.
3. **Modulation works on the existing paths.**
   - LFO targets write descriptor bytes through
     `instrumentManager_writeRuntime()`.
   - Velocity uses the direct-pointer path.
     `instrumentManager_buildModulationRange()` has a range for `TYPE_UINT8`
     (the descriptor's 0..127) but **none for `TYPE_UINT32`**. A
     `startPhase` binding would therefore fall back to the legacy velocity
     shaping.
4. **The per-block rule and the later types need the byte, not a phase.**
   - The bend works on differences of `pos`.
   - `wtb` reads `pos` every block (a continuous position).
   - `smp`/`slp` turn it into sample units.

**`startPhase` and `Drum_setPhaseVoice()`** stay as they are: unused,
removed at a later clean-up (Q7).

---

## 4. Changes

### 4.1 Oscillator (`Oscillator.h`; `Oscillator.c` unchanged)

- **`OscInfo`:** `uint8_t position; uint8_t positionApplied;` after
  `tableOffset`, with comments (S086 P3: the continuous control; for
  periodic waves, a phase offset in 1/128 cycles; `positionApplied` = the
  part already in `phase`).
- **New inline helpers:**

  ```c
  /* 1 for the waves pos moves as a phase: SINE, the table waves and
  ** CRASH (its whole sample is the full 2^32 phase; P4). */
  static inline uint8_t osc_positionIsPhase(const uint8_t waveform)
  {
      return (uint8_t)(waveform == SINE || waveform == CRASH ||
                       osc_isTableWave(waveform));
  }

  /* At a trigger: the offset to add to the start phase; resets the
  ** applied part. */
  static inline uint32_t osc_positionTrigger(OscInfo* osc)
  {
      osc->positionApplied = osc->position;
      return osc_positionIsPhase(osc->waveform)
             ? ((uint32_t)osc->position << 25) : 0u;
  }

  /* Per block: the phaseInc bend that moves the phase by the change in pos
  ** over this block (circular difference; OSC_POS_BEND_CAP, Q11). The
  ** caller adds it before rendering and subtracts it after. */
  static inline int32_t osc_positionBend(OscInfo* osc)
  {
      int32_t d = (((int32_t)osc->position - (int32_t)osc->positionApplied
                    + 64) & 127) - 64;
      /* optional cap: d = clamp(d, -OSC_POS_BEND_CAP, OSC_POS_BEND_CAP) */
      osc->positionApplied = (uint8_t)((osc->positionApplied + d) & 127);
      return osc_positionIsPhase(osc->waveform)
             ? d * (int32_t)(0x2000000 / OUTPUT_DMA_SIZE) : 0;
  }
  ```

  Add `_Static_assert(0x2000000 % OUTPUT_DMA_SIZE == 0)`. Every voice
  render runs at `OUTPUT_DMA_SIZE` (`mixer.c` passes it to
  `instrumentManager_calcSlotSyncBlock()`), so the bend is exact.
- **Later types:** these helpers are where each type gives `pos` its
  meaning, at the trigger and per block.

### 4.2 Engines

**Trigger** (one added line each):
- **Where:** in `Drum_triggerVoice()`, `Snare_triggerVoice()`,
  `Cymbal_triggerVoice()` and `HiHat_triggerVoice()`, directly after the
  existing `if (SINE) … else if (osc_isTableWave) … else` start-phase
  block.
- **The line:**

  ```c
  voice->osc.phase += osc_positionTrigger(&voice->osc);
  ```

- **Drum:** the start-phase block sits inside the
  `#ifdef USE_AMP_FILTER … { … }` scope. `USE_AMP_FILTER` is not defined,
  so the block always runs. The new line goes inside the same braces.

**Block** (two added lines around the `osc1` render):
- **Where:** in `Snare_calcSyncBlockVoice()`, `Cymbal_calcSyncBlockVoice()`
  and `HiHat_calcSyncBlockVoice()`. `osc_setFreq(&voice->osc)` has already
  run in `*_calcAsyncVoice()` earlier in the same block.
- **The code:**

  ```c
  const int32_t posBend = osc_positionBend(&voice->osc);
  voice->osc.phaseInc += (uint32_t)posBend;
  calcNext…(&voice->osc, …);              /* the existing render call */
  voice->osc.phaseInc -= (uint32_t)posBend;
  ```

- **`Drum_calcVoiceSyncBlock()`:** the same, around the mode switch, which
  renders `osc1` in `fm`/`mix`/`rng` (lines 411, 451, 461). In **`snc`**
  (line 424) the bend is not added (Q12). `osc_positionBend()` is still
  called, so `positionApplied` follows `pos`.

### 4.3 Descriptor rows (appended; IDs unchanged)

| Instrument | New row (last index) | Descriptor count |
|---|---|---|
| Drum | `DRUM_PARAM_OSC1_POSITION` (39) | 39 → 40 |
| Snare | `SNARE_PARAM_OSC1_POSITION` (38) | 38 → 39 |
| Cymbal | `CYMBAL_PARAM_OSC1_POSITION` (39) | 39 → 40 |
| HiHat | `HIHAT_PARAM_OSC1_POSITION` (39) | 39 → 40 |

- **The row:**
  `ROW("osc1_position", "Oscilltr", "Position", "pos", DTYPE_0B127, MOD_0_127, osc.position, TYPE_UINT8)`.
  It uses `FLAGS_IMAGE`, so it is morphable, an LFO/velocity target and
  automatable.
- **The enum entry** goes before `*_PARAM_COUNT`, and `*_PARAM_DESCRIPTOR_COUNT`
  in each `*Parameters.h` follows. The existing `_Static_assert`s catch a
  mismatch.
- **Capacity:** `INSTRUMENT_PARAM_COUNT` is 64 per slot. The Scene images,
  Morph images and interpolation arrays are fixed at 64, so there is **no
  RAM change**.
- **IDs:** the canonical ID is slot × 64 + index. Every existing automation
  ID, LFO/velocity token, NRPN ID and MIDI Learn row keeps its value. The
  new rows get new IDs at the end.
- **Target lists:** `pos` appears as a new entry in the LFO and velocity
  destination lists (Q8).

### 4.4 Menu pages

**Drum, Cymbal, HiHat:** page 1, cell 4: `INSTRUMENT_MENU_EMPTY` →
`*_PARAM_OSC1_POSITION`.

**Snare** (Q3):

| | Cells 1–4 (screen 1) | Cells 5–8 (screen 2) |
|---|---|---|
| Today | `coa fin noi mix` | `wav` |
| After | `coa fin wav pos` | `noi mix` |

`typ` and `run` join screen 2 later (S086 P1 §3.2).

**Display:** `DTYPE_0B127`. The compact cell shows `pos` and the number.
The single-parameter view shows `Oscilltr` / `Position` and the number, the
same as any 0–127 row. This matches the rollback rule ("the number, once").
No type-dependent formatter is needed until `typ` exists.

### 4.5 Storage files and tools

- **Kits** (`.drm .snr .cym .hat`):
  - the new key `osc1_position` is written by the generic descriptor
    writer, in `[params]` and `[morph]`;
  - `positionApplied` is runtime only, not stored;
  - **old files:** `instrumentManager_resetSlot()` zeroes the slot before
    parsing, so a missing key is 0, which plays as today;
  - **old firmware** skips the unknown key (forward compatible). No format
    version change.
- **Copy/clear, Morph, Scene images:** generic over descriptors. No change.
- **Host converters** (`tools/convert_legacy_kits.py`, `lxr1_converters/`):
  no change needed, since an absent key is 0 (Q9).

---

## 5. CPU and resources

| | Effect |
|---|---|
| Per-sample oscillator loops | **unchanged**: `Oscillator.c` is not edited, so `elf_osc86_check`/`elf_loops86_check` must show nothing new against the P2 state |
| Trigger | a load, the existing table-wave compare, a shift, an add, a store: a few instructions per hit |
| Block, per `osc1` | two loads, the circular difference, a multiply/shift, add before and subtract after the render, a store: about 10 instructions per 32-sample block, about 0.3 per sample per voice, outside the loops |
| RAM | 0 (padding; checked by the build size and `runtime_slots`) |
| Flash | a few hundred bytes (4 rows, the helpers, menu cells) |
| ITCM | 0 expected (the engine block functions are not in ITCM; verify) |

**The CPU rule** (S086 P1 §1.1):
- The gate is the per-sample loops. They do not change.
- The per-block cost is reported: about 0.3 instructions per sample per
  voice, against the 40–51 per-sample loop ceilings.
- G-C (the device `cpu` reading) is run, because hardware tests are the
  gate.

---

## 6. Not in this step

| Item | Why it waits | When |
|---|---|---|
| `pos` for the modulators (Drum FM `osc2`; Cymbal/HiHat `osc2`, `osc3`) | Their page place is on the FM / Mod Osc second screen, next to `typ` (P1 §3.2). With the continuous rule a change would be audible even on the oscillators that never reset (§2.6). | with `typ` (Q10) |
| `typ` row and the type-dependent `pos` meaning | S086 P1 S1 rest / S2 | next step |
| `pos` on the v1 user samples | S2 replaces them with `smp`/`slp`, and defines the sample start and reader (Q20.12, padding); the worked-out rule is in P4 Q2 | S2 |
| `pos` on `Cym` | not deferred: P4, implemented with this step | P4 |
| Wrapping instead of clamping at 0/127 for LFO depth | needs a wrap option in the LFO descriptor adapter | Q13 |

### 6.1 What `pos` will mean per type (reference for S2–S5)

| `typ` | At a trigger | While playing | Notes |
|---|---|---|---|
| `wav` (this step) | start phase + `pos`/128 cycle | the per-block bend (§2.3) | periodic waves only; no bend in Drum `snc` |
| `smp` (S2) | start point, `pos`/128 of the sample length | the same bend in sample units: Δ`pos` × length / 128 points, spread over the block (one multiply per block) | a change plays as a brief speed-up or slow-down of the playhead (a scrub), not a skip; the playhead stops at the end and clamps at the start |
| `slp` (S2) | start point inside a fixed loop region (P1 Q7) | the same bend; the playhead wraps inside the loop once per block (compare and subtract) | the loop region does not move with `pos` |
| `wtb` (S3) | — | position across the frames (P1 Q8), read every block | not a phase offset; blending claims a pool slot, snaps when none is free (P1 Q9) |
| `pwm` (S5, if any) | — | width (candidate b: selects the width-table pair), read every block | evaluate first (P1 §4.5) |
| run mode `fre` (S087) | no reset, so no start offset | the bend still works | `itp` to be defined in S087 |

The stored byte, the row, the key and the menu cell stay as this step
creates them. Each type's meaning lives in the helpers of §4.1 and the
type's own block code.

---

## 7. Tests

**Build:**
- `sizeof(OscInfo)` unchanged; add a `_Static_assert` for it.
- `runtime_slots` 7,152 B and `DrumVoice` 596 B unchanged.
- bss unchanged.
- No new warnings.

**Host (`tools/dsp_test/`): a new `osc_pos` target.** It extracts the
helpers from `Oscillator.h` and the table loops from `Oscillator.c`, as
`osc_stock` does.

| Check | What |
|---|---|
| **P1 trigger** | `osc_positionTrigger()` for waveforms 0–15 and `pos` 0, 1, 64, 127: `pos << 25` exactly for `Sin Tri Saw Rec Cym Re3 Sw3`, 0 for the rest; `positionApplied = pos` afterwards |
| **P2 identity** | with `pos` held (any value), the bend is 0 every block, and the rendered blocks are bit-identical to rendering without the helpers |
| **P3 no drift** | random `pos` sequences over many blocks (bend added and removed around `calcWavetableOscBlock`): the final phase equals the free-running phase + `pos_final << 25` exactly (mod 2³²), and `phaseInc` is restored exactly |
| **P4 circular** | 127 → 0 bends by +1 step, 0 → 127 by −1, 0 → 64 by −64 |
| **P5 continuity** | within a bent block, successive phases differ by `phaseInc + bend` exactly (no jump at the block edge) |
| **P6 non-periodic** | `Noi`, a user sample: bend 0, `positionApplied` follows `pos` (`Cym`: P4 §4) |
| **P7 cap** (if Q11 enables it) | a step larger than the cap glides over ⌈step / cap⌉ blocks and lands exactly |

| Other target | Check |
|---|---|
| `special_tags` | 155 → 159 rows, 0 mismatches (the new rows are plain `ROW`s) |
| `osc86_identity` | `Oscillator.c/.h`: no removed or changed lines beyond P2's allowed list; `Oscillator.h` gains lines only |
| engine identity (new, small) | the four engine `.c` files against `frozen86/`: P2's `osc_isTableWave` lines, plus only added lines |
| `elf_osc86_check`, `elf_loops86_check` | the same result as after P2 (11 changed, 1 added; 47 exact / 1 renamed / 10 noise fragments): no oscillator function changes in P3 |
| `osc_mod`, `osc_stock`, `armcheck-oscmod`, the rest of the bench | unchanged |

**Break proofs:**

| Mutation | Fails |
|---|---|
| `phaseInc` not restored after the render | P3 |
| linear instead of circular difference | P4 |
| the bend applied as a jump at the block start | P5 |
| `pos << 24` at the trigger | P1 |
| the helper ignores the table waves | P1 |
| a row appended in the wrong place | the build's `_Static_assert` or `special_tags` |

**Hardware (user):**

| # | Check | Expect |
|---|---|---|
| H1 | Load an old kit | `pos` shows 0 everywhere; sound identical to before |
| H2 | Drum/Snare/Cymbal/HiHat page 1 | screen 1 is `coa fin wav pos` (Snare `noi mix` on screen 2) |
| H3 | Low-pitched `Sin`/`Saw`/`Re3`, long decay: turn `pos` slowly, then fast, while it sounds | slow: a smooth phase shift, no clicks or zipper; fast: a short pitch bend; a jump of 64: a short blip (decides Q11) |
| H4 | Sweep `pos` 0 → 127 between hits | the attack click and first half-cycle change; 64 = half a cycle; 127 ≈ 0 again |
| H5 | LFO → `pos` (slow, fast, synced; retrigger off and on) | vibrato-like phase modulation, smooth; with retrigger, the start glides to the LFO's start value within 0.7 ms |
| H6 | Velocity → `pos`; step automation of `pos` | velocity: exact start per hit; automation: starts at the base and glides to the step value within the first block (decides Q14) |
| H7 | `pos` on `Noi`, a user sample | no change (`Cym`: P4 C1–C4) |
| H8 | Morph between two `pos` values; save, reload, power-cycle | intermediate phases; `pos` restored |
| H9 | Drum `fm`/`mix`/`rng` with `pos` moving; `snc` with `pos` | `fm`/`rng`: the carrier moves against the modulator; `snc`: start shift only |
| H10 (**G-C**) | the six-slot worst-case Scene with an LFO on every `pos` | `cpu` as after P2; zero underruns |

---

## 8. Order of work

1. **Baseline:** confirm the P2 state builds; record the `elf_*86` results
   and `runtime_slots`.
2. **`Oscillator.h`:** add `position`, `positionApplied` and the three
   helpers (with `CRASH` in `osc_positionIsPhase()`, P4), plus the size and
   divisibility assertions. P4 Q4: the `crashSample` guard byte.
3. **The four engines:** the trigger line, and the bend around the `osc1`
   render (Drum: not in `snc`).
4. **The four parameter tables** (rows, enum, counts) and the menu pages,
   including the Snare page change.
5. **Bench:** `osc_pos` P1–P7 (with P4's `Cym` additions), `special_tags`,
   the identity targets, the `elf` gates, break proofs; update
   `DSP_TEST.md`.
6. **Build, image; write `S086_P3_IMPLEMENTATION.md`;** hardware H1–H10.

---

## 9. Decisions (user, 2026-10-28)

Every recommendation was accepted. Q2 was split.

| # | Question | Decision |
|---|---|---|
| **Q1** | Storage | **`uint8_t OscInfo.position`** plus the runtime `positionApplied`, both in padding; not `startPhase` (§3) |
| **Q2** | `pos` on `Cym` and the v1 user samples | **`Cym`: `S086_P4_SAMPLE_CYM_POS.md`**. Start = `pos`/128 of the sample's length (P4 Q1, user-confirmed: 127 plays the last 1/128), continuous by the same bend; implemented with P3. **v1 user samples: S2**, with the rule worked out in P4 Q2 |
| **Q3** | Snare page 1 | **`coa fin wav pos` / `noi mix`** |
| **Q4** | Key and labels | **`osc1_position`, `Oscilltr` / `Position` / `pos`** |
| **Q5** | Scaling | **`pos`/128 of a cycle** (`<< 25`) |
| **Q6** | Transient offset mode | **`pos` unscaled**, added after the offset-mode scaling |
| **Q7** | `startPhase`, `Drum_setPhaseVoice()` | **Leave** for a later clean-up |
| **Q8** | Modulation | **LFO and velocity destination** (`MOD_0_127`) |
| **Q9** | Host converters | **No change** |
| **Q10** | Modulator `pos` rows | **Defer to the `typ` step**, with the same helpers; decide the Drum FM / HiHat modulator resets there |
| **Q11** | Bend cap | **Build the cap as one constant, off at first;** decide by ear at H3/H6 (and P4 C4). A large bend is a 0.7 ms blip that also aliases (§2.3) |
| **Q12** | Drum `snc` | **No bend on the slave;** `pos` acts at the trigger only there |
| **Q13** | LFO depth at 0/127 | **Clamp** (the existing adapter); revisit if wrap is wanted |
| **Q14** | Step automation ordering | **Accept the glide in P3;** trace the sequencer's publish order, and propose a fix only if H6 shows it matters |
