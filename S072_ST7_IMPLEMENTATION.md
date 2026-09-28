# S072 Step 7 — Implementation Schedule: FX Menu (SHIFT+PERF)

**Scope:** `EFFECTS_BUS_FEATURE_PLAN.md` §13.1–§13.3, §13.5, §13.6, §7.2 (the
`typ` gesture and commit), §16.1 item 8, and §17.1 Step 7:

> `menuEffects.c`: pages, default SELECT layout, `typ`, SHIFT Morph view,
> TRACK/SHIFT+TRACK, Euklid disabled.

**Gate:** a hardware UI walk-through (§11).

**What becomes usable:**

- SHIFT+PERF opens the Effect page for the active Scene.
- Every Effect parameter can be edited from the panel: `typ`, `out`/`vol`/`pan`,
  `run`/`len`/`scl`/`mrp`, and the type parameters (StereoFilter
  `frq`/`res`/`drv`/`typ`).
- Holding SHIFT shows and edits Morph endpoints.
- The type change runs the plan §7.2 transaction on encoder click-out.
- Everything persists through AutoSave and Scene/Bank Save (Step 6).

**Not in this step:**

- SEQ-button lock editing, `sel`, and the FX step LEDs and chase (Step 8). SEQ
  buttons do nothing on the Effect page in Step 7.
- Pattern automation `fx` category and LFO `fx` namespace (Step 9).
- Edit-mask fan-out and the type-matching gate (Step 10). Step 7 edits write
  the **active Scene only**; see D3.
- Effect library Load/Save screens (A35).

**Prerequisite:** apply Step 6 Findings 1 and 2 (`S072_ST6_IMPLEMENTATION.md`
§12.3) first, so the Step 7 walk-through saves through the final Step 6 code.

**Status:** source implementation complete; the hardware UI walk-through gate is
pending.

**Line numbers** refer to the Step 6 working tree reviewed in
`S072_ST6_IMPLEMENTATION.md` §12.

---

## 0. Decisions and notes

### D1 — Re-pressing the current SELECT button (confirm)

The plan's §13.2 line reads: "Pressing SELECT n jumps to its first screen."
VOICE mode instead **cycles** the screens behind the same SELECT button on a
re-press (`menu_switchSubPage()`).

**Proposal:**

- A press of a *different* SELECT button lands on that button's first screen,
  as in the plan.
- A *re-press* of the current button cycles to its next screen and wraps.

This keeps the Effect page consistent with VOICE and makes the 2- and 3-screen
buttons reachable without the encoder. SELECT 1 therefore toggles
`typ out vol pan` ↔ `run len scl mrp`, the same toggle as a repeated SHIFT+PERF.

If you prefer the literal plan text, the only change is one line in
`menuEffects_selectPressed()` (§4.2): always set screen 0.

### D2 — Provisional `scl` display labels (confirm)

The shared 14-entry step-scale table is built in Step 8, together with the
track-scale UI switch (§11.3). Step 7 still has to display the retained index.

**Proposal:** a menuEffects-local table, in index order (the same order as the
Step 6 `.fx` tokens). Step 8 moves it to the sequencer-owned shared table.

| Index | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Compact (3) | `/64` | `32t` | `/32` | `16t` | `/16` | `/8t` | `16.` | `/8 ` | `/4t` | `/8.` | `/4 ` | `/2 ` | `1br` | `2br` |
| Full view | `1/64` | `1/32T` | `1/32` | `1/16T` | `1/16` | `1/8T` | `1/16.` | `1/8` | `1/4T` | `1/8.` | `1/4` | `1/2` | `1 bar` | `2 bars` |

### D3 — Step 7 edits affect only the active Scene (acknowledge)

- VOICE edits already fan out through the active Scene's voice-edit mask.
  Effect edits do not until Step 10 (A44, §17.1).
- To make Step 10 a one-place change, every Effect write goes through a new
  **EffectsManager edit API** (§2). Menu, and later type UI hooks and the FX
  sequencer editor, never call SceneData Effect setters directly (§13.6
  rule). Step 10 then adds the mask loop inside these setters.

### D4 — PERF SHIFT loses its empty pattern-settings page (acknowledge)

Plan §13.1 compiles out "the Euklid entry, its SHIFT paths, and
`PATTERN_SETTINGS_PAGE`/rotation entry" behind `ENABLE_EUKLID_PAGE 0`.

- Today, pressing SHIFT in PERF switches the LCD to the (now empty)
  `PATTERN_SETTINGS_PAGE` and blinks the rotation step LED. After Step 7,
  SHIFT in PERF only lights SHIFT and shows the mute LEDs; PERF stays visible.
- The SHIFT-release path, which re-enters PERF, and SHIFT's PERF
  voice-button meaning (clear mutes up to the pressed voice) are unchanged.
- The Euklid and pattern-settings source, the page tables, and `EUKLID_PAGE`
  remain in the tree.

### D5 — RAM and flash (acknowledge)

| Object | Region | Bytes | Status |
|---|---|---|---|
| menuEffects page state (8 screen cells, Morph-view flag, `typ` edit flag / candidate / Scene, last Scene / type) | SRAM1 | 14 | approved, §16.1 item 8 (about 16) |
| `menu_cell_t` gains an embedded 8-byte `menuEffects_cell_t` | stack only | +8 per resolved cell | — |

- DTCM and the FX buffer are unchanged.
- Estimated flash: **+3 to +4.5 KB** (menuEffects.c, the EffectsManager edit
  API, dispatch branches). Headroom is 20,904 B.

### Notes (no decision needed)

1. **Page architecture.**
   - The Effect page reuses menu.c's four-cell compact renderer: the value
     formatter, the full edit view, the encoder, and the endless pots.
   - It does this through one new cell kind, `MENU_CELL_EFFECT`, whose
     value / dtype / format / clamp / commit calls delegate to `menuEffects.c`
     (A43: "menu.c delegates").
   - Cursor, screen memory, layout and navigation are owned entirely by
     `menuEffects.c`, through early `EFFECT_PAGE` branches at menu.c's
     navigation entry points.
   - The VOICE screen machinery (`menu_voiceSubPageScreen[]`, the mix-page
     Scene screens, the held-step overlay) is **not modified**.
2. **Chase suppression already holds.**
   - `led_updateCurrentStep()` shows the chase only on voice, SEQ and Euklid
     pages (`ledHandler.c:1202-1204`), so `EFFECT_PAGE` is dark without any
     change.
   - Step 7 additionally keeps the recording-refresh and follow-mode paths
     from painting the SEQ row on the Effect page (§8). The full FX LED
     ownership is Step 8.
3. **Runtime needs no change.**
   - Parameter writes reach DSP through `effects_service()`'s every-block
     rescan (Step 4).
   - A type change goes through `effects_changeType()`, which already performs
     plan §7.2 steps 1–6 and 9: handoff export, type set, 3..63 defaults, full
     sequence clear, `init`, and the AutoSave whole-record mark.
   - Step 7 (LFO re-validation) is Step 9; step 8 (fan-out) is Step 10.
4. **Type UI hooks** (§13.6) are defined and dispatched, but no registered type
   provides them (`ui = NULL`). Their only observable behavior in Step 7 is "not
   handled", which falls back to the defaults.

---

## 1. Change index

| # | File | Line(s) | Op | Summary |
|---|---|---|---|---|
| 1 | `Core/DSP/Effects/EffectsManager.h` | 114–116 | modify | Define `struct effect_ui_hooks` (replaces the Step 4 placeholder) |
| 2 | `Core/DSP/Effects/EffectsManager.h` | after the `.fx` helper block (~210) | add | `effect_image_t` and the Effect edit API |
| 3 | `Core/DSP/Effects/EffectsManager.c` | after `effects_laneByFileKey()` (~312) | add | Edit API implementation |
| 4 | `Core/Menu/menuEffects.h` | new | add | Effect-page API for menu.c and buttonHandler.c |
| 5 | `Core/Menu/menuEffects.c` | new | add | Layout, cursor, values, `typ` transaction, Morph view, hooks, service |
| 6 | `Makefile` | after 89 | add | `Core/Menu/menuEffects.c` |
| 7 | `config.h` | after 256 | add | `ENABLE_EUKLID_PAGE 0` |
| 8 | `Core/Menu/menu.h` | 76–77, after 407 | modify/add | `EFFECT_PAGE`; `menu_setEffectShowMorph()` prototype |
| 9 | `Core/Menu/menuPages.h` | before 137 | add | `EFFECT_PAGE` table row (resolved in code) |
| 10 | `Core/Menu/menu.c` | 30–61 | add | `#include "menuEffects.h"` |
| 11 | `Core/Menu/menu.c` | 1639–1676 | modify | `MENU_CELL_EFFECT`; `menu_cell_t.fx` |
| 12 | `Core/Menu/menu.c` | after 1763 | add | `menu_isScreenPage()` |
| 13 | `Core/Menu/menu.c` | 2921 | add | `menu_resolveCell()` Effect branch |
| 14 | `Core/Menu/menu.c` | 3157, 3178, 3244, 3883, 3987 | add | dtype / value / commit / format / clamp Effect branches |
| 15 | `Core/Menu/menu.c` | 7656 | add | `checkScrollSign()` Effect branch |
| 16 | `Core/Menu/menu.c` | 9236–9310, 9401–9420 | modify | Full edit view and compact names for Effect cells |
| 17 | `Core/Menu/menu.c` | after 9504 | add | Encoder on `typ` browses candidates |
| 18 | `Core/Menu/menu.c` | before 9627 | add | Linear Effect navigation with SELECT LED follow |
| 19 | `Core/Menu/menu.c` | after 10459 | add | Encoder click in/out drives the `typ` transaction |
| 20 | `Core/Menu/menu.c` | 10536, 10583, 10641 | modify | Compact-column mapping uses `menu_isScreenPage()` |
| 21 | `Core/Menu/menu.c` | 10590–10593, after 10648 | modify/add | Double-rate pots for 0..255 Effect cells; pots never edit `typ` |
| 22 | `Core/Menu/menu.c` | before 10777 | add | Effect-page live service |
| 23 | `Core/Menu/menu.c` | 11694, 11778 | add | `switchSubPage` / `resetActiveParameter` Effect branches |
| 24 | `Core/Menu/menu.c` | 11807–12020 | modify/add | `menu_switchPage()`: leave/enter/toggle, LEDs |
| 25 | `Core/Menu/menu.c` | after 12646 | add | `menu_setEffectShowMorph()` |
| 26 | `Core/Hardware/frontPanel/buttonHandler.h` | 21 | modify | `SELECT_MODE_FX` replaces `SELECT_MODE_PAT_GEN` |
| 27 | `Core/Hardware/frontPanel/buttonHandler.c` | 27, 427–439, 876–887, 900–958, 1100–1180, 1305–1336, 1338–1403, 1487–1540 | modify | FX mode entry, SELECT, TRACK, BAR, SHIFT; Euklid compiled out |
| 28 | `Core/Hardware/frontPanel/ledHandler.c` | 1243, 1419–1423 | modify | Keep the SEQ row dark on the Effect page |
| 29 | `config.h` 248–255 | comment | modify | `DEV_EFFECT_FORCE_TYPE` comment: Step 7 now supplies the user path |
| 30 | Docs | — | modify | MODULE_INTERCHANGE, SRAM_MANIFEST, BANK_PRESET_ARCHITECTURE, plan §13/§17.1, MEMORY |

No change to SceneData, Autosave, filesystem, storageTypes, mixer, sequencer,
or any Instrument engine.

---

## 2. EffectsManager — UI hooks and the edit API

### 2.1 `EffectsManager.h` lines 114–116 — replace the Step 4 placeholder

