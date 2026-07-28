# Targeted Load: Bank fix

## Scope

This plan addresses only the failure where selecting `Load: Bank`, leaving the
Bank number at `000 Full`, and activating `ok` appears to hang without restoring
any resident Scenes or Kits.

The fix is confined to `Core/Menu/menu.c`. It does not change the Bank, Scene,
Kit, or Instrument payload loaders; the on-card format; boot loading; the
existing `ok`/`OW` command presentation; or any SRAM-resident data structure.
It requires **0 bytes of SRAM growth**.

## Confirmed cause

The failure occurs before `filesystem_requestLoadBank()` starts. It is not a
failure in the shared Scene payload parser or in the 16-child Bank loop.

The failing menu sequence is:

```text
enter Load: Bank
  -> menu_resetLoadSaveSceneSelection()
       menu_kitLoadSceneMask = 0
  -> menu_requestCurrentLoadSaveSelection(0)
       /Bank/.hcindex is not resident yet
  -> menu_requestLibraryIndexLoad(SAVE_TYPE_BANK)
  -> filesystem_requestLoadBankIndex(menu_libraryIndexLoadComplete)
  -> menu_libraryIndexLoadComplete()
       releases menu_storageBusy and repaints
       DOES NOT request the selected Bank's child-Scene preview

activate ok without changing Bank number
  -> preset_loadBank(0, menu_kitLoadSceneMask)
  -> preset_loadBank(0, 0)
  -> filesystem_requestLoadBank() rejects the zero mask
  -> no Bank filesystem operation, payload load, or completion callback exists
```

`Load: Bank` intentionally starts with a zero Scene mask because its selectable
bits must come from the actual `00..15` child directories in the highlighted
Bank. `menu_bankLoadPreviewComplete()` is the only code that publishes that
physical child mask to `menu_kitLoadSceneMask`. When Bank entry first has to
load `/Bank/.hcindex`, the index callback stops one step too early and the
preview is never requested.

Changing the Bank number after the index is resident calls
`menu_requestCurrentLoadSaveSelection()` again and can start the preview. That
is why the defect is specific to entering Bank Load and accepting the initially
highlighted Bank. Root Scene Load has no child-preview prerequisite, so it does
not have this failure.

There is also a small race after a preview has been requested:
`menu_requestBankLoadPreview()` currently leaves `menu_storageBusy` clear while
the filesystem scans the Bank directory. The user can therefore reach and
activate `ok` before `menu_bankLoadPreviewComplete()` installs the nonzero
mask. That produces the same rejected zero-mask request. The preview must own
the existing Menu storage gate until its result is published.

## Targeted changes

### 1. Continue Bank-index completion into the selected Bank preview

**File:** `Core/Menu/menu.c`

**Function:** `menu_libraryIndexLoadComplete()`

After a successful `/Bank/.hcindex` load, if the Menu is still on top-level
`Load: Bank`, immediately call:

```c
menu_requestBankLoadPreview(
    menu_currentPresetNr[SAVE_TYPE_BANK]);
```

Perform this after releasing the index request's ownership of
`menu_storageBusy`, so the preview helper can post the next filesystem
operation and take ownership of the same gate. If the preview request is
accepted, do not execute a second completion repaint that implies the Bank
selection is ready; the preview callback owns the final mask/LED repaint.

**Why this must exist:** loading the root Bank index and scanning one selected
Bank's children are two serial filesystem operations. The first callback is the
only reliable continuation point when Bank Load is entered with no Bank index
resident. Without this continuation, slot `000` is displayed but has no
selectable child mask and `ok` can never post a valid Bank request.

**Inputs:**

- successful filesystem status from the completed Bank index load;
- current page and top-level type;
- current `SAVE_TYPE_BANK` slot, including the unchanged default slot `000`;
- the newly resident Bank index cache.

**Outputs:**

- one `filesystem_requestScanBankScenes()` request for the currently displayed
  Bank;
- no Scene, Kit, BankData, or audio parameter mutation;
- no callback or phase change in the actual Bank payload loader.

**Adjacent comment requirement:** document beside the continuation that Bank
index completion is not selection readiness. A Bank Load mask is valid only
after the selected Bank's physical child scan has completed.

### 2. Make the child preview own the existing input gate

**File:** `Core/Menu/menu.c`

**Functions:**

- `menu_requestBankLoadPreview()`
- `menu_bankLoadPreviewComplete()`

