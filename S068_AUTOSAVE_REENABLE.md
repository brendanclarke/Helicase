# S068 AutoSave OFF-to-ON assessment

## Verdict

The current OFF-to-ON implementation does re-establish both scalar-parameter
and Pattern AutoSave. I do not find the previously suspected “ON never rearms
the dirty state” defect in the current source. There are, however, two reasons
it can look non-working:

1. Re-enable deliberately marks the **entire resident Bank**, then scalar work
   and diagnostic traces run ahead of Pattern files. A changed Pattern can wait
   behind a large scalar backlog and lower-index Pattern Scenes.
2. Pattern pool maintenance currently creates continual layout-only dirty
   events. That can keep Pattern AutoSave busy indefinitely and conceal the one
   convergence write the user is waiting for.

There is also one genuine liveness weakness: a single failure of the runtime
“ensure AutoSave files” transaction latches setup failed and does not retry
until another explicit OFF-to-ON transition or Bank lifecycle change. The UI
does not report that state.

## Current transition, step by step

`filesystem_setAutosaveEnabled(0)`:

- disables mutation tracking;
- revokes setup, recovery, armed-writer, boot-ready, and cached-winner state;
- discards the scalar dirty mask and Pattern dirty mask immediately, or defers
  that discard until an already admitted transaction reaches its safe terminal
  boundary.

`filesystem_setAutosaveEnabled(1)`:

- clears the deferred-discard and setup-failure latches;
- queues `FS_INTERNAL_OP_ENSURE_AUTOSAVE_FILES` when runtime storage is ready
  and a resident Bank exists.

On successful asynchronous ensure, `filesystem_autosaveSetupCompleted()`:

- enables mutation tracking;
- sets writer/recovery authorization;
- calls `autosave_markResidentBankDirty()`.

That last call marks every scalar field in the Bank and every present Scene,
and uses `autosave_markSceneWithPatternDirty()` so every present Scene's
separate Pattern bit is also set. This correctly captures edits made while
tracking was OFF; it does not rely on post-toggle edits to wake the writer.

The user commit path is connected: the changed Global-menu cell calls
`filesystem_setAutosaveEnabled()` after updating `PAR_AUTOSAVE_ENABLED` and
marking `settings.cfg` dirty. Continuous playback is not an admission gate for
either scheduler. Pattern AutoSave pauses for record/erase activity, but not
merely because the transport is running.

## Evidence from `SD_CARD_PAT_ASSIGN_BUG`

This fixture does not contain an explicit OFF/ON trace stage, so it cannot
prove that a particular toggle occurred. It does prove that both writer types
were live at the end of the captured use:

- `settings.cfg` contains `autosave=1`.
- `.hcprms1` and `.hcprms2` are both 34,768-byte committed records at
  generations 141 and 142. Their 3,856-byte masks are clear.
- The tail of `asavetrc.bin` contains a complete successful scalar lifecycle:
  `S -> A -> V -> M -> C -> P -> T`, ending in generation 142. The trace has no
  operation-error (`E`), writer-suppression (`W`), trace-failure (`F`), or
  phase-stall (`X`) record.
- Pattern A/B files exist for the resident Scenes. Scene 8 reaches generations
  538/539, Scene 10 reaches 324, and Scene 1 reaches 54/55. These generations
  are far beyond boot creation and prove repeated runtime Pattern drains.

The very high Pattern generations are evidence of excess work, not healthy
edit volume: the Pattern trace shows thousands of maintenance relocations, and
every relocation currently marks the Pattern dirty.

## Why re-enable can appear not to work

### Scalar parameters

The first writer attempt is intentionally delayed by five seconds. Re-enable
marks a full Bank, while one scalar transaction captures at most 1,536 live
payload bytes. Remaining work continues in later transactions with a 250 ms
inter-transaction delay and complete validation/copy/CRC/SD work each time.
In a populated Bank this is observably longer than the nominal five seconds.

The DEV build also flushes AutoSaveTrace and PatternTrace before scalar
AutoSave whenever those rings are pending. A full-Bank dirty mark produces a
large `D`-record burst, so the diagnostic designed to observe re-enable can
itself delay its writer admission.

### Patterns

Pattern AutoSave is the final background claimant, after settings, both trace
writers, and scalar AutoSave. It selects the lowest dirty Scene bit first and
writes a complete 10,656-byte PAT4 file plus HCNAMES publication. Thus a change
to Scene 8 after re-enable can wait for Scenes 0-7 even when Scene 8 is the
active one.

The current relocation service then re-dirties the active Pattern on every
physical block move. In the supplied fixture Scene 8 was moved 5,315 times and
saved through generation 539. The desired user edit is being saved, but its
file is immediately classified stale again for a layout-only change. This is
the strongest explanation for the Pattern-specific observation.

## Genuine failure mode

If `FS_INTERNAL_OP_ENSURE_AUTOSAVE_FILES` returns an error during re-enable,
`filesystem_autosaveSetupCompleted()` sets `fs_autosave_setup_failed`, leaves
tracking disabled, clears writer authorization, and suppresses automatic
retry. The settings value can still show ON. Only a later OFF/ON transition or
Bank lifecycle reset clears the latch. Without an enable/setup trace stage or
UI status, that state is indistinguishable from a scheduler that never woke.

This behavior is defensible as protection from a tight SD-error retry loop,
but it needs a visible error/retry policy. A bounded delayed retry after card
ready, or an explicit “AutoSave error” status with a retry action, would be
safer than silent permanent inactivity.

## Recommended implementation/test plan

1. Add lifecycle trace stages for policy OFF, policy ON, setup admitted, setup
   success/failure, tracking enabled, full-Bank seed complete, scalar dirty
   count, and Pattern dirty mask. Do not use per-byte `D` records for this
   summary.
2. Run the `AS-ENABLE` matrix from
   `AUTOSAVE_TEST_CASES_LOAD_SAVE_REVISIONS.md` with a small known change in a
   low and high Scene. Hash scalar payloads and PAT4 semantic content before
   OFF, after OFF edits, after ON/setup, after every writer generation, and
   after reboot.
3. Verify ON during playback, recording, Load/Save suppression, an active
   scalar transaction, and an active Pattern transaction separately.
4. Fix the layout-only Pattern dirty feedback described in
   `S068_ATS_PAT_BOUNDED_CPU.md`, then repeat. This is necessary to measure
   convergence rather than perpetual housekeeping.
5. Expose or automatically retry `fs_autosave_setup_failed` with a bounded
   cadence. Preserve the rule that no retry can start while Load/Save owns the
   facade.
6. Consider prioritizing the active Scene's Pattern after re-enable while
   retaining a rotating cursor for fairness; this changes latency, not data or
   atomicity semantics.

No `Core/` code was changed in this assessment.
