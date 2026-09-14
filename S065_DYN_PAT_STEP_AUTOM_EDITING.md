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

Per `SRAM_MANIFEST.md` policy, the ~162–194 bytes of new static SRAM1
allocation requires explicit user acknowledgement before implementation. The
pending buffer is Pattern playback infrastructure in normal SRAM1.
