# S075 — Phase 6 start: copy and clear operations (overview and decisions)

**Status:** startup overview for Session 075, written at the Session 074
close (2026-09-30). Nothing here is implemented. It surveys what exists,
proposes which components copy and clear should act on and how, and lists
the architectural decisions (C1–C14) to settle before any line-level
schedule.

**Goal (user, 2026-09-30):** Phase 6 begins with copy and clear operations
for step, bar, track, automation, Instrument, Scene and other Scene
components.

**Read first:** `MEMORY.md`; `knowledge_files/log_archive/074_SESSION_HANDOFF_LOG.md`
(End of session block, §13); `PATTERN_DYNAMIC_STACK.md` §2–§5 and §12
(Pattern storage and the Pattern Stack Service);
`BANK_PRESET_ARCHITECTURE.md` §3–§5 (what a Scene, Kit and Instrument hold);
`AUTOSAVE.md` "Dirty marking rules"; `FILESYSTEM_SPEC.md` (HCNAMES rows and
provenance); `dsp_instruments_effects/EFFECTS_BUS_REFERENCE.md` §5, §9
(the Effect record and fan-out).

---

## 1. State at the start of Session 075

| Item | Value |
|---|---|
| Branch / HEAD | `dev-ph5-effects` / `50610dd`; the S074 closeout docs and the `dsp_instruments_effects/` move are uncommitted |
| Link | `text=502,512`, `data=416`, `bss=426,392` |
| Flash | 502,928 / 753,664 B (250,736 B free) |
| RAM | ITCM 4,168 B; DTCM statics 4,480 B; FX arena 126,592 B (margin 3,712 B); SRAM1 statics 296,248 B, 80,584 B unlinked. Free SRAM1 is **reserved for Pattern data**, and free DTCM for audio buffers (`STORAGE_SRAM_MANIFEST.md` §10). |
| Config | `DEV_MODE_DIAGNOSTIC 0`, `DEV_MODE_LOGGING 1`, `DEV_STALL_DETECTION 1` |

Build with `make all && make img`; measure with
`python3 tools/link_budget.py arm-none-eabi-nm build/lxr02.elf`; record the
`.img` SHA-256 (the `.bin` is unstamped).

---

## 2. What exists today

### 2.1 Front-panel gestures (`buttonHandler.c`, `copyClearTools.c`)

| Gesture | Mode set | What happens now |
|---|---|---|
| COPY (press) | `MODE_COPY_TRACK`; COPY LED blinks; SELECT and VOICE LEDs cleared | Arms copy |
| COPY held, VOICE *a*, then VOICE *b* | `MODE_COPY_TRACK` | `copyClear_copyTrack()` → `pat_copyTrack(viewed Pattern, a, b)`: **a no-op**. Only the LEDs are cleared and repainted. |
| COPY held, SELECT *a*, then SELECT *b* | `MODE_COPY_PATTERN` (reused for bars) | `copyClear_copyBar()` → `pat_copyBar(viewed Pattern, active track, a, b)`: **a no-op** |
| COPY release | — | `copyClear_reset()`: mode none, blink LEDs cleared, source/destination forgotten |
| SHIFT+COPY (not recording) | `MODE_CLEAR` | LCD `clear [track]?`; the encoder picks the target `track` / `pattern` / `autom.1` / `autom.2`; an encoder click or a second SHIFT+COPY executes; releasing SHIFT (COPY not held) cancels |
| SHIFT+COPY while recording and running | — | erase mode (`seq_setErasingMode`), which is unrelated to copy/clear |

- `copyClear_copyPattern()` exists but **has no caller**, so no gesture
  copies a Pattern.
- The source and destination are button indices masked in
  `copyClearTools.c` (`& 0xF` for tracks, `& 0x07` for bars).

### 2.2 What clear does

- **Track:** `patSvc_clearTrack(viewed Pattern, active voice)`. It is a
  bounded service barrier, 16 steps per tick, that detaches each address
  entry before freeing its pool block.
