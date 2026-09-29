# S074 — Effect-page automation underline: implementation schedule

**Status:** implemented; hardware test PASS (user, 2026-09-29). Assessment
and build record in §12.

**Base:** branch `dev-ph5-effects`, HEAD `223abdc` before this implementation.
The working tree was clean at the start of the implementation. The line
numbers below are the original schedule's reference numbers, not current
post-edit locations. The implementation was applied in the documented
highest-line-first order.

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

### Implementation log (2026-09-29)

- Confirmed the current source matches the schedule's S072 Effect-page
  integration points. The referenced `S072_ST5_IMPLEMENTATION.md` tab is not
  present in this checkout; the durable S072/S073 references and current code
  were used instead.
- Applied C1–C17: the shared VOICE search now supports the Effect page's
  seven-track Pattern scan; `fxm` and Effect locals 0..62 are recorded; local
  63/off is rejected; FX locks are read across all 16 retained steps; compact
  and full Effect names use the shared CGRAM marker transaction; and deferred
  marker retry covers both screen-page families.
- Preserved the existing 45-byte `_Static_assert`; no new static or stack
  state was introduced. The buffer-using Effect goal in
  `S074_EFFECT_BUGS_BUFFER_USE.md` is intentionally not part of this change.
- Verification: `git diff --check` passed; `make all` and `make img` passed;
  the existing 45-byte `_Static_assert` compiled; and the structural checks
  confirmed the local-63 guard, Effect entry/Pattern-change restarts, and one
  shared deferred-marker retry test.
- Measured output: `text=487,384`, `data=416`, `bss=426,336`; flash payload
  `487,800 / 753,664 B` with `265,864 B` headroom; ITCM `4,168 B`, DTCM
  statics `4,448 B`, and FX arena `126,624 B`. The generated image is
  `build/LXRV2_lxr02.img` (`487,800 B`).
- Hardware behavior checks in §8 remain for the user to run; no device was
  flashed from this session.

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

---

## 11. Follow-up fix: underline ordering on SEQ hold and release

**Status:** schedule only. No code has been changed.

**Base:** the working tree with C1–C17 applied (uncommitted on top of HEAD
`223abdc`). Every line number in this section is a **current working-tree**
line number. Each file gets exactly one change here, so the numbers stay
valid in any order. Apply F1 first, because F2 and F3 use its define.

### 11.1 Symptom (hardware, 2026-09-29)

The underlines now appear on the right parameters, but the change is drawn
in the wrong order when a SEQ hold starts or ends:

- **Hold:** the underlined held-value character flashes briefly in the
  **name row** (row 0) before it appears in the value row (row 1).
- **Release:** the underlined name character flashes briefly in the **value
  row** before it appears in the name row.

This is the defect the VOICE overlay had in S066
(`066_SESSION_HANDOFF_LOG.md`, "Fix 5 — Overlay Exit CGRAM Ordering Glitch").

### 11.2 Root cause

A visible cell keeps the same CGRAM marker slot (slot `i` for compact cell
`i`, slot 0 in the full view) whether its underline is on the name or on the
held value. A hold start or release therefore **moves** a slot from one LCD
cell to another and **redefines** its glyph.

`va_queueMarkerTransaction()` (`menu.c:2313`) orders that move correctly,
but only if `currentDisplayBuffer` matches the LCD:

1. **Stale restore:** every cell whose shadow still holds a changed slot's
   code (2..5) is rewritten with its plain ROM character.
2. **Define:** the slot is redefined with the new underlined glyph. No cell
   references it at this point, so nothing visible changes.
3. **Frame diff:** changed cells are written, row 0 then row 1, including the
   slot code at its new cell.

On the Effect page every hold transition is redrawn with `menu_repaintAll()`:

- `menuEffects_service()` sets `MENU_FX_ACT_REPAINT` for the hold start (the
  held mask goes from 0 to nonzero on the first pass after
  `menuEffects_seqHoldExpired()`), for a held-mask change (`menuEffects.c:667`)
  and for the release (`menuEffects.c:662`).
- `menu_serviceRuntimeWidgets()` turns that bit into `menu_repaintAll()`
  (`menu.c:11408–11409`).

