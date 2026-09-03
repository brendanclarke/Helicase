# Phase D: Re-Dirty Mechanism — General Plan

Source: `S060_AUTOSAVE_UPDATE_READER_PREP.md` Sections 6, 10, and 12 (Phase D).

## What Phase D delivers

When a sub-object is loaded or saved mid-session, the autosave record must
converge to the new state. Phase D ensures the autosave writer re-captures
the affected bytes and, once fully captured, clears the refreshed flag and
safe-rewrites `.hcnames` so the boot reader can distinguish between
"autosave is authoritative" and "reload from library."

---

## 1. Parent plan specification (Section 10)

The parent plan describes Phase D as six steps:

1. Add a per-Scene re-dirty request mask (`uint16_t`) to the writer workspace.
2. At drain entry (phase 56), OR in all mutation bits for re-dirty-flagged
   Scenes before the normal mask scan.
3. Set re-dirty flags from load/save completions.
4. Save completions: re-dirty only name+source bits (10 B per object: 8 name +
   2 source) for the saved object and all contained sub-objects.
5. Load completions: re-dirty all bits for the loaded object and all contained
   sub-objects.
6. Clear re-dirty flags after they are consumed by the drain.

SRAM cost estimate: ~4–6 bytes for a uint16_t scene mask plus Kit/Instrument
flags.

---

## 2. Current state: what's already implemented

Phases B2 and C have already built all of the functional machinery that
Phase D was designed to provide. This section maps each Phase D requirement
to existing code.

### 2a. Load completions re-dirty all mutation bits

**Parent plan requirement:** "Request the autosave writer to re-dirty all
mutation bits for the loaded object at the next drain entry."

**Already implemented by:** Compound dirty markers called from Preset layer
load callbacks:

| Load type | Marker called | Coverage |
|-----------|--------------|----------|
| Scene Load | `autosave_markSceneWithoutPatternDirty()` | Scene params + source + Kit params + source + 6 × Instrument type/source/endpoints |
| Kit Load | `autosave_markKitDirty()` | Kit params + source + 6 × Instrument type/source/endpoints |
| Instrument Load | `autosave_markWholeInstrumentDirty()` | Instrument type + source + normal + morph endpoints |
| Bank Load | `autosave_markResidentBankDirty()` | All 16 Scenes via `markSceneWithoutPatternDirty()` |

These markers fire **immediately** from the load completion callback, not
deferred to the next drain entry. Each call goes through
`autosave_markPayloadOffsetDirty()`, which uses an IRQ-save/restore atomic
to set individual mask bits. The drain's phase-56 scanner sees the set bits
and captures the live values.

**Why immediate works:** The parent plan preferred deferred re-dirtying
("the load callback is not the right place to mutate the mask wholesale")
out of concern that immediate mask mutation could interfere with an active
drain. In practice, the atomic per-bit operations are safe:

- If the drain has not yet scanned past the bit, it captures the new value
  in the current cycle.
- If the drain has already scanned past the bit, the bit stays set and is
  captured in the next cycle.
- In both cases, `autosave_objectFullyCaptured()` returns false until all
  bits are clean, so the refreshed flag stays set until full capture.

The immediate approach is **strictly better** than deferred: it can capture
some or all changes in the current cycle, whereas a deferred mask OR at
drain entry always adds one cycle of latency.

### 2b. Save completions re-dirty source bits

**Parent plan requirement:** "Re-dirty only the name and source mutation bits."

**Already implemented by:** Phase C's `autosave_markSourceDirty()` calls at
all three save completion families in `filesystem.c`:

| Save type | Call site | Rows marked |
|-----------|----------|-------------|
| Instrument Save | `filesystem.c:14103` | 1 Instrument |
| Kit Save | `filesystem.c:15805–15815` | Kit + 6 Instruments |
| Scene Save | `filesystem.c:17388–17404` | Scene + Kit + 6 Instruments |

Each call is paired with the corresponding `filesystem_setResidentSource()`
that changes the source value. The drain then captures the new 2-byte source
value via `autosave_getSourceByte()`.

**Name bytes are deliberately excluded** — see Section 3 below.

### 2c. Refreshed flag set at load/save completions

**Already implemented by:** Phase B2's `filesystem_setResidentRefreshed()`
and `filesystem_setResidentSceneRefreshed()`, called from all load/save
completion sites:

| Completion | Call site | Rows refreshed |
|-----------|----------|---------------|
| Scene Load | `filesystem.c:11875` | Scene + Kit + 6 Instruments (via `setResidentSceneRefreshed`) |
| Kit Load | `filesystem.c:10376–10381` | Kit + Instruments |
| Instrument Load | `filesystem.c:10376` | 1 Instrument |
| Bank Load | `filesystem.c:12659, 12835` | Bank row 0 |
| Instrument Save | `filesystem.c:14110` | 1 Instrument |
| Kit Save | `filesystem.c:15823–15828` | Kit + Instruments |
| Scene Save | `filesystem.c:17413` | Scene + Kit + 6 Instruments (via `setResidentSceneRefreshed`) |

### 2d. Post-drain: check objectFullyCaptured, clear refreshed, rewrite HCNAMES

**Already implemented by:** Phase B2's post-commit pipeline:

1. `filesystem_autosaveDrainAfterCommit()` (`filesystem.c:6746`) — checks
   whether any refreshed rows have fully resolved mutation bits.
2. If yes, enters phase 70–78: open `.hcnamtmp`, stream all 129 rows with
   the refreshed flag state, sync, remove old `.hcnames`, rename temp →
   `.hcnames`, final sync.
3. Terminal callback (`filesystem.c:22655`) — on successful completion,
   calls `filesystem_clearResidentRefreshedCaptured()` which clears bit 13
   on every row where `objectFullyCaptured()` returns true.
4. `op_file_version` tracks whether an HCNAMES convergence was attempted;
   an error preserves the refreshed flag for the next retry cycle.

### 2e. Deferred re-dirty request mask

**Parent plan specified:** A `uint16_t` per-Scene mask in the writer workspace.

**Not implemented, and not needed.** The immediate compound markers (2a)
and immediate source marking (2b) provide the same correctness guarantees
as the deferred mechanism. The deferred mask would:
- Add ~4–6 bytes of persistent SRAM
- Add a mask-OR step at drain entry
- Add load/save completion calls to set the mask
- Introduce a new ordering dependency (mask must be consumed before clearing)

All of this complexity is avoided by the existing immediate marking path.

---

## 3. Analysis: name-byte re-dirtying is unnecessary

The parent plan specifies that save completions should re-dirty "name and
source" bits — 10 bytes per object (8 name + 2 source). Phase C implements
source marking but deliberately excludes name bytes. This section explains
why.

### Name bytes have no live getter

`autosave_getLivePayloadByte()` dispatches source bytes, parameter bytes,
type bytes (Instrument), and endpoint bytes. Name bytes (Scene +0..+7,
Kit +0..+7, Instrument +3..+10) fall through all dispatch checks and
return 0 — "no live owner."

When the drain encounters a dirty bit for a name byte, it calls
`getLivePayloadByte()`, gets 0, and skips the byte without adding it to
the patch list. The dirty bit is consumed but no write occurs. This is
equivalent to never marking the byte dirty.

### Compound markers exclude names by design

The compound markers (`markWholeInstrumentDirty`, `markKitDirty`,
`markSceneWithoutPatternDirty`) explicitly exclude HCNAMES-owned name
bytes. From `Autosave.c:1319`: "HCNAMES-owned name is deliberately
excluded."

Name bytes are set only by `autosave_initialRecordByte()` during initial
record creation and preserved by copy-forward during subsequent drains.
They are never updated by the drain's live-capture path.

### Names in the autosave record may be stale after a load

After a Scene Load changes the resident scene, the autosave record retains
the old scene's name from initial creation. This is acceptable because:

1. **The reader uses `.hcnames` for identity, not autosave record names.**
   The `.hcnames` file carries the current name, source, and refreshed flag.
   The reader's load decision (autosave vs. library) depends on source
   fields, refreshed flags, and mutation-bit cleanliness — not on whether
   the autosave record's embedded name matches.

2. **Adding a name getter would require new RAM access.** The HCNAMES name
   mirror (`hcnames_name_mirror[]`) is a `filesystem.c`-private array. To
   read from it, a new public accessor and dispatch would be needed,
   touching files that currently have a clean one-way dependency
   (filesystem.c → Autosave.h, not the reverse).

3. **Re-dirtying names without a getter is a no-op.** Marking name bytes
   dirty then having `getLivePayloadByte()` return 0 just wastes drain
   cycles — the bits get consumed without writing anything.

### Conclusion