- **Pattern:** `patSvc_clearPattern(viewed Pattern)`, a whole-region reset.
- **`autom.1` / `autom.2`: this is a bug.** `copyClear_executeClear()` has
  `default:` and `case CLEAR_TRACK:` on the same branch, so choosing
  `autom.1` or `autom.2` **clears the whole track**. The two targets are
  legacy LXR automation lanes that the v4 Pattern model no longer has
  (`copyClear_clearTrackAutom()` is an empty stub). Fix this in Phase 6
  (decision C9).
- After a clear, `menu_voiceAutoOverlayPatternDeleted()` restarts the
  automation-presence search (the VOICE and Effect-page underlines).

### 2.3 Pattern storage facts that decide the design (`PATTERN_DYNAMIC_STACK.md`)

- **One Pattern per Scene.** "Pattern *n*" in the UI is the Pattern of Scene
  *n*. `pat_regions[16]` holds sixteen self-contained 10,519 B regions:
  - the address array, 7 tracks × 128 steps × `u16`;
  - an 8,192 B pool of 4-byte chunks with a 512 B bitmap;
  - per-track length, scale and shuffle;
  - `pattern_change_bar` and `pattern_next`.
- **Address entry:** bit 15 trigger, bit 14 specials block exists, bits 13..0
  a pool offset **relative to that Scene's pool**; `0x3FFF` is empty.
- **Pool block:** a 2-byte header (10-bit back-reference `track·128 + step`,
  6-bit automation count), a flags byte (note/velocity/probability), the
  present values, then up to 63 automation entries of 2 bytes (9-bit target,
  7-bit value). Blocks are 4-byte aligned. The largest is 132 B.
- **Consequences:**
  - **A whole-Pattern copy between Scenes is a `memcpy` of the region.**
    Offsets are region-relative and back-references are Scene-independent,
    so the copy is valid as it stands. Pattern Load already does exactly this
    when it fans a loaded Pattern out to several Scenes
    (`filesystem.c` Pattern phases 51–52): it writes inside the
    `patSvc_prepareSceneReplace()` / `patSvc_finishSceneReplace()` boundary,
    then calls `autosave_markPatternDirty()` and `bank_invalidateSdCleanScene()`
    for each destination. The Session 062 comment on `pat_copyPattern()`
    ("must duplicate … each pool block") is more cautious than the storage
    format requires.
  - **A track, bar or step copy is not a `memcpy`.** Each destination step
    needs its own newly allocated block with its back-reference rewritten.
    The address entry is swapped in with the existing publication order
    (allocate, write, PRIMASK swap, free the old block), so TIM3 never reads
    a half-written block. The pool can run out part-way.
- **The Pattern Stack Service** (`patSvc_*`) serialises every pool mutation
  (binding constraints, §12.14):
  - exactly one mutation-target Scene at a time;
  - bounded work per tick (bulk barriers do 16 steps per tick);
  - trailing-slack reservations (`patSvc_isChunkReserved()`);
  - `patSvc_idle()` at replacement boundaries;
  - every pool-mutating path from Menu goes through it.

### 2.4 Scene-level facts (`BANK_PRESET_ARCHITECTURE.md`, `SceneData.h`)

