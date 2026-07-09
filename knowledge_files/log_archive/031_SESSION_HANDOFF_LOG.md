# Session 031 Handoff Log

```
DATE: 2026-07-09
SESSION GOAL: Complete and fine-tune the one-live-pattern/8-bar bridge, STEP track-settings page, LED flash behavior, legacy kit conversion, morph voice mode, voice preview, and per-track shuffle.
COMPLETED: One-pattern bridge behavior, corrected sequencer timing, group LED flash overlay, STEP track-settings page, PatternData track settings, provisional pattern/container track-settings storage, kit converter rebuild, SHIFT+VOICE morph edit mode, stopped voice preview, per-track shuffle, post-closeout boot-hang fixup, and session documentation.
VERIFIED ON HARDWARE: User reported the post-closeout boot-hang fixup seems to work. Broader Session 031 interaction matrix still needs hardware confirmation.

CHANGES THIS SESSION:
- Core/Scene/Pattern/PatternData.c/h: Changed live pattern storage toward one 128-step pattern with `NUM_PATTERN=1`, `NUM_BARS=8`, and `NUM_STEPS_PER_BAR=16`; kept staging support for bridge loads. Added `PAT_DEFAULT_TRACK_LENGTH` as 16 so empty boot tracks start at one bar. Expanded the per-track `LengthRotate` record to carry STEP front-page pattern settings: length, rotation, scale, MIDI channel, MIDI note, and shuffle. Added accessors for track scale, MIDI channel/note, and shuffle, plus menu-apply helpers that mirror the active track into `parameter_values[]`.
- Core/Sequencer/sequencer.c/h: Slowed the default step advancement to the corrected 1/8 rate and introduced a 96-PPQ master step clock that resets on sequencer start and pattern changes. Added exact per-track scale ratios, including `/25`, with due-event timing derived from absolute master ticks so fractional scales do not drift across 128-step loops. Added pattern realign for the active pattern, per-track shuffle application, stopped voice preview through `seq_previewVoice()`, and MIDI note/channel lookup through the new per-track PatternData settings. Removed the global `seq_setShuffle()` path.
- Core/Menu/menu.c/h and Core/Menu/menuPages.h: Added STEP subpage-0 front-page/track-settings behavior. The first half shows length, scale, MIDI channel, and MIDI note in that order; the second half shows per-track shuffle. Track settings refresh immediately when entering STEP mode and on first voice/track button press. Added morph-view helpers so VOICE pages can display and edit `parameters2[]` while `voiceModeShowMorph` is set. Removed PERF shuffle display for now so shuffle ownership is per-track PatternData.
- Core/Hardware/frontPanel/buttonHandler.c: Wired STEP entry and voice/track changes to show the front-page track settings immediately. Repeated presses of the selected VOICE button in STEP mode toggle the front-page half and also trigger stopped voice preview when transport is stopped. `SHIFT+VOICE` now enters persistent morph endpoint edit mode instead of the old shifted VOICE/STEP behavior. PERF SELECT1 now realigns the currently playing/selected pattern to the master step clock. BAR changes ask LED flash for a SELECT overlay without stealing SELECT-row ownership from VOICE subpage LEDs.
- Core/Hardware/frontPanel/ledHandler.c/h: Reworked the existing flash machinery as a group-aware overlay, not as a second flash layer. `led_flashGroup(group, mask)` accepts a 16-bit mask for SELECT, SEQ, MODE, VOICE, BAR, or function-button groups; bits outside the group are ignored. A new flash for a group cancels only that group's prior flash and restores the current base state before starting the new mask. Base LED writes continue during flash, so expiry restores the latest state rather than the state captured at flash start. Existing 400 ms / 80 ms flash timing was retained. `led_setBlinkLed()` start is now idempotent for an already-blinking LED, preventing duplicate blink slots from making the VOICE LED appear steady in SHIFT+VOICE mode.
- Core/Hardware/SD/filesystem.c: Kept old 8-slot bridge streaming for pattern/container files while serializing the one live pattern. Added optional per-track settings extension for length/rotation/scale/MIDI channel/MIDI note and optional per-track shuffle extension. Removed import/export of the old single/global shuffle byte entirely; missing per-track shuffle data leaves tracks at shuffle off. Fixed the directory-kit loader terminal kitset-read phase so EOF advances to close instead of boot-locking. Post-closeout fixup also corrected the selected-kit-directory close phase (`9 -> 10`), the two kitset read/parse error branches (`15 -> 14` so close is actually requested), and standalone pattern-save close (`9 -> 10`), with inline comments documenting the async close-request/wait-close invariant.
- Core/Hardware/SD/storageTypes.c/h: Continued Phase 2 kit text ownership. Reduced `kitset.kcg` handling to kit membership, slot file/type, audio outs, and minimal schema/version handling. Instrument parse supports `[params]` and `[morph]`, with missing morph data falling back to main parameters.
- tools/convert_legacy_kits.py: Rebuilt legacy `Pxxx.SND` to `SD_CARD/Kit` conversion using Slak/P000 as the canary and `ParameterArray.h` as the enum authority. Removed comment-derived assumptions and the old CC/CC2 split inference. The converter maps payload bytes directly by parameter enum, writes both `[params]` and `[morph]`, writes `audio_out` into `kitset.kcg`, and clean-replaces `SD_CARD/Kit` before generation.
- SD_CARD/Kit/: Regenerated generated kit folders from the current root `P*.SND` set. Current tree reflects missing/deleted `P030..P032` and new `P035.SND`; Slak is slot 001 from `P000.SND`.
- PAT_8BAR_SINGLE_AUDIT.md: Consolidated into this log below: bridge plan, corrected flash design, STEP track-settings ownership, kit directory boot-lock fix, reduced kitset schema, instrument morph/padding notes, legacy converter false starts and final Slak-canary rebuild, immediate STEP refresh, and 16-step boot defaults.
- PAT_SUPPLEMENTARY_FEATURES_AUDIT.md: Consolidated into this log below: SHIFT+VOICE morph editing, voice preview, per-track shuffle, storage split, final removal of legacy single-shuffle import/export, implementation order, validation, and code-comment rationale.
- 031_FIXUP.md: Consolidated into this log below: boot hang root cause, state-machine close transition fixes, malformed-kitset close routing, pattern-save close fix, inline-comment rationale, and validation.
- knowledge_files/MODULE_INTERCHANGE_SPEC.md: Updated direct-call boundary map for Session 031 PatternData settings, menu morph/STEP helpers, LED blink idempotence, Sequencer scale/shuffle/preview/realign helpers, and removal of old global shuffle APIs.
- SCOPING_TARGETS.md: Added current Phase 2 bridge status and noted the final LED state consolidation pass as Phase 3.11, before Phase 4.
- MEMORY.md: Updated quick-start context through Session 031 and repository tree.
- knowledge_files/log_archive/000_SESSION_INDEX.md and 031_SESSION_HANDOFF_LOG.md: Added session index entry, cross-session facts, and this handoff.

KNOWN ISSUES INTRODUCED: Pattern/container storage is intentionally provisional and not backward-compatible for the old single shuffle byte. Final Scene/Pattern/Kit save semantics are still unsettled for Phase 2. The broader SHIFT+VOICE, STEP-page, scaled/shuffled timing, and regenerated kit tree interaction matrix still needs a hardware pass beyond the reported boot-fix confirmation. `SD_CARD/Kit` is generated data and the worktree contains large generated diffs plus user-replaced `P*.SND` files. `tools/__pycache__/convert_legacy_kits.cpython-314.pyc` exists from converter validation.
KNOWN ISSUES RESOLVED: STEP track settings no longer lag one voice press; STEP front-page order is length, scale, MIDI channel, MIDI note; empty boot tracks default to 16 steps; VOICE subpage SELECT LEDs persist through BAR flash; only one flash overlay per LED group is active; SHIFT+VOICE edits morph endpoints and the VOICE LED can blink continuously; stopped selected-voice re-press previews voice; per-track shuffle replaces the global runtime shuffle path; the old single shuffle byte is ignored/omitted; directory kitset EOF no longer boot-locks; selected kit directory close no longer self-loops during boot kit load; malformed kitset read/parse errors route through actual close before wait-close; standalone pattern save no longer self-loops on close; Slak-based kit conversion now lands known parameters and audio outs correctly.

NEXT SESSION RECOMMENDED GOAL: Hardware-test the complete Session 031 interaction matrix: SHIFT+VOICE blink/edit, stopped voice preview across modes, STEP front-page half toggle and parameter refresh, per-track shuffle timing, scale/realign behavior, BAR flash over VOICE subpage LEDs, malformed kitset failure behavior, pattern save completion, and regenerated Kit load. Then continue Phase 2 final Scene/Pattern/Kit storage design.
BLOCKERS: Hardware confirmation is needed before treating the interaction/timing pass as closed. Final storage shape and save operations remain intentionally undecided until more Phase 2 work lands; external Python converters should handle final migration once that shape is stable.

CRITICAL REMINDERS FOR NEXT SESSION:
- Do not reintroduce old single/global shuffle import/export. It is intentionally ignored now.
- Flash is an overlay on top of whatever LED mode owns the group. Keep the existing 400/80 timing and keep group cancellation/restore semantics.
- `led_setBlinkLed()` start must remain idempotent; duplicate blink slots can visually cancel blinking.
- STEP track settings are PatternData-owned per-track pattern settings, not Euklid settings.
- `SHIFT+VOICE` edits `parameters2[]`; normal VOICE edits `parameter_values[]`.
- Runtime SD/file work must remain asynchronous. For asyncfatfs reads, bare `n == 0` is not EOF unless paired with `afatfs_feof()`.
- Async file close phases have a strict two-step contract: one phase requests `afatfs_fclose(..., on_file_closed)`, then the next phase waits for `op_close_done`. Never jump directly to a wait-close phase unless a close was already requested, and never advance a close-request phase back to itself.
- New code in `.c` and `.h` files should keep the detailed contract comments standard: why it exists, what it does, inputs/outputs, and clients/accessors/affiliates.
```

