# S062 — Steps C–E Implementation Schedule

**Scope:** Pool allocator, dynamic block read/write, specials through menu
and playback, hardware verification. Builds on the Step A–B½ address
array completed in `S062_DYNAMIC_PATTERN_B_IMPLEMENT.md`.

**Prerequisite state (before Steps C–D):** The address array, pool memory, and bitmap are
allocated and initialized. Triggers work through bit 15. The pool is
zeroed and the bitmap is in its initial free/occupied split. No pool
blocks exist. `pat_applyStepToMenu`, `pat_setStepNote`,
`pat_setStepVolume`, `pat_setStepProbability` are no-op stubs.
`seq_advanceTrackStep` passes `PAT_DEFAULT_VELOCITY` /
`PAT_DEFAULT_NOTE` unconditionally. The step-edit submenu is visible
and navigable but displays zeros.

**Notation:** Each change entry lists file, line, operation
(ADD / REMOVE / MODIFY), and a description block structured as:
what / why / inputs / outputs / affiliates. These descriptions are
sized and worded for use as adjacent comment-block text in the
corresponding `.c` and `.h` files.

## Implementation log

### 2026-09-10 — Steps C/D source implementation and link checkpoint

- Added the three special-flag masks, dynamic-block header constants, and the
  public `pat_step_specials_t`/`pat_readStepSpecials()` contract in
  `PatternData.h`. Adjacent comments describe the field ownership and
  default-resolution contract.
- Added the synchronous first-fit chunk allocator, bitmap helpers, guarded
  pool release, block-size calculation, big-endian header writer, block reader,
  and specials read-modify-write orchestrator in `PatternData.c`. Pool offsets
  are accepted only when aligned, backed by the configured pool, and different
  from `PAT_ADDR_SENTINEL`; reserved special bits are masked on write/read.
- Updated erase and track-clear paths to reclaim pool chunks before resetting
  address entries. The existing whole-pattern clear still uses
  `pat_initScene()`, which resets all address, pool, and bitmap state.
- Replaced the three menu setter stubs and `pat_applyStepToMenu()` with the
  dynamic-pool read/write path. `PatternData.c` now includes `menu.h` so the
  existing `parameter_values[]`/`PAR_STEP_*` ownership is explicit; no new RAM
  allocation was introduced.
- Updated `seq_advanceTrackStep()` to resolve note/velocity/probability and gate
  triggers with the existing hardware RNG. Roll triggers remain independent
  fixed-note events, and automation/length/scale/shuffle/rotation remain out of
  scope as specified.
- `make -j2` passed. Conventional linked totals are `text=408,220`,
  `data=404`, `bss=262,468`; `pat_regions` remains `0x29000` / 167,936 bytes,
  `scenes` remains 19,200 bytes, and the legacy bridge remains 112 bytes.
  `git diff --check` also passed. The normal newlib syscall and LTO serial-link
  warnings remain the expected build warnings.
- Hardware Step E verification has not yet been performed in this session.

### 2026-09-10 — forced rebuild/package confirmation

- `make -B -j2` rebuilt every translation unit after the header changes and
  linked successfully. The expected existing warnings remain confined to
  unused legacy helpers, the nano-libc syscall stubs, and LTO's serial-job
  notice; no warning originated in the new PatternData or Sequencer code.
- `make img` produced `build/LXRV2_lxr02.img` at 408,640 bytes (408,624-byte
  firmware payload), SHA-256
  `412ca5b21509e1e2a787d7f82f15a4946f0c92489eb3ed0f422056c87f58eee9`.
- `git diff --check` passed. Step E remains a hardware procedure: no device
  flash, audio audition, probability sample, or pool-reclamation observation
  is claimed from the build-only environment.
- The live implementation deliberately uses a `should_trigger` byte instead
  of the schedule's illustrative local `goto`: it preserves the same
  probability semantics while keeping the trigger decision explicit. It also
  zeroes allocated block padding and validates pool bounds at every public
  read/free boundary; these are implementation-hardening details, not format
  changes.

### 2026-09-10 — independent code review (post-implementation)

Verified every scheduled C/D change against the live source tree. Assessment:

**PatternData.h (C1, C2, C13):** All seven new defines present.
`PAT_SPECIAL_FLAGS_MASK` added as a bonus combining mask — ensures reserved
bits are never set on write or read. `pat_step_specials_t` typedef and
`pat_readStepSpecials` declaration placed correctly between the trigger-bit
API and the range-operations block. Transitional-stubs comment updated to
reflect the three special setters are now real. Matches schedule.

**PatternData.c — Step C internals (C3–C12):** All internal functions present
in correct order:
- `pat_bitmapGet/Set/Clear` (C3) — three helpers, LSB-first convention.
- `pat_poolOffsetValid` — bonus centralized guard not in the schedule.
  Validates sentinel, 4-byte alignment, and pool range in one check. Used by
  `pat_poolFree`, `pat_blockWrite`, `pat_blockRead`, `pat_writeSpecials`,
  and `pat_readStepSpecials`.
- `pat_poolAlloc` (C4) — first-fit with `uint32_t` cast on `start + chunks`
  to prevent 16-bit overflow near pool boundary.
- `pat_poolFree` (C5) — uses `pat_poolOffsetValid` and `uint32_t` overflow
  guard on `base_chunk + chunks`.
- `pat_blockChunks` (C6) — masks with `PAT_SPECIAL_FLAGS_MASK` before
  counting. Explicit per-bit check instead of the schedule's shift loop;
  equivalent, clearer.
- `pat_blockWrite` (C7) — validates offset, masks reserved flags, and zeroes
  the full allocation (`memset(p, 0, chunks*4)`) before writing values.
  Schedule did not zero padding; this prevents stale bytes.
- `pat_blockRead` (C8) — validates offset and masks flags on read. No
  duplicate `pat_step_specials_t` typedef (defined in header per C13 note).
- `pat_writeSpecials` (C9) — all four cases (flags=0 free, same-chunks
  in-place, realloc, alloc failure) match schedule. Uses `trigger_bits`
  (stores the mask, not 0/1) — correct.
- `pat_eraseStep` (C10) — pool free before sentinel write via
  `pat_poolOffsetValid`.
- `pat_clearTrack` (C11) — free walk before sentinel writes.
- `pat_readStepSpecials` (C12) — public reader checks specials bit and
  offset validity before delegating to `pat_blockRead`.

**PatternData.c — Step D stubs replaced (D1–D4):**
- `pat_applyStepToMenu` (D1) — reads specials via `pat_readStepSpecials`,
  writes `parameter_values[PAR_STEP_NOTE/VOLUME/PROB]`. `#include "menu.h"`
  added for `parameter_values[]` access — necessary, not in schedule.
