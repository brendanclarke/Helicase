# Copy/Clear Utilities (Phase 6)

As-built reference for copy and clear of Pattern data, Scenes and Scene
children on the LXR-02: what the user can do, what each operation changes,
and how the code does it. Written for a developer who has to read, fix or
extend `Core/Menu/CopyClear/`.

- **Current as of:** Session 078 close (2026-10-10, `dev-ph6-cleanup`).
  Built in S075 (base pass, F1 follow-up, F2 follow-up); S076 P3 added morph
  reset/copy operations and P4 added reload scene; S077 P1 migrated copy
  snapshot to `pat_background_region`, P4 corrected Scene morph fan-out, and
  P5 added bar-to-step cross-kind paste; S078 added the track run mode and the
  track Morph endpoints to track/morph copy and clear, and STEP track-settings
  pot clears (whole Scene, and held steps only from the STEP held-step
  overlay). History and every user decision are
  in `knowledge_files/log_archive/075_SESSION_HANDOFF_LOG.md` §4–§10,
  `076_SESSION_HANDOFF_LOG.md` §4–§5, `077_SESSION_HANDOFF_LOG.md`
  §2/§5/§6 and `078_SESSION_HANDOFF_LOG.md` §2.5, §3.2, §4.
- **Where this document and the code disagree, the code wins;** fix this
  document in the same change.
- **Related documents:**
  - `PATTERN_DYNAMIC_STACK.md` §3 (swap block), §4 (block format), §12.17
    (exclusive boundary, raw block API, sliding compaction);
  - `MODULE_INTERCHANGE_SPEC.md` "Core/Menu/CopyClear" (call map);
  - `BANK_PRESET_ARCHITECTURE.md` §2 (VOICE edit masks) and §3 (Scene
    contents);
  - `dsp_instruments_effects/EFFECTS_BUS_REFERENCE.md` §8–§9 (Effect page,
    fan-out, FX sequence);
  - `AUTOSAVE.md` (suspension gates, dirty markers), `FILESYSTEM_SPEC.md`
    (HCNAMES rows, the `HNcU` write, the name-buffer loan), `DEV_MODES.md`
    (trace ring), `STORAGE_SRAM_MANIFEST.md` (RAM).

## Contents

1. Mental model in one page
2. Terms
3. Quick reference: gestures by mode
4. Operation lifecycle
5. Sources, ranges and the row gesture
6. Copy: menus and what each selection does
7. Clear: menus and what each selection does
8. Pot clears (automation under an endless pot)
9. Screens, labels and LEDs
10. Fan-out, the edit mask and the present rule
11. Architecture: files, entry points, state, the service tick
12. Pattern engines: boundary, swap block, pastes, clears, whole-Pattern work
13. Scene-level executors
14. Names (HCNAMES)
15. Trace stage `c` (DEV builds)
16. Verification checklist (hardware)
17. Resources
18. Known limits and design rationale
19. How to modify
20. History

Appendix A. Reading code comments: section and decision references

---

## 1. Mental model in one page

- **The user holds COPY.** While it is held, copy/clear owns every
  front-panel edge first. With SHIFT held at the COPY press it is a **clear
  operation**, otherwise a **copy operation**.
- **Copy:** the first object button pressed (step, bar, track, Scene or FX
  step) becomes the **source** and opens a menu of **selections** (for
  example `step -> repl`). Every later press of a destination queues a
  **paste** of the current selection from the source to that destination.
  The user may navigate (change bar, track, mode or Scene) while COPY is
  held and keep pasting.
- **Clear:** each object button pressed opens a clear menu at `cancel`; on
  its release the shown selection is queued as a clear. Endless-pot turns
  (with no menu shown) clear that parameter's automation.
- **Releasing COPY** ends the interaction. Queued work finishes in the
  background (`ccSvc_tick()`, 500 Hz, foreground), bounded per tick.
- **Feedback is immediate even when the work is not:** the destination LEDs
  flash once and trigger bits change at the press; the pool work (blocks,
  specials, automation) can take longer.
- **Safety rules:** only one Scene's Pattern is written at a time, through an
  exclusive boundary; every step write keeps the publication order (TIM3
  never sees half a block); a paste completes whole or is dropped whole;
  drops are silent (traced in DEV builds); AutoSave and Pattern maintenance
  pause while an operation runs.
- **Pattern data never fans out. Scene children** (Instrument, Kit, Effect,
  FX sequence, `send`) **fan out** to every Scene in the destination's VOICE
  edit mask.

---

## 2. Terms

These are the user's terms; use them in code comments and UI text.

| Term | Meaning |
|---|---|
| Copy/clear button | `BUT_COPY`, `LED_COPY` |
| Copy operation | Starts when COPY is held (no SHIFT) and a copy object button is pressed; lasts until COPY is released and every paste it queued has finished |
| Clear operation | Starts when SHIFT is held and COPY is pressed and held, then an object is pressed or a pot turned; lasts until COPY is released and every clear it queued has finished |
| Menu interaction | The part of an operation in which buttons act; ends at COPY release |
| Source | What the first copy object button selected: step, step range, bar, bar range, track, Scene, FX step or FX step range, with its Scene and track |
| Selection | The copy or clear menu entry chosen with the encoder |
| Paste | Applying the current selection from the source to one destination |
| Active Scene | The Scene being viewed and written to (not defined by playback) |
| Edit mask | The VOICE edit fan-out mask (`scene_mask_voice_edit`), one directional entry per Scene |
| Fan out | Applying the same change to every Scene in the destination's edit mask |
| Queue | Pastes and clears waiting to run (up to 4) |
| Register | Parameters waiting for a pot clear (up to 8) |
| 9 kB name buffer | `fs_list_cache_name`, the 9,000 B browser name cache, lent to copy/clear |
| Swap block | The permanently reserved top 132 B of every Scene's Pattern pool |
| Source indicator | The ≤ 8-character source text on the display |

Tracks are 1..7 in this document (0..6 in code); Instrument slots 1..6 (0..5
in code). Track 7 plays slot 6's alternate sound and **behaves as track 6**
wherever an Instrument or per-slot setting is involved.

---

## 3. Quick reference: gestures by mode

| Mode | Copy (COPY held) | Clear (SHIFT + COPY held) |
|---|---|---|
| VOICE, STEP | SEQ = step source (press, range); TRACK = track source; SEQ presses then paste steps | SEQ = step clear; TRACK = track clear; pots = automation clear |
| STEP | SELECT = bar source (press, range); SELECT presses then paste bars | SELECT = bar clear |
| PERF | SEQ = Scene source; SEQ presses then paste into the pressed Scene; TRACK = track source | SEQ = Scene clear; TRACK = track clear; pots over `1vm..6vm`, `fxm` = automation clear |
| EFFECTS (SHIFT+PERF) | SEQ = FX step source (press, range); TRACK = track source | SEQ = clear that FX step at once (no menu); TRACK = track clear (+ `send`); pots over Effect rows / `mrp` = lane + automation clear |
| LOAD/SAVE, MENU, SOM | refused | refused |

Navigation while COPY is held (copy): BAR (not while a SEQ is held), TRACK
(in VOICE/STEP), MODE VOICE/STEP/PERF, SHIFT+PERF to EFFECTS, PERF SEQ (to
change the active Scene). SHIFT stays a MODE modifier and never turns a copy
into a clear.

---

## 4. Operation lifecycle

### 4.1 Copy operation

1. COPY pressed (no SHIFT): the copy LED lights **steady**.
2. The first copy object button opens its menu at the default selection and
   shows the source indicator. Step/bar/FX rows are **provisional** until the
   last held row button is released; track and Scene sources are set on
   press. From the menu opening, the copy LED **blinks** until release.
   AutoSave and Pattern maintenance are suspended (§11.6).
3. The encoder turns the selection (clamped, no wrap); the encoder click does
   nothing.
4. Each destination press queues a paste with the selection shown at that
   moment and flashes the destination LEDs once. Up to 4 pastes may wait; a
   further press while 4 wait is dropped silently (`QUEUE_FULL`). A paste
   identical to its source does nothing (`PASTE_NOOP`).
5. **COPY release ends menu interaction**; queued pastes finish in the
   background. A row still held at release is dropped (nothing applies).

### 4.2 Clear operation

1. SHIFT pressed (SHIFT LED lights), then COPY pressed: **SHIFT and COPY
   LEDs blink, latched until COPY is released**, whether or not SHIFT stays
   held. SHIFT's release does not run its mode actions or cancel anything.
2. The page stays visible for pot clears (§8). **While COPY is held no pot or
   encoder changes a parameter value.**
3. An object button opens its clear menu. The first object of a **button
   group** (SEQ steps, SELECT bars, TRACK, PERF SEQ Scenes) opens at
   `cancel`; further objects of the same group **keep the selection**, so
   each is a real clear. A new group, or COPY release, starts at `cancel`
   again. A kept selection that does not exist in the new menu (TRACK `send`
   outside EFFECTS) falls back to `cancel`.
4. On release of the object button(s) the clear is queued unless the menu
   shows `cancel`. A press and release with nothing else held selects one
   object; only a press while another row button is held makes a range.
5. EFFECTS SEQ clears and pot clears act at once, with no menu. Once an
   object has opened a menu, pots do nothing until COPY is released.
6. COPY release ends menu interaction; an object still held applies nothing.

### 4.3 Refusals (silent; `OP_REFUSED` in DEV builds)

