# Narrow autosave parameter-hook follow-up

## Status and boundary

Implemented in source on 2026-08-02. No configuration or `SD_CARD/` fixture was
changed. Firmware build and static verification pass; targeted hardware
behavior remains to be tested.

This follow-up corrects the current in-progress Phase 2 Bank behavior. It does
not redesign root fallback, DSP application, filesystem transactions, the
autosave format, or completion APIs. It allocates no mask, cache, or persistent
state.

The complete target is:

- Bank name and slot are ordinary autosave payload fields, never record keys.
- `bank_active_scene_slot` is the only active-Scene SRAM owner.
- Bank Save's active fallback exists only in `bankset.bcg` write scratch.
- Bank Save dirties no Scene parameter region.
- Bank Load marks only Scenes it actually loaded.
- Bank Load never changes active Scene while the sequencer is playing.

## Current problems confirmed in code

1. `SceneData.c` retains `scene_active_index` while `BankData.c` separately
   retains `bank_active_scene_slot`. Most runtime code reads the former;
   Autosave reads the latter.
2. PERF selection and Bank Load write both copies. Bank Save can write only the
   BankData copy, so the two values can diverge.
3. Bank Save correctly chooses the lowest saved Scene for
   `op_bankset_state.active_scene` when the live active Scene is outside the
   save mask, but phase 45 currently copies that file-only value, the save
   mask, and the VOICE mask back into resident BankData.
4. `autosave_streamValidationMatchesBank()` rejects a structurally valid record
   when its Bank name/slot payload differs from live SRAM.
5. The current in-progress `autosave_markBankOperationDirty(mask)` is called by
   Save as well as Load, so Save unnecessarily re-dirties complete Scene
   regions.

## Exact behavioral rules

### Bank Save

Save reads resident Scene data; it does not mutate it. Already-drained values
are already in the autosave payload, and remaining changes remain in the SRAM
and file-carried mutation masks. Save must not clear those bits or manufacture
new whole-Scene bits.

If the authoritative active Scene is outside a nonempty save mask,
`bankset.bcg` stores the lowest set Scene in that mask. The live active Scene,
resident Scene-present mask, and resident VOICE mask remain unchanged.

After durable success, only the Bank display name and restore slot pass through
their normal change-aware setters. Saved-Scene provenance continues to update
through `settings.cfg`.

### Bank Load

Keep the existing final presence behavior:

```text
final_present = old_present | successfully_loaded_mask
```

Unavailable and unselected Bank children therefore do not erase resident
Scenes. `op_bank_scene_load_mask` already becomes the exact successfully loaded
mask and needs no companion request mask.

Resolve active Scene at the existing final metadata commit:

- playing: retain the current active Scene;
- stopped and stored Bank active is present: use stored active;
- stopped, stored active absent, current active present: retain current active;
- stopped and neither present: use the lowest bit in `final_present`;
- no final data: retain a bounded index and let the existing external fallback
  path run unchanged.

Only successfully loaded Scene regions receive whole-region Autosave marking.

### Autosave validation

Candidate validity is exact size, magic, supported format, final commit marker,
and CRC. Generation/probe chooses the newest valid peer. Bank name and slot stay
CRC-covered payload bytes and drain through ordinary mutation bits.

## Five implementation units

### 1. Make autosave validation payload-neutral

Files:

- `Core/Bank/Scene/Autosave.c`
- `Core/Bank/Scene/Autosave.h`
- `Core/Hardware/SD/filesystem.c`
- `Core/Hardware/SD/filesystem.h`

Changes:

- Delete `autosave_streamValidationMatchesBank()` and its declaration.
- Remove parsed `bank_name` and `bank_slot` members from
  `autosave_stream_validation_t` and stop extracting them during streamed
  validation.
- In filesystem candidate phase 3, accept
  `autosave_streamValidationFinish()` without a live Bank comparison.
- Correct adjacent validator/drain/recovery comments. Regeneration means both
  records are actually invalid or missing, not merely that payload changed.

What/why: this preserves the newest complete mixed-source record while name and
slot change like any other data.

Inputs: streamed A/B records. Outputs: integrity result and unchanged
generation winner selection.

Unchanged affiliates: initial slot/name creation bytes, their payload offsets,
live getters, CRC coverage, ping-pong commit ordering, and recovery writer.

### 2. Make BankData the sole active-Scene owner

Files:

- `Core/Bank/BankData.c`
- `Core/Bank/BankData.h`
- `Core/Bank/Scene/SceneData.c`
- `Core/Bank/Scene/SceneData.h`
- `Core/Menu/menu.c`
- `main.c` comment only

Changes:

- Keep `bank_active_scene_slot`, `bank_activeSceneSlot()`, and BankData's
  active-in-VOICE-mask behavior as the sole owner.
- Delete `scene_active_index` and its `scene_initAll()` reset.
- Make `scene_getActiveIndex()` return `bank_activeSceneSlot()`.
- Make bounds-safe `scene_selectActive()` delegate to
  `bank_selectActiveSceneForEditMask()`.
- Remove Menu PERF selection's second direct BankData call; its existing
  SceneData call now reaches the owner.
- Update the main initialization comment: `bank_init()` establishes active
  identity after `scene_initAll()` constructs Scene payloads. Do not change
  executable boot order.

What/why: all existing Menu, Preset, DSP, MIDI, Pattern, and Instrument callers
keep their API but observe the same byte Autosave obtains.

Inputs: existing Scene reads/selections. Outputs: one normalized active byte
and the existing active/VOICE dirty notifications.

Affiliates: all `scene_getActiveIndex()` callers, Autosave's Bank getter, PERF
selection, and Bank Load.

### 3. Keep Bank Save serialization separate from resident state

Files:

- `Core/Hardware/SD/filesystem.c`
- `Core/Hardware/SD/filesystem.h`
- `Core/Bank/Scene/Preset/presetManager.c`
- `Core/Bank/Scene/Preset/presetManager.h`

Changes:

- Retain the current lowest-set-bit adjustment of `op_bank_active_scene`, but
  document it as file-only `bankset.bcg` scratch.
- Remove Bank Save phase 45's staged BankData batch commit. Do not copy the save
  mask, adjusted file active, or saved VOICE representation into live SRAM.
- On successful Save callback, call the ordinary Bank display-name and restore-
  slot setters. They store/dirty only real changed Bank bytes.
- Preserve the current saved-Scene provenance update and settings debounce.
- Remove Bank Save's `autosave_markBankOperationDirty(save_mask)` call. Save
  marks no Scene parameter region and clears no existing bit.

What/why: `bankset.bcg` must reference one of its saved children, but saving a
subset does not replace the live workspace.

Inputs: authoritative active Scene, save mask, destination name/slot, terminal
success. Outputs: valid saved file plus unchanged live active/presence/VOICE
state and exact name/slot mutations.

Unchanged affiliates: Bank directory promotion, HCNAMES, Bank index rebuild,
flush, and Scene-source persistence.

### 4. Resolve Bank Load active Scene through the one owner

Files:

- `Core/Hardware/SD/filesystem.c`
- `Core/Hardware/SD/filesystem.h`
- `Core/Bank/BankData.c`
- `Core/Bank/BankData.h`
- `Core/Menu/menu.c`

Changes:

- Rename or document `bank_commitFilesystemSessionMetadata()` as Load-only;
  Save no longer uses it.
- Add one file-local lowest-set-bit helper with no retained storage.
- In both existing final Bank Load metadata branches, sample current active,
  use the existing final presence merge, apply the playing/stored/current/
  lowest rules above, and pass that resolved byte to the Load-only batch.
- Remove filesystem's separate `scene_selectActive(op_bank_active_scene)` call;
  the batch already updated the authoritative owner.
- In successful Menu Bank Load completion, if stopped, align
  `menu_setShownPattern()` and `seq_selectActivePattern()` to
  `bank_activeSceneSlot()` before the existing sound apply. If playing, call
  neither selector.