When `filesystem_requestScanBankScenes()` accepts the preview request, set the
existing `menu_storageBusy` flag. Do not set the explicit
`menu_loadSaveCommandActive` flag: the preview is preparatory browser work, not
an activated `ok`/`OW` command, so it must not display `...` or perform the
command-final cursor reset.

In `menu_bankLoadPreviewComplete()`, release `menu_storageBusy` before the final
repaint. On successful completion for the same page, type, and Bank slot,
retain the existing behavior:

```c
menu_bankLoadPreviewMask = filesystem_bankChildSceneMask();
menu_bankLoadPreviewValid = 1;
menu_kitLoadSceneMask = menu_bankLoadPreviewMask;
```

Then refresh the Scene LEDs and repaint. The input gate ensures the user cannot
activate `ok` between clearing the old mask and publishing this result.

If the preview request is rejected because another filesystem operation still
owns the single operation slot, retain the existing deferred-selection retry;
do not claim or clear another operation's `menu_storageBusy` ownership.

**Why this must exist:** the load request requires a nonzero mask, and that mask
does not exist until the asynchronous child scan completes. Allowing input
during that interval exposes an invalid intermediate Menu state as an
actionable `ok` row.

**Inputs:**

- selected Bank slot;
- Bank index occupancy;
- `filesystem_requestScanBankScenes()` accepted/rejected result;
- completion status and the existing same-page/same-slot guards.

**Outputs:**

- input is locked only for the duration of the preparatory child scan;
- successful preview publishes the physical `000 Full/00..15` mask
  (`0xffff` for the supplied fileset);
- `ok` becomes actionable only after the nonzero mask is installed;
- an accepted `preset_loadBank(0, 0xffff)` then enters the already-existing
  Bank loader and its shared Scene parsing path.

**Adjacent comment requirement:** state that `menu_storageBusy` serializes the
index-to-preview-to-command boundary, while `menu_loadSaveCommandActive`
continues to represent only an accepted user command.

## Explicit non-changes

Do not modify:

- `filesystem_requestLoadBank()` or its boot name-repair handoff;
- `filesystem_loadBankDirectory_tick()`;
- `filesystem_loadSceneDirectory_tick()`;
- Bank child iteration, HCNAMES, or `/Bank/.hcindex` restoration;
- `preset_loadBank()` or its completion callback;
- Instrument type parsing, LFO parsing, or runtime modulation application;
- `main.c` boot loading;
- the existing `...`/cursor command lifecycle; or
- `SRAM_MANIFEST.md`.

The existing Bank loader already receives `0xffff` at boot and already delegates
each selected Bank child to the shared Scene parser. Reworking that loader would
not fix this Menu-side zero-mask rejection and risks regressing the known-good
boot path.

## Verification

1. Build the firmware and run `git diff --check`. Confirm linked SRAM totals do
   not grow.
2. Enter `Load: Bank` from a different top-level type so `/Bank/.hcindex` must
   be loaded. Do not turn the Bank number.
3. Confirm input is briefly gated while the child preview scans `000 Full`, then
   all 16 Scene LEDs reflect its `00..15` children.
4. Activate `ok`. Confirm it changes to `...`, proving the Bank request was
   accepted rather than rejected with a zero mask.
5. Before the load, alter every resident Scene Kit. After completion, switch
   through all 16 Scenes and confirm each Kit was restored from
   `SD_CARD/Bank/000 Full/`.
6. Confirm completion restores the type-row `[]` cursor and `ok` text through
   the existing command lifecycle.
7. Repeat after turning to another Bank and back to `000`; confirm the same
   preview gating and load behavior.
8. Power-cycle and confirm the existing at-boot Bank restore is unchanged.

## Investigation note

- 2026-07-27: Traced the request from Menu activation through Preset and
  filesystem admission. The failing default-slot path never reaches
  `FS_INTERNAL_OP_LOAD_BANK`: Bank type entry clears the destination mask,
  loads the root Bank index, and fails to continue into the child preview that
  alone repopulates that mask. The plan was reduced to the two Menu-side
  handoff/gating changes above; no firmware source or SRAM layout was changed.
- 2026-07-27: Implemented the targeted fix in `Core/Menu/menu.c` only.
  `menu_libraryIndexLoadComplete()` now continues a successful top-level
  Load:Bank index load into `menu_requestBankLoadPreview()` for the unchanged
  highlighted slot. An accepted preview owns the existing
  `menu_storageBusy` input gate until `menu_bankLoadPreviewComplete()` releases
  it and atomically publishes the child mask. The preview does not set
  `menu_loadSaveCommandActive`, so the existing `...`/cursor lifecycle still
  begins only after `preset_loadBank()` accepts the explicit `ok` click.
