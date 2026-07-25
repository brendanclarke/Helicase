# Final Cache + Names SRAM Manifest

## Scope and evidence

This is the current, source- and linked-image-derived manifest for caches,
display names, and filename/file-key storage. It is deliberately an audit, not
a claim that every historical cache has already been disposed.

Evidence used:

- `build/lxr02.elf`, rebuilt and packaged at Session 042 closeout
  (`text 373328`, `data 408`, aggregate `bss 259368`)
- `arm-none-eabi-nm -S --size-sort build/lxr02.elf`
- Current definitions and call paths in `filesystem.c`, `filesystem.h`,
  `menu.c`, `presetManager.c/.h`, `BankData.c`, `storageTypes.c/.h`, and
  `asyncfatfs.c/.h`

Definitions:

- **Resident identity** is an active Bank/Scene/Kit/Instrument display name
  kept across menu work.
- **Cache** is a multi-entry list retained for later lookup/traversal.
- **Operation scratch** is a request-local string/alias that must survive
  foreground-pumped asyncfatfs phases. It is not an authoritative resident
  name or a menu cache.

## Contracted musical-name storage

| Storage | Bytes | Count / content | Status |
|---|---:|---|---|
| `bank_display_name` in `BankData` | 9 | 1 Bank name, 8 characters + NUL | Required; sole physical Bank-name copy |
| `fs_identity_name` | 72 | 1 Scene + 1 Kit + 6 Instruments, each 8 characters + NUL | Required |
| **Active musical identity total** | **81** | **1 Bank, 1 Scene, 1 Kit, 6 Instruments** | **Exactly the requested contract** |

`FS_IDENTITY_BANK_ROW` is a logical API row only: it aliases
`bank_display_name`. It does not allocate another 9-byte Bank string.

There is no per-Scene name array, per-Kit name array, Instrument stem array,
or retained file-key array in `scene_t`/`kit_t`.

## Contracted general-purpose cache and staging

| Storage | Bytes | Purpose | Status |
|---|---:|---|---|
| `fs_list_cache_name[1000][9]` | 9,000 | The sole `.hcindex` or `.hcnames` cache; one domain at a time | Required |
| `fs_stage_workspace` | 2,048 | Aligned non-Pattern payload stage: Kit, Scene-with-Kit, or one Instrument candidate | Required staging, not a name/key cache |
| `staging_buf` | 512 | Existing streaming bulk-write buffer | Required stream scratch, not a payload/name cache |
| asyncfatfs `openFiles[5]` | +656 vs. the original three-handle configuration | Five concurrent asyncfatfs file-handle slots | Accepted capacity increase |

The 9,000-byte cache and 2,048-byte stage are deliberately separate. An
Instrument/Kit parser must never overwrite the active `.hcindex` rows used by
the browser.

### 2026-07-25 Instrument Load temporary `kit` source

The former original-Instrument stage snapshot is disposed. The 2,048-byte stage
contains only one Instrument parser candidate. Normal Instrument Load adds one
explicit, user-approved **9-byte Menu temporary name** (`menu_instrumentTempName`)
while its matching `.hctmp.<ext>` file is valid in one Instrument type folder.
That name is not HCNAMES, an index row, or a file key; it is cleared on
voice/type/mode/Scene/exit invalidation. The hidden file is excluded from every
typed `.hcindex` scan and does not alter HCNAMES.

### Instrument Load `kit` ownership

The reversible `kit` row does **not** retain a second Instrument parameter
image in SRAM:

- Menu entry, or a voice change, saves the current voice to the exact hidden
  file `Instrument/<type>/.hctmp.<ext>`.
- `fs_stage_workspace` holds only the one Instrument file currently being
  parsed, whether that source is a numbered pool row or `.hctmp`.
- `menu_instrumentTempName[9]` retains the original eight-cell display name
  shown beside `kit`; this is the one explicit name allocation outside the
  resident 81-byte identity contract.
- Numbered pool text is read from the active `.hcindex` cache and converted to
  a stem for display, so the instrument extension never enters identity SRAM.
- Menu invalidates the temporary label on Scene, voice, instrument type, load
  type, mode, or nested-menu exit. The dirty `.hctmp` file may remain on SD,
  but scans, repair, and `.hcindex` generation exclude it.

## File keys and filename handling

There are no retained per-slot file keys. A normal Instrument leaf is derived
on demand from:

```text
eight-cell HCNAMES stem + instrument type extension + slot context
```

Instrument `.hcindex` rows contain raw filenames for opening files. Menu and
HCNAMES identity use `filesystem_copyInstrumentDisplayName()` to take only the
stem, so `.snr`, `.drm`, etc. never appear in a short name field.

### Allowed / necessary asynchronous operation-local scratch

These are not long-lived identities or lookup caches. Each must remain valid
while asyncfatfs consumes an open/create/rename request.

