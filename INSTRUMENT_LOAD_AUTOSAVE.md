# Instrument Load AutoSave — Whole-Object Mutation-Mark Plan

## Status, decision, and scope

Implementation and the normal root-Instrument and InstrumentMrp hardware
fixtures are complete. This isolated AutoSave pass adds mutation-mask
production for successful root Instrument and same-type InstrumentMrp loads.
The reversible-`kit` fixture remains to be exercised separately. It does not
add an AutoSave boot reader, change record geometry, mark HCNAMES names, add
Load/Save exclusion, or extend the work to Kit, Scene, Bank, Pattern, or
Effect loads.

### Implementation notes — 2026-08-11

#### Trace-arbitration correction after fresh-card hardware capture

The fresh-card retest produced an authoritative HCNAMES source update
(`barfd1 @`) but only the boot-time eight-byte `S` record in
`/asavetrc.bin`. The two hidden records remained their creation baselines
(`.hcprms1` generation 1 and `.hcprms2` generation 0), so that capture does
not demonstrate a completed parameter drain. It also cannot prove that the
normal Instrument marker was skipped: normal Instrument completion occurs
while Load owns the filesystem, and the former idle scheduler gave an overdue
full-bank AutoSave drain priority over the trace once the user left Load.
Removing power while that first long drain was active therefore preserved the
older `S` append but not the later RAM-only `N`/`I`/`J` witnesses.

`filesystem_tick()` now retains settings first but schedules the pending trace
append before `filesystem_autosaveWriterSchedule_tick()`. This changes no
AutoSave mask, record bytes, source name, Menu exit behavior, or SRAM size. In
the logging build it gives the bounded existing trace ring one append at the
first idle poll after any Load/Save exit, before the writer can begin or resume
a lengthy drain. The trace scheduler still refuses to start while the active
page is Load or Save, so it cannot take the shared facade during the foreground
operation it observes. The purpose is diagnostic causality: an Instrument
commit must leave J/I evidence before an interrupted durable write can obscure
whether the mutation was published.

The following capture with that image still contained only `S`, despite the
new `beatmsd1 @` HCNAMES row. Source review then established the concrete
blocker: `menu_residentNameScratchFlushComplete()` is a direct filesystem
callback, not a Preset callback, and it inspected the successful HCNAMES
status without calling `filesystem_ack()`. The facade therefore remained
`DONE` after a normal Load/Save exit. All autonomous schedulers require
`IDLE`, so neither the now-prioritized trace nor the AutoSave writer could
start. The callback now captures its terminal result, acknowledges the facade
on both success and failure, and then performs its existing success/error UI
work. This is not a new exit path or allocation; it restores the mandatory
terminal release that the direct callback must provide.

#### Hardware acceptance — normal root Instrument load

The next hardware run flashed the acknowledgement fix and passed the normal
root-Instrument fixture. `asavetrc.bin` contains 71 records (568 bytes): the
root commit witness is `J` with flags `0x03` (whole mark requested and called),
and the immediately preceding `I` has flags `0x07` (payload base valid,
tracking enabled, and every requested byte published). Its packed value is
`0x004c4c05`, meaning Scene 5, slot 0, 76 expected bytes, and 76 accepted
bytes. The trace then reaches `A`, `V`, `M`, `C`, `P` (generation 2), and a
successful `T` (`flags == 1`).

`/.hcprms2` is consequently a committed generation-2 record, while the older
`/.hcprms1` remains generation 1. At Scene 5/slot 0, the persisted type token
is `drm`; the first Normal and Morph endpoint bytes (`03 24 7e ...`) match
`Instrument/Drum/brezeld1.drm`. The AutoSave name allocation still holds the
prior `rollind1` text, as designed: Instrument names and direct source remain
HCNAMES-owned. The authoritative HCNAMES row 63 (one-based line 64) is
`brezeld1 @`. This validates immediate root-load mutation marking plus the
subsequent writer path; it does not claim a boot reader or name-overlay feature.

#### Hardware acceptance — combined root Instrument and InstrumentMrp load

