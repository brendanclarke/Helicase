# Sessions 046–048 Closeout — Archived; Session 049 Carryover Identified

## Closeout boundary

Sessions 046–048 are closed. This historical working plan is now superseded
by `knowledge_files/log_archive/048_SESSION_HANDOFF_LOG.md` for Session 048
and `AUTOSAVE_PHASE2_PLAN.md` for the still-active later Phase-2 order. It may
be deleted after those durable records are retained.

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
   retry Load/Save exclusion last and in isolation. Session 048 completed the
   normal root-Instrument and InstrumentMrp portions; Kit, Scene, and Bank
   Load publication now carry to Session 049.
5. **Host log conversion:** only after AutoSave and logging formats settle,
   add a read-only `/tools/` converter from copied `SD_CARD/` outputs to a
   dated root `dev_log_date.txt`.

## Session 048 closeout — completed

### HCNAMES source authority

- `/.hcnames` is now the sole resident identity/provenance authority. Its 129
  rows use `name<TAB>source`; `-`, `?`, `000..999`, and `@` encode inheritance,
  unknown, direct numbered source, and direct root-Instrument stem.
- The approved 258-byte filesystem-owned source register replaced the retired
  32-byte SceneData source array. Source propagation is staged at successful
  Instrument, Kit, Scene, and Bank boundaries and remains independent of the
  later AutoSave reader.
- `settings.cfg` now contains only its 17 current allowlisted settings,
  including `active_bank` and `autosave`; legacy `scene_source_NN` keys are
  accepted and ignored during migration. It is not a source authority.

### Instrument Load AutoSave Phase-2 acceptance

- A root-pool Instrument commit immediately marks its three type bytes, all
  owned Normal endpoints, and all Morphable Morph endpoints. The hidden typed
  `kit` restore, staging, cancellation, and invalid operation remain
  non-marking.
- A compatible InstrumentMrp commit immediately marks only the destination's
  Morphable Morph endpoints. It changes neither type nor Normal values,
  HCNAMES identity/source, or AutoSave name storage.
- Menu takes the immutable filesystem request-local temporary flag before
  starting the shared Preset apply path; it does not use a mutable browser or
  temporary-session latch as load provenance. A one-byte approved queued page
  switch ensures the ordinary physical exit begins after a busy Load/Save
  operation releases its owner.
- The direct HCNAMES flush callback now acknowledges its terminal filesystem
  status before UI follow-up. This releases the facade to `IDLE`, allowing the
  autonomous trace and writer schedulers to run after the ordinary exit.

### Hardware evidence and Session 049 boundary

- Normal root Instrument hardware fixture: `J=0x03`, `I=0x07`, and packed
  `0x004c4c05` prove Scene 5/slot 0 accepted all 76 expected bytes. The trace
  reached successful `A/V/M/C/P/T`; generation 2 persisted the `brezeld1.drm`
  type/Normal/Morph payload while its HCPRMS name remained intentionally old.
- Combined fixture: generation 3 in `.hcprms1` contains Drum 1 Normal/Morph
  values from `brezeld3.drm`; Drum 2's type/name/Normal bytes are unchanged
  and only its Morph allocation matches `casiopd3.drm`. The final writer trace
  reached successful publication. The 64-entry diagnostic ring wrapped the
  prior root `I/J` behind two Drum-2 Morph dirty sweeps, so the durable A/B
  byte comparison—not a single-command trace count—is the acceptance proof.
- Session 049 is limited to the next three distinct owner boundaries: normal
  Kit Load, root Scene Load without Pattern, and selective Bank Load using the
  actual committed child mask. Test each separately with copied records and
  trace. Do not add a writer exclusion, Boot reader, Save-side mutation mark,
  KitMrp expansion, or unrelated Bank fix in the same pass.
- Deferred UI bug: InstrumentMrp's `kit` row is blank. It needs a Morph-only
  temporary snapshot plus the current HCNAMES name, without replacing the
  slot's Normal/type/source state. `SCOPING_TARGETS.md` is the backlog owner.