| Storage class | Size each | Examples | Why it exists |
|---|---:|---|---|
| Derived filename component | 49 B | `op_filename_component` | One Kit/Scene Instrument member leaf at a time |
| Immutable Instrument Save target | 49 B | `op_instrument_save_display_name` | Root Instrument Save target remains stable while asynchronous write phases run |
| Directory/rename display components | 49 B | `op_save_*_display_name`, `op_repair_*_name` | A create/rename sequence needs a request-stable argument; a rename may require source and destination simultaneously |
| 8.3 open aliases | 13 B | `op_*_open_name` | asyncfatfs returns/needs a short alias for later exact reopen |
| FAT LFN iterator scratch | 80 B | `op_lfn_name` | One directory-entry decode buffer |
| Streamed text line scratch | 160 B | `op_line_buf` | One parser line; does not hold an entire file |

These scratch buffers are operation-local and should not be counted as another
Bank/Scene/Kit/Instrument identity copy. They are still candidates for future
consolidation only where asyncfatfs lifetime rules permit it.

## Completed exception disposal — 2026-07-24

The former exceptions are now removed from the linked image. No replacement
name/key cache was introduced.

| Former storage | Freed linked BSS | Replacement / result |
|---|---:|---|
| `op_bank_child_name[16][9]` | 144 | No array. Bank Load rescans the selected parent and retains only one selected child in existing `op_scene_display_name` scratch. |
| `op_bank_child_open_name[16][13]` | 208 | No array. `storage_formatBankSceneDir()` derives the exact one-child LFN component immediately before `afatfs_opendir_lfn()`. |
| `op_bank_child_present[16]` | 16 | No array. The existing 16-bit `op_bank_child_present_mask` remains as occupancy/mask state, not a name/key cache. |
| `fs_test_file_name[64][49]` | 3,120 | Retired. File diagnostics are no longer offered by Menu; compatibility calls are zero-work and retain no list. |
| `fs_test_dir_name[64][49]` | 3,120 | Retired. Directory diagnostics are no longer offered by Menu; compatibility calls are zero-work and retain no list. |
| Firmware recursive delete stacks/buffers | 558 | Retired. Slot cleanup delegates recursion to `afatfs_deleteTree()` and retains only its completion result latch. |

The named removals account directly for 7,160 bytes: 368 B of Bank child
arrays, 6,240 B of diagnostic lists, and 558 B of recursive-delete scratch
(the first subtotal includes the 16-byte occupancy array that was replaced by
an already-required bitmask). Intermediate builds moved further as adjacent
state was revised, so those removals must not be reverse-derived from one old
whole-image total. The authoritative closeout image reports aggregate BSS
**259,368 B**; its physical region split is recorded in
`knowledge_files/specification_reference/SRAM_DTCM_MANIFEST.md`. The retained
asyncfatfs delete completion latch is scalar operation state, not a cache.

## Explicitly disposed storage

The following former storage does not remain as persistent SRAM:

- Per-Scene Kit display name.
- Per-Scene six Instrument display names.
- Per-Scene six Instrument filename stems/keys.
- Menu seven-name scratch array and separate 16-Scene name cache.
- Any per-Instrument-type 1,000-entry name cache.
- Duplicate physical filesystem Bank identity row.
- Six retained Kit member filename components in resident Scene/Kit state.

`storage_kitset_t op_kitset` still contains six 49-byte `file=` cells while
one Kit or Scene manifest is actively parsed. Its linked size is 312 bytes.
Those strings are request-local schema state needed to open the six member
files; they are overwritten for the next parse and are never copied into
`scene_t` or `kit_t`, so they are not retained resident file keys.

## Residual linked diagnostic UI state

The two former 64-entry filesystem diagnostic caches are gone, and the
File/Dir operations are no longer reachable from the normal Load/Save type
cycle. However, `menu.c` still links the compatibility editor/result display
state:

| Storage | Bytes | Status |
|---|---:|---|
| `menu_testEditName[49]` | 49 | Unreachable File/Dir name editor |
| `menu_testResultName[49]` | 49 | Unreachable File/Dir result text |
| result bytes/timer/kind/flags | 9 | Four result bytes, 16-bit timer, and three one-byte fields |
| **Residual diagnostic UI total** | **107** | **Not a cache; still linked** |

These objects are not musical names and not multi-entry caches. They are
nevertheless real SRAM and must not be described as disposed. Removing them
requires deleting the remaining compatibility UI path rather than changing
the musical-library cache contract.

## Bottom line

The active musical name contract is now exact:

```text
9,000 B  one general-purpose .hcindex/.hcnames cache
2,048 B  one non-Pattern staging workspace
   81 B  one Bank + one Scene + one Kit + six Instrument names
```

The separate 512-byte streaming writer buffer and five asyncfatfs handles are
not caches. The requested Bank child arrays, diagnostic list caches, and
firmware delete stacks are disposed. The 107 bytes of unreachable diagnostic
UI state disclosed above remain linked outside that disposed exception list.
