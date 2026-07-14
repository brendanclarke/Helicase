# Session 036 Handoff Log

DATE: 2026-07-14

SESSION GOAL: Repair the broken Kit Save foundation by expanding asyncfatfs to support exact-case long filenames for files and directories, prove it with basic File/Dir diagnostic menus, then restore Kit and Instrument load/save operations on top of the new filesystem behavior.

COMPLETED: asyncfatfs long-filename/case expansion, File/Dir diagnostics, Kit Save repair, `000` direct-slot correction, restored top-level Kit load/save reachability, and root Instrument Save.

VERIFIED ON HARDWARE: Yes, partially. The user tested File/Dir diagnostics and confirmed root file reads, root directory child reads, Save File, Save Dir, and re-load of firmware-created File/Dir objects. The user also confirmed Kit Save appeared good on hardware after the final filesystem repair, including saving `037 Slkty`. The final restored Kit/Instrument pass builds but still needs hardware retest.

## Changes This Session

- `Core/Hardware/SD/asyncfatfs/fat_standard.c/.h`: added FAT/VFAT filename helpers for short-name case flags, LFN checksums/fragments, ASCII case folding, display-name comparison, and LFN/SFN display conversion.
- `Core/Hardware/SD/asyncfatfs/asyncfatfs.c/.h`: expanded asyncfatfs with case-preserving, LFN-aware object APIs: LFN file/directory create/open, case-sensitive match modes, object iterator metadata, and returned 8.3 aliases.
- `Core/Hardware/SD/filesystem.c/.h`: added generic File/Dir diagnostics; moved Kit/Instrument scans/opens to asyncfatfs object/LFN APIs; repaired Kit Save; added root Instrument Save; preserved save completion flush boundary.
- `Core/Hardware/SD/storageTypes.c/.h`: updated root name policy, numbered folder parsing, saved instrument filename display behavior, and slot range semantics.
- `Core/Hardware/SD/kitBrowser.h`: expanded Kit browser capacity to 1000 direct slots.
- `Core/Menu/menu.c/.h`: added File/Dir diagnostic UI, repaired OK/OW overwrite indicator behavior, restored File/Dir/Kit type whitelist, made `000` display direct, and added nested Instrument Save from Save-page VOICE press.
- `Core/Scene/Preset/presetManager.c/.h`: added File/Dir diagnostic wrappers, root Instrument Save wrapper/completion, Kit Save acceptance return, and completion handling.
- `Core/Hardware/frontPanel/buttonHandler.c`: widened Instrument voice-button comments/behavior model for both Load and Save nested Instrument surfaces.
- `ASYNCFATFS_EXPANSION.md`: temporary work plan and implementation notes for asyncfatfs expansion. Details are preserved in this log and in `FILESYSTEM_SPEC.md`; the temporary file can be deleted later.
- `LOAD_SAVE_FOLLOWUP.md`: temporary work plan and implementation notes for restoring Kit/Instrument load/save. Details are preserved in this log and in `FILESYSTEM_SPEC.md`; the temporary file can be deleted later.
- `build/LXRV2_lxr02.img`: rebuilt firmware image changed as a build artifact.

## Known Issues Introduced

- No intentional new runtime issues are known from build/testing.
- Final restored Kit/Instrument UI pass is build-verified but not yet hardware-verified.
- The File/Dir diagnostics are still visible top-level entries and should remain until the next load/save promotion stage is finished.

## Known Issues Resolved

- Kit Save no longer depends on incomplete short-name-only directory creation.
- Kit Save no longer creates an empty folder because it cannot re-enter the visible long-name directory it just created.
- Occupied Kit Save opens the scan-cache identity instead of blindly creating a duplicate visible folder.
- The Load/Save OK/OW indicator now refreshes from `ok` to `OW` after overwrite checks instead of briefly showing `OW` then reverting to `ok`.
- `kitset.kcg` and future system filenames should preserve intended case through asyncfatfs instead of being forced uppercase by short-name conversion.
- `000` is no longer treated as missing/sentinel for numbered library slots.
- Root Instrument Save now exists as a production feature.

## Verification

Build verification:

- Ran `make`.
- Result: success.
- Existing nano-lib syscall warnings remain for `_close`, `_lseek`, `_read`, and `_write`; they are not new.
- Ran `git diff --check`.
- Result: clean.

Hardware observations from File/Dir diagnostics:

- `Load:[File] GLO.CFG` displayed `0x86 0x04 0x00 0x00`.
- `Load:[File] P013.PRF` displayed `0x54 0x72 0x70 0x74`; later checked against `SD_CARD_EXACT`, where the first file in sort order was a dot-prefixed object and the result was therefore correct.
- `Load:[File] P001.ALL` displayed `0x31 0x61 0x6C 0x6C`.
- `Load:[Dir] Kit old` displayed `0x00 0x00 0x00 0x01`.
- `Load:[Dir] samples` displayed `0x00 0x05 0x16 0x07`.
- `Save:[File] 1est.bin` displayed `0x31 0xC5 0xFC 0xFA`.
- `Save:[Dir] 1dstkbin` displayed `0x36 0x51 0x81 0x1D`.
- Earlier Save/File and Save/Dir tests created `0fut.bin` and `0dirnbin`, and reloading those objects showed the saved bytes.

Important interpretation:

- The filesystem must not hide dot-prefixed files/directories. Dot-prefixed objects are real FAT objects. Product-level scanners may filter by product naming/type rules, but asyncfatfs and the File/Dir diagnostic browsers must not suppress ordinary names beginning with `.`.
- The duplicated-line diagnostic display was corrected so byte results show bytes 0/1 on the top row and bytes 2/3 on the bottom row.

## asyncfatfs Expansion

### Policy Decisions

- The firmware needs both exact case preservation and case-sensitive lookup for new LFN APIs.
- FAT short names remain physically case-insensitive on disk, but asyncfatfs now preserves display case using `ntReserved` lower-case base/ext bits.
- VFAT LFN names preserve mixed-case display strings.
- New LFN APIs accept a match mode. Production Kit/Instrument code uses `AFATFS_MATCH_CASE_SENSITIVE`.
- Compatibility `afatfs_fopen()` / `afatfs_mkdir()` remain available for legacy short-name behavior.
- Names are still single path components. Path parsing such as `Kit/060 Smpty/kitset.kcg` is not an asyncfatfs API contract.
- ASCII/printable display names are the supported firmware subset. Unsupported/control characters are sanitized or rejected at higher layers as appropriate.

### New/Expanded asyncfatfs Concepts

- Short-name case preservation:
  - `fat_calculateFilenameCaseFlags()`
  - `fat_applyFilenameCaseFlags()`
  - FAT `ntReserved` lower-case base/ext bits.
- VFAT LFN support:
  - packed LFN entry representation;
  - LFN sequence constants;
  - checksum helpers;
  - fragment encode/decode helpers;
  - display-name comparison helper.
- LFN-aware object metadata:
  - display name;
  - short alias;
  - object kind: file, directory, none;
  - attributes;
  - whether an LFN chain was present.
- LFN-aware object iterator:
  - object scans skip deleted entries, VFAT fragments, volume labels, and structural `.` / `..`;
  - ordinary dot-prefixed names remain real objects;
  - VFAT fragments are accumulated and emitted only with the owning SFN entry;
  - invalid/mismatched LFN chains fall back safely to the SFN display.
- LFN-aware create/open:
  - `afatfs_fopen_lfn(displayName, mode, matchMode, openNameOut, callback)`;
  - `afatfs_mkdir_lfn(displayName, matchMode, openNameOut, callback)`;
  - these create VFAT display entries when needed, return an asyncfatfs-openable 8.3 alias, and handle files/directories through the same conceptual component boundary.
- Object scanning/opening uses case-sensitive matching where the caller requests it.

### Missing asyncfatfs Primitives

Still missing after Session 036:

- atomic rename/replace;
- safe temp-file promotion;
- recursive directory delete/replace;
- recursive directory copy/promotion;
- power-loss-safe dot-file autosave promotion.

Do not implement Scene/Bank autosave as one-off FAT mutations in callers. Add these primitives once at asyncfatfs/filesystem boundary before relying on them for `.tmp` or dot-file promotion.

## Diagnostic File/Dir Menus

Temporary diagnostics added to prove asyncfatfs behavior independently from product formats:

- `Load:[File    ]`: scans root files only, preserving display case. OK reads first four bytes of selected root file and displays them for two seconds.
- `Load:[Dir     ]`: scans root directories only, preserving display case. OK enters the selected directory, inspects the first alphanumerically sorted child object, and either reads first four bytes of a file or displays `Dir: <name>` for a child directory.
- `Save:[File    ]`: edits a root filename including extension, writes/overwrites four diagnostic bytes, then displays the saved bytes.
- `Save:[Dir     ]`: edits a directory name, creates/opens that directory, writes four diagnostic bytes to a same-name child file, then displays the saved bytes.

These diagnostics intentionally operate on exact filesystem objects, not product-specific filters. They must continue showing dot-prefixed files and directories.

## Kit Save Repair

