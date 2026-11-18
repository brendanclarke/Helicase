# Pattern Track Properties and Widgets Completion (Draft)

Deferred from S070 to a later session. These are Phase 4 remaining items that
require dedicated implementation time and are not blockers for Phase 5 Effects.

## Per-track scale and per-track shuffle

Source: `SCOPING_TARGETS.md` §4.7. Session 031 landed a bridge version with
STEP front-page track settings exposing length, scale, MIDI channel, MIDI note,
and per-track shuffle. Sequencer derives scaled/shuffled timing from an absolute
96-PPQ master clock. Session 068 fixed per-track step length to read from
`pat_scene_region_t.track_length` (was hardcoded 16).

Remaining work:
- Complete the scale implementation: per-track scale from `/16` to `×16` in
  `/2` increments relative to 1/16th base
- Decide dot/triplet subdivisions (flagged open in SCOPING_TARGETS §4.10) —
  affects scale-value encoding
- Per-track shuffle consumption from the bridge storage into the dynamic pool
  model

## Dynamic Pattern copy operations

Source: `SCOPING_TARGETS.md` §4.5 and `S070_GENERAL_FITNESS_AGENDA.md` §2.1.

`pat_copyTrack()`, `pat_copyPattern()`, and `pat_copyBar()` are deliberate
no-ops. Implementation plan:

- Snapshot source semantic blocks; never copy raw destination-owned offsets
- Preflight required chunks and queue/bulk capacity
- Define atomicity for partial allocation failure (destination unchanged
  preferred)
- Preserve trigger bits, specials, and every automation entry
- Support overlap safely for same-Pattern bar/track copies
- Publish one semantic dirty event per completed destination scope
- Test under playback, Pattern maintenance, AutoSave snapshot, and service
  target handover

Key risk from SCOPING_TARGETS §4.3b: copying a portion of a pattern can sound
different once pasted elsewhere if the copied region didn't start right after
an active-step reset point (due to the hold-based automation model). Must be
addressed explicitly in the implementation.

## Copy/paste and clear UI operations

Source: `SCOPING_TARGETS.md` §4.5 and §4.6. The full set: copy scene, copy
instrument (single voice part), copy track sequence, copy FX, copy bar, copy
step. Uses the existing hold-COPY/press-source/press-destination gesture
extended to new selectable units.

`SHIFT+COPY/CLEAR` clears a step or bar of all automation/settings (distinct
from toggling the on/off bit). The on/off MSB, automation offset, and
has-specials bit are independent fields.

## Live record automation

Source: `SCOPING_TARGETS.md` §4.3a. The hold/record model is settled:

- Parameter changes are watched per-step, stamped at step-end with final value
- Active steps re-stamp held values to maintain correct playback
- Inactive steps consume slack only for parameters that receive new automation
- Must distinguish external CC/knob input from internal LFO overlay movement
  (§4.3b cross-feature risk)

The reserved-slack mechanism (now implemented as trailing-slack reservation in
S069) guarantees live recording never stalls. The reactive recovery path handles
slack exhaustion.

## Roll overhaul

Source: `SCOPING_TARGETS.md` §4.8. Roll rate becomes independent of pattern
length. Three recordable modes: full (pitch + velocity), note-only,
velocity-only. Record-to-track option (`slf` or specified track). Manual roll
triggering UI proposal needed before Phase 6 can wire it up.

## Automation hold flag reconciliation

Source: `SCOPING_TARGETS.md` §4.3a. The `automation hold` flag's purpose needs
clarification now that ordinary automation already holds by default until the
next active step. Decision: does `hold` mean "persist through active-step
reset," or something else? Resolve before both mechanisms are built.

## Patgen/Euklid revert

Source: `SCOPING_TARGETS.md` §4.9. `SHIFT+PERF` twice reverts; normal exit
commits. Length-change revert may leave residual track-offset artifacts.
