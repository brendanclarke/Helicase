# S056 — AsyncFATFS non-duplicate mandate

**Date:** 2026-08-23
**File under change:** `Core/Hardware/SD/asyncfatfs/asyncfatfs.c`
**Status:** G1 implemented and verified; G2 implemented via boot sweep; on-card
verification outstanding

---

## 1. The mandate

Two guards, in strict priority order.

| | Guard | Strength |
|---|---|---|
| **G1** | It must be **impossible** for the writer to create a file whose display name already exists in the target directory. | **Inviolable.** No timing, no directory layout, no free-space pattern may defeat it. |
| **G2** | If duplicates already exist (external modification), they must be **detected and the extras deleted**. Which copy survives does not matter. | Best-effort. Guards operation contract only; file-content validity cannot be guaranteed for a card mutated outside the firmware. |

Matching is **case-preserving, not case-sensitive**: `.HCNAMES` and `.hcnames`
are the same name and must never coexist. Storage preserves the spelling the
caller supplied; lookup folds case.

---

## 2. Root cause of G1 violation — proven

`afatfs_createFileContinue()`'s scan phase, `AFATFS_CREATEFILE_PHASE_FIND_FILE`
([asyncfatfs.c:3836-3852](Core/Hardware/SD/asyncfatfs/asyncfatfs.c#L3836-L3852)):

```c
} else if (opState->longNameEnabled &&
           (fat_isDirectoryEntryEmpty(entry) ||
            fat_isDirectoryEntryTerminator(entry))) {
    uint8_t wasTerminator = fat_isDirectoryEntryTerminator(entry) ? 1u : 0u;
    afatfs_noteFreeDirectoryEntry(opState, &file->directoryEntryPos);
    if ((file->mode & AFATFS_FILE_MODE_CREATE) != 0 &&
        afatfs_freeRunIsReady(opState)) {
        afatfs_findLast(&afatfs.currentDirectory);
        opState->phase = AFATFS_CREATEFILE_PHASE_CREATE_NEW_LFN_FILE;
        goto doMore;                    /* <-- commits to CREATE, mid-scan */
    }
    ...
```

**The scan commits to creating a new entry the instant it finds a free run
large enough, without finishing the directory.** Any existing entry with the
same name that lies *after* that free run is never examined.

That is sufficient to produce a duplicate whenever the directory contains a
free run before the existing file, which is the normal state of any directory
that has ever had a file deleted from it.

Concretely, for a root directory laid out as:

```
[settings.cfg][free][free][free][free][.hcnames][.hcprms1] ...
```

a create-capable open of `.hcnames` fills the free run and produces a second
`.hcnames`, while the first remains. Both are then physically present, and
which one any given reader sees depends on its scan order.

### 2.1 Why the scan itself is otherwise sound

`afatfs_findNext()`
([asyncfatfs.c:2528-2560](Core/Hardware/SD/asyncfatfs/asyncfatfs.c#L2528-L2560))
returns `NULL` **only at physical end-of-allocated-directory**
(`afatfs_isEndOfAllocatedFile()`), not at a `0x00` terminator. It hands back
every raw entry including terminators. So the machinery to scan a complete
directory already exists and is correct — the create path simply exits before
using it.

### 2.2 The codebase already knows this hazard

`afatfs_retireDirectoryTerminator()`
([asyncfatfs.c:3607-3620](Core/Hardware/SD/asyncfatfs/asyncfatfs.c#L3607-L3620)):

> "A 0x00 terminator left before that run would make normal directory
> enumeration stop before the new object, so retire the skipped terminator
> before scanning onward."

The invisible-object failure mode was understood and defended against for one
case. The mid-scan create exit re-opens it for the general case.

---

## 3. Design

### 3.1 G1 — defer creation until the scan is complete

**Rule: a create may only begin after `afatfs_findNext()` has returned `NULL`
(physical end of directory).** Nothing else changes about how a create picks
its slot.

- On encountering a ready free run, **latch** it (`chosenFreeRun*`) instead of
  branching to create. Latch only the first such run, so first-fit placement is
  preserved exactly as today.
- Continue scanning. `afatfs_noteFreeDirectoryEntry()` may reset the working
  `freeRunStart`/`freeRunLength` freely; the latch is independent.
- At `entry == NULL`:
  - a name match was found earlier → SUCCESS (unchanged path),
  - else a latch exists → restore `freeRunStart`/`freeRunLength` from it and
    enter `CREATE_NEW_LFN_FILE`,
  - else → extend the directory, as today.

**Terminator retirement rule.** Today a passed terminator is retired whenever
create is intended. With the deferred decision that would retire the
directory's real terminator on every create-capable open, even when the object
ends up placed before it. Correct rule:

> Retire a terminator only if **no free run has been latched yet** at the moment
> it is encountered.

If a run is already latched, the new object will be placed *before* this
terminator, so enumeration reaches it and the terminator must stay. If no run is
latched, the object may be placed after this point, so the terminator must be
retired exactly as the original comment requires.

**Cost.** Every create-capable open scans the full directory. Read-only opens
are unaffected (see §3.3). Opens are not in the audio path.

### 3.2 G2 — detect and delete duplicates in the same pass

The G1 scan already visits every entry, so duplicate detection is free.

- On the **first** display-name match: load it into `file` as today, save its
  `directoryEntryPos`, set `matchFound`, and **keep scanning** (do not exit).
- On any **subsequent** match: retire that entry run in place — mark the SFN
  entry and its immediately preceding checksum-valid LFN fragments `0xE5`, and
  mark the sector dirty. Count it.
- At end of scan: restore `file->directoryEntryPos` from the saved match
  position and report SUCCESS.

Restoring the saved position matters: the finder *is* `file->directoryEntryPos`,
and continuing the scan moves it. That field is what a later close uses to
update the file's size and first cluster, so it must point at the surviving
entry, not wherever the scan stopped.

The survivor is the first match in scan order. The mandate explicitly does not
care which survives.

### 3.3 Scope (superseded by 3.4)

The first implementation made **every create-capable open** walk the full
directory, so G2 cleanup rode along on G1's proof. Measurement showed that was
the wrong trade — see §3.4 — and the scope is now narrower.

### 3.3.1 Original reasoning, retained

G1 is only meaningful for opens that can create. A read-only open cannot
manufacture a duplicate, so it keeps today's early-exit-on-match behaviour and
today's performance.

This is deliberate and bounded: a directory containing duplicates is repaired
by the next create-capable open of that name, and until then a read may return
either copy. That is exactly the "operation contract, not validity contract"
limit the mandate allows for externally-modified cards.

### 3.4 Revision: G1 costs nothing extra; G2 gets a dedicated sweep

**The key realisation.** G1 does not need the full walk on every create-capable
open. A create happens *only* when the scan finds no match, and "no match" is
only knowable once the scan has already reached physical end-of-directory. An
open that **does** find its file will never create one, so stopping there cannot
violate G1.

So the expensive proof is already paid exactly when it is needed — on genuine
creation — and is free in the common case of rewriting a file that exists.

**Why that matters here.** Directories are allocated in whole clusters. The root
holds ~16 entries but occupies a full cluster: at 32 KB clusters that is 64
sectors / 1024 entry slots. Forcing the walk on every create-capable open would
turn each `.hcnames` rewrite from ~1 sector read into ~64, at roughly 1–2.5 ms
per 512-byte block on the bit-bang bus. Across a boot that is on the order of
1–3 seconds of pure overhead, on a system that had just spent a session losing
to a boot deadline.

**Resulting design.**

| Path | Behaviour |
|---|---|
| Any ordinary open, read or write | stops at first match; full walk only when the name is absent (which is when G1 needs it) |
| `afatfs_fsweep_lfn()` — `AFATFS_FILE_MODE_SWEEP` | always walks to physical end and retires every surplus same-name run |
| Boot integrity pass | sweeps the firmware-owned root singletons once, before anything reads them |

G1 is unchanged and unweakened. G2 moves from "incidentally, on every write" to
"deliberately, once per boot, where the cost is accounted for."

### 3.5 Why the pre-existing `.hcnames` probe could not do this

`filesystem_hcnamesProbe_tick()` is built on `afatfs_findNextObject()`, which
**stops at a `0x00` directory terminator**
([asyncfatfs.c:2795](Core/Hardware/SD/asyncfatfs/asyncfatfs.c#L2795)). A
duplicate sitting after a stray terminator is invisible to it, so it reports a
clean singleton when the card holds two — consistent with it never having
flagged the duplicate that was actually present.

The sweep reads the raw entry stream via `afatfs_findNext()` and has no such
blind spot. Note the asymmetry this leaves: file **opens** are not affected,
because every `afatfs_fopen_lfn()` sets `longNameEnabled` and uses the raw scan.
Only directory *enumeration* stops at terminators, so the residual is that
browsing may not list such a file — cosmetic, and out of scope for this mandate.

---

## 4. SRAM

Added to `afatfsCreateFile_t`
([asyncfatfs.c:~234-260](Core/Hardware/SD/asyncfatfs/asyncfatfs.c#L234)), which
is a member of the per-file operation **union**, so this only costs anything if
`createFile` becomes the union's largest member.

| Field | Type | Bytes |
|---|---|---|
| `savedEntryPos` | `afatfsDirEntryPointer_t` | 8 |
| `chosenFreeRunLength` | `uint8_t` | 1 |
| `matchFound` | `uint8_t` | 1 |
| `duplicatesRetired` | `uint8_t` | 1 |
| padding | | 1 |
| **per instance** | | **12** |

**Correction.** A first draft of this section estimated "≤22 bytes" total. That
was wrong: `afatfsCreateFile_t` is instantiated once per open file
(`AFATFS_MAX_OPEN_FILES` = 5) plus the current directory and other file
objects, so every byte added is multiplied by roughly seven. The first
implementation used two separate `afatfsDirEntryPointer_t` fields and measured
**+160 bytes of `bss` — over the 100-byte approval.**

The fix was to collapse them into one `savedEntryPos`. Its two roles are
mutually exclusive by construction: while `matchFound` is zero it latches the
free run for a pending create; once `matchFound` is one the object already
exists, creation is impossible for that operation, and the field holds the
surviving match's position instead. `chosenFreeRunLength` is only consulted
while `matchFound` is zero, and the match position only when it is one.

`afatfsDirEntryPointer_t` is `uint32_t sectorNumberPhysical` + `int16_t
entryIndex` = 8 bytes with padding
([asyncfatfs.h:87-90](Core/Hardware/SD/asyncfatfs/asyncfatfs.h#L87-L90)).

**Measured: `bss` 94764 → 94852 = +88 bytes**, against the 100-byte
pre-approval. `text` 382188 → 383260 = +1072. The sweep's filename table is
`const char *const []` and lives in flash; the boot pass adds no state, reusing
`op_phase`/`op_item_offset`.

---

## 5. Changes

Recorded here as they land; each carries its comment block in the source.

| # | Site | Change |
|---|---|---|
| **C1** | `afatfsCreateFile_t` | Added `savedEntryPos`, `chosenFreeRunLength`, `matchFound`, `duplicatesRetired` with the sizing note. |
| **C2** | `AFATFS_CREATEFILE_PHASE_INITIAL` | Clears all four fields so no previous operation's latch or match can authorize a create or suppress a scan. |
| **C3** | LFN free-run branch | **The G1 fix.** Latches the first ready run instead of branching to `CREATE_NEW_LFN_FILE`, and keeps scanning. Latch written once, so first-fit placement is unchanged. |
| **C4** | Terminator retirement | Now conditional on no run having been latched. Prevents destroying the directory's real terminator on every create-capable open while preserving the original hazard fix when the object may land after it. |
| **C5** | `entry == NULL` branch | The single authorized creation point. Restores the latched run, and short-circuits to SUCCESS with `file->directoryEntryPos` restored when the scan matched. |
| **C6** | `afatfs_retireDuplicateNameRun()` | New. Marks a surplus SFN plus its checksum-matching LFN fragments in the same sector `0xE5` and dirties the sector. Does not free the FAT chain — foreign data ownership is not guaranteed. |
| **C7** | Match branch | First match loads and records, then keeps scanning; subsequent matches are retired. Read-only opens keep the original exit-on-first-match. |
| **C8** | Short-name terminator branch | **Second G1 fix.** A create-capable request no longer creates at the first `0x00`; it scans to physical end. Read-only keeps the early exit. |
| **C9** | `AFATFS_FILE_MODE_SWEEP` (bit 64) | New mode bit; no mode string produces it. Gates the walk-past-first-match behaviour. |
| **C10** | Match branch gate | Changed from `CREATE` to `SWEEP`. All ordinary opens now stop at first match; only a sweep continues. G1 unaffected — see §3.4. |
| **C11** | `afatfs_fsweep_lfn()` + header | New public entry point. Opens read-only with SWEEP, never creates; absent name completes with NULL = "nothing to do". |
| **C12** | `FS_INTERNAL_OP_SWEEP_DUPLICATES` + `filesystem_sweepDuplicates_tick()` | New facade op iterating `fs_duplicate_sweep_names[]`. Skips absent names and handle-exhaustion rather than failing boot. |
| **C13** | `filesystem_sweepDuplicateSingletonsBlocking()` + header | Blocking driver; zeroes `op_item_offset` because `filesystem_start()` does not. Zero return is advisory. |
| **C14** | [main.c](main.c) boot ladder | Call placed after mount and **before** `preset_loadGlobals()`, the first ladder step that reads a swept file. Result discarded by design. |
| **C15** | `BOOT_FILESYSTEM_TIMEOUT_MS` 20000u → 30000u | Margin for the sweep. **Ceiling 32767** — the deadline compares a `uint16_t` elapsed value, so ≥32768 removes the deadline rather than extending it. A `120000u` value set earlier in Session 056 did exactly that. |

---

## 6. Verification

1. `make clean && make` — **done, clean.** The one `asyncfatfs.c` warning
   (`unused parameter 'eraseCount'`, line 1246) is pre-existing and unrelated.
2. `bss` delta **measured at +96** by building `HEAD`'s `asyncfatfs.c` and the
   changed one back to back. Recorded in §4.
3. Static review points — **all confirmed**:
   - `grep` for `AFATFS_CREATEFILE_PHASE_CREATE_NEW` resolves to exactly two
     assignments, at lines 3934 and 3963, and the `entry == NULL` branch spans
     3889–3971. **Both create sites lie inside it**, so no path reaches a
     create without physical end-of-directory having been observed.
   - the latch is guarded by `chosenFreeRunLength == 0u`, so it is written once.
   - `file->directoryEntryPos` is restored from `savedEntryPos` before SUCCESS.
   - no path reaches `CREATE_NEW_LFN_FILE` or `CREATE_NEW_FILE` without
     `entry == NULL` having been observed first,
   - the latch is written once per operation,
   - `file->directoryEntryPos` is restored to the surviving match before
     SUCCESS.
4. On-card: create-capable open of a name that exists after a free run must
   reuse the existing entry, not create a second one.
5. On-card: a directory seeded with two same-name entries must lose one on the
   next create-capable open of that name.

---

## 7. What this does not do

- It does not repair duplicates found by read-only opens.
- It does not validate file *contents* on a card modified externally; the
  mandate limits that to the operation contract.
- It does not change how a surviving duplicate's data is chosen — the first in
  scan order wins, arbitrarily and by design.
