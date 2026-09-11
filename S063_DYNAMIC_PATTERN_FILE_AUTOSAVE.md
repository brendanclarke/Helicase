# Session 063 — Dynamic Pattern File Format and Load/Save

**Scope**: File format, Scene/Bank/Pattern load/save, boot restore.
**Deferred to Session 064**: 17th-Scene snapshot allocation, AutoSave
pattern drain, AutoSave boot restore.

## Goal

Wire the Session 062 dynamic pattern storage to persistent media:

1. Define the v4 binary `<name>.pat` file format that serializes the address
   array, pool, bitmap, pattern parameters, and per-track settings.
2. Integrate v4 Pattern load/save into Scene and Bank filesystem paths,
   fully removing the v3 text stub (reader and writer).
3. Add root `/Pattern/` library load/save with `.hcindex` and HCNAMES
   Pattern rows (source tracking, refreshed, names).
4. Update boot restore to handle v4 pattern files.

---

## Resolved Decisions

These were open questions in the initial planning pass. The user's answers
are recorded here; no further discussion is needed.

### R1 — Track Settings Storage

Track length, scale, and shuffle are **pattern parameters** (per-track,
stored in the pattern file and in `pat_scene_region_t`). MIDI channel and
MIDI note are **scene parameters** (per-track, stored in `scene_settings_t`
or scene-level pattern metadata).

Pattern params: 3 bytes × 7 tracks × 17 scenes = **357 bytes** in
`pat_regions`. Scene params (MIDI channel/note) are stored in the scene
struct, not in the pattern region.

**Decision**: Store the 3 per-track pattern params in
`pat_scene_region_t` (as a `uint8_t track_params[7][3]` or similar) —
no padding in SRAM, only the bytes actually used. The file format pads
each track's block to 16 bytes and the pattern-level block to 16 bytes
so future parameters can be added without a format version bump (but
those would require a corresponding SRAM allocation when implemented).
The no-op setters in PatternData.c become real storage writers.
`parameter_values[]` remains the Menu display buffer; the setters copy
through to the region storage.

**parameter_values[] status**: still used by Menu for display, MIDI CC
dispatch, encoder editing, Euklid, screensaver, morph, and other UI. It is
not retired this session. `parameters2[]` is the morph endpoint buffer
and is also still active (morph interpolation, morph-kit save/load,
SHIFT+VOICE endpoint editing).

### R2 — 17th Scene Allocation

**Deferred to Session 064.** The 17th Scene is for AutoSave snapshot and
future background Bank Load. It is not needed for the Session 063 file
format and load/save work.

When allocated: if a separate `scene_temp` of the same struct type is
simpler than bumping `SCENE_COUNT`, that is acceptable. The implementation
will choose the simpler approach.

### R3 — File Count in Directories

The 32 AutoSave files in root are not a concern for FAT32 (unlimited root
directory entries). `.pat00a` etc. fit 8.3 format (7 chars, no LFN needed).

**Broader risk noted**: the `/Pattern/`, `/Kit/`, `/Scene/`, `/Bank/`, and
`/Instrument/<type>/` library directories each need to support 1,000+
files. If total file count per directory is a real FAT limitation, it will
surface there first. This is not a Session 063 blocker but should be tested
at some point — if asyncfatfs directory iteration performance degrades at
high entry counts, that's a platform issue independent of pattern storage.

### R4 — Snapshot Timing and TIM3

**Deferred to Session 064** (snapshot is AutoSave-only).

For reference, the analysis: TIM3 runs `seq_tick()` at priority 2 (4 kHz).
The existing main-loop BASEPRI mask (`irq_setBasepri(6u << 4)`) leaves
TIM3 **unmasked** — it only blocks priority ≥ 6. To mask TIM3 during the
snapshot memcpy, the code would either:
- Set BASEPRI to `2u << 4` (blocks priority ≥ 2), which masks TIM3
  but leaves SysTick (priority 0) and TIM1 encoder capture (priority 1)
  running. This **delays** one or more 250 µs sequencer ticks for the ~50 µs
  memcpy duration. The ticks are not lost — TIM3's update interrupt flag
  latches and fires immediately when unmasked. At 4 kHz, a single delayed
  tick shifts the sequencer by ≤ 50 µs, which is inaudible.