Name-byte re-dirtying serves no correctness purpose in the current
architecture. The 8-byte name in each sub-object header is a diagnostic
artifact from initial record creation. If a future Phase E requirement
needs self-consistent autosave record names, a name getter can be added
at that time. For now, the reader design does not depend on it.

---

## 4. Remaining Phase D work

Given the analysis above, Phase D has no new code to implement. The
remaining work is **verification testing** of the end-to-end lifecycle.

### Test 1: Load → drain → refreshed flag cleared → HCNAMES rewritten

1. Boot with a known bank. Note the `.hcnames` file on card.
2. Perform a Scene Load (any scene, any slot).
3. Allow one or more drain cycles to complete.
4. Pull the card. Verify:
   - `.hcnames` was safe-rewritten (fresh `.hcnames`, no stale `.hcnamtmp`).
   - The loaded scene's source value in `.hcnames` reflects the load source.
   - The `R` suffix is absent on the loaded scene's row (refreshed flag
     cleared after full capture).
   - The autosave record's source bytes at the loaded scene's offset match
     the `.hcnames` source value.

### Test 2: Save → drain → refreshed flag cleared → HCNAMES rewritten

1. With a resident bank, perform a Scene Save (or Kit Save).
2. Allow drain cycles to complete.
3. Pull the card. Verify:
   - `.hcnames` source values reflect the saved slot numbers.
   - `R` suffix is absent (refreshed cleared after source capture).
   - Autosave record source bytes match.

### Test 3: Load during active drain (race condition)

1. Trigger a Scene Load while a drain is actively scanning (hard to time
   precisely, but can be attempted by loading immediately after a drain
   starts).
2. Verify that the refreshed flag stays set until the next drain fully
   captures the loaded scene's bytes.
3. Confirm that no data corruption occurs (CRC validation on the committed
   record).

### Test 4: Multiple overlapping loads

1. Load Scene A, then immediately load Scene B before the first load's
   drain completes.
2. Verify that both scenes eventually converge: refreshed flags cleared,
   source fields correct, HCNAMES consistent.

---

## 5. Risk registry from previous phases

### From Phase A (writer speedup)

- **Off-by-one in drain chunking**: The drain processes a bounded number of
  mask positions per tick. Phase A optimized this but the bounds must be
  respected by any new mask-OR or scan operation. Phase D adds no new drain
  operations, so this risk does not apply.

### From Phase B/B2 (HCNAMES safe-write, refreshed flag)

- **HCNAMES mirror validity**: The post-drain HCNAMES rewrite reads from
  `hcnames_name_mirror[]` and `fs_resident_source[]`. If the mirror is
  invalid (`hcnames_mirror_valid != FS_HCNAMES_MIRROR_VALID`), the
  `filesystem_autosaveDrainHasRefreshWork()` function fails closed (returns
  0, skipping the rewrite). This guard is critical: a stale mirror
  serialized to card would corrupt the `.hcnames` file.

- **Error path preserves refreshed flags**: If the post-drain HCNAMES
  rewrite fails (disk full, I/O error), the refreshed flags are NOT cleared
  (`op_file_version` stays nonzero, but the terminal callback only clears
  on `FS_STATUS_DONE`). The next successful drain retries the rewrite. This
  is correct — a failed rewrite must not mark the system as converged.

- **Safe-write atomicity**: The `.hcnamtmp` → `.hcnames` rename pattern
  ensures that a power failure at any point leaves either the old valid
  `.hcnames` or a fully synced new one. The temp file's presence after a
  power failure is harmless — the next boot ignores it and rewrites on the
  next drain.

### From Phase C (source fields)

- **Zero-growth discipline**: Phase C absorbed source fields into existing
  reserved space with no record growth. Phase D must maintain this: no new
  persistent SRAM, no new fields in the wire format.

- **Pairing source mutations with mask updates**: Every
  `filesystem_setResidentSource()` call is paired with
  `autosave_markSourceDirty()`. If future code adds new source mutations
  (e.g., for a "rename" operation), the same pairing must be maintained.
  The current codebase has no mechanism to enforce this pairing at compile
  time — it's a developer discipline requirement.

- **Source token encoding**: Post-Phase-B2 tokens are 13-bit
  (`INHERIT = 0x1FFF`, `UNKNOWN = 0x1FFE`, `INSTRUMENT_DIRECT = 0x1FFD`).
  The parent plan document still references old 15-bit values. Follow the
  code, not the document.

### General best practices

- **`make clean && make`** after any header change. No header dependency
  tracking in the Makefile; incremental builds can miss stale object files.
