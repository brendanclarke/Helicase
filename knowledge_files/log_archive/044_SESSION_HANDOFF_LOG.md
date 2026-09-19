# Session 044 Handoff — Boot Scene Activation And Runtime Bank Load Closure

DATE: 2026-07-28

SESSION GOAL: Correct the initial Bank Scene's cold-boot Instrument
types/parameters/LFO assignments, repair top-level runtime Load:Bank, simplify
Scene/Bank Load completion around shared lower-level operations, and make all
explicit OK/OW commands expose one consistent in-progress/terminal UI.

COMPLETED: Cold boot now initializes SceneData before tagged DSP runtime
construction, applies every incoming slot image before rebuilding cross-slot
modulation, and starts the exact ordinary Scene-switch worker after audio
startup. Load:Bank now obtains its child mask when first entered without a
resident Bank index, cannot accept OK while that preview is incomplete, and
preserves its completed “loaded a Scene” result through DSP application.
Root Scene and Bank Loads restore their unchanged browser index read-only as
their final command step; numbered-root Saves alone rescan/rebuild an index.
The accepted-command UI displays `...`, suppresses cursors, retains the input
gate through all terminal work, and always returns to the bracketed type row.
Four pre-audio SD pacing changes were also implemented as an experiment after
an intermittent warm-boot hang report.

VERIFIED ON HARDWARE:

- Yes: the initially selected `000 Full` active Scene now has the correct
  Instrument behavior at cold boot. The Snare parameters address the Snare
  runtime rather than a stale Drum member.
- Yes: the two reported LFO modulations now apply at boot without manually
  changing the LFO target or switching away from/back to the Scene.
- Yes: top-level Load:Bank no longer remains indefinitely on OK and restores
  the Bank payload.
- Yes: after the final ordering change, a Bank loaded during playback applies
  the currently active Scene immediately, matching Load:Scene rather than
  requiring a later Scene round trip.
- Partly/indirectly: the unified OK/OW command lifecycle was exercised through
  the repaired Scene/Bank workflows. A complete hardware matrix across every
  latent Settings/Samples/legacy operation was not run.
- Inconclusive: one intermittent boot hang was seen after installing the
  timing-test firmware, but it could not be reproduced afterward. The four
  delays did not prove a cause or a fix.

CHANGES THIS SESSION:

- `main.c`: initializes all Scene/Bank retained ownership before
  `dsp_init()` constructs tagged runtime members; starts the normal deferred
  Scene worker immediately after `audioCodec_init()`; adds 50 ms post-mount and
  50 ms pre-Bank boot holds.
- `Core/Bank/Scene/Preset/presetManager.c/.h`: separates one-slot tagged
  reset/image application from supplemental modulation binding; uses the
  existing Instrument rebind cursor after all six Scene slots are valid;
  retains the Scene apply gate until rebinding completes; makes Settings
  load/save return whether their asynchronous request was accepted.
- `Core/Menu/menu.c`: adds accepted-command ownership for `...`, cursor
  suppression, input locking, and terminal type-row reset; repairs the
  Bank-index-to-child-preview continuation; gates input during Bank preview;
  adds one post-DSP final-index helper/callback shared by root Scene and Bank
  Loads.
- `Core/Hardware/SD/filesystem.c/.h`: exposes the common read-only numbered-root
  index reload; renames the physical scan/write continuation as a Save-owned
  rebuild; makes HCNAMES completion neutral; preserves the completed Bank
  loaded-Scene result; leaves selected Bank children on the shared Scene
  reader; adds a 250 ms pre-SD-init hold.
- `Core/Hardware/SD/SPI/sd_routines.c/.h`: paces ACMD41 attempts by one real
  millisecond with a one-second readiness timeout.
- `Core/Hardware/timebase.c/.h`: adds the pre-audio-only millisecond hold used
  by the four SD timing boundaries.
- `INITAL_BOOT_MISLOAD.md`: complete investigation, implementation contracts,
  LFO follow-up, storage constraints, and verification record. Its durable
  content is preserved below.
- `BANK_LOAD_FIX.md`: complete targeted Bank admission fix, supplementary
  final-index ordering deep dive, implementation notes, and verification
  record. Its durable content is preserved below.
- `knowledge_files/specification_reference/FILESYSTEM_SPEC.md`,
  `MODULE_INTERCHANGE_SPEC.md`, and `SRAM_MANIFEST.md`: reconciled to the
  Session 044 runtime/load/index and final linked-memory contracts.
