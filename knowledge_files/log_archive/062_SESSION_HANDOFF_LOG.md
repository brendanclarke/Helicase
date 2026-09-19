# Session 062 — Phase 4 Dynamic Pattern Storage

```
DATE: 2026-09-09/10
SESSION GOAL: Replace bitmap-only Pattern representation with dynamic
  address-array + event-pool + free-bitmap storage; implement per-step note,
  velocity, and probability specials with allocator, menu bridge, and
  Sequencer playback integration.
COMPLETED: Full Steps A through E — architecture design, address-array and
  pool structures, bitmap allocator, block read/write, step-edit menu bridge,
  Sequencer probability gating, PatternSet removal from scene_t, and hardware
  verification of all specials persistence and Scene independence.
VERIFIED ON HARDWARE: Yes — entered steps on several tracks across several
  scenes, switched scenes, changed note number, velocity, and probability,
  confirmed specials were retained and switched correctly between scenes.

CHANGES THIS SESSION:
- Core/Bank/Scene/Pattern/PatternData.h: complete header with address-array
  defines, special-flags defines, header constants, pat_step_specials_t,
  pat_readStepSpecials declaration
- Core/Bank/Scene/Pattern/PatternData.c: full allocator and specials
  implementation — static helpers (bitmap, pool offset validation, alloc,
  free, block chunks/write/read, writeSpecials), public pat_readStepSpecials,
  modified pat_eraseStep/pat_clearTrack/pat_applyStepToMenu/
  pat_setStepNote/Volume/Probability
- Core/Sequencer/sequencer.c: probability gating with pat_readStepSpecials
  and GetRngValue hardware RNG in seq_advanceTrackStep
- config.h: PAT_STACK_SIZE 256u, PAT_DEFAULT_NOTE 63u, PAT_DEFAULT_VELOCITY
  100u

KNOWN ISSUES INTRODUCED: None
KNOWN ISSUES RESOLVED: Step-edit menu navigation (B½ fix confirmed on
  hardware)

NEXT SESSION RECOMMENDED GOAL: Phase 4.5 copy operations
  (pat_copyTrack/Pattern/Bar with pool block duplication), then v4 Pattern
  file format for Scene/Bank persistence.
BLOCKERS: None — all Phase 4 core implementation is hardware-verified.

CRITICAL REMINDERS FOR NEXT SESSION:
- pat_regions is 167,936 B in SRAM1 — do not add parallel Pattern storage
- Copy operations are deliberate no-ops; they must duplicate pool blocks
- Filesystem bridge is disconnected: boot reader returns 0, save writes
  empty discard PatternSet, fan-out disconnected
- Roll triggers remain independent fixed-note events — do not route through
  specials
- pat_poolOffsetValid() is a hardening addition not in the original schedule
- Default values: PAT_DEFAULT_NOTE=63, PAT_DEFAULT_VELOCITY=100,
  probability default=127 (always fires)
```

---

## 1. Memory Architecture

### 1.1 Per-Scene Region Layout (pat_scene_region_t)

Each of the 16 resident Scenes owns a contiguous 10,496-byte region in the
static `pat_regions` array (total 167,936 bytes in SRAM1 `.bss`):

| Component | Size | Purpose |
|-----------|-----:|---------|
| Address array | 1,792 B | 896 × uint16_t entries (7 tracks × 128 steps) |
| Event pool | 8,192 B | PAT_STACK_SIZE=256 × 32-byte chunks |
| Free bitmap | 512 B | One byte per potential chunk slot; lower 256 active, upper 256 permanently 0xFF |

The address array is indexed as `addr[track * NUM_STEPS + step]`.

### 1.2 Address Entry Encoding (uint16_t)

| Bits | Name | Purpose |
|------|------|---------|
| 15 | PAT_ADDR_TRIGGER_BIT | Step trigger state (on/off) |
| 14 | PAT_ADDR_SPECIALS_BIT | Dynamic block contains specials |
| 13..0 | PAT_ADDR_OFFSET_MASK | 4-byte-aligned pool byte offset; 0x3FFF = sentinel (no data) |

