# SESSION_050_HANDOFF_PROGRESS.md - Session 050 Progress Handoff

Date: 2026-08-14
Branch: dev-ph3-autosave-ph2
HEAD at start of review: 5c26155 pre-refactor planning stage

## 1. Summary

Session 050 progressed the autosave trace observability work in two parts:

1. Applied the refactor staged in `autosave-trace_refactor/`: a durable
   whole-object `L` (LOAD_MARK) trace record and a narrower trace-flush guard.
2. Planned and executed a temporary expansion of the retained trace ring from
   64 records to 2048 records, with the flush/scheduler pipeline changed to
   drain the larger ring in bounded 512-byte batches.

All source changes are present in the working tree but are not yet committed
and have not been compile-verified: the embedded toolchain is not installed in
this environment.

## 2. Repository state

- Branch: dev-ph3-autosave-ph2 (up to date with origin).
- The twelve `.failed` / `_failed.*` shadow files are already removed and
  committed; no shadow source remains in the tree.
- Modified, unstaged files:
  `AUTOSAVE_REFACTOR.md`,
  `Core/Bank/Scene/Autosave.c`,
  `Core/Bank/Scene/AutosaveTrace.c`,
  `Core/Bank/Scene/AutosaveTrace.h`,
  `Core/Hardware/SD/filesystem.c`,
  `Core/Hardware/SD/filesystem.h`,
  `Core/Menu/menu.c`,
  `Core/Menu/menu.h`,
  `config.h`.
- New untracked planning/review documents:
  `AUTOSAVE_REFACTOR.md` (tracked but modified), `TRACE_EXTENSION.md`.

## 3. Work completed

### 3.1 AUTOSAVE_REFACTOR.md

Created a phased implementation plan for the `autosave-trace_refactor/`
changes, then expanded it into a complete change inventory with what/why/
inputs/outputs for every edit site (C1-C7) and a risk review (section 10).

### 3.2 Refactor changes C1-C7 (applied)

These implement refactor items 5.2, 5.3, and 5.5. Item 5.1 (shadow-file
removal) was already complete.

- `Core/Bank/Scene/AutosaveTrace.h`
  - C1: `AUTOSAVE_TRACE_STAGE_LOAD_MARK = 'L'`.
  - C2: `AUTOSAVE_TRACE_LOAD_MARK_*` kind/flag/shift macros.
- `Core/Bank/Scene/Autosave.c`
  - C3: emit one `L` KIND_KIT record at the end of `autosave_markKitDirty()`.
  - C4: emit one `L` KIND_SCENE record at the end of
    `autosave_markSceneWithoutPatternDirty()`.
- `Core/Menu/menu.c` / `Core/Menu/menu.h`
  - C5/C6: read-only `menu_isLoadSaveCommandActive()` accessor and declaration.
- `Core/Hardware/SD/filesystem.c`
  - C7: trace-flush guard changed from `menu_activePage == LOAD_PAGE/SAVE_PAGE`
    to `menu_isLoadSaveCommandActive()`.

### 3.3 TRACE_EXTENSION.md

Created an exact plan to make the retained trace count configurable and to
temporarily raise it from 64 to 2048 records. The plan covers every edit site
(E1-E8), RAM impact, phases, hardware verification, and a risk register.

### 3.4 Trace extension E1-E8 (applied)

- `config.h`
  - E1/E8: `AUTOSAVE_TRACE_RECORD_COUNT_DEFAULT 64u` and effective
    `AUTOSAVE_TRACE_RECORD_COUNT 2048u` (temporary approved expansion).
- `Core/Bank/Scene/AutosaveTrace.h`
  - E2: `AUTOSAVE_TRACE_RECORD_COUNT` is now a guarded 64-record fallback.
- `Core/Bank/Scene/AutosaveTrace.c`
  - E3: `config.h` included before `AutosaveTrace.h`; retained-storage comment
    generalized from 64 to the configured count.
- `Core/Hardware/SD/filesystem.c`
  - E4: `AUTOSAVE_TRACE_FLUSH_BATCH_RECORDS` macro and a `_Static_assert` that
    the 512-byte staging buffer holds whole 8-byte records.
  - E5: flush phase 0 caps each snapshot to the batch size (64), not the ring
    size.
  - E6: completion callback re-arms the scheduler immediately after a
    successful full-batch append while a full batch remains.
  - E7: `filesystem_autosaveTraceFlushBlocking()` loops until every pending
    batch is drained.
- `Core/Hardware/SD/filesystem.h`
  - Blocking-helper contract updated to "every pending batch".

### 3.5 F1 include-reorder fix (applied this turn)

Review found a macro-redefinition hazard: `Autosave.c` and `menu.c` included
`AutosaveTrace.h` before `config.h`, so the 64 fallback would be defined and
then `config.h` would redefine `AUTOSAVE_TRACE_RECORD_COUNT` to 2048.

Fixed by reordering those two files so `config.h` precedes `AutosaveTrace.h`:

- `Core/Bank/Scene/Autosave.c`
- `Core/Menu/menu.c`

This makes every translation unit that includes both headers observe the
config-owned value first, eliminating the redefinition sequence. The change is
noted in `TRACE_EXTENSION.md` section 11.5.

## 4. Verification status

- Static review only. `arm-none-eabi-gcc` is not on PATH in this environment,
  so no compile, link, size, or symbol check has been run.
- `git diff --check` passes for all modified files; the only notices are the
  repository's normal LF-to-CRLF warnings.
- Recursive search confirms no `.failed` or `_failed.*` source remains.
- The following remain unverified until a real toolchain run:
  - `make clean && make` for both `DEV_MODE_LOGGING=1` and `0`.
  - `_Static_assert` evaluation and absence of the redefinition warning.
  - `.bss` delta of about +15,872 bytes at 2048 records.
  - Absence of `autosave_trace_records` in the logging-off image.
  - Hardware Scene/Kit Load trace capture with the 2048 ring.

## 5. Known risks and open items

- Temporary 2048 expansion must be reverted to
  `AUTOSAVE_TRACE_RECORD_COUNT_DEFAULT` after the experiment; re-check `.bss`.
- E6 continuous re-arm may increase trace append frequency while a backlog of
  at least one full batch remains. Settings still claims the facade first each
  tick, but the autosave writer runs after trace.
- `/asavetrc.bin` grows up to 16,384 bytes per full drain during the
  experiment.
- A naive reader may see a Scene mark's nested KIT + SCENE `L` records as a
  duplicate; future decoder must group by kind and Scene index.
- Refactor items 5.4 and 5.6 remain intentionally deferred.
- `SRAM_MANIFEST.md` needs updating if a logging-on manifest snapshot is
  produced during the 2048 experiment.

## 6. Recommended next steps

1. Stage and commit in logical units:
   - `AUTOSAVE_REFACTOR.md` + C1-C7 refactor changes.
   - `TRACE_EXTENSION.md` + E1-E8 trace extension + F1 reorder.
2. On the real embedded toolchain, build both logging configurations and
   confirm no redefinition warning, the static assert passes, and the expected
   `.bss` delta appears.
3. Flash the logging-on build and capture a Scene Load and Kit Load, verifying
   the terminal `L` records and the earlier `D`/`I` records survive in
   `/asavetrc.bin`.
4. After the diagnostic is complete, revert the effective
   `AUTOSAVE_TRACE_RECORD_COUNT` to `AUTOSAVE_TRACE_RECORD_COUNT_DEFAULT` (64),
   rebuild, and restore the pre-experiment `.bss` baseline.
