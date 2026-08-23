# Session 056 — HCNAMES Scene-Row Corruption Investigation

Follow-up to `S056_BANK_TESTS.md` §3.1: after the tested Bank Load, HCNAMES
row 9 (Scene 08's identity) read `Moch to` — the name of an unrelated
*root-library* Kit/Scene slot 004 — instead of `KitWool`, the correct
Bank-local Scene 08 name. This document traces the mechanism as far as
static code reading can take it, proposes a structural fix that closes the
risk without needing to name the exact clobbering statement, and proposes a
trace hook that will name it precisely if reproduced.

## 1. Recap of the evidence

- `Bank/012 LoadTst/08 KitWool/` is a completely normal, correctly-formed
  child: `Kit KitWool`, `sceneset.scg`, `pattern.pat`, `effects.fx` all
  present and consistent.
- HCNAMES row 25 (`Kit 08`, the *Kit* name for the same child) correctly
  reads `KitWool`.
- HCNAMES rows 105–110 (the six *Instrument* names for the same child) are
  all correct.
- Only HCNAMES row 9 (`Scene 08`, the *Scene's own* name) is wrong, and it's
  not simply stale (no Bank in the library — including the previously
  loaded Bank 011 — has a Scene 08 named `Moch to`). It's root-library slot
  004's name (`Kit/004 Moch to`, `Scene/004 Moch to`), which has nothing to
  do with Bank 012 or its Scene 08 at all.
- Every other identity row for every other child (15 Scenes, 16 Kits, 96
  Instruments, the Bank row) is correct. This is a single-row, single-field
  defect, not a wholesale sweep failure.

The Scene name and the Kit name for the same child are sourced from **two
different variables** (see below), and only the Scene one is wrong — that
asymmetry is the main clue this document follows.

## 2. Tracing the write path

`filesystem.c` uses one shared, giant per-tick state machine
(`filesystem_loadSceneDirectory_tick()`) for both a root Scene Load *and* a
Bank-local child Scene load — Bank Load reuses it per selected child,
switching on `op_bank_child_cursor`. Two module-level statics carry a
child's display name across that state machine's many phases:

```c
// filesystem.c:1062-1064
static char op_scene_display_name[STORAGE_SCENE_DISPLAY_NAME_LEN + 1u];
static char op_scene_child_display_name[STORAGE_KIT_DISPLAY_NAME_LEN + 1u];
```

- `op_scene_display_name` — the Scene's *own* folder name (e.g. `KitWool`
  parsed out of `08 KitWool`).
- `op_scene_child_display_name` — the name of the **embedded Kit**
  subfolder inside that Scene (`Kit KitWool` → `KitWool`). Written only once,
  at `filesystem.c:8976`, inside the child-content scan (phase 8/9).

The doc comment at `filesystem.c:10005-10008` states the separation is
deliberate: *"Scene identity remains outside the copied payload because
`sceneset.scg` never stores its own name... `op_scene_display_name` was
captured from the selected root/Bank directory, while
`op_scene_child_display_name` was copied into the staged embedded Kit."*
Two variables, specifically so the Kit-parsing work can't clobber the
Scene's own name. That's exactly the asymmetry we're seeing (Kit correct,
Scene wrong) — which makes `op_scene_display_name` the prime suspect, not a
wholesale mix-up of the child.

### The relevant phases, for Bank-local child 08 specifically

1. **Phase 27** (`filesystem.c:10921`): resets `op_scene_display_name[0] = '\0'`
   before rescanning the Bank parent for exactly this one child slot.
2. **Phase 29** (`filesystem.c:10959`): scans Bank-parent children, accepts
   only a directory whose parsed slot number equals `op_bank_child_cursor`,
   and copies its display text into `op_scene_display_name`. If nothing
   matches, phase 29/31 reports `FS_STATUS_ERROR` for the *whole* Bank Load
   — so this rescan cannot silently leave `op_scene_display_name` empty or
   wrong for one child while everything else still succeeds. Since the
   overall Load succeeded and the correct folder (`08 KitWool`) was
   demonstrably opened (all of that child's Kit/Instrument data came out
   correct), **`op_scene_display_name` was genuinely `KitWool` at this
   point.**
3. **Phase 31** (`filesystem.c:11017`): uses that validated name to build
   `op_root_open_name` and open the child directory.
