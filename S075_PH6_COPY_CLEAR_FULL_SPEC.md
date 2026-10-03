# S075 — Phase 6: Copy and Clear — Full Specification

**Status:** specification, revision 3 (Session 075, 2026-10-01). Nothing here
is implemented.

- Revision 1 merged the user's copy/clear specification with the startup brief
  `S075_PH6_COPY_CLEAR.md` and checked both against the code on
  `dev-ph6-copyclear` (HEAD `b33c94e`).
- Revision 2 folded in the user's first answers.
- Revision 3 folds in the second answers: global `srt` retired and replaced on
  PERF by Effect Morph, a permanent swap block in every pool, step-by-step
  pastes, edit-mask fan-out for Scene children, and a terminology pass (this
  document uses the user's terms; anything else is a code name).

§12 records every decision; §13 lists what is still open. This is a general
specification, not a line-level schedule. It supersedes the decision table in
`S075_PH6_COPY_CLEAR.md` §9.

**Read first:** `MEMORY.md`; `PATTERN_DYNAMIC_STACK.md` §2–§5, §12;
`BANK_PRESET_ARCHITECTURE.md` §3–§5; `AUTOSAVE.md` "Dirty marking rules";
`FILESYSTEM_SPEC.md` (HCNAMES, `sceneset.scg`);
`dsp_instruments_effects/EFFECTS_BUS_REFERENCE.md` §5, §9, §10–§11.

**Binding rules:**

- Plans first: this specification, the answers to §13, a line-level schedule,
  then implementation, review and hardware gates.
- RAM: up to +100 B approved for this feature, including the 18 B pot-clear
  register. Larger working storage uses the 9 kB name buffer (§9.3).
- Only the active Scene's Pattern is modified, one Scene at a time; any Scene
  and track may be read (§9.4).
- Pattern pool writes keep the publication order (write, PRIMASK swap, free)
  and bounded work per tick.
- SceneData is the only writer of retained Scene data; every retained change
  marks AutoSave.
- Foreground only; no audio-path change except retiring global `srt` (§10).
- Hardware checks and commits are the user's.

---

## Contents

1. Terms
2. What exists today
3. Copy and clear operations: start, hold, end
4. Copy behaviour by mode
5. Clear behaviour by mode
6. Endless-pot parameter clear
7. Button routing
8. Screens and LEDs
9. Architecture
10. Related change: retire global `srt`, Effect Morph on PERF
11. Removal list and resources
12. Decision log
13. Open follow-ups
14. Implementation stages and hardware checks

---

## 1. Terms

Terms are the user's unless marked as a code name.

| Term | Meaning |
|---|---|
| **Copy/clear button** | `BUT_COPY`, `LED_COPY`. |
| **Copy operation** | Starts when the copy/clear button is held and a copy object button is pressed (§4.1). Lasts until the button is released and every paste it queued has finished. |
| **Clear operation** | Starts when SHIFT is held, the copy/clear button is pressed and held, and then a copy object button is pressed or an endless pot is turned. Lasts until the copy/clear button is released and every clear it queued has finished. |
| **Menu interaction** | The part of an operation in which the user's buttons act. It ends when the copy/clear button is released; after that only queued background work finishes. |
| **Source** | What the copy object button selected: a step, step range, bar, bar range, track, Scene, FX step or FX step range, with its Scene and track. |
| **Selection** | The copy- or clear-menu entry chosen with the encoder. |
| **Paste** | Applying the current selection from the source to one destination. |
| **Active Scene** | The Scene being viewed and written to. Not defined by playback. |
| **Edit mask** | The VOICE edit fan-out mask (`scene_mask_voice_edit`), one directional entry per Scene. Parameters and FX settings of Scenes in one edit mask are meant to match. |
| **Fan out** | Applying the same change to every Scene in the destination's edit mask. |
| **Queue** | Pastes and clears waiting to run in the background (up to 4). |
| **Register** | The 8 parameters waiting for a pot clear. |
| **9 kB name buffer** | `fs_list_cache_name` (code name), the 9,000 B name cache, used as working storage while AutoSave and maintenance are suspended. |
| **Swap block** | A permanently reserved 132 B region at the top of every Scene's pool (§9.6). |
| **Source indicator** | The ≤ 8-character source text on the display (§8.1). |

Tracks are 1..7 here and 0..6 in code; Instrument slots 1..6 (0..5 in code).
Track 7 plays slot 6's alternate sound, and **behaves as track 6** wherever an
Instrument or per-slot setting is involved.

---

## 2. What exists today

### 2.1 Gestures

| Gesture | Code | Behaviour now |
|---|---|---|
| Copy/clear press | `buttonHandler.c` `processPress()` `case BUT_COPY` | `copyClear_Mode = MODE_COPY_TRACK`; LED blinks; SELECT and VOICE LEDs cleared. |
| + VOICE *a*, VOICE *b* | `handleVoiceButton()` | `pat_copyTrack()`, a no-op. |
| + SELECT *a*, SELECT *b* | `buttonHandler_partButtonPressed()` | `pat_copyBar()`, a no-op, in every mode. |
| + SEQ | not intercepted | Falls through: VOICE toggles the step, STEP selects it, PERF switches Scene, FX selects or holds an FX step. |
| SHIFT + copy/clear | `case BUT_COPY` | The original LXR clear menu, written straight to the LCD by `copyClear_armClearMenu()`: `track`, `pattern`, `autom.1`, `autom.2`. The last two are the original LXR's automation lanes, which this product does not have; a missing `case` makes both clear the whole track. |
| SHIFT + copy/clear while recording and running | `case BUT_COPY` | Sequencer erase mode. Kept. |

### 2.2 Pattern storage and the service

- `pat_regions[16]`, 10,519 B each: address array (7 × 128 × `u16`), 8,192 B
  pool (2,048 four-byte chunks), 512 B bitmap, per-track `track_length`
  (1..128, default 16), `track_scale`, `track_shuffle`, `pattern_change_bar`,
  `pattern_next`.
- Address entry: bit 15 trigger, bit 14 block present, bits 13..0 pool offset;
  `0x3FFF` = no block.
- Block: 2-byte header (10-bit back-reference `track·128 + step`, 6-bit
  automation count), flags byte, special values, up to 63 automation entries
  (9-bit target, 7-bit value). Largest block 132 B (33 chunks).
- The Pattern Stack Service mutates one Scene (`service_scene`), which follows
  `seq_activePattern`. Today a PERF Scene press selects the viewed and played
  Scene together, so this is the active Scene. If viewing and playback are ever
  split, the service must follow the active Scene, not playback.
- Bulk barriers: 8 steps per tick (`PATSVC_BULK_STEPS`); multi-track cursor
  `bulk_track_cursor` reserved and unused.
- Maintenance: the repair epoch (trailing reservations, repair relocations) and
  reactive compaction (only for a blocked queued edit).
- `patSvc_prepareSceneReplace()` / `patSvc_finishSceneReplace()` are the
  existing exclusive-access boundary used by Pattern loads.
- Pattern AutoSave snapshots the region at admission and is not gated on the
  Scene being Bank-present. Scalar AutoSave maps only present Scenes.
- Pattern Load's fan-out `memcpy` can tear one playback tick (logged in
  `SCOPING_TARGETS.md`, "Session 075 findings").

