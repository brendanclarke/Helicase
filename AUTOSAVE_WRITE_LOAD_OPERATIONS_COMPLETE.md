# Session 049 — AutoSave Write-on-Load Completion Plan

## Objective

Complete the remaining Session-049 AutoSave mutation producers for successful
normal load operations, in this strict order:

1. normal root Kit Load;
2. KitMrp Load;
3. root Scene Load, excluding Pattern; and
4. selective root Bank Load.

Root Instrument Load and InstrumentMrp are already accepted Session-048
boundaries and are not part of this change. This plan does not authorize a
boot reader, Load/Save writer exclusion, Save-side marking,
Pattern/Effect persistence, recursive-delete work, or a runtime Bank active-
Scene behavior change.

## Fixed contracts

- `Autosave.c` remains the only owner of the canonical dirty mask and wire
  mapping. Producers call typed/whole-object marker APIs only; they do not
  calculate payload offsets or perform filesystem I/O.
- The existing whole-object helpers already define the required wire scopes:
  `autosave_markKitDirty(scene)` marks two live Kit bytes plus six complete
  Instrument regions; `autosave_markSceneWithoutPatternDirty(scene)` marks the
  40 live Scene bytes, the presently empty Effect scope, and the Kit scope.
  Pattern, HCNAMES identities/sources, and Instrument/Kit names remain out of
  these markers.
- A mark is permitted only after the complete public filesystem operation has
  succeeded. Staging, browser scrolling/Bank child preview, an invalid load,
  and a later rejected filesystem completion produce no mark.
- AutoSave maps only Bank-present Scenes. Normal Kit and root Scene callbacks
  must first retain their existing successful-load present-mask promotion, then
  mark the corresponding scope. KitMrp intentionally preserves the existing
  no-promotion rule and may mark only an already-present destination.
- BankData setters already mark their own changed Bank payload fields. The
  Bank-specific new work is marking the non-Pattern Scene scopes for the exact
  completed child mask; it must not broadly re-mark all Bank fields or all 16
  Scenes.
- Do not add RAM, a new trace format, an AutoSave API, a second dirty mask, or
  a record buffer. Existing `D/S/A/V/M/C/P/T` lifecycle records remain the
  diagnostic path. A Kit or multi-Scene fixture can wrap the 64-record trace,
  so copied A/B records and a decoded mask/payload comparison are the primary
  acceptance evidence.

## Current commit boundaries

| Load type | Resident write today | Correct new mutation boundary |
| --- | --- | --- |
| Kit | `filesystem_loadKitDirectory_tick()` copies `op_staged_kit` into every bit of `op_kit_load_scene_mask` after all member files validate. | `on_kit_load_complete()` in Preset, only when `filesystem_status() == FS_STATUS_DONE`, after present-mask promotion and before acknowledgement. |
| KitMrp | Filesystem only stages the Kit; `preset_commitStagedKitNormalToMorph()` copies each compatible source Normal endpoint into the destination Morph endpoint and copies the generated slot-6 Morph decay. | Immediately beside each successful Preset endpoint/generated-setting commit. It is intentionally separate from normal Kit Load. |
| Root Scene | `filesystem_commitSceneStage()` copies staged settings/Kit before the intentionally non-atomic direct Pattern read; root Scene success then completes its HCNAMES transaction. | `on_scene_load_complete()` in Preset, only after the final successful filesystem/HCNAMES completion and present-mask promotion; mark the non-Pattern scope only. |
| Bank | Each selected child delegates through the Scene loader; BankData/HCNAMES finish only after every selected child succeeds. `filesystem_lastBankLoadSceneMask()` remains valid during the completion callback. | `on_bank_load_complete()` in Preset, only on success and before acknowledgement, using `filesystem_lastBankLoadSceneMask()` exactly. |

This placement intentionally avoids the earlier failed Phase-2 pattern of
marking at raw staging or UI-selection boundaries. It also means a root Scene
whose Pattern/effect phase later fails is not published as a successful
whole-Scene mutation, even though its current non-atomic loader may already
have copied its non-Pattern data to resident SRAM.

## Implementation sequence

### 1. Normal Kit Load only

Edit `Core/Bank/Scene/Preset/presetManager.c` in
`on_kit_load_complete()`.

1. Preserve the existing successful-load gate and call
   `preset_markRequestedScenesPresentOnSuccessfulLoad()` first.
2. While `filesystem_status()` still reports `FS_STATUS_DONE`, iterate the
   immutable `pm_kit_request_scene_mask` across the valid 16 resident Scene
   bits and call `autosave_markKitDirty(scene_index)` once per selected target.
3. Call the existing `preset_completeFilesystemOp(PRESET_OP_KIT_LOAD)` last;
   it acknowledges the facade, so no operation-local status/mask may be read
   after it.
4. Leave `filesystem_loadKitDirectory_tick()` as the staging/commit owner and
   leave KitMrp on its existing separate non-normal path. Do not put the mark
   in Menu, HCNAMES session flushing, directory parsing, or scrolling logic.

