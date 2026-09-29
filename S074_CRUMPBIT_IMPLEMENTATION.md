# S074 — CrumpBit (`cbt`) implementation schedule

**Status:** schedule only. No code has been changed.

**Specification:** `S074_CRUMPBIT_EFFECT.md` (draft 3, every decision
settled). This document lists every code change that plan needs.

**Base:** the working tree of 2026-09-29, which is HEAD `223abdc` plus the
uncommitted S074 underline fix and image-script fold. Every line number
below is a line number in **that** tree, before any edit. Apply the changes
in each file **from the highest line number down** so the numbers stay valid
(§1.3 gives the order).

**Stages:** each change is tagged with the stage it belongs to (§1.2).
Stage 1 is a complete, listenable build on the default Effect page. Stage 2
adds the custom page.

---

## 1. Overview

### 1.1 Change list

| ID | File | Lines | Action | What | Stage |
|---|---|---|---|---|---|
| B1 | `Core/DSP/Effects/FxBuffer.c` | 227, 237 | MODIFY (move) | Reset the handoff before the diagnostic forced units (gap 3) | 0 |
| X1 | `Core/DSPAudio/mixer.c` | after 1019 | ADD | Zero the FX return ramp origin while the Effect is `off` (gap 2) | 0 |
| N1 | `Core/DSP/Effects/CrumpBit/CrumpBitParameters.h` | new | ADD file | Row enum, tokens, table and page exports | 1 |
| N2 | `Core/DSP/Effects/CrumpBit/CrumpBitParameters.c` | new | ADD file | Tokens and descriptors (Stage 1); layout and page hooks (Stage 2 block) | 1 + 2 |
| N3 | `Core/DSP/Effects/CrumpBit/CrumpBitEffect.h` | new | ADD file | Runtime struct, buffer size, ops, Rate/Sync helpers | 1 |
| N4 | `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c` | new | ADD file | ADC, data lines, DAC, AC coupling, tape loop; arena contract | 1 |
| E1 | `Core/DSP/Effects/EffectsManager.h` | 61–63 | MODIFY | `EFFECT_TYPE_CRUMPBIT`, `EFFECT_TYPE_COUNT = 3` | 1 |
| E2 | `Core/DSP/Effects/EffectsManager.h` | 117–136 | MODIFY | Page-hook contract: `paint_row0`, `format_value3`, `flags`, return codes | 2 |
| E3 | `Core/DSP/Effects/EffectsManager.h` | 138–142 | MODIFY | Layout contract: `custom_row0`, home screen, `mrp` sentinel | 2 |
| M1 | `Core/DSP/Effects/EffectsManager.c` | after 21 | ADD | Include the CrumpBit headers | 1 |
| M2 | `Core/DSP/Effects/EffectsManager.c` | 49–51 | MODIFY | Add `CrumpBitRuntime` to the runtime union | 1 |
| M3 | `Core/DSP/Effects/EffectsManager.c` | after 91 | ADD | Registry row `cbt` | 1 (pointers in 2) |
| M4 | `Core/DSP/Effects/EffectsManager.c` | after 95 | ADD | Static asserts: the union holds CrumpBit and stays 76 B | 1 |
| M5 | `Core/DSP/Effects/EffectsManager.c` | 1126–1162 | MODIFY | Factor out `effects_exportHandoff()` | 3 |
| M6 | `Core/DSP/Effects/EffectsManager.c` | 1212–1213 | MODIFY | Refresh the handoff on a same-type Scene switch (gap 1) | 3 |
| MK1 | `Makefile` | after 39 | ADD | `-ICore/DSP/Effects/CrumpBit` | 1 |
| MK2 | `Makefile` | after 113 | ADD | `CrumpBitParameters.c` in `SRCS` | 1 |
| MK3 | `Makefile` | after 140 | ADD | `CrumpBitEffect.c` in `DSP_SRCS` | 1 |
| MK4 | `Makefile` | after 205 | ADD | Explicit `-Ofast` rule for `CrumpBitEffect.o` | 1 |
| H1 | `Core/Menu/menuEffects.h` | after 115 | ADD | Page-extension API declarations | 2 |
| H2 | `Core/Menu/menuEffects.h` | 117–130 | MODIFY | Hold contract (last step held) and `shownParam`/`editParam` | 2 |
| P1 | `Core/Menu/menuEffects.c` | after 22 | ADD (comment) | File header: type-owned screens | 2 |
| P2 | `Core/Menu/menuEffects.c` | 54–61 | MODIFY | `holdActive` → packed `holdState` (0 B) | 2 |
| P3 | `Core/Menu/menuEffects.c` | 160–161 | MODIFY | Layout `mrp` sentinel | 2 |
| P4 | `Core/Menu/menuEffects.c` | 202 | MODIFY | `menuEffects_enter()` uses `holdState` | 2 |
| P5 | `Core/Menu/menuEffects.c` | 221–233 | MODIFY | `menuEffects_leave()`: clear owned SELECT LEDs; `holdState` | 2 |
| P6 | `Core/Menu/menuEffects.c` | 452–475 | MODIFY | `menuEffects_formatValue3()` → type format hook | 2 |
| P7 | `Core/Menu/menuEffects.c` | before 635 | ADD | `menuEffects_highestStep()`, `menuEffects_heldStep()` | 2 |
| P8 | `Core/Menu/menuEffects.c` | 652–683 | MODIFY | Service: track the last step held; re-render owned LEDs | 2 |
| P9 | `Core/Menu/menuEffects.c` | 706–712 | MODIFY | `menuEffects_hookSelect()` abandons a `typ` browse | 2 |
| P10 | `Core/Menu/menuEffects.c` | after 734 | ADD | Row-0 painter, format, LED owner, home, live refresh | 2 |
| P11 | `Core/Menu/menuEffects.c` | 753–763 | REMOVE | `menuEffects_firstHeldStep()` | 2 |
| P12 | `Core/Menu/menuEffects.c` | 774–784 | MODIFY | `seqHoldExpired()` / `seqHoldActive()` use `holdState` | 2 |
| P13 | `Core/Menu/menuEffects.c` | 786–812 | MODIFY | `menuEffects_holdEdit()` seeds from the last step held | 2 |
| P14 | `Core/Menu/menuEffects.c` | 814–829 | MODIFY | `menuEffects_holdDisplay()` shows the last step held | 2 |
| P15 | `Core/Menu/menuEffects.c` | after 829 | ADD | `menuEffects_shownParam()`, `menuEffects_editParam()` | 2 |
| U1 | `Core/Menu/menu.h` | after 417 | ADD | `menu_effectShowHome()` declaration | 2 |
| C1 | `Core/Menu/menu.c` | after 13442 | ADD | `menu_effectShowHome()` | 2 |
| C2 | `Core/Menu/menu.c` | 12788 | MODIFY | Effect page entry: SELECT LEDs through the owner check | 2 |
| C3 | `Core/Menu/menu.c` | 12496 | MODIFY | Cursor repair: SELECT LEDs through the owner check | 2 |
| C4 | `Core/Menu/menu.c` | 10225 | MODIFY | Encoder move: SELECT LEDs through the owner check | 2 |
| C5 | `Core/Menu/menu.c` | after 10003 | ADD | Paint a type-owned top row | 2 |
| C6 | `Core/Menu/menu.c` | between 9966 and 9967 | ADD | Full view: type value text | 2 |
| C7 | `Core/Menu/menu.c` | 2872 | MODIFY | Live refresh on the Effect page (Sync label) | 2 |
| C8 | `Core/Menu/menu.c` | 2789–2791 | MODIFY | No name markers on a type-painted row 0 | 2 |
| C9 | `Core/Menu/menu.c` | 2767–2768 | MODIFY | Held PARAM values through the type format hook | 2 |
| BH1 | `Core/Hardware/frontPanel/buttonHandler.c` | 961–968 | MODIFY | FX SELECT: act on `EFFECT_UI_SHOW_HOME`; owned LEDs | 2 |
| BH2 | `Core/Hardware/frontPanel/buttonHandler.c` | 935–938 | MODIFY | FX SHIFT+SELECT: act on `EFFECT_UI_SHOW_HOME` | 2 |

### 1.2 Stages

- **Stage 0 (arena gaps 2 and 3): B1, X1.** No audible change for `flt`, but
  the return ramp now starts from 0 after `off`.
- **Stage 1 (listenable `cbt` on the default page):** N1, N3, N4, N2 without
  its Stage-2 block, E1, M1–M4 (M3 with `NULL` layout and `NULL` hooks), and
  MK1–MK4.
  - `cbt` appears in `typ`. Rows 3–10 fill SELECT 2 and 3 as numbers (the
    masks as 0..255).
  - Every sound feature works. Sync is audible, but the Rate cell shows the
    raw number.
- **Stage 2 (the page):** E2, E3, the N2 Stage-2 block, the M3 pointers,
  H1, H2, P1–P15, U1, C1–C9, BH1 and BH2.
- **Stage 3 (arena contract):** M5 and M6, then the minimum-share test.

### 1.3 Order within each file (highest line first)

| File | Order |
|---|---|
| `menu.c` | C1 → C2 → C3 → C4 → C5 → C6 → C7 → C8 → C9 |
| `menuEffects.c` | P15 → P14 → P13 → P12 → P11 → P10 → P9 → P8 → P7 → P6 → P5 → P4 → P3 → P2 → P1 |
| `menuEffects.h` | H2 → H1 |
| `EffectsManager.c` | M6 → M5 → M4 → M3 → M2 → M1 |
| `EffectsManager.h` | E3 → E2 → E1 |
| `buttonHandler.c` | BH1 → BH2 |
| `Makefile` | MK4 → MK3 → MK2 → MK1 |
| Others | single change each |

### 1.4 Decisions carried from the specification

| Point | Implemented as |
|---|---|
| ADC (Q1) | Offset binary, `c = clamp(round(x·128) + 128, 0, 255)` |
| Data lines (F1) | `c' = (c & ~(off & ~inv)) ^ inv`: off = 0, invert = flip, invert wins |
| DAC and AC coupling (F2) | `y = (c' − 128)/128`, then a one-pole high-pass at about 10 Hz per channel |
| Delay | Mono `½(hL + hR)`, 8-bit loop in the Effect share, linear interpolation, 150 ms one-pole glide, feedback 0..0.99 |
| Rate (Q5) | `t = 1.60 s · 80^(−rate/127)` (20 ms at 127); fixed range that fits the minimum share |
| Sync (Q7) | Nearest step-scale division (log time) at `seq_getBpm()` among divisions ≤ 1.60 s. The raw Rate stays underneath; only the Effect page shows the division. |
| Mix, pan | Linear crossfade; balance with the mixer's stereo return law |
| Hold (Q13, F3) | SELECT during a hold writes both mask lanes, with the last step held's masks and the bit changed, to every held step. The screen and LEDs show the last step held. |
| **Interpretation** | "Last step held" is applied to **every** Effect-page held cell (display and seed), not only the masks, so one screen never mixes two steps. This also changes `flt`'s hold seed from the lowest-numbered held step to the last one held. To keep the S072 rule for other cells, give P13 and P14 their own lowest-step accessor. |
| No WIDE8 on the masks | WIDE8 only affects Pattern automation, which the masks do not use |

### 1.5 Resources

| Resource | Change |
|---|---|
| Static RAM | **0 B.** `CrumpBitRuntime` is 56 B inside the existing 76 B DTCM union (M4 asserts that the union stays 76 B). The last step held lives in spare bits of the existing `menuEffects` hold byte (P2). Everything else is `const` (flash). |
| Arena | 70,592 B of the Effect share (1.60 s of 8-bit mono plus a guard), within the 73,632 B minimum share |
| Stack | A few scalars; no arrays |
| Flash | An estimated 3–4 KB (includes `expf` from newlib; `powf` is already linked) |
| CPU | About 75–95 instructions per sample (both channels and the delay), plus one `expf`, one divide and a 14-step loop per block; constant whatever the settings |
| Sector 6 | The image ends 3,560 B below `0x08080000`, so Stage 1 will almost certainly be the first image past it. Keep the current known-good `.img` before flashing (F6). |

---

## 2. Stage 0 — arena gaps 2 and 3

### B1 — `Core/DSP/Effects/FxBuffer.c` L227, L237 — MODIFY (move one line)

**Current (L222–239):**

```c
void fxbuf_init(void)
{
    fxbuf_state.base = _sfxbuf;
    fxbuf_state.bytes = (uint32_t)(_efxbuf - _sfxbuf);
    fxbuf_state.on_share_changed = NULL;
    fxbuf_clearOwners();

#if DEV_MODE_DIAGNOSTIC
    fxbuf_selfTestResult = fxbuf_selfTest();
    {
        uint8_t n;
        for (n = 0u; n < (uint8_t)DEV_FXBUF_FORCE_VOICE_UNITS; n++)
            (void)fxbuf_voiceAcquire((uint8_t)(n / FXBUF_VOICE_UNITS_PER_SLOT), 1u);
    }
#endif
    fxbuf_handoffResetAll();
    (void)fxbuf_handoffBeginExit();
}
```

**New:** remove L237 (`fxbuf_handoffResetAll();`) and insert it, with a
comment, after L227 (`fxbuf_clearOwners();`):

```c
    fxbuf_clearOwners();
    /*
     * Reset the handoff record before any unit is claimed (S072 debt 1,
     * closed in S074).
     *
     * What: clears the handoff snapshot to "nothing valid" immediately after
     * the owner table. Why: fxbuf_voiceAcquire() stamps each claimed unit's
     * default store rate into the handoff record. With the reset after the
     * diagnostic forced-unit loop, those units were left with rate 0, and the
     * minimum-share test (DEV_FXBUF_FORCE_VOICE_UNITS 12) that the first
     * buffer-using Effect needs ran against a malformed record. Inputs: none.
     * Output: a zeroed record whose pointers are FXBUF_OFFSET_NONE. The
     * self-test and forced claims below now write valid unit rates, and
     * fxbuf_handoffBeginExit() at the end clears free units. Affiliates:
     * fxbuf_handoffResetAll(), fxbuf_voiceAcquire(), DEV_MODES.md.
     */
    fxbuf_handoffResetAll();
```

Production builds are unchanged in effect: with `DEV_MODE_DIAGNOSTIC 0`
nothing runs between the two positions.

---

### X1 — `Core/DSPAudio/mixer.c` after L1019 — ADD

After L1019 (the closing `}` of `if (fx_active) { … }`) and before the
function's closing `}` at L1021, add:

