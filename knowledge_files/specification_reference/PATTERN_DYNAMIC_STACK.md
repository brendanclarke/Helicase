# Dynamic Pattern Storage Specification

This is the authoritative specification for the live Pattern storage system
implemented in Session 062. It describes the memory layout, allocator
algorithm, public and internal APIs, integration points with the Sequencer and
Menu, the filesystem bridge, and planned future extensions.

The dynamic system replaces the bitmap-only Session 043 representation. The
legacy `PatternSet` (112-byte `step_on[7][16]` bitmap) is retained only for
the disconnected v3 filesystem bridge and is never live Scene data. This
document is the primary reference for anyone modifying `PatternData.c/h`,
the Sequencer trigger path, the step-edit menu, or Pattern persistence.

## Implementation Status

| Component | Status | Session |
|-----------|--------|---------|
| Address array + pool + bitmap structures | Complete, tested | 062 |
| pat_initScene / pat_initPatternSet | Complete, tested | 062 |
| Trigger operations (isStepActive, toggleStep, setStepActive, eraseStep) | Complete, tested | 062 |
| Bitmap allocator (alloc, free) | Complete, tested | 062 |
| Block read/write | Complete, tested | 062 |
| pat_readStepSpecials (unified reader) | Complete, tested | 062 |
| Note/velocity/probability setters (pool read-modify-write) | Complete, tested | 062 |
| Step-edit menu bridge | Complete, tested | 062 |
| Sequencer probability gating | Complete, tested | 062 |
| Pool offset validation (hardening) | Complete, tested | 062 |
| pat_clearTrack / pat_clearPattern (pool-aware) | Complete, tested | 062 |
| pat_copyTrack / pat_copyPattern / pat_copyBar | **No-op stubs** — deferred to Phase 4.5 |
| v4 PAT4 binary file format (serialization) | Complete, tested | 063 |
| pat_scene_region_t packed struct + Option B accessors | Complete, tested | 063 |
| Pattern Save HCNAMES/index chain fix | Complete, tested | 063 |
| Pattern Load dual-defect fix (menu lifecycle + scene mask) | Complete, tested | 063 |
| filesystem_requestLoadPatternForScenes (mask + fan-out) | Complete, tested | 063 |
| HCNAMES 145-row expansion (Pattern rows 129-144) | Complete, tested | 063 |
| Step automation (pool header reserves 6-bit count) | **Not started** — header field reserved |
| pat_applyPatternSettingsToMenu | Compatibility shim, no storage | 062 |
| pat_setTrackLength / Scale / Shuffle | Live per-track storage, not pooled | pre-062 |

## 1. Memory Budget

### 1.1 Per-Scene Region (`pat_scene_region_t`)

As of Session 063, the per-Scene data is wrapped in a packed struct
`pat_scene_region_t`:

| Component | Formula | Bytes |
|-----------|---------|------:|
| Address array | 7 tracks × 128 steps × 2 B | 1,792 |
| Event pool | PAT_STACK_SIZE × 32 B | 8,192 |
| Free bitmap | 512 (always) | 512 |
| track_length[7] | 7 × 1 B | 7 |
| track_scale[7] | 7 × 1 B | 7 |
| track_shuffle[7] | 7 × 1 B | 7 |
| pattern_change_bar | 1 B | 1 |
| pattern_next | 1 B | 1 |
| **Total per Scene** | | **10,519** |

Accessed via Option B accessors `pat_sceneRegion(scene_index)` (const) and
`pat_sceneRegionMut(scene_index)` (mutable), matching the `scene_t` / `kit_t`
pattern.

### 1.2 System Total

| Quantity | Value |
|----------|------:|
| Scenes | 16 |
| Total pat_scene_region_t × 16 | 168,304 B |
| scenes (after PatternSet removal) | 19,200 B |
| filesystem_pattern_discard | 112 B |
| **SRAM1 Pattern footprint** | **187,616 B** |

`PAT_STACK_SIZE` is defined in `config.h` as `256u`. The address array size
is fixed; the pool and bitmap scale with PAT_STACK_SIZE. Increasing
PAT_STACK_SIZE requires SRAM1 reservation approval per the RAM allocation
policy in `MEMORY.md`.

## 2. Address Array

### 2.1 Layout

The address array is a flat `uint16_t` array of 896 entries, indexed as
`addr[track * NUM_STEPS + step]` where `track ∈ [0,6]` and `step ∈ [0,127]`.

### 2.2 Entry Encoding

