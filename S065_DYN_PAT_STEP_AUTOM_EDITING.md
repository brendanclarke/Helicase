# S065 — Step Automation Editing Implementation

Parent: `S065_DYN_PAT_STEP_AUTOMATION.md` (general plan).
Session 066 will cover the VOICE overlay (Method 2), CGRAM underline, step
illumination, and async track-wide search.

---

## 1. Session 065 Scope

Deliver a working end-to-end automation path: create, edit, delete, and clear
step automations through the step-edit menu (Method 1), hear them play back via
the sequencer, and persist them through PAT4 save/load and Pattern AutoSave.

### In scope

| Area | Deliverable |
|------|-------------|
| Pool block format | `pat_blockChunks()`, `pat_blockWrite()`, `pat_blockRead()` extended for automation entries |
| New public APIs | `pat_readStepAutomations()`, `pat_writeStepAutomation()`, `pat_removeStepAutomation()`, `pat_removeTrackAutomationByTarget()` |
| Existing code fixes | `pat_writeSpecials()`, `pat_eraseStep()`, `pat_clearTrack()` — all must account for automation block sizes |
| Target validation | `instrumentManager_targetValid()` extended for Scene-range automation targets |
| Legacy cleanup | Replace four AVR-era stubs and the `modTargets[]` integration in `menu.c` |
| Step-edit menu | Automation pages after specials pages: add, edit, `del`, `clr`, `inv` display, parameter cycling with uniqueness |
| Sequencer playback | TIM3 copies automation entries to pending buffer; foreground drains via `instrumentManager_writeRuntime()` |
| Persistence | No format change needed — PAT4 and AutoSave carry automation via raw pool bytes |

### Deferred to 066

Method 2 VOICE overlay, CGRAM underline, step illumination, async track-wide
search, and any polish from this session.

---

## 2. Systems That Change

### 2.1 PatternData.h

New types:

```c
typedef struct {
    uint16_t target;   /* 9-bit instrument_param_id_t (bits 8..0) */
    uint8_t  value;    /* 7-bit (0..127) */
} pat_automation_entry_t;
```

New public function declarations:

- `uint8_t pat_readStepAutomations(uint8_t scene, uint8_t track, uint8_t step,
  pat_automation_entry_t *out, uint8_t max_count)` — returns count
- `uint8_t pat_writeStepAutomation(uint8_t scene, uint8_t track, uint8_t step,
  uint16_t target, uint8_t value)` — returns 1 on success, 0 on failure
- `uint8_t pat_removeStepAutomation(uint8_t scene, uint8_t track, uint8_t step,
  uint16_t target)` — returns 1 if found and removed
- `uint8_t pat_removeTrackAutomationByTarget(uint8_t scene, uint8_t track,
  uint16_t target)` — returns count of entries removed
- `uint8_t pat_stepAutomationCount(uint8_t scene, uint8_t track,
  uint8_t step)` — returns 0..63

Existing `pat_setStepAutomationDestination()`, `pat_setStepAutomationValue()`,
`pat_setActiveAutomationTrack()`, and `pat_setSelectedStep()` declarations
are removed.

### 2.2 PatternData.c

**Modified statics:**

| Function | Change |
|----------|--------|
| `pat_blockChunks()` | Add `auto_count` parameter. All existing callers updated to pass 0 or read from header. |
| `pat_blockWrite()` | Add `automation_entries` array + `auto_count` parameters. Writes entries after specials, pads to 4-byte boundary. Sets header bits 5..0 to auto_count. |
| `pat_blockRead()` | No change — still returns `pat_step_specials_t` for specials-only callers. |
| `pat_writeSpecials()` | Must read and preserve existing automation entries across specials edits. Block size calculation must include auto_count. The `new_flags == 0` early-exit must check auto_count before removing the block. |
| `pat_eraseStep()` | Block size calculation must include auto_count from header. |
| `pat_clearTrack()` | Same — reads each block's full size before freeing. |

**New static:**

- `pat_blockReadAutomations()` — reads automation entries from a pool offset,
  given the specials flags byte (to skip past specials region).

**New publics:** the five functions listed in §2.1.

**Removed:** the four legacy no-op stubs.

### 2.3 InstrumentManager.c

`instrumentManager_targetValid()` (`InstrumentManager.c:724-759`): extend the
`use == INSTRUMENT_TARGET_AUTOMATION` path to accept Scene-range IDs (384+) by
calling through to `sceneModTarget_valid()` instead of rejecting all non-voice
IDs.

### 2.4 menu.c

**Step-edit automation pages:** new rendering, navigation, and edit logic after
the existing specials pages. The page system handles:

- Dynamic page count (`fixed_specials_pages + auto_count + 1` for the add page)
- One automation entry per page: index, voice, parameter, amount
- Item 0: `del`/`clr` toggle via pot, encoder click executes
- Item 1: voice pot cycling (1..6)
- Item 2: parameter pot cycling (automatable descriptors, uniqueness-filtered)
- Item 3: amount pot (0..127)
- Add page: creates entry on encoder click or pot adjustment
- `inv` display for stale targets
- Navigation scroll (encoder turn) between automation pages

**Legacy removal:** the `modTargets[]` / `PAR_P1_DEST` / `PAR_P2_DEST`
integration at `menu.c:9582-9623` is replaced by the new dynamic page system.

### 2.5 sequencer.c

**Step advance modification** (`seq_advanceTrackStep()`): after the existing
trigger/specials path, check for automation entries regardless of trigger state
(bit 14 set + valid offset + auto_count > 0). Copy decoded entries to the
pending buffer.

**New static:** pending automation buffer (32 entries), drain flag, write index.

### 2.6 Main loop

**Foreground drain:** check the pending automation drain flag each main-loop
pass. If set, iterate entries and call `instrumentManager_writeRuntime()` for
each. Clear the flag and reset the write index after draining.

The drain call site is in the same pass as `timebase_serviceFrontPanel()` and
`filesystem_tick()` — somewhere in the existing foreground service loop.

---

## 3. Implementation Tasks (Dependency Order)

### Phase A: Pool block infrastructure

**A1. Extend `pat_blockChunks()`**

Add `uint8_t auto_count` parameter. Formula:

```c
total = PAT_BLOCK_HEADER_BYTES + 1u + value_count + auto_count * 2u;
return (uint8_t)((total + 3u) >> 2u);
```

Update all existing callers. Every call site that currently reads
`pat_blockChunks(r->pool[offset + 2u])` must also extract the auto_count from
the block header byte at `r->pool[offset + 1] & PAT_BLOCK_AUTO_COUNT_MASK`.

Affected callers:
- `pat_writeSpecials()` lines 395, 403, 405
- `pat_eraseStep()` line 575
- `pat_clearTrack()` — find its block-size calculation
- `pat_blockWrite()` line 305 (the memset size)

**A2. Extend `pat_blockWrite()`**

Add `const pat_automation_entry_t *autos, uint8_t auto_count` parameters. After
writing specials, write each 2-byte automation entry (packed: value<<9 | target)
in the remaining bytes. Set header bits 5..0 to auto_count. All existing
callers pass `NULL, 0`.

**A3. Add `pat_blockReadAutomations()`**

Static function. Given a pool offset and the specials flags byte, compute the
automation region start (`3 + popcount(flags & 0x07)`), read the auto_count
from header byte `pool[offset + 1] & 0x3F`, and unpack each 2-byte entry into
a `pat_automation_entry_t` array. Returns the count.

**A4. Fix `pat_writeSpecials()` for automation preservation**

The current `pat_writeSpecials()` must preserve existing automation entries
across a specials-only edit. Modified flow:

1. Read old block: specials via `pat_blockRead()`, automations via
   `pat_blockReadAutomations()`, auto_count from header.
2. Calculate `old_chunks = pat_blockChunks(old_flags, old_auto_count)`.
3. Calculate `new_chunks = pat_blockChunks(new_flags, old_auto_count)`.
4. If `new_flags == 0 && old_auto_count == 0`: remove block entirely (existing
   behavior).
5. If `new_flags == 0 && old_auto_count > 0`: keep block (flags byte = 0, no
   specials stored, automation entries remain).
6. If `new_chunks == old_chunks`: overwrite in place — write specials then
   re-write automations.
7. If sizes differ: allocate new block FIRST, write complete content (specials +
   preserved automations), swap address entry, then free old block. Do NOT free
   the old block before allocating the new one (see §5.2).

**A5. Fix `pat_eraseStep()` for automation-aware block size**

Replace `pat_blockChunks(r->pool[offset + 2u])` with the two-argument version
that includes the header's auto_count. The free size must account for
automation entries or pool chunks are leaked.

**A6. Fix `pat_clearTrack()` similarly**

Same change as A5 for every block-size calculation in the track-clear loop.

### Phase B: Public automation APIs

**B1. `pat_stepAutomationCount()`**

Read the address entry, check bit 14, read the header byte, return
`pool[offset + 1] & 0x3F`. Returns 0 for steps with no block.

**B2. `pat_readStepAutomations()`**

Validate coordinates, read address entry, call `pat_blockReadAutomations()`,
copy up to `max_count` entries into the output array. Returns the actual count.

**B3. `pat_writeStepAutomation()`**

Add or update one automation entry on a step. Flow:

1. Read existing specials and automations from the step's block (or defaults if
   no block exists).
2. Scan existing entries for a matching 9-bit target (uniqueness check).
   - If found: update the value. If block size unchanged, overwrite in place.
   - If not found: append a new entry. If auto_count == 63, fail (return 0).
3. Calculate new block size. Allocate new block (if size changed), write
   complete content, swap address, free old.
4. If step had no block (offset == SENTINEL): allocate a new block with the
   header, flags byte = 0 (or current specials flags), and the one automation
   entry.
5. Mark scene dirty. Return 1 on success, 0 on pool exhaustion or 63-limit.

**B4. `pat_removeStepAutomation()`**

Remove one entry by 9-bit target. Read-modify-write: read current block, find
the entry, remove it (shift remaining entries down), rewrite with
auto_count - 1. If auto_count reaches 0 and no specials exist, remove the
block entirely. Return 1 if found, 0 if not.

**B5. `pat_removeTrackAutomationByTarget()`**

Iterate all 128 steps. For each step with a block, call
`pat_removeStepAutomation()` if the step has a matching entry. Return total
count of entries removed. Used by `clr`.

### Phase C: Target validation and legacy cleanup

**C1. Extend `instrumentManager_targetValid()`**

In the `use == INSTRUMENT_TARGET_AUTOMATION` branch, before the
`instrumentParam_isVoiceParameter()` gate, add a check for Scene-range IDs:
if `id >= INSTRUMENT_VOICE_ID_COUNT && id < INSTRUMENT_TOTAL_ID_COUNT`, call
`sceneModTarget_valid(id)` (or equivalent) and return its result.

**C2. Remove legacy stubs**

Delete `pat_setStepAutomationDestination()`, `pat_setStepAutomationValue()`,
`pat_setActiveAutomationTrack()`, and `pat_setSelectedStep()` from
PatternData.c. Delete their declarations from PatternData.h. Remove the
`modTargets[]` integration at `menu.c:9582-9623`. Fix any compilation errors
from removed symbols.

### Phase D: Step-edit menu pages

**D1. Automation page rendering**

After the existing specials pages (note, velocity, probability), add
dynamically counted automation pages. Each page shows one entry. Render the
four items: index (`nnn`), voice (`1`..`6`), parameter (3-char `short_name` or
`inv`), amount (0..127).

**D2. Navigation**

Encoder scroll moves between automation pages. Rightmost page is the "add"
page (index = auto_count, items show `add`/`off`/`off`/`off`). Track
`current_auto_page_index` as menu state; reset on step selection change.

**D3. Pot editing**

- Item 0 pot: toggle `del`/`clr`.
- Item 1 pot: cycle voice 1..6.
- Item 2 pot: cycle automatable parameters for the selected voice, skipping
  targets already used by other entries on this step.
- Item 3 pot: adjust value 0..127.

On any value change, call `pat_writeStepAutomation()` to persist.

**D4. Add behavior**