No operation starts while the sequencer records or erases, while Load/Save
or an Instrument Load transaction owns the UI, in LOAD/SAVE, MENU or SOM
mode, or while the **previous operation's** pastes/clears are still queued
(they read that operation's source). A pending register drain or name write
does not block a new operation. SHIFT + COPY while recording and running is
still the sequencer erase gesture.

Queued work that cannot run is dropped silently: not enough pool space, the
Advanced instrument limit, an FX step paste onto another Effect type, a
missing source. Work that needs a busy Preset apply worker (for example right
after a PERF Scene switch) **waits** at the head of the queue.

### 4.4 Background end

After COPY is released, the service runs the queue and the register to
empty, waits for any apply worker it started, writes the names (§14), returns
the name buffer, and calls `copyClear_serviceFinished()`, which forgets the
source and ends the suspension.

---

## 5. Sources, ranges and the row gesture

### 5.1 Sources

| Mode | Object gesture | Source kind (`cc_kind_t`) | Set when |
|---|---|---|---|
| VOICE, STEP | SEQ press and release, or a SEQ range | `CC_KIND_STEP` (absolute step 0..127 on the active track) | last held SEQ released |
| STEP | SELECT press and release, or a SELECT range | `CC_KIND_BAR` (bar 0..7 on the active track) | last held SELECT released |
| VOICE, STEP, PERF, EFFECTS | TRACK press | `CC_KIND_TRACK` | press |
| PERF | SEQ (Scene) press | `CC_KIND_SCENE` | press |
| EFFECTS | SEQ press and release, or a SEQ range | `CC_KIND_FX_STEP` (0..15) | last held SEQ released |

`cc_source_t` (6 B): kind, Scene, track, start, end (press order; start may
exceed end for a reversed range), reserved.

### 5.2 The row gesture (literal sequence, user)

| # | Action | Copy | Clear |
|---|---|---|---|
| 1 | press COPY | LED steady | SHIFT + COPY blink (clear) |
| 2 | press a step | menu opens, indicator = that step (provisional); copy LED blinks | menu shows that step; selection kept within the group |
| 3 | press a second step (first held) | indicator = range first → second | same |
| 4 | press a third step | range = second → third (most recent held → new) | same |
| 5 | release second and third | nothing changes | same |
| 6 | press a new step (first still held) | range = first → new | same |
| 7 | release all | nothing on screen; the source is set | clear queued unless `cancel` |
| 8 | press another step | paste | new single object |

Rule: the **end** is a button pressed while another button of the same row is
held; the **start** is the most recently pressed button still held at that
moment. Later holds take precedence; the last pair before all buttons are
released is the source. Example (user): hold 7, hold 3, hold 11, press and
release 2, press and release 16, release all → source 11–16.

Implementation: the press stack (`cc_rowStack[8]`, sixteen 4-bit slots +
count) holds **row indices** 0..15 (SEQ) or 0..7 (SELECT), never absolute
steps. `cc_rowAbsolute()` converts to `16 · visible bar + index` only when
start/end are formed. (Before F1 the stack held absolute steps and truncated
them to 4 bits, which broke every range in bars 2..8.)

### 5.3 Range rules

- Ranges never cross bars; BAR is consumed while a SEQ source button is held.
- Order runs from start to end: SEQ 11 held then SEQ 7 pressed, pasted at 4:
  11→4, 10→5, 9→6, 8→7, 7→8. Bar ranges reverse completely (SELECT 6 then
  SELECT 2: steps `6·16+15` down to `2·16`).
- Destinations **wrap**: past step 128 the paste continues at step 1 of the
  same track; FX steps wrap past 16; bars past 8.

---

## 6. Copy: menus and what each selection does

### 6.1 Copy menus (default first; `copyOps.c` label tables)

| Source | Menu (`cc_menu_t`) | Selections |
|---|---|---|
| step, step range | `CC_MENU_COPY_STEP` | `step -> repl`, `step -> merge`, `auto -> repl`, `auto -> merge` |
| bar, bar range | `CC_MENU_COPY_BAR` | `bar -> repl`, `bar -> merge`, `auto -> repl`, `auto -> merge` |
| track | `CC_MENU_COPY_TRACK` | `track`, `instrument`, `inst -> morph` |
| Scene | `CC_MENU_COPY_SCENE` | `scene`, `settings`, `kit`, `effect`, `pattern`, `scene -> morph` |
| FX step, FX range | `CC_MENU_COPY_FX` | `step` |

Enum values: `CC_COPY_ALL` (`… -> repl`), `CC_COPY_MERGE_ALL`
(`… -> merge`), `CC_COPY_MORPH` (2, track), `CC_COPY_AUTO` (`auto -> repl`),
`CC_COPY_MERGE_AUTO` (`auto -> merge`); `CC_COPY_TRACK`/`CC_COPY_INSTRUMENT`;
`CC_COPY_SCENE`, `CC_COPY_SCENE_SETTINGS`, `CC_COPY_KIT`, `CC_COPY_EFFECT`,
`CC_COPY_PATTERN`, `CC_COPY_SCENE_MORPH` (5, Scene).

### 6.2 Pastes and where they write

| Source | Paste by | Writes |
|---|---|---|
| step / step range | SEQ press in VOICE or STEP | active Scene, active track, from the pressed step |
| bar / bar range | SELECT press in STEP | active Scene, active track, from the pressed bar |
| track | TRACK press in VOICE, STEP, PERF, EFFECTS | active Scene, pressed track |
| Scene | SEQ press in PERF | the **pressed** Scene; no Scene switch |
| FX step / range | SEQ press in EFFECTS | the active Scene's FX sequence, from the pressed step |

A PERF SEQ press while a step/bar/track source is set changes the active
Scene in the normal way (navigation), so a Pattern paste can cross Scenes.

### 6.3 Step and bar family: per source → destination step (`ccCopy_buildStep()`)

| Selection | Trigger | Specials (note, velocity, probability) | Automation |
|---|---|---|---|
| `… -> repl` | := source | := source | := source |
| `… -> merge` | destination OR source | each source special wins; specials only the destination has are kept | union; source wins on the same target; source entries first; destination entries beyond 63 dropped silently |
| `auto -> repl` | unchanged | unchanged, **except probability := source** (set, changed or removed) | := source |
| `auto -> merge` | unchanged | unchanged (probability included) | union as above |

- An empty source step under `… -> repl` empties the destination; under a
  merge it changes nothing.
- **Why probability travels with automation:** probability gates the whole
  step, automation included (`PATTERN_DYNAMIC_STACK.md` §6.1), so it belongs
  with the automation (user, F1).
- **Length extension:** `length := max(length, 16 · (highest written bar + 1))`;
  a paste never shortens a track.
- **Early trigger bits:** `… -> repl` and `… -> merge` write the destination
  trigger bits at the press (§12.6).
- **Retargeting** when the destination track or Scene differs (§6.6).
- Never fans out.

**Bar-to-step paste (S077 P5):** when a bar or bar range is the copy source,
a SEQ press in VOICE/STEP mode pastes the bar content starting at the pressed
step (cross-kind paste). The source geometry is identical to a bar-to-bar
paste (bar indices x 16); only the destination is in step space (absolute
step, not multiplied). The reverse (step source -> SELECT press) is
navigation, not a paste. The job kind is `CC_KIND_BAR_TO_STEP`.

### 6.4 Track

- `copy track` (`CC_COPY_TRACK`): all 128 steps as `… -> repl`, plus the
  track's length, scale, shuffle and (S078) run mode. Early triggers. Never
  fans out. The track Morph endpoints are Scene settings and are not copied
  by `copy track`; `copy inst -> morph` handles them.
- `copy instrument` (`CC_COPY_INSTRUMENT`): the source track's slot (type,
  Normal and Morph images) replaces the destination track's slot **and fans
  out** to the same slot in every Scene in the destination's edit mask.
  - The Advanced limit (`instrumentManager_typeSelectableForSceneSlot()`)
    for any member drops the paste (`ADVANCED_LIMIT`).
  - LFO target-voice selectors that pointed at the source slot move to the
    destination slot (`preset_retargetSelfLfoVoice()`, the Kit Save `self`
    rule).
  - Slot-6 alternates: a HiHat's `_choke` rows travel inside its image; the
    Kit-owned `slot6_track7_*` decay pair is copied only slot 6 → slot 6
    (tracks 6 and 7); otherwise the destination keeps its own pair.
  - Same Scene and same slot: nothing to do.

### 6.5 Scene (PERF), destination = the pressed Scene

