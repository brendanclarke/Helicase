# Generator Page — Draft Spec

Status: **proposal / not implemented**. This is a design draft against the
current LXR-02 firmware tree, written in the style of
`MODULE_INTERCHANGE_SPEC.md`, for review before any code is written.

**Revised 2026-10-27 (Session 085 close):**
- **Code facts re-checked** against the tree (module paths, select-mode
  codes, the Euklid page, `CAT_GENERATOR`, the SOM flag): §1 and §2.2.
- **New §2.6.** Session 085 built a type selector whose dependent rows
  change meaning with the type (the oscillator `typ`/`wav`/`mod` rows).
  §2.6 records the parts of that pattern a Generator type row should
  reuse.
- **§5 updated:** the S085 answers to the open questions on page tables
  and CPU.

## 1. What exists today

The firmware already has two pattern generators, but they are architecturally
inconsistent with each other and with the ask ("one Generator page, pick a
type, treat the current pattern as input, choose Off/Overdub/Replace"):

| | Euklid | SOM |
|---|---|---|
| Module | `Core/Bank/Scene/Pattern/EuklidGenerator.c` | `Core/Bank/Scene/Pattern/SomGenerator.c` (data in `SomData.c`) |
| Menu page | `EUKLID_PAGE` still exists in `menu.h`/`menuPages.h`, but **its select mode is retired and compiled out** (`ENABLE_EUKLID_PAGE` is off; `buttonHandler.h`: "the retired Euklid mode is compiled out"). There is no `SELECT_MODE_PAT_GEN` any more. | `SOM_PAGE` (select mode `SELECT_MODE_SOM_GEN = 0x06`; `buttonHandler.c` switches to it) |
| Params | `PAR_EUKLID_LENGTH/STEPS/ROTATION` and `PAR_MORPH` (shown as X-fade). `PAR_ROLL` was retired in place in S081 (A3-5, Q14); its cell is cleared. | `PAR_POS_X/POS_Y/FLUX/SOM_FREQ` |
| Scope | Per-track (`euklid_length[NUM_TRACKS]` etc.), one track edited at a time via `menu_getActiveVoice()` | Global/all-voice — one `SomGenerator` struct drives all 7 tracks at once |
| When it runs | **Write-once ("bake")**: any param edit calls `euklid_generate()` → `euklid_transferPattern()` → `pat_setMainStepsRaw()`. It overwrites the pattern's main-step mask immediately and permanently. There is no "live" Euklid. | **Live, real-time**: `som_tick()` is called from `sequencer.c` every step and directly calls `seq_triggerVoice()`. It does not touch `PatternData` at all — it plays *instead of* the stored pattern. |
| Mix mode | None. Editing Euklid destroys the previous main-step content for that track/pattern. | Binary and, importantly, **currently dead**: `seq_SomModeActive` (`sequencer.c:804`, static) is checked at `sequencer.c:3067` (the `som_tick()` call is at 3069) and at 5616, and is never set to 1 anywhere. SOM mode cannot be turned on from the UI. (Re-checked 2026-10-27.) |

Key finding: **neither generator today implements "Off / Overdub / Replace."**
Euklid is a destructive one-shot bake. SOM is an unwired, would-be full
replace of the entire track engine. Building the requested feature means
introducing that concept for the first time, not exposing something that's
already there.

Also relevant:

- **Select-mode buttons** are a 3-bit field. `buttonHandler.h` today:

  | Code | Mode |
  |---|---|
  | `0x00` | `VOICE` |
  | `0x01` | `PERF` |
  | `0x02` | `STEP` |
  | `0x03` | `LOAD_SAVE` |
  | `0x04` | **free** (the retired Euklid mode) |
  | `0x05` | `FX` (SHIFT+PERF opens the Effect page) |
  | `0x06` | `SOM_GEN` |
  | `0x07` | `MENU` |

  A Generator mode can take `0x04`, or replace `SOM_GEN` at `0x06` (which
  also frees `0x06`). Collapsing is no longer forced by a lack of codes; it
  is still the cleaner design, since SOM would become one Generator type.
- Pattern data model (`PatternData.h`): `NUM_TRACKS=7`, `NUM_PATTERN=8`,
  `NUM_STEPS=128` (16 main steps × 8 sub-steps). Each `Step` has
  `volume`(+active bit)/`prob`/`note`/2 automation lanes. Reads for a
  generator's "current pattern as input" are already exposed narrowly:
  `pat_isMainStepActive`, `pat_isStepActive`, `pat_getStepVolume`,
  `pat_getStepProbability`, `pat_getStepNote`, `pat_readStep`.
- Ownership rule from `MODULE_INTERCHANGE_SPEC.md`: "UI code calls the owner
  module directly," "Pattern/track/step/automation edits go through `pat_*`,"
  "Sequencer may read pattern data through narrow PatternData playback
  helpers; it must not index PatternData storage arrays directly." Any new
  Generator engine should follow this — it's a peer of EuklidGenerator/
  SomGenerator, not a Sequencer-internal thing, and it must not reach into
  `pat_patternSet` directly.

## 2. Proposed architecture

### 2.1 New module: `Core/Bank/Scene/Pattern/GeneratorEngine.c/.h`

A thin dispatcher that owns:
- the active `GeneratorType` (per active voice, see 2.3),
- the shared "output mode" (Off / Overdub / Replace) per voice,
- a `gen_tick(stepNr, mutedTracks)` entry point called from `sequencer.c` in
  place of the current hard-coded `if(seq_SomModeActive) som_tick(...)`
  branch,
- routing into the six type-specific backends (`EuklidGenerator`,
  `RotateSpreadHumanize` [new], `SomGenerator`, `Arpeggiator` [new],
  `MarkovGenerator` [new], `GeneticGenerator` [new]).

This replaces the SOM-only global-replace hook with a general one, and gives
Euklid a live path it doesn't have today (see 2.4).

### 2.2 Menu: collapse two pages into one

- Remove `EUKLID_PAGE` and `SOM_PAGE` from `enum PageNames`; add
  `GENERATOR_PAGE`. The `ENABLE_EUKLID_PAGE` blocks in `buttonHandler.c`
  (lines ~30, 719, 2832, 2875, 3297) go with it.
- Replace `SELECT_MODE_SOM_GEN` (`0x06`) with `SELECT_MODE_GENERATOR`, or
  use the free `0x04`. (`0x05` is `SELECT_MODE_FX` since the Effect page
  work; the older draft's `0x05` proposal would collide.)
- `menuPages[GENERATOR_PAGE][NUM_SUB_PAGES]` sub-page 0 becomes the **type +
  mix** selector row (new `TEXT_GEN_TYPE`, `TEXT_GEN_OUTPUT_MODE` labels,
  new `PAR_GEN_TYPE`/`PAR_GEN_OUTPUT_MODE` params, `DTYPE_MENU`-style
  encoding same as other enumerated params). Sub-pages 1+ are populated
  **dynamically** based on the selected type, reusing the existing
  `Page{top1..8, bot1..8}` struct but swapped at runtime in `menu_switchPage`
  rather than baked into the const table, since the six types don't share a
  parameter shape. This is a bigger change than a normal page (all other
  pages are `const` and static); it needs either (a) six const sub-tables
  selected by `menu_activeGeneratorType`, or (b) one sub-table with the
  labels/param IDs patched in `buttonHandler_applyGeneratorParamsToMenu()`
  before `menu_switchPage(GENERATOR_PAGE)`, mirroring how
  `buttonHandler_applyEuklidParamsToMenu()` already primes `parameter_values`
  before switching to `EUKLID_PAGE`. (a) is more in keeping with the existing
  all-`const` table style and is the recommended option.

- `CAT_GENERATOR` already exists in `catNamesEnum`. It is in use (the SOM
  `X`/`Y` names in `menu.c` ~1430); reuse it for all six types instead of
  adding a category.

### 2.3 Scope: per-voice or global?

Euklid today is per-track; SOM today is global-across-all-voices. The six
types don't have a natural shared scope:

- Euklid, Rotate/Spread/Humanize, Arpeggiator, Markov are naturally
  **per-track** (they read/write one track's steps).
- SOM and Genetic are naturally **multi-voice** (SOM's whole design is
  cross-voice interpolation; a genetic algorithm scoring "how good is this
  8-track pattern" is inherently pattern-wide).

Proposal: `GeneratorEngine` state is **per active voice** for the
single-track types, and the SOM/Genetic backends internally ignore the
per-voice framing and act on all unmuted tracks when selected — same
compromise SOM already makes today. The Generator page's "Type" and "Output
Mode" selection is stored per-voice (`menu_getActiveVoice()`-indexed, same
pattern as `euklid_length[NUM_TRACKS]`), but SOM/Genetic write-back is
naturally understood as "engaging this generator engages it for the whole
kit" — this should be called out explicitly in the UI (e.g. LED feedback
lighting all 7 voice LEDs when SOM/Genetic is the active type), since it
will otherwise surprise users coming from the per-track Euklid mental model.

### 2.4 Output mode semantics (Off / Overdub / Replace)

This is genuinely new. Proposed definitions, implemented in `gen_tick()`,
which is called from the same `sequencer.c` per-track loop location that
currently does `if(seq_SomModeActive)`:

- **Off**: generator does not run. Sequencer plays `PatternData` exactly as
  it does today (unchanged code path).
- **Replace**: generator output is used instead of the stored pattern for
  that track/step, exactly like SOM does today — `PatternData` is not read
  or written. This is the cheap, already-proven path (SOM already works
  this way; it just needs `seq_SomModeActive` replaced with a real per-type
  per-voice flag and wired to a button/param instead of being dead code).
- **Overdub**: generator output triggers **in addition to** stored pattern
  playback — i.e. both `pat_isMainStepActive(...)`-driven triggers *and*
  generator-driven triggers fire, without one silencing the other, and
  without either being written into `PatternData`. This is a "live layer on
  top," not a record-into-pattern behavior — nothing about the user's
  wording implies it should persist edits into the pattern on stop, and
  auto-writing generator overdub into `PatternData` would need an explicit
  separate "commit/bake" action (see 2.5) so it doesn't silently clobber the
  user's pattern the way Euklid does today.

Euklid needs the most rework to fit this model: today every param edit bakes
immediately into `PatternData` and there is no live variant. Under
Off/Overdub/Replace, Euklid's *Off* mode should mean "don't auto-bake on
param change" (params can be auditioned without touching the stored
pattern), *Replace* should mean "generate into a scratch buffer and play
that instead of stored data live, exactly like SOM does," and only an
explicit **Bake/Commit** action (see 2.5) should call the existing
`euklid_transferPattern()` → `pat_setMainStepsRaw()` path. This preserves
the existing, working bake behavior as an opt-in rather than an unconditional
side effect of turning a knob.

### 2.5 "Treats the current pattern as a potential input"

Read-only inputs already exist and should be used as-is rather than adding
new PatternData accessors:

- Euklid: N/A today (pure algorithmic), but could optionally seed
  `rotation`/`length` from `pat_getEffectiveTrackLength`/
  `pat_getTrackRotation` when first entering the page for a track, so the
  generator starts aligned with the track's current length instead of
  always defaulting to 16.
- Rotate/Spread/Humanize: reads the *entire* current track's steps
  (`pat_isStepActive`, `pat_getStepVolume`, `pat_getStepProbability`,
  `pat_getStepNote` per sub-step) as its actual input — this type doesn't
  generate from nothing, it transforms what's already there. This is the
  one type where "current pattern as input" is the whole point rather than
  an optional seed.
- SOM: unaffected by current pattern (matches existing behavior).
- Arpeggiator: pattern's active main steps can define the *rhythm gate*
  (which steps play), while a note pool comes from a `TEXT_GEN_NOTE_SOURCE`
  param (pattern's per-step notes / a fixed scale / MIDI-learned).
- Markov: pattern's existing sub-step note/velocity/probability sequence
  is the **training corpus** the transition table is learned from
  (`pat_readStep` per step). No corpus = flat/uniform transitions.
- Genetic: the current pattern (or current 7-track pattern set for that
  `patternNr`) is generation-0's fittest individual / seed of the initial
  population, so "evolve" nudges away from what's already programmed rather
  than starting from random noise.

- **Commit/Bake control**: add one shared action, not per-type — a
  long-press or dedicated button (mirroring existing `SHIFT`-combo
  conventions in `buttonHandler.c`) that calls into `pat_setMainStepsRaw`
  and/or `pat_recordNote`-per-step to write the generator's current live
  output into `PatternData` for the viewed pattern, after which Output Mode
  can be set back to Off. Without this, Overdub/Replace content is lost on
  pattern change or power-down, which is probably not what "let the pattern
  play through" implies for a live performance tool but *is* fine for
  auditioning — both use cases are legitimate, so bake should be a manual,
  explicit step rather than automatic.

### 2.6 Reuse of the Session 085 type-selector pattern

**Why:** Session 085 added the oscillator `typ` row. Its dependent rows
(`wav`, `mod`) change meaning, range and display with the type. After
the S085 P2 §14 swap, `wav` is the selection (wave, pwm family, sample,
table; a name) and `mod` a plain 0..127 (pwm width, sample start, table
position). Some types are excluded per oscillator (no `pwm` on the
Cymbal/HiHat modulators, `instrumentManager_oscTypeAllowed()`). That is the same problem as the
Generator's Type row and its type-specific parameters. The pieces that
worked, and where they live, are summarised below. Full detail is in
`085_SESSION_HANDOFF_LOG.md` §4.10–§4.13, `INSTRUMENTS_DSP_REFERENCE.md`
(oscillator chapter) and `BANK_PRESET_ARCHITECTURE.md` (selector rows).

1. **Keep the page tables `const`; vary formatting and range per type**
   (answers §5 item 3).
   - S085 did not swap page tables. Each cell kept its parameter.
   - `InstrumentCellFormat` (`instrumentCellFormat_describe()`) returns,
     for the shown value, a *kind* (name table, number, sample name…), a
     clamped index, the current maximum and an aux value.
   - `menu.c` uses it at the formatting sites, in `menu_clampCellValue()`
     and for the automation maximum and text.
   - **For the Generator:** sub-pages 1+ could be one const table of
     generic `PAR_GEN_P1…P8` cells. A `generatorCellFormat_describe(type,
     param, value)` would supply the per-type label, value text and maximum
     (option (c), alongside the draft's (a) and (b)). Option (a) (six const
     sub-tables) remains valid; (c) keeps `menuPages` unchanged and one
     automation ID per generic parameter.
2. **Request bytes survive a type round trip.**
   - When the type changes, S085 re-derives each dependent value from its
     last *retained* request (`waveRequest`, `sourceRequest`), clamped to
     the new type's maximum. A width of 100 survives pwm → wav → pwm
     although `wav` allows only 0–10.
   - Only retained writes record the request: menu, MIDI, load, Morph and
     automation. Temporary overlays (LFO) do not (S085 D2).
   - **For the Generator:** store per-type parameter values, or one request
     byte per generic parameter, so switching Type and back restores
     settings.
3. **Ranges follow the type everywhere** (S085 D11, A9). The clamp, the
   automation maximum, the velocity and LFO ranges, and the LFO *base*
   must all use the current type's maximum. When the type changes, the
   base is re-derived from the request, not from the clamped value.
4. **Stored zero is today's behaviour.** New rows default to 0 = the old
   behaviour (`typ` 0 = `wav`, `run` 0 = `rst`). The Generator's Type 0 and
   Output Mode 0 (Off) should map to "what the firmware does today".
5. **Selectors are single-ended and applied first.**
   - The S085 selector rows (`typ`, `run`) are automatable but not
     morphable.
   - They are stored with the Normal cell, the Morph mirror and the
     interpolation cell set equal.
   - They are applied before anything that depends on them
     (`preset_applySelectorRows()` runs before the Morph image on every
     slot reset path).
   - In the SHIFT Morph view they edit Normal (D12).
   - If Generator Type and Output Mode become stored or automatable
     values, they need the same rules.
6. **Constant worst-case cost** (MEMORY.md "DSP CPU Policy"; reaffirmed in
   S085).
   - Budget each Generator type by its worst tick, never its average.
   - S085's render pool is a precedent for a shared, budgeted resource
     with a fixed claim order: 2 units per block, ongoing work first, new
     work refused rather than over budget. Markov or Genetic work in the
     sequencer path would follow it.

## 3. Proposed page layout

`GENERATOR_PAGE`, sub-page 0 (always the same regardless of type):

| top | bot |
|---|---|
| `TEXT_GEN_TYPE` | `PAR_GEN_TYPE` (0=Euklid,1=Rotate/Spread/Humanize,2=SOM,3=Arp,4=Markov,5=Genetic) |
| `TEXT_GEN_OUTPUT_MODE` | `PAR_GEN_OUTPUT_MODE` (0=Off,1=Overdub,2=Replace) |
| `TEXT_GEN_TARGET` | `PAR_GEN_TARGET` (active voice, or "all" for SOM/Genetic — greyed/forced) |
| `TEXT_GEN_MIX` | `PAR_GEN_MIX` (0-127; blend/probability the generator's trigger wins vs. the stored step, used by Overdub and by Replace-with-partial-influence types) |

Sub-pages 1–3 (or however many the type needs) are type-specific, selected
by `parameter_values[PAR_GEN_TYPE]`, mirroring the existing per-voice
`VOICE1_PAGE..VOICE7_PAGE` sub-page pattern (8 sub-pages × 8 params each,
`TEXT_EMPTY`/`PAR_NONE` padding unused slots).

Common param proposed for **every** type (in addition to Type/Output
Mode/Target/Mix above):

- `PAR_GEN_SEED` — regenerate/reseed trigger (button-style `DTYPE_0b1`
  pulse), re-rolls anything with internal randomness (SOM flux, Markov
  sampling, Genetic mutation) without changing settings. No-op for Euklid
  and Rotate/Spread (deterministic given params) but present for UI
  consistency and future-proofing.

## 4. Per-type parameter proposal

### 4.1 Euklid (existing, extended)

Already implemented: Length (`PAR_EUKLID_LENGTH`, 1-16), Steps
(`PAR_EUKLID_STEPS`, 1-length), Rotation (`PAR_EUKLID_ROTATION`, 0-length-1).
`PAR_MORPH`/`PAR_ROLL` currently sit on the Euklid page too but are actually
general track roll/crossfade params, not Euklid-specific — worth confirming
whether they belong on the shared page or should move off it now that
Generator is unified.

Proposed for the new page: Length, Steps, Rotation (unchanged), no new
params required — its "generator-specific" surface is already minimal. Its
main change is behavioral (2.4/2.5), not new knobs.

### 4.2 Rotate/Spread/Humanize (new)

Operates on the current track's existing steps in place — this is the type
whose whole job is "current pattern as input."

- `Rotate Amount` — steps, ±(track length) — shifts active steps like
  `euklid_rotatePattern()` already does bitwise, but reusable generically.
- `Spread` — 0-127 — probability-based widening: for each active step,
  chance of also activating a neighboring sub-step (implements a
  "spread hits outward" feel).
- `Timing Humanize` — 0-127 — jitter applied to *effective* trigger timing.
  Given this hardware has no per-step microtiming field in `Step` today
  (only `volume/prob/note/2 automation slots`), true sample-accurate
  humanize needs either (a) a new sub-step-resolution nudge encoded in
  spare bits, or (b) implemented as trigger-time jitter in the live/Replace
  or Overdub path only (never baked), since baking would require a
  `PatternData` schema change. Recommend (b) for v1 — humanize only exists
  live, and only "bakes" if the user separately samples/exports.
- `Velocity Humanize` — 0-127 — random ± offset on `pat_getStepVolume()`
  output, again live-only unless baked via the commit action.
- `Probability Humanize` — 0-127 — random jitter added to each step's
  existing `pat_getStepProbability()` before evaluating trigger.

### 4.3 SOM (existing, extended)

Already implemented: `PAR_POS_X`, `PAR_POS_Y`, `PAR_FLUX`, `PAR_SOM_FREQ`
(per-voice threshold, currently one param edited per active voice via
`menu_getActiveVoice()`, same as today). No new params proposed — its gap is
purely the dead `seq_SomModeActive` wiring (2.4), not missing controls.

### 4.4 Arpeggiator (new)

- `Pattern Style` — up / down / up-down / random / as-played (order notes
  are read from the pattern's active steps).
- `Octave Range` — 1-4.
- `Rate/Division` — synced to existing sub-step clock (1/8, 1/16, 1/32 of
  the existing 128-step/16-main-step grid — reuse, don't reinvent, the
  substep timing already driving `seq_stepIndex`).
- `Gate Length` — 0-127 (percentage of step slot).
- `Note Source` — pattern notes (`pat_getStepNote` pool from active steps)
  vs. fixed scale/root (needs a scale table — none currently exists in the
  repo; would be new).
- `Gate Source` — which steps trigger: pattern's existing main-step mask
  (`pat_isMainStepActive`) vs. every Rate-th tick regardless of pattern.

### 4.5 Markov (new)

- `Order` — 1st/2nd order transition table (how many prior steps condition
  the next).
- `Learn Source` — current pattern / current track / all 7 tracks pooled.
  Triggers a training pass over `pat_readStep` for the viewed pattern.
- `Density` — 0-127, bias added to transition probabilities toward more/
  fewer active steps than the learned corpus.
- `Temperature` — 0-127, 0 = always pick highest-probability next step
  (deterministic replay of learned pattern), 127 = uniform random (ignores
  learning).
- Uses `PAR_GEN_SEED` (4.common) to reseed the RNG stream (`GetRngValue()`,
  already used by SOM) for a fresh walk without relearning.

### 4.6 Genetic (new)

- `Population Size` — small fixed range (e.g. 4-16, given STM32F765 RAM/CPU
  budget — this needs a real budget check before committing to numbers;
  flagged as an open question in §5).
- `Mutation Rate` — 0-127.
- `Crossover Rate` — 0-127.
- `Fitness Target` — selects which heuristic scores candidates: density
  match to seed, syncopation, groove/downbeat weight, or a simple
  "similarity to current pattern" (so low = wander far, high = stay close
  to the seed pattern read in 2.5).
- `Evolve` — action/pulse param (button-style, like `PAR_GEN_SEED` but
  type-specific semantics: advances one generation rather than reseeding)
  — advances the population by one generation each press/pulse rather than
  running continuously, since continuous background GA evaluation on an
  audio-rate MCU is a real CPU-budget risk (see §5).
- `Generation` — read-only display of current generation count.

## 5. Open questions / risks

1. **CPU budget** (constant worst-case policy; §2.6 item 6).
   `sequencer.c`'s step loop runs at audio-adjacent timing
   on the STM32F765. SOM's `som_tick()` only actually computes on every 8th
   step (`if(stepNr%8 != 0) return;`) — that budget precedent should apply
   to Markov/Genetic too; neither should do real work every single
   sub-step. Genetic in particular (population scoring) should not run
   inside the audio/sequencer ISR path at all — `Evolve` should likely be
   deferred to a lower-priority background tick, not `gen_tick()` itself.
2. **Per-voice vs. global scope mismatch** (2.3) — SOM/Genetic naturally
   want all tracks, Euklid/Rotate/Arp/Markov naturally want one track. The
   unified page's `PAR_GEN_TARGET` needs UI treatment that makes this
   difference legible (e.g., forcing/greying the target selector to "All"
   and lighting all voice LEDs when Type is SOM or Genetic).
3. **Menu page table is currently fully `const`.** Making Generator's
   sub-pages 1+ swap by type is the first non-static page in the menu
   system (§2.2) — this is more invasive than adding a page, and is worth
   confirming as acceptable scope before implementation starts. **S085
   precedent:** keep the tables const and drive labels, value text and
   ranges from a per-type formatter (§2.6 item 1, option (c)).
4. **Overdub is entirely new territory.** Nothing in the current engine
   triggers two independent sources (stored pattern + generator) into the
   same voice on the same step without one silencing the other — needs a
   small but real change to the trigger dispatch in `sequencer.c`'s
   per-track loop (currently a single `if/else` between muted-pattern-
   playback and `som_tick`), not just new generator code.
5. **Humanize/timing-jitter has no home in `Step` today** — recommend
   living entirely in the live/Overdub/Replace path (never serialized) for
   v1 rather than extending the `Step` struct, which is
   filesystem-format-visible per `MODULE_INTERCHANGE_SPEC.md`'s stated risk
   note.
6. **Bake/commit is a new shared action** and needs a physical control
   assignment (long-press? shift-combo?) — not specified by the request,
   flagged for product decision.

## 6. Suggested phasing

1. Wire up the missing pieces first, no new UI: fix `seq_SomModeActive` so
   SOM's existing Replace-style behavior actually works from a real toggle.
2. Collapse `EUKLID_PAGE`/`SOM_PAGE` into `GENERATOR_PAGE` with the
   Type/Output-Mode/Target/Mix header row (§3), Off/Replace only (defer
   Overdub — it's the riskiest sequencer change, §5.4).
3. Add Rotate/Spread/Humanize (reuses existing pattern-read APIs, no new
   engine concepts, good validation of the "current pattern as input"
   plumbing).
4. Add Overdub mode once the header row / type switch is proven.
5. Add Arpeggiator, then Markov, then Genetic, in roughly increasing order
   of new-concept risk (scale tables, learned corpora, background-tick
   evaluation).