- Or disable TIM3 via NVIC_ICER (targeted, no BASEPRI change). Same
  timing effect but more surgical.

**The sequencer does not lose ticks. Timing is delayed by the memcpy
duration (~50 µs), not skipped.** This is safe for musical timing. The
decision on which masking approach to use will be made in Session 064.

### R5 — Scene Save Coherency

**Not a concern.** Entering the Load/Save menu disables live recording and
MIDI parameter input. Nothing can mutate Scene state while the user is
interacting with the menu. The v4 writer serializes directly from the live
`pat_regions[scene]` during Scene Save without a snapshot. This may be
revisited when the live recorder and MIDI input are fully wired, but for
now it is correct.

### R6 — v3 Format Removal

**v3 is completely removed.** No v3 reader, no v3 writer, no fallback, no
probe. The v4 binary format is the sole authority for both internal storage
and file format. All v3 text pattern code (reader phases 46/53, writer
phases, `storage_patternStub*` helpers, `filesystem_pattern_discard`, the
`PatternSet` typedef and its helpers) is deleted. If existing Scene/Bank
libraries contain v3 `pattern.pat` files, they are treated as invalid/
missing and the Scene loads with an empty pattern.

**Consequence**: `PatternSet`, `pat_initPatternSet()`,
`pat_patternSetGetStep()`, `pat_patternSetSetStep()` are all removed from
PatternData.h/c. `filesystem_pattern_discard` is removed from
filesystem.c. The `storageTypes.c` pattern stub parser is removed.

### R7 — Pattern Names, HCNAMES, and .hcindex

Patterns get names like every other file type. They are **not** stored as
`pattern.pat` inside Scene directories. Instead:

- **In a Scene directory**: `<name>.pat` (e.g., `intro.pat`). The name is
  the pattern's identity.
- **In `/Pattern/` library**: `NNN <name>.pat` (e.g., `000 init.pat`).
  Numbered library slot format, same as Kit/Scene/Bank.
- **In a Bank child directory**: `<name>.pat` inside each `NN/` child,
  same as the Scene directory case.

**HCNAMES expansion**: add 16 Pattern rows to `.hcnames`, one per resident
Scene's pattern. Row layout matches existing rows: `name<TAB>source[<TAB>R]`.
Pattern rows follow Kit rows (or follow Instrument rows — exact placement
TBD during implementation).

- `FS_RESIDENT_NAMES_ROW_COUNT` increases from 129 to 145.
- `fs_resident_source` grows from 258 to 290 bytes.
- `hcnames_name_mirror` grows from 1,161 to 1,305 bytes.

Source tracking: patterns get `@` (AutoSave provenance), `000..999`
(library source), and `-` (no source / default) tokens, same as other
object types. `R` (refreshed) flag works identically.

**`/Pattern/.hcindex`**: a new `.hcindex` file in the `/Pattern/` directory,
generated at boot and refreshed on Pattern Save, same as Kit/Scene/Bank
indexes. The existing `FS_FILE_PATTERN` slot in the filesystem type
registry is reused.

### R8 — Session Split

**Session 063**: v4 file format definition, v3 removal, per-track pattern
params in `pat_scene_region_t`, Scene Save/Load with v4, Bank Save/Load with v4,
root `/Pattern/` library load/save, Pattern HCNAMES rows, `/Pattern/
.hcindex`, boot restore with v4, and hardware verification.

**Session 064**: 17th-Scene allocation (snapshot or `scene_temp`), AutoSave
dirty tracking, AutoSave pattern drain with snapshot, AutoSave `.patNNx`
pair writer, AutoSave pattern boot restore, and hardware verification.

### R9 — v3 Removal Thoroughness

The v3 format is fully nuked. After Session 063, there should be zero
references to the v3 text pattern format in live code. If the load/save
state machine flow has redundant or sloppy patterns exposed by the removal,
a refactor session can be taken before Session 064's AutoSave work.

### R10 — Pattern Error Invalidates Scene

A pattern read error (PAT_STACK_SIZE mismatch, corrupt file, missing file)
**invalidates the entire Scene load**. This is a deliberate choice: if the
user is changing allocation sizes, they accept the risk. A future offline
Python tool may resize pattern files, but the firmware does not attempt
partial recovery.

---

## Part A — v4 Pattern File Format

### A.1 Layout