On the "add" page, encoder press on item 0 or any pot adjustment away from
`off` creates a new entry with defaults (track's own voice, first available
automatable parameter, parameter's current Scene image value). Transform page
to normal editing page.

**D5. Delete and clear**

Encoder press on item 0:
- `del`: call `pat_removeStepAutomation()` for the current entry's target.
  Adjust page index.
- `clr`: call `pat_removeTrackAutomationByTarget()` for the current entry's
  target. Adjust page index.

### Phase E: Sequencer playback

**E1. Pending automation buffer**

Static module-level in sequencer.c (or a small automation_playback.c):

```c
typedef struct {
    uint16_t step_id;   /* track * 128 + step */
    uint16_t target;    /* 9-bit instrument_param_id_t */
    uint8_t  value;     /* 7-bit (0..127) */
} pending_auto_entry_t;

static volatile pending_auto_entry_t pending_auto[32];
static volatile uint8_t pending_auto_count;
static volatile uint8_t pending_auto_drain;
```

**E2. ISR step-advance automation readout**

In `seq_advanceTrackStep()`, after the existing trigger/specials block
(line 409), add an automation check that runs **regardless of trigger state**:

1. Read the address entry for the current step.
2. If bit 14 is set and offset is valid, read the block header for auto_count.
3. If auto_count > 0, read each 2-byte entry from the pool.
4. For each entry, scan the pending buffer for a matching `(step_id, target)`.
   If found, overwrite its value (debounce). If not found and buffer not full,
   append. If buffer full, drop.
5. Set `pending_auto_drain = 1`.

**E3. Foreground drain**

In the main loop, after `timebase_serviceFrontPanel()`:

```c
if (pending_auto_drain) {
    for (i = 0; i < pending_auto_count; i++) {
        uint16_t target = pending_auto[i].target;
        uint8_t value7 = pending_auto[i].value;
        uint8_t value8 = (value7 == 127u) ? 255u : (uint8_t)(value7 * 2u);
        uint8_t slot = instrumentParam_slot(target);
        uint8_t local = instrumentParam_local(target);
        // validate before applying
        if (instrumentParam_isVoiceParameter(target)) {
            if (local < instrumentManager_descriptorCount(slot))
                instrumentManager_writeRuntime(slot, local, value8);
        }
        // Scene targets: handle separately when validator is extended
    }
    pending_auto_count = 0;
    pending_auto_drain = 0;
}
```

### Phase F: Test and verify

**F1. Build and flash.** `make clean && make && make img`.

**F2. Step-edit workflow.** Create automations on several steps via Method 1.
Navigate between pages, add, edit values, delete, verify `clr` removes
track-wide.

**F3. Playback.** Start sequencer, hear automation values applied. Verify
automation fires on untriggered steps (automation-only, no trigger).

**F4. Instrument swap.** Change an instrument type on a slot that has
automation. Verify `inv` display. Verify `clr` cleanup. Verify playback
skips invalid entries.

**F5. PAT4 round-trip.** Save pattern, load into another Scene, verify
automations survive.

**F6. Pool stress.** Fill a step with many automations. Verify 63-limit
behavior. Fill the pool, verify allocation failure is graceful.

---

## 4. RAM Impact

### 4.1 New static allocations

| Symbol | Size | Region | Owner | Lifetime |
|--------|------|--------|-------|----------|
| `pending_auto` | 32 × 5 = 160 B (or 32 × 6 = 192 B padded) | normal SRAM1 `.bss` | sequencer.c | process lifetime |
| `pending_auto_count` | 1 B | normal SRAM1 `.bss` | sequencer.c | process lifetime |
| `pending_auto_drain` | 1 B | normal SRAM1 `.bss` | sequencer.c | process lifetime |

Total: approximately **162–194 bytes** of new static SRAM1 allocation.

Per `SRAM_MANIFEST.md` allocation policy: "Normal SRAM1 free capacity is
reserved exclusively for future Pattern data." The pending buffer is Pattern
playback infrastructure and fits within that reservation. An explicit byte
count, region, lifetime, owner, and user acknowledgement are required before
implementation.

### 4.2 Stack impact

The deepest new stack allocation is a local `pat_automation_entry_t[63]` array
in `pat_writeStepAutomation()` and `pat_writeSpecials()` (for preserving
existing automations during read-modify-write): `63 × 4 = 252 bytes`. The
foreground call paths (menu handler → PatternData) have ample stack budget.

The ISR path (`seq_advanceTrackStep`) does NOT allocate a local automation
array — it reads directly from the pool and writes to the static pending
buffer. No ISR stack growth.

---

## 5. Risks and Edge Cases

### 5.1 Block size calculation must include auto_count everywhere

**Critical.** Every existing call to `pat_blockChunks()` passes only the
specials flags byte. After this session, blocks can be larger due to automation
entries. If any caller calculates block size without the auto_count:

- `pat_eraseStep()` and `pat_clearTrack()` will under-free, leaking pool
  chunks that remain marked as occupied but are unreferenced.
- `pat_writeSpecials()` will under-size the replacement block, truncating
  automation entries.

All callers must be audited. The auto_count lives in the block header:
`r->pool[offset + 1] & PAT_BLOCK_AUTO_COUNT_MASK` (bits 5..0 of the second
header byte, big-endian storage).

### 5.2 pat_writeSpecials() free-before-allocate destroys automation on failure

**Critical.** The current `pat_writeSpecials()` pattern is:

1. Free old block (line 414: `pat_poolFree`)
2. Allocate new block (line 417: `pat_poolAlloc`)
3. If allocation fails → address becomes SENTINEL (line 419)

If a specials edit triggers a size change on a step that also has automation,
and the new allocation fails, all existing automation on that step is lost.
The old block was already freed at step 1.

**Fix:** allocate the new block FIRST. If allocation succeeds, write, swap
address, then free old. If allocation fails, the old block is untouched and the
edit is rejected (the specials change doesn't happen, but the step retains all
its existing data). This is a different allocation order from the current code.

Trade-off: allocate-first means the old block's chunks are unavailable for the
new allocation. A block that grows by one chunk might fail even though the old
block's released chunks would have provided space. This is the correct trade-off
— data preservation beats opportunistic in-place growth.

### 5.3 Automation-only blocks (no specials)

A step can have automation entries but no specials overrides (flags byte = 0x00).
This happens when automation is added to a step that has never had note/velocity/
probability set.

The current `pat_writeSpecials()` has an early exit at line 392:
`if (new_flags == 0u)` → free block and set SENTINEL. With automation, this
must become `if (new_flags == 0u && auto_count == 0u)`. A block with flags=0
but auto_count>0 must be preserved.

The block format handles this cleanly: byte 2 (flags) = 0x00 means no specials
are stored. The automation entries follow immediately after byte 2. Block size:
`3 + auto_count * 2` bytes, rounded up to chunks.

### 5.4 PAT_ADDR_SPECIALS_BIT naming

Bit 14 is named `PAT_ADDR_SPECIALS_BIT` and documented as "dynamic specials
block exists." With automation, a block can exist without specials. The bit's
semantic meaning becomes "dynamic block exists." The name should be updated
or aliased to avoid confusion, but all behavioral logic is the same — bit 14
means "a valid pool block is referenced."

No behavioral change needed, only a naming/documentation update.

### 5.5 ISR reading during foreground block rewrite

The TIM3 ISR reads pool blocks for both specials and automation. The foreground
edits blocks via read-modify-write. The existing code uses a 16-bit address
entry as the atomic switch: the ISR reads through the address entry to find the
block offset, and the 16-bit address write is atomic on Cortex-M4 (naturally
aligned `uint16_t` in the address array at struct offset 0).

Two sub-cases:

**In-place overwrite** (old_chunks == new_chunks): `pat_blockWrite()` does
`memset(p, 0, ...)` then writes bytes sequentially. If the ISR reads mid-write,
it sees zeros (memset phase) or partial values. For specials, this means
one-tick defaults. For automation, this means auto_count=0 for one tick — no
automations applied. Benign.

**Size-change rewrite** (with allocate-first fix from §5.2): the old block
remains valid at its offset until the address entry is updated. The ISR reads
either the old block (before swap) or the new block (after swap). No partial
state is visible. The free of the old block happens after the address swap; the
ISR cannot reference the freed block because the address no longer points to it.

### 5.6 Writing automation to a step with no existing block

A step with address == `PAT_ADDR_SENTINEL` (no block, just trigger state) needs
a new block allocated when automation is added. The new block contains:
header (with auto_count=1), flags byte (0x00, no specials), and one automation
entry. Chunk count: `(3 + 2 + 3) / 4 = 2` → wait: `(3 + 2) = 5 bytes`, padded
to 8 → 2 chunks.

The address entry gains bit 14 and the new offset. Bit 15 (trigger) is
preserved — the step might be untriggered but still have automation
(`SCOPING_TARGETS.md` §4.6).

### 5.7 pat_eraseStep called from TIM3 ISR (record-erase mode)

`seq_advanceTrackStep()` at `sequencer.c:391-393` calls `pat_eraseStep()` from
the TIM3 ISR when erase mode is active. `pat_eraseStep()` frees pool chunks
and writes the address entry. With automation, the block may be larger (more
chunks to free), but the free operation is the same loop — clear bitmap bits
and zero pool bytes. This is safe from the ISR (SRAM writes, no function
pointers or strings). No change needed, just the block-size fix from §5.1.

### 5.8 `clr` traversing 128 steps

The `clr` action searches all 128 steps for matching automation entries and
removes them. Each removal is an independent read-modify-write with potential
reallocation. Worst case: 128 steps each having one matching entry. Each step
needs a pool read, entry removal, and pool write. This is pure SRAM work — no
SD I/O, no ISR interaction. At ~microseconds per step, the full scan completes
in well under 1 ms. No perceptible UI stall.

### 5.9 Step-edit page index adjustment after delete/clear

When `del` removes an entry, the total automation count decreases. If the
deleted entry was at index N:
- If N < new auto_count: show entry at index N (the one that shifted down).
- If N == new auto_count (was the last entry): show index N-1 (new last entry),
  or the add page if none remain.

When `clr` removes multiple entries (including possibly the current one):
- Re-read the step's auto_count. Set page index to `min(old_index, new
  auto_count)`. If new auto_count == 0, show the add page.

### 5.10 Sequencer: automation read independent of trigger state

The current `seq_advanceTrackStep()` only enters the specials/trigger path when
`pat_isStepActive()` returns true (bit 15 set). Automation must be read
regardless of trigger state (`SCOPING_TARGETS.md` §4.6).

The automation read check must be OUTSIDE the `if (pat_isStepActive(...))` block
— it uses the address entry's bit 14 and offset, not bit 15. An untriggered
step with automation (bit 15=0, bit 14=1, valid offset, auto_count>0) must
still copy entries to the pending buffer.

But it must still be inside the `if (!(seq_mutedTracks & (1u << track)))` check
— muted tracks should not apply automation.

### 5.11 Default values when adding a new automation entry

The "add" page in Method 1 creates an entry with:
- **Voice:** the track's own voice slot (track index, since tracks 0..6 map to
  slots 0..5 for tracks 0..5, and track 6 maps to slot 5). Verify the
  track-to-slot mapping.
- **Parameter:** first valid automatable parameter from
  `instrumentManager_stepTargetForSlot()` with `current = INSTRUMENT_PARAM_INVALID`
  and `direction = +1`. This skips non-automatable descriptors and any targets
  already used by existing entries on this step.
- **Value:** the parameter's current Scene image value, converted 8-bit→7-bit:
  `scene_instrumentSlotConst(scene, slot)->parameter_images
  .instrument_parameters[descriptor_index]`, then `(v >= 255) ? 127 : v / 2`.

If no valid automatable parameter is available (all parameters on all voices
are already covered by existing entries, or the instrument has no automatable
parameters), the add operation fails silently.

---

## 6. Open Items to Resolve Before Code

### 6.1 Track-to-voice-slot mapping

The general plan assumes track N maps to voice slot N. Verify this is correct
in the existing codebase — check `menu_getActiveVoice()`, the sequencer's
`seq_triggerVoice()`, and any track-to-slot translation. If tracks 0..5 map to
slots 0..5 but track 6 has a special mapping, the automation target builder
needs to know.

### 6.2 Header byte order

`pat_blockWrite()` stores the header big-endian: `p[0] = header >> 8`,
`p[1] = header & 0xFF`. The auto_count is in bits 5..0 of the 16-bit header,
which lands in `p[1] & 0x3F`. Verify `pat_blockRead()` reads the header
consistently. The current `pat_blockRead()` skips the header entirely (starts
reading at `p[2]` for flags). The new automation reader will need to read `p[1]`
for the auto_count.

### 6.3 Foreground drain call site

Identify the exact location in the main loop for the pending automation drain
check. It should run every foreground pass, before the audio render but after
step advance processing. Check `main.c` or the timebase service loop for the
right insertion point.

### 6.4 Menu integration points

Identify how the step-edit page system works:
- How does the encoder scroll between pages?
- How are page counts managed?
- Where does the specials page rendering live?
- How does `pat_applyStepToMenu()` populate the current page?

These determine where the automation page code hooks in.

### 6.5 Scene target application path

The foreground drain applies voice parameters via
`instrumentManager_writeRuntime()`. Scene targets (IDs 384+) need a different
application path — there is no `instrumentManager_writeRuntime()` for Scene
parameters. Identify how Scene mod targets (Morph, Decimation, etc.) receive
runtime values today (velocity modulation? LFO? direct write?) and replicate
that for automation.

If Scene target application is complex, it can be deferred to a later session
while voice parameters (0..383) work immediately.

### 6.6 RAM allocation acknowledgement

Per `SRAM_MANIFEST.md` policy, new static SRAM1 allocations require
explicit user acknowledgement before implementation:

- **Pending step-event buffer:** 514 bytes unconditional SRAM1 .bss
  (SEQ_PENDING_BUF_COUNT × 4 + 2 = 128 × 4 + 2). Count adjustable in
  config.h. Owner: sequencer.c. Lifetime: process.
- **Pattern trace ring:** 256 bytes DEV_MODE_LOGGING-only SRAM1
  (PAT_TRACE_RECORD_COUNT × 8 = 32 × 8). Not present in production builds.
  Owner: PatternTrace.c. Lifetime: process.

---

## 7. Complete Implementation Specification

Every change below is described by file, line number, and operation
(ADD/MODIFY/REMOVE). Code blocks include the comment documentation
required at each site. All line numbers reference commit `ed4a2db`.

---

### 7.1 Phase A — Pool Block Infrastructure

#### A1. Extend `pat_blockChunks()`

**File:** `Core/Bank/Scene/Pattern/PatternData.c`
**Lines:** 261–275
**Operation:** MODIFY

**Current code (lines 261–275):**
```c
static uint8_t pat_blockChunks(uint8_t special_flags)
{
    uint8_t value_count = 0u;
    uint8_t total;

    special_flags &= (uint8_t)PAT_SPECIAL_FLAGS_MASK;
    if (special_flags & PAT_SPECIAL_NOTE_BIT)
        value_count++;
    if (special_flags & PAT_SPECIAL_VEL_BIT)
        value_count++;
    if (special_flags & PAT_SPECIAL_PROB_BIT)
        value_count++;
    total = (uint8_t)(PAT_BLOCK_HEADER_BYTES + 1u + value_count);
    return (uint8_t)((total + 3u) >> 2u);
}
```

**Replacement:**
```c
/*
 * Calculate the four-byte allocation size for a dynamic pool block.
 *
 * What: compute chunks from the specials flags byte and the automation
 * entry count. Why: allocator, free, reallocation, and block-size audit
 * paths must all agree on block ownership. The formula accounts for the
 * two-byte header, one flags byte, popcount(flags) specials values, and
 * auto_count two-byte automation entries, all rounded up to 4-byte chunks.
 *
 * Inputs: special_flags (bits 0..2 note/vel/prob), auto_count (0..63).
 * Output: chunk count (1..32). Affiliates: pat_poolAlloc(), pat_poolFree(),
 *   pat_writeSpecials(), pat_eraseStep(), pat_clearTrack(),
 *   pat_writeStepAutomation(), pat_removeStepAutomation().
 */