What/why: stored Bank active may be unavailable during partial Load, and
playback must not switch. Pattern selectors must follow a stopped active change
without recreating another active owner.

Inputs: parsed Bank active, current active, final present mask, and
`seq_isRunning()`. Outputs: one authoritative resolved active Scene and stopped
Pattern alignment.

Unchanged affiliates: child discovery, effective loaded mask, Scene payload
commit, HCNAMES, Pattern/effect loading, empty-Bank fallback, and current DSP
apply lifecycle.

### 5. Make whole-region publication Load-only

Files:

- `Core/Bank/Scene/Autosave.c`
- `Core/Bank/Scene/Autosave.h`
- `Core/Bank/Scene/Preset/presetManager.c`
- `Core/Bank/Scene/Preset/presetManager.h`
- `AUTOSAVE_PARAM_HOOK.md` notes after implementation

Changes:

- Rename the general in-progress Bank-operation marker to a Bank Load-specific
  API/comment.
- On successful Load, mark all current Bank fields plus complete non-Pattern
  Scene regions only for `filesystem_lastBankLoadSceneMask()`.
- OR into the canonical mask; never clear prior SRAM/file-carried work.
- Keep Save entirely on the scalar name/slot setters from unit 3.
- Correct adjacent comments that still describe whole-Bank “session
  replacement.”

What/why: Load assigns whole structures outside scalar setters; Save only reads
them.

Inputs: final loaded Bank metadata and exact successful loaded mask. Outputs:
Bank field bits plus only loaded Scene regions.

Affiliates: staged Load metadata, existing Scene region marker, settings
provenance, and the background drain.

## Exact code-edit inventory

This inventory expands the five units above into the complete set of concrete
edits. It does not add another behavior or source file.

### `Core/Bank/Scene/Autosave.h`

#### `autosave_stream_validation_t`

Remove only `bank_name[8]` and `bank_slot`. Retain CRC accumulator, stored CRC,
generation, bytes-seen, probe counter, and header-valid fields.

Replace the adjacent state comment:

- what: caller-owned state retains only integrity/header/generation data;
- why: payload equality is not part of candidate validity;
- inputs: sequential A/B record chunks;
- outputs: bounded validation state, still far smaller than a record;
- affiliates: `autosave_streamValidationBegin/Update/Finish()` and filesystem's
  operation-stage validator.

#### streamed validator declaration block

Change the comment that currently says update parses “Bank identity cells.” It
must instead state that update parses only control-header cells and includes
every payload byte, including Bank fields, in CRC accumulation.

Delete the complete declaration/comment block for
`autosave_streamValidationMatchesBank()`.

#### Bank whole-region declaration

Rename:

```text
autosave_markBankOperationDirty(completed_scene_mask)
```

to a Load-specific name, proposed:

```text
autosave_markBankLoadDirty(loaded_scene_mask)
```

Replace its header comment:

- what: publish one successful selective Bank Load;
- why: filesystem assigned Bank metadata and loaded Scene structures without
  scalar setter notifications;
- inputs: exact successfully loaded Scene mask after final Bank metadata;
- outputs: all Bank-field bits plus only loaded non-Pattern Scene regions ORed
  into the existing canonical mask;
- affiliates: Preset's Bank Load callback, BankData's Load-only batch, and
  `autosave_markSceneWithoutPatternDirty()`;
- exclusions: no mask clear, no Save use, no file/display work, no second mask.

No format constant, creation signature, live getter, generic resident-Bank
re-enable marker, CRC API, or writer-mask API changes.

### `Core/Bank/Scene/Autosave.c`

#### `autosave_streamValidationUpdate()`

Remove only the post-header branches that copy Bank slot/name payload bytes into
validation state. Keep the loop over every byte because all bytes remain
CRC-covered.

Correct its function comment:

- what: parse sequential control-header fields and accumulate whole-record CRC;
- why: Bank payload changes must not invalidate an otherwise sound record;
- inputs: exact contiguous record interval at `bytes_seen`;
- outputs: updated header/generation/CRC state or fail-closed header validity;
- affiliates: `autosave_recordCrcUpdate()` and filesystem candidate phase 3.

#### `autosave_streamValidationMatchesBank()`

Delete the complete function. No replacement comparison is added.

#### `autosave_markBankOperationDirty()`

Rename it to the Load-only API. Retain its current bounded loops and tracking
gate: all Bank fields are marked, then only set bits in `loaded_scene_mask`
receive `autosave_markSceneWithoutPatternDirty()`.

Replace its comment using the same Load-only contract as the header. Explicitly
state that existing canonical bits survive because every operation is OR-only.

No change to `autosave_markResidentBankDirty()`: runtime AutoSave re-enable is
the separate case that intentionally marks the complete current resident image.

### `Core/Hardware/SD/filesystem.c`

#### runtime autosave drain overview and candidate phase 3

Change the state-machine overview sentence “validates for the Bank identity” to
structural/integrity validation. Change the phase-3 label/comment from
`CRC/HEADER/BANK-NAME VALIDATION` to header/commit/CRC validation.

Replace:

```text
Finish(validation) && MatchesBank(validation, live slot, live name)
```

with `Finish(validation)` only.

The adjacent comment must describe:

- what: candidate validity is independent of payload values;
- why: copy-forward needs the previous Bank name/slot bytes as its source;
- inputs: completed stream validation;
- outputs: candidate-valid flag used by unchanged generation selection;
- affiliates: recovery phase 30, winner phase 5, and payload transform.

No change to winner ordering, mask import, capture budget, copy transform, CRC
publication, final commit marker, or regeneration phases.

#### file-local lowest-present helper

Add one helper next to the existing Bank mask/name helpers, proposed shape:

```text
static uint8_t filesystem_lowestSceneInMask(uint16_t mask,
                                            uint8_t *scene_index)
```

It loops 0..15, writes the first set index and returns one, or returns zero for
an empty mask without inventing slot 0.

Its comment must describe:

- what: resolve the lowest present Bank Scene;
- why: Load and Save need the same deterministic fallback without retained
  state;
- inputs: bounded 16-bit mask and result pointer;
- outputs: success plus lowest index, or failure for empty/NULL;
- affiliates: Bank Load final active resolution and Bank Save file-active
  formatting.

Bank Save may either call this helper or retain its identical existing loop; if
the helper is shared, remove the duplicate loop rather than keeping two paths.

#### `filesystem_loadBankDirectory_tick()`, phase 17

Remove `active_bit` and the current block that changes
`op_bank_active_scene` to the first loaded child immediately after intersecting
the request with discovered children. This value must remain the parsed
`bankset.bcg` preference until final active resolution.

Keep the separate `child_slot` scan that selects the first payload child for
the loader cursor; child traversal order is not active-Scene policy.

For the effective-zero branch:

1. read `current_active = bank_activeSceneSlot()` before the batch commit;
2. retain the existing `final_present_mask = bank_scenePresentMask()`;
3. resolve active using running/stored/current/lowest rules;
4. pass resolved active to the Load-only BankData batch;
5. leave HCNAMES phase 83 and the existing no-child completion behavior
   unchanged.

The branch comment must describe:

- what: commit loaded Bank metadata without replacing resident Scene payload;
- why: selective zero intersection is not authority to erase existing Scenes;
- inputs: parsed Bank metadata, current active/presence, sequencer state;
- outputs: final BankData with zero loaded-Scene region mask;
- affiliates: BankData Load batch and Preset Load callback.

#### `filesystem_loadBankDirectory_tick()`, phase 20

After the selected-child loop finishes:

1. read current authoritative active before BankData changes;
2. keep the existing final presence union;
3. resolve active with the same helper/policy;
4. pass that resolved value to the Load-only BankData batch;
5. remove `scene_selectActive(op_bank_active_scene)`.

The phase-20 metadata comment must no longer claim the active Scene is always a
just-loaded payload. It must describe:

- what: commit the final mixed resident metadata after all selected children;
- why: a partial Load may retain the current active Scene or choose an existing
  lowest-present Scene;
- inputs: parsed preference, old active, final presence, running state, exact
  loaded mask;
- outputs: one authoritative active Scene plus merged presence;
- affiliates: child payload loop, BankData Load batch, Menu stopped Pattern
  alignment, and Preset Load publication.

The playing check occurs at final commit time using existing `seq_isRunning()`;
no request-time playback latch is added.

#### `filesystem_requestSaveBank()`

Keep initialization from `bank_activeSceneSlot()`. Replace the current fallback
comment, and optionally route its first-set scan through the new helper.

The comment must describe:

- what: choose a valid `bankset.bcg` active child for a nonempty selected save;
- why: the live active Scene may not be included in this output directory;
- inputs: authoritative active Scene and `op_bank_scene_save_mask`;
- outputs: operation-local `op_bank_active_scene` only;
- affiliates: `op_bankset_state.active_scene` and
  `filesystem_nextBanksetLine()`;
- exclusion: no resident BankData or autosave mutation.

#### `filesystem_saveBankDirectory_tick()`, phase 45

Delete the entire call to `bank_commitFilesystemSessionMetadata()` and replace
its incorrect resident-session comment. Do not add another BankData write at
this phase.

The replacement comment next to the existing HCNAMES row update must state:

- what: the promoted directory and HCNAMES identity continue toward final
  index/flush publication;
- why: promotion is not yet durable public success and Save does not replace
  resident active/presence/VOICE state;
- inputs: promoted folder/open name and saved display name;
- outputs: filesystem/cache publication only;
- affiliates: phases 83+, Bank index rebuild, final flush, and Preset Save
  callback.

### `Core/Hardware/SD/filesystem.h`

Update only the public comments for `filesystem_requestLoadBank()` and
`filesystem_requestSaveBank()`.

Load comment additions:

- exact loaded child mask supplies the later whole-region marker;
- final presence remains the existing union with retained Scenes;
- playing preserves current active; stopped resolves stored/current/lowest;
- no individual child publishes dirty work before public success.

Save comment corrections:

- selected children and `bankset.bcg` are serialized;
- file-active may fall back to the lowest saved child;
- live active/presence/VOICE state is unchanged;
- successful Save changes ordinary Bank name/slot and provenance only;
- no Scene-region dirty publication.

Signatures remain unchanged.

### `Core/Bank/BankData.h`

#### resident Bank owner overview

Expand the top comment to state `bank_active_scene_slot` is the sole SRAM
active-Scene owner. `scene_getActiveIndex()` is a compatibility accessor, while
`bankset.bcg` and autosave are persistence consumers.

#### staged batch declaration

Rename `bank_commitFilesystemSessionMetadata()` to the Load-specific proposed
name `bank_commitFilesystemLoadMetadata()` and update its comment:

- what: atomically store the final Bank Load metadata image without scalar
  notices;
- why: filesystem commits before its public callback, so provisional field
  markers are forbidden;
- inputs: validated name/slot, final presence, resolved active, parsed VOICE
  mask;
- outputs: normalized BankData plus resident flag, no dirty bits;
- affiliates: Bank Load phases 17/20 and Preset's Load-only region marker;
- exclusion: Bank Save must use normal name/slot setters after success.

Keep the existing public active getter/selection signatures. No new owner API
is required.

### `Core/Bank/BankData.c`

#### storage declaration, `bank_init()`, and active getter/selection comments

No storage layout change is needed. Update adjacent comments so
`bank_active_scene_slot` is clearly the authoritative runtime byte initialized
by `bank_init()` and consumed through both BankData and SceneData accessors.

Do not change active normalization, `bank_ensureActiveInVoiceEditMask()`, or
ordinary active/VOICE dirty notification logic.

#### staged batch definition

Rename it to match the header/call sites and make its comment Load-only. The
implementation continues direct normalized storage and invariant repair; only
Save affiliation/language is removed.

### `Core/Bank/Scene/SceneData.c`

#### includes and retained storage

Add the BankData declaration include needed by the compatibility accessors.
Delete only `static uint8_t scene_active_index`; retain `scenes[]` and the exact
32-byte `scene_sources[]` allocation unchanged.

#### `scene_getActiveIndex()`

Replace the returned variable with `bank_activeSceneSlot()`.

New adjacent comment:

- what: expose BankData's authoritative active index through the stable
  SceneData API;
- why: existing DSP/Menu/MIDI/Pattern callers must not retain a second owner;
- inputs: BankData owner;
- outputs: current bounded active Scene;
- affiliates: BankData getter and all existing SceneData callers.

#### `scene_selectActive()`

Keep `scene_indexValid()`. On success call
`bank_selectActiveSceneForEditMask(scene_index)` and return one; delete the
local assignment.

New adjacent comment:

- what: bounds-check and delegate ordinary selection;
- why: selection must update one active owner and the existing VOICE invariant;
- inputs: resident Scene index;
- outputs: accepted selection plus BankData's change-aware notifications;
- affiliates: PERF selection and Preset/runtime apply (which remain separate).

#### `scene_initAll()`

Delete only `scene_active_index = 0u`. Update its owner comment: this function
initializes Scene payload/provenance; the adjacent later `bank_init()` owns
active identity.

### `Core/Bank/Scene/SceneData.h`

Update the comments for `scene_getActiveIndex()` and `scene_selectActive()` to
document BankData delegation, input/output behavior, and the fact that no
active byte is retained in SceneData. Signatures remain unchanged.

### `Core/Menu/menu.c`

#### `menu_refreshPerfSceneLeds()` comment

Correct “SceneData's active index” to BankData's authoritative active index
exposed through the SceneData compatibility getter. No logic change.

#### `menu_perfModeSceneButtonPressed()`

Remove the direct `bank_selectActiveSceneForEditMask()` call after
`scene_selectActive()`. Update the adjacent comment:

- what: the one SceneData compatibility call reaches BankData owner;
- why: duplicate writes concealed two-owner semantics;
- inputs: validated present Scene button;
- outputs: authoritative selection followed by unchanged Pattern/Sequencer/DSP
  ordering;
- affiliates: SceneData delegation and BankData VOICE invariant.

#### `PRESET_OP_BANK_LOAD` completion branch

After selection-current validation and before the existing loaded-child/empty
branch, add:

```text
if (!seq_isRunning()) {
    menu_setShownPattern(bank_activeSceneSlot());
    seq_selectActivePattern(bank_activeSceneSlot());
}
```

Use one local active value if needed to avoid two reads. Do not run either call
while playing. Do not change the existing fallback decision or sound-apply
lifecycle.

Adjacent comment requirements:

- what: align stopped Menu and Sequencer Pattern selectors to the resolved
  authoritative active Scene;
- why: those selectors are separate runtime/view state, not active identity;
- inputs: successful Bank Load, stopped transport, BankData active;
- outputs: aligned stopped view/playback selection;
- affiliates: filesystem final resolution and existing sound apply;
- exclusion: no playing-time switch.

No `Menu.h` change is required.

### `Core/Bank/Scene/Preset/presetManager.c`

#### `on_bank_load_complete()`

Rename its marker call to `autosave_markBankLoadDirty()` and retain
`filesystem_lastBankLoadSceneMask()` as the exact input. Preserve successful
Scene provenance and settings debounce.

Replace the marker comment:

- what: publish final loaded Bank fields and only loaded Scene regions;
- why: whole-object loads bypass scalar setters, while unselected Scenes did
  not change;
- inputs: terminal DONE and exact effective loaded mask;
- outputs: OR-only canonical bits, boot no-op behind tracking gate;
- affiliates: filesystem loaded-mask accessor and Load-only Autosave marker.

#### `on_bank_save_complete()`

Inside the DONE branch, before settings dirty publication:

1. call `bank_setDisplayName(preset_currentName)`;
2. call `bank_setRestoreBankSlot(pm_request_slot)`;
3. retain saved-mask Scene provenance;
4. call `filesystem_markSettingsDirty()` after the slot setter so settings sees
   the final slot;