The helper recursively calls the accepted whole-Instrument marker for all six
slots. Its `I` summaries are existing logging-only diagnostics, not a new Kit
trace design.

### 2. KitMrp Load only

The session carryover originally deferred KitMrp, but the explicit Session-049
goal includes it. Keep it isolated from normal Kit Load and retain the existing
endpoint-only semantics.

Edit `preset_commitStagedKitNormalToMorph()` in
`Core/Bank/Scene/Preset/presetManager.c`.

1. For every selected destination Scene and Instrument slot whose
   `preset_copyInstrumentNormalToMorphIfSameType()` call succeeds, immediately
   call `autosave_markInstrumentMorphDirty(scene_index, slot)`. This is the
   same descriptor-aware Morphable-only scope already accepted for
   InstrumentMrp; it must not mark type, Normal, name, HCNAMES, routing, or a
   mismatched slot.
2. Replace the direct assignment of
   `slot6_track7_morph_amp_envelope_decay` with the owning
   `scene_setSlot6Track7MorphAmpEnvelopeDecay()` setter (after the existing
   same-type slot-6 test). That setter retains the equal-value guard and marks
   the one generated Kit Morph-decay byte only when it changes.
3. Keep the current active-Scene Morph-worker requests exactly where they are.
   Inactive selected Scenes still receive their retained endpoint markers;
   they do not need immediate DSP work.
4. Do not promote absent target Scenes for a morph-only operation. The existing
   contract intentionally does not call
   `preset_markRequestedScenesPresentOnSuccessfulLoad()` for KitMrp. A
   non-Bank-present target remains outside the current HCPRMS map; do not
   broaden Bank-presence semantics as part of this mutation hook.

The filesystem callback merely proves that staging succeeded. The actual
retained mutation occurs later in this Preset commit routine, before the
next foreground pass can admit the background writer, so it is the correct
endpoint marker boundary.

### 3. Root Scene Load without Pattern only

After the KitMrp fixture is accepted, edit `on_scene_load_complete()` in the
same Preset file.

1. Keep the existing `preset_markRequestedScenesPresentOnSuccessfulLoad()`
   before any marker.
2. On `FS_STATUS_DONE`, iterate the immutable
   `pm_kit_request_scene_mask` and call
   `autosave_markSceneWithoutPatternDirty(scene_index)` for each selected
   destination.
3. Finish through `preset_completeFilesystemOp(PRESET_OP_SCENE_LOAD)` exactly
   as today.
4. Do not use `autosave_markSceneWithPatternDirty()` merely because the normal
   Scene loader also reads `pattern.pat`: it is presently only a non-Pattern
   alias and its name would obscure the intentional exclusion. Do not mark in
   `filesystem_commitSceneStage()`, because this occurs before the direct
   Pattern/effect and root-HCNAMES completion paths can fail.

This scope covers retained Scene settings and the complete embedded Kit
payload. It deliberately leaves Pattern outside the HCPRMS record and retains
the current zero-byte Effect scope.

### 4. Selective Bank Load only

After the root Scene fixture is accepted, edit `on_bank_load_complete()` in
Preset.

1. Before `preset_completeFilesystemOp()` acknowledges the callback, require
   `filesystem_status() == FS_STATUS_DONE`.
2. Read `filesystem_lastBankLoadSceneMask()` exactly once into a local mask.
   It is valid only in this immediate completion window.
3. Iterate only set bits in that returned mask and call
   `autosave_markSceneWithoutPatternDirty(scene_index)` once for each.
4. Then call `preset_completeFilesystemOp(PRESET_OP_BANK_LOAD)` unchanged.

Do not substitute the original requested Menu mask, the Bank preview mask,
`pm_kit_request_scene_mask`, `bank_scenePresentMask()`, or a full sixteen-bit
mask. The filesystem result is the requested/present child intersection and a
child becomes part of it only after the shared Scene reader commits it. The
existing Bank phase-20 setters continue to mark actual changed Bank metadata;
unselected resident Scene payload and HCNAMES blocks must remain untouched.
An empty Bank returns a zero completed-child mask and receives no whole-Scene
mark.

## Code review checks

- Include `Autosave.h` only where needed by Preset; do not expose a new
  filesystem mutation callback or alter public request APIs.
- Verify normal Kit/Scene/Bank markers run before `filesystem_ack()` and only
  after their required Bank-present publication. Verify KitMrp marks occur only
  in its actual post-stage Preset commit and only for already-present targets.
  No marker may run on an `FS_STATUS_ERROR` path.
- Keep normal Kit, root Scene, and Bank branches visibly separate. A small
  private Preset loop helper is acceptable only if it takes an explicit mask
  and explicit scope; do not create a generic "mark load" switch that can
  accidentally include Morph, Save, or Pattern work.
- Preserve all existing HCNAMES ordering: Kit remains deferred to its Menu
  session exit; root Scene and Bank retain their filesystem-owned successful
  HCNAMES close gates. HCNAMES remains identity/provenance authority, not
  AutoSave payload identity.
