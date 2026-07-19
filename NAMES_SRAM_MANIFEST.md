# SRAM object-name manifest

This manifest lists current SRAM-resident object names and name-like directory
cache entries for Instrument, Kit, Scene, and Bank operations. It separates
user-visible display names from filesystem open keys, because an LFN display
component and a FAT short alias are not interchangeable identities.

## Resident object names

| Scope | SRAM field | Capacity | Meaning |
|---|---|---:|---|
| Instrument slot | None in kit_instrument_slot_t | — | A resident Instrument slot stores its type and parameter images, but not an independent filename or display name. |
| Kit | kit.display_name | 9 bytes | Eight printable display cells plus NUL. Seeds Kit display and Save editing. |
| Kit, per Instrument slot | kit.instrument_display_name | 6 x 9 bytes | Eight-character visible Instrument source name/stem plus NUL. |
| Kit, per Instrument slot | kit.instrument_stem | 6 x 17 bytes | Up to 16 characters of the source filename stem plus NUL. Retained so later Kit Save can generate member filenames. |
| Scene | scene.display_name | 9 bytes | Eight printable Scene display cells plus NUL. |
| Bank | bank_display_name | 9 bytes | Eight printable Bank display cells plus NUL. |
| Save editor | preset_currentName | 8 bytes | Current editable Load/Save UI field. This is UI state, not an object-owned retained name. |

The resident name fields live inside each resident Scene's embedded Kit or
Scene record, except BankData's single workspace Bank display name. There are
16 resident Scene slots.

## Root directory browser caches

Each numbered root library cache covers slots 000 through 999. Presence arrays
are retained beside these names and determine whether a cache entry is valid.

| Browser | Visible display cache | Open-key cache | Capacity |
|---|---|---|---:|
| Kit | kit_slot_name[1000][9] | kit_slot_open_name[1000][13] | 1,000 slots |
| Scene | scene_slot_name[1000][9] | scene_slot_open_name[1000][13] | 1,000 slots |
| Bank | bank_slot_name[1000][9] | bank_slot_open_name[1000][13] | 1,000 slots |
| Root Instrument pool | `fs_list_cache_name[128][9]` | None retained | 128 entries total; active type tracked separately |
| Root Instrument pool | `fs_list_cache_type` + count | — | 1 active type and 1 shared entry count |

The root Instrument browser retains only one generalized display-name cache of
128 entries and one count. The cache is tagged with the currently loaded
Instrument type; changing type or exiting nested Instrument Load/Save disposes
it, and entering either nested menu reloads the selected type's `.hcindex`.
FAT short aliases are operation-local values, and the longer source stem used
after a successful load is staged metadata; neither is retained as a per-entry
browser cache.

The Kit and Scene open-key arrays normally retain a FAT short alias suitable
for the associated low-level open path. The Bank cache deliberately retains
the LFN display component as its later LFN open key: substituting a generated
short alias into that display-match API caused the historical BnkL06 failure.

The current Instrument registry has Drum, Snare, Cymbal, and HiHat types. Its
typed browser cache is therefore bounded per type rather than sharing the
numbered Kit/Scene/Bank slot arrays.

## Load and save operation scratch

Only one filesystem operation runs at a time. These buffers are transient
operation state, not browser caches or resident object ownership.

### Generic and Instrument scratch

- loaded_name[9]: result for generic name browsing.
- kb_kitName[9]: legacy Kit browser display copy.
- op_staged_instrument_display_name[9] and
  op_staged_instrument_stem[17]: validated root Instrument Load metadata before
  Preset commits the staged Instrument.
- op_instrument_save_display_name[AFATFS_LONG_FILENAME_MAX + 1] and
  op_instrument_save_open_name[AFATFS_SHORT_FILENAME_MAX]: captured root
  Instrument Save target and returned short alias.

### Kit and Scene scratch

- op_save_kit_dir_display_name and op_save_kit_dir_open_name: requested Kit
  directory display component and returned short alias.
- op_save_kit_member_display_file[6][STORAGE_KIT_MEMBER_FILENAME_MAX]:
  captured member Instrument filenames for an in-flight Kit Save.
- op_save_scene_kit_display_name and op_save_scene_kit_open_name: embedded Kit
  directory identity while writing a Scene payload.
- op_scene_display_name[9]: root or Bank-local Scene name for the delegated
  Scene loader.
- op_scene_child_display_name[9] and op_scene_child_open_name[13]: discovered
  embedded Kit display/open name.
- op_scene_pattern_open_name[13] and op_scene_effect_open_name[13]: discovered
  child pattern/effects filenames.

The four Scene child-discovery buffers above must be cleared before every
delegated Bank-local Scene. Their lifetime is one directory payload. Retaining
child 00's Kit, pattern, or effects name for child 01 was the cause of ERR
BnkL14 and is now prevented by filesystem_resetSceneLoadChildDiscovery().

### Bank scratch

- op_bank_display_name[9]: selected/current Bank display name.
- op_bank_child_name[16][9] and op_bank_child_open_name[16][13]: discovered
  Bank-local child Scene display/open names for the selected Bank.
- op_save_bank_dir_display_name: final numbered Bank display component.
- op_save_bank_tmp_display_name: unique non-numbered staging sibling.
- op_save_bank_old_display_name: unique non-numbered backup/old sibling.
- op_save_bank_dir_open_name and op_save_bank_rename_open_name: short aliases
  returned by create/promotion operations.

The temporary and old Bank names support the current staging/promotion flow.
They are not a durable transaction journal and must not be treated as a
power-loss recovery protocol.

### Legacy delete-walker scratch

The compatibility recursive-delete walker retains display and short-alias names
in op_delete_tree_name_stack, op_delete_tree_open_name_stack,
op_delete_tree_child_name, and op_delete_tree_child_open_name. Current
same-slot cleanup prefers native afatfs_deleteTree with a captured
afatfsObjectId_t; these buffers remain for the fallback walker.

## Not included

Static descriptor metadata such as file_key, short_name, long_name, category,
and Instrument type labels is program metadata, not an object-specific SRAM
name cache. The FAT object iterator's afatfsObjectInfo is operation scratch;
it carries a display component and short alias only while a scan is active.
