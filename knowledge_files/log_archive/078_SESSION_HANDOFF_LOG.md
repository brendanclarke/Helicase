# Session 078 — Handoff Log

```
DATE: 2026-10-08 .. 2026-10-10
SESSION GOAL: Track settings expansion (S078 P1): continuous 128-position
              per-track step scale with fractional (Q8.8 DDA) timing, FX
              sequencer migration to the same curve, per-track shuffle,
              per-track run (play) modes, and morphable/automatable track
              length/scale/shuffle. Then STEP-mode multi-step editing and
              held-step track automation (P2), its underline/clear follow-up
              (P3), and the run-mode remediation (P4). Close out the S076/S077
              retest checklist.
COMPLETED: P1–P4 implemented, building clean, and all four hardware
           tested PASS (user, 2026-10-10). Retest checklist P4/P5/P6
           sections (S076 P4, S077 P1, S077 P2) all PASS.
VERIFIED ON HARDWARE:
  - S078 P3 §7 rows 1–14: all PASS (STEP underline + pot clear, held-step
    overlay write/clear, step-automation editor restarts, track/Scene change,
    SHIFT press/release, subpage-1 and second-half pot-clear refusal,
    copy/clear refresh, VOICE/Effect/PERF regression).
  - S078_RETEST_CHECKLIST.md: 4.1–4.20 (S076 P4 reload scene, bar
    chaselight, SHIFT+SELECT length), 5.1–5.11 (S077 P1 background region /
    snapshot gate), 6.1–6.42 (S077 P2 per-track Scene playback, incl.
    double-click 6.23–6.27): all PASS.
  - S078 P1: checked and PASS (user, 2026-10-10; test focus §2.11). The
    run-mode defect found during P1 use was fixed in P4.
  - S078 P2 (§A multi-step specials broadcast and §B held-step overlay):
    checked and PASS (user, 2026-10-10).
  - S078 P4: checked and PASS (user, 2026-10-10; test plan §5.7).

CHANGES THIS SESSION (detail in §2–§5):
- Core/Sequencer/StepScale.c/.h: 128-entry Q8.8 LUT, 14 musical stops,
  stepScale_ticksQ8()/shortName()/longName()/isMusicalStop()/formatShort()
- Core/Sequencer/sequencer.c/.h: per-track DDA accumulators, FX DDA +
  step counter, per-track shuffle deferral, play modes + play state,
  effective (morphed) length/scale/shuffle cache + refresh, track step
  automation apply/restore, Q8.8 realignment incl. seq_stepIndexForMode()
- Core/Bank/Scene/Pattern/PatternData.c/.h: track_play_mode[7] (+setter,
  init, menu apply, region copy; region assert 23 -> 30)
- Core/Bank/Scene/SceneData.c/.h: track_morph_length/scale/shuffle[7] +
  defaults/setters/getters; scene_commitSettings() carries them
- Core/Bank/Scene/Autosave.c/.h: Scene cells 51..71; COUNT/LIVE_BYTES 72
- Core/Bank/Scene/SceneModTargets.c/.h: TRACK_LENGTH/SCALE/SHUFFLE kinds,
  21 rows (IDs 405..425)
- Core/Bank/Scene/Preset/presetMorphEngine.c/.h: track effective getters,
  interpolation, step-automation overlay mask/values
- Core/Bank/Scene/Preset/ParameterArray.h: PAR_TRACK_PLAY_MODE
- Core/DSP/Effects/CrumpBit/*: StepScale API migration
- Core/Hardware/SD/filesystem.c, storageTypes.c: PAT4 track header byte 3,
  sceneset track_morph_* lines, .fx step_scale token->CC map
- Core/Hardware/frontPanel/buttonHandler.c: STEP SHIFT Morph view
  press/release; held-steps + TRACK -> STEP track automation overlay
- Core/Menu/menu.c/.h, MenuText.h, menuPages.h, menuEffects.c: SEQ page
  layout, scale/run-mode display, STEP Morph view, multi-step broadcast,
  STEP held-step overlay, STEP underline search, STEP pot clear, run-mode
  relabel, Morph-view rule
- Core/Menu/CopyClear/copyOps.c, clearOps.c, copyClearService.c,
  copyClearSession.c/.h: track play mode copy/clear, track Morph endpoint
  copy/reset, copyClear_isClearMode()
- tools/convert_scene_scale.py: offline PAT4/.fx scale migration (new)
- knowledge_files/specification_reference/*: see §9

KNOWN ISSUES INTRODUCED:
- STEP-page marker retry gap (found S078, not fixed): the deferred CGRAM
  marker retry in menu_serviceRuntimeWidgets() (menu.c ~13329) admits
  VOICE/Effect/PERF pages only. A STEP-page underline deferred for lack of
  LCD queue room stays missing until an unrelated repaint. One-condition fix
  (`|| menu_activePage == SEQ_PAGE`) offered, not applied.
- Old PAT4 track_scale bytes (0..13, old 14-index) play near the bottom of
  the new curve (very fast). Intended (no on-device migration);
  tools/convert_scene_scale.py migrates cards.
KNOWN ISSUES RESOLVED:
- Per-track step scale and shuffle had no playback effect since S031/S068
  (PATTERN_DYNAMIC_STACK §6.4 "deferred", SCOPING A10/§6.0). Implemented.
- STEP track-parameter automation names never underlined (P3 Bug 1a/1b).
- STEP step-automation editor never restarted the underline search (P3 1c,
  dead `menu_isVoicePage()` guards).
- SHIFT+COPY + pot never cleared track-parameter automation (P3 Bug 2).
- STEP SHIFT Morph view stuck on after SHIFT+mode-button exits, busy page
  refusals, or dropped SHIFT releases (P4 Defect 1).
- Run-mode cell blanked (`---`) and locked in the Morph view (P4 Defect 2).
- Run-mode label `mod`/`Pattern`/`PlayMode` (P4 Defect 3).

NEXT SESSION RECOMMENDED GOAL:
1. Decide on the STEP marker retry one-liner (§6.1).
2. Retest items still blank: 3.10/3.14/3.15 (S077 P4 Scene morph fan-out),
   C1–C6; S077 P5 (bar-to-step) and P6 (PERF morph underline) have never had
   checklist rows.
3. Re-measure the production build (DEV_MODE_LOGGING 0).
4. Agree the next feature/fix target with the user.

BLOCKERS: none.

CRITICAL REMINDERS FOR NEXT SESSION:
- SHIFT Morph view rule (user, S078 P4, project-wide): in ANY SHIFT Morph
  view a non-morphable parameter shows and edits its Normal value; it is
  never blanked (`---`) or locked. VOICE is the reference behaviour.
- The STEP Morph view is momentary: menu_patternTrackMorphViewActive()
  requires the flag AND SEQ_PAGE AND buttonHandler_getShift(). Do not
  remove the physical-SHIFT term.
- Track timing is read from the effective cache
  (seq_effectiveTrackLength/Scale/Shuffle[]); the retained Pattern Normal
  values are never written by Morph or automation, and the Pattern dirty bit
  is never raised by them.
- seq_setStepIndexToStart() seeds each track accumulator to interval - 256
  so step zero fires on the first tick; do not seed 0.
- Shuffle delay is (shuffle * 24) / 256 PPQ ticks on the 1/16 grid, stored
  as delay - 1 (tick-down decrements then fires at 0).
- Run mode display cannot use DTYPE_MENU (all 16 table ids used) or a new
  DTYPE (all 16 nibbles used): it is DTYPE_0B127 + static-param special
  cases keyed on MENU_CELL_STATIC + PAR_TRACK_PLAY_MODE.
- SHIFT+STEP is the only SOM entry ((2+4)&7 = 6); no STEP Morph latch there.
- STEP search bits are VA_SEARCH_SCENE_TRACK_BIT(0..2) = 0x10/0x20/0x40 in
  va_searchSceneMask; set and clear only through va_seqTrackSearchBit().
- menu_automationTargetCleared() is for whole-Scene removals only;
  held-step-only removals restart the search instead.
- The step-automation editor runs only on SEQ_PAGE; its search restarts are
  unconditional.
- The root S078_*.md task documents are superseded by this log and the
  updated specs (list in §10).
```

