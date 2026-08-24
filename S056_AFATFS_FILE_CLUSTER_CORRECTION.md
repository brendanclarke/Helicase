# S056 — AsyncFATFS file-size cluster-boundary fix

**Date:** 2026-08-24
**File changed:** `Core/Hardware/SD/asyncfatfs/asyncfatfs.c`
**Status:** Fix applied, pending hardware verification.

---

## Bug

`afatfs_fseekAtomic()` does not call `afatfs_fileUpdateFilesize()`.

During sequential writes (the common path), every inter-sector seek resolves
atomically — `afatfs_fseekInternal()` calls `afatfs_fseekAtomic()` and returns
without queuing `afatfs_fseekInternalContinue()`. The continue path *does* call
`afatfs_fileUpdateFilesize()`, but for single-cluster-boundary seeks it is never
reached.

This means `logicalSize` stays at 0 throughout the entire write of a new file.
The only file-size value persisted to the directory entry during writing is
`physicalSize` (cluster-rounded), written by `AFATFS_SAVE_DIRECTORY_NORMAL`
during cluster allocation. `fclose()` compensates — it updates `logicalSize`
from `cursorOffset` and writes it via `AFATFS_SAVE_DIRECTORY_FOR_CLOSE` — but
if that final directory-entry write does not persist (cache eviction timing,
sector flush ordering, power loss), the on-disk size reverts to `physicalSize`.

## Evidence

`.hcprms1` on SD_CARD_B: **32,768 bytes** (one 32 KB cluster).
Expected: **34,768 bytes** (`AUTOSAVE_RECORD_BYTES`).
`.hcprms2` on both cards: correct 34,768 bytes.

32,768 = the `physicalSize` written by the first cluster allocation's
`SAVE_DIRECTORY_NORMAL`. The second cluster allocation would write 65,536, and
`fclose` would write 34,768. Observing 32,768 means the directory entry
retained only the first allocation's size.

## Location

[asyncfatfs.c:2365](Core/Hardware/SD/asyncfatfs/asyncfatfs.c#L2365) —
end of `afatfs_fseekAtomic()`, before `return true`.

The parallel path `afatfs_fseekInternalContinue()` already has this call at
[asyncfatfs.c:2408](Core/Hardware/SD/asyncfatfs/asyncfatfs.c#L2408).

## Fix

One line added to `afatfs_fseekAtomic()`:

```c
    if (!afatfs_isEndOfAllocatedFile(file)) {
        file->cursorOffset += offset;
    }

    afatfs_fileUpdateFilesize(file);   // ← ADDED

    return true;
}
```

Also removed stale `// TODO do we need this?` comment from the existing call
in `afatfs_fseekInternalContinue()` — yes, it is needed, and the atomic path
was the one missing it.

## Effect

`logicalSize` now tracks `cursorOffset` on every successful seek, not just
queued ones. Every subsequent `SAVE_DIRECTORY_NORMAL` during cluster allocation
will see the true file size in `logicalSize`. If `fclose`'s final directory
write is lost, the on-disk size is still accurate from the most recent
allocation save rather than stuck at a stale cluster-boundary value.