Before:

```c
/* Step 7 UI layout placeholder; registry rows remain NULL until then. */
struct effect_ui_hooks;
typedef struct effect_ui_hooks effect_ui_hooks_t;
```

After:

```c
/*
 * Optional per-type Effect-page hooks (Session 072 step 7; plan §13.6).
 *
 * What: lets a type take over SELECT, TRACK, or BAR gestures and add LED
 * rendering on the Effect page. Each input hook receives the zero-based
 * button, the SHIFT state, and pressed (1) / released (0), and returns
 * nonzero when it handled the gesture; zero falls back to the default page
 * behavior. render_leds runs after the page has drawn its own LEDs.
 * Rules: hooks run in foreground, must not block, must write retained data
 * only through the EffectsManager edit API (so fan-out and AutoSave stay
 * consistent), and never touch the filesystem. Any member may be NULL.
 * Affiliates: menuEffects_hook*(), buttonHandler FX-mode branches, and the
 * registry's `ui` field.
 */
struct effect_ui_hooks {
    uint8_t (*select)(uint8_t button, uint8_t shift, uint8_t pressed);
    uint8_t (*track)(uint8_t track, uint8_t shift, uint8_t pressed);
    uint8_t (*bar)(uint8_t bar, uint8_t shift, uint8_t pressed);
    void (*render_leds)(void);
};
typedef struct effect_ui_hooks effect_ui_hooks_t;
```

### 2.2 `EffectsManager.h` — after the `.fx` helper block (after `effects_laneByFileKey` prototype, ~line 210), add

```c
/*
 * Retained Effect edit API (Session 072 step 7).
 *
 * What: the single mutation boundary for user-facing Effect edits: the
 * Effect page now, type UI hooks, and the Step 8 lock editor later. Each
 * setter validates against the Scene's current type, clamps to the
 * descriptor maximum, writes through SceneData's change-aware setter (which
 * marks AutoSave and clears the card-clean bit), and returns nonzero only
 * when a byte changed. Runtime needs no call: effects_service() rescans every
 * block.
 *
 * Why here: plan §13.6 requires edits to flow through EffectsManager so that
 * Step 10 can add edit-mask fan-out inside these functions without touching
 * any caller (S072_ST7 D3). Step 7 writes the given Scene only.
 *
 * Image rule: EFFECT_IMAGE_MORPH addresses the Morph endpoint for Morphable
 * rows only. For a non-Morphable row it reads and writes the single normal
 * value, so "cells without a Morph endpoint show their single value" (§13.5)
 * holds for every caller.
 */
typedef enum {
    EFFECT_IMAGE_NORMAL = 0,
    EFFECT_IMAGE_MORPH
} effect_image_t;

uint8_t effects_paramMorphable(effect_type_id_t type, uint8_t index);
uint8_t effects_getParameter(uint8_t scene_index, uint8_t index,
                             effect_image_t image);
uint8_t effects_setParameter(uint8_t scene_index, uint8_t index,
                             effect_image_t image, uint8_t value);
uint8_t effects_setSeqRunMode(uint8_t scene_index, uint8_t mode);
uint8_t effects_setSeqLength(uint8_t scene_index, uint8_t length);
uint8_t effects_setSeqStepScale(uint8_t scene_index, uint8_t scale);
uint8_t effects_setMorphAmount(uint8_t scene_index, uint8_t amount);
```

### 2.3 `EffectsManager.c` — after `effects_laneByFileKey()`, add

```c
/*
 * Report whether one descriptor row owns a Morph endpoint.
 *
 * Inputs: registry type and descriptor index. Output: nonzero for rows with
 * INSTRUMENT_PARAM_FLAG_MORPHABLE. Clients: the edit API image rule and the
 * Effect page's SHIFT Morph view.
 */
uint8_t effects_paramMorphable(effect_type_id_t type, uint8_t index)
{
    const effect_param_descriptor_t *descriptor =
        effects_descriptor(type, index);

    return (uint8_t)(descriptor &&
        (descriptor->base.flags & INSTRUMENT_PARAM_FLAG_MORPHABLE) != 0u);
}

/*
 * Read one retained Effect endpoint (contract in EffectsManager.h).
 *
 * Output: the Morph cell for a Morphable row in the Morph image, otherwise
 * the normal cell; 0 for an invalid Scene. Indices beyond the type's rows
 * read their stored (unused) byte, which is always a valid 0..255 value.
 */
uint8_t effects_getParameter(uint8_t scene_index, uint8_t index,
                             effect_image_t image)
{
    const effect_record_t *record = scene_effectConst(scene_index);

    if (!record || index >= EFFECT_PARAM_COUNT)
        return 0u;
    if (image == EFFECT_IMAGE_MORPH &&
        effects_paramMorphable(record->type, index))
        return record->morph[index];
    return record->normal[index];
}

/*
 * Write one retained Effect endpoint (contract in EffectsManager.h).
 *
 * Inputs: Scene, descriptor index, image, and value. Output: nonzero when the
 * retained byte changed. Rows outside the Scene's type are rejected, values
 * clamp to the descriptor maximum, and non-Morphable Morph writes land on the
 * single normal value. Step 10 adds edit-mask fan-out here.
 */
uint8_t effects_setParameter(uint8_t scene_index, uint8_t index,
                             effect_image_t image, uint8_t value)
{
    const effect_record_t *record = scene_effectConst(scene_index);
    const effect_param_descriptor_t *descriptor;
    uint8_t before;

    if (!record)
        return 0u;
    descriptor = effects_descriptor(record->type, index);
    if (!descriptor)
        return 0u;
    if (value > descriptor->max_value)
        value = descriptor->max_value;
    if (image == EFFECT_IMAGE_MORPH &&
        (descriptor->base.flags & INSTRUMENT_PARAM_FLAG_MORPHABLE) != 0u) {
        before = record->morph[index];
        scene_setEffectMorphParameter(scene_index, index, value);
        return (uint8_t)(record->morph[index] != before);
    }
    before = record->normal[index];
    scene_setEffectNormalParameter(scene_index, index, value);
    return (uint8_t)(record->normal[index] != before);
}

/*
 * Sequence-setting and Effect Morph setters (contract in EffectsManager.h).
 *
 * Each compares the retained byte around SceneData's normalizing setter, so
 * callers learn whether a repaint or LED refresh is needed.
 */
uint8_t effects_setSeqRunMode(uint8_t scene_index, uint8_t mode)
{
    const effect_record_t *record = scene_effectConst(scene_index);
    uint8_t before;

    if (!record)
        return 0u;
    before = record->seq_run_mode;
    scene_setEffectSeqRunMode(scene_index, mode);
    return (uint8_t)(record->seq_run_mode != before);
}

uint8_t effects_setSeqLength(uint8_t scene_index, uint8_t length)
{
    const effect_record_t *record = scene_effectConst(scene_index);
    uint8_t before;

    if (!record)
        return 0u;
    before = record->seq_length;
    scene_setEffectSeqLength(scene_index, length);
    return (uint8_t)(record->seq_length != before);
}

uint8_t effects_setSeqStepScale(uint8_t scene_index, uint8_t scale)
{
    const effect_record_t *record = scene_effectConst(scene_index);
    uint8_t before;

    if (!record)
        return 0u;
    before = record->seq_step_scale;
    scene_setEffectSeqStepScale(scene_index, scale);
    return (uint8_t)(record->seq_step_scale != before);
}

uint8_t effects_setMorphAmount(uint8_t scene_index, uint8_t amount)
{
    uint8_t before = scene_getEffectMorphAmount(scene_index);

    scene_setEffectMorphAmount(scene_index, amount);
    return (uint8_t)(scene_getEffectMorphAmount(scene_index) != before);
}
```

---

## 3. `Core/Menu/menuEffects.h` (new file)

```c
/*
 * Core/Menu/menuEffects.h
 *
 *  Created on: 28.09.2026
 * -----------------------------------------------------------------------------
 *  Copyright 2026 Brendan Clarke
 * -----------------------------------------------------------------------------
 *  This file is part of the LXR02 Open-Source software.
 * -----------------------------------------------------------------------------
 */
#ifndef MENU_EFFECTS_H_
#define MENU_EFFECTS_H_

#include <stdint.h>
#include "EffectsManager.h"

/*
 * Effect page (SHIFT+PERF) owner (Session 072 step 7; plan §13, A43).
 *
 * What: owns the Effect page's SELECT layout, per-SELECT screen memory,
 * cursor navigation, cell values and formatting, the `typ` click-in/turn/
 * click-out transaction, the momentary SHIFT Morph view, and type UI hook
 * dispatch. menu.c keeps its generic compact renderer, full edit view,
 * encoder, and endless-pot plumbing and delegates every Effect cell here
 * through MENU_CELL_EFFECT.
 * Why a module: the Effect page layout is registry-driven and type-dependent;
 * keeping it outside menu.c leaves the VOICE screen machinery untouched.
 * Context: foreground only (Menu/ButtonHandler). All retained writes go
 * through the EffectsManager edit API; this module never calls SceneData
 * Effect setters directly.
 * Affiliates: menu.c (EFFECT_PAGE branches), buttonHandler.c (SELECT_MODE_FX),
 * EffectsManager (registry, edit API, effects_changeType()).
 */

/* Cell classes on the Effect page. */
typedef enum {
    MENU_FX_CELL_NONE = 0,
    MENU_FX_CELL_TYPE,          /* `typ`: record type, encoder-only view     */
    MENU_FX_CELL_PARAM,         /* descriptor index 0..63                    */
    MENU_FX_CELL_RUN,           /* sequence run mode                         */
    MENU_FX_CELL_LENGTH,        /* sequence length 1..16                     */
    MENU_FX_CELL_SCALE,         /* sequence step scale (shared table index)  */
    MENU_FX_CELL_MORPH_AMOUNT   /* Scene Effect Morph amount (`mrp`)         */
} menuEffects_cell_kind_t;

/*
 * One resolved Effect cell. descriptor is set only for MENU_FX_CELL_PARAM and
 * points at the registry row's ParamDescriptor, so menu.c's generic dtype,
 * name, and full-view code can read it directly.
 */
typedef struct {
    uint8_t kind;
    uint8_t index;
    const ParamDescriptor *descriptor;
} menuEffects_cell_t;

/* menuEffects_service() action bits consumed by menu.c. */
#define MENU_FX_ACT_REPAINT    0x01u
#define MENU_FX_ACT_REPAIR     0x02u
#define MENU_FX_ACT_EXIT_EDIT  0x04u

/* Page lifecycle and cursor (menuIndex = subPage << 3 | column). */
void menuEffects_enter(uint8_t *sub_page, uint8_t *column);
void menuEffects_toggleFirstScreen(uint8_t *sub_page, uint8_t *column);
void menuEffects_leave(void);
uint8_t menuEffects_resolveCell(uint8_t sub_page, uint8_t column,
                                menuEffects_cell_t *out);
uint8_t menuEffects_scrollSign(uint8_t sub_page);
uint8_t menuEffects_move(int8_t inc, uint8_t *sub_page, uint8_t *column);
uint8_t menuEffects_selectPressed(uint8_t button, uint8_t *sub_page,
                                  uint8_t *column);
void menuEffects_repairCursor(uint8_t *sub_page, uint8_t *column);

/* Cell values, formatting, clamping, and commits. */
uint8_t menuEffects_cellDtype(const menuEffects_cell_t *cell);
uint16_t menuEffects_cellValue(const menuEffects_cell_t *cell);
void menuEffects_clampValue(const menuEffects_cell_t *cell, uint16_t *value);
uint8_t menuEffects_cellCommit(const menuEffects_cell_t *cell, uint16_t value);
uint8_t menuEffects_formatValue3(const menuEffects_cell_t *cell, char *dst);
void menuEffects_shortName(const menuEffects_cell_t *cell, char *dst);
uint8_t menuEffects_paintEditView(const menuEffects_cell_t *cell);
uint8_t menuEffects_cellWantsDoublePot(const menuEffects_cell_t *cell);

/* `typ` transaction and SHIFT Morph view. */
uint8_t menuEffects_editModeChanged(uint8_t edit_active,
                                    const menuEffects_cell_t *cell);
void menuEffects_typeBrowse(int8_t inc);
void menuEffects_setShowMorph(uint8_t on);
uint8_t menuEffects_showMorph(void);

/* Foreground service and type UI hooks. */
uint8_t menuEffects_service(void);
uint8_t menuEffects_hookSelect(uint8_t button, uint8_t shift, uint8_t pressed);
uint8_t menuEffects_hookTrack(uint8_t track, uint8_t shift, uint8_t pressed);
uint8_t menuEffects_hookBar(uint8_t bar, uint8_t shift, uint8_t pressed);
void menuEffects_renderLeds(void);

#endif /* MENU_EFFECTS_H_ */
```

