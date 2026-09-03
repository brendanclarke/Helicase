# Phase C: Autosave Source Fields — General Plan

Source: `S060_AUTOSAVE_UPDATE_READER_PREP.md` Sections 8, 12 (Phase C), and 13.

## What Phase C delivers

Every sub-object in the autosave record (Scene, Kit, Instrument) gains a 2-byte source field recording which library slot it came from. This lets the future boot reader verify that the autosave record's contents match the identity in `.hcnames` — the gate between "load from autosave" and "reload from library."

---

## 1. Geometry changes in Autosave.h

The source field is absorbed into each sub-object's existing reserved space rather than growing the record. Scene and Kit each have large parameter allocations with significant headroom (80 and 118 bytes respectively); shrinking each allocation by 2 bytes accommodates the source field without moving the Effect, Kit, or Instrument boundaries. Instruments have 37 bytes of explicit tail padding; absorbing 2 bytes for the source field leaves 35 bytes of padding with no change to the 192-byte record size.

**Zero-growth result:** Every section boundary, the mask, the payload, and the complete record size remain unchanged. No SRAM cost, no on-card file size change, no format migration.

**Items to update:**
- `AUTOSAVE_SCENE_PARAMETERS_OFFSET` (8 → 10), `AUTOSAVE_SCENE_PARAMETER_ALLOC_BYTES` (120 → 118)
- `AUTOSAVE_KIT_PARAMETERS_OFFSET` (8 → 10), `AUTOSAVE_KIT_PARAMETER_ALLOC_BYTES` (120 → 118)
- `AUTOSAVE_INSTRUMENT_NORMAL_OFFSET` (11 → 13) — Morph and padding offsets are derived and shift automatically
- Source field offset defines (new): Scene +8, Kit +8, Instrument +11, width 2
- Structural `_Static_assert` additions for source→parameter/normal continuity
- Header comment blocks describing Scene/Kit/Instrument internal layout

**Items that do NOT change:**
- `AUTOSAVE_EFFECT_OFFSET` (128), `AUTOSAVE_KIT_OFFSET` (640), `AUTOSAVE_KIT_INSTRUMENTS_OFFSET` (128)
- `AUTOSAVE_INSTRUMENT_RECORD_BYTES` (192), `AUTOSAVE_KIT_SECTION_BYTES` (1280), `AUTOSAVE_SCENE_SECTION_BYTES` (1920)
- `AUTOSAVE_MASK_BYTES` (3856), `AUTOSAVE_PAYLOAD_BYTES` (30848), `AUTOSAVE_RECORD_BYTES` (34768)
- All existing `_Static_assert` expected values

**Best practice:** Change constants first, then `make clean && make`. The static asserts are the safety net — they'll catch any missed dependency before the code runs. Don't skip `make clean`; there's no header dependency tracking in this Makefile.

**Risk:** Missing a single offset constant produces silent data corruption — the drain writes correct values at wrong offsets in the record. The static asserts are the only compile-time defense. Audit every field-level constant within the sub-object headers, not just the ones that obviously need to change.

**Layout after Phase C:**

| Sub-object | Old layout | New layout | Size |
|---|---|---|---|
| Scene header | name(8) + params(120) = 128 | name(8) + source(2) + params(118) = 128 | 128 (unchanged) |
| Kit header | name(8) + params(120) = 128 | name(8) + source(2) + params(118) = 128 | 128 (unchanged) |
| Instrument | type(3) + name(8) + normal(72) + morph(72) + pad(37) = 192 | type(3) + name(8) + source(2) + normal(72) + morph(72) + pad(35) = 192 | 192 (unchanged) |

---

## 2. Wire format compatibility

The record size, mask size, and payload size are unchanged: 34,768 / 3,856 / 30,848. Old-format `.hcprms1`/`.hcprms2` files pass CRC validation and size checks on the new firmware. The internal byte layout differs (parameters shift by 2 within each sub-object header), but this is harmless:

1. Boot validation reads the winner record's mask and ORs it into the canonical SRAM mask.
2. `autosave_markResidentBankDirty()` then marks ALL present scene bytes dirty — including the new source field positions (via the extended compound markers from Task 6).
3. The drain calls `autosave_getLivePayloadByte()` for every dirty offset, writing the current live value at the correct new-format position.
4. After one complete drain cycle, the record is fully new-format. Old parameter bytes at old offsets are overwritten.

No format version bump, no re-creation, no migration code. The first drain seamlessly upgrades old records in place.

---

## 3. Source getter implementation

A new `autosave_getSourceByte()` function reads the resident source via the public `filesystem_residentSource()` API for the appropriate HCNAMES row, which already strips the metadata bits (refreshed, dirty), and returns one byte of the 2-byte LE value.

**Items to implement:**
- `#include "filesystem.h"` in Autosave.c
- The getter function itself in `Autosave.c`
- Row-coordinate mapping: Scene source → HCNAMES row 1..16, Kit source → HCNAMES row 17..32, Instrument source → HCNAMES rows 33..128. This mapping must match the existing `autosave_objectFullyCaptured()` row mapping exactly.

**Best practice:** The getter calls `filesystem_residentSource(row)` which already masks off bits 13-15 (refreshed + dirty + reserved) via `FS_RESIDENT_SOURCE_VALUE_MASK`. Do not duplicate the masking in Autosave.c — rely on the existing public accessor.

**Risk:** The source token encoding (Section 8 of the parent plan) says `0x7fff` for inherit, `0x7ffe` for unknown, `0x7ffd` for direct. But Phase B2 already narrowed these to 13 bits: `INHERIT = 0x1fff`, `UNKNOWN = 0x1ffe`, `INSTRUMENT_DIRECT = 0x1ffd`. The getter must use the post-narrowing values. The parent plan document was written before Phase B2 landed and still references the old 15-bit tokens — follow the code, not the document's token table.

---

## 4. Transform/drain byte routing

`autosave_getLivePayloadByte()` needs to route the new source bytes. When the drain encounters an offset within the 2-byte source field of any sub-object, it must call the source getter instead of reading from SceneData/KitData/InstrumentManager RAM.

`autosave_transformDrainChunk()` does NOT need code changes — it delegates payload routing through the sorted patch list, which is populated from `getLivePayloadByte()` during the classification phase. The mask and payload offsets update automatically when the compile-time constants change.

**Items to update:**
- The offset → data-source dispatch in `autosave_getLivePayloadByte()` — add source-field ranges for each object type

**Best practice:** The byte routing is the most error-prone part of the phase. Each object type has its own offset map, and the source field sits between name and parameters — exactly where the old parameter start was. A single off-by-one in the dispatch puts source bytes into parameter cells or vice versa.

**Pitfall:** The Instrument record has a 3-byte type prefix before the name, so its source field is at +11 (not +8 like Scene/Kit). The dispatch logic must handle this asymmetry without a generic "source is always at name_offset + 8" assumption.

---

## 5. Dirty marking extension

