# Session 077 — Handoff Log

```
DATE: 2026-10-08
SESSION GOAL: Six-part implementation pass (P1–P6) on top of Session 076's
              copy/clear and feature work: background Scene region, per-track
              Scene playback, blank menu fix, Scene morph fan-out correction,
              bar-to-step copy, and PERF mode morph automation underline.
COMPLETED: All six parts implemented. P3 hardware verified (primary symptom).
           P1–P2 retest items 1.1–2.7 PASS. P3 items 3.1–3.18 PASS.
           P4/P5/P6 not yet hardware tested. P4 retest items 3.10/3.14/3.15
           cleared for re-test after fan-out correction.
VERIFIED ON HARDWARE: P3 (blank VOICE page after load: confirmed fixed).
                      P1 items 1.1–1.7 PASS. P2 items 2.1–2.7 PASS.
                      P3 items 3.1–3.18 PASS.

CHANGES THIS SESSION:
- Core/Bank/Scene/Pattern/PatternData.c: pat_background_region replaces
  pat_autosave_snapshot; pat_backgroundPoolMut() accessor
- Core/Bank/Scene/Pattern/PatternData.h: pat_backgroundPoolMut() declaration
- Core/Menu/CopyClear/copyClearService.c: static ccSvc_snapTable[128],
  CC_SNAP_OFFSET widened, ccSvc_table()/ccSvc_snapBlock() use static table
  and background pool, SNAPSHOT phase uses background pool, overlap waits
  on filesystem_patternSnapshotInUse()
- Core/Menu/CopyClear/copyClearService.h: removed CC_SCRATCH_TABLE_OFFSET
  and CC_SCRATCH_BLOCK_OFFSET, updated comments
- Core/Hardware/SD/filesystem.c: filesystem_patternSnapshotInUse()
- Core/Hardware/SD/filesystem.h: filesystem_patternSnapshotInUse() declaration
- Core/Bank/Scene/AutosaveTrace.h: AUTOSAVE_TRACE_CC_EVT_SNAPSHOT_GATE 0x51u
- Core/Sequencer/sequencer.c: seq_perTrackActive, seq_recomputePerTrackActive(),
  seq_setTrackPlayedScene(), seq_clearPerTrackOverrides(),
  seq_realignTrackToMasterClock(), seq_getTrackPlayedScene(),
  per-track region reads in seq_advanceTrackStep/queueStepAutomations/
  queueEffectStepMarker/triggerVoice/previewVoice/realignActivePatternToMasterClock/
  drainPendingAutomation/restoreAutomatedParameters/restoreAllAutomation,
  seq_setRecordingMode gated on !seq_perTrackActive
- Core/Sequencer/sequencer.h: extern seq_perTrackActive, new API declarations
- Core/Bank/Scene/Preset/presetManager.c: preset_startSingleVoiceApply(),
  preset_tickSingleVoiceApply(), preset_commitSingleVoiceSlot(),
  preset_applyPerVoiceSceneSettings(), preset_getSlotPlayedScene(),
  preset_applyInstrumentRuntimeValueForced(),
  preset_applyDeferredSceneSlotForTrigger() force path,
  preset_startDrumsetApply() clears single-voice mask,
  preset_resetAndApplyKitVoiceImage() unconditional runtime reset
- Core/Bank/Scene/Preset/presetManager.h: new declarations
- Core/Bank/Scene/Preset/presetMorphEngine.c/.h: presetMorph_writeRuntimeBaseEx()
  with force flag; presetMorph_applyVoiceNowFromScene()
- Core/DSP/Instruments/InstrumentManager.c/.h:
  instrumentManager_clearRuntimeModulationTargetsForSlot(),
  instrumentManager_captureLfoPhaseForSlot(),
  instrumentManager_resetRuntimeSlotFromScene()
- Core/DSPAudio/mixer.c: mixer_faderGains() reads preset_getSlotPlayedScene()
- Core/Hardware/frontPanel/buttonHandler.c: perf_heldVoiceMask,
  perf_voiceActionOccurred, dblclick_onPress()/dblclick_cancel(),
  PERF VOICE hold/SEQ routing/release mute toggle, processPress() feeds
  double-click detector, buttonHandler_perfRefreshHeldSceneLeds(),
  view-follows-track in handleVoiceButton()
- Core/Hardware/frontPanel/ledHandler.c: led_processSeqLedState() BEAT drain
  refreshes PERF Scene LEDs
- Core/Menu/menu.c: menu_refreshPerfSceneLeds() tempo pulse + viewed-scene blink;
  menu_residentNameDeferredFlushComplete() (P3); menu_endResidentNameScratchSession()
  guard (P3 A1); menu_triggerDeferredHcnamesFlush() rewritten (P3);
  va_searchPerfMorphMask + menu_applyPerfMarkers() (P6); PERF scan branch
  in va_scanService(); PERF extensions to repaintGeneric, serviceRuntimeWidgets,
  CGRAM retry, patternContentChanged, switchPage, automationTargetCleared;
  _Static_assert 45→46
- Core/Menu/menu.h: comment update for menu_triggerDeferredHcnamesFlush()
- Core/Menu/CopyClear/copyOps.c: ccCopy_runSceneMorph() rewritten with
  bank_sceneFanoutMask() (P4); ccCopy_requestBarToStep() new (P5);
  ccCopy_runJob() routes CC_KIND_BAR_TO_STEP (P5); label corrections (P4)
- Core/Menu/CopyClear/copyOps.h: ccCopy_requestBarToStep() declaration
- Core/Menu/CopyClear/clearOps.c: ccClear_runResetSceneMorph() rewritten
  with fan-out (P4)
- Core/Menu/CopyClear/copyClearSession.h: CC_KIND_BAR_TO_STEP (6)
- Core/Menu/CopyClear/copyClearSession.c: cc_copySeq() bar-source→step-dest
  branch (P5); flash uses CC_KIND_STEP with sourceLength * NUM_STEPS_PER_BAR
- Core/Menu/CopyClear/copyClearService.c: ccSvc_pasteGeometry() bar-to-step
  case (P5); ccSvc_pasteTriggersNow() gate accepts CC_KIND_BAR_TO_STEP
- config.h: DOUBLE_CLICK_TIMEOUT 300u

KNOWN ISSUES INTRODUCED: none new
KNOWN ISSUES RESOLVED:
- Blank VOICE page after Scene Load (P3): background .hcnames rewrite raised
  menu_storageBusy, locking all foreground interaction for ~3 s. Fixed.
- Double-click timing inversion (P2): dblclick_onPress() comparison was
  inverted; fast double-clicks treated as two singles. Fixed.
- Spurious FsErr overlay (P3 A1): re-entering Load/Save during background
  write triggered menu_endResidentNameScratchSession() → FsErr. Fixed.
- Scene morph fan-out (P4): copy/reset scene morph incorrectly paralleled
  copy/clear scene instead of fanning out like copy kit/clear fx. Fixed.
  Pre-existing §13 error in COPYCLEAR_UTILITIES.md corrected.

NEXT SESSION RECOMMENDED GOAL:
1. Hardware testing of P4–P6 and carry-over items using S077_RETEST_CHECKLIST.md.
2. Re-measure the production build.
3. Collect user's pending S075 hardware reports.
4. Agree the next feature/fix target.

BLOCKERS:
- P4 items 4.1–4.20, P5 items 5.1–5.11, P6 items 6.1–6.42 need hardware.
- Carry-over C1–C6 from earlier sessions need hardware.
- P3 items 3.10/3.14/3.15 re-cleared for P4 fan-out re-test.

CRITICAL REMINDERS FOR NEXT SESSION:
- Background .hcnames rewrite must NOT raise menu_storageBusy. The lightweight
  callback (menu_residentNameDeferredFlushComplete) is the only correct path.
- menu_endResidentNameScratchSession() must check filesystem_status() == BUSY
  before starting a write.
- Scene morph copy/reset uses bank_sceneFanoutMask() fan-out, NOT the
  copy scene/clear scene edit-mask exchange model.
- pat_background_region is shared between AutoSave snapshot, overlap paste pool,
  and future Bank Load staging. These uses must never run concurrently.
- filesystem_patternSnapshotInUse() is the gate for overlap paste pool
  availability; the retired ccSvc_ensureScratch() must not be restored.
- seq_perTrackPattern[7] per-track Scene reads are ISR-safe (single-byte
  SRAM1 on Cortex-M7). Recording is gated on !seq_perTrackActive.
- CC_SNAP_OFFSET is now 0x0FFFu (was 0x07FFu). Pool offset encoding has 12 bits.
- DOUBLE_CLICK_TIMEOUT is 300u ms. The comparison is
  (uint16_t)(dblclick_deadline - time_sysTick) < 32768u — do not reinvert.
- CC_KIND_BAR_TO_STEP (6) geometry: bar source coords (src×16), step dest
  coords (job->start directly, not ×16).
- _Static_assert for the overlay block in menu.c is now 46 bytes.
- The eleven S077 root plan documents (S077_P1_*.md through S077_P6_*.md,
  S077_RETEST_CHECKLIST.md) are superseded by this log and should be deleted
  after hardware testing results are recorded.
```

