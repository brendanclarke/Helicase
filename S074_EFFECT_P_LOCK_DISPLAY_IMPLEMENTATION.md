# S074 — Effect-page automation underline: implementation schedule

**Status:** schedule only. No code has been changed.

**Base:** branch `dev-ph5-effects`, HEAD `ca77891`, clean tree. Every line
number below is a HEAD `ca77891` line number **before any edit**. Apply the
changes in each file **from the highest line number down** (§6.1 gives the
order) so the cited numbers stay valid while you work.

**Goal (user, 2026-09-29):** on the Effect page (SHIFT+PERF), underline a
parameter's name when it is automated in the FX sequencer **or** in the
current Pattern, in both the 4-parameter (compact) and 1-parameter (full)
view, as the VOICE pages do. The held-step value underline already works and
is kept.

**Files:** `Core/Menu/menu.c`, `Core/Menu/menuEffects.c`,
`Core/Menu/menuEffects.h`, `Core/Menu/menu.h` (comment only), `config.h`
(comment only). There is no change to EffectsManager, PatternData, SceneData,
the sequencer or any DSP/ISR path.

**Cost:** 0 B of RAM (static and stack), no DSP or ISR work, and an estimated
few hundred bytes of flash.

---

## 1. Root cause

| Piece | VOICE pages | Effect page (today) |
|---|---|---|
| Pattern presence search | `va_scanService()` (`menu.c:1905`) scans the viewed Pattern's active track, 4 steps per pass | **none**: `menu_serviceRuntimeWidgets()` (`menu.c:11197`) calls the scan only when `menu_isVoicePage()` |
| Name marker | `va_applyVoiceMarkers()` (`menu.c:2381`) underlines the first letter of an automated name (row 0) | **none**: `menu_applyEffectMarkers()` (`menu.c:2558`) only draws the held-step value marker (row 1), and `continue`s when the lane is not locked |
| FX-sequence lock presence | n/a | read nowhere except the SEQ LEDs (`menuEffects_renderSeqLeds()`, `menuEffects.c:824`) |
| Deferred-marker retry | `menu.c:11205` | **none**: a marker transaction deferred for a full LCD queue stays missing until an unrelated repaint |

The fix gives the Effect page the missing pieces and reuses the VOICE
machinery: the same bounded search, extended to all seven tracks; the same
CGRAM marker transaction; the same retry.

---

## 2. Behaviour after the fix

### 2.1 What counts as "automated", per cell

**Rule (user, 2026-09-29):** a parameter's name is underlined when *any*
automation data addresses it, in the FX sequence or in the Scene's Pattern,
whether or not that data would play with the current settings. The underline
reports what is stored, not what is audible.

| Effect cell | Pattern automation (viewed Pattern: any step 0..127 of any of the 7 tracks) | FX-sequence lock (active Scene: any of the 16 steps) |
|---|---|---|
| `typ`, `run`, `len`, `scl` | never | never |
| `mrp` | a Scene target `fxm` entry (ID 404) | lane 0 (Effect Morph) |
| `out` and other PARAM rows without a lane | an entry for block-7 ID `448 + index` | never (no lane) |
| PARAM rows with a lane (`flt`: `frq res drv typ vol pan`) | as above | the row's registry lane `L` (1..15) |

Nothing that decides whether automation *plays* filters the underline:

- **FX sequence:** `len`, the run mode, the `sel` step and the transport.
- **Pattern:** the track length, mute, the step's trigger bit, probability,
  and the current Effect type's AUTOMATABLE flags. An entry left over from an
  earlier type still underlines the row with that local index.

The only entry ignored is `PAT_AUTOMATION_TARGET_OFF` (`0x1FF`). It decodes as
Effect local 63 but is the stored "target off" value, not automation (§4.1).

### 2.2 Which marker a cell shows (at most one per cell)

1. **SEQ steps held, and the first held step locks this cell's lane:** the
   held value on row 1 with its rightmost glyph underlined. This is today's
   behaviour, unchanged.
2. **Otherwise, the cell is automated (§2.1):** the name on row 0 with its
   first non-space character underlined.
   - Compact view: the 3-character short name at columns `4i..4i+2` (the
     active cell is uppercased by `upr_three()`; `lcd_underlineGlyph()`
     accepts `A–Z`, `a–z` and `0–9`).
   - Full view: the 8-character long name at columns `8..15` (`mrp` shows
     `Effect  Morph` there, so its `M` is underlined).
3. **Otherwise:** no marker.

A held cell whose lane is **not** locked on the first held step still shows
the held value (unchanged) and now also gets the name marker when it is
automated. This matches the VOICE rule, where a held step without the target
falls back to the name marker.

### 2.3 When the underline appears

- **FX-lock underlines** are read live from the Scene record on every
  repaint, so they appear immediately and need no search.
- **Pattern underlines** appear when the Pattern search completes: 7 tracks ×
  128 steps at 4 step reads per foreground pass = **224 passes** (a VOICE
  page needs 32). Completion triggers one repaint.

---

## 3. Decisions (S074 startup §2.4)

| # | Resolution | Basis |
|---|---|---|
| U1 | All 7 tracks of the viewed Pattern | User: "automated ... in the current pattern". Effect targets are Scene-wide; playback applies them from any track. |
| U2 | `menu_shownPattern` | VOICE parity; the STEP `fx` editor also edits `menu_getViewedPattern()`. |
| U3 | **All 16 steps count**, whatever `len` is | User correction (2026-09-29): any stored automation counts, whether or not it plays. The same rule applies to the Pattern side, so the current type's AUTOMATABLE flags do not filter either (§2.1). The SEQ LEDs are not changed and still light only steps within `len`. |
| U4 | `mrp` is underlined for `fxm` Pattern automation and for lane-0 locks | User: "same as for voice" (VOICE underlines a voice's Morph cell for its Scene target). |
| U5 | Full view included | User: "both 4-parameter and 1-parameter view". |
| U6 | The held locked value owns its cell; otherwise the name marker | VOICE parity (`va_applyVoiceMarkers()`). |
| U7 | **Reuse the VOICE search state: 0 B** | The reset coverage is proven in §4.2. No new allocation, so no RAM approval is needed. |
| U8 | The restart points in §4.4 | FX locks need no restart (live read). |

---

## 4. Design

### 4.1 Sources of truth

- **Pattern automation:** `pat_readStepAutomations(menu_shownPattern, track,
  step, …)`, tracks 0..6, steps 0..127.
  - An Effect parameter entry is `effectTarget_isEffectId(t)`, and its local
    index is `effectTarget_local(t)`.
  - **Local 63 must be rejected.** `PAT_AUTOMATION_TARGET_OFF` is `0x1FF`,
    which decodes as Effect local 63. A STEP-page "Add" that is still `off`
    is a valid stored entry (`pat_writeStepAutomation()` accepts it), so
    without the guard such an entry would set bit 63.
  - `fxm` is `sceneModTarget_descriptor(t)->kind ==
    SCENE_MOD_TARGET_KIND_EFFECT_MORPH`.
- **FX locks:** `scene_effectConst(scene_getActiveIndex())->steps[s].lock_mask`,
  bit `L`, for every step `s` in 0..15. The lane of a cell comes from the
  existing `menuEffects_cellLane()` (`menuEffects.c:723`). Lane meaning always
  belongs to the current type, because a type change clears every step
  (`effects_changeTypeScene()`).
- **Pattern steps:** `pat_readStepAutomations()` reads a step's automation
  block whether or not the step's trigger bit is set (it tests only
  `PAT_ADDR_SPECIALS_BIT`). The search visits all 128 steps of every track, so
  the track length, mute and probability never hide an entry.
