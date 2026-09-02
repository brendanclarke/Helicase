# Session 060 — AutoSave Writer Speedup & Reader Preparation

## Purpose

Refactor the autosave writer for speed, and extend `.hcnames` and the autosave
record format so that a future autosave reader can safely reconstruct the
resident Bank at boot. This document is the working plan; it identifies each
change, its rationale, SRAM cost, open questions, and phasing.

---

## 1. Current Performance Baseline

One autosave drain transaction in `filesystem_autosaveParameterDrain_tick()`
(`filesystem.c:6329`) goes through roughly this sequence:

| Phase group | What it does | Approx tick cost |
|---|---|---|
| 0–5 | Validate both `.hcprms1` and `.hcprms2` via full CRC32C streaming reads (128 B/tick each, 34,768 B × 2 files) | ~544 ticks + open/close |
| 50–55 | Reopen winner, read 3,856-byte mask, OR-merge into canonical SRAM mask | ~8 ticks + open |
| 56 | Scan merged mask (256 bits/tick), atomically take dirty bits, capture live values (up to 1,536 patches) | ~121 ticks |
| 10–13 | Reopen winner as copy source; remove inactive target; create new target; transformed copy at 128 B/tick with inline CRC | ~272 ticks + open/close/remove |
| 67, 14–21 | Close source; close target; sync; reopen target; write 4-byte CRC32C | ~10 ticks + sync |
| 57–65 | Close CRC handle; sync; reopen target; write commit byte `0xa5`; close; final sync | ~10 ticks + 2× sync |

**Total per cycle: ~965+ filesystem ticks** of bounded work, plus async
open/close/sync latency. At ~7,600 `filesystem_tick()` calls/second (measured
Session 057), the bounded work alone is ~127 ms, but actual wall time is
dominated by SD I/O latency in the open/close/sync barriers.

**Key observation:** The dual-record validation (phases 0–5) consumes **~544
ticks of CRC work and four file open/close cycles** on every single drain
transaction, including 250 ms continuation cycles. This is the single largest
cost and is entirely redundant when the card has not been removed.

---

## 2. Speedup: Skip Dual-Record Validation on Continuation Cycles

### Rationale

The dual CRC validation exists to re-derive which `.hcprms` file is the valid
winner and whether it matches the current Bank identity. This is necessary on
the *first* drain after boot or after a Bank transition, but on continuation
cycles (250 ms apart within the same Bank session), both files are in known
states — the writer itself just wrote the target and knows which file is the
winner and what its generation is.

The user confirms that SD card removal/modification/re-insertion while the
device is powered is **not part of the product contract**. The only scenarios
that invalidate the cached winner are:

- First drain after boot (no prior knowledge)
- First drain after autosave re-enable
- After an error/retry (card state may have changed)
- After a Bank Load (identity change triggers full re-dirty anyway)

### Design

Add cached winner state to the autosave writer workspace:

```
uint8_t  winner_cached;        // nonzero if winner_index/generation are valid
uint8_t  cached_winner_index;  // 0 = A, 1 = B
uint32_t cached_winner_gen;    // generation of the last committed record
```

On drain entry:

- If `winner_cached` is set, skip phases 0–5 entirely. Jump directly to
  phase 50 (mask load) using the cached winner identity. The target is the
  *other* file (1 - cached_winner_index).
- On successful commit (phase 65/22), update the cache: `cached_winner_index`
  becomes the just-committed target, `cached_winner_gen` becomes the new
  generation.

Invalidate the cache (`winner_cached = 0`) on:

- Boot / `filesystem_ensureAutosaveFilesBlocking()` return
- `filesystem_setAutosaveEnabled()` transitions
- Any drain error (phase reaches error completion)
- Bank Load commit (Bank identity change already triggers
  `autosave_markResidentBankDirty()`, but must also invalidate the winner
  cache)
- Any external reset of the autosave scheduler

### Expected improvement