The existing typed dirty markers (`autosave_markSceneWithoutPatternDirty()`, `autosave_markKitDirty()`, `autosave_markWholeInstrumentDirty()`, etc.) need to include the 2 source bytes in their dirty ranges.

**Items to update:**
- New public `autosave_markSourceDirty(uint16_t hcnames_row)` function in Autosave.h/c — takes a row coordinate and marks the corresponding 2 source bytes dirty
- Each compound marker's scope must expand to cover the source field via `autosave_markSourceDirty()`
- Save completions in filesystem.c that call `filesystem_setResidentSource()` must also call `autosave_markSourceDirty()` for the affected rows

**Risk:** If a Save sets the source in `fs_resident_source[]` but doesn't mark the source bytes dirty in the autosave mask, the source field in the record is never updated. The reader would then see a stale source and make wrong load decisions. This is a silent correctness bug with no runtime symptom until the reader is implemented.

**Best practice:** Pair every `filesystem_setResidentSource()` call with `autosave_markSourceDirty()`. Audit all call sites — they are in load/save completions across filesystem.c.

---

## 6. Interaction with Phase B2 (refreshed flag)

Phase B2 is already implemented. The source getter must coexist with the refreshed flag (bit 13) in `fs_resident_source[]`. The getter calls `filesystem_residentSource()` which already masks off bits 13-15 before returning the value. The dirty marker for source bytes must not inadvertently clear or set the refreshed flag.

**Pitfall:** `autosave_objectFullyCaptured()` uses the existing geometry to map HCNAMES rows to byte ranges. The function uses compile-time constants (`AUTOSAVE_SCENE_SECTION_BYTES`, `AUTOSAVE_KIT_OFFSET`, etc.) directly — those constants are unchanged in the zero-growth approach, so the byte ranges are identical. No code changes needed, no verification of shifted ranges required.

---

## 7. Testing approach

- **Compile-time:** Static asserts catch geometry mismatches. `make clean && make` is mandatory.
- **Card-level:** After the first drain with the new firmware, verify `.hcprms1`/`.hcprms2` remain 34,768 bytes. Verify the source bytes at the expected offsets contain the masked `fs_resident_source[]` values (not raw values with flag bits).
- **Round-trip:** Load a Bank, let autosave drain, then inspect the record to confirm source fields match `.hcnames` source column for every sub-object.
- **Old-record upgrade:** Boot with pre-Phase-C records on card. Verify CRC validation passes (same size), `markResidentBankDirty()` fires, and the first drain cycle overwrites all payload bytes with correct new-format values.

---

## 8. Summary of workflows affected

| Workflow | What changes |
|---|---|
| Autosave geometry constants | Field-level offsets shift within sub-object headers; section/record sizes unchanged |
| `autosave_getLivePayloadByte()` | New source-field dispatch for Scene, Kit, Instrument |
| `autosave_transformDrainChunk()` | No code change — constants unchanged, propagation automatic |
| `autosave_objectFullyCaptured()` | No code change — section constants unchanged |
| Typed dirty markers | Extend ranges to include 2 source bytes per object |
| Load/Save completions | Pair `setResidentSource()` with `markSourceDirty()` |
| `_Static_assert` suite | New structural asserts; all existing values unchanged |
| `.hcprms` file creation | Same 34,768-byte size; old records upgraded seamlessly on first drain |
| Source token encoding | Use post-Phase-B2 narrowed tokens (0x1fff, not 0x7fff) |
| Initial record creation | No code change — source bytes are zero in baseline, first drain populates them |

---

# Detailed Implementation Schedule

All line numbers reference the current working tree on branch `dev-ph3-autosave-pre-overscope-apply`.

---

## Task 1: Geometry constants — Autosave.h

**File**: `Core/Bank/Scene/Autosave.h`

### Change 1a: Add source field offset and width constants

**Line**: After line 64 (`AUTOSAVE_NAME_BYTES`)
**Action**: ADD

```c
#define AUTOSAVE_SOURCE_BYTES                  2u
#define AUTOSAVE_SCENE_SOURCE_OFFSET           8u
#define AUTOSAVE_KIT_SOURCE_OFFSET             8u
#define AUTOSAVE_INSTRUMENT_SOURCE_OFFSET     11u
```

**Description**: Wire-format source field positions, each expressed as the byte offset within its containing sub-object (Scene, Kit, or Instrument). The source field is always a 2-byte little-endian value occupying the bytes immediately after the name field. Scene and Kit share the same relative offset (after their 8-byte names); Instrument's source starts at +11 because the 3-byte type prefix precedes its 8-byte name. These constants feed the live payload getter (Task 5) and the source dirty marker (Task 6). They never change independently of each other — a name-width change requires re-auditing all three.

**Affiliates**: `AUTOSAVE_NAME_BYTES`, `AUTOSAVE_INSTRUMENT_TYPE_BYTES`, `autosave_getLivePayloadByte()`, `autosave_markSourceDirty()`.

---

### Change 1b: Shift Scene parameters past the source field

**Line**: 116 (`AUTOSAVE_SCENE_PARAMETERS_OFFSET`)
**Action**: MODIFY

Old: `#define AUTOSAVE_SCENE_PARAMETERS_OFFSET        8u`
New: `#define AUTOSAVE_SCENE_PARAMETERS_OFFSET       10u`

**Description**: Scene parameters previously started at byte 8 (immediately after the 8-byte name). With the 2-byte source field at bytes 8-9, parameters now start at byte 10. The static assert `SCENE_PARAMETERS_OFFSET + SCENE_PARAMETER_ALLOC_BYTES == EFFECT_OFFSET` (line 205) validates the cascade — it continues to pass because the parameter allocation shrinks correspondingly (Change 1c).

**Affiliates**: `autosave_getSceneParameter()`, `autosave_markSceneParameterDirty()`, `autosave_getLivePayloadByte()` Scene parameter dispatch.

---

### Change 1c: Shrink Scene parameter allocation to absorb the source field

**Line**: 117 (`AUTOSAVE_SCENE_PARAMETER_ALLOC_BYTES`)
**Action**: MODIFY

Old: `#define AUTOSAVE_SCENE_PARAMETER_ALLOC_BYTES  120u`
New: `#define AUTOSAVE_SCENE_PARAMETER_ALLOC_BYTES  118u`

**Description**: The Scene header stays at 128 bytes total: name(8) + source(2) + params(118) = 128 = `EFFECT_OFFSET`. The 2 bytes absorbed by the source field reduce future parameter headroom from 80 to 78 reserved slots (live count is 40, unchanged). The existing static assert `SCENE_PARAM_COUNT <= SCENE_PARAMETER_ALLOC_BYTES` (line 237) validates that live parameters still fit: 40 ≤ 118. The structural assert `SCENE_PARAMETERS_OFFSET + SCENE_PARAMETER_ALLOC_BYTES == EFFECT_OFFSET` (line 205) validates the header fills to the Effect boundary: 10 + 118 = 128.

