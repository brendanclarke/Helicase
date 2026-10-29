# S088 P0 — Oscillator Run Modes: Implementation Schedule

**Session:** 088, phase P0 (implementation schedule; no firmware code this turn)
**Plan:** `S088_P0_OSC_RUN_MODES.md` (all D1–D12 accepted)
**Branch:** `dev-ph7-voice_expansion` (base `2ab359eb`, post-S087 closeout)
**Date:** 2026-10-29

---

## How to read this document

Every code change is cited as `file:line` with **ADD**, **MODIFY** or
**REMOVE**. Each change carries an in-place comment block that documents
what it does, why it exists, its inputs, outputs, accessors and
affiliates. The changes are grouped into the four implementation layers
from the plan (§3 of `S088_P0_OSC_RUN_MODES.md`). Each layer compiles
against the one before it.

The term "armed block" means the first render block after a retrigger with
`runMode != OSC_RUN_RST` and the amp envelope still audible. The gain ramp
runs for that one block, then the endpoints match and the ramp is flat.

---

## Layer 1 — The parameter (`run`): menu, storage, preset

### L1-1. `MenuText.h` — ONOFF_TAG and oscRunNames

**File:** `Core/Menu/MenuText.h`

#### L1-1a. ONOFF_TAG_OSC_RUN constant — ADD after line 163

Insert after `oscTypeNames` (line 161–163), before `outputNames` (line 164):

```c
/*
 * DTYPE_ON_OFF high-nibble sub-type tag (S088 P0 §2.1 D12).
 *
 * What:       ONOFF_TAG_OSC_RUN = 1u. The descriptor dtype byte stores
 *             DTYPE_ON_OFF | (ONOFF_TAG_OSC_RUN << 4) = 0x16 so that
 *             menu.c can distinguish run-mode cells from ordinary on/off
 *             cells. The low nibble & 0x0f = 6 = DTYPE_ON_OFF; the high
 *             nibble >> 4 = 1.
 * Why:        all 16 DTYPE_MENU ids and all 16 dtype enum values are taken
 *             (S078 binding mandate). The high nibble of DTYPE_ON_OFF was
 *             previously unused; all existing >> 4 reads of the dtype byte
 *             occur only inside DTYPE_MENU cases (verified S088 P0).
 * Inputs:     the four engine *Parameters.c descriptor rows for osc1_run.
 * Outputs:    read by the six DTYPE_ON_OFF switch sites in menu.c.
 * Accessors:  ROW_SELECT_ONOFF_SPECIAL macro (each *Parameters.c).
 * Affiliates: oscRunNames below; S088_P0_OSC_RUN_MODES.md §2.1.
 */
#define ONOFF_TAG_OSC_RUN   1u
```

#### L1-1b. oscRunNames table — ADD after L1-1a

Insert immediately after the `ONOFF_TAG_OSC_RUN` define:

```c
/*
 * Oscillator run-mode names (S088 P0 §2.1).
 *
 * What:       [0] = count (3), then rst fre itp. Same pattern as
 *             mixFmNames: leading count byte, 3+1 char entries.
 * Why:        the six DTYPE_ON_OFF tagged sites in menu.c use this table
 *             for display and clamp; step automation and the single-
 *             parameter view also read it.
 * Accessors:  menu.c menu_formatCellValue3(), menu_repaintGeneric(),
 *             menu_clampCellValue(), menu_automationValueMax(),
 *             menu_formatAutomationValue3(), step-automation edit display.
 * Affiliates: ONOFF_TAG_OSC_RUN above; the osc1_run descriptor rows.
 */
static const char oscRunNames[][4] = {
    {3}, {"rst"},{"fre"},{"itp"},
};
```

---

### L1-2. `InstrumentManager.h` — IM_SPECIAL_OSC_RUN

**File:** `Core/DSP/Instruments/InstrumentManager.h`

#### L1-2a. Enum value — ADD at line 155, before IM_SPECIAL_OSC_TYPE's closing

Insert `IM_SPECIAL_OSC_RUN` after `IM_SPECIAL_OSC_TYPE` (line 155) and
before `IM_SPECIAL_WRITER_COUNT` (line 156):

```c
    /*
     * S088 P0: osc1_run writer. Clamps the run mode value to 0..2
     * (OSC_RUN_RST..OSC_RUN_ITP) and stores it in the engine's runMode
     * field. No side effects beyond the store: the trigger and render
     * functions read runMode at their own pace.
     *
     * What:       IM_SPECIAL_OSC_RUN = 22 (next after IM_SPECIAL_OSC_TYPE).
     * Why:        run mode is a selector, not a continuous parameter. It
     *             must pass through the special writer so Kit apply, Scene
     *             apply, step automation and menu edit all write the same
     *             way. The writer clamps to the valid range (0..2) so
     *             corrupt storage or MIDI writes above 2 become itp.
     * Inputs:     slot, descriptor tag (IM_SPECIAL_OSC1), byte value.
     * Outputs:    the engine struct's runMode field.
     * Accessors:  instrumentManager_writeSpecialRuntime() switch.
     * Affiliates: the osc1_run descriptor rows; OSC_RUN_* constants
     *             (Oscillator.h); the four engine structs' runMode field.
     */
    IM_SPECIAL_OSC_RUN,
```

After this insertion `IM_SPECIAL_WRITER_COUNT` becomes 23. The existing
`_Static_assert` at line 165 (`<= IM_SPECIAL_WRITER_MASK + 1u = 32`) still
passes.

---

### L1-3. `Oscillator.h` — OSC_RUN_* constants

**File:** `Core/DSPAudio/Oscillator.h`

#### L1-3a. Run mode constants — ADD after line 98

Insert after the `_Static_assert` for `OSC_TYPE_SELECTABLE` (line 97–98),
before the sample reader constants block (line 100):

```c
/*
 * Oscillator run modes (S088 P0 §1).
 *
 * What:       OSC_RUN_RST 0 (reset, today's behaviour), OSC_RUN_FRE 1
 *             (free-run, no phase reset, gain-matched retrigger),
 *             OSC_RUN_ITP 2 (interpolate, carrier crossfade).
 * Why:        symbolic constants for the four engines' trigger and render
 *             functions; the number 0/1/2 is stored in the engine struct's
 *             runMode field and in Scene/preset storage.
 * Inputs:     the IM_SPECIAL_OSC_RUN writer, which clamps to this range.
 * Outputs:    read by the engine trigger and render functions.
 * Accessors:  the four engine .c files; the writer; oscRunNames table.
 * Affiliates: S088_P0_OSC_RUN_MODES.md §1; the osc1_run rows.
 */
#define OSC_RUN_RST   0u
#define OSC_RUN_FRE   1u
#define OSC_RUN_ITP   2u
#define OSC_RUN_COUNT 3u
```

---

### L1-4. Engine voice headers — `uint8_t runMode`

Each engine struct gains a `uint8_t runMode` field. In Layer 2 this is
joined by `uint8_t ampRampArm` and the ramp/snapshot fields, but Layer 1
adds only `runMode` (the stored value).

#### L1-4a. `DrumVoice.h` — ADD after line 209

Insert after `uint8_t volumeMod;` (line 209), before `Dither dither;`
(line 211):

```c
    /*
     * Oscillator run mode (S088 P0 §2.4).
     *
     * What:       0 rst (today), 1 fre (free-run), 2 itp (interpolate).
     *             Written by IM_SPECIAL_OSC_RUN through the descriptor.
     * Why:        the trigger and render functions branch on this byte to
     *             select phase behaviour, gain-matched retrigger and
     *             crossfade logic. It is not a continuous parameter (never
     *             morphed, no LFO, no velocity).
     * Inputs:     instrumentManager_writeSpecialRuntime() via the descriptor.
     * Outputs:    read by Drum_triggerVoice(), Drum_calcVoiceSyncBlock().
     * Accessors:  Drum_initVoice() (0), the descriptor writer, trigger, render.
     * Affiliates: OSC_RUN_* (Oscillator.h); the osc1_run row (DrumParameters.c).
     * Layout:     padding after volumeMod; DrumVoice grows by 1 B (+ alignment).
     */
    uint8_t runMode;
```

#### L1-4b. `Snare.h` — ADD after line 98

Insert after `uint8_t volumeMod;` (line 98), before the closing `}
SnareVoice;` (line 100):

```c
    /*
     * Oscillator run mode (S088 P0 §2.4).
     *
     * What:       0 rst, 1 fre, 2 itp. Written by IM_SPECIAL_OSC_RUN.
     * Why:        the trigger and render functions branch on this byte.
     * Inputs:     instrumentManager_writeSpecialRuntime() via the descriptor.
     * Outputs:    read by Snare_triggerVoice(), Snare_calcSyncBlockVoice().
     * Accessors:  Snare_initVoice() (0), the descriptor writer, trigger, render.
     * Affiliates: OSC_RUN_*; the osc1_run row (SnareParameters.c).
     */
    uint8_t runMode;
```

#### L1-4c. `CymbalVoice.h` — ADD after line 88

Insert after `uint8_t volumeMod;` (line 88), before `SnapEg snapEg;`
(line 90):

```c
    /*
     * Oscillator run mode (S088 P0 §2.4).
     *
     * What:       0 rst, 1 fre, 2 itp. Written by IM_SPECIAL_OSC_RUN.
     * Why:        the trigger and render functions branch on this byte.
     * Inputs:     instrumentManager_writeSpecialRuntime() via the descriptor.
     * Outputs:    read by Cymbal_triggerVoice(), Cymbal_calcSyncBlockVoice().
     * Accessors:  Cymbal_initVoice() (0), the descriptor writer, trigger, render.
     * Affiliates: OSC_RUN_*; the osc1_run row (CymbalParameters.c).
     */
    uint8_t runMode;
```

#### L1-4d. `HiHat.h` — ADD after line 95

Insert after `uint8_t volumeMod;` (line 95), before `uint8_t isOpen;`
(line 96):

```c
    /*
     * Oscillator run mode (S088 P0 §2.4).
     *
     * What:       0 rst, 1 fre, 2 itp. Written by IM_SPECIAL_OSC_RUN.
     * Why:        the trigger and render functions branch on this byte.
     * Inputs:     instrumentManager_writeSpecialRuntime() via the descriptor.
     * Outputs:    read by HiHat_triggerVoice(), HiHat_calcSyncBlockVoice().
     * Accessors:  HiHat_initVoice() (0), the descriptor writer, trigger, render.
     * Affiliates: OSC_RUN_*; the osc1_run row (HiHatParameters.c).
     */
    uint8_t runMode;
```

---

### L1-5. Engine init functions — zero runMode

Each engine's init function must set `runMode = 0` (rst).

#### L1-5a. `DrumVoice.c` — ADD inside `Drum_initVoice()`, after line 166

Insert after `voice->ringSnap = 1u;` (line 166), before the closing of
`Drum_initVoice()`:

```c
    voice->runMode = OSC_RUN_RST;
```

#### L1-5b. `Snare.c` — ADD inside `Snare_initVoice()` (equivalent position)

At the end of `Snare_initVoice()`, before its return:

```c
    voice->runMode = OSC_RUN_RST;
```

#### L1-5c. `CymbalVoice.c` — ADD inside `Cymbal_initVoice()` (equivalent position)

```c
    voice->runMode = OSC_RUN_RST;
```

#### L1-5d. `HiHat.c` — ADD inside `HiHat_initVoice()` (equivalent position)

```c
    voice->runMode = OSC_RUN_RST;
```

---

### L1-6. `InstrumentManager.c` — IM_SPECIAL_OSC_RUN writer case

**File:** `Core/DSP/Instruments/InstrumentManager.c`

#### L1-6a. Writer switch case — ADD at line 3589

Insert a new `case IM_SPECIAL_OSC_RUN:` block after the closing `}` of
`IM_SPECIAL_OSC_TYPE` (line 3589), before `case IM_SPECIAL_NONE:` (line 3590):

```c
    case IM_SPECIAL_OSC_RUN: {
        /*
         * osc1_run writer (S088 P0 §2.4).
         *
         * What:       clamp byteValue to 0..OSC_RUN_COUNT-1 (0 rst, 1 fre,
         *             2 itp), then store it in the engine struct's runMode
         *             field through instrumentManager_byteFieldByOffset().
         * Why:        every osc1_run write (menu, Kit apply, Scene apply,
         *             step automation, MIDI) must clamp and store the same
         *             way. No side effects: the trigger and render functions
         *             read runMode each block.
         * Inputs:     slot, the osc selector (IM_SPECIAL_OSC1), byteValue.
         * Outputs:    the engine struct's runMode byte.
         * Accessors:  instrumentManager_writeRuntimeInternal() via the tag.
         * Affiliates: OSC_RUN_* (Oscillator.h); the engine trigger functions.
         */
        if (byteValue >= OSC_RUN_COUNT)
            byteValue = (uint8_t)(OSC_RUN_COUNT - 1u);
        uint8_t *field = instrumentManager_byteFieldByOffset(slot, offset);
        if (!field) return 0u;
        *field = byteValue;
        return 1u; }
```

**Note:** `instrumentManager_byteFieldByOffset()` is the same byte-write
helper used by other selector writers. The `offset` comes from the
descriptor's `BIND_SPECIAL(osc.runMode, TYPE_UINT8, ...)`. If the existing
code path writes through a different mechanism (direct struct cast), match
the `IM_SPECIAL_OSC_TYPE` pattern above it exactly.

---

### L1-7. Engine parameter files — ROW_SELECT_ONOFF_SPECIAL macro, enum entry, descriptor row, page cell

Each engine's `*Parameters.c` file needs four changes:

1. The `ROW_SELECT_ONOFF_SPECIAL` macro (one definition, can go in any of
   the four files or in a shared header; pattern: define it in each file
   locally, beside `ROW_SELECT_SPECIAL`, since the existing macros are
   `#undef`'d at the bottom of each file).
2. The `_PARAM_OSC1_RUN` enum entry.
3. The `osc1_run` descriptor row.
4. The osc page cell placement.

#### L1-7a. `DrumParameters.c`

**Macro — ADD after line 106** (after `ROW_SELECT_SPECIAL`):

```c
/*
 * ROW_SELECT_ONOFF_SPECIAL: a tagged DTYPE_ON_OFF selector (S088 P0 §2.3 D12).
 *
 * What:       like ROW_SELECT_SPECIAL but the dtype byte is
 *             DTYPE_ON_OFF | (tag_ << 4) instead of DTYPE_MENU | (menu_ << 4).
 *             The high nibble distinguishes this from a plain on/off cell.
 * Why:        all 16 DTYPE_MENU ids are taken; osc1_run uses DTYPE_ON_OFF's
 *             unused high nibble as a sub-type tag.
 * Inputs:     SD key, category, long/short label, ONOFF_TAG constant,
 *             struct member, scalar type, IM_SPECIAL tag.
 * Outputs:    one ParamDescriptor initializer.
 * Accessors:  the osc1_run rows below.
 * Affiliates: ONOFF_TAG_OSC_RUN (MenuText.h); the six DTYPE_ON_OFF sites
 *             in menu.c; ROW_SELECT_SPECIAL above.
 */
#define ROW_SELECT_ONOFF_SPECIAL(key_, cat_, long_, short_, tag_, member_, type_, special_) \
    { key_, short_, long_, cat_, (uint8_t)(DTYPE_ON_OFF | ((tag_) << 4)), \
      (INSTRUMENT_PARAM_FLAG_AUTOMATABLE | INSTRUMENT_PARAM_FLAG_SELECTOR), \
      MOD_NONE, BIND_SPECIAL(member_, type_, special_) }
```

**Enum — MODIFY line 192–193:**

Change:
```c
    DRUM_PARAM_OSC1_TYPE, /* S087 B: appended; preserve all prior IDs. */
    DRUM_PARAM_COUNT
```
To:
```c
    DRUM_PARAM_OSC1_TYPE, /* S087 B: appended; preserve all prior IDs. */
    DRUM_PARAM_OSC1_RUN, /* S088: appended; preserve all prior IDs. */
    DRUM_PARAM_COUNT
```

**`_Static_assert` at line 196 and `DRUM_PARAM_DESCRIPTOR_COUNT`:**

The `_Static_assert` uses `DRUM_PARAM_DESCRIPTOR_COUNT`, which must be
updated wherever it is defined (likely a header or a `#define` above the
assert). The count increases by 1. No code change to the assert itself —
only to the count constant. Find and update `DRUM_PARAM_DESCRIPTOR_COUNT`.

**Descriptor row — ADD after line 309** (after the `osc1_type` row), before
the closing `};` of `drum_param_descriptors[]` (line 310):

```c
    /*
     * osc1_run: run, the oscillator run mode (S088 P0).
     *
     * What:       0 rst (reset, today), 1 fre (free-run), 2 itp (interpolate),
     *             stored in osc.runMode through IM_SPECIAL_OSC_RUN.
     * Why:        S088 P0 §1: three oscillator trigger behaviours. A selector
     *             (never morphed, Normal image only, applied before the Morph
     *             image, step-automatable one value per step, not an LFO/velocity
     *             target). Appended last so every existing descriptor index,
     *             automation ID, token, NRPN ID and Learn row is unchanged; a
     *             file without the key loads 0 (rst, today's sound).
     * Inputs:     menu edit, Kit/Scene/Instrument apply, the trigger restore,
     *             step automation.
     * Outputs:    osc.runMode (the engine struct field).
     * Accessors:  instrumentManager_writeSpecialRuntime().
     * Affiliates: OSC_RUN_* (Oscillator.h); oscRunNames (MenuText.h);
     *             ONOFF_TAG_OSC_RUN; the six DTYPE_ON_OFF sites in menu.c.
     */
    ROW_SELECT_ONOFF_SPECIAL("osc1_run", "Oscilltr", "Run Mode", "run",
        ONOFF_TAG_OSC_RUN, osc.runMode, TYPE_UINT8,
        IM_SPECIAL_OSC_RUN | IM_SPECIAL_OSC1),
```

**Page cell — MODIFY line 328:**

The osc page line currently has `DRUM_PARAM_OSC1_TYPE` at cell 4, then
three `INSTRUMENT_MENU_EMPTY`. Change cell 5 from `INSTRUMENT_MENU_EMPTY`
to `DRUM_PARAM_OSC1_RUN`:

```
DRUM_PARAM_OSC1_TYPE, DRUM_PARAM_OSC1_RUN, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY,
```

(Replace the first `INSTRUMENT_MENU_EMPTY` after `DRUM_PARAM_OSC1_TYPE`.)

**Undef — ADD at bottom (after line 356):**

```c
#undef ROW_SELECT_ONOFF_SPECIAL
```

#### L1-7b. `SnareParameters.c`

Same four changes. Key differences:

- **Macro:** same `ROW_SELECT_ONOFF_SPECIAL` definition, inserted after
  Snare's `ROW_SELECT_SPECIAL`.
- **Enum:** `SNARE_PARAM_OSC1_RUN` after `SNARE_PARAM_OSC1_TYPE` (line 190),
  before `SNARE_PARAM_COUNT` (line 191).
- **Descriptor row:** after the `osc1_type` row (line 296), identical comment
  block adapted for Snare.
- **Page cell:** Snare osc page (line 313) has cells 4–6 as
  `SNARE_PARAM_NOISE_FREQ, SNARE_PARAM_OSC1_NOISE_MIX, SNARE_PARAM_OSC1_TYPE`.
  Cell 7 is `INSTRUMENT_MENU_EMPTY`. Change cell 7 to
  `SNARE_PARAM_OSC1_RUN`.
- **Undef:** `#undef ROW_SELECT_ONOFF_SPECIAL` at the bottom.
- **`SNARE_PARAM_DESCRIPTOR_COUNT`:** increment by 1.

#### L1-7c. `CymbalParameters.c`

- **Enum:** `CYMBAL_PARAM_OSC1_RUN` after `CYMBAL_PARAM_OSC1_TYPE` (line 191),
  before `CYMBAL_PARAM_COUNT` (line 192).
- **Descriptor row:** after the `osc1_type` row (line 298).
- **Page cell:** Cymbal osc page (line 314) has cell 4 as
  `CYMBAL_PARAM_OSC1_TYPE`, cell 5 `INSTRUMENT_MENU_EMPTY`. Change cell 5
  to `CYMBAL_PARAM_OSC1_RUN`.
- **`CYMBAL_PARAM_DESCRIPTOR_COUNT`:** increment by 1.

#### L1-7d. `HiHatParameters.c`

- **Enum:** `HIHAT_PARAM_OSC1_RUN` after `HIHAT_PARAM_OSC1_TYPE` (line 194),
  before `HIHAT_PARAM_COUNT` (line 195).
- **Descriptor row:** after the `osc1_type` row (line 301).
- **Page cell:** HiHat osc page (line 319) has cell 4 as
  `HIHAT_PARAM_OSC1_TYPE`, cell 5 `INSTRUMENT_MENU_EMPTY`. Change cell 5
  to `HIHAT_PARAM_OSC1_RUN`.
- **`HIHAT_PARAM_DESCRIPTOR_COUNT`:** increment by 1.

---

### L1-8. `menu.c` — Six DTYPE_ON_OFF switch sites

All six `DTYPE_ON_OFF` case branches gain a high-nibble tag check. When
the tag is non-zero, dispatch to `oscRunNames` instead of
`menuText_on`/`menuText_off`. When the tag is zero, fall through to today's
on/off code (bit-identical).

The helper to extract the tag from a cell's dtype:
```c
(uint8_t)(menu_cellDtype(cell) >> 4)
```
This is the same expression used for `DTYPE_MENU` menu ids. It is safe
because the high nibble of a plain `DTYPE_ON_OFF` (0x06) is 0.

#### L1-8a. Site 1: `menu_formatCellValue3()` — MODIFY line 3346–3348

**Current (lines 3346–3348):**
```c
    case DTYPE_ON_OFF:
        memcpy(out, (value == 1u) ? menuText_on : menuText_off, 3);
        break;
```

**New:**
```c
    case DTYPE_ON_OFF:
        /*
         * S088 P0 §2.1 D12: high-nibble tag on DTYPE_ON_OFF.
         *
         * What:       if the descriptor's dtype high nibble is non-zero,
         *             the cell is a tagged on/off sub-type (currently only
         *             ONOFF_TAG_OSC_RUN = 1). Display the tag table's name
         *             instead of on/off. If the nibble is 0, this is a
         *             standard on/off cell; the on/off path is unchanged.
         * Why:        all DTYPE_MENU ids are taken; the tag reuses the same
         *             packing pattern on a dtype whose high nibble was free.
         * Inputs:     cell dtype byte, value.
         * Outputs:    3-char name into out[].
         * Accessors:  every DTYPE_ON_OFF cell with a tagged descriptor.
         * Affiliates: oscRunNames (MenuText.h); ONOFF_TAG_OSC_RUN.
         */
        if ((uint8_t)(menu_cellDtype(cell) >> 4) != 0u) {
            uint8_t idx = (value < (uint8_t)oscRunNames[0][0]) ? value : 0u;
            memcpy(out, oscRunNames[1 + idx], 3);
        } else {
            memcpy(out, (value == 1u) ? menuText_on : menuText_off, 3);
        }
        break;
```

#### L1-8b. Site 2: `menu_repaintGeneric()` — MODIFY line 6491–6494

**Current (lines 6491–6494):**
```c
    case DTYPE_ON_OFF:
        if (value == 1u) memcpy(valueAsText, menuText_on, 3);
        else            memcpy(valueAsText, menuText_off, 3);
        break;
```

**New:**
```c
    case DTYPE_ON_OFF:
        /* S088 P0 §2.1 D12: tagged on/off sub-type display. */
        if ((uint8_t)(menu_cellDtype(cell) >> 4) != 0u) {
            uint8_t idx = (value < (uint8_t)oscRunNames[0][0]) ? value : 0u;
            memcpy(valueAsText, oscRunNames[1 + idx], 3);
        } else {
            if (value == 1u) memcpy(valueAsText, menuText_on, 3);
            else            memcpy(valueAsText, menuText_off, 3);
        }
        break;
```

#### L1-8c. Site 3: `menu_clampCellValue()` — MODIFY line 6759–6761

**Current (lines 6759–6761):**
```c
    case DTYPE_ON_OFF:
    case DTYPE_0b1:
        if (*value > 1u) *value = 1u;
        break;
```

**New:**
```c
    case DTYPE_ON_OFF:
        /*
         * S088 P0 §2.1 D12: tagged on/off sub-type clamp.
         *
         * What:       if the high nibble is non-zero, clamp to 0..count-1
         *             from the tag's name table (oscRunNames: 0..2). If the
         *             nibble is 0, clamp to 0..1 as before.
         * Why:        MIDI CC/NRPN writes can send any value 0..127; the
         *             clamp keeps the stored value within the tag's range.
         * Inputs:     cell dtype byte, *value.
         * Outputs:    *value clamped in place.
         * Affiliates: oscRunNames[0][0] = count; ONOFF_TAG_OSC_RUN.
         */
        if ((uint8_t)(menu_cellDtype(cell) >> 4) != 0u) {
            uint8_t n = (uint8_t)oscRunNames[0][0];
            if (*value >= n) *value = (uint16_t)(n - 1u);
        } else {
            if (*value > 1u) *value = 1u;
        }
        break;
    case DTYPE_0b1:
        if (*value > 1u) *value = 1u;
        break;
```

**Note:** `DTYPE_ON_OFF` and `DTYPE_0b1` previously shared one case. They
must be split so the tag check only applies to `DTYPE_ON_OFF`.

#### L1-8d. Site 4: `menu_automationValueMax()` — MODIFY line 13257–13258

**Current (lines 13257–13258):**
```c
    case DTYPE_ON_OFF:
        return 1u;
```

**New:**
```c
    case DTYPE_ON_OFF:
        /*
         * S088 P0 §2.1 D12: tagged on/off automation max.
         *
         * What:       for a tagged on/off (high nibble != 0), the automation
         *             max is count-1 from the tag's name table (oscRunNames:
         *             max = 2). For a plain on/off, max = 1 as before.
         * Why:        step automation needs the max to draw the bar and to
         *             clamp encoder increments.
         * Inputs:     descriptor dtype byte.
         * Outputs:    the uint8_t maximum value.
         * Affiliates: oscRunNames[0][0]; ONOFF_TAG_OSC_RUN.
         */
        if ((uint8_t)(descriptor->parameter_type >> 4) != 0u)
            return (uint8_t)(oscRunNames[0][0] - 1u);
        return 1u;
```

**Note:** this function receives the descriptor pointer, not a cell. The
dtype is `descriptor->parameter_type`. Verify the exact field name used in
the function signature. If it uses `menu_cellDtype(cell)`, follow that
pattern instead.

#### L1-8e. Site 5: `menu_formatAutomationValue3()` — MODIFY line 13298–13300

**Current (lines 13298–13300):**
```c
    case DTYPE_ON_OFF:
        memcpy(buf, value ? menuText_on : menuText_off, 3);
        return;
```

**New:**
```c
    case DTYPE_ON_OFF:
        /* S088 P0 §2.1 D12: tagged on/off automation display. */
        if ((uint8_t)(descriptor->parameter_type >> 4) != 0u) {
            uint8_t idx = (value < (uint8_t)oscRunNames[0][0]) ? value : 0u;
            memcpy(buf, oscRunNames[1 + idx], 3);
        } else {
            memcpy(buf, value ? menuText_on : menuText_off, 3);
        }
        return;
```

#### L1-8f. Site 6: step-automation edit display — MODIFY line 14417–14420

**Current (lines 14417–14420):**
```c
            case DTYPE_ON_OFF:
                if (value==1) memcpy(&editDisplayBuffer[1][13], menuText_on, 3);
                else               memcpy(&editDisplayBuffer[1][13], menuText_off, 3);
                break;
```

**New:**
```c
            case DTYPE_ON_OFF:
                /* S088 P0 §2.1 D12: tagged on/off step-edit display. */
                if ((uint8_t)(dtype >> 4) != 0u) {
                    uint8_t idx = (value < (uint8_t)oscRunNames[0][0]) ? value : 0u;
                    memcpy(&editDisplayBuffer[1][13], oscRunNames[1 + idx], 3);
                } else {
                    if (value==1) memcpy(&editDisplayBuffer[1][13], menuText_on, 3);
                    else               memcpy(&editDisplayBuffer[1][13], menuText_off, 3);
                }
                break;
```

**Note:** the dtype local variable in the step-edit display may already
hold the full dtype byte (from `menu_cellDtype()`). Verify which local is
available in scope.

---

### L1-9. `sequencer.c` — no change needed

`seq_rampDtypeOk()` (line 4260–4276) already returns 0 for `DTYPE_ON_OFF`
(falls through to `default`). The high-nibble tag on `DTYPE_ON_OFF` does
not affect this: the low nibble `& 0x0Fu` still reads 6, which hits the
same default. **No code change.**

---

### Layer 1 gate

`make all` must compile cleanly. The `special_tags` diagnostic and the full
existing test suite must pass unchanged. The `run` cell must display
`rst`/`fre`/`itp`, store through Kit/Scene/step-automation, and survive a
file save/load round trip (default 0 = rst).

---

## Layer 2 — `fre`: the free-run oscillator

### L2-1. `SlopeEg2.h` — new declarations

**File:** `Core/DSPAudio/SlopeEg2.h`

#### L2-1a. `slopeEg2_retriggerFrom()` — ADD after line 65

Insert after `void slopeEg2_trigger(SlopeEg2* eg);` (line 65):

```c
/*
 * Gain-matched retrigger (S088 P0 §3 Layer 2a).
 *
 * What:       restarts the envelope's attack from a given gain level so
 *             the output curve enters at the current audible gain instead
 *             of from silence. The raw internal value v is set by inverting
 *             the attack slope curve: g = f(v, invSlope) ⟹
 *             v = g / (1 + invSlope · (1 − g)). The state is set to EG_A.
 *             If the repeat count is non-zero, falls back to
 *             slopeEg2_trigger() (the repeat path restarts from 1.0).
 * Why:        in fre/itp a retrigger into an audible tail must not click.
 *             The attack curve starts where the old decay left off; the
 *             one-block gain ramp in the post-chain covers the step from
 *             the old gain to the matched attack output.
 * Inputs:     eg pointer, currentGain (the shaped output gain, 0..1).
 * Outputs:    eg->value, eg->state, eg->repeatCnt.
 * Accessors:  the four engine trigger functions when runMode != OSC_RUN_RST.
 * Affiliates: slopeEg2_calcSlopeValue(); the post-chain ramp (voicePostChain.h).
 */
void slopeEg2_retriggerFrom(SlopeEg2* eg, float currentGain);
```

#### L2-1b. `slopeEg2_isAudibleGain()` — ADD after L2-1a

```c
/*
 * Audible-gain test (S088 P0 §3 Layer 2a).
 *
 * What:       returns 1 if gain >= 1.f/32768.f (one int16 LSB), 0 otherwise.
 *             This is the threshold below which a trigger in fre/itp behaves
 *             as rst: full phase reset, filter clear, slopeEg2_trigger().
 * Why:        avoids a gain-matched retrigger when the tail is inaudible;
 *             the retrigger from silence would produce a zero-amplitude
 *             click from the curve inversion's denominator rounding.
 * Inputs:     gain (the shaped output gain, 0..1).
 * Outputs:    uint8_t 0 or 1.
 * Accessors:  the four engine trigger functions.
 * Affiliates: slopeEg2_retriggerFrom(); the four engine triggers.
 */
static inline uint8_t slopeEg2_isAudibleGain(float gain)
{
    return (gain >= (1.f / 32768.f)) ? 1u : 0u;
}
```

---

### L2-2. `SlopeEg2.c` — retriggerFrom implementation

**File:** `Core/DSPAudio/SlopeEg2.c`

#### L2-2a. Function body — ADD after `slopeEg2_trigger()` (after line 71)

```c
/*
 * slopeEg2_retriggerFrom() — gain-matched retrigger.
 *
 * What:       restarts the attack curve from the given gain. The internal
 *             raw value v is the inverse of the attack slope function:
 *                 g = (1 + invSlope) · v / (1 + invSlope · |v|)
 *             For 0 ≤ v ≤ 1 and invSlope ≥ 0:
 *                 v = g / (1 + invSlope · (1 − g))
 *             The denominator is positive for 0 ≤ g ≤ 1, invSlope ≥ 0.
 *             The resulting value is clamped to [0, 1] for safety.
 *             If repeat mode is active (eg->repeat != 0), falls back to
 *             slopeEg2_trigger() because the repeat path starts from 1.0
 *             and the gain-match entry point is undefined for repeat decay.
 * Why:        in fre/itp a retrigger into an audible tail must not click.
 *             The attack starts at the tail's gain; the output value is
 *             continuous across the retrigger boundary. The one-block
 *             post-chain ramp covers any residual step from rounding.
 * Inputs:     eg pointer, currentGain (shaped, 0..1).
 * Outputs:    eg->value, eg->state, eg->repeatCnt.
 * Math:       v = g / (1 + k(1 − g))  where k = invSlope.
 *             When g = 0: v = 0. When g = 1: v = 1. Monotonic for k ≥ 0.
 * Affiliates: slopeEg2_calcSlopeValue(val, invSlope) is the forward curve;
 *             this is its inverse for the attack leg.
 */
void slopeEg2_retriggerFrom(SlopeEg2* eg, float currentGain)
{
    /* Repeat mode: fall back to trigger(). The repeat path starts at 1.0
     * and loops the decay; there is no meaningful attack entry point. */
    if (eg->repeat) {
        slopeEg2_trigger(eg);
        return;
    }

    /* Invert the attack slope: v = g / (1 + invSlope * (1 - g)). */
    const float k = eg->invSlope;
    const float denom = 1.f + k * (1.f - currentGain);
    float v = currentGain / denom;

    /* Clamp to [0, 1] for float rounding. */
    if (v < 0.f) v = 0.f;
    else if (v > 1.f) v = 1.f;

    eg->value = v;
    eg->state = EG_A;
    eg->repeatCnt = 0;
}
```

---

### L2-3. `voicePostChain.h` — ramped gain endpoints

**File:** `Core/DSPAudio/voicePostChain.h`

All three post-chain functions gain a `lastXxxGain` endpoint argument. When
the endpoints are equal (every `rst` call and every non-retrigger block),
the ramp produces `gain` every sample — bit-identical to today.

#### L2-3a. `voicePost_drum()` — MODIFY lines 26–39

**Current signature (lines 26–28):**
```c
static inline void voicePost_drum(int16_t *buf, const float gain,
        const float lastGain, const float veloGain,
        const Distortion *dist, const uint8_t size)
```

**Note:** `voicePost_drum()` already has a gain ramp (`lastGain` → `gain`)
but does NOT ramp `veloGain`. For S088 fre, the velocity changes on a
retrigger, so the velocity must also ramp from `lastVeloGain` to
`veloGain` on the armed block.

**New signature:**
```c
/*
 * Drum amp ramp → velocity ramp → distortion (S088 P0 §3 Layer 2b).
 *
 * What:       the existing gain ramp (lastGain → gain) is unchanged.
 *             A velocity ramp (lastVeloGain → veloGain) is added: on the
 *             armed block after a fre/itp retrigger, the velocity steps
 *             from the previous hit's value to the new one across the
 *             block. On every other block (and in rst), lastVeloGain ==
 *             veloGain, so the ramp is flat: bit-identical to today.
 * Why:        without ramping, a velocity step of eg. 0.3 → 1.0 at a
 *             non-zero gain produces an audible pop.
 * Inputs:     filtered buf, current/previous amp gains, current/previous
 *             velocity gains, distortion state and size.
 * Outputs:    buf overwritten in place.
 * Accessors:  DrumVoice.c Drum_calcVoiceSyncBlock().
 * Affiliates: bufferTool_interpolatedGain(); the S073 Step 4 golden checks.
 */
static inline void voicePost_drum(int16_t *buf, const float gain,
        const float lastGain, const float veloGain,
        const float lastVeloGain,
        const Distortion *dist, const uint8_t size)
{
    uint8_t i;
    const float inv_size = 1.f/(size-1.f);
    for(i=0;i<size;i++)
    {
        /* Amp EG ramp: interpolate lastGain → gain across the block. */
        const int16_t eg = bufferTool_floatToInt16Store(
            buf[i] * bufferTool_interpolatedGain(i, inv_size, gain, lastGain));
        /* Velocity ramp: interpolate lastVeloGain → veloGain.
         * When lastVeloGain == veloGain (every non-armed block and rst):
         * the ramp is flat, so the output is bit-identical to today. */
        const float vg = bufferTool_interpolatedGain(i, inv_size,
                                                      veloGain, lastVeloGain);
        const int16_t vel = bufferTool_floatToInt16Store(eg * vg);
        buf[i] = distortion_curveSample16(dist, vel);
    }
}
```

**Call site update — `DrumVoice.c` line 619–621:**

**Current:**
```c
    voicePost_drum(buf, voice->ampFilterInput, voice->lastGain,
                   voice->volumeMod ? voice->velo : 1.0f,
                   &voice->distortion, size);
```

**New:**
```c
    voicePost_drum(buf, voice->ampFilterInput, voice->lastGain,
                   voice->volumeMod ? voice->velo : 1.0f,
                   voice->volumeMod ? voice->lastVeloGain : 1.0f,
                   &voice->distortion, size);
```

**Bookkeeping — ADD after the `voicePost_drum()` call:**
```c
    /* Update the velocity ramp endpoint for the next block.
     * After the armed block, lastVeloGain == velo, so the ramp is flat. */
    voice->lastVeloGain = voice->volumeMod ? voice->velo : 1.0f;
```

#### L2-3b. `voicePost_mixAddGainDist()` — MODIFY lines 47–59

**New signature and body:**
```c
/*
 * Snare mix → saturating add → amp gain ramp → distortion (S088 P0 Layer 2b).
 *
 * What:       adds a lastAmpGain endpoint. The loop ramps from lastAmpGain
 *             to ampGain across the block. When they are equal (every non-
 *             armed block and rst), the ramp is flat: bit-identical.
 * Why:        a velocity or EG step on a fre/itp retrigger produces a pop
 *             without the ramp.
 * Inputs:     filtered buf, transient add[], mix, current/previous amp gains,
 *             distortion state and size.
 * Outputs:    buf overwritten in place.
 * Accessors:  Snare.c Snare_calcSyncBlockVoice().
 * Affiliates: bufferTool_interpolatedGain(); S073 Step 4 golden checks.
 */
static inline void voicePost_mixAddGainDist(int16_t *buf, const int16_t *add,
        const float mix, const float ampGain, const float lastAmpGain,
        const Distortion *dist, const uint8_t size)
{
    uint8_t j;
    const float inv_size = 1.f/(size-1.f);
    for(j=0;j<size;j++)
    {
        const int16_t m = bufferTool_floatToInt16Store(buf[j] * mix);
        const int16_t s = bufferTool_satAdd16(m, add[j]);
        /* Amp gain ramp: interpolate lastAmpGain → ampGain across the block. */
        const float ag = bufferTool_interpolatedGain(j, inv_size,
                                                      ampGain, lastAmpGain);
        const int16_t g = bufferTool_floatToInt16Store(s * ag);
        buf[j] = distortion_curveSample16(dist, g);
    }
}
```

**Call site update — `Snare.c` lines 303–308:**

**Current:**
```c
    {
        const float ampGain = voice->volumeMod
            ? voice->velo * voice->egValueOscVol
            : voice->egValueOscVol;
        voicePost_mixAddGainDist(buf, transBuf, voice->mix, ampGain,
                                 &voice->distortion, size);
    }
```

**New:**
```c
    {
        const float ampGain = voice->volumeMod
            ? voice->velo * voice->egValueOscVol
            : voice->egValueOscVol;
        voicePost_mixAddGainDist(buf, transBuf, voice->mix, ampGain,
                                 voice->lastAmpGain,
                                 &voice->distortion, size);
        /* Update the ramp endpoint. After the armed block, lastAmpGain ==
         * ampGain, so the ramp is flat. */
        voice->lastAmpGain = ampGain;
    }
```

#### L2-3c. `voicePost_addGainDist()` — MODIFY lines 67–77

**New signature and body:**
```c
/*
 * Cymbal/HiHat saturating add → amp gain ramp → distortion (S088 Layer 2b).
 *
 * What:       adds a lastAmpGain endpoint. Same ramp logic as
 *             voicePost_mixAddGainDist().
 * Inputs:     filtered buf, transient/mod add[], current/previous amp gains,
 *             distortion state and size.
 * Outputs:    buf overwritten in place.
 * Accessors:  CymbalVoice.c, HiHat.c.
 * Affiliates: bufferTool_interpolatedGain(); S073 Step 4 golden checks.
 */
static inline void voicePost_addGainDist(int16_t *buf, const int16_t *add,
        const float ampGain, const float lastAmpGain,
        const Distortion *dist, const uint8_t size)
{
    uint8_t j;
    const float inv_size = 1.f/(size-1.f);
    for(j=0;j<size;j++)
    {
        const int16_t s = bufferTool_satAdd16(buf[j], add[j]);
        /* Amp gain ramp: interpolate lastAmpGain → ampGain. */
        const float ag = bufferTool_interpolatedGain(j, inv_size,
                                                      ampGain, lastAmpGain);
        const int16_t g = bufferTool_floatToInt16Store(s * ag);
        buf[j] = distortion_curveSample16(dist, g);
    }
}
```

**Call site updates:**

**`CymbalVoice.c` lines 332–336 → same pattern as Snare:**
```c
    {
        const float ampGain = voice->volumeMod
            ? voice->velo * voice->egValueOscVol
            : voice->egValueOscVol;
        voicePost_addGainDist(buf, mod, ampGain, voice->lastAmpGain,
                              &voice->distortion, size);
        voice->lastAmpGain = ampGain;
    }
```

**`HiHat.c` lines 364–368 → same pattern:**
```c
    {
        const float ampGain = voice->volumeMod
            ? voice->velo * voice->egValueOscVol
            : voice->egValueOscVol;
        voicePost_addGainDist(buf, mod1, ampGain, voice->lastAmpGain,
                              &voice->distortion, size);
        voice->lastAmpGain = ampGain;
    }
```

---

### L2-4. Engine voice headers — ramp state fields

#### L2-4a. `DrumVoice.h` — ADD after `runMode` (L1-4a)

```c
    /*
     * Amp ramp armed flag (S088 P0 §3 Layer 2a).
     *
     * What:       set to 1 by the trigger function when runMode != RST and
     *             the amp gain is audible. Cleared to 0 by the render
     *             function after the ramp block completes. When 0, the
     *             post-chain ramp endpoints are equal (flat).
     * Why:        the render function uses this to know the current block
     *             is the armed block and that lastVeloGain carries the
     *             previous hit's value.
     * Inputs:     Drum_triggerVoice(). Outputs: Drum_calcVoiceSyncBlock().
     * Affiliates: voicePost_drum(); lastVeloGain below.
     */
    uint8_t ampRampArm;

    /*
     * Previous-block velocity gain (S088 P0 §3 Layer 2b).
     *
     * What:       the velocity gain that was applied on the previous block's
     *             post-chain: volumeMod ? velo : 1.0f. Updated after each
     *             post-chain call. On the armed block after a fre/itp
     *             retrigger, this still holds the previous hit's velocity,
     *             so the post-chain ramps from it to the new velo.
     * Why:        without a ramp endpoint, the velocity steps at the block
     *             boundary and clicks.
     * Inputs:     Drum_calcVoiceSyncBlock() updates after voicePost_drum().
     * Outputs:    passed to voicePost_drum() as lastVeloGain.
     * Affiliates: voicePost_drum(); ampRampArm above.
     */
    float lastVeloGain;
```

#### L2-4b. `Snare.h` — ADD after `runMode` (L1-4b)

```c
    /*
     * Amp ramp armed flag (S088 P0 §3 Layer 2a).
     *
     * What:       set to 1 by trigger when runMode != RST and gain is
     *             audible. Cleared after the ramp block.
     * Inputs:     Snare_triggerVoice(). Outputs: Snare_calcSyncBlockVoice().
     * Affiliates: voicePost_mixAddGainDist(); lastAmpGain below.
     */
    uint8_t ampRampArm;

    /*
     * Previous-block compound amp gain (S088 P0 §3 Layer 2b).
     *
     * What:       the ampGain value (velo * egValueOscVol, or egValueOscVol
     *             alone) from the previous block. Updated after each post-
     *             chain call. On the armed block, this still holds the
     *             previous hit's gain, so the post-chain ramps to the new one.
     * Inputs:     Snare_calcSyncBlockVoice() updates after voicePost.
     * Outputs:    passed as lastAmpGain.
     * Affiliates: voicePost_mixAddGainDist(); ampRampArm above.
     */
    float lastAmpGain;
```

#### L2-4c. `CymbalVoice.h` — ADD after `runMode` (L1-4c)

Same as Snare: `uint8_t ampRampArm;` and `float lastAmpGain;` with comment
blocks adapted for Cymbal.

#### L2-4d. `HiHat.h` — ADD after `runMode` (L1-4d)

Same as Snare: `uint8_t ampRampArm;` and `float lastAmpGain;`.

---

### L2-5. Engine init functions — zero ramp fields

#### L2-5a–d. All four engines' init functions

After the `voice->runMode = OSC_RUN_RST;` added in L1-5, add:

```c
    voice->ampRampArm = 0u;
```

For Drum:
```c
    voice->lastVeloGain = 1.0f;
```

For Snare, Cymbal, HiHat:
```c
    voice->lastAmpGain = 0.0f;
```

---

### L2-6. `ResonantFilter.h` / `ResonantFilter.c` — filter coefficient slew

**File:** `Core/DSPAudio/ResonantFilter.h`

#### L2-6a. Declaration — ADD after line 118

After `void SVF_recalcFreq(ResonantFilter* filter);` (line 118):

```c
/*
 * Filter coefficient slew (S088 P0 §3 Layer 2d).
 *
 * What:       steps filter->g from its current value toward fastTan(π · f)
 *             by a per-block slew constant (OSC_RUN_FILTER_SLEW, ~0.28).
 *             One call per async block when runMode is fre or itp.
 * Why:        in rst the filter is cleared and SVF_recalcFreq() sets g
 *             directly. In fre/itp the filter state is kept; jumping g
 *             to the new frequency would produce a transient. The slew
 *             rate ≈ 0.28 per block converges in ≈ 2 ms, matching the
 *             S085 P2 spike tuning.
 * Inputs:     filter pointer (filter->f must already be set to the new
 *             cutoff by the caller's async path).
 * Outputs:    filter->g stepped toward the new target.
 * Accessors:  Drum_calcVoiceAsync(), Snare_calcAsyncVoice(),
 *             Cymbal_calcAsyncVoice(), HiHat_calcAsyncVoice().
 * Affiliates: SVF_recalcFreq(); the engine async functions.
 */
void SVF_recalcFreqSlew(ResonantFilter* filter);
```

**File:** `Core/DSPAudio/ResonantFilter.c`

#### L2-6b. Implementation — ADD after `SVF_recalcFreq()` (after line 113)

```c
/*
 * SVF_recalcFreqSlew() — filter coefficient slew.
 *
 * What:       computes the target g = fastTan(π · f), then steps the
 *             current g toward the target by a fraction of the difference:
 *             g += OSC_RUN_FILTER_SLEW · (target − g).
 *             The slew constant 0.28 is tuned to converge in ≈ 7 blocks
 *             (≈ 5 ms) at 44,108 Hz / 32-sample blocks. A constant of 1.0
 *             would jump (= SVF_recalcFreq), 0 would freeze.
 * Why:        in fre/itp the filter state is not cleared, so jumping the
 *             coefficient causes a transient proportional to the frequency
 *             change × resonance. The slew spreads the change over several
 *             blocks. Cost: one fastTan + one multiply + one add per async
 *             block, identical to SVF_recalcFreq + one MAD.
 * Math:       g_new = g_old + 0.28 · (fastTan(π·f) − g_old).
 * Inputs:     filter->f (the new cutoff, already set by the caller).
 * Outputs:    filter->g.
 * Affiliates: fastTan() (this file); SVF_recalcFreq().
 */
#define OSC_RUN_FILTER_SLEW  0.28f
void SVF_recalcFreqSlew(ResonantFilter* filter)
{
    const float target = fastTan((float)M_PI * filter->f);
    filter->g += OSC_RUN_FILTER_SLEW * (target - filter->g);
#if USE_SHAPER_NONLINEARITY
    setDistortionShape(&filter->shaper, filter->drive);
#endif
}
```

---

### L2-7. Engine trigger functions — fre behaviour

Each engine's trigger function gains a `runMode` branch. When
`runMode != OSC_RUN_RST`:

1. Skip the main oscillator phase reset (the carrier continues running).
2. Gain-matched retrigger: call `slopeEg2_retriggerFrom()` instead of
   `slopeEg2_trigger()` when the gain is audible.
3. Arm the ramp: set `ampRampArm = 1`.
4. Skip `SVF_reset()` (Drum) or equivalent filter clear.

The pitch envelope, snap envelope, transient, LFO and mod osc restart
are unchanged (they restart in all modes).

#### L2-7a. `DrumVoice.c` `Drum_triggerVoice()` — MODIFY lines 201–300

The phase-reset block (lines 201–263) is wrapped in a runMode guard:

```c
    /*
     * Phase reset guard (S088 P0 §3 Layer 2c).
     *
     * What:       when runMode is fre or itp, the main oscillator phase is
     *             NOT reset. The carrier continues from its current phase;
     *             osc_setFreq() in the next async block glides the increment
     *             to the new pitch. When runMode is rst, the original phase-
     *             reset chain (S087 D, S086 P3, transient offset) runs
     *             unchanged.
     * Why:        fre means free-run; itp resets the carrier but that is
     *             handled later in Layer 3 (the snapshot + crossfade).
     *             For Layer 2, itp's carrier also continues (the crossfade
     *             renders the snapshot; the live carrier keeps running).
     * Inputs:     voice->runMode (OSC_RUN_RST/FRE/ITP).
     * Outputs:    voice->osc.phase is either reset (rst) or untouched.
     * Affiliates: the Layer 3 itp snapshot (L3-*).
     */
    if (voice->runMode == OSC_RUN_RST) {
        /* --- original phase-reset block (lines 201–263, unchanged) --- */
        ... (keep existing code here verbatim)
    }
    /* fre/itp: carrier phase is not reset. */
```

The amp envelope section (line 291) gains a conditional:

```c
    /*
     * Gain-matched retrigger (S088 P0 §3 Layer 2a).
     *
     * What:       if runMode != RST and the current amp gain is audible
     *             (≥ 1/32768), call slopeEg2_retriggerFrom() to start the
     *             attack from the current gain. Arm the post-chain ramp
     *             (ampRampArm = 1) so the render function knows this block
     *             must ramp from the old gain to the new one. If inaudible,
     *             behave as rst: slopeEg2_trigger(), no ramp arm.
     * Why:        a retrigger into an audible tail must not click; the
     *             attack starts where the decay left off.
     * Inputs:     voice->egValueOscVol (the previous block's shaped gain),
     *             voice->runMode.
     * Outputs:    voice->oscVolEg state, voice->ampRampArm.
     * Math:       retriggerFrom inverts the slope curve (see SlopeEg2.c).
     * Affiliates: slopeEg2_retriggerFrom(); slopeEg2_isAudibleGain();
     *             the post-chain ramp in voicePost_drum().
     */
    if (voice->runMode != OSC_RUN_RST &&
        slopeEg2_isAudibleGain(voice->egValueOscVol)) {
        slopeEg2_retriggerFrom(&voice->oscVolEg, voice->egValueOscVol);
        voice->ampRampArm = 1u;
    } else {
        slopeEg2_trigger(&voice->oscVolEg);
    }
```

(This replaces the standalone `slopeEg2_trigger(&voice->oscVolEg);` at
line 291.)

The filter reset (line 299) gains a guard:

```c
    /*
     * Filter reset guard (S088 P0 §3 Layer 2c).
     *
     * What:       in rst the filter is cleared as today. In fre/itp the
     *             filter state is kept; the async path calls
     *             SVF_recalcFreqSlew() instead of SVF_recalcFreq() so the
     *             coefficient converges to the new cutoff without a jump.
     * Inputs:     voice->runMode.
     * Outputs:    voice->filter (cleared or untouched).
     */
    if (voice->runMode == OSC_RUN_RST)
        SVF_reset(&voice->filter);
```

(This replaces the standalone `SVF_reset(&voice->filter);` at line 299.)

#### L2-7b. `Snare.c` `Snare_triggerVoice()` — MODIFY lines 140–188

Same pattern:

1. **Phase reset (lines 140–176):** wrap in `if (voice->runMode == OSC_RUN_RST)`.
2. **Amp envelope (line 179):** replace `slopeEg2_trigger()` with the
   conditional `retriggerFrom`/`trigger` block.
3. **Filter reset:** Snare's trigger doesn't call `SVF_reset()` explicitly
   (no standalone reset line). If there is a reset path, guard it. If not,
   no change for the filter here — the slew is in the async path (L2-8).

#### L2-7c. `CymbalVoice.c` `Cymbal_triggerVoice()` — MODIFY lines 167–213

1. **Phase reset (lines 167–198):** wrap in `if (voice->runMode == OSC_RUN_RST)`.
2. **Mod osc phase reset (lines 200–201):** in `itp`, the modulators are
   NOT reset (§1.3 A4). Wrap `voice->modOsc.phase = 0; voice->modOsc2.phase = 0;`
   in `if (voice->runMode == OSC_RUN_RST)`.
3. **Amp envelope (line 207):** conditional `retriggerFrom`/`trigger`.

#### L2-7d. `HiHat.c` `HiHat_triggerVoice()` — MODIFY lines 181–238

Same pattern as Cymbal. Additionally:

- **Decay selection (line 231):** `voice->oscVolEg.decay = isOpen ? voice->decayOpen : voice->decayClosed;`
  — this line runs in ALL modes. The decay rate is set to the new open/closed
  selection regardless of run mode (A9: the matched attack decays at the
  new rate).

---

### L2-8. Engine async functions — filter slew

Each engine's async function currently calls `SVF_recalcFreq()` (or the
inline equivalent through `SVF_directSetFilterValue()`). When
`runMode != OSC_RUN_RST`, call `SVF_recalcFreqSlew()` instead.

