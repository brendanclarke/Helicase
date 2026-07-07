# PAT_8_BAR_SINGLE_AUDIT.md
# Single-Scene 8-Bar Pattern Reformat & Menu Representation Plan

## Goal

Replace the legacy `8 patterns × 7 tracks × 128 sub-steps` data model with a
**single Scene** containing one pattern of **128 flat steps** per track, 8
visible 16-step bars navigated by SELECT buttons and BAR <> buttons. No
sub-steps. No microtiming for now (to be added later). The 16 SEQ
buttons/LEDs represent the 16 steps of the currently visible bar.

This is a Phase 2 / Phase 3 bridge pass. It introduces the single-pattern
bar-nav model without yet building the dynamic event pool from SCOPING_TARGETS
Phase 3. The storage layout is deliberately designed so Phase 3 can replace
the inner `Step[128]` array with a pool address array without touching the
bar-navigation or menu-button contracts.

---

## Architecture Impact Summary

| Layer | Impact |
|---|---|
| `PatternData.h/.c` | Reduce from 8 patterns to 1; redefine NUM_STEPS as flat 128-step array; remove `pat_subStepPattern` nesting; remove `TempPattern` staging if safe |
| `sequencer.c/.h` | Update step index math to treat 128 steps as flat (no sub-step arithmetic); track bar counter |
| `filesystem.c` | Save/load first pattern only; write zeros for patterns 2–8 on save; on load read first pattern, discard rest |
| `buttonHandler.c` | SELECT buttons → bar select (0–7); BAR1/BAR2 → bar dec/inc; PERF mode SELECT1 only active |
| `ledHandler.c/.h` | SEQ LEDs = steps in current bar; SELECT LEDs = bar indicator; flash bar SELECT on bar change in VOICE mode |
| `menu.c` | Expose `menu_currentBar` for bar-aware display; update sub-page references |
| `copyClearTools.c` | Update copy/clear helpers to work with flat step index |
| `PatternData.h` API | Update helpers that encode `step = mainStep * 8 + subStep` to use flat step index |

---

## Open Questions (resolve before implementation)

> [!IMPORTANT]
> **Q1 — `pat_tmpPattern` staging:** The staging buffer is currently used when
> the sequencer is running and a `.pat` file is being loaded. With only 1
> pattern, the staging/commit protocol still applies if the loaded slot IS the
> active pattern (always true now). The `TempPattern` struct mirrors the old
> single-pattern slice of `PatternSet`. If we keep it we can keep the
> boundary-safe commit from `pat_commitStagedPattern()`. **Recommended: keep
> `TempPattern` and the staging path unchanged for this session; Phase 3 will
> replace them wholesale when the pool design lands.**

> [!IMPORTANT]
> **Q2 — File format version byte:** The existing `.pat` file starts with 8
> name bytes then streams all pattern data in fixed-field order. With
> `NUM_PATTERN` changing from 8 to 1, the file size shrinks 8×. Files written
> by old firmware are now too large; files written by new firmware are too small
> for old firmware. **Decision needed: bump `FS_CONTAINER_VERSION` from 2 to 3
> and treat any file with `>= 1-pattern-worth` of data as valid, zeroing
> missing patterns on load. The save path writes only pattern 0 + blanks so
> the file size stays at `1 * (NUM_TRACKS * NUM_STEPS * 7)` bytes + headers.**

> [!IMPORTANT]
> **Q3 — `pat_eraseMainStepSubSteps` + automation arming:** Both functions
> still use the `mainStep * 8 + subStep` encoding. After the reformat, "main
> step" and "step" become the same thing (1:1, no sub-step expansion). Confirm
> the automation-arm long-press on a step button still makes sense when there
> are no sub-steps — the held step button IS the automation step, with no
> SELECT sub-window. This should simplify the code rather than break it.

> [!CAUTION]
> **Q4 — `seq_nextStep()` and bar wrap:** The sequencer currently uses
> `pat_getEffectiveTrackLength()` returning 1–16 main steps and multiplies by 8
> to get sub-step range. After this change, effective length should return
> 16–128 flat steps. This is the highest-risk single change in this plan.

---

## Detailed Changes by File

---

### 1. `Core/Scene/Pattern/PatternData.h`

**File role:** Public data model and API contract for all pattern storage. All
callers use these types; changing them cascades everywhere.

#### 1a. Change `NUM_PATTERN` from 8 to 1

```c
// BEFORE — 8 selectable patterns
#define NUM_PATTERN 8

// AFTER — single scene, single pattern (the "17th scene" design from Phase 3
// will later replace this with a pool-per-scene; for now one pattern is
// enough to validate the 8-bar bar-nav model)
#define NUM_PATTERN 1
```

**Why:** The user spec says "only one pattern available instead of 8." The 8
`SELECT` buttons will navigate 8 bars of a single pattern rather than
selecting between 8 separate patterns.

**Risk:** Every function that iterates `0..NUM_PATTERN-1` now iterates exactly
once. The filesystem serializer uses `NUM_PATTERN` in its count macros —
changing it shrinks the file format. All eight old-pattern slots are gone from
RAM. `pat_patternValid()` now only accepts 0. See filesystem changes below.

**Inputs/outputs:** `NUM_PATTERN` is used as an array dimension in
`PatternSet`, `TempPattern`, and all `pat_*` API parameters named `pattern`.

**Affiliates:** `filesystem.c` (`FS_PATTERN_STEP_COUNT`, `FS_PATTERN_MAIN_COUNT`,
`FS_PATTERN_SETTINGS_COUNT`, `FS_PATTERN_LENGTH_COUNT` macros all recompute
automatically from `NUM_PATTERN * ...`), `sequencer.c`
(`seq_determineNextPattern()`, `seq_activePattern`, `seq_pendingPattern`),
`buttonHandler.c` (PERF mode pattern-select SELECT handling),
`ledHandler.c` (`led_notifyPatternChanged()`).

