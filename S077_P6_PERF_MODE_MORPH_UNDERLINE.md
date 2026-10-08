# S077 P6 — PERF Mode Morph Automation Underline

**Parent:** S077 retest, user request 2026-10-08.
**Status (2026-10-08):** implemented — build clean, boot image written.

---

## Implementation Log

### 2026-10-08 — complete

All 11 changes are applied in `Core/Menu/menu.c`. Every change carries its
comment block adjacent (the enum/function comments and the new function's
contract block). Build is clean and the boot image is written.

**Build (DEV config, `make all` then `make img`):**

```
   text     data     bss      dec      hex   filename
 538416      416  427008   965840    ebcd0   build/lxr02.elf
Flash : 538,832 / 753,664 B used, headroom 214,832 B
ITCM  : 4,168 / 16,384 B;  DTCM statics 4,472 B
FXBUF : 126,592 B at 0x20001180 (min 122,880, margin 3,712)
Written: build/LXRV2_lxr02.img (538832b) OK
```

No warnings from `menu.c` (the only warnings in the build are the pre-existing
`-Wunused-function` ones in `Core/Hardware/SD/filesystem.c`).

**RAM:** the plan quotes one new `static uint8_t`; the map confirms
`va_searchPerfMorphMask` at `0x20068119` (1 byte in `.bss`), immediately after
`va_searchSceneMask`. The `size` summary shows `bss` unchanged at 427,008 B
because the byte fits existing alignment padding.

**Steps taken**

1. **Change 1** — `va_searchPerfMorphMask` + `VA_PERF_MORPH_EFFECT_BIT`
   added after `va_searchSceneMask`.
2. **Change 2** — `va_searchRestart()` clears the new mask and starts the
   track cursor at 0 for PERF as well as Effect.
3. **Change 3** — `va_scanService()` gains `perf_page`, the PERF
   classification block (voice morph bits + effect morph bit), the relaxed
   context-mismatch guard, and the multi-track advance for PERF.
4. **Change 4** — new `menu_applyPerfMarkers()` (compact-view name markers;
   column 7 also consults `menuEffects_cellSeqLocked()`), plus a forward
   declaration beside `menu_applyEffectMarkers()`.
5. **Change 5** — `menu_repaintGeneric()` tail calls
   `menu_applyPerfMarkers()`.
6. **Change 6** — `menu_serviceRuntimeWidgets()` scans on PERF.
7. **Change 7** — the CGRAM retry guard accepts PERFORMANCE_PAGE; comment
   updated.
8. **Change 8** — `menu_patternContentChanged()` also restarts on PERF.
9. **Change 9** — `menu_switchPage()` restarts the search on fresh PERF
   entry.
10. **Change 10** — `va_scanService()` comment block gains the PERF bullet and
    the updated Caller line.
11. **Change 11** — `menu_automationTargetCleared()` drops the PERF bit
    immediately after a pot clear.

**Required deviations (the schedule did not list them)**

- **`_Static_assert` 45 → 46.** The new mask sits inside the 45-byte shared
  overlay block, so the assert and its comment were updated to 46 bytes
  ("S077 VOICE/PERF overlay state must remain exactly 46 bytes"). Without this
  the build fails. This is the one quoted 1-byte allocation.
- **Overlay block comment** updated to list the new byte and describe
  PERF-page sharing (alongside the existing Effect-page paragraph).
- **`va_searchRestart()` comment** and **`menu_patternContentChanged()` /
  `menu_automationTargetCleared()` comments** updated for PERF accuracy.
- **Forward declaration** `static void menu_applyPerfMarkers(void);` added next
  to `menu_applyEffectMarkers()` for consistency (the definition precedes the
  only call site, so it is not strictly required).

**Correctness review**

- PERF cannot enter `editModeActive` (every `editModeActive = 1` site is a
  Load/Save path), so `menu_applyPerfMarkers()` at the `menu_repaintGeneric()`
  tail always runs in compact view; the four visible cells are columns
  `is2nd..is2nd+3` with names at `editDisplayBuffer[0][4*i]`.
- `menu_applyVoiceMarkers()` and `menu_applyEffectMarkers()` still return
  early on PERF, so only `menu_applyPerfMarkers()` drives the shared CGRAM
  transaction there.
- `va_searchRestart()` is reached on every PERF entry (`menu_switchPage()`) and
  on every exit to a VOICE page (the voice default case already restarts), so a
  stale VOICE/Effect result can never underline PERF names.
- The synthetic `menuEffects_cell_t { .kind = MENU_FX_CELL_MORPH_AMOUNT }`
  resolves to `EFFECT_SEQ_LANE_MORPH` in `menuEffects_cellLane()`, matching the
  pot-clear path's `EFFECT_SEQ_LANE_MORPH` mapping.

