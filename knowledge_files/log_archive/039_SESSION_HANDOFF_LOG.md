# Session 039 Handoff Log

DATE: 2026-07-16

SESSION GOAL: Implement and harden root Scene and root Bank directory load/save on the Session 036-038 asyncfatfs foundation; fix hardware retest issues around Scene/Bank menus, retained names, scoped deletion, boot Bank load, and pattern payload persistence; move Scene under Bank in the source tree; gate diagnostic Load/Save entries behind Dev Mode; and preserve the audit details in durable logs/specs.

COMPLETED: Session 039 moved Scene-level routing/settings into SceneData, implemented root Scene Load/Save, added BankData and root Bank Load/Save with one Bank-local Scene bridge, changed boot to Bank-first fallback, fixed multiple hardware-reported Scene/Bank defects, replaced empty Scene/Bank pattern saves with a v2 draft 128x7 active-step payload, hid File/Dir/sDir diagnostics behind `CONFIG_DEV_MODE`, moved `Core/Scene/` under `Core/Bank/Scene/`, and updated generated SD fixtures/tools.

VERIFIED: `make`, `git diff --check`, and `make img` passed at the end of the session. Earlier passes also successfully compiled `tools/convert_legacy_kits.py` and `tools/populate_bank_directory.py`. The usual nano-libc syscall and LTO serial-compilation warnings remain.

## Code Layout At Closeout

The project source layout changed during this session:

- `Core/Scene/` moved to `Core/Bank/Scene/`.
- `Core/Scene/BankData.c/h` moved to `Core/Bank/BankData.c/h`.
- Makefile include/source paths now use:
  - `-ICore/Bank`
  - `-ICore/Bank/Scene`
  - `-ICore/Bank/Scene/Preset`
  - `-ICore/Bank/Scene/Pattern`
- `tools/convert_legacy_kits.py` was updated to read `Core/Bank/Scene/Preset/ParameterArray.h`.

The public prefixes did not change: `scene_*`, `pat_*`, `preset_*`, and `bank_*` remain the runtime API vocabulary.

## Scene Data Ownership

Scene now owns the per-voice mix settings that were previously kit-adjacent:

- `audio_out[6]`
- `fx_send_amount[6]`
- `fader_setting[6]`

`kitset.kcg` no longer requires or emits `audio_out`. Legacy `audio_out=` lines remain parse-only compatibility side data inside `storage_kitset_t`. Scene Load imports that legacy six-value block only when an old embedded Kit supplies it and the loaded `sceneset.scg` lacks explicit `audio_out`.

SceneData exposes getter/setter boundaries for the new fields:

- `audio_out` clamps to the persisted route domain `0..5`.
- `fx_send_amount` clamps to `0..127`.
- `fader_setting` clamps to `0..2`.

Preset applies active mixer routing from SceneData. UI/storage code mutates these fields through Preset/SceneData helpers rather than writing SceneData arrays directly.

## Scene File Shape

Root Scene folders are direct `000..999` library entries under `Scene/`:

```text
Scene/
  000 Slak/
    sceneset.scg
    Kit Slak/
      kitset.kcg
      <six instrument files>
    pattern.pat
    effects.fx
```

`sceneset.scg` stores Scene settings and never stores `name`. This is now a hard product rule: object names are owned by directory/file names. The only current file-level name registry exception remains the list of instrument member filenames in `kitset.kcg`.

The root Scene display name is captured from `Scene/<NNN Name>/`. The embedded Kit display name is captured from the `Kit <name>/` child directory. This matters because Save Scene and Save Kit character editors must seed from the resident object identity, not from the target slot cache and not from metadata inside files.

## Scene Load

Scene Load now validates and stages the whole Scene before committing resident memory:

- `sceneset.scg`
- embedded `Kit <name>/kitset.kcg`
- six embedded Instrument files
- `pattern.pat`
- `effects.fx`

`filesystem_initStagedScene()` initializes Scene defaults, Kit defaults, and a valid empty PatternSet before optional file data overlays it. Pattern initialization uses `pat_initPatternSet()` so the filesystem does not duplicate PatternData default logic.