5. delete the whole-region Autosave call.

Replace adjacent comments:

- what: adopt only durable saved Bank name/slot and child provenance;
- why: Save did not mutate resident Scene parameters or selection metadata;
- inputs: DONE, request name/slot, immutable save mask;
- outputs: change-aware Bank field bits and settings provenance only;
- affiliates: BankData setters, settings writer, and filesystem phase 45;
- exclusion: no Scene-region marker or canonical-mask clear.

Failed Save continues to publish none of these changes.

### `Core/Bank/Scene/Preset/presetManager.h`

Split the combined Bank Load/Save Autosave wording:

- Load stages metadata and publishes Bank fields plus the exact loaded Scene
  mask after DONE;
- Save leaves active/presence/VOICE and Scene parameters unchanged, then
  publishes only change-aware name/slot and saved-child provenance after DONE;
- both preserve prior dirty bits and failed operations publish nothing.

No Preset signature or completion enum changes.

### `main.c`

Update only the comment adjacent to `scene_initAll(); bank_init();`:

- what: SceneData constructs payload/type/provenance, then BankData initializes
  the sole active identity and Bank metadata;
- why: tagged DSP initialization still requires Scene payload first;
- inputs/outputs: cold SRAM to initialized Scene/Bank owners;
- affiliates: SceneData compatibility getter and BankData authority.

Do not change statements, ordering, boot screen behavior, or filesystem flow.

### planning documents

During implementation, append dated results to this file. After successful
verification, update only Phase 2 status/implementation notes in
`AUTOSAVE_PARAM_HOOK.md` so they no longer claim Bank Save marks Scene regions
or that name/slot are validation keys. Do not edit historical requirements to
hide the correction.

## Required adjacent documentation

Every changed `.c` and `.h` location receives nearby comment text stating what
the change does, why it exists, inputs, outputs, and affiliates. No comments are
added to unrelated modules merely to broaden the patch.

After implementation, append dated results here and correct the Phase 2 status
in `AUTOSAVE_PARAM_HOOK.md` while preserving the history of this correction.

## Verification

1. `make -j4` and `git diff --check` succeed with no new warnings.
2. `scene_active_index` is absent; `bank_active_scene_slot` is the only retained
   active selector.
3. `autosave_streamValidationMatchesBank` is absent.
4. Exactly one 3,856-byte dirty mask and the unchanged 4,608-byte transaction
   cache remain.
5. Partial Bank Save excluding active writes the lowest saved active to
   `bankset.bcg`, while live active/presence/VOICE state remains unchanged.
6. Bank Save creates no new Scene-region bits; an existing pending bit survives
   and drains afterward.
7. Stopped Bank Load chooses stored, current, or lowest-present active in that
   order. Playing Bank Load retains current active and does not switch Menu or
   Sequencer Pattern.
8. Bank Load marks only its exact successful loaded mask; unselected resident
   Scenes and existing dirty bits survive.
9. Valid records remain selectable across Bank name/slot changes; bad CRC still
   loses, and regeneration occurs only when neither peer validates.
10. No `SD_CARD/` fixture changes.

## Acceptance criteria

- One active-Scene SRAM owner.
- Bank Save fallback remains file-local.
- Bank Save changes no live Scene selection/presence/edit state and dirties no
  Scene parameters.
- Bank Load never switches active while playing.
- Only successfully loaded Scenes receive whole-region mutation bits.
- Bank name/slot are ordinary CRC-covered payload.
- No unrelated fallback, DSP, filesystem, cache, or format redesign.

## Implementation notes — 2026-08-02

### Completed validation and canonical-mask changes

- Streamed candidate validation now retains only header, generation, probe,
  length, and CRC state. Bank name/slot parsing and the live-Bank equality API
  were removed; every payload byte remains CRC-covered.
- Filesystem candidate phase 3 now accepts the completed header/commit/CRC
  result directly. Generation selection, recovery, mask import, capture,
  copy-forward, post-copy CRC publication, and commit-last ordering were not
  changed.
- The Bank operation region API is now `autosave_markBankLoadDirty()`. It ORs
  all Bank fields and only the exact successfully loaded Scene mask into the
  same canonical record. Bank Save no longer invokes a Scene-region marker.

### Completed active-Scene ownership changes

- `bank_active_scene_slot` is the sole retained active selector.
  `scene_getActiveIndex()` delegates to its getter and `scene_selectActive()`
  bounds-checks before delegating selection/invariant handling to BankData.
  SceneData's former retained byte and initialization assignment were removed.
- PERF selection now makes one compatibility selection call. The duplicate
  direct BankData selection call was removed; Pattern, Sequencer, DSP, LED, and
  repaint ordering remains unchanged.
- The boot initialization statements remain in the same order. Their adjacent
  comment now records that SceneData constructs payload/provenance first and
  BankData initializes the sole active identity second.

### Completed Bank Load/Save separation

- BankData's unnotified metadata batch is now Load-only. In both final Bank
  Load branches, final presence remains the existing union/preservation rule.
  Playing retains current active; stopped resolves stored active when present,
  then current active when present, then the lowest final-present Scene.
- The child traversal cursor no longer changes parsed active preference. The
  separate post-commit `scene_selectActive()` call was removed because the Load
  batch already updates the sole owner.
- Successful stopped Bank Load aligns Menu's shown Pattern and Sequencer's
  selected Pattern to the resolved owner before the existing apply/fallback
  branch. Playing Bank Load calls neither selector.
- Bank Save's lowest-selected active fallback remains only in operation scratch
  used to serialize `bankset.bcg`. Phase 45 no longer copies save mask,
  file-active, or VOICE state into resident BankData.
- After durable Save success, Preset adopts only the Bank display name and
  restore slot through ordinary change-aware setters, preserves the existing
  saved-child provenance update, and queues settings after the final slot is
  stored. Failed Save still publishes none of those changes.

### Verification results

- `make -j4` completed successfully. It retained only the pre-existing unused
  filesystem-helper and newlib syscall warnings; no new warning was introduced
  by these edits.
- `git diff --check` passed.
- Symbol inspection reports one `bank_active_scene_slot` byte, one
  `autosave_dirty_mask` at `0x0f10` bytes (3,856), and the unchanged
  `fs_autosave_parameter_cache` at `0x1200` bytes (4,608).
- Source search confirms `scene_active_index`,
  `autosave_streamValidationMatchesBank()`,
  `autosave_markBankOperationDirty()`, and
  `bank_commitFilesystemSessionMetadata()` are absent from executable source.
- No file under `SD_CARD/` was changed.

Hardware verification remains outstanding for partial Save file-active versus
live state, stopped/playing partial Load selection, exact Load dirty-mask scope,
pending-bit survival, payload-neutral winner selection, and corrupt-peer
recovery.

## Assignment-boundary mutation correction — 2026-08-02

Hardware testing of a root Scene Load showed that neither its selected Scene
regions nor its source provenance appeared in the returned files. Direct code
review found that the Scene settings/Kit image entered retained SRAM before
Pattern, Effects, HCNAMES, root-return, and flush work, while its compensating
whole-Scene marker was deferred to the final `FS_STATUS_DONE` callback. A later
failure could therefore leave changed SRAM with no mutation bit. Kit and Bank
Load used the same late-publication pattern.

### Corrected retained commit boundaries

- Root Scene Load now promotes its exact destination mask into BankData's
  present mask immediately before `filesystem_commitSceneStage()` assigns the
  selected `scene_t` images. Each selected Scene is marked with
  `autosave_markSceneWithoutPatternDirty()` immediately after its settings/Kit
  assignment. Pattern remains outside the current autosave format. Later
  Pattern, Effects, HCNAMES, root-return, or flush failure cannot suppress the
  already-resident non-Pattern mutation.