- **Static asserts are the safety net.** Every geometry relationship should
  have a corresponding `_Static_assert`. Phase C added 4 new structural
  asserts; Phase D adds none (no new geometry).
- **Audit all call sites.** When adding a new operation (dirty marking, flag
  setting), grep for ALL existing call sites of the paired operation
  (`setResidentSource`, `setResidentRefreshed`, compound markers) to ensure
  nothing is missed.
- **Test the terminal error path.** Phase B2's HCNAMES convergence has an
  error path that preserves refreshed flags. Verify this path is exercised
  (e.g., by simulating a disk-full condition) if possible.

---

## 6. Forward declarations already in place

Phase B2 added forward declarations for three functions in `filesystem.c`
that were designed for Phase D integration:

```
static uint8_t filesystem_autosaveDrainHasRefreshWork(void);   /* line 1320 */
static void filesystem_clearResidentRefreshedCaptured(void);   /* line 1321 */
static void filesystem_autosaveDrainAfterCommit(void);         /* line 1322 */
```

All three are **fully implemented** (not just declared):

- `filesystem_autosaveDrainHasRefreshWork()` (`filesystem.c:6699–6722`):
  scans all 129 rows for refreshed + fully-captured objects. Returns 1 if
  any exist. Guards on `hcnames_mirror_valid`.

- `filesystem_clearResidentRefreshedCaptured()` (`filesystem.c:6724–6744`):
  clears bit 13 on every row where refreshed is set AND
  `objectFullyCaptured()` returns true. Called from the terminal callback
  after a successful HCNAMES convergence commit.

- `filesystem_autosaveDrainAfterCommit()` (`filesystem.c:6746–6765`):
  entry point called from drain phase completion (lines 7170, 7634). If no
  refresh work exists, finishes the drain normally. Otherwise, sets
  `op_file_version = 1` and enters phase 70 (HCNAMES temp-file writer).

These functions are called from the drain state machine at two points:
1. After mask merge when no dirty bits exist (line 7170) — handles the case
   where a load set the refreshed flag but no parameter edits are pending.
2. After the autosave target commit (line 7634) — the normal post-drain
   convergence path.

---

## 7. Implications for Phase E (boot reader)

The boot reader needs to determine, for each sub-object, whether to load
from the autosave record or from the library:

- **Case 1a** (not refreshed, mutation bits clean): load from autosave.
  The record fully represents the current state.
- **Case 1b** (refreshed): load from library source. The record may be
  mid-capture. The `.hcnames` source field tells the reader which library
  slot to load from.
- **Case 1c** (stale name/source, no library fallback): present as empty,
  display error. This is a degraded recovery path.

The writer-side preparation for Phase E is complete:
- Source fields identify where each sub-object came from (Phase C).
- The refreshed flag distinguishes "autosave is authoritative" from "reload
  from library" (Phase B2).
- The post-drain HCNAMES convergence ensures that cleared refreshed flags
  are durably persisted to card (Phase B2).
- Mutation-bit cleanliness is the gate for refreshed flag clearing, ensuring
  the record is fully captured before declaring it authoritative (Phase B2).

Phase E implementation is deferred to a future session.

---

## 8. Summary

| Phase D requirement | Status | Implemented by |
|---------------------|--------|---------------|
| Load re-dirties all mutation bits | Done | Compound markers (immediate, via Preset callbacks) |
| Save re-dirties source bits | Done | Phase C `autosave_markSourceDirty()` at save completions |
| Save re-dirties name bits | Not needed | Names have no live getter; reader uses `.hcnames` for identity |
| Refreshed flag set at load/save | Done | Phase B2 `filesystem_setResidentRefreshed()` |
| Post-drain objectFullyCaptured check | Done | Phase B2 `filesystem_autosaveDrainHasRefreshWork()` |
| Post-drain HCNAMES safe-rewrite | Done | Phase B2 drain phases 70–78 |
| Refreshed flag clearing after commit | Done | Phase B2 `filesystem_clearResidentRefreshedCaptured()` |
| Deferred re-dirty request mask | Not needed | Immediate marking is correct and lower-latency |
| SRAM cost | 0 bytes | No new persistent allocation |

**Phase D requires no new code.** All functional requirements are satisfied
by the combination of Phase B2 (refreshed flag lifecycle, post-drain
HCNAMES convergence) and Phase C (source dirty marking at save completions).
The remaining work is verification testing of the end-to-end lifecycle
described in Section 4.
