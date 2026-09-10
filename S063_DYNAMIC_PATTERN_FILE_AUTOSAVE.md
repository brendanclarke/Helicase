# Session 063 — Dynamic Pattern File Format, Load/Save, and AutoSave

## Goal

Wire the Session 062 dynamic pattern storage to persistent media:

1. Define a binary v4 `pattern.pat` file format that serializes the address
   array, pool blocks, and track settings.
2. Integrate Pattern load/save into the existing Scene and Bank filesystem
   paths, replacing the v3 text stub.
3. Add root `/Pattern/` library load/save for standalone patterns.
4. Add per-pattern AutoSave with 16 pairs of hidden root files, using a
   17th-Scene snapshot region as the coherent serialization source.
5. Update boot restore to handle v4 pattern files in Scene/Bank/AutoSave.

---

## Part A — v4 Pattern File Format

### A.1 Design

One binary file per Scene pattern: `pattern.pat` inside each Scene or Bank
child directory, or `NNN Name.pat` in the root `/Pattern/` library.

### A.2 Layout

```
Offset  Size          Content
──────  ────          ───────
0       4             Magic: "PAT4" (0x50 0x41 0x54 0x34)
4       2             Format version: 1 (uint16_t LE)
6       2             PAT_STACK_SIZE used when writing (uint16_t LE)
8       2             Header total size in bytes (uint16_t LE) — for
                      forward-compatible skipping
10      2             Reserved (zero)

── Pattern parameters (padded to fixed size) ──
12      1             pattern_change_bar
13      1             pattern_next
14      14            Reserved pattern params (zero-padded, future use)

── Per-track parameters (7 tracks × fixed block) ──
28      7 × 8 = 56    Per-track block:
                        [0] uint8_t  length
                        [1] uint8_t  scale
                        [2] uint8_t  shuffle
                        [3] uint8_t  midi_channel
                        [4] uint8_t  midi_note
                        [5..7]       reserved (3 bytes, zero-padded)

── Header padding ──
84      44            Reserved (zero-padded to header boundary)

═══════════════════════════════════════════════════════
FIXED HEADER END = 128 bytes
═══════════════════════════════════════════════════════

── Static address array (fixed size, never changes) ──
128     1,792         uint16_t address[7][128], stored as-is (LE byte order,
                      native ARM layout — these are already LE in SRAM)

── Occupancy bitmap (fixed size, never changes) ──
1,920   512           bitmap[512], stored as-is

── Dynamic pool (variable based on PAT_STACK_SIZE) ──
2,432   PAT_STACK_SIZE × 4    Pool bytes for all addressable chunks
                              (currently 256 × 4 = 1,024 bytes)
```

**Total file size**: 128 + 1,792 + 512 + (PAT_STACK_SIZE × 4) bytes.
At PAT_STACK_SIZE=256: **3,456 bytes**.

### A.3 Compatibility Rules

- **Reader opens a file with the same or smaller PAT_STACK_SIZE**: load
  succeeds, extra bitmap/pool slots remain at their initialized (free) state.
- **Reader opens a file with a larger PAT_STACK_SIZE than firmware**: return
  error. The file references pool offsets the firmware cannot address.
- **Magic/version mismatch**: return error (do not attempt v3 fallback in
  the same path; v3 is a text file and is visually distinguishable).
- **Header size field**: the reader skips to `header_size` before reading
  the address array, so a future v1.x revision can add header fields
  without bumping the version.

### A.4 Relationship to v3

The v3 `pattern.pat` text format (seven 32-hex-character rows = 112-byte
trigger bitmap) remains the fallback **reader** for existing Scene/Bank
libraries. The v4 binary format becomes the sole **writer**. On load, the
reader probes the first four bytes: `PAT4` → v4 binary path; otherwise →
existing v3 text path. The v3 reader continues to populate only trigger bits
(no specials, no track settings from v3 sources).

### A.5 Track Settings Source

Track length, scale, shuffle, MIDI channel, and MIDI note are currently
stored in `parameter_values[]` (Menu parameter buffer) and serviced by
no-op PatternData setters. **Decision point**: Session 063 must give these
values a real per-Scene per-track home.

