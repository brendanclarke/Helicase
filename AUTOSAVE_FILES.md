# Autosave File Pair — first implementation pass

## Goal and boundary

This is a deliberately small first pass.  Once the SD card has finished the
normal boot filesystem work and the initial Bank/Scene/Kit fallback has
finished, ensure that two root-level autosave register files exist for the
last Bank context.

The requested root filenames are, verbatim:

```text
/.hcprms1
/hcprms2
```

They are two different files.  This plan deliberately preserves the missing
leading dot on `hcprms2` rather than silently correcting it.

This pass creates only a **name-shaped, all-zero delta baseline**.  It does
not restore an autosave, compare bytes, set replacement-mask bits, mark edits,
write changed parameters, choose a ping-pong winner, or modify any committed
Bank/Scene/Kit/Instrument file.  It also must not change DSP state, Menu
state, HCNAMES, library indexes, or `settings.cfg`.  It reads HCNAMES only to
copy its names into a newly created register file.

Autosave is inactive when no Bank has loaded successfully.  In that case this
boot step makes no filesystem request and creates neither file.  When a Bank
has loaded, its validated `bank_restoreBankSlot()` value is written as the
two-byte Bank Slot Number.  This plan uses little-endian solely as the local
wire convention; no behavior in this milestone depends on that choice.

The existing `Autosave.c/.h` and `AUTOSAVE_IMPLEMENTATION.md` describe a
rejected sector-record/dirty-ledger experiment.  They are not an implementation
base for this work.

## Fixed file geometry

Each file is exactly **23,184 bytes**.  All offsets below are byte offsets from
the beginning of one file.

| Range | Bytes | Contents on this pass |
| --- | ---: | --- |
| `0x0000..0x0A0F` | 2,576 | replacement mask: 1 bit for each following byte, all zero |
| `0x0A10..0x0A8F` | 128 | Bank section |
| `0x0A90..0x0F8F` | 1,280 | Scene 00 |
| `0x0F90..0x148F` | 1,280 | Scene 01 |
| `...` | `14 × 1,280` | Scenes 02 through 15, contiguous |
| `0x5590..0x5A8F` | 1,280 | Scene 15 |

The arithmetic is exact:

```text
2,576 mask bytes × 8 bits = 20,608 covered bytes
128-byte Bank section + (16 × 1,280-byte Scene sections) = 20,608 bytes
2,576 + 20,608 = 23,184 bytes
```

The mask covers every byte after it, including every padding byte.  A zero bit
means “leave the committed Bank byte untouched” in the later reader.  Therefore
the initially populated names are descriptive only: they are **not** an
autosave overlay until a future writer sets their mask bits.

### Bank section (128 bytes)

| Offset within section | Bytes | Field |
| ---: | ---: | --- |
| 0 | 2 | Bank Slot Number, little-endian |
| 2 | 8 | Bank Name, fixed eight cells, zero-padded |
| 10 | variable | ordered Bank parameter bytes (all zero this pass) |
| final bytes | to 128 | zero padding |

### Scene section (1,280 bytes)

| Offset within scene | Bytes | Field |
| ---: | ---: | --- |
| 0 | 8 | Scene Name, fixed eight cells, zero-padded |
| 8 | variable | ordered Scene parameter bytes (all zero this pass) |
| final bytes of first block | to 128 | zero padding |
| 128 | 1 | Effect type, zero |
| 129 | 8 | Effect Name, zero |
| 137 | 119 | Effect parameters/padding, zero |
| 384 | 8 | Kit Name, fixed eight cells, zero-padded |
| 392 | variable | ordered Kit parameter bytes (all zero this pass) |
| final bytes of Kit header | to 512 | zero padding |
| 512 | 128 | Kit Instrument 0 |
| 640 | 128 | Kit Instrument 1 |
| 768 | 128 | Kit Instrument 2 |
| 896 | 128 | Kit Instrument 3 |
| 1,024 | 128 | Kit Instrument 4 |
| 1,152 | 128 | Kit Instrument 5 |

The Kit occupies 896 bytes: its 128-byte header plus six 128-byte Instrument
records.  Together, Scene header (128), Effect block (256), and Kit (896) give
the required 1,280-byte Scene section.

### Instrument record (128 bytes)

| Offset within Instrument | Bytes | Field |
| ---: | ---: | --- |
| 0 | 3 | type by extension, zero this pass |
| 3 | 8 | Instrument Name, fixed eight cells, zero-padded |
| 11 | 72 | parameter bytes, zero this pass |
| 83 | 45 | zero padding |

