# SRAM object-name manifest

This manifest lists current SRAM-resident object names and name-like directory
cache entries for Instrument, Kit, Scene, and Bank operations. It separates
user-visible display names from filesystem open keys, because an LFN display
component and a FAT short alias are not interchangeable identities.

## Resident object names

| Scope | SRAM field | Size | Meaning |
|---|---|---:|---|
| Instrument slot | None in kit_instrument_slot_t | 0 bytes | A resident Instrument slot stores its type and parameter images, but not an independent filename or display name. |
| Kit, per resident Scene | kit.display_name | 9 bytes each; 144 bytes for 16 Scenes | Eight printable display cells plus NUL. Seeds Kit display and Save editing. |
| Kit, per Instrument slot | kit.instrument_display_name | 54 bytes per resident Kit; 864 bytes for 16 Scenes | Six 9-byte visible Instrument source names/stems plus NUL. |
| Kit, per Instrument slot | kit.instrument_stem | 102 bytes per resident Kit; 1,632 bytes for 16 Scenes | Six 17-byte source filename stems, including NULs. Retained so later Kit Save can generate member filenames. |
| Scene, per resident Scene | scene.display_name | 9 bytes each; 144 bytes for 16 Scenes | Eight printable Scene display cells plus NUL. |
| Bank | bank_display_name | 9 bytes | Eight printable Bank display cells plus NUL. |
| Save editor | preset_currentName | 8 bytes | Current editable Load/Save UI field. This is UI state, not an object-owned retained name. |

Resident object-name metadata totals 2,801 bytes: 2,784 bytes across the 16
resident Scene/Kit records, plus 9 bytes for BankData and 8 bytes for the Save
editor field.

The resident name fields live inside each resident Scene's embedded Kit or
Scene record, except BankData's single workspace Bank display name. There are
16 resident Scene slots.

## Root directory browser caches

Each numbered root library cache covers slots 000 through 999. For Kit, root
Scene, and root Bank, a non-blank row in the active shared cache is both the display name
and the slot-existence record; no parallel presence or per-slot open-alias
arrays are retained.

| Browser | Visible display cache | Open-key cache | SRAM size |
|---|---|---|---:|
| Kit | Shared `fs_list_cache_name[1000][9]` when Kit is active; non-blank row means present | None retained | 9,000 bytes, one physical cache |
| Scene | Shared `fs_list_cache_name[1000][9]` when root Scene is active; non-blank row means present | None retained; one-operation alias scratch only | 9,000 bytes shared, not additional |
| Bank | Shared `fs_list_cache_name[1000][9]` when Bank is active; non-blank row means present | None retained; one-operation root-key scratch only | 9,000 bytes shared, not additional |
| Generalized Instrument/Kit/Scene/Bank name pool | `fs_list_cache_name[1000][9]` | None retained in the name cache | 9,000 bytes total; Instrument uses sorted rows, numbered libraries use direct slot rows |
| Generalized cache tag | `fs_list_cache_kind` 1 + `fs_list_cache_type` 1 + count 2 | — | 4 bytes; one active domain, disposed on menu exit/type change |

The legacy Kit browser compatibility bridge is still allocated and is not a
second name cache: `kb_map[1000]` is 2,000 bytes of `uint16_t` slot numbers,
and `kb_numKits` plus `kb_mapIndex` add 4 bytes. Its map/control total is 2,004
bytes; `kb_kitName[9]` adds 9 bytes and is included in the generic scratch
section below. The complete bridge is therefore 2,013 bytes. It remains only
for older `kitBrowser.c` clients; it is the remaining candidate for removal if
that compatibility API is retired.

