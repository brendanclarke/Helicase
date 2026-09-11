# Session 063 — Phases A & B Implementation Plan

**Scope**: v4 binary Pattern file format (read/write), per-track pattern
params in `pat_scene_region_t`, v3 removal, HCNAMES 129→145 expansion,
`/Pattern/.hcindex`, Scene/Bank/Boot/Library load/save with v4.

**Authoritative source**: `S063_DYNAMIC_PATTERN_FILE_AUTOSAVE.md` (Resolved
Decisions R1–R10, Parts A–D, Implementation Order steps 1–13).

**Accessor design**: Option B — expose `pat_scene_region_t` in
PatternData.h, return one struct pointer per Scene. Two functions total
(`pat_sceneRegion` / `pat_sceneRegionMut`). Same approach used for every
other persistent struct in the firmware (`scene_t`, `kit_t`, etc.).

---

## v4 File Format (final)

```
Offset  Size    Content
──────  ────    ───────
0       4       Magic: "PAT4" (0x50 0x41 0x54 0x34)
4       2       Format version: 1 (uint16_t LE)
6       2       PAT_STACK_SIZE (uint16_t LE)
8       2       Header total size (uint16_t LE)
10      4       Generation counter (uint32_t LE)
14      4       CRC32C (uint32_t LE) — zeroed during computation
18      14      Reserved (zero)
32      1       pattern_change_bar
33      1       pattern_next
34      14      Reserved pattern params (zero)
48      112     Per-track params: 7 × {length, scale, shuffle, 13B reserved}
160     1,792   address[7][128] (uint16_t LE)
1,952   512     bitmap[512]
2,464   8,192   pool[PAT_STACK_SIZE * 32]
────────────────────────────────────
Total: 10,656 bytes (~21 SD sectors)
```

---

## Phase A — Foundations

### A.1 Per-Track Pattern Params in `pat_scene_region_t`

#### Change A.1.1 — Extend `pat_scene_region_t` struct

**File**: `Core/Bank/Scene/Pattern/PatternData.c`
**Line**: 33–37 (struct definition)
**Action**: Modify

Add per-track and pattern-level parameter storage after the existing
bitmap field. The SRAM layout stores only the bytes actually used (no
file-format padding). 3 bytes per track × 7 tracks + 2 pattern-level
= 23 bytes per Scene.

```
typedef struct {
    uint16_t address[NUM_TRACKS][NUM_STEPS];  /* existing */
    uint8_t  pool[PAT_STACK_SIZE * 32u];      /* existing */
    uint8_t  bitmap[512u];                    /* existing */
    uint8_t  track_length[NUM_TRACKS];        /* NEW: 7 bytes */
    uint8_t  track_scale[NUM_TRACKS];         /* NEW: 7 bytes */
    uint8_t  track_shuffle[NUM_TRACKS];       /* NEW: 7 bytes */
    uint8_t  pattern_change_bar;              /* NEW: 1 byte */
    uint8_t  pattern_next;                    /* NEW: 1 byte */
} pat_scene_region_t;
```

**Why**: R1 decided track length/scale/shuffle are pattern params stored
in `pat_scene_region_t`, not scene-level. These fields are the source of
truth for both menu display and file serialization. The v4 writer reads
them; the v4 reader writes them.

**Inputs**: `NUM_TRACKS` (7), menu-dispatched values via setters.
**Outputs**: 23 additional bytes per Scene in `pat_regions[]`.
**Affiliates**: `pat_initScene()`, `pat_setTrackLength/Scale/Shuffle`,
`pat_setPatternChangeBar/Next`, `pat_applyTrackSettingsToMenu`,
`pat_applyPatternSettingsToMenu`, v4 reader/writer.
**SRAM impact**: +23 × 16 = 368 bytes in SRAM1 `.bss`.

#### Change A.1.2 — Update `pat_scene_region_t` static assert

**File**: `Core/Bank/Scene/Pattern/PatternData.c`
**Line**: 41–43
**Action**: Modify

Update the size assertion to include the 23 new bytes per Scene:
`(PAT_STEPS_PER_SCENE * 2u) + (PAT_STACK_SIZE * 32u) + 512u + 23u`

**Why**: The static assert is the compile-time proof that the SRAM budget
matches expectations. Every struct change must update it.

#### Change A.1.3 — Initialize new fields in `pat_initScene()`

**File**: `Core/Bank/Scene/Pattern/PatternData.c`
**Line**: 446–472 (inside `pat_initScene`)
**Action**: Modify

After the existing bitmap initialization, zero or default the new fields:
- `track_length[t]` = `NUM_STEPS` (128, full-length default)
- `track_scale[t]` = `TRACK_SCALE_OFF` (10, no scale)
- `track_shuffle[t]` = 0
- `pattern_change_bar` = 0
- `pattern_next` = 0

**Why**: `pat_initScene()` is called at boot, on Scene Load failure, and
by `pat_clearPattern()`. New fields must have known defaults so the v4
writer never serializes uninitialized SRAM.

**Inputs**: Validated Scene index. **Outputs**: All new fields at defaults.
**Affiliates**: Boot path, Scene Load error recovery, `pat_clearPattern()`.

#### Change A.1.4 — Make `pat_setTrackLength` / `Scale` / `Shuffle` real

**File**: `Core/Bank/Scene/Pattern/PatternData.c`
**Line**: 704–706 (no-op stubs)
**Action**: Modify

Replace the `(void)` no-ops with writes to the new struct fields:
```c
void pat_setTrackLength(uint8_t s, uint8_t t, uint8_t v) {
    if (!scene_indexValid(s) || !pat_trackValid(t)) return;
    pat_regions[s].track_length[t] = v;
    bank_invalidateSdCleanScene(s);
}
```
Same pattern for Scale and Shuffle. Each setter validates coordinates,
writes the field, and calls `bank_invalidateSdCleanScene()` to mark the
Scene dirty for AutoSave.

**Why**: These are the SRAM write path for per-track params. Menu encoder
dispatch calls them; the v4 file format reads the result.

**Inputs**: Scene index, track index, parameter value from Menu.
**Outputs**: One byte written to `pat_regions[s]`. Scene dirty flag set.
**Affiliates**: `menu_parseParameter()`, `pat_applyTrackSettingsToMenu()`,
v4 writer, `bank_invalidateSdCleanScene()`.