```c
	else {
		/*
		 * Reset the FX return ramp origin while no Effect runs (S072 debt 8,
		 * closed in S074).
		 *
		 * What: holds both return gains' previous-block origin at 0 on every
		 * `off` block. Why: mixer_addFxReturnToOutput() ramps from
		 * mixer_fx_return_last_gain[] to the new gains. While `off` it is
		 * not called, so the origin kept the last active Effect's gains, and
		 * a type that outputs on its first block after init (CrumpBit's AC
		 * coupled 8-bit output is never silent) started from those stale
		 * gains, a click. From 0 the return fades in over one block. Cost: two
		 * stores per `off` block, unconditional. Inputs: fx_active == 0.
		 * Output: mixer_fx_return_last_gain[0..1] = 0. Affiliates:
		 * mixer_addFxReturnToOutput(), effects_activeIoFlags(),
		 * EFFECTS_MIXER_DSP_REFERENCE.md §5.3 item 2.
		 */
		mixer_fx_return_last_gain[0] = 0.0f;
		mixer_fx_return_last_gain[1] = 0.0f;
	}
```

(`mixer.c` uses tab indentation; match it.) The active-Effect path is
untouched, so its sound is bit-identical (S0).

---

## 3. New files (N1–N4)

All four use the project's standard file header. Copy the block from
`StereoFilterParameters.h` and change the path and date (29.09.2026).

### N1 — `Core/DSP/Effects/CrumpBit/CrumpBitParameters.h` — ADD file (Stage 1)

```c
/* <standard LXR02 file header: Core/DSP/Effects/CrumpBit/CrumpBitParameters.h> */

#ifndef CRUMP_BIT_PARAMETERS_H_
#define CRUMP_BIT_PARAMETERS_H_

#include "EffectsManager.h"

/*
 * CrumpBit (`cbt`) registry exports (Session 074; S074_CRUMPBIT_EFFECT.md).
 *
 * What: the immutable descriptor table, display tokens, Effect-page layout
 * and page hooks of the CrumpBit Effect type: a bipolar 8-bit ADC whose
 * eight data lines can be forced off or inverted, an 8-bit DAC with AC
 * coupling, and a mono 8-bit tape-style delay in the FxBuffer Effect share.
 * Why a separate file: EffectsManager owns the registry row but not a type's
 * control tables. This side is ordinary -O2 control and page code;
 * CrumpBitEffect.c is the -Ofast DSP.
 * Inputs/outputs: compile-time tables, plus page hooks that run in foreground
 * on the Effect page and write retained data only through
 * menuEffects_editParam(), and so through the EffectsManager edit API.
 * Accessors: EffectsManager.c (registry row), menuEffects.c (layout, hooks).
 * Affiliates: CrumpBitEffect.c/.h (DSP, shared Rate/Sync helpers),
 * EffectParamRows.h, StepScale.h, ledHandler.h.
 */

/* Descriptor rows: common rows 0..2 plus type rows 3..10. */
#define CRUMPBIT_PARAM_COUNT 11u

/*
 * Local descriptor indices.
 *
 * These are permanent identities: they are the Effect-local numbers used by
 * FX lanes, Pattern automation targets (block 7, 448 + index) and LFO
 * tokens, and each row's file key is stored in `.fx` and AutoSave. Append
 * only; never renumber.
 */
typedef enum {
    CRUMPBIT_PARAM_AUDIO_OUT  = EFFECT_COMMON_PARAM_AUDIO_OUT,
    CRUMPBIT_PARAM_LEVEL      = EFFECT_COMMON_PARAM_LEVEL,
    CRUMPBIT_PARAM_PAN        = EFFECT_COMMON_PARAM_PAN,
    CRUMPBIT_PARAM_BIT_OFF    = 3,
    CRUMPBIT_PARAM_BIT_INVERT,
    CRUMPBIT_PARAM_MIX,
    CRUMPBIT_PARAM_FEEDBACK,
    CRUMPBIT_PARAM_RATE,
    CRUMPBIT_PARAM_SUBTYPE,
    CRUMPBIT_PARAM_SYNC,
    CRUMPBIT_PARAM_DLY_PAN,
    CRUMPBIT_PARAM_ENUM_COUNT
} crumpbit_param_t;

/* Sub-effect behind Mix/Feedback/Rate; only the delay exists in v1. */
#define CRUMPBIT_SUBTYPE_DELAY 0u

extern const effect_param_descriptor_t crumpBit_descriptors[];
extern const char crumpBit_token3[];
extern const char crumpBit_abbrev5[];
extern const char crumpBit_full8[];

/*
 * Effect-page layout and hooks (Stage 2).
 *
 * crumpBit_layout places the overlay (home), page 2 and page 3 on SELECT 2.
 * crumpBit_ui owns the SELECT buttons (bit toggles), the SELECT LEDs (bit
 * states), the overlay's top row and the `sub`/Sync value text. Both are
 * referenced only by the registry row. For a Stage 1 build, leave them out
 * and pass NULL.
 */
extern const effect_select_layout_t crumpBit_layout;
extern const effect_ui_hooks_t crumpBit_ui;

#endif /* CRUMP_BIT_PARAMETERS_H_ */
```

---

### N2 — `Core/DSP/Effects/CrumpBit/CrumpBitParameters.c` — ADD file (Stage 1, and a Stage 2 block)

```c
/* <standard LXR02 file header: Core/DSP/Effects/CrumpBit/CrumpBitParameters.c> */

#include "CrumpBitParameters.h"
#include "CrumpBitEffect.h"
#include "menu.h"
#include "MenuText.h"
#include "EffectParamRows.h"
#include "menuEffects.h"
#include "ledHandler.h"
#include "StepScale.h"
#include "sequencer.h"
#include <string.h>

/* Persisted token (permanent), UI abbreviation (5) and full name (<= 8). */
const char crumpBit_token3[] = "cbt";
const char crumpBit_abbrev5[] = "CrmBt";
const char crumpBit_full8[] = "CrumpBit";

/*
 * CrumpBit descriptor table (S074_CRUMPBIT_EFFECT.md §4).
 *
 * What: rows 0..2 are the shared common rows; rows 3..10 are CrumpBit's.
 * - bit off / bit invert (3, 4): 8-bit masks, one bit per ADC data line
 *   (bit 0 = LSB). Not Morphable (interpolating a mask gives unrelated
 *   bits), not modulatable, not Pattern-automatable (a Pattern value is
 *   7-bit and cannot reach bit 7). Sequenceable through FX lanes 1 and 2,
 *   which store full 8-bit values. Edited by the SELECT buttons (Stage 2);
 *   in Stage 1 they show as plain 0..255 numbers.
 * - mix, feedback, rate, delay pan (5, 6, 7, 10): Morphable, LFO and
 *   Pattern automation, like voice rows.
 * - sub-type (8): only `dly` (max 0); its label comes from the format hook,
 *   because every DTYPE_MENU table id (4 bits) is already in use.
 * - sync (9): on/off; Pattern-automatable and sequenceable.
 * Why: the registry makes storage, AutoSave, the Effect page, lanes,
 * automation and LFO work without type-specific code elsewhere. Inputs:
 * none (const). Output: 11 rows. Accessors: EffectsManager (resolution,
 * clamps, capabilities), menuEffects/menu.c (labels, dtypes), `.fx` and
 * AutoSave (file keys). Affiliates: EffectParamRows.h, CrumpBitEffect.c
 * write_param().
 */
const effect_param_descriptor_t crumpBit_descriptors[] = {
    EFFECT_COMMON_ROWS(INSTRUMENT_PARAM_FLAG_AUTOMATABLE,
                       EFFECT_FLAGS_IMAGE,
                       EFFECT_FLAGS_IMAGE),
    EFFECT_ROW("crump_bit_off", "Bits", "BitOff", "bof", DTYPE_0B255,
               0u, EFFECT_MOD_NONE, 0u, 255u),
    EFFECT_ROW("crump_bit_invert", "Bits", "BitInv", "biv", DTYPE_0B255,
               0u, EFFECT_MOD_NONE, 0u, 255u),
    EFFECT_ROW("crump_mix", "Delay", "Mix", "mix", DTYPE_0B127,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 40u, 127u),
    EFFECT_ROW("crump_feedback", "Delay", "Feedback", "fbk", DTYPE_0B127,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 48u, 127u),
    EFFECT_ROW("crump_rate", "Delay", "Rate", "rte", DTYPE_0B127,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 64u, 127u),
    EFFECT_ROW("crump_subtype", "CrumpBit", "SubType", "sub", DTYPE_0B127,
               0u, EFFECT_MOD_NONE, CRUMPBIT_SUBTYPE_DELAY, 0u),
    EFFECT_ROW("crump_sync", "Delay", "Sync", "syn", DTYPE_ON_OFF,
               INSTRUMENT_PARAM_FLAG_AUTOMATABLE, EFFECT_MOD_NONE, 0u, 1u),
    EFFECT_ROW("crump_dly_pan", "Delay", "DlyPan", "dpn", DTYPE_PM63,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 64u, 127u),
};

_Static_assert(sizeof(crumpBit_descriptors) /
                   sizeof(crumpBit_descriptors[0]) == CRUMPBIT_PARAM_COUNT,
               "CRUMPBIT_PARAM_COUNT must match the descriptor table");
_Static_assert(CRUMPBIT_PARAM_ENUM_COUNT == CRUMPBIT_PARAM_COUNT,
               "crumpbit_param_t must match the descriptor table");

/* ======================================================================
 * Stage 2: Effect-page layout and hooks. For a Stage 1 build, omit this
 * block and register NULL layout and hooks (M3).
 * ====================================================================== */

/* Three states per data line, shown as `-`, `0` and `!`. */
#define CRUMPBIT_BIT_NORMAL 0u
#define CRUMPBIT_BIT_OFF    1u
#define CRUMPBIT_BIT_INVERT 2u
#define CRUMPBIT_BIT_COUNT  8u

/*
 * Resolve one data line's state from a mask pair.
 *
 * Input: the shown bit-off and bit-invert masks and one bit. Output:
 * CRUMPBIT_BIT_INVERT if the invert bit is set (invert wins, as in the DSP),
 * else CRUMPBIT_BIT_OFF if the off bit is set, else CRUMPBIT_BIT_NORMAL.
 * Why: the SELECT cycle, the top row and the LEDs must read a stale
 * off+invert pair exactly as the DSP plays it. Callers: the three hooks
 * below.
 */
static uint8_t crumpBit_bitState(uint8_t off, uint8_t invert, uint8_t bit)
{
    if ((invert & bit) != 0u)
        return CRUMPBIT_BIT_INVERT;
    if ((off & bit) != 0u)
        return CRUMPBIT_BIT_OFF;
    return CRUMPBIT_BIT_NORMAL;
}

/*
 * SELECT hook: cycle one data line (S074 Q13, Q14, F3).
 *
 * What: SELECT n (button n-1 = bit n-1, bit 0 = LSB) moves that bit
 * normal -> off -> invert -> normal. SHIFT+SELECT resets it to normal. The
 * new whole masks are written with menuEffects_editParam(): with no SEQ
 * hold they become the retained masks (fanned out to same-type masked
 * Scenes, AutoSave marked); during a hold both mask lanes are locked on
 * every held step with the same values, so all held steps end up equal.
 * The masks being edited are the ones shown (menuEffects_shownParam(): the
 * last step held's locks during a hold, otherwise the retained masks).
 * Why: CrumpBit's masks are edited by SELECT, not by sequential values
 * (user specification). Inputs: button 0..7, shift, pressed (the page only
 * calls on press). Output: EFFECT_UI_SHOW_HOME, so the page jumps to the
 * overlay and re-renders the LEDs; 0 for anything it does not handle.
 * Accessor: menuEffects_hookSelect() from buttonHandler's FX SELECT paths.
 * Affiliates: menuEffects_shownParam(), menuEffects_editParam(),
 * menu_effectShowHome(), effects_setParameter(), effects_setSeqLaneLock().
 */
static uint8_t crumpBit_uiSelect(uint8_t button, uint8_t shift,
                                 uint8_t pressed)
{
    uint8_t off;
    uint8_t invert;
    uint8_t bit;
    uint8_t next;

    if (!pressed || button >= CRUMPBIT_BIT_COUNT)
        return 0u;
    off = menuEffects_shownParam(CRUMPBIT_PARAM_BIT_OFF);
    invert = menuEffects_shownParam(CRUMPBIT_PARAM_BIT_INVERT);
    bit = (uint8_t)(1u << button);
    if (shift) {
        next = CRUMPBIT_BIT_NORMAL;
    } else {
        switch (crumpBit_bitState(off, invert, bit)) {
        case CRUMPBIT_BIT_NORMAL: next = CRUMPBIT_BIT_OFF;    break;
        case CRUMPBIT_BIT_OFF:    next = CRUMPBIT_BIT_INVERT; break;
        default:                  next = CRUMPBIT_BIT_NORMAL; break;
        }
    }
    off = (uint8_t)((off & (uint8_t)~bit) |
                    ((next == CRUMPBIT_BIT_OFF) ? bit : 0u));
    invert = (uint8_t)((invert & (uint8_t)~bit) |
                       ((next == CRUMPBIT_BIT_INVERT) ? bit : 0u));
    (void)menuEffects_editParam(CRUMPBIT_PARAM_BIT_OFF, off);
    (void)menuEffects_editParam(CRUMPBIT_PARAM_BIT_INVERT, invert);
    return EFFECT_UI_SHOW_HOME;
}

/*
 * LED hook: the SELECT row shows the eight data lines.
 *
 * What: SELECT LED n is on when bit n-1 is off or inverted, and off when it
 * is normal. The source is menuEffects_shownParam(), so during a SEQ hold
 * the LEDs recover the last step held's locked masks (Q13), and otherwise
 * the retained masks. Why: user specification; the page's own "active
 * SELECT" LED is suppressed because this type sets
 * EFFECT_UI_FLAG_OWNS_SELECT_LEDS. Inputs: none. Output: 8 led_setValue()
 * calls. Accessors: menuEffects_renderLeds() (after each SEQ-row repaint),
 * menuEffects_renderSelectLeds() (page entry, cursor moves, SELECT edits),
 * and menuEffects_service() (hold transitions). Affiliates: ledHandler.
 */
static void crumpBit_uiRenderLeds(void)
{
    const uint8_t used =
        (uint8_t)(menuEffects_shownParam(CRUMPBIT_PARAM_BIT_OFF) |
                  menuEffects_shownParam(CRUMPBIT_PARAM_BIT_INVERT));
    uint8_t i;

    for (i = 0u; i < CRUMPBIT_BIT_COUNT; i++)
        led_setValue((uint8_t)((used >> i) & 1u),
                     (uint8_t)(LED_PART_SELECT1 + i));
}

/*
 * Row-0 hook: paint the overlay's data-line row.
 *
 * What: writes `- - - - - - - -` into columns 0..14. Column 2n shows bit n
 * (LSB left): `-` normal, `0` off, `!` inverted; odd columns are spaces.
 * Column 15 is left alone, so the page's scroll marker (`>`) stays. The
 * source is menuEffects_shownParam() (the last step held's masks during a
 * hold). Why: user specification. The overlay has no parameter names, and
 * this row replaces them. Inputs: sub-page and screen (unused: only one
 * screen is flagged in crumpBit_layout.custom_row0) and the 16-byte row.
 * Output: row0[0..14]. Accessor: menuEffects_paintRow0() from the compact
 * branch of menu_repaintGeneric(). Affiliates: crumpBit_layout, C5, C8.
 */
static void crumpBit_uiPaintRow0(uint8_t sub_page, uint8_t screen,
                                 char *row0)
{
    static const char glyph[3] = { '-', '0', '!' };
    const uint8_t off = menuEffects_shownParam(CRUMPBIT_PARAM_BIT_OFF);
    const uint8_t invert = menuEffects_shownParam(CRUMPBIT_PARAM_BIT_INVERT);
    uint8_t i;

    (void)sub_page;
    (void)screen;
    for (i = 0u; i < CRUMPBIT_BIT_COUNT; i++) {
        row0[2u * i] = glyph[crumpBit_bitState(off, invert,
                                               (uint8_t)(1u << i))];
        if (i + 1u < CRUMPBIT_BIT_COUNT)
            row0[2u * i + 1u] = ' ';
    }
}

/*
 * Format hook: CrumpBit value text on the Effect page (Q7).
 *
 * What:
 * - `sub` shows `dly` (CRUMPBIT_SUBTYPE_DELAY); an unknown value shows `---`.
 * - `rte` shows the snapped step division (`/16`, `16t`, `1br` ...) when the
 *   shown Sync is on. crumpBit_syncDivision() applies the same rule the DSP
 *   uses, at the current tempo.
 * - Everything else returns 0 (generic dtype formatting).
 * Why: the raw 0..127 Rate stays underneath (pots, storage, automation,
 * locks, LFO), and only the Effect page shows the division. The STEP
 * automation page never calls this hook and keeps raw numbers.
 * Inputs: row index, the value being shown (retained, Morph or held), and a
 * 3-character output. Output: nonzero when out[0..2] was written.
 * Accessors: menuEffects_formatParamValue3() via menuEffects_formatValue3()
 * (compact cells), C6 (full view), C9 (held values). Affiliates:
 * stepScale_shortName(), seq_getBpm(), menuEffects_shownParam().
 */
static uint8_t crumpBit_uiFormatValue3(uint8_t index, uint8_t value,
                                       char *out)
{
    if (index == CRUMPBIT_PARAM_SUBTYPE) {
        memcpy(out, (value == CRUMPBIT_SUBTYPE_DELAY) ? "dly" : "---", 3u);
        return 1u;
    }
    if (index == CRUMPBIT_PARAM_RATE &&
        menuEffects_shownParam(CRUMPBIT_PARAM_SYNC) != 0u) {
        memcpy(out, stepScale_shortName(
                        crumpBit_syncDivision(value, seq_getBpm())), 3u);
        return 1u;
    }
    return 0u;
}

/*
 * CrumpBit Effect-page layout (S074_CRUMPBIT_EFFECT.md §5.1).
 *
 * What: SELECT 1 stays manager-owned (typ/out/vol/pan and run/len/scl/mrp).
 * SELECT 2 holds three screens:
 *   0 (home, custom row 0): mix fbk rte mrp  - the overlay, no names;
 *   1:                      mix fbk rte sub;
 *   2:                      syn dpn  -   -.
 * Every other SELECT has 0 screens (the SELECT buttons are bit toggles).
 * The encoder walks SELECT 1 s0 -> s1 -> overlay -> page 2 -> page 3.
 * Why: user specification. Inputs: none (const). Accessors: menuEffects
 * (_cellAt, _screenCount, _screenHasCustomRow0, _home). Affiliates:
 * EFFECT_LAYOUT_CELL_MORPH, crumpBit_uiPaintRow0().
 */
const effect_select_layout_t crumpBit_layout = {
    .screen_count = { 0u, 3u, 0u, 0u, 0u, 0u, 0u, 0u },
    .cells = {
        [1] = {
            { CRUMPBIT_PARAM_MIX, CRUMPBIT_PARAM_FEEDBACK,
              CRUMPBIT_PARAM_RATE, EFFECT_LAYOUT_CELL_MORPH },
            { CRUMPBIT_PARAM_MIX, CRUMPBIT_PARAM_FEEDBACK,
              CRUMPBIT_PARAM_RATE, CRUMPBIT_PARAM_SUBTYPE },
            { CRUMPBIT_PARAM_SYNC, CRUMPBIT_PARAM_DLY_PAN,
              EFFECT_LANE_NONE, EFFECT_LANE_NONE },
            { EFFECT_LANE_NONE, EFFECT_LANE_NONE,
              EFFECT_LANE_NONE, EFFECT_LANE_NONE },
        },
    },
    .custom_row0 = { 0u, 0x01u, 0u, 0u, 0u, 0u, 0u, 0u },
    .home_sub_page = 1u,
    .home_screen = 0u,
};

/*
 * CrumpBit page hooks.
 *
 * What: SELECT toggles bits (returns EFFECT_UI_SHOW_HOME), render_leds owns
 * the SELECT row, paint_row0 draws the overlay row, format_value3 labels
 * `sub` and Sync. TRACK and BAR keep the default page behaviour (NULL).
 * Why: the registry-driven hook contract (E2) keeps type UI out of menu.c.
 * Accessor: the registry row (M3). Affiliates: menuEffects hook
 * dispatchers, buttonHandler FX SELECT paths.
 */
const effect_ui_hooks_t crumpBit_ui = {
    .select = crumpBit_uiSelect,
    .track = NULL,
    .bar = NULL,
    .render_leds = crumpBit_uiRenderLeds,
    .paint_row0 = crumpBit_uiPaintRow0,
    .format_value3 = crumpBit_uiFormatValue3,
    .flags = EFFECT_UI_FLAG_OWNS_SELECT_LEDS,
};
```