There is one generalized display-name cache, not one cache per instrument or
library type. Instrument uses the same storage as the full 1,000 sorted rows;
Kit, root Scene, and root Bank use that storage as 1,000 slot-addressed rows. Their `.hcindex` files are
also slot-addressed: each line contains only the name, including blank lines,
so the line number supplies `NNN` when a full folder key is needed. Changing a
Load/Save type or exiting the menu disposes the cache; entering Kit, root
Scene, or Bank reloads `/Kit/.hcindex`, `/Scene/.hcindex`, or `/Bank/.hcindex`,
while nested Instrument menus reload the selected typed index.

FAT short aliases are operation-local values, and the longer Instrument source
stem used after a successful load is staged metadata; neither is retained as a
per-entry browser name cache.

Kit and root Scene do not retain per-slot open-key arrays. Normal Load
constructs the visible `NNN Name` key from the shared cache; a Scene Load may
hold one returned FAT alias in operation-local scratch only while it reopens
the selected directory. Bank root loads similarly reconstruct `NNN Name` from
the shared Bank row and retain only operation-local root-key/alias scratch;
Bank-local child Scene names remain operation-local and are never part of the
root Bank index.

The removed dedicated Kit, root Scene, and root Bank arrays each previously
contained a 1,000-byte presence array, a 9,000-byte display-name array, and a
13,000-byte short-alias array: 23,000 bytes per library, 69,000 bytes total.
Those arrays are crossed off. The replacement generalized name array is 9,000
bytes, shared by all four library families and never multiplied by instrument
type. The separate 2,000-byte `kb_map` compatibility bridge remains as noted
above.

The current Instrument registry has Drum, Snare, Cymbal, and HiHat types. Only
one type is active at a time, but that active type can occupy all 1,000 rows;
changing type disposes the shared cache and reloads the selected `.hcindex`.

## Reconciled implementation checklist

- [x] One physical 1,000-row name cache serves Instrument, Kit, root Scene,
  and root Bank. No per-instrument-type or per-library name cache remains.
- [x] The old 128-entry Instrument limit is removed; the shared cache is
  9,000 bytes and accepts all 1,000 Instrument rows.
- [x] Kit, root Scene, and root Bank `.hcindex` files preserve slot order and
  blank rows, so the row number reconstructs the `NNN Name` directory key.
- [x] Kit/Scene/Bank entry and exit lifecycle disposes/reloads the one cache.
- [x] Successful Kit, Scene, and Bank saves rescan the physical directory and
  rewrite the corresponding `.hcindex` before the Save callback is released.
- [x] Boot generates the root Kit, root Scene, and root Bank indexes, then
  reloads Bank after Instrument index generation has disposed the shared cache.
- [x] Dedicated Kit, Scene, and Bank presence/name/alias arrays are removed;
  69,000 bytes of those obsolete arrays are crossed off.
- [ ] Remove the 2,013-byte legacy `kitBrowser` compatibility bridge. This is
  intentionally not crossed off because older `kitBrowser.c` clients still
  consume `kb_map` and `kb_kitName`.

## Load and save operation scratch

Only one filesystem operation runs at a time. These buffers are transient
operation state, not browser caches or resident object ownership.

### Generic and Instrument scratch

- `loaded_name[9]` (9 bytes): result for generic name browsing.
- `kb_kitName[9]` (9 bytes): legacy Kit browser display copy; this is separate
  from the 2,013-byte compatibility bridge listed above.
- `op_staged_instrument_display_name[9]` (9 bytes) and
  `op_staged_instrument_stem[17]` (17 bytes): validated root Instrument Load
  metadata before Preset commits the staged Instrument.
- `op_instrument_save_display_name[49]` (49 bytes) and
  `op_instrument_save_open_name[13]` (13 bytes): captured root Instrument
  Save target and returned short alias.

### Kit and Scene scratch

- `op_root_open_name[13]` (13 bytes): one generic returned short alias for the
  currently open root directory.
- `op_save_kit_dir_display_name[49]` and
  `op_save_kit_dir_open_name[13]` (62 bytes total): requested Kit directory
  display component and returned short alias.
