# S062 — Steps A–B½ Implementation Schedule

**Scope:** Static address array allocation, trigger-bitmap port, hardware
checkpoint. No dynamic pool allocator, no specials read/write, no
automation.

**Prerequisite reading:** `S062_DYNAMIC_PATTERN.md` for memory budget,
architectural decisions, and resolved ambiguities.

**Notation:** Each change entry lists file, line, operation
(ADD / REMOVE / MODIFY), and a description block structured as:
what / why / inputs / outputs / affiliates. These descriptions are
sized and worded for use as adjacent comment-block text in the
corresponding `.c` and `.h` files.

---

## 1. config.h — ADD pattern storage defines

**Location:** after line 232 (`#define SEQ_DEFAULT_NOTE 63`)

**Operation:** ADD

```c
#define PAT_STACK_SIZE           256
#define PAT_DEFAULT_NOTE          63
#define PAT_DEFAULT_VELOCITY     100
```

**Description:**

`PAT_STACK_SIZE` controls the per-Scene dynamic pool allocation in
bitmap-tracking units. Pool bytes per Scene = `PAT_STACK_SIZE × 32`.
The free-tracking bitmap is always 512 bytes (4,096 bits); chunks
beyond index `PAT_STACK_SIZE × 8 − 1` are permanently marked occupied.
To double the pool to the full 14-bit capacity in a future session, set
this to 512 with no structural changes.

`PAT_DEFAULT_NOTE` is the MIDI note used by `seq_advanceTrackStep()` when
a triggered step has no note special assigned. `PAT_DEFAULT_VELOCITY` is
the velocity used when no velocity special is assigned. Both are global
defaults, not per-track. These numerically match the existing
`MIDI_DEFAULT_TRIGGER_NOTE` (63) and `ROLL_VOLUME` (100) but represent
pattern-storage ownership rather than sequencer constants.

Why: all pool sizing must derive from one tunable define so a later
session can expand the pool by changing a single line. Default note and
velocity belong to the pattern module, not the sequencer.

Inputs: compile-time only. Outputs: `PatternData.c` derives region
struct size; `sequencer.c` uses the defaults at trigger time.

Affiliates: `PatternData.c` region sizing, bitmap init,
`seq_advanceTrackStep()`.

---

## 2. Core/Bank/Scene/Pattern/PatternData.h — ADD address-array defines, KEEP PatternSet

### 2.1 ADD address-entry constants — after line 19 (`PATTERN_TRACK_BYTES`)

**Operation:** ADD

```c
#define PAT_ADDR_SENTINEL     0x3FFFu
#define PAT_ADDR_TRIGGER_BIT  (1u << 15)
#define PAT_ADDR_SPECIALS_BIT (1u << 14)
#define PAT_ADDR_OFFSET_MASK  0x3FFFu
```

**Description:**

Address-entry bit-field constants for the 16-bit per-step entries in the
pattern address array. Each `uint16_t` entry encodes trigger on/off (bit
15), has-specials (bit 14), and a 14-bit pool byte offset (bits 13–0).
`PAT_ADDR_SENTINEL` (`0x3FFF`) is the reserved "no lookup" value; it is
inherently invalid as a pool byte offset because valid offsets are
always multiples of 4.

Why: named constants prevent magic numbers in every address-entry reader
and writer across PatternData, Sequencer, and future pool code.

Inputs: compile-time only. Outputs: used by `pat_isStepActive()`,
`pat_toggleStep()`, `pat_setStepActive()`, `pat_eraseStep()`,
`pat_clearTrack()`, `pat_clearPattern()`, and `seq_advanceTrackStep()`.

Affiliates: `PatternData.c` implementation, `sequencer.c` playback path
(Step D), `S062_DYNAMIC_PATTERN.md` Section 4.2.

### 2.2 ADD step-count define — after the address-entry constants

**Operation:** ADD

```c
#define PAT_STEPS_PER_SCENE   (NUM_TRACKS * NUM_STEPS)
```

**Description:**

Total address-entry count per Scene: 7 tracks × 128 steps = 896. Used
for address-array sizing and full-scene scans.

### 2.3 KEEP PatternSet, _Static_assert, pat_patternSetGetStep, pat_patternSetSetStep, pat_initPatternSet

**Operation:** NO CHANGE

The `PatternSet` typedef (lines 30–32), its `_Static_assert` (lines
34–35), and the three helpers `pat_patternSetGetStep` (line 48),
`pat_patternSetSetStep` (line 50), and `pat_initPatternSet` (line 59)
remain unchanged. They are still used by the v3 `pattern.pat`
parser/writer in `storageTypes.c` and by the filesystem's discard
PatternSet bridge (see Section 6 below). Removing them requires
touching the file-format code, which is out of scope.

### 2.4 KEEP all Scene-indexed API declarations

**Operation:** NO CHANGE to signatures

All existing `pat_` function declarations (lines 60–106) retain their
current signatures. Only internal implementations change.

---

## 3. Core/Bank/Scene/Pattern/PatternData.c — MODIFY implementations

### 3.1 ADD include — line 1 area

**Operation:** ADD after `#include "BankData.h"` (line 9)

```c
#include "config.h"
```

**Description:**

Needed for `PAT_STACK_SIZE`, `PAT_DEFAULT_NOTE`, `PAT_DEFAULT_VELOCITY`.
These are compile-time constants used for region sizing, bitmap init,
and (in Step D) default value resolution.

### 3.2 ADD per-scene pattern memory region — after includes

**Operation:** ADD

```c
typedef struct {
    uint16_t address[NUM_TRACKS][NUM_STEPS];
    uint8_t  pool[PAT_STACK_SIZE * 32u];
    uint8_t  bitmap[512];
} pat_scene_region_t;

_Static_assert(sizeof(pat_scene_region_t) ==
               (NUM_TRACKS * NUM_STEPS * 2u) +
               (PAT_STACK_SIZE * 32u) + 512u,
               "pat_scene_region_t size must match budget");

static pat_scene_region_t pat_regions[SCENE_COUNT];
```

**Description:**

Static SRAM1 allocation for 16 Scenes of pattern data. Each region
contains: a 1,792-byte address array (896 × 2-byte entries), a
configurable dynamic pool (`PAT_STACK_SIZE × 32` bytes), and a
512-byte free-tracking bitmap. The array is placed in `.bss` (SRAM1)
and zero-initialized by the startup code. `pat_initScene()` writes
the correct initial state (sentinel entries, bitmap occupied mask).

