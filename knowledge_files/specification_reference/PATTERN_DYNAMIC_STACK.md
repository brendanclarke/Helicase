# Dynamic Pattern Stack Specification

## Authority and status

This is the authoritative live-memory, allocator, PAT4 interchange, and
Pattern AutoSave reference through Session 065. Historical Session 062/063/064
plans describe how the design was reached but do not override this file.
Filesystem hierarchy and HCNAMES grammar are in `FILESYSTEM_SPEC.md`; scalar
and Pattern AutoSave scheduling/recovery are in `AUTOSAVE.md`; exact linked
memory totals are in `SRAM_MANIFEST.md`.

Implemented and hardware accepted:

- 16 independent resident Scene Patterns;
- 7 tracks × 128 steps per Scene;
- trigger state plus dynamic note, velocity, and probability specials;
- per-step automation entries (2-byte LE, 7-bit value + 9-bit target,
  up to 63 per step) with uniqueness invariant and dtype-aware editing;
- first-fit bit-packed pool allocator and block reclamation;
- Pattern/track settings and Sequencer probability playback;
- sequencer automation playback: TIM3 copies to 32-entry debounced
  pending buffer, foreground drain via `instrumentManager_writeRuntime()`,
  per-slot dirty bitmap with morph-interpolation restore on voice trigger;
- step-edit automation pages (Method 1): cursor navigation, detail views,
  add/delete/clear, parameter cycling with uniqueness, dtype-aware display;
- exact binary PAT4 Scene/Bank/root Pattern Load and Save;
- scene-mask Pattern Load fan-out;
- per-Scene hidden A/B Pattern AutoSave and boot restore;
- HCNAMES Pattern identity rows 129..144.

Not implemented: Pattern copy operations (the three APIs are deliberate
no-ops), VOICE-page held-step automation overlay (Method 2, Session 066),
live-record capture, allocator compaction, and real-time editing guarantees
while a snapshot is admitted during record/erase (admission is instead
deferred while those modes are active).

## 1. Resident object

`PatternData` owns:

```c
typedef struct __attribute__((packed)) {
    uint16_t address[7][128];       /* 1,792 B */
    uint8_t  pool[256 * 32];        /* 8,192 B */
    uint8_t  bitmap[512];           /* 512 B */
    uint8_t  track_length[7];       /* 7 B */
    uint8_t  track_scale[7];        /* 7 B */
    uint8_t  track_shuffle[7];      /* 7 B */
    uint8_t  pattern_change_bar;    /* 1 B */
    uint8_t  pattern_next;          /* 1 B */
} pat_scene_region_t;               /* 10,519 B */
```

`pat_regions[16]` is exactly 168,304 bytes. Pattern storage is not embedded in
`scene_t`; `scenes[16]` remains 19,200 bytes. `pat_sceneRegion(scene)` returns
read-only access and `pat_sceneRegionMut(scene)` is reserved for bounded owner
paths such as validated filesystem application. Ordinary clients use the
public operations so mutation tracking cannot be bypassed.

One additional `pat_autosave_snapshot` is exactly 10,519 bytes. It is the sole
immutable source for an in-flight Pattern AutoSave write; there is no snapshot
per Scene and no PAT4-sized filesystem stage.

## 2. Address encoding

Every track/step owns one little-endian native `uint16_t`:

```text
bit 15       trigger active
bit 14       dynamic specials block exists
bits 13..0   4-byte-aligned byte offset into the Scene pool
0x3fff       no-data sentinel (never a valid aligned pool offset)
```

Turning a step off preserves bits 14..0, so an existing specials block can
survive off/on. Erasing a step, clearing a range, or initializing the Scene
frees any owned block and restores `PAT_ADDR_SENTINEL`.

An address is valid only if the offset is aligned, within the 8,192-byte pool,
the referenced chunks are occupied, the stored back-reference identifies the
same track/step, the special flags are known, and the block fits before the
pool end.

## 3. Pool and bitmap geometry

`PAT_STACK_SIZE == 256` is a historical sizing unit, not the number of
allocatable chunks. It creates an 8,192-byte pool (`256 × 32`). Allocation is
in 4-byte chunks, so the pool contains 2,048 allocatable chunks.

