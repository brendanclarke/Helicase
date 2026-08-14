# AUTOSAVE_REFACTOR.md - Autosave Trace / Load-Mark Refactor Implementation Plan

## 1. Purpose

This document is the executable, phased implementation plan for the fixes and
changes staged in `autosave-trace_refactor/`. It turns the proposed
replacement files and their diffs into an ordered sequence of source edits,
build checks, and hardware checks against the live `Core/` tree.

The changes address the Scene/Kit/Bank Load AutoSave trace "black hole"
diagnosed in `autosave-trace_refactor/LOAD_SCENE_TRACE_AUDIT.md`: a Scene Load
already marks dirty bytes, but its per-byte `D` trace flood overflows the
64-record ring before anything can flush it, and the flush scheduler refuses
to run while the Load/Save page is open. The fix adds a durable whole-object
`L` summary record and narrows the flush guard to the actual busy window.

This plan implements refactor items 5.1, 5.2, 5.3, and 5.5. Items 5.4 and 5.6
are intentionally deferred and are documented in the out-of-scope section.

## 2. Current repository state

- The twelve `*.failed` / `*_failed.*` shadow files have been removed manually
  from the working tree (refactor item 5.1). `git status --short` shows them as
  deletions:

  ```text
   D Core/Bank/BankData.c.failed
   D Core/Bank/BankData.h.failed
   D Core/Bank/Scene/Autosave_failed.c
   D Core/Bank/Scene/Autosave_failed.h
   D Core/Bank/Scene/Preset/presetManager.c.failed
   D Core/Bank/Scene/Preset/presetManager.h.failed
   D Core/Bank/Scene/SceneData.c.failed
   D Core/Bank/Scene/SceneData.h.failed
   D Core/Hardware/SD/filesystem.c.failed
   D Core/Hardware/SD/filesystem.h.failed
   D Core/Menu/menu.c.failed
   D Core/Menu/menu.h.failed
  ```

- A recursive filesystem search confirms no `.failed` or `_failed` source
  remains anywhere in the tree.
- The deletions are not yet staged. They must be staged/committed before the
  code changes in this plan are landed (see Phase 1).
- The five source files that still need editing are unchanged and present at
  their live paths.

## 3. Sources of truth

- Proposed replacement sources and notes:
  `autosave-trace_refactor/Autosave.c`
  `autosave-trace_refactor/AutosaveTrace.h`
  `autosave-trace_refactor/filesystem.c`
  `autosave-trace_refactor/menu.c`
  `autosave-trace_refactor/menu.h`
  `autosave-trace_refactor/CHANGES.md`
  `autosave-trace_refactor/LOAD_SCENE_TRACE_AUDIT.md`
- Live files to edit:
  `Core/Bank/Scene/Autosave.c`
  `Core/Bank/Scene/AutosaveTrace.h`
  `Core/Hardware/SD/filesystem.c`
  `Core/Menu/menu.c`
  `Core/Menu/menu.h`

The five proposed sources in `autosave-trace_refactor/` are the already-applied
target state. The edits below are derived from `git diff --no-index` between
the live files and those proposed files.

## 4. Complete change inventory

This section is the exhaustive list of every place in the codebase that must
change to implement items 5.2, 5.3, and 5.5 (5.1 is already complete). Each
entry states what the change does, why it must exist, its inputs, its outputs,
and the exact edit. Section 4.8 then lists the places that intentionally do
not change, with the reason, so the inventory is complete.

Summary of edit sites:

| ID | File | Change |
| --- | --- | --- |
| C1 | `Core/Bank/Scene/AutosaveTrace.h` | add `AUTOSAVE_TRACE_STAGE_LOAD_MARK = 'L'` enum member |
| C2 | `Core/Bank/Scene/AutosaveTrace.h` | add `AUTOSAVE_TRACE_LOAD_MARK_*` kind/flag/shift macros |
| C3 | `Core/Bank/Scene/Autosave.c` | emit `L` (KIND_KIT) from `autosave_markKitDirty()` |
| C4 | `Core/Bank/Scene/Autosave.c` | emit `L` (KIND_SCENE) from `autosave_markSceneWithoutPatternDirty()` |
| C5 | `Core/Menu/menu.c` | define `menu_isLoadSaveCommandActive()` accessor |
| C6 | `Core/Menu/menu.h` | declare `menu_isLoadSaveCommandActive()` |
| C7 | `Core/Hardware/SD/filesystem.c` | replace flush guard with `menu_isLoadSaveCommandActive()` |