```
Offset  Size          Content
──────  ────          ───────
── Fixed header (32 bytes) ──
0       4             Magic: "PAT4" (0x50 0x41 0x54 0x34)
4       2             Format version: 1 (uint16_t LE)
6       2             PAT_STACK_SIZE used when writing (uint16_t LE)
8       2             Header total size in bytes (uint16_t LE) — for
                      forward-compatible skipping
10      4             Generation counter (uint32_t LE) — library saves
                      write 0; AutoSave (Session 064) increments from 1;
                      boot takes the highest valid generation
14      4             CRC32C (uint32_t LE) — treated as zero during
                      computation
18      14            Reserved (zero-padded to 32)

── Pattern-level parameters (16 bytes, padded for future use) ──
32      1             pattern_change_bar
33      1             pattern_next
34      14            Reserved pattern params (zero-padded)

── Per-track parameters (7 tracks × 16-byte block, padded for future use) ──
48      7 × 16 = 112  Per-track block:
                        [0] uint8_t  length
                        [1] uint8_t  scale
                        [2] uint8_t  shuffle
                        [3..15]      Reserved (13 bytes, zero-padded)

═══════════════════════════════════════════════════════
TOTAL HEADER = 160 bytes
(32 fixed + 16 pattern params + 7 × 16 per-track)
═══════════════════════════════════════════════════════

── Static address array (fixed size, never changes) ──
160     1,792         uint16_t address[7][128], native LE byte order

── Occupancy bitmap (fixed size, never changes) ──
1,952   512           bitmap[512], stored as-is

── Dynamic pool (variable based on PAT_STACK_SIZE) ──
2,464   PAT_STACK_SIZE × 32   Pool bytes for all addressable chunks
                              (currently 256 × 32 = 8,192 bytes)
```

**Corrected pool size**: the bitmap is bit-packed (one bit per 4-byte
chunk, not one byte), so each PAT_STACK_SIZE bitmap byte covers 8
chunks × 4 bytes = 32 pool bytes. The addressable pool is therefore
`PAT_STACK_SIZE × 32`, not `× 4`. Code confirms: `pat_poolAlloc`
scans `PAT_STACK_SIZE * 8` chunks; `pool[PAT_STACK_SIZE * 32]`.

**Total file size**: 160 + 1,792 + 512 + (PAT_STACK_SIZE × 32) bytes.
At PAT_STACK_SIZE=256: **10,656 bytes** (~21 SD sectors).

### A.2 Compatibility

- **Same or smaller PAT_STACK_SIZE in file vs firmware**: load succeeds;
  extra firmware-side bitmap/pool slots stay at init state.
- **Larger PAT_STACK_SIZE in file than firmware**: **error — Scene
  invalidated** (R10).
- **Magic/version mismatch or missing file**: error — Scene invalidated.
- **Header size field**: reader skips to `header_size` before the address
  array, so a future v1.x can add header fields without version bump.

### A.3 CRC32C

A CRC32C covers the entire file (treating its own 4-byte field as zero
during computation). This validates the AutoSave A/B pair selection and
catches truncated/corrupt files. The CRC field lives at fixed header
offset 14, inside a 32-byte fixed header (expanded from 16 to fit the
4-byte CRC with reserved padding to byte 32).

**Resolved**: CRC32C (Castagnoli, 0x82F63B78 reflected), same software
byte-at-a-time implementation as `.hcprms` AutoSave records
(`autosave_crc32cByteUpdate()` in Autosave.c). Hardware CRC32C
acceleration deferred to SCOPING_TARGETS.md.

---

## Part B — Load/Save Integration

### B.1 Scene Load

The Scene loader currently opens `pattern.pat` relative to the Scene
directory and enters the v3 text reader. After v3 removal:

1. Scan the Scene directory for a `*.pat` file (the pattern's name is the
   filename stem, not a fixed `pattern.pat`).
2. Open and read the v4 binary file.
3. Validate magic, version, PAT_STACK_SIZE, CRC. On any failure → Scene
   invalidated.
4. Read header → apply track settings to `pat_regions[scene]` and any
   scene-level per-track settings.
5. Read address array → `pat_regions[scene].address`.
6. Read bitmap → `pat_regions[scene].bitmap`.
7. Read pool → `pat_regions[scene].pool`.
8. Register the pattern name in HCNAMES.