---

## 1. Session overview

Session 078 ran on `dev-ph6-cleanup` from the S077 close (`8c123fb`; text
538,416, data 416, bss 427,008) on 2026-10-08 to 2026-10-10.

| Commit | Content |
|--------|---------|
| `4dcc07f` | S078 pre-implementation plan |
| `0c23def` | S078 P1 implementation (Steps 1–7 incl. Step 5 Morphability); hardware PASS at the close |
| `b537e6f` | S078 P2 + P3 (multi-step broadcast, STEP held-step overlay, underline/clear fix) plus the `SD_CARD_S078/` card tree |
| uncommitted | S078 P4 (`menu.c`, `menu.h`, `MenuText.h`), checklist/test results, root task documents, this close-out |

The user manages commits.

### Build progression (DEV config)

| Point | text | data | bss | Notes |
|-------|-----:|-----:|----:|-------|
| S077 close | 538,416 | 416 | 427,008 | baseline |
| P1 Steps 1–4, 6, 7 (clean rebuild) | 534,808 | 420 | 427,200 | net text −3,608 B: LTO layout/de-unroll artefact (CrumpBit division walk became a 128-iteration loop) |
| P1 incl. Step 5 (`0c23def`) | 537,592 | 420 | 427,608 | +2,784 text for Morphability |
| P2 | 539,880 | 412 | 427,608 | +2,288 text; bss +0 (`sa_trackPresenceMask` absorbed in padding) |
| P3 | 539,952 | 416 | 427,616 | +72 text; no new static symbol (nm), `sa_trackPresenceMask` removed; the +4 data/+8 bss is LTO section placement, not investigated |
| P3 comment fixes | 539,952 | 416 | 427,616 | identical |
| **P4 (final)** | **539,856** | **416** | **427,616** | −96 text |

Final: flash payload **540,272 B of 753,664 B, headroom 213,392 B**; ITCM
4,168 / 16,384 B; DTCM statics 4,472 B; FX arena 126,592 B at 0x20001180
(margin 3,712). `build/LXRV2_lxr02.img` 540,384 B, SHA-256
`ecc0eb0b…3bd8bd`. Production build (`DEV_MODE_LOGGING 0`) not re-measured
since S075 F1.

---

## 2. P1 — Track Settings Expansion

Source documents: `S078_P1_TRACK_SETTINGS.md` (accepted proposal) and
`S078_P1_IMPLEMENTATION.md` (schedule, Codex progress log, Step 5 log, code
review). Before S078, only `track_length` affected playback (S068);
`track_scale`/`track_shuffle` were stored, edited and persisted, but every
track advanced on one global 24-PPQ-tick divisor
(`SEQ_INTERNAL_TICKS_PER_DEFAULT_STEP`).

### 2.1 Step scale: continuous 128-position log curve

**Model.** A single byte 0..127 ("CC") replaces the old 14-entry discrete
index (`STEP_SCALE_COUNT` 14 → **128**, `STEP_SCALE_DEFAULT` 4 → **76**).
Raw curve:

    multiplier(cc) = 0.25 × 2^(cc / (127/5))      // 0.25× (1/64) .. 8× (1/2)

relative to the default 1/16 step (24 ticks at 96 PPQ).

**Musical stops** (nudged exactly onto musical values, from
`step_scale_0_127.csv`; all other positions use the raw curve):

| CC | Multiplier | Musical | Short | Long |
|---:|---:|---|---|---|
| 0 | 0.25 | 1/64 | `/64` | `1/64` |
| 16 | 0.333… | 1/32 triplet | `32t` | `1/32T` |
| 38 | 0.5 | 1/32 | `/32` | `1/32` |
| 54 | 0.667… | 1/16 triplet | `16t` | `1/16T` |
| 60 | 0.75 | dotted 1/32 | `d32` | `d 1/32` |
| 76 | 1.0 | 1/16 (default) | `/16` | `1/16` |
| 83 | 1.333… | 1/8 triplet | `8Tr` | `1/8T` |
| 86 | 1.5 | dotted 1/16 | `d16` | `d 1/16` |
| 93 | 2.0 | 1/8 | `/8 ` | `1/8` |
| 100 | 2.667… | 1/4 triplet | `4Tr` | `1/4T` |
| 103 | 3.0 | dotted 1/8 | `d/8` | `d 1/8` |
| 110 | 4.0 | 1/4 | `/4 ` | `1/4` |
| 120 | 6.0 | dotted 1/4 | `d/4` | `d 1/4` |
| 127 | 8.0 | 1/2 | `/2 ` | `1/2` |

**Display.** A musical stop shows its symbolic short name; every other
position shows the raw value right-justified in three characters (CC 0 is
always `/64`, so a bare `0` never appears). Decimal multipliers are never
shown. `stepScale_shortName()`/`longName()` return NULL for non-stops;
`stepScale_formatShort()` owns the symbolic-or-decimal fallback and is shared
by Menu (`MENU_TRACK_SCALE`), menuEffects and CrumpBit.
`stepScale_isMusicalStop()` answers stop membership.

**LUT.** `stepScale_q8Table[128]` (256 B flash): `round(multiplier × 24 ×
256)` in Q8.8 (min 1,536 at CC 0, max 49,152 at CC 127; both fit `uint16_t`).
`stepScale_ticksQ8(cc)` returns the entry; out-of-range returns the CC 76
default (0x1800 = 24.0 ticks). ISR-safe, flash-only.

**Per-track DDA** (`seq_processSchedulerTick()`): every PPQ tick, each track
adds 256 (1.0 Q8.8) to `seq_trackAccumulator[track]` (`uint16_t[7]`, 14 B);
`while (acc >= interval) { acc -= interval; seq_advanceTrackStep(track); }`.
The fractional remainder carries, so the average rate is exact with no
drift. The `while` runs at most once per tick (min interval 1,536 > 256).
Cost: 7 adds + 7 compares per tick. The interval comes from
`seq_effectiveTrackScale[track]` (§2.5), not the region byte directly.
The master 1/16 counter (`seq_masterStepClock`/`seq_masterStepCnt`) is still
derived from the 24-tick modulo; it drives bar boundaries, bar display,
trigger clock and MIDI beat, but no longer track advance.

