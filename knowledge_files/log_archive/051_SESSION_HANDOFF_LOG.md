# Session 051 Handoff Log — Scene Names Follow-up and InstrumentMrp Kit Restore

**Project**: LXR-02 firmware port (STM32F765VIH6)
**Session goal**: Execute SCENE_FOLLOWUP_NAMES_POP_AUDIT.md: repair root Scene
embedded Kit/Instrument HCNAMES publication and add the reversible InstrumentMrp
kit row without adding persistent RAM.
**Working repository**: /Users/bc/Helicase Project/Helicase, branch
dev-ph3-autosave-ph2, intentional dirty worktree.

## End of session

```
DATE: 2026-08-17
SESSION GOAL: Repair Scene Load's embedded Kit/Instrument HCNAMES population and
  add a correct InstrumentMrp reversible kit row.
COMPLETED: Implemented both scoped items and then repaired a restore-source
  defect found by the first hardware pass. Root Scene Load now flushes its
  embedded Kit plus six Instrument HCNAMES rows at the physical Load/Save exit
  and at the Scene-to-Kit-family type boundary. InstrumentMrp now shows the
  slot's HCNAMES name beside kit, stores a Morph-only hidden snapshot, and
  restores only the Morphable Morph endpoint cells.
VERIFIED ON HARDWARE: Partially. The Scene Load fixture produced the correct
  .hcnames rows and AutoSave publication. The InstrumentMrp label was correct
  but the first restore was wrong; after the post-build repair the user
  re-tested and confirmed it works. The full Scene fixture matrix and Bank Load
  persistence remain outstanding.

CHANGES THIS SESSION:
- Core/Menu/menu.c: widened the physical exit predicate for a nonzero Scene
  dirty mask; added the Scene type-boundary flush; added InstrumentMrp
  Morph-only snapshot/restore sequencing and HCNAMES-backed kit rendering;
  cleared temporary-operation latches on failed hidden operations.
- Core/Hardware/SD/storageTypes.c/.h: added the Morph-only temporary Instrument
  projection (one parser anchor plus every Morphable [morph] endpoint).
- Core/Hardware/SD/filesystem.c/.h: added hidden Morph-temp save/load requests
  and the morph-temporary origin query.
- Core/Bank/Scene/Preset/presetManager.c/.h: added Morph-temp save/load wrappers
  and completion tags; added the Morph-to-Morph staged restore commit and
  origin dispatch in preset_startInstrumentMorphApply.
- knowledge_files/specification_reference/: AUTOSAVE.md, FILESYSTEM_SPEC.md,
  MODULE_INTERCHANGE_SPEC.md, SRAM_MANIFEST.md, and DEV_MODES.md reconciled.
- MEMORY.md and knowledge_files/log_archive/000_SESSION_INDEX.md updated.

KNOWN ISSUES INTRODUCED: None that remain. The first InstrumentMrp restore
  committed the staged normal image into the Morph endpoints; that was repaired
  in-session. Bank Load persistence remains deferred and is not a regression.
KNOWN ISSUES RESOLVED: Scene Load no longer leaves its embedded Kit/Instrument
  HCNAMES rows stale after exit; InstrumentMrp no longer has a blank kit row or
  a normal full-image restore.

NEXT SESSION RECOMMENDED GOAL: Bank Load persistence. See
  SESSION_052_PRE_PLAN.md: settings.cfg active_bank is stale after Bank Load and
  the AutoSave scene-present mask is captured as zero.
BLOCKERS: Hardware coverage for the remaining Scene fixtures; identification of
  the four initialized .data bytes added this session.

CRITICAL REMINDERS FOR NEXT SESSION:
- Keep filesystem_ack() after the final Scene/Bank index callback snapshots its
  result and before menu_finishLoadSaveCommand().
- Do not add a second HCNAMES writer or a SceneData/Menu name cache. The Scene
  fix reuses menu_endResidentNameScratchSession() and the operation-scoped
  identity block.
- InstrumentMrp must never replace type, Normal image, HCNAMES name/source,
  routing, or non-Morph cells. Its restore is Morph-to-Morph via the
  filesystem origin flag, not the normal-to-Morph pool path.
- The RAM policy still applies: total SRAM shrank, but four initialized .data
  bytes were added and must be identified.
```