- Normal Kit Load now promotes its exact destination mask before assigning the
  validated staged Kit. Each `target_scene->kit = op_staged_kit` is immediately
  followed by `autosave_markKitDirty(scene_index)`. KitMrp remains staged and
  uses its existing same-type Morph commit markers; Save behavior is unchanged.
- Each validated Bank child now promotes its one destination and publishes its
  Scene-without-Pattern region beside that child's retained assignment, so a
  later child failure cannot hide an earlier SRAM replacement. Both final
  BankData metadata commit branches then call `autosave_markBankLoadDirty(0)`
  to publish Bank fields without redundantly expanding Scene scope, before
  HCNAMES and flush. The Preset callback retains only durable
  provenance/settings publication.
- Instrument Load required no code movement. Its filesystem operation only
  stages an Instrument; resident SRAM is first assigned later in
  `preset_startInstrumentApplyImage()`, where the existing
  `autosave_markWholeInstrumentDirty()` call is already directly adjacent to
  each selected destination assignment. InstrumentMrp's same-type Morph copy
  remains similarly assignment-adjacent.

### Scope retained for this correction

- Root Scene and Bank source provenance still updates only after durable public
  success. This pass intentionally corrects mutation capture first and does not
  change the separate `settings.cfg` completion policy.
- No Save path, Pattern persistence, Effect owner, autosave geometry, writer
  cadence, cache allocation, canonical-mask ownership, or SD-card test fixture
  was changed.
- The former Preset whole-Scene/whole-Kit mask walker was removed because those
  direct filesystem assignments now own publication. Preset's presence helper
  remains only for normal Instrument Load, whose retained assignment occurs
  after successful staging in Menu/Preset apply context.

### Verification results

- `make -j4` completed successfully. The output retained only the five existing
  unused filesystem-helper warnings and the existing newlib syscall warnings;
  these edits introduced no new compiler warning.
- `make img` produced `build/LXRV2_lxr02.img` at 368,596 bytes.
- `git diff --check` passed.
- Source search confirms Scene, Kit, and Bank whole-region markers now occur at
  their retained commit sites, while the Instrument marker remains adjacent to
  its existing assignment.

Hardware retest of root Scene Load mutation capture is outstanding. Because the
current input `.hcprms` fixtures begin fully dirty, the decisive result is that
the final drained selected Scene payloads match the loaded root Scene data; a
later clean-start test can independently observe the newly set region bits.

## Parameter-boundary correction — 2026-08-02

The preceding assignment-boundary correction has been superseded before its
hardware acceptance. The returned test still contained the original Bank
payload after a successful Scene Load, so Load-operation-specific whole-region
publication was removed. Staging remains quiet, but final retained commits now
use the same owners as individual parameter edits.

### Removed Load special cases

- `bank_commitFilesystemLoadMetadata()` and
  `autosave_markBankLoadDirty()` were removed. Both final Bank Load branches
  now pass already-resolved Bank name, slot, presence, active Scene, and VOICE
  mask values through the ordinary BankData setters. The resident-session flag
  remains nonserialized and is set last.
- `filesystem_commitSceneStage()` no longer assigns `scene_settings_t` and
  `kit_t` wholesale and no longer calls
  `autosave_markSceneWithoutPatternDirty()`. It still promotes selected Scene
  presence, then transfers every staged non-Pattern value through scalar/type/
  descriptor owners before initializing the non-autosaved Pattern.
- Normal Kit Load no longer assigns `target_scene->kit` or calls
  `autosave_markKitDirty()`. Normal Instrument Load no longer assigns a slot
  struct or calls `autosave_markWholeInstrumentDirty()`.
- KitMrp and InstrumentMrp no longer assign Morph endpoint arrays followed by
  a whole-Morph marker. Each compatible Morph descriptor and the generated
  Kit Morph decay now use their ordinary parameter setters.

The named whole-region marker APIs remain only as the previously requested
future copy/paste/re-enable scopes. No Load path calls them.

### Added canonical aggregate-to-parameter transfers

- `scene_storeSettingsImage()` explicitly transfers all 40 current Scene
  parameters through the existing SceneData setters: overall Morph, six voice
  Morph amounts, decimation, six audio routes, six FX sends, six fader modes,
  seven MIDI channels, and seven MIDI notes. Its header/source comments require
  future serialized Scene fields to extend this transfer beside their owner.
- `scene_storeKitSettingsImage()` transfers both current Kit parameters through
  their generated-decay setters and carries the same extension rule.
- `preset_storeInstrumentImage()` is the single Instrument type/endpoint
  transfer used by Instrument, Kit, Scene, and Bank Load. A changed type marks
  exactly its three token bytes. Every current registry descriptor transfers by
  enum index through the generic normal/Morph endpoint owner.
- When Instrument type changes, every descriptor meaningful under the new type
  is force-marked even if its raw backing-array byte compares equal. This is
  necessary because a cell that was nonexistent under the old type was not
  guaranteed to match the autosave payload. Same-type transfers remain strictly
  change-aware and coalesce repeated values.
- `preset_storeKitImage()` composes the Kit scalar transfer with six generic
  Instrument transfers. Future descriptor-table additions therefore enter all
  compound loads without a new per-type or per-load switch.
- `autosave_markInstrumentTypeDirty()` supplies the missing exact owner hook for
  the three-byte type token. It marks no endpoint or HCNAMES-owned name byte.

### Preserved scope and name ownership

- Pattern remains outside the autosave payload; the existing Pattern reset and
  direct streaming behavior was not changed.
- Effect parameter count remains zero; no fake Effect state or mutation was
  introduced.
- Bank display name is SRAM-owned and now follows `bank_setDisplayName()` on
  Load. Scene, Kit, and Instrument display names remain card-resident HCNAMES
  data with no retained live getter. This correction does not allocate a second
  name cache or pretend those bytes can be drained as SRAM parameters; their
  eventual autosave capture needs a separate explicit HCNAMES-value design.
- Presence promotion remains an ordinary Bank scalar mutation at the retained
  commit boundary. Source provenance remains the separate `settings.cfg`
  durability concern and was not changed here.
- Autosave format, canonical-mask size/ownership, drain cadence, transaction
  cache, CRC/commit sequence, filesystem staging, and `SD_CARD/` fixtures were
  not changed.

### Verification results

- `make -j4` completed successfully. It retained only the five pre-existing
  unused filesystem-helper warnings and the existing newlib syscall warnings;
  no new warning was introduced.
- `make img` produced `build/LXRV2_lxr02.img` at 369,944 bytes.
- `git diff --check` passed.
- Linked-symbol inspection still reports exactly one 3,856-byte
  `autosave_dirty_mask` and the unchanged 4,608-byte
  `fs_autosave_parameter_cache`; this correction allocated no retained mask or
  aggregate cache.
- Source searches confirm that executable code contains no
  `bank_commitFilesystemLoadMetadata()`, `autosave_markBankLoadDirty()`, direct
  final Scene-settings/Kit/Instrument aggregate assignment, or whole-region
  marker call in filesystem/Preset Load paths.
- Hardware retest remains required. The targeted test is a clean drained input,
  root Scene Load into several inactive destinations while playing, exit to an
  autosave-eligible menu, and enough time for the normal 5-second debounce plus
  continuation drain. The output Scene parameter regions should match the
  loaded source while unrelated Scenes remain unchanged.

## Two-byte loaded-Scene register replacement — 2026-08-02

The parameter-boundary transfer above is superseded specifically for complete
root Scene loads and Bank-child Scene loads. Ordinary Menu/MIDI edits retain
their exact scalar dirty hooks. A complete Scene replacement now produces one
coalesced resident event and the asynchronous writer expands that event into
the existing canonical mutation record.

### Resident commit and exact SRAM ownership

- `Autosave.c` owns one `volatile uint16_t` loaded-Scene register. Its linked
  size is exactly two bytes. A successful retained Scene commit atomically ORs
  the destination bit; OR semantics intentionally coalesce repeated loads and
  cannot cancel two pending loads as XOR/toggle semantics would.