| Object | Size | Content | Retained writer | AutoSave marker | Runtime apply |
|---|---:|---|---|---|---|
| `scene_settings_t` | 45 B | Scene Morph, per-voice Morph[6], `srt`, MIDI channel/note[7], audio out[6], FX send[6], fader[6], Effect Morph, bus compressor[4] | the individual `scene_set*()` setters (change-aware) | Scene parameter cells 0..44 | Preset mirrors; mixer reads per block |
| `effect_record_t` | 420 B | type, sequence settings, normal[64], Morph[64], 16 steps × (lock mask + 16 values) | `scene_commitEffectRecord()`, `scene_effectRecordForWholeCommit()`/`…Finish…()` | `autosave_markEffectDirty()` | `effects_activateScene()` if the Scene is active |
| `kit_t` | 1,160 B | `kit_settings_t` (the slot-6/track-7 decay pair) + six `kit_instrument_slot_t` | the Kit/Instrument load commit paths | `autosave_markKitDirty()`, `autosave_markWholeInstrumentDirty()` | `menu_startSoundApply()` / `preset_startDrumsetApply()` (Kit); `preset_startInstrumentApply()` (one slot) |
| `kit_instrument_slot_t` | — | `type` + normal image + Morph image + `morph_interpolation[]` (a **runtime** image) | Instrument load commit | whole-Instrument marker | `preset_startInstrumentApply()` rebuilds Morph and rebinds modulation |
| Names and provenance | HCNAMES rows | row 1+s Scene, 17+s Kit, 33+6s+slot Instrument, 129+s Pattern, 145+s Effect; rows are `name<TAB>source[<TAB>R]`, and Instrument rows add a mandatory type field before `R` | filesystem identity APIs | `autosave_markSourceDirty()` | — |

- **Instrument type changes** alter what a VOICE edit mask may group, so
  every commit that changes a slot type calls
  `bank_revalidateVoiceEditMasks()`. The existing apply funnels already do.
- **Effect locals are type-relative.** An Effect copied between Scenes of
  different types must carry its type, which also clears the destination's
  FX locks and changes what its masks may group.
- **Instrument-internal cross references:** LFO target voice selectors store
  a one-based slot number. Kit Save writes `self` for a target equal to the
  source slot and maps it back on load. A copied Instrument that pointed at
  itself must be retargeted to its new slot (§6.6).

### 2.5 Other existing mechanisms worth reusing

- **Fan-out to a SEQ Scene mask:** Kit Load, Pattern Load and Instrument Load
  already take a destination Scene mask chosen with the SEQ buttons on the
  Load page.
- **Apply funnels:** `menu_startSoundApply()`, `menu_startInstrumentApply()`
  and `preset_startDrumsetApply()` handle runtime rebuilds, modulation
  rebinding and mask re-validation over bounded foreground ticks.
- **Whole-object AutoSave markers:** Kit, Instrument, Effect, Scene with or
  without Pattern, Pattern dirty.

---

## 3. What "copy" and "clear" should mean (the proposed component list)

The components fall into two families. They differ in storage and in how
an operation must run.

### 3.1 Pattern family (PatternData, through the Pattern Stack Service)

| Component | Copy | Clear | Existing code |
|---|---|---|---|
| **Step** | trigger, specials (note/velocity/probability) and every automation entry of one step onto another step (any track of the same Pattern). **Replace**, not merge (C6). | `patSvc_eraseStep()` (trigger, specials and automation) | erase exists |
| **Bar** | 16 steps of one track onto another bar of the same or another track; the destination length extends to cover the bar (the existing comment promises this) | 16-step erase | none |
| **Track** | 128 steps, plus that track's length, scale and shuffle (C7) | `patSvc_clearTrack()` | clear exists; copy is a no-op |
| **Automation** | several scopes (C8): one step's entries; one target on a track (a "lane"); all automation of a track, keeping triggers | `pat_removeStepAutomation()`, `patSvc_removeTrackAutomationByTarget()`, plus new "all automation of a track/Pattern" | partial |
| **Pattern** | the whole region to another Scene (`memcpy` through the replace boundary) | `patSvc_clearPattern()` | clear exists; copy is a no-op |

### 3.2 Scene family (SceneData scalars, names, runtime apply)

