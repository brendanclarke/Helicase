# Bank Load completion and Load/Save command-state plan

## Scope and constraints

This plan addresses the observed Bank Load failure for `SD_CARD/Bank/000 Full/` and establishes the shared load/save boundaries needed to keep Kit, Scene, and Bank operations consistent.

It does **not** use Autosave as a dependency and does not alter Autosave files.

The filesystem work reuses the existing operation scratch, Scene staging workspace, child mask, child cursor, and name buffers except for one byte, `op_scene_payload_phase`; the Menu work requires one byte, `menu_loadSaveCommandActive`, in normal SRAM1 `.bss`. The planned retained-state maximum is therefore **+2 B SRAM1**, within the already approved 32-byte allowance. Neither field is a cache or a copy of payload data. The implementation must add each field's exact size, lifetime, owner, and role to [the SRAM manifest](knowledge_files/specification_reference/SRAM_MANIFEST.md), then compare the linked image against the current baseline.

The loader must continue to accept any valid six-instrument arrangement in every Scene slot. No step may assume Drum, Snare, Hat, Cymbal, or any fixed type ordering. Scene parsing remains responsible for validating the `kitset.kcg` type/file mapping, resetting each staged slot through `instrumentManager_resetSlot()`, and parsing every instrument with its declared type. The same rule applies to all modulation/LFO fields: they are Scene/Instrument payload data and must be parsed and committed by the shared payload path before Menu/Preset performs the existing runtime apply/rebind.

## Investigation result

`preset_loadBank()` posts one `filesystem_requestLoadBank()` request, and the Menu correctly waits for the Preset completion callback before it starts the active-Scene runtime apply. The Bank request is therefore not meant to be a UI-only operation.

The problem is in the extra Bank-specific preflight and continuation path:

1. `filesystem_requestLoadBank()` begins with `filesystem_startRepairBankNames()`.
2. `filesystem_repairNames_tick()` reaches repair phase 43 and calls `filesystem_quarantineBankKitsBlocking()`.
3. That helper recursively walks **every parseable child in the Bank, regardless of the selected load mask**, and every embedded Kit using `while` loops that call only `afatfs_poll()`. It does not return to the normal foreground loop between operations. A full 16-Scene Bank can therefore appear frozen on its confirmation text before any Scene payload is committed. It also makes cancellation/input/display progress impossible during the longest part of the request.
4. After this preflight, `filesystem_loadBankDirectory_tick()` delegates a child to `filesystem_loadSceneDirectory_tick()` by setting the shared `op_phase` to Scene phase 8. The Scene worker later recognizes Bank ownership through `current_op` and `op_bank_payload_active`, then writes Bank phase 20 back into the same numeric phase field. This works only because both state machines know each other's private phase numbers and unwind rules.
5. At the end, Bank Load writes the resident-name register and parks the original completion callback through the save-index refresh chain solely to restore `/Bank/.hcindex`. This reuses a save-oriented mechanism and obscures the logical completion boundary.

Root Scene Load does not have the blocking all-child quarantine pass or the Bank-to-Scene-to-Bank phase handoff. That explains why it can complete while Bank Load remains visibly stuck and none of the Bank's Kits have reached resident Scene storage.

### Exact current execution trace

The following is the current control flow that implementation must replace without changing the on-card format:

```text
Menu: Load:[Bank] + OK
  -> preset_loadBank(slot, menu_kitLoadSceneMask)
  -> filesystem_requestLoadBank(slot, mask, on_bank_load_complete)
  -> FS_INTERNAL_OP_REPAIR_NAMES, FS_REPAIR_SCOPE_BANK_LOAD
       repair phase 43: filesystem_quarantineBankKitsBlocking(slot)   [BLOCKING]
       repair phase 44: current_op = FS_INTERNAL_OP_LOAD_BANK
  -> Bank phase 0..17: borrow HCNAMES, parse bankset, scan children, intersect mask
  -> Bank phase 31: initialize Scene stage and open one `SS Name`
  -> Bank phase 18: set op_bank_payload_active; write Scene phase 8 into op_phase
  -> Scene phases 8..72: parse sceneset + embedded Kit + six Instrument files + pattern/fx
       Scene phase 61: commit one child and overlay its HCNAMES block
       Scene phase 72: clear Bank flag; write Bank phase 20 into op_phase
  -> Bank phase 20: choose next child or commit BankData
  -> Bank phases 83..86: rewrite HCNAMES, park callback in save-index refresh chain
  -> scan /Bank + rewrite /Bank/.hcindex + callback
  -> Preset UPDATE_READY -> Menu runtime apply of active Scene -> Menu cursor reset
```