---

#### 1b. Add `NUM_BARS` and `NUM_STEPS_PER_BAR` constants

```c
// NEW — 8 bars × 16 steps = 128 flat steps. These replace the old
// "main step" (1..16) / "sub-step" (0..7 within main step) encoding.
// NUM_STEPS remains 128 — the flat total.
#define NUM_BARS          8u   // number of bars in a single pattern
#define NUM_STEPS_PER_BAR 16u  // steps per bar (matches SEQ button count)
// NUM_STEPS stays 128 — total flat steps per track
```

**Why:** Named constants avoid magic numbers in the bar-nav logic and document
the intent even before microtiming slots are added.

**Risk:** Low — purely additive.

**Affiliates:** `buttonHandler.c` uses these in bar selection math;
`ledHandler.c` uses them to compute which 16 steps to show on the SEQ LEDs.

---

#### 1c. Remove `pat_subStepPattern` nesting; keep flat `pat_steps[NUM_TRACKS][NUM_STEPS]`

The current `PatternSetStruct` is:
```c
Step pat_subStepPattern[NUM_PATTERN][NUM_TRACKS][NUM_STEPS];  // 8×7×128 Steps
uint16_t pat_mainSteps[NUM_PATTERN][NUM_TRACKS];              // 8×7 bitmasks
PatternSetting pat_patternSettings[NUM_PATTERN];              // 8 settings
LengthRotate pat_patternLengthRotate[NUM_PATTERN][NUM_TRACKS];// 8×7 length/rotate
```

After this change it becomes:
```c
// RENAMED: pat_subStepPattern → pat_steps to drop the misleading "sub-step"
// label. Dimensionally: [1][7][128] — pattern 0 only, 7 tracks, 128 flat steps.
Step pat_steps[NUM_PATTERN][NUM_TRACKS][NUM_STEPS];

// pat_mainSteps is REMOVED. In the legacy model it was a 16-bit bitmask over
// 16 "main steps", where bit N=1 meant "the Nth group of 8 sub-steps has at
// least one active sub-step". In the new flat model each Step already carries
// its own active bit in step->volume bit 7 (STEP_ACTIVE_MASK). There are no
// "main steps" to aggregate over. The 16 SEQ buttons directly address the 16
// steps of the current bar; their active state is read from step->volume bit 7.
// REMOVAL cascades: filesystem no longer saves/loads a main-steps section;
// ledHandler reads step active bits directly; sequencer uses step active bits.

// pat_patternSettings survives but is scalar now that NUM_PATTERN=1.
PatternSetting pat_patternSettings[NUM_PATTERN];  // 1 setting

// LengthRotate survives for per-track length (useful for variable-length tracks).
LengthRotate pat_patternLengthRotate[NUM_PATTERN][NUM_TRACKS]; // 1×7
```

**Why:** `pat_mainSteps` was an aggregated bitmask over 8-sub-step "main step"
groups. With no sub-steps the aggregation layer is unnecessary. Each step's
`volume & STEP_ACTIVE_MASK` already encodes whether that step fires.

**Risk — HIGH:** This is the largest structural change. All of the following
must update:
- `pat_mainStepsPtr()` → **REMOVE** from public API
- `pat_isMainStepActive()` → **REMOVE** from public API
- `pat_setMainStep()` → **REMOVE** from public API
- `pat_setMainStepsRaw()` → **REMOVE** from public API
- `pat_toggleMainStep()` → **REMOVE** from public API
- `led_updatePatternTrack()` currently reads `pat_isMainStepActive()` for the
  STEP1..16 row. Must be replaced with direct `pat_isStepActive()` per bar slot.
- `buttonHandler.c` `buttonHandler_setRemoveStep()` calls `pat_toggleMainStep()`
  — must change to `pat_toggleStep()` on the bar-relative step.
- `filesystem.c` main-steps serialization section (phase 4 in save/load tick)
  — **REMOVE** those phases.

**Accessor/client list:**
`ledHandler.c` (`led_updatePatternTrack`, `led_updateRecordedMainStep`),
`buttonHandler.c` (`buttonHandler_setRemoveStep`),
`filesystem.c` (save phase 4, load phase 4),
`copyClearTools.c` (`pat_setMainStepsRaw` calls in clear logic).

---

#### 1d. Rename API functions to drop "SubStep" / "MainStep" terminology

| Old name | New name | Reason |
|---|---|---|
| `pat_subStepPattern` (field) | `pat_steps` | No sub-steps |
| `pat_mainStepsPtr` | **REMOVED** | Main-step bitmask gone |
| `pat_isMainStepActive` | **REMOVED** | Replaced by `pat_isStepActive` on bar steps |
| `pat_setMainStep` | **REMOVED** | Replaced by `pat_toggleStep` |
| `pat_toggleMainStep` | **REMOVED** | Replaced by `pat_toggleStep` |
| `pat_setMainStepsRaw` | **REMOVED** | No caller after clear refactor |
| `pat_eraseMainStepSubSteps` | `pat_eraseBarSteps` | Bar replaces main-step |
| `pat_recordNote` (step param was sub-step) | same name, step is flat 0..127 | Clarify comment |

**Why:** Keeping "SubStep" and "MainStep" names after the concept disappears is
a maintenance hazard. The rename pass is mechanical and every compiler error is
a client that needs updating.

---