The following hardware fixture loaded a root Drum Instrument into Scene 5,
slot 0 (Drum 1), then a same-type DrumMrp source into Scene 5, slot 1 (Drum
2), before the ordinary Load/Save exit and idle wait. `/.hcprms1` is the valid
new winner at generation 3; `/.hcprms2` is the preserved generation-2 source.
At slot 0 the committed Normal and Morph values match
`Instrument/Drum/brezeld3.drm`. At slot 1, direct generation-to-generation
comparison shows no changed type, name allocation, or Normal endpoint bytes;
only its Morph allocation changed, and its resulting values match
`Instrument/Drum/casiopd3.drm`. Equal source/destination bytes need not appear
in the byte-difference list, but the complete destination Morph image has the
source values. This is the required Morph-only persistence result.

The appended trace reaches `A`, `V`, `M`, `C`, `P` (generation 3), and
successful `T`. Its fixed 64-entry RAM ring retained 64 `D` records for Scene
5/slot 1 Morph offsets, including the tail of one 34-cell Drum Morphable
sweep and a complete later 34-cell sweep; earlier root-load `I`/`J` records
therefore wrapped before the append. The ring cannot identify which UI event
caused each repeated sweep, so the acceptance conclusion is based on the
durable byte-for-byte A/B result, not a claim that the retained trace proves a
single Morph command. The authoritative HCNAMES rows remain outside HCPRMS
name storage, as designed.

- Field correction pending verification: the first implementation used Menu's
  temporary-operation latch as the completed-file classifier. The updated
  path reads filesystem's existing immutable `op_instrument_load_temporary`
  request flag through `filesystem_loadedInstrumentWasTemporary()`, then passes
  the resulting call-local `mark_autosave_whole_instrument` decision into the
  shared Preset apply path. It still avoids the mutable Instrument cursor and
  adds no retained state or SRAM allocation.
- `preset_startInstrumentApplyImage()` marks every destination immediately
  after assigning the staged root-pool Instrument into retained SceneData. The
  same helper receives zero for hidden `.hctmp` `kit` restore, so rollback
  cannot generate its own whole-Instrument mark.
- Field correction pending verification: the marker's payload mapping is
  intentionally limited to Bank-present Scenes. The commit path now publishes
  each actual destination as present immediately before assigning and marking
  it; this closes the gap between an accepted Instrument file and the earlier
  asynchronous present-mask notification. No new map, payload field, or RAM
  state is introduced.
- `preset_startInstrumentMorphApply()` marks only after a compatible staged
  endpoint copy succeeds, before its active-Scene-only runtime refresh branch.
- Existing `autosave_markWholeInstrumentDirty()` and
  `autosave_markInstrumentMorphDirty()` supply the required type/Normal/Morph
  and Morph-only coverage without new payload mappings or record geometry.
- Field capture on 2026-08-11 still contained no loaded-Instrument `D` offsets,
  despite ordinary scalar dirty events. To replace inference with evidence,
  the whole-Instrument marker now emits one fixed-size `I` outcome after every
  request. Its flags state map-base validity, tracking-gate state, and whether
  every requested byte reached the canonical dirty funnel; its value packs the
  destination Scene/slot plus expected and accepted counts. This observer is
  RAM-only, has no new allocation or record-size change, and must be checked
  in the next hardware capture before any further behavioral correction.
- The unrelated first-entry blank-`kit` delay now has diagnostic-only `N`
  milestones in the same fixed trace ring: entry request, HCNAMES read/flush,
  hidden temporary snapshot, and typed-index request/completion. Their paired
  ticks identify the serialized operation that consumes time; they do not
  change entry sequencing, AutoSave bits, filesystem policy, or RAM ownership.
- The `I` marker outcome alone cannot prove that Preset received a root-pool
  request to call it. A diagnostic-only `J` commit witness now follows every
  committed destination: bit 0 means Menu requested whole-Instrument marking;
  bit 1 means Preset called the marker. This separates a false provenance gate
  from a marker-internal rejection without changing the commit or mask.
- Approved Menu exit correction: `menu_storageBusy` previously discarded a
  physical mode-switch request, even though switching modes is the normal
  Load/Save exit. The user approved one normal-SRAM1 byte with zero as its
  empty sentinel and page-plus-one encoding to retain the latest non-Load
  destination only while the busy owner drains.
  `menu_pollPresetStatus()` consumes it through the ordinary
  `menu_switchPage()` cleanup path before any deferred browser selection can
  start. This changes no Instrument payload, HCNAMES ownership, or AutoSave
  policy; it guarantees the requested exit starts once resources are safe.