---

### N3 — `Core/DSP/Effects/CrumpBit/CrumpBitEffect.h` — ADD file (Stage 1)

```c
/* <standard LXR02 file header: Core/DSP/Effects/CrumpBit/CrumpBitEffect.h> */

#ifndef CRUMP_BIT_EFFECT_H_
#define CRUMP_BIT_EFFECT_H_

#include "EffectsManager.h"

/*
 * CrumpBit loop geometry (S074_CRUMPBIT_EFFECT.md §3.4).
 *
 * CRUMPBIT_DELAY_MAX_SAMPLES: the longest delay, 1.60 s at 44,108 Hz. Rate 0
 * and every Sync division are clamped to it. It is fixed (not
 * share-dependent) so a Scene sounds the same whatever voice units future
 * instruments claim.
 * CRUMPBIT_BUFFER_BYTES: the loop length actually used, 1.60 s plus a
 * 2-sample interpolation guard, rounded up to 32 B. One byte per 8-bit mono
 * sample. It must stay <= the minimum Effect share (73,632 B with all
 * twelve voice units claimed). It is declared as buffer_min_bytes and
 * buffer_pref_bytes in the registry row.
 * Accessors: CrumpBitEffect.c, EffectsManager.c (registry row).
 */
#define CRUMPBIT_DELAY_MAX_SAMPLES   70573.0f
#define CRUMPBIT_BUFFER_BYTES        70592u

/*
 * CrumpBit runtime (56 B, a member of EffectsManager's DTCM runtime union).
 *
 * What: the per-sample DSP state and the last resolved row values. Audio
 * (the 8-bit loop) lives in the FxBuffer Effect share, never here.
 * - length: the loop length in samples/bytes (0 = not seated yet);
 * - write_pos: the next loop index to write (0..length-1);
 * - valid: samples written since the loop was last treated as empty,
 *   saturating at length. Reads further back than this are muted, which
 *   gives "clear unless you adopt" without a bulk clear;
 * - delay: the current delay in samples, gliding towards the per-block
 *   target (tape style);
 * - mix, feedback, gain_l, gain_r: block-ramp values (end of the previous
 *   block);
 * - ac_x_*, ac_y_*: the AC-coupling (one-pole high-pass) state per channel;
 * - bit_off .. pan_raw: the last row values from write_param();
 * - primed: 0 until the first block sets delay directly to its target (no
 *   glide on entry).
 * Why here: EffectsManager zeroes the union and calls init() when the type
 * becomes active; the union is 76 B (StereoFilter), so 56 B needs no RAM.
 * Accessors: CrumpBitEffect.c only; no other module may keep a pointer.
 * Affiliates: effects_runtime_t, M4 (size guard), EFFECTS_MIXER_DSP_REFERENCE.md §4.2.
 */
typedef struct {
    uint32_t length;
    uint32_t write_pos;
    uint32_t valid;
    float delay;
    float mix;
    float feedback;
    float gain_l;
    float gain_r;
    float ac_x_l;
    float ac_x_r;
    float ac_y_l;
    float ac_y_r;
    uint8_t bit_off;
    uint8_t bit_invert;
    uint8_t mix_raw;
    uint8_t feedback_raw;
    uint8_t rate;
    uint8_t sync;
    uint8_t pan_raw;
    uint8_t primed;
} CrumpBitRuntime;

/* DSP operations for the registry row (init, export, write, process, share). */
extern const effect_type_ops_t crumpBit_ops;

/*
 * Free-running delay for one Rate value.
 *
 * What: t = 1.60 s * 80^(-rate/127) in samples: rate 0 = 1.60 s (70,573),
 * rate 127 = 20 ms (882), about 20 steps per octave of time. Higher rate
 * means faster tape and a shorter delay (Q5). Why shared: the DSP and the
 * Effect-page Sync label must use one curve. Input: rate 0..127 (larger
 * values are treated as 127). Output: delay in samples.
 * Accessors: crumpBit_syncDivision(), CrumpBitEffect.c per-block target.
 * Affiliates: CRUMPBIT_DELAY_MAX_SAMPLES.
 */
float crumpBit_rateSamples(uint8_t rate);

/*
 * Step-scale division that Sync snaps a Rate to (Q7).
 *
 * What: among the shared StepScale divisions (/64 .. 2br, 96 PPQ ticks)
 * whose length at `bpm` fits CRUMPBIT_DELAY_MAX_SAMPLES, returns the index
 * nearest in log time to crumpBit_rateSamples(rate). It uses the bracketing
 * pair and the geometric-mean rule: no logarithms, and a constant 14-step
 * loop. If no division fits (below about 4 BPM) it returns 0, and the DSP
 * clamps that length to the maximum. Why shared: the Effect-page label must
 * show exactly the division the DSP plays. Inputs: rate 0..127, bpm from
 * seq_getBpm() (0 is treated as 1). Output: a StepScale index 0..13.
 * Accessors: crumpBit_uiFormatValue3() (label), CrumpBitEffect.c (target).
 * Affiliates: stepScale_ticks(), stepScale_shortName().
 */
uint8_t crumpBit_syncDivision(uint8_t rate, uint16_t bpm);

#endif /* CRUMP_BIT_EFFECT_H_ */
```

---

### N4 — `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c` — ADD file (Stage 1)

