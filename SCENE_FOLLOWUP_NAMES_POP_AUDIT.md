# Scene Follow-up: Names Population Audit

**Session**: 051
**Branch/state**: dev-ph3-autosave-ph2, clean worktree at f614563
**Scope**: exactly two items. Everything else identified in the prior
investigation is intentionally out of scope for this session.

1. **Main** — Scene Load does not persist the loaded Scene's embedded Kit and
   six Instrument names into root /.hcnames.
2. **Side** — InstrumentMrp (...Mrp / Morph Load) has no reversible kit row:
   the label is blank and there is no Morph-only snapshot to restore.

This revision was re-derived from the current source, not from handoffs or
specifications. Section 3 is the complete implementation schema for the main
issue; the comment blocks there are written to be placed in the code verbatim.

---

## 1. Verified root-cause chain (main issue)

The defect is a Menu exit-boundary bookkeeping gap. The loader, the identity
block, and the deferred HCNAMES writer all already do their part.

### 1.1 What a root Scene Load commits

preset_loadSceneForScenes() captures the destination mask into
pm_kit_request_scene_mask and the filesystem Scene loader
(filesystem_loadSceneDirectory_tick) does the following:

1. Embedded-kit parse completion (phase 24, filesystem.c) publishes the loaded
   Kit and Instrument names into the single operation-scoped nine-row identity
   block:

       filesystem_setIdentityName(FS_IDENTITY_KIT_ROW,
                                  op_scene_child_display_name);
       filesystem_setIdentityName(FS_IDENTITY_INSTRUMENT_ROW_0 + slot,
                                  op_kitset.instrument_file[slot]);

   It also stages each destination's paired HCNAMES sources (Scene row gets the
   root Scene slot number op_slot; Kit and Instrument rows get INHERIT) through
   filesystem_setResidentSource().

2. At phase 72, a successful root Scene Load transitions into
   FS_INTERNAL_OP_UPDATE_HCNAMES_SCENE, which rewrites ONLY the destination
   Scene rows. The Kit and six Instrument rows are intentionally left for the
   Menu-side deferred flush.

3. The Preset callback completes PRESET_OP_SCENE_LOAD and the Menu completion
   branch (menu.c) runs:

       menu_refreshResidentNameScratchKit(preset_getKitRequestSceneMask());

   which ORs the committed destination bits into
   menu_residentNameDirtySceneMask.

### 1.2 Where the chain breaks

menu_switchPage() computes its exit predicate as:

    end_resident_name_session =
        (menu_activePage == LOAD_PAGE || menu_activePage == SAVE_PAGE) &&
        (menu_instrumentLoadActive ||
         menu_saveOptions.what == SAVE_TYPE_KIT ||
         menu_saveOptions.what == SAVE_TYPE_KIT_MORPH) &&
        pageNr != LOAD_PAGE;

During a Scene session menu_saveOptions.what is SAVE_TYPE_SCENE and
menu_instrumentLoadActive is zero, so the predicate is false and
menu_endResidentNameScratchSession() never runs. The accumulated mask is
dropped and filesystem_requestUpdateResidentKitNames() is never requested.
Hardware result: the Scene row changes; the Kit row and six Instrument rows of
that resident Scene keep their previous names.

### 1.3 Corrective facts verified in the source

- menu_endResidentNameScratchSession() flushes whenever the dirty mask is
  nonzero, independent of menu_residentNameScratchValid. A Scene session may
  legitimately have an invalid scratch, so widening the predicate is safe.
- The deferred-exit path reenters menu_switchPage() through
  menu_processPendingPageSwitch() after storage and Preset become idle, so the
  same widened predicate covers exits deferred by busy work.
- menu_residentNameScratchFlushComplete() already snapshots and acknowledges
  the filesystem facade, clears the mask and scratch exactly once on success,
  and skips browser reentry when the page has already switched away.
- The failure path never accumulates the mask: menu_pollPresetStatus() returns
  with an error overlay before the completion switch whenever
  preset_getCompletedOk() is false, so a failed Scene Load cannot trigger a
  flush of stale identity data.
- The updater serializes the CURRENT nine-row identity block, not SceneData
  fields. Correct for one Scene Load, including a multi-destination load,
  because every destination received the same payload and therefore the same
  embedded Kit/Instrument names.