The sentinel value `PAT_ADDR_SENTINEL` (0x3FFF) cannot be a valid aligned
offset within the pool range. Trigger state (bit 15) is independent of
specials state (bit 14) so an on→off→on cycle preserves any allocated pool
block.

### 1.3 Pool Block Format

Each allocated pool block starts at a 4-byte (one-chunk) aligned offset:

| Offset | Size | Content |
|--------|-----:|---------|
| 0 | 2 B | Header: bits 15..6 = 10-bit step-ID (`track * NUM_STEPS + step`), bits 5..0 = 6-bit automation count (always 0 this session) |
| 2 | 1 B | Special-flags byte |
| 3+ | 1–3 B | Value bytes in ascending bit order of flags |

**Special-flags byte:**

| Bit | Define | Meaning | Value byte |
|-----|--------|---------|------------|
| 0 | PAT_SPECIAL_NOTE_BIT | Note override present | uint8_t note (0..127) |
| 1 | PAT_SPECIAL_VEL_BIT | Velocity override present | uint8_t velocity (0..127) |
| 2 | PAT_SPECIAL_PROB_BIT | Probability override present | uint8_t probability (0..127, 127=always) |
| 3..7 | Reserved | Must be zero | — |

Values appear in the block in ascending bit order: note first (if present),
then velocity, then probability.

**Chunk sizing:**

| Specials count | Header + flags + values | Chunks required | Block bytes |
|:-:|:-:|:-:|:-:|
| 0 | — | 0 (freed) | 0 |
| 1 | 2 + 1 + 1 = 4 | 1 | 4 |
| 2 | 2 + 1 + 2 = 5 | 2 | 8 |
| 3 | 2 + 1 + 3 = 6 | 2 | 8 |

### 1.4 Free Bitmap

The 512-byte bitmap uses one byte per potential chunk slot:

- `0x00` = free
- `0xFF` = occupied (or permanently unavailable)

At initialization, the lower `PAT_STACK_SIZE` (256) bytes are zeroed (free),
and the upper 256 bytes are set to `0xFF` (permanently occupied sentinel).
This allows a single linear scan without separate bounds checking. The bitmap
uses byte-per-slot (not bit-per-slot) for simplicity at the current scale.

### 1.5 Allocator

**First-fit linear scan** starting from byte 0 of the bitmap, looking for
a contiguous run of `0x00` bytes equal to the requested chunk count (1 or 2).
Returns the byte offset within the pool (`slot_index * 4`) or
`PAT_ADDR_SENTINEL` on failure. Menu-paced, no defragmentation, no free-list.

**Allocation failure** is graceful: `pat_writeSpecials` falls back to
retaining whatever subset of specials fit in the existing block size, or
drops the write entirely if no block exists. No assert, no error display.

### 1.6 Scene Initialization

`pat_initScene(scene_index)` sets every address entry to `PAT_ADDR_SENTINEL`,
zeroes the pool, and initializes the bitmap (lower half 0x00, upper half
0xFF). This matches the legacy `pat_initPatternSet()` behavior of silence
but also clears all dynamic state.

---

## 2. PatternData.c Implementation Detail

### 2.1 Static Helper Functions

| Function | Purpose |
|----------|---------|
| `pat_bitmapGet(region, slot)` | Read one bitmap byte |
| `pat_bitmapSet(region, slot)` | Mark slot occupied (0xFF) |
| `pat_bitmapClear(region, slot)` | Mark slot free (0x00) |
| `pat_poolOffsetValid(region, offset)` | Validate: not sentinel, 4-byte aligned, within pool range |
| `pat_poolAlloc(region, chunks)` | First-fit allocator; returns byte offset or sentinel |
| `pat_poolFree(region, offset, chunks)` | Clear bitmap slots for a block |
| `pat_blockChunks(flags, auto_count)` | Compute chunk count from flags and automation |
| `pat_blockWrite(pool, offset, step_id, flags, auto_count, specials)` | Serialize header + flags + values into pool |
| `pat_blockRead(pool, offset, out_specials)` | Deserialize pool block into pat_step_specials_t |
| `pat_writeSpecials(region, addr_entry_ptr, track, step, specials)` | Central read-modify-write: handles 4 cases |