---

## 4. `Core/Menu/menuEffects.c` (new file)

### 4.1 Header, state, and layout

```c
/*
 * Core/Menu/menuEffects.c
 *
 *  Created on: 28.09.2026
 * -----------------------------------------------------------------------------
 *  Copyright 2026 Brendan Clarke
 * -----------------------------------------------------------------------------
 *  This file is part of the LXR02 Open-Source software.
 * -----------------------------------------------------------------------------
 *
 * Effect page implementation (contract in menuEffects.h).
 *
 * Layout (plan §13.2, A39):
 *   SELECT 1  screen 0: typ out vol pan    screen 1: run len scl mrp
 *   SELECT 2..8: type-specific rows 3..63 (N <= 61), 4 per screen, S =
 *     ceil(N / 28) screens per button (1..3), filled button by button.
 *   A type with select_layout overrides SELECT 2..8 (up to 4 screens each;
 *   cells hold descriptor indices, EFFECT_LANE_NONE = empty). SELECT 1 is
 *   always manager-owned so `typ` stays reachable.
 * Navigation: the encoder walks every populated cell linearly across
 * screens AND SELECT buttons, without wrapping; SELECT n lands on its first
 * screen, and a re-press cycles its screens (S072_ST7 D1).
 */
#include "menuEffects.h"
#include "menu.h"
#include "MenuText.h"
#include "SceneData.h"
#include "ledHandler.h"
#include <string.h>

#define MENU_FX_SELECT_COUNT     8u
#define MENU_FX_SCREENS_MAX      4u
#define MENU_FX_CELLS            4u
#define MENU_FX_ROWS_PER_SCREEN_SET 28u   /* 7 buttons x 4 cells */

/*
 * Effect page state (14 B SRAM1; plan §16.1 item 8).
 *
 * screen[]: remembered screen per SELECT button, like VOICE's but separate
 * so VOICE memory is never disturbed. show_morph: SHIFT held. type_edit:
 * the `typ` full view is open; type_candidate is the browsed id and
 * type_scene the Scene it belongs to (a Scene change discards it, §7.2).
 * last_scene/last_type: service change detection for repaint/relayout.
 */
static uint8_t menuEffects_screen[MENU_FX_SELECT_COUNT];
static uint8_t menuEffects_showMorphFlag;
static uint8_t menuEffects_typeEdit;
static effect_type_id_t menuEffects_typeCandidate;
static uint8_t menuEffects_typeScene;
static uint8_t menuEffects_lastScene;
static effect_type_id_t menuEffects_lastType;

/* Run-mode labels in effect_seq_run_mode_t order: compact / full view. */
static const char menuEffects_runShort[EFFECT_SEQ_RUN_MODE_COUNT][4] = {
    "fwd", "rev", "pip", "rnd", "sel"
};
static const char *const menuEffects_runLong[EFFECT_SEQ_RUN_MODE_COUNT] = {
    "forward", "reverse", "pingpong", "random", "select"
};

/*
 * Provisional step-scale labels (S072_ST7 D2), shared-table index order.
 * Step 8 moves these to the sequencer-owned shared scale table.
 */
static const char menuEffects_scaleShort[EFFECT_SEQ_SCALE_COUNT][4] = {
    "/64", "32t", "/32", "16t", "/16", "/8t", "16.",
    "/8 ", "/4t", "/8.", "/4 ", "/2 ", "1br", "2br"
};
static const char *const menuEffects_scaleLong[EFFECT_SEQ_SCALE_COUNT] = {
    "1/64", "1/32T", "1/32", "1/16T", "1/16", "1/8T", "1/16.",
    "1/8", "1/4T", "1/8.", "1/4", "1/2", "1 bar", "2 bars"
};

/* Active Scene's retained record and registry row (`off` fallback). */
static const effect_record_t *menuEffects_record(void)
{
    return scene_effectConst(scene_getActiveIndex());
}

static const effect_registry_entry_t *menuEffects_entry(void)
{
    const effect_record_t *record = menuEffects_record();
    const effect_registry_entry_t *entry =
        record ? effects_registryEntry(record->type) : NULL;

    return entry ? entry : effects_registryEntry(EFFECT_TYPE_OFF);
}

/* Type-specific row count N (indices 3..descriptor_count-1). */
static uint8_t menuEffects_typeRowCount(const effect_registry_entry_t *entry)
{
    return (entry->descriptor_count > EFFECT_COMMON_PARAM_COUNT)
        ? (uint8_t)(entry->descriptor_count - EFFECT_COMMON_PARAM_COUNT)
        : 0u;
}

/* Screens per SELECT 2..8 button in the default layout: ceil(N / 28). */
static uint8_t menuEffects_defaultScreensPerButton(uint8_t rows)
{
    return (uint8_t)((rows + MENU_FX_ROWS_PER_SCREEN_SET - 1u) /
                     MENU_FX_ROWS_PER_SCREEN_SET);
}

/*
 * Number of populated screens behind one SELECT button.
 *
 * Inputs: SELECT index 0..7 and the active type. Output: 2 for SELECT 1,
 * the custom layout's count, or the default-layout count (0 when the button
 * holds no rows, which makes it an ignored SELECT press).
 */
static uint8_t menuEffects_screenCount(uint8_t sub_page)
{
    const effect_registry_entry_t *entry = menuEffects_entry();
    uint8_t rows;
    uint8_t per_button;
    uint8_t first;
    uint8_t screens;

    if (sub_page >= MENU_FX_SELECT_COUNT)
        return 0u;
    if (sub_page == 0u)
        return 2u;
    if (entry->select_layout) {
        screens = entry->select_layout->screen_count[sub_page];
        return (screens > MENU_FX_SCREENS_MAX) ? MENU_FX_SCREENS_MAX : screens;
    }
    rows = menuEffects_typeRowCount(entry);
    per_button = menuEffects_defaultScreensPerButton(rows);
    first = (uint8_t)((sub_page - 1u) * per_button * MENU_FX_CELLS);
    if (per_button == 0u || first >= rows)
        return 0u;
    screens = (uint8_t)((rows - first + MENU_FX_CELLS - 1u) / MENU_FX_CELLS);
    return (screens > per_button) ? per_button : screens;
}

/*
 * Resolve one (SELECT, screen, column) coordinate to an Effect cell.
 *
 * Output: nonzero for a real cell. PARAM cells carry their descriptor so the
 * generic renderer can read dtype and names.
 */
static uint8_t menuEffects_cellAt(uint8_t sub_page, uint8_t screen,
                                  uint8_t column, menuEffects_cell_t *out)
{
    const effect_registry_entry_t *entry = menuEffects_entry();
    uint8_t index = EFFECT_LANE_NONE;

    memset(out, 0, sizeof(*out));
    if (sub_page >= MENU_FX_SELECT_COUNT || column >= MENU_FX_CELLS ||
        screen >= menuEffects_screenCount(sub_page))
        return 0u;
    if (sub_page == 0u) {
        static const uint8_t second[MENU_FX_CELLS] = {
            MENU_FX_CELL_RUN, MENU_FX_CELL_LENGTH,
            MENU_FX_CELL_SCALE, MENU_FX_CELL_MORPH_AMOUNT
        };
        if (screen == 1u) {
            out->kind = second[column];
            return 1u;
        }
        if (column == 0u) {
            out->kind = MENU_FX_CELL_TYPE;
            return 1u;
        }
        index = (uint8_t)(column - 1u);          /* out, vol, pan */
    } else if (entry->select_layout) {
        index = entry->select_layout->cells[sub_page][screen][column];
    } else {
        uint8_t rows = menuEffects_typeRowCount(entry);
        uint8_t ordinal = (uint8_t)(
            ((sub_page - 1u) * menuEffects_defaultScreensPerButton(rows) +
             screen) * MENU_FX_CELLS + column);
        if (ordinal < rows)
            index = (uint8_t)(EFFECT_COMMON_PARAM_COUNT + ordinal);
    }
    if (index == EFFECT_LANE_NONE || index >= entry->descriptor_count)
        return 0u;
    out->kind = MENU_FX_CELL_PARAM;
    out->index = index;
    out->descriptor = &entry->descriptors[index].base;
    return 1u;
}

/* First selectable column of one screen, or 0 when empty. */
static uint8_t menuEffects_firstColumn(uint8_t sub_page, uint8_t screen)
{
    menuEffects_cell_t cell;
    uint8_t column;

    for (column = 0u; column < MENU_FX_CELLS; column++) {
        if (menuEffects_cellAt(sub_page, screen, column, &cell))
            return column;
    }
    return 0u;
}
```

### 4.2 Cursor and navigation