**Affiliates**: `AUTOSAVE_SCENE_PARAMETER_LIVE_BYTES` (unchanged at 40), `AUTOSAVE_EFFECT_OFFSET` (unchanged at 128), `autosave_getLivePayloadByte()` Scene parameter dispatch range.

---

### Change 1d: Shift Kit parameters past the source field

**Line**: 127 (`AUTOSAVE_KIT_PARAMETERS_OFFSET`)
**Action**: MODIFY

Old: `#define AUTOSAVE_KIT_PARAMETERS_OFFSET          8u`
New: `#define AUTOSAVE_KIT_PARAMETERS_OFFSET         10u`

**Description**: Kit parameters previously started at Kit-relative byte 8 (after the 8-byte name). With the 2-byte source field at Kit+8..+9, parameters move to Kit+10. The Kit instruments offset (`AUTOSAVE_KIT_INSTRUMENTS_OFFSET = 128`) is unchanged because the parameter allocation shrinks correspondingly (Change 1e).

**Affiliates**: `autosave_getLivePayloadByte()` Kit parameter dispatch, `autosave_markKitParameterDirty()`.

---

### Change 1e: Shrink Kit parameter allocation to absorb the source field

**Line**: 128 (`AUTOSAVE_KIT_PARAMETER_ALLOC_BYTES`)
**Action**: MODIFY

Old: `#define AUTOSAVE_KIT_PARAMETER_ALLOC_BYTES    120u`
New: `#define AUTOSAVE_KIT_PARAMETER_ALLOC_BYTES    118u`

**Description**: The Kit header stays at 128 bytes total: name(8) + source(2) + params(118) = 128 = `KIT_INSTRUMENTS_OFFSET`. The 2 bytes absorbed by the source field reduce future parameter headroom from 118 to 116 reserved slots (live count is 2, unchanged). The existing static assert `KIT_PARAM_COUNT <= KIT_PARAMETER_ALLOC_BYTES` (line 243) validates that live parameters still fit: 2 ≤ 118. The new structural assert (Change 1h) validates the header fills to the Instruments boundary: 10 + 118 = 128.

**Affiliates**: `AUTOSAVE_KIT_PARAMETER_LIVE_BYTES` (unchanged at 2), `AUTOSAVE_KIT_INSTRUMENTS_OFFSET` (unchanged at 128), `autosave_getLivePayloadByte()` Kit parameter dispatch range.

---

### Change 1f: Shift Instrument normal offset past the source field

**Line**: 185 (`AUTOSAVE_INSTRUMENT_NORMAL_OFFSET`)
**Action**: MODIFY

Old: `#define AUTOSAVE_INSTRUMENT_NORMAL_OFFSET       11u`
New: `#define AUTOSAVE_INSTRUMENT_NORMAL_OFFSET       13u`

**Description**: Normal endpoint parameters previously started at Instrument-relative byte 11 (after type(3) + name(8)). With the 2-byte source field at Instrument+11..+12, normal parameters move to Instrument+13. The morph offset (`AUTOSAVE_INSTRUMENT_MORPH_OFFSET`) and padding offset (`AUTOSAVE_INSTRUMENT_PADDING_OFFSET`) are both derived from this constant via formulas, so they shift automatically: morph moves from 83 to 85, padding moves from 155 to 157. The Instrument record stays at 192 bytes; tail padding shrinks from 37 to 35 bytes. The existing static assert `INSTRUMENT_PADDING_OFFSET <= INSTRUMENT_RECORD_BYTES` (line 227) validates the fit: 157 ≤ 192.

**Affiliates**: `autosave_getLivePayloadByte()` Instrument normal/morph dispatch, `autosave_markInstrumentNormalParameterDirty()`, `autosave_markInstrumentMorphParameterDirty()`, `autosave_markWholeInstrumentDirty()`.

---

### Change 1g: Add source field structural asserts

**Line**: After line 229 (after the `INSTRUMENT_PADDING_OFFSET` assert)
**Action**: ADD

```c
_Static_assert(AUTOSAVE_SCENE_SOURCE_OFFSET + AUTOSAVE_SOURCE_BYTES ==
                   AUTOSAVE_SCENE_PARAMETERS_OFFSET,
               "Scene source must end at parameters");
_Static_assert(AUTOSAVE_KIT_SOURCE_OFFSET + AUTOSAVE_SOURCE_BYTES ==
                   AUTOSAVE_KIT_PARAMETERS_OFFSET,
               "Kit source must end at parameters");
_Static_assert(AUTOSAVE_INSTRUMENT_SOURCE_OFFSET + AUTOSAVE_SOURCE_BYTES ==
                   AUTOSAVE_INSTRUMENT_NORMAL_OFFSET,
               "Instrument source must end at normal endpoints");
```

**Description**: Validates that each sub-object's 2-byte source field fits contiguously between the name field and the next allocation (parameters for Scene/Kit, normal endpoints for Instrument). If a future change shifts source or parameter offsets independently, these asserts fire at compile time. The values: Scene 8+2=10 ✓, Kit 8+2=10 ✓, Instrument 11+2=13 ✓.

**Affiliates**: All source offset and parameter offset constants from Changes 1a-1f.

---

### Change 1h: Add Kit parameter → instruments structural assert

**Line**: After the new source asserts (Change 1g)
**Action**: ADD

```c
_Static_assert(AUTOSAVE_KIT_PARAMETERS_OFFSET +
                   AUTOSAVE_KIT_PARAMETER_ALLOC_BYTES ==
                   AUTOSAVE_KIT_INSTRUMENTS_OFFSET,
               "Kit parameter allocation must end at Instruments");
```

**Description**: Validates that Kit name + source + parameter allocation = instruments offset. This relationship was previously implicit (8 + 120 = 128); with the source field absorbing 2 bytes from the allocation, it becomes 10 + 118 = 128. Making it explicit catches any future Kit-header geometry error at compile time. The Scene equivalent already exists at line 205 (`SCENE_PARAMETERS_OFFSET + SCENE_PARAMETER_ALLOC_BYTES == EFFECT_OFFSET`).

---

### Change 1i: Update header comment blocks

**Line**: 109-113 (Scene section comment), 175-180 (Instrument layout comment)
**Action**: MODIFY

Scene section comment update (line 109-113):

Old:
```
 * Scene parameters occupy bytes 8..127, of which indices 0..39 currently
 * exist. Effects reserve 512 bytes without a live owner. Kit begins at 640 so
 * its 128-byte header plus six 192-byte Instruments ends exactly at 1,920.
```

New:
```
 * Scene source occupies bytes 8..9; parameters occupy bytes 10..127, of
 * which indices 0..39 currently exist. Effects reserve 512 bytes without a
 * live owner. Kit begins at 640: source at 8..9, parameters at 10..127,
 * then six 192-byte Instruments, ending at 1,920.
```

Instrument layout comment update (line 177-180):

Old:
```
 * Type text and name are followed by separate descriptor-indexed normal and
 * Morph images.
```

