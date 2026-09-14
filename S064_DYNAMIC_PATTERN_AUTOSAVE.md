# Session 064 — Pattern AutoSave

**Scope**: Wire `pat_scene_region_t` into the existing AutoSave persistence
system so pattern data survives power loss between explicit saves.

**Prerequisite**: Session 063 — v4 binary Pattern file format, Scene/Bank/root
Pattern load/save, HCNAMES 145-row expansion, per-track params, v3 removal.
All verified on hardware.

**Authoritative source**: `S063_DYNAMIC_PATTERN_FILE_AUTOSAVE.md` (R2, R4,
R5, Session 064 scope), and the existing AutoSave infrastructure in
`Autosave.h`/`Autosave.c`/`filesystem.c`.

---

## Background

The existing AutoSave system persists scalar parameters (Bank, Scene settings,
Kit settings, Instrument endpoints) into a pair of ping-pong files
(`.hcprms1`/`.hcprms2`) in root. Each file is a 34,768-byte record:

```
64 B header (magic, version, commit, generation, CRC32C, probe)
3,856 B mutation mask (1 bit per payload byte)
30,848 B payload:
  128 B Bank section
  16 × 1,920 B Scene sections:
    128 B Scene header (name, source, 40 live parameters, reserved)
    512 B Effect section (stub — 0 live params)
    1,280 B Kit section:
      128 B Kit header (name, source, 2 live params, reserved)
      6 × 192 B Instrument records (type, name, source, endpoints)
```

The drain cycle is incremental: the writer classifies dirty bits from the
canonical SRAM mask, captures changed payload bytes, transforms a copy of the
existing record with the patches, updates CRC, and publishes the commit byte.
This runs in the background filesystem scheduler alongside trace flush.

Pattern data (`pat_scene_region_t`) is **not** in this record. At 10,519
bytes per Scene × 16 Scenes = 168,304 bytes, it is far too large to embed in
the existing 34,768-byte ping-pong record. The record would grow to ~224 KB
per file (448 KB for both), and the per-byte dirty mask would grow to ~25 KB
SRAM — neither is acceptable.

Pattern therefore needs its own separate AutoSave file scheme.

---

## Design

### Separate per-Scene Pattern AutoSave files

Each resident Scene gets its own pair of ping-pong Pattern AutoSave files in
root, analogous to `.hcprms1`/`.hcprms2`:

```
.pat00a / .pat00b    — Scene 0
.pat01a / .pat01b    — Scene 1
...
.pat15a / .pat15b    — Scene 15
```

32 files total, all 8.3-compatible (7 chars + extension-less, fits 8.3).

Each file is a complete v4 PAT4 image (10,656 bytes at current
`PAT_STACK_SIZE=256`): the same format already used by Scene directories and
root `/Pattern/` library files. The generation counter in the v4 header
(offset 10, uint32_t LE) distinguishes the A/B pair; the CRC32C at offset 14
validates integrity. Boot picks the valid file with the highest generation.

This reuses the existing v4 format exactly. No new wire format, no new CRC
scheme, no new header fields. The generation counter and CRC are already
defined in the v4 spec and were zeroed by library saves; AutoSave increments
from 1.

### Dirty tracking

Add a 16-bit `autosave_pattern_dirty_mask` in `Autosave.c`. One bit per
Scene. Set by `bank_invalidateSdCleanScene()` (which every pattern mutation
already calls) or by a new `autosave_markPatternDirty(scene_index)`. Cleared
per Scene after a successful drain writes the durable file.

This is intentionally coarser than the per-byte mask used by the parameter
record. Pattern data changes in bulk (step toggle, pool alloc, copy, paste,
load) and the drain writes the entire 10,656-byte file at once. Per-byte
tracking of 10,519 bytes per Scene would cost 1,315 bytes of mask SRAM per
Scene (21 KB total) for marginal benefit — the SD write is already
sector-aligned and the drain writes ~21 sectors regardless.

### Snapshot

The drain writer must capture a coherent snapshot of `pat_scene_region_t`
because `seq_tick()` (priority 2, 4 kHz) both reads and writes the address
array and pool during playback:

- `pat_isStepActive()`, `pat_readStepSpecials()` — **read** from
  `pat_regions[]` on every active step (sequencer.c:389, :395)
- `pat_eraseStep()` — **write** to `pat_regions[]` during live erase
  (sequencer.c:391, called from TIM3 ISR when `seq_eraseActive`)