#### 1e. Add bar-navigation state to PatternData (or Menu)

```c
// NEW — which of the 8 bars is currently displayed/edited.
// 0 = steps 0..15, 1 = steps 16..31, ..., 7 = steps 112..127.
// This is UI/edit state so it could live in Menu. However, PatternData
// step-access helpers (pat_applyStepToMenu, led_updatePatternTrack) need it
// to know which 16 steps to display on SEQ LEDs. Keeping it in PatternData
// avoids PatternData importing Menu.
// Alternative: keep in menu.c as menu_currentBar. Menu is already the
// boundary for UI state; led_ and buttonHandler_ read menu_currentBar.
// DECISION: place in menu.c as extern uint8_t menu_currentBar.
```

This is resolved in the menu.c section below.

---

#### 1f. Update `TempPattern` struct to match new `PatternSet`

```c
// TempPattern mirrors a single-pattern slice for the active-pattern staging
// buffer. After removing pat_mainSteps the struct becomes:
typedef struct TempPatternStruct {
    Step pat_steps[NUM_TRACKS][NUM_STEPS];
    PatternSetting pat_patternSettings;
    LengthRotate pat_patternLengthRotate[NUM_TRACKS];
    // pat_mainSteps removed — see §1c rationale above
} TempPattern;
```

**Risk:** `pat_commitStagedPattern()` copies fields individually; removing
`pat_mainSteps` means removing those copy lines. Filesystem load path writes
to `pat_tmpPattern.pat_mainSteps` — those writes must also be removed.

---

### 2. `Core/Scene/Pattern/PatternData.c`

#### 2a. Update `pat_init()` and `pat_resetStep()`

`pat_init()` currently calls `memset` over `pat_patternSet`. The struct is
smaller after the changes; `memset` + field-level init loop still works.

```c
// No change needed to pat_init() if it uses memset on the whole struct.
// However: the explicit note/prob/volume field setup loop must now iterate
// NUM_TRACKS × NUM_STEPS (128) instead of NUM_PATTERN × NUM_TRACKS × NUM_STEPS.
// With NUM_PATTERN=1 the loop body is the same; the outer loop just runs once.
```

#### 2b. Remove all `pat_mainSteps` related functions

Remove bodies of:
- `pat_mainStepsPtr()` — remove from .c and .h
- `pat_isMainStepActive()` — remove from .c and .h
- `pat_setMainStep()` — remove from .c and .h
- `pat_setMainStepsRaw()` — remove from .c and .h
- `pat_toggleMainStep()` — remove from .c and .h

Any call site still using these will produce a compiler error, which is the
desired discovery mechanism.

#### 2c. Update `pat_commitStagedPattern()` to copy only surviving fields

```c
// BEFORE: copies pat_subStepPattern, pat_mainSteps, pat_patternSettings,
//         pat_patternLengthRotate
// AFTER: copies pat_steps, pat_patternSettings, pat_patternLengthRotate
// WHY: pat_mainSteps is removed; its copy line must be deleted to avoid
//      referencing a nonexistent field and to keep the boundary-safe commit
//      correct for the new layout.
```

#### 2d. Rename `pat_eraseMainStepSubSteps` → `pat_eraseBarSteps`

```c
// Old behavior: clear all 8 sub-steps within one main step (group of 8).
// New behavior: clear all 16 steps within one bar (group of 16).
// Input: pattern, track, bar (0..7). Output: steps bar*16 .. bar*16+15 reset.
// Caller: sequencer live-erase path. Risk: sequencer must pass bar number
// (step / 16) rather than main-step number (step / 8).
void pat_eraseBarSteps(uint8_t pattern, uint8_t track, uint8_t bar);
```

#### 2e. Update `pat_stepValid()` range check

```c
// pat_stepValid stays checking 0..NUM_STEPS-1 = 0..127. No change needed.
// pat_patternValid now only accepts 0. Change: return (pattern == 0).
// Why: NUM_PATTERN=1 means only pattern 0 is valid. This will cause compiler
// warnings / runtime rejections any time old code passes 1..7.
```

---

### 3. `Core/Sequencer/sequencer.c`

#### 3a. Remove sub-step arithmetic from `seq_stepIndex[]`

The current sequencer tracks `seq_stepIndex[track]` as a sub-step index
(0..127) derived from main-step × 8 + sub-step. After this change steps are
still 0..127 flat — **no arithmetic change needed in the index itself**. The
existing range 0..127 already covers the flat 128 steps.

The change is in **what the step index means conceptually**: it now directly
addresses `pat_patternSet.pat_steps[0][track][step_index]` rather than going
through a main-step / sub-step decomposition. The decomposition only appeared
in two places:

1. `pat_eraseMainStepSubSteps()` → replaced by `pat_eraseBarSteps()` with
   `bar = seq_stepIndex[track] / NUM_STEPS_PER_BAR`.

2. `led_updateRecordedMainStep()` — this helper computed
   `mainStep = subStep / 8` to decide which STEP LED to update. After the
   reformat, "which SEQ LED to light" is `step % NUM_STEPS_PER_BAR` (0..15
   within the current bar). See ledHandler changes.

#### 3b. Update `pat_getEffectiveTrackLength()` caller in sequencer

```c
// BEFORE: len = pat_getEffectiveTrackLength(pattern, track);
//         len *= 8;  // convert main-steps to sub-steps for modular arithmetic
// AFTER: len = pat_getEffectiveTrackLength(pattern, track);
//         // pat_getEffectiveTrackLength now returns flat step count (16..128)
//         // because the stored length nibble is reinterpreted (see PatternData)
//         // No ×8 multiplication needed.
// WHY: previously 1 main step = 8 sub-steps, so effective length in sub-steps
//      was mainSteps * 8. Now 1 "step" = 1 step; length is already in steps.
// RISK: This is the most coupled change. seq_triggerNextMasterStep() and
//       external sync math multiply stepSize * 8 in similar ways; audit all
//       sites that do (len * 8) or (stepSize * 8).
```