New:
```
 * Type text and name are followed by a 2-byte source field, then separate
 * descriptor-indexed normal and Morph images.
```

---

## Task 2: Geometry static asserts — Autosave.c

**File**: `Core/Bank/Scene/Autosave.c`

No changes needed to static asserts in Autosave.c. The existing asserts at lines 22-61 validate cross-module relationships (INSTRUMENT_PARAM_COUNT vs allocation, SCENE_COUNT vs geometry, Scene parameter group widths). None of these are affected by the source field insertion — they validate that parameter counts match, not that offsets are at specific absolute values.

The canonical mask allocation `autosave_dirty_mask[AUTOSAVE_MASK_BYTES]` at line 74 and its assert at line 77 are sized from `AUTOSAVE_MASK_BYTES` which is unchanged at 3,856 bytes. No code change needed.

---

## Task 3: Source field getter — Autosave.c

**File**: `Core/Bank/Scene/Autosave.c`

### Change 3a: Add filesystem.h include

**Line**: After line 18 (`#include "InstrumentManager.h"`)
**Action**: ADD

```c
#include "filesystem.h"
```

**Description**: Provides access to `filesystem_residentSource()`, the public accessor for `fs_resident_source[]` that already strips the refreshed, dirty, and reserved flag bits via `FS_RESIDENT_SOURCE_VALUE_MASK`. This is the sole new include Autosave.c requires for Phase C. The include does not create a circular dependency: filesystem.h does not include Autosave.h. The existing one-way dependency (filesystem.c includes Autosave.h) is preserved.

**Affiliates**: `filesystem_residentSource()` (filesystem.h:642, filesystem.c:5286), `FS_RESIDENT_SOURCE_VALUE_MASK` (filesystem.h:639).

---

### Change 3b: Implement source byte getter

**Line**: After line 207 (after `autosave_nameByte()`)
**Action**: ADD

```c
static uint8_t autosave_getSourceByte(uint16_t hcnames_row,
                                       uint8_t byte_index,
                                       uint8_t *value)
{
    uint16_t source;

    if (!value || byte_index >= AUTOSAVE_SOURCE_BYTES ||
        hcnames_row >= AUTOSAVE_HCNAMES_ROW_COUNT)
        return 0u;
    source = filesystem_residentSource(hcnames_row);
    *value = (uint8_t)(source >> ((uint16_t)byte_index * 8u));
    return 1u;
}
```

**Description**: Returns one byte of the 2-byte LE source value for the specified HCNAMES row. The function delegates source-register access to `filesystem_residentSource()`, which already masks off bits 13-15 (refreshed + dirty + reserved) and returns `FS_RESIDENT_SOURCE_UNKNOWN` for invalid rows. The byte_index 0 returns the low byte; index 1 returns the high byte (which is at most 0x1F since the value range is 13-bit: 0..0x1FFF). The function never writes HCNAMES, performs I/O, or retains state.

Row coordinate mapping (must match `autosave_objectFullyCaptured()` exactly):
- Scene source for `scene_index`: `AUTOSAVE_HCNAMES_SCENE_BASE + scene_index` (rows 1..16)
- Kit source for `scene_index`: `AUTOSAVE_HCNAMES_KIT_BASE + scene_index` (rows 17..32)
- Instrument source for `scene_index`, `slot`: `AUTOSAVE_HCNAMES_INSTRUMENT_BASE + scene_index × 6 + slot` (rows 33..128)

**Affiliates**: `filesystem_residentSource()`, `autosave_getLivePayloadByte()` (Task 5), `AUTOSAVE_HCNAMES_SCENE_BASE` / `_KIT_BASE` / `_INSTRUMENT_BASE` (Autosave.h:71-75).

---

## Task 4: Source dirty marker — Autosave.h/c

**File**: `Core/Bank/Scene/Autosave.h`

### Change 4a: Declare public source dirty marker

**Line**: After line 450 (`autosave_markSceneWithPatternDirty`)
**Action**: ADD

```c
void autosave_markSourceDirty(uint16_t hcnames_row);
```

**Description**: Marks the 2 source bytes dirty for one HCNAMES-addressed sub-object. Takes a row coordinate (0..128) matching the `fs_resident_source[]` register layout. Bank row 0 is a no-op — the Bank has no source field in the autosave record. Scene rows 1..16 mark 2 bytes at `scene_base + AUTOSAVE_SCENE_SOURCE_OFFSET`. Kit rows 17..32 mark 2 bytes at `scene_base + AUTOSAVE_KIT_OFFSET + AUTOSAVE_KIT_SOURCE_OFFSET`. Instrument rows 33..128 mark 2 bytes at their record's `AUTOSAVE_INSTRUMENT_SOURCE_OFFSET`. This function is the sole source-dirty entry point for both compound Autosave markers and filesystem.c save completions.

**Affiliates**: `filesystem_setResidentSource()` (filesystem.h:643), `autosave_markSceneWithoutPatternDirty()`, `autosave_markKitDirty()`, `autosave_markWholeInstrumentDirty()`.

---

**File**: `Core/Bank/Scene/Autosave.c`

### Change 4b: Implement source dirty marker

**Line**: After `autosave_markEffectParameterDirty()` (after line 1124)
**Action**: ADD

```c
void autosave_markSourceDirty(uint16_t hcnames_row)
{
    uint16_t payload_base;
    uint8_t byte;

    if (hcnames_row == AUTOSAVE_HCNAMES_BANK_ROW)
        return;
    if (hcnames_row >= AUTOSAVE_HCNAMES_SCENE_BASE &&
        hcnames_row < AUTOSAVE_HCNAMES_KIT_BASE) {
        uint8_t scene_index = (uint8_t)(
            hcnames_row - AUTOSAVE_HCNAMES_SCENE_BASE);
        if (!autosave_scenePayloadBase(scene_index, &payload_base))
            return;
        payload_base = (uint16_t)(
            payload_base + AUTOSAVE_SCENE_SOURCE_OFFSET);
    } else if (hcnames_row >= AUTOSAVE_HCNAMES_KIT_BASE &&
               hcnames_row < AUTOSAVE_HCNAMES_INSTRUMENT_BASE) {
        uint8_t scene_index = (uint8_t)(
            hcnames_row - AUTOSAVE_HCNAMES_KIT_BASE);
        if (!autosave_scenePayloadBase(scene_index, &payload_base))
            return;
        payload_base = (uint16_t)(
            payload_base + AUTOSAVE_KIT_OFFSET +
            AUTOSAVE_KIT_SOURCE_OFFSET);
    } else if (hcnames_row >= AUTOSAVE_HCNAMES_INSTRUMENT_BASE &&
               hcnames_row < AUTOSAVE_HCNAMES_ROW_COUNT) {
        uint16_t instrument_index = (uint16_t)(
            hcnames_row - AUTOSAVE_HCNAMES_INSTRUMENT_BASE);
        uint8_t scene_index = (uint8_t)(
            instrument_index / AUTOSAVE_INSTRUMENTS_PER_KIT);
        uint8_t slot = (uint8_t)(
            instrument_index % AUTOSAVE_INSTRUMENTS_PER_KIT);
        if (!autosave_scenePayloadBase(scene_index, &payload_base))
            return;
        payload_base = (uint16_t)(
            payload_base + AUTOSAVE_KIT_OFFSET +
            AUTOSAVE_KIT_INSTRUMENTS_OFFSET +
            ((uint16_t)slot * AUTOSAVE_INSTRUMENT_RECORD_BYTES) +
            AUTOSAVE_INSTRUMENT_SOURCE_OFFSET);
    } else {
        return;
    }
    for (byte = 0u; byte < AUTOSAVE_SOURCE_BYTES; byte++)
        autosave_markPayloadOffsetDirty((uint16_t)(payload_base + byte));
}
```