```c
/* <standard LXR02 file header: Core/DSP/Effects/CrumpBit/CrumpBitEffect.c>
 *
 * CrumpBit DSP (contract in CrumpBitEffect.h; specification
 * S074_CRUMPBIT_EFFECT.md §3). Compiled with -Ofast (Makefile MK4).
 * Every stage runs on every sample whatever its settings (constant CPU).
 */

#include "CrumpBitEffect.h"
#include "CrumpBitParameters.h"
#include "StepScale.h"
#include "sequencer.h"
#include <math.h>

/*
 * Private DSP constants (S074_CRUMPBIT_EFFECT.md §3).
 *
 * CRUMPBIT_SAMPLE_RATE_HZ   codec rate used for every time conversion.
 * CRUMPBIT_LSB              one 8-bit step, 1/128 of int16 full scale.
 * CRUMPBIT_RATE_LN_SPAN     ln(1.60 s / 0.020 s) = ln 80, the Rate curve.
 * CRUMPBIT_SAMPLES_PER_TICK samples per 96-PPQ tick at 1 BPM: 44,108*60/96.
 * CRUMPBIT_GLIDE_K          one-pole tape glide, tau 150 ms:
 *                           1 - exp(-1 / (0.15 * 44,108)).
 * CRUMPBIT_AC_R             AC coupling pole, about 10 Hz:
 *                           1 - 2*pi*10 / 44,108.
 * CRUMPBIT_FEEDBACK_MAX     feedback gain at row value 127 (bounded < 1).
 * CRUMPBIT_LOOP_MIN         smallest usable loop; below it the block passes
 *                           through untouched (cannot happen with the
 *                           guaranteed minimum share).
 */
#define CRUMPBIT_SAMPLE_RATE_HZ    44108u
#define CRUMPBIT_LSB               (1.0f / 128.0f)
#define CRUMPBIT_RATE_LN_SPAN      4.3820266f
#define CRUMPBIT_SAMPLES_PER_TICK  27567.5f
#define CRUMPBIT_GLIDE_K           1.5113e-4f
#define CRUMPBIT_AC_R              0.9985755f
#define CRUMPBIT_FEEDBACK_MAX      0.99f
#define CRUMPBIT_LOOP_MIN          4u
#define CRUMPBIT_DIVISION_NONE     0xFFu

_Static_assert(sizeof(CrumpBitRuntime) == 56u,
               "CrumpBitRuntime size is recorded in the S074 schedule and "
               "must stay inside the 76 B Effect union");
_Static_assert(CRUMPBIT_BUFFER_BYTES >=
               (uint32_t)CRUMPBIT_DELAY_MAX_SAMPLES + 2u,
               "the loop must hold the longest delay plus the interpolation "
               "guard");

/*
 * Simulated bipolar 8-bit ADC (offset binary).
 *
 * What: clamps the normalised sample to +-1.0 full scale and converts it to
 * a code 0..255 with round-half-up. Silence is code 128 (mid-tread); +1.0
 * (256 before the fold) becomes 255. Why: the front end simulates an 8-bit
 * audio converter (Q1). The same quantiser re-records the delay loop.
 * Branch-free: two compare-selects, one multiply-add, one convert, one
 * shift-subtract. Input: float sample. Output: code 0..255.
 * Callers: crumpBit_process(). Affiliates: CRUMPBIT_LSB (the decode side).
 */
static inline uint32_t crumpBit_adc(float x)
{
    uint32_t code;

    x = (x > 1.0f) ? 1.0f : x;
    x = (x < -1.0f) ? -1.0f : x;
    code = (uint32_t)(x * 128.0f + 128.5f);
    return code - (code >> 8);
}

float crumpBit_rateSamples(uint8_t rate)
{
    const uint8_t r = (rate > 127u) ? 127u : rate;

    return CRUMPBIT_DELAY_MAX_SAMPLES *
           expf(-(float)r * (CRUMPBIT_RATE_LN_SPAN / 127.0f));
}

/*
 * Nearest fitting division for one free-running length.
 *
 * What: walks the 14 ascending StepScale divisions and keeps the longest
 * one <= target (low) and the shortest one > target (high), skipping any
 * longer than CRUMPBIT_DELAY_MAX_SAMPLES. It picks low when
 * target^2 < low*high (target below the pair's geometric mean), which is
 * nearest in log time. Why: one implementation for the DSP and the label.
 * Inputs: target length and samples per tick at the current tempo. Output:
 * a StepScale index; 0 if nothing fits. Callers: crumpBit_syncDivision(),
 * crumpBit_targetSamples(). Affiliates: stepScale_ticks().
 */
static uint8_t crumpBit_divisionFor(float target, float per_tick)
{
    uint8_t low = CRUMPBIT_DIVISION_NONE;
    uint8_t high = CRUMPBIT_DIVISION_NONE;
    float t_low = 0.0f;
    float t_high = 0.0f;
    uint8_t i;

    for (i = 0u; i < STEP_SCALE_COUNT; i++) {
        const float t = (float)stepScale_ticks(i) * per_tick;

        if (t > CRUMPBIT_DELAY_MAX_SAMPLES)
            continue;
        if (t <= target) {
            low = i;
            t_low = t;
        } else if (high == CRUMPBIT_DIVISION_NONE) {
            high = i;
            t_high = t;
        }
    }
    if (low == CRUMPBIT_DIVISION_NONE)
        return (high == CRUMPBIT_DIVISION_NONE) ? 0u : high;
    if (high == CRUMPBIT_DIVISION_NONE)
        return low;
    return (target * target < t_low * t_high) ? low : high;
}

uint8_t crumpBit_syncDivision(uint8_t rate, uint16_t bpm)
{
    const float per_tick =
        CRUMPBIT_SAMPLES_PER_TICK / (float)(bpm ? bpm : 1u);

    return crumpBit_divisionFor(crumpBit_rateSamples(rate), per_tick);
}

/*
 * Per-block delay target in samples.
 *
 * What: computes both the free-running length and the snapped division
 * every block (constant control cost, whatever Sync is), selects one by
 * `sync`, and clamps it to CRUMPBIT_DELAY_MAX_SAMPLES. A tempo change
 * therefore moves the target, and the glide bends the pitch like a tape
 * machine. Inputs: row values rate and sync, and bpm (seq_getBpm(), which
 * also follows MIDI and pulse clock). Output: target length in samples.
 * Caller: crumpBit_process(). Affiliates: crumpBit_divisionFor(),
 * stepScale_ticks().
 */
static float crumpBit_targetSamples(uint8_t rate, uint8_t sync, uint16_t bpm)
{
    const float per_tick =
        CRUMPBIT_SAMPLES_PER_TICK / (float)(bpm ? bpm : 1u);
    const float free_run = crumpBit_rateSamples(rate);
    const float synced = (float)stepScale_ticks(
        crumpBit_divisionFor(free_run, per_tick)) * per_tick;
    const float target = sync ? synced : free_run;

    return (target > CRUMPBIT_DELAY_MAX_SAMPLES)
        ? CRUMPBIT_DELAY_MAX_SAMPLES : target;
}

/*
 * Seat the loop in the current Effect share.
 *
 * What: the loop length is min(share bytes, CRUMPBIT_BUFFER_BYTES). When it
 * differs from the seated length (first block after init, or a share
 * change), the loop is re-seated: write position 0 and valid 0, so the old
 * content is muted rather than read with the wrong geometry. The share
 * base is always the arena bottom (fxbuf_effectShare() offset 0), so the
 * length is the only thing that can move. Inputs: runtime, share (may be
 * NULL). Output: the loop length (0 if no share). Callers:
 * crumpBit_process() every block, crumpBit_bufferChanged().
 * Affiliates: FxBuffer share contract (EFFECTS_MIXER_DSP_REFERENCE.md §5.2).
 */
static uint32_t crumpBit_seat(CrumpBitRuntime *rt, const fx_share_t *share)
{
    uint32_t len = share ? share->bytes : 0u;

    if (len > CRUMPBIT_BUFFER_BYTES)
        len = CRUMPBIT_BUFFER_BYTES;
    if (len != rt->length) {
        rt->length = len;
        rt->write_pos = 0u;
        rt->valid = 0u;
    }
    return len;
}

/*
 * Initialise a newly live CrumpBit runtime ("clear unless you adopt").
 *
 * What: the manager has already zeroed the union. This marks the loop
 * unseated and unprimed, so the first block seats the loop, mutes every
 * unwritten sample through `valid`, and starts the delay at its target.
 * The handoff is not adopted in v1 (Q19): re-entering CrumpBit never plays
 * a previous owner's tail. Nothing is cleared in bulk (no foreground spike).
 * Inputs: runtime, handoff (unused). Output: a runtime ready for
 * write_param() (EffectsManager forces every row on the next service
 * pass). Caller: effects_switchRuntime(). Affiliates: crumpBit_seat(),
 * FXBUF_STATE_EFFECT_WRITTEN.
 */
static void crumpBit_init(void *rt_void, const fxbuf_handoff_t *handoff)
{
    CrumpBitRuntime *rt = (CrumpBitRuntime *)rt_void;

    (void)handoff;
    rt->length = 0u;
    rt->write_pos = 0u;
    rt->valid = 0u;
    rt->primed = 0u;
}

/*
 * Describe the loop in the handoff when CrumpBit is exited or refreshed.
 *
 * What: records 1 channel, 8 bits, 44,108 Hz, the write position and the
 * integer read position in pointer slot 12
 * (FXBUF_HANDOFF_EFFECT_POINTER_BASE), and FXBUF_STATE_EFFECT_WRITTEN when
 * any sample was written. The positions are share-relative, which is
 * arena-relative because the share starts at the arena bottom. The manager
 * wrote the type and an io-derived channel count first; the loop is mono,
 * so this overrides the count. Why: the FxBuffer contract; the next owner
 * decides whether to adopt or clear. Inputs: runtime, the record from
 * fxbuf_handoffBeginExit(). Output: the Effect fields of the record.
 * Callers: effects_exportHandoff() (type switch, M5; same-type Scene switch,
 * M6). Affiliates: fxbuf_handoff_t.
 */
static void crumpBit_exportHandoff(const void *rt_void, fxbuf_handoff_t *out)
{
    const CrumpBitRuntime *rt = (const CrumpBitRuntime *)rt_void;
    float rp;

    if (!rt || !out)
        return;
    out->effect_channels = 1u;
    out->effect_bits = 8u;
    out->effect_rate_hz = (uint16_t)CRUMPBIT_SAMPLE_RATE_HZ;
    if (rt->length == 0u)
        return;
    rp = (float)rt->write_pos - rt->delay;
    rp += (rp < 0.0f) ? (float)rt->length : 0.0f;
    out->write_offset[FXBUF_HANDOFF_EFFECT_POINTER_BASE] = rt->write_pos;
    out->read_offset[FXBUF_HANDOFF_EFFECT_POINTER_BASE] =
        (uint32_t)rp % rt->length;
    if (rt->valid != 0u)
        out->state_flags |= FXBUF_STATE_EFFECT_WRITTEN;
}

/*
 * Store one resolved row value.
 *
 * What: keeps the raw 0..255 value of rows 3..10. Every conversion (masks,
 * gains, curve, division) happens once per block in process(), so an LFO
 * that rewrites a row every block costs the same as a static setting. The
 * sub-type row is ignored (only `dly` exists). Rows 0..2 are applied by
 * EffectsManager and never arrive here. Inputs: runtime, row index, value
 * (already clamped to the descriptor maximum). Output: runtime fields.
 * Caller: effects_service(). Affiliates: crumpBit_descriptors.
 */
static void crumpBit_writeParam(void *rt_void, uint8_t index, uint8_t value)
{
    CrumpBitRuntime *rt = (CrumpBitRuntime *)rt_void;

    switch (index) {
    case CRUMPBIT_PARAM_BIT_OFF:    rt->bit_off = value;               break;
    case CRUMPBIT_PARAM_BIT_INVERT: rt->bit_invert = value;            break;
    case CRUMPBIT_PARAM_MIX:        rt->mix_raw = value;               break;
    case CRUMPBIT_PARAM_FEEDBACK:   rt->feedback_raw = value;          break;
    case CRUMPBIT_PARAM_RATE:       rt->rate = value;                  break;
    case CRUMPBIT_PARAM_SYNC:       rt->sync = (uint8_t)(value != 0u); break;
    case CRUMPBIT_PARAM_DLY_PAN:    rt->pan_raw = value;               break;
    default:                                                           break;
    }
}

/*
 * Process one Effect bus block in place (S074_CRUMPBIT_EFFECT.md §3).
 *
 * What, per sample, for both channels:
 *   8-bit ADC -> data lines ((c & keep) ^ invert) -> DAC ((c - 128)/128)
 *   -> AC coupling (h = y - y[-1] + R*h[-1]).
 * Then the mono feed 0.5*(hL + hR) goes into the tape loop:
 *   the delay glides to the block target; the fractional read uses linear
 *   interpolation and is muted beyond `valid`; the write is
 *   8-bit(mono + fb*wet), re-quantised by the same ADC.
 * Output: out = h + mix*(gain*wet - h) (a linear crossfade; the gains
 * attenuate only). mix, fb and both gains ramp linearly across the block.
 * Why: the CrumpBit specification. Every operation runs on every sample
 * whatever the settings (constant CPU): mix 0 still runs the loop, and
 * masks of 0 still run the logic.
 * Inputs: the runtime and io (l and r normalised floats, 1.0 = int16 full
 * scale; frames = 32; share = the Effect share). A NULL r (never supplied
 * for this stereo type) aliases l, and right is written before left so l
 * keeps the left result. Output: io->l and io->r in place, and the loop
 * bytes in the share.
 * Caller: effects_process() from mixer_calcNextSampleBlock(), after
 * effects_service(). Affiliates: crumpBit_adc(), crumpBit_targetSamples(),
 * crumpBit_seat(), seq_getBpm().
 */
static void crumpBit_process(void *rt_void, effect_io_t *io)
{
    CrumpBitRuntime *rt = (CrumpBitRuntime *)rt_void;
    float *left;
    float *right;
    int8_t *loop;
    uint32_t len;
    uint32_t keep;
    uint32_t invert;
    uint32_t wp;
    uint32_t valid;
    float flen;
    float target;
    float delay;
    float mix;
    float fb;
    float gain_l;
    float gain_r;
    float mix_to;
    float fb_to;
    float gain_l_to;
    float gain_r_to;
    float mix_inc;
    float fb_inc;
    float gain_l_inc;
    float gain_r_inc;
    float ac_xl;
    float ac_xr;
    float ac_yl;
    float ac_yr;
    float per_frame;
    uint8_t n;

    if (!rt || !io || !io->l || !io->share || !io->share->base ||
        io->frames == 0u)
        return;
    len = crumpBit_seat(rt, io->share);
    if (len < CRUMPBIT_LOOP_MIN)
        return;
    left = io->l;
    right = io->r ? io->r : io->l;
    loop = (int8_t *)io->share->base;
    flen = (float)len;

    /* Data lines: invert wins over off (spec §3.2). */
    invert = rt->bit_invert;
    keep = (uint32_t)~((uint32_t)rt->bit_off & ~invert) & 0xFFu;

    /* Tape target for this block; the first block starts on it. */
    target = crumpBit_targetSamples(rt->rate, rt->sync, seq_getBpm());
    if (target > flen - 2.0f)
        target = flen - 2.0f;
    if (!rt->primed) {
        rt->delay = target;
        rt->primed = 1u;
    }

    /* Block ramps (spec §3.5): 0..127 rows to gains. */
    per_frame = 1.0f / (float)io->frames;
    mix_to = (float)rt->mix_raw * (1.0f / 127.0f);
    fb_to = (float)rt->feedback_raw * (CRUMPBIT_FEEDBACK_MAX / 127.0f);
    gain_l_to = (rt->pan_raw <= 64u) ? 1.0f
        : (float)(127u - rt->pan_raw) * (1.0f / 63.0f);
    gain_r_to = (rt->pan_raw >= 64u) ? 1.0f
        : (float)rt->pan_raw * (1.0f / 64.0f);
    mix = rt->mix;
    fb = rt->feedback;
    gain_l = rt->gain_l;
    gain_r = rt->gain_r;
    mix_inc = (mix_to - mix) * per_frame;
    fb_inc = (fb_to - fb) * per_frame;
    gain_l_inc = (gain_l_to - gain_l) * per_frame;
    gain_r_inc = (gain_r_to - gain_r) * per_frame;

    /* Local copies: loop[] is int8_t and may alias anything. */
    delay = rt->delay;
    wp = rt->write_pos;
    valid = rt->valid;
    ac_xl = rt->ac_x_l;
    ac_xr = rt->ac_x_r;
    ac_yl = rt->ac_y_l;
    ac_yr = rt->ac_y_r;

    for (n = 0u; n < io->frames; n++) {
        const uint32_t code_l = (crumpBit_adc(left[n]) & keep) ^ invert;
        const uint32_t code_r = (crumpBit_adc(right[n]) & keep) ^ invert;
        const float yl = (float)((int32_t)code_l - 128) * CRUMPBIT_LSB;
        const float yr = (float)((int32_t)code_r - 128) * CRUMPBIT_LSB;
        const float hl = yl - ac_xl + CRUMPBIT_AC_R * ac_yl;
        const float hr = yr - ac_xr + CRUMPBIT_AC_R * ac_yr;
        float rp;
        float frac;
        float wet;
        uint32_t i0;
        uint32_t i1;

        ac_xl = yl;
        ac_yl = hl;
        ac_xr = yr;
        ac_yr = hr;
        mix += mix_inc;
        fb += fb_inc;
        gain_l += gain_l_inc;
        gain_r += gain_r_inc;

        /* Tape glide and fractional read (wrap by compare, not modulo). */
        delay += (target - delay) * CRUMPBIT_GLIDE_K;
        rp = (float)wp - delay;
        rp += (rp < 0.0f) ? flen : 0.0f;
        i0 = (uint32_t)rp;
        frac = rp - (float)i0;
        i0 -= (i0 >= len) ? len : 0u;
        i1 = i0 + 1u;
        i1 -= (i1 >= len) ? len : 0u;
        wet = ((float)loop[i0] +
               frac * (float)(loop[i1] - loop[i0])) * CRUMPBIT_LSB;
        wet = ((delay + 2.0f) <= (float)valid) ? wet : 0.0f;

        /* Re-record through the 8-bit converter; feedback stays bounded. */
        loop[wp] = (int8_t)((int32_t)crumpBit_adc(0.5f * (hl + hr) +
                                                 fb * wet) - 128);
        wp++;
        wp -= (wp >= len) ? len : 0u;
        valid += (valid < len) ? 1u : 0u;

        right[n] = hr + mix * (gain_r * wet - hr);
        left[n] = hl + mix * (gain_l * wet - hl);
    }

    rt->delay = delay;
    rt->write_pos = wp;
    rt->valid = valid;
    rt->ac_x_l = ac_xl;
    rt->ac_x_r = ac_xr;
    rt->ac_y_l = ac_yl;
    rt->ac_y_r = ac_yr;
    /* Land exactly on the targets so ramp rounding never accumulates. */
    rt->mix = mix_to;
    rt->feedback = fb_to;
    rt->gain_l = gain_l_to;
    rt->gain_r = gain_r_to;
}

/*
 * Share-change notification (voice units claimed or released).
 *
 * What: re-seats the loop. If the usable length changed, the old content is
 * muted through `valid`. A forced re-resolution follows from the manager.
 * Why: the FxBuffer contract (share geometry can change between blocks).
 * With CRUMPBIT_BUFFER_BYTES below the minimum share this never changes
 * the length in practice, but it keeps the contract. Inputs: runtime and
 * the new share. Output: runtime seat fields. Caller:
 * effects_onShareChanged(). Affiliates: crumpBit_seat().
 */
static void crumpBit_bufferChanged(void *rt_void, const fx_share_t *share)
{
    (void)crumpBit_seat((CrumpBitRuntime *)rt_void, share);
}

/*
 * CrumpBit operations for the registry row. effective_max is NULL: no row
 * is BUFFER_DEPENDENT, because the delay range is fixed (Q5).
 */
const effect_type_ops_t crumpBit_ops = {
    crumpBit_init,
    crumpBit_exportHandoff,
    crumpBit_writeParam,
    crumpBit_process,
    crumpBit_bufferChanged,
    NULL,
};
```