The blocking preflight is redundant for a valid Bank: the common Scene path already parses `sceneset.scg`, `kitset.kcg`, all six declared Instrument files, `pattern.pat`, and `effects.fx` before it commits that child. Its only extra policy is the eight-character Kit-member basename check used to avoid retaining an un-reconstructable FAT key. That check must move into the shared Scene payload validation step for Bank-origin payloads; it must not scan the complete Bank before the selected child loop begins.

The Scene parsing itself already uses the declared type in `kitset.kcg`, calls `instrumentManager_resetSlot()` with that type, and uses `storage_instrumentParseLine()` / `storage_instrumentFinalize()` for each of six slots. Bank-specific code must not add a type mapping or a special LFO parser. The refactor's requirement is to preserve this exact shared path, so all LFO source/target/depth/rate and target-assignment fields continue to arrive through the same parser and staged `scene_t` commit as root Scene Load.

## Target operation hierarchy

The code should express the persisted-object hierarchy directly:

```text
Instrument payload
  -> Kit payload       (kitset + six declared Instrument payloads)
     -> Scene payload  (sceneset + Kit payload + pattern + effects)
        -> Bank payload (bankset + selected 00..15 Scene payloads)
```

The lower layer is a reusable *payload worker*, not a public operation that opens unrelated root folders. A Bank child Scene is already positioned inside the Bank directory and must pass that context to the same Scene payload worker used by root Scene Load. The Bank layer owns only Bank metadata, selected-child enumeration, resident-name overlay, and the final root-Bank browser restoration.

## Implementation plan

### 1. Delete the runtime recursive Bank preflight; validate only selected payloads

**Change.** In `filesystem_repairNames_tick()`, replace Bank-repair phase 43/44 with one direct successful handoff from `FS_REPAIR_SCOPE_BANK_LOAD` to `FS_INTERNAL_OP_LOAD_BANK` at Bank phase 0. Do not call `filesystem_quarantineBankKitsBlocking()` at runtime. Delete the Bank-only blocking recursive helpers `filesystem_quarantineBankKitsBlocking()`, `filesystem_quarantineScenesInParentBlocking()`, and `filesystem_quarantineEmbeddedKitBlocking()` once their only caller is removed. Keep the generic root-Kit boot quarantine and its `filesystem_block*()` support because boot intentionally runs before audio and still uses them.

Move the existing `filesystem_kitMemberNameIsCanonical()` check to the common Scene payload worker immediately after successful `storage_kitsetFinalize()` for a Bank-origin child. If any selected child's member basename cannot be represented by the eight-character resident identity contract, set the ordinary filesystem error, unwind to root, and complete the one Bank request with error before `filesystem_commitSceneStage()`. Do not rename or quarantine arbitrary user files during a runtime Load command.

**Why it must exist.** The current helper contains nested blocking `while` loops and scans all Bank children even when the caller selected only one. That is the freeze. The ordinary Scene payload worker already fully validates every selected child before commit, so pre-validating the complete tree duplicates I/O and makes a user-initiated load mutate unrelated card contents. The moved basename check preserves the one Bank-specific retained-identity constraint without reintroducing a broad preflight.

**Inputs.** Captured Bank slot/name, selected child mask, one current child slot, parsed `kitset.kcg` member names, and existing `afatfs` handles/callback completion flags.

**Outputs.** The repair stage yields at once after its existing asynchronous immediate-child name canonicalization. Every selected child is then validated and either atomically committed by the shared Scene worker or reports one deterministic Bank Load error. Unselected children are neither opened for payload validation nor renamed/quarantined. No browser cache or per-child name table is retained.

**Code affiliates.** `Core/Hardware/SD/filesystem.c`: `filesystem_requestLoadBank()`, `filesystem_repairNames_tick()`, `filesystem_kitMemberNameIsCanonical()`, `filesystem_loadSceneDirectory_tick()` (to become the common worker), `filesystem_quarantineBankKitsBlocking()`, `filesystem_quarantineScenesInParentBlocking()`, `filesystem_quarantineEmbeddedKitBlocking()`, `filesystem_quarantineKitLibraryBlocking()`, and the `filesystem_block*()` helpers. `Core/Hardware/SD/filesystem.h`: revise the Bank-request contract comment to say that selected children are validated by the foreground-pumped payload reader rather than a complete-tree preflight.

