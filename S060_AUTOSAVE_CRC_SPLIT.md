# Session 058 — AutoSave v2: Per-Section CRC Record Format

Status: **design, pre-implementation.** Prerequisite for the AutoSave boot
reader (`AUTOSAVE_READ_PLAN.md`). Replaces the v1 single whole-file CRC32C
with per-section CRCs and an XOR aggregate, enabling surgical section-level
writes and per-section validation.

---

## 1. Motivation

The v1 record format uses one CRC32C over all 34,768 bytes. Any change to
any byte — even the 2-byte Bank slot — requires streaming the entire file
to recompute the CRC. At `AUTOSAVE_CRC_BYTES_PER_TICK = 128`, that is 272
ticks (~272 ms) minimum per write cycle, regardless of how few bytes
actually changed.

A Bank-session transition dirties ~6,735 payload positions. The capture
budget (`AUTOSAVE_PARAMETER_GETS_PER_WRITE = 1536`) limits each cycle to
1,536 captures, requiring 5 full-file streaming cycles. With ~2 seconds
per cycle overhead (validation + mask + CRC), the total drain takes ~17
seconds for what amounts to writing ~6.7 KB of actual data into a ~34 KB
file.

Per-section CRCs solve this by making each section independently writable
and verifiable. The writer can update one 2,160-byte Scene section (~17 ms)
instead of streaming 34,768 bytes (~272 ms). A full Bank transition across
all 17 sections takes ~280 ms instead of ~17 seconds.

The format also enables per-section validation for the reader: a corrupt
Scene 7 does not prevent reading Scene 3. This directly supports the
reader's per-Scene Case 1/2/3 architecture (`AUTOSAVE_READ_PLAN.md` §5).

---

## 2. v2 Record Format

### 2.1 Overview

The payload (128-byte Bank + 16 × 1,920-byte Scenes = 30,848 bytes) and
mutation mask (3,856 bytes total) are unchanged. What changes is their
arrangement: the monolithic mask is distributed into each section, and the
single trailing CRC is replaced by a per-section CRC table in the header.