---

## 1. Session overview

Session 077 continued on `dev-ph6-cleanup` with six implementation parts
(P1–P6), each building on the S075/S076 copy/clear framework and per-track
playback system. The session ran 2026-10-08.

### Build progression

| Part | text | data | bss | Delta |
|------|-----:|-----:|----:|-------|
| S076 close | 535,976 | 416 | 426,744 | — |
| P1 | 536,040 | 416 | 427,008 | +64 text, +264 bss |
| P2 | 538,544 | 420 | 427,008 | +2,504 text, +4 data |
| P3 (with A1) | 538,696 | 420 | 427,008 | +152 text |
| P4 | 538,608 | 420 | 427,008 | −88 text |
| P5 | 538,576 | 420 | 427,008 | −32 text |
| P6 | 538,416 | 416 | 427,008 | −160 text, −4 data |

Final: flash payload 538,832 B of 753,664 B. **Headroom 214,832 B.**

BSS growth from S076: +264 B (256 B `ccSvc_snapTable[128]` + 8 B alignment).
The `pat_background_region` replaced `pat_autosave_snapshot` at the same
10,519 B — no net BSS change from that substitution.

---

## 2. P1 — Background Scene Region (17th `pat_background_region`)

### Problem

The copy/clear overlap paste path borrowed a 9 kB name buffer from the
filesystem for both the paste source table and the snapshot block data. This
coupled copy/clear to the name buffer's availability and forced the entire
buffer to be lent even when only the table or block was needed.

