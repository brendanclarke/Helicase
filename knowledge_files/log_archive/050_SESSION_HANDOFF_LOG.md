# Session 050 Handoff Log — Scene-Load Trace and AutoSave Publication

**Project**: LXR-02 firmware port (STM32F765VIH6)  
**Session goal**: Determine why a successful root Scene Load changed resident
parameters and its Scene HCNAMES row but published neither its trace nor its
AutoSave record, make the smallest correction in the real load path, and test
it on hardware.

## End of session

```
DATE: 2026-08-16
SESSION GOAL: Restore observable and durable trace/AutoSave publication after
  a root Scene Load without adding a parallel load path.
COMPLETED: Located and corrected the terminal filesystem-facade ownership leak
  in the existing root Scene/Bank Load final-index callback. The field fixture
  confirms the Scene completion trace and AutoSave transaction now publish.
  Session specifications, SRAM figures, and diagnostic contracts were updated.
VERIFIED ON HARDWARE: Yes. Loading root Scene 024 (SeaWaked) into resident
  Scene 15, then leaving Load for Voice, produced R/D/I/L/F/W and A/V/M/C/P/T
  trace evidence and advanced the valid HCPRMS winner from generation 5 to 6.

CHANGES THIS SESSION:
- Core/Menu/menu.c: final Scene/Bank index callback now snapshots terminal
  result and calls filesystem_ack() before Menu teardown; Scene completion also
  accumulates its Kit-family HCNAMES dirty mask.
- Core/Bank/Scene/Preset/presetManager.c: Scene-load R witness and one-second
  bounded non-quiet voice-apply recovery are present.
- Core/Hardware/SD/filesystem.c and Core/Bank/Scene/AutosaveTrace.h:
  logging-only R/W/F/G evidence stages and bounded gate/loss witnesses are
  present.
- config.h and AutosaveTrace/filesystem trace path: temporary approved
  2,048-record diagnostic ring and 64-record/512-byte flush batching are
  present.
- knowledge_files/specification_reference/: AutoSave, filesystem, module,
  development-mode, and SRAM authority refreshed with the verified result.
- knowledge_files/log_archive/: Session 050 index and handoff added.

KNOWN ISSUES INTRODUCED: None intentionally. Root Scene Load now leaves an
  accumulated HCNAMES Kit-family dirty mask that is not flushed on a Scene or
  Bank page exit because the pre-existing exit predicate recognizes only
  Instrument/Kit/KitMrp sessions. This exposed, rather than caused, the
  remaining identity-registration defect.
KNOWN ISSUES RESOLVED: The terminal Scene/Bank Load filesystem facade no
  longer remains FS_STATUS_DONE after the final read-only index restore. This
  releases the idle-only trace flush and, after page exit/debounce, the
  AutoSave writer.

NEXT SESSION RECOMMENDED GOAL: Repair the one existing Menu family-exit
  predicate so a nonzero resident-name dirty mask is flushed after root Scene
  and Bank Load; then independently re-test Bank Load persistence/restore
  identity before making any broader AutoSave change.
BLOCKERS: Hardware coverage still lacks a Bank regression and a deliberate
  continuously-playing/non-quiet-voice Scene Load. The user is updating
  tools/decode_bootlog.py separately; do not inspect or rely on its currently
  transient worktree state until they say it is complete.

CRITICAL REMINDERS FOR NEXT SESSION:
- Preserve the one-line filesystem_ack() placement: after index_ok captures
  DONE/ERROR, before menu_finishLoadSaveCommand(). Do not move Scene marking
  into the filesystem commit stage or add a second publication path.
- Trace may flush after command finalization while still on Load; the AutoSave
  writer must remain suppressed until the user exits Load/Save because that
  page owns the shared name cache.
- The temporary AUTOSAVE_TRACE_RECORD_COUNT is 2048, not the normal default
  64. Keep it for the remaining diagnosis; restore it only after an explicit
  follow-up decision and fresh memory/build checks.
- Do not treat a changed Scene HCNAMES row as proof that the embedded Kit and
  Instrument identity rows were registered, or that HCPRMS persisted.
```

## Source-verified result

The production defect was not the Scene payload loader, HCNAMES Scene-row
write, mutation marker, or trace producer. The real explicit root Scene/Bank
command ends with a read-only reload of the selected `/.hcindex`:

```text
preset_loadSceneForScenes()
  -> filesystem_requestLoadSceneForScenes()
  -> filesystem_loadSceneDirectory_tick()
  -> successful Scene/Pattern/Effect/HCNAMES completion
  -> on_scene_load_complete()            (Scene marker and R witness)
  -> shared runtime Scene apply
  -> menu_requestLoadCommandFinalIndexRestore()
  -> filesystem_requestReloadLibraryIndex()
  -> filesystem_complete(DONE)
  -> menu_loadCommandFinalIndexComplete()
```