- Preserve current runtime apply/index behavior. The marker is RAM-only and
  must not alter `menu_storageBusy`, the `...` command lifetime, DSP apply,
  child preview, or root-index reload ordering.

## Source-derived code-change ledger

This ledger is derived from the current C and header implementation, rather
than from a storage specification or prior-session account. It is the complete
set of implementation edits for this plan. The comment blocks are intended to
be pasted beside the corresponding changes with only normal local style edits.

### Proven ordering constraints from live code

1. `preset_completeFilesystemOp()` first reads `filesystem_status()` and then
   calls `filesystem_ack()`. Consequently a completion callback must inspect
   filesystem status and any operation-scratch result before it calls that
   helper. This is material for the Bank mask, whose own header says it is
   valid only for the immediate completion callback.
2. `autosave_scenePayloadBase()` rejects a Scene whose
   `bank_scenePresent(scene_index)` bit is clear. All of the whole-Kit,
   whole-Scene, and Instrument-Morph helpers ultimately use that guard. The
   normal Kit and root Scene callbacks already call
   `preset_markRequestedScenesPresentOnSuccessfulLoad()` before their current
   completion helper; keep that call before their new markers.
3. A normal Kit is copied into retained Scenes by filesystem before its
   callback. A root Scene's non-Pattern stage is copied before its direct
   Pattern/effect steps and before its root-HCNAMES close gate. Therefore their
   marker must be at the final successful Preset callback, not at either raw
   assignment.
4. KitMrp is different: filesystem deliberately leaves its Kit staged;
   `preset_commitStagedKitNormalToMorph()` later makes the retained writes.
   Its marker belongs at those writes, not in `on_kit_morph_load_complete()`.
5. The Bank loader intersects its requested mask with discovered children in
   its phase 17, calls the shared Scene loader one child at a time, and only
   finishes successfully after the phase-20 BankData commit and the final
   HCNAMES path. `filesystem_lastBankLoadSceneMask()` returns that retained
   effective mask. It is the only correct per-Scene source at Bank completion.

### Required functional edits — `Core/Bank/Scene/Preset/presetManager.c`

#### C1 — normal Kit completion: `on_kit_load_complete()`

**Current code:** this private filesystem callback calls
`preset_markRequestedScenesPresentOnSuccessfulLoad()` and immediately calls
`preset_completeFilesystemOp(PRESET_OP_KIT_LOAD)`. The latter acknowledges the
facade. `pm_kit_request_scene_mask` is the request-time destination mask set
by `preset_loadKitForScenes()`.

**Required change:** retain the present-mask call; before the completion helper,
test `filesystem_status() == FS_STATUS_DONE`, loop through valid Scene indices
(`scene_index < SCENE_COUNT && scene_index < 16u`), test the matching bit of
`pm_kit_request_scene_mask`, and call `autosave_markKitDirty(scene_index)` for
each set bit. Do not make the loop a Menu operation, do not use the currently
viewed or active Scene, and do not call the marker after the completion helper.

**Inputs:** the terminal filesystem status and Preset's immutable selected
Scene mask. **Outputs:** for every successfully loaded and now-present target,
the existing helper sets the two current Kit-setting bits and invokes the
existing complete-Instrument scope for all six slots. **Why:** the filesystem
assignment `target_scene->kit = op_staged_kit` bypasses every scalar owner
setter, so no existing scalar dirty hook observes a normal Kit replacement.
**Affiliates:** `filesystem_loadKitDirectory_tick()` performs the staged
assignment; `preset_markRequestedScenesPresentOnSuccessfulLoad()` makes the
Autosave Scene map valid; `autosave_markKitDirty()` owns the wire scope;
`preset_completeFilesystemOp()` must remain last; Menu later starts the
unchanged drumset-apply flow.

Suggested C comment text:

```c
/*
 * Publish the complete retained Kit image only after the normal Kit request
 * reached its terminal successful filesystem callback.
 *
 * Inputs: the immutable pm_kit_request_scene_mask captured when
 * preset_loadKitForScenes() was accepted, and FS_STATUS_DONE before
 * preset_completeFilesystemOp() acknowledges the facade. Outputs: every
 * selected now-present Scene receives the existing whole-Kit dirty scope,
 * comprising its two live Kit settings and all six complete Instrument
 * regions. Why: filesystem_loadKitDirectory_tick() assigns kit_t directly
 * after validation, bypassing scalar SceneData/Preset setters. The marker
 * records the already-retained bytes only; it performs no I/O and does not
 * change HCNAMES, Menu state, or runtime application. Affiliates:
 * preset_markRequestedScenesPresentOnSuccessfulLoad(),
 * autosave_markKitDirty(), and preset_completeFilesystemOp().
 */
```

#### C2 — KitMrp Instrument endpoints: `preset_commitStagedKitNormalToMorph()`