## Consolidated Audit Archive

The two PAT audit documents and `031_FIXUP.md` are consolidated here so this
handoff remains complete if those working audit files are deleted.

### One-Pattern / 8-Bar Bridge

Goal and scope:

- Session 031 created the Phase 2 bridge toward Scene work: one live Scene-style
  pattern per track, 128 stored `Step` records, displayed as 8 bars of 16 steps.
- This is deliberately not the Phase 3 dynamic pattern pool or final file
  format. The bridge keeps the existing `Step` struct, public seams, and names
  where possible, and avoids broad renames such as replacing
  `pat_subStepPattern` everywhere.
- The previous audit was judged too much like a final cleanup. The chosen bridge
  reinterprets `pat_subStepPattern[0][track][0..127]` as the live 128-step
  pattern, stops using `pat_mainSteps` for playback/UI truth, and confines old
  8-slot file compatibility to filesystem boundaries.

Storage and PatternData decisions:

- `NUM_PATTERN` is one live pattern. `NUM_BARS` is `8`, `NUM_STEPS_PER_BAR` is
  `16`, and `NUM_STEPS` stays `128`.
- `pat_patternValid()` accepts only pattern `0` for live PatternData.
- `pat_mainSteps` is retained only for legacy/file compatibility. Playback and
  visible editing use `Step` active bits directly.
