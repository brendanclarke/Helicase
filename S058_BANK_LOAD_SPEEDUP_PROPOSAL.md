# Session 058 Bank Save/Load speedup proposal

## Decision summary

The first implementation should be a **legacy-format-preserving traversal and
name-cache refactor**. It removes known redundant work from Bank Load and Bank
Save without increasing foreground CPU work or changing anything on the SD
card. The proposed SRAM1 reservation is **1,314 bytes** (reserve 1,320 bytes to
allow normal alignment):

| Allocation | Exact payload | Owner and lifetime |
|---|---:|---|
| Dedicated HCNAMES name rows, `129 x 9` | 1,161 bytes | Filesystem; persistent for the mounted card |
| HCNAMES mirror validity state | 1 byte | Filesystem; cleared at boot/remount and during an uncommitted rewrite |
| Sixteen Bank-child display names, `16 x 9` | 144 bytes | Filesystem; meaningful only during Bank operations |
| Buffered text-reader cursor/state | at most 8 bytes | Filesystem; one active text file |
| **Total** | **at most 1,314 bytes** | SRAM1 `.bss` |

That package should:

1. collect all selected child names during Bank Load's one existing Bank scan;
2. retain the selected Bank directory as the parent between child Scenes rather
   than returning to root and reopening it;
3. give HCNAMES its own small cache so it no longer destroys the 9,000-byte
   Bank `.hcindex` cache; and
4. use the existing 512-byte staging buffer as text read-ahead while retaining
   the present 16-character parse budget per foreground tick.

This is the best first step because it is low risk, preserves legacy media, and
directly removes an O(number-of-children squared) Bank Load scan. It will make
Bank Load materially faster. It will improve Bank Save more modestly because
Save's dominant remaining cost is creating/truncating 161 individual files.

For repeated saves in one powered session, Option 2 adds an **SD-card-verified
clean-Scene mask** using about eight bytes. Its bits are deliberately volatile:
they start clear at boot/remount and become set only after the matching Bank
Scene was actually loaded from, or durably saved to, that Bank on the current
mounted card. Autosave recovery never establishes this authority. For a larger
overwrite improvement while retaining the current format, Option 3 is an
**in-place retained-cluster rewrite path** using about **160 more bytes** of
operation state.

The earlier pack-file Options 4 and 5 are withdrawn from the active proposal as
unnecessary file-format complexity. The earlier hardware-SPI/DMA Option 6 is
also withdrawn: Session 010's hardware investigation records that the traced SD
GPIOs are the wrong pins for every usable SPI alternate-function mapping, while
the remaining SPI peripherals are occupied or unbonded. The bit-banged route is
fixed in PCB copper and is the supported board path.

Expanding the AsyncFATFS sector cache is **not necessary and is not
recommended**. It cannot remove the file/directory/FAT operations that dominate
this workload, and every additional cache sector increases both SRAM use and
the CPU cost of AsyncFATFS's linear cache searches. No proposal below increases
the foreground per-tick byte budget. Compression is also excluded because it
would exchange I/O for CPU, contrary to the stated constraint.

## Constraints and measured baseline

This proposal treats the following as hard constraints:

- audio/DSP CPU allocation and interrupt headroom must not be reduced;
- filesystem work must remain asynchronous and bounded per foreground tick;
- a slight, explicitly owned SRAM1 increase is preferred over spending more
  CPU;
- existing staged-commit behavior must not be weakened; and
- legacy-format compatibility must be identified separately from format
  revisions.

The Session 057 trace measured approximately **7.7--8.3 seconds per Scene**
during Bank Save. A complete 16-Scene payload therefore takes roughly
**125--135 seconds** before final HCNAMES/index maintenance on the tested card.
The trace also proved that file handles do not accumulate and that the apparent
stall was a poll-count watchdog abort, not handle exhaustion. The diagnostic
handle-pool expansion did not change the observed cadence. Nothing in that
trace identifies sector-cache capacity as the limiting resource.

A complete legacy Bank contains:

- one `bankset` file;
- sixteen Scene directories;
- per Scene: one `sceneset`, one embedded Kit directory, one `kitset`, six
  Instrument files, one Pattern file, and one Effects file.

That is **161 data files**: `1 + 16 x 10`. It also creates/uses the Bank
directory, sixteen Scene directories, and sixteen embedded Kit directories.
The inspected fixture contains 152 files and 134,970 logical bytes because some
fixture children are incomplete. A normal complete Bank is still only around
140 KiB of logical payload.

The tested FAT volume uses 32,768-byte clusters. Consequently, the 161 small
files alone consume at least:

```
161 x 32,768 = 5,275,648 bytes (5.03 MiB)
```

The 33 Bank/Scene/Kit directories can add another 1.03 MiB of cluster
allocation on a fresh tree. Thus a roughly 140-KiB logical Bank can require
about 6 MiB of physical cluster ownership. More importantly for latency, each
small file requires some combination of directory lookup/update, file
open/create, FAT allocation or truncation, data write, size publication, cache
flush, and close. That metadata traffic, not the text serializer's byte count,
is the primary Save cost.

The current storage memory relevant to this proposal is:

| Existing allocation | Size | Current purpose |
|---|---:|---|
| General name/index cache | 9,000 bytes | 1,000 rows of nine bytes; also temporarily borrowed by HCNAMES |
| HCNAMES source register | 258 bytes | 129 `uint16_t` source/provenance values |
| Typed payload stage | 2,048 bytes | Mutually exclusive Scene/Kit/Instrument/Autosave state |
| Streaming buffer | 512 bytes | Pattern, Autosave, and other serialized chunks |
| AsyncFATFS data cache | 4,096 bytes | Eight 512-byte sectors |

