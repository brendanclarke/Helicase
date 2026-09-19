# S068 missing chaselight assessment

## Root cause

The boot alignment path updates three of the four Pattern/Scene authorities
used by the chaselight and leaves the fourth at its BSS default:

| State | After restoring active Scene N |
|---|---:|
| `scene_active_index` | N |
| `seq_activePattern` | N |
| `menu_shownPattern` | N |
| `menu_playedPattern` | **0** |

After Bank restore, `filesystem.c` calls
`seq_alignActivePatternToScene(op_bank_active_scene)` and
`menu_setShownPattern(op_bank_active_scene)`. The sequencer helper deliberately
does not call `led_notifyPatternChanged()` because that notifier also performs
runtime presentation/performance side effects that are inappropriate during
pre-audio boot. The helper does not otherwise update `menu_playedPattern`.

`menu_playedPattern` is zero-initialized and is assigned only by
`led_notifyPatternChanged()`. The chase renderer reads that mirror, not
`seq_activePattern`:

```text
shownPattern = menu_getViewedPattern();
playedPattern = menu_playedPattern;
show chase only if shownPattern == playedPattern
```

Therefore a Bank that boots with active Scene N != 0 has
`menu_shownPattern == N` and `menu_playedPattern == 0`. Every chase update is
deliberately cleared by `led_updateCurrentStep()`, even though the producer is
healthy. A Bank that boots into Scene 0 happens to work because the stale BSS
default equals the real Scene, which explains why the failure is intermittent
across fresh boots.

Changing Scenes in PERF mode calls `seq_selectActivePattern()`, which calls
`led_notifyPatternChanged(seq_activePattern)`. That notifier finally assigns
`menu_playedPattern`, making it equal to the shown Pattern; the next chase
dirty event becomes visible. This exactly matches the reported self-repair.

## What is not failing

- `seq_realignActivePatternToMasterClock()` writes `seq_ledState.chaseStep`
  and sets `SEQ_LED_DIRTY_CHASE` during boot alignment.
- Normal scheduler advances continue to set the same dirty bit.
- `led_processSeqLedState()` drains it in the foreground.
- The missing LED is not caused by absence of producer events or by the
  temporary LED toggle itself. It is rejected at the shown/played equality
  predicate.

## Targeted correction

Keep the no-MIDI/no-note-off/no-PERF-repaint property of the boot alignment
path, but align the played-Pattern UI mirror at the same commit boundary.
Either:

1. extend `seq_alignActivePatternToScene()` to publish the validated Scene to a
   side-effect-free Menu/LED state setter, or
2. add a dedicated `led_alignPlayedPattern()`/`menu_setPlayedPattern()` and call
   it beside `menu_setShownPattern()` after every filesystem Scene/Bank
   realignment.

Do not call the existing `led_notifyPatternChanged()` blindly from pre-audio
filesystem code: its follow, PERF repaint, program-change companion path, and
ownership assumptions are the reason the alignment helper was split out.
The invariant should instead be explicit:

> At every committed playback realignment, `menu_playedPattern` must equal
> `seq_activePattern` before a chase dirty event can be drained.

This invariant should also be applied to the other filesystem realignment call
sites, not only the Bank Load phase shown by the report.

## Acceptance test

- Save/restore a Bank with each active Scene 0-15 and follow both OFF and ON.
- Start playback without entering PERF. On VOICE, STEP, and EUKLID pages, the
  correct visible step must chase immediately.
- Confirm PERF still suppresses chase because it owns the SEQ row.
- Change Scene from PERF and through runtime Bank/Scene Load; verify
  `scene_getActiveIndex()`, `seq_activePattern`, `menu_shownPattern`, and
  `menu_playedPattern` at each completion boundary.
- Confirm boot alignment emits no MIDI program change and no all-notes-off.

No `Core/` code was changed in this assessment.

---

## Exact fix specification

### Summary

Add a new side-effect-free setter `menu_setPlayedPattern()` in `menu.c`/`menu.h`,
then call it at each of the three filesystem realignment sites that already pair
`seq_alignActivePatternToScene()` with `menu_setShownPattern()`. This restores
the invariant that `menu_playedPattern == seq_activePattern` at every committed
playback realignment, so the chase renderer's shown/played equality predicate
passes on the very first dirty drain after boot.