#### L2-8a. `DrumVoice.c` `Drum_calcVoiceAsync()` — MODIFY

Find the filter frequency recalculation path. If it calls
`SVF_recalcFreq(&voice->filter)` or updates `filter.g` directly, add:

```c
    /*
     * Filter coefficient path (S088 P0 §3 Layer 2d).
     *
     * What:       in rst, SVF_recalcFreq() sets g directly (jump). In
     *             fre/itp, SVF_recalcFreqSlew() steps g toward the target
     *             by the slew constant, converging in ≈ 2 ms.
     * Why:        in fre/itp the filter state is not cleared; a g jump
     *             causes a transient at high resonance.
     * Inputs:     voice->runMode, voice->filter.f (already set by the
     *             caller's frequency path).
     * Outputs:    voice->filter.g.
     * Affiliates: SVF_recalcFreq(); SVF_recalcFreqSlew().
     */
    if (voice->runMode != OSC_RUN_RST)
        SVF_recalcFreqSlew(&voice->filter);
    else
        SVF_recalcFreq(&voice->filter);
```

**Note:** if the filter frequency is set through the InstrumentManager
special writer (`IM_SPECIAL_FILTER_FREQ`, which calls
`SVF_directSetFilterValue()` then `SVF_recalcFreq()`), the slew must be
applied in the async path AFTER the writer has set `filter.f`, not inside
the writer. The async path runs once per block; the writer runs on value
change. Identify the exact call site. If the async path does NOT currently
call `SVF_recalcFreq()` (because the writer already did), then the async
path must gain a `SVF_recalcFreqSlew()` call when `runMode != RST`.