- **Type independence:** the search records raw locals, and the underline
  applies no type filter. A `typ` change therefore needs no rescan: it does not
  touch Pattern data, and the page repaints on the change anyway.

### 4.2 Shared search state (U7): why reuse is safe

The Effect page reuses the 13 VOICE search bytes (`va_searchTrack`,
`va_searchPattern`, `va_searchCursor`, `va_searchComplete`,
`va_searchTargetMask[8]`, `va_searchSceneMask`).

- **Mutually exclusive:** `menu_activePage` is one page at a time.
- **Both entries restart the search:**
  - Effect entry: C13 (new).
  - VOICE entry from any non-VOICE page: `menu.c:12480` (`!was_voice_page`).
- **Role of each byte on the Effect page:**
  - `va_searchTrack` is the 0..6 track cursor.
  - `va_searchTargetMask[]` holds Effect locals 0..62.
  - `va_searchSceneMask` uses a new bit, `VA_SEARCH_SCENE_EFFECT_MORPH_BIT`
    (`0x08`).

Every reader and writer of the shared bytes:

| Site (HEAD line) | Runs on | Effect-page consequence after the fix |
|---|---|---|
| `va_scanService()` (1905), from `menu_serviceRuntimeWidgets()` | VOICE; Effect (C12) | Scans in Effect mode (C8) |
| `va_searchRestart()` (1843) | any page | Chooses the geometry from the page (C6) |
| Voice-page entry restart (12482) | VOICE entry | Discards any Effect result when you leave for a VOICE page |
| Effect-page entry restart (C13, new) | Effect entry | Discards any VOICE result |
| `menu_setActiveVoice()` restart (13121) | any page; SHIFT+TRACK reaches it on the Effect page | Skipped on the Effect page (C14): the search already covers every track |
| `menu_setShownPattern()` restart (13237) | VOICE; Effect (C15) | Restart and repaint on a Pattern change |
| `menu_voiceAutoOverlayPatternDeleted()` (2141) | VOICE; Effect (C9) | Restart after SHIFT+COPY clear, which is not page-gated |
| STEP automation delete restarts (9287, 9292) | SEQ page context | Not reachable from the Effect page. The next Effect entry restarts. |
| `va_writeAutomationFromKnob()` bit writes (2783–2785) | VOICE overlay only | none |
| `va_applyVoiceMarkers()` reads (2439–2530) | returns unless VOICE (2393) | none |
| `menu_effectCellAutomated()` reads (C10, new) | Effect only | — |

The CGRAM cache (`va_cgramBase[4]`, `va_cgramValid`) was already shared in
S072 through `va_queueMarkerTransaction()`. The held, debounce and working
bytes stay VOICE-only; the Effect hold lives in `menuEffects.c`.

### 4.3 Scan geometry

`va_scanService()` keeps one loop and one 252-byte entry buffer
(`pat_automation_entry_t` is 4 bytes × 63).

- **VOICE page:** `va_searchTrack` is fixed at the active voice; the search
  is done when the cursor reaches 128. This behaviour is unchanged.
- **Effect page:** after step 127 of tracks 0..5, the cursor moves to step 0
  of the next track. On track 6 it stops at 128, which completes the search.
- **Budget:** `VOICE_AUTOMATION_SCAN_STEPS_PER_PASS` (4) step reads per pass
  on both pages, so a pass still makes at most 4 × 63 comparisons.

**Why one function rather than a second scan function:** a second static
function would carry its own 252-byte array. After LTO inlining into
`menu_serviceRuntimeWidgets()`, whether the two arrays share a stack slot is
up to the compiler. One function makes the zero stack growth structural and
not something to measure.

### 4.4 Restart and refresh points (U8)

| Event on the Effect page | Handling |
|---|---|
| Page entry (from any other page) | C13: restart |
| Repeated SHIFT+PERF (screen toggle) | no restart (same page) |
| Pattern change (PERF/MIDI/follow Scene switch, Bank realign) | C15: restart and repaint. The Pattern check in C8 is the backstop. |
| Pattern or track clear (SHIFT+COPY) | C9: restart and repaint |
| SHIFT+TRACK (active track change) | C14: no restart |
| `typ` change | none needed (§4.1); the page already repaints |
| FX lock edit, Scene switch | none needed (live read); the page already repaints (hold release returns `MENU_FX_ACT_REPAINT`; encoder and pot edits repaint) |
| `len`, run mode, track length, mute, probability | no effect on the underline (§2.1) |
| STEP-page `fx` automation edits | made on the SEQ page; the next Effect entry restarts |
| Live erase while recording | **not handled**, as on the VOICE pages (§9) |

---

## 5. Resource accounting

| Resource | Change |
|---|---|
| Static RAM | **0 B.** No new variables. `_Static_assert(... == 45u)` at `menu.c:1295` stays true. `va_searchSceneMask` gains a bit value, not a byte. |
| Stack | **0 B added by design.** No new arrays. The scan keeps its single 252-byte buffer (§4.3). The new helpers use only a few scalar locals. |
| Flash | An estimated few hundred bytes of `.text`; measure with `link_budget.py` (§7). |
| CPU | Foreground only. The scan budget per pass is unchanged. Each Effect repaint adds at most 4 cells × 16 lock-mask reads. No DSP or ISR change. |
| ITCM / DTCM / FX arena | unchanged |

---

## 6. Change schedule

### 6.0 Summary

| ID | File | Lines (HEAD) | Action | What |
|---|---|---|---|---|
| C1 | `config.h` | 399–417 | MODIFY (comment) | The scan budget also bounds the Effect-page search |
| C2 | `Core/Menu/menu.h` | 435–436 | MODIFY (comment) | `menu_voiceAutoOverlayPatternDeleted()` contract covers the Effect page |
| C3 | `Core/Menu/menu.c` | after 1251 | ADD (comment) | Effect-page sharing paragraph in the VOICE state block |
| C4 | `Core/Menu/menu.c` | before 1258 | ADD (comment) | Per-field roles of the search bytes |
| C5 | `Core/Menu/menu.c` | 1263–1274, after 1277 | MODIFY comment, ADD define | `VA_SEARCH_SCENE_EFFECT_MORPH_BIT` |
| C6 | `Core/Menu/menu.c` | 1833–1851 | MODIFY | `va_searchRestart()` picks the geometry from the page |
| C7 | `Core/Menu/menu.c` | after 1892 | ADD | `va_searchRecordEffectTarget()` |
| C8 | `Core/Menu/menu.c` | 1894–1959 | MODIFY (replace) | `va_scanService()` seven-track Effect mode |
| C9 | `Core/Menu/menu.c` | 2127–2144 | MODIFY | Pattern-clear restart also on the Effect page |
| C10 | `Core/Menu/menu.c` | after 2549 | ADD | `menu_effectCellAutomated()` |
| C11 | `Core/Menu/menu.c` | 2551–2613 | MODIFY (replace) | `menu_applyEffectMarkers()` name markers |
| C12 | `Core/Menu/menu.c` | 11191–11223 | MODIFY (REMOVE 11201–11208; ADD ×2) | Effect scan call; shared marker retry |
| C13 | `Core/Menu/menu.c` | 12449–12450 | ADD | Restart the search on Effect entry |
| C14 | `Core/Menu/menu.c` | 13114–13125 | MODIFY | No restart on SHIFT+TRACK from the Effect page |
| C15 | `Core/Menu/menu.c` | 13215–13243 | MODIFY | Restart on a Pattern change on the Effect page |
| C16 | `Core/Menu/menuEffects.h` | after 108 | ADD | `menuEffects_cellSeqLocked()` declaration |
| C17 | `Core/Menu/menuEffects.c` | after 815 | ADD | `menuEffects_cellSeqLocked()` (any of the 16 steps) |

