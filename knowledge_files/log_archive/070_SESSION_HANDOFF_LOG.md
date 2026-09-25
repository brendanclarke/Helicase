# Session 070 — Systems Fitness Pass

```
DATE: 2026-09-22 through 2026-09-25
SESSION GOAL: Bounded systems-level fitness pass before Phase 5 Effects development —
  engineering hygiene, Load/Save revision, feature behavior completion, testing closeout.
COMPLETED: All four phases. Makefile header deps. Full Load/Save revision (LSR-01..04).
  Probability gating, Scene automation targets 384..403, LED layer bitmap consolidation.
  AutoSave re-enable test, Pattern persistence boundary validation, automation restart fix,
  Scene automation runtime overlay architecture (Q1), Pattern generation fix (Q2),
  transport restart restore (Q3). Hardware-validated.
VERIFIED ON HARDWARE: Yes — T1 (AutoSave OFF→ON), T2a/b/c (Pattern persistence),
  T3 (automation restart), Q1 (Scene automation overlay, 160,129 records).
  Phase 2 LSR-01 final trace (54,972 records). Phase 3 trace (SD_CARD_PHASE3_OUTPUT).

CHANGES THIS SESSION:
- Makefile: -MMD -MP + -include $(OBJS:.o=.d)
- menu.c: +300 lines (LSR-01..04, Scene automation editing, LED consolidation, live refresh)
- menu.h: +14 lines (generation counter, Scene mask, refresh timer, helpers)
- filesystem.c: +64 lines (deferred HCNAMES scheduler, generation fix, load rename)
- filesystem.h: +19 lines (deferred flush trigger, domain-mismatch accessors, load rename)
- sequencer.c: probability gating, Scene automation apply/restore, dirty bitmap, transport fix
- sequencer.h: seq_evaluateStepCondition(), seq_restoreAllAutomation(), seq_restoreAllSceneAutomation()
- presetMorphEngine.c: morph_step_override[6], step automation override API
- presetMorphEngine.h: setStepAutomationOverride, clearAll, getEffectiveVoiceAmount
- InstrumentManager.c: slot6_track7_decay_step_active/value, trigger cascade
- presetManager.c: preset_applyVoiceAudioOutRuntime()
- ledHandler.c: led_activeLayers[41], led_renderFromStack(), priority model
- tools/decode_devlogs.py: offset corrections (SCENE_PARAMS_OFF=10, etc.)

KNOWN ISSUES INTRODUCED: None
KNOWN ISSUES RESOLVED:
- Makefile header dependency tracking (footgun since Session 001)
- HCNAMES not checkpointed at browser domain transitions
- Stale async name callbacks accepted after rapid scrolling
- Probability not gating automation (automation fired on skipped steps)
- Scene automation targets silently dropped by sequencer drain
- LED fall-to-base on layer expiry instead of priority fallback
- Pattern generation reset to 0 on load (older hidden file wins at boot)
- Automation parameters not restored on transport restart
- TIM3 race in seq_setRunning (ISR sees running before init)
- Scene automation using retained setters (AutoSave thrashing, F2)

NEXT SESSION RECOMMENDED GOAL: Session 071 — per-Scene voice-edit mask,
  base-independent LFO voice-morph contribution, Scene superpage live display.
  See S071_VOICE_MORPH_AUTOMATION_MODULATION_CLEANUP.md.
BLOCKERS: None. All Phase 4 tests passed, all decisions resolved.

CRITICAL REMINDERS FOR NEXT SESSION:
- Scene automation uses runtime overlays, not retained setters
- seq_evaluateStepCondition() reads specials before trigger-active check
- HCNAMES flush requires destination state installed before flush call
- Pattern generation must not be reset to 0 on load
- seq_restoreAllAutomation() must precede seq_clearAutomationDirty()
- seq_setRunning() stop-first / start-last ordering
- FX_SEND automation apply is a no-op until the Phase 5 FX bus exists
```

---

## Baseline

Commit `9627f70` on `dev-ph5-effects` (Session 069 closure, 2026-09-20).
Build: text=450,140, data=416, bss=291,756; image 450,572 bytes.
Session 069 Pass 2 hardware-validated 2026-09-22 (PASS).

---

## 1. Phase 1 — Engineering Hygiene

### 1.1 Makefile Header Dependencies

Added `-MMD -MP` to `CFLAGS` and `-include $(OBJS:.o=.d)` to the Makefile.
This resolves the longstanding footgun documented since Session 054: editing
`config.h` (or any header) without `make clean` silently produced a binary
with stale values. Now GCC generates `.d` dependency files alongside each
`.o`, and Make includes them, so any header edit triggers recompilation of
all dependent translation units.

### 1.2 Developer Mode Default

Verified: `DEV_MODE_LOGGING 1` at config.h:88. This remains the default build
configuration. No changes made.

### 1.3 IWDG — Leave Inactive