- `SCOPING_TARGETS.md`: Phase 3 status reconciled to the completed 16-Scene
  Bank, Session 044 Load/Save behavior, and rejected/unimplemented autosave.
- `MEMORY.md`: volatile/durable project context reconciled for Session 044.

KNOWN ISSUES INTRODUCED:

- The four boot delays increase pre-audio startup time by at least 350 ms plus
  any ACMD41 retry pacing. They are intentionally confined to boot.
- Their motivating intermittent hang is not localized. If it recurs, do not
  add more blind delays; record the exact blocking stage/operation without the
  timing perturbation caused by `lcd_waitForIdle()`.

KNOWN ISSUES RESOLVED:

- Cold boot no longer constructs all six runtime slots as accidental Drums
  from raw zeroed SceneData.
- Loaded Snare descriptor values no longer write through a stale Drum tagged
  member at startup.
- Both reported boot LFO destinations now install through the same complete
  clear/image/rebind lifecycle as a manual Scene switch.
- Entering Load:Bank and accepting unchanged Bank `000` no longer submits an
  invalid zero Scene mask.
- User input cannot race Bank child-preview completion.
- Bank Load no longer loses its completed loaded-Scene result to an early
  index-rebuild request.
- Runtime Bank Load now applies the currently playing Scene immediately.
- Scene/Bank Loads no longer perform a physical directory scan and rewrite an
  unchanged `.hcindex`.
- Accepted OK/OW commands no longer remain visually stuck at `ok`; command
  progress and terminal cursor placement are consistent.

NEXT SESSION RECOMMENDED GOAL: Continue the explicitly requested Phase 3 work
from the now-working Scene/Bank load/save baseline. If the boot hang becomes
reproducible, first add non-blocking stage/operation deadlines or a
non-perturbing retained/GPIO marker; do not infer the cause from developer-mode
OLED waits.

BLOCKERS: The intermittent boot hang currently has no reproducible test case.
The existing autosave implementation is explicitly rejected and must not be
used as a base without a fresh design/implementation review.

CRITICAL REMINDERS FOR NEXT SESSION:

- Any new/enlarged RAM allocation still requires exact byte/region/lifetime/
  owner disclosure and user acknowledgement. Session-specific permission for
  up to 32 bytes of unanticipated growth did not create general headroom.
- Instrument arrangements are fully dynamic. Never assume
  Drum/Drum/Drum/Snare/Cymbal/HiHat or a fixed type in any physical slot.
- Target tokens are local to the selected target Instrument type. Install
  LFO/velocity destinations only after all relevant tagged runtime members have
  their final types.
- A pure Load may borrow the shared cache for HCNAMES, but it must restore the
  unchanged Scene/Bank `.hcindex` read-only only after DSP apply. A Save that
  mutates a numbered root owns the physical rescan/rebuild.
- The Load:Bank child mask is not valid merely because `/Bank/.hcindex` is
  resident. It is valid only after the highlighted Bank's immediate children
  have been scanned.
- Do not treat the committed `Autosave.c/.h` and accompanying drafts as
  accepted architecture. The user explicitly said that work was done
  incorrectly.

---

## 1. Initial boot parameter/type/LFO repair

### Scope and constraints

The repair was limited to the mismatch between a loaded initial Scene's stored
Instrument types/parameters and the six live tagged DSP runtime slots. The
selected Bank Scene must sound and behave identically to switching away and
then back.

The user first approved a four-byte SRAM increase and then allowed up to 32
bytes of unanticipated growth for this work without further approval. The final
implementation added no retained apply object, cache, stage, union capacity, or
material stack allocation. It reuses the existing `drumset_apply_*` and
`instrument_apply_*` state.

The filesystem format, 2,048-byte typed stage, HCNAMES, Bank masks, Pattern
storage, and 1,176-byte tagged runtime slot reserve remain unchanged.

Instrument membership must stay fully flexible. Every slot obtains its type
from:

```c
scene_instrumentSlotConst(active_scene, slot)->type
```

No part of the repair may assume the supplied fixture's
`drm, drm, drm, snr, cym, hat` arrangement, a fixed Snare slot, or equal type
arrangements between Scenes.