The recommended additions have a named owner and do not repurpose playable
Scene/Pattern memory. Before implementation, the SRAM manifest must be updated
with the chosen reservation; this document proposes the allocation but makes no
code or linker change.

## Phase and cost audit by element

### Instrument

An Instrument is about 1.31--1.38 KiB on the inspected media, but occupies one
32-KiB cluster. Six Instrument files are processed for every Kit.

| Direction | Current work | Refactor opportunities |
|---|---|---|
| Load | Scene phases 27--31 repeat prepare/open, wait, line parse, close, and close-wait for each of six files. `filesystem_readTextLine()` allows 16 characters per tick but calls `afatfs_fread(..., 1)` once per character. | Refill the existing 512-byte buffer with one block of at most the remaining 16-byte tick budget, then parse at the same 16-character limit. This reduces function/cache lookup overhead without increasing the transfer budget. |
| Save | Scene phases 23--27 repeat open/create, wait, text-line writes, close, and close-wait for all six files. The line writer already sends each generated line to `afatfs_fwrite()` as a block. | Whole-file pre-serialization has little I/O benefit and would create a larger CPU burst. Retained-cluster rewrite avoids freeing/reallocating each existing Instrument cluster. |

Instrument parsing is not the main SD bottleneck: once a sector is in cache,
one-byte reads primarily waste foreground call overhead. A read-ahead change is
therefore a safe CPU reduction, but it will not approach the gain from removing
file and FAT operations.

### Kit

A legacy Kit is one `kitset` plus six Instrument files: seven files and, when
embedded in a Scene, one directory. Its logical payload is under about 9 KiB,
but its seven files consume at least 224 KiB of data clusters on this card.

| Direction | Current work | Refactor opportunities |
|---|---|---|
| Load | Standalone Kit Load processes `kitset` and then six Instruments. Embedded Scene Load enters the discovered Kit directory, processes the same seven files, and returns to the owning Scene. | Buffered text input reduces the per-character call overhead in all seven files. Parent-CWD retention prevents surrounding Bank navigation from being repeated. |
| Save | Standalone and embedded paths create/open the Kit directory, write `kitset`, then delegate six Instrument writes. | Retained-cluster rewrite removes FAT reallocation on canonical overwrite while keeping the current Kit structure and serializers. |

The seven-file Kit subtree remains the largest repeated object group under the
current format. Option 3 targets its overwrite allocation churn without adding
a second file schema.

### Scene

A complete legacy Scene contains ten files and two directories (the Scene and
its embedded Kit). Its logical payload is small, but the file payloads alone
own at least 320 KiB of clusters on this volume.

| Direction | Current work | Refactor opportunities |
|---|---|---|
| Load | Phases 8--11 scan the selected Scene to discover `Kit *`, Pattern, and Effects. Phases 12--16 read `sceneset`; phases 17--31 read Kit plus six Instruments; phases 44--60 read Pattern and Effects. A standalone Scene must navigate back into the Scene after returning from the nested Kit. The Bank-local path already avoids part of that reopen. | Cache discovery results from the initial scan and retain the correct parent CWD across Bank delegation. Keep the current ten-file schema. |
| Save | Phases 8--37 create the Scene and write `sceneset`, the Kit directory and seven Kit files, Pattern, and Effects. In a Bank Save, phase 37 returns all the way to root. | In Bank delegation, return one level to the selected Bank instead. Retained-cluster rewriting accelerates canonical legacy overwrite. |

The 47-byte Effects file and roughly 314-byte Pattern file each still cost a
full cluster plus their own metadata transaction. Under the selected scope they
remain separate legacy objects; Option 3 avoids reallocating their clusters on
canonical overwrite.

### Bank Load

The current Bank Load contains two avoidable navigation loops in addition to
the necessary payload reads:

1. phases 15--17 scan the selected Bank once to build the child-presence mask;
2. phases 27--30 reopen `.` and rescan the entire Bank directory to rediscover
   the display name for one child;
3. after that child Scene completes, Scene phase 72 returns to filesystem root;
4. phases 21--26 reopen `/Bank/`, enter the selected Bank, close both handles,
   and then repeat the per-child scan.

For a full Bank this is one initial scan plus up to sixteen complete rescans.
It visits on the order of 256 child objects to recover sixteen names, and it
performs fifteen unnecessary root-plus-Bank reopen cycles after the first
child. This is an O(n-squared) directory traversal layered on top of the actual
file reads.

Bank Load also reads root HCNAMES into the shared 9,000-byte name cache. That
invalidates the already-loaded Bank `.hcindex` view even though HCNAMES needs
only 129 nine-byte name rows. The filesystem must later restore the Bank index
view.

### Bank Save

Bank Save similarly returns to root after every delegated Scene and uses phases
13--19 to reopen `/Bank/` and the selected Bank before the next child. Phase
20/21 scans for and recursively deletes the old child, then Scene phases 8--37
recreate its ten-file tree. The reopen loop is redundant, but the delete and
recreate traffic is the dominant cost.

At completion, Bank Save durably rewrites HCNAMES. Because the HCNAMES reader
borrowed and cleared the shared Bank index cache, the save then physically
rescans `/Bank/` and rewrites the complete slot-ordered `.hcindex`. The index
rewrite is only around a kilobyte on a sparse library; the avoidable physical
directory rescan and cache destruction are the meaningful parts.

