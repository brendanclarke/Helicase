# Session 032 Handoff Log

**Date**: 2026-07-10  
**Source at end**: local working directory, branch `dev-burst-reduction`  
**Theme**: Instrument parameter refactor follow-up: descriptor voice pages,
Scene-backed kit load/readthrough, runtime DSP propagation repair, and durable
spec consolidation.

---

## Starting Point

Session 031 left the directory-kit format usable and added `SHIFT+VOICE` morph
endpoint edit mode, but the new instrument parameter storage was not yet fully
visible or audible:

- The system booted and the menu was navigable.
- VOICE pages were intentionally blank after the first instrument refactor.
- The sequencer triggered voices and LEDs lit, but no sound was produced.
- It was unclear whether directory kits were failing to populate new instrument
  storage or whether the DSP was failing to read it.
- The new instrument helper structures and functions needed in-place contract
  comments.

Root planning/audit files used during this session:

- `INSTRUMENT_PARAM_REFACTOR.md`
- `INSTRUMENT_PARAM_REFACTOR_FOLLOWUP.md`
- `INSTRUMENT_PARAM_REFACTOR_FOLLOWUP_PATCHING.md`
- `KIT_DIR_LOAD_AUDIT.md`
- `KIT_INSTRUMENT_SPEC_FOLLOWUP_AUDIT.md`

Those files were consolidated into the durable specs/logs in this session so
they can be deleted later.

---

## Instrument Menu Pages

VOICE menu pages are now descriptor-backed instead of statically enumerated in
`menuPages.h`.

Implemented model:

- Each instrument parameter file owns its parameter text and menu layout:
  - `Core/DSP/Instruments/Drum/DrumParameters.c`
  - `Core/DSP/Instruments/Snare/SnareParameters.c`
  - `Core/DSP/Instruments/Cymbal/CymbalParameters.c`
  - `Core/DSP/Instruments/HiHat/HiHatParameters.c`
- `InstrumentManager.c` stores the registry-level outline and exposes lookup
  helpers for menu code.
- The active VOICE page resolves against the active Scene kit slot and that
  slot's instrument type.
- VOICE7 maps to the hihat/open layout where appropriate.
- Instrument layouts were checked against `menuPages.old` so Drum, Snare,
  Cymbal, and HiHat parameter positions match the old page/subpage intent.

Important shape:

- `instrument_menu_page_t` holds descriptor indexes for the 4x2 LCD grid.
- `INSTRUMENT_MENU_EMPTY` marks blank cells.
- Each instrument file now defines local enum names immediately above its
  descriptor array so layouts read as names instead of raw indexes:
  `DRUM_OSC1_WAVE` rather than `0u`, etc.
- The layout arrays use those enum names, preserving the compact table shape
  while keeping review/debug readable.

`menu.c` changes:

- Added a dynamic cell resolver so static pages and descriptor-backed instrument
  cells can share repaint/edit paths.
- Descriptor-backed display pulls short/long labels and dtype from the active
  `ParamDescriptor`.
- Descriptor-backed edits write through Preset/Scene helpers instead of raw
  `parameter_values[]`.
- Target-display helpers can show descriptor names for instrument target cells
  instead of only legacy `modTargets[]` strings.

---

## Scene Storage and Kit Loading

Directory kits now populate descriptor-indexed Scene storage.

Relevant storage decisions:

- `kitset.kcg` owns slot membership, instrument filename/type, and per-slot
  audio routing.
- Instrument files own per-instrument sound values in `[params]` and optional
  `[morph]`.
- Missing `[morph]` remains valid; the loader copies main values into the morph
  image for morphable descriptors.
- Unknown instrument keys are skipped for forward compatibility.
- Known descriptor target cells `velo_mod_dest` and `lfo_target_param` parse as
  `uint16_t`; normal descriptor values parse as `uint8_t`.

Slak-specific load audit:

- `SD_CARD/Kit/001 Slak/kitset.kcg` names all six instrument files.
- All referenced instrument files are present.
- The long hihat keys `amp_envelope_decay_closed` and
  `amp_envelope_decay_open` are 25 bytes and exceeded the old parser key
  buffer.
- `Core/Hardware/SD/storageTypes.c` now uses
  `STORAGE_INSTRUMENT_KEY_MAX=32u`, which is long enough for current keys plus
  terminator and gives a little forward margin.

Result:

- Hardware report after the runtime fix: the system mostly works, the menu is
  populated, and sequenced voices can produce sound.

---

## Descriptor Flags and Row Macros

Session 032 clarified that descriptor flags live in the descriptor rows in each
instrument parameter file, not in a global static parameter list.

Flag meaning:

- `INSTRUMENT_PARAM_FLAG_MORPHABLE`: the descriptor owns main and morph endpoint
  image values and is eligible for morph interpolation.
- `INSTRUMENT_PARAM_FLAG_MODULATABLE`: the descriptor may appear in velocity/LFO
  target selection.
- `INSTRUMENT_PARAM_FLAG_AUTOMATABLE`: the descriptor may appear in step
  automation target selection.

Macro model:

- `ROW` describes normal image parameters with a runtime member bind.
- `ROW_MENU` is a normal image parameter with menu/display text differences.
- `ROW_NOBIND` describes supplemental single-endpoint values that are not
  morphable/modulatable/automatable.
- `ROW_NOBIND_IMAGE` describes image parameters that are
  morphable/modulatable/automatable but cannot be written through a simple
  instrument-struct member offset.
- Target selector rows remain `ROW_NOBIND`.

Code-adjacent comments were rewritten so each explanatory block sits directly
above the macro or structure it describes. This was done in Drum, Snare, Cymbal,
and HiHat parameter files.

Two descriptors were changed to `ROW_NOBIND_IMAGE`:

- `instrument_decimation`
- `velo_mod_amount`

Rationale:

- Both need image semantics for Morph, LFO target selection, and step
  automation selection.
- Neither has a simple direct member-offset bind that the generic runtime writer
  can safely use.
- Runtime application for those values must go through explicit
  `InstrumentManager` handling.

---

## Runtime DSP Propagation

`InstrumentManager` is now the descriptor registry and runtime apply bridge for
instrument parameters.

Implemented responsibilities:

- Resolve instrument type and descriptor index.
- Resolve descriptor-backed menu layouts.
- Convert stored byte values through descriptor shapers.
- Write direct-bound parameters through the generic runtime writer when a
  descriptor has a valid byte offset/type binding.
- Apply special runtime setters for parameters identified by `descriptor->file_key`
  when a plain offset write is not enough.

This repaired the immediate "sequencer triggers LEDs but no sound" failure: the
loaded Scene instrument values now have a path back into the DSP runtime.

Known runtime limitations:

- Descriptor target storage/display exists, but runtime modulation is not fully
  descriptor-aware.
- `ModulationNode` and `AutomationNode` still lean on legacy/static parameter
  IDs and `ParameterArray` semantics.
- Descriptor-backed LFO/velocity target assignments probably store enough data
  to display intent, but they do not yet reliably modulate the DSP.

---

## Morph Status

Morph was not fixed by this session.

Hardware report after the pass:

- Normal boot/menu/audio mostly works.
- Morph does not work.
- LFO assignments and probably the rest of automation assignments do not work.

Likely reason:

- Session 031's morph endpoint edit mode originally used `parameters2[]` for
  static VOICE page parameters.
- Session 032 moved instrument values into Scene descriptor images.
- The display/edit side now understands descriptor images, but the full morph
  worker/runtime path still needs to be audited so interpolation reads the
  descriptor main/morph images, applies descriptor shapers, and reaches DSP
  runtime state.

Next session should start with descriptor Morph before changing unrelated
instrument storage.

---

## Spec Consolidation

Created:

- `knowledge_files/specification_reference/`
- `knowledge_files/specification_reference/INSTRUMENT_FILE_SPEC.md`

Moved into `specification_reference/`:

- `knowledge_files/MODULE_INTERCHANGE_SPEC.md`
- `knowledge_files/MEMORY_AUDIT.md`
- `knowledge_files/DSP_AUDIT.md`

Updated:

- `knowledge_files/specification_reference/MODULE_INTERCHANGE_SPEC.md`
  - Now records descriptor-backed VOICE pages and Scene-owned instrument images.
  - Notes that `ParameterArray` is legacy/static runtime pointer infrastructure,
    not the canonical instrument file map.
  - Points to `INSTRUMENT_FILE_SPEC.md` for instrument file/storage/menu/DSP
    propagation.
  - Calls out broken descriptor Morph/modulation/automation.
- `knowledge_files/specification_reference/MEMORY_AUDIT.md`
  - Marked as a Session 023 historical memory snapshot.
  - Notes that old large-symbol names predate SceneData/instrument descriptor
    refactors.
- `knowledge_files/specification_reference/DSP_AUDIT.md`
  - Marked as a historical DSP/performance snapshot.
  - Notes the Session 032 descriptor runtime bridge and the current broken
    Morph/modulation/automation state.
- `knowledge_files/log_archive/000_SESSION_INDEX.md`
  - Added Session 032 quick row, summary, and cross-session facts.
- `MEMORY.md`
  - Updated for Session 032, new spec paths, and current known gaps.

`INSTRUMENT_FILE_SPEC.md` now records:

- Directory kit shape under `SD_CARD/Kit/NNN Name/`.
- `kitset.kcg` format.
- Instrument file format and parser rules.
- Canonical descriptor key ownership.
- Scene storage model and packed descriptor IDs.
- Descriptor flag/macro meaning.
- Dynamic VOICE menu definition.
- DSP propagation path through Preset/InstrumentManager.
- Known broken descriptor Morph, LFO/modulation, and automation paths.

---

## Validation

Performed during the implementation pass:

- Focused `git diff --check` for touched code files passed.
- Root full-tree `git diff --check` was noisy because of unrelated existing
  whitespace/CRLF churn.
- `make` was not available in the active shell during the late pass, so a final
  compile was not run there.
- Hardware feedback from the user is the main functional signal at end of
  session: boot/menu/audio mostly works; Morph and descriptor automation do not.

---

## Known Follow-ups

1. Fix descriptor Morph:
   - Audit `preset_morph()`, `preset_morphTick()`, and
     `preset_getMorphValue()`.
   - Ensure interpolation reads Scene descriptor main/morph images instead of
     legacy `parameters2[]`.
   - Ensure interpolated descriptor values apply through
     `preset_applyInstrumentRuntimeValue()` / InstrumentManager.

2. Fix descriptor LFO/velocity modulation:
   - Audit `ModulationNode` destination storage and target value application.
   - Add a descriptor-aware target representation or bridge.
   - Ensure target display and runtime target selection use the same ID space.

3. Fix descriptor step automation:
   - Audit PatternData automation destination storage and Sequencer automation
     playback.
   - Bridge descriptor target IDs into the automation apply path without
     corrupting legacy/static parameter IDs.

4. Compile on the embedded toolchain:
   - Run `make clean`, `make`, `make img`.
   - Run focused grep checks for stale root spec paths after the root planning
     docs are removed.

5. Hardware-test after each repair:
   - 001 Slak boot load.
   - VOICE page main edits.
   - `SHIFT+VOICE` morph endpoint edits.
   - Morph knob/CC path.
   - LFO target assignment and audible modulation.
   - Velocity target assignment.
   - Step automation record/playback.