#### Change A.1.5 — Make `pat_setPatternChangeBar` / `pat_setPatternNext` real

**File**: `Core/Bank/Scene/Pattern/PatternData.c`
**Line**: 711–712 (no-op stubs)
**Action**: Modify

Same pattern as A.1.4: validate Scene, write field, invalidate.

**Inputs**: Scene index, value from Menu. **Outputs**: One byte written.
**Affiliates**: Same as A.1.4 plus Sequencer pattern-chain logic.

#### Change A.1.6 — Make `pat_applyTrackSettingsToMenu` real

**File**: `Core/Bank/Scene/Pattern/PatternData.c`
**Line**: 703 (no-op stub)
**Action**: Modify

Read the track's stored values from `pat_regions[scene]` and write them
into `parameter_values[]` for display:
```c
void pat_applyTrackSettingsToMenu(uint8_t s, uint8_t t) {
    if (!scene_indexValid(s) || !pat_trackValid(t)) return;
    parameter_values[PAR_TRACK_LENGTH] = pat_regions[s].track_length[t];
    parameter_values[PAR_TRACK_SCALE] = pat_regions[s].track_scale[t];
    parameter_values[PAR_TRACK_SHUFFLE] = pat_regions[s].track_shuffle[t];
}
```

**Why**: RQ4 — Scene switch must project the incoming Scene's stored values
into the Menu display buffer. Currently a no-op; this makes it the live
projection path. The `parameter_values[]` array remains the Menu's display
buffer; this function copies from the authoritative region storage.

**Inputs**: Scene, track. **Outputs**: `parameter_values[]` updated.
**Affiliates**: Scene-switch call sites, `pat_setTrackLength/Scale/Shuffle`.

#### Change A.1.7 — Make `pat_applyPatternSettingsToMenu` real

**File**: `Core/Bank/Scene/Pattern/PatternData.c`
**Line**: 702 (no-op stub)
**Action**: Modify

Same pattern as A.1.6 for pattern-level fields:
```c
void pat_applyPatternSettingsToMenu(uint8_t s) {
    if (!scene_indexValid(s)) return;
    parameter_values[PAR_PATTERN_NEXT] = pat_regions[s].pattern_next;
    /* pattern_change_bar goes to its PAR_* cell */
}
```

**Inputs**: Scene. **Outputs**: `parameter_values[]` updated.
**Affiliates**: Scene-switch call sites.

---

### A.2 v4 Format Constants and Accessor (Option B)

#### Change A.2.1 — Move `pat_scene_region_t` to PatternData.h

**File**: `Core/Bank/Scene/Pattern/PatternData.h`
**Line**: After line 67 (`PAT_STEPS_PER_SCENE` define)
**Action**: Add

Move the struct typedef from PatternData.c to PatternData.h so
filesystem.c can see `region->address`, `region->pool`, etc.

The struct definition currently at PatternData.c:33–37 becomes a public
declaration in the header (with the new fields from A.1.1). The `.c` file
retains only the `static pat_regions[]` array.

**Why**: Option B accessor — filesystem.c needs the struct layout to
serialize members directly. This is the same visibility model used for
`scene_t`, `kit_t`, `storage_kitset_t`, and every other struct that
filesystem.c serializes. No hidden invariants are broken: the struct is
flat arrays and scalars.

**Affiliates**: filesystem.c v4 reader/writer, PatternData.c.

#### Change A.2.2 — Add `pat_sceneRegion` / `pat_sceneRegionMut` accessors

**File**: `Core/Bank/Scene/Pattern/PatternData.h`
**Line**: After the struct typedef (new)
**Action**: Add declarations

```c
const pat_scene_region_t *pat_sceneRegion(uint8_t scene_index);
pat_scene_region_t       *pat_sceneRegionMut(uint8_t scene_index);
```

**File**: `Core/Bank/Scene/Pattern/PatternData.c`
**Line**: After `pat_initScene()` (new)
**Action**: Add definitions

```c
const pat_scene_region_t *pat_sceneRegion(uint8_t scene_index) {
    if (!scene_indexValid(scene_index)) return NULL;
    return &pat_regions[scene_index];
}
pat_scene_region_t *pat_sceneRegionMut(uint8_t scene_index) {
    if (!scene_indexValid(scene_index)) return NULL;
    return &pat_regions[scene_index];
}
```

**Why**: The v4 writer needs read-only access; the v4 reader needs mutable
access to populate address/pool/bitmap/params after deserialization.
Bounds-checked scene index prevents out-of-range access.

**Inputs**: Scene index 0..15. **Outputs**: Pointer into `pat_regions[]`,
or NULL for invalid index.
**Affiliates**: filesystem.c v4 reader/writer phases.

#### Change A.2.3 — Remove struct definition from PatternData.c

**File**: `Core/Bank/Scene/Pattern/PatternData.c`
**Line**: 33–37 (struct typedef)
**Action**: Remove

The typedef has moved to PatternData.h (A.2.1). The static assert
(A.1.2) stays in `.c` adjacent to the array definition.

#### Change A.2.4 — v4 format constants

**File**: `Core/Bank/Scene/Pattern/PatternData.h`
**Line**: After accessor declarations (new)
**Action**: Add

```c
#define PAT_V4_MAGIC_0       'P'
#define PAT_V4_MAGIC_1       'A'
#define PAT_V4_MAGIC_2       'T'
#define PAT_V4_MAGIC_3       '4'
#define PAT_V4_FORMAT_VERSION 1u
#define PAT_V4_FIXED_HEADER_BYTES   32u
#define PAT_V4_PATTERN_PARAMS_BYTES 16u
#define PAT_V4_TRACK_BLOCK_BYTES    16u
#define PAT_V4_HEADER_TOTAL_BYTES \
    (PAT_V4_FIXED_HEADER_BYTES + PAT_V4_PATTERN_PARAMS_BYTES + \
     (NUM_TRACKS * PAT_V4_TRACK_BLOCK_BYTES))
#define PAT_V4_CRC_OFFSET          14u
#define PAT_V4_ADDRESS_BYTES       (NUM_TRACKS * NUM_STEPS * 2u)
#define PAT_V4_BITMAP_BYTES        512u
#define PAT_V4_POOL_BYTES          (PAT_STACK_SIZE * 32u)
#define PAT_V4_FILE_BYTES \
    (PAT_V4_HEADER_TOTAL_BYTES + PAT_V4_ADDRESS_BYTES + \
     PAT_V4_BITMAP_BYTES + PAT_V4_POOL_BYTES)
```

