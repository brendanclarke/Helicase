# Session 037 Handoff Log

DATE: 2026-07-15

SESSION GOAL: Continue Phase 3.6 load/save expansion after Session 036 by adding Morph Kit and Morph Instrument load/save operations, implementing mandatory asyncfatfs LFN-aware rename/replace semantics, applying the case-insensitive/case-preserving overwrite policy, and keeping Kit/Instrument save/load behavior aligned with the new directory Kit format.

COMPLETED: Several planning updates and code passes were attempted, including asyncfatfs rename/replace support, menu exposure for KitMrp/InstrumentMrp, Kit member filename storage changes, InstrumentMrp projection fixes, and multiple attempted Kit Save repairs. These changes did not produce a working result.

VERIFIED ON HARDWARE: Yes, and testing failed. User hardware testing repeatedly showed that Kit Save still does not create/load a usable Kit directory. At one point a Kit appeared in the Kit list even though it was not loadable, proving an optimistic/fake cache publication bug. That cache bug was identified and patched, but the underlying Kit directory creation/load failure remained unresolved. Treat Session 037 as failed.

## Working Repository Snapshot

- Working directory: `/mnt/c/Users/brendan.clarke/proj/Helicase`
- Branch observed at closeout: `dev-phase2-filesys`
- Recent visible commits at closeout:
  - `a6cdfe7 (HEAD -> dev-phase2-filesys, origin/dev-phase2-filesys) fix?`
  - `69183cc fix?`
  - `aa53854 kit save dir create`
  - `b3e29ff kit dir make patch`
  - `f98d2a0 fix build error`
  - `8f19f2f kit member fix, morph parameter save fix`
  - `f7ae03e no new directory bug maybe fix?`
  - `876029f test files`
  - `e9c2483 test files`
  - `3bbdcae menu-morph post fix`
  - `ff1087f load-save expansion - character case handling updated for existing Kit and Instrument. -- UNTESTED`
  - `0c5788d update rename-replace and case-parsing contract: case-insensitive, case-preserving in all instances, mandatory -- UNTESTED`
  - Session 036 closeout commit before these visible attempts: `8439083 asyncfatfs updated successfully with long filename and case sensitive parsing. Kit and instrument load and save operations added back and tested. close of session 036`
- Focused status before writing this log showed no uncommitted code changes in the main touched files; the tested-failed work appears to have been committed during the session.
- Full `git status --short` is extremely noisy in this checkout due to pre-existing broad working-tree modifications/line-ending state. Do not infer Session 037 scope from whole-tree status.

## Critical Result

Session 037 failed.

The main unresolved failure is:

- Save:[Kit] and Save:[KitMrp] still do not create a usable Kit directory in `/Kit/`.
- User reported "still no" after repeated fixes.
- User reported the created/fake Kit entry could appear in the Kit list but did not load.
- The system must not publish cache entries for objects that do not exist or cannot be scanned/opened from card.

Recommended next action is not to continue patch-stacking. Start the next session by choosing a rollback/isolation strategy.

## User Policy Decisions Captured

- Rename directory support is mandatory before Morph work.
- Rename behavior must be implemented the same way for existing normal Kit Save and new Morph Kit Save.
- Universal filename policy:
  - case-insensitive matching;
  - case-preserving storage/display;
  - a user-entered filename overwrites and shows `OW` if it matches an existing file regardless of case;
  - the newly written file retains the user-entered case.
- Same-casefold duplicates created externally should be hidden deterministically:
  - capital letters sort before lowercase;
  - `fiRstfile.snr` precedes `firStfile.snr`;
  - the first sorted physical object is displayed/read/handled;
  - overwrite removes all matching casefold variants and leaves only the user-entered spelling.
- Blank Kit name is valid:
  - internal retained Kit name can be all blank;
  - root Kit folder component can be `NNN `, meaning three digits plus a space separator and no name text.
- `"Kit <name>"` always means the embedded Kit directory inside a Scene. This must not be confused with the blank root Kit name policy.
- Scene name storage "now is fine" and was not a blocker.
- Sequencer automation planning correction:
  - parameter automation persists until an active step, meaning first bit high;
  - automation on inactive steps does not reset other automated parameters unless it automates the same parameter with a new value.

## Changes Attempted This Session

### Planning Documents

- `LOAD_SAVE_EXPANSION_ADD_MORPH.md`
  - Updated for mandatory LFN-aware rename/replace before Morph.
  - Updated case-insensitive/case-preserving policy.
  - Updated duplicate-casefold ordering and overwrite behavior.
  - Expanded Phase 2 through Phase 5 into detailed implementation plans.
  - Later expanded Phase 3 Morph implementation plan based on code dive.
  - Notes were kept during implementation passes.
