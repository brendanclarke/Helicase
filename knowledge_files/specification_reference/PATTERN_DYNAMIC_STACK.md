# Dynamic Pattern Stack Specification

## Authority and status

This is the authoritative live-memory, allocator, PAT4 interchange, Pattern
Stack Service, and Pattern AutoSave reference through Session 068. Historical
Session 062/063/064 plans describe how the design was reached but do not
override this file.
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
- Pattern/track settings storage, Menu edit, and PAT4 persistence; Sequencer
  probability playback; per-track step-length playback (Session 068, see
  §6.4) — per-track step-scale and shuffle are stored/edited/persisted but
  have no playback effect (deferred, see §6.4);
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
no-ops), live-record capture, and real-time editing guarantees while a snapshot
is admitted during record/erase (admission is instead deferred while those
modes are active). Allocator compaction/defragmentation is implemented via the
Pattern Stack Service (Session 067, see §12).

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
free clears exactly the occupied span. Allocation failure leaves the old step
block/value intact. The Pattern Stack Service (§12) provides two-tier
maintenance: Tier 1 trailing-gap merge after frees, and Tier 2 paced
compaction that relocates live blocks toward pool start.

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
time). The 7-bit value is an identity mapping: stored value = parameter value.
Every automatable descriptor parameter has a range that fits in 7 bits
(DTYPE_0B127, DTYPE_PM63, DTYPE_MENU, DTYPE_ON_OFF, DTYPE_MIX_FM,
DTYPE_LFO_POLARITY, DTYPE_NOTE_NAME, DTYPE_1B16). No DTYPE_0B255 parameter
is currently automatable. If a DTYPE_0B255 parameter becomes automatable in
the future, its conversion must be handled as a dtype-conditional special case.

Default resolved values when a special is absent are the Pattern default note,
default velocity, and probability 127. `pat_readStepSpecials()` always returns
usable values plus flags saying which were explicitly stored.

Writers use read-modify-write semantics with publication-safe ordering
(Session 067): allocate new block, write complete content, PRIMASK-protect the
address-entry swap (re-read trigger bit to avoid race with TIM3 step advance),
then free old block. In-place rewrite of same-size blocks is not used: even a
removal can compact automation bytes, creating a window where TIM3 reads
partially updated content. Clearing the last special removes the block and
bit 14 only when no automation entries remain; a block with zero specials but
nonzero automation count is preserved (flags byte = 0, no special values
stored, automation entries follow immediately). Erase and clear paths detach
the address entry before freeing the pool block.

## 5. Public behavior

Core operations validate Scene/track/step coordinates and do no work for an
invalid coordinate:

- `pat_isStepActive`, `pat_setStepActive`, `pat_toggleStep`, `pat_eraseStep`
  (Session 068: `buttonHandler_setRemoveStep()` emits a `DEV_MODE_LOGGING`-
  gated `K` witness record, via `AutosaveTrace.h`/`autosaveTrace_record()`,
  immediately before calling `pat_toggleStep()` — packs track/absolute
  step/pattern/pre-toggle trigger state, so a future report can distinguish
  front-panel input delivery failing to reach this call from a failure in
  the mutation itself. `K` is an AutoSaveTrace stage code, not a
  PatternTrace one — see `DEV_MODES.md` and note the two trace systems share
  some letters with different meanings);
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

Since Session 067, all pool-mutating operations from Menu, Sequencer,
copyClearTools, and EuklidGenerator route through the Pattern Stack Service
(`patSvc_*` API) rather than calling `pat_*` mutation functions directly. The
service guarantees exactly one mutation target at a time and serializes all
pool access. See §12.

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

### 6.4 Per-track length, scale, and shuffle — playback consumption status

`track_length[7]`, `track_scale[7]`, and `track_shuffle[7]` (§1) are all
correctly stored, Menu-editable, dirty-marked, and PAT4-persisted, but only
`track_length` currently affects playback. This distinction is not visible
from the resident-object struct alone and is stated here explicitly because
it was the source of a Session 068 field report.