Plus static asserts:
```c
_Static_assert(PAT_V4_HEADER_TOTAL_BYTES == 160u,
               "v4 pattern header must be 32+16+112 bytes");
_Static_assert(PAT_V4_ADDRESS_BYTES == 1792u,
               "v4 address array must be 7*128*2 bytes");
```

**Why**: Named constants prevent magic numbers in the reader/writer and
make the file format auditable at compile time. The CRC offset constant
matches the Autosave.h pattern (`AUTOSAVE_HEADER_CRC32C_OFFSET`).

**Affiliates**: filesystem.c v4 reader/writer, future AutoSave pattern
drain (S064).

---

### A.3 v3 Removal

#### Change A.3.1 — Delete `PatternSet` typedef and static assert

**File**: `Core/Bank/Scene/Pattern/PatternData.h`
**Lines**: 69–86 (comment block, typedef, static assert)
**Action**: Remove

Delete the `PatternSetStruct` typedef, the `step_on[7][16]` member, and
the `sizeof(PatternSet) == 112` assert. These exist solely for the v3
text bridge.

**Why**: R6/R9 — v3 is completely removed. No code path will reference
`PatternSet` after this session.
**Affiliates**: `pat_initPatternSet`, `pat_patternSetGetStep`,
`pat_patternSetSetStep`, `filesystem_pattern_discard`,
`storage_patternStubParseLine`, `storage_formatPatternStubLine`.

#### Change A.3.2 — Delete `pat_patternSetGetStep` / `SetStep` declarations

**File**: `Core/Bank/Scene/Pattern/PatternData.h`
**Lines**: 88–102 (comment, declarations)
**Action**: Remove

These are the bounded accessors for the legacy 112-byte bitmap. No
callers remain after v3 removal.

#### Change A.3.3 — Delete `pat_initPatternSet` declaration

**File**: `Core/Bank/Scene/Pattern/PatternData.h`
**Line**: 110
**Action**: Remove

The 112-byte memset helper is unused after `filesystem_pattern_discard`
is deleted.

#### Change A.3.4 — Delete `pat_patternSetGetStep` / `SetStep` / `initPatternSet` definitions

**File**: `Core/Bank/Scene/Pattern/PatternData.c`
**Lines**: 392–444 (three function bodies with comments)
**Action**: Remove

These are the implementations of the removed declarations. `SetStep` is
the only writer into the PatternSet bitmap; `GetStep` is the only reader.
Both are called only by `storage_patternStubParseLine` (v3 reader) and
`storage_formatPatternStubLine` (v3 writer).

#### Change A.3.5 — Delete `filesystem_pattern_discard`

**File**: `Core/Hardware/SD/filesystem.c`
**Line**: 119 (static declaration + comment block 107–119)
**Action**: Remove

This 112-byte static `PatternSet` is the discard target for the v3
reader/writer. It is populated during Scene Load and read during Scene
Save, but its contents are deliberately never transferred to live
`pat_regions[]`. With v4, the reader/writer operates directly on
`pat_regions[]`.

**SRAM freed**: 112 bytes SRAM1 `.bss`.
**Affiliates**: `filesystem_directPatternTarget()`.

#### Change A.3.6 — Delete `filesystem_directPatternTarget()`

**File**: `Core/Hardware/SD/filesystem.c`
**Lines**: 1389 (forward declaration), ~15270–15278 (definition)
**Action**: Remove

This accessor returns `&filesystem_pattern_discard`. With the discard
deleted and v3 code removed, no caller exists.

#### Change A.3.7 — Delete `op_pattern_stub_state`

**File**: `Core/Hardware/SD/filesystem.c`
**Line**: 1226
**Action**: Remove

This 5-byte `storage_pattern_stub_state_t` is the incremental parse state
for the v3 text reader. No longer needed.

**SRAM freed**: 5 bytes SRAM1 `.bss`.

#### Change A.3.8 — Delete `filesystem_nextPatternStubLine()` (v3 writer adapter)

**File**: `Core/Hardware/SD/filesystem.c`
**Lines**: 1550–1551 (forward declaration), 15757–15772 (definition)
**Action**: Remove

This adapts `storage_formatPatternStubLine()` to the
`filesystem_writeTextLine()` callback interface. The v4 writer uses
binary streaming, not text lines.

#### Change A.3.9 — Delete v3 pattern defines and retired helper functions

**File**: `Core/Hardware/SD/filesystem.c`
**Lines**: 2728–2737 (FS_PATTERN_FILE_PATTERN_COUNT and associated defines)
**Action**: Remove

Delete `FS_PATTERN_FILE_PATTERN_COUNT`, `FS_PATTERN_STEP_COUNT`,
`FS_PATTERN_MAIN_COUNT`, `FS_PATTERN_SETTINGS_COUNT`,
`FS_PATTERN_LENGTH_COUNT`, `FS_PATTERN_STEP_SIZE`, `FS_PATTERN_MAIN_SIZE`,
`FS_PATTERN_SETTING_SIZE`, `FS_PATTERN_TRACK_SETTINGS_EXTRA_SIZE`,
`FS_PATTERN_TRACK_SHUFFLE_SIZE`, `FS_CONTAINER_VERSION`.

These are sizing constants for the retired binary Step format and the v3
text format. None are referenced by v4 code.

#### Change A.3.10 — Delete retired `#if 0` binary bridge code

**File**: `Core/Hardware/SD/filesystem.c`
**Lines**: 2747–2870+ (`#if 0` block containing
`filesystem_patternStepAddress`, `filesystem_patternTrackAddress`,
`filesystem_discardStep`, `filesystem_discardMainSteps`,
`filesystem_discardPatternSetting`, `filesystem_discardLengthRotate`,
`filesystem_defaultTrackMidiChannel`, `filesystem_defaultTrackSettings`,
`filesystem_patternStepPtr`, `filesystem_patternMainPtr`,
`filesystem_patternSettingPtr`, `filesystem_patternLengthPtr`)
**Action**: Remove entire `#if 0` block