static uint8_t pat_blockChunks(uint8_t special_flags, uint8_t auto_count)
{
    uint8_t value_count = 0u;
    uint8_t total;

    special_flags &= (uint8_t)PAT_SPECIAL_FLAGS_MASK;
    if (special_flags & PAT_SPECIAL_NOTE_BIT)
        value_count++;
    if (special_flags & PAT_SPECIAL_VEL_BIT)
        value_count++;
    if (special_flags & PAT_SPECIAL_PROB_BIT)
        value_count++;
    total = (uint8_t)(PAT_BLOCK_HEADER_BYTES + 1u + value_count +
                      auto_count * 2u);
    return (uint8_t)((total + 3u) >> 2u);
}
```

**Caller update summary** — every existing call must pass the auto_count
read from the block header (`pool[offset + 1] & PAT_BLOCK_AUTO_COUNT_MASK`)
or `0u` for new blocks:

| Call site | Line | Current | New |
|-----------|------|---------|-----|
| `pat_blockWrite` memset | 305 | `pat_blockChunks(special_flags)` | `pat_blockChunks(special_flags, auto_count)` |
| `pat_writeSpecials` old-block size | 395 | `pat_blockChunks(r->pool[old_offset + 2u])` | `pat_blockChunks(r->pool[old_offset + 2u], old_auto_count)` |
| `pat_writeSpecials` new-block size | 403 | `pat_blockChunks(new_flags)` | `pat_blockChunks(new_flags, old_auto_count)` |
| `pat_writeSpecials` same-size check | 405 | `pat_blockChunks(r->pool[old_offset + 2u])` | `pat_blockChunks(r->pool[old_offset + 2u], old_auto_count)` |
| `pat_eraseStep` | 575 | `pat_blockChunks(r->pool[offset + 2u])` | `pat_blockChunks(r->pool[offset + 2u], r->pool[offset + 1u] & PAT_BLOCK_AUTO_COUNT_MASK)` |
| `pat_clearTrack` | 662 | `pat_blockChunks(r->pool[offset + 2u])` | `pat_blockChunks(r->pool[offset + 2u], r->pool[offset + 1u] & PAT_BLOCK_AUTO_COUNT_MASK)` |

---

#### A2. Extend `pat_blockWrite()`

**File:** `Core/Bank/Scene/Pattern/PatternData.c`
**Lines:** 287–317
**Operation:** MODIFY

**Current code (lines 287–317):**
```c
static void pat_blockWrite(pat_scene_region_t *r, uint16_t byte_offset,
                           uint8_t track, uint8_t step,
                           uint8_t special_flags, uint8_t note,
                           uint8_t velocity, uint8_t probability)
{
    uint8_t *p;
    uint16_t step_id;
    uint16_t header;
    uint8_t idx;

    if (!r || !pat_poolOffsetValid(byte_offset))
        return;
    special_flags &= (uint8_t)PAT_SPECIAL_FLAGS_MASK;
    p = &r->pool[byte_offset];
    step_id = (uint16_t)(track * NUM_STEPS + step);
    header = (uint16_t)((step_id << PAT_BLOCK_STEP_ID_SHIFT) &
                        PAT_BLOCK_STEP_ID_MASK);

    memset(p, 0, (size_t)pat_blockChunks(special_flags) * 4u);
    p[0] = (uint8_t)(header >> 8u);
    p[1] = (uint8_t)(header & 0xFFu);
    p[2] = special_flags;

    idx = 3u;
    if (special_flags & PAT_SPECIAL_NOTE_BIT)
        p[idx++] = note;
    if (special_flags & PAT_SPECIAL_VEL_BIT)
        p[idx++] = velocity;
    if (special_flags & PAT_SPECIAL_PROB_BIT)
        p[idx++] = probability;
}
```

**Replacement:**
```c
/*
 * Write one complete dynamic block at an allocated pool offset.
 *
 * What: encode the step back-reference, automation count, flags, specials
 * values, and automation entries in the documented byte order. Why: every
 * mutation path (specials edit, automation add/remove, erase) and future
 * integrity reader needs one stable layout. The header stores the step_id
 * in bits 15..6 and auto_count in bits 5..0 (big-endian). After the
 * specials region, each automation entry is packed as a little-endian
 * 16-bit word: bits 15..9 = 7-bit value, bits 8..0 = 9-bit target.
 *
 * Inputs: region, validated offset, track/step, flags, three candidate
 *   specials values, automation entry array (may be NULL when count is 0),
 *   and auto_count (0..63).
 * Output: the allocated block is fully written and zero-padded to the
 *   chunk boundary. Affiliates: pat_blockRead(),
 *   pat_blockReadAutomations(), pat_writeSpecials(),
 *   pat_writeStepAutomation(), pat_removeStepAutomation().
 */
static void pat_blockWrite(pat_scene_region_t *r, uint16_t byte_offset,
                           uint8_t track, uint8_t step,
                           uint8_t special_flags, uint8_t note,
                           uint8_t velocity, uint8_t probability,
                           const pat_automation_entry_t *autos,
                           uint8_t auto_count)
{
    uint8_t *p;
    uint16_t step_id;
    uint16_t header;
    uint8_t idx;
    uint8_t i;

    if (!r || !pat_poolOffsetValid(byte_offset))
        return;
    special_flags &= (uint8_t)PAT_SPECIAL_FLAGS_MASK;
    if (auto_count > 63u)
        auto_count = 63u;
    p = &r->pool[byte_offset];
    step_id = (uint16_t)(track * NUM_STEPS + step);
    header = (uint16_t)(((step_id << PAT_BLOCK_STEP_ID_SHIFT) &
                         PAT_BLOCK_STEP_ID_MASK) |
                        (auto_count & PAT_BLOCK_AUTO_COUNT_MASK));

    memset(p, 0, (size_t)pat_blockChunks(special_flags, auto_count) * 4u);
    p[0] = (uint8_t)(header >> 8u);
    p[1] = (uint8_t)(header & 0xFFu);
    p[2] = special_flags;

    idx = 3u;
    if (special_flags & PAT_SPECIAL_NOTE_BIT)
        p[idx++] = note;
    if (special_flags & PAT_SPECIAL_VEL_BIT)
        p[idx++] = velocity;
    if (special_flags & PAT_SPECIAL_PROB_BIT)
        p[idx++] = probability;

    for (i = 0u; i < auto_count; i++) {
        uint16_t packed = (uint16_t)(((autos[i].value & 0x7Fu) << 9u) |
                                     (autos[i].target & 0x01FFu));
        p[idx++] = (uint8_t)(packed & 0xFFu);
        p[idx++] = (uint8_t)(packed >> 8u);
    }
}
```

**Caller update** — every existing call to `pat_blockWrite` must append
`NULL, 0u`:

| Call site | Line | Change |
|-----------|------|--------|
| `pat_writeSpecials` in-place overwrite | 407–408 | append `, autos, old_auto_count` (preserved entries) |
| `pat_writeSpecials` new-block write | 424–425 | append `, autos, old_auto_count` (preserved entries) |

These callers are modified as part of A4 below, not simply `NULL, 0u`,
because `pat_writeSpecials` must preserve existing automation entries.

---

#### A3. Add `pat_blockReadAutomations()`

**File:** `Core/Bank/Scene/Pattern/PatternData.c`
**Location:** ADD after `pat_blockRead()` (after line 356, before line 358)
**Operation:** ADD

```c
/*
 * Read automation entries from one dynamic pool block.
 *
 * What: locate the automation region after the specials values, read the
 * auto_count from the block header, and unpack each 2-byte entry into the
 * caller's typed array. Why: separating automation reads from specials
 * reads lets the sequencer's hot trigger path stay small (specials only)
 * while edit and playback paths read automations independently.
 *
 * Inputs: validated Scene region, pool byte offset, the flags byte from
 *   pool[offset + 2] (to compute the specials region size), output array,
 *   and max_count capacity. Output: actual entry count (0..min(auto_count,
 *   max_count)). Each entry's target and value are unpacked from the
 *   little-endian packed format (bits 15..9 = value, bits 8..0 = target).
 * Affiliates: pat_readStepAutomations(), pat_writeStepAutomation(),
 *   pat_removeStepAutomation(), pat_writeSpecials().
 */
static uint8_t pat_blockReadAutomations(const pat_scene_region_t *r,
                                        uint16_t byte_offset,
                                        uint8_t special_flags,
                                        pat_automation_entry_t *out,
                                        uint8_t max_count)
{
    const uint8_t *p;
    uint8_t auto_count;
    uint8_t value_count;
    uint8_t auto_start;
    uint8_t copy_count;
    uint8_t i;

    if (!r || !pat_poolOffsetValid(byte_offset) || !out || max_count == 0u)
        return 0u;

    p = &r->pool[byte_offset];
    auto_count = (uint8_t)(p[1] & PAT_BLOCK_AUTO_COUNT_MASK);
    if (auto_count == 0u)
        return 0u;

    special_flags &= (uint8_t)PAT_SPECIAL_FLAGS_MASK;
    value_count = 0u;
    if (special_flags & PAT_SPECIAL_NOTE_BIT)   value_count++;
    if (special_flags & PAT_SPECIAL_VEL_BIT)    value_count++;
    if (special_flags & PAT_SPECIAL_PROB_BIT)   value_count++;
    auto_start = (uint8_t)(3u + value_count);

    copy_count = (auto_count < max_count) ? auto_count : max_count;
    for (i = 0u; i < copy_count; i++) {
        uint8_t base = (uint8_t)(auto_start + i * 2u);
        uint16_t packed = (uint16_t)((uint16_t)p[base + 1u] << 8u) |
                          (uint16_t)p[base];
        out[i].target = (uint16_t)(packed & 0x01FFu);
        out[i].value  = (uint8_t)((packed >> 9u) & 0x7Fu);
    }
    return copy_count;
}
```

---

#### A4. Fix `pat_writeSpecials()` for automation preservation

**File:** `Core/Bank/Scene/Pattern/PatternData.c`
**Lines:** 370–428
**Operation:** MODIFY (complete rewrite)

**Current code (lines 370–428):**
```c
static void pat_writeSpecials(uint8_t scene_index, uint8_t track,
                              uint8_t step, uint8_t new_flags,
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
    uint16_t trigger_bits;

    entry = pat_addrPtr(scene_index, track, step);
    if (!entry)
        return;
    r = &pat_regions[scene_index];
    new_flags &= (uint8_t)PAT_SPECIAL_FLAGS_MASK;
    addr = *entry;
    trigger_bits = (uint16_t)(addr & PAT_ADDR_TRIGGER_BIT);
    old_offset = (uint16_t)(addr & PAT_ADDR_OFFSET_MASK);

    if (new_flags == 0u) {
        if (pat_poolOffsetValid(old_offset)) {
            old_chunks = pat_blockChunks(r->pool[old_offset + 2u]);
            pat_poolFree(r, old_offset, old_chunks);
        }
        *entry = (uint16_t)(trigger_bits | PAT_ADDR_SENTINEL);
        pat_markSceneDirty(scene_index);
        return;
    }

    new_chunks = pat_blockChunks(new_flags);
    if (pat_poolOffsetValid(old_offset)) {
        old_chunks = pat_blockChunks(r->pool[old_offset + 2u]);
        if (old_chunks == new_chunks) {
            pat_blockWrite(r, old_offset, track, step, new_flags, note,
                           velocity, probability);
            *entry = (uint16_t)(trigger_bits | PAT_ADDR_SPECIALS_BIT |
                                old_offset);
            pat_markSceneDirty(scene_index);
            return;
        }
        pat_poolFree(r, old_offset, old_chunks);
    }

    new_offset = pat_poolAlloc(r, new_chunks);
    if (new_offset == PAT_ADDR_SENTINEL) {
        *entry = (uint16_t)(trigger_bits | PAT_ADDR_SENTINEL);
        pat_markSceneDirty(scene_index);
        return;
    }

    pat_blockWrite(r, new_offset, track, step, new_flags, note, velocity,
                   probability);
    *entry = (uint16_t)(trigger_bits | PAT_ADDR_SPECIALS_BIT | new_offset);
    pat_markSceneDirty(scene_index);
}
```

**Replacement:**
```c
/*
 * Replace one step's specials while preserving existing automation entries.
 *
 * What: read the current block (specials + automations), replace only the
 * specials portion, and rewrite the complete block. If no specials remain
 * AND no automations exist, the block is removed. If only automations
 * remain (new_flags == 0 but auto_count > 0), the block is preserved with
 * flags = 0. The allocate-first pattern prevents automation loss when a
 * size change requires reallocation (S065 risk §5.2).
 *
 * Why: specials and automations share one pool block. A specials-only edit
 * must not truncate or lose the automation payload. The previous code freed
 * before allocating and ignored auto_count entirely.
 *
 * Inputs: Scene/track/step, desired flags, three candidate specials values.
 * Output: address and pool state agree; on allocation failure the old block
 *   is untouched and the specials edit is silently rejected (data preserved).
 * Affiliates: pat_setStepNote(), pat_setStepVolume(), pat_setStepProbability(),
 *   pat_blockReadAutomations(), pat_blockWrite(), pat_blockChunks().
 */
