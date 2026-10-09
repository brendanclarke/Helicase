# S078 P3 — Track Parameter Automation Follow-up

Two defects found during hardware testing of S078 P2 §B (STEP track-settings
automation). Both root causes were found by reading the code; no diagnostic
traces are needed. Line numbers refer to `Core/Menu/menu.c` in the
uncommitted S078 P2 tree.

The code-level schedule is `S078_P3_IMPLEMENTATION.md` (changes M1–M26,
H1–H3, D1–D2). This plan was updated after that deep dive. The F-items below
cite the matching M/H/D changes.

| # | Symptom | Root cause (one line) |
|---|---------|-----------------------|
| 1 | In STEP mode, an automated `len`/`scl`/`shf` name on the top row of the track settings page is never underlined | Nothing searches the whole Pattern for track targets on SEQ_PAGE, and the one branch that underlines a name in `sa_applyTrackMarkers()` can never run (plus 1c: the step-automation editor's search restarts are dead code) |
| 2 | SHIFT+COPY (clear) + turning the `scl` or `shf` pot does not remove that parameter's automation | `menu_knobClearTarget()` has no case for the SEQ_PAGE track cells, so the pot clear is never queued |

---

## Bug 1: no top-row underline for automated track parameters

### Root cause

The VOICE, Effect, and PERF pages underline a parameter **name** (top row)
when the Pattern search finds automation for it anywhere in the Pattern. The
search state is `va_scanService()` → `va_searchSceneMask` /
`va_searchTargetMask[]`, and it runs whether or not steps are held (S074
rule: the underline reports stored data). The STEP page has nothing that does
this. There are two separate faults.

**1a. The name-underline branch can never run.** In the overview loop of
`sa_applyTrackMarkers()`:

```c
if (va_resolveHeldValue(target, &value7)) {          /* :3812 */
    ... underline the VALUE (row 1) ...
} else if ((sa_trackPresenceMask & (1u << pos)) != 0u && ...) {   /* :3834 */
    ... underline the NAME (row 0) ...
}
```

`sa_trackPresenceMask` is only ever set in two places:

- `sa_refreshPresence()` (:3561), which sets bit `cell` exactly when
  `va_resolveHeldValue(target, ...)` succeeds for the same target over the
  same held list (:3572);
- `sa_writeAutomationFromKnob()` (:3646), right after writing that target to
  the held steps, which makes `va_resolveHeldValue()` succeed.

So whenever a presence bit is set, the `if` condition is already true and the
`else if` is never reached. The top-row name is never underlined. When a held
step carries the automation, the most the code can do is underline the value
on the bottom row. The edit-mode branch has no name case at all.

**1b. Outside the held-step overlay nothing runs at all.**
`sa_applyTrackMarkers()` returns at once when `!va_overlayActive` (:3756).
`va_scanService()` is called only on VOICE pages (:12966), the Effect page
(:13045), and PERF; it never runs on SEQ_PAGE. Once the steps are released,
or when the track settings page is opened normally, no automated track
parameter can be underlined.

**1c. A related fault found in the deep dive: the step-automation editor
never restarts the search.** `menu_stepAutomationExecuteItem0()` restarts
the search after a delete only `if (menu_isVoicePage(menu_activePage))`
(:10762, :10767). The editor only runs while `menu_stepAutomationPageActive()`
is true, which requires `SEQ_PAGE` (:10046), so that guard is never true.
Target changes (`menu_stepAutomationReplaceTarget()`, :10347) never restart
the search either. Once 1a/1b are fixed, a `len`/`scl`/`shf` target added or
removed in the editor (`scn` category) would show a stale name underline
unless these sites restart the search.

### Fix

Give SEQ_PAGE the same Pattern-wide search for the active track that VOICE
pages have, using three new bits in the existing `va_searchSceneMask` byte.
`sa_applyTrackMarkers()` then follows the VOICE marker rule exactly:

- the overlay is active and a held step carries the target: show its value and
  underline the value (unchanged);
- otherwise, if the search is complete and found the target: underline the name.

The dead `sa_trackPresenceMask` / `sa_refreshPresence()` pair is removed.

**F1.1 Search bits** (M1, M2; near :1431, beside
`VA_SEARCH_SCENE_EFFECT_MORPH_BIT`):

```c
/* SEQ_PAGE only (S078 P3): track length/scale/shuffle of the active track,
 * bit = 0x10 << SEQ subpage-0 cell position (0 len, 1 scl, 2 shf). */
#define VA_SEARCH_SCENE_TRACK_BIT(cell) ((uint8_t)(0x10u << (cell)))
```

Bits 0x10/0x20/0x40 do not collide with VOICE (0x01–0x04) or Effect (0x08).
PERF uses its own mask. The va_* budget comment gets a "STEP-page sharing
(+0 B)" paragraph.

**F1.2 Shared classifier and `va_scanService()`** (M6–M9):

- New helper `va_seqTrackSearchBit(target, track)`, placed after
  `va_sceneSearchBitForCell()` (:2122). It returns
  `VA_SEARCH_SCENE_TRACK_BIT(0/1/2)` when `target` is the length, scale, or
  shuffle Scene target of `track` (descriptor kind plus `voice_slot`), and 0
  otherwise. Both setting the bit (scan) and clearing it (F1.6) use it, so
  the two stay symmetric, in the same way `va_sceneSearchBitForCell()` serves
  the VOICE Scene cells.
- In `va_scanService()`, add `seq_page = (menu_activePage == SEQ_PAGE)`.
  SEQ_PAGE uses the VOICE geometry: only the active track, no track advance,
  and the existing track-mismatch restart applies. In the entry loop, after
  the `effect_page` branch:

```c
if (seq_page) {
    va_searchSceneMask |=
        va_seqTrackSearchBit(autos[i].target, va_searchTrack);
    continue;
}
```

The `continue` keeps the VOICE classification from running with the
meaningless SEQ_PAGE slot value. `va_searchRestart()` needs only a comment
update (M5): its default branch already sets
`va_searchTrack = menu_activeVoice`.

**F1.3 Run the search on SEQ_PAGE** (M24; `menu_serviceRuntimeWidgets()`,
:12983). Use the same order as the VOICE pages: held state, then scan, then
debounce.

```c
if (menu_activePage == SEQ_PAGE) {
    if (va_overlayActive)
        va_updateHeldState();
    va_scanService();
    if (va_overlayActive)
        va_underlineService();
}
```

The search runs on every SEQ subpage, including the step editor and the
step-automation editor. So a restart triggered there has finished by the time
the user returns to the track settings.

**F1.4 Keep the search result current.** `va_scanService()` returns early when
`va_searchComplete` is set, before its context-mismatch check, so every change
of context or content needs an explicit restart:

| Event | Site | Change |
|-------|------|--------|
| Entering SEQ_PAGE from another page (a VOICE result can be complete for the same Pattern and track) | M25: `menu_switchPage()`, inside `if (pageNr == SEQ_PAGE)` (:14321–14332) | `if (old_page != SEQ_PAGE) va_searchRestart();`. The `old_page` guard keeps the SHIFT release (`buttonHandler_enterSeqModeStepMode()` → `menu_switchPage(SEQ_PAGE)`) and STEP TRACK presses from blanking valid underlines |
| Track change | `menu_setActiveVoice()` | none, it already restarts on every page except the Effect page |
| Viewed Pattern/Scene change | M26: `menu_setShownPattern()` (:15330) | add a separate `else if (menu_activePage == SEQ_PAGE)` arm: restart and repaint. Widening the Effect arm was rejected because its comment ("SEQ row belongs to the FX sequencer") does not apply. The held-step overlay is not reset: it already reads `menu_shownPattern` live |
| Copy/clear finished a paste, a clear, or a pot-clear register pass | M13: `menu_patternContentChanged()` (:2498) | also admit `SEQ_PAGE` |
| Step-automation editor changes a target (category or PAR cycling) | M21: `menu_stepAutomationReplaceTarget()` (:10365–10374) | `va_searchRestart()` on both success paths. This function is the single path for every editor target change. Value-only edits and Add (`off` sentinel) do not change presence |
| Step-automation editor deletes an entry or a track-wide target (Bug 1c) | M22: `menu_stepAutomationExecuteItem0()` (:10759–10769) | make both `va_searchRestart()` calls unconditional (remove the dead `menu_isVoicePage()` guards) |

This replaces the earlier idea of restarting in
`menu_showStepTrackSettingsFirstHalf()` / `menu_toggleStepTrackSettingsHalf()`.
Restarting where the data changes covers every way back to subpage 0 (TRACK
press, SHIFT release, encoder) and needs no navigation tracking.

**F1.5 Immediate updates from the overlay** (M17, M18):

- `sa_writeAutomationFromKnob()` (:3646): replace
  `sa_trackPresenceMask |= ...` with
  `va_searchSceneMask |= VA_SEARCH_SCENE_TRACK_BIT(knob_idx);`. This matches
  the VOICE write path, which sets its search bit when it writes. The bit is
  needed because a completed search is never re-run, and a running one may
  already have passed the held steps (or the write may still be queued in the
  patSvc FIFO). With it, the name underline is correct as soon as the steps
  are released.
- `sa_clearAutomationFromKnob()` (:3687–3690): replace `sa_refreshPresence();`
  and `menu_automationTargetCleared(target);` with `va_searchRestart();`. This
  clear removes the target from the held steps only, so other steps may still
  carry it. The bit must be found again by the scan, not dropped outright.
  This matches the STEP automation editor's deletes.

**F1.6 `menu_automationTargetCleared()`** (M14; :2513): add a SEQ_PAGE arm
after the PERF arm (:2581). A pot clear (Bug 2 fix) removes the target from
the whole track, so the bit is dropped at once:

```c
} else if (menu_activePage == SEQ_PAGE) {
    va_searchSceneMask &=
        (uint8_t)~va_seqTrackSearchBit(target, menu_activeVoice);
}
```

This function is for removals from the whole Scene only (its header in
`menu.c` and `menu.h` H2 says so). The held-step clear uses the restart in
F1.5.

**F1.7 `sa_applyTrackMarkers()`** (M19, which replaces the function and its
header):

- Entry guard (:3756): `if (menu_activePage != SEQ_PAGE) return;`. Keep the
  subpage-0 check. The pass now always runs a marker update on subpage 0, even
  with nothing to mark, so stale CGRAM marks are retired. No other SEQ_PAGE
  code defines CGRAM slots 2–5 (checked).
- Overview: gate the value branch with `va_overlayActive &&
  va_resolveHeldValue(...)`. Change the name `else if` condition from
  `sa_trackPresenceMask & (1u << pos)` to
  `va_searchComplete && (va_searchSceneMask & VA_SEARCH_SCENE_TRACK_BIT(pos))`
  (`pos` < 3 is guaranteed because `menu_seqCellToTrackTarget()` returned
  valid). Keep the existing `va_underlineSuppressed & suppress_bit` term.
  Suppression is only non-zero while the overlay is active and is cleared when
  it exits.
- Edit mode: restructure into an `invalid / held value / name` chain. Gate the
  held-value branch with `va_overlayActive`, and add a name branch that
  underlines the first non-space character of `editDisplayBuffer[0][8..15]`.
  That is where `menu_repaintGeneric()` writes the long name for static cells
  (category at 0..7), matching `va_applyVoiceMarkers()`.

**F1.8 Remove dead state** (M3, M4, M10–M12, M15, M16, M20, H3):
`sa_trackPresenceMask` (:1501) with its comment block, `sa_refreshPresence()`
(:3561) with its forward declaration (:2013), and their uses in
`va_updateHeldState()` (:2389, :2406), `va_resetOverlay()` (:2435), and
`menu_enterStepTrackAutomationOverlay()` (:3894, :3899). Comment references in
`menu_seqCellToTrackTarget()` and `menu.h` are updated.
`sa_refreshAutomationLeds()` calls stay. The overlay entry needs no restart of
its own: `menu_setActiveVoice()` restarts on a track change, and F1.4 restarts
on a page change.

---

## Bug 2: SHIFT+COPY + pot does not clear track automation

### Root cause

The gesture as tested: on the STEP track settings page, hold SHIFT+COPY and
turn the `scl`/`shf` pot, with no step buttons held. Without held steps the
P2 held-step overlay is not active (`va_overlayActive == 0`), so the
overlay's intercept in `menu_parseKnobDelta()` (:12759) is skipped. The turn
then reaches the ordinary pot-clear path (:12790):

```c
if (copyClear_ownsPots()) {
    cc_pot_target_t target;
    if (delta != 0 && menu_knobClearTarget(knobNr, &target))
        (void)copyClear_potTurned(&target);
    return;
}
```

SEQ_PAGE track cells resolve as `MENU_CELL_STATIC` with
`PAR_TRACK_LENGTH`/`PAR_TRACK_SCALE`/`PAR_SHUFFLE`. In `menu_knobClearTarget()`
the `MENU_CELL_STATIC` arm (:12708) handles only `PAR_VOICE1..6_MORPH` and
`PAR_EFFECT_MORPH`, and returns 0 for everything else. So
`copyClear_potTurned()` is never called and nothing is queued. The pot-clear
target table in `COPYCLEAR_UTILITIES.md` §8 has no STEP row, although the
spec's mode table lists "pots = automation clear" for STEP.

The clear path of the held-step overlay (`sa_clearAutomationFromKnob()`, used
only while steps are held after a TRACK press) reads correctly. SHIFT
press/release does not reset the overlay: `menu_switchPage(SEQ_PAGE)` →
SEQ_PAGE does not call `va_resetOverlay()`. That path is not the cause of this
defect, but it still needs hardware testing (test 5).

### Fix

**F2.1 `menu_knobClearTarget()`** (M23; :12708): at the top of the
`MENU_CELL_STATIC` arm, map SEQ_PAGE subpage-0 cells through the existing
resolver:

```c
if (menu_activePage == SEQ_PAGE && active_page == 0u) {
    out->pattern_target = menu_seqCellToTrackTarget(
        (uint8_t)(knobNr + second_page), menu_activeVoice);
    return (uint8_t)(out->pattern_target != INSTRUMENT_PARAM_INVALID);
}
```

- The `active_page == 0u` guard is required. On subpage 1, the STATIC cells at
  positions 0–2 are velocity, note, and probability, which must stay
  non-clearable.
- `second_page` is already correct for SEQ_PAGE because it is not a screen
  page. On the second half, positions 4–7 return INVALID: play mode, MIDI
  channel, and note are not automatable.
- The SHIFT Morph endpoint view (`menu_patternTrackMorphEndpoint`) changes
  only value resolution, not the cell identity, so the clear works in both
  views, as it does on VOICE.

After this, the existing pipeline does the rest: `copyClear_potTurned()` →
`ccClear_potTurned()` → `ccSvc_registerAdd()`. The register is generic: it
accepts any 9-bit target and removes it from all 7 × 128 steps of the active
Scene (`ccSvc_runRegister()`). The target ID is per-track, so only the active
track is affected. The underline drops at once through
`menu_automationTargetCleared()` (F1.6). `ccSvc_targetPending()` stops the
scan from marking it again while the removal is still waiting. When the
removal finishes, `ccSvc_patternChangedUi()` → `menu_patternContentChanged()`
(admitted for SEQ_PAGE in F1.4) restarts the search. The types fit:
`cc_pot_target_t.pattern_target` and `instrument_param_id_t` are both
`uint16_t`, and `INSTRUMENT_PARAM_INVALID` is `0xffff`.

Removing the automation does not undo the value it last applied to the
track. The track keeps that scale/length/shuffle until transport stop or a
Pattern restore (`presetMorph_clearAllTrackParamStepOverrides()`), which is
how all step automation already behaves.

**F2.2 Spec** (D1, D2): add a STEP row to the `COPYCLEAR_UTILITIES.md` §8
target table:
`STEP (track settings, Normal or Morph view) | len, scl, shf | track target
405+t / 412+t / 419+t | —`. Add a bullet saying that with steps held after a
TRACK press, the pot clear is limited to the held steps (P2 §B.8). Extend the
"Pot clears" test summary row (line 1054) to cover STEP.

---

## Files changed

| File | Change |
|------|--------|
| `Core/Menu/menu.c` | F1.1–F1.8, F2.1 (M1–M26) |
| `Core/Menu/menu.h` | Comments only: `menu_patternContentChanged()`, `menu_automationTargetCleared()`, `menu_enterStepTrackAutomationOverlay()` (H1–H3) |
| `knowledge_files/specification_reference/COPYCLEAR_UTILITIES.md` | F2.2 (D1, D2) |

No changes to buttonHandler, copyClearSession, copyClearService, clearOps,
PatternData, PatternStackService, SceneModTargets, presetMorphEngine, or the
sequencer.

## RAM

No new allocation. The three bits use spare bits of the existing 1-byte
`va_searchSceneMask` (static SRAM). Removing `sa_trackPresenceMask` frees
1 B of static SRAM (`.bss`), which per policy is not reused for anything
else. No stack growth: the SEQ search reuses the existing `autos[]` buffer in
`va_scanService()`, and `va_seqTrackSearchBit()` has one pointer local. The
46 B `_Static_assert` on the va_* state is unaffected.

## Known limitations (accepted)

- A held-step clear or an editor delete that is still waiting in the patSvc
  FIFO (service busy) can be read by the immediate rescan before it runs. The
  name can then stay underlined until the next restart. The existing STEP
  editor deletes already behave this way.
- Clearing automation does not undo the value it last applied to the track
  (see F2.1).

## Testing

| # | Steps | Expected |
|---|-------|----------|
| 1 | STEP mode, track settings, Scene with no track automation | No underlines |
| 2 | Hold steps → TRACK → turn `scl` → release steps | `scl` name underlined on the top row: at once if the search had finished (the bit is set at the write), otherwise when it finishes |
| 3 | Repeat 2 but keep the steps held | `scl` **value** underlined after the 100 ms quiet period (VOICE rule: a held value replaces the name marker) |
| 4 | Clicked-in (encoder) on `scl` with no steps held, track has `scl` automation | Long name underlined (row 0, first character from column 8) |
| 5 | No steps held, SHIFT+COPY, turn the `scl` pot | `scl` underline disappears at once; after the register pass, step automation for `scl` no longer runs on that track; other tracks unchanged; the last applied scale stays until transport stop or Pattern restore |
| 6 | Same as 5 for `shf` and `len`; also with SHIFT still held (Morph view) | Same as 5 |
| 7 | Hold steps → TRACK → write `shf` → SHIFT+COPY (steps still held) → turn the `shf` pot | Removed from the held steps only; after the rescan, the name stays underlined if non-held steps still carry `shf` |
| 8 | Step-automation editor (`scn`): add `len` on one step, TRACK back to track settings; then delete it and return | `len` name underlined, then not underlined |
| 9 | Change track, then change the shown Scene, on the track settings page | Underlines follow the shown track and Scene; no stale marks |
| 10 | SHIFT press/release with underlines shown | Underlines stay (no rescan blank) |
| 11 | Subpage 1 (vel/note/prob), SHIFT+COPY + turn a pot | Nothing cleared |
| 12 | Second half (play mode/MIDI ch/note), SHIFT+COPY + turn a pot | Nothing cleared |
| 13 | Copy/paste or clear a track that carries `scl` automation while on the track settings page | Underline updates after the job completes |
| 14 | Regression: VOICE, Effect, PERF pot clears and underlines; VOICE held-step overlay | Unchanged |