An earlier Pattern-selection experiment was reverted after hardware testing
showed it did not fix the problem. Existing autosave work was also explicitly
excluded because it was considered incorrect.

### Fixture and confirmed fault boundary

`SD_CARD/Bank/000 Full/bankset.bcg` selects local Scene 06, the seventh physical
Scene button. Its `Kit DocWire/kitset.kcg` contains
`drm, drm, drm, snr, cym, hat`; its Snare payload contains
`noise_freq=127` and `osc1_noise_mix=102`. The card data was valid.

The original startup order was:

```text
dsp_init()
  -> instrumentManager_runtimeInit()
       -> read zero-initialized scenes[]
scene_initAll()
```

`INSTRUMENT_TYPE_DRM` is zero. Raw BSS therefore appeared to be six valid Drum
assignments. Later Snare descriptor writes could dispatch through a still-Drum
`runtime_slot_type[]` and write a Snare descriptor offset into the wrong union
member. This matched the observed Snare mix/noise controls affecting the wrong
behavior.

The runtime Scene-switch flow also clears and rebuilds the complete modulation
graph, while the initial boot path originally used an abbreviated synchronous
apply. Correct parsing alone could not make an LFO destination live when its
target install lifecycle was incomplete.

### Required and implemented invariant

For each of the six slots after boot and after later Scene/Bank activation:

| Runtime item | Required source/owner |
| --- | --- |
| `runtime_slot_type[slot]` | Active Scene slot type |
| Tagged union member | That type's initializer |
| Descriptor runtime write | Descriptor belonging to the same stored/runtime type |
| Routing and Scene settings | Active Scene retained settings |
| LFO/velocity source cells | Active Scene source slot |
| LFO target | Resolved after target members are valid, in the target type's local descriptor namespace |

Invalid/unsupported target pairs normalize to
`INSTRUMENT_TARGET_TOKEN_OFF`; they must never be interpreted through a stale
physical-slot type.

### Change 1: initialize SceneData before tagged runtime construction

`main.c` now calls `scene_initAll()` and `bank_init()` before `dsp_init()`.

Inputs are power-on BSS and SceneData's existing default type construction.
Outputs are defined per-slot Scene type records before
`instrumentManager_runtimeInit()` initializes `runtime_slot_type[]` and the six
union members. This is an ordering-only change with no storage cost.

Affiliates are `scene_initAll()`, `bank_init()`, `dsp_init()`,
`instrumentManager_runtimeInit()`, and every type-specific `*_initVoice()`.

### Change 2: separate slot image application from graph binding

The former combined private `preset_applyKitVoice()` path became:

- `preset_resetAndApplyKitVoiceImage(scene, slot)`: reset the tagged member
  from the retained type, apply routing, and apply the Morph-derived descriptor
  image.
- `preset_applyKitVoiceSupplemental(scene, source)`: normalize and install the
  source's LFO/velocity graph only during the later all-source rebind phase.

Inputs are the active Scene, source/target slots, retained tagged slot records,
descriptor registry, Scene routing, and Morph images. The image helper outputs
a type-correct live member without cross-slot destinations. The supplemental
pass outputs valid bindings only after the whole type vector is live.

This boundary is required because an LFO source can target any other slot and
the parameter token is local to that target type. Binding source 0 before slots
1..5 have been reset would make validity depend on loop order.

Affiliates are `preset_applyKitAudioRouting()`,
`presetMorph_applyVoiceNow()`, `preset_normalizeSlotModulationTargets()`,
`instrumentManager_resetRuntimeSlot()`, and
`instrumentManager_writeRuntime()`.

### Change 3: boot uses the complete active-Scene transaction

The pre-audio `preset_sendDrumsetParameters()` now:

1. Clears all outgoing runtime modulation destinations.
2. Ensures Morph is initialized.
3. Applies Scene-wide settings.
4. Reset/image-applies all six slots from their actual retained types.
5. Drains pending Morph descriptor writes so destination bases are settled.

It deliberately does not claim that a boot-only binding loop is equivalent to
the known-good live path.

Immediately after `audioCodec_init()`, `main.c` calls
`preset_startDrumsetApply()`. The ordinary Scene-switch worker then performs
its complete clear, six-slot image, and all-source rebind sequence in the same
environment as a manual Scene change. This final revision followed hardware
evidence that a partial post-audio rebind still left both reported LFOs
detached; only replaying the whole working Scene transaction corrected them.