**Adjacent source documentation.** The repair handoff comment must state that repair canonicalizes only Bank child directory components and does not validate/mutate all embedded Kits. The new in-worker canonical-name check needs an adjacent input/output comment explaining that it is Bank-origin-only, occurs before commit, and preserves flexible declared Instrument types. The retained boot-only blocking helpers must say explicitly that no runtime Bank Load calls them.

### 2. Make Scene payload loading a context-driven shared worker

**Change.** Split `filesystem_loadSceneDirectory_tick()` into:

- a root-Scene adapter that captures the root slot/name, opens `Scene/NNN Name`, initializes the stage, and owns the root HCNAMES update/final public completion;
- a Bank adapter that parses Bank container metadata and opens `Bank/NNN Name/SS Name`; and
- `filesystem_scenePayload_tick()`, a common worker that starts with an already-open Scene directory handle and owns all phases currently numbered 8..72.

Add one `uint8_t op_scene_payload_phase` in the existing filesystem operation record. Reuse the existing `op_bank_payload_active` byte by renaming it to `op_scene_payload_active`; it becomes true for both root and Bank payloads. Keep `op_phase` in the outer adapter's phase namespace throughout. The worker's final phase always returns to filesystem root, clears `op_scene_payload_active`, and leaves `op_close_status` as `FS_STATUS_DONE` or `FS_STATUS_ERROR`; it never writes a root/Bank phase number into `op_phase` and never invokes the public callback itself.

The adapter starts the worker through a named `filesystem_beginScenePayload(...)` helper after it has initialized `fs_stage_workspace.scene_stage`, installed the target mask, captured the Scene display identity, and placed the directory handle in existing `op_kit_slot_dir` scratch. At worker completion, the adapter consumes `op_close_status`: root Scene starts its targeted HCNAMES update or finishes; Bank advances its child iterator or finishes with error.

**Why it must exist.** Root Scene and Bank-local Scene currently share most parsing code, but the Bank path enters it by assigning Scene phase 8 and leaves it by recognizing Bank flags at Scene phase 72. That is implicit coupling between two unrelated phase-number namespaces. A context-driven worker makes the lower-level Scene data loader the single authority and lets Bank repeat it without duplicating payload behavior or guessing when the worker has unwound.

**Inputs.**

- Root adapter: root Scene slot/name and selected destination mask.
- Bank adapter: selected Bank child slot/name, one-bit destination mask, and Bank parent context.
- Shared worker: existing `fs_stage_workspace.scene_stage`, `op_sceneset_state`, `op_kitset`, Instrument parser state, existing open-name scratch, and explicit completion continuation.

**Outputs.** On success, selected resident `scene_t` payloads have the exact parsed Scene settings, Kit, all six flexible Instrument slots, pattern, effects, LFO assignments, and modulation target data. The caller receives a success/error result plus the identity block needed for HCNAMES. On failure, no partial staged Kit/Scene payload is committed beyond the existing documented direct-pattern boundary.

**Code affiliates.** `Core/Hardware/SD/filesystem.c`: `filesystem_loadSceneDirectory_tick()`, `filesystem_loadBankDirectory_tick()`, `filesystem_initSceneStage()`, `filesystem_commitSceneStage()`, `filesystem_resetSceneLoadChildDiscovery()`, `filesystem_cacheCurrentBankSceneNameBlock()`, `storage_scenesetParseLine()`, `storage_kitsetParseLine()`, `storage_instrumentParseLine()`, `storage_instrumentFinalize()`, `instrumentManager_resetSlot()`. `Core/Storage/*` parser headers only need changes if an explicit payload-context type is publicly declared.

**State/SRAM rule.** This is a mandatory `+1 B` normal-SRAM1 field: `op_scene_payload_phase`. `op_scene_payload_active` is a rename/reuse of the existing Bank-only byte and costs `0 B`. Do not introduce a child-name array, a second Scene stage, a per-Bank Instrument cache, or a payload-context struct containing duplicated names. Add the one-byte field to the SRAM manifest with owner `filesystem operation record`, lifetime `one active root Scene or Bank child payload`, and purpose `private shared payload state machine phase`.

**Adjacent source documentation.** Put a comment block beside the common context/continuation declaration that lists root Scene and Bank-child inputs and states that the worker parses declared instrument types and LFO/modulation fields identically in both cases. Put matching adapter comments beside root Scene and Bank entry points.

### 3. Reduce Bank loading to container metadata plus repeated Scene worker calls

**Change.** Keep only these Bank responsibilities in the Bank adapter:

- validate/parse `bankset.bcg`;
- discover the physical `00..15` child presence mask;
- intersect it with the request mask without widening the request;
- choose the active loaded Scene deterministically;
- iterate selected children, invoking the common Scene worker once per child;
- overlay only loaded identity rows into the existing resident-name register;
- commit `BankData` once after the selected children succeed; and
- restore the root Bank browser/index before the public completion callback.

Delete duplicated child-payload control flow and replace magic transitions such as Bank phase 18 to Scene phase 8 and Scene phase 72 to Bank phase 20 with `filesystem_beginScenePayload()` and the adapter's explicit `op_scene_payload_active` / `op_close_status` completion check. Bank phase 31 remains the only place that formats and opens a selected `SS Name`; the next Bank adapter pass sees the worker finished and proceeds to phase 20's existing next-selected-bit scan.

**Why it must exist.** A Bank is a sequence of Scene payloads plus Bank metadata. The current code has that intent but encodes it through private phase jumps and a save-index side channel. Naming the continuation makes the all-16-child sequence auditable, protects mask-selective load semantics, and prevents a successful child payload from leaving the original callback parked in an unexpected state.

**Inputs.** Bank request slot/name, caller-selected child mask from `menu_bankLoadPreviewMask`, parsed `bankset.bcg`, and the common Scene worker result for each selected child.

**Outputs.** All and only selected existing child Scenes are restored to their corresponding resident Scene slots; active Scene points at a loaded child when one exists; empty-Bank behavior remains a successful Bank identity load followed by the existing fallback ladder; `preset_completedBankLoadedScene()` remains correct.

**Code affiliates.** `Core/Hardware/SD/filesystem.c`: `filesystem_loadBankDirectory_tick()`, `filesystem_requestLoadBank()`, `filesystem_lastBankLoadLoadedScene()`, resident-name writer/cache routines, the renamed `filesystem_startLibraryIndexRefresh()`, and `filesystem_flushFinish_tick()`. `Core/Bank/Scene/BankData.*`: `bank_setDisplayName()`, `bank_setScenePresentMask()`, `bank_selectActiveSceneForEditMask()`, `bank_setRestoreBankSlot()`, `bank_setHasResidentBank()`. `Core/Bank/Scene/Preset/presetManager.c`: Bank completion callback and fallback trigger.

**Completion rule and exact rename.** Retain the physical `/Bank/.hcindex` restoration because Bank Load borrows the one shared cache for HCNAMES. Rename the save-oriented chain so it can truthfully serve both saves and Bank Load:

- `op_save_index_refresh_kind` -> `op_library_index_refresh_kind`;
- `op_save_index_refresh_pending` -> `op_library_index_refresh_pending`;
- `op_save_completion_callback` -> `op_library_index_refresh_callback`;
- `filesystem_completeSaveIndexRefresh()` -> `filesystem_completeLibraryIndexRefresh()`;
- `filesystem_startSaveIndexRefresh()` -> `filesystem_startLibraryIndexRefresh()`;
- `filesystem_saveIndexScanComplete()` -> `filesystem_libraryIndexRefreshScanComplete()`; and
- `filesystem_saveIndexWriteComplete()` -> `filesystem_libraryIndexRefreshWriteComplete()`.

Update every existing Kit/Scene/Bank Save and HCNAMES caller to the generic names; this is a semantic rename, not a second chain or an SRAM change. Bank phase 86 sets the generic pending/kind fields and calls `filesystem_finish(FS_STATUS_DONE)`. `filesystem_flushFinish_tick()` starts the generic root-Bank scan after the HCNAMES flush, then the existing `.hcindex` writer, and finally invokes the parked original Bank callback exactly once. Any scan/index error calls the same generic completion helper with error and never leaves the Preset callback parked.

**Adjacent source documentation.** Comment the one Bank commit point and the generic cache-restore helper with input/output/lifetime text. Explicitly state that no unselected Scene payload or identity row is modified.

### 4. Normalize Preset completion into request, payload, runtime-apply, final-UI phases

**Change.** Make the Preset/Menu boundary express four separate milestones:

1. request accepted;
2. filesystem payload fully completed (including Bank cache restore);
3. active runtime sound/pattern/global apply completed; and
4. the originating explicit Load/Save command has finalized its UI state.

Keep `preset_loadBank()` as a single public request and preserve its current selected-mask input. Its completion callback should only mark the filesystem milestone. Menu starts the active Scene apply once, after the selected Bank payload is complete; it must not start one apply per Bank child. The empty-Bank fallback remains an explicit subsequent Preset request after acknowledging the Bank completion.