**Stage 1/Stage 2.** The plan's integer-only Stage 1 was not kept as a
separate code state; the shipped LUT is the true fractional Q8.8 table.
Stage 1 timing can be re-validated by zeroing the low bytes temporarily.

**Deviations (Step 1):**

1. **Immediate step zero.** A zero seed would lose step 0 (the first tick adds
   256 and cannot reach 1,536). `seq_setStepIndexToStart()` seeds each
   accumulator to `interval − 256`, so the first tick fires exactly once with
   zero remainder (fires at elapsed 0, interval, 2×interval, …).
2. **Mode-aware realignment.** `seq_realignActivePatternToMasterClock()` and
   `seq_realignTrackToMasterClock()` set the Q8.8 phase
   (`(seq_elapsedPpqTicks << 8) % interval`) and derive the step from the same
   Q8.8 timeline through `seq_stepIndexForMode()`, so rev/pip/onc/1fr land on
   the step their mode would have reached; pip sets its direction bit from the
   mapped cycle phase.
3. **Stopped once-mode tracks are not realigned** (§2.4 retrigger rules).
4. **Menu clamp.** `getMaxEntriesForMenu(MENU_TRACK_SCALE)` returns
   `STEP_SCALE_COUNT` (the clamp maps `>= n` to `n−1`, giving 127); the
   plan's `STEP_SCALE_COUNT − 1` would have capped at 126.

**Storage/defaults.** `region->track_scale[track]` stays one byte;
`TRACK_SCALE_DEFAULT` aliases `STEP_SCALE_DEFAULT` (now 76). **No on-device
migration**: old PAT4 bytes 0..13 land near the bottom of the curve (§2.6).

### 2.2 FX sequencer migration (Stage 3)

- `seq_fxAccumulator` (`uint16_t`, 2 B) and `seq_fxStepCounter` (`uint32_t`,
  4 B). `seq_fxClockTick()` uses the same Q8.8 DDA and a monotonic step
  counter (fractional intervals make `elapsed / ticks` ambiguous); the
  fwd/rev/pip/rnd/sel index is derived from the counter. Seeded/reset in
  `seq_setStepIndexToStart()`.
- `EFFECT_SEQ_SCALE_COUNT`/`DEFAULT` alias StepScale (now 128/76); the
  `EffectsManager.c` static assert still holds.
- **`.fx` `step_scale` is token-based**, so the no-migration rule does not
  apply directly. `storageTypes.c` keeps the original 14 tokens, each mapped
  to its CC (`storage_effectScaleTokens[]`):
  `1/64`→0, `1/32t`→16, `1/32`→38, `1/16t`→54, `1/16`→76, `1/8t`→83,
  `1/16.`→86, `1/8`→93, `1/4t`→100, `1/8.`→103, `1/4`→110, `1/2`→127,
  `1bar`→127, `2bar`→127 (clamped: beyond the 8× maximum). The parser also
  accepts a decimal CC 0..127; the writer emits the token when one exists,
  decimal otherwise. Old `.fx` files load at the same musical division.

### 2.3 Per-track shuffle

- Shuffle delays every odd-indexed step (0-indexed 1, 3, 5, …; the "even"
  musical steps) by `(shuffle × 24) / 256` PPQ ticks (0..11 ticks). The delay
  is always on the **1/16 grid** (24 ticks), never the track's scale: on a
  slow track it is proportionally tiny (musically correct; S078 §3.3).
- State (`sequencer.c`, 28 B): `seq_trackShuffleDelay[7]`,
  `seq_trackShufflePending[7]`, `seq_trackShuffleVel[7]`,
  `seq_trackShuffleNote[7]`.
- `seq_advanceTrackStep()` defers the trigger (stores velocity/note) instead
  of calling `seq_triggerVoice()`; `seq_processShuffleDelays()` runs each
  tick **before** the DDA advance, decrements, and fires at 0.
- **Off-by-one fix (review):** the counter is set to `delay − 1` (decrement
  then fire at 0 would otherwise give N+1 ticks); safe because `delay > 0`.
- A pending deferral is flushed before a new one is set (a fast track cannot
  lose an un-fired step). A pending deferral fires even if the track has
  stopped (S078 §10 decision 2).
- The value used is `seq_effectiveTrackShuffle[track]` (§2.5). Shuffle is
  orthogonal to length and scale.

### 2.4 Run (play) modes

`pat_scene_region_t::track_play_mode[7]` (PAT4 track header byte 3; region
`_Static_assert` 23 → 30 in `PatternData.h` and `.c`; 7 B × 16 Scenes =
112 B). Values ≥ 6 are treated as `fwd`.

| Value | Token | Behaviour |
|---:|---|---|
| 0 | `fwd` | Default. 0→…→L−1→0 |
| 1 | `rev` | L−1→…→0→L−1 |
| 2 | `pip` | Ping-pong, ends play twice: 0..L−1, L−1..0, 0… (cycle 2L; default 16 syncs to 2 bars); same formula as the FX sequencer |
| 3 | `rnd` | `(GetRngValue() & 0x7FFF) % L` each boundary |
| 4 | `onc` | Once, synced: enters aligned to the master clock, plays forward once, then stops |
| 5 | `1fr` | Once, free: always starts at step 0, plays once, stops |

- `seq_trackPlayState[7]` (7 B): bit 0 `SEQ_PLAY_STATE_STOPPED`, bit 1
  `SEQ_PLAY_STATE_PIP_REV`. Pip uses the direction bit plus the index (no
  6-bit cycle counter: a 128-step track needs a 256-step cycle).
- The DDA owns **when** a step fires; the run mode owns **where** the index
  goes. A Morph sweep that shortens the effective length parks an
  out-of-range index at L−1 before the mode advance (fwd then wraps to 0;
  rev/pip cannot crawl back over many steps).
- **Retrigger (clears STOPPED):** global Scene change
  (`seq_selectActivePattern()`, `seq_alignActivePatternToScene()`),
  per-track Scene reassignment (`seq_setTrackPlayedScene()` — that track only;
  voice 6 assigns tracks 6 and 7 together), transport start/stop and Pattern
  boundaries (via `seq_setStepIndexToStart()`). **Not** the double-click
  realign (stopped once-tracks are skipped) and not a reassignment of a
  different track.
- Persistence: PAT4 writer `header[base + 3] = track_play_mode`; three
  readers (root Pattern Load, Scene Load Pattern, AutoSave Pattern boot).
  Old files carry 0 there = `fwd`.
- Copy/clear: track copy carries it (`copyClearService.c`), track clear
  resets it to 0. Menu: `PAR_TRACK_PLAY_MODE` (`ParameterArray.h`, after
  `PAR_TRACK_MIDI_NOTE`, below `PAR_BEGINNING_OF_GLOBALS`; dispatch reaches
  `menu_parseGlobalParam()` because it is above `END_OF_SOUND_PARAMETERS`),
  `pat_setTrackPlayMode()` (marks the Scene dirty),
  `pat_applyTrackSettingsToMenu()`.