## Option 1: legacy traversal and dedicated HCNAMES cache

**Recommendation: implement first.**

### Current code locations to inspect

This option is intentionally a refactor of existing ownership and navigation,
not a new Bank path. The important locations are:

| Current location | Why it matters |
|---|---|
| `Core/Hardware/SD/filesystem.c:888-923` | Declares the 9,000-byte shared name cache, 258-byte HCNAMES source register, and their exact size assertions. Put any dedicated HCNAMES name rows adjacent to these owners. |
| `Core/Hardware/SD/filesystem.c:1414-1478` | Clears, retags, and exposes the shared cache by library kind. The dedicated HCNAMES mirror must stop HCNAMES operations from passing through this numbered-library disposal path. |
| `Core/Hardware/SD/filesystem.c:4730-4753` | `filesystem_prepareResidentNamesCache()` explicitly clears the `.hcindex` view and borrows `fs_list_cache_name` for 129 HCNAMES rows. This is the exact cache-ownership behavior Option 1C replaces. |
| `Core/Hardware/SD/filesystem.c:4792-4872` | Parses HCNAMES name/source pairs, overlays a committed name, and clears source dirty flags only after close. Preserve these parsing and durability semantics while changing the destination array. |
| `Core/Hardware/SD/filesystem.c:10953-11010` | Bank Load already scans the selected Bank once, but records only presence bits. This is where each slot's lexical-winning nine-byte display name should also be captured. |
| `Core/Hardware/SD/filesystem.c:11191-11550` | Advances children, reopens `/Bank/` and the selected Bank in phases 21--26, then opens `.` and rescans it in phases 27--30 for one name. This entire normal-path repetition is the principal Option 1A/1B target. |
| `Core/Hardware/SD/filesystem.c:10343-10360` | Scene Load has validated and committed one Bank child here and already knows `op_bank_child_cursor`. This is the successful per-child boundary that must retain the Bank parent contract. |
| `Core/Hardware/SD/filesystem.c:10490-10509` | Scene phase 72 unconditionally returns to root before handing Bank Load back to phase 20. Split the Bank-delegated and standalone navigation here. |
| `Core/Hardware/SD/filesystem.c:14659-14757` | Bank Save phases 12--19 reopen `/Bank/` and the selected Bank after each Scene writer returns to root. These phases become fallback/recovery rather than the successful loop. |
| `Core/Hardware/SD/filesystem.c:15513-15531` | Scene Save phase 37 unconditionally returns to root, then hands delegated Bank Save back to phase 12. This is the Save-side parent-retention split. |
| `Core/Hardware/SD/asyncfatfs/asyncfatfs.c:5506-5584` | `afatfs_chdirParent()` is wait-capable and returns an enum, not a boolean. The new Scene return branches must retry `IN_PROGRESS`, accept `SUCCESS`, and route `FAILURE` through root/error cleanup. |
| `Core/Hardware/SD/filesystem.c:14964-14988` and `:7197-7239` | Bank Save currently schedules a physical Bank rescan and then the complete index writer after HCNAMES closes. A still-valid Bank cache can bypass only the scan; it must not bypass the durable index-write/flush boundary. |
| `Core/Hardware/SD/filesystem.c:8365-8416` | `filesystem_readTextLine()` performs up to sixteen one-byte `afatfs_fread()` calls per tick. This is the bounded reader that Option 1D coalesces without raising its byte budget. |

These locations are the “make sure you look at this” set for Option 1. In
particular, `afatfs_chdirParent()`'s enum contract and HCNAMES source dirty-flag
publication must not be simplified while removing redundant traversal.

### 1A. Collect all child names during the existing Bank scan

Change Bank Load phases 15--17 so the same object visit that sets the
child-presence bit also stores the lexical winning display name in
`bank_child_display[slot][9]`. Preserve the existing duplicate rule: for two
directories that parse to the same slot, retain the same folded/case-aware
lexical winner used today. At phase 27, format `SS Name` directly from the
cached row and open it. Remove normal-path phases 27--30 and their `.` handle.

The comparison cannot be “first entry wins.” The existing per-child scan at
`filesystem.c:11490-11500` calls `filesystem_displayPrecedesCached()` to retain
the product's deterministic duplicate winner. Move that decision into the
initial scan rather than changing it. Clear all sixteen rows before phase 15 so
an absent slot cannot inherit a name from an earlier Bank operation, and treat
`op_bank_child_present_mask` plus a nonblank captured row as the paired result
of the same scan.

The exact SRAM payload is 144 bytes. There are two implementation variants:

- a zero-new-SRAM variant can use rows 129--144 of the existing 9,000-byte
  cache while that allocation is tagged as HCNAMES, because HCNAMES exposes
  only rows 0--128;
- the recommended dedicated-cache variant below preserves the 9,000-byte Bank
  index, so it should allocate the explicit 144-byte Bank-child table.

The zero-SRAM variant is useful if memory policy rejects the complete package,
but it retains the current cache ownership problem and is not the preferred
design.

### 1B. Retain the selected Bank as the parent CWD

For a Scene loader delegated by Bank Load, change successful Scene phase 72
from `afatfs_chdir(NULL)` to one `afatfs_chdirParent()`. Control then returns to
Bank phase 20 with the selected Bank still current. The Bank state machine can
advance its cursor and directly open the cached next child; phases 21--26 remain
only as recovery/fallback entry paths.

