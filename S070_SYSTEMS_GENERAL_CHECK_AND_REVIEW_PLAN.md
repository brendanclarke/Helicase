# S070 Systems General Check and Review Plan

Baseline: commit `9627f70` on `dev-ph5-effects` (Session 069 closure,
2026-09-20). Build: text=450,140, data=416, bss=291,756; image 450,572 bytes.

## Purpose

Bounded systems-level fitness gate before Phase 5 Effects development. This
session consolidates deferred S069 items, the AutoSave re-enable test matrix,
the general fitness agenda items not yet resolved, and relevant SCOPING_TARGETS
Phase 4 remaining work.

## Gate 0 — S069 deferred validation and closure

### 0.1 Pass 2 hardware validation (PENDING from S069)

Confirm the Session 069 Pass 2 implementation on hardware:

- Shared background CPU budget enforces 2.5%/5% limits during playback/stopped
- `H` trace reports (DEV build) show per-class budget accounting with
  reasonable charged microseconds and denied-slice counts
- Load/Save repair gate suppresses `patSvc_tick()` repair section when
  `menu_activePage == LOAD_PAGE || SAVE_PAGE`
- Pattern quiet window (250 ms `AUTOSAVE_PATTERN_QUIET_WINDOW_MS`) and max
  latency (5000 ms `AUTOSAVE_PATTERN_MAX_LATENCY_MS`) behave correctly under
  sustained editing
- No audio glitches under combined editing with budget active

Acceptance: capture card with `H` trace records; verify per-class breakdown
sums are plausible. Verify repair section is silent during Load/Save page
presence.

### 0.2 AutoSave OFF-to-ON re-enable test matrix

Source: `S070_AUTOSAVE_REENABLE.md` items 1-6. S069 resolved the root cause
(Pattern maintenance churn creating perpetual layout-only dirty events) but
the re-enable path itself needs focused hardware testing:

1. **Lifecycle trace stages**: add policy OFF, policy ON, setup admitted, setup
   success/failure, tracking enabled, full-Bank seed complete, scalar dirty
   count, and Pattern dirty mask trace stages. Do not use per-byte `D` records
   for this summary.
2. **AS-ENABLE matrix**: run the `AS-ENABLE` cases from
   `AUTOSAVE_TEST_CASES_LOAD_SAVE_REVISIONS.md` with a known change in a low
   and high Scene. Hash scalar payloads and PAT4 semantic content before OFF,
   after OFF edits, after ON/setup, after every writer generation, and after
   reboot.
3. **Concurrent activity**: verify ON during playback, recording, Load/Save
   suppression, an active scalar transaction, and an active Pattern transaction
   separately.
4. **Item 4 resolved**: layout-only Pattern dirty feedback was fixed in S069
   (non-semantic separation). Confirm by observing that clean Pattern state
   produces no file generations after convergence.
5. **Setup failure retry**: expose or automatically retry
   `fs_autosave_setup_failed` with a bounded cadence. Preserve the rule that no
   retry can start while Load/Save owns the facade. Consider a visible "AutoSave
   error" UI status.
6. **Active Scene priority**: consider prioritizing the active Scene's Pattern
   after re-enable while retaining a rotating cursor for fairness. This changes
   latency, not data or atomicity semantics.

### 0.3 S069 deferred refactor assessment

Review but do not implement unless needed:

- **Budget extraction**: The budget primitive lives in filesystem.c alongside
  its consumers. If future non-filesystem modules beyond PatternStackService
  need budget gating, extract to a standalone `BackgroundBudget.c` module.
  Currently not needed.
- **Repair gate refinement**: Currently uses `menu_activePage` check. If
  Load/Save lifecycle becomes more complex, consider a dedicated
  `filesystem_isLoadSaveActive()` predicate.

## Gate 1 — Pattern persistence boundary validation

Source: `S070_GENERAL_FITNESS_AGENDA.md` Gate 1. The storage choice is resolved
(separate per-Scene PAT4 A/B files); what remains is validation:

- Root Scene Save/Load round trips
- Partial Bank Save/Load without modifying unselected resident Patterns
- Boot restore from every active Scene
- Active, shown, pending, service-owned, and `menu_playedPattern` alignment
- Corrupt, missing, truncated, wrong-generation, and power-cut PAT4 cases
- Edits during Pattern snapshot/write (proving the later edit remains dirty)
- One-time initialization at every load/retry phase
- Mixed AutoSave/HCNAMES/explicit Scene fixtures with Pattern rows and `@`
  provenance

Build reproducible card fixtures and semantic Pattern comparators first. Use
PAT4 generations, CRCs, decoded addresses/blocks, HCNAMES rows, and runtime
indices — not FAT timestamps.

## Gate 2 — Phase 4 incomplete behavior

Source: `S070_GENERAL_FITNESS_AGENDA.md` Gate 2 and `SCOPING_TARGETS.md` Phase
4 remaining items.

### 2.1 Dynamic Pattern copy operations

`pat_copyTrack()`, `pat_copyPattern()`, and `pat_copyBar()` are deliberate
no-ops. Implement Phase 4.5 before building UI features that assume copy is
trustworthy:

- Snapshot source semantic blocks; never copy raw destination-owned offsets
- Preflight required chunks and queue/bulk capacity
- Define atomicity for partial allocation failure (destination unchanged)
- Preserve trigger bits, specials, and every automation entry
- Support overlap safely for same-Pattern bar/track copies
- Publish one semantic dirty event per completed destination scope
- Test under playback, Pattern maintenance, AutoSave snapshot, and service
  target handover