This is the dead binary Step-format bridge code. It references retired
types (`Step`, `PatternSetting`, `LengthRotate`,
`PATTERNDATA_STAGING_PATTERN`) and functions (`pat_stepPtr`,
`pat_mainStepsPtr`, `pat_patternSettingPtr`, `pat_patternLengthPtr`).

#### Change A.3.11 — Replace v3 reader phases 44–55 in Scene Load

**File**: `Core/Hardware/SD/filesystem.c`
**Lines**: 12245–12589 (cases 44–55 in Scene Load state machine)
**Action**: Remove v3 code, replace with v4 binary reader (see B.2)

The current phases:
- 44: Open `op_scene_pattern_open_name`
- 45: Wait open
- 46: Probe "format=" text header
- 47–52: `#if 0` retired binary Step reader
- 53: Read text pattern placeholder/draft via `storage_patternStubParseLine`
- 54: Close bridge pattern
- 55: Wait close

All of this is replaced by v4 binary reader phases that open the same
`.pat` file, stream-read the 160-byte header + 10,496 bytes of payload,
validate CRC, and populate `pat_regions[scene]` directly.

#### Change A.3.12 — Replace v3 writer phases 29–33 in Scene Save

**File**: `Core/Hardware/SD/filesystem.c`
**Lines**: 18339–18376 (cases 29–33 in Scene Save state machine)
**Action**: Remove v3 code, replace with v4 binary writer (see B.1)

The current phases:
- 29: Open `pattern.pat` for write
- 30: Wait open, init `filesystem_pattern_discard`
- 31: Write text lines via `filesystem_nextPatternStubLine`
- 32: Close
- 33: Wait close

Replaced by v4 binary writer phases that open `<name>.pat`, write the
160-byte header from region params, then stream address/bitmap/pool
directly from `pat_regions[scene]`, then seek back to write CRC.

#### Change A.3.13 — Delete `storage_pattern_stub_state_t` and helpers from storageTypes

**File**: `Core/Hardware/SD/storageTypes.h`
**Lines**: 222–245 (typedef), 572–594 (function declarations)
**Action**: Remove

Delete `storage_pattern_stub_state_t`, `storage_patternStubStateInit`,
`storage_patternStubParseLine`, `storage_patternStubFinalize`,
`storage_formatPatternStubLine` declarations.

**File**: `Core/Hardware/SD/storageTypes.c`
**Lines**: 1712–1819 (v3 parser and writer implementations)
**Action**: Remove

Also remove the `#if 0` block at lines 1230–1702 (the older v2
Step/length/scale draft parser).

**Why**: These exist solely for the text-format pattern stub. The v4
binary format has no text parsing.

#### Change A.3.14 — Verification grep

After all removals, verify zero references:
```
grep -rn "PatternSet\|pat_initPatternSet\|pat_patternSetGetStep\|
pat_patternSetSetStep\|filesystem_pattern_discard\|
storage_patternStub\|op_pattern_stub_state\|
filesystem_directPatternTarget\|filesystem_nextPatternStubLine\|
FS_PATTERN_FILE_PATTERN_COUNT\|FS_PATTERN_STEP_COUNT\|
FS_PATTERN_MAIN_COUNT" Core/
```

Any surviving reference is a missed deletion.

---

### A.4 HCNAMES Expansion: 129 → 145 Rows

#### Change A.4.1 — Add `FS_RESIDENT_NAMES_PATTERN_BASE` define

**File**: `Core/Hardware/SD/filesystem.c`
**Line**: After line 159 (after current `FS_RESIDENT_NAMES_ROW_COUNT`)
**Action**: Add

```c
#define FS_RESIDENT_NAMES_PATTERN_BASE \
    (FS_RESIDENT_NAMES_INSTRUMENT_BASE + \
     (STORAGE_BANK_SCENE_MAX_SLOTS * STORAGE_KIT_SLOT_COUNT))
```

This evaluates to 129 (33 + 96) and marks the first Pattern row.

#### Change A.4.2 — Update `FS_RESIDENT_NAMES_ROW_COUNT`

**File**: `Core/Hardware/SD/filesystem.c`
**Line**: 157–159
**Action**: Modify

```c
#define FS_RESIDENT_NAMES_ROW_COUNT \
    (FS_RESIDENT_NAMES_PATTERN_BASE + STORAGE_BANK_SCENE_MAX_SLOTS)
```

This evaluates to 145 (129 + 16).

**Why**: R7 — 16 Pattern rows (one per resident Scene) are appended after
Instrument rows. This grows `fs_resident_source[]` (258→290 B, +32 B) and
`hcnames_name_mirror[]` (1161→1305 B, +144 B).

**Affiliates**: Every loop or bounds check that uses
`FS_RESIDENT_NAMES_ROW_COUNT`.

#### Change A.4.3 — Update HCNAMES mirror comment

**File**: `Core/Hardware/SD/filesystem.c`
**Line**: 1005 (comment says "129 rows x 9 bytes = 1,161 bytes")
**Action**: Modify

Update to "145 rows x 9 bytes = 1,305 bytes".

#### Change A.4.4 — Update HCNAMES mirror comment in line 198 area

**File**: `Core/Hardware/SD/filesystem.c`
**Line**: ~198–204 (comment referencing "129-row mirror")
**Action**: Modify to say "145-row mirror"

#### Change A.4.5 — Add `filesystem_residentPatternRow()` helper

**File**: `Core/Hardware/SD/filesystem.c`
**Line**: After `filesystem_residentSceneRow()` (~line 5473)
**Action**: Add

```c
static uint16_t filesystem_residentPatternRow(uint8_t scene_index)
{
    if (scene_index >= STORAGE_BANK_SCENE_MAX_SLOTS)
        return FS_RESIDENT_NAMES_ROW_COUNT;
    return (uint16_t)(FS_RESIDENT_NAMES_PATTERN_BASE + scene_index);
}
```

**Why**: Pattern rows follow the same mapping convention as Scene, Kit, and
Instrument rows. This helper prevents filesystem code from hardcoding the
129+ offset.

**Inputs**: Scene index 0..15. **Outputs**: HCNAMES row 129..144.
**Affiliates**: HCNAMES read/write, Scene/Bank Load/Save publication paths,
source tracking.

#### Change A.4.6 — Update HCNAMES reader/writer row count handling