Why: `scene_t.pattern` (the embedded 112-byte `PatternSet`) is removed
from `SceneData.h`. The pattern module owns its own permanent memory,
indexed by scene. Every `pat_` function already takes `scene_index`;
internally it computes a pointer to `pat_regions[scene_index]`. No
pointer or index is stored in `scene_t`. This gives clean ownership
separation: `scene_t` holds settings and kit; the pattern module holds
its own allocations.

Inputs: `SCENE_COUNT` (16), `NUM_TRACKS` (7), `NUM_STEPS` (128),
`PAT_STACK_SIZE` (256). Outputs: 167,936 bytes total in SRAM1. The
`_Static_assert` catches any struct-packing surprise at compile time.

Affiliates: `S062_DYNAMIC_PATTERN.md` Section 3 (memory budget),
Section 4.6 (allocation topology). `SceneData.h` removes
`PatternSet pattern` from `scene_t` (Section 4 below). SRAM manifest
updated in Step B½.

### 3.3 ADD internal address-entry accessor

**Operation:** ADD (static helper)

```c
static uint16_t *pat_addrPtr(uint8_t scene_index, uint8_t track,
                             uint8_t step)
{
    if (!scene_indexValid(scene_index) || !pat_trackValid(track) ||
        !pat_stepValid(step))
        return NULL;
    return &pat_regions[scene_index].address[track][step];
}
```

**Description:**

Bounds-checked accessor for one address-entry slot. Returns a mutable
pointer to the 16-bit entry so callers can read or write atomically
(Cortex-M7 `LDRH`/`STRH` on a 2-byte aligned `uint16_t`).

Why: centralizes bounds checking and region indexing. Invalid
coordinates return NULL; every caller checks before dereferencing.

Inputs: scene_index, track, step. Output: pointer to the `uint16_t`
entry or NULL. Affiliates: every `pat_` function below.

### 3.4 MODIFY pat_initScene — replace line 82–95

**Current code (lines 82–95):**
```c
void pat_initScene(uint8_t scene_index)
{
    scene_t *scene = scene_get(scene_index);
    if (scene)
        pat_initPatternSet(&scene->pattern);
}
```

**New code:**
```c
void pat_initScene(uint8_t scene_index)
{
    pat_scene_region_t *r;
    uint8_t t;
    uint16_t s;

    if (!scene_indexValid(scene_index))
        return;
    r = &pat_regions[scene_index];

    for (t = 0u; t < NUM_TRACKS; t++)
        for (s = 0u; s < NUM_STEPS; s++)
            r->address[t][s] = PAT_ADDR_SENTINEL;

    memset(r->pool, 0, sizeof(r->pool));

    memset(r->bitmap, 0x00, PAT_STACK_SIZE);
    memset(r->bitmap + PAT_STACK_SIZE, 0xFF,
           512u - PAT_STACK_SIZE);
}
```

**Description:**

Initialize one Scene's pattern region to the empty state. The address
array is filled with `PAT_ADDR_SENTINEL` (0x3FFF): bit 15 clear (off),
bit 14 clear (no specials), offset = sentinel (no lookup). The pool is
zeroed. The bitmap marks the lower `PAT_STACK_SIZE × 8` chunks as free
(0) and all chunks beyond as permanently occupied (1). At
`PAT_STACK_SIZE 256`, the first 256 bitmap bytes are 0x00 and the
remaining 256 are 0xFF — a clean byte-aligned split.

Why: `scene_t` no longer contains `PatternSet`; the pattern module
owns its own memory. The previous implementation called
`pat_initPatternSet(&scene->pattern)` which no longer applies. Bitmap
init here ensures the pool boundary is established even though the pool
allocator is not yet wired (Step C).

Inputs: `scene_index`, validated against `SCENE_COUNT`. Outputs: one
Scene's address array, pool, and bitmap are in the defined initial
state. Callers: `scene_initAll()` at boot, `filesystem_bootReaderEmptyScene()` for Case-3 reset, `filesystem.c` Scene Load commit
(replacing `pat_initPatternSet(&target->pattern)`).

Affiliates: `SceneData.c:601` (`scene_initAll`), `filesystem.c:27460`
(boot reader empty scene), `filesystem.c:15253` (scene load commit).

### 3.5 MODIFY pat_isStepActive — replace lines 97–103

**Current code:**
```c
uint8_t pat_isStepActive(uint8_t track, uint8_t step, uint8_t scene_index)
{
    const scene_t *scene = scene_getConst(scene_index);
    return scene ? pat_patternSetGetStep(&scene->pattern, track, step) : 0u;
}
```

**New code:**
```c
uint8_t pat_isStepActive(uint8_t track, uint8_t step, uint8_t scene_index)
{
    const uint16_t *entry = pat_addrPtr(scene_index, track, step);
    return entry ? (uint8_t)((*entry >> 15u) & 1u) : 0u;
}
```

**Description:**

Read the on/off trigger state from bit 15 of the address entry. Returns
1 when the step is active, 0 otherwise. Invalid coordinates return 0.

Why: trigger state has moved from `PatternSet.step_on` bitmap bits to
bit 15 of the address array. The public API is unchanged; only the
storage it reads from has changed.

Inputs: track, step, scene_index. Output: 0 or 1. Callers: sequencer
playback (`seq_advanceTrackStep`), LED display
(`led_updatePatternTrackView`, `led_updateRecordedMainStep`), button
handler (`buttonHandler_setRemoveStep`), menu PERF LED display.

Affiliates: `sequencer.c:379`, `ledHandler.c:1052/1125`,
`buttonHandler.c:504`, `menu.c:4890`.

### 3.6 MODIFY pat_setStepActive — replace lines 105–128

**Current code:**
```c
void pat_setStepActive(uint8_t scene_index, uint8_t track, uint8_t step,
                       uint8_t on)
{
    scene_t *scene = scene_get(scene_index);
    if (scene) {
        (void)pat_patternSetSetStep(&scene->pattern, track, step, on);
        bank_invalidateSdCleanScene(scene_index);
    }
}
```