Verified: `DEV_LOGGING_IWDG 0` at config.h:200. Left inactive per standing
policy (prior activation caused boot-hang regressions in Sessions 044/054).

---

## 2. Phase 2 — Load/Save Revision

Four items under one selection-coordinate architecture. All implemented and
hardware-tested.

### 2.1 LSR-01 — HCNAMES Checkpoint at Browser Domain Transitions

**Problem.** Switching between Kit and Instrument browser domains, or changing
Instrument type, clobbers the shared 9,000-byte name cache. Any pending
HCNAMES dirty mask from the outgoing domain's edits was silently lost.

**Solution.** Mark-and-flush of the existing `menu_residentNameDirtySceneMask`
at every browser domain transition boundary:

- Kit → Instrument (type enter)
- Instrument → Kit (type exit)
- Instrument type A → Instrument type B (type switch)
- VOICE button entry (from any browser state)
- Pot-1 rotary entries into Instrument parameters
- Instrument exit (back to Load/Save root)

The flush calls `menu_triggerDeferredHcnamesFlush()` which sets the dirty
mask and defers actual I/O to the filesystem scheduler (LSR-02).

**Ordering fix (Appendix A1-A4).** The destination state (new type, new
domain) must be installed in menu state BEFORE the flush call. This ensures
the completion callback dispatches to the correct context. All four
transition sites install destination state first, then flush.

**Optimistic repaint (F1-F4).** Four sites gained an immediate visual repaint
before the deferred flush, eliminating the ~300ms visual delay on dirty-path
transitions. The repaint uses cached data already in menu state; no I/O.

**LSR-01 Supplement.** After the initial LSR-01 implementation, hardware
testing found 4 missing flush boundaries. These were added, bringing the
total to 6 explicit checkpoint sites. Build after supplement: 452,188 bytes.

### 2.2 LSR-02 — Deferred HCNAMES Write

**Problem.** Page exit from Load/Save had to wait for HCNAMES persistence to
complete before the display could transition, because the browser and its
dirty state were coupled.

**Solution.** New scheduler rung in `filesystem_tick()`, positioned between
PatternTrace flush and budget refill. The page exit tears down the browser
immediately (display transitions, input unlocks) and retains only the dirty
mask. The deferred scheduler picks up the retained mask and completes the
HCNAMES write asynchronously.

`menu_hasResidentNameDirtyMask()` — returns whether any Scene bit is set.
`menu_triggerDeferredHcnamesFlush()` — sets the deferred-write flag for the
filesystem scheduler to pick up.

### 2.3 LSR-03 — Blank vs. Empty Display Semantics

**Problem.** The Load page could not distinguish "name not yet loaded" from
"index proved this slot is empty." Both displayed as spaces, and the OK
button was always enabled.

**Solution.**
- Blank (8 spaces) = "not ready" — the name request is in flight or has not
  started. OK is disabled on the Load page while the displayed name is
  all-space.
- `Empty` = "proved absence" — the index completed and this slot has no
  directory. OK remains disabled (nothing to load).
- Domain-mismatch check added to all four `filesystem_*SlotName()` accessors.
  If the caller's browser domain does not match the active filesystem
  operation's domain, the accessor returns blank, preventing cross-domain
  name leaks.

### 2.4 LSR-04 — Selection Generation Counter

**Problem.** Rapid scrolling through the Load/Save browser generated multiple
overlapping async name requests. Callbacks arriving out of order caused the
displayed name to retroactively change to an older selection, and
Kit/KitMrp/Instrument loads gated input via `menu_storageBusy` to prevent
this — causing UI lag.

**Solution.** Single `uint8_t` generation counter (`menu_selectionGeneration`)
plus one `uint8_t` snapshot variable.

- Incremented on every scroll event and every type switch.
- Captured into a local snapshot before issuing an async name request.
- Completion callbacks compare their captured snapshot against the live
  counter; mismatch = stale → silent discard.
- Kit/KitMrp/Instrument loads no longer gate input via `menu_storageBusy`.
- OK commit increments generation to freeze the display name (any in-flight
  callback becomes stale and cannot overwrite the committed name).

**RAM cost:** 2 bytes (generation counter + snapshot).

### Phase 2 build and test

Files changed: menu.c (+300 lines), filesystem.c (+64), menu.h (+14),
filesystem.h (+19). Build: 452,188 bytes after LSR-01 supplement, 452,028
text after user-feel optimistic repaints.

Hardware test: SD_CARD_LSR01-FINAL trace analysis — 54,972 records, zero
failures, zero stale callback accepts, all HCNAMES transitions correct.

---

## 3. Phase 3 — Feature Behavior

### 3.1 Probability Gating

**Problem.** `seq_advanceTrackStep()` applied probability only around
`seq_triggerVoice()` and queued step automation afterward regardless. A step
that failed probability still applied its automation — violating the hold
model where active steps are the reset boundaries.