**Why it must exist.** The selected resident Scenes should all be restored from SD, while only the active Scene needs immediate DSP application. Treating those as one hidden transition makes it easy for `menu_storageBusy`, cursor reset, or the callback acknowledge to occur too early.

**Inputs.** Completed Preset operation kind, successful/error status, Bank loaded-Scene result, active page, and existing apply worker completion.

**Outputs.** One acknowledged Preset completion; exactly one active runtime apply for a non-empty successful Bank; no active parameter changes before the filesystem Bank payload commits; consistent error and empty-Bank fallback handling.

**Required Preset API consistency change.** Change `preset_loadGlobals()` and `preset_saveGlobals()` from `void` to `uint8_t` in both `presetManager.c` and `presetManager.h`. They must return `1` only after `filesystem_requestLoad()`/`filesystem_requestSave()` accepts the request, and return `0` after restoring `PRESET_IDLE` on rejection. This gives the explicit-command Menu entry point the same accepted/rejected contract already used by Kit, Scene, Bank, Instrument, and test requests; it prevents showing `...` forever for a Globals request that never started.

**Code affiliates.** `Core/Bank/Scene/Preset/presetManager.c/.h`: `preset_loadBank()`, `on_bank_load_complete()`, `preset_completedBankLoadedScene()`, `preset_ackStatus()`, `preset_loadFirstAvailableSceneOrKit()`, `preset_loadGlobals()`, and `preset_saveGlobals()`. `Core/Menu/menu.c`: `menu_handleLoadSaveMenu()`, `menu_pollPresetStatus()`, `menu_startSoundApply()`, `menu_finishSoundApply()`, `menu_startGlobalApply()`, `menu_finishGlobalApply()`.

**Adjacent source documentation.** Add matching comments in both `presetManager.c` and `presetManager.h` for any changed public milestone/result API. The comments must distinguish SD completion from DSP apply completion and state that Bank preserves flexible Instrument/LFO payloads until the existing apply/rebind code consumes the active Scene.

### 5. Introduce one explicit Load/Save command lifecycle for `ok` / `OW`

**Change.** Add `static uint8_t menu_loadSaveCommandActive = 0u;` beside the existing Menu storage/apply state and centralize explicit confirmation commands behind three static helpers:

- `menu_beginLoadSaveCommand()` — called only after an `ok`/`OW` command has been accepted; sets the new flag, sets the existing transport gate, and repaints before the next foreground transfer;
- `menu_loadSaveCommandIsActive()` — queried by the renderer; and
- `menu_finishLoadSaveCommand()` — called exactly once after success, failure, or an intentional empty-Bank fallback reaches its terminal command result; clears the flag, releases the transport gate, calls `menu_resetSaveParameters()`, and performs the one final repaint.

The helpers must cover Save Kit, KitMrp, Scene, Bank, Globals and diagnostic commands that show `OK`/`OW`; Load Scene, Bank, Globals, Samples/modal commands, and any other confirmation-row operation. Live Kit/KitMrp scrolling and background browser/Bank-preview scans are deliberately not explicit confirmation commands and must not enter this lifecycle.

**Entry-point rewrite.** Replace the direct `menu_storageBusy = 1u` assignments in the explicit-click part of `menu_handleLoadSaveMenu()` with one dispatch helper that first posts the relevant Preset request and calls `menu_beginLoadSaveCommand()` only when it returns accepted. It covers File/Dir/sDir test load/save, Kit/KitMrp/Scene/Bank save, Scene/Bank load, Globals load/save (using the new return values), and Samples. `menu_instrumentSaveRequestSelection()` likewise calls the begin helper after an accepted normal/Morph Instrument Save. Live Kit/KitMrp scroll loads, nested Instrument pool loads, Bank child preview scans, index loads, name reads, and deferred selection retries remain non-command activity and never set the new flag.

**Why it must exist.** `menu_storageBusy` currently represents several different things: an explicit user command, a background index/name scan, a deferred selection, a runtime apply, and warning overlays. It cannot by itself decide whether an `ok` label should turn into `...` or whether a cursor should disappear. A single lifecycle prevents every completion branch from independently resetting labels/cursor state.

**Inputs.** Origin page (Load or Save), requested object type, confirmation-row activation, request-accepted result, Preset/filesystem completion, post-load apply completion, modal completion, and error completion.