**Track length (Session 068, implemented).** `seq_advanceTrackStep()` and
`seq_realignActivePatternToMasterClock()` (`Core/Sequencer/sequencer.c`) read
`region->track_length[track]` as each track's independent step-wrap boundary,
falling back to `NUM_STEPS_PER_BAR` (16) when the region pointer is
unavailable or the stored value is 0. Each track therefore loops
independently at its own configured length. The in-memory init default is 16
(`PatternData.c`), matching historic single-bar playback; the field's valid
storage range is 1..`NUM_STEPS` (128), but multi-bar lengths (17..128)
additionally require `menu_currentBar` integration in the chase-LED renderer
that has not been built — the accepted, tested range is 1..16.
`seq_handleMasterBoundary()` intentionally remains fixed at
`NUM_STEPS_PER_BAR`: it detects the master-grid bar boundary (pattern-change
commit, beat LED, clock output), a bar-level concept independent of any
individual track's loop length, and must not be changed to track length.

**Track scale (not implemented).** All tracks advance together on one global
divisor, `SEQ_INTERNAL_TICKS_PER_DEFAULT_STEP` (24 PPQ ticks = 1/16th note,
in `seq_processSchedulerTick()`). There is no per-track tick accumulator or
scale-to-ticks lookup. `track_scale[track]` (init default `TRACK_SCALE_OFF`)
has no playback effect regardless of its stored value. A fix requires
per-track PPQ tick accumulators (`NUM_TRACKS * 4` bytes of new ISR-static
state) and a scale-to-ticks mapping table validated against the original
LXR's documented scale labels; tracked in `SCOPING_TARGETS.md` § Session 068
deferred items.

**Track shuffle (not implemented).** Every step fires at a uniform tick
boundary; there is no shuffle-offset calculation in `sequencer.c`.
`track_shuffle[track]` (init default 0) has no playback effect regardless of
its stored value. A fix requires sub-step scheduling — either a per-track
"delay ticks remaining" counter (`NUM_TRACKS` bytes of new ISR-static state)
or a deferred trigger queue; tracked in `SCOPING_TARGETS.md` § Session 068
deferred items.

Scale and shuffle are each orthogonal to length and to each other: length
selects which step indices exist, scale selects how fast steps are visited,
shuffle offsets timing within a step interval. All three are meant to compose
independently once scale and shuffle are implemented.

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

Session 066 implemented the VOICE-page held-step automation overlay (Method 2):
overlay activation via configurable short long-press, four-slot bounded CGRAM
underline cache (slots 2..5), held-value resolution with endpoint fallback,
async track-wide automation search agent (4 steps/pass), pot/encoder-to-
automation write with working-value cache, debounced value-underline
reapplication, step illumination of automated steps, and Morph integration.
44 bytes overlay state in `menu.c` (approved). 496 bytes flash font table in
`lcd.c`. Six post-hardware-test fixes applied: delta handling working-value
cache, editMode bit index, non-numeric dtype display, 'S' glyph, PM63 nibble
split, diff-based CGRAM transactions with retry bit. Hardware-tested.
See `../log_archive/066_SESSION_HANDOFF_LOG.md`.

Session 067 implemented the Pattern Stack Service (§12): unified pool mutation
dispatcher with SPSC queue, two-tier defragmentation (trailing-gap merge and
paced compaction), bulk barriers for track/pattern clear, filesystem
replacement handover, pool usage monitor widget, and elastic gap policy.
Publication ordering fix applied to all address-to-pool transactions
(write-new/swap/free-old with PRIMASK trigger-bit re-read). Dtype offset bug
fixed: automation value domain changed from halved MIDI CC-style to identity
mapping across all four code sites (menu write, menu read, sequencer drain,
step automation add). Hardware-validated: PatternTrace zero errors across
6,172 records (5,185 gap + 987 relocation), AutoSaveTrace zero errors across
157,207 records, PAT4 structural integrity confirmed, automation values
confirmed in identity domain post-fix. See
`../log_archive/067_SESSION_HANDOFF_LOG.md` for exact evidence.

