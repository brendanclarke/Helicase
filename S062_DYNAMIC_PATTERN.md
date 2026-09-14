# S062 — Dynamic Pattern Storage: Foundation Session

**Session 062 — 2026-09-08**
**Branch:** `dev-ph3-autosave-ph6`
**Predecessor:** Session 061 (AutoSave reader closeout, HCNAMES type schema)

---

## 1. Context and prior-work state

Sessions 045–061 completed the AutoSave A/B scalar reader/writer, typed
HCNAMES provenance, committed Load/Save publication, and boot restore. Pattern
and Effect data are not yet in HCPR. The AutoSave and Load/Save revision
backlog is tracked in `AUTOSAVE_TEST_CASES_LOAD_SAVE_REVISIONS.md` and is
deliberately deferred until after Pattern storage lands.

The current pattern representation is a 112-byte `PatternSet` per Scene:
seven tracks of 128-bit on/off bitmaps (`step_on[7][16]`), embedded directly
in `scene_t` at `SceneData.h:225`. It stores only trigger state. The stub
functions `pat_setStepNote()`, `pat_setStepVolume()`, and
`pat_setStepProbability()` exist in the menu parameter routing
(`PAR_STEP_NOTE`, `PAR_STEP_VOLUME`, `PAR_STEP_PROB` in `menuPages.h:84` and
`menu.c:9751–9774`) but are no-ops: the current `PatternSet` has nowhere to
put the values.

This session replaces that shim with the first layer of the Phase 4 dynamic
event pool: a static address array, a reduced-size dynamic pool, and a
free-tracking bitmap — enough to store and play per-step specials (note,
velocity, probability/random) from the existing menu interface.

---

## 2. Session 062 objectives

1. **Allocate the static address array** — 1,792 bytes per Scene (896 steps
   × 16-bit entries), replacing the 112-byte `PatternSet` bitmap. Each entry
   carries the on/off trigger bit (bit 15), the has-specials flag (bit 14),
   and a 14-bit pool offset address.

2. **Allocate the configurable dynamic pool** — sized by `PAT_STACK_SIZE` in
   `config.h`, measured in bitmap-tracking units. Pool bytes per Scene =
   `PAT_STACK_SIZE × 32`. Initial value: `PAT_STACK_SIZE 256` → 8,192 bytes
   per Scene (half the 14-bit address space). The 14-bit field stores a
   direct byte offset into the pool; valid offsets are multiples of 4
   from `0x0000` to `(PAT_STACK_SIZE × 32) − 4`. `0x3FFF` remains the
   reserved sentinel (always invalid: not a multiple of 4). See Section
   3.1 for the parameterization details.

3. **Allocate the full free-tracking bitmap** — 512 bytes per Scene, covering
   all 4,096 four-byte chunks of the nominal 2^14 address space. Chunks
   beyond the pool boundary are permanently marked occupied. This preserves
   the Phase 4 bitmap infrastructure at full width so a later session can
   expand the pool by increasing `PAT_STACK_SIZE` without changing the
   tracking layer.

4. **Implement the specials storage path** — a dynamic block at a real pool
   address carries a 2-byte header (10-bit step-ID back-reference + 6-bit
   automation count, which is zero for this session) and a special-flags byte
   with value bytes for note, velocity, and probability. The menu's existing
   `PAR_STEP_NOTE`, `PAR_STEP_VOLUME`, and `PAR_STEP_PROB` setters become
   real writes into the pool. `pat_applyStepToMenu()` becomes a real read
   from the pool.

5. **Sequencer playback reads the new structure** — `seq_tick()` resolves
   trigger state from the address array's bit 15, reads specials from the
   pool when bit 14 is set, and uses them for note/velocity/probability at
   trigger time. Steps with no specials (address `0x3FFF` or bit 14 clear)
   use per-track defaults (`PAT_DEFAULT_NOTE` and `PAT_DEFAULT_VELOCITY`
   from `config.h`).

6. **Prove the dynamic address system** — by the end of the session, the user
   can assign note, velocity, and probability overrides to individual steps
   from the menu, hear them during playback, toggle steps on/off without
   losing those assignments, and clear step data.

7. **Hardware checkpoint after static allocation** — a testing firmware is
   built after Step B (address array replaces bitmap, before any pool work)
   to verify that step toggle, playback, SEQ LEDs, and generators all work
   correctly on the new address-entry representation. This confirms the
   static segment is sound before the dynamic pool is wired in.