```c
/*
 * Enter the Effect page (plan §13.2: entry always lands on SELECT 1 screen 1).
 *
 * Output: SELECT 1, screen 0, column 0 (`typ`); every other button's screen
 * memory resets because the page may be showing a different Scene/type than
 * last time. The Morph view follows the physical SHIFT key so SHIFT+PERF may
 * momentarily show Morph values, which the plan allows.
 */
void menuEffects_enter(uint8_t *sub_page, uint8_t *column)
{
    const effect_record_t *record = menuEffects_record();

    memset(menuEffects_screen, 0, sizeof(menuEffects_screen));
    menuEffects_typeEdit = 0u;
    menuEffects_lastScene = scene_getActiveIndex();
    menuEffects_lastType = record ? record->type : EFFECT_TYPE_OFF;
    *sub_page = 0u;
    *column = 0u;
}

/* Repeated SHIFT+PERF: toggle SELECT 1 between its two screens (§13.2). */
void menuEffects_toggleFirstScreen(uint8_t *sub_page, uint8_t *column)
{
    menuEffects_typeEdit = 0u;
    menuEffects_screen[0] = (uint8_t)(menuEffects_screen[0] ? 0u : 1u);
    *sub_page = 0u;
    *column = 0u;
}

/* Leaving the page discards an open `typ` candidate and the Morph view. */
void menuEffects_leave(void)
{
    menuEffects_typeEdit = 0u;
    menuEffects_showMorphFlag = 0u;
}

/* Resolve a visible column on the remembered screen of one SELECT button. */
uint8_t menuEffects_resolveCell(uint8_t sub_page, uint8_t column,
                                menuEffects_cell_t *out)
{
    if (sub_page >= MENU_FX_SELECT_COUNT)
        sub_page = 0u;
    return menuEffects_cellAt(sub_page, menuEffects_screen[sub_page],
                              (uint8_t)(column % MENU_FX_CELLS), out);
}

/*
 * Compact-view scroll marker for column 15, matching VOICE semantics:
 * '>' on the first of several screens, '*' in the middle, '<' on the last,
 * nothing for a single-screen button.
 */
uint8_t menuEffects_scrollSign(uint8_t sub_page)
{
    uint8_t count;
    uint8_t screen;

    if (sub_page >= MENU_FX_SELECT_COUNT)
        return 0u;
    count = menuEffects_screenCount(sub_page);
    screen = menuEffects_screen[sub_page];
    if (count <= 1u)
        return 0u;
    if (screen == 0u)
        return '>';
    return (screen + 1u < count) ? '*' : '<';
}

/*
 * Encoder navigation across every populated cell (plan §13.2).
 *
 * Inputs: direction and the current SELECT/column. Output: nonzero when the
 * cursor moved; the new SELECT's screen memory is updated so repaint and pots
 * follow. The walk crosses screens and SELECT buttons, skips empty cells and
 * empty buttons, and stops at either end (no wrap), like VOICE's encoder.
 */
uint8_t menuEffects_move(int8_t inc, uint8_t *sub_page, uint8_t *column)
{
    int8_t step = (inc > 0) ? 1 : -1;
    int8_t sp = (int8_t)((*sub_page < MENU_FX_SELECT_COUNT) ? *sub_page : 0u);
    int8_t screen = (int8_t)menuEffects_screen[sp];
    int8_t col = (int8_t)(*column % MENU_FX_CELLS);
    menuEffects_cell_t cell;

    for (;;) {
        col = (int8_t)(col + step);
        if (col >= (int8_t)MENU_FX_CELLS || col < 0) {
            col = (step > 0) ? 0 : (int8_t)(MENU_FX_CELLS - 1u);
            screen = (int8_t)(screen + step);
            while (screen < 0 ||
                   screen >= (int8_t)menuEffects_screenCount((uint8_t)sp)) {
                sp = (int8_t)(sp + step);
                if (sp < 0 || sp >= (int8_t)MENU_FX_SELECT_COUNT)
                    return 0u;                      /* end of page: stay */
                screen = (step > 0) ? 0 :
                    (int8_t)menuEffects_screenCount((uint8_t)sp) - 1;
            }
        }
        if (menuEffects_cellAt((uint8_t)sp, (uint8_t)screen, (uint8_t)col,
                               &cell)) {
            menuEffects_screen[sp] = (uint8_t)screen;
            *sub_page = (uint8_t)sp;
            *column = (uint8_t)col;
            return 1u;
        }
    }
}

/*
 * SELECT press (S072_ST7 D1).
 *
 * Output: nonzero when the button has screens. A different button lands on
 * its first screen; a re-press of the current button cycles its screens. An
 * empty button (fewer type rows) is ignored so the page never shows a blank
 * screen.
 */
uint8_t menuEffects_selectPressed(uint8_t button, uint8_t *sub_page,
                                  uint8_t *column)
{
    uint8_t count;

    if (button >= MENU_FX_SELECT_COUNT)
        return 0u;
    count = menuEffects_screenCount(button);
    if (count == 0u)
        return 0u;
    if (button == *sub_page)
        menuEffects_screen[button] =
            (uint8_t)((menuEffects_screen[button] + 1u) % count);
    else
        menuEffects_screen[button] = 0u;
    *sub_page = button;
    *column = menuEffects_firstColumn(button, menuEffects_screen[button]);
    return 1u;
}

/*
 * Repair the cursor after a type change or Scene switch changed the layout.
 *
 * Output: an empty SELECT falls back to SELECT 1; a remembered screen past
 * the new count resets to 0; an empty column moves to the first selectable
 * one.
 */
void menuEffects_repairCursor(uint8_t *sub_page, uint8_t *column)
{
    menuEffects_cell_t cell;
    uint8_t sp = (*sub_page < MENU_FX_SELECT_COUNT) ? *sub_page : 0u;
    uint8_t i;

    for (i = 0u; i < MENU_FX_SELECT_COUNT; i++) {
        if (menuEffects_screen[i] >= menuEffects_screenCount(i))
            menuEffects_screen[i] = 0u;
    }
    if (menuEffects_screenCount(sp) == 0u)
        sp = 0u;
    if (!menuEffects_cellAt(sp, menuEffects_screen[sp],
                            (uint8_t)(*column % MENU_FX_CELLS), &cell))
        *column = menuEffects_firstColumn(sp, menuEffects_screen[sp]);
    *sub_page = sp;
}
```

### 4.3 Values, formatting, and commits

```c
/*
 * Dtype for menu.c's generic formatter and clamp. PARAM cells use their
 * descriptor; `len` and `mrp` use existing numeric dtypes; `typ`, `run`, and
 * `scl` are always formatted here (menuEffects_formatValue3/paintEditView),
 * so their dtype only needs to be a harmless numeric one.
 */
uint8_t menuEffects_cellDtype(const menuEffects_cell_t *cell)
{
    if (!cell)
        return DTYPE_0B127;
    switch (cell->kind) {
    case MENU_FX_CELL_PARAM:
        return cell->descriptor ? cell->descriptor->dtype : DTYPE_0B127;
    case MENU_FX_CELL_LENGTH:
        return DTYPE_1B16;
    case MENU_FX_CELL_MORPH_AMOUNT:
        return DTYPE_0B255;
    default:
        return DTYPE_0B127;
    }
}

/*
 * Displayed value of one cell.
 *
 * PARAM cells show the Morph endpoint while SHIFT is held and the row is
 * Morphable (plan §13.5); every other cell shows its single value. `typ`
 * shows the browsed candidate while its full view is open.
 */
uint16_t menuEffects_cellValue(const menuEffects_cell_t *cell)
{
    const effect_record_t *record = menuEffects_record();
    uint8_t scene_index = scene_getActiveIndex();

    if (!cell || !record)
        return 0u;
    switch (cell->kind) {
    case MENU_FX_CELL_TYPE:
        return menuEffects_typeEdit ? menuEffects_typeCandidate
                                    : record->type;
    case MENU_FX_CELL_PARAM:
        return effects_getParameter(
            scene_index, cell->index,
            menuEffects_showMorphFlag ? EFFECT_IMAGE_MORPH
                                      : EFFECT_IMAGE_NORMAL);
    case MENU_FX_CELL_RUN:
        return record->seq_run_mode;
    case MENU_FX_CELL_LENGTH:
        return record->seq_length;
    case MENU_FX_CELL_SCALE:
        return record->seq_step_scale;
    case MENU_FX_CELL_MORPH_AMOUNT:
        return scene_getEffectMorphAmount(scene_index);
    default:
        return 0u;
    }
}

/* Clamp an edited value to the cell's retained domain. */
void menuEffects_clampValue(const menuEffects_cell_t *cell, uint16_t *value)
{
    uint16_t max = 127u;
    uint16_t min = 0u;

    if (!cell || !value)
        return;
    switch (cell->kind) {
    case MENU_FX_CELL_TYPE:
        max = (uint16_t)(effects_registryCount() - 1u);
        break;
    case MENU_FX_CELL_PARAM: {
        const effect_record_t *record = menuEffects_record();
        const effect_param_descriptor_t *descriptor = record
            ? effects_descriptor(record->type, cell->index) : NULL;
        max = descriptor ? descriptor->max_value : 0u;
        break; }
    case MENU_FX_CELL_RUN:
        max = (uint16_t)(EFFECT_SEQ_RUN_MODE_COUNT - 1u);
        break;
    case MENU_FX_CELL_LENGTH:
        min = EFFECT_SEQ_LENGTH_MIN;
        max = EFFECT_SEQ_LENGTH_MAX;
        break;
    case MENU_FX_CELL_SCALE:
        max = (uint16_t)(EFFECT_SEQ_SCALE_COUNT - 1u);
        break;
    case MENU_FX_CELL_MORPH_AMOUNT:
        max = 255u;
        break;
    default:
        max = 0u;
        break;
    }
    if (*value < min)
        *value = min;
    if (*value > max)
        *value = max;
}

/*
 * Commit one edited value through the EffectsManager edit API.
 *
 * Output: nonzero when retained data changed (menu.c then repaints). `typ` is
 * never committed here: only the click-out transaction changes the type
 * (plan §13.3), so pots and generic encoder commits cannot reach it.
 */
uint8_t menuEffects_cellCommit(const menuEffects_cell_t *cell, uint16_t value)
{
    uint8_t scene_index = scene_getActiveIndex();

    if (!cell)
        return 0u;
    switch (cell->kind) {
    case MENU_FX_CELL_PARAM:
        return effects_setParameter(
            scene_index, cell->index,
            menuEffects_showMorphFlag ? EFFECT_IMAGE_MORPH
                                      : EFFECT_IMAGE_NORMAL,
            (uint8_t)value);
    case MENU_FX_CELL_RUN:
        return effects_setSeqRunMode(scene_index, (uint8_t)value);
    case MENU_FX_CELL_LENGTH:
        return effects_setSeqLength(scene_index, (uint8_t)value);
    case MENU_FX_CELL_SCALE:
        return effects_setSeqStepScale(scene_index, (uint8_t)value);
    case MENU_FX_CELL_MORPH_AMOUNT:
        return effects_setMorphAmount(scene_index, (uint8_t)value);
    default:
        return 0u;
    }
}

/*
 * Compact three-character value for the manager-owned text cells.
 *
 * Output: nonzero when this function wrote dst (`typ` token, `run` token,
 * `scl` label); zero lets menu.c's generic dtype formatter handle numeric
 * and descriptor cells, so Effect rows reuse the exact voice formatting.
 */
uint8_t menuEffects_formatValue3(const menuEffects_cell_t *cell, char *dst)
{
    uint16_t value = menuEffects_cellValue(cell);

    if (!cell || !dst)
        return 0u;
    if (cell->kind == MENU_FX_CELL_TYPE) {
        const char *token = effects_typeToken((effect_type_id_t)value);
        memcpy(dst, token ? token : "---", 3);
        return 1u;
    }
    if (cell->kind == MENU_FX_CELL_RUN && value < EFFECT_SEQ_RUN_MODE_COUNT) {
        memcpy(dst, menuEffects_runShort[value], 3);
        return 1u;
    }
    if (cell->kind == MENU_FX_CELL_SCALE && value < EFFECT_SEQ_SCALE_COUNT) {
        memcpy(dst, menuEffects_scaleShort[value], 3);
        return 1u;
    }
    return 0u;
}

/* Compact three-character label for one cell (top LCD row). */
void menuEffects_shortName(const menuEffects_cell_t *cell, char *dst)
{
    const char *text = "   ";
    uint8_t i;

    if (!cell || !dst)
        return;
    switch (cell->kind) {
    case MENU_FX_CELL_TYPE:         text = "typ"; break;
    case MENU_FX_CELL_RUN:          text = "run"; break;
    case MENU_FX_CELL_LENGTH:       text = "len"; break;
    case MENU_FX_CELL_SCALE:        text = "scl"; break;
    case MENU_FX_CELL_MORPH_AMOUNT: text = "mrp"; break;
    case MENU_FX_CELL_PARAM:
        if (cell->descriptor && cell->descriptor->short_name)
            text = cell->descriptor->short_name;
        break;
    default:
        break;
    }
    for (i = 0u; i < 3u; i++)
        dst[i] = (text[i] != '\0') ? text[i] : ' ';
}

/* Copy up to width printable characters, space-padded. */
static void menuEffects_copyField(char *dst, const char *src, uint8_t width)
{
    uint8_t i;

    for (i = 0u; i < width; i++)
        dst[i] = (src && src[i] != '\0') ? src[i] : ' ';
}

/*
 * Full (clicked-in) view for the manager-owned cells.
 *
 * Output: nonzero when both LCD rows were painted here; zero leaves PARAM
 * cells to menu.c's generic descriptor view (category + long name, dtype
 * value). `typ` shows the candidate's full 8-character name, its token, and
 * '*' at column 11 while the candidate differs from the committed type, so
 * the user can see that click-out will change the type.
 */
uint8_t menuEffects_paintEditView(const menuEffects_cell_t *cell)
{
    const effect_record_t *record = menuEffects_record();
    uint16_t value = menuEffects_cellValue(cell);

    if (!cell || !record || cell->kind == MENU_FX_CELL_PARAM ||
        cell->kind == MENU_FX_CELL_NONE)
        return 0u;
    memset(&editDisplayBuffer[0][0], ' ', 16);
    memset(&editDisplayBuffer[1][0], ' ', 16);
    switch (cell->kind) {
    case MENU_FX_CELL_TYPE: {
        const effect_registry_entry_t *entry =
            effects_registryEntry((effect_type_id_t)value);
        menuEffects_copyField(&editDisplayBuffer[0][0], "Effect", 8u);
        menuEffects_copyField(&editDisplayBuffer[0][8], "Type", 8u);
        menuEffects_copyField(&editDisplayBuffer[1][0],
                              entry ? entry->full8 : "?", 8u);
        if (menuEffects_typeEdit && value != record->type)
            editDisplayBuffer[1][11] = '*';
        (void)menuEffects_formatValue3(cell, &editDisplayBuffer[1][13]);
        break; }
    case MENU_FX_CELL_RUN:
        menuEffects_copyField(&editDisplayBuffer[0][0], "FX Seq", 8u);
        menuEffects_copyField(&editDisplayBuffer[0][8], "RunMode", 8u);
        if (value < EFFECT_SEQ_RUN_MODE_COUNT)
            menuEffects_copyField(&editDisplayBuffer[1][0],
                                  menuEffects_runLong[value], 8u);
        (void)menuEffects_formatValue3(cell, &editDisplayBuffer[1][13]);
        break;
    case MENU_FX_CELL_LENGTH:
        menuEffects_copyField(&editDisplayBuffer[0][0], "FX Seq", 8u);
        menuEffects_copyField(&editDisplayBuffer[0][8], "Length", 8u);
        numtostrpu(&editDisplayBuffer[1][13], (uint8_t)value, ' ');
        break;
    case MENU_FX_CELL_SCALE:
        menuEffects_copyField(&editDisplayBuffer[0][0], "FX Seq", 8u);
        menuEffects_copyField(&editDisplayBuffer[0][8], "StepScal", 8u);
        if (value < EFFECT_SEQ_SCALE_COUNT)
            menuEffects_copyField(&editDisplayBuffer[1][0],
                                  menuEffects_scaleLong[value], 8u);
        (void)menuEffects_formatValue3(cell, &editDisplayBuffer[1][13]);
        break;
    case MENU_FX_CELL_MORPH_AMOUNT:
        menuEffects_copyField(&editDisplayBuffer[0][0], "Effect", 8u);
        menuEffects_copyField(&editDisplayBuffer[0][8], "Morph", 8u);
        numtostrpu(&editDisplayBuffer[1][13], (uint8_t)value, ' ');
        break;
    default:
        return 0u;
    }
    return 1u;
}

/* 0..255 cells use the double-rate endless-pot scale, like PERF Morph. */
uint8_t menuEffects_cellWantsDoublePot(const menuEffects_cell_t *cell)
{
    const effect_param_descriptor_t *descriptor;

    if (!cell)
        return 0u;
    if (cell->kind == MENU_FX_CELL_MORPH_AMOUNT)
        return 1u;
    if (cell->kind != MENU_FX_CELL_PARAM)
        return 0u;
    descriptor = menuEffects_record()
        ? effects_descriptor(menuEffects_record()->type, cell->index) : NULL;
    return (uint8_t)(descriptor && descriptor->max_value > 127u);
}
```

