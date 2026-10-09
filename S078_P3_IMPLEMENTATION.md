# S078 P3 — Implementation Schedule: Track Parameter Automation Follow-up

Source plan: `S078_P3_TRACK_PAR_AUTOM_FOLLOWUP.md` (F1.1–F1.8, F2.1–F2.2).

This schedule lists every code change needed to fix:

- **Bug 1:** no top-row name underline for automated `len`/`scl`/`shf` on the
  STEP track settings page.
- **Bug 2:** SHIFT+COPY + pot does not clear track-parameter automation.

Each change gives the file, line, action (ADD / REMOVE / MODIFY), the current
text (where it is replaced), and the new text. Every new block carries its
in-place documentation comment.

---

## 0. Ground rules

- **Baseline.** Line numbers are those of the current working tree on
  `dev-ph6-cleanup`: S078 P2 applied and uncommitted, base commit `0c23def`.
  Verify each anchor text before editing.
- **Apply bottom-up.** Apply the changes in each file from the highest line
  number to the lowest (M26 → M1, then H3 → H1), so cited line numbers stay
  valid while editing. The schedule below is listed top-down for reading.
- **RAM (approval policy).** No new allocation of any kind. Three spare bits
  of the existing 1-byte `va_searchSceneMask` (static SRAM, Menu-owned, life
  of the firmware) carry the new result. Removing `sa_trackPresenceMask`
  **releases 1 B** of static SRAM (`.bss`). Per policy, that byte is not
  reused for anything else. The new helper `va_seqTrackSearchBit()` uses one
  pointer local and no buffers, so the stack does not grow. The
  `_Static_assert` of 46 B at menu.c:1475–1478 is unaffected.
- **No changes to:** `buttonHandler.c`, `copyClearSession.c/h`,
  `copyClearService.c`, `clearOps.c`, PatternData, PatternStackService,
  SceneModTargets, presetMorphEngine, or the sequencer.

---

## 1. Deviations from the P3 plan (decided during the deep dive)

| Plan item | Change | Reason |
|-----------|--------|--------|
| F1.4, last row: restart the search in `menu_showStepTrackSettingsFirstHalf()` / `menu_toggleStepTrackSettingsHalf()` when returning to subpage 0 | **Replaced** by restarts at the step-automation editor's mutation sites: M21 `menu_stepAutomationReplaceTarget()` and M22 `menu_stepAutomationExecuteItem0()` | The editor is the only SEQ_PAGE writer of track targets other than the overlay. `menu_stepAutomationPageActive()` (menu.c:10046) requires `SEQ_PAGE`, and every target change goes through `menu_stepAutomationReplaceTarget()`, so restarting on the data change covers every way back to subpage 0. The search now runs on every SEQ subpage (M24), so it has finished before the user returns. This also fixes the two existing `if (menu_isVoicePage(menu_activePage))` guards at :10762/:10767, which can never be true because the editor never runs on a voice page |
| F1.2 and F1.6: inline descriptor switch / loop over `menu_seqCellToTrackTarget()` | **One shared helper**, `va_seqTrackSearchBit()` (M6) | Setting the bit (scan) and clearing it (pot clear) use one predicate, the same pattern as `va_sceneSearchBitForCell()` |
| F1.4: widen the Effect arm of `menu_setShownPattern()` | **Separate SEQ_PAGE arm** (M26) | The Effect arm's comment ("SEQ row belongs to the FX sequencer") does not apply to STEP mode |

---

## 2. Change index

### `Core/Menu/menu.c`

| ID | Lines | Action | Function / object | Plan |
|----|-------|--------|-------------------|------|
| M1 | 1378–1383 | MODIFY comment | va_* state budget block | F1.1 |
| M2 | 1418–1431 | MODIFY comment, ADD macro | `va_searchSceneMask` / `VA_SEARCH_SCENE_TRACK_BIT()` | F1.1 |
| M3 | 1480–1502 | REMOVE | `sa_trackPresenceMask` + comment | F1.8 |
| M4 | 2013 | REMOVE | forward declaration of `sa_refreshPresence()` | F1.8 |
| M5 | 2048–2069 | MODIFY comment | `va_searchRestart()` header | F1.4 |
| M6 | after 2122 | ADD | `va_seqTrackSearchBit()` | F1.2/F1.6 |
| M7 | 2163–2188 | MODIFY comment | `va_scanService()` header | F1.2 |
| M8 | 2193 | ADD | `va_scanService()`: `seq_page` local | F1.2 |
| M9 | after 2223 | ADD | `va_scanService()`: SEQ classification | F1.2 |
| M10 | 2387–2389 | REMOVE | `va_updateHeldState()` overlay exit | F1.8 |
| M11 | 2398–2409 | MODIFY | `va_updateHeldState()` mask change | F1.8 |
| M12 | 2430–2435 | REMOVE | `va_resetOverlay()` | F1.8 |
| M13 | 2480–2500 | MODIFY | `menu_patternContentChanged()` | F1.4 |
| M14 | 2506–2512, 2581 | MODIFY comment, ADD arm | `menu_automationTargetCleared()` | F1.6 |
| M15 | 3516–3518 | MODIFY comment | `menu_seqCellToTrackTarget()` affiliates | F1.8 |
| M16 | 3549–3577 | REMOVE | `sa_refreshPresence()` | F1.8 |
| M17 | 3588–3590, 3642–3646 | MODIFY | `sa_writeAutomationFromKnob()` | F1.5 |
| M18 | 3654–3665, 3685–3691 | MODIFY | `sa_clearAutomationFromKnob()` | F1.5 |
| M19 | 3732–3856 | MODIFY (replace) | `sa_applyTrackMarkers()` | F1.7 |
| M20 | 3858–3872, 3894, 3899 | MODIFY comment, REMOVE | `menu_enterStepTrackAutomationOverlay()` | F1.8 |
| M21 | 10338–10376 | MODIFY | `menu_stepAutomationReplaceTarget()` | F1.4 (deviation) |
| M22 | 10759–10769 | MODIFY | `menu_stepAutomationExecuteItem0()` | F1.4 (deviation) |
| M23 | 12662–12670, 12708 | MODIFY comment, ADD | `menu_knobClearTarget()` | F2.1 |
| M24 | 12955–12986 | MODIFY | `menu_serviceRuntimeWidgets()` | F1.3 |
| M25 | 14321–14332 | ADD | `menu_switchPage()`, SEQ_PAGE entry | F1.4 |
| M26 | 15300–15306, after 15330 | MODIFY comment, ADD arm | `menu_setShownPattern()` | F1.4 |

### `Core/Menu/menu.h`

| ID | Lines | Action | Declaration |
|----|-------|--------|-------------|
| H1 | 519–533 | MODIFY comment | `menu_patternContentChanged()` |
| H2 | 535–545 | MODIFY comment | `menu_automationTargetCleared()` |
| H3 | 571–587 | MODIFY comment | `menu_enterStepTrackAutomationOverlay()` |

### Specification

| ID | File | Lines | Action |
|----|------|-------|--------|
| D1 | `knowledge_files/specification_reference/COPYCLEAR_UTILITIES.md` | 439–451 | ADD bullet + STEP table row |
| D2 | same | 1054 | MODIFY test row |

---

## 3. `Core/Menu/menu.c` changes

### M1 — va_* state budget comment (menu.c:1378–1383, MODIFY)

**Current** (1378–1383):

```c
 * PERF-page sharing (S077 P6, +1 B): the PERF page uses the same held/search
 * machinery but needs independent per-voice morph presence, which the
 * single-slot va_searchSceneMask cannot express, so it adds one dedicated
 * mask byte (va_searchPerfMorphMask). The page is mutually exclusive with
 * VOICE/Effect and every PERF entry restarts the search.
 */
```

**New:**

```c
 * PERF-page sharing (S077 P6, +1 B): the PERF page uses the same held/search
 * machinery but needs independent per-voice morph presence, which the
 * single-slot va_searchSceneMask cannot express, so it adds one dedicated
 * mask byte (va_searchPerfMorphMask). The page is mutually exclusive with
 * VOICE/Effect and every PERF entry restarts the search.
 *
 * STEP-page sharing (S078 P2 §B held overlay, S078 P3 search, +0 B): the
 * SEQ_PAGE track-settings view reuses the held/marker/debounce/working-value
 * bytes for its held-step overlay and the 13 search bytes for an active-track
 * search of the three track timing targets (length, scale, shuffle), whose
 * result is VA_SEARCH_SCENE_TRACK_BIT(0..2) in va_searchSceneMask. SEQ_PAGE is
 * mutually exclusive with the VOICE, Effect and PERF pages, and every SEQ
 * entry from another page restarts the search (menu_switchPage()). S078 P3
 * removed the former 1 B STEP presence byte (sa_trackPresenceMask), whose only
 * reader was unreachable.
 */
```

### M2 — Search bits for the STEP track cells (menu.c:1418–1431, MODIFY comment + ADD macro)

**Current comment** (1418–1430, the block above `#define VA_SEARCH_SCENE_VOICE_MORPH_BIT`):

```c
/*
 * Pattern-wide Scene-target automation search result (+1 B static SRAM).
 *
 * What: on VOICE pages, one bit each for the Voice Morph, Audio Out, and FX
 * Send targets of the active VOICE slot; on the Effect page (S074), one bit
 * for the Scene Effect Morph target `fxm` (ID 404), shown on the `mrp` cell.
 * Why: Scene target IDs occupy block 6 (384..447) and cannot be represented
 * by va_searchTargetMask[], whose bits are locals 0..63. Inputs:
 * va_scanService() entries (VOICE: the active track; Effect: all tracks).
 * Outputs: va_applyVoiceMarkers() and menu_effectCellAutomated() underline
 * the matching name after the bounded search completes. Lifetime: the current
 * search context; cleared by va_searchRestart(). Affiliate:
 * sceneModTarget_descriptor().
 */
```

**New comment:**