- Adjacent comments in Menu, Preset, and AutoSave C/H files now document the
  owner-before-marker rule, the temporary-restore exclusion, and the exact
  endpoint domain. `AUTOSAVE.md`, `FILESYSTEM_SPEC.md`, and
  `MODULE_INTERCHANGE_SPEC.md` were reconciled.
- Isolated `arm-none-eabi-gcc` compilation of `presetManager.c` and `menu.c`,
  followed by a full `make -j2` link, passed. The hardware fixtures below
  remain required; no target claim is made.
- Hardware-observation boundary: while `menu_activePage` is `LOAD_PAGE` or
  `SAVE_PAGE`, the existing scheduler intentionally retains both canonical
  dirty bits and the diagnostic trace in RAM. It neither starts the AutoSave
  drain nor appends `asavetrc.bin` until the user has left Load/Save; the
  normal five-second debounce then still applies, followed by the bounded
  record-copy/commit transaction. Likewise, the selected
  Instrument's `@` source is written only when the nested Instrument name
  session closes. A card copied while browsing is therefore not valid negative
  evidence for the immediate commit marker. This implementation changes none
  of those established ownership/exclusion rules.

The required policy is binding:

- A successful normal root Instrument Load marks its complete implemented
  Instrument payload immediately when that payload is committed to each
  destination Scene slot. “Immediately” means the commit/apply transaction,
  not Instrument-menu exit and not the later HCNAMES session flush.
- Normal root Instrument Load marks the three-byte type token, every owned
  Normal endpoint, and every owned Morphable Morph endpoint.
- Successful Instrument Morph Load marks only every owned Morphable Morph
  endpoint. It does not mark type, Normal endpoints, name, or HCNAMES source.
- Hidden temporary `kit` restore, parse/staging, failed/cancelled operations,
  incompatible Morph loads, and Instrument Save operations mark nothing.

The existing canonical mask is an OR-set, not a history queue. Therefore, if a
user loads several Instruments during one Instrument Load session—or loads the
same destination more than once—the first successful commit arms the required
coordinates and later commits overwrite the retained owners. The eventual
writer reads those final live owners. No per-session list, deferred “loaded
Instrument” cache, or additional SRAM is needed or permitted.

## Why the hook is at commit, not at menu exit

The current nested Instrument UI deliberately keeps an HCNAMES session open:
each normal pool load updates the seven-name scratch and row source in RAM;
one `/.hcnames` rewrite occurs only at the later session boundary. That is
correct for names, but it must not decide parameter persistence.

The authoritative parameter transition happens when
`preset_startInstrumentApplyImage()` assigns the validated staged slot into a
resident `scene->kit.instruments[slot]`. At that exact point the live AutoSave
getter can read every new value. Marking at `menu_loadInstrumentExit()` would
lose a sequence of loads if the user stays in the menu, changes voice, or loses
power before exit.

The existing scheduler already suppresses new background AutoSave starts while
a Load/Save page is active. Thus the mask may be armed during the Instrument
session without allowing a hidden record to race ahead of the session’s
deferred HCNAMES publication. On the session boundary the existing HCNAMES
update remains the foreground operation; only after the page releases its
Load/Save gate can the writer consume the already-marked values. This plan must
not weaken that ordering or start a writer directly from Menu/Preset code.

## Existing implementation facts

| Concern | Existing owner | Required outcome |
| --- | --- | --- |
| Normal root Instrument request | `preset_loadInstrumentForScenes()` | Its immutable destination mask can contain one or more Scenes. |
| Temporary `kit` restore | `preset_loadInstrumentTemp()` | Uses the same staged parser/apply path, but must never produce a new mutation mark. |
| Normal resident commit | `preset_startInstrumentApplyImage()` | Publishes each actual destination as Bank-present, then assigns the staged Instrument to its selected Scene/slot. |
| Instrument Morph commit | `preset_commitStagedInstrumentNormalToMorph()` | Returns nonzero only after compatible Morphable values were copied. |
| Whole Instrument marker | `autosave_markWholeInstrumentDirty()` | Already marks type, all owned Normal cells, and all owned Morphable Morph cells; excludes name. |
| Morph-only marker | `autosave_markInstrumentMorphDirty()` | Already marks only owned Morphable Morph cells. |
| Completion routing | `filesystem_loadedInstrumentWasTemporary()`, `menu_pollPresetStatus()`, and `menu_startInstrumentApply()` | Filesystem's request-local flag says whether the just-completed operation was the hidden temporary restore. |
| Deferred identity persistence | `menu_requestAppliedInstrumentNameUpdate()` and session exit | Remains HCNAMES-only work; it is not the parameter mutation hook. |