### 4.1 C1 - new stage code `AUTOSAVE_TRACE_STAGE_LOAD_MARK = 'L'`

File: `Core/Bank/Scene/AutosaveTrace.h`
Location: in `typedef enum { ... } autosave_trace_stage_t`, between
`AUTOSAVE_TRACE_STAGE_INSTRUMENT_ENTRY = 'N',` (line 58) and
`AUTOSAVE_TRACE_STAGE_SCHEDULED = 'S',` (line 59).

What it does:

Adds a new one-byte stage code to the autosave lifecycle trace enum. The code
is `'L'`, which is currently unused by any producer and is not matched by any
switch elsewhere. It becomes a legal first argument to the already-existing
`autosaveTrace_record(stage, flags, value)` API.

Why it must exist:

Without a terminal whole-object stage, the only evidence for Scene/Kit Load
marking is the per-byte `AUTOSAVE_TRACE_STAGE_DIRTY` (`'D'`) stream and the
per-Instrument `'I'` summaries. One Scene Load emits hundreds of `D` records
in one synchronous call, wrapping the 64-record ring before any flush can run.
`'L'` is emitted once at the end of a whole-object marker, so it is the single
durable fact guaranteed to survive the wrap. It is the direct Scene/Kit/Bank
analogue of the existing per-Instrument `'I'` summary stage.

Inputs:

None at the header level. It is a compile-time enum constant consumed later by
`autosaveTrace_record()`.

Outputs:

A valid stage value that `AutosaveTrace.c` serializes into
`record[AUTOSAVE_TRACE_STAGE_OFFSET]` as one byte (`0x4C`). No runtime storage,
counter, or allocation is introduced.

Exact edit:

```c
    AUTOSAVE_TRACE_STAGE_INSTRUMENT_ENTRY = 'N',
    /*
     * One whole-Kit or whole-Scene-without-Pattern dirty-mark outcome,
     * emitted once at the end of autosave_markKitDirty() and
     * autosave_markSceneWithoutPatternDirty() respectively. A single call
     * into either function can emit far more D records (Scene settings plus
     * up to six full Instrument scopes) than the 64-record ring holds, all
     * within one synchronous call with no intervening flush opportunity, so
     * this terminal record is the only evidence guaranteed to survive that
     * wrap. It mirrors AUTOSAVE_TRACE_STAGE_INSTRUMENT_MARK's role for the
     * per-Instrument case. Root Scene Load, root Bank Load (which marks each
     * selected child Scene through the same Scene marker), and root Kit Load
     * all become observable through this one stage rather than requiring a
     * separate summary per call site.
     */
    AUTOSAVE_TRACE_STAGE_LOAD_MARK = 'L',
    AUTOSAVE_TRACE_STAGE_SCHEDULED = 'S',
```

### 4.2 C2 - LOAD_MARK kind, flag, and shift macros

File: `Core/Bank/Scene/AutosaveTrace.h`
Location: after the `AUTOSAVE_TRACE_INSTRUMENT_ENTRY_*` block (after line 139,
`#define AUTOSAVE_TRACE_INSTRUMENT_ENTRY_TYPE_SHIFT   8u`), immediately before
the `autosaveTrace_record` prototype.

What it does:

Defines the bit-layout contract for the `'L'` record's flags and 32-bit value
fields:

- `AUTOSAVE_TRACE_LOAD_MARK_KIND_KIT` (`0`) and
  `AUTOSAVE_TRACE_LOAD_MARK_KIND_SCENE` (`1`) distinguish which whole-object
  marker emitted the record.
- `AUTOSAVE_TRACE_LOAD_MARK_FLAG_TRACKING_ENABLED` (`1 << 0`) snapshots the
  production mutation-tracking gate at marking time.
- `AUTOSAVE_TRACE_LOAD_MARK_KIND_SHIFT` (`0`) and
  `AUTOSAVE_TRACE_LOAD_MARK_SCENE_SHIFT` (`2`) define how kind and Scene index
  pack into the value field.

Why it must exist:

The fixed eight-byte trace record has one flags byte and one 32-bit value
word. The macros make the `'L'` record self-describing in the same way the
existing `'I'`/`'J'`/`'N'` macros do, so a raw `asavetrc.bin` can be read
without a decoder and so a Scene Load's nested KIT/SCENE records are not
mistaken for duplicates. Tracking-enable is recorded because a marker can be
invoked while tracking is off, in which case it produced no dirty bytes.