`menu_repaintAll()` (`menu.c:8240`) fills `currentDisplayBuffer` with `0x7F`.
Step 1 then finds nothing to restore, and step 2 redefines the slot while
the old cell on the LCD still references it. The new glyph shows in the old
row until step 3's full-frame write reaches that cell. That is exactly the
reported flash, in both directions.

The VOICE overlay already avoids this. Its hold start
(`menu_voiceAutoOverlayHoldExpired()`, `menu.c:2202`), held-mask change and
release (`va_updateHeldState()`, comment at `menu.c:2166`) all use
`menu_repaint()`.

Neither the SEQ press nor the release path in `buttonHandler.c` (FX branches
at lines 758–763 and 802–808) repaints the LCD. The service pass is the only
repaint on these transitions, so fixing it covers every hold transition.

### 11.3 Fix

Mirror the VOICE rule: the Effect hold transitions redraw with
`menu_repaint()`, which keeps the LCD shadow.

- **F1:** a new action bit, `MENU_FX_ACT_HOLD_REPAINT` (`0x08`), with the
  action-bit contract documented.
- **F2:** `menuEffects_service()` reports hold start, held-mask change and
  release with the new bit instead of `MENU_FX_ACT_REPAINT`.
- **F3:** `menu_serviceRuntimeWidgets()` answers the new bit with
  `menu_repaint()`.
  - A Scene or type change alone keeps its `menu_repaintAll()`.
  - If one pass reports both bits, `menu_repaint()` is used.
    `menu_repaintGeneric()` rebuilds both rows of the Effect frame in every
    view: the compact view clears both rows (`menu.c` compact branch), the
    PARAM full view clears both rows, and `menuEffects_paintEditView()`
    clears both rows for manager cells. The only thing `menu_repaintAll()`
    adds is the forced resend, and that is what breaks the ordering.

The resulting LCD queue order (compact view; cell 1 `frq` with a name
underline; the first held step locks `frq` at 90; normal value 64):

| Step | On hold (F applied) | On release (F applied) |
|---|---|---|
| 1. Stale restore | `[0][4]` ← plain `f` (top row un-underlined) | `[1][6]` ← plain `0` (bottom row un-underlined) |
| 2. Define CGRAM slot 1 | underlined `0` (no cell references it) | underlined `f` (no cell references it) |
| 3. Frame diff | `[1][5]` ← `9`, `[1][6]` ← slot 1 (underlined `0` appears on the bottom row) | `[0][4]` ← slot 1 (underlined `f` appears on the top row), then `[1][5]` ← `6`, `[1][6]` ← `4` |

This is the order you asked for:

- **On hold:** restore the plain character on the top row, change the
  glyph, then write the underline and update the bottom row.
- **On release:** the same steps in reverse.

The queue-full fallback also stays clean. When the transaction does not fit,
it reverts the marker cells to ROM characters and sets the retry bit, so
`sendDisplayBuffer()` replaces the old slot reference with its plain
character before the retry defines the new glyph.

**Cost:** 0 B of RAM, no new state, a few bytes of flash. The LCD queue cost
of a hold transition falls: a diff-based write (typically 2 + 10 per changed
slot + 2 per changed cell) replaces the forced 64-op frame. The forced frame
also made the transaction fall back to the retry more often.

### 11.4 Change schedule

| ID | File | Lines (working tree) | Action | What |
|---|---|---|---|---|
| F1 | `Core/Menu/menuEffects.h` | 53–56 | MODIFY comment, ADD define | `MENU_FX_ACT_HOLD_REPAINT` and the action-bit contract |
| F2 | `Core/Menu/menuEffects.c` | 652–668 | MODIFY | Hold transitions report `MENU_FX_ACT_HOLD_REPAINT` |
| F3 | `Core/Menu/menu.c` | 11408–11409 | MODIFY | `menu_repaint()` for hold transitions |

---

#### F1 — `Core/Menu/menuEffects.h` L53–56 — MODIFY comment, ADD define

**Current (L53–56):**

```c
/* menuEffects_service() action bits consumed by menu.c. */
#define MENU_FX_ACT_REPAINT    0x01u
#define MENU_FX_ACT_REPAIR     0x02u
#define MENU_FX_ACT_EXIT_EDIT  0x04u
```

**New:** the three existing `#define` lines are unchanged; the comment is
replaced and one define is added.