### Solution

1. **`pat_background_region`** (10,519 B) replaced the standalone
   `pat_autosave_snapshot` at identical size and type (`pat_scene_region_t`).
   Same three non-concurrent uses: AutoSave snapshot, overlap paste pool,
   future Bank Load staging.
2. **Static paste table**: `static uint16_t ccSvc_snapTable[NUM_STEPS]`
   (128 entries, 256 B BSS) in `copyClearService.c`. Returned by `ccSvc_table()`.
3. **Background pool**: `ccSvc_snapBlock()` now reads from
   `pat_backgroundPoolMut()` instead of the name buffer. `CC_SNAP_OFFSET`
   widened from `0x07FFu` to `0x0FFFu` (12-bit pool offset encoding).
4. **Snapshot gate**: `filesystem_patternSnapshotInUse()` returns nonzero when
   `current_op == FS_INTERNAL_OP_AUTOSAVE_PATTERN_DRAIN && op_phase >= 2u &&
   op_phase <= 6u`. The overlap path waits on this gate instead of the retired
   `ccSvc_ensureScratch()`.
5. **Name buffer scope reduced**: scratch is now only for the HCNAMES row
   remap (161 B) and the end-of-operation name write. Removed
   `CC_SCRATCH_TABLE_OFFSET` and `CC_SCRATCH_BLOCK_OFFSET`.