### 2.3 Background writers (admission points in `filesystem.c`)

`filesystem_autosaveWriterSchedule_tick()` (scalar drain and runtime ensure),
`filesystem_autosavePatternDrainSchedule_tick()`,
`filesystem_autosaveNonSemanticPatternDrainSchedule_tick()`,
`filesystem_autosaveTraceFlushSchedule_tick()`,
`filesystem_patternTraceFlushSchedule_tick()`,
`filesystem_settingsWriterSchedule_tick()`. All run only while the facade is
idle; a started writer is never pre-empted. The scalar writer's LOAD/SAVE gate
sets `fs_autosave_page_suppressed`, which shortens the next deadline to the
250 ms continuation.

### 2.4 Scene-level facts

| Object | Writer / commit | AutoSave | Runtime (active Scene) | Fans out today |
|---|---|---|---|---|
| `scene_settings_t` (every `sceneset.scg` field) | `scene_set*()` setters; VOICE mix cells via `preset_setVoice*()` | per cell | `preset_applySceneSettings()` | mix cells and voice Morph, through the active Scene's edit mask |
| `effect_record_t` (420 B) | `scene_commitEffectRecord()`; EffectsManager edit API | whole Effect region / per cell | `effects_activateScene()` | yes, to same-type Scenes (`effects_fanoutMask()`, active Scene only) |
| FX steps (18 B × 16) | `scene_setEffectSeqLaneValue/Locked()`; `effects_setSeqLaneLock()` | per cell | `effects_service()` | lock edits, same as Effect |
| `kit_t` (1,160 B) | Kit Load commit | `autosave_markKitDirty()` | `preset_startDrumsetApply()` | Kit Load takes a Scene mask |
| Instrument slot | `preset_startInstrumentApplyImage()` (same slot index in and out) | `autosave_markWholeInstrumentDirty()` | bounded Instrument apply | Instrument parameter edits fan out |
| Edit masks | `bank_setSceneMaskVoiceEditForScene()`; `bank_revalidateVoiceEditMasks()` | Bank field | — | — |
| Names, sources | HCNAMES ops; `.hcnamtmp` then swap | `autosave_markSourceDirty()` | — | — |

- Advanced limit: `instrumentManager_typeSelectableForSceneSlot()`.
- LFO target voice selectors are one-based slots (`self` on Kit Save).
- `filesystem_bootReaderEmptyScene()` is boot-only.
- Global `srt` (`voice_decimation_all`) is a real multiplier on all six
  voices' decimation (`mixer.c`: `cnt += rate[voice] * rate[6]`), stored per
  Scene, step-automatable, an LFO target and a MIDI CC (§10).

---

## 3. Copy and clear operations: start, hold, end

### 3.1 Copy operation

1. Copy/clear pressed and held (SHIFT not held): **its LED lights steady.**
2. The first copy object button (§4.1) opens that object's menu provisionally
   and shows its source indicator. The row source is committed when the last
   held row button is released; track and Scene sources commit on press. Once
   the menu is open, the copy LED flashes until release. AutoSave and Pattern
   maintenance are suspended (§9.2).
3. The encoder changes the selection; the encoder click does nothing.
4. The user navigates (§7) and presses destinations. Each press queues a paste
   with the selection shown at that moment. Up to 4 may wait; a further press
   while 4 wait is dropped.
5. **Releasing the copy/clear button ends menu interaction.** Queued pastes
   finish in the background.

The source is set once per operation. SHIFT never turns a copy operation into a
clear operation; it still works as a MODE modifier (SHIFT+PERF reaches EFFECTS
mode).

### 3.2 Clear operation

1. SHIFT pressed: SHIFT LED lights.
2. Copy/clear pressed while SHIFT is held: **SHIFT and copy/clear flash, latched
   until copy/clear is released**, whether or not SHIFT is still held. SHIFT's
   release must not run its mode-specific actions or cancel anything.
3. The page stays visible for pot clears (§6). **While the copy/clear button is
   held, no pot or encoder changes a parameter value.**
4. A copy object button opens its clear menu at `cancel`. The encoder chooses.
   On release of the object button(s) the clear is queued unless `cancel` is
   shown.
5. The menu stays up while copy/clear is held. Re-selecting another object in
   the same object group keeps the current selection; a new group starts at
   `cancel`. While a clear menu is shown, pot turns are ignored.
6. EFFECTS-mode SEQ clears and pot clears act at once, with no menu.
7. Releasing copy/clear ends menu interaction; an object button still held at
   that moment applies nothing.

### 3.3 Refusals (silent)

No operation starts while the sequencer records or erases, while Load/Save or
an Instrument Load transaction owns the UI, or in LOAD/SAVE, MENU or SOM mode.
Pastes and clears that cannot run (not enough pool space, Advanced limit,
Effect type mismatch) are dropped silently. A queued paste or clear that needs
a busy Preset apply worker (for example just after a PERF Scene switch) waits
at the head of the queue until the worker is idle.