Notes on N4:

- **Constant cost.** Every `?:` in the loop is a compare-select, the same
  work on every sample. The only per-block costs are one `expf`, one divide
  for the tick length, one divide for the ramp step, and the 14-step
  division walk.
- **`seq_getBpm()`** is a plain read of `seq_tempo` (foreground; it is also
  written by the clock-sync paths). No locking is needed for a 16-bit read.

---

## 4. EffectsManager contract (E1–E3, M1–M6)

### E1 — `Core/DSP/Effects/EffectsManager.h` L61–63 — MODIFY

**Current:**

```c
/* Registry ids are append-only; persisted identity is the three-byte token. */
#define EFFECT_TYPE_STEREO_FILTER        1u
#define EFFECT_TYPE_COUNT                2u
```

**New:**

```c
/*
 * Registry ids are append-only; persisted identity is the three-byte token.
 * EFFECT_TYPE_CRUMPBIT (S074): the first buffer-using type (`cbt`, 8-bit
 * data lines and an 8-bit tape delay in the FxBuffer share).
 */
#define EFFECT_TYPE_STEREO_FILTER        1u
#define EFFECT_TYPE_CRUMPBIT             2u
#define EFFECT_TYPE_COUNT                3u
```

---

### E2 — `Core/DSP/Effects/EffectsManager.h` L117–136 — MODIFY (page hooks)

Replace the comment and struct (L117–136) with:

```c
/*
 * Optional per-type Effect-page hooks (Session 072 step 7; plan §13.6;
 * extended in S074 for CrumpBit).
 *
 * What: lets a type take over SELECT, TRACK or BAR gestures, add LED
 * rendering, paint a type-owned top row, and label its own values on the
 * Effect page.
 * - select/track/bar(button, shift, pressed) return nonzero when handled;
 *   zero falls back to the default page behaviour. select may return
 *   EFFECT_UI_SHOW_HOME: handled, and the page must show the layout's home
 *   screen (menu_effectShowHome()). Any nonzero select return abandons an
 *   open `typ` browse (S074).
 * - render_leds() runs after the page has drawn its own LEDs. With
 *   EFFECT_UI_FLAG_OWNS_SELECT_LEDS it is also the only writer of the SELECT
 *   LED row while the type is on the page: the page's "active SELECT" LED
 *   is suppressed, and menuEffects_renderSelectLeds() calls this instead.
 * - paint_row0(sub_page, screen, row0) (S074) writes columns 0..14 of the
 *   compact top row for screens flagged in select_layout->custom_row0;
 *   column 15 keeps the page's scroll marker. Those screens show no
 *   automation name markers.
 * - format_value3(index, value, out) (S074) may replace the 3-character
 *   value text of a type row (3..63) on the Effect page only: compact cells,
 *   the full view, and held-step values. It returns nonzero when it wrote
 *   out[0..2]. The STEP automation page and storage always show raw values.
 * - flags (S074): EFFECT_UI_FLAG_*.
 * Rules: hooks run in foreground, must not block, must write retained data
 * only through the EffectsManager edit API (menuEffects_editParam() for
 * held-aware writes), and never touch the filesystem. Any member may be
 * NULL or 0.
 * Accessors: menuEffects_hook*(), _renderLeds(), _renderSelectLeds(),
 * _paintRow0(), _formatParamValue3(), _liveRefreshWanted().
 * Affiliates: buttonHandler FX branches, the registry's ui field,
 * crumpBit_ui.
 */
#define EFFECT_UI_HANDLED                 1u
#define EFFECT_UI_SHOW_HOME               2u
#define EFFECT_UI_FLAG_OWNS_SELECT_LEDS   0x01u

struct effect_ui_hooks {
    uint8_t (*select)(uint8_t button, uint8_t shift, uint8_t pressed);
    uint8_t (*track)(uint8_t track, uint8_t shift, uint8_t pressed);
    uint8_t (*bar)(uint8_t bar, uint8_t shift, uint8_t pressed);
    void (*render_leds)(void);
    void (*paint_row0)(uint8_t sub_page, uint8_t screen, char *row0);
    uint8_t (*format_value3)(uint8_t index, uint8_t value, char *out);
    uint8_t flags;
};
typedef struct effect_ui_hooks effect_ui_hooks_t;
```

No existing hook table needs changing (`flt` and `off` use NULL).

---

### E3 — `Core/DSP/Effects/EffectsManager.h` L138–142 — MODIFY (layout)

**Current:**

```c
/* Optional per-type SELECT layout for the Effect page (NULL = default, §13.2). */
typedef struct {
    uint8_t screen_count[8];
    uint8_t cells[8][4][4];
} effect_select_layout_t;
```

**New:**

```c
/*
 * Optional per-type SELECT layout for the Effect page (NULL = default, §13.2;
 * extended in S074).
 *
 * screen_count[b], cells[b][screen][column]: the screens behind SELECT b+1
 * (up to 4). A cell holds a descriptor index, EFFECT_LANE_NONE (empty), or
 * EFFECT_LAYOUT_CELL_MORPH (the manager `mrp` cell, Scene Effect Morph;
 * S074). Index 0 (SELECT 1) is always manager-owned and ignored here.
 * custom_row0[b] (S074): bit s set = screen s of SELECT b+1 has a
 * type-painted top row (ui->paint_row0) instead of names.
 * home_sub_page, home_screen (S074): the screen a hooked SELECT shows when it
 * returns EFFECT_UI_SHOW_HOME; home_sub_page must be 1..7 and home_screen
 * below that SELECT's screen_count.
 * Accessors: menuEffects_cellAt(), _screenCount(), _screenHasCustomRow0(),
 * _home(). Affiliates: crumpBit_layout.
 */
#define EFFECT_LAYOUT_CELL_MORPH         0xFDu

typedef struct {
    uint8_t screen_count[8];
    uint8_t cells[8][4][4];
    uint8_t custom_row0[8];
    uint8_t home_sub_page;
    uint8_t home_screen;
} effect_select_layout_t;
```

---

### M1 — `Core/DSP/Effects/EffectsManager.c` after L21 — ADD

After `#include "StereoFilterEffect.h"` (L21):

```c
/* S074: CrumpBit (`cbt`), the first buffer-using Effect type. */
#include "CrumpBitParameters.h"
#include "CrumpBitEffect.h"
```

---

### M2 — `Core/DSP/Effects/EffectsManager.c` L49–51 — MODIFY

**Current:**

```c
typedef union {
    StereoFilterRuntime stereo_filter;
} effects_runtime_t;
```

**New:**

```c
typedef union {
    StereoFilterRuntime stereo_filter;
    /* S074: 56 B. Its audio lives in the FxBuffer share, not in this union. */
    CrumpBitRuntime crump_bit;
} effects_runtime_t;
```

---

### M3 — `Core/DSP/Effects/EffectsManager.c` after L91 — ADD (registry row)

After the StereoFilter row's closing `},` (L91) and before `};` (L92):

```c
    /*
     * CrumpBit (`cbt`, S074): stereo in and out. The 8-bit mono tape loop
     * uses CRUMPBIT_BUFFER_BYTES of the FxBuffer Effect share (min = pref;
     * the fixed range fits the minimum share). Lanes 1..9: bit off, bit
     * invert, mix, feedback, rate, sync, vol, pan, delay pan. Lane 0 is
     * Effect Morph. Stage 1 build: replace &crumpBit_layout and &crumpBit_ui
     * with NULL (default page, no hooks).
     */
    {
        crumpBit_token3, crumpBit_abbrev5, crumpBit_full8,
        EFFECT_IO_STEREO_IN | EFFECT_IO_STEREO_OUT,
        crumpBit_descriptors, CRUMPBIT_PARAM_COUNT,
        { EFFECT_LANE_MORPH_SOURCE,
          CRUMPBIT_PARAM_BIT_OFF, CRUMPBIT_PARAM_BIT_INVERT,
          CRUMPBIT_PARAM_MIX, CRUMPBIT_PARAM_FEEDBACK,
          CRUMPBIT_PARAM_RATE, CRUMPBIT_PARAM_SYNC,
          CRUMPBIT_PARAM_LEVEL, CRUMPBIT_PARAM_PAN,
          CRUMPBIT_PARAM_DLY_PAN,
          EFFECT_LANE_NONE, EFFECT_LANE_NONE, EFFECT_LANE_NONE,
          EFFECT_LANE_NONE, EFFECT_LANE_NONE, EFFECT_LANE_NONE },
        &crumpBit_layout, &crumpBit_ops, &crumpBit_ui,
        (uint16_t)sizeof(CrumpBitRuntime),
        CRUMPBIT_BUFFER_BYTES, CRUMPBIT_BUFFER_BYTES
    },
```

---

### M4 — `Core/DSP/Effects/EffectsManager.c` after L95 — ADD

After the StereoFilter `_Static_assert` (L94–95):

```c
_Static_assert(sizeof(CrumpBitRuntime) <= sizeof(effects_runtime_t),
               "Effect runtime union must hold CrumpBit");
/*
 * RAM guard (S074): the DTCM union is 76 B (STORAGE_SRAM_MANIFEST.md). A
 * larger member grows .dtcmz and shrinks the FxBuffer arena by the same
 * amount, which needs RAM approval before it is merged.
 */
_Static_assert(sizeof(effects_runtime_t) == 76u,
               "Effect runtime union size changed: RAM approval required");
```

---

### M5 — `Core/DSP/Effects/EffectsManager.c` L1126–1162 — MODIFY (Stage 3)

Add a helper after `effects_runtimeMember()` (after L1130), and use it in
`effects_switchRuntime()` (L1139–1162).

**Add after L1130:**

```c
/*
 * Export the live type's arena description into a fresh handoff snapshot.
 *
 * What: begins a new FxBuffer handoff record (share bounds and unit owners
 * refreshed, Effect fields reset), stamps the live type and its io-derived
 * channel count, and lets the type describe its arena use through
 * export_handoff. Why: the handoff must be current both when a type exits
 * (type switch) and when a same-type Scene switch keeps the runtime alive
 * (S074 gap 1, EFFECTS_BUS_REFERENCE.md §13 item 1), so one routine serves
 * both. Inputs: effects_state.runtime_type and the runtime union. Output:
 * the handoff record. No runtime or arena byte changes. Callers:
 * effects_switchRuntime(), effects_activateScene(). Affiliates:
 * fxbuf_handoffBeginExit(), crumpBit_exportHandoff().
 */
static void effects_exportHandoff(void)
{
    const effect_registry_entry_t *entry =
        effects_registryEntry(effects_state.runtime_type);
    fxbuf_handoff_t *handoff = fxbuf_handoffBeginExit();

    handoff->effect_type = effects_state.runtime_type;
    handoff->effect_channels = (entry &&
        (entry->io_flags & EFFECT_IO_STEREO_OUT) != 0u) ? 2u :
        ((entry && entry->io_flags != 0u) ? 1u : 0u);
    if (entry && entry->ops && entry->ops->export_handoff)
        entry->ops->export_handoff(effects_runtimeMember(), handoff);
}
```

**Replace L1139–1162 (`effects_switchRuntime()`) with:**

