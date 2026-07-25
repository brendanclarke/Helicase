# Session 033 Handoff Log

DATE: 2026-07-11
SESSION GOAL: Close the Phase 3 instrument-runtime gap opened by Session 032:
fix descriptor menu naming, LFO target selection/apply, descriptor Morph,
per-voice Morph, and per-voice Morph modulation, then consolidate the scratch
audits into durable specs/logs.
SOURCE AT END: local working directory, branch `dev-burst-reduction`.
VERIFIED ON HARDWARE: user reported the LFO fixes, Morph fix, per-voice Morph,
and per-voice Morph modulation landed. Local `make` passed during the code
implementation passes; final closeout edits were documentation/comment/spec
work.

## Completed

- Fixed descriptor display string leakage in the VOICE single-parameter view.
- Reworked descriptor row source order for Drum/Snare/Cymbal/HiHat to
  `key, category, long, short, dtype, ...` while preserving the compiled
  `ParamDescriptor` ABI layout.
- Fixed LFO target selection so target voice and target parameter stay coupled,
  target lists are dynamic, and non-modulatable descriptors are skipped instead
  of shown as repeated `off`.
- Added descriptor-aware LFO/velocity runtime apply for direct descriptor
  targets.
- Expanded LFOs to two target selector pairs, two amounts, and shared polarity.
- Expanded VOICE sub-page rows to 16 cells shown as four-cell screens.
- Fixed descriptor Morph routing and 0..255 endpoint math.
- Split Morph into global set-all plus six Scene-retained per-voice Morph
  values.
- Added per-voice Morph and Scene Decimation as velocity/LFO modulation
  targets through a new Scene modulation target namespace.
- Added detailed comment documentation for the catalogued SceneData ownership
  gap.
- Consolidated root audit documents into durable specs and logs.
- Made `knowledge_files/specification_reference/FILESYSTEM_SPEC.md` the only
  filesystem spec and deleted the root compatibility pointer.

## Code Changes

### Descriptor Display And Row Shape

Files:

- `Core/DSP/Instruments/Drum/DrumParameters.c/h`
- `Core/DSP/Instruments/Snare/SnareParameters.c/h`
- `Core/DSP/Instruments/Cymbal/CymbalParameters.c/h`
- `Core/DSP/Instruments/HiHat/HiHatParameters.c/h`
- `Core/Menu/menu.c`

Changes:

- Local descriptor macros now read naturally as
  `ROW("file_key", "Category", "LongName", "shr", DTYPE_..., ...)`.
- The macro expansion still writes the existing `ParamDescriptor` field order:
  `file_key`, `short_name`, `long_name`, `category`.
- Removed failed LFO-specific display fallbacks and designated-initializer
  experiments.
- Fixed the actual rendering bug: fixed-width string helpers had been reading
  past the first NUL terminator while padding. Short strings such as `"LFO"` or
  `"FM"` could pull adjacent string-literal bytes into the LCD row, producing
  headers such as `LFO lfo_Frequncy` or `FM  osc2Amount`.
- `menu_copyPaddedField()` now copies only until the first NUL and then pads the
  remaining display field with spaces. This is general display hygiene, not an
  LFO workaround.

Decision:

- File keys are only for storage lookup. Menu display always uses
  category/long/short descriptor strings, copied exactly and padded safely.

### LFO Target Selection

Files:

- `Core/Menu/menu.c`
- `Core/DSP/Instruments/InstrumentManager.c/h`
- Instrument `*Parameters.c/h` files

Changes:

- LFO target selection is driven by the active Scene slot's current instrument
  descriptor table.
- `lfo_target_voice` values are bounded to the supported destination domain.
  After Scene targets were added, `1..6` select voice slots and the `scn`
  display value selects Scene modulation targets.
- `lfo_target_param` walks a filtered target list: one `off`, then only valid
  modulatable descriptors for the selected voice, or valid Scene targets when
  destination voice is `scn`.
- Changing the LFO target voice reconciles the paired parameter. If the same
  local descriptor is not valid for the new destination, the parameter resets
  to `off`.
- Target labels no longer add redundant `Voice1`, `1wa`, or `1co` prefixes.
  The compact view shows the target descriptor short name; the single-parameter
  view shows the target descriptor category and long name.