```c
/*
 * Pattern-wide Scene-target automation search result (+1 B static SRAM).
 *
 * What: on VOICE pages, one bit each for the Voice Morph, Audio Out, and FX
 * Send targets of the active VOICE slot; on the Effect page (S074), one bit
 * for the Scene Effect Morph target `fxm` (ID 404), shown on the `mrp` cell;
 * on the STEP track-settings page (SEQ_PAGE, S078 P3), one bit each for the
 * track length, scale, and shuffle targets of the active track
 * (VA_SEARCH_SCENE_TRACK_BIT(0..2)), shown on the `len`/`scl`/`shf` cells.
 * Why: Scene target IDs occupy block 6 (384..447) and cannot be represented
 * by va_searchTargetMask[], whose bits are locals 0..63. Inputs:
 * va_scanService() entries (VOICE and SEQ: the active track; Effect: all
 * tracks). Outputs: va_applyVoiceMarkers(), menu_effectCellAutomated(), and
 * sa_applyTrackMarkers() underline the matching name after the bounded search
 * completes. Lifetime: the current search context; cleared by
 * va_searchRestart(). Affiliates: sceneModTarget_descriptor(),
 * va_sceneSearchBitForCell(), va_seqTrackSearchBit().
 */
```

The four existing `#define`s at 1427–1431 are unchanged. **ADD after
line 1431** (`#define VA_SEARCH_SCENE_EFFECT_MORPH_BIT 0x08u`), before
`static uint8_t va_searchSceneMask = 0u;`:

```c
/*
 * SEQ_PAGE only: Pattern automation of the active track's timing targets
 * (S078 P3).
 *
 * What: one bit per automatable SEQ subpage-0 cell, in cell order:
 * cell 0 `len` = 0x10, cell 1 `scl` = 0x20, cell 2 `shf` = 0x40. Why: the
 * STEP track-settings page underlines a parameter name when its target is
 * stored anywhere on the shown track (S074 rule: the underline reports
 * stored data), and these block-6 Scene targets (405..425) have no place in
 * va_searchTargetMask[]. The bits sit above the VOICE (0x01..0x04) and Effect
 * (0x08) bits, so a stale cross-page result can never alias a STEP cell,
 * although every page entry restarts the search anyway. Input: a SEQ
 * subpage-0 cell position 0..2 (callers guarantee the range by first
 * resolving a valid target through menu_seqCellToTrackTarget() or
 * va_seqTrackSearchBit()). Output: the bit mask for va_searchSceneMask.
 * Accessors: va_seqTrackSearchBit() (set and clear predicate),
 * sa_writeAutomationFromKnob() (immediate set on a held write),
 * sa_applyTrackMarkers() (reader). Affiliates: va_scanService(),
 * menu_automationTargetCleared().
 */
#define VA_SEARCH_SCENE_TRACK_BIT(cell) ((uint8_t)(0x10u << (cell)))
```

### M3 — Remove `sa_trackPresenceMask` (menu.c:1480–1502, REMOVE)

**Remove** the whole comment block that starts at 1480
(`/* STEP held-step track automation overlay (S078 P2 §B), +1 B static SRAM.`),
the declaration at 1501 (`static uint8_t sa_trackPresenceMask = 0u;`), and
the blank line at 1502. The next line kept is 1503
(`/* Declared here so a CGRAM/DDRAM transaction can retire ...`).

**Removal rationale** (for the commit/handoff, not for the source): the byte
was only read by the `else if` name branch of `sa_applyTrackMarkers()`. Its
bits were set only when `va_resolveHeldValue()` succeeded for the same target,
which is the condition of the `if` above that branch, so the branch could
never run (P3 Bug 1a). Its job moves to VA_SEARCH_SCENE_TRACK_BIT() in
`va_searchSceneMask` (M2). This releases 1 B of static SRAM.

### M4 — Remove the forward declaration (menu.c:2013, REMOVE)

**Remove** line 2013:

```c
static void sa_refreshPresence(void);
```

The block comment at 2008–2010 and the other `sa_*` declarations
(2011–2012, 2014–2017) stay.

### M5 — `va_searchRestart()` header (menu.c:2048–2069, MODIFY comment)

**Replace** the comment block that ends at 2069 (the line before
`static void va_searchRestart(void)` at 2071; it begins with
`/*` / ` * Restart the asynchronous automation-presence search.`) with:

```c
/*
 * Restart the asynchronous automation-presence search.
 *
 * What: records the viewed Pattern, sets the track context, clears every
 * presence mask and the completion flag, and resumes at step zero. VOICE
 * pages and the STEP track-settings page (SEQ_PAGE, S078 P3) record
 * menu_activeVoice as the one track to scan; the Effect page (S074) and the
 * PERF page (S077 P6) start their seven-track cursor at track 0.
 * Why: a result from another Pattern, track, voice slot, or page must never
 * produce a stale name underline. VOICE, SEQ, Effect, and PERF share this
 * state (0 B), so the restart is the single place that selects the page's
 * scan geometry; callers must therefore set menu_activePage before calling
 * it.
 * Inputs: menu_activePage, menu_shownPattern, menu_activeVoice. Outputs:
 * cleared va_search* state; markers from the search stay absent until
 * va_scanService() completes the new search (FX-lock underlines on the Effect
 * page do not depend on it). Callers: VOICE, Effect, PERF, and SEQ entry in
 * menu_switchPage(), menu_setActiveVoice() (not on the Effect page),
 * menu_setShownPattern(), menu_patternContentChanged(), the STEP
 * automation editor's target changes and deletes
 * (menu_stepAutomationReplaceTarget(), menu_stepAutomationExecuteItem0()),
 * the STEP overlay's held-step clear (sa_clearAutomationFromKnob()), and
 * va_scanService() on a context mismatch.
 * Affiliates: va_scanService(), va_searchSetBit(), va_seqTrackSearchBit(),
 * va_applyVoiceMarkers(), sa_applyTrackMarkers(), menu_effectCellAutomated().
 */
```

The body (2071–2081) is unchanged. SEQ_PAGE falls into the existing
`: menu_activeVoice` branch at 2073–2074.

### M6 — ADD `va_seqTrackSearchBit()` (menu.c, after 2122)

**ADD after line 2122** (the closing `}` of `va_sceneSearchBitForCell()`),
before the blank line and the `va_searchRecordEffectTarget()` comment at 2124:

```c

/*
 * Resolve the SEQ_PAGE search bit for one stored track-timing target
 * (S078 P3).
 *
 * What: returns VA_SEARCH_SCENE_TRACK_BIT(0), (1), or (2) when `target` is
 * the track length, scale, or shuffle Scene target of `track`, and zero for
 * every other stored target: voice descriptors, other Scene targets, Effect
 * targets, another track's timing targets, and the
 * PAT_AUTOMATION_TARGET_OFF sentinel (0x1FF is not a Scene target).
 * Why: the STEP track-settings page underlines `len`/`scl`/`shf` from the
 * Pattern-wide search. The bit must come from one predicate both when the
 * search sets it (va_scanService()) and when a pot clear drops it
 * (menu_automationTargetCleared()), so set and clear stay symmetric, as
 * va_sceneSearchBitForCell() keeps them for the VOICE Scene cells. The
 * kind + voice_slot test is the same validation that
 * menu_seqCellToTrackTarget() applies, so the bit order equals the SEQ
 * subpage-0 cell order (0 len, 1 scl, 2 shf).
 * Inputs: a stored 9-bit Pattern target and the track 0..6 being searched or
 * shown. Output: one bit of va_searchSceneMask, or 0. Cost: one descriptor
 * lookup; no state.
 * Accessors: va_scanService() (SEQ_PAGE classification) and
 * menu_automationTargetCleared() (SEQ_PAGE arm).
 * Affiliates: sceneModTarget_isSceneTarget(), sceneModTarget_descriptor(),
 * SCENE_MOD_TARGET_KIND_TRACK_* (SceneModTargets.h), menu_seqCellToTrackTarget(),
 * sa_applyTrackMarkers().
 */
static uint8_t va_seqTrackSearchBit(uint16_t target, uint8_t track)
{
    const scene_mod_target_descriptor_t *descriptor;

    if (!sceneModTarget_isSceneTarget(target))
        return 0u;
    descriptor = sceneModTarget_descriptor(target);
    if (!descriptor || descriptor->voice_slot != track)
        return 0u;
    switch (descriptor->kind) {
    case SCENE_MOD_TARGET_KIND_TRACK_LENGTH:
        return VA_SEARCH_SCENE_TRACK_BIT(0u);
    case SCENE_MOD_TARGET_KIND_TRACK_SCALE:
        return VA_SEARCH_SCENE_TRACK_BIT(1u);
    case SCENE_MOD_TARGET_KIND_TRACK_SHUFFLE:
        return VA_SEARCH_SCENE_TRACK_BIT(2u);
    default:
        return 0u;
    }
}
```

No forward declaration is needed: both callers (va_scanService at 2189, and
menu_automationTargetCleared at 2513) come after this definition.

### M7 — `va_scanService()` header (menu.c:2163–2188, MODIFY comment)

**Replace** the comment block that ends at 2188 (it begins
` * Advance the automation-presence search by the configured bounded slice.`)
with:

```c
/*
 * Advance the automation-presence search by the configured bounded slice.
 *
 * What: reads at most VOICE_AUTOMATION_SCAN_STEPS_PER_PASS step lists of the
 * viewed Pattern and records the targets that the current page underlines.
 *   - VOICE page: the active track only; voice descriptor targets and
 *     per-voice Scene targets owned by the page's slot. Done after 128 steps
 *     (32 passes).
 *   - SEQ_PAGE, the STEP track settings (S078 P3): the active track only, with
 *     the same geometry and track-mismatch restart as a VOICE page; the
 *     track's length/scale/shuffle Scene targets set
 *     VA_SEARCH_SCENE_TRACK_BIT(0..2) in va_searchSceneMask through
 *     va_seqTrackSearchBit(). Done after 128 steps (32 passes).
 *   - Effect page (S074): all seven tracks, one after another through
 *     va_searchTrack; Effect parameter targets and `fxm`, classified by
 *     va_searchRecordEffectTarget(). Done after 896 steps (224 passes).
 *   - PERF page (S077 P6): all seven tracks, one after another through
 *     va_searchTrack; Scene voice morph targets set the per-voice bit in
 *     va_searchPerfMorphMask, and Scene effect morph sets bit 6.
 *     va_searchTargetMask[] is unused. Done after 896 steps (224 passes).
 * Why: scanning a Pattern synchronously on every repaint would stall the UI.
 * One function serves all four modes so the 252-byte entry buffer exists
 * once on the stack (S074/S077 P6/S078 P3 add no stack). Inputs: the current
 * search context, menu_activePage, and the PatternData pool. Output: complete
 * presence masks and one menu_repaint() when the last step is read, with a
 * hard 4*63 comparison ceiling per service pass on any page. A Pattern change
 * (and, on VOICE and SEQ pages, a track change) restarts the search, but only
 * while the search is incomplete: a completed result returns at the top, so
 * every context change must also call va_searchRestart() explicitly. Caller:
 * menu_serviceRuntimeWidgets() on VOICE, SEQ, Effect, and PERF pages.
 * Affiliates: instrumentParam namespace, sceneModTarget_descriptor(),
 * va_searchRecordEffectTarget(), va_seqTrackSearchBit(), and PatternData.
 */
```