4. **Phases 33–60**: settings, embedded Kit (via
   `op_scene_child_display_name`, not the variable in question), six
   Instrument files, `pattern.pat`, `effects.fx` placeholder all parse and
   commit. This is a wide window — dozens of async phases/ticks — during
   which nothing in the code I read is *supposed* to touch
   `op_scene_display_name` again.
5. **Phase 61** (`filesystem.c:9992`, specifically the call at
   `filesystem.c:10038`): `filesystem_cacheCurrentBankSceneNameBlock(op_bank_child_cursor)`
   finally reads `op_scene_display_name` and writes it into HCNAMES row
   `1 + op_bank_child_cursor` (row 9 for child 8). **This is the sole
   consumer that produced the corrupted row.**

So the variable is proven correct at phase 31 and wrong by phase 61, with a
long, multi-phase gap in between during which — per the code's own comments
— nothing should have touched it.

## 3. What I could and couldn't confirm

**Confirmed:**
- The two-variable Scene/Kit-name separation exists and explains why only
  the Scene row (not the Kit row) was affected.
- `op_scene_display_name` is correct going into the gap (phase 31) and wrong
  coming out of it (phase 61).
- Nothing in `filesystem.c` can be POSTing a *new* top-level operation
  (which is where most of the other 25+ writers of this same shared
  variable live — Kit Load, Kit Save, Scene Load, root HCNAMES update
  requests, etc., all at `filesystem_request*()` entry points) during that
  gap: `filesystem_start()` requires an idle facade, and the facade is
  occupied by this same Bank Load (`current_op == FS_INTERNAL_OP_LOAD_BANK`)
  for the operation's entire duration. That rules out "an unrelated
  top-level request snuck in and reused the shared variable" as the
  mechanism — the corruption has to come from something that runs *while*
  the Bank Load itself is still `current_op`, or from something that
  isn't gated by `current_op` at all.

**Not confirmed — ran out of productive static-reading budget before
finding it:**
- The exact statement, among phases 33–60's settings/Kit/6×Instrument/
  Pattern/Effects sub-steps, that overwrites `op_scene_display_name`. I did
  not find one on inspection, but I did not exhaustively read every line of
  every intervening phase either — this file is large and those phases
  cover a lot of ground.
- Whether this requires playback running at all, or whether it's a general,
  rarer interactive-Bank-Load defect that's simply never been exercised
  before (see `S056_BANK_TESTS.md`: every trace-confirmed Bank Load in this
  project's history has been boot/power-cycle-triggered, never interactive —
  so this exact pathway may just never have been checked this closely
  until now, independent of the playback variable).
- Whether the mechanism is a genuine software write (something reusing
  `op_scene_display_name` as scratch for an unrelated purpose inside the
  gap) or a hardware-adjacent timing issue specific to this platform's
  bit-bang SD SPI (`Core/Hardware/SD/SPI/spi_sd.c`) being timing-sensitive
  to a concurrently-running high-priority audio ISR — plausible given
  playback was the test's one unusual variable, but unproven; I found no
  code-level evidence either way.

## 4. Proposed fix (structural — doesn't require finding the exact culprit)

Don't make phase 61 trust a shared global that's had dozens of ticks and
unrelated sub-phases to potentially drift since it was last validated.
Snapshot the name into Bank-Load-owned, per-child storage the moment it's
validated (end of phase 29/31), and have
`filesystem_cacheCurrentBankSceneNameBlock()` read the snapshot instead of
the shared scratch variable:

- Add one `char[STORAGE_SCENE_DISPLAY_NAME_LEN + 1]` field to the Bank
  Load's own operation-scoped state (alongside `op_bank_child_cursor`,
  `op_bank_display_name`, etc. — this operation already carries similar
  small per-load fields, so this isn't a new class of state).