The 512-byte bitmap is bit-packed: bit `chunk & 7` of byte `chunk >> 3` owns
one 4-byte chunk. Only its first 256 bytes correspond to the 2,048 backed
chunks. Initialization clears those 256 bytes and fills the upper 256 bytes
with `0xff`, permanently reserving the unbacked range. Do not treat the bitmap
as one byte per chunk or limit the live pool to 1,024 bytes.

Allocation is deterministic first-fit linear search. Blocks are contiguous;
free clears exactly the occupied span. There is no compaction or
defragmentation. Allocation failure leaves the old step block/value intact.

## 4. Dynamic block format

A block starts at a 4-byte-aligned pool offset:

```text
bytes 0..1  little-endian header
             bits 15..6: track * 128 + step (10-bit back-reference)
             bits 5..0: automation count (0..63)
byte 2      special flags: bit0 note, bit1 velocity, bit2 probability
bytes 3..   present values in note, velocity, probability order
next bytes  automation entries, 2 bytes each, auto_count entries:
             each entry little-endian: bits 15..9 = 7-bit value (0..127),
             bits 8..0 = 9-bit instrument_param_id_t target
padding     zero to a 4-byte boundary
```

Block size: `chunks = (2 + 1 + popcount(flags & 0x07) + auto_count * 2 + 3) / 4`.
One special with no automation is one 4-byte chunk. Three specials with four
automations is four chunks (14 bytes padded to 16). An unknown flag bit or an
inconsistent back-reference makes a block invalid.

Each automation entry's 9-bit target is the canonical `instrument_param_id_t`:
`slot * INSTRUMENT_PARAM_COUNT + descriptor_index` for voice parameters
(IDs 0..383), or a Scene target ID (384+). A step must never contain two
entries with the same 9-bit target (uniqueness invariant, enforced at write
time). The 7-bit value maps to `instrument_param_value_t` (0..255) via
`(v == 127) ? 255 : v * 2` (same as MIDI CC); the inverse is
`(v >= 255) ? 127 : v / 2`.

Default resolved values when a special is absent are the Pattern default note,
default velocity, and probability 127. `pat_readStepSpecials()` always returns
usable values plus flags saying which were explicitly stored.

Writers use read-modify-write semantics. When a new size differs, allocate the
replacement, write it completely, point the address entry at it, then free the
old block. Clearing the last special removes the block and bit 14 only when
no automation entries remain; a block with zero specials but nonzero
automation count is preserved (flags byte = 0, no special values stored,
automation entries follow immediately).

## 5. Public behavior

Core operations validate Scene/track/step coordinates and do no work for an
invalid coordinate:

- `pat_isStepActive`, `pat_setStepActive`, `pat_toggleStep`, `pat_eraseStep`;
- `pat_clearTrack`, `pat_clearPattern`;
- `pat_readStepSpecials` and the note/velocity/probability setters;
- `pat_readStepAutomations` — read decoded entries for one step (returns
  count);
- `pat_writeStepAutomation` — add or update one entry (uniqueness enforced,
  returns 1 on success, 0 on pool exhaustion or 63-entry ceiling);
- `pat_removeStepAutomation` — remove one entry by 9-bit target (returns 1
  if found);
- `pat_removeTrackAutomationByTarget` — remove all entries with a given
  target from all 128 steps of a track (returns count removed);
- `pat_stepAutomationCount` — count entries without reading them;
- track length/scale/shuffle and Pattern change-bar/next setters;
- menu apply helpers for Pattern, track, and selected-step state.

`pat_writeSpecials` preserves existing automation entries across
specials-only edits. `pat_eraseStep` and `pat_clearTrack` include automation
entries in block-size calculations when freeing pool chunks.

`pat_copyTrack`, `pat_copyPattern`, and `pat_copyBar` intentionally do nothing.
Their future implementation must duplicate live pool blocks (including
automation entries) and rebuild destination address offsets/bitmap ownership;
it must never alias one Scene's or step's pool allocation from another.

All real Pattern mutations converge on PatternData's local dirty helper. It
invalidates the Bank clean-Scene witness and calls
`autosave_markPatternDirty(scene)`. Direct mutable-region clients must provide
equivalent complete-operation marking or they violate AutoSave ownership.

## 6. Sequencer and menu integration