**Size analysis (negative text delta explained)**

The additive change reports `text` −160 B and `data` −4 B versus the S077 P5
build (538,576/420). This is a compiler inlining shift, not lost code: at
source level (non-LTO `-O2` objects) the change is `text` −124 B, `bss` +1 B,
`data` unchanged. The extra branch made GCC stop inlining `va_searchRestart()`
into its seven callers (each of which shrank by ~52–132 B) and emit it once
(108 B), which more than offsets the new function. No function lost behaviour.

---

## 1. Problem Statement

When voice or effect morph automation exists in a pattern (Pattern automation
or FX sequence locks), the automation underline does not appear on the PERF
page.  It should: the pot-clear path already works on PERF (it uses an
independent target resolver that maps MENU_CELL_STATIC cells directly), so
the user can clear morph automation from PERF but has no visual indication
that it exists.

**Scope:**
- Columns 1..6 (PAR_VOICE1_MORPH..PAR_VOICE6_MORPH): underline the name when
  Pattern automation exists for that voice's morph target on any track, any
  step of the viewed pattern.
- Column 7 (PAR_EFFECT_MORPH): underline the name when Pattern automation
  exists for the effect morph target on any track, any step, OR when any FX
  sequence step has a morph lane lock.
- Column 0 (PAR_MORPH, global Scene morph): never underline — not automatable.

---

## 2. Existing Architecture

### 2.1  Automation Underline Pipeline (VOICE / Effect)

The underline pipeline has three stages:

1. **Scan** (`va_scanService()`, called from `menu_serviceRuntimeWidgets()`):
   reads 4 steps per pass of the viewed pattern.  On VOICE pages, scans the
   active track only and records voice descriptor locals in
   `va_searchTargetMask[8]` and per-voice Scene targets (voice morph, audio
   out, FX send) in `va_searchSceneMask`.  On EFFECT page, scans all 7 tracks
   sequentially and records effect descriptor locals and effect morph in the
   same variables.  Sets `va_searchComplete = 1` when done.

2. **Classify** (`menu_effectCellAutomated()` for Effect cells): combines the
   scan result with FX sequence lock presence (`menuEffects_cellSeqLocked()`).

3. **Render** (`va_applyVoiceMarkers()` / `menu_applyEffectMarkers()`):
   after `menu_repaintGeneric()` formats the LCD frame, walks the visible
   cells and underlines the first non-space name character of any cell whose
   automation bit is set.  Uses CGRAM slots 2..5 (4 markers, one per visible
   compact-view cell) via `va_queueMarkerTransaction()`.

### 2.2  Why PERF Is Excluded

- `va_scanService()` is called only when `menu_isVoicePage()` or
  `menu_activePage == EFFECT_PAGE`.
- `va_applyVoiceMarkers()` guards `if (!menu_isVoicePage(...)) return;`.
- `menu_applyEffectMarkers()` guards `if (menu_activePage != EFFECT_PAGE)
  return;`.
- `menu_patternContentChanged()` guards `if (!menu_isScreenPage(...)) return;`,
  so a paste/clear on PERF does not restart the search.
- PERF cells resolve as `MENU_CELL_STATIC` — none of the existing marker
  functions know how to underline them.

### 2.3  Why Pot-Clear Works on PERF

`menu_knobClearTarget()` (line 11755) resolves MENU_CELL_STATIC cells
independently: it maps PAR_VOICE1_MORPH..PAR_VOICE6_MORPH to
`sceneModTarget_voiceMorphId(slot)` and PAR_EFFECT_MORPH to
`sceneModTarget_effectMorphId()` + `EFFECT_SEQ_LANE_MORPH`.  This path
does not use the scan or marker system at all.

### 2.4  PERF Page Layout

Subpage 0, 8 cells (menuPages.h line 72):

| Column | Text              | Parameter          | Automatable? |
|--------|-------------------|--------------------|-------------|
| 0      | TEXT_X_FADE       | PAR_MORPH          | No (global) |
| 1      | TEXT_VOICE1_MORPH | PAR_VOICE1_MORPH   | Yes         |
| 2      | TEXT_VOICE2_MORPH | PAR_VOICE2_MORPH   | Yes         |
| 3      | TEXT_VOICE3_MORPH | PAR_VOICE3_MORPH   | Yes         |
| 4      | TEXT_VOICE4_MORPH | PAR_VOICE4_MORPH   | Yes         |
| 5      | TEXT_VOICE5_MORPH | PAR_VOICE5_MORPH   | Yes         |
| 6      | TEXT_VOICE6_MORPH | PAR_VOICE6_MORPH   | Yes         |
| 7      | TEXT_EFFECT_MORPH | PAR_EFFECT_MORPH   | Yes         |