**Current code:** for every bit in `pm_kit_request_scene_mask`, this private
function calls `preset_copyInstrumentNormalToMorphIfSameType()` for each slot.
That helper returns nonzero only after it has found matching instrument types,
obtained the destination registry entry, and assigned one or more Morphable
destination endpoint bytes. The current `if` combines that result with the
active-Scene test, so inactive committed targets receive no runtime request
(correctly) and currently receive no mutation mark (the missing behavior).

**Required change:** split the combined condition. When the copy helper
returns nonzero, call `autosave_markInstrumentMorphDirty(scene_index, slot)`
immediately. Retain the existing `scene_index == scene_getActiveIndex()` test
inside that successful-copy block only for `presetMorph_requestVoice()` and
`active_queued`.

**Inputs:** the staged Kit returned by `filesystem_loadedKit()`, the immutable
request mask, one retained destination Scene, and one matching type pair.
**Outputs:** all Morphable descriptor endpoints in that committed destination
slot are offered to the existing descriptor map, which publishes them when its
tracking and Bank-present guards accept the destination; type, Normal
endpoints, identity, routing, and a mismatched slot remain unmarked. **Why:** the
endpoint-copy helper writes `morph_instrument_parameters[]` directly, and the
existing InstrumentMrp path already establishes
`autosave_markInstrumentMorphDirty()` as the matching retained endpoint scope.
The mark must not be nested under the active-Scene condition, because inactive
Bank-present Scenes can be selected by the request mask. **Affiliates:**
`preset_copyInstrumentNormalToMorphIfSameType()`,
`autosave_markInstrumentMorphDirty()`, `presetMorph_requestVoice()`, and
`preset_startKitMorphApply()`.

Suggested C comment text:

```c
/*
 * Record one successful KitMrp endpoint import at its retained-data boundary.
 *
 * Inputs: a selected resident Scene/slot and a nonzero result from
 * preset_copyInstrumentNormalToMorphIfSameType(), which proves the source and
 * destination types matched and one or more Morphable destination cells were
 * assigned. Output: Autosave offers exactly that slot's Morphable Morph
 * descriptor cells to its existing tracking/Bank-present map. Why: KitMrp
 * preserves type, Normal image, names, routing, and modulation ownership, so
 * a whole-Kit or whole-Instrument marker would over-publish unrelated payload.
 * This is deliberately outside the active Scene runtime branch: inactive
 * Bank-present Scenes also need persistence, while only the active Scene needs
 * presetMorph_requestVoice(). Affiliates:
 * autosave_markInstrumentMorphDirty(), presetMorph_requestVoice(), and the
 * staged Kit lifetime established by on_kit_morph_load_complete().
 */
```

#### C3 — KitMrp generated setting: the slot-6 block in `preset_commitStagedKitNormalToMorph()`

**Current code:** after the Instrument loop, a matching slot-6 type causes a
direct assignment from staged normal
`source->settings.slot6_track7_amp_envelope_decay` to retained Morph
`scene->kit.settings.slot6_track7_morph_amp_envelope_decay`. This bypasses
`scene_storeKitParameterByte()` and its named dirty hook.

**Required change:** retain the same slot-6 type-match condition but replace
the direct assignment with
`scene_setSlot6Track7MorphAmpEnvelopeDecay(scene_index,
source->settings.slot6_track7_amp_envelope_decay)`. The source-stage parser
already bounds the staged normal value to 127; the called setter applies the
same 127 bound and invokes `scene_storeKitParameterByte()`, which offers
`AUTOSAVE_KIT_PARAM_SLOT6_TRACK7_MORPH_DECAY` to its existing dirty map only
when the retained value changes. Keep the current active-Scene Morph request
immediately afterward.

**Inputs:** one selected destination Scene, the staged Kit's generated normal
decay byte, and the existing slot-6 same-type condition. **Outputs:** one
retained generated Morph-decay byte changes only when needed and its one Kit
payload coordinate is offered to the existing named dirty setter. **Why:** this
byte is outside an Instrument descriptor image, so C2's per-Instrument Morph
marker cannot cover it. **Affiliates:**
`scene_setSlot6Track7MorphAmpEnvelopeDecay()`, its private
`scene_storeKitParameterByte()` helper, `autosave_markKitParameterDirty()`,
and the existing slot-6 runtime refresh.

Suggested C comment text:

```c
/*
 * Commit the KitMrp source's generated track-7 normal decay as the destination
 * Morph endpoint through SceneData's named setter.
 *
 * Inputs: a selected Scene, matching slot-6 types, and the bounded staged
 * normal decay value. Output: the generated Morph-decay byte changes only
 * when different and then offers AUTOSAVE_KIT_PARAM_SLOT6_TRACK7_MORPH_DECAY
 * to its existing dirty map.
 * Why: this endpoint is a Kit setting rather than an Instrument descriptor,
 * so the per-slot Morph marker cannot represent it. The setter preserves the
 * normal owner rule—store first, then mark—and leaves runtime scheduling to
 * the existing active-Scene branch. Affiliates:
 * scene_setSlot6Track7MorphAmpEnvelopeDecay(),
 * scene_storeKitParameterByte(), and presetMorph_requestVoice().
 */
```