static void pat_writeSpecials(uint8_t scene_index, uint8_t track,
                              uint8_t step, uint8_t new_flags,
                              uint8_t note, uint8_t velocity,
                              uint8_t probability)
{
    pat_scene_region_t *r;
    uint16_t *entry;
    uint16_t addr;
    uint16_t old_offset;
    uint8_t old_auto_count;
    uint8_t old_chunks;
    uint8_t new_chunks;
    uint16_t new_offset;
    uint16_t trigger_bits;
    pat_automation_entry_t autos[63];

    entry = pat_addrPtr(scene_index, track, step);
    if (!entry)
        return;
    r = &pat_regions[scene_index];
    new_flags &= (uint8_t)PAT_SPECIAL_FLAGS_MASK;
    addr = *entry;
    trigger_bits = (uint16_t)(addr & PAT_ADDR_TRIGGER_BIT);
    old_offset = (uint16_t)(addr & PAT_ADDR_OFFSET_MASK);

    /* Read existing automation entries to preserve them. */
    old_auto_count = 0u;
    if ((addr & PAT_ADDR_SPECIALS_BIT) != 0u &&
        pat_poolOffsetValid(old_offset)) {
        old_auto_count = pat_blockReadAutomations(
            r, old_offset, r->pool[old_offset + 2u], autos, 63u);
    }

    /*
     * If no specials AND no automations, remove the block entirely.
     * If no specials but automations exist, keep the block (flags=0).
     */
    if (new_flags == 0u && old_auto_count == 0u) {
        if (pat_poolOffsetValid(old_offset)) {
            old_chunks = pat_blockChunks(r->pool[old_offset + 2u],
                                         old_auto_count);
            pat_poolFree(r, old_offset, old_chunks);
        }
        *entry = (uint16_t)(trigger_bits | PAT_ADDR_SENTINEL);
        pat_markSceneDirty(scene_index);
        return;
    }

    new_chunks = pat_blockChunks(new_flags, old_auto_count);
    if (pat_poolOffsetValid(old_offset)) {
        old_chunks = pat_blockChunks(r->pool[old_offset + 2u],
                                     old_auto_count);
        if (old_chunks == new_chunks) {
            pat_blockWrite(r, old_offset, track, step, new_flags, note,
                           velocity, probability, autos, old_auto_count);
            *entry = (uint16_t)(trigger_bits | PAT_ADDR_SPECIALS_BIT |
                                old_offset);
            pat_markSceneDirty(scene_index);
            return;
        }
        /*
         * Allocate-first: do NOT free the old block before the new one
         * is secured. If allocation fails, the old block and all its
         * automation data remain intact. The specials edit is rejected
         * rather than losing automation. (S065 risk §5.2.)
         */
        new_offset = pat_poolAlloc(r, new_chunks);
        if (new_offset == PAT_ADDR_SENTINEL) {
            /* Allocation failed — old block preserved, edit rejected. */
            pat_markSceneDirty(scene_index);
            return;
        }
        pat_blockWrite(r, new_offset, track, step, new_flags, note,
                       velocity, probability, autos, old_auto_count);
        *entry = (uint16_t)(trigger_bits | PAT_ADDR_SPECIALS_BIT |
                            new_offset);
        pat_poolFree(r, old_offset, old_chunks);
        pat_markSceneDirty(scene_index);
        return;
    }

    /* No existing block — allocate new. */
    new_offset = pat_poolAlloc(r, new_chunks);
    if (new_offset == PAT_ADDR_SENTINEL) {
        *entry = (uint16_t)(trigger_bits | PAT_ADDR_SENTINEL);
        pat_markSceneDirty(scene_index);
        return;
    }
    pat_blockWrite(r, new_offset, track, step, new_flags, note, velocity,
                   probability, autos, old_auto_count);
    *entry = (uint16_t)(trigger_bits | PAT_ADDR_SPECIALS_BIT | new_offset);
    pat_markSceneDirty(scene_index);
}
```

**Critical changes from the current code:**
1. Reads existing automations into stack-local `autos[63]` (252 B) before any modification.
2. Early exit now checks `new_flags == 0u && old_auto_count == 0u` (was `new_flags == 0u`).
3. All `pat_blockChunks()` calls pass `old_auto_count`.
4. All `pat_blockWrite()` calls pass `autos, old_auto_count`.
5. **Allocate-first pattern**: new block is allocated BEFORE old block is freed. On failure, the old block is preserved and the edit is silently rejected — no data loss.

---

#### A5. Fix `pat_eraseStep()`

**File:** `Core/Bank/Scene/Pattern/PatternData.c`
**Lines:** 554–580
**Operation:** MODIFY (line 575 only)

**Current code (line 575):**
```c
        uint8_t chunks = pat_blockChunks(r->pool[offset + 2u]);
```

**Replacement:**
```c
        /*
         * Block size must include automation entries. The auto_count lives
         * in the header's second byte bits 5..0. Under-freeing would leak
         * occupied pool chunks that remain marked but unreferenced.
         */
        uint8_t chunks = pat_blockChunks(r->pool[offset + 2u],
                                          r->pool[offset + 1u] &
                                              PAT_BLOCK_AUTO_COUNT_MASK);
```

---

#### A6. Fix `pat_clearTrack()`

**File:** `Core/Bank/Scene/Pattern/PatternData.c`
**Lines:** 640–668
**Operation:** MODIFY (line 662 only)

**Current code (line 662):**
```c
            uint8_t chunks = pat_blockChunks(r->pool[offset + 2u]);
```

**Replacement:**
```c
            /*
             * Same auto_count-aware block size as pat_eraseStep(). Every
             * block-free path must read the header for auto_count or
             * pool chunks are leaked.
             */
            uint8_t chunks = pat_blockChunks(r->pool[offset + 2u],
                                              r->pool[offset + 1u] &
                                                  PAT_BLOCK_AUTO_COUNT_MASK);
```

---

### 7.2 Phase B — Public Automation APIs

#### B-TYPE. New type in PatternData.h

**File:** `Core/Bank/Scene/Pattern/PatternData.h`
**Location:** ADD after line 168 (after `pat_step_specials_t` closing brace)
**Operation:** ADD

```c
/*
 * One decoded step automation entry.
 *
 * What: in-memory representation of one automation binding stored in a
 * dynamic pool block. Why: callers read and write automations as typed
 * structs while the pool stores them as packed 2-byte words. The target
 * field carries a 9-bit instrument_param_id_t (voice-descriptor or
 * Scene-mod-target namespace); the value field carries a 7-bit amount
 * (0..127). Inputs/outputs: pat_readStepAutomations() fills these;
 * pat_writeStepAutomation() consumes target+value. Affiliates:
 * pat_blockReadAutomations(), pat_blockWrite(), sequencer playback.
 */
typedef struct {
    uint16_t target;   /* 9-bit instrument_param_id_t (bits 8..0) */
    uint8_t  value;    /* 7-bit (0..127) */
} pat_automation_entry_t;
```

---

#### B-DECL. New declarations in PatternData.h

**File:** `Core/Bank/Scene/Pattern/PatternData.h`
**Lines:** 201–206
**Operation:** REMOVE old declarations, ADD new block

**Remove (lines 201–206):**
```c
void pat_setActiveAutomationTrack(uint8_t value);
void pat_setSelectedStep(uint8_t step);
void pat_setStepAutomationDestination(uint8_t scene_index, uint8_t track,
                                      uint8_t step, uint8_t slot, uint16_t value);
void pat_setStepAutomationValue(uint8_t scene_index, uint8_t track,
                                uint8_t step, uint8_t slot, uint8_t value);
```

**Insert in their place:**
```c
/*
 * Step automation read/write API.
 *
 * What: public access to per-step automation entries stored in the dynamic
 * pool. Each entry pairs a 9-bit instrument_param_id_t with a 7-bit value.
 * A step holds 0..63 entries; the count is in header bits 5..0. Why: the
 * step-edit menu and sequencer need a stable interface to automation data
 * without learning pool byte layout. These replace the legacy two-lane
 * pat_setStepAutomation* stubs.
 *
 * Inputs: Scene/track/step coordinates. Outputs: decoded entries or
 *   success/failure codes. Affiliates: step-edit menu (Phase D), sequencer
 *   playback (Phase E), Pattern AutoSave (raw pool persistence).
 */
uint8_t pat_stepAutomationCount(uint8_t scene_index, uint8_t track,
                                uint8_t step);
uint8_t pat_readStepAutomations(uint8_t scene_index, uint8_t track,
                                uint8_t step, pat_automation_entry_t *out,
                                uint8_t max_count);
uint8_t pat_writeStepAutomation(uint8_t scene_index, uint8_t track,
                                uint8_t step, uint16_t target, uint8_t value);
uint8_t pat_removeStepAutomation(uint8_t scene_index, uint8_t track,
                                 uint8_t step, uint16_t target);
uint8_t pat_removeTrackAutomationByTarget(uint8_t scene_index, uint8_t track,
                                          uint16_t target);
```

---

#### B1–B5. New public functions in PatternData.c

**File:** `Core/Bank/Scene/Pattern/PatternData.c`
**Location:** ADD after line 798 (replacing the four removed stubs)
**Operation:** REMOVE lines 795–798, ADD five new functions

**Remove (lines 795–798):**
```c
void pat_setActiveAutomationTrack(uint8_t v) { (void)v; }
void pat_setSelectedStep(uint8_t step) { (void)step; }
void pat_setStepAutomationDestination(uint8_t s,uint8_t t,uint8_t p,uint8_t l,uint16_t v) {(void)s;(void)t;(void)p;(void)l;(void)v;}
void pat_setStepAutomationValue(uint8_t s,uint8_t t,uint8_t p,uint8_t l,uint8_t v) {(void)s;(void)t;(void)p;(void)l;(void)v;}
```

**Insert:**

```c
/*
 * Return the automation entry count on one step.
 *
 * What: read the 6-bit auto_count from the pool block header without
 * decoding any entry data. Why: the step-edit menu needs a count to
 * compute its dynamic page total, and callers need a lightweight
 * has-automation test. Returns 0 for trigger-only or unallocated steps.
 *
 * Inputs: resident Scene index, track 0..6, step 0..127.
 * Output: 0..63 automation entry count.
 * Affiliates: step-edit page count computation,
 *   pat_readStepAutomations().
 */
uint8_t pat_stepAutomationCount(uint8_t scene_index, uint8_t track,
                                uint8_t step)
{
    const uint16_t *entry;
    uint16_t addr;
    uint16_t offset;

    entry = pat_addrPtr(scene_index, track, step);
    if (!entry)
        return 0u;
    addr = *entry;
    if ((addr & PAT_ADDR_SPECIALS_BIT) == 0u)
        return 0u;
    offset = (uint16_t)(addr & PAT_ADDR_OFFSET_MASK);
    if (!pat_poolOffsetValid(offset))
        return 0u;
    return (uint8_t)(pat_regions[scene_index].pool[offset + 1u] &
                     PAT_BLOCK_AUTO_COUNT_MASK);
}

/*
 * Read decoded automation entries from one step.
 *
 * What: validate coordinates, locate the pool block, and unpack entries
 * into the caller's array. Why: the step-edit menu and track-wide removal
 * helper need decoded entries without learning pool layout. Entries beyond
 * max_count are silently not copied.
 *
 * Inputs: Scene/track/step, output array (must not be NULL), max_count.
 * Output: actual count written to `out` (0..min(auto_count, max_count)).
 * Affiliates: pat_blockReadAutomations(), pat_writeStepAutomation(),
 *   pat_removeStepAutomation().
 */
uint8_t pat_readStepAutomations(uint8_t scene_index, uint8_t track,
                                uint8_t step, pat_automation_entry_t *out,
                                uint8_t max_count)
{
    const uint16_t *entry;
    uint16_t addr;
    uint16_t offset;
    const pat_scene_region_t *r;

    if (!out || max_count == 0u)
        return 0u;
    entry = pat_addrPtr(scene_index, track, step);
    if (!entry)
        return 0u;
    addr = *entry;
    if ((addr & PAT_ADDR_SPECIALS_BIT) == 0u)
        return 0u;
    offset = (uint16_t)(addr & PAT_ADDR_OFFSET_MASK);
    if (!pat_poolOffsetValid(offset))
        return 0u;
    r = &pat_regions[scene_index];
    return pat_blockReadAutomations(r, offset, r->pool[offset + 2u],
                                    out, max_count);
}

/*
 * Add or update one automation entry on a step.
 *
 * What: write one (target, value) entry to the step's pool block.
 * Creates the block if none exists, updates in place if the target already
 * has an entry (uniqueness invariant), or appends otherwise. Uses the
 * allocate-first pattern so a failed reallocation preserves the old block.
 *
 * Why: the step-edit menu edits one entry at a time. The API must own the
 * complete read-modify-write including allocation, address swap, and free,
 * so callers never see partial pool state.
 *
 * Inputs: Scene/track/step, 9-bit target (masked), 7-bit value (clamped).
 * Output: 1 on success, 0 on pool exhaustion, 63-limit, or bad coords.
 *   Trigger bit is preserved. Scene is marked dirty on success.
 * Affiliates: pat_blockReadAutomations(), pat_blockRead(),
 *   pat_blockWrite(), pat_blockChunks(), pat_poolAlloc(), pat_poolFree(),
 *   pat_markSceneDirty(), step-edit menu, VOICE overlay (Session 066).
 */
uint8_t pat_writeStepAutomation(uint8_t scene_index, uint8_t track,
                                uint8_t step, uint16_t target, uint8_t value)
{
    uint16_t *entry;
    uint16_t addr;
    uint16_t offset;
    uint16_t trigger_bits;
    pat_scene_region_t *r;
    pat_step_specials_t sp;
    pat_automation_entry_t autos[63];
    uint8_t auto_count;
    uint8_t old_flags;
    uint8_t old_chunks;
    uint8_t new_chunks;
    uint16_t new_offset;
    uint8_t i;
    uint8_t found;

    entry = pat_addrPtr(scene_index, track, step);
    if (!entry)
        return 0u;

    r = &pat_regions[scene_index];
    addr = *entry;
    trigger_bits = (uint16_t)(addr & PAT_ADDR_TRIGGER_BIT);
    target &= 0x01FFu;
    if (value > 127u)
        value = 127u;

    offset = (uint16_t)(addr & PAT_ADDR_OFFSET_MASK);
    if ((addr & PAT_ADDR_SPECIALS_BIT) != 0u && pat_poolOffsetValid(offset)) {
        sp = pat_blockRead(r, offset);
        old_flags = sp.flags;
        auto_count = pat_blockReadAutomations(r, offset, old_flags,
                                              autos, 63u);
        old_chunks = pat_blockChunks(old_flags, auto_count);
    } else {
        sp.note = PAT_DEFAULT_NOTE;
        sp.velocity = PAT_DEFAULT_VELOCITY;
        sp.probability = 127u;
        old_flags = 0u;
        auto_count = 0u;
        old_chunks = 0u;
        offset = PAT_ADDR_SENTINEL;
    }

    found = 0u;
    for (i = 0u; i < auto_count; i++) {
        if ((autos[i].target & 0x01FFu) == target) {
            autos[i].value = value;
            found = 1u;
            break;
        }
    }
    if (!found) {
        if (auto_count >= 63u)
            return 0u;
        autos[auto_count].target = target;
        autos[auto_count].value = value;
        auto_count++;
    }

    new_chunks = pat_blockChunks(old_flags, auto_count);

    if (old_chunks > 0u && new_chunks == old_chunks) {
        pat_blockWrite(r, offset, track, step, old_flags,
                       sp.note, sp.velocity, sp.probability,
                       autos, auto_count);
        *entry = (uint16_t)(trigger_bits | PAT_ADDR_SPECIALS_BIT | offset);
        pat_markSceneDirty(scene_index);
        return 1u;
    }

    new_offset = pat_poolAlloc(r, new_chunks);
    if (new_offset == PAT_ADDR_SENTINEL)
        return 0u;

    pat_blockWrite(r, new_offset, track, step, old_flags,
                   sp.note, sp.velocity, sp.probability,
                   autos, auto_count);
    *entry = (uint16_t)(trigger_bits | PAT_ADDR_SPECIALS_BIT | new_offset);

    if (pat_poolOffsetValid(offset))
        pat_poolFree(r, offset, old_chunks);

    pat_markSceneDirty(scene_index);
    return 1u;
}

/*
 * Remove one automation entry from a step by its 9-bit target.
 *
 * What: read-modify-write: find the matching entry, shift remaining
 * entries down, rewrite with auto_count - 1. If the block becomes empty
 * (no specials and no automations), remove it entirely. Uses allocate-first
 * when the block shrinks to a smaller chunk count; falls back to in-place
 * overwrite if the smaller allocation somehow fails.
 *
 * Inputs: Scene/track/step, 9-bit target. Output: 1 if found and removed,
 *   0 if not found or bad coords. Trigger bit preserved.
 * Affiliates: pat_removeTrackAutomationByTarget(), step-edit `del` action.
 */