**New code:**
```c
void pat_setStepActive(uint8_t scene_index, uint8_t track, uint8_t step,
                       uint8_t on)
{
    uint16_t *entry = pat_addrPtr(scene_index, track, step);
    if (!entry)
        return;
    if (on)
        *entry |= PAT_ADDR_TRIGGER_BIT;
    else
        *entry &= (uint16_t)~PAT_ADDR_TRIGGER_BIT;
    bank_invalidateSdCleanScene(scene_index);
}
```

**Description:**

Set or clear bit 15 (trigger on/off) in the address entry while
preserving bits 14–0 (specials flag and pool byte offset). A step that
has specials assigned retains its pool reference when toggled off; the
specials survive an on → off → on cycle.

Why: the trigger bit has moved from `PatternSet.step_on` to the address
entry's bit 15. The `bank_invalidateSdCleanScene` call is retained so a
live pattern edit still invalidates the Scene's card-clean bit.

Inputs: scene_index, track, step, on (0 or nonzero). Output: one
address entry bit is updated. Callers: `seq_recordTrigger` (recording),
`EuklidGenerator` (generator output), button handler (toggle path
delegates through `pat_toggleStep`), `pat_eraseStep` (live erase).

Affiliates: `sequencer.c:877`, `EuklidGenerator.c:291`,
`BankData.c` card-clean tracking.

### 3.7 MODIFY pat_toggleStep — replace lines 130–142

**Current code:**
```c
void pat_toggleStep(uint8_t track, uint8_t step, uint8_t scene_index)
{
    scene_t *scene = scene_get(scene_index);
    if (scene && pat_trackValid(track) && pat_stepValid(step)) {
        (void)pat_patternSetSetStep(&scene->pattern, track, step,
                                    (uint8_t)!pat_patternSetGetStep(
                                        &scene->pattern, track, step));
        bank_invalidateSdCleanScene(scene_index);
    }
}
```

**New code:**
```c
void pat_toggleStep(uint8_t track, uint8_t step, uint8_t scene_index)
{
    uint16_t *entry = pat_addrPtr(scene_index, track, step);
    if (!entry)
        return;
    *entry ^= PAT_ADDR_TRIGGER_BIT;
    bank_invalidateSdCleanScene(scene_index);
}
```

**Description:**

XOR bit 15 of the address entry to toggle trigger state. Bits 14–0 are
preserved: toggling a step off does not free its pool block, and
toggling back on does not reallocate.

Why: bit-toggle is a single XOR instruction on the address entry,
replacing the read-modify-write through two PatternSet helper calls.
The `bank_invalidateSdCleanScene` call is retained.

Inputs: track, step, scene_index. Output: bit 15 of one address entry
is flipped. Caller: `buttonHandler_setRemoveStep` (line 503 of
`buttonHandler.c`).

Affiliates: `buttonHandler.c:503`.

### 3.8 MODIFY pat_eraseStep — replace lines 144–148

**Current code:**
```c
void pat_eraseStep(uint8_t scene_index, uint8_t track, uint8_t step)
{
    pat_setStepActive(scene_index, track, step, 0u);
}
```

**New code:**
```c
void pat_eraseStep(uint8_t scene_index, uint8_t track, uint8_t step)
{
    uint16_t *entry = pat_addrPtr(scene_index, track, step);
    if (!entry)
        return;
    *entry = PAT_ADDR_SENTINEL;
    bank_invalidateSdCleanScene(scene_index);
}
```

**Description:**

Erase a step completely: clear trigger (bit 15), clear specials flag
(bit 14), and set the offset to sentinel (`0x3FFF`). In Step D this
will also free the pool block before writing the sentinel; for Step B
there are no pool blocks to free.

Why: live erase (sequencer erase mode) should return the entry to the
fully empty state, not merely clear the trigger bit. A cleared trigger
with a dangling pool offset is a valid "off with data" state (specials
survive toggle); erase is the destructive path.

Inputs: scene_index, track, step. Output: one address entry is
`PAT_ADDR_SENTINEL`. Caller: `seq_advanceTrackStep` during erase mode
(line 381 of `sequencer.c`).

Affiliates: `sequencer.c:381`.

### 3.9 MODIFY pat_sceneHasActiveSteps — replace lines 150–170

**Current code:**
```c
uint8_t pat_sceneHasActiveSteps(uint8_t scene_index)
{
    const scene_t *scene = scene_getConst(scene_index);
    uint8_t track;
    uint8_t byte;
    if (!scene)
        return 0u;
    for (track = 0u; track < NUM_TRACKS; track++)
        for (byte = 0u; byte < PATTERN_TRACK_BYTES; byte++)
            if (scene->pattern.step_on[track][byte])
                return 1u;
    return 0u;
}
```

**New code:**
```c
uint8_t pat_sceneHasActiveSteps(uint8_t scene_index)
{
    uint8_t track;
    uint16_t step;
    if (!scene_indexValid(scene_index))
        return 0u;
    for (track = 0u; track < NUM_TRACKS; track++)
        for (step = 0u; step < NUM_STEPS; step++)
            if (pat_regions[scene_index].address[track][step] &
                PAT_ADDR_TRIGGER_BIT)
                return 1u;
    return 0u;
}
```

**Description:**

Scan the address array for any entry with bit 15 set. Returns nonzero
at the first active step. This replaces the byte-scan of
`PatternSet.step_on`.

Why: trigger state is now in bit 15 of address entries. The scan covers
all 896 entries (7 tracks × 128 steps). Early exit on the first hit
keeps this fast for the common case (some steps active).

Inputs: scene_index. Output: 0 or 1. Caller: menu PERF LED display
(`menu.c:4890`).

Affiliates: `menu.c:4890`.

### 3.10 MODIFY pat_clearTrack — replace lines 172–182

**Current code:**
```c
void pat_clearTrack(uint8_t scene_index, uint8_t track)
{
    scene_t *scene = scene_get(scene_index);
    if (scene && pat_trackValid(track)) {
        memset(scene->pattern.step_on[track], 0, PATTERN_TRACK_BYTES);
        bank_invalidateSdCleanScene(scene_index);
    }
}
```