Future parameter bytes follow the selected Instrument type's descriptor/parameter
enum order: byte N is descriptor index N.  The present retained image has
`INSTRUMENT_PARAM_COUNT == 64`, so indices 0..63 have a current owner and the
remaining eight reserved bytes remain zero until the parameter enum expands.
This first pass writes all 72 bytes as zero.

## Name sources and padding

The file must not add a second resident name store.  The Bank name comes from
the existing `BankData` owner.  All sixteen Scene names, sixteen Kit names,
and 96 Instrument names come from the authoritative fixed rows of
`/.hcnames`, read into the existing shared 9,000-byte filesystem name cache
for this one boot operation.  The cache is then released normally.

For every name field, copy at most eight printable source cells, stop at the
source NUL/trimmed end, and fill the remaining cells with `0x00` (not the
space-padded in-RAM display convention).  All non-name bytes, including the
full mask, are `0x00` in this milestone.

## Proposed two-file handoff model

The two files are two complete, independent images: Record A is `/.hcprms1`
and Record B is `/hcprms2`.  A later write would
always build the next complete image in the inactive file while leaving the
currently accepted file untouched.  Only after the inactive image is fully
written, closed, and the normal filesystem flush gate reports success may it
become the candidate for the next boot.  This gives the useful ping-pong rule:
an interrupted write cannot destroy the prior complete image.

Two important limits follow from the requested fixed layout:

1. The replacement mask says which bytes to overlay; it does **not** say
   whether a file is complete or which record is newer.
2. FAT modification time is not an adequate commit selector, and selecting by
   a bare nonzero mask would accept a torn write.

Consequently, future ping-pong use will need an explicit commit protocol in
reserved Bank padding.  Its versioning, generation, integrity, and reader are
expressly outside this milestone.  Both initial files leave that padding zero;
neither is a current/winning record yet.

## Complete implementation map for this smallest change

Only the following files change.  No Menu, Preset, BankData, SceneData,
storageTypes, asyncfatfs, DSP, or RAM-manifest source changes are part of this
pass.

| File | Change | Why it must exist |
| --- | --- | --- |
| `AUTOSAVE_FILES.md` | This document becomes the fixed wire-layout and boot-order contract. | The byte positions, all-zero policy, and no-Bank gate must be reviewable before any writer exists. |
| `Core/Bank/Scene/Autosave.h` | **Replace the rejected header completely.** Define only the two filenames, fixed byte counts/offsets, HCNAMES row counts, and a narrow pure formatting API for an initial 512-byte chunk. Remove all dirty-ledger, payload, CRC, barrier, and mutation APIs. | The format needs one canonical definition. Removing the prior API prevents the rejected background-autosave design from being accidentally linked or called. |
| `Core/Bank/Scene/Autosave.c` | **Replace the rejected source completely.** Implement only the pure initial-chunk formatter declared by `Autosave.h`: zero the supplied range, place the little-endian Bank slot, and copy Bank/Scene/Kit/Instrument names at their fixed offsets with zero padding. It has no file handles, no filesystem calls, no static state, no reader, and no dirty state. | It isolates byte-layout arithmetic from the filesystem state machine while preserving the existing rule that `filesystem.c` is the only AsyncFATFS owner. |
| `Makefile` | Add `Core/Bank/Scene/Autosave.c` to the normal C source list. It is not currently compiled. | Replacing an unlinked rejected module is insufficient; the new pure layout formatter must link into the firmware used for this creation pass. |
| `Core/Hardware/SD/filesystem.h` | Add one boot-only public wrapper: `filesystem_ensureAutosaveFilesBlocking(void)`. Document that it returns without I/O when no resident Bank exists, otherwise creates only missing files and never rewrites an existing one. | `main.c` needs a narrow, truthful entry point and must not know AsyncFATFS phases, HCNAMES cache ownership, or record offsets. |
| `Core/Hardware/SD/filesystem.c` | Add one private `FS_INTERNAL_OP_ENSURE_AUTOSAVE_FILES` operation, its dispatcher case, its formatter call, and the blocking wrapper. Reuse existing operation fields, `staging_buf`, HCNAMES cache, finder, file handles, close callback, and flush gate; do not add retained storage. | The existing foreground-pumped facade serializes all card I/O and already supplies the required close-plus-`afatfs_sync()` durability boundary. |
| `main.c` | Immediately after successful `preset_loadGlobals()` completion and its `menu_pollPresetStatus()` acknowledgement, call the new wrapper only when `bank_hasResidentBank()` is true. Keep it before stage 14/`audioCodec_init()`. | At this point the Bank slot has been validated and loaded, HCNAMES has received the Bank-load update, and all pre-existing boot filesystem operations have completed. Audio is still off, so the bounded creation can be pumped without a runtime scheduling change. |