uint8_t pat_removeStepAutomation(uint8_t scene_index, uint8_t track,
                                 uint8_t step, uint16_t target)
{
    uint16_t *entry;
    uint16_t addr;
    uint16_t offset;
    uint16_t trigger_bits;
    pat_scene_region_t *r;
    pat_step_specials_t sp;
    pat_automation_entry_t autos[63];
    uint8_t auto_count;
    uint8_t old_flags;
    uint8_t old_chunks;
    uint8_t new_chunks;
    uint16_t new_offset;
    uint8_t i;
    uint8_t found_idx;

    entry = pat_addrPtr(scene_index, track, step);
    if (!entry)
        return 0u;
    addr = *entry;
    if ((addr & PAT_ADDR_SPECIALS_BIT) == 0u)
        return 0u;
    offset = (uint16_t)(addr & PAT_ADDR_OFFSET_MASK);
    if (!pat_poolOffsetValid(offset))
        return 0u;

    r = &pat_regions[scene_index];
    trigger_bits = (uint16_t)(addr & PAT_ADDR_TRIGGER_BIT);
    target &= 0x01FFu;

    sp = pat_blockRead(r, offset);
    old_flags = sp.flags;
    auto_count = pat_blockReadAutomations(r, offset, old_flags, autos, 63u);

    found_idx = 0xFFu;
    for (i = 0u; i < auto_count; i++) {
        if ((autos[i].target & 0x01FFu) == target) {
            found_idx = i;
            break;
        }
    }
    if (found_idx == 0xFFu)
        return 0u;

    for (i = found_idx; i + 1u < auto_count; i++)
        autos[i] = autos[i + 1u];
    auto_count--;

    if (auto_count == 0u && old_flags == 0u) {
        old_chunks = pat_blockChunks(0u, auto_count + 1u);
        pat_poolFree(r, offset, old_chunks);
        *entry = (uint16_t)(trigger_bits | PAT_ADDR_SENTINEL);
        pat_markSceneDirty(scene_index);
        return 1u;
    }

    old_chunks = pat_blockChunks(old_flags, auto_count + 1u);
    new_chunks = pat_blockChunks(old_flags, auto_count);

    if (new_chunks == old_chunks) {
        pat_blockWrite(r, offset, track, step, old_flags,
                       sp.note, sp.velocity, sp.probability,
                       autos, auto_count);
        *entry = (uint16_t)(trigger_bits | PAT_ADDR_SPECIALS_BIT | offset);
        pat_markSceneDirty(scene_index);
        return 1u;
    }

    new_offset = pat_poolAlloc(r, new_chunks);
    if (new_offset == PAT_ADDR_SENTINEL) {
        pat_blockWrite(r, offset, track, step, old_flags,
                       sp.note, sp.velocity, sp.probability,
                       autos, auto_count);
        *entry = (uint16_t)(trigger_bits | PAT_ADDR_SPECIALS_BIT | offset);
        pat_markSceneDirty(scene_index);
        return 1u;
    }

    pat_blockWrite(r, new_offset, track, step, old_flags,
                   sp.note, sp.velocity, sp.probability,
                   autos, auto_count);
    *entry = (uint16_t)(trigger_bits | PAT_ADDR_SPECIALS_BIT | new_offset);
    pat_poolFree(r, offset, old_chunks);
    pat_markSceneDirty(scene_index);
    return 1u;
}

/*
 * Remove all entries matching one target from an entire track.
 *
 * What: iterate all 128 steps and call pat_removeStepAutomation() for each
 * that has a matching entry. Why: the step-edit `clr` action removes a
 * stale or unwanted target across the whole track so the user does not
 * visit each step individually. Each step is an independent pool
 * read-modify-write; the full scan completes in microseconds (pure SRAM).
 *
 * Inputs: Scene/track, 9-bit target. Output: count of entries removed.
 * Affiliates: step-edit menu `clr` action.
 */
uint8_t pat_removeTrackAutomationByTarget(uint8_t scene_index, uint8_t track,
                                          uint16_t target)
{
    uint8_t s;
    uint8_t removed = 0u;

    if (!scene_indexValid(scene_index) || !pat_trackValid(track))
        return 0u;
    for (s = 0u; s < NUM_STEPS; s++) {
        if (pat_removeStepAutomation(scene_index, track, s, target))
            removed++;
    }
    return removed;
}
```

---

### 7.3 Phase C — Target Validation and Legacy Cleanup

#### C1. Extend `instrumentManager_targetValid()`

**File:** `Core/DSP/Instruments/InstrumentManager.c`
**Lines:** 728–732
**Operation:** MODIFY

**Current code (lines 728–732):**
```c
    const kit_instrument_slot_t *slot;
    const ParamDescriptor *descriptor;
    uint8_t target_slot;
    if (!instrumentParam_isVoiceParameter(id))
        return 0u;
```

**Replacement:**
```c
    const kit_instrument_slot_t *slot;
    const ParamDescriptor *descriptor;
    uint8_t target_slot;
    /*
     * Scene-range automation target gate.
     *
     * What: accept Scene mod targets (IDs >= INSTRUMENT_VOICE_ID_COUNT)
     * for automation use by delegating to the Scene target namespace.
     * Why: the voice-parameter gate below rejects all IDs outside the
     * per-slot descriptor range, but Scene targets (Morph, Decimation,
     * Slot6 Decay, future FX) are valid automation destinations in a
     * different ID namespace. Presence in the Scene target table is
     * sufficient for automation validity; there is no AUTOMATION flag
     * in scene_mod_target_use_t yet.
     *
     * Inputs: target ID in the Scene namespace (384..511).
     * Output: nonzero when the ID exists in the Scene target table.
     * Affiliates: sceneModTarget_isSceneTarget(), SceneModTargets.h,
     *   step-edit parameter picker, sequencer playback drain.
     */
    if (!instrumentParam_isVoiceParameter(id)) {
        if (use == INSTRUMENT_TARGET_AUTOMATION &&
            id >= INSTRUMENT_VOICE_ID_COUNT &&
            id < INSTRUMENT_TOTAL_ID_COUNT) {
            return sceneModTarget_isSceneTarget(id);
        }
        return 0u;
    }
```

Lines 733+ remain unchanged.

InstrumentManager.c already includes `"SceneModTargets.h"` (line 6), so
no new include is needed.

---

#### C2. Remove legacy stubs and menu integration

**C2-1. PatternData.c lines 795–798:** REMOVE (replaced by Phase B functions above).

**C2-2. PatternData.h lines 201–206:** REMOVE (replaced by Phase B declarations above).

**C2-3. menu.c lines 9573–9623:** REMOVE the five case handlers
(`PAR_AUTOM_TRACK`, `PAR_P1_DEST`, `PAR_P2_DEST`, `PAR_P1_VAL`, `PAR_P2_VAL`).

**C2-4. menuPages.h line 84:** MODIFY — replace the four legacy parameter IDs:

**Current:**
```c
  {TEXT_STEP_VELOCITY,TEXT_NOTE,TEXT_PROBABILITY,TEXT_SKIP,TEXT_PARAM_DEST,TEXT_PARAM_VAL,TEXT_PARAM_DEST,TEXT_PARAM_VAL, PAR_STEP_VOLUME,PAR_STEP_NOTE,PAR_STEP_PROB,PAR_NONE,PAR_P1_DEST,PAR_P1_VAL,PAR_P2_DEST,PAR_P2_VAL},
```

**Replacement:**
```c
  {TEXT_STEP_VELOCITY,TEXT_NOTE,TEXT_PROBABILITY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY, PAR_STEP_VOLUME,PAR_STEP_NOTE,PAR_STEP_PROB,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE},
```

**C2-5. menuPages.h line 115:** MODIFY — replace `PAR_AUTOM_TRACK`:

**Current:**
```c
  {TEXT_AUTOMATION_TRACK,TEXT_QUANTISATION,..., PAR_AUTOM_TRACK,PAR_QUANTISATION,...},
```

**Replacement:**
```c
  {TEXT_QUANTISATION,TEXT_EMPTY,..., PAR_QUANTISATION,PAR_NONE,...},
```

**Preserved items (dead but harmless):**
- `ParameterArray.h` enum members `PAR_AUTOM_TRACK`, `PAR_P1_DEST`, `PAR_P2_DEST`,
  `PAR_P1_VAL`, `PAR_P2_VAL` — keep to preserve enum numbering.
- `menu.c` parameterTypes entries and `DTYPE_AUTOM_TARGET` rendering branch —
  dead code (never reached after menuPages change), removable in cleanup.

---

### 7.4 Phase D — Step-Edit Menu Automation Pages

#### D0. Architectural approach

The automation pages use **custom rendering** that bypasses the static
`menuPages[][]` table. This matches the precedent set by VOICE pages,
which already use instrument-descriptor-driven custom rendering.

When the user is on SEQ_PAGE subpage 1 (step edit) and scrolls right past
the last specials parameter (probability, activeParameter 2), the menu
enters automation page mode. A new `menu_stepAutoPageIndex` state variable
tracks which automation entry is displayed (0 = first entry,
`auto_count` = add page). Scrolling left from automation page 0 returns
to the specials view.

All automation display and editing is handled by dedicated functions in
menu.c. The static page table's positions 3–7 on SEQ_PAGE subpage 1 are
`PAR_NONE`/`TEXT_EMPTY` (per C2-4) and serve as a hard stop so the normal
traversal cannot enter them.

---

#### D1. New menu state variables

**File:** `Core/Menu/menu.c`
**Location:** ADD near existing static menu state (after line 1133)
**Operation:** ADD

```c
/*
 * Step-edit automation page state.
 *
 * What: menu_stepAutoPageIndex tracks which automation entry is displayed
 * on the dynamic step-edit automation pages (0 = first entry,
 * auto_count = add page). menu_stepAutoDeleteMode toggles between del (0)
 * and clr (1) for item 0. menu_stepAutoActive is nonzero when the step
 * edit subpage has scrolled into the automation region.
 *
 * Why: automation pages are dynamically generated from PatternData pool
 * blocks, not from the static menuPages table. These variables hold the
 * navigation and edit state that the static page system provides for
 * fixed parameters.
 *
 * Inputs: set by encoder scroll, pot turns, step selection changes.
 * Output: read by the automation page renderer and edit handlers.
 * Affiliates: pat_stepAutomationCount(), pat_readStepAutomations(),
 *   pat_writeStepAutomation(), pat_removeStepAutomation(),
 *   pat_removeTrackAutomationByTarget().
 */
static uint8_t menu_stepAutoPageIndex = 0u;
static uint8_t menu_stepAutoDeleteMode = 0u;
static uint8_t menu_stepAutoActive = 0u;
```

---

#### D2. Automation page reset on step change

**File:** `Core/Menu/menu.c`
**Line:** 9820 (PAR_ACTIVE_STEP handler)
**Operation:** MODIFY

**Current:**
```c
        pat_applyStepToMenu(menu_getViewedPattern(), menu_getActiveVoice(), value);
```

**Replacement:**
```c
        pat_applyStepToMenu(menu_getViewedPattern(), menu_getActiveVoice(), value);
        menu_stepAutoPageIndex = 0u;
        menu_stepAutoDeleteMode = 0u;
        menu_stepAutoActive = 0u;
```

Also in `menu_showStepEditPage()` at line 9994, add after `menu_endlessPotMappingChanged()`:
```c
    menu_stepAutoPageIndex = 0u;
    menu_stepAutoDeleteMode = 0u;
    menu_stepAutoActive = 0u;
```

---

#### D3. Automation page rendering function

**File:** `Core/Menu/menu.c`
**Location:** ADD before `menu_repaintGeneric()` (before line 6871)
**Operation:** ADD

```c
/*
 * Render one step-edit automation page on the LCD.
 *
 * What: display one automation entry (or the add page) using four
 * columns: index/action, voice, parameter, amount. Why: automation pages
 * are dynamically generated from pool data, not from the static menuPages
 * table, so they need a dedicated renderer.
 *
 * Layout (16 chars × 2 rows):
 *   Top:    "nnn voi par amt"  or  "add voi par amt"
 *   Bottom: " del  1  wav 064"  (existing entry)
 *           " add off off off"  (add page)
 *
 * When the stored target does not resolve to a valid descriptor on the
 * current instrument, the parameter column shows "inv" (invalid).
 *
 * Inputs: menu_stepAutoPageIndex, current step/track/scene, menu_stepAutoDeleteMode.
 * Output: editDisplayBuffer is filled; lcd_setString queues the display.
 * Affiliates: pat_readStepAutomations(), pat_stepAutomationCount(),
 *   instrumentManager_descriptor(), instrumentParam_slot(),
 *   instrumentParam_local(), scene_instrumentSlotConst().
 */
