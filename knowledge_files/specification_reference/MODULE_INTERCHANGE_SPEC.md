# Module Interchange Spec

This is the current direct-call ownership and API-boundary map through Session
082 (S081 documented after S082), including typed HCNAMES, `.fx` Effect storage, AutoSave boot restore, typed Instrument-index
repair, AsyncFATFS directory publication, the Phase 4 dynamic Pattern storage
system, step automation editing/playback (Session 065), the VOICE-page
held-step automation overlay (Session 066), the Pattern Stack Service with
dtype offset bug fix (Session 067), the Session 068 front-panel event-ring
rebuild, played-Pattern mirror fix, and per-track sequencer length fix,
Session 069 bounded CPU convergence, Session 070 systems fitness pass, and
Session 071 per-Scene voice-edit mask, base-independent LFO voice-morph
contribution, Scene superpage live display, LED chase state defect fix, and
LFO target voice handler fix, the Session 072 Effects bus, the Session 073
changes (boot image check, sample floor, special-writer tags, one-pass mixer
dry + send), and the Session 074 additions:

- the CrumpBit Effect type and the Effect-page hook/layout extensions;
- the Effect-page automation underlines;
- the master bus compressor (`Core/DSPAudio/BusCompressor`);
- the `xfd` fader mode;
- the AutoSave torn-record validation step;

and the Session 075 additions:

- Phase 6 copy/clear (`Core/Menu/CopyClear/`, reference
  `COPYCLEAR_UTILITIES.md`) with the PatternData raw block API, the
  PatternStackService exclusive boundary, the BankData/SceneData/Preset/
  EffectsManager whole-object helpers and the filesystem name-buffer loan;
- the retirement of global `srt` (PERF `fxm` in its cell);
- the FX-send Morph endpoint, the VOICE hold-SHIFT Morph view and the
  Effect-page SHIFT+TRACK voice-mix overlay (F2);
- the automation-priority fix: `seq_automationHoldsParameter()`, the guarded
  Morph-base write, one-parameter menu edits and the external-MIDI endpoint
  entry (F3);

and the Session 076 additions:

- Scene parameter automation override clear rules (P1): Rule A
  (non-automation writes clear their override) and Rule B (Scene activation
  clears all overrides via `seq_clearSceneAutomationDirty()` and inline
  clears in `preset_applySceneSettings()`);
- LFO Scene-reset retrigger `scn` (P2): `LFO_RETRIGGER_SCENE` (7u),
  `captureLfoPhases()`, `restoreLfoPhaseIfNeeded()`, and
  `IM_SPECIAL_LFO_OFFSET` for phase-offset scaling;
- copy/clear morph additions (P3): `preset_resetSlotMorphToNormal()`,
  `preset_copySlotNormalToMorph()`, `effects_resetMorphToNormal()`,
  `effects_resetMorphToNormalSingle()`, new clear/copy selections;
- reload scene, bar chaselight, SHIFT+SELECT pattern length (P4):
  `CC_CLEAR_SCENE_RELOAD`, `led_updateSelectBarChaselight()` (static),
  `handleSelectButton()` SHIFT+STEP split.

and the Session 078 additions (per-track timing, `PATTERN_DYNAMIC_STACK.md`
§6.4 and §6.3a; Morph rules in `BANK_PRESET_ARCHITECTURE.md`),

and the Session 079 additions (step trigger conditions, per-step roll,
micro-timing, P2 remediation):

- 7-special pool block: `PAT_SPECIAL_COND_BIT` (was `PROB`), `ROLL_BIT`,
  `TIMING_BIT`, `RHYTHM_BIT`, `FILL_BIT`; `pat_step_specials_t` widened;
  `pat_rawEncode()` takes `const pat_step_specials_t *`;
- `pat_setStepCondition()` (was `pat_setStepProbability()`),
  `pat_setStepRoll()`, `pat_setStepTiming()`, `pat_setStepRhythm()`,
  `pat_setStepFill()` — PatternData step-special setters;
- `patSvc_setStepCondition()` (was `patSvc_setStepProbability()`),
  `patSvc_setStepRoll()`, `patSvc_setStepTiming()`,
  `patSvc_setStepRhythm()`, `patSvc_setStepFill()` — service wrappers;
- `menu_toggleStepEditSubpage()` — flips SEQ sub 1 ↔ 2 on step re-press;
- `instrumentManager_requestGateDuck(trigger_track)` — arms a ~5 ms
  output-gain fade for the roll fill boundary (replaces the removed
  `instrumentManager_applyTriggerGateDecay()`);
- `instrumentManager_gateGain(slot)` — read by the mixer each block;
- **Removed:** `instrumentManager_applyTriggerGateDecay()` (decay mutation
  was root cause of stuck-decay bug), `voiceControl_noteOnGated()` and
  bit-7 velocity packing (gated-trigger plumbing),
  `seq_triggerVoiceGated()` (roll engine fires plain triggers only);
- `seq_fillActive` — fill mode flag, set by buttonHandler PERF hold;

- StepScale's 128-position Q8.8 curve (`stepScale_ticksQ8()`,
  `stepScale_formatShort()`, `stepScale_isMusicalStop()`) shared by Pattern
  tracks, the FX sequencer and CrumpBit;
- sequencer per-track DDA, shuffle, run modes and the effective timing cache
  (`seq_refreshTrackEffectiveParams()`);
- `pat_setTrackPlayMode()`, `scene_setTrackMorphLength/Scale/Shuffle()`,
  `presetMorph_getTrackEffective*()`, `presetMorph_setTrackParamStepOverride()`
  / `presetMorph_clearAllTrackParamStepOverrides()`, three Scene target kinds
  (IDs 405..425);
- STEP-page UI: `menu_setPatternTrackMorphEndpoint()` (momentary SHIFT Morph
  view), `menu_enterStepTrackAutomationOverlay()` (held steps + TRACK),
  the multi-step specials broadcast, the STEP automation underline search and
  `copyClear_isClearMode()`.

and the Session 080 additions (PERF performance surface, see the section of
that name below; engine timing in `PATTERN_DYNAMIC_STACK.md` §6.5/§6.6):

- the master-clock-anchored PERF looper (`seq_loopEngage/UpdateMask/
  Disengage/UpdateDivision()`, `seq_loopableTrackMask()`), loop trigger
  conditions `lop`/`!lp` (41-entry table) and the per-track Loopable flag
  (`pat_setTrackLoopable()` / `pat_trackLoopable()`);
- direct/queued scene change divisions, queue actions and the one PC reset
  (`seq_requestSceneChange()`, `seq_queueSceneChange()`, fire-flag
  accessors, `seq_cancelPendingDirectChange()`, `seq_queuedSceneTarget()`,
  `menu_serviceDeferredSceneChange()`);
- scene chaining (`scene_setChainCondition/Next()`, TIM3 chain engine,
  `seq_chainRepeatSelf()`, chain editor `menu_chainMenuOpen/View/Close/
  IsOpen()`);
- the BAR VOICE-trigger overlay, the latched SHIFT+REC settings menu and the
  SHIFT+PLAY+SEQ chord in buttonHandler;
- the LED fast-blink layer (`led_setFastBlinkLed()`, `led_getFastBlinkLed()`,
  `led_isBlinkLed()`); removed `led_initPerformanceLeds()`.

and the Session 081 additions (live record and live erase, automation
priority R12/R13, automation ramps, loop while record, VOICE bar pulse;
module walkthrough in "Core/Sequencer/LiveRecord (S081)" below; engine
rules in `PATTERN_DYNAMIC_STACK.md` §6.1, §6.2a, §6.2b, §6.5, §6.7 and
`EFFECTS_BUS_REFERENCE.md` §6/§10; session record
`081_SESSION_HANDOFF_LOG.md`):

- the new LiveRecord module (`liveRec_*`) and its Sequencer services
  (boundary ring, capture, placement, quantiser, echo/live-hold/erase
  masks, epochs, drain filter), the Pattern Stack Service `RECORD_STEP` /
  `ERASE_SPECIALS` operations and target override, `pat_recordStep()` /
  `pat_eraseSpecials()`;
- the step reset rule: `voiceControl_pendingReset`,
  `voiceControl_noteOnNoReset()`, `voiceControl_resetOnly()`,
  `seq_triggerVoice(…, reset)`, `seq_fireDeferred()`;
- R12 `seq_menuOverrideSet()` and the per-target Scene override clears;
  R13 `menuHold`, `effects_lockClearLayers()`, the FX realign
  (`seq_fxRealignToMasterClock()`, `seq_requestEffectOverlayReset()`); the
  Effect step markers and owner state removed;
- the 96-PPQ roll table, `QUANT_TRACK`, `PAR_ROLL` retired;
- automation ramps (`track_auto_smooth`, `pat_setTrackAutoSmooth()`,
  `pat_findStepAutomation()`, `PAR_TRACK_AUTO_SMOOTH`, `seq_rampService()`);
- REC tap/hold and the live-erase chord in buttonHandler, the `rtk` cell,
  the record/erase menu hooks and `menu_automationTargetRecorded()`;
- loop while record (jump entries, `seq_placeLoopImage()`, the PERF hold
  rules); the VOICE-mode bar pulse and the wrap-safe LED deadlines.

and the Session 082 additions (Load/Save cleanup, background Bank load,
Scene 17, work while loading; walkthrough in "Background Bank load (S082)"
at the end of this document):

- two-mask Bank operations `preset_loadBank(slot, act, empty, flags)` /
  `preset_saveBank(slot, act, empty)`, the shared empty-Scene primitive
  `scene_resetResidentToEmpty()`, edit-mask merge helpers, the Bank child
  content preview and its cancel API;
- Scene 17 storage and mapping (`scene_background*()`, index 16 through
  `scene_get*()` and `pat_regionFor()`), 32-bit fan-out
  (`bank_editTargetMask()`, `bank_sceneFanoutMask32()`);
- the background Bank load (`preset_requestBackgroundBankLoad()`,
  `preset_tickBackgroundBankLoad()`, `preset_backgroundBankLoadRunning/
  Unlocked/Locked()`, `preset_takeBackgroundBankLoadResult()`), the Scene 17
  switch/move/release in Sequencer, `effects_rebindSourceScene()`;
- MIDI-origin UI entry (`buttonHandler_enterLoadSaveForBackgroundLoad()`,
  `menu_backgroundBankLoadUiBegin/UiIdle/UiPoll()`), the MIDI drop filter
  `midiIn_acceptedDuringBankLoad()` and `CC_BANK_SELECT_LSB`;
- the Scene 17 name capture (`filesystem_backgroundNamesCaptureScene()`,
  `filesystem_backgroundNameCaptureInstrument()`,
  `filesystem_backgroundNameSet()`);
- load availability in BankData (`bank_beginLoadAvailability()`,
  `bank_noteLoadChildDone()`, `bank_endLoadAvailability()`,
  `bank_sceneAvailable*()`), the progressive Bank Load, the frozen Load/Save
  screen, the two-slot fast blink (`led_setFastBlinkSlot()`), and the
  copy/clear and chain gates during a load.

The DSP internals behind these APIs are described in
`INSTRUMENTS_DSP_REFERENCE.md` and `EFFECTS_MIXER_DSP_REFERENCE.md`.
Historical migrations belong in session logs; this document states which live
module owns each call, state transition, and retained object.

## Rules

- UI code calls the owner module directly.
- Pool-mutating Pattern/track/step/automation edits go through `patSvc_*`
  (the Pattern Stack Service, Session 067). Direct `pat_*` mutation calls are
  reserved for PatternData internals and the service itself. Non-mutating
  reads (e.g. `pat_readStepSpecials`, `pat_readStepAutomations`) remain
  direct `pat_*` calls.
- Live recording and live erase go through LiveRecord (`liveRec_*`, S081).
  The Sequencer never calls LiveRecord; LiveRecord never calls buttonHandler
  or MidiNotes and calls Menu only for `menu_automationTargetRecorded()`.
  The step-boundary ring has one producer function (`seq_recRingPush()`).
- Automation priority: Instrument automation holds until the voice's next
  resetting trigger, except against a menu edit of that parameter (R12,
  `seq_menuOverrideSet()`); Effect parameters resolve menuHold > lock >
  overlay > menu (R13). Ramp samples are automation writes
  (`seq_writeInstrumentAutomation()`).
- LED rendering goes through `led_*`.
- Transport/playback timing goes through `seq_*`.
- Sound parameter application goes through Preset.
- MIDI runtime configuration goes through MidiIn (`ch`, RXF/TXF, MRT);
  received messages are consumed by MidiNotes/MidiControl/MidiProgram/MidiLearn
  and transmitted through MidiOut only. A MIDI parameter write is a menu edit
  and goes through `menu_commitTargetEdit()` (S083 Q1).
- Filesystem may serialize Pattern data through PatternData pointer accessors.
- Filesystem owns async SD/FAT access; storage text schemas and parameter maps
  live in `storageTypes.c/h`.
- Sequencer may read pattern data through narrow PatternData playback helpers;
  it must not index PatternData storage arrays directly.
- PAT4 readers stream into one selected live Scene and commit/fan out only
  after validation. Pattern AutoSave streams from PatternData's one immutable
  snapshot; there is no second staging Scene or per-Scene snapshot array.
- Preset code lives under `Core/Bank/Scene/Preset/`, but public API names remain
  `preset_*`, `parameterArray_*`, and `paramArray_*` for this mechanical move.
- Normal root Kit loads and saves are directory-based. Kit Morph Load and
  Instrument Morph Load copy normal source endpoints into morph endpoints for
  same-type slots; mismatched slots are no-change. Root Instrument Save writes
  one resident voice to the root Instrument pool. Kit/Instrument Morph Save
  writes the current per-voice interpolated value into both normal and morph
  endpoint fields.
- Scene and Bank load/save are directory-based. Bank is the Session 040
  16-Scene workspace.
  Root Scene is a library/pool. Root Bank is the 16-resident-Scene workspace
  selector; its present/edit masks and active Scene are BankData state.
- Numbered library slots are direct `000..999`; slot `000` is real. This does
  not change instrument file voice coordinates, which remain one-based `1..6`.
- Instrument, Kit, root Scene, and root Bank browser names use one physical
  shared SRAM cache of 1,000 nine-byte rows. Kit/Scene/Bank rows are direct
  slot positions; Instrument rows are sorted browser positions. Type changes
  replace the cache domain. Entering an Instrument type opens its `.hcindex`
  directly; missing/empty/corrupt metadata rebuilds only that type, while
  genuine FAT/SD failures remain terminal errors.
- Root `/.hcnames` is the authoritative fixed-order resident identity and
  provenance register. A dedicated 1,305-byte name mirror and the
  filesystem-owned 322-byte source register retain its 161 rows independently
  of the browser cache.
  Its physical file is a mandatory `#types<TAB>drm<TAB>snr<TAB>cym<TAB>hat`
  header plus 161 rows; Instrument rows carry a mandatory three-byte type
  column, rows 129..144 own Pattern identity, and rows 145..160 own Effect
  identity. Load/Save commits publish
  name, source, and refreshed state for the complete hierarchy they committed.
  The sole active identity block is 81 bytes: BankData's Bank row plus
  filesystem's Scene, Kit, and six Instrument rows. SceneData stores no name or
  filename text.
- HCNAMES creation requires a complete case-insensitive root absence proof.
  A NULL read open, duplicate match, or failed scan/close remains an error and
  cannot authorize creation or automatic repair.
- The 9,000-byte cache never aliases payload validation. A separate aligned
  2,048-byte union stages one Kit, one Instrument candidate, or Scene settings
  plus embedded Kit. Pattern is excluded and reads directly into final Scene
  SRAM after the non-Pattern commit.
- Boot reader Instrument types never alias that destructive stage. All 96 are
  retained through the final Scene in the alternate view of the existing
  144-byte Bank-child scratch; its 16x9 Bank-name view is used only after a
  reader returns and canonical Bank Load starts.
- Boot writes `/Kit/.hcindex`, `/Scene/.hcindex`, `/Bank/.hcindex`, and each
  registry-owned Instrument index. After Instrument index generation disposes
  the shared cache, boot reloads `/Bank/.hcindex`, validates AutoSave candidates
  when enabled, then selects the matching-winner reader, gated
  HCNAMES-authoritative reader, or canonical library ladder.
- Successful Kit, root Scene, and root Bank saves perform a physical parent
  rescan and complete `.hcindex` rewrite before the original save callback is
  released; Menu then refreshes the current Save slot display.
- Pure root Scene and Bank Loads never enter that Save-owned physical rebuild.
  After payload and HCNAMES commit, Preset/Menu applies the loaded active Scene,
  then Menu reloads the unchanged selected `.hcindex` read-only before ending
  `...` and releasing input. Its direct final-index callback snapshots the
  terminal result, calls `filesystem_ack()`, then releases Menu; without that
  acknowledgement the idle-only trace and AutoSave schedulers remain blocked
  at `FS_STATUS_DONE`.
- Load:Bank index completion is only the first browser boundary. Menu continues
  directly into a physical child preview for the highlighted Bank and holds
  input until that callback publishes the `00..15` destination mask.
- Bank Load and Bank Save call `filesystem_markSettingsDirty()` beside their
  committed `bank_setRestoreBankSlot()` update. The existing filesystem-owned
  settings scheduler owns asynchronous `active_bank` serialization; Menu,
  BankData, and AutoSave do not open or write `settings.cfg` directly.
- Combined Kit/Instrument menu entry reads one Scene's Kit plus six Instrument
  identities once and family exit performs at most one HCNAMES rewrite. Bank
  Load/Save own a full-register transaction; selective Bank Load overlays only
  requested/present children.
- Root Scene completion accumulates and publishes its Scene, Kit, and six
  Instrument identity/source/refreshed rows as one committed hierarchy. Scene
  Save seeds the child identity store from the source register before writing;
  Bank Save child preparation stages the Kit identity too.
- Session 070 LSR-01 resolved the deferred HCNAMES checkpoint: Kit/Instrument
  HCNAMES dirty state is now mark-and-flushed at every browser domain transition
  (Kit↔Instrument, type switches, VOICE button, Pot-1 entries, Instrument exit).
  Destination state is installed before flush so completion callbacks dispatch
  correctly. A deferred scheduler rung in `filesystem_tick()` drains retained
  dirty masks asynchronously after page exit. `menu_selectionGeneration`
  (`uint8_t`) tags async name/preview callbacks; stale completions are silently
  discarded. OK commit increments generation to freeze displayed name.
- asyncfatfs owns exact-case filename behavior. Product code should use
  filesystem/asyncfatfs object/LFN APIs instead of local FAT/LFN reconstruction.
  Dot-prefixed files are ordinary filesystem objects except macOS AppleDouble
  `._*` entries, which `afatfs_findNextObject()` filters system-wide. Product
  scans explicitly exclude `.hctmp.<ext>` where required.
- Filesystem shape, instrument file shape, descriptor tables, Scene storage,
  menu layout, and DSP propagation are specified in
  `knowledge_files/specification_reference/FILESYSTEM_SPEC.md`.
- AutoSave wire geometry, dirty ownership, scheduling, and extension rules are
  specified only in `knowledge_files/specification_reference/AUTOSAVE.md`.
  Development-mode and diagnostic-file policy are specified only in
  `knowledge_files/specification_reference/DEV_MODES.md`.
- Instrument voice parameter values are Scene-owned descriptor images. Dynamic
  VOICE menu cells resolve through the active slot's instrument descriptor
  layout, not through static `menuPages.h` cells or `parameter_values[]`.
- `parameter_values[]` remains the legacy/global/menu byte store and the bridge
  for non-instrument sound parameters. It is not the canonical store for new
  descriptor-backed instrument parameters.
- `ParameterArray` remains the legacy runtime pointer map used by older flat
  parameter paths (the old `AutomationNode` is gone). It is no longer the
  canonical map for instrument file keys. Descriptor step automation uses canonical 9-bit `instrument_param_id_t`
  targets and 7-bit pool-packed values with identity mapping (stored value =
  parameter value; Session 065 storage, Session 067 dtype fix).
- Descriptor-backed velocity/LFO target storage, menu display, and runtime
  application are live after Session 035 for direct descriptor targets,
  voice-local decimation, per-voice Morph, and Scene Decimation. Direct
  descriptor LFO overlays use InstrumentManager's descriptor-domain adapter and
  the normal descriptor runtime writer; step automation now uses the same
  descriptor runtime writer via `instrumentManager_writeRuntime()` (Session 065).
- Direct descriptor modulation must use the descriptor owner's byte-domain
  runtime writer. Do not write raw DSP members such as `SlopeEg2.decay` for a
  byte descriptor: that bypasses the setter's unit conversion and can turn a
  zero byte value into an indefinitely held envelope. Session 035 fixed this
  for descriptor LFO targets; keep that adapter path as the model for future
  modulation/automation work.
- Modulation-owner scans must enumerate InstrumentManager's current dynamic
  runtime pools. Fixed global source scans are insufficient after freely
  replaceable instrument types; the Instrument Load transaction compensates by
  clearing/rebinding all sources, but normal reset/original-value paths still
  require this migration.
- Descriptor Morph is live after Session 033. The Morph engine walks
  Scene-owned descriptor images and the active slot's descriptor table instead
  of hardcoded parameter lists.
- Scene-level sound modulation targets live in
  `Core/Bank/Scene/SceneModTargets.c/h`: `1vm..6vm`, the retired `srt` row (ID 390, S075 placeholder),
  `1ou..6ou`, `1fx..6fx`, and Effect Morph `fxm` (404). Effect **parameters**
  do not join that namespace. They are block-7 IDs `448 + local`, owned and
  validated by EffectsManager (`effects_targetValid()`), and selected in the
  LFO `fx` namespace (byte 8) or the `fx` step-automation category. See
  `EFFECTS_BUS_REFERENCE.md` §11.
- Live Pattern storage is the dynamic address-array + pool + bit-packed bitmap
  system in `pat_regions`. PAT4 persists the complete object, including
  dynamic note/velocity/probability specials and current track/Pattern
  parameters. Pattern AutoSave uses a separate dirty mask and root A/B files;
  HCPR contains Pattern identity but no Pattern payload.

## PERF performance surface (Session 080, extended S081)

Cross-module behaviour of PERF mode after Session 080; S081 added live
record from the overlay and Morph cells, the REC tap/hold, live erase and
looping while recording. Owners: buttonHandler
(gestures, hold state), Sequencer (timing, TIM3), Menu (pages, executor,
LED owners), ledHandler (LED layers), SceneData/PatternData (retained
settings). Engine maths: `PATTERN_DYNAMIC_STACK.md` §6.5 (looper) and §6.6
(scene change timing, chaining). Session record:
`log_archive/080_SESSION_HANDOFF_LOG.md` §2.

### Button map in PERF

| Gesture | Action |
|---|---|
| SEQ | Scene change at `scd` (instant when stopped); with VOICE held and no loop: per-track Scene assignment (S077); while looping: always a full change; while the chain editor is open: view that Scene's chain |
| SHIFT+SEQ | Queue a change at `qcd` (replaces a pending queue); with the SHIFT+PLAY chord armed: open the chain editor for that Scene |
| SHIFT+PLAY | Arms the chain chord (`bh_chainPlayArmed`). Since S083, SHIFT+PLAY never toggles the transport in **any** mode |
| SHIFT+PLAY+REC | Arms MIDI Learn in any mode (S083): SHIFT, PLAY and REC flash; the next touched addressable parameter shows `LRN`; a valid CC stores the row (`MIDI_SPEC.md` §6.3). While Learn is active a SHIFT, PLAY or REC press cancels it and is consumed |
| SELECT1..8 (hold) | Looper division 1 bar, 1/2, 1/4, 1/8 bar, 1/16, 1/32, 1/64, 1/128; last pressed wins |
| VOICE then SELECT | Loop only the held VOICE tracks |
| SELECT held, VOICE press | Move the loop to the currently held VOICE set (kept tracks keep their loop start) |
| VOICE press, looping, no SELECT held | Add that track to the loop with a fresh loop start (S081 P3 Q1/FQ1); a `lop`-off track does not join but still holds the loop |
| VOICE (looping) | Looper only: never fires the overlay trigger, never toggles a mute, never starts an erase gesture (S081 P3 Q2/Q4) |
| VOICE release (looped track) | Drops that track; it resumes where it would have been |
| BAR1 / BAR2 (looping) | Momentary triplet ×2/3 / dot ×3/2 |
| BAR1 / BAR2 (not looping) | Toggle VOICE-trigger overlay `<` / `>` (one at a time, LED lit). The state `bh_overlayActive` is shared with MIDI: CC2 on G sets it the same way (0 off, 1 `<`, 2 `>`; last action wins), and received MIDI notes perform the overlay in any mode (S083) |
| VOICE (overlay on, no SHIFT) | Fire the overlay trigger (`pb<`/`pn<` or `pb>`/`pn>`); release = note-off. Not while the loop is engaged: the loop pre-empts the overlay (S081 P3 Q2/Q4) |
| SHIFT+VOICE (overlay on) | Normal mute toggle |
| VOICE release (no loop, no overlay, no action during hold) | Mute toggle (S077) |
| REC tap / REC hold (any mode, S081) | tap toggles live record LATCH on release; hold > 300 ms = OVERDUB while held (`PATTERN_DYNAMIC_STACK.md` §6.7.1) |
| SHIFT+CLEAR while armed (any mode, S081) | live erase; SEQ/VOICE presses become erase gestures per mode, except that while the loop is engaged VOICE stays a looper button and SEQ in PERF is consumed |
| VOICE overlay hit while recording (S081) | recorded onto the hit track (single, flam, roll; `liveRec_triggerIn()`), with `fil` while PERF is held |
| `1VM`..`6VM`, `fxm` edits while recording (S081 P3 FQ5) | recorded like the VOICE mix `Nvm` and the Effect Morph; `mrp` is never recorded |
| SHIFT+REC | Latched settings menu (RECORDING_PAGE) — any mode |
| MODE press | Closes SHIFT+REC and the chain editor; leaving PERF force-releases the looper |

The loop is engaged while any looper SELECT, any VOICE or any BAR button is
held (`buttonHandler_loopCheckDisengage()`), only while the transport runs,
and is filtered by each track's Loopable flag. While the loop is engaged,
SELECT, BAR and TRACK (VOICE) always keep their looper functions: a VOICE
press with no SELECT held adds its track with a fresh loop start, a VOICE
release drops its track (S080 D1), and no VOICE fires the overlay trigger,
toggles a mute or starts an erase gesture (S081 P3 Q1/Q4). A VOICE that is
held as an overlay trigger when the loop engages ends that gesture and
becomes an ordinary TRACK hold (S081 P3 FQ2). Live record and live erase are
not turned off: the looper and the record modes run together, and REC works
while looping (S081 P3 L-2). Leaving PERF force-releases the looper and
clears the VOICE holds. Event-ring overflow force-releases the looper and
closes latched menus.

### Latched menus

SHIFT+REC (`bh_shiftRecMenuActive`) and the chain editor
(`menu_chainMenuIsOpen()`): opening keeps them open after every button is
released; every SHIFT release is ignored while one is open (SHIFT LED stays
lit). A SHIFT press closes the open menu and arms `bh_shiftReleaseConsume`,
so its release only turns the SHIFT LED off and does not run the
mode-specific SHIFT release path (whose PERF branch re-enters
PERFORMANCE_PAGE). REC also closes SHIFT+REC. `handleModeButtons()` closes
both first.

### Pages

| Page | Content |
|---|---|
| PERFORMANCE_PAGE | sub-page 0 `mrp fxm 1VM 2VM`, sub-page 1 `3VM 4VM 5VM 6VM`; entry lands on `mrp` |
| RECORDING_PAGE (SHIFT+REC) | sub-page 0 `pb< pn< pb> pn>` (Prfrmnc); sub-page 1 (S083 follow-up) `pv< pv> qnt rtk`: `pv<`/`pv>` (Prfrmnc Rhythm</>) the overlay roll velocity rhythm, the step `rtm` list (`off`, `1-2`..`4-5` halve, `1+2`..`4+5` double; single/flam types ignore it), `qnt` (`off 1/8 1/16 1/32 1/64 trk`, S081 adds `trk`), `rtk` (RecTrk, recording track 1..7, runtime only, S081); 2..7 reserved |
| PATTERN_SETTINGS_PAGE (chain editor) | sub-page 0 `cn1 nx1 cn2 nx2` (Chain) of `menu_chainMenuScene`; the page's LED block is treated like PERF while open |
| MENU_MIDI_PAGE | sub-page 2 `scd qcd qac` (cue `^`), sub-page 3 `cmp cam ctm csc` (cue `+`, `MENU_GLOBAL_SCENE_SUBPAGE`, always last); `pcr` is on sub-page 1 |
| SEQ_PAGE sub-page 0 | `len scl shf run` / `ch nte lop rmp` (`ch` lists off/glb/1..16 and `nte` off/on/notes in category Track, both read from the viewed Scene; `rmp` = Automtn/Ramp, S081 P2, label `asm` before the S083 follow-up) |

The encoder crosses sub-pages through the static cross-sub-page step in
`menu_moveToMenuItem()` (PERFORMANCE_PAGE, RECORDING_PAGE, MENU_MIDI_PAGE;
non-cyclic), so a sub-page with fewer than eight cells is never a dead end.
The MODE MENU re-press cycle (`menu_switchSubPage()`) advances from a global
sub-page without a second half to the next populated one.

### LEDs

| Row | Rule |
|---|---|
| SEQ (Scenes) | `menu_refreshPerfSceneLeds()`: steady = present with steps; blink (standard rate) = active Scene while stopped or while the chain editor is open, viewed Scene (follow off, differs), chain-viewed Scene; running active Scene = beat pulse; a running blink is never restarted (`led_isBlinkLed()`) |
| SEQ (queued Scene) | double-rate flash on `LED_LAYER_FAST`, owned by `menu_syncQueuedSceneLed()` (every tick: PERF mode and `seq_queuedSceneTarget() < 16`) |
| SELECT | dark unless the looper is held/engaged, then the division (`menu_syncPerfSelectLed()` ← `buttonHandler_loopSelectLed()`) |
| BAR | overlay toggle state; both off while the looper runs except the held modifier; restored on loop release |
| VOICE | mutes (`buttonHandler_showMuteLEDs()`); a queued mute reset clears the mirror (`buttonHandler_clearAllMutes()`) |
| SHIFT / REC | lit while SHIFT+REC is open; SHIFT lit while the chain editor is open. S081: REC on in LATCH, blinks once on overdub entry and exit, flashes while SHIFT+REC is open and armed; SHIFT and CLEAR lit while live erase is on |
| BAR (outside PERF) | S081 R19: the overlay toggle LEDs are shown only in PERF; leaving PERF restores the target mode's BAR LEDs and entering PERF restores the overlay state (`bh_overlayActive`) |

### Scene change execution (foreground)