---

## 4. Copy behaviour by mode

### 4.1 Sources and copy menus

| Mode | Copy object gesture | Source | Copy menu (default first) |
|---|---|---|---|
| VOICE, STEP | SEQ press and release | step | `step -> repl`, `step -> merge`, `auto -> repl`, `auto -> merge` |
| VOICE, STEP | SEQ range (§4.2) | step range | as step |
| STEP | SELECT press and release | bar | `bar -> repl`, `bar -> merge`, `auto -> repl`, `auto -> merge` |
| STEP | SELECT range (§4.2) | bar range | as bar |
| VOICE, STEP, PERF, EFFECTS | TRACK press | track | `copy track`, `copy instrument` |
| PERF | SEQ (Scene) press and release | Scene | `copy scene`, `copy scene settings`, `copy kit`, `copy effect`, `copy pattern` |
| EFFECTS | SEQ press and release, or range | FX step / FX step range | `copy step` |

Step and bar sources are set when the last held button of that row is
released; track and Scene sources on press. The first step/bar/FX-row press
opens its menu provisionally, and the final row release commits the source
without resetting the menu selection. Row stacks retain raw button indices and
convert them to absolute steps at the press, so ranges remain correct across
the visible-bar offset.

### 4.2 Ranges

- The **end** is a button pressed while another button of the same row is
  held. The start is the most recently pressed button still held at that
  moment. Later holds take precedence; the last start/end pair before all
  buttons are released is the source. Example (user): hold 7, hold 3, hold 11,
  press and release 2, press and release 16, release all → source 11–16.
- Ranges never cross bars; BAR presses are ignored while a SEQ source button is
  held.
- Order runs from start to end. SEQ 11 held, SEQ 7 pressed, pasted at 4:
  11→4, 10→5, 9→6, 8→7, 7→8. Bar ranges reverse completely (SELECT 6 held,
  SELECT 2 pressed: steps `6·16+15` down to `2·16`).
- Destinations wrap: past step 128 the paste continues at step 1 of the same
  track; FX steps wrap past 16.

### 4.3 Pastes and navigation

| Source | Paste | Navigation while copy/clear is held |
|---|---|---|
| step / step range | SEQ press in VOICE or STEP | BAR (no SEQ held), TRACK, MODE, PERF SEQ |
| bar / bar range | SELECT press in STEP | TRACK, MODE, PERF SEQ |
| track | TRACK press in VOICE, STEP, PERF or EFFECTS | MODE, PERF SEQ |
| Scene | SEQ press in PERF | — |
| FX step / range | SEQ press in EFFECTS | MODE, PERF SEQ, SHIFT+PERF back to EFFECTS |

Step, range, bar and track pastes write the active Scene. A PERF SEQ press
while such a source is set selects the active Scene in the normal way. A Scene
paste in PERF writes the pressed Scene and changes nothing else (no Scene
switch). A paste identical to its source does nothing.

### 4.4 What each selection does

**Step and bar family** (per source → destination step):

| Selection | Trigger | Specials | Automation |
|---|---|---|---|
| `… -> repl` | := source | := source | := source |
| `… -> merge` | destination OR source | source wins per field; fields only the destination has are kept | union; source wins on the same target; beyond 63, destination entries are dropped silently |
| `auto -> repl` | unchanged | unchanged except source probability replaces destination probability | := source |
| `auto -> merge` | unchanged, including probability | unchanged | union as above |

- An empty source step under `… -> repl` empties the destination step; under a
  merge it changes nothing.
- For `… -> repl` and `… -> merge`, the trigger result is written at paste
  press time. If the queued paste later drops, the service restores its early
  trigger write only where the user has not changed that step since; a clear
  similarly turns final-state-off triggers off before its background job.
- **Length extension:** `max(length, 16·(highest written bar + 1))`; a paste
  never shortens a track.
- Automation targets follow the destination when the track or Scene differs
  (§9.7); unmatched targets, or targets whose dtype differs, are dropped.
- Never fans out (Pattern data).

**Track:**

- `copy track`: 128 steps as `copy … all`, plus length, scale and shuffle.
  Never fans out.
- `copy instrument`: the source track's slot (type, Normal and Morph images)
  replaces the destination track's slot, **and fans out** to the same slot in
  every Scene in the destination's edit mask. The Advanced limit fails the
  paste silently. LFO targets pointing at the source slot move to the
  destination slot. Slot-6 alternates: a HiHat's `_choke` rows travel inside
  its image; the Kit-owned `slot6_track7_*` decay pair is copied only slot 6
  → slot 6 (tracks 6 and 7); otherwise the destination keeps its own pair.

**Scene** (PERF), destination = the pressed Scene:

| Selection | Copies | Fans out | Also |
|---|---|---|---|
| `copy scene` | settings, Effect, Kit, Pattern | no; the destination's edit mask is set by the exchange below | Bank-present; names |
| `copy scene settings` | all of `scene_settings_t`, including Effect Morph | no; mask exchange | — |
| `copy kit` | `kit_t` | yes | Bank-present; names |
| `copy effect` | `effect_record_t` | yes | names |
| `copy pattern` | the whole region, including the Pattern globals; targets follow the destination's types (§9.7) | no | names |

- **Present rule.** Copies into a Scene that is not present are done. Only
  `copy scene` and `copy kit` make it present. A Scene that receives only a
  Pattern, settings or an Effect stays dark in PERF and is selectable only from
  the Load menu. Its settings and Effect are AutoSaved only once a Kit makes it
  present (accepted).
- **Edit-mask exchange** (`copy scene`, `copy scene settings`): the
  destination's entry becomes the source's entry with the source and
  destination bits exchanged; other entries are unchanged; masks are then
  revalidated.

  ```
  m  = mask[src]
  m' = (m & ~(bit(src)|bit(dst)))
     | (m & bit(src) ? bit(dst) : 0)
     | (m & bit(dst) ? bit(src) : 0)
  mask[dst] = m'
  ```

**FX step** (EFFECTS): each source step's lock mask and lane values replace the
destination step's, in range order with wrap, and **fan out** to the
destination's edit mask. The destination may be another Scene's sequence; if
its Effect type differs from the source's, the paste is dropped.