**Solution.** New function `seq_evaluateStepCondition()`:
1. Reads dynamic specials BEFORE the trigger-active check.
2. Evaluates the shared conditional gate (probability, condition logic).
3. Returns `step_allowed` boolean.
4. `step_allowed` gates both trigger AND automation publication.
5. Erase is independent of probability (always allowed when erase mode active).
6. Non-trigger steps with automation fire normally when no condition is set.

**Error and remediation.** The initial implementation incorrectly moved
automation queueing inside the step-active block, destroying the ability for
non-trigger steps (automation-only) to fire. This was corrected in
`S070_PHASE3_FUCKUP_REMEDIATION.md`: specials are read before the
trigger-active check, and `step_allowed` gates both trigger and automation
independently of the trigger state. No public `sequencer.h` API changed.

### 3.2 Scene Automation Targets (384–403)

**Problem.** `seq_drainPendingAutomation()` handled only voice descriptor
parameters (`instrumentParam_isVoiceParameter(target)`) and silently dropped
Scene targets. Step automation for Voice Morph, Audio Out, and FX Send had
no runtime effect.

**Solution.** Complete Scene-target automation dispatcher:

**New Scene mod target entries (12 total):**
- AUDIO_OUT: IDs 392–397 (one per voice), max value 5
- FX_SEND: IDs 398–403 (one per voice), max value 127 (stubbed apply — no
  FX bus yet)
- Voice Morph: existing IDs, `SCENE_MOD_TARGET_USE_AUTOMATION` flag added

**Scene automation apply function:** `seq_applySceneAutomation()` static in
sequencer.c. Dispatches by target range to the appropriate runtime overlay
(see §4 Q1 for the final overlay architecture). Voice Morph uses 7→8 bit
expansion: `menu_morphAutomationExpand()` maps 0–126→0–252, 127→255.
Corresponding `menu_morphAutomationStore()` maps 0–255→0–127 for storage.

**Step-edit cycling:** The step-edit VOI (voice) field now cycles through 8
categories: voices 1–6, scn (Scene targets), fx (FX targets). D17 design
decision: category change always defaults to `PAT_AUTOMATION_TARGET_OFF`
sentinel (`0x1ff`) — the "off" value is a Pattern-only concept, never stored
in Scene parameter space.

**Held-step overlay extension:** The VOICE-page held-step automation overlay
(Session 066) was extended for Scene setting cells. `MENU_SCENE_SETTING_COUNT`
changed from 3 to 4, with `MENU_SCENE_SETTING_VOICE_MORPH` added as the
fourth setting.

**Progressive automation scan:** `va_scanService()` extended to detect Scene
targets. Produces `va_searchSceneMask` (1 byte, 3 bits: vm=0x01, ou=0x02,
fx=0x04) for pattern-wide Scene target underlines on the held-step overlay.
Scans 4 steps per service pass (128 steps = 32 passes).

**Live display:** `menu_sceneLiveRefreshService()` runs at 8 Hz during
playback and repaints live Scene values on PERF and VOICE/mix pages. Uses a
16-bit timer (`menu_sceneRefreshTimestamp`) against `timebase_tim2Now()`.

**RAM:** 1 byte `va_searchSceneMask` + 2 bytes refresh timer + 1 byte menu
category state for D17 editor = 4 bytes total.

### 3.3 LED State Consolidation

**Problem.** LED temporary effects (blink, flash, pulse, chase) restored
state independently using `led_reset()` which always fell back to the base
state. A pulsed LED that was also blinking fell to base instead of blinking.
Layer interactions were order-dependent.

**Solution.** Per-LED active-layer bitmap.

`led_activeLayers[41]` — one byte per LED, bits indicate which layers are
currently active. Priority order: pulse > flash > blink/chase > base.

`led_renderFromStack()` — replaces blind `led_reset()` in all expiry paths.
Clears the expiring layer's bit, then renders the highest remaining active
layer. If only base remains, renders the base state.

Chase LED runs at blink priority (D12 design decision). The held-step
automation overlay writes LEDs directly and does not participate in the layer
system (it fully owns the LED state during its activation).

**RAM:** 41 bytes `led_activeLayers[]`.

### Phase 3 build

Build: text=455,060, bss=291,804 after implementation. After remediation:
text=455,548, bss=291,812. Final Phase 3 closeout: 455,980 bytes at commit
`e1a3223`.

---

## 4. Phase 4 — Testing Closeout

### Findings

**F1 — Decoder offset regression.** Session 060 Phase C added 2 source bytes
after name fields in the AutoSave record, shifting all downstream offsets in
`tools/decode_devlogs.py`. Fixed: `SCENE_PARAMS_OFF` 8→10,
`KIT_PARAMS_OFF` 8→10, `INST_NORMAL_OFF` 11→13, `INST_MORPH_OFF` 83→85.