- `LengthRotate` keeps its historical name for this bridge but is now the
  PatternData-owned per-track settings record: `length`, `rotate`, `scale`,
  `midiChannel`, `midiNote`, and later `shuffle`.
- Track length is real `1..128` step count, not a packed 4-bit old length.
  `pat_setTrackLength()` clamps to `1..128`; readers return real step counts;
  missing/zero loaded legacy lengths defensively fall back to 128.
- Empty/cleared boot tracks now use `PAT_DEFAULT_TRACK_LENGTH =
  NUM_STEPS_PER_BAR`, so a fresh pattern starts as a compact 16-step loop, while
  corrupt/old loaded zero lengths still expand to the full 128-step fallback.
- `pat_clearTrack()` clears all 128 steps inactive and resets length, rotation,
  scale, MIDI channel/note, and shuffle to safe defaults.
- `pat_recordNote()`, `pat_toggleStep()`, and `pat_isStepActive()` operate on
  direct 0..127 step addresses. The old “main step off clears first substep”
  behavior is gone from live editing.

Legacy file bridge decisions:

- Filesystem pattern streaming must not shrink just because `NUM_PATTERN == 1`.
  It uses independent legacy constants for the eight-slot file shape:
  `FS_PATTERN_FILE_PATTERN_COUNT`, step count, main count, settings count, and
  length count.