- 2026-07-27: Added adjacent input/output/ownership comments at every changed
  code boundary. No public declaration or contract changed, so no `.h` file
  required modification.
- 2026-07-27: Verification completed: `git diff --check` passes and the normal
  firmware build succeeds. Linked size is `text=351,924 B`, `data=400 B`,
  `bss=69,948 B`; BSS is unchanged from the pre-fix image, confirming
  **0 bytes retained SRAM growth**. Generated
  `build/LXRV2_lxr02.img` is `352,324 B` and passed the image packer's
  validation. Hardware verification with `000 Full` remains pending.

---

# Supplementary plan: make browser-index restoration the final Load step

## Scope and conclusion of the deep dive

This supplementary plan addresses the remaining runtime difference between
`Load: Scene` and `Load: Bank`: after a successful Bank payload load, the
currently playing Scene must be applied before any operation is allowed to
reuse or replace filesystem result state. It also establishes the same ordering
rule for root Scene Load.

The proposed command order is:

```text
root Scene/Bank Load
  -> validate and commit payload through the existing shared Scene reader
  -> update and flush /.hcnames
  -> publish Preset completion
  -> consume the completed Bank result, if applicable
  -> apply the loaded active Scene through the shared runtime DSP worker
  -> read the already-existing root .hcindex into the browser cache
  -> end `...`, return the cursor to [type], and unlock Menu input
```

The final index step for a pure Load is a **read-only cache reload**. It must not
scan the directory and must not rewrite `.hcindex`, because Load does not
create, rename, or remove a root Scene or Bank directory.

Save remains:

```text
save/promote payload
  -> update and flush /.hcnames where that object owns resident identity
  -> physically rescan the mutated root directory
  -> rewrite and flush .hcindex
  -> publish completion and unlock Menu input
```

This is not an exception to the last-step rule. A Save changes the indexed
namespace, so its rescan/rewrite is already the final durable operation before
the Menu completion callback. A Load does not change that namespace, so its
last step is only reloading the unchanged index after DSP application.

`/.hcnames` should remain inside the filesystem payload transaction rather than
being moved behind DSP application. It is durable resident-object identity,
not a browser cache. Keeping it in the filesystem transaction means an
HCNAMES failure is reported as part of the Load and the Preset callback is not
published prematurely. The fragility comes from using the subsequent physical
directory scan/index rewrite as an early cache restoration, not from the
targeted HCNAMES update itself.

## Confirmed current ordering defect

There is one generalized cache allocation. Root Kit, root Scene, root Bank,
typed Instrument indexes, and the 129-row HCNAMES view take turns owning it.
Consequently, Scene and Bank Loads must restore their browser domain after
HCNAMES has used the cache.

The current Scene path changes the operation to
`FS_INTERNAL_OP_UPDATE_HCNAMES_SCENE`. At HCNAMES close,
`filesystem_residentNames_tick()` unconditionally schedules a physical Scene
scan and `.hcindex` rewrite. That behavior is shared by Scene Load and Scene
Save even though only Save mutates `/Scene/`.

The current Bank Load path independently schedules the same kind of physical
Bank scan and `.hcindex` rewrite at phase 86. The scan is started through
`filesystem_start()`, whose generic operation initialization clears
`op_bank_loaded_scene`. Preset/Menu later asks that byte whether the successful
Bank contained a loaded Scene. The early index rebuild can therefore erase the
result before Menu decides to run `menu_startSoundApply()`. A later manual
Scene change calls the normal Scene apply worker, which explains why the newly
loaded data becomes audible only after changing away and back.

The Bank payload reader itself does not need replacement. It already iterates
the selected physical Bank children and delegates every child to
`filesystem_loadSceneDirectory_tick()`, the same Scene payload parser used by
root Scene Load. The remaining defect is completion/cache ordering after that
shared reader succeeds.

## Workflow matrix after the change