The Sequencer obtains trigger/special state through PatternData. For a
triggered step it evaluates probability using the hardware RNG, suppresses the
event when the random value is greater than or equal to the stored 0..127
probability, and otherwise triggers with the resolved note and velocity. Roll
events remain independent fixed-note behavior.

### 6.1 Step automation playback

On each step advance, `seq_advanceTrackStep()` reads automation entries from
every step that has a pool block (bit 14 set, valid offset), regardless of
trigger state. Decoded entries are copied into a 32-entry debounced pending
buffer in `sequencer.c` (192 B static SRAM). Multiple writes to the same
`(step_id, target)` pair coalesce; the ISR is the sole writer.

The foreground drain (`seq_drainPendingAutomation()`) runs inside
`audio_check_and_render()` immediately after `voiceControl_processPending()`,
within the per-chunk render loop. For each entry, it validates the target,
expands the 7-bit value to 8-bit, and calls
`instrumentManager_writeRuntime(slot, descriptor, value8)`. On success, it
sets the corresponding bit in `seq_automation_dirty[slot]` (a `uint64_t`
per-slot bitmap, 48 B total).

### 6.2 Automation reset on voice retrigger

All trigger sources funnel through `voiceControl_triggerNow()` in
`MidiVoiceControl.c`. Before `instrumentManager_triggerTrack()`, it calls
`seq_restoreAutomatedParameters(voice)`, which iterates set bits in the
dirty bitmap using `__builtin_ctzll`, writes the `morph_interpolation[]`
value for each dirty descriptor back to the runtime, and clears the bitmap.
The dirty bitmap is also cleared on `seq_init()`, transport stop, and
`seq_setStepIndexToStart()`.

### 6.3 Step-edit automation pages

After the existing specials pages (note, velocity, probability), the
selected-step edit page shows dynamically counted automation pages. Each
page displays one entry with a 5-item cursor: number (with number-lock
mode), del/clr action, voice, parameter (with uniqueness-filtered cycling),
and amount (with dtype-aware display and bounds clamping). An "add" page
follows the last assigned entry. Detail views show category+long_name for
parameter targets and dtype-aware named values for amounts.

The selected-step edit page reads the live dynamic block. Setting note,
velocity, or probability performs a tracked pool read-modify-write and repaints
the menu. Track and Pattern setting pages read/write the fields in the resident
region; PAT4 persists them.

## 7. PAT4 wire format

All current Scene/Bank/root Pattern Save operations write PAT4. Legacy text
v1-v3 files may be accepted for import but are never emitted.

One PAT4 file is exactly 10,656 bytes:

| Region | Offset | Bytes | Contents |
| --- | ---: | ---: | --- |
| Fixed header | 0 | 32 | magic/version/stack/generation/CRC/reserved |
| Pattern parameters | 32 | 16 | change-bar, next, 14 reserved |
| Track parameters | 48 | 112 | 7 × (length, scale, shuffle, 13 reserved) |
| Address array | 160 | 1,792 | 7 × 128 little-endian encoded entries |
| Bitmap | 1,952 | 512 | exact bit-packed allocator state |
| Pool | 2,464 | 8,192 | dynamic blocks/padding |

Fixed-header fields:

```text
0..3    "PAT4"
4..5    uint16 LE format version = 1
6..7    uint16 LE stack size = 256 (must equal firmware PAT_STACK_SIZE)
8..9    uint16 LE header size = 160
10..13  uint32 LE generation
14..17  uint32 LE CRC32C
18..31  reserved zero
```

CRC32C uses the Castagnoli polynomial, covers the entire exact-size file, and
treats bytes 14..17 as zero. Readers require current version, exact stack size,
exact EOF and matching CRC before committing. Firmware does not add a semantic
allocator graph audit during load; host validators should check address,
bitmap, back-reference, and pool consistency. Root/library saves use generation
zero.

Scene directories contain exactly one `<Pattern name>.pat`, not a fixed
`pattern.pat`. The root library uses `Pattern/NNN <name>.pat`. Pattern Load
accepts a destination Scene mask, streams into the first selected Scene, then
copies the validated `pat_scene_region_t` to every other selected Scene.

## 8. Pattern identity