### M8 — `va_scanService()`: `seq_page` local (menu.c:2193, ADD)

**ADD after line 2193**
(`    uint8_t perf_page = (uint8_t)(menu_activePage == PERFORMANCE_PAGE);`):

```c
    /* S078 P3: STEP track settings - active-track search of the three track
     * timing targets; VOICE geometry, so the mismatch test below applies. */
    uint8_t seq_page = (uint8_t)(menu_activePage == SEQ_PAGE);
```

The mismatch test at 2199–2200 is **unchanged**. SEQ_PAGE is neither
`effect_page` nor `perf_page`, so a track change restarts its search exactly
as on a VOICE page.

### M9 — `va_scanService()`: SEQ classification (menu.c, after 2223, ADD)

**ADD after line 2223** (the `}` closing `if (effect_page) { ... continue; }`),
before the PERF comment at 2224:

```c
            /*
             * STEP track settings (S078 P3): classify only the active track's
             * length/scale/shuffle Scene targets.
             *
             * What: ORs VA_SEARCH_SCENE_TRACK_BIT(0..2) into va_searchSceneMask
             * for each matching stored entry; every other target contributes
             * 0 and is skipped. Why: the `len`/`scl`/`shf` names must be
             * underlined whenever that target is stored on any of the track's
             * 128 steps, whether or not steps are held (P3 Bug 1b - nothing
             * searched SEQ_PAGE before). The `continue` keeps the VOICE
             * classification below from running with the meaningless SEQ_PAGE
             * slot value from menu_voicePageToSlot(). Inputs: the decoded entry
             * and va_searchTrack (== menu_activeVoice at restart). Output:
             * va_searchSceneMask bits, read by sa_applyTrackMarkers() once
             * va_searchComplete is set. Entries waiting in the pot-clear
             * register were already skipped above (ccSvc_targetPending()).
             * Affiliates: va_seqTrackSearchBit(), menu_automationTargetCleared(),
             * sa_writeAutomationFromKnob().
             */
            if (seq_page) {
                va_searchSceneMask |=
                    va_seqTrackSearchBit(autos[i].target, va_searchTrack);
                continue;
            }
```

The track-advance block after the loop (`if ((effect_page || perf_page) && ...)`)
is **unchanged**. SEQ_PAGE never advances its track, so the search completes
when `va_searchCursor` reaches `NUM_STEPS`.

### M10 — `va_updateHeldState()` overlay exit (menu.c:2387–2389, REMOVE)

**Remove** lines 2387–2389:

```c
        /* S078 P2 §B: the STEP overlay's compact presence mask belongs to the
         * released held set and must not survive into the next entry. */
        sa_trackPresenceMask = 0u;
```

Lines 2384–2386 and 2390–2396 are unchanged. After the overlay exits,
`sa_applyTrackMarkers()` (M19) keeps underlining names from the search
result, so there is no presence state to drop.

### M11 — `va_updateHeldState()` mask change (menu.c:2398–2409, MODIFY)

**Current** (2397–2409):

```c
        /*
         * S078 P2 §B: the same held-mask bookkeeping serves both overlays, so
         * the LED refresh is page-dispatched. STEP runs on SEQ_PAGE and shows
         * per-step track-parameter automation; VOICE runs on voice pages.
         */
        if (menu_activePage == SEQ_PAGE)
        {
            /* The held set changed, so re-derive which of the three track
             * cells still have a held automation source before repainting. */
            sa_refreshPresence();
            sa_refreshAutomationLeds();
        } else
            va_refreshAutomationLeds();
```

**New:**

```c
        /*
         * S078 P2 §B / P3: the same held-mask bookkeeping serves both
         * overlays, so the LED refresh is page-dispatched. STEP runs on
         * SEQ_PAGE and shows per-step track-parameter automation; VOICE runs
         * on voice pages. No STEP presence state needs re-deriving here: the
         * held-value markers are resolved live by sa_applyTrackMarkers()
         * through va_resolveHeldValue() on the repaint below, and the name
         * markers come from the Pattern-wide search, which does not depend
         * on the held set. Affiliates: sa_refreshAutomationLeds(),
         * va_refreshAutomationLeds(), sa_applyTrackMarkers().
         */
        if (menu_activePage == SEQ_PAGE)
            sa_refreshAutomationLeds();
        else
            va_refreshAutomationLeds();
```

`menu_repaint();` at 2410 is unchanged.

### M12 — `va_resetOverlay()` (menu.c:2430–2435, REMOVE)

**Remove** lines 2430–2435:

```c
    /*
     * S078 P2 §B: the STEP overlay shares this reset because it shares the
     * held/marker state. Clearing the compact track presence mask here keeps
     * a stale length/scale/shuffle underline from surviving a page change.
     */
    sa_trackPresenceMask = 0u;
```

The function ends at `va_cgramValid = 0u;` (2429). Stale STEP name underlines
across a page change are now prevented by the search restart on every page
entry (M25 for SEQ; the existing VOICE, Effect and PERF entries).

### M13 — `menu_patternContentChanged()` (menu.c:2480–2500, MODIFY)

**Current header** (2480–2495, the block above `void menu_patternContentChanged(void)`):
replace the sentence (2483–2486)

```c
 * debounce while keeping the held-step context, and repaints. Runs on VOICE
 * pages (active-track search), and, since S074, on the Effect page and, since
 * S077 P6, on the PERF page (both seven-track searches). Why: removing a
```

with

```c
 * debounce while keeping the held-step context, and repaints. Runs on VOICE
 * pages (active-track search), since S074 on the Effect page, since S077 P6
 * on the PERF page (both seven-track searches), and since S078 P3 on the STEP
 * track-settings page (SEQ_PAGE, active-track search of the length/scale/
 * shuffle targets, which a track clear, a step clear, a paste, or a
 * `len`/`scl`/`shf` pot clear (register completion) can change). Why: removing a
```

and replace (2491–2492)

```c
 * search result and a refreshed frame; other pages return at once because
 * their next VOICE/Effect entry restarts the search anyway. Callers:
```

with

```c
 * search result and a refreshed frame; other pages return at once because
 * their next VOICE/Effect/PERF/SEQ entry restarts the search anyway. Callers:
```

**Current code** (2498–2500):

```c
    if (!menu_isScreenPage(menu_activePage) &&
        menu_activePage != PERFORMANCE_PAGE)
        return;
```

**New:**

```c
    /*
     * S078 P3: admit SEQ_PAGE. What: a completed copy/clear job on the viewed
     * Scene restarts the STEP active-track search. Why: va_scanService()
     * never re-validates a completed result, so without this restart a
     * pasted or cleared `len`/`scl`/`shf` target would keep its old name
     * underline (or miss a new one) until the next SEQ entry. Input: the
     * page shown when ccSvc_patternChangedUi() runs. Output: restart,
     * debounce reset, repaint. Accessor: ccSvc_patternChangedUi()
     * (copyClearService.c). Affiliates: va_searchRestart(),
     * va_scanService(), sa_applyTrackMarkers().
     */
    if (!menu_isScreenPage(menu_activePage) &&
        menu_activePage != PERFORMANCE_PAGE &&
        menu_activePage != SEQ_PAGE)
        return;
```

### M14 — `menu_automationTargetCleared()` (menu.c:2506–2512 comment, 2581 ADD arm)

**Current header** (2506–2512):

```c
/*
 * Drop one automation target's underline immediately after pot clear.
 *
 * Inputs: canonical Pattern target. Output: only the matching VOICE, Effect,
 * or PERF presence bit is cleared; the bounded search continues for all other
 * targets, and Menu repaints without changing the target value.
 */
```

**New header:**

```c
/*
 * Drop one automation target's underline immediately after pot clear.
 *
 * Inputs: canonical Pattern target. Output: only the matching VOICE, Effect,
 * PERF, or STEP track-settings (SEQ_PAGE, S078 P3) presence bit is cleared;
 * the bounded search continues for all other targets, and Menu repaints
 * without changing the target value. Accessor: ccClear_potTurned()
 * (clearOps.c) after ccSvc_registerAdd(); the target is then removed from
 * every step of the Scene, so dropping the bit outright is correct. A
 * held-step-only removal (sa_clearAutomationFromKnob()) must not use this
 * function and restarts the search instead. Affiliates: va_searchSceneMask,
 * va_searchTargetMask[], va_searchPerfMorphMask, va_seqTrackSearchBit(),
 * ccSvc_targetPending().
 */
```

**Current** (2580–2582):

```c
        }
    }
    menu_repaint();
```

The `}` at 2581 closes `} else if (menu_activePage == PERFORMANCE_PAGE) {`.
**MODIFY line 2581** from `    }` to the following, so the new arm chains
after the PERF arm:

```c
    } else if (menu_activePage == SEQ_PAGE) {
        /*
         * STEP track settings (S078 P3).
         *
         * What: drops the `len`/`scl`/`shf` presence bit of the active track
         * when its target enters the pot-clear register. Why: the pot clear
         * (menu_knobClearTarget() SEQ arm, Bug 2 fix) removes the target from
         * all 128 steps of the track in the background, and spec §8 requires
         * the underline to go at the turn; the scan cannot set the bit again
         * while the target waits (ccSvc_targetPending()). A non-track target,
         * or another track's target, maps to 0, so the AND leaves the mask
         * unchanged. Inputs: target, menu_activeVoice. Output:
         * va_searchSceneMask. Affiliates: va_seqTrackSearchBit(),
         * ccClear_potTurned(), sa_applyTrackMarkers().
         */
        va_searchSceneMask &=
            (uint8_t)~va_seqTrackSearchBit(target, menu_activeVoice);
    }
```

`menu_repaint();` (2582) and the closing brace are unchanged.

### M15 — `menu_seqCellToTrackTarget()` affiliates (menu.c:3516–3518, MODIFY comment)

**Current:**

```c
 * automatable" instead of writing the wrong target. Affiliates:
 * sa_writeAutomationFromKnob(), sa_clearAutomationFromKnob(),
 * sa_refreshPresence(), sa_applyTrackMarkers(), sa_refreshAutomationLeds().
```

**New:**

