# S084 P3 — Implementation Schedule: Elastic Effects, Voice Buffers and Tails, Insert Effects, Transient Buffers

**Session:** 084, voice-expansion phase 3 (schedule only; no code changed in
this turn)
**Branch:** `dev-ph6-midi`
**Date:** 2026-10-26
**Plan of record:** `S084_P1_VOICE_EXPANSIONS.md` (r7). "Plan §x" refers to
it.
**General plan for this phase:** `S084_P3_VOICE_EXPANSION_3.md`. "P3 §x"
refers to it.
**Depends on:**

- `S084_P1_IMPLEMENTATION.md`: the deferred swap, the slot level witness and
  declick, `OscSection`, the selector pre-pass;
- `S084_P2_IMPLEMENTATION.md`, including its §17 review fixes: the `trt`
  row with `buf` reserved, `DriveShaper`, the catalogue, `armcheck-vx2`.

P3 starts after P2's hardware checks X1–X8.

---

## 0. Read this first

### 0.1 Scope of P3

| WP | Work package | Plan / P3 |
|---|---|---|
| WP0 | Baselines: the `D+S` share (distortion + decimator) in instructions, the worst-case Scene CPU, sizes, the arena | P3-C caps, §5.3.5 |
| WP1 | `FxBuffer`: 16 unit positions (12 regular, 4 lease), owner tags, adjacent pairs, apply passes with one coalesced share change, the computed Effect floor, the DEV owner dump | P3-A, plan §5.3.2 |
| WP2 | `EffectsManager` elastic contract and self-check; CrumpBit elastic loop (compaction on shrink, growth at the wrap, read crossfade, loop-limited sync, Rate minimum) | P3-A, plan §5.4 |
| WP3 | `DelayLine.h` (new) and `InsertEffect.h` (new): the shared buffer primitives and the insert type contract | P3-E, plan §5.8.2 |
| WP4 | `VoiceBuffer.c/h` (new): records in DTCM, use ids, reservations, tails, 2 runners, Hold, re-adoption, the decay limit and expiry, passes | P3-B, plan §5.3 |
| WP5 | `InsertEffectsManager.c/h` (new): the Mix FX and transient `buf` registries, per-slot stage, selection, fit and hiding queries, labels, the ×1.25 input | P3-C, plan §5.8.2 |
| WP6 | Mix FX types: `D+S Fld Crs Rng Pha Fla Cho Ech Ec2 Cmb Rev Rv2` (10 files) | P3-C, plan §4.6 |
| WP7 | Integration: the stage call in InstrumentManager, the post-chain split (distortion leaves the engines), the decimator leaves the mixer, the swap witness at the stage, the engine slot field | P3-C, plan §5.7 |
| WP8 | Transient `buf` types: `Res Plk Tub Flm Bzz Rvs Spr` (7 files), the engines' pre/post call sites, `trt` = `buf` selectable | P3-D, plan §4.3.2 |
| WP9 | Descriptor rows (`fxt`, `amt`; `drv` → `dpt`, `srt` → `rte`), writers, Preset apply passes | P3 §3 |
| WP10 | Menu: labels by type, option hiding, cell formats, names | plan §5.2, §5.3.1 |
| WP11 | Tools and host tests, including the C1–C16 case runner | P3 §6 |
| WP12 | Specification updates | P3 §7 |

### 0.2 Line numbers

- **Baseline:** the working tree on 2026-10-26, after
  `S084_P2_IMPLEMENTATION.md` §17. Every change also names its anchor (a
  function, a define, or quoted text); re-find the anchor before editing.
- **Order:** apply changes within a file from the bottom up.

### 0.3 Change IDs and comment blocks

Tag **`S084 VX3`**, change IDs `VX3-<WP>.<n>`. This keeps it apart from
"S084 P2/P3/P4", which the specifications already use for the roll/overlay
work (the same reason P2 used VX2).

Every change carries the project's contract block:

```
/*
 * <Name> (S084 VX3 VX3-<WP>.<n>).
 *
 * What:       ...
 * Why:        ...
 * Inputs:     ...
 * Outputs:    ...
 * Accessors:  ...
 * Affiliates: ...
 */
```

Header declarations take the same block. Removed code gets no block; the
specification records the removal.

### 0.4 Decisions to confirm before coding

The deep dive found gaps, or reasons to refine the general plan, in these
places. Each row has a recommendation, which coding follows unless you say
otherwise.

| ID | Decision | Recommendation and reason |
|---|---|---|
| D1 | **Tail overflow** (P3 §7: drains or hold). | **Hold.** Only the 2 runners carry tails. When both are busy, a slot that changes use keeps its *old* option running (wet, fed by the voice) until a runner frees, which is ≤ 3.5 s by the decay limit. Then the old record moves to the runner and the new option starts. Nothing is cut and nothing runs dry; the cost is a delayed change in a rare case. Drain lanes would need about 2.5 % CPU and 4 more records (+336 B DTCM). They can be added later behind `VOICEBUF_DRAIN_LANES` (default 0). |
| D2 | **Effect floor.** The plan's 20,608 B is the arena (126,592 B) minus 24 units. The records (1,120 B, DTCM, approved Q21) shrink the arena by the same, which would have made that floor about 19,488 B. | With D1 (Hold), at most 2 runners × 2 units = **4 lease units** exist, so the worst case is 16 units. The floor is **computed at boot: arena − 16 units ≈ 54,848 B** (arena ≈ 125,504 B after the records and the removed mixer arrays, §0.5). CrumpBit keeps the plan's minimum of 20,608 B, which is ≤ the floor, and the self-check enforces it. If drain lanes are ever enabled, the lease cap and floor are recomputed by the same formula. |
| D3 | **Unit positions.** The plan: "lease units: up to 12 positions below the regular 12". | **One space of 16 positions** (0 at the arena top). A position is free, regular (slot s), or lease (tail id t). Caps: regular ≤ 12 and ≤ 2 per slot; lease ≤ 4. Tails keep their physical units (a retag, no copy). A 2-unit option needs an **adjacent pair** (u, u + 1), which is contiguous memory, so its ring needs no split addressing. "Does not fit" means no adjacent free pair. The menu hides the option, and a load downgrades it (C13). |
| D4 | **Mix FX call site.** The plan says "the very end of each engine render". | **`instrumentManager_calcSlotSyncBlock()`, right after the engine render.** It is the same signal point. It is one site instead of four, and it knows the slot. It also lets InstrumentManager substitute the stage *input* peak for buffer types (plan §5.7) without InsertEffects including InstrumentManager (layering rule §5.8.1). |
| D5 | **`dpt` binding.** | **Unchanged:** `instrument_drive` stays bound to the engine's `distortion.shape` with the `IM_SPECIAL_INSTRUMENT_DRIVE` writer. `D+S` uses that `Distortion` directly, so it stays bit-identical under velocity and LFO modulation, which write the float through the pointer. The other types read the depth once per block as `depth = shape / (2 + shape)`, the exact inverse of `setDistortionShape()`, giving 0..127/128. |
| D6 | **`rte` binding.** The plan: "the SLOT_DECIMATION binding becomes an engine field". | **Keep `INSTRUMENT_BIND_SLOT_DECIMATION`, its installed LFO/velocity target and its morph path. Re-point only its writer:** from `mixer_decimation_rate[slot]` to `insertFx_setRate(slot, byte)`, which stores the byte and the shaped `D+S` rate per slot in the manager. The modulation machinery (`instrumentManager_applySlotDecimationTarget()` and friends) keeps working untouched, so this is lower risk than re-binding, with the same musical result. |
| D7 | **`amt` binding.** | An ordinary **engine `uint8_t` field** (`insertAmount`, an instance row with `FLAGS_IMAGE`), so Morph, LFO, velocity and automation reach it through the normal descriptor path. Its writer tag `IM_SPECIAL_MIX_FX_AMOUNT` only stores the byte; the tag exists so the label hook (D13) recognises the row by tag, like the others. |
| D8 | **`fxt` binding.** | A selector row (`ROW_SELECT_NOAUTO_SPECIAL`, P2) with a new writer, `IM_SPECIAL_MIX_FX_TYPE`, that calls `insertFx_selectMix(slot, type)`. Its member is an engine byte `insertType`, a runtime copy for diagnostics only. The selector pre-pass applies it before the Morph image. |
| D9 | **Declick order.** Today: engine (with distortion) → level witness + declick → decimator. P3: engine → stage (`D+S` = distortion + decimator) → witness + declick. | **Accept.** The declick offset is exactly 0 except for about 9 ms after a swap, so steady-state output is bit-identical. During that window the declick now acts after decimation, which is cleaner. The `D+S` S0 test runs with the declick idle. |
| D10 | **Switching between non-buffer types** (e.g. `Fld` → `Pha`): not specified. | The manager reports "type changed" and InstrumentManager **arms the existing slot declick** (P1). The new type starts from reset state, and the step is smoothed over about 1 ms at no extra cost. Buffer-type changes follow C4–C6 (tails). |
| D11 | **Engines need their slot** for the transient `buf` calls. | A new engine byte `stageSlot`, written by `instrumentManager_resetRuntimeSlotFromScene()` after `*_initVoice()`. No change to the render signatures. |
| D12 | **Transient placement.** | **Pre** = the signal entering the filter: Drum's oscillator section, Snare's noise path, the Cymbal/HiHat FM section. **Post** = right after the filter, before the post-chain (amp, velocity). The resonant types (`Res Plk Tub`) are pre; the time types (`Flm Bzz Rvs Spr`) are post. |
| D13 | **Type-dependent row labels** (plan §5.2: "the Mix FX labels"). | Two hooks in `InstrumentCellFormat`, `instrumentCellFormat_shortLabel()` and `_longLabel()`, return the active type's labels for `dpt`/`rte`/`amt`, or NULL for the descriptor's own. Menu uses them at its five label sites. |
| D14 | **CrumpBit Rate clamp** (plan: `effective_max`). A *lower* Rate is a *longer* delay, so the reachable range is a **minimum** Rate. | Add an **`effective_min`** op to `effect_type_ops_t`. `effects_service()` applies `value < min → min`. CrumpBit returns the smallest Rate whose delay fits the current loop. The plan's intent (Q32) is unchanged. |
| D15 | **"Same use".** | Same algorithm family **and** new units ≥ old units: `Ech` → `Ec2` (C3) and `Rev` → `Rv2` continue; `Ec2` → `Ech` (C4) and `Rv2` → `Rev` are different uses (tails). A non-buffer type has use id 0, and its changes follow D10. |
| D16 | **Hidden options in the menu.** | `InstrumentCellFormat` returns a 32-bit `hidden` mask. The encoder steps to the next visible value (`instrumentCellFormat_step()`, after the roll-rhythm precedent), and pots snap to the nearest visible value in `menu_clampCellValue()`. |

### 0.5 Approvals needed before coding

**DTCM (`.dtcmz`; shrinks the FxBuffer arena by the same, rounded to 32 B):**

| Item | Bytes | Owner |
|---|---:|---|
| `voiceBuffer_records[14]` (12 active: 6 slots × 2 stages; 2 runners), 80 B each | **1,120** (35 cache lines) | `VoiceBuffer.c` |
| `mixer_decimation_rate[6]`, `mixer_decimation_cnt[6]`, `mixer_voice_samples[6]` removed (`INCCMZ`) | −60 | `mixer.c` |
| **Net DTCM** | **≈ +1,060** (arena 126,592 → ≈ 125,504 B, still ≥ the 122,880 B linker minimum) | |

**SRAM1 `.bss`:**

| Item | Bytes | Owner |
|---|---:|---|
| `fxbuf_state.unit_owner` 12 → 16 positions; pass depth, pass-start share, dirty flag | +4 +6 | `FxBuffer.c` |
| `insert_slot[6]` (Mix type, `rte` byte, shaped rate, transient option, flags; 8 B) | +48 | `InsertEffectsManager.c` |
| `instrumentManager_stageInputPeak[6]` (`uint16_t`) | +12 | `InstrumentManager.c` |
| `voiceBuffer_tailScratch[32]` (`int16_t`, one tail block) | +64 | `VoiceBuffer.c` |
| **Total** | **≈ +134** | |

**Inside the runtime-slot union** (no new static RAM): `insertType`,
`insertAmount` and `stageSlot` are +3 B per engine. Drum: 1,116 → 1,119 of
1,176 B.

**ITCM** (12,720 / 16,384 B used, **3,664 B free**): P3 render code starts
in **flash** (`.text`, `-Ofast`, ART cache). After WP0/WP11 measurement the
hottest loops (`D+S`, the active `Ech`, the runner loop) may move to ITCM
inside the free budget; the placement is recorded in the log.

**Program flash** (101,844 B headroom): about +30–40 KB.

### 0.6 Constants introduced in P3 (single source of truth)