**New code:**
```c
void pat_clearTrack(uint8_t scene_index, uint8_t track)
{
    uint16_t step;
    if (!scene_indexValid(scene_index) || !pat_trackValid(track))
        return;
    for (step = 0u; step < NUM_STEPS; step++)
        pat_regions[scene_index].address[track][step] = PAT_ADDR_SENTINEL;
    bank_invalidateSdCleanScene(scene_index);
}
```

**Description:**

Clear one track's 128 address entries to sentinel. In Step D this will
also free any pool blocks referenced by those entries and reclaim their
bitmap chunks. For Step B there are no pool blocks.

Why: a track-clear from copyClearTools or EuklidGenerator must reset
every entry in the track to the fully empty state.

Inputs: scene_index, track. Output: 128 address entries set to
`PAT_ADDR_SENTINEL`. Callers: `copyClearTools.c:126`
(`copyClear_clearCurrentTrack`), `EuklidGenerator.c:283`
(`euklid_generate`).

Affiliates: `copyClearTools.c:126`, `EuklidGenerator.c:283`.

### 3.11 MODIFY pat_clearPattern — replace lines 184–194

**Current code:**
```c
void pat_clearPattern(uint8_t scene_index)
{
    scene_t *scene = scene_get(scene_index);
    if (scene) {
        pat_initPatternSet(&scene->pattern);
        bank_invalidateSdCleanScene(scene_index);
    }
}
```

**New code:**
```c
void pat_clearPattern(uint8_t scene_index)
{
    if (!scene_indexValid(scene_index))
        return;
    pat_initScene(scene_index);
    bank_invalidateSdCleanScene(scene_index);
}
```

**Description:**

Clear the entire pattern region for one Scene by re-initializing it to
the empty state. This resets all address entries to sentinel, zeroes the
pool, and restores the bitmap to its initial free/occupied partition.

Why: pattern-clear from copyClearTools must reset all pattern data.
Delegating to `pat_initScene()` ensures the bitmap and pool are also
reset, not just the address array.

Inputs: scene_index. Output: one Scene's pattern region is in the
defined initial state. Caller: `copyClearTools.c:63`
(`copyClear_clearCurrentPattern`).

Affiliates: `copyClearTools.c:63`.

### 3.12 MODIFY pat_copyTrack — replace lines 196–207

**Current code:**
```c
void pat_copyTrack(uint8_t scene_index, uint8_t src_track, uint8_t dst_track)
{
    scene_t *scene = scene_get(scene_index);
    if (scene && pat_trackValid(src_track) && pat_trackValid(dst_track)) {
        memcpy(scene->pattern.step_on[dst_track],
               scene->pattern.step_on[src_track], PATTERN_TRACK_BYTES);
        bank_invalidateSdCleanScene(scene_index);
    }
}
```

**New code:**
```c
void pat_copyTrack(uint8_t scene_index, uint8_t src_track, uint8_t dst_track)
{
    (void)scene_index;
    (void)src_track;
    (void)dst_track;
}
```

**Description:**

Stubbed as a no-op for this session. Pool block duplication during
track copy requires the allocator (Step C) and a duplication strategy
for shared vs. independent block ownership. Analysis is deferred to
SCOPING_TARGETS Phase 4.5.

Caller: `copyClearTools.c:153` (`copyClear_copyTrack`). The menu copy
gesture completes without error but produces no data change.

### 3.13 MODIFY pat_copyPattern — replace lines 209–221

**Current code:**
```c
void pat_copyPattern(uint8_t src_scene, uint8_t dst_scene)
{
    scene_t *src = scene_get(src_scene);
    scene_t *dst = scene_get(dst_scene);
    if (src && dst) {
        memcpy(&dst->pattern, &src->pattern, sizeof(dst->pattern));
        bank_invalidateSdCleanScene(dst_scene);
    }
}
```

**New code:**
```c
void pat_copyPattern(uint8_t src_scene, uint8_t dst_scene)
{
    (void)src_scene;
    (void)dst_scene;
}
```

**Description:**

Stubbed as a no-op for this session. Cross-Scene pattern copy requires
duplicating the source Scene's entire address array and all referenced
pool blocks into the destination Scene's pool. Deferred to
SCOPING_TARGETS Phase 4.5.

Caller: `copyClearTools.c:179` (`copyClear_copyPattern`).

### 3.14 MODIFY pat_copyBar — replace lines 223–246

**Current code:**
```c
void pat_copyBar(uint8_t scene_index, uint8_t track, uint8_t src_bar,
                 uint8_t dst_bar)
{
    scene_t *scene = scene_get(scene_index);
    uint8_t src_byte;
    uint8_t dst_byte;
    if (!scene || !pat_trackValid(track) || src_bar >= NUM_BARS ||
        dst_bar >= NUM_BARS)
        return;
    src_byte = (uint8_t)(src_bar * 2u);
    dst_byte = (uint8_t)(dst_bar * 2u);
    memcpy(&scene->pattern.step_on[track][dst_byte],
           &scene->pattern.step_on[track][src_byte], 2u);
    bank_invalidateSdCleanScene(scene_index);
}
```

**New code:**
```c
void pat_copyBar(uint8_t scene_index, uint8_t track, uint8_t src_bar,
                 uint8_t dst_bar)
{
    (void)scene_index;
    (void)track;
    (void)src_bar;
    (void)dst_bar;
}
```

**Description:**

Stubbed as a no-op for this session. Bar copy requires duplicating
address entries and their referenced pool blocks for 16 steps. Deferred
to SCOPING_TARGETS Phase 4.5.

Caller: `copyClearTools.c:199` (`copyClear_copyBar`).

### 3.15 NO CHANGE to PatternSet helpers or legacy stubs

The following remain unchanged:

- `pat_patternSetGetStep` (lines 28–42) — used by `storageTypes.c` v3
  parser
- `pat_patternSetSetStep` (lines 44–67) — used by `storageTypes.c` v3
  parser
- `pat_initPatternSet` (lines 69–80) — used by filesystem discard
  bridge
- All legacy menu bridge stubs (lines 248–263) — `pat_applyStepToMenu`,
  `pat_setStepProbability`, `pat_setStepNote`, `pat_setStepVolume`, etc.
  These become real implementations in Step D.

### 3.16 REMOVE scene_get/scene_getConst dependencies

The modified functions no longer call `scene_get()` or
`scene_getConst()` for pattern data. They access `pat_regions[]`
directly via `pat_addrPtr()` or by indexing. `scene_indexValid()` is
used for bounds checking (already included via `SceneData.h`).

