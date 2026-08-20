# AsyncFATFS Recursive Delete — Diagnostic Instrumentation Whitepaper

Session 054 (2026 series). Documents every diagnostic hook added while chasing
the `ScnS05` Scene-overwrite error: what each hook is, how it is implemented,
where it lives, what flags gate it, what SRAM it costs, what it is meant to
prove, and what it has actually proven so far. All facts below were re-verified
against the current working tree immediately before writing this document
(line numbers current as of this commit's working state).

This is a diagnostics document, not a design spec. Every hook described here
is either read-only observation or a narrowly-scoped error-classification
write; none of them change save/load behavior except the documented
exceptions (the AutoSave drain stall, and the already-shipped root-cause
fixes: Bugs #1/#2 recapped in §11, Bugs #3/#4 in §6, and **Bug #5 — the
actual `ScnS05` defect — in §6a**, which supersedes the premise §6 was
written on).

---

## 1. The trace ring itself (context for everything below)

All hooks funnel through the existing `autosaveTrace_record()` API in
[`Core/Bank/Scene/AutosaveTrace.c`](Core/Bank/Scene/AutosaveTrace.c) /
[`AutosaveTrace.h`](Core/Bank/Scene/AutosaveTrace.h). Nothing new was built at
the storage layer this session — only new *stages* (record types) and their
bit layouts.

- **Record format** (8 bytes, fixed): `stage` (1 byte, ASCII tag), `flags`
  (1 byte), `tick16` (2 bytes), `value32` (4 bytes). Offsets are
  `AUTOSAVE_TRACE_STAGE_OFFSET=0`, `FLAGS_OFFSET=1`, `TICK_OFFSET=2`,
  `VALUE_OFFSET=4`, `RECORD_BYTES=8`.
- **Ring storage**: `static volatile uint8_t autosave_trace_records[AUTOSAVE_TRACE_RECORD_COUNT][8]`
  in AutosaveTrace.c, plus three `uint16_t` cursors (write/flush/dropped).
  Entirely inside `#if DEV_MODE_LOGGING`; with logging off, the array does not
  exist and every public function in the header compiles to a no-op stub.
- **Capacity**: `AUTOSAVE_TRACE_RECORD_COUNT`. AutosaveTrace.h falls back to
  `64u` under an `#ifndef` guard for any translation unit that includes it
  before config.h, but the effective value is set by
  [`config.h:255`](config.h#L255): `#define AUTOSAVE_TRACE_RECORD_COUNT 2048u
  /* TEMPORARY approved expansion */` (default documented at
  `config.h:254` as `AUTOSAVE_TRACE_RECORD_COUNT_DEFAULT 64u`). This is a
  session-scoped, user-approved RAM expansion for debugging, not a permanent
  size — it should be reverted to 64u (or removed in favor of the default)
  once the recursive-delete bug is closed.
- **Drain**: the ring is flushed to `/asavetrc.bin` on the SD card by
  filesystem.c's existing AutoSave writer machinery; decoded off-device by
  [`tools/decode_devlogs.py`](tools/decode_devlogs.py).
- **Governing flags**:
  - `DEV_MODE_LOGGING` (`config.h:88`, currently `1`) — gates the ring's
    existence, `autosaveTrace_record()`'s body, and every hook described in
    this document. With it at `0`, all instrumentation compiles away to
    nothing (no SRAM, no calls, stubs only).
  - `DEV_MODE_DIAGNOSTIC` (`config.h:68`, currently `0`) — orthogonal;
    gates on-screen diagnostics, not the trace ring. None of this session's
    hooks depend on it.

---

## 2. SRAM accounting

All figures are additions made *this session*, layered on top of whatever the
ring itself already cost.

| Item | Bytes | Gated by | File:line |
|---|---|---|---|
| Trace ring capacity expansion (64→2048 records × 8 bytes) | +15,872 | `DEV_MODE_LOGGING` | config.h:255 |
| `op_delete_slot_error_reason` (`fs_delete_slot_reason_t`, effectively `uint8_t`-sized enum) | 1 | unconditional | filesystem.c (declared with delete-slot statics, ~line 641 area) |
| `op_delete_slot_error_detail` (`uint8_t`) | 1 | unconditional | filesystem.c |
| `op_delete_slot_error_site` (`uint8_t`) | 1 | unconditional | filesystem.c |
| `op_delete_slot_stall_ticks` (`uint32_t`) | 4 | unconditional | filesystem.c:12928 |
| `op_delete_slot_last_phase` (`uint8_t`) | 1 | unconditional | filesystem.c:12929 |
| `op_bank_save_entry_stall_ticks` (`uint32_t`) | 4 | unconditional | filesystem.c:13500 |
| `op_bank_save_entry_last_phase` (`uint8_t`) | 1 | unconditional | filesystem.c:13499 |
| `op_autosave_drain_stall_ticks` (`uint32_t`) | 4 | unconditional | filesystem.c:5592 |
| `op_autosave_drain_last_phase` (`uint8_t`) | 1 | unconditional | filesystem.c:5591 |
| `op_instrument_save_content_crc` (`uint32_t`) | 4 | unconditional | filesystem.c:1321 |
| `afatfsDeleteTree_t.failureSite` (`uint8_t`, new field in the existing persistent `afatfs.deleteTreeState` singleton) | 1 | unconditional | asyncfatfs.c (struct, ~line 420-452 area) |
| **Unconditional plain-static total** | **22 bytes** | always present | — |
| **Ring expansion total** | **+15,872 bytes** | `DEV_MODE_LOGGING` only | — |

The stall-observer pairs (`*_last_phase`/`*_stall_ticks`) and the delete-slot
reason/detail/site fields are plain file-scope statics, not trace-ring
storage — they persist across ticks so `filesystem_pollPhaseStall()` and the
delete-slot resolver can detect state that doesn't change, and so a later
`'O'` DELETE_RESULT or `'E'` OPERATION_ERROR record can report *why* a
failure happened, not just that it happened. They cost the same whether or
not `DEV_MODE_LOGGING` is on, since none of them depend on the trace ring to
have a reason.

Last confirmed clean build after all hooks + the 5 minor-finding fixes:
`text=381,892 data=400 bss=94,624` (`make -j2`, no new warnings).

---

## 3. `filesystem_pollPhaseStall()` — shared stall-edge detector

**File**: [`Core/Hardware/SD/filesystem.c`](Core/Hardware/SD/filesystem.c),
declared (forward) at line 1126, defined at line 12856.

```c
static uint8_t filesystem_pollPhaseStall(uint8_t phase,
                                          uint8_t *last_phase,
                                          uint32_t *stall_ticks,
                                          uint32_t threshold_ticks)
{
    if (phase != *last_phase) {
        *last_phase = phase;
        *stall_ticks = 0u;
        return 0u;
    }
    (*stall_ticks)++;
    return (uint8_t)(*stall_ticks == threshold_ticks + 1u);
}
```

**What it does**: edge-triggered "this cooperative state machine's phase has
not advanced in N polls" detector. Returns `1` exactly once per stall episode
(the tick where `stall_ticks` crosses `threshold_ticks`), not on every poll
thereafter — callers don't need their own debounce.

**Why it exists**: the project has zero blocking I/O in the main loop, so a
genuinely wedged async operation (a lost callback, a card that stops
responding) previously just sat there forever with no evidence. This gives
every long-running tick function an inexpensive, allocation-free way to
notice and report "I have been on the same phase for far too long" without
introducing any actual timeout/retry/abort logic of its own — it is purely an
observer unless a caller chooses to act on its return value (only one caller
does; see 3.3 below).

**Cost**: no SRAM of its own — callers each supply their own `last_phase`/
`stall_ticks` pair (accounted in §2).

### 3.1 Delete-slot resolver (site `DELETE_SLOT` = 0)

Caller: `filesystem_deleteSlotDirectory_tick()`, filesystem.c:12930-12935.
Threshold: 50,000 polls. State: `op_delete_slot_last_phase` /
`op_delete_slot_stall_ticks` (filesystem.c:12928-12929).

Reset: implicitly safe rather than explicitly zeroed at operation start —
unlike the other two sites, this one predates the "unreachable sentinel"
reset pattern. Its start phase (`FS_DELETE_SLOT_OPEN_SCAN` = 1) rarely
collides with a stale terminal phase value left over from the previous
delete, so no `0xffu` reset was added; this is a known minor asymmetry, not a
bug (no stale-collision false-stall has been observed in any retest).

On stall: reads `afatfs_getDeleteTreePhase()` for the native delete's own
subphase (valid only while `op_delete_slot_phase ==
FS_DELETE_SLOT_DELETE_MATCH`), packs phase/slot/subphase, and — this is the
one site where a stall still ends the operation — sets
`op_delete_slot_timeout_observed = 1u`, which the resolver's later phase
checks read as an abandonment signal (see §11 for why this no longer *also*
forces a hard error by itself; that was Bug #1).

Record: `AUTOSAVE_TRACE_STAGE_PHASE_STALL` (`'X'`), flags carry the site
(`AUTOSAVE_TRACE_PHASE_STALL_SITE_DELETE_SLOT` = 0, bits 0-2) and, if in
native delete, `AUTOSAVE_TRACE_PHASE_STALL_FLAG_IN_NATIVE_DELETE` (bit 3).
`value32` packs: phase (bits 0-7, shift 0), slot (bits 8-17, shift 8), and —
only when the native-delete flag is set — the asyncfatfs subphase (shift 18).

### 3.2 Bank Save entry/metadata phases (site `BANK_ENTRY` = 1)

Caller: `filesystem_saveBankDirectory_tick()`, filesystem.c:13520-13522.
Threshold: 20,000 polls. State: `op_bank_save_entry_last_phase` /
`op_bank_save_entry_stall_ticks` (filesystem.c:13499-13500). Reset to
`0xffu`/`0u` inside `filesystem_requestSaveBank()` at filesystem.c:21299-21300
(the fix for review finding #3 — without this reset, a stale phase value from
a *previous* Bank Save could produce a spurious first-poll stall report on
the next one).

Scope: deliberately observes only Bank Save's own non-payload phases. The
function's very first lines (`if (op_bank_payload_active) {
filesystem_saveSceneDirectory_tick(); return; }`) delegate to the Scene payload
loop *before* the stall poll runs, so a slow child-Scene write is never
misattributed to Bank-entry stalling.

Record: `'X'`, site = `AUTOSAVE_TRACE_PHASE_STALL_SITE_BANK_ENTRY` (1),
`value32` = phase (shift 0) | slot (shift 8). Purely diagnostic — does not
force completion.

### 3.3 AutoSave parameter drain (site `DRAIN` = 2) — the one exception

Caller: `filesystem_autosaveParameterDrain_tick()`, filesystem.c:5608-5619.
Threshold: 30,000 polls. State: `op_autosave_drain_last_phase` /
`op_autosave_drain_stall_ticks` (filesystem.c:5591-5592). Reset to `0xffu`/`0u`
inside the `filesystem_start(FS_INTERNAL_OP_AUTOSAVE_PARAMETER_DRAIN, ...)`
admission block (filesystem.c:19777-19778).

This is the only one of the three call sites where a stall previously had
**no bounded escape at all** — unlike delete-slot and Bank-entry, which are
user-initiated and already had (or gained) failure paths, an AutoSave drain
that wedges would otherwise hang the writer state machine indefinitely with
no way to ever produce a completion. So this site does more than observe: on
stall it also calls `filesystem_autosaveWriterFinishError()`, forcing a real
`FS_STATUS_ERROR` completion so the state machine can recover instead of
hanging forever.

Record: `'X'`, site = `AUTOSAVE_TRACE_PHASE_STALL_SITE_DRAIN` (2), `value32`
= phase (shift 0) | `(op_autosave_writer.stream_offset >> 4u)` packed at the
`EXTRA` shift (18) — the shifted stream offset gives a coarse read on how far
the drain got before wedging.

---

## 4. `fs_delete_slot_reason_t` — delete-slot resolver failure classification

**File**: filesystem.c, enum declared ~line 640-668, alongside the other
delete-slot statics.

```c
typedef enum {
    FS_DELETE_SLOT_REASON_NONE = 0u,
    FS_DELETE_SLOT_REASON_SCAN_IO,
    FS_DELETE_SLOT_REASON_MALFORMED_LFN,
    FS_DELETE_SLOT_REASON_WRONG_KIND,
    FS_DELETE_SLOT_REASON_DUPLICATE,
    FS_DELETE_SLOT_REASON_MATCH_COUNT_BACKSTOP,
    FS_DELETE_SLOT_REASON_DELETE_REJECTED,
    FS_DELETE_SLOT_REASON_DELETE_RESULT,
    FS_DELETE_SLOT_REASON_DIR_OPEN_FAILED,
    FS_DELETE_SLOT_REASON_STALL_ABANDONED
} fs_delete_slot_reason_t;
```

Plus two companion fields, set only when the reason is `DELETE_RESULT`:
`op_delete_slot_error_detail` (raw `afatfsResultCode_t` from the native
delete) and `op_delete_slot_error_site` (the new
`afatfsDeleteTreeFailureSite_e`, §5).

**What it's for**: `filesystem_deleteSlotDirectory_tick()` is the single
resolver behind every Kit/Scene overwrite-delete. Before this session, every
one of its failure branches collapsed to the same bare `FS_STATUS_ERROR` —
indistinguishable at the trace/error-code level. This enum exists so the one
`'O'` DELETE_RESULT record (and the universal `'E'` backstop) can say *which*
of nine architecturally different problems occurred instead of just "it
failed."

**Exhaustive tagging** (the direct response to "make sure ALL the possible
problematic save pathways get tagged"): every branch of
`filesystem_deleteSlotDirectory_tick()` that returns `FS_STATUS_ERROR` now
sets this field before returning. Verified branch-by-branch:

| Branch | Reason set | Location |
|---|---|---|
| Stall in `DELETE_MATCH`/`WAIT_SCAN`/`WAIT_CLOSE_SCAN` | `STALL_ABANDONED` | filesystem.c:12968 |
| `"."` open resolves to NULL handle (`FS_DELETE_SLOT_WAIT_SCAN`) | `DIR_OPEN_FAILED` | filesystem.c:12984 — **the confirmed root cause of the round-2 untagged (reason=NONE) evidence gap; see §12** |
| Scan I/O failure | `SCAN_IO` | filesystem.c:13062 |
| Match-eligibility: malformed LFN | `MALFORMED_LFN` | filesystem.c:13088 |
| Match-eligibility: wrong object kind | `WRONG_KIND` | filesystem.c:13092 |
| Match-eligibility: duplicate match | `DUPLICATE` | filesystem.c:13096 |
| Match-count backstop (only if no reason already set — provably unreachable given the three above, kept as a defensive net) | `MATCH_COUNT_BACKSTOP` | filesystem.c:13119-13125 |
| `afatfs_deleteTree()` start rejected | `DELETE_REJECTED` | filesystem.c:13147 |
| Native delete finished with a non-OK result | `DELETE_RESULT` (+ `error_detail` = raw result code, + `error_site` = `afatfsDeleteTreeFailureSite_e`) | filesystem.c:13159 |

The `MALFORMED_LFN`/`WRONG_KIND`/`DUPLICATE` split required refactoring what
was previously one combined `||` condition into separate `if`/`else if`
branches specifically so each could be tagged independently — this is a
behavior-preserving refactor (same net eligibility test, same outcome), done
purely to make the reason field meaningful.

Reset: all three fields (`error_reason`, `error_detail`, `error_site`) are
cleared to `NONE`/`0`/`0` only in `filesystem_deleteSlotDirectoryStart()`, so
they remain valid to read from the moment the resolver reports its one
`FS_STATUS_ERROR` until the next delete starts.

**Consumers**: the `'O'` DELETE_RESULT record (Kit Save case 5, Scene Save
case 5 — see §7.2) and both `'E'` OPERATION_ERROR hooks (§7.3), which each
report whether a delete-slot reason was set via
`AUTOSAVE_TRACE_OPERATION_ERROR_FLAG_DELETE_REASON_SET`.

---

## 5. `afatfsDeleteTreeFailureSite_e` — native delete-tree failure classification

**Files**: enum in
[`Core/Hardware/SD/asyncfatfs/asyncfatfs.c`](Core/Hardware/SD/asyncfatfs/asyncfatfs.c)
(~line 484-510); accessor declared in
[`asyncfatfs.h`](Core/Hardware/SD/asyncfatfs/asyncfatfs.h) at line 510,
defined in asyncfatfs.c at line 6435; storage field added to the existing
`afatfsDeleteTree_t` struct.

**Why it exists**: `afatfs_deleteTreeContinue()` — the native, low-level
recursive-delete state machine that Betaflight's AsyncFATFS never actually
exercised recursively — has roughly seventeen independent structural checks
that all fail the same way: `afatfs_deleteTreeFinish(file,
AFATFS_RESULT_UNSUPPORTED_LAYOUT)`. Before this session,
`op_delete_slot_error_detail` could tell you the native call returned
`AFATFS_RESULT_UNSUPPORTED_LAYOUT`, but not *which* of the seventeen checks
produced it — the single most important piece of missing information for
root-causing `ScnS05`.

```c
typedef enum {
    AFATFS_DELETE_TREE_FAILURE_SITE_NONE = 0u,
    AFATFS_DELETE_TREE_FAILURE_SITE_OPEN_DIR_BAD_ROOT_ON_FAT32,
    AFATFS_DELETE_TREE_FAILURE_SITE_OPEN_DIR_CLUSTER_OUT_OF_RANGE,
    AFATFS_DELETE_TREE_FAILURE_SITE_SCAN_ROOT_CLUSTER_MISMATCH,
    AFATFS_DELETE_TREE_FAILURE_SITE_SCAN_MALFORMED_OBJECT,
    AFATFS_DELETE_TREE_FAILURE_SITE_SCAN_CHILD_CLUSTER_OUT_OF_RANGE,
    AFATFS_DELETE_TREE_FAILURE_SITE_SCAN_STRUCTURAL_BUDGET_EXHAUSTED_DESCEND,
    AFATFS_DELETE_TREE_FAILURE_SITE_ASCEND_BAD_DOTDOT_ENTRY,
    AFATFS_DELETE_TREE_FAILURE_SITE_ASCEND_PARENT_CLUSTER_OUT_OF_RANGE,
    AFATFS_DELETE_TREE_FAILURE_SITE_ASCEND_PARENT_CLUSTER_MISMATCH,
    AFATFS_DELETE_TREE_FAILURE_SITE_SCAN_PARENT_BAD_ROOT_ON_FAT32,
    AFATFS_DELETE_TREE_FAILURE_SITE_SCAN_PARENT_FOR_SELF_EXHAUSTED,
    AFATFS_DELETE_TREE_FAILURE_SITE_SCAN_PARENT_FOR_SELF_MATCH_CLUSTER_OUT_OF_RANGE,
    AFATFS_DELETE_TREE_FAILURE_SITE_RETIRE_ENTRIES_ROOT_CLUSTER_MISMATCH,
    AFATFS_DELETE_TREE_FAILURE_SITE_FREE_CLUSTERS_NONFILE_NONZERO_SIZE,
    AFATFS_DELETE_TREE_FAILURE_SITE_FREE_CLUSTERS_CLUSTER_OUT_OF_RANGE_OR_BUDGET,
    AFATFS_DELETE_TREE_FAILURE_SITE_FREE_CLUSTERS_NEXT_CLUSTER_INVALID,
    AFATFS_DELETE_TREE_FAILURE_SITE_CORRUPT_PHASE
} afatfsDeleteTreeFailureSite_e;
```

18 values total (`NONE` + 17 real sites). Storage: `uint8_t failureSite`
added to `afatfsDeleteTree_t`, which lives in the persistent
`afatfs.deleteTreeState` singleton — deliberately **not** in a per-handle
`openFiles[]` slot, so it survives `afatfs_deleteTreeFinish()`'s handle reset
and is still readable from the callback filesystem.c receives after the
operation completes.

**Accessor**:
```c
uint8_t afatfs_getDeleteTreeFailureSite(void)
{
    return afatfs.deleteTreeState.failureSite;
}
```
Unlike `afatfs_getDeleteTreePhase()` (which scans `openFiles[]` for a live
handle and is only meaningful *during* the operation), this reads the
persistent singleton directly and is documented as safe to call *after* the
operation's callback has already run — exactly when filesystem.c actually
wants it (at `FS_DELETE_SLOT_REASON_DELETE_RESULT` tagging time,
filesystem.c:13159, after the handle is already gone).

**Coverage**: every one of the 17 real `AFATFS_RESULT_UNSUPPORTED_LAYOUT`
call sites inside `afatfs_deleteTreeContinue()` (asyncfatfs.c lines
6526-6958) sets `op->failureSite = AFATFS_DELETE_TREE_FAILURE_SITE_...;`
immediately before calling `afatfs_deleteTreeFinish(file,
AFATFS_RESULT_UNSUPPORTED_LAYOUT)`. This was exhaustively cross-checked (a
one-off Python scan, not a committed test) confirming every `UNSUPPORTED_LAYOUT`
result is preceded by a `failureSite` assignment within a few lines, with the
lone false positive being a comment mentioning the term. The `default:` case
is tagged `CORRUPT_PHASE` (defensive; not expected reachable).

`AFATFS_RESULT_IO_ERROR` and `AFATFS_RESULT_CORRUPT_LFN_RUN` sites were
deliberately left untagged — only `UNSUPPORTED_LAYOUT` is overloaded across
many structurally distinct checks; the other two result codes are already
unambiguous on their own.

**The two flagged leading hypotheses** (documented in-code, cross-referenced
with each other, at asyncfatfs.c:6708-6803) — **superseded**: Round 4 (§6, §12)
found the actual `ScnS05` failure was a third, unrelated site
(`OPEN_DIR_BAD_ROOT_ON_FAT32`, via a bug in `AFATFS_DELETE_TREE_RESUME_PARENT`,
now fixed). Both hypotheses below remain legitimate, still-instrumented
failure modes for their own scenarios — a genuine `..`-entry mismatch or an
exhausted re-find would still be caught and correctly attributed — they
simply weren't what this particular evidence turned out to be:

- `ASCEND_PARENT_CLUSTER_MISMATCH` (asyncfatfs.c:6727) — fires in
  `AFATFS_DELETE_TREE_EMPTY_DIR_ASCEND` when a child directory's own `..`
  entry disagrees with the parent cluster recorded at descent time.
- `SCAN_PARENT_FOR_SELF_EXHAUSTED` (asyncfatfs.c:6803) — fires in
  `AFATFS_DELETE_TREE_SCAN_PARENT_FOR_SELF_LOOP` when re-scanning the
  recovered parent never re-finds the child by its exact saved short-filename
  pointer + cluster.

Both are associated with the specific evidence already gathered (Scene slot
11's nested `Kit FilMod/` subdirectory forcing the traversal's
descend/ascend logic) — see §12 for the exact prior evidence and what is
still outstanding.

`SCAN_MALFORMED_OBJECT` (asyncfatfs.c:6610) is guarded to fire only when
`!object.lfnMalformed`, since that flag can instead route to
`AFATFS_RESULT_CORRUPT_LFN_RUN`, a different result code this instrumentation
must not mislabel.

---

## 6. Bugs #3 and #4 (both fixed) — and why the `'Y'` probe is now retired

Round 4's card retest (using §5's freshly-completed 17-site instrumentation)
produced clean evidence: `failure site=OPEN_DIR_BAD_ROOT_ON_FAT32` (target's
`firstCluster==0` on a FAT32 card), at Scene slot 2. This was neither of §5's
two documented leading hypotheses — it fires at the very first step of a
directory open, before any tree-walking check, meaning the target identity
handed in was itself invalid.

**Root cause**: `AFATFS_DELETE_TREE_RESUME_PARENT` in asyncfatfs.c (the phase
reached after deleting a plain file, or after retiring an emptied,
ascended-from child directory's own entry) reconstructed its next scan
target from `op->parentCluster` and re-entered through
`AFATFS_DELETE_TREE_OPEN_DIR`. `op->parentCluster` is only ever assigned by a
prior *descend into* or *ascend from* a subdirectory — so when the very
first object a traversal's root-level scan encounters is a plain file rather
than a subdirectory (e.g. Scene slot 2's `pattern.pat`/`sceneset.scg`
sorting before its `Kit Hard` subdirectory), no descend/ascend has run yet,
`op->parentCluster` is still its zero-initialized default, and the
reconstructed target gets `firstCluster=0` — which `OPEN_DIR` then rejects
as an invalid FAT16-root reference on this FAT32 card. A tree where a
subdirectory happened to be scanned first would "work" by accident, which is
why three prior rounds of otherwise-clean evidence never surfaced it.

**Fix** (asyncfatfs.c, `AFATFS_DELETE_TREE_RESUME_PARENT`, ~line 6931):
the phase no longer reconstructs a target or reopens via `OPEN_DIR`. `file`
is already correctly bound to the directory that needs resuming in both
paths that reach this phase — after deleting a plain file it was never
rebound away from that directory; after retiring an ascended, now-empty
child's own entry, it's the parent `AFATFS_DELETE_TREE_SCAN_PARENT_FOR_SELF`
already reopened. The fix resets the scan cursor on that already-open handle
in place (mirroring `OPEN_DIR`'s own reset sequence, minus the now-
unnecessary reconstruction) and transitions straight to
`AFATFS_DELETE_TREE_SCAN`, never touching `op->parentCluster` or
`op->currentTarget` for this transition. `op->parentCluster` keeps its two
original, legitimate uses (recording the ascend target; cross-checking
against a child's own `..` entry) — only its incorrect third use as a
resume-target source was removed.

**Confirmed fixed**: Round 5's retest hit a different failure site entirely
(`SCAN_PARENT_FOR_SELF_EXHAUSTED`, below) — `OPEN_DIR_BAD_ROOT_ON_FAT32` did
not recur.

One pre-existing minor dead-store surfaced by this fix, left as-is (out of
scope for this fix): `op->parentEntry` is now written (at descend,
asyncfatfs.c ~line 6660) but never read anywhere — its one reader was the
code this fix removed. Harmless (a struct field, not a local, so no compiler
warning), and a candidate for later removal.

### The `'Y'` `SCAN_PARENT_DIAG` probe

Round 5's retest (Scene slot 6, `006 Brezel`, containing an empty `Kit
Brezel` subdirectory and a `pattern.pat` file) produced
`failure site=SCAN_PARENT_FOR_SELF_EXHAUSTED` — one of §5's two original
leading hypotheses, but the bare failure site alone cannot distinguish two
very different underlying problems: the re-scanned parent looking
*completely empty* (suggesting the reopened parent itself is wrong,
unreadable, or was mutated) versus the re-scan finding *other* objects but
never re-matching the expected child's exact identity (suggesting the
target's own directory entry changed, moved, or was never matched in the
first place). Rather than guess between these, a minimal, targeted probe was
added.

**Implementation**:
- `afatfsDeleteTree_t` (asyncfatfs.c, the persistent `afatfs.deleteTreeState`
  singleton) gained `uint8_t scanParentForSelfSeenCount;` — a saturating
  counter, reset to `0` each time `AFATFS_DELETE_TREE_SCAN_PARENT_FOR_SELF`
  starts a fresh re-scan, incremented once per non-NONE, non-malformed
  candidate `AFATFS_DELETE_TREE_SCAN_PARENT_FOR_SELF_LOOP` examines
  (asyncfatfs.c, immediately after the `lfnMalformed` check, before the
  identity-match test).
- Two new accessors, mirroring `afatfs_getDeleteTreeFailureSite()`'s
  post-callback-safe, persistent-singleton read pattern:
  `uint8_t afatfs_getDeleteTreeScanParentForSelfSeenCount(void)` and
  `uint32_t afatfs_getDeleteTreeTargetClusterToRetire(void)` (the latter
  exposing the existing `targetClusterToRetire` field, so the probe can
  report *which* cluster the failed re-scan was actually looking for).
  Declared in asyncfatfs.h beside `afatfs_getDeleteTreeFailureSite()`.
- `afatfsDeleteTreeFailureSite_e` itself was moved from asyncfatfs.c to
  asyncfatfs.h (unchanged in every other respect — same 18 values, same
  declaration order) so filesystem.c could compare
  `op_delete_slot_error_site` against
  `AFATFS_DELETE_TREE_FAILURE_SITE_SCAN_PARENT_FOR_SELF_EXHAUSTED` by name
  instead of an undocumented magic number.
- filesystem.c (`filesystem_deleteSlotDirectory_tick()`,
  `FS_DELETE_SLOT_DELETE_MATCH`, immediately after capturing
  `op_delete_slot_error_site`): when that site is specifically
  `SCAN_PARENT_FOR_SELF_EXHAUSTED`, emits one new `'Y'`
  `AUTOSAVE_TRACE_STAGE_SCAN_PARENT_DIAG` record — `flags` is the raw
  seen-count byte; `value32` packs the numbered slot (bits 0-15) and the
  target cluster's low 16 bits (bits 16-31,
  `AUTOSAVE_TRACE_SCAN_PARENT_DIAG_SLOT_SHIFT`/`_CLUSTER_SHIFT` in
  AutosaveTrace.h). Both new accessors are read here, before the next
  delete request's own setup resets the singleton — same timing rationale
  as every other post-completion read of `deleteTreeState` in this file.
- `tools/decode_devlogs.py`: new `"Y"` entries in `STAGE_ENUM`/
  `STAGE_PRODUCER` and a dedicated decode branch reporting slot, target
  cluster, candidate count, and an explicit "(parent looked completely
  empty)" annotation when the count is `0`. Verified via a synthetic-record
  round-trip.

**Cost**: 1 new byte in the always-present `afatfsDeleteTree_t` singleton
(no `DEV_MODE_LOGGING` gating on the struct field itself, matching
`failureSite`'s existing precedent); the `'Y'` trace record itself is
ring-storage, gated like every other record. Rebuilt clean:
`text=381,996 data=400 bss=94,624` (+40 bytes text over the Bug #3 fix, no
new statics beyond the 1-byte field, no new warnings).

**Round 6's retest** (Scene slot 7, `007 Chip`, containing `Kit Chip`
(empty), `pattern.pat`, and `sceneset.scg` — three children total) produced
`candidates examined before giving up=3`, exactly matching `007 Chip`'s full
child count. That rules out "the reopened parent looked empty" — the
re-scan reached every child, including `Kit Chip` itself, and still never
matched it. This pointed squarely at the identity-match predicate itself
(`object.id.kind == AFATFS_OBJECT_DIRECTORY && sfnEntry-equals(target) &&
firstCluster == target`) rather than at scan scope, so the probe was
extended one more notch to show *which half* of that predicate fails for
the last directory-kind candidate examined:

- `afatfsDeleteTree_t` gained `uint8_t scanParentForSelfLastDirCandidateFlags;`
  — bit 0 (`AFATFS_SCAN_PARENT_FOR_SELF_CANDIDATE_SFN_MATCH`) and bit 1
  (`_CLUSTER_MATCH`), evaluated independently of the real match test for
  every `AFATFS_OBJECT_DIRECTORY` candidate seen, overwritten (not
  accumulated) each time — so at `EXHAUSTED` time it reflects the *last*
  directory candidate, which in every card evidence gathered so far is the
  only subdirectory (there is exactly one Kit-embedding per Scene slot).
- New accessor `afatfs_getDeleteTreeScanParentForSelfLastDirCandidateFlags(void)`,
  same post-callback-safe pattern as the others.
- The `'Y'` record's `value32` now also packs these two bits at
  `AUTOSAVE_TRACE_SCAN_PARENT_DIAG_SFN_MATCH`/`_CLUSTER_MATCH` (bits 14-15,
  chosen because the numbered-slot field only ever uses bits 0-9 for real
  slot values 0-999, so this costs nothing against the existing layout).
- `tools/decode_devlogs.py`: reports one of four outcomes — SFN matched but
  cluster didn't (the same directory-entry slot now reports a different
  `firstCluster`), cluster matched but SFN didn't (the target cluster
  reappeared under a different directory-entry pointer), neither matched
  (or no directory-kind candidate was seen at all), or — flagged as
  unexpected, since a full match should retire immediately rather than
  reach `EXHAUSTED` — both matched. Verified via four synthetic records,
  one per outcome.

Rebuilt clean: `text=382,004 data=400 bss=94,624` (+8 bytes, no new
statics beyond the 1-byte field, no new warnings).

### Bug #4 (fixed): the re-scan itself was the bug — eliminated, not diagnosed

Round 6's evidence (candidates=3, matching `007 Chip`'s full child count)
proved the re-scan reached every child, including `Kit Chip` itself, and
still never matched it — ruling out scan scope and pointing at the
identity-match predicate. Rather than extend the probe again to isolate
*why* the predicate disagreed with itself, the code was re-read end to end
to check whether the re-scan was even necessary in the first place.

It wasn't. `afatfs_retireObjectNameRun()` (used by
`AFATFS_DELETE_TREE_RETIRE_ENTRIES` to actually delete a name) takes only an
`afatfsObjectInfo_t` and operates entirely via `afatfs_cacheSector()` on
that object's own stored physical sector/entry pointers — it has no
dependency on any directory being "open" as the object's parent. And
`op->currentTarget` — the object identity captured the moment
`AFATFS_DELETE_TREE_SCAN` originally discovers a child (`Kit Chip`, in this
evidence) — is never overwritten between that discovery and the ascend that
follows finding it empty: `AFATFS_DELETE_TREE_SCAN`'s "found nothing, and
I'm not root" branch transitions straight to
`AFATFS_DELETE_TREE_EMPTY_DIR_ASCEND` without touching `currentTarget`. So
by the time `AFATFS_DELETE_TREE_SCAN_PARENT_FOR_SELF_LOOP` went searching
for an object matching `Kit Chip`'s identity, the traversal already *had*
that identity, complete and untouched, sitting in `op->currentTarget` —
the search was re-deriving something already known, through a second,
independent path (a fresh directory re-scan) that could disagree with the
first for reasons never fully isolated. Eliminating the second path removes
the disagreement instead of chasing it.

**Fix**: `AFATFS_DELETE_TREE_SCAN_PARENT_FOR_SELF` and
`AFATFS_DELETE_TREE_SCAN_PARENT_FOR_SELF_LOOP` are gone, replaced by a
single `AFATFS_DELETE_TREE_REOPEN_PARENT` phase that rebinds `file` to
`op->parentCluster` by cluster identity only (the exact same reopen logic
the old phase used, minus the search that followed it), sets
`op->currentCluster = op->currentTarget.id.firstCluster` directly, and goes
straight to `AFATFS_DELETE_TREE_FREE_FILE_CLUSTERS` — which still performs
its own cluster-range validation on entry, so no safety check is lost, only
the redundant, apparently-unreliable re-verification. `EMPTY_DIR_ASCEND` no
longer captures `targetClusterToRetire`/`targetEntryToRetire` (both
removed, along with the now-dead `parentEntry` write inside `DESCEND`,
which lost its only reader when the search was deleted) since
`op->currentTarget` already carries everything needed.

Because the search is gone, `AFATFS_DELETE_TREE_FAILURE_SITE_SCAN_PARENT_FOR_SELF_EXHAUSTED`
and `..._MATCH_CLUSTER_OUT_OF_RANGE` are now permanently unreachable (kept
declared in the enum, per this file's own "append-only" convention, only so
already-captured evidence naming them stays decodable — see asyncfatfs.h).
The `'Y'` `SCAN_PARENT_DIAG` probe this section originally documented is
retired for the same reason: its only producer no longer exists. Its struct
fields, both accessors, and the filesystem.c emission call site are all
removed; only the trace-format documentation in AutosaveTrace.h and the
decoder branch in `tools/decode_devlogs.py` remain, so any `asavetrc.bin`
already captured during rounds 5/6 (before this fix) still decodes.

**Cost**: net *negative* — this fix removes more than it adds. Rebuilt
clean: `text=381,100 data=400 bss=94,600` (**-904 bytes text, -24 bytes
bss** versus the pre-Bug-#4 build), no new warnings.

Round 7 then reported `FREE_CLUSTERS_NEXT_CLUSTER_INVALID` — see Bug #5
below, which found that **the premise stated in this section was wrong** and
that Bugs #3 and #4 had each broken one half of the ascend path. Bug #5 is
the actual repair.

---

## 6a. Bug #5 (the real defect): the ascend path lost track of *which* directory it was leaving

Round 7's evidence: `reason=DELETE_RESULT`,
`detail=AFATFS_RESULT_UNSUPPORTED_LAYOUT`,
**`failure site=FREE_CLUSTERS_NEXT_CLUSTER_INVALID`**, Scene slot 12
(`012 Emott` — containing `Kit Emott/`, `effects.fx`, `pattern.pat`,
`sceneset.scg`). That site fires when the FAT link being followed reads as
**free space** — i.e. the traversal was freeing a cluster chain that had
*already* been freed.

Re-reading the traversal against that clue showed the Bug #3 and Bug #4
fixes had each removed one half of a single invariant, and that the `'Y'`
probe results from rounds 5/6 were **collateral damage from Bug #3's fix,
not independent findings**. Both halves are now repaired.

### The invariant

`file->directoryEntryPos` is the traversal's record of **which directory the
`file` handle is currently open on**. `AFATFS_DELETE_TREE_OPEN_DIR` writes it
from `op->currentTarget.id.sfnEntry`. Its critical consumer is
`AFATFS_DELETE_TREE_SCAN`'s "directory is now empty" branch
(asyncfatfs.c:6551):

```c
if (afatfs_entryPointerEquals(&file->directoryEntryPos, &op->root.id.sfnEntry)) {
    /* finished the whole tree: retire the delete root, then SUCCESS */
} else {
    /* merely emptied a nested child: ascend */
}
```

If that field does not identify the directory actually being scanned, an
emptied **delete root** is misread as a nested child and the traversal
ascends *out of the tree it was asked to delete*.

### Half one — `RESUME_PARENT` erased it (introduced by Bug #3's fix)

Bug #3's fix correctly stopped `AFATFS_DELETE_TREE_RESUME_PARENT` from
reconstructing a target out of `op->parentCluster`, but it copied
`OPEN_DIR`'s reset sequence wholesale — including the two lines that clear
`file->directoryEntryPos`. `RESUME_PARENT` only *rewinds a scan of the
directory already open*; it never rebinds the handle, so clearing that field
destroyed the only record of which directory was being scanned. Consequences:

- an emptied delete root took the ascend branch instead of the retire branch;
- the pointer `EMPTY_DIR_ASCEND` captured as the child's identity
  (`targetEntryToRetire`, in the code as it stood in rounds 5/6) became a
  zeroed placeholder — which is **exactly** why round 6's re-scan examined
  all 3 of `007 Chip`'s children and matched none of them. The `'Y'` probe's
  "candidates=3, no match" was reporting a corrupted search key, not a
  disagreement between two sources of truth.

**Fix**: `RESUME_PARENT` no longer touches `file->directoryEntryPos`; it
resets only the cursor fields a rewind actually requires.

### Half two — `REOPEN_PARENT` used a stale identity (introduced by Bug #4's fix)

Bug #4's fix removed the parent re-scan and took the ascended-from child's
identity straight from `op->currentTarget`, on the stated premise that
"nothing between that discovery and this ascend ever touches it." **That
premise is false.** `op->currentTarget` is the *object currently being
deleted* register, and `AFATFS_DELETE_TREE_SCAN` rewrites it for every object
it processes. It survives the descent only when the child directory was
**already empty when discovered** — which is why the earlier fixtures
appeared to work and why the defect surfaced only on a slot whose Kit
subdirectory still had files.

For `012 Emott` the real sequence was:

1. `SCAN` finds `Kit Emott` → `currentTarget = Kit Emott` → descend.
2. `SCAN` inside `Kit Emott` finds its files and deletes them one by one,
   overwriting `currentTarget` each time. After the last one,
   `currentTarget` = that file, **whose clusters were just freed**.
3. `Kit Emott` is now empty → `EMPTY_DIR_ASCEND` → `REOPEN_PARENT`.
4. `REOPEN_PARENT` sets `op->currentCluster = op->currentTarget.id.firstCluster`
   — the already-freed file's chain — and `FREE_FILE_CLUSTERS` walks it into
   a FAT entry that now reads `0` → **`FREE_CLUSTERS_NEXT_CLUSTER_INVALID`**.

(The residue confirms it: `012 Emott/Kit Emott` is empty on the returned
card, exactly as `006 Brezel/Kit Brezel` and `007 Chip/Kit Chip` were emptied
by rounds 5 and 6 — each failed run deletes the Kit subdirectory's contents
and then dies on the ascend.)

The ascend needs two things the descent destroys: the child's **cluster** and
its **complete VFAT name run** (`lfnEntryCount`/`lfnFirstEntry`/
`lfnFollowingEntry`, so `afatfs_retireObjectNameRun()` retires every fragment
rather than orphaning them — `file->directoryEntryPos` carries only the short
entry). The original code recovered these by re-scanning the parent; Bug #4
removed that without providing a replacement source.

**Fix**: snapshot them at the one moment they are known-good — the descent
itself. Two new fields in `afatfsDeleteTree_t`:

| Field | Type | Captured at | Restored by |
|---|---|---|---|
| `descendTarget` | `afatfsObjectInfo_t` | `SCAN`'s descend branch, from `op->currentTarget` | `REOPEN_PARENT` → `op->currentTarget` |
| `parentEntry` | `afatfsDirEntryPointer_t` | `SCAN`'s descend branch, from `file->directoryEntryPos` | `REOPEN_PARENT` → `file->directoryEntryPos` |

`REOPEN_PARENT` now restores `file->directoryEntryPos` from `op->root.id.sfnEntry`
when the parent *is* the delete root (so the root is still recognizable once
its scan empties) and from `op->parentEntry` otherwise, then restores
`op->currentTarget = op->descendTarget` before handing off to
`FREE_FILE_CLUSTERS`/`RETIRE_ENTRIES`. No search, no extra card I/O.

`parentEntry` re-introduces the field Bug #4 deleted as "dead" — it was dead
only because the code that needed it had just been removed.

### Depth bound (unchanged, not a regression)

`descendTarget`/`parentEntry` hold one level: a descend nested inside another
descend overwrites them. That is the **same** depth-one bound `op->parentCluster`
already enforces — at two levels of nesting, the ascend's `..`-agreement check
compares the inner parent's cluster against the outer one and raises
`ASCEND_PARENT_CLUSTER_MISMATCH`. The pre-session code had this bound too. It
is not exercised: `afatfs_deleteTree()` has exactly one caller
(`filesystem_deleteSlotDirectory_tick()`, filesystem.c), invoked only on a Kit
slot (files only) or a Scene slot (files plus one `Kit …/` subdirectory) — a
maximum of one nested directory below the delete root.

### Verified traversal (Scene slot with a populated Kit subdirectory)

`SCAN` root → descend into Kit (**save both**) → delete Kit's files
(`RESUME_PARENT` rewinds in place, `directoryEntryPos` **preserved**) → Kit
empty → `EMPTY_DIR_ASCEND` (`..` agrees) → `REOPEN_PARENT` (**restore both**)
→ free Kit's chain → retire Kit's full name run → `RESUME_PARENT` → delete
root's remaining files → root empty → `directoryEntryPos == root.sfnEntry`
→ **retire root → SUCCESS**. The Kit-slot case (no subdirectory) reaches the
same terminal branch directly, and Bug #3's original failure (first object
under the root being a plain file) remains fixed.

**Cost**: `text=381,140 data=400 bss=94,736` — +40 bytes text, +136 bytes bss
(`descendTarget` ≈128, `parentEntry` 8) versus the Bug #4 build. No new
warnings. **Not yet retested on card** — this is the current open thread (§12).

---

## 7. `'O'` `AUTOSAVE_TRACE_STAGE_SAVE_LIFECYCLE` — Save lifecycle checkpoints

**Header**: AutosaveTrace.h:92-177 (stage value `'O'`, five checkpoints, four
save types).

```c
#define AUTOSAVE_TRACE_SAVE_LIFECYCLE_TYPE_MASK 0x03u
#define AUTOSAVE_TRACE_SAVE_LIFECYCLE_TYPE_KIT 0u
#define AUTOSAVE_TRACE_SAVE_LIFECYCLE_TYPE_SCENE 1u
#define AUTOSAVE_TRACE_SAVE_LIFECYCLE_TYPE_BANK 2u
#define AUTOSAVE_TRACE_SAVE_LIFECYCLE_TYPE_INSTRUMENT 3u
#define AUTOSAVE_TRACE_SAVE_LIFECYCLE_CHECKPOINT_SHIFT 2u
#define AUTOSAVE_TRACE_SAVE_LIFECYCLE_CHECKPOINT_REQUEST 0u
#define AUTOSAVE_TRACE_SAVE_LIFECYCLE_CHECKPOINT_DELETE_RESULT 1u
#define AUTOSAVE_TRACE_SAVE_LIFECYCLE_CHECKPOINT_CREATE_RESULT 2u
#define AUTOSAVE_TRACE_SAVE_LIFECYCLE_CHECKPOINT_SOURCE_STAGED 3u
#define AUTOSAVE_TRACE_SAVE_LIFECYCLE_CHECKPOINT_FINISH 4u
#define AUTOSAVE_TRACE_SAVE_LIFECYCLE_FLAG_FAILED (1u << 7u)
#define AUTOSAVE_TRACE_SAVE_LIFECYCLE_SLOT_SHIFT 0u
#define AUTOSAVE_TRACE_SAVE_LIFECYCLE_CRC16_SHIFT 16u
#define AUTOSAVE_TRACE_SAVE_LIFECYCLE_MENU_BRANCH_SHIFT 16u   /* reuses CRC16 bits, Menu-only */
#define AUTOSAVE_TRACE_SAVE_LIFECYCLE_DELETE_REASON_SHIFT 16u
#define AUTOSAVE_TRACE_SAVE_LIFECYCLE_DELETE_DETAIL_SHIFT 20u
#define AUTOSAVE_TRACE_SAVE_LIFECYCLE_DELETE_SITE_SHIFT   24u
```

`flags` byte = `(checkpoint << 2) | type`, with `FLAG_FAILED` (bit 7) set on
a failure record. `value32`'s upper bits are reused per checkpoint (documented
per-site below) to avoid growing the fixed 4-byte field.

**What it's for**: gives an ordered, per-save-type timeline of exactly which
stage a Save reached and whether it succeeded, independent of whatever
low-level reason a DELETE_RESULT or the `'E'` backstop also reports. A
REQUEST with no matching FINISH means the operation never got picked up by
the tick loop or was still in flight when the trace drained; a
DELETE_RESULT with `FLAG_FAILED` but no CREATE_RESULT means the failure was
in the pre-existing-slot cleanup, not the new write.

### 7.1 REQUEST — 6 call sites

Recorded the instant a Save is accepted, before any I/O starts.

| Save type | Function | Location |
|---|---|---|
| Kit | `filesystem_requestSaveKitDirectory()` | filesystem.c:21006-21011 |
| Scene | `filesystem_requestSaveSceneDirectory()` | filesystem.c:21070-21075 |
| Bank | `filesystem_requestSaveBank()` | filesystem.c:21341-21346 |
| Instrument (Save/Morph, not Temp) | the Instrument Save/Morph request path | filesystem.c:22165-22171 |
| Kit (Menu-side, 3 branches: invalid scratch, cache already Kit, cache reload requested) | `menu_requestKitEntryNames()`'s three cache-domain branches | menu.c:3816, 3832, 3855 |
| Bank (Menu-side bracket) | `menu_handleLoadSaveMenu()`, `SAVE_TYPE_BANK` case, immediately before `preset_saveBank()` | menu.c:6984-6990 |

The three Menu-side Kit REQUEST records at menu.c:3816/3832/3855 deliberately
reuse `value32`'s bits 16-31 as a **local Menu branch tag** (1/2/3, marking
which cache-domain decision fired), *not* the CRC16 field the same bit range
means everywhere else — this is ambiguous from the record alone without
knowing the call site, and is documented in-code and in
`tools/decode_devlogs.py`'s decoder as a special case.

### 7.2 DELETE_RESULT — 2 call sites (Kit, Scene)

Recorded once the delete-slot resolver clears `FS_STATUS_BUSY`, for the two
save types that can overwrite an existing numbered slot.

| Save type | Function/case | Location |
|---|---|---|
| Kit | `filesystem_saveKitDirectory_tick()` case 5 | filesystem.c:13264 |
| Scene | `filesystem_saveSceneDirectory_tick()` case 5 | filesystem.c:14156 |

`value32` = `slot << 0`; on failure, additionally packs
`op_delete_slot_error_reason` (bits 16-19), `op_delete_slot_error_detail`
(bits 20-23, raw `afatfsResultCode_t`), `op_delete_slot_error_site` (bits
24-31, `afatfsDeleteTreeFailureSite_e`) — this one record is what let the
round-3 retest identify `reason=DELETE_RESULT`,
`detail=AFATFS_RESULT_UNSUPPORTED_LAYOUT` (§12).

### 7.3 CREATE_RESULT — 3 call sites (Kit, Scene, Instrument)

Recorded once the fresh target directory/file materializes (or fails to).

| Save type | Function/case | Location |
|---|---|---|
| Kit | `filesystem_saveKitDirectory_tick()` case 9 | filesystem.c:13304 |
| Scene | `filesystem_saveSceneDirectory_tick()` case 9 | filesystem.c:14195 |
| Instrument (open failure) | Instrument Save tick, phase 17, `!op_file` | filesystem.c:11767 |
| Instrument (success, with content CRC) | same function, successful close path | filesystem.c:11819 |

Instrument's success-path record is the only CREATE_RESULT that also packs a
content fingerprint: `value32` bits 16-31 carry
`op_instrument_save_content_crc & 0xffff0000u` — the **top 16 bits of the
running, unfinished CRC32C accumulator**, not the
`autosave_recordCrcFinish()`-complemented ("finished") form used by the fixed
AutoSave record elsewhere in this file. This is deliberate: it is a
diagnostic-only fingerprint, meaningful only when compared against another
value produced the exact same way (a later Save/Load of the same slot),
never against a "real" finished CRC32C. See §7.5 for how the byte-level
accumulation is wired in.

Bank has no DELETE_RESULT/CREATE_RESULT checkpoints of its own: its per-child
payload writes delegate straight into `filesystem_saveSceneDirectory_tick()`
(the `op_bank_payload_active` branch at the top of
`filesystem_saveBankDirectory_tick()`, see §3.2), so those two checkpoints
fire under the `TYPE_SCENE` tag even during a Bank Save's child-write phase.
Bank's own two checkpoints are SOURCE_STAGED (root-folder provenance) and
FINISH only — §7.4/§7.5.

### 7.4 SOURCE_STAGED — 4 call sites (Kit, Scene, Bank, Instrument)

Recorded immediately after `filesystem_setResidentSource()` is called to
mark HCNAMES row provenance — this is the direct instrumentation for Bug #2
(§11).

| Save type | Location |
|---|---|
| Kit | filesystem.c:13467 |
| Scene | filesystem.c:14564 |
| Bank | filesystem.c:13971 (root-folder provenance staging, before phase 83) |
| Instrument | filesystem.c:11904 |

### 7.5 FINISH — 4 call sites (Kit, Scene, Bank, Instrument)

Recorded at the last point before (or as) the operation hands off to
completion, one per save type: filesystem.c:13484 (Kit),
filesystem.c:14571 (Scene), filesystem.c:14051 (Bank),
filesystem.c:11911 (Instrument).

### Instrument content CRC wiring (backs 7.3's success record)

- `op_instrument_save_content_crc` (filesystem.c:1321) reset via
  `autosave_recordCrcBegin()` at Instrument Save phase entry
  (filesystem.c:11781).
- Accumulated byte-by-byte inside `filesystem_writeTextLine()`
  (filesystem.c:11999-12001), guarded so it only runs for
  `current_op == FS_INTERNAL_OP_SAVE_INSTRUMENT &&
  !op_instrument_save_temporary` — Temp saves and every other save type are
  untouched.
- New public helper added because filesystem.c cannot call the pre-existing
  `static uint32_t autosave_crc32cUpdate()` in Autosave.c:
  ```c
  // Autosave.h:526
  uint32_t autosave_crc32cByteUpdate(uint32_t crc32c, uint8_t value);
  // Autosave.c:376-380
  uint32_t autosave_crc32cByteUpdate(uint32_t crc32c, uint8_t value)
  {
      return autosave_crc32cUpdate(crc32c, value);
  }
  ```
  A one-line pass-through, not new CRC logic.

---

## 8. `'E'` `AUTOSAVE_TRACE_STAGE_OPERATION_ERROR` — universal error-completion witness

**Header**: AutosaveTrace.h:108-118 and the flag/shift block at
AutosaveTrace.h:199-203.

```c
#define AUTOSAVE_TRACE_OPERATION_ERROR_FLAG_DELETE_REASON_SET (1u << 0u)
#define AUTOSAVE_TRACE_OPERATION_ERROR_FLAG_INDEX_REBUILD     (1u << 1u)
#define AUTOSAVE_TRACE_OPERATION_ERROR_OP_SHIFT    0u
#define AUTOSAVE_TRACE_OPERATION_ERROR_PHASE_SHIFT 8u
#define AUTOSAVE_TRACE_OPERATION_ERROR_SLOT_SHIFT  16u
```

**Why this exists — the direct answer to "tag ALL the possible problematic
save pathways"**: this session's own retest cycle showed that hand-picking
individual failure branches to instrument reliably misses one — the
delete-slot resolver's `"."` open-failure path went completely untagged for
a full user round-trip (round 2, `reason=NONE`) before being found by
elimination. There is no way to enumerate, by hand, with confidence, every
current *and future* failure branch across every Load/Save/scan/HCNAMES/
index operation in a ~20,000-line facade. So instead of tagging branches,
this hooks the **shared terminal completion functions** — every internal
operation in filesystem.c ends by calling one of exactly two functions, and
both are now instrumented. Whatever fails, however it fails, whenever a
future change adds a new failure path nobody thought to specifically
instrument, this still fires — by construction, not by diligence.

### 8.1 Hook #1 — `filesystem_complete()`

filesystem.c:3233-3241, inside the primary operation-completion function
(fires for every `FS_INTERNAL_OP_*` that terminates through the normal
path):

```c
if (final_status == FS_STATUS_ERROR) {
    autosaveTrace_record(
        AUTOSAVE_TRACE_STAGE_OPERATION_ERROR,
        (op_delete_slot_error_reason != FS_DELETE_SLOT_REASON_NONE)
            ? AUTOSAVE_TRACE_OPERATION_ERROR_FLAG_DELETE_REASON_SET : 0u,
        ((uint32_t)current_op << AUTOSAVE_TRACE_OPERATION_ERROR_OP_SHIFT) |
        ((uint32_t)op_phase << AUTOSAVE_TRACE_OPERATION_ERROR_PHASE_SHIFT) |
        ((uint32_t)op_slot << AUTOSAVE_TRACE_OPERATION_ERROR_SLOT_SHIFT));
}
```

### 8.2 Hook #2 — `filesystem_completeLibraryIndexRebuild()`

filesystem.c:3343-3353. Discovered during the audit to be a **separate**
terminal function that bypasses `filesystem_complete()` entirely — every Save
path hands off to a deferred `.hcindex`/typed-Instrument-index rebuild chain
after its primary write succeeds, and that rebuild can itself fail here,
invisibly, no matter how thoroughly the primary path was instrumented. Same
packing as Hook #1, plus `AUTOSAVE_TRACE_OPERATION_ERROR_FLAG_INDEX_REBUILD`
(bit 1) set so the decoder can tell the two hook sites apart.

Both hooks report `op_delete_slot_error_reason`'s presence via bit 0 so a
decoder immediately knows whether to also cross-reference the more specific
`'O'` DELETE_RESULT record for this same failure.

---

## 9. Boot Bank Load timing — reused `'N'` `INSTRUMENT_ENTRY` stage

Not a new stage — reuses the pre-existing `AUTOSAVE_TRACE_STAGE_INSTRUMENT_ENTRY`
(`'N'`) layout (AutosaveTrace.h:250-276), which already had a `PHASE_REQUEST`
milestone value (`AUTOSAVE_TRACE_INSTRUMENT_ENTRY_PHASE_REQUEST = 1u`) that
had no boot-time producer before this session. New call site added inside
`filesystem_loadSceneDirectory_tick()` case 27, filesystem.c:9293-9299,
guarded by `filesystem_bankPayloadDetailActive()` and emitted alongside the
existing `filesystem_bootLoggingSetBankSceneDetail('I')` call. Packs
`op_bank_child_cursor`/`op_instrument_slot` at the existing SCENE/SLOT
shifts. This gives a first data point on boot Bank Load's per-Instrument
timing without inventing a new record type — one of the still-open Session
054 items listed as not-yet-root-caused (§12).

---

## 10. `tools/decode_devlogs.py` — decoder changes

Kept in lockstep with every C-side addition. New lookup tables:
`PHASE_STALL_SITES` (3), `SAVE_LIFECYCLE_TYPES` (4), `SAVE_LIFECYCLE_CHECKPOINTS`
(5), `DELETE_SLOT_REASONS` (10, matching `fs_delete_slot_reason_t`),
`AFATFS_RESULT_CODES` (11, matching `afatfsResultCode_t`),
`ASYNCFATFS_DELETE_TREE_FAILURE_SITES` (18, matching the new asyncfatfs enum,
including the "leading hypothesis" framing text for the two sites called out
in §5), `FS_INTERNAL_OPS` (44-entry ordered list mirroring `fs_internal_op_t`'s
declaration order — explicitly commented that it must be kept in sync by
hand, since Python can't introspect the C enum). New `elif` branches for
`ch == "X"`, `"O"` (with special-casing for `DELETE_RESULT`+`failed` to
unpack reason/detail/site, `Instrument`+`CREATE_RESULT` to show the CRC16,
and `Kit`+`REQUEST` to flag the ambiguous Menu-branch-tag caveat), and
`ch == "E"`. Verified via synthetic-record round-trip checks (inline, not
committed as test files), including a final check that a synthetic
`ASCEND_PARENT_CLUSTER_MISMATCH` site value decodes correctly through the new
table.

---

## 11. Bugs #1 and #2 (recap — already fixed, kept here since the diagnostics
were built to prove them)

**Bug #1 — delete-resolver spurious timeout error.** Two completion gates in
`filesystem_deleteSlotDirectory_tick()` (`FS_DELETE_SLOT_WAIT_CLOSE_SCAN` and
`FS_DELETE_SLOT_DELETE_MATCH`) previously treated
`op_delete_slot_timeout_observed` itself as a hard failure condition — meaning
any stall report, even a purely diagnostic one, would fail the whole
operation. Fixed so only `op_delete_slot_scan_error`/`op_delete_slot_match_count
> 1u` (scan phase) or `op_delete_tree_result != AFATFS_RESULT_OK` (native
result phase) can produce `FS_STATUS_ERROR`; a stall report is now purely
informational. **Status: confirmed fixed** — zero `'X'` PHASE_STALL/
DELETE_SLOT records have appeared in any retest since, meaning the 50,000-tick
stall path is no longer what's failing.

**Bug #2 — HCNAMES source provenance.** `filesystem_setResidentSource()`
staging calls added at the appropriate point in Kit/Scene/Bank Save
(straightforward) and Instrument Save (required a new self-transition into
`FS_INTERNAL_OP_UPDATE_HCNAMES_INSTRUMENT`, since the public
`filesystem_requestUpdateResidentInstrumentNames()` had zero callers
repo-wide before this fix — plus a new `filesystem_startInstrumentIndexRebuildScan()`
helper and two new branches in `filesystem_startLibraryIndexRebuild()`/
`filesystem_libraryIndexRebuildScanComplete()` to route the Instrument-typed
rebuild correctly). **Status: implementation verified correct by code
review; not yet independently re-confirmed via a fresh end-to-end card
test** — blocked behind the still-open native-delete bug below, since every
retest so far has hit `ScnS05` before reaching HCNAMES-staged territory on
the Scene path.

---

## 12. What the instrumentation has actually documented so far

In chronological retest order:

1. **Round 1** (post Bug #1 fix): `ScnS05` still occurred. **Zero** `'X'`
   PHASE_STALL records anywhere in the trace — proved Bug #1's mechanism was
   genuinely not the cause this time (failure happened in ~37 ticks, far
   under the 50,000-tick stall threshold). At this point no reason-tracking
   instrumentation existed yet, so nothing further could be determined from
   the trace alone.
2. **Round 2** (after adding the first 7 `fs_delete_slot_reason_t` values):
   `reason=NONE` — none of the 7 tagged branches fired, proving an 8th,
   untagged branch existed. Root-caused by code inspection to the
   `FS_DELETE_SLOT_WAIT_SCAN` `"."`-open-failure branch. Added
   `FS_DELETE_SLOT_REASON_DIR_OPEN_FAILED` and `STALL_ABANDONED` (9 reasons
   total) plus the universal `'E'` backstop, anticipating the next failure
   class before another round trip was needed.
3. **Round 3** (with full delete-slot reason coverage): clean evidence —
   `reason=DELETE_RESULT`, `detail=AFATFS_RESULT_UNSUPPORTED_LAYOUT`. This
   confirmed the bug lives inside asyncfatfs.c's native delete traversal
   (one of ~17 possible checks that all produce that same result code), not
   in filesystem.c's resolver — the reason the 17-site
   `afatfsDeleteTreeFailureSite_e` instrumentation (§5) was then built.
4. **Round 4** (with the full 17-site instrumentation): clean evidence again
   — `failure site=OPEN_DIR_BAD_ROOT_ON_FAT32`, at Scene slot 2 (`002 Hard`).
   Neither of §5's two documented leading hypotheses. Root-caused to
   `AFATFS_DELETE_TREE_RESUME_PARENT` reconstructing a resume target from a
   not-yet-established `op->parentCluster` when the first object a
   traversal's root scan encounters is a plain file rather than a
   subdirectory. Fixed — full root cause, fix, and confirmation are in §6
   ("Bug #3").
5. **Round 5** (post-Bug-#3-fix retest): `OPEN_DIR_BAD_ROOT_ON_FAT32` did
   **not** recur, confirming the Bug #3 fix — but a different failure site
   fired: `failure site=SCAN_PARENT_FOR_SELF_EXHAUSTED`, at Scene slot 6
   (`006 Brezel`, which contains an empty `Kit Brezel` subdirectory and a
   `pattern.pat` file). This is the other of §5's two original leading
   hypotheses. Rather than guess between the two ways this site can fire
   ("the re-scanned parent looked empty" vs. "the re-scan found other
   objects but never re-matched the target"), the `'Y'` `SCAN_PARENT_DIAG`
   probe (§6) was added specifically to distinguish them. *(Superseded:
   §6a shows this failure was caused by Bug #3's fix clobbering the
   search key, not by a genuine identity disagreement.)*
6. **Round 6** (with the seen-count probe): `candidates examined before
   giving up=3`, at Scene slot 7 (`007 Chip`, containing `Kit Chip` (empty),
   `pattern.pat`, and `sceneset.scg` — exactly 3 children). The count
   matching the full child count rules out "parent looked empty" — the
   re-scan reached every child, including `Kit Chip` itself, and still
   never matched it. This pointed at the identity-match predicate itself.

   Rather than extend the probe a third time to isolate *why* the
   predicate disagreed with itself, the surrounding code was re-read end
   to end to check whether the re-scan needed to exist at all — and it
   didn't (**Bug #4**, §6): `afatfs_retireObjectNameRun()` never needed a
   live parent scan, only the object identity already sitting untouched
   in `op->currentTarget` since the moment `Kit Chip` was first
   discovered. The re-scan-by-identity mechanism was deleted outright
   (`AFATFS_DELETE_TREE_SCAN_PARENT_FOR_SELF`/`_LOOP` → one direct
   `AFATFS_DELETE_TREE_REOPEN_PARENT` phase), which also retires the `'Y'`
   probe (its producer no longer exists) and the two failure sites it was
   built to disambiguate (now permanently unreachable). Net code size:
   **-904 bytes text, -24 bytes bss**. *(Superseded: the "untouched
   `op->currentTarget`" premise was wrong — the child's own scan overwrites
   that register. Round 7 proved it; see §6a.)*
7. **Round 7** (post-Bug-#4-fix retest): `failure site=FREE_CLUSTERS_NEXT_CLUSTER_INVALID`
   at Scene slot 12 (`012 Emott`) — the traversal freeing an already-freed
   cluster chain. This exposed **Bug #5** (§6a): Bugs #3 and #4 had each
   broken one half of the same invariant — `RESUME_PARENT` erased
   `file->directoryEntryPos` (the record of which directory is open), and
   `REOPEN_PARENT` took the ascended-from child's identity from
   `op->currentTarget`, which the child's own scan overwrites with every
   object it deletes. Both halves are now repaired by snapshotting the
   child's identity and the parent's entry pointer at descend time
   (`descendTarget`/`parentEntry`) and restoring them on ascend. This also
   retroactively explains rounds 5 and 6 as collateral damage from Bug #3's
   fix rather than independent findings. **Not yet retested on card** — this
   is the current open thread.

**Still open, not yet root-caused, and not this document's subject** (listed
only because the diagnostics above were also built to eventually illuminate
them): Kit Save menu cache behavior, a Bank Save entry freeze investigation
(§3.2's instrumentation targets this directly), boot Bank Load per-Instrument
timing (§9's instrumentation targets this directly), and Instrument overwrite
content verification (§7.3/§7.5's CRC fingerprint targets this directly).
None of these have produced findings yet — they are un-fired diagnostic
capability, not confirmed results.