---

## 1. Scope

This session implemented exactly two items from
SCENE_FOLLOWUP_NAMES_POP_AUDIT.md:

1. Root Scene Load must persist the loaded Scene's embedded Kit name and six
   Instrument names into the corresponding root /.hcnames rows.
2. InstrumentMrp must provide the same reversible kit affordance as normal
   Instrument Load, but restricted to the Morph endpoint domain.

Everything else identified during planning was out of scope, including Bank
Load persistence, recursive overwrite delete, runtime Bank Load active-Scene
preservation, and DSP debt.

---

## 2. Scene Load HCNAMES root cause and fix

### 2.1 Verified cause

The loader was never the problem. During root Scene Load,
filesystem_loadSceneDirectory_tick phase 24 publishes the embedded Kit name
into the operation-scoped nine-row identity block from
op_scene_child_display_name, and the six Instrument names from
op_kitset.instrument_file[]. It also stages per-destination sources: the Scene
row gets the root Scene slot number, Kit and Instrument rows get INHERIT.
Phase 72 then runs only the Scene-row HCNAMES writer, deliberately leaving the
Kit-family rows for the Menu exit.

The Menu completion branch for PRESET_OP_SCENE_LOAD accumulates the committed
destination mask through menu_refreshResidentNameScratchKit(), but the physical
exit predicate in menu_switchPage() only recognized Instrument, Kit, and KitMrp
sessions. A Scene session has menu_saveOptions.what equal to SAVE_TYPE_SCENE,
so menu_endResidentNameScratchSession() never ran and the accumulated mask was
dropped.

### 2.2 Change 1 — exit predicate

menu_switchPage() now admits a nonzero menu_residentNameDirtySceneMask:

    end_resident_name_session =
        (menu_activePage == LOAD_PAGE || menu_activePage == SAVE_PAGE) &&
        (menu_instrumentLoadActive ||
         menu_saveOptions.what == SAVE_TYPE_KIT ||
         menu_saveOptions.what == SAVE_TYPE_KIT_MORPH ||
         menu_residentNameDirtySceneMask != 0u) &&
        pageNr != LOAD_PAGE;

This is safe because menu_endResidentNameScratchSession() already no-ops when
both the scratch and the dirty mask are clear, and it dispatches the existing
filesystem_requestUpdateResidentKitNames() writer whenever the mask is nonzero.
The deferred-exit path re-enters menu_switchPage() through
menu_processPendingPageSwitch(), so busy exits also flush exactly once.

### 2.3 Change 2 — Scene-to-Kit-family type boundary

The type-change branch in menu_parseEncoder() now flushes a pending Scene mask
before a later normal Kit Load can overwrite the operation-scoped identity
block. The reachable path was Scene -> KitMrp -> Kit. The condition is:

    previous_type == SAVE_TYPE_SCENE &&
    menu_saveOptions.what != previous_type &&
    menu_residentNameDirtySceneMask != 0u

It mirrors the existing Kit-to-non-Kit boundary and reuses the same writer.

### 2.4 What did not need to change

The filesystem identity publication, the root Scene Scene-row writer, the
deferred Kit-family writer, and the failure-path guard in
menu_pollPresetStatus() were all already correct. Bank Load has its own
per-child overlay and phase-86 register write and does not accumulate a Menu
mask, so it is not double-flushed.

---

## 3. InstrumentMrp kit row implementation and repair

### 3.1 Implementation

Normal Instrument Load writes the entry voice to the hidden
Instrument/<type>/.hctmp.<ext> and retains the nine-byte kit label. InstrumentMrp
was skipping that step entirely, leaving the kit row blank and with nothing to
restore.

The session added:

- menu_prepareInstrumentLoadTemp() morph branch calls
  preset_saveInstrumentMorphTemp() and stores only a Morph-only baseline.
- The kit label is rendered from menu_instrumentSaveName (the HCNAMES identity
  row) in morph mode.
- menu_restoreInstrumentLoadTemp() morph branch posts
  preset_loadInstrumentMorphTemp().
- storageTypes gained STORAGE_INSTRUMENT_SAVE_MORPH_SNAPSHOT: the hidden file
  keeps one ordinary [params] parser anchor and streams every Morphable [morph]
  endpoint. The [params] anchor exists only because the instrument finalizer
  requires at least one primary value.