> [!WARNING]
> **Search for all `* 8` multiplications in sequencer.c that relate to step/length.**
> There are at least three: in `seq_offsetTrackStepIndexForRotation()`, in
> `seq_triggerNextMasterStep()`, and in `seq_calcDeltaT()` comments. Each must
> be audited. Some multiply conceptual main-steps by 8; those multiplications
> disappear. Others multiply MIDI PPQ ratios and are unrelated — do not touch.

#### 3c. Update `seq_determineNextPattern()`

```c
// This function reads pat_getPatternChangeBar() and pat_getPatternNext() to
// decide when to chain to a new pattern. With NUM_PATTERN=1 there is no
// pattern to chain to — seq_pendingPattern is always 0.
// SIMPLIFICATION: Remove the random-next resolution (PAT_NEXT_RANDOM,
// PAT_NEXT_RANDOM_PREV) for now. seq_determineNextPattern() can be gutted to
// always return 0. Leave the function stub so Phase 3 can wire scene-switching
// through the same return-value contract.
// WHY: Removing random pattern selection is justified because there is only one
//      pattern. The function shell must remain so its call site in seq_nextStep()
//      needs no structural change.
```

#### 3d. Add bar tracking to sequencer (lightweight)

```c
// NEW: uint8_t seq_currentBar = 0;  — which 16-step bar is currently playing
// Updated in seq_nextStep() when seq_masterStepCnt rolls over a bar boundary:
//   seq_currentBar = (seq_masterStepCnt / NUM_STEPS_PER_BAR) % NUM_BARS;
// WHY: ledHandler needs to know which bar is currently playing so the chase
//      LED shows the correct SEQ button (step within bar). The menu also needs
//      this to highlight the current-bar SELECT LED.
// RISK: This is a new global visible to ledHandler. Export it or route via
//       a getter. Prefer a getter to avoid accidental writes.
uint8_t seq_getCurrentBar(void);  // returns seq_currentBar
```

---

### 4. `Core/Hardware/SD/filesystem.c`

#### 4a. Remove `FS_PATTERN_MAIN_COUNT` / phase 4 (main steps) from save and load

```c
// BEFORE: save phases 3→4→5→6→7 = steps, main-steps, settings, shuffle, lengths
// AFTER:  save phases 3→4→5→6   = steps, settings, shuffle, lengths
//         (main-steps phase removed)
// WHY: pat_mainSteps is removed from PatternSet. There is no data to write.
// RISK: File format changes. Any OLD firmware trying to read a new file will
//       fail because expected bytes are missing from the main-steps region.
//       Bump FS_CONTAINER_VERSION to 3 as a breaking-format marker.
// LOAD: Symmetrically, remove load phase 4 (main-steps). Old files that still
//       have main-step bytes will be silently skipped because the load now
//       reads only NUM_TRACKS × NUM_STEPS × 7 step bytes (for 1 pattern) then
//       expects pattern settings. If an old 8-pattern file is loaded, the
//       function reads the first pattern's steps, then tries to read settings
//       from the offset that was previously "pattern 1 steps". The layout
//       mismatch means corrupt-but-detectable data. The version byte is the
//       correct gate.
```

#### 4b. On save of `.pat`, `.prf`, `.all`: write zeros for patterns 2–8

```c
// The user spec says: "Pattern/all/prf saving should also be updated to also
// write just the first pattern and write the rest blank."
// With NUM_PATTERN=1 there are no "patterns 2-8" in RAM. The save loop now
// only iterates pattern 0. Old file readers expecting 8 patterns will read
// pattern 0 correctly and get zeros for 1..7 IF we pad. However, since we are
// bumping the version number, old readers should reject the file entirely.
// RECOMMENDATION: Do not pad. Write exactly what pattern 0 contains.
//                 The version byte protects old readers.
// WHY NOT PAD: Padding 7 × 7 × 128 × 7 bytes = 43,904 zero bytes is
//              ~43KB of SD writes. On a 127kB/s async SPI path that's 340ms
//              of extra write time per save. Not acceptable for a pattern save.
//              Document the non-padding rationale in the version check comment.
```

#### 4c. On load of `.pat`, `.prf`, `.all`: read only the first pattern

```c
// With NUM_PATTERN=1, FS_PATTERN_STEP_COUNT = NUM_TRACKS * 1 * NUM_STEPS = 896.
// The load loop now reads exactly 896 steps (6,272 bytes). A new-format file
// is exactly that size. An old-format file has 8× that (50,176 bytes) of step
// data. The version check at file open should detect old files and abort with
// FS_STATUS_ERROR rather than silently loading 1/8th of the pattern data.
// IMPLEMENTATION:
//   In the file header, after the 8-byte name, write/read a 1-byte version
//   field. On load, if version != FS_CONTAINER_VERSION (3) then close and
//   return FS_STATUS_ERROR with a recognizable status so the menu can show
//   "Format?" for 2 seconds.
// WHY: A silent partial load is worse than a clean error. The user can always
//      clear the pattern and re-save to update the file format.
```

#### 4d. Eliminate `op_loaded_active_pattern_running` staging flag complexity