Inputs are the fully committed and selected active Scene and the existing
Morph/apply cursors. Outputs are six matching live types/images/routes and a
complete LFO/velocity graph. There is no boot-only retained cursor.

### Change 4: deferred Scene switching finishes with the same all-source rebind

`preset_tickDrumsetApply()` no longer declares completion when the six pending
image bits reach zero. It starts the existing Instrument target-rebind phase
and keeps `drumset_apply_active` asserted until that cursor drains.

The quiet-envelope behavior remains: at most one safe slot image is replaced
per foreground pass. `preset_applyDeferredSceneSlotForTrigger()` remains the
trigger-time escape hatch, but it applies only the pending slot's type/image/
route. If that clears the last pending bit, it starts the same all-source
rebind.

Inputs are the existing `drumset_apply_*` mask/scene/voice state and
`instrument_apply_*` phase/source state. Outputs are bounded, audio-safe slot
replacement followed by graph validity. Menu's sound-apply gate cannot finish
while cross-slot target installation is incomplete.

No apply-state field was added.

### Change 5: preserve typed LFO/velocity parsing and installation

No second parser and no fixed physical-voice parameter map were added. Storage
continues to parse the compact source Instrument cells. During the all-source
rebind, each source processes both LFO pairs and velocity:

1. Read `lfo_target_voice`/`lfo_target_param`.
2. Validate self, voice 1..6, or Scene namespace.
3. Resolve the parameter token in the selected target type's descriptor
   namespace.
4. Normalize invalid combinations to off.
5. Install the descriptor, slot-decimation, per-voice Morph, or Scene adapter.
6. Repeat for `_2`, then install velocity.

Affiliates are `storage_instrumentParseLine()`,
`storage_instrumentFinalize()`, `preset_normalizeLfoTargetPair()`,
`instrumentManager_lfoTargetIdFromToken()`,
`instrumentManager_installLfoModulationTarget()`, and
`instrumentManager_installVelocityModulationTarget()`.

The verified fixture contained two nonzero LFO source configurations:

- Slot 2 `docwird2.drm`: rate 26, amount 115, sync 8, retrigger voice 2; the
  file-only `self` destination resolves to slot 2 local descriptor 1,
  `osc1_pitch_coarse`.
- Slot 3 `docwird3.drm`: rate 28, amount 120; `voice=2,param=12` resolves in
  slot 2's Drum descriptor namespace to `amp_envelope_decay`.

Both had parsed correctly before the final fix but were not installed at cold
boot. Manually editing the target or round-tripping the Scene invoked the
complete runtime lifecycle and made them work. The post-audio full worker is
therefore an installation/lifecycle fix, not a storage-schema change.

### Change 6: preserve loader/runtime ownership separation

`filesystem_loadBankDirectory_tick()` remains responsible for parsing,
validating, committing selected Scene payloads, and selecting the Bank's
validated active Scene. Menu/Preset is responsible for activating that
already-committed Scene in DSP runtime.

No tagged runtime mutation occurs while a Bank child is only partially
validated. No Bank-specific Instrument parser or LFO cache was introduced.

### Boot-fix verification record

The implementation passed `make -j2`, `make img`, and `git diff --check`.
During the focused fix, linked `.bss` did not grow relative to the clean
baseline. Final Session 044 linked memory is recorded in section 5.

Hardware confirmed the supplied active Scene's Snare behavior and both LFO
assignments now match a manual Scene return.

---

## 2. Targeted top-level Load:Bank admission fix

### Confirmed cause

The original apparent hang occurred before
`filesystem_requestLoadBank()` started. Top-level Bank Load intentionally
cleared `menu_kitLoadSceneMask` because valid selectable bits must come from
the highlighted Bank's physical `00..15` children.

On first entry, when `/Bank/.hcindex` was not resident:

```text
enter Load:Bank
  -> clear Scene destination mask
  -> request /Bank/.hcindex
  -> index callback unlocks/repaints
  -> no selected-Bank child preview

activate unchanged 000 OK
  -> preset_loadBank(0, 0)
  -> filesystem rejects zero mask
  -> no operation or callback exists
```

Turning the Bank number caused another selection request after the index was
resident, which did request the preview. That explained why only the unchanged
initial Bank appeared to hang. Root Scene Load has no child-mask prerequisite.