- filesystem gained hidden Morph-temp save/load requests and the
  filesystem_loadedInstrumentWasMorphTemporary() origin query.

### 3.2 Hardware defect and repair

The first hardware pass showed the label populated correctly but the restore
wrong: scrolling to a pool instrument and back to kit left the Morph endpoints
at the previewed values. Source inspection found the cause.

preset_startInstrumentMorphApply() always called
preset_commitStagedInstrumentNormalToMorph(), which copies the staged normal
image into the resident Morph endpoints. That is correct for a pool
InstrumentMrp load, but the hidden snapshot stores its meaningful payload in
the [morph] section. The restore therefore copied the entry Normal values and
discarded the captured entry endpoints.

Repair in presetManager.c:

- preset_copyInstrumentMorphToMorphIfSameType() copies only Morphable descriptor
  cells from the staged Morph image into the resident Morph image on a type
  match.
- preset_commitStagedInstrumentMorphToMorph() applies the same staged type and
  request-type gate.
- preset_startInstrumentMorphApply() selects the Morph-to-Morph commit when
  filesystem_loadedInstrumentWasMorphTemporary() is set, and keeps the
  normal-to-Morph commit for ordinary pool loads.

The repair adds no state or RAM and reuses the existing hidden-file transport,
Morph worker, and Morph-only AutoSave marker. The user re-tested the repaired
image and confirmed the kit restore works.

---

## 4. Build and RAM

Logging-on build before the repair: text=377,788, data=400, bss=95,176.
Logging-on build after the repair: text=377,956, data=400, bss=95,176.
make img produced build/LXRV2_lxr02.img at 378,356 bytes.

A separate logging-off build was produced before the repair at text=369,364,
data=400, bss=78,684 and was not re-run after the repair; the repair only adds
static code paths and changes no RAM.

Compared with the Session 050 snapshot, text grew, initialized .data grew from
396 to 400 bytes, and bss shrank from 95,188 to 95,176 bytes (net -8 bytes RAM).
The four initialized .data bytes must still be identified under the RAM
allocation policy.

---

## 5. Hardware evidence

### 5.1 Scene Load HCNAMES and AutoSave

Loading root Scene 015 Machine into resident Scene 15, then exiting Load,
produced:

- /.hcnames Scene row 15: Machine 015.
- /.hcnames Kit row 15: Machine with source INHERIT.
- /.hcnames Instrument rows 123..128: machind1, machind2, machind3, machins1,
  machinc1, machinh1 with source INHERIT.
- A complete AutoSave lifecycle trace with R/D/I/L/F/W and A/V/M/C/P/T.
- .hcprms2 advanced to generation 2 with 499 captured bytes (three Bank bytes
  plus the 496-byte Scene 15 scope).
- The winning record's Scene 15 scene-parameter bytes and all six Instrument
  type/normal cells matched the Scene 015 source files byte-for-byte.

### 5.2 InstrumentMrp kit

The first test confirmed the kit label and exposed the restore defect. After the
Morph-to-Morph repair, the user re-tested and confirmed the restore works.

---

## 6. Remaining work

- Complete the Scene fixture matrix: multi-destination Scene Load, deferred and
  toggle exits, the Scene -> KitMrp -> Kit hazard, and failed-load preservation.
- Bank Load persistence. See SESSION_052_PRE_PLAN.md: settings.cfg active_bank
  is stale after Bank Load and the AutoSave scene-present mask is captured as
  zero.
- Identify the four initialized .data bytes added this session.

---

## 7. Documentation close-out

The disposable planning files SCENE_FOLLOWUP_NAMES_POP_AUDIT.md and
051_SCENE_NAMES_IMPLEMENTATION_LOG.md are superseded by this handoff and the
specification reference set:

- AUTOSAVE.md: InstrumentMrp restore marks only Morphable Morph cells.
- FILESYSTEM_SPEC.md: repaired Scene HCNAMES exit contract and the
  InstrumentMrp reversible row.
- MODULE_INTERCHANGE_SPEC.md: new Morph-temp Preset/filesystem APIs.
- SRAM_MANIFEST.md: Session 051 linked totals and the .data identification
  caveat.
- DEV_MODES.md: build description refreshed to the Session 051 build.