Inputs:

None. These are preprocessor constants.

Outputs:

Symbols used by `Autosave.c` to build the `flags` and `value32` arguments of
`autosaveTrace_record()`.

Exact edit:

```c
/*
 * Kind codes distinguishing which whole-object marker emitted a LOAD_MARK
 * record. A whole-Scene mark internally calls the whole-Kit mark, so one
 * Scene Load can legitimately emit both a KIT and a SCENE record for the
 * same scene_index; that nesting is intentional and mirrors the existing
 * INSTRUMENT_MARK-inside-Kit relationship rather than being a duplicate.
 */
#define AUTOSAVE_TRACE_LOAD_MARK_KIND_KIT    0u
#define AUTOSAVE_TRACE_LOAD_MARK_KIND_SCENE  1u

/*
 * Flag for AUTOSAVE_TRACE_STAGE_LOAD_MARK. Snapshots the production
 * mutation-tracking gate at the moment marking ran, matching
 * AUTOSAVE_TRACE_INSTRUMENT_MARK_FLAG_TRACKING_ENABLED's role.
 */
#define AUTOSAVE_TRACE_LOAD_MARK_FLAG_TRACKING_ENABLED  (1u << 0u)

/*
 * AUTOSAVE_TRACE_STAGE_LOAD_MARK value32 layout: kind in bits 0..1, Scene
 * index in bits 2..5. Higher bits are reserved as zero. This record proves
 * only that the marker ran for this Scene/kind; per-byte expected/accepted
 * counts for the nested whole-Instrument calls remain in their own I records.
 */
#define AUTOSAVE_TRACE_LOAD_MARK_KIND_SHIFT   0u
#define AUTOSAVE_TRACE_LOAD_MARK_SCENE_SHIFT  2u
```

### 4.3 C3 - emit `L` (KIND_KIT) from autosave_markKitDirty()

File: `Core/Bank/Scene/Autosave.c`
Location: `autosave_markKitDirty(uint8_t scene_index)` (lines 1236-1256).
Declare two locals after `uint8_t slot;` (line 1239); add the record after the
six-instrument loop (line 1255), before the closing brace (line 1256).

What it does:

Adds two stack-local variables (`trace_flags`, `trace_value`) and, after the
existing Kit-parameter and six-instrument dirty loops complete, emits exactly
one `AUTOSAVE_TRACE_STAGE_LOAD_MARK` record with kind `KIT` and the destination
Scene index.

Why it must exist:

A whole-Kit mark produces two Kit-level `D` records plus, for each of six
instruments, a `D` flood and one `I` summary. Any of those can wrap the shared
64-record ring before the filesystem tick can flush. The single terminal `L`
record is emitted after the loop, so it is the durable witness that this
whole-Kit marker actually ran for `scene_index`, independent of how many
preceding records were lost.

Inputs:

- `scene_index`: the destination Scene the Kit is marked into.
- `autosave_mutation_tracking_enabled`: the production dirty-mask gate; it
  controls the `FLAG_TRACKING_ENABLED` bit but does not prevent the record.

Outputs:

- The same dirty-bit production as before (the loops are unchanged).
- One eight-byte trace record when `DEV_MODE_LOGGING` is 1, or a no-op call
  when it is 0. No persistent state is added.

Exact edit:

```c
void autosave_markKitDirty(uint8_t scene_index)
{
    uint8_t parameter_index;
    uint8_t slot;
    uint8_t trace_flags = 0u;
    uint32_t trace_value;

    /* ... existing doc comment and loops unchanged ... */
    for (parameter_index = 0u; parameter_index < AUTOSAVE_KIT_PARAM_COUNT;
         parameter_index++) {
        autosave_markKitParameterDirty(scene_index, parameter_index);
    }
    for (slot = 0u; slot < AUTOSAVE_INSTRUMENTS_PER_KIT; slot++)
        autosave_markWholeInstrumentDirty(scene_index, slot);

    /*
     * One durable outcome record for this whole-Kit mark, emitted after the
     * six per-Instrument I records above, any of which may already have
     * wrapped the shared D-record ring (see AUTOSAVE_TRACE_STAGE_LOAD_MARK).
     */
    if (autosave_mutation_tracking_enabled)
        trace_flags |= AUTOSAVE_TRACE_LOAD_MARK_FLAG_TRACKING_ENABLED;
    trace_value = ((uint32_t)AUTOSAVE_TRACE_LOAD_MARK_KIND_KIT <<
                   AUTOSAVE_TRACE_LOAD_MARK_KIND_SHIFT) |
                  ((uint32_t)scene_index << AUTOSAVE_TRACE_LOAD_MARK_SCENE_SHIFT);
    autosaveTrace_record(AUTOSAVE_TRACE_STAGE_LOAD_MARK, trace_flags,
                         trace_value);
}
```