**F2 — Scene-target AutoSave thrashing.** `seq_applySceneAutomation()` used
retained Scene/Kit setters during step playback, keeping AutoSave perpetually
busy and overwriting user-set values. Root cause of the Q1 architectural
decision (runtime overlay).

**F3 — `menu_loadInstrumentTransactionBusy()` incomplete.** Extended to
include `preset_getStatus() != PRESET_IDLE`, ensuring the instrument
transaction busy check covers the full preset apply drain.

**F4 — Stale Pattern generation.** `fs_pattern_generation[si] = 0u` at 3
load sites reset the generation to 0, causing an older hidden file with
generation > 0 to win at boot. Fixed: removed the resets, boot reader
non-`@` branch seeds from `winner_generation`.

**F5/F6 — Automation missed on transport restart.** Two related defects:
(F5) `seq_clearAutomationDirty()` zeroed the dirty bitmaps without restoring
parameters to their retained values. (F6) `seq_setRunning()` set
`seq_running=1` before state initialization was complete, creating a TIM3
race window where the ISR could see the running flag and advance steps
before the sequencer was ready.

### Decisions

**Q1 — Scene-target automation runtime overlay.** Major architectural
decision. `seq_applySceneAutomation()` replaced with a runtime-only dispatch
architecture that never touches retained Scene/Kit state:

*Voice Morph overlay:*
- `morph_step_override[6]` array in presetMorphEngine.c — each slot has
  `active` flag and `amount` value.
- `presetMorph_setStepAutomationOverride(slot, amount)` — sets override.
- `presetMorph_clearAllStepAutomationOverrides()` — clears all on restore.
- `presetMorph_getEffectiveVoiceAmount(slot)` — returns step override when
  active, else retained per-voice amount.
- Morph LFO interaction: LFO modulates around the step override value when
  active. The effective amount is `step_override + lfo_delta`, clamped 0..255.
  Decimation last-writer-wins.

*Slot-6 track-7 generated decay override:*
- `slot6_track7_decay_step_active` and `slot6_track7_decay_step_value` in
  InstrumentManager.c.
- Trigger cascade: step override > LFO contribution > retained value. The
  runtime writer checks step-active before LFO before retained.

*Audio Out routing override:*
- `preset_applyVoiceAudioOutRuntime(voice, value)` in presetManager.c.
- Writes the mixer routing register directly, bypassing Scene/Kit storage.

*Scene dirty bitmap and restore:*
- `seq_scene_automation_dirty` (`uint32_t`) in sequencer.c — one bit per
  Scene automation target. Set when a step automation override is applied.
- `seq_restoreAllSceneAutomation()` — walks the bitmap and restores each
  dirty target from its retained SceneData getter. Called from
  `seq_setStepIndexToStart()`.

*FX Send:* Deliberately skipped. The `FX_SEND` target IDs (398–403) are
allocated and editable in the step editor, but `seq_applySceneAutomation()`
treats them as a no-op until the Phase 5 FX bus is implemented.

**Q2 — Pattern generation fix.** Three sites in filesystem.c reset
`fs_pattern_generation[si]` to 0 during explicit Pattern/Scene/Bank loads.
This caused the boot reader to prefer an older hidden-file candidate with
generation > 0 over the just-loaded Pattern (which had generation 0).

Fix: removed all three resets. The function was renamed from
`filesystem_resetPatternAutosaveGeneration()` to
`filesystem_patternAutosaveOnLoad()` to reflect its actual role (invalidating
the sd-clean authority without resetting the monotonic generation). The boot
reader's non-`@` branch already seeds from `winner_generation`, which
remains correct. No PAT4 file format change.

**Q3 — Transport restart automation restore.** Two fixes:

1. `seq_restoreAllAutomation()` — new function that walks all 6 slots'
   64-bit dirty bitmaps and restores each dirty descriptor index from
   `morph_interpolation[]` (the morph-crossfader result) via
   `instrumentManager_writeRuntime()`. Called from
   `seq_setStepIndexToStart()` BEFORE `seq_clearAutomationDirty()`.