Continuation cycles drop from ~965 ticks to ~411 ticks of bounded CRC work
(mask load + scan + transformed copy + CRC/commit publication). The four
validation file open/close cycles are eliminated. Wall time savings depend on
SD latency but should be roughly 50% of per-cycle cost.

### SRAM cost

~7 bytes added to the `autosave_writer` workspace (already in union storage).
No new static allocation.

### Risk

Low. The cache is invalidated on every state transition that could change the
winner identity. A false cache hit (theoretically impossible unless the card is
swapped while powered) would cause a CRC mismatch on the next boot validation,
which the existing reader recovery handles.

---

## 3. Speedup: Reduce File Reopen Cycles on Target

### Current state

The target file is opened/closed three separate times in one transaction:

1. Create + transformed copy (phases 24–13)
2. Reopen for CRC write (phases 17–21)
3. Reopen for commit byte write (phases 60–64)

Each reopen requires an async directory scan and SD I/O.

### Proposal

Combine the CRC write and commit byte write into a single reopen cycle. After
the transformed copy completes and the target is closed+synced (data durable),
reopen once, write the CRC at offset 12, then write the commit byte at offset
5, then close+sync. This eliminates one full open/close/sync cycle.

The commit-last contract is preserved because the CRC and commit are written in
the same open session — the commit byte is not durable until the final sync
after both writes.

### Expected improvement

One fewer open/close/sync cycle per transaction. SD-latency-dependent, but
typically 50–100 ms savings.

### SRAM cost

Zero. Reduces phase count.

---

## 4. Speedup: Skip Mask Re-Read When Winner Is Cached

### Current state

Phases 50–55 reopen the winner file and read the full 3,856-byte on-card mask
to OR-merge with the canonical SRAM mask. This recovers interrupted work from
a previous power cycle.

### Proposal

When the winner is cached (i.e., this session wrote the winner and knows its
mask is a subset of the canonical SRAM mask), skip the mask re-read entirely.
The canonical SRAM mask already contains all dirty bits — the on-card mask is
only useful for recovering state from a *previous boot session*.

On a non-cached (first) drain, the mask re-read proceeds as before to recover
any interrupted work.

### Expected improvement

Eliminates ~8 ticks of mask reading plus one file open/close cycle on
continuation drains.

### SRAM cost

Zero. Uses the existing `winner_cached` flag.

---

## 5. Summary of Combined Writer Speedup

| Metric | Current | After speedup |
|---|---|---|
| CRC validation ticks (continuation) | ~544 | 0 |
| Mask re-read ticks (continuation) | ~8 | 0 |
| Transformed copy + CRC ticks | ~272 | ~272 |
| Mask scan + capture ticks | ~121 | ~121 |
| File open/close cycles | 6 pairs | 3 pairs |
| Sync barriers | 3 | 2 |
| **Total bounded ticks (continuation)** | **~965** | **~393** |

First-drain-after-boot retains the full validation path unchanged.

---

## 6. `.hcnames` Extension: Per-Row "Refreshed" Flag

### Purpose

The autosave reader (future) must distinguish between:

- An autosave record that fully represents a sub-object's current state (safe
  to load from autosave), and
- A sub-object that was loaded/saved since the last complete autosave capture
  (must be loaded from its library source instead).

The "refreshed" flag is the bridge. It is set at load/save time and cleared by
the autosave writer only after it has fully resolved that object's mutation
bits.

### Mechanism

**Storage:** Bit 14 of the existing `fs_resident_source[]` register
(`uint16_t`, 129 entries, `filesystem.c:898`). Currently only values 0–999 and
`0x7ffd`–`0x7fff` are used, and bit 15 is already `FS_RESIDENT_SOURCE_DIRTY_FLAG`.
Bit 14 is free. Define:

```c
#define FS_RESIDENT_SOURCE_REFRESHED_FLAG  0x4000u
```

**Set by:**