6. **Assert retarget**: plan kept a remap assert referencing the removed
   `CC_SCRATCH_TABLE_OFFSET`; retargeted to `FS_NAME_SCRATCH_BYTES`.
7. **Trace**: new `AUTOSAVE_TRACE_CC_EVT_SNAPSHOT_GATE 0x51u` records each
   gate wait/pass.

### Files changed (P1)

| File | What changed |
|------|-------------|
| `Core/Bank/Scene/Pattern/PatternData.c` | `pat_background_region` replaces `pat_autosave_snapshot`; `pat_snapshotScene()` / `pat_autosaveSnapshot()` operate on it; `pat_backgroundPoolMut()` |
| `Core/Bank/Scene/Pattern/PatternData.h` | `pat_backgroundPoolMut()` declaration |
| `Core/Menu/CopyClear/copyClearService.c` | `ccSvc_snapTable[128]` static; `ccSvc_table()` / `ccSvc_snapBlock()` use new storage; SNAPSHOT phase writes to background pool; overlap waits on `filesystem_patternSnapshotInUse()` |
| `Core/Menu/CopyClear/copyClearService.h` | Removed `CC_SCRATCH_TABLE_OFFSET`, `CC_SCRATCH_BLOCK_OFFSET`; comment updates |
| `Core/Hardware/SD/filesystem.c` | `filesystem_patternSnapshotInUse()` |
| `Core/Hardware/SD/filesystem.h` | Declaration |
| `Core/Bank/Scene/AutosaveTrace.h` | `AUTOSAVE_TRACE_CC_EVT_SNAPSHOT_GATE 0x51u` |

### Deviations from plan (P1)

1. Assert retarget to `FS_NAME_SCRATCH_BYTES` (plan referenced removed define).
2. Trace shape: one record per episode, not per-tick.
3. BSS +264 vs plan +256 (8 B alignment padding).

---

## 3. P2 — Per-Track Scene Playback

### Problem

In standard LXR-02 operation, selecting a Scene switches all 7 tracks at
once. Per-track Scene assignment lets the user hold a PERF VOICE button and
press a SEQ button to assign individual tracks to different Scenes, enabling
hybrid pattern layering during live performance.

### Solution (50 changes across 16 files)

**Phase 1 — Sequencer core:**

- `seq_perTrackPattern[7]` (`pat_scene_region_t*`): per-track region pointer,
  defaulting to the active Scene's region.
- `seq_perTrackActive` flag and `seq_recomputePerTrackActive()` helper.
- `seq_setTrackPlayedScene()`, `seq_clearPerTrackOverrides()`,
  `seq_realignTrackToMasterClock()`, `seq_getTrackPlayedScene()` APIs.
- `seq_advanceTrackStep()` reads from `seq_perTrackPattern[track]` for
  region/specials/step-active/erase.
- `seq_queueStepAutomations()` reads played Scene.
- `seq_queueEffectStepMarker()` gained effect-type gate via
  `seq_trackEffectTypeMatchesActive()`.
- `seq_triggerVoice()` / `seq_previewVoice()` read played Scene MIDI.
- `seq_realignActivePatternToMasterClock()` per-track region.
- `seq_drainPendingAutomation()` per-track instrument + effect type gate.
- `seq_restoreAutomatedParameters()` / `seq_restoreAllAutomation()` per-track.
- `seq_setRecordingMode()` gated on `!seq_perTrackActive`.
- Coalesce in `seq_selectActivePattern`, `seq_alignActivePatternToScene`,
  `seq_handleMasterBoundary`, `seq_init`.

**Phase 2 — Preset/instrument runtime:**

- `preset_startSingleVoiceApply()` / `preset_tickSingleVoiceApply()` /
  `preset_commitSingleVoiceSlot()`: factor single-voice instrument apply from
  the drumset worker with supersede coordination (a pending drumset apply
  clears the single-voice mask; a single-voice request while drumset is
  draining waits).
- `preset_applyPerVoiceSceneSettings()`: MIDI, routing, fader, FX send for
  one voice from a specified Scene.
- `preset_getSlotPlayedScene()`: returns the played Scene index for a slot.
- `preset_applyInstrumentRuntimeValueForced()`: force-write bypassing the
  automation hold guard.