- `pat_setStepActive()` — **write** to `pat_regions[]` during real-time
  recording (sequencer.c:900 via `seq_recordTrigger()`, TIM3 ISR when
  `seq_recordActive`; also MidiParser.c:1470 from MIDI ISR)

Without protection, a main-loop `memcpy` during active recording or erasing
can produce a torn snapshot (ISR modifies address/pool mid-copy).

**Decision**: Pattern AutoSave drain does not run while `seq_recordActive`
or `seq_eraseActive` is true. The drain scheduler checks both flags before
initiating a snapshot; if either is set, the dirty Scene is skipped this
cycle and retried next cycle. This eliminates the need for any ISR masking
(BASEPRI, NVIC_ICER) during the snapshot `memcpy`, because:

- When recording/erasing is inactive, `seq_tick()` only reads
  `pat_regions[]` — reads cannot tear a read-side `memcpy`.
- `seq_recordActive` and `seq_eraseActive` are set by main-loop button
  handlers, so they cannot transition to true during a main-loop `memcpy`.
- MIDI-triggered recording goes through `seq_recordTrigger()` which
  requires `seq_recordActive` to already be set.

The snapshot `memcpy` (10,519 B SRAM1→SRAM1) takes ~25–40 µs at M7
speeds with optimized LDMIA/STMIA bursts; 50 µs is the conservative
bound accounting for DMA bus contention. This is negligible CPU cost
(0.001% at 5s cadence, 0.0025% at 2s).

### 17th Scene snapshot region

The 17th region remains useful even without ISR masking: copy the live
region into the snapshot in the main loop, then drain from the snapshot
across multiple scheduler ticks without holding up main-loop pattern
edits. The snapshot region also serves future background Bank Load staging.

### Drain scheduling

Pattern drain runs in the existing background AutoSave scheduler alongside
the parameter drain and trace flush. When `autosave_pattern_dirty_mask` is
nonzero, the scheduler picks the lowest dirty Scene. Before snapshotting,
it checks `seq_recordActive` and `seq_eraseActive`: if either is true, the
Scene is skipped this cycle (dirty bit stays set, retried next cycle).
Otherwise, it snapshots the Scene and writes one `.patNNx` file. One Scene
per drain cycle keeps foreground latency low.

The generation counter increments each write and alternates between the A and
B files. The commit model is identical to the parameter record: write the
complete file with CRC, then the commit byte is implicit in the valid
CRC + generation — no separate commit byte is needed because the entire file
is one atomic image (as opposed to the parameter record's incremental
copy-forward).

### Boot restore

The boot reader for each Scene:
1. Attempt to read `.patNNa` and `.patNNb`.
2. Validate magic, version, PAT_STACK_SIZE, CRC for each.
3. Pick the valid file with the higher generation (A wins ties).
4. Stream into `pat_sceneRegionMut(scene)`.
5. If neither file is valid, leave the Scene at `pat_initScene()` defaults.

This runs as part of the existing blocking boot sequence, after the parameter
record reader has restored Bank/Scene/Kit state and the Scene directory
Pattern files have been read. The AutoSave Pattern file wins over the Scene
directory Pattern file only when its generation is nonzero and its source
matches the HCNAMES provenance — otherwise the Scene directory version is
authoritative.

### HCNAMES row count alignment

The parameter AutoSave record's HCNAMES section is currently 129 rows
(`AUTOSAVE_HCNAMES_ROW_COUNT = 129`). The filesystem's HCNAMES is now 145
rows. Session 064 must expand the AutoSave HCNAMES to 145 rows.

This changes the parameter record wire format: `resident_names` arrays grow
from `[129][9]` to `[145][9]` — a +144-byte increase per occurrence. The
record payload, mask, and total size all change. This is a breaking change
for existing AutoSave files, but the boot reader already handles
missing/invalid records by synthesizing a fresh initial image.

---

## Implementation Plan

### 1. HCNAMES expansion in AutoSave record

**What**: Update `AUTOSAVE_HCNAMES_ROW_COUNT` from 129 to 145. Adjust all
dependent constants (`AUTOSAVE_MASK_BYTES`, `AUTOSAVE_PAYLOAD_BYTES`,
`AUTOSAVE_RECORD_BYTES`).

**Risk**: The record format change invalidates existing `.hcprms1`/`.hcprms2`
files. The boot reader must detect the old format (via size or version) and
treat it as invalid, triggering fresh initial creation. Alternatively, bump
`AUTOSAVE_HEADER_FORMAT_VERSION` from 1 to 2.