### Explicitly out of scope for Session 062

- Load/Save of pattern data — the v3 `pattern.pat` bridge is disconnected,
  not converted. Scenes boot with empty patterns. File format v4 is a
  separate later session.
- AutoSave integration (HCPR pattern payload, dirty marking).
- Automation entries (the 2-byte target+value pairs in dynamic blocks).
- Live recording of specials or automation.
- Defragmentation, micro-relocation, and slack reservation.
- Roll, timing/microtiming, and automation-hold specials.
- Copy/paste operations for steps, bars, tracks, or patterns.
- Pattern generator rework — Euklid/SOM continue to operate on bit 15
  (on/off) through the existing `pat_` API; they do not touch the pool.
- Any menu expansion; only the existing step-edit knobs are wired.

---

## 3. Memory budget

### 3.1 `PAT_STACK_SIZE` parameterization

All pool sizing derives from one `config.h` define:

```c
#define PAT_STACK_SIZE      256   /* pool size in bitmap-tracking units   */
#define PAT_DEFAULT_NOTE     63   /* step note when no special assigned   */
#define PAT_DEFAULT_VELOCITY 100  /* step velocity when no special        */
```

The free-tracking bitmap is always 512 bytes (4,096 bits, one per 4-byte
chunk across the full 2^14 nominal address space). `PAT_STACK_SIZE` controls
how many of those chunks are actually backed by pool memory:

| Derived quantity | Formula | Value at size 256 |
|---|---|---:|
| Pool bytes per Scene | `PAT_STACK_SIZE × 32` | 8,192 B |
| Usable 4-byte chunks | `PAT_STACK_SIZE × 8` | 2,048 chunks |
| Max usable byte offset | `(PAT_STACK_SIZE × 32) − 4` | 0x1FFC |
| Permanently occupied chunks | `4096 − (PAT_STACK_SIZE × 8)` | 2,048 chunks |

To double the pool to the full 14-bit capacity in a future session, set
`PAT_STACK_SIZE 512` → 16,384 B pool, all 4,096 chunks usable. The bitmap,
address array, and block format are unchanged; only the permanently-occupied
region of the bitmap shrinks to zero (minus the `0x3FFF` sentinel chunk).

### 3.2 Per-Scene allocation

| Component | Size | Notes |
|---|---:|---|
| Address array | 1,792 B | 896 entries × 2 B; replaces 112-B `PatternSet` bitmap |
| Dynamic pool | 8,192 B | `PAT_STACK_SIZE(256) × 32`; addresses `0x0000`–`0x07FF` |
| Free-tracking bitmap | 512 B | Always 4,096 bits; upper half permanently occupied at size 256 |
| **Per-Scene total** | **10,496 B** | |

### 3.3 16-Scene total

| | Bytes |
|---|---:|
| 16 × 10,496 | 167,936 B |
| Less: retired 16 × 112-B `PatternSet` | −1,792 B |
| **Net new SRAM1** | **166,144 B** |

### 3.4 SRAM1 budget

| | Bytes |
|---|---:|
| Current SRAM1 static use | 93,044 B |
| SRAM1 capacity | 376,832 B |
| Current free (reserved for Pattern) | 283,788 B |
| After this allocation | **117,644 B remaining** |

The remaining ~117 KB accommodates future `PAT_STACK_SIZE` increases toward
the full 512 (adding ~131 KB to reach full-capacity pools) and any future
per-track metadata. Nearly all remaining SRAM1 is expected to be consumed by
pattern storage; this was always the purpose of the SRAM reservation.

### 3.5 Dynamic pool capacity at `PAT_STACK_SIZE 256`

With 8,192 usable bytes per Scene:

- **Minimum block** (header only, no specials, no automation): 2 bytes →
  1 chunk. Maximum blocks per Scene: 2,048. More than covers 896 steps.

- **Specials-only block** (header + flags + 3 value bytes): 6 bytes →
  2 chunks. Maximum such blocks: 1,024. Every step in a Scene can carry
  all three specials simultaneously.

- **4-byte chunk budget per step.** If all 896 steps are allocated, each
  gets an average of ~2.3 chunks (9.1 bytes). A single chunk (4 bytes)
  holds: a step header + special-flags + one value; or a step header + one
  automation entry; or two add-on automation entries; or four add-on
  special value bytes. The granularity is well-matched to the data shapes.