A second race existed after preview request: input remained unlocked until the
scan callback populated the mask, allowing OK to submit the same zero mask.

### Change 1: continue index completion into the selected Bank preview

On successful top-level Load:Bank index completion,
`menu_libraryIndexLoadComplete()` releases the index request's gate and calls:

```c
menu_requestBankLoadPreview(menu_currentPresetNr[SAVE_TYPE_BANK]);
```

The preview helper then takes the same gate. The index callback does not issue a
second “ready” repaint when the preview owns completion.

Inputs are successful Bank index status, unchanged Menu page/type/slot, and the
newly resident cache. Output is exactly one
`filesystem_requestScanBankScenes()` for the highlighted Bank; no payload,
SceneData, BankData, or DSP mutation occurs.

### Change 2: preview owns the existing input gate

An accepted `filesystem_requestScanBankScenes()` sets
`menu_storageBusy`. It does not set `menu_loadSaveCommandActive`: preview is
preparatory browser work, not the user's accepted OK command, so it shows no
`...`.

`menu_bankLoadPreviewComplete()` releases the gate and, only if its page/type/
slot guards still match, publishes:

```c
menu_bankLoadPreviewMask = filesystem_bankChildSceneMask();
menu_bankLoadPreviewValid = 1;
menu_kitLoadSceneMask = menu_bankLoadPreviewMask;
```

For the supplied `000 Full`, the physical mask is `0xffff`. A rejected preview
request retains the existing deferred retry and does not claim or clear another
operation's gate.

The fix is Menu-only and adds no retained field. It deliberately does not
replace the Bank payload reader, which already delegates each selected child
through the shared Scene reader.

### Hardware result

The formerly unchanged `000 Full` selection now progresses from index to
preview to a real Bank request and restores the Bank payload. This addressed
the “OK but nothing happens” symptom.

---

## 3. Harmonized Scene/Bank Load terminal ordering

### Final ordering rule

Root Scene/Bank Load:

```text
validate/commit payload through shared Scene reader
  -> update and flush owned HCNAMES rows
  -> publish Preset completion/result
  -> apply the loaded active Scene through the shared DSP worker
  -> read the unchanged root .hcindex into the one browser cache
  -> end `...`, restore [type], unlock input
```

Numbered-root Save:

```text
write/promote the payload and owned HCNAMES rows
  -> physically rescan the mutated root namespace
  -> completely rewrite and flush .hcindex
  -> publish completion and unlock
```

HCNAMES remains part of the filesystem transaction because it is durable
resident identity and its failure must fail the Load. The final index operation
for a pure Load is a read-only cache restoration, not a physical rebuild.

### Confirmed defect

There is one 9,000-byte cache for `.hcindex` and HCNAMES domains.

Scene Load entered the shared Scene HCNAMES updater, whose close phase
unconditionally scheduled a physical Scene scan/rewrite inherited from Scene
Save.

Bank Load independently scheduled a physical Bank scan/rewrite after HCNAMES.
Starting that new operation passed through generic `filesystem_start()`, which
cleared `op_bank_loaded_scene`. Preset/Menu then observed a successful Bank as
though it had loaded no Scene and skipped `menu_startSoundApply()`. The
resident payload was correct, so changing Scenes later applied it.

The Bank payload loop itself was already correct: it iterates selected/present
children and delegates each to `filesystem_loadSceneDirectory_tick()`, the same
reader used by root Scene Load.

### Change 1: explicit read-only root-index reload API

`filesystem_requestReloadLibraryIndex(kind, cb)` maps Kit, Scene, and Bank
kinds to the existing slot-ordered index reader. The older domain-specific
wrappers delegate to it.

The former internal `*_library_index_refresh_*` chain is named
`*_library_index_rebuild_*`. It means physical parent scan plus full
`.hcindex` rewrite and is scheduled only by a numbered-root Save that mutated
its namespace.

Inputs to reload are an idle filesystem, the root-library kind, the existing
index, and an optional callback. Output is the correctly tagged/populated
shared cache with no directory mutation or index write.

Inputs to rebuild are a successfully mutated root, its kind, and the parked
original Save callback. Output is one callback after scan, full index write,
and flush.

### Change 2: shared HCNAMES completion is neutral

`filesystem_residentNames_tick()` now finishes only the requested HCNAMES
transaction. It does not infer whether `/Scene/` changed.