- `preset_applyDeferredSceneSlotForTrigger()` force path.
- `preset_startDrumsetApply()` clears single-voice mask.
- `preset_resetAndApplyKitVoiceImage()` unconditional runtime reset from Scene.
- `presetMorph_writeRuntimeBaseEx()` with force flag.
- `presetMorph_applyVoiceNowFromScene()`.
- `instrumentManager_clearRuntimeModulationTargetsForSlot()`.
- `instrumentManager_captureLfoPhaseForSlot()`.
- `instrumentManager_resetRuntimeSlotFromScene()`.

**Phase 3 — Front-panel gestures:**

- `perf_heldVoiceMask` / `perf_voiceActionOccurred` in `buttonHandler.c`.
- `dblclick_onPress()` / `dblclick_cancel()`: general-purpose double-click
  detector.
- PERF VOICE press arms hold; PERF SEQ routing to
  `buttonHandler_perfVoiceHeldSeqPressed()` or
  `buttonHandler_perfScenePressed()`.
- PERF VOICE release toggles mute if no action occurred.
- `processPress()` feeds double-click detector, cancels on non-SEQ/non-VOICE.

**Phase 4 — LED:**

- `buttonHandler_perfRefreshHeldSceneLeds()`.
- `led_processSeqLedState()` BEAT drain refreshes PERF Scene LEDs.
- `menu_refreshPerfSceneLeds()` tempo pulse + viewed-scene blink.

**Phase 5 — UI:**

- View-follows-track in `handleVoiceButton()`.

**Architecture deviation from plan:** no flat mixer mirrors; FX send/fader/
morph read live from SceneData via `preset_getSlotPlayedScene()`.

**Known limitation:** Scene-namespace LFO/velocity target tokens resolve
through the active Scene in InstrumentManager, not the played Scene.

### ISR safety

Per-track Scene assignment writes are single-byte SRAM1 reads/writes, which
are atomic on Cortex-M7. The foreground writes `seq_perTrackPattern[track]`
(a pointer, but on a 32-bit aligned address with single-word store); the ISR
reads it. No explicit critical section is needed.

### Bug found and fixed (P2)

`dblclick_onPress()` timing comparison on `buttonHandler.c` line 432:

**Before (wrong):** `(uint16_t)(time_sysTick - dblclick_deadline) < 32768u`
— TRUE when deadline has passed (window expired). Effect: fast double-clicks
treated as two singles; slow presses falsely triggered double-click.

**After (correct):** `(uint16_t)(dblclick_deadline - time_sysTick) < 32768u`
— TRUE when `time_sysTick` is still before `dblclick_deadline` (still within
the window).

### Files changed (P2)

| File | What changed |
|------|-------------|
| `Core/Sequencer/sequencer.c` | All Phase 1 changes |
| `Core/Sequencer/sequencer.h` | `extern seq_perTrackActive`, API declarations |
| `Core/Bank/Scene/Preset/presetManager.c` | All Phase 2 preset changes |
| `Core/Bank/Scene/Preset/presetManager.h` | Declarations |
| `Core/Bank/Scene/Preset/presetMorphEngine.c/.h` | `writeRuntimeBaseEx()`, `applyVoiceNowFromScene()` |
| `Core/DSP/Instruments/InstrumentManager.c/.h` | `clearRuntimeModulationTargetsForSlot()`, `captureLfoPhaseForSlot()`, `resetRuntimeSlotFromScene()` |
| `Core/DSPAudio/mixer.c` | `mixer_faderGains()` uses `preset_getSlotPlayedScene()` |
| `Core/Hardware/frontPanel/buttonHandler.c` | All Phase 3 + Phase 4 + Phase 5 changes |
| `Core/Hardware/frontPanel/ledHandler.c` | Phase 4 LED drain |
| `Core/Menu/menu.c` | Phase 4 `menu_refreshPerfSceneLeds()` |
| `config.h` | `DOUBLE_CLICK_TIMEOUT 300u` |