- `SCOPING_TARGETS.md`
  - Swept and corrected automation persistence wording from "resets on any next active or automated step" to the user-corrected model: reset boundary is the next active step, not any inactive automation step.
- `knowledge_files/specification_reference/MEMORY_AUDIT.md`
  - Received multiple Session 037 notes during attempts:
    - Kit member filenames now store visible LFN components.
    - Occupied Kit saves remove stale member files.
    - InstrumentMrp mode capture.
    - Empty Kit save cleanup bypass attempts.
    - opened subdirectory physicalSize attempt.
    - removal of optimistic Kit cache insertion.
    - fresh-directory per-member remove skip.
  - These notes document attempted changes, not success. Hardware testing failed.

### asyncfatfs Rename/Replace Attempt

Attempted to add LFN-aware rename/replace behavior in asyncfatfs:

- Public API additions/changes included LFN rename and object removal helpers.
- New remove/rename state was added internally.
- Case-insensitive overwrite collapse was intended to remove all same-casefold file variants.
- Directory rename was needed for normal Kit Save and KitMrp Save.
- Comments were added adjacent to code per session convention.

Important warning:

- Even if this code compiles, it is not proven safe. The final Kit Save behavior failed hardware testing.
- Do not assume asyncfatfs rename/remove is correct just because File/Dir diagnostics had worked in Session 036.

### Phase 2 Implementation Attempt

Phase 2 focused on normal Kit/Instrument save behavior and cache/file naming:

- Kit member filenames were changed to retain visible LFN display names rather than only asyncfatfs short aliases.
- `storageTypes.h` received a larger Kit member filename field.
- `storageTypes.c` gained helpers for copying Kit member filenames.
- Kit load was changed to open `file=` entries through LFN-aware open.
- Kit save wrote visible member filenames into `kitset.kcg`.
- The eighth-character-is-voice-number convention was attempted for saved Kit member files.

User testing found:

- New `kitset.kcg` contents and/or generated files did not follow the required convention.
- User specifically called out `SD_CARD/Kit/037 Slak2/` as bad.
- The generated files did not enforce "the eighth character is the voice number."

Later attempts adjusted:

- `storage_makeSavedInstrumentDisplayFilename()`
- `op_save_instrument_display_file`
- `kitset.kcg` writer inputs
- LFN-aware member opens

The final user result for Kit Save still failed.

### Phase 3 Morph Implementation Attempt

Phase 3 attempted Morph Kit and Morph Instrument behavior:

- Load:[KitMrp] and Save:[KitMrp] menu entries were intended to appear directly after normal Kit load/save entries.
- Load:[InstrumentMrp] existed and reportedly worked initially.
- Save:[InstrumentMrp] needed UI changes so the top-row instrument type label could be selected and scrolled from `Drum` to `DrumMrp` or equivalent.
- InstrumentMrp save behavior was intended to:
  - save active interpolated parameters at the current instrument's per-voice morph value into the normal endpoint fields in the file;
  - save the normal resident instrument parameters into the file's morph endpoint fields.

User testing found:

- There were no menu items for Load/Save Morph Kit or Save Morph Instrument at first.
- Load Morph Instrument existed and seemed to work.
- InstrumentMrp Save behaved exactly like normal Instrument Save.
- It saved normal-to-normal and morph-to-morph, not the requested projected/interchanged endpoint behavior.

Later attempted fixes included:

- Capturing instrument save mode as request state.
- Adding/using `STORAGE_INSTRUMENT_SAVE_MORPH` in storage write views.
- Updating menu Save surface behavior for InstrumentMrp.

The session still failed because Kit Save never became reliable, blocking meaningful Morph Kit validation.

### Menu Changes Attempted

Attempted menu changes included:

- Add Load:[KitMrp] directly after Load:[Kit].
- Add Save:[KitMrp] directly after Save:[Kit].
- Make root Instrument Save top-row type selectable so `Drum` could scroll to `DrumMrp`.
- Add or repair OK/OW behavior for case-insensitive overwrite.
- Ensure normal Kit and KitMrp use independent browser cursors but same root Kit namespace.

Testing did not reach a stable successful end state.

### Build Error Fixed

User reported:

- build error: `afatfs` undeclared in `filesystem.c`.

Cause:

- A cleanup attempt had directly referenced the private asyncfatfs singleton from `filesystem.c`.

Fix attempted:

- Reworked cleanup to use public handles such as `op_kit_slot_dir` instead of `afatfs.currentDirectory`.

This build error was addressed, but final Kit Save testing still failed.

### Fake Kit Cache Bug Found

User reported:

- created directory shows up in Kit list;
- nothing loads;
- user explicitly objected to fake cached lists.

Root cause found in `filesystem_saveKitDirectory_tick()` phase 24:

- save code inserted:
  - `kit_slot_present[op_slot] = 1`
  - `kit_slot_name[op_slot] = parsed_display`
  - `kit_slot_open_name[op_slot] = op_save_kit_dir_name`
  - `filesystem_noteKitBrowserSlot(op_slot)`
- This was based on save intent and returned alias, not a real `/Kit` scan.
- It could publish a Kit list entry that the card scanner could not enumerate/load.

Patch attempted:

- Removed save-phase cache insertion.
- Kit Save and KitMrp Save completion were changed to request a real `filesystem_requestScanKits()` before reporting save completion.
- Intended rule: the Kit list should be scan-derived only.

Result:

- This fixed one real design bug, but user testing still reported no usable Kit directory creation.

## Failed Fix Attempts For Kit Save

Several hypotheses were tried. All should be treated as unproven or insufficient.

### Attempt: Skip Occupied-Slot Cleanup For Empty Slots

Hypothesis:

- newly-created empty directories were entering stale member cleanup before final sync;
- cleanup could fail/stall before save completion, making the directory disappear.

Patch:

- If `op_save_found_existing_dir == false`, phase 10 skipped broad stale-member cleanup and closed the new directory handle directly.

Result:

- User still reported Kit Save did not create a directory.

### Attempt: Opened Directory physicalSize Repair

Hypothesis:

- FAT directory entries store `fileSize == 0`;
- `afatfs_fileLoadDirectoryEntry()` was deriving `physicalSize` from fileSize;
- opening `/Kit` could produce a directory handle that looked like it had no allocated cluster.

Patch:

- In `afatfs_fileLoadDirectoryEntry()`, directories with nonzero firstCluster were given `physicalSize = afatfs_clusterSize()`.

Result:

- User still reported Kit Save did not create/load usable directories.

Warning:

- This may only handle one-cluster directories and may not correctly represent multi-cluster directories.
- It should be audited rather than trusted.

### Attempt: Remove Optimistic Kit Cache Publication

Hypothesis:

- The visible Kit list was fake because save code directly wrote the browser cache.

Patch:

- Removed direct cache insert from save phase.
- Added post-save real scan before reporting completion.

Result:

- Correctly identified a real fake-list bug.
- Did not fix directory creation.

### Attempt: Skip Per-Member Duplicate Remove For Fresh Directories

Hypothesis:

- Fresh Kit directories do not need per-member `afatfs_removeObjects_lfn()` before writing member files.
- A delete scan before first member write could stall/fail before final sync.

Patch:

- Fresh directories now skip per-member remove and proceed directly to member `fopen_lfn`.

Result:

- User still reported failure before this log closeout, or at minimum no success was observed. Treat as unverified and insufficient.

## Known Issues Introduced

- Session 037 likely left the branch in a high-risk state.
- Kit Save still fails user testing.
- KitMrp Save cannot be trusted because it uses the same broken Kit directory writer.
- InstrumentMrp Save behavior was reported wrong during testing.
- asyncfatfs rename/remove changes are not proven.
- The working tree/branch may contain committed failed attempts with vague commit messages (`fix?`, `kit save dir create`, etc.).
- The cache publication bug was found and patched, but the underlying save failure is unresolved.

## Known Issues Resolved

Resolved only in the narrow sense:

- The direct `afatfs` singleton reference build error in `filesystem.c` was addressed.
- The fake Kit cache publication path was identified and removed.
- The planning docs captured the user policy decisions for casefold overwrite and blank root Kit names.

These do not mean the feature works.

## Verification

Local agent verification:

- Repeated focused `git diff --check` on touched files passed.
- `rg` confirmed some private `afatfs.` references were removed from `filesystem.c`.
- No local compile was possible in the Codex shell because `make` was unavailable.

User/hardware verification:

- FAILED.
- User tested after multiple fixes and continued to report:
  - Kit Save still not creating the directory.
  - Kit Save still not loading from Kit.
  - A Kit could appear in the Kit list despite not being loadable.
  - Final user conclusion: work appears built on a corrupted foundation and rollback may be needed.

## Rollback Guidance

No rollback was performed in this closeout.

Candidate rollback boundaries:

- To abandon only Session 037 post-Session-036 work:
  - inspect/revert/reset commits after `8439083`.
  - Visible commits after `8439083`: `0c5788d`, `ff1087f`, `3bbdcae`, `e9c2483`, `876029f`, `f7ae03e`, `8f19f2f`, `f98d2a0`, `b3e29ff`, `aa53854`, `69183cc`, `a6cdfe7`.
- To abandon the whole LFN expansion foundation:
  - consider rollback to before `8439083`.
  - Likely nearby pre-LFN-expansion candidate: `530b57f save extension - failed` or an earlier known-good point, but this must be chosen deliberately after checking Session 036 scope.