- `filesystem_commitSceneStage()` directly copies the fully validated staged
  Scene settings and Kit into each selected destination, then arms that Scene's
  bit. Root Scene Load and each selected Bank child share this final resident
  boundary. Pattern retains its separate reset/stream path because Pattern is
  outside the autosave record.
- The failed Scene aggregate-to-scalar transfer was removed. The generic
  Instrument/Kit transfers remain for their own root load paths, and ordinary
  individual parameter setters remain the owners of exact edit mutations.
- Disabling/discarding an autosave session clears both the canonical mutation
  record and the loaded-Scene register. No file interaction occurs at the
  Scene commit boundary.

### Drain-side expansion and two-record publication

- The idle scheduler treats either a nonempty canonical mask or a nonzero
  loaded-Scene register as pending work, so a Scene load wakes an otherwise
  clean writer after the existing five-second debounce.
- Drain initialization snapshots the two-byte register without clearing it and
  ORs every bit in each selected 1,920-byte Scene region into the one existing
  3,856-byte canonical SRAM mask. This includes names, parameters, and reserved
  cells; the existing live-byte classifier captures implemented owners and
  closes cells that do not currently exist.
- After winner validation and file-mask merge, a loaded-Scene snapshot enters
  a mask-only publication mode before any mutation bit is taken. The existing
  transformed copy, CRC-after-data publication, commit-last publication, and
  filesystem sync sequence writes the complete canonical mask into the
  inactive peer. That new peer is promoted and the same unchanged mask is then
  committed back into the other record.
- Only after both CRC-valid records have synced with the complete Scene mask
  does the writer acknowledge the operation's Scene-register snapshot. It then
  enters the unchanged bounded parameter classification/get/drain path. Thus
  both records first receive the full recovery request; normal ping-pong drain
  generations may subsequently carry progressively smaller masks as intended.
- A failure before the second mask publication does not acknowledge the Scene
  register. The next writer transaction re-expands and retries the request.
  If neither input record validates, the existing two-file recovery completes
  first; the still-armed Scene register schedules the publication/drain retry.

### Scope retained

- No autosave file geometry, mask size, debounce, get cap, parameter cache,
  settings format, Pattern persistence, Effect implementation, or SD-card
  fixture changed.
- The two mask-publication passes reuse the writer's existing 2 KiB operation
  union. The only new persistent SRAM is the approved two-byte Scene register.
- Scene-source provenance remains the separate `settings.cfg` completion path;
  this change only guarantees that a resident Scene load requests autosave
  parameter capture.

### Verification results

- `make -j4` completed successfully. It retained only the pre-existing unused
  filesystem-helper and bare-metal newlib syscall warnings; no new warning was
  introduced.
- `make img` produced `build/LXRV2_lxr02.img` at 369,884 bytes.
- `git diff --check` passed.
- Linked-symbol inspection reports the loaded-Scene register at `0x00000002`
  bytes, the unchanged canonical mask at `0x00000f10` bytes (3,856), the
  unchanged parameter cache at `0x00001200` bytes (4,608), and the unchanged
  filesystem stage union at `0x00000800` bytes (2,048).
- Source search confirms `scene_storeSettingsImage()` is absent and the only
  executable Scene-load notification is adjacent to the aggregate resident
  Scene assignment in `filesystem_commitSceneStage()`.

Hardware verification remains outstanding. Start from drained valid records,
load one Scene into known destinations (or load selected Bank children), leave
the Load/Save menu, and allow the five-second debounce plus continuation drain.
The selected autosave Scene payloads should then match the loaded resident
parameters; unrelated Scene payloads should remain unchanged.

## Root Scene notification moved to Menu selection — 2026-08-02

The first hardware run with the two-byte loaded-Scene register produced valid,
fully drained generations but no root Scene replacement in the payload. A
quick-exit diagnostic also found no on-card Scene mask before the drain. For the
next targeted test, root Scene ownership is moved earlier than the filesystem
load: the destination register now mirrors the Scene Load LED selection.

### Changes completed

- `Autosave.c/.h` now expose `autosave_setSceneLoadSelectionMask()`. While
  mutation tracking is enabled, it atomically replaces the exact existing
  two-byte pending register from a complete 16-bit Menu selection. It does not
  expand the canonical mask and performs no file operation.
- `menu.c` explicitly includes `Autosave.h` and owns one filtered helper that
  publishes only top-level Load:[Scene]. Save, Bank, Kit, Global, and nested
  Instrument contexts cannot alter the register through this helper.
- Fresh LOAD_PAGE entry publishes the active-Scene default after the restored
  load type is known. A later type reset into Scene Load publishes the reset
  active-Scene selection. Every accepted Scene SEQ-button toggle then replaces
  the register from the resulting LED-backed `menu_kitLoadSceneMask` before
  repaint.
- `filesystem_commitSceneStage()` still directly copies validated root Scene
  settings/Kit, but root Scene Load no longer arms the register there. The
  earlier Menu selection is its sole notification. Bank Load retains the
  post-copy OR for each child because Bank traversal must not publish a child
  that failed before its resident commit.
- The drain-side two-record full-mask publication, acknowledgement ordering,
  bounded parameter gets, cache size, cadence, and CRC/commit sequence remain
  unchanged.

### Behavioral boundary

Entering or toggling Scene Load changes SRAM only. Load/Save still pauses the
autosave writer; after leaving the menu the existing five-second debounce must
expire before the writer can publish the complete selected Scene regions into
both files and begin normal draining. This preserves the rule that diagnostic
selection code performs no SD interaction.

### Verification results

- `make -j4` and `make img` completed successfully with only the existing
  unused filesystem helper and bare-metal newlib syscall warnings.
- `make img` produced `build/LXRV2_lxr02.img` at 369,996 bytes.
- `git diff --check` passed.
- Linked BSS remains 78,468 bytes; no second selection or mutation mask was
  allocated.
- Hardware verification of the earlier root Scene signal remains outstanding.

## Boot all-Scene register diagnostic — 2026-08-02

To isolate the Scene-register-to-canonical-mask boundary from Menu and Scene
Load signaling, successful boot autosave authorization now deliberately arms
all sixteen bits of the existing two-byte loaded-Scene register.

### Changes completed

- `Autosave.h` defines `AUTOSAVE_ALL_SCENES_MASK` as the fixed-format
  `0xffff` value beside `AUTOSAVE_SCENE_COUNT`, with a static assertion binding
  the mask to the sixteen-Scene wire contract.
- `filesystem_ensureAutosaveFilesBlocking()` calls
  `autosave_setSceneLoadSelectionMask(AUTOSAVE_ALL_SCENES_MASK)` only after a
  resident Bank's missing-file ensure succeeds durably, autosave authorization
  is published, and mutation tracking is enabled. No-Bank fallback, autosave
  OFF, and setup failure do not arm the diagnostic.
- The boot call changes only the existing two-byte SRAM register. It performs
  no synchronous canonical-mask expansion and no SD write. The normal delayed
  recovery/drain must observe the nonzero register, expand all sixteen complete
  1,920-byte Scene regions, publish that mask into both records, acknowledge
  the register, and continue through the existing bounded getter.
- This is explicitly a temporary diagnostic hook. Menu selection, Bank-child
  signaling, file format, cache size, debounce, get cap, and CRC/commit ordering
  were not changed.

### Verification results

- `make -j4`, `make img`, and `git diff --check` passed with only the existing
  unused filesystem-helper and bare-metal newlib syscall warnings.
- Linked symbols remain exactly two bytes for
  `autosave_loaded_scene_pending_mask`, 3,856 bytes for
  `autosave_dirty_mask`, 4,608 bytes for `fs_autosave_parameter_cache`, and
  2,048 bytes for `fs_stage_workspace`.
- `build/LXRV2_lxr02.img` is 370,084 bytes.