For a Scene writer delegated by Bank Save, apply the same rule at successful
Scene phase 37: return from the child Scene to its selected Bank parent, not to
root. Bank phase 12 can advance directly to phase 20. Standalone Scene
Load/Save must retain their current root-return contract. Any error/cancel
cleanup should still restore root before releasing the filesystem facade.

This change uses no SRAM and performs less CPU and SD work. It must be expressed
as an explicit parent/CWD postcondition in comments beside both Scene return
branches because Bank and standalone callers require different destinations.
It must also use the three-way `afatfsOperationStatus_e` handling already used
by Scene quarantine traversal at `filesystem.c:10394-10400`; treating
`AFATFS_OPERATION_SUCCESS` as a C boolean is specifically forbidden by the API
comment in `asyncfatfs.h:425-436`.

### 1C. Give HCNAMES a 1,161-byte name mirror

Add `129 x 9` bytes for HCNAMES name rows next to the existing `129 x uint16_t`
source register. HCNAMES readers and writers use this mirror instead of
borrowing `fs_list_cache_name`.

Required cache-coherency rules are precise:

- invalidate it on card removal, mount change, HCNAMES open/read failure, or
  structural parse failure;
- fill it on the first valid HCNAMES read after mount;
- on a targeted update, mark the mirror invalid for readers, overlay the
  changed rows in that same allocation, and publish it as valid again only
  after the rewritten HCNAMES file closes and flushes successfully; a failed
  transaction leaves it invalid and forces a later physical reload, so no
  second name image or rollback allocation is required;
- retain the existing dirty-source semantics while a write is in flight;
- fall back to a physical HCNAMES read whenever the mirror is invalid.

With that mirror, a normal Bank operation does not destroy a valid Bank
`.hcindex` cache. After Bank Save, update the known target slot in the retained
slot-ordered Bank cache and invoke the index writer directly. A physical
`/Bank/` rescan is needed only if the Bank cache was not valid when the save
began or if repair/duplicate handling made the final slot identity uncertain.
The final HCNAMES write itself remains required when resident identities or
sources changed; this option removes redundant reads and cache restoration,
not durability.

The direct cache update is safe only for `FS_NAME_CACHE_BANK`, a valid
slot-ordered cache, and an operation whose final numbered Bank identity is
known. `filesystem_loadLibraryIndex_tick()` documents the non-compacted
slot-to-row contract at `filesystem.c:7242-7253`. If those preconditions are
not true, preserve the current scan-and-rebuild path instead of manufacturing
an apparently valid cache.

### 1D. Reuse the 512-byte buffer for text read-ahead

Refactor `filesystem_readTextLine()` into a small buffered reader:

1. when its `[position, length)` window is empty, make one block read into the
   existing `staging_buf`, limited to the remaining 16-byte budget for that
   tick rather than the buffer's full 512-byte capacity;
2. consume no more than the current 16 characters per foreground tick;
3. preserve partial-line state in the existing 160-byte line buffer;
4. reset reader state on every file open/close and never carry bytes between
   handles; and
5. keep Pattern/Autosave use mutually exclusive through the existing facade
   operation ownership.

Only two `uint16_t` cursors and a few flags are needed, at most eight bytes.
This reduces as many as sixteen one-byte `afatfs_fread()` calls and cache lookup
loops to one bounded block call per tick without increasing the byte budget.
Do not raise the 16-character parse budget as part of this change.

Every caller that switches `op_file` must reset the read window. This includes
Bankset, sceneset, kitset, six Instrument files, HCNAMES, settings, and index
text readers; a helper local only to the Instrument loop would leave stale
bytes available to the next file. The shared filesystem facade makes the
existing `staging_buf` mutually exclusive with Pattern/Autosave operations, but
that assumption should remain stated beside the reader state rather than being
implicit.

### Option 1 impact

| Property | Assessment |
|---|---|
| New SRAM1 | At most 1,314 bytes; reserve 1,320 bytes |
| CPU | Lower: fewer directory scans, opens, and byte-sized read calls |
| On-card format | Unchanged |
| Legacy card compatibility | Complete |
| Bank Load gain | High for a multi-Scene Bank; O(n-squared) scan becomes O(n), and 15 parent reopen cycles disappear |
| Bank Save gain | Low to moderate; parent reopens and post-save Bank rescan disappear, but 161-file writes remain |
| Risk | Low to medium; CWD postconditions and cache invalidation need focused tests |

An engineering target is at least a 20% full-Bank Load reduction and at least a
10% overwrite-Save reduction on the Session 057 card, but these are benchmark
gates, not promises. The phase counts provide a stronger deterministic check:
a 16-Scene Load should perform one Bank child scan, zero per-child rescans, and
zero root/Bank reopen pairs after the first child.

## Option 2: skip card-verified clean Scenes within one power cycle

This option must use **card-verified clean**, not merely “unchanged,” as its
term and invariant. The firmware cannot know whether a removable SD card was
edited while the machine was powered off. Therefore the clean state is
deliberately volatile and is never serialized to settings, HCNAMES, a Bank, or
Autosave.

### Exact authority rule

A bit for Scene `N` may be set only when, during the current boot and current
mounted-card session, one of these events has completed successfully:

1. Scene `N` was actually loaded from child `N` of identified root Bank slot
   `B`; or
2. Scene `N` was actually written to child `N` of Bank slot `B`, and the entire
   Bank Save reached its final sync/index completion boundary without a later
   edit to that Scene.

The bit means only: “resident Scene `N` equals child `N` of Bank `B` on the card
that is mounted now, based on I/O completed during this powered session.” It
does not mean that SRAM equals some historically selected Bank, and it does not
survive loss of card authority.

