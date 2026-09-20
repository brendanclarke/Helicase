# S068 pre-feature general fitness agenda

## Scope

Dynamic Pattern storage and its service are now implemented, so the explicit
“after Pattern storage” backlog is eligible. The next work should be a bounded
correctness/refactor pass before Phase 5/6 feature expansion. This agenda does
not reopen items already resolved in `SCOPING_TARGETS.md`, and it does not
recommend changing file formats without failing evidence.

## Gate 0 — correct the S068 defects and retest as one system

These changes interact and should be fixed before interpreting further CPU or
input tests:

1. Repair the lossy front-panel event/hold ownership described in
   `S068_PAT_ASSIGN_BUG.md`; then change the hold threshold to 200 ms.
2. Align the played-Pattern mirror during side-effect-free boot/runtime
   filesystem realignment (`S068_MISSING_CHASELIGHT.md`).
3. Remove Pattern maintenance's layout-only AutoSave dirtiness, eliminate the
   two clean-state full scans, and stop Tier 1/Tier 2 oscillation
   (`S068_ATS_PAT_BOUNDED_CPU.md`).
4. Add OFF/ON setup visibility and run the focused re-enable matrix from
   `S068_AUTOSAVE_REENABLE.md`.

These are prerequisites because event loss can make functional tests lie,
churn can make AutoSave latency appear unbounded, and the boot Pattern mirror
can make a correctly loaded Pattern look absent.

## Gate 1 — close the new Pattern persistence boundary

Section 5 of `AUTOSAVE_TEST_CASES_LOAD_SAVE_REVISIONS.md` was explicitly
deferred until Pattern storage existed. The storage choice is now resolved:
Pattern uses separate per-Scene PAT4 A/B files, so do not redesign HCPR to
contain Pattern bytes. What remains is validation:

- root Scene Save/Load round trips;
- partial Bank Save/Load without modifying unselected resident Patterns;
- boot restore from every active Scene;
- active, shown, pending, service-owned, and `menu_playedPattern` alignment;
- corrupt, missing, truncated, wrong-generation, and power-cut PAT4 cases;
- edits during Pattern snapshot/write, proving the later edit remains dirty;
- one-time initialization at every load/retry phase, proving no asynchronous
  retry clears an already committed Pattern;
- mixed AutoSave/HCNAMES/explicit Scene fixtures rerun now that Pattern rows
  and `@` provenance are live.

Build reproducible card fixtures and semantic Pattern comparators first. Do
not use FAT timestamps as evidence; use PAT4 generations, CRCs, decoded
addresses/blocks, HCNAMES rows, and runtime indices.

## Gate 2 — finish Phase 4 behavior that is currently incomplete

### 2.1 Dynamic Pattern copy operations

`pat_copyTrack()`, `pat_copyPattern()`, and `pat_copyBar()` are still deliberate
no-ops. Now that allocator/service ownership exists, implement Phase 4.5 before
building UI features that assume copy is trustworthy.

Plan:

- snapshot source semantic blocks, never copy raw destination-owned offsets;
- preflight required chunks and queue/bulk capacity;
- define atomicity for partial allocation failure (destination unchanged is
  preferred);
- preserve trigger bits, specials, and every automation entry;
- support overlap safely for same-Pattern bar/track copies;
- publish one semantic dirty event per completed destination scope;
- test under playback, Pattern maintenance, AutoSave snapshot, and service
  target handover.

### 2.2 Probability must gate the complete step

`seq_advanceTrackStep()` currently applies probability only around
`seq_triggerVoice()` and queues step automation afterward regardless of the
result. This is the Session 066 defect. Compute one step-play decision and use
it to gate both trigger and automation publication. Define erase/record and
trigger-inactive-but-automated semantics explicitly in the test before making
the branch broader.

### 2.3 Complete automation target runtime ownership

The dynamic pool stores the canonical nine-bit target, but
`seq_drainPendingAutomation()` currently applies only Instrument descriptor
targets and deliberately ignores Scene targets. `SCOPING_TARGETS.md` says
canonical descriptor/Scene targets are required before automation is
feature-complete. Add a typed Scene-target dispatcher with the same runtime
side effects as an ordinary Scene parameter edit; do not route it back through
generic Menu code. Test per-voice Morph, Scene decimation, and the track-7
choke/base-decay ownership rule.

