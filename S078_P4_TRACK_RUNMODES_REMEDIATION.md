# S078 P4 — Track Run Mode Remediation

Three defects in the STEP track-settings run-mode cell (S078 P1 §4, §7):

| # | Symptom | Root cause (one line) |
|---|---------|-----------------------|
| 1 | The STEP SHIFT Morph view stays on after SHIFT is released | `menu_patternTrackMorphEndpoint` is cleared only by a SHIFT release that is processed while still in STEP mode. Three routes skip that release (§1.2) |
| 2 | The run-mode cell shows `---` and cannot be changed while the Morph view is on | The cell was deliberately blanked and its commit refused in the Morph view. That breaks the project rule: in **any** SHIFT Morph view, a non-morphable parameter shows and edits its **Normal** value, as on VOICE |
| 3 | The cell is labelled `mod` / `Pattern` / `PlayMode` | The P1 working name was put in the label tables. Required: `run` / `Track` / `RunMode`, matching the FX sequencer's `run` cell (`FX Seq` / `RunMode`) |

Defects 1 and 2 together produce the reported symptom: a stuck flag puts a
plain STEP page into the Morph view, and the Morph view then blanks and locks
the run-mode cell.

All root causes were found by reading the code. Line numbers refer to the
current working tree on `dev-ph6-cleanup` (S078 P2 + P3 applied,
uncommitted).

**Not at fault** — the run-mode feature itself is wired end to end:

- **Storage and persistence:** `track_play_mode[7]`, PAT4 header byte 3,
  `pat_setTrackPlayMode()`, `pat_applyTrackSettingsToMenu()`.
- **Copy/clear:** copyClearService.c:854, :1027.
- **Menu dispatch:**
  - `menu_cellCommitValue()` → `menu_sendEditedParameter()` →
    `menu_sendSoundParameter()`.
  - `PAR_TRACK_PLAY_MODE > END_OF_SOUND_PARAMETERS`, so it goes to
    `menu_parseGlobalParam()` → `case PAR_TRACK_PLAY_MODE` (menu.c:14979) →
    `pat_setTrackPlayMode()`.
- **Sequencer:** the fwd/rev/pip/rnd/onc/1fr switch in
  `seq_advanceTrackStep()` (sequencer.c:1335–1407).

With the flag clear, the cell displays and edits correctly.

---

## 1. Defect 1 — the STEP SHIFT Morph view hangs

### 1.1 How the flag is meant to work

`menu_patternTrackMorphEndpoint` (menu.c:831) is the S078 §5.6 "SHIFT held on
the STEP front page" flag. `menu_patternTrackMorphViewActive()` (menu.c:840)
reports it as active while the flag is set and `menu_activePage == SEQ_PAGE`.
Only `menu_setPatternTrackMorphEndpoint()` writes it, and that function has
two callers, both in buttonHandler.c:

| Event | Site | Effect |
|-------|------|--------|
| SHIFT **press** processed while `selectButtonMode == SELECT_MODE_STEP` | buttonHandler.c:1868–1875 | flag = 1 |
| SHIFT **release** processed while `selectButtonMode == SELECT_MODE_STEP` | buttonHandler.c:2048–2052 | flag = 0 |

Nothing else clears it. Not page changes (`menu_switchPage()` ends the VOICE
Morph view and the held-step overlay, but not this flag), not STEP entry
(`buttonHandler_enterSeqModeStepMode()`, buttonHandler.c:564), and not
event-ring overflow reconciliation.

The press and release are always handled. `processPress()` and
`processRelease()` have no early return that can consume a `BUT_SHIFT` edge
before their `BUT_SHIFT` case: `copyClear_buttonPressed()` returns 0 for SHIFT
and `copyClear_buttonReleased()` matches only SEQ/SELECT/TRACK. So the flag
hangs only when the release is **dropped**, or is processed **in another
mode**.

### 1.2 The three routes to a stuck flag