All bits must be cleared:

- in `bank_init()` at every cold boot;
- in `filesystem_initAfterCardReady()` for every fresh mount/remount, including
  reinsertion of the same physical card;
- when a different Bank becomes the clean-state target, except for bits newly
  proven by that operation;
- after any root Scene, root Kit, root Instrument, Pattern, or future source
  operation replaces all or part of a resident Scene from somewhere other than
  that exact Bank child; and
- when a Scene/Kit/Instrument/Pattern value or any associated Scene/Kit/
  Instrument HCNAMES identity changes.

Most importantly, a future Autosave reader must **not** set these bits. Autosave
can reconstruct the last resident workspace, but it does not prove that any
root Bank directory still contains the same bytes: the card may have been
modified between power-offs, and the recovered source is the Autosave record,
not `Bank/BBB Name/SS Name/`. An Autosave recovery commit should leave the mask
zero (or explicitly clear it if recovery can also happen after a remount).

This still has useful behavior. After a user loads Bank 009 from the card,
edits one Scene, and saves Bank 009 several times without powering off or
remounting, the first save can establish clean bits for every Scene it writes;
later saves can skip those clean children until their content or identity is
changed.

### Minimal volatile state and request behavior

The existing eight-byte budget remains sufficient if it is used as four
two-byte fields rather than as a persistent dirty history:

| Field | Bytes | Meaning |
|---|---:|---|
| `bank_scene_sd_clean_mask` | 2 | Scenes currently proven equal to one Bank slot on this mounted card |
| `bank_scene_sd_clean_slot` | 2 | That Bank slot, with `0xffff` meaning no card authority |
| `op_bank_sd_clean_candidate_mask` | 2 | Children written by the active Bank Save but not yet published clean |
| `op_bank_scene_mutated_during_save_mask` | 2 | Scenes changed while their candidate write/final flush was in flight |
| **Total** | **8** | Volatile SRAM1/BSS only |

At `filesystem_requestSaveBank()`:

- first retain the existing bounds/presence filtering;
- if the target slot equals `bank_scene_sd_clean_slot`, calculate
  `skip_mask = requested_mask & bank_scene_sd_clean_mask`;
- otherwise `skip_mask` is zero and every requested/present Scene must be
  written;
- keep an explicit force-save path that ignores `skip_mask`; and
- continue writing Bank metadata when necessary even if the effective child
  write mask becomes zero.

Do not publish a clean bit when a child file closes. Add it only to the
operation candidate mask, and merge candidates into the persistent clean mask
after the complete Bank operation has crossed `afatfs_sync()` and the deferred
`.hcindex` chain. Any mutation clears the persistent bit and the candidate bit
for that Scene. Clear that Scene's “mutated during save” bit immediately before
its writer starts; if a mutation sets it before the writer and final completion
finish, the child must remain non-clean even if Save otherwise succeeds. A
failed/cancelled Bank Save discards all candidates.

For a Bank Load, use the **completed effective mask**, not the requested mask or
the Bank's presence mask. Missing or failed children were not proven. When the
load slot matches the current clean slot, successful selected bits can be ORed
in while unselected proven bits remain. Loading from another slot clears the
old authority and sets only successfully completed children for the new slot.

### Current code locations to inspect

| Current location | Why it matters |
|---|---|
| `Core/Bank/BankData.c:5-10` and `:99-117` | Owns current Bank identity and boot initialization. The clean mask/slot are Bank-session metadata and must start invalid here; do not add them to serialized Bank fields. |
| `Core/Hardware/SD/filesystem.c:20291-20330` | Reinitializes filesystem/autosave policy for a freshly mounted card. Clear all card-clean authority here as well as at cold `bank_init()`, so removal/reinsertion in one power cycle cannot preserve bits. |
| `Core/Hardware/SD/filesystem.c:22643-22712` | Bank Load request captures the actual root Bank slot and requested mask. The slot association starts here, but no bit is proven merely by request acceptance. |
| `Core/Hardware/SD/filesystem.c:10343-10360` | One delegated Bank Scene has validated and committed its settings, Kit, Pattern, and names. This is a per-child success fact, but final publication should still use the operation's completed mask. |
| `Core/Bank/Scene/Preset/presetManager.c:510-544` | `on_bank_load_complete()` already obtains `filesystem_lastBankLoadSceneMask()`, which is the effective successful child mask. This is the correct mask to establish load-derived clean bits after `FS_STATUS_DONE`; do not use `bank_scenePresentMask()`. |
| `Core/Hardware/SD/filesystem.c:22714-22846` | Bank Save request filtering and `op_bank_scene_save_mask` ownership live here. Apply the same-slot clean skip only after the current present/bounds safety filter, and retain a force-save bypass. |
| `Core/Hardware/SD/filesystem.c:14759-14858` | Per-child delete/write handoff. Reset the current child's mutation-in-flight bit before phase 22 and form a candidate only after the delegated Scene writer succeeds. |
| `Core/Hardware/SD/filesystem.c:14964-14988`, `:3584-3613`, and `:3655-3673` | A Bank Save does not become durable at child close: it still performs HCNAMES close, optional index rescan/write, and final `afatfs_sync()`. Publish candidate clean bits only after this complete success chain. |
| `Core/Bank/Scene/SceneData.c:35-64` | Central change-aware Scene and Kit scalar stores. Actual changes here must invalidate that Scene's card-clean bit. |
| `Core/Bank/Scene/Preset/presetManager.c:830-859` | Central normal/Morph Instrument endpoint store. This is another required invalidation funnel. Whole Instrument/Kit copy paths around `presetManager.c:1635-1660` also need explicit review. |
| `Core/Bank/Scene/Pattern/PatternData.c:104-217` | Pattern step, clear, copy-track, copy-pattern, and copy-bar operations mutate Bank-saved data but are not currently represented by Autosave's non-Pattern dirty mask. Every actual Pattern change must invalidate the destination Scene's clean bit. |
| `Core/Bank/Scene/Preset/presetManager.c:250-281` and `:461-494` | Successful root Kit and root Scene loads directly replace resident data. They must clear affected Bank clean bits because their source is not the identified Bank child. Root Instrument/Pattern load completions need the same audit. |
| `Core/Bank/Scene/Autosave.c:911-1012` and `:1319-1364` | Shows why the Autosave dirty mask is not the authority: it tracks byte persistence for another file and explicitly omits Pattern from the current whole-Scene scope. Do not derive card-clean state from it. |