- Bank Load needs no change: the Bank loader consumes the identity block per
  child at commit (filesystem_cacheCurrentBankSceneNameBlock) and writes its
  own complete HCNAMES register at Bank phase 86 before finishing. The Bank
  completion branch in Menu does not accumulate the mask at all.
- One real hazard exists and requires the second change below: leaving Scene
  for KitMrp (and then Kit) carries a pending Scene mask past the point where a
  later normal Kit Load overwrites the identity block, which would then
  serialize the wrong Kit identity for the Scene-loaded destinations at exit.

---

## 2. Fixed HCNAMES row contract (do not change)

Root /.hcnames is the authoritative identity and provenance register. Its row
math is centralized in filesystem.c and must remain the single source
(zero-based rows):

- row 0: Bank
- rows 1..16: Scene 0..15
- rows 17..32: Kit 0..15
- rows 33..128: six Instruments per Scene, 33 + scene*6 + slot

filesystem_requestUpdateResidentKitNames(scene_mask, cb) reads the full
variable-length register, replaces exactly one Kit row plus six Instrument rows
per selected Scene from the identity block, preserves every other row, and
rewrites through the normal close/flush gate. It allocates no persistent SRAM.

Constraints:

- Do not add a second HCNAMES writer or a Scene-load side-path flush; both
  changes below reuse menu_endResidentNameScratchSession().
- Keep the filesystem_ack() placement in the final Scene/Bank index callback.
- The INHERIT (-) source token remains valid; no source-token redesign.
- A changed Scene row is not proof that Kit/Instrument rows are registered.

---

## 3. Main issue — complete implementation schema

Exactly two source locations change, both in Core/Menu/menu.c. No filesystem,
Preset, or header change is required, and no new state or RAM is added.

### 3.1 Change 1 — admit Scene sessions at the physical exit boundary

Location: menu_switchPage(), the end_resident_name_session computation that
currently reads

    end_resident_name_session = (uint8_t)(
        (menu_activePage == LOAD_PAGE || menu_activePage == SAVE_PAGE) &&
        (menu_instrumentLoadActive ||
         menu_saveOptions.what == SAVE_TYPE_KIT ||
         menu_saveOptions.what == SAVE_TYPE_KIT_MORPH) &&
        pageNr != LOAD_PAGE);

becomes

    end_resident_name_session = (uint8_t)(
        (menu_activePage == LOAD_PAGE || menu_activePage == SAVE_PAGE) &&
        (menu_instrumentLoadActive ||
         menu_saveOptions.what == SAVE_TYPE_KIT ||
         menu_saveOptions.what == SAVE_TYPE_KIT_MORPH ||
         menu_residentNameDirtySceneMask != 0u) &&
        pageNr != LOAD_PAGE);

Replacement comment block for the existing "Capture the old context" comment
directly above this assignment:

    /*
     * Capture the old context before page mutation. Pressing the Load/Save
     * mode button toggles LOAD_PAGE/SAVE_PAGE through pageNr==LOAD_PAGE and
     * keeps the shared name session. Any other page target is the physical
     * exit boundary that must flush accumulated resident names once.
     *
     * What: the predicate now also admits a root Scene Load session. Its
     * completion accumulated menu_residentNameDirtySceneMask, but its
     * menu_saveOptions.what is SAVE_TYPE_SCENE, so the Kit-family type checks
     * alone never matched and the mask was silently dropped on exit.
     * Why: a nonzero mask means committed Scene payloads replaced resident
     * Kit and Instrument identities that still need their one deferred HCNAMES
     * rewrite; without this clause the Scene row changes while its Kit row
     * and six Instrument rows stay stale.
     * Inputs: the pre-switch menu_activePage, menu_saveOptions, and
     * menu_residentNameDirtySceneMask values, evaluated before any page
     * mutation below.
     * Outputs: end_resident_name_session, which posts exactly one
     * menu_endResidentNameScratchSession() call after the page mutation. The
     * deferred busy exit reenters here through menu_processPendingPageSwitch()
     * and sees the same condition.
     * Affiliates: menu_endResidentNameScratchSession(),
     * filesystem_requestUpdateResidentKitNames(),
     * menu_residentNameScratchFlushComplete(), and
     * menu_processPendingPageSwitch().
     */

The mask clause is safe to add because menu_endResidentNameScratchSession()
no-ops when both the scratch and the mask are clear, and dispatches the
existing writer whenever the mask is nonzero.