- **Display deviation.** `DTYPE_MENU` packs its table id in the high nibble
  and ids 0..15 are all used; a new `DTYPE_*` is impossible because all 16
  nibble values are used and every formatter unpacks `dtype & 0x0f`. The cell
  is `DTYPE_0B127` with static-param special cases keyed on
  `MENU_CELL_STATIC + PAR_TRACK_PLAY_MODE` in `va_formatValue3()`,
  `menu_formatCellValue3()`, `menu_clampCellValue()` (0..5) and the
  `menu_repaintGeneric()` edit painter, all via `menu_getPlayModeName()` and
  `trackPlayModeNames[]` (MenuText.h). Text ids `TEXT_TRACK_PLAY_MODE`,
  `SHORT_PLAY_MODE`, `LONG_PLAY_MODE` were appended (no index moved). The
  label was later renamed (§5).
- **SEQ_PAGE subpage-0 layout** (menuPages.h:89):
  `len | scl | shf | run | mch | not | --- | ---` (morphable cells grouped
  left; two spare cells). TRACK press toggles halves (cells 0..3 / 4..7).

### 2.5 Morphability — effective-getter architecture

**Rule:** Length, scale and shuffle are Scene parameters for Morph purposes.
The Pattern region stores Normal; the Scene stores Morph endpoints; nothing
in Morph or automation writes the region or marks the Pattern dirty (the
S075 FX-send Morph pattern).

- **Endpoints:** `scene_settings_t` gains `track_morph_length/scale/shuffle
  [NUM_TRACKS]` (21 B per Scene, 336 B over 16). Defaults 16/76/0
  (`scene_settingsDefaults()`, `filesystem_initSceneStage()`), so Morph
  changes nothing until endpoints are edited. Setters
  `scene_setTrackMorphLength/Scale/Shuffle()` clamp and go through the
  change-aware `scene_storeParameterByte()` funnel (AutoSave cell marked);
  matching getters. `scene_commitSettings()` carries them, so Scene/settings
  copy/clear include them.
- **AutoSave:** Scene parameter cells 51..57 (length), 58..64 (scale),
  65..71 (shuffle); `AUTOSAVE_SCENE_PARAM_COUNT` and
  `AUTOSAVE_SCENE_PARAMETER_LIVE_BYTES` 51 → **72** (≤ 118 capacity). A zero
  length cell (pre-S078 record) is skipped, leaving the fresh default. No
  format version bump (user deletes AutoSave files after this change).
- **Effective cache:** `seq_effectiveTrackLength/Scale/Shuffle[7]` (21 B,
  file-static in `sequencer.c`). `seq_refreshTrackEffectiveParams()`
  recomputes them per track from each track's **played Scene**
  (`seq_perTrackPattern[track]`, S077 P2) via
  `presetMorph_getTrackEffectiveLength/Scale/Shuffle()` (presetMorphEngine.c,
  which own interpolation and the automation overlay check). Read by
  `seq_advanceTrackStep()`, `seq_processSchedulerTick()`, both realign
  helpers and the DDA seed.
- **Refresh points:** `presetMorph_tick()` every foreground worker tick
  **before** its `!active` early-out (endpoint edits propagate within one
  main-loop pass, so no `preset_rebuildMorph()` call is needed — plan spec
  superseded), `seq_init()`, `seq_selectActivePattern()`,
  `seq_alignActivePatternToScene()`, `seq_setTrackPlayedScene()`,
  `seq_clearPerTrackOverrides()`, `seq_setStepIndexToStart()`.
- **Interpolation:** `presetMorph_interpTrack()`:
  `(normal × (255 − amount) + morph × amount + 127) / 255` in unsigned 16-bit
  (identical to `presetMorph_interpolate()` for this domain; amount 0 =
  Normal, 255 = Morph). Length result floored at 1.
- **Amount source:** the retained `voice_morph_amount[slot]` of the track's
  voice (tracks 1–5 → voices 1–5; tracks 6 and 7 → voice 6), not the
  LFO/step-resolved amount, so voice-Morph step automation does not change
  track timing. No separate per-track Morph amount.