- `filesystem_requestLoadScene()` completion (the terminal Scene callback):
  sets the refreshed flag on the loaded Scene row, its Kit row, and its six
  Instrument rows. Also requests the autosave writer to re-dirty all mutation
  bits for the loaded Scene at the next drain entry.
- `filesystem_requestLoadKit()` completion: sets the refreshed flag on the Kit
  row and its six Instrument rows. Re-dirties Kit+Instrument mutation bits.
- `filesystem_requestLoadBank()` completion: sets the refreshed flag on every
  loaded Scene and its children. Re-dirties all loaded Scene mutation bits.
- Root Instrument Load completion: sets the refreshed flag on the Instrument
  row. Re-dirties that Instrument's mutation bits.
- Scene Save completion: sets the refreshed flag on the saved Scene row and
  children. Re-dirties the name/source mutation bits (not the entire object
  — parameter edits are already captured).
- Kit Save, Bank Save, Instrument Save: analogous.

**Cleared by:**

- The autosave writer, after it completes a drain cycle that resolves all
  mutation bits for an object that has the refreshed flag set. This is a new
  post-drain step: after the commit succeeds, the writer examines each
  refreshed row. If the canonical SRAM mask has no remaining dirty bits for
  that object's byte range, the refreshed flag is cleared and `.hcnames` is
  safe-rewritten.

**Serialized to card:**

The refreshed flag is serialized as an additional character in the `.hcnames`
row format. Current format: `name<TAB>source\n`. Extended:
`name<TAB>source[<TAB>R]\n`, where the optional `R` suffix after source
indicates refreshed. Absence of the suffix means not-refreshed (backward
compatible with existing `.hcnames` files).

### `.hcnames` safe-rewrite from the autosave writer

Currently the autosave writer never touches `.hcnames`. This change adds a
new post-commit step: if any refreshed flags were cleared this cycle, the
writer must safe-rewrite `.hcnames` using the existing targeted-update
mechanism (`FS_INTERNAL_OP_UPDATE_HCNAMES_*`). This is a read-modify-write
of the specific affected rows, not a full 129-row rewrite.

The safe-rewrite must use the same filesystem facade arbitration as the
existing HCNAMES paths. Since the autosave writer already holds the facade
during its transaction, this is a natural extension — the HCNAMES update
happens as a sub-operation within the drain transaction, after the `.hcprms`
commit succeeds.

### SRAM cost

Zero new static allocation. The refreshed flag uses an unused bit in the
existing 258-byte `fs_resident_source[]` register.

---

## 7. AutoSave Record Extension: Bank Source Field

### Purpose

The autosave reader (Case 1 in the boot model) must verify that the Bank
source in the autosave record agrees with `settings.cfg`'s `active_bank`.
Currently the record stores the restore slot (which is the Bank library slot
number), but not explicitly as a "source" field. The existing `restore_slot`
field (`AUTOSAVE_BANK_SLOT_OFFSET`, 2 bytes LE at Bank payload +0) already
serves this purpose — it is the numbered library slot from which the Bank
was loaded.

### Analysis

**The existing `restore_slot` field already is the bank source.** It records
the library slot number (e.g., 004 for `Bank/004 Full`), which is exactly
what `settings.cfg`'s `active_bank` records. The boot reader can compare
`restore_slot` against the loaded `active_bank` to verify Case 1.

No new field is needed unless the source semantics diverge from the library
slot number in the future (e.g., a Bank loaded from USB or created in-memory
without a library slot). If that case arises, a dedicated 2-byte source field
can be added at Bank payload offset +15 (bytes 15–16, currently reserved)
without a format version change, since the record has not shipped.

### Decision

**No change needed now.** The reader should compare `autosave_bank_slot` against
`settings.cfg active_bank`. Document this as the defined boot-reader contract.

---

## 8. AutoSave Record Extension: Sub-Object Source Fields

### Purpose