### 4.5 Fan-out summary

| Fans out | Does not fan out |
|---|---|
| `copy instrument`, `copy kit`, `copy effect`, FX step pastes, `clear fx`, `clear fx sequence`, EFFECTS SEQ clear, the FX-lock part of an EFFECTS pot clear, `clear send` | every Pattern paste and clear (step, range, bar, track, `copy pattern`, `clear pattern`, `clear automation`, `clear notes`, the Pattern part of any pot clear); `copy scene` and `copy scene settings` (they set the mask); `clear scene` and `clear scene settings` (they reset the mask) |

The mask used is the destination Scene's own entry (the active Scene's entry
for pastes in VOICE/STEP/EFFECTS; the pressed Scene's entry for PERF Scene
pastes), filtered by `scene_editLayoutMatches()` (Effect: same type). Masks are
revalidated after any type change. Names follow every fanned-out destination.

---

## 5. Clear behaviour by mode

The first clear object in a group opens at `cancel`; another object in the
same group keeps the current selection.

| Mode | Copy object | Clear menu | Applied |
|---|---|---|---|
| VOICE, STEP, PERF | TRACK | `cancel`, `clear track`, `clear track automation`, `clear track notes` | on TRACK release |
| EFFECTS | TRACK | as above plus `clear send` | on TRACK release |
| VOICE, STEP | SEQ, SEQ range | `cancel`, `clear step`, `clear step automation`, `clear step notes` | on release of the last SEQ |
| STEP | SELECT, SELECT range | `cancel`, `clear bar`, `clear bar automation`, `clear bar notes` | on release of the last SELECT |
| PERF | SEQ (Scene) | `cancel`, `clear scene`, `clear scene settings`, `clear pattern`, `clear automation`, `clear notes`, `clear fx`, `clear fx sequence` | on SEQ release |
| EFFECTS | SEQ | none | at once |
| any | endless pot | none | §6 |

| Selection | Effect | Fans out |
|---|---|---|
| `clear step` / `clear bar` | trigger off, block released | no |
| `clear track` | as above for 128 steps; length, scale, shuffle to defaults (16, default scale, 0) | no |
| `… automation` | automation removed; trigger and specials kept | no |
| `… notes` | trigger off, note/velocity specials removed; probability and automation kept | no |
| `clear send` | the track's slot (track 7 → slot 6): both FX-send endpoints 0, fader `pre` | yes |
| `clear scene` on the active Scene | everything except the Kit to defaults (settings, Effect `off`, FX sequence, Pattern); the Scene's edit mask reset to itself | no |
| `clear scene` on another Scene | emptied, Kit included; Bank-present off; edit mask reset to itself | no |
| `clear scene settings` | settings to defaults; edit mask reset to itself | no |
| `clear pattern` | Pattern region to empty | no |
| `clear automation` / `clear notes` | as the track selections, on all 7 tracks | no |
| `clear fx` | Effect record to defaults (`off`) | yes |
| `clear fx sequence` / EFFECTS SEQ | sequence steps only (all 16, or the one pressed): lock masks and lane values to their empty state, as after an Effect type change. Parameters, run mode, length, scale stay. | yes |

Names stay on every clear (§9.10). There is no Instrument clear.

Clear order for trigger-off selections is visible immediately: the destination
LED flash is followed by the trigger-bit write, then the queued pool rewrite.
If the queued clear later drops, the accepted trigger-off state may remain.

---

## 6. Endless-pot parameter clear

- In a clear operation, turning a pot over an **automatable** parameter adds it
  to the register and suppresses its underline until removal finishes. The
  value never changes. Over a non-automatable parameter (including PERF `mrp`)
  nothing happens. In a copy operation pots do nothing. While a clear menu is
  shown, pots are ignored.
- Pot clears take effect at acceptance and their background register drains
  through the same whole-call trickle governor while an older filesystem
  writer is busy.
- The Pattern part covers the active Scene, all 7 tracks × 128 steps, and never
  fans out:

| Page | Cell | Pattern target | FX sequence (fans out) |
|---|---|---|---|
| VOICE (Normal or Morph view) | Instrument parameter | `slot·64 + descriptor` | — |
| VOICE | voice Morph, audio out, FX send | `Nvm`, `Nou`, `Nfx` | — |
| VOICE | generated slot-6/track-7 decay | `7dc` | — |
| PERF | `1vm..6vm` | matching Scene target | — |
| PERF | `fxm` (Effect Morph, new cell, §10) | `fxm` (404) | lane 0 |
| EFFECTS | Effect parameter, local *L* | `448 + L` | lane mapped to *L*, all 16 steps |
| EFFECTS | `mrp` (Effect Morph) | `fxm` | lane 0 |

- **Register:** 8 entries (18 B, approved). A ninth turn while 8 wait does
  nothing and its underline stays. Duplicates are not added. Entries run one at
  a time in the background (about 224 ms each) and use the swap block (§9.6),
  so they never fail for lack of pool space.
- The PERF page gets no underline.

---

## 7. Button routing

The copy/clear handling runs before every existing handler for SEQ, SELECT,
TRACK, BAR, MODE, SHIFT, the encoder and the pots, and consumes both edges of
each button it uses (pairing masks like `buttonHandler_loadSceneSeqPressedMask`,
also cleared by the event-ring overflow reconciliation). **No consumed edge may
reach step toggling, the VOICE hold overlay, the FX lane-lock hold, PERF Scene
switching, mute or audition.** Stage 3 (§14) includes an exhaustive
mode × button × state test.

### 7.1 Copy operation