Only 4 cells are visible at a time (compact view; columns 0..3 or 4..7
depending on `activeParameter`).  PERF has only sub-page 0.

### 2.5  Scan Masks

```
va_searchSceneMask bits (existing):
  0x01  VA_SEARCH_SCENE_VOICE_MORPH_BIT   — one voice slot (VOICE pages)
  0x02  VA_SEARCH_SCENE_AUDIO_OUT_BIT     — one voice slot (VOICE pages)
  0x04  VA_SEARCH_SCENE_FX_SEND_BIT       — one voice slot (VOICE pages)
  0x08  VA_SEARCH_SCENE_EFFECT_MORPH_BIT  — scene-wide (EFFECT page)
```

The VOICE morph bit reports presence for a *single* slot (the viewed voice).
PERF needs morph presence for *all six voices independently*, so the existing
mask cannot serve PERF without a new data path.

---

## 3. Design

### 3.1  New Mask: `va_searchPerfMorphMask`

A new `static uint8_t` in menu.c:

| Bit | Meaning                         |
|-----|----------------------------------|
| 0   | Voice 0 morph automated          |
| 1   | Voice 1 morph automated          |
| 2   | Voice 2 morph automated          |
| 3   | Voice 3 morph automated          |
| 4   | Voice 4 morph automated          |
| 5   | Voice 5 morph automated          |
| 6   | Effect morph automated (pattern) |
| 7   | (unused)                         |

Cleared by `va_searchRestart()`, populated by `va_scanService()` in PERF
mode, consumed by `menu_applyPerfMarkers()`.

### 3.2  Scan Mode: PERF

When `menu_activePage == PERFORMANCE_PAGE`, `va_scanService()` operates in a
third mode: scans all 7 tracks (like Effect mode) but classifies only
voice morph and effect morph targets:

- `sceneModTarget_isSceneTarget(target)` → descriptor lookup →
  `SCENE_MOD_TARGET_KIND_VOICE_MORPH`: set bit `descriptor->voice_slot` in
  `va_searchPerfMorphMask`.
- `SCENE_MOD_TARGET_KIND_EFFECT_MORPH`: set bit 6 in
  `va_searchPerfMorphMask`.
- All other targets: ignored.

The existing `va_searchTargetMask[]` and `va_searchSceneMask` are unused on
PERF; `va_searchRestart()` clears them regardless, so they stay zero.

### 3.3  FX Sequence Lock Check

Effect morph can also be present via FX sequence locks (not Pattern
automation).  `menuEffects_cellSeqLocked()` checks all 16 FX seq steps for
a given lane.  For PERF, `menu_applyPerfMarkers()` builds a synthetic
`menuEffects_cell_t` with `.kind = MENU_FX_CELL_MORPH_AMOUNT` and calls
`menuEffects_cellSeqLocked()`.  This avoids duplicating the lock-scan logic.

### 3.4  Marker Rendering: `menu_applyPerfMarkers()`

Called from `menu_repaintGeneric()` tail, after `va_applyVoiceMarkers()` and
`menu_applyEffectMarkers()` (which both return early on PERF).  It walks the
4 visible compact-view cells:

- Column 0 (PAR_MORPH): skip — not automatable.
- Columns 1..6 (PAR_VOICEn_MORPH): automated if
  `va_searchPerfMorphMask & (1 << (column - 1))`.
- Column 7 (PAR_EFFECT_MORPH): automated if
  `va_searchPerfMorphMask & 0x40` OR `menuEffects_cellSeqLocked()` on
  the synthetic morph cell.

For an automated cell, find the first non-space character in
`editDisplayBuffer[0][4*i..4*i+2]`, probe it with `lcd_underlineGlyph()`,
and set the desired_base/marker_row/marker_col/desired_valid for that CGRAM
slot.  Queue via `va_queueMarkerTransaction()`.

The function always runs in compact view (PERF never enters edit mode), so
all 4 visible cells can be processed. PERF has no held-step value markers.

### 3.5  Search Restart on PERF

Extend the following gates to include PERFORMANCE_PAGE:

1. `menu_patternContentChanged()`: currently guards
   `!menu_isScreenPage()`.  Add `|| page == PERFORMANCE_PAGE` to the
   condition (or replace with a broader predicate).

2. `menu_switchPage()` PERFORMANCE_PAGE case: add `va_searchRestart()`
   on entry (page transition to PERF).