`menu_serviceDeferredSceneChange()` runs on every `buttonHandler_tick()`:
when the fire flag is up it reads target / queue / chain, clears the
pending state, then: queued → `menu_applyQueuedSceneAction(target)` (qac
flags from `menu_queueActionFlags()`: mute → `seq_setMute(7, 0)` +
`buttonHandler_clearAllMutes()`; morph → `preset_morphScene(target, 0)`;
reload → deferred) and `menu_perfSceneChangeExecute(target, qac reset bit)`;
chain fire to the playing Scene → `seq_chainRepeatSelf(pcr)`; otherwise
`menu_perfSceneChangeExecute(target, pcr)`. It always ends with
`menu_syncQueuedSceneLed()`. `menu_perfSceneChangeExecute()` selects the
Scene (SceneData, BankData edit mask, shown pattern, `seq_selectActivePattern()`),
calls `seq_cancelPendingDirectChange()` (a queued change survives), runs
`seq_resetToPatternStart()` when `restart`, then `preset_startDrumsetApply()`,
the Scene LED refresh and a repaint. Direct presses go through
`menu_perfModeSceneButtonPressed()` (instant or stopped → execute with pcr,
else `seq_requestSceneChange()`).

### Settings and storage owned by this surface

- settings.cfg Globals: `perf_btn_lt`, `perf_note_lt`, `perf_btn_rt`,
  `perf_note_rt`, `scd`, `qcd`, `qac` (and the pre-existing `bar_reset_mode`
  = pcr); `FILESYSTEM_SPEC.md`.
- Scene: `chain_condition[2]`, `chain_next[2]` (sceneset lines, AutoSave
  cells 72..75); `BANK_PRESET_ARCHITECTURE.md`, `AUTOSAVE.md`.
- Pattern: `track_loop_exclude` (PAT4 track byte 4), `track_auto_smooth`
  (`asm`, PAT4 track byte 5, S081); `PATTERN_DYNAMIC_STACK.md` §1/§7.
- settings.cfg: `quantisation` may be 5 (`trk`, S081).
- Runtime only: overlay state, looper state, queue/chain pending state, live
  record mode and `rtk`.

### Adding a PERF gesture or setting (how-to)

- Route the edge in `processPress()`/`processRelease()` before generic
  routing; keep hold state in event order (not `btn_held[]`, which the scan
  updates ahead of queued edges); make every press/release pair symmetric
  and reconcile it in the overflow path and in `handleModeButtons()`.
- Any timing state shared with TIM3 must be published under PRIMASK with a
  single-byte gate written last.
- New enumerated menu cells use `DTYPE_0B127` + `menu_getS080StaticName()` /
  `menu_clampCellValue()` cases (no free DTYPE ids). Mirrors appended after
  the Globals must be no-ops in `menu_parseGlobalParam()` and commit in
  `menu_cellCommitValue()`.
- SEQ-row indications must go through `menu_refreshPerfSceneLeds()` or a
  dedicated LED layer; never start/stop blinks from a per-beat path.

## Core/Bank/BankData

Affiliate modules: filesystem, Preset, SceneData, PatternData, Menu, AutoSave.

Purpose: owns the 16-Scene resident Bank workspace (present/edit masks, active
Scene, restore slot, resident-Bank flag) and, since Session 058, the
session-scoped card-verified clean-Scene authority used to skip Bank Save
children that are provably unchanged on the current mounted card.