Build: text=538,544, data=420, bss=427,008. +8 B storage fits alignment
slack. Flash 538,964 / 753,664 (headroom 214,700).

---

## 4. P3 — Blank Menu After Scene Load

### Root cause

Background `.hcnames` rewrite after a Scene Load called
`menu_endResidentNameScratchSession()`, which raised `menu_storageBusy`.
While `menu_storageBusy` is set, all foreground interaction is gated:
mode switches, encoder, pots, widgets, and copy/clear. The background
write takes ~3 seconds, so the VOICE page appeared blank and unresponsive
for that duration.

### Fix

1. **New callback `menu_residentNameDeferredFlushComplete()`**: reads
   `filesystem_status()`, acks, clears mask on success, shows error overlay on
   failure. Never touches `menu_storageBusy`.
2. **Rewritten `menu_triggerDeferredHcnamesFlush()`**: calls
   `filesystem_requestUpdateResidentKitNames()` directly with the new callback.
3. **Comment corrections** in `filesystem.h` (lines 903–920, 946–962) and
   `filesystem.c`: "generalized cache" → "dedicated hcnames_name_mirror".

### A1 fix (spurious FsErr overlay)

After the P3 fix removed `menu_storageBusy` from the background write path,
re-entering Load/Save during the write and crossing a name-session boundary
would trigger `menu_endResidentNameScratchSession()` which sets
`menu_storageBusy=1` and tries `filesystem_requestUpdateResidentKitNames()`
(refused because busy), then shows FsErr overlay.

**Fix:** `menu_endResidentNameScratchSession()` gained a guard: if
`filesystem_status() == FS_STATUS_BUSY`, return 0 with no overlay.

Build: text=538,696 (A1: +56 B vs P3 base), data=420, bss=427,008.
No RAM delta. Flash 539,116.

### Hardware verification

Primary symptom (blank VOICE page after load) confirmed fixed.

### Files changed (P3)

| File | What changed |
|------|-------------|
| `Core/Menu/menu.c` | `menu_residentNameDeferredFlushComplete()`, rewritten `menu_triggerDeferredHcnamesFlush()`, `menu_endResidentNameScratchSession()` guard |
| `Core/Menu/menu.h` | Comment update |
| `Core/Hardware/SD/filesystem.h` | 2 comment corrections |
| `Core/Hardware/SD/filesystem.c` | 2 comment corrections |

---

## 5. P4 — Scene Morph Fan-Out Correction

### Problem

`ccCopy_runSceneMorph()` and `ccClear_runResetSceneMorph()` incorrectly
paralleled `copy scene` / `clear scene`, which exchange/reset the edit mask
entry. Morph endpoints are Scene-child data and should fan out like
`copy kit` / `clear fx` through `bank_sceneFanoutMask()`.

### Fix

1. **`ccCopy_runSceneMorph()` rewritten:** uses `bank_sceneFanoutMask()` loop,
   per-member Effect type check. Each fan-out member copies the source Scene's
   morph endpoint for matching slots.
2. **`ccClear_runResetSceneMorph()` rewritten:** uses fan-out; each member
   uses its OWN Normal endpoints for reset. `effects_resetMorphToNormalSingle(m)`
   per member (not the fan-out variant — avoids double fan-out).
3. **Label corrections:** `"morph"` → `"inst -> morph"`, `"scene morph"` →
   `"scene -> morph"`.
4. **Bonus fix:** pre-existing §13 error in `COPYCLEAR_UTILITIES.md` in the
   `clear reset fx morph` row was corrected.

### Files changed (P4)

| File | What changed |
|------|-------------|
| `Core/Menu/CopyClear/copyOps.c` | `ccCopy_runSceneMorph()` rewritten, label changes |
| `Core/Menu/CopyClear/clearOps.c` | `ccClear_runResetSceneMorph()` rewritten |
| `Core/Menu/CopyClear/copyOps.h` | Comment updates |
| `Core/Menu/CopyClear/clearOps.h` | Comment updates |
| `Core/Menu/CopyClear/copyClearSession.c` | Comment updates |
| `Core/DSP/Effects/EffectsManager.h/.c` | Comment updates |
| `Core/Bank/Scene/Preset/presetManager.h/.c` | Comment updates |

