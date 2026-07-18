# Journaling / `.names` implementation log

This is a forensic record of the asyncfatfs journaling and `.names` work attempted in the implementation pass on 2026-07-18. It is intentionally separate from the feature plan: it records what was changed, what went wrong, and what remains uncertain so a subsequent implementer can start from evidence rather than from assumptions.

## Scope and starting point

Before the implementation pass, the repository already contained the earlier asyncfatfs Phase 5/6 work. That work included parent-relative filesystem APIs, tree-copy/replacement primitives, and recoverable journal/temporary-tree concepts. Those pre-existing changes were not the subject of the later rollback request and should be preserved.

The implementation pass began after the `.names` contract was clarified:

- `.names` is a names-only register. It must never encode a path, slot number, directory, or other location information.
- An instrument name is its filename without the extension.
- A kit name is its folder name with a leading `NNN ` or `Kit ` removed.
- A scene name is its folder name with a leading `NNN ` or `NN ` removed.
- A bank name is its folder name with a leading `NNN ` removed.
- The register has capacity for 96 instrument names, 16 kit names, 16 scene names, and one bank name.
- A name changes when its associated storage is successfully loaded or saved: up to one instrument name, up to six kit/scene names, or all names for a bank operation.
- Menu code remains responsible for choosing locations and ordinals; the register only supplies or updates display/source names.

## What the implementation pass attempted

### `.names` register

The pass added a `namesRegister.c`/`namesRegister.h` module and wired it into the build and filesystem-facing code. The attempted design used two register copies with metadata such as generation, record count, and CRC, allowing a newer valid copy to be selected after an interrupted write. It also added mount/create and record read/write/provider APIs.

The intended benefits were bounded storage, corruption detection, and recovery without keeping all names resident in SRAM. The implementation became coupled to the broader filesystem rewrite, however, instead of remaining a small names-only service.

### asyncfatfs and tree operations

The pass modified asyncfatfs parent/root handling and several parent-relative operations. It also attempted to connect save transactions to temporary trees, replacement/recovery behavior, numbered directory resolution, and bank-child resolution.

These changes were meant to support real-tree lookup and safe overwrite while avoiding cached location data. They were spread across the existing filesystem facade and did not receive a clean, isolated transaction boundary.

### Cache removal and name loading

Resident scene/kit/bank/instrument name fields and larger cache arrays were removed or bypassed in several paths. Replacement code attempted to resolve names from the real tree and read/write one `.names` record at a time. Development-only caches were also gated in some paths so they would not be used when development mode is disabled.

Instrument load/save wiring was added, including name-register updates and an instrument tree replacement/probe path. Container save/load paths attempted grouped `.names` updates for kit, scene, and bank operations, and menu seed flows were adjusted accordingly.

Sample-name and installer-related paths were also touched while trying to eliminate the old resident-name assumptions.

### Documentation and comments

The pass added many change-adjacent comments in C and H files, including rationale, inputs/outputs, related code, and comments around loops, variables, and calculations. Because the implementation was repeatedly patched, some comments now describe paths that are only partially present or are legacy compatibility paths; they must be re-audited with the code before reuse.

## Why the result was rejected

The implementation became concentrated in `Core/Hardware/SD/filesystem.c`. The file grew through a large sequence of patches and accumulated duplicated paths, legacy compatibility code, and `#if 0` sections. The user observed that approximately 14,000 lines had been written to this file and correctly rejected that shape as unacceptable.

The main technical problems were:

1. The names-only register boundary was not kept narrow. Name storage, location resolution, cache removal, and transaction behavior became interdependent.
2. Multiple old and new paths coexisted, making it unclear which resolver or cache was authoritative.
3. The filesystem facade became the integration point for too many responsibilities instead of delegating to small modules.
4. The implementation did not complete a hardware-level validation pass. Builds were run, but no final SD/power-loss validation established that overwrite and recovery behavior were correct.
5. The broad rewrite made review and rollback difficult and increased the chance of leaving stale references behind.

Observed build-size snapshots during the pass were approximately:

- At the beginning of the pass: `text=367752 data=408 bss=364908`.
- During the broad implementation: `text=370664 data=436 bss=262872`.

These numbers are historical snapshots only. They do not describe the final worktree after rollback and must not be used as acceptance measurements.

## Rollback requested by the user

The user then requested: “undo your code changes from last turn. I will ask someone else to implement”. The rollback was deliberately limited to the implementation window, rather than resetting the repository, because the tree already contained earlier Phase 5/6 work and other user changes.

The rollback procedure used the Codex session log to identify successful `apply_patch` events between the implementation start and the stop request, then replayed inverse patches in reverse order. This was chosen to preserve unrelated dirty files and prior work.

The procedure was not cleanly completed:

- A first composite inverse patch partially applied before reporting a hunk failure.
- One ambiguous one-line hunk matched the wrong occurrence in `filesystem.c` and had to be corrected manually.
- The long reverse-replay process was interrupted before all 134 successful implementation-window patches had been inverted.
- Consequently, the current state must be treated as a partial, unverified rollback—not as a known-good restoration.

The deleted `knowledge_files/specification_reference/MEMORY_AUDIT.md` had not yet been restored at the time this log was written. An untracked `build.log` was present and was intentionally left untouched. The generated build image and other pre-existing modifications were also left untouched.

## Worktree evidence at log time

The last inspection reported these tracked/untracked changes:

```
 M Core/Hardware/SD/filesystem.c
 M Core/SampleRom/SampleMemory.c
 M Makefile
 M build/LXRV2_lxr02.img
?? build.log
```

`Core/Hardware/SD/filesystem.c` was reported at 13,554 lines. References to `namesRegister` remained in the Makefile and filesystem code even though the register source files were no longer shown as separate untracked files, another indication that the inverse replay was incomplete. The exact set of stale symbols and comments still needs a deliberate diff against the pre-implementation checkpoint.

## Recommended handoff for the next implementer

The next implementation should begin by auditing and, if necessary, completing the rollback before adding features. Do not use a destructive whole-tree reset because it would erase the earlier Phase 5/6 work and unrelated user changes.

The preferred architecture is:

- Keep `.names` in a small, independently testable register module.
- Keep `.names` strictly names-only; all paths, ordinals, and directory selection stay in the load/save menu and real-tree resolver.
- Put instrument resolution, library resolution, and save transaction/recovery logic in separate modules or narrowly scoped APIs.
- Make filesystem.c a thin integration facade rather than the owner of every operation.
- Add tests for record offsets, CRC/generation selection, one-name and grouped-name updates, failed writes, interrupted replacement, and real-tree overwrite before removing more caches.
- Restore and update `MEMORY_AUDIT.md` before using its memory estimates as a design baseline.
- Set a line-count/module-size review gate so a feature cannot silently become another monolithic filesystem rewrite.

## Status

This log documents the attempted work and its rollback risk. It does not claim that journaling or `.names` is currently implemented correctly, and it does not claim that the worktree is fully restored. A clean diff audit is required before another implementation begins.