| Button | No source yet | step/range | bar/range | track | Scene | FX step |
|---|---|---|---|---|---|---|
| SEQ (VOICE/STEP) | source | **paste** | ignored | ignored | ignored | ignored |
| SEQ (PERF) | Scene source | navigate | navigate | navigate | **paste** | navigate |
| SEQ (EFFECTS) | FX source | ignored | ignored | ignored | ignored | **paste** |
| SELECT (STEP) | bar source | navigate (bar) | **paste** | normal | ignored | ignored |
| SELECT (other modes) | normal | normal | normal | normal | normal | normal |
| TRACK | track source | navigate (VOICE/STEP; ignored in PERF/EFFECTS, where TRACK mutes) | navigate (VOICE/STEP; ignored in PERF/EFFECTS) | **paste** | ignored | ignored |
| BAR1/BAR2 | normal | navigate (not while SEQ held) | normal | normal | normal | ignored |
| MODE VOICE/STEP/PERF, SHIFT+PERF | normal | navigate | navigate | navigate | navigate | navigate |
| MODE LOAD/SAVE, MENU, SOM | ignored | ignored | ignored | ignored | ignored | ignored |
| SHIFT | MODE modifier only | | | | | |
| Encoder | turn: selection; click: ignored | | | | | |
| Pots | ignored | | | | | |

### 7.2 Clear operation

| Button | Action |
|---|---|
| SEQ (VOICE/STEP), SELECT (STEP), TRACK (any mode), SEQ (PERF) | open the clear menu; queue the clear on release |
| SEQ (EFFECTS) | clear that FX step at once |
| Pots | §6; ignored while a clear menu is shown |
| BAR, MODE (VOICE/STEP/PERF/EFFECTS) | navigate |
| SHIFT | LED only |
| Encoder | turn: selection while a menu is shown; click: ignored |

---

## 8. Screens and LEDs

### 8.1 Screens

The menus are drawn by `menu_repaint()` as an overlay; no direct LCD writes.
While a menu is up, page repaints draw the menu, underline (CGRAM) updates are
held, and pot repaints do not draw the page.

```
Copy    03T2s005        Clear   S03T2
[step -> merge ]        [track notes   ]
```

| Source | Indicator | Example |
|---|---|---|
| step | `NNTNsNNN` | `03T2s005` |
| step range | `sNNN-NNN` (start-end) | `s011-016` |
| bar | `SNNTNbN` | `S03T2b4` |
| bar range | `bN-N` | `b6-2` |
| track / instrument | `SNNTN` / `SNNiN` | `S03T2`, `S03i2` |
| Scene / pattern / effect / kit / settings | `SNN` / `SNNP` / `SNNf` / `SNNK` / `SNNc` | `S03P` |
| FX sequence / FX step / FX range | `SNNfs` / `SNNfsNN` / `fsNN-NN` | `S11fs`, `S11fs05`, `fs05-08` |

One-based numbers: Scenes 01..16, tracks 1..7, steps 001..128, bars 1..8, FX
steps 01..16. Clear menus show the same indicator for the pressed object.

Selection labels: `step -> repl`, `step -> merge`, `bar -> repl`, `bar -> merge`,
`auto -> repl`, `auto -> merge`, `track`, `instrument`, `scene`, `settings`, `kit`, `effect`,
`pattern`, `step` (FX); clears: `cancel`, `step`, `step auto`, `step notes`,
`bar`, `bar auto`, `bar notes`, `track`, `track auto`, `track notes`, `send`,
`scene`, `settings`, `pattern`, `automation`, `notes`, `fx`, `fx sequence`.

### 8.2 LEDs

- Copy: copy/clear LED steady when pressed; the first row/object opens the
  menu and then the copy LED flashes until release. There is no source-row LED
  blink.
- Clear: SHIFT as normal; once copy/clear is pressed, SHIFT and copy/clear
  flash, latched until copy/clear is released.
- A paste or clear flashes every visible LED in the affected destination
  object, including a full range; Pattern changes repaint the visible track.
  The old source group-blink layer is not used.

---

## 9. Architecture

### 9.1 Module layout

New directory `Core/Menu/CopyClear/`, replacing `copyClearTools.c/h`:

| File pair | Contents |
|---|---|
| `copyClearSession.c/h` | start/end state, button handling for `buttonHandler.c`, source and range rule, menu state and selection lists, LEDs, the suspension predicate |
| `copyOps.c/h` | paste rules: merge, retargeting (§9.7), Scene-level copies, edit-mask exchange, fan-out masks, FX step copy |
| `clearOps.c/h` | clear rules, Scene-level clears, `clear send`, FX sequence clears, the register front end |
| `copyClearService.c/h` | the background service: queue (4), register drain, the 9 kB name buffer, Pattern work (snapshot, step-by-step placement, swap block), end-of-operation name write, end of suspension |

- Prefixes: `copyClear_`, `ccCopy_`, `ccClear_`, `ccSvc_`.
- Pool layout helpers stay in `PatternData.c`; the exclusive-access boundary
  stays in `PatternStackService.c` (§9.4); the HCNAMES op stays in
  `filesystem.c`.
- Makefile: four new sources in `SRCS`, `-ICore/Menu/CopyClear`;
  `copyClearTools.c` removed.

### 9.2 Suspension of AutoSave and Pattern maintenance

From the start of an operation until menu interaction has ended and the queue,
the register, any apply worker a paste started, and the name write have all
finished, `copyClear_suspendBackground()` is nonzero and:

| Site | Effect |
|---|---|
| scalar AutoSave scheduler | no ensure, no drain; sets `fs_autosave_page_suppressed` (250 ms continuation afterwards) |
| semantic and non-semantic Pattern AutoSave schedulers | no admission |
| AutoSave and Pattern trace flush schedulers | no admission |
| `settings.cfg` writer | no admission |
| `patSvc_tick()` repair epoch | returns before scanning; cursor kept |

A writer already running finishes normally; pastes do not wait for it. Work
that needs the facade or the 9 kB name buffer waits in the queue. Reactive
compaction runs only inside copy/clear's own pastes and clears. At the end, the
service clears its reservation image and arms a rebuild. Dirty marks happen at
every change, so AutoSave sees everything afterwards.

### 9.3 The 9 kB name buffer

- `filesystem_borrowNameCacheScratch()` succeeds only while the facade is idle.
  It tags the cache with a new domain so every browser accessor reports "not
  loaded". `filesystem_returnNameCacheScratch()` clears it.
- Borrowed lazily only for an overlapping Pattern paste that needs a source
  snapshot, or for the final HCNAMES read/write. Non-overlapping Pattern
  pastes read the live source and do not need the loan. Load/Save reloads its
  index on the next entry (the slow Load type switch from S073 applies).

