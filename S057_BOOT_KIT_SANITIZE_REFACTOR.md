# Boot Kit-Directory Sanitizer Refactor — Planning Doc (Rev 2)

Status: **planning — §4a decided (option 2, generalized), fully detailed
implementation plan below, no code changed yet.**
Companion to `S057_AUTOSAVE_WRITER_WRAP.md` §8 (diagnosis) and the deferred
refactor target already scoped in `SCOPING_TARGETS.md:211-246` (Session
052). This is the last remaining code-change item before the AutoSave boot
reader (`AUTOSAVE_READ_PLAN.md`) — everything else on the wrap punch list is
now resolved or reduced to testing.

**Rev 2 changes:** §4a is decided — lazy quarantine-on-failed-load (option
2), generalized to Kit/Scene/Bank Load uniformly, with an explicit
Kit-invalidation-cascades-to-owning-Scene rule and a Bank-specific
partial-failure contract (§4a-iv). §5 is rewritten as a fully detailed,
file/function/phase-level implementation plan. §6 gains cascade-specific
test cases. A new §4f records the judgment calls made this session that are
worth a quick confirm.

---

## 1. The problem, confirmed

`filesystem_quarantineKitLibraryBlocking()` (`filesystem.c:17920-`, called
from `filesystem_createLibraryIndexBlocking(FS_LIBRARY_INDEX_KIT)` before
every boot's Scene/Bank scan) does **full content validation** of every
present root Kit, not just a name/reconstructability check:

- `filesystem_validateCurrentKitBlocking()` opens `kitset.kcg` and streams
  it **one byte at a time**, then for each of the six
  `STORAGE_KIT_SLOT_COUNT` member slots, opens and closes the referenced
  Instrument file individually — a full blocking LFN directory lookup each.
- That's **1 + 6 = 7 blocking file operations per Kit**, all under **one
  shared `KITQUAR` 10-second deadline for the entire root Kit traversal**
  (armed once per boot, not once per Kit).
- On the current 44-Kit card that's **308 blocking operations sharing one
  10-second budget**. This scales linearly with total Kit count. The code
  correctly distinguishes timeout from invalid-content and never quarantines
  good data on a timeout, but it still fails the *entire boot*
  (`goto boot_filesystem_timeout` in `main.c`) for a card that simply has
  more Kits or a slower card — a live risk of a false boot failure as the
  library grows.

## 2. Why this is safe to remove — the precedent already exists in this codebase

This is the load-bearing finding for the whole refactor: **Scene and Bank
boot indexing already do the lighter thing this refactor wants for Kit.**
Confirmed by reading `filesystem_createLibraryIndexBlocking()`
(`filesystem.c:21531-`): for `FS_LIBRARY_INDEX_SCENE` and
`FS_LIBRARY_INDEX_BANK`, boot does exactly two things before writing
`.hcindex` — `filesystem_repairLibraryNamesBlocking(kind)` (canonicalize
every numbered folder to `NNN Name`) and a plain directory scan
(`filesystem_requestScanScenes()` / `filesystem_requestScanBanks()`). No
`sceneset.scg`/`bankset.bcg` is opened, no content is parsed, no child file
existence is checked. Kit is the only one of the three that additionally
calls `filesystem_quarantineKitLibraryBlocking()` after the same repair
step. This refactor's core is not inventing a new lighter path — **it's
deleting Kit's extra step so Kit matches the pattern Scene and Bank already
use successfully.**

`filesystem_repairLibraryNamesBlocking(FS_LIBRARY_INDEX_KIT)` already runs
unconditionally before the quarantine call (`filesystem.c:21573`, same call
site for all three kinds) — so canonicalization (SCOPING_TARGETS.md's item
2) is **already implemented and already runs for Kit today.** It is not new
work this refactor needs to add.

## 3. Why this is safe to remove — load-time validation already exists independently

The other load-bearing finding: **every context that actually loads Kit
content already re-parses `kitset.kcg` and already fails gracefully.** This
was not built for this refactor — it already exists, independently, because
loading a Kit requires opening its member files regardless of what boot
did. Confirmed by reading all three load contexts:

- **Root Kit Load** — `filesystem_loadKitDirectory_tick()` case 11-15
  (`filesystem.c:8594-8676`): opens `kitset.kcg`, parses it line-by-line via
  `storage_kitsetParseLine()`, and on any parse failure or read error sets
  `FS_STATUS_ERROR`, calls `filesystem_setPresetNameInvalid()`, and routes
  through the close-then-fail phase (never leaves a dangling handle).
- **Root Scene Load's embedded Kit** — `filesystem_loadSceneDirectory_tick()`
  case 22-25 (`filesystem.c:9210-9260`): same shape, same
  `storage_kitsetParseLine()` call, same `FS_STATUS_ERROR` path.
- **Bank child Scene's embedded Kit** — the same code path as above; the
  function branches internally on `current_op == FS_INTERNAL_OP_LOAD_BANK`
  for Bank-specific member-provenance validation, but the parse/fail
  mechanics are identical.

So the "move validation to the load attempt" half of the deferred refactor
target is **already done, in production, today** — it was never gated on
boot quarantine existing. What boot quarantine currently adds on top is
*only* a pre-emptive strike so a broken Kit never gets far enough to be
selected. Removing it does not remove any correctness guarantee at load
time; it only changes *when* a broken Kit's brokenness becomes visible —
from "hidden before the user ever sees it" to "surfaces as a failed Load
attempt." §4a below (now decided) closes that gap back up: the failed Load
attempt itself performs the same hiding, lazily, at the one folder that was
actually proven bad.

## 4. Architectural questions to resolve before implementation

### 4a. Discoverability contract for a broken Kit — DECIDED: option 2, generalized

Today: a malformed Kit is renamed to `errNNN ...` at boot, so it never
appears as a selectable row in `.hcindex`/the menu. After this refactor: a
malformed Kit's *name* is still fine (repair/canonicalization doesn't touch
content), so it publishes a normal-looking row with its display name. The
user can select it. The Load then fails via the existing graceful path
(§3) — `FS_STATUS_ERROR`, Menu presumably shows some failure indication.

**Decision: option 2, lazy quarantine-on-failed-load.** When a Load attempt
fails due to invalid content (not an I/O abort), rename the offending
folder to `err...` at that point, exactly as `filesystem_makeQuarantineName()`
already does at boot, so it stops appearing in any future `.hcindex`
rebuild/scan. This section records the decision and its generalization;
§5 below is the fully detailed implementation.

#### 4a-i. Confirmed: the error screen already exists

First open item from this session — checked, and it's already true. Every
failed Kit/Scene/Bank Load already surfaces a visible error overlay today,
independent of this refactor:

- `preset_completeFilesystemOp()` (`presetManager.c:219-247`) preserves the
  failed operation identity instead of collapsing it to `PRESET_OP_NONE`.
- Menu's `menu_pollPresetStatus()` checks `!preset_getCompletedOk()`
  (`menu.c:7960`) **before** its `switch (preset_getCompletedOp())`, and for
  any failed Kit/Scene/Bank Load calls `menu_showFilesystemErrorOverlay()`
  (`menu.c:8012`), which paints `filesystem_errorCode()` (or a generic
  `"FsErr"` fallback) into the existing test-result overlay and repaints.
  This is already "the common terminal path for essentially every failed
  Menu filesystem operation" per its own header comment
  (`menu.c:3002-3004`).

So no new UI plumbing is needed to make a failed Load visible — the
generalization below only needs to (a) make sure the right operations still
route through this existing failure path, and (b) for the one case where
Bank Load's overall result must stay `FS_STATUS_DONE` for boot-safety
reasons (§4a-iv), add one explicit call to the same existing
`menu_showFilesystemErrorOverlay()` function rather than inventing new UI.