#### L2-8b–d. Snare, Cymbal, HiHat async functions — same pattern

Each gains the same `runMode` guard on its filter coefficient path.

---

### L2-9. Drum FM depth ramp (F-A)

**File:** `Core/DSP/Instruments/Drum/DrumVoice.c`

#### L2-9a. `Drum_calcVoiceSyncBlock()` — MODIFY the FM render path

In the `fm` case of the mod-mode switch (around lines 492–580), the FM
modulator block is currently rendered and applied directly. In S088, a
per-sample linear ramp from `oldDepth` to `newDepth` is applied across
the block, every block, in every mode (constant-CPU, D1).

```c
    /*
     * FM depth ramp (S088 P0 §3 Layer 2e, D1: constant).
     *
     * What:       the FM modulator block is scaled by a linear ramp from
     *             the previous block's fmModAmount * egPitchVal product to
     *             the current one. On most blocks oldDepth ≈ newDepth and
     *             the ramp is near-flat; on a retrigger block the mod
     *             envelope restarted so the depth jumps and the ramp covers
     *             the step.
     * Why:        without the ramp, a depth jump produces a click on the
     *             FM carrier proportional to the modulator amplitude at the
     *             block boundary. The ramp runs in every mode (constant-CPU);
     *             flat endpoints (oldDepth == newDepth) add only a multiply
     *             + add per sample.
     * Cost:       +15 instructions per sample on Drum fm, every block (D1).
     * Inputs:     the modulator block, oldDepth (stored from previous block),
     *             newDepth (this block's fmModAmount * egPitchVal).
     * Outputs:    the modulator block, scaled in place.
     * Affiliates: voice->osc.fmMod; Drum_calcVoiceAsync() egPitchVal path.
     */
```