- **Step automation:** three Scene target kinds
  `SCENE_MOD_TARGET_KIND_TRACK_LENGTH/SCALE/SHUFFLE`, 21 rows
  `SCENE_MOD_TARGET_ID(21..41)` = IDs **405..411 length, 412..418 scale,
  419..425 shuffle**, `voice_slot` = track 0..6, length min 1 max 128, scale
  and shuffle 0..127, `SCENE_MOD_TARGET_USE_AUTOMATION` only (offered in the
  step-automation editor's `scn` category). `seq_applySceneAutomation()`
  calls `presetMorph_setTrackParamStepOverride()` (mask bit + value; 21 value
  + 3 mask bytes = 24 B) and writes the effective cache directly so the next
  DDA tick follows. `seq_restoreAllSceneAutomation()` calls
  `presetMorph_clearAllTrackParamStepOverrides()`; `seq_setStepIndexToStart()`
  then recomputes the cache. Overrides are sticky until transport stop or a
  Pattern restore, like every step automation. Storage is 7-bit, so length
  128 cannot be automated (clamped to 127).
- **Persistence:** `sceneset.scg` keys `track_morph_length`,
  `track_morph_scale`, `track_morph_shuffle` (7 CSV `uint8_t` each), writer
  cases 15/16/17 appended (earlier line numbers unchanged); missing keys keep
  the staged 16/76/0 defaults.
- **STEP SHIFT Morph view:** dedicated `menu_patternTrackMorphEndpoint` flag
  + `menu_setPatternTrackMorphEndpoint()` (parallel to, not widening,
  `voiceModeShowMorph`). SHIFT press in STEP sets it, release clears it.
  `menu_getParameterDisplayValue()` and `menu_cellCommitValue()` redirect
  only `PAR_TRACK_LENGTH/SCALE/SHUFFLE` to the viewed Scene's endpoints
  (`menu_patternTrackMorphEndpointActive()`); encoder and pots both converge
  there. P1 blanked/locked the run cell in this view; **P4 removed that and
  gated the view on physical SHIFT** (§5).
- **No SHIFT+STEP latch:** SHIFT+MODE_STEP maps to `SELECT_MODE_SOM_GEN`
  (`(2+4)&7`), the only SOM entry; latching there would make SOM unreachable.
- **Copy/clear:** `copy track morph` (`ccCopy_runMorphTrack`) and
  `clear track reset morph` (`ccClear_runResetMorphTrack`) set the
  destination track's `track_morph_*` from the source/own Pattern Normal;
  `copy scene -> morph` (`ccCopy_runSceneMorph`) and `clear scene reset morph`
  (`ccClear_runResetSceneMorph`) do all seven tracks (fan-out per S077 P4).

### 2.6 Conversion utility — `tools/convert_scene_scale.py`

Offline migration: remaps the seven `track_scale` bytes of PAT4 files and
recomputes the header CRC32C (Castagnoli, reflected; exactly
`autosave_recordCrcBegin/Finish`). `.fx` files are token-based (§2.2), so
only a numeric `step_scale` 0..13 is remapped. Remap (old index → CC):
0→0, 1→16, 2→38, 3→54, 4→76, 5→83, 6→86, 7→93, 8→100, 9→103, 10→110,
11→127 (1/2), 12→127 (1 bar, clamped, warns), 13→127 (2 bars, clamped,
warns). Round-trip verified (CRC self-consistent, idempotent).

### 2.7 Bar display (Step 7) — no change needed

`menu_currentBar` is the user-selected viewed bar (written only by
`buttonHandler_selectBar()`), not a chase position. The playback bar comes
from `seq_ledState.chaseStep` = `seq_stepIndex[menu_getActiveVoice()]`
(`led_updateSelectBarChaselight()`, `led_updateCurrentStep()`), which already
advances at the viewed track's own scale.

### 2.8 CrumpBit (downstream StepScale consumer)

`crumpBit_divisionFor()`: `stepScale_ticks(i)` → `stepScale_ticksQ8(i) /
256.0f`; the nearest-division walk is now over 128 positions (CPU note in
`CPU_USE_DSP_AUDIT.md`). `crumpBit_uiFormatValue3()` uses
`stepScale_formatShort()`.

### 2.9 Resources (P1)

| Category | Bytes |
|---|---:|
| ISR-static SRAM1 Steps 1–4: accumulators 14 + FX acc 2 + FX counter 4 + shuffle 28 + play state 7 | 55 |
| ISR-static Step 5: effective arrays | 21 |
| **Total new ISR-static** | **76** |
| Morph-engine track override state (21 value + 3 mask) | 24 |
| Per-Scene settings (21 × 16) | 336 |
| Per-Scene region play mode (7 × 16) | 112 |
| **bss growth S077 → P1** | **+600** (incl. alignment) |
| Flash tables: LUT 256 + stops ~170 + run-mode names ~28 + 21 target rows | ~1.5 kB |

All approved at plan acceptance (the proposal's 72 B estimate became 76 B
measured, acknowledged in the plan).

### 2.10 Accepted decisions (S078 §10)

1. Display raw CC for non-stops; symbolic names at stops.
2. A pending shuffle deferral fires even after a once-track stops.
3. Scale change mid-playback carries the DDA remainder (no reset).
4. Pip cycle across length changes clamps and continues.
5. No on-device migration; converter offline.
6. No AutoSave version bump (user deletes AutoSave files).
7. Bar display follows the viewed track (already true, §2.7).

Other track settings surveyed in the P1 proposal and **not** in scope:
track rotation (already implemented, `PAR_TRACK_ROTATION`); per-track Scene
assignment (S077 P2, a PERF feature); external MIDI sequencing tracks
(SCOPING §6.8); per-track probability scaling and per-track swing styles
(groove templates) — unscoped ideas.

### 2.11 P1 hardware test focus — PASS (user, 2026-10-10)

1. The 14 stops match the old table timing (CC 0/16/38/54/60/76/83/86/93/
   100/103/110/120/127).
2. Two tracks at different scales stay phase-correct across Pattern and Scene
   boundaries.
3. Fractional positions (e.g. CC 50) are audibly distinct from neighbours; a
   knob sweep has no glitches; long runs show no drift against the bar.
4. Shuffle on/off per track; extremes.
5. All run modes incl. once-mode retrigger (Scene change, per-track
   reassignment, transport) and stopped tracks surviving a double-click
   realign.
6. FX sequencer at non-musical CCs and on old `.fx` tokens.
7. PAT4 round-trip of the run mode; track copy/clear carries it.
8. Old PAT4 files play fast (intended); the converter fixes them.
9. SHIFT Morph view on STEP; PERF Morph sweep with differing endpoints
   changes length/scale/shuffle live and equals Normal at amount 0.
10. `sceneset.scg` round-trip of the three lines; an old file loads 16/76/0.
11. Copy scene → morph / reset morph incl. track endpoints.
12. Step automation on `len`/`scl`/`shf` changes playback and restores at
    transport/Pattern boundaries.

---

## 3. P2 — STEP multi-step editing and held-step track automation

Source documents: `S078_P2_TRACK_SETTINGS_CLEANUP.md` (plan),
`S078_P2_IMPLEMENTATION.md` (schedule + implementation notes).

### 3.1 §A Multi-step specials broadcast

1. **Behaviour.** In STEP mode with several SEQ buttons held, adjusting
   velocity, note or probability writes the value to every held step; the
   display seeds from the last-pressed step (each press re-selects via
   `buttonHandler_selectActiveStep()`). One held step behaves as before. The
   step-automation editor (scroll right past probability) stays single-step
   on `PAR_ACTIVE_STEP`.
2. **Code.** `menu_broadcastStepSpecial(scene, track, value, setter)` with
   setter `patSvc_setStepVolume/Note/Probability` (identical
   `(uint8_t ×4) → uint8_t` signatures); popcount of
   `buttonHandler_seqHeldMask()` < 2 → single-step path; else every held bit
   via `buttonHandler_visibleStep(i)` (= `menu_currentBar × 16 + i`). The
   three `PAR_STEP_*` arms live in `menu_parseGlobalParam()` (reached from
   `menu_cellCommitValue()` → `menu_sendEditedParameter()`).
3. **Edges.** Steps without a trigger still get the special (specials and
   automation are independent of the trigger bit); a step without a dynamic
   block allocates one; pool exhaustion silently drops writes (existing
   PatternStackService behaviour). Only the visible bar's held steps are in
   the mask. RAM 0.
4. **Test status:** PASS (user, 2026-10-10). Checks: one held step
   (regression), two/three held steps for velocity/note/probability, held
   steps on another bar excluded, automation editor single-step, near-full
   pool.

### 3.2 §B Held-step track automation overlay (as built, after P3)

- **Gesture.** STEP mode: hold SEQ steps → press TRACK →
  `handleVoiceButton()` (STEP arm, `buttonHandler_seqHeldMask() != 0`) calls
  `menu_enterStepTrackAutomationOverlay(voiceNr)` instead of the ordinary
  half toggle. Entry: `menu_setActiveVoice()`, switch to SEQ_PAGE if needed,
  first half, `pat_applyTrackSettingsToMenu()`, `editModeActive = 0`, arm
  `va_overlayActive`, seed the held order (`va_updateHeldState()`), LEDs,
  repaint.
- **Shared state.** The overlay reuses the VOICE overlay's va_* block (46 B,
  `_Static_assert`): held mask/order/count, overlay flag, debounce,
  working values, CGRAM cache, search. STEP and VOICE are mutually exclusive
  by page; `menu_switchPage()` resets the shared state when leaving a voice
  page **or SEQ_PAGE** (P2 deviation 3); `va_underlineService()` admits
  `SEQ_PAGE && va_overlayActive` (deviation 4).
- **Targets.** `menu_seqCellToTrackTarget(cell, track)`: cell 0 → 405+t,
  1 → 412+t, 2 → 419+t, else `INSTRUMENT_PARAM_INVALID`; computed from the
  seven-row-per-kind layout and re-validated through
  `sceneModTarget_descriptor()` (kind + `voice_slot`), because
  `SCENE_MOD_TARGET_ID()` is private to SceneModTargets.c (no
  `_Static_assert` possible in menu.c).