### 2.2 pat_writeSpecials Four Cases

1. **flags=0, no block** → no-op
2. **flags=0, block exists** → free block, clear specials bit, retain
   trigger
3. **Same chunk count** → in-place rewrite
4. **Different chunk count** → free old, alloc new, write new; on alloc
   failure, try to reuse old size with reduced specials (graceful
   degradation)

### 2.3 Modified Public Functions

| Function | Change |
|----------|--------|
| `pat_eraseStep()` | Now frees pool block before writing sentinel |
| `pat_clearTrack()` | Now walks address array and frees allocated blocks |
| `pat_applyStepToMenu()` | Now reads real pool data via `pat_blockRead` instead of returning defaults |
| `pat_setStepNote()` | Real read-modify-write: reads current specials, updates note, calls `pat_writeSpecials` |
| `pat_setStepVolume()` | Same pattern as setStepNote for velocity |
| `pat_setStepProbability()` | Same pattern as setStepNote for probability |

### 2.4 New Public Function

`pat_readStepSpecials(scene_index, track, step)` → `pat_step_specials_t`

Returns resolved values: absent specials are filled with defaults
(PAT_DEFAULT_NOTE=63, PAT_DEFAULT_VELOCITY=100, probability=127). The
`flags` field reports which values were explicitly stored. Used by both the
Sequencer playback path and the step-edit menu display.

---

## 3. Sequencer Integration

### 3.1 seq_advanceTrackStep Trigger Block

At the trigger decision point (lines ~394–408 of sequencer.c):

1. Read `pat_step_specials_t sp = pat_readStepSpecials(scene, track, step)`
2. Evaluate probability: if `sp.probability < 127`, generate
   `GetRngValue()` (hardware RNG, int16_t), compute
   `rnd = (rng & 0x7FFF) * 127 / 32767`, suppress trigger if
   `rnd >= sp.probability`
3. Use explicit `should_trigger` boolean (not goto) for clarity
4. Trigger with `sp.velocity` and `sp.note` instead of hardcoded defaults

Roll triggers remain independent fixed-note events and are not routed
through the specials path.

### 3.2 Dependencies Added

- `#include "random.h"` added to sequencer.c for `GetRngValue()`
- `#include "menu.h"` added to PatternData.c for `parameter_values[]` access

---

## 4. Menu Bridge

### 4.1 Step-Edit Page Entry

`menu_showStepEditPage()` (added in Step B½):
1. Sets `menuIndex` to subpage 1 (the step-edit page)
2. Calls `pat_applyStepToMenu()` to populate parameter_values with current
   step specials
3. Calls `menu_endlessPotMappingChanged()` for encoder rebase
4. Calls `menu_repaintAll()` for full display refresh

### 4.2 Parameter Routing

The existing menu dispatch at lines ~9751–9776 of menu.c routes
PAR_STEP_VOLUME, PAR_STEP_NOTE, and PAR_STEP_PROB through the
pat_setStep* functions, which now perform real pool read-modify-write.
No menu.c changes were needed for Step C/D.

### 4.3 Step-Edit Menu Page Layout

Defined in menuPages.h subpage 1:
- PAR_STEP_VOLUME (velocity)
- PAR_STEP_NOTE (note override)
- PAR_STEP_PROB (probability)
- PAR_P1_DEST, PAR_P1_VAL (automation slot 1 — future)
- PAR_P2_DEST, PAR_P2_VAL (automation slot 2 — future)

Parameter indices: PAR_ACTIVE_STEP=80, PAR_STEP_VOLUME=81, PAR_STEP_PROB=82,
PAR_STEP_NOTE=83.

---

## 5. Filesystem Bridge Status

### 5.1 Current State (Disconnected)