The mutation audit must include direct `memcpy`/whole-object commits, not just
scalar setters. Conversely, a Bank Load's existing calls to
`autosave_markSceneWithoutPatternDirty()` are notifications to the Autosave
writer and must not immediately invalidate the newly established Bank-card
clean bit. Keep the two concepts and APIs separate.

| Property | Assessment |
|---|---|
| New SRAM1 | 8 bytes, deliberately nonpersistent |
| CPU | A few mask operations on actual mutations/operation boundaries; large net reduction when children are skipped |
| On-card format | Unchanged |
| Benefit | High for repeated saves of the same Bank during one boot/mount; none immediately after boot, remount, Autosave recovery, a different target, or force-save |
| Risk | Medium; source authority, direct-copy invalidation, Pattern invalidation, and in-flight mutation ordering must all be exact |

This is worth implementing after Option 1, with the UI meaning stated as
“avoid rewriting card-verified clean selected Scenes during this session.” It
must not be presented as a cross-boot content comparison and must retain a
force-save path.

## Option 3: retain existing clusters during legacy overwrite

The legacy Save path currently recursively deletes each selected Scene and
recreates all ten files. Opening a file with the present write/truncate path
also releases its old cluster chain before rewriting. On a 32-KiB-cluster card,
that produces repeated FAT free/search/allocate cycles even though every normal
Scene file fits in its existing first cluster.

### Current code locations to inspect

| Current location | Why it matters |
|---|---|
| `Core/Hardware/SD/filesystem.c:13563-13839` | Bank-child cleanup scans the parent, requires one unambiguous matching directory, then calls `afatfs_deleteTree()`. Reuse its slot/duplicate/layout validation ideas for fast-path eligibility, but do not enter the delete phase when the tree is canonical. |
| `Core/Hardware/SD/filesystem.c:14638-14858` | Shows the complete Bank Save child loop: select a mask bit, navigate, recursively delete that Scene, then delegate phase 8--37 to recreate it. Option 3 changes only the canonical-existing-child branch; fresh or suspect children retain this path. |
| `Core/Hardware/SD/filesystem.c:15165-15531` | The Scene writer creates the Scene directory and opens `sceneset`, Kit directory/`kitset`, six Instruments, Pattern, and Effects. The serializers and close ordering should be reused; only object acquisition changes from create/truncate to validated rewrite. |
| `Core/Hardware/SD/asyncfatfs/asyncfatfs.c:5587-5656` | Mode parsing documents that `"w"` means create plus truncate and that `"r+"` opens an existing file for read/write without that automatic truncate. This is the starting point, not a complete retained-rewrite solution. |
| `Core/Hardware/SD/asyncfatfs/asyncfatfs.c:4153-4185` | Existing nonempty files opened in exact write-only create mode are handed to `afatfs_ftruncate()`. This is the automatic cluster-release transition Option 3 must avoid. |
| `Core/Hardware/SD/asyncfatfs/asyncfatfs.c:3114-3257` | `afatfs_ftruncate()` immediately drops `firstCluster`, zeros logical/physical sizes, updates the directory, and walks the whole FAT chain to free it. It cannot be reused for retained-first-cluster rewriting. |
| `Core/Hardware/SD/asyncfatfs/asyncfatfs.c:1474-1479` | Normal writes only grow `logicalSize` with `MAX(oldSize, cursorOffset)`. Therefore `"r+"` plus seek/write alone leaves a stale tail when the new serialization is shorter. |
| `Core/Hardware/SD/asyncfatfs/asyncfatfs.c:1767-1807` and `:5271-5315` | Close writes the true `logicalSize` and `firstCluster` to the directory entry before releasing the handle. A retained rewrite needs an explicit exact-size finalization before this close path. |
| `Core/Hardware/SD/asyncfatfs/asyncfatfs.c:565-625` | `afatfsFile_t` already owns cursor, logical size, estimated physical size, directory entry position, and first cluster. Keep retained-allocation state on the file operation rather than duplicating FAT metadata in `filesystem.c`. |
| `Core/Hardware/SD/asyncfatfs/asyncfatfs.h:257-286` and `asyncfatfs.c:4485-4500` | The existing LFN rename API returns the post-rename short open name through caller storage. Any retained-rewrite path that renames a Scene, Kit, or Instrument must replace its old alias with this returned alias before the next open. |

The critical point is that merely replacing `"w"` with `"r+"` is incorrect:
it avoids truncation but preserves the old logical length for any shorter new
file. Conversely, calling the existing `afatfs_ftruncate()` afterward frees the
allocation that this option is intended to retain.