### B.2 Scene Save

The Scene writer currently opens `pattern.pat` and writes v3 text. After
v3 removal:

1. Create/overwrite `<name>.pat` in the Scene directory.
2. Write 160-byte header (32B fixed + 16B pattern params + 7×16B
   per-track, including track settings from `pat_regions[scene]`).
3. Write address array (1,792 B), bitmap (512 B), pool
   (PAT_STACK_SIZE × 32 = 8,192 B).
4. CRC32C computed over the full file (CRC field zeroed during
   computation), written into the header at offset 14.
5. Update HCNAMES pattern row.
6. The writer serializes directly from `pat_regions[scene]` (R5 — no
   snapshot needed during Save because input is disabled).

### B.3 Bank Load/Save

Bank delegates each child through the shared Scene loader/writer. No
separate Bank-specific pattern handler. Each Bank child's `<name>.pat`
loads/saves through B.1/B.2.

### B.4 Boot Restore

The existing boot path does best-effort pattern loads per accepted Scene.
Replace the v3 reader call with the v4 reader. Missing or invalid pattern
files result in an empty pattern for that Scene (via `pat_initScene`
called before the reader).

### B.5 Root `/Pattern/` Library

- `/Pattern/` directory on SD, same as `/Kit/`, `/Scene/`, `/Bank/`.
- Files are `NNN <name>.pat` (numbered library slots).
- `/Pattern/.hcindex` generated at boot, refreshed on Save.
- Load: reads the v4 file into the target Scene's pattern region,
  replacing its entire pattern (address + pool + bitmap + track settings).
  Does not touch Kit, Effect, or Scene settings.
- Save: writes the target Scene's pattern region to the selected library
  slot.
- The Load/Save type cycler includes Pattern alongside Kit, Scene, Bank.
- Pattern gets its own browser cache domain in the shared 9,000-byte
  cache, same as other file types.

---

## Part C — HCNAMES and Source Tracking

### C.1 Row Expansion

HCNAMES grows from 129 to 145 data rows (+ 1 header = 146 physical lines):

| Rows | Range | Content |
|------|-------|---------|
| 0 | 0 | Bank |
| 1–16 | 1..16 | Scene |
| 17–32 | 17..32 | Kit |
| 33–128 | 33..128 | Instrument (6 per Scene × 16) |
| **129–144** | **129..144** | **Pattern (1 per Scene × 16)** |

Pattern rows use the standard `name<TAB>source[<TAB>R]` format. No type
field (unlike Instrument rows).

### C.2 Source Tokens

- `-` : no source / empty / default pattern
- `000..999` : library `/Pattern/NNN` source
- `@` : AutoSave provenance (Session 064)

### C.3 Refreshed Flag

`R` flag works identically to Scene/Kit/Instrument: cleared on mutation,
set on successful AutoSave drain (Session 064). Library load/save set
source to slot number and mark refreshed.

### C.4 SRAM Impact

- `fs_resident_source`: 129 → 145 × 2 B = 290 B (+32 B)
- `hcnames_name_mirror`: 129 → 145 × 9 B = 1,305 B (+144 B)
- Static asserts and row-count constants updated.

### C.5 Publication

Scene Load, Scene Save, Bank Load, and Bank Save publication paths include
the pattern row alongside Scene/Kit/Instrument rows when committing
HCNAMES. Pattern Load from library sets the source to the library slot.

---

## Part D — v3 Removal Checklist

All of the following are deleted in Session 063:

| Item | File |
|------|------|
| `PatternSet` typedef and static assert | PatternData.h |
| `pat_initPatternSet()` | PatternData.h, PatternData.c |
| `pat_patternSetGetStep()` | PatternData.h, PatternData.c |
| `pat_patternSetSetStep()` | PatternData.h, PatternData.c |
| `filesystem_pattern_discard` | filesystem.c |
| v3 text reader phases (46, 53, associated) | filesystem.c |
| v3 text writer phases | filesystem.c |
| `storage_patternStub*` helpers | storageTypes.c/h |
| `op_pattern_stub_state` | filesystem.c |
| `FS_PATTERN_STEP_*` / `FS_PATTERN_MAIN_*` etc. defines | filesystem.c |
| Retired `#if 0` binary Step reader phases | filesystem.c |
| Any remaining `PatternSet` references in live code | grep verification |