| Use | Lifetime | Bytes |
|---|---|---:|
| HCNAMES row remap (§9.10) | operation | 161 |
| source address entries of the running paste | paste | ≤ 256 |
| source blocks, retargeted (one track's blocks cannot exceed one usable pool) | paste | ≤ 8,060 |
| **worst case** | | **8,572** |

The paste region also holds Kit (1,160 B), Effect (420 B) and FX-range (288 B)
copies, and at the end the original names and sources for the name write
(1,771 B).

### 9.4 One Scene written at a time

- Any Scene and track may be read. Only one Scene's Pattern is modified at a
  time: the active Scene for step, range, bar and track work; the pressed Scene
  for a PERF Scene paste or clear. Every Pattern write keeps the publication
  order, so readers (playback now, chaining and per-track playback later)
  always see a complete old or new step.
- `PatternStackService.c` generalises the replace boundary into
  `patSvc_beginExclusive(scene)` / `patSvc_endExclusive(scene)` (code names),
  used by filesystem loads and copy/clear:
  - for the service Scene, begin closes admission and is ready once queued
    work has drained;
  - for another Scene, begin is ready at once and holds off any handover into
    that Scene until end;
  - one holder at a time.
- Each queued paste or clear takes the boundary when it starts and releases it
  when it ends. A paste whose active Scene is still being handed over (just
  after a PERF switch) waits in the queue.

### 9.5 Pastes: step by step

1. **Choose the source path.** A non-overlapping paste reads the live source;
   an overlapping paste snapshots address entries and blocks into the 9 kB
   name buffer, with automation retargeted for the destination (§9.7).
   Early-trigger paste selections write their source and destination trigger
   masks at button time, before the queued job runs.
2. **Check room.** Walk the destination steps in paste order, adding each
   step's new size minus its old size. If the largest running total exceeds
   the free chunks (outside the swap block, counting reclaimable
   reservations), drop the paste; nothing has changed. Otherwise the paste
   always completes.
3. **Place each step** (8 per tick):
   - if a free run of the new size exists: write the new block there, publish
     the address entry (trigger as the selection requires, block bit, offset)
     in one PRIMASK store, free the old block;
   - otherwise: write the new block into the swap block, publish, free the old
     block, compact (relocate published blocks downward) until a run of the
     new size exists, copy the block there, publish, and the swap block is
     empty again.
4. **Finish:** length extension or track settings, dirty mark, presence-search
   restart, LED and STEP page refresh. In trickle mode the governor admits a
   whole bounded engine call only while its measured credit is positive. A
   dropped paste restores only early-trigger steps that still contain the
   service's write; user changes made after the press are preserved.

Merges decode the destination's automation into one 63-entry stack buffer
(252 B, as `pat_writeSpecials()` does) and read the source from the live path
or the overlap snapshot selected above.

### 9.6 The swap block

- **Permanent reserve:** the top 33 chunks (132 B, bytes 8,060..8,191) of every
  Scene's pool. Normal allocation, repair, reservations and compaction never
  use it; usable pool capacity becomes 8,060 B. The pool-use widget reports
  against 8,060 B.
- **Purpose:** a guaranteed rewrite area, so a rewrite never needs free pool
  space while the old block is still live. Copy/clear is its first user;
  **the reserve stays available to any later feature that needs the same
  guarantee**, and is documented as such in `PATTERN_DYNAMIC_STACK.md`.
- Removals (`… automation`, `… notes`, pot clears) use it the same way as
  §9.5 step 3: write the smaller block into the swap block, publish, free the
  old block, write it back at the old location, publish, swap block empty
  again.
- The PAT4 format is unchanged (the reserve is free space). Patterns saved
  before this change may have blocks in the reserve; the first paste or clear
  that needs the swap block relocates them downward first. If a full pool makes
  that impossible (only possible with such older data), that paste's or
  clear's rewrites are dropped silently.
- `clear … all` and `clear pattern` release blocks only and do not need it.

### 9.7 Retargeting automation

Applied when the destination track or Scene differs from the source, and for
`copy pattern` when the destination's Instrument or Effect types differ. Best
effort; a slightly different parameter is fine; a target with no match, not
automatable, or with a different dtype is dropped.

| Source target | Destination target |
|---|---|
| Instrument parameter of the source track's own slot | the destination track's slot: same type → same parameter; else same storage key; else same VOICE page position. Must have the same dtype. |
| Instrument parameter of another slot | same slot in the destination Scene, matched as above |
| `Nvm`, `Nou`, `Nfx` of the source's own slot | same kind on the destination slot |
| track-7 alternates (`7dc`, `_choke` parameters) | the destination track 7's alternate decay when one exists; else dropped |
| Scene-wide (`fxm`) | unchanged |
| Effect parameter | same type → unchanged; else same key in the destination type; else dropped |
| automation-off value (`0x1FF`) | unchanged |

Two entries that land on the same target keep the first. Track 7's own slot is
slot 6.

### 9.8 Whole-Pattern copy

Under the exclusive boundary on the destination:

1. Publish every destination address entry as empty with trigger off.
2. Copy pool, bitmap, track settings and Pattern globals.
3. `copy pattern`: retarget entries in the not-yet-referenced pool, shrink
   blocks that lost entries, free their tail chunks.
4. Copy the 896 address entries last.
5. Release the boundary; mark the Pattern dirty; restart the presence search if
   the destination is viewed.

### 9.9 Scene-level pastes and clears

A paste or clear that needs a busy Preset worker waits at the head of the queue.

| Paste / clear | Commit (per fan-out destination where it fans out) | Then |
|---|---|---|
| `copy instrument` | Advanced check; `preset_startInstrumentApplyImage()` generalised to separate source and destination slots; LFO `self` retarget; slot-6 decay pair per §4.4 | marker and Bank-present (in the helper); apply if active; revalidate masks if the type changed; names |
| `copy kit` | assign `kit_t` as Kit Load does | `autosave_markKitDirty()`; Bank-present; apply if active; revalidate; names |
| `copy effect` | `scene_commitEffectRecord()` | activate if active; revalidate; names |
| `copy scene settings` | each field through its setter | apply if active; mask exchange; revalidate |
| `copy scene` | settings, Effect, Kit, Pattern (§9.8) | Bank-present; one Scene activation if active; mask exchange; revalidate; names |
| `clear scene` (active / other), `clear scene settings` | per §5, through the setters and InstrumentManager slot reset | apply if active; mask reset to itself |
| `clear fx` | `scene_effectRecordDefaults()` committed | activate if active; revalidate |
| `clear fx sequence`, EFFECTS SEQ | sequence steps through SceneData setters (closes Phase 5 debt A15) | refresh held Morph lane and lock markers |
| `clear send` | `preset_setVoiceFxSendAmount(…, 0)`, `preset_setVoiceFxSendMorph(…, 0)`, `preset_setVoiceFaderSetting(…, pre)` | — |

When `copy kit` or `copy scene` makes a Scene present, mark the whole Scene and
its Pattern dirty (`autosave_markSceneWithPatternDirty()`), so settings or an
Effect pasted earlier are captured.

### 9.10 Names (HCNAMES)

- Pastes copy the **name** of each copied object's row, with its source token,
  and clear the refreshed flag `R`. Clearing `R` is what stops the boot reader
  from reloading the library object over the pasted content; the copied token
  keeps the row's origin accurate (`-` would wrongly say it inherits from the
  parent). Rows: `copy scene` → Scene, Kit, six Instruments, Pattern, Effect;
  `copy kit` → Kit and six Instruments; `copy instrument` → one Instrument
  (its type field follows); `copy effect` → Effect; `copy pattern` → Pattern;
  repeated for every fan-out destination.