```c
// CURRENT: if (op_loaded_active_pattern_running && pattern == seq_activePattern)
//              use staging buffer; else use live storage.
// With NUM_PATTERN=1, seq_activePattern is always 0. The condition
// `pattern == seq_activePattern` is always true when pattern==0.
// SIMPLIFICATION: replace the condition with a single flag:
//   op_loaded_active_pattern_running = seq_isRunning();
// The logic becomes: if running, load into staging; always. This removes a
// code path and is semantically identical given only one pattern exists.
// WHY: Eliminates the pattern-equality comparison that no longer differentiates.
// RISK: If for any future reason a background pattern is loaded while the
//       sequencer is stopped, the staging buffer is still used — which is
//       harmless (the commit just happens immediately at pattern boundary which
//       fires on next start).
```

---

### 5. `Core/Hardware/frontPanel/buttonHandler.c`

This file owns all button-press dispatch. The bar-navigation changes live here.

#### 5a. Add `menu_currentBar` (defined in menu.c, declared extern)

```c
// Used throughout buttonHandler to know which bar of 16 steps is displayed.
// Changed by SELECT button presses (new bar select) and BAR1/BAR2 presses.
// Read by led_updatePatternTrack() to know which 16 steps to show on SEQ LEDs.
// Declared extern in menu.h; defined as uint8_t menu_currentBar = 0 in menu.c.
```

#### 5b. Change SELECT mode behavior in PERF mode: SELECT1 only

```c
// PERF mode: in the old 8-pattern model, SELECT1..8 chose which of the 8
// patterns to queue next. With NUM_PATTERN=1 there is only one pattern;
// queuing is meaningless. New behavior:
//   SELECT1 → no operation (or could be reserved for "scene select" later).
//   SELECT2..8 → no operation.
// IMPLEMENTATION in handleSelectButton(), case SELECT_MODE_PERF:
//   if (selectNr == 0) { /* no-op or future scene select */ }
//   else { /* ignore */ }
// WHY: User spec says "leave the first SELECT button as the only one active;
//      no function for the rest of them."
// LED: Only SELECT1 LED is lit in PERF mode. See ledHandler §7b.
// RISK: seq_setNextPattern() calls are removed from this path. If the sequencer
//       still polls seq_pendingPattern, it will always get 0 — which is correct.
```

#### 5c. Change SELECT behavior in VOICE and PATTERN (STEP) modes: bar select

```c
// Old behavior: SELECT1..8 chose sub-step window within a main step (sub-steps
// 0..7 for SELECT1, 8..15 for SELECT2, etc.).
// New behavior: SELECT1..8 selects which 16-step bar is shown on the SEQ LEDs.
//   SELECT1 → bar 0 (steps 0..15)
//   SELECT2 → bar 1 (steps 16..31)
//   ...
//   SELECT8 → bar 7 (steps 112..127)
// IMPLEMENTATION in handleSelectButton():
//   case SELECT_MODE_VOICE:
//   case SELECT_MODE_STEP:
//     menu_currentBar = selectNr;
//     led_updatePatternTrack(trackNr, 0, menu_currentBar * NUM_STEPS_PER_BAR);
//     /* In PATTERN mode: also solidly light the selected bar's SELECT LED */
//     /* In VOICE mode: flash the bar SELECT LED for 500ms, then restore voice page */
//     break;
// WHY: User spec says "the SELECT button functions that used to apply to sub-steps
//      now move the bar to one of the 8 16-step bars."
// RISK: The old selectedStepBase = buttonHandler_selectedStep now needs to become
//       menu_currentBar * NUM_STEPS_PER_BAR. Every site that reads
//       buttonHandler_selectedStep to compute a sub-step window must update.
```

#### 5d. Change BAR1 / BAR2 buttons: bar dec/inc navigation

```c
// Old behavior: BAR1 plays the active voice at full velocity; BAR2 at half vel.
// New behavior: BAR1 = previous bar; BAR2 = next bar. (Wrapping at 0 and 7.)
// IMPLEMENTATION in processPress(), cases BUT_BAR1 / BUT_BAR2:
//   case BUT_BAR1:
//     /* Decrement bar, wrap at 0 */
//     menu_currentBar = (uint8_t)((menu_currentBar + NUM_BARS - 1u) % NUM_BARS);
//     /* Refresh SEQ LEDs and SELECT LED */
//     led_updatePatternTrack(menu_getActiveVoice(), 0,
//                            menu_currentBar * NUM_STEPS_PER_BAR);
//     led_setActiveSelectButton(menu_currentBar);
//     /* Flash logic: if in VOICE mode, flash bar SELECT for 500ms */
//     break;
//   case BUT_BAR2:
//     /* Increment bar, wrap at 7 */
//     menu_currentBar = (uint8_t)((menu_currentBar + 1u) % NUM_BARS);
//     /* Same LED refresh as BAR1 */
//     break;
// WHY: User spec says "The BAR <> buttons should also move the bar."
// NOTE: The old voice-trigger behavior (midiParser_playVoiceMidiNote) is lost.
//       That functionality was likely a placeholder and the user spec supersedes it.
// RISK: Any external MIDI or trigger paths that fired on BAR1/BAR2 button press
//       remain unaffected (those are hardware-routed, not software-dispatched
//       from this path). Only the button UI action changes.
```

#### 5e. Update SEQ button actions: operate on bar-relative steps