```
Bit 15:    PAT_ADDR_TRIGGER_BIT  — step is active (trigger on)
Bit 14:    PAT_ADDR_SPECIALS_BIT — a dynamic pool block exists
Bits 13-0: PAT_ADDR_OFFSET_MASK  — 4-byte-aligned pool byte offset
           PAT_ADDR_SENTINEL = 0x3FFF — no pool block allocated
```

Key properties:

- Trigger state (bit 15) is independent of specials state (bit 14). An
  on→off→on cycle preserves any allocated pool block.
- `pat_eraseStep()` frees the pool block and writes the full sentinel; this
  is the permanent removal path.
- The sentinel value 0x3FFF cannot be a valid 4-byte-aligned offset within
  the pool range (max valid offset = (PAT_STACK_SIZE - 1) × 4 = 1020).
- `pat_toggleStep()` and `pat_setStepActive()` modify only bit 15, leaving
  bits 14..0 intact.

### 2.3 Defines (PatternData.h)

```c
#define PAT_ADDR_SENTINEL     0x3FFFu
#define PAT_ADDR_TRIGGER_BIT  (1u << 15)
#define PAT_ADDR_SPECIALS_BIT (1u << 14)
#define PAT_ADDR_OFFSET_MASK  0x3FFFu
```

## 3. Event Pool

### 3.1 Pool Geometry

The pool is declared as `uint8_t pool[PAT_STACK_SIZE * 32]` = 8,192 bytes.
`PAT_STACK_SIZE` (256) is the number of allocatable 4-byte chunks.

- 256 chunks × 4 bytes each = 1,024 bytes of addressable pool
- Chunk `n` starts at byte offset `n * 4`
- Maximum valid pool offset: `(PAT_STACK_SIZE - 1) * 4 = 1,020`
- Bytes 1,024..8,191 are reserved for future `PAT_STACK_SIZE` growth
- The bitmap has 512 slots; only the lower 256 map to real chunks
- The upper 256 bitmap slots are permanently `0xFF` (scan terminators)

### 3.2 Block Format

Each allocated block starts at a chunk-aligned (4-byte) boundary:

```
Byte 0-1:  Header (uint16_t, big-endian in memory as two bytes)
           Bits 15..6: 10-bit step-ID = track * NUM_STEPS + step
           Bits 5..0:  6-bit automation count (0 in Session 062)

Byte 2:    Special-flags byte
           Bit 0: PAT_SPECIAL_NOTE_BIT  — note override present
           Bit 1: PAT_SPECIAL_VEL_BIT   — velocity override present
           Bit 2: PAT_SPECIAL_PROB_BIT  — probability override present
           Bits 3-7: Reserved (must be 0)

Byte 3+:   Value bytes in ascending bit order of flags
           If note:        1 byte (uint8_t, 0..127)
           If velocity:    1 byte (uint8_t, 0..127)
           If probability: 1 byte (uint8_t, 0..127; 127 = always fires)
```

### 3.3 Chunk Sizing

| Condition | Total bytes | Chunks |
|-----------|:-----------:|:------:|
| 0 specials (flags=0) | 0 (block freed) | 0 |
| 1 special | 4 (header + flags + 1 value) | 1 |
| 2 specials | 5 (header + flags + 2 values) | 2 |
| 3 specials | 6 (header + flags + 3 values) | 2 |

Computed by `pat_blockChunks(flags, auto_count)`:
- Count popcount of flags masked with `PAT_SPECIAL_FLAGS_MASK`
- Add `auto_count` (currently always 0)
- Total payload = `PAT_BLOCK_HEADER_BYTES + 1 + popcount + auto_count`
- Chunks = `(total + 3) / 4` (ceiling division to 4-byte boundary)

### 3.4 Defines (PatternData.h)

```c
#define PAT_SPECIAL_NOTE_BIT     (1u << 0)
#define PAT_SPECIAL_VEL_BIT      (1u << 1)
#define PAT_SPECIAL_PROB_BIT     (1u << 2)
#define PAT_SPECIAL_FLAGS_MASK   (PAT_SPECIAL_NOTE_BIT | \
                                  PAT_SPECIAL_VEL_BIT | \
                                  PAT_SPECIAL_PROB_BIT)

#define PAT_BLOCK_HEADER_BYTES    2u
#define PAT_BLOCK_STEP_ID_SHIFT   6u
#define PAT_BLOCK_STEP_ID_MASK    0xFFC0u
#define PAT_BLOCK_AUTO_COUNT_MASK 0x003Fu
```

### 3.5 Default Values (config.h)

```c
#define PAT_STACK_SIZE       256u
#define PAT_DEFAULT_NOTE      63u
#define PAT_DEFAULT_VELOCITY 100u
```