`scene_get()` is still called by `pat_setStepActive()` — no: actually
it is NOT called anymore, since we access `pat_regions` directly. But
`bank_invalidateSdCleanScene()` still needs the scene_index only.
The `#include "SceneData.h"` stays for `scene_indexValid()` and
`SCENE_COUNT`. The `#include "BankData.h"` stays for
`bank_invalidateSdCleanScene()`.

---

## 4. Core/Bank/Scene/SceneData.h — REMOVE PatternSet from scene_t

### 4.1 MODIFY scene_t struct — line 225

**Current code (line 225 in context):**
```c
     */
    PatternSet pattern;
    kit_t kit;
} scene_t;
```

**New code:**
```c
     */
    kit_t kit;
} scene_t;
```

**Description:**

Remove the embedded 112-byte `PatternSet` from `scene_t`. The pattern
module now owns its own static memory array (`pat_regions[]` in
`PatternData.c`), indexed by scene. Every `pat_` function already
takes `scene_index` and no longer dereferences `scene->pattern`.

Why: `PatternSet` was a 112-byte bitmap that stored only trigger on/off
bits. The address array (1,792 bytes, plus pool and bitmap) is too
large to embed in `scene_t` and is logically owned by the pattern
module, not by SceneData. Removing the field shrinks each `scene_t` by
112 bytes (total 1,792 bytes across 16 Scenes), reclaimed by the new
allocation.

Inputs: compile-time struct change. Outputs: `sizeof(scene_t)` shrinks
by 112 bytes. The `memset(scenes, 0, sizeof(scenes))` in
`scene_initAll()` zeroes the smaller struct correctly. Pattern
initialization continues through `pat_initScene()` which writes to
`pat_regions[]`.

Affiliates: `SceneData.c:579` (`memset`), `SceneData.c:601`
(`pat_initScene`), `filesystem.c` (multiple `&scene->pattern`
references updated below).

### 4.2 MODIFY scene_t comment — lines 198–211

**Operation:** MODIFY

Update the `scene_t` struct comment to remove references to
"PatternSet" and "current bridge PatternSet". Replace the phrase
"pattern holds the current bridge PatternSet" with text explaining that
pattern data is owned separately by the pattern module.

**New comment paragraph (replacing line 199):**

```
 * settings holds Scene-level performance/settings data and kit holds
 * the embedded six-slot Kit. Pattern data is owned by PatternData.c's
 * static memory array, indexed by scene; it is not embedded in scene_t.
```

---

## 5. Core/Bank/Scene/SceneData.c — NO DIRECT CHANGES

`scene_initAll()` (line 563) continues to call `pat_initScene()` at
line 601, which now writes to `pat_regions[]` instead of
`scene->pattern`. The `memset(scenes, 0, sizeof(scenes))` at line 579
zeroes the now-smaller `scene_t` records. No code change required.

---

## 6. Core/Hardware/SD/filesystem.c — Bridge `scene->pattern` removal

The filesystem code has multiple references to `scene->pattern` and
`target->pattern` that must be updated. The strategy is to add a static
discard `PatternSet` that absorbs all legacy pattern file I/O without
connecting it to the new address arrays.

### 6.1 ADD discard PatternSet — near existing discard records

**Location:** Add near the existing `filesystem_discardStep` etc.
declarations. These are inside the `#if 0` block (line 2732+), so the
new discard must be placed OUTSIDE the `#if 0` — in the active code
region. Find the `#endif` that closes the retired binary bridge block
and add after it, or add as a file-scope static near the top of the
active pattern code.

**Operation:** ADD

```c
static PatternSet filesystem_pattern_discard;
```

**Description:**

A module-local zeroed `PatternSet` that serves as the parse/write
target for all legacy pattern.pat file I/O while the v3 bridge is
disconnected. Parsers write into it (data is ignored); the writer reads
from it (producing empty pattern files). This isolates the filesystem
from the structural change in `scene_t` without modifying the file
format code in `storageTypes.c`.

### 6.2 MODIFY filesystem_directPatternTarget — lines 15267–15289

**Current code:**
```c
static PatternSet *filesystem_directPatternTarget(void)
{
    uint8_t scene_index;
    for (scene_index = 0u;
         scene_index < SCENE_COUNT && scene_index < 16u;
         scene_index++) {
        if ((op_scene_load_scene_mask & (uint16_t)(1u << scene_index)) != 0u) {
            scene_t *target = scene_get(scene_index);
            if (target)
                return &target->pattern;
        }
    }
    return NULL;
}
```

**New code:**
```c
static PatternSet *filesystem_directPatternTarget(void)
{
    pat_initPatternSet(&filesystem_pattern_discard);
    return &filesystem_pattern_discard;
}
```

**Description:**

Returns the discard PatternSet instead of a Scene's removed `.pattern`
field. The discard is zeroed before return so any parser phase that
writes into it starts from a known state.

Why: `scene_t` no longer has a `.pattern` field. Pattern file data
parsed during Scene Load goes into the discard and is never applied
to the new address arrays. The v3 parser phases that call this function
(lines 12314, 12348, 12381, 12416, 12448, 12483, 12533 — all inside
`#if 0` retired binary phases, so actually only the v3 text path uses
it via `storage_patternStubParseLine`) continue to compile and run
harmlessly.

### 6.3 MODIFY Scene Load pattern fan-out — lines 12671–12681

**Current code:**
```c
        PatternSet *direct = filesystem_directPatternTarget();
        for (scene_index = 0u;
             scene_index < SCENE_COUNT && scene_index < 16u;
             scene_index++) {
            if ((op_scene_load_scene_mask &
                 (uint16_t)(1u << scene_index)) != 0u) {
                scene_t *target = scene_get(scene_index);
                if (target && direct && &target->pattern != direct)
                    target->pattern = *direct;
            }
        }
```

**New code:**
```c
        /* Pattern fan-out disconnected: pattern data is not loaded from
         * files this session. Each selected Scene's pattern region was
         * initialized by pat_initScene() during the settings commit
         * phase (filesystem_commitStagedScene). */
```

**Description:**