| Workflow | Payload/index behavior | Required terminal behavior |
| --- | --- | --- |
| Kit Load / KitMrp Load | Keeps `/Kit/.hcindex` resident; Kit/Instrument names are accumulated in the existing Menu session scratch | Apply through the existing worker; no per-load index reload |
| Instrument Load / InstrumentMrp Load | Keeps the selected typed Instrument index resident; HCNAMES is deferred to family exit | Apply through the existing Instrument worker; no per-load index reload |
| Scene Load | Shared Scene reader commits Scene/Kit/Pattern/settings, then HCNAMES borrows the cache | Apply DSP, read `/Scene/.hcindex`, then finish the command |
| Bank Load | Bank loop delegates selected children to the shared Scene reader, commits Bank metadata, then HCNAMES borrows the cache | Consume the Bank result, apply DSP, read `/Bank/.hcindex`, then finish the command |
| Kit/Scene/Bank Save | Root directory may be created, renamed, or removed | Rescan that physical root and rewrite its index as the last filesystem step |
| Instrument Save | Typed Instrument namespace may change | Retain its existing typed scan/index refresh; the type dimension is a valid reason not to use the numbered-root helper |
| Globals/Pattern/ALL/Performance/Samples | Does not use a numbered root browser index | Retain the existing completion/apply path |

The supplied `000 Full` Bank contains child Scenes, so runtime Bank Load follows
the normal Bank row above. An empty Bank cannot be accepted from the current
runtime Bank page because its preview produces a zero mask and the explicit
request is rejected. The boot-only empty-Bank fallback remains a separate
pre-audio concern: no Load/Save Menu command is active there, so this plan does
not start a final browser reload at boot. A later Menu entry loads whichever
index it actually needs. This avoids adding a boot-only asynchronous
continuation or changing the existing fallback policy as part of the targeted
runtime fix.

## Detailed code-change plan

### 1. Separate a namespace rebuild from a read-only cache reload

**Files:**

- `Core/Hardware/SD/filesystem.c`
- `Core/Hardware/SD/filesystem.h`

**Affected code:**

- `filesystem_requestLoadLibraryIndex()` and the three root-index wrappers;
- the declarations beside `fs_library_index_kind_t`;
- `op_library_index_refresh_pending`;
- `op_library_index_refresh_kind`;
- `op_library_index_refresh_callback`;
- `filesystem_completeLibraryIndexRefresh()`;
- `filesystem_startLibraryIndexRefresh()`;
- `filesystem_libraryIndexRefreshScanComplete()`;
- `filesystem_libraryIndexRefreshWriteComplete()`;
- the refresh check in `filesystem_flushFinish_tick()`.

Expose one enum-based read-only request:

```c
bool filesystem_requestReloadLibraryIndex(
    fs_library_index_kind_t kind,
    fs_completion_cb_t cb);
```

It maps `FS_LIBRARY_INDEX_KIT`, `FS_LIBRARY_INDEX_SCENE`, and
`FS_LIBRARY_INDEX_BANK` to the existing common slot-ordered index loader. The
existing Kit/Scene/Bank wrapper functions remain available and delegate to
this function, so boot and existing browser-entry callers do not need to
duplicate the mapping.

Rename the internal `*_library_index_refresh_*` fields and helpers to
`*_library_index_rebuild_*`. This is a mechanical semantic rename: that chain
does a physical parent-directory scan followed by a complete `.hcindex`
rewrite, so “refresh” is too ambiguous and made it appear suitable for a pure
Load. The renamed chain remains common to numbered-root Saves.

**What this change does:** creates an explicit API and vocabulary boundary
between:

- reloading an existing `.hcindex` into the shared SRAM browser cache; and
- rebuilding `.hcindex` from a directory whose contents were mutated.

**Why it must exist:** Menu needs the first operation after DSP apply, while
Save needs the second operation before it publishes completion. Reusing a name
for both permits the destructive/expensive Save path to be inserted into Load
again.

**Inputs:**

- public root-library kind;
- optional completion callback;
- idle single-operation filesystem gate;
- the corresponding existing `.hcindex`.

**Outputs:**

- the shared cache is tagged and populated for exactly that root library;
- no directory scan, directory mutation, or `.hcindex` write;
- the compatibility wrappers retain their behavior;
- the rebuild chain retains the original Save callback until scan, index
  rewrite, and media flush are all complete.

**Code affiliates:**

- `filesystem_prepareLibraryNameCache()`;
- `filesystem_loadLibraryIndex_tick()`;
- `filesystem_libraryNameCacheLoaded()`;
- `filesystem_requestScanKits()`;
- `filesystem_requestScanScenes()`;
- `filesystem_requestScanBanks()`;
- `filesystem_createLibraryIndex_tick()`;
- `filesystem_flushFinish_tick()`.