#### C4 — root Scene completion: `on_scene_load_complete()`

**Current code:** this callback promotes every requested destination to the
Bank-present mask, then acknowledges through
`preset_completeFilesystemOp(PRESET_OP_SCENE_LOAD)`. The filesystem's Scene
reader committed settings and Kit before Pattern I/O; only after a successful
root Scene operation does it enter the root-HCNAMES update path and invoke
this callback.

**Required change:** after the existing present-mask call and before the
completion helper, condition on `FS_STATUS_DONE`, loop over the exact same
valid bits in `pm_kit_request_scene_mask`, and call
`autosave_markSceneWithoutPatternDirty(scene_index)` for each one.

**Inputs:** terminal root-Scene status and the captured root-Scene destination
mask. **Outputs:** for each successful and present target, the existing scope
marks all live Scene settings, the current zero-count Effect scope, and the
complete Kit scope. It does not mark Pattern. **Why:** direct assignment of
`target->settings` and `target->kit` in `filesystem_commitSceneStage()`
bypasses their scalar setters, while a marker there would survive a later
Pattern/effect or HCNAMES failure. **Affiliates:**
`filesystem_commitSceneStage()`, the root Scene HCNAMES continuation,
`preset_markRequestedScenesPresentOnSuccessfulLoad()`,
`autosave_markSceneWithoutPatternDirty()`, and the unchanged Menu sound apply.

Suggested C comment text:

```c
/*
 * Mark the committed non-Pattern root-Scene payload only after the complete
 * root Scene operation, including its HCNAMES continuation, succeeded.
 *
 * Inputs: FS_STATUS_DONE and the immutable destination mask captured by
 * preset_loadSceneForScenes(). Outputs: every selected now-present Scene gets
 * the existing non-Pattern scope—live Scene settings, the current Effect
 * scope, and the complete Kit scope. Why: filesystem_commitSceneStage()
 * assigns settings and kit_t directly before Pattern I/O; marking there could
 * publish a load whose later Pattern/effect or HCNAMES work failed. Pattern is
 * intentionally absent because the chosen marker has no Pattern payload.
 * Affiliates: filesystem_commitSceneStage(),
 * preset_markRequestedScenesPresentOnSuccessfulLoad(),
 * autosave_markSceneWithoutPatternDirty(), and
 * preset_completeFilesystemOp().
 */
```

#### C5 — selective Bank completion: `on_bank_load_complete()`

**Current code:** this callback immediately acknowledges the Bank result. The
filesystem has already intersected request and discovered-child masks; when
all selected child loads succeed, it merges that exact mask into BankData's
present mask and then reaches the callback. The existing BankData setters have
already marked changed Bank fields individually.

**Required change:** before `preset_completeFilesystemOp()`, test for
`FS_STATUS_DONE`, read `filesystem_lastBankLoadSceneMask()` once into a local
`uint16_t`, and loop only over set valid Scene bits, calling
`autosave_markSceneWithoutPatternDirty(scene_index)`. Do not read the function
after acknowledgement; do not use `pm_kit_request_scene_mask`, Menu preview,
`bank_scenePresentMask()`, or `op_bank_loaded_scene` as the marker mask.

**Inputs:** successful Bank terminal status and the filesystem's completed
effective-child mask. **Outputs:** each committed selected child receives the
non-Pattern Scene scope; selected Bank metadata remains covered by its existing
BankData scalar setters; a valid empty Bank produces zero Scene-scope marks.
**Why:** the original request can include missing child directories, and
`bank_scenePresentMask()` includes older unselected resident Scenes; either
would over-mark unrelated payload. **Affiliates:**
`filesystem_lastBankLoadSceneMask()`, Bank phases 17/20,
`bank_setScenePresentMask()`, `autosave_markSceneWithoutPatternDirty()`, and
`preset_completeFilesystemOp()`.

Suggested C comment text:

```c
/*
 * Publish only the Bank children that the completed filesystem operation
 * actually loaded.
 *
 * Inputs: FS_STATUS_DONE and filesystem_lastBankLoadSceneMask() before the
 * completion helper acknowledges operation scratch. Output: each set child
 * bit receives the existing non-Pattern Scene dirty scope; a valid empty Bank
 * yields no Scene marks. Why: the original request may name absent children,
 * while bank_scenePresentMask() also contains retained unselected Scenes; only
 * the completed effective-child mask identifies this Bank Load's payload.
 * BankData has already marked changed Bank fields through its own setters.
 * Affiliates: filesystem_lastBankLoadSceneMask(), Bank phase-20 metadata
 * commit, autosave_markSceneWithoutPatternDirty(), and
 * preset_completeFilesystemOp().
 */
```

### Required header and implementation-comment edits