The implementation: store the previous block's `fmMod` (or the depth
product) in a new field `float lastFmDepth` on DrumVoice, and ramp the
modulator block from `lastFmDepth` to the current depth. The field is
updated after the ramp.

**DrumVoice.h — ADD `float lastFmDepth;`** near the other ramp fields:

```c
    /*
     * Previous-block FM depth (S088 P0 §3 Layer 2e).
     *
     * What:       the fmModAmount * egPitchVal product from the previous
     *             block. Used as the ramp start for the FM depth ramp.
     * Inputs:     Drum_calcVoiceSyncBlock() (fm path) updates after the ramp.
     * Outputs:    read at the start of the next fm render.
     * Affiliates: the FM depth ramp (D1); fmModAmount; egPitchVal.
     */
    float lastFmDepth;
```

**Drum_initVoice() — ADD:**
```c
    voice->lastFmDepth = 0.0f;
```

---

### L2-10. Drum `rng` — "ramp always" in `fre`/`itp` (A8, D7)

**File:** `Core/DSP/Instruments/Drum/DrumVoice.c`

#### L2-10a. `Drum_triggerVoice()` — MODIFY line 290

The current `voice->ringSnap = 1u;` (line 290) always sets snap. In
`fre`/`itp`, the ring depth should ramp from the current depth instead of
snapping (A8, D7):

```c
    /*
     * rng depth snap vs ramp (S088 P0 A8, D7).
     *
     * What:       in rst, ringSnap = 1 (today's snap). In fre/itp,
     *             ringSnap = 0 so the next rng block ramps from the
     *             current ringDepthQ15 to the new target.
     * Why:        user requested "ramp always in free-run mode" (S085 P1
     *             follow-up); snapping the depth from a decayed tail
     *             to full ring produces a click in fre.
     * Inputs:     voice->runMode.
     * Outputs:    voice->ringSnap.
     * Affiliates: the rng depth ramp in Drum_calcVoiceSyncBlock();
     *             the ringSnap comment in DrumVoice.h.
     */
    voice->ringSnap = (voice->runMode == OSC_RUN_RST) ? 1u : 0u;
```

---

### Layer 2 gate

1. `make all` clean.
2. Host test `ampretrig`: gain match, audible test, ramp wiring. `rst`
   produces 0 differing samples against the pre-S088 post-chain golden.
3. Host test `postchain`: 0 differing samples in `rst`. Cost report:
   Drum ≤ 35, Snare ≤ 35, Cymbal/HiHat ≤ 30.
4. Host test `osc_run_fre`: continuous output on retrigger, bounded
   sample-to-sample step on the armed block.
5. `armcheck` cost probe: Drum `fm` ceiling must hold.

---

## Layer 3 — `itp`: the interpolate mode

### L3-1. `OscCarrierSnapshot` struct

**File:** `Core/DSPAudio/Oscillator.h` (or a new header
`Core/DSPAudio/OscCarrierSnapshot.h` included by the engine headers)

#### L3-1a. Struct definition — ADD after the `OSC_RUN_*` constants (L1-3a)

```c
/*
 * Carrier snapshot for itp crossfade (S088 P0 §3 Layer 3a).
 *
 * What:       a frozen copy of the carrier's render state at the moment of
 *             an itp retrigger. The snapshot renders alongside the live
 *             carrier for 132 samples (≈ 3 ms, D10), blending from old to
 *             new via a linear crossfade.
 * Why:        the itp mode renders the old waveform position and pitch for
 *             a brief overlap with the new trigger, producing a smooth
 *             transition instead of a hard cut. The snapshot is rendered
 *             as a plain carrier (S085 P1 Q14: no FM/sync/ring on the old
 *             copy).
 * Fields:     phase, phaseInc, waveform, tableOffset (the minimum state
 *             for calcNextOscSampleBlock / osc_renderTyped). oscType for
 *             typed dispatch. Sample cache fields for smp/slp/wtb types.
 *             active (1 while the crossfade runs), counter (remaining
 *             blocks, decrements each block).
 * Size:       ≤ 28 bytes. All four engines carry one instance whether or
 *             not itp is used (constant-size mandate).
 * Inputs:     the engine trigger function (fills on itp retrigger).
 * Outputs:    the engine render function (reads each block while active).
 * Accessors:  oscSection_renderOldCarrier() helper.
 * Affiliates: the itp crossfade logic (Layer 3a); modNode interp slots.
 */
typedef struct OscCarrierSnapshotStruct {
    uint32_t    phase;
    uint32_t    phaseInc;
    uint8_t     waveform;
    uint8_t     tableOffset;
    uint8_t     oscType;
    uint8_t     active;       /* 1 while crossfade runs, 0 otherwise */
    uint8_t     counter;      /* remaining 32-sample blocks (132/32 ≈ 5) */
    uint8_t     pad[3];       /* alignment padding */
    /* Sample cache fields for smp/slp/wtb snapshot rendering: */
    uint8_t     sampleCacheIndex;
    uint8_t     sampleCacheLooped;
    uint16_t    pad2;
    uint32_t    sampleCacheGeneration;
    uint32_t    sampleCacheSize;
    const int16_t* sampleCacheData;
} OscCarrierSnapshot;
```

**`_Static_assert` — ADD after the struct:**
```c
_Static_assert(sizeof(OscCarrierSnapshot) <= 32u,
               "OscCarrierSnapshot must fit in 32 bytes (slot margin)");
```

---

### L3-2. Engine voice headers — snapshot and itp fields

#### L3-2a. `DrumVoice.h` — ADD after `lastFmDepth` (L2-9)

```c
    /*
     * Carrier snapshot for itp crossfade (S088 P0 §3 Layer 3a).
     *
     * What:       holds the frozen carrier state during an active itp
     *             crossfade. When snapshot.active == 0, the struct is
     *             inert (no cost). When active, the render function calls
     *             oscSection_renderOldCarrier() into a scratch buffer and
     *             blends with the live output.
     * Size:       ≤ 28 bytes; within the Drum runtime slot margin (596 B).
     * Inputs:     Drum_triggerVoice() fills on itp retrigger.
     * Outputs:    Drum_calcVoiceSyncBlock() reads while active.
     * Affiliates: OscCarrierSnapshot; oscSection_renderOldCarrier().
     */
    OscCarrierSnapshot snapshot;
```

#### L3-2b–d. `Snare.h`, `CymbalVoice.h`, `HiHat.h` — same field

Each gains `OscCarrierSnapshot snapshot;` with the same comment block
adapted for that engine's names.

---

### L3-3. Engine init functions — zero snapshot

#### L3-3a–d. All four engines

After the ramp field inits (L2-5), add:

```c
    voice->snapshot.active = 0u;
    voice->snapshot.counter = 0u;
```

---

### L3-4. `oscSection_renderOldCarrier()` helper

**File:** New function in an appropriate location (e.g.
`Core/DSPAudio/Oscillator.c` or a new `Core/DSPAudio/oscSection.c`; or
as a `static inline` in a header if the function is small enough).

#### L3-4a. Function — ADD

```c
/*
 * oscSection_renderOldCarrier() — plain carrier render for itp (S088 P0 §3 Layer 3c).
 *
 * What:       renders the old carrier snapshot into buf for one block.
 *             Dispatches to osc_renderTyped() when the snapshot is a typed
 *             oscillator (smp/slp/wtb with oscType != OSC_TYPE_WAV), else
 *             to calcNextOscSampleBlock() (the stock wave path).
 *             The carrier is rendered PLAIN: no FM, no sync, no ring mod
 *             (S085 P1 Q14). The modulators drive only the live carrier.
 * Why:        one dispatch site for all four engines. The plain render
 *             keeps the cost to one carrier loop + 23 blend per sample.
 * Inputs:     snapshot (OscCarrierSnapshot*, phase/phaseInc/waveform/etc.),
 *             buf (int16_t* output buffer), size (block size, 32).
 * Outputs:    buf filled with the old carrier's samples; snapshot.phase
 *             advanced by size * phaseInc.
 * Accessors:  the four engine render functions when snapshot.active.
 * Affiliates: osc_renderTyped(); calcNextOscSampleBlock();
 *             OscCarrierSnapshot; the itp crossfade blend loop.
 */
void oscSection_renderOldCarrier(OscCarrierSnapshot *snap,
                                  int16_t *buf, uint8_t size);
```

The implementation builds a temporary `OscInfo`-compatible view from the
snapshot fields (or casts directly if the snapshot's layout matches the
relevant OscInfo fields) and calls the existing render function. The
snapshot's phase is advanced by the render. The function must NOT apply
FM, sync or ring mod to the old carrier.

---

### L3-5. Engine trigger functions — itp snapshot

Each engine's trigger function, when `runMode == OSC_RUN_ITP` and the amp
gain is audible, fills the snapshot and starts the crossfade:

#### L3-5a. `DrumVoice.c` `Drum_triggerVoice()` — ADD inside the trigger, after the retriggerFrom block

```c
    /*
     * itp carrier snapshot (S088 P0 §3 Layer 3a).
     *
     * What:       if runMode is ITP and the amp gain is audible, snapshot
     *             the carrier's current state (phase, phaseInc, waveform,
     *             type, sample cache) into voice->snapshot. Set
     *             snapshot.active = 1 and snapshot.counter to the crossfade
     *             block count (132 / OUTPUT_DMA_SIZE ≈ 5, rounded up).
     *             If the amp is fully decayed, itp behaves as rst (A3):
     *             no snapshot, no crossfade — the phase-reset block ran.
     * Why:        the crossfade blends the old carrier with the new one
     *             for ≈ 3 ms, producing a smooth transition.
     * Inputs:     voice->osc (current carrier state), voice->runMode,
     *             voice->egValueOscVol (audible test).
     * Outputs:    voice->snapshot filled and activated.
     * Affiliates: OscCarrierSnapshot; oscSection_renderOldCarrier();
     *             the itp blend in Drum_calcVoiceSyncBlock().
     */
    if (voice->runMode == OSC_RUN_ITP && voice->ampRampArm) {
        voice->snapshot.phase = voice->osc.phase;
        voice->snapshot.phaseInc = voice->osc.phaseInc;
        voice->snapshot.waveform = voice->osc.waveform;
        voice->snapshot.tableOffset = voice->osc.tableOffset;
        voice->snapshot.oscType = voice->osc.oscType;
        voice->snapshot.sampleCacheIndex = voice->osc.sampleCacheIndex;
        voice->snapshot.sampleCacheLooped = voice->osc.sampleCacheLooped;
        voice->snapshot.sampleCacheGeneration = voice->osc.sampleCacheGeneration;
        voice->snapshot.sampleCacheSize = voice->osc.sampleCacheSize;
        voice->snapshot.sampleCacheData = voice->osc.sampleCacheData;
        voice->snapshot.active = 1u;
        voice->snapshot.counter = (uint8_t)((132u + OUTPUT_DMA_SIZE - 1u) / OUTPUT_DMA_SIZE);
    }
```