```c
 * automatable" instead of writing the wrong target. Accessors:
 * sa_writeAutomationFromKnob(), sa_clearAutomationFromKnob(),
 * sa_applyTrackMarkers(), sa_refreshAutomationLeds(), menu_parseKnobDelta()
 * (STEP overlay intercept), menu_encoderChangeParameter(), and (S078 P3)
 * menu_knobClearTarget() for the SEQ_PAGE pot clear. Affiliate:
 * va_seqTrackSearchBit(), which applies the same kind/voice_slot validation
 * from the opposite direction (stored target -> cell bit).
```

### M16 — Remove `sa_refreshPresence()` (menu.c:3549–3577, REMOVE)

**Remove** lines 3549–3577: the comment block
(` * Refresh the compact track-automation presence mask.` …), the function
`static void sa_refreshPresence(void) { ... }`, and the trailing blank line.
The next line kept is the `/*` at 3578 opening the
`sa_writeAutomationFromKnob()` header.

**Removal rationale:** its only output was `sa_trackPresenceMask` (M3), and
its test was a copy of the value-branch condition in `sa_applyTrackMarkers()`.
The callers removed or replaced are M11 (2406), M18 (3688) and M20 (3899).

### M17 — `sa_writeAutomationFromKnob()` (menu.c:3588–3590 comment, 3642–3646 code, MODIFY)

**Header, current** (3588–3590):

```c
 * on SEQ_PAGE subpage 0) and a signed delta. Output:
 * patSvc_writeStepAutomation() for every held step, the working-value cache,
 * the presence bit, edit-flash suppression, and the coalesced repaint flag.
```

**Header, new:**

```c
 * on SEQ_PAGE subpage 0) and a signed delta. Output:
 * patSvc_writeStepAutomation() for every held step, the working-value cache,
 * the SEQ search bit VA_SEARCH_SCENE_TRACK_BIT(cell) in va_searchSceneMask
 * (S078 P3), edit-flash suppression, and the coalesced repaint flag.
```

**Code, current** (3642–3646):

```c
    if (wrote) {
        /* Set the presence bit now so the underline appears without waiting
         * for a rescan, and hold the edit-flash suppression exactly like the
         * VOICE overlay. */
        sa_trackPresenceMask |= (uint8_t)(1u << knob_idx);
```

**Code, new:**

```c
    if (wrote) {
        /*
         * S078 P3: mark the SEQ_PAGE search result now, exactly as the VOICE
         * held write sets its search bit (va_searchSetBit()). What: ORs this
         * cell's VA_SEARCH_SCENE_TRACK_BIT into va_searchSceneMask. Why: a
         * completed search is never re-run by va_scanService(), and a running
         * one may already have passed the held steps (or the write may still
         * sit in the patSvc FIFO), so without this bit the `len`/`scl`/`shf`
         * name would not underline when the steps are released. knob_idx ==
         * cellPos here, because only cells 0..2 resolve to a valid target.
         * Then hold the edit-flash suppression exactly like the VOICE overlay.
         * Reader: sa_applyTrackMarkers() (name branch). Affiliates:
         * va_scanService(), va_searchRestart().
         */
        va_searchSceneMask |= VA_SEARCH_SCENE_TRACK_BIT(knob_idx);
```

Lines 3647–3651 (`va_underlineSuppressed |= ...`, `va_lastEditTick`,
`menu_knobs_dirty`) are unchanged.

### M18 — `sa_clearAutomationFromKnob()` (menu.c:3654–3665 comment, 3685–3691 code, MODIFY)

**Header, new** (replaces 3654–3665 in full):

```c
/*
 * Remove one track parameter's automation from every held step (S078 P2 §B).
 *
 * What: called when the STEP overlay intercepts a copy/clear clear-mode pot
 * turn while SEQ steps are held; removes the resolved track target from each
 * held step, restarts the active-track search, and refreshes LEDs and the
 * frame. Why: the copy/clear default pot action clears a whole track (all
 * 128 steps), which would over-clear when only some steps are held. Because
 * only the held steps lose the target, other steps may still carry it, so the
 * name underline must be found again by a rescan, not dropped
 * (menu_automationTargetCleared() is for whole-track clears only, S078 P3).
 * Inputs: absolute cell position (0..7). Output: per-held-step automation
 * removal, a restarted search, LED refresh, and a repaint. Accessor:
 * menu_parseKnobDelta() STEP overlay intercept. Affiliates:
 * copyClear_isClearMode(), patSvc_removeStepAutomation(), va_searchRestart(),
 * sa_refreshAutomationLeds(), sa_applyTrackMarkers().
 */
```

**Code, current** (3685–3691):

```c
    /* Drop the working-value validity so the display re-seeds from the
     * retained value now that the held automation is gone. */
    va_underlineSuppressed &= (uint8_t)~(uint8_t)(0x10u << knob_idx);
    sa_refreshPresence();
    sa_refreshAutomationLeds();
    menu_automationTargetCleared(target);
    menu_repaint();
```

**Code, new:**

```c
    /* Drop the working-value validity so the display re-seeds from the
     * retained value now that the held automation is gone. */
    va_underlineSuppressed &= (uint8_t)~(uint8_t)(0x10u << knob_idx);
    /*
     * S078 P3: restart the SEQ_PAGE search instead of dropping the bit.
     * What: clears the result and rescans the active track (32 passes). Why:
     * this clear covers only the held steps, so the `len`/`scl`/`shf` name
     * must stay underlined if any other step still carries the target; only
     * a full rescan can show that. The name stays blank until the rescan
     * completes, the same behaviour as the STEP automation editor's deletes.
     * Output: va_search* reset. Affiliates: va_scanService(),
     * menu_stepAutomationExecuteItem0().
     */
    va_searchRestart();
    sa_refreshAutomationLeds();
    menu_repaint();
```

Known limitation, accepted and the same as the existing editor deletes: a
removal still waiting in the patSvc FIFO (service busy) can be read by the
rescan before it runs. The name can then stay underlined until the next
restart.

### M19 — `sa_applyTrackMarkers()` (menu.c:3732–3856, MODIFY: replace whole function with its header)

**Replace** lines 3732–3856 (the header comment starting
` * Apply underline markers and held automation values for the STEP overlay.`
through the closing `}` of the function) with:

```c
/*
 * Apply underline markers for the STEP track-settings page (S078 P2 §B / P3).
 *
 * What: on SEQ_PAGE subpage 0, applies the VOICE one-marker-per-cell rule to
 * the three automatable track cells (`len`, `scl`, `shf`):
 *   - held value: while the held-step overlay is active and a held step
 *     carries the cell's target, the cell shows the newest held value
 *     (working value mid-edit) and that value is underlined (row 1);
 *   - name: otherwise, once the active-track search is complete and found
 *     the target anywhere on the track, the parameter name is underlined
 *     (row 0; overview: first character of the short name; clicked-in view:
 *     first character of the long name at columns 8..15).
 * Non-automatable cells (play mode, MIDI channel, MIDI note) never mark.
 * Why: S078 P3 Bug 1 - the former name branch depended on a presence byte
 * set only when the value branch already applied, so it never ran, and the
 * pass returned without the overlay, so automated track parameters were never
 * underlined on the top row. The S074 rule holds here too: the underline
 * reports stored data, whatever the transport, track length, or held state.
 * Inputs: menuIndex, editModeActive, va_overlayActive with the shared held
 * list and working values, va_searchComplete/va_searchSceneMask, and
 * editDisplayBuffer as formatted by menu_repaintGeneric(). Output: one
 * va_queueMarkerTransaction() per SEQ subpage-0 frame (also with no markers,
 * so stale CGRAM marks are retired). Accessor: menu_repaintGeneric() (after
 * the VOICE/Effect/PERF marker passes, which return at once on SEQ_PAGE).
 * Affiliates: menu_seqCellToTrackTarget(), va_resolveHeldValue(),
 * va_formatValue3(), VA_SEARCH_SCENE_TRACK_BIT(), va_scanService(),
 * va_queueMarkerTransaction(), va_applyVoiceMarkers() (the reference rule).
 */
static void sa_applyTrackMarkers(void)
{
    uint8_t glyph_probe[8];
    uint8_t desired_base[4] = { 0u, 0u, 0u, 0u };
    uint8_t marker_row[4] = { 0u, 0u, 0u, 0u };
    uint8_t marker_col[4] = { 0u, 0u, 0u, 0u };
    uint8_t desired_valid = 0u;
    uint8_t activePage;
    uint8_t is2ndPage;

    /* S078 P3: runs on SEQ_PAGE with or without the held-step overlay; the
     * held-value branches below test va_overlayActive themselves. */
    if (menu_activePage != SEQ_PAGE)
        return;

    activePage = (uint8_t)((menuIndex & MASK_PAGE) >> PAGE_SHIFT);
    if (activePage != 0u)
        return; /* Only subpage 0 (track settings) has automatable cells. */
    is2ndPage = (uint8_t)(((menuIndex & MASK_PARAMETER) > 3u) ? 4u : 0u);

    if (editModeActive) {
        uint8_t pos = (uint8_t)(menuIndex & MASK_PARAMETER);
        uint8_t knob_idx = (uint8_t)(pos & 3u);
        uint8_t suppress_bit = (uint8_t)(1u << knob_idx);
        uint8_t validity_bit = (uint8_t)(suppress_bit << 4u);
        uint8_t value7;
        instrument_param_id_t target =
            menu_seqCellToTrackTarget(pos, menu_activeVoice);

        if (target == INSTRUMENT_PARAM_INVALID) {
            /* Non-automatable cell: no marker. */
        } else if (va_overlayActive &&
                   va_resolveHeldValue(target, &value7)) {
            menu_cell_t cell = menu_resolveCell(activePage, pos);
            char val_text[3];
            uint8_t display_val =
                (va_underlineSuppressed & validity_bit)
                    ? va_workingValue[knob_idx]
                    : value7;
            int8_t right;

            va_formatValue3(&cell, display_val, val_text);
            memcpy(&editDisplayBuffer[1][13], val_text, 3u);
            if ((va_underlineSuppressed & suppress_bit) == 0u) {
                for (right = 15; right >= 13 &&
                     editDisplayBuffer[1][right] == ' '; right--)
                    ;
                if (right >= 13 && lcd_underlineGlyph(
                        (uint8_t)editDisplayBuffer[1][right], glyph_probe)) {
                    desired_base[0] = (uint8_t)editDisplayBuffer[1][right];
                    marker_row[0] = 1u;
                    marker_col[0] = (uint8_t)right;
                    desired_valid = 0x01u;
                }
            }
        } else if (va_searchComplete &&
                   (va_searchSceneMask &
                    VA_SEARCH_SCENE_TRACK_BIT(pos)) != 0u &&
                   (va_underlineSuppressed & suppress_bit) == 0u) {
            /*
             * S078 P3 clicked-in name marker. What: underlines the first
             * character of the long name that menu_repaintGeneric() writes
             * at editDisplayBuffer[0][8..15] for static cells (columns 0..7
             * hold the category). Why: same name-marker rule as the VOICE
             * clicked-in view (va_applyVoiceMarkers()); `pos` is 0..2
             * because the target resolved. Output: CGRAM slot 0 request.
             */
            int8_t left;
            for (left = 8; left < 16 &&
                 editDisplayBuffer[0][left] == ' '; left++)
                ;
            if (left < 16 && lcd_underlineGlyph(
                    (uint8_t)editDisplayBuffer[0][left], glyph_probe)) {
                desired_base[0] = (uint8_t)editDisplayBuffer[0][left];
                marker_row[0] = 0u;
                marker_col[0] = (uint8_t)left;
                desired_valid = 0x01u;
            }
        }
    } else {
        uint8_t i;

        for (i = 0u; i < 4u; i++) {
            uint8_t pos = (uint8_t)(is2ndPage + i);
            uint8_t start = (uint8_t)(4u * i);
            uint8_t value7;
            instrument_param_id_t target =
                menu_seqCellToTrackTarget(pos, menu_activeVoice);
            uint8_t suppress_bit = (uint8_t)(1u << (pos & 3u));
            uint8_t validity_bit = (uint8_t)(suppress_bit << 4u);

            if (target == INSTRUMENT_PARAM_INVALID)
                continue;
            if (va_overlayActive && va_resolveHeldValue(target, &value7)) {
                menu_cell_t cell = menu_resolveCell(activePage, pos);
                char val_text[3];
                uint8_t display_val =
                    (va_underlineSuppressed & validity_bit)
                        ? va_workingValue[pos & 3u]
                        : value7;
                int8_t right;

                va_formatValue3(&cell, display_val, val_text);
                memcpy(&editDisplayBuffer[1][start], val_text, 3u);
                if ((va_underlineSuppressed & suppress_bit) == 0u) {
                    for (right = 2; right >= 0 && val_text[right] == ' '; right--)
                        ;
                    if (right >= 0 && lcd_underlineGlyph(
                            (uint8_t)val_text[right], glyph_probe)) {
                        desired_base[i] = (uint8_t)val_text[right];
                        marker_row[i] = 1u;
                        marker_col[i] = (uint8_t)(start + right);
                        desired_valid |= (uint8_t)(1u << i);
                    }
                }
            } else if (va_searchComplete &&
                       (va_searchSceneMask &
                        VA_SEARCH_SCENE_TRACK_BIT(pos)) != 0u &&
                       (va_underlineSuppressed & suppress_bit) == 0u) {
                /*
                 * S078 P3 overview name marker. What: underlines the first
                 * non-space character of the cell's short name on row 0 (the
                 * active cell may be upper-cased by upr_three(); the glyph
                 * table covers both cases). Why: replaces the unreachable
                 * sa_trackPresenceMask branch with the Pattern-wide search
                 * result, so the name marks with or without held steps.
                 */
                int8_t left;
                for (left = 0; left < 3 &&
                     editDisplayBuffer[0][start + left] == ' '; left++)
                    ;
                if (left < 3 && lcd_underlineGlyph(
                        (uint8_t)editDisplayBuffer[0][start + left],
                        glyph_probe)) {
                    desired_base[i] =
                        (uint8_t)editDisplayBuffer[0][start + left];
                    marker_row[i] = 0u;
                    marker_col[i] = (uint8_t)(start + left);
                    desired_valid |= (uint8_t)(1u << i);
                }
            }
        }
    }

    va_queueMarkerTransaction(desired_base, desired_valid,
                              marker_row, marker_col);
}
```