No existing function is repurposed. No MIDI, note-off, follow-mode, or repaint
side effects are introduced. The fix is a pure Menu-state alignment that mirrors
the sequencer-state alignment `seq_alignActivePatternToScene()` already provides.

### Change 1 of 3 — New setter function (menu.c)

**File**: `Core/Menu/menu.c`
**Location**: immediately after `menu_setShownPattern()` (after line 11875)
**Action**: ADD

```c
/*
 * menu_setPlayedPattern — align the played-Pattern UI mirror.
 *
 * What: stores the Pattern slot that the sequencer is currently playing,
 * as a Menu-owned UI mirror. The chase renderer in led_updateCurrentStep()
 * compares menu_getViewedPattern() against this mirror to decide whether
 * the STEP-row chase LED should be visible: chase is shown only when the
 * viewed and played Patterns match.
 *
 * Why this exists: led_notifyPatternChanged() is the normal runtime writer
 * of menu_playedPattern, but that function carries follow-mode LED/menu
 * repaint, PERF Scene LED refresh, and foreground-only ownership assumptions
 * that are inappropriate during pre-audio boot or filesystem-driven Scene
 * realignment. This setter provides the state update alone, with no
 * side effects beyond the assignment.
 *
 * Input: patternNr is the Scene/Pattern slot to record as the played
 * Pattern. Validated through pat_patternValid(); an out-of-range value
 * falls back to 0.
 *
 * Output: menu_playedPattern is set to the validated value.
 *
 * Common callers: filesystem.c's three Scene/Bank realignment sites, each
 * of which already calls seq_alignActivePatternToScene() (sequencer state)
 * and menu_setShownPattern() (viewed-Pattern state). This setter completes
 * the triple by aligning played-Pattern state.
 *
 * Affiliates:
 *   - menu_playedPattern (this file, line ~1248) — the global this sets
 *   - menu_setShownPattern() (this file) — the viewed-Pattern counterpart
 *   - led_notifyPatternChanged() (ledHandler.c) — the runtime writer that
 *     also sets menu_playedPattern but with follow/PERF/LED side effects
 *   - led_updateCurrentStep() (ledHandler.c) — the chase renderer that
 *     reads menu_playedPattern to gate chase visibility
 *   - seq_alignActivePatternToScene() (sequencer.c) — the sequencer-state
 *     counterpart called at the same realignment sites
 */
void menu_setPlayedPattern(uint8_t patternNr)
{
    menu_playedPattern = pat_patternValid(patternNr) ? patternNr : 0u;
}
```

### Change 2 of 3 — Declare the new setter (menu.h)

**File**: `Core/Menu/menu.h`
**Location**: immediately after the `menu_setShownPattern()` declaration (after line 462)
**Action**: ADD

```c
/*
 * menu_setPlayedPattern — side-effect-free alignment of the played-Pattern
 * UI mirror used by the chase renderer. See menu.c for full contract.
 *
 * Input: patternNr is the Scene/Pattern slot. Output: menu_playedPattern
 * is set to the validated value. No LED, MIDI, follow, or repaint work.
 *
 * Callers: filesystem.c Scene/Bank realignment sites.
 * Affiliates: menu_setShownPattern(), led_notifyPatternChanged(),
 * led_updateCurrentStep(), seq_alignActivePatternToScene().
 */
void menu_setPlayedPattern(uint8_t patternNr);
```

### Change 3 of 3 — Call the setter at all three filesystem realignment sites

**File**: `Core/Hardware/SD/filesystem.c`

#### Site A — Bank Load phase-20 commit (line ~14047)

**Location**: immediately after `menu_setShownPattern(op_bank_active_scene);`
**Action**: ADD one line

```c
        menu_setPlayedPattern(op_bank_active_scene);
```

Existing context (before → after):

```c
        /* BEFORE */
        seq_alignActivePatternToScene(op_bank_active_scene);
        menu_setShownPattern(op_bank_active_scene);
        memcpy(preset_currentName, op_bank_display_name, 8u);

        /* AFTER */
        seq_alignActivePatternToScene(op_bank_active_scene);
        menu_setShownPattern(op_bank_active_scene);
        menu_setPlayedPattern(op_bank_active_scene);
        memcpy(preset_currentName, op_bank_display_name, 8u);
```

