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
because `seq_tick()` (priority 2, 4 kHz) reads the address array and pool
during playback. Without a snapshot, a mid-write pattern edit or sequencer
read could produce a torn image.

Options (from R4):
- **BASEPRI mask**: set BASEPRI to `2u << 4` during `memcpy`, blocking TIM3
  for ~50 µs. Ticks are delayed, not lost. Inaudible.
- **NVIC_ICER**: disable TIM3 via NVIC for the copy. Same timing effect.
- **17th Scene region**: allocate one extra `pat_scene_region_t` (10,519 B)
  as a staging buffer. Copy from live region under the TIM3 mask, then drain
  from the snapshot at leisure without blocking the sequencer.

The 17th region is the cleanest option: copy is fast (~50 µs), the drain
writes from stable SRAM, and TIM3 is blocked only for the copy, not the
entire SD write sequence. The snapshot region also serves future background
Bank Load staging.

### Drain scheduling

Pattern drain runs in the existing background AutoSave scheduler alongside
the parameter drain and trace flush. When `autosave_pattern_dirty_mask` is
nonzero, the scheduler picks the lowest dirty Scene, snapshots it, and writes
one `.patNNx` file. One Scene per drain cycle keeps foreground latency low.

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

### 3. 17th Scene snapshot region

**What**: Allocate one extra `pat_scene_region_t` as the AutoSave staging
buffer. This can be:
- A 17th entry in `pat_regions[]` (bump `SCENE_COUNT` or use a separate
  static)
- A standalone `static pat_scene_region_t pat_autosave_snapshot` in
  PatternData.c

The standalone approach is simpler — no `SCENE_COUNT` change, no risk of
16-vs-17 off-by-one in other code paths.

**SRAM cost**: 10,519 bytes in SRAM1 `.bss`.

**Files**: `PatternData.c` (allocation + accessor), `PatternData.h`
(declaration).

### 4. Snapshot copy with TIM3 mask

**What**: Implement `pat_snapshotScene(scene_index)` that:
1. Raises BASEPRI to `2u << 4` (or disables TIM3 via NVIC)
2. `memcpy(&pat_autosave_snapshot, &pat_regions[scene], sizeof(...))`
3. Restores BASEPRI / re-enables TIM3

Duration: ~50 µs for 10,519 bytes at M7 speeds. Sequencer ticks delayed
by at most one 250 µs period, with the pending interrupt firing immediately
on restore.

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

### R6 — TIM3 masking safety

The snapshot `memcpy` under BASEPRI mask delays TIM3 by ~50 µs. This is
safe per R4 analysis (sequencer ticks are delayed, not lost). However, if
future code increases `pat_scene_region_t` size (e.g., larger pool), the
delay grows proportionally. At the current 10,519 bytes, the delay is
well within one sequencer tick period (250 µs).

---

## Open Questions

### Q1 — File naming

The spec says `.patNNx` where NN is the two-digit scene index and x is `a`
or `b`. Are there naming conflicts with any existing files? The format
`.pat00a` through `.pat15b` uses 7 characters, fitting 8.3 without LFN.
Confirm no other firmware component creates files matching `.pat*` in root.

### Q2 — Generation counter initialization

Library saves write generation 0. AutoSave starts from generation 1. After a
Pattern Load from library, the AutoSave generation for that Scene resets to
0 (no AutoSave provenance). Does the first AutoSave drain after a library
load write generation 1, and is this correctly distinguished from an existing
generation-1 file from a previous session?

The answer should be yes: the dirty mask is set on load, the drain writes a
new file with generation 1, and the old file (if any) has a stale CRC because
the pattern data changed. But this sequence should be verified.

### Q3 — Partial drain recovery

If the firmware loses power mid-write of a `.patNNx` file, that file has an
invalid CRC. The other file in the pair (the previous generation) remains
valid. Boot reads both, rejects the corrupt one, and uses the valid one. This
is the standard A/B pair recovery. But confirm: does asyncfatfs guarantee
that a partially written file has at least a bad CRC (not a valid-looking
truncation that passes CRC by coincidence)?

The v4 CRC covers the entire file including the header. A truncated file
produces a size mismatch (fewer bytes than expected) or a CRC mismatch. Both
are rejected by the existing `filesystem_patternHeaderValid()` + CRC
validation. This should be safe but deserves a deliberate test.

### Q4 — Should drain be whole-file or incremental?

The current design writes the entire 10,656-byte Pattern file on every drain.
An incremental approach (tracking dirty sectors within the pattern, writing
only changed sectors) would reduce SD wear and write time. However:
- The per-byte dirty mask would cost 1,315 bytes SRAM per Scene
- Address array edits (step toggle) change one 2-byte entry but the sector
  containing it must be rewritten anyway
- Pool allocations change scattered bytes across the 8 KB pool

Whole-file is simpler, uses less SRAM, and the write is already small (~21
sectors). Incremental drain is a future optimization if needed.

### Q5 — 17th Scene vs standalone snapshot

The spec mentions either bumping `SCENE_COUNT` or using a separate
`scene_temp`. A standalone static is recommended (avoids touching
`SCENE_COUNT` which gates loops throughout the codebase). But should the
snapshot buffer be in PatternData.c (owned by the pattern module) or in
filesystem.c (owned by the drain writer)?

The drain writer is the sole consumer, so filesystem.c ownership is
defensible. But PatternData.c owns the TIM3-masked copy operation. Either
location works; the decision affects only which module exposes the accessor.

### Q6 — When does Pattern AutoSave enable?

The existing parameter AutoSave enables at a specific point in the boot
sequence (`autosave_setMutationTrackingEnabled`). Pattern dirty tracking
should enable at the same point. Verify that no pattern mutations occur
between boot restore and tracking enable that would be lost.

### Q7 — Delete on Scene clear/empty

When a Scene is cleared or emptied (no pattern), should the AutoSave files
`.patNNa`/`.patNNb` be deleted? Or left as stale files that boot will ignore
(because HCNAMES source says empty)?

Leaving them is simpler and consistent with the parameter record (which
retains Scene data even for empty Scenes). Deleting saves 21 KB of SD space
per empty Scene but adds complexity. Recommend: leave them.

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
| `PatternData.c` | Allocate snapshot region, implement TIM3-masked copy |
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