| Component | Copy | Clear (reset to) | Notes |
|---|---|---|---|
| **Instrument** (one slot) | type + normal + Morph images to another slot of the same or another Scene; retarget `self` LFO targets | the slot type's default image (C10) | like Instrument Load: apply funnel, mask re-validation, HCNAMES row |
| **Voice strip** (per-voice mix) | audio out, FX send, fader mode, voice Morph amount, and the track's MIDI channel/note (C11) | defaults | Scene settings, per slot |
| **Kit** | six Instruments + the Kit settings | a default Kit | like Kit Load |
| **Effect** | the whole record (type, parameters, Morph, sequence) | `off`, with defaults | changes type; re-validates masks |
| **FX sequence** | steps, lane locks and sequence settings (same type only), or one FX step onto another | **all FX locks** (A15), one lane, or one step | a clear-only lane/step removal is also Phase 5 debt A15 |
| **Scene settings** | the 45 B settings record (or chosen groups: mix, MIDI, Morph, bus compressor) | defaults | no runtime rebuild beyond the mirrors |
| **Scene** | settings + Effect + Kit + Pattern + the nine names/sources, to another resident Scene | the empty-Scene state (C12) | the largest operation; marks a full AutoSave drain |

**Out of scope for the first pass (proposed):** Bank-level copy (Bank → Bank
belongs to Load/Save), copy between Banks, library files, and undo (C5).

### 3.3 Which to do first (proposal)

1. **Pattern: step, bar, track, whole Pattern.** These are the
   long-standing no-ops, and gestures for track and bar already exist.
2. **Automation clears and the `autom.*` fall-through fix.**
3. **FX sequence clear** (A15, lock removal) and FX step copy.
4. **Instrument and Kit copy.**
5. **Effect, Scene settings, Scene copy.**

---

## 4. The operation model (architecture)

### 4.1 What a "copy" holds: coordinates, not data (recommended)

All sixteen Scenes and their Patterns are resident in RAM. A copy can
therefore remember **where** the source is, and the paste can read it at
that moment. That is a coordinate clipboard: component kind, source Scene,
track, bar or step, slot, and target — about 6–8 B. The alternatives:

| Model | RAM | Behaviour | Risk |
|---|---:|---|---|
| **A. Direct gesture** (today): source and destination within one COPY hold | ~2 B (exists) | immediate, nothing kept | cannot copy across pages or modes; one destination at a time unless extended |
| **B. Coordinate clipboard** (recommended) | ~8 B | copy marks the source; paste to one or more destinations later, even after moving to another page or Scene | the source may change or be replaced before the paste. Paste uses its **current** content (define as intended), and loads that replace the source Scene invalidate the clipboard. |
| **C. Data clipboard** | step 132 B; bar up to 2.1 KB; track up to 8 KB; Pattern 10.5 KB; Instrument 129 B (type + two 64-byte images); Kit 1,160 B; Scene 1,626 B + 10.5 KB | a snapshot | large, and free SRAM1 is reserved for Pattern data; needs RAM approval per size. The only existing Pattern-sized buffer (`pat_autosave_snapshot`) belongs to Pattern AutoSave and must not be borrowed. |

B keeps RAM near zero and supports multi-destination paste. A snapshot is
only needed where a copy's source and destination overlap in a way that
matters (for example a bar copy onto an overlapping range of the same
track); handle that by copying in a safe order, not with a buffer (C2).

### 4.2 Execution layers