**Note:** the carrier phase IS reset in `itp` (§1.3: "Main oscillator
phase: Reset as rst"). So after the snapshot is taken, the phase-reset
block (from the `rst` path) must also run for `itp`. This means the phase-
reset guard (L2-7a) should be:
```c
    if (voice->runMode != OSC_RUN_FRE) {
        /* phase reset runs for rst AND itp */
        ...
    }
```
(Not `== OSC_RUN_RST` as first stated in L2-7a. The snapshot is taken
BEFORE the reset, so the snapshot captures the current phase, then the
reset starts the new carrier from the beginning.)

**Correction to L2-7a:** the phase-reset guard should read:
```c
    if (voice->runMode != OSC_RUN_FRE) {
```
This way `rst` and `itp` both reset the phase, while `fre` does not.

#### L3-5b–d. Snare, Cymbal, HiHat — same snapshot pattern

Each engine's trigger fills its `voice->snapshot` from its `voice->osc`
with the same fields. Cymbal and HiHat also note that the modulator phases
(`modOsc.phase`, `modOsc2.phase`) are NOT snapshotted — the modulators
keep running (§1.3).

---

### L3-6. Engine render functions — itp crossfade blend

Each engine's sync-block render function, when `snapshot.active`, renders
the old carrier into a scratch buffer, blends with the live output, and
decrements the counter.

#### L3-6a. `DrumVoice.c` `Drum_calcVoiceSyncBlock()` — ADD after the carrier render

After the main carrier render (before the transient and filter sections),
insert:

```c
    /*
     * itp crossfade blend (S088 P0 §3 Layer 3a).
     *
     * What:       when snapshot.active, render the old carrier into a
     *             scratch buffer via oscSection_renderOldCarrier(), then
     *             blend: out[i] = (1−t)·old[i] + t·new[i], where t ramps
     *             linearly from the crossfade's progress to progress +
     *             blockSize/132. Decrement snapshot.counter; when it reaches
     *             0, set snapshot.active = 0 (crossfade complete).
     * Why:        the blend produces a smooth transition from the old
     *             carrier position/pitch to the new one over ≈ 3 ms.
     * Cost:       one carrier render (plain, ≤ dearest osc loop) + 23
     *             instructions per sample for the blend. Budgeted as
     *             always full (2 slots); constant-CPU.
     * Inputs:     voice->snapshot, buf (the live carrier output), size.
     * Outputs:    buf overwritten with the blended output;
     *             voice->snapshot.counter decremented.
     * Math:       t = (totalSamples − remainingSamples + i) / totalSamples.
     *             Linear in i. Full-block blend (D4): no early termination.
     * Affiliates: oscSection_renderOldCarrier(); OscCarrierSnapshot;
     *             the itp trigger snapshot (L3-5a).
     */
    if (voice->snapshot.active) {
        int16_t oldBuf[OUTPUT_DMA_SIZE];
        oscSection_renderOldCarrier(&voice->snapshot, oldBuf, size);

        /* Cross-fade progress: how far into the 132-sample total. */
        const uint8_t totalBlocks = (uint8_t)((132u + OUTPUT_DMA_SIZE - 1u) / OUTPUT_DMA_SIZE);
        const uint8_t elapsed = totalBlocks - voice->snapshot.counter;
        const float totalSamples = (float)(totalBlocks * OUTPUT_DMA_SIZE);
        const float blockStart = (float)(elapsed * OUTPUT_DMA_SIZE);

        for (uint8_t i = 0; i < size; i++) {
            /* t ramps from 0.0 (start) to 1.0 (end of crossfade). */
            const float t = (blockStart + (float)i) / totalSamples;
            /* Blend: (1−t)·old + t·new. */
            buf[i] = (int16_t)((1.f - t) * (float)oldBuf[i] + t * (float)buf[i]);
        }

        voice->snapshot.counter--;
        if (voice->snapshot.counter == 0u)
            voice->snapshot.active = 0u;
    }
```

#### L3-6b–d. Snare, Cymbal, HiHat — same crossfade pattern

Each engine's render function gains the same crossfade block after its
carrier render, before the transient/filter/post-chain stages.

---

### L3-7. `mixer.c` — itp slot priority in block-start sequence

**File:** `Core/DSPAudio/mixer.c`

#### L3-7a. Slot bookkeeping — ADD before line 866

Insert before `modNode_resetTargets();` (line 866):

```c
    /*
     * itp crossfade slot accounting (S088 P0 §3 Layer 3b).
     *
     * What:       before modNode_resetTargets() advances the generation
     *             stamp, count the active itp crossfades across all 6 slots.
     *             These slots are reserved: modNode_resetTargets() will see
     *             a reduced available count. Claim order per block:
     *             1. Ongoing itp crossfades (never cut mid-fade).
     *             2. Wavetable frame blending (modNode_interpClaim()).
     *             3. New itp crossfades, lowest slot first.
     * Why:        the two interpolation slots are shared between wtb frame
     *             blending and itp crossfades (S088 P0 §1.3). Ongoing
     *             fades have priority; wtb steps if both are occupied.
     * Inputs:     the six runtime slots' snapshot.active flags.
     * Outputs:    modNode_interpReserved (the number of slots pre-claimed
     *             by active itp fades).
     * Accessors:  this block-start sequence.
     * Affiliates: modNode_resetTargets(); OSC_WAVE_INTERP_MAX_ACTIVE;
     *             the engine trigger functions (itp snapshot L3-5).
     */
    {
        uint8_t itpActive = 0u;
        for (uint8_t s = 0u; s < 6u; s++) {
            if (instrumentManager_slotHasActiveItpFade(s))
                itpActive++;
        }
        modNode_interpReserved = itpActive;
    }
```

**Note:** `instrumentManager_slotHasActiveItpFade(s)` is a new query
function that returns 1 if the slot's engine struct has `snapshot.active`.
This requires a small helper in `InstrumentManager.c` that reads the
snapshot flag through the tagged runtime slot:

**`InstrumentManager.c` — ADD helper:**

```c
/*
 * instrumentManager_slotHasActiveItpFade() (S088 P0 §3 Layer 3b).
 *
 * What:       returns 1 if the given slot's current engine instance has an
 *             active itp crossfade (snapshot.active == 1), 0 otherwise.
 * Why:        mixer.c's block-start sequence needs to count active fades
 *             before modNode_resetTargets(). The slot's engine type
 *             determines which struct to read.
 * Inputs:     slot index (0..5).
 * Outputs:    uint8_t 0 or 1.
 * Affiliates: the itp slot accounting in mixer.c; the snapshot fields.
 */
uint8_t instrumentManager_slotHasActiveItpFade(uint8_t slot);
```

**`InstrumentManager.h` — ADD declaration.**

**`modulationNode.c` — MODIFY `modNode_resetTargets()`:**

The function must subtract `modNode_interpReserved` from the available slot
count so that `modNode_interpClaim()` does not overwrite itp-held slots.
The `modNode_interpReserved` static is already declared at line 68
(`static INCCMZ uint8_t modNode_interpReserved = 0u;`). Its value is set
by the mixer sequence above before this function runs.

---

### Layer 3 gate

1. `make all` clean.
2. Host test `osc_run_itp`: retrigger with itp + audible amp crossfades
   (132 samples). Retrigger with itp + decayed amp behaves as rst.
3. Slot-exhaustion test: all 2 slots occupied by itp fades → new itp
   trigger falls back to fre (no crossfade, no phase reset, gain-matched
   retrigger only).
4. Cost probe: one itp crossfade ≤ dearest carrier render + 23 blend per
   sample.
5. `rst` produces 0 differing samples against the pre-S088 golden.

---

## Layer 4 — Tests, proofs, specifications

### L4-1. Host tests

| Test | What it covers |
|------|----------------|
| `ampretrig` | Gain-matched retrigger: curve inversion, audible threshold, repeat fallback. |
| `postchain` | Ramped endpoints: 0 diff in rst; bounded ramp in fre/itp. Drum ≤ 35, Snare ≤ 35, Cymbal/HiHat ≤ 30 instructions. |
| `osc_run_fre` | Phase continuity, pitch slew, filter coefficient slew, velocity ramp. |
| `osc_run_itp` | Snapshot correctness, crossfade shape, slot priority, fre fallback on exhaustion. |
| `osc_run_rst_golden` | rst mode produces 0 differing samples against the pre-S088 post-chain golden for all four engines. |

### L4-2. ARM cost probes

| Probe | Target |
|-------|--------|
| `armcheck-postchain` | Drum ≤ 35, Snare ≤ 35, Cymbal/HiHat ≤ 30 per sample. |
| `armcheck-fm-fa` | Drum fm + F-A ramp ≤ the fm ceiling (≈ 92 + 15 = 107). |
| `armcheck-itp-blend` | One itp crossfade ≤ dearest carrier render + 23 per sample. |
| `elf_osc_check` | Full ELF against pre-S088 snapshot. |

### L4-3. Break proofs

Each new host test must include deliberate break proofs: inputs that should
fail are verified to fail.

### L4-4. Specification updates

| File | What changes |
|------|-------------|
| `INSTRUMENTS_DSP_REFERENCE.md` | Add osc1_run parameter, three modes, trigger behaviour per mode. |
| `CPU_USE_DSP_AUDIT.md` | Post-chain ramp costs, F-A ramp, itp pool budget. |
| `STORAGE_SRAM_MANIFEST.md` | Engine struct growth (runMode, ampRampArm, lastVeloGain/lastAmpGain, lastFmDepth, snapshot). Slot margin update. |
| `FILESYSTEM_SPEC.md` | `osc1_run` key, default 0. |
| `MEMORY.md` | S088 summary. |
| `MIDI_SPEC.md` | osc1_run NRPN / Learn ID (auto-assigned by the append). |

---

## Change summary by file

| File | Layer | Changes |
|------|:-----:|---------|
| `Core/Menu/MenuText.h` | 1 | ADD `ONOFF_TAG_OSC_RUN`, `oscRunNames` |
| `Core/DSP/Instruments/InstrumentManager.h` | 1, 3 | ADD `IM_SPECIAL_OSC_RUN` enum value; ADD `instrumentManager_slotHasActiveItpFade()` declaration |
| `Core/DSP/Instruments/InstrumentManager.c` | 1, 3 | ADD `IM_SPECIAL_OSC_RUN` case in writer switch; ADD `instrumentManager_slotHasActiveItpFade()` body |
| `Core/DSPAudio/Oscillator.h` | 1, 3 | ADD `OSC_RUN_*` constants; ADD `OscCarrierSnapshot` struct |
| `Core/DSP/Instruments/Drum/DrumVoice.h` | 1, 2, 3 | ADD `runMode`, `ampRampArm`, `lastVeloGain`, `lastFmDepth`, `snapshot` |
| `Core/DSP/Instruments/Drum/DrumVoice.c` | 1, 2, 3 | MODIFY `Drum_initVoice()` (zero new fields); MODIFY `Drum_triggerVoice()` (phase guard, retriggerFrom, filter guard, snapshot, ring snap); MODIFY `Drum_calcVoiceAsync()` (filter slew); MODIFY `Drum_calcVoiceSyncBlock()` (FM depth ramp, itp blend, velocity bookkeeping) |
| `Core/DSP/Instruments/Drum/DrumParameters.c` | 1 | ADD `ROW_SELECT_ONOFF_SPECIAL` macro, `DRUM_PARAM_OSC1_RUN` enum, osc1_run descriptor row, page cell 5; MODIFY `DRUM_PARAM_DESCRIPTOR_COUNT` |
| `Core/DSP/Instruments/Snare/Snare.h` | 1, 2, 3 | ADD `runMode`, `ampRampArm`, `lastAmpGain`, `snapshot` |
| `Core/DSP/Instruments/Snare/Snare.c` | 1, 2, 3 | Same pattern as Drum (init, trigger, async, render) |
| `Core/DSP/Instruments/Snare/SnareParameters.c` | 1 | Same pattern as Drum (macro, enum, row, page cell 7) |
| `Core/DSP/Instruments/Cymbal/CymbalVoice.h` | 1, 2, 3 | ADD `runMode`, `ampRampArm`, `lastAmpGain`, `snapshot` |
| `Core/DSP/Instruments/Cymbal/CymbalVoice.c` | 1, 2, 3 | Same pattern as Snare + mod osc phase guard |
| `Core/DSP/Instruments/Cymbal/CymbalParameters.c` | 1 | Same pattern as Drum (cell 5) |
| `Core/DSP/Instruments/HiHat/HiHat.h` | 1, 2, 3 | ADD `runMode`, `ampRampArm`, `lastAmpGain`, `snapshot` |
| `Core/DSP/Instruments/HiHat/HiHat.c` | 1, 2, 3 | Same pattern as Cymbal + decay selection unchanged |
| `Core/DSP/Instruments/HiHat/HiHatParameters.c` | 1 | Same pattern as Drum (cell 5) |
| `Core/DSPAudio/SlopeEg2.h` | 2 | ADD `slopeEg2_retriggerFrom()` decl, `slopeEg2_isAudibleGain()` inline |
| `Core/DSPAudio/SlopeEg2.c` | 2 | ADD `slopeEg2_retriggerFrom()` body |
| `Core/DSPAudio/voicePostChain.h` | 2 | MODIFY all three post-chain functions (add last-gain endpoint, per-sample ramp) |
| `Core/DSPAudio/ResonantFilter.h` | 2 | ADD `SVF_recalcFreqSlew()` declaration |
| `Core/DSPAudio/ResonantFilter.c` | 2 | ADD `SVF_recalcFreqSlew()` body, `OSC_RUN_FILTER_SLEW` constant |
| `Core/DSPAudio/mixer.c` | 3 | ADD itp slot accounting block before `modNode_resetTargets()` |
| `Core/DSPAudio/modulationNode.c` | 3 | MODIFY `modNode_resetTargets()` to subtract `modNode_interpReserved` from available slots |
| `Core/DSPAudio/Oscillator.c` (or new file) | 3 | ADD `oscSection_renderOldCarrier()` body |
| `Core/Menu/menu.c` | 1 | MODIFY 6 `DTYPE_ON_OFF` switch sites (lines 3346, 6491, 6759, 13257, 13298, 14417) |
| `Core/Sequencer/sequencer.c` | — | No change (`seq_rampDtypeOk` already excludes `DTYPE_ON_OFF`) |

---

## Implementation order

```
Layer 1  ──┐
            ├── GATE: make all, special_tags, rst golden
Layer 2a ──┤
Layer 2b ──┤
Layer 2c ──┤   (2a–2e can be done in sequence within one commit)
Layer 2d ──┤
Layer 2e ──┘
            ├── GATE: ampretrig, postchain, osc_run_fre, armcheck
Layer 3a ──┤
Layer 3b ──┤   (3a–3c in sequence)
Layer 3c ──┘
            ├── GATE: osc_run_itp, slot exhaustion, cost probe, rst golden
Layer 4  ──┘
            └── GATE: all host tests, ARM probes, spec updates
```

---

*End of implementation schedule.*

---

# Progress log (implementation)

Working notes, appended step by step during S088 P0 implementation.
Line numbers below are the post-edit facts; the schedule above cites the
pre-edit tree.

## 0. Baseline observations (before any edit)

| Check | Result |
|-------|--------|
| `git status` | only the two S088 docs untracked; tree at `2ab359eb` |
| `make all` | ELF builds; the recipe then fails at `tools/link_budget.py` (the file lives in `tools/diagnostic_tools/`). Pre-existing, not touched. |
| `make -C tools/dsp_test all` | every module target passes; the last target `stock_tables` fails because `tools/plots/gen_stock_tables.py` resolves its table path from the repository root instead of `tools/`. Pre-existing, not touched. |
| `voice_control` | the 6 `FAIL law 1/law 4` lines are the intentional `-DBREAK_DRAIN_AFTER_FIRE` run; the real run is 0 failures. |

Gate used from here: `make build/lxr02.elf` (compile + link) plus the
`tools/dsp_test` targets that do not depend on the two broken tool paths.

## Layer 1 — complete

Implemented as scheduled, with these recorded deviations:

1. **`osc1_run` member path is `runMode`, not `osc.runMode`.** D9 stores the
   byte on the engine struct (DrumVoice/SnareVoice/CymbalVoice/HiHatVoice),
   outside `OscInfo`; `offsetof(Engine, osc.runMode)` does not exist. The row
   uses `ROW_SELECT_ONOFF_SPECIAL(..., runMode, ...)`.
2. **`IM_SPECIAL_OSC_RUN` is ordinal 21 (0-based), the 22nd tag.** The plan's
   "value 22" is the count.
3. **The special-tag classifier had to learn `osc1_run`.** The boot self-check
   (and `check_special_tags.py`) compares every row tag with the independent
   key classifier; a new tag without the classifier entry is a mismatch.
   `check_special_tags.py` gained the matching `expected()` branch.
4. **`ROW_SELECT_ONOFF_SPECIAL` rows are written on one physical line** so
   `check_special_tags.py` (which parses one row per line) still sees them.
5. **The engine struct is needed for the descriptor offset.** `osc1_type`
   offsets into `OscInfo` (`osc.oscType`); `osc1_run` offsets into the engine
   struct.

### Files touched in Layer 1

| File | Change |
|------|--------|
| `Core/Menu/MenuText.h` | `ONOFF_TAG_OSC_RUN`, `oscRunNames` |
| `Core/DSP/Instruments/InstrumentManager.h` | `IM_SPECIAL_OSC_RUN` |
| `Core/DSPAudio/Oscillator.h` | `OSC_RUN_RST/FRE/ITP/COUNT` |
| `Drum/DrumVoice.h`, `Snare/Snare.h`, `Cymbal/CymbalVoice.h`, `HiHat/HiHat.h` | `uint8_t runMode` |
| the four engine `.c` init functions | `runMode = OSC_RUN_RST` |
| `InstrumentManager.c` | writer case + classifier |
| the four `*Parameters.c` | macro, enum, row, osc page cell |
| the four `*Parameters.h` | descriptor count +1 |
| `Core/Menu/menu.c` | six `DTYPE_ON_OFF` sites (clamp split from `DTYPE_0b1`) |
| `tools/dsp_test/check_special_tags.py` | `osc1_run` rule |
| `tools/dsp_test/modules/instrument_manager.c` | laws 6/7, run row |
| `tools/dsp_test/Makefile` | extract `ROW_SELECT_ONOFF_SPECIAL`, `ONOFF_TAG_OSC_RUN`; on/off-tag break build |

### Layer 1 gate result

- `make build/lxr02.elf`: clean (only pre-existing warnings in
  `PatternData.c`, `EuklidGenerator.c`).
- `special_tags`: OK (167 rows, 0 mismatches).
- `instrument_manager`: 0 failures; both old breaks and the new on/off-tag
  break are caught.
- `oscillator`, `modulation`, `voice_control`, `sequencer`, `samples`,
  `sampleoptimize`: unchanged, 0 failures.

## Layer 2 — complete

Implemented as scheduled. Decisions worth recording:

1. **`ampRampArm` gates every new ramp endpoint.** The schedule's call sites
   (L2-3a/L2-3b) pass `lastVeloGain`/`lastAmpGain` unconditionally. That would
   smooth a velocity step in `rst` too and break the §1.1 bit-identity gate.
   The call sites pass the previous endpoint only when the trigger armed the
   ramp; every other block passes `last == current`, where
   `bufferTool_interpolatedGain()` is exactly the block gain. Host law B1
   proves the equality.
2. **`behavesAsRst` instead of `runMode == OSC_RUN_RST`** for the filter
   clear, the ring snap and the Cymbal/HiHat modulator reset. D6/A3 say a
   fully decayed `itp` retrigger is exactly `rst`; the literal condition would
   have kept the filter ringing and the ring depth ramping in that case.
3. **The Drum FM ramp runs whenever the trigger armed it, including `itp`.**
   The schedule says "flat under an active itp crossfade"; ramping is a
   superset (the depth still ends at the target) and avoids the click if the
   fade ends before the envelope settles. The loop always runs (constant cost,
   D1) and is the identity when `start == 1.0f`.
4. **The post-chain ramp cannot divide by `size - 1` when `size == 1`.**
   All three functions use `inv_size = (size > 1) ? 1/(size-1) : 0`. The
   mixer always passes `OUTPUT_DMA_SIZE`; the guard only protects host calls
   from `0 x inf = NaN`.
5. **Filter slew lives in the per-block dispatcher.**
   `instrumentManager_recalcSlotFilter()` is already called once per block per
   slot from `mixer_calcNextSampleBlock()` before the async pass, so it
   chooses `SVF_recalcFreqSlew()` vs `SVF_recalcFreq()` on `runMode`. No
   engine async function gained a call.
   The cutoff *writer* also had to change: `SVF_directSetFilterValue()` sets
   `f` and jumps `g`, which would defeat the slew at the control-rate write.
   `SVF_setFrequencyTarget()` (f only) is used by `IM_SPECIAL_FILTER_FREQ`
   when the slot's run mode is not rst; the filter is still cleared and set
   directly in rst, exactly as before.
   Only the cutoff coefficient `g` is slewed. `SVF_recalcFreq()` never touched
   `q`, and resonance changes already arrive at control rate; if hardware
   listening (G2) finds a resonance step in fre/itp, extend
   `SVF_recalcFreqSlew()` to step `q` as well.
6. **The inversion covers the whole slope knob.** An earlier draft clamped
   `k` at -0.99 because the knob's top looked like it produced `k < -1`.
   `slopeEg2_setSlope()` actually yields `k = 2a/(1-a)` for a net amount
   `a` in (-1, 1): knob 0 is `k = +3998`, knob 127 is `k = -0.99975`, so
   `k > -1` strictly and `1 + k(1-g)` stays positive for every `g`. The clamp
   was removed; host law A1 now sweeps slopes 0/32/64/96/127 against ten
   gains with the S085 float-resolution tolerance, and A2b proves the
   matched attack never dips.

### Files touched in Layer 2

| File | Change |
|------|--------|
| `SlopeEg2.h/.c` | `slopeEg2_retriggerFrom()`, `slopeEg2_isAudibleGain()` |
| `voicePostChain.h` | velocity ramp (Drum), amp ramps (Snare, Cymbal/HiHat) |
| `ResonantFilter.h/.c` | `SVF_recalcFreqSlew()`, `SVF_setFrequencyTarget()`, `OSC_RUN_FILTER_SLEW` 0.28 |
| `InstrumentManager.c` | dispatcher chooses the slew path; cutoff writer sets f only in fre/itp |
| `DrumVoice.h/.c` | `ampRampArm`, `lastVeloGain`, `lastFmDepth`, trigger, FM ramp, post-chain |
| `Snare.h/.c`, `CymbalVoice.h/.c`, `HiHat.h/.c` | `ampRampArm`, `lastAmpGain`, trigger, post-chain |
| `tools/dsp_test/modules/run_modes.c` (new) | laws A1-A4, B1-B4 |
| `tools/dsp_test/Makefile` | `run_modes` target + gain-match break build |

### Layer 2 gate result

- `make build/lxr02.elf`: clean.
- `run_modes`: 0 failures; the deliberate `v = currentGain` break is caught.
- `rst` bit-identity: proven at the post-chain level (B1/B4) and by
  construction for the trigger (the original trigger path runs,
  `ringSnap = 1`, `SVF_reset()`, `SVF_recalcFreq()`).
- ARM hot-loop check (`elf_loopdump.py` + `elf_hotloops.py`) against an ELF
  built from `git archive HEAD` (pre-S088): the only grown per-sample loops
  are the three post-chains, at exactly the planned costs (below).

| Loop | Base | S088 | Budget |
|------|-----:|-----:|-------:|
| Drum post-chain | 32 | 35 | 35 (G4) |
| Snare post-chain | 30 | 35 | 35 (G4) |
| Cymbal/HiHat post-chain | 25 | 30 | 30 (G4) |
| Drum FM depth ramp (new, no base counterpart) | - | 10 | +15 planned, ~107 total (G1) |

No unrelated loop grew, so the S086 P3 register-pressure hazard did not
reappear. G1 therefore holds by the measured +10: the frozen ≈ 92 Drum `fm`
figure plus 10 = about 102, below the 107 ceiling.

## Layer 3 — complete

Implemented as scheduled. Decisions worth recording:

1. **`OscCarrierSnapshot` is 28 B on ARM / 32 B on the host** (asserted
   `<= 32`). The pointer is first so the host/ARM difference shows only in the
   final padding. `positionApplied` was added to the schedule's field list:
   the wavetable plain copy needs the frozen frame.
2. **One shared 64 B scratch, not one per engine.** `osc_itpScratch[...]` in
   `Oscillator.c`; `oscSection_renderOldCarrier()` renders into it and blends
   into the live block, so the four engines need no new locals. Measured
   `nm`: `osc_itpScratch` 0x40 B in SRAM1 `.bss`.
3. **The helper owns the progress, the counter and the release.** Its
   signature is `(snap, buf, size)`; the schedule's `t0`/`tstep` are derived
   inside from `OSC_RUN_ITP_FADE_BLOCKS` and `snap->counter`, so the four
   engines cannot drift. `t` is exactly 0 on the first sample of a fresh fade
   (host law I2).
4. **Slot reserve needs a setter.** The schedule said "set
   `modNode_interpReserved` before `modNode_resetTargets()`", but
   `modNode_resetTargets()` assigns that byte itself. Added
   `modNode_setItpReserved()` writing a separate `modNode_itpReserved`, which
   `resetTargets()` folds in as `reserved = lastBlockWaveGrants + itpFades`.
   Host law L6 proves one fade admits exactly one wavetable grant and two
   fades admit none.
5. **Claim decision.** The engine trigger asks
   `instrumentManager_itpFadeSlotAvailable(source_slot)`: it counts active
   fades in the other slots (re-snapshotting this voice reuses its own slot)
   and approves below `OSC_WAVE_INTERP_MAX_ACTIVE`. No wavetable state is
   consulted because wtb claims happen later in the render; a wtb that wanted
   the slot steps for the fade, as R5 accepts.
6. **The phase guard adds the refused-fade exception.**
   `phaseReset = runMode != FRE && !(itpRequested && !itpFade)`: rst and a
   granted itp reset, a decayed itp resets (exactly rst), fre and a refused
   itp do not.
7. **The snapshot is taken before `osc_setBaseNote()` and the phase reset**, so
   it holds the previous block's phase and pitch (Q6: `pos` is already folded
   into `positionApplied`; `positionBendStep` never applies to the old copy).