- **Write.** `sa_writeAutomationFromKnob(cellPos, delta)`: pots via the
  `menu_parseKnobDelta()` intercept (before `copyClear_ownsPots()`, absolute
  cell = `knobNr + half offset`), encoder via `menu_encoderChangeParameter()`
  (before the VOICE overlay check). Seed: working value if valid, else the
  newest held automation value (`va_resolveHeldValue()`), else the retained
  display value; `menu_clampCellValue()`; working cache; 7-bit store
  `min(value, 127)`; `patSvc_writeStepAutomation()` per held step; sets the
  STEP search bit (P3) and edit-flash suppression. Non-automatable cells
  (run, mch, not) fall through to ordinary retained editing.
- **Clear.** SHIFT+COPY held + pot over `len`/`scl`/`shf` while steps are
  held: `copyClear_isClearMode()` (new: `cc_state.phase == CC_OP_CLEAR`) and
  `!copyClear_menuVisible()` → `sa_clearAutomationFromKnob()` removes the
  target from the held steps only (`patSvc_removeStepAutomation()`),
  restarts the search (P3), refreshes LEDs. Copy mode: pots inert.
  Encoder-based clear: none (copy/clear encoder is menu-only).
- **LEDs.** `sa_refreshAutomationLeds()`: clicked into an automatable cell →
  `led_updateAutomationStepView(track, scene, target, held mask)` (automated
  steps lit, held steps blink); else ordinary pattern view.
- **Exit.** All steps released (`va_updateHeldState()`), page/mode change,
  track change.
- **Markers.** `sa_applyTrackMarkers()` — final form in §4.4.
- **Deviations from the P2 plan (all kept):** commit-arm owner one level
  down; no static assert (descriptor re-validation); SEQ exit reset;
  underline-service admission; presence refresh on mask change (later
  removed by P3); pot half-page offset; clear-menu guard; full VOICE-style
  value display; entry resets `editModeActive`; `va_formatValue3()` instead
  of the non-existent `menu_formatCellValue()`. `PatternStackService` was not
  changed (`pat_readStepAutomations()`/`va_resolveHeldValue()` suffice).
- **Resources:** +2,288 B text; bss +0 (the 1 B `sa_trackPresenceMask` sat in
  padding; P3 removed it).
- **P2 test matrix (§B.19; rows 1–8 and 13 were exercised by the P3 hardware
  tests):** hold 1 step → TRACK 1 → adjust length (automation written); hold
  3 steps → TRACK 2 → adjust scale (all 3 written) and check the underline;
  SHIFT+COPY + turn the knob (removed from held steps, underline drops);
  release all steps (overlay exits); adjust run mode / MIDI channel in the
  overlay (ordinary retained edit, no automation); transport running (the
  automation fires at those steps); single/multi-step velocity and note
  broadcast; automation editor stays single-step; VOICE mode hold steps +
  TRACK enters the VOICE overlay, not the STEP one.

---

## 4. P3 — STEP underline and pot-clear follow-up

Source documents: `S078_P3_TRACK_PAR_AUTOM_FOLLOWUP.md` (root-cause plan),
`S078_P3_IMPLEMENTATION.md` (schedule M1–M26/H1–H3/D1–D2, progress notes,
review §10).

### 4.1 Root causes

- **1a (dead name branch).** `sa_applyTrackMarkers()` underlined the name
  only in an `else if` after `va_resolveHeldValue()` succeeded; its
  `sa_trackPresenceMask` bits were set only by `sa_refreshPresence()` (same
  predicate) and by a write (which makes the predicate true), so the branch
  could never run.
- **1b (no search on SEQ_PAGE).** The marker pass returned without the
  overlay, and `va_scanService()` ran only on VOICE, Effect and PERF pages.
- **1c (dead editor restarts).** `menu_stepAutomationExecuteItem0()` restarted
  the search only `if (menu_isVoicePage(...))`, but the editor runs only on
  SEQ_PAGE (`menu_stepAutomationPageActive()`); target changes never
  restarted it.
- **Bug 2 (pot clear).** Without held steps the overlay intercept is skipped;
  `menu_knobClearTarget()`'s `MENU_CELL_STATIC` arm handled only
  `PAR_VOICE1..6_MORPH` and `PAR_EFFECT_MORPH`, so `copyClear_potTurned()` was
  never called for track cells. (The held-step clear path was fine.)
- Ruled out by reading: SHIFT resetting the overlay, missing underline
  glyphs (digits/letters are in the WS0010 table; `/` is not but the
  rightmost non-space is chosen), wrong half page.

### 4.2 Fix summary (as implemented)

- **Search bits:** `VA_SEARCH_SCENE_TRACK_BIT(cell)` = `0x10 << cell` (0x10
  `len`, 0x20 `scl`, 0x40 `shf`) in `va_searchSceneMask` (disjoint from
  VOICE 0x01–0x04, Effect 0x08; PERF has its own mask).
- **Classifier:** `va_seqTrackSearchBit(target, track)` (descriptor kind +
  `voice_slot`), used both to set (scan) and clear (pot clear).
- **Scan:** `va_scanService()` `seq_page` branch, VOICE geometry (active
  track, track-mismatch restart), `continue` before the VOICE slot
  classification; runs on every SEQ subpage (`menu_serviceRuntimeWidgets()`:
  held state → scan → debounce).
- **Restarts** (a completed search is never re-validated by
  `va_scanService()`): SEQ entry from another page (`old_page != SEQ_PAGE`
  guard keeps SHIFT release/STEP TRACK presses from blanking);
  `menu_setActiveVoice()` (existing); `menu_setShownPattern()` new SEQ arm
  (overlay kept); `menu_patternContentChanged()` admits SEQ_PAGE (copy/clear
  and register completion); `menu_stepAutomationReplaceTarget()` both success
  paths; `menu_stepAutomationExecuteItem0()` unconditional (dead guards
  removed); held-step clear.
- **Immediate updates:** a held write ORs the search bit (a completed search
  is never re-run; a running one may have passed the steps); a whole-Scene
  pot clear drops the bit in `menu_automationTargetCleared()` (new SEQ arm);
  a held-only clear restarts the search instead.
- **Pot clear:** `menu_knobClearTarget()` STATIC arm, first:
  `menu_activePage == SEQ_PAGE && active_page == 0` →
  `menu_seqCellToTrackTarget(knobNr + second_page, menu_activeVoice)`. The
  subpage guard keeps velocity/note/probability non-clearable; cells 4..7
  return invalid. Works in the SHIFT Morph view (cell identity unchanged).
  The generic register removes the per-track ID from all 7 × 128 steps.
- **Removed:** `sa_trackPresenceMask`, `sa_refreshPresence()` (−1 B static).
- **Docs:** `COPYCLEAR_UTILITIES.md` §8 STEP row and bullet, test-row update.

### 4.3 Deviations from the P3 plan (decided in the code deep dive)

1. Editor-site restarts (`ReplaceTarget`, `ExecuteItem0`) instead of
   restarting on return to subpage 0.
2. One shared `va_seqTrackSearchBit()` helper.
3. Separate SEQ arm in `menu_setShownPattern()` instead of widening the
   Effect arm.

### 4.4 `sa_applyTrackMarkers()` final rule (VOICE one-marker-per-cell)