Retest fix: before copying `op_staged_scene` into resident SceneData, filesystem writes the display name captured from the root Scene directory into `op_staged_scene.display_name`. This is required because `sceneset.scg` no longer contains a name and must never contain one.

Retest fix: while scanning the embedded Kit child, Scene Load captures the bytes after `Kit ` into `op_scene_child_display_name`. After `kitset.kcg` validates, that name is committed to the staged embedded Kit. This fixed the boot-then-immediately-Save-Kit case where the Save Kit name editor had been blank instead of showing `Slak`.

## Scene Save

`Save:[Scene]` is now reachable from the Save type cycle and writes:

- root `Scene/<NNN Name>/`
- `sceneset.scg`
- embedded `Kit <kit name>/`
- six embedded Instrument files
- draft text `pattern.pat`
- placeholder `effects.fx`

Save Scene character editing seeds from `scene_sceneDisplayName(scene_getActiveIndex())`, including when the target slot is empty. It must not seed from `filesystem_sceneSlotName(target_slot)`, because an empty target cache reports `Empty`, and it must not seed from the embedded Kit name.

Scene Save updates the resident Scene display name only after the directory write succeeds.

## Scene Save Deletion Hardening

The first Scene Save implementation exposed the same class of bug previously seen with Kit Save: an overwrite path can delete far too much if scoped to the wrong parent or if it treats aliases as proof of identity.

Final rule:

- Scene Save cleanup is scoped only under root `/Scene/`.
- It deletes only physical child directories whose visible display name parses as the exact requested three-digit Scene slot.
- It must never delete the root `Scene/` folder.
- It must never delete other root Scene folders.
- It must never recursively touch nested Kits/pattern/effect folders except inside the exact same-slot Scene directory being replaced.

Implementation detail: the shared slot-delete worker gained an `allow_short_alias` flag. Kit Save keeps short-alias fallback for old Kit folders. Scene Save calls `filesystem_deleteSceneSlotDirectoriesStart()` with short-alias fallback disabled, so visible numbered Scene names are the authority.

This is the durable lesson: root library overwrite code must first enter the correct root directory, then scan only immediate child objects, then parse visible product names, then delete only same-slot matches. Never run a recursive cleanup from the filesystem root with a broad target string.

## VOICE Mix Scene Settings

The first Scene settings UI stacked all voices' Scene parameters together on every instrument mix page. Retest corrected this.

Current behavior:

- Each selected VOICE mix subpage shows only that voice's Scene mix settings.
- The appended Scene-setting screen has three cells:
  - `audio_out` for the selected voice
  - `fx_send_amount` for the selected voice
  - `fader_setting` for the selected voice
- `MENU_SCENE_SETTING_COUNT` is `3`.
- `menu_resolveSceneSettingCell()` derives the voice slot from `menu_voicePageToSlot(menu_activePage)`.
- The fourth compact column is empty.

Scene settings are not instrument descriptors and must not pass through `instrumentManager_voicePageDescriptorIndex()`.

## BankData And Bank File Shape

`Core/Bank/BankData.c/h` is a small resident owner for current Bank identity:

- eight printable display cells plus NUL
- active Bank-local Scene slot clamped to `00..15`
- `has_resident_bank` bit for future autosave/debug state

`bank_init()` seeds the display name as `none    `.

Root Bank folders are direct `000..999` library entries under `Bank/`. A Bank contains `bankset.bcg` plus up to 16 Bank-local Scene folders:

```text
Bank/
  000 Slak/
    bankset.bcg
    00 Slak/
      sceneset.scg
      Kit Slak/
      pattern.pat
      effects.fx
```

Bank-local Scene folders are exactly two digits, `00..15`, not `000..015`. This is both a UI/product distinction and a parser invariant: root Scene library folders and Scene-in-Bank folders are different objects.

`bankset.bcg` v1 writes:

```text
format=helicase.bankset
version=1
active_scene=<0..15>
```

It never writes `name`.