The original failure was an incomplete filesystem foundation: Kit Save created the visible folder but then could not reliably re-enter it under the long-name/case convention, yielding empty folders or duplicate visible folders.

Final Kit Save behavior:

- Root `Kit` is created/opened through `afatfs_mkdir_lfn("Kit", AFATFS_MATCH_CASE_SENSITIVE, ...)`.
- Empty slots create visible `NNN Name` folders through `afatfs_mkdir_lfn()`.
- Occupied slots are opened by scan-cache identity, not recreated by typed display text, so saving into an occupied slot does not create a duplicate visible directory.
- Member instruments are written before `kitset.kcg`.
- Each member file is opened with `afatfs_fopen_lfn()` so mixed-case/spaces/long display names are preserved.
- Each member open returns the physical 8.3 alias that will be written into `kitset.kcg`.
- `kitset.kcg` is written only after the member aliases are known.
- Successful saves update Kit scan/browser cache using the actual created folder display/alias pair.
- Existing-slot saves overwrite authoritative child files but do not rename the folder as a side effect.
- Stale unreferenced files may remain in a Kit folder because recursive directory replace is not implemented. The loader ignores them because `kitset.kcg` is authoritative.

System filename policy:

- Firmware-created system files such as `kitset.kcg` should preserve lower-case spelling.
- Future system filenames named in specs should remain non-uppercase unless there is a specific compatibility reason.

## Numbered Slot Policy

Important correction made during the session:

- `000` is a real slot for all numbered filetypes.
- Numbered library slots are direct `000..999`.
- `STORAGE_KIT_MAX_SLOTS` and `STORAGE_SCENE_MAX_SLOTS` are 1000.
- `KITBROWSER_MAX_KITS` is 1000.
- Display format uses direct slot number, not `slot + 1`.
- `storage_parseNumberedFolder()` parses `000 Name` through `999 Name` into the same direct slot value.

This slot policy is separate from instrument file voice coordinates:

- Instrument files still use one-based voice numbers `1..6` internally for parser context, LFO `self` resolution, and file voice identity.
- Root Instrument Save intentionally passes `source_slot + 1` to `storage_formatInstrumentLine()`.

## Restored Load/Save Reachability

Top-level Load/Save type cycling is now whitelisted, not enum-contiguous:

```text
File -> Dir -> Kit -> File
```

Still compiled but not panel-promoted:

- Scene;
- Settings/Globals;
- Samples;
- KitMrp;
- Pattern;
- All;
- Performance;
- legacy Morph.

Rationale:

- File/Dir remain the proof surface for asyncfatfs.
- Kit is the first musical load/save surface restored on the new filesystem.
- Scene and Morph require deliberate redo and retest.
- Settings/Samples and legacy containers should be promoted only after their own focused pass.

`menu_resetSaveParameters()` now preserves the restored whitelist and clears nested Instrument mode instead of forcing all musical entries back to File.

## Kit Load/Save Restoration

Kit Load:

- Scans `Kit/` using asyncfatfs object iterator.
- Opens root `Kit` through `afatfs_opendir_lfn(..., AFATFS_MATCH_CASE_SENSITIVE, ...)`.
- Accepts object kind `AFATFS_OBJECT_DIRECTORY`.
- Product filtering remains in `storage_parseNumberedFolder()`.
- Cache stores display name and short alias from the asyncfatfs object.
- Loading a selected Kit folder opens from scan-cache identity.
- `kitset.kcg` and member files remain the authority for content.
- Kit Load can fan a staged Kit into selected resident Scenes by mask.

Kit Save:

- Uses repaired LFN-aware directory/file creation described above.
- `preset_saveDrumset()` now returns acceptance so Menu sets `menu_storageBusy` only after filesystem accepts the request.
- Save completion clears `menu_storageBusy`.
- The OK/OW overwrite display checks slot occupancy and shows `OW` when any data will be overwritten.

## Root Instrument Load/Save

Instrument Load:

- Root `Instrument` is now an exact display component, not `INSTRU~1`.
- Root open uses `afatfs_opendir_lfn("Instrument", AFATFS_MATCH_CASE_SENSITIVE, ...)`.
- Scan uses the asyncfatfs object iterator.
- Files only are accepted.
- Classification prefers the visible `object.displayName` extension and falls back to `object.shortName` for legacy alias-only media.
- Cache stores:
  - eight-character display stem;
  - retained 16-character source stem;
  - short alias for opening.
- Load still stages one file into private filesystem storage and Preset commits only after validation.

Instrument Save:

- Added `FS_INTERNAL_OP_SAVE_INSTRUMENT`.
- Added `filesystem_requestSaveInstrument(source_scene, source_slot, display_name, cb)`.
- Added `filesystem_saveInstrument_tick()`.
- Added `PRESET_OP_INSTRUMENT_SAVE`.
- Added `preset_saveInstrument(source_scene, source_slot, display_name)`.
- Save-page VOICE press enters nested Instrument Save.
- Nested Save screen shows `Save:[Type]` on the top row, eight editable filename cells on the bottom row, and `ok` at the right.
- OK writes one resident source voice to `Instrument/<stem.ext>`.
- SEQ buttons can change the source Scene while nested Save is active; the editable stem reseeds from the newly selected resident slot.
- The source Scene, source voice slot, source type, and target display filename are captured at request acceptance time.
- The writer creates/opens root `Instrument/` with `afatfs_mkdir_lfn()`.
- The target file opens with `afatfs_fopen_lfn()`.
- Instrument text is streamed by `storage_formatInstrumentLine()`.
- The saved file is added/updated in the Instrument browser cache using display name plus returned short alias.

## Morph Save Postponement

Do not treat Session 035 KitMrp/InstrumentMrp Load or the legacy `.SND` morph path as final Morph Save.

The desired future `Save:[KitMrp]` semantics remain:

- place `Save:[KitMrp]` between `Save:[Kit]` and `Save:[Scene]`;
- operate on root `Kit/`;
- save current interpolation parameters into normal kit/instrument `[params]` endpoints;
- save normal endpoints into `[morph]` endpoints;
- at morph 255 this effectively flips normal and morph endpoints for morphable parameters;
- non-morphable parameters remain whatever they are in the normal kit because unmorphable means unmorphable.

This must be reimplemented from the reset/current filesystem baseline, not as a patch over the failed early attempt.

## Scene Load/Save Postponement

Scene Load/Save code exists from earlier work but is not considered promoted after the asyncfatfs expansion.

Before Bank:

- redo root `Scene/` scan/load/save on the Session 036 asyncfatfs foundation;
- use direct `000..999` slots;
- open/create roots and directories through LFN-aware exact-case APIs;
- verify `sceneset.scg`, embedded `Kit <name>/`, `pattern.pat`, and `effect.fx`;
- preserve root `Scene/` as explicit library/pool, not autosaved workspace;
- keep Scene folder work separate from Bank autosave/dot-file work.

## Documentation Updates Required From This Log

This handoff preserves the important content from:

- `ASYNCFATFS_EXPANSION.md`;
- `LOAD_SAVE_FOLLOWUP.md`;
- earlier `KITSAVE_FIX.md` conclusions about long-name entry/re-entry failures;
- `MORPH_SAVE+OVERWRITE-FIX_AUDIT.md` direction that Morph Save needs a clean baseline implementation.

After this log, those temporary audit files may be deleted if the durable specs are also updated.

## Next Session Recommended Goal

Hardware retest of the final Session 036 restored surface:

1. Confirm File/Dir diagnostics still work.
2. Confirm `Load:[Kit]` appears and loads existing Kit folders, including slot `000`.
3. Confirm `Save:[Kit]` writes an empty slot and an occupied slot without duplicate folders.
4. Confirm saved Kit folders mount on desktop with visible mixed-case names and lower-case `kitset.kcg`.
5. Confirm VOICE press on Load page enters Instrument Load and loads all four types.
6. Confirm VOICE press on Save page enters Instrument Save and writes all four root Instrument types.
7. Power-cycle/card-mount and confirm saved root Instrument files exist and reload.

After that, redo Morph Save and Scene Load/Save before starting Bank.

## Blockers

- No Bank work should start until Morph Save and Scene Load/Save have been deliberately reworked and hardware-tested on the new asyncfatfs foundation.
- Dot-file/autosave work needs asyncfatfs rename/replace or safe copy/replace.
- Descriptor-aware step automation remains unfinished.

## Critical Reminders For Next Session

- `000` is real for every numbered library slot.
- Do not hide ordinary dot-prefixed files in asyncfatfs.
- Do not reintroduce local LFN parsing in product scans when asyncfatfs object iteration can supply display name, short alias, and kind.
- Product scanners may filter by product naming/type; asyncfatfs itself should not.
- Instrument file voice numbers are still one-based `1..6`; do not confuse them with root library slots.
- Top-level Load/Save is intentionally only File/Dir/Kit right now.
- Morph Save and Scene Load/Save must be redone before Bank.
- Save completion should not report success until close/flush/sync has drained.
- New code should keep detailed adjacent comments for function contracts, data structures, loops, and math.