- **Pattern operations** become new bounded service operations (for
  example `patSvc_copyStep`, `patSvc_copyTrack`, `patSvc_copyBar`,
  `patSvc_clearTrackAutomation`). Each has a **pre-flight** check and then
  runs as a bulk barrier at 16 steps per tick, like clear.
  - Pre-flight: the chunks needed (the sum of the source blocks' sizes) must
    fit the destination's free chunks plus the chunks the destination range
    will release, respecting reservations. If they do not, refuse the
    whole operation with a message. **Never leave a partial copy.**
  - Each destination step keeps the publication order: allocate, write the
    block with the new back-reference, swap the address entry under
    PRIMASK, then free the old block.
  - The operation's Scene is the service's single mutation target. A copy
    whose source is another Scene only reads that region; the service must
    not be relocating the source at the time (C3).
  - Completion calls `autosave_markPatternDirty()` through PatternData's
    dirty helper, `menu_voiceAutoOverlayPatternDeleted()` (the underline
    search restart; the name should become "Pattern changed") and a LED
    repaint.
- **Whole-Pattern copy** uses the replace boundary exactly as Pattern Load
  does: `patSvc_prepareSceneReplace(dst)` until it returns 1, `memcpy`,
  `patSvc_finishSceneReplace(dst)`, `autosave_markPatternDirty(dst)`,
  `bank_invalidateSdCleanScene(dst)`. Decide whether `pattern_next` and
  `pattern_change_bar` are copied (C7).
- **Scene-family operations** commit through SceneData:
  - whole-record commits (the Effect has them; Kit and Instrument follow the
    load paths' commit code);
  - then the typed whole-object AutoSave markers;
  - then the existing apply funnel **only if the destination is the active
    Scene** (inactive Scenes are applied on the next Scene switch, as loads
    do);
  - then `bank_revalidateVoiceEditMasks()` whenever a type changes.
  - Names and sources are published through the filesystem identity APIs
    (C4).
- **Foreground only.** Nothing runs in an ISR. Blocked states refuse the
  operation, never queue it silently:
  - the service busy on another Scene's barrier;
  - a filesystem operation in progress;
  - `seq_recordActive` or `seq_eraseActive`;
  - the Load/Save pages.

### 4.3 Destinations and the VOICE edit mask

- **Single or multiple destinations (C1):** Load already uses a SEQ Scene
  mask for Kit/Pattern/Instrument fan-out. Copy could do the same at Scene
  level (copy Scene 3's Kit to Scenes 5, 6, 9) and at track level (copy
  track 1 to tracks 2 and 4).
- **Edit-mask fan-out:** VOICE and Effect edits fan out to the active
  Scene's VOICE edit mask. Copy destinations are chosen explicitly, so
  **copies should not also fan out through the edit mask** (recommended;
  C1). The mask gate (`scene_editLayoutMatches()`) must still be
  re-validated afterwards, since a copy can change types.

### 4.4 Identity and provenance (HCNAMES)

A copied Kit, Instrument, Effect, Pattern or Scene gets a destination row.

- **Recommended:** copy the source row's **name and source token**. The
  destination then holds the same library object, possibly edited, which
  AutoSave captures as payload. Mark the source bytes dirty
  (`autosave_markSourceDirty()`). Do **not** set the refreshed `R` witness:
  the content did not come from the library.
- A cleared object gets a blank name. Its source (`-` or inherit) is C4.

### 4.5 Confirmation and undo

- Clears are destructive and already ask for confirmation (`clear
  [track]?` plus a click). Keep that.
- Proposal: confirm Scene-family copies (they overwrite a lot) but not
  Pattern-family copies (fast gestures).
- **No undo in the first pass** (C5). A useful undo needs a snapshot the size
  of the largest destination, up to 10.5 KB for a Pattern. That is RAM the
  policy reserves for Pattern data, and borrowing the Pattern AutoSave
  snapshot would race the writer.

### 4.6 UI model (C13)

| Option | How | For |
|---|---|---|
| **A. Context gestures** | COPY held + source button + destination button(s), with the meaning taken from the current mode: SEQ = steps in step mode, SELECT = bars, VOICE = tracks (sequencer) or Instruments (VOICE pages), SEQ = Scenes in PERF mode, SEQ = FX steps on the Effect page | fast Pattern work; matches LXR habits and today's track/bar gestures |
| **B. A Copy/Clear page** | like Load/Save: a type row (Step, Bar, Track, Autom, Inst, Kit, FX, FXseq, Mix, SceneSet, Pattern, Scene), source and destination fields, SEQ masks for Scene destinations, OK to execute | the Scene family, multi-destination, anything that needs confirmation |
| **C. Hybrid (recommended)** | A for step, bar, track and FX step; B (or an extended SHIFT+COPY menu) for automation scopes and the Scene family | both |

The SHIFT+COPY clear menu already has an encoder-selected target list; it
can grow into the clear side of B.

**Button meaning conflicts to resolve:** VOICE buttons mean "track" (Pattern)
on sequencer pages but "Instrument" on VOICE pages. Track 7 has no
Instrument of its own: it is slot 6's alternate sound.

---

## 5. Component details and open points

### 5.1 Step

- Source and destination can be any (track, step) in the same Pattern
  (another Pattern: C3).
- The copy is exact: the destination trigger, specials and automation become
  the source's. The source's absence also copies, so an empty source step
  clears the destination (C6).
- **Automation targets are voice-specific** (`slot·64 + descriptor`). Copying
  a step from track 1 to track 3 copies voice-1 targets onto track 3. The
  sequencer applies automation by target, not by track, so track 3's step
  would then automate voice 1. Decide: keep targets (a literal copy) or
  retarget to the destination voice when the types match (C8).
- **Effect and Scene targets** (IDs 384..510) are track-independent; copy them
  as they are.

### 5.2 Bar

- 16 steps. The existing gesture is COPY + SELECT *a* → SELECT *b* on the
  active track.
- Extend the destination track's length when the destination bar lies
  beyond it (as the existing comment promises)? Decide in C7.
- Overlap on the same track is impossible: bars are disjoint.

### 5.3 Track

- 128 steps. Include the track's length, scale and shuffle (C7).
- Within one Pattern the source and destination share a pool: pre-flight
  counts the source blocks against the destination's free space plus what
  the destination track releases.
- Across Patterns: C3.
- Track 7's automation behaves like any track's.

### 5.4 Automation

- **Scopes** (C8):
  - one step (all entries);
  - one target on one track (a lane; `pat_removeTrackAutomationByTarget()`
    exists for clear);
  - all automation of a track (triggers kept);
  - one target across the whole Pattern;
  - all automation of a Pattern.
- **Copying a lane** to another track: keep the target, or retarget to the
  destination voice's same descriptor when the Instrument types match (C8).
- **UI:** choosing a target needs a menu (the STEP automation page already
  lists targets by category). This argues for the page or menu route (4.6
  B/C).
- **The old `autom.1`/`autom.2` targets** should become real scopes (for
  example "track automation" and "Pattern automation"), or be removed. They
  must stop clearing the track (C9).

### 5.5 Pattern

- `memcpy` through the replace boundary (§4.2). A destination that is the
  playing Pattern takes effect at once, exactly like Pattern Load into the
  playing Scene.
- Decide whether `pattern_next` (the chain pointer) and `pattern_change_bar`
  travel with it (C7).
- The destination's HCNAMES Pattern row (129+s) gets the source's name.
  Pattern AutoSave `@` provenance: the new content is not the library
  Pattern, so the row should become `@` once published, as after an edit
  (C4).

### 5.6 Instrument

- Copy `type` + normal + Morph images. Do not copy `morph_interpolation[]`:
  the apply funnel rebuilds it.
- Retarget any LFO target voice equal to the source slot to the destination
  slot (the Kit Save `self` rule). Velocity targets are self-scoped already.
- If the destination Scene is active: `preset_startInstrumentApply()`. In
  all cases: `bank_revalidateVoiceEditMasks()`, whole-Instrument AutoSave
  marker, HCNAMES Instrument row.
- **Slot 6 / track 7:** copying into slot 6 changes what track 7 plays (the
  Choke alternate for a HiHat, or the Kit's generated decay for other
  types). The Kit-owned `slot6_track7_*` decay values are Kit settings, not
  Instrument data (C10).
- **Clear:** reset to the type's descriptor defaults (the image a new
  Instrument of that type gets), keeping the type (C10).

### 5.7 Kit

- Copy `kit_t` (six slots + Kit settings) Scene to Scene. Same runtime and
  AutoSave path as Kit Load (`menu_startSoundApply()` when active; mask
  re-validation; the seven Kit/Instrument HCNAMES rows).
- Per-voice mix settings (audio out, FX send, fader) are **Scene** settings,
  not Kit, so a Kit copy does not move them. That matches Kit Load.

### 5.8 Effect and FX sequence

- **Effect copy:** the whole record through `scene_commitEffectRecord()`,
  then `effects_activateScene()` if the destination is active, then mask
  re-validation. Effect Morph (`effect_morph_amount`) is a Scene setting:
  copy it with the Effect (recommended) or not (C11).
- **FX sequence copy:** steps and sequence settings only; same type only
  (lane meaning is type-relative).
- **FX step copy on the Effect page:** COPY + SEQ *a* → SEQ *b* copies the
  lock mask and values. Both go through the EffectsManager edit API so they
  fan out and mark AutoSave like other Effect edits (here the edit-mask
  fan-out is the existing rule for Effect edits; C1).
- **FX lock clear (A15):** all steps, one lane, or one step. This closes the
  Phase 5 debt "locks cannot be removed".

### 5.9 Scene settings and the voice strip

- Groups: mix (audio out, FX send, fader per voice), MIDI (channel/note per
  track), Morph (Scene + per-voice), `srt`, Effect Morph, bus compressor.
  Copy all, or by group (C11).
- Each field goes through its change-aware SceneData setter, so AutoSave
  marking is automatic.
- If the destination is active, refresh the Preset mirrors
  (`preset_applySceneSettings()`-style) so pages and the mixer see the new
  values.

### 5.10 Scene

- Everything: settings, Effect, Kit, Pattern, and the nine identity rows
  (Scene, Kit, six Instruments, Effect) plus the Pattern row.
- Implement as the composition of the pieces above, run as one foreground
  transaction with bounded steps: the Pattern replace boundary, the SceneData
  commits, one apply if the destination is active, mask re-validation, and
  `autosave_markSceneWithPatternDirty()`.
- **Bank present mask:** copying into an empty Scene makes it present.
- **Clear (C12):** reset to defaults but keep the Scene present, or empty it
  as Bank Load's empty-Scene path and AutoSave's Case 3 do (present bit off,
  rows `-`).
- A Scene copy dirties about 1.9 KB of scalar payload plus a whole Pattern.
  AutoSave publishes it over one drain cycle plus a Pattern write; that is
  expected.

---

## 6. Problems found while surveying (fix in Phase 6)

1. **`autom.1`/`autom.2` clear the whole track** (§2.2). This is
   destructive; highest priority.
2. **The track and bar copy gestures silently do nothing** except clear the
   LEDs, because `pat_copyTrack/Bar` are no-ops.
3. **`copyClear_copyPattern()` has no caller.**
4. **`copyClear_clearTrackAutom()` is an empty stub**, a leftover from the
   LXR automation-lane model.
5. **The copy source and destination are raw button indices** (`int8_t`,
   `-1` = none) masked in `copyClearTools.c`. PatternData validates
   coordinates, but the model should become explicit coordinates if the
   coordinate clipboard (§4.1) is adopted.
6. `copyClear_armClearMenu()` writes the LCD directly (`lcd_clear()`,
   `lcd_string()`, "TODO this wastes RAM"), outside the Menu repaint model.
   Keep that in mind if the clear menu grows.

---

## 7. Resources (estimates, to confirm in the schedule)

| Resource | Estimate |
|---|---|
| RAM | Coordinate clipboard about 8 B SRAM1 (needs approval). New service barriers reuse the service's cursor state. A snapshot-based design would need approval per buffer. |
| Flash | A few KB for the service operations, Scene-family commits and UI (250 KB free). |
| CPU | Foreground only, bounded per tick (16 steps per service tick); no DSP change. |
| AutoSave | Pattern ops mark the Pattern dirty (one PAT4 write); Scene-family copies mark whole objects (drain cycles as after a Load). |

---

## 8. Hardware checks to plan for (outline)

- Each Pattern copy (step, bar, track, Pattern) on a playing Pattern: no
  glitches, correct result, LEDs and underlines refresh.
- Pool-full refusal (build a dense Pattern): a clear message, nothing
  changed.
- Automation copy: targets behave as decided (C8); the STEP page and the
  VOICE/Effect underlines agree.
- Instrument/Kit/Effect/Scene copies into the active and an inactive Scene:
  sound, modulation and LFO `self` targets correct; VOICE edit masks
  re-validated; HCNAMES rows as decided.
- AutoSave: power-cycle after each kind of copy; the data survives.
- Clears: each scope, with confirmation; `autom.*` no longer clears the
  track.
- Regression: Load/Save, the Pattern Stack Service under recording, VOICE
  and Effect underlines.

---

## 9. Decisions for the user

| # | Question | Recommendation |
|---|---|---|
| C1 | Destinations: one at a time, or several (SEQ Scene mask / several tracks while COPY is held)? Should copies also fan out through the VOICE edit mask? | Allow several explicit destinations; **no** edit-mask fan-out for copies, except FX step copies, which use the Effect edit API like any Effect edit |
| C2 | Clipboard model: direct gesture (A), coordinate clipboard (B), or data clipboard (C)? | B (about 8 B), with invalidation when a load replaces the source Scene; paste uses the source's current content |
| C3 | May Pattern-family copies cross Patterns (Scene A track 2 → Scene B track 5)? | Yes for whole Patterns (replace boundary). For step/bar/track, first pass within one Pattern, extending across Patterns once the service handover for a read-only source is designed |
| C4 | HCNAMES on copy and clear: copy name and source from the source row? What source does a cleared object get? | Copy name and source; mark the source bytes dirty; no `R`. Cleared objects: blank name, source `-` |
| C5 | Undo? | None in the first pass (RAM policy) |
| C6 | Step copy: replace or merge automation? Does an empty source step clear the destination? | Replace; yes |
| C7 | Do track copies carry length/scale/shuffle? Does a bar copy extend the destination length? Do Pattern copies carry `pattern_next`/`pattern_change_bar`? | Yes; yes; yes (a Pattern copy is the whole region) |
| C8 | Automation copy across tracks: keep voice targets literally, or retarget to the destination voice (same type)? Which automation scopes are needed first? | Retarget when the destination slot has the same Instrument type, else keep literally; first scopes: step, track (all), lane (one target on a track) |
| C9 | What replaces the `autom.1`/`autom.2` clear targets? | `trk aut` (all automation of the track) and `pat aut` (all automation of the Pattern); fix the fall-through first |
| C10 | Instrument clear: the type's defaults, or change to a default type? Does an Instrument copy into slot 6 also copy the Kit's slot-6/track-7 decay pair? | Type defaults, type kept; copy the decay pair only with Kit copies |
| C11 | Scene-settings copy granularity: all, or groups (mix / MIDI / Morph / bus compressor)? Does Effect copy include Effect Morph? | Groups, with "all"; yes |
| C12 | Scene clear: defaults but present, or empty (present bit off)? | Empty, matching Bank Load's empty-Scene path |
| C13 | UI model: gestures, a Copy/Clear page, or the hybrid? Which button means what on which page? | Hybrid (§4.6) |
| C14 | Confirmation: which operations ask first? | All clears; Scene-family copies; not Pattern-family copies |

---

## 10. Working rules (from `MEMORY.md` and the user)

- **Plans first:** agree the decisions, then a line-level schedule (every
  change by file, line and action, with comment blocks); the user or an
  implementing agent they direct applies it; then review and gates.
- **RAM approval** for any new or larger allocation (byte count, region,
  lifetime, owner).
- **Constant-CPU rule** for anything in the audio path (copy/clear are
  foreground and should not touch it).
- **Pattern mutations only through the Pattern Stack Service**; publication
  ordering and bounded barriers are binding.
- **SceneData is the only writer of retained Scene data; mark AutoSave after
  every retained change.**
- **No unrequested features; log unrelated findings in `SCOPING_TARGETS.md`.**
- **Hardware checks and commits are the user's.**