Scene Save declares a Scene rebuild before it hands off to the shared HCNAMES
updater. Root Scene Load does not. If the pre-rebuild Scene Save transaction
fails, `filesystem_finish()` cancels the pending rebuild so an unrelated later
success cannot inherit it.

Outputs:

- Scene Save still completes after physical Scene rescan/index rebuild.
- Scene Load completes after HCNAMES durability with the cache still in
  HCNAMES mode; Menu owns the post-DSP read-only Scene index reload.
- Public targeted Scene-name updates acquire no hidden root-index mutation.

### Change 3: preserve the completed Bank result

Bank Load no longer sets `op_bank_loaded_scene` merely because a child directory
opened. The authoritative assignment occurs only after the shared Scene parser
validates and commits that child.

At Bank HCNAMES close, Load calls `filesystem_finish(FS_STATUS_DONE)` without
scheduling a Bank rebuild. The original Preset callback can consume
`op_bank_loaded_scene` before a new request resets operation scratch.

Bank Save still owns the Bank physical rebuild because it can mutate `/Bank/`.

### Change 4: one shared Menu terminal helper

After synchronous pre-audio or chunked foreground sound apply completes,
`menu_requestLoadCommandFinalIndexRestore()` checks the still-locked accepted
command:

- no accepted command: no browser I/O (the boot case);
- explicit root Scene Load: read `/Scene/.hcindex`;
- explicit root Bank Load: read `/Bank/.hcindex`;
- other operation: retain its existing terminal path.

The helper derives the type from locked `menu_saveOptions.what`; no retained
operation-kind byte was added.

The dedicated `menu_loadCommandFinalIndexComplete()` callback ends `...`,
releases input, resets to the bracketed type row, and repaints. It is not
`menu_libraryIndexLoadComplete()`, because that entry callback has the required
Load:Bank child-preview side effect.

If final reload fails, the loaded audio state remains committed. The callback
clears the unusable cache, terminates the command so it cannot strand `...`,
then shows the existing filesystem error overlay while the error code is still
available. An unexpectedly rejected terminal reload is likewise treated as a
terminal filesystem error rather than a false success.

### Change 5: Save behavior remains shared where contracts match

Kit, Scene, and Bank Saves each schedule the one numbered-root physical
scan/index rebuild after their successful mutation. Instrument Save retains its
typed index refresh because its namespace is registry/type specific rather than
one numbered root.

`menu_refreshSavedLibraryName()` reads the newly rebuilt selected row for Save
display. Pure Loads do not use it.

### Hardware result

After this ordering change, runtime Load:Bank during playback applies the
currently active Scene immediately. The user confirmed the fix works. Root
Scene and Bank explicit Loads now have the same payload/HCNAMES/DSP/final-cache
terminal pattern.

---

## 4. Unified OK/OW command lifecycle

`menu_loadSaveCommandActive` represents only a user command whose Preset/
filesystem request was accepted. `menu_storageBusy` is broader and also gates
preparatory browser/index work.

On accepted command:

- `menu_beginLoadSaveCommand()` sets command ownership and the storage gate.
- The confirmation surface changes from `ok`/`OW` to `...`.
- `[]`, `>`, edit underlines, and hardware cursor markers are suppressed.
- Encoder, Scene, page, and BAR inputs stay gated for the operation.

On terminal completion:

- `menu_finishLoadSaveCommand()` clears command/gate ownership.
- Load/Save state resets exactly once.
- Cursor returns to the top type selection with brackets.
- The confirmation text returns to `ok` or `OW` as appropriate.

The lifecycle spans direct filesystem completion, synchronous or chunked sound
apply, globals apply, Pattern/settings follow-up, and—after the supplementary
fix—the final root Scene/Bank index reload.

Rejected requests never enter `...`. Settings load/save APIs were changed to
return accepted/rejected status so they obey this rule too.

This adds one Menu byte at source level, but the final linked image has no BSS
growth relative to the pre-session 69,948-byte baseline because layout/padding
absorbed it and other changes offset initialized storage.

---

## 5. Intermittent boot experiment

### Report and hypothesis

An intermittent boot hang was reported, approximately half the time initially,
with the same firmware/card files. Long power-off/unplug appeared less likely
to hang, and `CONFIG_DEV_MODE` diagnostics appeared to prevent it. That
suggested a possible warm-reset/card-ready timing boundary.