Build: text=538,608 (−88 B vs P3), data=420, bss=427,008. No RAM change.

---

## 6. P5 — Bar-to-Step Copy

### Problem

Users expect to be able to copy a bar source and paste it at a step
destination (cross-kind paste). The existing paste path assumed source and
destination share the same coordinate system.

### Solution

1. **`CC_KIND_BAR_TO_STEP` (6)** added to `cc_kind_t` in `copyClearSession.h`.
2. **`cc_copySeq()` in `copyClearSession.c`:** gained `else if
   (cc_source.kind == CC_KIND_BAR)` branch calling
   `ccCopy_requestBarToStep()`. Flash uses `CC_KIND_STEP` with count
   `sourceLength * NUM_STEPS_PER_BAR`.
3. **`ccCopy_requestBarToStep()` in `copyOps.c`:** sets
   `job.kind = CC_KIND_BAR_TO_STEP`, no identical-paste check (cross-kind is
   never identical).
4. **`ccCopy_runJob()` in `copyOps.c`:** routes `CC_KIND_BAR_TO_STEP` to
   `ccSvc_runPatternPaste()`.
5. **`ccSvc_pasteGeometry()` in `copyClearService.c`:** new case — source uses
   bar coords (`src × 16`), destination uses `job->start` directly (not `× 16`).
6. **`ccSvc_pasteTriggersNow()` in `copyClearService.c`:** gate accepts
   `CC_KIND_BAR_TO_STEP`.

### Files changed (P5)

| File | What changed |
|------|-------------|
| `Core/Menu/CopyClear/copyClearSession.h` | `CC_KIND_BAR_TO_STEP` (6) |
| `Core/Menu/CopyClear/copyClearSession.c` | `cc_copySeq()` bar→step branch |
| `Core/Menu/CopyClear/copyOps.c` | `ccCopy_requestBarToStep()`, routing |
| `Core/Menu/CopyClear/copyOps.h` | Declaration |
| `Core/Menu/CopyClear/copyClearService.c` | `ccSvc_pasteGeometry()` new case |
| `Core/Menu/CopyClear/copyClearService.h` | Comment updates |

Build: text=538,576 (−32 B vs P4), data=420, bss=427,008. No RAM change.

---

## 7. P6 — PERF Mode Morph Automation Underline

### Problem

The automation underline pipeline (scan → classify → render via CGRAM markers)
excluded the PERF page because:
1. Scan was not triggered on PERF page entry.
2. The marker write functions guarded against non-VOICE/EFFECT pages.
3. PERF cells are `MENU_CELL_STATIC` (no underline slot).
4. `menu_patternContentChanged()` did not restart scan on PERF.

### Solution (11 changes in `menu.c`)

1. **New state:** `#define VA_PERF_MORPH_EFFECT_BIT 0x40u` + `static uint8_t
   va_searchPerfMorphMask = 0u` (bits 0–5 per-voice morph, bit 6 effect morph).
2. **`va_searchRestart()`** clears new mask, starts track at 0 for PERF.
3. **`va_scanService()` PERF branch:** `perf_page` variable,
   context-mismatch guard relaxed, PERF classification (voice morph per-slot
   bit, effect morph bit 6), multi-track advance for PERF.
4. **New `menu_applyPerfMarkers()`:** walks 4 visible compact-view cells;
   col 0 skipped; cols 1–6 check pattern bits; col 7 checks pattern bit OR
   `menuEffects_cellSeqLocked()` with synthetic cell.
5. **`menu_repaintGeneric()`** tail calls `menu_applyPerfMarkers()`.
6. **`menu_serviceRuntimeWidgets()`** PERF scan branch.
7. **CGRAM retry guard** accepts `PERFORMANCE_PAGE`.
8. **`menu_patternContentChanged()`** includes `PERFORMANCE_PAGE`.
9. **`menu_switchPage()` PERF entry** calls `va_searchRestart()`.
10. **`va_scanService()` comment** updated with PERF bullet.
11. **`menu_automationTargetCleared()` PERF branch** clears bits on pot-clear.