| Selection | Copies | Fans out | Also |
|---|---|---|---|
| `copy scene` | settings, Effect, Kit, Pattern | no; the destination's edit-mask entry is set by the **exchange** below | makes the Scene present; names |
| `copy scene settings` | all of `scene_settings_t` (everything in `sceneset.scg`, Effect Morph included) | no; mask exchange | — |
| `copy kit` | `kit_t` (six slots + Kit settings) | yes | makes each written Scene present; names |
| `copy effect` | `effect_record_t` (may change the destination's type) | yes | names |
| `copy pattern` | the whole region including the Pattern globals; automation retargeted when the Instrument or Effect types differ | no | names |
| `copy scene -> morph` | all 6 slots' Normal → Morph, FX send × 6 Normal → Morph, slot-6 decay Normal → Morph, Effect Normal → Morph (same type only), and (S078) all 7 tracks' Pattern Normal length/scale/shuffle → the Scene's track Morph endpoints | yes | — |

**Edit-mask exchange** (`bank_exchangeVoiceEditMask(src, dst)`): the
destination's entry becomes the source's entry with the source and
destination bits exchanged; other Scenes' entries are unchanged; masks are
then revalidated.

```
m  = mask[src]
m' = (m & ~(bit(src)|bit(dst)))
   | (m & bit(src) ? bit(dst) : 0)
   | (m & bit(dst) ? bit(src) : 0)
mask[dst] = m'
```

### 6.6 Retargeting automation (`ccCopy_retarget()`)

Applied when the destination track or Scene differs from the source, and for
`copy pattern` when the destination's Instrument or Effect types differ.
**Best effort:** a slightly different parameter is fine; a target with no
match, not automatable, or with a different dtype is dropped (counted in
`JOB_STATS`).

| Source target | Destination target |
|---|---|
| Instrument parameter of the source track's own slot | the destination track's slot: same type → same parameter; else same storage key; else same VOICE page position; must keep the dtype |
| Instrument parameter of another slot | the same slot in the destination Scene, matched as above |
| `Nvm`, `Nou`, `Nfx` of the source's own slot | same kind on the destination slot |
| track-7 alternates (`7dc`, `_choke` parameters) | the destination track 7's alternate decay when one exists; else dropped |
| Scene-wide (`fxm`) | unchanged |
| Effect parameter (448 + local) | same type → unchanged; else the same key in the destination type; else dropped |
| automation off (`0x1FF`) | unchanged |
| retired `srt` (390) | dropped |

Two entries that land on the same target keep the first. Track 7's own slot
is slot 6. `ccCopy_retargetNeeded()` is zero for the identity mapping (same
Scene and slot, or identical types), so the engines skip the work.

### 6.7 FX step (EFFECTS)

Each source step's lock mask and 16 lane values replace the destination
step's, in range order with wrap, and **fan out** to the destination's edit
mask (`effects_pasteSeqStep()`). The source steps are snapshotted on the
stack (288 B). If the destination Effect type differs from the source's, the
paste is dropped (`FX_TYPE_MISMATCH`).

---

## 7. Clear: menus and what each selection does

### 7.1 Clear menus (`clearOps.c` label tables; `cancel` first)

| Mode | Object | Menu | Selections |
|---|---|---|---|
| VOICE, STEP | SEQ, SEQ range | `CC_MENU_CLEAR_STEP` | `cancel`, `step`, `step auto`, `step notes` |
| STEP | SELECT, SELECT range | `CC_MENU_CLEAR_BAR` | `cancel`, `bar`, `bar auto`, `bar notes` |
| VOICE, STEP, PERF | TRACK | `CC_MENU_CLEAR_TRACK` | `cancel`, `track`, `track auto`, `track notes`, `reset morph` |
| EFFECTS | TRACK | `CC_MENU_CLEAR_TRACK_FX` | as above + `send` |
| PERF | SEQ (Scene) | `CC_MENU_CLEAR_SCENE` | `cancel`, `scene`, `settings`, `pattern`, `automation`, `notes`, `fx`, `fx sequence`, `reset morph`, `reset fx morph`, `reload scene` |
| EFFECTS | SEQ | none | clears that FX step at once |
| any | endless pot | none | §8 |

### 7.2 What each clear does

| Selection | Effect | Fans out |
|---|---|---|
| `step` / `bar` | trigger off, block released | no |
| `track` | as above for 128 steps; length, scale, shuffle, run mode to defaults (16, scale CC 76 = 1/16, 0, `fwd`) | no |
| `… auto` | automation removed; trigger and specials kept | no |
| `… notes` | trigger off; note and velocity specials removed; **probability kept** (it gates the step's automation); automation kept | no |
| `send` (EFFECTS TRACK) | the track's slot (track 7 → slot 6): **both** FX-send endpoints 0, fader `pre` | yes |
| `scene`, active Scene | everything except the Kit to defaults (settings, Effect `off`, FX sequence, Pattern); edit mask reset to the Scene itself | no |
| `scene`, another Scene | emptied, Kit included; Bank-present bit off (dark in PERF); edit mask reset to itself | no |
| `settings` | Scene settings to defaults (St1 routes, compressor off/0/0/off, sends 0, …); edit mask reset to itself | no |
| `pattern` | the Pattern region to empty | no |
| `automation` / `notes` (PERF) | as the track selections, on all 7 tracks | no |
| `fx` | Effect record to defaults (`off`) | yes |
| `fx sequence`, EFFECTS SEQ | sequence steps only (all 16, or the one pressed): lock masks and lane values to their empty state; parameters, run mode, length, scale stay | yes |
| `reset morph` (VOICE/EFFECTS TRACK) | the track's slot: all Morphable descriptor Morph endpoints := Normal, plus correlated Scene morph endpoints (FX send morph, Kit slot-6 decay morph) and (S078) the track's Morph length/scale/shuffle := each member's own Pattern Normal | yes |
| `reset morph` (PERF Scene) | all 6 slots' Morphable Morph := Normal, plus all correlated Scene morph endpoints, every morphable Effect morph endpoint and (S078) all 7 tracks' Morph length/scale/shuffle := the member's own Pattern Normal; does NOT clear morph amounts | yes |
| `reset fx morph` (PERF Scene) | only the Effect's morphable morph endpoints := Normal | yes |
| `reload scene` (PERF Scene) | if the Scene has a valid numeric HCNAMES source (0..999), issue a full Scene reload from the SD card through `preset_loadSceneForScenes()`; otherwise silently dropped. Fire-and-forget: returns `CC_RUN_DONE` immediately | no |

Names stay on every clear (rows whose content changed lose `R`, §14). There
is no Instrument clear.

### 7.3 Clear order (user rule)

On acceptance: (1) the object's visible LEDs flash once; (2) for selections
whose final state is trigger-off (`step`, `bar`, `track`, `… notes`,
`scene`, `pattern`, PERF `notes`) the trigger bits of every object step are
written off at once (`ccClear_triggersOffNow()`: `pat_setStepActive()`, one
halfword RMW each, at most 896, ≈ 20 µs) and the step LEDs repaint, so the
current track's steps go dark immediately; (3) the pool work is queued.
A clear can be dropped later only by `EVACUATE_FAILED` on a block-rewriting
selection (`… notes`, `… auto`); its triggers then stay off (accepted).

---

## 8. Pot clears (automation under an endless pot)

- In a clear operation with **no menu shown**, turning an endless pot over an
  **automatable** parameter adds its Pattern target to the **register**,
  drops its underline at once (`menu_automationTargetCleared()`), and removes
  the automation in the background. The parameter value never changes. Over
  a non-automatable parameter nothing happens. In a copy operation pots do
  nothing.
- Pot clears never open a menu. To pot-clear again after an object menu was
  opened, release COPY and hold SHIFT + COPY again.
- The Pattern part covers the **active Scene, all 7 tracks × 128 steps, and
  never fans out**. The FX-sequence part (Effect rows, Effect Morph) clears
  the lane on all 16 steps **and fans out**.
- STEP track settings (S078 P3): with no SEQ steps held, a pot clear over
  `len`, `scl`, or `shf` (first half of the track-settings page) follows the
  rules above for the active track's target. With SEQ steps held after a
  TRACK press (the held-step overlay, S078 P2 §B), the same turn instead
  removes the target from the held steps only, at once, without the register
  (`sa_clearAutomationFromKnob()`); the name underline is then re-derived by
  the search.

| Page | Cell | Pattern target | FX sequence (fans out) |
|---|---|---|---|
| VOICE (Normal or Morph view) | Instrument parameter | `slot·64 + descriptor` | — |
| VOICE | voice Morph, audio out, FX send | `Nvm`, `Nou`, `Nfx` | — |
| VOICE | generated slot-6/track-7 decay | `7dc` | — |
| STEP (track settings, Normal or Morph view) | `len`, `scl`, `shf` | track target 405+t / 412+t / 419+t (t = active track) | — |
| PERF | `1vm..6vm` | matching Scene target | — |
| PERF | `fxm` (Effect Morph) | `fxm` (404) | lane 0 (Morph lane) |
| EFFECTS | Effect parameter, local *L* | `448 + L` | the lane mapped to *L* |
| EFFECTS | `mrp` (Effect Morph) | `fxm` | lane 0 |

Resolution: `menu_knobClearTarget()` (`menu.c`) → `cc_pot_target_t`
(`pattern_target`, `fx_lane`) → `copyClear_potTurned()` →
`ccClear_potTurned()` (FX lane cleared at once through
`effects_clearSeqLanes()`, Pattern target added with `ccSvc_registerAdd()`).

**Register:** 8 targets (`ccSvc_regTarget[8]` + count + Scene, 18 B), all for
one Scene. A ninth turn while 8 wait, a duplicate, or a target of another
Scene while entries wait does nothing and the underline stays
(`REG_REFUSED`). Entries run one at a time: each pass scans 896 steps and
rewrites the steps that hold the target through the swap block, so it never
fails for lack of pool space (`REG_DONE`). `ccSvc_targetPending()` keeps the
underline search from re-marking a waiting target. The PERF page shows no
underline.

---

## 9. Screens, labels and LEDs

### 9.1 The menu overlay

Drawn by `menu_repaint()` after the page renderer, replacing both rows
(`copyClear_menuVisible()` / `copyClear_formatMenu()`); no direct LCD writes.
While a menu is up, page repaints draw the menu, CGRAM underline updates are
held (`va_queueMarkerTransaction()` guard) and pot repaints do not draw the
page. `menu_copyClearMenuClosed()` invalidates the CGRAM cache and repaints
the page.

```
Copy    03T2s005        Clear   S03T2
[step -> merge ]        [track notes   ]
```

Row 0: `Copy` or `Clear` at column 0, the source indicator from **column 8
(the 9th character)** whatever the word length. Row 1: the bracketed
selection label (≤ 14 characters).

| Source | Indicator | Example |
|---|---|---|
| step | `NNTNsNNN` | `03T2s005` |
| step range | `sNNN-NNN` (start-end) | `s011-016` |
| bar | `SNNTNbN` | `S03T2b4` |
| bar range | `bN-N` | `b6-2` |
| track / instrument | `SNNTN` / `SNNiN` | `S03T2`, `S03i2` |
| Scene / pattern / effect / kit / settings | `SNN` / `SNNP` / `SNNf` / `SNNK` / `SNNc` | `S03P` |
| FX sequence / FX step / FX range | `SNNfs` / `SNNfsNN` / `fsNN-NN` | `S11fs`, `S11fs05`, `fs05-08` |

One-based numbers: Scenes 01..16, tracks 1..7, steps 001..128, bars 1..8,
FX steps 01..16. Clear menus show the same indicator for the pressed object.

### 9.2 LEDs (`cc_applyButtonLeds()`, `cc_flashObject()`)

| State | Copy/clear LED | SHIFT LED |
|---|---|---|
| copy held, no menu yet | steady | normal |
| copy menu open (provisional or set source) | blinking | normal |
| clear operation | blinking from the press | blinking from the press |
| released | off | physical SHIFT state |

- **No LED shows the source** (user): the indicator does. The S075 Stage 1
  "group blink" layer in `ledHandler.c` was reverted in F1.
- A paste or clear **flashes every visible LED of the destination object
  once** at acceptance (`led_flashGroup()` per row): steps of the visible bar
  for step pastes/clears (`(dst + i) % 128`), all 16 steps if the visible bar
  is inside a bar range plus the SELECT LEDs of the bars, FX steps
  `(dst + i) % 16`, the pressed Scene in PERF, and **only the TRACK LED** for
  `copy track`, `copy instrument` and `clear track` (the current track's step
  LEDs go dark at once through the early trigger write).
- `copyClear_postEvent()` re-asserts the copy/clear and SHIFT blinks after
  every event because a mode change clears blink slots.

---

## 10. Fan-out, the edit mask and the present rule

| Fans out | Does not fan out |
|---|---|
| `copy instrument`, `copy kit`, `copy effect`, `copy scene -> morph`, FX step pastes, `clear fx`, `clear fx sequence`, `clear reset morph` (track and Scene), `clear reset fx morph`, EFFECTS SEQ clear, the FX-lane part of an EFFECTS/PERF pot clear, `clear send` | every Pattern paste and clear (step, range, bar, track, `copy pattern`, `clear pattern`, `clear automation`, `clear notes`, the Pattern part of any pot clear); `copy scene` and `copy scene settings` (they **set** the mask); `clear scene` and `clear scene settings` (they **reset** it) |

- **Why:** Scenes in one edit mask are meant to share parameters and FX
  settings; the edit mask locks them together for everything except the
  Pattern (user, F3/F5).
- **Which mask:** the destination Scene's own entry
  (`bank_sceneFanoutMask(scene)`): the active Scene's entry for pastes in
  VOICE/STEP/EFFECTS, the pressed Scene's entry for PERF Scene pastes,
  filtered to present Scenes whose layout matches
  (`scene_editLayoutMatches()`: same Effect type and six Instrument types).
  Effect fan-out from a non-active origin uses that Scene's own entry
  (`effects_fanoutMask()`).
- **Revalidation:** `bank_revalidateVoiceEditMasks()` after any paste that
  can change a type (Instrument, Kit, Effect, Scene).
- **Names follow every fanned-out destination** (§14).
- **Present rule:** copies into a Scene that is not Bank-present are done,
  but only `copy scene` and `copy kit` make it present. A Scene that receives
  only a Pattern, settings or an Effect stays dark in PERF and is selectable
  only from the Load menu; its settings and Effect are AutoSaved only once a
  Kit makes it present (accepted). When `copy kit` or `copy scene` makes a
  Scene present, the whole Scene and its Pattern are marked dirty
  (`autosave_markSceneWithPatternDirty()`) so earlier pastes are captured.

---

## 11. Architecture: files, entry points, state, the service tick

### 11.1 Files (`Core/Menu/CopyClear/`, replaces `copyClearTools.c/h`)

| File pair | Prefix | Owns |
|---|---|---|
| `copyClearSession.c/h` | `copyClear_`, `cc_` | operation phase, source and the range rule, menu kind and selection, button routing and edge pairing, LEDs, the suspension predicate, the menu text |
| `copyOps.c/h` | `ccCopy_` | copy menus, paste requests, retargeting, merge rules (`ccCopy_buildStep()`), Scene-level paste executors, FX step paste |
| `clearOps.c/h` | `ccClear_` | clear menus, clear requests and early trigger-off, EFFECTS SEQ clear, the pot-clear front end, Scene-level clear executors |
| `copyClearService.c/h` | `ccSvc_` | queue (4) and run state, the Pattern paste/clear engines, whole-region copy/reset, the register (8), the lazy name-buffer loan, early-trigger masks and restore, the trickle governor, the name remap and HCNAMES write, the DEV trace feeds |

Makefile: the four sources in `SRCS` and `-ICore/Menu/CopyClear`.

Rule of thumb for where code goes: **policy** (what a selection means) in
`copyOps.c`/`clearOps.c`; **sequencing and bounded pool work** in
`copyClearService.c`; **pool layout** in `PatternData.c`; **exclusive
access** in `PatternStackService.c`; the **HCNAMES op** in `filesystem.c`;
**retained Scene writes** only through SceneData/Preset/EffectsManager.

### 11.2 Entry points into the module

| Caller | Call | When |
|---|---|---|
| `main.c` | `copyClear_init()` | boot, after `patSvc_init()` |
| `buttonHandler.c` `processPress()` | `copyClear_buttonPressed(btn)` | first, before every other handler; nonzero = consumed |
| `buttonHandler.c` `case BUT_COPY` | `copyClear_copyPressed(shift)` | COPY press, except SHIFT + COPY while recording and running (erase) |
| `buttonHandler.c` `processRelease()` | `copyClear_copyReleased()` (COPY), `copyClear_buttonReleased(btn)` | release; consumed releases return |
| `buttonHandler_processEvents()` | `copyClear_postEvent()`; `copyClear_eventOverflow()` | after every event; on event-ring overflow |
| `menu.c` `menu_repaint()` | `copyClear_menuVisible()`, `copyClear_formatMenu()` | overlay |
| `menu.c` `menu_parseEncoder()` | `copyClear_ownsEncoder()`, `copyClear_encoderTurned()` | turns only; clicks ignored |
| `menu.c` `menu_parseKnobDelta()` | `copyClear_ownsPots()`, `copyClear_potTurned()` | pot turns |
| `timebase.c` | `ccSvc_tick()` | 500 Hz foreground, right after `patSvc_tick()` |
| `filesystem_tick()` | `copyClear_backgroundSuspended()` | scheduler admission gates |
| `patSvc_tick()` | `copyClear_backgroundSuspended()` | repair-epoch gate |
| `va_scanService()` | `ccSvc_targetPending()` | underline filter for waiting pot clears |

Calls out (main ones): `patSvc_beginExclusive/endExclusive/
exclusiveCompactStep/exclusiveEvacuateSwapStep`; the PatternData raw block
API; `pat_setStepActive()`; `filesystem_borrowNameCacheScratch/
returnNameCacheScratch/identityRow/requestCopyResidentNames`;
`scene_commitSettings/commitKit/commitEffectRecord/settingsDefaults/
resetKitToDefaults`; `bank_sceneFanoutMask/exchangeVoiceEditMask/
resetVoiceEditMaskToSelf/revalidateVoiceEditMasks/setScenePresentMask`;
`preset_startInstrumentCopy/applyWorkersIdle/tickInstrumentApply/
startDrumsetApply/applySceneSettings/setVoiceFxSend*/setVoiceFaderSetting`;
`effects_pasteRecord/resetRecord/pasteSeqStep/clearSeqLanes/activateScene`;
`menu_copyClearMenuChanged/Closed`, `menu_patternContentChanged()`,
`menu_automationTargetCleared()`; `led_setBlinkLed/flashGroup`;
`autosave_markSceneWithPatternDirty/markEffectDirty`.

### 11.3 Button routing while COPY is held

**Copy operation:**

| Button | No source yet | step/range | bar/range | track | Scene | FX step |
|---|---|---|---|---|---|---|
| SEQ (VOICE/STEP) | source | **paste** | **paste** (bar-to-step: bar content starting at the pressed step) | ignored | ignored | ignored |
| SEQ (PERF) | Scene source | navigate | navigate | navigate | **paste** | navigate |
| SEQ (EFFECTS) | FX source | ignored | ignored | ignored | ignored | **paste** |
| SELECT (STEP) | bar source | navigate (bar) | **paste** | normal | ignored | ignored |
| SELECT (other modes) | normal | normal | normal | normal | normal | normal |
| TRACK | track source | navigate (VOICE/STEP; ignored in PERF/EFFECTS) | navigate (VOICE/STEP) | **paste** | ignored | ignored |
| BAR1/BAR2 | normal | navigate (not while SEQ held) | normal | normal | normal | ignored |
| MODE VOICE/STEP/PERF, SHIFT+PERF | normal | navigate | navigate | navigate | navigate | navigate |
| MODE LOAD/SAVE, MENU, SOM | ignored | ignored | ignored | ignored | ignored | ignored |

**Clear operation:** SEQ (VOICE/STEP), SELECT (STEP), TRACK (any mode), SEQ
(PERF) open the clear menu and queue on release; SEQ (EFFECTS) clears that FX
step at once; pots per §8; BAR and MODE (VOICE/STEP/PERF/EFFECTS) navigate;
SHIFT only drives its LED; encoder turns the selection while a menu is shown.

**Edge pairing:** every consumed SEQ/SELECT/TRACK press records its button in
`cc_seqMask` (16 bits), `cc_selectMask` or `cc_trackMask`, so its release is
consumed too, even after COPY was released. `copyClear_eventOverflow()`
forgets the masks and the row stack (a release may have been lost). SHIFT
edges are never consumed (they keep their MODE-modifier and LED roles).
**No consumed edge may reach step toggles, the VOICE hold overlay, the FX
lock hold, PERF Scene switching, mutes or audition.**

### 11.4 Session state (`copyClearSession.c`)

| Static | Bytes | Meaning |
|---|---:|---|
| `cc_state` | 6 | phase (`cc_op_t`: NONE, ARMED_COPY, COPY, CLEAR), operation kind, menu (`cc_menu_t`), selection, flags (`CC_FLAG_PAIR`, `CC_FLAG_OBJECT`), held row (NONE/SEQ/SELECT) |
| `cc_source` | 6 | `cc_source_t` |
| `cc_rowStack[8]` + `cc_rowCount` | 9 | press order, raw 4-bit row indices |
| `cc_seqMask`, `cc_selectMask`, `cc_trackMask` | 4 | consumed-edge masks |

### 11.5 Service state and the tick (`copyClearService.c`)

| Static | Bytes | Meaning |
|---|---:|---|
| `ccSvc_queue[4]` (`cc_job_t`: op, kind, scene, track, start, end) + head + count | 26 | FIFO of pastes/clears. `op` = class (`CC_JOB_PASTE` 0x10 / `CC_JOB_CLEAR` 0x20) \| selection |
| `ccSvc_earlySrc[4][16]`, `ccSvc_earlyPrev[4][16]`, `ccSvc_earlyFlags` | 129 | early-trigger masks per slot (§12.6) |
| `ccSvc_regTarget[8]` + count + Scene | 18 | pot-clear register |
| `ccSvc_runState` (`cc_run_t`: phase, sub, cursor, aux) | 6 | state of the running job/register pass; `phase` belongs to the executor, `sub/cursor/aux` to the engines |
| `ccSvc_flags` | 1 | `JOB_ACTIVE`, `REG_ACTIVE`, `ENDED` (interaction over), `NAMES_DIRTY`, `NAMES_BUSY`, `TRICKLE` |
| `ccSvc_claimScene`, `ccSvc_nameRetry`, `ccSvc_buf` | 4–7 | held exclusive claim, name-write retry counter, borrowed buffer pointer |
| `ccSvc_trickleCredit` | 2 | governor credit (µs, signed) |

`ccSvc_tick()` (500 Hz, after `patSvc_tick()`), in order:

1. Return while the HCNAMES write is in flight (`NAMES_BUSY`).
2. **Governor:** while `filesystem_status()` is busy (an older writer is
   finishing; no new one can start during suspension) set `TRICKLE`, add
   `CC_TRICKLE_US_PER_TICK` (2 µs) to the credit (cap 40 µs); with work
   pending and credit ≤ 0, return. Each engine call is then charged its
   measured TIM2 time (floor −30,000 µs). Average ≤ 0.1 % CPU. With an idle
   facade the governor is bypassed and the engines use their full per-tick
   counts (8 steps, 16 snapshot reads, 32 region entries).
3. **Running job:** call `ccCopy_runJob()` or `ccClear_runJob()` with the
   head job. `CC_RUN_WAIT` → return. `CC_RUN_DROP` with early triggers →
   `ccSvc_restoreEarlyTriggers()`. Then end the job (trace, release the
   claim, pop the queue).
4. **Running register pass:** `ccSvc_runRegister()` until done.
5. Start the next job (FIFO), else the next register pass.
6. Nothing queued and interaction ended: request the name write (needs the
   buffer and an idle facade; retried for 1,000 ticks = 2 s, then given up),
   return the buffer, and call `copyClear_serviceFinished()`.

Executors return `CC_RUN_DONE`, `CC_RUN_WAIT` (call again next tick) or
`CC_RUN_DROP` (dropped silently). A job stuck in WAIT for 5,000 ticks
(10 s) is traced once (`JOB_STALL`).

### 11.6 Suspension of background work (`copyClear_backgroundSuspended()`)

Nonzero from the start of an operation (first object press or pot clear)
until COPY has been released **and** the queue, the register, every apply
worker a paste started and the name write have finished.

| Site | Effect while suspended |
|---|---|
| scalar AutoSave scheduler (`filesystem.c`) | no ensure, no drain; holds `fs_autosave_page_suppressed`, so the next drain runs on the 250 ms continuation afterwards |
| semantic and non-semantic Pattern AutoSave schedulers | no admission |
| AutoSave trace and Pattern trace flush schedulers | no admission (trace records accumulate in RAM and are written afterwards) |
| `settings.cfg` writer | no admission |
| `patSvc_tick()` repair epoch | returns before scanning (cursor kept) |

A writer already running finishes normally; pastes do not wait for it unless
they need the name buffer. Dirty marks happen at every change, so AutoSave
captures everything after the suspension ends.

### 11.7 The 9 kB name buffer loan

- `filesystem_borrowNameCacheScratch()` succeeds only while the facade is
  idle; it tags the cache `FS_NAME_CACHE_COPYCLEAR` so every browser
  accessor reports "not loaded". `filesystem_returnNameCacheScratch()`
  clears it; Load/Save reloads its index on the next entry.
- While lent: `filesystem_clearNameCacheStorage()` and
  `filesystem_prepareLibraryNameCache()` do nothing, and `filesystem_start()`
  refuses every operation except the copy/clear name write (callers see a
  busy facade). Entering LOAD/SAVE right after releasing COPY therefore
  waits until the buffer is returned.
- **Borrowed lazily, only by:** the end-of-operation name phase (the
  `FS_INTERNAL_OP_UPDATE_HCNAMES_COPY` overlay applies the remap from offset
  0 of the buffer). Since S077 an overlapping paste no longer borrows the
  buffer; it snapshots into the background region's pool (§17). Clears, pot
  clears, whole-Pattern copy/reset, non-overlapping pastes and Scene-level
  data commits never wait for it.

| Offset (`CC_SCRATCH_*`) | Use | Bytes |
|---|---|---:|
| 0 | HCNAMES row remap (`0xFF` = unchanged; 161 rows) | 161 |

The buffer carries the 161 B remap alone. The paste source table is the static
`ccSvc_snapTable[128]` and the retargeted source blocks live in the background
region's pool (S077); neither uses the borrowed buffer, so jobs copy no block
data through it.

---

## 12. Pattern engines

### 12.1 The exclusive boundary (`PatternStackService.c`)

`patSvc_beginExclusive(scene)` / `patSvc_endExclusive(scene)` give one holder
sole write access to one Scene's Pattern region, **for any resident Scene**
(the active Scene may differ from the playback Scene). Begin records the
claim, closes the service's admission and returns 1 once its FIFO, bulk
barrier and reactive recovery have drained (0 while a filesystem
replacement or another claim is pending); it clears the reservation image.
While the claim exists the service's handover neither reopens admission nor
adopts the claimed Scene. End recounts occupancy, restarts the repair epoch
and reopens admission. One holder at a time. `ccSvc_claim()` takes it at job
start; a job whose Scene is still being handed over (just after a PERF
switch) waits. Any Scene and track may be **read** at any time.

### 12.2 The swap block

The top 33 chunks (132 B, bytes 8,060..8,191, `PAT_POOL_SWAP_OFFSET`,
`PAT_POOL_SWAP_CHUNKS`) of every Scene's pool are never allocated by
ordinary paths (`pat_poolAlloc()`, service searches, repair, reservations
and the in-place append `pat_tryAppendAutomation()` all stop at
`PAT_POOL_ALLOC_CHUNKS` = 2,015 chunks). It holds one maximum block (132 B)
and is the **guaranteed rewrite area**: a step can be republished without
free pool space while its old block is still live. Copy/clear is its first
user; it stays available to any later feature that needs the same
guarantee. PAT4 is unchanged (the reserve is free space). Patterns saved
before S075 may have blocks there: `patSvc_exclusiveEvacuateSwapStep()`
moves them down before first use (or clears orphan swap bits); if a full
pool makes that impossible, the job is dropped (`EVACUATE_FAILED`). The
pool-use widget reports against 2,015 chunks.

### 12.3 The raw block API (`PatternData.h`; legal only inside the boundary)

`pat_rawReadBlock` (0 for no block), `pat_rawBlockBytes`, `pat_rawDecode`,
`pat_rawEncode`, `pat_rawPlace`, `pat_rawPlaceViaSwap`, `pat_rawSwapReturn`,
`pat_rawPublishEmpty`, `pat_rawFreeChunks`, `pat_rawSwapFree`, and the
whole-region set `pat_rawRegionSilence`, `pat_rawRegionCopyBody`,
`pat_rawRegionPublishSteps`, `pat_rawRegionCopiedBlock`,
`pat_rawRegionPublishRewritten`, `pat_rawRegionReset`. Every step write keeps
the publication order (`PATTERN_DYNAMIC_STACK.md` §12.10): new bytes into
unreferenced space, **one PRIMASK store of the complete address entry**
(trigger policy KEEP re-reads the live trigger bit), then free the old run.
A rewrite that does not grow goes through the swap block: place there,
publish, free the old run, then `pat_rawSwapReturn()` moves it into the
freed run and publishes again (swap block empty again).

### 12.4 Sliding compaction

`patSvc_exclusiveCompactStep(scene)` moves the block just above the lowest
free chunk down into it, through the empty swap block when the two runs
overlap. Repeated calls leave all free space as one run below the swap block,
so a growing step whose new size fits the free chunks always finds its run.
One slide per call; the paste engine calls it while a growing step does not
fit. (Copy/clear's own moves are not traced per move in PatternTrace;
`JOB_STATS` counts them.)

### 12.5 Paste engine (`ccSvc_runPatternPaste()`; step, range, bar, bar-to-step, `copy track`)

Run state: `run.phase` bit 0 = snapshot path; `run.sub` = phase below;
`cursor` = step; `aux` = snapshot cursor / free chunks / swap-return bit.

| `sub` | Phase | What happens |
|---|---|---|
| 0 | path, buffer, claim, evacuate | Geometry from the job and source (`ccSvc_pasteGeometry()`; count 0 → `BAD_GEOMETRY`). **Overlap test** (`ccSvc_pasteOverlaps()`: same Scene and track and the destination step set intersects the source set, two 128-bit stack masks) → snapshot path, which waits while the Pattern drain is reading the background region snapshot (`filesystem_patternSnapshotInUse()`; WAIT until clear). Then claim the destination Scene and evacuate the swap block if needed. |
| 1 | snapshot (overlap only) | Copy source address entries into the static `ccSvc_snapTable[128]` and the retargeted source blocks into the background region's pool (`pat_backgroundPoolMut()`), 16 per tick. Trigger bits come from the slot's early source mask, not the live entry. |
| 2 | check | Walk the destination steps in paste order with the would-be block of each (`ccSvc_pasteBuild()` → `ccCopy_buildStep()`); for **every growing step** the free chunks before it (outside the swap block, counting reclaimable reservations) must be ≥ its new size, because old and new blocks coexist until publication. Fail → `CHECK_FAIL`, `NO_ROOM` drop; nothing has changed. Steps that do not grow always fit through the swap block. |
| 3 | place | Up to 8 steps per tick: a growing step goes into a free run (sliding compaction first if needed) and is published; a non-growing step goes through the swap block (place, publish, free, return). Trigger bits as the selection requires. |
| 4 | finish | Length extension or track settings (`copy track`), dirty mark (PatternData helper → Pattern AutoSave), `ccSvc_patternChangedUi()` (presence-search restart via `menu_patternContentChanged()`, step LEDs, STEP page), early flags cleared. |

The **live path** (no overlap) reads each source block at the moment it is
needed (`ccSvc_sourceBlock()`: `pat_rawReadBlock()` + retarget into a stack
buffer) in both check and place; the source cannot change underneath because
only this job writes Patterns and the source is not in the destination set.
Stack peak on the live path ≈ 1.2 KB (two 132 B blocks, a 252 B decode list,
`ccCopy_buildStep()` 504 B, caller 132 B), foreground only.

### 12.6 Early trigger bits and drop restore (pastes)

- At the press, `ccCopy_requestPaste()` → `ccSvc_pasteTriggersNow(slot)` for
  `… -> repl` / `… -> merge` step/bar/track pastes: reads the source trigger
  of every pasted step (live) into `ccSvc_earlySrc[slot]` (range order),
  the previous destination triggers into `ccSvc_earlyPrev[slot]`, writes
  each destination trigger with `pat_setStepActive()` (repl := source;
  merge := OR), and repaints the step LEDs (`ccSvc_triggersChangedUi()`).
  `auto -> …` pastes write nothing early.
- The source mask is captured for **every** early-trigger paste: a later
  paste's early write can change the source of an earlier queued paste
  (A: X→Y queued; B: Z→X written at once); A must copy X as it was at A's
  press. The job takes trigger bits from the mask.
- On `CC_RUN_DROP` (`NO_ROOM`, `EVACUATE_FAILED`),
  `ccSvc_restoreEarlyTriggers()` restores each destination step whose live
  trigger **still equals the value written at the press** from the restore
  mask; a step the user toggled since keeps the user's state. The paste is
  then undone whole.
- Clears write trigger-off early too (§7.3) but keep no restore storage.

### 12.7 Clear engine (`ccSvc_runPatternClear()`)

Step, bar and track clears and the PERF `automation`/`notes` clears, 8 steps
per tick per object range (`ccSvc_clearRange()`), each step through
`ccSvc_clearStep()`:

- `… all`: trigger off and the block released (`pat_rawPublishEmpty()`;
  no swap block needed);
- `… auto`: automation removed, trigger and specials kept (block shrinks via
  the swap block, or empties);
- `… notes`: trigger off, note/velocity removed, probability kept when set,
  automation kept (`pat_rawEncode(block, flags & PAT_SPECIAL_PROB_BIT, 0, 0,
  probability, autos, count)`);
- `clear track` also resets length 16, default scale, shuffle 0.

### 12.8 Whole-Pattern copy and reset

`ccSvc_runRegionCopy(src, dst)` (`copy pattern`, `copy scene`) under the
boundary on the destination: (1) publish every destination entry empty,
trigger off; (2) copy pool, bitmap, track settings and Pattern globals;
(3) if types differ, retarget entries in the not-yet-referenced pool (32 per
tick), shrinking blocks that lost entries and freeing their tail chunks
(blocks only shrink; growth is an anomaly); (4) publish the 896 address
entries last; (5) release, mark the Pattern dirty, restart the presence
search if the destination is viewed. TIM3 never reads a pointer into
half-copied data. `ccSvc_runRegionReset(scene)` empties one region
(`clear pattern`, `clear scene`).

### 12.9 Register pass (`ccSvc_runRegister()`)

One target per pass for the register's Scene: scans 896 steps and rewrites
every step holding the target without it, through the swap block; then
`menu_patternContentChanged()`. A pass can only drop if the swap block cannot
be emptied (pre-S075 data in a full pool).

---

## 13. Scene-level executors

Every executor that touches the active Scene waits (`CC_RUN_WAIT`) while
`preset_applyWorkersIdle()` is zero, and after starting an apply keeps
waiting until it is done. Data commits come first; names are recorded in a
final phase that waits for `ccSvc_namesReady()` and never delays the data.

| Paste / clear | Function | Commit (per fan-out destination where it fans out) | Then |
|---|---|---|---|
| `copy instrument` | `ccCopy_runInstrument()` | Advanced check for every member; `preset_startInstrumentCopy(src_scene, src_slot, dst_mask, dst_slot)` (the Instrument Load commit path, `source_slot` moves `self` LFO selectors); slot-6 decay pair when slot 6 → slot 6 | whole-Instrument marker and Bank-present inside the helper; `bank_revalidateVoiceEditMasks()`; wait `preset_tickInstrumentApply()`; repaint; Instrument row names |
| `copy kit` | `ccCopy_runKit()` | `scene_commitKit()` per member (not the source); Bank-present (+ `autosave_markSceneWithPatternDirty()` if it was not present) | `preset_startDrumsetApply()` if the active Scene was written; revalidate; Kit + six Instrument names |
| `copy effect` | `ccCopy_runEffect()` | `effects_pasteRecord(dst, src)` (may change type; the mask is taken on the destination's current type; a member equal to the source is skipped) | activation if active; revalidate; Effect name |
| `copy scene settings` | `ccCopy_runSceneSettings()` | `scene_commitSettings(dst, &source->settings)` (each field through its change-aware setter) | mask exchange; apply if active |
| `copy scene` | `ccCopy_runScene()` | phase 0: settings, mask exchange, `scene_commitEffectRecord()`, `scene_commitKit()`, present, `autosave_markSceneWithPatternDirty()` + `markEffectDirty()`; phase 1: `ccSvc_runRegionCopy()` | `preset_startDrumsetApply()` if active; revalidate; Scene, Kit, Instrument, Pattern and Effect names |
| `copy pattern` | `ccCopy_runPatternOnly()` | `ccSvc_runRegionCopy()` | Pattern name |
| FX step paste | `ccCopy_runFxSteps()` | `effects_pasteSeqStep()` per step (type mismatch → drop) | sequence serial bump; held Morph latch dropped if the active Morph lane changed |
| `clear scene` | `ccClear_runScene()` | settings defaults; Effect defaults (whole commit); active: `effects_activateScene()`; other: `scene_resetKitToDefaults()` and present bit off; mask reset to self; revalidate; then `ccSvc_runRegionReset()` | apply if active; rows lose `R` |
| `clear scene settings` | `ccClear_runSceneSettings()` | `scene_settingsDefaults()` + `scene_commitSettings()`; mask reset to self | apply if active |
| `clear fx` | `ccClear_runFx()` | `effects_resetRecord(scene)` (fans out) | repaint if the active Scene was written |
| `clear fx sequence` | `ccClear_runFxSequence()` | `effects_clearSeqLanes(scene, 0xFFFF, 0xFFFF)` (fans out) | refresh Effect UI |
| EFFECTS SEQ clear | `ccClear_fxStepNow()` | `effects_clearSeqLanes(scene, bit(step), 0xFFFF)` at the press | — |
| `clear send` | `ccClear_runSend()` | `preset_setVoiceFxSendAmount(…, 0)`, `preset_setVoiceFxSendMorph(…, 0)`, `preset_setVoiceFaderSetting(…, pre)` per member | repaint |
| `clear reset morph` (track) | `ccClear_runResetMorphTrack()` | `preset_resetSlotMorphToNormal(scene, slot)` per member (fans out through `bank_sceneFanoutMask()`): Morphable Normal → Morph, FX send morph, slot-6 decay morph; then `scene_setTrackMorphLength/Scale/Shuffle(m, track, member's own region Normal)` (S078) | repaint |
| `clear reset morph` (Scene) | `ccClear_runResetSceneMorph()` | loops 6 slots `preset_resetSlotMorphToNormal()` + `effects_resetMorphToNormalSingle()` per member (fans out through `bank_sceneFanoutMask()`): each member's own Morph := own Normal, FX send morph, slot-6 decay morph, Effect morph | repaint |
| `clear reset fx morph` | `ccClear_runResetFxMorph()` | `effects_resetMorphToNormal(scene)` (fans out internally) | repaint |
| `reload scene` | `ccClear_runReloadScene()` | `preset_loadSceneForScenes(source, 1u << scene)` if source ≤ 999 (fire-and-forget) | the Preset lifecycle handles apply and repaint |
| `copy inst -> morph` | `ccCopy_runMorphTrack()` | `preset_copySlotNormalToMorph(scene, slot)` per member (fans out); then (S078) the source track's Pattern Normal length/scale/shuffle (source Scene region) → each member's Morph endpoints for the destination track | repaint |
| `copy scene -> morph` | `ccCopy_runSceneMorph()` | `preset_copySlotNormalToMorph(src, slot, m, slot)` per member (fans out through `bank_sceneFanoutMask()`): source Normal → member Morph, FX send, slot-6 decay, Effect (same type only) | repaint |

Defaults used by `clear scene`/`settings` (`scene_settingsDefaults()`): MIDI
channel = track + 1, note `MIDI_DEFAULT_TRIGGER_NOTE` (63), audio out St1
(route 0) for every voice, FX send Normal and Morph 0, fader `pre`, Morph
amounts 0, Effect Morph 0, bus compressor off/0/0/off. A fresh Kit is
DRM, DRM, DRM, SNR, CYM, HAT (`scene_initialInstrumentTypes[]`).

---

## 14. Names (HCNAMES)

- Pastes copy the **name and source token** of each copied object's row and
  clear the refreshed flag `R` (so the boot reader never reloads the library
  object over pasted content; the copied token keeps the origin accurate).
  Rows: `copy scene` → Scene, Kit, six Instruments, Pattern, Effect;
  `copy kit` → Kit and six Instruments; `copy instrument` → one Instrument
  (its type field follows); `copy effect` → Effect; `copy pattern` → Pattern;
  repeated for every fan-out destination.
- Clears keep names and tokens; rows whose content changed lose `R`
  (`ccSvc_nameContentChanged()`: clears the resident refreshed flag and arms
  the final write; it never borrows the buffer).
- Each copy records `remap[dst] = remap[src]` if set, else `src`
  (`ccSvc_nameCopy()`), so chained pastes resolve to the original row. It
  returns 0 if the buffer is not borrowed; executors call it only after
  `ccSvc_namesReady()`.
- After the last queued item, one filesystem op (`HNcU`,
  `FS_INTERNAL_OP_UPDATE_HCNAMES_COPY`, `filesystem_requestCopyResidentNames()`)
  reads `.hcnames`, keeps the original rows in the buffer, overlays the
  remapped names and sources (`filesystem_cacheCopyClearRemap()`), writes
  `.hcnamtmp` and swaps it in (the existing safe-rewrite phases), then marks
  the changed source bytes dirty for AutoSave. On a card error the old
  `.hcnames` stays, the error is traced and the suspension ends. If the
  facade is never free for 2 s, the write is given up (traced).
- The names are written before AutoSave captures the pasted content; a power
  loss in that window shows the new names over the previous AutoSave content
  until the next save (accepted).

---

## 15. Trace stage `c` (DEV builds)

Copy/clear drops work silently by design and does most of it after the
button is released, so the trace is the only way to see what happened.
Records go to the AutoSave trace ring (`/asavetrc.bin`, `DEV_MODES.md`) as
stage `c` (`AUTOSAVE_TRACE_STAGE_COPY_CLEAR`); `flags` is the event,
`value32` the layout below, `tick16` the time. Producers: the four CopyClear
files and `filesystem.c`. Constants: `AutosaveTrace.h`; decoder:
`tools/decode_devlogs.py` (`cc_record_text()`). Every hook goes through
`ccTrace()` (`copyClearService.h`), empty without `DEV_MODE_LOGGING`.
Trace flushes are suspended during an operation, so records reach the card
afterwards. Copy/clear's own Pattern moves are **not** written to
PatternTrace (user D4); reactive and repair relocations keep their `R`/`L`
records.

### 15.1 Shared packings

**Job descriptor `JD`** (32 bits; `ccTrace_job()`): bits 0..7 `op` (class
`0x10` paste / `0x20` clear, low nibble selection), 8..10 kind (1 step, 2
bar, 3 track, 4 Scene, 5 FX step), 11..14 Scene, 15..17 track (7 = none),
18..24 start, 25..31 end.

**Source `SRC`:** bits 0..2 kind, 3..6 Scene, 7..9 track, 10..16 start,
17..23 end, 24..26 mode (`SELECT_MODE_*`), 27..31 zero.

### 15.2 Events

| Code | Name | `value32` | Producer |
|---:|---|---|---|
| 0x01 | `OP_START` | 0..1 phase (1 armed copy, 3 clear); 2..4 mode; 8..11 operation sequence; 16..23 queued jobs; 24..31 register entries | `copyClear_copyPressed()` |
| 0x02 | `OP_REFUSED` | 0 recording; 1 erasing; 2 storage busy; 3 Instrument transaction; 4 mode not allowed; 5 previous jobs queued; 8..10 mode; 16..23 queued jobs | `copyClear_copyPressed()` |
| 0x03 | `SOURCE_SET` | `SRC` (final source only, never provisional ranges) | `cc_commitSource()` |
| 0x04 | `OP_RELEASE` | 0 operation started; 1 menu was visible; 8..15 queued jobs; 16..23 register entries | `copyClear_copyReleased()` |
| 0x05 | `OP_FINISH` | 0..7 jobs done; 8..15 jobs dropped; 16..23 register passes; 24..27 operation sequence; 28..29 names result (0 none, 1 written, 2 error, 3 gave up) | `ccSvc_tick()` teardown |
| 0x10 | `JOB_START` | `JD` | `ccSvc_tick()` |
| 0x11 | `JOB_STATS` | 0..7 compaction slides; 8..15 swap-path rewrites; 16..23 retarget-dropped entries (each sat 255); 24..27 evacuation moves; 28..31 claim-wait ticks (sat 15); only when nonzero | `ccSvc_tick()` |
| 0x12 | `JOB_END` | 0..1 result (0 DONE, 2 DROP); 2..7 drop reason (§15.3); 8..15 reason detail; 16..31 ticks run (sat 65,535) | `ccSvc_tick()` |
| 0x13 | `JOB_STALL` | `JD`; once per job after 5,000 ticks (10 s) waiting; followed by `ANOMALY` 9 with the run phase | `ccSvc_tick()` |
| 0x14 | `CHECK_FAIL` | 0..10 free chunks before the step; 11..17 step index; 18..23 new chunks; 24..29 old chunks | paste check phase |
| 0x15 | `QUEUE_FULL` | `JD` of the dropped job | `ccSvc_enqueue()` |
| 0x16 | `PASTE_NOOP` | `JD` of a paste identical to its source | `ccCopy_requestPaste()` |
| 0x17 | `JOB_TRICKLE` | 0..15 ticks spent in trickle mode; 16..31 engine calls run in trickle mode (each sat 65,535); at job/register end when nonzero | `ccSvc_traceTrickle()` |
| 0x18 | `ANOMALY` | 0..7 code (§15.4); 8..11 track; 12..15 Scene; 16..23 step; 24..31 extra | engines |
| 0x19 | `EARLY_RESTORED` | 0..7 steps restored; 8..15 steps left alone (changed by the user since); 16..17 queue slot; right before a dropped paste's `JOB_END` | `ccSvc_restoreEarlyTriggers()` |
| 0x20 | `REG_ADD` | 0..15 target; 16..19 Scene; 20..23 entries after the add; 24..27 FX lane (15 none) | `ccClear_potTurned()` |
| 0x21 | `REG_REFUSED` | 0..15 target; 16..19 Scene; 20..23 reason (1 full, 2 other Scene's entries, 3 already pending, 4 no target) | `ccClear_potTurned()` |
| 0x22 | `REG_DONE` | 0..15 target; 16..25 steps rewritten (0..896); 26..30 swap-path rewrites (sat 31); 31 dropped (swap block could not be emptied) | `ccSvc_runRegister()` |
| 0x23 | `FX_CLEAR` | 0..15 fan-out Scene mask; 16..19 Scene; 20..23 step (15 all); 24..27 lane (15 all); 28 changed | `ccClear_fxStepNow()`, pot clears, `ccClear_runFxSequence()` |
| 0x24 | `EARLY_TRIG` | 0..7 steps written (sat 255); 8 paste (0 clear); 9 source mask used; 16..18 kind; 19..22 Scene; 23..25 track; 26..27 queue slot (pastes) | `ccSvc_pasteTriggersNow()`, `ccClear_triggersOffNow()` |
| 0x30 | `FANOUT` | 0..15 Scene mask written; 16..19 destination Scene; 20..23 kind (1 instrument, 2 kit, 3 effect, 4 send, 5 clear fx, 6 FX step paste); 24 active Scene touched; 25..28 slot (instrument/send) | Scene-level executors |
| 0x31 | `MASK_SET` | 0..15 resulting entry; 16..19 Scene; 20 0 exchange / 1 reset; 21..24 source Scene (exchange) | `copy scene`, `copy scene settings`, `clear scene`, `clear scene settings` |
| 0x40 | `SCRATCH` | 0 0 borrow / 1 return; 8..23 ticks waited for the borrow (S077: always 0; the snapshot wait moved to 0x51) | `ccSvc_ensureScratch()`, teardown |
| 0x41 | `FS_REFUSED` | 0..7 refused `fs_internal_op_t`; 8..15 `current_op`; first refusal per loan only | `filesystem_start()` |
| 0x42 | `NAMES` | 0..1 event (0 requested, 1 written, 2 error, 3 gave up); 8..15 rows copied; 16..23 reserved 0; 24..31 refused requests before acceptance (sat 255) | `ccSvc_tick()`, `ccSvc_namesWritten()` |
| 0x50 | `SUSPEND` | 0 0 begin / 1 end; 1 facade busy at the edge; 8..15 `current_op` | `filesystem_tick()` |
| 0x51 | `SNAPSHOT_GATE` | 0..15 ticks an overlapping paste waited for the Pattern drain to stop reading the background snapshot (sat 65,535); emitted once when the gate clears | `ccSvc_runPatternPaste()` |

### 15.3 Drop reasons (`JOB_END` bits 2..7)

| Code | Reason | Detail (bits 8..15) |
|---:|---|---|
| 0 | none (DONE) | run phase at end |
| 1 | `NO_ROOM` (paste check failed; a `CHECK_FAIL` precedes it) | step index |
| 2 | `EVACUATE_FAILED` (pre-S075 block in the swap block cannot move) | Scene |
| 3 | `ADVANCED_LIMIT` (`copy instrument`) | refusing member Scene (low nibble), slot (high nibble) |
| 4 | `FX_TYPE_MISMATCH` (FX step paste) | source type (low), destination type (high) |
| 5 | `NO_SOURCE` | — |
| 6 | `NO_SCRATCH` (retired in S077; no producer remains) | — |
| 7 | `BAD_SELECTION` (dispatch default) | selection |
| 8 | `BAD_GEOMETRY` (paste count 0) | kind |

### 15.4 Anomaly codes (`ANOMALY` bits 0..7); each one is a bug report

| Code | Name | Site | Extra |
|---:|---|---|---|
| 1 | `GROW_UNPLACEABLE` | place phase: growing step, `pat_rawPlace()` failed and compaction moved nothing | new chunks |
| 2 | `SWAP_RETURN_ABANDONED` | swap return failed and compaction moved nothing; the step is left in the swap block | 0 |
| 3 | `SWAP_OCCUPIED` | `pat_rawPlaceViaSwap()` failed after evacuation | new bytes / 4 |
| 4 | `REGION_REWRITE_GREW` | region copy: a retargeted block would grow | 0 |
| 5 | `CLAIM_OTHER_SCENE` | another Scene's claim still held | held Scene |
| 6 | `TEARDOWN_CLAIM_HELD` | teardown found a claim (recorded only, user D3) | held Scene |
| 9 | `STALL_PHASE` | follows `JOB_STALL`; extra = run phase (high nibble), sub (low nibble) | phase/sub |

(Codes 7 and 8 were planned and are unused.)

### 15.5 Volume

About 10 records per operation frame (`OP_START`, `SOURCE_SET`, `SUSPEND`,
`SCRATCH`, `OP_RELEASE`, `NAMES` ×2, `SCRATCH`, `OP_FINISH`, `SUSPEND`), 3–5
per job (`EARLY_TRIG`, `JOB_START`, optional `JOB_STATS`/`JOB_TRICKLE`,
`JOB_END`), 2–3 per pot clear. A heavy operation (30 pastes, 8 pot clears) is
≈ 180 records. The ring is the temporary 2,048 records
(`AUTOSAVE_TRACE_RECORD_COUNT`, user D2); with the 64-record default a heavy
operation overflows and the `G` record reports the drop count.

### 15.6 Expected traces for common cases

| Case | Records |
|---|---|
| COPY while recording | `OP_REFUSED` recording |
| normal step paste | `OP_START`, `SUSPEND` begin, `SOURCE_SET`, `EARLY_TRIG`, `JOB_START`, `JOB_END` DONE, `OP_RELEASE`, `OP_FINISH`, `SUSPEND` end (`SCRATCH` only if it overlapped or names were written) |
| paste during a running AutoSave drain | `SUSPEND` begin with facade busy, `JOB_TRICKLE` nonzero |
| fifth quick paste | `QUEUE_FULL` |
| merge into a nearly full pool | `CHECK_FAIL`, `EARLY_RESTORED`, `JOB_END` DROP NO_ROOM — or `JOB_STATS` with slides |
| `copy instrument` beyond the Advanced limit | `JOB_END` DROP ADVANCED_LIMIT |
| FX paste onto another type | `JOB_END` DROP FX_TYPE_MISMATCH |
| `copy kit` with a 3-Scene mask | `FANOUT` kit with that mask |
| `copy scene` | `MASK_SET` exchange |
| nine pot turns | eight `REG_ADD`, one `REG_REFUSED` full, eight `REG_DONE` |
| LOAD right after a long register drain | `FS_REFUSED` before `SCRATCH` return |
| card removed before the name write | `NAMES` error or gave up; `OP_FINISH` names 2/3 |

---

## 16. Verification checklist (hardware)

Combined from the S075 schedules; items not yet reported by the user at the
S075 close are marked ◻.

| Area | Check |
|---|---|
| Buttons ◻ | Every mode × SEQ/SELECT/TRACK/BAR/MODE/SHIFT × (no operation, copy armed, each source kind, clear): no step toggle, hold, Scene switch, mute or audition leaks; navigation works where §11.3 allows |
| LEDs | Copy: steady, then blinking from the first source press; clear: SHIFT and COPY blink until release; no source LED; every visible destination LED flashes once; `copy track`/`clear track` flash only the TRACK LED |
| Menus | `Copy`/`Clear` header; indicator from the 9th column for every kind; labels `step|bar -> repl/merge`, `auto -> repl/merge`; clear selection kept within a button group, `cancel` on a new group or release |
| Ranges | Bars 1, 2 and 8, both directions; the §5.2 sequence; `s017-032` in bar 2; no stale pairing after release; wrap at 128/16/8 |
| Pastes ◻ | Every step/bar selection on a playing Pattern without glitches; overlap with the source; cross-track and cross-Scene retargeting (same type, by key, by page position, dtype mismatch dropped); `copy track` with settings; nearly full pool: completed or dropped whole, early triggers restored except user-toggled steps |
| Probability | `auto -> repl` copies the source probability (set/changed/removed); `auto -> merge` leaves it; `… notes` keeps it |
| Clears | Every selection; `clear track` resets settings; steps go dark at once; PERF clears on the active and another Scene (other: emptied, dark); `clear send` zeroes both send endpoints and sets `pre` |
| Pot clears | VOICE, STEP track settings (`len`/`scl`/`shf`; held steps: held-only), PERF (`1vm..6vm`, `fxm`), Effect page; no menu; underline off at the turn; values never change; 8-entry register; FX-lane part fans out; starts at once during a running drain |
| Scene level ◻ | Instrument (fan-out, Advanced limit, track 7, decay pair, LFO `self`), Kit, Effect (type change, fan-out), settings (mask exchange), Scene, Pattern (retarget), present rule |
| Names ◻ | HCNAMES rows after each kind, fanned-out and chained pastes; `R` cleared; card removed during the name write ends the operation |
| Suspension ◻ | AutoSave/trace/settings writers do not start during an operation; a running writer finishes; the scalar drain runs ~250 ms after the end; repair resumes |
| Regression ◻ | Load/Save; the Pattern Stack Service under playback and Scene switching; VOICE and Effect underlines; AutoSave power cycle after each kind |

---

## 17. Resources

| Item | Bytes | Region / note |
|---|---:|---|
| Session state | 25 | SRAM1 `.bss` (`cc_state`, `cc_source`, row stack, edge masks) |
| Service queue, register, run state, flags, claim, retry, buffer pointer | ≈ 59 | SRAM1 |
| Early-trigger masks + flags | 129 | SRAM1 (F1, approved O1/A3) |
| Trickle credit | 2 | SRAM1 |
| `service_exclusive_scene` (PatternStackService) | 1 | SRAM1 |
| `fs_name_cache_borrowed` (filesystem) | 1 | SRAM1 |
| DEV trace state + filesystem latches | 24 | SRAM1, `DEV_MODE_LOGGING` only |
| Swap block | 132 per Scene | Pattern pool data, not RAM |
| Paste source table (`ccSvc_snapTable[128]`) | 256 | SRAM1 `.bss` (S077, approved) |
| Background region pool (paste snapshot target) | ≤ 8,060 of 8,192 | Pattern pool data, not new |
| Name buffer | 161 of 9,000 | borrowed, not new (S077: remap only) |
| Stack peaks | ≈ 1.2 KB live paste path; 288 B FX snapshot; 504 B merge lists | foreground main loop |
| CPU | 8 steps / 16 snapshot reads / 32 region entries per 2 ms tick; ≤ 0.1 % while an older writer finishes | foreground only; no audio-path work |

Exact linked totals: `STORAGE_SRAM_MANIFEST.md` §5, §8.2.

---

## 18. Known limits and design rationale

**Limits (by design or accepted by the user):**

- A paste into a nearly full pool can be dropped where a net-growth check
  would have accepted it, by at most one block (≤ 132 B of 8,060 B): each
  growing step must fit before the old block is freed.
- A whole-Pattern copy with retargeting into the playing Scene silences the
  destination entries for up to 28 ticks (896 entries / 32 per tick).
- LOAD/SAVE entered right after a long register drain waits for the name
  buffer.
- A power loss between the name write and the next AutoSave shows new names
  over old content.
- Settings or an Effect pasted into a non-present Scene are AutoSaved only
  once a Kit makes it present.
- A clear dropped after its early write leaves triggers off.
- At 0.1 % CPU a large clear can take a second or two while an older drain
  finishes (the immediate flash and trigger write are the acceptance
  signal).
- There is no undo (RAM policy) and no Instrument clear.

**Why it is built this way:**

- **No data clipboard:** free SRAM1 is reserved for Pattern data; the source
  is read when each paste runs (all 16 Patterns are resident), and only an
  overlapping paste takes a snapshot, into the background region's pool
  (`pat_backgroundPoolMut()`) rather than the 9 kB name buffer (S077).
- **Step-by-step placement with a swap block** instead of erase-then-write:
  a step is never missing and TIM3 never reads half a block, even in a full
  pool.
- **Early trigger bits:** the user must see a paste or clear accepted at
  once, even when the job waits for a writer or runs at the trickle rate.
- **Suspension of background work:** a paste must not race AutoSave
  capture, repair relocations or a Load/Save index into the borrowed
  buffer.
- **Fan-out only for Scene children:** Scenes in one edit mask are meant to
  sound alike; Patterns are per-Scene content.

---

## 19. How to modify

**Add a copy selection to an existing menu:** extend the label table and the
`cc_copy_*_sel_t` enum in `copyOps.c/.h` (`ccCopy_selectionCount()` follows
the table); handle it in `ccCopy_runJob()` (and `ccCopy_buildStep()` for a
step-family rule); decide early triggers in `ccSvc_pasteTriggersNow()`; add
trace detail if a new drop reason exists.

**Add a clear selection:** extend `ccClear_*Labels[]` and
`cc_clear_obj_sel_t`/`cc_clear_scene_sel_t`; route it in
`ccClear_runJob()`; if it ends with triggers off, add it to
`ccClear_triggersOffNow()`; Pattern-level work belongs in
`ccSvc_clearStep()`/`ccSvc_runPatternClear()`.

**Add a source kind:** extend `cc_kind_t`, the routing tables in
`copyClearSession.c` (`copyClear_buttonPressed/Released()`), the indicator
in `cc_formatIndicator()`, `ccCopy_menuForSource()`/`ccClear_menuForObject()`,
the flash masks in `cc_flashObject()`, and `ccSvc_pasteGeometry()` if it is
Pattern data. Keep edge pairing for every consumed button.

**Add a Scene-level executor:** commit only through SceneData, Preset or the
EffectsManager edit API; wait on `preset_applyWorkersIdle()` before touching
the active Scene; fan out with `bank_sceneFanoutMask()`; revalidate masks
after type changes; record names in a final phase behind
`ccSvc_namesReady()`; add `FANOUT`/`MASK_SET` trace records.

**Touch the Pattern pool:** only inside `patSvc_beginExclusive()`, only with
the raw block API, always publish before free, never allocate in the swap
block, and keep per-tick work bounded.

**Add a trace event:** a new `AUTOSAVE_TRACE_CC_EVT_*` code in
`AutosaveTrace.h`, the producer through `ccTrace()`, the decoder branch in
`tools/decode_devlogs.py` (`CC_EVENTS`, `cc_record_text()`), and the table in
§15 here and in `DEV_MODES.md`.

**Pitfalls:**

- Never call the raw block API, or write a Pattern of another Scene, outside
  the exclusive claim.
- The press stack holds 4-bit row indices; store raw indices, convert late.
- A consumed press without its release consumed leaks the release into step
  toggles or mutes.
- `cc_commitSource()` must not reset a provisional menu's selection.
- Anything that loads into the name cache must respect the loan
  (`fs_name_cache_borrowed`).
- New RAM needs the user's approval (byte count, region, lifetime, owner).

---

## 20. History

- **S024:** an earlier copy/clear audit fixed the old clear-mode encoder
  path; the copy gestures stayed no-ops.
- **S075 base pass (2026-10-01):** this module, the swap block, the raw block
  API, the exclusive boundary, sliding compaction, Scene-level executors,
  the `HNcU` write, the suspension, the name-buffer loan; `copyClearTools`
  and the Pattern copy no-ops removed; global `srt` retired (PERF `fxm`).
- **S075 F1 (2026-10-02):** source blink removed, LED rules, menu on the
  first row press, raw-index press stack (bars 2..8 fixed), kept selection
  per button group, column-9 indicator, labels, `notes` keeps probability,
  `auto -> repl` carries probability, lazy buffer, live paste path, early
  trigger bits with restore, trickle governor; trace stage `c`.
- **S075 F2 (2026-10-03):** `Copy`/`Clear` header; `clear send` clears both
  FX-send endpoints; Scene defaults (St1, compressor off/0/0/off) used by
  `clear scene`/`settings`.
- **S076 P3 (2026-10-06):** `reset morph` track clear (VOICE and EFFECTS
  TRACK menus), `reset morph` and `reset fx morph` Scene clears, `morph`
  track and Scene copy selections; `CC_CLEAR_RESET_MORPH` (4) inserted before
  `CC_CLEAR_SEND` (now 5); `preset_resetSlotMorphToNormal()`,
  `preset_copySlotNormalToMorph()`, `effects_resetMorphToNormal()`,
  `effects_resetMorphToNormalSingle()`.
- **S076 P4 (2026-10-06):** `reload scene` (10) added to the PERF clear-scene
  menu; fire-and-forget Scene reload from the HCNAMES source register through
  `preset_loadSceneForScenes()`.
- **S077 P4 (2026-10-08):** scene morph fan-out correction. `copy scene ->
  morph` (was `scene morph`) and `clear scene reset morph` now fan out through
  the edit mask (parallels `copy kit` / `clear fx`). Track morph copy label
  renamed `inst -> morph`. Comments and §10 fan-out table updated.
- **S078 (2026-10-08/10):** track run mode carried by `copy track` and reset
  by `clear track` (`copyClearService.c`); track Morph endpoints included in
  `copy inst -> morph`, `copy scene -> morph` and both `reset morph` clears;
  STEP track-settings pot clears (§8: `menu_knobClearTarget()` SEQ arm for
  the whole Scene; held steps only from the STEP held-step overlay through
  `copyClear_isClearMode()` and `sa_clearAutomationFromKnob()`).
- **S077 P5 (2026-10-08):** bar-to-step cross-kind paste. A bar/bar-range
  copy source can be pasted to a step destination (SEQ press in VOICE/STEP
  mode). New kind `CC_KIND_BAR_TO_STEP`, new request function
  `ccCopy_requestBarToStep()`. Geometry, early triggers, dispatch and flash
  updated.

---

## Appendix A. Reading code comments: section and decision references

Code comments in `Core/Menu/CopyClear/`, `PatternData.c`,
`PatternStackService.c/.h`, `BankData.c/.h`, `filesystem.c`, `buttonHandler.c`
and `menu.h` cite **"spec §N"**: the sections of the S075 full specification,
which was folded into this document and deleted at the S075 close. Map:

| Comment cites | Topic | Read here |
|---|---|---|
| spec §1 | terms | §2 |
| spec §3, §3.1, §3.2, §3.5 | operation start/hold/end, copy op, clear op, refusals | §4.1–§4.4 |
| spec §4, §4.1 | copy behaviour, sources and copy menus | §5.1, §6.1 |
| spec §4.2 | ranges | §5.2–§5.3 |
| spec §4.3 | pastes and navigation | §6.2, §11.3 |
| spec §4.4 | what each selection does | §6.3–§6.7 |
| spec §4.5 | fan-out summary | §10 |
| spec §5 | clear behaviour | §7 |
| spec §6 | pot clears | §8 |
| spec §7, §7.1, §7.2 | button routing | §11.3 |
| spec §8.1, §8.2 | screens, LEDs | §9.1, §9.2 |
| spec §9 | architecture | §11–§14 |
| spec §9.2 | suspension | §11.6 |
| spec §9.3 | the 9 kB name buffer | §11.7 |
| spec §9.4 | one Scene written at a time (exclusive boundary) | §12.1 |
| spec §9.5 | pastes, step by step | §12.5–§12.6 |
| spec §9.6 | the swap block | §12.2 |
| spec §9.7 | retargeting | §6.6 |
| spec §9.8 | whole-Pattern copy | §12.8 |
| spec §9.9 | Scene-level pastes and clears | §13 |
| spec §9.10 | names (HCNAMES) | §14 |
| spec §9.11 | underlines | §8, §11.2 |
| spec §10 | `srt` retirement, PERF `fxm` | `075_SESSION_HANDOFF_LOG.md` §7 |

Decision tags in comments (all recorded in `075_SESSION_HANDOFF_LOG.md`):

| Tag | Meaning |
|---|---|
| C1–C14 | startup decisions (log §4.5) |
| user B10 | `copy effect` copies the whole record (it may change the destination's type) |
| user B11 | `clear scene` on the active Scene keeps its Kit; another Scene is emptied |
| user B14 (with F4) | a pot clear removes the Pattern automation of the active Scene (never fanned out) and clears the FX lane (fanned out) |
| user B18 | encoder clicks are ignored while COPY is held |
| F1–F5 (spec round 3) | `fxm` label, `srt` eliminated, Scene-child fan-out, Effect Morph pot clear, fan-out through the pressed Scene's own mask (log §5.3) |
| F1-A … F1-K | F1 follow-up changes (log §9.3) |
| Q1–Q7, A1–A5, B1–B2 | F1 decisions (log §9.4); B1 = restore skips user-toggled steps, B2 = `auto -> repl` carries probability |
| D1–D5 | trace decisions (log §8.2) |
| F2-A … F2-H, F2-Q1 … F2-Q9, R2-1 … R2-3 | F2 follow-up (log §10) |
| F3 Q1, Q2, P1 | automation priority (log §11.4) |