#### 4a-ii. Generalizing to "any kit load," Scene Load, and Bank Load

The user's requirement is that this is a **general item**: any time a Kit's
content is proven invalid during a load attempt — root Kit Load, a root
Scene's embedded Kit, or a Bank-local Scene's embedded Kit — that Kit's own
folder is renamed `err...`, regardless of which caller found the problem.
This falls directly out of §3's existing finding: all three contexts already
call the same shape of validation (`storage_kitsetParseLine()` /
`storage_instrumentParseLine()` / canonical member-name check) and already
produce `FS_STATUS_ERROR` gracefully — the only new work is renaming the
proven-bad folder at that failure point instead of silently leaving it in
place.

This also generalizes past Kit: a root Scene's *own* content (`sceneset.scg`,
the bridge `.pat`, the `.fx` placeholder, or a Scene folder missing its
required `Kit `/`.pat`/`.fx` children) can independently be invalid, with no
Kit involved at all. The same lazy-quarantine mechanism applies there too —
rename the Scene folder — since `storage_parseNumberedFolder()` already
excludes `err`-prefixed names from ever matching a numbered-folder scan (its
first three characters must be digits — confirmed by reading
`storageTypes.c:1889-1942`), so this requires no new scan-exclusion logic in
any of the three namespaces (`Kit/`, `Scene/`, `Bank/<n>/`) — renaming alone
is sufficient to hide the folder from every future index rebuild.

#### 4a-iii. The invalidation cascade: Kit → owning Scene (root Scene Load only)

Per direction: when a Kit is invalidated during a **root Scene's** embedded
Kit load, the owning Scene is also invalid — a Scene whose Kit cannot be
loaded is not a usable Scene — so it is renamed `err...` too, in the same
failed Load. Concretely, for `filesystem_loadSceneDirectory_tick()` running
as `current_op == FS_INTERNAL_OP_LOAD_SCENE`:

- A **Kit-layer** failure (embedded Kit directory open, `kitset.kcg`
  open/parse/finalize, or an Instrument member open/parse/finalize) renames
  **both** the embedded `Kit <name>` folder and the owning `Scene/NNN Name`
  folder.
- A **Scene-layer** failure (missing `Kit `/`.pat`/`.fx` child, bad
  `sceneset.scg`, bad pattern, bad effect) renames **only** the owning Scene
  folder — there is no Kit to blame.

Standalone root Kit Load (`filesystem_loadKitDirectory_tick()`) has no
owning Scene to cascade to — it renames only the Kit folder itself, exactly
as boot's quarantine did.

#### 4a-iv. Bank Load is different: no Scene rename, fail only that slot, never fail the whole Bank

This is the one place a straight application of the cascade rule would be
actively harmful, and it is the most important finding of this session's
research, because it is a **boot-safety requirement**, not a style choice.

Per direction, when a Bank-local Scene's embedded Kit (or the Scene's own
content) is invalid during Bank Load:

- **Do not rename the Bank-local `SS Name` Scene folder.** Its identity is
  positional (a fixed two-digit Bank-local slot, discovered by
  `storage_parseBankSceneFolder()`), unlike a root Kit/Scene's browsable
  numbered identity — renaming it would corrupt the Bank's internal slot
  mapping, not just hide it from a browser. Leave its name untouched.
- **Still rename the embedded Kit folder** if the Kit was the layer that
  failed — the "any kit load" rule is unconditional; a bad Kit gets
  quarantined wherever it is found, including inside a Bank-local Scene.
- **Fail only that one child slot**, and load it as empty (§5.3 defines
  "empty" precisely as "excluded from `bank_scenePresentMask()`", not an
  SRAM wipe — see §4f). The rest of the Bank Load's requested mask must
  still be attempted and committed normally.
- **The kit/scene error must still visibly appear** to the user (per
  direction: "other than the kit error which should appear"), but —
  **the overall Bank Load operation must complete `FS_STATUS_DONE`, never
  `FS_STATUS_ERROR`, when the only problem was one child's invalid
  content.**

That last point is not a preference; it is required by code already read
this session. `main.c:898-901`, in the boot sequence's Bank Load poll loop:

```c
if (preset_getStatus() == PRESET_UPDATE_READY &&
    !preset_getCompletedOk()) {
    goto boot_filesystem_failure;
}
```

Boot loads the resident Bank with the **full 16-scene mask**
(`preset_loadBank(boot_bank_slot, 0xffffu)`, `main.c:813`). If Bank Load's
overall completion status became `FS_STATUS_ERROR` merely because one of
those sixteen children had a corrupted embedded Kit, this exact boot check
would take `goto boot_filesystem_failure` — **turning one bad Kit inside one
Scene inside the boot Bank into a failure to boot the whole device.** That
is precisely the class of false failure this entire refactor exists to
close (§1); reintroducing it at the Bank Load layer while removing it at the
boot-quarantine layer would be a regression, not a fix.

There is a second, independent reason `FS_STATUS_ERROR` for the whole
operation is wrong even outside boot: Menu's own `menu_pollPresetStatus()`
early-outs on `!preset_getCompletedOk()` (`menu.c:7960-8014`) **before**
reaching the `PRESET_OP_BANK_LOAD` success-path code that applies sound,
selects the active Scene/Pattern, and repaints (`menu.c:8134-8175`). Failing
the whole operation would throw away the runtime application of every
*other* Scene that loaded correctly, not just the one that failed.

**Resolution:** decouple "the operation completed successfully" from "the
user should see a warning." Bank Load keeps its existing all-selected-
children-processed, `FS_STATUS_DONE` contract for anything except a genuine
I/O abort (unchanged). A new sticky per-operation flag/mask (§5.4) records
which child(ren) failed with provably-invalid content; Preset surfaces it
through a new accessor; Menu's existing `PRESET_OP_BANK_LOAD` success branch
calls the *already-existing* `menu_showFilesystemErrorOverlay()` once,
after its normal sound-apply work, exactly when that flag is set. This
reuses the exact overlay already confirmed in §4a-i — no new UI is invented,
and no successful child's data or runtime apply is sacrificed to show it.

**Options considered:**
1. Fail the whole Bank Load (`FS_STATUS_ERROR`) whenever any child fails.
   **Rejected** — boot-fatal per `main.c:898-901` above, and throws away
   good children's runtime apply in Menu too.
2. Skip the failed child silently, no visible indication at all.
   **Rejected** — contradicts the explicit direction that "the kit error...
   should appear."
3. **Chosen:** keep `FS_STATUS_DONE` for the overall operation, decouple the
   warning through a new sticky flag/accessor, surface it via the existing
   overlay function from inside the existing success-path branch.

### 4b. Boot deadline/logging bookkeeping cleanup

`filesystem_bootLoggingArm("KITQUAR ")` / `filesystem_bootLoggingOperationDone()`
wrap the quarantine call specifically. Removing the call means removing
this arm/disarm pair too — confirm nothing else depends on a `KITQUAR`
detail label existing in the boot log format (`bootlog.bin` readers,
`DEV_MODES.md` documentation of log codes). Session 044's handoff also
notes "four pre-audio SD pacing holds remain in the source after an
intermittent boot report... not evidence of a verified root-cause fix" —
worth a quick check that none of those holds were placed *because of*
KITQUAR's timing profile (i.e., inserted to paper over a race that this
refactor's speedup might now expose differently). Likely unrelated, but
cheap to check before assuming zero interaction.

### 4c. Non-numbered stray folder policy — CONFIRMED uniform, no new decision needed