- `pat_setStepNote` (D2) — read-modify-write; `PAT_DEFAULT_NOTE` clears flag.
- `pat_setStepVolume` (D3) — read-modify-write; `PAT_DEFAULT_VELOCITY` clears
  flag.
- `pat_setStepProbability` (D4) — read-modify-write; 127 clears flag.
- All three setters match the schedule's pattern precisely.

**sequencer.c (D5–D7):**
- `#include "random.h"` with adjacent comment (D5).
- `seq_advanceTrackStep` trigger block (D6) — uses `should_trigger` flag
  instead of `goto`. Probability scaling matches:
  `(GetRngValue() & 0x7FFF) * 127 / 32767`. Triggers with `sp.velocity` and
  `sp.note` from `pat_readStepSpecials`.
- Comment updated (D7). Matches schedule.

**Link output:** `text=408,220` (+1,776 B from B½'s 406,444), `data=404`
(unchanged), `bss=262,468` (unchanged — no new static allocations).
`pat_regions` still 167,936 B, `scenes` still 19,200 B.

**No-change files verified:** `config.h`, `menu.c`, `menuPages.h`,
`buttonHandler.c`, `ledHandler.c`, `EuklidGenerator.c`, `SomGenerator.c`,
`copyClearTools.c`, `filesystem.c`, `storageTypes.c`, `sequencer.h` —
all confirmed unchanged.

**Quality improvements beyond schedule:**
1. `PAT_SPECIAL_FLAGS_MASK` combining constant.
2. `pat_poolOffsetValid()` centralized bounds guard.
3. Padding zeroed in `pat_blockWrite`.
4. `should_trigger` flag instead of `goto`.
5. `uint32_t` casts in allocator/free for 16-bit overflow safety.
6. `menu.h` include for explicit `parameter_values[]` ownership.

**No issues found.** Implementation matches the schedule on all structural
and behavioral points. Ready for Step E hardware verification.

---

## Step C — Pool allocator and dynamic block read/write

### Overview

Step C adds the internal allocator and block I/O layer. No public API
changes — the allocator is `static` to `PatternData.c`. Steps D and E
wire it into the menu setters and playback reader. Step C is a
compile-and-link checkpoint: all new code is reachable only from unit
tests or from the Step D callers added next.

---

### C1. PatternData.h — ADD special-flags constants

**Location:** after line 34 (`PAT_ADDR_OFFSET_MASK`)

**Operation:** ADD

```c
#define PAT_SPECIAL_NOTE_BIT     (1u << 0)
#define PAT_SPECIAL_VEL_BIT      (1u << 1)
#define PAT_SPECIAL_PROB_BIT     (1u << 2)
```

**Description:**

Bit masks for the special-flags byte (byte 2) of a dynamic pool block.
Each set bit means one value byte follows in the block, in ascending
bit order. Bit 0 = note override, bit 1 = velocity override, bit 2 =
probability. Bits 3–7 are reserved for future specials (timing, roll,
automation hold) and must be zero this session.

Why: named constants prevent magic numbers in every block reader and
writer. The sequencer playback path, `pat_applyStepToMenu`, and the
three `pat_setStep*` functions all test or construct this byte.

Inputs: compile-time only. Outputs: used by pool block readers/writers
in `PatternData.c` and (Step D) `sequencer.c`.

Affiliates: `PatternData.c` block layout, `S062_DYNAMIC_PATTERN.md`
Section 4.4 (special-flags byte field assignment).

---

### C2. PatternData.h — ADD block-header constants

**Location:** after the special-flags constants (C1)

**Operation:** ADD

```c
#define PAT_BLOCK_HEADER_BYTES   2u
#define PAT_BLOCK_STEP_ID_SHIFT  6u
#define PAT_BLOCK_STEP_ID_MASK   0xFFC0u
#define PAT_BLOCK_AUTO_COUNT_MASK 0x003Fu
```

**Description:**

Constants for the 2-byte dynamic block header. Bits 15–6 are the 10-bit
step-ID back-reference (0–895), and bits 5–0 are the 6-bit automation
entry count (always 0 this session; reserved for Phase 4.4).

The step-ID is `track * NUM_STEPS + step`. It allows the pool to be
scanned independently for integrity checks or future defragmentation
without requiring the address array. The automation count determines
the number of 2-byte target+value pairs that follow the specials
section of the block.

Why: named constants so the header encoding is never hardcoded. The
block writer and reader in `PatternData.c` both construct and parse
this header.

Inputs: compile-time only. Outputs: used by `pat_blockWrite` and
`pat_blockRead` (internal to `PatternData.c`).

Affiliates: `S062_DYNAMIC_PATTERN.md` Section 4.3 (dynamic block
layout).

---

### C3. PatternData.c — ADD bitmap chunk helpers

**Location:** after `pat_addrPtr()` (line 65)

**Operation:** ADD (three static helpers)

```c
static uint8_t pat_bitmapGet(const pat_scene_region_t *r,
                             uint16_t chunk)
{
    return (uint8_t)((r->bitmap[chunk >> 3u] >> (chunk & 7u)) & 1u);
}

static void pat_bitmapSet(pat_scene_region_t *r, uint16_t chunk)
{
    r->bitmap[chunk >> 3u] |= (uint8_t)(1u << (chunk & 7u));
}

static void pat_bitmapClear(pat_scene_region_t *r, uint16_t chunk)
{
    r->bitmap[chunk >> 3u] &= (uint8_t)~(1u << (chunk & 7u));
}
```

**Description:**

Read, set, and clear one bit in a Scene's 512-byte free-tracking bitmap.
Each bit represents one 4-byte chunk in the nominal 2^14 pool address
space. Chunk index = `byte_offset >> 2`. A set bit means "occupied"; a
clear bit means "free". Chunks beyond index `PAT_STACK_SIZE * 8 - 1`
are permanently set by `pat_initScene()`.

Why: the allocator and free functions both manipulate individual bitmap
bits. Centralizing the bit arithmetic prevents off-by-one and
byte/bit-order errors.

Inputs: region pointer and chunk index (0–4095). Outputs: one bitmap
bit is read, set, or cleared. No bounds checking — callers validate
the chunk index against `PAT_STACK_SIZE * 8`.

Affiliates: `pat_poolAlloc`, `pat_poolFree`, `pat_initScene`.

---

### C4. PatternData.c — ADD pool allocator

**Location:** after bitmap helpers (C3)

**Operation:** ADD (static function)

```c
static uint16_t pat_poolAlloc(pat_scene_region_t *r, uint8_t chunks)
{
    uint16_t maxChunk = (uint16_t)(PAT_STACK_SIZE * 8u);
    uint16_t start;
    uint16_t run;
    uint16_t i;

    if (chunks == 0u || r == NULL)
        return PAT_ADDR_SENTINEL;

    start = 0u;
    while (start + chunks <= maxChunk) {
        run = 0u;
        for (i = start; i < start + chunks; i++) {
            if (pat_bitmapGet(r, i)) {
                start = (uint16_t)(i + 1u);
                run = 0u;
                break;
            }
            run++;
        }
        if (run == chunks) {
            for (i = start; i < start + chunks; i++)
                pat_bitmapSet(r, i);
            return (uint16_t)(start << 2u);
        }
    }
    return PAT_ADDR_SENTINEL;
}
```

**Description:**

Allocate a contiguous run of `chunks` 4-byte blocks from the Scene's
pool. Returns the byte offset into `r->pool` on success, or
`PAT_ADDR_SENTINEL` on failure (pool full or no contiguous run
available).

The scan is a simple linear first-fit from chunk 0. When a free run of
the required length is found, each chunk is marked occupied in the
bitmap and the byte offset (`chunk_index << 2`) is returned. The caller
stores this byte offset in the address entry's bits 13–0.

Why: menu-paced edits do not require a sophisticated allocator. Linear
first-fit is correct and simple. Defragmentation and best-fit
strategies are deferred to Phase 4.3.

Inputs: region pointer and required chunk count (1–4 for this session).
Output: byte offset or sentinel. The caller must write block content
into `r->pool[offset]` after a successful allocation.

Affiliates: `pat_poolFree`, `pat_writeSpecials`, `pat_eraseStep`,
`pat_clearTrack`.

---

### C5. PatternData.c — ADD pool free

**Location:** after `pat_poolAlloc` (C4)

**Operation:** ADD (static function)

```c
static void pat_poolFree(pat_scene_region_t *r, uint16_t byte_offset,
                         uint8_t chunks)
{
    uint16_t base_chunk;
    uint16_t i;

    if (r == NULL || byte_offset == PAT_ADDR_SENTINEL || chunks == 0u)
        return;
    base_chunk = byte_offset >> 2u;
    for (i = base_chunk; i < base_chunk + chunks; i++)
        pat_bitmapClear(r, i);
    memset(&r->pool[byte_offset], 0, (uint16_t)(chunks * 4u));
}
```

**Description:**

Free a previously allocated pool block. Clears the corresponding bitmap
bits and zeroes the pool memory. The caller must also update the address
entry (clear bit 14 and write `PAT_ADDR_SENTINEL` to bits 13–0, or
write a new allocation offset if reallocating).

Why: the erase and clear paths need to return pool memory. Zeroing the
freed memory prevents stale data from being misread if a future
allocation occupies the same chunks.

Inputs: region pointer, byte offset of the block, and the chunk count
that was originally allocated. Output: bitmap bits cleared, pool bytes
zeroed.

Affiliates: `pat_poolAlloc`, `pat_eraseStep`, `pat_clearTrack`,
`pat_writeSpecials` (reallocate path).

---

### C6. PatternData.c — ADD block size calculator

**Location:** after `pat_poolFree` (C5)

**Operation:** ADD (static function)

```c
static uint8_t pat_blockChunks(uint8_t special_flags)
{
    uint8_t value_count = 0u;
    uint8_t total;
    uint8_t f = special_flags;

    while (f) {
        value_count += (uint8_t)(f & 1u);
        f >>= 1u;
    }
    total = (uint8_t)(PAT_BLOCK_HEADER_BYTES + 1u + value_count);
    return (uint8_t)((total + 3u) >> 2u);
}
```

**Description:**

Calculate the number of 4-byte chunks required for a dynamic block with
the given special-flags byte. The block layout is: 2-byte header +
1-byte special-flags + N value bytes (one per set flag bit). The result
is rounded up to the next 4-byte multiple.

This session's three flags (note, velocity, probability) produce these
sizes:
- 1 flag set: 2 + 1 + 1 = 4 bytes → 1 chunk
- 2 flags set: 2 + 1 + 2 = 5 bytes → 2 chunks
- 3 flags set: 2 + 1 + 3 = 6 bytes → 2 chunks

A block with zero flags is not allocated (the step has no specials,
so bit 14 is clear and the address is sentinel or trigger-only). The
automation count is always zero this session; when automation entries
are added in Phase 4.4, this function must also include `auto_count * 2`
bytes in the total.

Why: the allocator needs to know how many chunks to request, and the
reallocation path needs to compare old and new sizes.

Inputs: special-flags byte. Output: chunk count (1–4 for this session).

Affiliates: `pat_poolAlloc`, `pat_writeSpecials`.

---

### C7. PatternData.c — ADD block writer

**Location:** after `pat_blockChunks` (C6)

**Operation:** ADD (static function)

```c
static void pat_blockWrite(pat_scene_region_t *r, uint16_t byte_offset,
                           uint8_t track, uint8_t step,
                           uint8_t special_flags,
                           uint8_t note, uint8_t velocity,
                           uint8_t probability)
{
    uint8_t *p = &r->pool[byte_offset];
    uint16_t step_id = (uint16_t)(track * NUM_STEPS + step);
    uint16_t header = (uint16_t)((step_id << PAT_BLOCK_STEP_ID_SHIFT) & PAT_BLOCK_STEP_ID_MASK);

    p[0] = (uint8_t)(header >> 8u);
    p[1] = (uint8_t)(header & 0xFFu);
    p[2] = special_flags;

    uint8_t idx = 3u;
    if (special_flags & PAT_SPECIAL_NOTE_BIT)
        p[idx++] = note;
    if (special_flags & PAT_SPECIAL_VEL_BIT)
        p[idx++] = velocity;
    if (special_flags & PAT_SPECIAL_PROB_BIT)
        p[idx++] = probability;
}
```

**Description:**

Write a complete dynamic block at a known pool byte offset. The block
is laid out as: 2-byte header (10-bit step-ID in bits 15–6, 6-bit
automation count = 0 in bits 5–0), 1-byte special-flags, then value
bytes in ascending flag-bit order (note if bit 0, velocity if bit 1,
probability if bit 2).

The header is stored big-endian (high byte first) for consistent byte
ordering in pool dumps and future integrity checks. The step-ID is
`track * 128 + step` (range 0–895, fits in 10 bits).

Why: centralizes the block layout so every writer uses the same
encoding. The value byte ordering is fixed by the flag-bit ordering;
this function enforces that contract.

Inputs: region pointer, byte offset (from `pat_poolAlloc`), track,
step, special-flags, and the three value bytes. Only the values
corresponding to set flag bits are written. Output: block content is
written into the pool at the given offset.

Affiliates: `pat_blockRead`, `pat_writeSpecials`, `S062_DYNAMIC_PATTERN.md`
Section 4.3.

---

### C8. PatternData.c — ADD block reader

**Location:** after `pat_blockWrite` (C7)

**Operation:** ADD (static function)

```c
typedef struct {
    uint8_t note;
    uint8_t velocity;
    uint8_t probability;
    uint8_t flags;
} pat_step_specials_t;

static pat_step_specials_t pat_blockRead(const pat_scene_region_t *r,
                                         uint16_t byte_offset)
{
    pat_step_specials_t out;
    const uint8_t *p = &r->pool[byte_offset];
    uint8_t flags;
    uint8_t idx;

    out.note = PAT_DEFAULT_NOTE;
    out.velocity = PAT_DEFAULT_VELOCITY;
    out.probability = 127u;
    out.flags = 0u;

    flags = p[2];
    out.flags = flags;
    idx = 3u;
    if (flags & PAT_SPECIAL_NOTE_BIT)
        out.note = p[idx++];
    if (flags & PAT_SPECIAL_VEL_BIT)
        out.velocity = p[idx++];
    if (flags & PAT_SPECIAL_PROB_BIT)
        out.probability = p[idx++];

    return out;
}
```

**Description:**

Read a dynamic block at a known pool byte offset and extract the
specials into a value struct. Fields not present in the block (flag bit
clear) default to `PAT_DEFAULT_NOTE` (63), `PAT_DEFAULT_VELOCITY`
(100), and probability 127 (always fires).

The header (bytes 0–1) is skipped by this reader — it is only needed
for integrity scans and defragmentation, neither of which are
implemented this session. The reader parses the special-flags byte and
extracts value bytes in ascending flag-bit order, matching the writer.

Why: the sequencer playback path and `pat_applyStepToMenu` both need
to extract the same values from a pool block. A shared reader with
default fallbacks ensures consistency.

Inputs: region pointer and byte offset (from the address entry's bits
13–0). Output: `pat_step_specials_t` with resolved note, velocity,
probability, and the raw flags byte.

Affiliates: `pat_blockWrite`, `seq_advanceTrackStep` (Step D),
`pat_applyStepToMenu` (Step D).

---

### C9. PatternData.c — ADD specials write/update orchestrator

**Location:** after `pat_blockRead` (C8)

**Operation:** ADD (static function)

```c
static void pat_writeSpecials(uint8_t scene_index, uint8_t track,
                              uint8_t step,
                              uint8_t new_flags,
                              uint8_t note, uint8_t velocity,
                              uint8_t probability)
{
    pat_scene_region_t *r;
    uint16_t *entry;
    uint16_t addr;
    uint16_t old_offset;
    uint8_t old_chunks;
    uint8_t new_chunks;
    uint16_t new_offset;
    uint8_t trigger_bit;

    entry = pat_addrPtr(scene_index, track, step);
    if (!entry)
        return;
    r = &pat_regions[scene_index];
    addr = *entry;
    trigger_bit = (uint8_t)((addr >> 15u) & 1u);
    old_offset = addr & PAT_ADDR_OFFSET_MASK;

    if (new_flags == 0u) {
        if (old_offset != PAT_ADDR_SENTINEL) {
            old_chunks = pat_blockChunks(r->pool[old_offset + 2u]);
            pat_poolFree(r, old_offset, old_chunks);
        }
        *entry = (uint16_t)((trigger_bit ? PAT_ADDR_TRIGGER_BIT : 0u)
                            | PAT_ADDR_SENTINEL);
        bank_invalidateSdCleanScene(scene_index);
        return;
    }

    new_chunks = pat_blockChunks(new_flags);

    if (old_offset != PAT_ADDR_SENTINEL) {
        old_chunks = pat_blockChunks(r->pool[old_offset + 2u]);
        if (old_chunks == new_chunks) {
            pat_blockWrite(r, old_offset, track, step,
                           new_flags, note, velocity, probability);
            *entry = (uint16_t)((trigger_bit ? PAT_ADDR_TRIGGER_BIT : 0u)
                                | PAT_ADDR_SPECIALS_BIT
                                | old_offset);
            bank_invalidateSdCleanScene(scene_index);
            return;
        }
        pat_poolFree(r, old_offset, old_chunks);
    }

    new_offset = pat_poolAlloc(r, new_chunks);
    if (new_offset == PAT_ADDR_SENTINEL) {
        *entry = (uint16_t)((trigger_bit ? PAT_ADDR_TRIGGER_BIT : 0u)
                            | PAT_ADDR_SENTINEL);
        bank_invalidateSdCleanScene(scene_index);
        return;
    }

    pat_blockWrite(r, new_offset, track, step,
                   new_flags, note, velocity, probability);
    *entry = (uint16_t)((trigger_bit ? PAT_ADDR_TRIGGER_BIT : 0u)
                        | PAT_ADDR_SPECIALS_BIT
                        | new_offset);
    bank_invalidateSdCleanScene(scene_index);
}
```

**Description:**

Orchestrate a complete specials write for one step. This is the
single mutation entry point that the three `pat_setStep*` stubs will
call in Step D. It handles four cases:

1. **New flags = 0 (all specials removed):** Free the old block if one
   exists, write sentinel to bits 13–0, clear bit 14. The trigger bit
   (bit 15) is preserved.

2. **Same chunk count (in-place update):** When the old and new blocks
   require the same number of chunks, overwrite the block in place
   without freeing and reallocating. This is the common case when
   editing one special that was already assigned.

3. **Different chunk count (realloc):** Free the old block, allocate a
   new one at the new size, write the block, update the address entry.

4. **Allocation failure:** If `pat_poolAlloc` returns sentinel (pool
   full), the step loses its specials. The trigger bit is preserved.
   The user sees default values in the menu. This is a graceful
   degradation — no crash, no corruption, just data loss for the
   step's specials.

Why: the free-then-alloc approach is correct for menu-paced edits.
The in-place-update optimization avoids unnecessary fragmentation
when the user edits a value for a special that is already allocated.
The write-new/swap/free-old protocol from SCOPING_TARGETS 4.3 is
the correct approach for real-time paths in later sessions but is
not needed here.

Inputs: scene_index, track, step, new_flags (the desired special-flags
byte), and the three value bytes. Output: the pool block is written
(or freed), the address entry is updated, and the Scene's card-clean
bit is invalidated.

Affiliates: `pat_setStepNote`, `pat_setStepVolume`,
`pat_setStepProbability`, `pat_eraseStep`, `pat_clearTrack`.

---

### C10. PatternData.c — MODIFY pat_eraseStep — add pool free

**Current code (lines 215–232):**
```c
void pat_eraseStep(uint8_t scene_index, uint8_t track, uint8_t step)
{
    uint16_t *entry = pat_addrPtr(scene_index, track, step);
    /* ... comment ... */
    if (!entry)
        return;
    *entry = PAT_ADDR_SENTINEL;
    bank_invalidateSdCleanScene(scene_index);
}
```

**New code:**
```c
void pat_eraseStep(uint8_t scene_index, uint8_t track, uint8_t step)
{
    uint16_t *entry = pat_addrPtr(scene_index, track, step);
    uint16_t addr;
    uint16_t offset;

    if (!entry)
        return;
    addr = *entry;
    offset = addr & PAT_ADDR_OFFSET_MASK;
    if (offset != PAT_ADDR_SENTINEL) {
        pat_scene_region_t *r = &pat_regions[scene_index];
        uint8_t chunks = pat_blockChunks(r->pool[offset + 2u]);
        pat_poolFree(r, offset, chunks);
    }
    *entry = PAT_ADDR_SENTINEL;
    bank_invalidateSdCleanScene(scene_index);
}
```

**Description:**

Erase a step's complete address state, now including pool block
reclamation. Before writing the sentinel to the address entry, the
existing pool block (if any) is freed by reading its special-flags
byte to determine chunk count, then clearing those bitmap bits and
zeroing the pool memory.

Why: with pool blocks now existing (Step D writes them), the erase path
must reclaim pool memory. Without this, erased steps would leak chunks
that are never recovered until `pat_initScene()`.

Inputs: scene_index, track, step. Output: pool block freed (if any),
address entry set to `PAT_ADDR_SENTINEL`, Scene card-clean invalidated.

Affiliates: `seq_advanceTrackStep` erase mode (line 383),
`pat_poolFree`, `pat_blockChunks`.

---

### C11. PatternData.c — MODIFY pat_clearTrack — add pool free walk

**Current code (lines 257–274):**
```c
void pat_clearTrack(uint8_t scene_index, uint8_t track)
{
    uint16_t step;
    /* ... comment ... */
    if (!scene_indexValid(scene_index) || !pat_trackValid(track))
        return;
    for (step = 0u; step < NUM_STEPS; step++)
        pat_regions[scene_index].address[track][step] = PAT_ADDR_SENTINEL;
    bank_invalidateSdCleanScene(scene_index);
}
```

**New code:**
```c
void pat_clearTrack(uint8_t scene_index, uint8_t track)
{
    pat_scene_region_t *r;
    uint16_t step;
    uint16_t addr;
    uint16_t offset;

    if (!scene_indexValid(scene_index) || !pat_trackValid(track))
        return;
    r = &pat_regions[scene_index];
    for (step = 0u; step < NUM_STEPS; step++) {
        addr = r->address[track][step];
        offset = addr & PAT_ADDR_OFFSET_MASK;
        if (offset != PAT_ADDR_SENTINEL) {
            uint8_t chunks = pat_blockChunks(r->pool[offset + 2u]);
            pat_poolFree(r, offset, chunks);
        }
        r->address[track][step] = PAT_ADDR_SENTINEL;
    }
    bank_invalidateSdCleanScene(scene_index);
}
```

**Description:**

Clear one track's 128 address entries, now freeing each step's pool
block before writing the sentinel. Each entry is checked for a valid
pool offset; if present, the block's flags byte is read to determine
chunk count, the bitmap is cleared, and the pool memory is zeroed.

Why: track clear from copyClearTools and EuklidGenerator must reclaim
all pool blocks for the track. Without this, a track clear followed by
new step assignments would gradually fill the pool with unreachable
blocks.

Inputs: scene_index, track. Output: all 128 entries sentinel, all pool
blocks freed, Scene card-clean invalidated.

Affiliates: `copyClearTools.c:126` (`copyClear_clearCurrentTrack`),
`EuklidGenerator.c:283` (`euklid_generate`), `pat_poolFree`,
`pat_blockChunks`.

---

### C12. PatternData.c — ADD pat_readStepSpecials (public reader)

**Location:** after `pat_eraseStep` (or grouped with public API functions)

**Operation:** ADD

```c
pat_step_specials_t pat_readStepSpecials(uint8_t scene_index,
                                         uint8_t track, uint8_t step)
{
    pat_step_specials_t out;
    const uint16_t *entry;
    uint16_t addr;
    uint16_t offset;

    out.note = PAT_DEFAULT_NOTE;
    out.velocity = PAT_DEFAULT_VELOCITY;
    out.probability = 127u;
    out.flags = 0u;

    entry = pat_addrPtr(scene_index, track, step);
    if (!entry)
        return out;
    addr = *entry;
    if (!(addr & PAT_ADDR_SPECIALS_BIT))
        return out;
    offset = addr & PAT_ADDR_OFFSET_MASK;
    if (offset == PAT_ADDR_SENTINEL)
        return out;
    return pat_blockRead(&pat_regions[scene_index], offset);
}
```

**Description:**

Public API to read the resolved specials for one step. Returns a
`pat_step_specials_t` with defaults if the step has no pool block
(bit 14 clear or offset is sentinel). If the step has a pool block,
delegates to `pat_blockRead` to parse the block and extract values.

Why: the sequencer and menu both need to read specials. This function
provides a single public entry point that handles all the address-entry
checks and returns a complete value struct with defaults filled in.

Inputs: scene_index, track, step. Output: `pat_step_specials_t` with
note, velocity, probability, and flags. Invalid coordinates return
all-defaults.

Affiliates: `seq_advanceTrackStep` (Step D), `pat_applyStepToMenu`
(Step D), `pat_blockRead`.

---

### C13. PatternData.h — ADD pat_step_specials_t and pat_readStepSpecials declaration

**Location:** after line 94 (`uint8_t pat_sceneHasActiveSteps`)

**Operation:** ADD

```c
typedef struct {
    uint8_t note;
    uint8_t velocity;
    uint8_t probability;
    uint8_t flags;
} pat_step_specials_t;

pat_step_specials_t pat_readStepSpecials(uint8_t scene_index,
                                         uint8_t track, uint8_t step);
```

**Description:**

Public type and declaration for the step specials reader. The struct is
the return type for `pat_readStepSpecials` and is used by both the
sequencer (playback) and menu (step-edit display).

Why: `sequencer.c` and `menu.c` both include `PatternData.h`. The
struct must be visible to both translation units. The `typedef` in the
header replaces the file-local `typedef` in `PatternData.c` (C8) —
only one definition exists, in the header.

Inputs: compile-time type. Outputs: used as return value by
`pat_readStepSpecials`, used as local variable by `seq_advanceTrackStep`
and `pat_applyStepToMenu`.

**Note on C8 coordination:** The `typedef` in C8 (inside `PatternData.c`)
must be removed and replaced by an `#include` of this header-defined
type. Since `PatternData.c` already includes `PatternData.h`, the
struct definition in C8's code block is omitted at implementation time;
only the `pat_blockRead` function body is added in C8.

Affiliates: `sequencer.c`, `menu.c`, `PatternData.c`.

---

### C14. Step C link checkpoint

After all C1–C13 changes compile and link:

- All new functions are `static` except `pat_readStepSpecials`.
- No behavioral change: `pat_applyStepToMenu`, `pat_setStepNote`,
  `pat_setStepVolume`, `pat_setStepProbability` are still no-op stubs.
  `seq_advanceTrackStep` still passes defaults.
- `pat_eraseStep` and `pat_clearTrack` now call pool free, but since no
  pool blocks exist yet, the `offset != PAT_ADDR_SENTINEL` checks skip
  the free path for every entry. No behavioral change.
- Verify clean `make -j2`. Record text/data/bss sizes.

---

## Step D — Wire specials through menu and playback

### Overview

Step D replaces the no-op stubs with real pool read/write calls and
updates the sequencer to use resolved specials at trigger time. This
is the step where the dynamic pool becomes functional.

---

### D1. PatternData.c — MODIFY pat_applyStepToMenu

**Current code (line 348):**
```c
void pat_applyStepToMenu(uint8_t s,uint8_t t,uint8_t p) {(void)s;(void)t;(void)p;}
```

**New code:**
```c
void pat_applyStepToMenu(uint8_t scene_index, uint8_t track,
                         uint8_t step)
{
    pat_step_specials_t sp = pat_readStepSpecials(scene_index,
                                                   track, step);
    parameter_values[PAR_STEP_NOTE]   = sp.note;
    parameter_values[PAR_STEP_VOLUME] = sp.velocity;
    parameter_values[PAR_STEP_PROB]   = sp.probability;
}
```

**Description:**

Read the selected step's specials from the pool and copy them into
`parameter_values[]` so the step-edit submenu displays the current
values. If the step has no pool block, defaults are loaded (note 63,
velocity 100, probability 127).

Why: this function is called by `menu_showStepEditPage()` when the user
selects a step in STEP mode, and by `menu_parseParameter()` when
`PAR_ACTIVE_STEP` changes through encoder navigation. The menu's
endless-pot encoders and LCD repaint read `parameter_values[]` for
display and edit starting points.

Inputs: scene_index (viewed pattern), track (active voice), step
(active step). Output: `parameter_values[PAR_STEP_NOTE]`,
`parameter_values[PAR_STEP_VOLUME]`, and `parameter_values[PAR_STEP_PROB]`
are updated.

Affiliates: `menu_showStepEditPage()` (menu.c:9922),
`menu_parseParameter()` case `PAR_ACTIVE_STEP` (menu.c:9748),
`pat_readStepSpecials`.

---

### D2. PatternData.c — MODIFY pat_setStepNote

**Current code (line 350):**
```c
void pat_setStepNote(uint8_t s,uint8_t t,uint8_t p,uint8_t v) {(void)s;(void)t;(void)p;(void)v;}
```

**New code:**
```c
void pat_setStepNote(uint8_t scene_index, uint8_t track,
                     uint8_t step, uint8_t value)
{
    pat_step_specials_t sp = pat_readStepSpecials(scene_index,
                                                   track, step);
    uint8_t new_flags;

    if (value == PAT_DEFAULT_NOTE)
        new_flags = sp.flags & (uint8_t)~PAT_SPECIAL_NOTE_BIT;
    else
        new_flags = sp.flags | PAT_SPECIAL_NOTE_BIT;

    pat_writeSpecials(scene_index, track, step, new_flags,
                      value, sp.velocity, sp.probability);
}
```

**Description:**

Set the note override for one step. If the value equals
`PAT_DEFAULT_NOTE` (63), the note flag is cleared — the step uses the
default and the block shrinks or is freed. Otherwise the note flag is
set and the value is stored.

The read-modify-write pattern (read current specials, modify one field,
write all specials) ensures that editing one parameter does not disturb
the other two. `pat_writeSpecials` handles the allocate/free/realloc
orchestration.

Why: this is the menu encoder's mutation path for `PAR_STEP_NOTE`.
The user rotates the note knob on the step-edit submenu, and
`menu_parseParameter()` calls this function with the new value.

Inputs: scene_index, track, step, value (0–127). Output: pool block is
created, updated, resized, or freed.

Affiliates: `menu_parseParameter()` case `PAR_STEP_NOTE` (menu.c:9765),
`pat_readStepSpecials`, `pat_writeSpecials`.

---

### D3. PatternData.c — MODIFY pat_setStepVolume

**Current code (line 351):**
```c
void pat_setStepVolume(uint8_t s,uint8_t t,uint8_t p,uint8_t v) {(void)s;(void)t;(void)p;(void)v;}
```

**New code:**
```c
void pat_setStepVolume(uint8_t scene_index, uint8_t track,
                       uint8_t step, uint8_t value)
{
    pat_step_specials_t sp = pat_readStepSpecials(scene_index,
                                                   track, step);
    uint8_t new_flags;

    if (value == PAT_DEFAULT_VELOCITY)
        new_flags = sp.flags & (uint8_t)~PAT_SPECIAL_VEL_BIT;
    else
        new_flags = sp.flags | PAT_SPECIAL_VEL_BIT;

    pat_writeSpecials(scene_index, track, step, new_flags,
                      sp.note, value, sp.probability);
}
```

**Description:**

Set the velocity override for one step. If the value equals
`PAT_DEFAULT_VELOCITY` (100), the velocity flag is cleared. Otherwise
the flag is set and the value is stored.

Why: this is the menu encoder's mutation path for `PAR_STEP_VOLUME`.

Inputs: scene_index, track, step, value (0–127). Output: pool block
updated via `pat_writeSpecials`.

Affiliates: `menu_parseParameter()` case `PAR_STEP_VOLUME` (menu.c:9774),
`pat_readStepSpecials`, `pat_writeSpecials`.

---

### D4. PatternData.c — MODIFY pat_setStepProbability

**Current code (line 349):**
```c
void pat_setStepProbability(uint8_t s,uint8_t t,uint8_t p,uint8_t v) {(void)s;(void)t;(void)p;(void)v;}
```

**New code:**
```c
void pat_setStepProbability(uint8_t scene_index, uint8_t track,
                            uint8_t step, uint8_t value)
{
    pat_step_specials_t sp = pat_readStepSpecials(scene_index,
                                                   track, step);
    uint8_t new_flags;

    if (value == 127u)
        new_flags = sp.flags & (uint8_t)~PAT_SPECIAL_PROB_BIT;
    else
        new_flags = sp.flags | PAT_SPECIAL_PROB_BIT;

    pat_writeSpecials(scene_index, track, step, new_flags,
                      sp.note, sp.velocity, value);
}
```

**Description:**

Set the probability for one step. If the value equals 127 (always
fires — the default), the probability flag is cleared. Otherwise the
flag is set and the value is stored.

Note: the "default" for probability is 127 (always triggers), not a
`config.h` define, because it is a behavioral constant (certainty)
rather than a user-tunable default like note or velocity.

Why: this is the menu encoder's mutation path for `PAR_STEP_PROB`.

Inputs: scene_index, track, step, value (0–127, where 127 = always).
Output: pool block updated via `pat_writeSpecials`.

Affiliates: `menu_parseParameter()` case `PAR_STEP_PROB` (menu.c:9756),
`pat_readStepSpecials`, `pat_writeSpecials`.

---

### D5. sequencer.c — ADD include for random.h

**Location:** after line 59 (`#include "config.h"`)

**Operation:** ADD

```c
#include "random.h"
```

**Description:**

Needed for `GetRngValue()`, the hardware RNG function used for
probability-gated triggers.

Inputs: compile-time include. Output: `GetRngValue` is available for
the probability check in `seq_advanceTrackStep`.

Affiliates: `Core/DSPAudio/random.h`.

---

### D6. sequencer.c — MODIFY seq_advanceTrackStep — read specials

**Current trigger block (lines 386–388):**
```c
			} else {
				seq_triggerVoice(track, PAT_DEFAULT_VELOCITY, PAT_DEFAULT_NOTE);
			}
```

**New trigger block:**
```c
			} else {
				pat_step_specials_t sp = pat_readStepSpecials(
				    seq_activePattern, track,
				    (uint8_t)seq_stepIndex[track]);

				if (sp.probability < 127u) {
					uint8_t rnd = (uint8_t)((uint16_t)(GetRngValue() & 0x7FFFu) * 127u / 32767u);
					if (rnd >= sp.probability)
						goto skip_trigger;
				}
				seq_triggerVoice(track, sp.velocity, sp.note);
				skip_trigger: ;
			}
```

**Description:**

Read the step's specials from the pool and use them at trigger time.
The flow is:

1. Call `pat_readStepSpecials()` to get note, velocity, probability
   (with defaults for any values not stored in the pool).
2. If probability < 127: generate a random value 0–126 using the
   hardware RNG. If `rnd >= probability`, suppress the trigger
   (goto skip_trigger). Probability = 0 means the step never fires.
   Probability = 126 fires ~99.2% of the time. Probability = 127
   skips the check entirely (always fires).
3. Trigger with the resolved velocity and note.

The random scaling uses integer arithmetic:
`(GetRngValue() & 0x7FFF) * 127 / 32767` produces a value 0–127.
The `& 0x7FFF` ensures a positive value from `GetRngValue()` (which
returns `int16_t`). The division by 32767 scales the 15-bit range to
0–127.

Why: this is the core playback integration. Every triggered step now
uses its pool-stored note, velocity, and probability instead of the
global defaults. Steps without pool blocks get defaults from
`pat_readStepSpecials`, so the behavior is identical to Step B½ for
steps that have no specials assigned.

Inputs: track, seq_stepIndex[track], seq_activePattern. Output:
trigger is fired with resolved velocity/note, or suppressed by
probability.

Affiliates: `pat_readStepSpecials`, `GetRngValue` (random.h),
`seq_triggerVoice`.

**Implementation note:** The live code uses a local `should_trigger` boolean
to bypass the trigger call. It has the same semantics as the illustrative
`goto skip_trigger` form above, while making the final trigger decision
explicit and avoiding a local label in the scheduler.

---

### D7. sequencer.c — MODIFY seq_advanceTrackStep comment

**Current comment (lines 361–368):**
```c
	/*
	 * Advance and service one fixed-grid step for one track.
	 *
     * Input: track index at a sixteenth-note scheduler boundary. Output: its
     * cursor advances modulo 16 and an active address-array bit triggers with
     * PAT_DEFAULT_VELOCITY and PAT_DEFAULT_NOTE. Step B½ has no dynamic-pool
     * reader yet, so no probability, special note, automation, length, scale,
     * shuffle, or rotation data is read from PatternData.
	 */
```

**New comment:**
```c
	/*
	 * Advance and service one fixed-grid step for one track.
	 *
	 * Input: track index at a sixteenth-note scheduler boundary. Output: its
	 * cursor advances modulo 16 and an active address-array bit triggers with
	 * the step's pool-stored velocity and note (or defaults if no specials
	 * are assigned). Probability gates whether the trigger fires at all.
	 * Automation entries, length, scale, shuffle, and rotation are not yet
	 * read from PatternData.
	 */
```

**Description:**

Update the function comment to reflect the new specials integration.
The comment now accurately describes what data is read and how
probability affects triggering.

---

### D8. Step D link checkpoint

After all D1–D7 changes compile and link:

- `pat_applyStepToMenu`, `pat_setStepNote`, `pat_setStepVolume`, and
  `pat_setStepProbability` are no longer no-ops. They read and write
  pool blocks through the C-layer allocator.
- `seq_advanceTrackStep` reads specials and applies probability.
- Verify clean `make -j2`. Record text/data/bss sizes.
- The bss size should be unchanged (no new static allocations).
- Text will grow by the new function bodies.

---

## Step E — Hardware verification (full dynamic stack)

### E1. Flash and basic trigger test

Flash the firmware. Verify that existing patterns (from Step B½ testing)
continue to play correctly with default note 63 and velocity 100.
Verify step toggle on/off is unchanged.

### E2. Note override test

1. Enter STEP mode, select a step by pressing a SEQ button.
2. The step-edit submenu should display: velocity = 100, note = 63,
   probability = 127 (defaults).
3. Rotate the NOTE encoder to a different value (e.g., 72).
4. Play the pattern. The step should trigger at note 72 while all other
   steps trigger at note 63.
5. Select the step again. The menu should display note = 72 (the value
   persists in the pool).

### E3. Velocity override test

1. Select a step, rotate the VELOCITY encoder to a different value
   (e.g., 60).
2. Play the pattern. The step should be audibly quieter.
3. Verify the menu displays velocity = 60 when the step is re-selected.

### E4. Probability test

1. Select a step, set probability to ~64 (approximately 50%).
2. Play the pattern for several bars. The step should fire
   approximately half the time, visibly and audibly.
3. Set probability to 0. The step should never fire.
4. Set probability to 127. The step should always fire (back to
   default).

### E5. Multiple specials on one step

1. Assign note = 48, velocity = 80, probability = 100 to the same step.
2. Play: verify note 48, velocity 80, and probabilistic triggering all
   work simultaneously.
3. Select the step: verify the menu shows all three values correctly.

### E6. Toggle persistence test

1. Assign note = 72 to a step, then toggle the step off.
2. Toggle the step back on.
3. Select the step: the menu should still show note = 72 (specials
   survive the on→off→on toggle cycle because toggle only flips bit 15,
   preserving bits 14–0).

### E7. Erase test

1. Assign specials to a step.
2. Use sequencer erase mode (hold the erase button during playback) to
   erase the step.
3. Select the step: specials should be gone (note = 63, velocity = 100,
   probability = 127). The pool block was freed.

### E8. Track clear test

1. Assign specials to several steps on one track.
2. Clear the track from the menu's copy/clear page.
3. All steps should show default values. All pool blocks for that track
   should be freed (the pool is available for new allocations).

### E9. Pattern clear test

1. Assign specials to steps across multiple tracks.
2. Clear the entire pattern.
3. All steps across all tracks should show defaults. The bitmap should
   be reset to its initial state (first 256 bytes free, last 256
   permanently occupied).

### E10. Scene independence test

1. Switch to a different Scene.
2. Assign different specials to steps.
3. Switch back to the first Scene: its specials should be intact and
   independent.

### E11. Pool capacity test

1. Assign specials to many steps (e.g., 100+ steps with note + velocity
   + probability).
2. Verify the pool does not run out (100 steps × 2 chunks each = 200
   chunks out of 2,048 available — well within capacity).
3. If possible, push toward the capacity limit and verify that the
   allocator returns sentinel gracefully when full.

### E12. Default value behavior

1. Assign note = 63 (the default) to a step that had no note override.
   The pool block should not be allocated (or should be freed if one
   existed), since the default value requires no storage.
2. Assign velocity = 100 (the default) to a step. Same behavior.
3. Assign probability = 127 (the default) to a step. Same behavior.

### E13. Generator interaction test

1. Assign specials to steps, then run the Euklid generator for that
   track.
2. The generator calls `pat_clearTrack()` which frees pool blocks, then
   `pat_setStepActive()` which only sets bit 15. After generation,
   the track should have the Euklid trigger pattern with no specials
   (all defaults). The freed pool memory should be available for new
   allocations.

### E14. Linked image size verification

Record the final linked image sizes:
- `text`, `data`, `bss` from `arm-none-eabi-size`
- `pat_regions` symbol size from `arm-none-eabi-nm --print-size`
- Verify `bss` is unchanged from Step B½ (no new static allocations)
- Calculate text growth from Step B½ (the new function bodies)

### E15. SRAM_MANIFEST.md update

Update the SRAM manifest with final measured sizes. No static SRAM
change is expected from Steps C–D (all new code is text, not bss).

### E16. Commit as standalone checkpoint

Commit the complete Steps C–E firmware as a standalone checkpoint.
This firmware is the first version that supports per-step specials
(note, velocity, probability) with dynamic pool storage.

---

## Appendix A: Summary of operations by file

| File | Operations | Step |
|---|---|---|
| `PatternData.h` | ADD 8 defines (special-flags, supported-flags mask, and header); ADD `pat_step_specials_t` typedef; ADD `pat_readStepSpecials` declaration | C |
| `PatternData.c` | ADD 3 bitmap helpers + allocator + free + block-size + writer + reader + writeSpecials orchestrator + readStepSpecials public; MODIFY `pat_eraseStep` (add pool free); MODIFY `pat_clearTrack` (add pool free walk); MODIFY `pat_applyStepToMenu` (real read); MODIFY `pat_setStepNote` (real write); MODIFY `pat_setStepVolume` (real write); MODIFY `pat_setStepProbability` (real write) | C+D |
| `sequencer.c` | ADD `#include "random.h"`; MODIFY `seq_advanceTrackStep` trigger block (read specials + probability); MODIFY comment | D |
| `sequencer.h` | NO CHANGE | — |
| `config.h` | NO CHANGE | — |
| `menu.c` | NO CHANGE (existing `PAR_STEP_*` dispatch already calls the right `pat_setStep*` functions) | — |
| `menuPages.h` | NO CHANGE | — |
| `buttonHandler.c` | NO CHANGE | — |
| `ledHandler.c` | NO CHANGE | — |
| `EuklidGenerator.c` | NO CHANGE | — |
| `SomGenerator.c` | NO CHANGE | — |
| `copyClearTools.c` | NO CHANGE | — |
| `filesystem.c` | NO CHANGE | — |
| `storageTypes.c` | NO CHANGE | — |
| `STM32F765VIHx_FLASH.ld` | VERIFY (no change) | E |
| `SRAM_MANIFEST.md` | UPDATE measured sizes | E |

---

## Appendix B: Block layout reference

```
Pool block at byte_offset in r->pool:

Byte 0    : Header high byte  [step_id bits 9..2]
Byte 1    : Header low byte   [step_id bits 1..0 << 6 | auto_count (0)]
Byte 2    : Special-flags byte
              Bit 0 = note override present
              Bit 1 = velocity override present
              Bit 2 = probability present
              Bits 3–7 = reserved (must be 0)
Byte 3+   : Value bytes in ascending bit order:
              [note] if bit 0 set
              [velocity] if bit 1 set
              [probability] if bit 2 set

Chunk sizes:
  1 special  → 4 bytes → 1 chunk
  2 specials → 5 bytes → 2 chunks
  3 specials → 6 bytes → 2 chunks
```

---

## Appendix C: Address entry encoding reference

```
uint16_t address entry:

Bit 15     : trigger on/off (1 = active)
Bit 14     : has-specials (1 = pool block exists)
Bits 13–0  : 14-bit pool byte offset
               0x3FFF = sentinel (no pool block)
               0x0000–0x1FFC = valid at PAT_STACK_SIZE 256
               Must be a multiple of 4

Invariant: bit 14 set with offset 0x3FFF is illegal.
Toggle (bit 15 flip) preserves bits 14–0.
Erase writes 0x3FFF (full sentinel, bits 15+14 clear).
```

---

## Appendix D: Probability scaling

```
Input:  probability value 0–127 from pool block
Output: trigger or suppress

if probability == 127:
    always trigger (no RNG call)
else:
    rnd = (GetRngValue() & 0x7FFF) * 127 / 32767
    if rnd >= probability:
        suppress trigger
    else:
        trigger

probability = 0   → never fires (rnd is always >= 0)
probability = 64  → fires ~50% of the time
probability = 126 → fires ~99.2% of the time
probability = 127 → always fires (check skipped)
```