Record size: **34,832 bytes** (v1: 34,768; growth: +64 bytes, exactly one
old header's worth).

### 2.2 Header (128 bytes)

| Offset | Size | Field |
|-------:|-----:|-------|
| 0 | 4 | Magic `HCPR` |
| 4 | 1 | Format version (`2`) |
| 5 | 1 | Commit byte (`0xa5` when valid) |
| 6 | 2 | Reserved (zero) |
| 8 | 4 | Generation (wrapping 32-bit, LE) |
| 12 | 4 | Aggregate CRC32C (XOR of 17 section CRCs) |
| 16 | 1 | Probe counter |
| 17 | 3 | Reserved (zero) |
| 20 | 68 | Section CRC table (17 × CRC32C, LE) |
| 88 | 40 | Reserved (zero, future header fields) |

CRC table indexing:

| Index | Offset | Section |
|------:|-------:|---------|
| 0 | 20 | Bank |
| 1 | 24 | Scene 0 |
| 2 | 28 | Scene 1 |
| ... | ... | ... |
| 16 | 84 | Scene 15 |

The aggregate CRC at offset 12 occupies the same position as v1's
whole-record CRC32C. The format version at offset 4 distinguishes v1 (1)
from v2 (2) before any CRC check is attempted.

### 2.3 Section Layout

Each section is self-contained: its mutation mask slice followed by its
data. The CRC32C for each section covers its mask + data bytes.

| Offset | Size | Content |
|-------:|-----:|---------|
| **128** | **144** | **Bank section** |
| 128 | 16 | Bank mask (128 bits → 128 data bytes) |
| 144 | 128 | Bank data (restore slot, name, masks, active Scene) |
| **272** | **2,160** | **Scene 0 section** |
| 272 | 240 | Scene 0 mask (1,920 bits → 1,920 data bytes) |
| 512 | 1,920 | Scene 0 data (name, params, Effect, Kit) |
| **2,432** | **2,160** | **Scene 1 section** |
| ... | ... | ... |
| **32,672** | **2,160** | **Scene 15 section** |
| **34,832** | — | End of record |

General formulas:

```
Bank section offset   = 128
Bank mask offset      = 128
Bank data offset      = 144

Scene section offset  = 272 + scene_index × 2160
Scene mask offset     = 272 + scene_index × 2160
Scene data offset     = 512 + scene_index × 2160
```

Within each section's data region, all existing relative offsets are
unchanged:

- Bank data: restore slot at +0, name at +2, scene_present_mask at +10,
  active_scene at +12, voice_edit_mask at +13. Same as v1 relative
  positions.
- Scene data: name at +0, parameters at +8, Effect at +128, Kit at +640.
  Same as v1 relative positions.

### 2.4 CRC Semantics

Each section CRC is Castagnoli CRC32C computed over that section's mask
bytes immediately followed by its data bytes, with no bytes zeroed during
computation (unlike v1 where CRC bytes 12..15 are treated as zero).

The aggregate is:

```
aggregate = CRC[0] ^ CRC[1] ^ CRC[2] ^ ... ^ CRC[16]
```

Surgical update of one section requires only:

```
new_aggregate = old_aggregate ^ old_CRC[i] ^ new_CRC[i]
```

No re-read of other sections is needed to update the aggregate.

### 2.5 Mask Coverage Verification

Total mask bytes: 16 (Bank) + 16 × 240 (Scenes) = 3,856 bytes.
Total mask bits: 3,856 × 8 = 30,848 = `AUTOSAVE_PAYLOAD_BYTES`. ✓

The per-section mask maintains the same one-bit-per-payload-byte contract
as v1. Mask bit N within a section describes data byte N within that same
section. The mask never describes header bytes or bytes in other sections.

---

## 3. Constants (Autosave.h)

### 3.1 Header Constants — Modified

```c
#define AUTOSAVE_HEADER_BYTES               128u   /* was 64 */
#define AUTOSAVE_HEADER_FORMAT_VERSION        2u   /* was 1 */
#define AUTOSAVE_HEADER_CRC_TABLE_OFFSET     20u   /* new */
#define AUTOSAVE_HEADER_CRC_TABLE_COUNT      17u   /* new (1 Bank + 16 Scene) */
#define AUTOSAVE_HEADER_CRC_TABLE_BYTES \
    (AUTOSAVE_HEADER_CRC_TABLE_COUNT * 4u)         /* 68 */
```

Offsets 0 (magic), 4 (version), 5 (commit), 8 (generation), 12
(aggregate/CRC), 16 (probe) keep their byte positions. The CRC at offset
12 becomes the aggregate; the per-section table starts at offset 20.

### 3.2 Section Geometry — New

```c
#define AUTOSAVE_SECTION_COUNT               17u

#define AUTOSAVE_BANK_MASK_BYTES             16u
#define AUTOSAVE_BANK_SECTION_TOTAL_BYTES \
    (AUTOSAVE_BANK_MASK_BYTES + AUTOSAVE_BANK_SECTION_BYTES)   /* 144 */

#define AUTOSAVE_SCENE_MASK_BYTES           240u
#define AUTOSAVE_SCENE_SECTION_TOTAL_BYTES \
    (AUTOSAVE_SCENE_MASK_BYTES + AUTOSAVE_SCENE_SECTION_BYTES) /* 2160 */
```

### 3.3 Absolute Offsets — Redefined

```c
#define AUTOSAVE_BANK_SECTION_OFFSET    AUTOSAVE_HEADER_BYTES  /* 128 */
#define AUTOSAVE_BANK_MASK_OFFSET       AUTOSAVE_BANK_SECTION_OFFSET
#define AUTOSAVE_BANK_DATA_OFFSET \
    (AUTOSAVE_BANK_SECTION_OFFSET + AUTOSAVE_BANK_MASK_BYTES)  /* 144 */

#define AUTOSAVE_SCENES_SECTION_OFFSET \
    (AUTOSAVE_BANK_SECTION_OFFSET + AUTOSAVE_BANK_SECTION_TOTAL_BYTES) /* 272 */
```

Scene N absolute offsets are computed:

```c
/* Section start (mask + data): */
#define AUTOSAVE_SCENE_SECTION_OFFSET(n) \
    (AUTOSAVE_SCENES_SECTION_OFFSET + (n) * AUTOSAVE_SCENE_SECTION_TOTAL_BYTES)

/* Mask start within file: */
#define AUTOSAVE_SCENE_MASK_OFFSET_ABS(n)  AUTOSAVE_SCENE_SECTION_OFFSET(n)

/* Data start within file: */
#define AUTOSAVE_SCENE_DATA_OFFSET_ABS(n) \
    (AUTOSAVE_SCENE_SECTION_OFFSET(n) + AUTOSAVE_SCENE_MASK_BYTES)
```

### 3.4 Record Size — Updated

```c
#define AUTOSAVE_RECORD_BYTES \
    (AUTOSAVE_HEADER_BYTES + AUTOSAVE_BANK_SECTION_TOTAL_BYTES + \
     (AUTOSAVE_SCENE_COUNT * AUTOSAVE_SCENE_SECTION_TOTAL_BYTES))
```

Static assert: `AUTOSAVE_RECORD_BYTES == 34832u`.

### 3.5 Removed Definitions

The monolithic mask offset and payload offset are removed — they no longer
correspond to contiguous file regions:

```c
/* REMOVED: */
/* #define AUTOSAVE_MASK_OFFSET  AUTOSAVE_HEADER_BYTES */
/* #define AUTOSAVE_PAYLOAD_OFFSET (AUTOSAVE_MASK_OFFSET + ...) */
/* #define AUTOSAVE_BANK_OFFSET  AUTOSAVE_PAYLOAD_OFFSET */
/* #define AUTOSAVE_SCENES_OFFSET (AUTOSAVE_BANK_OFFSET + ...) */
```

`AUTOSAVE_MASK_BYTES` (3,856) and `AUTOSAVE_PAYLOAD_BYTES` (30,848)
survive as totals for the static assert and for canonical-mask sizing.

---

## 4. Canonical Mask ↔ Section Mask Mapping

The canonical mask in RAM remains one contiguous 3,856-byte bitfield. Its
bit-to-payload-byte mapping is unchanged:

- Bits 0..127 → Bank data bytes 0..127
- Bits 128..2047 → Scene 0 data bytes 0..1919
- Bits (128 + i×1920)..(128 + (i+1)×1920 − 1) → Scene i data bytes

When writing a section to disk, the writer extracts the corresponding
slice of the canonical mask:

```c
/* Bank: canonical mask bytes 0..15 → Bank section mask */
/* Scene i: canonical mask bytes (16 + i*240)..(16 + (i+1)*240 - 1)
   → Scene i section mask */
```

This is pointer arithmetic on the existing mask array — no format change
to the canonical mask is needed. The producer API
(`autosave_markBankFieldDirty()`, `autosave_markSceneParameterDirty()`,
etc.) is unaffected.

The writer also needs a per-section dirty predicate — "does this section
have any dirty bits?" — to decide whether to transform-write or copy-
forward that section. This is a scan of the section's canonical mask slice
for any nonzero byte, bounded by the section's mask size (16 bytes for
Bank, 240 bytes for Scene). Fast enough to run inline.