Options:
1. **Store in the pattern file and in `pat_scene_region_t`**: add a
   per-track settings struct to the region. This is the cleanest long-term
   answer but adds `7 × 8 = 56 bytes × 16 scenes = 896 bytes` to
   `pat_regions`.
2. **Store in the pattern file and read into `parameter_values[]` at Scene
   switch**: no RAM growth, but settings are lost if the user edits track
   settings before a save. This is the current behavior.
3. **Store in `scene_settings_t`**: these are really Scene-level settings,
   not Pattern-level. But they travel with the pattern in the file.

**Recommendation**: Option 1 — add a small per-track settings block to the
region. The 896-byte cost is within the Pattern reservation. Track settings
are Pattern data (they describe how the pattern plays) and should live with
the pattern. The alternative keeps the current fragile
`parameter_values[]` dependency.

**Risk**: This is a structural change to `pat_scene_region_t` and increases
`pat_regions` by 896 bytes. Needs explicit RAM approval.

---

## Part B — Load/Save Integration

### B.1 Scene Load Path

The existing Scene loader in `filesystem.c` handles `pattern.pat` as a
child file of each Scene directory. Currently it enters the v3 text
reader (phase 46/53) which populates `filesystem_pattern_discard` and then
applies trigger bits to the target Scene's address array.

**Changes**:
1. Phase 46 probes magic bytes: if `PAT4`, branch to new v4 binary reader
   phases; otherwise continue to existing v3 text parser.
2. v4 reader loads header → validates version and PAT_STACK_SIZE → reads
   track settings → reads address array directly into target
   `pat_regions[scene].address` → reads bitmap into
   `pat_regions[scene].bitmap` → reads pool into
   `pat_regions[scene].pool`.
3. On PAT_STACK_SIZE mismatch (file > firmware), set error and skip pattern
   (Scene loads with empty pattern rather than aborting entirely).
4. `pat_initScene()` must be called **before** the pattern reader starts, to
   clear any prior state. This is already the case for Scene Load.

### B.2 Scene Save Path

The existing Scene writer opens `pattern.pat` and writes v3 text rows.

**Changes**:
1. Replace v3 text writer with v4 binary writer.
2. Write header (128 bytes) → address array (1,792 bytes) → bitmap
   (512 bytes) → pool (PAT_STACK_SIZE × 4 bytes).
3. The writer can serialize directly from the live `pat_regions[scene]`
   because Scene Save does not run concurrently with live playback edits
   to the saved Scene. (The active Scene is always the one being saved,
   and the user cannot edit steps while Save is in progress because the
   Menu is in Save-command mode.)

### B.3 Bank Load Path

Bank Load iterates selected local children, each delegating through the
shared Scene loader. The pattern file reader is the same as B.1 — no
separate Bank-specific pattern reader is needed. Each child Scene gets its
own `pattern.pat` read via the standard Scene child handler.

### B.4 Bank Save Path

Bank Save iterates selected children and writes each through the Scene
writer. Same as B.2 — each child Scene gets a v4 `pattern.pat` via the
standard Scene writer.

### B.5 Boot Restore

The existing boot path loads `pattern.pat` per accepted Scene from the
resolved library source (best-effort). The v4/v3 probe at phase 46 handles
this transparently: boot Scenes with v4 files get full specials, boot
Scenes with v3 files get trigger-only patterns.

### B.6 Root `/Pattern/` Library

Standalone pattern files in `/Pattern/NNN Name.pat` with the same v4
format. Load replaces only the target Scene's pattern region (address +
pool + bitmap + track settings), not Kit/Effect/Scene settings. Save writes
only the pattern region.

**Implementation**: reuse the existing `FS_FILE_PATTERN` file type slot
with the v4 binary format. The load page already cycles through Pattern as
a type; the implementation just needs the reader/writer pair.

**Risk**: The current FS_FILE_PATTERN path uses the retired binary Step
reader (inside `#if 0`). This needs to be replaced entirely, not revived.

---

## Part C — 17th Scene Snapshot Region

### C.1 Rationale

AutoSave serializes pattern data to SD while the sequencer may be writing
pool blocks (live step edits, recording, probability state changes). A
several-KB file write spans many main-loop ticks. During that window, the
user or sequencer could modify the address array or pool, creating an
inconsistent snapshot — e.g., an address entry points to a pool offset
whose block was freed and reallocated between the address-array write and
the pool write.

