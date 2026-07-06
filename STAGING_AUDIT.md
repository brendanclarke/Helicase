# Staging / Temporary State Audit

Purpose: list places where the firmware stages, snapshots, defers, buffers, or
retains state around file loading, morphing, menu completion, and related UI or
runtime handoffs. The intent is to identify cleanup candidates, especially
places that may be leftovers from the original two-MCU LXR design.

This is an audit only. It does not recommend deleting anything blindly. Several
items below are essential because the current firmware is asynchronous, has an
audio deadline, or needs boundary-safe sequencer behavior.

## Session Follow-Up Decisions

- Leave the whole active-pattern load buffer for now. It is the only temporary
  storage we expect to need during the transition, and it should come out when
  the 17th Scene/background-bank-load design is implemented.
- Do not touch `filesystem.c` staging/scratch buffers in this cleanup thread.
  `filesystem.c` is new code and was built for the current hardware/async file
  model.
- Leave Menu sound-apply continuation flags for now, but audit them by file type
  and by load-completion phase. These states should be eliminated or recast when
  the filesystem/Scene storage model is restructured.
- Investigate global-apply continuation state separately. It may be possible to
  remove permanent duplicate global storage by applying globals directly to their
  owners while still chunking the work.
- Preferred globals direction: move the current raw globals into canonical
  settings structs, likely split into scene-level, bank-level, and system-level
  structs during the Scene/file redesign.
- Do not touch load/save selection deferral. That polling model is new code.
- The remaining items are generally new or purposeful and should be left alone
  unless their owning subsystem is redesigned.
- `parameter_values[]` and `parameters2[]` must eventually migrate fully into
  `/Core/Preset/`, which should become the only canonical source of Preset
  Voice/Kit/Morph endpoint parameters. The Menu should not permanently own full
  parameter arrays. This belongs with the instrument file redesign, not this
  cleanup.

## Classification

- **Strong candidate**: likely worth simplifying or replacing in a later cleanup.
- **Review candidate**: may be useful, but the ownership or shape is awkward.
- **Probably keep**: real buffering/snapshotting for async I/O, audio safety,
  input decoding, or UI correctness.
- **Do not touch as staging**: named like a buffer/temp/snapshot, but it is
  domain state rather than a removable staging layer.

## Strong Candidates

### Whole Active-Pattern Load Buffer

- Files:
  - `Core/Scene/Pattern/PatternData.c`
  - `Core/Scene/Pattern/PatternData.h`
  - `Core/Hardware/SD/filesystem.c`
  - `Core/Sequencer/sequencer.c`
- Symbols/functions:
  - `TempPattern pat_tmpPattern`
  - `PATTERNDATA_STAGING_PATTERN`
  - `pat_commitStagedPattern()`
  - `filesystem_patternStepPtr()`
  - `filesystem_patternMainPtr()`
  - `filesystem_patternSettingPtr()`
  - `filesystem_patternLengthPtr()`
  - `op_loaded_active_pattern_running`
  - `seq_newPatternAvailable`
  - `seq_armActivePatternReload()`

What it does:

- When loading a `.pat`, `.all`, or `.prf` while the sequencer is running,
  writes targeting the currently active pattern are redirected into
  `pat_tmpPattern`.
- At a sequencer pattern boundary, `seq_nextStep()` calls
  `pat_commitStagedPattern(seq_activePattern)` to copy that temporary pattern
  into the active slot.

Why it exists:

- It prevents the playing pattern from being partially overwritten by an async
  file stream.
- It preserves current behavior: active pattern loads become audible at a
  sequencer-safe boundary rather than immediately halfway through a bar.

Decision:

- Leave it for now.
- Flag it for removal during the 17th Scene/background-bank-load design.
- This should be the only temporary storage necessary in the old pattern path.

Why it may be vestigial or overbuilt later:

- This is a full pattern copy buffer, roughly the size of one pattern payload.
- On the single-MCU port, the old "SD MCU trickles data to synth MCU" reason no
  longer exists. The remaining reason is only boundary safety while running.
- If active-pattern load while running is rare or can change behavior, the buffer
  may be replaceable with a simpler policy.

Future cleanup options:

- Disallow active-pattern overwrite while running and show/busy-defer instead.
- Stop or suspend the sequencer for active-pattern loads, then stream directly
  into `pat_patternSet`.
- Stream active-pattern loads into an explicitly reserved pattern slot, then
  swap logical pattern selection at the boundary. This needs a data-model
  decision and is not a pure cleanup.
- Keep the buffer but rename/comment it as "active-pattern boundary shadow" so
  it does not look like generic staging.

Risk:

- Removing this without changing behavior would expose half-loaded pattern data
  to playback. Any simplification must define what happens when the currently
  playing pattern is loaded from disk.

### Filesystem Save Snapshot Buffer For Kit/Morph/Globals

- File: `Core/Hardware/SD/filesystem.c`
- Symbols/functions:
  - `static uint8_t staging_buf[320]`
  - `static uint16_t staging_len`
  - `filesystem_saveKit_tick()`
  - `filesystem_saveGlobals_tick()`

What it does:

- Kit and morph saves copy the name plus sound parameters into `staging_buf`
  before opening/writing the file.
- Globals save copies the globals slice into `staging_buf` before writing.

Decision:

- Leave it. `filesystem.c` is new code and correctly built around the current
  hardware and async filesystem model.

Why it exists:

- It gives the async write a stable payload while `afatfs_fwrite()` may complete
  over several `filesystem_tick()` calls.
- Morph save computes interpolated values once, including special handling for
  mod-target ranges that must save the base value.

Why it may be vestigial or overbuilt:

- Menu sets `menu_storageBusy` for save/load UI, so normal panel edits are
  blocked while saving.
- Some payloads could be streamed directly from `preset_currentName`,
  `parameter_values[]`, or computed per index during the write phase.
- The buffer is also reused as a tiny record scratchpad elsewhere, which makes
  the name "staging" mean both "stable save snapshot" and "one-record I/O
  scratch."

Possible cleanup options only if filesystem ownership is redesigned:

- Split `staging_buf` into a small `io_record_buf[8]` and a deliberate
  `save_snapshot_buf[]`, or remove the snapshot for saves that can prove source
  data cannot change mid-write.
- Stream globals directly from `parameter_values + PAR_BEGINNING_OF_GLOBALS`.
- Keep a morph-save snapshot only if repeated per-byte interpolation is too
  costly or if external MIDI can still change the base kit during a save.

Risk:

- MIDI/external control may still mutate `parameter_values[]` even when panel UI
  is storage-busy. Verify all non-menu writers before removing save snapshots.
- Morph save must remain internally consistent for all 128-ish sound parameters.

### Menu Sound-Apply Continuation Flags

- File: `Core/Menu/menu.c`
- Symbols/functions:
  - `menu_soundApplyActive`
  - `menu_soundApplyUpdateGap`
  - `menu_soundApplyResetSave`
  - `menu_soundApplyRepaintAll`
  - `menu_soundApplyStartGlobals`
  - `menu_soundApplyRequestPattern`
  - `menu_soundApplyApplyPerformanceGlobals`
  - `menu_soundApplyClearStorageBusy`
  - `menu_soundApplyShowStaleWarning`
  - `menu_soundApplyStaleWarning`
  - `menu_startSoundApply()`
  - `menu_tickSoundApply()`
  - `menu_finishSoundApply()`

What it does:

- Captures a set of post-load follow-up actions while `preset_tickDrumsetApply()`
  applies one voice per foreground pass.
- Replays the same side effects that used to happen in one synchronous
  completion burst: mod-target gap update, globals apply, pattern settings menu
  refresh, save UI reset, repaint, stale-warning display, and storage-busy clear.

Decision:

- Leave the current code for now.
- Expand the audit by file/load type so the future filesystem/Scene rewrite has
  a checklist for eliminating these retained states.

Why it exists:

- This is a burst-reduction continuation. It prevents one large foreground
  control update after kit/all/performance load.

Why it may be vestigial or awkward:

- The many parallel flags are a hand-rolled continuation record.
- It is easy for future operations to add another flag and make the state harder
  to reason about.

Possible cleanup options:

- Replace parallel globals with a small struct such as
  `menu_sound_apply_job_t`.