Hardware verification remains outstanding. Starting from clean valid records,
boot and leave the unit outside Load/Save. The writer should begin after the
normal five-second interval. Power interruption during the drain should expose
Scene-region mutation bits; an uninterrupted run should eventually return both
records to an empty mask after capturing all implemented Scene data.

## Boot diagnostic result and Scene Menu overwrite correction — 2026-08-02

The all-Scene boot diagnostic succeeded and isolated the remaining failure from
the writer. After roughly eight seconds, the returned records showed the exact
expected full-Scene expansion and bounded continuation state:

- `.hcprms1` was a valid 34,768-byte generation 27 record with 30,720 dirty
  bits: exactly 1,920 bits for each of all sixteen Scenes and no Bank bits.
- `.hcprms2` was the expected invalid/truncated power-cut generation 28 record.
  It had drained Scenes 00--02 and part of Scene 03 before ending at 32,768
  bytes. The older valid peer therefore remained recoverable.
- This proves the two-byte Scene register is observed while the canonical mask
  is otherwise empty, wakes the idle scheduler, expands complete Scene regions,
  publishes the full request, and enters the existing 1,536-get continuation
  drain. The temporary boot arming hook is no longer needed.

### Exact Menu failure found

Direct code tracing found that Scene LED entry and toggles did write the
two-byte register. The value was later destroyed by successful command cleanup:

1. `menu_finishLoadSaveCommand()` called `menu_resetSaveParameters()`.
2. That reset called `menu_resetLoadSaveSceneSelection()`.
3. The helper reset the visible selection to the current active Scene.
4. It then called the replacement-style
   `autosave_setSceneLoadSelectionMask()` with that default.

Consequently, a successful load into five non-active Scenes was replaced by the
one active-Scene bit before autosave could begin after leaving the menu. This
was not a drain failure and was not evidence that the LED handler failed to
execute; it was an explicit later overwrite by the same Menu owner.

### Targeted changes completed

- `menu.c` now separates the public ordinary reset from one private reset
  implementation whose input controls only Scene-selection publication.
  Normal page entry, type changes, and LED selection resets pass publish=true
  and preserve the requested early Menu behavior.
- `menu_finishLoadSaveCommand()` uses publish=false. It still resets the cursor,
  default Scene LEDs, storage gate, and repaint exactly once, but it cannot
  replace a successfully completed Scene Load's accepted autosave mask with the
  active-Scene UI default.
- `presetManager.c` reaffirms `pm_kit_request_scene_mask` through
  `autosave_setSceneLoadSelectionMask()` only after root Scene Load reports
  `FS_STATUS_DONE`. This is the immutable mask accepted with the request. It
  closes both the proven terminal-reset overwrite and the smaller race in which
  an autosave transaction already active when the menu opened could consume the
  provisional LED selection before the Scene filesystem request was accepted.
- `menu.h`, `presetManager.h`, `Autosave.c/.h`, and the root Scene commit comments
  in `filesystem.c` document the two publication moments and the UI-reset
  preservation rule beside their affiliates.
- The temporary `AUTOSAVE_ALL_SCENES_MASK`, its static assertion, and the boot
  call that armed every Scene were removed. Boot again begins with only real
  recovery, scalar mutations, and load notifications.

No file geometry, mutation-mask allocation, writer cadence, parameter getter,
CRC ordering, Scene source encoding, or Bank-child notification was changed.
The next hardware test should load known non-active Scenes, exit Load/Save, and
interrupt during or after the first five-second drain. The selected complete
Scene regions should now be visible without any boot-generated Scene bits.

### Verification results

- `make -j4` completed successfully. The only compiler/linker diagnostics were
  the existing unused filesystem helpers and bare-metal newlib syscall notices.
- `make img` produced `build/LXRV2_lxr02.img` at 370,012 bytes.
- `git diff --check` passed.
- Linked BSS remains 78,468 bytes. Symbol inspection confirms the existing
  loaded-Scene register is still exactly 2 bytes, the canonical mutation mask
  is still 3,856 bytes, the parameter cache is still 4,608 bytes, and the
  filesystem stage workspace is still 2,048 bytes. No second Scene or mutation
  record was introduced.

## Complete-object markers for normal Kit, Instrument, and Bank Load — 2026-08-03

Hardware confirmed the corrected root Scene selection survives Menu cleanup and
produces real header, mask, and payload differences. The same complete-object
rule is now applied to the remaining normal load types without allocating a
second mutation record.

### Exact pre-change behavior

- Normal Kit Load committed its two Kit settings and all six Instrument images
  through change-aware scalar/descriptor owners. Equal incoming bytes therefore
  produced no mutation bit, even though they were part of the loaded Kit.
- Normal Instrument Load used the same change-aware image transfer when Menu
  applied the validated stage after filesystem completion. It likewise marked
  only differing type/descriptor cells.
- Bank Load committed its five BankData field groups through change-aware
  setters. Its successfully committed child Scenes already ORed their bits into
  the two-byte loaded-Scene register after each direct Scene assignment; that
  Scene behavior was correct and required no replacement.

### Changes completed

- `Autosave.c/.h` add `autosave_markBankDataDirty()`. It marks every gettable
  BankData field—restore slot, eight-byte Bank name, resident Scene-present
  mask, authoritative active Scene, and VOICE edit mask—without marking Scene
  payload or reserved Bank padding. `autosave_markResidentBankDirty()` now
  reuses this helper before its existing present-Scene loop.
- `filesystem_storeLoadedBankMetadata()` calls the BankData-only marker after
  all normalized BankData setters and the resident-Bank flag have committed.
  Each actual Bank child continues using `autosave_noteSceneLoaded()` after its
  own resident Scene copy. Thus a Bank Load publishes exactly BankData plus its
  successfully committed selected Scenes; requested-but-missing and unselected
  Scenes are not added.
- `preset_storeKitImage()` emits one `autosave_markKitDirty()` only after both
  Kit settings and all six Instrument transfers succeed for one destination.
  Exact changed-cell hooks remain intact, while the final aggregate marker adds
  equal incoming live cells to the same canonical mask. The call is outside the
  Instrument loop, so one destination produces one Kit event.
- Normal Instrument apply emits one
  `autosave_markWholeInstrumentDirty()` immediately after the staged Instrument
  image successfully commits to each selected destination and before active-
  Scene-only DSP work. Invalid destinations/types publish nothing. Inactive
  selected Scenes are covered exactly like the active Scene.
- Normal Kit and Instrument aggregate markers cover currently gettable payload:
  Kit settings, Instrument type tokens, normal descriptors, and Morphable Morph
  descriptors. Their HCNAMES-owned display names remain excluded because those
  names are not resident per-Scene parameters and the current autosave live-byte
  getter deliberately cannot sample them.
- KitMrp and InstrumentMrp remain endpoint-only operations. They do not replace
  a complete Kit or Instrument and therefore retain their existing same-type,
  changed-Morph-descriptor marking rather than falsely dirtying normal/type
  fields.

No writer scheduling, debounce, continuation cap, CRC ordering, file geometry,
Scene aggregate register, Bank selection semantics, settings persistence, or
SRAM allocation changed. Hardware verification should exercise normal Kit,
normal Instrument, and Bank Load independently from clean/drained records, then
leave Load/Save and interrupt after the normal five-second debounce.

### Verification results

- `make -j4` completed successfully with only the existing unused filesystem
  helper and bare-metal newlib syscall warnings.
- `make img` produced `build/LXRV2_lxr02.img` at 370,148 bytes.
- `git diff --check` passed.
- Linked BSS remains 78,468 bytes. The loaded-Scene register remains 2 bytes,
  the canonical mask 3,856 bytes, the parameter cache 4,608 bytes, and the
  filesystem stage workspace 2,048 bytes. The new behavior adds no SRAM owner,
  queue, or second mutation mask.

## Load/Save exclusion and compact complete-load records — 2026-08-04

