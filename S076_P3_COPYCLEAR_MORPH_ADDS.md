# S076 P3 — Copy/Clear Morph Additions

Implementation plan for morph-targeted copy and clear operations on TRACK
buttons and PERF Scene buttons. Adds five new selections to the existing
Phase 6 copy/clear framework.

- **Current as of:** Session 076 (2026-10-06), branch `dev-ph6-cleanup`.
- **Reference:**
  - `COPYCLEAR_UTILITIES.md` — the as-built copy/clear spec;
  - `BANK_PRESET_ARCHITECTURE.md` §3 (SceneData), §5 (Instrument images), §8
    (Morph engine);
  - `EFFECTS_BUS_REFERENCE.md` §6 (Effect Morph);
  - `INSTRUMENTS_DSP_REFERENCE.md` §4.4 (morphable flags).

---

## 1. Summary of additions

| Mode | Gesture | Label | What it does |
|------|---------|-------|--------------|
| VOICE/STEP/PERF/EFFECTS | **Clear TRACK** | `reset morph` | Equalise: set the pressed track's voice slot morph endpoint to its current Normal endpoint, plus the voice's correlated Scene morph endpoints (FX send morph, Kit slot-6 decay morph). Fans out through the edit mask. |
| VOICE/STEP/PERF/EFFECTS | **Copy TRACK** | `morph` | Copy the source track's Normal endpoints onto the destination track's Morph endpoints (instrument + correlated Scene params). Silently skipped for a slot whose instrument type does not match. Fans out through the edit mask. |
| PERF | **Clear Scene** | `reset morph` | Equalise the entire Scene: all 6 instrument slots, all correlated Scene-level morph endpoints (FX send morph ×6, Kit decay morph), and all morphable Effect morph endpoints. Does NOT fan out. |
| PERF | **Clear Scene** | `reset fx morph` | Equalise only the Effect's morphable parameter morph endpoints to their Normal endpoints. Fans out through the edit mask (parallels `clear fx`). |
| PERF | **Copy Scene** | `scene morph` | Copy the source Scene's Normal endpoints onto the destination Scene's Morph endpoints for all matching-type components (instruments, FX send, Kit decay, Effect). Silently skip instruments and/or the Effect whose types differ. Does NOT fan out. |

---

## 2. Data touched by each operation

### 2.1 Morph endpoint data in a Scene

All endpoints listed below have Normal/Morph pairs. The operations set the
Morph value from the Normal value (reset) or from another Scene's Normal
value (copy).

| Data | Normal source | Morph target | Condition |
|------|--------------|--------------|-----------|
| Instrument descriptor images (per slot) | `instrument_parameters[i]` | `morph_instrument_parameters[i]` | `flags & INSTRUMENT_PARAM_FLAG_MORPHABLE` on the slot's descriptor table |
| FX send (per slot) | `fx_send_amount[slot]` | `fx_send_morph[slot]` | always |
| Kit slot-6/track-7 decay | `slot6_track7_amp_envelope_decay` | `slot6_track7_morph_amp_envelope_decay` | only when the slot is slot 6 |
| Effect parameter images | `effect_record.normal[i]` | `effect_record.morph[i]` | `effects_paramMorphable(type, i)` returns nonzero |

### 2.2 What is NOT changed