**File**: `Core/Hardware/SD/filesystem.c`
**Lines**: HCNAMES text parser and writer loops that iterate up to
`FS_RESIDENT_NAMES_ROW_COUNT`
**Action**: Modify (no functional change — loops are already
`FS_RESIDENT_NAMES_ROW_COUNT`-bounded, so they automatically handle 145
after the constant changes)

Verify that the 9 KB serialization buffer (used by HCNAMES rewrite) can
hold 146 physical lines (145 data + 1 header). At ~16 bytes per line
average, 146 × 16 = 2,336 bytes — fits easily.

#### Change A.4.7 — Update `AUTOSAVE_HCNAMES_ROW_COUNT` (deferred note)

**File**: `Core/Bank/Scene/Autosave.h`
**Line**: 80

**NOTE**: `AUTOSAVE_HCNAMES_ROW_COUNT` is currently 129 and participates
in the AutoSave wire format (`resident_names[129][9]` arrays). Expanding
this changes the AutoSave record layout, which is a Session 064 concern.
For Session 063, the AutoSave HCNAMES count stays at 129; the filesystem's
row count expands to 145. This means Pattern rows are not part of the
AutoSave HCNAMES snapshot until S064.

**Action**: No change this session. Add a comment noting the divergence.

---

### A.5 `/Pattern/.hcindex` Boot Generation

#### Change A.5.1 — Wire Pattern into boot index generation

**File**: `Core/Hardware/SD/filesystem.c`
**Lines**: Boot index generation sequence (where Kit/Scene/Bank indexes are
generated)
**Action**: Modify

Add `FS_FILE_PATTERN` to the boot index chain. The existing
`FS_FILE_PATTERN` registry entry at line 401 already has all capability
flags set to 1 (numbered=1, has_name_header=1, supports_load=1,
supports_save=1). The boot index generator reads `/Pattern/` directory
entries matching `NNN <name>.pat` and writes `/Pattern/.hcindex`.

**Why**: R7 — Pattern library needs an index for the browser, same as
Kit/Scene/Bank.

#### Change A.5.2 — Wire Pattern into save-refresh index chain

**File**: `Core/Hardware/SD/filesystem.c`
**Lines**: Library index rebuild chain (after Pattern Save completion)
**Action**: Modify

After a successful Pattern Save to root `/Pattern/`, trigger the same
index rebuild used by Kit/Scene/Bank Save. The rebuild scans the
directory and rewrites `.hcindex`.

---

## Phase B — v4 Read/Write and Integration

### B.1 v4 Binary Writer (Scene Save Path)

#### Change B.1.1 — New v4 writer phases replacing cases 29–33

**File**: `Core/Hardware/SD/filesystem.c`
**Lines**: 18339–18376 (cases 29–33, replaced per A.3.12)
**Action**: Replace with new phases

The v4 writer needs these state machine phases:

**Phase 29 (OPEN pattern file for write)**:
Open `<name>.pat` using LFN with the pattern's HCNAMES name. The filename
comes from the HCNAMES Pattern row for this Scene (or the Scene name as
default per RQ2). Use `afatfs_fopen_lfn()` with `"w"` mode.

**Phase 30 (WAIT open)**:
Wait for `op_file_ready`. On failure, `filesystem_finish(FS_STATUS_ERROR)`.
On success, initialize streaming state: `op_stream_index = 0`,
`op_item_offset = 0`, `op_bytes_done = 0`.

**Phase 31 (WRITE 160-byte header)**:
Build the header in `staging_buf` (which is at least 512 bytes). Populate:
- Bytes 0–3: magic "PAT4"
- Bytes 4–5: format version 1 (LE)
- Bytes 6–7: PAT_STACK_SIZE (LE)
- Bytes 8–9: PAT_V4_HEADER_TOTAL_BYTES (LE)
- Bytes 10–13: generation counter 0 (library save) (LE)
- Bytes 14–17: CRC placeholder 0x00000000
- Bytes 18–31: reserved zeros
- Bytes 32–47: pattern_change_bar, pattern_next, 14 zeros
- Bytes 48–159: 7 × {length, scale, shuffle, 13 zeros}

Read pattern/track params from `pat_sceneRegion(scene)`.
Stream via `filesystem_writeStreamChunk()`. May need multiple polls.

**Why**: The header is small enough to fit in `staging_buf` in one shot.
The CRC field is zeroed because the CRC is computed post-write (see
phase 33).

**Inputs**: `pat_sceneRegion(scene)->track_length[t]` etc.
**Outputs**: 160 bytes written to SD. `op_bytes_done` tracks progress.

**Phase 32 (STREAM address + bitmap + pool)**:
Stream the three bulk regions directly from the region pointer:
1. `region->address` — 1,792 bytes (cast to `const uint8_t *`)
2. `region->bitmap` — 512 bytes
3. `region->pool` — 8,192 bytes (PAT_STACK_SIZE × 32)

Use `afatfs_fwrite()` in sector-sized chunks, advancing
`op_stream_index` to track which section and byte offset. The data is
native little-endian (ARM Cortex-M7 is LE), matching the file format.

**Why**: R5 — input is disabled during Save, so `pat_regions[scene]` is
stable. No snapshot needed. Direct streaming avoids the 10.7 KB staging
buffer that was eliminated by the accessor decision.

**Inputs**: `pat_sceneRegion(scene)->address`, `->bitmap`, `->pool`.
**Outputs**: 10,496 bytes streamed to SD.
**Affiliates**: `filesystem_writeStreamChunk()`, `afatfs_fwrite()`.

**Phase 33 (COMPUTE CRC and seek-write)**:
After the full file is written:
1. Seek to offset 0 (`afatfs_fseek(op_file, 0, AFATFS_SEEK_SET)`)
2. Stream-read the entire file through `autosave_crc32cByteUpdate()`,
   treating bytes 14–17 as zero
3. Finalize with `autosave_recordCrcFinish()`
4. Seek to offset 14
5. Write the 4-byte CRC
6. Seek back or close

Alternative: compute CRC incrementally during the write phases (tracking
the running accumulator in an `op_` static), then seek back only to write
the 4 bytes at offset 14. This avoids a second full-file read pass.

The incremental approach is preferred: maintain `op_pattern_crc` as a
static uint32_t, call `autosave_crc32cByteUpdate()` on every byte written
(zeroing bytes 14–17), then after the last write, seek to offset 14 and
write the finalized CRC.