**Net differences from the current function:**

- Entry guard (3756): `!va_overlayActive ||` removed.
- Clicked-in value branch (3773–3774): restructured into an
  `invalid / held value / name` chain; the held-value branch is now gated by
  `va_overlayActive`.
- Clicked-in **name** branch: added (new).
- Overview value branch (3812): `va_overlayActive &&` added.
- Overview name branch (3834–3836): `sa_trackPresenceMask & (1u << pos)`
  replaced by `va_searchComplete && (va_searchSceneMask &
  VA_SEARCH_SCENE_TRACK_BIT(pos))`. The `(va_underlineSuppressed &
  suppress_bit) == 0u` term is kept. Suppression is only non-zero while the
  overlay is active, and it is cleared on overlay exit
  (`va_updateHeldState()`) and by `va_underlineService()`.
- Everything else, including the value formatting and the marker
  transaction, is unchanged.

### M20 — `menu_enterStepTrackAutomationOverlay()` (menu.c:3858–3872 comment, 3894 & 3899 REMOVE)

**Header, current** (3866–3871):

```c
 * pressed track 0..6). Output: menu_activePage == SEQ_PAGE, menuIndex on
 * subpage 0, va_overlayActive set, held order seeded from the physical mask,
 * presence and LEDs refreshed. Caller: handleVoiceButton() in buttonHandler.c
 * when buttonHandler_seqHeldMask() is nonzero. Affiliates:
 * menu_setActiveVoice(), menu_switchPage(), va_updateHeldState(),
 * sa_refreshPresence(), sa_refreshAutomationLeds().
```

**Header, new:**

```c
 * pressed track 0..6). Output: menu_activePage == SEQ_PAGE, menuIndex on
 * subpage 0, va_overlayActive set, held order seeded from the physical mask,
 * step LEDs refreshed. Name underlines come from the SEQ_PAGE active-track
 * search (S078 P3), which menu_setActiveVoice() (track change) or
 * menu_switchPage() (entry from another page) restarts when needed; held
 * value markers are resolved live by sa_applyTrackMarkers(). Caller:
 * handleVoiceButton() in buttonHandler.c when buttonHandler_seqHeldMask() is
 * nonzero. Affiliates: menu_setActiveVoice(), menu_switchPage(),
 * va_updateHeldState(), va_searchRestart(), sa_refreshAutomationLeds(),
 * sa_applyTrackMarkers().
```

**Remove** line 3894: `    sa_trackPresenceMask = 0u;`
**Remove** line 3899: `    sa_refreshPresence();`

The remaining body (3875–3893, 3895–3898, 3900–3901) is unchanged. No
search restart is added here. If the track changed, `menu_setActiveVoice()`
(3878) restarts the search. If the page changed, M25 restarts it. If neither
changed, the existing result is still valid.

### M21 — `menu_stepAutomationReplaceTarget()` (menu.c:10338–10376, MODIFY)

**Header, current** (10338–10346):

```c
/*
 * Atomically replace a page target while preserving uniqueness.
 *
 * Inputs: old/new canonical targets, page value, and current list count.
 * Output: the new target is written before the old one is removed whenever
 * capacity permits. At the 63-entry limit the old entry is temporarily removed
 * and restored on failure so a full block can still change target without an
 * intermediate duplicate. Affiliate: main-encoder target edits.
 */
```

**Header, new:**

```c
/*
 * Atomically replace a page target while preserving uniqueness.
 *
 * Inputs: old/new canonical targets, page value, and current list count.
 * Output: the new target is written before the old one is removed whenever
 * capacity permits. At the 63-entry limit the old entry is temporarily removed
 * and restored on failure so a full block can still change target without an
 * intermediate duplicate. On success the automation-presence search restarts
 * (S078 P3): this is the single path for every STEP-editor target change
 * (VOI/category cycling and PAR cycling in menu_stepAutomationEdit()), and a
 * changed target can add or remove a `len`/`scl`/`shf` name underline on the
 * STEP track-settings page. The editor only runs on SEQ_PAGE
 * (menu_stepAutomationPageActive()), so the restart selects the SEQ
 * active-track geometry, and the search, serviced on every SEQ subpage, has
 * finished before the user returns to the track settings. Accessor:
 * menu_stepAutomationEdit(). Affiliates: va_searchRestart(), va_scanService(),
 * patSvc_writeStepAutomation(), patSvc_removeStepAutomation().
 */
```

**Code, current** (10365–10374):

```c
    if (count < PAT_BLOCK_AUTO_COUNT_MASK) {
        if (!patSvc_writeStepAutomation(scene, track, step, new_target, value))
            return 0u;
        (void)patSvc_removeStepAutomation(scene, track, step, old_target);
        return 1u;
    }
    if (!patSvc_removeStepAutomation(scene, track, step, old_target))
        return 0u;
    if (patSvc_writeStepAutomation(scene, track, step, new_target, value))
        return 1u;
```

**Code, new:**

```c
    if (count < PAT_BLOCK_AUTO_COUNT_MASK) {
        if (!patSvc_writeStepAutomation(scene, track, step, new_target, value))
            return 0u;
        (void)patSvc_removeStepAutomation(scene, track, step, old_target);
        /* S078 P3: a target change can move a track-settings name marker. */
        va_searchRestart();
        return 1u;
    }
    if (!patSvc_removeStepAutomation(scene, track, step, old_target))
        return 0u;
    if (patSvc_writeStepAutomation(scene, track, step, new_target, value)) {
        /* S078 P3: a target change can move a track-settings name marker. */
        va_searchRestart();
        return 1u;
    }
```

The failure tail (10375–10376: restore `old_target`, `return 0u;`) is
unchanged. No restart there, because presence does not change. Value-only
edits (field 3, 10707) and Add (`PAT_AUTOMATION_TARGET_OFF`, 10393) do not
change presence and need no restart.

### M22 — `menu_stepAutomationExecuteItem0()` (menu.c:10759–10769, MODIFY)

**Current:**

```c
    } else if (menu_stepAutoDeleteMode) {
        (void)patSvc_removeTrackAutomationByTarget(scene, track, autos[page].target);
        /* S066: a target deletion can change the Pattern-wide name marker. */
        if (menu_isVoicePage(menu_activePage))
            va_searchRestart();
    } else {
        (void)patSvc_removeStepAutomation(scene, track, step, autos[page].target);
        /* S066: a target deletion can change the Pattern-wide name marker. */
        if (menu_isVoicePage(menu_activePage))
            va_searchRestart();
    }
```

**New:**