On SEQ_PAGE subpage 0, with or without the overlay, for cells 0..2:
held overlay active and a held step carries the target → show the newest held
value (working value mid-edit) and underline the value (row 1, after the
100 ms quiet period); else if the search is complete and found the target →
underline the name (overview: first non-space of the short name; clicked-in:
first non-space of the long name at columns 8..15). Non-automatable cells
never mark. One `va_queueMarkerTransaction()` per frame, also with nothing to
mark (retires stale CGRAM marks). Only the shared marker cache and the splash
animation define CGRAM slots.

### 4.5 Verification

- Independent review (§10 of the schedule): all 31 changes match; build
  `text=539,952 data=416 bss=427,616`, no `menu.c` warnings; nm shows no
  `sa_*` symbol; va_* = 46 B; `git diff --check` clean. Two stale comments
  fixed afterwards (`va_searchRestart()` caller list;
  `menu_enterStepTrackAutomationOverlay()` header).
- A first "doesn't work" report was a wrong firmware image (false alarm).
- **Hardware: §7 rows 1–14 PASS** (2026-10-10).

### 4.6 Known limitations (accepted)

- A held-step clear or editor delete still queued in the patSvc FIFO can be
  read by the immediate rescan; the name can stay underlined until the next
  restart.
- Clearing automation does not undo the last applied track value (sticky
  until transport stop / Pattern restore).
- The STEP marker retry gap (§6.1).

---

## 5. P4 — Track run-mode remediation

Source document: `S078_P4_TRACK_RUNMODES_REMEDIATION.md`.

### 5.1 Symptom