The autosave reader (Cases 1a/1b/1c) needs to know the source of each
sub-object (Scene, Kit, Instrument) stored in the autosave record, so it can
detect stale loads. The name is already present (8 bytes per object), but the
source (library slot number or inherit/direct token) is not.

### Proposal

Place a 2-byte LE source field immediately after the name field for each
sub-object, consistent with the `.hcnames` `name<TAB>source` ordering.
This shifts the parameter/normal start offset by 2 bytes for each object:

**Scene section (1,920 bytes, unchanged total):**

| Field | Current offset | New offset | Bytes |
|---|---|---|---|
| Name | +0 | +0 | 8 |
| **Source** | — | **+8** | **2** |
| Parameters | +8 | +10 | 118 (was 120 alloc, 40 live) |
| Effect | +128 | +128 (unchanged) | 512 |
| Kit | +640 | +640 (unchanged) | 1,280 |

The Scene parameter allocation shrinks from 120 to 118 bytes. With 40 live
parameters, 78 bytes remain reserved (was 80). No functional impact.

**Kit section (1,280 bytes within Scene, unchanged total):**

| Field | Current offset | New offset | Bytes |
|---|---|---|---|
| Name | +0 | +0 | 8 |
| **Source** | — | **+8** | **2** |
| Parameters | +8 | +10 | 118 (was 120 alloc, 2 live) |
| Instruments | +128 | +128 (unchanged) | 1,152 |

Kit parameter allocation shrinks from 120 to 118 bytes. With 2 live, 116
remain reserved. No functional impact.

**Instrument record (192 bytes, unchanged total):**

| Field | Current offset | New offset | Bytes |
|---|---|---|---|
| Type | +0 | +0 | 3 |
| Name | +3 | +3 | 8 |
| **Source** | — | **+11** | **2** |
| Normal params | +11 | +13 | 70 (was 72) |
| Morph params | +83 | +85 | 70 (was 72) |
| Reserved | +155 | +157 | 35 (was 37) |

Instrument Normal and Morph allocations each shrink from 72 to 70 bytes.
With 72 descriptor entries currently indexed, **this is a problem**: the
current `AUTOSAVE_INSTRUMENT_PARAMETER_BYTES` is 72 and all 72 are
potentially live. Shifting the normal start by 2 would lose 2 cells.

**Resolution options:**

a) **Grow the Instrument record from 192 to 196 bytes.** This increases
   each Scene's Kit section by 24 bytes (6 × 4), each Scene section by 24
   bytes, and the total record by 384 bytes (16 × 24). New record size:
   35,152 bytes (was 34,768). Mask grows to 3,904 bytes (was 3,856). This
   is under the user's 100-byte SRAM ceiling for the mask increase (48 new
   mask bytes in the canonical mask). The on-card file grows by 384 bytes.

b) **Place Instrument source at the tail of reserved padding** (record +155,
   2 bytes) instead of after name. Instrument layout is name-at-3,
   source-at-155 — inconsistent with Scene/Kit ordering but avoids touching
   any live parameter allocation.

c) **Shrink Normal/Morph from 72 to 70 live cells.** Would require dropping
   two descriptor entries from autosave coverage, which loses data.

**Recommendation: option (a)** — grow the record. The SRAM increase is 48
bytes of canonical mask (within the 100-byte pre-approval) plus negligible
workspace. The on-card increase is 384 bytes. The layout is then fully
consistent: name → source → parameters for every object type.

### Source token encoding

The source is stored as `uint16_t` LE, using the same token values as
`fs_resident_source[]`: 0–999 for numbered library slots, `0x7fff` for
inherit (`-`), `0x7ffe` for unknown (`?`), `0x7ffd` for direct (`@`).
Bits 14–15 (dirty/refreshed flags) are masked off before writing to the
record.

### Live getter

New `autosave_getSourceByte(uint16_t source_value, uint8_t byte_index)`
reads from `fs_resident_source[]` for the appropriate row, masks off flags,
and returns the low or high byte of the 2-byte LE value.