No function signature, enum, storage type, or new public API is needed. The
following comment changes are nevertheless required so the code accurately
states the new live ownership after the functional changes.

#### H1 — `Core/Bank/Scene/Preset/presetManager.h`

Update the existing shared comment immediately above
`preset_startKitMorphApply()` / `preset_startInstrumentMorphApply()`. Its
current text names AutoSave marking only for InstrumentMrp, although KitMrp
will then mark two existing domains: compatible Instrument Morph endpoints
and the generated Kit Morph-decay setting.

Suggested header comment text:

```c
/*
 * Commit staged KitMrp or InstrumentMrp endpoints and drain the bounded Morph
 * worker without replacing identity, Normal images, routing, or modulation
 * ownership.
 *
 * Inputs: filesystem-owned validated staging plus the immutable selected
 * destination coordinates. Outputs: InstrumentMrp marks only the committed
 * destination's Morphable Morph descriptors; KitMrp marks the same
 * Morphable-only descriptor domain for each successfully type-compatible
 * selected slot and commits its generated slot-6 Morph-decay setting through
 * SceneData's named Kit setter. Mismatched slots, types, Normal endpoints,
 * HCNAMES identity/source, and routing remain unchanged. Why: each loader
 * imports endpoint data without becoming a normal Kit/Instrument replacement.
 * The active Scene alone receives the existing deferred runtime refresh; an
 * inactive selected Scene can publish only if it is already Bank-present.
 * Affiliates:
 * preset_commitStagedKitNormalToMorph(),
 * preset_commitStagedInstrumentNormalToMorph(),
 * autosave_markInstrumentMorphDirty(), and
 * scene_setSlot6Track7MorphAmpEnvelopeDecay().
 */
```

#### H2 — `Core/Bank/Scene/Autosave.h`

Update the comment over the existing whole-scope declarations, not their
signatures. It already describes the scopes but names only Instrument load
callers. Add the now-real callers so no future code treats
`autosave_markSceneWithPatternDirty()` as the root Scene hook or treats a
KitMrp endpoint import as a whole Kit replacement.

Suggested header comment text to add after the current scope description:

```c
/*
 * Current load affiliates are intentionally asymmetric: successful normal Kit
 * completion calls autosave_markKitDirty() for each target; successful root
 * Scene and exact-mask Bank completion call
 * autosave_markSceneWithoutPatternDirty(); KitMrp and InstrumentMrp call
 * autosave_markInstrumentMorphDirty() only after compatible endpoint copies.
 * The generated KitMrp track-7 Morph decay is a named Kit scalar and therefore
 * reaches autosave_markKitParameterDirty() through SceneData, not an
 * Instrument marker. These calls must occur after retained assignment and
 * before any completion handoff makes request-local filesystem state invalid.
 */
```

#### C6 — comment-only updates in `Core/Bank/Scene/Autosave.c`

No Autosave algorithm change is needed: every necessary marker already exists.
Update its existing comments to replace the now-stale prospective wording and
to make the actual callers visible at the owner of the dirty scope:

- `autosave_markInstrumentMorphDirty()` should name both KitMrp and
  InstrumentMrp as affiliates.
- `autosave_markKitDirty()` should state that normal Kit completion is its
  current caller, rather than only a future load hook.
- `autosave_markSceneWithoutPatternDirty()` should state that successful root
  Scene and exact-mask Bank completion are current callers, and that it has no
  Pattern side effect.

Suggested C comment text for the shared intent:

```c
/*
 * These whole-scope markers observe already-committed retained state only.
 * Normal Kit completion uses the Kit scope; successful root Scene and
 * exact-mask Bank completion use the non-Pattern Scene scope; KitMrp and
 * InstrumentMrp use only the compatible Instrument Morph scope. Inputs are
 * validated resident coordinates after their owning commit. Outputs are atomic
 * ORs into the one canonical mask through existing typed markers; no helper
 * copies data, owns filesystem state, or changes identity/provenance. Why:
 * direct load assignments bypass scalar setters but must not broaden into
 * Pattern, HCNAMES, Normal endpoint, or unselected-Scene publication.
 * Affiliates: Preset load completions, SceneData's generated Kit setter, and
 * filesystem's existing writer.
 */
```

### Verified no-functional-change affiliates

These files are involved in the call chain but must remain functionally
unchanged for this plan:

| File / API | Source-audited reason for no functional edit |
| --- | --- |
| `Core/Hardware/SD/filesystem.c` | It already stages/commits Kit and Scene payloads, runs root Scene HCNAMES before its callback, computes the Bank effective mask, and exposes it until completion acknowledgement. Adding producer calls here would either occur before final success or duplicate Preset ownership. |
| `Core/Hardware/SD/filesystem.h` | `filesystem_lastBankLoadSceneMask()` already documents the exact returned mask and its immediate-callback lifetime. No new accessor or request field is required. |
| `Core/Bank/Scene/Autosave.c` functional code | `autosave_markKitDirty()`, `autosave_markSceneWithoutPatternDirty()`, and `autosave_markInstrumentMorphDirty()` already map exactly the required scopes through the canonical tracking/mask guards. Only their comments need current-caller updates. |
| `Core/Bank/Scene/SceneData.c/.h` functional code | `scene_setSlot6Track7MorphAmpEnvelopeDecay()` already owns normalization, equal-value suppression, storage-before-mark ordering, and `AUTOSAVE_KIT_PARAM_SLOT6_TRACK7_MORPH_DECAY`. C3 reuses it. |
| `Core/Bank/BankData.c/.h` | Successful Bank Load already reaches `bank_setDisplayName`, `bank_setScenePresentMask`, `bank_selectActiveSceneForEditMask`, `bank_setSceneMaskVoiceEdit`, and `bank_setRestoreBankSlot`; each changed serialized Bank field already calls its typed marker. |
| `Core/Menu/menu.c/.h` | Menu correctly delays KitMrp's actual retained commit until `preset_startKitMorphApply()`, and starts existing Kit/Scene/Bank runtime-apply/command completion flows. It owns no new dirty decision. |

The no-change list is part of the implementation boundary: moving any marker
into those affiliates would either see staged data, select the wrong mask, or
mix persistence with Menu/runtime work that the current code already separates.

## Build and focused hardware fixtures

For every boundary, begin from a copied, fully drained valid A/B pair and
record the winner generation, CRC, mutation mask, relevant payload bytes,
HCNAMES, `settings.cfg`, and `asavetrc.bin`. Build with
`make -j2 && make img` before flashing. No new allocation is planned, but
also confirm that the normal logging-off build still compiles with its existing
trace stubs.

After a load, leave its owning Load/Save page using the normal exit path, then
wait for the existing five-second debounce and bounded transaction. A card copy
made while browsing, or after an `S` without a terminal `T`, is not a result.

### Kit fixture

1. Select a known root Kit and one explicit non-active resident Scene target;
   use a distinguishable fixture for its Kit values and all six Instrument
   type/Normal/Morph images. Repeat with a two-Scene target mask only after the
   one-Scene result passes.
2. Confirm the next A/B winner advances and the selected Scene's Kit region
   matches the loaded directory's parsed values. Its six Instrument regions
   must carry their loaded type plus live Normal and Morphable Morph cells.
3. Confirm another unselected Scene's full payload is byte-identical, and that
   no Scene-settings or Pattern scope was introduced by this Kit-only load.
4. Confirm an empty/invalid Kit selection or failed request advances neither
   generation nor dirty coverage.

### Root Scene fixture

1. Use explicit `OK` to load a root Scene whose settings, embedded Kit, and
   Pattern all differ from the target. Scrolling/previewing other rows first
   must not advance AutoSave.
2. After command completion, normal page exit, and drain, verify the selected
   Scene's 40 live Scene bytes and complete Kit/Instrument payload in the new
   HCPRMS winner. Verify only the requested destination mask changed.
3. Verify Pattern is absent from the AutoSave result by design; its audible/
   resident change is not evidence for an HCPRMS mutation. Effect remains a
   zero-live-byte scope.
4. Exercise an invalid/failed root Scene fixture and confirm that no successful
   whole-Scene mark or new generation is produced.

### KitMrp fixture

1. Choose an already Bank-present target Scene and a staged Kit whose one or
   more slot types match the selected resident Kit and whose normal endpoint
   values differ from its destination Morph
   endpoints; include the generated slot-6/track-7 decay when applicable.
   Also include at least one deliberate source/destination type mismatch.
2. After the ordinary KitMrp apply and drain, compare the next A/B winner with
   its predecessor. Only Morphable Morph endpoints of compatible slots, plus a
   changed generated slot-6 Morph-decay cell, may change. Type, Normal,
   identity, source, audio routing, and every mismatched-slot Morph region must
   stay byte-identical.
3. Repeat once with equal endpoint values to confirm the generated setting's
   normal setter guard does not produce a needless scalar bit; an unchanged
   compatible Instrument endpoint may still be covered by the accepted
   whole-endpoint import marker, so judge this fixture from its intended
   endpoint-domain coverage rather than a new equal-value rule.
4. Confirm KitMrp browsing, a failed stage, and an incompatible slot do not
   generate Normal/type/HCNAMES mutations or promote an absent Scene.

### Selective Bank fixture

1. Pick a Bank with at least three present local children. In Load:Bank,
   choose a proper subset that includes at least one present child and leaves
   at least one present child unselected; wait for the child preview before
   accepting `OK`.
2. Record the exact expected requested/present intersection. After completion
   and drain, verify whole non-Pattern Scene payloads only for that completed
   mask. Check the corresponding Bank metadata bytes independently, since
   BankData's existing scalar setters own them.
3. Verify every unselected resident Scene payload and its HCNAMES block remain
   byte-identical. In particular, demonstrate that a present-but-unselected
   child did not become dirty merely because it was discovered.