- Clears keep names and tokens; rows whose content changed lose `R`.
- Each paste records `remap[dst] = remap[src]` if set, else `src`, in the 9 kB
  name buffer, so chained pastes resolve to the original row.
- After the last queued item, one filesystem op reads `.hcnames`, keeps the
  original rows in the name buffer, overlays the remapped names and sources,
  writes `.hcnamtmp` and swaps it in (the existing safe-rewrite phases), then
  marks the changed source bytes dirty. On a card error the old `.hcnames`
  stays, the error is traced, and suspension ends.
- The names are written before AutoSave captures the pasted content. A power
  loss in that window shows the new names over the previous AutoSave content
  until the next save.

### 9.11 Underlines

`va_applyVoiceMarkers()` and `menu_effectCellAutomated()` skip targets waiting
in the register; when one finishes, the presence search restarts.
`menu_voiceAutoOverlayPatternDeleted()` becomes a general "Pattern changed"
notification, called after every Pattern paste or clear on the viewed Scene.

---

## 10. Related change: retire global `srt`, Effect Morph on PERF

User decision: remove global `srt` as a parameter and put Effect Morph in its
PERF cell.

**What `srt` is today:** `scene_settings_t.voice_decimation_all` (0..127,
default 127), applied as `mixer_decimation_rate[6]`, a multiplier on every
voice's decimation counter. It is the last PERF cell (`PAR_VOICE_DECIMATION_ALL`),
a Scene automation and LFO target (`SCENE_MOD_TARGET_KIND_DECIMATION_ALL`), the
`sceneset.scg` key `voice_decimation_all`, an AutoSave Scene parameter cell,
and the MIDI CC `VOICE_DECIMATION_ALL`.

| Area | Change |
|---|---|
| Mixer | `mixer_decimation_rate[]` shrinks to 6 entries and the `* mixer_decimation_rate[6]` multiply is removed. For a Scene at 127 the result is bit-identical (multiplying by exactly 1.0f is exact): sound class S0. Per-voice decimation is the only sample-rate reduction left, unless a future Effect type adds one (user). |
| Sound | Scenes saved with `srt` below 127 lose that extra decimation. |
| SceneData, Preset | the `voice_decimation_all` field, its setter and the runtime apply are removed |
| AutoSave | Scene cell 7 becomes a reserved cell: written as 127 (neutral for older firmware), ignored on restore; record layout, mask and format version unchanged |
| `sceneset.scg` | no longer written; ignored when an existing file has it. Older firmware defaults a missing key to 127 |
| Scene target | the `srt` row stays in the positional table as a retired placeholder (kind `SCENE_MOD_TARGET_KIND_RETIRED`, no use flags), so the IDs of `7dc`, `Nou`, `Nfx` and `fxm` do not move. Pickers skip it, validation rejects it, and existing Pattern entries and LFO tokens that name it do nothing |
| MIDI | `VOICE_DECIMATION_ALL` CC handling removed (the CC is unassigned; the enum position stays so later CC numbers do not move) |
| Menu | `menu_parseGlobalParam()` branch and the O1 Settings-Load equalisation of `srt` go away |
| PERF cell | `fxm`: Effect Morph of the active Scene (`effect_morph_amount`, 0..255), committed like the Effect page's `mrp` through `effects_setMorphAmount()` (fans out through the edit mask), AutoSave cell 40, double-speed endless pot like Scene Morph. |

---

## 11. Removal list and resources

### 11.1 Removal list

| Location | Remove / replace |
|---|---|
| `Core/Menu/copyClearTools.c/h` | delete (old clear menu with `autom.1`/`autom.2`, direct LCD writes, `copyClear_clearTrackAutom()` stub, `copyClear_copy*()`, `MODE_*`/`CLEAR_*`) |
| `PatternData.c/h` | `pat_copyTrack/Pattern/Bar()` no-ops and comments |
| `buttonHandler.c` | copy branches in `buttonHandler_partButtonPressed/Released()` and `handleVoiceButton()`; `BUT_COPY` cases; clear-cancel in the SHIFT release |
| `menu.c` | clear-mode block in `menu_parseEncoder()`; stale notes about `copyClear_isClearModeActive()` |
| `Makefile` | `copyClearTools.c` out; four sources and an include path in |
| global `srt` | per §10 |

Kept: `patSvc_clearTrack()`, `patSvc_clearPattern()`, `pat_releaseStepDynamic()`,
the erase-mode gesture.