- Use an enum/job type for `KIT_LOAD`, `ALL_LOAD`, `PERFORMANCE_LOAD`, etc.,
  then derive follow-up actions at finish time.
- Keep the chunking, but make the retained state explicit rather than a cluster
  of independent bytes.

Known current file/load uses:

- Kit load:
  - `menu_pollPresetStatus()` handles `PRESET_OP_KIT_LOAD`.
  - Calls `menu_startSoundApply(updateGap=1, resetSave=0, repaintAll=1,
    startGlobals=0, requestPattern=0, applyPerformanceGlobals=0,
    clearStorageBusy=0, showStaleWarning=0)`.
  - The retained flags finish by updating the LFO target gap, repainting, and
    leaving storage-busy handling to the surrounding load/save page logic.
- Morph load:
  - Does not use `menu_startSoundApply()`.
  - Normalizes `parameters2[]`, arms `preset_morph()`, and repaints directly.
- Pattern load:
  - Does not use `menu_startSoundApply()`.
  - Calls `pat_applyPatternSettingsToMenu()`, clears storage busy, resets save
    parameters, and repaints directly.
- Globals load:
  - Does not use `menu_startSoundApply()`.
  - Starts `menu_startGlobalApply(resetSave=load/save page, repaintAll=not
    load/save page)` and may show a stale `.glo` warning.
- ALL load:
  - Calls `menu_startSoundApply(updateGap=0, resetSave=1, repaintAll=1,
    startGlobals=1, requestPattern=1, applyPerformanceGlobals=0,
    clearStorageBusy=1, showStaleWarning=stale_all)`.
  - The retained flags finish sound apply, start global apply, refresh pattern
    settings, eventually clear storage busy, repaint/reset through global apply,
    and defer the stale `.all` warning until after the chained applies settle.
- Performance load:
  - Calls `menu_startSoundApply(updateGap=0, resetSave=1, repaintAll=1,
    startGlobals=0, requestPattern=1, applyPerformanceGlobals=1,
    clearStorageBusy=1, showStaleWarning=0)`.
  - The retained flags finish sound apply, apply `PAR_BPM` and
    `PAR_BAR_RESET_MODE`, refresh pattern settings, clear storage busy, reset
    save UI, and repaint.

Proposed future state-progress struct:

```c
typedef enum SceneFileKind {
    SCENE_FILE_BANK_SETTINGS,
    SCENE_FILE_SCENE,
    SCENE_FILE_KIT,
    SCENE_FILE_INSTRUMENT_DRUM,
    SCENE_FILE_INSTRUMENT_SNARE,
    SCENE_FILE_INSTRUMENT_CYMBAL,
    SCENE_FILE_INSTRUMENT_HAT,
    SCENE_FILE_PATTERN,
    SCENE_FILE_FX,
    SCENE_FILE_GLOBALS,
    SCENE_FILE_SAMPLE,
    SCENE_FILE_WAVETABLE
} SceneFileKind;

typedef enum SceneLoadPhase {
    SCENE_LOAD_OPEN,
    SCENE_LOAD_STREAM,
    SCENE_LOAD_APPLY_SOUND,
    SCENE_LOAD_APPLY_GLOBALS,
    SCENE_LOAD_APPLY_PATTERN,
    SCENE_LOAD_APPLY_FX,
    SCENE_LOAD_FINISH_UI,
    SCENE_LOAD_DONE
} SceneLoadPhase;

typedef struct SceneFileProgress {
    SceneFileKind kind;
    SceneLoadPhase phase;
    uint8_t bank;
    uint8_t scene;
    uint8_t part;
    uint16_t item_index;
    uint32_t byte_offset;
    uint32_t byte_count;
    uint8_t needs_sound_apply;
    uint8_t needs_global_apply;
    uint8_t needs_pattern_refresh;
    uint8_t needs_fx_apply;
    uint8_t reset_save_ui;
    uint8_t repaint;
    uint8_t clear_storage_busy;
    fs_stale_warning_source_t stale_warning;
} SceneFileProgress;
```

SCOPING_TARGETS file families this needs to cover:

- Current flat files: kit, morph kit, pattern, performance, all, globals,
  samples.