`menuEffects_renderSeqLeds()` is **not** changed: the SEQ LEDs keep
lighting only steps within `len`.

### 6.1 Order of application (highest line first within each file)

- `menu.c`: C15 → C14 → C13 → C12 → C11 → C10 → C9 → C8 → C7 → C6 → C5 →
  C4 → C3.
- `menuEffects.c`: C17.
- `menuEffects.h`: C16.
- `menu.h`: C2.
- `config.h`: C1.

Dependencies:

- C8 calls C7.
- C11 calls C10.
- C10 calls C17, which C16 declares.
- C7 and C10 use the define added in C5.
- C12 calls `va_scanService()` on the Effect page, which needs C6 and C8
  first, or the Effect page would run a VOICE-mode scan.

---

### C1 — `config.h` L399–417 — MODIFY (comment only)

Replace the comment block above `BUTTON_HOLD_DELAY_MS` (lines 399–417) with
the block below. The three `#define`s at 418–420 are unchanged.

```c
/* -----------------------------------------------------------------------
** VOICE overlay, Effect-page underline, and UI hold-gesture timing.
**
** What: three tunable constants governing the VOICE-page held-step
** automation overlay (S066) and the automation-presence search shared by the
** VOICE and Effect pages (S074). BUTTON_HOLD_DELAY_MS is the common short
** long-press threshold shared by every UI gesture that distinguishes a hold
** from a tap; the current default is 200 ms. VOICE_AUTOMATION_UNDERLINE_QUIET_MS
** is the quiet period before reapplying a value underline after a rapid pot
** edit. VOICE_AUTOMATION_SCAN_STEPS_PER_PASS bounds the asynchronous Pattern
** search to four step reads per foreground pass on both pages: a VOICE page
** reads its one track (128 steps, 32 passes); the Effect page reads all
** seven tracks of the viewed Pattern (896 steps, 224 passes).
**
** Why: timing belongs in config.h so one clean rebuild applies the same
** thresholds to every UI path. The search budget also keeps the worst case at
** 4 * 63 = 252 automation-entry comparisons per pass on either page.
**
** Inputs: none (compile-time constants). Outputs: buttonHandler and Menu UI
** timing/search policy. Affiliates: time_sysTick, buttonHandler_tick(),
** va_scanService(), and menu_serviceRuntimeWidgets().
** ----------------------------------------------------------------------- */
```

---

### C2 — `Core/Menu/menu.h` L435–436 — MODIFY (comment only)

**Current (L435–436):**

```c
/* Notify the VOICE search after an in-place Pattern/track clear. */
void menu_voiceAutoOverlayPatternDeleted(void);
```

**New:**

```c
/*
 * Invalidate the automation-presence search after an in-place Pattern/track
 * clear.
 *
 * What: restarts the bounded search shared by the VOICE pages (active track)
 * and the Effect page (all seven tracks, S074), then repaints, so a name
 * underline whose last target was cleared disappears once the rescan
 * completes. Why: a cleared target cannot be proven absent from the rest of
 * the Pattern without a full rescan, and the SHIFT+COPY clear is not
 * page-gated. Inputs: none; call after the PatternData clear has been
 * submitted. Outputs: a cleared search and a repaint on VOICE and Effect
 * pages; nothing on other pages, whose next VOICE/Effect entry restarts the
 * search anyway. Callers: copyClear_clearCurrentPattern(),
 * copyClear_clearCurrentTrack(). Affiliates: va_searchRestart() and
 * va_scanService() in menu.c.
 */
void menu_voiceAutoOverlayPatternDeleted(void);
```

---

### C3 — `Core/Menu/menu.c` after L1251 — ADD (comment only)

Inside the existing "VOICE held-step automation overlay state" block, after
line 1251 (` * extended +1 B for the Scene-target search mask in S070
remediation.`) and before the closing ` */` on line 1252, add:

```c
 *
 * Effect-page sharing (S074, +0 B): the Effect page (SHIFT+PERF) reuses the
 * 13 search bytes for its seven-track automation-presence search, and the
 * 5 CGRAM bytes for its markers (the CGRAM bytes have been shared since
 * S072 through menu_applyEffectMarkers()). The pages are mutually exclusive
 * and every entry to either page restarts the search, so a result never
 * crosses pages; va_searchRestart() selects the page's scan geometry. The
 * held, debounce, and working-value bytes remain VOICE-only (the Effect SEQ
 * hold lives in menuEffects.c).
```

---

### C4 — `Core/Menu/menu.c` before L1258 — ADD (comment only)

Between line 1257 (blank) and line 1258 (`static uint8_t va_searchTrack =
0u;`), add:

```c
/*
 * Pattern-wide automation-presence search (13 B with va_searchSceneMask).
 *
 * va_searchPattern: the Pattern the result belongs to (menu_shownPattern at
 *   restart); a mismatch restarts the search.
 * va_searchTrack: VOICE pages - the scanned track (menu_activeVoice at
 *   restart; a mismatch restarts). Effect page - the 0..6 track cursor of the
 *   seven-track scan (S074).
 * va_searchCursor: next step 0..127 on va_searchTrack; NUM_STEPS on the last
 *   track to scan means every step has been read.
 * va_searchComplete: nonzero once the search has read every step; markers
 *   use the masks only then, so a partial result never shows.
 * va_searchTargetMask[8]: one bit per local 0..63 - the VOICE slot's
 *   descriptor index, or the Effect local of block-7 target 448 + local.
 * Writers: va_searchRestart(), va_scanService(), va_searchRecordEffectTarget(),
 * and (VOICE held-step writes) va_writeAutomationFromKnob(). Readers:
 * va_applyVoiceMarkers() and menu_effectCellAutomated().
 */
```

---

### C5 — `Core/Menu/menu.c` L1263–1274 MODIFY (comment); after L1277 ADD (define)

**Replace the comment at L1263–1274** with:

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

**Add after L1277** (`#define VA_SEARCH_SCENE_FX_SEND_BIT     0x04u`),
before L1278 (`static uint8_t va_searchSceneMask = 0u;`):

```c
/* Effect page only: `fxm` Pattern automation for the `mrp` cell (S074). */
#define VA_SEARCH_SCENE_EFFECT_MORPH_BIT 0x08u
```