### Deviations from plan (P6)

- `_Static_assert` 45 → 46 (new mask byte in overlay block).
- Overlay block comment updated.
- Forward declaration added.
- Additional comment updates.

### Files changed (P6)

| File | What changed |
|------|-------------|
| `Core/Menu/menu.c` | All 11 changes above |

Build: text=538,416 (−160 B vs P5, compiler inlining shift), data=416,
bss=427,008. +1 B fits alignment.

---

## 8. Retest status

### Completed

| Item range | Part | Result |
|-----------|------|--------|
| 1.1–1.7 | P1 | all PASS |
| 2.1–2.7 | P2 | all PASS |
| 3.1–3.18 | P3 | all PASS |
| 3.10, 3.14, 3.15 | P4 re-cleared | blank (awaiting P4 fan-out re-test) |

### Pending hardware verification

| Item range | Part | Notes |
|-----------|------|-------|
| 4.1–4.20 | P4 | Scene morph fan-out |
| 5.1–5.11 | P5 | Bar-to-step copy |
| 6.1–6.42 | P6 | PERF morph underline |
| C1–C6 | Carry-over | From earlier sessions |

---

## 9. Specification reference updates

The following specification documents were updated during or at the close of
this session to preserve all implementation details:

- **`COPYCLEAR_UTILITIES.md`** — P1 (§11.7, §12.5, §15.2, §15.3, §17, §18),
  P4 (§6.1, §6.5, §7.2, §10, §13, §20), P5 (§6.3, §11.3, §12.5, §20).
- **`PATTERN_DYNAMIC_STACK.md`** — P1 (§1 background region).
- **`STORAGE_SRAM_MANIFEST.md`** — P1 (§5, §8.2, §11).
- **`BANK_PRESET_ARCHITECTURE.md`** — updated at session close for P2
  per-track playback, P3 HCNAMES fix, P6 underline content.
- **`AUTOSAVE.md`** — updated at session close for P1 snapshot gate.

---

## 10. Disposable documents

The following root documents are superseded by this handoff log and the
specification reference updates. They should be deleted after hardware
testing results are recorded:

- `S077_P1_BG_SCENE_COPY_SELF_REPLACE.md`
- `S077_P1_IMPLEMENTATION.md`
- `S077_P2_IMPLEMENTATION.md`
- `S077_P2_PER_TRACK_PLAYBACK.md`
- `S077_P3_BLANK_MENU_AFTER_SCN_LOAD.md`
- `S077_P3_IMPLEMENTATION.md`
- `S077_P4_IMPLEMENTATION.md`
- `S077_P4_SCN_MORPH_FAN_CORRECTION.md`
- `S077_P5_BAR_TO_STEP_COPY.md`
- `S077_P6_PERF_MODE_MORPH_UNDERLINE.md`
- `S077_RETEST_CHECKLIST.md`

---

## 11. Carry-forward items

### Still open from S077

- P4/P5/P6 hardware testing.
- P3 items 3.10/3.14/3.15 re-test after P4 fan-out correction.
- Carry-over C1–C6.

### Still open from S076

- S075 carry-over: SHIFT+TRACK overlay re-test; combined case list
  (`COPYCLEAR_UTILITIES.md` §16); F3 edge cases; card preparation; production
  build re-measurement.

### Still open from S075

- Pattern Load fan-out tear (not fixed).
- O1: Settings Load per-voice Morph equalisation.
- F4: trace ring drops lifecycle records during dirty bursts.
- Underline limitations (live erase race, deferred clear race).

### Still open from S074

- Unexplained boot timeout.
- `cpu` widget with `cmp` on.
- CrumpBit minimum-share run.
- BC11.
- Saturator α 0.35 A/B.
- Stale comments.

### Known limitation (P2)

Scene-namespace LFO/velocity target tokens resolve through the active Scene
in InstrumentManager, not the played Scene. This is accepted for the current
implementation and does not cause incorrect behavior in the common case
(per-track playback with the same instrument types across Scenes).