**Why**: this is the primary Bank Load commit during both boot and runtime
Bank Load. It is the exact site identified in the root cause — active Scene N
is committed here but `menu_playedPattern` was left at its BSS default of 0.

#### Site B — HCPR matching-winner reader (line ~26884)

**Location**: immediately after `menu_setShownPattern(active_scene);`
**Action**: ADD one line

```c
    menu_setPlayedPattern(active_scene);
```

Existing context (before → after):

```c
    /* BEFORE */
    seq_alignActivePatternToScene(active_scene);
    menu_setShownPattern(active_scene);
    (void)filesystem_blockChdir(NULL);

    /* AFTER */
    seq_alignActivePatternToScene(active_scene);
    menu_setShownPattern(active_scene);
    menu_setPlayedPattern(active_scene);
    (void)filesystem_blockChdir(NULL);
```

**Why**: the HCPR matching-winner boot reader restores a Bank from an AutoSave
record that matched the canonical Bank. Its realignment commit is structurally
identical to Site A and has the same stale-mirror defect.

#### Site C — HCPR all-refreshed reader (line ~28039)

**Location**: immediately after `menu_setShownPattern(bank_activeSceneSlot());`
**Action**: ADD one line

```c
    menu_setPlayedPattern(bank_activeSceneSlot());
```

Existing context (before → after):

```c
    /* BEFORE */
    seq_alignActivePatternToScene(bank_activeSceneSlot());
    menu_setShownPattern(bank_activeSceneSlot());
    /* Step 3: per-Scene Case 1/2/3 evaluation. */

    /* AFTER */
    seq_alignActivePatternToScene(bank_activeSceneSlot());
    menu_setShownPattern(bank_activeSceneSlot());
    menu_setPlayedPattern(bank_activeSceneSlot());
    /* Step 3: per-Scene Case 1/2/3 evaluation. */
```

**Why**: the HCPR all-refreshed boot reader is the fallback path when every
HCNAMES row carried an `R` flag. Same structural gap as the other two sites.

---

## Risk assessment

**Risk level: very low.** The fix is three identical one-line additions at
existing, well-documented realignment sites, plus a trivially simple setter
function.

1. **No new cross-module coupling.** `filesystem.c` already includes `menu.h`
   (line 76) and already calls `menu_setShownPattern()` at the exact same
   sites.

2. **No side effects.** `menu_setPlayedPattern()` writes one byte with
   `pat_patternValid()` bounds checking. It does not touch LEDs, LCD, MIDI,
   note-off, follow mode, or any repaint path. Compare
   `led_notifyPatternChanged()`, which does all of those and is explicitly
   why the boot alignment path avoided it.

3. **No ISR interaction.** `menu_playedPattern` is read only in
   `led_updateCurrentStep()` (foreground), and the three call sites are all
   foreground (boot is single-threaded, runtime Bank Load is in
   `filesystem_tick()`). No ISR reads or writes this variable.

4. **No RAM cost.** The setter is a single conditional store. No new globals,
   no new state, no allocation.

5. **No behavioral change when Scene == 0.** The BSS default is 0, so a Bank
   that boots with active Scene 0 already has `menu_playedPattern == 0 ==
   menu_shownPattern`. The setter writes 0 to 0, which is a no-op. This
   confirms the fix cannot regress the working case.

6. **`led_notifyPatternChanged()` remains the authoritative runtime writer.**
   PERF Scene switches still go through `seq_selectActivePattern()` →
   `led_notifyPatternChanged()`, which sets `menu_playedPattern` along with
   follow-mode and LED work. The new setter does not compete with that path;
   it covers only the pre-audio/filesystem-driven realignment that
   `led_notifyPatternChanged()` was deliberately excluded from.

### Ambiguities: none identified

- All three call sites are structurally identical: they pair
  `seq_alignActivePatternToScene()` + `menu_setShownPattern()`. The new
  third call is a natural extension of the same pattern.