**Phase 34 (CLOSE pattern file)**:
`afatfs_fclose()`, wait for `op_close_done`, advance to effects.fx
writer (existing phase 33→34 becomes 35→36 etc., renumbered).

#### Change B.1.2 — Add `op_pattern_crc` static variable

**File**: `Core/Hardware/SD/filesystem.c`
**Line**: Near line 1222 (Scene Load/Save operation scratch)
**Action**: Add

```c
static uint32_t op_pattern_crc;
```

4 bytes. Used only during the v4 write (and later read for validation).
Initialized to `autosave_recordCrcBegin()` (0xFFFFFFFF) at write start.

**SRAM**: 4 bytes static. Replaces the removed `op_pattern_stub_state`
(5 bytes), so net -1 byte.

---

### B.2 v4 Binary Reader (Scene Load Path)

#### Change B.2.1 — New v4 reader phases replacing cases 44–55

**File**: `Core/Hardware/SD/filesystem.c`
**Lines**: 12245–12589 (replaced per A.3.11)
**Action**: Replace with new phases

**Phase 44 (OPEN pattern file)**:
Open `op_scene_pattern_open_name` for read. Same as the existing phase 44.

**Phase 45 (WAIT open)**:
Same wait logic. On NULL file, Scene invalidated.

**Phase 46 (READ + VALIDATE fixed header)**:
Read 32 bytes into `staging_buf`. Validate:
- Magic = "PAT4"
- Format version = 1
- PAT_STACK_SIZE ≤ firmware's PAT_STACK_SIZE (reject larger)
- Header total size ≥ PAT_V4_HEADER_TOTAL_BYTES

Store the file's PAT_STACK_SIZE in an op-local for pool size calculation.
Store the file's header total size for the skip operation.
Initialize `op_pattern_crc = autosave_recordCrcBegin()` and feed the 32
header bytes through it (zeroing bytes 14–17).

On any validation failure: Scene invalidated (R10), jump to close phase.

**Phase 47 (READ pattern params + per-track params)**:
Read bytes 32 through `header_total_size - 1` (128 bytes for v1 format).
Parse pattern_change_bar, pattern_next from bytes 32–33.
Parse per-track blocks: for each track 0..6, read bytes
48 + t*16 .. 48 + t*16 + 2 as length/scale/shuffle.
Write into `pat_sceneRegionMut(scene)->track_length[t]` etc.
Feed all bytes through `op_pattern_crc`.

If `header_total_size` > 160 (future format), seek past the extra bytes
(forward-compatible skip).

**Phase 48 (STREAM-READ address array)**:
Read 1,792 bytes directly into `pat_sceneRegionMut(scene)->address`.
Feed through CRC. Stream in chunks matching `afatfs_fread()` return.

**Phase 49 (STREAM-READ bitmap)**:
Read 512 bytes into `region->bitmap`. Feed through CRC.

**Phase 50 (STREAM-READ pool)**:
Read `file_PAT_STACK_SIZE × 32` bytes into `region->pool`.
If file's PAT_STACK_SIZE < firmware's, the extra region pool bytes are
already zeroed by `pat_initScene()` (which ran before the reader).
Feed through CRC.

**Phase 51 (VALIDATE CRC)**:
Finalize CRC: `autosave_recordCrcFinish(op_pattern_crc)`.
Compare against the stored CRC from the header (saved during phase 46).
On mismatch: Scene invalidated (R10).

**Phase 52 (CLOSE)**:
`afatfs_fclose()`.

**Phase 53 (WAIT close)**:
Wait for `op_close_done`. On success, advance to next child (effect).

**Why**: The binary reader populates `pat_regions[scene]` directly from
the file. No intermediate PatternSet or text parsing.

**Inputs**: `op_scene_pattern_open_name` (discovered in phase 9 scan).
**Outputs**: `pat_regions[scene]` fully populated with file contents.
**Affiliates**: `pat_sceneRegionMut()`, `autosave_crc32cByteUpdate()`.

---

### B.3 Pattern Name in Scene Directory

#### Change B.3.1 — Update Scene Save to use HCNAMES pattern name

**File**: `Core/Hardware/SD/filesystem.c`
**Lines**: Scene Save phase 29 (v4 writer open, see B.1.1)
**Action**: Modify

Instead of hardcoded `"pattern.pat"`, construct the filename from the
HCNAMES Pattern row name: `<name>.pat`. Read the name from
`filesystem_cachedResidentName(filesystem_residentPatternRow(scene))`.
If the name is blank/default, fall back to the Scene name (RQ2).

**Why**: RQ1 — pattern files have user-visible names, not a fixed
`pattern.pat`. The Scene directory contains exactly one `*.pat` file.

#### Change B.3.2 — Scene Load scan already captures `.pat` by extension

**File**: `Core/Hardware/SD/filesystem.c`
**Lines**: 11617–11621 (phase 9 child scan)
**Action**: No change needed

The existing scan already finds the first `.pat` file and stores its
short name in `op_scene_pattern_open_name`. The v4 reader opens this
name. No change required.

Verify: the scan should reject if more than one `.pat` file is found.
Currently it takes only the first. Add a validation check in phase 11:
if a second `.pat` is found during scan, set an error flag. This is a
minor hardening, not a blocker.

---

### B.4 HCNAMES Pattern Row Publication

#### Change B.4.1 — Scene Load publishes Pattern row

**File**: `Core/Hardware/SD/filesystem.c`
**Lines**: Scene Load HCNAMES publication block (after successful load)
**Action**: Modify

After the existing Scene/Kit/Instrument row publications, add:
```c
filesystem_setResidentName(
    filesystem_residentPatternRow(target_scene),
    pattern_display_name);
filesystem_setResidentSource(
    filesystem_residentPatternRow(target_scene),
    source);
```

The pattern display name is extracted from the `.pat` filename stem
(the part before `.pat`). Source is `-` for Scene-embedded patterns,
or the library slot number for library-loaded patterns.

**Why**: C.1/C.5 — Pattern rows must be published alongside other HCNAMES
rows so the register stays coherent.

#### Change B.4.2 — Scene Save publishes Pattern row

**File**: `Core/Hardware/SD/filesystem.c`
**Lines**: Scene Save HCNAMES publication block (after successful save)
**Action**: Modify