Recommended next session:

1. Do not implement new Morph work.
2. Decide rollback target.
3. If not rolling back, isolate Kit Save with a minimal reproducible state machine:
   - create/open `/Kit`;
   - create one `NNN Name` child;
   - close and sync;
   - rescan `/Kit`;
   - only then add one member file;
   - only then add all six member files;
   - only then add `kitset.kcg`.
4. Use File/Dir diagnostics as the known working control.
5. Do not update product caches except by real scan.

## Files/Areas To Inspect Next

- `Core/Hardware/SD/asyncfatfs/asyncfatfs.c`
  - LFN create/open state machine.
  - Directory initialization handoff.
  - Directory open metadata for existing subdirectories.
  - Rename/remove object state machines added in this session.
  - Final sync behavior after nested directory/file writes.
- `Core/Hardware/SD/filesystem.c`
  - `filesystem_saveKitDirectory_tick()`.
  - `filesystem_loadKitDirectory_tick()`.
  - `filesystem_scanKits_tick()`.
  - all writes to `kit_slot_present`, `kit_slot_name`, `kit_slot_open_name`, and `kb_map`.
- `Core/Hardware/SD/storageTypes.c/.h`
  - Kit member filename generation.
  - `kitset.kcg` file entry parsing/writing.
  - Morph save projection rules.
- `Core/Scene/Preset/presetManager.c`
  - Kit save completion and post-save scan.
  - InstrumentMrp/KitMrp completion paths.
- `Core/Menu/menu.c`
  - Save type cycling for Kit/KitMrp.
  - nested Instrument Save top-row type selection.
  - OK/OW identity logic.

## End of session block

```
DATE: 2026-07-15
SESSION GOAL: Add Morph Kit/Instrument save/load on top of asyncfatfs LFN rename/replace and restored Kit/Instrument saves.
COMPLETED: Planning updates, asyncfatfs rename/remove attempts, Kit/Instrument Morph menu/save attempts, Kit member filename changes, fake Kit cache bug identification, and multiple Kit Save repair attempts.
VERIFIED ON HARDWARE: yes, failed. User repeatedly tested and Kit Save still does not create/load a usable Kit directory. InstrumentMrp behavior was also reported wrong earlier in the session.

CHANGES THIS SESSION:
- LOAD_SAVE_EXPANSION_ADD_MORPH.md: expanded/updated plan for rename/replace and Morph implementation.
- SCOPING_TARGETS.md: corrected automation persistence wording to active-step reset boundary.
- Core/Hardware/SD/asyncfatfs/asyncfatfs.c/.h: attempted LFN-aware rename/remove/replace and directory-open repair work.
- Core/Hardware/SD/filesystem.c/.h: attempted Kit Save/KitMrp Save integration, cache publication fix, stale cleanup changes, member save sequencing changes.
- Core/Hardware/SD/storageTypes.c/.h: attempted larger visible Kit member filenames and Morph save projection support.
- Core/Menu/menu.c: attempted KitMrp/InstrumentMrp menu exposure and Save UI top-row selection repair.
- Core/Scene/Preset/presetManager.c/.h: attempted KitMrp/InstrumentMrp request/completion paths and post-save Kit rescan.
- knowledge_files/specification_reference/MEMORY_AUDIT.md: added notes for attempted fixes. These notes do not imply success.
- knowledge_files/log_archive/000_SESSION_INDEX.md and 037_SESSION_HANDOFF_LOG.md: added failed-session closeout.

KNOWN ISSUES INTRODUCED: Branch likely contains committed failed attempts; Kit Save remains broken; KitMrp Save cannot be trusted; InstrumentMrp Save was reported to behave like normal save; asyncfatfs rename/remove work is unproven.
KNOWN ISSUES RESOLVED: Fake Kit cache publication after save was identified and removed; direct private `afatfs` singleton build error was fixed.

NEXT SESSION RECOMMENDED GOAL: Decide rollback boundary. Either revert Session 037 commits after 8439083, or roll back the full LFN expansion before 8439083 and rebuild from a smaller tested filesystem primitive.
BLOCKERS: Hardware/user testing says current Kit Save fails. No local `make` is available in Codex shell. Need user decision on rollback vs isolation.

CRITICAL REMINDERS FOR NEXT SESSION:
- Mark Session 037 as failed. Do not treat its feature work as a working foundation.
- Do not publish Kit cache entries from save intent. Kit list must be rebuilt by real `/Kit` scan only.
- Do not continue Morph implementation until normal Kit Save creates, scans, and loads a usable directory.
- If keeping LFN work, build a minimal Kit directory creation diagnostic inside `/Kit` before restoring full Kit Save.
- User may prefer full rollback to before LFN expansion; respect that if requested.
```