### Dirty marking

The existing typed markers are extended to include the 2-byte source field.
Load and Save completions stage the new source into `fs_resident_source[]`
(they already do this) and mark the source bytes dirty via the typed marker.

### SRAM cost

If option (a): +48 bytes canonical mask (3,856 → 3,904). Within pre-approval.
Source values come from the existing `fs_resident_source[]` register — no
new allocation for the getters.

---

## 9. Boot Process Alignment

### Current boot flow (Case 3 — what runs today)

1. `settings.cfg` loaded → `active_bank` known, `autosave` on/off known
2. `filesystem_ensureAutosaveFilesBlocking()` → creates/validates hidden files
3. `.hcnames` read into the mirror and source register
4. Bank loaded from library slot per `active_bank` (or fallback)
5. Mutation tracking enabled; full Bank marked dirty
6. Autosave writer starts its first drain

### User's described boot model

**Case 1 (future reader):** Autosave valid, `restore_slot` agrees with
`active_bank`. Load root Bank data from autosave. Then per sub-object:

- **1a:** Sub-object not refreshed in `.hcnames`, name/source mutation bits
  clean → load from autosave.
- **1b:** Sub-object marked refreshed in `.hcnames` → load from its library
  source (per `.hcnames` source token).
- **1c:** Sub-object has stale name/source mutation bits, no valid `.hcnames`
  help → empty the Scene, display error.

**Case 2:** Autosave invalid, `.hcnames` valid, autosave ON → load from
`.hcnames` sources. Mark entire Bank dirty for autosave.

**Case 3 (current):** Both invalid or autosave OFF → load from `settings.cfg`
/ library fallback. Mark entire Bank dirty if autosave ON.

### Alignment analysis

The current codebase implements Case 3 only. The writer changes in this
session prepare for Cases 1/2 by ensuring:

1. The autosave record contains source fields for all sub-objects (Section 8).
2. `.hcnames` carries the refreshed flag so the reader can distinguish
   1a from 1b (Section 6).
3. The writer clears refreshed flags only after fully capturing the object,
   maintaining the safety invariant (Section 6).
4. `.hcnames` is regenerated with correct sources on every load/save
   (existing behavior, extended with refreshed flag).

**The boot reader itself is deferred** — it will be implemented after these
writer-side preparations are in place and tested. The current boot path (Case
3) remains the active path regardless of autosave setting until the reader is
implemented.

### `.hcnames` regeneration on autosave-ON with missing `.hcnames`

Case 1 specifies that if `.hcnames` is missing/invalid but autosave is valid,
`.hcnames` should be regenerated from the autosave names, with only the bank
having a source and all sub-objects inheriting upward. This is a reader-side
concern and is deferred with the reader.

---

## 10. Load/Save Mutation Bit Re-Dirtying

### Current behavior

Load completions call existing whole-object markers (e.g.,
`autosave_markSceneWithPatternDirty()` for Scene Load). These mark currently
gettable cells but use the existing marker functions.

### Required change

Load completions must additionally:

1. Set the refreshed flag on the loaded object's `.hcnames` rows.
2. Request the autosave writer to re-dirty **all** mutation bits for the loaded
   object at the next drain entry (not immediately — the load callback is not
   the right place to mutate the mask wholesale).

Save completions must:

1. Set the refreshed flag on the saved object's `.hcnames` rows.
2. Request the autosave writer to re-dirty only the **name and source**
   mutation bits (parameter edits are already captured — the object's data
   hasn't changed, only its library source identity).

### Mechanism

A per-Scene (and per-Kit, per-Instrument) "re-dirty request" flag, checked at
drain entry. When set, the drain's phase-56 mask scan ORs in all bits for the
affected object range before proceeding. This is a one-time bulk dirty, not a
persistent state.

### SRAM cost

A 16-bit re-dirty scene mask (one bit per Scene), plus a flag for Kit-only and
per-Instrument re-dirty. Approximately 4–6 bytes in the writer workspace.

---

## 11. Concerns and Open Questions

### Q1: Format version bump — RESOLVED

No version bump. This is development firmware; nothing has shipped. The
existing format version 1 absorbs the source field additions into its
reserved space. If a future shipped version needs to distinguish record
generations, a version bump can be added at that point.

### Q2: `.hcnames` safe-rewrite atomicity — RESOLVED

All `.hcnames` writes will use the same temp-file safe-write pattern as
`settings.cfg` (Session 057). The pattern is:

1. Write to `.hcnamtmp` (truncate is safe on a temp file).
2. Close + `afatfs_sync()` — make temp file durable.
3. Remove existing `.hcnames` (case-insensitive, tolerate not-found).
4. Rename `.hcnamtmp` → `.hcnames`.
5. Final sync through `filesystem_finish()`.

At boot, before reading `.hcnames`, check for a leftover `.hcnamtmp`. If
found, validate it (correct row count, parseable rows). If valid, promote
(remove `.hcnames`, rename temp → `.hcnames`). If invalid, discard (remove
temp). Then proceed to read `.hcnames` normally. This mirrors the
`settings.cfg` recovery prelude exactly (`filesystem.c:17868–18083`).

The existing `afatfs_renameObject_lfn()`, `afatfs_removeObjects_lfn()`, and
`afatfs_sync()` primitives are already used for `settings.cfg` safe-write
and require no driver changes.

**Both** HCNAMES write paths need this treatment:

- **Boot full-write** (`filesystem_writeResidentNames_tick()`, `filesystem.c:
  4750`): currently opens `.hcnames` with `"w"` directly. Change to write
  `.hcnamtmp`, sync, remove old, rename.
- **Runtime targeted-update** (`filesystem_residentNames_tick()`, `filesystem.c:
  5545`, phase 5): currently reads `.hcnames`, overlays rows, then reopens
  `.hcnames` with `"w"` for rewrite. Change the rewrite target to
  `.hcnamtmp`, then sync + remove-old + rename after close.
- **New autosave-writer post-commit HCNAMES update**: uses the same temp-file
  pattern from the start.

The temp filename `.hcnamtmp` is chosen to be:
- Distinct from `.hcnames` and from Instrument `.hctmp.<ext>` files.
- A dot-prefixed hidden file (excluded from product scanners).
- Short enough for an 8.3 SFN fallback.

Boot recovery for `.hcnamtmp` is added to the existing
`filesystem_ensureAutosaveFilesBlocking()` path, which already runs before
`.hcnames` is read. The recovery logic is identical to `settings.cfg`
recovery: open temp → validate → promote or discard → continue.

### Q3: AsyncFATFS driver changes

**None required.** The autosave writer already uses `afatfs_fopen`,
`afatfs_fread`, `afatfs_fwrite`, `afatfs_fclose`, `afatfs_sync`,
`afatfs_deleteTree`, and `afatfs_fseekAtomic`. The `.hcnames` write uses
existing paths. No new filesystem primitives are needed.

### Q4: SRAM allocation summary — pre-approved up to 100 bytes

| Item | Bytes | Region | New? |
|---|---|---|---|
| Winner cache in writer workspace | ~7 | Union (existing) | New fields in existing union |
| Refreshed flag in `fs_resident_source[]` | 0 | Existing 258 B | Bit 14, no new allocation |
| Re-dirty request flags | ~6 | Writer workspace | New fields in existing union |
| Source getters for autosave | 0 | Read from existing register | No allocation |
| **Total new static SRAM** | **~13 bytes** | | In existing union/workspace |

Pre-approved: up to 100 bytes total new static SRAM without further
approval. If the implementation exceeds 100 bytes, escalate before
proceeding. No new DTCM or SRAM1 reservation impact anticipated.

### Q5: Interaction with the name-cache ownership hazard

`SCOPING_TARGETS.md` notes that the autosave writer reads the shared 9,000-byte
name cache live during serialization, and Menu clears it from 18 call sites.
The `.hcnames` safe-rewrite from the autosave writer uses the dedicated
1,161-byte HCNAMES mirror (`hcnames_name_mirror`), NOT the shared name cache,
so it does not worsen this existing hazard.

### Q6: Effect sub-objects

The current record allocates 512 bytes per Scene for Effects with zero live
parameters. The refreshed flag and source field design accommodates Effects
when they are implemented — an Effect row in `.hcnames` and a source field in
the Effect section can be added following the same pattern. No preemptive
allocation is needed.

### Q7: Mutation bits for name and source — layout decision

User preference: name then source, as laid out in `.hcnames`. The autosave
record will place the source field immediately after the name field for each
object type, consistent with the `.hcnames` `name<TAB>source` ordering:

| Object | Name offset | Name bytes | Source offset | Source bytes |
|---|---|---|---|---|
| Scene | Scene +0 | 8 | Scene +8 (was first parameter byte) | 2 |
| Kit | Kit +0 | 8 | Kit +8 (was first parameter byte) | 2 |
| Instrument | Inst +3 | 8 | Inst +11 (was first normal byte) | 2 |

This shifts the parameter/normal start offset by 2 for each object type.
See Section 8 revised offsets below.

The "name/source mutation bits" for Save re-dirtying are these 10 bytes per
object (8 name + 2 source). For Kit, which contains 6 Instruments, a Kit
Save re-dirties Kit name+source (10 B) plus all 6 Instrument name+source
(60 B) = 70 bytes of mask bits total. A Scene Save additionally includes
its own 10 B plus Kit's 70 B = 80 B per Scene.

### Q8: The "empty Scene + error" contract (Case 1c)

Case 1c says: if a sub-object has stale name/source bits and `.hcnames` can't
help, don't load it — present as empty and display an error. This is a
reader-side behavior. The writer-side preparation ensures that the reader
*can* detect this case (source fields + refreshed flag + mutation bits exist).
The actual empty-Scene presentation and error display are deferred to the
reader implementation.

---

## 12. Phased Implementation Plan

### Phase A: Writer speedup

1. Add winner cache to the autosave writer workspace.
2. On continuation drains (winner_cached set), skip phases 0–5 and 50–55.
3. Combine CRC+commit reopen into a single open/close/sync cycle.
4. Invalidate cache on boot, re-enable, error, Bank Load.
5. Test: verify first drain does full validation; subsequent drains skip it;
   Bank Load triggers re-validation; error triggers re-validation.

### Phase B: `.hcnames` atomic safe-write

1. Define `FS_RESIDENT_NAMES_TEMP_FILENAME` (`.hcnamtmp`).
2. Refactor the boot full-write (`filesystem_writeResidentNames_tick()`) to
   write `.hcnamtmp`, close, sync, remove old `.hcnames`, rename temp →
   `.hcnames`, final sync.
3. Refactor the runtime targeted-update (`filesystem_residentNames_tick()`,
   phase 5) to write `.hcnamtmp` instead of `.hcnames`, then sync + remove-
   old + rename.
4. Add `.hcnamtmp` recovery prelude to `filesystem_ensureAutosaveFilesBlocking()`
   or the settings load path: open temp → validate row count/parseability →
   promote (valid) or discard (invalid) → continue to `.hcnames` read.
5. The new autosave-writer HCNAMES update (Phase B2) uses the same temp-file
   pattern from the start.

### Phase B2: `.hcnames` refreshed flag

1. Define `FS_RESIDENT_SOURCE_REFRESHED_FLAG` (bit 14).
2. Extend `.hcnames` row format with optional `\tR` suffix after source.
3. Extend `filesystem_formatResidentNameLine()` to emit `R` when flag is set.
4. Extend `filesystem_cacheResidentRecord()` / parse path to read `R` flag.
5. Set refreshed flag in load/save completion callbacks.
6. Extend the autosave writer post-commit step to check and clear refreshed
   flags when mutation bits are fully resolved for that object.
7. After clearing, trigger an `.hcnames` safe-rewrite (using Phase B pattern)
   as a sub-operation within the drain transaction.

### Phase C: Autosave source fields

1. Grow `AUTOSAVE_INSTRUMENT_RECORD_BYTES` from 192 to 196 (if option (a)
   from Section 8 is accepted). Update all dependent geometry constants and
   `_Static_assert`s in `Autosave.h`.
2. Define source offsets: Scene +8, Kit +8, Instrument +11.
3. Shift parameter offsets: Scene params +10 (was +8), Kit params +10 (was
   +8), Instrument normal +13 (was +11), Instrument morph +85 (was +83).
4. Update all `_Static_assert`s and compile-time geometry checks.
5. Add `autosave_getSourceByte()` getter in `Autosave.c`.
6. Add source dirty markers (extend existing typed markers with source bytes).
7. Update `autosave_transformDrainChunk()` and `autosave_getLivePayloadByte()`
   to handle source bytes via the new getters.

### Phase D: Re-dirty mechanism

1. Add per-Scene re-dirty request mask (uint16_t) to writer workspace.
2. At drain entry (phase 56), OR in all bits for re-dirty-flagged Scenes
   before the normal mask scan.
3. Set re-dirty flags from load/save completions.
4. Save completions: re-dirty only name+source bits for the saved object and
   all contained sub-objects (e.g., Scene Save dirties Scene name+source,
   Kit name+source, and all 6 Instrument name+source).
5. Load completions: re-dirty all bits for the loaded object and all
   contained sub-objects.
6. Clear re-dirty flags after they are consumed by the drain.

### Phase E: Boot reader (future session, deferred)

1. Implement Case 1/1a/1b/1c at boot.
2. Implement Case 2 fallback.
3. Case 3 remains the existing path.

---

## 13. Files Changed (Phases A–D)

| File | Changes |
|---|---|
| `config.h` | New constants if needed for refreshed flag serialization |
| `Core/Bank/Scene/Autosave.h` | Source field offsets; format version 2; refreshed helpers |
| `Core/Bank/Scene/Autosave.c` | Source getters; source markers; refreshed-flag helpers |
| `Core/Hardware/SD/filesystem.c` | Winner cache; phase skip; CRC+commit merge; HCNAMES safe-rewrite from drain; refreshed flag set/clear; re-dirty mechanism |
| `Core/Hardware/SD/filesystem.h` | Refreshed flag constant; any new public API |
| `knowledge_files/specification_reference/AUTOSAVE.md` | Document all changes |

No changes to `asyncfatfs.c/h`, `BankData.c/h`, `SceneData.c/h`,
`presetManager.c`, `main.c`, `menu.c`, the linker script, or the Makefile.

---

## 14. What This Does NOT Change

- The boot path remains Case 3 until the reader (Phase E) is implemented.
- No AsyncFATFS driver changes.
- No new SRAM regions or reservation policy changes. New static SRAM is
  within the 100-byte pre-approval (estimated ~61 bytes: 48 mask + 13
  workspace).
- The five-second initial debounce and 250 ms continuation interval are
  unchanged.
- The page-exit expedite (Session 056) is unchanged and compatible.
- `settings.cfg` format and writer are unchanged.

What DOES change on-card (if option (a), Section 8):

- `.hcprms1` and `.hcprms2` grow from 34,768 to 35,152 bytes.
- Mutation mask grows from 3,856 to 3,904 bytes.
- A new temp file `.hcnamtmp` may transiently exist during `.hcnames` writes.
- `.hcnames` row format gains an optional `\tR` refreshed suffix.