- **Future worst-case block** (header + flags + 5 value bytes + 4×2-byte
  automation entries): 16 bytes → 4 chunks. Maximum such blocks: 512.
  Automation density is the driver for expanding toward `PAT_STACK_SIZE 512`.

---

## 4. Architectural decisions

### 4.1 Address array replaces PatternSet

The 112-byte `PatternSet` is retired. Its on/off trigger state migrates to
bit 15 of the corresponding address array entry. The `PatternSet` type,
its `_Static_assert`, and its helpers (`pat_patternSetGetStep`,
`pat_patternSetSetStep`, `pat_initPatternSet`) are replaced by address-array
equivalents. The `scene_t.pattern` field changes type from `PatternSet` to a
pointer (or index) referencing the Scene's address array in the new
separately-allocated pattern memory.

Every current caller of the `pat_` bitmap API must be updated:
- `pat_isStepActive()` → reads bit 15 of the address entry.
- `pat_toggleStep()` → toggles bit 15, preserving bits 14–0.
- `pat_setStepActive()` → sets/clears bit 15, preserving bits 14–0.
- `pat_eraseStep()` → clears bits 15 and 14, writes `0x3FFF` address,
  frees the old pool block if one existed.
- `pat_sceneHasActiveSteps()` → scans address array for any bit-15-set entry.
- `pat_clearTrack()`, `pat_clearPattern()` → clear address entries and free
  pool blocks.
- `pat_copyTrack()`, `pat_copyPattern()`, `pat_copyBar()` → stubbed as
  no-ops for this session. Pool block duplication analysis is deferred
  to SCOPING_TARGETS Phase 4.5 (copy operations) for a later session.
- `seq_tick()` → reads bit 15 for trigger, bit 14 + address for specials.
- `seq_recordTrigger()` → sets bit 15, preserving pool address.
- Filesystem `pattern.pat` read/write → out of scope; bridge format is
  frozen until a later session defines the new file format.

### 4.2 Static address entry encoding (unchanged from Phase 4 design)

```
Bit 15      : on/off trigger
Bit 14      : has-specials (special-flags byte present in pool block)
Bits 13–0   : 14-bit pool byte offset (direct pointer arithmetic)
              0x3FFF = no lookup (reserved sentinel; always invalid
                       because valid byte offsets are multiples of 4)
              0x0000 to (PAT_STACK_SIZE*32 - 4) = valid pool byte offsets
              (PAT_STACK_SIZE*32) to 0x3FFE = invalid (no pool backing)
```

At `PAT_STACK_SIZE 256`, valid byte offsets are `0x0000`–`0x1FFC`
(multiples of 4, covering 2,048 chunks × 4 bytes = 8,192 bytes). At
full capacity (512), valid byte offsets extend to `0x3FFC`. The reader
adds the byte offset directly to the pool base pointer; the bitmap
chunk index is `byte_offset >> 2`.

Invariant: bit 14 set together with address `0x3FFF` is illegal. The
address entry is a 2-byte aligned `uint16_t`, so reads and writes are
single-instruction atomic on Cortex-M7 (`LDRH`/`STRH`).

### 4.3 Dynamic block layout for specials

At a valid pool address:

```
Byte 0–1  : Header
              Bits 15–6 : 10-bit step-ID (0–895 back-reference)
              Bits 5–0  : 6-bit automation count (always 0 this session)

Byte 2    : Special-flags byte (present only when bit 14 is set)
              Each set bit means one value byte follows, in flag-bit order.
              This session defines three flags; remaining bits are reserved.

Byte 3+   : Value bytes, one per set flag, in ascending bit order.
```

### 4.4 Special-flags byte — Session 062 field assignment

| Bit | Field | Value byte | Range | Default when absent |
|:---:|---|---|---|---|
| 0 | note | 1 B | MIDI note 0–127 | `PAT_DEFAULT_NOTE` (63) |
| 1 | velocity | 1 B | 0–127 | `PAT_DEFAULT_VELOCITY` (100) |
| 2 | probability | 1 B | 0–127 (127 = always) | 127 (always triggers) |
| 3 | (reserved: timing/microtiming) | — | — | — |
| 4 | (reserved: roll) | — | — | — |
| 5 | (reserved: automation hold) | — | — | — |
| 6–7 | (reserved) | — | — | — |