```c
/*
 * menuEffects_service() action bits, consumed by menu_serviceRuntimeWidgets().
 *
 * MENU_FX_ACT_REPAINT: the active Scene or its Effect type changed. The page
 *   is redrawn with menu_repaintAll() (forced full resend) unless the same
 *   pass also reports MENU_FX_ACT_HOLD_REPAINT.
 * MENU_FX_ACT_REPAIR: the layout may have changed; menu.c repairs the cursor
 *   (menu_resetActiveParameter()) and the endless-pot mapping.
 * MENU_FX_ACT_EXIT_EDIT: an open `typ` transaction was abandoned; menu.c
 *   leaves the full view.
 * MENU_FX_ACT_HOLD_REPAINT (S074): the SEQ lock-edit hold started, changed
 *   its held steps, or ended. A visible cell keeps its CGRAM marker slot while
 *   its underline moves between the name (row 0) and the held value (row 1),
 *   so menu.c redraws with menu_repaint(). That keeps currentDisplayBuffer
 *   equal to the LCD, and va_queueMarkerTransaction() can restore the old
 *   cell to its plain character before it redefines the slot and writes the
 *   new cell. menu_repaintAll() would erase that knowledge and flash the new
 *   glyph in the old row (the S066 Fix 5 defect). It takes precedence over
 *   MENU_FX_ACT_REPAINT in the same pass.
 * Producer: menuEffects_service(). Consumer: the EFFECT_PAGE branch of
 * menu_serviceRuntimeWidgets(). Affiliates: menu_applyEffectMarkers(),
 * va_queueMarkerTransaction().
 */
#define MENU_FX_ACT_REPAINT    0x01u
#define MENU_FX_ACT_REPAIR     0x02u
#define MENU_FX_ACT_EXIT_EDIT  0x04u
#define MENU_FX_ACT_HOLD_REPAINT 0x08u
```

---

#### F2 — `Core/Menu/menuEffects.c` L652–668 — MODIFY hold tracking in `menuEffects_service()`

**Current (L652–668):**

```c
    /*
     * Lock-edit hold follows the physical SEQ mask. Newly seen steps flash;
     * releasing every SEQ button ends the lock editor and requests one repaint.
     */
    if (menuEffects_holdActive) {
        uint16_t mask = buttonHandler_seqHeldMask();

        if (mask == 0u) {
            menuEffects_holdActive = 0u;
            menuEffects_holdMask = 0u;
            actions |= MENU_FX_ACT_REPAINT;
        } else if (mask != menuEffects_holdMask) {
            led_flashGroup(LED_FLASH_GROUP_SEQ,
                           (uint16_t)(mask & (uint16_t)~menuEffects_holdMask));
            menuEffects_holdMask = mask;
            actions |= MENU_FX_ACT_REPAINT;
        }
    }
```

**New:**

```c
    /*
     * Lock-edit hold follows the physical SEQ mask.
     *
     * What: the first pass after menuEffects_seqHoldExpired() (held mask
     * 0 -> nonzero) starts the held view; newly seen steps flash; releasing
     * every SEQ button ends the lock editor. Each of these transitions
     * requests one MENU_FX_ACT_HOLD_REPAINT.
     * Why not MENU_FX_ACT_REPAINT (S074): the redraw can move a cell's
     * underline between its name (row 0) and its held value (row 1) on the same
     * CGRAM slot. Only menu_repaint() keeps the LCD shadow that lets
     * va_queueMarkerTransaction() order the move: restore the old cell to its
     * plain character, redefine the slot, then write the new cell. The VOICE
     * overlay has used menu_repaint() for the same transitions since S066
     * Fix 5.
     * Inputs: buttonHandler_seqHeldMask(), menuEffects_holdActive/holdMask.
     * Outputs: updated hold state, SEQ LED flashes, and the action bit.
     * Consumer: menu_serviceRuntimeWidgets().
     */
    if (menuEffects_holdActive) {
        uint16_t mask = buttonHandler_seqHeldMask();

        if (mask == 0u) {
            menuEffects_holdActive = 0u;
            menuEffects_holdMask = 0u;
            actions |= MENU_FX_ACT_HOLD_REPAINT;
        } else if (mask != menuEffects_holdMask) {
            led_flashGroup(LED_FLASH_GROUP_SEQ,
                           (uint16_t)(mask & (uint16_t)~menuEffects_holdMask));
            menuEffects_holdMask = mask;
            actions |= MENU_FX_ACT_HOLD_REPAINT;
        }
    }
```