---

### C6 — `Core/Menu/menu.c` L1833–1851 — MODIFY `va_searchRestart()`

Replace lines 1833–1851 (comment and function) with:

```c
/*
 * Restart the asynchronous automation-presence search.
 *
 * What: records the viewed Pattern, sets the track context, clears both
 * presence masks and the completion flag, and resumes at step zero. VOICE
 * pages record menu_activeVoice as the one track to scan; the Effect page
 * (S074) starts its seven-track cursor at track 0.
 * Why: a result from another Pattern, track, voice slot, or page must never
 * produce a stale name underline. VOICE and Effect share this state (0 B),
 * so the restart is the single place that selects the page's scan geometry;
 * callers must therefore set menu_activePage before calling it.
 * Inputs: menu_activePage, menu_shownPattern, menu_activeVoice. Outputs:
 * cleared va_search* state; markers from the search stay absent until
 * va_scanService() completes the new search (FX-lock underlines on the
 * Effect page do not depend on it).
 * Callers: VOICE and Effect entry in menu_switchPage(), menu_setActiveVoice()
 * (not on the Effect page), menu_setShownPattern(),
 * menu_voiceAutoOverlayPatternDeleted(), the STEP automation deletes, and
 * va_scanService() on a context mismatch. Affiliates: va_scanService(),
 * va_searchSetBit(), va_applyVoiceMarkers(), menu_effectCellAutomated().
 */
static void va_searchRestart(void)
{
    va_searchTrack = (menu_activePage == EFFECT_PAGE) ? 0u : menu_activeVoice;
    va_searchPattern = menu_shownPattern;
    va_searchCursor = 0u;
    va_searchComplete = 0u;
    memset(va_searchTargetMask, 0, sizeof(va_searchTargetMask));
    va_searchSceneMask = 0u;
}
```

`EFFECT_PAGE` is already visible at this point (it is used at `menu.c:1817`).

---

### C7 — `Core/Menu/menu.c` after L1892 — ADD `va_searchRecordEffectTarget()`

After line 1892 (the closing `}` of `va_sceneSearchBitForCell()`) and the
blank line 1893, and before the `va_scanService()` comment, add:

```c
/*
 * Record one Pattern automation entry for the Effect-page search (S074).
 *
 * What: sets the presence bit for an Effect parameter target (block 7, IDs
 * 448..510 = locals 0..62) or for the Scene Effect Morph target `fxm`. Every
 * other target (voice descriptors, other Scene targets, the automation-off
 * sentinel) is ignored.
 * Why: Effect targets are Scene-wide and may be written on any of the seven
 * tracks (the STEP page `fx` category), so the Effect page classifies entries
 * differently from a VOICE page, which keeps only its own slot's targets.
 * Local 63 must be rejected explicitly: PAT_AUTOMATION_TARGET_OFF (0x1FF) is
 * a valid stored entry (a STEP-page Add still set to `off`) and decodes as
 * Effect local 63, which Pattern automation never addresses.
 * Input: one stored 9-bit target. Output: bit `local` in
 * va_searchTargetMask[], or VA_SEARCH_SCENE_EFFECT_MORPH_BIT in
 * va_searchSceneMask. The result is type-independent (raw locals) and the
 * underline applies no type filter (any stored automation counts, S074 rule),
 * so an Effect type change needs no rescan.
 * Caller: va_scanService() on EFFECT_PAGE. Affiliates:
 * effectTarget_isEffectId() and effectTarget_local() (EffectTypes.h),
 * sceneModTarget_descriptor(), and seq_drainPendingAutomation() (the playback
 * decoding of the same IDs).
 */
static void va_searchRecordEffectTarget(uint16_t target)
{
    if (effectTarget_isEffectId(target)) {
        uint8_t local = effectTarget_local(target);

        if (local < EFFECT_TARGET_PATTERN_LOCAL_LIMIT)
            va_searchSetBit(local);
    } else if (sceneModTarget_isSceneTarget(target)) {
        const scene_mod_target_descriptor_t *descriptor =
            sceneModTarget_descriptor(target);

        if (descriptor &&
            descriptor->kind == SCENE_MOD_TARGET_KIND_EFFECT_MORPH)
            va_searchSceneMask |= VA_SEARCH_SCENE_EFFECT_MORPH_BIT;
    }
}

```

---

### C8 — `Core/Menu/menu.c` L1894–1959 — MODIFY (replace) `va_scanService()`

Replace lines 1894–1959 (comment and function) with the code below.
Differences from HEAD:

- An `effect_page` flag.
- The track comparison in the context check applies to VOICE only.
- One `if (effect_page) { …; continue; }` at the top of the entry loop.
- The cursor increment moves from the `for` header into the body, followed by
  the Effect track advance.

The VOICE classification lines are byte-identical, and for VOICE the loop
behaves exactly as before.

```c
/*
 * Advance the automation-presence search by the configured bounded slice.
 *
 * What: reads at most VOICE_AUTOMATION_SCAN_STEPS_PER_PASS step lists of the
 * viewed Pattern and records the targets that the current page underlines.
 *   - VOICE page: the active track only; voice descriptor targets and
 *     per-voice Scene targets owned by the page's slot. Done after 128 steps
 *     (32 passes).
 *   - Effect page (S074): all seven tracks, one after another through
 *     va_searchTrack; Effect parameter targets and `fxm`, classified by
 *     va_searchRecordEffectTarget(). Done after 896 steps (224 passes).
 * Why: scanning a Pattern synchronously on every repaint would stall the UI.
 * One function serves both pages so the 252-byte entry buffer exists once on
 * the stack (S074 adds no stack).
 * Inputs: the current search context, menu_activePage, and the PatternData
 * pool. Output: complete presence masks and one menu_repaint() when the last
 * step is read, with a hard 4*63 comparison ceiling per service pass on
 * either page. A Pattern change (and, on VOICE pages, a track change)
 * restarts the search. Caller: menu_serviceRuntimeWidgets() on VOICE and
 * Effect pages. Affiliates: instrumentParam_make namespace,
 * sceneModTarget_descriptor(), va_searchRecordEffectTarget(), PatternData.
 */
static void va_scanService(void)
{
    pat_automation_entry_t autos[PAT_BLOCK_AUTO_COUNT_MASK];
    uint8_t effect_page = (uint8_t)(menu_activePage == EFFECT_PAGE);
    uint8_t slot;
    uint8_t budget;

    if (va_searchComplete)
        return;
    if (va_searchPattern != menu_shownPattern ||
        (!effect_page && va_searchTrack != menu_activeVoice)) {
        va_searchRestart();
        return;
    }

    slot = menu_voicePageToSlot(menu_activePage);
    for (budget = 0u;
         budget < VOICE_AUTOMATION_SCAN_STEPS_PER_PASS &&
         va_searchCursor < NUM_STEPS;
         budget++) {
        uint8_t count = pat_readStepAutomations(
            va_searchPattern, va_searchTrack, va_searchCursor,
            autos, PAT_BLOCK_AUTO_COUNT_MASK);
        uint8_t i;

        for (i = 0u; i < count; i++) {
            if (effect_page) {
                va_searchRecordEffectTarget(autos[i].target);
                continue;
            }
            if (instrumentParam_isVoiceParameter(autos[i].target) &&
                instrumentParam_slot(autos[i].target) == slot)
                va_searchSetBit(instrumentParam_local(autos[i].target));
            else if (sceneModTarget_isSceneTarget(autos[i].target)) {
                const scene_mod_target_descriptor_t *descriptor =
                    sceneModTarget_descriptor(autos[i].target);

                if (descriptor && descriptor->voice_slot == slot) {
                    switch (descriptor->kind) {
                    case SCENE_MOD_TARGET_KIND_VOICE_MORPH:
                        va_searchSceneMask |= VA_SEARCH_SCENE_VOICE_MORPH_BIT;
                        break;
                    case SCENE_MOD_TARGET_KIND_AUDIO_OUT:
                        va_searchSceneMask |= VA_SEARCH_SCENE_AUDIO_OUT_BIT;
                        break;
                    case SCENE_MOD_TARGET_KIND_FX_SEND:
                        va_searchSceneMask |= VA_SEARCH_SCENE_FX_SEND_BIT;
                        break;
                    default:
                        break;
                    }
                }
            }
        }
        va_searchCursor++;
        /*
         * Effect page: step 127 of tracks 0..5 continues at step 0 of the
         * next track. Track 6 leaves the cursor at NUM_STEPS, which ends the
         * loop and completes the search below. VOICE pages never advance.
         */
        if (effect_page && va_searchCursor >= NUM_STEPS &&
            (uint8_t)(va_searchTrack + 1u) < NUM_TRACKS) {
            va_searchCursor = 0u;
            va_searchTrack++;
        }
    }
    if (va_searchCursor >= NUM_STEPS) {
        va_searchComplete = 1u;
        menu_repaint();
    }
}
```