### 4.4 The `typ` transaction, Morph view, service, and hooks

```c
/*
 * Encoder click-in / click-out on the Effect page (plan §7.2, §13.3).
 *
 * Inputs: menu.c's new editModeActive value and the focused cell. Click-in
 * on `typ` opens browsing with the committed type as the candidate and
 * records the owning Scene. Click-out commits only when the candidate differs
 * and the active Scene is still the one it was browsed on; the whole §7.2
 * transaction (handoff, defaults, sequence clear, init, AutoSave) runs inside
 * effects_changeType() before the repaint. Output: nonzero when the page
 * layout changed, so menu.c must repair the cursor and repaint everything.
 */
uint8_t menuEffects_editModeChanged(uint8_t edit_active,
                                    const menuEffects_cell_t *cell)
{
    const effect_record_t *record = menuEffects_record();
    uint8_t scene_index = scene_getActiveIndex();
    uint8_t i;

    if (edit_active) {
        if (cell && cell->kind == MENU_FX_CELL_TYPE && record) {
            menuEffects_typeEdit = 1u;
            menuEffects_typeCandidate = record->type;
            menuEffects_typeScene = scene_index;
        }
        return 0u;
    }
    if (!menuEffects_typeEdit)
        return 0u;
    menuEffects_typeEdit = 0u;
    if (!record || scene_index != menuEffects_typeScene ||
        menuEffects_typeCandidate == record->type)
        return 0u;
    if (!effects_changeType(scene_index, menuEffects_typeCandidate))
        return 0u;
    /* New type: SELECT 2..8 hold different rows; SELECT 1 keeps its screen. */
    for (i = 1u; i < MENU_FX_SELECT_COUNT; i++)
        menuEffects_screen[i] = 0u;
    menuEffects_lastType = menuEffects_typeCandidate;
    return 1u;
}

/* Turn inside the `typ` view: browse candidate ids without wrapping. */
void menuEffects_typeBrowse(int8_t inc)
{
    uint8_t count = effects_registryCount();

    if (!menuEffects_typeEdit || count == 0u)
        return;
    if (inc > 0 && menuEffects_typeCandidate + 1u < count)
        menuEffects_typeCandidate++;
    else if (inc < 0 && menuEffects_typeCandidate > 0u)
        menuEffects_typeCandidate--;
}

/* Momentary SHIFT Morph view (plan §13.5, A42). */
void menuEffects_setShowMorph(uint8_t on)
{
    menuEffects_showMorphFlag = (uint8_t)(on != 0u);
}

uint8_t menuEffects_showMorph(void)
{
    return menuEffects_showMorphFlag;
}

/*
 * Foreground change detection while the page is visible.
 *
 * Output: MENU_FX_ACT_* bits. A Scene switch or a type change from outside
 * the page (Scene/Bank Load, boot restore, dev hook) repaints and repairs the
 * cursor; an open `typ` view is discarded because its candidate belonged to
 * the previous Scene (plan §7.2). Cost: two byte compares per call.
 */
uint8_t menuEffects_service(void)
{
    const effect_record_t *record = menuEffects_record();
    uint8_t scene_index = scene_getActiveIndex();
    effect_type_id_t type = record ? record->type : EFFECT_TYPE_OFF;
    uint8_t actions = 0u;

    if (scene_index != menuEffects_lastScene ||
        type != menuEffects_lastType) {
        menuEffects_lastScene = scene_index;
        menuEffects_lastType = type;
        actions = (uint8_t)(MENU_FX_ACT_REPAINT | MENU_FX_ACT_REPAIR);
        if (menuEffects_typeEdit) {
            menuEffects_typeEdit = 0u;
            actions |= MENU_FX_ACT_EXIT_EDIT;
        }
    }
    return actions;
}

/* Active type's hook table, or NULL. */
static const effect_ui_hooks_t *menuEffects_hooks(void)
{
    return menuEffects_entry()->ui;
}

/* §13.6 gesture hooks: nonzero = the type handled it. */
uint8_t menuEffects_hookSelect(uint8_t button, uint8_t shift, uint8_t pressed)
{
    const effect_ui_hooks_t *hooks = menuEffects_hooks();
    return (hooks && hooks->select) ? hooks->select(button, shift, pressed)
                                    : 0u;
}

uint8_t menuEffects_hookTrack(uint8_t track, uint8_t shift, uint8_t pressed)
{
    const effect_ui_hooks_t *hooks = menuEffects_hooks();
    return (hooks && hooks->track) ? hooks->track(track, shift, pressed) : 0u;
}

uint8_t menuEffects_hookBar(uint8_t bar, uint8_t shift, uint8_t pressed)
{
    const effect_ui_hooks_t *hooks = menuEffects_hooks();
    return (hooks && hooks->bar) ? hooks->bar(bar, shift, pressed) : 0u;
}

/* Let the type add LEDs after the page painted its own (§13.6). */
void menuEffects_renderLeds(void)
{
    const effect_ui_hooks_t *hooks = menuEffects_hooks();
    if (hooks && hooks->render_leds)
        hooks->render_leds();
}
```

**Notes on §4:**

- `menuEffects_move()` terminates. Each outer step advances `col` or
  `screen`/`sp` monotonically, and `sp` leaving 0..7 returns. SELECT 1 always
  has cells, so a walk from any valid position finds one within at most
  8 × 4 × 4 iterations.
- `numtostrpu()` and `editDisplayBuffer` are the existing `menu.h` exports.
  The file includes `menu.h` for them and for the `DTYPE_*` enum.

---

## 5. Build and config

### 5.1 `Makefile` — after line 89 (`Core/Menu/menu.c \`)

```make
  Core/Menu/menuEffects.c \
```

It needs no new `-I` path: `Core/Menu` and `Core/DSP/Effects` are already on
the list.

### 5.2 `config.h` — after line 256 (`#define DEV_EFFECT_FORCE_TYPE 0u`)

```c
/*
 * ENABLE_EUKLID_PAGE — keep the retired Euklid/rotation UI compiled out.
 *
 * What: SHIFT+PERF (select mode 5) is the Effect page from Session 072
 * step 7 (plan §13.1). The Euklid generator page, its SHIFT paths, and the
 * PATTERN_SETTINGS_PAGE/rotation entry are compiled out behind this switch;
 * their source, page tables, and EuklidGenerator stay in the tree. Setting it
 * to 1 is not supported without first giving Euklid a different select mode,
 * because mode 5 now belongs to SELECT_MODE_FX.
 */
#define ENABLE_EUKLID_PAGE 0
```

### 5.3 `config.h` lines 248–255 — `DEV_EFFECT_FORCE_TYPE` comment (modify)