**Outputs.** While active, the displayed confirmation affordance is exactly `...`; no `[]`, `>` arrow, or LCD hardware cursor is shown anywhere on the Load/Save page; input is consumed as an in-progress operation. At the terminal completion, the label is recomputed as `ok` or `OW`, the type-row selection is restored with `[]`, and normal navigation resumes.

**Code affiliates.** `Core/Menu/menu.c/.h`: `menu_handleLoadSaveMenu()`, `menu_repaintLoadSavePage()` (including nested Instrument Save rendering), `menu_pollPresetStatus()`, `menu_startSoundApply()`, `menu_finishSoundApply()`, `menu_startGlobalApply()`, `menu_finishGlobalApply()`, `menu_resetSaveParameters()`, error overlay completion, stale-warning/modal completion, and the test-operation branches. `Core/Bank/Scene/Preset/presetManager.c/.h` only where a request currently has no accepted/failed return value required to enter this lifecycle safely.

**Display details and exact renderer edits.** The existing bottom-right confirmation occupies columns 14–15 and the arrow normally uses column 13. Add one `menu_paintLoadSaveConfirmation()` helper that writes `...` to columns 13–15 while the command flag is set, otherwise writes `ok`/`OW` and any selection arrow. Route every confirmation renderer through it: nested Instrument Save, File/Dir/sDir, top-level Save, and explicit top-level Load. Add a `menu_loadSaveCursorVisible()` predicate and guard every marker emission in `menu_repaintLoadSavePage()` with it: nested Instrument Save type/name fields, nested Instrument Load type/source fields, the top-level type field, preset-number field, and top-level Save filename fields. Leave `cur_want_on` at zero whenever the predicate is false. Do not erase arbitrary `[` or `]` characters from edited names after rendering; suppress only Menu-generated markers.

**Input details.** Add the same flag guard to `menu_handleLoadSaveMenu()`, `menu_handleLoadSaveKnobDelta()`, `menu_loadSaveBarButtonPressed()`, and `menu_loadSceneButtonPressed()` so a queued encoder/button edge cannot mutate selection between accepted request posting and the next `menu_storageBusy` observation. `menu_switchPage()` is already gated by `menu_storageBusy` and needs only a comment tying that gate to explicit-command ownership.

**Adjacent source documentation.** Put the lifecycle invariant directly beside the state/helper declaration in `menu.c`: `menu_storageBusy` remains a transport/apply gate; `menu_loadSaveCommandActive` is presentation/terminal-reset ownership. The helpers are private, so `menu.h` needs no new declaration. Every command-entry and terminal helper call must have a local comment explaining why that branch is beginning or completing the one user-visible transaction. Add the exact +1 B entry to the SRAM manifest.

### 6. Eliminate scattered direct cursor resets from explicit-command branches

**Change.** Route command completion through the lifecycle helper instead of allowing individual branches to independently call `menu_resetSaveParameters()`, clear `menu_storageBusy`, or repaint. Preserve direct resets for page transitions that are not commands. In particular, coordinate:

- successful Scene/Bank load after `menu_finishSoundApply()`;
- successful globals/all load after `menu_finishGlobalApply()`;
- empty Bank fallback only after its nested fallback request is terminal;
- save completion after filesystem/index durability;
- failed filesystem requests and failed post-apply paths; and
- modal/test/stale-warning completions.

**Exact terminal-owner map.**

| Current completion point | Required final owner | Required change |
| --- | --- | --- |
| `PRESET_OP_SCENE_LOAD` / non-empty `PRESET_OP_BANK_LOAD` | `menu_finishSoundApply()` | Start the sound apply without a direct `resetSave` cursor reset. After pattern/runtime work, call `menu_finishLoadSaveCommand()` when the command flag is set. Boot-origin Scene load has no command flag and keeps its current non-Menu behavior. |
| `PRESET_OP_BANK_LOAD` with no loaded child | the fallback request's terminal branch | Acknowledge the completed empty Bank before posting the fallback as today, but retain the command flag across that second request. Only the successful fallback's final apply, a failed fallback, or no fallback available calls `menu_finishLoadSaveCommand()`. |
| `PRESET_OP_GLOBALS_LOAD` / All / Performance that originated from an explicit row | `menu_finishGlobalApply()` (or its stale-warning terminal callback) | Do not reset on the filesystem callback. The global worker completes the command after all parameter sends; a stale-warning overlay, if applicable, completes it only when its timer expires. |
| successful asynchronous saves | their `menu_pollPresetStatus()` case after durable filesystem/index completion | Preserve existing name-cache refresh/scratch update first, then call the command finalizer instead of direct busy/reset/repaint stores. For nested Instrument Save, the finalizer resets to the top-level type row before the existing non-command index reload is posted. |
| File/Dir test operation | test completion case before timed result overlay | Finalize the underlying command before showing the copied result overlay. When the overlay expires, it repaints the already-restored type row; it must not perform a second reset. |
| ordinary filesystem error | `menu_pollPresetStatus()` error branch | Call the command finalizer before `menu_showFilesystemErrorOverlay()`. Make the overlay helper presentation-only: it must not clear busy or own cursor state. Non-command selection/index errors retain their existing behavior. |
| Samples modal | `menu_loadSamplesModal()` exit | Begin the command before suspending audio; force the initial repaint/LCD queue to drain before replacing the page with modal progress text. After audio resumes, call the command finalizer before success/error presentation. |