static void menu_repaintStepAutomation(void)
{
    uint8_t scene = menu_getViewedPattern();
    uint8_t track = menu_getActiveVoice();
    uint8_t step_idx = parameter_values[PAR_ACTIVE_STEP];
    uint8_t auto_count = pat_stepAutomationCount(scene, track, step_idx);
    uint8_t page = menu_stepAutoPageIndex;
    pat_automation_entry_t autos[63];
    uint8_t read_count;
    uint8_t i;

    memset(&editDisplayBuffer[0][0], ' ', 16);
    memset(&editDisplayBuffer[1][0], ' ', 16);

    if (page >= auto_count) {
        /* Add page */
        memcpy(&editDisplayBuffer[0][0], "add", 3);
        memcpy(&editDisplayBuffer[0][4], "voi", 3);
        memcpy(&editDisplayBuffer[0][8], "par", 3);
        memcpy(&editDisplayBuffer[0][12], "amt", 3);
        memcpy(&editDisplayBuffer[1][1], "add", 3);
        memcpy(&editDisplayBuffer[1][5], "off", 3);
        memcpy(&editDisplayBuffer[1][9], "off", 3);
        memcpy(&editDisplayBuffer[1][13], "off", 3);
    } else {
        read_count = pat_readStepAutomations(scene, track, step_idx,
                                              autos, 63u);
        if (page < read_count) {
            pat_automation_entry_t *ae = &autos[page];
            uint16_t tgt = (uint16_t)(ae->target & 0x01FFu);
            uint8_t slot = instrumentParam_slot(tgt);
            uint8_t local = instrumentParam_local(tgt);
            const kit_instrument_slot_t *kit_slot;
            const ParamDescriptor *desc;
            char par_text[4] = "inv";

            /* Top row: index, labels */
            numtostru(&editDisplayBuffer[0][0], page);
            memcpy(&editDisplayBuffer[0][4], "voi", 3);
            memcpy(&editDisplayBuffer[0][8], "par", 3);
            memcpy(&editDisplayBuffer[0][12], "amt", 3);
            if (page + 1u < auto_count)
                editDisplayBuffer[0][15] = '>';

            /* Bottom row item 0: del or clr */
            if (menu_stepAutoDeleteMode)
                memcpy(&editDisplayBuffer[1][1], "clr", 3);
            else
                memcpy(&editDisplayBuffer[1][1], "del", 3);

            /* Bottom row item 1: voice (1-based) */
            if (instrumentParam_isVoiceParameter(tgt)) {
                numtostru(&editDisplayBuffer[1][5],
                          (uint8_t)(slot + 1u));
            } else {
                memcpy(&editDisplayBuffer[1][5], "scn", 3);
            }

            /* Bottom row item 2: parameter short name or inv */
            if (instrumentParam_isVoiceParameter(tgt)) {
                kit_slot = scene_instrumentSlotConst(scene, slot);
                desc = kit_slot ? instrumentManager_descriptor(
                    kit_slot->type, local) : NULL;
                if (desc && desc->short_name) {
                    for (i = 0u; i < 3u && desc->short_name[i]; i++)
                        par_text[i] = desc->short_name[i];
                }
            } else if (sceneModTarget_isSceneTarget(tgt)) {
                const scene_mod_target_descriptor_t *smt =
                    sceneModTarget_descriptor(tgt);
                if (smt && smt->short_name) {
                    for (i = 0u; i < 3u && smt->short_name[i]; i++)
                        par_text[i] = smt->short_name[i];
                }
            }
            editDisplayBuffer[1][9]  = par_text[0];
            editDisplayBuffer[1][10] = par_text[1];
            editDisplayBuffer[1][11] = par_text[2];

            /* Bottom row item 3: amount 0..127 */
            numtostru(&editDisplayBuffer[1][13], ae->value);
        }
    }
}
```

---

#### D4. Integration into rendering path

**File:** `Core/Menu/menu.c`
**Function:** `menu_repaintGeneric()` (line 6871)
**Operation:** MODIFY — add early return for automation pages

**Add at the top of `menu_repaintGeneric()` (after line 6877, inside the
`if (editModeActive)` block):**
```c
        if (menu_activePage == SEQ_PAGE && menu_stepAutoActive) {
            menu_repaintStepAutomation();
            return;
        }
```

---

#### D5. Navigation: encoder scroll into/out of automation

**File:** `Core/Menu/menu.c`
**Function:** The encoder-turn handler for SEQ_PAGE step edit subpage.
This is in `menu_moveToMenuItem()` or the encoder scroll handler near
lines 7143–7243.
**Operation:** MODIFY — intercept right-scroll past probability (parameter 2)
and left-scroll from automation page 0.

When on SEQ_PAGE subpage 1 and `activeParameter == 2` (probability) and
direction > 0:
- Enter automation mode: `menu_stepAutoActive = 1; menu_stepAutoPageIndex = 0;`
- Suppress the normal parameter increment.

When `menu_stepAutoActive` and direction > 0:
- `menu_stepAutoPageIndex++` (capped at `auto_count` = add page).

When `menu_stepAutoActive` and direction < 0:
- If `menu_stepAutoPageIndex > 0`: decrement.
- If `menu_stepAutoPageIndex == 0`: exit automation mode, return to specials
  (`menu_stepAutoActive = 0; menuIndex = (1u << PAGE_SHIFT) | 2u;`).

The exact insertion point depends on how the encoder handler dispatches
for SEQ_PAGE; the handler at line ~7143 switches on `activeParameter` and
`activePage`. A new guard at the top of that handler checks
`menu_stepAutoActive` and delegates to automation navigation before the
normal parameter logic runs.

---

#### D6. Pot editing on automation pages

**File:** `Core/Menu/menu.c`
**Function:** The pot-value handler for SEQ_PAGE. Currently, pot changes
call `menu_parseParameter()` which routes through the static page table.
**Operation:** MODIFY — when `menu_stepAutoActive`, intercept pot changes.

The pot handler checks which of the four columns (items 0–3) the pot
corresponds to. Because automation pages show four items across four
endless pots:

- **Pot 0 (item 0):** toggle `menu_stepAutoDeleteMode` between 0 (del) and
  1 (clr). No PatternData write.
- **Pot 1 (item 1):** cycle voice 1..6 (slot 0..5). Re-resolve the target:
  `target = instrumentParam_make(new_slot, old_local)`. Call
  `pat_removeStepAutomation()` for old target, then
  `pat_writeStepAutomation()` for new. If the new slot has no valid
  descriptor at `old_local`, snap to the first available parameter.
- **Pot 2 (item 2):** cycle automatable parameter for the selected voice.
  Use `instrumentManager_stepTargetForSlot(scene, slot, current, direction,
  INSTRUMENT_TARGET_AUTOMATION)` which already skips non-automatable
  descriptors. Additionally filter out targets already present in other
  entries on this step (uniqueness): before accepting a candidate, scan
  the step's other entries to ensure no duplicate.
- **Pot 3 (item 3):** adjust value 0..127. Call
  `pat_writeStepAutomation(scene, track, step, target, new_value)`.

On the **add page**, any pot adjustment away from `off` creates a new
entry with defaults (see D7 below).

---

#### D7. Encoder click behavior

**File:** `Core/Menu/menu.c`
**Function:** The encoder-press handler for SEQ_PAGE.
**Operation:** MODIFY — when `menu_stepAutoActive`, handle add/del/clr.

On an existing entry page (page < auto_count), encoder click on item 0:
- If `menu_stepAutoDeleteMode == 0` (del): call
  `pat_removeStepAutomation(scene, track, step, current_target)`. Adjust
  page index: if page >= new auto_count, show page = max(0, new auto_count - 1)
  or the add page if none remain.
- If `menu_stepAutoDeleteMode == 1` (clr): call
  `pat_removeTrackAutomationByTarget(scene, track, current_target)`. Re-read
  auto_count. Set page = min(old page, new auto_count).

On the add page (page == auto_count), encoder click:
- Determine default voice slot = track index (tracks 0..5 map to slots 0..5;
  track 6 maps to slot 5). Verify with `menu_getActiveVoice()`.
- Determine default parameter = first automatable parameter from
  `instrumentManager_stepTargetForSlot(scene, slot, INSTRUMENT_PARAM_INVALID,
  +1, INSTRUMENT_TARGET_AUTOMATION)`.
- If no valid parameter available, fail silently (no entry created).
- Determine default value: read from Scene image at
  `scene_instrumentSlotConst(scene, slot)->parameter_images
  .instrument_parameters[local]`, convert 8-bit to 7-bit:
  `(v >= 255u) ? 127u : (uint8_t)(v / 2u)`.
- Call `pat_writeStepAutomation(scene, track, step, target, value)`.
- Transform page to normal editing page (page index stays at what was
  auto_count, now showing the new entry).

---

### 7.5 Phase E — Sequencer Playback

#### E0. config.h constants

**File:** `config.h`
**Location:** ADD after the autosave trace constants (after line 388)
**Operation:** ADD

```c
/*
 * Pending step-event buffer capacity.
 *
 * What: maximum number of 4-byte records in the ISR-to-foreground pending
 * buffer in sequencer.c. Why: adjustable here so bench builds can enlarge
 * the buffer to stress-test the future stack servicer's deferred-write
 * path without editing sequencer.c.
 *
 * Inputs: none (compile-time). Output: buffer array size in sequencer.c.
 * Budget: SEQ_PENDING_BUF_COUNT * 4 + 2 control bytes. At 128, that is
 * 514 bytes in SRAM1 .bss.
 * Affiliates: sequencer.c (buffer owner), seq_drainPendingAutomation().
 */
#define SEQ_PENDING_BUF_COUNT 128u

/*
 * Pattern trace retained ring capacity for the DEV_MODE_LOGGING-only
 * diagnostic file `pattrace.bin`.
 *
 * What: number of 8-byte trace records retained in SRAM before filesystem
 * flush. Why: the pattern pending buffer silently drops entries on
 * overflow; the trace ring captures those drops and any future pool
 * servicer anomalies so card-side evidence is available for debugging.
 *
 * Inputs: none (compile-time). Output: PatternTrace.c ring size.
 * Budget: PAT_TRACE_RECORD_COUNT * 8 bytes SRAM1 (256 bytes at 32).
 * This SRAM exists only when DEV_MODE_LOGGING is 1.
 * Affiliates: PatternTrace.h/c, filesystem.c pattrace.bin drain.
 */
#define PAT_TRACE_RECORD_COUNT 32u

/*
 * Minimum idle interval between background pattern-trace append attempts.
 * Same role as AUTOSAVE_TRACE_FLUSH_INTERVAL_MS for asavetrc.bin.
 */
#define PAT_TRACE_FLUSH_INTERVAL_MS 1000u
```

---

#### E1. Pending step-event buffer

**File:** `Core/Sequencer/sequencer.c`
**Location:** ADD near existing statics (after line 111)
**Operation:** ADD

```c
/*
 * Pending step-event buffer for foreground application.
 *
 * What: a flat append-only array that the TIM3 ISR fills with 4-byte
 * step-event records and the foreground main loop drains. Each record
 * pairs a step identity word with a payload word, supporting both
 * automation entries (current) and specials events (future stack servicer).
 *
 * Record format (4 bytes, all little-endian):
 *   Word 0 — identity:
 *     bits  9..0 : step_id (track * NUM_STEPS + step, 0..895)
 *     bit  10    : type (0 = special, 1 = automation)
 *     bits 15..11: reserved (zero)
 *   Word 1 — payload:
 *     automation (type=1): bits 15..9 = 7-bit value, bits 8..0 = 9-bit
 *       target. This is the raw pool wire format — the ISR copies the
 *       packed 2-byte entry from the pool block directly into this word,
 *       avoiding any unpack/repack overhead.
 *     special (type=0, future): bits 15..8 = 8-bit value,
 *       bits 7..0 = special subtype (0=note, 1=vel, 2=prob).
 *
 * Why: instrumentManager_writeRuntime() is NOT ISR-safe (strcmp chains,
 * modulation baseline refresh, instance pointer arithmetic) and must run
 * in the foreground. The ISR appends here and sets a drain flag; the
 * foreground applies entries within one main-loop pass (~sub-ms latency).
 *
 * Future stack servicer: during pool defragmentation, the ISR cannot
 * safely read from pool blocks that the foreground is moving. The ISR
 * sets a defrag-active flag (checked before pool reads) and the
 * foreground buffers menu-originated pool writes here instead of writing
 * the pool directly. Both use cases fit the same 4-byte record format.
 * The buffer capacity (SEQ_PENDING_BUF_COUNT, config.h) is sized for
 * either path: 128 records handles 2+ ticks of 7-track automation
 * readout or >4 seconds of continuous maximum-speed menu editing during
 * defrag.
 *
 * Overflow: when the buffer is full, new entries are silently dropped —
 * no on-screen message. The dropped entry is recorded in the pattern
 * trace ring (PatternTrace.h) so overflow events appear in pattrace.bin
 * on the SD card when DEV_MODE_LOGGING is 1.
 *
 * Race safety: the ISR (TIM3, priority 2) is the sole writer. The
 * foreground is the sole reader/drainer. The foreground cannot preempt
 * TIM3. volatile on the drain flag and count is sufficient.
 *
 * RAM cost: SEQ_PENDING_BUF_COUNT * 4 + 2 = 514 bytes at default 128.
 * Region: normal SRAM1 .bss. Owner: sequencer.c. Lifetime: process.
 * Affiliates: seq_advanceTrackStep(), seq_drainPendingAutomation(),
 *   main.c foreground drain call, PatternTrace (overflow logging).
 */

#define SEQ_PENDING_TYPE_SPECIAL    0u
#define SEQ_PENDING_TYPE_AUTOMATION 1u
#define SEQ_PENDING_TYPE_SHIFT      10u
#define SEQ_PENDING_STEP_ID_MASK    0x03FFu

typedef struct {
    uint16_t identity;   /* step_id | (type << 10) */
    uint16_t payload;    /* type-dependent: raw pool word or special+value */
} seq_pending_entry_t;