Session 068 confirmed the Pattern Stack Service was not implicated in a
reported VOICE-mode step-toggle failure (the `pattrace.bin` trace showed
zero error-class records; ordinary trigger toggling never enters the
service, see §12.14 item 1) — the actual defect was in front-panel event
delivery (`buttonHandler.c`'s event ring, pairing masks, and hold timer),
fixed with no Pattern Stack Service changes. The same session implemented
per-track step-length playback consumption (§6.4) and fixed a
`menu_playedPattern` UI-mirror desync that suppressed the sequencer chase
LED after boot (Menu/Sequencer concern, not a Pattern-storage defect). Session
069 then implemented the settled plan: physical relocation is non-semantic
AutoSave work, and the Tier 1/Tier 2 periodic chase is replaced by owned
trailing-slack repair plus reactive-only compaction. Source/build verification
passed; hardware fixtures remain pending. See
`../../S069_SLACK_REACTIVE_COMPACTION_IMPLEMENTATION.md` and
`../log_archive/068_SESSION_HANDOFF_LOG.md` §4 for the plan and closeout.

Deferred supplemental cases are deterministic mid-write power interruption,
record/erase admission instrumentation, injected CRC fallback, and performance
measurement. They do not reopen the functional closeout. Phase 4.5 copy
operations and live-record capture are future features. The Session 068
self-generated relocation/dirty-work-at-idle finding is closed in source by
the S069 repair/reactive design; hardware performance measurement remains
pending.

## 12. Pattern Stack Service

### 12.1 Architecture

`PatternStackService.c` and `PatternStackService.h`
implement a unified dispatcher that serializes all pool-mutating operations
through a single service tick. This guarantees exactly one mutation target at
a time, preventing concurrent access between foreground callers, the TIM3
ISR's automation reads, and the filesystem replacement boundary.

Admission policy: direct-when-idle (immediate foreground execution with
service lock), queued-when-busy (PRIMASK-protected enqueue into the SPSC ring
for later drain).

### 12.2 Service tick priority order

Evaluated every call to `patSvc_tick()` (called from `timebase.c` after
`endlessPots_tick()`):

1. **Handover** — check and complete filesystem replacement boundary
   transitions.
2. **Reactive recovery** — when the queue head is blocked by fragmentation,
   inspect a bounded set of address entries and relocate one live block toward
   a lower destination; surplus reservations may be reclaimed only when the
   density latch is inactive.
3. **Queue/bulk work** — advance one bounded track/pattern clear barrier or
   dequeue and execute one pending mutation.
4. **Finite bounded repair** — scan up to `PAT_REPAIR_SCAN_IDLE` (or
   `PAT_REPAIR_SCAN_BUSY` under AutoSave pressure) address entries per tick,
   creating or verifying one trailing-chunk reservation per occupied block.
   The cursor sleeps at `PATSVC_ADDRESS_COUNT` between epochs and wakes only
   on mutation, reservation consumption, density restore, handover, or
   filesystem replacement.

### 12.3 Queue format

64-entry volatile `uint32_t` ring buffer (256 bytes SRAM1). Each entry packs:

```text
bits 31..29   operation code (3 bits)
bits 28..25   scene (4 bits)
bits 24..22   track (3 bits)
bits 21..15   step (7 bits)
bits 14..6    target (9 bits) — instrument_param_id_t
bits 5..0     value (7 bits) — 0..127 instrument_param_value_t
```

Enqueue uses `__disable_irq()`/`__enable_irq()` (PRIMASK) to protect
head/tail consistency. Dequeue is foreground-only. Queue full is a silent drop.

### 12.4 Bulk barriers

Track clear and pattern clear are bounded operations, 16 steps per service
tick:

- **Track clear**: 128-step sweep, completes in 8 ticks.
- **Pattern clear**: 7-track × 128-step sweep, completes in 56 ticks.

### 12.5 Filesystem replacement handover

`patSvc_idle()` is called from 5 filesystem replacement boundary points in
`filesystem.c`. When a Scene Load, Bank Load, or Pattern Load completes, the
service completes or abandons any in-flight bulk barrier, drains remaining
queue entries for the old Scene, switches `service_scene` to the new target,
and clears internal repair/recovery cursors.
The handover-complete boundary also clears the non-persisted reservation image
and lazily rebuilds it through the next repair epoch.

### 12.6 Owned trailing-slack reservation

The reservation image is a 512-byte bit-packed array with the same geometry as
the occupancy bitmap. A set reservation bit means the chunk is reserved as
trailing slack for the immediately-preceding occupied block and is refused to
ordinary allocation. The repair pass creates reservations; the Gate-6 growth
path (`pat_tryAppendAutomation`) consumes them; reactive recovery reclaims
surplus ones under allocation pressure. The image is not persisted or included
in PAT4 payloads.

The density latch disables new reservations at or above
`PAT_RESERVATION_REDUCE_THRESHOLD` (70% occupancy) and re-enables them below
`PAT_RESERVATION_RESTORE_THRESHOLD` (50%). Hysteresis prevents oscillation.
An adaptive per-tick budget (`PAT_REPAIR_SCAN_IDLE` versus
`PAT_REPAIR_SCAN_BUSY`) yields foreground cycles to AutoSave I/O when dirty
work is pending.

The reservation image is cleared and lazily rebuilt at init, handover
completion, and filesystem replacement. During the rebuild window, allocation
falls back to the occupancy bitmap alone. Ordinary block frees and service
relocations clear the former positional trailing claim so the image cannot
retain stale reservations after a block changes owner.

Physical relocations are non-semantic AutoSave work. Reactive recovery runs
only after a blocked allocation; there is no periodic Tier-2 sweep.

### 12.8 Elastic gap policy

The former elastic-gap policy is retired. Reservation-density hysteresis now
controls owned trailing slack, and `PAT_COMPACT_SCAN_PER_TICK` (16 address
entries) is retained only as the reactive-recovery scan bound.

### 12.9 Pool usage monitor

`pat_poolUsagePercent()` in PatternData.c reads the Scene bitmap with
`memcpy` (packed-bitmap-safe), counts set bits with `__builtin_popcount`
across 64 words, and returns `(occupied × 100) / 2048` clamped to 99.
Displayed as `"pts:NN"` on the Settings menu Global subpage, computed once
on page entry and retained in a static byte.

### 12.10 Publication ordering contract

All address-to-pool transactions follow detach/publish-before-free:

1. **Replace**: allocate new, write content, PRIMASK swap address entry
   (re-read trigger bit), free old.
2. **Clear last special**: PRIMASK detach address (clear bit 14, sentinel
   offset), free old.
3. **Erase step**: detach entire address word, free old.

In-place rewrite of same-size blocks is not used. All PRIMASK sections are
under 50 ns: one load, one bit-field insert/clear, one store, no allocation
or loop.

### 12.11 Config constants

```c
#define PAT_COMPACT_SCAN_PER_TICK          16u /* reactive entries/tick */
#define PAT_RESERVATION_REDUCE_THRESHOLD   70u /* % occupancy: disable */
#define PAT_RESERVATION_RESTORE_THRESHOLD  50u /* % occupancy: re-enable */
#define PAT_REPAIR_SCAN_IDLE               16u /* entries/tick */
#define PAT_REPAIR_SCAN_BUSY                4u /* entries/tick under AutoSave */
```

### 12.12 Public API

```c
void     patSvc_init(void);
void     patSvc_tick(void);
void     patSvc_idle(void);
uint8_t  patSvc_writeStepAutomation(scene, track, step, target9, value7);
uint8_t  patSvc_removeStepAutomation(scene, track, step, target9);
void     patSvc_setStepNote(scene, track, step, note);
void     patSvc_setStepVolume(scene, track, step, volume);
void     patSvc_setStepProbability(scene, track, step, prob);
void     patSvc_eraseStep(scene, track, step);
void     patSvc_clearTrack(scene, track);
void     patSvc_clearPattern(scene);
void     patSvc_removeTrackAutomationByTarget(scene, track, target9);
void     patSvc_enqueueErase(scene, track, step);
uint8_t  patSvc_isChunkReserved(chunk);
void     patSvc_consumeReservation(chunk);
```

### 12.13 Integration points

| Location | Call |
|----------|------|
| `main.c` | `patSvc_init()` after boot filesystem ladder |
| `timebase.c` | `patSvc_tick()` after `endlessPots_tick()` |
| `filesystem.c` | `patSvc_idle()` at 5 replacement boundary points |

### 12.14 Binding constraints

1. No pool mutation path may bypass the service without explicit justification.
2. Exactly one mutation target Scene at a time.
3. TIM3 reads address entries directly; they must always point at valid or
   sentinel data.
4. PRIMASK sections must stay under 50 ns.
5. Bulk barriers must be bounded per-tick.
6. `patSvc_idle()` must be called at every filesystem replacement boundary.
7. Queue drop on full is the accepted failure mode.

### 12.15 PatternTrace stage codes

Eleven stage codes in `PatternTrace.h` for service diagnostics:

| Code | Meaning |
|------|---------|
| `H` | Pending-buffer overflow witness |
| `Q` | Queue event (enqueue/dequeue/drop) |
| `C` | Capacity drop |
| `F` | Fragmentation drop |
| `R` | Reactive relocation (historical enum name: `TIER2_RELOC`) |
| `M` | Retired Tier-1 gap relocation |
| `G` | Retired gap fallback |
| `X` | Service state change (handover, mode transition) |
| `V` | Repair created an in-place trailing reservation |
| `L` | Repair relocated a block to create a reservation |
| `D` | Direct-path mutation retained for reactive recovery |

These are PatternTrace codes in `PatternTrace.h`, distinct from the
AutoSaveTrace codes in `AutosaveTrace.h` that use the same single-letter
convention.