### 4.4 C4 - emit `L` (KIND_SCENE) from autosave_markSceneWithoutPatternDirty()

File: `Core/Bank/Scene/Autosave.c`
Location: `autosave_markSceneWithoutPatternDirty(uint8_t scene_index)` (lines
1277-1296). Declare two locals after `uint8_t parameter_index;` (line 1279);
add the record after `autosave_markKitDirty(scene_index);` (line 1295), before
the closing brace (line 1296).

What it does:

Adds two stack-local variables and, after the Scene parameters, Effect stub,
and nested whole-Kit mark all complete, emits one
`AUTOSAVE_TRACE_STAGE_LOAD_MARK` record with kind `SCENE` and the destination
Scene index.

Why it must exist:

This is the exact function reached by `on_scene_load_complete()` for root Scene
Load and by the selected-child loop for Bank Load. Its marking burst is the
largest of all (Scene settings plus Effect plus the full Kit scope), so it is
the most likely to wrap the ring. The outer `L` KIND_SCENE record proves the
whole-Scene marker ran for this `scene_index`; the nested `L` KIND_KIT from
`autosave_markKitDirty()` is intentionally also emitted and is not a duplicate.

Inputs:

- `scene_index`: the destination Scene being marked.
- `autosave_mutation_tracking_enabled`: the production dirty-mask gate.

Outputs:

- The same Scene/Effect/Kit dirty production as before.
- One `'L'` KIND_SCENE record (plus the nested `'L'` KIND_KIT record from the
  callee) when `DEV_MODE_LOGGING` is 1, or no-op calls when it is 0. No
  persistent state is added.

Exact edit:

```c
void autosave_markSceneWithoutPatternDirty(uint8_t scene_index)
{
    uint8_t parameter_index;
    uint8_t trace_flags = 0u;
    uint32_t trace_value;

    /* ... existing doc comment and loop unchanged ... */
    for (parameter_index = 0u; parameter_index < AUTOSAVE_SCENE_PARAM_COUNT;
         parameter_index++) {
        autosave_markSceneParameterDirty(scene_index, parameter_index);
    }
    autosave_markEffectDirty(scene_index);
    autosave_markKitDirty(scene_index);

    /*
     * One durable outcome record for this whole-Scene mark, emitted after
     * the nested whole-Kit mark's own LOAD_MARK record above (and all of the
     * D/I records both may have produced). This is the fix for the Scene
     * Load AutoSave trace "black hole": previously nothing survived to prove
     * this function ran for a given scene_index once its D-record flood
     * wrapped the ring, with no chance to flush in between since marking is
     * one synchronous call. See LOAD_SCENE_TRACE_AUDIT.md.
     */
    if (autosave_mutation_tracking_enabled)
        trace_flags |= AUTOSAVE_TRACE_LOAD_MARK_FLAG_TRACKING_ENABLED;
    trace_value = ((uint32_t)AUTOSAVE_TRACE_LOAD_MARK_KIND_SCENE <<
                   AUTOSAVE_TRACE_LOAD_MARK_KIND_SHIFT) |
                  ((uint32_t)scene_index << AUTOSAVE_TRACE_LOAD_MARK_SCENE_SHIFT);
    autosaveTrace_record(AUTOSAVE_TRACE_STAGE_LOAD_MARK, trace_flags,
                         trace_value);
}
```


### 4.5 C5 - define menu_isLoadSaveCommandActive() in menu.c

File: `Core/Menu/menu.c`
Location: Accessors section, immediately after line 8941
(`uint8_t menu_getActivePage(void) { return menu_activePage; }`).

What it does:

Adds a non-static read-only accessor that returns the file-static
`menu_loadSaveCommandActive` flag.

Why it must exist:

`menu_loadSaveCommandActive` is the precise "an accepted OK/OW command is
actively executing" signal, distinct from `menu_activePage`, which only says
the user is on the Load/Save page. It is currently `static` in `menu.c`, so
`filesystem.c` cannot read it directly. The accessor exposes exactly the one
fact `filesystem.c` needs without making the flag globally writable or
inventing a second, looser proxy.

Inputs:

None. Reads the existing static flag.

Outputs:

`0` when no accepted Load/Save command is in flight; `1` while an accepted
command and its post-apply root-index restore are active.

Exact edit:

```c
uint8_t menu_getActivePage(void)   { return menu_activePage; }

/*
** menu_isLoadSaveCommandActive
** ----------------------------------------------------------------------
** Read-only accessor for the accepted-OK/OW-command busy window (see the
** menu_loadSaveCommandActive declaration comment above for the exact
** lifetime). menu_loadSaveCommandActive itself stays file-static; this is
** the one sanctioned way for another module (currently
** filesystem_autosaveTraceFlushSchedule_tick()) to gate on it without
** widening the flag's scope or inventing a second, looser proxy signal
** such as menu_activePage.
*/
uint8_t menu_isLoadSaveCommandActive(void) { return menu_loadSaveCommandActive; }
```

### 4.6 C6 - declare menu_isLoadSaveCommandActive() in menu.h

File: `Core/Menu/menu.h`
Location: immediately after line 410 (`uint8_t menu_getActivePage(void);`).

What it does:

Adds the public prototype for `menu_isLoadSaveCommandActive()`.

Why it must exist:

`filesystem.c` includes `menu.h` and calls the accessor, so the symbol must be
declared for the translation unit to compile cleanly. The comment documents the
contract so future callers use the busy window rather than `menu_activePage`.

Inputs:

None. This is a declaration only.

Outputs:

A linkable declaration consumed by `filesystem.c`.

Exact edit:

```c
uint8_t menu_getActivePage(void);
/*
 * Read-only accepted-OK/OW-command busy window (see menu_loadSaveCommandActive
 * in menu.c). Use this rather than menu_activePage when a caller needs to
 * know whether a Load/Save command is actively executing right now, as
 * opposed to merely being on the Load/Save page.
 */
uint8_t menu_isLoadSaveCommandActive(void);
```

### 4.7 C7 - narrow the trace-flush guard in filesystem.c

File: `Core/Hardware/SD/filesystem.c`
Location: `filesystem_autosaveTraceFlushSchedule_tick()`; replace the guard at
line 19620 and update the surrounding comment (lines 19607-19619).

What it does:

Replaces the page-based guard
`if (menu_activePage == LOAD_PAGE || menu_activePage == SAVE_PAGE) return;`
with the busy-based guard `if (menu_isLoadSaveCommandActive()) return;`.

Why it must exist:

The old guard blocked the optional `asavetrc.bin` append for the entire time
the user stayed on the Load/Save page, including long after the accepted
command had already completed. That is the second half of the black hole: even
the surviving ~64 trace records could not reach the card while still on the
page. The new guard still protects the single filesystem facade during the
real busy window (an accepted command and its deferred root-index restore), but
allows the ring to flush as soon as that command completes, without requiring
page navigation.

Inputs:

- `menu_isLoadSaveCommandActive()`: `0` or `1`, from the new accessor.

Outputs:

- If the command is active: the function returns early, retaining the RAM trace
  ring and its existing flush deadline (same behavior as the old guard during
  the genuinely busy window).
- If no command is active: execution continues to the existing pending-count,
  deadline, and `filesystem_start(FS_INTERNAL_OP_AUTOSAVE_TRACE_FLUSH, ...)`
  path, so pending records flush to `asavetrc.bin`.

Exact edit:

```c
    /*
     * Reserve the one filesystem facade for a foreground Load/Save command.
     *
     * Input: menu_isLoadSaveCommandActive(), true only for the lifetime of
     * an accepted OK/OW command and its post-apply root-index restore (see
     * menu_beginLoadSaveCommand()/menu_finishLoadSaveCommand() in menu.c).
     * Output: retain the RAM trace ring and its existing deadline without
     * opening `asavetrc.bin`. Why: this optional diagnostic append previously
     * could start between Bank payload completion and Menu's final read-only
     * `.hcindex` request, making that foreground request fail with generic
     * FsErr solely because the shared facade was busy.
     *
     * This was previously gated on menu_activePage == LOAD_PAGE/SAVE_PAGE,
     * which blocked flushing for as long as the user simply remained on the
     * page after a command had already completed. Gating on the narrower
     * busy flag lets the ring flush as soon as the accepted command actually
     * finishes, without requiring page navigation first. No trace data is
     * discarded and no foreground filesystem operation is delayed either
     * way.
     */
    if (menu_isLoadSaveCommandActive())
        return;
```