**Files**: `Autosave.h`, `Autosave.c` (format chunk, CRC update, validation,
static asserts), `filesystem.c` (boot reader, initial creation, drain).

### 2. Pattern dirty mask

**What**: Add `static volatile uint16_t autosave_pattern_dirty_mask` in
`Autosave.c`. Add `autosave_markPatternDirty(scene_index)` (sets bit),
`autosave_patternDirtyMask()` (returns mask), and
`autosave_clearPatternDirty(scene_index)` (clears bit after drain).

**Wire into existing callers**: `bank_invalidateSdCleanScene()` already
covers load-path and copy-path mutations. PatternData setters already call it
via the `bank_invalidateSdCleanScene(s)` at the end of each setter. Verify
that all mutation paths reach the dirty marker.

**Files**: `Autosave.h`, `Autosave.c`, possibly `BankData.c`.

### 3. Snapshot region (`scene_temp`)

**What**: Allocate `static pat_scene_region_t scene_temp` in `PatternData.c`
as the AutoSave staging buffer. Standalone — not a 17th entry in
`pat_regions[]`, `SCENE_COUNT` stays 16, no off-by-one risk. Expose via
`pat_snapshotScene(scene_index)` (performs the copy) and a const pointer
accessor for the drain writer to read from.

**SRAM cost**: 10,519 bytes in SRAM1 `.bss`.

**Files**: `PatternData.c` (allocation + accessors), `PatternData.h`
(declarations).

### 4. Snapshot copy (no ISR masking needed)

**What**: Implement `pat_snapshotScene(scene_index)` that:
1. `memcpy(&pat_autosave_snapshot, &pat_regions[scene], sizeof(...))`

No BASEPRI mask or NVIC disable. The drain scheduler (step 6) guards the
call behind `!seq_recordActive && !seq_eraseActive`, so no ISR writes to
`pat_regions[]` during the copy. `seq_tick()` only reads in this state.
Both guard flags are set by main-loop button handlers and cannot transition
during a main-loop `memcpy`.

Duration: ~25–40 µs for 10,519 bytes (SRAM1→SRAM1, M7 LDMIA/STMIA);
50 µs conservative bound with DMA bus contention.

**Files**: `PatternData.c`, `PatternData.h`.

### 5. Pattern AutoSave file writer

**What**: Add a state machine in `filesystem.c` that:
1. Receives a scene index from the scheduler
2. Calls `pat_snapshotScene(scene_index)`
3. Opens `.patNNx` (alternating A/B based on generation parity)
4. Writes the v4 PAT4 image from the snapshot region, using the existing
   `filesystem_patternBuildHeader()` and stream-write helpers
5. Writes CRC (incremental, seek-back to offset 14)
6. Closes the file
7. Clears the dirty bit

The v4 writer infrastructure from Session 063 (`filesystem_patternBuildHeader`,
`filesystem_patternCrcFeed`, stream-write chunking) is reused directly. The
only new work is the file naming (`".pat%02u%c"`) and generation management.

**Files**: `filesystem.c` (new tick function, scheduler integration).

### 6. Pattern AutoSave scheduler integration

**What**: Extend the existing AutoSave scheduler in `filesystem.c` to check
`autosave_patternDirtyMask()` after the parameter drain is idle. When a dirty
Scene is found, enter the Pattern drain state machine.

Scheduling priority: parameter drain > trace flush > pattern drain. Pattern
drain writes one Scene per cycle and returns to the scheduler. If multiple
Scenes are dirty, they are drained in ascending order across multiple cycles.

**Files**: `filesystem.c` (scheduler tick, state transitions).

### 7. Pattern AutoSave boot reader

**What**: Add a blocking boot reader that, for each present Scene:
1. Attempts to open `.patNNa` and `.patNNb`
2. Validates each using the existing `filesystem_patternHeaderValid()` + CRC
3. Picks the winner (highest generation with valid CRC, A wins ties)
4. Streams into `pat_sceneRegionMut(scene)`
5. Falls back to the Scene directory Pattern (already restored) or
   `pat_initScene()` defaults

**Ordering**: this runs after the Scene directory Pattern restore. If the
AutoSave file has a higher generation than 0 (library/scene saves write
generation 0) and the HCNAMES source matches, AutoSave wins. Otherwise the
Scene directory version is authoritative.

**Files**: `filesystem.c` (boot sequence), `Autosave.h` (constants).