No new AutoSave payload offsets, mapping functions, live getter cases, source
tokens, or persistent memory are required. The one read-only filesystem origin
accessor exposes existing request state; it allocates neither storage nor a new
wire field. The present
whole-Instrument marker and live getter already agree on descriptor ownership:
Normal cells exist for each descriptor owned by the current type; Morph cells
exist only for descriptors flagged `INSTRUMENT_PARAM_FLAG_MORPHABLE`.

## Required source changes

### 1. Make the commit path explicitly distinguish pool load from temporary restore

Files: `Core/Menu/menu.c`, `Core/Bank/Scene/Preset/presetManager.c`, and
`Core/Bank/Scene/Preset/presetManager.h`.

`preset_startInstrumentApplyImage()` is intentionally shared by normal root
pool loads and the hidden `.hctmp.<ext>` `kit` restore. The marker therefore
cannot be unconditionally placed in that helper. The plan must thread one
explicit, call-local `mark_autosave_whole_instrument` decision from Menu to
that helper (either as a boolean argument or through two clearly named public
wrappers). Do not introduce a static “loaded Instrument pending” array, a Menu
session bitmap, or an additional retained request field.

At `PRESET_OP_INSTRUMENT_LOAD` handling in `menu_pollPresetStatus()`:

1. Read filesystem's request-local temporary flag before any subsequent
   filesystem operation can reuse it.
2. Use that immutable filesystem result—not
   `menu_instrumentTempOperationPending`, `menu_instrumentLoadSource`, the
   selected browser row, or the current encoder position—to classify the
   accepted completion. The Menu latch sequences temporary UI work and the
   cursor is intentionally mutable while a pool operation drains; neither
   identifies the staged file's source.
3. Start the normal apply with marking enabled only for a successful root pool
   load. Start a temporary restore with marking disabled.
4. Preserve the current temporary-name restoration and deferred session
   behavior exactly; this classification conveys persistence policy only.

The new/changed comments at this decision must state, in substance:

```c
/*
 * Carry the accepted operation's immutable persistence meaning into the
 * shared staged-Instrument commit.  The visible Load cursor may have moved
 * while I/O completed, so it cannot say whether this payload came from the
 * root pool or the hidden reversible `kit` file.  A pool replacement changes
 * retained Instrument data and must arm AutoSave at commit; a temporary
 * restore re-establishes the session baseline and must not manufacture a
 * second mutation event.
 */
```

Update `menu_startInstrumentApply()` and the public Preset declaration to
carry the same explicit policy. The parameter name must describe the outcome
(`mark_autosave_whole_instrument`), not an implementation detail such as
`from_menu` or `normal`.

Why this change exists: it permits the existing shared safe-DSP lifecycle to
remain shared while preventing the temporary `kit` rollback path from being
mistaken for a user-selected root Instrument replacement. It also makes each
normal load independently markable before menu exit.

### 2. Mark every committed normal destination in the shared commit loop

Files: `Core/Bank/Scene/Preset/presetManager.c` and
`Core/Bank/Scene/Preset/presetManager.h`.

Extend the private `preset_startInstrumentApplyImage()` signature with the
explicit marking policy from Step 1. In its existing destination-mask loop,
perform the mutation mark immediately after this successful retained-owner
write:

```c
scene->kit.instruments[slot] = *staged;
```

For every selected Scene that has a valid `scene_t`, call:

```c
autosave_markWholeInstrumentDirty(target_scene_index, slot);
```

only when `mark_autosave_whole_instrument` is nonzero. The marker must stay
inside the loop; calling it only for `scene_index`, the active Scene, or
`pm_instrument_request_scene` would omit MODE/VOICE fan-out destinations.

The added comment must establish all of the following:

```c
/*
 * The staged slot has now replaced this destination's retained owner, so the
 * live AutoSave getter can capture a self-consistent type plus endpoint image.
 * Mark here rather than at Instrument-menu exit: one session may commit many
 * root pool Instruments, and the canonical mask must represent each completed
 * destination even if the session never reaches its deferred HCNAMES flush.
 * The caller disables this for `.hctmp` restore because that path is only the
 * reversible `kit` session rollback, not a newly loaded root Instrument.
 */
```

Keep the call before the active-Scene-only runtime reset/routing/Morph worker.
The dirty mask represents retained SceneData, not the eventual DSP cursor
completion. It must never be moved into `preset_tickInstrumentApply()`, which
runs after only the active Scene’s bounded runtime work and would miss
non-active fan-out targets.

Update `preset_startInstrumentApply()` so normal pool callers pass the policy
received from Menu. Do not call the marker in `on_instrument_load_complete()`:
at callback time the staged candidate has not yet been committed to resident
SceneData, and a filesystem-success result alone does not identify temporary
versus pool semantics.

Why this change exists: it is the sole boundary at which all normal-load
requirements are simultaneously true—candidate validation succeeded, the
destination coordinates are immutable, the retained owner contains the new
data, and all fan-out destinations are visible.

### 3. Mark only Morph endpoints after an actual Instrument Morph commit

Files: `Core/Bank/Scene/Preset/presetManager.c` and
`Core/Bank/Scene/Preset/presetManager.h` comments.

In `preset_startInstrumentMorphApply()`, preserve the existing call to
`preset_commitStagedInstrumentNormalToMorph()`. Restructure the success branch
so that, immediately after that helper returns nonzero, it calls:

```c
autosave_markInstrumentMorphDirty(scene_index, slot);
```

Then retain the existing active-Scene conditional that queues
`presetMorph_requestVoice()` and the bounded Morph worker. The AutoSave mark
must be outside that active-Scene conditional: an inactive Scene’s retained
Morph image is still valid data that must be persisted, even though no audible
runtime refresh is needed now.

The added comment must make the endpoint boundary explicit:

```c
/*
 * InstrumentMrp copied only compatible Morphable endpoint values into the
 * retained destination.  Mark precisely that same descriptor domain now.
 * Type, Normal endpoints, name, HCNAMES source, routing, and runtime
 * interpolation did not change, so marking the whole Instrument here would
 * write unrelated state and conceal a type-mismatch no-change result.
 */
```

Do not mark when the helper returns zero. That zero covers invalid staging and
type mismatch, both deliberately defined as no-change for InstrumentMrp.
Do not add an HCNAMES update to this path: Instrument Morph Load preserves the
destination identity and source.

Why this change exists: the current Morph loader copies source **normal**
values into destination **Morph** endpoint storage only for same-type,
Morphable descriptors. `autosave_markInstrumentMorphDirty()` already embodies
the same registry/flag rule, so it is the exact non-broad marker required.

### 4. Promote the existing AutoSave helpers from “future” to live hooks

Files: `Core/Bank/Scene/Autosave.c`, `Core/Bank/Scene/Autosave.h`, and
`knowledge_files/specification_reference/AUTOSAVE.md`.

No functional AutoSave helper is required. Update the detailed comments for
these existing APIs:

- `autosave_markWholeInstrumentDirty()`;
- `autosave_markInstrumentMorphDirty()`; and
- the grouped whole-object declarations in `Autosave.h`.

They must say that these functions are now called after successful root
Instrument and InstrumentMrp resident commits, respectively. Retain their
current exact coverage and exclusions:

| Helper | Marked coordinates | Must remain excluded |
| --- | --- | --- |
| `autosave_markWholeInstrumentDirty(scene, slot)` | 3 type bytes, all owned Normal descriptor bytes, all owned Morphable Morph descriptor bytes | Instrument name, HCNAMES source token, padding, non-Morphable Morph cells, runtime state |
| `autosave_markInstrumentMorphDirty(scene, slot)` | all owned Morphable Morph descriptor bytes | type, Normal, name, HCNAMES source, padding, runtime state |

The comments must keep the owner-before-marker rule clear: the marker does no
copy and performs no filesystem I/O; it records bytes already committed to
SceneData for the later background writer. It must retain the normal mutation
tracking gate and the existing atomic mask-bit operations unchanged.