"Random" is probability: the likelihood that the step fires. 127 means
always (default); anything less scales linearly to zero. It does not mutate
note or velocity. The probability behavior may evolve in a later session,
but this is the original implementation and serves as the dynamic-pool proof.

### 4.5 Pool allocator — minimal Session 062 version

This session does not need defragmentation, micro-relocation, slack
reservation, or a servicer queue. All pool writes are human-paced menu edits.
The allocator is:

1. **Allocate:** linear scan of the free-tracking bitmap for a contiguous
   run of chunks sufficient for the block. Mark those chunks occupied. Write
   the block. Update the address entry atomically.

2. **Free:** read the address entry's offset. Mark the corresponding bitmap
   chunks as free. Write `0x3FFF` (preserving bit 15) to the address entry.

3. **Grow/shrink:** when a special is added to or removed from a step that
   already has a pool block, the block size may change. The simplest correct
   approach for this session: free the old block, allocate a new one at the
   new size, write the updated content, update the address entry. This is
   adequate for menu-paced edits. The write-new/swap/free-old protocol from
   SCOPING_TARGETS 4.3 is the correct approach for real-time paths in later
   sessions.

4. **Initialization:** all address entries are `0x3FFF` (bit 15 clear, no
   lookup). The bitmap marks chunks `0` through `PAT_STACK_SIZE × 8 − 1` as
   free and all chunks beyond as permanently occupied.

### 4.6 Allocation topology

The pattern memory is a single contiguous SRAM1 block sized for all 16
Scenes, statically allocated at link time. Within that block, each Scene owns
a fixed-offset region containing its address array, dynamic pool, and bitmap
in a known layout. No per-scene `malloc` or dynamic sizing. These are
permanent runtime allocations — read, written, and erased but never moved.

```
Per-Scene region: 1,792 + (PAT_STACK_SIZE × 32) + 512 bytes
At PAT_STACK_SIZE 256: 1,792 + 8,192 + 512 = 10,496 B per Scene

Pattern memory: 16 × 10,496 = 167,936 bytes
  Scene 0:   [address_array: 1792 B] [pool: 8192 B] [bitmap: 512 B]
  Scene 1:   [address_array: 1792 B] [pool: 8192 B] [bitmap: 512 B]
  ...
  Scene 15:  [address_array: 1792 B] [pool: 8192 B] [bitmap: 512 B]
```

**`scene_t.pattern` is removed entirely.** The pattern module owns its own
static memory array, indexed by scene. Every `pat_` function already takes
`scene_index` as a parameter; internally it computes the offset into the
pattern memory block. No pointer or index is stored in `scene_t`. This gives
clean ownership separation: `scene_t` holds settings and kit data; the
pattern module holds its own permanent allocations. `scene_t` shrinks by
112 bytes (the retired `PatternSet`).

### 4.7 Integration with sequencer playback

`seq_tick()` currently calls `pat_isStepActive()` to decide whether to
trigger. The new path:

1. Read the 16-bit address entry for (track, step) from the active Scene's
   address array.
2. If bit 15 is clear → no trigger, done.
3. Bit 15 is set → start with defaults: note = `PAT_DEFAULT_NOTE` (63),
   velocity = `PAT_DEFAULT_VELOCITY` (100), probability = 127.
4. If bit 14 is set and address ≠ `0x3FFF` → read the dynamic block at the
   pool offset. Parse the special-flags byte. Override note, velocity,
   and/or probability from the value bytes present.
5. Apply probability: if probability < 127, generate a random check; if the
   check fails, suppress the trigger. Probability does not mutate note or
   velocity — it only gates whether the trigger fires.
6. Trigger the voice with the resolved note and velocity via
   `seq_triggerVoice(voiceNr, vol, note)`, which already accepts note
   and velocity parameters. No sequencer API change is needed; the
   playback path only changes what values it passes.

Automation entries (count from the header) are ignored this session.

### 4.8 Filesystem bridge — pattern.pat

Current pattern data from Scenes is ignored on boot and load/save for this
session. All Scenes initialize with empty pattern memory (all address entries
`0x3FFF`, empty pools). The existing `pattern.pat` v3 reader and writer
remain in the source but their output is not applied to the new address
arrays.

The file format conversion (v4 serializing address arrays and pool content)
will be wired separately after the SRAM structure is proven on hardware.
Test pattern files may be generated independently for that work.