- Old-format `.pat`, `.all`, and `.prf` streams still read/write legacy-sized
  payloads. Slot 0 maps to the live/staged bridge pattern; slots 1-7 are read
  and discarded on load and written as blank/default bridge records on save.
- Slot 0 main-step masks are retained only as compatibility information.
  Loading old masks can clear groups that old files marked inactive even if
  default substep records contain active-looking data. Saving can synthesize a
  mask from active bridge steps for reload compatibility.
- New bridge saves keep old prefix layout but append provisional optional
  extensions for per-track settings and shuffle. This is not the final Scene
  serializer.

Sequencer bridge decisions:

- `seq_stepIndex[]` is the current 0..127 step per track.
- Playback no longer gates through `pat_isMainStepActive()`. It checks
  `pat_isStepActive(track, seq_stepIndex[i], seq_activePattern)` and evaluates
  probability per actual step.
- Live erase clears the current step, not an old 8-step group.
- `seq_determineNextPattern()` and pattern selection stay clamped to pattern 0
  in this bridge. `seq_activePattern` and `seq_pendingPattern` remain, but the
  live model is one pattern.
- Rotation and length math are direct step counts. Old `* 8` conversion rules
  were removed from wrap/start/rotation-offset paths.

Menu and front-panel bridge decisions:

- `menu_currentBar` tracks the viewed bar. `menu_shownPattern`,
  `menu_playedPattern`, and `menu_getViewedPattern()` remain for future Scene
  view seams but clamp to pattern 0 for now.
- `buttonHandler_selectedStep` remains the selected absolute step.
- STEP1..16 operate on `menu_currentBar * 16 + stepButton`.
- Plain VOICE SELECT1..8 still selects voice subpages. In VOICE mode,
  `SHIFT+SELECT1..8` selects bar 0..7. In STEP/PAT_GEN context,
  SELECT1..8 selects the visible bar.
- BAR1/BAR2 no longer trigger voices. They move the visible bar without
  wrapping, flash SELECT1/SELECT8 again at boundaries, and light their own LEDs
  while pressed.
- Long-press automation arming blinks the visible STEP LED for `step % 16`.
  SELECT-row recorded-substep feedback is suppressed as obsolete bridge
  compatibility.
- COPY+SELECT in VOICE or STEP/PATTERN mode became copy-bar/paste-bar on the
  current track: copy all 16 `Step` records including trigger, note, velocity,
  probability, and automation fields; paste extends track length to include the
  destination bar.

LED bridge decisions:

- STEP LEDs show active state for the 16 steps in `menu_currentBar`.
- SELECT LEDs show current bar only in contexts that own SELECT as bar
  indication. VOICE mode owns SELECT as subpage indication.
- `led_setActive_step()` displays `step % 16` only when the played step is in
  the viewed bar. Chase/record feedback only appears for the visible bar.
- `led_updateRecordedSubStep()` is a documented compatibility no-op.
- `led_initPerformanceLeds()` lights only SELECT1 in the one-pattern bridge.

Euklid and copy/clear bridge decisions:

- Euklid no longer writes a 16-bit main-step mask as live state. It writes
  generated rhythm directly into 128 `Step` active bits and preserves existing
  note/probability/automation fields.
- Euklid length/steps/rotation setters guard invalid track indices and clamp to
  active 1..128 bridge range.
- Whole-track copy/clear still operates on all 128 steps.
- Old pattern copy is meaningless with one live pattern and was replaced by the
  copy-bar gesture above.

Bridge validation and watchpoints:

- Intended checks included build, `sizeof(PatternSet)` shrinkage, old
  `.pat/.prf/.all` slot-0 load compatibility, legacy-sized saves with blank
  slots 1-7, bar navigation, 0..127 sequencer wrap, 1..128 length editing and
  save/load, Euklid across 128 steps, copy-bar length extension, visible-bar
  chase/record feedback, one-step erase, and inactive PERF SELECT2..8.
- Early notes said `make` was unavailable in one environment. Final Session 031
  validation did run `make` and `make img`.

### Clock, Scale, Flash Overlay, And Realign

Timing decisions:

- The corrected default pattern grid is 4 steps per beat. Internal scheduler
  timing is 96 PPQ, so one default step is 24 internal PPQ ticks.
- MIDI clock output remains 24 PPQ and is generated independently from the
  internal phase clock; slowing the pattern grid must not slow external MIDI
  clock.
- External MIDI sync advances four 96-PPQ scheduler ticks per MIDI clock.
  Trigger-jack sync still uses the legacy native 32 PPQ prescaler mapped to the
  scheduler by multiplying by 3.

Track scale decisions:

- `PAR_TRACK_SCALE` is a Pattern-owned per-track setting. It changes playback
  timing, not stored step data.
- Menu order is centered around `off`:
  `/8 /7 /6 /5 /4 /3 /25 /2 /.6 /.3 off x.3 x.6 x2 x25 x3 x4 x5 x6 x7 x8`.
- Ratios are exact small rationals: off `1/1`, `x.3` `4/3`, `x.6` `5/3`, `x2`
  `2/1`, `x25` `5/2`, `x3..x8`, and slow inverses including `/.3` `3/4`,
  `/.6` `3/5`, `/25` `2/5`, `/2.. /8`.
- Fast scales must play every traversed step at the finest scheduler tick
  resolution; they must not skip intermediate steps or collapse them into one
  landed trigger.
- Fractional scale timing is derived from absolute PPQ/event position so it
  does not drift across repeated 128-step loops.
- `PAR_TRACK_SCALE`, `PAR_TRACK_MIDI_CHAN`, and `PAR_TRACK_MIDI_NOTE` were
  added before globals, so they are menu/runtime parameters rather than saved
  globals. `PAR_TRACK_SCALE` uses `DTYPE_MENU` table id 0.

Scaled scheduler implementation:

- The sequencer now uses a 96-PPQ elapsed tick counter and per-track event
  counts. `seq_trackEventBaseTick()` converts a track event index to absolute
  PPQ due tick using `pat_getTrackScaleRatio()`.
- `seq_dueTrackEvents()` counts every unprocessed event whose due tick has
  arrived, allowing fast tracks to visit all intermediate steps.
- Reset/start/pattern-change paths clear the scaled scheduler state.

Pattern realign:

- `seq_realignActivePatternToMasterClock()` is a performance action. It
  rewrites Sequencer runtime counters/positions from the master timing reference
  without changing PatternData.
- In PERF mode only, SELECT1 realigns the one active/selected pattern. SELECT2-8
  remain inactive placeholders during the one-pattern bridge.
- Realign is ratio-based so fractional scales land where uninterrupted playback
  from master clock zero would have landed.

Flash overlay:

- Flash is a general overlay, not LED ownership state. Any supported group may
  request a 16-bit mask: SELECT, SEQ, MODE, VOICE, BAR, or FUNCTION.
- The live implementation keeps the existing 400 ms duration and 80 ms cycle.
- Starting a new flash for a group cancels only that group’s previous mask,
  restores/render the current base state for the old mask, and starts the new
  temporary mask.
- Base LED writes continue to update remembered state during flash. On expiry,
  `led_reset()` restores the latest base state, not a snapshot from flash start.
- `led_flashLed()` remains a compatibility wrapper, but obvious callers use
  `led_flashGroup()`.
- BAR changes request SELECT flash feedback but must not take persistent
  SELECT-row ownership unless the current UI context uses SELECT as bar row.

### STEP Track Settings Ownership

Purpose:

- STEP front page is a PatternData-owned track settings page, not Euklid
  generator storage and not a transient collection of kit/global aliases.
- It remains `SEQ_PAGE` subpage 0 because STEP mode is the user entry point.

Behavior:

- Entering STEP mode or changing active track while `SEQ_PAGE` is visible shows
  the track settings front page and refreshes from PatternData before repaint.
- Selecting a concrete STEP1..16 moves to the per-step editor and keeps it there
  until STEP mode is re-entered or the active track changes.
- First half order is length, scale, MIDI channel, MIDI note. The second half
  carries per-track shuffle.
- Plain VOICE button changes while `SEQ_PAGE` is displayed re-enter the
  track-settings front page and repaint from PatternData even if later gestures
  reach that page from outside strict STEP mode.

Ownership:

- `PAR_TRACK_MIDI_CHAN` and `PAR_TRACK_MIDI_NOTE` are view/edit aliases. Edits
  write PatternData first through setters/getters. Legacy `PAR_MIDI_CHAN_*` and
  `PAR_MIDI_NOTE*` arrays may be mirrored for compatibility, but are no longer
  the source of truth for the STEP front page.
- Sequencer MIDI output and MIDI input note/channel matching read the active
  pattern track’s MIDI channel/note from PatternData.
- Euklid generator settings remain on `EUKLID_PAGE` and keep their own arrays.

Storage:

- The old one-byte track-length block remains in place.
- A versionless optional four-byte per-track settings extension follows it:
  `rotate`, `scale`, `midiChannel`, `midiNote`.
- Old files that stop after length load safe defaults for the newer fields.
  When old files omit MIDI channel/note, loader defaults come from valid legacy
  MIDI parameters where possible to preserve old kit/pattern behavior.
- The whole `LengthRotate` settings record must copy through staging/temp
  pattern paths.

### Phase 2 Kit Directory And Converter Work

Directory kit loader:

- Boot with SD card blocks in `main.c` through: kit scan, `preset_loadDrumset(0,
  0)`, then globals load. The kit load uses `filesystem_loadKitDirectory_tick()`.
- Earlier Session 031 fixed terminal `kitset.kcg` read outcomes in case 13 so
  EOF/finalize success/failure and read errors advance to close phase 14 rather
  than looping on phase 13.
- `op_close_status` preserves `FS_STATUS_DONE` or `FS_STATUS_ERROR`; phase 14
  closes `kitset.kcg`, phase 15 waits for close, and the preset-manager callback
  can leave `PRESET_LOAD_IN_PROGRESS`.
- Inputs are streamed lines from `Kit/NNN Name/kitset.kcg`, parser results from
  `storage_kitsetParseLine()` / `storage_kitsetFinalize()`, and the async close
  callback `on_file_closed()`.

Reduced `kitset.kcg` schema:

- `kitset.kcg` is only a kit-folder guard plus six voice-slot manifest sections.
- It contains `format=helicase.kitset`, `version=1`, and six `[slotN]` sections
  with `type`, `file`, and `audio_out`.
- Folder names such as `001 Slak` own kit display names. The loader gets the
  name from the scan cache and initializes `PAR_VOICE_DECIMATION_ALL` to `127`.
- Parser/converter no longer consume or emit `kit_name`,
  `voice_decimation_all`, `[metadata]`, `source_name`, `source_file`,
  `legacy_slot`, `legacy_trailing_hex`, or trailing legacy metadata in
  `kitset.kcg`.
- Scan still populates the legacy `kitBrowser` map; runtime clients can use
  `filesystem_kitSlotExists()` and `filesystem_kitSlotName()`.
- Validation during the session rewrote generated kitsets and found no removed
  keys in mirrored `kitset.kcg` files.

Instrument files:

- Generated `.drm`, `.snr`, `.cym`, and `.hat` files have header metadata,
  `[params]`, `_padNN=0` rows through 64 byte-valued entries, `[morph]`, and
  another 64-entry padded endpoint.
- Legacy `.SND` files have no distinct morph endpoint, so generated morph data
  initially mirrors the main endpoint.
- Loader writes `[params]` into `parameter_values[]` and `[morph]` into
  `parameters2[]`. If explicit morph is missing, it copies main to morph via
  fallback.
- `storage_instrument_state_t` tracks `seen_param_count` and
  `seen_morph_count`.
- Runtime clients are `filesystem_loadKitDirectory_tick()` and
  `storage_instrumentParseLine()`; eventual directory-kit save should emit the
  same sections.

Converter history and final state:

- Several converter approaches were tried and documented. A CC2-split model
  based on `PAR_FILTER_DRIVE_1` was validated for some generated references,
  then parameter assignment was temporarily removed when inferred mappings still
  disagreed with known hardware values.
- Final Session 031 converter rebuild used Slak/P000 as canary and established
  direct payload indexing by live `ParameterArray.h` enum value after the
  eight-byte name:
  `file_offset = 8 + PAR_* enum value`.