### 3.2 Change 2 — end a Scene name session at the type boundary

Location: menu_parseEncoder(), the SAVE_STATE_EDIT_TYPE branch. After the
existing Kit-family boundary condition and before the cache-clear line, add:

                if (previous_type == SAVE_TYPE_SCENE &&
                    menu_saveOptions.what != previous_type &&
                    menu_residentNameDirtySceneMask != 0u &&
                    menu_endResidentNameScratchSession()) {
                    menu_resetLoadSaveSceneSelection();
                    menu_refreshLoadSceneLeds();
                    break;
                }

Comment block to be placed directly above this new condition:

                /*
                 * End a Scene name session before any later Kit-family payload
                 * can overwrite the operation-scoped identity block.
                 *
                 * What: flush a nonzero accumulated Scene dirty mask when the
                 * type row leaves SAVE_TYPE_SCENE, mirroring the Kit-family
                 * boundary condition above. The new type is installed first so
                 * an asynchronous flush completion continues directly into that
                 * type's browser.
                 * Why: filesystem publishes each committed Scene's embedded Kit
                 * name and six Instrument names into the single nine-row
                 * identity block, and the deferred exit writer later serializes
                 * that block into every Scene bit in
                 * menu_residentNameDirtySceneMask. A subsequent normal Kit Load
                 * overwrites the same block, so carrying Scene bits past this
                 * boundary (reachable via Scene -> KitMrp -> Kit) could publish
                 * the later Kit's identity for the Scene-loaded destinations.
                 * Inputs: previous_type captured before the type advance, the
                 * newly installed menu_saveOptions.what, and the accumulated
                 * mask.
                 * Outputs: at most one HCNAMES rewrite through
                 * filesystem_requestUpdateResidentKitNames(); on success
                 * menu_residentNameScratchFlushComplete() clears the mask and
                 * the completion resumes the new type's browser.
                 * Affiliates: menu_endResidentNameScratchSession(),
                 * menu_residentNameScratchFlushComplete(), and the equivalent
                 * Kit-family boundary condition directly below.
                 */

### 3.3 Explicitly unchanged paths (verified, do not modify)

- filesystem_loadSceneDirectory_tick() identity publication at phase 24 stays.
- The root Scene Load transition to FS_INTERNAL_OP_UPDATE_HCNAMES_SCENE stays;
  it owns only the Scene rows by design.
- filesystem_requestUpdateResidentKitNames() and
  filesystem_cacheCurrentResidentKitNames() stay; they already serialize the
  identity block into every masked Scene's Kit and six Instrument rows.
- Bank Load stays untouched: it has its own per-child overlay and phase-86
  register write, and does not accumulate a Menu mask.
- The failure-path guard in menu_pollPresetStatus() stays; it already prevents
  mask accumulation on failed Scene Loads.
- No new RAM: both changes only add predicate conditions over existing state.

### 3.4 Build and RAM check

Rebuild logging-on and logging-off and compare section totals against
SRAM_MANIFEST.md. Expected delta: zero bytes.

---

## 4. Main issue — test plan

Primary objective: after a root Scene Load and Load/Save exit, the destination
Scene's Kit row and all six Instrument rows match the loaded Scene's embedded
identity, and every unrelated row is preserved.

### 4.1 Fixture requirements

- A root Scene whose embedded Kit name and six Instrument names all differ
  from the destination's current rows.
- Record the destination before rows (Scene row 1..16, Kit row 17..32,
  Instrument rows 33..128) and the source's expected names.

### 4.2 Core scenarios

1. Single-destination Scene Load into the active Scene, exit to Voice, copy
   the card. Verify Scene row, Kit row, and six Instrument rows all match the
   source, with the Scene row carrying the numeric slot source token and the
   Kit/Instrument rows INHERIT.
2. Multi-destination Scene Load (toggle SEQ targets before OK). Every
   destination must receive the same source Kit and Instrument rows.
3. Deferred exit: press the mode button while the command/index work is still
   busy. The queued exit must still flush exactly once.
4. Load/Save toggle before exit: Scene Load, toggle to Save, then exit. The
   mask must survive the toggle and flush once at the physical exit.