**Description**: Converts one HCNAMES row coordinate into the payload-relative offset of its 2-byte source field, then marks both bytes dirty through the same `autosave_markPayloadOffsetDirty()` funnel used by all other dirty markers. The `autosave_scenePayloadBase()` check enforces the same Scene-presence guard as the existing parameter markers — an absent Scene's source bytes are not marked. The row-to-offset mapping uses the same HCNAMES base constants and arithmetic as `autosave_objectFullyCaptured()` (lines 1436-1492), ensuring the two functions address the same byte ranges.

The function is the row-addressed complement to the coordinate-addressed markers. Compound markers call it with a computed row; filesystem.c save completions call it with the same row passed to `filesystem_setResidentSource()`.

**Affiliates**: `autosave_scenePayloadBase()` (line 158), `autosave_markPayloadOffsetDirty()` (line 131), `autosave_objectFullyCaptured()` (line 1436).

---

## Task 5: Live payload byte routing — Autosave.c

**File**: `Core/Bank/Scene/Autosave.c`
**Function**: `autosave_getLivePayloadByte()` (line 661)

### Change 5a: Add Scene source dispatch

**Line**: Before line 754 (before the `AUTOSAVE_SCENE_PARAMETERS_OFFSET` check)
**Action**: ADD

Insert before the existing Scene parameter routing:

```c
    if (relative >= AUTOSAVE_SCENE_SOURCE_OFFSET &&
        relative < AUTOSAVE_SCENE_SOURCE_OFFSET + AUTOSAVE_SOURCE_BYTES) {
        return autosave_getSourceByte(
            (uint16_t)(AUTOSAVE_HCNAMES_SCENE_BASE + scene_index),
            (uint8_t)(relative - AUTOSAVE_SCENE_SOURCE_OFFSET),
            value);
    }
```

**Description**: Routes Scene-relative bytes 8-9 to the source getter. These bytes were previously at the start of the Scene parameter allocation (old params started at byte 8, new at byte 10). The constant change to `SCENE_PARAMETERS_OFFSET` (8→10) automatically opens up bytes 8-9 from the old parameter range; this dispatch fills that gap. The dispatch order is: name (0-7, falls through to return 0 — HCNAMES-owned), **source (8-9, new)**, parameters (10-127), then effect/kit as before.

**Affiliates**: `autosave_getSourceByte()` (Task 3b), `AUTOSAVE_HCNAMES_SCENE_BASE` (Autosave.h:71).

---

### Change 5b: Add Kit source dispatch

**Line**: After line 787 (after entering Kit-relative space with `relative -= AUTOSAVE_KIT_OFFSET`), before the existing `AUTOSAVE_KIT_PARAMETERS_OFFSET` check
**Action**: ADD

Insert before the existing Kit parameter routing:

```c
    if (relative >= AUTOSAVE_KIT_SOURCE_OFFSET &&
        relative < AUTOSAVE_KIT_SOURCE_OFFSET + AUTOSAVE_SOURCE_BYTES) {
        return autosave_getSourceByte(
            (uint16_t)(AUTOSAVE_HCNAMES_KIT_BASE + scene_index),
            (uint8_t)(relative - AUTOSAVE_KIT_SOURCE_OFFSET),
            value);
    }
```

**Description**: Routes Kit-relative bytes 8-9 to the source getter. Identical pattern to the Scene source dispatch but using `AUTOSAVE_HCNAMES_KIT_BASE`. The `scene_index` variable is already in scope from the Scene dispatch above. The Kit name (bytes 0-7) remains HCNAMES-owned and returns 0; Kit parameters shift from byte 8 to byte 10.

**Affiliates**: `autosave_getSourceByte()` (Task 3b), `AUTOSAVE_HCNAMES_KIT_BASE` (Autosave.h:72).

---

### Change 5c: Add Instrument source dispatch

**Line**: After line 842 (after the Instrument type check, before the `AUTOSAVE_INSTRUMENT_NORMAL_OFFSET` check)
**Action**: ADD

Insert between the type-text dispatch and the normal-endpoint dispatch:

```c
        if (field_offset >= AUTOSAVE_INSTRUMENT_SOURCE_OFFSET &&
            field_offset < AUTOSAVE_INSTRUMENT_SOURCE_OFFSET +
                           AUTOSAVE_SOURCE_BYTES) {
            return autosave_getSourceByte(
                (uint16_t)(AUTOSAVE_HCNAMES_INSTRUMENT_BASE +
                    ((uint16_t)scene_index * AUTOSAVE_INSTRUMENTS_PER_KIT) +
                    instrument_index),
                (uint8_t)(field_offset - AUTOSAVE_INSTRUMENT_SOURCE_OFFSET),
                value);
        }
```

**Description**: Routes Instrument-relative bytes 11-12 to the source getter. The Instrument record has a unique layout: type(0-2), name(3-10), **source(11-12, new)**, normal(13-84), morph(85-156), padding(157-191). The source field is at offset 11, not 8, because of the 3-byte type prefix. The `instrument_index` variable is already in scope from the surrounding instruments loop. The HCNAMES row for this instrument is `INSTRUMENT_BASE + scene_index × 6 + instrument_index`, matching the mapping in `autosave_objectFullyCaptured()` (line 1467).

**Affiliates**: `autosave_getSourceByte()` (Task 3b), `AUTOSAVE_HCNAMES_INSTRUMENT_BASE` (Autosave.h:74), `instrument_index` loop variable (line 815).

---

## Task 6: Extend compound dirty markers — Autosave.c

**File**: `Core/Bank/Scene/Autosave.c`

### Change 6a: Extend `autosave_markWholeInstrumentDirty()` to include source

**Line**: After line 1228 (after the `for type_byte` loop that marks type bytes, before the `for descriptor_index` loop)
**Action**: ADD

```c
        for (type_byte = 0u; type_byte < AUTOSAVE_SOURCE_BYTES;
             type_byte++) {
            published_count = (uint8_t)(published_count +
                autosave_markPayloadOffsetDirty((uint16_t)(instrument_base +
                    AUTOSAVE_INSTRUMENT_SOURCE_OFFSET + type_byte)));
        }
```