Remove the pattern fan-out loop. It previously copied the first
target's parsed `PatternSet` to all other selected Scenes. Since
`scene_t.pattern` no longer exists and `pat_initScene()` is called
during the commit phase for each selected Scene, pattern data for all
targets is already in the correct initial state.

Why: the pattern fan-out directly assigned `target->pattern` which is
a removed field. All selected Scenes were already initialized by the
commit phase change below (Section 6.4), so no replacement logic is
needed.

### 6.4 MODIFY Scene Load settings commit — line 15253

**Current code:**
```c
        pat_initPatternSet(&target->pattern);
```

**New code:**
```c
        pat_initScene(scene_index);
```

**Description:**

Initialize the new pattern region for the loaded Scene instead of the
removed embedded `PatternSet`. This writes sentinel entries to the
address array and initializes the bitmap, establishing the correct
empty state for each selected target Scene.

Why: `target->pattern` no longer exists. `pat_initScene()` is the
correct initialization entry point for the new pattern memory.

### 6.5 MODIFY Scene Save pattern writer — line 18377

**Current code:**
```c
        if (filesystem_writeTextLine(filesystem_nextPatternStubLine,
                                     (void *)&scene->pattern))
```

**New code:**
```c
        if (filesystem_writeTextLine(filesystem_nextPatternStubLine,
                                     (void *)&filesystem_pattern_discard))
```

**Description:**

Pass the zeroed discard `PatternSet` to the pattern save writer instead
of the removed `scene->pattern` field. The writer produces a valid but
empty v3 `pattern.pat` file (all tracks zeroed). This preserves the
file-format contract so existing on-card Scene folders remain parseable
by future v4 loaders.

Why: `scene->pattern` no longer exists. Writing empty pattern data is
correct because pattern storage is not yet serialized to files. Future
file format v4 will serialize the address array and pool content.

### 6.6 MODIFY boot reader pattern load — line 27217+

**Current code (function start):**
```c
static uint8_t filesystem_bootReaderLoadPattern(uint8_t scene_index)
{
    uint16_t scene_row = filesystem_residentSceneRow(scene_index);
    ...
```

**New code (function start):**
```c
static uint8_t filesystem_bootReaderLoadPattern(uint8_t scene_index)
{
    (void)scene_index;
    return 0u;
}
```

**Description:**

Disconnect boot-time pattern loading. The function returns 0 (skip)
immediately. Pattern data from `pattern.pat` v3 files is not loaded
into the new address arrays. All Scenes boot with empty patterns.

Why: the v3 file format stores `PatternSet` bitmap data, not address
array entries. Loading v3 data into the new address arrays would
require a format conversion that is out of scope. The boot reader's
caller treats a 0 return as non-fatal (boot continues with an empty
pattern for that Scene).

Inputs: scene_index (consumed). Output: 0 (no pattern loaded).
Affiliates: the boot reader orchestrator step 3b.

### 6.7 NO CHANGE to storageTypes.c or storageTypes.h

The v3 pattern stub parser and writer in `storageTypes.c` (lines
1712–1819) operate on `PatternSet *` parameters through
`pat_patternSetSetStep()` and direct `step_on` field access. The
`PatternSet` type is unchanged; these functions continue to work on the
discard target passed by filesystem. No changes needed.

---

## 7. Core/Sequencer/sequencer.c — MODIFY defaults for Step B½ test

### 7.1 ADD include — line 59 area

**Operation:** ADD after `#include "SceneData.h"` (line 59)

```c
#include "config.h"
```

**Description:**

Needed for `PAT_DEFAULT_NOTE` and `PAT_DEFAULT_VELOCITY`.

### 7.2 MODIFY seq_advanceTrackStep trigger — line 385

**Current code:**
```c
				seq_triggerVoice(track, ROLL_VOLUME, MIDI_DEFAULT_TRIGGER_NOTE);
```

**New code:**
```c
				seq_triggerVoice(track, PAT_DEFAULT_VELOCITY, PAT_DEFAULT_NOTE);
```

**Description:**

Use pattern-module default constants for the trigger call. At this
step, specials are not yet read from the pool; every triggered step
uses `PAT_DEFAULT_VELOCITY` (100) and `PAT_DEFAULT_NOTE` (63). In
Step D, this call site will read specials from the pool and pass the
resolved values instead.

Why: `ROLL_VOLUME` and `MIDI_DEFAULT_TRIGGER_NOTE` are sequencer/MIDI
constants. The values used for pattern playback belong to the pattern
module. Numerically these are identical (100 and 63), so this is an
ownership change, not a behavioral change.

Inputs: track from the scheduler loop. Outputs: trigger with pattern
defaults. Affiliate: `config.h` defines.

### 7.3 MODIFY seq_recordTrigger comment — line 122

**Current code (sequencer.h, lines 118–123):**
```c
/*
 * Record a live MIDI/roll event as one quantized fixed-grid trigger bit.
 * Input is the track; output is an on-bit only when recording is active.
 * Note and velocity are intentionally absent because PatternSet stores neither.
 */
void seq_recordTrigger(uint8_t trackNr);
```

**New code:**
```c
/*
 * Record a live MIDI/roll event as one quantized fixed-grid trigger bit.
 * Input is the track; output is an on-bit only when recording is active.
 * Note and velocity are not recorded here; they are assigned separately
 * as per-step specials through the step editor.
 */
void seq_recordTrigger(uint8_t trackNr);
```

**Description:**

Update the comment to reflect the new pattern storage model. Note and
velocity are no longer "intentionally absent because PatternSet stores
neither"; the address array can carry specials, but live recording
targets only the trigger bit. Per-step note/velocity specials are
assigned through the menu step editor.

---

## 8. Core/Hardware/frontPanel/buttonHandler.c — NO CHANGES

`buttonHandler_setRemoveStep()` (line 503) calls `pat_toggleStep()` and
`pat_isStepActive()`. Both retain their existing signatures. No change.

`buttonHandler_stepEditSelect()` (line 462) calls
`pat_applyStepToMenu()` which remains a no-op stub until Step D. No
change.

---

## 9. Core/Hardware/frontPanel/ledHandler.c — NO CHANGES

`led_updateRecordedMainStep()` (line 1052) and
`led_updatePatternTrackView()` (line 1125) call `pat_isStepActive()`,
`pat_trackValid()`, and `pat_patternValid()`. All signatures unchanged.
No change.

---