The minimal predicate change is:

```diff
-    if (menu_activePage == LOAD_PAGE || menu_activePage == SAVE_PAGE)
+    if (menu_isLoadSaveCommandActive())
         return;
```

### 4.8 Places that intentionally do not change

These are enumerated so "every place the code must change" is exhaustive and no
further edits are required to fulfil items 5.2/5.3/5.5.

- `Core/Bank/Scene/AutosaveTrace.c`: no change. `autosaveTrace_record()` already
  accepts an arbitrary `autosave_trace_stage_t` and serializes it byte-for-byte;
  it needs no knowledge of `'L'`.
- `Core/Bank/Scene/AutosaveTrace.h` function prototypes: no signature change.
  `autosaveTrace_record()` already takes `(stage, flags, value)`.
- `Core/Bank/Scene/Preset/presetManager.c`: no change. The three completion
  callbacks (`on_kit_load_complete`, `on_scene_load_complete`,
  `on_bank_load_complete`) already call the markers that now emit `L`; no new
  call site is needed.
- `Makefile`: no change. The shadow files were never in `SRCS`/`DSP_SRCS`, and
  `DEV_MODE_LOGGING` is defined in `config.h`, not in `Makefile`.
- Trace decode tooling: no change in this pass. The settled `asavetrc.bin`
  decoder/converter is already deferred project tooling; the `L` record is
  human-readable as a raw one-byte stage code.

## 3.1 Implementation notes (2026-08-14)

- Confirmed the worktree was clean and no `.failed` or `_failed.*` files
  remain, so Phase 1 required no additional deletion or staging.
- Applied C1-C7 to the five live files. Each new C/H change has adjacent
  comment text describing its purpose and ownership; the existing trace ring
  and persistent RAM allocations remain unchanged.
- Build checks are intentionally skipped because the embedded toolchain is not
  installed in this environment. Static source inspection and diff checks are
  the available verification for this pass.
- `git diff --check` passes after restoring the repository's LF line endings.
  A broad `git status`/`git diff --stat` scan timed out on the mounted
  worktree, so no build or unbounded repository scan was attempted afterward.

## 5. Dependency and phase ordering

- C1 must precede C3 and C4 (the enum member must exist before the code
  references it).
- C2 must precede C3 and C4 (the macros must exist before the code uses them).
- C5 must precede C7 (the accessor must exist before `filesystem.c` calls it).
- C6 must precede or accompany C7 (the prototype must be visible to
  `filesystem.c`).
- C3 and C4 are independent of C5-C7; they can be built in either order, but
  the plan groups all five source edits into two coherent passes below.

## 6. Implementation phases

### Phase 1 - 5.1: stage the already-completed shadow-file removal

The working tree deletions are present but unstaged. Verify and record them:

```powershell
Get-ChildItem -Path . -Recurse -File -Force |
  Where-Object { $_.Name -match '\.failed$|_failed\.' }
rg -n '\.failed|_failed' Makefile Core -g '*.c' -g '*.h' -g '*.s' -g '*.ld'
git status --short
```

Expected: the `Get-ChildItem` search returns nothing; the `rg` search returns
only legitimate identifiers such as `fs_autosave_setup_failed` and
`fs_boot_logging_recovery_failed`; `git status` shows the twelve `D` entries.

When ready, stage and commit the deletions separately from the code changes:

```powershell
git add -u
git commit -m "Remove unused .failed shadow sources"
```

### Phase 2 - 5.2: add the LOAD_MARK stage and emit it

Apply, in order:

1. C1 (enum member in `AutosaveTrace.h`).
2. C2 (kind/flag/shift macros in `AutosaveTrace.h`).
3. C3 (`L` KIND_KIT in `Autosave.c`).
4. C4 (`L` KIND_SCENE in `Autosave.c`).

Then build:

```powershell
make clean
make
```

### Phase 3 - 5.5 + 5.3: add the accessor and narrow the guard

Apply, in order:

1. C5 (accessor definition in `menu.c`).
2. C6 (accessor declaration in `menu.h`).
3. C7 (flush-guard predicate change in `filesystem.c`).