Probability default is 127 (always fires), defined inline in
`pat_readStepSpecials`.

## 4. Free Bitmap

### 4.1 Layout

The bitmap is a 512-byte array of per-slot occupancy markers:

- `0x00` = free chunk
- `0xFF` = occupied (or permanently unavailable)

At initialization:
- Bytes 0..255 (lower PAT_STACK_SIZE): set to `0x00` (free)
- Bytes 256..511 (upper half): set to `0xFF` (permanently occupied)

The permanently-occupied upper half acts as a scan terminator: the
first-fit allocator can scan linearly without separate bounds checking.

### 4.2 Operations

| Function | Action |
|----------|--------|
| `pat_bitmapGet(region, slot)` | Return byte value at slot |
| `pat_bitmapSet(region, slot)` | Write `0xFF` at slot |
| `pat_bitmapClear(region, slot)` | Write `0x00` at slot |

## 5. Allocator

### 5.1 First-Fit Algorithm

`pat_poolAlloc(region, chunks)`:

1. Linear scan from slot 0 through the full 512-byte bitmap
2. Find a contiguous run of `chunks` consecutive `0x00` bytes
3. On success: mark all slots `0xFF`, return `start_slot * 4`
4. On failure: return `PAT_ADDR_SENTINEL`

This is adequate for menu-paced edits. No free-list, no best-fit, no
defragmentation. Worst case: 256 slots × 512 bitmap bytes = ~128K
comparisons, which is trivially fast on a 216 MHz Cortex-M7.

### 5.2 Free

`pat_poolFree(region, offset, chunks)`:

1. Compute `start_slot = offset / 4`
2. Clear `chunks` consecutive bitmap slots to `0x00`
3. Zero the freed pool bytes (optional cleanup)

### 5.3 Offset Validation

`pat_poolOffsetValid(region, offset)`:

Returns true if and only if:
- `offset != PAT_ADDR_SENTINEL`
- `offset` is 4-byte aligned (`offset & 3 == 0`)
- `offset < PAT_STACK_SIZE * 4` (within addressable pool range)

This hardening helper gates every pool read path. Invalid offsets produce
default values, not crashes.

## 6. Central Write Path: pat_writeSpecials

`pat_writeSpecials(region, addr_entry_ptr, track, step, specials)` is the
single mutation funnel for all pool block writes. It handles four cases:

| Case | Condition | Action |
|------|-----------|--------|
| 1 | `flags == 0` and no existing block | No-op |
| 2 | `flags == 0` and block exists | Free block, clear SPECIALS_BIT, retain trigger |
| 3 | Same chunk count as existing block | In-place `pat_blockWrite` |
| 4 | Different chunk count | Free old, alloc new, write new; on alloc failure, try reusing old slot count with reduced specials |

**Graceful degradation**: If a reallocation fails (pool full), the writer
attempts to retain the existing block size and stores whatever subset of
specials fits. If no block exists at all and allocation fails, the write is
silently dropped. No assert, no error display, no data corruption.

## 7. Public API

### 7.1 Unified Reader

```c
typedef struct {
    uint8_t note;         // PAT_DEFAULT_NOTE if not stored
    uint8_t velocity;     // PAT_DEFAULT_VELOCITY if not stored
    uint8_t probability;  // 127 if not stored
    uint8_t flags;        // which values were explicitly stored
} pat_step_specials_t;

pat_step_specials_t pat_readStepSpecials(uint8_t scene_index,
                                         uint8_t track, uint8_t step);
```

Always returns usable values. The `flags` field lets callers distinguish
stored values from defaults. Used by:
- **Sequencer**: `seq_advanceTrackStep` reads specials for every triggered step
- **Menu**: `pat_applyStepToMenu` reads specials to populate parameter_values

### 7.2 Special Setters (Pool Read-Modify-Write)

```c
void pat_setStepNote(uint8_t scene_index, uint8_t track,
                     uint8_t step, uint8_t value);
void pat_setStepVolume(uint8_t scene_index, uint8_t track,
                       uint8_t step, uint8_t value);
void pat_setStepProbability(uint8_t scene_index, uint8_t track,
                            uint8_t step, uint8_t value);
```

Each setter:
1. Reads current specials via `pat_readStepSpecials` (or direct block read)
2. Updates the targeted field and its flag bit
3. Handles default-value optimization: setting note to PAT_DEFAULT_NOTE
   clears its flag bit (reducing block size when possible)
4. Calls `pat_writeSpecials` for the central 4-case mutation