```c
// Old behavior: SEQ button N in VOICE mode → main step N, expanded to sub-step N*8.
//               SEQ button N in STEP mode → selected step N*8 within a step window.
// New behavior: SEQ button N → step (menu_currentBar * NUM_STEPS_PER_BAR + N).
//               This is the absolute flat step index for PatternData.
// IMPLEMENTATION in buttonHandler_seqButtonPressed() and _seqButtonReleased():
//   uint8_t stepNr = (uint8_t)(menu_currentBar * NUM_STEPS_PER_BAR + seqButtonPressed);
// VOICE mode (non-shift press-and-release): toggle step active bit at stepNr.
// STEP mode: select stepNr as the active step for automation editing.
// The selectedStepLed math stays the same (seqButtonPressed + LED_STEP1) because
//   the SEQ LED row always shows steps 0..15 within the current bar.
// WHY: With no sub-step layer, the 16 SEQ buttons directly address the 16 steps
//      of the current bar. The user taps SEQ button 1 to toggle step 1 of bar 0,
//      or step 1 of bar 3 if bar 3 is selected.
// RISK: The old buttonHandler_selectedStep = seqButtonPressed * 8 line must become
//       buttonHandler_selectedStep = stepNr. The variable name is now "active step"
//       not "selected main step", but we keep the variable to minimise diff.
```

#### 5f. Remove sub-step window logic from `buttonHandler_updateSubSteps()`

```c
// Old: led_updatePatternTrack(trackNr, patternNr, buttonHandler_selectedStep)
//      The third arg was a sub-step base (multiple of 8).
// New: led_updatePatternTrack(trackNr, 0, menu_currentBar * NUM_STEPS_PER_BAR)
//      The third arg is now the first step of the current bar (multiple of 16).
// WHY: buttonHandler_updateSubSteps() is called when the track/pattern view
//      changes and must repaint the SELECT and SEQ LEDs for the current bar.
```

#### 5g. Update automation-arm step logic

```c
// pat_armAutomationStep: the stepNr passed is now an absolute flat 0..127 index.
// The modular decomposition (isMainStep = stepNr % 8 == 0) must change to
// check whether the step is a bar-boundary step:
//   isBarStep = (stepNr % NUM_STEPS_PER_BAR == 0);
// LED feedback: a bar-start step blinks a SEQ LED (STEP1 + bar), all other
// steps blink a SELECT LED (PART_SELECT1 + (stepNr % NUM_STEPS_PER_BAR)).
// Wait — with the new model, long-pressing any SEQ button in STEP mode arms
// that step directly. The SELECT sub-step window within a main step no longer
// exists. The arm logic simplifies:
//   Long-press SEQ button N while in STEP mode → arm flat step
//     (menu_currentBar * NUM_STEPS_PER_BAR + N).
//   The blink LED is STEP1 + N (the SEQ button itself).
// WHY: No sub-step means no SELECT-LED arm feedback; the armed step IS a SEQ button.
// RISK: Any UX that expected to arm individual sub-steps (SELECT LEDs during arm)
//       no longer works. This is intentional per the spec ("no sub-steps").
```

---

### 6. `Core/Hardware/frontPanel/ledHandler.c/.h`

#### 6a. Add bar SELECT LED flash timer state for VOICE mode bar changes

```c
// NEW state in ledHandler.c:
//   static uint16_t led_barFlashTimer = 0;
//   static uint8_t  led_barFlashLed   = 0xFF; // invalid = inactive
//   static uint8_t  led_barFlashWasVoicePage = 0; // which SELECT was lit before
// When buttonHandler changes menu_currentBar in VOICE mode:
//   led_barFlashLed = menu_currentBar;
//   led_barFlashTimer = time_sysTick + (500u * SYSTICK_TICKS_PER_MS);
//   led_barFlashWasVoicePage = menu_getSubPage();
//   led_setBlinkLed(LED_PART_SELECT1 + menu_currentBar, 1);
// In led_tickHandler() (or a new led_serviceBarFlash() called from foreground):
//   if (led_barFlashTimer && time_sysTick >= led_barFlashTimer) {
//       led_setBlinkLed(LED_PART_SELECT1 + led_barFlashLed, 0);
//       led_barFlashTimer = 0;
//       led_setActiveSelectButton(led_barFlashWasVoicePage); // restore voice page LED
//   }
// WHY: User spec says "if the bar changes, the SELECT led of the current bar
//      flashes for 0.5 second ... then the SELECT leds return to the previous
//      display showing the VOICE page selected."
// RISK: led_tickHandler() is called from TIM6 ISR context. The timer comparison
//       is safe (reads time_sysTick which is ISR-updated). The LED write
//       functions are foreground-only. Move the flash resolution into a new
//       foreground function led_serviceBarFlash() called from main loop adjacent
//       to led_processSeqLedState(). Do NOT call it from TIM6.
// AFFILIATE: main.c must call led_serviceBarFlash() each iteration.
```

#### 6b. Update `led_updatePatternTrack()` to use flat-step model

```c
// BEFORE:
//   /* STEP1..16 show main-step mask */
//   for (i = 0; i < 16; i++) {
//       uint8_t on = pat_isMainStepActive(track, i, pattern);
//       led_setValue(on, LED_STEP1 + i);
//   }
//   /* SELECT1..8 show 8 sub-steps of the selected window */
//   start = (selectedStepBase / 8) * 8;
//   for (i = 0; i < 8; i++) {
//       on = pat_isStepActive(track, start + i, pattern);
//       led_setValue(on, LED_PART_SELECT1 + i);
//   }
// AFTER:
//   /* SEQ1..16 show whether each step in the current bar is active */
//   uint8_t barStart = (uint8_t)(selectedStepBase); /* now bar*16 */
//   for (i = 0; i < NUM_STEPS_PER_BAR; i++) {
//       uint8_t on = pat_isStepActive(track, barStart + i, pattern);
//       led_setValue(on, LED_STEP1 + i);
//   }
//   /* SELECT1..8 show bar indicator: lit = current bar, others off */
//   /* (In PATTERN mode: current bar solid. In VOICE mode: shown by caller) */
//   for (i = 0; i < NUM_BARS; i++) {
//       led_setValue((i == menu_currentBar) ? 1 : 0, LED_PART_SELECT1 + i);
//   }
// WHY: The old STEP1..16 row showed which main steps had any active sub-step
//      (aggregated bitmask). The new STEP1..16 row shows which of the 16 steps
//      in the current bar are active — a direct per-step active bit read.
//      The SELECT1..8 row used to show sub-step state; it now shows bar position.
// RISK: The `selectedStepBase` parameter is now interpreted as bar*16 not
//       a sub-step window base. Rename the parameter to `barStepBase` in the
//       function signature to make intent clear.
```