Same as B.4.1 but source is the Scene's existing source or `-`.

#### Change B.4.3 — Bank Load/Save publishes Pattern rows

**File**: `Core/Hardware/SD/filesystem.c`
**Lines**: Bank Load/Save HCNAMES publication blocks
**Action**: Modify

Bank Load iterates child Scenes. Each child's Pattern row must be
published. Bank delegates to the shared Scene loader, so the publication
happens in the same Scene Load path (B.4.1). Verify this is reached for
Bank children.

---

### B.5 Root `/Pattern/` Library Load/Save

#### Change B.5.1 — Implement `filesystem_loadPattern_tick()`

**File**: `Core/Hardware/SD/filesystem.c`
**Lines**: Near line 24760 (currently returns `FS_STATUS_ERROR`)
**Action**: Replace error stub with real implementation

State machine phases:
1. Open `/Pattern/` directory
2. Open `NNN <name>.pat` file
3. Validate header (same as B.2 reader)
4. Stream-read into `pat_regions[active_scene]`
5. Validate CRC
6. Close
7. Update HCNAMES Pattern row with library source
8. Completion callback

**Why**: R7 — Pattern gets Load/Save alongside Kit/Scene/Bank. The
`FS_INTERNAL_OP_LOAD_PATTERN` enum already exists (line 342).

**Inputs**: `op_slot` (library slot 0..999), `op_type` = `FS_FILE_PATTERN`.
**Outputs**: Active Scene's pattern region fully replaced.
**Affiliates**: `filesystem_requestLoad()` (line 28057), Menu type cycler.

#### Change B.5.2 — Implement `filesystem_savePattern_tick()`

**File**: `Core/Hardware/SD/filesystem.c`
**Lines**: Near line 24760
**Action**: Replace error stub with real implementation

State machine phases:
1. Open/create `/Pattern/` directory
2. Create `NNN <name>.pat` file
3. Write header from `pat_sceneRegion(active_scene)`
4. Stream-write address + bitmap + pool
5. Compute + write CRC
6. Close
7. Update HCNAMES Pattern row
8. Trigger `/Pattern/.hcindex` rebuild
9. Completion callback

**Inputs**: `op_slot`, pattern name from HCNAMES or user entry.
**Outputs**: File written to `/Pattern/NNN <name>.pat`.
**Affiliates**: `filesystem_requestSave()` (line 28367), Menu type cycler.

#### Change B.5.3 — Wire Pattern into dispatcher

**File**: `Core/Hardware/SD/filesystem.c`
**Lines**: 24760–24771 (dispatcher case)
**Action**: Modify

Replace the `filesystem_finish(FS_STATUS_ERROR)` for
`FS_INTERNAL_OP_LOAD_PATTERN` and `FS_INTERNAL_OP_SAVE_PATTERN` with
calls to the new tick functions.

#### Change B.5.4 — Add Pattern to Menu Load/Save type cycler

**File**: Menu source (likely `menu.c` or `Preset.c`)
**Action**: Modify

Add Pattern to the type rotation alongside Kit, Scene, Bank. This allows
the user to cycle through Load/Save targets and select Pattern.

---

### B.6 Boot Restore with v4

#### Change B.6.1 — Replace v3 boot pattern reader call

**File**: `Core/Hardware/SD/filesystem.c`
**Lines**: Boot Scene restore path (where `pat_initScene()` is called
followed by the pattern reader)
**Action**: Modify

The boot path already calls `pat_initScene(scene)` for each resident
Scene, then attempts to load the pattern from the Scene directory. The
v3 reader call is replaced by the v4 reader (which is the same reader
used in the Scene Load path, B.2). Missing or invalid files leave the
Scene at its `pat_initScene()` defaults (empty pattern).

---

### B.7 Bank Load/Save Verification

No new code changes. Bank delegates each child through the shared Scene
Load/Save path. The v4 reader/writer phases (B.1, B.2) run for each Bank
child Scene. Verification confirms:
- Each Bank child's `<name>.pat` is read/written correctly
- Pattern names are preserved per child
- HCNAMES Pattern rows are published for each loaded child

---

## Risk and Ambiguity Summary

### Resolved

| Item | Resolution |
|------|-----------|
| Track settings storage location | R1: pattern params, stored in `pat_scene_region_t` |
| 17th Scene allocation | R2: deferred to S064 |
| File count in directories | R3: not a blocker |
| Snapshot timing | R4: deferred to S064 |
| Save coherency | R5: input disabled during Save, no snapshot |
| v3 removal scope | R6/R9: full nuke, no fallback |
| Pattern name format | R7: `<name>.pat` in Scene, `NNN <name>.pat` in library |
| Session split | R8: this is the S063 scope |
| Pattern error handling | R10: invalidates entire Scene |
| Row count change | RQ5: no concern, 9 KB buffer sufficient |
| CRC field placement | Fixed header expanded to 32 bytes; CRC at offset 14 |
| CRC polynomial | CRC32C (Castagnoli) — same as `.hcprms` AutoSave |
| Accessor design | **Option B** — expose struct, 2 functions |
| Binary streaming | `filesystem_writeStreamChunk` is binary-safe |
| Pool size | PAT_STACK_SIZE × 32 = 8,192 bytes (spec error corrected) |

### To Resolve During Implementation

1. **Phase number allocation**: v3 removal frees phases 44–55 in Scene Load
   and 29–33 in Scene Save. The v4 reader/writer reuses these slots or
   takes new numbers. Mechanical but requires careful navigation.

2. **Scene switch track-param projection (RQ4)**: Enumerate all callers of
   `pat_applyTrackSettingsToMenu()` / `pat_applyPatternSettingsToMenu()` and
   verify they fire on Scene activation. Must be done during A.1.6/A.1.7.

3. **CRC seek-back strategy**: Incremental CRC during write with a 4-byte
   seek-write at offset 14 is preferred over a second full-file read pass.
   Confirm `afatfs_fseek()` supports seek-to-absolute-offset on a write
   file.

### Spec Error to Correct

`PATTERN_DYNAMIC_STACK.md` §3.1: pool addressing text says `× 4`, should
be `× 32`. Code is correct; only the spec text is wrong. Correct at session
close.

---

## Implementation Sequence

