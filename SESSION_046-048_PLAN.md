# Sessions 046–047 Plan — Closed; Deferred Work Carried to Session 048

## Closeout boundary

Session 046 is closed on 2026-08-10 at rollback commit `c9807fa` (`autosave
trace logger working, pre steps 2 and 3 implementation`). The authoritative
verbose record is `knowledge_files/log_archive/046_SESSION_HANDOFF_LOG.md`.

Later Session 046 firmware experiments were reset. Do not infer current source
behavior from a later SD-card fixture or from an untracked plan.

## Completed in Session 046

### 1. Baseline reconciliation

- Session 045 was consolidated into a deletion-safe post-mortem and handoff.
- Current source/build state was checked against the accepted scalar AutoSave
  architecture rather than restored from `*.failed` files.
- Development modes were reduced to the binding distinction:
  `DEV_MODE_DIAGNOSTIC` is screen-only; `DEV_MODE_LOGGING` is file-only.

### 2. Boot filesystem localization and failure transparency

- The timeout remains ten seconds per boot operation. A timeout means real
  non-progress because normal boot, including Bank Load, completes well inside
  that interval.
- Kit quarantine and Bank Load now retain detailed non-rearming substep labels.
- Kit I/O abort is distinct from malformed content and cannot authorize
  quarantine rename.
- Boot index/load request failures are consumed before Menu acknowledgement or
  empty-library fallback can hide them.
- Normal hardware smoke testing passed; the intermittent hang did not recur
  with a more specific detail code.

### 3. HCNAMES singleton correction

- All HCNAMES access uses one case-insensitive singleton match policy.
- Every create-capable HCNAMES path requires a complete root absence proof.
- A NULL read open, duplicate match, or scan/open/close/FAT error remains an
  error and cannot authorize creation.
- Hardware retesting did not reproduce the duplicate HCNAMES entry.

### 4. AutoSave Step-1 observability

- The approved 520-byte `DEV_MODE_LOGGING`-only lifecycle trace is implemented.
- The first background append defect was corrected: the trace writer now
  acknowledges its terminal facade status through its completion callback.
- `D/S/A/V/M/C/P/T` records can distinguish dirty production, scheduling,
  admission, validation, mask merge, capture, publication, and completion.

### 5. Phase 1 scalar acceptance

This work is complete and must not be reopened as a vague matrix:

- Scene values were tested.
- Kit and Instrument values were tested.
- MIDI channel/note are Scene-owned and were covered by Scene testing.
- There are no separate user-editable Bank scalar values in the current UI.

The acceptance does not claim whole-object Load/Save/copy publication, Pattern
or Effect persistence, or power-cut coverage.

### 6. Semantics settled

- Bank slot/name and other Bank metadata are payload, not record-selection
  identity.
- A current-format record can be structurally valid but incomplete as a
  resident-Bank snapshot; initial records contain zero parameter payload.
- Partial Bank operations do not imply replacement of all sixteen Scenes.

This settles the design rule, not the current validator implementation:
`c9807fa` still performs a live-Bank match during winner selection. Reconcile
that boundary before whole-object Bank session publication.

## Reverted Session 046 work

The following is explicitly not in `c9807fa`:

- generic one-millisecond filesystem pacing;
- byte-bounded CRC generation;
- runtime Bank Load active-Scene preservation;
- later settings-notification changes;
- unified `/devlog.bin` output or duplicate-safe log publication.

The one-millisecond pacing experiment caused severe boot and Load/Save
slowdowns and delayed rather than removed audio glitches. The unified-log
experiment caused a boot timeout, no durable `devlog.bin`, and a partial
32,768-byte `.hcprms2`. Neither may be restored mechanically.

## Session 047 closeout — completed and deferred

The authoritative detailed record is
`knowledge_files/log_archive/047_SESSION_HANDOFF_LOG.md`. The removable
working plans `SETTINGS_BANK_LOAD_REIMPLEMENT.md` and
`HCPRMS_BOOTLOCK_DIAGNOSIS.md` are preserved there and in
`AUTOSAVE_PHASE2_PLAN.md`.

### Completed

1. **Bounded CRC:** every AutoSave CRC path now advances through a uniform
   128-byte retained work budget: initial A/B creation, invalid-pair recovery,
   candidate validation, and transformed-copy generation. No generic pacing,
   blind delay, record-sized buffer, or permanent CRC allocation was added.
2. **Missing-pair selector repair:** boot creation now opens the saved A/B
   selector rather than the scratch field later reused as the CRC accumulator.
   This fixed the deterministic “only `.hcprms2` exists” outcome.
3. **Settings:** the user re-tested the existing one-second debounced
   `settings.cfg` writer and found it correct. No settings source change was
   made.
4. **Trace arbitration and audio test:** the optional lifecycle trace does not
   open `asavetrc.bin` while a Load/Save page owns the facade. Repeated
   playback-time loads of `000 Full` and `013 LoadTst`, plus overwrites of 024
   and 009, produced no audible glitch.
5. **Intermittent 32 KiB capture:** one initial-B write stopped at 32,768 bytes
   and the real ten-second `ASENSURE` deadline fired. A logging-only capsule
   now records application progress plus read-only AsyncFATFS/cache/allocator
   and SD-transport snapshots before ordinary boot recovery. The 64-byte
   capsule follows the eight-byte token only for that timeout; it changes no
   allocator, retry, timing, or recovery behavior. Later boots had two full
   34,768-byte committed records and no bootlog, so the lower-level failure is
   not reproduced or diagnosed.

### Deferred to Session 048 or later

1. **Overwrite Save stale folder:** Bank, root Scene, and Kit overwrite may
   leave the old directory. Repair `afatfs_deleteTree()` recursively and test
   it with populated fixtures. Do not revive the rejected `old*` rename or
   boot-cleanup feature.
2. **Runtime Bank Load playing-Scene switch:** preserve the pre-load active
   Scene only after ordinary Bank Load/Save behavior is stable. Boot must keep
   restoring the Bank's saved default Scene.
3. **Intermittent 32 KiB boot stop:** do not speculate on a FAT/SD repair. If
   it recurs, preserve `SD_CARD/bootlog.bin`, decode the 72-byte ASENSURE
   capsule, and use its state to choose one lower-layer investigation.
4. **AutoSave Phase 2:** preserve the completed trace/scalar/settings results,
   then resume only the written whole-object sequence: Whole Instrument,
   Whole Kit, root Scene without Pattern, selective Bank, Morph projection;
   retry Load/Save exclusion last and in isolation.
5. **Host log conversion:** only after AutoSave and logging formats settle,
   add a read-only `/tools/` converter from copied `SD_CARD/` outputs to a
   dated root `dev_log_date.txt`.