3. `menu_serviceRuntimeWidgets()`: add a PERF branch that calls
   `va_scanService()`.

### 3.6  CGRAM Retry for PERF

The existing retry logic (line 12076 area in `menu_serviceRuntimeWidgets()`)
checks `menu_isVoicePage() || menu_activePage == EFFECT_PAGE`.  Extend to
include `PERFORMANCE_PAGE`.

### 3.7  Pot-Clear Underline Drop

`menu_dropAutomationUnderline()` (if it exists) and
`menu_patternContentChanged()` handle the visual drop when pot-clear removes
the last automation entry.  Since `menu_patternContentChanged()` is being
extended to include PERF, the scan will restart and the underline will
disappear naturally on the next complete scan.

---

## 4. Implementation Schedule

### Change 1 — `menu.c`: new mask variable

**Where:** after `va_searchSceneMask` (line 1344).

**What:** declare `static uint8_t va_searchPerfMorphMask = 0u;` with a
comment block.

```c
/*
 * Per-voice morph presence mask for PERF mode.
 *
 * What: bits 0..5 report voice morph automation for voices 0..5; bit 6
 * reports effect morph automation from Pattern data. Set by va_scanService()
 * in PERF mode, cleared by va_searchRestart(). Consumed by
 * menu_applyPerfMarkers(). FX sequence morph locks are checked separately.
 */
#define VA_PERF_MORPH_EFFECT_BIT 0x40u
static uint8_t va_searchPerfMorphMask = 0u;
```

### Change 2 — `menu.c` `va_searchRestart()`: clear new mask

**Where:** `va_searchRestart()` (line 1926), after `va_searchSceneMask = 0u;`.

**What:** add `va_searchPerfMorphMask = 0u;`.

Also: set `va_searchTrack = 0u` when `menu_activePage == PERFORMANCE_PAGE`
(same as EFFECT_PAGE: scan from track 0).  Modify the existing line:

**Old:**
```c
va_searchTrack = (menu_activePage == EFFECT_PAGE) ? 0u : menu_activeVoice;
```

**New:**
```c
va_searchTrack = (menu_activePage == EFFECT_PAGE ||
                  menu_activePage == PERFORMANCE_PAGE) ? 0u : menu_activeVoice;
```

### Change 3 — `menu.c` `va_scanService()`: PERF classification branch

**Where:** inside the per-entry loop (line 2063), after the `if (effect_page)`
branch and the voice-page descriptor/scene-target branches.

**What:** add a third classification path for PERF.  The restructured logic:

The existing code at line 2041 sets `effect_page` from
`menu_activePage == EFFECT_PAGE`.  Add a `perf_page` variable:

```c
uint8_t perf_page = (uint8_t)(menu_activePage == PERFORMANCE_PAGE);
```

In the per-entry loop, add after the `if (effect_page)` block:

```c
if (perf_page) {
    if (sceneModTarget_isSceneTarget(autos[i].target)) {
        const scene_mod_target_descriptor_t *descriptor =
            sceneModTarget_descriptor(autos[i].target);

        if (descriptor) {
            if (descriptor->kind == SCENE_MOD_TARGET_KIND_VOICE_MORPH &&
                descriptor->voice_slot < 6u)
                va_searchPerfMorphMask |=
                    (uint8_t)(1u << descriptor->voice_slot);
            else if (descriptor->kind ==
                     SCENE_MOD_TARGET_KIND_EFFECT_MORPH)
                va_searchPerfMorphMask |= VA_PERF_MORPH_EFFECT_BIT;
        }
    }
    continue;
}
```

Also: the multi-track cursor advance logic (line 2102) must also apply to
PERF mode.  Change:

**Old:**
```c
if (effect_page && va_searchCursor >= NUM_STEPS &&
    (uint8_t)(va_searchTrack + 1u) < NUM_TRACKS) {
```

**New:**
```c
if ((effect_page || perf_page) && va_searchCursor >= NUM_STEPS &&
    (uint8_t)(va_searchTrack + 1u) < NUM_TRACKS) {
```

Also: the context-mismatch guard (line 2047) currently expects
`va_searchTrack == menu_activeVoice` on non-effect pages.  PERF must skip
this check since it scans from track 0, not from `menu_activeVoice`.  Change:

**Old:**
```c
if (va_searchPattern != menu_shownPattern ||
    (!effect_page && va_searchTrack != menu_activeVoice)) {
```

**New:**
```c
if (va_searchPattern != menu_shownPattern ||
    (!effect_page && !perf_page && va_searchTrack != menu_activeVoice)) {
```