2. `seq_setRunning()` restructured:
   - Stop: sets `seq_running = 0` FIRST, then performs cleanup (calls
     `seq_setStepIndexToStart()` which triggers the restore).
   - Start: performs all initialization FIRST, then sets `seq_running = 1`
     LAST.
   This closes the TIM3 preemption window where the ISR could see
   `seq_running == 1` before initialization was complete.

   Redundant `seq_clearAutomationDirty()` removed from the stop branch
   (it's already called by `seq_setStepIndexToStart()`).

### Test Results

| Test | Description | Result | Notes |
|------|-------------|--------|-------|
| T1 | AutoSave OFF→ON re-enable (SD_CARD_ATS_OFF_ON) | PASS | Full lifecycle: setup, tracking, scalar+Pattern convergence |
| T2a | Pattern Load persistence round-trip | FAIL → PASS | Initial FAIL exposed F4 (generation reset). PASS after Q2 fix |
| T2b | Scene/Bank Load Pattern persistence | PASS | Partial Bank Save/Load without modifying unselected Patterns |
| T2c | Boot restore from active Scene | PASS | All Scenes, correct Pattern winners |
| T3 | Automation restart (SD_CARD_PH4_AUTOMATION_MISSED) | PASS | All voices, parameters restored correctly |
| Q1-HW | Scene automation overlay hardware verification | PASS | 160,129 trace records, zero E/X errors |

### Deferred

- **D-A**: Pattern copy/paste/clear test — deferred with copy operations.
- **D-B**: Pattern power-cut test — requires instrumentation.
- **D-C**: Edit-during-snapshot test — deferred with record/erase admission.
- **D-D**: Duplicate filename retest — expected non-issue after S056 LFN fix.
- **4.3**: Duplicate filename test — deferred per D-D.

---

## 5. Build History

| Phase | Commit | text | data | bss | image |
|-------|--------|------|------|-----|-------|
| Baseline (S069) | `9627f70` | 450,140 | 416 | 291,756 | 450,572 |
| Phase 2 LSR-01 | — | 451,964 | — | — | — |
| Phase 2 LSR-01 supplement | — | 452,188 | — | — | — |
| Phase 2 user-feel | — | 452,028 | — | — | — |
| Phase 3 closeout | `e1a3223` | 455,060 | — | 291,804 | 455,980 |
| Phase 3 remediation | — | 455,548 | — | 291,812 | — |
| Phase 4 Q1 implementation | — | 455,804 | 416 | 291,820 | 456,236 |
| Phase 4 Q2 fix (T2a retest) | — | 455,652 | 416 | 291,804 | 456,084 |
| Final (Phase 4 closeout) | `e3ae961` | 455,804 | 416 | 291,820 | 456,236 |

---

## 6. RAM Allocation Changes

| Item | Region | Bytes | Owner |
|------|--------|-------|-------|
| `menu_selectionGeneration` + snapshot | SRAM1 .bss | 2 | menu.c |
| `led_activeLayers[41]` | SRAM1 .bss | 41 | ledHandler.c |
| `va_searchSceneMask` | SRAM1 .bss | 1 | menu.c |
| `menu_sceneRefreshTimestamp` | SRAM1 .bss | 2 | menu.c |
| `menu_scene_category` (D17 editor) | SRAM1 .bss | 1 | menu.c |
| `morph_step_override[6]` (active+amount) | SRAM1 .bss | 12 | presetMorphEngine.c |
| `slot6_track7_decay_step_active/value` | SRAM1 .bss | 2 | InstrumentManager.c |
| `seq_scene_automation_dirty` | SRAM1 .bss | 4 | sequencer.c |
| **Total new allocation** | | **65** | |

Net bss change from baseline: 291,820 − 291,756 = 64 bytes.
Net text change from baseline: 455,804 − 450,140 = 5,664 bytes.

---

## 7. Files Changed (Complete List)

### Phase 1
| File | Change |
|------|--------|
| `Makefile` | `-MMD -MP` in CFLAGS, `-include $(OBJS:.o=.d)` |

### Phase 2 (LSR-01..04)
| File | Change |
|------|--------|
| `Core/Menu/menu.c` | +300 lines: HCNAMES checkpoint flush at 6 domain transition sites, destination-state-before-flush ordering at 4 sites, optimistic repaint at 4 sites, generation counter increment on scroll/type-switch, snapshot capture before async requests, stale callback discard on generation mismatch, OK commit generation freeze, blank/Empty display logic, OK disabled while unresolved, `menu_hasResidentNameDirtyMask()`, `menu_triggerDeferredHcnamesFlush()` |
| `Core/Menu/menu.h` | +14 lines: `menu_selectionGeneration`, snapshot variable, `menu_hasResidentNameDirtyMask()`, `menu_triggerDeferredHcnamesFlush()` declarations |
| `Core/Hardware/SD/filesystem.c` | +64 lines: deferred HCNAMES scheduler rung in `filesystem_tick()`, domain-mismatch check in slot-name accessors |
| `Core/Hardware/SD/filesystem.h` | +19 lines: deferred flush trigger API, domain accessor declarations |

### Phase 3
| File | Change |
|------|--------|
| `Core/Sequencer/sequencer.c` | `seq_evaluateStepCondition()` implementation, `seq_advanceTrackStep()` restructured for probability+automation gating, `seq_applySceneAutomation()` static function, Scene target drain dispatch |
| `Core/Sequencer/sequencer.h` | `seq_evaluateStepCondition()` declaration |
| `Core/Menu/menu.c` | Scene automation: VOI 8-category cycling, D17 off-default, Scene mod target table expansion (12 entries), `va_scanService()` Scene target detection, `va_searchSceneMask` production, `menu_sceneLiveRefreshService()`, held-step overlay Scene cell extension, `menu_morphAutomationStore()`/`menu_morphAutomationExpand()`, `MENU_SCENE_SETTING_COUNT` 3→4, `MENU_SCENE_SETTING_VOICE_MORPH` |
| `Core/Hardware/frontPanel/ledHandler.c` | `led_activeLayers[41]`, `led_renderFromStack()`, priority-based re-render on all layer expiry paths, chase at blink priority |

### Phase 4
| File | Change |
|------|--------|
| `Core/Sequencer/sequencer.c` | `seq_scene_automation_dirty` bitmap, `seq_restoreAllSceneAutomation()`, `seq_restoreAllAutomation()`, `seq_setStepIndexToStart()` restructured (restore before clear), `seq_setRunning()` restructured (stop-first/start-last) |
| `Core/Sequencer/sequencer.h` | `seq_restoreAllAutomation()`, `seq_restoreAllSceneAutomation()` declarations |
| `Core/Bank/Scene/Preset/presetMorphEngine.c` | `morph_step_override[6]`, `presetMorph_setStepAutomationOverride()`, `presetMorph_clearAllStepAutomationOverrides()`, `presetMorph_getEffectiveVoiceAmount()` |
| `Core/Bank/Scene/Preset/presetMorphEngine.h` | Override API declarations |
| `Core/DSP/Instruments/InstrumentManager.c` | `slot6_track7_decay_step_active`, `slot6_track7_decay_step_value`, trigger cascade step>LFO>retained |
| `Core/Bank/Scene/Preset/presetManager.c` | `preset_applyVoiceAudioOutRuntime()`, `menu_loadInstrumentTransactionBusy()` extended |
| `Core/Hardware/SD/filesystem.c` | Removed `fs_pattern_generation[si] = 0u` at 3 sites, renamed `filesystem_resetPatternAutosaveGeneration` → `filesystem_patternAutosaveOnLoad` |
| `Core/Hardware/SD/filesystem.h` | Function rename |
| `tools/decode_devlogs.py` | `SCENE_PARAMS_OFF` 8→10, `KIT_PARAMS_OFF` 8→10, `INST_NORMAL_OFF` 11→13, `INST_MORPH_OFF` 83→85 |

---

## 8. Phase 2 Design Decisions and Risk Resolutions

### Resolved Risks (from S070_PHASE2_LOAD_SAVE_REVISION.md)

**R1 — HCNAMES mirror lifetime.** Resolved: mark-and-flush of existing dirty
mask, not a separate snapshot buffer. The 9,000-byte name cache is not
duplicated; only the 16-bit scene dirty mask is retained for deferred write.

**R2 — Stale callback ordering.** Resolved: generation counter with
snapshot-vs-live comparison. OK commit freezes generation. No post-commit
name overwrite is possible.

**R3 — Page-exit vs. persistence race.** Resolved: page exit tears down the
browser immediately and retains only the dirty mask. The deferred scheduler
in `filesystem_tick()` completes the write. Verified compatible with the
Session 056 page-exit expedite.

**R4 — Bank identity agreement.** Verified: Load/Save completion path
preserves `settings.cfg` / HCNAMES row 0 / HCPR agreement. No refactoring
of the completion path was needed.

**R5 — Corrupt/partial Pattern inside Scene loads.** Verified: the PAT4
reader validates before committing; a failed Pattern does not publish `@`
provenance.

**R6 — Load/Save exclusion during active AutoSave transaction.** Verified:
the existing exclusion gate prevents HCNAMES publication races.

### AutoSave Exclusion Gates (6 layers, confirmed unchanged)

1. `menu_storageBusy` — Menu facade occupancy
2. `filesystem_status() != FS_STATUS_BUSY` — filesystem facade occupancy
3. Page-exit suppression (Session 056 expedite)
4. `fs_autosave_page_suppressed` — Load/Save page flag
5. Transaction atomicity (A/B record completion)
6. HCNAMES convergence step ordering

---

## 9. Phase 3 Detailed Implementation Record

### 3.1 — Probability Gating (42 named changes)

**seq_evaluateStepCondition() contract:**
```
Input:  track index, step index
Output: step_allowed (bool)
Side:   reads step specials for condition data

1. Read step specials (note, velocity, probability, condition)
2. If no condition set → allowed
3. Evaluate condition (probability percentage, future condition types)
4. Return allowed/denied
```

Called from `seq_advanceTrackStep()` before the trigger-active check. The
trigger and automation paths both check `step_allowed`:
- If denied: no trigger, no automation queued
- If allowed: trigger (if step active) and automation (always) proceed
- Erase: independent of probability, always allowed when erase mode active
- Non-trigger steps (automation-only, step not active): fire normally when
  no condition is set

### 3.2 — Scene Automation Target Table

12 new entries in the Scene mod target namespace:

| ID Range | Target | Max Value | Apply |
|----------|--------|-----------|-------|
| 384–391 | Voice Morph (per voice 0-5, + scn, fx) | 127 (stored) / 255 (expanded) | Runtime overlay |
| 392–397 | Audio Out (per voice 0-5) | 5 | Runtime overlay |
| 398–403 | FX Send Amount (per voice 0-5) | 127 | Stubbed (no FX bus) |

Voice Morph 7→8 bit expansion:
- Storage: 7-bit value (0..127) in the automation entry
- Expansion: `menu_morphAutomationExpand(v)` → 0..126 maps to 0..252
  (v * 2), 127 maps to 255
- Compression: `menu_morphAutomationStore(v)` → 0..255 maps to 0..127
  ((v + 1) / 2)

### 3.3 — LED Active Layers

Priority order (highest to lowest):
1. Pulse (momentary highlight, highest priority)
2. Flash (group overlay, 400/80ms timing)
3. Blink / Chase (periodic toggle, shared priority level)
4. Base (steady state, lowest priority)

On layer expiry, `led_renderFromStack()` clears the expired layer's bit in
`led_activeLayers[led_index]` and renders the highest remaining active layer.
If only base remains, renders base. This replaces the previous `led_reset()`
which always fell to base regardless of other active layers.

---

## 10. Phase 4 Q1 — Scene Automation Runtime Overlay Architecture

### Motivation

The initial `seq_applySceneAutomation()` (Phase 3) routed automation values
through the retained Scene/Kit setters — the same path used by user edits
and preset loads. This caused:
- F2: AutoSave perpetually busy (every step tick marked Scene/Kit dirty)
- User-set values overwritten during playback
- No clean restore path on transport stop

### Architecture

Runtime-only overlay: step automation writes transient runtime state that
is separate from retained Scene/Kit storage. The retained values are
preserved and serve as the restore source.

```
┌─────────────────────────────────────────────────┐
│ Step automation fires (TIM3 ISR → pending queue) │
└──────────────────┬──────────────────────────────┘
                   │
         ┌─────────▼──────────┐
         │ Voice descriptor?   │─── Yes ──→ instrumentManager_writeRuntime()
         │ (target < 384)      │            seq_automation_dirty[slot] |= bit
         └─────────┬──────────┘
                   │ No (Scene target 384+)
         ┌─────────▼──────────┐
         │ seq_applyScene-     │
         │ Automation()        │
         └─────────┬──────────┘
                   │
    ┌──────────────┼──────────────┐
    │              │              │
 Morph          Slot6 Decay    Audio Out
    │              │              │
 morph_step_   slot6_track7_  preset_apply-
 override[]    decay_step_*   VoiceAudioOut-
    │              │           Runtime()
    │              │              │
 seq_scene_    seq_scene_     seq_scene_
 automation_   automation_    automation_
 dirty |= bit  dirty |= bit   dirty |= bit
```

### LFO + Step Automation Interaction

**Voice Morph:** The morph engine's LFO contribution modulates around the
step override value when active:
```
effective = step_active ? step_amount : retained_amount
lfo_result = effective + lfo_delta
output = clamp(lfo_result, 0, 255)
```
`presetMorph_getEffectiveVoiceAmount(slot)` is the single query point. It
returns the step override when active, else the retained per-voice amount.
The morph decimation engine uses this as its base.

**Slot-6 Track-7 Generated Decay:** Three-level cascade:
```
if (step_active)     → use step_value
else if (lfo_active) → use lfo_contribution
else                 → use retained Scene value
```
The runtime writer checks in this priority order.

**Audio Out:** Direct register write, no LFO interaction.

### Transport Restore

On transport stop or pattern restart (`seq_setStepIndexToStart()`):

1. `seq_restoreAllSceneAutomation()` walks `seq_scene_automation_dirty`:
   - For each set bit, reads the retained value from the appropriate
     SceneData getter and applies it through the runtime path.
   - Morph overrides: `presetMorph_clearAllStepAutomationOverrides()`.
   - Slot-6 decay: clears `slot6_track7_decay_step_active`.
   - Audio Out: `preset_applyVoiceAudioOutRuntime()` from retained value.
   - Clears `seq_scene_automation_dirty`.

2. `seq_restoreAllAutomation()` walks all 6 voice-parameter dirty bitmaps:
   - For each set bit, reads `morph_interpolation[slot][index]` and writes
     through `instrumentManager_writeRuntime()`.
   - Clears per-slot dirty bitmap.

Both restore functions run BEFORE `seq_clearAutomationDirty()` in
`seq_setStepIndexToStart()`.

---

## 11. Phase 4 Q2 — Pattern Generation Fix

### Root Cause

Three sites in `filesystem.c` contained `fs_pattern_generation[si] = 0u`:
1. Explicit root Pattern Load completion
2. Scene Load Pattern commit
3. Bank Load per-Scene Pattern commit

When a Pattern was loaded and its generation reset to 0, the boot reader
would find an older hidden `.patNNa`/`.patNNb` file with generation > 0 and
prefer it — restoring stale Pattern data on boot instead of the just-loaded
Pattern.

### Fix

Removed all three generation resets. The function was renamed from
`filesystem_resetPatternAutosaveGeneration()` to
`filesystem_patternAutosaveOnLoad()` to clarify its intent: it invalidates
the sd-clean authority (so AutoSave knows the resident Pattern differs from
the card) without resetting the monotonic generation counter. The boot
reader's non-`@` branch already seeds from `winner_generation`, which
remains correct because the generation is never reset below the last
successfully written hidden file.

No PAT4 file format change. No HCNAMES schema change.

---

## 12. Phase 4 Q3 — Transport Restart Automation Restore

### Root Cause

Two defects in the transport restart path:

**F5 — Clearing without restoring.** `seq_clearAutomationDirty()` zeroed all
6 slots' dirty bitmaps without first restoring the overridden parameters to
their retained values. Any voice descriptor parameter that had been
overridden by step automation was left at its last step-written value.

**F6 — TIM3 race in `seq_setRunning()`.** The start path set `seq_running=1`
before completing state initialization. TIM3 (4 kHz, priority 2) could
preempt and call `seq_tick()` → `seq_advanceTrackStep()` while the step
index, automation bitmaps, and override state were still being set up.

### Fix

**`seq_restoreAllAutomation()`** — new function. Walks all 6 slots:
```c
for each slot:
    bitmap = seq_automation_dirty[slot]
    while (bitmap != 0):
        idx = __builtin_ctzll(bitmap)
        instrumentManager_writeRuntime(slot, idx, morph_interpolation[slot][idx])
        bitmap &= ~(1ULL << idx)
    seq_automation_dirty[slot] = 0
```

Called from `seq_setStepIndexToStart()` before `seq_clearAutomationDirty()`.

**`seq_setRunning()` restructured:**
- Stop: `seq_running = 0` (prevents TIM3 from advancing), then cleanup.
- Start: all initialization first, then `seq_running = 1` last.

Redundant `seq_clearAutomationDirty()` in stop branch removed.

---

## 13. S071 Carry-Over Plan

Documented in `S071_VOICE_MORPH_AUTOMATION_MODULATION_CLEANUP.md`:

### Part A — Per-Scene Voice-Edit Mask (A1–A10)

Storage, accessors, Autosave, bankset.bcg, filesystem, morph rebuild for a
per-Scene voice-edit fan-out mask. Currently `bank_scene_mask_voice_edit` is
a single global `uint16_t` in BankData; Part A moves it to per-Scene storage.

### Part B — Base-Independent LFO Voice-Morph Contribution (B1–B4)

Direction+depth encoding so the LFO voice-morph contribution works
independently of the base morph position. InstrumentManager polarity
encoding.

### Part C — Scene Superpage Live Display + Bugs

- C1: Live display for Scene superpage during playback
- C2: Immediate underline on held-step Scene write
- C3: Voice-edit mask boot-state cleanup

### Open Questions

- **Q-C1**: Scope of live display — full Scene superpage or subset?
- **Q-C3**: Is Part A sufficient for boot-state cleanup, or does it also
  need defensive present-mask intersection?

---

## 14. Hardware Test Fixtures

| Fixture | Purpose | Result |
|---------|---------|--------|
| SD_CARD_LSR01-FINAL | Phase 2 HCNAMES checkpoint validation | PASS — 54,972 records, zero failures |
| SD_CARD_PHASE3_OUTPUT | Phase 3 feature validation | PASS |
| SD_CARD_ATS_OFF_ON | T1 AutoSave OFF→ON re-enable | PASS |
| SD_CARD_PH4_Q1_OUTPUT2 | Q1 Scene automation overlay | PASS — 160,129 records, zero E/X errors |
| SD_CARD_PH4_AUTOMATION_MISSED | T3 automation restart | PASS |

---

## 15. Specification Reference Updates

The following specification documents were updated as part of session
closeout:

- `MODULE_INTERCHANGE_SPEC.md` — Scene automation overlay ownership, runtime
  overlay API boundaries, LED layer model, LSR selection generation.
- `PATTERN_DYNAMIC_STACK.md` — Probability gating behavior (§6.1 update),
  Scene automation target IDs and step-edit cycling.
- `AUTOSAVE.md` — Pattern generation handling, Scene automation dirty
  bitmap exclusion, deferred HCNAMES scheduler.
- `BANK_PRESET_ARCHITECTURE.md` — New document covering resident parameter
  storage, voice-edit mask, Scene mod targets, runtime overlay model.