### Files touched in Layer 3

| File | Change |
|------|--------|
| `Oscillator.h` | `OSC_RUN_ITP_SAMPLES`, `OSC_RUN_ITP_FADE_BLOCKS`, `OscCarrierSnapshot`, helper declaration |
| `Oscillator.c` | `osc_itpScratch`, `oscSection_renderOldCarrier()` |
| `InstrumentManager.h/.c` | `slotHasActiveItpFade`, `activeItpFadeCount`, `itpFadeSlotAvailable` |
| `modulationNode.h/.c` | `modNode_setItpReserved()`, reserved sum in `resetTargets()` |
| `mixer.c` | per-block fade count before `modNode_resetTargets()` |
| four engine `.h/.c` | `snapshot` field, init, trigger snapshot, render blend |
| `tools/dsp_test/modules/oscillator.c` | `itp` group I1-I4 |
| `tools/dsp_test/modules/modulation.c` | law L6 |

### Layer 3 gate result

- `make build/lxr02.elf`: clean.
- `oscillator` (incl. the `itp` group): 0 failures; `modulation` incl. L6: 0.
- `rst` bit-identity: the helper is not called (`snapshot.active == 0`) and
  the reserve term is 0 (no itp fades), so `modNode_resetTargets()` behaves
  exactly as in S087.