### Change 4 — `menu.c`: new `menu_applyPerfMarkers()` function

**Where:** after `menu_applyEffectMarkers()` (after line ~2960, before
`va_underlineService()`).

**What:** new static function with comment block.

```c
/*
 * Apply PERF-page morph automation markers after the compact frame is formed.
 *
 * What: underlines the name of each PERF morph cell whose automation is
 * present in the viewed Pattern or the FX sequence (effect morph only).
 * Column 0 (PAR_MORPH, global) is never marked. Columns 1..6 check
 * va_searchPerfMorphMask bits 0..5 (Pattern voice morph). Column 7 checks
 * va_searchPerfMorphMask bit 6 (Pattern effect morph) and
 * menuEffects_cellSeqLocked() (FX seq morph lane).
 * Why: PERF morph cells are MENU_CELL_STATIC, so neither va_applyVoiceMarkers
 * nor menu_applyEffectMarkers can render them. The pot-clear path already
 * resolves PERF morph targets independently; this function adds the matching
 * visual presence indicator. Inputs: va_searchComplete,
 * va_searchPerfMorphMask, the compact view layout, editDisplayBuffer.
 * Output: up to 4 CGRAM marker transactions. Caller: menu_repaintGeneric().
 */
static void menu_applyPerfMarkers(void)
{
    uint8_t glyph_probe[8];
    uint8_t desired_base[4] = { 0u, 0u, 0u, 0u };
    uint8_t marker_row[4] = { 0u, 0u, 0u, 0u };
    uint8_t marker_col[4] = { 0u, 0u, 0u, 0u };
    uint8_t desired_valid = 0u;
    uint8_t activeParameter;
    uint8_t is2nd;
    uint8_t i;

    if (menu_activePage != PERFORMANCE_PAGE)
        return;

    activeParameter = (uint8_t)(menuIndex & MASK_PARAMETER);
    is2nd = (uint8_t)((activeParameter > 3u) ? 4u : 0u);

    for (i = 0u; i < 4u; i++) {
        uint8_t column = (uint8_t)(i + is2nd);
        uint8_t automated = 0u;
        int8_t left;
        uint8_t start;

        if (column == 0u)
            continue;
        if (column >= 1u && column <= 6u) {
            if (va_searchComplete &&
                (va_searchPerfMorphMask &
                 (uint8_t)(1u << (column - 1u))) != 0u)
                automated = 1u;
        } else if (column == 7u) {
            if (va_searchComplete &&
                (va_searchPerfMorphMask & VA_PERF_MORPH_EFFECT_BIT) != 0u)
                automated = 1u;
            if (!automated) {
                menuEffects_cell_t fx_cell;
                memset(&fx_cell, 0, sizeof(fx_cell));
                fx_cell.kind = MENU_FX_CELL_MORPH_AMOUNT;
                automated = menuEffects_cellSeqLocked(&fx_cell);
            }
        }
        if (!automated)
            continue;

        start = (uint8_t)(4u * i);
        for (left = 0; left < 3 &&
             editDisplayBuffer[0][start + left] == ' '; left++)
            ;
        if (left < 3 && lcd_underlineGlyph(
                (uint8_t)editDisplayBuffer[0][start + left], glyph_probe)) {
            desired_base[i] =
                (uint8_t)editDisplayBuffer[0][start + left];
            marker_row[i] = 0u;
            marker_col[i] = (uint8_t)(start + left);
            desired_valid |= (uint8_t)(1u << i);
        }
    }
    va_queueMarkerTransaction(desired_base, desired_valid,
                              marker_row, marker_col);
}
```

### Change 5 — `menu.c` `menu_repaintGeneric()`: call `menu_applyPerfMarkers()`

**Where:** tail of `menu_repaintGeneric()`, after `menu_applyEffectMarkers();`
(line 10485).

**What:** add `menu_applyPerfMarkers();`.

```c
    va_applyVoiceMarkers();
    menu_applyEffectMarkers();
    menu_applyPerfMarkers();
}
```

### Change 6 — `menu.c` `menu_serviceRuntimeWidgets()`: PERF scan branch

**Where:** after the EFFECT_PAGE branch (after line 12073), before the CGRAM
retry block.

**What:** add a PERF branch that calls `va_scanService()`.

```c
    /*
     * PERF-page morph automation-presence search (S077 P6).
     *
     * What: advances the search over all seven tracks of the viewed Pattern,
     * four step reads per pass, classifying only voice morph and effect morph
     * Scene targets into va_searchPerfMorphMask. Why: PERF underlines morph
     * names whose automation exists in the Pattern; the result feeds
     * menu_applyPerfMarkers(). Output: one menu_repaint() when the search
     * completes. Affiliates: va_scanService(), va_searchRestart().
     */
    if (menu_activePage == PERFORMANCE_PAGE)
        va_scanService();
```