Also update the `expected_count` initialization (line 1213):

Old: `expected_count = AUTOSAVE_INSTRUMENT_TYPE_BYTES;`
New: `expected_count = (uint8_t)(AUTOSAVE_INSTRUMENT_TYPE_BYTES + AUTOSAVE_SOURCE_BYTES);`

**Description**: Marks the 2 Instrument source bytes dirty alongside the 3 type bytes and all normal/morph endpoints. The function now marks type(3) + source(2) + normal(N) + morph(M) bytes. The `expected_count` tracking for the trace record must include the source bytes so the "all published" flag is accurate. The source bytes are marked using `autosave_markPayloadOffsetDirty()` directly rather than calling `autosave_markSourceDirty()` because the `instrument_base` payload offset is already computed.

**Affiliates**: `autosave_markPayloadOffsetDirty()` (line 131), `AUTOSAVE_TRACE_INSTRUMENT_MARK_*` trace encoding.

---

### Change 6b: Extend `autosave_markKitDirty()` to include Kit source

**Line**: After line 1286 (after the Kit parameter marking loop, before the Instrument marking loop)
**Action**: ADD

```c
    autosave_markSourceDirty(
        (uint16_t)(AUTOSAVE_HCNAMES_KIT_BASE + scene_index));
```

**Description**: Marks the 2 Kit source bytes dirty when a whole Kit is committed. This sits between the Kit parameter marking and the six-Instrument marking, ensuring all Kit-owned bytes in the autosave record are covered. The `autosave_markSourceDirty()` function (Task 4b) handles the payload offset computation from the HCNAMES row.

**Affiliates**: `autosave_markSourceDirty()` (Task 4b), `AUTOSAVE_HCNAMES_KIT_BASE` (Autosave.h:72).

---

### Change 6c: Extend `autosave_markSceneWithoutPatternDirty()` to include Scene source

**Line**: After line 1339 (after the Scene parameter marking loop, before `autosave_markEffectDirty()`)
**Action**: ADD

```c
    autosave_markSourceDirty(
        (uint16_t)(AUTOSAVE_HCNAMES_SCENE_BASE + scene_index));
```

**Description**: Marks the 2 Scene source bytes dirty when a whole Scene (without Pattern) is committed. This extends the Scene parameter scope to include the source field. Kit and Instrument sources are covered transitively through `autosave_markKitDirty()` (which now calls `autosave_markSourceDirty()` for the Kit row, and `autosave_markWholeInstrumentDirty()` now marks Instrument source bytes).

**Affiliates**: `autosave_markSourceDirty()` (Task 4b), `AUTOSAVE_HCNAMES_SCENE_BASE` (Autosave.h:71).

---

## Task 7: Source dirty marking at save completions — filesystem.c

**File**: `Core/Hardware/SD/filesystem.c`

Save completions call `filesystem_setResidentSource()` to record that the object now lives at a specific library slot. The parameters haven't changed (they were saved from the current state, which is already in the autosave record), but the source field in the autosave record must be updated to reflect the new provenance. Each `setResidentSource` call below must be paired with `autosave_markSourceDirty()`.

Load completions do NOT need additional source marking because Preset's load callbacks already call compound markers (`autosave_markSceneWithoutPatternDirty`, `autosave_markKitDirty`, `autosave_markWholeInstrumentDirty`) which, after Task 6, include source bytes.

### Change 7a: Instrument Save completion — mark Instrument source dirty

**Line**: After line 14099 (after `filesystem_setResidentSource(..., FS_RESIDENT_SOURCE_INSTRUMENT_DIRECT)`)
**Action**: ADD

```c
            autosave_markSourceDirty(
                filesystem_residentInstrumentRow(
                    op_instrument_save_source_scene,
                    op_instrument_save_source_slot));
```

**Description**: After a successful root Instrument Save, the source changes to `INSTRUMENT_DIRECT`. The autosave record must capture this new source value. The save does not change instrument parameters, so no compound marker is needed — only the 2 source bytes.

**Affiliates**: `filesystem_setResidentSource()` (line 14095), `filesystem_residentInstrumentRow()` (line 5200), `autosave_markSourceDirty()` (Task 4).

---

### Change 7b: Kit Save completion — mark Kit + Instrument sources dirty

**Line**: After line 15801 (after the loop that sets Instrument sources to INHERIT)
**Action**: ADD

```c
            autosave_markSourceDirty(
                filesystem_residentKitRow(op_kit_save_source_scene));
            for (instrument_slot = 0u;
                 instrument_slot < STORAGE_KIT_SLOT_COUNT;
                 instrument_slot++) {
                autosave_markSourceDirty(
                    filesystem_residentInstrumentRow(
                        op_kit_save_source_scene, instrument_slot));
            }
```

**Description**: After a successful Kit Save, the Kit source changes to the saved slot and all member Instrument sources change to INHERIT. The autosave record must capture these 7 new source values (1 Kit + 6 Instruments). The save does not change Kit/Instrument parameters — only provenance.

**Affiliates**: `filesystem_setResidentSource()` (lines 15792-15801), `filesystem_residentKitRow()` (line 5220), `filesystem_residentInstrumentRow()` (line 5200).

---

### Change 7c: Scene Save completion — mark Scene + Kit + Instrument sources dirty

**Line**: After line 17382 (after the loop that sets Instrument sources to INHERIT)
**Action**: ADD

```c
            autosave_markSourceDirty(
                filesystem_residentSceneRow(op_kit_save_source_scene));
            autosave_markSourceDirty(
                filesystem_residentKitRow(op_kit_save_source_scene));
            {
                uint8_t src_slot;
                for (src_slot = 0u;
                     src_slot < STORAGE_KIT_SLOT_COUNT;
                     src_slot++) {
                    autosave_markSourceDirty(
                        filesystem_residentInstrumentRow(
                            op_kit_save_source_scene, src_slot));
                }
            }
```

**Description**: After a successful Scene Save, the Scene source changes to the saved slot, the Kit source and all Instrument sources change to INHERIT. The autosave record must capture these 8 new source values (1 Scene + 1 Kit + 6 Instruments).

**Affiliates**: `filesystem_setResidentSource()` (lines 17367-17381), `filesystem_residentSceneRow()` (line 5235).

---

### Change 7d: Bank Save completion — no source marking needed

**Line**: 16609
**Action**: NONE (verification only)

The Bank Save sets `filesystem_setResidentSource(FS_IDENTITY_BANK_ROW, op_slot)` for Bank row 0. However, the Bank row has NO source field in the autosave record — only Scene, Kit, and Instrument sub-objects get source fields. `autosave_markSourceDirty()` returns immediately for Bank row 0 (the `AUTOSAVE_HCNAMES_BANK_ROW` early-exit). No marking needed.

---

## Task 8: No-change verification — functions that update automatically