- Copy `op_scene_display_name` into it right after phase 29/31 validates
  the child (the exact point where we've proven the value is correct).
- Change `filesystem_cacheCurrentBankSceneNameBlock()`'s single call site
  (`filesystem.c:10038`) to pass that snapshot instead of relying on
  `op_scene_display_name` still being intact several phases later.

This closes the whole *class* of risk — "some later sub-phase reuses the
shared scratch and phase 61 reads the stale/wrong result" — regardless of
which specific sub-phase turns out to be the culprit, and regardless of
whether the trigger is software reuse or a hardware timing effect that
corrupts one read. It costs a handful of bytes of already-in-scope
operation state and one extra `memcpy`, no new allocation class, and no
change to any other caller of `op_scene_display_name` (root Scene Load
still uses the shared variable exactly as before, since it doesn't have
Bank Load's multi-phase-per-child structure — only the Bank Load path gets
the snapshot).

Root Scene Load's own use of `op_scene_display_name` (`filesystem.c:4829-4841`)
looks structurally safer than Bank Load's, since it publishes the mask right
after the request, closer to when the variable was set — but it's not
proven immune either. If a Kit/Instrument-heavy root Scene Load turns out to
have the same multi-phase gap, the same snapshot treatment should be
considered there too; out of scope for this pass since it wasn't what this
test exercised.

## 5. Proposed diagnostic hook (for next time, whether or not the fix above lands first)

Two options, cheapest first:

**A. Bracket-and-compare (minimal, catches drift without touching every
write site).** At phase 31 (right after validation), snapshot
`op_scene_display_name` into a small local/static shadow. At phase 61
(right before `filesystem_cacheCurrentBankSceneNameBlock()` reads it),
compare the live value against the shadow and emit one new trace record
only on mismatch:

```c
if (memcmp(op_scene_display_name, op_bank_child_name_snapshot,
           sizeof(op_scene_display_name)) != 0) {
    autosaveTrace_record(AUTOSAVE_TRACE_STAGE_NAME_SCRATCH_DRIFT, /* new, e.g. 'H' */
        op_bank_child_cursor,
        /* value32: first 4 bytes of each name, packed, or a cheap hash of both */
        ...);
}
```

This is cheap (one comparison per child, one record only when something is
actually wrong) and — combined with the structural fix in §4 using the
snapshot as the authoritative source — doubles as the fix's own regression
detector: if this record ever fires again after the fix lands, the snapshot
itself is being read from the wrong place.

**B. Full write-site instrumentation (expensive, only worth it if A doesn't
reproduce the drift on the next attempt).** Wrap every write to
`op_scene_display_name` in `filesystem.c` (~10 sites) with a trace record
tagging a small call-site enum plus `op_bank_child_cursor`. This will name
the exact clobbering statement directly, at the cost of a lot of trace
volume during ordinary operation — this is exactly the kind of
high-volume-diagnostic-only instrumentation `DRAFT_TRACE_SPLIT_BY_MODULE.md`
argues should live behind its own module toggle rather than the shared
firehose, so land that first if going this route.

Recommend shipping **A** alongside the §4 structural fix (cheap, permanent,
doubles as a regression check) and only reaching for **B** if the drift
somehow reproduces *after* the snapshot fix ships (which would mean the
snapshot itself is being written from a bad source, not that phase 61 is
reading stale data — a different bug).

## 6. Retest recommendation

Two cheap variants worth running before writing more code, to narrow §3's
open questions:

1. Repeat the interactive Bank Load with playback **stopped**. If row 9 (or
   any row) still corrupts, the playback variable is incidental and this is
   a general interactive-Bank-Load defect — raises priority, since it isn't
   gated on an unusual condition.
2. Repeat with playback **running**, `DEV_MODE_LOGGING` on, and (once
   available) the §5-A drift check in place. If it fires, it will identify
   which child slot drifted and confirm the mechanism directly instead of
   needing another round of forensic reconstruction from `.hcnames` alone.

Either result is useful: a clean run with playback stopped strengthens the
playback-timing hypothesis; a repeat corruption with playback stopped means
the fix in §4 is needed regardless of playback state, which is good news for
prioritization (it means it's reproducible on demand rather than a rare
timing-dependent hazard).

---

# Implementation Plan

Expanded from §4/§5 above into an exact, file-by-file change list.
Implementation is now in progress. The source trace confirmed the proposed
9-byte Bank-child snapshot is the smallest operation-local boundary that
removes the phase-31-to-phase-61 shared-scratch dependency; the implementation
log at the end of this document records landed changes and verification.

This covers both pieces from §4/§5 together, since they touch the same
handful of lines and are cheapest to land as one pass: the structural
snapshot fix (closes the risk regardless of the exact cause) and diagnostic
hook option A, the bracket-and-compare drift detector (proves the mechanism
directly if it ever fires again). Diagnostic option B (full write-site
instrumentation) is intentionally **not** included here — per §5's own
recommendation, it's only worth its trace volume if A fails to reproduce the
drift after the structural fix ships, which can't be known yet.

## Change list

| # | File | Function / location | Kind |
|---|---|---|---|
| 1 | `Core/Hardware/SD/filesystem.c` | Bank load/save scratch block (~line 1082) | add 1 new static |
| 2 | `Core/Hardware/SD/filesystem.c` | `filesystem_loadSceneDirectory_tick()` phase 27 | add 1 reset line |
| 3 | `Core/Hardware/SD/filesystem.c` | `filesystem_loadSceneDirectory_tick()` phase 31 | add snapshot copy |
| 4 | `Core/Hardware/SD/filesystem.c` | `filesystem_cacheCurrentBankSceneNameBlock()` | change read source + add drift check |
| 5 | `Core/Bank/Scene/AutosaveTrace.h` | `autosave_trace_stage_t` enum | add stage `'H'` |
| 6 | `Core/Bank/Scene/AutosaveTrace.h` | flag-constant block | add value-layout comment (no bit flags needed) |
| 7 | `knowledge_files/specification_reference/DEV_MODES.md` | `/asavetrc.bin` stage list | document `'H'` |

Suggested implementation order: 5 → 1 → 2 → 3 → 4 → 6 (comment-only, can
happen alongside 5) → 7. Nothing here depends on
`S056_BANK_SETTINGS_CORRECTION.md`'s changes or vice versa; the two plans
touch different functions in the same file and can land independently, in
either order.

---

### Change 1 — `Core/Hardware/SD/filesystem.c`: new snapshot field

Insert into the existing "Bank load/save scratch" block, immediately after
`op_bank_loaded_scene` (`filesystem.c:1090`):

```c
static uint8_t op_bank_loaded_scene = 0u;
/*
 * Bank-Load-owned copy of this child's validated Scene display name,
 * authoritative from the moment phase 31 captures it through phase 61's
 * HCNAMES publish.
 *
 * Inputs: copied from op_scene_display_name at the end of phase 31 (the
 * exact point that name is proven correct -- see the phase 29/31 comments
 * immediately above), once per Bank-local child, overwriting whatever this
 * field held for the previous child. Output: filesystem_cacheCurrentBankSceneNameBlock()
 * reads this instead of op_scene_display_name when publishing the child's
 * Scene-name HCNAMES row. Why this exists instead of trusting
 * op_scene_display_name directly at phase 61: op_scene_display_name is a
 * single module-wide scratch variable shared by root Scene Load/Save,
 * Kit-related paths, and this Bank child loop (~25 write sites across
 * filesystem.c); phases 33-60 (settings, embedded Kit, six Instrument
 * files, Pattern, Effects) run between phase 31's validation and phase 61's
 * read with no proof nothing in that window reuses the shared variable.
 * Session 056 found HCNAMES row 9 (a Bank-local Scene name) corrupted with
 * an unrelated root-library name after exactly this gap; this field removes
 * the dependency on that gap staying empty, regardless of which future
 * change (or existing one not yet found) might otherwise write through
 * op_scene_display_name during it. See S056_NAMES_CORRUPTION.md. Affiliates:
 * phase 27 (reset), phase 31 (write), filesystem_cacheCurrentBankSceneNameBlock()
 * (sole read site).
 */
static char op_bank_child_scene_display_name[STORAGE_SCENE_DISPLAY_NAME_LEN + 1u];
```

**Why this size/type**: matches `op_scene_display_name`'s own declaration
(`STORAGE_SCENE_DISPLAY_NAME_LEN + 1u`, `filesystem.c:1062`) exactly, since
it holds a copy of the same content.
**Inputs**: none yet (declaration only).
**Output**: one new zero-initialized (BSS) byte array; no RAM-approval
concern beyond what's already been reviewed for this file's existing
per-operation scratch — this is the same class of allocation as the ~15
other `op_bank_*`/`op_scene_*` fields already in this block, not a new
category, and it replaces no existing field (net addition of
`STORAGE_SCENE_DISPLAY_NAME_LEN + 1` bytes, currently 9).
**Affiliates**: Change 2 (reset), Change 3 (write), Change 4 (read).

---

### Change 2 — `Core/Hardware/SD/filesystem.c`: reset at phase 27

Current code (`filesystem.c:10921-10942`):

```c
    case 27:
        if (!op_close_done)
            return;
        op_kit_slot_dir = NULL;
        /*
         * Rescan the current selected Bank parent for just one child slot.
         * [... existing comment ...]
         */
        op_scene_display_name[0] = '\0';
        filesystem_bootLoggingSetBankSceneDetail('O');
        op_file_ready = false;
        op_file = NULL;
        if (!afatfs_fopen(".", "r", on_file_opened))
            return;
        op_phase = 28u;
        return;
```

Change the reset line to:

```c
        /*
         * Rescan the current selected Bank parent for just one child slot.
         * [... existing comment, unchanged ...]
         */
        op_scene_display_name[0] = '\0';
        /*
         * Also clear the Bank-Load-owned snapshot (Session 056) so a phase
         * 31 that is somehow reached without this rescan actually finding
         * and validating a match fails loudly -- filesystem_cacheCurrentBankSceneNameBlock()
         * would then publish an empty Scene-name row instead of silently
         * reusing the previous child's still-cached name. This is a
         * defensive belt-and-suspenders reset: phase 29/31's existing
         * FS_STATUS_ERROR path on no-match already stops the whole Bank
         * Load before this snapshot would ever be read for a child that
         * failed to resolve, so this line should never actually change
         * observed behavior -- it exists to keep that guarantee explicit
         * and local to this field's own lifecycle rather than only implied
         * by phase 29/31's control flow. Affiliate: op_bank_child_scene_display_name.
         */
        op_bank_child_scene_display_name[0] = '\0';
        filesystem_bootLoggingSetBankSceneDetail('O');
        op_file_ready = false;
        op_file = NULL;
        if (!afatfs_fopen(".", "r", on_file_opened))
            return;
        op_phase = 28u;
        return;
```

**Inputs/Output**: no functional change to this phase's control flow — the
new field starts every child rescan empty, same as `op_scene_display_name`
already does.
**Affiliates**: Change 1 (declaration), Change 3 (the write this reset
protects against reusing stale data before).

---

### Change 3 — `Core/Hardware/SD/filesystem.c`: snapshot at phase 31

Current code (`filesystem.c:11017-11028`):

```c
    case 31:
        if (!op_close_done)
            return;
        op_kit_slot_dir = NULL;
        if (op_close_status != FS_STATUS_DONE) {
            filesystem_setPresetNameInvalid();
            if (!afatfs_chdir(NULL))
                return;
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_scene_load_scene_mask = (uint16_t)(1u << op_bank_child_cursor);
        filesystem_initSceneStage(&fs_stage_workspace.scene_stage);
```

Insert the snapshot copy immediately after the error-return block, before
`op_scene_load_scene_mask = ...`:

```c
    case 31:
        if (!op_close_done)
            return;
        op_kit_slot_dir = NULL;
        if (op_close_status != FS_STATUS_DONE) {
            filesystem_setPresetNameInvalid();
            if (!afatfs_chdir(NULL))
                return;
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        /*
         * Freeze this child's validated Scene name into Bank-Load-owned
         * storage the moment it's proven correct (Session 056).
         *
         * Inputs: op_scene_display_name, having just survived phase 29's
         * scan-and-match-op_bank_child_cursor loop with op_close_status ==
         * FS_STATUS_DONE -- the only way to reach this line is a confirmed,
         * unambiguous match for this exact child slot. Output:
         * op_bank_child_scene_display_name holds the one value
         * filesystem_cacheCurrentBankSceneNameBlock() will later trust for
         * this child's Scene-name HCNAMES row (Change 4), independent of
         * whatever op_scene_display_name is reused for by any of the many
         * phases (33-60: settings, embedded Kit, six Instrument files,
         * Pattern, Effects) between here and there. Why a full copy instead
         * of a pointer/index: op_scene_display_name is reused in place by
         * design (it's the same nine-byte scratch buffer across every
         * consumer in this file), so anything short of copying the bytes
         * out now would still be vulnerable to exactly the drift this
         * change exists to prevent.
         */
        memcpy(op_bank_child_scene_display_name, op_scene_display_name,
               sizeof(op_bank_child_scene_display_name));
        op_scene_load_scene_mask = (uint16_t)(1u << op_bank_child_cursor);
        filesystem_initSceneStage(&fs_stage_workspace.scene_stage);
```

**Inputs**: `op_scene_display_name` (validated by phase 29's match loop,
confirmed via `op_close_status == FS_STATUS_DONE`).
**Output**: `op_bank_child_scene_display_name` now holds an independent copy
that phases 33-60 cannot affect, however they use the shared scratch
variable.
**Affiliates**: Change 1 (declaration), Change 2 (the reset this pairs
with), Change 4 (the sole reader).

---

### Change 4 — `Core/Hardware/SD/filesystem.c`: `filesystem_cacheCurrentBankSceneNameBlock()`

Current code (`filesystem.c:4844-4879`):

```c
static void filesystem_cacheCurrentBankSceneNameBlock(uint8_t scene_index)
{
    uint8_t slot;

    /* [existing doc comment, ~4848-4860] */
    if (scene_index >= STORAGE_BANK_SCENE_MAX_SLOTS)
        return;
    filesystem_cacheResidentName(filesystem_residentSceneRow(scene_index),
                                 op_scene_display_name);
    (void)filesystem_setResidentSource(filesystem_residentSceneRow(scene_index),
                                       FS_RESIDENT_SOURCE_INHERIT);
    filesystem_cacheResidentName(filesystem_residentKitRow(scene_index),
                                 filesystem_identityName(FS_IDENTITY_KIT_ROW));
    (void)filesystem_setResidentSource(filesystem_residentKitRow(scene_index),
                                       FS_RESIDENT_SOURCE_INHERIT);
    for (slot = 0u; slot < STORAGE_KIT_SLOT_COUNT; slot++) {
        uint16_t row = filesystem_residentInstrumentRow(scene_index, slot);
        filesystem_cacheResidentName(
            row,
            filesystem_identityName((uint8_t)(
                FS_IDENTITY_INSTRUMENT_ROW_0 + slot)));
        (void)filesystem_setResidentSource(row, FS_RESIDENT_SOURCE_INHERIT);
    }
}
```

Replace with:

```c
static void filesystem_cacheCurrentBankSceneNameBlock(uint8_t scene_index)
{
    uint8_t slot;

    /*
     * Overlay one successfully committed Bank child onto the HCNAMES register.
     *
     * Inputs: the Bank loader's one-bit child cursor, the child's Scene
     * display name in the Bank-Load-owned op_bank_child_scene_display_name
     * snapshot (Session 056 -- previously op_scene_display_name directly;
     * see S056_NAMES_CORRUPTION.md for why that was unsafe by this point in
     * the child's load), and the resident Scene just atomically committed
     * by the shared Scene loader. Output: only that Scene row, its Kit row,
     * and its six Instrument rows change as name/source pairs in the
     * borrowed cache. The child hierarchy inherits the Bank source; unmasked
     * resident Scenes are deliberately never touched, preserving both their
     * payload/name/source pairing during every mask-selective Bank Load.
     * Affiliates: filesystem_loadSceneDirectory_tick() phase 31 (snapshot
     * write) and phase 61 (this function's sole caller), and the final Bank
     * HCNAMES writer.
     */
    if (scene_index >= STORAGE_BANK_SCENE_MAX_SLOTS)
        return;

    /*
     * Diagnostic-only regression detector (Session 056): prove whether the
     * shared op_scene_display_name scratch drifted away from this child's
     * frozen snapshot by the time this function runs. This can now only
     * ever affect the trace, never the published HCNAMES row -- the write
     * below always uses the snapshot. flags is reserved (always 0); value32
     * packs scene_index in bits 0..7, the snapshot's first display byte in
     * bits 8..15, and the live (possibly drifted) value's first display
     * byte in bits 16..23, enough to identify which child and roughly what
     * changed without a second record. See AutosaveTrace.h's 'H' stage
     * comment for the full field layout and rationale.
     */
    if (memcmp(op_scene_display_name, op_bank_child_scene_display_name,
               sizeof(op_scene_display_name)) != 0) {
        autosaveTrace_record(AUTOSAVE_TRACE_STAGE_NAME_SCRATCH_DRIFT, 0u,
            (uint32_t)scene_index |
            ((uint32_t)(uint8_t)op_bank_child_scene_display_name[0] << 8) |
            ((uint32_t)(uint8_t)op_scene_display_name[0] << 16));
    }

    filesystem_cacheResidentName(filesystem_residentSceneRow(scene_index),
                                 op_bank_child_scene_display_name);
    (void)filesystem_setResidentSource(filesystem_residentSceneRow(scene_index),
                                       FS_RESIDENT_SOURCE_INHERIT);
    filesystem_cacheResidentName(filesystem_residentKitRow(scene_index),
                                 filesystem_identityName(FS_IDENTITY_KIT_ROW));
    (void)filesystem_setResidentSource(filesystem_residentKitRow(scene_index),
                                       FS_RESIDENT_SOURCE_INHERIT);
    for (slot = 0u; slot < STORAGE_KIT_SLOT_COUNT; slot++) {
        uint16_t row = filesystem_residentInstrumentRow(scene_index, slot);
        filesystem_cacheResidentName(
            row,
            filesystem_identityName((uint8_t)(
                FS_IDENTITY_INSTRUMENT_ROW_0 + slot)));
        (void)filesystem_setResidentSource(row, FS_RESIDENT_SOURCE_INHERIT);
    }
}
```

**Only two lines actually change behavior**: the `filesystem_cacheResidentName(filesystem_residentSceneRow(scene_index), ...)`
call now passes `op_bank_child_scene_display_name` instead of
`op_scene_display_name` (the fix), and the new `memcmp()`/`autosaveTrace_record()`
block above it (the diagnostic, side-effect-free unless `DEV_MODE_LOGGING`
is on and a drift is actually detected). The Kit-row and Instrument-row
writes are untouched — they already used `filesystem_identityName(...)`,
never `op_scene_display_name`, so they were never part of this defect.
**Inputs**: `op_scene_display_name` (read-only now, for the drift check
only), `op_bank_child_scene_display_name` (Change 3's snapshot, now the
sole source for the HCNAMES Scene-name row).
**Output**: HCNAMES row `1 + scene_index` is now written from data that
cannot have drifted since phase 31, regardless of what happens in phases
33-60. `asavetrc.bin` gains one `'H'` record per Bank-local child *only*
when a drift is actually caught (zero cost in the normal case beyond one
`memcmp()` per child, ~9 bytes).
**Affiliates**: `filesystem_loadSceneDirectory_tick()` phase 61
(`filesystem.c:10038`, sole caller — no change needed there, the fix is
entirely inside this function and its two feeder phases).

---

### Change 5 — `Core/Bank/Scene/AutosaveTrace.h`: new stage `'H'`

Insert into the `autosave_trace_stage_t` enum. Placement suggestion: right
after `AUTOSAVE_TRACE_STAGE_LOAD_MARK = 'L',` (`AutosaveTrace.h:62`), since
`'H'` is — like `'L'` — a Bank/Scene-child-load-scoped diagnostic rather than
a Bank-container-level one like `'B'`/`'K'`:

```c
    /*
     * H: diagnostic-only regression detector for the Session 056
     * op_scene_display_name drift defect (see S056_NAMES_CORRUPTION.md).
     * filesystem_cacheCurrentBankSceneNameBlock() compares the live shared
     * op_scene_display_name scratch against the Bank-Load-owned snapshot
     * frozen at phase 31 (op_bank_child_scene_display_name) immediately
     * before publishing a child's Scene-name HCNAMES row, and fires this
     * record only when they differ -- i.e. only when something already went
     * wrong. Since the actual HCNAMES write always uses the snapshot (never
     * the live, possibly-drifted value), this stage can never indicate a
     * corrupted card; it only proves the shared-scratch hazard the snapshot
     * was added to neutralize is still being triggered by something. flags
     * is reserved, always 0. value32 packs the Bank-local child slot
     * (0..15) in bits 0..7, the snapshot's first display-name byte in bits
     * 8..15, and the live (drifted) value's first display-name byte in bits
     * 16..23 -- enough to identify which child and roughly what the two
     * values were without a second record or a new string-carrying record
     * shape. If this stage appears in a trace captured after the snapshot
     * fix has shipped, the snapshot is being populated from a bad value at
     * phase 31 -- a different bug than the one this was added to catch, and
     * worth escalating to the full write-site instrumentation
     * (S056_NAMES_CORRUPTION.md §5 option B) at that point.
     */
    AUTOSAVE_TRACE_STAGE_NAME_SCRATCH_DRIFT = 'H',
```

**Inputs/Output**: pure type addition, no behavior until Change 4 calls it.
**Affiliates**: `filesystem_cacheCurrentBankSceneNameBlock()` (Change 4,
sole producer).

---

### Change 6 — `Core/Bank/Scene/AutosaveTrace.h`: no separate flag `#define`s needed

Unlike `'K'` (`S056_BANK_SETTINGS_CORRECTION.md`'s Change 2), `'H'` has no
flag bits (`flags` is always `0`, reserved for future use) — its entire
payload is the `value32` bit-packing documented inline in Change 5's enum
comment above. No `#define AUTOSAVE_TRACE_...` constants are needed; the bit
shifts (`<< 8`, `<< 16`) are simple enough to leave as literals at the one
call site (Change 4) rather than naming them, matching how a few other
single-producer stages in this header (e.g. the raw shift values inside `O`'s
three Menu branch-tag reuse, per its own doc comment) already do. If a
second producer for `'H'` is ever added, revisit and promote these to named
shifts at that point.

---

### Change 7 — `knowledge_files/specification_reference/DEV_MODES.md`: document `'H'`

Insert immediately after the `L` paragraph (`DEV_MODES.md:273-275`):

```markdown
`H` is a diagnostic-only regression detector added Session 056 for a found
(and, as of that session, fixed) defect: `filesystem_cacheCurrentBankSceneNameBlock()`
now publishes a Bank-local child's Scene-name HCNAMES row from a per-child
snapshot frozen at phase 31, instead of trusting the shared
`op_scene_display_name` scratch variable that many other phases across
`filesystem.c` also reuse. `H` fires only when a live/snapshot mismatch is
still detected despite that fix — it can never indicate a bad card, only
that the underlying shared-scratch hazard is still being triggered by
something not yet found. flags is reserved (always 0). value32 packs the
Bank-local child slot (bits 0..7), the snapshot's first name byte (bits
8..15), and the live/drifted value's first name byte (bits 16..23). See
`S056_NAMES_CORRUPTION.md` for the full investigation.
```

Also update `tools/decode_devlogs.py` and `tools/devlog_unpack.py`'s stage
lookup tables for `'H'`, same as noted for `'K'` in
`S056_BANK_SETTINGS_CORRECTION.md`'s Change 11 — confirm the shared table
location once at implementation time and add both new stages there
together.

---

## Testing / verification checklist (for whoever implements this)

- Re-run this session's exact reproduction (interactive Bank Load with
  playback running) after Change 4 ships and confirm HCNAMES row 9 (or
  whichever row was affected) is now correct via `tools/verify_bank_autosave.py`.
- Leave `DEV_MODE_LOGGING` on for that retest and confirm whether `'H'`
  fires. If it does, `value32`'s packed first-bytes plus the child slot
  should point at the same corruption already seen this session — that
  would be strong confirmation the fix's snapshot mechanism is working
  correctly (drift still detected, but no longer able to reach the
  published row). If it does *not* fire but the corruption still
  reproduces, the corruption is not coming through `op_scene_display_name`
  at all and this entire investigation's premise needs revisiting.
- Run the playback-stopped variant recommended in §6 above at least once
  with this fix in place, to gather the still-open "is playback required"
  data point regardless of whether the fix resolves the symptom.
- Confirm `STORAGE_BANK_SCENE_MAX_SLOTS` is exactly 16 (assumed for the
  `value32` bit-packing's 0..7 slot field in Change 4/5 comfortably fitting
  a 0..15 range) — read from existing code, not re-verified independently
  for this plan.

---

## Implementation log

### 2026-08-22 — source implementation landed

- Added `op_bank_child_scene_display_name[9]`, a filesystem-owned normal-SRAM1
  operation scratch copy. It is cleared at Bank-child phase 27 and populated
  immediately after the successful phase-31 child-name validation.
- Bank-child HCNAMES publication now always uses the frozen snapshot rather
  than the shared `op_scene_display_name` scratch. This closes the observed
  Scene-row corruption without changing the existing Kit or Instrument row
  paths.
- Added trace stage `H` as the planned bracket-and-compare detector. It uses
  reserved flags (`0`) and packs child slot, snapshot first byte, and live
  scratch first byte into `value32`; it fires only on a mismatch, while the
  snapshot remains authoritative for the actual HCNAMES write.
- Added `H` documentation and both decoder views. `STORAGE_BANK_SCENE_MAX_SLOTS`
  is confirmed as 16 and `STORAGE_SCENE_DISPLAY_NAME_LEN` as 8, so the packed
  fields and 9-byte snapshot match the existing contracts.
- Verification status: `git diff --check` passed; `make clean && make` passed
  with the linked image reporting `text=381644`, `data=400`, `bss=94760`; the
  `make img` packaging check passed; and synthetic `H` records decoded
  correctly through both decoder entry points. The interactive
  playback-running and playback-stopped hardware retests remain pending.