- Verified spot values included `PAR_COARSE1=31`, `PAR_FINE1=126`,
  `PAR_VOL1=127`, `PAR_VOL6=127`, `PAR_FILTER_DRIVE_1=30`,
  `PAR_FILTER_TYPE_1=5`, `PAR_AUDIO_OUT1=2`, and `PAR_AUDIO_OUT6=1`.
- `audio_out` belongs in `kitset.kcg`, not instrument files. Slak routes slot 1
  to `audio_out=2`, slots 2-5 to `0`, and slot 6 to `1`.
- The converter strips C comments, parses actual `ParamEnums` from
  `ParameterArray.h`, keeps storage maps aligned with `storageTypes.c`, emits
  `type/file/audio_out` in kitsets, emits voice-owned `[params]` keys in
  instruments, initializes `[morph]` from the same values, and clean-replaces
  `SD_CARD/Kit/` on each run so stale folders/host metadata do not survive.
- Generated output reflects the current source `P*.SND` set. At one point 33
  mirrored kitsets/198 instrument files were validated; final regeneration from
  the current source set produced 31 kit folders, with source slot numbers
  preserved rather than compacted. Current tree reflects missing/deleted
  `P030..P034` and new `P035.SND`, so folders jump to `036 1ShtSnr`.
- `Core/MIDI/MidiMessages.h` appears drifted around the MIDI-size boundary and
  was explicitly not cleaned up here. The final converter uses
  `ParameterArray.h` as authority for emitted values.

### Supplementary Features

SHIFT+VOICE morph parameter mode:

- `SHIFT+MODE_VOICE` now enters persistent morph voice mode instead of shifted
  mode arithmetic or the old held-SHIFT temporary STEP overlay.
- Visible mode remains `SELECT_MODE_VOICE`; morph is an overlay flag, not a new
  select mode. This preserves VOICE page/subpage/track semantics.
- `buttonHandler` owns the gesture and blinking VOICE/MODE1 LED feedback.
  `Menu` owns `voiceModeShowMorph` and parameter-buffer resolution.
- In morph voice mode, VOICE pages display/edit `parameters2[]` for eligible
  sound parameters on voice pages; normal mode uses `parameter_values[]`.
  Pattern/global/runtime aliases must not read from `parameters2[]`.
- Encoder and endless-pot edits write morph endpoint values and call
  `preset_morph(parameter_values[PAR_MORPH])` to refresh interpolation without
  applying morph endpoint values as active-kit writes or recording automation.
- Leaving to non-VOICE modes clears morph view and blink. Holding SHIFT alone in
  VOICE no longer steals UI into STEP mode.
- No new file format is required: morph mode edits the existing morph-kit buffer
  used by morph load/save.

Voice preview:

- If transport is stopped, re-pressing the already selected VOICE/track previews
  that voice. Pressing a different voice changes selection first and does not
  preview until re-pressed.
- Running transport suppresses preview.
- Preview is skipped for copy/mute/clear/destructive modified gestures.
- `seq_previewVoice()` owns the actual trigger path so buttonHandler does not
  duplicate voiceControl/MIDI/trigger-jack logic. It does not advance
  `seq_stepIndex[]`, write pattern data, record automation, or mutate transport
  counters.
- Preview note uses PatternData track MIDI note, then existing MIDI override,
  then `PAT_DEFAULT_NOTE`; volume uses `ROLL_VOLUME`.
- Repeated selected VOICE in STEP mode can both toggle the STEP front-page half
  and preview when stopped.

Per-track shuffle:

- Shuffle moved from global/transport state into `LengthRotate.shuffle`, the
  PatternData-owned per-track settings record.
- `PAR_SHUFFLE` remains the public menu parameter for now. On STEP front-page
  second half it edits `pat_setTrackShuffle(menu_getViewedPattern(),
  menu_getActiveVoice(), value)`.
- PERF no longer exposes shuffle, avoiding a misleading global shuffle slot.
- `pat_setTrackShuffle()` validates/clamps `0..127`, stores in PatternData, and
  mirrors `PAR_SHUFFLE` for display. It must not forward to a global sequencer
  coefficient. `pat_getTrackShuffle()` returns track value or `0` for invalid
  coordinates.
- `pat_applyTrackSettingsToMenu()` refreshes `PAR_SHUFFLE` from the active
  pattern/track.
- `seq_calcDeltaT()` remains a uniform 96-PPQ transport tick. Per-track shuffle
  is applied in per-track due-event timing, not by mutating the global tick
  duration.