Update `AUTOSAVE.md` to list this as the first admitted whole-object load
hook, with the normal/Morph distinction above. It must explicitly say that
this is writer-side persistence only and does not implement the planned boot
reader or HCNAMES name-group AutoSave overlay.

Why this change exists: the code’s former “future copy/load” wording would be
wrong after the hook lands and could invite a later developer to remove or
duplicate the calls. The specification must match the exact payload domain
that reaches hidden records.

### 5. Preserve the existing HCNAMES and scheduler boundaries; document them where touched

Files requiring comment review: `Core/Menu/menu.c`,
`Core/Hardware/SD/filesystem.c/.h`, and
`knowledge_files/specification_reference/FILESYSTEM_SPEC.md`.

No new filesystem state machine or `settings.cfg` field is part of this plan.
In particular:

- `filesystem_loadInstrument_tick()` remains a parser/stager. Its successful
  normal-pool path continues to stage the identity stem and `@` source before
  Menu’s deferred HCNAMES update. It must not touch AutoSave.
- `filesystem_requestLoadInstrumentTemp()` remains explicitly excluded: its
  temporary flag suppresses HCNAMES publication and Step 1 suppresses its
  AutoSave mark.
- `menu_requestAppliedInstrumentNameUpdate()` remains name-scratch work only;
  it must not receive an AutoSave call.
- the Menu exit/session HCNAMES writer remains the only physical publication
  of the accumulated Instrument identity rows. It must complete before an
  AutoSave writer becomes eligible after the Load/Save UI gate is released.
- `settings.cfg` continues to contain no Scene or Instrument sources.

Add or update nearby comments only when needed to prevent a false future
connection between source/name publication and parameter dirty marking. The
comments must state that HCNAMES owns identity/provenance while AutoSave owns
the current typed payload bytes; neither is a duplicate owner of the other.

Why this review exists: putting the mark in a filesystem parser, name-update
helper, or menu exit would look superficially convenient but would either
capture uncommitted data, make multiple loads collapse into one late event, or
make the temporary rollback behave as a root load.

## Explicit non-changes

The implementation must not:

- add `autosave_noteInstrumentLoaded()`, a pending-scene array, an event queue,
  or any retained allocation;
- mark Instrument names or add an HCNAMES-backed name getter to AutoSave in
  this pass;
- change `AUTOSAVE_*` geometry, CRC, generation selection, dirty-mask format,
  trace format, writer chunk limits, debounce, or Load/Save page suppression;
- call `filesystem_markSettingsDirty()`;
- mark at parser EOF, filesystem callback, `menu_tickInstrumentApply()` finish,
  `menu_requestAppliedInstrumentNameUpdate()`, or Instrument-menu exit;
- change normal Instrument Save, Instrument Morph Save, Kit Morph Load, or
  hidden temporary save behavior; or
- invoke the HCNAMES resolver or imply that an AutoSave boot reader now exists.

These boundaries keep the change within the approved retained-memory policy:
all state remains in the existing 3,856-byte mutation mask and existing
request/Menu fields. No new RAM allocation is proposed.

## Expected coverage

For a successful normal root pool load into each committed `(scene, slot)`,
the set bits are exactly:

- the three Instrument type-token bytes;
- one Normal endpoint byte for every descriptor owned by the new Instrument
  type; and
- one Morph endpoint byte for every descriptor owned by that type and flagged
  Morphable.

For a successful InstrumentMrp load, the set bits are exactly the final bullet
above for its one compatible destination. The staged file’s `[params]` values
are copied into retained Morph endpoints, so the resulting AutoSave payload
contains those destination Morph values, not a second source-file view.

No bit represents a name, source token, descriptor padding, non-Morphable
Morph reserve, DSP interpolation, modulation bindings, Kit setting, Scene
setting, sibling slot, Pattern, or Effect.

## Hardware fixtures and acceptance criteria

Use copied pre-test `.hcprms1`, `.hcprms2`, `.hcnames`, and `asavetrc.bin`
fixtures. Keep the `D/I/S/A/V/M/C/P/T` trace enabled. The `I` record is the
whole-Instrument marker's compact outcome and must show base-valid +
tracking-enabled + all-published flags, with equal nonzero expected/accepted
counts. Individual `D` records can still wrap the small diagnostic ring and
are not, by themselves, evidence of lost canonical mask bits.