`slot` is unused on the Effect page (`menu_voicePageToSlot(EFFECT_PAGE)`
returns 5 harmlessly). It is computed unconditionally to keep the VOICE lines
unchanged.

---

### C9 — `Core/Menu/menu.c` L2127–2144 — MODIFY `menu_voiceAutoOverlayPatternDeleted()`

Replace lines 2127–2144 with:

```c
/*
 * Invalidate the automation-presence result after a destructive Pattern clear.
 *
 * What: restarts the bounded search, cancels any pending VOICE value-marker
 * debounce while keeping the held-step context, and repaints. Runs on VOICE
 * pages (active-track search) and, since S074, on the Effect page
 * (seven-track search). Why: removing a target cannot be proven absent from
 * the remaining steps without a full rescan, and the SHIFT+COPY clear gesture
 * is not page-gated, so it can run while the Effect page is visible. Inputs:
 * an already-submitted copy/clear PatternData mutation. Outputs: a cleared
 * search result and a refreshed frame; other pages return at once because
 * their next VOICE/Effect entry restarts the search anyway. Callers:
 * copyClear_clearCurrentPattern(), copyClear_clearCurrentTrack().
 * Affiliates: va_searchRestart(), va_scanService(), copyClearTools.c.
 */
void menu_voiceAutoOverlayPatternDeleted(void)
{
    if (!menu_isScreenPage(menu_activePage))
        return;
    va_searchRestart();
    va_underlineSuppressed = 0u;
    menu_repaint();
}
```

`menu_isScreenPage()` (`menu.c:1815`) is exactly "VOICE1..7 or
EFFECT_PAGE". `va_underlineSuppressed` is always 0 on the Effect page, so
clearing it there is a no-op.

---

### C10 — `Core/Menu/menu.c` after L2549 — ADD `menu_effectCellAutomated()`

After line 2549 (the closing `}` of `va_applyVoiceMarkers()`) and the blank
line 2550, and before the `menu_applyEffectMarkers()` comment at 2551, add:

```c
/*
 * Report whether one Effect cell's name takes the automation underline (S074).
 *
 * What: nonzero when any stored automation addresses the cell's parameter:
 *   - Pattern automation: once the shared search is complete, a PARAM cell
 *     has its local bit set in va_searchTargetMask[], or the `mrp` cell has
 *     VA_SEARCH_SCENE_EFFECT_MORPH_BIT (`fxm`) set. The search covers every
 *     step of every track of the viewed Pattern;
 *   - FX-sequence locks: the cell's lane is locked on any of the 16 steps
 *     (menuEffects_cellSeqLocked()). This is read live from the active
 *     Scene's record, so it needs no search and no restart after lock edits.
 * `typ`, `run`, `len`, and `scl` are never automated and return zero.
 * Why: the S074 rule (user, 2026-09-29) - if there is any automation on a
 * parameter, in the FX sequence or the Scene's Pattern, its name is
 * underlined in both views, whether or not that automation would play with
 * the current settings (FX length, run mode, track length, mute, trigger,
 * probability, or the current type's AUTOMATABLE flags). The underline
 * reports stored data, not audible effect.
 * Input: one resolved menu cell. Output: 0/1. Caller:
 * menu_applyEffectMarkers(). Affiliates: va_scanService(), va_searchTestBit(),
 * menuEffects_cellSeqLocked().
 */
static uint8_t menu_effectCellAutomated(const menu_cell_t *cell)
{
    if (!cell || cell->kind != MENU_CELL_EFFECT)
        return 0u;
    if (va_searchComplete) {
        if (cell->fx.kind == MENU_FX_CELL_MORPH_AMOUNT &&
            (va_searchSceneMask & VA_SEARCH_SCENE_EFFECT_MORPH_BIT) != 0u)
            return 1u;
        if (cell->fx.kind == MENU_FX_CELL_PARAM &&
            va_searchTestBit(cell->fx.index))
            return 1u;
    }
    return menuEffects_cellSeqLocked(&cell->fx);
}

```

`cell->fx.index` is the Effect descriptor index, which is the block-7 local
(`effectTarget_id(local) = 448 + local`). The search never sets bit 63
(C7), so the only way to reach a local above 62 is a descriptor row that no
Pattern entry can address.

---

### C11 — `Core/Menu/menu.c` L2551–2613 — MODIFY (replace) `menu_applyEffectMarkers()`

Replace lines 2551–2613 (comment and function) with the code below.
Differences from HEAD:

- The held-value formatting and value marker are unchanged, but a locked
  cell now `continue`s *after* drawing its value marker.
- An unlocked or unheld cell falls through to a new name-marker step.
- `locked` is initialised to 0.