```c
    } else if (menu_stepAutoDeleteMode) {
        (void)patSvc_removeTrackAutomationByTarget(scene, track, autos[page].target);
        /*
         * S066 / S078 P3: a target deletion can change the Pattern-wide name
         * marker. What: restart the presence search unconditionally. Why:
         * this editor only runs on SEQ_PAGE (menu_stepAutomationPageActive()),
         * so the former menu_isVoicePage() guard was never true and a deleted
         * `len`/`scl`/`shf` target kept its STEP track-settings underline.
         * Output: SEQ active-track search reset. Affiliates:
         * va_searchRestart(), va_scanService(), sa_applyTrackMarkers().
         */
        va_searchRestart();
    } else {
        (void)patSvc_removeStepAutomation(scene, track, step, autos[page].target);
        /* S066 / S078 P3: as above - unconditional SEQ_PAGE search restart. */
        va_searchRestart();
    }
```

### M23 — `menu_knobClearTarget()` (menu.c:12662–12670 comment, 12708 ADD)

**Header, current** (12662–12670):

```c
/*
 * Resolve one visible endless-pot column to the canonical clear target.
 *
 * Inputs: physical pot number and output record. Output: Pattern target and,
 * for Effect cells, the optional FX-sequence lane; zero means this column has
 * no automatable owner. Clear mode calls this before consuming a delta so the
 * normal value-edit path is never entered. Affiliates: Menu cell resolution,
 * SceneModTargets, EffectsManager, and copyClearSession.
 */
```

**Header, new:**

```c
/*
 * Resolve one visible endless-pot column to the canonical clear target.
 *
 * Inputs: physical pot number and output record. Output: Pattern target and,
 * for Effect cells, the optional FX-sequence lane; zero means this column has
 * no automatable owner. Clear mode calls this before consuming a delta so the
 * normal value-edit path is never entered. Pages: VOICE, PERF, Effect, and
 * (S078 P3) the STEP track settings, SEQ_PAGE subpage 0 (`len`/`scl`/`shf` of
 * the active track); COPYCLEAR_UTILITIES.md §8. Accessor:
 * menu_parseKnobDelta() copy/clear branch (only reached on SEQ_PAGE when the
 * held-step overlay is not active; with held steps the overlay's per-step
 * clear runs first). Affiliates: Menu cell resolution, SceneModTargets,
 * EffectsManager, menu_seqCellToTrackTarget(), and copyClearSession.
 */
```

**ADD after line 12708** (`    if (cell.kind == MENU_CELL_STATIC) {`), before
`if (cell.static_param >= PAR_VOICE1_MORPH &&` at 12709:

```c
        /*
         * STEP track settings pot clear (S078 P3, Bug 2).
         *
         * What: on SEQ_PAGE subpage 0, maps the pot's cell (first half: 0
         * `len`, 1 `scl`, 2 `shf`; second half: 4..7, never automatable) to
         * the active track's length/scale/shuffle Scene target.
         * Why: these cells are MENU_CELL_STATIC (PAR_TRACK_LENGTH /
         * PAR_TRACK_SCALE / PAR_SHUFFLE), and this arm previously accepted
         * only the PERF Morph cells, so SHIFT+COPY + pot never queued a clear
         * for track automation. The `active_page == 0u` guard is required:
         * SEQ subpage 1 positions 0..2 (velocity, note, probability) are also
         * STATIC and must stay non-clearable. The SHIFT Morph endpoint view
         * changes only value resolution, not the cell, so the clear works in
         * both views, as on VOICE.
         * Inputs: knobNr, second_page (SEQ_PAGE is not a screen page, so it
         * is the half offset), menu_activeVoice. Output: out->pattern_target
         * = 405+t / 412+t / 419+t, fx_lane stays 0xff (no FX lane); return 1
         * for an automatable cell, 0 otherwise. Downstream:
         * copyClear_potTurned() -> ccClear_potTurned() -> ccSvc_registerAdd()
         * removes the target from every step of the active Scene (only this
         * track uses the ID), menu_automationTargetCleared() drops the name
         * underline at once, and ccSvc_patternChangedUi() ->
         * menu_patternContentChanged() restarts the search on completion.
         * Affiliates: menu_seqCellToTrackTarget(), va_seqTrackSearchBit(),
         * sa_clearAutomationFromKnob() (held-step variant).
         */
        if (menu_activePage == SEQ_PAGE && active_page == 0u) {
            out->pattern_target = menu_seqCellToTrackTarget(
                (uint8_t)(knobNr + second_page), menu_activeVoice);
            return (uint8_t)(out->pattern_target != INSTRUMENT_PARAM_INVALID);
        }
```

Types: `cc_pot_target_t.pattern_target` is `uint16_t`;
`instrument_param_id_t` is `uint16_t`; `INSTRUMENT_PARAM_INVALID` is
`0xffffu`. The forward declaration at 2011 makes
`menu_seqCellToTrackTarget()` visible here.

### M24 — `menu_serviceRuntimeWidgets()` (menu.c:12955–12986, MODIFY)

**Comment, current** (12955–12961): replace the last sentence

```c
     * only. Pattern-wide scans are four step reads per pass by configuration
     * (VOICE and Effect pages).
```

with

```c
     * only. Pattern-wide scans are four step reads per pass by configuration
     * (VOICE, SEQ, Effect, and PERF pages).
```

**Current block** (12970–12986):

```c
    /*
     * S078 P2 §B: STEP held-step track automation overlay service.
     *
     * What: on SEQ_PAGE with the shared overlay armed, poll the physical
     * held-step mask and run the value-underline debounce, exactly as the
     * VOICE pages do above. Why: va_updateHeldState() owns the one held-order
     * list and its overlay-exit path (mask -> 0), and va_underlineService()
     * owns the edit-flash restore; both must run for the STEP context too.
     * Inputs: menu_activePage == SEQ_PAGE, va_overlayActive. Outputs: held
     * order, presence/LED refresh on a mask change, overlay exit, and the
     * debounced marker repaint. Affiliates: va_updateHeldState(),
     * sa_refreshAutomationLeds(), va_underlineService().
     */
    if (menu_activePage == SEQ_PAGE && va_overlayActive) {
        va_updateHeldState();
        va_underlineService();
    }
```

**New block:**

```c
    /*
     * S078 P2 §B / P3: STEP track-settings services.
     *
     * What: on SEQ_PAGE, always advance the active-track automation-presence
     * search (S078 P3); with the held-step overlay armed, also poll the
     * physical held-step mask first and run the value-underline debounce,
     * in the same order as the VOICE pages above.
     * Why: the `len`/`scl`/`shf` name underlines come from the Pattern-wide
     * search, which must run whether or not steps are held (P3 Bug 1b: it
     * never ran on SEQ_PAGE). It also runs on SEQ subpage 1 and the
     * step-automation editor, so a search restarted there by an editor change
     * has finished when the user returns to the track settings.
     * va_updateHeldState() owns the one held-order list and its overlay-exit
     * path (mask -> 0), and va_underlineService() owns the edit-flash
     * restore; the second overlay test sees an exit made by the first call.
     * Inputs: menu_activePage == SEQ_PAGE, va_overlayActive. Outputs: held
     * order and LED refresh on a mask change, overlay exit, search progress
     * (one repaint on completion), and the debounced marker repaint. Cost:
     * at most VOICE_AUTOMATION_SCAN_STEPS_PER_PASS step reads per pass, as
     * on a VOICE page. Affiliates: va_updateHeldState(), va_scanService(),
     * va_underlineService(), sa_refreshAutomationLeds(),
     * sa_applyTrackMarkers().
     */
    if (menu_activePage == SEQ_PAGE) {
        if (va_overlayActive)
            va_updateHeldState();
        va_scanService();
        if (va_overlayActive)
            va_underlineService();
    }
```

### M25 — `menu_switchPage()` SEQ_PAGE entry (menu.c:14321–14332, ADD)

**ADD after line 14331**
(`            pat_applyTrackSettingsToMenu(menu_getViewedPattern(), menu_getActiveVoice());`),
inside `if (pageNr == SEQ_PAGE) {` and before its closing `}` at 14332:

```c
            /*
             * STEP track-settings automation-presence search (S078 P3).
             *
             * What: entering SEQ_PAGE from any other page restarts the search
             * in SEQ geometry (active track; menu_activePage is already
             * SEQ_PAGE at 14301, which va_searchRestart() requires).
             * Why: the shared va_search* bytes may hold a completed VOICE,
             * Effect, or PERF result. A VOICE result for the same Pattern and
             * track would even pass va_scanService()'s mismatch test, which a
             * completed search never reaches anyway, so the `len`/`scl`/`shf`
             * bits would never be filled. The old_page guard keeps re-entries
             * from SEQ_PAGE (SHIFT release -> buttonHandler_enterSeqModeStepMode(),
             * STEP TRACK presses) from blanking valid underlines for a
             * 32-pass rescan. Inputs: old_page (captured at 14147),
             * menu_shownPattern, menu_activeVoice. Output: va_search* reset.
             * Affiliates: va_searchRestart(), va_scanService(),
             * sa_applyTrackMarkers(), menu_enterStepTrackAutomationOverlay().
             */
            if (old_page != SEQ_PAGE)
                va_searchRestart();
```

### M26 — `menu_setShownPattern()` (menu.c:15300–15306 comment, after 15330 ADD)

**Comment, current** (15302–15305):

```c
     * valid, otherwise it falls back to Scene 0. A VOICE context change also
     * invalidates the held-step/search view before repainting it; on the
     * Effect page (S074) the automation-presence search restarts and the
     * page repaints.
```

**Comment, new:**

```c
     * valid, otherwise it falls back to Scene 0. A VOICE context change also
     * invalidates the held-step/search view before repainting it; on the
     * Effect page (S074) and the STEP track-settings page (SEQ_PAGE, S078 P3)
     * the automation-presence search restarts and the page repaints.
```

**MODIFY line 15330** (`        }`, closing the Effect arm) to chain a new
arm:

```c
        } else if (menu_activePage == SEQ_PAGE) {
            /*
             * STEP track settings (S078 P3).
             *
             * What: restart the active-track search for the new Pattern and
             * repaint. Why: va_scanService() only checks the Pattern while a
             * search is still running, so a completed result for the old
             * Scene would keep underlining (or missing) `len`/`scl`/`shf`
             * names after a Scene change (PERF selection, follow, chain).
             * The held-step overlay is deliberately kept: its held values and
             * LEDs already read menu_shownPattern live, and its exit stays
             * tied to the physical step release. No Pattern LED update here:
             * the STEP LED owners refresh the row on Pattern change. Inputs:
             * menu_shownPattern (already updated). Output: va_search* reset
             * and one repaint. Affiliates: va_searchRestart(),
             * va_scanService(), sa_applyTrackMarkers().
             */
            va_searchRestart();
            menu_repaint();
        }
```