- **Morph amounts** (`voice_morph_amount[]`, `effect_morph_amount`, the global
  `morph_amount`): these stay as-is. The purpose of the operation is to
  equalise endpoints so a non-zero morph amount produces no change (or, for
  the copy, produces a morph that sweeps into the source's normal sound).
- **Step automation overlays** (`morph_step_override[]`, Effect overlays):
  transient, not touched.
- **LFO modulation state**: transient, not touched.
- **Names and HCNAMES**: identity does not change.
- **Pattern data**: none of these operations affect Pattern content.
- **Instrument types, supplemental parameters, routing, targets**: unchanged.

---

## 3. Menu changes

### 3.1 Clear menus

**`CC_MENU_CLEAR_TRACK` (VOICE/STEP/PERF):**

| Sel | Current label | New? |
|-----|---------------|------|
| 0 | `cancel` | — |
| 1 | `track` | — |
| 2 | `track auto` | — |
| 3 | `track notes` | — |
| 4 | `reset morph` | **new** |

**`CC_MENU_CLEAR_TRACK_FX` (EFFECTS):**

| Sel | Current label | New? |
|-----|---------------|------|
| 0 | `cancel` | — |
| 1 | `track` | — |
| 2 | `track auto` | — |
| 3 | `track notes` | — |
| 4 | `send` | — |
| 5 | `reset morph` | **new** |

New enum value: `CC_CLEAR_RESET_MORPH` (in `cc_clear_obj_sel_t`, after
`CC_CLEAR_SEND`).

**`CC_MENU_CLEAR_SCENE` (PERF Scene):**

| Sel | Current label | New? |
|-----|---------------|------|
| 0 | `cancel` | — |
| 1 | `scene` | — |
| 2 | `settings` | — |
| 3 | `pattern` | — |
| 4 | `automation` | — |
| 5 | `notes` | — |
| 6 | `fx` | — |
| 7 | `fx sequence` | — |
| 8 | `reset morph` | **new** |
| 9 | `reset fx morph` | **new** |

New enum values: `CC_CLEAR_SCENE_RESET_MORPH`, `CC_CLEAR_SCENE_RESET_FX_MORPH`
(in `cc_clear_scene_sel_t`).

### 3.2 Copy menus

**`CC_MENU_COPY_TRACK`:**

| Sel | Current label | New? |
|-----|---------------|------|
| 0 | `track` | — |
| 1 | `instrument` | — |
| 2 | `morph` | **new** |

New enum value: `CC_COPY_MORPH` (in `cc_copy_track_sel_t`, after
`CC_COPY_INSTRUMENT`).

**`CC_MENU_COPY_SCENE`:**

| Sel | Current label | New? |
|-----|---------------|------|
| 0 | `scene` | — |
| 1 | `settings` | — |
| 2 | `kit` | — |
| 3 | `effect` | — |
| 4 | `pattern` | — |
| 5 | `scene morph` | **new** |

New enum value: `CC_COPY_SCENE_MORPH` (in `cc_copy_scene_sel_t`, after
`CC_COPY_PATTERN`).

---

## 4. Executor design

### 4.1 `ccClear_runResetMorphTrack(job)` — Clear TRACK "reset morph"

**Location:** `clearOps.c`.

**Logic (per fan-out destination Scene from `bank_sceneFanoutMask()`):**

1. Wait for `preset_applyWorkersIdle()` if the active Scene is written.
2. Read the slot's instrument descriptor table
   (`instrumentManager_getDescriptorTable(type, &count)`).
3. For each descriptor index `i` where
   `descriptor[i].flags & INSTRUMENT_PARAM_FLAG_MORPHABLE`:
   set `morph_instrument_parameters[i] = instrument_parameters[i]`.
4. Set `fx_send_morph[slot] = fx_send_amount[slot]` through
   `preset_setVoiceFxSendMorph()` (or the SceneData setter with dirty mark).
5. If the slot is slot 5 (0-indexed, track 6):
   `kit.settings.slot6_track7_morph_amp_envelope_decay =
    kit.settings.slot6_track7_amp_envelope_decay` through
   `scene_setSlot6Track7MorphAmpEnvelopeDecay()`.
6. Mark dirty: whole-instrument Morph AutoSave marker
   (`autosave_markInstrumentMorphDirty(scene, slot)`), FX-send Morph cell
   (`autosave_markSceneDirty()` for the send-morph region).
7. If the active Scene was written: queue the voice morph worker
   (`presetMorph_requestVoice(slot)`).

**Returns:** `CC_RUN_DONE`.

### 4.2 `ccCopy_runMorphTrack(job)` — Copy TRACK "morph"

**Location:** `copyOps.c`.

**Logic:**

1. Wait for `preset_applyWorkersIdle()` if the active Scene is written.
2. Read source instrument type from `src_scene->kit.instruments[src_slot].type`.
3. For each fan-out destination Scene from `bank_sceneFanoutMask()`:
   a. Read destination slot type. If types differ → skip this member.
   b. For each morphable descriptor: set destination
      `morph_instrument_parameters[i] = source.instrument_parameters[i]`.
   c. `dst.fx_send_morph[dst_slot] = src.fx_send_amount[src_slot]`.
   d. Slot 6 → slot 6: Kit decay morph = source Kit decay normal.
   e. Dirty mark, queue morph worker for the active Scene.
4. Revalidation: not needed (types are unchanged).

**Returns:** `CC_RUN_DONE`.

**Type mismatch:** If source and destination instrument types differ, the
entire paste for that member is silently skipped (no block needed; a
different type's descriptor indices are meaningless).

### 4.3 `ccClear_runResetSceneMorph(job)` — Clear Scene "reset morph"

**Location:** `clearOps.c`.

**Logic (single Scene, no fan-out):**

1. Wait for `preset_applyWorkersIdle()` if this is the active Scene.
2. For each of the 6 instrument slots: reset morph image from Normal image
   (morphable descriptors only), same as §4.1 step 3.
3. For all 6 slots: `fx_send_morph[slot] = fx_send_amount[slot]`.
4. Kit decay morph = Kit decay normal.
5. For each morphable Effect parameter:
   `effect_record.morph[i] = effect_record.normal[i]`.
6. Dirty mark: all 6 instrument morph regions, FX-send morph cells, Kit
   morph decay, Effect region.
7. If active Scene: queue all 6 voice morph workers + Effect morph rebuild
   (either `preset_startDrumsetApply()` or 6 ×
   `presetMorph_requestVoice()` + `effects_activateScene()`).

**Returns:** `CC_RUN_DONE` (or `CC_RUN_WAIT` while workers drain for an
active Scene).

### 4.4 `ccClear_runResetFxMorph(job)` — Clear Scene "reset fx morph"

**Location:** `clearOps.c`.

**Logic (fans out through destination Scene's edit mask, paralleling
`clear fx`):**

For each fan-out destination Scene:
1. For each Effect parameter index `i` where
   `effects_paramMorphable(type, i)`:
   set `effect_record.morph[i] = effect_record.normal[i]`.
   (Write through an EffectsManager API or direct SceneData write with
   mask gate, same as `effects_pasteRecord()` does.)
2. Mark the Effect region dirty
   (`autosave_markEffectDirty(scene)` / `scene_finishEffectWholeCommit()`).
3. If the active Scene was written: rebuild Effect morph
   (`effects_activateScene()` or the narrower morph refresh).

**Returns:** `CC_RUN_DONE`.

### 4.5 `ccCopy_runSceneMorph(job)` — Copy Scene "scene morph"

**Location:** `copyOps.c`.

**Logic (single destination Scene, no fan-out):**

1. Wait for `preset_applyWorkersIdle()` if the destination is active.
2. For each instrument slot 0..5:
   a. If `src.instruments[slot].type != dst.instruments[slot].type` → skip.
   b. For each morphable descriptor:
      `dst.morph_instrument_parameters[i] = src.instrument_parameters[i]`.
3. For all 6 slots:
   `dst.fx_send_morph[slot] = src.fx_send_amount[slot]`
   (FX send is always per-voice, no type dependency).
4. Kit decay morph = source Kit decay normal.
5. Effect: if `src.effect.type == dst.effect.type`:
   for each morphable Effect param:
   `dst.effect.morph[i] = src.effect.normal[i]`.
   If types differ → skip Effect entirely.
6. Dirty marks for everything written.
7. If the destination is active: queue all 6 voice morph workers + Effect
   morph rebuild.

**Returns:** `CC_RUN_DONE` (or `CC_RUN_WAIT` while workers drain).

**No edit-mask exchange.** Unlike `copy scene` / `copy settings`, this
operation does not exchange or reset the edit mask — it preserves the
existing layout groupings.

---

## 5. Implementation steps

### Step 1 — Enum and label additions

**Files:** `clearOps.h`, `clearOps.c`, `copyOps.h`, `copyOps.c`.

1. Add `CC_CLEAR_RESET_MORPH` to `cc_clear_obj_sel_t`.
2. Add `CC_CLEAR_SCENE_RESET_MORPH` and `CC_CLEAR_SCENE_RESET_FX_MORPH` to
   `cc_clear_scene_sel_t`.
3. Add `CC_COPY_MORPH` to `cc_copy_track_sel_t`.
4. Add `CC_COPY_SCENE_MORPH` to `cc_copy_scene_sel_t`.
5. Update the label arrays: `ccClear_trackLabels[]`,
   `ccClear_trackFxLabels[]`, `ccClear_sceneLabels[]`,
   `ccCopy_trackLabels[]`, `ccCopy_sceneLabels[]`.
6. Update `ccClear_selectionCount()` and `ccCopy_selectionCount()` for the
   affected menus.

### Step 2 — Support functions

**File:** `presetManager.c/h` (or inline in the executors).

1. A helper to copy Normal → Morph for one instrument slot's morphable
   descriptors. Takes a `scene_t *`, slot index, writes in place. Returns
   the count of bytes written (for trace).
2. A helper to copy one source Scene's Normal → another Scene's Morph for
   one slot (with type-match gate).
3. Each helper must mark the affected AutoSave regions dirty.

Whether these are presetManager helpers or inline in the executors depends on
code size. Since at least three executors share the per-slot morphable-copy
logic, a shared helper is justified.

Candidate signature:
```c
/* Reset one slot's morph endpoints to its current normal endpoints.
 * Operates on morphable descriptors only. Marks AutoSave dirty.
 * Returns the number of bytes set. */
uint8_t preset_resetSlotMorphToNormal(uint8_t scene_index, uint8_t slot);

/* Copy src scene/slot normal endpoints to dst scene/slot morph endpoints.
 * Requires matching instrument types; returns 0 if types differ. */
uint8_t preset_copyNormalToMorph(uint8_t src_scene, uint8_t src_slot,
                                 uint8_t dst_scene, uint8_t dst_slot);
```

An EffectsManager helper for the Effect endpoints:
```c
/* Reset the Effect record's morphable morph endpoints to their normal
 * values. Returns nonzero if anything changed. */
uint8_t effects_resetMorphToNormal(uint8_t scene_index);
```

### Step 3 — Clear executors

**File:** `clearOps.c`.

1. `ccClear_runResetMorphTrack()` — dispatch from `ccClear_runJob()` for
   `CC_CLEAR_RESET_MORPH` on `CC_KIND_TRACK`.
2. `ccClear_runResetSceneMorph()` — dispatch for
   `CC_CLEAR_SCENE_RESET_MORPH` on `CC_KIND_SCENE`.
3. `ccClear_runResetFxMorph()` — dispatch for
   `CC_CLEAR_SCENE_RESET_FX_MORPH` on `CC_KIND_SCENE`.

### Step 4 — Copy executors

**File:** `copyOps.c`.

1. `ccCopy_runMorphTrack()` — dispatch from `ccCopy_runJob()` for
   `CC_COPY_MORPH` on `CC_KIND_TRACK`.
2. `ccCopy_runSceneMorph()` — dispatch from `ccCopy_runJob()` for
   `CC_COPY_SCENE_MORPH` on `CC_KIND_SCENE`.

### Step 5 — Morph worker integration

After each executor commits endpoint changes, the morph worker must
re-interpolate:
- Single voice: `presetMorph_requestVoice(slot)` (queues for the bounded
  worker).
- All voices: queue all 6 via the mask or use `preset_startDrumsetApply()`.
- Effect morph: `effects_activateScene(scene)` already rebuilds the
  interpolated Effect state.

### Step 6 — AutoSave dirty markers

Each endpoint write must mark the right AutoSave dirty cell:
- Instrument morph descriptors → `autosave_markInstrumentMorphDirty()` or
  the per-byte marker used by the existing Morph view edit path.
- FX send morph → the AutoSave Scene cells 45..50 dirty path
  (`scene_setFxSendMorph()` / `preset_setVoiceFxSendMorph()`).
- Kit decay morph → the Kit morph decay dirty path
  (`scene_setSlot6Track7MorphAmpEnvelopeDecay()`).
- Effect morph endpoints → `scene_finishEffectWholeCommit()` /
  `autosave_markEffectDirty()`.

### Step 7 — HCNAMES / names

None of these operations change identity. The `R` (refreshed) flag should
be dropped on any row whose morph content changed, so the boot reader
doesn't overwrite the equalized endpoints with library file content. This
is handled by the existing `ccSvc_nameContentChanged()` mechanism — call it
for each affected row.

### Step 8 — Trace records (DEV builds)

Reuse the existing `FANOUT` trace event (code 0x30) with new kind codes for
the morph operations. A new `JOB_END` result for a silently-skipped
type-mismatch member is already handled by the existing `JOB_STATS`
retarget-dropped counter.

### Step 9 — Build and test

1. `make all && make img` — verify clean build and measure link budget.
2. Verify menu labels appear correctly for all new selections.
3. Test each operation on hardware with a known Scene/Kit/Morph state.
4. Verify AutoSave captures the changed endpoints after an operation.

---

## 6. Fan-out summary

| Operation | Fans out? | Reason |
|-----------|-----------|--------|
| Clear TRACK `reset morph` | **Yes** | Voice slot is a Scene child; parallels `clear send` |
| Copy TRACK `morph` | **Yes** | Voice slot is a Scene child; parallels `copy instrument` |
| Clear Scene `reset morph` | **No** | Whole-Scene operation; parallels `clear scene` |
| Clear Scene `reset fx morph` | **Yes** | Effect-only operation; parallels `clear fx` |
| Copy Scene `scene morph` | **No** | Whole-Scene paste; parallels `copy scene` |

---

## 7. RAM and flash impact

- **No new static RAM allocations.** All work uses existing Scene data
  structures, the existing job queue, and local stack variables. The per-slot
  morphable-copy loop iterates the existing descriptor table in place.
- **Flash:** The new executors are small (each is a loop over 6 slots ×
  descriptors, plus a few Scene-setting writes). Estimated ≈ 500–800 bytes
  of text for the five executors plus the shared helper, plus ≈ 80 bytes of
  label strings. Well within the 220 KB flash headroom.
- **Stack:** The executors use the existing foreground stack budget. The
  inner loop reads descriptors and writes endpoint bytes — no buffer
  allocation. Peak stack addition ≈ 40 bytes (loop counters, Scene
  pointers).

---

## 8. Blocking issues

### 8.1 Confirmed clear — no blockers

1. **Descriptor morphability flag** (`INSTRUMENT_PARAM_FLAG_MORPHABLE`) is
   already defined and used by the morph engine. The descriptor table for
   any instrument type is available through InstrumentManager.

2. **Effect morphability** (`effects_paramMorphable()`) is already
   implemented and used by the Effect morph engine.

3. **AutoSave dirty markers** for instrument morph bytes, FX send morph, Kit
   decay morph, and Effect records all exist and are exercised by the
   existing VOICE Morph view edit path and the `clear send` executor.

4. **Morph worker** (`presetMorph_requestVoice()`) already accepts per-slot
   rebuild requests and is used by existing copy/clear executors.

5. **Effect activation** (`effects_activateScene()`) already rebuilds Effect
   morph state and is used by `clear fx` and `copy effect`.

6. **The copy/clear framework** already supports Scene-level executors with
   fan-out, worker waits, and dirty marking — the new operations follow the
   exact same pattern as `ccClear_runSend()`, `ccCopy_runInstrument()`, and
   `ccClear_runFx()`.

### 8.2 User confirmations (Session 076, 2026-10-06)

All five questions confirmed:

1. **Track morph copy with mismatched types:** **Confirmed — silent skip.**
   No key-based remapping between different instrument types for morph
   endpoint copying. If types differ, the slot is silently skipped.

2. **HCNAMES `R` flag drop on morph-only changes:** **Confirmed — drop `R`**
   on rows whose morph content changed. Prevents the boot reader from
   overwriting equalized morph endpoints with the library file's original
   morph endpoints.

3. **`clear scene` vs `reset morph` for non-active Scenes:** **Confirmed —
   `reset morph` does NOT clear the present bit.** It only equalises
   endpoints; the Scene remains Bank-present.

4. **Effect morph amount on "reset morph":** **Confirmed — none of these
   operations touch any of the 8 morph *amounts* (voices 1–6, fx, global).**
   After a morph reset, if Effect morph is at 128, the sound stays unchanged
   because Normal == Morph. The morph amounts are amount controls, not
   endpoints.

5. **Voice morph amounts on "reset morph":** **Confirmed — same rule.** None
   of the five new operations touch any morph amount. After the reset, the
   morph pot does nothing (until endpoints diverge again).

---

## 9. Source indicator text

Using the existing indicator format:

| Operation | Indicator | Example |
|-----------|-----------|---------|
| Clear TRACK `reset morph` | `SNNTN` | `S03T2` (same as existing track clears) |
| Copy TRACK `morph` | `SNNmN` | `S03m2` (new: `m` for morph source) |
| Clear Scene `reset morph` | `SNN` | `S03` (same as existing Scene clears) |
| Clear Scene `reset fx morph` | `SNN` | `S03` (same as existing Scene clears) |
| Copy Scene `scene morph` | `SNNm` | `S03m` (new: `m` suffix for morph source) |

The copy source indicators distinguish morph operations by using `m` instead
of `T` (track) or `i` (instrument). This is a new indicator letter. If
undesired, we can reuse the existing track/Scene indicator and rely on the
menu label alone.

---

## 10. Verification checklist

| # | Test | Expected |
|---|------|----------|
| 1 | Clear TRACK `reset morph` on a voice with Morph != Normal | Morph endpoints match Normal; runtime rebuilds; sound at morph 50% is now identical to morph 0% |
| 2 | Clear TRACK `reset morph` fans out to edit-mask Scenes | All mask members' morph endpoints reset |
| 3 | Copy TRACK `morph` same type, different tracks | Destination morph endpoints = source Normal endpoints |
| 4 | Copy TRACK `morph` different types | Silently skipped; destination unchanged |
| 5 | Copy TRACK `morph` fans out | All mask members' morph endpoints updated |
| 6 | Clear Scene `reset morph` | All 6 instruments + FX send morph + Kit decay morph + Effect morph endpoints all equalised |
| 7 | Clear Scene `reset morph` does NOT fan out | Only the pressed Scene is affected |
| 8 | Clear Scene `reset fx morph` | Only Effect morphable params reset |
| 9 | Clear Scene `reset fx morph` fans out | All mask members' Effect morph endpoints reset |
| 10 | Copy Scene `scene morph` matching types | All Normal → Morph copied |
| 11 | Copy Scene `scene morph` with one mismatched instrument | That slot skipped; others copied |
| 12 | Copy Scene `scene morph` with mismatched Effect type | Effect skipped; instruments still copied |
| 13 | AutoSave captures changes after each operation | Card copy shows updated morph endpoints |
| 14 | Boot with equalized endpoints | Morph endpoints survive reboot via AutoSave |