**Adjacent comment requirement:** both `.c` and `.h` must explicitly say that
reload is read-only cache restoration, while rebuild is reserved for a
namespace mutation. The rebuild state comments must list Save as the owner and
must no longer cite Bank Load.

### 2. Stop Scene Load from inheriting Scene Save's rebuild

**File:** `Core/Hardware/SD/filesystem.c`

**Affected code:**

- phase 6 of `filesystem_residentNames_tick()`;
- phase 37 of `filesystem_saveSceneDirectory_tick()`;
- phase 72 of `filesystem_loadSceneDirectory_tick()`.

Remove the unconditional Scene rebuild scheduling from
`filesystem_residentNames_tick()` when the current operation is
`FS_INTERNAL_OP_UPDATE_HCNAMES_SCENE`.

Before Scene Save changes `current_op` to the shared Scene HCNAMES updater, set
the renamed Scene rebuild kind/pending fields in
`filesystem_saveSceneDirectory_tick()` phase 37. Scene Save therefore carries
its own mutation requirement into the common HCNAMES writer.

Do not set the rebuild fields in root Scene Load phase 72. It should prepare
the HCNAMES cache, run the same targeted Scene-name update, flush it, and then
invoke the original Preset callback with the shared cache still in HCNAMES
mode.

**What this change does:** makes the caller that mutated `/Scene/` responsible
for requesting a rescan/rewrite. The shared HCNAMES writer becomes neutral and
finishes only the metadata operation it was asked to perform.

**Why it must exist:** the same HCNAMES state is used by public targeted name
updates, Scene Load, and Scene Save. HCNAMES operation type alone cannot tell
whether the root Scene directory changed. Only Scene Save has that fact.

**Inputs:**

- Scene Save's successful promoted directory tree and captured display name;
- or Scene Load's validated staged Scene and destination mask;
- the targeted HCNAMES row mask already stored in
  `op_scene_load_scene_mask`.

**Outputs:**

- Scene Save still completes only after `/Scene/.hcindex` has been rebuilt and
  flushed;
- Scene Load completes immediately after HCNAMES is durable, without scanning
  or rewriting `/Scene/.hcindex`;
- `filesystem_requestUpdateResidentSceneNames()` no longer has the hidden side
  effect of rebuilding a root library it did not mutate.

**Code affiliates:**

- `filesystem_prepareResidentNamesCache()`;
- `filesystem_requestUpdateResidentSceneNames()`;
- `filesystem_finish()`;
- `filesystem_flushFinish_tick()`;
- the renamed library-index rebuild chain.

**Adjacent comment requirement:** update the comments at all three phases.
Scene Save's comment must identify it as the mutation owner. Scene Load's
comment must state that browser restoration is intentionally deferred to the
post-DSP Menu terminal stage. The generic HCNAMES close comment must state that
it neither infers nor schedules a root-index rebuild.

### 3. Remove the early physical Bank rebuild from Bank Load

**File:** `Core/Hardware/SD/filesystem.c`

**Affected code:**

- phase 86 of `filesystem_loadBankDirectory_tick()`;
- comments around `op_bank_loaded_scene`;
- the phase that marks a successfully committed Bank child Scene.

At Bank Load phase 86, close and flush HCNAMES with
`filesystem_finish(FS_STATUS_DONE)` but do not set the Bank rebuild
kind/pending fields.

Keep `op_bank_loaded_scene` as the completed Bank operation's result. It is
initialized by the accepted Bank Load and set only when the shared Scene reader
successfully commits at least one selected child. No new filesystem request is
started between that commit and the Preset callback, so Menu can consume the
result before the later post-DSP index reload starts.

The early phase-18 assignment should be removed so opening a child directory
does not claim a successful Scene payload before parsing and commit have
completed. The assignment after the shared Scene commit remains the
authoritative producer.

**What this change does:** allows successful Bank completion to reach
Preset/Menu before any general filesystem request resets operation scratch.

**Why it must exist:** the current early index rebuild starts with
`filesystem_start()`, which clears `op_bank_loaded_scene`. Menu then takes the
valid empty-Bank branch and skips the shared sound apply even though Scene data
was committed.

**Inputs:**

- validated Bank child mask;
- successful shared Scene child commits;
- completed Bank metadata and targeted HCNAMES overlay.

**Outputs:**