### Deliberately unchanged files

- `Core/Bank/BankData.c/.h`: already own `bank_hasResidentBank()`,
  `bank_restoreBankSlot()`, and the sole Bank display name.  This pass only
  reads them.
- `Core/Bank/Scene/SceneData.*`, `Preset/*`, and `Core/Menu/*`: no values are
  mutated and no user-visible autosave action exists.
- `Core/Hardware/SD/storageTypes.*`: the register is binary and no parameter
  serialization happens yet.
- `Core/Hardware/SD/asyncfatfs/*`: its existing open/write/close/poll/sync and
  root object iterator are sufficient; changing the filesystem library would
  expand this first test unnecessarily.
- `knowledge_files/specification_reference/SRAM_MANIFEST.md`: no static RAM
  allocation changes.  The implementation reuses existing SRAM1 objects only.

## Exact ownership and memory budget

The new source must add **zero bytes of persistent RAM** in every region:

| Resource | Existing owner | Initial-file use |
| --- | --- | --- |
| `staging_buf[512]` in SRAM1 | filesystem streaming operations | one sequential output chunk; not enlarged |
| `fs_list_cache_name[1000][9]` in SRAM1 | browser/HCNAMES cache | 129 HCNAMES rows while this boot operation runs; not enlarged |
| `op_file`, `op_phase`, `op_stream_index`, `op_item_offset`, `op_bytes_done`, `op_object_finder`, and close/open callbacks | filesystem operation state | existing fields reused for phase, selected file, root scan, and byte progress |
| stack | caller and formatter | only scalar counters/pointers; no record-sized, name-table, or sector-sized local buffer |

`Autosave.c` has no globals or static workspace.  The required new code adds
FLASH text/rodata only; it does not consume the SRAM reserved for Pattern or
DTCM reserved for delay lines.

## Filesystem operation, phase by phase

`filesystem_ensureAutosaveFilesBlocking()` is a thin boot wrapper.  It starts
the new internal operation, repeatedly calls the existing `filesystem_tick()`,
checks the normal terminal status, acknowledges it, and returns success/failure.
It does not create an independent blocking I/O path.

The internal operation performs these steps in this order:

1. **Eligibility gate.** Reject its own request without I/O unless
   `bank_hasResidentBank()` is true.  The call site also has the same gate.
   This makes fallback-only boot explicitly non-autosave rather than silently
   attaching records to slot 000.
2. **Read the full authoritative name register.** Return to root, borrow the
   existing 9,000-byte cache as `FS_NAME_CACHE_HCNAMES`, open `/.hcnames`
   read-only, and use the existing line reader/cache normalizer to read its
   129 logical rows.  Close the read handle.  A missing or unreadable HCNAMES
   is an error: correct all-Scene name population is impossible, and no
   autosave file is created from partial identity.
3. **Prove the first target is absent.** Scan root with the existing
   LFN-aware object iterator for the exact requested component `.hcprms1`.
   If it exists as any object, leave it untouched and advance to the second
   target.  This explicit scan, rather than a failed read-open followed by
   write-open, is what preserves the promise never to overwrite an existing
   record.
4. **Create only an absent target.** For a missing target, open it in root
   create/write mode.  Stream offsets `0..23,183` in existing 512-byte chunks.
   Before each `afatfs_fwrite()`, call the new pure
   `autosave_formatInitialChunk()` with the absolute offset, validated Bank
   slot, `bank_displayName()`, and the resident-name cache.  The formatter
   clears the complete chunk first, then inserts only its intersecting name or
   Bank-slot cells.
5. **Close before continuing.** Close the new file and wait for the close
   callback before beginning the second root scan.  The operation makes one
   normal `filesystem_finish(FS_STATUS_DONE)` call only after both targets have
   been preserved or closed; the existing `AFATFS` sync gate then flushes all
   creation work before boot continues.  This avoids inventing a nested or
   second filesystem operation merely to flush the first file.