```c
/*
 * Apply Effect-page automation markers after the ordinary Effect frame is
 * formed (S072 held-value marker; S074 name marker).
 *
 * What: chooses at most one underline per visible cell, in this order:
 *   1. Held-step value (row 1). While SEQ steps are held, a sequenceable
 *      cell (a PARAM row with a lane, or `mrp`) shows the first held step's
 *      lane value. If that lane is locked on that step, the value's
 *      rightmost glyph is underlined and the cell takes no other marker.
 *   2. Parameter name (row 0). Otherwise, when menu_effectCellAutomated()
 *      reports any Pattern automation (any step of any track of the viewed
 *      Pattern) or any FX-sequence lock (any of the 16 steps, whether or not
 *      it plays), the first non-space character of
 *      the name is underlined: the 3-character short name at columns
 *      4*i..4*i+2 in the compact view, or the 8-character long name at
 *      columns 8..15 in the full (clicked-in) view.
 * Why: the VOICE pages follow this convention (va_applyVoiceMarkers()); until
 * S074 the Effect page drew only step 1, so automated parameter names were
 * never underlined.
 * Inputs: menuIndex, editModeActive, the resolved Effect cells, the SEQ hold
 * (menuEffects_holdDisplay()), the shared search result, and the active
 * Scene's FX sequence. Outputs: held values written into editDisplayBuffer
 * row 1 and one CGRAM marker transaction (marker slots 0..3 = CGRAM 2..5,
 * one per visible cell; slot 0 only in the full view). No retained state
 * changes.
 * Callers: menu_repaintGeneric() - after menuEffects_paintEditView() for the
 * manager full views (typ/run/len/scl/mrp), and at its common tail for PARAM
 * full views and the compact view. Affiliates: menu_effectCellAutomated(),
 * va_formatValue3(), menuEffects_formatValue3(), va_queueMarkerTransaction(),
 * lcd_underlineGlyph().
 */
static void menu_applyEffectMarkers(void)
{
    uint8_t glyph_probe[8];
    uint8_t desired_base[4] = { 0u, 0u, 0u, 0u };
    uint8_t marker_row[4] = { 0u, 0u, 0u, 0u };
    uint8_t marker_col[4] = { 0u, 0u, 0u, 0u };
    uint8_t desired_valid = 0u;
    uint8_t activePage;
    uint8_t activeParameter;
    uint8_t first;
    uint8_t count;
    uint8_t i;

    if (menu_activePage != EFFECT_PAGE)
        return;
    activePage = (uint8_t)((menuIndex & MASK_PAGE) >> PAGE_SHIFT);
    activeParameter = (uint8_t)(menuIndex & MASK_PARAMETER);
    first = editModeActive ? activeParameter : 0u;
    count = editModeActive ? 1u : 4u;
    for (i = 0u; i < count; i++) {
        uint8_t column = (uint8_t)(first + i);
        menu_cell_t cell = menu_resolveCell(activePage, column);
        uint8_t value;
        uint8_t locked = 0u;
        uint8_t slot = editModeActive ? 0u : i;
        /* Name field: long name (full view) or 3-character short name. */
        uint8_t name_start = editModeActive ? 8u : (uint8_t)(4u * i);
        uint8_t name_width = editModeActive ? 8u : 3u;
        uint8_t left;

        if (cell.kind != MENU_CELL_EFFECT)
            continue;
        if (menuEffects_holdDisplay(&cell.fx, &value, &locked)) {
            char *field = editModeActive ? &editDisplayBuffer[1][13]
                                         : &editDisplayBuffer[1][4u * i];
            int8_t right;

            if (cell.fx.kind == MENU_FX_CELL_PARAM)
                va_formatValue3(&cell, value, field);
            else if (cell.fx.kind == MENU_FX_CELL_MORPH_AMOUNT)
                /* Held `mrp` is a plain number, so show its lock value directly. */
                numtostrpu(field, value, ' ');
            else
                (void)menuEffects_formatValue3(&cell.fx, field);
            if (locked) {
                for (right = 2; right >= 0 && field[right] == ' '; right--)
                    ;
                if (right >= 0 && lcd_underlineGlyph((uint8_t)field[right],
                                                     glyph_probe)) {
                    desired_base[slot] = (uint8_t)field[right];
                    marker_row[slot] = 1u;
                    marker_col[slot] = (uint8_t)(
                        (editModeActive ? 13u : 4u * i) + (uint8_t)right);
                    desired_valid |= (uint8_t)(1u << slot);
                }
                /* The held step's locked value owns this cell (VOICE rule). */
                continue;
            }
        }
        /* Unheld, or held but unlocked: fall back to the name marker. */
        if (!menu_effectCellAutomated(&cell))
            continue;
        for (left = 0u; left < name_width &&
             editDisplayBuffer[0][name_start + left] == ' '; left++)
            ;
        if (left < name_width && lcd_underlineGlyph(
                (uint8_t)editDisplayBuffer[0][name_start + left],
                glyph_probe)) {
            desired_base[slot] =
                (uint8_t)editDisplayBuffer[0][name_start + left];
            marker_row[slot] = 0u;
            marker_col[slot] = (uint8_t)(name_start + left);
            desired_valid |= (uint8_t)(1u << slot);
        }
    }
    va_queueMarkerTransaction(desired_base, desired_valid,
                              marker_row, marker_col);
}
```

Where the name is on screen when this runs:

- **Compact view:** `menu_repaintGeneric()` writes the short names
  (`menuEffects_shortName()`, `menu.c:9794`) and uppercases the active one
  (`menu.c:9808`) before it calls this function (`menu.c:9833`).
- **PARAM full view:** the long name is at `[0][8]` (`menu.c:9687`).
- **Manager full view:** `menuEffects_paintEditView()` writes `Morph` at
  `[0][8]` for `mrp` (`menuEffects.c:556`) before the call at `menu.c:9611`.

---

### C12 — `Core/Menu/menu.c` L11191–11223 — MODIFY `menu_serviceRuntimeWidgets()`

Three steps:

- **C12a — REMOVE L11201–11208** (the retry comment and `if` block inside
  the VOICE branch).
- **C12b — ADD after L11222** (`menu_repaintAll();` in the Effect branch):
  the Effect scan call.