### 11.2 RAM (SRAM1 `.bss`, firmware lifetime)

| Owner | Item | Bytes |
|---|---|---:|
| `copyClearSession.c` | operation state, selection, menu kind, flags | 6 |
| `copyClearSession.c` | source (kind, Scene, track, start, end) | 6 |
| `copyClearSession.c` | press order for the range rule | 16 |
| `buttonHandler.c` | consumed-edge masks | 4 |
| `copyClearService.c` | queue 4 × 4 B + head/count | 18 |
| `copyClearService.c` | register (approved) | 18 |
| `copyClearService.c` | boundary Scene, phase, cursors | 5 |
| `filesystem.c` | name-buffer borrow state | 1 |
| **Total** | | **74** |

Stack: one 252 B automation buffer on the merge path. Pool: 132 B per Scene
reserved for the swap block (pool data, not RAM).

### 11.3 Flash and CPU

About 10–14 KB flash. Foreground only: 8 steps per tick; compaction moves are
bounded per tick; whole-Pattern copy and snapshots are single passes well under
a millisecond.

---

## 12. Decision log

### 12.1 Startup brief

| # | Decision |
|---|---|
| C1 | One destination per press; Pattern data never fans out; Scene children fan out (§4.5). |
| C2 | Source read when each paste starts, copied into the 9 kB name buffer. |
| C3 | Cross-Scene step/bar/track pastes by navigating; written to the active Scene. |
| C4 | Names and source tokens copied with `R` cleared; names kept on clear; one `.hcnamtmp` swap at the end. |
| C5 | No undo. |
| C6 | Replace and merge both offered. |
| C7 | Pastes extend length; `copy track` carries length/scale/shuffle; `clear track` resets them. |
| C8 | Targets follow the destination, best effort, same dtype, else dropped. |
| C9 | Old clear menu deleted. |
| C10 | No Instrument clear; slot-6 decay pair only slot 6 → slot 6. |
| C11 | Scene settings = all of `sceneset.scg`; `copy effect` is the record only. |
| C12 | `clear scene`: active keeps its Kit; others are emptied. |
| C13 | Gestures with a selection menu. |
| C14 | Clears start at `cancel`, apply on release; copies need no confirmation. |

### 12.2 Second round (user, 2026-10-01)

| Item | Decision |
|---|---|
| `srt` | Retired; Effect Morph takes its PERF cell (§10). |
| Swap block | Permanent 132 B reserve in every pool, kept for other future uses (§9.6). |
| Paste method | Always step by step, queued (§9.5). |
| Non-present persistence | Accepted. |
| PERF Scene paste | Writes the pressed Scene; no Scene switch. |
| Pots with a clear menu shown | Ignored. |
| Indicator formats | Accepted (§8.1). |
| Retarget by page position | Allowed when the dtype matches. |
| Fan-out | Scene children fan out; Pattern never; Scene and settings copies/clears set or reset the mask (§4.5). |
| FX sequence clear | Lock masks and lane values zeroed. |
| `clear scene`, `clear scene settings` | Both reset the mask to the Scene itself. |
| Source token on copy | Copied with `R` cleared (§9.10). |

### 12.3 Third round (user, 2026-10-01)

| Item | Decision |
|---|---|
| F1 PERF Effect Morph cell | Label `fxm`; double-speed endless pot like Scene Morph. |
| F2 `srt` | Eliminated: not loaded from Scenes that have it, not saved in new ones. Per-voice sample-rate reduction is the only one left, unless a future Effect type adds one. Scenes that used it sound different (accepted). |
| F3 Fan-out | Any paste of a Kit, Instrument, Effect or any sub-component of those also applies to every Scene in the destination's edit mask. |
| F4 Pot clear over Effect Morph (PERF and EFFECTS) | Clears both the FX sequence lane and the Pattern automation. The FX sequence part fans out; the Pattern part never does. |
| F5 Fan-out through the pressed Scene's own mask | Intended: the edit mask locks Scenes together for everything except the Pattern. |

First-round decisions are folded into §3–§9 and were recorded in revision 2.

---

## 13. Open follow-ups

None. The third round (F1–F5) is resolved in §12.3. The line-level schedule is
`S075_PH6_COPYCLEAR_IMPLEMENTATION.md` (root).

---

## 14. Implementation stages and hardware checks

Each stage: `make all && make img`, `link_budget.py`, hardware check by the
user.

| Stage | Content | Hardware check |
|---|---|---|
| 1 | LED consolidation (§8.2) | Existing LED behaviour unchanged in every mode |
| 2 | Retire global `srt`, Effect Morph on PERF (§10) | PERF cell edits and fans out; old Scenes load; old `srt` automation inert; AutoSave round trip |
| 3 | `Core/Menu/CopyClear/` skeleton, button handling, menus, indicator, LEDs, suspension gates, name-buffer borrow; old code removed | Exhaustive mode × button × state test, no leaked edges; AutoSave, trace and settings writers pause and resume (trace `W`/`S`/`A`); repair resumes; Load reloads its index |
| 4 | Exclusive boundary; swap block reserve and legacy relocation; snapshot, retargeting, step-by-step placement; step/range/bar/track pastes; wrap; length extension; queue | Each selection on a playing Pattern without glitches; reversed and overlapping ranges; wrap; cross-track and cross-Scene retargeting; nearly full pool: paste dropped whole or completed, never partial |
| 5 | Step/bar/track clears; PERF Pattern clears | Each selection; track settings reset; underlines restart |
| 6 | Register, underline suppression | 8 limit; values never change; VOICE and Effect underlines |
| 7 | FX step copy and clears, fan-out, type check | Locks and values; fan-out to same-type mask members; mismatch dropped |
| 8 | Scene-level pastes and clears, `clear send`, mask exchange and reset, present rule, fan-out | Active and other Scenes; Advanced limit; LFO `self`; decay pair; mask cases; dark LED for Kit-less Scenes; power cycle after each |
| 9 | Name write | Names and sources after each kind, fanned-out and chained pastes; card-error path ends suspension |

Regression at every stage: Load/Save, the Pattern Stack Service under playback
and Scene switching, underlines, AutoSave power cycle.