### Normal root Instrument Load, one destination

1. Start with valid, clean hidden records and AutoSave enabled.
2. Load a root Instrument whose type and at least one Normal and Morphable
   Morph value differ from the destination.
3. Before leaving nested Instrument Load, inspect/trace the canonical mask if
   diagnostic tooling permits: the whole-Instrument scope must already be
   dirty. Do not use menu exit as the trigger.
4. Wait longer than five seconds while still on Load. Verify no AutoSave write
   begins; the existing Load/Save suppression must hold.
5. Exit the Instrument session and then the parent Load/Save page normally.
   Leave the unit on an ordinary page without further controls long enough for
   the five-second debounce *and* the bounded AutoSave transaction to finish.
   In `asavetrc.bin`, require the post-test `S` schedule record to be followed
   by `A`, `V`, `M`, `C`, `P`, and successful `T`; a lone `S` means the fixture
   ended before a durable writer terminal and cannot distinguish an unacquired
   facade from a transaction interrupted before the trace could flush. Then
   copy the resulting hidden files.
6. Verify `.hcnames` contains the expected name and `@` for the destination;
   verify the committed AutoSave generation contains its type/Normal/Morph
   values and no unrelated payload changes.

### Multiple normal loads in one Instrument session

1. Without exiting nested Instrument Load, load at least two different slots;
   repeat one of those slots with a second root Instrument.
2. Confirm each successful load marks when committed, while no hidden write
   begins during the session.
3. Exit once and verify the eventual record contains the final retained image
   of every affected slot, including the second value of the repeatedly loaded
   slot. Unaffected slots must remain unchanged.

### Instrument Morph Load

1. Load a same-type InstrumentMrp file with values distinct from the target’s
   current Morph endpoints.
2. Verify only the target’s Morphable Morph bytes change in the eventual
   record. Its type, Normal values, name, source, and all other slots must be
   unchanged.
3. Repeat with a type mismatch or invalid file. No resident endpoint, dirty
   bit, HCNAMES row, or hidden-record generation may change.

### Reversible `kit` restore and failures

1. Enter normal Instrument Load, load one pool Instrument, then return to the
   `kit` row before exit.
2. The pool load’s earlier mark may remain set; the writer must capture the
   final restored retained values. The temporary restore itself must not add a
   separate whole-Instrument mark or change HCNAMES identity/source.
3. Force a failed/cancelled pool load and verify no new mark, HCNAMES update,
   or AutoSave generation is produced.

### Regression checks

- Existing scalar Scene/Kit/Instrument/MIDI mutation marking still drains.
- AutoSave OFF accepts the root load and HCNAMES update but produces no mask
  work; runtime re-enable follows the existing complete-resident convergence
  policy.
- No clean idle period produces a hidden-file write.
- All build modes retain zero logging-off allocation beyond the existing
  canonical mask; no trace format/size change occurs.
- `git diff --check` and a firmware build pass before hardware validation.

## Documentation closeout — completed

The references below and the Session 048 archive were reconciled after the
root-Instrument and InstrumentMrp hardware fixtures passed:

- `knowledge_files/specification_reference/AUTOSAVE.md` — admitted normal and
  Morph Instrument whole-object hooks, exact coverage, and writer-only limit;
- `knowledge_files/specification_reference/MODULE_INTERCHANGE_SPEC.md` — Menu
  completion classification, Preset commit ownership, and the no-mark
  temporary path;
- `knowledge_files/specification_reference/FILESYSTEM_SPEC.md` — HCNAMES
  remains independent from the immediate payload mutation mark; and
- `MEMORY.md` plus the session handoff — test evidence, whether multiple-load
  and temporary-restore fixtures passed, and any unresolved power-cut case.

Do not claim boot recovery from this change alone. It creates the parameter
payload that a later AutoSave reader can overlay; the HCNAMES source resolver
and the actual reader remain separate work. The durable implementation record,
hardware evidence, exclusions, and Session 049 handoff are in
`knowledge_files/log_archive/048_SESSION_HANDOFF_LOG.md`; this working plan is
safe to remove after that archive record is retained.