## 10. Core/Bank/Scene/Pattern/EuklidGenerator.c — NO CHANGES

`euklid_generate()` (line 283) calls `pat_clearTrack()` and
`pat_setStepActive()`. Both signatures unchanged. Euklid operates on
bit 15 (on/off) only; it does not touch the pool. Specials assigned to
steps before a generator run are cleared by `pat_clearTrack()` writing
sentinel entries (in Step D, this will also free pool blocks). No
change.

---

## 11. Core/Bank/Scene/Pattern/SomGenerator.c — NO CHANGES

`som_tick()` (line 95) calls `seq_triggerVoice()` directly, not any
`pat_` function. It does not read or write pattern data. No change.

---

## 12. Core/Menu/menu.c — NO CHANGES

`pat_sceneHasActiveSteps()` (line 4890), `pat_applyStepToMenu()` (line
9748), `pat_setStepProbability()` (line 9756), `pat_setStepNote()` (line
9765), `pat_setStepVolume()` (line 9774): all signatures unchanged. The
three set-special functions remain stubs until Step D. No change.

---

## 13. Core/Menu/copyClearTools.c — NO CHANGES

`pat_clearPattern()` (line 63), `pat_clearTrack()` (line 126),
`pat_copyTrack()` (line 153), `pat_copyPattern()` (line 179),
`pat_copyBar()` (line 199): all signatures unchanged. Copy functions
are now no-ops (the menu gestures complete silently). Clear functions
work through the new address-array path. No change to this file.

---

## 14. STM32F765VIHx_FLASH.ld — NO CHANGES (VERIFY)

The `pat_regions[16]` array is an uninitialized static in
`PatternData.c`, so it goes into `.bss` which is placed in SRAM1 at
line 122 of the linker script:

```
.bss : { ... *(.bss) *(.bss*) *(COMMON) ... } >SRAM1
```

No linker script changes are needed. The new allocation simply grows
`.bss`. Verify at link time that the total `.bss` + `.data` +
`.dma_nocache` in SRAM1 does not exceed the 368 KB SRAM1 region.

---

## 15. SRAM_MANIFEST.md — UPDATE at Step B½

After the Step B½ firmware links successfully, update the SRAM manifest
with:

- **New SRAM1 static use:** previous 93,044 B + 167,936 B
  (`pat_regions`) − 1,792 B (retired `PatternSet` × 16) = 259,188 B
- **New SRAM1 free:** 376,832 − 259,188 = 117,644 B
- **scene_t size change:** previous size − 112 B per Scene
- **New allocation line:** `pat_regions` array: 167,936 B (16 ×
  10,496 B per Scene)

---

## 16. Step B½ — Hardware checkpoint verification

After all changes compile and link:

1. Flash the firmware.
2. Verify step toggle on/off from SEQ buttons: correct LED state,
   correct playback.
3. Verify playback triggers at note 63 and velocity 100 (the config.h
   defaults — audibly identical to the previous firmware).
4. Verify pattern clear: track clear and whole-pattern clear.
5. Verify Euklid generator: produces expected on/off patterns.
6. Verify SOM generator: produces expected trigger events.
7. Verify Scene switching: each Scene has independent trigger state.
8. Verify `seq_recordTrigger`: live recording sets bit 15 correctly.
9. Verify no regressions in voice editing, Load/Save of non-pattern
   data, AutoSave of non-pattern parameters.
10. Verify linked image size: `.bss` growth matches budget.
11. Update SRAM_MANIFEST.md with final measured sizes.
12. Commit as standalone checkpoint firmware.

---

## Appendix: Summary of operations by file

| File | Operations | Step |
|---|---|---|
| `config.h` | ADD 3 defines | A |
| `PatternData.h` | ADD 5 defines; NO CHANGE to existing API | A |
| `PatternData.c` | ADD region struct + static array + helper; MODIFY 9 functions; KEEP 7 unchanged | A+B |
| `SceneData.h` | REMOVE `PatternSet pattern` from `scene_t`; update comment | A |
| `SceneData.c` | NO CHANGE | — |
| `filesystem.c` | ADD discard PatternSet; MODIFY 5 sites; disconnected boot reader | A |
| `sequencer.c` | ADD include; MODIFY 1 trigger call | B |
| `sequencer.h` | MODIFY 1 comment | B |
| `buttonHandler.c` | NO CHANGE | — |
| `ledHandler.c` | NO CHANGE | — |
| `EuklidGenerator.c` | NO CHANGE | — |
| `SomGenerator.c` | NO CHANGE | — |
| `menu.c` | NO CHANGE | — |
| `copyClearTools.c` | NO CHANGE | — |
| `storageTypes.c` | NO CHANGE | — |
| `STM32F765VIHx_FLASH.ld` | VERIFY (no change) | B½ |
| `SRAM_MANIFEST.md` | UPDATE measured sizes | B½ |

---

## 17. Implementation notes

### 2026-09-09 — implementation start

- Read `MEMORY.md`, `S062_DYNAMIC_PATTERN.md`, and this schedule before
  changing source. The requested allocation is the schedule's permanent
  `pat_regions[16]`: 167,936 bytes in normal SRAM1, owned by
  `PatternData.c` for the firmware lifetime.
- The live tree still embeds the 112-byte `PatternSet` in `scene_t`. The
  legacy `PatternSet` type and helpers must remain for `storageTypes.c`'s v3
  text bridge, but all resident Scene pattern access will move to the new
  address-array regions.
- The current filesystem still fans a parsed `PatternSet` into selected
  Scenes and the Session-061 boot reader parses `pattern.pat`. Both paths
  will be disconnected for this scope; Scene pattern regions will initialize
  empty and the v3 save path will write through a discard `PatternSet`.
- No dynamic pool allocator or special-value read/write is being added in
  B/B½. The per-Scene pool and full-width free bitmap are allocated and
  initialized now so the later C/D work has its defined memory topology.

### 2026-09-09 — source implementation and clean link

- Added `PAT_STACK_SIZE=256`, `PAT_DEFAULT_NOTE=63`, and
  `PAT_DEFAULT_VELOCITY=100` to `config.h`, plus named address-entry masks and
  `PAT_STEPS_PER_SCENE` in `PatternData.h`.