- `seq_trackEventShuffleOffset()` derives a non-negative delay from absolute
  event timing, existing `seq_shuffleTable[]`, and
  `pat_getTrackShuffle()`. Because due ticks are absolute event-count math,
  shuffle does not accumulate drift or stale residuals across realign/loop.
- Standalone `.pat` and `.all/.prf` save/load paths removed the old one-byte
  global shuffle phase entirely by follow-up decision. External Python
  converters will handle final migration once Phase 2 storage shape settles.
- The existing four-byte track-settings extension remains four bytes. Per-track
  shuffle is a separate one-byte-per-track optional append block after it, so
  earlier four-byte-extension files are unambiguous.
- EOF before settings or shuffle extensions is valid and leaves defaults; EOF in
  optional shuffle closes as done without carrying partial stream offset into
  later phases.
- `FS_CONTAINER_VERSION` stays `2` because the new data is append-only after
  the required pattern payload prefix.

Supplementary validation:

- `make` and `make img` passed. Remaining output was existing newlib nano
  syscall linker warnings (`_close`, `_lseek`, `_read`, `_write`) plus LTO
  serial-compilation notes.
- Recommended hardware matrix remains: SHIFT+VOICE values/blink/edit/save,
  selected voice preview across modes, STEP half toggle and track refresh,
  per-track shuffle independence, slow/fast scales with shuffle, pattern
  realign determinism, no 128-step drift, new save/load restoring different
  per-track shuffle values, old single-shuffle byte ignored, old files without
  extensions loading safely.

### 031 Fixup

Problem:

- After closeout, boot hung despite the previous kitset EOF fix. The failing
  boot path is still the synchronous kit-0 load:
  `main.c` calls `preset_loadDrumset(0, 0)`, preset manager starts
  `FS_INTERNAL_OP_LOAD_KIT`, `filesystem_tick()` dispatches
  `filesystem_loadKitDirectory_tick()`, and boot waits while preset status is
  `PRESET_LOAD_IN_PROGRESS`.
- The loader opens `Kit/`, enters the selected `Kit/NNN Name/` directory, then
  closes the selected kit directory handle before opening `kitset.kcg`.
- Case 9 accidentally advanced back to case 9 after `afatfs_fclose()` accepted
  the async close. Case 10 is the wait-close phase. Re-entering case 9 repeats
  the close request and prevents the callback/completion path from firing.

Fixup code changes:

- In `filesystem_loadKitDirectory_tick()` selected-kit-directory close, changed
  close-request phase case 9 from `op_phase = 9` to `op_phase = 10`.
- In the same loader, changed the two `kitset.kcg` read/parse error branches
  from jumping directly to wait-close phase 15 back to close-request phase 14.
  A malformed/open file must request `afatfs_fclose()` before waiting for
  `op_close_done`; otherwise it can wait forever for a callback that was never
  scheduled.
- In `filesystem_savePattern_tick()` standalone pattern save close, changed
  case 9 from `op_phase = 9` to `op_phase = 10`. This was not the boot hang,
  but any pattern save could hang after writing payload by self-looping in the
  close-request phase.
- Inline comments were added next to all three fixes to make the async close
  invariant visible: request close in one phase, wait for `on_file_closed()` in
  the next phase, finish only after `op_close_done`.
- The fixup does not reintroduce the legacy single-shuffle import/export path.
  The boot hang was filesystem phase transitions, not SHIFT+VOICE blink or
  shuffle scheduling.

Fixup validation:

- `make` passed after the fixup and after adding inline comments. Warnings were
  the same existing newlib nano syscall/LTO notes.
- User then reported the fixup seems to work on hardware.

## Validation

- `make` passed after the code changes.
- `make img` passed and regenerated `build/LXRV2_lxr02.img`.
- `git diff --check` passed after documentation/log closeout.
- Post-closeout fixup validation: `make` passed after the close-phase fixes and
  inline comments; user then reported the boot-hang fixup seems to work on
  hardware.
- Remaining build warnings are the existing newlib nano syscall warnings and LTO
  serial-compilation notes.

The full Session 031 interaction matrix still needs hardware testing. The
reported hardware confirmation applies specifically to the post-closeout boot
hang fixup.
