# Session 051 Worklog — Scene Names Implementation

**Project**: LXR-02 firmware port (STM32F765VIH6)  
**Session goal**: Execute `SCENE_FOLLOWUP_NAMES_POP_AUDIT.md`: repair root
Scene embedded Kit/Instrument HCNAMES publication and add the reversible
InstrumentMrp `kit` row without adding persistent RAM.  
**Status**: Implementation and build work complete; hardware fixture
verification and session closeout are still pending.

## Work completed so far

Implemented both scoped Scene-follow-up audit items. Root Scene dirty masks now
flush at the physical Load/Save exit boundary, and flush before a later
Kit-family type can overwrite the operation-scoped identity block. The existing
identity publication and HCNAMES writer were reused unchanged.

InstrumentMrp now displays the selected slot's HCNAMES name beside `kit`,
stores a Morph-only hidden snapshot through the existing `.hctmp.<ext>`
transport, and restores only same-type Morphable Morph endpoint cells through
the existing Morph commit/apply/AutoSave path. No persistent RAM was added.

### Source changes

- `Core/Menu/menu.c`: widened the physical exit predicate for a nonzero Scene
  dirty mask; added the Scene type-boundary flush; added InstrumentMrp
  Morph-only snapshot/restore sequencing and HCNAMES-backed `kit` rendering;
  cleared temporary-operation latches on failed hidden operations.
- `Core/Hardware/SD/storageTypes.c/.h`: added a bounded Morph-only temporary
  Instrument projection containing one parser anchor and all Morphable
  `[morph]` endpoint cells.
- `Core/Hardware/SD/filesystem.c/.h`: added hidden Morph-temp save/load requests
  and an operation-origin query; reused the existing typed stage and file
  operation.
- `Core/Bank/Scene/Preset/presetManager.c/.h`: added Morph-temp save/load
  wrappers and completion tags that route the staged snapshot through the
  existing same-type Morph commit.
- `knowledge_files/specification_reference/FILESYSTEM_SPEC.md`: documented the
  repaired Scene HCNAMES exit contract and InstrumentMrp reversible row.
- `knowledge_files/specification_reference/AUTOSAVE.md`: documented that the
  Morph-only restore remains limited to Morphable Morph cells.
- `MEMORY.md` and `knowledge_files/log_archive/000_SESSION_INDEX.md`:
  reconciled Session 051 context and index.

## Build verification

- Logging-on `make -j2` passed: `text=377,788`, `data=400`, `bss=95,176`.
- Separate logging-off build in `build_off` passed:
  `text=369,364`, `data=400`, `bss=78,684`.
- The logging-off build was compiled with `config.h` temporarily set to 0 and
  the tracked configuration was restored to logging 1.
- No SRAM manifest update is required because no RAM allocation moved.
- `make img` generated the image at `build/LXRV2_lxr02.img`.

## Post-build repair — InstrumentMrp kit restore source

The first hardware pass showed the InstrumentMrp kit label populated correctly
while its restore was wrong: scrolling to a pool instrument and back to kit
left the Morph endpoints at the previewed values instead of the entry values.
Source inspection found the cause: preset_startInstrumentMorphApply() always
committed the staged normal image
(preset_commitStagedInstrumentNormalToMorph), but the hidden snapshot stores
its meaningful payload in the [morph] section. The restore therefore copied
the entry Normal values into the resident Morph endpoints and discarded the
captured entry endpoints.

Repair in Core/Bank/Scene/Preset/presetManager.c:

- added preset_copyInstrumentMorphToMorphIfSameType(), which copies only
  Morphable descriptor cells from the staged Morph image into the resident
  Morph image when the types match;
- added preset_commitStagedInstrumentMorphToMorph(), the same type/request
  gate plus that copy;
- preset_startInstrumentMorphApply() now selects the Morph-to-Morph commit
  when filesystem_loadedInstrumentWasMorphTemporary() is set and keeps the
  normal-to-Morph commit for ordinary pool InstrumentMrp loads.

The repair adds no state or RAM and reuses the existing hidden-file transport,
the existing Morph worker, and the existing Morph-only AutoSave marker.
Logging-on rebuild passed: text=377,956, data=400, bss=95,176.

## Current SD-card trace audit

The current binary trace is `SD_CARD/asavetrc.bin`; the text traces previously
opened under `.Trash/logs` are older copies and are not the current card trace.

- `SD_CARD/.hcnames` contains 129 valid rows.
- Scene 15 and Kit 15 are registered as `Machine`.
- Scene 15 instruments are registered as `machind1`, `machind2`, `machind3`,
  `machins1`, `machinc1`, and `machinh1`.
- The trace records Scene 15 publication with six instrument dirty records;
  each expected/published byte count matches (`76/76`, `76/76`, `76/76`,
  `74/74`, `76/76`, `76/76`).
- The extra playback edit appears after the first load completion at tick
  `42927`, followed by a complete AutoSave cycle: capture, promotion to
  `.hcprms2` generation 2, and terminal `DONE`.
- No dropped trace records, errors, or failed operations were found.
- Both `.hcprms` records have valid CRCs and cleared dirty masks.

The reserved identity/name fields in `.hcprms2` still contain the previous
`Pop` values. This is expected under the current contract: `.hcnames` is
authoritative for names, while AutoSave does not mark those name fields dirty.
The live type/endpoints and the HCNAMES register are consistent with the
Scene 15 `Machine` load.

## Known issues and remaining work

Hardware behavior is not yet confirmed. The Morph-only temporary container uses
one ordinary `[params]` parser anchor because the current instrument finalizer
requires at least one primary value. After the post-build repair the restore
commit copies only the captured Morphable Morph endpoint cells from the staged
`[morph]` payload and ignores that anchor.

The hardware fixture matrix remains outstanding:

1. Flash the logging-on image.
2. Test one Scene Load into a destination whose Kit and six Instrument names
   all differ.
3. Test Scene single/multi-destination, deferred/toggle exit, and the
   Scene→KitMrp→Kit overwrite hazard.
4. Test failed-load preservation.
5. Test InstrumentMrp `kit` with changed Morph endpoints, including audible
   restore and AutoSave-mask checks. Re-run this fixture against the repaired
   image: enter Mrp, load a pool instrument, return to `kit`, and confirm the
   endpoints return to their entry values while type/Normal/name/source stay
   unchanged.
6. Capture the card-copy, exact row/source, and trace evidence before closing
   Session 051.

## Critical implementation reminders

- Keep the physical Scene/Bank terminal `filesystem_ack()` placement after
  capturing `index_ok` and before command teardown.
- Do not add a second HCNAMES writer or a SceneData/Menu name cache. The Scene
  fix reuses `menu_endResidentNameScratchSession()` and the existing identity
  block.
- InstrumentMrp must never replace type, Normal image, HCNAMES name/source,
  routing, or non-Morph cells. Its temporary file is a Morph-only baseline, not
  the normal Instrument Load snapshot.
- SRAM1/DTCM free capacity remains reserved under `MEMORY.md` policy; no new
  persistent allocation was approved or introduced in this session.