The STEP `mod` cell showed `---` and could not be changed; the user also
asked for the label `run` / `Track` / `RunMode` (matching the FX
sequencer's `run` cell: `FX Seq` / `RunMode`).

Not at fault: storage, persistence, copy/clear, dispatch and the sequencer
switch were all wired.

### 5.2 Defect 1 — the STEP SHIFT Morph view hangs

`menu_patternTrackMorphEndpoint` is set by a SHIFT press processed in STEP
mode and cleared only by a SHIFT release processed in STEP mode
(buttonHandler.c SHIFT arms). Press/release edges are never consumed before
their `BUT_SHIFT` case. Three routes skip the clearing release:

- **Route A (common, deterministic):** SHIFT+mode button from STEP.
  `handleModeButtons()` (only writer of `selectButtonMode`) selects
  `(mode + 4) & 7` (or the SHIFT+VOICE Morph latch) while SHIFT is held:
  SHIFT+PERF → FX/Effect page (the normal way there), SHIFT+VOICE → VOICE
  latch, SHIFT+LOAD/SAVE → MENU, SHIFT+STEP → SOM. The release runs in the
  new mode's arm. Likely trigger here: the P3 Effect-page regression test.
- **Route B:** while `menu_storageBusy` or Preset is busy,
  `menu_switchPage()` refuses (queues only when leaving LOAD/SAVE): the mode
  changes but the SEQ page stays visible with the flag set.
- **Route C:** a SHIFT release dropped by event-ring overflow (64 slots;
  overflow reconciliation resets copy/clear, pairing masks, the voice-mix
  overlay, PERF holds and the hold timer, but not SHIFT-derived views).

**Silent impact while stuck:** `len`/`scl`/`shf` show and edit the Scene
Morph endpoints with SHIFT up. Any STEP track-setting edits made after such
an exit may have changed Morph endpoints (check with SHIFT held or use
`reset morph`).

**Fix F1:** `menu_patternTrackMorphViewActive()` = flag **&& SEQ_PAGE &&
`buttonHandler_getShift()`**. The view is momentary (no latch), and
`btn_held[BUT_SHIFT]` is written by the scan, not the ring, so all three
routes end on physical release. The Effect page already re-syncs its Morph
view from `buttonHandler_getShift()` (`menu_switchPage()` Effect arm,
`menu_fxVoiceMixOverlayEnd()`). A stale flag is harmless (the next STEP SHIFT
press sets it again). Clearing on SEQ exit (first idea) would cover Route A
only.

### 5.3 Defect 2 — `---` for a non-morphable parameter

**Rule (user, S078 P4, project-wide):** in any SHIFT Morph view a
non-morphable parameter shows and edits its **Normal** value; never blanked,
never locked. Status: VOICE correct (`menu_paramUsesMorphView()` redirects
sound parameters only); Effect page correct (`effects_getParameter()` /
`effects_setParameterScene()` fall back to Normal without
`INSTRUMENT_PARAM_FLAG_MORPHABLE`; `run`/`len`/`scl`/`typ` use the record);
STEP wrong only for `run` (`mch`/`not` were already Normal).

**Fix F2:** removed the Morph-view `---` block in `menu_getPlayModeName()`
(the `count == 0` table-integrity dash stays) and the commit guard in
`menu_cellCommitValue()` that refused `PAR_TRACK_PLAY_MODE` in the view;
added the rule comment at the endpoint-redirect point. The run cell is not
redirected, so it reads/writes Normal in either view.

### 5.4 Defect 3 — label

**Fix F3:** `shortNames` `mod` → `run`; `longNames` `PlayMode` → `RunMode`;
new `catNames` entry `Track` + `CAT_TRACK` (end of `catNamesEnum`, index
aligned); `valueNames[TEXT_TRACK_PLAY_MODE]` = `{SHORT_PLAY_MODE, CAT_TRACK,
LONG_PLAY_MODE}`. Clicked-in view reads `Track   RunMode`. Symbol names
(`PAR_/TEXT_/SHORT_/LONG_PLAY_MODE`, `track_play_mode[]`) kept. The other
`mod` short names (`SHORT_MOD` LFO amount, `SHORT_MODE`) untouched.
`len`/`scl`/`shf` keep category `Pattern` (moving them is three
`valueNames` edits if wanted; `shf` shares `SHORT_SHUFFLE`).

### 5.5 Files and resources

`menu.c` (predicate + header, guard removed, rule comment,
`menu_getPlayModeName()`, `valueNames`), `menu.h` (`CAT_TRACK`, comments),
`MenuText.h` (`run`, `Track`, `RunMode`, comment). RAM 0; text −96 B
(+16 B category string).

### 5.6 Documentation correction

`S078_P1_TRACK_SETTINGS.md` §5.6 ("play mode cell `mod` not shown in morph
view") and §7 (`mod`) are superseded by §5.3/§5.4 here; the spec references
carry the rule.

### 5.7 P4 hardware test plan — PASS (user, 2026-10-10)

1. Fresh boot → STEP: cell 4 reads `run`, value `fwd`.
2. Pot/encoder cycles fwd/rev/pip/rnd/onc/1fr, clamps both ends; clicked-in
   `Track   RunMode`.
3. SHIFT held: `len`/`scl`/`shf` show Morph endpoints; `run`/`mch`/`not`
   show Normal; turning `run` changes the Normal mode.
4. Release SHIFT: Normal values; `run` keeps the value.
5. Route A: STEP → SHIFT+PERF → release SHIFT → STEP: no Morph view;
   `len`/`scl`/`shf` edit Normal (SHIFT afterwards: endpoints unchanged).
6. Same via SHIFT+VOICE, SHIFT+LOAD/SAVE, SHIFT+STEP.
7. Route B: start a Scene load/apply, SHIFT+PERF during the busy window,
   release: view ends.
8. Each run mode audibly; onc/1fr retrigger rules; double-click realign does
   not restart a stopped track.
9. Save/Load; copy/clear track.
10. Regression: VOICE Morph view/latch, Effect Morph view, FX `run`, P3
    STEP underlines/pot clears (SHIFT+COPY still clears while SHIFT held).
Route C cannot be forced; covered by construction.

---

## 6. Findings not fixed

### 6.1 STEP-page marker retry gap

`menu_serviceRuntimeWidgets()`' deferred-marker retry
(`(menu_isScreenPage() || PERFORMANCE_PAGE) && (va_cgramValid &
VA_MARKER_RETRY_BIT) && lcd_queueFree() >= 72`) excludes SEQ_PAGE. When
`va_queueMarkerTransaction()` defers a STEP marker for lack of queue room
(`LCD_QUEUE_SIZE` 128), it stays missing until an unrelated repaint. Fix:
add `|| menu_activePage == SEQ_PAGE`. Not applied (user's call); P3 tests
passed without it.

### 6.2 Other notes

- `S078_RETEST_CHECKLIST.md`'s P4/P5/P6 sections are S076 P4, S077 P1 and
  S077 P2 (not S077 P4–P6 as the S077 log's next-steps implied). S077 P4's
  re-test rows 3.10/3.14/3.15 remain blank; S077 P5 (bar-to-step) and P6
  (PERF morph underline) never had checklist rows.

---

## 7. Retest checklist results (`S078_RETEST_CHECKLIST.md`)

| Rows | Feature (origin) | Result |
|------|------------------|--------|
| 1.1–1.7 | Override clear rules (S076 P1) | PASS (S077) |
| 2.1–2.7 | LFO `scn` retrigger, phase offset (S076 P2) | PASS (S077) |
| 3.1–3.9, 3.11–3.13, 3.16–3.18 | Morph copy/reset (S076 P3) | PASS (S077) |
| 3.10, 3.14, 3.15 | Scene morph fan-out (S077 P4 re-test) | **open** |
| 4.1–4.20 | Reload scene, bar chaselight, SHIFT+SELECT length (S076 P4) | **PASS (S078)** |
| 5.1–5.11 | Background region, snapshot gate, name-buffer scope (S077 P1) | **PASS (S078)** |
| 6.1–6.42 | Per-track Scene playback, PERF gestures, double-click, LEDs, follow, coalesce, regression (S077 P2) | **PASS (S078)** |
| C1–C6 | S075 overlay follow-up, F3 cases, MIDI CC priority, production build, S072 Steps 6–10 | **open** |

---

## 8. Resource ledger (session total)

| Item | Region | Bytes |
|---|---|---:|
| P1 ISR-static (sequencer.c) | SRAM1 | 76 |
| P1 Morph-engine track overrides | SRAM1 | 24 |
| P1 per-Scene settings endpoints | SRAM1 (Scene data) | 336 |
| P1 per-Scene region play mode | SRAM1 (Pattern regions) | 112 |
| P2 `sa_trackPresenceMask` | SRAM1 | +1 (in padding) |
| P3 `sa_trackPresenceMask` removed | SRAM1 | −1 (released, not reused) |
| P3 STEP search bits | existing `va_searchSceneMask` | 0 |
| P4 | — | 0 |
| **bss S077 → S078** | | **427,008 → 427,616 (+608)** |
| Flash | | text 538,416 → 539,856 (+1,440) |

---

## 9. Specification reference updates (S078 close)

- `PATTERN_DYNAMIC_STACK.md` — §1 region (`track_play_mode`), status, §6.4
  rewritten (per-track timing as built), track automation targets, §7 PAT4
  header byte 3.
- `BANK_PRESET_ARCHITECTURE.md` — track Morph endpoints, effective cache,
  track automation targets and overrides, STEP SHIFT Morph view, the
  Morph-view rule, STEP underline/overlay.
- `FILESYSTEM_SPEC.md` — PAT4 `track_scale` CC domain and byte 3,
  `sceneset.scg` `track_morph_*`, `.fx` `step_scale` token→CC map.
- `AUTOSAVE.md` — Scene cells 51..71, COUNT/LIVE_BYTES 72, zero-length guard.
- `MODULE_INTERCHANGE_SPEC.md` — StepScale contract, sequencer timing APIs,
  new Pattern/Scene/Morph/Menu/copy-clear APIs.
- `COPYCLEAR_UTILITIES.md` — STEP pot clears (P3), track copy/clear of the
  run mode and track Morph endpoints.
- `STORAGE_SRAM_MANIFEST.md` — S078 ledger.
- `dsp_instruments_effects/EFFECTS_BUS_REFERENCE.md` — FX sequencer DDA,
  128-position scale, `.fx` tokens, defaults.
- `dsp_instruments_effects/EFFECTS_MIXER_DSP_REFERENCE.md`,
  `CPU_USE_DSP_AUDIT.md` — CrumpBit 128-position division walk.

---

## 10. Disposable documents

Superseded by this log and the specification updates (the user deletes them):

- `S078_P1_TRACK_SETTINGS.md`
- `S078_P1_IMPLEMENTATION.md`
- `S078_P2_TRACK_SETTINGS_CLEANUP.md`
- `S078_P2_IMPLEMENTATION.md`
- `S078_P3_TRACK_PAR_AUTOM_FOLLOWUP.md`
- `S078_P3_IMPLEMENTATION.md`
- `S078_P4_TRACK_RUNMODES_REMEDIATION.md`
- `S078_RETEST_CHECKLIST.md` (results in §7; open rows carried in §11)
(`knowledge_files/volatile/S078_WORKING_NOTES.md` is not disposable: it was
rewritten at this close as the carry-over notes for Session 079.)

---

## 11. Carry-forward

### From S078

- STEP marker retry one-liner (§6.1) — decision.
  (P1, P2 and P4 hardware tests: PASS, 2026-10-10.)
- Track length 128 cannot be step-automated (7-bit) — accepted limit.
- Optional: move `len`/`scl`/`shf` to category `Track`.

### From S077 / S076 / S075 (unchanged unless noted)

- Retest 3.10/3.14/3.15 (S077 P4 Scene morph fan-out); S077 P5 bar-to-step
  and P6 PERF morph underline have no recorded tests.
- C1 SHIFT+TRACK overlay follow-up; C2–C4 S075 F3 automation priority cases;
  C5 production build re-measurement; C6 S072 Steps 6–10 acceptance.
- Known limitation (S077 P2): Scene-namespace LFO/velocity target tokens
  resolve through the active Scene in InstrumentManager.
- Pattern Load fan-out tear (S075, not fixed); O1 Settings Load per-voice
  Morph equalisation; F4 trace-ring priorities; underline limitations (live
  erase race, deferred clear race); MIDI-entered values reach the LCD only at
  the next repaint; Effects saved at pan 64 show `1`.
- S074: unexplained boot timeout; `cpu` widget with `cmp` on; CrumpBit
  minimum-share run; BC11; saturator α 0.35; stale comments; carried small
  debt.
- Deferred features: `/Effect/` browser (A35); MIDI mapping (A20); live record
  of FX moves (A22) and automation; roll overhaul; Patgen/Euklid revert;
  triplet `12a/12b` scale mode.
