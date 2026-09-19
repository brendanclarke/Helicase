# S068 track settings ignored by sequencer

## Summary

Track length, step scale, and shuffle are stored, persisted, and editable
through the Menu, but the sequencer playback engine reads none of them. All
tracks play with 16 steps, 1/16th note resolution, and zero shuffle regardless
of the values in PatternData.

## Data lifecycle (working correctly)

PatternData owns the three per-track fields in `pat_scene_region_t`
(`PatternData.h:82-84`):

```c
uint8_t track_length[NUM_TRACKS];   /* default: NUM_STEPS (128) */
uint8_t track_scale[NUM_TRACKS];    /* default: TRACK_SCALE_OFF (10) */
uint8_t track_shuffle[NUM_TRACKS];  /* default: 0 */
```

Storage, edit, and persistence all work:

| Path | Working? | Where |
|------|----------|-------|
| Init default | Yes | `PatternData.c:768-770` |
| Menu display | Yes | `pat_applyTrackSettingsToMenu()` → `parameter_values[]`, `PatternData.c:1112-1114` |
| Menu edit | Yes | `pat_setTrackLength/Scale/Shuffle()`, `PatternData.c:1118-1147` |
| AutoSave dirty mark | Yes | `pat_markSceneDirty()` called by each setter |
| PAT4 file write | Yes | `filesystem.c:2876-2878` at header offset `48 + track*16` |
| PAT4 file read | Yes | `filesystem.c:12684-12686`, `14656-14658`, `27256-27258` |

The problem is exclusively in the sequencer's consumption of these values.

---

## Root cause 1 — Track length hardcoded to 16

### Where

`sequencer.c:525-529`, `seq_advanceTrackStep()`:

```c
static void seq_advanceTrackStep(uint8_t track)
{
    seq_stepIndex[track]++;
    if (seq_stepIndex[track] >= (int16_t)NUM_STEPS_PER_BAR)  /* == 16 */
        seq_stepIndex[track] = 0;
```

The compile-time constant `NUM_STEPS_PER_BAR` (16, defined `PatternData.h:18`)
is used as the step boundary for every track. The sequencer never reads
`region->track_length[track]`.

### Same issue in two other sites

1. **`seq_realignActivePatternToMasterClock()`** (`sequencer.c:744`):
   ```c
   seq_stepIndex[track] = (int16_t)(seq_masterStepClock % NUM_STEPS_PER_BAR);
   ```
   Used by boot alignment and runtime Scene/Bank realignment. The modulo wraps
   at 16 regardless of per-track length.

2. **`seq_handleMasterBoundary()`** (`sequencer.c:694`):
   ```c
   uint8_t masterStepPos = (uint8_t)(seq_masterStepClock % NUM_STEPS_PER_BAR);
   ```
   Detects bar boundaries for pattern-change commit, beat LED, and clock output.
   This is a master-grid concern and may legitimately stay fixed at 16; per-track
   wrapping is a separate question.

### Why it plays "16 steps"

`seq_stepIndex[]` is declared as `int16_t` (`sequencer.c:86`) and wraps at 16.
A pattern with `track_length = 128` (the default init value) stores step data
in 128 slots, but the sequencer only visits slots 0–15 on every bar.

### Corrective path

Replace the `NUM_STEPS_PER_BAR` boundary in `seq_advanceTrackStep()` with a
per-track read from the active Scene region:

```c
static void seq_advanceTrackStep(uint8_t track)
{
    const pat_scene_region_t *region = pat_sceneRegion(seq_activePattern);
    uint8_t len = (region && region->track_length[track] > 0u)
                  ? region->track_length[track]
                  : NUM_STEPS_PER_BAR;

    seq_stepIndex[track]++;
    if (seq_stepIndex[track] >= (int16_t)len)
        seq_stepIndex[track] = 0;
```

The same substitution is needed in `seq_realignActivePatternToMasterClock()`:

```c
    const pat_scene_region_t *region = pat_sceneRegion(seq_activePattern);
    for (track = 0u; track < NUM_TRACKS; track++) {
        uint8_t len = (region && region->track_length[track] > 0u)
                      ? region->track_length[track]
                      : NUM_STEPS_PER_BAR;
        seq_stepIndex[track] = (int16_t)(seq_masterStepClock % len);
        seq_lastMasterStep[track] = (uint8_t)seq_stepIndex[track];
    }
```

`seq_handleMasterBoundary()` should remain at `NUM_STEPS_PER_BAR` — it drives
the master grid (pattern-change commit, beat LED, clock output), which is a
bar-level concept independent of per-track loop length.

### Chase LED implication

`led_updateCurrentStep()` (`ledHandler.c:972`) shows the chase for the
*viewed* track's step. When tracks have independent lengths, the chase step
published via `seq_ledState.chaseStep` must be the index for the currently
active UI voice, which is already the case:

```c
seq_ledState.chaseStep = seq_stepIndex[menu_getActiveVoice()];
```

The chase renderer's `menu_currentBar` bar-within-pattern clamp
(`ledHandler.c:993-994`) may need adjustment if track lengths exceed 16 and
multi-bar navigation is exposed, but for the initial implementation (lengths
1–16), it works as-is. For lengths > 16, `menu_currentBar` logic is a
separate follow-on.

### Valid track length range

The current `pat_setTrackLength()` accepts any `uint8_t`. The sequencer fix
should treat 0 as "use default 16" (guard against a zeroed/corrupt field).
Valid musical lengths are 1–128 (NUM_STEPS), but the initial implementation
targets 1–16 (one bar). Multi-bar lengths (17–128) require `menu_currentBar`
integration which is deferred.

### Risks

- **ISR context**: `seq_advanceTrackStep()` runs inside `TIM3_IRQHandler`
  (priority 2). `pat_sceneRegion()` returns a pointer to resident SRAM1 —
  it does not perform SD I/O or allocation. The pointer dereference is a
  single load. Safe.

- **Region validity during Scene transitions**: the region pointer is stable
  between pattern-change commits. A Scene switch atomically replaces
  `seq_activePattern` at the master boundary in `seq_handleMasterBoundary()`,
  after which the next `seq_advanceTrackStep()` reads the new region.
  No torn-read risk.

- **Default value mismatch**: the current default init is `NUM_STEPS` (128),
  not `NUM_STEPS_PER_BAR` (16). All existing patterns on the test card have
  been patched to 16 via `SD_CARD_PAT_LENGTH/`. The in-memory init
  (`PatternData.c:768`) should be changed to 16 to match the sequencer's
  historic behavior and the patched card content.

---

## Root cause 2 — Step scale has no per-track prescaler (deferred)

### Where

`sequencer.c:766-779`, `seq_processSchedulerTick()`:

```c
if ((seq_elapsedPpqTicks % SEQ_INTERNAL_TICKS_PER_DEFAULT_STEP) == 0u) {
    ...
    for (track = 0u; track < NUM_TRACKS; track++)
        seq_advanceTrackStep(track);
```

All tracks advance together on a single global divisor.
`SEQ_INTERNAL_TICKS_PER_DEFAULT_STEP` = `96/4` = 24 PPQ ticks (1/16th note).
There is no per-track tick counter, accumulator, or scale lookup.

### What the stored value means

`track_scale[track]` is stored and edited (`PatternData.h:83`, `menu.c:11385`)
but never queried by the sequencer. `TRACK_SCALE_OFF` (10) is the init default.

### Potential fix (deferred)

Add per-track PPQ tick accumulators (`seq_trackTickAccum[NUM_TRACKS]`).
On each PPQ tick, increment each track's accumulator and advance only
when it reaches the threshold derived from the scale value. The scale maps
to a ticks-per-step ratio:

| Scale label | Ticks per step | Musical division |
|-------------|---------------|-----------------|
| 1/32        | 12            | 32nd note       |
| 1/16        | 24            | 16th note (default) |
| 1/8         | 48            | 8th note        |
| 1/4         | 96            | quarter note    |