- `preset_completedBankLoadedScene()` returns the actual completed Bank result;
- Bank Load performs no root Bank scan and no `.hcindex` write;
- Bank Save continues to own the existing Bank rebuild because Save can change
  `/Bank/`;
- no new Bank payload parser or duplicate Scene loop is introduced.

**Code affiliates:**

- `filesystem_loadSceneDirectory_tick()`;
- `filesystem_start()`;
- `filesystem_lastBankLoadLoadedScene()`;
- `preset_completedBankLoadedScene()`;
- `menu_pollPresetStatus()`'s `PRESET_OP_BANK_LOAD` case.

**Adjacent comment requirement:** the Bank phase-86 comment must distinguish
the durable HCNAMES close from deferred browser restoration. The surviving
result assignment must say that parsing/commit, not directory-open success,
makes the result true.

### 4. Add one shared Menu terminal helper for root Scene/Bank Loads

**File:** `Core/Menu/menu.c`

**Affected code:**

- static forward declarations;
- `menu_startSoundApply()`'s synchronous completion path;
- `menu_finishSoundApply()`'s foreground/chunked completion path;
- one new request helper;
- one new filesystem completion callback.

Add a helper with the following responsibility:

```text
if no accepted Load/Save command is active:
    do not start browser work
else if the locked Load type is Scene or Bank:
    keep menu_storageBusy and menu_loadSaveCommandActive set
    request the matching read-only root index reload
    defer command reset/repaint to that request's callback
else:
    use the existing immediate terminal cleanup
```

The helper derives the index kind from `menu_saveOptions.what`. That value
cannot change while `menu_storageBusy` locks input, so no new retained
operation-kind byte is required. Both the pre-audio synchronous sound-apply
branch and `menu_finishSoundApply()` must call the same helper after all
requested Scene/Bank pattern, settings, and DSP work has completed.

Use `filesystem_requestReloadLibraryIndex()` directly. Do not use
`menu_libraryIndexLoadComplete()` as the callback: that callback is a
browser-entry continuation and, for `Load: Bank`, deliberately launches the
selected Bank child preview. Reusing it here would begin another preview
instead of terminating the accepted command.

Add a dedicated terminal callback. On a successful index reload it calls
`menu_finishLoadSaveCommand()` and repaints. On failure it clears the invalid
cache, calls `menu_finishLoadSaveCommand()` so `...` cannot remain stuck, and
then uses `menu_showFilesystemErrorOverlay()` while the filesystem error code
is still available. The payload and DSP state remain applied; an index-cache
failure is not a valid reason to roll audio state back.

If the reload request is unexpectedly rejected, treat that as a terminal
filesystem error rather than unlocking as though restoration succeeded. At
this point Preset has already been acknowledged and sound apply has drained,
so the filesystem is expected to be idle.

**What this change does:** moves Menu command completion from “DSP finished” to
“DSP finished and the browser cache needed by the still-selected type is
resident.”

**Why it must exist:** `menu_finishLoadSaveCommand()` ends `...`, clears the
input gate, resets the cursor to the type row, and permits new cache users.
Calling it before index restoration exposes HCNAMES as though it were a Scene
or Bank browser and allows a new request to race the restoration.

**Inputs:**

- `menu_loadSaveCommandActive`;
- locked `menu_saveOptions.what`;
- completed synchronous or chunked Scene/Bank sound apply;
- filesystem completion status from the read-only index request.

**Outputs:**

- `...` remains visible and no `[]` or `>` cursor is displayed during DSP apply
  and final index I/O;
- Menu input remains locked throughout;
- success always returns to the `[type]` selection and restores `ok`/`OW`;
- failure also terminates the command before showing the existing error
  overlay;
- boot Scene/Bank application does not request a browser index, because no
  explicit Menu command is active.

**Code affiliates:**

- `menu_beginLoadSaveCommand()`;
- `menu_finishLoadSaveCommand()`;
- `menu_paintLoadSaveConfirmation()`;
- `menu_showFilesystemErrorOverlay()`;
- `menu_startSoundApply()`;
- `menu_finishSoundApply()`;
- `menu_libraryIndexLoadComplete()`;
- `filesystem_requestReloadLibraryIndex()`.

**Adjacent comment requirement:** document the ordering and ownership beside
the helper, both sound-apply call sites, and the dedicated callback. The
comments must explicitly state that the entry callback is not reused because
it has Bank-preview side effects.