```c
static void effects_switchRuntime(effect_type_id_t incoming)
{
    const effect_registry_entry_t *new_entry = effects_registryEntry(incoming);

    if (!new_entry) {
        incoming = EFFECT_TYPE_OFF;
        new_entry = effects_registryEntry(EFFECT_TYPE_OFF);
    }
    /* The outgoing type describes its arena use before the union clears. */
    effects_exportHandoff();
    memset(&effects_runtime, 0, sizeof(effects_runtime));
    effects_state.runtime_type = incoming;
    if (new_entry && new_entry->ops && new_entry->ops->init)
        new_entry->ops->init(effects_runtimeMember(), fxbuf_handoff());
    effects_state.force_all = 1u;
}
```

This is behaviour-identical to today (the same statements, moved into the
helper). The comment above the function (L1132–1138) is unchanged.

---

### M6 — `Core/DSP/Effects/EffectsManager.c` L1212–1213 — MODIFY (Stage 3, gap 1)

**Current:**

```c
    if (type != effects_state.runtime_type)
        effects_switchRuntime(type);
```

**New:**

```c
    if (type != effects_state.runtime_type) {
        effects_switchRuntime(type);
    } else {
        /*
         * Same-type Scene switch (S074 gap 1): the runtime and its arena
         * content stay live, so a CrumpBit tail keeps ringing into the new
         * Scene's settings (design F6). The handoff record is refreshed
         * without init, so share bounds, unit owners and the type's
         * positions describe the arena as it is now.
         */
        effects_exportHandoff();
    }
```

---

## 5. Makefile (MK1–MK4)

### MK1 — `Makefile` after L39 — ADD

After `-ICore/DSP/Effects/StereoFilter \`:

```make
          -ICore/DSP/Effects/CrumpBit \
```

### MK2 — `Makefile` after L113 — ADD

After `Core/DSP/Effects/StereoFilter/StereoFilterParameters.c \`:

```make
  Core/DSP/Effects/CrumpBit/CrumpBitParameters.c \
```

### MK3 — `Makefile` after L140 — ADD

After `Core/DSP/Effects/StereoFilter/StereoFilterEffect.c \`:

```make
  Core/DSP/Effects/CrumpBit/CrumpBitEffect.c \
```

### MK4 — `Makefile` after L205 — ADD

After the StereoFilter `-Ofast` rule (L201–205):

```make

# CrumpBit DSP (S074) runs with the fast-math policy like StereoFilter; its
# descriptor, layout and page-hook source stays an ordinary -O2 source in SRCS.
$(BUILD)/Core/DSP/Effects/CrumpBit/CrumpBitEffect.o: Core/DSP/Effects/CrumpBit/CrumpBitEffect.c | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS_DSP) $< -o $@
```

(The recipe lines start with a tab.)

---

## 6. menuEffects (H1–H2, P1–P15)

### H1 — `Core/Menu/menuEffects.h` after L115 — ADD

After `void menuEffects_renderLeds(void);` (L115):

```c

/*
 * Type page extensions (S074; used by CrumpBit through EffectsManager's
 * select_layout and effect_ui_hooks_t).
 *
 * menuEffects_screenHasCustomRow0(sub_page): nonzero when the remembered
 *   screen of that SELECT is flagged in select_layout->custom_row0 and the
 *   type has paint_row0. Callers: menu_applyEffectMarkers() (no name
 *   markers there).
 * menuEffects_paintRow0(sub_page, row0): lets the type paint columns 0..14
 *   of the compact top row on such a screen. Caller: menu_repaintGeneric().
 * menuEffects_formatParamValue3(cell, value, dst): the type's value text for
 *   an explicit PARAM value (the format_value3 hook); nonzero when it wrote
 *   dst[0..2]. Callers: menuEffects_formatValue3(), menu.c full view and
 *   held values.
 * menuEffects_renderSelectLeds(sub_page): the SELECT LED row for the page:
 *   the type's render_leds when it owns the row, else the active-SELECT LED.
 *   Callers: menu.c (page entry, cursor moves, repair,
 *   menu_effectShowHome()), buttonHandler FX SELECT.
 * menuEffects_home(sub_page, column): moves the page memory to the layout's
 *   home screen and returns its coordinates; zero when the type has none.
 *   Caller: menu_effectShowHome().
 * menuEffects_liveRefreshWanted(): nonzero when the type labels values
 *   through format_value3 (they may depend on the tempo), so Menu's
 *   live-refresh cadence repaints the page while playing. Caller:
 *   menu_sceneLiveRefreshService().
 * All foreground-only, read-only except _home(), allocation-free.
 */
uint8_t menuEffects_screenHasCustomRow0(uint8_t sub_page);
void menuEffects_paintRow0(uint8_t sub_page, char *row0);
uint8_t menuEffects_formatParamValue3(const menuEffects_cell_t *cell,
                                      uint8_t value, char *dst);
void menuEffects_renderSelectLeds(uint8_t sub_page);
uint8_t menuEffects_home(uint8_t *sub_page, uint8_t *column);
uint8_t menuEffects_liveRefreshWanted(void);
```

---

### H2 — `Core/Menu/menuEffects.h` L117–130 — MODIFY

Replace the hold comment and declarations (L117–130) with:

```c
/*
 * FX-sequencer hold gestures and row rendering (Session 072 step 8; plan
 * §13.4; S074). SEQ presses jump immediately in `sel`; the shared
 * ButtonHandler hold threshold calls menuEffects_seqHoldExpired(), after
 * which sequenceable cells write lane locks across the physically held
 * steps. Non-sequenceable manager cells are ignored while held.
 * S074: the displayed and seeding step is the **last step held** (the most
 * recently pressed SEQ button still down; if it is released while others
 * stay down, the highest-numbered remaining step), so the whole page shows
 * one step. Every held edit writes one value to every held step.
 * The display and LED helpers are foreground-only and keep all Scene writes
 * inside EffectsManager.
 */
void menuEffects_seqButtonPressed(uint8_t step);
void menuEffects_seqHoldExpired(void);
uint8_t menuEffects_seqHoldActive(void);
uint8_t menuEffects_holdEdit(const menuEffects_cell_t *cell, int16_t delta);
uint8_t menuEffects_holdDisplay(const menuEffects_cell_t *cell,
                                uint8_t *value, uint8_t *locked);

/*
 * Held-aware row access for type hooks (S074).
 *
 * menuEffects_shownParam(index): the value the page shows for a row of the
 *   active Scene's Effect: the last step held's lane lock when that row has
 *   a lane locked there, otherwise the retained value (Morph endpoint while
 *   SHIFT shows Morph, for Morphable rows).
 * menuEffects_editParam(index, value): writes a row the way the page edits:
 *   during a SEQ hold, locks the row's lane on every held step with `value`
 *   (nothing is written when the row has no lane, or before the held mask is
 *   known); otherwise sets the retained value through effects_setParameter()
 *   (clamped, fanned out, AutoSave marked). Returns nonzero if a byte
 *   changed.
 * Callers: crumpBit_ui hooks (SELECT, LEDs, row 0, Sync label).
 * Affiliates: effects_getParameter(), effects_getLaneLock(),
 * effects_setParameter(), effects_setSeqLaneLock().
 */
uint8_t menuEffects_shownParam(uint8_t index);
uint8_t menuEffects_editParam(uint8_t index, uint8_t value);
```

---

### P1 — `Core/Menu/menuEffects.c` after L22 — ADD (comment)

Inside the file header, after L22 (`* and a re-press cycles its screens
(S072_ST7 D1).`):

```c
 * Type-owned pages (S074): a layout may place the manager `mrp` cell
 *   (EFFECT_LAYOUT_CELL_MORPH), flag screens whose top row the type paints
 *   (custom_row0), and name a home screen. A type's hooks may take the
 *   SELECT buttons and the SELECT LED row, and label its own values
 *   (format_value3). CrumpBit (`cbt`) is the first user.
```

---

### P2 — `Core/Menu/menuEffects.c` L54–61 — MODIFY (hold state, 0 B)

**Current (L54–61):**

```c
/*
 * FX-sequencer page state (Session 072 step 8; +7 B, S072_ST8 D5).
 *
 * hold_active/hold_mask own the SEQ hold-edit gesture and the physically held
 * steps it last observed. The LED signature fields avoid repainting the FX
 * row when neither the active step nor retained sequence state changed.
 */
static uint8_t menuEffects_holdActive;
```

**New:**

```c
/*
 * FX-sequencer page state (Session 072 step 8; +7 B, S072_ST8 D5; S074
 * repacks the hold byte at no size change).
 *
 * holdState (was holdActive) packs the SEQ hold-edit gesture into one byte:
 *   MENU_FX_HOLD_ACTIVE     the lock editor is open (hold threshold passed);
 *   MENU_FX_HOLD_LAST_VALID the last-held field below is meaningful;
 *   MENU_FX_HOLD_LAST_MASK  the last step held (0..15): the most recently
 *                           pressed SEQ button still down.
 * holdMask is the physically held steps the service last observed. The LED
 * signature fields avoid repainting the FX row when neither the active step
 * nor retained sequence state changed.
 * Why packed: S074 needs the last step held (F3), and the RAM policy
 * allows no new byte without approval. The old flag used 1 bit of this byte.
 * Writers: menuEffects_enter/leave(), _seqHoldExpired(), _service(). Readers:
 * _heldStep(), _seqHoldActive(), _editParam().
 */