Replace the last sentence ("It exists so Steps 4–5 can exercise … before the
Step 7 Effect page supplies a user path.") with:

"It exists so Steps 4–5 could exercise registry, AutoSave token, and DSP
initialization before a user path existed. From Step 7 the Effect page's `typ`
view is the normal path; the hook is retained for bench diagnostics."

---

## 6. `menu.h` and `menuPages.h`

### 6.1 `menu.h` lines 76–77 — append the page before `NUM_PAGES`

```c
    SOM_PAGE,
    /*
     * Effect page (SHIFT+PERF, Session 072 step 7). Cells are resolved by
     * menuEffects.c, not by menuPages[][]; the table row exists only so the
     * page-indexed array stays dense. Appended so earlier page ids keep their
     * values.
     */
    EFFECT_PAGE,
    NUM_PAGES
```

### 6.2 `menu.h` — after the `menu_setVoiceModeShowMorph()` prototype (line ~407), add

```c
/*
 * Effect-page SHIFT Morph view (Session 072 step 7; plan §13.5).
 *
 * Input: SHIFT held (1) or released (0) while SELECT_MODE_FX is active.
 * Output: Morphable Effect cells display/edit their Morph endpoint while on;
 * endless-pot snapshots refresh and the page repaints. Clients: buttonHandler
 * SHIFT press/release in FX mode. Separate from voiceModeShowMorph so VOICE's
 * persistent Morph mode is never affected.
 */
void menu_setEffectShowMorph(uint8_t onOff);
```

### 6.3 `menuPages.h` — before the closing `};` at line 137 (after the SOM_PAGE block), add

```c
/* EFFECT_PAGE — cells resolved by menuEffects.c (Session 072 step 7). */ { {0} },
```

---

## 7. `menu.c`

### 7.1 Includes, lines 30–61 — after `#include "SomGenerator.h"` (line 56)

```c
#include "menuEffects.h"   /* EFFECT_PAGE cells, cursor, typ transaction */
```

### 7.2 Cell kind and cell struct, lines 1639–1676 (modify)

- **`menu_cell_kind_t`:** after `MENU_CELL_SCENE_SETTING` (line 1644) add
  `,` and:

```c
    /*
     * Effect page cell (Session 072 step 7). Value, dtype, format, clamp,
     * and commit delegate to menuEffects.c through cell.fx; PARAM cells
     * also set cell.descriptor so the generic name/dtype code applies.
     */
    MENU_CELL_EFFECT
```

- **`menu_cell_t`:** after `const ParamDescriptor *descriptor;` (line 1675)
  add:

```c
    /* Effect page cell identity; meaningful only for MENU_CELL_EFFECT. */
    menuEffects_cell_t fx;
```

### 7.3 After `menu_voicePageToSlot()` (line ~1785), add

```c
/*
 * Pages that use the four-cell compact screen model (Session 072 step 7).
 *
 * Output: nonzero for VOICE1..7 and EFFECT_PAGE. Used only where the old
 * static-page "second half" arithmetic must be disabled (compact columns
 * always map to positions 0..3 of the current screen). VOICE-only features
 * (held-step overlay, Scene settings, instrument screens) keep testing
 * menu_isVoicePage().
 */
static uint8_t menu_isScreenPage(uint8_t page)
{
    return (uint8_t)(menu_isVoicePage(page) || page == EFFECT_PAGE);
}
```

Also add its prototype beside `menu_isVoicePage`'s (line 1693).

### 7.4 `menu_resolveCell()`, line 2921 — insert at the top of the body

```c
    if (menu_activePage == EFFECT_PAGE) {
        menu_cell_t cell;

        /*
         * Effect page cells come from menuEffects' registry-driven layout on
         * the remembered screen of this SELECT button (Session 072 step 7).
         */
        memset(&cell, 0, sizeof(cell));
        cell.kind = MENU_CELL_EMPTY;
        cell.static_param = PAR_NONE;
        cell.text_id = TEXT_EMPTY;
        cell.descriptor_index = INSTRUMENT_MENU_EMPTY;
        if (menuEffects_resolveCell(subPage, position, &cell.fx)) {
            cell.kind = MENU_CELL_EFFECT;
            cell.descriptor = cell.fx.descriptor;
        }
        return cell;
    }
```

### 7.5 Cell dispatch functions (add one branch at the top of each)

| Function (line) | Insert |
|---|---|
| `menu_cellDtype()` (3157) | `if (cell && cell->kind == MENU_CELL_EFFECT) return menuEffects_cellDtype(&cell->fx);` |
| `menu_cellDisplayValue()` (3178) | `if (cell && cell->kind == MENU_CELL_EFFECT) return menuEffects_cellValue(&cell->fx);` |
| `menu_cellCommitValue()` (3244) | `if (cell && cell->kind == MENU_CELL_EFFECT) return menuEffects_cellCommit(&cell->fx, value);` |
| `menu_formatCellValue3()` (3883), before its locals are computed | `if (cell && cell->kind == MENU_CELL_EFFECT && menuEffects_formatValue3(&cell->fx, valueAsText)) return;` |
| `menu_clampCellValue()` (3987) | `if (cell && cell->kind == MENU_CELL_EFFECT) { menuEffects_clampValue(&cell->fx, value); return; }` |

Precede each with the comment:
`/* Effect page cells are owned by menuEffects.c (Session 072 step 7). */`

- **`menu_formatCellValue3`:** its `dtype`/`raw` initializers call
  `menu_cellDtype()`/`menu_cellDisplayValue()`, which is harmless before the
  early return. The branch can therefore sit after the declarations, provided
  it comes before the first use.
- A zero return from `menuEffects_formatValue3()` falls through to the
  generic dtype switch, which formats PARAM (pan `PM63`, filter type
  `MENU_FILTER`, …), `len` and `mrp` exactly like voice cells.

### 7.6 `checkScrollSign()`, line 7656 — insert before the voice branch (line 7660)

```c
    /* Effect page: per-SELECT screen marker from menuEffects (step 7). */
    if (menu_activePage == EFFECT_PAGE)
        return menuEffects_scrollSign(activePage);
```

### 7.7 `menu_repaintGeneric()` (modify)

**Edit view** (clicked-in), immediately after `if (menu_cellIsEmpty(&cell)) return;`
(~9237), add:

```c
        /*
         * Effect page manager cells (typ/run/len/scl/mrp) paint their own
         * full view; PARAM cells fall through to the descriptor view below.
         */
        if (cell.kind == MENU_CELL_EFFECT &&
            menuEffects_paintEditView(&cell.fx))
            return;
```

**Edit-view names**, lines 9307–9308: extend the condition to
`cell.kind == MENU_CELL_INSTRUMENT || cell.kind == MENU_CELL_KIT_SETTING ||
cell.kind == MENU_CELL_EFFECT`. Effect PARAM cells then show
`descriptor->category` / `long_name` (for example `Filter  Frequncy`) and use
the same dtype value rendering.

**Compact view**, line 9401: `menu_isVoicePage(menu_activePage)` →
`menu_isScreenPage(menu_activePage)`.

**Compact names**, before `} else if (cell.kind == MENU_CELL_INSTRUMENT ||`
(line 9413), insert:

```c
            } else if (cell.kind == MENU_CELL_EFFECT) {
                menuEffects_shortName(&cell.fx,
                                      &editDisplayBuffer[0][4u * i]);
```

### 7.8 `menu_encoderChangeParameter()`, after the empty/CPU checks (~line 9510), add

```c
    /*
     * `typ` changes only through its full view: turning browses candidates,
     * and click-out commits (plan §13.3). No generic value commit may run.
     */
    if (cell.kind == MENU_CELL_EFFECT && cell.fx.kind == MENU_FX_CELL_TYPE) {
        menuEffects_typeBrowse(inc);
        return;
    }
```

### 7.9 `menu_moveToMenuItem()`, before the voice branch at line 9627, add

```c
    if (menu_activePage == EFFECT_PAGE) {
        /*
         * Effect page: linear walk across screens and SELECT buttons, with the
         * SELECT LED following the cursor (plan §13.2). menuEffects owns the
         * screen memory; menuIndex keeps its subPage<<3 | column form.
         */
        uint8_t sub_page = (uint8_t)activePage;
        uint8_t column = (uint8_t)activeParameter;

        if (menuEffects_move(inc, &sub_page, &column)) {
            menuIndex = (uint8_t)((sub_page << PAGE_SHIFT) | column);
            led_setActiveSelectButton(sub_page);
        }
        return;
    }
```

### 7.10 `menu_parseEncoder()`, after line 10459 (`editModeActive = …` toggle), add

```c
    if (btnClicked && menu_activePage == EFFECT_PAGE) {
        /*
         * Encoder click on the Effect page (Session 072 step 7). Click-in on
         * `typ` opens candidate browsing; click-out commits the §7.2 type
         * change when the candidate differs. A committed change alters the
         * SELECT 2..8 layout, so the cursor and pot mapping are repaired
         * before the full repaint below.
         */
        menu_cell_t fx_cell = menu_resolveCell(menu_getSubPage(),
                                               menuIndex & MASK_PARAMETER);
        if (menuEffects_editModeChanged(
                editModeActive,
                fx_cell.kind == MENU_CELL_EFFECT ? &fx_cell.fx : NULL)) {
            menu_resetActiveParameter();
            menu_endlessPotMappingChanged();
        }
    }
```

### 7.11 Compact-column mapping (modify)

In `menu_paramVisible` (10536), `menu_updateEndlessPotScales` (10583) and
`menu_parseKnobDelta` (10641), change
`menu_isVoicePage(menu_activePage) ? 0u : …` to
`menu_isScreenPage(menu_activePage) ? 0u : …`.

### 7.12 Endless pots (modify/add)

**`menu_updateEndlessPotScales()`**, lines 10590–10593: after `useDouble = …;`
add:

```c
            /* Effect 0..255 cells (mrp, 8-bit rows) use the double rate. */
            if (cell.kind == MENU_CELL_EFFECT)
                useDouble = menuEffects_cellWantsDoublePot(&cell.fx);
```

**`menu_parseKnobDelta()`**, after `if (menu_cellIsEmpty(&cell)) return;` (line
10648), add:

```c
    /* The endless pot over `typ` does nothing (plan §13.3). */
    if (cell.kind == MENU_CELL_EFFECT && cell.fx.kind == MENU_FX_CELL_TYPE)
        return;
```

### 7.13 `menu_serviceRuntimeWidgets()`, before `menu_sceneLiveRefreshService();` (line 10777), add

```c
    if (menu_activePage == EFFECT_PAGE) {
        /*
         * Effect page follows Scene switches and external type changes
         * (Session 072 step 7): an open `typ` view is closed, the cursor is
         * repaired for the new layout, and the page repaints.
         */
        uint8_t fx_actions = menuEffects_service();

        if (fx_actions & MENU_FX_ACT_EXIT_EDIT)
            editModeActive = 0u;
        if (fx_actions & MENU_FX_ACT_REPAIR) {
            menu_resetActiveParameter();
            menu_endlessPotMappingChanged();
        }
        if (fx_actions & MENU_FX_ACT_REPAINT)
            menu_repaintAll();
    }
```

### 7.14 `menu_switchSubPage()`, line 11694 — insert before the voice branch

```c
    if (menu_activePage == EFFECT_PAGE) {
        /*
         * Effect SELECT press (plan §13.2, S072_ST7 D1): a different button
         * lands on its first screen, a re-press cycles its screens, and an
         * empty button is ignored. ButtonHandler lights the SELECT LED from
         * menu_getSubPage() afterwards.
         */
        uint8_t sub_page = activePage;
        uint8_t column = activeParameter;

        if (menuEffects_selectPressed(subPageNr, &sub_page, &column))
            menuIndex = (uint8_t)((sub_page << PAGE_SHIFT) | column);
        menu_endlessPotMappingChanged();
        return;
    }
```

### 7.15 `menu_resetActiveParameter()`, line 11778 — insert before the voice branch

```c
    if (menu_activePage == EFFECT_PAGE) {
        /* Repair the Effect cursor after layout changes (step 7). */
        uint8_t sub_page = activePage;
        uint8_t column = menuIndex & MASK_PARAMETER;

        menuEffects_repairCursor(&sub_page, &column);
        menuIndex = (uint8_t)((sub_page << PAGE_SHIFT) | column);
        led_setActiveSelectButton(sub_page);
        return;
    }
```

### 7.16 `menu_switchPage()` (modify/add)

**(a)** After `if (was_voice_page && !menu_isVoicePage(pageNr)) va_resetOverlay();`
(line 11845), add:

```c
    /* Leaving the Effect page discards an open typ candidate and Morph view. */
    if (menu_activePage == EFFECT_PAGE && pageNr != EFFECT_PAGE)
        menuEffects_leave();
```

**(b)** Before `case PERFORMANCE_PAGE:` (line 11936), add:

```c
    case EFFECT_PAGE: {
        /*
         * SHIFT+PERF Effect page (Session 072 step 7; plan §13.1-§13.2).
         *
         * First entry lands on SELECT 1 screen 1 (`typ out vol pan`); a
         * repeated SHIFT+PERF while already here toggles SELECT 1 between
         * its two screens. Any open typ candidate is discarded (only an
         * encoder click-out commits). VOICE Morph mode never carries over.
         */
        uint8_t sub_page;
        uint8_t column;

        menu_instrumentLoadActive = 0u;
        menu_setVoiceModeShowMorph(0u);
        if (menu_activePage == EFFECT_PAGE)
            menuEffects_toggleFirstScreen(&sub_page, &column);
        else
            menuEffects_enter(&sub_page, &column);
        menuEffects_setShowMorph(buttonHandler_getShift());
        menu_activePage = EFFECT_PAGE;
        editModeActive = 0;
        lockPotentiometerFetch();
        menuIndex = (uint8_t)((sub_page << PAGE_SHIFT) | column);
        break; }
```

**(c)** The LED updates, line 12012: extend the PERF branch:

```c
    if (pageNr == PERFORMANCE_PAGE) {
        … unchanged …
    } else if (pageNr == EFFECT_PAGE) {
        /*
         * Effect page LEDs: TRACK LEDs show mute state (plan §13.5, A41),
         * the SELECT LED shows the current button, and the SEQ row stays
         * dark (it was cleared above; the FX step view arrives in Step 8).
         * The active type may add LEDs through its render hook.
         */
        buttonHandler_showMuteLEDs();
        led_setActiveSelectButton(menu_getSubPage());
        menuEffects_renderLeds();
    } else {
        … unchanged …
    }
```

The function then continues through `menu_resetActiveParameter()`, which
routes to the §7.15 Effect branch, and repaints.

### 7.17 After `menu_setVoiceModeShowMorph()` (line ~12646), add

```c
void menu_setEffectShowMorph(uint8_t onOff)
{
    /*
     * Effect page SHIFT Morph view (contract in menu.h). Pot snapshots are
     * refreshed because the visible cells' values change image; the repaint
     * shows Morph endpoints for Morphable rows and single values elsewhere.
     */
    menuEffects_setShowMorph(onOff);
    if (menu_activePage != EFFECT_PAGE)
        return;
    menu_endlessPotMappingChanged();
    menu_repaint();
}
```

---

## 8. `ledHandler.c` (modify)

- **Line 1243, `led_updateRecordedMainStep()`:**
  `if (menu_activePage == PERFORMANCE_PAGE)` →
  `if (menu_activePage == PERFORMANCE_PAGE || menu_activePage == EFFECT_PAGE)`.
  Comment: "The Effect page owns the SEQ row for its FX sequencer view (Step 8),
  so recording feedback must not paint Pattern steps there."
- **Lines 1419–1423, `led_notifyPatternChanged()` follow branch:** keep
  `menu_setShownPattern(patNr)`, but paint the SEQ row only off the Effect page:

```c
    if (parameter_values[PAR_FOLLOW]) {
        menu_setShownPattern(patNr);
        /* The Effect page's SEQ row is not a Pattern view (Session 072 step 7). */
        if (menu_activePage != EFFECT_PAGE) {
            led_clearSequencerLeds();
            led_updatePatternTrack(menu_getActiveVoice(), patNr,
                                   buttonHandler_selectedStep);
        }
    }
```

The chase (`led_updateCurrentStep`, lines 1202–1204) is already off for
`EFFECT_PAGE` and needs no change.

---

## 9. `buttonHandler`

### 9.1 `buttonHandler.h` line 21 (modify)

```c
/*
 * SHIFT+PERF select mode: the Effect page (Session 072 step 7; plan §13.1).
 * Mode 5 was SELECT_MODE_PAT_GEN (Euklid), now compiled out behind
 * ENABLE_EUKLID_PAGE; its LED feedback (MODE2 blinking) is unchanged.
 */
#define SELECT_MODE_FX          0x05
```

Then replace every live `SELECT_MODE_PAT_GEN` in `buttonHandler.c` as listed
below. Compiled-out blocks keep the old spelling only inside
`#if ENABLE_EUKLID_PAGE`; add
`#define SELECT_MODE_PAT_GEN SELECT_MODE_FX` inside such a guard, at the top
of `buttonHandler.c`, so re-enabling still compiles.

### 9.2 `buttonHandler.c` includes, after line 29

```c
#include "menuEffects.h"   /* Effect page type UI hook dispatch (step 7) */
```

Wrap the `#include "EuklidGenerator.h"` (line 27) and
`buttonHandler_applyEuklidParamsToMenu()` (lines 427–439) in
`#if ENABLE_EUKLID_PAGE … #endif`, with the comment
`/* Euklid UI compiled out (Session 072 step 7; plan §13.1). */`. Guarding the
function avoids an unused-static warning.

### 9.3 `handleModeButtons()`, lines 876–887 — replace the PAT_GEN case

```c
    case SELECT_MODE_FX:
        /*
         * SHIFT+PERF: the Effect page (Session 072 step 7). menu_switchPage()
         * enters on SELECT 1 screen 1, or toggles SELECT 1's two screens when
         * already here; it also paints TRACK mute LEDs and the SELECT LED.
         * The SEQ and SELECT rows are cleared first so no Pattern view
         * lingers from the previous mode.
         */
        led_clearSequencerLeds();
        led_clearSelectLeds();
        menu_switchPage(EFFECT_PAGE);
        break;
```

### 9.4 `handleSelectButton()`, lines 900–958 (modify)

**SHIFT switch** (lines 907–909): replace the `SELECT_MODE_PAT_GEN`
`selectBar` case with:

```c
        case SELECT_MODE_FX:
            /* SHIFT+SELECT on the Effect page: type hook only (§13.5). */
            (void)menuEffects_hookSelect(selectNr, 1u, 1u);
            break;
```

**Non-SHIFT switch** (lines 932–934): replace the `SELECT_MODE_PAT_GEN` case
with:

```c
    case SELECT_MODE_FX:
        /*
         * Effect SELECT (plan §13.2): the type may take the press; otherwise
         * choose the button's screens. The LED follows the resulting button
         * (an empty button leaves both unchanged).
         */
        if (menuEffects_hookSelect(selectNr, 0u, 1u))
            break;
        menu_switchSubPage(selectNr);
        led_setActiveSelectButton(menu_getSubPage());
        menu_repaintAll();
        break;
```

### 9.5 `handleVoiceButton()`, lines 1100–1180 (modify)

- **Line 1104:**
  `if (bh_state.selectButtonMode == SELECT_MODE_PERF)` →
  `if (bh_state.selectButtonMode == SELECT_MODE_PERF || bh_state.selectButtonMode == SELECT_MODE_FX)`.
  Comment: "PERF and the Effect page mute by default; SHIFT selects (A41)."
- **Immediately inside the block, before `if (muteModeActive) {`, add:**

```c
        if (bh_state.selectButtonMode == SELECT_MODE_FX &&
            menuEffects_hookTrack(voiceNr, buttonHandler_getShift(), 1u))
            return;
```

- **After the mute branch returns, before the PERF branch at line 1124,
  add:**

```c
        if (bh_state.selectButtonMode == SELECT_MODE_FX) {
            /*
             * SHIFT+TRACK on the Effect page selects the active track, as a
             * plain press does in VOICE/STEP (A41), without leaving the page.
             * TRACK LEDs keep showing mute state; the selected track flashes
             * once as feedback. The selection matters from Step 8/9 (FX lock
             * editing and live automation target the active track).
             */
            menu_setActiveVoice(voiceNr);
            buttonHandler_showMuteLEDs();
            led_flashLed((uint8_t)(LED_VOICE1 + voiceNr));
            if (shouldPreviewVoice)
                seq_previewVoice(voiceNr);
            return;
        }
```

- **Line 1156:** wrap `buttonHandler_applyEuklidParamsToMenu(voiceNr);` in
  `#if ENABLE_EUKLID_PAGE`.
- **Line 1178:** wrap the `else if (menu_activePage == EUKLID_PAGE) { … }`
  branch in the same guard.

### 9.6 BAR buttons, lines 1305–1336 (modify)

In both `case BUT_BAR1:` and `case BUT_BAR2:`, after the
`menu_loadSaveBarButtonPressed()` test, add:

```c
        if (bh_state.selectButtonMode == SELECT_MODE_FX) {
            /* BAR on the Effect page does nothing unless the type hooks it
             * (plan §13.5); Pattern bar navigation must not run here. */
            (void)menuEffects_hookBar(0u /* BAR1 */, buttonHandler_getShift(), 1u);
            break;
        }
```

Pass `1u` for BAR2. The BAR LED feedback (`led_setValue`) above it is kept.

### 9.7 SHIFT press, lines 1338–1403 (modify)

Replace
`case SELECT_MODE_PERF: case SELECT_MODE_PAT_GEN: { … menu_switchPage(PATTERN_SETTINGS_PAGE); … break; }`
with:

```c
        case SELECT_MODE_FX:
            /*
             * Holding SHIFT on the Effect page shows and edits Morph endpoints
             * of Morphable cells (plan §13.5, A42). Mute LEDs are repainted
             * below as for PERF.
             */
            menu_setEffectShowMorph(1u);
            break;

        case SELECT_MODE_PERF:
#if ENABLE_EUKLID_PAGE
            { … existing PATTERN_SETTINGS_PAGE / rotation block, unchanged … }
#endif
            /* PERF SHIFT keeps PERF visible (S072_ST7 D4); only mute LEDs. */
            break;
```

`buttonHandler_showMuteLEDs();` after the switch stays.

### 9.8 SHIFT release, lines 1487–1540 (modify)

- **Replace** `case SELECT_MODE_PAT_GEN: … menu_switchPage(EUKLID_PAGE); break;`
  (lines 1522–1527) with:

```c
        case SELECT_MODE_FX:
            /* End the momentary Morph view; TRACK LEDs keep mute state. */
            menu_setEffectShowMorph(0u);
            buttonHandler_showMuteLEDs();
            return;
```

- **Line 1536:** the tail `if (bh_state.selectButtonMode != SELECT_MODE_PERF)`
  is unreachable for FX because of the `return` above. Leave it as is.

---

## 10. Documentation (same change set)

- **`MODULE_INTERCHANGE_SPEC.md`:** the new `menuEffects` module (API §3), its
  boundaries with menu.c (`MENU_CELL_EFFECT`, `EFFECT_PAGE` branches) and
  buttonHandler (`SELECT_MODE_FX`), and the EffectsManager edit API as the
  only Effect write path for UI and hooks.
- **`SRAM_MANIFEST.md`:** menuEffects page state, 14 B SRAM1 (item 8).
- **`BANK_PRESET_ARCHITECTURE.md`:** Effect edits are active-Scene-only until
  Step 10 fan-out (D3); `typ` changes run `effects_changeType()`.
- **`EFFECTS_BUS_FEATURE_PLAN.md`:**
  - §13.2: record D1 (re-press cycles) and D2 (provisional labels);
  - §13.1: `ENABLE_EUKLID_PAGE`, `SELECT_MODE_FX`, D4;
  - §17.1: mark Step 7 implemented with the hardware gate pending.
- **`MEMORY.md` volatile note:** "S072 Step 7: SHIFT+PERF Effect page live
  (`menuEffects.c`). Edits active Scene only until Step 10; SEQ buttons inert
  until Step 8."

---

## 11. Build and verification gates

### Build

1. `make clean && make all` succeeds with no new warnings. In particular, no
   unused `buttonHandler_applyEuklidParamsToMenu` and no unused Euklid
   include.
2. `link_budget.py`:
   - DTCM and FXBUF unchanged.
   - Record the flash delta (expected +3 to +4.5 KB).
   - `.bss` about +14 B.

### Hardware walk-through (production image, a Scene with `filter.fx` loaded)

3. **Entry.** SHIFT+PERF shows `TYP OUT VOL PAN` with `flt 1   127 0` (St1,
   centre = `0`). MODE2 blinks. TRACK LEDs show mute state, and the SELECT1
   LED is lit. The SEQ row is dark, including while playing.
4. **SELECT 1 toggle.** Repeated SHIFT+PERF, and a SELECT1 re-press, toggle to
   `RUN LEN SCL MRP` = `fwd 16 /16 0` and back.
5. **Type rows.** SELECT2 shows `FRQ RES DRV TYP` with the loaded values.
   SELECT3..8 presses are ignored (LED stays on SELECT2).
6. **Encoder scroll.** From `typ` it walks every cell across both SELECT1
   screens into SELECT2, and the SELECT LED follows. It stops at the ends.
7. **Pots.**
   - The four pots edit the visible cells; the change is audible with a send
     raised.
   - The pot over `typ` does nothing.
   - `mrp` moves at double rate.
8. **Full view.**
   - Encoder click on a parameter shows `Filter  Frequncy` with its value.
   - Turning edits it; clicking again returns.
9. **`typ` gesture.**
   - Click in on `typ`: `Effect  Type` / `StFilter … flt`.
   - Turn to `Off`: `*` appears. Click out: the return goes silent, SELECT2
     becomes empty, and `out/vol/pan` and `run/len/scl` are unchanged.
   - Click in, turn back to `StFilter`, click out: filter defaults are
     restored (cutoff 64 LP), and the sequence is cleared.
   - Click in and out without turning: no change.
10. **Scene switch during the `typ` view** (PERF Scene select from another
    mode, then return) and **Scene switch while on the page** (via MIDI or
    an automated Scene change, if available): the candidate is discarded and
    the page repaints the new Scene.
11. **SHIFT Morph view.**
    - Hold SHIFT: Morphable cells (`vol pan frq res drv typ(filter)`) show
      Morph endpoints; `typ`, `out`, `run/len/scl/mrp` show single values.
    - Editing while SHIFT is held writes only Morph endpoints.
    - Raising `mrp` then sweeps between the two endpoints audibly.
12. **TRACK.**
    - Plain TRACK toggles mute (the LED follows).
    - SHIFT+TRACK selects the track: the LED flashes once, the mute LEDs stay,
      and it previews when stopped. The page does not change.
13. **BAR / SHIFT+SELECT** do nothing on the Effect page.
14. **Leave and return.**
    - VOICE, STEP, PERF and Load/Save work as before.
    - VOICE Morph mode (SHIFT+VOICE) is unaffected.
    - PERF SHIFT keeps PERF visible (D4).
15. **Persistence.**
    - Edits survive reboot through AutoSave.
    - Scene Save writes them into `<name>.fx` (Step 6 writer), with
      `effect_morph_amount` in `sceneset.scg`.
16. **Regression.** No Euklid page is reachable; CPU matches Step 6.

### Not testable yet

- SEQ lock editing, `sel`, and FX LEDs (Step 8);
- automation and LFO on `fx` (Step 9);
- fan-out to masked Scenes (Step 10).

### Rollback

Revert the change set. There is no data or file-format change.

---

## 12. Implementation notes (2026-09-28)

### Completed source work

- Added `Core/Menu/menuEffects.c/.h` with the registry-driven Effect page:
  SELECT screen memory, default/custom layouts, linear navigation, compact and
  full views, `typ` candidate browsing/transaction, Morph endpoint display,
  sequence-setting cells, and optional type gesture/LED hooks.
- Added the EffectsManager retained-edit boundary for parameters, sequence
  settings, and Scene Effect Morph amount. Step 7 writes the active Scene only;
  Step 10 can add edit-mask fan-out inside this boundary.
- Added `EFFECT_PAGE` and `MENU_CELL_EFFECT` delegation in `menu.c`, including
  encoder click/turn/click-out, endless pots, cursor repair, Scene/type-change
  service, Morph setter, and SELECT LED tracking.
- Reassigned select mode 5 to `SELECT_MODE_FX`, routed SHIFT+PERF, SELECT,
  TRACK, BAR, and SHIFT gestures, and suppressed Pattern recording/follow/chase
  painting on the Effect page. The Euklid UI helper/include and retired SHIFT
  paths are guarded by `ENABLE_EUKLID_PAGE=0`.
- Updated the Effects Bus plan, `MEMORY.md`, module interchange contract,
  SRAM ledger, and Bank/Preset parameter-change notes. New and modified code
  paths retain adjacent descriptive comment blocks in both source and header
  interfaces.

### Design decisions applied

- D1: re-pressing the current SELECT cycles its screens; pressing a different
  SELECT starts at screen 0.
- D2: Step 7 owns provisional compact/full `scl` labels; Step 8 will move them
  to shared sequencer ownership.
- D3: Effect edits are active-Scene-only until Step 10.
- D4: PERF SHIFT no longer enters the empty Pattern Settings page when the
  Euklid page switch is disabled.
- D5: the 14-byte `menuEffects` state block is in SRAM1; all RAM expansion is
  approved for this session.

### Build verification

`git diff --check` passed. A clean `make -j2 all` passed with the existing
toolchain warnings only (legacy unused filesystem helpers, USB `packed`
diagnostics, libc syscall stubs, and LTO serial-job notice). The resulting
link is:

```text
text=475,592  data=416  bss=426,144  dec=902,152
Flash payload: 476,008 / 491,520 B, headroom 15,512 B
ITCM: 3,768 / 16,384 B
DTCM statics: 4,448 B
FXBUF: 126,624 B at 0x20001160, margin 3,744 B
```

The production image was generated at `build/LXRV2_lxr02.img` (476,024 bytes
including its 16-byte wrapper). The hardware UI walk-through remains to be
completed on the device. Step 8 SEQ lock/LED behavior, Step 9 automation/LFO,
and Step 10 fan-out remain intentionally out of scope.

---

## 13. Assessment (review of the implemented Step 7 tree)

Reviewed on 2026-09-28. I read the full working-tree diff of `menuEffects.c/.h`,
`menu.c/.h`, `menuPages.h`, `buttonHandler.c/.h`, `ledHandler.c`,
`EffectsManager.c/.h`, `config.h` and the `Makefile`, and did an independent
clean rebuild.

### 13.1 Build

- **Clean rebuild:** `make clean && make all`, exit 0. `text=475,592`,
  `data=416`, `bss=426,144`, matching §12.
- **Warnings:** 20 in total. None is new: the only `Core/Menu` warnings are
  the pre-existing unused `splashAnimation_restore*` constants.
- **`nm`:** the menuEffects state is exactly 14 B (8 + six 1-byte cells), as
  in D5.
- **Flash: 476,008 / 491,520 B, headroom 15,512 B** (−5,392 B against
  Step 6).
  - This is about 900 B above the §0 D5 estimate.
  - **`link_budget.py` now prints its below-16 KiB warning.**
  - See §13.4.

### 13.2 Code against schedule

| § | Item | Result |
|---|---|---|
| 2 | UI hooks struct; edit API decl + impl | Match. |
| 3–4 | `menuEffects.h/.c` | Match. The file includes `buttonHandler.h` instead of `ledHandler.h`; this is harmless because no LED call is made. `menuEffects_enter()` also clears the Morph flag, and `menu_switchPage()` then re-reads SHIFT, which is equivalent. |
| 5 | Makefile, `ENABLE_EUKLID_PAGE`, `DEV_EFFECT_FORCE_TYPE` comment | Match. |
| 6 | `EFFECT_PAGE`, `menu_setEffectShowMorph()`, menuPages row | Match. |
| 7.1–7.17 | menu.c dispatch, navigation, click, pots, service, switchPage, LEDs | Match. |
| 8 | ledHandler recording and follow guards | Match. The chase comment is updated. |
| 9 | buttonHandler FX mode, SELECT, TRACK, BAR, SHIFT; Euklid guards | Match. The old `#define SELECT_MODE_PAT_GEN` alias was not added, and none is needed: the only surviving guarded Euklid block no longer names the mode. The PERF SHIFT block, when re-enabled, is simplified to its PERF (rotation) form. |

### 13.3 Findings

1. **A stale `typ` candidate can be committed from another cell (defect;
   schedule gap).**
   - **Cause:** `menu_switchSubPage()` clears `editModeActive` on every SELECT
     press (`menu.c:11812`), but its Effect branch does not end menuEffects'
     `typ` transaction.
   - **Sequence:**
     1. Click in on `typ` and turn to `Off` (candidate `off`, `*` shown).
     2. Press SELECT 2, then SELECT 1. The compact `typ` cell now shows the
        uncommitted `off`, because `menuEffects_cellValue()` still returns
        the candidate.
     3. Click in on `vol` (no effect on the flag), then click out.
     4. `menuEffects_editModeChanged(0, …)` sees `menuEffects_typeEdit == 1`
        and **commits `off`**. The type is lost, parameters 3..63 reset, and
        the sequence is cleared.
   - The schedule (§4.2 `menuEffects_selectPressed`, §7.14) missed this path;
     `menu_switchPage()` and the service path already discard the candidate
     correctly.
   - **Fix:** at the top of `menuEffects_selectPressed()`, before the
     empty-button return, add `menuEffects_typeEdit = 0u;` with the comment
     "A SELECT press leaves any full view (menu.c clears editModeActive), so an
     open `typ` candidate is discarded, never committed later." One line.
2. **Flash headroom below the 16 KiB warning line (budget).**
   - Steps 8 (FX sequencer), 9 (automation + LFO) and 10 (fan-out + gate)
     still add code. At the observed rate this is about 3, 5 and 2 KB, which
     leaves roughly 5 KB after Step 10.
   - Nothing is needed now, but each remaining schedule must state a flash
     estimate and prefer table-driven code.
   - Candidate recoveries, if needed later:
     - retiring the compiled-in Euklid generator and SOM page code that is no
       longer reachable from the UI;
     - the unused `filesystem.c` static helpers flagged by `-Wunused-function`
       (dropped by LTO already, so likely small);
     - the large constant tables (ST1 §21 finding 6).
   - This is a decision for later, not a Step 7 defect.
3. **Step 6 Findings 1 and 2 are still open.**
   - The blank Effect name at Scene Save phases 82/33 still passes the raw
     cell (`filesystem.c:19659-19661`, `19888-19890`), so a blank name can
     still save as `inst.fx`.
   - Kit Save still writes the Effect row (`filesystem.c` in
     `filesystem_saveKitDirectory_tick()`, the two `residentEffectRow` lines
     after the Kit-row source).
   - These are the prerequisites stated at the top of this schedule. Apply
     them with Finding 1.

No other defects were found.

- Hook dispatch, Morph image rules, `typ` inertness on pots and generic
  commits, the Scene-switch service and the LED ownership all read correctly.
- VOICE screen code is untouched: only the four `is2ndPage` predicates moved
  to `menu_isScreenPage()`.

### 13.4 Hardware status

The §11 walk-through (gates 3–16) is pending. Apply Finding 1 before running
gate 9 (the `typ` gesture), and repeat gate 9 with an intervening SELECT press.