Decision:

- No hardcoded target lists. Instrument slots are swappable; Menu and LFO code
  must ask InstrumentManager/SceneData what the current slot contains.

### LFO Runtime Apply

Files:

- `Core/DSPAudio/modulationNode.c/h`
- `Core/DSPAudio/lfo.c/h`
- `Core/DSPAudio/mixer.c`
- `Core/DSP/Instruments/InstrumentManager.c/h`
- Trigger paths in:
  - `Core/DSP/Instruments/Drum/DrumVoice.c`
  - `Core/DSP/Instruments/Snare/Snare.c`
  - `Core/DSP/Instruments/Cymbal/CymbalVoice.c`
  - `Core/DSP/Instruments/HiHat/HiHat.c`

Changes:

- `ModulationNode` now supports legacy `parameterArray[]` destinations and
  descriptor-resolved direct runtime destinations.
- Descriptor direct targets carry a cached runtime `Parameter`, optional
  waveform interpolation target, restore baseline, and range contract.
- `modNode_clearDestination()`, `modNode_setDirectDestination()`,
  `modNode_directOriginalValueChanged()`, and `modNode_shapeRangeU16()` were
  added with adjacent contract comments.
- Ordinary descriptor runtime writes notify the modulation backend so an active
  LFO/velocity target recaptures its base value after menu edits, Morph apply,
  kit apply, or other runtime writes.
- LFO dispatch now passes the source slot so Scene targets can attribute hidden
  per-voice Morph overlays to the emitting LFO and clear/replace the correct
  contribution.
- Velocity trigger paths now call InstrumentManager's descriptor/Scene target
  adapter after the direct ModNode update.

Decision:

- Descriptor IDs must never be handed to legacy `modNode_setDestination()`.
  Descriptor IDs identify Scene/descriptor targets; the backend must resolve
  them through InstrumentManager and install a direct runtime target.

### LFO Expansion

Files:

- `Core/DSPAudio/lfo.c/h`
- `Core/DSPAudio/modulationNode.c/h`
- `Core/Menu/menu.c`
- `Core/Menu/MenuText.h`
- `Core/DSP/Instruments/*/*Parameters.c/h`

Changes:

- Each LFO keeps one oscillator but now has two destination nodes:
  `modTarget` and `modTarget2`.
- Added `lfo_amount_2`, `lfo_target_voice_2`, and `lfo_target_param_2`.
- Added shared `lfo_polarity`, displayed as `neg`, `pos`, or `bi`.
- Negative polarity keeps the prior behavior: modulation below the base value.
- Positive polarity moves from the base value toward maximum.
- Bipolar polarity moves around the base value and clamps to range.
- The LFO short-name pages are:
  - `frq snc wav ofs`
  - `rtg pol am1 am2`
  - `vo1 ds1 vo2 ds2`
- VOICE sub-page rows now hold 16 cells. The UI shows four at a time and uses
  `>`, `*`, and `<` top-right indicators to show first/middle/last screen
  state. If a row has only one screen, no indicator is shown.
- Repeated SELECT presses on the same VOICE sub-page advance through screens
  and wrap to the first only when there is no next screen.
- Encoder movement across visible parameter boundaries moves to the next or
  previous populated screen without looping past the first/last real parameter.
- The numbered empty sentinel experiment was removed. Rows are plain 16-cell
  arrays filled with `INSTRUMENT_MENU_EMPTY`.

Decision:

- Two destinations do not mean two LFOs. Rate, sync, wave, offset, retrigger,
  and polarity are shared; only destination, amount, restore baseline, and
  range are per target.

### Morph Fix

Files:

- `Core/Menu/menu.c`
- `Core/MIDI/MidiParser.c`
- `Core/Scene/Preset/presetMorphEngine.c/h`
- `Core/Scene/Preset/presetManager.c/h`

Changes:

- PERF Morph no longer disappears into the legacy sound-parameter path. The
  static menu routing now lets non-instrument parameters reach
  `menu_parseGlobalParam()` and then `preset_morph()`.