**Route A — SHIFT+mode button from STEP (deterministic, the common one).**
`handleModeButtons()` is the only writer of `selectButtonMode`
(buttonHandler.c:1142, :1156, :1158). With SHIFT held it selects the shifted
mode, `(mode + 4) & 7` (buttonHandler.c:1155–1156), or the SHIFT+VOICE Morph
latch (1128–1150). So every SHIFT+mode-button gesture from STEP changes the
mode **between** the SHIFT press (STEP arm: flag = 1) and the SHIFT release
(new mode's arm: the STEP flag is not touched):

| Gesture from STEP | New mode | Page |
|-------------------|----------|------|
| SHIFT+PERF | FX (5) | Effect page — the normal way to open it |
| SHIFT+VOICE | VOICE (Morph latch) | voice page |
| SHIFT+LOAD/SAVE | MENU (7) | MENU_MIDI_PAGE |
| SHIFT+STEP | SOM (6) | SOM_PAGE |

The next plain STEP entry shows the SEQ page with the flag still set. This is
the likely trigger here: the Effect-page regression test (P3 test 14) is
reached from STEP with SHIFT+PERF.

**Route B — mode changes but the page switch is refused (busy).** While
`menu_storageBusy` or `preset_getStatus() != PRESET_IDLE`,
`menu_switchPage()` returns at menu.c:14400–14420 without switching. It only
queues the switch when leaving LOAD/SAVE. A SHIFT+mode press on the STEP page
during that window still changes `selectButtonMode` (Route A), but the SEQ
page **stays visible**. The release then runs in the new mode's arm, so the
flag stays set on a page that is still showing.

**Route C — the SHIFT release edge is dropped.** `evt_push()`
(buttonHandler.c:117–129) drops an edge when the 64-slot event ring is full,
for example during a long foreground stall with buttons being pressed. The
overflow reconciliation in `buttonHandler_processEvents()`
(buttonHandler.c:2092–2124) resets copy/clear, pairing masks, the
voice-mix overlay, PERF holds and the hold timer, but not SHIFT-derived view
state. A dropped STEP SHIFT release leaves the flag set. This is rare by
design (41 buttons < 64 slots), but possible.

### 1.3 Wider impact (silent)

While the flag is stuck, `menu_patternTrackMorphEndpointActive()`
(menu.c:854) is also true for `len`, `scl` and `shf`:

- They show the Scene Morph endpoints (`menu_getParameterDisplayValue()`,
  menu.c:909–921).
- Pot and encoder edits are written to the Morph endpoints
  (`menu_cellCommitValue()`, menu.c:4826–4850).

Nothing on screen says so, because SHIFT is not held. **`len`/`scl`/`shf`
edits made in STEP mode after any of these routes may have changed Morph
endpoints instead of the Pattern.** Check them with SHIFT held, or equalise
them with `clear → reset morph`.

### 1.4 Fix (F1) — gate the view on the physical SHIFT state

The STEP Morph view is **momentary**: it has no latch, unlike SHIFT+VOICE. It
must therefore never be active while SHIFT is physically up. Adding that
condition to the single predicate closes all three routes at once:

- Route A: on return to STEP, SHIFT is up.
- Route B: SHIFT is up after the release.
- Route C: `btn_held[BUT_SHIFT]` is written by the scan, not the ring, so it
  is correct even when the edge was dropped.

Clearing the flag on SEQ exit (the previous version of this plan) would cover
Route A only.

**`Core/Menu/menu.c:833–844`, MODIFY** `menu_patternTrackMorphViewActive()`
(header and body):

```c
/*
 * Test whether the STEP-page track Morph endpoint view is showing.
 *
 * What: nonzero only while the STEP SHIFT flag is set, the SEQ page is
 * showing, and SHIFT is physically held (S078 P4).
 * Why: the STEP Morph view is momentary (no latch, unlike SHIFT+VOICE). The
 * flag is cleared only by a SHIFT release processed in STEP mode, and three
 * routes skip that: a SHIFT+mode gesture from STEP (the release runs in the
 * new mode), a mode change whose page switch was refused while storage or
 * Preset was busy (the SEQ page stays visible), and a SHIFT release edge
 * dropped by event-ring overflow. The physical SHIFT state
 * (btn_held[BUT_SHIFT], written by the scan, not the ring) is correct in
 * every case, so gating on it ends a stuck view on all three routes without
 * a reset at each one. The Effect page already re-syncs its Morph view from
 * buttonHandler_getShift() in the same way (menu_switchPage() Effect arm,
 * menu_fxVoiceMixOverlayEnd()).
 * Inputs: menu_patternTrackMorphEndpoint, menu_activePage,
 * buttonHandler_getShift(). Output: 0/1. Accessors:
 * menu_patternTrackMorphEndpointActive() (len/scl/shf display and commit
 * redirection). Affiliates: menu_setPatternTrackMorphEndpoint(),
 * buttonHandler.c SHIFT press/release STEP arms. Foreground only.
 */
static uint8_t menu_patternTrackMorphViewActive(void)
{
    return (uint8_t)(menu_patternTrackMorphEndpoint != 0u &&
                     menu_activePage == SEQ_PAGE &&
                     buttonHandler_getShift() != 0u);
}
```

Notes:

- **Callers:** all foreground Menu code: `menu_getParameterDisplayValue()`
  via `menu_patternTrackMorphEndpointActive()`, and `menu_cellCommitValue()`.
  After F2 the predicate has no play-mode client. `menu.c` already calls
  `buttonHandler_getShift()` (menu.c:14616, :15476, :15501), so no new
  include is needed.
- **Stale flag:** the stale flag value itself is harmless. The next SHIFT
  press in STEP sets it again, and the release clears it.
- **Event lag:** `btn_held[]` is updated at scan time, before the queued
  release is processed. The view therefore ends at the physical release, and
  the processed release then repaints it. It never extends past it.
- **RAM:** none. No buttonHandler change.

---

## 2. Defect 2 — `---` for a non-morphable parameter in the Morph view

### 2.1 Rule (user, S078 P4)

> In any SHIFT Morph view, a parameter that is not morphable shows and edits
> its **Normal** value. It is never blanked and never locked.

Current state of each Morph view:

| View | Non-morphable handling | Status |
|------|------------------------|--------|
| VOICE SHIFT / SHIFT+VOICE latch | `menu_paramUsesMorphView()` redirects only sound parameters; everything else reads and writes Normal | correct |
| Effect page SHIFT | `effects_getParameter()` / `effects_setParameterScene()` (EffectsManager.c:499–502, :570–578) fall back to the Normal image when the row lacks `INSTRUMENT_PARAM_FLAG_MORPHABLE`; `run`/`len`/`scl`/`typ` always use the record | correct |
| STEP SHIFT | `mch` and `not` show and edit Normal; **`run` is blanked and locked** | **wrong** |

`menuText_dash` has no other Morph-view use in Menu. Its other uses
(`getMenuItemNameForValue()`, `menu_getLfoPolarityName()`) are
out-of-range fallbacks.

### 2.2 The two offending sites

1. `menu_getPlayModeName()`, menu.c:9320–9327, prints `---` whenever
   `menu_patternTrackMorphViewActive()` is true.
2. `menu_cellCommitValue()`, menu.c:4818–4825, returns 0 for
   `PAR_TRACK_PLAY_MODE` while `menu_patternTrackMorphViewActive()` is true.

Both were written to S078 P1 §5.6 ("The play mode cell is not shown in morph
view"), which contradicts the rule above. `PAR_TRACK_PLAY_MODE` is not
redirected by `menu_patternTrackMorphEndpointActive()`, so once both blocks
are gone it reads and writes Normal (`parameter_values[]` →
`pat_setTrackPlayMode()`) in either view. That is exactly the rule.

### 2.3 Fix (F2)

**`Core/Menu/menu.c:4818–4825`, REMOVE:**

```c
    /*
     * S078 §5.6: the discrete play-mode cell is hidden in the STEP Morph view,
     * so an encoder/pot turn there must not change the Normal play mode.
     */
    if (cell->kind == MENU_CELL_STATIC &&
        cell->static_param == PAR_TRACK_PLAY_MODE &&
        menu_patternTrackMorphViewActive())
        return 0u;
```

**`Core/Menu/menu.c:4826–4827`, ADD above the existing
`if (cell->kind == MENU_CELL_STATIC && menu_patternTrackMorphEndpointActive(...))`
block** (comment only, documenting the rule at the redirection point):

```c
    /*
     * SHIFT Morph view rule (S078 P4, project-wide): only morphable cells are
     * redirected to a Morph endpoint. On the STEP page that is len/scl/shf
     * (menu_patternTrackMorphEndpointActive()). The non-morphable run, mch,
     * and not cells fall through to the ordinary Normal commit below, so a
     * Morph view never blanks or locks a parameter, matching VOICE
     * (menu_paramUsesMorphView()) and the Effect page (EffectsManager Normal
     * fallback). Affiliates: menu_getParameterDisplayValue(),
     * menu_getPlayModeName().
     */
```

**`Core/Menu/menu.c:9315–9338`, MODIFY** `menu_getPlayModeName()`. Remove
the Morph-view block (9320–9327) and update the header:

```c
/*
 * Format one per-track run-mode value into a three-character field.
 *
 * What: writes the fwd/rev/pip/rnd/onc/1fr token for stored values 0..5, and
 * clamps stale larger values to the last token. Inputs: raw stored value and a
 * three-character output. Outputs: exactly three characters, no terminator.
 * Why: the stored byte is the raw run-mode enum and has no DTYPE_MENU table
 * id (the nibble space is full), so the value/clamp is applied by static-param
 * special cases. The run mode is not morphable, so every view - including the
 * STEP SHIFT Morph view - shows its Normal value (S078 P4 rule; never `---`).
 * Affiliates: menu_formatCellValue3(), va_formatValue3(),
 * menu_clampCellValue(), the edit-view painter.
 */
static void menu_getPlayModeName(uint16_t value, char *buf)
{
    const char *p;
    uint8_t count = (uint8_t)trackPlayModeNames[0][0];

    if (count == 0u) {
        memcpy(buf, menuText_dash, 3);
        return;
    }
    if (value >= count)
        value = (uint16_t)(count - 1u);
    p = trackPlayModeNames[value + 1u];
    buf[0] = p[0];
    buf[1] = p[1] ? p[1] : ' ';
    buf[2] = p[2] ? p[2] : ' ';
}
```

(The `count == 0u` guard is a table-integrity fallback, not a Morph-view
blank, and stays.)

**`Core/Menu/menu.c:833–839`:** the predicate header's client list
("menu_getPlayModeName() (hides the discrete play-mode cell)") is replaced by
the F1 header above.

**RAM:** none.

---

## 3. Defect 3 — label `mod` / `Pattern` / `PlayMode` → `run` / `Track` / `RunMode`

### 3.1 Current tables

| Table | Site | Current | Required |
|-------|------|---------|----------|
| `shortNames[]` | MenuText.h:160–161 | `{"mod"}` (own slot, `SHORT_PLAY_MODE`) | `{"run"}` |
| `longNames[]` | MenuText.h:199–200 | `{"PlayMode"}` (`LONG_PLAY_MODE`) | `{"RunMode"}` |
| `catNames[]` | MenuText.h:164–173 | no "Track" entry | add `{"Track"}` |
| `catNamesEnum` | menu.h:156–164 | ends at `CAT_SCENE` | add `CAT_TRACK` |
| `valueNames[TEXT_TRACK_PLAY_MODE]` | menu.c:1272–1273 | `{SHORT_PLAY_MODE,CAT_PATTERN,LONG_PLAY_MODE}` | `{SHORT_PLAY_MODE,CAT_TRACK,LONG_PLAY_MODE}` |

- `SHORT_PLAY_MODE` has its own slot. The other `{"mod"}` entries at
  MenuText.h:142 and :148 are `SHORT_MOD` (LFO amount) and `SHORT_MODE`, and
  are not touched.
- The `catNames` lookups (menu.c:9350, :11364) index by enum, so adding one
  entry at the end changes no existing category.
- The FX sequencer's `run` cell uses `run` / `FX Seq` / `RunMode`
  (menuEffects.c:530, :597–598).

### 3.2 Fix (F3)

**F3.1 `Core/Menu/MenuText.h:160–161`, MODIFY:**

```c
    /*
     * S078 per-track run mode compact label (S078 P4: renamed from `mod`).
     * `run` matches the FX sequencer's run-mode cell (menuEffects.c), so the
     * same concept reads the same on the Effect page and the STEP track
     * settings. Accessor: valueNames[TEXT_TRACK_PLAY_MODE] (menu.c).
     */
    {"run"},
```

**F3.2 `Core/Menu/MenuText.h`, ADD after `{"Scene"},` (line 172):**

```c
    /*
     * S078 P4: CAT_TRACK, the full-view category of per-track Pattern
     * settings. The clicked-in view of the STEP run-mode cell reads
     * "Track   RunMode", in parallel with the FX sequencer's
     * "FX Seq  RunMode". Accessor: valueNames[TEXT_TRACK_PLAY_MODE].
     */
    {"Track"},
```

**F3.3 `Core/Menu/MenuText.h:199–200`, MODIFY:**

```c
    /* S078 per-track run mode long name (S078 P4: renamed from PlayMode to
     * match the FX sequencer's RunMode). */
    {"RunMode"},
```

**F3.4 `Core/Menu/menu.h:162–163`, MODIFY** (`catNamesEnum`):

```c
    /* S074: Scene-owned static cells on the bus compressor page. */
    CAT_SCENE,
    /* S078 P4: per-track Pattern settings category ("Track"); parallel to
     * catNames[] in MenuText.h, which must stay index-aligned. */
    CAT_TRACK
```

**F3.5 `Core/Menu/menu.c:1272–1273`, MODIFY:**

```c
    /* S078 per-track run mode: run / Track / RunMode (S078 P4 relabel). */
    {SHORT_PLAY_MODE,CAT_TRACK,LONG_PLAY_MODE},
```

**F3.6 Comment-only wording updates:**

- MenuText.h:114–120 (`trackPlayModeNames` header): "the STEP
  track-settings "mod" cell" → "the STEP track-settings `run` cell".
- menu.h:197: `/* S078 per-track play mode. */` →
  `/* S078 per-track run mode long name: RunMode (S078 P4). */`.
- menu.h:230: `/* S078 per-track play mode compact label: mod. */` →
  `/* S078 per-track run mode compact label: run (S078 P4). */`.

Symbol names (`TEXT_/PAR_/SHORT_/LONG_PLAY_MODE`, `track_play_mode[]`)
are kept. Renaming them would touch PatternData, persistence, copy/clear and
the sequencer for no behavioural gain.

**RAM:** none (+16 B flash for the new `catNames` row).

**Not changed:** `len`, `scl` and `shf` keep category `Pattern`. Moving them
to `Track` is three one-word edits in `valueNames[]` (menu.c:1214, :1237,
:1256) if wanted. `shf`'s `TEXT_SHUFFLE` row would move alone.

---

## 4. Files changed

| File | Change |
|------|--------|
| `Core/Menu/menu.c` | F1 (predicate gate + header), F2 (remove commit guard, add rule comment, remove Morph-view blank in `menu_getPlayModeName()`), F3.5 |
| `Core/Menu/menu.h` | F3.4, F3.6 (comments at :197, :230) |
| `Core/Menu/MenuText.h` | F3.1, F3.2, F3.3, F3.6 |

No changes to buttonHandler, PatternData, filesystem, copy/clear, or the
sequencer.

## 5. Documentation follow-up

- `S078_P1_TRACK_SETTINGS.md` §5.6: replace "The play mode cell (`mod`) is
  not shown in morph view (discrete, non-morphable)" with "The run mode cell
  (`run`) shows and edits its Normal value in the Morph view (non-morphable;
  S078 P4 rule)". In §7, rename `mod` to `run`.
- `MEMORY.md` (repo): record the durable rule from §2.1, that in any SHIFT
  Morph view a non-morphable parameter shows and edits its Normal value and
  is never blanked or locked, with VOICE as the reference behaviour.

## 6. Testing

| # | Steps | Expected | Result |
|---|-------|----------|--------|
| 1 | Fresh boot → STEP → track settings | Cell 4 reads `run`, value `fwd` | PASS |
| 2 | Turn pot 4 / click in and turn the encoder | Cycles fwd/rev/pip/rnd/onc/1fr and clamps at both ends; the clicked-in view reads `Track   RunMode` | PASS |
| 3 | In STEP, hold SHIFT | `len`/`scl`/`shf` show the Morph endpoints; `run`, `mch`, `not` show their Normal values; turning `run` changes the Normal run mode | PASS |
| 4 | Release SHIFT after 3 | `len`/`scl`/`shf` show Normal; `run` keeps the value set in 3 | PASS |
| 5 | **Route A:** STEP → SHIFT+PERF → release SHIFT → STEP button | No Morph view: `len`/`scl`/`shf` show Normal and edit Normal (confirm by holding SHIFT afterwards: the Morph endpoints are unchanged) | PASS |
| 6 | Route A via SHIFT+VOICE, SHIFT+LOAD/SAVE, SHIFT+STEP, then back to STEP | Same as 5 | PASS |
| 7 | **Route B:** start a Scene load/apply from PERF, return to STEP, and during the busy window press SHIFT+PERF, then release SHIFT | Morph view ends when SHIFT is released, even if the page did not switch | PASS |
| 8 | Each run mode audibly: rev, pip (ends play twice), rnd, onc (stops after one pass, aligned), 1fr (from step 0) | Per S078 P1 §4.2 | PASS |
| 9 | onc/1fr retrigger: transport stop/start, Scene change, per-track Scene reassignment; double-click realign does **not** restart a stopped track | Per S078 P1 §4.3 | PASS |
| 10 | Save/Load the Scene; copy track; clear track | Run mode persists, copies, and resets to `fwd` | PASS |
| 11 | Regression: VOICE SHIFT Morph view and SHIFT+VOICE latch; Effect-page SHIFT Morph view; FX `run` cell; P3 STEP underlines/pot clears (SHIFT+COPY still clears `len`/`scl`/`shf` while SHIFT is held) | Unchanged | PASS |

Route C (dropped release on ring overflow) cannot be forced on hardware. F1
covers it by construction, because `btn_held[]` does not depend on the ring.