### 2.2 Probability must gate the complete step

`seq_advanceTrackStep()` currently applies probability only around
`seq_triggerVoice()` and queues step automation afterward regardless. Compute
one step-play decision and use it to gate both trigger and automation
publication. Define erase/record and trigger-inactive-but-automated semantics.

### 2.3 Complete automation target runtime ownership

`seq_drainPendingAutomation()` currently applies only Instrument descriptor
targets and deliberately ignores Scene targets. Add a typed Scene-target
dispatcher with the same runtime side effects as an ordinary Scene parameter
edit. Test per-voice Morph, Scene decimation, and the track-7
choke/base-decay ownership rule.

### 2.4 Final LED state consolidation

Perform Phase 4.11 after the S068 chaselight fix (now resolved). Give
`ledHandler` one explicit render priority and fallback model, including BAR1
and chase. Keep the public LED API stable and regression-test mode changes,
held automation, recording feedback, pulses over blinks, and Scene changes.

### 2.5 Per-track scale and per-track shuffle

Listed in SCOPING_TARGETS as Phase 4 remaining items. Scope and implement if
time permits.

## Gate 3 — Load/Save revision pass

Source: `S070_GENERAL_FITNESS_AGENDA.md` Gate 3. Four user-visible items under
one selection-coordinate architecture:

1. **LSR-01**: checkpoint dirty Kit/Instrument HCNAMES state before browser
   cache/domain handoff, asynchronously and without changing Morph identity.
2. **LSR-02**: detach page repaint/exit from HCNAMES persistence so the old
   Load screen cannot remain visible while storage drains.
3. **LSR-03**: standardize blank as "not ready" and `Empty` as "current index
   proved absent." Disable commit until the displayed coordinate is resolved.
4. **LSR-04**: make every browser publish selection immediately, request name
   and optional preview asynchronously, tag results with a generation, and
   discard stale callbacks.

Add missing top-level Load/Save request/refusal and latency observations
before behavior changes. Run the non-destructive AutoSave/HCNAMES/settings
matrix, followed by power-cut and failure injection.

Particularly important cases:

- `AS-ENABLE` complete-current-state capture and OFF during active work
- HCNAMES publication across Kit/Instrument browser-family changes
- Active transaction plus Load/Save exclusion and page-exit release
- Bank identity agreement among `settings.cfg`, HCNAMES row 0, and HCPR
- Settings/HCNAMES/HCPR/PAT4 later-mutation-wins behavior
- Corrupt/partial Pattern inside Scene and partial Bank loads
- Stale callback ownership after rapid reverse scrolling/type changes

## Gate 4 — engineering hygiene

Source: `S070_GENERAL_FITNESS_AGENDA.md` Gate 4.

- **Makefile header dependencies**: add `-MMD -MP` and inclusion of generated
  `.d` files. At present a `config.h` edit can leave stale objects.
- **Trace ring size**: decide and normally revert the temporary 2,048-record
  AutoSave trace ring to its 64-record default. Keep larger capture builds
  explicit and short-lived.
- **Duplicate filenames**: re-check duplicate `bootlog.bin`/`asavetrc.bin`
  names on hardware now that the earlier LFN fix is present; close if not
  reproducible.
- **IWDG**: validate `DEV_LOGGING_IWDG` only in a dedicated diagnostic build.
  Not a release blocker while disabled.
- **Spec updates**: update specification/backlog after each gate so historical
  "unresolved" notes do not continue to direct work after their implementation
  has landed.

## Gate resolution status

| Gate | Status | Notes |
|------|--------|-------|
| 0.1 Pass 2 HW validation | PENDING | Code-complete at `9627f70` |
| 0.2 AutoSave re-enable | NOT STARTED | Root cause (churn) resolved in S069 |
| 0.3 Deferred refactor | DEFERRED | Review only; implement if needed |
| 1 Pattern persistence | NOT STARTED | Validation cases defined |
| 2.1 Copy operations | NOT STARTED | No-op stubs exist |
| 2.2 Probability gating | NOT STARTED | Defect from S066 |
| 2.3 Scene automation targets | NOT STARTED | Descriptor targets only |
| 2.4 LED consolidation | NOT STARTED | Phase 4.11 |
| 2.5 Per-track scale/shuffle | NOT STARTED | Phase 4 remaining |
| 3 Load/Save revision | NOT STARTED | LSR-01 through LSR-04 |
| 4 Engineering hygiene | NOT STARTED | Makefile deps, trace ring, etc. |

## Prioritization guidance

Gates 0.1 and 0.2 are prerequisites — they validate S069 changes and close the
AutoSave re-enable question. Gates 1 and 2.1-2.3 are the highest-value
remaining Phase 4 work. Gate 3 is a significant refactor that can be scoped
incrementally. Gate 4 items are individually small.

Not a pre-feature blocker (from `S070_GENERAL_FITNESS_AGENDA.md`):

- Comprehensive Menu specification sheet
- Host log conversion while trace formats are still changing
- Hardware CRC32C/table acceleration at current file sizes
- Manual roll UI, triplet/dotted scale decisions, looper mapping, external MIDI
  tracks, and DSP/FX expansion