Called from `menu.c` parameter dispatch:
- `PAR_STEP_NOTE` → `pat_setStepNote`
- `PAR_STEP_VOLUME` → `pat_setStepVolume`
- `PAR_STEP_PROB` → `pat_setStepProbability`

### 7.3 Trigger Operations

```c
uint8_t pat_isStepActive(uint8_t track, uint8_t step, uint8_t scene_index);
void pat_toggleStep(uint8_t track, uint8_t step, uint8_t scene_index);
void pat_setStepActive(uint8_t scene_index, uint8_t track,
                       uint8_t step, uint8_t on);
void pat_eraseStep(uint8_t scene_index, uint8_t track, uint8_t step);
```

- `pat_isStepActive`: reads bit 15 of address entry
- `pat_toggleStep`: XORs bit 15, preserving bits 14..0
- `pat_setStepActive`: sets or clears bit 15, preserving bits 14..0
- `pat_eraseStep`: **frees pool block first**, then writes full sentinel

### 7.4 Range Operations

```c
void pat_clearTrack(uint8_t scene_index, uint8_t track);
void pat_clearPattern(uint8_t scene_index);
void pat_copyTrack(uint8_t scene_index, uint8_t src_track, uint8_t dst_track);
void pat_copyPattern(uint8_t src_scene, uint8_t dst_scene);
void pat_copyBar(uint8_t scene_index, uint8_t track,
                 uint8_t src_bar, uint8_t dst_bar);
```

- `pat_clearTrack`: walks all 128 steps, frees each allocated block, writes
  sentinel to each address entry
- `pat_clearPattern`: calls `pat_clearTrack` for all 7 tracks
- **Copy operations are deliberate no-ops** pending Phase 4.5 pool block
  duplication design

### 7.5 Initialization

```c
void pat_initScene(uint8_t scene_index);
void pat_initPatternSet(PatternSet *pattern);
```

- `pat_initScene`: sets all address entries to sentinel, zeroes pool,
  initializes bitmap (lower half free, upper half occupied)
- `pat_initPatternSet`: zeroes the legacy 112-byte bitmap (filesystem only)

### 7.6 Menu Bridge

```c
void pat_applyStepToMenu(uint8_t scene_index, uint8_t track, uint8_t step);
```

Reads pool block via `pat_blockRead` (or returns defaults for unallocated
steps) and writes resolved values into `parameter_values[PAR_STEP_NOTE]`,
`parameter_values[PAR_STEP_VOLUME]`, `parameter_values[PAR_STEP_PROB]`.

Called by `menu_showStepEditPage()` and on step-change encoder events.

### 7.7 Legacy Bridge

```c
uint8_t pat_patternSetGetStep(const PatternSet *pattern, uint8_t track,
                              uint8_t step);
uint8_t pat_patternSetSetStep(PatternSet *pattern, uint8_t track,
                              uint8_t step, uint8_t on);
```

Read/write one bit in a caller-owned `PatternSet`. Used only by the v3
filesystem bridge in `storageTypes.c`. These do not interact with the
dynamic address array or pool.

## 8. Sequencer Integration

### 8.1 Trigger Path

In `seq_advanceTrackStep` (sequencer.c, lines ~394-408):

```
1. Read: pat_step_specials_t sp = pat_readStepSpecials(scene, track, step)
2. Probability gate:
   if sp.probability < 127:
     rng = GetRngValue()                  // hardware RNG, int16_t
     rnd = (rng & 0x7FFF) * 127 / 32767  // map to 0..127
     if rnd >= sp.probability:
       should_trigger = false             // suppress this step
3. Trigger:
   if should_trigger:
     voiceControl_noteOn(track, sp.velocity, sp.note)
```

### 8.2 Roll Triggers

Roll (sub-step repeat) triggers are independent fixed-note events. They use
the track's default note and a fixed velocity, not the step's specials.
This is intentional: rolls are timing events, not note events.

### 8.3 Dependencies

- `#include "random.h"` for `GetRngValue()` (hardware RNG, returns int16_t)
- `#include "PatternData.h"` for `pat_readStepSpecials`

## 9. Menu Step-Edit Integration

### 9.1 Entry Point

`menu_showStepEditPage()` (menu.h/menu.c):
1. Sets `menuIndex` to subpage 1 (step-edit page in menuPages.h)
2. Calls `pat_applyStepToMenu()` to read current specials into parameter_values
3. Calls `menu_endlessPotMappingChanged()` for encoder rebase
4. Calls `menu_repaintAll()` for full display refresh

### 9.2 Step-Edit Page Layout (menuPages.h subpage 1)