| API | Use | Usual callers / clients |
| --- | --- | --- |
| `bank_init()` | Zero present/edit masks (per-Scene voice-edit masks default to `(1u << i)` self-only since Session 071), active Scene, resident-Bank flag, and all Session 058 clean authority. | boot |
| `bank_setScenePresentMask()` / `bank_scenePresentMask()` / `bank_scenePresent()` | Present-mask get/set; the setter reports whether it changed (Session 052). | filesystem Bank Load/Save, AutoSave, Preset |
| `bank_hasResidentBank()` / `bank_setHasResidentBank()` | Resident-Bank presence flag; the empty-Scene/Bank overwrite guard (Session 057) depends on it. | filesystem |
| `bank_clearSdCleanAuthority()` | Drop every card-clean bit and the identified Bank slot (`BANK_SD_CLEAN_SLOT_NONE = 0xffff`). Called at cold boot and every fresh mount/remount. | filesystem `initAfterCardReady` |
| `bank_invalidateSdCleanScene(scene_index)` | Clear one resident Scene's card-clean bit and set its mutation-during-save bit. Called by every retained-data mutation funnel (Scene/Kit scalar setters, Instrument endpoint store, KitMrp/InstrumentMrp/whole-Instrument commit, Pattern mutators). Equal-value no-ops never reach it. | SceneData, presetManager, PatternData |
| `bank_publishSdCleanAuthority(slot, proven_mask)` | Establish/merge clean authority after a completed Bank Load/Save. Same-slot ORs into existing bits; a different slot replaces the old authority. Autosave recovery never calls it. | presetManager `on_bank_load_complete`; filesystem Save completion |
| `bank_sdCleanMask()` / `bank_sdCleanSlot()` / `bank_sdCleanSlotIsValid()` | Read the clean mask/slot/validity. | filesystem Bank Save skip |
| `bank_setSceneMaskVoiceEditForScene(scene, mask)` / `bank_sceneMaskVoiceEditForScene(scene)` | Per-Scene indexed voice-edit mask setter/getter for boot restore (Autosave) and bankset load/save (Session 071). | filesystem, Autosave |
| `bank_toggleSceneMaskVoiceEdit(scene)` | Toggle one Scene in the active mask. Menu applies the layout gate before turning a bit on; the active bit cannot be removed. | Menu VOICE-held SEQ |
| `bank_revalidateVoiceEditMasks()` | Drop mask members whose Effect/Instrument layout no longer matches their owner; changed entries mark AutoSave (S072 Step 10). | Menu load-completion funnels, `main.c` boot, `effects_changeType()` |
| `bank_sceneFanoutMask(scene)` / `bank_exchangeVoiceEditMask(src, dst)` / `bank_resetVoiceEditMaskToSelf(scene)` | Copy/clear fan-out set (the Scene's own entry, **available** and layout-matching members; available = present outside a load, S082 P3), the `copy scene` / `copy scene settings` mask exchange, and the `clear scene` / `clear scene settings` reset. (S075) | CopyClear |
| `bank_editTargetMask()` / `bank_sceneFanoutMask32(scene)` | S082: 32-bit edit/fan-out targets. While the active Scene is Scene 17 the target is `1ul << 16` alone (17's edit mask is itself only); otherwise the active entry (`bank_editTargetMask()` masks out unavailable Scenes during a load, S082 P3) or `bank_sceneFanoutMask()`. Loops run over `SCENE_SLOT_TOTAL` (17). | Menu commit loops, EffectsManager, copy/clear |
| `bank_mergeVoiceEditEntry(entry, keep, n)` / `bank_mergeVoiceEditMasksAfterLoad(src, seen, loaded, emptied)` / `bank_stripFailedFromVoiceEditMasks(failed)` | S082 mutual exclusion after a partial Bank operation: entry = `(entry & keep) | self`; after a load, loaded Scenes take their source entry ∩ loaded, emptied Scenes become self, kept Scenes drop every replaced Scene. A progressive load merges before the first child and later only strips failed children (each failed child's entry becomes self). | filesystem Bank Load/Save |
| `bank_revalidateVoiceEditMasksIn(scope)` | S082 P3: the layout revalidation restricted to owners and members inside `scope`; `bank_revalidateVoiceEditMasks()` is the 0xFFFF wrapper. | `bank_noteLoadChildDone()` |
| `bank_beginLoadAvailability(act|empty)` / `bank_noteLoadChildDone(scene)` / `bank_endLoadAvailability()` / `bank_loadUnavailableMask()` / `bank_sceneAvailableMask()` / `bank_sceneAvailable(scene)` | S082 P3 load availability. The 2 B `bank_load_unavailable_mask` marks Scenes a running progressive Bank load has not finished (pending, in progress, emptied, absent). `noteLoadChildDone()` sets the child's present bit at once, clears its unavailable bit and revalidates layouts among available Scenes. **Available = present & ~unavailable**; selection, fan-out, copy/clear and LEDs use it, persistence (AutoSave, Bank Save, the end-of-load commit) uses presence. Zero outside a load, so behaviour is identical then. | filesystem Bank Load, Preset completion, Menu, ButtonHandler, copy/clear, EffectsManager |
| `bank_resetSdSaveMutationWindow()` / `bank_resetSdSaveMutationScene()` / `bank_sdSaveMutatedMask()` | Own the operation-scoped mutation window across a Bank Save so a child edited while its write was in flight stays non-clean. | filesystem Bank Save |

The clean authority is **never serialized** (not in settings, HCNAMES, Bank, or
AutoSave): it survives only for one boot+mount and is rebuilt by successful
Bank Load/Save completion paths. See `058_SESSION_HANDOFF_LOG.md` §5.

## Core/Bank/Scene/SceneData

Affiliate modules: Autosave, BankData, PatternData, InstrumentManager,
EffectsManager, and Preset.

Purpose: owns retained Scene settings and the Scene-owned Effect record. The
`scene_t.effect` record is never written directly by callers: SceneData is the
sole mutation boundary and performs the retained write, typed AutoSave dirty
mark, and card-clean invalidation in that order.

The Effect record's meaning is supplied by the EffectsManager registry; its
ordered AutoSave projection is defined by `Autosave.h`, not by the C-struct
layout.

| API / data | Use | Usual callers / clients |
|---|---|---|
| `scene_effectConst(scene)` | Read-only access to a retained Effect record. | AutoSave, EffectsManager |
| `scene_effectRecordDefaults(record)` | Build the `off` Effect record and common defaults without dirty marking. | Scene initialization, `.fx` parser, AutoSave reader |
| `scene_commitEffectRecord(scene, record)` | Replace a complete record, normalize sequence fields, and mark all live Effect cells. | Effect loader/editor transaction |
| `scene_effectRecordForWholeCommit(scene)` / `scene_finishEffectWholeCommit(scene)` | Open the resident record for a validated in-place whole-record transaction, then normalize, mark all Effect cells including the live type token, and invalidate the Scene's card-clean authority. | EffectsManager type change |
| `scene_setEffectNormalParameter()` / `scene_setEffectMorphParameter()` | Store one 0..255 parameter cell and mark its ordered live cell. | EffectsManager edit API (fan-out workers) |
| `scene_setEffectSeqRunMode()` / `scene_setEffectSeqLength()` / `scene_setEffectSeqStepScale()` | Store normalized sequencer settings and mark the corresponding live cell. | EffectsManager edit API |
| `scene_setEffectSeqLaneValue()` / `scene_setEffectSeqLaneLocked()` | Store one sequencer lane value or lock bit and mark only its live cell. | EffectsManager lock editor |
| `scene_setEffectMorphAmount()` / `scene_getEffectMorphAmount()` | Retained Scene parameter 40, separate from the Effect record. | EffectsManager, `preset_morphScene()` (PERF `mrp`), AutoSave |
| `scene_editLayoutMatches(a, b)` | Same Effect type and six Instrument types; read-only. | Menu mask gate, BankData re-validation |
| `scene_busCompDefaults(settings)` / `scene_busCompClamp(field, value)` / `scene_setBusCompSetting(scene, field, value)` / `scene_getBusCompSetting(scene, field)` | S074 master bus compressor bytes `bus_comp[4]` (MODE 0..2, AMOUNT 0..127, TIME 0..127, SIDECHAIN 0..6; defaults off/0/0/off since S075 F2). The setter clamps, stores change-aware and marks AutoSave Scene parameter 41 + field. | Preset (menu commit, fan-out), `.scg` parser, AutoSave reader, BusCompressor (reads per block) |
| `scene_setVoiceFaderSetting()` / `scene_getVoiceFaderSetting()` | Per-voice fader mode 0..`SCENE_FADER_SETTING_MAX` (3 = `xfd` since S074; equal to `MIXER_FADER_XFD`, asserted in `mixer.c`). | Preset, `.scg` parser, AutoSave, mixer |
| `scene_settingsDefaults(out)` / `scene_commitSettings(scene, src)` / `scene_commitKit(scene, kit)` / `scene_resetKitToDefaults(scene)` | Whole-settings defaults and field-by-field commit through the change-aware setters; whole-Kit commit and fresh-Kit reset with the Kit AutoSave marker and card-clean invalidation. No runtime apply. (S075) | CopyClear |
| `scene_setVoiceAudioOut()` / `scene_getVoiceAudioOut()` | Per-voice route 0..5 (0 St1, 1 St2, 2 L1, 3 R1, 4 L2, 5 R2). The default for every slot, and the fallback for an invalid byte, is St1 (route 0) since S075 F2 (`scene_defaultVoiceAudioOut()`). | Preset, `.scg` parser, AutoSave |
| `scene_setTrackMorphLength/Scale/Shuffle(scene, track, value)` / `scene_getTrackMorphLength/Scale/Shuffle(scene, track)` | S078: Morph endpoints of the Pattern's per-track length (1..128), scale CC (0..127) and shuffle (0..127). Setters clamp and go through `scene_storeParameterByte()` (AutoSave Scene cells 51..71). The Normal values stay in the Pattern region. | Menu STEP SHIFT Morph view (`menu_cellCommitValue()`), copy/clear morph copy/reset, AutoSave reader, `presetMorph_getTrackEffective*()` |
| `scene_setVoiceFxSendAmount()` / `scene_getVoiceFxSendAmount()` / `scene_setVoiceFxSendMorph()` / `scene_getVoiceFxSendMorph()` | Normal and Morph endpoints of the per-voice FX send (0..127; AutoSave Scene cells 14..19 and 45..50). Change-aware; no runtime push: the mixer interpolates every block through `preset_getEffectiveFxSendAmount()`. (Morph endpoint: S075 F2) | Preset, `.scg` parser/writer, AutoSave, CopyClear |

| `scene_backgroundExists()` / `scene_backgroundHomeIndex()` / `scene_playableIndexValid(scene)` / `scene_backgroundBegin(home)` / `scene_backgroundRelease()` | S082 Scene 17: one extra `scene_t` (`scene_background`, 1,656 B) addressed as `SCENE_BACKGROUND_INDEX` (16) by `scene_get()`, `scene_getConst()` and `scene_selectActive()` while it exists. `scene_indexValid()` stays 0..15 (storage, AutoSave, HCNAMES, masks never see 17). `scene_backgroundHomeIndex()` = the active Scene when < 16, else the Scene that was active when 17 was built (every load targets it). Release clears the exists flag; 17 can never be selected again. | Preset build, Sequencer switch/release, Menu, filesystem |
| `scene_resetResidentToEmpty(scene)` | S082 shared empty-Scene primitive: settings, Effect and Kit defaults (the Pattern region is reset separately under the Pattern service). | filesystem `filesystem_bankEmptyScenes()`, boot reader |
| `scene_setChainCondition(scene, slot, value)` / `scene_getChainCondition(scene, slot)` / `scene_setChainNext(scene, slot, value)` / `scene_getChainNext(scene, slot)` | S080 Scene chain pairs (slot 0..1): condition 0..78 (`SCENE_CHAIN_CONDITION_MAX`), next 0..43 (`SCENE_CHAIN_NEXT_MAX`, 43 = `stp`). Setters clamp and mark AutoSave Scene cells 72..75; getters are ISR-safe single-byte reads. Copied/cleared by `scene_commitSettings()`; never fanned out through the VOICE edit mask. | chain editor (Menu), TIM3 chain engine, `.scg` parser, AutoSave |

The 420-byte record and the 16-record SRAM1 allocation (`scenes`, 26,496 B:
1,656 B per Scene since S080, +4 B chain settings; S078 added +21 B track
Morph endpoints) are firmware-lifetime SceneData storage. EffectsManager owns 84 B of resolution state and 184 B of
overlay/LFO state in SRAM1, plus the 76-byte DTCM type runtime; the mixer owns
the FX bus.

## Core/Bank/Scene/Pattern/PatternData

### Session 067 current contract

PatternData owns a three-part dynamic storage system per resident Scene:
a 1,792-byte static address array (896 × uint16_t), an 8,192-byte event
pool (PAT_STACK_SIZE=256 × 32-byte chunks), and a 512-byte free-tracking
bitmap plus 23 parameter bytes. These live in the permanent `pat_regions` symbol (168,304 bytes
total across 16 Scenes). Each address entry encodes trigger (bit 15),
has-specials (bit 14), and a 14-bit pool byte offset (sentinel 0x3FFF).
Pool blocks carry a 2-byte header, a 1-byte special-flags byte, and value
bytes for note, velocity, and probability in ascending bit order.

`pat_readStepSpecials()` is the unified read path for Sequencer playback
and step-edit menu display; it returns resolved defaults for absent
specials. `pat_setStepNote/Volume/Probability` perform real pool
read-modify-write through `pat_writeSpecials` (returns `uint8_t`
success/failure since Session 067), which handles reallocation, free, and
graceful degradation on alloc failure. Session 067 publication ordering fix:
all address-to-pool transactions use write-new/swap/free-old with PRIMASK
trigger-bit re-read; in-place same-size rewrite removed entirely.
`pat_eraseStep` and `pat_clearTrack` detach addresses before freeing pool
blocks. Copy operations remain deliberate no-ops pending pool block
duplication design. Since Session 067, all pool-mutating calls from callers
outside PatternData route through the Pattern Stack Service (`patSvc_*`).

Current Scene/Bank/root Pattern interchange is exact binary PAT4; legacy text
is import-only and no retained `PatternSet`/discard instance remains. `scene_t`
does not contain Pattern data. Affiliates are SceneData, Sequencer,
UI/LED/copy-clear, Euklid/SOM, and filesystem; none may add a parallel
Pattern owner. See `STORAGE_SRAM_MANIFEST.md` for linked sizes and
`PATTERN_DYNAMIC_STACK.md` for the complete specification.

Affiliate modules: Menu, buttonHandler, ledHandler, CopyClear, filesystem,
Sequencer, EuklidGenerator, Preset indirectly through Sequencer
automation.

Purpose: owns pattern, track, step, length/rotation, pattern settings, and step
automation storage. Provides edit APIs and menu-refresh helpers.

| API | Use | Usual callers / clients |
|---|---|---|
| `pat_init(void)` | Initialize all PatternData storage. | `seq_init()` |
| `pat_trackValid(track)` / `pat_patternValid(pattern)` / `pat_stepValid(step)` | Bounds checks before direct access. | PatternData internals, ledHandler, filesystem, buttonHandler |
| `pat_stepPtr(pattern, track, step)` | Bounded pointer to `Step`; supports `PATTERNDATA_STAGING_PATTERN`. | filesystem, PatternData owner-level code |
| `pat_mainStepsPtr(pattern, track)` | Bounded pointer to main-step mask. | filesystem, PatternData, Euklid |
| `pat_patternSettingPtr(pattern)` | Bounded pointer to per-pattern settings. | filesystem, Sequencer pattern-change logic |
| `pat_lengthRotatePtr(pattern, track)` | Bounded pointer to the per-track settings record (`LengthRotate`: length, rotation, scale, MIDI channel/note, shuffle). | filesystem, Euklid, PatternData |
| `pat_isStepActive(track, step, pattern)` | Read sub-step active bit. | ledHandler, Sequencer playback wrappers |
| `pat_isMainStepActive(track, mainStep, pattern)` | Read main-step active bit. | ledHandler, Sequencer playback wrappers, buttonHandler |
| `pat_sceneHasActiveSteps(scene)` | Scan complete Scene PatternData for any active Step; hides the bridge storage shape from Load-menu LEDs. | Menu Load Scene LED helper |
| `pat_readStep(pattern, track, step, out)` | Copy one `Step` snapshot for playback-side inspection. | Sequencer automation parsing and MIDI echo velocity |
| `pat_getStepProbability(pattern, track, step)` | Read one step probability with safe invalid default. | Sequencer playback |
| `pat_getStepNote(pattern, track, step)` | Read one step note with `PAT_DEFAULT_NOTE` fallback. | Sequencer playback, rolls, MIDI note echo |
| `pat_getStepVolume(pattern, track, step)` | Read lower 7-bit velocity. | Sequencer playback and MIDI note echo |
| `pat_setMainStep(pattern, track, mainStep, onOff)` | Set one main-step bit. | Sequencer recording/erase, PatternData operations |
| `pat_setMainStepsRaw(pattern, track, bits)` | Replace whole 16-bit main-step mask. | EuklidGenerator, file load paths |
| `pat_toggleStep(track, step, pattern)` | Toggle one sub-step active bit. | buttonHandler shift-select |
| `pat_toggleMainStep(track, mainStep, pattern)` | Toggle one main-step bit. | buttonHandler step edit |
| `pat_setStepNote(pattern, track, step, note)` | Mutate selected step note and menu mirror. | `menu_parseGlobalParam(PAR_STEP_NOTE)` |
| `pat_setStepVolume(pattern, track, step, volume)` | Mutate selected step velocity without clearing active bit. | `menu_parseGlobalParam(PAR_STEP_VOLUME)` |
| `pat_setStepProbability(pattern, track, step, prob)` | Mutate selected step probability. | `menu_parseGlobalParam(PAR_STEP_PROB)` |
| `pat_readStepAutomations(scene, track, step, out[], maxEntries)` | Read decoded automation entries for one step; returns count. | Sequencer playback, step-edit menu display |
| `pat_writeStepAutomation(scene, track, step, target9, value7)` | Add or update one automation entry; uniqueness enforced, returns 1 on success, 0 on pool exhaustion or 63-entry ceiling. | Step-edit menu add/update, future VOICE overlay |
| `pat_removeStepAutomation(scene, track, step, target9)` | Remove one automation entry by 9-bit target; returns 1 if found. | Step-edit menu delete |
| `pat_removeTrackAutomationByTarget(scene, track, target9)` | Remove all entries with a given target from all 128 steps of a track; returns count removed. | Step-edit menu clear-by-target |
| `pat_stepAutomationCount(scene, track, step)` | Count automation entries without reading them. | Step-edit menu page count, sequencer |
| `pat_setPatternChangeBar(pattern, value)` | Set pattern change-bar rule. | `menu_parseGlobalParam(PAR_PATTERN_BEAT)` |
| `pat_setPatternNext(pattern, value)` | Set pattern next-pattern rule. | `menu_parseGlobalParam(PAR_PATTERN_NEXT)` |
| `pat_getPatternChangeBar(pattern)` / `pat_getPatternNext(pattern)` | Read pattern settings. | Future callers; currently mostly direct pointer reads |
| `pat_setTrackLength(pattern, track, length)` | Set track length with 0==16 storage encoding. | `menu_parseGlobalParam(PAR_TRACK_LENGTH)` |
| `pat_getTrackLength(pattern, track)` | Read length in UI form. | `pat_applyTrackSettingsToMenu()` |
| `pat_getEffectiveTrackLength(pattern, track)` | Read nonzero playback length, converting stored 0 to 16. | Sequencer step walk, external clock, start-position reset |
| `pat_setTrackRotation(pattern, track, rotation)` | Set track rotation and invoke Sequencer runtime compensation if needed. | buttonHandler perf rotation, Sequencer stop reset |
| `pat_getTrackRotation(pattern, track)` | Read track rotation. | `pat_applyTrackSettingsToMenu()` |
| `pat_setTrackScale(pattern, track, scale)` / `pat_getTrackScale(pattern, track)` / `pat_getTrackScaleRatio(pattern, track)` | Store/read the per-track Normal step-scale CC (0..127 on the S078 curve; 76 = 1/16). Playback reads the effective cache, not this directly. | `menu_parseGlobalParam(PAR_TRACK_SCALE)`, copy/clear, `presetMorph_getTrackEffectiveScale()` |
| `pat_setTrackPlayMode(scene, track, mode)` | S078: store the per-track run mode 0..5 (fwd/rev/pip/rnd/onc/1fr) and mark the Scene's Pattern dirty. Read by the sequencer per step boundary (`region->track_play_mode`); PAT4 track header byte 3. | `menu_parseGlobalParam(PAR_TRACK_PLAY_MODE)`, copy/clear track copy/clear |
| `pat_setTrackLoopable(scene, track, loopable)` / `pat_trackLoopable(scene, track)` | S080: per-track looper opt-out stored inverted in `track_loop_exclude` (bit set = not loopable; default all loopable); setter marks the Pattern dirty only on change; getter returns 1 for invalid coordinates. PAT4 track header byte 4. The sequencer reads the region directly in `seq_loopableTrackMask()`. | `menu_cellCommitValue()` (`lop` cell), copy/clear track copy (carries) / clear (loopable), `pat_applyTrackSettingsToMenu()` mirror |
| `pat_setTrackAutoSmooth(scene, track, on)` / `pat_trackAutoSmooth(scene, track)` | S081 P2: per-track automation-ramp flag `asm` in `track_auto_smooth` (bit set = `on`; default `off`); setter marks the Pattern dirty only on change; PAT4 track header byte 5. The Sequencer reads the region byte directly from TIM3 (`PATTERN_DYNAMIC_STACK.md` §6.2b). | `menu_cellCommitValue(PAR_TRACK_AUTO_SMOOTH)`, copy/clear track copy/clear |
| `pat_findStepAutomation(scene, track, step, target9, *value)` | S081 P2: single-target lookup in one step's automation tail; returns 1 and the 7-bit value when present. Copies nothing. | Sequencer ramp pairing (`seq_rampStartIfPaired()`), LiveRecord first-change write (`liveRec_writeCurrentValue()`) |
| `pat_recordStep(scene, track, step, const pat_record_spec_t *spec)` | S081: one live-recorded trigger (note or default note, velocity, timing, roll, fill, `fil`); trigger ON in the same PRIMASK publish; rhythm cleared; condition kept unless `setFil`; automation kept; velocity/fill 100 stored as absent; bit-15-only cheap path. Returns success. | PatternStackService `RECORD_STEP` only |
| `pat_eraseSpecials(scene, track, step)` | S081: live SEQ-hold erase of one step: with automation, keeps condition + automation and drops the other specials; otherwise releases the block (trigger KEEP; TIM3 cleared bit 15). | PatternStackService `ERASE_SPECIALS` only |
| `pat_writeDynamic(…, trigger_policy)` (internal) | S081: the block publish takes KEEP/ON/OFF (`PAT_RAW_TRIGGER_*`) applied inside both PRIMASK publishes; every pre-S081 caller passes KEEP. | PatternData internals |
| `pat_applyTrackSettingsToMenu(scene, track)` | Mirror length/scale/shuffle/run mode, Loopable and (S081) `asm` into `parameter_values[]`. | Menu track/page changes, buttonHandler, STEP overlay entry |
| `pat_setTrackMidiChannel(pattern, track, channel)` / `pat_getTrackMidiChannel(pattern, track)` | Store/read per-track MIDI output channel in menu form (`1..16`). | `menu_parseGlobalParam(PAR_TRACK_MIDI_CHAN)`, Sequencer MIDI output/input matching |
| `pat_setTrackMidiNote(pattern, track, note)` / `pat_getTrackMidiNote(pattern, track)` | Store/read per-track MIDI note override (`0` means fallback/default). | `menu_parseGlobalParam(PAR_TRACK_MIDI_NOTE)`, Sequencer trigger/preview/MIDI output |
| `pat_setTrackShuffle(pattern, track, shuffle)` / `pat_getTrackShuffle(pattern, track)` | Store/read per-track Normal shuffle amount (`0..127`); played since S078 through the effective cache (odd steps deferred `(shuffle × 24) / 256` ticks). No legacy all-track shuffle import/export remains. | `menu_parseGlobalParam(PAR_SHUFFLE)`, filesystem, `presetMorph_getTrackEffectiveShuffle()` |
| `pat_clearTrack(scene, track)` | Free dynamic blocks and reset one track. | PatternStackService barrier, `pat_clearPattern()` |
| `pat_clearPattern(scene)` | Free dynamic blocks and reset all tracks in one Scene Pattern. | PatternStackService barrier, initialization |
| Raw block API: `pat_rawReadBlock`, `pat_rawBlockBytes`, `pat_rawDecode`, `pat_rawEncode`, `pat_rawPlace`, `pat_rawPlaceViaSwap`, `pat_rawSwapReturn`, `pat_rawPublishEmpty`, `pat_rawFreeChunks`, `pat_rawSwapFree`, `pat_rawRegionSilence/CopyBody/PublishSteps/CopiedBlock/PublishRewritten/Reset` | Whole-step and whole-region reads, placements and publications in publication order, including the permanent swap block (S075; `PATTERN_DYNAMIC_STACK.md` §12.17). Legal only under `patSvc_beginExclusive()`. The S062 no-op `pat_copy*` surface is removed. | `copyClearService.c` only |
| `pat_setSelectedStep(step)` | Keep active step mirror in `PAR_ACTIVE_STEP`; `seq_selectedStep` no longer exists. | PatternData menu-apply and automation destination edit |
| `pat_setActiveAutomationTrack(track)` / `pat_getActiveAutomationTrack(void)` | Select automation lane. | Menu; future UI clients |
| `pat_armAutomationStep(step, track, armed)` | Arm/disarm held-step automation recording target. | buttonHandler long-press path |
| `pat_readStepSpecials(scene, track, step)` | Resolve stored note/velocity/probability values and flags. | Sequencer, step menu |
| `pat_setStepNote/Volume/Probability(scene, track, step, value)` | Tracked dynamic-block read/modify/write. | Menu edit path |
| `pat_applyStepToMenu(scene, track, step)` | Copy step editable fields into `parameter_values`. | buttonHandler, Menu active-step edits |
| `pat_applyPatternSettingsToMenu(scene)` | Copy Pattern settings into menu params. | Menu load/apply paths, buttonHandler Pattern view |
| `pat_applyTrackSettingsToMenu(scene, track)` | Copy track settings into menu params. | buttonHandler, LED follow paths, Menu voice page |
| `pat_releaseStepDynamic(scene, track, step)` | Trigger-preserving detach+free: clears bit 14 and sets sentinel offset while preserving the trigger bit, then frees pool block. (Session 067) | PatternStackService |
| `pat_markPoolMutationDirty(scene)` | Narrow dirty-marking boundary for service operations that bypass normal `pat_write*` paths. (Session 067) | PatternStackService |
| `pat_tryAppendAutomation(scene, track, step, target9, value7)` | In-place growth for adding automation entries when adjacent free space is available. Returns 1 on success. (Session 067) | PatternStackService |
| `pat_poolUsagePercent(void)` | Return pool occupancy percentage 0..99 via memcpy+popcount of active Scene bitmap. (Session 067) | Menu settings widget |
| `pat_snapshotScene(scene)` / `pat_autosaveSnapshot()` | Transfer one live Scene into the immutable Pattern AutoSave snapshot/read it during the drain. Resident Scenes only; never while Scene 17 exists (it owns the region). | filesystem Pattern scheduler/writer |
| Scene 17 mapping (S082, internal `pat_regionFor()`) | Every Scene-indexed `pat_*` call resolves 0..15 to `pat_regions[]` and 16 to `pat_background_region` while Scene 17 exists (NULL otherwise); `pat_patternValid(16)` is true only then. `pat_initScene()` stays resident-only; Scene 17 resets through `pat_rawRegionReset(16)`. PatternStackService uses `pat_patternValid()` so 17 is a playable service target; `patSvc_prepareSceneReplace()` stays resident-only (no load writes 17). | PatternStackService, Sequencer, LiveRecord, Preset build, copy/clear |

## Core/Bank/Scene/Pattern/PatternStackService

Affiliate modules: PatternData, Menu, Sequencer, CopyClear,
EuklidGenerator, filesystem, timebase.

Purpose: unified pool mutation dispatcher (Session 067, bounded CPU
convergence Session 069). Serializes all pool-mutating operations through a
single service tick, guaranteeing exactly one mutation target at a time.
Provides owned trailing-slack reservation (512-byte bit-packed reservation
image), finite bounded repair epoch (cursor sleeps at `PATSVC_ADDRESS_COUNT`;
wakes only on mutation, reservation consumed, density restore, handover, or
filesystem replacement), reactive-only compaction (triggered only by blocked
allocation; periodic sweep deleted), bounded bulk barriers for track/pattern
clear, filesystem replacement handover, and an elastic gap policy. Direct-path
parity fix classifies fragmentation failures in `patSvc_submit()` and retains
events for reactive recovery (`D` trace stage). Load/Save repair gate
suppresses repair when `menu_activePage == LOAD_PAGE || SAVE_PAGE`. Budget
integration: repair section queries `filesystem_backgroundBudgetAvailable()`
and charges elapsed time. See `PATTERN_DYNAMIC_STACK.md` §12 for the complete
specification.

| API | Use | Usual callers / clients |
|---|---|---|
| `patSvc_init(void)` | Initialize service state, queue, cursors. | `main.c` after boot filesystem ladder |
| `patSvc_tick(void)` | Service tick: handover → queue drain → bulk barrier → Tier 1 gap → Tier 2 compaction. | `timebase.c` after `endlessPots_tick()` |
| `patSvc_idle(void)` | Filesystem replacement handover: complete/abandon bulk barrier, drain queue, switch Scene, clear cursors. | `filesystem.c` at 5 replacement boundary points |
| `patSvc_writeStepAutomation(scene, track, step, target9, value7)` | Add/update one automation entry via service. Returns 1 on success. | Menu VOICE overlay, step-edit menu |
| `patSvc_removeStepAutomation(scene, track, step, target9)` | Remove one automation entry via service. Returns 1 if found. | Menu VOICE overlay, step-edit menu |
| `patSvc_setStepNote(scene, track, step, note)` | Set step note via service. | Menu step edit |
| `patSvc_setStepVolume(scene, track, step, volume)` | Set step velocity via service. | Menu step edit |
| `patSvc_setStepProbability(scene, track, step, prob)` | Set step probability via service. | Menu step edit |
| `patSvc_eraseStep(scene, track, step)` | Erase step via service. | Sequencer erase mode |
| `patSvc_clearTrack(scene, track)` | Clear track via bounded bulk barrier. | (no current caller; copy/clear uses its own engine) |
| `patSvc_clearPattern(scene)` | Clear pattern via bounded bulk barrier. | (no current caller; copy/clear uses its own engine) |
| `patSvc_beginExclusive(scene)` / `patSvc_endExclusive(scene)` | Exclusive write access to one Scene's Pattern region (any Scene); begin returns 1 once queued work drained. (S075) | `copyClearService.c` |
| `patSvc_exclusiveCompactStep(scene)` / `patSvc_exclusiveEvacuateSwapStep(scene)` | One sliding-compaction move / move a pre-S075 block out of the swap block, for the exclusive holder. (S075) | `copyClearService.c` |
| `patSvc_removeTrackAutomationByTarget(scene, track, target9)` | Remove all entries with a given target from all 128 steps. | Menu VOICE overlay |
| `patSvc_enqueueErase(scene, track, step)` | Queue erase from TIM3 ISR context (PRIMASK-protected enqueue). | Sequencer live erase (TRACK hold, S081) |
| `patSvc_enqueueEraseSpecials(scene, track, step)` | S081: TIM3-safe `ERASE_SPECIALS` enqueue (live SEQ-hold erase). | Sequencer live erase |
| `patSvc_recordStep(scene, track, step, const pat_record_spec_t *spec)` | S081: copy the spec into a free slot of the 4-slot staging table and submit `RECORD_STEP` (direct when idle); a full table rejects. Returns 1 when accepted. | LiveRecord (`liveRec_writeStep()`) |
| `patSvc_requestTarget(scene)` / `patSvc_releaseTarget()` / `patSvc_targetOpen(scene)` | S081 recording-target override: the service mutates `scene` (a track's per-track Scene) instead of the active Scene. Request returns 0 when refused (exclusive claim, filesystem replacement, non-resident Scene, busy queue); a target change drains the old work first; `patSvc_idle()` is false while held. `patSvc_desiredScene()` (internal) = request, else `seq_activePattern`. | LiveRecord service |
| `patSvc_replacePending()` | S081: true while a filesystem Pattern replacement is prepared or applied. | LiveRecord (Load suspend without depending on Menu) |
| `patSvc_tryAppendAutomation(scene, track, step, target9, value7)` | Gate-6 growth: try append with reservation consumption. Returns 1 on success, 0 on blocked allocation (retained for reactive recovery). | Menu VOICE overlay, step-edit menu |
| `patSvc_countUsed(void)` | Popcount of occupancy bitmap (64-word `__builtin_popcount`). | filesystem.c idle tick-tail (removed from tick-tail in S069) |

Caller migration (Session 067): 15+ call sites across `menu.c`,
`copyClearTools.c` (removed in S075), `EuklidGenerator.c`, and `sequencer.c` changed from
direct `pat_*` mutation calls to `patSvc_*` routing. TIM3's automation read
path is not routed through the service — it reads address entries directly,
which are always consistent due to the publication ordering fix.

## Core/Bank/Scene/Pattern/EuklidGenerator

Affiliate modules: Menu, buttonHandler, ledHandler, PatternData.

Purpose: owns Euclidean generator state and writes generated main-step masks to
PatternData.

| API | Use | Usual callers / clients |
|---|---|---|
| `euklid_init()` | Initialize generator defaults. | `main.c` boot |
| `euklid_getLength(trackNr)` / `euklid_getSteps(trackNr)` / `euklid_getRotation(trackNr)` | Read generator UI values. | buttonHandler PATGEN page entry |
| `euklid_setLength(trackNr, value)` | Set generator length; caller re-applies steps. | Menu |
| `euklid_setSteps(trackNr, value, patternNr)` | Set pulse count and regenerate PatternData main steps. | Menu |
| `euklid_setRotation(trackNr, value, patternNr)` | Set generator rotation and regenerate PatternData main steps. | Menu |
| `euklid_rotatePattern(length, amount)` | Rotate generated mask. | Euklid internals |
| `euklid_transferPattern(trackNr, patternNr)` | Transfer generated mask/length into PatternData. | Euklid internals |

## Core/Bank/Scene/Pattern/SomGenerator

Affiliate modules: Menu, Sequencer, random, SomData.

Purpose: owns SOM generator state. Menu configures it directly; Sequencer ticks
it during playback when SOM mode is active.

| API | Use | Usual callers / clients |
|---|---|---|
| `som_init()` | Initialize SOM coordinates/frequencies/flux. | `main.c` boot |
| `som_tick(stepNr, mutedTracks)` | Generate SOM voice triggers on main-step boundaries. | Sequencer |
| `som_setX(x)` / `som_setY(y)` | Set normalized interpolation coordinates from menu values. | Menu |
| `som_setFlux(flux)` | Set randomization amount. | Menu |
| `som_setFreq(freq, voice)` | Set per-voice trigger threshold/frequency. | Menu |

## Core/Hardware/frontPanel/ledHandler

Affiliate modules: Sequencer, Menu, buttonHandler, PatternData, dout.

Purpose: owns physical LED interpretation and rendering. Also owns deferred
Sequencer LED feedback via `SeqLedState`.

| API | Use | Usual callers / clients |
|---|---|---|
| `led_init()` | Initialize LED shadow/output state. | boot |
| `led_setValue(val, ledNr)` / `led_setValueTemp(val, ledNr)` | Set physical/logical LED value. | buttonHandler, Menu, ledHandler helpers |
| `led_reset(ledNr)` / `led_toggle(ledNr)` / `led_toggleTemp(ledNr)` | Direct LED edits. | buttonHandler, Menu |
| `led_clearAll()` | Clear all LED state. | UI flows |
| `led_tickHandler()` | Blink/pulse/flash maintenance, plus the S080 fast blink. S081 P4: pulse and flash deadlines are compared by signed 16-bit distance (`led_timeReached()`), so an effect started just before the 16-bit `time_sysTick` wrap lasts its full time (the former `now > deadline` test could lose a pulse and end a flash at once or flicker its phase). | foreground/timer service |
| `led_pulseLed(ledNr)` | Pulse one LED. | voice trigger indication paths |
| `led_setBlinkLed(ledNr, onOff)` / `led_clearAllBlinkLeds()` | Blink management; starting blink for an already-blinking LED is idempotent so duplicate slots cannot cancel visually. | buttonHandler, Menu |
| `led_setActivePage(pageNr)` | Light one page/select LED. | buttonHandler load/save |
| `led_setActiveVoice(voiceNr)` / `led_setActiveVoiceLeds(pattern)` | Voice LED display. | buttonHandler, Menu |
| `led_setActiveSelectButton(butNr)` | SELECT LED display. | buttonHandler, Menu |
| `led_setMode2Leds(value)` / `led_setMode2(status)` | Mode LED display. | buttonHandler |
| `led_clearSequencerLeds()` / `led_clearSequencerLeds1_8()` / `led_clearSequencerLeds9_16()` | Clear step LEDs. | buttonHandler, Menu |
| `led_clearSelectLeds()` / `led_clearVoiceLeds()` | Clear select/voice LED groups. | buttonHandler, Menu |
| `led_setActive_step(stepNr)` / `led_clearActive_step()` | Chase/current-step display helpers. | ledHandler internals/future callers |
| ~~`led_initPerformanceLeds()`~~ | Removed in S080 (it forced SELECT1 on in PERF). The PERF SELECT row is painted by `menu_syncPerfSelectLed()`. | — |
| `led_setFastBlinkSlot(slot, ledNr)` / `led_getFastBlinkSlot(slot)` | S080, two slots since S082 P3 (replacing `led_setFastBlinkLed()`/`led_getFastBlinkLed()`): `LED_FAST_SLOT_QUEUED_SCENE` (0, the queued Scene's SEQ LED) and `LED_FAST_SLOT_BANK_LOAD` (1, `LED_MODE4` while a background load is unlocked and the panel is not in Load/Save mode). Each slot moves `LED_LAYER_FAST` to one LED (0xFF = free); both share one phase toggled every `LED_FAST_BLINK_TIME_MS` (125, 2× the normal blink) in `led_tickHandler()`, and a slot that starts while the other runs joins its phase. Blink start/stop, `led_clearAllBlinkLeds()`, `led_setMode2()` and `led_setValue()` cannot disturb it; `led_clearAll()` frees both (the owner re-asserts them next tick). | `menu_syncQueuedSceneLed()` (sole owner) |
| `led_isBlinkLed(ledNr)` | S080: 1 when the LED holds a persistent blink slot; lets the per-beat PERF refresh leave running blinks alone. | `menu_refreshPerfSceneLeds()` |
| `led_updateCurrentStep(step)` | Repaint chase LED from Sequencer payload. Returns early on `EFFECT_PAGE` — and, since S075 F2, while the SHIFT+TRACK voice-mix overlay is shown over it (`menu_fxVoiceMixOverlayActive()`) — because the Effect page owns the SEQ row (lock LEDs, FX chase, `sel` blink) through `menuEffects_renderSeqLeds()`. `led_updateRecordedMainStep()` and the follow-mode repaint in `led_notifyPatternChanged()` use the same gate. | `led_processSeqLedState()` |
| `led_updateRecordedMainStep(…)` | S081: repaint the step a live record just wrote, by **absolute** step (`seq_markRecordedStep(track, scene, step)` sets `seq_ledState.recordMainStep` and `SEQ_LED_DIRTY_REC_MAIN`); drawn only when it is in `menu_currentBar` on the visible track. | `led_processSeqLedState()` |
| ~~`led_updateRecordedSubStep()`~~ | Removed in S081 with `SEQ_LED_DIRTY_REC_SUB` and `recordSubStep` (no writer). | — |
| `led_updatePatternTrack(track, pattern, selectedStepBase)` | Repaint all visible step/select LEDs from PatternData. | buttonHandler, Menu, pattern follow |
| `led_setBeatPulse(on)` | Apply beat pulse to START/STOP LED. | `led_processSeqLedState()` |
| `led_notifyPatternChanged(playedPattern)` | Sequencer pattern-change notification and follow-mode UI refresh. | Sequencer |
| `led_notifyTrackRotationReset(rotation)` | Update visible rotation parameter after Sequencer stop reset. | Sequencer |
| `led_processSeqLedState()` | Foreground drain of Sequencer LED dirty state. Session 071 added a transport-aware chase guard: when `seq_isRunning()` is false, `SEQ_LED_DIRTY_CHASE` events clear the chase layer via `led_clearActive_step()` instead of installing it. Includes `sequencer.h` for `seq_isRunning()`. | `main.c` |
| `led_updateAutomationStepView(track, target, scene)` | Light SEQ LEDs for steps with automation matching a target parameter (Session 066). | Menu VOICE overlay |
| `led_renderFromStack(ledNr)` | Re-render one LED from its active-layer bitmap: pulse > flash > blink/chase > base. Replaces blind `led_reset()` in all layer-expiry paths (Session 070). | ledHandler internals |

Shared object: `SeqLedState seq_ledState` is written by Sequencer and consumed
by ledHandler in foreground.

LED layer model (Session 070): `led_activeLayers[41]` is a per-LED bitmap
tracking which layers are currently active. Priority order (highest first):
pulse, fast (S080 `LED_LAYER_FAST`: the queued-Scene flash and, since S082
P3, the Load/Save mode LED during a background load), flash,
blink/chase, base. On layer expiry, `led_renderFromStack()`
clears the expired layer's bit and renders the highest remaining active layer.
Chase runs at blink priority. The held-step automation overlay writes LEDs
directly and does not participate in the layer system.

**VOICE-mode bar pulse (S081 P4).** In VOICE mode the SELECT LED of the bar
the active track is playing pulses once (50 ms `led_pulseLed()`, the pulse
layer) whenever that bar changes, without touching the SELECT row's base
state, so the sub-page indicator keeps working. The bar is read live from the
active track with `seq_captureTrackTime()` (not `seq_ledState.chaseStep`,
which carries no track id), in the `SEQ_LED_DIRTY_CHASE` branch of
`led_processSeqLedState()` via `led_updateVoiceBarPulse()`. There is no pulse
when the effective track length is 16 or less (a single bar, including after a
Morph or length automation), when the play mode is `rnd` (3), when the bar is
the one being left (pip boundary repeat, wrap within a bar), when the active
voice changed since the last drain, or on the first step after a transport
start (the bar is forgotten while stopped). A jump into another bar (realign,
looper resume, per-track Scene or length change) pulses once, like any bar
change. The track and bar are remembered in every mode. State:
`led_barPulseTrack`/`led_barPulseBar` (0xFF = none), 2 B SRAM1, ledHandler.
STEP mode keeps its own bar chaselight; PERF never shows the bar.

## Core/Hardware/frontPanel/buttonHandler

Affiliate modules: ledHandler, Menu, `menuEffects`, PatternData,
EuklidGenerator, Sequencer, Preset, MidiNotes, CopyClear, and
EffectsManager.

Purpose: owns physical button event queue, select modes, shift state, selected
step UI state, held-step automation gesture, mute UI shadow, and direct button
dispatch to owners.

| API | Use | Usual callers / clients |
|---|---|---|
| `buttonHandler_buttonPressed(buttonNr)` / `buttonHandler_buttonReleased(buttonNr)` | Scan-safe event recording into the 64-entry monotonic-counter event ring (Session 068; was a 16-entry masked ring, 15 usable, silent-drop on overflow). Runs from the foreground 500 Hz `din_dout_exchange()` scan via `timebase_serviceFrontPanel()`, **not** an ISR — corrected stale comment, Session 068. | Foreground front-panel scan |
| `buttonHandler_processEvents()` | Drain one button event and execute foreground UI actions; on ring overflow, reconciles the VOICE-Scene/Load-Scene pairing masks and the hold timer (Session 068). Called **twice** per main-loop pass since Session 068 (separated by `audio_check_and_render()`), ~30 events/500 Hz scan interval; must never call `audio_check_and_render()` itself. | `main.c` (2 call sites) |
| `buttonHandler_tick()` | Long-press timer promotion. | foreground timing path |
| `buttonHandler_getMode()` | Current SELECT mode. | ledHandler, Menu/UI checks |
| `buttonHandler_getShift()` | Current shift-held state. | ledHandler, button logic |
| `buttonHandler_setRunStopState(running)` | Sync UI transport bit and START/STOP LED. | MidiTransport (MTC start/stop), local button path |
| `buttonHandler_showMuteLEDs()` | Show mute-state LEDs. | Menu/voice/performance paths |
| `buttonHandler_muteVoice(voice, isMuted)` | Update front-panel mute shadow. | buttonHandler local, MIDI/UI paths if needed |
| `SELECT_MODE_FX` / FX dispatch | SHIFT+PERF owns the Effect page; SELECT/TRACK/BAR gestures first offer the active type's `effect_ui_hooks_t`, while default TRACK/SELECT behavior remains in buttonHandler/Menu. | `menuEffects`, EffectsManager registry |
| `seqHeldMask()` | Return the current held-step bitmask for the VOICE overlay or active FX lock editor (Session 066/ST8). | Menu overlay, `menuEffects` |
| `visibleStep()` | Return the visible step index accounting for bar offset (Session 066, promoted to extern). | Menu, ledHandler |
| Copy/clear router (S075) | `processPress()`/`processRelease()` offer every edge to `copyClear_buttonPressed/Released()` first (nonzero = consumed); `case BUT_COPY` calls `copyClear_copyPressed(shift)` except SHIFT+COPY while live record is armed (the live-erase chord, S081); the event loop calls `copyClear_postEvent()` and, on ring overflow, `copyClear_eventOverflow()`. | CopyClear |
| VOICE hold-SHIFT Morph view (S075 F2) | SHIFT press in VOICE mode → `menu_setVoiceModeShowMorph(1)`; release → back to the SHIFT+MODE VOICE latch (`buttonHandler_morphVoiceModeActive`). | Menu |
| STEP hold-SHIFT Morph view (S078) | SHIFT press in STEP mode → `menu_setPatternTrackMorphEndpoint(1)` (+ LED leave); SHIFT release in STEP mode → `menu_setPatternTrackMorphEndpoint(0)` + `buttonHandler_enterSeqModeStepMode()`. A release handled in another mode (SHIFT+mode button) cannot clear it, so Menu also gates the view on `buttonHandler_getShift()` (S078 P4). No latch: SHIFT+MODE STEP is the only SOM entry. | Menu |
| STEP held steps + TRACK (S078) | In `handleVoiceButton()`'s STEP arm, `buttonHandler_seqHeldMask() != 0` calls `menu_enterStepTrackAutomationOverlay(voiceNr)` and returns before the ordinary half toggle. | Menu |
| PERF looper / overlay / SHIFT+REC / chain chord (S080; overlay S083 Q5) | See "PERF performance surface". Hold state `bh_loopSelectHeldMask`, `bh_loopLastSelectButton`, `bh_loopTrackMask` (mask returned by the sequencer), `bh_loopBarModifier`, `bh_loopBarHeld` (event-order BAR holds); helpers `buttonHandler_loopCheckDisengage()`, `buttonHandler_loopForceRelease()`; overlay `bh_overlayActive` + the per-track table `bh_overlay[7]` (heldMidiNote, note + `def` bit, roll rhythm position (was velocity, always 100), count 0/1/0xFF, interval, next; serviced in `buttonHandler_tick()`); latches `bh_shiftRecMenuActive`, `bh_shiftReleaseConsume`, `bh_chainPlayArmed`. | Sequencer, Menu, ledHandler, MidiNotes |
| MIDI overlay entry points (S083) | `buttonHandler_overlayActive()` (the one BAR overlay state 0/1/2, any mode), `buttonHandler_setOverlay(which)` (CC2 on G: same as a BAR press — state, BAR LEDs in PERF unless the looper owns them, held entries re-configured), `buttonHandler_overlayMidiNoteOn(track, which, internal, midiNote, ageUs)`, `buttonHandler_overlayMidiNoteOff(track, midiNote)` (ends a MIDI-held roll; a flam main still plays). One executor and one state for panel and MIDI; the C-9 BAR-over-CC2 priority is retired (2026-10-22). | MidiNotes |
| Learn LEDs (S083) | `buttonHandler_setLearnLeds(on)`: on flashes SHIFT/PLAY/REC; off restores SHIFT hold, transport and REC LATCH states. | MidiLearn |
| `buttonHandler_loopSelectLed()` | S080: SELECT index (0..7) of the looper division while the looper is held or engaged, else 0xFF. | `menu_syncPerfSelectLed()` |
| REC button (S081) | Press: with SHIFT and the SHIFT+PLAY chord armed → `midiLearn_arm()` (S083, first test); SHIFT → SHIFT+REC menu; menu open → close it; live erase on → nothing (SHIFT or not); otherwise start the hold timer (`bh_recPressTick` `u16`, wrap-safe, `bh_recArmed`). `buttonHandler_tick()` enters OVERDUB once the hold passes `DOUBLE_CLICK_TIMEOUT` (300 ms) (`liveRec_overdubBegin()` + REC LED pulse). Release: an overdub → `liveRec_overdubEnd()` + pulse; otherwise toggle `liveRec_setLatch()`. REC LED flashes while SHIFT+REC is open and armed. REC works while looping (S081 P3). | LiveRecord |
| Live-erase chord and router (S081) | `bh_shiftHeld`/`bh_clearHeld` are updated before any early exit. With `liveRec_armed()`, SHIFT+COPY in either order calls `buttonHandler_liveEraseBegin()` (the single entry; clears the gesture masks only when entering from off) → `liveRec_eraseBegin()`; SHIFT and CLEAR LEDs stay lit while either is held; releasing both calls `buttonHandler_liveEraseEnd()` (the single exit: `liveRec_eraseEnd()`, REC LED restored, automation-underline search restarted). The SHIFT release runs the erase-end test before its consume/latched-menu breaks. Router right after `copyClear_buttonPressed()`: SEQ in VOICE/STEP → `liveRec_eraseSeqHeld(activeTrack, held)`, in FX → `liveRec_eraseFxSeqHeld()`, else consumed; VOICE in VOICE/STEP/PERF/FX → `liveRec_eraseTrackHeld()` (not while the loop is engaged: the VOICE stays a looper button), else consumed; PLAY → plain transport toggle (`buttonHandler_setRunStopState()` + `seq_setRunning()`, bypassing `bh_chainPlayArmed`). Per-button gesture masks `bh_eraseSeqMask`, `bh_eraseFxSeqMask`, `bh_eraseVoiceMask` end exactly the gesture a press started (even after erase ended); a MODE press clears them and calls `liveRec_eraseReleaseHeld()`. | LiveRecord |
| Overlay recording and R19 (S081) | `buttonHandler_overlayReport()` feeds `liveRec_triggerIn()` after each overlay `seq_triggerVoice()`: SINGLE / FLAM_GRACE / ROLL_START from `buttonHandler_overlayTrigger()`, FLAM_MAIN / ROLL_HIT from `buttonHandler_overlayTick()` (the 1 ms timer, reset = `bh_overlayCount != 0xFF`); `buttonHandler_overlayRelease()` → `liveRec_rollRelease()`; `bh_overlayNoteDefault` marks a `def` note. `buttonHandler_overlaySwitchHeld()` (`bh_overlayHeldType`) re-configures a held VOICE trigger when the other BAR is pressed or the held overlay's BAR is toggled off (roll ↔ single/flam per F-B/F-O) and calls `liveRec_rollRateChange()`. `handleModeButtons()` shows the BAR overlay LEDs only in PERF. | LiveRecord, Sequencer |
| Q13 STEP refusal (S081) | `buttonHandler_setRemoveStep()` refuses a step toggle silently while `liveRec_activePatternLocked()` (writing into a per-track target Scene other than the active one). | LiveRecord |
| `buttonHandler_enterLoadSaveForBackgroundLoad()` | S082 P1 D7/D32: put the front panel into Load/Save for a MIDI-origin Bank load. Closes latched surfaces (PERF looper, SHIFT+REC, overlay, chain chord), ends an open copy/clear session, sets mode LEDs. Menu performs the page change. | `menu_backgroundBankLoadUiBegin()` |
| Background load panel gates (S082 P1 R7, P3) | While the load is **locked** (`preset_backgroundBankLoadLocked()`: phases 1–4 and 6, or a load requested with the transport stopped), MODE presses and SHIFT+REC entry are dropped (mode LEDs unchanged, nothing queued) and `menu_switchPage()` drops every page request. Once it is unlocked (phase 5 of a progressive load) MODE and SHIFT+REC work; Menu then shows only the frozen Bank Load screen for Load/Save. VOICE on the Load/Save page stays consumed for the whole load (`menu_loadInstrumentVoicePressed()`). | handleModeButtons, buttonHandler_shiftRecMenuEnter, menu_switchPage, menu_loadInstrumentVoicePressed |
| Bank page COPY (S082 P0) | In Load/Save mode `case BUT_COPY` first offers the press to `menu_bankCopyPressed()` (Bank page, no command active) and records `bh_bankCopyPressTick`; `buttonHandler_tick()` promotes a held COPY at `BUTTON_TIMEOUT` (200 ms) with `menu_bankCopyHoldReached()`; the release goes to `menu_bankCopyReleased()`; an event-ring overflow calls `menu_bankCopyForceRelease()`. `bh_clearHeld` is not set on this path, so copy/clear and the live-erase chord never see it. | Menu |
| Scene selection during a load (S082 P3) | PERF Scene/view presses, VOICE+SEQ per-track moves and the SHIFT+SEQ queue accept only `bank_sceneAvailable()` Scenes (equal to present outside a load); the STEP toggle also refuses while `menu_viewedSceneLoadPending()`. | Menu, BankData |
| `buttonHandler_clearAllMutes()` | S080: clear the VOICE mute mirror; repaint the VOICE row only when it shows mutes. | `menu_applyQueuedSceneAction()` |
| `handleModeButtons()` | The only writer of `selectButtonMode`. With SHIFT held a MODE button selects `(mode + 4) & 7` (SHIFT+VOICE = VOICE Morph latch), so SHIFT-derived view state set in the old mode must not rely on the SHIFT release (it runs in the new mode). | buttonHandler |
| Effect-page SHIFT+TRACK voice-mix overlay (S075 F2) | After the type's TRACK hook declines, SHIFT+TRACK in FX mode selects the track and calls `menu_fxVoiceMixOverlayBegin()`; `buttonHandler_fxVoiceMixTrackMask` (1 B) holds the TRACKs keeping it up. While the mask is nonzero every TRACK press joins it and moves the overlay to that track; **no TRACK press mutes**. The last release calls `menu_fxVoiceMixOverlayEnd()` (release consumed before copy/clear routing). SHIFT under the overlay switches the voice Morph view; default SELECT navigation is skipped (type SELECT hooks still run); overflow clears the mask and ends the overlay. | Menu, ledHandler |

Public state used by other modules:

- `buttonHandler_selectedStep`: selected sub-step base for LED/menu refresh.

## Core/Menu/menu

Affiliate modules: buttonHandler, ledHandler, PatternData, Preset, Sequencer,
Core/MIDI (MidiIn/MidiControl/MidiLearn/MidiOut), EuklidGenerator,
SomGenerator, filesystem, triggerJacks, mod nodes, InstrumentManager.

Purpose: owns menu pages, visible/edit state, `parameter_values`, dynamic
descriptor-backed VOICE page resolution, load/save UI, encoder and endless-pot
edit dispatch, and post-load operation follow-up.

| API | Use | Usual callers / clients |
|---|---|---|
| `menu_init()` / `menu_start()` | Initialize menu state and enter first page. | `main.c` boot |
| `menu_repaintAll()` / `menu_repaint()` / `sendDisplayBuffer()` | LCD frame rendering. | UI modules |
| `menu_parseEncoder(inc, button)` | Main encoder edit/navigation. | main loop |
| `menu_parseKnobDelta(knobNr, delta)` | RV1-RV4 edit dispatch. | main loop |
| `menu_serviceKnobRepaint()` | Coalesced knob repaint. | main loop |
| `menu_commitTargetEdit(id, value, origin)` | S083 Q1: the single parameter commit used by the panel and MIDI. Clamps to the menu domain, commits the Normal image, fans out through `bank_editTargetMask()`, applies the R12/R13/Scene-target actions, records in LATCH/OVERDUB and requests the repaint. | `MidiControl`, `menu_cellCommitValue()` |
| `menu_commitGlobalMorphFromMidi(value)` | CC1 on G: global Morph through the `mrp` cell, fan-out, never recorded (G-1). | `MidiControl` |
| `menu_notifyTargetChanged(id)` | Coalesced repaint request when a target ID is on screen (replaces the flat-ID `menu_notifyExternalParamChanged()`). | `MidiControl`, `MidiLearn` |
| `menu_learnDisplayChanged()` | Learn state changed: repaint `LRN`/`FUL`. | `MidiLearn` |
| `menu_perfModeSceneChangeFrom(scene, origin)` | Whole-Scene change with origin; a MIDI origin suppresses the PC echo (Q9). | `MidiProgram`, panel wrapper |
| `menu_pollPresetStatus()` | Async preset/filesystem completion and sound/global apply. | main loop |
| `menu_parseGlobalParam(paramNr, value)` | Dispatch global/menu parameter side effects to owner modules. | Menu edits, globals load |
| `menu_sendAllParameters()` / `menu_sendAllGlobals()` | Apply all parameters/globals to owners. | load/apply paths |
| `menu_serviceRuntimeWidgets()` | Runtime UI widgets such as CPU. | main loop |
| `menu_switchPage(pageNr)` / `menu_switchSubPage(subPageNr)` | Page navigation and direct Pattern/LED refresh. | buttonHandler/Menu |
| `menu_resetActiveParameter()` | Keep active parameter valid for page. | buttonHandler/Menu |
| `menu_setVoiceModeShowMorph(onOff)` | Set the VOICE-page Morph endpoint view for descriptor-backed instrument cells and the FX-send cell; repaint/edit helpers resolve the main or Morph image. Driven by the SHIFT+MODE VOICE latch, by SHIFT held in VOICE mode (S075 F2) and by SHIFT under the Effect-page overlay. | buttonHandler |
| `menu_setPatternTrackMorphEndpoint(onOff)` | S078: set the STEP-page track Morph endpoint flag (repaints when SEQ_PAGE is shown). The view is active only while the flag is set, SEQ_PAGE is shown **and SHIFT is physically held** (`menu_patternTrackMorphViewActive()`); then `len`/`scl`/`shf` display and commit the Scene endpoints (`scene_setTrackMorph*()`), while `run`/`mch`/`not` keep their Normal values (Morph-view rule). | buttonHandler SHIFT press/release in STEP |
| `menu_enterStepTrackAutomationOverlay(voiceNr)` | S078: open the STEP held-step track automation overlay for the pressed track: SEQ_PAGE first half, shared va_* held state armed, LEDs, repaint. Pot/encoder turns on `len`/`scl`/`shf` write Scene targets 405..425 to every held step (`sa_writeAutomationFromKnob()`); SHIFT+COPY + pot removes them from the held steps (`sa_clearAutomationFromKnob()`); exits on release of all steps or a page/mode/track change. | buttonHandler `handleVoiceButton()` |
| STEP multi-step specials (internal, S078) | `menu_broadcastStepSpecial()` writes velocity/note/probability to every held SEQ step of the visible bar (2+ held) through `patSvc_setStepVolume/Note/Probability()`; one held step = single-step path. | `menu_parseGlobalParam(PAR_STEP_*)` |
| `menu_fxVoiceMixOverlayBegin(track)` / `menu_fxVoiceMixOverlayEnd()` / `menu_fxVoiceMixOverlayActive()` | S075 F2 Effect-page SHIFT+TRACK overlay: Begin saves the Effect position in a 4-byte record and shows VOICE `track`'s mix sub-page on its first Scene-setting screen (a second call moves it); End restores the Effect page, applies latched `menuEffects_service()` actions and repaints. Does **not** go through `menu_switchPage()` (which would run `menuEffects_leave()`); a real page switch ends it without restore. `menu_serviceRuntimeWidgets()` keeps running `menuEffects_service()` under it so the Effect LEDs stay live. | buttonHandler, ledHandler |
| VOICE mix FX-send cell (internal, S075 F2) | Normal view shows the step override or the Normal endpoint (`preset_getFxSendDisplayAmount()`); Morph view shows and edits the Morph endpoint (`scene_getVoiceFxSendMorph()` / `preset_setVoiceFxSendMorph()`); never the interpolated send. Edits fan out over the edit mask. | Menu internals |
| `menu_setEffectShowMorph(onOff)` | Toggle the momentary SHIFT Morph endpoint view on `EFFECT_PAGE`; refreshes Effect pot snapshots and repaint without touching VOICE morph state. | buttonHandler FX SHIFT press/release |
| `MENU_CELL_EFFECT` delegation (internal) | Effect page cells resolve, format, clamp and commit through `menuEffects_*`; early `EFFECT_PAGE` branches in navigation/encoder/pots/service hand the cursor to menuEffects (Session 072 step 7). `menu_isScreenPage()` covers the four-cell compact model for VOICE and EFFECT. | Menu internals |
| `fx` step-automation category (internal) | Category 7 of the STEP automation page lists the viewed Scene's AUTOMATABLE Effect rows (`effects_stepTarget()`); WIDE8 rows display the `expand7` value; `menu_stepAutomationIsEffect()` excludes the 511 off sentinel. `fxm` in `scn` stores seven bits like voice Morph (`menu_sceneTargetIsMorph()`). | Menu STEP page |
| LFO `fx` namespace display (internal) | `lfo_target_voice` clamps to `INSTRUMENT_TARGET_VOICE_NAMESPACE_LAST` (8); value 8 displays `fx`; Effect targets render the active Scene's descriptor short/long labels or `off` when not modulatable. | Menu VOICE LFO cells |
| `menu_voiceHeldSceneButtonPressed(scene)` | VOICE-held SEQ mask toggle. Turning a Scene **on** requires `scene_editLayoutMatches(active, scene)` (same Effect type and six Instrument types); a mismatch is consumed silently. Turning off is always allowed (Session 072 step 10). | buttonHandler |
| `menu_startSoundApply()` / `menu_startInstrumentApply()` (internal) | Post-load apply funnels. Both call `bank_revalidateVoiceEditMasks()` after the retained commit; the empty-Bank branch of Bank Load does too. | `menu_pollPresetStatus()` |
| `menu_loadInstrumentVoicePressed(voice)` / `menu_loadInstrumentExit()` | Enter/select or leave nested Instrument Load/Save. Entry establishes the combined Kit/Instrument HCNAMES identity session; normal Instrument Load also writes the current voice to `.hctmp.<ext>`. Voice/exit boundaries invalidate that temporary session. | buttonHandler Load/Save VOICE gestures |
| `menu_loadSceneButtonPressed(scene)` | Consume Load/Save-context SEQ presses as Kit target toggles, Instrument Load one-Scene selection, or Instrument Save source-Scene selection. On Load:[Bank] and Save:[Bank] every one of the 16 buttons toggles, including unlit Scenes, and all-deselected is legal (S082). While Bank COPY is held a SEQ press is consumed with no change. | buttonHandler SEQ press/release routing |
| `menu_loadInstrumentTransactionBusy()` | Report read/save-plus-commit Instrument transaction ownership. Accepted request coordinates remain immutable; number-only scrolling may coalesce the newest pool row, while Scene/voice/type/mode changes are boundaries rather than retargeting. | buttonHandler/Menu gates |
| `menu_paramUsesMorphView(paramNr)` / `menu_getParameterDisplayValue(paramNr)` / `menu_getParameterEditPtr(paramNr)` | Legacy display/edit helpers for static parameter IDs. Descriptor-backed VOICE cells use the internal dynamic cell resolver instead. | Menu repaint, encoder edits, endless-pot edits |
| `menu_showStepTrackSettingsFirstHalf()` / `menu_toggleStepTrackSettingsHalf()` | Select STEP front-page first half or toggle to the second half where per-track shuffle lives. | buttonHandler STEP-mode voice/track buttons |
| `menu_resetSaveParameters()` | Reset load/save UI state for the promoted Kit/Scene/Bank families and clear nested Instrument/HCNAMES/temp-session state. Retired File/Dir types are not restored by Dev Mode. | load/save page |
| `menu_setNumSamples(num)` | Sample count display state. | sample/filesystem paths |
| `menu_getActivePage()` / `menu_getSubPage()` | Read visible page/subpage. | buttonHandler, ledHandler |
| `menu_getActiveVoice()` / `menu_setActiveVoice(voiceNr)` | UI active voice. | buttonHandler, MidiNotes, MidiProgram, PatternData callers |
| `menu_areMuteLedsShown()` | Mute LED UI state query. | ledHandler/buttonHandler |
| `menu_setShownPattern(patternNr)` / `menu_getViewedPattern()` | UI viewed/edited pattern. | buttonHandler, ledHandler, PatternData callers |
| `menu_setPlayedPattern(patternNr)` | Side-effect-free alignment of the played-Pattern UI mirror (`menu_playedPattern`) the chase renderer compares against the viewed Pattern to gate chase-LED visibility (Session 068). Unlike `led_notifyPatternChanged()` (the runtime writer, with follow/PERF/LED side effects), performs only a validated assignment — safe to call from pre-audio filesystem realignment. | `filesystem.c` Scene/Bank realignment (3 sites) |
| `menu_voiceAutoOverlayEnter()` / `menu_voiceAutoOverlayExit()` | Enter/exit VOICE-page held-step automation overlay (Session 066). Entry initializes 44-byte state block, starts async search. Exit restores normal VOICE display and clears CGRAM. | buttonHandler SEQ held-step timing |
| `menu_voiceAutoOverlayHeldChanged()` | Notify overlay that the held-step mask changed. Invalidates working values, restarts async search. | buttonHandler SEQ press/release during overlay |
| `menu_patternContentChanged()` (renamed from `menu_voiceAutoOverlayPatternDeleted()`, S075) | Restart the automation-presence search after any Pattern paste or clear and repaint on VOICE pages and the Effect page (`menu_isScreenPage()`); other pages return (their next entry restarts the search). | copyClearService |
| `menu_automationTargetCleared(target)` | Drop one target's underline at a whole-Scene pot clear without restarting the search (S075). VOICE, Effect, PERF and (S078) the STEP `len`/`scl`/`shf` bits. Not for held-step-only removals, which restart the search. | clearOps |
| STEP automation underline (internal, S078) | On SEQ_PAGE `va_scanService()` searches the active track for targets 405..425 (`va_seqTrackSearchBit()` → `VA_SEARCH_SCENE_TRACK_BIT(0..2)` = 0x10/0x20/0x40 in `va_searchSceneMask`); `sa_applyTrackMarkers()` underlines held values (overlay) or names (search). Restarts: SEQ entry, track/Scene change, `menu_patternContentChanged()`, step-automation editor target changes/deletes, held-step clear. `menu_knobClearTarget()` maps SEQ subpage-0 cells 0..2 to their track targets for whole-Scene pot clears. | Menu internal |
| `menu_copyClearMenuChanged()` / `menu_copyClearMenuClosed()` / `menu_isStorageBusy()` | Copy/clear menu repaint, close (CGRAM cache invalidation + full repaint), storage-lock view. (S075) | copyClearSession |
| Automation-presence search (internal, S066/S074) | `va_searchRestart()`, `va_scanService()` (4 step reads per pass; VOICE: the active track; Effect page: all 7 tracks of the viewed Pattern), `va_searchRecordEffectTarget()` (block-7 locals 0..62 and `fxm`; rejects local 63/`0x1FF`), `menu_effectCellAutomated()`, `menu_applyEffectMarkers()`. 13 shared bytes; every entry to a VOICE or Effect page restarts it. | Menu internals |
| `menu_effectShowHome()` | S074: jump the Effect page to the active type's layout home screen, leaving any full view (used when a type's SELECT hook returns `EFFECT_UI_SHOW_HOME`). Under the S075 overlay it only re-renders the SELECT LEDs for the saved Effect sub-page (no screen change while TRACK is held). | buttonHandler FX SELECT paths |
| `menu_serviceDeferredSceneChange()` | S080: run a fired direct / queued / chain scene change (queue actions, PC reset choice, chain repeat) and re-derive the queued-Scene fast blink. See "PERF performance surface". | `buttonHandler_tick()` |
| `menu_perfModeSceneButtonPressed(scene)` | PERF SEQ scene change; S080 defers to `seq_requestSceneChange()` when `scd` is not instant and the transport runs. | buttonHandler |
| `menu_chainMenuOpen(scene)` / `menu_chainMenuView(scene)` / `menu_chainMenuClose()` / `menu_chainMenuIsOpen()` | S080 chain editor on PATTERN_SETTINGS_PAGE; mirrors `PAR_CHAIN_*` from the viewed Scene; commits via `menu_commitChainParam()` to that Scene only. | buttonHandler |
| `menu_syncPerfSelectLed()` | S080: PERF SELECT row from looper state (division or dark). | `menu_switchPage()` PERF block, cross-sub-page step, buttonHandler looper release |
| `menu_refreshPerfSceneLeds()` | PERF SEQ Scene row; S080 rewrite (blink set, chain awareness, no blink restart). Presence comes from `menu_residentPresentSceneMask()`, which is BankData **availability** since S082 P3 (no home fallback while a load has Scenes unavailable), so Scenes not yet loaded stay dark and the row fills left to right; no Scene is lit for Scene 17 (D8). | `menu_switchPage()`, beat drain, scene change, chain editor, copy/clear, background-load fill observer |
| Bank page (S082 P0) `menu_bankCopyPressed()` / `menu_bankCopyHoldReached()` / `menu_bankCopyReleased()` / `menu_bankCopyForceRelease()` / `menu_bankCopyArmed()` | COPY on Load:[Bank]/Save:[Bank]: a press holds the gesture (SEQ and OK consumed), the hold threshold peeks at the other view (Load: resident RAM; Save: the selected slot), a release before it toggles this page's Merge flag (`menu_bankUiFlags`), a forced release never toggles. Labels `ok`/`mr` (Load), `ok`/`OW`/`MR` (Save). LEDs = content (`menu_residentContentSceneMask()` shared with PERF) through `menu_refreshLoadSceneLeds()`. Selection default 0xFFFF; `menu_resetSaveParametersEx(after_command)` keeps the Bank Load selection and Merge after a completed Load. Leaving Load/Save force-releases COPY and clears both Merge flags. | buttonHandler BUT_COPY, `menu_switchPage()` |
| Background-load UI (S082 P1) `menu_backgroundBankLoadUiBegin(slot)` / `menu_backgroundBankLoadUiIdle()` / `menu_backgroundBankLoadUiPoll(slot, &act, &empty)` / `menu_backgroundBankLoadAbort()` / `menu_backgroundNamesChanged()` | MIDI-origin entry: close every latched surface and session (ButtonHandler), enter top-level Load:[Bank] at the slot with Replace and all 16 selected, command active. Idle = nothing else owns Menu storage, the command, a queued exit, an Instrument transaction, Preset or the facade (D33 deferral). Poll = `MENU_BG_UI_WAIT` / `READY` (masks as OK would post) / `REFUSED`. Abort ends the command (quietly when off Load/Save) and re-requests the shown slot's preview. Names-changed drops name scratch keyed to Scene 17 after every build. | Preset background tick phases 0, 6 and failures |
| Panel during an unlocked background load (S082 P3) `menu_backgroundLoadReleasesPanel()` / `menu_bankLoadFrozenPageActive()` / `menu_viewedSceneLoadPending()` | Released panel = unlocked load and the page is not Load/Save: the storage gates of the encoder, pots, knob repaint, runtime widgets, STEP overlay, copy/clear COPY, the `menu_switchPage()` busy queue and its final repaint all ignore the busy command there. Frozen page = unlocked load on LOAD_PAGE (`menu_enterFrozenBankLoadPage()`: Bank type, `EDIT_TYPE`, slot = `preset_getRequestSlot()`, name = `filesystem_bankLoadDisplayName()`, no fresh-entry work, no Load↔Save toggle; SEQ LEDs drawn PERF-style, including the per-beat refresh). Viewed-pending = the viewed Scene is unavailable: STEP and track-setting edits on SEQ_PAGE are dropped like the S081 H-2 refusal. | Menu gates, ledHandler, buttonHandler, copyClearSession |
| Background-load completion (S082 P3, internal) | In `PRESET_OP_BANK_LOAD` an unlocked load never re-applies the active Scene (it is 17, kept, or a loaded child the user already activated). Off Load/Save the command ends through `menu_finishBackgroundLoadCommandOffPage()` (S058 end, command and busy flags cleared, no Load/Save reset, repaint or failure overlay); on the frozen page it ends exactly as after a normal Bank Load (final index restore, D7). `menu_refreshBackgroundLoadSceneLeds()` (1 B count of available Scenes) repaints the Scene row when a child lands and once when the unlock ends. `menu_syncQueuedSceneLed()` owns both fast-blink slots. | `menu_pollPresetStatus()` |
| Name-session ownership (S082 P1 A3, internal `menu_flushForeignNameRows(source)`) | One identity block serves one Scene at a time: the Load page's name session is the Scene Load writes (the home Scene on 17), and entering a Save context flushes rows owed for any other Scene first (every Scene when the source is 17). Prevents a save from writing its name into another Scene's HCNAMES row (which could empty that Scene at boot). | Kit/Instrument/Scene Save entry |
| Live-record hooks (S081, internal) | `menu_liveRecRecordCell(subPage, position)` runs after the pot (`menu_parseKnobDelta()`) and encoder (`menu_encoderChangeParameter()`) commits, also on a clamped detent: when `liveRec_mode()` is LATCH/OVERDUB and the cell resolves through `menu_cellClearTarget(page, position)` (the pot-clear resolver split out of `menu_knobClearTarget()`), it calls `liveRec_paramIn(target)`. Exclusions test the top-level `menu_activePage` (never the sub-page index): RECORDING, MENU_MIDI, LOAD, SAVE pages; Morph views (`voiceModeShowMorph`, the STEP and Effect Morph views); track length; `scl`/`shf` while the viewed Pattern is not the active Scene. PERFORMANCE_PAGE Morph cells `1VM`..`6VM`/`fxm` are recorded (S081 P3 FQ5); `mrp` (`PAR_MORPH`) resolves to no target and is never recorded. Held-step overlays return before the hook. Live-erase knob branch (before the copy/clear pot branch): `liveRec_mode() == ERASE` → `liveRec_eraseParam(target, fxLane)`, no value change, no underline drop. | LiveRecord |
| `menu_automationTargetRecorded(scene, track, target)` | S081 hardware fix 1: set the automation-presence bit a live-record write just made true (on the shown Pattern; on VOICE/STEP pages only for the searched track) and repaint if a completed result changed, without restarting the search. Shares the scan's classifier `va_searchRecordTarget()`. | LiveRecord step-end and first-change writes |
| R12 commit sites (S081) | `menu_cellCommitValue()`: INSTRUMENT (Normal or Morph image) → one `seq_menuOverrideSet(cell->slot, cell->descriptor_index)` before the edit-mask Scene loop; SCENE_SETTING audio out / FX send → `preset_clearAudioOutStepOverride()` / `preset_clearFxSendStepOverride()`; KIT_SETTING `7dc` → its clear; STEP `len`/`scl`/`shf` Normal and Morph commits → `menu_clearTrackTimingOverride()` (→ `presetMorph_clearTrackParamStepOverride()`). Each pairs with `seq_clearSceneAutomationBit()`. | Sequencer, Preset |
| Q13 refusal (S081) | While `liveRec_activePatternLocked()` (writing into a target Scene T ≠ active), `menu_parseKnobDelta()`/`menu_parseEncoder()` refuse edits that write the viewed Pattern (`PAR_STEP_*`, track length/scale/shuffle/run mode/loopable, the step-automation editor, `va_writeAutomationFromKnob()`, `sa_*`, `menu_broadcastStepSpecial()`) before `parameter_values[]` or the display change. | LiveRecord |
| `rtk` cell (S081) | RECORDING_PAGE sub-page 2 cell 0, `PAR_AUTOM_TRACK` (`rtk`/`RecTrk`, `DTYPE_0B127` clamped 0..6, shown 1..7, runtime only, excluded from the RECORDING_PAGE settings-dirty mark); display and edit seed read `liveRec_recordTrack()`; commit via `menu_parseGlobalParam()` → `liveRec_setRecordTrack()`. `menu_setActiveVoice()` calls `liveRec_onActiveTrackChanged()` when the track changes. | LiveRecord |
| S081 lists and cells | `quantisationNames` gains `trk`; `stepRollRateNames` = `off 1/8 8tr 16 16t 32 32t 64 64t 128` (STEP roll clamp and `menu_getRollRateName()` limit 9); `PAR_ROLL`, `rollRateNames`, `MENU_ROLL_RATES` retired; SEQ_PAGE sub-page 0 cell 7 `asm`/`Automtn` (`PAR_TRACK_AUTO_SMOOTH`, `DTYPE_ON_OFF`, a no-op in `menu_parseGlobalParam()`, commits `pat_setTrackAutoSmooth()` in `menu_cellCommitValue()`). | — |
| S080 static-param cells (internal) | `PAR_PERF_*`, `PAR_SCENE_*`, `PAR_CHAIN_*` use `DTYPE_0B127` with names from `menu_getS080StaticName()` (`perfBtnTypeNames`, `sceneChangeDivNames` (scd), `sceneQueueDivNames` (qcd), `sceneQueueActionNames`, `chainConditionNames`, `chainNextNames`) and clamps in `menu_clampCellValue()`. `PAR_TRACK_LOOPABLE` (on/off) and `PAR_CHAIN_*` commit in `menu_cellCommitValue()`; `menu_parseGlobalParam()` ignores them. RECORDING_PAGE edits count as Global for the settings.cfg debounce. | Menu internals |
| Bus compressor settings page (internal, S074) | `MENU_MIDI_PAGE` sub-page `MENU_GLOBAL_SCENE_SUBPAGE` (3 since S080; the scd/qcd/qac sub-page 2 precedes it), always last. Cells `cmp cam ctm csc` mirror the active Scene through `PAR_BUS_COMP_*` (58..61). Commits go through a dedicated `menu_cellCommitValue()` branch into `preset_setBusCompSetting()` for every VOICE-edit-masked Scene; `menu_parseGlobalParam()` only refreshes the mirrors for these ids, so a Settings Load can never write Scenes. Cues `^` (first screen) and `+` (this page) in `checkScrollSign()`. BC18 diagnostic boot check (cmp at top1, csc at top4, empty top5, nothing after). S080: the sub-page before it shows `^` when it has no second half. | Menu internals, Preset |

Shared state used by clients:

- `parameter_values[]`, `parameters2[]` for legacy/static parameter paths.
- Active Scene instrument images for descriptor-backed VOICE cells.
- `menu_activePage`, `menu_activeVoice`, `menu_playedPattern`,
  `menu_shownPattern`, `menu_muteModeActive`
- `modTargets[]`, `paramToModTarget[]` for legacy/static target naming.
  Descriptor and Scene target cells display labels through InstrumentManager
  and SceneModTargets, with no hardcoded per-instrument target lists in Menu.

## Core/Menu/menuEffects

Affiliate modules: Menu, buttonHandler, EffectsManager, SceneData, and
ledHandler.

Purpose: owns the registry-driven `EFFECT_PAGE`: SELECT screen memory and
layout, linear cursor navigation, compact/full cell formatting, the `typ`
click-in/turn/click-out transaction, and the momentary SHIFT Morph view.
`menu.c` delegates generic cell rendering, encoder, and endless-pot plumbing
through `MENU_CELL_EFFECT`; `menuEffects` never calls SceneData Effect setters
directly. All retained UI writes use the EffectsManager edit API, which fans
them out through the active VOICE edit mask (Step 10). Step 8 adds foreground
lock editing, shared track/FX scale labels,
and page-owned SEQ LEDs; the Pattern chase yields to this LED layer.

| API / data | Use | Usual callers / clients |
|---|---|---|
| `menuEffects_enter()` / `menuEffects_leave()` | Initialize or discard page-local screen, type-candidate, and Morph-view state. | `menu_switchPage()` |
| `menuEffects_resolveCell()` / `menuEffects_move()` / `menuEffects_selectPressed()` | Resolve the current registry-driven cell and navigate screens/SELECT buttons. | `menu.c`, buttonHandler |
| `menuEffects_cellCommit()` / `menuEffects_typeBrowse()` / `menuEffects_editModeChanged()` | Commit retained edits or run the `typ` transaction through EffectsManager. | `menu.c` |
| `menuEffects_hookSelect()` / `menuEffects_hookTrack()` / `menuEffects_hookBar()` / `menuEffects_renderLeds()` | Dispatch optional type UI hooks. | buttonHandler, Menu |
| `menuEffects_seqButtonPressed()` / `menuEffects_seqHoldExpired()` / `menuEffects_holdEdit()` / `menuEffects_holdDisplay()` | Own FX SEQ tap/hold routing, held-step lock writes, and locked/unlocked display values. Non-sequenceable cells are inert during a hold. | buttonHandler, Menu |
| `menuEffects_renderSeqLeds()` | Paint lock LEDs, selected-step blink, and running FX chase, then dispatch the type hook. | Menu foreground |
| `menuEffects_service()` action bits | `MENU_FX_ACT_REPAINT` (Scene/type change → `menu_repaintAll()`), `_REPAIR`, `_EXIT_EDIT`, and S074 `MENU_FX_ACT_HOLD_REPAINT` (hold start/change/release → `menu_repaint()`, taking precedence so CGRAM markers move in order). | `menu_serviceRuntimeWidgets()` |
| `menuEffects_cellSeqLocked(cell)` | S074: nonzero if the cell's lane is locked on any of the 16 steps of the active Scene (ignores `len`). | `menu_effectCellAutomated()` |
| `menuEffects_screenHasCustomRow0()` / `menuEffects_paintRow0()` | S074: a type-painted compact top row for flagged layout screens (columns 0..14). | `menu_repaintGeneric()` compact branch |
| `menuEffects_formatValue3()` / `menuEffects_formatParamValue3(cell, value, dst)` | Value text; S074 routes type rows through the type's `format_value3` hook first (Effect page only). | Menu compact/full view, held values |
| `menuEffects_renderSelectLeds(sub_page)` | S074: the only Effect-page SELECT LED writer: the type's `render_leds` when it owns the SELECT LEDs, else `led_setActiveSelectButton()`. | Menu page entry, cursor repair, encoder; buttonHandler |
| `menuEffects_home()` / `menuEffects_liveRefreshWanted()` | S074: the layout home screen; whether the page needs the live-refresh cadence (a `format_value3` hook is present). | `menu_effectShowHome()`, `menu_sceneLiveRefreshService()` |
| `menuEffects_shownParam(index)` / `menuEffects_editParam(index, value)` | S074: the value a row shows (the last held step's lock where locked, else retained); a held-aware write (retained edit with no hold, locks on every held step during a hold). | Type UI hooks (CrumpBit) |

## Core/Menu/CopyClear (S075; replaces copyClearTools)

Affiliate modules: buttonHandler, Menu, ledHandler, PatternData,
PatternStackService, SceneData, BankData, presetManager, EffectsManager,
filesystem, Autosave. Reference: `COPYCLEAR_UTILITIES.md` (behaviour,
architecture, engines, trace layouts); history:
`../log_archive/075_SESSION_HANDOFF_LOG.md` §4–§10.

Purpose: Phase 6 copy and clear of Pattern data (step, range, bar, track) and
of Scenes and Scene children (Instrument, Kit, Effect, FX sequence, settings,
Pattern), with pot clears of automation. Four file pairs:

| File pair | Owns |
|---|---|
| `copyClearSession.c/h` | operation phase, source and range rule, menus and selection, button routing and edge pairing, LEDs, the suspension predicate |
| `copyOps.c/h` | copy menus, paste requests, retargeting, merge rules, Scene-level paste executors |
| `clearOps.c/h` | clear menus, clear requests, EFFECTS SEQ clear, pot-clear front end, Scene-level clear executors |
| `copyClearService.c/h` | queue (4), run state, Pattern paste/clear engines, whole-region copy/reset, pot-clear register (8), lazy borrowed 9 kB name buffer, early trigger masks/restore, trickle governor, name remap and HCNAMES write, DEV stage-`c` trace feeds |

| API | Use | Usual callers / clients |
|---|---|---|
| `copyClear_init()` | Boot reset (calls `ccSvc_init()`). | `main.c` after `patSvc_init()` |
| `copyClear_copyPressed(shift)` / `copyClear_copyReleased()` | Start an operation (copy, or clear with SHIFT); end menu interaction. | buttonHandler `BUT_COPY` |
| `copyClear_buttonPressed(btn)` / `copyClear_buttonReleased(btn)` | Route SEQ/SELECT/TRACK/BAR/MODE edges first; nonzero = consumed. | buttonHandler `processPress/Release()` |
| `copyClear_postEvent()` / `copyClear_eventOverflow()` | Re-assert operation LEDs after each event; reset pairing on ring overflow. Source-row blink is not owned by copy/clear. | `buttonHandler_processEvents()` |
| `copyClear_menuVisible()` / `copyClear_formatMenu(r0, r1)` | Menu overlay. | `menu_repaint()`, `va_queueMarkerTransaction()` |
| `copyClear_ownsEncoder()` / `copyClear_encoderTurned(inc)` | Encoder ownership while held (clicks ignored). | `menu_parseEncoder()` |
| `copyClear_ownsPots()` / `copyClear_potTurned(target)` | Pot ownership; pot clear in a clear operation. | `menu_parseKnobDelta()` |
| `copyClear_isClearMode()` | S078: nonzero only while the held gesture is a clear (`CC_OP_CLEAR`); `copyClear_ownsPots()` is true for copy too. | Menu STEP held-step overlay pot intercept |
| `copyClear_backgroundSuspended()` | AutoSave/maintenance suspension predicate. | `filesystem_tick()` gates, `patSvc_tick()` repair gate |
| `ccSvc_tick()` | One bounded service step at 500 Hz. | `timebase.c` after `patSvc_tick()` |
| `ccSvc_targetPending(target)` | Underline filter for targets waiting in the register. | `va_scanService()` |
| `ccSvc_pasteTriggersNow(slot)` / `ccSvc_triggersChangedUi()` | Capture/write early trigger masks for an accepted replace/merge paste and notify the visible Pattern UI. | `copyOps`, Menu |
| `ccSvc_traceRetargetDropped()` / `ccSvc_traceDropReason()` / `ccSvc_traceOpSequence()` | DEV-only stage-`c` summary feeds; production calls are no-ops. | `copyOps`, `clearOps`, session |
| `ccSvc_dropQueued()` | S082: drop every queued job that has not started (the running job completes; early-trigger bits restored; each drop traced). | Preset background tick phase 2 |
| Background-load gates (S082) | Scene 17 is a source and a non-overlapping destination; an overlapping paste while 17 exists drops (`SCENE17`, 9). During an unlocked load the COPY session is allowed off Load/Save; each job is checked when it runs (`ccSvc_jobLoadPending()`): a destination or paste source not yet available drops silently (`LOAD_PENDING`, 10). The name loan (`ccSvc_ensureScratch()`) is refused and the name-retry budget frozen while `menu_isLoadSaveCommandActive()`, so a copy's names publish after the load and its final index restore. | `ccSvc_tick()` |

Calls out: `patSvc_beginExclusive/endExclusive/exclusiveCompactStep/
exclusiveEvacuateSwapStep`, the PatternData raw block API,
`filesystem_borrowNameCacheScratch/returnNameCacheScratch/identityRow/
requestCopyResidentNames/clearResidentRefreshed`, `scene_commitSettings/
commitKit/commitEffectRecord/settingsDefaults/resetKitToDefaults`,
`bank_sceneFanoutMask/exchangeVoiceEditMask/resetVoiceEditMaskToSelf/
revalidateVoiceEditMasks`, `preset_startInstrumentCopy/applyWorkersIdle/
tickInstrumentApply/startDrumsetApply/applySceneSettings`,
`effects_pasteRecord/resetRecord/pasteSeqStep/clearSeqLanes`,
`menu_copyClearMenuChanged/Closed`, `menu_patternContentChanged`,
`menu_automationTargetCleared`, `led_setBlinkLed/flashGroup`.

## Core/Sequencer/sequencer

Affiliate modules: PatternData, ledHandler, Menu, MidiNotes, MidiProgram, MidiOut, MidiVoiceControl,
triggerJacks, clockSync, SOM, USB/UART MIDI.

Purpose: owns playback timing, transport, mute, roll, quantization, external
sync state, MIDI note output, current playback pattern, runtime step indices,
and recording gates. Pattern storage reads/writes go through PatternData APIs;
Sequencer no longer exposes `seq_patternSet`, `seq_tmpPattern`, or
`seq_selectedStep` compatibility names.

| API | Use | Usual callers / clients |
|---|---|---|
| `seq_init()` | Initialize automation nodes, runtime indices, PatternData. | `main.c` boot |
| `seq_tick()` | Advance playback when due. | TIM3 owner |
| `seq_triggerVoice(voiceNr, vol, note, reset)` | Trigger one voice. S081 `reset` (the step reset rule, `PATTERN_DYNAMIC_STACK.md` §6.2): 1 for a step's first trigger (on-time hit, deferred fire, `seq_rollBegin()` first hit), overlay presses, preview and SOM; 0 for roll retriggers 2+ (`seq_processRollTicks()`); the overlay 1 ms timer passes `bh_overlayCount != 0xFF`. Reaches `voiceControl_noteOn()` / `voiceControl_noteOnNoReset()`. | Sequencer, SOM, buttonHandler overlay |
| `seq_previewVoice(voiceNr)` | Trigger the selected voice while transport is stopped without reading or advancing step state. | buttonHandler selected-voice re-press |
| Internal scheduler helpers (`seq_advanceTrackStep()`, `seq_processSchedulerTick()`, `seq_processShuffleDelays()`, `seq_stepIndexForMode()`) | S078: per-PPQ-tick, each track adds 256 to its Q8.8 accumulator and advances while ≥ `stepScale_ticksQ8(effective scale)`; shuffle deferrals tick down first; the run mode (`region->track_play_mode`, `seq_trackPlayState[]`) moves the index; wrap at the effective length. Timing comes only from `seq_effectiveTrack*[]`. See `PATTERN_DYNAMIC_STACK.md` §6.4. | Sequencer only |
| `seq_refreshTrackEffectiveParams()` | S078: recompute `seq_effectiveTrackLength/Scale/Shuffle[7]` per track from its played Scene via `presetMorph_getTrackEffective*()`. | `presetMorph_tick()` (every foreground pass), `seq_init()`, Scene activation, per-track assignment/coalesce, `seq_setStepIndexToStart()` |
| `seq_resetDeltaAndTick()` / `seq_resetToPatternStart()` / `seq_setDeltaT(delta)` | Clock/reset timing control. | trigger/MIDI sync paths |
| `seq_realignActivePatternToMasterClock()` / `seq_realignTrackToMasterClock(track)` | Recalculate each track's step and Q8.8 accumulator phase from `seq_elapsedPpqTicks` and the track's effective scale/length, mapped through its run mode (`seq_stepIndexForMode()`); stopped once-mode tracks are skipped (S078). | buttonHandler/PERF realign gesture, per-track Scene assignment |
| `seq_triggerNextMasterStep(stepSize)` | External clock master-step scheduling. | trigger/MIDI sync paths |
| `seq_setBpm(bpm)` / `seq_getBpm()` | Tempo. | Menu/global apply |
| `seq_sync()` | External MIDI clock tick. | MidiTransport |
| `seq_getExtSync()` / `seq_setExtSync(isExt)` / `seq_setExtSyncSource(source)` / `seq_getExtSyncSource()` / `seq_noteExtSyncActivity(source, timestampUs)` | External sync mode/activity. | Menu, MidiTransport, triggerJacks |
| `seq_setQuantisation(value)` / `seq_getQuantisation()` | Recording and looper quantisation 0..5 (`NO_QUANTISATION`, `QUANT_8`, `QUANT_16`, `QUANT_32`, `QUANT_64`, S081 `QUANT_TRACK` = `trk`). | Menu, LiveRecord |
| `seq_armActivePatternReload()` | Mark active pattern for reload/commit. | filesystem/preset load paths |
| `seq_setRunning(isRunning)` / `seq_setRunningEx(isRunning, sendRealtime)` / `seq_isRunning()` | Transport. `Ex` with `sendRealtime = 0` is the received-MIDI path (C-11: MRT forwards the message, the LXR does not send a second copy). A stop stores `seq_resumeTick` for Continue (S083 R-C). The stop branch queues `SEQ_LED_DIRTY_CHASE` after setting `seq_running = 0u`. | buttonHandler, MidiTransport, clockSync |
| `seq_continueRunning(sendRealtime)` / `seq_setResumeTick(tick)` | S083 T1: resume at the stored tick (or a plain start at 0), and set the tick from SPP x 24 while stopped. | `sync_midiContinue()`, `midiTransport_handleSystemCommon()` |
| `seq_assignTracksToScene(trackMask, scene)` | S083 P3: shared per-track Scene move (set played Scene, realign, note-off, one slot apply) for the panel and a MIDI PC on a voice channel. | buttonHandler, MidiProgram |
| `seq_rollRhythmVelocity(rhythm, vel, pos)` | S083: the pure roll velocity-rhythm rule (packed `rtm` byte: bits 7..4 cycle, bit 3 halve/double, bits 2..0 full count). Shared by step rolls (`seq_rollComputeVelocity()`) and the PERF BAR overlay rolls (`pv<`/`pv>`). | Sequencer, buttonHandler overlay tick |
| `seq_setSceneChangeOrigin(origin)` | S083 Q9: suppress the PC for a MIDI-originated whole-Scene change, including a deferred one. | `menu_perfModeSceneChangeFrom()` |
| `seq_setMute(trackNr, isMuted)` / `seq_isTrackMuted(trackNr)` | Playback mute state. | buttonHandler, Menu |
| ~~`seq_setRoll()` / `seq_setRollRate()`~~ | Removed in S081 with the legacy live roll and `PAR_ROLL`. Step rolls use `seq_rollBegin()` / `seq_processRollTicks()` with the 96-PPQ table `seq_rollPpq()` (`PATTERN_DYNAMIC_STACK.md` §6). | — |
| ~~`seq_addNote()`~~, ~~`seq_recordAutomation()`~~ | Stale rows corrected at the S081 close: neither exists in the code (removed with the old pattern storage). MIDI notes and knob/CC automation are recorded through LiveRecord (`liveRec_triggerIn()`, `liveRec_paramIn()`). | — |
| ~~`seq_setRecordingMode()` / `seq_setErasingMode()` / `seq_recordTrigger()` / `seq_quantize()`~~ | Removed in S081; LiveRecord owns record and erase ("Core/Sequencer/LiveRecord (S081)"). `seq_recordActive` stays as the published writing flag (`seq_setRecordTarget()`). | — |
| `seq_drainPendingAutomation(void)` | Foreground drain. It first takes the Effect reset latch (clearing Effect overlays, `menuHold` and `fxm`) and ends automation ramps of tracks whose next step applied (`seq_rampConsumeSteps()`, S081 P2). Each entry is skipped when live record latched or erased its target (`seq_setAutomationFilter()`) or, for an Instrument target, when an R12 menu override is set (`seq_menuOverride[slot]`). Voice targets write through `seq_writeInstrumentAutomation()` (`instrumentManager_writeRuntime()` + `seq_automation_dirty[]`) and may start a ramp (`seq_rampStartIfPaired()`); Scene targets (384..425) go through `seq_applySceneAutomation()` (`fxm` → `effects_setMorphAutomation()`); Effect targets (448..510) go to `effects_applyAutomation()`. The Effect step markers were removed in S081 (R13). | `main.c` `audio_check_and_render()` after `voiceControl_processPending()` |
| `seq_fxTakeEvent()` | Take the newest FX RESET/STEP latch byte. | `effects_service()` |
| ~~`seq_setEffectAutomationTracks(mask)`~~ | Removed in S081 with the Effect step markers (R13, `EFFECTS_BUS_REFERENCE.md` §10). | — |
| Looper (S080) `seq_loopActive` / `seq_loopEngage(mask, div, mod)` / `seq_loopUpdateMask(mask)` / `seq_loopDisengage()` / `seq_loopUpdateDivision(div, mod)` / `seq_loopableTrackMask()` | Master-clock-anchored per-track loop (`PATTERN_DYNAMIC_STACK.md` §6.5). Engage returns the mask now looping (0 when stopped); UpdateMask resumes leaving tracks, snapshots joining ones, keeps the rest. All publishes under PRIMASK; wrap in TIM3 before the DDA. | buttonHandler PERF |
| Live record service (S081 P1, extended P3) `seq_setRecordTarget()` / `seq_takeRecBoundary()` / `seq_captureTrackTime()` / `seq_placeFromSnapshot()` / `seq_suppressStepOnce()` / `seq_setLiveHold()` / `seq_setAutomationFilter()` / `seq_setEraseMasks()` / `seq_cancelPendingTrigger()` / `seq_getPlaybackEpoch()` / `seq_getSceneEpoch()` / `seq_getQuantisation()` | Sequencer side of the LiveRecord interface (`PATTERN_DYNAMIC_STACK.md` §6.7). `seq_rec_boundary_t` is one step-boundary ring entry: `step`, `track`, `fired` (the step's trigger fired at its start) and, since S081 P3, `jump` (a looper position jump began the step — a mid-step wrap or a resume — so it is a partial replay whose trigger did not fire this pass). Every entry is pushed by `seq_recRingPush()`, from TIM3 (step advance, mid-step wrap) or the foreground under the looper's PRIMASK (resume). `seq_track_time_t` is the PRIMASK capture; since S081 P3 it also carries the loop state (`looped`, `loopAnchor`, `loopStartStep/Accum/PlayState`, `loopTicks`) so `seq_placeFromSnapshot()` can map a shift that crosses a wrap onto the pass the target lands in. The looper and record modes run together (S081 P3 L-2); the P1 interim `liveRec_forceOff()` is removed, so engaging the loop never disarms record. The global Morph (`mrp`) is never recorded (S081 P3 G-1): it has no target, and LiveRecord writes a voice/FX Morph from the value captured at its individual edit (`liveRec_morphValue[7]`), never a value a global change overwrote. | LiveRecord |
| Live record service details (S081) | `seq_setRecordTarget(track, writing)` publishes `rtk` and the writing flag (`seq_recTrack`, `seq_recWriting`, `seq_recordActive`). `seq_takeRecBoundary()` drains the 16-entry ring. `seq_captureTrackTime()` adds `nowUs` (the capture's own µs time) to the snapshot. `seq_placeFromSnapshot(track, snap, deltaQ8, snapToStep, seq_place_t *)` returns {step, delay 0..99 from the nominal start, rel, ahead, snapQ8}; 0 before the track started. `seq_shiftPositionQ8()` is the signed-Q8 position walk it uses (`seq_loopShiftPosition()` is its whole-tick wrapper). `seq_quantiseTick(tick, frac8)` is master-grid only. `seq_suppressStepOnce(track, step, aheadTicks)` sets the echo mark and its deadline. `seq_cancelPendingTrigger(track, step)` mutes only the matching pending deferral (its automation still queues). `seq_setEraseMasks(specials, full)` publishes both masks in one PRIMASK section. `seq_setLiveHold(track, on)`. `seq_setAutomationFilter(latch, erase, eraseTrack)`. `seq_markRecordedStep(track, scene, step)` (recorded-step LED). `seq_getSceneEpoch()` changes only on a Pattern change of a track; `seq_getPlaybackEpoch()` on every playhead break. All TIM3-shared fields are `volatile` bytes or PRIMASK-published pairs. | LiveRecord |
| Step reset and deferral (S081 §5.8, internal) | `seq_fireDeferred(track)`: queue the deferred step's automation, then (unless muted by `seq_trackShuffleMuteMask`) start its roll or fire its hit with reset = 1; used by the expiry path and the early flush. Deferral state gains `seq_trackShuffleStep/Roll[7]`. A roll renewal calls `voiceControl_resetOnly()`. A running roll ends only when the next step's trigger fires (F-R). | Sequencer only |
| `seq_menuOverrideSet(slot, local)` / `seq_clearSceneAutomationBit(index)` | S081 R12: clear the held bit and set the menu-override bit (foreground); clear one Scene-target dirty bit for a per-target override clear. Overrides clear at the voice's next resetting trigger, transport, Scene change, per-track Scene change and realign. | Menu commit sites |
| FX realign (S081, internal) | `seq_fxRealignToMasterClock()` + `seq_requestEffectOverlayReset()` at the end of both realign functions: realign the FX accumulator, counter and published step to the master clock and clear Effect overlays, without an FX RESET (`EFFECTS_BUS_REFERENCE.md` §10). | Sequencer only |
| `seq_rampService()` | S081 P2: every DSP block ends the ramps of tracks whose next step applied; every third block (~2.2 ms) writes running ramps (only on a value change) through the drain's Instrument write path. Internal: `seq_rampStepEvent()`, `seq_rampPredict()`/`seq_rampNextStep()`/`seq_rampConditionPredict()` (TIM3), `seq_rampBreakTrack/All()` at every position break, `seq_rampStartIfPaired()`, `seq_rampConsumeTrack/Steps()`, `seq_writeInstrumentAutomation()`, `seq_rampNowQ4()`. Keep `seq_rampNextStep()` in step with the play-mode switch of `seq_advanceTrackStep()`. `PATTERN_DYNAMIC_STACK.md` §6.2b. | `main.c` `audio_check_and_render()` right after the drain |
| Looper while recording (S081 P3, internal) | `seq_recRingPush(track, step, fired, jump)` is the single ring producer; `seq_loopProcessTick()` pushes a jump entry at a mid-step wrap (no DDA advance follows on the tick, not SOM); `seq_loopResumeTrack()` drops the echo mark and pushes a jump entry when the step changes (callers hold PRIMASK); `seq_placeLoopImage()` maps a placement shift across a wrap. `seq_loopSnapshotTracks()` computes per-track anchors for `QUANT_TRACK` (L1). | Sequencer only |
| Scene change / queue (S080) `seq_requestSceneChange()` / `seq_queueSceneChange()` / `seq_sceneChangeFirePending()` / `seq_pendingSceneTarget()` / `seq_pendingSceneIsQueue()` / `seq_pendingSceneIsChain()` / `seq_queuedSceneTarget()` / `seq_clearPendingSceneChange()` / `seq_cancelPendingDirectChange()` | Deferred direct (`scd`) and queued (`qcd`) changes; TIM3 raises the fire flag at the division boundary (`seq_handleMasterBoundary()`); instant or stopped queue fires at once (§6.6). | Menu, buttonHandler |
| Scene chaining (S080) `seq_chainRepeatSelf(restart)` + TIM3 chain engine | Per-tick bar/repetition detection, condition weights, next-scene resolve, `stp` freeze; a chain fire becomes a chain-marked direct change (§6.6). | Menu executor |
| `seq_evaluateStepCondition(track, step)` | Read step specials and evaluate the conditional gate (41 entries since S080, incl. `lop`/`!lp` = `seq_loopActive`; `PATTERN_DYNAMIC_STACK.md` §6). Returns `step_allowed`. Called before the trigger-active check in `seq_advanceTrackStep()`. Both trigger and automation are gated by the result; erase is independent (Session 070). | Sequencer only |
| `seq_restoreAutomatedParameters(voice)` | Restore dirty descriptors from `morph_interpolation[]` and clear the per-slot dirty bitmap. S081: runs only for a trigger that carries a reset (`voiceControl_triggerNow(…, reset)`); clears `seq_menuOverride[slot]` before its early return (R12); first ends the trigger track's own ramps and skips (keeps held) parameters a running ramp holds (S081 P2 D3). `morph_interpolation[]` holds every endpoint edit and Morph change made while the value was held (S075 F3). | `voiceControl_triggerNow()`, `voiceControl_processPending()` |
| `seq_automationHoldsParameter(slot, local)` | S075 F3: nonzero while step automation holds that descriptor's runtime value (its `seq_automation_dirty[slot]` bit). Read-only; foreground only. The guard behind "automation always wins". | `presetMorph_writeRuntimeBase()` |
| `seq_restoreAllAutomation()` | Walk all 6 slots' 64-bit dirty bitmaps and restore each dirty descriptor index from `morph_interpolation[]` via `instrumentManager_writeRuntime()`. Called from `seq_setStepIndexToStart()` before `seq_clearAutomationDirty()` (Session 070). | Sequencer transport restart |
| `seq_restoreAllSceneAutomation()` | Walk `seq_scene_automation_dirty` (uint32_t) bitmap and restore each dirty Scene target from its retained SceneData getter. Clears morph step overrides, slot6 decay step state, and audio routing. Called from `seq_setStepIndexToStart()` (Session 070). | Sequencer transport restart |
| `seq_clearSceneAutomationDirty()` | Clear the `seq_scene_automation_dirty` bitmap. Rule B: called from `seq_selectActivePattern()` and `seq_alignActivePatternToScene()` so a Scene activation starts with a clean overlay state. (S076 P1) | Sequencer Scene activation paths |
| `seq_switchToBackgroundScene()` | S082: move playback to Scene 17 without a sound change. Under PRIMASK: active, pending and every per-track Scene = 16, `seq_perTrackActive = 0`, looper re-snapshot (anchors kept); then pending direct/queued/chain changes are cleared and the effective track cache refreshed. **No** realign, roll/condition/epoch reset, PC, note-off or LED notify; the live-record take continues (D35). | Preset background tick phase 3 (fresh build) |
| `seq_moveTracksToBackgroundScene(track_mask)` | S082 P1 E4.6: under PRIMASK, move selected tracks (7-bit mask) to play from Scene 17's Pattern region; recompute `seq_perTrackActive` and re-snapshot the looper; clear pending changes; refresh the effective cache. Used by the rebuild-in-place path (D19/D31); the view is never moved (D28). | presetManager |
| Scene 17 release (S082, in `seq_selectActivePattern()`) | The first whole-Scene change to a resident Scene (SEQ press, MIDI PC, queued or chain change) calls `scene_backgroundRelease()` after publishing; per-track moves never release (D10). `seq_sendProgChg()` refuses index ≥ 16; `seq_chainResolveNext()` refuses cur ≥ 16 (17 has no chain). | Menu executor, MIDI |
| Chains during a Bank load (S082 P3 D50, D52) | `seq_chainServiceTick()` keeps advancing the bar/repetition counters but returns before `seq_chainEvaluate()` while `preset_backgroundBankLoadRunning()` (one-byte ISR-safe read), because an automated target could resolve differently depending on which children have loaded. Evaluation resumes at the next boundary after the load, with the counts as they stand. Manual direct/queued changes are unaffected. | TIM3 |
| `seq_offsetTrackStepIndexForRotation(trackNr, oldRot, newRot, len)` | Narrow runtime hook for live rotation compensation. | PatternData only |

### Shared StepScale contract

`Core/Sequencer/StepScale.h/.c` owns the 128-position continuous step-scale
curve (S078) used by Pattern track scale, the FX sequencer clock and CrumpBit
sync. A scale byte is a CC 0..127: `multiplier = 0.25 × 2^(cc / (127/5))`
relative to the 24-tick 1/16 step, with 14 positions nudged to musical values
(`stepScale_stops[]`; default `STEP_SCALE_DEFAULT` = 76 = 1/16,
`STEP_SCALE_COUNT` = 128).

| API | Use | Usual callers |
|---|---|---|
| `stepScale_ticksQ8(cc)` | Q8.8 96-PPQ tick interval from the 256 B LUT (1,536..49,152); out of range → default. ISR-safe. | sequencer DDA (tracks and FX), realignment, CrumpBit (`/ 256.0f`) |
| `stepScale_shortName(cc)` / `stepScale_longName(cc)` | Symbolic label at a musical stop, NULL elsewhere. | Menu, menuEffects |
| `stepScale_isMusicalStop(cc)` | Stop membership. | Menu |
| `stepScale_formatShort(cc, out3)` | Three characters: the stop name, else the right-justified decimal CC. | Menu `MENU_TRACK_SCALE`, menuEffects, CrumpBit `crumpBit_uiFormatValue3()` |

The `.fx` file keeps its own symbolic tokens mapped to CCs in
`storageTypes.c` (`FILESYSTEM_SPEC.md`). Pre-S078 PAT4 bytes are the old
14-entry index and are not migrated on device (`tools/convert_scene_scale.py`).

The FX clock runs in TIM3 from `seq_elapsedPpqTicks`, publishing only
`SEQ_FX_EVENT_RESET`/`SEQ_FX_EVENT_STEP` plus a 0..15 index through
`seq_fxTakeEvent()`. `effects_service()` consumes the newest event in
foreground, resolves run-mode/length/scale position, and applies retained
lane locks after Morph interpolation. `effects_seqSelect()` sets the `sel`
cursor, which always applies and survives RESET and Scene switches. Scene
activation invalidates only the clock position and held Morph until the next
boundary.

## Core/Bank/Scene/Preset/presetManager

Affiliate modules: filesystem, Menu, MidiControl, Sequencer, DSP voice/mod nodes,
InstrumentManager, SceneData, and EffectsManager.

Purpose: owns preset load/save status, Scene kit apply, and sound-parameter
application. The folder now lives under `Core/Bank/Scene/Preset/`; public function
prefixes remain `preset_*` for the mechanical move.

| API | Use | Usual callers / clients |
|---|---|---|
| `preset_init()` | Initialize preset operation status. | boot |
| `preset_getStatus()` / `preset_getCompletedOp()` / `preset_getRequestSlot()` / `preset_getRequestType()` / `preset_ackStatus()` | Async operation status protocol. | Menu |
| `preset_loadDrumset(presetNr, isMorph)` / `preset_saveDrumset(presetNr, isMorph, source_scene)` | Async kit/morph load/save. Normal Kit Save writes resident normal/morph endpoints; KitMrp Save writes current interpolated values into both endpoint sections. | Menu |
| `preset_loadKitMorphForScenes(presetNr, scene_mask)` | Parse a Kit directory through normal staging, then copy source normal endpoint values into selected resident morph endpoints only where source/destination instrument types match. | Menu KitMrp Load |
| `preset_loadGlobals()` / `preset_saveGlobals()` | Async globals load/save. | Menu |
| `preset_loadPattern(presetNr)` / `preset_savePattern(presetNr)` | Async pattern load/save. | Menu |
| `preset_saveAll(presetNr, isAll)` / `preset_loadAll(presetNr, isAll)` | Async all/performance load/save. | Menu |
| `preset_loadName(presetNr, what)` / `preset_applyLoadedName()` | Async slot name browsing. | Menu |
| `preset_loadInstrument(scene, slot, type, browser_index)` | Post one immutable root Instrument request; request coordinates publish only after filesystem accepts it. | Instrument Load lower-row browser |
| `preset_saveInstrumentTemp(scene, slot)` / `preset_loadInstrumentTemp(scene, slot, type)` | Save or restore the hidden typed `.hctmp.<ext>` used by the reversible Instrument Load `kit` row. These operations publish neither HCNAMES nor `.hcindex`. | Menu Instrument Load session |
| `preset_saveInstrumentMorphTemp(scene, slot)` / `preset_loadInstrumentMorphTemp(scene, slot, type)` | Save or restore the Morph-only hidden baseline used by the reversible InstrumentMrp `kit` row. The load completion commits the staged Morph image back into the resident Morphable Morph endpoints only, never type/Normal/name/source/routing. | Menu InstrumentMrp session |
| `preset_loadInstrumentMorph(scene, slot, type, browser_index)` | Post one root Instrument request for morph endpoint import; type must match the destination slot and only morphable source normal endpoint values are copied into the resident morph image. | Instrument Load `<Type>Mrp` lower-row browser |
| `preset_saveInstrument(scene, slot, display_name)` | Post one root Instrument Save request from a resident Scene/voice slot. The display stem is captured at request acceptance and filesystem writes `Instrument/<stem.ext>`. | Instrument Save nested Save-page OK |
| `preset_saveInstrumentMorph(scene, slot, display_name)` | Post one root InstrumentMrp Save request. The writer uses the normal Instrument schema but writes the current interpolated values into both endpoint sections and does not rename resident source metadata. | Instrument Save `<Type>Mrp` OK |
| `preset_loadSceneForScenes(presetNr, scene_mask)` / `preset_saveScene(presetNr, source_scene)` | Load/save root Scene library folders through the non-Pattern Scene stage and direct Pattern phase. | Load/Save Scene |
| `preset_loadBank(slot, act_mask, empty_mask, flags)` / `preset_saveBank(slot, act_mask, empty_mask)` | S082 two-mask Bank operations. Load: act_mask Scenes are loaded exactly, empty_mask Scenes are reset to the shared empty Scene (Replace); Merge passes empty_mask = 0 and untouched Scenes stay as they are. A runtime load never changes the active or viewed Scene; only the boot flag applies the manifest active_scene. Save: act_mask children are written exactly, empty_mask children are deleted from the slot, others untouched; a Save never changes resident presence or edit masks. | Load/Save Bank, boot, S083 MIDI |
| `preset_requestBackgroundBankLoad(slot, act_mask, empty_mask, origin)` / `preset_tickBackgroundBankLoad()` | S082 P1 background Bank Load (`origin` = `PRESET_BANK_ORIGIN_MENU`/`MIDI`, `| PRESET_BANK_REQUEST_MERGE` from Menu). Every request drops queued copy/clear work and yields AutoSave (which aborts an in-flight drain at its next safe phase, R6). If a running transport plays a Scene the operation replaces, it builds and switches to Scene 17 seamlessly, then posts the ordinary two-mask load; otherwise it loads directly. Scene 17 loads and running direct loads are posted progressive and unlock the panel at phase 5 (P3). MIDI origin is deferred while anything is busy and enters Load:[Bank] when it starts. Waits are bounded (20 s). Ticked from `menu_pollPresetStatus()`. Scene 17 invariants: D28 no return (the user cannot navigate back to it); R1 nothing loads into Scene 17 — every load targets the home Scene (`scene_backgroundHomeIndex()`); D29/R2 Saves from 17 use the name capture and suppress HCNAMES publication; D19/D31 rebuild in place copies only tracks whose resident Scene is replaced (`preset_backgroundCandidateTracks(replaced)`). | Menu Bank OK, S083 MIDI CC0 |
| `preset_backgroundBankLoadRunning()` / `preset_backgroundBankLoadUnlocked()` / `preset_backgroundBankLoadLocked()` | S082: running = any background phase 1–6; unlocked = phase 5 of a progressive load (`PM_BG_UNLOCKED`, bit 3 of `pm_bg_phase`; Scene 17 loads and running direct loads); locked = running and not unlocked (the S082 P1 R7 panel drop). One-byte reads, ISR-safe. | MIDI filter, `main.c` idle servicing, chain tick, ButtonHandler, Menu |
| `preset_buildBackgroundScene(rebuild, replaced)` | S082: build Scene 17 from what is playing (fresh: copy the active `scene_t` without chain, capture its ten names, then the whole region or per track T1→T7 with fit; rebuild in place: only `preset_backgroundCandidateTracks(replaced)` tracks are emptied then copied; a non-fitting track is left empty). Returns the copied-track mask. Context: foreground before the switch/move. | Preset background tick phase 3 |
| `preset_takeBackgroundBankLoadResult()` | S082 P1 D34: return the one-shot result byte and clear it. Bits 0..1: `PRESET_BG_RESULT_DONE` (1), `PRESET_BG_RESULT_FAILED` (2), `PRESET_BG_RESULT_CODE` (3). Bit 7 (`PRESET_BG_RESULT_MIDI` 0x80) set when the load originated from MIDI. Returns `PRESET_BG_RESULT_NONE` (0) when no result is pending. | Menu `on_bank_load_complete` polling |
| `preset_backgroundCandidateTracks(replaced)` | S082 P1 D19/D31: given the replaced Scene mask, return a 7-bit track mask of tracks whose currently played resident Scene is in the replaced set. Only those tracks are rebuilt into Scene 17. | `preset_buildBackgroundScene()` |
| `preset_bankLoadFailedSceneMask()` (Session 057) | Read `pm_bank_load_failed_scene_mask`, populated from `filesystem_lastBankLoadFailedSceneMask()` in `on_bank_load_complete()`. Nonzero after an otherwise-successful (`FS_STATUS_DONE`) Bank Load means Menu still needs to show the filesystem error overlay once for the excluded child(ren). | Menu `PRESET_OP_BANK_LOAD` success path (both the empty-Bank fallback and normal sound-apply branches) |
| `preset_loadFirstAvailableSceneOrKit()` | Fallback after absent/empty Bank: lowest root Scene, then lowest root Kit, then defaults. | boot, Bank Load completion |
| `preset_sendDrumsetParameters()` | Synchronous pre-audio clear plus six-slot tagged reset/routing/descriptor-image apply. It intentionally leaves target installation to the exact ordinary Scene worker started after audio initialization. | Menu boot/load path |
| `preset_setInstrumentParameter(scene, slot, descriptor_index, image, value)` | Store one descriptor-backed Normal or Morph endpoint (AutoSave marker, card-clean bit). On the active Scene re-interpolate **only that parameter** at the resolved Morph amount (`presetMorph_applyParameterNow()`); a parameter held by step automation keeps its value until the next trigger. The runtime never receives the raw edited value. (`record_automation` removed in S075 F3.) | Menu dynamic VOICE cells |
| `preset_setSupplementalParameter(scene, slot, descriptor_index, value)` | Store one single-endpoint supplemental descriptor value. | Menu dynamic VOICE cells, storage |
| `preset_applyInstrumentRuntimeValue(scene, id, value)` | Apply one descriptor-backed instrument value to the DSP runtime through InstrumentManager. | Menu, morph/runtime apply |
| `preset_applyKitAudioRouting(scene, slot)` | Apply one Scene kit slot's audio route to mixer routing. | Kit load/apply paths |
| `preset_applySceneSettings(scene)` | Apply Scene settings/global runtime values and synchronously activate the Scene's Effect runtime. | Boot/load paths |
| `preset_applyVoiceAudioOutRuntime(voice, value)` | Apply a transient audio output routing value directly to the mixer routing register, bypassing retained Scene/Kit storage. Used by Scene automation step overlays (Session 070). | Sequencer Scene automation restore, step automation drain |
| `preset_applyVelocityModTarget(voice, targetParam)` | Direct velocity mod destination update. | Menu, preset load apply |
| `preset_applyLfoModTarget(lfo, targetParam)` | Direct LFO mod destination update. | Menu, preset load apply |
| `preset_startDrumsetApply()` / `preset_tickDrumsetApply()` | Clear outgoing modulation, quiet/trigger-time reset and image-apply all six incoming tagged slots, activate the selected Scene's Effect runtime, then keep the Scene gate active while the existing Instrument cursor normalizes/rebinds every source's two LFO pairs and velocity against the final type vector. | Menu and `main.c` post-audio boot activation |
| `preset_startKitMorphApply()` | Drain same-type KitMrp endpoint copies and refresh active-scene Morph runtime images without replacing kit membership or routing. | Menu KitMrp completion |
| `filesystem_loadedInstrumentWasTemporary()` plus `preset_startInstrumentApply(scene, slot, mark_autosave_whole_instrument)` / `preset_tickInstrumentApply()` | Filesystem exposes the existing request-local root-pool versus hidden-`kit` origin during completion; Menu passes that immutable result as the mark flag. A root-pool commit immediately marks each destination's type/Normal/Morph payload for AutoSave; hidden restore supplies zero. Active Scene path clears all outgoing modulation owners, commits/resets the incoming runtime, rebuilds all six Morph images, then normalizes/rebinds all six source target relationships. | Filesystem, then Menu Instrument completion |
| `preset_startInstrumentCopy(src_scene, src_slot, dst_mask, dst_slot)` / `preset_applyWorkersIdle()` | Resident Instrument copy through the Instrument Load commit path (`source_slot` parameter moves `self` LFO selectors); idle test of the Scene and Instrument apply workers. (S075) | CopyClear |
| `preset_startInstrumentMorphApply(scene, slot)` | Copy staged same-type Instrument normal endpoints into the destination morph image, immediately mark only the committed Morphable Morph payload for AutoSave, and refresh active-scene Morph runtime. | Menu InstrumentMrp completion |
| `preset_morph(morph)` / `preset_morphVoice(slot, morph)` / `preset_morphTick()` / `preset_getMorphValue(index, morph)` | Rate-limited descriptor Morph interpolation/application. Global Morph bulk-sets all six per-voice Morph values and the active Scene's Effect Morph amount; per-voice/Effect retained Morph values are the engine inputs. | Menu, MIDI, velocity modulation, main loop |
| `presetMorph_setVoiceLfoModulation(scene_index, target_slot, source_slot, target_pair, direction, depth)` / `presetMorph_clearLfoSource(source_slot, target_pair)` | Maintain the hidden per-voice Morph LFO overlay. Session 071 changed the setter from absolute amount to base-independent direction (`PresetMorphLfoDirection`: NONE/MAIN/MORPH) + normalized depth. The resolver computes signed deltas from the current effective base at resolution time, eliminating stale-base errors when step automation changes the base between LFO sample and resolve. | InstrumentManager/LFO dispatch |
| `presetMorph_getResolvedVoiceAmount(scene, slot)` / `presetMorph_applyParameterNow(scene, slot, local)` | S075: the amount the Morph worker uses (step override or retained base, plus active-Scene LFO contributions); re-interpolate one parameter now, store `morph_interpolation[local]` and write the runtime through the guarded `presetMorph_writeRuntimeBase()` (skips held automation). | `preset_getEffectiveFxSendAmount()`, `preset_setInstrumentParameter()` |
| `presetMorph_getTrackEffectiveLength/Scale/Shuffle(scene, track)` | S078: effective track timing value = the step override when its mask bit is set, else `presetMorph_interpTrack(Normal (region), Scene endpoint, retained voice Morph amount of the track's voice)`; length floored at 1. Tracks 6 and 7 use voice 6's amount. | `seq_refreshTrackEffectiveParams()` |
| `presetMorph_setTrackParamStepOverride(track, param, value)` / `presetMorph_clearAllTrackParamStepOverrides()` | S078: runtime-only track length/scale/shuffle step-automation overlay (3 mask bytes + 21 values). Set from the Scene automation drain (targets 405..425), cleared by `seq_restoreAllSceneAutomation()`. Never writes the Pattern region. | `seq_applySceneAutomation()`, `seq_restoreAllSceneAutomation()` |
| `presetMorph_clearTrackParamStepOverride(track, param)` / `preset_clearFxSendStepOverride(slot)` / `preset_clearAudioOutStepOverride(slot)` | S081 R12 per-target clears: a menu edit of a track length/scale/shuffle, FX send or audio out clears that one runtime override so the menu value applies until the next automation (audio out also re-applies the retained route). Callers refresh the effective cache (`seq_refreshTrackEffectiveParams()`) for track timing and pair each with `seq_clearSceneAutomationBit()`. | Menu commit sites |
| `presetMorph_setStepAutomationOverride(scene, slot, amount)` | Set a per-voice Morph step automation override. The morph engine uses this value instead of the retained per-voice amount while active. LFO modulates around the override value (Session 070). | Sequencer Scene automation drain |
| `presetMorph_clearAllStepAutomationOverrides(scene)` | Clear all per-voice Morph step overrides and restore retained amounts. Called on transport restart (Session 070). | Sequencer Scene automation restore |
| `presetMorph_clearStepAutomationOverride(slot)` | Clear one voice Morph step override. Rule A: called when a non-automation write changes the voice Morph retained value (`preset_morphVoiceScene()`, `preset_morphScene()`). (S076 P1) | presetManager Morph setters |
| `presetMorph_getEffectiveVoiceAmount(scene, slot)` | Return the step override when active, else the retained per-voice Morph amount. Single query point for the morph decimation engine (Session 070). Also used by menu.c for Scene superpage live display (Session 071). | presetMorphEngine internals, morph decimation, Menu |
| `preset_setAudioOutStepOverride(slot, route)` / `preset_clearAllAudioOutStepOverrides(scene)` / `preset_getEffectiveAudioOut(scene, slot)` | Per-voice audio-out step automation overlay. Route clamped to `MIXER_ROUTING_DAC2_R`. Cleared by `preset_init()` and transport restore (Session 071). | Sequencer Scene automation drain, Menu superpage |
| `preset_setFxSendStepOverride(slot, amount)` / `preset_clearAllFxSendStepOverrides()` / `preset_getEffectiveFxSendAmount(scene, slot)` | Per-voice FX-send step automation overlay (clamped to 127). The effective (audible) send is the override, else the Normal/Morph endpoints interpolated by `presetMorph_getResolvedVoiceAmount()` (equal endpoints return at once; S075 F2). The mixer reads it each block and ramps the live send. | Sequencer Scene automation drain, mixer |
| `preset_getFxSendDisplayAmount(scene, slot)` / `preset_setVoiceFxSendAmount(scene, slot, amount)` / `preset_setVoiceFxSendMorph(scene, slot, amount)` | S075 F2: the Normal-view cell value (override else Normal endpoint); retain the Normal or Morph endpoint through SceneData (no runtime push). | Menu VOICE mix cell, CopyClear `clear send` |
| `preset_setBusCompSetting(scene, field, value)` / `preset_syncBusCompMirrors()` | S074: clamp and commit one bus compressor field of one Scene through SceneData (the VOICE edit-mask fan-out calls it per Scene), and copy the active Scene's four values into the `PAR_BUS_COMP_*` page mirrors (also on every Scene apply, `preset_applySceneSettings()`). No runtime push: BusCompressor reads SceneData per block. | Menu bus compressor page, Scene apply |
| `preset_setVoiceFaderSetting(scene, slot, mode)` | Clamp to 0..`SCENE_FADER_SETTING_MAX` (3 since S074, `xfd`) and store through SceneData. | Menu VOICE mix page |
| `preset_resetSlotMorphToNormal(scene, slot)` | Copy all Morphable Normal descriptor bytes into the Morph image for one slot, plus the correlated Scene morph endpoints (FX send morph, Kit slot-6 decay morph). (S076 P3) | CopyClear clear morph, copy morph |
| `preset_copySlotNormalToMorph(scene, slot)` | Copy all Normal descriptor bytes into the Morph image for one slot. (S076 P3) | CopyClear copy morph |

## Core/DSPAudio/mixer

Affiliate modules: InstrumentManager, EffectsManager, voice engines, SceneData,
Preset, and the live FX bus.

Purpose: renders the foreground audio block. Voice engines supply pre-volume
samples; the mixer applies the relocated voice volume after decimation. Each
block it calls `effects_service()`, taps each decimated voice before volume,
applies the PRE/POST/FX/XFD fader mode, sums the send into the mono/stereo FX
bus, calls `effects_process()`, and returns the processed block through common
level/pan and jack-resolved routing. While the Effect is `off` it zeroes the
return ramp origin (S074). Its last stage is the master bus compressor
(`busComp_processBlock(output2, output, scene)`). See
`EFFECTS_BUS_REFERENCE.md` §3 and `EFFECTS_MIXER_DSP_REFERENCE.md` §3, §5A.

| API / data | Use | Usual callers / clients |
|---|---|---|
| `mixer_decimateBlock()` | Reduce each voice block before final voice-volume application and the pre-volume FX send tap. | mixer render path |
| `mixer_addVoiceInt16ToOutputAndFx()` (static, S073) | One pass per slot for the dry output and the FX send when the Effect is active and the send ramp is non-zero; replaces the removed `mixer_addVoiceToFxBus()`. Otherwise `mixer_addVoiceInt16ToOutput()` does the dry path alone. | mixer render path |
| `effects_service()` | Resolve the active Effect's retained parameters between render blocks. | mixer block service |
| `effects_process()` | Process the current mono/stereo normalized FX bus block in place. | mixer |
| `MIXER_FADER_PRE/POST/FX/XFD` | Define whether the Scene fader scales both taps, only the dry mix, only the FX send, or (S074 `xfd`, 3) crossfades dry (fader) against send (the mirrored fader from `adc_sliderGainMirrored()` in `adcPots.c`). | SceneData/Preset, Menu, mixer |
| `mixer_send_last_gain[]` / `mixer_fx_return_last_gain[]` | Retain block-end send and return gains so automation and common return changes ramp without clicks. | mixer render path |
| `mixer_faderGains()` (static) | Reads `preset_getEffectiveFxSendAmount()` per voice per block (S075 F2: Normal/Morph send interpolation). | mixer render path |
| Effect return pan (internal) | Stereo output: balance law centred on stored 63 (`gL = p ≤ 63 ? 1 : (127−p)/64`, `gR = p ≥ 63 ? 1 : p/63`, S075 F2); mono output: `squareRootLut` constant power. `mixer_decimation_rate[]` has six entries (the global `srt` multiplier is gone, S075). | mixer render path |

## Core/DSPAudio/BusCompressor (Session 074)

Affiliate modules: mixer, MidiVoiceControl, SceneData, Preset, Menu.

Purpose: the Scene-owned master bus compressor. It is not an Effect type (no
registry row, send, lanes or arena). It reads the active Scene's four
settings each block; nothing pushes settings into it. It owns its 32 B DTCM
state (`busComp`). Full model: `EFFECTS_MIXER_DSP_REFERENCE.md` §5A.

| API | Use | Usual callers / clients |
|---|---|---|
| `busComp_processBlock(st1, st2, scene_index)` | Process one 32-frame block of the selected pair in place (St1 = the mixer's `output2`/DAC1, St2 = `output`/DAC2). One-block fades on every target change. With `cmp` off and no fade pending it clears any pending sidechain weight and returns: no DSP work. | `mixer_calcNextSampleBlock()`, last stage, once per block |
| `busComp_sidechainTrigger(track, velocity)` | Record the largest `(velocity/127)³` since the last block when `track` matches the active Scene's `csc` (track index 6 counts as voice 6, BC11). Velocity 0 and `csc off` are ignored. Foreground only. | `voiceControl_triggerNow()` |

Retained settings live in SceneData (`bus_comp[4]`); the Menu edits them
only through `preset_setBusCompSetting()`. Nothing else may keep a pointer
into `busComp`.

## Core/Bank/Scene/Preset/ParameterArray

Affiliate modules: Menu, Preset manager, MidiControl, modulationNode, DSP voice
modules, mixer, filesystem, PatternData.

Purpose: owns the legacy numeric parameter id map and runtime pointer map from
flat sound parameter ids to DSP engine fields. After Session 032, instrument
file keys and dynamic VOICE pages are descriptor-backed through
InstrumentManager/SceneData instead of this table. `ParameterArray` remains
important for older static sound parameters. Session 033 added
descriptor-aware LFO/velocity adapters. (The legacy AutomationNode path and the
S083-retired MIDI CC map no longer use it.)

| API / Data | Use | Usual callers / clients |
|---|---|---|
| `enum ParamEnums` / `PAR_*` / `END_OF_SOUND_PARAMETERS` / `NUM_PARAMS` | Legacy/static parameter id space for sound, menu, pattern, and globals. | Menu, filesystem, Preset, PatternData |
| `TYPE_UINT8`, `TYPE_FLT`, `TYPE_SPECIAL_F`, `TYPE_UINT32`, `TYPE_SPECIAL_P`, `TYPE_SPECIAL_FILTER_F` | Type tags for runtime parameter pointer entries. | ParameterArray, modulationNode |
| `ptrValue` | Float/integer value carrier for typed writes. | modulationNode, ParameterArray |
| `Parameter` / `parameterArray[]` | Map legacy sound parameter id to target DSP field and value type. | ParameterArray, modulationNode |
| `paramArray_setParameter(idx, newValue)` | Write one typed value into the mapped DSP field when the id/pointer are valid. | modulationNode restore/apply paths |
| `parameterArray_init()` | Fill the sound-parameter pointer/type map. | `main.c` boot |
| S080 ids | `PAR_PERF_BTN_LT/NOTE_LT/BTN_RT/NOTE_RT` (overlay Globals), `PAR_SCENE_CHANGE_DIV` (scd 0..9), `PAR_SCENE_QUEUE_DIV` (qcd 0..17), `PAR_SCENE_QUEUE_ACTION` (qac 0..8), `PAR_CHAIN_COND1/NEXT1/COND2/NEXT2` (chain editor mirrors), `PAR_TRACK_LOOPABLE` (STEP `lop` mirror) — all appended after the Globals, inside the fixed 384 allocation (static assert). The duplicate `PAR_SCENE_PC_RESET` added in S080 P1 was removed; pcr is `PAR_BAR_RESET_MODE`. | Menu, filesystem, buttonHandler |
| `extern parameter_values[]` | Legacy/static parameter byte store declaration. Descriptor-backed instrument values live in Scene storage. | Defined in Menu today |

## Core/DSP/Effects/EffectTypes

`EffectTypes.h` is the data-only retained Effect contract. It defines the
`effect_record_t` (420 bytes), sequencer step shape, common defaults, and the
block-7 Effect target helpers. It owns no runtime allocation, registry, audio
processing, file I/O, or Scene storage; SceneData embeds one record per Scene
and owns all mutation.

The canonical Effect target IDs are 448..511 (64 IDs), with local index 63
reserved as the automation-off alias. Scene target IDs remain in block 6
(384..447), so the two namespaces cannot overlap.

## Core/DSP/Effects/EffectsManager

Affiliate modules: SceneData, BankData, FxBuffer, Preset, AutoSave, mixer,
Sequencer, InstrumentManager, menuEffects/Menu, and the per-type
implementations.

Purpose: owns the immutable `off`/`flt`/`cbt` registry (CrumpBit added in
S074) and the active Effect runtime.
It resolves retained normal/Morph endpoint images, switches type instances
through FxBuffer's foreground handoff, publishes common return settings, and
calls the active type's parameter writer once per changed effective value. It
rescans the active descriptor table once per render block (no dirty
notifications). It does not own retained Scene bytes, filesystem parsing, UI,
or the FX audio bus.

| API / data | Use | Usual callers / clients |
|---|---|---|
| `effects_registryEntry()` / `effects_registryCount()` / `effects_typeToken()` / `effects_typeFromToken()` | Append-only type registry and persisted tokens; unknown → rejected (runtime falls back to `off`). | AutoSave, storageTypes, menuEffects |
| `effects_descriptor()` / `effects_descriptorByKey()` / `effects_paramMorphable()` / `effects_paramAutomatable()` / `effects_paramModulatable()` | Descriptor lookup and capability rules (local 63, WIDE8 + `expand7`, modulation domain). | storage, Menu, automation/LFO |
| `effects_paramMorphable()` / `effects_getParameter()` | Query Morph capability and resolve the visible normal/Morph endpoint for the Effect page. | `menuEffects` |
| `effects_recordDefaultsForType()` / `effects_laneFileKey()` / `effects_laneByFileKey()` | Unowned default record and lane ↔ `.fx` key mapping. | `.fx` parser, AutoSave reader, boot loader |
| `effects_getParameter()` | Read a normal/Morph endpoint. | `menuEffects`, step-automation current value |
| `effects_setParameter()` / `effects_setSeqRunMode()` / `effects_setSeqLength()` / `effects_setSeqStepScale()` / `effects_setMorphAmount()` / `effects_setSeqLaneLock()` / `effects_changeType()` | Retained edit API. Each fans out through the origin Scene's own VOICE edit-mask entry (S075; the active Scene uses the self-repairing getter), with a same-type guard for rows/locks/sequence; `mrp` and `typ` reach all masked Scenes; `changeType` then re-validates masks. Returns nonzero if any reached Scene changed. | `menuEffects`, type UI hooks, DEV hook |
| `effects_pasteRecord(dst, src)` / `effects_resetRecord(dst)` / `effects_pasteSeqStep(dst, step, src)` / `effects_clearSeqLanes(scene, step_mask, lane_mask)` | Copy/clear record and FX-sequence operations fanned out through the destination Scene's own edit-mask entry (same type as the destination's current type); a record paste may change the type. Lock removal (A15) is now possible. (S075) | CopyClear |
| `effects_seqActiveStep()` / `effects_seqSelectedStep()` / `effects_seqSelect()` / `effects_seqSerial()` / `effects_laneOfParam()` / `effects_getLaneLock()` | FX position/selection and lock inspection. | `menuEffects` |
| `effects_init()` | Initialize manager state and install the FxBuffer share-change callback after `fxbuf_init()`. | `main.c` |
| `effects_rebindSourceScene(scene)` | S082 P2: change only `effects_state.scene_index`, the Scene whose Effect record `effects_service()` reads every block (no runtime switch, handoff, force, sequence, automation or LFO reset); refused when the Scene has no record or its type differs from the running type (the caller then falls back to `effects_activateScene()`). Called at the Scene 17 switch so the sounding Effect keeps reading an identical copy while the home Scene's record is overwritten by the load. **Rule:** any module that caches a Scene index must follow playback onto Scene 17 (the S082 P2-1 lesson). | Preset background tick phase 3 |
| Fan-out masks (S082) | `effects_fanoutMask()` returns 32-bit masks (Scene 17 = bit 16, itself only) and masks out `bank_loadUnavailableMask()` during a background load. | EffectsManager edit/paste API |
| `effects_activateScene(scene)` | Select a Scene's retained Effect type; different types switch immediately through the FxBuffer handoff, while same-type activation preserves tails. | Preset Scene/Bank apply paths |
| `effects_targetValid()` / `effects_targetDescriptor()` / `effects_stepTarget()` | Validate/describe/walk block-7 targets for a Scene's type and use. | InstrumentManager, Menu |
| `effects_automationReset()` / `effects_applyAutomation()` / `effects_setMorphAutomation()` | Pattern overlay lifecycle and the `fxm` override (third restore rule). S081 R13: `effects_applyAutomation()` sets a held, last-writer overlay and clears that parameter's `menuHold`; `effects_setMorphAutomation()` clears `menuHoldMorph`; `effects_automationReset()` clears every overlay, `menuHold`, `menuHoldMorph`, `fxm` and the live-erase per-step state. `effects_automationStepBegin()`/`StepFlush()`/`PublishOwners()` and the owner state were removed (`effects_automation_t` 184 → 120 B). | Sequencer drain, `seq_applySceneAutomation()` |
| R13 menu holds and lock layering (S081, internal) | `effects_setParameter()` / `effects_setMorphAmount()` (origin = active Scene, row in the runtime type) clear the overlay and set `menuHold`/`menuHoldMorph`; `effects_lockClearLayers()` at an FX lock step start (`effects_seqConsume()` STEP, `effects_seqSelect()` in `sel`) clears the overlay and `menuHold` of each locked lane's parameter; `effects_service()` resolves menuHold > lock (its step only; the held Morph lane is gone) > overlay > menu (`EFFECTS_BUS_REFERENCE.md` §6). | Menu, Sequencer drain |
| `effects_setLiveEraseLanes(laneMask)` / `effects_takeErasedLocks(*step, *laneMask)` | S081 live FX-sequence erase: the 16-bit lane mask whose locks are excluded at each FX step start; the per-step record `effects_erasedLanes[16]` is taken one step at a time (returns 0 when empty) and cleared with `effects_clearSeqLanes()`. | LiveRecord |
| `effects_storeValue7(scene, local, value)` | S081: the inverse of a WIDE8 row's `expand7`, so a live-recorded Effect value is stored 7-bit. | LiveRecord value resolver |
| `effects_setMorphAmountFromGlobal(scene, amount)` | S083 follow-up: global Morph (`mrp`, CC1 on G) sets the Effect Morph of ONE Scene with `fxm` menu-edit semantics: stores the amount, clears the Pattern `fxm` override (S076 Rule A) and, on the active Scene, sets `menuHoldMorph` (holds over an FX Morph lock until the next Morph automation). No fan-out (the caller runs per edit-mask Scene); never records (G-1). | `preset_morphScene()` |
| `effects_clearMorphAutomationOverride()` | Clear the Effect Morph automation override. Rule A: called when a non-automation write changes `fxm` (`effects_setMorphAmountScene()`). (S076 P1) | EffectsManager Morph setter |
| `effects_resetMorphToNormal(scene)` | Equalise all morphable Effect endpoints of a Scene to their Normal values. (S076 P3) | CopyClear Scene morph reset |
| `effects_resetMorphToNormalSingle(scene)` | Same as above, for fan-out use (single Scene at a time). (S076 P3) | CopyClear fx morph reset |
| `effects_setLfoContribution()` / `effects_clearLfoSource()` | Base-independent LFO entries per source/pair. | InstrumentManager |
| `effects_init()` / `effects_activateScene()` | Boot init (share callback); Scene activation (type switch through the handoff, clears FX position, overlays, `menuHold` and `fxm`; the held Morph latch no longer exists, S081). Since S074 a same-type activation refreshes the handoff (`effects_exportHandoff()`, internal) without `init`. | `main.c`, Preset |
| `effect_ui_hooks_t` / `effect_select_layout_t` (registry contract, extended S074) | Per-type page hooks (`select`/`track`/`bar`, `render_leds`, `paint_row0`, `format_value3`, `flags` with `EFFECT_UI_FLAG_OWNS_SELECT_LEDS`; returns `EFFECT_UI_HANDLED`/`EFFECT_UI_SHOW_HOME`) and layouts (`custom_row0`, home screen, the `EFFECT_LAYOUT_CELL_MORPH` sentinel). Contract in `EFFECTS_BUS_REFERENCE.md` §8.4. | menuEffects, buttonHandler, type Parameters files |
| `effects_service()` / `effects_process()` / `effects_commonRuntime()` / `effects_activeType()` / `effects_activeIoFlags()` | Per-block resolution, process and return settings. | mixer |
| `effects_registryCheckResult()` | Diagnostic-only immutable registry self-check result. | diagnostic boot screen |

All calls are foreground-only and must occur between render blocks; no Effect
manager API is ISR-safe. `StereoFilter` and `CrumpBit` own their descriptors,
runtime ops and (CrumpBit) page hooks, while `EffectsManager` owns the runtime
union (76 B) and dispatch boundary. CrumpBit's DSP helpers
`crumpBit_rateSamples()` and `crumpBit_syncDivision()` are shared by its DSP
and its Sync label so the page shows exactly the division the DSP plays.

## Core/DSP/Effects/FxBuffer

Affiliate modules: the linker script/startup, `main.c`, future
`EffectsManager`, Instrument buffer voices, and the Phase 5 Effect types.

Purpose: sole owner of the linker-defined NOLOAD `.dtcm_fxbuf` span. FxBuffer
does not clear or otherwise touch audio bytes. It allocates one contiguous
bottom-of-arena Effect share and up to twelve fixed 4,416-byte 16-bit mono
voice units from the top, with at most two units per one of the six Instrument
slots. All calls are foreground-only and occur between audio render blocks.

| API / data | Use | Usual callers / clients |
|---|---|---|
| `fxbuf_init()` / `fxbuf_arenaBytes()` | Capture `_sfxbuf.._efxbuf`, reset ownership, publish the boot handoff, and report link-time arena size. The arena is neither copied, zeroed, nor cleared. | `main.c` `dsp_init()`, diagnostics |
| `fxbuf_effectShare()` | Return the current bottom-contiguous Effect share. | EffectsManager, Effect types, diagnostics |
| `fxbuf_voiceAcquire()` / `fxbuf_voiceRelease()` | Claim/release 1..2 units for a slot; share changes synchronously notify the registered callback. Release never clears contents. | Phase 7 voice buffer owners, diagnostic boot hook |
| `fxbuf_voiceUnit()` / `fxbuf_voiceUnitCount()` / `fxbuf_unitsInUse()` | Access an owned aligned unit or inspect allocation counts. | Phase 7 voices, diagnostics |
| `fxbuf_handoffBeginExit()` / `fxbuf_handoffSetVoiceUnit()` / `fxbuf_handoff()` | Exchange arena-relative positions, rates, ownership, and written-content flags between exiting and entering owners. The entering owner adopts or disposes of persistent contents. A same-type Scene switch does not currently refresh the record (`EFFECTS_BUS_REFERENCE.md` §13). | EffectsManager, future voice types |
| `fxbuf_handoff_t` / `fx_share_t` | Retained SRAM1 control records; audio storage remains in `.dtcm_fxbuf`. | FxBuffer API clients |

The linker ASSERT keeps the arena at or above the approved 120 KiB minimum,
and `DEV_MODE_DIAGNOSTIC` can run the allocation self-test without touching
the arena. `DEV_FXBUF_FORCE_VOICE_UNITS` is ignored in production builds.

## Core/DSP/Instruments/InstrumentManager

Affiliate modules: SceneData, Preset/Morph, Menu, filesystem/storageTypes,
MidiVoiceControl, mixer, modulationNode, LFO/velocity paths, and all four
instrument engines.

Purpose: owns the immutable instrument registry and the active Scene
slot-to-runtime bridge. Descriptor tables remain beside each instrument type;
InstrumentManager supplies registry lookup, assignment policy, target
validation, Choke sibling lookup, current type dispatch, and runtime lifecycle
operations that callers must not duplicate.

Resident Instrument parameter values use the byte
`instrument_param_value_t` domain. Target selectors use the byte
`instrument_target_token_t` domain with `0xff` as off. Wide descriptor and
Scene IDs are resolved only for validation, display, or runtime dispatch; they
are not the stored selector representation. Velocity target selection is
self-scoped plus its source-voice Morph token; LFO selection supports self,
voices, the Scene namespace, and the Effect namespace (`fx`, value 8).

| API | Use | Usual callers / clients |
|---|---|---|
| `instrumentManager_registryEntry()` / `registryCount()` / `registryEntryAt()` / `typeDisplayLabel()` / `typeFlags()` | Immutable type metadata: token, extension, label, Basic/Advanced/Choke flags, descriptor and menu tables. | Menu, storage/filesystem, converter-aligned tooling |
| `instrumentManager_typeSelectableForSceneSlot(scene, slot, type)` | Enforce any-Basic/two-Advanced replacement policy. | Instrument Load type browser |
| `instrumentManager_chokeDescriptorIndexForBase(type, base, out)` | Resolve `<base>_choke` sibling within one type. | Menu VOICE7 resolver |
| `instrumentManager_runtimeInstance()` / trigger/filter/async/sync/pan/volume/LFO dispatch family | Resolve the runtime object for the current active Scene slot type. Engines render pre-volume; `instrumentManager_runtimeVolume()` supplies the channel volume that the mixer applies after decimation (Session 072 step 2). | mixer, MIDI/Sequencer trigger paths, Preset |
| `instrumentManager_clearAllRuntimeModulationTargets()` | Restore/clear both LFO pairs and velocity target for every outgoing current source before a slot type changes. | Preset staged Instrument commit |
| `instrumentManager_resetRuntimeSlot(slot)` | Initialize only the incoming committed slot/type runtime object. | Preset staged Instrument commit |
| `instrumentManager_writeRuntime()` / target validation/stepping helpers | Apply descriptor/supplemental bindings and validate canonical targets against current slot types. Special conversions are chosen by the row's flash tag (`runtime.special`, `IM_SPECIAL_*`, S073) in `instrumentManager_writeSpecialRuntime()`, not by the key string. `IM_SPECIAL_LFO_OFFSET` (19, S076 P2) scales the 0–127 byte to a full 32-bit phase: `(uint64_t)v * 0xFFFFFFFF / 127`. | Preset, Menu, modulation paths |
| `captureLfoPhases()` / `restoreLfoPhaseIfNeeded(voice)` | Scene-change LFO phase handoff for retrigger `scn` (7). Capture runs in `preset_startDrumsetApply()` before teardown; restore runs in `preset_resetAndApplyKitVoiceImage()` after each voice is rebuilt. Static 28 B BSS `lfo_scene_handoff`. (S076 P2) | Preset Scene/Bank apply paths |
| `instrumentManager_specialTagSelfCheck()` (diagnostic builds) | Compare every row's tag with the old key rules; shown as the `s` digit on the `FxBf` boot screen. Host twin: `tools/dsp_test/check_special_tags.py`. | `main.c` diagnostic screen |
| `instrumentManager_updateLfoAdapters(source_slot, pair, lfo, polarity, amount)` | Update InstrumentManager-owned LFO destinations: descriptor-domain adapters, slot decimation, and Scene targets. Descriptor adapters shape in parameter space and then call the normal runtime writer. For voice-morph targets, Session 071 changed encoding to direction+depth without reading the morph base; Effect parameter and `fxm` destinations are encoded to direction/depth by the shared `instrumentManager_lfoDirectionDepth()` and stored in EffectsManager. | `lfo.c` |
| `INSTRUMENT_TARGET_VOICE_EFFECT` / `INSTRUMENT_TARGET_VOICE_NAMESPACE_LAST` | LFO namespace byte 8 (`fx`); the upper clamp for every picker/parser. | Menu, storageTypes |
| `instrumentManager_targetValid()` (block 7) | AUTOMATION validation of Effect IDs via `effects_targetValid()`; MODULATION stays voice-only. | PatternData writer, Menu |
| `INSTRUMENT_BIND_LFO_TARGET_VOICE` / `_VOICE_2` handler | Session 071 fix: changed from store-only validation to full target reinstall via `instrumentManager_installLfoModulationTarget()`. Reads sibling param token via `instrumentManager_descriptorIndexForBinding()` and clears stale morph/decimation/Scene contributions through the existing restore path. | `instrumentManager_writeRuntimeInternal()` |

Slot-6 track-7 generated decay step automation override (Session 070):
`slot6_track7_decay_step_active` and `slot6_track7_decay_step_value` in
InstrumentManager.c provide a runtime-only override for the generated
`slot6_track7_amp_envelope_decay` Scene parameter. Trigger cascade priority:
step override > LFO contribution > retained Scene value. The runtime writer
checks step-active before applying LFO or retained values.

Transaction rule: clear all current owners before changing Scene slot type;
commit the staged slot; reset the incoming runtime; rebuild retained runtime
images; then normalize and rebind all sources. Clearing after the type swap
cannot recover a dynamic-pool source owned by the outgoing identity.

## Core/Bank/Scene/SceneModTargets

Affiliate modules: Menu, InstrumentManager, Preset/Morph, EffectsManager (`fxm`).

Purpose: owns the canonical ID/name/range list for Scene-level sound
parameters that are modulation targets but are not parameters of a swappable
instrument in a voice slot. This keeps per-instrument descriptor tables dynamic:
voice-local targets come from the currently installed instrument descriptor
table, while non-voice sound targets come from this Scene namespace.

Current target order:

- `1vm`, `2vm`, `3vm`, `4vm`, `5vm`, `6vm` — per-voice Morph
- Scene Decimation `srt`
- Audio Out `1ou`..`6ou` (IDs 392–397, max 5) — Session 070
- FX Send `1fx`..`6fx` (IDs 398–403, max 127, mixer-applied per block) —
  Session 072 Step 5
- Effect Morph `fxm` (ID 404; LFO + automation, no velocity) — EffectsManager
  Morph-base overlay (Session 072 Step 9)
- Track length (IDs 405–411), scale (412–418) and shuffle (419–425), one per
  track (`voice_slot` = track 0..6), `SCENE_MOD_TARGET_KIND_TRACK_*`,
  automation only — the sequencer's track timing overlay (Session 078)

Scene Decimation deliberately appears after the six Morph targets so the
velocity target list does not place it directly beside a voice-local
`instrument_decimation` row, which also uses short label `srt`.

Session 070 additions: Audio Out and FX Send targets are Scene-level
parameters with `SCENE_MOD_TARGET_USE_AUTOMATION` flag, reachable from step
automation but not yet from velocity/LFO modulation pickers. FX Send apply
is consumed by the live mixer on the next block. Step automation uses
runtime-only overlays; see `070_SESSION_HANDOFF_LOG.md` §10 for the overlay
architecture.
`fxm` carries `USE_LFO | USE_AUTOMATION`; its Pattern value expands like voice
Morph and follows the Scene reset rule.
Scene block 6 is limited to IDs 384..447; Effect parameters use block 7
IDs 448..511.

| API | Use | Usual callers / clients |
|---|---|---|
| `sceneModTarget_count()` | Number of Scene target descriptors. | Menu/InstrumentManager list traversal |
| `sceneModTarget_idAt(index)` | Convert list index to canonical Scene target ID. | Menu stepping/display |
| `sceneModTarget_valid(id, use_mask)` | Validate a target for velocity and/or LFO use. | Menu, InstrumentManager |
| `sceneModTarget_get(id)` | Read descriptor metadata: kind, voice slot, range, labels, flags. | Menu, InstrumentManager |
| `sceneModTarget_first(use_mask)` / `sceneModTarget_step(current, dir, use_mask)` | Non-looping target-list traversal with skipped invalid entries. | Menu target pickers |

## Core/MIDI (Session 083 rebuild)

Affiliate modules: USB, Sequencer, Menu, Preset, MidiVoiceControl,
buttonHandler, LiveRecord, EffectsManager, InstrumentManager, SceneData.

Purpose: the single-role MIDI modules built in S083. The old `MidiParser`,
`Uart`, `MidiMessages`, `FIFO` and `MidiRealtime` files are gone; their
duties are split below. A received message is offered to its consumers first
and forwarded per MRT only when no consumer acted (consume-then-route).

| Module | API | Use | Context |
|---|---|---|---|
| `MidiDefs.h` | `MidiMsg` (6 B: status/data1/data2/bits/stamp), status and CC constants, `midiDefs_ccReserved()`, `midiDefs_routeMask()`, `MIDI_ORIGIN_*` | Shared vocabulary; no legacy CC/CC2 map | — |
| `MidiBuffer.c/h` | `midiBuffer_*` | DIN TX lanes and stamped RX buffer | ISR / foreground |
| `MidiDin.c/h` | `midiDin_init()`, `midiDin_sendBytes()`, `midiDin_txFree()`, `midiDin_rxPop()` | USART3 driver; realtime bytes diverted to the transport ring | USART3 ISR |
| `MidiIn.c/h` | `midiIn_service()`, `midiIn_setGlobalChannel/Filter/Routing()`, `midiIn_rxAllows()`, `midiIn_txAllows()`, `midiIn_routeMask()` | Framing (incl. SPP/Song Select), RXF gate, S082 Bank-load drop filter, dispatcher, MRT forwarding | Foreground |
| `MidiTransport.c/h` | `midiTransport_processRealtime()` (TIM3), `midiTransport_handleSystemCommon()`, `midiTransport_checkMtc()` | Realtime ring and `snc` gate, Start/Continue/Stop, SPP, MTC | TIM3; foreground for SPP/MTC |
| `MidiNoteMap.c/h` | `midiNoteMap_load/resolve/inverse/effectiveChannel/tracksOnChannel()` | N1–N5 note → track/pitch map and Q4 TX inverse; stateless | Foreground (inverse also TIM3) |
| `MidiNotes.c/h` | `midiNotes_handle()`, `midiNotes_setNoteAction()` | Single trigger, overlay dispatch, CC2 → BAR overlay (no own state), LiveRecord with arrival age | Foreground |
| `MidiControl.c/h` | `midiControl_handleCc()`, `midiControl_writeTarget()`, `midiControl_targetValue7/Channel()`, `midiControl_targetOnChannel()` | NRPN contexts, CC1, value clamp/Morph translation, menu-edit commit | Foreground |
| `MidiLearn.c/h` | `midiLearn_arm/cancel/service()`, `midiLearn_captureCc/applyCc()`, `midiLearn_ccForTarget()`, `midiLearn_formatFileLine/parseFileValue()` | 128-entry global Learn table and `midilrn.cfg` lines (each Learn calls `filesystem_requestMidiLearnWrite()`) | Foreground |
| `MidiProgram.c/h` | `midiProgram_programChange()`, `midiProgram_bankChange()` | PC on G/per-track; CC0 background Bank request | Foreground |
| `MidiOut.c/h` | `midiOut_sendMsg()`, `midiOut_forward()`, `midiOut_trackTrigger/NoteOn/NoteOff()`, `midiOut_parameterChanged()`, `midiOut_trackProgramChange()`, `midiOut_service()` | All transmission, USB CIN framing, per-track note-off, coalesced parameter TX, no echoes | Foreground and TIM3 |
| `MidiPitch.h` | `MIDI_DEFAULT_TRIGGER_NOTE`, `MidiNoteFrequencies[]`, `midiPitch_detune()` | Pitch constants and detune | — |

Shared runtime state:

- `midiIn_config`: global channel (0 = off), packed RX/TX filter, MRT mask.
- `midiControl_nrpn[2][8]`: one NRPN context per port/channel.
- `midiLearn_table[128]`: global type-tagged CC rows.
- `midiOut_trackNote/Channel[7]`: per-track sounding note and its channel.
- `midiOut_nrpnKey[2]`, `midiOut_pending`, `midiOut_lastSendMs`: parameter TX.

Data flow in one line each (full diagrams, every module's internals, the
integration points, concurrency rules and extension recipes are in
`MIDI_SPEC.md` Part II, the code reference):

- **Receive:** USART3/OTG ISRs stamp each message (TIM2 µs) → realtime to the
  `MidiTransport` ring (TIM3 handler, `snc` gate), everything else to the DIN
  buffer / USB ring → `midiIn_service()` frames and dispatches → consumers
  (`MidiNotes`, `MidiControl` → `MidiLearn`, `MidiProgram`, `MidiTransport`)
  → not consumed: `midiOut_forward()` per MRT.
- **Write:** every received parameter value goes through
  `midiControl_writeTarget()` → `menu_commitTargetEdit()` (a MIDI write is a
  menu edit: R12/R13, fan-out, recording, repaint).
- **Transmit:** `seq_triggerVoice()` → `midiOut_trackTrigger()` (inverse note
  map); panel edits → `midiOut_parameterChanged()` → `midiOut_service()`
  (learned CC or NRPN); PC via `seq_sendProgChg()` /
  `midiOut_trackProgramChange()`; CC0 after a Menu Bank load.

The S082 Bank-load drop filter moved verbatim into `MidiIn.c`
(`midiIn_acceptedDuringBankLoad()`): while a load runs, only realtime,
note on/off and CC except CC 0/32 act; everything else is forwarded. A
message dropped by the filter is not consumed, so it still follows MRT.

## Core/MIDI/MidiVoiceControl

Affiliate modules: Sequencer, MidiNotes, MidiOut, audio render boundary.

Purpose: owns voice note-on/off dispatch and pending trigger drain at the audio
render boundary. Session 028 removed obsolete front-panel dependency.

| API | Use | Usual callers / clients |
|---|---|---|
| `voiceControl_noteOn(voice, note, vel)` | Trigger/queue voice-on behavior; always requests a step reset for the voice (live triggers reset at each press, S081). | Sequencer, MidiNotes, preview, overlay |
| `voiceControl_noteOnNoReset(voice, note, vel)` | S081: queue a trigger that does **not** reset the voice's held automation (roll retriggers 2+). | Sequencer (`seq_triggerVoice(…, 0)`) |
| `voiceControl_resetOnly(voice)` | S081: request a reset with no trigger (a roll carried into the next step). | Sequencer roll carry |
| `voiceControl_noteOff(voice)` | Stop one voice or all voices with `0xff`. | Sequencer, MidiNotes |
| `voiceControl_processPending()` | Drain pending triggers at the audio render boundary. S081: `voiceControl_popPending()` returns each entry's voice reset bit, tested and cleared under the queue's PRIMASK from the per-voice mask `voiceControl_pendingReset` (`u8`, bits 0..6; never a flag in the entry, because `voiceControl_noteOff()` drops every queued entry of the voice). After the ring is empty the leftover mask is taken under PRIMASK and each voice is restored (reset-only renewals, resets whose entry was dropped). Runs before `seq_drainPendingAutomation()`, so a step's automation always lands after its reset. | `audio_check_and_render()` |
| `voiceControl_triggerNow(voice, note, vel, reset)` (the trigger funnel) | Apply one trigger in the foreground in the order `preset_applyDeferredSceneSlotForTrigger()` → `seq_restoreAutomatedParameters()` (only if `reset`, S081) → `instrumentManager_triggerTrack()`, sidechain, LED. The restore must not move ahead of the deferred Scene-slot apply. Since S074 it also calls `busComp_sidechainTrigger(track, velocity)`, so every trigger source (sequencer, rolls, MIDI, previews) can duck the bus compressor. | `voiceControl_processPending()`, previews |
| `voiceControl_isVoicePlaying(voice)` | Query voice state. | clients/future UI |

## Core/Bank/Scene/Autosave and AutosaveTrace

Affiliate modules: BankData, SceneData, Preset, filesystem, config.

Purpose: `Autosave.c/.h` owns the hidden-record wire contract, live-byte
projection, one canonical dirty mask with maintained O(1) dirty count
(`autosave_dirty_count`, `uint16_t`), a separate 16-bit non-semantic Pattern
dirty mask (`autosave_nonsemantic_pattern_dirty_mask`), atomic dirty
operations, typed scalar and whole-region marker vocabulary, CRC helpers, and
the boot-only inverse payload-to-resident projection. It owns no file handle
or scheduler. `AutosaveTrace.c/.h` is a logging-only observer with no
filesystem ownership. Stage `c` is the S075 copy/clear lifecycle/risk witness;
its event and value vocabulary is authoritative in `AutosaveTrace.h` and
`DEV_MODES.md`. Exact format and other trace-field semantics remain
authoritative in `AUTOSAVE.md` and `DEV_MODES.md` rather than being duplicated
here.

| API family | Interchange rule | Usual callers / clients |
| --- | --- | --- |
| `autosave_mark*Dirty(...)` | Retained owners store/commit first, then mark typed coordinates. Producers perform SRAM-only work and never calculate wire offsets. | BankData, SceneData, Preset, successful whole-object commits |
| Whole-Scene marks (S082 P2) | `autosave_markSceneWithoutPatternDirty()` (and `…WithPatternDirty()`) OR one contiguous mask range `[Scene base + 8, base + 1,920)` in one short PRIMASK section (`autosave_maskPayloadRangeDirty()`, exact fresh-bit count) and emit one `L` witness, instead of per-byte marks and `D` records. Exact because drain phase 56 keeps a patch only for bytes with a live getter. `autosave_markKitDirty()` keeps its per-byte form. | Bank/Scene Load completion, whole-Scene paste, Merge re-mark |
| `autosave_maskHasDirty()` / atomic take and merge helpers | One canonical mask coordinates foreground capture and interrupt-reachable producers; `maskHasDirty()` is O(1) via maintained dirty count. Filesystem may consume through the documented API but never owns a second mask. | filesystem AutoSave scheduler/writer |
| `autosave_markNonSemanticPatternDirty()` / `autosave_nonSemanticPatternDirtyMask()` / `autosave_clearNonSemanticPatternDirty()` | Separate 16-bit mask for physical pool relocation dirtiness (no semantic AutoSave implications). | PatternStackService repair; filesystem non-semantic scheduler |
| `autosave_getLivePayloadByte()` and format/CRC helpers | Project final resident bytes and serialize/validate v1 records without copying C structs as the wire format. | filesystem AutoSave setup/validation/copy |
| `autosave_applyBankPayload()` / `autosave_applyScenePayload()` / `autosave_applyKitPayload()` / `autosave_applyInstrumentPayload()` | Inverse-project validated winner bytes into retained boot state while tracking is off. Instrument apply rejects unknown three-byte type text. | filesystem matching-winner reader |
| `autosave_extractPayloadSource()` | Read the two-byte source field used for Case-1 payload/HCNAMES cross-check. | filesystem matching-winner reader |
| Scene parameter cells (S080) | `AUTOSAVE_SCENE_PARAM_CHAIN_CONDITION_BASE` 72..73 and `AUTOSAVE_SCENE_PARAM_CHAIN_NEXT_BASE` 74..75 (count 76) inside the reserved 118-byte Scene allocation; getter/restore branches follow the enum order. `autosave_applyScenePayload()` now `continue`s past a zero track-Morph-length cell (it used to `return`, skipping every later cell for pre-S078 records). | SceneData chain setters, filesystem |
| `autosave_setMutationTrackingEnabled()` / complete-resident dirty boundary | Filesystem policy enables tracking only after successful setup; AutoSave OFF/new-session transitions preserve active-transaction safety. | filesystem policy lifecycle |
| `autosaveTrace_record()` and ring read/ack APIs | Producers append bounded RAM records; only filesystem may append them to the diagnostic file and acknowledge them after durable sync. | AutoSave and filesystem under `DEV_MODE_LOGGING` |

## Core/Hardware/SD/filesystem

Affiliate modules: Preset, Menu, PatternData, SampleMemory, storageTypes,
SceneData, AutoSave, AutoSaveTrace, and development-mode boot logging.

Purpose: public typed async filesystem facade. It serializes pattern data
through PatternData accessors after Session 028. Normal kit load/save scans,
opens, and writes root `Kit/NNN Name/` directory-format data, root Instrument
Load/Save operates on registry-owned `Instrument/<type>/` pools, typed index
loading owns validation and selected-type repair, HCNAMES owns resident display
identity/provenance/type authority, boot readers restore the resident Bank, the
retired File/Dir compatibility surface performs no work, and the shared
elapsed-time background CPU budget (`budget_state`, Session 069) gates all
background work (scalar drain, Pattern drain, Pattern repair).
S080 additions: settings.cfg keys `perf_btn_lt`, `perf_note_lt`,
`perf_btn_rt`, `perf_note_rt`, `scd`, `qcd`, `qac` (terminator `lines=24`,
`lines=26` since S083 adds `perf_vel_lt`/`perf_vel_rt`;
load sanitizing qac ≤ 8, qcd ≤ 17, scd ≤ 9); sceneset lines 18/19
`chain_condition`/`chain_next`; PAT4 track header byte 4 (loop exclusion) in
`filesystem_patternBuildHeader()` and the three PAT4 readers
(`FILESYSTEM_SPEC.md`).

Storage text parsing/formatting and descriptor-key validation stay in
`storageTypes.c/h`.

| API | Use | Usual callers / clients |
|---|---|---|
| `filesystem_initCardAndMountBlocking()` / `filesystem_initAfterCardReady()` | Boot card init/mount. | `main.c` |
| `filesystem_bootLoggingBegin()` / `filesystem_bootLoggingArm()` / `filesystem_bootLoggingTimedOut()` / `filesystem_writeBootFailureLogBlocking()` / `filesystem_bootLoggingEnd()` | Own the pre-audio ten-second filesystem deadline and one bounded best-effort retained-code recovery write. Private detail hooks may change only the label inside an armed deadline. | `main.c`; filesystem boot operations under `DEV_MODE_LOGGING` |
| `filesystem_tick()` | Pump asyncfatfs work. While the Session 058 fast drain is active, the BUSY/non-READY branch runs four consecutive `afatfs_poll()` calls instead of one; idle polling keeps its rate limit. | main loop |
| Copy/clear support (S075): `filesystem_borrowNameCacheScratch()` / `filesystem_returnNameCacheScratch()`, `filesystem_identityRow(cls, scene, slot)`, `filesystem_requestCopyResidentNames(cb)` | Lend the 9,000 B name cache as copy/clear working storage (while lent: disposal requests ignored, every op except the copy/clear name write refused like a busy facade); fixed HCNAMES row of a Scene/Kit/Instrument/Pattern/Effect identity; one HCNAMES rewrite (`HNcU`) applying the copy/clear row remap. `filesystem_tick()` admits no background writer (settings, both trace flushes, deferred HCNAMES flush, scalar and both Pattern AutoSave drains) while `copyClear_backgroundSuspended()`. | CopyClear |
| `filesystem_setFastDrain(on)` / `filesystem_fastDrainActive()` | Select/observe the Session 058 foreground fast-drain policy. Foreground-only, not reference-counted, no retained payload. Menu is the sole setter: it enables only after verified codec suspend and clears before resume. | Menu no-playback lifecycle |
| `filesystem_status()` / `filesystem_ack()` | Operation status protocol. A direct Menu callback must snapshot its terminal result before acknowledging it; if it does not immediately post another filesystem request, it must acknowledge `DONE`/`ERROR` before releasing its UI owner so autonomous idle-only schedulers can run. | Preset/Menu |
| `filesystem_requestLoad(type, slot, cb)` / `filesystem_requestSave(type, slot, cb)` | Async typed load/save. For `FS_FILE_KIT`, load is `Kit/NNN Name/kitset.kcg` plus instruments and save routes to the new Kit directory writer. For `FS_FILE_MORPH`, load/save remains legacy `.SND`. | Preset |
| `filesystem_requestLoadKitForScenes(slot, scene_mask, cb)` | Parse one direct Kit library slot `000..999` into staging and fan the completed Kit payload into selected resident Scenes. | Preset/Menu Kit Load |
| `filesystem_requestLoadKitMorphForScenes(slot, scene_mask, cb)` | Parse one Kit directory into staging only so Preset can copy matching source normal endpoints into resident morph endpoints. | Preset/Menu KitMrp Load |
| `filesystem_requestSaveKitDirectory(slot, source_scene, display_name, morph_projection, cb)` | Create/open visible `Kit/<NNN Name>/` with asyncfatfs LFN creation, stream six descriptor-keyed instrument files with visible LFN stems, then stream `kitset.kcg` with returned 8.3 aliases. `morph_projection` writes current interpolated values into both endpoint sections. | Preset/Menu Kit Save |
| `filesystem_requestLoadSceneForScenes(slot, scene_mask, cb)` | Parse `sceneset.scg` plus embedded Kit and the optional named `.fx` child into the independent non-Pattern stage, commit them after validation, then read/validate PAT4 into final Pattern storage. Missing `.fx` supplies the `off` Effect default; malformed `.fx` rejects the Scene. | Preset/Menu Scene Load, boot |
| `filesystem_requestSaveSceneDirectory(slot, source_scene, display_name, cb)` | Replace one root Scene slot and stream `sceneset.scg`, embedded `Kit <name>/`, six Instrument files, one named PAT4 file, and named `.fx` v2 content from a resident Scene. | Preset/Menu Scene Save |
| `filesystem_requestLoadPatternForScenes(slot, scene_mask, cb)` | Validate one root-library PAT4 into the first selected Scene, fan out the complete region to the remaining selected Scenes, publish Pattern identity, and reset hidden-generation baselines. | Preset/Menu Pattern Load |
| `filesystem_requestLoadInstrumentIndex(type, cb)` | Directly open and validate the registered type's `.hcindex` into the shared compact cache. Missing, empty, or structurally invalid metadata transfers the same accepted request into selected-type scan/write/sync recovery; fatal FAT/SD/read/scan/close/write faults return ERROR. The callback runs once. | Nested Instrument Load, InstrumentMrp, and Instrument Save type transitions |
| `filesystem_requestScanInstruments(cb)` / `filesystem_instrumentCount()` / `filesystem_instrumentName()` / `filesystem_instrumentDisplayIndex()` | Scan/query the single shared 1,000-entry root Instrument browser cache for the currently loaded type. | Menu Instrument Load; boot uses one type-at-a-time scan/index passes |
| `filesystem_requestLoadInstrument(scene, slot, type, browser_index, cb)` | Capture one typed index selection into immutable operation scratch and validate it into the one Instrument candidate stage without mutating live SceneData. | Preset Instrument request |
| `filesystem_requestSaveInstrument(scene, slot, display_name, cb)` / `filesystem_requestSaveInstrumentMorph(scene, slot, display_name, cb)` | Save one resident Scene/voice slot to root `Instrument/<stem.ext>` using LFN/case-sensitive create and the descriptor-keyed instrument text writer. The Morph variant writes current interpolated values into both endpoint sections and preserves resident source naming. | Preset Instrument Save |
| `filesystem_requestSaveInstrumentTemp(scene, slot, cb)` / `filesystem_requestLoadInstrumentTemp(scene, slot, type, cb)` | Write/read the hidden typed `.hctmp.<ext>` through the normal serializer/parser without publishing it to HCNAMES or `.hcindex`. | Preset/Menu reversible `kit` row |
| `filesystem_requestSaveInstrumentMorphTemp(scene, slot, cb)` / `filesystem_requestLoadInstrumentMorphTemp(scene, slot, type, cb)` | Write/read the Morph-only `.hctmp.<ext>` projection: one parser anchor plus every Morphable `[morph]` endpoint. The load sets the morph-temporary origin flag so Preset can choose the Morph-to-Morph restore commit. | Preset/Menu InstrumentMrp reversible row |
| `filesystem_loadedInstrumentWasMorphTemporary()` | Query whether the staged hidden Instrument load is the InstrumentMrp Morph-only baseline, valid beside the staged Instrument until the next request reuses operation scratch. | Preset Morph-apply origin dispatch |
| `filesystem_ensureAutosaveFilesBlocking()` / `filesystem_setAutosaveEnabled(enabled)` / `filesystem_autosaveEnabled()` | Establish the hidden pair at boot, apply runtime policy, and authorize mutation tracking/background work only after successful setup. Format and failure rules are in `AUTOSAVE.md`. | `main.c`, Menu/settings policy |
| `filesystem_validateAutosaveWinnerBlocking()` / `filesystem_hasBootWinner()` | Stream-validate both HCPR candidates after settings/index boot and expose only a valid active-Bank match to stage 11. | `main.c` boot stage 10b/11 |
| `filesystem_autosaveValidateCandidateStep()` (static, S074) | One bounded validation step shared by the runtime drain's and the boot validator's phase 3: 128 B CRC chunks, then exactly one extra byte. Data there means overlong (torn), so the candidate is invalid and its `overlong_mask` bit is set (reported as `V` bits 4..5); end-of-file lets the header/commit/CRC verdict stand. At most 273 reads per candidate. | drain and boot validator |
| `filesystem_autosaveBootReaderBlocking()` | Apply a validated matching HCPR v3 winner and evaluate Scene/Effect/Kit/Instrument rows as Case 1 payload, Case 2 narrow load, or Case 3 whole-Scene invalidation. | `main.c` boot stage 11 |
| `filesystem_patternAutosaveBootReaderBlocking()` | Validate each present Scene's PAT4 A/B pair and apply the eligible newest Pattern-AutoSave winner. | `main.c` after scalar restore |
| `filesystem_regenerateHcnamesFromWinnerBlocking()` | Atomically rebuild absent/corrupt typed HCNAMES from a validated winner before its reader proceeds. | boot reader orchestration |
| `filesystem_bootHcnamesAuthoritativeLoad()` | Load the settings-Bank-matching, all-161-rows-refreshed special state entirely from HCNAMES/library sources, including each named Effect child; decline on failed gates/hard error. | `main.c` boot stage 11 |
| `filesystem_setBootLatchBankFallback()` | Record a canonical or HCNAMES-authoritative Bank restore whose dirty mark must replay after tracking enables. | `main.c`, HCNAMES-authoritative reader |
| `filesystem_bootReaderNoticeSceneMask()` / `filesystem_bootReaderNoticeBankFallback()` | Read and clear Case-3/Bank one-shot notice state after audio starts. | Menu boot-notice sequencer |
| `filesystem_autosaveTraceFlushBlocking()` | Bench-only durable boundary for currently pending lifecycle records; ordinary runtime trace flushing is autonomous and lower priority. | temporary test harness only |
| `filesystem_backgroundBudgetRefill()` / `filesystem_backgroundBudgetCharge()` / `filesystem_backgroundBudgetAvailable()` | Shared elapsed-time CPU budget for background work (scalar drain, Pattern drain, repair). Refill adds credit proportional to elapsed wall time; charge subtracts measured work; available is the admission gate. Signed credit tracks overshoot. | filesystem scheduler, PatternStackService repair |
| `filesystem_markSettingsDirty()` | Increment the keyed-settings change revision; the one-second writer acknowledges only the revision it actually serialized and synced. | Menu settings policy |
| `filesystem_requestMidiLearnWrite()` / `filesystem_requestLoadMidiLearn(cb)` | S083: `midilrn.cfg` (MIDI Learn table). The write request sets a 1 B flag; the idle tick starts a full safe rewrite (`FS_INTERNAL_OP_SAVE_MIDILEARN`: `midilrn.tmp`, sync, remove, rename) with no debounce, one retry on error, one follow-up write when a Learn lands mid-write. The load is boot-only (main.c, right after the settings load): tmp recovery, then read; always completes DONE. `FILESYSTEM_SPEC.md` "`midilrn.cfg`". | `midiLearn_captureCc()`; main.c |
| `filesystem_loadedInstrumentSlot()` | Borrow the validated candidate payload for Preset's ordered commit. Names are exchanged through identity rows, not staged filename/stem accessors. | Preset only |
| `filesystem_requestLoadName(type, slot, cb)` | Async name load. For `FS_FILE_KIT`, returns the cached directory scan name instead of opening a `.SND` header. | Preset/Menu |
| `filesystem_requestScanKits(cb)` | Scan root `Kit/` directories into the shared slot-indexed name cache; non-blank rows provide occupancy. | main startup, Menu |
| `filesystem_requestReloadLibraryIndex(kind, cb)` / domain-specific Kit/Scene/Bank wrappers | Read an existing slot-ordered root `.hcindex` into the one shared cache; blank rows remain slot positions. This is read-only cache restoration and never scans or rewrites the namespace. The accepted root Scene/Bank terminal callback acknowledges its captured result before Menu teardown. | Menu entry/type changes and post-DSP root Scene/Bank Load terminal work |
| `filesystem_createLibraryIndexBlocking(kind)` | Boot-only repair/scan and slot-ordered `.hcindex` rebuild for one root library. Runtime numbered-root Saves use the common asynchronous scan/rebuild continuation instead. | boot |
| `filesystem_clearNameCache()` / `filesystem_libraryNameCacheLoaded(kind)` | Dispose/query the one active Instrument/Kit/Scene/Bank browser-name cache. | Menu lifecycle and index gating |
| `filesystem_setIdentityName(row, name)` / `filesystem_identityName(row)` / `filesystem_identityNameMutable(row)` / `filesystem_clearIdentityNames()` | Own the logical Bank/Scene/Kit/six-Instrument/Effect identity interface. Bank aliases BankData; the nine non-Bank rows occupy 81 bytes per Scene. | Menu and filesystem HCNAMES/load/save completion |
| `filesystem_residentSource(row)` / `filesystem_setResidentSource(row, source)` / `filesystem_resolveResidentSource(row, resolved_row)` | Read, stage, and resolve the paired HCNAMES provenance token. The resolver follows Instrument -> Kit -> Scene -> Bank and does no I/O; boot readers own target opens and Case-2/3 decisions. | filesystem successful-load boundaries and AutoSave boot readers |
| `filesystem_requestLoadResidentKitName()` / `filesystem_requestUpdateResidentKitNames()` | Borrow HCNAMES for one Scene's Kit-plus-six block or preserve/overlay full seven-row blocks for a dirty Scene mask. | combined Kit/Instrument Menu session |
| `filesystem_requestLoadResidentInstrumentName()` / `filesystem_requestUpdateResidentInstrumentNames()` | Legacy/narrow one-Instrument HCNAMES row operations; the combined Menu entry normally loads the seven-row Kit block. | Menu/filesystem compatibility paths |
| `filesystem_requestLoadResidentSceneName()` / `filesystem_requestUpdateResidentSceneNames()` | Borrow selected Scene identity for menu use; Scene completion overlays the Scene plus its committed Kit/six-Instrument hierarchy before publication. | Scene menu and Scene completion |
| `filesystem_repairLibraryNamesBlocking()` / `filesystem_repairInstrumentNamesBlocking()` / `filesystem_requestRepairBankNames()` | Canonicalize one namespace with one-candidate rename/sync/rescan before index publication or Bank payload load. No `.hcrepair` journal exists. | boot index wrappers and Bank Load |
| `filesystem_installSamplesBlocking()` / `filesystem_installLoopsBlocking()` | Blocking sample/loop install under audio suspend. | Menu |
| `filesystem_loadedName()` | Read loaded name buffer. | Preset |
| `filesystem_kitSlotExists(slot)` | Query the Kit scan cache for a direct `000..999` numbered folder. | Menu/future browsers |
| `filesystem_kitSlotName(slot)` | Return the shared-cache-backed eight-character Kit name or `Empty   `. The slot number is not part of the cached row. | Menu Load/Save page |
| `filesystem_sceneSlotName(slot)` | Return the shared-cache-backed root Scene name or `Empty   `; Bank-local Scenes are excluded. | Menu Load/Save page |
| `filesystem_bankSlotName(slot)` | Return the shared-cache-backed root Bank name or `Empty   `; Bank-local child Scenes are excluded. | Menu Load/Save page |
| `filesystem_requestScanTestFiles()` / `filesystem_requestScanTestDirs()` and File/Dir test accessors | Retired compatibility API: returns empty/failure and starts no operation or cache. | stale developer callers only |
| `filesystem_diagOp()` / `filesystem_diagPhase()` / `filesystem_diagBytesDone()` | Diagnostics. | diagnostics/future UI |

The retired File/Dir facade has no multi-entry filesystem cache. `menu.c`
nevertheless still links `menu_testEditName[49]`,
`menu_testResultName[49]`, and nine result-screen bytes from its unreachable
compatibility renderer: 107 bytes total. Those are residual Menu UI state, not
filesystem API output, musical identity, or permission to restore the old
diagnostics.
| `filesystem_lastMountResult()` / `filesystem_bootDetectedUnsupportedCard()` | Boot/card status. | main/Menu |
| `filesystem_takeStaleGlobalsWarning()` | One-shot stale globals warning source. | Menu |
| `filesystem_bankChildCursor()` (Session 057) | Live 0-based Bank-local child slot (0-15) during an active Bank Load/Save, or `0xFF` otherwise. Pure read accessor of `op_bank_child_cursor`; drives the `NN.` Load/Save progress indicator, which Menu shows **1-based** (`01.`–`16.`) since S082 P3. 0 before the first child; stays on the last child after the loop. | Menu Bank progress display |
| S082 Bank requests: `filesystem_requestLoadBank(slot, act, empty, flags, cb)` / `filesystem_requestSaveBank(slot, source, name, save_mask, empty_mask, ...)` / `filesystem_lastBankLoadEmptyMask()` / `filesystem_bankSlotExists(slot)` | Two-mask Bank operations (outcome table and phases in `FILESYSTEM_SPEC.md` "Current Bank behavior"); `FS_BANK_FLAG_BOOT/REPLACE/PROGRESSIVE` equal the `PRESET_BANK_LOAD_*` values. The empty-mask getter is read by Menu's completion before scratch resets; the slot test refuses a missing slot before any Scene 17 work. | Preset, Menu |
| Bank child preview: `filesystem_requestScanBankScenes(slot)` / `filesystem_bankChildSceneMask()` / `filesystem_bankChildContentMask()` / `filesystem_scanBankScenesRunning()` / `filesystem_cancelScanBankScenes()` | Present and content (PAT4 trigger) masks of a Bank slot's children, for the Bank page LEDs and the Merge act mask. Cancellable at callback-free phases (finishes ERROR); never holds `menu_storageBusy` (Slot Scrolling Policy). | Menu Bank page |
| `filesystem_setAutosaveYield(yield)` | S082: persistent `fs_autosave_yield`; while set both AutoSave schedulers decline new drains and an in-flight drain aborts at its next allow-listed phase (`filesystem_autosaveDrainAbortSafe()`; Pattern drain phases 3–8 except 7). HCNAMES publication is never cut. | Preset background tick |
| `filesystem_backgroundNamesCaptureScene(src)` / `filesystem_backgroundNameCaptureInstrument(dst_slot, src_scene, src_slot)` / `filesystem_backgroundNameSet(row, name)` | S082 P1 D29 Scene 17 name capture (`fs_background_name[10][9]`, rows `FS_BACKGROUND_NAME_*`): copied from the HCNAMES mirror at the build (per copied slot), renamed after a normal save from 17; every resident-name reader returns it for index 16. | Preset build, filesystem save paths |
| `filesystem_bankLoadDisplayName()` | S082 P3: the eight-character name captured by `filesystem_requestLoadBank()`, for the frozen Load:[Bank] screen. | Menu |
| `filesystem_tickUncounted()` | S082 P2: one `filesystem_tick()` that the DEV poll-count stall observers ignore (`fs_stall_poll_uncounted`); used only for idle-time servicing of a background Bank load. | `main.c` |
| `filesystem_lastBankLoadFailedSceneMask()` (Session 057) | Bitmask of Bank Load children that failed with provably-invalid content (not I/O abort) during the just-completed, still-`FS_STATUS_DONE` Bank Load. Nonzero is the sole signal that the existing filesystem error overlay still needs to be shown once. Cleared at the start of every new Bank Load request. | `preset_bankLoadFailedSceneMask()` (below), Menu `PRESET_OP_BANK_LOAD` success path |

Important private Phase 2 kit helpers:

- `filesystem_scanKits_tick()` opens root `Kit/` by exact display component,
  iterates asyncfatfs objects, and records display names in the shared cache;
  no per-slot alias, occupancy array, or compatibility slot map is retained.
- `filesystem_loadKitDirectory_tick()` opens the selected kit folder, parses
  `kitset.kcg`, and loads six listed instrument files into the 2,048-byte
  `kit_t` stage. It fans out the complete Kit payload only after every file
  validates; runtime apply is handled later through Preset/InstrumentManager.
- `filesystem_loadInstrument_tick()` parses one root Instrument into private
  `kit_instrument_slot_t` candidate staging. It must never reset a live Scene
  slot during asynchronous I/O; Preset owns the post-completion transaction and
  identity publication is separate.
- `filesystem_loadInstrumentIndex_tick()` opens the selected type's
  `.hcindex` directly after changing CWD and closing the redundant directory
  handle. It validates compact printable 1..8-character, nonblank,
  non-temporary, unique, sorted rows. Missing/empty/corrupt metadata hands the
  original callback to `filesystem_beginInstrumentIndexRecovery()`; genuine
  lower-layer faults never start the writer. Menu snapshots and acknowledges
  the terminal status before publishing the cache or showing an error.
- `filesystem_saveInstrument_tick()` writes one resident Scene/voice slot into
  root `Instrument/` with `afatfs_mkdir_lfn()` plus `afatfs_fopen_lfn()`,
  streams `storage_formatInstrumentLine()`, and updates the Instrument browser
  cache from the actual display/alias pair.
- `filesystem_saveKitDirectory_tick()` creates/opens root `Kit/`, creates/opens
  the target LFN Kit folder after deleting every physical directory for that
  slot, streams six instrument files first, and streams `kitset.kcg` after
  member aliases are known. `kitset.kcg` remains authoritative for load, but
  Session 038 Kit Save no longer relies on leaving stale unreferenced files in
  place.
- `filesystem_loadSceneDirectory_tick()` validates complete Scene folders in
  the Scene settings-plus-Kit-plus-Effect stage, commits that non-Pattern
  payload, then reads Pattern directly into final Scene SRAM. It imports legacy embedded-kit
  `audio_out` only when `sceneset.scg` lacks Scene-owned `audio_out`.
- `filesystem_saveSceneDirectory_tick()` writes the current Scene folder shape:
  `sceneset.scg`, embedded Kit without `audio_out`, six instruments, PAT4
  `pattern.pat`, and named `.fx` v2 content.
- `filesystem_saveBankDirectory_tick()` (rewritten Session 057): scans and
  reuses the root Bank directory itself (open-or-rename-then-open if a match
  for the target slot exists, create only if absent) rather than deleting and
  recreating it, then for each Scene bit in the save mask deletes only that
  one Bank-local child (`filesystem_deleteBankChildSlotDirectoryStart()`)
  before writing its replacement — non-masked children are never scanned,
  deleted, or written. This replaced an earlier total-tree-delete design that
  destroyed every child, selected or not, and that could itself fail mid-scan
  on a quarantined `errKit` directory's rewritten LFN entries (`ErrS05`).
  After the per-child loop, it parks the original callback while the root
  Bank directory is rescanned and `/Bank/.hcindex` is rewritten, the same
  Save-owned rebuild chain used for Kit and root Scene saves.
- `filesystem_loadLibraryIndex_tick()` reads one slot-preserving Kit, root
  Scene, or root Bank `.hcindex` into the shared cache. It never compacts blank
  rows and never retains per-slot aliases.
- `filesystem_residentNames_tick()` reads the typed HCNAMES register (mandatory
  `#types` header plus 129 fixed data rows),
  optionally overlays exactly the action-owned rows, rewrites the complete
  variable-length file, and finishes only that metadata transaction. It does
  not infer whether a root namespace changed: Scene Save declares its own
  rebuild, while Scene/Bank Load defers read-only browser restoration until
  after DSP apply.
- Bank Load captures the selected Bank's 16 child display names in one scan.
  Successful children return to and retain the selected Bank as parent CWD;
  only recovery paths reopen from root. Its completed loaded-Scene result
  becomes true only after shared Scene validation and commit and is consumed
  before the later index-reload request.
- Kit folders prefer `NNN Name` and accept `NNN_Name`; scan has a short-alias
  fallback for FAT aliases like `000INI~1` or `001SLA~1`.

asyncfatfs boundary:

- Low-level asyncfatfs API rules live in
  `knowledge_files/specification_reference/ASYNCFATFS_REFERENCE.md`.
  Module-level code should reuse `filesystem.c` or those documented
  component/object primitives instead of creating one-off FAT writers.
- Dot-prefixed files/directories are ordinary objects except macOS AppleDouble
  `._*` entries, which object iteration filters before product scanners see
  them. Product-specific hidden files such as `.hctmp.<ext>` remain explicit
  scanner exclusions.
- Native `afatfs_deleteTree()` provides filesystem-level recursive cleanup for
  one captured object and releases each FAT chain before retiring its complete
  name run. Replacement flows still are not atomic/journaled and must not be
  described as power-loss recoverable.
- Short create, LFN create, and same-parent rename share a sector-local
  deleted/terminator run reservation. A moved `0x00` run initializes and
  persists its target sector before retiring the old tail. Directory extension
  initializes only the new cluster's first sector; later sectors remain hidden
  until that publication path clears them. FAT16 fixed-root exhaustion still
  fails normally.
- `afatfs_getDiagnosticSnapshot()` and `sdcard_getTransportSnapshot()` are
  logging-only, read-only observation helpers for a frozen boot-time
  `ASENSURE` failure capsule. They copy live allocator/cache/file and SD
  transport state without polling, allocating, issuing I/O, changing callbacks,
  retries, or filesystem ownership. `filesystem.c` may call them only while
  freezing the capsule immediately before existing boot recovery destroys the
  active state; ordinary product code must not use them as control flow.

Private but important pattern serialization helpers:

- `filesystem_patternStepPtr()`
- `filesystem_patternMainPtr()`
- `filesystem_patternSettingPtr()`
- `filesystem_patternLengthPtr()`

These select normal PatternData storage or `PATTERNDATA_STAGING_PATTERN` when
loading the currently active pattern.

## Core/Hardware/SD/storageTypes

Affiliate modules: filesystem, SceneData, InstrumentManager, generated
`SD_CARD/Kit` data, `tools/convert_legacy_kits.py`.

Purpose: pure Phase 2/3 storage-format layer. It owns text schema parsing and
formatting, validation, numbered folder parsing, filename/type checks,
display-name normalization, descriptor-key-to-Scene-storage writes, and
descriptor-image text emission. It must not call `asyncfatfs`; filesystem owns
I/O and passes complete text lines/names into this layer. All functions in this
layer use the `storage_` prefix.

| API / Data | Use | Usual callers / clients |
|---|---|---|
| `storage_status_t` | Parser/validator result codes. | filesystem |
| `storage_instrument_type_t` | Format-level type enum for `.drm`, `.snr`, `.cym`, `.hat`. | kitset/instrument parser |
| `storage_kitset_t` | Incremental parse state for `kitset.kcg`. | filesystem directory kit loader |
| `storage_instrument_state_t` | Incremental parse state for one instrument file. | Kit and root-Instrument filesystem loaders |
| `storage_bankset_t` | Incremental parse state for `bankset.bcg` v2. Session 071 expanded to hold `uint16_t scene_mask_voice_edit[16]` and `uint16_t seen_scene_mask_voice_edit` (one bit per Scene). Parser reads legacy single-key `scene_mask_voice_edit=XXXX` (expands to self-only defaults `(1u << i)`) and per-Scene `scene_mask_voice_edit_NN=XXXX`. Writer emits 3 + 16 = 19 lines. | filesystem bankset load/save |
| `storage_banksetParseLineMasked(state, line, protect)` | S082: parse one `bankset.bcg` line without overwriting the protected per-Scene edit-mask entries or `active_scene`; Merge Bank Save reads the slot's old manifest into the same staging that already holds the written Scenes' entries. | filesystem Bank Save phase 62 |
| `storage_kitsetInit()` / `storage_kitsetParseLine()` / `storage_kitsetFinalize()` | Validate `kitset.kcg`, collect instrument filenames/types, and retain legacy `audio_out` side data without making it required. | Kit and Scene filesystem loaders |
| `storage_kitsetHasCompleteLegacyAudioOut()` / `storage_kitsetLegacyAudioOut()` | Expose complete legacy embedded-kit routing only for Scene Load fallback when `sceneset.scg` has no `audio_out`. | `filesystem_loadSceneDirectory_tick()` |
| `storage_instrumentStateInit()` / `storage_instrumentParseLine()` / `storage_instrumentFinalize()` | Validate one instrument file and write descriptor-indexed `[params]`/`[morph]` values into caller-owned Kit/slot staging. | Kit and root-Instrument loaders |
| `storage_instrumentCopyMainToMorphFallback()` | Copy mapped main values into morph buffer when an instrument has no `[morph]` section. | `filesystem_loadKitDirectory_tick()` |
| `storage_instrumentTypeFromText()` / `storage_instrumentFilenameMatchesType()` | Convert/validate type strings and extensions. | kitset/instrument validation |
| `storage_instrumentTypeToText()` / `storage_instrumentTypeExtension()` | Convert type enum back into schema token/extension for save. | Kit Save writer |
| `storage_formatKitsetLine()` / `storage_formatInstrumentLine()` | Emit one bounded schema line at a time for streaming Kit Save and root Instrument Save. Instrument emission writes `self` for own-slot LFO voice selectors. | filesystem Kit/Instrument Save |
| `storage_makeSavedInstrumentDisplayFilename()` | Generate one visible Instrument component from the active fixed-width identity stem, slot type, and optional voice suffix. No Scene-retained stem exists; asyncfatfs returns any required short alias. | filesystem Kit/Instrument Save |
| `storage_patternStubStateInit()` / `storage_patternStubParseLine()` / `storage_patternStubFinalize()` | Validate legacy thin Scene `pattern.pat` placeholders. | Scene Load compatibility |
| `storage_formatPatternStubLine()` | Emit legacy Pattern placeholder lines when compatibility output is requested. | Scene Save compatibility |
| `storage_effectStateInit()` / `storage_effectParseLine()` / `storage_effectFinalize()` / `storage_formatEffectLine()` | Stream-validate or emit `.fx` v2; accept legacy v1 `placeholder=1` as `off`, reject malformed v2, and use registry descriptors for parameter/lane keys. | Scene/Bank Effect Load/Save |
| `chain_condition` / `chain_next` sceneset keys | S080: `storage_scenesetParseLine()` parses two comma-separated values each (0..78 / 0..43) into the staged `chain_condition[]` / `chain_next[]`; missing keys keep the stage default 0 (no chaining). Writer: `filesystem_nextScenesetLine()` lines 18/19. | Scene Load, Bank Load |
| `storage_busCompKey(field)` | S074: the permanent `sceneset.scg` key for one bus compressor field (`bus_comp_mode`, `_amount`, `_time`, `_sidechain`), shared by the parser and the Scene writer. The parser clamps values through `scene_busCompClamp()`; `fader_setting` accepts 0..`SCENE_FADER_SETTING_MAX`. | filesystem Scene writer, `storage_scenesetParseLine()` |
| `storage_parseNumberedFolder()` | Parse visible numbered folders `NNN Name` or `NNN_Name` into direct `000..999` slot plus eight-character display name. Slot `000` is real. | Kit/Scene/Bank scan |
| `storage_copyDisplayName()` / `storage_copyFilename()` | Fixed-width display-name normalization and short filename copying. | filesystem/parser code |

Current ownership decisions:

- `kitset.kcg` owns only format/version validation plus per-slot kit
  membership, instrument filenames, and instrument types.
- Legacy `audio_out` lines in `kitset.kcg` are compatibility-only side data.
  New writers do not emit them. Root Kit Load ignores them; Scene Load may
  import them only for old embedded Kits when `sceneset.scg` lacks `audio_out`.
- The kit name comes only from the kit folder name. It is not stored in
  `kitset.kcg`.
- `storage_kitset_t` retains the six parsed `file=` components only for the
  lifetime of one active manifest parse. They are never copied into `scene_t`
  or `kit_t`.
- Instrument files own per-voice descriptor values, including volume, pan, and
  optional `[morph]` endpoint values.
- MIDI note/channel values, `voice_decimation_all`, per-voice audio routing,
  FX send amount, and fader mode do not belong in `kitset.kcg` or instrument
  files; they belong in Scene settings.
- Missing instrument `[morph]` data is treated as "copy main parameters into
  morph" for morphable descriptors.
- Kit Save, KitMrp Save, root Instrument Save, and InstrumentMrp Save write the same instrument file schema the
  loader accepts. Instrument files emit metadata once, then one `[params]`
  section and one `[morph]` section. Supplemental target selectors are written
  in `[params]` only; `[morph]` writes morphable endpoint descriptors only.
- `self` is a file-only LFO voice selector token. storageTypes resolves it on
  load and emits it on save only when the selector points at the saved
  instrument's own slot.

## Core/Sequencer/LiveRecord (S081)

Affiliate modules: Sequencer (timing services, TIM3 ring), PatternStackService
(writes, target override), EffectsManager (FX-sequence erase), SceneData /
PatternData getters (values, types), buttonHandler / Menu / MidiNotes
(callers), Menu (`menu_automationTargetRecorded()`, the one Menu call).

Purpose: live record (LATCH, OVERDUB) and live erase of the PERF overlay
triggers, MIDI notes and knob automation onto one recording track `rtk` of
one target Scene T = P(rtk), best effort, while playback keeps priority. Rules
and mechanisms: `PATTERN_DYNAMIC_STACK.md` §6.7. Session record:
`081_SESSION_HANDOFF_LOG.md` §2–§8.

Dependency rules: buttonHandler, Menu and MidiNotes/MidiControl call LiveRecord;
LiveRecord calls the Sequencer, PatternStackService, EffectsManager and
SceneData getters, and (one deviation, S081 hardware fix 1)
`menu_automationTargetRecorded()`. The Sequencer never calls LiveRecord (the
drain reads the bitmaps LiveRecord registers). LiveRecord never calls
buttonHandler or the MIDI modules, never runs in TIM3, and never writes a Scene
other than T (Pattern) or the origin Scene's fan-out set (FX sequence).

| API | Use | Usual callers / clients |
|---|---|---|
| `liveRec_init(activeTrack)` | Boot: `rtk` = the active track, mode OFF. | `main.c` after `copyClear_init()` |
| `liveRec_service()` | 500 Hz pass: Scene/playback epochs, Load suspend (`patSvc_replacePending()`), target override and publication, ring drain (span writes, step-end tables, first-change pending writes), FX-erase drain. | `timebase.c`, before `patSvc_tick()` |
| `liveRec_setLatch(on)` / `liveRec_overdubBegin()` / `liveRec_overdubEnd()` | REC tap / hold. Each entry and exit is a new take. | buttonHandler |
| `liveRec_eraseBegin()` / `liveRec_eraseEnd()` | Live erase on (ends overdub) / off (resets every erase source). | `buttonHandler_liveEraseBegin()` / `buttonHandler_liveEraseEnd()` |
| `liveRec_mode()` / `liveRec_armed()` / `liveRec_patternWriting()` | `LIVEREC_MODE_OFF/LATCH/OVERDUB/ERASE`; armed = mode ≠ OFF (copy/clear refusal, REC/erase gates); writing = armed and running (Pattern AutoSave gate, target override). | buttonHandler, Menu, copyClearSession, filesystem |
| `liveRec_onActiveTrackChanged(track)` / `liveRec_setRecordTrack(track)` / `liveRec_recordTrack()` | `rtk` follow (on a real track change), SHIFT+REC override, display. | `menu_setActiveVoice()`, `rtk` cell |
| `liveRec_activePatternLocked()` | Q13: 1 while writing into T ≠ the active Scene; Pattern edits of the viewed Pattern are refused silently. | Menu, `buttonHandler_setRemoveStep()` |
| `liveRec_triggerIn(const liveRec_trigger_t *)` | One hit when it sounds: {track, note, velocity, kind `LIVEREC_HIT_SINGLE/FLAM_GRACE/FLAM_MAIN/ROLL_START/ROLL_HIT`, roll index, source `LIVEREC_SRC_OVERLAY/MIDI`, note_is_default}. Capture, placement, flam pairing, roll classification and spans. | `buttonHandler_overlayReport()`, `midiNotes_handle()` |
| `liveRec_rollRateChange(track, rollIndex)` / `liveRec_rollRelease(track)` | Overlay switch or toggle-off while held; release (roll end write). | `buttonHandler_overlaySwitchHeld()`, `buttonHandler_overlayRelease()` |
| `liveRec_paramIn(target9, source)` | Mark an adjusted recordable target (dirty; latch in LATCH) after the ownership check; captures individual voice/FX Morph edits into `liveRec_morphValue[7]`; with `LIVEREC_AUTOMATION_FIRST_CHANGE` 1 writes the first change of the step at once. No value is passed: the write reads the current value from the origin Scene A. | `menu_liveRecRecordCell()`, `menu_commitTargetEdit()` (MIDI) |
| `liveRec_eraseParam(target9, fxLane)` | Knob erase: the target joins the erase bitmap; an FX lane joins the knob lanes. | Menu knob-erase branch |
| `liveRec_eraseSeqHeld(track, held)` / `liveRec_eraseFxSeqHeld(held)` / `liveRec_eraseTrackHeld(track, held)` | SEQ hold on the active track (specials) / FX SEQ hold (counted) / TRACK hold (full). | buttonHandler erase router |
| `liveRec_eraseReleaseHeld()` | Drop every held SEQ/TRACK/FX-SEQ erase gesture, keeping erase mode and the knob targets. | `handleModeButtons()`, `liveRec_eraseEnd()` |

State (static SRAM1): mode, `rtk`, T, origin Scene A, epochs; the `latch`,
`dirty` and `erase` bitmaps (3 × 64 B over the 9-bit target space); roll
capture (`liveRec_rollTrack/Step/Index/StartDelay/Span/Hits/PendingIdx`,
note, velocity, note-default); flam grace (`liveRec_graceDeltaQ8/Tick/Vel/
Valid`); per-track `liveRec_prevStep[7]` / `liveRec_prevFlags[7]` (VALID,
FIRED, JUMP of the entry that began each step); erase gesture state
(`liveRec_eraseSpecials/Full`, `liveRec_eraseKnobFxLanes`,
`liveRec_eraseFxSeqCount`, `liveRec_eraseSeqCount/Track`);
`liveRec_morphValue[7]`; DEV-only `liveRec_drops[7]`. Sizes:
`STORAGE_SRAM_MANIFEST.md`.

Build switch: `LIVEREC_AUTOMATION_FIRST_CHANGE` in `config.h` (default 1:
the first change in a step is written at once into that step; 0: the value at
the step's end). `LiveRecord.c` raises `#error` when it is undefined.

## main.c

Purpose: boot ordering and foreground service scheduler.

Relevant interchange points:

- `led_processSeqLedState()` is called in the foreground loop after audio render
  opportunities and after TIM3-owned sequencer timing can mark LED state dirty.
  Do not move this into TIM3 without auditing Menu/button/LED access.
- `scene_initAll()` and `bank_init()` run before `dsp_init()` so
  InstrumentManager constructs each tagged runtime member from a defined
  retained type rather than zeroed BSS (`DRM == 0`).
- Pre-audio loading establishes a coherent six-slot image synchronously.
  Immediately after `audioCodec_init()`, `preset_startDrumsetApply()` starts the
  same live clear/image/all-source-rebind worker used by a manual Scene switch.
- `audio_check_and_render()` returns immediately while
  `audioCodec_isSuspended()` is true (Session 058): suspend empties the DMA/
  render queue, so the renderer must not advance queue, trigger, DSP/control,
  or buffer commit without a DMA consumer. `audioCodec_isSuspended()` is a
  side-effect-free observer of the existing `audio_hw_suspended` hardware flag;
  Menu pairs it with `filesystem_setFastDrain()` for eligible stopped-playback
  Load/Save commands.
- Render-loop order inside `audio_check_and_render()` (S081):
  `voiceControl_processPending()` (resets, then triggers) →
  `seq_drainPendingAutomation()` (that step's automation) →
  `seq_rampService()` (automation ramps). This order is what makes a step's
  automation survive its own trigger; do not reorder. `liveRec_init()` runs
  after `copyClear_init()`; `liveRec_service()` runs from `timebase.c`
  immediately before `patSvc_tick()`.
- `main_serviceBackgroundLoadIdle()` (S082 P2) runs once per main-loop pass
  after the regular `filesystem_tick()` + render pair: while a background
  Bank load runs, the codec is live, the facade is BUSY and both audio render
  slots are full (`audioCodec_queueFreeSlots() == 0`), it calls
  `filesystem_tickUncounted()` for up to `BG_LOAD_IDLE_FS_BUDGET_US` (200 µs,
  `config.h`), re-testing the queue before every step. Rendering keeps
  priority; this is foreground scheduling only (no DSP work is skipped).
- `flashImage_verifyAtBoot()` (Session 073, `Core/Hardware/flashImage.c`)
  runs after `din_init()`/`time_initTimer()` and before `sampleMemory_init()`,
  DSP and storage. It checks the stamped per-sector CRC32s and, on a
  mismatch, holds an LCD report until BAR1. It owns no RAM. Its only affiliates
  are the linker's `.image_check` block and `tools/build_lxrv2_img.py`
  (`STORAGE_SRAM_MANIFEST.md` §3.4).
- Sample flash writes go only through `sampleFlash.c`, which refuses any
  sector below `SAMPLE_FIRST_SECTOR` (7) and any operation when
  `SAMPLE_ROM_START_ADDRESS` differs from the linker's `__sample_flash_start`.
- `timebase_holdPreAudioMs()` is used only for the current SD pre-init,
  post-mount, and pre-Bank pacing experiment. It must not be used after audio
  startup, from an ISR, or as runtime filesystem pacing. The intermittent hang
  that motivated these holds remains unlocalized.

## Removed Front-Panel Protocol Surface

Deleted modules:

- `Core/MIDI/frontPanelParser.c`
- `Core/MIDI/frontPanelParser.h`

Removed from live API surface:

- `frontPanel_sendData`
- `frontPanel_sendByte`
- `frontPanel_sendMidiMsg`
- `frontPanel_parseData`
- `frontParser_parseUartData`
- `frontParser_updateTrackLeds`
- front-panel UART stubs in `Uart.h`
- protocol opcodes such as `LED_CC`, `SEQ_CC`, `SET_P1`, `SET_P2`, `STEP_CC`,
  `MAIN_STEP_CC`, `ARM_AUTOMATION_STEP`, `SAMPLE_CC`, and `VOICE_CC`

Historical logs may still mention these names. In current code, they should
only appear in comments, documentation, or old session logs.
## Background Bank load (S082)

How a Bank Load from the Menu (OK on Load:[Bank]) or from MIDI CC0 (S083)
runs without interrupting playback, and how the user can keep working while
it loads. Decision history: `082_SESSION_HANDOFF_LOG.md` §4–§6. File-level
detail: `FILESYSTEM_SPEC.md` "Current Bank behavior".

### Why

- A Bank change replaces Scenes 1–16, but whatever is playing must keep
  sounding exactly the same (D4) and the change must be immediate (D1).
- Everything else that touches the outgoing Bank (AutoSave, Pattern AutoSave,
  copy/clear) gives way, because it is about to be replaced.

### Scene 17

**What it is:**
- A 17th Scene: `scene_background` (`scene_t`) plus `pat_background_region` (its Pattern), addressed as index 16 (`SCENE_BACKGROUND_INDEX`) through `scene_get*()` and `pat_regionFor()` while `scene_backgroundExists()`.
- It is built only when the transport runs and the load replaces or empties a played Scene (the active Scene or any `seq_perTrackPattern[t]`).

**Build** (`preset_buildBackgroundScene()`):
- the active Scene's Scene-level state, with the chain cleared;
- its ten identity names;
- then the whole Pattern region, or per track (T1→T7) the track and its voice slot from that track's played Scene. A track that does not fit is left empty (D3/D30); T6/T7 share slot 6.

**Switch:** `seq_switchToBackgroundScene()` changes only the read address.
- No Scene activation runs (the runtime already holds exactly these images).
- `effects_rebindSourceScene(16)` moves the Effect's cached source (P2-1).
- The view follows only with `PAR_FOLLOW` on.
- PERF shows no Scene. 17 has no MIDI PC number and no chain.

**While it exists:**
- it is editable; its edit mask is itself only (`bank_editTargetMask()` = bit 16);
- it is never AutoSaved (both AutoSave schedulers are gated);
- it can be saved (Scene/Kit/Instrument/Pattern, names from the capture), copied from, and pasted into (non-overlapping only).

**Rules:**
- **Nothing loads into Scene 17:** every load targets the home Scene (`scene_backgroundHomeIndex()`).
- **Release:** the first whole-Scene change releases it permanently (D9a, D10). Per-track moves never release it, and nothing can return to it (D28).
- **Rebuild:** a Bank load while 17 is active rebuilds in place, copying only the tracks whose played resident Scene the load replaces (D31); the view is never moved.

### The state machine (`preset_tickBackgroundBankLoad()`, from `menu_pollPresetStatus()`)

| Phase | What happens |
|------:|--------------|
| 0 | Idle. A pending MIDI request (one slot, latest wins) starts when Preset, the facade and the Menu are idle: `menu_backgroundBankLoadUiBegin()` takes the UI to Load:[Bank] (closing everything, D39) → 6. |
| 6 | MIDI only: wait for the slot's preview, then act as OK (Replace, all 16) → 1, or FAILED for a missing slot. |
| 1 | Decide Scene 17 (`PM_BG_NEEDED`). |
| 2 | Drop queued copy/clear jobs (`ccSvc_dropQueued()`), yield AutoSave (abort at the next safe phase), wait for the running copy/clear job, an idle facade and a free Pattern snapshot; Replace discards the outgoing dirty mask (Merge keeps it). |
| 3 | Scene 17 needed: build (or rebuild) and switch (or move). |
| 4 | `preset_loadBank()`; retry while the facade is busy. A load on Scene 17 or a running direct load is posted **progressive** and sets `PM_BG_UNLOCKED`. |
| 5 | Wait for the filesystem; Menu handles `PRESET_OP_BANK_LOAD`; then `preset_bgEnd()` releases the yield and ends the availability session. |

- Waits in phases 2, 4 and 6 end FAILED after 20 s.
- The result for MIDI is read once with `preset_takeBackgroundBankLoadResult()`.
- While any phase runs:
  - MIDI other than realtime, notes and CC (not CC0/CC32) is not acted on;
  - a further MIDI Bank request is dropped;
  - chains do not evaluate.

### Working while it loads (S082 P3)

**Locked:** phases 1–4 and 6, and any load requested with the transport stopped.
- MODE, SHIFT+REC and page changes are dropped.
- The UI stays on Load:[Bank] with `...`/`NN.`.

**Unlocked:** from phase 5 of a progressive load.
- **Panel.** Every page except Load/Save works: parameters, Morph, Effect, transport, mutes, looper, live record, Scene changes and copy/clear.
- **Load/Save shows only the frozen Bank Load screen:**
  - `Load:[Bank    ]`, the slot, the captured Bank name, live `NN.` (1-based);
  - SEQ LEDs as PERF;
  - no interaction, no Load↔Save toggle.
- **Mode LED.** `LED_MODE4` fast-blinks while the panel is not in Load/Save mode.
- **Availability.**
  - Each child becomes available (present, selectable, a fan-out member) the moment it finishes loading, so the PERF row fills left to right.
  - Pending, in-progress, emptied and failed Scenes are dark and refused.
  - Merge-kept Scenes are available throughout.
- **Scene changes.**
  - The user can switch from Scene 17 to any available Scene, and between available Scenes, repeatedly (a manual SEQ press, `scd` change, SHIFT+SEQ queue or VOICE+SEQ move).
  - Automated changes (chains) wait for the end of the load.
- **Edits.**
  - Edits on the active resident Scene fan out only to available members of its new edit-mask entry.
  - STEP edits on a viewed Scene that is not yet available are dropped.
- **Copy/clear.**
  - A job on an unavailable Scene drops when it runs.
  - An overlapping paste drops while Scene 17 exists.
  - Names publish after the load.
- **Completion.**
  - Off Load/Save: the command ends quietly, with no Load/Save repaint and no failure overlay.
  - On the frozen screen: it ends as a normal Bank Load and stays there.
  - The active Scene is never re-applied for an unlocked load.

### Name editing, Bank page and Instrument `chg` (S082 P0)

Bank page:

* The 16-bit Scene selection defaults to 0xFFFF on both Load:[Bank] and
  Save:[Bank]. Every SEQ button toggles; the last selected bit may be cleared.
* Replace (default) acts on all 16 Scenes. Merge (COPY tap) acts only on
  selected Scenes with pattern content.
* COPY tap/hold uses the common 200 ms threshold: a tap toggles Merge, a hold
  shows the other view (Load: resident RAM; Save: the selected slot). SEQ and
  OK are consumed while COPY is held; encoder slot moves stay allowed and
  refresh the held view. LED_COPY mirrors Merge. COPY on any other Load/Save
  type remains refused.
* Labels: Load Replace ok, Load Merge mr; Save Replace ok/OW, Save Merge
  ok/MR.
* The SEQ matrix lights pattern content (active trigger steps), never bare
  presence; no blinks on either Bank page.
* Selection retention: Load keeps it after a completed Load until the source
  or page changes; Save keeps it across slot moves and resets after a
  completed Save. A page/type/mode exit resets both Merge flags.

Name editing:

* Load/Save pots 2-4 raise the coalesced knob-repaint flag instead of
  repainting the whole frame per detent. `menu_serviceKnobRepaint()` routes
  Load/Save pages through `menu_repaint()` at most every 20 ms, so only changed
  characters are sent. Pot behaviour itself is unchanged.
* BAR2 moves the cursor right and keeps every character; at the last cell it is
  consumed and changes nothing. BAR1 backspace is unchanged.
* The Load/Save pots obey the encoder's storage gate (S082 F2): while
  `menu_storageBusy` or Preset is busy every pot delta is dropped, except pot
  2 on a Kit/KitMrp/nested Instrument Load number (the same turn
  `menu_parseEncoder()` allows). A refused request restores the previous
  `menu_storageBusy` instead of forcing 0 (F3), so it never releases another
  owner's gate.
* Slot scrolling never blocks (binding Load/Save Slot Scrolling Policy,
  `MEMORY.md`): the number updates at once, the name follows, then the LEDs;
  the Bank child preview never holds the gate and each new detent cancels it.

Nested Instrument Load:

* A normal type step to a type different from the destination slot's resident
  type shows `chg` plus row 000's name. A click on the lower row or a positive
  turn commits row 000 of that type; a negative turn does nothing.
* After the commit the row becomes a numbered pool row and the old `kit`
  snapshot is invalidated. Stepping back to the old type shows `chg` again.
* Stepping away and back before committing restores `kit` with its snapshot.
  Multi-Scene destinations hide a type unless every selected Scene can take it.
  InstrumentMrp never shows `chg`.