The exact scale-to-ticks mapping must be defined against the original LXR's
documented table before implementation. The accumulators add
`NUM_TRACKS * 4` = 28 bytes of ISR-local static state.

### Interaction with track length

Track scale changes the rate at which steps are visited, not the total number
of steps. A track with length=8 and scale=1/8 plays 8 steps at half the rate
of the default grid. These are orthogonal.

---

## Root cause 3 — Shuffle is entirely absent from playback (deferred)

### Where

The sequencer fires every step at a uniform tick boundary. There is no shuffle
offset calculation anywhere in `sequencer.c`. The comment at line 221 states:
*"The transport tick stays uniform: fixed-grid patterns have no shuffle."*

### What the stored value means

`track_shuffle[track]` is stored and edited (`PatternData.h:84`, `menu.c:11434`)
but never consumed. The init default is 0.

### Potential fix (deferred)

Shuffle delays even-numbered steps (or odd, depending on convention) by a
fraction of the step interval. For 1/16th notes at 96 PPQ, the maximum
shuffle offset is ~23 ticks (one step minus one tick).

Implementation requires either:
1. **Sub-step scheduling**: maintain a per-track "delay ticks remaining"
   counter; on an even step, load the counter from the shuffle value instead
   of immediately triggering, and trigger when the counter expires on a
   subsequent PPQ tick.
2. **Deferred trigger queue**: queue the trigger with a future tick offset and
   drain it in `seq_processSchedulerTick()`.

Option 1 is simpler and lower-RAM. The per-track counter adds 7 bytes of ISR
static state.

### Interaction with track length and scale

Shuffle is a timing offset within each step interval. It is orthogonal to both
length (which steps exist) and scale (how fast steps arrive). All three can
compose independently.

---

## Implementation priority

1. **Track length** — fix this session. Three sequencer sites to change, one
   init default to align. The fix is minimal and the test card is ready.
2. **Track scale** — deferred. Needs the scale-to-ticks mapping table defined,
   per-track accumulators added, and interaction with master boundary tested.
3. **Track shuffle** — deferred. Needs sub-step scheduling infrastructure.

## Pat file preparation

All PAT4 files in `SD_CARD_PAT_LENGTH/` have been patched: every track's
`track_length` set to 16, CRC32C recalculated. 35 PAT4 files were patched;
297 legacy text-format files (v1-v3, no stored track_length) were skipped —
those files have no track_length field and will receive the default from
`PatternData.c` on import.

## Hardware acceptance — track length

- 2026-09-19: tested on hardware. Per-track independent lengths play correctly:
  tracks wrap at their configured `track_length` value, not the hardcoded 16.
  Tracks with length < 16 loop independently of other tracks. The default
  init value (now 16, matching historic behavior) produces identical playback
  to the pre-fix firmware. Realignment after Scene/Bank switch places each
  track's cursor at the correct position within its own length via
  `seq_masterStepClock % len`. Chase LED follows the active voice's step
  index within its configured length. **PASS — track length fix accepted.**

### Deferred items

The following track-setting bugs are identified and assessed but deferred to
future sessions. Full root cause analysis and potential fix paths are documented
in the Root cause 2 and Root cause 3 sections above.

1. **Per-track step scale** — `track_scale[track]` is stored, persisted, and
   editable but the sequencer advances all tracks on a single global divisor
   (`SEQ_INTERNAL_TICKS_PER_DEFAULT_STEP` = 24 PPQ ticks). Fix requires
   per-track PPQ tick accumulators and a scale-to-ticks mapping table.
   Tracked in `SCOPING_TARGETS.md` § Session 068.

2. **Per-track shuffle** — `track_shuffle[track]` is stored, persisted, and
   editable but the sequencer fires every step at a uniform tick boundary with
   no shuffle offset calculation. Fix requires sub-step scheduling
   infrastructure (per-track delay counters or a deferred trigger queue).
   Tracked in `SCOPING_TARGETS.md` § Session 068.