#define MENU_FX_HOLD_ACTIVE      0x80u
#define MENU_FX_HOLD_LAST_VALID  0x40u
#define MENU_FX_HOLD_LAST_MASK   0x0Fu
#define MENU_FX_STEP_NONE        0xFFu
static uint8_t menuEffects_holdState;
```

L62–66 (`holdMask` and the LED signature bytes) are unchanged.

---

### P3 — `Core/Menu/menuEffects.c` L160–161 — MODIFY

**Current:**

```c
    } else if (entry->select_layout) {
        index = entry->select_layout->cells[sub_page][screen][column];
```

**New:**

```c
    } else if (entry->select_layout) {
        index = entry->select_layout->cells[sub_page][screen][column];
        /*
         * A layout may place the manager `mrp` cell (S074; CrumpBit's overlay
         * shows Effect Morph at the right). It resolves exactly like SELECT 1
         * screen 1's `mrp`: value, clamp, commit, lane 0 and the double-rate
         * pot all follow the MENU_FX_CELL_MORPH_AMOUNT paths.
         */
        if (index == EFFECT_LAYOUT_CELL_MORPH) {
            out->kind = MENU_FX_CELL_MORPH_AMOUNT;
            return 1u;
        }
```

(The `} else {` at L162 follows unchanged.)

---

### P4 — `Core/Menu/menuEffects.c` L202 — MODIFY

`    menuEffects_holdActive = 0u;` → `    menuEffects_holdState = 0u;`

---

### P5 — `Core/Menu/menuEffects.c` L221–233 — MODIFY (`menuEffects_leave()`)

**New L221–233:**

```c
/*
 * Leaving the page discards the type candidate, Morph view, hold and FX LED
 * layer. S074: a type that owns the SELECT LED row (CrumpBit's bit LEDs)
 * leaves it dark, because the next mode may not repaint that row itself.
 */
void menuEffects_leave(void)
{
    const effect_ui_hooks_t *hooks = menuEffects_entry()->ui;
    uint8_t step;

    menuEffects_typeEdit = 0u;
    menuEffects_showMorphFlag = 0u;
    menuEffects_holdState = 0u;
    menuEffects_holdMask = 0u;
    if (hooks && (hooks->flags & EFFECT_UI_FLAG_OWNS_SELECT_LEDS) != 0u)
        led_clearSelectLeds();
    led_clearActive_step();
    for (step = 0u; step < EFFECT_SEQ_STEP_COUNT; step++)
        led_setBlinkLed((uint8_t)(LED_STEP1 + step), 0u);
}
```

---

### P6 — `Core/Menu/menuEffects.c` L452–475 — MODIFY (`menuEffects_formatValue3()`)

Change the one-line comment at L452 and add the PARAM case before
`return 0u;` (L474):

```c
/*
 * Format manager-owned tokens and type-labelled rows; return zero for generic
 * descriptor formatting. S074: a PARAM cell goes to the type's format_value3
 * hook with its shown value (CrumpBit: `sub` -> `dly`, `rte` -> the Sync
 * division), which covers every compact cell through menu_formatCellValue3().
 */
uint8_t menuEffects_formatValue3(const menuEffects_cell_t *cell, char *dst)
{
    ...                                /* L454–473 unchanged */
    if (cell->kind == MENU_FX_CELL_PARAM)
        return menuEffects_formatParamValue3(cell, (uint8_t)value, dst);
    return 0u;
}
```

---

### P7 — `Core/Menu/menuEffects.c` before L635 — ADD

Between L633 (end of `menuEffects_showMorph()`) and the
`menuEffects_service()` comment at L635:

```c
/*
 * Return the highest-numbered step in a SEQ mask, or MENU_FX_STEP_NONE.
 *
 * What: scans steps 15..0. Why: when several SEQ buttons are first seen in
 * one service pass, or the last step held is released while others stay
 * down, the page needs one deterministic step to show (S074 F3). Input: a
 * 16-bit step mask. Output: 0..15 or MENU_FX_STEP_NONE. Callers:
 * menuEffects_service().
 */
static uint8_t menuEffects_highestStep(uint16_t mask)
{
    uint8_t step;

    for (step = EFFECT_SEQ_STEP_COUNT; step > 0u; step--) {
        if ((mask & (uint16_t)(1u << (step - 1u))) != 0u)
            return (uint8_t)(step - 1u);
    }
    return MENU_FX_STEP_NONE;
}

/*
 * Return the last step held, or MENU_FX_STEP_NONE (S074 F3).
 *
 * What: decodes holdState. Valid only while the lock editor is open and the
 * service has seen the held mask at least once. Why: the one step every
 * held display and edit on the page uses (screen, LEDs, seeds). Inputs:
 * menuEffects_holdState. Output: 0..15 or MENU_FX_STEP_NONE. Callers:
 * _service(), _holdEdit(), _holdDisplay(), _shownParam().
 */
static uint8_t menuEffects_heldStep(void)
{
    const uint8_t need = (uint8_t)(MENU_FX_HOLD_ACTIVE |
                                   MENU_FX_HOLD_LAST_VALID);

    if ((menuEffects_holdState & need) != need)
        return MENU_FX_STEP_NONE;
    return (uint8_t)(menuEffects_holdState & MENU_FX_HOLD_LAST_MASK);
}

```

---

### P8 — `Core/Menu/menuEffects.c` L652–683 — MODIFY (service hold block)

Replace L652–683 with:

```c
    /*
     * Lock-edit hold follows the physical SEQ mask.
     *
     * What: the first pass after menuEffects_seqHoldExpired() (held mask
     * 0 -> nonzero) starts the held view; newly seen steps flash; releasing
     * every SEQ button ends the lock editor. Each of these transitions
     * requests one MENU_FX_ACT_HOLD_REPAINT.
     * S074: the pass also records the last step held. It is the newly
     * pressed step (the highest if several appear in one pass), or, when the
     * last step held was released while others stay down, the highest
     * remaining step. A type that owns the SELECT LEDs re-renders them,
     * because they show that step's values (CrumpBit's bit masks).
     * Why not MENU_FX_ACT_REPAINT (S074): the redraw can move a cell's
     * underline between its name (row 0) and its held value (row 1) on the
     * same CGRAM slot. Only menu_repaint() keeps the LCD shadow that lets
     * va_queueMarkerTransaction() order the move: restore the old cell to its
     * plain character, redefine the slot, then write the new cell. The VOICE
     * overlay has used menu_repaint() for the same transitions since S066
     * Fix 5.
     * Inputs: buttonHandler_seqHeldMask(), menuEffects_holdState/holdMask.
     * Outputs: updated hold state, SEQ LED flashes, owned SELECT LEDs, and
     * the action bit. Consumer: menu_serviceRuntimeWidgets().
     */
    if ((menuEffects_holdState & MENU_FX_HOLD_ACTIVE) != 0u) {
        uint16_t mask = buttonHandler_seqHeldMask();

        if (mask == 0u) {
            menuEffects_holdState = 0u;
            menuEffects_holdMask = 0u;
            actions |= MENU_FX_ACT_HOLD_REPAINT;
        } else if (mask != menuEffects_holdMask) {
            const uint16_t added =
                (uint16_t)(mask & (uint16_t)~menuEffects_holdMask);
            uint8_t last = menuEffects_heldStep();

            led_flashGroup(LED_FLASH_GROUP_SEQ, added);
            if (added != 0u)
                last = menuEffects_highestStep(added);
            else if (last == MENU_FX_STEP_NONE ||
                     (mask & (uint16_t)(1u << last)) == 0u)
                last = menuEffects_highestStep(mask);
            menuEffects_holdMask = mask;
            menuEffects_holdState = (uint8_t)(MENU_FX_HOLD_ACTIVE |
                MENU_FX_HOLD_LAST_VALID | (last & MENU_FX_HOLD_LAST_MASK));
            actions |= MENU_FX_ACT_HOLD_REPAINT;
        }
    }
    if ((actions & MENU_FX_ACT_HOLD_REPAINT) != 0u &&
        menuEffects_entry()->ui &&
        (menuEffects_entry()->ui->flags &
         EFFECT_UI_FLAG_OWNS_SELECT_LEDS) != 0u)
        menuEffects_renderLeds();
```

`menuEffects_renderLeds()` is declared in `menuEffects.h`, so it can be
called before its definition.

---

### P9 — `Core/Menu/menuEffects.c` L706–712 — MODIFY (`menuEffects_hookSelect()`)

```c
/*
 * Dispatch SELECT hooks; zero falls back to the default page behaviour.
 *
 * S074: the hook's return passes through (EFFECT_UI_HANDLED or
 * EFFECT_UI_SHOW_HOME; buttonHandler acts on the latter), and any consumed
 * SELECT abandons an unconfirmed `typ` browse, as a default SELECT press does
 * in menuEffects_selectPressed() (Q17). Inputs: button 0..7, shift, pressed.
 * Output: the hook's action code or 0. Callers: buttonHandler FX SELECT and
 * SHIFT+SELECT. Affiliates: effect_ui_hooks_t.select, menu_effectShowHome().
 */
uint8_t menuEffects_hookSelect(uint8_t button, uint8_t shift, uint8_t pressed)
{
    const effect_ui_hooks_t *hooks = menuEffects_hooks();
    uint8_t action;

    if (!hooks || !hooks->select)
        return 0u;
    action = hooks->select(button, shift, pressed);
    if (action != 0u)
        menuEffects_typeEdit = 0u;
    return action;
}
```

---

### P10 — `Core/Menu/menuEffects.c` after L734 — ADD (page extensions)

After `menuEffects_renderLeds()` (ends at L734):

```c

/*
 * Report a type-painted top row on the remembered screen of one SELECT.
 *
 * What: nonzero when the active type has a select_layout whose custom_row0
 * flags that SELECT's remembered screen and a paint_row0 hook. SELECT 1 is
 * never custom. Why: menu.c must know when row 0 carries type content
 * rather than names (no name markers; the painter runs). Input: sub-page
 * 0..7. Output: 0/1. Callers: menuEffects_paintRow0(), menu.c C8.
 * Affiliates: effect_select_layout_t.custom_row0.
 */
uint8_t menuEffects_screenHasCustomRow0(uint8_t sub_page)
{
    const effect_registry_entry_t *entry = menuEffects_entry();
    const effect_ui_hooks_t *hooks = entry->ui;
    uint8_t screen;

    if (sub_page == 0u || sub_page >= MENU_FX_SELECT_COUNT ||
        !entry->select_layout || !hooks || !hooks->paint_row0)
        return 0u;
    screen = menuEffects_screen[sub_page];
    return (uint8_t)((entry->select_layout->custom_row0[sub_page] &
                      (uint8_t)(1u << screen)) != 0u);
}

/*
 * Let the type paint the compact top row of a flagged screen.
 *
 * What: calls ui->paint_row0 for the remembered screen when
 * menuEffects_screenHasCustomRow0() allows it; otherwise does nothing.
 * Column 15 (the scroll marker) is the page's, not the type's. Inputs:
 * sub-page and the 16-byte top row of editDisplayBuffer. Output:
 * row0[0..14]. Caller: menu_repaintGeneric() compact branch (C5), after the
 * names, the uppercase cue and the scroll marker. Affiliates:
 * crumpBit_uiPaintRow0().
 */
void menuEffects_paintRow0(uint8_t sub_page, char *row0)
{
    if (!row0 || !menuEffects_screenHasCustomRow0(sub_page))
        return;
    menuEffects_entry()->ui->paint_row0(sub_page, menuEffects_screen[sub_page],
                                        row0);
}

/*
 * Type value text for one explicit PARAM value.
 *
 * What: forwards (row index, value) to ui->format_value3. Why: the compact
 * cell, the full view and a held step all show different values of the same
 * row, and each must use the same type label (Q7: CrumpBit's Sync
 * division). Inputs: a resolved cell (PARAM only), the value to show, and
 * the destination. Output: nonzero when dst[0..2] was written. Callers:
 * menuEffects_formatValue3() (P6), menu.c C6 and C9. Affiliates:
 * crumpBit_uiFormatValue3().
 */
uint8_t menuEffects_formatParamValue3(const menuEffects_cell_t *cell,
                                      uint8_t value, char *dst)
{
    const effect_ui_hooks_t *hooks = menuEffects_hooks();

    if (!cell || !dst || cell->kind != MENU_FX_CELL_PARAM ||
        !hooks || !hooks->format_value3)
        return 0u;
    return hooks->format_value3(cell->index, value, dst);
}

/*
 * Draw the SELECT LED row for the Effect page.
 *
 * What: a type with EFFECT_UI_FLAG_OWNS_SELECT_LEDS renders the row itself
 * (render_leds; CrumpBit shows its data lines); every other type gets the
 * page's active-SELECT LED for `sub_page`. Why: the page wrote
 * led_setActiveSelectButton() from four places, which would overwrite a
 * type's LED meaning; they all route here now. Input: current sub-page.
 * Output: SELECT LED row. Callers: menu.c C2, C3, C4, C1; buttonHandler
 * BH1. Affiliates: led_setActiveSelectButton(), menuEffects_renderLeds().
 */
void menuEffects_renderSelectLeds(uint8_t sub_page)
{
    const effect_ui_hooks_t *hooks = menuEffects_hooks();

    if (hooks && hooks->render_leds &&
        (hooks->flags & EFFECT_UI_FLAG_OWNS_SELECT_LEDS) != 0u)
        hooks->render_leds();
    else
        led_setActiveSelectButton(sub_page);
}

/*
 * Move the page memory to the type layout's home screen.
 *
 * What: validates select_layout->home_sub_page/home_screen, remembers that
 * screen for the SELECT, abandons any open `typ` browse, and returns the
 * sub-page and its first selectable column. Why: a hooked SELECT
 * (EFFECT_UI_SHOW_HOME) must land on the overlay from any screen (user
 * specification). Inputs: output pointers. Output: 1 with coordinates, or 0
 * when the type has no valid home (the caller then only repaints).
 * Caller: menu_effectShowHome() (C1). Affiliates: menuEffects_firstColumn(),
 * menuEffects_screenCount().
 */
uint8_t menuEffects_home(uint8_t *sub_page, uint8_t *column)
{
    const effect_select_layout_t *layout = menuEffects_entry()->select_layout;
    uint8_t sp;

    if (!sub_page || !column || !layout)
        return 0u;
    sp = layout->home_sub_page;
    if (sp == 0u || sp >= MENU_FX_SELECT_COUNT ||
        layout->home_screen >= menuEffects_screenCount(sp))
        return 0u;
    menuEffects_typeEdit = 0u;
    menuEffects_screen[sp] = layout->home_screen;
    *sub_page = sp;
    *column = menuEffects_firstColumn(sp, layout->home_screen);
    return 1u;
}

/*
 * Ask for Menu's live-refresh cadence on the Effect page.
 *
 * What: nonzero when the active type labels values through format_value3.
 * Why: CrumpBit's Sync label depends on the tempo, which changes without
 * any page input (MIDI or pulse clock, BPM edits). Menu's existing bounded
 * repaint (about 125 ms, only while the transport runs, never while editing
 * or under the screensaver) keeps it current with no new state (Q7).
 * Output: 0/1. Caller: menu_sceneLiveRefreshService() (C7).
 */
uint8_t menuEffects_liveRefreshWanted(void)
{
    const effect_ui_hooks_t *hooks = menuEffects_hooks();

    return (uint8_t)(hooks && hooks->format_value3);
}
```

---

### P11 — `Core/Menu/menuEffects.c` L753–763 — REMOVE

Delete `menuEffects_firstHeldStep()` and its comment (L753–763). P7's
`menuEffects_heldStep()` replaces it; keeping it would leave an unused
static function (a `-Wall` warning).

---

### P12 — `Core/Menu/menuEffects.c` L774–784 — MODIFY

```c
/* Open FX lock editing after the shared VOICE hold threshold. The last step
 * held becomes known on the next service pass (S074). */
void menuEffects_seqHoldExpired(void)
{
    menuEffects_holdState = MENU_FX_HOLD_ACTIVE;
    menuEffects_holdMask = 0u;
}

uint8_t menuEffects_seqHoldActive(void)
{
    return (uint8_t)((menuEffects_holdState & MENU_FX_HOLD_ACTIVE) != 0u);
}
```

---

### P13 — `Core/Menu/menuEffects.c` L786–812 — MODIFY (`menuEffects_holdEdit()`)

```c
/*
 * Edit one sequenceable cell across all held steps.
 *
 * The last step held (S074; the S072 rule used the lowest-numbered held
 * step) seeds from its lock value when locked, otherwise from the ordinary
 * cell display. The resulting value is clamped by the cell domain, and
 * EffectsManager writes and locks the lane on every held step, so all held
 * steps end up equal. Manager cells such as typ/run/len/scl are deliberately
 * ignored while holding (D2).
 */
uint8_t menuEffects_holdEdit(const menuEffects_cell_t *cell, int16_t delta)
{
    uint8_t lane;
    uint8_t held = menuEffects_heldStep();
    uint8_t stored;
    int32_t next;
    uint16_t value;

    if (held == MENU_FX_STEP_NONE || !menuEffects_cellLane(cell, &lane))
        return 0u;
    next = effects_getLaneLock(scene_getActiveIndex(), held, lane, &stored)
        ? (int32_t)stored : (int32_t)menuEffects_cellValue(cell);
    next += delta;
    value = (next < 0) ? 0u : (uint16_t)((next > 255) ? 255 : next);
    menuEffects_clampValue(cell, &value);
    return effects_setSeqLaneLock(scene_getActiveIndex(), menuEffects_holdMask,
                                  lane, (uint8_t)value);
}
```

---

### P14 — `Core/Menu/menuEffects.c` L814–829 — MODIFY (`menuEffects_holdDisplay()`)

```c
/* Show the last step held's lane value (S074) and report whether that lane
 * is locked there. */
uint8_t menuEffects_holdDisplay(const menuEffects_cell_t *cell,
                                uint8_t *value, uint8_t *locked)
{
    uint8_t lane;
    uint8_t held = menuEffects_heldStep();
    uint8_t stored;

    if (!value || !locked || held == MENU_FX_STEP_NONE ||
        !menuEffects_cellLane(cell, &lane))
        return 0u;
    *locked = effects_getLaneLock(scene_getActiveIndex(), held, lane,
                                  &stored);
    *value = *locked ? stored : (uint8_t)menuEffects_cellValue(cell);
    return 1u;
}
```

Consequence: between the hold threshold and the next service pass
(`LAST_VALID` not yet set), held values are not shown for that one pass.
Before, they showed the lowest step. This is invisible in practice (well
under a millisecond).

---

### P15 — `Core/Menu/menuEffects.c` after L829 — ADD

After `menuEffects_holdDisplay()` (ends L829), before the
`menuEffects_cellSeqLocked()` comment:

```c

uint8_t menuEffects_shownParam(uint8_t index)
{
    const effect_record_t *record = menuEffects_record();
    const uint8_t scene_index = scene_getActiveIndex();
    const uint8_t held = menuEffects_heldStep();
    uint8_t value;
    uint8_t lane;
    uint8_t stored;

    if (!record)
        return 0u;
    value = effects_getParameter(scene_index, index,
                                 menuEffects_showMorphFlag
                                     ? EFFECT_IMAGE_MORPH
                                     : EFFECT_IMAGE_NORMAL);
    if (held != MENU_FX_STEP_NONE &&
        effects_laneOfParam(record->type, index, &lane) &&
        effects_getLaneLock(scene_index, held, lane, &stored))
        value = stored;
    return value;
}

uint8_t menuEffects_editParam(uint8_t index, uint8_t value)
{
    const effect_record_t *record = menuEffects_record();
    const uint8_t scene_index = scene_getActiveIndex();
    uint8_t lane;

    if (!record)
        return 0u;
    if ((menuEffects_holdState & MENU_FX_HOLD_ACTIVE) != 0u) {
        /* Held SEQ steps write lane locks, never retained values (D2). */
        if (menuEffects_holdMask == 0u ||
            !effects_laneOfParam(record->type, index, &lane))
            return 0u;
        return effects_setSeqLaneLock(scene_index, menuEffects_holdMask,
                                      lane, value);
    }
    return effects_setParameter(scene_index, index,
                                menuEffects_showMorphFlag
                                    ? EFFECT_IMAGE_MORPH
                                    : EFFECT_IMAGE_NORMAL,
                                value);
}
```

The contract comments are in `menuEffects.h` (H2). The masks are not
Morphable, so `effects_setParameter()` writes their single value whatever
the image (EffectsManager's image rule).

---

## 7. Menu and buttonHandler (U1, C1–C9, BH1–BH2)

### U1 — `Core/Menu/menu.h` after L417 — ADD

After `void menu_setEffectShowMorph(uint8_t onOff);`:

```c

/*
 * Show the Effect type's home screen after a hooked SELECT (S074).
 *
 * What: on the Effect page, leaves the full view, moves the cursor to the
 * type layout's home screen (CrumpBit: the bit overlay), refreshes the pot
 * mapping, re-renders the SELECT LEDs through the type owner, and repaints
 * with menu_repaint(), which keeps the LCD shadow so marker moves stay
 * ordered. Without a valid home it only re-renders and repaints. Any open
 * `typ` browse was already abandoned by menuEffects_hookSelect(). Inputs:
 * none. Output: menuIndex, editModeActive, LEDs, LCD. Callers:
 * buttonHandler FX SELECT and SHIFT+SELECT on EFFECT_UI_SHOW_HOME.
 * Affiliates: menuEffects_home(), menuEffects_renderSelectLeds().
 */
void menu_effectShowHome(void);
```

---

### C1 — `Core/Menu/menu.c` after L13442 — ADD

After `menu_setEffectShowMorph()` (ends L13442), before
`menu_showStepTrackSettingsFirstHalf()`:

```c

/* Contract in menu.h (S074). */
void menu_effectShowHome(void)
{
    uint8_t sub_page;
    uint8_t column;

    if (menu_activePage != EFFECT_PAGE)
        return;
    if (menuEffects_home(&sub_page, &column)) {
        editModeActive = 0u;
        menuIndex = (uint8_t)((sub_page << PAGE_SHIFT) | column);
        menu_endlessPotMappingChanged();
    }
    menuEffects_renderSelectLeds(menu_getSubPage());
    menu_repaint();
}
```

---

### C2 — `Core/Menu/menu.c` L12788 — MODIFY

`        led_setActiveSelectButton(menu_getSubPage());` →

```c
        /* S074: a type may own the SELECT row (CrumpBit's data lines). */
        menuEffects_renderSelectLeds(menu_getSubPage());
```

### C3 — `Core/Menu/menu.c` L12496 — MODIFY

`        led_setActiveSelectButton(sub_page);` →

```c
        /* S074: a type may own the SELECT row (CrumpBit's data lines). */
        menuEffects_renderSelectLeds(sub_page);
```

This also restores the active-SELECT LED when `typ` changes away from `cbt`,
because the repair path runs on every type change.

### C4 — `Core/Menu/menu.c` L10225 — MODIFY

`            led_setActiveSelectButton(sub_page);` →

```c
            /* S074: a type may own the SELECT row (CrumpBit's data lines). */
            menuEffects_renderSelectLeds(sub_page);
```

---

### C5 — `Core/Menu/menu.c` after L10003 — ADD

After `editDisplayBuffer[0][15] = (char)checkScrollSign(activePage, activeParameter);`:

```c
        /*
         * Type-painted top row (S074; CrumpBit's data-line overlay).
         *
         * What: on a screen flagged in the type's select_layout->custom_row0,
         * the type replaces columns 0..14 (names and the uppercase cue) with
         * its own row; column 15 keeps the scroll marker written above. Why:
         * the overlay shows bit states instead of names (user specification).
         * Runs after the names so a type needs no knowledge of the default
         * row. Inputs: sub-page, editDisplayBuffer[0]. Output: row 0.
         * Affiliates: menuEffects_paintRow0(), C8 (no name markers there).
         */
        if (menu_activePage == EFFECT_PAGE)
            menuEffects_paintRow0(activePage, editDisplayBuffer[0]);
```

---

### C6 — `Core/Menu/menu.c` between L9966 and L9967 — ADD

After the `switch (dtype) { … }` closes (L9966), inside the enclosing
`else` block that closes at L9967:

```c
            /*
             * Type value text in the full view (S074; Effect page only).
             *
             * What: after the generic dtype text, an Effect PARAM row may be
             * relabelled by its type's format_value3 hook (CrumpBit: `sub`
             * -> `dly`; `rte` -> the Sync division while Sync is on). The raw
             * value underneath is unchanged. Inputs: the cell and its
             * displayed value. Output: editDisplayBuffer[1][13..15] when the
             * hook handles the row. Affiliates:
             * menuEffects_formatParamValue3(), C9, P6.
             */
            if (cell.kind == MENU_CELL_EFFECT)
                (void)menuEffects_formatParamValue3(
                    &cell.fx, value, &editDisplayBuffer[1][13]);
```

---

### C7 — `Core/Menu/menu.c` L2872 — MODIFY (`menu_sceneLiveRefreshService()`)

Replace the VOICE branch's closing `}` at L2872 with a closing brace plus a
new branch:

```c
    } else if (menu_activePage == EFFECT_PAGE) {
        /*
         * S074: a type that labels values (CrumpBit's Sync division follows
         * the tempo) gets the same bounded repaint while playing (Q7).
         */
        visible = menuEffects_liveRefreshWanted();
    }