- Phase 2 hierarchy: bank `settings.cfg`, 16 scene directories, per-instrument
  files (`.drm`, `.snr`, `.cym`, `.hat`), `pattern.pat`, `effects.fx`, root
  library `KIT/`, `PAT/`, `FX/`, `SCENE/`, `SAMPLES/`, and `WAVETABLES/`.

Risk:

- Do not collapse this back into a synchronous burst. The current retained state
  exists to protect audio scheduling after runtime loads.

## Review Candidates

### Filesystem One-Record Scratch Buffer

- File: `Core/Hardware/SD/filesystem.c`
- Symbol: `staging_buf[320]`
- Used by:
  - `filesystem_savePattern_tick()`
  - `filesystem_loadPattern_tick()`
  - `filesystem_saveContainer_tick()`
  - `filesystem_loadContainer_tick()`
  - `filesystem_loadGlobals_tick()`

What it does:

- Packs/unpacks small file records: `Step` as 7 bytes, main-step masks as
  little-endian two-byte values, pattern settings, shuffle bytes, lengths, and
  container metadata.

Decision:

- Leave it. `filesystem.c` is new code and correctly built for current hardware.

Why it exists:

- File format byte order is explicit and not identical to "just write the C
  struct" in every case.
- Async reads may return partial chunks, so `op_item_offset` tracks completion
  before unpacking a logical record.

Review notes:

- This is probably legitimate, but the 320-byte size comes from save snapshots,
  not from the small pattern record scratch usage.
- If save snapshots are removed or split, the scratch buffer can shrink
  dramatically.
- The name `staging_buf` hides the distinction between "record scratch" and
  "whole save snapshot."

Recommendation if filesystem is later redesigned:

- Keep a scratch buffer, but split/rename it during filesystem cleanup.

### `presetManager` Async Status State

- File: `Core/Preset/presetManager.c`
- Symbols/functions:
  - `pm_status`
  - `pm_completed_op`
  - `pm_request_slot`
  - `pm_request_type`
  - `preset_completeFilesystemOp()`
  - `preset_getStatus()`
  - `preset_ackStatus()`

What it does:

- Filesystem completion callbacks record which preset operation completed.
- Menu later polls and applies operation-specific follow-up.

Why it exists:

- It reproduces the old two-MCU "parameters arrive after transfer completes"
  delay while using async filesystem operations on one MCU.
- It separates filesystem completion from menu/UI/DSP side effects.

Why it may be a cleanup target:

- `filesystem.c`, `presetManager.c`, and `menu.c` now form a three-stage
  completion chain.
- Some of this could become a single scene/storage job object when file
  operations move under a scene model.

Recommendation:

- Do not remove casually. Consider consolidating it with menu completion state
  only as part of a broader storage/job API cleanup.

### Global-Apply Continuation State

- File: `Core/Menu/menu.c`
- Symbols/functions:
  - `menu_globalApplyActive`
  - `menu_globalApplyResetSave`
  - `menu_globalApplyRepaintAll`
  - `menu_globalApplyIndex`
  - `menu_startGlobalApply()`
  - `menu_tickGlobalApply()`
  - `menu_finishGlobalApply()`

What it does:

- Applies global parameters over foreground passes after runtime globals/all
  load, two parameters per pass.

Decision:

- Investigate now in `GLOBALS_STAGING_AUDIT.md`.
- The likely cleanup is not "stop chunking"; it is "stop permanently storing the
  same global value in both a menu byte array and an owner module when the owner
  can be canonical."

Why it exists:

- Globals can touch hardware/system settings. Chunking avoids a runtime burst.

Cleanup angle:

- This state is probably necessary, but it could be expressed as a small job
  struct or merged with the sound-apply continuation model.

Risk:

- Synchronous boot-time apply is still intentionally retained when
  `audioCodec_renderCount == 0`.

### Load/Save Selection Deferral

- File: `Core/Menu/menu.c`
- Symbols/functions:
  - `menu_deferSelectionRequest`
  - `menu_deferSelectionLoadKit`
  - `menu_requestCurrentLoadSaveSelection()`
  - `menu_pollPresetStatus()`

What it does:

- Defers name/selection load requests when the filesystem or preset layer is not
  idle.

Decision:

- Do not touch it. This load/save menu polling model is new code.

Why it exists:

- Name browsing uses async filesystem operations and the load/save page can
  change selection while another request is completing.

Cleanup angle:

- Likely legitimate but could be folded into a load/save-page controller object.
- It is not a large buffer, but it is retained menu state that exists only to
  bridge async filesystem behavior.

### Stale-Settings Warning Deferral

- Files:
  - `Core/Hardware/SD/filesystem.c`
  - `Core/Menu/menu.c`
- Symbols/functions:
  - `fs_stale_warning_pending`
  - `filesystem_takeStaleGlobalsWarning()`
  - `menu_soundApplyShowStaleWarning`
  - `menu_soundApplyStaleWarning`
  - `menu_staleWarningActive`
  - `menu_staleWarningStart`
  - `menu_pendingAllStaleWarning`

What it does:

- Carries "old globals/all file" warnings from filesystem validation through
  menu post-load apply, then displays a timed warning without blocking audio.

Why it exists:

- The file parser detects compatibility problems, but the menu owns UI timing.
- ALL loads need kit/pattern/global application to finish before showing the
  stale warning.

Cleanup angle:

- State is legitimate, but it is scattered between filesystem and menu.
- A storage result object could carry warning metadata more cleanly.

### Kit Browser Name Retention

- File: `Core/Hardware/SD/kitBrowser.c`
- Symbols/functions:
  - `kb_map[]`
  - `kb_numKits`
  - `kb_mapIndex`
  - `kb_dirty`
  - `kb_name_pending`
  - `kb_kitName[9]`
  - `loaded_name[9]` in `filesystem.c`
  - `filesystem_requestLoadName()`
  - `filesystem_loadedName()`

What it does:

- `filesystem_scanKits_tick()` builds a slot map.
- KitBrowser requests an async 8-byte name load for the selected kit and keeps a
  local displayed name while the filesystem owns the transient `loaded_name`.

Why it exists:

- Names are read asynchronously so scrolling does not block.

Cleanup angle:

- `filesystem_loadedName()` plus `kb_kitName` is a small copy chain. It is not a
  memory problem, but ownership is a little blurred.
- If a generalized file browser is added for patterns/all/performance, this
  should become a shared browser/name-load model instead of more per-file-type
  retained name buffers.

### Pattern Load Shuffle Import

- Files:
  - `Core/Hardware/SD/filesystem.c`
  - `Core/Scene/Pattern/PatternData.c`
- Symbols/functions:
  - `pat_setAllShuffle()`
  - `pat_shuffleValue[NUM_PATTERN]`
  - `parameter_values[PAR_SHUFFLE]`
  - `seq_setShuffle()`

What it does:

- Legacy files store one shuffle byte. PatternData copies it into every current
  pattern slot and updates menu/runtime playback state.

Why it exists:

- File format is still a one-byte global-ish shuffle value while ownership is
  moving to PatternData.

Cleanup angle:

- This is not a staging buffer, but it is a compatibility fan-out. It should be
  revisited when the pattern file format can represent per-pattern shuffle or
  when playback becomes scene-aware.

## Probably Keep / Not Vestigial

### `parameters2[]` Morph Kit Buffer

- File: `Core/Menu/menu.c`
- Users:
  - `Core/Hardware/SD/filesystem.c`
  - `Core/Preset/presetManager.c`
- Symbol: `uint8_t parameters2[END_OF_SOUND_PARAMETERS]`

What it does:

- Holds the loaded morph kit target values.
- `preset_getMorphValue()` interpolates between `parameter_values[]` and
  `parameters2[]`.

Decision:

- Leave for now, but it must eventually migrate fully into `/Core/Preset/`
  alongside `parameter_values[]`.
- Menu should not permanently own the full kit/morph endpoint arrays.
- The right time is the instrument file redesign, when Preset becomes canonical
  for Voice/Kit/Morph endpoint parameters.

Why it is probably not vestigial:

- This is the actual morph target kit, not a temporary transfer buffer.
- Removing it would require a different morph data model.

Possible cleanup:

- Rename or move ownership under a future Scene/Preset module so it is not
  declared in `menu.c`.

### Morph Pass Scheduler State