- Boot reader: `pat_readFromFile()` returns 0 (no pattern data loaded)
- Save writer: writes empty discard `PatternSet` (112 bytes) through
  `filesystem_pattern_discard`
- Fan-out: disconnected — v3 bridge bits are not synchronized with the
  dynamic address array

### 5.2 PatternSet Retention

`PatternSet` (112-byte `step_on[7][16]` bitmap) is retained only for the v3
file bridge in `storageTypes.c`. One instance lives as
`filesystem_pattern_discard` in `filesystem.c`. It is never live Scene data.
`scene_t` contains only `{ scene_settings_t settings; kit_t kit; }` — no
PatternSet field.

---

## 6. Build and Link Details

### 6.1 Final Link Output

| Metric | Value |
|--------|-------|
| text | 408,220 B (+1,776 from Step B½ baseline 406,444) |
| data | 404 B (unchanged) |
| bss | 262,468 B (unchanged from B½) |
| Image | 408,640 B |
| SHA-256 | `412ca5b21509e1e2a787d7f82f15a4946f0c92489eb3ed0f422056c87f58eee9` |

### 6.2 Key Symbol Sizes

| Symbol | Size |
|--------|-----:|
| `pat_regions` | 167,936 B (0x29000) |
| `scenes` | 19,200 B (down from 20,992 after PatternSet removal) |
| `filesystem_pattern_discard` | 112 B |

---

## 7. Quality Improvements Beyond Schedule

Code review found six implementation quality improvements that went beyond
the original C-E schedule:

1. `pat_poolOffsetValid()` — hardening helper validates sentinel, 4-byte
   alignment, and pool range
2. `should_trigger` explicit boolean — clearer than the schedule's goto
   pattern
3. All read paths return `pat_step_specials_t` with resolved defaults — no
   caller needs to handle missing data
4. `pat_writeSpecials` graceful degradation — alloc failure retains existing
   data instead of corrupting
5. `pat_eraseStep` frees pool before sentinel write — correct resource
   cleanup order
6. `pat_clearTrack` walks and frees all allocated blocks — no pool leaks on
   track clear

---

## 8. Deferred Work

### 8.1 Phase 4.5 — Copy Operations

`pat_copyTrack`, `pat_copyPattern`, and `pat_copyBar` are deliberate no-ops.
They must duplicate pool blocks (alloc + memcpy) in the destination Scene's
pool to preserve independence. This is deferred until the copy-operations
design session.

### 8.2 v4 Pattern File Format

The current v3 bridge persists only trigger bits (112-byte bitmap). A v4
format must serialize the address array, pool blocks, and free bitmap to
preserve note/velocity/probability across Scene/Bank save/load cycles.

### 8.3 Automation

The pool block header reserves a 6-bit automation count (bits 5..0) and
special-flags bits 3..7 are reserved. Step automation destinations and
values currently remain no-op storage-free compatibility shims.

### 8.4 Pattern Copy During Scene/Bank Load

Scene Load currently reads pattern directly into final resident SRAM. When
copy operations are implemented, Scene-to-Scene pattern duplication will need
pool-aware copying rather than memcpy.

---

## 9. Files Modified This Session

| File | Nature | Lines |
|------|--------|------:|
| `Core/Bank/Scene/Pattern/PatternData.h` | Major rewrite | 187 |
| `Core/Bank/Scene/Pattern/PatternData.c` | Major expansion | ~800 |
| `Core/Sequencer/sequencer.c` | Targeted integration | ~15 lines changed |
| `config.h` | Three defines added | 3 |
| `Core/Menu/menu.c` | Step B½ fix only (prior context) | ~10 lines |
| `Core/Menu/menu.h` | menu_showStepEditPage declaration | 1 |

No other files were modified.

---

## 10. Session Timeline

1. **Prior context window**: Steps A (architecture), B (address array +
   structures), B½ (step-edit menu fix — two iterations)
2. **This context**: Hardware confirmation of step-edit menu fix; C-E
   implementation schedule creation; user implemented all code changes;
   code review verification (13 entries, no issues, 6 quality improvements);
   hardware testing (all passed); session closeout