`storage_parseBankSceneFolder()` accepts exactly two leading digits plus a space or underscore separator, rejects values above 15, and copies the post-separator display bytes into the fixed Scene display field.

## Bank Load

Boot now scans Banks and tries the lowest-number root Bank first. If no Bank is present, or if a loaded Bank has no usable child Scene, boot falls back to:

1. lowest-number root Scene
2. lowest-number root Kit
3. initialized defaults

Bank Load itself:

- opens root `Bank/`
- opens selected `NNN <name>/`
- validates `bankset.bcg`
- scans Bank-local `00..15` child Scene folders
- loads `active_scene` if present, otherwise the lowest present child Scene
- completes successfully with no Scene payload when the Bank is empty

Preset/Menu acknowledge an empty-Bank success before posting the root Scene/root Kit fallback so only one Preset operation is active at a time.

## Bank-Local Scene Re-Entry Fix

Hardware retest found Bank 000 was not loading its initial Kit. The bug was directory position after the embedded Kit load.

Root Scene Load path:

```text
Scene/NNN Name/Kit Name/
```

After reading the embedded Kit, root Scene Load reopens/re-enters the root Scene folder before opening `pattern.pat` and `effects.fx`.

Bank Load path:

```text
Bank/NNN Name/SS Scene/Kit Name/
```

After reading the embedded Kit, Bank Load must take one parent step from `Kit Name/` back to `SS Scene/`, not attempt the root Scene re-entry path. The shared Scene payload reader now distinguishes root Scene load from Bank child load and returns to the correct parent before opening `pattern.pat` and `effects.fx`.

This keeps the root `Scene/NNN` namespace and Bank-local `Bank/NNN/SS` namespace separate during both boot and manual Bank Load.

## `ERR BnkL06` Fix

After the first Bank implementation, manual Load Bank failed with `ERR BnkL06`.

Meaning:

- `BnkL06` is Bank Load phase 6.
- The root Bank scan cache said slot 000 existed.
- Opening the selected root Bank folder returned no handle.

Cause:

- The Bank scanner cached `op_object.shortName` as the later open key.
- Host-created LFN directories such as `000 Slak` list correctly, but `afatfs_opendir_lfn()` read-only display matching expects the public display component, not the generated SFN alias.
- Attempting the LFN open by SFN alias can fail even though the slot was discovered.

Fix:

- Root Bank scan stores `op_object.displayName` as both browser text and future open key.
- Bank-local child Scene discovery applies the same rule, so `00 Slak` opens by display component after the selected Bank opens.
- Saved Bank cache updates retain the display directory name as the future open key instead of the returned SFN alias.

Durable rule: for LFN-aware read-only opens that match display components, cache the display component. Short aliases remain useful for short-name APIs and some returned-create paths, but they are not interchangeable with visible names in LFN display matching.

## Bank Save

`Save:[Bank]` is promoted. Bank Save currently writes the one-resident-Scene bridge:

- root `Bank/<NNN Name>/`
- `bankset.bcg`
- Bank-local child Scene `00 <scene name>/` when mask bit 0 is set

A zero scene mask is valid and creates/saves an empty Bank. Future 16-Scene workspace work will expose toggles; this phase keeps the mask-driven writer shape without implementing the full toggle UI.

Retest fix: Bank Save name editing seeds from `bank_displayName()`, so empty target slots use the retained resident Bank name or `none`, not `Empty`.

Important limitation retained from the audit: safe root Bank folder rename while preserving untoggled child Scenes is not solved. If changing `NNN Old` to `NNN New` would require replacing the whole folder, that can violate the future "do not overwrite untoggled Scenes" rule. The correct future primitive is async rename or safe promotion; until then, Bank Save must be conservative about folder identity.

## Universal `none` Defaults

Hardware retest showed uninitialized names were inconsistent (`Untitled`, blank, or accidental one-character names). Current rule:

- `bank_init()` seeds Bank identity as `none    `.
- `scene_initAll()` seeds Scene and embedded Kit identity as `none    `.
- Default Instrument save stems are `none`.
- Saving default instruments produces filenames such as `none   1.drm` through `none   6.hat`.
- An all-blank embedded Kit display field formats as `Kit none`, not `Kit Untitled`.
- `storage_makeSavedInstrumentDisplayFilename()` falls back to `none` for null, empty, or all-space stems.

This preserves the product rule that files do not store their own object names while still giving every newly saved directory/file a visible safe identity.

## Pattern `pattern.pat` Draft v2

Initial Scene and Bank saves wrote empty pattern placeholders. Session 039 replaced that with a draft text payload so save/load can round-trip basic step activity.

Accepted formats:

- v1 placeholder, still accepted for old fixtures:

```text
format=helicase.pattern
version=1
placeholder=1
```

- v2 draft payload, emitted by new Scene/Bank saves:

```text
format=helicase.pattern
version=2
track1=<length>,<scale>,<128 active bits>
...
track7=<length>,<scale>,<128 active bits>
```

Storage rules:

- Only `STEP_ACTIVE_MASK` is serialized for each of 128 steps on each of 7 tracks.
- Velocity, note, probability, automation, rotation, shuffle, next-pattern, and change-bar remain PatternData defaults on load.
- Length validates as `1..NUM_STEPS`.
- Scale validates as `0..TRACK_SCALE_COUNT-1`.
- The v2 parser overlays active bits and length/scale into the already initialized `PatternSet`.
- The loader rebuilds the legacy 16-bit `pat_mainSteps[]` shadow with `step % NUM_STEPS_PER_BAR`, matching `pat_recordNote()` and the current 16-button LED projection.
- `FS_TEXT_LINE_MAX` was raised to 160 because one row needs key text, two decimal fields, 128 bit characters, newline, and NUL.

Parser/writer pitfalls fixed:

- `storage_parseU8()` must return `STORAGE_STATUS_OK`, not boolean true, because callers compare against the storage status enum.
- `storage_patternDraftParseTrack()` must also return `STORAGE_STATUS_OK` for a valid `trackN=` row.
- `storage_appendDecimalU16()` is a boolean append helper and must return `1u` on success because the writer treats zero as line overflow/end.

This draft is explicitly not the final dynamic Pattern schema.

## Load/Save Menu Changes

Scene and Bank use explicit OK/OW behavior. They do not load while scrolling. Kit and KitMrp retain live-on-scroll behavior.

Retest fix: after every OK/OW operation finishes or fails, the selection returns to the top-row type field. This avoids leaving the pointer on `ok` with no completion indication.

Load Bank now shows an `ok` affordance like Load Scene.

Diagnostic entries:

- `File`
- `Dir`
- Save-only `sDir`

remain compiled and dispatched, but they are hidden from the Load/Save type arrays unless `CONFIG_DEV_MODE != 0`.

`CONFIG_DEV_MODE` lives in `config.h` and defaults to `0`.

Retest fix: when an old/current type is no longer visible, Load/Save reset falls back to `Kit`, not hidden `File`.

## Generated Fixture And Tools

Session 039 added/updated:

- `SD_CARD/Scene/000 Slak/` in the new Scene data shape.
- `SD_CARD/Bank/000 Slak/bankset.bcg`.
- `SD_CARD/Bank/000 Slak/00 Slak/`.
- `tools/populate_scene_directory.py` to emit Scene-owned mix settings, strip Kit `audio_out`, write pattern placeholders/drafts as appropriate for the script version, and ignore `.DS_Store`.
- `tools/populate_bank_directory.py` to regenerate `Bank/000 Slak/00 Slak/` from a root Scene.

The generated files must not contain object self-name fields. `rg -n "^name=" SD_CARD/Bank` was clean during retest.

## Filesystem Lessons To Preserve

1. **Names live in paths.** Scene, Bank, Kit, and Instrument identity comes from directory/file names. Do not add `name=` fields to `sceneset.scg`, `bankset.bcg`, or instrument files.

2. **Deletion must be scoped by parent and product parser.** To replace a numbered library slot, enter the correct root folder first, scan immediate children, parse visible names using the correct root or Bank-local parser, and delete only same-slot matches.