- `op_save_kit_member_display_file[6][49]` (294 bytes): captured member
  Instrument filenames for an in-flight Kit Save.
- `op_save_scene_kit_display_name[49]` and
  `op_save_scene_kit_open_name[13]` (62 bytes): embedded Kit directory
  identity while writing a Scene payload.
- `op_scene_display_name[9]` (9 bytes): root or Bank-local Scene name for the
  delegated Scene loader.
- `op_scene_root_open_name[13]` (13 bytes): one returned FAT alias held only
  while a root Scene Load reopens the selected directory; this is not a
  1,000-slot cache.
- `op_scene_child_display_name[9]` and `op_scene_child_open_name[13]` (22 bytes):
  discovered embedded Kit display/open name.
- `op_scene_pattern_open_name[13]` and `op_scene_effect_open_name[13]` (26
  bytes): discovered child pattern/effects filenames.

The four Scene child-discovery buffers above must be cleared before every
delegated Bank-local Scene. Their lifetime is one directory payload. Retaining
child 00's Kit, pattern, or effects name for child 01 was the cause of ERR
BnkL14 and is now prevented by filesystem_resetSceneLoadChildDiscovery().

### Bank scratch

- `op_bank_display_name[9]` (9 bytes): selected/current Bank display name.
- `op_bank_child_present[16]` (16 bytes): Bank-local child occupancy flags.
- `op_bank_child_name[16][9]` (144 bytes) and
  `op_bank_child_open_name[16][13]` (208 bytes): discovered Bank-local child
  Scene display/open names for the selected Bank.
- `op_save_bank_dir_display_name[49]`, `op_save_bank_tmp_display_name[49]`,
  and `op_save_bank_old_display_name[49]` (147 bytes total): final numbered
  Bank display component plus unique staging and backup siblings.
- `op_save_bank_dir_open_name[13]` and `op_save_bank_rename_open_name[13]`
  (26 bytes): short aliases returned by create/promotion operations.
- `op_save_bank_scratch_attempts` and `op_save_bank_scratch_collision` (2
  bytes): bounded scratch-name collision state.

The temporary and old Bank names support the current staging/promotion flow.
They are not a durable transaction journal and must not be treated as a
power-loss recovery protocol.

### Legacy delete-walker scratch

The compatibility recursive-delete walker retains display and short-alias names
in `op_delete_tree_name_stack[8][49]` (392 bytes),
`op_delete_tree_open_name_stack[8][13]` (104 bytes),
`op_delete_tree_child_name[49]` (49 bytes), and
`op_delete_tree_child_open_name[13]` (13 bytes): 558 bytes total. Current
same-slot cleanup prefers native afatfs_deleteTree with a captured
afatfsObjectId_t; these buffers remain for the fallback walker.

### Shared parser and index-writer scratch

- `op_lfn_name[80]` (80 bytes): one physical LFN accumulator used while
  scanning a directory.
- `op_line_buf[160]` and `op_write_line_buf[160]` (320 bytes): streamed index
  and payload text lines; they are bounded line buffers, not retained name
  arrays.
- `op_library_index_kind` (1 byte),
  `op_save_index_refresh_kind` (1 byte),
  `op_save_index_refresh_pending` (1 byte), and
  `op_save_completion_callback` (4 bytes): 7 bytes controlling the
  boot-equivalent post-save scan/index chain. These fields were added so the
  original Save callback cannot run until the refreshed Kit/Scene/Bank index
  has been written and flushed.

The named operation scratch above is 2,124 bytes in total, including the
legacy delete walker and shared text buffers. It is statically allocated but
only one filesystem operation uses the relevant subset at a time.

## Not included

Static descriptor metadata such as file_key, short_name, long_name, category,
and Instrument type labels is program metadata, not an object-specific SRAM
name cache. The FAT object iterator's afatfsObjectInfo is operation scratch;
it carries a display component and short alias only while a scan is active.