Four pre-audio timing changes were requested and implemented:

1. `filesystem_initCardAndMountBlocking()` holds 250 ms after
   `spi_sd_set_slow()` and before `SD_init()`.
2. `SD_init()` paces CMD55/ACMD41 pairs by 1 ms and uses a real 1,000 ms
   readiness timeout instead of 255 CPU-speed attempts.
3. `main.c` holds 50 ms after asyncfatfs mount reports ready and before the
   first Kit scan.
4. `main.c` holds 50 ms after all root/Instrument index writes and before the
   Bank index reload/initial payload.

`timebase_holdPreAudioMs(uint16_t)` is the shared helper. Its contract is
strictly boot-before-audio, foreground-only, after TIM6 timekeeping starts.
It must not become runtime filesystem pacing or be called from an ISR.

### Result and current diagnosis boundary

One hang was observed after flashing the new firmware, then repeated attempts
did not reproduce it. The test therefore did not prove whether any delay helped.

The delay experiment also established that “developer mode fixes it” is not
equivalent to one coarse card-power delay. Developer diagnostics call
`lcd_waitForIdle()` at individual filesystem stages and, during initial Bank
work, at operation/phase changes. Those waits insert latency at many internal
transitions and also alter compiled layout. Possible classes, if the issue
recurs, include:

- an unbounded boot loop waiting for a missed asyncfatfs callback/state change;
- a card command/write-busy transition that needs localization at the exact
  operation boundary;
- an async handle/state lifetime issue in repair/index/Bank traversal;
- undefined or uninitialized state whose manifestation changes with
  `CONFIG_DEV_MODE` layout.

Simple SD power-up settling is no longer a sufficient explanation by itself.
Every blocking boot loop currently lacks a diagnostic deadline, so any missed
transition appears as a permanent freeze.

The next useful test is non-perturbing localization: add a bounded deadline and
capture the current boot stage/filesystem operation/phase/SD state without
draining the OLED at every transition. No further change is justified until
the fault is reproducible.

---

## 6. Reverted/rejected paths

- The initial Pattern-selection experiment for the boot sound mismatch was
  reverted after hardware failure.
- Earlier attempted targeted boot changes that did not reproduce the full
  working Scene-switch lifecycle were superseded; partial LFO rebind was
  insufficient.
- A broad Bank-loader rewrite that regressed at-boot Bank Load was undone. The
  final runtime Bank repair is the Menu admission/result/index-ordering fix and
  keeps the shared Scene payload reader.
- Physical index rebuild during pure Scene/Bank Load is rejected. It mutates
  or clears operation/cache state before DSP consumers have finished.
- The committed `Core/Bank/Scene/Autosave.c/.h`,
  `AUTOSAVE_IMPLEMENTATION.md`, and `knowledge_files/drafts/
  AUTOSAVE_BLOB_SCHEMA.md` predate/fall outside the accepted Session 044
  solution. The user explicitly stated this autosave work was done incorrectly.
  It is neither hardware-verified nor an approved Phase 3 baseline.

---

## 7. Final build and storage verification

Commands:

```sh
make -j2
make img
git diff --check
arm-none-eabi-size build/lxr02.elf
arm-none-eabi-size -A build/lxr02.elf
```

Final linked totals:

```text
text = 352,404 B
data =     400 B
bss  =  69,948 B
```

Section allocation:

```text
.text         339,472 B
.itcm           3,768 B
.dtcm           8,708 B
.dtcmz          3,572 B
.dma_nocache    3,100 B
.data             400 B
.bss           63,276 B
```

Static RAM:

- DTCM: 12,280 B, unchanged.
- SRAM1: 66,776 B, four bytes below the Session 043 manifest.
- All static allocated RAM: 79,056 B.
- No new cache, Scene/Kit/Instrument/LFO stage, or boot cursor was allocated.

Artifact sizes:

```text
build/lxr02.bin          352,804 B
build/LXRV2_lxr02.img    352,820 B
```

`git diff --check` passes. The standard firmware build and image packaging
succeed; normal pre-existing newlib syscall and LTO serialization warnings are
unchanged.

The current working tree is on `dev-ph3-autosave`. Session 044 load/save and
boot-pacing edits remain uncommitted at handoff time; the initial boot runtime
fix is in commit `4b86021`. That commit also contains rejected autosave
material, so commit membership must not be mistaken for architectural approval.