---

## 5. Validation (Autosave.c)

### 5.1 Structural Validation

`autosave_streamValidationFinish()` currently checks:

1. `header_valid` (magic + commit)
2. `bytes_seen == AUTOSAVE_RECORD_BYTES`
3. Computed CRC == stored CRC

For v2:

1. `header_valid` — same magic/commit check, plus version == 2
2. `bytes_seen == AUTOSAVE_RECORD_BYTES` (now 34,832)
3. Aggregate CRC == XOR of CRC table entries

The aggregate check validates the CRC table's internal consistency without
streaming any section data. It catches header corruption and CRC-table
bit-flips but does not prove section data integrity.

### 5.2 Per-Section Validation

New function:

```c
uint8_t autosave_sectionCrcValid(
    const autosave_stream_validation_t *state,
    uint8_t section_index);
```

After streaming the full file (as today), the validator will have computed
per-section CRCs and can compare each against its CRC table entry. A
section is valid when its computed CRC matches its stored CRC.

The writer's winner-selection logic can treat a file as a candidate if its
header is structurally valid (aggregate passes), even if individual
sections fail. The winner selection prefers files with more valid sections.

### 5.3 Bank Identity Check

`autosave_streamValidationMatchesBank()` is unchanged in semantics — it
compares stored Bank slot/name against the current resident Bank. The
stored values are now parsed from the Bank section's data region (file
offset 144) instead of the old monolithic payload region (old offset 3920).
The parser's internal field storage stays the same; only the absolute offset
at which it extracts the slot/name changes.

---

## 6. Initial Record Generation (Autosave.c)

`autosave_initialRecordByte()` currently returns the initial content for
absolute record byte N. For v2:

- Header bytes 0..127: magic, version 2, commit, generation 0, zeroed CRC
  table, zeroed aggregate, probe, reserved.
- Bank section bytes 128..271: 16 zero mask bytes, then Bank data produced
  by the existing Bank-field initial-value logic (slot, name from
  current state; rest zero).
- Scene N section bytes: 240 zero mask bytes, then Scene data produced by
  the existing Scene initial-value logic (HCNAMES-sourced names; rest
  zero).

The function's interface can remain byte-indexed. Internal offset math
changes to reflect the v2 layout. The CRC table entries and aggregate for
an initial record are computed after the full content is determined — the
regeneration path (case 30 in the writer) already streams the content
through a CRC accumulator, which now accumulates per-section CRCs and
stores them in the header's CRC table region.

---