- No future call site is likely to call `seq_alignActivePatternToScene()`
  without also needing both Menu mirrors, but the assessment doc's invariant
  statement makes this explicit for future code.
- The `menu_init()` path (line ~11940) zeroes `menu_shownPattern` but not
  `menu_playedPattern`; this is fine because `menu_playedPattern` is BSS-
  zeroed to 0 before `menu_init()` runs, so both are 0 at init.

## Implementation notes

- 2026-09-19: Confirmed the working tree has exactly three
  `seq_alignActivePatternToScene()` + `menu_setShownPattern()` filesystem
  pairs, matching Sites A-C. `menu_playedPattern` had no side-effect-free
  writer; `led_notifyPatternChanged()` remained the only assignment site.
- 2026-09-19: Added `menu_setPlayedPattern()` to `Core/Menu/menu.c` and its
  documented declaration to `Core/Menu/menu.h`. The setter validates through
  `pat_patternValid()` and performs no LED, MIDI, follow-mode, repaint, or
  allocation work.
- 2026-09-19: Added the setter immediately after `menu_setShownPattern()` at
  all three filesystem realignment commits: Bank Load phase 20, the HCPR
  matching-winner reader, and the HCPR all-refreshed reader. Updated the
  adjacent filesystem comment blocks to describe all three aligned mirrors.
- 2026-09-19: `make` passed. The linker produced the existing project
  warnings only (unused legacy filesystem helpers and newlib syscall stubs);
  the changed translation units produced no new warnings. Final image sizes:
  `text=447,772`, `data=412`, `bss=291,196`; `make img` produced
  `build/LXRV2_lxr02.img` with a 448,184-byte binary payload.
- 2026-09-19: Final source checks passed: `git diff --check`, one setter
  declaration/definition, three filesystem call sites, and no additional
  direct writers to `menu_playedPattern`. Hardware acceptance remains the
  outstanding step from the assessment: exercise active Scenes 0-15 with
  follow OFF/ON and verify VOICE/STEP/EUKLID chase plus PERF suppression,
  runtime Scene/Bank loads, and no boot MIDI program change/all-notes-off.

## Post-implementation code review

Reviewed 2026-09-19 against the committed working tree diff.

### Verification summary

| Check | Result |
|-------|--------|
| Setter definition matches spec | PASS — `menu.c:11914`, single validated assignment, no side effects |
| Setter declaration matches spec | PASS — `menu.h:474`, placed after `menu_setShownPattern()` |
| Site A (Bank Load phase 20, `filesystem.c:14049`) | PASS — `menu_setPlayedPattern(op_bank_active_scene)` immediately after `menu_setShownPattern` |
| Site B (HCPR matching-winner, `filesystem.c:26889`) | PASS — `menu_setPlayedPattern(active_scene)` immediately after `menu_setShownPattern` |
| Site C (HCPR all-refreshed, `filesystem.c:28049`) | PASS — `menu_setPlayedPattern(bank_activeSceneSlot())` immediately after `menu_setShownPattern` |
| Comment blocks updated | PASS — all three filesystem sites mention `menu_setPlayedPattern` and describe the third mirror |
| No stray `menu_playedPattern =` writers | PASS — exactly two: `ledHandler.c:1218` (runtime `led_notifyPatternChanged`) and `menu.c:11916` (new setter) |
| No additional direct callers of setter | PASS — exactly three filesystem sites, matching the spec |
| Image delta | PASS — text grew 48 bytes (447,724 → 447,772), consistent with three call-site additions plus one small function; bss unchanged (291,196) |

### Assessment

The implementation is a faithful, minimal execution of the spec. No
deviations, no extra changes, no missing sites. The filesystem comment blocks
were proactively updated to name the third mirror and clarify the
side-effect-free constraint — a durable improvement over the pre-fix comments.

The only remaining step is the hardware acceptance test matrix from the
assessment section above.

## Hardware acceptance

- 2026-09-19: tested on hardware. Chaselight appears immediately on boot
  across non-zero active Scenes, on VOICE, STEP, and EUKLID pages, with
  follow both OFF and ON. PERF still suppresses chase. Runtime Scene/Bank
  Load transitions preserve the chase. No MIDI program change or
  all-notes-off observed at boot alignment. **PASS — fix accepted.**