### Change 7 — `menu.c` `menu_serviceRuntimeWidgets()`: extend CGRAM retry

**Where:** the CGRAM retry guard (line 12090).  The condition currently uses
`menu_isScreenPage(menu_activePage)` which is
`menu_isVoicePage() || page == EFFECT_PAGE`.

**What:** add `|| menu_activePage == PERFORMANCE_PAGE`.

**Old:**
```c
    if (menu_isScreenPage(menu_activePage) &&
```

**New:**
```c
    if ((menu_isScreenPage(menu_activePage) ||
         menu_activePage == PERFORMANCE_PAGE) &&
```

Also update the comment block above (line 12083) to mention PERF:
`The VOICE, Effect, and PERF pages share the transaction`; and add
`menu_applyPerfMarkers()` to the Affiliates line.

### Change 8 — `menu.c` `menu_patternContentChanged()`: extend guard

**Where:** line 2297.

**Old:**
```c
    if (!menu_isScreenPage(menu_activePage))
        return;
```

**New:**
```c
    if (!menu_isScreenPage(menu_activePage) &&
        menu_activePage != PERFORMANCE_PAGE)
        return;
```

### Change 9 — `menu.c` `menu_switchPage()`: restart search on PERF entry

**Where:** `menu_switchPage()`, PERFORMANCE_PAGE case (line 13307).

**What:** add `va_searchRestart();` after `preset_syncEffectMorphMirror();`.

```c
        if (pageNr == PERFORMANCE_PAGE) {
            /* S075: refresh PERF `fxm` after Effect/copy/clear changes. */
            preset_syncEffectMorphMirror();
            va_searchRestart();
        }
```

### Change 10 — `menu.c` `va_scanService()` comment block update

**Where:** the comment block above `va_scanService()` (lines 2016..2037).

**What:** add a third bullet for PERF mode, and update the "Caller" line.

Add to the bullet list after the Effect page bullet:

```
 *   - PERF page (S077 P6): all seven tracks, one after another through
 *     va_searchTrack; Scene voice morph targets set the per-voice bit in
 *     va_searchPerfMorphMask, and Scene effect morph sets bit 6.
 *     va_searchTargetMask[] is unused. Done after 896 steps (224 passes).
```

Update "Caller" to: `menu_serviceRuntimeWidgets() on VOICE, Effect, and
PERF pages.`

---

## 5. Risk Analysis

### 5.1  Stack

`menu_applyPerfMarkers()` uses 24 bytes of locals (4×uint8_t arrays + loop
variables), comparable to the existing marker functions.  `va_scanService()`
adds one `uint8_t perf_page` variable (1 byte).  No new stack pressure.

### 5.2  Scan Budget

PERF scan is 7 × 128 = 896 steps at 4 per pass = 224 passes.  At 500 Hz
service rate, the full scan completes in ~450 ms.  This matches the existing
Effect-page scan, which already runs at this budget without UI impact.

### 5.3  CGRAM Slot Contention

PERF never enters edit mode, so all 4 CGRAM marker slots are available.
Only 4 cells are visible at once in compact view.  No contention.

### 5.4  FX Seq Lock Check Cost

`menuEffects_cellSeqLocked()` scans 16 steps per call.  It is called at
most once per repaint (only for the effect morph column when visible and
when the pattern scan did not already set the bit).  Negligible cost.

### 5.5  Backward Compatibility

No behavioral change to VOICE or Effect pages.  The existing scan masks
(`va_searchTargetMask[]`, `va_searchSceneMask`) are unused on PERF; they are
cleared by `va_searchRestart()` and never read by `menu_applyPerfMarkers()`.
The existing marker functions (`va_applyVoiceMarkers()`,
`menu_applyEffectMarkers()`) exit early on PERF as before.

### 5.6  Pattern Change During PERF

`menu_patternContentChanged()` is extended to include PERF, so pastes and
clears that change the viewed pattern will restart the scan.  Scene switches
that change `menu_shownPattern` are detected by the existing
`va_searchPattern != menu_shownPattern` mismatch guard in `va_scanService()`.

---

## 6. Files Changed