Then build:

```powershell
make clean
make
```

### Phase 4 - build and static verification

1. Logging-on build and size comparison against a clean baseline:

   ```powershell
   make clean
   make
   arm-none-eabi-size build/lxr02.elf
   ```

   Expect no meaningful `.bss`/`.data` change; `L` uses the existing 512-byte
   ring.

2. Confirm the new symbols resolve:

   ```powershell
   rg -n 'AUTOSAVE_TRACE_STAGE_LOAD_MARK|AUTOSAVE_TRACE_LOAD_MARK_|menu_isLoadSaveCommandActive' Core
   ```

3. Logging-off build to prove the new code degrades to a no-op and no trace
   ring is linked:

   ```powershell
   # temporarily edit config.h: #define DEV_MODE_LOGGING 0
   make clean
   make
   arm-none-eabi-size build/lxr02.elf
   arm-none-eabi-nm build/lxr02.elf | Select-String 'autosave_trace_records'
   # restore config.h: #define DEV_MODE_LOGGING 1
   ```

   The symbol query must return nothing, and `make` must succeed.

4. Diff hygiene:

   ```powershell
   git diff --check
   git diff --stat
   ```

### Phase 5 - hardware verification

Run with `DEV_MODE_LOGGING=1`, in order:

1. Flash and boot with AutoSave enabled.
2. Scene Load: accept `Load:[Scene   ]` OK, stay on the page, then inspect
   `/asavetrc.bin`. Expect an `L` KIND_SCENE record plus a nested `L` KIND_KIT
   for the loaded Scene index. Before this change, no write would have occurred
   while still on the page.
3. Kit Load: repeat for `Load:[Kit     ]`. Expect an `L` KIND_KIT record.
4. Confirm the terminal `L` survives even when preceding `D` records wrap the
   ring.
5. Confirm no foreground regression: a normal Load/Save still produces
   `A/V/M/C/P/T` and reaches publication with no `FsErr`/glitch.
6. Logging-off smoke check: `DEV_MODE_LOGGING=0` build runs normally and writes
   no `asavetrc.bin`.

### Phase 6 - close-out and documentation

1. Update `MEMORY.md` with the completed items 5.1, 5.2, 5.3, 5.5, and note
   that 5.4 and 5.6 remain deferred pending `L` validation.
2. Write/update the session handoff log under `knowledge_files/log_archive/`
   with exact size deltas and saved `asavetrc.bin` fixtures.
3. Remove `autosave-trace_refactor/` after the changes are committed, since the
   proposed files will then match the live tree.

## 7. Intentionally out of scope

Per `autosave-trace_refactor/CHANGES.md`, do not fold these into this pass:

- 5.4 shared completion-marking helper in `presetManager.c`. The three
  completion callbacks remain untouched; they already call the markers that now
  emit `L`.
- 5.6 shared directory-tick state machine in `filesystem.c`. Deferred until
  5.2/5.3 are validated on hardware with the new `L` records as the regression
  detector.

Each must be a separate future change with its own hardware checkpoint.

## 8. Rollback

- Phase 1 deletions are recoverable from git history if reference copies are
  still needed (they were deleted, not moved to an archive).
- C1-C7 are small, localized edits; reverting the affected file restores prior
  behavior.
- Capture `git diff` per phase before reverting so unrelated work is not lost.

## 9. Verification matrix

| Check | Phase | Expected result |
| --- | --- | --- |
| No shadow sources in tree | 1 | `Get-ChildItem` finds none |
| No Makefile/source `.failed` references | 1 | only legitimate `*_failed` identifiers remain |
| Logging-on build | 4 | `make` succeeds, no `.bss` growth |
| Logging-off build | 4 | `make` succeeds, no trace ring symbol |
| Scene Load `L` on page | 5 | `KIND_SCENE` and nested `KIND_KIT` records |
| Kit Load `L` on page | 5 | `KIND_KIT` record |
| Writer lifecycle intact | 5 | `A/V/M/C/P/T` reaches publication |
| Logging-off runtime | 5 | no `/asavetrc.bin` write |

## 10. Risk review (2026-08-14)

The C1-C7 edits are present in the working tree and match the intended
behavior on source inspection, but they are not compile-verified because
`arm-none-eabi-gcc` is not installed in this environment. The risks below are
ordered by severity.

### 10.1 Verification status