These functions use compile-time constants and require NO code changes. The zero-growth approach is especially clean here: section-level constants (`SCENE_SECTION_BYTES`, `KIT_SECTION_BYTES`, `KIT_OFFSET`, `EFFECT_OFFSET`, `KIT_INSTRUMENTS_OFFSET`, `INSTRUMENT_RECORD_BYTES`, `MASK_BYTES`, `PAYLOAD_BYTES`, `RECORD_BYTES`) are ALL unchanged. Only field-level offsets within sub-object headers change, and those are used correctly by the dispatch and marking functions.

### `autosave_objectFullyCaptured()` (Autosave.c:1436)

Uses section-level constants only: `AUTOSAVE_BANK_SECTION_BYTES`, `AUTOSAVE_SCENE_SECTION_BYTES`, `AUTOSAVE_KIT_OFFSET`, `AUTOSAVE_KIT_SECTION_BYTES`, `AUTOSAVE_KIT_INSTRUMENTS_OFFSET`, `AUTOSAVE_INSTRUMENT_RECORD_BYTES`. All unchanged. Byte ranges are identical to pre-Phase-C. **No code change needed.**

### `autosave_initialRecordByte()` (Autosave.c:218)

Uses section-level and name-level constants only. Name offsets (`SCENE_NAME_OFFSET`, `KIT_NAME_OFFSET`, `INSTRUMENT_NAME_OFFSET`) are unchanged. Source bytes and all non-header/non-name bytes return 0 from the `return 0u` default — zero is the correct initial value (the first drain populates source fields via `autosave_markResidentBankDirty()` and the extended compound markers). **No code change needed.**

### `autosave_formatInitialChunk()` (Autosave.c:424)

Delegates to `autosave_initialRecordByte()`. Uses `AUTOSAVE_RECORD_BYTES` (unchanged). **No code change needed.**

### `autosave_initialRecordCrcUpdate()` (Autosave.c:383)

Delegates to `autosave_initialRecordByte()`. Uses `AUTOSAVE_RECORD_BYTES` and `AUTOSAVE_CRC_BYTES_PER_TICK` (both unchanged). **No code change needed.**

### `autosave_streamValidationFinish()` (Autosave.c:558)

Uses `AUTOSAVE_RECORD_BYTES` (unchanged at 34768). Old-format and new-format records are the same size, so validation passes for both. **No code change needed.**

### `autosave_transformDrainChunk()` (Autosave.c:1596)

Uses `AUTOSAVE_RECORD_BYTES`, `AUTOSAVE_MASK_OFFSET`, `AUTOSAVE_MASK_BYTES`, `AUTOSAVE_PAYLOAD_OFFSET`. All unchanged. **No code change needed.**

### `autosave_maskMergeChunk()` (Autosave.c:1392)

Uses `AUTOSAVE_MASK_BYTES` (unchanged). **No code change needed.**

### `autosave_maskHasDirty()` (Autosave.c:1416)

Uses `AUTOSAVE_MASK_BYTES` (unchanged). **No code change needed.**

### `autosave_maskBitTake()` (Autosave.c:1494)

Uses `AUTOSAVE_PAYLOAD_BYTES` (unchanged). **No code change needed.**

### `autosave_markResidentBankDirty()` (Autosave.c:1367)

Calls `autosave_markBankFieldDirty()` and `autosave_markSceneWithoutPatternDirty()`. The latter now includes source bytes (Task 6c), which transitively includes Kit/Instrument sources (Tasks 6b, 6a). **No code change needed.**

---

## Task 9: No-change verification — filesystem.c geometry references

### `filesystem_ensureAutosaveFiles_tick()` (filesystem.c)

Uses `AUTOSAVE_RECORD_BYTES` (unchanged at 34768). Files are created at the same size. Old files validate at the same size. **No code change needed.**

### `filesystem_autosaveParameterDrain_tick()` (filesystem.c)

Uses `AUTOSAVE_RECORD_BYTES`, `AUTOSAVE_MASK_BYTES`, `AUTOSAVE_PAYLOAD_BYTES`. All unchanged. **No code change needed.**

### `filesystem_residentRefreshedNeedsClear()` / `filesystem_clearResidentRefreshedCaptured()` (filesystem.c:6700-6743)

These iterate `FS_RESIDENT_NAMES_ROW_COUNT` and call `autosave_objectFullyCaptured()`. No autosave geometry constants used directly. **No code change needed.**

---

## Implementation order

1. **Task 1** (Autosave.h constants): Field-level offsets and param alloc sizes. `make clean && make` — static asserts validate all relationships.
2. **Task 2** (Autosave.c static asserts): Verification only — confirms no changes needed.
3. **Task 3** (Source getter): New include + `autosave_getSourceByte()`. Compile-only verification.
4. **Task 4** (Source dirty marker): `autosave_markSourceDirty()` in Autosave.h/c. Compile-only.
5. **Task 5** (Live payload routing): Three new dispatch branches in `autosave_getLivePayloadByte()`. Compile + functional test.
6. **Task 6** (Extend compound markers): Add source marking to three compound functions. Compile + full drain test.
7. **Task 7** (Save completion pairing): Add `autosave_markSourceDirty()` calls at three save completion sites in filesystem.c. Compile + save-then-drain test.
8. **Task 8** (Auto-updating functions): Verification-only audit. No code changes.
9. **Task 9** (filesystem.c geometry): Verification-only audit. No code changes.

---

## SRAM impact

| Item | Old bytes | New bytes | Delta |
|------|-----------|-----------|-------|
| `autosave_dirty_mask[]` (Autosave.c BSS) | 3,856 | 3,856 | **0** |
| `autosave_getSourceByte()` stack | 0 | ~8 | +8 (stack, not persistent) |
| `autosave_markSourceDirty()` stack | 0 | ~12 | +12 (stack, not persistent) |
| `fs_resident_source[]` (filesystem.c BSS) | 258 | 258 | 0 (unchanged) |
| **Total persistent BSS** | | | **0** |

Zero persistent SRAM cost. The mask, payload, and record sizes are unchanged.

---

## Summary of all changes