static seq_pending_entry_t seq_pendingBuf[SEQ_PENDING_BUF_COUNT];
static volatile uint8_t seq_pendingCount = 0u;
static volatile uint8_t seq_pendingDrain = 0u;
```

---

#### E2. ISR automation readout

**File:** `Core/Sequencer/sequencer.c`
**Function:** `seq_advanceTrackStep()` (line 366)
**Location:** ADD after line 410 (after the close of the
`if (pat_isStepActive(...))` block), before line 412 (the roll check).
Must remain INSIDE the `if (!(seq_mutedTracks & (1u << track)))` block.
**Operation:** ADD

```c
        /*
         * Automation readout — fires regardless of trigger state.
         *
         * What: read the address entry for the current step. If a pool
         * block exists (bit 14) with auto_count > 0, copy each raw 2-byte
         * packed entry from the pool into the pending buffer as a 4-byte
         * record. Why: automation must apply on untriggered steps too
         * (SCOPING_TARGETS §4.6). The ISR reads directly from the pool
         * because the public pat_readStepAutomations() uses stack-local
         * arrays unsuitable for the ISR budget. The address-entry read is
         * atomic (16-bit naturally aligned on Cortex-M7).
         *
         * No dedup: with 128 entries (SEQ_PENDING_BUF_COUNT) there is
         * room for ~2 full ticks of 7-track readout. A linear dedup scan
         * would cost O(n) ISR cycles per entry for marginal benefit.
         * Duplicate targets are harmless — last-write-wins in the drain.
         *
         * Overflow: when the buffer is full, the entry is silently
         * dropped and a trace record is emitted to PatternTrace (when
         * DEV_MODE_LOGGING is 1). No on-screen message.
         *
         * Inputs: seq_activePattern, track, seq_stepIndex[track].
         * Output: records appended to seq_pendingBuf[], drain flag set.
         * Affiliates: seq_drainPendingAutomation(), pat_sceneRegion(),
         *   PAT_ADDR_SPECIALS_BIT, PAT_BLOCK_AUTO_COUNT_MASK,
         *   patternTrace_record().
         */
        if (!seq_eraseActive || track != menu_getActiveVoice()) {
            const pat_scene_region_t *rgn =
                pat_sceneRegion(seq_activePattern);
            if (rgn) {
                uint16_t a_addr =
                    rgn->address[track][(uint8_t)seq_stepIndex[track]];
                if ((a_addr & PAT_ADDR_SPECIALS_BIT) != 0u) {
                    uint16_t a_off =
                        (uint16_t)(a_addr & PAT_ADDR_OFFSET_MASK);
                    if (a_off != PAT_ADDR_SENTINEL &&
                        (a_off & 3u) == 0u &&
                        a_off < (uint16_t)(PAT_STACK_SIZE * 32u)) {
                        uint8_t a_cnt = (uint8_t)(
                            rgn->pool[a_off + 1u] &
                            PAT_BLOCK_AUTO_COUNT_MASK);
                        if (a_cnt > 0u) {
                            uint8_t a_flags = (uint8_t)(
                                rgn->pool[a_off + 2u] &
                                PAT_SPECIAL_FLAGS_MASK);
                            uint8_t a_vcnt = 0u;
                            uint8_t a_base;
                            uint8_t ai;
                            uint16_t a_step_id = (uint16_t)(
                                track * NUM_STEPS +
                                (uint8_t)seq_stepIndex[track]);
                            uint16_t a_ident = (uint16_t)(
                                (a_step_id & SEQ_PENDING_STEP_ID_MASK) |
                                (SEQ_PENDING_TYPE_AUTOMATION
                                    << SEQ_PENDING_TYPE_SHIFT));
                            if (a_flags & PAT_SPECIAL_NOTE_BIT) a_vcnt++;
                            if (a_flags & PAT_SPECIAL_VEL_BIT)  a_vcnt++;
                            if (a_flags & PAT_SPECIAL_PROB_BIT) a_vcnt++;
                            a_base = (uint8_t)(3u + a_vcnt);
                            for (ai = 0u; ai < a_cnt; ai++) {
                                uint8_t ab =
                                    (uint8_t)(a_base + ai * 2u);
                                /* Raw packed word from pool — no decode
                                 * needed. The foreground drain unpacks. */
                                uint16_t pk =
                                    (uint16_t)(
                                        (uint16_t)rgn->pool[a_off + ab + 1u]
                                            << 8u) |
                                    (uint16_t)rgn->pool[a_off + ab];
                                if (seq_pendingCount <
                                        SEQ_PENDING_BUF_COUNT) {
                                    seq_pendingBuf[
                                        seq_pendingCount].identity =
                                            a_ident;
                                    seq_pendingBuf[
                                        seq_pendingCount].payload = pk;
                                    seq_pendingCount++;
                                } else {
                                    patternTrace_recordOverflow(
                                        a_ident, pk);
                                }
                            }
                            seq_pendingDrain = 1u;
                        }
                    }
                }
            }
        }
```

The `if (!seq_eraseActive || track != menu_getActiveVoice())` guard
prevents automation readout on the step being erased in record-erase mode,
matching the trigger path's existing guard.

---

#### E3. Foreground drain

**File:** `Core/Sequencer/sequencer.c`
**Location:** ADD at end of file (before closing `#endif` if any)
**Operation:** ADD

Add to sequencer.h:
```c
void seq_drainPendingAutomation(void);
```

In sequencer.c:
```c
/*
 * Drain pending step-event records from the ISR buffer.
 *
 * What: iterate all pending 4-byte records. For automation-type records
 * (type bit = 1), unpack the payload word (pool wire format: bits 15..9 =
 * 7-bit value, bits 8..0 = 9-bit target) and apply via
 * instrumentManager_writeRuntime(). Special-type records (type bit = 0)
 * are skipped — they are reserved for the future stack servicer's
 * deferred-write path. The identity word's step_id field is not used by
 * the current drain (the DSP target is fully specified by the payload's
 * target field); step_id is carried for the future servicer and for
 * diagnostic logging.
 *
 * Why: the buffer is private to sequencer.c; this function is the
 * foreground-safe drain point called once per main-loop pass. Keeping the
 * drain in sequencer.c prevents exposing the buffer statics and
 * concentrates all buffer access (ISR writer + foreground drainer) in one
 * translation unit.
 *
 * Value conversion: 7-bit (0..127) → 8-bit (0..255) using the
 * established MIDI CC formula: (v == 127) ? 255 : v * 2.
 *
 * Inputs: seq_pendingBuf[], seq_pendingCount, seq_pendingDrain.
 * Output: each valid voice-automation target is applied to its DSP owner.
 *   Buffer count and drain flag are cleared. Scene targets deferred.
 * Affiliates: seq_advanceTrackStep() (writer), main.c (caller),
 *   instrumentManager_writeRuntime(), instrumentManager_descriptor(),
 *   instrumentParam_slot(), instrumentParam_local(),
 *   scene_instrumentSlotConst(), scene_getActiveIndex().
 */
void seq_drainPendingAutomation(void)
{
    uint8_t i;
    uint8_t count;

    if (!seq_pendingDrain)
        return;
    count = seq_pendingCount;
    for (i = 0u; i < count; i++) {
        uint16_t ident = seq_pendingBuf[i].identity;
        uint8_t type = (uint8_t)((ident >> SEQ_PENDING_TYPE_SHIFT) & 1u);
        if (type == SEQ_PENDING_TYPE_AUTOMATION) {
            uint16_t pk = seq_pendingBuf[i].payload;
            uint16_t tgt = (uint16_t)(pk & 0x01FFu);
            uint8_t v7 = (uint8_t)((pk >> 9u) & 0x7Fu);
            uint8_t v8 = (v7 == 127u) ? 255u : (uint8_t)(v7 * 2u);
            if (instrumentParam_isVoiceParameter(tgt)) {
                uint8_t slot = instrumentParam_slot(tgt);
                uint8_t local = instrumentParam_local(tgt);
                const kit_instrument_slot_t *ks =
                    scene_instrumentSlotConst(scene_getActiveIndex(),
                                              slot);
                if (ks) {
                    const ParamDescriptor *desc =
                        instrumentManager_descriptor(ks->type, local);
                    if (desc)
                        instrumentManager_writeRuntime(slot, desc, v8);
                }
            }
            /* Scene targets (IDs 384+) deferred to Session 066. */
        }
        /* SEQ_PENDING_TYPE_SPECIAL: future stack servicer path. */
    }
    seq_pendingCount = 0u;
    seq_pendingDrain = 0u;
}
```

**File:** `main.c`
**Location:** ADD after line 1243 (`timebase_serviceFrontPanel();`),
before line 1244 (`audio_check_and_render();`)
**Operation:** ADD

```c
        seq_drainPendingAutomation();
        audio_check_and_render();
```

No new includes needed in main.c — the drain function is declared in
sequencer.h which main.c already includes.

---

#### E4. PatternTrace module

New files following the AutosaveTrace architecture: a DEV_MODE_LOGGING-
gated SRAM ring with peek/advance/dropped-count API, no filesystem I/O.
When DEV_MODE_LOGGING is 0, all functions compile to no-op stubs and no
SRAM is allocated.

**File:** `Core/Bank/Scene/Pattern/PatternTrace.h` (NEW)
**Operation:** ADD

```c
/*
 * PatternTrace.h -- bounded SRAM diagnostic trace for pattern pool events.
 *
 * This module owns a fixed-size ring of 8-byte records and cursor
 * bookkeeping that lets filesystem.c drain them to pattrace.bin on the
 * SD card. It owns no filesystem handle and performs no I/O. It exists
 * to capture pending-buffer overflow events (and future pool servicer
 * anomalies) so card-side evidence is available for debugging.
 *
 * Every API is safe to call unconditionally. When DEV_MODE_LOGGING is 0
 * the implementation supplies no-op/zero-return stubs, so production
 * builds keep no trace SRAM and perform no trace-file I/O while call
 * sites stay simple.
 *
 * Architecture mirrors AutosaveTrace.h: stage(1) + flags(1) + tick16(2)
 * + value32(4) = 8 bytes per record, same peek/advance/dropped interface.
 * Affiliates: PatternTrace.c, filesystem.c (pattrace.bin drain),
 *   sequencer.c (overflow producer), config.h (PAT_TRACE_RECORD_COUNT).
 */
#ifndef PATTERN_TRACE_H_
#define PATTERN_TRACE_H_

#include <stdint.h>

#define PAT_TRACE_RECORD_BYTES 8u

#ifndef PAT_TRACE_RECORD_COUNT
#define PAT_TRACE_RECORD_COUNT 32u
#endif

#define PAT_TRACE_FILENAME "pattrace.bin"

/*
 * Stage codes. Currently only overflow; future sessions add pool servicer
 * stages (defrag start/end, block move, allocation failure, etc.).
 */
typedef enum {
    /*
     * H: pending buffer overflow. An ISR entry was silently dropped.
     * flags: the record type that was dropped (0=special, 1=automation).
     * value32: bits 0..15 = identity word (step_id + type),
     *          bits 16..31 = payload word (the dropped data).
     * Why: captures the exact entry that was lost so the developer can
     * assess whether the buffer count needs to increase.
     */
    PAT_TRACE_STAGE_PENDING_OVERFLOW = 'H',
} pat_trace_stage_t;

/* Record one timestamped event. Safe to call from ISR (uses PRIMASK). */
void patternTrace_record(pat_trace_stage_t stage, uint8_t flags,
                         uint32_t value);

/*
 * Convenience wrapper called from the ISR overflow path. Packs the
 * identity and payload words into value32 and emits a PENDING_OVERFLOW
 * record. When DEV_MODE_LOGGING is 0 this compiles to nothing.
 */
void patternTrace_recordOverflow(uint16_t identity, uint16_t payload);

/* Return the bounded number of records not yet acknowledged durable. */
uint16_t patternTrace_pendingCount(void);
/* Copy one pending record by oldest-relative index. */
uint8_t patternTrace_peekRecord(uint16_t index,
                                uint8_t out[PAT_TRACE_RECORD_BYTES]);
/* Acknowledge records whose serialized bytes have passed a sync gate. */
void patternTrace_advanceFlushCursor(uint16_t count);
/* Return the saturated count of records overwritten before durable flush. */
uint16_t patternTrace_droppedCount(void);

#endif /* PATTERN_TRACE_H_ */
```

**File:** `Core/Bank/Scene/Pattern/PatternTrace.c` (NEW)
**Operation:** ADD

Implementation mirrors `AutosaveTrace.c` exactly: a volatile ring of
`PAT_TRACE_RECORD_COUNT` 8-byte records, a write cursor, a flush cursor,
a dropped counter, and PRIMASK-based ISR safety. All functions compile to
stubs when `DEV_MODE_LOGGING` is 0.

`patternTrace_recordOverflow()` is:
```c
void patternTrace_recordOverflow(uint16_t identity, uint16_t payload)
{
#if DEV_MODE_LOGGING
    uint8_t type = (uint8_t)((identity >> 10u) & 1u);
    uint32_t value = (uint32_t)identity |
                     ((uint32_t)payload << 16u);
    patternTrace_record(PAT_TRACE_STAGE_PENDING_OVERFLOW, type, value);
#else
    (void)identity;
    (void)payload;
#endif
}
```

---

#### E5. filesystem.c — pattrace.bin drain

**File:** `Core/Hardware/SD/filesystem.c`
**Operation:** MODIFY — add a new drain state machine following the
autosaveTraceFlush pattern.

This is a direct structural clone of the existing `asavetrc.bin` drain
(lines 5232–5318). The changes are:

1. **New static forward declarations** (after line 1450):
```c
static void filesystem_patternTraceFlush_tick(void);
static void filesystem_patternTraceFlushSchedule_tick(void);
static void filesystem_patternTraceFlushCompleted(void);
```

2. **New internal operation enum value**: add `FS_INTERNAL_OP_PATTERN_TRACE_FLUSH`
   to the `fs_internal_op_t` enum (after the autosave trace flush entry).

3. **New flush state machine** (`filesystem_patternTraceFlush_tick`):
   Same 4-phase structure as `autosaveTraceFlush_tick`:
   - Phase 0: chdir root, snapshot pending count, serialize, open
     `PAT_TRACE_FILENAME` in append mode
   - Phase 1: wait for file open
   - Phase 2: stream the snapshot, handle full-media error
   - Phase 3: wait close + sync, advance flush cursor
   - Phase 4: error-handle close

4. **Scheduler** (`filesystem_patternTraceFlushSchedule_tick`):
   Same debounce pattern as autosave trace: check
   `PAT_TRACE_FLUSH_INTERVAL_MS` elapsed since last attempt, check
   `patternTrace_pendingCount() > 0`, start the flush operation if the
   filesystem scheduler is idle.

5. **Integration into `filesystem_tick()`**: call
   `filesystem_patternTraceFlushSchedule_tick()` from the same scheduling
   section that calls `filesystem_autosaveTraceFlushSchedule_tick()`, gated
   on `DEV_MODE_LOGGING`.

6. **Include**: add `#include "PatternTrace.h"` to filesystem.c's includes.

The drain uses the same `staging_buf` and `on_file_opened`/`on_file_closed`
callbacks as the autosave trace drain — these are shared infrastructure in
filesystem.c. The two drains never run concurrently because the filesystem
scheduler serializes all operations.

---

### 7.6 Summary of All Changed Files

| File | Lines | Operation | Phase |
|------|-------|-----------|-------|
| `config.h` | 388+ | ADD `SEQ_PENDING_BUF_COUNT`, `PAT_TRACE_*` | E0 |
| `PatternData.h` | 168+ | ADD `pat_automation_entry_t` | B-TYPE |
| `PatternData.h` | 201–206 | REMOVE old stubs, ADD new decls | B-DECL, C2-2 |
| `PatternData.c` | 261–275 | MODIFY `pat_blockChunks` | A1 |
| `PatternData.c` | 287–317 | MODIFY `pat_blockWrite` | A2 |
| `PatternData.c` | 356+ | ADD `pat_blockReadAutomations` | A3 |
| `PatternData.c` | 370–428 | MODIFY `pat_writeSpecials` | A4 |
| `PatternData.c` | 575 | MODIFY `pat_eraseStep` chunk calc | A5 |
| `PatternData.c` | 662 | MODIFY `pat_clearTrack` chunk calc | A6 |
| `PatternData.c` | 795–798 | REMOVE stubs, ADD 5 new publics | B1–B5, C2-1 |
| `InstrumentManager.c` | 728–732 | MODIFY `targetValid` | C1 |
| `menu.c` | 1133+ | ADD auto page state vars | D1 |
| `menu.c` | 6871+ | ADD `menu_repaintStepAutomation()` | D3 |
| `menu.c` | 6877+ | MODIFY `menu_repaintGeneric()` | D4 |
| `menu.c` | 7143+ | MODIFY encoder scroll handler | D5 |
| `menu.c` | 9573–9623 | REMOVE legacy cases | C2-3 |
| `menu.c` | 9820 | MODIFY PAR_ACTIVE_STEP handler | D2 |
| `menu.c` | 9994+ | MODIFY `menu_showStepEditPage()` | D2 |
| `menuPages.h` | 84 | MODIFY SEQ_PAGE subpage 1 | C2-4 |
| `menuPages.h` | 115 | MODIFY RECORDING_PAGE | C2-5 |
| `PatternTrace.h` | NEW | ADD trace header | E4 |
| `PatternTrace.c` | NEW | ADD trace ring implementation | E4 |
| `sequencer.c` | 111+ | ADD pending buffer types/vars | E1 |
| `sequencer.c` | 410+ | ADD ISR automation readout | E2 |
| `sequencer.c` | EOF | ADD `seq_drainPendingAutomation()` | E3 |
| `sequencer.h` | EOF | ADD drain declaration | E3 |
| `filesystem.c` | 1450+ | ADD pattern trace flush state machine | E5 |
| `main.c` | 1243+ | ADD drain call | E3 |