`filesystem_complete()` publishes `FS_STATUS_DONE` before invoking the direct
Menu callback. The callback originally sampled the result, cleared the visible
command, and repainted, but did not consume the terminal facade state.
`filesystem_tick()` admits both the diagnostic trace append and the AutoSave
writer only while that facade is `FS_STATUS_IDLE`. Thus the user could see the
loaded parameters and a durable Scene HCNAMES row, but the completed command
left the facade at DONE forever; page exit could not repair it.

The implemented targeted correction in
`menu_loadCommandFinalIndexComplete()` is exactly:

```c
uint8_t index_ok = (uint8_t)(filesystem_status() == FS_STATUS_DONE);
filesystem_ack();
```

It is intentionally unconditional so both DONE and ERROR release the shared
facade. It is after the status snapshot so the existing error path still has
the right result, and before `menu_finishLoadSaveCommand()` so the next idle
filesystem pass can publish. It adds no state, new operation, scheduler,
callback, or load side path. The existing
`menu_residentNameScratchFlushComplete()` direct callback establishes the same
snapshot-then-ack ownership rule.

## Chronology, failed attempts, and diagnosis

### Initial falsification fixtures

Two pre-fix hardware captures established the symptom:

- `SD_CARD_NOEXIT/`: root Scene Load was allowed to finish and the card was
  copied before leaving the Load page.
- `SD_CARD_NOPLAY/`: root Scene Load was followed by an exit to Voice mode
  before the card was copied.

In both captures the target Scene parameters changed and `/.hcnames` changed
its Scene row, while `.hcprms1`/`.hcprms2` stayed at generations 5/4 with no
new Scene payload. The NOPLAY trace suffix did contain unrelated boot/Kit
activity and a writer-page `W` observation, but no root Scene `R`, no
Scene-kind `L`, and no following writer transaction. This distinction matters:
absence from the card did not prove that the Scene callback failed; it proved
the RAM trace was never admitted to the filesystem facade before power-off.

The parameter and Scene-row changes were expected because they occur before
the terminal read-only index reload. The final callback's missing
acknowledgement was therefore the one boundary that explains all observations.

### Planning hypotheses retained but not accepted as fixes

`SCENE_LOAD_ASAVE_TRACE_RESTORE.md` proposed three independent aids:

1. accumulate the root Scene's Kit-family HCNAMES name-session mask;
2. bound a non-quiet post-load voice apply after 1,000 foreground passes;
3. add R/W/F/G trace witnesses to distinguish callback, command, page, append,
   and ring-overflow failures.

Those changes are present in the current source and are useful diagnostics or
progress safeguards. They were not the cause of the original no-record result.
In particular, moving or duplicating `autosave_markSceneWithoutPatternDirty()`
inside `filesystem_commitSceneStage()` was rejected: it would publish a
partial Scene before later Pattern/effect/HCNAMES success and duplicate the
correct Preset completion ownership.

The initial `SESSION_050_HANDOFF_PROGRESS.md` reported no local embedded
toolchain verification. Later source notes record a clean current logging-on
build/package (`text=376,596`, `data=396`, `bss=95,188`, wrapped image
377,008 B), and the copied card image has the same SHA-256 as the packaged
build. Do not repeat the obsolete “unbuilt” status from that early progress
note.

### Temporary diagnostic parameters

The normal trace-ring default is 64 eight-byte records (512 B). The current
approved diagnostic configuration in `config.h` is:

```c
#define AUTOSAVE_TRACE_RECORD_COUNT_DEFAULT 64u
#define AUTOSAVE_TRACE_RECORD_COUNT 2048u /* TEMPORARY approved expansion */
```

That uses 16,384 B of logging-only normal SRAM1 for the ring. Trace flushing
serializes at most the existing staging-buffer capacity, 64 records/512 B, per
filesystem operation and schedules another batch while work remains. It is
not a writer debounce or persistence-policy change. Supporting logging-only
cursor/cadence/witness state is 12 B; the non-logging Scene apply force counter
is 2 B. Current linked totals are documented in `SRAM_MANIFEST.md`.

Do not revert the effective 2048 value merely because this fixture succeeded:
the additional capacity remains needed to diagnose the outstanding Scene
identity and Bank paths. Conversely, do not let it silently become a permanent
production allocation; the default/revert point remains explicit.

## Hardware evidence — successful root Scene Load

The successful field fixture loaded root Scene slot 024 `SeaWaked` into
resident Scene 15, exited Load to Voice, then copied `SD_CARD/`.

- `SD_CARD/LXRV2_lxr02.img` and `build/LXRV2_lxr02.img` are both 377,008 B
  with SHA-256
  `6093925993e2e2e624f3aee2837d2f556282e5ea5a4171dabe39137955368f3f`.
- `asavetrc.bin` is 55,080 B (6,885 records). Its observed counts include
  `R=1`, `F=1`, `W=2`, `A/V/M/C/P/T=4` each, `L=206`, and no required Scene
  completion witness is missing.