**Solution**: allocate a 17th Scene slot (both `scene_t` and
`pat_scene_region_t`) as a coherent snapshot buffer. Before an AutoSave
pattern drain begins, memcpy the target Scene's pattern region (10,496
bytes) into the snapshot slot, then serialize from the snapshot at leisure.

### C.2 SRAM Cost

| Component | Size |
|-----------|-----:|
| `pat_scene_region_t` (17th slot) | 10,496 B |
| `scene_t` (17th slot) | 1,200 B |
| **Total** | **11,696 B** |

Current SRAM1 remaining: 117,532 B. After 17th Scene: **105,836 B**
(103.4 KB remaining), all still within the Pattern reservation.

### C.3 Implementation

- Change `SCENE_COUNT` from `16u` to `17u` in `SceneData.h`.
- Change `pat_regions` array size to `SCENE_COUNT` (already uses
  `SCENE_COUNT`; this is automatic).
- Define `PAT_SNAPSHOT_SCENE 16u` — the index of the snapshot slot.
- The snapshot Scene is **never** initialized by `pat_initScene()` at boot
  (it's scratch). It is never addressed by the sequencer, menu, LED, or
  copy/clear code.
- Before AutoSave pattern drain: `memcpy(&pat_regions[16],
  &pat_regions[target_scene], sizeof(pat_scene_region_t))`.
- The AutoSave writer then serializes from `pat_regions[16]` exclusively.

### C.4 Future Background Loading

The 17th Scene slot is allocated now with the dual purpose of:
1. **Session 063**: AutoSave coherent snapshot (pattern region only)
2. **Future session**: background Bank Load staging (full scene_t + pattern
   region — cache the playing Scene while loading a new Bank)

These two uses are mutually exclusive: AutoSave does not drain during Bank
Load, and background loading does not happen during normal AutoSave
operation. No additional allocation is needed when the background loader is
eventually implemented.

### C.5 Guard Rails

- The 17th `scene_t` slot exists but is unused this session (only the
  pattern region is used for snapshot). Scene activation, UI, and the
  sequencer must never address Scene index 16.
- `bank_getActiveScene()` and `scene_get()` already validate against
  `SCENE_COUNT`; the 17th slot passes validation but is never selected by
  any user path.
- HCNAMES has 16 Scene rows (1..16). The 17th Scene has no HCNAMES row.
- AutoSave dirty mask has 16 Scene slots. The 17th Scene has no dirty slot.
- Bank `scene_present_mask` is 16 bits. The 17th Scene has no presence bit.

---

## Part D — Pattern AutoSave

### D.1 File Naming

16 pairs of hidden root files, one pair per resident Scene's pattern:

```
/.pat00a  /.pat00b    — Scene 0 pattern
/.pat01a  /.pat01b    — Scene 1 pattern
...
/.pat15a  /.pat15b    — Scene 15 pattern
```

32 files total. Each file is identical in format to the v4 `pattern.pat`
(3,456 bytes at PAT_STACK_SIZE=256).

### D.2 Format

Identical to the v4 file format from Part A. The data is already largely
serialized in the pattern region; the snapshot memcpy + direct write is the
simplest and most robust approach.

**Why not differential/patch format like HCPR?** The pattern data structure
(address array + pool) is not a flat parameter array where individual byte
offsets can be meaningfully patched. A modified address entry and its
associated pool block are semantically coupled; patching one without the
other creates inconsistency. The full snapshot is only ~3.5 KB and the
memcpy from live→snapshot is a single O(10KB) operation completing in
microseconds.

### D.3 Dirty Tracking

Each Scene's pattern has a single dirty bit. The dirty bit is set by:
- `pat_toggleStep()`
- `pat_setStepActive()`
- `pat_eraseStep()`
- `pat_clearTrack()` / `pat_clearPattern()`
- `pat_setStepNote()` / `pat_setStepVolume()` / `pat_setStepProbability()`
- `pat_setTrackLength()` / `pat_setTrackScale()` / `pat_setTrackShuffle()`
  (once these become real storage)
- Pattern load (Scene Load, Bank Load, Pattern Load) — marks dirty to
  force initial capture
- `pat_copyTrack()` / `pat_copyPattern()` / `pat_copyBar()` — when
  eventually implemented

A 16-bit `pat_autosave_dirty_mask` is sufficient (one bit per Scene).

### D.4 Drain Scheduling

The AutoSave drain currently alternates between parameter capture and
HCNAMES convergence. Pattern drain is a third phase:

**Proposed drain cycle**:
1. **Parameter scan/capture** (existing HCPR drain)
2. **Pattern check** — if any dirty bit is set:
   a. Prioritize non-active dirty Scene (user probably done with it)
   b. If only the active Scene is dirty, drain it (but expect re-dirtying)
   c. Snapshot the selected Scene into slot 16
   d. Write the snapshot to the corresponding `.patNNx` file
   e. Clear the dirty bit on successful write
   f. The drain alternation returns to step 1

**Interleaving**: the drain checks one pattern per parameter-drain cycle.
If multiple Scenes are dirty, they serialize across multiple drain cycles
(one Scene per cycle, non-active first).

### D.5 Boot Restore

Boot restore already loads `pattern.pat` from resolved Scene library
sources. The Pattern AutoSave restore would:

1. After the existing HCPR restore selects/accepts Scenes, check for
   `.patNNa` / `.patNNb` pairs for each accepted Scene.
2. Select the newer valid pair (by generation or by presence, matching
   HCPR's A/B selection logic).
3. If the AutoSave pattern is newer than the library `pattern.pat` that
   was already loaded, overwrite the Scene's pattern region with the
   AutoSave version.

**Open question**: How does the reader know the AutoSave pattern is "newer"?
HCPR has generation counters. Pattern files need a comparable mechanism —
either a generation field in the v4 header or a timestamp or an HCPR
cross-reference.

### D.6 Pair Management

Each `.patNNa` / `.patNNb` pair uses the same A/B alternation as HCPR:
- Write to the non-current file
- Increment generation counter
- Validate via header magic + CRC32C
- On successful write, the new file becomes current

The generation field in the v4 header (Part A reserved bytes) serves this
purpose. Alternatively, use a separate small sidecar — but since the file
is already small (3.5 KB), embedding the generation is simpler.

---

## Part E — Boot Reader Updates

### E.1 Scene Boot Pattern Loading

The current boot path does best-effort `pattern.pat` library loads per
accepted Scene. This path needs:
1. v4/v3 probe (same as B.1)
2. v4 binary reader for boot Scenes
3. Subsequent AutoSave pattern overlay (D.5) if applicable

### E.2 AutoSave Pattern-Aware Boot

After HCPR restore and Scene library pattern loads:
1. For each present Scene, check for valid `.patNN{a,b}` pairs
2. Select the valid pair with the higher generation
3. If its generation is higher than what the library load produced,
   replace the Scene's pattern region

### E.3 Deferred

AutoSave pattern boot restore may be deferred to a later session if the
file format and save/load integration are already a full session's work.
The pairs can be written without a boot reader; the reader can follow once
the format is proven stable.

---

## Risks and Open Questions

### R1 — Track Settings Home (Decision Required)

Track length, scale, shuffle, MIDI channel, and MIDI note currently live in
`parameter_values[]` and the PatternData setters are no-ops. The v4 file
format needs to know where to read/write these values. Options:
1. Add per-track storage to `pat_scene_region_t` (+896 B to `pat_regions`)
2. Store in `scene_settings_t` (conceptually odd but zero extra RAM)
3. Keep in `parameter_values[]` and project from there (fragile)

**Recommendation**: Option 1. The RAM cost is modest and the ownership is
clean.

### R2 — SCENE_COUNT=17 Ripple

Changing `SCENE_COUNT` from 16 to 17 may ripple through:
- `scenes[]` array grows by 1,200 B
- `pat_regions[]` grows by 10,496 B
- Bank `scene_present_mask` (16-bit) — unaffected (17th Scene has no
  presence bit)
- HCNAMES row count (129 rows, 16 Scenes) — unaffected
- AutoSave HCPR Scene payload (16 × 1,920 B) — unaffected
- Menu Scene select (0..15) — unaffected (17th is not user-selectable)
- `scene_get(16)` — currently returns NULL if >= SCENE_COUNT; with
  SCENE_COUNT=17 it returns the 17th slot, which is correct for internal
  snapshot use but must never be exposed to normal Scene UI paths

**Risk**: Any code that iterates `0..SCENE_COUNT-1` for UI/Bank/HCNAMES
purposes will now include the 17th Scene. These loops must be audited and
gated to iterate only 0..15 for user-visible operations.

**Mitigation**: Define `PAT_USER_SCENE_COUNT 16u` and use it in all
user-facing loops. `SCENE_COUNT` remains the physical allocation count.

### R3 — AutoSave File Count

32 hidden files in the root directory is a significant number. Combined with
the existing `.hcprms1`, `.hcprms2`, `.hcnames`, and `settings.cfg`, the
root directory carries 36 hidden/system files.

**Risk**: FAT root directory entry limits (512 entries for FAT16, unlimited
for FAT32). At 32-byte entries per LFN (or 32 bytes for 8.3 names), 36
files is trivially within any limit. The names `.pat00a` etc. fit in 8.3
format (7 characters), so no LFN entries are needed.

**Risk**: AsyncFATFS file handle pool exhaustion. The writer uses one handle
at a time (open, write, close, then next). This is safe.

### R4 — Snapshot Timing

The 10,496-byte memcpy from live region → snapshot must happen at a point
where no concurrent write is possible. The safe window is:
- Inside the main-loop drain, before posting the filesystem write
- The sequencer timer ISR (TIM3, priority 2) can fire during memcpy and
  call `pat_toggleStep` or `pat_setStepActive` from recording

**Mitigation**: Briefly mask TIM3 (BASEPRI) during the snapshot memcpy.
The memcpy at 216 MHz takes ~50 µs for 10 KB; this is well within the
4 kHz tick budget (250 µs). Alternatively, use a dirty-generation counter:
snapshot, check if generation changed, re-snapshot if so (optimistic
locking).

**Recommendation**: BASEPRI mask during memcpy. It's simpler and the
duration is negligible.

### R5 — v4 Save Coherency for Scene Save

Scene Save serializes from the live `pat_regions[scene]` (B.2). Is this
safe? During Save, the menu is in Save-command mode (`menu_storageBusy`),
which blocks step editing. But the sequencer is still running — probability
does not modify storage, but recording does. If the user is recording while
saving, address entries could change mid-write.

**Mitigation**: Use the same snapshot approach as AutoSave: memcpy the
pattern region to slot 16 at the start of the Scene Save pattern phase,
then write from the snapshot. This unifies the coherency strategy.

### R6 — v3 Write Retirement

After v4 becomes the sole writer, existing v3 `pattern.pat` files in
Scene/Bank directories will be overwritten with v4 binary content on the
next save. This is a one-way migration — the v4 writer will produce files
that older firmware cannot read.

**Risk**: Users with mixed firmware versions sharing SD cards. This is
accepted — the port is already incompatible with original LXR firmware at
the Kit/Scene/Bank level.

### R7 — Pattern AutoSave Generation Cross-Reference

How does boot restore know whether the AutoSave pattern is newer than the
library pattern? Options:
1. **Generation counter in v4 header**: AutoSave bumps generation on each
   write; library saves start at generation 0. Boot compares.
2. **HCPR cross-reference**: add a per-Scene pattern generation field to
   the HCPR record. This couples pattern and parameter AutoSave.
3. **Always prefer AutoSave if valid**: simpler, but loses explicit library
   saves until the next AutoSave cycle.

**Recommendation**: Option 1 — generation counter in the v4 header. Simple,
self-contained, no HCPR format change needed. Library saves reset generation
to 0; AutoSave writes increment from 1. Boot takes the highest generation.

### R8 — Session Scope

This is a large session. The implementation order should be:
1. v4 file format + Scene Save writer + Scene Load reader (can be tested
   immediately: save a Scene, reload it, verify specials survive)
2. Bank Save/Load integration (should be automatic if Scene path works)
3. Root `/Pattern/` library (lower priority, can defer)
4. 17th Scene allocation + snapshot memcpy
5. AutoSave dirty tracking + drain integration + file pair writer
6. Boot restore for AutoSave patterns

If the session runs long, defer items 5-6 (AutoSave) to Session 064 and
focus on getting file format + Scene/Bank load/save solid.

### R9 — Existing v3 Text Pattern Reader/Writer

The existing v3 reader (phase 46/53) and writer (phase ~100+) are wired
into the Scene load/save state machines. The v4 integration must:
- Add a probe branch at phase 46 (read first 4 bytes)
- Add new phases for v4 binary reading
- Replace the v3 writer phases with v4 binary writing
- Keep the v3 reader alive as a fallback for old files

**Risk**: The filesystem state machine is large and phase numbers are
dense. Adding new phases requires careful numbering and may interact with
the existing Scene/Bank phase sequencing.

### R10 — Pattern file during Scene/Bank Load error paths

If the v4 pattern reader returns an error (e.g., PAT_STACK_SIZE mismatch),
the Scene should still load with an empty pattern rather than failing
entirely. The Kit, effects, and scene settings should remain valid.

**Current behavior with v3**: a malformed `pattern.pat` results in an empty
pattern (the discard PatternSet). The v4 path should behave identically —
`pat_initScene()` before reading, so a read failure leaves a clean slate.

---

## Implementation Order

| Step | What | Dependencies |
|------|------|-------------|
| 1 | Define v4 format constants, header struct, static asserts | None |
| 2 | Implement v4 binary writer phases in filesystem.c | Step 1 |
| 3 | Implement v4 binary reader phases with v3 fallback probe | Step 1 |
| 4 | Test: Scene Save → Scene Load round-trip with specials | Steps 2, 3 |
| 5 | Verify Bank Save/Load automatically uses v4 path | Step 4 |
| 6 | Verify boot restore handles v4 files | Step 4 |
| 7 | Decide and implement track settings home (R1) | Step 1 |
| 8 | Allocate 17th Scene (SCENE_COUNT=17, guards) | None |
| 9 | Implement snapshot memcpy + Scene Save from snapshot | Step 8 |
| 10 | Add Pattern dirty tracking (16-bit mask) | None |
| 11 | Add `.patNNx` pair ensure/creation at boot | Step 1 |
| 12 | Implement AutoSave pattern drain phase | Steps 8, 9, 10, 11 |
| 13 | Implement AutoSave pattern boot restore | Steps 11, 12 |
| 14 | Root `/Pattern/` library load/save | Steps 2, 3 |
| 15 | Hardware verification | All |

---

## Decisions From This Planning Pass

| Decision | Detail | Conflict? |
|----------|--------|-----------|
| v4 binary format replaces v3 text writer | v3 reader retained as fallback | No |
| 128-byte fixed header with padding | Forward-compatible via header_size field | No |
| Address array + bitmap + pool serialized as raw bytes | Native LE layout, no transformation needed | No |
| PAT_STACK_SIZE mismatch → error, not truncation | Larger file into smaller firmware is rejected | No |
| 17th Scene allocated for snapshot/staging | 11,696 B from Pattern SRAM1 reservation | No — this is explicitly within the Pattern reservation |
| AutoSave uses full-file format, not differential | ~3.5 KB per file, 32 files total | No |
| Drain alternates parameter/pattern phases | Non-active dirty Scene prioritized | No |
| BASEPRI mask during snapshot memcpy | ~50 µs at 216 MHz | No |
| `PAT_USER_SCENE_COUNT` (16) vs `SCENE_COUNT` (17) | UI/Bank/HCNAMES iterate 0..15 only | Extends existing SCENE_COUNT; prior code assumed 16 everywhere |

---

## RAM Budget Impact

| Allocation | Size | Region |
|------------|-----:|--------|
| 17th `scene_t` | 1,200 B | SRAM1 `.bss` |
| 17th `pat_scene_region_t` | 10,496 B | SRAM1 `.bss` |
| `pat_autosave_dirty_mask` | 2 B | SRAM1 `.bss` |
| Per-track settings in region (if R1 Option 1) | 896 B | SRAM1 `.bss` (inside `pat_regions`) |
| **Total** | **~12,594 B** | SRAM1 Pattern reservation |

SRAM1 remaining after: 117,532 − 12,594 = **~104,938 B** (102.5 KB).