#### 6c. Remove `led_updateRecordedMainStep()` or adapt it

```c
// Old behavior: called when a sub-step is live-recorded to update the parent
//               main-step STEP LED if the main step just became active.
// New behavior: called when a step is live-recorded to update the SEQ LED for
//               that step's position in the current bar.
// SIMPLIFIED IMPLEMENTATION:
//   uint8_t stepInBar = step % NUM_STEPS_PER_BAR;
//   uint8_t bar = step / NUM_STEPS_PER_BAR;
//   if (bar != menu_currentBar) return; /* not in the visible bar */
//   on = pat_isStepActive(activeTrack, step, shownPattern);
//   led_setValue(on, LED_STEP1 + stepInBar);
// WHY: "Main step" is gone. The recorded step's LED is the SEQ button at
//      position stepInBar within the current bar. If the recording lands in
//      a different bar the LED is not updated (the bar change itself should
//      move the cursor, handled by bar-nav logic).
// RENAME: `led_updateRecordedMainStep` → `led_updateRecordedStep` for clarity.
```

#### 6d. Remove `led_updateRecordedSubStep()` or adapt it

```c
// Old behavior: updated a SELECT LED after recording a sub-step if the
//               sub-step was within the current 8-step SELECT window.
// New behavior: No SELECT sub-step window exists. The SELECT LEDs show bar
//               position, not step state. Recording a step does not change
//               which bar is selected.
// DISPOSITION: Remove `led_updateRecordedSubStep()` entirely.
//              Its caller in `led_processSeqLedState()` (SEQ_LED_DIRTY_REC_SUB
//              handling) should also be removed.
// WHY: The SELECT LEDs in VOICE/STEP mode now show the bar indicator and are
//      not individually toggled by recording events.
// RISK: The SEQ_LED_DIRTY_REC_SUB dirty bit and seq_ledState.recordSubStep
//       field become unused. Remove them from SeqLedState to avoid confusion.
//       This also removes the `selectMode` parameter from the function.
```

#### 6e. Update PATTERN mode SELECT LED: always lit for current bar

```c
// User spec: "In PATTERN mode, the LED of the currently selected bar is always
//            lit on the SELECT buttons."
// IMPLEMENTATION: When selectButtonMode == SELECT_MODE_STEP, after any bar
//                 navigation change, call:
//   led_clearSelectLeds();
//   led_setValue(1, LED_PART_SELECT1 + menu_currentBar);
// This is unconditional — in PATTERN mode the current bar SELECT is always solid.
// The chase LED from sequencer playback (SEQ_LED_DIRTY_CHASE) still moves the
// SEQ (step) LEDs, but SELECT LEDs are managed by bar position only.
```

#### 6f. Add `led_initPerformanceLeds()` update for single-pattern mode

```c
// Old: lights all 8 SELECT LEDs to show all 8 patterns available for queuing.
// New: only SELECT1 is lit (the single pattern's "position"); SELECT2..8 off.
// WHY: User spec says PERF mode has only SELECT1 active. The function already
//      sets multiple LEDs; change to:
//   led_clearSelectLeds();
//   led_setValue(1, LED_PART_SELECT1);  /* only SELECT1 */
// RISK: Any SEQ LEDs in PERF mode (roll buttons) are unchanged — they are STEP
//       LEDs, not SELECT LEDs.
```

---

### 7. `Core/Menu/menu.c` and `menu.h`

#### 7a. Add `menu_currentBar`

```c
// menu.h: extern uint8_t menu_currentBar;
// menu.c: uint8_t menu_currentBar = 0;
// WHY: Central mutable UI state for which 16-step bar is visible. Lives in
//      menu.c because it is UI edit state (like menu_activePage, menu_shownPattern).
//      Read by buttonHandler, ledHandler, sequencer (for chase LED bar).
// RISK: No thread-safety concern — only mutated in foreground by button presses.
```

#### 7b. Update `menu_setShownPattern()` / pattern-follow behavior

```c
// With NUM_PATTERN=1, menu_shownPattern and menu_playedPattern are always 0.
// menu_setShownPattern(0) is the only valid call. Simplify the body to a no-op
// that just re-runs the LED update for pattern 0.
// WHY: The follow-mode "snap to played pattern" logic is still needed when bar
//      navigation is added to the follow model in Phase 3, but for now it is
//      harmless to leave the body as-is (it just resolves to pattern 0).
```

#### 7c. Remove `copyClearTools` pattern-copy from SELECT handling in PERF mode