Implement a validated in-place fast path:

1. scan the existing Scene once and verify a canonical, unambiguous tree;
2. retain the openable alias for the Scene's four direct objects
   (`sceneset`, Kit, Pattern, Effects) and the Kit's seven objects (`kitset`
   plus six Instruments);
3. open each validated existing regular file read/write without invoking the
   current zero-length truncate, seek to offset zero, and run the existing
   serializer;
4. add an explicitly named AsyncFATFS exact-size finalize operation that sets
   logical EOF to the final cursor while retaining the first allocation, then
   let normal close publish that exact size;
5. rename canonical objects when display names change; and
6. fall back to the current delete/recreate path if anything is missing,
   duplicated, wrong-kind, noncanonical, or quarantined.

For the first version, constrain the retained path to files whose old logical
size and maximum possible new serialization both fit in one card cluster and
whose first cluster is valid. Every normal Scene member on the inspected card
meets that condition. Let AsyncFATFS enforce the cluster bound internally; the
filesystem facade should not infer FAT-chain geometry from file size. A later
general “retain prefix and free tail chain” operation is possible, but it is
not required to obtain the expected Bank overwrite gain and would broaden this
change unnecessarily.

The canonical tree scan should prove all of the following before the first
in-place write:

- exactly one Scene directory parses to the selected Bank child slot;
- exactly one `sceneset`, Pattern, Effects, and embedded Kit exist with the
  expected object kinds;
- exactly one `kitset` and one correctly typed file for each of the six
  Instrument slots exist inside the Kit;
- no malformed LFN, duplicate product identity, quarantine name, unexpected
  directory, or missing required object is present; and
- all regular-file logical sizes are inside their schema's accepted bound and
  the retained-one-cluster API accepts them.

This deliberately makes the fast path conservative. An unfamiliar host-edited
tree is not repaired in place; it goes through the already-audited replacement
path.

Do **not** implement this by casually changing the current `"w"` mode. It needs
a separately named AsyncFATFS operation with documented allocation and failure
semantics so an error cannot expose stale tail bytes or an incorrect file size.

Display-name changes also need care. Use the existing LFN rename operation's
returned open alias rather than retaining the pre-rename alias. Rename the
Scene/Kit/Instrument object only after its parent scan has proved there is no
same-product duplicate; otherwise fall back. Fixed system members such as
`sceneset.scg`, `kitset.kcg`, Pattern, and Effects normally need no rename.

Operation scratch needs eleven 8.3 aliases (`11 x 13 = 143` bytes), validity
bits, and cursors. Reserve **160 bytes SRAM1**. It can be placed in the existing
mutually exclusive 2,048-byte stage only if a size assertion proves it never
overlaps the Scene/Kit payload needed by that same write; otherwise use a
separate operation table. The conservative budget assumes a separate table.

| Property | Assessment |
|---|---|
| New SRAM1 | Up to 160 bytes |
| CPU | Lower FAT search/delete work; serializer unchanged |
| On-card format | Unchanged |
| Benefit | High for overwriting canonical existing Banks; no benefit for a fresh Bank |
| Risk | Medium to high; new filesystem write semantics and power-loss behavior require care |

This path should target a 30--60% overwrite-Save reduction on the test card,
subject to measurement. It cannot make a fresh 161-file Bank cheap. The current
delete-first implementation is already non-atomic at the Scene level; this
option must not make recovery worse. Close/size publication and final sync are
part of success, and abort must drain the active asynchronous operation.

An in-place write changes the failure shape: power loss can leave a partially
new file in an existing directory, whereas the current path can leave the whole
child missing after delete. That trade-off must be accepted explicitly. A
temp-file swap would improve per-file atomicity but would reintroduce allocation
and directory traffic, so it is a separate safety design, not part of this
speed option.

## Withdrawn Options 4--6

Options 4 and 5 (Kit/Scene packs and a whole-Bank A/B pack) are no longer active
recommendations. They reduce FAT object count, but they add a second storage
schema, migration rules, old-firmware compatibility decisions, and new
authority/recovery semantics. That is disproportionate while the current
format still contains the known traversal and overwrite waste addressed by
Options 1--3. Do not pursue pack files in Session 058.

Option 6 (hardware SPI plus DMA) is also withdrawn. The project's completed
hardware bring-up record is the authority:

- `knowledge_files/log_archive/010_SESSION_HANDOFF_LOG.md:95-106` audits every
  hardware SPI peripheral and concludes that the traced SD pins
  PC12/PD2/PC8/PD0 are the wrong pins for every free/usable SPI alternate-
  function mapping, the bit-bang path is the only board option, and it is fixed
  in PCB copper;
- the same log at `:181-195` records the working fast bit-bang implementation
  and repeats that hardware SPI is impossible;
- `knowledge_files/hardware_archive/HARDWARE_MAP.md:42-57` records the traced,
  working GPIO assignments and the bootloader's use of the same bit-bang path;
  and
- `MEMORY.md:960-964` preserves “hardware SPI is impossible” as current project
  guidance.

The archive does not record a separate native-SDMMC driver experiment, so this
proposal makes no new SDMMC pin claim. It does establish that the Option 6
hardware-SPI/DMA migration is invalid on the routed board. Treat that path as
closed unless new schematic/PCB evidence and a deliberate hardware decision
supersede Session 010. It is not an S058 speedup option.

## Options explicitly not recommended

### More AsyncFATFS cache sectors