### 2.4 Final LED state consolidation

Perform Phase 4.11 after the chaselight fix. The present base/blink/flash/pulse/
chase layers restore state independently and remain vulnerable to ordering
bugs. Give `ledHandler` one explicit render priority and fallback model,
including BAR1 and chase. Keep the public LED API stable and regression-test
mode changes, held automation, recording feedback, pulses over blinks, and
Scene changes.

## Gate 3 — execute the deferred Load/Save revision pass

The backlog's four user-visible items should now be handled under one
selection-coordinate architecture rather than four local patches:

1. **LSR-01:** checkpoint dirty Kit/Instrument HCNAMES state before browser
   cache/domain handoff, asynchronously and without changing Morph identity.
2. **LSR-02:** detach page repaint/exit from HCNAMES persistence so the old Load
   screen cannot remain visible while storage drains.
3. **LSR-03:** standardize blank as “not ready” and `Empty` as “current index
   proved absent.” Disable commit until the displayed coordinate is resolved.
4. **LSR-04:** make every browser publish selection immediately, request name
   and optional preview asynchronously, tag results with a generation, and
   discard stale callbacks.

Add the missing top-level Load/Save request/refusal and latency observations
before behavior changes, as the backlog already specifies. Then run the
non-destructive AutoSave/HCNAMES/settings matrix, followed by power-cut and
failure injection. The dedicated HCNAMES name mirror and separate Pattern
store are already implemented decisions; test them, do not reintroduce the
shared `.hcindex` cache or fold Pattern into HCPR.

Particularly important cases before new features:

- `AS-ENABLE` complete-current-state capture and OFF during active work;
- HCNAMES publication across Kit/Instrument browser-family changes;
- active transaction plus Load/Save exclusion and page-exit release;
- Bank identity agreement among `settings.cfg`, HCNAMES row 0, and HCPR;
- settings/HCNAMES/HCPR/PAT4 later-mutation-wins behavior;
- corrupt/partial Pattern inside Scene and partial Bank loads;
- stale callback ownership after rapid reverse scrolling/type changes.

The larger recursive-delete fault matrix and every power-cut point remain
valuable, but can follow the non-destructive matrix unless a current failure
reappears.

## Gate 4 — small engineering hygiene before feature branches spread

- Add Makefile header dependencies (`-MMD -MP` and inclusion of generated
  `.d` files). At present a `config.h` edit can leave stale objects unless the
  developer remembers `make clean`; the requested 200 ms change is itself an
  example of this risk.
- Decide and normally revert the temporary 2,048-record AutoSave trace ring to
  its 64-record default. Keep larger capture builds explicit and short-lived.
- Re-check duplicate `bootlog.bin`/`asavetrc.bin` names on hardware now that
  the earlier LFN fix is present; close the item if not reproducible.
- Validate `DEV_LOGGING_IWDG` only in a dedicated diagnostic build. It is not a
  release blocker while disabled and should not be mixed into this functional
  pass.
- Update the specification/backlog after each gate so historical “unresolved”
  notes do not continue to direct work after their implementation has landed.

## Not a pre-feature blocker

These should remain separate future work unless the gates above expose a
dependency:

- the comprehensive Menu specification sheet;
- host log conversion while trace formats are still changing;
- hardware CRC32C/table acceleration at current file sizes;
- manual roll UI, triplet/dotted scale decisions, looper mapping, external MIDI
  tracks, and DSP/FX expansion.

## Exit criteria for returning to feature development

- No lost front-panel edges or stale hold ownership under combined SD/Pattern
  stress.
- Clean AutoSave and clean Pattern maintenance have near-zero steady CPU and
  produce no file generations.
- OFF-to-ON captures the full current scalar and Pattern state and survives
  reboot; setup failure is observable and recoverable.
- All 16 boot Scenes show chase without a PERF repair action.
- Pattern copy, probability, and canonical automation target behavior have
  deterministic tests.
- Pattern persistence and the four asynchronous browser behaviors pass the
  focused hardware matrix with no stale callback or identity mismatch.
- A clean rebuild occurs automatically after header edits, and diagnostic
  builds no longer distort normal background scheduling by default.

No `Core/` code was changed in this assessment.