### 8. Wire `autosave_markSceneWithPatternDirty`

**What**: The existing `autosave_markSceneWithPatternDirty()` currently just
calls `autosave_markSceneWithoutPatternDirty()` with a TODO comment. Extend
it to also call `autosave_markPatternDirty(scene_index)`.

Similarly, `autosave_markResidentBankDirty()` should mark present Scenes'
patterns dirty.

**Files**: `Autosave.c`.

### 9. HCNAMES Pattern row source/refreshed lifecycle

**What**: After a successful Pattern AutoSave drain for a Scene, set the
HCNAMES Pattern row source to `@` (AutoSave provenance) and set the refreshed
flag. After a user mutation that dirties the pattern, clear the refreshed flag.

This follows the same lifecycle as Kit/Instrument AutoSave: the `R` flag in
HCNAMES means "the on-card copy matches SRAM."

**Files**: `filesystem.c` (post-drain publication), `Autosave.c` (dirty
marker clears refreshed).

---

## Risks

### R1 — Record format break

Expanding `AUTOSAVE_HCNAMES_ROW_COUNT` from 129 to 145 changes the parameter
record size. All existing `.hcprms1`/`.hcprms2` files become invalid. The boot
reader must detect the old size and discard gracefully.

**Mitigation**: Bump `AUTOSAVE_HEADER_FORMAT_VERSION` to 2. The existing
validation rejects mismatched versions. First boot after the update
synthesizes fresh initial records. This is a one-time data loss of unsaved
parameter edits, which is acceptable for a development firmware.

### R2 — 32 new root directory files

The 32 `.patNNx` files double the root directory population from the current
AutoSave perspective (2 files → 34). FAT32 root directories have no entry
limit, but the asyncfatfs directory scan at boot visits every entry. Boot
time may increase slightly.

**Mitigation**: Measure boot time after implementation. If it's a problem,
the files could go in a subdirectory (`.pat/NNa`), but that adds complexity
and probably isn't needed for 34 small files.

### R3 — SRAM cost

The 17th Scene snapshot region costs 10,519 bytes. Current SRAM1 free
estimate after S063 is ~117,101 bytes (from the Phase A/B doc). This leaves
~106,582 bytes — still comfortable.

### R4 — Drain throughput

At 10,656 bytes per file (~21 sectors), one Pattern drain writes ~10 KB to
the SD card. At typical sustained SD write speeds (1–5 MB/s), this takes
2–10 ms of actual write time, spread across multiple ticks. With 16 dirty
Scenes, a full drain pass writes ~170 KB — still fast.

However, the drain writes one file per cycle and returns to the scheduler.
If all 16 Scenes are dirty simultaneously (e.g., after a Bank Load), the
full drain requires 16 scheduler cycles. At the current scheduler cadence,
this could take 30+ seconds. This is acceptable for a background operation
but should be measured.

### R5 — Boot ordering and AutoSave vs Scene directory conflict

The boot reader must decide whether the AutoSave Pattern file or the Scene
directory Pattern file is authoritative. The rule is:
- AutoSave generation > 0 AND HCNAMES source matches → AutoSave wins
- Otherwise → Scene directory wins (or `pat_initScene()` default)

This requires the HCNAMES parameter record to be fully restored before the
Pattern AutoSave reader runs, since the decision depends on the HCNAMES
Pattern row's source value. The existing boot sequence already restores
HCNAMES first, but this ordering dependency must be verified and documented.

### R6 — Recording/erasing drain deferral

~~(Originally: TIM3 masking safety.)~~ Resolved by architectural decision:
drain does not run while `seq_recordActive` or `seq_eraseActive` is true.
No BASEPRI mask, no ISR contention. The only consequence is that pattern
changes made during a sustained recording/erasing session accumulate in the
dirty mask and drain once the mode is exited. This is acceptable — the user
is actively interacting, so immediate persistence is not expected.

If a recording session lasts long enough that all 16 Scenes become dirty
before drain runs, the first drain pass after exit writes all 16 in
sequence (30+ seconds at one Scene per cycle). Not a problem.

---

## Resolved Questions

### Q1 — File naming — **No conflicts**

`.pat00a` through `.pat15b` (7 chars, 8.3-compatible). No other firmware
component creates `.pat*` files in root.

### Q2 — Generation counter initialization — **Same model as hcprms**

Library saves write generation 0. AutoSave starts from generation 1. After
a Pattern Load from library, the dirty mask is set, the drain writes
generation 1 with fresh CRC, and any stale prior-session file has a
non-matching CRC for the new data. Same lifecycle as the parameter record.
Verify during implementation.