| Cell | Parameter | Pool field |
|------|-----------|-----------|
| 0 | PAR_STEP_VOLUME (81) | velocity |
| 1 | PAR_STEP_NOTE (83) | note |
| 2 | PAR_STEP_PROB (82) | probability |
| 3 | PAR_P1_DEST | automation slot 1 destination (future) |
| 4 | PAR_P1_VAL | automation slot 1 value (future) |
| 5 | PAR_P2_DEST | automation slot 2 destination (future) |
| 6 | PAR_P2_VAL | automation slot 2 value (future) |

### 9.3 Parameter Routing (menu.c ~lines 9751-9776)

The existing `case PAR_STEP_VOLUME/NOTE/PROB` dispatch calls the
pat_setStep* functions, which now perform real pool read-modify-write.
PAR_ACTIVE_STEP (80) is the step cursor.

## 10. Filesystem Bridge

### 10.1 Current State (v3 — Disconnected)

- **Boot read**: Returns 0 (empty pattern). No dynamic data loaded from disk.
- **Save write**: Writes an empty 112-byte `PatternSet` through
  `filesystem_pattern_discard`. Specials are not persisted.
- **v3 format**: Seven 32-hex-character rows representing the trigger bitmap.
  This is the same format Scene/Bank saves use via `pattern.pat`.
- **Fan-out**: Disconnected. The v3 bridge bitmap is not synchronized with
  the live address array.

### 10.2 PatternSet Retention

`PatternSet` exists only in:
- `PatternData.h`: type definition and static assert (112 bytes)
- `filesystem.c`: one `filesystem_pattern_discard` instance
- `storageTypes.c`: v3 parser/writer using `pat_patternSetGetStep/SetStep`

`scene_t` contains `{ scene_settings_t settings; kit_t kit; }` — no
PatternSet field.

### 10.3 Future v4 Format Requirements

A v4 Pattern file must serialize:
1. The complete address array (1,792 bytes or compressed equivalent)
2. All allocated pool blocks (variable size, up to 1,024 used bytes)
3. The bitmap (512 bytes or reconstructed from pool blocks)
4. Track settings (length, scale, shuffle) already have their own storage

This is deferred work. The primary design question is whether to serialize
all 256 chunks or only allocated blocks.

## 11. Planned Extensions

### 11.1 Phase 4.5 — Copy Operations

`pat_copyTrack`, `pat_copyPattern`, and `pat_copyBar` must:
1. Allocate new pool blocks in the destination Scene
2. Copy block contents from source pool to destination pool
3. Update destination address entries with new pool offsets
4. Handle allocation failure gracefully (partial copy or error)

For `pat_copyPattern` (cross-Scene), source and destination have
independent pools and bitmaps.

### 11.2 Step Automation

The pool block header reserves 6 bits for an automation count. When
automation is implemented:
- The flags byte gains bits 3-7 for automation-related markers
- Automation destination/value pairs append after the specials values
- Block chunk count increases accordingly
- `pat_blockChunks` already accepts `auto_count` in its computation

### 11.3 AutoSave Pattern Persistence

Pattern data is not in the current HCPR AutoSave records. Adding Pattern
persistence requires either extending the HCPR format or creating a
separate Pattern-specific AutoSave stream.

## 12. Invariants and Constraints

1. **No parallel Pattern owner**: only `PatternData.c` may allocate,
   read, or write pool blocks. Sequencer reads through
   `pat_readStepSpecials`; Menu reads through `pat_applyStepToMenu`.

2. **Pool offset validation**: every read path gates on
   `pat_poolOffsetValid` before dereferencing. Invalid offsets produce
   defaults, never crashes.

3. **Erase frees before sentinel**: `pat_eraseStep` must free the pool
   block before writing the sentinel to prevent pool leaks.

4. **Clear walks all steps**: `pat_clearTrack` must iterate all 128 steps
   and free each allocated block.

5. **Copy is a no-op**: do not enable copy operations without implementing
   pool block duplication. A memcpy of address entries without pool
   duplication creates dangling/aliased offsets.

6. **Default values are not stored**: setting a special to its default
   value clears the corresponding flag bit, potentially shrinking or
   freeing the block. This is an optimization, not a bug.

7. **Trigger independence**: bit 15 (trigger) is orthogonal to bit 14
   (specials). Toggling a step on/off does not affect its pool block.

8. **Scene independence**: each Scene has its own pool and bitmap. No
   cross-Scene aliasing is possible.

9. **Roll independence**: roll triggers do not read specials. They use
   the track's default note and fixed velocity.

10. **RAM policy**: `pat_regions` is the permanent Pattern allocation.
    No additional Pattern SRAM may be allocated without explicit approval
    per the policy in `MEMORY.md`.