Confirmed this session by reading `storage_parseNumberedFolder()` and
`storage_parseBankSceneFolder()` (`storageTypes.c:1889-1974`): both simply
return 0 (no match) for any component that doesn't begin with the required
count of ASCII digits followed by `_`/` `. Every one of Kit/Scene/Bank's
directory scans (`filesystem_repairLibraryNamesBlocking()`,
`filesystem_createLibraryIndexBlocking()`'s scan, and every scan inside the
three load tick functions) is built on these same two parsers. A
non-numbered stray like `Bank/old012-c76c` is therefore already uniformly
ignored by all three namespaces today — it is neither renamed nor removed,
just silently skipped by every scan, identically for Kit/Scene/Bank. This
refactor inherits that existing uniform behavior for free; §8b's flagged
folder remains permanently invisible-but-present, which is pre-existing
behavior, not something this refactor changes. No action needed.

This same parser behavior is *why* the `err...` rename mechanism from §4a
requires no separate scan-exclusion code: `err017 MyKit` fails the same
digit-prefix check (`'e' < '0'`), so it is automatically skipped by the very
same scan logic that already ignores `Bank/old012-c76c`.

### 4d. Legacy `errNNN ...` folders already on cards

Any Kit previously quarantined by the current boot logic stays renamed
forever — this refactor doesn't touch existing `err`-prefixed folders and
has no reverse-quarantine step. That's consistent with existing behavior
(nothing un-quarantines today either), so likely no action needed, but
worth stating explicitly so it isn't mistaken for a regression: a
previously-quarantined-but-actually-fine Kit (the exact `KQ019KST` false-
positive-adjacent scenario from Session 052) will **not** automatically
reappear after this refactor ships. That would require a separate manual
rename or a deliberate one-time migration pass, out of scope here.

### 4e. Boot-order interaction — explicitly not this refactor's problem to solve

Per direction: leave `main.c`'s boot order as-is, or change it only as this
refactor mechanically requires (e.g., if removing ~300 blocking ops changes
which pacing hold is adjacent to what). Do not attempt to solve
`AUTOSAVE_READ_PLAN.md §3`'s separate boot-order reorder here — that gets
re-examined on its own during the reader's implementation step, since it
touches the same sequence for an unrelated reason (candidate validation
before the scan-and-load ladder) and the plan itself already flags it as
needing its own dedicated review and hardware test.

Still stands unchanged after this session's §4a decision: the lazy
quarantine-on-failed-load mechanism adds no new boot-order dependency — it
only reacts to a load failure that already exists on the existing boot
sequence, at the exact point that failure already occurs. The one boot-path
interaction actually found this session (`main.c:898-901`'s Bank Load
poll-loop failure gate) is handled directly in §4a-iv, not deferred here.

### 4f. New open items from this session — judgment calls made, worth a quick confirm

These are places this session had to make a specific design choice where
the direction given left more than one reasonable reading. Each is called
out again at its concrete implementation site in §5; listed together here
for a fast top-level review.

1. **What "load the scene 'empty'" means for Bank Load's failed child.**
   Chosen meaning: exclude that child's bit from
   `bank_scenePresentMask()` (the codebase's single existing "does this slot
   have real data" signal, relied on uniformly by AutoSave, the SEQ LED
   present-mask, and Save-page toggling — confirmed via
   `S057_AUTOSAVE_WRITER_WRAP.md` §2/§3). This does **not** zero or
   otherwise touch the resident `scene_t` SRAM for that slot; it only
   ensures the slot is never marked present by this load. If stronger
   "wipe the audio data too" semantics are wanted, `SceneData.c`'s
   `scene_initAll()` (`SceneData.c:544-583`) would need a new per-slot
   `scene_initOne(uint8_t index)` extracted from its existing loop body —
   noted in §5.3 as an optional add-on, not the default plan.