- Applied: C1-C7 to the five live files.
- Applied: the twelve shadow-file removals (already committed).
- Static checks passed: recursive shadow-file search is empty; all new symbols
  resolve; `git diff --check` reports no whitespace errors (it does print the
  repository's normal LF-to-CRLF warnings).
- Not performed: `make` (logging-on and logging-off), `arm-none-eabi-size`,
  logging-off trace-ring symbol check, and all hardware fixtures.

### 10.2 Risk register

| # | Severity | Risk | Impact | Mitigation |
| --- | --- | --- | --- | --- |
| R1 | High | Changes are not compile-verified. The toolchain is absent, so a typo, missing include, or `-Werror`-class warning would be invisible here. | A broken build would not be discovered until the real toolchain run. | Run `make clean && make` for both `DEV_MODE_LOGGING=1` and `0` before commit/hardware; check size and the absence of `autosave_trace_records` in the logging-off image. |
| R2 | Medium | Working tree changes are uncommitted and git reports LF-will-become-CRLF warnings. | Risk of losing unstaged edits or introducing line-ending churn on commit. | Stage and commit C1-C7 separately; normalize line endings to the repository convention first so the commit diff is only the intended change. |
| R3 | Medium | Residual ring wrap can still overwrite `L` records. `autosave_markResidentBankDirty()` loops up to 16 scenes, each emitting nested KIT+SCENE `L` records plus a large `D`/`I` flood; early-scene `L` records can wrap before any flush. | Diagnostic only; it cannot change dirty-bit production or persistence, but a full-Bank re-enable may show only the last scenes' `L` records. | Treat `L` as "marker ran", not a complete census. For multi-scene Bank proof, compare the two durable HCPRMS generations as the existing Session 048 note already prescribes. Do not enlarge the ring without RAM-approval. |
| R4 | Medium | A naive `asavetrc.bin` reader may read a Scene mark's two `L` records (KIT then SCENE) as a duplicate. | Confusing diagnosis, not a firmware defect. | Document the kind bits; any future decoder must group by scene index and kind rather than dedupe by stage alone. |
| R5 | Low | `menu_loadSaveCommandActive` could become stuck at 1 if an accepted command terminates without reaching `menu_finishLoadSaveCommand()`. | The diagnostic flush (and the existing `...` UI) would be blocked; foreground I/O and the autosave writer are unaffected. | This flag-lifecycle invariant predates C7, but C7 now ties trace flush to it. Verify every accepted-command error/timeout path clears the flag; if a recurrence appears, clear it on terminal filesystem status or add a watchdog. |
| R6 | Low | In `DEV_MODE_LOGGING=0`, the two markers now read `autosave_mutation_tracking_enabled`, compute shifts/ORs, and call the no-op stub. | Negligible CPU on whole-object loads; no state or I/O is added. | Accept as-is; no action required beyond noting it. |
| R7 | Low | New locals are declared mid-function after the doc comment rather than with the file's top-of-function declarations. | Cosmetic; legal under `-std=gnu11`. | Optionally move `trace_flags`/`trace_value` next to the existing locals for consistency. |
| R8 | Low | The applied comments are condensed relative to the plan's "Exact edit" blocks and the `autosave-trace_refactor/` copies. | Documentation drift only; code behavior is identical. | Either reconcile the comment text or remove `autosave-trace_refactor/` after the change is committed and confirmed. |
| R9 | Low | No decoder/tooling exists for the new `L` stage yet. | Raw card inspection only for now. | Already deferred project tooling; not required for this pass. |
| R10 | Informational | `menu_isLoadSaveCommandActive()` is a plain read of a non-volatile static, safe only from the main-loop context used today. | None now. | If the flag ever becomes ISR-writable, add a critical section/volatile. |

### 10.3 Things checked and found safe

- C1 stage code `'L'` was previously unused and is not consumed by any switch.
- C2 shift layout fits `kind` (2 bits) and `scene_index` (4 bits) without
  overlap or truncation.
- C7 still protects the original hazard window: `menu_loadSaveCommandActive`
  remains set through `menu_requestLoadCommandFinalIndexRestore()` and is
  cleared only in `menu_finishLoadSaveCommand()`, so the deferred root-index
  restore is still guarded.
- The flush scheduler is only invoked when the filesystem is `IDLE`, so the
  narrower guard cannot make the optional append contend with a BUSY facade.
- `AutosaveTrace.c`, `presetManager.c`, and `Makefile` correctly require no
  changes for this pass.