- The relevant tail proves the ordering:

  ```text
  R flags=01 tick=14187 value=00008000    Scene callback, DONE, Scene 15
  L flags=01 tick=14187 value=0000003c    whole Kit, Scene 15
  L flags=01 tick=14187 value=0000003d    whole Scene, Scene 15
  F flags=01 tick=14187 value=000001f9    command-active trace deferment
  W flags=01 tick=14187 value=00001aa7    Load-page writer suppression
  A/V/M/C/P/T                             admitted, validated, merged,
                                          captured, published, terminal
  P flags=01 tick=17672 value=00000006    generation 6
  T flags=01 tick=17687 value=00000000    successful terminal writer state
  ```

- `.hcprms1` remains valid generation 5; `.hcprms2` is valid generation 6
  (`HCPR`, version 1, commit `a5`, probe 5). The writer captured 499 unique
  bytes: three existing Bank bytes plus the Scene-15 496-byte scope (40 Scene
  settings, two Kit values, and 454 Instrument bytes).
- The Scene row is durable: `/.hcnames` row 17 is `SeaWaked<TAB>024`.

This is direct hardware confirmation of the publication repair. It does not
prove Pattern or live Effect persistence; those remain excluded from the v1
AutoSave scope. It also does not exercise a deliberately continuous/non-quiet
voice long enough to prove the new one-second forced apply branch.

## Remaining work and exact boundaries

### Scene/Bank embedded Kit-family HCNAMES registration

The source includes the intended accumulation call in the
`PRESET_OP_SCENE_LOAD` Menu completion branch:

```c
menu_refreshResidentNameScratchKit(preset_getKitRequestSceneMask());
```

The successful card proves it is insufficient by itself. `menu_switchPage()`
currently calls `menu_endResidentNameScratchSession()` on a physical Load/Save
exit only when the session is Instrument, Kit, or KitMrp. A root Scene/Bank
session has a nonzero `menu_residentNameDirtySceneMask` but does not meet that
predicate, so the existing one-shot
`filesystem_requestUpdateResidentKitNames()` transaction is never requested.

The field result is concrete: Scene 15 is `SeaWaked<TAB>024`, but its Kit row
and six Instrument rows remain the previous `Pop`/`barfd*` identities. Next
session should widen the existing family-exit condition based on the nonzero
dirty mask (or an equivalently precise existing-session condition), then use
the existing writer once. Do not add a second HCNAMES writer or a Scene-load
side-path flush.

The current HCNAMES source grammar remains authoritative: `-` means inherit.
The later working-plan suggestion to replace embedded rows with an empty source
field was not implemented or validated and must not be treated as a completed
rule. Resolve any source-token redesign separately from the exit predicate.

### Bank Load remains unverified

Session 049's Bank fixture still recorded valid bases but disabled tracking in
its `I` markers, left autosave payload unchanged, and left `settings.cfg`
`active_bank`/the hidden-record restore slot stale after Bank `005 Full`.
Current source contains the selective Bank completion marker, but Session 050
did not run a Bank regression after the final-index acknowledgement fix. Do
not claim Bank persistence or restore identity is fixed. Keep it isolated from
the Scene HCNAMES repair.

### Other retained limitations

- Pattern and live Effect are intentionally outside AutoSave v1.
- InstrumentMrp's nested `kit` row remains a separate blank-label/snapshot UI
  defect.
- Native exact recursive deletion for overwrite Save remains required; do not
  revive old-folder renames as a workaround.
- Runtime Bank Load can still switch the playing Scene; preserve boot behavior
  while addressing that later.
- The intermittent `ASENSURE` boot setup timeout remains unresolved; its
  logging-only capsule is forensic evidence, not a retry or timing-policy fix.

## Documentation close-out

The specification reference set is updated to make the session independent of
the disposable planning files:

- `AUTOSAVE.md`: root Scene terminal marker ownership, page/facade contract,
  and the generation-6 hardware acceptance.
- `FILESYSTEM_SPEC.md`: final root Scene/Bank index callback acknowledgement,
  and the explicitly deferred embedded Kit/Instrument HCNAMES registration.
- `MODULE_INTERCHANGE_SPEC.md`: direct callback snapshot/ack rule and actual
  Scene/Bank terminal ownership.
- `DEV_MODES.md`: 2048-record diagnostic configuration, R/W/F/G contract,
  and field trace chain.
- `SRAM_MANIFEST.md`: current linked section totals, 16,384-B trace ring,
  12-B logging support, and 2-B Scene-apply bound.

`tools/decode_bootlog.py` is being updated independently by the user. It was
not inspected or edited in this session. The next session must check its final
state and update only the relevant bootlog-tool documentation if its schema or
usage changed; do not infer that from its transient deleted/modified status in
this worktree.

The disposable root documents examined for this closeout are planning and
evidence records, not future authority: `SESSION_050_HANDOFF_PROGRESS.md`,
`SCENE_LOAD_ASAVE_TRACE_RESTORE.md`, `SCENE_LOAD_RECORDS_FIX.md`,
`AUTOSAVE_WRITE_LOAD_OPERATIONS_COMPLETE.md`, and `AUTOSAVE_REFACTOR.md`.
This handoff and the specification files preserve their verified facts before
the user deletes those working documents.