---

## 4. `Core/Menu/menu.h` changes

### H1 — `menu_patternContentChanged()` (menu.h:519–533, MODIFY comment)

**New comment** (replaces the block above `void menu_patternContentChanged(void);`):

```c
/*
 * Restart the automation-presence search after Pattern content changed (S075;
 * formerly menu_voiceAutoOverlayPatternDeleted()).
 *
 * What: restarts the bounded search shared by the VOICE pages (active track),
 * the STEP track-settings page (SEQ_PAGE, active track, `len`/`scl`/`shf`,
 * S078 P3), the Effect page (all seven tracks, S074), and the PERF page (all
 * seven tracks, S077 P6), then repaints, so underlines follow the new content
 * once the rescan completes. Why: a paste or clear can add or remove
 * automation anywhere in the viewed Pattern, and a removed target cannot be
 * proven absent without a full rescan. Inputs: none; call after Pattern
 * content has been changed. Outputs: a cleared search and a repaint on those
 * pages; nothing on other pages, whose next entry restarts the search anyway.
 * Callers: copyClearService.c (ccSvc_patternChangedUi()) after every Pattern
 * paste/clear and pot-clear register pass on the viewed Scene. Affiliates:
 * va_searchRestart() and va_scanService() in menu.c.
 */
```

### H2 — `menu_automationTargetCleared()` (menu.h:535–545, MODIFY comment)

**New comment:**

```c
/*
 * Drop one target's underline now, without restarting the search (S075).
 *
 * What: clears the presence bit for one target in the current search result
 * and repaints; VOICE, Effect, PERF, and (S078 P3) the STEP track-settings
 * `len`/`scl`/`shf` bits of the active track. Why: a pot clear removes the
 * underline at the turn (spec §8) while the background removal runs;
 * restarting the whole search would make every other underline vanish until
 * the rescan completes. The search loop filters targets waiting in the
 * register (ccSvc_targetPending()), so the bit cannot come back before the
 * removal finishes. Only for whole-Scene removals: a held-step-only removal
 * restarts the search instead. Inputs: Pattern target ID. Output: repaint.
 * Caller: ccClear_potTurned().
 */
```

### H3 — `menu_enterStepTrackAutomationOverlay()` (menu.h:571–587, MODIFY comment)

**Current** (579–586):

```c
 * Inputs: voiceNr is the pressed TRACK button (0..6). Outputs: the SEQ_PAGE
 * front page is shown for that track, the shared VOICE-overlay state is armed
 * for the STEP context, automation presence and step LEDs are refreshed, and
 * the frame is repainted. Lifetime: until all steps are released, the page
 * changes, or the mode changes. Caller: handleVoiceButton() in
 * buttonHandler.c when buttonHandler_seqHeldMask() is nonzero. Affiliates:
 * menu_seqCellToTrackTarget(), sa_refreshPresence(),
 * sa_refreshAutomationLeds().
```

**New:**

```c
 * Inputs: voiceNr is the pressed TRACK button (0..6). Outputs: the SEQ_PAGE
 * front page is shown for that track, the shared VOICE-overlay state is armed
 * for the STEP context, step LEDs are refreshed, and the frame is repainted;
 * held-step values are underlined on the value row, and automated
 * `len`/`scl`/`shf` names are underlined from the SEQ_PAGE Pattern search
 * (S078 P3), which also runs without the overlay. Lifetime: until all steps
 * are released, the page changes, or the mode changes. Caller:
 * handleVoiceButton() in buttonHandler.c when buttonHandler_seqHeldMask() is
 * nonzero. Affiliates: menu_seqCellToTrackTarget(),
 * sa_refreshAutomationLeds(), sa_applyTrackMarkers().
```

---

## 5. Specification changes

### D1 — `COPYCLEAR_UTILITIES.md` §8 (lines 439–451, ADD)

**ADD after line 441** (end of the "The Pattern part covers ..." bullet):

```markdown
- STEP track settings (S078 P3): with no SEQ steps held, a pot clear over
  `len`, `scl`, or `shf` (first half of the track-settings page) follows the
  rules above for the active track's target. With SEQ steps held after a
  TRACK press (the held-step overlay, S078 P2 §B), the same turn instead
  removes the target from the held steps only, at once, without the register
  (`sa_clearAutomationFromKnob()`); the name underline is then re-derived by
  the search.
```

**ADD after line 447** (`| VOICE | generated slot-6/track-7 decay | ... |`):

```markdown
| STEP (track settings, Normal or Morph view) | `len`, `scl`, `shf` | track target 405+t / 412+t / 419+t (t = active track) | — |
```

### D2 — `COPYCLEAR_UTILITIES.md` test summary (line 1054, MODIFY)

**Current:**

```markdown
| Pot clears | VOICE, PERF (`1vm..6vm`, `fxm`), Effect page; no menu; underline off at the turn; values never change; 8-entry register; FX-lane part fans out; starts at once during a running drain |
```

**New:**

```markdown
| Pot clears | VOICE, STEP track settings (`len`/`scl`/`shf`; held steps: held-only), PERF (`1vm..6vm`, `fxm`), Effect page; no menu; underline off at the turn; values never change; 8-entry register; FX-lane part fans out; starts at once during a running drain |
```

---

## 6. Build and static verification

1. `make all && make img`, DEV configuration. Zero new warnings, in
   particular no unused-function warning (`sa_refreshPresence` is gone) and
   no `-Wswitch` warning in `va_seqTrackSearchBit()` (it has a `default`).
2. `grep -n "sa_trackPresenceMask\|sa_refreshPresence" Core/` must return
   nothing.
3. `grep -n "VA_SEARCH_SCENE_TRACK_BIT" Core/Menu/menu.c` should show the
   macro (M2), the helper (M6), the write (M17), and the two marker branches
   (M19).
4. Size report against the S077 close (`text=538,416`, `data=416`,
   `bss=427,008`): expect `bss` the same or 1 B lower (alignment may absorb
   the byte), `data` unchanged, and `text` changed by a few hundred bytes at
   most (one helper and four small arms, minus `sa_refreshPresence()`).
   Record the actual numbers in the session log.

---

## 7. Hardware test plan

| # | Steps | Expected |
|---|-------|----------|
| 1 | STEP mode, track settings, Scene with no track automation | No underlines |
| 2 | Hold steps → TRACK → turn `scl` → release steps | `scl` name underlined on the top row: at once if the search had finished (the bit is set at the write), otherwise when it finishes |
| 3 | Repeat 2 but keep the steps held | `scl` **value** underlined (bottom row) after the 100 ms quiet period |
| 4 | Clicked-in (encoder) on `scl` with no steps held, track has `scl` automation | Long name underlined (row 0, first character from column 8) |
| 5 | No steps held, SHIFT+COPY, turn the `scl` pot | `scl` underline disappears at once; after the register pass, step automation for `scl` no longer runs on that track; other tracks unchanged. The last applied scale stays until transport stop or Pattern restore (sticky step-override rule, `presetMorph_clearAllTrackParamStepOverrides()`) |
| 6 | Same as 5 for `shf` and `len`; also with SHIFT still held (Morph view) | Same as 5 |
| 7 | Hold steps → TRACK → write `shf` → SHIFT+COPY (steps still held) → turn the `shf` pot | Removed from the held steps only; after the rescan, the name stays underlined if non-held steps still carry `shf` |
| 8 | Step-automation editor (`scn`): add `len` on one step, then TRACK back to track settings; then delete it and return | `len` name underlined, then not underlined |
| 9 | Change track, then change the shown Scene, on the track settings page | Underlines follow the shown track and Scene; no stale marks |
| 10 | SHIFT press/release with underlines shown | Underlines stay (no rescan blank) |
| 11 | Subpage 1 (vel/note/prob), SHIFT+COPY + turn a pot | Nothing cleared |
| 12 | Second half (play mode/MIDI ch/note), SHIFT+COPY + turn a pot | Nothing cleared |
| 13 | Copy/paste or clear a track that carries `scl` automation while on track settings | Underline updates after the job completes |
| 14 | Regression: VOICE, Effect, PERF pot clears and underlines; VOICE held-step overlay | Unchanged |

---

## 8. Follow-up documentation (at session close, not part of the code change)

- `077`→`078` handoff log: record the root causes, the deviations in §1, the
  1 B RAM release, and the build sizes.
- `MEMORY.md` Volatile Notes: STEP track settings now take part in the shared
  search (`VA_SEARCH_SCENE_TRACK_BIT`).
- `S078_RETEST_CHECKLIST.md`: add the §7 rows.

## 9. Session progress notes

### Step 0 — baseline and anchors verified

- Read `MEMORY.md`, `S078_P3_TRACK_PAR_AUTOM_FOLLOWUP.md`, and this schedule.
- Confirmed the working tree contains the uncommitted S078 P2 STEP overlay and
  the planned S078 P3 anchors in `Core/Menu/menu.c`, `Core/Menu/menu.h`, and
  `knowledge_files/specification_reference/COPYCLEAR_UTILITIES.md`.
- Confirmed the existing defects are present: `SEQ_PAGE` has no Pattern-wide
  track-target search, `sa_trackPresenceMask` is the unreachable name-marker
  source, and `menu_knobClearTarget()` has no STEP track-settings arm.
- Preserved all pre-existing user changes and generated/card artifacts shown by
  `git status`; S078 P3 work is limited to the files listed in §2.

### Step 1 — context and mutation wiring applied

- Added the SEQ editor restarts at target replacement and both delete paths,
  admitted `SEQ_PAGE` to `menu_patternContentChanged()`, and added the
  `menu_setShownPattern()`/SEQ-page entry restarts.
- Added the STEP track-settings pot-clear mapping for `len`/`scl`/`shf` and
  the immediate SEQ search-bit clear for a whole-track pot clear.
- Changed runtime service so the active-track SEQ search runs on every SEQ
  subpage, including the step-automation editor.
- Replaced the STEP marker pass so it works with or without the held-step
  overlay: held values still win, while completed Pattern-wide search results
  underline names in overview and clicked-in views.
- Removed the obsolete STEP held-presence state and its refresh/update paths;
  the shared `va_searchSceneMask` now owns the stored-data result.
- Updated the adjacent `menu.h` contracts and `COPYCLEAR_UTILITIES.md` pot-
  clear table/test row to describe whole-track versus held-step behavior.