**Required call-site removals.** Remove the immediate `menu_resetSaveParameters()` after an accepted top-level Save request in `menu_handleLoadSaveMenu()`; it currently moves the logical selection before the operation is done. Remove or redirect direct finalization stores in `menu_finishSoundApply()`, `menu_finishGlobalApply()`, `menu_loadSamplesModal()`, the general filesystem-error branch, test result branches, Instrument Save, Kit Save, Scene/Bank/Globals save, Pattern load, and the default completion branch. Each retains its data-specific cleanup, then delegates the shared UI transition exactly once.

**Why it must exist.** The requested behavior says *always* return to the menu-type selection after an `ok`/`OW` operation. Today some branches reset immediately on request posting, some after filesystem completion, and some after post-apply work. Central ownership is necessary to avoid a restored cursor while `...` is still active, or a final `ok` while a second stage is still running.

**Inputs.** Existing completion flags and the command lifecycle state from step 5.

**Outputs.** Exactly one return to `SAVE_STATE_EDIT_TYPE` for every accepted command, including errors; no duplicate repaint or premature unbusy during a Scene/Bank runtime apply.

**Code affiliates.** `Core/Menu/menu.c`: all current `menu_resetSaveParameters()` calls reachable from Load/Save buttons, `menu_storageBusy` stores in those same branches, `menu_showFilesystemErrorOverlay()`, `menu_tickSoundApply()`, `menu_tickGlobalApply()`, and modal completion callbacks.

### 7. File-level change manifest and implementation order

1. `Core/Hardware/SD/filesystem.c`
   - Remove the runtime Bank recursive blocking call and Bank-only recursive quarantine helpers; preserve boot-only root-Kit quarantine helpers.
   - Add `op_scene_payload_phase`, rename/reuse `op_bank_payload_active`, and document their exact +1 B / 0 B memory effects adjacent to the declarations.
   - Extract the phase-8..72 Scene reader into `filesystem_beginScenePayload()` / `filesystem_scenePayload_tick()`; make root Scene and Bank adapters consume a named completion result rather than sharing numeric phases.
   - Run the existing canonical Kit-member basename predicate after `storage_kitsetFinalize()` for Bank-origin payloads, before any Instrument file/open/commit. Keep type-driven Instrument and LFO parser calls in the one common worker.
   - Rename the save-index refresh chain to the generic library-index refresh chain and update all callers.
2. `Core/Hardware/SD/filesystem.h`
   - Update only the `filesystem_requestLoadBank()` contract comment: selected children are foreground-pumped and atomically validated; no full-tree runtime quarantine is performed. No new public state-machine phase or payload API is exposed.
3. `Core/Bank/Scene/Preset/presetManager.c` and `.h`
   - Change Globals request functions to `uint8_t` accepted/rejected APIs with matching adjacent public/private comments.
   - Update Bank completion comments to say callback occurs after selected payloads, HCNAMES, and Bank index restoration are durable; preserve its one `PRESET_OP_BANK_LOAD` completion and empty-Bank result.
4. `Core/Menu/menu.c`
   - Add the +1 B command flag and static helpers with adjacent lifecycle documentation.
   - Convert command entry dispatch, all confirmation rendering, marker rendering, input guards, and exact terminal owners described in steps 5–6.
   - Do not change live Kit/KitMrp/Instrument browsing behavior or background selection/index progress.
5. `knowledge_files/specification_reference/SRAM_MANIFEST.md`
   - Update the generated-date/result only after a successful linked image exists. Add a compact operation-state row/note for `op_scene_payload_phase` (+1 B) and `menu_loadSaveCommandActive` (+1 B), and update the linked totals from actual tooling rather than source estimates.