4. Run a valid empty-Bank/empty-intersection case: the Bank identity/metadata
   path may complete and its existing scalar marks may apply, but there must be
   no whole-Scene mutation coverage and no all-Scene fallback marking.
5. If trace `D` records wrap, treat the A/B payload/mask comparison as the
   proof; do not enlarge the trace ring to make this fixture easier to view.

For each passing fixture, the lifecycle trace should show dirty production
before a later successful `S/A/V/M/C/P/T` writer transaction. Missing early
records after a large Kit/Bank load are expected ring-wrap behavior, not proof
of a missing marker.

## Completion and documentation

After all four isolated fixtures pass, update the authoritative documents to
say exactly what is now implemented:

- `knowledge_files/specification_reference/AUTOSAVE.md` — successful normal
  Kit, KitMrp endpoint-only, root Scene without Pattern, and selective Bank
  whole-object boundaries;
  retain every existing exclusion.
- `knowledge_files/specification_reference/FILESYSTEM_SPEC.md` and
  `MODULE_INTERCHANGE_SPEC.md` — the Preset completion ownership and exact
  Bank child-mask rule.
- `MEMORY.md` plus the Session-049 handoff — implementation locations, build
  result, hardware evidence, remaining exclusions, and any fixture caveat.

Do not change `SRAM_MANIFEST.md` unless the linked output disproves the
no-allocation expectation. If it does, stop and obtain the required explicit
RAM approval rather than using reserved SRAM1/DTCM capacity.

## Session 049 implementation record — 2026-08-11

### Implemented source boundaries

- C1 normal Kit completion is implemented in
  `Core/Bank/Scene/Preset/presetManager.c:on_kit_load_complete()`. On
  `FS_STATUS_DONE`, Preset first performs its existing selected-Scene presence
  promotion, then iterates only `pm_kit_request_scene_mask` valid Scene bits
  and calls `autosave_markKitDirty()` before the common completion helper.
- C2 KitMrp is implemented in
  `preset_commitStagedKitNormalToMorph()`. Every successful same-type
  descriptor endpoint copy now calls `autosave_markInstrumentMorphDirty()`
  outside the active-Scene runtime branch. The existing generated slot-6
  track-7 Morph decay direct assignment now uses
  `scene_setSlot6Track7MorphAmpEnvelopeDecay()`, preserving its existing
  change-aware named Kit dirty bit.
- C3 root Scene completion is implemented in
  `on_scene_load_complete()`. On terminal success and after the existing
  presence promotion, each requested destination receives
  `autosave_markSceneWithoutPatternDirty()`. The marker remains at completion,
  rather than the earlier filesystem staging commit, so a later Pattern,
  Effect, or HCNAMES failure cannot publish a partial Scene load.
- C4 selective Bank completion is implemented in
  `on_bank_load_complete()`. It reads
  `filesystem_lastBankLoadSceneMask()` exactly once before acknowledgement and
  marks only its valid set bits with `autosave_markSceneWithoutPatternDirty()`;
  it does not substitute the requested, present, preview, or all-Scene mask.
- C5 uses the pre-existing InstrumentMrp marker unchanged. Its adjacent
  Autosave documentation now explicitly identifies both InstrumentMrp and
  KitMrp as its compatible-endpoint callers.

### Adjacent interface and ownership documentation

- `presetManager.c` contains input/output/why/affiliate comments next to all
  new mutation calls and the replacement named setter.
- `presetManager.h` documents the KitMrp/InstrumentMrp retained-data contract,
  including its endpoint-only dirty domains and generated Kit scalar.
- `Autosave.c` and `Autosave.h` now describe the current normal Kit, root
  Scene, exact-mask Bank, KitMrp, and InstrumentMrp caller relationships and
  their exclusions. No public function signature changed.

### Static review performed; build intentionally not attempted

- Confirmed normal Kit, root Scene, and Bank request creation still binds the
  edited completion callbacks, and KitMrp still reaches its staged commit
  through `preset_startKitMorphApply()`.
- Confirmed the normal Kit/root Scene markers precede
  `preset_completeFilesystemOp()`, the Bank mask is read before that helper,
  and no direct assignment to
  `slot6_track7_morph_amp_envelope_decay` remains in `presetManager.c`.
- Ran `git diff --check` for the four edited C/H files successfully. The
  source line endings were normalized to the repository's LF form after the
  edit, leaving a content-only diff.
- No compiler, build, firmware image, or hardware test was run, per the
  session constraint that this environment has no build toolchain. The four
  fixture groups above remain required before claiming hardware validation.

### Deliberate non-changes

- No RAM allocation, AutoSave wire-format, manifest, filesystem state-machine,
  BankData setter, SceneData API, Menu flow, writer policy, boot reader, or
  Pattern persistence change was made.
- Normal Instrument and InstrumentMrp load behavior remains as previously
  implemented; this work adds only the remaining normal Kit, KitMrp, root
  Scene, and selective Bank write-on-load coverage.