5. Type-boundary hazard (Change 2): Scene Load, change type to KitMrp, then to
   Kit, load a different Kit into another Scene, exit. The Scene-loaded
   destination must keep the Scene's Kit/Instrument names; only the Kit-load
   destination may change.
6. Failed Scene Load (bad slot or absent file): error overlay, no flush, and
   the destination Kit/Instrument rows must remain byte-for-byte unchanged.

### 4.3 Regression checks

- Normal Kit Load still publishes exactly one Kit row plus six Instrument rows
  for its mask.
- KitMrp still preserves identity and publishes nothing.
- Instrument Load and InstrumentMrp exit paths are unchanged.
- Bank Load still publishes through its own writer; its rows must not be
  double-flushed by the Menu exit.
- Boot Scene Load still results in a consistent /.hcnames after the next
  Load/Save exit.
- Unrelated Scene/Kit/Instrument rows are unchanged in every scenario.

### 4.4 Trace/AutoSave confirmation

- The existing A/V/M/C/P/T transaction still completes after the exit flush;
  the flush must release the facade so the AutoSave writer can run.
- Keep the temporary 2,048-record trace ring for diagnosis.

---

## 5. Side issue — InstrumentMrp kit restore slot

### 5.1 Objective

Give InstrumentMrp the same reversible kit behavior as normal Instrument Load,
but restricted to the Morph endpoint domain:

- show the selected slot's current HCNAMES instrument name beside kit;
- snapshot the slot's current Morph endpoint values on entry;
- selecting kit restores only the Morphable Morph endpoint cells.

It must never change the slot's type, Normal parameter image, HCNAMES name, or
HCNAMES source.

### 5.2 Key distinction from normal restore

Normal restore (preset_loadInstrumentTemp() -> on_instrument_load_complete())
applies a full instrument image and is owned by type/Normal/name semantics.
InstrumentMrp must instead mirror the same-type, Morphable-endpoint-only commit
used by preset_loadInstrumentMorph().

Relevant storage:

- Normal image: instrument->parameter_images.normal_instrument_parameters[]
- Morph endpoint image: instrument->parameter_images.morph_instrument_parameters[]
- Morphable mask: INSTRUMENT_PARAM_FLAG_MORPHABLE

### 5.3 Open decision — snapshot location

Two options, resolved during implementation:

1. Reuse an existing staging buffer or the on-card .hctmp mechanism with a
   Morph-only projection.
2. Add a small Menu-owned Morph-endpoint snapshot buffer.

Preference: avoid new persistent RAM. If option 2 is required, it must state the
exact byte count, region, lifetime, and owner and go through the RAM allocation
approval policy before implementation.

### 5.4 Implementation outline

- On InstrumentMrp entry, populate the kit display name from the existing HCNAMES
  identity row (menu_instrumentSaveName) without performing a normal
  full-instrument .hctmp save.
- Capture only the current slot's Morphable Morph endpoint cells.
- Route kit selection to a Morph-only restore that writes only the captured
  Morphable cells and then runs the existing Morph interpolation/apply so the
  result is audible.
- Invalidate the snapshot on the same boundaries that invalidate the normal
  temporary image (type change, voice change, Scene change, mode/exit).

### 5.5 Test plan

1. Enter InstrumentMrp for a slot whose type has morphable parameters.
2. Verify the kit row shows the slot's current HCNAMES name.
3. Change one or more Morph endpoint values.
4. Select kit and verify:
   - Morphable Morph endpoint values return to their entry values;
   - type, Normal image, HCNAMES name, and source are unchanged;
   - the audible Morph result reflects the restore.
5. Confirm AutoSave marks only the restored Morphable Morph endpoints, not type
   or Normal cells.
6. Confirm normal Instrument Load kit behavior is unaffected.

---

## 6. Session boundaries and close-out

Out of scope this session:

- Bank Load persistence/restore regression
- recursive delete for overwrite Save
- runtime Bank Load active-Scene preservation
- dth wiring, 24-bit voice widening, and other DSP debt
- trace-ring revert and developer-log converter

Close-out requires:

- hardware evidence for the main-issue scenarios above and the InstrumentMrp
  kit restore;
- an updated Session 051 handoff log;
- updates to FILESYSTEM_SPEC.md and AUTOSAVE.md only for behavior actually
  changed, plus SRAM_MANIFEST.md if RAM moves;
- MEMORY.md volatile notes reconciled to the final state.