### 5. Preserve Save behavior while making its ownership explicit

**Files:**

- `Core/Hardware/SD/filesystem.c`
- `Core/Hardware/SD/filesystem.h`
- `Core/Menu/menu.c`

Keep the existing Kit Save and Bank Save rebuild scheduling, and move only
Scene Save's scheduling to its mutation-owning caller as described above.
Instrument Save retains its typed index refresh because typed Instrument
directories are not slot-ordered root libraries.

`menu_refreshSavedLibraryName()` continues to read the saved row from the
freshly rebuilt cache. Save completion has no post-save DSP apply, so the
filesystem callback remains the correct terminal boundary. Update its adjacent
comment to use “rebuilt” rather than ambiguous “refreshed” terminology.

**What this change does:** keeps all numbered-root Saves on one common physical
scan/index-rebuild implementation while keeping pure Loads off that path.

**Why it must exist:** harmonization means sharing behavior that has the same
contract. Load cache restoration and Save namespace rebuild have different
contracts and should share only the lower-level slot-ordered index reader/
writer primitives, not the mutation step.

**Inputs:**

- a successfully promoted Kit, Scene, or Bank directory;
- its root library kind;
- the original Preset completion callback.

**Outputs:**

- exactly one physical scan and one complete index rewrite per successful
  numbered-root Save;
- no scan/rewrite per root Scene or Bank Load;
- Save UI still sees the newly rebuilt row before it returns to the type
  selector.

**Code affiliates:**

- Kit Save phase 21;
- Scene Save phase 37;
- Bank Save phase 86;
- the renamed library-index rebuild chain;
- `menu_refreshSavedLibraryName()`;
- Scene/Bank Save completion cases in `menu_pollPresetStatus()`.

**Adjacent comment requirement:** `.c` comments at each Save scheduling site
must name the directory mutation that requires the rebuild. The `.h` root-index
API comment must prevent callers from confusing read-only reload with Save
rebuild.

## Explicit non-changes

Do not change:

- Bank child selection or iteration;
- `filesystem_loadSceneDirectory_tick()` payload parsing;
- Kit, Instrument, Scene settings, Pattern, Effect, or LFO parsing;
- the runtime drumset/LFO target apply implementation;
- SceneData, BankData, Kit, Instrument, or Pattern storage layouts;
- on-card payload formats;
- `main.c` boot load ordering for the supplied nonempty Bank;
- the Bank preview fix documented in the first part of this file;
- the `ok`/`OW` renderer;
- `SRAM_MANIFEST.md`.

This plan introduces no second name cache, no Scene-name mirror, and no
payload staging allocation. The generic index API and semantic renames add no
retained state. The Menu helper derives its kind from existing locked state.
Expected retained SRAM growth is therefore **0 bytes**, well below the
previously approved allowance.

## Required verification

1. Run `git diff --check`, build the normal firmware, and compare linked
   `.data`/`.bss` against the pre-change image. Confirm 0-byte retained SRAM
   growth.
2. Add temporary trace points or use the existing filesystem diagnostic to
   verify runtime `Load: Bank` ordering:
   `LOAD_BANK -> HCNAMES -> Preset callback -> drumset apply -> LOAD_LIBRARY_INDEX`.
   Confirm `SCAN_BANKS` and `CREATE_LIBRARY_INDEX` do not occur.
3. With playback running, alter the active Scene and several other resident
   Scenes, then load `SD_CARD/Bank/000 Full/` with all 16 mask bits selected.
   Confirm the currently playing Scene changes as soon as the shared sound
   apply completes, without changing Scenes away and back.
4. Confirm all 16 resident Scenes contain their selected Bank child payloads
   after switching through them.
5. During the Bank operation, confirm `...` remains visible and no cursor is
   displayed through payload I/O, HCNAMES, DSP application, and final
   `/Bank/.hcindex` reload. Confirm completion always returns to `[Bank]` with
   `ok`.
6. Repeat for explicit root Scene Load. Confirm it applies immediately, then
   performs only a read of `/Scene/.hcindex`, and returns to `[Scene]`.
7. Force or simulate a final index-read failure. Confirm the loaded sound
   remains applied, the Menu does not remain on `...`, the cursor is reset to
   the type row, and the existing filesystem error overlay appears.
8. Save a Kit, Scene, and Bank into empty slots and over existing named slots.
   Confirm each Save still performs a physical root scan and complete index
   rewrite as its final filesystem work and that the saved name is immediately
   visible.