The code is unchanged apart from the two `actions |=` lines (L662 and L667).
The Scene/type branch at L643–651 keeps `MENU_FX_ACT_REPAINT`. The SEQ LED
block at L670–682 tests only `MENU_FX_ACT_REPAIR`, so it is unaffected.

---

#### F3 — `Core/Menu/menu.c` L11408–11409 — MODIFY the Effect-branch repaint in `menu_serviceRuntimeWidgets()`

**Current (L11408–11409):**

```c
        if (fx_actions & MENU_FX_ACT_REPAINT)
            menu_repaintAll();
```

**New:**

```c
        /*
         * Redraw after this pass's Effect state changes (S074 ordering fix).
         *
         * What: a SEQ hold transition (MENU_FX_ACT_HOLD_REPAINT) redraws with
         * menu_repaint(); a Scene or type change alone (MENU_FX_ACT_REPAINT)
         * keeps its forced full menu_repaintAll().
         * Why: menu_repaintAll() overwrites currentDisplayBuffer with 0x7F, so
         * va_queueMarkerTransaction() cannot find the LCD cell that still
         * shows a CGRAM marker slot. It then redefines the slot while that
         * cell still references it: on a hold, the new underlined value glyph
         * flashes in the name row; on release, the underlined name glyph
         * flashes in the value row, until the frame write reaches them.
         * menu_repaint() keeps the shadow equal to the LCD, so the transaction
         * restores the old cell to its plain character first, then redefines
         * the slot, then writes the new cell and the rest of the frame (row 0,
         * then row 1). A pass that reports both bits uses menu_repaint():
         * menu_repaintGeneric() rebuilds both rows of the Effect frame in
         * every view, so menu_repaintAll() would add only the forced resend
         * that breaks the ordering.
         * Inputs: fx_actions from menuEffects_service(). Output: at most one
         * repaint. Affiliates: va_queueMarkerTransaction(),
         * menu_applyEffectMarkers(), and va_updateHeldState() (the VOICE
         * precedent, S066 Fix 5).
         */
        if (fx_actions & MENU_FX_ACT_HOLD_REPAINT)
            menu_repaint();
        else if (fx_actions & MENU_FX_ACT_REPAINT)
            menu_repaintAll();
```

The surrounding lines (the `MENU_FX_ACT_EXIT_EDIT` and `MENU_FX_ACT_REPAIR`
handling before it, and the C12 `va_scanService()` call after it) are
unchanged. Cursor repair still runs before the repaint.

### 11.5 Build and verification gates

1. `make all && make img`, one build at a time. Expect no new warnings.
2. `python3 tools/link_budget.py arm-none-eabi-nm build/lxr02.elf`:
   - `data=416` and `bss=426,336`: unchanged;
   - ITCM, DTCM statics and the FX arena: unchanged;
   - `text` within a few bytes of the C1–C17 build (`487,384`).
3. Read-through checks:
   - `grep -n "MENU_FX_ACT_HOLD_REPAINT" Core/Menu/*.c Core/Menu/*.h`: one
     define (F1), two sets in `menuEffects.c` (F2) and one test in `menu.c`
     (F3).
   - `grep -n "MENU_FX_ACT_REPAINT" Core/Menu/menuEffects.c`: only the
     Scene/type line (L646).

### 11.6 Hardware checks (yours)

1. **Compact view:** a cell whose name is underlined and whose lane is
   locked on a step. Hold that step: the name loses its underline and the
   value appears underlined in the bottom row, with **no** underlined value
   glyph flashing in the name row.
2. **Release:** the value loses its underline and the name is underlined
   again, with **no** underlined name glyph flashing in the value row.
3. **Full view:** click into `frq` (long name) and repeat checks 1–2.
4. **`mrp`** (lane 0): repeat checks 1–2.
5. **Held-mask change:** hold a second step whose lock value differs. The
   value underline changes in place, with no flash elsewhere.
6. **Four cells at once:** all four names underlined, and a held step that
   locks only two of them. Only those two move; the other two keep their
   name underlines and do not flicker.
7. **Held but unlocked:** an automated cell that is not locked on the held
   step keeps its name underline through hold and release.