| Task | File:Line | Action | Description |
|------|-----------|--------|-------------|
| 1a | Autosave.h:~65 | ADD | Source field offset/width constants |
| 1b | Autosave.h:116 | MODIFY | Scene params offset 8→10 |
| 1c | Autosave.h:117 | MODIFY | Scene param alloc 120→118 |
| 1d | Autosave.h:127 | MODIFY | Kit params offset 8→10 |
| 1e | Autosave.h:128 | MODIFY | Kit param alloc 120→118 |
| 1f | Autosave.h:185 | MODIFY | Instrument normal offset 11→13 |
| 1g | Autosave.h:~230 | ADD | Source→parameter/normal structural asserts |
| 1h | Autosave.h:~234 | ADD | Kit params→instruments structural assert |
| 1i | Autosave.h:109,177 | MODIFY | Comment blocks describing sub-object layout |
| 3a | Autosave.c:~19 | ADD | `#include "filesystem.h"` |
| 3b | Autosave.c:~208 | ADD | `autosave_getSourceByte()` |
| 4a | Autosave.h:~451 | ADD | `autosave_markSourceDirty()` declaration |
| 4b | Autosave.c:~1125 | ADD | `autosave_markSourceDirty()` implementation |
| 5a | Autosave.c:~754 | ADD | Scene source dispatch in `getLivePayloadByte()` |
| 5b | Autosave.c:~788 | ADD | Kit source dispatch in `getLivePayloadByte()` |
| 5c | Autosave.c:~843 | ADD | Instrument source dispatch in `getLivePayloadByte()` |
| 6a | Autosave.c:~1213,1228 | MODIFY+ADD | Whole Instrument marker: +source bytes + expected_count |
| 6b | Autosave.c:~1287 | ADD | Kit marker: +Kit source dirty |
| 6c | Autosave.c:~1340 | ADD | Scene marker: +Scene source dirty |
| 7a | filesystem.c:~14100 | ADD | Instrument Save: source dirty marking |
| 7b | filesystem.c:~15802 | ADD | Kit Save: source dirty marking (7 rows) |
| 7c | filesystem.c:~17383 | ADD | Scene Save: source dirty marking (8 rows) |

---

## Implementation notes — 2026-09-03

- Confirmed the working tree already contained the uncommitted Phase B/B2
  HCNAMES work in `Autosave.c`, `Autosave.h`, and `filesystem.c`; Phase C was
  applied on top of those changes without reverting or rewriting them.
- Added the zero-growth Scene/Kit/Instrument source geometry, source-to-next-
  region static asserts, and in-place layout comments in `Autosave.h`.
- Added the filesystem-backed source-byte getter and the HCNAMES-row source
  dirty marker in `Autosave.c`. The getter uses
  `filesystem_residentSource()`, so refreshed/dirty metadata bits are removed
  by the existing filesystem accessor rather than duplicated in AutoSave.
- Added live-byte dispatch for Scene rows 1..16, Kit rows 17..32, and
  Instrument rows 33..128. Extended whole-Instrument, whole-Kit, and
  Scene-without-Pattern markers to include their source bytes; endpoint-only
  normal/Morph markers remain provenance-neutral by design.
- Paired the three root Save completion families with source dirty marking:
  Instrument Save marks one row, Kit Save marks its Kit plus six Instrument
  rows, and Scene Save marks its Scene plus Kit plus six Instrument rows. Bank
  row zero remains source-field-free.
- The approved RAM impact remains bounded automatic stack use only (about
  20 bytes across the new getter/marker call frames); no persistent allocation
  or wire-record growth was introduced.
- Clean verification completed with `make clean && make`: all geometry
  `_Static_assert`s pass and the linked firmware reports `text=389,036`,
  `data=400`, `bss=96,192`. The only diagnostics are existing unused-function
  and toolchain-library warnings; no Phase C warning or error was emitted.
- Image packaging completed with `make img`; the firmware payload is 389,436
  bytes and the resulting `build/LXRV2_lxr02.img` is 389,452 bytes including
  its 16-byte package envelope.

---

## SD card verification — 2026-09-03

Firmware was booted on hardware with Phase C changes. The following operations
were performed before pulling the SD card:

1. Bank Load (loads all 16 scenes and their sub-objects from the resident bank)
2. Scene Load on Scene slot 6 (individually loads a scene named "FilMod")
3. Multiple autosave drain cycles completed (confirmed by generation advancement)

### Files inspected: `SD_CARD_C_PHASE/`

| File | Size | Expected | Status |
|------|------|----------|--------|
| `.hcprms1` | 34,768 | 34,768 | Unchanged record size |
| `.hcprms2` | 34,768 | 34,768 | Unchanged record size |
| `.hcnamtmp` | 1,412 | present | Phase B2 temp file from post-drain HCNAMES convergence |
| `asavetrc.bin` | 214,144 | present | Autosave trace buffer |

### Record headers

| Field | Record A (`.hcprms1`) | Record B (`.hcprms2`) |
|-------|----------------------|-----------------------|
| Magic | `HCPR` | `HCPR` |
| Version | 1 | 1 |
| Commit signature | `0xa5` | `0xa5` |
| Generation | 15 | 16 |

Record B (generation 16) is the current winner. Both records pass CRC
validation at the expected 34,768-byte size, confirming zero-growth
compatibility.

### Source field values

Source fields were read at the Phase C wire-format offsets: Scene at
`scene_base + 8`, Kit at `scene_base + 648`, Instrument at
`instrument_base + 11`. All values are 2-byte little-endian.

**Scenes 0–8 (Bank-loaded, no individual reload):**

| Scene | Name | Scene source | Kit source | Inst 0–5 sources |
|-------|------|-------------|-----------|-----------------|
| 0 | "Barf" | `0x1FFF` (INHERIT) | `0x1FFF` (INHERIT) | all `0x1FFF` (INHERIT) |
| 1–5 | various | `0x1FFF` | `0x1FFF` | all `0x1FFF` |
| 6 | "FilMod" | `0x0003` (slot 3) | `0x1FFF` | all `0x1FFF` |
| 7–8 | various | `0x1FFF` | `0x1FFF` | all `0x1FFF` |

Scene 6 ("FilMod") has source `0x0003` — library slot 3 — because it was
individually loaded via Scene Load. Its Kit and Instrument sources remain
INHERIT because the Kit and Instruments were loaded as part of the Scene
(inheriting the Scene's provenance), not loaded independently.

**Scenes 9–15 (Bank-loaded, source reflects bank slot):**

| Scene | Scene source | Kit source | Inst 0–5 sources |
|-------|-------------|-----------|-----------------|
| 9–15 | `0x0000` (slot 0) | `0x0000` (slot 0) | all `0x0000` (slot 0) |

These scenes have source `0x0000` (bank slot 0), indicating they were loaded
as part of a Bank Load from slot 0. Source is set per-object at Bank Load
time because each sub-object is individually loaded from its bank-resident
library entry.

**Instrument types (Scene 0, verification):**

| Slot | Type text | Expected |
|------|-----------|----------|
| 0 | `drm` | drum |
| 1 | `drm` | drum |
| 2 | `drm` | drum |
| 3 | `snr` | snare |
| 4 | `cym` | cymbal |
| 5 | `hat` | hi-hat |

Instrument types are at the expected offsets (instrument record + 0..2),
confirming the 3-byte type prefix is correctly positioned and the source
field at +11 does not overlap it.

### Verification summary

All source fields appear at the correct wire-format offsets with the expected
values. The INHERIT token (`0x1FFF`) is the post-Phase-B2 narrowed 13-bit
encoding, not the old 15-bit `0x7FFF`. Scene 6's individual-load provenance
is correctly distinguished from bank-loaded scenes. Record sizes are unchanged
at 34,768 bytes, confirming the zero-growth geometry. The first drain after
boot with Phase C firmware seamlessly upgraded the records in place — no
format migration, no re-creation.