- Morph math now uses the requested 0..255 range, with exact endpoints.
- MIDI CC1 maps 0..126 to `value * 2` and maps 127 to 255.
- Endpoint edits through `SHIFT+VOICE` queue the Morph worker with the relevant
  per-voice amount.
- The worker remains descriptor-driven and applies one morphable descriptor per
  foreground pass.

Decision:

- Morph is not allowed to own hardcoded per-instrument parameter lists. It
  walks the active slot's current descriptor table and uses descriptor flags.

### Per-Voice Morph

Files:

- `Core/Scene/SceneData.c/h`
- `Core/Scene/Preset/presetMorphEngine.c/h`
- `Core/Scene/Preset/presetManager.c/h`
- `Core/Scene/Preset/ParameterArray.h`
- `Core/Menu/menu.c`
- `Core/Menu/menuPages.h`
- `Core/Menu/MenuText.h`
- `Core/MIDI/MidiParser.c`

Changes:

- `scene_settings_t` now stores `voice_morph_amount[6]`.
- Added SceneData accessors for one voice Morph and set-all Morph.
- Global PERF `mrp` remains visible and retained, but its runtime meaning is
  “set all six per-voice Morph values.”
- PERF layout is now:
  - `mrp 1vm 2vm 3vm`
  - `4vm 5vm 6vm srt`
- `PAR_ROLL` was removed from the PERF page.
- Scene Decimation `srt` defaults to 127 at startup if not defined.
- MIDI CC1 handling respects the priority rule: the global channel has first
  claim if assigned and able to process the parameter; otherwise matching voice
  channel processing may apply per-voice Morph.

Decision:

- There is no separate per-voice Morph engine. The engine is per voice; global
  Morph is only a bulk setter for the six per-voice values.

### Scene Modulation Targets And Per-Voice Morph Modulation

Files:

- `Core/Scene/SceneModTargets.c/h`
- `Core/Scene/Preset/presetMorphEngine.c/h`
- `Core/Scene/Preset/presetManager.c/h`
- `Core/DSP/Instruments/InstrumentManager.c/h`
- `Core/Menu/menu.c`
- `Core/DSPAudio/lfo.c/h`
- `Core/DSPAudio/modulationNode.c/h`

Changes:

- Added a Scene target namespace beginning at `INSTRUMENT_VOICE_ID_COUNT`.
- Initial Scene targets are ordered:
  `1vm`, `2vm`, `3vm`, `4vm`, `5vm`, `6vm`, Scene `srt`.
- Scene Decimation is deliberately at the end so it is not adjacent to the
  voice-local `instrument_decimation` target, which also displays as `srt`.
- Velocity target lists now show one `off`, the source voice's modulatable
  descriptor targets, and then Scene targets.
- Velocity modulation of per-voice Morph is retained and visible: it writes the
  Scene per-voice Morph base, updates the menu value, and queues Morph.
- Velocity modulation of Scene Decimation is retained and visible.
- LFO modulation of per-voice Morph is hidden: it writes a secondary overlay
  centered on the retained base value and does not move the PERF menu.
- Multiple LFO contributions to one voice Morph sum as signed deltas around the
  retained base and clamp to `0..255`.
- LFO modulation of Scene Decimation is runtime-only and does not change the
  retained PERF `srt` value.
- The Morph worker gains one extra pass per active LFO-modulated voice, but
  still interpolates one descriptor per pass.

Decision:

- Per-instrument decimation is not a Scene target. It is a voice-local
  descriptor target using `INSTRUMENT_BIND_SLOT_DECIMATION`, and it remains
  morphable, modulatable, and marked automatable.

### Comment Documentation Pass

Files:

- `COMMENT_DOCUMENTATION_NEEDED.md`
- `Core/Scene/SceneData.c/h`

Changes:

- Completed the catalogued documentation pass for SceneData.
- Added detailed comments for Scene, Kit, instrument slot, descriptor image,
  Scene settings, MIDI settings, Scene Decimation, per-voice Morph, and all
  SceneData accessors.
- Comments describe why storage exists, what each function does, inputs,
  outputs, and affiliates/clients.

## Documentation And Spec Updates

Files:

- `knowledge_files/specification_reference/FILESYSTEM_SPEC.md`
- `knowledge_files/specification_reference/MODULE_INTERCHANGE_SPEC.md`
- `knowledge_files/specification_reference/DSP_AUDIT.md`
- `SCOPING_TARGETS.md`
- `MEMORY.md`
- `knowledge_files/log_archive/000_SESSION_INDEX.md`
- `knowledge_files/log_archive/033_SESSION_HANDOFF_LOG.md`
- root `FILESYSTEM_SPEC.md`

Changes:

- `FILESYSTEM_SPEC.md` in `knowledge_files/specification_reference/` is now
  the authoritative filesystem/spec state after Session 033.
- Root `FILESYSTEM_SPEC.md` was deleted so there is no duplicate authority.
- The spec clearly states that the only implemented filesystem operation in
  the new hierarchy is root Kit folder load by `Kit/NNN Name/`.
- The spec clearly states that new-format saves, Scene load/save, Bank
  load/save, effects, Instrument pool browsing/save, root `settings.cfg`, and
  descriptor step automation are not implemented.
- The spec now includes Session 033 runtime facts: per-voice Morph, Scene
  targets, LFO expansion, voice-local decimation target, and current descriptor
  counts.
- `MODULE_INTERCHANGE_SPEC.md` now treats SceneModTargets as an owner module
  and updates the stale Session 032 “Morph/modulation broken” statements.
- `DSP_AUDIT.md` is marked historical with a Session 033 note.
- `INSTRUMENT_FILE_SPEC.md` was subsequently folded completely into
  `FILESYSTEM_SPEC.md` and deleted, leaving one authoritative file/storage
  reference.
- `SCOPING_TARGETS.md` now puts the project effectively in Phase 3 file work:
  automation, Kit/Instrument save, morphed-instrument save/load, Scene, FX shim,
  Bank, and root settings.
- `MEMORY.md` now points new sessions at Session 033 and removes the stale
  “descriptor Morph broken” note.

## Known Issues Introduced

- None known from the code paths landed this session.

## Known Issues Resolved

- LFO edit headers and target labels no longer leak file-key/string-literal
  bytes.
- LFO target voice/parameter selection no longer walks raw IDs or repeated
  `off` placeholders.
- Descriptor-backed LFO and velocity targets now apply at runtime.
- Descriptor Morph now reaches endpoints and responds to PERF/MIDI/menu paths.
- Per-voice Morph exists as Scene-retained state and as modulation targets.
- Scene Decimation starts at audible default 127 when not defined.

## Remaining Gaps

- Descriptor-aware step automation is still pending:
  `AutomationNode`, `seq_recordAutomation()`, step target storage, target
  display, and recording must preserve descriptor/Scene IDs.
- No new-format save operations exist yet.
- Scene folders, Bank folders, Effect files, Instrument pool operations, and
  root `settings.cfg` are still target specs, not implemented file flows.
- Voice 6/tracks 6+7/choke/open-hat storage needs a cleanup decision before
  hardening file save/load.
- Instrument swapping in Kit slots should be explicitly verified during the
  next file-work session.

## Next Session Recommended Goal

Start Phase 3 file work:

- Minor restructure around how voice 6 is stored for tracks 6+7 and choke.
- Verify swappable instruments in Kit slots.
- Implement Kit save in the same folder shape that current Kit load accepts.
- Implement Instrument save/load for descriptor-keyed files, including
  morphed-instrument `[params]` and `[morph]` endpoints.
- Then work upward through Scene, an FX slot shim, and Bank datatypes.

## Critical Reminders For Next Session

- `knowledge_files/specification_reference/FILESYSTEM_SPEC.md` is the
  filesystem authority.
- Root `FILESYSTEM_SPEC.md` was intentionally deleted.
- `INSTRUMENT_FILE_SPEC.md` has been deleted after being folded into
  `FILESYSTEM_SPEC.md`.
- Do not hardcode instrument parameter lists. Voice slots are swappable.
- Scene targets are only non-voice sound targets. Voice-local descriptor
  targets stay in the current instrument descriptor tables.
- Scene Decimation `srt` and voice-local `instrument_decimation` `srt` are
  different targets.
- LFO Morph modulation is hidden and runtime-layered; velocity Morph modulation
  is retained and visible.
- Automation is still the target runtime path most likely to be wrong.