---

## Resolved Open Questions

### RQ1 — Pattern Name in Scene Directory

**Decided**: scan by extension. A Scene directory contains exactly one
`*.pat` file; the loader finds it by extension. More than one `.pat` file
in a Scene directory is an error (reject the Scene load). The save path
creates it with the user-chosen name; the load path scans for `*.pat`.

### RQ2 — Pattern Name Default

**Decided**: inherit from Scene name. A new/empty Scene that has never
been saved uses the Scene's own name as the default pattern name. This
sets the initial HCNAMES pattern row content and the filename used on
first save.

### RQ3 — Filesystem Phase Numbering

The v3 reader occupied phases 46/53 and the v3 writer occupied nearby
phases. The v4 binary reader/writer needs new phase numbers. The
filesystem state machine is large (~18,000+ lines). Phase number
allocation must be carefully chosen to avoid collisions. This is a
mechanical implementation detail, not a design risk, but it requires
careful code navigation.

### RQ4 — Pattern Identity at Scene Switch

When the user switches Scenes, `pat_applyStepToMenu()` already reads the
correct Scene's pattern region. But track settings (length, scale, etc.)
need to be projected from the new Scene's `pat_scene_region_t` into
`parameter_values[]` so the Menu displays the correct values. This is
analogous to `pat_applyTrackSettingsToMenu()` and
`pat_applyPatternSettingsToMenu()` — these must become real projections
from the new region storage rather than no-ops.

### RQ5 — HCNAMES Row Count Change

**Decided**: not a concern. The existing 9 KB index/names buffer is used
to serialize HCNAMES; 145 rows fits with plenty of room. The migration
from 129 to 145 rows is a one-time boot regeneration — the boot path
already handles missing/invalid `.hcnames`. A row count check in the
validator catches old-format files.

**Note**: a future session will add another 16 rows for Effects (145→161).
Same approach — expand the row count, regenerate on first boot.

---

## Implementation Order (Session 063)

| Step | What | Dependencies |
|------|------|-------------|
| 1 | Add per-track pattern params (length/scale/shuffle) to `pat_scene_region_t`, make setters real | None |
| 2 | Define v4 format constants, header struct, static asserts | None |
| 3 | v3 removal: delete PatternSet, v3 reader/writer, pattern stub | None |
| 4 | HCNAMES expansion: 145 rows, Pattern row base/accessors | None |
| 5 | `/Pattern/.hcindex` boot generation and save refresh | Step 4 |
| 6 | Implement v4 binary writer in filesystem.c (Scene Save path) | Steps 1–3 |
| 7 | Implement v4 binary reader in filesystem.c (Scene Load path) | Steps 1–3 |
| 8 | Wire Pattern name into Scene save/load (RQ1 resolution) | Steps 6, 7 |
| 9 | Wire HCNAMES Pattern row publication in Scene/Bank paths | Steps 4, 8 |
| 10 | Root `/Pattern/` library load/save with browser | Steps 5–7 |
| 11 | Update boot pattern loading to v4 | Step 7 |
| 12 | Bank Save/Load verification | Steps 6, 7 |
| 13 | Hardware verification: save Scene, load Scene, verify specials | All |

---

## RAM Budget Impact (Session 063 Only)

| Allocation | Size | Region |
|------------|-----:|--------|
| Per-track settings in `pat_scene_region_t` (16 scenes × 23 B) | 368 B | SRAM1 `.bss` (inside `pat_regions`) |
| `fs_resident_source` growth (129→145 rows) | +32 B | SRAM1 `.bss` |
| `hcnames_name_mirror` growth (129→145 rows) | +144 B | SRAM1 `.bss` |
| **Total Session 063** | **544 B** | SRAM1 |
| **Removed**: `filesystem_pattern_discard` | −112 B | SRAM1 `.bss` |
| **Removed**: `op_pattern_stub_state` | −5 B | SRAM1 `.bss` |
| **Net Session 063** | **~427 B** | SRAM1 Pattern reservation |

SRAM1 remaining after: 117,532 − 421 = **~117,111 B** (114.4 KB).

Session 064 adds the 17th Scene snapshot (~11,696 B) and AutoSave dirty
mask (2 B), bringing the total down to ~104,818 B (102.4 KB).
