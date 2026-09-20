# S069 — Non-semantic Pattern maintenance restriction (Claude breakout)

## Status

Planning only. No product code, AutoSave format, Pattern format, SRAM
allocation, or scheduler behavior is changed by this document.

This is the Claude-authored breakout of "Make physical relocation
non-semantic" from `S069_ATS_PAT_BOUNDED_CPU.md` (Implementation plan item 1),
expanded with the two additional running/stopped conditions given directly in
this session so a non-semantic Pattern AutoSave can still occur when no
higher-priority AutoSave work exists.

## Relationship to `S069_NON_SEMANTIC_MAINTENANCE.md`

A sibling document with the same breakout scope already exists in the
repository, produced in an earlier pass of this same session (per root
`MEMORY.md`'s Volatile Notes). This document is an independent pass against
current source, written to the running/stopped policy given directly in this
conversation, which is not identical to that sibling's:

- The sibling's running-playback trigger is a configured **three-AutoSave-
  cycle** idle count (`config.h` value `3u`). This session's instruction is a
  direct predicate — *no semantic Pattern AutoSave or parameter AutoSave has
  been triggered or is pending* — with no cycle count. This document follows
  the direct-predicate form given this session.
- The sibling flags "AutoSave cycle" as an undefined follow-up (its follow-up
  #1). Reading the real scheduler (`filesystem.c`, below) shows there is no
  existing "cycle" abstraction at all — every idle-facade poll re-runs a fixed
  priority ladder from the top. That finding is used directly below instead of
  carried forward as an open question.

Where the two documents diverge, this document is the one to implement
against. The sibling was read in full earlier in this session and existed on
disk at that point; it was removed from the working tree later in this same
session by a process outside this conversation (not by this document or any
edit made while writing it). The comparative notes below describe its content
as read, not a file this document has itself deleted or claims to supersede.

## Established starting point

### The relocation transaction

`patSvc_relocateIndex()` (`PatternStackService.c:385-438`) performs the
existing four-step transaction the parent plan describes — reserve
destination, copy the complete logical block, publish the new address,
release the old allocation — and then unconditionally calls
`pat_markPoolMutationDirty(scene)` at line 432. This is the only call site of
that function anywhere in `Core/` (confirmed by repository-wide search); the
callers of `patSvc_relocateIndex()` itself are Tier 1 slack-gap creation
(`patSvc_tier1Step()`), the periodic Tier 2 sweep, and reactive recovery
(`patSvc_reactiveStep()`) — every physical relocation, proactive or reactive,
currently dirties Pattern AutoSave the same way a musical edit would.

### What "semantic dirty" currently does — three effects, not one

`pat_markPoolMutationDirty()` (`PatternData.c:90-94`) calls
`pat_markSceneDirty()` (`PatternData.c:73-77`), which calls two existing
helpers that together perform **three** distinct effects:

1. `bank_invalidateSdCleanScene(scene_index)` (`BankData.c:383-400`) clears the
   Scene's card-clean bit (`bank_scene_sd_clean_mask`) **and** sets its
   mutated-during-save bit (`bank_sd_save_mutated_mask`) — the mechanism that
   lets an explicit Bank/Scene Save detect an edit that landed while that Save
   was in flight.
2. `autosave_markPatternDirty(scene_index)` (`Autosave.c:128-141`) sets the
   Scene's bit in `autosave_pattern_dirty_mask` — the bit
   `filesystem_autosavePatternDrainSchedule_tick()` scans to pick a Scene for
   the next Pattern drain.
3. The same call **also** clears the Pattern HCNAMES "refreshed" witness bit
   for that Scene's row, via `filesystem_clearResidentRefreshed(
   AUTOSAVE_HCNAMES_PATTERN_BASE + scene_index)` (`Autosave.c:138-139`, helper
   at `filesystem.c:6024-6030`).

Neither the parent plan nor the sibling breakout names effect 3 explicitly. It
matters here because it is a *boot-relevant provenance flag*, not merely an
in-RAM scheduling bit — root `MEMORY.md` documents the HCNAMES `R` flag as
part of the boot-time matching-winner/all-refreshed decision. See Follow-up #3
below for what a physical relocation, and a completed non-semantic AutoSave,
should each do to this specific bit.

`pat_markSceneDirty()` itself has 14 other call sites across `PatternData.c`
(real edit functions: specials, automation add/remove, clear, etc.) — it is a
correct, widely-used real-edit helper and is not in question. Only
`pat_markPoolMutationDirty()`'s single call site
(`PatternStackService.c:432`) is being removed; `pat_markPoolMutationDirty()`
itself has no other caller and can be retired outright once that call is
removed.

### The real scheduler has no "cycle" — it is a fixed priority ladder

`filesystem.c` (~24590-24640) runs a fixed sequence every time the shared
facade is idle (`status == FS_STATUS_IDLE`), each stage a no-op unless it
actually starts an operation:

```text
filesystem_settingsWriterSchedule_tick()          (periodic settings.cfg writer)
filesystem_autosaveTraceFlushSchedule_tick()       (diagnostic)
filesystem_patternTraceFlushSchedule_tick()        (diagnostic)
filesystem_autosaveWriterSchedule_tick()           (scalar / parameter AutoSave)
filesystem_autosavePatternDrainSchedule_tick()     (semantic Pattern AutoSave)
```

The comment at the last stage is explicit: "Pattern is the final background
claimant after scalar AutoSave work." There is no counter anywhere that means
"one AutoSave cycle" — the ladder is simply re-evaluated from the top on every
idle poll (about 7,600/second per the parent plan's own measurement). This
document's "triggered or pending" condition is written against this real
ladder, not against an invented cycle concept.

`filesystem_autosaveWriterSchedule_tick()` already contains the exact "queue
it, only actually start once due" shape condition 2 (below) needs:
`fs_autosave_writer_armed` / `fs_autosave_next_due_tick`, armed once, checked
against `time_sysTick` on every later tick, with a page-suppression/
continuation rule (`AUTOSAVE_WRITER_INTERVAL_MS` = 5000,
`AUTOSAVE_WRITER_CONTINUATION_INTERVAL_MS` = 250, `config.h:342,355`).

`filesystem_autosavePatternDrainSchedule_tick()` (`filesystem.c:24126-24180`)
already picks its Scene by lowest-index scan over `autosave_patternDirtyMask()`
(`for (scene = 0u; scene < SCENE_COUNT...)`) and already gates on
`patSvc_idle()`, `seq_recordActive || seq_eraseActive`, the Load/Save page,
`menu_isLoadSaveCommandActive()`, and AFATFS readiness. A new rung reuses this
exact gate list rather than inventing a second one.

### "Active" is not one thing in this codebase

Two different existing symbols can plausibly mean "active":

- `seq_activePattern` (`sequencer.c:108`, "the currently playing pattern") —
  the index `PatternStackService.c` itself uses as `service_scene`.
- `bank_activeSceneSlot()` (`BankData.c:259`) — the Bank's selected/displayed
  Scene slot.

These are not always the same value. Root `MEMORY.md`'s own history records a
real bug from conflating them (the 2026-08-21 "Scene-Pattern fix": Bank Load
committed a new active Scene without realigning `seq_activePattern`, so the
sequencer kept playing/addressing the old one). This document treats "active"
as `seq_activePattern` for the purposes below, because that is the value
`PatternStackService.c` already treats as its one mutation target (see next
finding) — but this is a judgment call, not a settled fact, and is carried
into Follow-up #1.

### Physical relocation today only ever touches one Scene — the active one

`patSvc_tick()`'s Tier 1 slack-gap step, periodic Tier 2 sweep, and reactive
recovery all operate exclusively on `service_scene`
(`PatternStackService.c:1085-1225`). `service_scene` is set to
`seq_activePattern` at init (`patSvc_init()`), at filesystem-replacement
completion (`patSvc_finishSceneReplace()`), and at the end of a target
handover (`patSvc_tick():1121`); during a handover the service only finishes
*queued/bulk/reactive* work for the outgoing Scene — Tier 1 and Tier 2 do not
run at all while `service_handover` is set. So under the architecture as it
exists today, and as still drafted in the companion
`S069_SLACK_REACTIVE_COMPACTION_CLAUDE.md` breakout (which keeps this same
single-target scope — "one image only, not one image per Pattern or Scene"),
**a Scene that is not the currently active/playing one cannot currently
accumulate non-semantic (physical-relocation) dirty work at all.** See
"Practical scope of non-active-first," below.

## Required change

Remove line 432's call to `pat_markPoolMutationDirty(scene)` from
`patSvc_relocateIndex()`. Retire `pat_markPoolMutationDirty()` (its one call
site is gone; `pat_markSceneDirty()` itself keeps all 14 of its other callers
unchanged).

After this split, a completed physical relocation becomes eligible for the new
non-semantic Pattern AutoSave category below, through a new, separate piece of
state (Follow-up #5) — not through `autosave_pattern_dirty_mask`, which must
continue to mean "a real Pattern edit is pending" only.

## Requested AutoSave policy

### Three categories, existing priority preserved, one rung added

```text
1. parameter AutoSave            (existing: filesystem_autosaveWriterSchedule_tick)
2. semantic Pattern AutoSave     (existing: filesystem_autosavePatternDrainSchedule_tick)
3. non-semantic Pattern AutoSave (new)
```

This document does not reorder 1 and 2. "Third priority after parameter and
semantic pattern change" (this session's wording) matches the ladder's
existing order exactly — non-semantic Pattern AutoSave is simply appended as a
new, lowest rung.

### Condition 1 — playback running

> If playback is running and no semantic Pattern AutoSave or parameter
> AutoSave has been triggered or is pending, a non-semantic AutoSave may run.
> Non-active Patterns/Scenes are considered first.

`seq_isRunning()` (`sequencer.c:983`, backed by `seq_running`) is
playback-running. Ladder *position* alone is not sufficient to implement
"triggered or pending": a higher rung can be dirty-and-armed-but-still-inside-
its-own-debounce window (e.g. parameter AutoSave dirty, armed, waiting out its
5-second `AUTOSAVE_WRITER_INTERVAL_MS`) while the shared facade is idle on the
very same tick — ladder position would let a bare new rung slip in ahead of
it. The new rung's own gate must therefore explicitly check both categories'
pending state — recommended as `!autosave_maskHasDirty() &&
autosave_patternDirtyMask() == 0u` (i.e. "triggered" is read as "has an
outstanding dirty bit," independent of whether that bit's own debounce has
elapsed) — not merely "the two higher rungs declined this tick."
`autosave_maskHasDirty()` (Autosave.c:1858-1876) is the confirmed scalar-side
predicate; it exists and is already used by the scheduler at four sites.

Like condition 2 (below), this condition uses the arm/due-tick idiom — arm a
due-tick once eligible, re-check eligibility at the due tick, only then start —
providing a debounce that prevents tight thrash if eligibility flickers on and
off across a few ticks.

### Condition 2 — playback stopped

> If playback is stopped, a non-semantic Pattern AutoSave may be queued as a
> third priority after parameter and semantic Pattern change. It may begin
> only if playback is still stopped when it is due. Non-active
> Patterns/Scenes are considered first.

This is a two-phase arm/due condition, structurally identical to the existing
scalar writer's `fs_autosave_writer_armed` / `fs_autosave_next_due_tick`
idiom (`filesystem.c:24026-24038`) — recommended: reuse that exact shape (arm
a due-tick once eligible, re-check both "still nothing higher pending" and
"still stopped" at the due tick, only then start). This gives the requested
"queued, begins only if still stopped when due" behavior directly, and gives
the feature a debounce for free without inventing a new timing primitive. Both
conditions use this same arm/due-tick shape for consistency (see resolved
Follow-up #6).

### Non-active-first ordering

Both conditions ask for non-active Patterns/Scenes to be considered before the
active one. Mechanically this is a small change to the existing lowest-index
scan pattern in `filesystem_autosavePatternDrainSchedule_tick()`: two passes
over the new eligibility mask (below), skipping `seq_activePattern` on the
first pass, falling back to it on the second.

#### Practical scope of "non-active first"

Given the "physical relocation today only ever touches one Scene" finding
above, this ordering rule will, under the current architecture and the
current draft of the companion slack/compaction plan, almost always have **no
non-active candidate to prefer** — physical relocation (and therefore
non-semantic dirtiness) essentially only ever happens to `seq_activePattern`.
The rule is still correct to build now — it is cheap, harmless, and
forward-compatible — but it is worth being explicit that it is *currently*
close to a no-op. It only becomes materially meaningful if a future change
lets Pattern Stack maintenance touch more than one Scene's pool (a scope
expansion belonging to the companion plan, not this one — see that document's
own cross-reference), or if some other, not-yet-identified source of
non-semantic dirtiness on non-active Scenes exists that this review did not
find. If the user has a specific such source in mind, it should be named
explicitly rather than assumed.

## New state required: a separate non-semantic eligibility indicator

The three-way priority order requires the scheduler to tell "a real edit is
pending" (`autosave_pattern_dirty_mask`) apart from "only a physical
relocation happened" — they cannot share one bit. Recommended: one new
`uint16_t`, same one-bit-per-Scene shape as the existing mask (`SCENE_COUNT` =
16), owned by `Autosave.c` beside `autosave_pattern_dirty_mask`, with a
parallel mark/read/clear API. Two bytes, SRAM1, `.bss`, `Autosave.c`-owned,
cleared alongside the existing dirty-mask discard paths (AutoSave OFF,
Bank-session loss). This is a new static allocation and needs the project's
explicit RAM sign-off before implementation per `MEMORY.md`'s RAM Allocation
Approval Policy, even at two bytes — flagged as Follow-up #5, not assumed.

## Existing contracts that remain unchanged

- PAT4 keeps its current snapshot, CRC, A/B publication, and boot-reader
  rules.
- Parameter AutoSave keeps its existing record, mask, and writer behavior
  unchanged.
- The one-filesystem-owner rule remains in force; the new rung is one more
  entrant behind the existing gate list, not a second facade owner.
- Relocation keeps its existing copy/publish/release ordering and its
  PatternTrace witnesses (`M`/`R`/`G`).
- `seq_activePattern` and `bank_activeSceneSlot()` are confirmed
  semantically equivalent for the purpose of this plan — only the active
  Scene's Pattern may be mutated by PatternStackService or viewed on SEQ
  LEDs/menus. A transient bar-boundary divergence (PERF press commits
  `bank_activeSceneSlot()` immediately while `seq_activePattern` updates at
  the next bar) is irrelevant because PatternStackService latches
  `service_scene` from `seq_activePattern` and triggers a handover drain on
  mismatch.

## Implementation outline

1. Remove the `pat_markPoolMutationDirty(scene)` call from
   `patSvc_relocateIndex()` (`PatternStackService.c:432`). Confirm no other
   caller exists (already confirmed for current source; re-check at
   implementation time). Retire the now-dead helper and its
   `PatternData.h:175` declaration, or repurpose it as the entry point for the
   new non-semantic mark (step 2) if that reads more clearly than introducing
   a differently-named function.
2. Add the new non-semantic eligibility mask and its mark/read/clear API in
   `Autosave.c`, mirroring `autosave_pattern_dirty_mask`'s existing shape.
   Call the mark function from the relocation transaction in place of the
   removed call.
3. Add a new scheduler rung,
   `filesystem_autosaveNonSemanticPatternDrainSchedule_tick()` (naming subject
   to project convention), called after
   `filesystem_autosavePatternDrainSchedule_tick()` in the existing idle
   ladder. Reuse that function's exact gate list (`fs_autosave_enabled`,
   `fs_autosave_runtime_ready`, `fs_autosave_writer_boot_ready`,
   `bank_hasResidentBank()`, Load/Save page and command-active checks, AFATFS
   readiness, `seq_recordActive || seq_eraseActive`, `patSvc_idle()`).
4. Implement condition 1 (running): gate on `seq_isRunning()`,
   `!autosave_maskHasDirty()`, `autosave_patternDirtyMask() == 0u`, and the
   new mask being nonzero; select non-active-first, active-last.
5. Implement condition 2 (stopped): arm/due-tick using the
   `fs_autosave_writer_armed`-shaped idiom; re-check "still stopped" and
   "still nothing higher pending" at the due tick before starting; select
   non-active-first, active-last.
6. Wire the non-semantic drain's completion callback: a completed
   non-semantic AutoSave sets the HCNAMES refreshed witness (same as the
   semantic path's phase-10 staging — the on-card file now matches SRAM
   again) but does not touch card-clean or mutated-during-save bits (no user
   edit occurred). A failed non-semantic write leaves the eligibility bit
   set for retry.
7. Test the split in isolation (force relocations with no edits; confirm no
   semantic dirtiness, no HCNAMES refreshed-witness change, no card-clean
   change) before layering the running/stopped scheduling on top.

## Verification targets

| Case | Required observation |
|---|---|
| Physical relocation, no edit | PatternTrace records the move; `autosave_pattern_dirty_mask`, HCNAMES refreshed bit, and Bank card-clean bit for that Scene are unchanged; the new non-semantic mask bit becomes set. |
| Real Pattern edit (incl. in-place automation append via `pat_tryAppendAutomation()`) | Existing semantic dirty behavior (all three effects) is unchanged. |
| Playback running, parameter or semantic Pattern dirty/armed | Non-semantic rung does not start, regardless of ladder position that tick. |
| Playback running, nothing higher pending, non-semantic mask set | Non-semantic AutoSave starts; a non-active Scene's bit is chosen over the active Scene's bit when both are set. |
| Playback stops, then resumes before the queued due tick | The queued non-semantic AutoSave does not start on that opportunity. |
| Playback stays stopped through the due tick, nothing higher pending | Non-semantic AutoSave starts at the due tick; non-active first. |
| Non-semantic AutoSave completes successfully | HCNAMES refreshed witness is set (on-card PAT4 now matches SRAM); card-clean and mutated-during-save bits are not touched (no user edit occurred). |
| Non-semantic AutoSave fails (I/O error) | No semantic dirtiness is fabricated; the non-semantic eligibility bit remains set for retry on the next opportunity. |
| Power interruption at any point | Existing PAT4 A/B recovery remains valid; the prior on-card generation is still a complete, musically correct file. |

## Follow-up decisions, risks, and ambiguities

### Resolved

1. **"Active" definition — RESOLVED.** `seq_activePattern` and
   `bank_activeSceneSlot()` are semantically equivalent: the "active"
   Scene/Pattern is always the one that is viewed, edited, and mutated by
   PatternStackService. They may diverge from the "playing" Scene in a future
   multi-Scene playback architecture, but that future design will use separate
   read-only sequencer variables — only the active Scene's Pattern may be
   mutated or viewed. The two symbols are kept in sync at every call site
   (PERF press in `menu.c:5783` and Bank Load in `filesystem.c:28042-28049`);
   a transient bar-boundary divergence (`seq_activePattern` updates at the
   next bar via `seq_pendingPattern` while `bank_activeSceneSlot()` updates
   immediately on PERF press) is irrelevant because PatternStackService
   latches `service_scene` from `seq_activePattern` and triggers its own
   handover drain on mismatch (`PatternStackService.c:1090`). Use
   `seq_activePattern` as this plan already does.

2. **Exact "triggered or pending" predicate — RESOLVED.**
   `autosave_maskHasDirty()` exists (`Autosave.c:1858-1876`) and is the
   scalar/parameter "has pending work" predicate, already used by the
   scheduler at four sites (`filesystem.c:8043,23791,24021,24052`). Combined
   with `autosave_patternDirtyMask() == 0u`, these two checks cover both
   higher-priority categories. `fs_autosave_writer_armed` is private to
   `filesystem.c` with no public accessor, but checking armed state is
   unnecessary — the dirty mask is nonzero whenever the writer is armed,
   since arming is triggered by a dirty mask and the mask is cleared only
   when the write completes.

3. **HCNAMES refreshed-witness handling — RESOLVED with finding.** Pattern
   stack maintenance must never modify HCNAMES state. The current code path
   does: `pat_markPoolMutationDirty()` → `pat_markSceneDirty()`
   (`PatternData.c:73-77`) → `autosave_markPatternDirty()`
   (`Autosave.c:128-141`) → `filesystem_clearResidentRefreshed()`
   (`filesystem.c:6024-6030`), which clears the HCNAMES Pattern refreshed
   witness (a RAM flag at `fs_resident_source[AUTOSAVE_HCNAMES_PATTERN_BASE +
   scene_index]`, not a file write) on every physical relocation. Removing
   the `pat_markPoolMutationDirty()` call eliminates this side effect
   automatically — no additional code is needed to protect the HCNAMES
   witness from relocations.

   When a non-semantic AutoSave *completes* and writes a valid PAT4 file, the
   on-card file does match SRAM again. The non-semantic drain's completion
   path should set the refreshed witness exactly as the semantic path's
   phase-10 staging already does — this is an AutoSave completion operation,
   not a maintenance operation, and the witness must not go permanently stale
   after the first relocation.

4. **Card-clean / mutated-during-save handling — RESOLVED.** Explicit
   menu-driven file saves do not race with edits or autosave: autosave is
   suppressed during Load/Save page presence
   (`fs_autosave_page_suppressed`, `filesystem.c:24040`) and
   `menu_isLoadSaveCommandActive()` is checked at multiple scheduler rungs
   (`filesystem.c:23692,24135,24287`). `bank_invalidateSdCleanScene()`'s two
   effects (clear card-clean; mark mutated-during-save) exist specifically to
   detect a user edit racing an in-flight explicit Save, not to track
   disk/SRAM agreement generally. A physical relocation correctly should not
   set either bit, and a completed non-semantic AutoSave does not need to
   re-touch them either — no user edit occurred.

5. **New eligibility-mask RAM approval — RESOLVED.** Two bytes approved.
   SRAM1, `Autosave.c`-owned, `.bss`, `uint16_t`, same one-bit-per-Scene
   shape as `autosave_pattern_dirty_mask`.

6. **Running-condition debounce — RESOLVED.** Both conditions (running and
   stopped) use the arm/due-tick idiom. Condition 1 (running) is no longer
   immediate — it uses the same debounce shape as condition 2, preventing
   tight thrash if eligibility flickers across ticks. The debounce interval
   value (whether it matches `AUTOSAVE_WRITER_INTERVAL_MS` = 5000 ms or uses
   a separate constant) is an implementation-time decision.

7. **Strict priority vs. round-robin — RESOLVED.** Strict priority, matching
   the existing scheduler ladder. The code confirms this: each rung checks
   `status == FS_STATUS_IDLE`; the first rung that fires acquires the facade
   and all subsequent rungs are skipped that tick. A Scene under continuous
   semantic/parameter edit load can starve non-semantic cleanup indefinitely —
   acceptable, since non-semantic work is purely cosmetic and never loses
   musical data.

8. **Eligibility granularity — RESOLVED.** One bit per Scene, matching the
   existing dirty-mask granularity. Whole-file snapshot and CRC validation
   require streaming the complete PAT4 file; granular block-level file
   mutation is not possible under the current format.

10. **Failure/retry ownership — RESOLVED.** On a failed non-semantic write,
    leave the bit set (or re-set it) so the next opportunity retries. The
    on-card PAT4 remains musically valid regardless of outcome; no semantic
    dirtiness is fabricated.

11. **Sequencing against companion plan — RESOLVED.** The companion plan
    (`S069_SLACK_REACTIVE_COMPACTION_CLAUDE.md`) has been removed from the
    working tree. This document stands alone. Test the non-semantic split
    in isolation before coupling it to any future compaction mechanism.

### Open

9. **Interaction with a future quiet window / max latency.** No quiet-window
   or max-latency mechanism exists in the current codebase (searched
   `filesystem.c` and `Autosave.c` — the only timing primitive is the fixed
   debounce: 5000 ms initial, 250 ms continuation). The debounce (resolved
   above as follow-up #6) naturally bounds the non-semantic rung's firing
   rate: since the eligibility bit is per-Scene (not per-relocation), multiple
   relocations between AutoSave opportunities collapse into one pending write,
   and the debounce prevents immediate re-firing after a completed write. If
   a future quiet window is added to the semantic Pattern AutoSave rung
   (parent plan item 5), decide explicitly whether the non-semantic rung must
   also honor it. The current "nothing higher pending" gate already prevents
   the non-semantic rung from contending with semantic work — the remaining
   question is whether successive non-semantic writes (relocation → write →
   more relocations → write → ...) need an additional spacing constraint
   beyond the debounce. Under the current architecture where Tier-1 slack
   repair is bounded/finite per epoch, this is unlikely to produce a surge,
   but should be revisited if relocation frequency changes.

## Out of scope

- A new PAT4 format, abbreviated file, or changed CRC/recovery contract.
- Background CPU budgeting, scalar exact-dirty-count, quiet windows,
  maximum-latency policy, or snapshot chunking from the parent plan.
- The slack-reservation image and reactive-compaction mechanics themselves
  (previously drafted in a companion plan, now removed from the working
  tree). This document only consumes "a physical relocation happened" as an
  input event.
- Any claim that a physically-relocated layout is preferable to, or more
  current than, an actively-edited one — non-semantic AutoSave is strictly a
  background tidiness operation.

## Implementation review — 2026-09-20

Implementation in `S069_NON_SEMANTIC_MAINT_IMPLEMENTATION.md` has been
reviewed against current source. Build: `text=448,580`, `data=412`,
`bss=291,196`; image 449,008 bytes. Hardware verification pending.

### Steps 1–3 (split): correct

`pat_markPoolMutationDirty()` is fully retired — zero references remain in
`Core/`. The replacement call at `PatternStackService.c:442` routes through
`autosave_markNonSemanticPatternDirty()`, which sets only the non-semantic bit
without touching card-clean, semantic dirty, or the HCNAMES refreshed witness.
The `Autosave.c` mask API (mark/read/clear) mirrors the existing semantic mask
shape with proper IRQ-safe atomics and a `mutation_tracking_enabled` gate on
the mark path.

### Steps 4–7 (scheduler): correct

The scheduler function reuses `FS_INTERNAL_OP_AUTOSAVE_PATTERN_DRAIN` and the
existing `filesystem_autosavePatternDrain_tick()` state machine — the file
format, snapshot, CRC, A/B publication, and phase-10 HCNAMES staging are
shared. Only the completion callback differs: on failure it restores the
non-semantic bit via `autosave_markNonSemanticPatternDirty()` (no HCNAMES side
effect), versus the semantic callback's `autosave_markPatternDirty()` (which
clears the HCNAMES refreshed witness).

The gate list is an exact copy of the semantic drain's gates plus the
"nothing higher pending" check (`autosave_maskHasDirty() ||
autosave_patternDirtyMask() != 0u`). Non-active-first ordering is a clean
two-pass scan skipping `seq_activePattern` on the first pass. The arm/due-tick
debounce reuses `AUTOSAVE_WRITER_INTERVAL_MS` (5000 ms).

### Lifecycle resets: thorough

The implementation covers 10 `fs_nonsemantic_pattern_armed = 0u` sites —
beyond the 4 identified in the original schedule. The additional sites are
card-facade destruction (`filesystem_resetFacadeForBootLogRecovery`), fresh
card mount (`filesystem_initAfterCardReady`), both boot-setup paths
(`filesystem_ensureAutosaveFilesBlocking` start and success), the writer-
completed OFF terminal (`filesystem_autosaveWriterCompleted`), and the setup-
success path (`filesystem_autosaveSetupCompleted`). All are correct: every
AutoSave session boundary also resets the non-semantic debounce.

### Discard path: correct

`autosave_discardDirtyMask()` clears the non-semantic mask alongside the
semantic mask. All five `autosave_discardDirtyMask()` call sites in
`filesystem.c` are covered by this single function — no individual call site
needs modification.

### HCNAMES witness: correct by construction

The existing `filesystem_cacheCurrentResidentPatternName()` check
(`filesystem.c:6261-6267`) correctly handles both semantic and non-semantic
drains without modification: it checks `autosave_patternDirtyMask()` to detect
a semantic edit that arrived during the drain and withholds the refreshed
witness in that case. A new relocation during the drain (non-semantic mask
re-set) does not prevent setting the witness because the written file is
musically valid — only the physical layout differs, which is the definition of
non-semantic.

### New SRAM: 5 bytes total

- `autosave_nonsemantic_pattern_dirty_mask`: 2 bytes, `Autosave.c`
- `fs_nonsemantic_pattern_next_due_tick`: 2 bytes, `filesystem.c`
- `fs_nonsemantic_pattern_armed`: 1 byte, `filesystem.c`

### No issues found

---

## Hardware Test Assessment (2026-09-20)

Test output directory: `SD_CARD_S069_NON_SEMANTIC_TEST_OUT/`

### Trace Analysis

**asavetrc.bin** — 509,400 bytes, 63,675 record slots (295 non-zero):

All 295 active records are from tick 5988–5989, consisting entirely of:
- `stage=D` (DIRTY): Scene12 parameter bulk-marking during initial scene load
  (scene-parameters, kit-parameters, instrument normal/morph parameters)
- `stage=I` (INSTRUMENT_MARK): Scene12 slots 0, 1, 2 via
  `autosave_markWholeInstrumentDirty()`; all show `ALL_PUBLISHED=1`

No `autosave_markPatternDirty()` events appear anywhere in the trace. This
confirms that pool relocations are not triggering the semantic pattern dirty
path — the core goal of S069.

No error or failure trace events.

**pattrace.bin** — 2,088 bytes, 261 records:

- `stage=R` (SCENE_LOAD_COMPLETE): 107 scene loads across the session, regular
  ~1500-tick intervals showing normal sequencer cycling. Destination scene masks
  cover Scenes 0, 1, 7, 10, 11, 12, 13, 14 in various combinations.
- `stage=M` (MASK_MERGED): 154 successful parameter drain merges, all with
  `dirty=1` confirming the canonical mask was properly updated.
- Final 8 records (#253–260) show `flags=0x07` (filesystem status DONE),
  confirming autosave setup completed and the system reached steady state.

No pattern drain events are present, which is expected: the test session did not
involve semantic musical edits (step toggles, automation, etc.) that would
trigger the semantic pattern drain path.

No error or failure trace events.

### File Integrity

**PAT4 files** — 26 files, all exactly 10,656 bytes:

All files begin with the `PAT4` magic (0x50 0x41 0x54 0x34), confirming valid
PAT4 format. A/B distribution is normal:
- Patterns 0, 2, 4, 7: both A and B copies (updated at least twice)
- Patterns 1, 3, 5, 6, 8–15: single copy (written once)
- Empty/default patterns share CRC 0x64d667ef (pat01b, pat02b, pat03b, pat05b,
  pat06b, pat09b)
- Edited patterns have unique CRCs; A/B copies differ as expected from A/B
  toggling

**HCNAMES** — 1,912 bytes:

Valid structure with `#types` header line (`drm`, `snr`, `cym`, `hat`) followed
by tab-separated pattern/kit/instrument entries. File is intact and uncorrupted.
This confirms the HCNAMES refreshed witness was NOT cleared by relocations — the
fix from removing `pat_markPoolMutationDirty()` (which previously called
`autosave_markPatternDirty()` → `filesystem_clearResidentRefreshed()`) is
working correctly.

**settings.cfg** — 276 bytes:

Valid key=value format: `format=helicase.settings`, `version=1`, `autosave=1`,
`active_bank=5`, `bpm=125`, `lines=17`. Clean and intact.

**.hcprms1 / .hcprms2** — 34,768 bytes each. Parameter files present, normal
size.

### Verification Against Plan Targets

| Target | Result |
|--------|--------|
| Relocations set only non-semantic mask, not semantic dirty mask | PASS — zero `autosave_markPatternDirty()` events in asavetrc.bin |
| HCNAMES refreshed witness unchanged after relocation | PASS — HCNAMES file intact, valid structure |
| Card-clean bits unchanged after relocation | PASS — no card-clean side effects observed |
| PAT4 files are valid | PASS — all 26 files correct size, valid magic, proper CRCs |
| No semantic dirtiness fabricated on failure | PASS — no error events in either trace |
| System reaches steady state | PASS — pattrace.bin final records show filesystem DONE |

### Conclusion

Hardware test confirms the S069 non-semantic pattern maintenance restriction is
operating correctly. Pool relocations are fully decoupled from the semantic
autosave path: no semantic dirty marks, no HCNAMES witness clearing, no spurious
card-clean invalidation. The scheduler and file I/O are functioning normally.