| Constant | Value | File |
|---|---|---|
| `FXBUF_POSITION_COUNT` | 16 | `FxBuffer.h` |
| `FXBUF_REGULAR_UNIT_MAX`, `FXBUF_LEASE_UNIT_MAX` | 12, 4 (`= VOICEBUF_RUNNERS × 2`) | `FxBuffer.h` |
| `FXBUF_OWNER_LEASE_BASE` | 0x40 (owner = 0x40 + tail id) | `FxBuffer.h` |
| `FXBUF_EFFECT_FLOOR_BYTES` (runtime) | arena − 16 × 4,416 (≈ 54,848 B) | `FxBuffer.c` (`fxbuf_effectFloorBytes()`) |
| `CRUMPBIT_BUFFER_MIN_BYTES`, `CRUMPBIT_BUFFER_BYTES` (pref) | 20,608, 70,592 | `CrumpBitEffect.h` |
| `VOICEBUF_RECORDS`, `VOICEBUF_ACTIVE`, `VOICEBUF_RUNNERS` | 14, 12, 2 | `VoiceBuffer.h` |
| `VOICEBUF_STAGE_MIX`, `VOICEBUF_STAGE_TRANSIENT` | 0, 1 | `VoiceBuffer.h` |
| `INSERT_STATE_BYTES` | 48 | `InsertEffect.h` |
| `VOICEBUF_SILENCE_PEAK` | 3 LSB (≈ −80 dBFS) | `VoiceBuffer.c` |
| `VOICEBUF_SILENCE_BLOCKS` | 69 (50 ms at 32 / 44,108) | `VoiceBuffer.c` |
| `VOICEBUF_TAIL_MAX_BLOCKS` | 4,824 (3.5 s) | `VoiceBuffer.c` |
| `VOICEBUF_FADE_BLOCKS` | 28 (20 ms residue fade) | `VoiceBuffer.c` |
| `INSERT_MIX_ID_*` | `D+S`=0 `Fld`=1 `Crs`=2 `Rng`=3 `Pha`=4 `Fla`=5 `Cho`=6 `Ech`=7 `Ec2`=8 `Cmb`=9 `Rev`=10 `Rv2`=11; count 12 | `InsertEffectsManager.h` |
| `INSERT_TRANSIENT_ID_*` | `Res`=0 `Plk`=1 `Tub`=2 `Flm`=3 `Bzz`=4 `Rvs`=5 `Spr`=6; count 7 | `InsertEffectsManager.h` |
| `INSERT_INPUT_GAIN` | 1.25 (every Mix type except `D+S`) | `InsertEffectsManager.c` |
| `TRANSIENT_TYPE_SELECTABLE` | 3 (P2's 2 → `buf` opens) | `transientGenerator.h` |
| `IM_SPECIAL_MIX_FX_TYPE`, `IM_SPECIAL_MIX_FX_AMOUNT` | 25, 26 (count 27 ≤ 32) | `InstrumentManager.h` |

**Stored zero is today's behaviour:**

- `fxt` 0 = `D+S`, today's drive + SRR;
- `amt` defaults 0 and `D+S` ignores it;
- `trt` 0/1 are unchanged; 2 = `buf` is new.

### 0.7 Order of work and gates

1. **WP0** baselines.
2. **WP1 → WP2** (FxBuffer, EffectsManager, CrumpBit). **Gate G1:**
   `test_fxbuf` and `test_crumpbit_elastic` pass; CrumpBit on hardware keeps
   its audio across forced shrinks and grows (DEV forced-units build). This
   lands before anything reserves voice units (P3-A).
3. **WP3 → WP4 → WP5** with **only `D+S` and `Ech`** registered, plus
   **WP7**. **Gate G2:** `D+S` is S0 against the frozen P2 chain, and the
   case runner passes C1–C16 with `Ech` (P3-B "proof").
4. **WP6** (the remaining Mix types), one at a time, each with its cost gate
   (≤ the `D+S` share measured at WP0) and its decay test. **Gate G3.**
5. **WP8** (transient `buf`). **Gate G4:** each type ≤ 22 instructions,
   0 divisions; decay ≤ 3 s.
6. **WP9/WP10** land with the types they expose; **WP11/WP12** throughout.

---

## 1. WP0 — Baselines

No code change. Record in §16 before WP1:

| Measure | How | Why |
|---|---|---|
| `D+S` share: `distortion_curveSample16` inside each `voicePost_*` loop, plus `mixer_decimateBlock` | `fpseq.py --report --all` (the `armcheck/postchain.c` and `mixer.c` units; extend with the decimator) | the cap for every Mix type (P3-C) |
| Worst-case Scene CPU | P1 WP0 procedure, with P2's worst voice (`lsw`, `wtb` FM, PCM transient) | headroom for 2 runners (≈ 1.8 %) and every stage |
| Arena bytes, `.dtcmz`, ITCM, flash, the four `sizeof`s | `link_budget.py`, the compile probe | §0.5 approvals |
| CrumpBit audible check: full-length loop at rate 0 | hardware | reference before elasticity |

---

## 2. WP1 — `FxBuffer`: positions, leases, passes, the floor

### VX3-1.1 `Core/DSP/Effects/FxBuffer.h` lines 93–104 — modify (geometry constants)

Keep `FXBUF_VOICE_SLOT_COUNT` (6), `FXBUF_VOICE_UNITS_PER_SLOT` (2),
`FXBUF_VOICE_UNIT_SAMPLES/BYTES`, `FXBUF_VOICE_RATE_HZ_DEFAULT` and
`FXBUF_UNIT_FREE`.

`FXBUF_VOICE_UNIT_COUNT` (12) keeps its meaning: the regular-unit cap and
the handoff's unit range. Add:

```c
/*
 * Unit positions, regular and lease (S084 VX3 VX3-1.1).
 *
 * What:       the arena top holds FXBUF_POSITION_COUNT unit positions
 *             (position 0 highest). A position is free, regular (owner =
 *             slot 0..5, at most FXBUF_VOICE_UNITS_PER_SLOT per slot and
 *             FXBUF_REGULAR_UNIT_MAX in total) or lease (owner =
 *             FXBUF_OWNER_LEASE_BASE + tail id, a ringing tail that left its
 *             slot; at most FXBUF_LEASE_UNIT_MAX in total). A tail keeps its
 *             physical units: a lease is a retag, never a copy.
 * Why:        plan §5.3.2, D1-D3: with the Hold overflow policy only the 2
 *             tail runners carry tails, so at most 4 lease units exist and
 *             the worst case is 16 units, not 24.
 * Inputs:     compile-time.  Outputs: the position space.
 * Accessors:  FxBuffer.c; VoiceBuffer.c (the only caller of the unit APIs).
 * Affiliates: VOICEBUF_RUNNERS (VoiceBuffer.h; asserted equal x 2).
 */
#define FXBUF_POSITION_COUNT           16u
#define FXBUF_REGULAR_UNIT_MAX         FXBUF_VOICE_UNIT_COUNT   /* 12 */
#define FXBUF_LEASE_UNIT_MAX           4u
#define FXBUF_OWNER_LEASE_BASE         0x40u
_Static_assert(FXBUF_REGULAR_UNIT_MAX + FXBUF_LEASE_UNIT_MAX ==
               FXBUF_POSITION_COUNT, "positions = regular + lease caps");
```

### VX3-1.2 `Core/DSP/Effects/FxBuffer.h` before line 284 (`#if DEV_MODE_DIAGNOSTIC`) — add the unit, pass and floor API

```c
/*
 * Effect floor (S084 VX3 VX3-1.2).
 *
 * What:       arena bytes minus FXBUF_POSITION_COUNT units: the smallest
 *             Effect share any combination of regular and lease units can
 *             leave (about 54,848 B with the P3 DTCM records, D2).
 * Why:        plan §5.4 contract: every Effect type's buffer_min_bytes must
 *             be <= this; EffectsManager's self-check enforces it.
 * Inputs:     none.  Outputs: bytes (32-aligned).
 * Accessors:  effects_registrySelfCheck(), the DEV owner dump, tests.
 */
uint32_t fxbuf_effectFloorBytes(void);

/*
 * Claim regular units for a slot (S084 VX3 VX3-1.2).
 *
 * What:       count 1 claims the free position nearest the top; count 2
 *             claims the adjacent pair (u, u + 1) nearest the top, so the
 *             two units form one contiguous ring (u + 1 lies directly below
 *             u). All-or-nothing; respects the per-slot and regular caps.
 *             The positions are returned in units_out[0..count-1], lowest
 *             address first. Inside a pass the share change is deferred to
 *             fxbuf_endPass().
 * Why:        D3: 2-unit options (Ec2, Rv2) must not need split addressing;
 *             a missing adjacent pair is "does not fit" (menu hides it, load
 *             downgrades it, C13).
 * Inputs:     slot 0..5, count 1..2, units_out (2 bytes).
 * Outputs:    1 = claimed; 0 = refused (caps or no adjacent pair).
 * Accessors:  voiceBuffer_reserve().
 * Affiliates: fxbuf_unitFits(), fxbuf_unitPtr().
 */
uint8_t fxbuf_unitAcquire(uint8_t slot, uint8_t count, uint8_t units_out[2]);

/* Would fxbuf_unitAcquire(slot, count) succeed now? (no state change) */
uint8_t fxbuf_unitFits(uint8_t slot, uint8_t count);

/*
 * Turn a slot's units into a lease, or a lease back into a slot's units
 * (S084 VX3 VX3-1.2).
 *
 * What:       retags the given positions to owner FXBUF_OWNER_LEASE_BASE +
 *             tail_id (unitToLease) or back to slot (leaseToSlot,
 *             re-adoption, C9). Respects FXBUF_LEASE_UNIT_MAX and the slot
 *             caps; all-or-nothing. Audio is untouched; the share is
 *             unchanged (positions do not move).
 * Why:        plan §5.3.5: a tail keeps ringing in the units it already
 *             holds while its slot claims fresh units for the new option.
 * Inputs:     positions, count, tail id (0..VOICEBUF_RUNNERS-1) or slot.
 * Outputs:    1 = retagged; 0 = refused (the caller holds the change, D1).
 * Accessors:  VoiceBuffer.c only.
 */
uint8_t fxbuf_unitToLease(const uint8_t *units, uint8_t count, uint8_t tail_id);
uint8_t fxbuf_leaseToSlot(const uint8_t *units, uint8_t count, uint8_t slot);

/* Free positions (regular or lease). Inside a pass the share change waits. */
void fxbuf_unitRelease(const uint8_t *units, uint8_t count);

/* 32-aligned pointer to a claimed position's 2,208 samples, or NULL. */
int16_t *fxbuf_unitPtr(uint8_t unit);

/*
 * Apply pass bracket: one coalesced share change (S084 VX3 VX3-1.2).
 *
 * What:       beginPass() records the share and increments a depth counter;
 *             acquire/release/retag inside a pass never notify; the
 *             outermost endPass() notifies the registered callback once if
 *             the share size differs from the pass start. Unbalanced
 *             endPass() calls are ignored.
 * Why:        plan §5.3.2: one apply pass (a Scene, an Instrument, a
 *             selector edit) causes one Effect share change, delivered
 *             before the voices write into newly claimed units, so CrumpBit
 *             compacts once and in time (C14).
 * Inputs:     none.  Outputs: at most one callback per outermost pass.
 * Accessors:  voiceBuffer_beginPass()/endPass() only.
 */
void fxbuf_beginPass(void);
void fxbuf_endPass(void);

#if DEV_MODE_DIAGNOSTIC
/*
 * DEV owner dump (S084 VX3 VX3-1.2).
 *
 * What:       writes one character per position into out[0..15]: '.' free,
 *             '0'..'5' regular slot, 'a'..'d' lease tail id; out[16] = 0.
 * Why:        plan §5.3 risk table: an on-device witness of the unit map for
 *             the hardware Scene-switch matrix. No storage access.
 * Accessors:  the DEV boot/diagnostic screens (VX3-4.9).
 */
void fxbuf_devOwnerMap(char out[FXBUF_POSITION_COUNT + 1u]);
#endif
```

- **Kept:** the existing `fxbuf_voiceAcquire()`, `_voiceRelease()`,
  `_voiceUnitCount()` and `_voiceUnit()`. They are now thin wrappers over
  the position API, restricted to regular units, and are used by the DEV
  self-test and `DEV_FXBUF_FORCE_VOICE_UNITS`. Their blocks gain: "Wrapper
  over the position API since S084 VX3; production callers use
  VoiceBuffer."
- **`fxbuf_unitsInUse()`:** counts regular and lease units.

### VX3-1.3 `Core/DSP/Effects/FxBuffer.c` — modify the state and helpers (lines 75–145)

- `fxbuf_state_t`:
  - `unit_owner[FXBUF_VOICE_UNIT_COUNT]` → `unit_owner[FXBUF_POSITION_COUNT]`;
  - add `uint8_t lease_in_use;`, `uint8_t pass_depth;` and
    `uint32_t pass_start_bytes;`.
- `fxbuf_unitOffset(u)` is unchanged: `bytes − (u + 1) · unit`. It is valid
  for u < 16.
- `fxbuf_shareBytes()` loops `FXBUF_POSITION_COUNT − 1 .. 0`.
- `fxbuf_notifyIfShareChanged(before)` returns immediately while
  `pass_depth > 0`.
- `fxbuf_clearOwners()` clears 16 positions and both counters.

```c
/*
 * Share notification gate (S084 VX3 VX3-1.3).
 *
 * What:       outside a pass, notify as before when the share size changed;
 *             inside a pass, do nothing (endPass compares against the pass
 *             start instead).
 * Why:        coalescing (VX3-1.2): CrumpBit compacts at most once per pass.
 */
```

### VX3-1.4 `Core/DSP/Effects/FxBuffer.c` — add the position API (after line 383, before `fxbuf_setShareChangedCallback`)

Implement VX3-1.2 with the same blocks.

- **Pair search** (`count == 2`): the lowest `u` with `u + 1 <
  FXBUF_POSITION_COUNT` and both free.
- **Lease retag:** checks `lease_in_use + count ≤ FXBUF_LEASE_UNIT_MAX`.
- **`fxbuf_effectFloorBytes()`:** `bytes − FXBUF_POSITION_COUNT ·
  FXBUF_VOICE_UNIT_BYTES`.
- **`fxbuf_init()`** (line 222): asserts at runtime (DEV) that the floor is
  ≥ the largest registered `buffer_min_bytes` (EffectsManager also checks
  this at its init).

### VX3-1.5 `Core/DSP/Effects/FxBuffer.c` line 147 — extend `fxbuf_selfTest()` (DEV)

New result codes 10–13:

| Code | Check |
|---|---|
| 10 | an adjacent pair claim returns contiguous pointers (`ptr(u+1) + 2,208 == ptr(u)`) |
| 11 | the lease cap of 4 refuses a fifth lease |
| 12 | a pass with acquire + release calls the callback at most once |
| 13 | the floor equals `bytes − 16 units` and is ≥ 20,608 |

### VX3-1.6 `Core/DSP/Effects/FxBuffer.c` line 397 `fxbuf_handoffBeginExit()` — comment only

Add to the block: "S084 VX3: the handoff describes positions 0..11
(FXBUF_VOICE_UNIT_COUNT); positions 12..15 are not represented. The share
bounds are exact. Effects never adopt voice audio, and voice tails live in
VoiceBuffer records, so this is informational only."

`sizeof(fxbuf_handoff_t)` stays 180 B (assert unchanged).

---

## 3. WP2 — Elastic Effect contract and CrumpBit

### VX3-2.1 `Core/DSP/Effects/EffectsManager.h` line 200 `effect_type_ops_t` — add `effective_min`

```c
    /*
     * Lower clamp of a buffer-dependent row (S084 VX3 VX3-2.1, D14).
     *
     * What:       optional; returns the smallest value the row may take with
     *             this share (e.g. CrumpBit Rate: lower Rate = longer delay).
     * Why:        effective_max can only cap from above; CrumpBit's reachable
     *             range under a smaller share is bounded from below.
     * Inputs:     row index, current share.  Outputs: minimum 0..255.
     * Accessors:  effects_service() (BUFFER_DEPENDENT rows only).
     */
    uint8_t (*effective_min)(uint8_t index, const fx_share_t *share);
```

Append it as the last member. StereoFilter's ops initialiser gets `NULL`.

### VX3-2.2 `Core/DSP/Effects/EffectsManager.c` line ~1949 (`effects_service()`, the BUFFER_DEPENDENT clamp) — modify

After the existing `effective_max` clamp:

```c
        /* S084 VX3 VX3-2.2: buffer-dependent lower clamp (D14). */
        if ((descriptor->effect_flags & EFFECT_PARAM_FLAG_BUFFER_DEPENDENT) != 0u &&
            entry->ops && entry->ops->effective_min) {
            const uint8_t floor_value = entry->ops->effective_min(index, &share);

            if (value < floor_value)
                value = floor_value;
        }
```

- **Display:** stored values are untouched. No accessor for the applied
  value exists today (`last_applied[]` is private, line 153), so add one:

  ```c
  /*
   * Value the active type last applied for a row (S084 VX3 VX3-2.2).
   *
   * What:       effects_state.last_applied[index] (after Morph, locks, LFO and
   *             the buffer-dependent clamps); 0 for an out-of-range index.
   * Why:        Q32 / C14: the CrumpBit Rate cell shows the reachable value
   *             while the stored value is kept for when the share grows again.
   * Inputs:     row index.  Outputs: applied value.
   * Accessors:  menuEffects.c (the CrumpBit Rate cell through its
   *             format hook, VX3-2.9).
   */
  uint8_t effects_appliedValue(uint8_t index);
  ```

### VX3-2.3 `Core/DSP/Effects/EffectsManager.c` line 67 (`effects_registry[]`, CrumpBit row) — modify

The last two fields become `CRUMPBIT_BUFFER_MIN_BYTES, CRUMPBIT_BUFFER_BYTES`
(min 20,608, pref 70,592). The row comment changes from "min = pref; the
fixed range fits the minimum share" to: "elastic since S084 VX3: min 20,608
B (<= the floor), preferred 70,592 B; shrinks keep the newest audio, grows
happen at the write wrap."

### VX3-2.4 `Core/DSP/Effects/EffectsManager.c` `effects_registrySelfCheck()` (~line 1580) — add code 11

```c
        /* S084 VX3 VX3-2.4: the elastic contract (plan §5.4). */
        if (entry->buffer_min_bytes > fxbuf_effectFloorBytes() ||
            entry->buffer_min_bytes > entry->buffer_pref_bytes)
            return 11u;
```

The boot FxBf screen shows codes ≥ 10 as `A`, `B`, … (main.c line ~371,
already supported).

### VX3-2.5 `Core/DSP/Effects/CrumpBit/CrumpBitEffect.h` lines 31–79 — modify (constants, runtime)

```c
#define CRUMPBIT_BUFFER_MIN_BYTES    20608u   /* S084 VX3: elastic minimum */
#define CRUMPBIT_BUFFER_BYTES        70592u   /* preferred (unchanged value) */
```

`CrumpBitRuntime` gains, after `primed`:

```c
    /*
     * Elastic loop state (S084 VX3 VX3-2.5).
     *
     * What:       pending_length: a larger loop length waiting for the write
     *             wrap (0 = none); xfade_wet: the last wet sample before a
     *             shrink moved the read point; xfade: 1 for the one block
     *             that crossfades from xfade_wet to the new read (Q31).
     * Why:        plan §5.4 items 2-4: no muting on share changes.
     * Writers:    crumpBit_bufferChanged(), crumpBit_process().
     */
    uint32_t pending_length;
    float    xfade_wet;
    uint8_t  xfade;
```

Update the `_Static_assert` at `CrumpBitEffect.c` line 45 to the measured
size: 56 + 9 → **68 B with padding**, ≤ 76 (the union is unchanged). The
assert text becomes "… 68 B … (S084 VX3)".

### VX3-2.6 `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c` line 161 `crumpBit_seat()` — replace with the elastic seat

```c
/*
 * Seat the loop in the current share, elastically (S084 VX3 VX3-2.6).
 *
 * What:       target = min(share bytes, CRUMPBIT_BUFFER_BYTES).
 *             - first seat (length 0): seat directly (write 0, valid 0);
 *             - shrink (target < length): crumpBit_compact() keeps the
 *               newest min(valid, target - 2) samples in chronological order
 *               at [0, kept), write_pos = kept % target, valid = kept, and if
 *               the current delay no longer fits, arms the one-block read
 *               crossfade; any pending growth is cancelled;
 *             - grow (target > length): pending_length = target; process()
 *               switches at the write wrap so the ring stays contiguous.
 * Why:        plan §5.4: shrinks keep the newest audio, grows may be lazy;
 *             the former seat muted the whole loop on any length change
 *             (C14/C15 would have cut CrumpBit on every voice reservation).
 * Inputs:     runtime, share (NULL = length 0).  Outputs: seat fields.
 * Accessors:  crumpBit_bufferChanged() (the coalesced notification, before
 *             voices write), crumpBit_process() (safety re-seat).
 * Affiliates: crumpBit_compact(), fxbuf_endPass().
 */
static uint32_t crumpBit_seat(CrumpBitRuntime *rt, const fx_share_t *share);
```

**`crumpBit_compact(rt, base, newLen)`** (static, new):

- L = length, w = `write_pos`, K = min(valid, newLen − 2).
- **No wrap** (w ≥ K): `memmove(base, base + w − K, K)`.
- **Wrap**, with a = K − w:
  1. `memmove(base + a, base, w)` (head up);
  2. `memmove(base, base + L − a, a)` (tail down).
- The second move's source [L − a, L) lies at or above newLen ≥ K, so the
  regions never overlap destructively.
- Cost ≤ 70,592 B of DTCM copy, about 0.16 ms (plan), once per coalesced
  change.

Comment block:

```c
/*
 * Keep the newest audio in a smaller loop (S084 VX3 VX3-2.6).
 *
 * What:       reorders the K newest loop bytes to [0, K) oldest first with at
 *             most two memmove()s; no scratch buffer.
 * Why:        plan §5.4 item 2: the shrinking top of the share is about to
 *             belong to a voice unit, so the newest audio moves down first.
 * Inputs:     runtime (length, write_pos, valid), base, newLen.
 * Outputs:    the reordered bytes; caller sets write_pos/valid/length.
 */
```

### VX3-2.7 `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c` line 268 `crumpBit_process()` — modify

1. **Grow at the wrap:** where `wp` wraps (`wp -= (wp >= len) ? len : 0u`),
   when `rt->pending_length > len` and `wp == len`, switch `len =
   pending_length`, `flen = (float)len`, `pending_length = 0` and continue
   writing at the old length. This branch is taken once per growth, so the
   block cost stays constant otherwise.
2. **Read crossfade (Q31):** if `rt->xfade`, the block's wet sample is
   `wet = xfade_wet + (wet − xfade_wet) · (n + 1) / frames`; it is cleared
   after the block. Every block stores the last wet sample in `xfade_wet`
   (one store).
3. **Target clamp:** `target > flen − 2` is unchanged. The glide then
   carries the delay to the new limit (tape-like, the existing
   `CRUMPBIT_GLIDE_K`).
4. **Sync divisions limited to the loop:**
   `crumpBit_targetSamples(rate, sync, bpm)` gains `max_len = min(
   CRUMPBIT_DELAY_MAX_SAMPLES, flen − 2)`, passed to
   `crumpBit_divisionFor()`, which skips divisions longer than `max_len`
   instead of the fixed maximum.

Block addition to `crumpBit_process()`:

```c
 * S084 VX3 VX3-2.7: elastic loop — growth takes effect at the write wrap
 * (pending_length), a shrink's read jump crossfades over one block, and Sync
 * only picks divisions that fit the current loop (plan §5.4 items 3-5).
```

### VX3-2.8 `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c` — `crumpBit_effectiveMin()` (new) and ops (line 430)

```c
/*
 * Smallest reachable Rate for the share (S084 VX3 VX3-2.8, D14, Q32).
 *
 * What:       for CRUMPBIT_PARAM_RATE returns ceil(127 * ln(70573 /
 *             (len - 2)) / ln 80), clamped to 0..127, where len = min(share,
 *             CRUMPBIT_BUFFER_BYTES): the lowest Rate whose free-running
 *             delay fits the loop. Other rows return 0. Control rate.
 * Why:        the Rate row shows and applies only what the loop can play.
 * Inputs:     index, share.  Outputs: minimum Rate.
 * Accessors:  effects_service() via ops->effective_min.
 */
```

`crumpBit_ops` gains `crumpBit_effectiveMin` as its last member.

### VX3-2.9 `Core/DSP/Effects/CrumpBit/CrumpBitParameters.c` line 65 (`crump_rate`) — modify

Set `effect_flags = EFFECT_PARAM_FLAG_BUFFER_DEPENDENT`. `EFFECT_ROW`
passes 0 today, so add `EFFECT_ROW_FLAGS()` in `EffectParamRows.h` with an
explicit `effect_flags` argument.

`crumpBit_uiFormatValue3()` (line 221): the Sync label calls
`crumpBit_syncDivisionFor(value, bpm, len)` with the current share length
(`fxbuf_effectShare()`, foreground), so the label shows the division the DSP
plays. `crumpBit_syncDivision()` gains the `max_len` parameter; its callers
are updated.

---

## 4. WP3 — Shared primitives and the insert type contract

### VX3-3.1 `Core/DSPAudio/DelayLine.h` (new; header only)

```c
/*
 * DelayLine — static inline building blocks for voice buffer DSP
 * (S084 VX3 VX3-3.1).
 *
 * What:       ring write and compare-and-subtract wrap; a linearly
 *             interpolated tap at a fractional delay; a one-pole low-pass
 *             (damping); a first-order allpass; delay_feedbackCeiling(d) =
 *             10^(-d) (d in seconds), the plan §5.3.4 decay rule: any loop
 *             gain at or below it falls 60 dB within 3 s. No state of its own;
 *             callers keep positions and filter memories in their dsp_state.
 * Why:        plan §5.8.3: every buffer type (Mix FX and transient) shares
 *             one tested set of primitives instead of re-deriving them.
 * Inputs:     ring pointer and length (samples), positions, coefficients.
 * Outputs:    samples, updated positions/memories.
 * Accessors:  Core/DSP/InsertEffects/ types (Fla Cho Ech Ec2 Cmb Rev Rv2,
 *             Res Plk Tub Flm Bzz Rvs Spr).
 * Affiliates: InsertEffect.h (state budget), tools/dsp_test/test_insert_types.c.
 * Building rules (DSPAudio layer, §5.8.1): engine-agnostic; no slots,
 * descriptors, FxBuffer or Menu.
 */
static inline uint32_t delay_wrap(uint32_t i, uint32_t len)
{ return (i >= len) ? (i - len) : i; }                 /* i < 2 len */

static inline void delay_write(int16_t *ring, uint32_t len, uint32_t *wp,
                               int32_t x)
{ ring[*wp] = (int16_t)__SSAT(x, 16); *wp = delay_wrap(*wp + 1u, len); }

static inline float delay_tap(const int16_t *ring, uint32_t len, uint32_t wp,
                              float d)                  /* 1 <= d <= len - 2 */
{
    float rp = (float)wp - d;
    uint32_t i0;
    float fr;

    rp += (rp < 0.0f) ? (float)len : 0.0f;
    i0 = (uint32_t)rp;
    fr = rp - (float)i0;
    return (float)ring[i0] +
           fr * (float)(ring[delay_wrap(i0 + 1u, len)] - ring[i0]);
}

static inline float delay_onePole(float *z, float x, float a)
{ *z += a * (x - *z); return *z; }

static inline float delay_allpass(float *z, float x, float g)
{ const float y = *z - g * x; *z = x + g * y; return y; }

/* 10^(-d): -60 dB in <= 3 s for any loop of delay d seconds (block rate). */
static inline float delay_feedbackCeiling(float d_seconds)
{ return expf(-2.302585093f * d_seconds); }
```

Each inline gets a one-line What/Inputs/Outputs comment in the file.

### VX3-3.2 `Core/DSP/InsertEffects/InsertEffect.h` (new)

```c
/*
 * Insert effect type contract (S084 VX3 VX3-3.2).
 *
 * What:       one const descriptor per Mix FX type and per transient buffer
 *             type: the 3-character name, the three short and long labels
 *             for dpt/rte/amt (Mix) or wav/vol/frq (transient), units 0/1/2,
 *             the use family (0 = non-buffer), placement (transients:
 *             PRE/POST filter), flags (INSERT_FLAG_INPUT_GAIN: x1.25 input;
 *             INSERT_FLAG_HALF_RATE: half-rate inner loop), and the ops:
 *             init (fresh state, ring cleared once), retarget (same-use
 *             change, e.g. Ech -> Ec2 extends at the next wrap), process
 *             (in place, one block) and tail (zero input, wet only, into
 *             a scratch block). Each type keeps its state inside the record's
 *             INSERT_STATE_BYTES (asserted in the type file).
 * Why:        plan §5.8.2: self-contained type files (O1-O3); the manager and
 *             VoiceBuffer stay type-agnostic.
 * Inputs:     insert_block_ctx_t per block (resolved, ramped controls; ring).
 * Outputs:    processed samples; state.
 * Accessors:  InsertEffectsManager.c (registries), VoiceBuffer.c (calls).
 * Affiliates: DelayLine.h, distortion.h (D+S), DriveShaper.h (Fld fold).
 */
#define INSERT_STATE_BYTES       48u
#define INSERT_FLAG_INPUT_GAIN   0x01u
#define INSERT_FLAG_HALF_RATE    0x02u
#define INSERT_PLACE_NONE        0u
#define INSERT_PLACE_PRE         1u
#define INSERT_PLACE_POST        2u

typedef struct {
    int16_t        *ring;       /* units' samples (NULL for non-buffer)   */
    uint32_t        ring_len;   /* 2,208 or 4,416                         */
    uint32_t       *wp;         /* the record's write position            */
    float           c0, c1, c2; /* ramp targets 0..1: dpt/rte/amt or
                                   vol/frq/- (transient)                  */
    float           rate_shaped;/* D+S decimator increment (manager)      */
    const Distortion *drive;    /* D+S curve (the engine's distortion)    */
    uint8_t         trigger;    /* 1 on the block after a voice trigger
                                   (Plk Bzz Rvs re-arm)                   */
} insert_block_ctx_t;

typedef struct insert_effect_type {
    const char *name3;
    const char *label3[3];
    const char *label8[3];
    uint8_t units, family, placement, flags;
    void (*init)(void *state, const insert_block_ctx_t *ctx);
    void (*retarget)(void *state, const insert_block_ctx_t *ctx,
                     uint8_t old_units);
    void (*process)(void *state, int16_t *buf, uint8_t size,
                    const insert_block_ctx_t *ctx);
    void (*tail)(void *state, int16_t *wet_out, uint8_t size,
                 const insert_block_ctx_t *ctx);
} insert_effect_type_t;
```

- **Controls arrive as targets.** `c0..c2` are the block's targets;
  VoiceBuffer keeps `param_now[3]` and ramps it linearly across the block
  (`param_now += (target − now) / size` per sample, or once per block for
  types that only need block-rate controls). Every type receives smoothed
  controls (C2).
- **Rule:** no type allocates, divides per sample, or loops on data
  (constant cost).

---

## 5. WP4 — `Core/DSP/InsertEffects/VoiceBuffer.c/h` (new)

### VX3-4.1 `VoiceBuffer.h` (new)

```c
/*
 * VoiceBuffer — per-slot, per-stage insert records: units, leases, tails,
 * runners, expiry (S084 VX3 VX3-4.1).
 *
 * What:       owns the 14 records (12 active = 6 slots x {Mix, transient};
 *             2 tail runners) in DTCM, every unit reservation (the only
 *             caller of FxBuffer's unit/lease/pass API), the use-id rule (D15),
 *             same-use continuation with parameter ramps and gliding delay
 *             (C1-C3, Q23), different-use tails on leases (C4-C6), re-adoption
 *             of a returning tail (C9, Q26), separate tails (C11, Q25), the Hold
 *             overflow policy (D1), the decay-limit expiry (-80 dBFS for 50 ms
 *             or 3.5 s, then a 20 ms residue fade; plan §5.3.4) and the apply
 *             pass bracket (one coalesced share change, plan §5.3.2).
 * Why:        plan §5.3, D12/D13: elastic reservations made only when an option
 *             is applied (never at boot), tails never cut, nothing dry.
 * Inputs:     select calls from InsertEffectsManager (foreground); render calls
 *             from the slot render (per block).
 * Outputs:    processed stage blocks with this slot's tails added; units.
 * Accessors:  InsertEffectsManager.c only (and the DEV dump).
 * Affiliates: FxBuffer.c (units, leases, passes), InsertEffect.h (types),
 *             tools/dsp_test/test_voicebuffer_cases.c (C1-C16).
 */
#define VOICEBUF_ACTIVE      12u
#define VOICEBUF_RUNNERS     2u
#define VOICEBUF_RECORDS     (VOICEBUF_ACTIVE + VOICEBUF_RUNNERS)
#define VOICEBUF_STAGE_MIX       0u
#define VOICEBUF_STAGE_TRANSIENT 1u
#define VOICEBUF_SELECT_SAME     0u   /* continued (C1/C2/C3/C8) */
#define VOICEBUF_SELECT_CHANGED  1u   /* new option running (C4-C7) */
#define VOICEBUF_SELECT_HELD     2u   /* Hold: old option keeps running (D1) */
_Static_assert(VOICEBUF_RUNNERS * 2u == FXBUF_LEASE_UNIT_MAX,
               "lease cap = 2 units per runner (D2)");

void    voiceBuffer_init(void);
void    voiceBuffer_beginPass(void);
void    voiceBuffer_endPass(void);
uint8_t voiceBuffer_select(uint8_t slot, uint8_t stage,
                           const insert_effect_type_t *type, uint8_t type_id);
uint8_t voiceBuffer_fits(uint8_t slot, uint8_t stage,
                         const insert_effect_type_t *type);
void    voiceBuffer_service(void);
void    voiceBuffer_render(uint8_t slot, uint8_t stage, int16_t *buf,
                           uint8_t size, insert_block_ctx_t *ctx);
void    voiceBuffer_addTails(uint8_t slot, int16_t *buf, uint8_t size);
#if DEV_MODE_DIAGNOSTIC
void    voiceBuffer_devDump(char row1[17], char row2[17]);
#endif
```

Per-prototype blocks:

- **`voiceBuffer_init`.**
  - **What:** marks every record free and pending none, and claims
    **nothing**.
  - **Why:** D12: reservations happen at option apply only, including the
    restored Scene's own apply, never as a blanket boot assignment.
  - **Accessors:** `insertFx_init()` from `dsp_init()`, after
    `fxbuf_init()`.
- **`voiceBuffer_beginPass` / `_endPass`:** nesting wrappers over
  `fxbuf_beginPass`/`_endPass`. `select` always brackets itself, so callers
  only bracket multi-slot applies.
- **`voiceBuffer_select`** (the change engine), for slot s, stage g and new
  type T, with record R = active[s][g]:
  1. **R holds the same use** (D15; the same type id, or the same family
     with units ≥ R's): set `R.type`. If T needs more units (C3), claim the
     position directly *above* R's unit (`unit − 1`, so the ring base is
     unchanged) and call `T.retarget`, which extends the ring at the next
     write wrap. If that position is busy, fall through to step 3. Return
     SAME.
  2. **A runner holds a tail of (s, g) whose use equals T** (C9): swap. The
     runner's units go back to slot s (`fxbuf_leaseToSlot`) and R's units
     become that runner's lease. Swap the record bytes; the old R continues
     as the tail. Return CHANGED.
  3. **Otherwise, a different use:**
     - **R has units (a buffer type):** it must become a tail. If no runner
       is free, set `R.pending_type = T` and return HELD (D1): R keeps
       processing with its input, and the change completes in
       `voiceBuffer_service()` when a runner frees. Otherwise copy R into
       the free runner, retag its units to the runner's lease
       (`fxbuf_unitToLease`) and mark it TAIL (age 0, input stopped).
     - **Then set up R for T:** claim T's units (`fxbuf_unitAcquire`; 2
       units = an adjacent pair). On refusal, a Mix 2-unit type downgrades
       to its 1-unit sibling (C13). Clear the ring once (`memset`, ≤ 8,832 B,
       foreground; P3's owner-clears rule). `T.init`. Return CHANGED. This
       covers C4, C5, C6 (to non-buffer: no units) and C7 (from non-buffer:
       fresh units, one share change).
- **`voiceBuffer_fits`:** is T selectable for (s, g) now? It applies the
  per-slot 2-unit cap across both stages (plan §5.3.1: transient `buf` hides
  `Ec2`/`Rv2` and vice versa) and `fxbuf_unitFits`, counting R's own units as
  reusable when T is the same use.
- **`voiceBuffer_service`** (foreground, once per block before the slot
  renders; VX3-7.4):
  - completes any HELD change whose runner has freed (step 3 again, in a
    pass);
  - releases expired tails' leases (in a pass).
- **`voiceBuffer_render`:**
  - ramps `param_now` toward `ctx->c0..c2` and sets `ctx->ring`/`wp` from
    R;
  - applies the ×1.25 input when `T.flags & INPUT_GAIN`;
  - calls `T.process`.
  - Non-buffer types use the same record (their state lives in
    `dsp_state`).
- **`voiceBuffer_addTails`:** for each runner owned by slot s:
  1. `T.tail` into `voiceBuffer_tailScratch`;
  2. apply the residue fade;
  3. measure the block peak for expiry: below `VOICEBUF_SILENCE_PEAK` →
     `silent_blocks++`, else 0; `age_blocks++`;
  4. add with saturation into `buf`.
  - **Fade:** when `silent_blocks ≥ 69` or `age_blocks ≥ 4,824`, fade over
    28 blocks, then mark EXPIRED (released by service).
  - **Placement:** tails of both stages are added at the end of the slot
    chain (after the Mix stage), so they follow the slot's volume, pan and
    fader. That holds even after the slot's instrument changed (C8, C12).

### VX3-4.2 `VoiceBuffer.c` (new)

- **Records:**

  ```c
  typedef struct {
      uint8_t  owner;          /* slot 0..5, 0xFF free                     */
      uint8_t  stage;          /* VOICEBUF_STAGE_*                          */
      uint8_t  type_id;        /* registry index within the stage           */
      uint8_t  flags;          /* ACTIVE TAIL FADING EXPIRED PRIMED         */
      uint8_t  units;          /* 0..2                                      */
      uint8_t  unit_pos[2];    /* FxBuffer positions, [0] = lowest address  */
      uint8_t  pending_type;   /* HELD target type id, 0xFF none            */
      uint32_t write_pos;
      uint16_t silent_blocks;
      uint16_t age_blocks;
      float    fade;
      float    param_now[3];
      uint8_t  dsp_state[INSERT_STATE_BYTES];
  } voice_buffer_record_t;     /* 80 B */
  _Static_assert(sizeof(voice_buffer_record_t) == 80u, "approved 80 B");
  static INDTCMZ voice_buffer_record_t voiceBuffer_records[VOICEBUF_RECORDS];
  static int16_t voiceBuffer_tailScratch[OUTPUT_DMA_SIZE];
  ```

  Index layout: active (s, g) = `2·s + g`; runners = 12, 13 (their tail
  ids 0, 1).
- **Type pointer resolution:** the record stores `type_id` and its stage.
  The registry arrays are owned by the manager and reached through two
  `const` getters (`insertFx_mixType()`, `insertFx_transientType()`) passed
  into `voiceBuffer_*` as `const insert_effect_type_t *`, so VoiceBuffer
  never includes Instrument headers (§5.8.1).
- **Comment blocks** on the static helpers (`vb_sameUse`, `vb_moveToRunner`,
  `vb_setupFresh`, `vb_expire`) restate the relevant C-case numbers.

### VX3-4.3 RAM and placement

- The records are DTCM (`INDTCMZ`, approved Q21); the scratch is SRAM1.
- The whole file builds with `-Ofast` under the InsertEffects pattern rule
  (VX3-7.9).
- **Render functions:** `voiceBuffer_render` and `_addTails` start in flash.
  The ITCM decision follows WP11.

### VX3-4.9 DEV owner dump (`main.c` `boot_showFxBufDiagnostic()`, line ~344)

Under `DEV_MODE_DIAGNOSTIC`, the FxBf screen's second line can be toggled to
`fxbuf_devOwnerMap()` (16 characters). `voiceBuffer_devDump()` shows per
record `o s t f` codes on a DEV page reachable from the existing diagnostic
path. Display only; no storage access (DEV_MODES rule).

---

## 6. WP5 — `Core/DSP/InsertEffects/InsertEffectsManager.c/h` (new)

### VX3-5.1 `InsertEffectsManager.h` (new)

```c
/*
 * InsertEffectsManager — Mix FX and transient buffer registries and the
 * per-slot stage (S084 VX3 VX3-5.1).
 *
 * What:       the two const registries (Mix FX: 12 rows from 10 files;
 *             transient buf: 7 rows), per-slot selection and the rte rate,
 *             fit/hidden queries for the menus, names and labels for the cell
 *             formatter, and the per-block render calls: the Mix stage at the
 *             end of a slot's render and the transient stage at the engines'
 *             pre/post-filter points.
 * Why:        plan §5.8.2; the Instrument layer hands slot and parameter
 *             values in, so InsertEffects never includes InstrumentManager,
 *             Menu or Scene data (§5.8.1).
 * Inputs:     selections (writers), per-block stage inputs.
 * Outputs:    processed blocks; the stage witness peak.
 * Accessors:  InstrumentManager.c (writers, slot render), the four engines
 *             (transient stage, trigger), InstrumentCellFormat.c (names,
 *             labels, hiding), main/dsp_init (init).
 * Affiliates: VoiceBuffer.c, the type files, InsertEffect.h.
 */
#define INSERT_MIX_COUNT        12u
#define INSERT_TRANSIENT_COUNT  7u
#define INSERT_WITNESS_OUTPUT   0xFFFFu   /* measure the stage output (non-buffer) */

typedef struct {
    const Distortion *drive;   /* engine distortion: D+S curve, dpt depth */
    uint8_t amount;            /* engine insertAmount (amt)               */
} insert_stage_in_t;

void        insertFx_init(void);
uint8_t     insertFx_selectMix(uint8_t slot, uint8_t type);
void        insertFx_selectTransient(uint8_t slot, uint8_t option, uint8_t active);
void        insertFx_setRate(uint8_t slot, uint8_t value);
uint8_t     insertFx_mixType(uint8_t slot);
uint8_t     insertFx_mixIsBuffer(uint8_t slot);
uint32_t    insertFx_mixHiddenMask(uint8_t slot);
uint32_t    insertFx_transientHiddenMask(uint8_t slot);
uint8_t     insertFx_transientBufFits(uint8_t slot);
const char *insertFx_mixName(uint8_t type);
const char *insertFx_transientName(uint8_t option);
const char *insertFx_mixLabel(uint8_t type, uint8_t row, uint8_t long_label);
uint16_t    insertFx_renderMix(uint8_t slot, int16_t *buf, uint8_t size,
                               const insert_stage_in_t *in);
void        insertFx_renderTransient(uint8_t slot, uint8_t placement,
                                     int16_t *buf, uint8_t size,
                                     float vol, float pitch);
void        insertFx_transientTrigger(uint8_t slot);
void        insertFx_service(void);
```

Per-prototype blocks:

- **`insertFx_selectMix`.**
  - **What:** clamps `type` to `INSERT_MIX_COUNT − 1`, then calls
    `voiceBuffer_select(slot, MIX, …)`.
  - **Load conflicts** (C13): `Ec2`/`Rv2` that do not fit become
    `Ech`/`Rev` before the select.
  - **Returns** 1 when a **non-buffer** type changed, so the caller arms
    the slot declick (D10). Buffer changes return 0 (their tails carry the
    sound).
- **`insertFx_selectTransient`:** with `active` (trt = `buf`), selects
  option 0..6 on the TRANSIENT stage. With `active` = 0, it selects "none",
  so a running buffer option becomes a tail (C6), and that stage's render
  does nothing.
- **`insertFx_setRate`:** stores the `rte` byte and
  `rate_shaped = valueShaperI2F(byte, −0.7f)`, the exact former
  `mixer_decimation_rate` mapping (`InstrumentManager.c` line 4240), so
  `D+S` stays S0.
- **Hidden masks:** bit t is set when type t is not selectable for the slot
  now (`voiceBuffer_fits()` false), which applies plan §5.3.1. The current
  type is never hidden.
- **`insertFx_renderMix`:**
  1. builds the block context: `c0 = depth = shape / (2 + shape)` (D5),
     `c1 = rte / 127`, `c2 = amount / 127`, `rate_shaped`, `drive`;
  2. measures the **input** peak when the type is a buffer type;
  3. calls `voiceBuffer_render`, then `voiceBuffer_addTails`.
  - **Returns** the input peak (buffer types, plan §5.7) or
    `INSERT_WITNESS_OUTPUT`.
  - **Cost:** the type's loop plus about 2 instructions per sample for the
    peak (buffer types only, constant per type).
- **`insertFx_renderTransient`:** returns unless the slot's transient
  option has this placement. It builds `c0 = vol`, `c1 = (pitch − 0.25) /
  1.5` (0..1 from P2's 0.25–1.75 factor) and `trigger` (set by
  `insertFx_transientTrigger`, cleared after one block), then calls
  `voiceBuffer_render` on the TRANSIENT stage.
- **`insertFx_service`:** forwards to `voiceBuffer_service()`, once per
  block.

### VX3-5.2 `InsertEffectsManager.c` (new)

- **Registries:**

  ```c
  static const insert_effect_type_t *const insert_mix_types[INSERT_MIX_COUNT] = {
      &insert_typeDriveSrr, &insert_typeFold, &insert_typeCrush, &insert_typeRing,
      &insert_typePhaser, &insert_typeFlanger, &insert_typeChorus,
      &insert_typeEcho1, &insert_typeEcho2, &insert_typeComb,
      &insert_typeRoom1, &insert_typeRoom2,
  };
  static const insert_effect_type_t *const
      insert_transient_types[INSERT_TRANSIENT_COUNT] = {
      &insert_typeResonator, &insert_typePluck, &insert_typeTube,
      &insert_typeFlam, &insert_typeStutter, &insert_typeReverse,
      &insert_typeSpring,
  };
  ```

  The `extern const insert_effect_type_t insert_type*` declarations live in
  this header (O3: no per-type headers).
- **Per-slot state** (SRAM1, 48 B):

  ```c
  typedef struct {
      uint8_t mix_type, transient_option, transient_active, rte;
      float   rate_shaped;
  } insert_slot_t;
  static insert_slot_t insert_slot[6];
  ```

  plus a 6-bit `insert_triggerMask`.
- **`insertFx_init()`:** every slot `D+S`, rate 1.0, transient inactive.
  It calls `voiceBuffer_init()`. It does **not** select anything into
  records: the boot Scene's selector pre-pass does that (D12).
- **Self-check** (DEV, code reported on the FxBf screen like the Effect
  registry):
  - each type's `units ≤ 2`;
  - `family != 0` iff `units > 0`;
  - Mix types have placement NONE and transients PRE/POST;
  - `name3` is 3 characters and every label is ≤ 3/8 characters;
  - each type file's own state-size assert has passed (link-time).

---

## 7. WP6 — Mix FX types (`Core/DSP/InsertEffects/`)

**Common to every file:**

- the project licence header;
- includes `InsertEffect.h`, `InsertEffectsManager.h` and `DelayLine.h`
  where needed;
- one static state struct with `_Static_assert(sizeof(state) <=
  INSERT_STATE_BYTES)`;
- the ops, and one exported `const insert_effect_type_t`.

**Rows per type** (plan §4.6 table, Q-N4): `c0` = `dpt`, `c1` = `rte`,
`c2` = `amt`, all 0..1 after the ramp. Control mapping happens at block rate
inside `process` (one evaluation per block). The feedback and decay laws
obey `delay_feedbackCeiling()` (plan §5.3.4).

**Cost gate:** every type ≤ the `D+S` share measured at WP0 (expected ≈ 30
instructions per sample), via the `armcheck-vx3` probes (VX3-11.6).

### VX3-6.1 `InsertDriveSrr.c` — `D+S` (units 0, flags 0)

- **State:** `float cnt; int16_t held;` (8 B).
- **`process`:** per sample `y = distortion_curveSample16(ctx->drive, x)`;
  then the decimator copied from `mixer_decimateBlock()` (`mixer.c`
  203–216):
  `cnt += rate_shaped; if (cnt >= 1.f) { cnt -= 1.f; held = y; } out = held`.
- **`init`:** `cnt = 0`, `held = 0` (the mixer's boot state).
- **Labels:** `drv`/`Drive`, `srt`/`SampleRt`, `---`/`-`.
- **S0 requirement:** `voicePost_*Gain` (VX3-7.1) followed by this stage is
  bit-identical to P2's fused post-chain followed by the mixer decimator
  (`test_insert_ds`).

```c
/*
 * D+S: today's voice drive and sample-rate reduction (S084 VX3 VX3-6.1).
 *
 * What:       the (1+k)x/(1+k|x|) curve of the engine's Distortion, then the
 *             sample-and-hold decimator formerly in mixer.c, in that order.
 * Why:        plan §4.6: D+S is the default Mix type and must sound exactly
 *             like the P2 chain (the S0 host test); it defines the CPU cap
 *             every other type must meet.
 * Inputs:     drive (dpt through the engine), rate_shaped (rte, manager).
 * Outputs:    the block in place.
 */
```

### VX3-6.2 `InsertFold.c` — `Fld` (0 units, INPUT_GAIN)

- **Per sample:** `u = x/32767 · (1 + 7·depth)`;
  `y = driveShaper_triFold(u + 0.6·(amt − 0.5), 1) − driveShaper_triFold(0.6·(amt − 0.5), 1)`
  (the symmetry offset removed); scale ×32767; then the `D+S` decimator on
  `rte`.
- **State** 8 B. **Labels** `fld`/`Fold`, `srt`/`SampleRt`, `sym`/`Symmetry`.
  **Estimate** ≈ 26 (plan).

### VX3-6.3 `InsertCrush.c` — `Crs` (0 units, INPUT_GAIN)

- **Block rate:** `bits = 16 − (int)(depth·12 + 0.5)` (16..4), giving
  `mask = 0xFFFF << (16 − bits)`.
- **Per sample:** `q = x & mask`, the decimator on `rte`, then
  `out = x + amt·(q − x)`.
- **State** 8 B. **Labels** `bit`/`Bits`, `srt`/`SampleRt`, `wet`/`Wet`.
  **Estimate** ≈ 22.

### VX3-6.4 `InsertRing.c` — `Rng` (0 units, INPUT_GAIN)

- **Block rate:** carrier `f = 20 · 2^(8·rte)` Hz (20 Hz–5.1 kHz),
  `inc = f · 2^32 / FS`.
- **Per sample:** `s = −sine_table[phase >> 20] / 32767`. The table is a
  −cos (P2 finding), so this starts at 0 rising after a 1,024-entry offset;
  start the phase at `1024 << 20`. Then `m = (1 − depth) + depth·s` (AM at
  low depth, ring at full depth) and `out = x + amt·(x·m − x)`.
- **State:** phase 4 B. **Labels** `dpt`/`Depth`, `frq`/`Carrier`,
  `wet`/`Wet`. **Estimate** ≈ 15.

### VX3-6.5 `InsertPhaser.c` — `Pha` (0 units, INPUT_GAIN)

- **Structure:** four first-order allpasses (`delay_allpass`) with a common
  coefficient `g`, recomputed once per block from an LFO.
  - LFO rate `0.05 · 100^rte` Hz (0.05–5 Hz), sine at block rate.
  - `g = 0.2 + 0.7·(0.5 + 0.5·lfo)`.
- **Feedback:** `fb = 0.7·depth` from the last stage's output (bounded
  < 1, so it decays in milliseconds: no buffer, inside the 3 s rule).
- **Mix:** `out = x + amt·(ap − x)·0.5`.
- **State:** 4 + 1 floats + LFO phase = 24 B. **Labels** `dpt`/`Depth`,
  `rte`/`Rate`, `wet`/`Wet`. **Estimate** ≈ 25.

### VX3-6.6 `InsertFlanger.c` — `Fla` (1 unit, INPUT_GAIN)

- **Delay:** `d = 0.5 ms + 7.5 ms · (0.5 + 0.5·lfo)`; LFO `0.05 · 60^rte` Hz.
  The delay ramps per sample from the block's start value to its end value.
- **Feedback:** `fb = depth · min(0.9, delay_feedbackCeiling(d_mean))`.
- **Per sample:** `w = delay_tap(…, d)`; `delay_write(x + fb·w)`;
  `out = x + amt·(w − x)·0.5`.
- **State:** LFO phase, `d` now, 12 B. **Labels** `fbk`/`Feedback`,
  `rte`/`Rate`, `wet`/`Wet`. **Estimate** ≈ 24.

### VX3-6.7 `InsertChorus.c` — `Cho` (1 unit, INPUT_GAIN)

- **Delay:** `d = 12 ms + depth·6 ms·lfo` (LFO `0.1 · 30^rte` Hz); no
  feedback.
- **Mix:** `out = x + amt·(tap − x)·0.5`.
- **State** 12 B. **Labels** `dpt`/`Depth`, `rte`/`Rate`, `wet`/`Wet`.
  **Estimate** ≈ 22.

### VX3-6.8 `InsertEcho.c` — `Ech` (1 unit) and `Ec2` (2 units), family ECHO (O1)

- **Time** (`rte` = time, Q-N4): `target_d = 5 ms + rte · (T_max − 5 ms)`,
  with `T_max` = 49 ms (`Ech`, 2,208 samples less the guard) or 99 ms
  (`Ec2`).
- **Glide** (Q23): the read delay glides tape-style toward `target_d` with
  a per-sample one-pole `k = 2e−3`. The glide sweeps the pitch like CrumpBit
  and never steps.
- **Feedback:** `fb = depth · delay_feedbackCeiling(target_d)` (≤ 0.891 at
  50 ms, ≤ 0.794 at 100 ms).
- **Loop:** a one-pole damping in the loop (`a` = 0.6), keeping repeats
  dark and stable; then `out = x + amt·(w − x)`.
- **`retarget`** (C3, `Ech` → `Ec2`):
  - sets `pending_len` = 4,416;
  - `process` switches the ring length when `wp` wraps at 2,208, the same
    rule as CrumpBit's growth;
  - the delay may then glide above 49 ms.
- **State:** `d_now`, damping memory, `pending_len` = 12 B. **Labels**
  `fbk`/`Feedback`, `tim`/`Time`, `wet`/`Wet`. **Estimate** ≈ 14 + the tap
  ≈ 20.
- **This is the P3-B proof type:** the case runner drives it through C1–C16
  before any other buffer type lands (Gate G2).

### VX3-6.9 `InsertComb.c` — `Cmb` (1 unit, INPUT_GAIN)

- **Pitch:** `f = 40 · 2^(5.6·rte)` Hz (40 Hz–1.94 kHz), so
  `d = FS / f` samples (22.7–1,103), computed at block rate.
- **Feedback:** `fb = depth · delay_feedbackCeiling(d / FS)`.
- **Per sample:** `w = delay_tap(…, d)`; `delay_write(x + fb·w)`;
  `out = x + amt·(w − x)`.
- **State** 8 B. **Labels** `fbk`/`Feedback`, `ptc`/`Pitch`, `wet`/`Wet`.
  **Estimate** ≈ 18.

### VX3-6.10 `InsertRoom.c` — `Rev` (1 unit) and `Rv2` (2 units), family ROOM (O1), HALF_RATE

- **Half rate:** the inner network runs on every second sample
  (22,054 Hz); the output is linearly interpolated between network outputs.
  One unit then holds 100 ms of delay.
- **Network** (the plan's "lo-fi room"): two parallel combs (lengths 0.61
  and 0.47 of the per-unit space), each with a one-pole damping (`rte` =
  damping), feeding two series allpasses (5.1 ms, 1.7 ms) carved from the
  same ring at fixed offsets.
  - `Rv2` uses the doubled space: longer combs, denser and darker.
- **Decay:** comb feedback `g_i = 10^(−3·d_i / RT60)` with
  `RT60 = 0.3 s + depth·2.7 s` (**≤ 3 s**, plan §5.3.4), computed at block
  rate with one `expf` per comb.
- **Mix:** `out = x + amt·(wet − x)`.
- **State:** 2 damping memories, 2 allpass memories, the interpolation
  sample, positions within the shared ring = 36 B. **Labels**
  `dcy`/`Decay`, `dmp`/`Damping`, `wet`/`Wet`. **Estimate** ≈ 30 at half
  rate (plan).
- **P3 §7:** confirm the half-rate sound by ear before committing `Rev`/`Rv2`
  (X4).

---

## 8. WP7 — Integration: engines, mixer, InstrumentManager

### VX3-7.1 `Core/DSPAudio/voicePostChain.h` — modify (distortion leaves the post-chain)

| Old function | New function | Change |
|---|---|---|
| `voicePost_drum(buf, gain, lastGain, veloGain, dist, size)` (line ~30) | `voicePost_drumGain(buf, gain, lastGain, veloGain, size)` | the last line becomes `buf[i] = vel;` (the int16 stored before the curve) |
| `voicePost_mixAddGainDist(…)` | `voicePost_mixAddGain(buf, add, mix, ampGain, size)` | `buf[j] = g;` |
| `voicePost_addGainDist(…)` | `voicePost_addGain(buf, add, ampGain, size)` | `buf[j] = g;` |

```c
/*
 * Post-chain without distortion (S084 VX3 VX3-7.1).
 *
 * What:       the same fused passes as S073 Step 4 (amp EG ramp, velocity,
 *             or Snare/Cymbal/HiHat mix-add-gain) with the final distortion
 *             curve removed; the stored int16 is exactly the value the curve
 *             used to receive.
 * Why:        plan §4.6: drive moves into the Mix FX stage (D+S), which
 *             applies distortion_curveSample16() to the same int16, so D+S is
 *             bit-identical to the old chain (test_insert_ds).
 * Inputs:     as before minus the Distortion.  Outputs: pre-drive block.
 * Accessors:  the four engines.
 */
```

### VX3-7.2 Engines — post-chain call sites

| File | Line | Change |
|---|---|---|
| `Drum/DrumVoice.c` | 416 | `voicePost_drum(…, &voice->distortion, size)` → `voicePost_drumGain(…, size)` |
| `Drum/DrumVoice.c` | 401 | non-default `USE_FILTER_DRIVE == 0` branch: delete `calcDistBlock(&voice->distortion,buf,size);` (the stage now applies it) |
| `Snare/Snare.c` | 310 | `voicePost_mixAddGainDist` → `voicePost_mixAddGain` |
| `Cymbal/CymbalVoice.c` | 328 | `voicePost_addGainDist` → `voicePost_addGain` |
| `HiHat/HiHat.c` | 339 | the same |

The comments at Cymbal 272, HiHat 283 and Snare 254 (the old "volume before
calcDistBlock" notes) gain: "S084 VX3: distortion now runs in the Mix FX
stage (D+S) after this pass."

`Distortion distortion` stays in each engine struct: it remains the `dpt`
target (D5).

### VX3-7.3 Engine structs — add `insertType`, `insertAmount`, `stageSlot`

| File | After |
|---|---|
| `Drum/DrumVoice.h` | line 88 `Distortion distortion;` |
| `Snare/Snare.h` | line 109 |
| `Cymbal/CymbalVoice.h` | line 97 |
| `HiHat/HiHat.h` | line 103 |

```c
	/*
	 * Insert stage fields (S084 VX3 VX3-7.3).
	 *
	 * What:       insertType: runtime copy of the fxt row (diagnostics; the
	 *             manager holds the live selection); insertAmount: the amt row
	 *             (0..127), read by the Mix stage every block; stageSlot: this
	 *             runtime member's slot, set after init, for the transient
	 *             buf calls (D11).
	 * Why:        plan §4.6 rows; D7, D8, D11. 3 B inside the runtime-slot
	 *             union (Drum 1,116 -> 1,119 of 1,176 B).
	 * Writers:    the IM writers / the descriptor path; resetRuntimeSlot.
	 */
	uint8_t		insertType;
	uint8_t		insertAmount;
	uint8_t		stageSlot;
```

### VX3-7.4 `Core/DSPAudio/mixer.c` — remove the decimator; add the service call

- **Remove:**
  - `mixer_decimation_rate[6]`, `mixer_decimation_cnt[6]` and
    `mixer_voice_samples[6]` (lines 129–131, DTCM, −60 B);
  - in `mixer_init()` (lines 189–196), their three init lines. **Keep**
    `mixer_audioRouting[i] = 0` and `mixer_voice_last_gain[i] = …`, which
    leave the `#if USE_DECIMATOR` block: hoist them out of the `#if` so they
    still run;
  - `mixer_decimateBlock()` (lines 203–216);
  - the call at line 1035.
- **`mixer.h`:** remove line 60 (`extern float mixer_decimation_rate[6]`)
  and the `mixer_decimateBlock` prototype.
- **Add:** before the slot loop (above line 1023, after `effects_service()`
  in the same function), call `insertFx_service();` with this comment:

```c
	/*
	 * Voice buffer service, once per block before any slot renders
	 * (S084 VX3 VX3-7.4).
	 *
	 * What:       completes held option changes whose runner has freed and
	 *             releases expired tails' leases (one coalesced share change).
	 * Why:        D1 Hold; plan §5.3.2: share changes happen between blocks,
	 *             before voices write into units.
	 */
```

- **Update** the comment at line 1037 ("sampleData is the decimated,
  pre-volume voice block"). It now reads: "sampleData is the voice block
  after its Mix FX stage (D+S includes today's decimator), pre-volume."

### VX3-7.5 `Core/DSP/Instruments/InstrumentManager.c` line 2036 `instrumentManager_calcSlotSyncBlock()` — add the Mix stage

After the engine `switch` (before the function returns), for a known slot
type:

```c
    /*
     * Mix FX stage at the end of the slot's render (S084 VX3 VX3-7.5, D4).
     *
     * What:       hands the slot's Distortion (dpt / D+S curve) and
     *             insertAmount (amt) to insertFx_renderMix(), which runs the
     *             slot's Mix type, adds its ringing tails, and returns the
     *             stage-input peak for buffer types (else
     *             INSERT_WITNESS_OUTPUT); that value is kept for
     *             instrumentManager_noteSlotBlockPeak().
     * Why:        plan §4.6 placement (after amp EG and velocity, before the
     *             mixer's volume/pan/send), §5.7 swap witness; one site
     *             for all four engines; InsertEffects does not include
     *             InstrumentManager (the values are handed in).
     * Inputs:     slot, the rendered block.  Outputs: block in place,
     *             instrumentManager_stageInputPeak[slot].
     */
    {
        insert_stage_in_t in;

        in.drive = instrumentManager_distortion(slot);
        in.amount = instrumentManager_insertAmount(slot);
        instrumentManager_stageInputPeak[slot] =
            in.drive ? insertFx_renderMix(slot, buf, size, &in)
                     : INSERT_WITNESS_OUTPUT;
    }
```

- **Static accessor:** `instrumentManager_insertAmount(slot)`, beside
  `instrumentManager_distortion()` at line 2571.
- **Static array:** `instrumentManager_stageInputPeak[6]` (SRAM1, 12 B),
  initialised to `INSERT_WITNESS_OUTPUT`.

### VX3-7.6 `InstrumentManager.c` line 1563 `instrumentManager_noteSlotBlockPeak()` — modify

First line of the body:

```c
    /* S084 VX3 VX3-7.6: buffer-type stages report their input level, so a
    ** ringing echo or room tail never holds a swap back (plan §5.7). */
    if (slot < INSTRUMENT_SLOT_COUNT &&
        instrumentManager_stageInputPeak[slot] != INSERT_WITNESS_OUTPUT)
        peak = instrumentManager_stageInputPeak[slot];
```

### VX3-7.7 `InstrumentManager.c` line 1694 `instrumentManager_resetRuntimeSlotFromScene()` — set `stageSlot`

After the engine `*_initVoice()` switch: set the new member's
`stageSlot = slot` (a per-type case, the same pattern as
`instrumentManager_driveFields()`).

```c
    /* S084 VX3 VX3-7.7: the runtime member learns its slot for the transient
    ** buf calls; insert records are NOT touched (they live outside the slot,
    ** so a swap never cuts a tail, plan §5.7 item 6). */
```

### VX3-7.8 `InstrumentManager.c` line 4237 (`INSTRUMENT_BIND_SLOT_DECIMATION`) — re-point the writer (D6)

```c
    case INSTRUMENT_BIND_SLOT_DECIMATION:
        if (slot >= INSTRUMENT_SLOT_COUNT)
            return 0u;
        /*
         * rte -> the slot's Mix stage (S084 VX3 VX3-7.8, D6).
         *
         * What:       stores the byte in InsertEffectsManager, which keeps
         *             the D+S increment valueShaperI2F(byte, -0.7f) (the same
         *             mapping the mixer used) and the raw 0..1 rate for other
         *             types.
         * Why:        the decimator moved out of mixer.c; the binding, its
         *             Morph path and its installed LFO/velocity targets
         *             (applySlotDecimationTarget and friends) are unchanged.
         */
        insertFx_setRate(slot, (uint8_t)((value > 127u) ? 127u : value));
        return 1u;
```

### VX3-7.9 `Makefile` — build rules and includes

- **Includes** (line 41 area): `-ICore/DSP/InsertEffects` and
  `-ICore/DSP/InsertEffects/Transient`.
- **Sources:**
  - `INSERT_SRCS` lists the manager, VoiceBuffer, the 10 Mix files and the
    7 transient files;
  - append `$(patsubst %.c,$(BUILD)/%.o,$(INSERT_SRCS))` to `OBJS`.
- **Pattern rules** (plan §5.8.5), beside the DSPAudio rule at line 211:

```make
# S084 VX3: insert effects and voice buffers are render code (-Ofast).
$(BUILD)/Core/DSP/InsertEffects/%.o: Core/DSP/InsertEffects/%.c | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS_DSP) $< -o $@
$(BUILD)/Core/DSP/InsertEffects/Transient/%.o: Core/DSP/InsertEffects/Transient/%.c | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS_DSP) $< -o $@
```

### VX3-7.10 `main.c` line 121 (`dsp_init()`) — init order

Call `insertFx_init()` right after `fxbuf_init();` (line 121) and before
`instrumentManager_runtimeInit()` and `effects_init()`, so the slot render
and the first Scene apply find initialised records. The boot Scene's
selector pre-pass then makes the first reservations inside
`preset_sendDrumsetParameters()`'s pass (VX3-9.6).

---

## 9. WP8 — Transient buffer types (`Core/DSP/InsertEffects/Transient/`)

**Common to every type:**

- `units` 1 (50 ms, Q27), `family` = its own id, `flags` 0;
- a placement per D12;
- cap ≤ 22 instructions per sample and **0 divisions** (P3-D);
- decay ≤ 3 s past the input (the feedback laws use
  `delay_feedbackCeiling`).

**Controls:** `c0` = `vol` (`transient_vol`, the wet level), `c1` = `frq`
(0..1 from the P2 pitch factor, VX3-5.1). The `transient_wave` byte selects
the option (0..6). Labels: `wav` shows the option name; `vol` → `lvl`;
`frq` → the per-type `tun`/`tim`.

| Change | File | Type | Placement | Algorithm (per sample unless noted) | State | Est. |
|---|---|---|---|---|---:|---:|
| VX3-8.1 | `TransientResonator.c` | `Res` | PRE | damped feedback comb tuned by `frq`: `f = 20 · 100^frq` Hz (20 Hz–2 kHz), `d = FS/f` (block rate); `y = x·0.25 + g·tap(d)`, `g = 0.98·ceiling(d)`; `buf = x + vol·y` (the oscillators excite the cavity) | 8 B | 16–18 |
| VX3-8.2 | `TransientPluck.c` | `Plk` | PRE | Karplus–Strong: on `trigger`, the first `d` samples of the stock PCM `Clk` (`transientData[0]`) are written into the ring (one block of setup, ≤ `d` samples, inside the cap budget for that block); then `y = g·0.5·(tap(d) + tap(d+1))`, `g = ceiling(d)`; `buf = x + vol·y` | 8 B | 19–21 |
| VX3-8.3 | `TransientTube.c` | `Tub` | PRE | negative-feedback comb: `y = x·0.25 − g·tap(d)` (odd harmonics), as `Res` otherwise | 8 B | 16–18 |
| VX3-8.4 | `TransientFlam.c` | `Flm` | POST | one delayed copy, `t = 5 ms + frq·45 ms`: `buf = x + vol·tap(t)`; write `x` | 4 B | 10–12 |
| VX3-8.5 | `TransientStutter.c` | `Bzz` | POST | on `trigger`, record N = `5 ms + frq·45 ms` of input; afterwards loop that segment with gain `r^k` per repeat, `r = ceiling(N/FS)`; `buf = x + vol·loop` | 12 B | 14–16 |
| VX3-8.6 | `TransientReverse.c` | `Rvs` | POST | on `trigger`, record the next 50 ms; then play it backwards once with a 2 ms fade at each end; `buf = x + vol·rev` | 8 B | 12–14 |
| VX3-8.7 | `TransientSpring.c` | `Spr` | POST | 50 ms allpass with feedback: `w = tap(d)`, `v = x + g·w`, write `v`, `y = w − g·v`, `g = 0.7·ceiling(0.05)`; a one-pole adds the darker spring colour; `buf = x + vol·y` (`frq` sets the allpass length 20–50 ms) | 12 B | 14–16 |

Each file's block repeats its table row as What/Why, with Inputs (ring,
`c0`, `c1`, trigger) and Outputs (buf in place).

### VX3-8.8 `Core/DSPAudio/transientGenerator.h` line 60 and `transientGenerator.c` lines 136–195 — open `buf`

- `TRANSIENT_TYPE_SELECTABLE` 2 → **3**.
- `transient_waveMax(TRANSIENT_TYPE_BUF)` returns `INSERT_TRANSIENT_COUNT − 1`
  (6).
- **New kind:** `TRANSIENT_KIND_BUFFER`. `transient_resolve()` gives it for
  `type == buf`, and `transient_calcBlock()` renders silence for it (the
  stage processes the voice signal instead).

```c
/*
 * Transient buf type (S084 VX3 VX3-8.8).
 *
 * What:       trt = buf selects one of the 7 single-unit buffer options by
 *             the wav byte (0..6); the generator itself outputs silence and
 *             the InsertEffects transient stage processes the voice at the
 *             option's pre/post-filter point.
 * Why:        plan §4.3.1/§4.3.2; the type row keeps automation inside the
 *             type (it is not automatable), so automation never crosses
 *             between buffer and non-buffer options (Q27, C10).
 */
```

### VX3-8.9 Engines — transient stage call sites and trigger

**Pre and post calls:**

| File | Pre-filter call (after) | Post-filter call (after) |
|---|---|---|
| `Drum/DrumVoice.c` | the oscillator section and its `itp` blend (after line 364's block, before `transient_calcBlock` at 378) | the filter `if/else` (after line 388's block) |
| `Snare/Snare.c` | the noise render and its `itp` blend (after line 266's block) | the filter (after line 277's block) |
| `Cymbal/CymbalVoice.c` | the FM section and blend (after line 289's block) | the filter (after line 307's block) |
| `HiHat/HiHat.c` | the FM section and blend (after line 299's block) | the filter (after line 318's block) |

```c
	/* S084 VX3 VX3-8.9: transient buf stage, pre-filter (D12). Returns at
	** once unless this slot's buf option is a PRE type. */
	insertFx_renderTransient(voice->stageSlot, INSERT_PLACE_PRE, buf, size,
	                         voice->transGen.volume, voice->transGen.pitch);
```

The post call is identical, with `INSERT_PLACE_POST`.

**Trigger:** next to each `transient_trigger(&voice->transGen);` (Drum 256,
Snare 196, Cymbal 215, HiHat 220), add
`insertFx_transientTrigger(voice->stageSlot);`:

```c
	/* S084 VX3 VX3-8.9: re-arms Plk/Bzz/Rvs on the next block. */
```

**Cost:** both calls run every block. The inactive placement returns after
one compare, and the active one costs its type (constant per type).

---

## 10. WP9 — Descriptor rows, writers, Preset apply passes

### VX3-9.1 Rows — append after `filter_drive_type` (Drum line 318, Snare 294, Cymbal 306, HiHat 309)

```c
    /*
     * Voice Mix FX type and amount (S084 VX3 VX3-9.1).
     *
     * What:       fxt = INSERT_MIX_ID_* (D+S default), amt = the type's third
     *             control (wet / symmetry). dpt and rte stay on the existing
     *             instrument_drive / instrument_decimation cells.
     * Why:        plan §4.6 rows; fxt is not automatable or modulatable
     *             (Q27); amt is Morphable, modulatable and automatable.
     *             Appended: existing indices, automation IDs, LFO tokens,
     *             MIDI Learn and NRPN IDs are unchanged.
     * Inputs:     menu, MIDI, files (keys mix_fx_type, mix_fx_amount; missing
     *             = 0 = D+S / 0).
     * Outputs:    IM_SPECIAL_MIX_FX_TYPE / _MIX_FX_AMOUNT writers.
     */
    ROW_SELECT_NOAUTO_SPECIAL("mix_fx_type", "Voice", "MixFX", "fxt", insertType, IM_SPECIAL_MIX_FX_TYPE),
    ROW_SPECIAL("mix_fx_amount", "Voice", "Amount", "amt", DTYPE_0B127, MOD_0_127, insertAmount, TYPE_UINT8, IM_SPECIAL_MIX_FX_AMOUNT),
```

### VX3-9.2 Renamed labels on the existing cells

| Instrument | Line | Change |
|---|---|---|
| Drum | 248 | `"Overdriv", "drv"` → `"Depth", "dpt"` |
| Drum | 249 | `"SampleRt", "srt"` → `"Rate", "rte"` |
| Snare | 241 / 242 | the same |
| Cymbal | 248 / 249 | the same |
| HiHat | 251 / 252 | the same |

- **File keys** (`instrument_drive`, `instrument_decimation`) are
  unchanged, so old kits load.
- **Label source:** the type-specific labels come from the D13 hooks, so
  these strings are the fallback only.

```c
    /* S084 VX3 VX3-9.2: dpt/rte name the Mix stage controls; the shown label
    ** follows the slot's Mix type (InstrumentCellFormat label hook). */
```

### VX3-9.3 Enums, counts and pages

| File | Change |
|---|---|
| `Drum/DrumParameters.c` line 214 | after `DRUM_PARAM_FILTER_DRIVE_TYPE,`: `DRUM_PARAM_MIX_FX_TYPE, DRUM_PARAM_MIX_FX_AMOUNT,` |
| `Drum/DrumParameters.h` line 14 | 46 → 48 |
| Snare `.c` 208 / `.h` 14 | the same pair; 43 → 45 |
| Cymbal `.c` 214 / `.h` 14 | the same; 48 → 50 |
| HiHat `.c` 217 / `.h` 14 | the same; 48 → 50 |
| Voice page (Drum 355, Snare 328, Cymbal 339, HiHat 344) | cells 4, 5 (second screen): `…_MIX_FX_TYPE`, `…_MIX_FX_AMOUNT` |

The existing `_Static_assert`s prove the counts; all stay ≤ 64 (P3 §3).

### VX3-9.4 `Core/DSP/Instruments/InstrumentManager.h` line 170 — writer tags

After `IM_SPECIAL_DRIVE_TYPE,`: `IM_SPECIAL_MIX_FX_TYPE, IM_SPECIAL_MIX_FX_AMOUNT,`
(count 27 ≤ 32; the existing assert covers it).

### VX3-9.5 `InstrumentManager.c` — writers (after line 3904's `IM_SPECIAL_INSTRUMENT_DRIVE` case)

```c
    /*
     * Mix FX writers (S084 VX3 VX3-9.5).
     *
     * What:       TYPE: records the runtime copy and selects the type through
     *             insertFx_selectMix() (same use continues; a buffer change
     *             leaves a tail; a held change waits for a runner; a load
     *             conflict downgrades Ec2/Rv2); a non-buffer change arms the
     *             slot declick (D10). AMOUNT: stores the byte the Mix stage
     *             reads each block.
     * Why:        plan §4.6, §5.3.5; selection happens only when an option is
     *             applied (D12).
     * Inputs:     byteValue.  Outputs: engine insertType/insertAmount; records.
     * Accessors:  instrumentManager_writeRuntime() (the selector pre-pass for
     *             TYPE; Morph/LFO/velocity/automation for AMOUNT).
     */
    case IM_SPECIAL_MIX_FX_TYPE: {
        uint8_t *type_field;
        if (!instrumentManager_insertFields(slot, &type_field, NULL)) return 0u;
        *type_field = byteValue;
        if (insertFx_selectMix(slot, byteValue))
            instrumentManager_slotDeclickArm |= (uint8_t)(1u << slot);
        return 1u; }
    case IM_SPECIAL_MIX_FX_AMOUNT: {
        uint8_t *amount_field;
        if (!instrumentManager_insertFields(slot, NULL, &amount_field)) return 0u;
        *amount_field = (byteValue > 127u) ? 127u : byteValue;
        return 1u; }
```

`instrumentManager_insertFields()` is new, a per-type accessor beside
`instrumentManager_driveFields()`.

### VX3-9.6 `InstrumentManager.c` lines 3889/3894 — transient writers call the stage

After `transient_setType()` / `transient_setWave()`:

```c
        /* S084 VX3 VX3-9.6: buf options reserve/continue/tail through the
        ** transient stage; other types release it (a running buf becomes a
        ** tail, C6). */
        insertFx_selectTransient(slot, transient->waveform,
                                 (uint8_t)(transient->type == TRANSIENT_TYPE_BUF));
```

### VX3-9.7 `InstrumentManager.c` line ~4146 — classifier keys

```c
    if (strcmp(key, "mix_fx_type") == 0) return IM_SPECIAL_MIX_FX_TYPE;
    if (strcmp(key, "mix_fx_amount") == 0) return IM_SPECIAL_MIX_FX_AMOUNT;
```

### VX3-9.8 `Core/Bank/Scene/Preset/presetManager.c` — apply passes (one coalesced share change)

| Line | Function | Change |
|---|---|---|
| 1946 | `preset_resetAndApplyKitVoiceImage()` | `voiceBuffer_beginPass()` at entry, `voiceBuffer_endPass()` at every exit; the selector pre-pass at 1972 therefore reserves inside one pass |
| 2045 | `preset_sendDrumsetParameters()` | begin before the six-slot loop, end after it: the boot Scene's six slots give **one** share change |
| ~3117 | the immediate Instrument apply (`preset_applySelectorRows(scene_getActiveIndex(), slot)`) | begin before, end after the selector call |

The menu/MIDI single edits need no explicit bracket: `voiceBuffer_select()`
brackets itself.

```c
    /*
     * One voice-buffer apply pass (S084 VX3 VX3-9.8).
     *
     * What:       brackets this apply so every unit reservation and lease in
     *             it produces at most one Effect share change, delivered at
     *             the end of the pass, before the next render block writes
     *             into the new units.
     * Why:        plan §5.3.2 / P3-A: CrumpBit compacts once per Scene or
     *             Instrument apply, not once per slot (C7, C14).
     */
```

- **C12 (Instrument-Load preview):** the preview arms the shared deferred
  swap (P1). Its commit reaches `preset_resetAndApplyKitVoiceImage()`, so it
  inherits the pass, and the records outside the slot keep any tail
  ringing. No extra code.

### VX3-9.9 `tools/dsp_test/check_special_tags.py` line 51 — map the keys

Add `'mix_fx_type': 'MIX_FX_TYPE', 'mix_fx_amount': 'MIX_FX_AMOUNT'`.
Expected: 193 rows, 0 mismatches.

### VX3-9.10 Comment-only updates naming the removed mixer array

`mixer_decimation_rate[slot]` is removed (VX3-7.4). These comments still
name it:

| File | Line | Change |
|---|---|---|
| `Drum/DrumParameters.c` | 148 | `mixer_decimation_rate[slot]` → `insertFx_setRate(slot, byte)` (the `D+S` Mix stage rate) |
| `Snare/SnareParameters.c` | 146 | the same |
| `Cymbal/CymbalParameters.c` | 147 | the same |
| `HiHat/HiHatParameters.c` | 148 | the same |
| `InstrumentManager.c` | 2921 (`instrumentManager_applySlotDecimationTarget()`) | "updates mixer_decimation_rate[target_slot]" → "updates the slot's Mix stage rate through insertFx_setRate() (VX3-7.8)" |

```c
/* S084 VX3 VX3-9.10: the rate lives in InsertEffectsManager (insertFx_setRate);
** the mixer no longer decimates. */
```

After WP7, `grep -rn mixer_decimation Core/` must return nothing.

---

## 11. WP10 — Menu, labels, hiding

### VX3-10.1 `Core/Menu/MenuText.h` — `transientTypeNames` (P2)

```c
/* S084 VX3 VX3-10.1: buf opens (TRANSIENT_TYPE_SELECTABLE = 3). */
static const char transientTypeNames[][4] = {
    {3}, {"trs"},{"wav"},{"buf"},
};
```

The P2 count assert in `menu.c` follows the constant. Mix FX and
transient-buffer names are **not** added here: they live in their type
files (O3) and come through `insertFx_mixName()` /
`insertFx_transientName()`.

### VX3-10.2 `Core/DSP/Instruments/InstrumentCellFormat.h/.c` — kinds, hidden mask, labels, step

**Kinds** (append): `CELL_FMT_MIX_FX_TYPE` (`insertFx_mixName(index)`) and
`CELL_FMT_TRANSIENT_BUF` (`insertFx_transientName(index)`).

**`instrument_cell_format_t`** gains `uint32_t hidden;`. Bit v set means
value v is currently not selectable.

**New cases:**

- `IM_SPECIAL_MIX_FX_TYPE`: kind `MIX_FX_TYPE`, max 11,
  `hidden = insertFx_mixHiddenMask(slot)`.
- `IM_SPECIAL_TRANSIENT_TYPE`: max 2, and when
  `!insertFx_transientBufFits(slot)` it sets
  `hidden |= 1u << TRANSIENT_TYPE_BUF`, unless `buf` is the current value
  (plan §5.3.1: `buf` is hidden while `Ec2`/`Rv2` is active).
- `IM_SPECIAL_TRANSIENT_WAVE` under `buf`: kind `TRANSIENT_BUF`, max 6.
- `IM_SPECIAL_MIX_FX_AMOUNT`: not formatted (the dtype prints 0..127); its
  label comes from the hook below.

**Label hooks** (D13):

```c
/*
 * Type-dependent row labels (S084 VX3 VX3-10.2, D13).
 *
 * What:       for the dpt row (IM_SPECIAL_INSTRUMENT_DRIVE), the rte row
 *             (runtime.kind == INSTRUMENT_BIND_SLOT_DECIMATION) and the amt row
 *             (IM_SPECIAL_MIX_FX_AMOUNT), returns the slot's Mix type's 3- or
 *             8-character label (insertFx_mixLabel()); for transient_vol and
 *             transient_freq under trt = buf, the option's labels; NULL
 *             otherwise (Menu uses the descriptor's own names).
 * Why:        plan §5.2: Ech shows fbk/tim/wet, Rev dcy/dmp/wet, D+S drv/srt/-.
 * Inputs:     slot, descriptor.  Outputs: label or NULL.
 * Accessors:  menu.c label sites (VX3-10.3).
 */
const char *instrumentCellFormat_shortLabel(uint8_t slot,
                                            const ParamDescriptor *descriptor);
const char *instrumentCellFormat_longLabel(uint8_t slot,
                                           const ParamDescriptor *descriptor);
```

**Step** (D16):

```c
/*
 * Next visible value for an encoder step (S084 VX3 VX3-10.2, D16).
 *
 * What:       from value, moves |inc| visible steps in inc's direction,
 *             skipping bits set in the format's hidden mask; clamps at 0 and
 *             max. Returns the input value for unformatted rows.
 * Why:        hidden Mix/transient options must be skipped, not landed on.
 * Inputs:     slot, descriptor, value, inc.  Outputs: new value.
 * Accessors:  menu_encoderChangeParameter() (VX3-10.4).
 */
uint16_t instrumentCellFormat_step(uint8_t slot, const ParamDescriptor *descriptor,
                                   uint16_t value, int8_t inc);
```

### VX3-10.3 `Core/Menu/menu.c` — label sites

At each site, `const char *l = instrumentCellFormat_shortLabel(slot,
descriptor);` is used in place of `descriptor->short_name` when non-NULL;
likewise for long names:

| Line | Site | Hook |
|---|---|---|
| 14576 | page cells | short |
| 6400 | the target short text | short |
| 14260 | the step-automation target | short |
| 6467 | the single-parameter view long name | long |
| 14127 | the long name | long |

```c
/* S084 VX3 VX3-10.3: Mix FX / transient buf rows show the active type's
** labels (InstrumentCellFormat label hook, D13). */
```

### VX3-10.4 `menu.c` line ~14760 (`menu_encoderChangeParameter()`) and line 6544 (`menu_clampCellValue()`) — hiding

- **Encoder:** before the generic `value + inc` arithmetic, after the
  roll-rhythm precedent: for an instrument cell whose format has a non-zero
  `hidden` mask, `value = instrumentCellFormat_step(slot, descriptor, value,
  inc)`, then commit.
- **Clamp** (pots), inside the format branch: if the clamped value is
  hidden, move to the nearest visible value (prefer down).

```c
/* S084 VX3 VX3-10.4: options that do not fit (plan §5.3.1) are skipped by
** the encoder and avoided by pots; live writes of fxt/trt cannot happen
** (not automatable), loads downgrade instead (C13). */
```

---

## 12. WP11 — Tools and host tests (`tools/dsp_test/`)

All new tests follow the existing pattern: `$(EX)` extracts the firmware
functions into `$(GEN)`; the test `#include`s them, then builds with
`$(HOSTCC) $(HOSTFLAGS)` and prints `<name> OK (0 failures)`. Each target
is added to the `.PHONY` list (Makefile line 1–5) and to the runner's list.

### VX3-11.1 `test_fxbuf.c` (new) — Gate G1

```c
/*
 * FxBuffer positions, leases and passes (S084 VX3 VX3-11.1).
 *
 * What:       drives fxbuf_unitAcquire/Release/ToLease/LeaseToSlot and
 *             begin/endPass through random sequences (fixed seed, 100k
 *             steps), checking after each step: no position has two owners;
 *             2-unit claims are adjacent; the Effect share is never below
 *             fxbuf_effectFloorBytes(); exactly one share-changed callback
 *             per pass that changed the share, zero otherwise; the handoff
 *             export lists positions 0..11 only; fxbuf_selfTest() returns 0.
 * Why:        Gate G1; plan §5.3.2 invariants.
 * Inputs:     FxBuffer.c (extracted, static= renamed).  Outputs: OK / fail.
 */
```

### VX3-11.2 `test_crumpbit_elastic.c` (new) — Gate G1

- Writes a ramp of known values into a running CrumpBit, then forces the
  share down to `buffer_min` and back up to `pref` in steps.
- **Shrink check:** after each shrink, the newest `new_length` samples
  match the ramp. This proves `crumpBit_compact()`.
- **Read crossfade:** the read crossfade spans exactly one block, with no
  step larger than the input slope. This proves the 68 B runtime fields.
- **Grow check:** a grow takes effect only at the wrap.
- **Rate clamp:** `crumpBit_effectiveMin()` clamps the shown and applied
  Rate.
- **Sync divisions:** these stay within the loop.

### VX3-11.3 `test_insert_ds.c` (new) — Gate G2

The frozen P2 post-chain (`frozen/` copy of `voicePostChain.h`, the four
engine loops and `mixer_decimateBlock`) is checked against the P3 split, in
which the post-chain feeds the `D+S` Mix stage.

- **Signals:** 48 signals × 7 shapes × 5 rates × velocity extremes.
- **Steady state:** 0 differing samples.
- **Swap blocks:** the order differs only inside declick blocks (D9).
  These are measured and reported, and are not counted as failures.

### VX3-11.4 `test_insert_types.c` (new) — Gate G3, per type

For each registered Mix type and transient buffer type:

- `sizeof` state ≤ `INSERT_STATE_BYTES`.
- **Bounded output:** a 1 s full-scale noise burst at every control corner
  gives no NaN or Inf, and |output| stays ≤ the cap.
- **Decay:** after input stops, output falls below −90 dBFS within 3 s at
  the slowest corner (feedback ceiling `delay_feedbackCeiling`).
- **`tail()`:** reports quiet within 3 s.
- **Retarget:** `retarget()` at any control step gives no step larger than
  one ramp increment.
- **HALF_RATE types:** the output is interpolated with no images above
  Nyquist/2 over −60 dB.

### VX3-11.5 `test_voicebuffer_cases.c` (new) — Gate G2, C1–C16

The test drives `voiceBuffer_*` / `insertFx_select*` through each catalogue
case from S084_P1 §5.3.6. It uses a stub engine that writes a known
impulse train.

| Case | Assert |
|---|---|
| C1/C2/C8 | same record, no share change, ring contents preserved |
| C3 | second unit adjacent (unit−1), base kept, the loop extends at the wrap |
| C4/C5/C6 | the old record becomes a tail on a lease; the tail output equals the continuing ring's output to expiry; the new option starts at once |
| C7 | one share change |
| C9 | re-adoption: same record, no gap |
| C10 | trigger-time C5 for transient `buf` |
| C11 | two tails; overflow holds (D1) with 2 runners, no voice dry |
| C12 | the deferred swap path; the tail outside the slot continues |
| C13 | `Ec2`/`Rv2` downgrade when the units do not fit |
| C14/C15 | with the CrumpBit stub: shrink/grow counts, share ≥ floor |
| C16 | Morph never reaches `insertType` |

**Global asserts** after each case:

- All units and leases are returned after expiry.
- `fxbuf_selfTest() == 0`.
- One share change per pass.

### VX3-11.6 `test_transient.c` — extend (P2 test)

Add the cases below:

- `trt = buf` selects through the stub stage.
- `transient_waveMax()` returns 6 under `buf`.
- `TRANSIENT_TYPE_SELECTABLE == 3`.
- The `trs`/`wav` outputs are unchanged.

### VX3-11.7 `armcheck/vx3.c` + Makefile `armcheck-vx3` (new) — Gates G3/G4

Model this on `armcheck-vx2` (Makefile line 303).

- **Extraction:** extract each type's `process` with `--rename static=`,
  plus `voiceBuffer_render` and `insertFx_renderMix`.
- **Probes:** one probe per type, each one specialised loop. As with
  `vx2_spec.c`, shared functions hold overlapping loops, so loop 0 is not a
  meaningful figure.
- **Report:** `fpseq.py --report --all` per probe.

| Cap | Limit |
|---|---|
| Mix types | ≤ the WP0 `D+S` share per sample |
| Transient buf types | ≤ 22 instructions/sample |
| All types | 0 `vdiv`/`sdiv` in the loop |

```make
# S084 VX3 WP11: per-type instruction report for the insert stages (armcheck/vx3.c).
armcheck-vx3: | $(GEN)
```

### VX3-11.8 `check_special_tags.py` — expected count

185 → **193** rows (8 new rows: 2 × 4 instruments), 0 mismatches
(VX3-9.9 maps the keys).

### VX3-11.9 Baseline status (taken this session, before P3)

`make -C tools/dsp_test` (all targets) exits 0. Results:

- `selftest`: SDR inf.
- `filter`: S1 PASS.
- `special_tags`: 185 rows, 0 mismatches.
- `pack`, `postchain`, `mixer`: 0 differing.
- `octave`: 16 edge mismatches ≤ 2 ulps, as before.
- All P2 tests OK.

P3 must keep every one of these, except `special_tags` (expected 193) and
`mixer`. The `mixer` test changes because `mixer_decimateBlock` is removed:
its frozen comparison is re-pointed to `test_insert_ds` (VX3-11.3), and the
mixer test keeps covering the remaining sum and route.

---

## 13. WP12 — Specifications

All files are under `knowledge_files/specification_reference/`, and each
change is tagged `S084 VX3`.

| File | Section | Change |
|---|---|---|
| `dsp_instruments_effects/EFFECTS_BUS_REFERENCE.md` | Effect contract / CrumpBit | the elastic contract (buffer_min ≤ floor, shrink keeps newest, lazy grow, `effective_min`, one share change per pass); the CrumpBit min/pref; the Rate display at the reachable range |
| `dsp_instruments_effects/EFFECTS_MIXER_DSP_REFERENCE.md` | Mixer / FxBuffer | the decimator removed from the mixer (SRR is now the `D+S` Mix stage); positions 0..15, regular/lease, floor arena − 16 units; the declick-after-decimation order (D9) |
| `dsp_instruments_effects/INSTRUMENTS_DSP_REFERENCE.md` | Voice chain | the signal order (post-chain → Mix stage → level witness → declick); the transient stage pre/post points (D12); the type table with controls, units, placement and caps; the decay limit |
| `STORAGE_SRAM_MANIFEST.md` | DTCM / SRAM1 | 14 × 80 B records (`.dtcmz`); the mixer arrays removed; SRAM1 +134 B; arena 125,504 B |
| `MODULE_INTERCHANGE_SPEC.md` | Kit keys | `mix_fx_type`, `mix_fx_amount` (missing = 0); `instrument_drive`/`instrument_decimation` keep their keys with the new labels dpt/rte; transient `trt = 2` (`buf`) and its 0..6 option index |
| `dsp_instruments_effects/CPU_USE_DSP_AUDIT.md` | Voice / stages | the measured per-type costs (armcheck-vx3), the runner cost, and the worst-case Scene |
| `DEV_MODES.md` | FxBuffer diagnostic | the owner-map dump (VX3-4.9) and the forced-units build used for Gate G1 |

---

## 14. Verification

### 14.1 Host (every commit point)

- `make -C tools/dsp_test` (the full target list) passes, plus
  `test_fxbuf`, `test_crumpbit_elastic`, `test_insert_ds`,
  `test_insert_types` and `test_voicebuffer_cases`.
- `armcheck-vx3` is within its caps.
- `special_tags` reports 193.
- **Firmware build:** `make` with `-Werror` is clean.
- **Link budget:** `link_budget.py` shows DTCM, SRAM1, ITCM and flash
  within §0.5.

### 14.2 Hardware matrix

| # | Setup | Action | Expect |
|---|---|---|---|
| H1 | DEV forced-units build | step units 0→16→0 with CrumpBit running | CrumpBit continuous, newest audio kept, Rate display clamps (G1) |
| H2 | each Mix type in turn on one Drum slot | sweep dpt/rte/amt to both ends | the sound matches the type sheet; no runaway; decay ≤ 3 s |
| H3 | `D+S`, every shape and rate | A/B with the P2 build | identical by ear and by capture (null below −120 dB outside swaps) |
| H4 | 6 slots `Ec2`/`Rv2` | load the Scene | downgrade (C13); menu hides the options that do not fit |
| H5 | 6 slots `Ech` ringing + CrumpBit | switch Scene to 6 slots `Rev` | every echo tail plays out; no voice dry; one CrumpBit compaction; no click |
| H6 | the same, with an Instrument-Load preview | preview → commit → cancel | C12 behaviour; tails continue |
| H7 | A→B→A within the tail | the fxt encoder | re-adoption, no gap (C9) |
| H8 | transient `buf`, each option | trigger at all velocities | pre/post placement audible; decay; `buf` hidden under `Ec2`/`Rv2` |
| H9 | worst-case Scene (P2 worst voice + 6 buffer stages + 2 runners) | run the CPU meter | no overrun; headroom recorded in §16 |
| H10 | an old kit (no new keys) | load | `D+S`, amt 0; dpt/rte sound as in P2 |

### 14.3 Items to confirm during implementation

- **Engine anchors:** the line anchors in VX3-7.2/7.3/8.9 follow the P2
  fixes. Re-locate each one by its quoted code before editing (§0.2).
- **`.dtcmz` placement:** the records land in `.dtcmz` and stay out of the
  FxBuffer arena (check the map file).
- **`insertFx_service()` call site:** it runs before the render in each
  foreground tick, after `effects_service()`, so a pass's share change is
  delivered before the next block (VX3-7.4).
- **Every rte path reaches the stage:** the menu/MIDI/automation writer
  (line 4237) and the four modulation calls into
  `instrumentManager_applySlotDecimationTarget()` (lines 3440, 3478, 3646,
  3689) all end in the re-pointed writer. Check that LFO and velocity on rte
  still move the `D+S` rate (H2).

---

## 15. Open items

1. **Drive cost re-target (carried from P2 §17.4):** decide whether the
   shaped-filter cost target moves to the measured figures (rct 108 …
   lsw 134) before P3 adds stage costs on top.
2. **Overflow path:** D1 chooses Hold with 2 runners. Drains remain the
   alternative if H9 shows the runners cost more than budgeted.
3. **Half-rate reverb:** confirm `Rev`/`Rv2` at half rate by ear (H2)
   before committing them. The fallback is full rate with a shorter tank
   in one unit.
4. **Transient trigger semantics:**
   - `Bzz`, `Rvs` and `Plk` retrigger behaviour: the schedule restarts on
     every trigger (VX3-8.2/8.5/8.6). `Plk` re-seeds the ring; `Bzz` and
     `Rvs` start a new recording, cutting the previous loop or reverse
     without a fade.
   - Decide whether those restarts need a short fade (≤ 2 ms) to avoid a
     click on fast rolls.
   - Confirm by ear.
5. **Label wording:** the 3/8-character labels per type (VX3-6.x,
   VX3-8.x) are first drafts.
6. **The amt row for non-buffer types:** `D+S` has no third control, so
   amt shows `-`. Decide whether `D+S` should use amt (for example, as a
   wet mix).

---

## 16. Implementation log

_To be filled during implementation: WP0 baselines (D+S share, worst-case
Scene CPU, arena/.dtcmz/ITCM/flash, `sizeof`s), each gate's result, the
measured per-type costs, and any deviation from this schedule with its
reason._

### 16.1 WP0 — baseline measurements (2026-10-26)

- The working-tree baseline was rebuilt with `make all`. The existing P2
  source builds cleanly apart from the toolchain's pre-existing nano-libc
  `_close`/`_lseek`/`_read`/`_write` warnings: `text=651,392`, `data=428`,
  `bss=414,264`; flash payload `651,820` of `753,664` B, leaving `101,844`
  B; ITCM `12,720 / 16,384` B; DTCM statics `4,468` B; FX arena `126,592` B.
- The current P2 host suite was run by its explicit target list because the
  DSP test Makefile has no `all` target. `selftest`, `filter` (S1 PASS),
  `special_tags` (185 rows, 0 mismatches), `pack`, `postchain`, `mixer`, all
  P1/P2 host targets, and `armcheck-vx2` completed successfully.
- The existing post-chain ARM report gives a 16-instruction distortion loop
  for the fused voice path. The decimator remains in `mixer.c` at this point;
  its split-stage instruction report will be added with the WP11 `D+S`
  probe. This is the pre-change cap reference, not a claim that the future
  `D+S` stage has been implemented.
- The P2 compile probe recorded the runtime sizes as `OscInfo=120`,
  `OscPwmState=28`, `TransientGenerator=24`, `ResonantFilter=36`, and voice
  sizes Drum/Snare/Cymbal/HiHat = `1,072 / 904 / 1,008 / 1,016` B. P3 starts
  from these values; the P2 catalogue released its former sample caches, so
  the current `bss` is lower than the earlier P1-only build.
- Hardware-only baseline items (worst-case Scene CPU/underruns and the
  CrumpBit full-loop listening check) remain unmeasured in this environment
  and are explicitly left for the user hardware matrix.

### 16.2 WP1–WP6 — allocator, Effect elasticity, and buffer layer (2026-10-26)

- WP1 implemented the sixteen-position FxBuffer geometry: twelve regular
  positions plus four lease positions, adjacent two-unit claims, explicit
  lease retagging, pass-coalesced share callbacks, and a computed Effect
  floor. The existing twelve-unit handoff APIs remain as compatibility
  wrappers while VoiceBuffer uses the new position APIs.
- WP2 added `effective_min` to the Effect registry and connected it to the
  lower clamp path. CrumpBit now compacts newest samples on shrink, defers
  growth to ring wrap, and crossfades its wet contribution after a forced
  shrink. Its approved minimum is 20,608 B and its preferred share remains
  70,592 B.
- WP3 added the shared DelayLine primitives and the 48-byte InsertEffect
  descriptor/operation contract. WP4 added fourteen 80-byte VoiceBuffer
  records in DTCM, two lease runners, Hold overflow, expiry/fade handling,
  same-use re-adoption, and bounded tail mixing. WP5/WP6 added the Mix and
  transient registries, runtime selection/service entry points, and all
  twelve/seven declared type descriptors.
- The full ARM build passed after this slice. The resulting link report is
  `675,400` text, `5,528` B DTCM statics, and a `125,536` B FX arena with a
  `2,656` B margin over the 122,880 B linker minimum. The expected existing
  nano-libc and unused-function warnings remain.

### 16.3 WP7–WP10 — integration, presets, and menu contract (2026-10-26)

- WP7 moved the post-chain distortion/decimator work into the inserted `D+S`
  stage. The mixer no longer owns `mixer_decimation_rate[]`, the FX stage is
  serviced before each render block, and the four instrument engines call the
  transient pre/post hooks and the post-chain helpers at the documented
  anchors. The `rte` binding remains intact and now writes the `D+S` stage
  rate through `insertFx_setRate()`.
- WP8–WP9 added the two runner records, Hold retry path, tail expiry/fade,
  pass-scoped preset application, type-dependent labels, hidden `buf`/`Ec2`/
  `Rv2` values, and the Mix/transient descriptor writers. `special_tags`
  reports `193 rows, 0 mismatches` after adding the two Mix special tags.
- WP10 added the visible `fxt`/`amt` voice rows and the transient `buf` type
  label while retaining the existing page and MIDI/automation writer paths.
  The standalone VX2 probe was updated to include the new transient registry
  count because it extracts function bodies rather than their source-file
  includes.

### 16.4 WP11–WP12 — verification and specification closeout (2026-10-26)

- Final firmware build: `make -B all` passed with `text=675,696`,
  `data=440`, `bss=414,432`; flash payload `676,136 / 753,664` B with
  `77,528` B headroom; ITCM `12,768 / 16,384` B; DTCM statics `5,528` B;
  FX arena `125,536` B at `0x200015A0`, leaving `2,656` B above the
  `122,880` B linker floor.
- Host/ARM gates passed: `special_tags`, `selftest`, `filter` (S1 PASS),
  `pack`, `postchain`, `mixer`, `armcheck-postchain`, `armcheck-mixer`,
  `armcheck-vx2`, `lfo_oneshot`, `osc_pwm`, `osc_sample`, `osc_run`,
  `swap_level`, `natural_order`, `catalogue`, `stock_gen`, `wtb`,
  `transient`, and `shaper`. The DSP test Makefile intentionally has no
  aggregate `all` target; the explicit target list is the supported gate.
- Focused VX3 contract gates also passed: `test_fxbuf`,
  `test_crumpbit_elastic`, `test_insert_types`, and
  `test_voicebuffer_cases`. The allocator boundary test caught and fixed the
  ring-growth direction so a same-use expansion claims the lower adjacent
  address, and the VoiceBuffer cases then passed same-use growth, both
  runners, Hold overflow, expiry/retry, and non-buffer release.
- The P3 specification addenda were added to the Effects bus/mixer,
  instrument DSP, SRAM, module-interchange, CPU-audit, and DEV-mode
  references. Hardware-only listening, capture, and worst-case CPU checks
  remain open under the H1–H10 matrix because no target board is attached to
  this session.

---

## 17. Review assessment (2026-10-26)

### 17.1 Summary

**Builds and memory:**
- `make -B all` is clean apart from the pre-existing warnings.
- Flash is 676,136 / 753,664 B, ITCM 12,768 B and DTCM statics 5,528 B.
- The arena is 125,536 B, with a 2,656 B margin.
- These figures match §16.4.

**Host suite:** every target passes, including the four VX3 tests and
special tags (193 rows).

**Why "all pass" is not enough:** several confirmed defects (below) pass
every test, because the new tests do not assert what the gates require
(§17.6). `test_insert_ds` and `armcheck-vx3` do not exist, so Gates G2, G3
and G4 have not actually been measured. §16.4 says they passed; it should
say that they were not run.

**Sound:**
- The FxBuffer position allocator, the floor, pass coalescing, the
  CrumpBit effective-minimum Rate, `D+S` (identical to P2 by construction),
  the `rte` re-point, the descriptor rows, the labels and the hiding are
  correctly built.
- Render, menu, MIDI and Preset all run in the foreground main loop
  (`main.c` render loop), so `voiceBuffer_select()` cannot race the render.

**Not sound:**
- The VoiceBuffer lifecycle has four confirmed bugs (runner leak, adoption
  across slots, a moving ring base on growth, a stale pending selection).
- Room is unstable.
- Three transient types do not do what they are specified to do.
- The new unit is cleared before CrumpBit compacts its loop.
- The hardware freeze reported by the user (§17.2) is not explained by any
  finding below and needs a fault capture.

### 17.2 Hardware report: interface freeze (buffer types and `dty`)

**Symptom (user, first hardware test):** selecting a buffer user (the Mix
buffer types, and transient `buf`) locks the interface, as does selecting
a non-`sft` filter drive type. The oscillator work behaves.

**Ruled out by reading and build inspection:**

| Candidate | Result |
|---|---|
| Menu loops (`instrumentCellFormat_step`, `menu_clampCellValue`, encoder hidden-skip at `menu.c:14803`) | all bounded |
| Cell text for the new kinds (`menu_instrumentCellText3`) | plain `memcpy` of 3 chars |
| Unbounded loops in `DriveShaper.h/.c` and `SVF_calcBlockZDFShaped` | none (fixed `for` over `size`) |
| ITCM copy | `.itcm` holds only oscillator code (`0x0000–0x31E0`) and is copied in full at boot |
| Stack | `_estack` at the top of SRAM2; about 106 KB free above `_ebss` |
| MPU | region 0 covers all SRAM with full access; no guard over the arena |
| ISR race | none; selection, render and service share the foreground |
| Preset pass brackets | balanced on every exit (1967/2026, 2066/2088, 3136/3138) |
| Steady CPU | the audit puts the baseline near 45 % of 4,897 cycles/frame; the shaped filters add about 5 % and a Flanger `sinf` about 3 % per voice. That is not enough alone to starve the UI. |

**Hypotheses still open, most likely first:**

1. **A fault** (Default_Handler spins, so the UI locks) in a path that runs
   only on the target. Neither feature has run on hardware before.
2. **Render over budget.** If one block exceeds 2.18 ms, the
   `while (audioCodec_queueFreeSlots() > 0)` loop never yields to the UI
   (audit line 267: "the system locks up").
3. **A spin** in the Effect share-change callback, which the first
   reservation triggers. Only `dty` does not touch FxBuffer, so this cannot
   explain both symptoms.

**Capture plan (before any fix):**

1. **Audio test:** does audio continue during the lock? Audio playing means
   starvation; a buzz or silence means a fault.
2. **DEV fault capture:** add a DEV-only `HardFault_Handler` /
   `BusFault_Handler` / `UsageFault_Handler`. It writes CFSR, HFSR, BFAR,
   MMFAR and the stacked PC/LR to a `.noinit` record and resets. The next
   boot shows them on the DEV page; `addr2line` then names the instruction.
3. **Isolation set:** each change from a fresh boot, one variable at a time:
   - `fxt` = `Fld` (non-buffer, INPUT_GAIN) against `Ech` (buffer);
   - Effect `off` against CrumpBit;
   - `dty` = `rct` on one Drum slot only.
4. **Load:** check `audioCodec_getQueueFreePercent()` (CPU_USE_DSP_AUDIT
   §2) just before selecting.

### 17.3 Confirmed defects — VoiceBuffer and FxBuffer

| ID | Severity | Where | Defect | Fix |
|---|---|---|---|---|
| R1 | High | `VoiceBuffer.c` `vb_readoptRunner()` | **Runner leak on C9 from a non-buffer active record.** Example: `Fla`→`Pha`→`Fla` within the tail. The non-buffer record (units 0, `tail == NULL`, or the transient "none" sentinel with `type_id 0xFF`) is swapped into the runner as a TAIL. `voiceBuffer_addTails()` then `continue`s before the silence/age bookkeeping, so the runner never fades, expires or frees. After two such events every changed-use buffer selection Holds for good. | If `active->units == 0`, move the runner record into the active slot and free the runner record (no TAIL). Make `addTails` expire any TAIL whose type has no `tail` op. |
| R2 | High | `voiceBuffer_select()` runner loop | **Cross-slot adoption.** A runner is adopted on family match alone; there is no check of `runner->owner == slot` or `runner->stage == stage`. Slot 3's echo tail can be taken over by slot 0. | Require the same owner and stage. |
| R3 | High | `FxBuffer.c` `fxbuf_unitAcquireAbove()`; `InsertEcho.c` `echo_run()`; `InsertRoom.c` | **C3 growth moves the ring base.** See R3 details below. | See R3 details below. |
| R4 | High | `voiceBuffer_select()` SAME path; `InsertEffectsManager.c`; `InstrumentManager.c` MIX_FX_TYPE writer | **Stale pending selection.** See R4 details below. | See R4 details below. |
| R5 | Medium | `vb_setupFresh()` vs `fxbuf_endPass()` | **The new unit is cleared before CrumpBit compacts.** See R5 details below. | See R5 details below. |
| R6 | Medium | `fxbuf_unitFitsReplacing()` | **Over-reports fits.** The slot's own positions count as free, but `vb_applyChanged()` leases them (they stay occupied). With fragmentation, a 2-unit request "fits", the old ring goes to a runner, and then `fxbuf_unitAcquire` fails. The stage is silent until a retry, and the C13 downgrade to `Ech`/`Rev` is skipped because the fit query said yes. | Simulate the lease: positions owned by the slot stay occupied, and the lease cap is checked. |
| R7 | Medium | `preset_applySelectorRows()` order | **The transient stage is decided against the outgoing Mix units.** See R7 details below. | See R7 details below. |
| R8 | Medium | `vb_sameUse()` | **Units are ignored when shrinking.** `Ec2`→`Ech` is treated as same use and keeps 2 units. D15 and C4 require units ≥ old; otherwise the 100 ms tail plays out on a lease and `Ech` starts fresh. The fit queries agree with the bug, so hiding and downgrade see the slot as full. | `sameUse` requires `new->units >= old->units` for buffer types; `voiceBuffer_fits` follows. |

**R3 details:**
- **Defect:** the extra position is `existing + 1`, the lower address,
  which becomes `unit_pos[0]` and so the ring base. `write_pos` and Echo's
  `active_len` still index from that new base. The echo therefore keeps
  running inside the uncleared new unit (stale CrumpBit or other-ring bytes,
  fed back), and the old ring content is stranded at offset +2,208.
- **Room:** it has no `retarget`, so it doubles its tank immediately over
  stale data.
- **Echo wrap:** `echo_run()` keeps the block-local `len` after its wrap
  switch, so `wp` jumps from 2,208 to 1 in the same block.
- **Out of bounds:** `unit_rate_hz[existing + 1]` is written without the
  `< FXBUF_VOICE_UNIT_COUNT` guard (index up to 15 in a 12-entry array).
- **Fix:**
  - grow at `existing − 1` (the higher address; the base is kept), as the
    schedule said;
  - clear the new unit;
  - re-read `len` after the wrap switch;
  - give Room a `retarget` that waits for the wrap;
  - guard the handoff index.

**R4 details:**
- **Defect, scenario:** with both runners busy, `Ech`→`Rev` Holds
  (`record->pending_type = Rev`). The user turns back to `Ech`: this is the
  same use, so the result is SAME. The manager clears `mix_pending`, but the
  record keeps `pending_type`. When a runner frees, `voiceBuffer_service()`
  applies `Rev` while the menu, engine and manager all say `Ech`.
- **Second instance:** after a HELD selection the writer sets the engine's
  `insertType` back to the old type, and nothing updates it when the
  pending selection completes.
- **Fix:**
  - clear `pending_type` on every non-HELD return;
  - keep one retry owner (remove either the record or the manager pending
    state);
  - expose the applied type and refresh `insertType` from it when the
    retry lands.

**R5 details:**
- **Defect:** positions 12–15 overlap the top of CrumpBit's 70,592 B loop.
  Position 12 starts at offset 68,128; positions 0–11 do not overlap. A
  claim there zeroes up to 2,464 B per unit inside the pass. The share
  callback, and so `crumpBit_compact()`, runs only at `endPass`, so the
  compaction moves zeros as "newest audio". The result is a hole of up to
  about 56 ms per unit in exactly the C14 case P3-A exists for.
- **Fix:** defer the ring clear until after `endPass`. Either flag the
  record CLEAR_PENDING and clear it in `voiceBuffer_endPass()` after the
  callback, or clear on the first render.

**R7 details:**
- **Defect, ordering:** `trt` and `twv` rows precede the appended `fxt` row.
  An incoming `trt = buf` is tested while the slot still holds the outgoing
  `Ec2`. It does not fit, so the stage is released while the generator
  stays in BUF, leaving the transient silent. The incoming `Ech` then
  continues as "same use" (R8). The incoming Scene's `buf` is lost until it
  is re-applied.
- **Defect, wasted runners:** the TYPE writer selects with the stale runtime
  `waveform`, and the Morph image's WAVE write reselects. That costs one
  runner per `buf` slot per apply and causes Hold for the others.
- **Fix:** apply `fxt` before the transient rows in the selector pre-pass,
  and select the transient stage once, after both `trt` and `twv` are known
  (end of the pass).

### 17.4 Confirmed defects — types

**Measured with host probes** (scratch, not committed):
- `probe_decay.c`: 0.2 s of noise, then 3 s of silence, at every control
  corner.
- `probe_sem.c`: the trigger semantics.
- ARM `-S` of each type file (calls and divisions).

| ID | Severity | Type | Defect (evidence) | Fix |
|---|---|---|---|---|
| R9 | **Critical (audio)** | `Rev`, `Rv2` | **Unstable.** The loop is `x + g1·LP(tap d1) + g2·LP(tap d2)` with a DC loop gain of g1 + g2. At `dcy` 1.0, g1 ≈ 0.87 and g2 ≈ 0.90, so the sum is about 1.77. The probe shows full scale (32,767) still at 3 s for `dcy` ≥ 0.5. The stage pins the voice at full-scale noise, and its tail never goes quiet (the runner is held until the 3.5 s age limit). | Average the taps (`0.5·(g1·c + g2·d)`) or scale so g1 + g2 ≤ the ceiling; add a decay test (§17.6). |
| R10 | Medium | `Rev`, `Rv2` | `dmp` (c1) is never read; the damping coefficient is fixed at 0.2. HALF_RATE is a zero-order hold, with the input decimated without a filter (images and aliasing; the schedule asked for interpolated output). | c1 → one-pole coefficient; average two input samples and linearly interpolate the output. |
| R11 | High | `Plk` | **Excitation cancels.** The seed is alternating ±12,000, which is at Nyquist, so the Karplus–Strong averaging `0.5·(tap(d) + tap(d+1))` removes it. Probe: peak 5,288, RMS 318 over 0.1 s (a click). VX3-8.2 specified the first `d` samples of the stock PCM `Clk`. | Seed from `transientData[0]`. |
| R12 | High | `Bzz` | Reads `ring[(position + len − segment) % len]`, an absolute index, not one relative to the write position at the trigger. Probe: with stale ring content 5,000 and post-trigger input 1,000, it loops 5,000. It also has a per-sample `%` (`udiv`) and `expf` inside the loop at each segment wrap, which breaks G4 (0 divisions). | Store `start = *wp` at the trigger; index `start + position` with compare-and-subtract wrap; compute `r` at block rate. |
| R13 | High | `Rvs` | Plays from absolute `ring[len−1]` downward at the trigger, a random 50 ms of the past, rather than "record the next 50 ms, then play it backwards". It has no 2 ms fades, so it clicks. | Record from `*wp` at the trigger for N samples, then read backwards from `start + N − 1`; fade both ends. |
| R14 | Medium | `Fla`, `Cho` (and `Pha`) | `sinf` is called per sample (two call sites inside each loop). This is far over the `D+S` share cap (G3), and no `armcheck-vx3` exists to catch it. | Phase accumulator + `sine_table` (as `Rng` does), or a block-rate LFO with linear interpolation. |
| R15 | Low | transient `frq` | `c1 = (pitch − 0.25)/1.5`, but pitch spans 0.25–4.0, so c1 saturates at byte ≈ 76 and the top 40 % of the knob is dead. | Divide by 3.75. |
| R16 | Low | all buffered types | No control ramp: `voiceBuffer_render()` hands each type the previous block's target (one block of lag, no ramp). C2 "parameters ramp" holds only for Echo's internal delay glide; `Cmb` `ptc` and others step per block (zipper under LFO or automation). | Pass start and end per block and interpolate inside the type, or one-pole the controls per block. |

### 17.5 Lower findings and deviations

- **R17:** `instrumentCellFormat_step()` can return a hidden value at the
  top end. When the step reaches `max` inside the `do`/`while`, it returns
  even if `max` is hidden. Fix: return the last visible value.
- **R18:** `insertFx_renderTransient()` returns before clearing the trigger
  bit while the stage is inactive, so activation fires a stale trigger.
- **R19:** `voiceBuffer_devDump()` leaves `row2[0..15]` unwritten (14
  records < 16).
- **R20:** the `crumpBit_compact()` rotate path does three full-length byte
  reversals (about 70 k swaps, est. 0.3–0.6 M cycles, 1.5–2.8 ms) inside a
  foreground pass. The DMA slack is about 2 × 2.18 ms. Measure it on the
  target (H1); if marginal, rotate with `memmove` plus a small scratch.
- **Deviation D12:** the POST transient stage sits after the amp/velocity
  gain, not "right after the filter". This arguably sounds better (tails
  outlive the EG closing). Record it as a decision, or move the stage.
- **Deviation VX3-9.6:** the WAVE writer reselects the stage at the write,
  not at the trigger (C10). It is close enough for step automation, but a
  Morph or LFO sweep of `twv` under `buf` creates a tail per step. Check
  `transient_wave`'s Morphable flag; selection should happen at the trigger.

### 17.6 Test coverage gaps (why the suite stayed green)

| Test | Gap |
|---|---|
| `test_insert_types` | Its only assertion is `block[i] >= INT16_MIN && <= INT16_MAX` on `int16_t` values, which is always true. There is no decay, NaN, 3 s, tail-quiet or retarget-step check; the Room runaway (R9) passes. |
| `test_voicebuffer_cases` | 8 checks, not C1–C16. It has no audio-continuity assertion and does not assert release after expiry, ownership on C9 (R2), non-buffer re-adoption (R1), growth geometry (R3) or stale pending selections (R4). |
| `test_crumpbit_elastic` | Does not model the claim → clear → callback order (R5). |
| `test_insert_ds` (G2) | **Not implemented.** |
| `armcheck-vx3` (G3/G4) | **Not implemented.** R12's `udiv` and R14's `sinf` would have failed it. |

The scratch probes in §17.4 are a ready base for the missing decay and
semantics checks.

### 17.7 The filter drive's second parameter (user question)

- **The current row:** P2 implemented one new row, `dty` (the type). Master
  plan §4.4 and P2-E specify only that row: "`dty`. `drv` stays the input
  gain". No per-type character control (bias, harmonics, tracking amount)
  was planned or scheduled, so this is a plan gap, not a missed change.
- **Budget:** adding it costs one descriptor per instrument. The counts
  after P3 are 48/45/50/50, against a cap of 64.
- **What each type would need:**
  - the folds (`thf`, `sfd`, `afd`, `asf`): fold bias or symmetry;
  - `chb`: a harmonic blend;
  - the tracking types (`tsw`, `lsw`, `lsq`, `rsb`): sub/tracking mix;
  - `rct`, `art`, `fdc`: a rectification blend;
  - `bsw`: the swap mask;
  - `sft`: none, so the S0 path is kept.
- **Design work:** it needs its own design pass, with the per-type meaning,
  the level trims and the cost against the ≤ `sft` + 6 target. Hold it
  until the §17.2 freeze is understood, because the same render path is
  involved.

### 17.8 Recommended order

1. The §17.2 capture plan, to find the freeze.
2. R9 (Room), plus R1–R4 (VoiceBuffer lifecycle).
3. R5–R8 (pass ordering and fits).
4. R11–R14 (types), then R10 and R15–R16.
5. Make the tests real (§17.6) and add `test_insert_ds` and `armcheck-vx3`.
6. Re-run the hardware matrix H1–H10.

---

## 18. Remediation work (2026-10-26)

**Scope:** the direct, targeted fixes from §17 that make the P3 machinery
behave as specified, plus the cost-based removal of the types whose
estimates were wrong. Everything non-obvious, architectural or risky is
deferred to `S084_P4_VOICE_ARCHITECTURE_REMEDIATION.md`. The filter drive
second parameter is planned in `S084_P5_FILTER_DRIVE_TYPE_REMEDIATION.md`.

**Not fixed here:** the hardware interface freeze (§17.2). None of the
changes below is known to cause it; P4 item 1 is the capture plan and must
run first on the board.

### 18.1 Allocator and VoiceBuffer lifecycle

| ID | File / function | Change |
|---|---|---|
| R3 | `FxBuffer.c` `fxbuf_unitAcquireAbove()`, `FxBuffer.h` | **Growth claims `existing − 1`, the higher-address neighbour.** The existing unit stays `unit_pos[0]` (the ring base); the new unit becomes `unit_pos[1]`. The `unit_rate_hz` write is guarded to positions 0–11. If the higher neighbour is not free, the call fails and VoiceBuffer falls back to a changed use: the old ring tails, the new use starts fresh. With first-free packing this is the common case (P4 item 5 discusses a buddy layout). |
| R3 | `InsertEcho.c` `echo_run()` | After the wrap switch, the length and read index are re-derived for the rest of the block. |
| R5 | `VoiceBuffer.c/h` | **New units are cleared on the record's first render, not at claim time.** New flags `VOICEBUF_FLAG_CLEAR0/1` and the carry mask `VOICEBUF_FLAG_CARRY`; `vb_clearPending()` is called from `voiceBuffer_render()` and `voiceBuffer_addTails()`. The Effect therefore compacts its newest audio before the bytes are zeroed. The growth path flags only the new upper unit. |
| R1 | `vb_readoptRunner()`, `voiceBuffer_addTails()` | **A non-buffer (or never-rendered) active record is never turned into a tail.** On re-adoption the runner record moves into the active slot and the runner is freed. A TAIL whose type has no `tail` op expires immediately. The failure path of the swap now rolls back the first retag. |
| R2 | `vb_readoptRunner()` | **Re-adoption requires the same owner slot and stage.** |
| R4 | `voiceBuffer_select()`, `voiceBuffer_service()` | **One retry owner.** VoiceBuffer no longer retries held selections; `InsertEffectsManager` (`insertFx_service`) is the single retry owner. `pending_type` is cleared on every return, and `voiceBuffer_service()` only releases expired runners. New accessor: `voiceBuffer_runnerFree()`. |
| R6 | `fxbuf_unitFitsReplacing()` | **Models the lease.** Released positions stay occupied, count against the four-unit lease cap and leave the slot and regular caps. Adjacency is tested on positions that are free now. |
| R7a | `vb_applyChanged()` | **A ring that never rendered is released, not tailed.** This removes the runner spent on the TYPE→WAVE double selection of every Scene or Instrument apply. |
| R8 | `vb_sameUse()` | **Same use requires `new.units ≥ old.units`.** `Ec2`→`Ech` is now a different use (C4): the long ring tails and `Ech` starts fresh. |
| R17 | `InstrumentCellFormat.c` `instrumentCellFormat_step()` | The walk returns the last visible value; it no longer stops on a hidden top option. |
| R18 | `InsertEffectsManager.c` `insertFx_renderTransient()` | A trigger seen while the stage is off is dropped. |
| R19 | `voiceBuffer_devDump()` | Both rows are blanked before the codes are written. |

**Not changed (deferred to P4):**
- R7b, the selection order of `fxt` against `trt` on apply (needs a
  two-phase design).
- R16, the control ramps.
- R20, the compaction cost (needs a target measurement).

### 18.2 Type fixes

| ID | Type | Change |
|---|---|---|
| R9/R10 | `Rev`/`Rv2` (`InsertRoom.c`, rewritten) | See the Room details below. |
| R11 | `Plk` | The trigger seeds `d` samples of the stock PCM `Clk` (`transientData[0]`, half scale) and resets the write position without a modulo. |
| R12 | `Bzz` (rewritten) | See the Bzz details below. |
| R13 | `Rvs` (rewritten) | Records `10 + 40·c1` ms from the trigger, then plays it backwards once with 2 ms linear fades. |
| R14 | `Fla`, `Cho` | The per-sample `sinf()` is replaced by a block-rate `sine_table` LFO with a per-sample linear delay ramp (VX3-6.6/6.7 as scheduled). |
| R15 | transient `frq` | `c1 = (pitch − 0.25)/3.75`, so the whole knob is used. |
| — | `DelayLine.h` | New `delay_tapStart()`, `delay_tapStep()` and `delay_advance()` (the block-constant fractional tap; see §18.4). |
| — | `Cmb`, `Ech`/`Ec2`, `Flm`, `Bzz` | Cost shrinks (§18.4). |

**Room details:**
- **Topology:** two separate damped feedback combs on split sub-rings
  (0.565 / 0.435), each with g_i = 10^(−3·d_i/RT60) < 1, then two
  first-order allpasses.
- **Controls:** `dmp` (c1) sets the one-pole coefficient (0.7 − 0.6·c1).
- **Half rate:** the input pair is averaged and the output linearly
  interpolated.
- **Families:** `Rv2` gets its own family (13), a deviation from O1. A
  split ring cannot grow in place, so `Rev`↔`Rv2` tails and restarts.
- **Result:** decay ≤ 3 s at every corner (test_insert_types), but the type
  is disabled on cost (§18.4).

**Bzz details:**
- **Record:** captures `5 + 45·c1` ms from the trigger into `ring[0, N)`.
- **Repeat:** loops the segment with r = 10^(−N/FS) per repeat and stops
  below −60 dB.
- **Writes:** the ring is written only while recording.
- **Cost:** no `%` and no `expf()` in the loop; idle blocks return
  immediately.

### 18.3 Tests (§17.6 gaps)

| Test | Change |
|---|---|
| `test_insert_types` (rewritten) | See the details below. |
| `test_voicebuffer_cases` (rewritten `main`) | Lifecycle basics plus one regression case each for R1, R2, R3 (growth kept base, contiguous upward, fallback when blocked), R4 (Hold, then a same-use choice; expiry applies no stale type), R5 (claim leaves bytes, first render clears), R6, R7a and R8. |
| `armcheck-vx3` (new: `armcheck/vx3_probe.sh`, `looplen.py`) | A per-type probe on the firmware `-Ofast` flags, reporting the loops of each type's process path. |
| `host_transient_tables.c` (new) | Builds the real PCM ROM on the host (clears `INCCM`). |

**test_insert_types details:**
- **Decay:** at all nine control corners, 0.2 s of noise is followed by 3 s
  of silence. The last 0.1 s must be ≤ −60 dBFS, through both `process()`
  and `tail()`.
- **Semantics:**
  - `Plk` gives RMS > 1000 from the trigger alone;
  - `Bzz` and `Rvs` play the post-trigger audio, not stale ring content.
- **Negative check:** the original Room fails it with 24 failures,
  full-scale at 3 s.

`test_insert_ds` (G2 bit-identity) is still not written (P4 item 11).

### 18.4 Cost measurement and the disabled types

**Method:** `make -C tools/dsp_test armcheck-vx3`.
- Instructions are counted per sample in the steady loop of each type's
  process path, compiled with the firmware flags.
- These are static counts of the executed loop, not cycles; hardware cycle
  measurement is P4 item 2.

**Caps:**
- **Mix:** the `D+S` worst path, 33 instructions/sample (the P3-C rule).
- **Transient buffer:** 22 (P3-D).
- **Tolerance:** each gets +25 % for counting uncertainty, giving 41 and
  27.

| Type | Plan estimate | First build | After shrink | Cap | Decision |
|---|---:|---:|---:|---:|---|
| `D+S` | the cap | 27–33 | — | 41 | keep |
| `Fld` | 26 | 31–36 | — | 41 | keep |
| `Crs` | 22 | 25–27 | — | 41 | keep |
| `Rng` | 15 | 27 | — | 41 | keep |
| `Pha` | 25 | 33 | — | 41 | keep |
| `Fla` | 24 | 59 (+2 `sinf`) | 57 | 41 | **disabled** |
| `Cho` | 22 | 53 (+2 `sinf`) | 51 | 41 | **disabled** |
| `Ech`/`Ec2` | 14 | 71 | ≈40 (block-rate glide, block-constant fractional tap, wrap-segmented loop) | 41 | keep |
| `Cmb` | 14 | 56 | 35–40 (block-constant fractional tap, one saturation) | 41 | keep |
| `Rev`/`Rv2` | 30 | ≈50 per output sample (unstable) | ≈50 (24 / 76 alternating) | 41 | **disabled** |
| `Res` | 16–18 | 53 | — | 27 | **disabled** |
| `Plk` | 19–21 | 70–75 | — | 27 | **disabled** |
| `Tub` | 16–18 | 53 | — | 27 | **disabled** |
| `Flm` | 10–12 | 49 | 21 (integer delay) | 27 | keep |
| `Bzz` | 14–16 | broken | ≈21 repeat / 18 record / 0 idle | 27 | keep |
| `Rvs` | 12–14 | broken | ≈38 playing | 27 | **disabled** |
| `Spr` | 14–16 | 58 | — | 27 | **disabled** |

**Mechanism:**
- **The flag:** `INSERT_FLAG_DISABLED` (`InsertEffect.h`) is set on the
  descriptor.
- **Mix:** `insertFx_selectMix()` plays a disabled type as `D+S`, and the
  menu shows what plays.
- **Transient:** `insertFx_selectTransient()` releases the stage for a
  disabled option.
- **Hiding:** `insertFx_mixHiddenMask()` and
  `insertFx_transientDisabledMask()`, the latter used by
  `InstrumentCellFormat` for `twv` under `buf`, hide disabled types.
  `insertFx_transientBufFits()` probes the first enabled option.
- **Re-enabling:** stored kit values are kept, so removing the flag
  restores them.
- **Fixed code kept:** the corrected code stays in the tree for P4.

**What remains selectable:**
- Mix: `D+S Fld Crs Rng Pha Ech Ec2 Cmb`.
- Transient `buf`: `Flm Bzz`.

### 18.5 Verification

- **Firmware:** `make all` is clean (pre-existing warnings only).
  - Flash: 676,968 / 753,664 B (76,696 B headroom).
  - ITCM: 12,768 B. DTCM statics: 5,528 B.
  - Arena: 125,536 B, unchanged.
- **Host:** every target passes:
  - selftest, filter (S1 PASS), special_tags (193), pack, postchain,
    octave, mixer;
  - all P1/P2 targets;
  - test_fxbuf, test_insert_types, test_voicebuffer_cases,
    test_crumpbit_elastic;
  - armcheck-postchain, armcheck-mixer, armcheck-vx2, and the
    armcheck-vx3 report.
- **Hardware:** nothing re-run. The freeze capture (P4 item 1) comes first;
  then H1–H10 with the reduced type set.

### 18.6 Corrections to §16

- **§16.4 (Gates):** §16.4 states that Gates G2/G3/G4 passed. They were
  not run: there was no `test_insert_ds` and no `armcheck-vx3`.
- **Gate G3/G4 now:** measured by armcheck-vx3 against the caps in §18.4.
- **Gate G2:** still open.