HCNAMES rows 129..144 own one Pattern name/source/refreshed witness per Scene.
They have the non-Instrument grammar:

```text
name<TAB>source[<TAB>R]
```

Accepted sources are inherit `-`, unknown `?`, direct library slot `000` to
`999`, and Pattern AutoSave `@`. The RAM value for Pattern `@` is
`FS_RESIDENT_SOURCE_PATTERN_AUTOSAVE == 0x1ffc`; it must not be confused with
Instrument-direct `@ == 0x1ffd`. Pattern rows have no type column.

Scene/Bank directory load commits cache and publish the Pattern identity with
the Scene's other child rows. Root Pattern Load publishes the selected Pattern
name/source to every destination row. A Pattern mutation clears its row's
refresh witness before later AutoSave publication.

## 9. Pattern AutoSave

Each Scene has an independent hidden root pair:

```text
/.patNNa   generation even
/.patNNb   generation odd
```

`NN` is decimal `00`..`15`. Files are complete PAT4 images. The higher valid
generation wins and A wins a tie. Generations never intentionally use zero;
wrap skips zero.

Pattern dirtiness is a separate 16-bit mask. When all ordinary filesystem and
policy gates are open, Pattern is admitted only after higher-priority settings,
trace, and scalar AutoSave work declines. The scheduler chooses the lowest
dirty Scene, refuses admission during `seq_recordActive` or `seq_eraseActive`,
clears the bit before copying to the snapshot, advances generation, and starts
the whole-file writer.

Clear-before-snapshot is binding: a later edit re-sets the canonical bit and
must survive the in-flight completion. An admission/write/close/sync error
re-arms the bit. Successful durable completion publishes the Pattern row as
`name<TAB>@<TAB>R`, but the completion may set `R` only if no post-snapshot
edit made the Pattern dirty again.

At boot the Pattern reader evaluates present Scenes independently after the
scalar Bank/Scene restore. It applies the winning hidden candidate only when
the HCNAMES Pattern row is Pattern-AutoSave `@` and generation is nonzero.
Missing/invalid candidates leave the current initialized or library-loaded
Pattern intact and do not invalidate scalar Scene data. Explicit root Pattern
Load and Scene/Bank directory Pattern load reset the destination generation
baseline to zero.

## 10. Concurrency and failure invariants

- PatternData owns live regions and the snapshot; filesystem owns handles and
  Pattern generations; Autosave owns the dirty mask.
- Snapshot copy is a plain `memcpy`; it does not mask TIM3 or any interrupt.
- Record/erase activity gates admission because those paths can mutate the
  Pattern asynchronously.
- The snapshot is immutable until its writer completes.
- Only one filesystem facade operation is active; Pattern is not a second
  concurrent SD writer.
- FAT timestamps are irrelevant because the hardware has no RTC.
- Boot selects by exact format, size/EOF, CRC, generation, and provenance—not
  filename timestamp. Host diagnostics additionally validate allocator
  structure.
- A corrupt Pattern candidate never empties an otherwise valid scalar Scene.
- HCPR never contains Pattern identity or payload bytes. Its v2 APIs align
  with the 145-row HCNAMES schema, while HCNAMES/PAT4 own Pattern identity and
  payload durability.

## 11. Accepted validation and remaining work

Session 064 hardware acceptance covered single-Scene restore, ping-pong
parity, full 16-Scene drains, HCNAMES `@|R` publication, HCPR v2 coexistence,
absent rows, and content changes in every Scene. The full card contained 19
valid hidden candidates and no identified Pattern defect. See
`../log_archive/064_SESSION_HANDOFF_LOG.md` for exact evidence.

Session 065 implemented step automation editing (Method 1) and sequencer
playback: pool block automation read/write/remove APIs, step-edit menu with
cursor navigation and detail views, sequencer pending buffer with foreground
drain, per-slot dirty bitmap and trigger-time morph-interpolation restore.
Hardware-tested. See `../log_archive/065_SESSION_HANDOFF_LOG.md`.

Deferred supplemental cases are deterministic mid-write power interruption,
record/erase admission instrumentation, injected CRC fallback, and performance
measurement. They do not reopen the functional closeout. Phase 4.5 copy
operations, VOICE-page held-step automation overlay (Method 2, Session 066),
and live-record capture are future features.