6. **Repeat independently for `hcprms2`.** Re-scan root after Record A's
   completion.  Existing Record B remains byte-for-byte untouched; a missing
   Record B receives the same initial population.  The operation succeeds only
   after both files have either been preserved or newly created.
7. **Leave no autosave state behind.** Restore root, finish normally, and let
   the cache be reused by the next ordinary filesystem operation.  Do not set
   a selected record, dirty bit, generation, CRC, mask bit, or runtime flag.

The initial formatter has exactly these nonzero cells:

- Bank slot at record offsets 2,576 and 2,577;
- Bank name at 2,578 through 2,585;
- for each Scene `S`, Scene name from HCNAMES row `1 + S`, Kit name from row
  `17 + S`, and Instrument `I` name from row `33 + (S × 6) + I`;
- no other byte.

The HCNAMES cache stores display names as eight space-padded cells.  The
formatter trims only trailing spaces and replaces them with `0x00`; embedded
spaces are retained.  This yields the requested zero-padded wire names without
changing HCNAMES itself.

## Boot ordering change in `main.c`

The pre-audio sequence will become:

```text
Bank index/load/fallback ladder
  → completed Bank has written/updated HCNAMES
  → preset_loadGlobals()
  → menu_pollPresetStatus()
  → if bank_hasResidentBank(): filesystem_ensureAutosaveFilesBlocking()
  → boot stage 14
  → audioCodec_init()
```

No Bank means the third arrow is skipped.  This position deliberately does not
move the existing 50 ms SD holds, reload an index, or invoke `Preset` again.

## Acceptance checks for this implementation pass

1. Boot a card with a successfully loaded Bank and neither target file.  Verify
   `/.hcprms1` and `/hcprms2` each exist and are exactly 23,184 bytes.
2. Inspect each file: mask `0..2,575` is all zero; slot is the loaded Bank
   slot; the 129 name fields are zero-padded HCNAMES/Bank names; every other
   byte is zero.
3. Reboot unchanged.  Hash both files before and after; they must match
   byte-for-byte.  This verifies “exist means do not write.”
4. Delete only one file, boot, and verify that only the absent file is created
   and its existing partner hash is unchanged.  Run for each target.
5. Boot with no valid Bank but a Scene/Kit/default fallback.  Verify neither
   file is created or modified.
6. Simulate/observe a card error during HCNAMES read or target creation.  The
   operation must report a normal filesystem error, reach no audio-runtime
   code early, and never overwrite a target discovered as existing.

Still excluded after these checks: reader/overlay, replacement-mask setting,
parameter writing, effects, Pattern persistence, active-record selection,
generation/versioning, CRC, commit markers, debounce, dirty tracking, and all
runtime autosave I/O.

## Implementation record — 2026-07-29

The creation-only pass is now implemented exactly at the planned boundary.

- `Core/Bank/Scene/Autosave.c/.h` were fully replaced.  They now contain only
  fixed-format constants, compile-time geometry checks, and the pure
  `autosave_formatInitialChunk()` formatter.  The rejected dirty ledger,
  payload codec, CRC, barrier, and stage-workspace API are gone.
- `Makefile` now compiles the replacement formatter.  It has no global/static
  workspace; it operates on filesystem's existing 512-byte stream buffer.
- `filesystem_ensureAutosaveFilesBlocking()` starts one foreground-pumped
  state machine.  It reads HCNAMES into the existing cache, scans root with
  the LFN-aware iterator before each write-open, creates only a missing target,
  and reaches the ordinary final `afatfs_sync()` gate after both targets have
  been preserved or closed.
- `main.c` calls that wrapper after `preset_loadGlobals()` and its Menu
  acknowledgement, before `audioCodec_init()`, only when
  `bank_hasResidentBank()` is true.  Fallback-only boot remains autosave
  inactive and makes no request.
- The formatter writes the validated little-endian Bank slot, BankData Bank
  name, and HCNAMES Scene/Kit/Instrument names.  It clears every other byte,
  including all 2,576 mask bytes and all 72-byte Instrument parameter fields.
  Existing register objects are not opened for write.

Build verification completed with `make -j2`:

```text
text=354,332 B, data=400 B, bss=69,948 B
```

The linked BSS is unchanged from the current Session 044 baseline, confirming
that this pass consumed no new SRAM1 or DTCM capacity.  Hardware/card checks
in the acceptance list above remain required; no target SD-card test has been
performed in this implementation session.