- File: `Core/Preset/presetManager.c`
- Symbols/functions:
  - `morph_active`
  - `morph_target_value`
  - `morph_pass_value`
  - `morph_request_generation`
  - `morph_pass_generation`
  - `morph_index`
  - `preset_morph()`
  - `preset_morphTick()`

What it does:

- Applies one morph parameter per foreground pass.
- Uses a pass snapshot and generation counters to guarantee the latest morph
  value eventually receives a full pass.

Why it is probably not vestigial:

- This was added to avoid bursty full-kit morph application and to avoid the
  bad "skip cache" behavior documented in `MEMORY.md`.

Possible cleanup:

- Keep the scheduler, but move it with morph ownership if morph leaves
  `presetManager.c`.

### Direct Kit/Morph Load Into `parameter_values[]` / `parameters2[]`

- File: `Core/Hardware/SD/filesystem.c`
- Function: `filesystem_loadKit_tick()`

What it does:

- Reads a kit directly into `parameter_values[]` or a morph kit directly into
  `parameters2[]`.
- Post-load sound application happens later through the preset/menu completion
  path.

Why it is probably not vestigial:

- It avoids an extra full kit staging buffer.
- It already separates "bytes loaded" from "sound engine applied."

Risk / review note:

- Malformed short kit files zero-fill tails. If atomic kit load failure becomes
  important, this path would need a true temp kit buffer. That would add staging,
  not remove it.

### Sample Install Manifest and I/O Buffer

- File: `Core/Hardware/SD/filesystem.c`
- Symbols/functions:
  - `sample_manifest[]`
  - `sample_manifest_count`
  - `sample_io_buf[FS_SAMPLE_BUFFER_SIZE + 4]`
  - `filesystem_scanSamples()`
  - `filesystem_insertSampleManifest()`
  - `filesystem_installOneSample()`

What it does:

- Scans `/samples` or `/loops`, parses WAV metadata, keeps sorted install
  candidates, then streams audio data into sample flash in 512-byte chunks.

Why it is probably not vestigial:

- The manifest is needed for sorted install order and flash fit planning.
- The I/O buffer is needed for block reads and 4-byte padding before flash
  writes.
- This path is intentionally modal/blocking with audio suspended.

Cleanup angle:

- Only revisit if sample install becomes streaming/no-sort or if flash writer
  can consume asyncfatfs sector buffers directly.

### asyncfatfs Sector Retention / SD Transfer Buffers

- Files:
  - `Core/Hardware/SD/asyncfatfs/asyncfatfs.c`
  - `Core/Hardware/SD/asyncfatfs/sdcard_lxr02.c`
- Symbols:
  - cache sector memory/descriptors
  - `retainCount`
  - `readRetainCacheIndex`
  - `xfer_buffer`

What it does:

- Owns lower-level FAT sector caching and asynchronous SD block transfer state.

Why it is probably not a cleanup target:

- This is real filesystem machinery, not application-level staging left over
  from the two-MCU design.
- The cache retain counters protect sector pointers while async reads/writes
  are still in progress.

### Sequencer Pending Pattern State

- File: `Core/Sequencer/sequencer.c`
- Symbols/functions:
  - `seq_pendingPattern`
  - `seq_loadPendigFlag`
  - `seq_setNextPattern()`
  - `seq_armActivePatternReload()`
  - `seq_nextStep()`

What it does:

- Holds a requested pattern switch until a sequencer boundary.
- Also arms active-pattern reload after a staged file load finishes.

Why it is probably not vestigial:

- This is transport scheduling state, not a file staging buffer.
- Pattern changes cannot simply happen the instant the UI or filesystem asks.

Cleanup angle:

- Typo cleanup: `seq_loadPendigFlag` should eventually become
  `seq_loadPendingFlag`.
- If pattern scheduling moves under Scene/Pattern later, this needs an
  ownership decision, but the delayed behavior must remain.

### Sequencer Step Snapshot For Playback Automation

- File: `Core/Sequencer/sequencer.c`
- Function: `seq_triggerVoice()`
- Symbol: local `Step stepData`

What it does:

- Copies the current PatternData step through `pat_readStep()` so sequencer can
  parse automation and MIDI velocity without direct array access.