- Added `pat_scene_region_t` and `pat_regions[SCENE_COUNT]` in
  `PatternData.c`. Each region is 10,496 bytes: 1,792-byte address array,
  8,192-byte pool reservation, and 512-byte bitmap. Initialization writes
  `PAT_ADDR_SENTINEL`, zeros the pool, and marks the backed/unbacked bitmap
  split. The static assert and ARM symbol report both confirm the budget.
- Removed the embedded `PatternSet` field from `scene_t`. The legacy type and
  helpers remain available only to `storageTypes.c` and the filesystem discard
  bridge, as required by the schedule.
- Ported active-step read/set/toggle/erase, active-step scan, track/pattern
  clear, and copy stubs to the address array. Toggle preserves bits 14..0;
  erase and clear return entries to the sentinel. Sequencer B½ playback now
  uses the Pattern-owned default note and velocity.
- Disconnected Scene Load fan-out and boot `pattern.pat` application. Scene
  commits call `pat_initScene()`, Scene Save writes a reset discard PatternSet,
  and the v3 text parser remains validation-compatible. The discard accessor
  initially reset on every parsed line; this was corrected to reset once at
  text-parser entry so all seven track rows survive validation.
- Clean `make -j2` passed after the header edit. The linked result is
  `text=406,396`, `data=404`, `bss=262,468`; `pat_regions` is `0x29000`
  (167,936 bytes), `scenes` is `0x4b00` (19,200 bytes), and the discard bridge
  is 112 bytes. `make img` produced a 406,816-byte image with SHA-256
  `fcae86d0f49a48e02bd5def9eafe39ff3ab84d42c3404b51e7cbc8ff03de356e`.
- Hardware B½ checks remain pending: this session has source/link evidence only
  and has not flashed or exercised toggle, playback, clear, generators, Scene
  independence, or recording on the device.

### 2026-09-09 — independent code review (post-implementation)

Verified every scheduled change against the live source tree. Assessment:

**config.h (Section 1):** Three defines present after line 232 with a
detailed comment block. Values use the `u` suffix consistently (minor
improvement over the schedule's bare literals). Matches schedule.

**PatternData.h (Section 2):** All four address constants (2.1),
`PAT_STEPS_PER_SCENE` (2.2), retained `PatternSet` type (2.3), and
unchanged API declarations (2.4) verified. Comment blocks are richer than
the schedule specified — each section is now self-documenting.

**PatternData.c (Section 3):** All 16 subsections verified:
- `config.h` included (3.1). Region struct, two `_Static_assert`s, static
  array (3.2) — bonus range assert on `PAT_STACK_SIZE` itself.
  `pat_addrPtr` helper (3.3). `pat_initScene` rewritten with sentinel loop,
  pool zero, bitmap split (3.4). `pat_isStepActive` reads bit 15 (3.5).
  `pat_setStepActive` sets/clears bit 15, preserves 14–0 (3.6).
  `pat_toggleStep` XORs bit 15 (3.7). `pat_eraseStep` writes
  `PAT_ADDR_SENTINEL` with invalidation (3.8). `pat_sceneHasActiveSteps`
  scans for trigger bit (3.9). `pat_clearTrack` writes sentinel × 128
  (3.10). `pat_clearPattern` delegates to `pat_initScene` (3.11). Three
  copy functions are no-ops with `(void)` casts and deferral comments
  (3.12–3.14). Legacy helpers and stubs unchanged (3.15). No remaining
  `scene_get`/`scene_getConst` pattern-data dependencies (3.16).

**SceneData.h (Section 4):** `PatternSet pattern;` removed from `scene_t`.
Struct comment updated to explain pattern data lives in `PatternData.c`.
`kit_t kit` follows `settings` directly. Matches schedule.

**SceneData.c (Section 5):** No changes, as scheduled. `scene_initAll()`
still calls `pat_initScene()` at the correct point.

**filesystem.c (Section 6):** All six subsections verified:
- `filesystem_pattern_discard` at file scope outside `#if 0` (6.1).
  `filesystem_directPatternTarget` returns `&filesystem_pattern_discard`
  with `__attribute__((unused))` to suppress warnings from retired callers
  inside `#if 0` — good defensive addition not in the schedule (6.2).
  Pattern fan-out replaced with a disconnect comment; case 61 no longer
  copies `PatternSet` data (6.3). Scene Load commit calls
  `pat_initScene(scene_index)` (6.4). Save writer passes
  `&filesystem_pattern_discard` after a `pat_initPatternSet` reset (6.5).
  Boot reader returns `0u` immediately (6.6).
- Extra: the v3 text parse entry (line 12298) resets the discard once at
  file-phase entry rather than per-line. This was noted in the
  implementation log as a correction — the schedule's `directPatternTarget`
  originally cleared per call; the actual code correctly clears once so all
  seven track rows survive parsing.

**sequencer.c (Section 7):** `PAT_DEFAULT_VELOCITY, PAT_DEFAULT_NOTE` at
the trigger call site (7.2). `config.h` included.

**sequencer.h (Section 7.3):** Comment updated from "intentionally absent
because PatternSet stores neither" to the new formulation about specials
assigned through the step editor.

**No-change files (Sections 8–14):** `buttonHandler.c`, `ledHandler.c`,
`EuklidGenerator.c`, `SomGenerator.c`, `menu.c`, `copyClearTools.c`,
`storageTypes.c`, linker script — all confirmed unchanged, matching
schedule expectations.

**Link output:** `bss=262,468` with `pat_regions` at `0x29000` (167,936 B)
and `scenes` at `0x4b00` (19,200 B). The scene reduction is 112 × 16 =
1,792 B (from previous `scenes` size of 20,992 B). Net new SRAM1 = 166,144
B, matching Section 15's predicted budget within rounding. Total SRAM1
static use is within the 376,832-byte capacity.

**Quality improvements beyond schedule:**
1. `u` suffix on all config defines (type safety).
2. `PAT_STACK_SIZE` range assert in the region struct block.
3. `__attribute__((unused))` on the disconnected accessor.
4. Per-file-phase discard reset instead of per-line (correctness fix).
5. Comment blocks throughout are richer than the schedule's description
   text, providing full what/why/inputs/outputs/affiliates documentation.

**No issues found.** Implementation matches the schedule precisely on all
structural and behavioral points. Ready for Step B½ hardware verification.