---

## 5. Sequence of work

This is the implementation order, not a task list. Each step must compile
and not regress before starting the next.

### Step A — Allocate pattern memory

Add `PAT_STACK_SIZE`, `PAT_DEFAULT_NOTE`, and `PAT_DEFAULT_VELOCITY` to
`config.h`. Define the per-scene pattern region struct and the static array
in SRAM1 (sized from `PAT_STACK_SIZE`). Remove `scene_t.pattern` (the
embedded `PatternSet`); the pattern module owns its own memory indexed by
scene. Update `scene_initAll()` to initialize all 16 Scenes' address arrays
to `0x3FFF` and bitmaps to the correct initial state (lower
`PAT_STACK_SIZE × 8` chunks free, rest permanently occupied). Disconnect
the `pattern.pat` reader/writer from address-array population so Scenes
boot empty. Verify linked image size against the SRAM manifest.

### Step B — Port trigger bitmap callers

Replace every `PatternSet` API call with address-array-entry bit-15
operations. The public `pat_` API signatures can stay; only the
implementation changes. Pattern generators (Euklid, SOM) operate on the
static segment (on/off bit 15) only and leave the dynamic pool untouched;
they continue to work through the updated `pat_setStepActive()` and
`pat_clearTrack()` without changes to their own code.

### Step B½ — Hardware checkpoint (testing firmware)

**Build and flash.** Verify on hardware before any pool work:

- Step toggle on/off from SEQ buttons: correct LED state, correct playback.
- Playback triggers at `PAT_DEFAULT_NOTE` (63) and `PAT_DEFAULT_VELOCITY`
  (100) for every triggered step.
- Pattern clear (track and whole-scene).
- Euklid and SOM generators produce the expected on/off pattern.
- Scene switching: each Scene has independent trigger state.
- `seq_recordTrigger()` sets bit 15 correctly during live recording.
- No regressions in non-pattern behavior (voice editing, load/save of
  non-pattern data, AutoSave of non-pattern parameters).
- Linked image size is correct and SRAM manifest is updated.

This checkpoint proves the static address-entry representation is sound
before the dynamic pool is wired in. Commit this as a standalone firmware.

### Step C — Implement pool allocator and block read/write

Implement the bitmap-based chunk allocator (allocate, free, grow/shrink via
free-then-realloc). Implement block write (header + special-flags + value
bytes) and block read (parse header, flags, extract values). No servicer
queue or background processing — direct synchronous calls. Allocation
respects the `PAT_STACK_SIZE`-derived pool boundary.

### Step D — Wire specials through menu and playback

Connect `pat_setStepNote()`, `pat_setStepVolume()`, and
`pat_setStepProbability()` to real pool writes. Connect
`pat_applyStepToMenu()` to real pool reads. Update `seq_tick()` to read
specials from the pool and use note/velocity/probability at trigger time,
falling back to `PAT_DEFAULT_NOTE` and `PAT_DEFAULT_VELOCITY` when no
special is assigned.

### Step E — Hardware verification (full dynamic stack)

- Toggle steps on/off: confirm trigger state, LED state, and that pool
  blocks survive toggling (specials persist across on→off→on).
- Assign note override to a step: confirm playback uses the custom note.
- Assign velocity override: confirm audible volume change.
- Assign probability < 127: confirm probabilistic triggering.
  Assign probability = 0: step never fires. Probability = 127: always fires.
- Assign all three specials to one step: confirm correct block layout and
  all three are readable from the menu and used by playback.
- Clear a step with specials: confirm pool block is freed, bitmap chunk
  reclaimed, menu shows defaults again.
- Verify across multiple Scenes: each Scene's pool is independent.
- Verify linked image size and runtime SRAM use.
- SRAM manifest updated with final figures.

---

## 6. Resolved ambiguities

### A-1: "Random" = probability — RESOLVED

Random is the probability that the step fires. 127 = always (default);
anything less scales linearly to zero. It does not mutate note, velocity, or
any other parameter. The behavior may evolve later, but this is the original
implementation and serves as the dynamic-pool proof.

### A-2: Default note and velocity — RESOLVED

`PAT_DEFAULT_NOTE 63` and `PAT_DEFAULT_VELOCITY 100`, both in `config.h`.
These are the values used by `seq_tick()` when a step has no note or
velocity special assigned. They are global defaults, not per-track.