3. **Do not reuse root parsers for Bank-local children.** Root `Scene/NNN` and Bank-local `SS` Scene folders are different namespaces. Bank-local Scene folders are two digits.

4. **LFN display names and SFN aliases are not interchangeable.** Use the identity expected by the API. `afatfs_opendir_lfn()` display matching needs display components; short aliases are not safe open keys for host-created LFN display names.

5. **Empty Bank is valid.** Bank selection can succeed without loading a child Scene. Preset/Menu then run the Scene/Kit/default fallback path.

6. **Do not publish resident names before a successful write/load commit.** Save operations update resident Scene/Bank/Kit identity only after the filesystem operation succeeds.

7. **Text parser return domains matter.** `storage_status_t` functions return enum values; byte-count/append helpers return boolean or length. Mixing these can compile and still make valid files fail.

## Verification

Commands run successfully during the final closeout:

- `make`
- `git diff --check`
- `make img`

Earlier in-session verification also included:

- `python3 -m py_compile tools/populate_bank_directory.py`
- `python3 -m py_compile tools/convert_legacy_kits.py tools/populate_bank_directory.py`
- `rg -n "^name=" SD_CARD/Bank`

`make img` rewrote:

- `build/LXRV2_lxr02.img`

Known warnings remain the existing nano-libc syscall stubs (`_close`, `_lseek`, `_read`, `_write`) and LTO serial-compilation note.

## Important Follow-Up Tests

- Hardware-test the final v2 `pattern.pat` round-trip: create active steps, Save Scene, Load Scene, confirm step LEDs/playback retain active bits and track length/scale.
- Hardware-test Save Bank / Load Bank after the v2 pattern change.
- Test loading a v1 placeholder Scene/Bank fixture after the v2 parser addition.
- Test saving/loading an empty Bank folder containing only valid `bankset.bcg`; it should retain Bank identity and fall back to root Scene/Kit/defaults.
- Test same-slot Bank Save once the future 16-Scene toggle UI exists; it must not delete untoggled child Scene folders.
- Keep Scene/Bank autosave and dot-file promotion gated until async rename/replace or equivalent safe promotion is proven.

## Files Touched Heavily

Primary code:

- `config.h`
- `main.c`
- `Makefile`
- `Core/Bank/BankData.c`
- `Core/Bank/BankData.h`
- `Core/Bank/Scene/SceneData.c`
- `Core/Bank/Scene/SceneData.h`
- `Core/Bank/Scene/Pattern/PatternData.c`
- `Core/Bank/Scene/Pattern/PatternData.h`
- `Core/Bank/Scene/Preset/presetManager.c`
- `Core/Bank/Scene/Preset/presetManager.h`
- `Core/Hardware/SD/filesystem.c`
- `Core/Hardware/SD/filesystem.h`
- `Core/Hardware/SD/storageTypes.c`
- `Core/Hardware/SD/storageTypes.h`
- `Core/Menu/menu.c`
- `Core/Menu/menu.h`
- `Core/Hardware/frontPanel/buttonHandler.c`
- `tools/convert_legacy_kits.py`
- `tools/populate_scene_directory.py`
- `tools/populate_bank_directory.py`

Generated/fixture:

- `SD_CARD/Scene/000 Slak/`
- `SD_CARD/Bank/000 Slak/`
- `build/LXRV2_lxr02.img`

Docs/audits consumed:

- `SCENE_DATA_AUDIT.md`
- `BANK_DATA_AUDIT.md`

## Summary

Session 039 is the Scene/Bank filesystem bridge session. It made root Scene and Bank load/save real enough to boot through `Bank/000 Slak/00 Slak`, fixed the retest failures that would have made future autosave dangerous, and converted the audit learnings into durable rules: object names live in paths, deletion must be parent-scoped and parser-scoped, Bank-local Scene folders are two-digit, LFN display matching needs display names, empty Banks are valid, and draft pattern storage now preserves the basic 128x7 step-active grid plus track length/scale.