| # | File | Function/Section | Nature |
|---|------|-----------------|--------|
| 1 | menu.c | new `va_searchPerfMorphMask` | variable + define |
| 2 | menu.c | `va_searchRestart()` | clear new mask + track init |
| 3 | menu.c | `va_scanService()` | PERF classification branch + multi-track advance |
| 4 | menu.c | new `menu_applyPerfMarkers()` | function |
| 5 | menu.c | `menu_repaintGeneric()` | call new function |
| 6 | menu.c | `menu_serviceRuntimeWidgets()` | PERF scan branch |
| 7 | menu.c | `menu_serviceRuntimeWidgets()` | CGRAM retry guard |
| 8 | menu.c | `menu_patternContentChanged()` | extend guard |
| 9 | menu.c | `menu_switchPage()` | restart search on PERF entry |
| 10 | menu.c | `va_scanService()` comment | add PERF bullet |

### Change 11 — `menu.c` `menu_automationTargetCleared()`: PERF branch

**Where:** `menu_automationTargetCleared()` (line 2311), after the
`else if (menu_activePage == EFFECT_PAGE)` block (line 2343).

**What:** add a PERF branch that clears the matching
`va_searchPerfMorphMask` bit immediately on pot-clear, so the underline
disappears without waiting for a full rescan.

```c
    } else if (menu_activePage == PERFORMANCE_PAGE) {
        if (sceneModTarget_isSceneTarget(target)) {
            const scene_mod_target_descriptor_t *descriptor =
                sceneModTarget_descriptor(target);

            if (descriptor) {
                if (descriptor->kind == SCENE_MOD_TARGET_KIND_VOICE_MORPH &&
                    descriptor->voice_slot < 6u)
                    va_searchPerfMorphMask &=
                        (uint8_t)~(1u << descriptor->voice_slot);
                else if (descriptor->kind ==
                         SCENE_MOD_TARGET_KIND_EFFECT_MORPH)
                    va_searchPerfMorphMask &= (uint8_t)~VA_PERF_MORPH_EFFECT_BIT;
            }
        }
    }
```

Also update the comment block (line 2304) to mention PERF:
`Inputs: canonical Pattern target. Output: the matching VOICE, Effect, or
PERF presence bit is cleared`.

---

## 7. Files Changed

| # | File | Function/Section | Nature |
|---|------|-----------------|--------|
| 1 | menu.c | new `va_searchPerfMorphMask` | variable + define |
| 2 | menu.c | `va_searchRestart()` | clear new mask + track init |
| 3 | menu.c | `va_scanService()` | PERF classification branch + multi-track advance |
| 4 | menu.c | new `menu_applyPerfMarkers()` | function |
| 5 | menu.c | `menu_repaintGeneric()` | call new function |
| 6 | menu.c | `menu_serviceRuntimeWidgets()` | PERF scan branch |
| 7 | menu.c | `menu_serviceRuntimeWidgets()` | CGRAM retry guard |
| 8 | menu.c | `menu_patternContentChanged()` | extend guard |
| 9 | menu.c | `menu_switchPage()` | restart search on PERF entry |
| 10 | menu.c | `va_scanService()` comment | add PERF bullet |
| 11 | menu.c | `menu_automationTargetCleared()` | PERF branch |

All changes are in `Core/Menu/menu.c`.  No header changes.  No new files.

---

## 8. Assessment (2026-10-08)

All 11 scheduled changes verified against the plan in `Core/Menu/menu.c`.

### Change-by-change verification