### Step 2 — build and static validation

- `make all` passed. The only compiler warnings were the existing unrelated
  unused-function warnings in `filesystem.c`, the packed-member warning in
  `PatternData.c`, and standard embedded-library/linker warnings; `menu.c`
  introduced no warning.
- `make img` passed and wrote `build/LXRV2_lxr02.img` at 540,368 bytes.
- DEV link sizes: `text=539,952`, `data=416`, `bss=427,616`; link-budget output
  reports 213,296 bytes of application flash headroom.
- Static checks passed: no `sa_trackPresenceMask` or `sa_refreshPresence`
  references remain under `Core/`, all `VA_SEARCH_SCENE_TRACK_BIT`/
  `va_seqTrackSearchBit()` sites are present, and `git diff --check` is clean.

### Step 3 — final code-path audit

- Confirmed `menu_repaintGeneric()` formats the ordinary SEQ frame before
  `sa_applyTrackMarkers()`, so both stored-data name markers and held-value
  markers operate on the correct display text.
- Confirmed the scan uses active-track geometry on SEQ_PAGE, classifies only
  matching length/scale/shuffle Scene targets, and never passes SEQ entries
  through VOICE-slot classification.
- Confirmed whole-track pot clears use the generic register and clear the
  matching SEQ bit immediately; completion calls the admitted
  `menu_patternContentChanged()` restart. Held-step clears restart instead,
  because non-held steps may still carry the target.
- Confirmed no changes were made to ButtonHandler, CopyClear service/session,
  clear operations, PatternData, PatternStackService, SceneModTargets,
  presetMorphEngine, or the sequencer.

---

## 10. Implementation assessment (independent review, 2026-10-09)

**Verdict: the implementation matches the schedule and is ready for
hardware testing.** All 31 scheduled changes are present and correct. No
functional defects were found. There are two minor comment inaccuracies
(§10.4), and neither affects behaviour.

### 10.1 Method

- Read every changed site in the working tree against M1–M26, H1–H3 and
  D1–D2. Line numbers below are post-implementation.
- Rebuilt from a touched `menu.c` (`make all`), and checked the ELF symbol
  table and `git diff --check`.
- Traced the behaviour paths the fix depends on: page entry and exit, track
  and Scene changes, copy/clear completion, the SHIFT Morph view, CGRAM
  ownership, and subpage changes.

### 10.2 Conformance

| Change | Site (post-implementation) | Result |
|--------|----------------------------|--------|
| M1 | menu.c:1384–1392 | Match. The final sentence is shortened ("the former 1 B STEP presence byte" without the symbol name); same meaning |
| M2 | menu.c:1423–1440 comment, 1446–1466 macro | Match |
| M3 | — | Match: block and variable removed; no `sa_*` data symbol in the ELF |
| M4 | menu.c:2020–2028 | Match: `sa_refreshPresence` declaration removed |
| M5 | menu.c:2061–2084 | Match, except the caller list (§10.4 item 1) |
| M6 | menu.c:2138–2182 | Match: `va_seqTrackSearchBit()` exactly as scheduled |
| M7 | menu.c header above 2256 | Match |
| M8 | menu.c:2261–2262 | Match (shorter one-line comment) |
| M9 | menu.c:2293–2317 | Match: after the Effect branch, before PERF; `ccSvc_targetPending()` filter still runs first |
| M10 | menu.c:2478–2486 | Match |
| M11 | menu.c:2488–2503 | Match |
| M12 | menu.c:2516–2523 | Match |
| M13 | menu.c:2567–2606 | Match: guard admits `SEQ_PAGE` (2599–2602) |
| M14 | menu.c:2608–2621 header, 2690–2707 arm | Match |
| M15 | menu.c:3642–3648 | Match |
| M16 | — | Match: `sa_refreshPresence()` removed; no references under `Core/` |
| M17 | menu.c:3744–3763 | Match |
| M18 | menu.c:3766–3818 | Match: `va_searchRestart()` replaces the presence refresh and `menu_automationTargetCleared()` |
| M19 | menu.c:3886–4032 | Match: the function body is identical to the schedule |
| M20 | menu.c:4034–4080 | Match, except one stale header sentence (§10.4 item 2) |
| M21 | menu.c:10516–10569 | Match: restarts on both success paths only |
| M22 | menu.c:10951–10967 | Match: both restarts unconditional; the dead guards are gone |
| M23 | menu.c:12863–12876 header, 12914–12945 arm | Match: SEQ arm is first in the STATIC branch, guarded by `active_page == 0u` |
| M24 | menu.c:13192–13236 | Match |
| M25 | menu.c:14571–14602 | Match: `old_page != SEQ_PAGE` guard |
| M26 | menu.c:15570–15619 | Match: separate SEQ arm |
| H1–H3 | menu.h:519–595 | Match (H2 wording slightly expanded: "... are covered") |
| D1–D2 | COPYCLEAR_UTILITIES.md:442–448, 455, 1062 | Match |

### 10.3 Build and RAM verification (reproduced)

- `make all` succeeded. `menu.c` raised **no warnings**. The remaining
  warnings are the existing unrelated ones (LTO serial-compilation note,
  newlib linker notes).
- `arm-none-eabi-size`: `text=539,952`, `data=416`, `bss=427,616`. This is
  the same as the §9 Step 2 report. Flash headroom is 213,296 B.
- ELF symbol check: the va_* overlay statics total 46 B (the
  `_Static_assert` holds), `va_searchSceneMask` is 1 B, and **no `sa_*` data
  symbol exists**. So P3 adds no static RAM and removes the 1 B
  `sa_trackPresenceMask`.
- The `bss` rise against the S077 close (427,008 → 427,616) comes from S078
  P1/P2, not P3. P3 adds no storage, as the symbol check confirms. The S077
  figure is not a valid baseline for P3, so the exact −1 B cannot be read
  from the totals (alignment may also absorb it).
- `git diff --check` is clean.

### 10.4 Findings

No functional defects. Two minor comment inaccuracies, both optional fixes
with no behavioural effect:

1. **`va_searchRestart()` caller list (menu.c:2075–2076)** reads "Callers:
   VOICE and Effect entry in menu_switchPage()". It should read "VOICE,
   Effect, PERF, and SEQ entry", as in M5. The SEQ entry restart (M25) and
   the existing PERF restart are both real callers.
2. **`menu_enterStepTrackAutomationOverlay()` header (menu.c:4038–4039)**
   still says "...arms the shared held/marker state for the STEP context,
   then scans track automation presence and repaints." The function no
   longer scans presence. Suggested wording: "...for the STEP context,
   refreshes the step LEDs, and repaints." This sentence was outside the
   lines M20 replaced, so it is a gap in the schedule, not an
   implementation error.

### 10.5 Behaviour audit (code reading)

- **Bug 1a fixed.** The name branch in both views now reads the
  Pattern-wide search result. It no longer depends on the held-value
  predicate, so it can run.
- **Bug 1b fixed.** `sa_applyTrackMarkers()` no longer requires the overlay.
  The SEQ search runs on every SEQ subpage (M24).
- **Bug 1c fixed.** The editor's deletes and target changes restart the
  search. The removed `menu_isVoicePage()` guards survive only in an
  explanatory comment (menu.c:10957).
- **Bug 2 fixed.** `menu_knobClearTarget()` returns the track target for
  `len`/`scl`/`shf` on subpage 0 only. The second half (cells 4–7) and
  subpage 1 (velocity/note/probability) still return 0. The held-step
  overlay intercept still runs first when steps are held.
- **Search freshness.** These events restart the search:
  - Entering SEQ_PAGE from another page (M25).
  - A track change (the existing `menu_setActiveVoice()` restart).
  - A Scene change (M26).
  - A copy/clear or pot-clear register completion (M13, via
    `ccSvc_patternChangedUi()`).
  - Editor changes (M21/M22) and the held-step clear (M18).

  These events do not restart it:
  - SHIFT press/release and STEP TRACK presses, because of the `old_page`
    guard.
  - Overlay entry when the track and page are unchanged, where the existing
    result is still valid.
- **No cross-page leakage.** Leaving SEQ_PAGE resets the overlay and the
  CGRAM cache (existing `menu_switchPage()` reset). Every VOICE, Effect and
  PERF entry restarts the search. The STEP bits (0x10–0x40) are disjoint
  from the VOICE (0x01–0x04) and Effect (0x08) bits.
- **CGRAM.** The marker pass now runs on every SEQ subpage-0 frame, also with
  nothing to mark, so stale marks are retired. Only the shared marker cache
  and the splash animation define CGRAM slots. The copy/clear menu guard in
  `va_queueMarkerTransaction()` still holds markers back while a clear menu
  is visible. Subpage 1 and the step-automation editor return before the
  marker pass, and their full-frame writes replace any marker codes.
- **SHIFT Morph view.** `menu_patternTrackMorphEndpoint` affects only value
  resolution (menu.c:909, :4827). Cell identity and the row-0 text are
  unchanged, so the pot clear and the clicked-in name marker (row 0, column
  8 onward) work in both views.
- **Cost.** The SEQ search uses the VOICE budget (4 step reads per pass).
  `va_seqTrackSearchBit()` does one descriptor lookup per stored entry, the
  same cost as the existing PERF/VOICE Scene-target classification.

### 10.6 Residual risks (accepted, unchanged from the plan)

- **Queued mutations.** A held-step clear or an editor delete still waiting
  in the patSvc FIFO can be read by the immediate rescan before it runs. The
  name can then stay underlined until the next restart.
- **Underline gap after restarts.** In follow or chain playback, every Scene
  change on SEQ_PAGE restarts the search, so name underlines are briefly
  absent (32 passes) after each change. The VOICE pages behave the same way.
- **Value not undone by a clear.** Clearing automation does not undo the
  last applied track value. It persists until transport stop or a Pattern
  restore (`presetMorph_clearAllTrackParamStepOverrides()`).

### 10.7 Remaining work

- Hardware test plan §7 (14 rows) is not yet run.
- ~~Optional: fix the two comments in §10.4.~~ **Done (2026-10-09):** both
  comments corrected in menu.c (`va_searchRestart()` caller list;
  `menu_enterStepTrackAutomationOverlay()` header). This is a comment-only
  change: the rebuild gives identical sizes (`text=539,952`, `data=416`,
  `bss=427,616`), `build/LXRV2_lxr02.img` was rewritten, and `git diff --check`
  is clean.
- Session-close documentation per §8.