8. **Rapid hold and release:** the markers always end correct (the retry
   recovers a deferred transaction).
9. **Scene switch and `typ` change:** still redraw the page completely
   (regression).
10. **VOICE pages:** hold and release unchanged (regression).

### 11.7 Not changed

These `menu_repaintAll()` paths can still redefine a marker slot whose
position changes. Each redraws the whole frame, so any transient sits inside
a full redraw. None has been reported, and the VOICE pages share them. Log
them in `SCOPING_TARGETS.md` if they are ever seen.

- An encoder click in or out (`menu.c:11107–11108`, `if (btnClicked)
  menu_repaintAll();`). Slot 0 moves between compact cell 0 and the full
  view's name or value.
- A SELECT press (Effect: `buttonHandler.c:967`; VOICE/STEP:
  `buttonHandler.c:957`) while a marker changes row between screens.
- A Scene or type change while a hold stays on screen, with no hold
  transition in the same pass.

This section supersedes §9 item 3 for the SEQ hold and release transitions.

### 11.8 Follow-up implementation log (2026-09-29)

- Applied F1–F3: added `MENU_FX_ACT_HOLD_REPAINT`, made hold start/mask
  changes/release report it, and gave it precedence over the forced full
  repaint in the Effect-page service.
- Kept Scene/type changes on `MENU_FX_ACT_REPAINT` and `menu_repaintAll()`;
  no new state, RAM allocation, DSP work, or ISR path was added.
- Verification: `make all` and `make img` passed; the F1/F2/F3 code-line
  action-flow checks passed; and `git diff --check` passed. The final link is
  `text=487,544`, `data=416`, `bss=426,336`; flash payload
  `487,960 / 753,664 B` with `265,704 B` headroom; ITCM `4,168 B`, DTCM
  statics `4,448 B`, and FX arena `126,624 B`. The generated image is
  `build/LXRV2_lxr02.img` (`487,960 B`).
- Hardware display checks in §11.6 remain for the user to run; no device was
  flashed from this session.

---

## 12. Implementation assessment and test result (2026-09-29)

### 12.1 Result

**PASS.** The implemented code matches this schedule (C1–C17 and F1–F3),
with one cosmetic deviation (§12.3). The user reports the changes complete
and tested OK on hardware:

- automated Effect parameters show the name underline;
- the SEQ hold and release ordering defect (§11) is fixed.

The §8 and §11.6 checks were not reported one by one.

### 12.2 What was reviewed

`git diff HEAD` (`223abdc`) for `Core/Menu/menu.c`, `menu.h`,
`menuEffects.c`, `menuEffects.h` and `config.h`, compared change by change
with this document.

| Change | Status | Notes |
|---|---|---|
| C1 `config.h` comment | as scheduled | |
| C2 `menu.h` contract | as scheduled | |
| C3 sharing paragraph | as scheduled | |
| C4 search-field comment | **placed differently** | See §12.3 |
| C5 `VA_SEARCH_SCENE_EFFECT_MORPH_BIT` | as scheduled | Existing defines untouched |
| C6 `va_searchRestart()` | as scheduled | |
| C7 `va_searchRecordEffectTarget()` | as scheduled | Local-63 guard present |
| C8 `va_scanService()` | as scheduled | The VOICE classification lines are unchanged; for VOICE the only change is where the cursor increment sits (same behaviour) |
| C9 Pattern-clear restart | as scheduled | `menu_isScreenPage()` gate |
| C10 `menu_effectCellAutomated()` | as scheduled | No type filter (U3) |
| C11 `menu_applyEffectMarkers()` | as scheduled | |
| C12 Effect scan and shared retry | as scheduled | Exactly one `VA_MARKER_RETRY_BIT` test remains |
| C13 Effect-entry restart | as scheduled | |
| C14 `menu_setActiveVoice()` | as scheduled | |
| C15 `menu_setShownPattern()` | as scheduled | |
| C16/C17 `menuEffects_cellSeqLocked()` | as scheduled | All 16 steps |
| F1 `MENU_FX_ACT_HOLD_REPAINT` | as scheduled | |
| F2 hold transitions use the new bit | as scheduled | The Scene/type branch keeps `MENU_FX_ACT_REPAINT` |
| F3 `menu_repaint()` for hold transitions | as scheduled | The hold bit takes precedence |