| # | Change | Verdict |
|---|--------|---------|
| 1 | `va_searchPerfMorphMask` + `VA_PERF_MORPH_EFFECT_BIT` (line 1370–1371) | **Match.** Comment block is richer than the plan (adds Why/Affiliates), correctly references `menuEffects_cellSeqLocked()`. |
| 2 | `va_searchRestart()` clears mask + PERF track init (lines 1958–1965) | **Match.** `va_searchPerfMorphMask = 0u;` added after `va_searchSceneMask = 0u;`. Track ternary extended with `PERFORMANCE_PAGE`. Comment updated. |
| 3 | `va_scanService()` PERF branch (lines 2078, 2085, 2109–2133, 2166) | **Match.** `perf_page` variable added. Context-mismatch guard skips track check for PERF. PERF classification block with comment classifies voice morph (per-slot bit) and effect morph (`VA_PERF_MORPH_EFFECT_BIT`). `continue` after classification. Multi-track advance extended with `perf_page`. |
| 4 | `menu_applyPerfMarkers()` (lines 3052–3130) | **Match.** Comment block matches plan. Logic: skip column 0; columns 1–6 check per-voice bit; column 7 checks pattern bit then falls back to `menuEffects_cellSeqLocked()` with synthetic cell. Name underline via `lcd_underlineGlyph()` + `va_queueMarkerTransaction()`. |
| 5 | `menu_repaintGeneric()` tail (line 10654) | **Match.** `menu_applyPerfMarkers();` added after `menu_applyEffectMarkers();`. |
| 6 | `menu_serviceRuntimeWidgets()` PERF scan branch (lines 12245–12256) | **Match.** Comment block present. Calls `va_scanService()` when `menu_activePage == PERFORMANCE_PAGE`. Positioned after the Effect branch, before the CGRAM retry. |
| 7 | CGRAM retry guard (lines 12258–12280) | **Match.** Guard extended: `(menu_isScreenPage(...) \|\| menu_activePage == PERFORMANCE_PAGE)`. Comment updated to mention PERF pages and `menu_applyPerfMarkers()` in Affiliates. |
| 8 | `menu_patternContentChanged()` guard (lines 2362–2364) | **Match.** Changed from `!menu_isScreenPage(...)` to `!menu_isScreenPage(...) && menu_activePage != PERFORMANCE_PAGE`. Comment block updated. |
| 9 | `menu_switchPage()` PERF entry (lines 13496–13508) | **Match.** `va_searchRestart();` added after `preset_syncEffectMorphMirror();`. Full comment block explains why and when, correctly notes `menu_activePage` is set above. |
| 10 | `va_scanService()` comment block (lines 2048–2072) | **Match.** Third bullet added for PERF mode. "One function serves all three modes" updated. Caller line updated to mention PERF. |
| 11 | `menu_automationTargetCleared()` PERF branch (lines 2423–2444) | **Match.** `else if (menu_activePage == PERFORMANCE_PAGE)` after the Effect block. Comment block present. Clears voice morph bit by slot and effect morph bit. Function header comment (line 2373) updated to mention PERF. |

### Required deviations (not in the schedule, correctly applied)

1. **`_Static_assert` 45 → 46** (line 1388–1398): the new byte participates
   in the overlay block's size check. Assert expression includes
   `sizeof(va_searchPerfMorphMask)` and the expected total is 46. The assert
   message reads "S077 VOICE/PERF overlay state must remain exactly 46 bytes".
   Without this the build would fail — correct and necessary.

2. **Overlay block comment** (lines 1264–1302): updated to 46 B, added budget
   line "+ 1 B PERF per-voice morph mask", added approval history entry
   "+1 B for the PERF per-voice morph presence mask in S077 P6", and added a
   "PERF-page sharing" paragraph (lines 1298–1302) that mirrors the existing
   "Effect-page sharing" paragraph. Thorough.

3. **Forward declaration** `static void menu_applyPerfMarkers(void);`
   (line 1900): placed next to the existing `menu_applyEffectMarkers()`
   declaration. Not strictly required (definition precedes the call) but
   consistent with the file's style.

4. **Comment updates** beyond what the schedule listed: `va_searchRestart()`
   comment (lines 1934–1954) mentions PERF and updates the Callers line;
   `menu_patternContentChanged()` comment (lines 2344–2358) adds S077 P6 and
   PERF to the description; `menu_automationTargetCleared()` header (line 2373)
   updated to mention PERF. All accurate.

### Correctness

- The PERF classification block (Change 3) runs only for Scene targets
  (`sceneModTarget_isSceneTarget()`), correctly ignoring voice descriptor and
  effect block-7 targets that PERF does not display. Each voice morph bit is
  set by `descriptor->voice_slot` (0..5), matching the PERF column layout
  (columns 1..6 = voice 0..5).

- The multi-track advance (Change 3) uses the same `va_searchTrack + 1 <
  NUM_TRACKS` test as the Effect page, so PERF scans all 7 tracks and
  terminates correctly.

- The context-mismatch guard (Change 3) correctly skips the
  `va_searchTrack != menu_activeVoice` check for PERF, since PERF scans from
  track 0, not the active voice.

- `menu_applyPerfMarkers()` (Change 4) correctly handles the 4/8 split: only
  4 cells are visible, offset by `is2nd`, and CGRAM slots 0..3 map to visible
  cells i=0..3. Column 0 is always skipped. The FX seq lock fallback for
  column 7 creates a well-formed synthetic cell (`memset` + `.kind` set).

- The immediate-drop path (Change 11) matches the scan classification exactly:
  voice morph clears by `descriptor->voice_slot`, effect morph clears
  `VA_PERF_MORPH_EFFECT_BIT`. This ensures pot-clear drops the underline
  without a 450 ms rescan delay.

- No path can produce stale PERF markers on VOICE/Effect pages or vice versa:
  every `menu_switchPage()` entry to VOICE (default case, line ~13573) and
  Effect (line ~13540) already calls `va_searchRestart()`, and the new PERF
  entry (Change 9) does the same.

### No issues found