Why it is probably not vestigial:

- It is a one-record snapshot used to enforce the PatternData boundary.
- Removing it would likely reintroduce direct pattern storage reads or require a
  more specific PatternData API.

### Front-Panel Input Snapshots

- Files:
  - `Core/Hardware/frontPanel/IO/endlessPots.c`
  - `Core/Hardware/frontPanel/IO/endlessPots.h`
  - `Core/Hardware/frontPanel/IO/encoder.c`
- Symbols/functions:
  - `endless_snapshotUnlocked()`
  - `endlessPots_snapshot()`
  - `endlessPots_snapshotAll()`
  - endless pot baselines / accumulators / pending delta fields
  - encoder `ts_buf[]`, `ts_dirs[]`, partial `enc_delta`

What it does:

- Maintains physical-control baselines, pending deltas, acceleration history,
  and page-change rebaselines.

Why it is probably not vestigial:

- These are input-decoding state machines, not leftover two-MCU staging.
- Removing them would reintroduce ghost edits, page-change jumps, or encoder
  direction-change residue.

### Button Reset-Lock Snapshot

- File: `Core/Hardware/frontPanel/buttonHandler.c`
- Symbols/functions:
  - `buttonHandler_originalParameter`
  - `buttonHandler_originalValue`
  - `buttonHandler_resetLock`
  - `buttonHandler_disarmTimerActionStep()`

What it does:

- Captures the original parameter/value during reset-lock or long-press style
  edits so the value can be restored when the gesture ends.

Why it is probably not vestigial:

- It is gesture state, not transfer staging.
- It must restore through Preset or Menu global dispatch so DSP/global side
  effects remain correct.

### Euklid Generation Scratch

- File: `Core/Scene/Pattern/EuklidGenerator.c`
- Symbols:
  - `euklid_patternBuffer`
  - `euklid_nextCnt1`
  - `euklid_originalLength`

What it does:

- Holds intermediate state for recursive Euclidean pattern generation before
  transferring the generated bitmask to PatternData.

Why it is probably not file/menu staging:

- It is algorithm scratch state.

Cleanup angle:

- It could possibly be made local/reentrant later, but that is a generator
  cleanup, not a staging-buffer removal.

### Oscillator Temporary Copies For Wave Evaluation

- File: `Core/DSPAudio/Oscillator.c`
- Functions:
  - `osc_evalWaveAtPhase()`
  - `osc_evalWaveAtPhaseFm()`
- Symbol: local `OscInfo tmp`

What it does:

- Makes a temporary oscillator copy to evaluate a waveform at a phase without
  mutating the real oscillator.

Why it is probably not vestigial:

- It is DSP scratch used for waveform interpolation/preview behavior.
- Not related to file loading or menu state retention.

## Suggested Cleanup Order

1. Decide active-pattern load policy.
   - This determines whether `pat_tmpPattern` is truly needed.
   - If active-pattern load while running must remain boundary-safe and
     seamless, the buffer or an equivalent shadow target is still required.

2. Split filesystem buffer roles.
   - Rename/split `staging_buf` into record scratch vs save snapshot.
   - Then evaluate whether kit/globals save snapshots are still needed.

3. Convert menu continuation flags into structs/jobs.
   - Keep chunked behavior.
   - Reduce scattered retained state and make post-load jobs inspectable.

4. Revisit storage completion layering.
   - `filesystem.c` -> `presetManager.c` -> `menu.c` is correct today, but a
     future Scene storage job could carry completion type, stale-warning info,
     and follow-up policy in one object.

5. Leave physical input snapshots, asyncfatfs buffers, sample installer buffers,
   and morph target data alone unless their owning subsystems are redesigned.

## Quick Grep Seeds For Future Passes

- `rg -n "staging|tmp|temp|snapshot|pending|defer|retain|buffer" Core`
- `rg -n "parameters2|morph_|menu_soundApply|menu_globalApply" Core`
- `rg -n "PATTERNDATA_STAGING_PATTERN|pat_tmpPattern|op_loaded_active_pattern_running" Core`
- `rg -n "staging_buf|staging_len|loaded_name|kb_name_pending" Core/Hardware/SD Core/Menu`