## 7. Writer State Machine (filesystem.c)

### 7.1 Phase 1 — Section-Aware A/B Streaming

The existing A/B alternation model is retained. The writer still creates a
target file by streaming from the winner. The structural change is that the
streaming copy processes one section at a time instead of one monolithic
byte stream.

**Validation (cases 1–5):** No fundamental change. The validator streams
the full file (now 34,832 bytes) but tracks per-section CRC accumulators
instead of one whole-file accumulator. After streaming, the header's CRC
table and aggregate are checked. Bank identity is extracted from the Bank
section's data region.

**Mask merge (case 50+):** Instead of reading a contiguous 3,856-byte
on-file mask, the writer reads each section's embedded mask slice in
sequence. The merge logic is unchanged — it ORs the on-file mask into the
canonical mask for each section. The total bytes read is still 3,856.

**Capture:** Unchanged. The capture phase walks the canonical mask and
calls getters, bounded by `AUTOSAVE_PARAMETER_GETS_PER_WRITE`. Per-section
CRCs do not affect the capture phase.

**Transformed copy (case 60+):** Restructured. Instead of one streaming
pass over the entire file, the writer iterates sections:

```
write header placeholder (128 bytes, zeroed CRC table)
for section_index = 0..16:
    if section has dirty canonical mask bits:
        stream section: write [transformed mask | transformed data]
        compute section CRC during streaming
    else:
        stream section: copy [winner mask | winner data] verbatim
        copy winner's CRC table entry (no recomputation)
    store section CRC in target CRC table
seek to header: write CRC table, compute + write aggregate
write generation, probe
write commit byte last
```

The per-tick byte budget (`AUTOSAVE_CRC_BYTES_PER_TICK = 128`) is applied
within each section's streaming pass. Section boundaries are natural
suspend/resume points in the state machine — the writer finishes one
section before starting the next.

For clean sections, the verbatim copy skips CRC recomputation entirely.
In the common case (user edits one Scene), only 1–2 sections are dirty
(the edited Scene + Bank if Bank fields changed). The remaining 15–16
sections are copied verbatim with their CRCs forwarded from the winner.
CRC computation cost drops proportionally.

### 7.2 Phase 2 — Surgical In-Place Section Updates (future scope)

Once the v2 format is in place, the writer can be further optimized to
modify the target in-place rather than streaming a full copy. This
eliminates the full-file I/O:

1. Open the target file (which is the previous cycle's completed record).
2. For each dirty section: seek to section offset, write transformed mask +
   data, compute section CRC.
3. Seek to header: update CRC table entries, recompute aggregate, increment
   generation, write commit byte.

One Scene update: seek + 2,160 bytes + header update ≈ **~17 ms**.
Full Bank transition (17 sections): ≈ **~280 ms**.

This requires changing the A/B model: instead of deleting and recreating
the target each cycle, the writer maintains both files as persistent,
independently-valid records. The non-active file serves as the power-loss
backup. On validation, the reader assembles the best state from both files
by selecting the newer valid version of each section.

Phase 2 is a follow-up to Phase 1. The v2 format is designed to support it
but does not require it — Phase 1 delivers the format and the reader
benefits immediately.

---

## 8. Trace Updates

The VALIDATED trace record (stage V) already carries flags and generation.
No new trace stages are needed for Phase 1. The flags field gains:

- Bit 3: v2 format detected (set when the winning candidate has format
  version 2).

If per-section integrity checking is implemented in validation, a new
SECTION_VALID trace stage could report per-section CRC pass/fail, but this
is optional diagnostic work and not required for the format change.

---

## 9. Migration / Cutover

This is a single-hardware development project. There are no deployed
devices with v1 records in the field. Cutover strategy:

1. Flash the v2 firmware.
2. On first boot, the writer validates existing `.hcprms1`/`.hcprms2`.
   Both will fail: v1 records have format version 1, wrong record size
   (34,768 vs 34,832), and no valid CRC table.
3. The writer's no-winner path (case 30) regenerates both files in v2
   format. This is the same regeneration path that runs today when both
   files are invalid — no new migration code.
4. `autosave_markResidentBankDirty()` seeds the canonical mask at boot.
   The first drain cycle captures live state into the fresh v2 records.

No v1-to-v2 migration code. No backward-compatibility reader. The version
byte at offset 4 allows the validator to reject v1 records immediately
rather than failing at the CRC check.

The one-time regeneration adds ~6 seconds to the first boot after the
firmware update. Subsequent boots find valid v2 records and skip
regeneration.

---

## 10. Implementation Order

### Step 1: Format constants and static asserts

- Update `Autosave.h`: new header size, section geometry, absolute offsets,
  CRC table layout, record size. Remove monolithic mask/payload offset
  definitions.
- Update all static asserts.
- Fix every compile error from the changed constants — this reveals every
  file that depends on the v1 layout.

### Step 2: Per-section CRC helpers

- Add section CRC computation helpers to `Autosave.c`:
  `autosave_sectionCrcInit()`, `autosave_sectionCrcUpdate()`,
  `autosave_sectionCrcFinish()`. These wrap the existing CRC32C
  implementation with section-boundary awareness.
- Add `autosave_sectionOffset()` and `autosave_sectionSize()` helpers for
  the writer to iterate sections.

### Step 3: Validation update

- Update `autosave_streamValidationUpdate()` to track per-section CRC
  accumulators and parse the CRC table from the header.
- Update `autosave_streamValidationFinish()` to check aggregate = XOR of
  CRC table.
- Update `autosave_streamValidationMatchesBank()` for the new Bank data
  offset.
- Add `autosave_sectionCrcValid()` for per-section integrity queries.

### Step 4: Initial record generation

- Update `autosave_initialRecordByte()` for the v2 layout: header with CRC
  table, per-section mask + data arrangement.
- The regeneration path (case 30 in filesystem.c) computes per-section CRCs
  during streaming and fills the CRC table. This may require the writer to
  buffer or two-pass the header (write placeholder → stream sections →
  seek back to write CRC table). Alternatively, the writer can compute
  section CRCs in a small local array (17 × 4 = 68 bytes) and write the
  header last.

### Step 5: Writer state machine — section-aware streaming copy

- Restructure the transformed copy phase to iterate sections.
- For each section: determine dirty/clean, stream with per-section CRC or
  copy verbatim with forwarded CRC.
- Write completed CRC table and aggregate to target header.
- Commit byte last.

### Step 6: Mask merge update

- Update the winner mask read path to read per-section embedded masks
  instead of one contiguous mask block.
- Merge logic is unchanged; only the file offsets change.

### Step 7: Build and card test

- `make clean && make` — verify clean build.
- Flash, boot, verify regeneration produces valid v2 records.
- Verify normal drain cycles produce correct per-section CRCs.
- Verify Bank transition behavior (new Bank identity → dirty seed → drain).
- Verify trace records are sensible.

---

## 11. Test Plan

### 11.1 Format correctness

- After first boot (regeneration): read `.hcprms1` and `.hcprms2`, verify
  both are 34,832 bytes. Parse header: magic = `HCPR`, version = 2,
  commit = `0xa5`. Verify aggregate = XOR of CRC table entries. Verify
  each section CRC matches its computed CRC over section bytes.

### 11.2 Drain correctness

- Let the writer run through several drain cycles. Verify generation
  advances, A/B alternation works, per-section CRCs are correct after each
  cycle.
- Modify one Scene parameter. Verify only that Scene section's CRC changes
  in the next drain; other section CRCs are unchanged (copied from winner).

### 11.3 Bank transition

- Boot with Bank 14. Let drain complete. Load Bank 15. Exit Load/Save.
  Wait 20 seconds. Power off. Verify both records have Bank 15 identity
  in the Bank section. Verify Scene sections have captured data.

### 11.4 Power-loss resilience

- Interrupt power during a drain cycle. Verify the winner (previous valid
  record) survives. Verify the target's per-section CRCs correctly
  identify any corrupt sections.

### 11.5 v1 rejection

- Place a v1 record (34,768 bytes, format version 1) on the card. Boot
  with v2 firmware. Verify the validator rejects it (version mismatch)
  and the writer regenerates both files in v2 format.

---

## 12. Open Questions

**A. Phase 2 timing.** Should surgical in-place section updates land in
this session (S058) or defer to a follow-up alongside the reader? Phase 1
delivers the format; Phase 2 delivers the speed. The format is designed
for both, but Phase 2 changes the A/B alternation model and the validation
recovery logic (assemble best sections from two files).

**B. Header write strategy.** The CRC table lives in the header, which is
at file offset 0. The sections are at offsets 128+. The writer either:
(a) writes a placeholder header, streams sections, seeks back to write
the completed CRC table; or (b) buffers the 17 CRC values in a local
68-byte array and writes the header as the last step before commit. Option
(b) avoids a seek but adds 68 bytes of stack/local state. On an embedded
target with limited stack, confirm this is acceptable.

**C. Per-tick budget for clean-section copy.** Clean sections are copied
verbatim without CRC recomputation. Should the copy still respect the
128-byte-per-tick budget (consistent but slower), or should clean-section
copying run unbounded (faster but more tick-time variance)?