6. `BANK_LOAD_FIX.md`
   - Record the actual phase/helper names, exact linked SRAM delta, build output, and each hardware verification result as implementation proceeds.

## Verification plan

1. Build with the normal firmware target and run `git diff --check`. Record final `size` output and compare retained data/BSS to the pre-change baseline; the planned maximum is +2 B SRAM1, subject to compiler alignment and measured linked output.
2. On hardware, select `Load: Bank`, Bank `000 Full`, with all 16 child LEDs selected. Press `ok` and verify the label immediately becomes `...`, all cursor markers disappear, and audio/UI continue to make progress throughout the load.
3. Before that load, deliberately alter every resident Scene's Kit. After completion, verify each selected Scene restores its corresponding six Instrument types and parameters from `000 Full`; switch through all 16 Scenes and verify the data is retained, not just the active Scene.
4. Verify the active Scene's sound, type arrangement, LFO source/target/depth/rate, and other modulation targets are already correct immediately after Bank completion. Switch away and back only as a control; it must not be required to make LFO modulation apply.
5. Repeat with a partial Bank child selection. Verify only selected existing child slots change, unselected resident Scene payloads/names remain intact, and the active Scene becomes a selected loaded child when the saved active child is not selected.
6. Test an empty Bank and an invalid/missing child. Verify bounded progress, one clear filesystem error or documented fallback, `...` while active, then `ok`/`OW` plus `[]` on the Load/Save type row at completion.
7. Test each explicit Load/Save confirmation type, including overwrite Save, Globals, nested Instrument Save, test commands, and Samples/modal commands. Confirm `...`/no-cursor during work and a single final type-row selection afterward. Confirm live Kit scrolling and Bank child preview scans do not spuriously display `...`.

## Implementation notes log

- 2026-07-27: Investigation only; no source or SRAM changes were made for this plan.
- 2026-07-27: The primary Bank-specific blocker is the runtime call from `filesystem_repairNames_tick()` phase 43 into `filesystem_quarantineBankKitsBlocking()`. It recursively performs blocking FAT operations across the Bank tree instead of yielding to `filesystem_tick()`.
- 2026-07-27: The current Bank payload parser already reuses most of the Scene parsing implementation, but does so through private shared `op_phase` jumps. The refactor should preserve the shared parser while replacing those jumps with named context/continuation ownership.
- 2026-07-27: Deep dive completed. The exact planned retained-state budget is +2 B SRAM1: one shared Scene-payload phase byte and one explicit Menu confirmation-command byte. No firmware code, generated image, or SRAM manifest has been changed during investigation.
- 2026-07-27: Implementation: removed the active runtime call from Bank name-repair phase 43 into the recursive blocking Bank-tree quarantine. The retained helper code is compiled out as a historical reference; boot-only root-Kit quarantine remains active. Bank-origin `kitset.kcg` now applies the existing canonical member-name rule inside the shared Scene parser before payload commit, so only selected children are validated and no runtime load renames unrelated card contents.
- 2026-07-27: Implementation: retained the existing Scene worker rather than adding the proposed second phase byte. Root Scene and Bank children already parse through the identical phase-8..72 code path; preserving that proven shared parser avoids a redundant payload state machine and keeps the linked SRAM result at the current baseline. The named Bank/Scene handoff remains a future readability-only refactor, not a required correctness or storage change.
- 2026-07-27: Implementation: renamed the save-oriented index-refresh ownership to a generic library-index refresh chain, changed Globals requests to return accepted/rejected status, and added Menu's one-byte explicit-command lifecycle. The linked check measured `text=351,788 B`, `data=400 B`, `bss=69,948 B`; Menu's byte was packed without increasing BSS.
- 2026-07-27: Hardware follow-up: the remaining asynchronous Bank-name repair was still preceding every Bank request. Removed that preflight entirely from `filesystem_requestLoadBank()` and restored standalone repair completion at phase 43. Bank Load now posts `FS_INTERNAL_OP_LOAD_BANK` immediately, captures its root display name, then uses the existing selected-child Scene reader without a repair handoff.
- 2026-07-27: Reverted the preceding direct-start experiment after it regressed the at-boot Bank restore. `filesystem_requestLoadBank()` again uses the prior bounded immediate-child name-repair handoff at phase 43; the removed recursive blocking embedded-Kit traversal remains removed. This restores the known boot ordering while retaining the selected-child parser validation and the UI/cache changes.