2. **Whether the failed-child overlay should also fire at boot.** Boot
   polls Bank Load through the exact same `menu_pollPresetStatus()` code
   path Menu uses at runtime (`main.c:902`, comment "apply Bank/Scene/Kit +
   ack"), so the new overlay call in §5.7 fires unconditionally in both
   contexts by construction — there is no separate boot/runtime branch to
   choose between without adding one deliberately. Default plan: let it
   fire at boot too, on the theory that silently hiding a quarantine event
   the user explicitly asked to be visible defeats the point; flagged here
   in case unattended-boot noise is undesirable, in which case §5.7 would
   need an explicit `menu_activePage`/boot-phase guard.
3. **Multiple children failing in one Bank Load share one error code slot.**
   `fs_error_code` is a single flat buffer (`filesystem.c:339`); if more than
   one child fails in the same Bank Load, the overlay shows only the last
   failure's code. This matches the existing single-code convention used
   everywhere else in this file (`"KDir"`, `"KSet"`, `"KIns"`, etc.) and is
   treated as acceptable — the failed-slot mask (§5.4) is still fully
   accurate even though the displayed code names only one of possibly
   several failures.
4. **Embedded-Kit full display-name reconstruction needs a hardware/behavior
   check, not just static reading.** §5.3 reconstructs the on-disk `Kit
   <name>` component as the literal concatenation `"Kit "` +
   `op_scene_child_display_name` for the rename's `oldDisplayName` argument,
   because only the post-prefix suffix is retained past the initial scan
   (`filesystem.c:9057-9063`). `storage_copyDisplayName()`'s padding
   behavior (whether it right-pads with spaces to a fixed width) needs a
   one-time check against how `afatfs_renameObject_lfn()`'s
   case-insensitive match actually compares trailing padding, before
   trusting this reconstruction on real hardware — flagged in §5.3 at the
   exact line.
5. **Root Kit/Scene canonical-member-name check stays Bank-only, deliberately
   not widened** — **superseded by §4g below.** This item originally noted
   that `filesystem_kitMemberNameIsCanonical()` (a length-only check: is the
   member's filename stem 8 characters or fewer) currently only runs for a
   Bank-local embedded Kit, and proposed leaving that asymmetry alone rather
   than widening the check to reject the same over-length name elsewhere.
   Direction received since: an over-length name should never be a rejection
   at all — it should be silently repaired in place. §4g replaces "reject if
   non-canonical" with "truncate to canonical on sight, everywhere," which
   closes this asymmetry a different way than either original option (leave
   it, or widen the rejection) — see §4g for the rule and its interaction
   with the Kit-quarantine plan in §5.3.

### 4g. General rule: lazy 8-character truncation on sight, everywhere (new)

Per direction, a new standing rule, independent of the Kit-quarantine
mechanism above but touching the same code paths:

**Rule:** any time the filesystem code encounters an object name longer
than 8 characters, it truncates that name to 8 characters. For everything
except Instrument filenames, this is a plain truncation — no further
disambiguation. For Instrument filenames specifically, truncation must also
guarantee no duplicate results:

- **Instrument embedded in a Kit** (one of the six `file=` members in
  `kitset.kcg`): truncate to 7 characters and append the 1-based voice/slot
  number as the 8th character. This alone guarantees uniqueness among a
  single Kit's own members, since no two of a Kit's six member slots share a
  voice number.
- **Instrument in the library** (a standalone file under `Instrument/`, not
  embedded in a Kit): truncate, then apply the **existing** duplicate-
  avoidance numbering mechanism already used elsewhere in this codebase for
  exactly this kind of collision. Per direction, this implementation is not
  to be revised — only reused. (Not confirmed by name in this session per
  the "no code dive" instruction, but the collision-retry loop already found
  and read this session inside `filesystem_repairNames_tick()`
  — `op_repair_retry`/`op_repair_suffix`, incrementing a numeric suffix up
  to 999 on a naming collision, `filesystem.c:6967-7027` — looks like the
  right existing mechanism to point the implementer at; confirm before
  reusing.)

**Trigger discipline — this is the part that differs from a simple
validation-length check:** no new dedicated boot-time scan is added purely
to hunt for over-length names. The rule fires lazily, at whatever point the
filesystem's own existing code already touches that name for another
reason — an existing repair pass, a load, a scan, a rename — not from a new
proactive traversal added solely for this. "As soon as the filesystem sees
it" means the first natural touch-point, not a synchronized sweep.

**Interaction with §5's Kit-quarantine plan — flagged for the implementation
pass, not resolved here:** §5.3 currently classifies a failed
`filesystem_kitMemberNameIsCanonical()` check (Bank-embedded Kit context
only, `filesystem.c:9280-9288`) as `FS_LOAD_INVALID_KIT`, feeding the same
quarantine-the-whole-Kit path as a genuine parse failure. Under this new
rule, an over-length member name is no longer a failure to quarantine over
at all — it is a name to repair in place (truncate + rename the member
file, then continue loading it normally). This changes the shape of that
one classification row in §5.3's table and needs its own small
sub-machine (truncate-and-rename the one over-length member, then resume
the kitset parse) rather than reusing the quarantine machinery — left as a
follow-up detailing pass, not worked out in this session per the "no code
dive" direction for this rule.

## 5. Fully detailed implementation plan

This section supersedes the old brief §5. Every subsection names the exact
file, function, and (for the three tick state machines) `op_phase` case
numbers involved, plus the description-comment text to add at each new code
block. Phase numbers below were chosen by scanning each state machine's
existing `switch (op_phase)` for gaps, so nothing collides with an existing
case label:

- `filesystem_loadKitDirectory_tick()` uses phases `0-21` and `28`
  (terminal). Phases **22-27 are free** — used below for the Kit-quarantine
  sub-machine.
- `filesystem_loadSceneDirectory_tick()` (shared by root Scene Load and
  every Bank-local child Scene load) uses phases `0-46`, `53-61`, and `72`
  (terminal); `47-52` are a `#if 0`'d retired binary-Step reader, left alone.
  Phases **62-71 are free** — used below for the Kit/Scene-quarantine
  sub-machine.
- `filesystem_loadBankDirectory_tick()` uses phases `0-12`, `15-31`, and
  `80-89`. No new phases are needed here — the Bank-level change is a
  rewrite of existing case **20**'s body plus the terminal `FS_STATUS_DONE`
  call at case **86**, not new async steps (the actual embedded-Kit rename
  for a Bank-local child happens inside the shared Scene tick's own new
  62-71 phases, which the Bank loader already delegates into).

### 5.0 Shared building blocks (new state, reused across 5.1-5.4)

**New enum, `filesystem.c`, near the existing `fs_kit_validation_result_t`/
`fs_kit_quarantine_result_t` declarations (~line 1200-1211):**

```c
typedef enum {
    FS_LOAD_INVALID_NONE = 0u,   /* no content failure classified yet */
    FS_LOAD_INVALID_SCENE,       /* Scene's own content: sceneset/pattern/
                                     effect/missing-child */
    FS_LOAD_INVALID_KIT,         /* embedded or root Kit content */
} fs_load_invalid_layer_t;

/*
 * Classify a load-time content failure by which layer produced it.
 *
 * Set immediately alongside the existing op_close_status = FS_STATUS_ERROR
 * assignment at each site that already proves the failure is a content
 * defect (a parse/finalize rejection, a canonical-name check, or a required
 * child genuinely absent) rather than a raw open/read fault of unknown
 * cause. Consumed only at the shared terminal phase (Kit's case 28, Scene/
 * Bank's case 72) to decide whether the newly-identified folder is eligible
 * for the err... quarantine rename below. Distinct from FS_STATUS_ERROR
 * itself so the many pre-existing non-content error exits (case 0/2's
 * missing Kit/Scene root, a chdir/scan AFATFS_OPERATION_FAILURE) are never
 * accidentally treated as quarantine-eligible merely because they also set
 * FS_STATUS_ERROR.
 */
static fs_load_invalid_layer_t op_load_invalid_layer = FS_LOAD_INVALID_NONE;
```

Reset `op_load_invalid_layer = FS_LOAD_INVALID_NONE;` inside
`filesystem_start()` alongside the existing `memset(fs_error_code, ...)` at
`filesystem.c:21008`, so a stale classification from a previous operation
can never leak into a new one.

**Reused, not new:** the actual rename primitive and its wait/retry
scaffolding are not new code. `filesystem_repairNames_tick()`
(`filesystem.c:6835-7040`) already implements the exact shape needed —
close the scan handle, `chdir` so the current directory is the target's
*parent*, call `afatfs_renameObject_lfn(oldName, newName,
AFATFS_MATCH_CASE_INSENSITIVE, opennameOutBuf, on_rename_complete)`, wait on
the shared `op_rename_done`/`op_rename_result` flags set by the existing
`on_rename_complete()` callback (`filesystem.c:1698-1710`). The new
sub-machines in 5.2/5.3 below reuse this exact pattern and its existing
shared scratch fields — `op_rename_done`, `op_rename_result`,
`op_repair_old_name`, `op_repair_new_name`, `op_repair_rename_open_name`
(all already `AFATFS_LONG_FILENAME_MAX + 1u` or `AFATFS_SHORT_FILENAME_MAX`
sized and already reused across three unrelated call sites — the repair-name
tick, and two `settings.cfg` promote-renames at `filesystem.c:16277` and
`16509`) — no new rename-specific globals are introduced. Note: the smaller
per-load scratch buffers used for the *ordinary* open path,
`op_root_open_name[AFATFS_SHORT_FILENAME_MAX]` (13 bytes) and
`op_scene_display_name[STORAGE_SCENE_DISPLAY_NAME_LEN + 1u]` (9 bytes), are
**not** large enough to hold an `err`-prefixed name (`"err"` + up to 12 more
characters can reach 15-16 bytes) — this is exactly why `op_repair_old_name`/
`op_repair_new_name` (49 bytes each) must be used for the rename call instead
of the smaller buffers already in scope.

**`filesystem_makeQuarantineName()` is reused unchanged** (`filesystem.c:
17730-17755`) — it already takes an arbitrary source string and an output
buffer/capacity, with no dependency on the boot-only blocking helpers around
it.

### 5.1 Remove the boot-wide blocking quarantine call (the original §1/§2 ask)

1. In `filesystem_createLibraryIndexBlocking()`, delete the
   `if (kind == FS_LIBRARY_INDEX_KIT) { ... filesystem_quarantineKitLibraryBlocking()
   ... }` block (`filesystem.c:21575-21609` in the pre-refactor numbering),
   including its `filesystem_bootLoggingArm("KITQUAR ")` /
   `filesystem_bootLoggingOperationDone()` arm/disarm pair (§4b).
2. **Delete, not keep:** `filesystem_quarantineKitLibraryBlocking()`
   (`filesystem.c:17920-18043`) and `filesystem_validateCurrentKitBlocking()`
   (`filesystem.c:17626-17728`). The original §5 item 2 left open the
   possibility of keeping `filesystem_validateCurrentKitBlocking()` "if
   option 2 is chosen, since its per-Kit validation logic would be reused
   there instead." That does not apply to this concrete design: the new
   lazy-quarantine logic below detects invalidity by reacting to the
   load path's own **existing** async parse/open failures (§3's
   already-graceful `storage_kitsetParseLine()` /
   `storage_instrumentParseLine()` paths), not by re-running the blocking
   byte-at-a-time validator. Both functions become fully dead code and
   should be deleted.
   - The already-`#if 0`'d retired code immediately below them —
     `filesystem_quarantineEmbeddedKitBlocking()`,
     `filesystem_quarantineScenesInParentBlocking()`,
     `filesystem_quarantineBankKitsBlocking()` (`filesystem.c:18045-18293`)
     — stays exactly as-is (already excluded from the build, already
     documented as retired for the UI/audio-stall reason recorded in its own
     comment). Do not resurrect it; the new design does not need a recursive
     blocking traversal of Bank/Scene content, only the same single-child
     path Bank Load already walks.
3. No change to `filesystem_repairLibraryNamesBlocking()` — confirmed in §2
   it already runs unconditionally for all three kinds and already does the
   canonicalization work this refactor depends on.

### 5.2 `filesystem_loadKitDirectory_tick()` — standalone root Kit Load

**Classification (existing `op_close_status = FS_STATUS_ERROR;` sites gain
one line, `op_load_invalid_layer = FS_LOAD_INVALID_KIT;`, immediately
alongside):**

| Phase | Existing failure | Add classification |
|---|---|---|
| 12 | `kitset.kcg` open failed (`op_file == NULL`) | `FS_LOAD_INVALID_KIT` |
| 13 | `kitset.kcg` read/parse/finalize failed | `FS_LOAD_INVALID_KIT` |
| 18 | Instrument member open failed | `FS_LOAD_INVALID_KIT` |
| 19 | Instrument member read/parse/finalize failed | `FS_LOAD_INVALID_KIT` |

Phase **7** (`op_file == NULL` opening the selected Kit's own numbered
directory, error code `"KDir"`) is deliberately **excluded** — this means
the directory could not even be opened, which is exactly the ambiguous
"could be a transient open fault" case §3/the original boot validator
treated cautiously; see the FS-ready gate below, which independently covers
this by requiring the filesystem to still be provably healthy. Phases
**0/2** (invalid request / missing `Kit/` root entirely) call
`filesystem_finish(FS_STATUS_ERROR)` **directly**, bypassing the terminal
phase 28 entirely — correctly, since no specific Kit folder has been
identified yet at that point, so there is nothing to quarantine.

**New terminal handling — insert before existing case 28's body, phases
22-27:**

```
case 22: /* DECIDE: quarantine-eligible? */
    if (op_close_status != FS_STATUS_ERROR ||
        op_load_invalid_layer != FS_LOAD_INVALID_KIT ||
        afatfs_getFilesystemState() != AFATFS_FILESYSTEM_STATE_READY) {
        /*
         * Not a provable content defect: either the load actually
         * succeeded, the failure was never classified as Kit-content
         * (e.g. phase 7's ambiguous directory-open failure), or the
         * filesystem itself is not in a healthy READY state right now
         * (the same IO_ABORT-vs-INVALID_CONTENT distinction the original
         * boot validator drew via filesystem_blockFsOk(), reused here
         * through the equivalent live asyncfatfs state query). Skip
         * straight to the existing finish path with no rename attempt.
         */
        op_phase = 28u;
        return;
    }
    /* Current directory is still inside the bad Kit's own numbered folder
     * (chdir'd at phase 8 and never left since). Return to its parent,
     * Kit/, before the rename call below — renaming a directory the
     * current directory points into is not a valid operation. */
    {
        afatfsOperationStatus_e ast = afatfs_chdirParent();
        if (ast == AFATFS_OPERATION_IN_PROGRESS) return;
        if (ast == AFATFS_OPERATION_FAILURE) { op_phase = 28u; return; }
    }
    op_phase = 23u;
    return;

case 23: /* BUILD quarantine name + START rename */
    strncpy(op_repair_old_name, op_root_open_name,
            sizeof(op_repair_old_name) - 1u);
    op_repair_old_name[sizeof(op_repair_old_name) - 1u] = '\0';
    filesystem_makeQuarantineName(op_repair_new_name,
                                  sizeof(op_repair_new_name),
                                  op_repair_old_name);
    op_rename_done = 0u;
    op_rename_result = AFATFS_RESULT_OK;
    memset(op_repair_rename_open_name, 0, sizeof(op_repair_rename_open_name));
    if (!afatfs_renameObject_lfn(op_repair_old_name, op_repair_new_name,
                                 AFATFS_MATCH_CASE_INSENSITIVE,
                                 op_repair_rename_open_name,
                                 on_rename_complete))
        return;
    op_phase = 24u;
    return;

case 24: /* WAIT rename */
    if (!op_rename_done) return;
    /*
     * A failed quarantine rename does not change the outcome of the load
     * that was already proven to have failed above — it only means the
     * bad folder stays visible for one more session. Fall through to the
     * ordinary finish path either way; do not surface a second, different
     * error for the rename itself.
     */
    op_phase = 28u;
    return;
```

Note: phase 22-24 above is written as a 3-step block reusing
`op_root_open_name` as the already-known "old" on-disk component (it was
populated by `filesystem_makeNumberedDir()` at phase 6 and never
overwritten afterward) — phases 25-27 are left spare for whatever the
implementer finds is actually needed once this is coded against the real
`afatfs_chdirParent()`/`afatfs_renameObject_lfn()` timing (e.g. if a
close-scan-handle step turns out to be required; phase 8/9's directory
handle was already closed by phase 10 in the existing flow, so this may not
be needed, but confirm against the real state before assuming).

**Existing case 28 gains one description-comment update** (no logic change
beyond falling through from the new phase 24 above):

```
case 28: /* RETURN TO ROOT + FINISH */
    /*
     * Reached directly from an early non-content failure (phases 0/2/7 never
     * route here — see their own finish() calls), or from the new
     * quarantine decision/rename sub-machine at phases 22-24 above, which
     * always funnels back here regardless of whether the rename itself
     * succeeded. op_close_status already holds the load's own terminal
     * result; the quarantine attempt never changes it.
     */
    ...
```

### 5.3 `filesystem_loadSceneDirectory_tick()` — root Scene Load and every Bank-local child

This function is shared by `current_op == FS_INTERNAL_OP_LOAD_SCENE` (root
Scene Load, cascades to the owning Scene per §4a-iii) and
`current_op == FS_INTERNAL_OP_LOAD_BANK` (Bank-local child, never renames
the Scene per §4a-iv). Both branches funnel through the same new terminal
sub-machine; the branch on `current_op` decides only *which* rename(s) fire.

**Classification (existing `op_close_status = FS_STATUS_ERROR;` sites gain
one line each):**

| Phase range | What failed | Classification |
|---|---|---|
| 9, 11 | Scene child-scan failure, or missing `Kit `/`.pat`/`.fx` child | `FS_LOAD_INVALID_SCENE` |
| 13, 14 | `sceneset.scg` open/read/parse/finalize | `FS_LOAD_INVALID_SCENE` |
| 18 | Embedded Kit directory open failed | `FS_LOAD_INVALID_KIT` |
| 23, 24 | Embedded `kitset.kcg` open/read/parse/finalize (incl. Bank's canonical member-name check, `filesystem.c:9280-9288`) | `FS_LOAD_INVALID_KIT` |
| 28, 29 | Embedded Instrument member open/read/parse/finalize | `FS_LOAD_INVALID_KIT` |
| 40 | Selected-Scene reopen failed (phase 39/40, after Kit already proven valid) | `FS_LOAD_INVALID_SCENE` |
| 45, 46, 53 | Bridge `.pat` open/probe/parse | `FS_LOAD_INVALID_SCENE` |
| 57, 58 | Effect placeholder open/read/parse | `FS_LOAD_INVALID_SCENE` |

Phase **7** (selected Scene directory open failed) is deliberately
**excluded**, same reasoning as Kit loader's phase 7. Phase **33**'s
`afatfs_chdirParent()` failure branch (navigation, not content) is also
excluded — leave `op_load_invalid_layer` at `FS_LOAD_INVALID_NONE` there.
Phases **0/2** call `filesystem_finish()` directly (bypass phase 72),
correctly excluded for the same reason as the Kit loader.

**New terminal handling — insert before existing case 72's body, phases
62-71:**

```
case 62: /* DECIDE: quarantine-eligible, and which rename(s)? */
    if (op_close_status != FS_STATUS_ERROR ||
        op_load_invalid_layer == FS_LOAD_INVALID_NONE ||
        afatfs_getFilesystemState() != AFATFS_FILESYSTEM_STATE_READY) {
        op_phase = 72u;
        return;
    }
    if (current_op == FS_INTERNAL_OP_LOAD_BANK) {
        /*
         * Bank-local child: never rename the SS Name Scene folder (its
         * identity is positional, not browsable — see §4a-iv). Only an
         * embedded-Kit failure has anything to rename here.
         */
        if (op_load_invalid_layer != FS_LOAD_INVALID_KIT) {
            op_phase = 72u;
            return;
        }
        op_phase = 65u; /* embedded-Kit-only rename, no Scene rename */
        return;
    }
    /* Root Scene Load (current_op == FS_INTERNAL_OP_LOAD_SCENE). A
     * Kit-layer failure renames the embedded Kit AND the owning Scene; a
     * Scene-layer failure renames only the owning Scene. */
    op_phase = (op_load_invalid_layer == FS_LOAD_INVALID_KIT) ? 63u : 68u;
    return;

case 63: /* ROOT SCENE + KIT FAILURE: return from wherever we are to the
          * owning Scene folder before renaming the embedded Kit */
    {
        afatfsOperationStatus_e ast = afatfs_chdirParent();
        if (ast == AFATFS_OPERATION_IN_PROGRESS) return;
        if (ast == AFATFS_OPERATION_FAILURE) { op_phase = 72u; return; }
    }
    /*
     * Cwd is now the owning Scene folder (one chdirParent() hop out of the
     * embedded Kit directory — the same single-hop transition case 33's
     * existing successful path already relies on for this exact
     * Kit-child -> Scene-parent boundary).
     */
    op_phase = 64u;
    return;

case 64: /* BUILD embedded-Kit quarantine name + START rename */
    /*
     * Reconstruct the on-disk "Kit <name>" component. Only the suffix after
     * "Kit " survives past the initial child scan (op_scene_child_display_name,
     * filesystem.c:9057-9063) — filesystem_nameStartsWithKitSpace() already
     * proved the real on-disk name begins with the literal 4-character
     * prefix "Kit ", so concatenation reconstructs it exactly.
     *
     * NOTE (flagged in §4f item 4): confirm storage_copyDisplayName()'s
     * padding behavior against afatfs_renameObject_lfn()'s case-insensitive
     * match before trusting this on hardware — if the display suffix is
     * space-padded to a fixed width but the real on-disk name is not, the
     * match may need trimming first.
     */
    op_repair_old_name[0] = 'K'; op_repair_old_name[1] = 'i';
    op_repair_old_name[2] = 't'; op_repair_old_name[3] = ' ';
    strncpy(&op_repair_old_name[4], op_scene_child_display_name,
            sizeof(op_repair_old_name) - 5u);
    op_repair_old_name[sizeof(op_repair_old_name) - 1u] = '\0';
    filesystem_makeQuarantineName(op_repair_new_name,
                                  sizeof(op_repair_new_name),
                                  op_repair_old_name);
    op_rename_done = 0u;
    op_rename_result = AFATFS_RESULT_OK;
    memset(op_repair_rename_open_name, 0, sizeof(op_repair_rename_open_name));
    if (!afatfs_renameObject_lfn(op_repair_old_name, op_repair_new_name,
                                 AFATFS_MATCH_CASE_INSENSITIVE,
                                 op_repair_rename_open_name,
                                 on_rename_complete))
        return;
    op_phase = 66u; /* rename embedded Kit, then also rename owning Scene */
    return;

case 65: /* BANK-LOCAL CHILD + KIT FAILURE: return to the Bank-local Scene
          * folder, then rename only the embedded Kit (no Scene rename) */
    {
        afatfsOperationStatus_e ast = afatfs_chdirParent();
        if (ast == AFATFS_OPERATION_IN_PROGRESS) return;
        if (ast == AFATFS_OPERATION_FAILURE) { op_phase = 72u; return; }
    }
    /* Same reconstruction/rename as phase 64, but exit straight to 72
     * afterward (no owning-Scene rename in Bank context, per §4a-iv). */
    op_repair_old_name[0] = 'K'; op_repair_old_name[1] = 'i';
    op_repair_old_name[2] = 't'; op_repair_old_name[3] = ' ';
    strncpy(&op_repair_old_name[4], op_scene_child_display_name,
            sizeof(op_repair_old_name) - 5u);
    op_repair_old_name[sizeof(op_repair_old_name) - 1u] = '\0';
    filesystem_makeQuarantineName(op_repair_new_name,
                                  sizeof(op_repair_new_name),
                                  op_repair_old_name);
    op_rename_done = 0u;
    op_rename_result = AFATFS_RESULT_OK;
    memset(op_repair_rename_open_name, 0, sizeof(op_repair_rename_open_name));
    if (!afatfs_renameObject_lfn(op_repair_old_name, op_repair_new_name,
                                 AFATFS_MATCH_CASE_INSENSITIVE,
                                 op_repair_rename_open_name,
                                 on_rename_complete))
        return;
    op_phase = 67u;
    return;

case 66: /* WAIT embedded-Kit rename (root Scene path), then also rename
          * the owning Scene */
    if (!op_rename_done) return;
    /* Cwd is still the owning Scene folder (rename does not change cwd).
     * One more chdirParent() hop reaches the Scene's own parent (root
     * Scene/), where the Scene-rename below needs to run. */
    op_phase = 68u;
    return;

case 67: /* WAIT embedded-Kit rename (Bank-local path) — done, no Scene
          * rename in this context */
    if (!op_rename_done) return;
    op_phase = 72u;
    return;

case 68: /* ROOT SCENE RENAME: reach the Scene's own parent */
    /*
     * Reached either directly (Scene-layer failure, no Kit involved) or
     * after phase 66 already renamed the embedded Kit (Kit-layer failure).
     * Either way cwd is currently the owning Scene folder; one more
     * chdirParent() hop reaches Scene/ itself, the correct parent for
     * renaming the Scene folder.
     */
    {
        afatfsOperationStatus_e ast = afatfs_chdirParent();
        if (ast == AFATFS_OPERATION_IN_PROGRESS) return;
        if (ast == AFATFS_OPERATION_FAILURE) { op_phase = 72u; return; }
    }
    op_phase = 69u;
    return;

case 69: /* BUILD Scene quarantine name + START rename */
    strncpy(op_repair_old_name, op_root_open_name,
            sizeof(op_repair_old_name) - 1u);
    op_repair_old_name[sizeof(op_repair_old_name) - 1u] = '\0';
    filesystem_makeQuarantineName(op_repair_new_name,
                                  sizeof(op_repair_new_name),
                                  op_repair_old_name);
    op_rename_done = 0u;
    op_rename_result = AFATFS_RESULT_OK;
    memset(op_repair_rename_open_name, 0, sizeof(op_repair_rename_open_name));
    if (!afatfs_renameObject_lfn(op_repair_old_name, op_repair_new_name,
                                 AFATFS_MATCH_CASE_INSENSITIVE,
                                 op_repair_rename_open_name,
                                 on_rename_complete))
        return;
    op_phase = 70u;
    return;

case 70: /* WAIT Scene rename */
    if (!op_rename_done) return;
    op_phase = 72u;
    return;
```

(Phase 71 is left spare, same reasoning as the Kit loader's spare phases —
for whatever the implementer finds is actually needed once coded against
real timing, e.g. an explicit close of a stale handle that the read-only
analysis above did not catch.)

**Existing case 72 gains a description-comment update only** — no logic
change to its existing `op_bank_payload_active` handoff or the
`FS_INTERNAL_OP_LOAD_SCENE` HCNAMES-continuation branch; both are reached
exactly as before, just now possibly after a rename attempt instead of
straight from the original failure.

### 5.4 `filesystem_loadBankDirectory_tick()` — the partial-failure contract

**New per-operation state, declared near `op_bank_scene_load_mask`/
`op_bank_loaded_scene` (`filesystem.c:1088-1092`):**

```c
/*
 * Children of the current Bank Load proven invalid by content (not I/O
 * abort) — excluded from op_bank_scene_load_mask as they fail, so the
 * final present-mask union and Preset's dirty-marking loop never treat
 * them as loaded. Nonzero is also the signal Preset/Menu use to show the
 * existing filesystem error overlay after an otherwise-successful,
 * FS_STATUS_DONE Bank Load — see §4a-iv for why the overall operation
 * must not become FS_STATUS_ERROR just because one child failed.
 */
static uint16_t op_bank_scene_failed_mask = 0u;
```

Reset `op_bank_scene_failed_mask = 0u;` alongside the existing
`op_bank_loaded_scene = 0u;` reset at `filesystem.c:21078` (Bank Load's
request-start scratch reset).

**Rewrite case 20's failure branch** (`filesystem.c:10790-10797` in the
pre-refactor numbering):

```
case 20: {
    uint8_t child_slot;

    if (op_close_status != FS_STATUS_DONE) {
        if (afatfs_getFilesystemState() != AFATFS_FILESYSTEM_STATE_READY) {
            /*
             * Genuine I/O abort partway through a child: cannot trust any
             * completed work enough to keep going. Unchanged from today —
             * hard-fail the whole Bank Load exactly as before.
             */
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        /*
         * One child's content was proven invalid (its embedded Kit, or its
         * own sceneset/pattern/effect content — filesystem_loadSceneDirectory_tick()'s
         * new phases 62-71 have already quarantined the embedded Kit if
         * that was the cause, per §4a-iv, and never renamed this Bank-local
         * Scene folder itself). Exclude it from the effective load mask so
         * neither the present-mask union below nor Preset's dirty-marking
         * loop (on_bank_load_complete(), presetManager.c:509-543) ever
         * treats it as loaded, remember it as failed so the final present-
         * mask commit can force it empty even if it was resident from a
         * prior session, and set a distinguishing error code for the
         * overlay Menu shows later — then fall through to the existing
         * "advance to next selected child" loop instead of aborting the
         * whole Bank Load. See §4a-iv: the operation itself must still
         * finish FS_STATUS_DONE.
         */
        op_bank_scene_load_mask =
            (uint16_t)(op_bank_scene_load_mask &
                      ~(uint16_t)(1u << op_bank_child_cursor));
        op_bank_scene_failed_mask =
            (uint16_t)(op_bank_scene_failed_mask |
                      (uint16_t)(1u << op_bank_child_cursor));
        filesystem_makeNamedErrorCode("BKKit", op_phase);
    }
    for (child_slot = (uint8_t)(op_bank_child_cursor + 1u);
         child_slot < STORAGE_BANK_SCENE_MAX_SLOTS;
         child_slot++) {
        if ((op_bank_scene_load_mask & (uint16_t)(1u << child_slot)) != 0u) {
            op_bank_child_cursor = child_slot;
            op_phase = 21u;
            return;
        }
    }
    /* ... existing loop-exhausted metadata-commit body continues unchanged
     * from here (bank_setDisplayName(), present-mask union, active-scene
     * selection, filesystem_markSettingsDirty(), etc.) EXCEPT the present-
     * mask union gains one change, immediately below. */
```

**One-line change inside the existing loop-exhausted present-mask commit**
(both the empty-Bank branch around `filesystem.c:10685` and the normal
non-empty-Bank branch around `filesystem.c:10837-10838` — the latter is the
one that actually matters here, since a failed child only exists in the
non-empty path):

```
/* Before:
 *   bank_setScenePresentMask((uint16_t)(bank_scenePresentMask() |
 *                                       op_bank_scene_load_mask))
 * After: force-exclude any child that failed this load, even if it was
 * already present from a prior session — "load the scene empty" (§4f item
 * 1) means this load explicitly leaves that slot with no data, superseding
 * whatever was there before.
 */
bank_setScenePresentMask(
    (uint16_t)((bank_scenePresentMask() | op_bank_scene_load_mask) &
              ~op_bank_scene_failed_mask));
```

**Case 86's terminal `filesystem_finish(FS_STATUS_DONE)` is unchanged** —
this is the load-bearing part of §4a-iv: the overall Bank Load still reports
success. The failed-child signal is carried out-of-band via
`op_bank_scene_failed_mask`, exposed through a new accessor (5.5) instead of
through the completion status.

### 5.5 `filesystem.h` / `filesystem.c` — new public accessor

Alongside the existing `filesystem_lastBankLoadLoadedScene()` /
`filesystem_lastBankLoadSceneMask()` (`filesystem.c:23320-23344`):

```c
uint16_t filesystem_lastBankLoadFailedSceneMask(void)
{
    /*
     * Expose which Bank Load children failed with provably-invalid content
     * (never an I/O abort — that path still hard-fails the whole operation,
     * unchanged). Nonzero here is the sole signal that the just-completed,
     * FS_STATUS_DONE Bank Load still needs the existing filesystem error
     * overlay shown once, per §4a-iv. Cleared to zero at the start of every
     * new Bank Load request.
     */
    return op_bank_scene_failed_mask;
}
```

Declare it in `filesystem.h` next to the two existing sibling declarations.

### 5.6 `presetManager.c` — surface the new mask through Preset

**`on_bank_load_complete()` (`presetManager.c:509-543`)** — no change to its
existing dirty-marking loop (it already only reads
`filesystem_lastBankLoadSceneMask()`, which now correctly excludes the
failed child per 5.4's rewrite, so nothing extra is needed there). Add one
new retained field read at the same point:

```c
static uint16_t pm_bank_load_failed_scene_mask = 0u;
```

Inside `on_bank_load_complete()`, alongside its existing
`completed_scene_mask = filesystem_lastBankLoadSceneMask();` line:

```c
pm_bank_load_failed_scene_mask = filesystem_lastBankLoadFailedSceneMask();
```

**New accessor, alongside `preset_completedBankLoadedScene()`
(`presetManager.c:2274-2277`):**

```c
uint16_t preset_bankLoadFailedSceneMask(void)
{
    /*
     * Nonzero when the just-completed Bank Load (FS_STATUS_DONE) still had
     * one or more children fail with invalid content. Menu checks this
     * after its normal PRESET_OP_BANK_LOAD success handling to decide
     * whether to also show the existing filesystem error overlay — see
     * S057_BOOT_KIT_SANITIZE_REFACTOR.md §4a-iv.
     */
    return pm_bank_load_failed_scene_mask;
}
```

### 5.7 `menu.c` — show the existing overlay from the success path

Two call sites inside the `PRESET_OP_BANK_LOAD` case
(`menu.c:8134-8175`), both currently ending in either the
`preset_loadFirstAvailableSceneOrKit()` fallback branch (no child loaded at
all) or the normal `menu_startSoundApply()` success branch (at least one
child loaded). Add the same one check to both, right after their existing
work, so the overlay is shown regardless of which sub-branch actually ran:

```c
if (preset_bankLoadFailedSceneMask() != 0u) {
    /*
     * At least one Bank Load child failed with invalid content
     * (S057_BOOT_KIT_SANITIZE_REFACTOR.md §4a-iv). The operation itself
     * still completed FS_STATUS_DONE — boot's Bank Load poll loop
     * (main.c:898-901) would otherwise treat any FS_STATUS_ERROR here as a
     * whole-boot failure — so this is the only place that failure becomes
     * visible. Reuses the existing generic overlay function; filesystem_ack()
     * inside it is a documented no-op when the facade is already idle,
     * which it already is by this point via preset_completeFilesystemOp().
     */
    menu_showFilesystemErrorOverlay();
}
```

Placement: after the `if (!preset_completedBankLoadedScene()) { ... return; }`
branch's `preset_loadFirstAvailableSceneOrKit()`/`menu_finishLoadSaveCommand()`
work (before its `return`), and after the normal-path
`menu_startSoundApply(...)` call further down in the same case block (before
its `break`). Both are needed because either sub-branch can run depending on
whether the *active* scene specifically was the one that failed (§4a-iv
already noted this reuses the existing empty-Bank fallback machinery for
free when the active scene's slot is the failed one).

### 5.8 Summary of all touched files

| File | Change |
|---|---|
| `Core/Hardware/SD/filesystem.c` | Delete boot quarantine call site + both blocking helper functions (5.1); add `fs_load_invalid_layer_t` + `op_load_invalid_layer` (5.0); classify existing error sites in Kit/Scene tick functions (5.2/5.3); add quarantine sub-machine phases 22-27 (Kit) and 62-71 (Scene/Bank) (5.2/5.3); add `op_bank_scene_failed_mask` + rewrite Bank case 20/present-mask commit (5.4); add `filesystem_lastBankLoadFailedSceneMask()` (5.5) |
| `Core/Hardware/SD/filesystem.h` | Declare `filesystem_lastBankLoadFailedSceneMask()` (5.5) |
| `Core/Bank/Scene/Preset/presetManager.c` | Add `pm_bank_load_failed_scene_mask`, populate it in `on_bank_load_complete()`, add `preset_bankLoadFailedSceneMask()` (5.6) |
| `Core/Bank/Scene/Preset/presetManager.h` | Declare `preset_bankLoadFailedSceneMask()` (5.6) |
| `Core/Menu/menu.c` | Two new calls to the existing `menu_showFilesystemErrorOverlay()` inside `PRESET_OP_BANK_LOAD` (5.7) |

## 6. Test plan

1. **Timing:** measure boot time before/after on the current 44-Kit card.
   Confirm the KITQUAR-specific delay is gone and no other stage grew to
   compensate.
2. **Normal boot, all-valid Kits (current card):** confirm every Kit still
   appears in `.hcindex` with its correct name, loads correctly, no
   regression versus current behavior.
3. **Deliberately malformed Kit — construct one:** corrupt one `kitset.kcg`
   line, or delete one referenced Instrument file, on a Kit in a test copy
   of the card. Confirm: (a) boot completes without hitting the removed
   quarantine gate, (b) the Kit still shows a normal row in
   `.hcindex`/menu with its display name, (c) attempting to Load it fails
   cleanly (`FS_STATUS_ERROR`, no crash, no partial/corrupt state applied,
   whatever failure UX Menu shows is not misleading), (d) confirm the folder
   is renamed `err...` after the failed attempt and no longer appears on a
   subsequent boot/rescan (§4a, superseded in detail by tests 7-9 below).
4. **Boot-timeout stress:** simulate a slow card (or a card with a much
   larger Kit count, e.g. duplicate the 44 Kits several times over) and
   confirm boot no longer times out purely from Kit-count scaling — this is
   the actual false-boot-failure risk this refactor exists to close.
5. **Regression on Scene/Bank scans:** confirm nothing about Kit's boot
   sequencing change affects the subsequent Scene/Bank scan timing or
   ordering (§4e — should be a non-issue, but worth a pass since all three
   share `main.c`'s sequential boot block).
6. **Hardware test, not just build-clean:** per this project's boot-hang
   history (explicitly called out in `AUTOSAVE_READ_PLAN.md §11.3` for the
   adjacent boot-order work), this needs a real hardware boot test, not
   just a clean build — boot-path changes have caused hangs before that
   simulation didn't catch.
7. **Root Kit Load quarantine:** corrupt one `kitset.kcg` line in a test
   Kit. From Menu, Load that Kit slot. Confirm: `FS_STATUS_ERROR`, the
   existing overlay appears (§4a-i), the Kit folder is renamed `errNNN ...`,
   and a subsequent `.hcindex` rebuild/rescan no longer lists it (§4c — the
   rename alone is sufficient, no separate scan-exclusion code needed).
8. **Root Scene Load, Kit-layer cascade:** corrupt the embedded Kit inside a
   root Scene's `Kit <name>/` folder. Load that Scene from Menu. Confirm:
   the embedded Kit folder is renamed `err...`, **and** the owning
   `Scene/NNN Name` folder is also renamed `err...` (§4a-iii), the overlay
   appears, and neither folder appears in a later rescan.
9. **Root Scene Load, Scene-layer-only failure:** corrupt `sceneset.scg` (or
   delete the bridge `.pat`) in a root Scene whose embedded Kit is otherwise
   valid. Confirm: only the Scene folder is renamed; the embedded Kit folder
   is untouched (since it was never proven bad).
10. **Bank Load, one bad child among several selected:** construct a test
    Bank with at least two Bank-local Scene children in the requested mask,
    one with a corrupted embedded Kit. Confirm: (a) the embedded Kit folder
    inside that one Bank-local Scene is renamed `err...`; (b) the Bank-local
    `SS Name` Scene folder itself is **not** renamed (§4a-iv); (c) the
    overall Bank Load still completes `FS_STATUS_DONE`; (d) the *other*,
    valid selected child(ren) load and apply normally — sound apply, active
    Scene/Pattern selection, and any resident data for those slots are
    unaffected; (e) the failed slot's `bank_scenePresentMask()` bit is
    clear even if that slot had prior resident data from an earlier session
    (§4f item 1's "empty" definition); (f) the existing error overlay still
    appears once, with a `BKKit`-style code, surfaced via the new
    `preset_bankLoadFailedSceneMask()` path (§5.6/5.7) rather than via a
    failed completion status.
11. **Bank Load, only the active scene is the bad one:** repeat test 10 but
    make the single requested/failed child the Bank's declared active
    Scene. Confirm the existing empty-Bank fallback ladder
    (`preset_loadFirstAvailableSceneOrKit()`) engages exactly as it already
    does for a genuinely empty Bank (§4a-iv's point that this reuses
    existing machinery for free), and the overlay still appears.
12. **Boot-safety regression (the reason this rewrite exists):** boot with
    a resident Bank whose full 16-scene load mask includes at least one
    corrupted embedded Kit. Confirm the device **boots normally** — this is
    the `main.c:898-901` check from §4a-iv; before this fix a Bank-wide
    `FS_STATUS_ERROR` here would take `goto boot_filesystem_failure`. This
    is the single most important test in this plan: a regression here means
    one bad Kit can still brick a boot, which is the exact failure mode the
    whole refactor exists to close.
13. **Multiple simultaneous Bank Load failures:** two children in the same
    requested mask both invalid. Confirm both are excluded from the present
    mask and both embedded Kits are quarantined; accept (per §4f item 3)
    that the displayed overlay code names only the more-recently-failed one.
14. **Legacy interaction:** confirm a folder already renamed `err...` by
    the old boot-time quarantine (before this refactor shipped) is still
    correctly skipped by every scan post-refactor (§4d — no regression, no
    reverse-quarantine expected either).