| Step | Description | Depends On | Risk |
|------|-------------|-----------|------|
| A.1 | Per-track params in `pat_scene_region_t` | — | Low |
| A.2 | v4 format constants, struct exposure, accessors | — | Low |
| A.3 | v3 removal | — | Medium (large surgery) |
| A.4 | HCNAMES 129→145 | — | Low |
| A.5 | `/Pattern/.hcindex` boot gen | A.4 | Low |
| B.1 | v4 binary writer (Scene Save) | A.1, A.2, A.3 | Medium |
| B.2 | v4 binary reader (Scene Load) | A.1, A.2, A.3 | Medium |
| B.3 | Pattern name wiring | B.1, B.2 | Low |
| B.4 | HCNAMES Pattern row publication | A.4, B.3 | Low |
| B.5 | `/Pattern/` library load/save | A.5, B.1, B.2 | Medium |
| B.6 | Boot restore with v4 | B.2 | Low |
| B.7 | Bank verification | B.1, B.2 | Low |

---

## RAM Budget (Session 063)

| Change | Bytes | Direction |
|--------|------:|-----------|
| Per-track params in `pat_scene_region_t` (16 × 23 B) | +368 | SRAM1 |
| `fs_resident_source` 129→145 | +32 | SRAM1 |
| `hcnames_name_mirror` 129→145 | +144 | SRAM1 |
| `op_pattern_crc` (uint32_t) | +4 | SRAM1 |
| Remove `filesystem_pattern_discard` (112 B) | −112 | SRAM1 |
| Remove `op_pattern_stub_state` (5 B) | −5 | SRAM1 |
| **Net estimate** | **~+431** | SRAM1 |

Starting SRAM1 bss: 262,468 B. Effective remaining after `pat_regions`
(167,936 B) and `scenes` (19,200 B): ~117,532 B free.
Post-S063: ~117,101 B free.

---

## Change Index by File

### `Core/Bank/Scene/Pattern/PatternData.h`

| Change | Lines | Action | Description |
|--------|-------|--------|-------------|
| A.2.1 | after 67 | Add | `pat_scene_region_t` struct typedef (moved from .c, extended) |
| A.2.2 | after struct | Add | `pat_sceneRegion()` / `pat_sceneRegionMut()` declarations |
| A.2.4 | after accessors | Add | v4 format constants and static asserts |
| A.3.1 | 69–86 | Remove | `PatternSet` typedef + static assert |
| A.3.2 | 88–102 | Remove | `pat_patternSetGetStep/SetStep` declarations |
| A.3.3 | 110 | Remove | `pat_initPatternSet` declaration |

### `Core/Bank/Scene/Pattern/PatternData.c`

| Change | Lines | Action | Description |
|--------|-------|--------|-------------|
| A.2.3 | 33–37 | Remove | struct typedef (moved to .h) |
| A.1.2 | 41–43 | Modify | Update size static assert for new fields |
| A.1.3 | 446–472 | Modify | Initialize new fields in `pat_initScene()` |
| A.3.4 | 392–444 | Remove | `pat_patternSetGetStep/SetStep/initPatternSet` bodies |
| A.2.2 | after init | Add | `pat_sceneRegion()` / `pat_sceneRegionMut()` definitions |
| A.1.4 | 704–706 | Modify | Make `setTrackLength/Scale/Shuffle` real |
| A.1.5 | 711–712 | Modify | Make `setPatternChangeBar/Next` real |
| A.1.6 | 703 | Modify | Make `applyTrackSettingsToMenu` real |
| A.1.7 | 702 | Modify | Make `applyPatternSettingsToMenu` real |

### `Core/Hardware/SD/filesystem.c`

| Change | Lines | Action | Description |
|--------|-------|--------|-------------|
| A.3.5 | 107–119 | Remove | `filesystem_pattern_discard` + comment |
| A.4.1 | after 159 | Add | `FS_RESIDENT_NAMES_PATTERN_BASE` define |
| A.4.2 | 157–159 | Modify | `FS_RESIDENT_NAMES_ROW_COUNT` → 145 |
| A.4.3 | ~1005 | Modify | Comment update "129 rows" → "145 rows" |
| A.4.4 | ~198 | Modify | Comment update "129-row" → "145-row" |
| A.3.7 | 1226 | Remove | `op_pattern_stub_state` |
| B.1.2 | ~1222 | Add | `op_pattern_crc` static uint32_t |
| A.3.6 | 1389, ~15270 | Remove | `filesystem_directPatternTarget()` fwd decl + body |
| A.3.8 | 1550, 15757–72 | Remove | `filesystem_nextPatternStubLine()` fwd decl + body |
| A.3.9 | 2728–2737 | Remove | `FS_PATTERN_*` defines |
| A.3.10 | 2747–2870+ | Remove | `#if 0` binary bridge code block |
| A.4.5 | after ~5473 | Add | `filesystem_residentPatternRow()` helper |
| A.3.11 | 12245–12589 | Replace | v3 reader → v4 binary reader phases (B.2) |
| A.3.12 | 18339–18376 | Replace | v3 writer → v4 binary writer phases (B.1) |
| B.4.1 | Scene Load pub | Modify | Publish Pattern HCNAMES row |
| B.4.2 | Scene Save pub | Modify | Publish Pattern HCNAMES row |
| B.4.3 | Bank Load/Save | Modify | Publish Pattern HCNAMES rows per child |
| B.5.1 | ~24760 | Replace | Implement `filesystem_loadPattern_tick()` |
| B.5.2 | ~24760 | Replace | Implement `filesystem_savePattern_tick()` |
| B.5.3 | 24760–24771 | Modify | Wire dispatcher to new tick functions |

### `Core/Hardware/SD/storageTypes.h`

| Change | Lines | Action | Description |
|--------|-------|--------|-------------|
| A.3.13 | 222–245 | Remove | `storage_pattern_stub_state_t` typedef |
| A.3.13 | 572–594 | Remove | Pattern stub function declarations |

### `Core/Hardware/SD/storageTypes.c`

| Change | Lines | Action | Description |
|--------|-------|--------|-------------|
| A.3.13 | 1230–1702 | Remove | `#if 0` v2 draft parser |
| A.3.13 | 1712–1819 | Remove | v3 parser + writer implementations |

### Menu source

| Change | Lines | Action | Description |
|--------|-------|--------|-------------|
| B.5.4 | TBD | Modify | Add Pattern to Load/Save type cycler |