- ARM cost (`elf_loopdump.py` on `oscSection_renderOldCarrier`): the blend
  loop is **15 instructions per sample** (two int16 converts, two
  multiply-adds, the store and the loop), against the 23 budgeted (G5). The
  old carrier adds one plain carrier loop, as planned.

## Layer 4 — tests and specifications

- Host tests added: `run_modes` (new target, A1-A4/B1-B4, break build),
  `oscillator` group `itp` (I1-I4), `modulation` law L6,
  `instrument_manager` laws 6/7 plus the on/off-tag break build, and the
  `check_special_tags.py` rule for `osc1_run`.
- Specifications updated: `INSTRUMENTS_DSP_REFERENCE.md` §6.1 (run modes),
  `CPU_USE_DSP_AUDIT.md` (S088 additions 50-54), `STORAGE_SRAM_MANIFEST.md`
  (measured sizes, slot reserve, scratch), `FILESYSTEM_SPEC.md` (`osc1_run`
  key), `MIDI_SPEC.md` (§5.5), `MEMORY.md` (status + binding rule).

**Not added: engine-level `osc_run_fre` / `osc_run_itp` / `rst` golden.**
This tree has no host harness for the four voice engines (they include
InstrumentManager, modulationNode, the LFO/transient/snap EGs and the
sample-flash catalogue; the spike's `test_osc_run.c` tested a standalone
`OscSection.c` module that the S088 design does not have - D3 chose one
shared helper instead of a section module). The gates that can run in this
tree are covered: `run_modes` (A1-A4, B1-B4, the gain-match break),
`oscillator` group `itp` (I1-I4, i.e. the old-carrier render and blend), and
`modulation` law L6 (the slot reserve). The engine-level sequences (a
retrigger with `fre`, a retrigger with `itp` audible/decayed, the refused
claim falling back to `fre`, and the `rst` A/B through a real voice) remain
hardware checks H1-H17 and are the first thing to add if an engine harness
is built later.

### Measured RAM (ARM `sizeof` probe; `nm` on the linked ELF)

| Object | Size | Delta |
|--------|-----:|------:|
| `DrumVoice` | 632 B | +36 B |
| `SnareVoice` | 452 B | fits its slot reserve |
| `CymbalVoice` | 508 B | fits its slot reserve |
| `HiHatVoice` | 516 B | fits its slot reserve |
| `OscCarrierSnapshot` | 28 B | new |
| `runtime_slots` | 7,584 B | +432 B SRAM1 (6 x 2 x 632) |
| `osc_itpScratch` | 64 B | new SRAM1 `.bss` |
| `modNode_itpReserved` | 1 B | new DTCM |

Whole build (final): `text 676,344`, `data 420`, `bss 412,892` (+500 B vs
S087 close, +5,440 text); flash headroom 77,320 B; ITCM 12,640 → 12,656 B
(+16 B from LTO re-specialising the wtb blend copies now that the helper
calls the plain wtb copy).

## Remaining gates (hardware / ARM measurement)

- **G1 (Drum `fm` ceiling with the F-A ramp)**: measured +10 per sample
  (about 102 total) against the 107 ceiling; keep the hardware stress Scene
  (H16) as the final word.
- **G4 (post-chain cost)**: measured 35/35/30, as planned (table above).
- **G5 (itp pool worst case)**: blend measured at 15 per sample (budget 23);
  the two-carrier system cost with all six voices still needs the hardware
  `cpu` figure (H16).
- Hardware matrix H1-H17 in `S088_P0_OSC_RUN_MODES.md` §10, in particular
  H1 (`rst` A/B), H7/H8 (`itp` audible/decayed), H11 (carrier-only `itp` on
  Drum `fm`), H15 (step automation of `run`) and H16 (worst-case CPU).
- `elf_osc_check`: `elf_symdump.py --compare` of `calcNextOscSampleBlock`
  between the baseline ELF (built from `git archive HEAD`) and the S088 ELF
  differs only in LTO's private names for the three plain sample/wavetable
  copies (`osc_*PlainBlock` -> `.lto_priv.0`); the dispatch itself is
  unchanged.
- `elf86_mixer_check`: the `elf_hotloops.py` comparison of
  `mixer_calcNextSampleBlock` is in the table above; no unrelated loop grew.

## Pre-existing tooling failures observed (not changed)

- `make all` ends with `python3 tools/link_budget.py` although the script is
  at `tools/diagnostic_tools/link_budget.py`. The ELF is built before that
  step; the gate used here is `make build/lxr02.elf`.
- `make -C tools/dsp_test all` ends at `stock_tables`, whose script
  (`tools/plots/gen_stock_tables.py`) resolves its paths from the repository
  root instead of `tools/`. Every module target run individually passes.

## Final verification (commands run last)

| Command | Result |
|---------|--------|
| `make build/lxr02.elf` | 0 errors (only the pre-existing `filesystem.c` unused-function warnings); `text 676,344`, `bss 412,892`, ITCM 12,656 |
| `make -C tools/dsp_test oscillator` | all groups 0, including the new `itp` group; both builds |
| `make -C tools/dsp_test modulation` | 0 (includes law L6) |
| `make -C tools/dsp_test instrument_manager` | 0; gate/row/on-off breaks caught |
| `make -C tools/dsp_test run_modes` | 0; gain-match break caught |
| `voice_control`, `sequencer`, `samples`, `sampleoptimize` | 0 failures; sequencer/samples breaks caught |
| `python3 tools/dsp_test/check_special_tags.py` | `OK (167 rows, 0 mismatches)` |
| `elf_loopdump`/`elf_hotloops` vs baseline ELF | post-chains 35/35/30; FM ramp 10; blend 15; no unrelated growth |
| `elf_symdump --compare calcNextOscSampleBlock` | only LTO private-name renames |

Absolute paths of the two core new helpers: `Core/DSPAudio/SlopeEg2.c`
(`slopeEg2_retriggerFrom`) and `Core/DSPAudio/Oscillator.c`
(`oscSection_renderOldCarrier`). Generated host artifacts live in
`tools/dsp_test/build/` (untracked, as the test bench documents).

---

## Independent diff assessment (appended by review)

**Scope:** every unstaged diff (`git diff`) and every untracked new file in
the working tree was read against the schedule sections L1-1 through L4-4.
The review covers the firmware source (Core/), the host test harness
(tools/dsp_test/), and the five specification documents.

### Verdict

The implementation is faithful to the schedule. Every scheduled change is
present. No file was touched that the schedule does not mention. The six
deviations the document already records (Layer 1 items 1–5, Layer 2 items
1–6, Layer 3 items 1–7) are real and correctly described. No additional
deviations were found beyond those already noted.

### Layer-by-layer confirmation

**Layer 1 (parameter).** All eight schedule sections (L1-1 through L1-8)
are present. `ONOFF_TAG_OSC_RUN` is 1, `oscRunNames` has the right `{3}`
count guard, the six `menu.c` sites all check `(dtype >> 4) != 0` before
branching, the clamp site correctly splits from `DTYPE_0b1`, and the four
`*Parameters.c` files each have `ROW_SELECT_ONOFF_SPECIAL` with the
`ONOFF_TAG_OSC_RUN` tag and the `IM_SPECIAL_OSC_RUN | IM_SPECIAL_OSC1`
special. Page cell placements: Drum cell 5, Snare cell 7, Cymbal cell 5,
HiHat cell 5 — all match the plan. `IM_SPECIAL_OSC_RUN` is ordinal 21
(fits the 5-bit mask), and the classifier entry in `InstrumentManager.c`
maps `"osc1_run"` correctly.

**Layer 2 (fre).** `slopeEg2_retriggerFrom()` implements the curve
inversion `v = g / (1 + k*(1-g))` with the repeat fallback (A3/A10), the
clamp, and the cleared repeat counter. `slopeEg2_isAudibleGain()` uses
`>= 1.f/32768.f` (A4). All three `voicePostChain.h` functions gained the
`lastGain` / `lastVeloGain` parameter with per-sample ramp and the
`size == 1` safety. `SVF_recalcFreqSlew()` steps `g` by 0.28 per block;
`SVF_setFrequencyTarget()` sets `f` without jumping `g`. The four engine
trigger functions each implement the `behavesAsRst` / `ampAudible` /
`ampRampArm` decision tree: `prevGain` captured before the EG call,
`retriggerFrom` when audible and not `behavesAsRst`, `ringSnap =
behavesAsRst` (A8/D7). Drum gains the FM depth ramp (constant cost,
start ratio clamped 0..4, identity when not armed). The centralized
filter slew in `instrumentManager_recalcSlotFilter()` (rather than four
async sites) is a clean improvement over the schedule's L2-8.

**Layer 3 (itp).** `OscCarrierSnapshot` is 28 B with `_Static_assert
<= 32`. `oscSection_renderOldCarrier()` builds an `OscInfo` view from
the snapshot, dispatches via `sampleCacheLooped` to the correct plain
block function, crossfades with accumulated `t` (no per-sample divide),
and decrements the counter to inactive. The trigger functions each
implement `Drum_fillItpSnapshot` / `Snare_fillItpSnapshot` /
`Cymbal_fillItpSnapshot` / `HiHat_fillItpSnapshot` before the phase
reset, and blend the old carrier into the render output after position
restore. `modNode_setItpReserved()` and the `mixer.c` accounting block
(`instrumentManager_activeItpFadeCount()` before `modNode_resetTargets()`)
correctly implement the slot priority. `instrumentManager_itpFadeSlotAvailable(exclude_slot)` is a useful
improvement: a voice re-triggering can reuse its own snapshot slot.
Cymbal and HiHat correctly guard the modulator phase resets behind
`behavesAsRst`.

**Layer 4 (tests / specs).** Five new or extended test sites:

| Target | Laws | Break build |
|--------|------|-------------|
| `run_modes` (new) | A1–A4 (gain match, audible threshold), B1–B4 (post-chain bit-identity and ramps) | `v = currentGain` defeats A1 |
| `oscillator` group `itp` (new) | I1 (snapshot == plain carrier), I2 (blend starts at t=0), I3 (counter → inactive), I4 (wtb snapshot, no claim) | — |
| `modulation` law L6 (new) | one fade admits one wtb; two fades admit none | — |
| `instrument_manager` laws 6–7, break | osc1_run appended last; tagged ONOFF with flags | tag-stripped macro defeats law 7 |
| `check_special_tags.py` | `osc1_run` → `{IM_SPECIAL_OSC_RUN, osc}` | — |

All five specification documents are updated:
- `INSTRUMENTS_DSP_REFERENCE.md` §6.1: full run-mode table, retrigger
  rules, snapshot rendering, slot priority.
- `CPU_USE_DSP_AUDIT.md`: additions 50–54 (retrigger, post-chains, FM
  ramp, crossfade, filter slew).
- `STORAGE_SRAM_MANIFEST.md`: measured sizes (DrumVoice 632, slots 7584,
  scratch 64, itpReserved 1), link figures updated.
- `FILESYSTEM_SPEC.md`: `osc1_run` key with backwards-compat note
  (older firmware reads the tag as a plain on/off).
- `MIDI_SPEC.md`: `osc1_run` appended after `osc1_type`, NRPN follows
  automatically.
- `MEMORY.md`: status updated, binding rule for `ampRampArm` and filter
  slew added.

### Improvements over the schedule

Six implementation choices improve on or refine the schedule:

1. **`SVF_setFrequencyTarget()`** (L2, decision 5): the schedule's L2-8
   had the engines call `SVF_directSetFilterValue()` in their async
   functions; the implementation instead adds a `f`-only setter so the
   `IM_SPECIAL_FILTER_FREQ` writer does not jump `g`, and moves the
   `recalcFreq` vs `recalcFreqSlew` decision into the existing per-block
   dispatcher. Fewer call sites, one decision point.
2. **`instrumentManager_itpFadeSlotAvailable(exclude_slot)`** (L3,
   decision 5): the schedule counted all active fades; the implementation
   excludes the requesting voice's own slot so a re-snapshot reuses it
   rather than double-counting.
3. **Centralized filter slew** (L2, decision 5): in
   `instrumentManager_recalcSlotFilter()` rather than four engine async
   functions. Avoids four near-identical sites.
4. **`size == 1` guard** (L2, decision 4): the three post-chain functions
   guard against `1/(size-1)` when `size == 1`. The firmware always passes
   `OUTPUT_DMA_SIZE` (32), so this is a host-safety addition with zero
   ARM cost.
5. **Accumulated `t` crossfade** (L3, decision 3): the helper owns the
   progress math internally (`t0 + step * i`), so no per-sample divide
   and no caller-side error.
6. **`behavesAsRst` unification** (L2, decision 2): a single boolean
   gates filter clear, ring snap, and modulator reset, correctly covering
   both `rst` and the decayed-`itp` case (D6/A3).

### Items remaining (hardware only)

No host-testable item is missing. The remaining gates are the hardware
matrix H1–H17 from the plan §10, in particular:
- H1: `rst` A/B (bit-identical to the pre-S088 firmware on a real kit).
- H7/H8: `itp` audible / decayed retrigger.
- H11: carrier-only `itp` on Drum `fm`.
- H15: step automation of `run`.
- H16: worst-case CPU with all six voices in `itp`, both fade slots held.
- G1: the Drum `fm` ceiling with the depth ramp (measured +10 on the ELF;
  hardware stress confirms).
- G2: resonance step in `fre`/`itp` (if audible, extend
  `SVF_recalcFreqSlew()` to step `q`).