```

Also extend the function comment's first sentence at L2841–2842: "repaints
the visible PERF Morph cells, VOICE/mix Scene-setting cells, or (S074) an
Effect page whose type labels values from live state, at a bounded
foreground cadence."

---

### C8 — `Core/Menu/menu.c` L2789–2791 — MODIFY (`menu_applyEffectMarkers()`)

**Current:**

```c
        /* Unheld, or held but unlocked: fall back to the name marker. */
        if (!menu_effectCellAutomated(&cell))
            continue;
```

**New:**

```c
        /* Unheld, or held but unlocked: fall back to the name marker. */
        /*
         * S074: a type-painted row 0 (CrumpBit's data-line overlay) holds no
         * names, and its `0` characters are underline-able, so a name marker
         * there would mark a bit. Those parameters show their underlines on
         * the type's named screens (page 2) instead (Q16).
         */
        if (!editModeActive && menuEffects_screenHasCustomRow0(activePage))
            continue;
        if (!menu_effectCellAutomated(&cell))
            continue;
```

---

### C9 — `Core/Menu/menu.c` L2767–2768 — MODIFY (held PARAM text)

**Current:**

```c
            if (cell.fx.kind == MENU_FX_CELL_PARAM)
                va_formatValue3(&cell, value, field);
```

**New:**

```c
            if (cell.fx.kind == MENU_FX_CELL_PARAM) {
                /* S074: the type's label for the held value (Sync division). */
                if (!menuEffects_formatParamValue3(&cell.fx, value, field))
                    va_formatValue3(&cell, value, field);
            }
```

---

### BH1 — `Core/Hardware/frontPanel/buttonHandler.c` L961–968 — MODIFY

**Current:**

```c
    case SELECT_MODE_FX:
        /* Let the active Effect type consume SELECT before default navigation. */
        if (menuEffects_hookSelect(selectNr, 0u, 1u))
            break;
        menu_switchSubPage(selectNr);
        led_setActiveSelectButton(menu_getSubPage());
        menu_repaintAll();
        break;
```

**New:**

```c
    case SELECT_MODE_FX: {
        /*
         * Let the active Effect type consume SELECT before default navigation.
         *
         * S074: EFFECT_UI_SHOW_HOME means the type handled the press and the
         * page must show its home screen (CrumpBit: toggle a data line, then
         * show the overlay). menu_effectShowHome() repaints and re-renders
         * the LEDs. Default navigation's SELECT LED now goes through the
         * type owner check, which is the active-SELECT LED for other types.
         */
        const uint8_t fx_action = menuEffects_hookSelect(selectNr, 0u, 1u);

        if (fx_action != 0u) {
            if (fx_action == EFFECT_UI_SHOW_HOME)
                menu_effectShowHome();
            break;
        }
        menu_switchSubPage(selectNr);
        menuEffects_renderSelectLeds(menu_getSubPage());
        menu_repaintAll();
        break; }
```

---

### BH2 — `Core/Hardware/frontPanel/buttonHandler.c` L935–938 — MODIFY

**Current:**

```c
        case SELECT_MODE_FX:
            /* SHIFT+SELECT is reserved for optional type UI hooks. */
            (void)menuEffects_hookSelect(selectNr, 1u, 1u);
            break;
```

**New:**

```c
        case SELECT_MODE_FX:
            /*
             * SHIFT+SELECT is reserved for optional type UI hooks. S074:
             * CrumpBit resets that data line to normal and asks for its home
             * screen.
             */
            if (menuEffects_hookSelect(selectNr, 1u, 1u) == EFFECT_UI_SHOW_HOME)
                menu_effectShowHome();
            break;
```

---

## 8. Build and verification gates

Run one build at a time.

1. `make all && make img`. Expect no new warnings. The existing newlib
   `nosys` stub and LTO notes are unrelated.
2. `python3 tools/link_budget.py arm-none-eabi-nm build/lxr02.elf`:
   - `data=416` and `bss=426,336`: **unchanged**;
   - ITCM 4,168 B and DTCM statics 4,448 B: unchanged;
   - the FX arena stays 126,624 B;
   - `text` grows by an estimated 3–4 KB, and the image will pass
     `0x08080000` (sector 6).
3. `arm-none-eabi-nm -S build/lxr02.elf | grep effects_runtime` shows size
   `0000004c` (76 B). M4 also enforces this at compile time.
4. A `DEV_MODE_DIAGNOSTIC 1` build: the `FxBf` registry self-check shows 0
   (codes 1–10 cover the new row: names, lanes, defaults, runtime size).
   Rebuild with diagnostics off afterwards.
5. `git diff --check` is clean. Record the `.img` SHA-256; the `.bin` is
   unstamped since the image-script fold.

## 9. Hardware checks (yours)

**Before the first Stage 1 flash:** keep the current known-good
`LXRV2_lxr02.img` (SHA-256 `0e004720…ddeea`). If boot shows
`Img BAD s:.....6`, reflash it by holding the encoder at power-on.

**Stage 0:** `flt` → `off` → `flt` (and later `off` → `cbt`): the return
fades in without a click.

**Stage 1 (default page):**

1. `typ` → `cbt`. SELECT 2 shows `bof biv mix fbk`; SELECT 3 shows
   `rte sub syn dpn`.
2. Masks as numbers: 128 (bit 7 off) and 127 (bits 0–6 off) on silence and
   on material. Bit 7 moves silence to a rail, and the AC coupling settles
   it back within about 50 ms.
3. Rate 0 → 127 (1.6 s → 20 ms) with an audible glide; Sync on at 60, 120
   and 200 BPM and with external clock. The length snaps to divisions (the
   label shows the raw number in Stage 1).
4. Mix 0/64/127, Feedback 127 (long, bounded, no runaway), DlyPan extremes.
5. `.fx` save and reload; AutoSave restore after a reboot.
6. FX locks on lanes 1–9 through the default cells; an LFO on `rte` (tape
   wobble).
7. Worst-case Scene with `cbt`, every send open and `rte` under an LFO:
   underruns and the `cpu` widget.

**Stage 2 (the page):**

8. The overlay after the two SELECT 1 screens: row 0 is
   `- - - - - - - ->`; row 1 is `mix fbk rte mrp` values.
9. SELECT 1–8 each cycle `-` → `0` → `!` → `-`, and LED *n* is lit on `0` and
   `!`. SHIFT+SELECT resets to `-`. SELECT from SELECT 1's screens, from
   page 3, from a full view, and from an open `typ` browse all land on the
   overlay. The `typ` candidate is **not** committed.
10. Page 2 (`mix fbk rte sub`, `sub` = `dly`), page 3 (`syn dpn`); S074
    underlines on pages 2 and 3, none on the overlay's row 0.
11. Sync on: `rte` shows the division on the overlay, on page 2, in the full
    view and in a held value. It follows a tempo change while playing. The
    STEP automation page still shows the raw number.
12. SEQ hold: press step 3, then step 7. Row 0, the LEDs and the values show
    step 7. Release 7 while holding 3, and they show step 3. SELECT during
    the hold writes the same masks to every held step (all equal
    afterwards), and both lanes are locked (SEQ LEDs lit). Releasing
    restores the retained masks.
13. `typ` back to `flt`: the SELECT LEDs show the active SELECT again, and
    SELECT navigates. The `flt` hold edit seeds from the last step held (the
    §1.4 interpretation).
14. VOICE pages: underlines, holds and marker ordering unchanged.

**Stage 3:**

15. A same-type Scene switch while a tail rings: the tail continues into the
    new Scene. A switch to `flt` and back to `cbt`: no old tail.
16. Minimum share (`DEV_MODE_DIAGNOSTIC 1`, `DEV_FXBUF_FORCE_VOICE_UNITS 12`):
    the full 1.6 s range still works.

## 10. Documentation after the hardware pass

- `EFFECTS_BUS_REFERENCE.md`:
  - §4.2–§4.3: the hook and layout fields, `EFFECT_UI_*`,
    `EFFECT_LAYOUT_CELL_MORPH`;
  - §8: the CrumpBit page, and the last step held for every Effect hold;
  - §13: item 1 closed;
  - §14: the tutorial's layout and hook steps;
  - the registry table: `cbt`.
- `EFFECTS_MIXER_DSP_REFERENCE.md`:
  - a new §4.4 for CrumpBit's DSP;
  - §5.3: gaps 1–3 closed;
  - §6: the measured cost.
- `MODULE_INTERCHANGE_SPEC.md`: the new `menuEffects_*` functions and
  `menu_effectShowHome()`.
- `STORAGE_SRAM_MANIFEST.md`:
  - flash and sector 6;
  - the union unchanged at 76 B (CrumpBit 56 B);
  - the repacked `menuEffects` hold byte.
- `SCOPING_TARGETS.md`: A8 closed; the Effect pan display quirk
  (`DTYPE_PM63` centre shows `1`); the S074 carry-forward.
- `MEMORY.md` and `074_SESSION_HANDOFF_LOG.md`.