9. Exercise Kit/KitMrp and Instrument/InstrumentMrp Load while playing.
   Confirm they retain their current index/session behavior and do not acquire
   an unnecessary post-load index operation.
10. Power-cycle with the supplied `000 Full` Bank selected. Confirm its active
    Scene and modulation apply still match a manual Scene switch and no new
    pre-audio index continuation is started.

## Supplementary investigation note

- 2026-07-27: Traced all root-index and HCNAMES users. Kit and Instrument Load
  already keep their browser indexes resident and defer one HCNAMES update to
  the combined name-session exit. Scene Load inherits a Scene rebuild only
  because the generic Scene HCNAMES close schedules it; Bank Load schedules a
  Bank rebuild directly at its HCNAMES close. Kit, Scene, and Bank Saves
  legitimately use the common physical scan/index rewrite because their root
  namespaces may have changed.
- 2026-07-27: Confirmed that Bank and root Scene payloads already share
  `filesystem_loadSceneDirectory_tick()`. No payload-loader duplication is
  required. The Bank-specific work is limited to child iteration, Bank
  metadata, HCNAMES overlay, and the completed “loaded a Scene” result.
- 2026-07-27: Confirmed that Menu currently calls
  `menu_finishLoadSaveCommand()` at sound-apply completion. That is the correct
  place to insert one shared read-only Scene/Bank index restoration helper,
  before the command flag and input gate are cleared. The normal browser-entry
  callback cannot be reused because its successful Bank branch intentionally
  starts a child preview.
- 2026-07-27: This turn changed only `BANK_LOAD_FIX.md`. No `.c`, `.h`,
  generated image, on-card file, or SRAM manifest was modified.
- 2026-07-27: Implemented the supplementary ordering plan. The filesystem now
  exposes `filesystem_requestReloadLibraryIndex()` as the common read-only
  Kit/Scene/Bank cache loader; the existing domain-specific wrappers delegate
  to it. The former `*_library_index_refresh_*` chain is now named
  `*_library_index_rebuild_*`, and its only scheduling sites are Kit Save,
  Scene Save, and Bank Save.
- 2026-07-27: Removed the implicit Scene rebuild from the generic Scene
  HCNAMES close. Scene Save now sets the rebuild request before entering the
  shared HCNAMES writer, while Scene Load flushes HCNAMES and publishes Preset
  completion without scanning or rewriting `/Scene/.hcindex`.
- 2026-07-27: The post-implementation error-path audit found that Scene Save's
  earlier rebuild declaration could survive a failed HCNAMES transaction.
  `filesystem_finish()` now cancels a still-pending rebuild on pre-rebuild
  failure; once the rebuild has actually started, pending is already clear and
  its parked callback retains the existing error-completion path. This prevents
  a later unrelated successful operation from inheriting a stale Scene scan.
- 2026-07-27: Removed Bank Load's phase-86 Bank scan/index rewrite and removed
  the premature `op_bank_loaded_scene = 1` assignment made when a child
  directory merely opened. The surviving assignment is after the shared Scene
  parser has validated and committed a child, so Preset/Menu consumes a real
  completed result before any later filesystem request can reset scratch.
- 2026-07-27: Added the shared Menu post-sound-apply terminal helper and a
  dedicated final-index callback. Explicit Scene/Bank Load keeps
  `menu_storageBusy` and `menu_loadSaveCommandActive` asserted through a
  read-only root-index reload; only that callback ends `...`, returns to the
  bracketed type row, and unlocks input. The browser-entry callback was not
  reused because its Bank branch intentionally starts a child preview. Boot
  has no active OK command and therefore starts no post-apply browser I/O.
- 2026-07-27: Added adjacent contract comments in `filesystem.c`,
  `filesystem.h`, and `menu.c` covering purpose, ordering, inputs, outputs,
  failure behavior, and affiliates. No Menu public declaration changed, so
  `menu.h` required no new API.
- 2026-07-27: Build verification completed. `git diff --check` passes;
  `make -j2` and `make img` succeed. Linked size is `text=352,412 B`,
  `data=400 B`, `bss=69,948 B`; BSS is unchanged, confirming **0 bytes
  retained SRAM growth**. The firmware payload is `352,812 B`; packaged
  `build/LXRV2_lxr02.img` is `352,828 B` including its 16-byte header and
  passed the image packer's validation. Hardware playback verification remains
  pending.