The current eight sectors consume 4,096 data bytes. Each added sector costs
exactly 512 data bytes plus a 16-byte descriptor: **528 bytes per sector**.
Adding two costs 1,056 bytes; adding four costs 2,112 bytes. Cache lookup,
allocation, flush selection, and census paths linearly scan the cache entries,
so expansion also adds CPU work.

Session 057 proved that the operation was not exhausting handles; it did not
identify cache capacity as the limiting resource. For speed, a larger cache
cannot eliminate 161 creates, closes, directory entries, or cluster
allocations. It is therefore the wrong SRAM trade.

### Larger foreground burst or line budgets

Increasing `SDCARD_BURST_SIZE` or the 16-character text parse budget can shorten
the number of foreground ticks only by taking a larger contiguous CPU slice.
That violates the CPU constraint and risks audio/UI latency. Removing redundant
directory/FAT work while retaining the current budgets is the applicable path
on this hardware.

### Pre-serialize every legacy text file

Instrument Save already writes generated lines as blocks. Building a complete
1.4-KiB text image would spend SRAM and concentrate formatting CPU without
removing its open, close, directory entry, or cluster. Buffered input is useful;
whole-file legacy output staging is not.

### Compression

The payload is already tiny; cluster and metadata count dominate. Compression
would add CPU and format complexity without removing the legacy directory and
file transactions that dominate this operation.

## Recommended implementation sequence

### Stage 1: low-risk, legacy-compatible speedup

Implement Option 1 in one focused change:

- dedicated HCNAMES mirror;
- one-pass Bank child-name capture;
- parent-retaining delegated Scene return;
- direct `.hcindex` cache update with scan fallback; and
- one bounded block read into the existing 512-byte buffer at the unchanged
  16-character/byte budget.

Reserve **1,320 bytes SRAM1**. Add compile-time size assertions for the 1,161-
and 144-byte arrays and comments next to every allocation/state branch stating
its owner, lifetime, inputs, outputs, and why the allocation/navigation split
must exist.

### Stage 2: session-scoped clean-Scene skipping

Implement Option 2 as a separate, reviewable authority change. Its additional
state is eight volatile bytes. The essential gate is not merely that a Scene
has no edits: the bit must have been established by a successful Bank child
Load/Save against the same Bank slot during the current boot and current mount.
Cold boot, remount, Autosave recovery, different-source loads, identity changes,
and all musical edits invalidate the appropriate authority. Retain an explicit
force-save route.

### Stage 3: improve legacy overwrite Save

If old-firmware and host-editable tree compatibility is required, implement
Option 3. Together with Stages 1 and 2's card-clean state, the conservative new
SRAM ceiling is:

```
1,314 traversal/cache + 8 card-clean state + 160 rewrite table = 1,482 bytes
```

Reserve at most **1.5 KiB SRAM1** for the complete legacy-preserving package.
No AsyncFATFS sector-cache or handle-pool expansion is needed.

## Verification and acceptance plan

Use lifecycle timing around existing requests and completion; no additional
diagnostic layers are required to implement these options. Benchmark the same
card and Bank before and after each stage.

Required matrix:

- full 16-Scene Load;
- one-Scene and full 16-Scene Save;
- fresh target versus canonical overwrite;
- sparse and full child masks;
- a fragmented/aged card as well as a freshly formatted card;
- repeated same-Bank saves during one powered/mounted session;
- cold power cycle before the next save, proving no clean bit survives;
- card removal/remount in one power cycle, proving all clean authority clears;
- a future Autosave-recovery boot, proving recovered Scenes start non-clean;
- root Scene/Kit/Instrument/Pattern loads and name changes, proving affected
  Scene bits clear; and
- mutation during a Bank child write and during final flush, proving that child
  is not published clean.

Acceptance invariants:

- no increase to SD or text-parser foreground byte budgets;
- no regression in audio interrupt timing or playback behavior;
- no live AsyncFATFS callback when the facade publishes completion/error;
- exact preservation of unselected Bank Scenes;
- HCNAMES and `.hcindex` remain coherent after success, failure, card removal,
  and remount;
- legacy duplicate/casefold winner behavior remains identical under Option 1;
- Stage 1 performs one Bank child scan and no per-child Bank rescan;
- Stage 1 performs no root/Bank reopen pair between successful child Scenes;
- Option 2 never skips a Scene after boot/remount/Autosave recovery until that
  exact Bank child has been loaded or saved successfully in the current mount;
- Option 2 uses the completed effective Bank Load mask, never the requested or
  present mask, to establish clean bits;
- failed Save, force-save, different target, direct-copy mutation, Pattern
  mutation, and identity mutation all preserve/restore the required dirty
  result;
- retained-cluster writes publish correct logical sizes and never expose stale
  tails; and
- any noncanonical or unsupported existing tree takes the current
  delete/recreate fallback without partial in-place modification.

## Final recommendation

Allocate the slight SRAM increase where it removes known redundant I/O:
**1,320 bytes for Option 1**, with an optional **8 bytes** for session-scoped
card-clean authority and **160 bytes** for a later legacy retained-cluster
rewrite. This is a hard proposed ceiling of **1.5 KiB SRAM1** for the complete
legacy-compatible speed package.

Do not expand the AsyncFATFS cache, change the file format, revive the rejected
hardware-peripheral path, or increase CPU burst budgets for this work. The
active sequence is Options 1, 2, and 3: first remove redundant traversal;
second, skip only card-verified clean Scenes within the current boot/mount; and
third, add a conservative retained-one-cluster overwrite fast path.