### Q3 — Partial drain recovery — **Explicit test after autosave verified**

Standard A/B pair recovery: truncated/corrupt file has bad CRC, boot picks
the valid peer. The v4 CRC covers the entire file; size mismatch or CRC
mismatch both reject. Add a deliberate power-pull test after Pattern
AutoSave is working and hardware-verified on the happy path.

### Q4 — Whole-file drain — **Decided: whole-file only**

Incremental sector-level drain is not a real optimization: the CRC covers
the entire file, so any sector change requires re-streaming and
recomputing the full CRC regardless. An incremental approach would still
read-back and re-CRC the whole file, potentially making it slower than a
straight whole-file write while adding per-Scene dirty-sector tracking
SRAM. Not considered further.

### Q5 — Snapshot buffer — **Decided: standalone `scene_temp` in PatternData.c**

`static pat_scene_region_t scene_temp` in `PatternData.c`, not a 17th
entry in `pat_regions[]`. Avoids touching `SCENE_COUNT` and all
index-bounded loops. PatternData.c owns the allocation, the
`pat_snapshotScene()` accessor, and the pointer accessor the drain writer
uses to read from it.

### Q6 — Pattern AutoSave enable timing — **Same point as parameter tracking**

Enable at `autosave_setMutationTrackingEnabled()`, same as parameter
tracking. No pattern mutations should be possible before this point because
both record-enable and step-edit require the menu to be unlocked, which
happens after boot restore completes. Verify during implementation that no
boot-path code mutates pattern data after restore and before tracking
enable.

### Q7 — Delete on Scene clear/empty — **Decided: leave stale files**

Do not delete `.patNNa`/`.patNNb` when a Scene is cleared. Boot ignores
them when HCNAMES source says empty. Consistent with parameter record
behavior. Files are also theoretically user-recoverable from the SD card,
though the product should not depend on that.

---

## RAM Budget

| Allocation | Size | Region |
|------------|-----:|--------|
| Snapshot region (`pat_scene_region_t`) | +10,519 | SRAM1 `.bss` |
| Pattern dirty mask (uint16_t) | +2 | SRAM1 `.bss` |
| HCNAMES row expansion in AutoSave (129→145, two arrays) | +288 | SRAM1 `.bss` |
| **Net Session 064** | **~10,809** | SRAM1 |

Post-S064 SRAM1 free estimate: ~117,101 − 10,809 ≈ **106,292 B** (103.8 KB).

---

## File Change Summary

| File | Changes |
|------|---------|
| `Autosave.h` | `AUTOSAVE_HCNAMES_ROW_COUNT` → 145, add Pattern HCNAMES base, format version bump, dirty mask API |
| `Autosave.c` | Expand `resident_names` arrays, update static asserts, implement dirty mask, wire `markSceneWithPatternDirty`, update `markResidentBankDirty` |
| `PatternData.h` | Declare snapshot accessor |
| `PatternData.c` | Allocate `scene_temp` snapshot region, implement `pat_snapshotScene()` (plain memcpy, no masking) |
| `filesystem.c` | Pattern drain state machine, scheduler integration, boot reader, AutoSave file naming, generation management |
| `filesystem.h` | Possibly expose Pattern AutoSave status for Menu display |

---

## Hardware Acceptance Tests

### A. Single Scene drain

1. Boot, load Kit, create a pattern (step toggles, specials).
2. Wait for AutoSave drain (observe SD activity LED or trace).
3. Power cycle without explicit Save.
4. Boot: pattern should be restored.

### B. Multi-Scene drain

1. Load the same pattern into Scenes 0, 3, and 7.
2. Edit each Scene's pattern differently.
3. Power cycle.
4. Each Scene should restore its own pattern independently.

### C. Library load overrides AutoSave

1. Have an AutoSaved pattern in Scene 0.
2. Load a different pattern from `/Pattern/` library into Scene 0.
3. Power cycle.
4. Scene 0 should have the library pattern, not the old AutoSave.

### D. Power-loss recovery

1. Begin a pattern edit (dirty the pattern).
2. Power cycle before the drain completes.
3. Boot should restore the previous valid AutoSave (generation N-1).
4. The interrupted edit is lost (expected — AutoSave is best-effort).

### E. Boot time

Measure boot time with 32 `.patNNx` files present vs absent. Confirm no
meaningful regression.