```c
// Old: PART_SELECT buttons in PERF shift-layer chose which pattern to view/edit
//      (copyClear pattern operations used these as pattern destinations).
// New: Pattern copy/clear operations are bar-scoped.
//   - Copy bar: hold COPY + press SOURCE bar SELECT + press DEST bar SELECT
//     copies a 16-step bar's steps to another bar.
//   - Clear bar: SHIFT+COPY then SELECT bar SELECT.
// The PERF-mode shift+SELECT path that called menu_setShownPattern(selectNr)
// and led_setBlinkLed for pattern selection is REMOVED.
// Its replacement is a bar-copy gesture described below.
// WHY: Pattern copy no longer makes sense with one pattern. Bar copy is the
//      natural equivalent.
// RISK: The copyClearTools infrastructure (MODE_COPY_PATTERN, copyClear_copyPattern)
//       will be repurposed or replaced in a subsequent session. For this session,
//       MODE_COPY_PATTERN can be left defined but will produce no action since
//       no button path now routes to copyClear_copyPattern().
```

---

### 8. `Core/Menu/copyClearTools.c`

#### 8a. Update step-range arithmetic

```c
// copyClear_copyTrack() copies step data between tracks within the active pattern.
// With flat steps it should copy the full 0..127 range.
// copyClear_clearTrack() clears all steps of the track.
// These functions call pat_clearTrack() which iterates all steps — no change needed.
// The "clear bar" gesture can be added as a new copyClear mode, but that is
// deferred to a follow-up session.
```

---

### 9. `Core/Hardware/frontPanel/ledHandler.h`

#### 9a. Remove `SEQ_LED_DIRTY_REC_SUB` from `SeqLedState`

```c
// Remove the SEQ_LED_DIRTY_REC_SUB flag (0x04) and recordSubStep field from
// SeqLedState. The sub-step recording feedback via SELECT LEDs is gone.
// WHY: SELECT LEDs now show bar position, not sub-step state. A recording event
//      should only update the relevant SEQ LED (step in bar) — that feedback
//      uses SEQ_LED_DIRTY_REC_MAIN (repurposed to "recorded step").
// RISK: Removing a struct field changes struct layout — any code that reads
//       dirty bits must be audited. grep for SEQ_LED_DIRTY_REC_SUB and
//       seq_ledState.recordSubStep in sequencer.c — those writes must be removed.
```

---

### 10. `Core/Sequencer/sequencer.c` — recording path

#### 10a. Remove `SEQ_LED_DIRTY_REC_SUB` writes

```c
// In seq_addNote() (live-recording), the sequencer currently sets:
//   seq_ledState.recordSubStep = stepNr;
//   seq_ledState.dirty |= SEQ_LED_DIRTY_REC_SUB;
// Remove both lines. The sub-step recording LED feedback path is gone.
// WHY: SELECT LEDs no longer show sub-step state during recording.
// The existing SEQ_LED_DIRTY_REC_MAIN path (updating the STEP LED for the
// recorded step's bar position) is updated as described in ledHandler §6c.
```

---

### 11. `main.c`

#### 11a. Add `led_serviceBarFlash()` call in main loop

```c
// After led_processSeqLedState() in the main loop, add:
//   led_serviceBarFlash();
// WHY: The 500ms bar-SELECT flash timer is resolved in foreground, not ISR.
//      Without this call the flash timer never expires.
```

---

## Verification Plan

### Automated Tests
- Build with `make && make img` — zero errors / warnings.
- Any test harness (if present) for PatternData step access.

### Manual Verification Checklist

1. **Pattern storage:** Start firmware. Confirm `pat_patternSet` uses ~6KB
   (7 tracks × 128 steps × 7 bytes) not ~50KB (old 8-pattern layout). Check
   with memory monitor or sizeof log.

2. **SEQ buttons in VOICE mode:** Press SEQ1..16 — steps in bar 0 toggle. SEQ
   LEDs reflect active state directly.

3. **BAR1/BAR2:** Press BAR2 twice. SELECT3 LED flashes for 0.5s, then voice
   page SELECT LED returns. SEQ LEDs now show bar 2 (steps 32..47).

4. **SELECT bar navigation in VOICE mode:** Press SELECT4. Bar 3 flash,
   SEQ LEDs show steps 48..63.

5. **SELECT bar navigation in PATTERN mode:** Press SELECT5. SELECT5 LED stays
   solid. SEQ LEDs show steps 64..79.

6. **PERF mode:** Only SELECT1 lit. SELECT2..8 do nothing.

7. **Save `.pat`:** Save a pattern. File is smaller than old format.
   Old firmware shows load error (format version mismatch). New firmware
   loads correctly.

8. **Load `.pat` (old format):** Load an old `.pat` file. Should show
   `Format?` error for 2 seconds.

9. **Load `.pat` (new format):** Load a new-format file. Pattern 0 loads.

10. **Load `.all` / `.prf`:** Same version check. First pattern loads; rest blank.

11. **Sequencer runs:** Press START. Chase LED advances across 128 steps,
    cycling through bars. Bar counter in sequencer increments at step 16/32/48...

12. **Live recording:** Record a note. The correct SEQ LED lights. The SELECT
    LED bar indicator does not change.

---

## Risk Register

| Risk | Severity | Mitigation |
|---|---|---|
| `seq_nextStep()` × 8 multiplications | HIGH | Audit all `* 8` in sequencer.c before any merge |
| File format version mismatch with old cards | MED | Version byte + graceful error on load |
| `pat_mainSteps` removal cascade | HIGH | Compiler errors act as checklist; fix each one explicitly |
| Bar flash timer in ISR | MED | Explicitly move to foreground-only `led_serviceBarFlash()` |
| `menu_currentBar` read by both buttonHandler and ledHandler without locks | LOW | All writes are foreground-only; no ISR writes this variable |
| `copyClearTools` pattern-copy path becomes dead code | LOW | Leave definitions; Phase 3 will replace with bar-copy |
| `seq_determineNextPattern()` stub removes RANDOM next | LOW | Random pattern selection is only useful with multiple patterns |