### 12.3 Findings

1. **C4 placement (cosmetic).** The per-field search comment sits *after*
   `va_searchTargetMask[8]` (`menu.c:1272–1291`), directly before the C5
   comment for `va_searchSceneMask`. The schedule put it above
   `va_searchTrack`. The text is correct, but two comment blocks now sit
   back to back, and the first one describes the lines above it.
   Recommendation: move it above `static uint8_t va_searchTrack` (line
   1267). There is no functional effect.
2. **No functional defects found.** Specifically:
   - the scan keeps one entry buffer (no stack growth);
   - `va_searchRestart()` runs after `menu_activePage` is set at Effect
     entry;
   - the local-63 guard is present;
   - hold transitions never reach `menu_repaintAll()`;
   - the VOICE retry behaves as before.

### 12.4 Build verification (2026-09-29)

These are after the F1–F3 build, and after the image-script fold in §12.5.

| Gate | Result |
|---|---|
| `make all` (Menu sources and `flashImage.c` forced to recompile) | **PASS.** No warnings from project sources. The only warnings are newlib's `_close`/`_lseek`/`_read`/`_write` stubs (`-specs=nosys.specs`) and the LTO serial-compilation note, which appear at every relink and are unrelated. |
| `size` | `text=487,544`, `data=416`, `bss=426,336` |
| `link_budget.py` | Flash 487,960 / 753,664 B, headroom 265,704 B; ITCM 4,168 B; DTCM statics 4,448 B; FX arena 126,624 B (margin 3,744) |
| RAM against the S073 close | **unchanged** (`data`, `bss`, ITCM, DTCM, arena). The 45 B `_Static_assert` compiles. |
| Flash against the S073 close | payload 487,104 → 487,960 B (+856 B for C1–C17 and F1–F3) |
| `git diff --check` | clean |
| Image | `build/LXRV2_lxr02.img`, 487,976 B; SHA-256 `0e004720767220d9ab7fc7589691314c2526e899f0cbd9a903fcffaacd0ddeea` |

**Headroom note:** the image now ends at `0x0807F218`, 3,560 B below sector
6 (`0x08080000`). The next change of that size will be the first image the
bootloader has to write past `0x08080000`. The boot image check exists to
report a failure there.

### 12.5 Build tooling change made in the same session (user request)

The image check stamp was folded into `tools/build_lxrv2_img.py`, so one
script now produces the card image. `tools/stamp_image_check.py` is deleted,
and its cosmetic "Image check: stamped at …" line is gone. Details:

- **Makefile:** the `.bin` rule is objcopy only. The `img` rule calls
  `build_lxrv2_img.py <nm> <elf> <bin> <img>`.
- **Unstamped `.bin`:** `lxr02.bin` now stays the raw, unstamped objcopy
  output. The stamped payload exists only inside the `.img`. **Record the
  `.img` SHA-256 from now on, not the `.bin`.**
- **Failure handling:** the script checks the layout exactly as before. On
  any failure it prints the reason to stderr, deletes any previous `.img`,
  and exits non-zero.
- **Verified:**
  - the new `.img` is **byte-identical** to the image built with the old
    two-script flow (same SHA-256 as above);
  - re-stamping an already stamped `.bin` gives the same image;
  - a truncated `.bin` and a missing check block each fail and leave no
    image.
- **References updated:** comments in `STM32F765VIHx_FLASH.ld` and
  `flashImage.c/h`; `README.md`, `MEMORY.md`, `STORAGE_SRAM_MANIFEST.md`
  §3.4–§3.5 and `MODULE_INTERCHANGE_SPEC.md`. The session logs are history
  and were left as they are.

### 12.6 Still open

- §12.3 item 1 (comment placement).
- §9 items 1–2 and §11.7: log them in `SCOPING_TARGETS.md` if you want them
  tracked.
- The §10 documentation follow-ups (`EFFECTS_BUS_REFERENCE.md` §8.3,
  `MODULE_INTERCHANGE_SPEC.md`, the `SCOPING_TARGETS.md` closeout, the 074
  handoff log).
- An existing inaccurate comment, not part of S074: `main.c:532` says the
  stamped CRCs are "at the end of the load image". They are in sector 1,
  right after the vector table.