Hardware output from the first normal-Instrument test exposed an ownership
error in the scheduling boundary: preventing a *new* autosave start while the
Load/Save page was visible did not itself guarantee that the page opened only
after an already-active autosave transaction had released the filesystem. The
returned records advanced from generations 0/1 to 32/33 and contained the old
resident Instrument values, demonstrating that the old backlog transaction was
still the file publisher during the user session.

The targeted correction does not abort AsyncFATFS, add another mutation mask,
change the record format, or alter the parameter getter:

- `filesystem.c/.h` now expose one narrow Load/Save suspension setter and one
  autosave-transaction ownership query. Menu arms suspension on the initial
  Load/Save gesture. This prevents setup, validation, ordinary debounce, and
  continuation starts until final page exit.
- An already-active writer is allowed to reach its existing close/final-flush
  callback while Menu remains on the prior page. It is not frozen beneath open
  file handles, which would permanently retain the sole filesystem facade and
  prevent Load/Save from opening.
- `menu.c` retains one byte only when this safe-entry wait is necessary.
  `menu_serviceRuntimeWidgets()` observes the transaction release and invokes
  the ordinary Load-page entry exactly once. Thus no autosave state-machine
  tick occurs while `menu_activePage` is Load or Save, and every existing cache,
  LED, selection, and repaint path remains the normal page-entry path.
- Final Load/Save exit clears suspension. Load/Save-to-Save toggling retains it.
  A different page gesture while entry is pending cancels the pending entry and
  clears suspension.

Complete-object load ownership now uses the explicitly approved compact SRAM
records in `Autosave.c`:

- one byte flags final BankData replacement;
- two bytes coalesce the exact destination Scenes of complete Kit loads;
- six bytes retain one `Scene + 1` coordinate for each Instrument slot.

The six Instrument bytes cover all 96 Scene/slot coordinates without pretending
that six bytes are a 96-bit bitset. If the same slot is loaded into another
Scene before drain, its displaced coordinate is first marked in the canonical
mask and the newest coordinate remains in that slot byte. Since autosave is
suspended for the complete Load/Save session, none of those bits can be
classified before entry exits.

`presetManager.c` and the final Bank metadata commit now queue these complete
load events instead of directly invoking their whole-object dirty markers.
Changed individual cells still pass through their existing parameter-boundary
hooks. On the first post-menu autosave operation, `filesystem.c` atomically
snapshots the Bank/Kit/Instrument records beside the existing Scene record,
expands all snapshots through the existing typed whole-object markers, and
performs the same two mask-only ping-pong commits before taking any bit. Only
after both CRC-valid records carry the complete dirty scopes are the matching
compact notifications acknowledged and normal bounded parameter capture begun.
Errors retain the notifications and the existing captured-offset rollback.

No `.hcprms` fixture, format offset, debounce, 1,536-get cap, CRC/commit order,
source tracking, Morph-only load behavior, or Save behavior was changed.

### Verification results

- `make -j4` completed successfully with only the existing unused filesystem
  helper and bare-metal newlib syscall warnings.
- `git diff --check` passed.
- Linked symbols confirm exactly 1 byte for the Bank notification, 2 bytes for
  the Kit mask, 6 bytes for the Instrument records, and 1 byte for deferred
  Menu entry. The canonical mask remains 3,856 bytes, the parameter cache
  remains 4,608 bytes, and the filesystem stage workspace remains 2,048 bytes.
- The linked image reports 78,492 bytes of BSS. The packaged firmware image is
  371,348 bytes.
- Hardware verification remains outstanding. The next normal-Instrument test
  should enter Load/Save, change the five intended Scene-15 slots, exit to Voice,
  wait for the five-second debounce plus drain, and compare the newest valid
  payload against the five selected Instrument sources.

### First hardware retest: scheduler-arm regression and correction

The mounted card used the newly built 371,348-byte image with AutoSave enabled,
but both hidden records remained byte-for-byte identical to the input pair:
generation/probe/CRC did not advance at all. Direct code tracing found the
cause in the new exclusion gate rather than in the load records or parameter
getter.

The scheduler returned on `fs_autosave_load_save_suspended` before inspecting
the canonical mask or compact Load records. Consequently it could not arm the
five-second SRAM deadline while the menu was open. The first tick after exit
only armed a brand-new five-second delay; powering down at approximately five
seconds reached the operation-start boundary without enough time to validate or
commit either peer. This exactly explains the unchanged files.

The suspension check now exists only at filesystem-operation admission. The
scheduler may observe pending SRAM work and retain its ordinary due tick during
Load/Save, but it still cannot start pair setup, validation, file reads,
parameter gets, writes, or continuation operations until Menu exits. An elapsed
deadline therefore starts promptly after exit instead of silently adding a
second full debounce. Runtime pair setup has the same start-only exclusion.
No file format, load record, mask geometry, get cap, or debounce duration was
changed.

## Failure postmortem — 2026-08-06

The final hardware retest disproved the scheduler-arm correction above.
Neither hidden record changed, and mutation bits already present in the records
did not drain. That result is broader than a failed Instrument-load hook: the
autosave drain transaction was not being admitted at all.

### What I got wrong

1. I treated an unchanged A/B pair as evidence for one timing explanation when
   it only proved that no commit completed. The same output is compatible with
   a disabled writer, failed setup authorization, a stuck suspension flag, a
   false resident-Bank gate, an unobserved pending record, or a filesystem-start
   rejection. I selected the debounce explanation without demonstrating which
   scheduler boundary had actually been reached.

2. I changed the deadline behavior before proving the full runtime path:
   producer/dirty state, scheduler observation, deadline arming, suspension
   release, operation admission, validation, mask merge, parameter capture,
   and A/B commit. The hardware test supplied only the final file state, so the
   proposed fix was not supported by a positive observation at any intermediate
   boundary.

3. I added too many silent lifecycle gates around an already working writer.
   The failed implementation depends on `fs_autosave_enabled`, resident-Bank
   state, boot-ready authorization, setup pending/failed state, recovery state,
   writer-armed state, transaction-active state, Load/Save suspension, facade
   readiness, active Menu page, the canonical mask, and four aggregate-load
   records. Most scheduler failures merely return and leave no durable evidence
   identifying which predicate rejected the work. That made the implementation
   fragile and made my later diagnosis speculative.

4. The compact Bank/Scene/Kit/Instrument records created another deferred
   ownership layer in front of the canonical mutation mask. Those records are
   expanded only inside the drain operation, but the records cannot help start
   or diagnose that operation if one of the outer lifecycle gates prevents the
   drain from running. This moved complete-load publication away from the
   already proven canonical-mask path and recreated the semantic-direction risk
   that had been explicitly called out earlier.

5. I coupled autosave scheduling to Menu page-entry deferral. That expanded a
   parameter-publication change into UI transition and filesystem-ownership
   behavior. Even if that exclusion was conceptually justified, I did not
   demonstrate on hardware that suspension was armed and cleared at the intended
   boundaries before stacking the aggregate-load mechanism on top of it.

6. I relied too heavily on compilation, image size, symbol size, CRC parsing,
   and static inspection. Those checks established that the image was coherent
   and fit in memory; they did not establish that the runtime writer remained
   reachable. I reported the timing change as a fix when it was only an
   unverified hypothesis.

### What can and cannot be concluded

The final result establishes that the regression is in the common writer
admission/lifecycle path, not merely in the Instrument-load producer: existing
mutation bits also failed to drain. From the returned files alone, I cannot
honestly identify which one of the added runtime predicates remained false or
stuck. Naming a specific predicate now would repeat the same unsupported
diagnosis.

The correct stopping point was the last hardware-proven implementation at
commit `326a8a1`, where single-parameter mutation gets and the writer drain were
working. I should have preserved that path, introduced only one independently
observable change at a time, and required a hardware result proving that change
before adding Load/Save exclusion or aggregate-load records. Instead I changed
producer ownership, scheduling, filesystem admission, and Menu behavior in one
chain, then attempted to repair the final symptom without isolating the first
failed boundary.