### A-3: scene_t.pattern ownership — RESOLVED

`scene_t.pattern` (the embedded `PatternSet`) is removed entirely. The
pattern module owns its own statically-allocated memory block, indexed by
scene. Every `pat_` function already takes `scene_index`; internally it
computes the offset into the pattern memory array. No pointer or index is
stored in `scene_t`.

Tradeoff considered and rejected: embedding the 1,792-byte address array
inline in `scene_t` would improve cache locality for the hot trigger-read
path but would grow `scene_t` by 1,680 B (1,792 − 112) per Scene, tightly
couple two ownership domains, and still require an external reference for
the pool and bitmap. The clean separation is preferable — the address array,
pool, and bitmap are a cohesive unit that the pattern module owns completely.

### A-4: Pattern generators — RESOLVED

Generators (Euklid, SOM) work on the static segment only: they call
`pat_setStepActive()` and `pat_clearTrack()` which operate on bit 15 of
address entries. They do not touch the dynamic pool. Specials assigned to
steps are preserved when a generator runs — a regeneration changes which
steps are on/off but does not clear note/velocity/probability overrides.
The generator behavior will change significantly in a later session; this
is the simplest correct policy for now.

### A-5: 4-byte chunk alignment — RESOLVED

Confirmed acceptable. A single 4-byte chunk holds: a step header +
special-flags + one value byte; or a step header + one automation entry; or
two add-on automation entries; or four add-on special value bytes. At
`PAT_STACK_SIZE 256` with all 896 steps allocated, each step gets an average
of ~2.3 chunks (~9.1 bytes), which accommodates note + velocity +
probability + one automation entry. Internal fragmentation from chunk
rounding is a modest cost well within the budget.

### A-6: Initialization — RESOLVED

All Scenes initialize with empty pattern memory: every address entry is
`0x3FFF` (off, no specials, no lookup), every pool byte is zero, and the
bitmap reflects all usable chunks free. Current pattern data from the
`pattern.pat` v3 bridge is not loaded into the new structure. Load/save
integration is wired separately after the SRAM structure is proven on
hardware.

### A-7: 14-bit field encoding — RESOLVED

The 14-bit field stores a direct byte offset into the pool, not a chunk
index. Valid byte offsets are always multiples of 4 (4-byte chunk
alignment), so the reader does `pool_base + byte_offset` with no
multiply. The sentinel `0x3FFF` is inherently invalid because it is not
a multiple of 4. Bitmap chunk index is recovered as `byte_offset >> 2`.

Tradeoff: byte offset wastes ~1 address bit at any pool size under the
full 14-bit capacity (at size 256, max valid offset is 0x1FFC, using
only 13 effective bits). Chunk index would use the field more
efficiently but requires a `<< 2` shift on every access. Byte offset
is simpler for the initial implementation and avoids the per-access
multiply in the playback hot path.

### A-8: Copy operations — RESOLVED

`pat_copyTrack()`, `pat_copyPattern()`, and `pat_copyBar()` are stubbed
as no-ops for this session. Implementation analysis for pool block
duplication during copy is redirected to SCOPING_TARGETS Phase 4.5
(copy operations) for a later session.

### A-9: Sequencer trigger receivers — RESOLVED

`seq_triggerVoice(uint8_t voiceNr, uint8_t vol, uint8_t note)` already
accepts note and velocity parameters. It is the target for resolved
specials from the dynamic pool. No API change is needed on the
sequencer side; only the caller (`seq_advanceTrackStep`) changes what
values it passes. For Step B½, it passes `PAT_DEFAULT_VELOCITY` and
`PAT_DEFAULT_NOTE` in place of the current `ROLL_VOLUME` and
`MIDI_DEFAULT_TRIGGER_NOTE` (identical numeric values: 100 and 63).

---

## 7. Relationship to SCOPING_TARGETS Phase 4

This session implements a subset of Phase 4.1 (step/bar model) and Phase 4.2
(dynamic event pool). The mapping:

| SCOPING_TARGETS section | Session 062 coverage |
|---|---|
| 4.1 Step/bar model | Grid dimensions (8 bars, 128 steps, 7 tracks) inherited from current code. No sub-steps already true. |
| 4.2 Address array | Fully implemented at designed bit layout. Pool reduced to half capacity. |
| 4.2 Dynamic block | Header and specials only. No automation entries. |
| 4.3 Defrag/real-time | Not implemented. Menu-paced allocator only. |
| 4.3a Automation hold | Not implemented. |
| 4.4 Parameter ID space | Not implemented. Zero automation count in all headers. |
| 4.5 Copy operations | Not implemented. |
| 4.6 Trigger/automation independence | Partially: trigger bit independent of pool address. Automation not yet present. |
| 4.7 Per-track scale | Already exists as bridge (Session 031). Unchanged. |
| 4.8 Roll | Not implemented. |
| 4.9 Patgen/Euklid reset | Not implemented. |
| 4.10 Triplet mode | Not implemented. |
| 4.11 LED consolidation | Not implemented. |

---

## 8. Implementation status

### Steps A+B — completed 2026-09-09

Static address array allocation, trigger-bitmap port, and filesystem
bridge disconnection are implemented and link-verified. The full change
set is documented in `S062_DYNAMIC_PATTERN_B_IMPLEMENT.md` with per-file,
per-function detail.

**What shipped:**
- `config.h`: `PAT_STACK_SIZE 256`, `PAT_DEFAULT_NOTE 63`,
  `PAT_DEFAULT_VELOCITY 100`.
- `PatternData.h`: address-entry constants (`PAT_ADDR_SENTINEL`,
  `PAT_ADDR_TRIGGER_BIT`, `PAT_ADDR_SPECIALS_BIT`, `PAT_ADDR_OFFSET_MASK`,
  `PAT_STEPS_PER_SCENE`). `PatternSet` type and helpers retained for v3
  bridge.
- `PatternData.c`: `pat_scene_region_t` (10,496 B per Scene) ×
  `SCENE_COUNT` = 167,936 B in SRAM1. `pat_initScene()` writes sentinel
  entries, zeros pool, marks bitmap split. All nine Scene-indexed functions
  ported to address-array bit-15 operations. Copy functions stubbed as
  no-ops (deferred to Phase 4.5).
- `SceneData.h`: `PatternSet pattern` removed from `scene_t` (−112 B per
  Scene).
- `filesystem.c`: static `PatternSet` discard bridge. Scene Load commit
  calls `pat_initScene()`. Fan-out disconnected. Save writes empty discard.
  Boot reader returns 0 (pattern files not loaded).
- `sequencer.c`: trigger call uses `PAT_DEFAULT_VELOCITY` /
  `PAT_DEFAULT_NOTE`.
- `sequencer.h`: `seq_recordTrigger` comment updated.

**Link output:** `text=406,396`, `data=404`, `bss=262,468`.
`pat_regions` = 167,936 B, `scenes` = 19,200 B (down from 20,992 B).
Image: 406,816 bytes.

**Independent review** confirmed all scheduled changes match the live
source, with five quality improvements beyond the schedule (type-safe `u`
suffixes, `PAT_STACK_SIZE` range assert, `__attribute__((unused))` on
disconnected accessor, per-file-phase discard reset, enriched comment
blocks).

### Step B½ — hardware checkpoint pending

The firmware has not yet been flashed or exercised. The verification
checklist (toggle, playback, clear, generators, Scene independence,
recording, image size) is in `S062_DYNAMIC_PATTERN_B_IMPLEMENT.md`
Section 16.

---

### Future sessions to complete Phase 4

- **File format v4:** serialize address arrays and pool content to
  `pattern.pat`. Generate test files independently to validate the round
  trip before wiring into the Load/Save path.
- **Automation entries:** add the 2-byte target+value pairs to dynamic
  blocks. Wire the 9-bit parameter ID space. Update playback to apply
  automation.
- **Live recording:** implement the servicer queue, slack reservation, and
  the record-watcher from 4.3a.
- **Defragmentation:** background micro-relocation and global sweep.
- **Timing/roll specials:** add microtiming and roll flag bits and value
  bytes to the reserved positions in the special-flags byte.
- **Copy operations:** step, bar, track, pattern, scene copy with pool
  block duplication.
- **AutoSave integration:** pattern data in HCPR or a separate store.
- **Pool expansion:** increase `PAT_STACK_SIZE` toward 512 as automation
  density requires, consuming the remaining ~117 KB SRAM1.
- **Generator rework:** Euklid/SOM may need deeper integration with the
  dynamic pool once their behavior is redesigned.