- **C12c — ADD after L11223** (the Effect branch's closing `}`): the shared
  retry.

Also replace the last sentence of the comment at L11195.

The result for lines 11191–11223 (the retry now follows the Effect branch):

```c
    /*
     * VOICE overlay services run every foreground pass, independently of the
     * slower CPU-use widget cadence. Held-state polling is first so scan/value
     * resolution sees the latest raw SEQ mask; all LCD work remains foreground
     * only. Pattern-wide scans are four step reads per pass by configuration
     * (VOICE and Effect pages).
     */
    if (menu_isVoicePage(menu_activePage)) {
        va_updateHeldState();
        va_scanService();
        va_underlineService();
    }

    if (menu_activePage == EFFECT_PAGE) {
        uint8_t fx_actions = menuEffects_service();

        /* Follow Scene switches and external type changes on the FX page. */
        if (fx_actions & MENU_FX_ACT_EXIT_EDIT)
            editModeActive = 0u;
        if (fx_actions & MENU_FX_ACT_REPAIR) {
            menu_resetActiveParameter();
            menu_endlessPotMappingChanged();
        }
        if (fx_actions & MENU_FX_ACT_REPAINT)
            menu_repaintAll();
        /*
         * Effect-page automation-presence search (S074).
         *
         * What: advances the search shared with the VOICE pages, here over
         * all seven tracks of the viewed Pattern, four step reads per pass.
         * Why: the Effect page underlines parameter names automated in the
         * Pattern; the result feeds menu_effectCellAutomated() through
         * menu_applyEffectMarkers(). It runs after menuEffects_service() so
         * that pass's Scene/type handling comes first. Output: one
         * menu_repaint() when the search completes. Affiliates:
         * va_scanService(), va_searchRestart().
         */
        va_scanService();
    }

    /*
     * Retry a deferred marker transaction once the LCD queue has drained.
     *
     * What: repaints once when va_queueMarkerTransaction() had to defer its
     * CGRAM work (VA_MARKER_RETRY_BIT) and the queue has room again. Why:
     * the retry bit survives sendDisplayBuffer() clearing
     * menu_lcdRefreshPending, so underlines recover after a burst of rapid
     * encoder/pot events. The VOICE and Effect pages share the transaction,
     * so both need the retry; before S074 only VOICE had it, and a deferred
     * Effect marker stayed missing until an unrelated repaint. Inputs:
     * menu_activePage, va_cgramValid, lcd_queueFree(). Output: at most one
     * menu_repaint() per pass. Affiliates: va_queueMarkerTransaction(),
     * va_applyVoiceMarkers(), menu_applyEffectMarkers().
     */
    if (menu_isScreenPage(menu_activePage) &&
        (va_cgramValid & VA_MARKER_RETRY_BIT) &&
        lcd_queueFree() >= 72u) {
        menu_repaint();
    }
```

For VOICE this is behaviour-identical: on a VOICE page the Effect branch does
not run, so the retry still follows `va_underlineService()` directly. The
`72u` threshold is the existing value, moved unchanged.

---

### C13 — `Core/Menu/menu.c` L12449–12450 — ADD (Effect-page entry restart)

In `menu_switchPage()`, `case EFFECT_PAGE:`, after line 12449
(`menu_activePage = EFFECT_PAGE;`) and before line 12450 (`editModeActive =
0u;`), add:

```c
        /*
         * Effect-page automation-presence search (S074).
         *
         * What: a fresh entry restarts the search shared with the VOICE pages
         * in Effect mode (seven-track cursor from track 0, empty masks). Why:
         * the shared va_search* bytes may still hold a completed VOICE
         * result, which would mark the wrong Effect names. The restart runs
         * after menu_activePage is set because va_searchRestart() selects the
         * scan geometry from the page. A repeated SHIFT+PERF (screen toggle,
         * old_page == EFFECT_PAGE) keeps the running search. Input: old_page
         * (captured at the top of menu_switchPage()). Output: a restarted
         * search that va_scanService() completes and repaints. Affiliates:
         * va_searchRestart(), menuEffects_enter(), menu_serviceRuntimeWidgets().
         */
        if (old_page != EFFECT_PAGE)
            va_searchRestart();
```

`old_page` is declared at `menu.c:12285` and is already used inside this
`switch` (`menu.c:12480`). `menu.c:12449` is the only assignment of
`EFFECT_PAGE` to `menu_activePage`, so this covers every entry, including a
deferred page switch replayed by `menu_processPendingPageSwitch()`.

---

### C14 — `Core/Menu/menu.c` L13114–13125 — MODIFY `menu_setActiveVoice()`

**Current (L13114–13125):**

```c
/* Track changes restart the custom STEP automation cursor at page zero. */
void menu_setActiveVoice(uint8_t v)
{
    if (menu_activeVoice != v) {
        menu_stepAutomationReset();
        va_resetOverlay();
        menu_activeVoice = v;
        va_searchRestart();
        return;
    }
    menu_activeVoice = v;
}
```

**New:**

```c
/*
 * Set the active track (voice) shown by Menu.
 *
 * What: a change restarts the custom STEP automation cursor at page zero,
 * releases the VOICE held-step overlay, and restarts the VOICE
 * automation-presence search for the new track. Why: those views are
 * track-scoped. The Effect page (reached here by SHIFT+TRACK in FX mode) keeps
 * its search, which already covers all seven tracks; a restart would only
 * blank its Pattern underlines until a redundant 224-pass rescan completed
 * (S074). The next VOICE entry restarts the search regardless. Input: track
 * 0..6. Output: menu_activeVoice and the dependent transient state.
 * Callers: buttonHandler voice/track presses, menu_switchPage() voice entry.
 * Affiliates: menu_stepAutomationReset(), va_resetOverlay(),
 * va_searchRestart().
 */
void menu_setActiveVoice(uint8_t v)
{
    if (menu_activeVoice != v) {
        menu_stepAutomationReset();
        va_resetOverlay();
        menu_activeVoice = v;
        if (menu_activePage != EFFECT_PAGE)
            va_searchRestart();
        return;
    }
    menu_activeVoice = v;
}
```

The Effect-page caller is `buttonHandler.c:1185`.

---

### C15 — `Core/Menu/menu.c` L13215–13243 — MODIFY `menu_setShownPattern()`

Two edits:

- **C15a — MODIFY the last sentence of the comment**, L13227
  (` * valid, otherwise it falls back to Scene 0. A VOICE context change
  also`) and L13228 (` * invalidates the held-step/search view before
  repainting it.`). Replace both lines with:

```c
     * valid, otherwise it falls back to Scene 0. A VOICE context change also
     * invalidates the held-step/search view before repainting it; on the
     * Effect page (S074) the automation-presence search restarts and the
     * page repaints.
```

- **C15b — ADD an `else if` branch** after line 13241 (the closing `}` of
  `if (menu_isVoicePage(menu_activePage)) { … }`), so the block reads:

```c
        menu_shownPattern = next;
        if (menu_isVoicePage(menu_activePage)) {
            va_resetOverlay();
            va_searchRestart();
            led_updatePatternTrack(menu_activeVoice, menu_shownPattern,
                                   buttonHandler_selectedStep);
            menu_repaint();
        } else if (menu_activePage == EFFECT_PAGE) {
            /*
             * Effect page (S074): the Pattern-wide presence result belongs to
             * the old Pattern. Restart at once so that no repaint before the
             * next service pass shows its underlines, then repaint (FX-lock
             * underlines are read live and stay correct). The Pattern check
             * in va_scanService() is the backstop, and its completion
             * repaints again. No Pattern LED update: the SEQ row belongs to
             * the FX sequencer on this page.
             */
            va_searchRestart();
            menu_repaint();
        }
```

The Effect-page callers are follow-mode `ledHandler.c:1433` and the realign
sites in `filesystem.c`. A PERF Scene switch (`menu.c:6471`) runs on the PERF
page.

---

### C16 — `Core/Menu/menuEffects.h` after L108 — ADD

After line 108 (`void menuEffects_renderSeqLeds(void);`) and before the blank
line and `#endif` (L109–110), add:

```c

/*
 * FX-sequence lock presence for the Effect-page name underline (S074).
 *
 * What: nonzero when the cell's FX-sequence lane is locked on any of the 16
 * retained steps, whether or not that step plays (FX length, run mode, `sel`
 * step and transport are ignored). Lane 0 is Effect Morph (`mrp`); PARAM
 * cells use the active type's registry lane map; cells without a lane
 * (typ/run/len/scl and lane-less rows such as `out`) return zero.
 * Why: the S074 rule (user, 2026-09-29) - any stored automation on a
 * parameter underlines its name. It reads the active Scene's retained record
 * directly (16 mask reads), so it is always current and keeps no cache.
 * Input: a resolved Effect cell. Output: 0/1. Foreground-only and read-only;
 * it allocates nothing. Caller: menu_effectCellAutomated() in menu.c.
 * Affiliates: menuEffects_cellLane(), effects_laneOfParam(), SceneData's
 * effect_record_t.
 */
uint8_t menuEffects_cellSeqLocked(const menuEffects_cell_t *cell);
```

---

### C17 — `Core/Menu/menuEffects.c` after L815 — ADD `menuEffects_cellSeqLocked()`

After line 815 (the closing `}` of `menuEffects_holdDisplay()`) and before
the blank line and the `menuEffects_renderSeqLeds()` comment, add:

```c

/*
 * Report an FX-sequence lock on the cell's lane on any step (S074).
 *
 * What: scans all 16 steps of the active Scene's record for the cell's lane
 * bit. The retained length is deliberately not consulted: a lock on a step
 * beyond `len` is still stored automation and still underlines the name
 * (user rule, 2026-09-29). The SEQ LEDs, which show what plays, keep their
 * own within-length rule in menuEffects_renderSeqLeds().
 * Why: an FX lock is one of the two sources of the Effect-page name
 * underline. Input: a resolved Effect cell. Output: 0/1; zero for cells
 * without a lane and when no record exists. Caller: menu_effectCellAutomated()
 * (menu.c). Affiliates: menuEffects_cellLane(), effects_getLaneLock().
 */
uint8_t menuEffects_cellSeqLocked(const menuEffects_cell_t *cell)
{
    const effect_record_t *record = menuEffects_record();
    uint16_t bit;
    uint8_t lane;
    uint8_t step;

    if (!record || !menuEffects_cellLane(cell, &lane))
        return 0u;
    bit = (uint16_t)(1u << lane);
    for (step = 0u; step < EFFECT_SEQ_STEP_COUNT; step++) {
        if ((record->steps[step].lock_mask & bit) != 0u)
            return 1u;
    }
    return 0u;
}
```

`menuEffects_cellLane()` already returns zero for a NULL cell, a missing
record and manager cells, and returns lane 0 for `mrp` on every type
(including `off`).

---

## 7. Build and verification gates

1. `make all && make img`. Run one build at a time (a concurrent `lto1` can
   leave an empty ELF). Expect no new warnings in `menu.c` or
   `menuEffects.c`.
2. `python3 tools/link_budget.py arm-none-eabi-nm build/lxr02.elf`.
   Compare with the S073 close:
   - `data=416` and `bss=426,336`: **unchanged**.
   - ITCM 4,168 B, DTCM statics 4,448 B and the FX arena 126,624 B:
     **unchanged**.
   - `text` grows by an estimated few hundred bytes (266,560 B were free).
   - Any `bss` or `data` change means a new allocation slipped in: stop and
     report it.
3. The `_Static_assert` at `menu.c:1295` (VOICE state = 45 B) still compiles
   without change.
4. Read-through checks:
   - `grep -n "va_searchRestart();" Core/Menu/menu.c` shows the new Effect
     entry (C13) and Effect Pattern-change (C15) calls.
   - `grep -n "EFFECT_TARGET_PATTERN_LOCAL_LIMIT" Core/Menu/menu.c` shows
     the local-63 guard (C7).
   - `grep -n "VA_MARKER_RETRY_BIT" Core/Menu/menu.c` shows exactly one
     retry test in `menu_serviceRuntimeWidgets()` (C12).

---

## 8. Hardware checks (yours)

From S074 startup §2.6, plus the new edges:

1. Pattern automation of `frq` on one track: `frq` is underlined in the
   compact view and `Frequncy` in the full view. Deleting the automation on
   the STEP page and returning clears the underline.
2. Automation of `vol`, `pan` and `out` (common rows), and of `fxm` (`mrp`
   in both views; the full view underlines the `M` of `Morph`).
3. Automation on a track other than the active one (U1), including track 7.
4. An FX lock on `res`: underlined immediately. A lock only on a step beyond
   `len` is also underlined (U3), and shortening `len` below the only locked
   step keeps the underline. That step's SEQ LED stays dark (unchanged).
   The run mode and `sel` step make no difference.
5. Both sources on the same parameter: one underline.
6. SEQ hold: a locked lane still shows the held value underlined (row 1). An
   unlocked but automated lane shows the held value **and** the name
   underline. Releasing the hold restores the name underlines.
7. SHIFT (Morph view): the underlines stay.
8. Scene switch, Pattern change and `typ` change: no stale underlines. A
   `typ` change clears every FX lock, so lock underlines go. Pattern entries
   stay, so the new type's rows with those local indices are underlined.
9. Pattern automation that does not play is still underlined: on a step
   beyond the track's length, on a muted track, on a step with its trigger
   off, and on a step with a low probability setting.
10. SHIFT+TRACK on the Effect page: underlines do not flicker (C14).
11. SHIFT+COPY Pattern clear or track clear while on the Effect page:
    underlines that only the cleared automation caused disappear.
12. A STEP-page automation entry left at `off` underlines nothing on the
    Effect page (local-63 guard).
13. Fast encoder or pot spinning on the Effect page: underlines recover when
    you stop (the shared retry).
14. VOICE pages: underlines, held values and the retry behave as before
    (regression).

---

## 9. Not changed / known limitations

These are outside this fix. The working rule is to log unrelated findings in
`SCOPING_TARGETS.md` rather than fix them.

1. **Live erase while recording** (SHIFT+COPY during record) removes step
   automation through `patSvc_enqueueErase()` (`sequencer.c:902`) without
   restarting the search. Removed targets stay underlined until the next
   restart. The VOICE pages have the same gap.
2. **A deferred Pattern or track clear:** when PatternStackService is busy,
   `patSvc_clearPattern()` and `patSvc_clearTrack()` queue the pool
   mutation, while C9 restarts the search at once. If the search reaches a
   step before the queued clear does, it can keep a stale bit. The VOICE
   pages share this.
3. **`menu_repaintAll()` and a moving marker.** `MENU_FX_ACT_REPAINT`
   repaints with `menu_repaintAll()`, which resets the LCD shadow. A marker
   slot redefined in that frame can briefly show its new glyph at the old
   position until the frame write reaches it (a few ms). The VOICE code uses
   `menu_repaint()` for this reason (`menu.c:2057`). This is S072 behaviour
   and is now visible when a hold is released.
4. **No quiet-period debounce for the Effect held-value marker:** unlike
   VOICE (`va_underlineSuppressed`), it redefines its glyph on each detent.
   This is S072 behaviour and is unchanged.
5. **Search latency is not measured.** It is 224 foreground passes. If Pattern
   underlines appear too slowly on hardware, raise it with the user; there is
   no new knob.

---

## 10. Documentation follow-ups at session close

- `EFFECTS_BUS_REFERENCE.md` §8.3: add the name-underline rule (§2 here),
  its two sources, U3, and the precedence.
- `MODULE_INTERCHANGE_SPEC.md` (the menuEffects row near line 564): add
  `menuEffects_cellSeqLocked()` (caller: Menu).
- `SCOPING_TARGETS.md` "Session 073 carry-forward": mark item 1 closed once
  the hardware checks pass, and log §9 items 1–2 if you want them tracked.
- `074_SESSION_HANDOFF_LOG.md`: the changes, the U-decisions and the link
  numbers.