**Total new static RAM (always present):**
~514 bytes — pending step-event buffer in SRAM1 .bss
(SEQ_PENDING_BUF_COUNT × 4 + 2 control = 128 × 4 + 2 = 514 bytes).

**DEV_MODE_LOGGING-only SRAM (not in production builds):**
~256 bytes — PatternTrace ring (PAT_TRACE_RECORD_COUNT × 8 = 32 × 8).

Per `SRAM_MANIFEST.md` policy, user acknowledgement required before
implementation. The 514-byte pending buffer is unconditional; the 256-byte
trace ring exists only in logging builds.

**Stack impact:** deepest new allocation is `pat_automation_entry_t[63]`
= 252 bytes in foreground call paths (`pat_writeStepAutomation`,
`pat_removeStepAutomation`, `pat_writeSpecials`, `menu_repaintStepAutomation`).
No ISR stack growth — the ISR reads directly from the pool and appends
4-byte records to the pending buffer without any stack-local arrays.

---

## 8. Session 065 Implementation Notes

### 8.1 Work completed

- Read `MEMORY.md`, the parent automation plan, and the complete implementation
  specification above before editing. The implementation follows the resolved
  Session-062 architecture: PatternData owns resident pool blocks, TIM3 only
  publishes raw automation words, and the foreground owns runtime writes.
- Extended the dynamic block layout and allocator accounting for up to 63
  packed automation entries. Special edits preserve automation entries, while
  erase, clear, removal, and reallocation free the complete block.
- Replaced the four legacy PatternData no-op stubs with count/read/write/remove
  APIs, extended Scene-range automation validation, and removed the legacy
  step-page `modTargets[]` handlers from the active menu path.
- Added Method 1 custom STEP automation rendering and navigation: fixed
  probability remains the entry point, pages expose `del`/`clr`, voice/
  parameter/value pots, uniqueness filtering, Add defaults, stale `inv`
  display, and track-wide clear.
- Added the 128-record, four-byte TIM3-to-foreground pending queue. Voice
  automation is validated and applied in the main loop with the settled
  7-bit-to-8-bit conversion; Scene target runtime application remains deferred
  to Session 066.
- Added the DEV-only 32-record PatternTrace ring and the asynchronous
  `pattrace.bin` append path. Pending-buffer overflow uses the specified H-stage
  wrapper, and trace records are acknowledged only after close and sync.

### 8.2 Resource accounting and verification

- New unconditional static allocation: 128 × 4-byte pending records plus two
  control bytes = 514 bytes in SRAM1 `.bss`.
- New `DEV_MODE_LOGGING` allocation: 32 × 8-byte PatternTrace records = 256
  bytes, plus cursor bookkeeping. No PatternTrace ring exists in production
  builds.
- All new and modified C/H code paths have adjacent comment-block
  descriptions, including the public API declarations and PatternTrace
  production stubs.
- `make -j2` and `make img` completed successfully after the implementation
  and the final contract corrections. The resulting image was
  `build/LXRV2_lxr02.img` (432352 bytes). The remaining diagnostics are the
  existing packed member and embedded-libc syscall warnings.

### 8.3 Deferred validation

Hardware workflow, audio playback, instrument-swap stale-entry cleanup,
PAT4 round-trip, and pool-stress checks remain hardware/integration tests. The
VOICE overlay, CGRAM underline, step illumination, async track-wide search,
and Scene-target runtime application remain explicitly deferred to Session 066.

---

## 9. Post-Implementation Code Assessment

Audit performed against the plan in §7 and the diff at the head of
`dev-ph4-pattern` (parent `ed4a2db`). All 13 changed files reviewed.

### 9.1 Phase A — Pool Block Infrastructure

| Item | Plan | Implementation | Status |
|------|------|----------------|--------|
| A1 `pat_blockChunks` | Add `auto_count` param | Matches. Adds defensive clamp to `PAT_BLOCK_AUTO_COUNT_MASK`. | OK+ |
| A2 `pat_blockWrite` | Add `autos, auto_count` params, pack entries LE | Matches. Packs auto_count into header word. Adds clamp. | OK+ |
| A3 `pat_blockReadAutomations` | New static after `pat_blockRead` | Matches. Reads flags from `p[2]` directly (plan passed as arg). Adds pool-boundary bounds check. | OK+ |
| A4 `pat_writeSpecials` rewrite | Allocate-first, preserve automations | Refactored into `pat_writeDynamic()` + thin `pat_writeSpecials` wrapper. Better design — see §9.6. | OK+ |
| A5 `pat_eraseStep` | Chunk calc with auto_count | Matches. | OK |
| A6 `pat_clearTrack` | Chunk calc with auto_count | Matches. | OK |

**Extra A changes not in plan:**
- `pat_blockRead()` gained a bounds check: rejects blocks that extend past
  pool end. Defensive; no functional change to existing specials path.
- `pat_writeDynamic` introduced `allow_shrink_in_place` parameter for the
  removal edge case where pool is full but block is shrinking. Plan did not
  address this scenario. Correct: writes the shorter block at the same
  offset and frees the surplus tail chunks.

### 9.2 Phase B — Public Automation APIs

| Item | Plan | Implementation | Status |
|------|------|----------------|--------|
| B-TYPE `pat_automation_entry_t` | After line 168 | Matches. | OK |
| B-DECL declarations | Replace lines 201–206 | Matches. | OK |
| B1 `pat_stepAutomationCount` | Pool read, return header bits 5..0 | Adds full bounds check (block must fit pool). | OK+ |
| B2 `pat_readStepAutomations` | Delegate to `pat_blockReadAutomations` | Matches. | OK |
| B3 `pat_writeStepAutomation` | Read-modify-write, allocate-first | Delegates to `pat_writeDynamic`. Adds `instrumentManager_targetValid()` pre-check (plan had it in caller). | OK+ |
| B4 `pat_removeStepAutomation` | Find, shift, rewrite | Delegates to `pat_writeDynamic` with `allow_shrink_in_place=1`. | OK+ |
| B5 `pat_removeTrackAutomationByTarget` | 128-step scan | Matches. | OK |

### 9.3 Phase C — Target Validation and Legacy Cleanup

| Item | Plan | Implementation | Status |
|------|------|----------------|--------|
| C1 `instrumentManager_targetValid` | Scene-range gate for AUTOMATION use | Matches exactly. Uses `sceneModTarget_isSceneTarget()`. | OK |
| C2-1 PatternData.c stubs | Remove lines 795–798 | Removed. | OK |
| C2-2 PatternData.h decls | Remove lines 201–206 | Replaced with new API decls. | OK |
| C2-3 menu.c legacy handlers | Remove PAR_AUTOM_TRACK/P1/P2 cases | Removed. | OK |
| C2-4 menuPages.h SEQ_PAGE | Positions 3–7 → PAR_NONE/TEXT_EMPTY | Matches. Positions 3–7 all PAR_NONE/TEXT_EMPTY. | OK |
| C2-5 menuPages.h RECORDING_PAGE | PAR_AUTOM_TRACK → PAR_NONE | Matches. Row shifted to start with PAR_QUANTISATION. | OK |

### 9.4 Phase D — Step-Edit Menu

Plan described behavior; implementation provides full function-level code.

| Item | Plan | Implementation | Status |
|------|------|----------------|--------|
| D1 state vars | 3 statics after line 1133 | Matches. | OK |
| D2 reset on step change | Reset in PAR_ACTIVE_STEP + showStepEditPage | Matches + adds resets in `menu_switchPage`, `menu_setActiveVoice`, `menu_showStepTrackSettingsFirstHalf`, `menu_toggleStepTrackSettingsHalf`. More thorough. | OK+ |
| D3 renderer | Four-column layout: action/voi/par/amt | Matches layout. Uses `sceneModTarget_formatShort()` for Scene targets, `instrumentManager_descriptor()->short_name` for voice. | OK |
| D4 render integration | Early return in `menu_repaintGeneric` | Matches. | OK |
| D5 navigation | Enter at probability+right, exit at page 0+left | Matches. Add page at `count`. | OK |
| D6 pot editing | 4 pots map to 4 fields | Matches. Pot 0 toggles del/clr mode. Pots 1–3 delegate to `menu_stepAutomationEdit`. Add page creates default on any pot turn. | OK |
| D7 encoder click | Add/del/clr dispatch | Matches intent. **Label/action mismatch — see §9.7.** | BUG |

**Additional menu helpers (not in plan but necessary):**
- `menu_stepAutomationPageActive()` — predicate consolidation.
- `menu_stepAutomationTargetUsed()` — uniqueness enforcement.
- `menu_stepAutomationFirstTarget()` / `menu_stepAutomationNextTarget()` —
  target stepper with uniqueness filtering.
- `menu_stepAutomationReplaceTarget()` — atomic target swap at the 63-entry
  edge case (write new before removing old, or remove+restore on failure).
- `menu_stepAutomationSlotForTrack()` — track→slot mapping.
- `menu_stepAutomationAddDefault()` — default entry creation with current
  parameter image value.
- `menu_stepAutomationEnsurePage()` — lazy creation on pot turn.

### 9.5 Phase E — Sequencer Playback

| Item | Plan | Implementation | Status |
|------|------|----------------|--------|
| E0 config.h | `SEQ_PENDING_BUF_COUNT=128`, `PAT_TRACE_*` | Matches. | OK |
| E1 pending buffer | 4-byte identity/payload records | Matches format. Struct name `seq_pending_automation_t` (plan: `seq_pending_entry_t`). Adds `_Static_assert(sizeof==4)`. Array is `volatile`. | OK+ |
| E2 ISR readout | Append raw packed words, no dedup | Factored into `seq_queueStepAutomations()` helper (plan had inline). Adds step_id header validation against expected track/step. Adds full block-tail bounds check. | OK+ |
| E3 foreground drain | `seq_drainPendingAutomation()` | Matches. Uses PRIMASK compare-and-reset loop to close ISR append race (plan used simple reset). Calls `instrumentManager_targetValid()` per entry (plan didn't). | OK+ |
| E4 PatternTrace.h/c | DEV_MODE_LOGGING ring, stage 'H' | Matches. Complete implementation with stubs for production. | OK |
| E5 filesystem.c drain | `pattrace.bin` append state machine | Matches. 4-phase clone of autosave trace flush. Scheduler, serializer, completion, integration into `filesystem_tick()`. | OK |

### 9.6 Architectural deviations (improvements)

**`pat_writeDynamic()` factoring.** The plan had `pat_writeSpecials` as the
transaction owner and each CRUD function duplicating the allocate/write/free
sequence. The implementation introduces `pat_writeDynamic()` as the single
transaction owner. Both `pat_writeSpecials` and all public CRUD functions
delegate to it. This eliminates ~80 lines of duplicated allocation logic and
makes the allocate-first invariant impossible to violate in a single caller.
Better than planned.

**PRIMASK drain loop.** The plan's drain did `count = seq_pendingCount;
for (i..count) { ... } count=0; drain=0;`. This has a race: if TIM3
appends between the last iteration and the reset, the new entry is lost. The
implementation uses a `for(;;)` loop that rechecks the count under PRIMASK
before resetting, retrying if TIM3 raced. Correct, and the plan's version
was subtly wrong.

**`allow_shrink_in_place`.** Plan's `pat_removeStepAutomation` returned
failure on pool exhaustion even when the block was shrinking. The
implementation allows in-place shrink: overwrite the block at the same offset
and free the trailing chunks. This is safe because the block is getting
smaller — the valid data is a prefix of the old allocation.

### 9.7 Bug: del/clr label-action mismatch

In `menu.c`, the renderer and the action handler have swapped semantics:

| `menu_stepAutoDeleteMode` | Renderer shows | Action executed |
|---------------------------|----------------|-----------------|
| 0 (default) | `del` | `pat_removeTrackAutomationByTarget` (all steps) |
| 1 | `clr` | `pat_removeStepAutomation` (this step only) |

The plan in §7.4 D7 specified:
- mode 0 (`del`) → `pat_removeStepAutomation` (single step)
- mode 1 (`clr`) → `pat_removeTrackAutomationByTarget` (track-wide)

The labels match the plan but the action branches are swapped.

**Fixed:** swapped the two function calls in
`menu_stepAutomationExecuteItem0()` so that mode 1 (`clr`) calls
`pat_removeTrackAutomationByTarget` and mode 0 (`del`) calls
`pat_removeStepAutomation`, matching the plan and the rendered labels.

### 9.8 RAM and binary size

| Resource | Plan | Actual |
|----------|------|--------|
| Pending buffer | 514 B SRAM1 | 514 B (128 × 4 + 2 volatile control) |
| PatternTrace ring (DEV only) | 256 B SRAM1 | 259 B (256 data + 3 cursors) |
| Flash image delta | — | +5184 B (427184 → 432368) |

### 9.9 Files changed vs. plan

All files listed in §7.6 are present in the diff. Two additional changes
not in the table:
- `Makefile`: `PatternTrace.c` added to `SRCS` (necessary, not in table).
- `build/LXRV2_lxr02.img`: binary artifact (expected, not a source change).

### 9.10 Assessment summary

All five phases are implemented. The code matches the plan's intent with
three architectural improvements (pat_writeDynamic factoring, PRIMASK drain
race fix, shrink-in-place removal) and pervasive defensive bounds checks not
in the plan. One bug found and fixed: del/clr label-action swap in the
step-edit menu (§9.7). No other issues. Binary builds clean.
