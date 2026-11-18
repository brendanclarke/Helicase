# S098 P0 — Implementation Schedule: Scene Switch Corrections

Date: 2026-11-18. Branch `dev-ph7-alkaline` at `04c5c004`, with the S097 P3
work uncommitted in the tree (fact only). **Status: P1 and P2 applied in the
tree, assessed, and the assessment fixes F1/F2 applied; builds, host tests
and the loop gate are green. Device matrices remain (see the assessment and
§A.6 at the end).**

Plan and decisions: `S098_P0_SCENE_SWITCH_CORRECTIONS.md`. Q1–Q11 were decided
by the user on 2026-11-18. Q2 was decided against the recommendation: one
switch procedure, decided per track. T3 (the Scene 17 LED) was added.

**Line numbers** are those of the unmodified tree. Inside each file, apply the
entries from the bottom of the file upwards, so the earlier line numbers stay
valid; or apply them top-down and use the quoted anchor text to find each site.

**Each entry gives:**

- the file and lines;
- the action (**add**, **modify**, **remove**);
- the code, with the comment block that goes into the source.

As in every schedule since S084, the in-code comments cite sessions (`S098 P1`)
but never schedule entry numbers.

---

## 0. Sign-off needed before implementation

**RAM: +3 B SRAM1 (.bss).**

| Bytes | Symbol | File | Phase | Why |
|------:|--------|------|-------|-----|
| 1 | `drumset_apply_rebind_mask` | `presetManager.c` | P1 | the sources the Scene worker rebinds when its last changed slot commits (today it is always all six) |
| 1 | `instrument_apply_rebind_mask` | `presetManager.c` | P1 | lets the shared rebind cursor skip the sources a switch did not touch |
| 1 | `pm_bg_kind` | `presetManager.c` | P2 | the Load kind of the background machine. `pm_request_type` cannot carry it, because `preset_loadName()` (`presetManager.c:4143`) rewrites it while the user browses |

Approval: **approved by the user, 2026-11-18**, before implementation.

**No other RAM.** The identity compare uses two `PAT_RAW_BLOCK_MAX` (136 B)
buffers on the foreground stack. `preset_copyBackgroundTrack()` already uses
one buffer of the same size.

---

## 1. What the code does after S098 (summary for review)

### Scene change (P1)

`menu_perfSceneChangeExecute()` handles every executed whole-Scene change:
PERF, MIDI PC, deferred, queued and chain-fired. It now runs four steps.

1. **Decide.** `preset_sceneSwitchKeepMask(n, &effect_same)` compares, per
   track, what the track plays now with what Scene `n` holds for it, using the
   Scene 17 field lists. Bit `t` set means track `t` and its slot are
   identical. Tracks 5 and 6 are decided as a pair. `effect_same` compares the
   Effect record and `effect_morph_amount`.
2. **Carry.** `preset_prepareSceneSwitch(n, keep)` copies `morph_interpolation[]`
   of the kept slots into `n`. It also retargets the Morph worker and drops the
   changed slots from its current pass.
3. **Switch the sequencer.** `seq_selectActivePattern(n, keep, effect_same)`:
   - every track reads `n`;
   - changed tracks get the per-track reset, realign, change mark, trigger
     drop and MIDI note-off;
   - kept tracks keep everything;
   - the FX sequence is realigned only when the Effect changed.
4. **Apply the voices.** `preset_startSceneSwitchApply(keep, effect_same)`:
   - changed slots: LFO phase captured, step overrides cleared, Morph pass
     requested, and the drumset worker commits them at quiet or trigger, as
     today;
   - the modulation graph is prepared only where it touches a changed slot or
     a changed Effect, and the rebind cursor revisits only those sources;
   - the Effect is rebound when identical, else activated;
   - the PERF mirrors are synced.

### Loads through Scene 17 (P2)

- Scene and Pattern Loads from the Menu go through the background machine.
  When a played Scene is a destination and the transport runs, the machine
  builds Scene 17 and moves playback there before the Load is posted.
- A running Load marks its destinations unavailable until it completes.

### Scene 17 LED (T3)

`LED_MODE4` fast-blinks whenever Scene 17 exists, in every mode.

---

# P1 — T1: one switch procedure, decided per track

## A. `Core/Sequencer/sequencer.c`

### A1. l. 1749 — **add** a forward declaration after
`static void seq_loopProcessTick(void);`

```c
/* S098 P1: the position-only half of seq_realignTrackToMasterClock(). */
static uint8_t seq_realignTrackPosition(uint8_t track);
```

### A2. l. 1558 — **add** after the closing brace of `seq_clearSceneAutomationDirty()`

```c
/*
 * Clear the Scene-target restore bits of the parts a switch changes
 * (S098 P1).
 *
 * What: drops the seq_scene_automation_dirty bits whose target belongs to a
 * changing part of a whole-Scene switch:
 *  - a voice target (Morph amount, audio out, FX send) of a changed slot;
 *  - the slot 6/track 7 decay when slot 6 changes;
 *  - a track timing target of a changed track;
 *  - the Effect Morph amount when the Effect changes;
 *  - any bit whose descriptor no longer resolves.
 * Bits that belong to kept tracks and slots stay.
 * Why: the bitmap tells the transport-boundary restore which retained values
 * to re-apply. A kept part keeps playing its automation across the switch,
 * so its restore duty must survive. A changed part's bits describe the
 * outgoing content, which is why S076 Rule B cleared the whole bitmap at
 * every Scene change. The whole-bitmap clear
 * (seq_clearSceneAutomationDirty()) stays for
 * seq_alignActivePatternToScene().
 * Inputs: track_mask (bit t = track t changed; tracks 5 and 6 map to slot 6),
 * effect_changed.
 * Outputs: seq_scene_automation_dirty.
 * Context: foreground, the same context as the drain that sets the bits
 * (seq_applySceneAutomation()).
 * Accessors: seq_selectActivePattern().
 * Affiliates: seq_restoreAllSceneAutomation(), sceneModTarget_descriptor(),
 * sceneModTarget_idFromIndex().
 */
static void seq_clearSceneAutomationDirtyFor(uint8_t track_mask,
                                             uint8_t effect_changed)
{
    uint32_t mask = seq_scene_automation_dirty;
    uint8_t slot_mask = (uint8_t)(track_mask & 0x1Fu);

    if ((track_mask & 0x60u) != 0u)
        slot_mask = (uint8_t)(slot_mask | 0x20u);
    while (mask) {
        uint8_t index = (uint8_t)__builtin_ctz(mask);
        const scene_mod_target_descriptor_t *descriptor =
            sceneModTarget_descriptor(sceneModTarget_idFromIndex(index));
        uint8_t drop = 1u;

        mask &= mask - 1u;
        if (descriptor) {
            switch (descriptor->kind) {
            case SCENE_MOD_TARGET_KIND_VOICE_MORPH:
            case SCENE_MOD_TARGET_KIND_AUDIO_OUT:
            case SCENE_MOD_TARGET_KIND_FX_SEND:
                if (descriptor->voice_slot < INSTRUMENT_SLOT_COUNT)
                    drop = (uint8_t)((slot_mask >> descriptor->voice_slot) &
                                     1u);
                break;
            case SCENE_MOD_TARGET_KIND_SLOT6_TRACK7_AMP_DECAY:
                drop = (uint8_t)((slot_mask >>
                                  (INSTRUMENT_SLOT_COUNT - 1u)) & 1u);
                break;
            case SCENE_MOD_TARGET_KIND_TRACK_LENGTH:
            case SCENE_MOD_TARGET_KIND_TRACK_SCALE:
            case SCENE_MOD_TARGET_KIND_TRACK_SHUFFLE:
                if (descriptor->voice_slot < NUM_TRACKS)
                    drop = (uint8_t)((track_mask >> descriptor->voice_slot) &
                                     1u);
                break;
            case SCENE_MOD_TARGET_KIND_EFFECT_MORPH:
                drop = (uint8_t)(effect_changed != 0u);
                break;
            default:
                break;
            }
        }
        if (drop)
            seq_scene_automation_dirty &= (uint32_t)~(1u << index);
    }
}
```

### A3. l. 2366 — **add** between the closing brace of `seq_resetStepScheduler()` (l. 2365) and `void seq_selectActivePattern(` (l. 2367)

```c
/*
 * Reset one track's playback state for a Scene change (S098 P1).
 *
 * What: the per-track part of a whole-Scene change:
 *  - the play state (a finished once track restarts, S078 §4.3);
 *  - the X:Y repeat, random-step and last-condition counters
 *    (S079 §3.2/§3.4);
 *  - the step roll and plain-step length window (S079 P2 fix 4, S084 P1,
 *    S096 P3);
 *  - the slot's R12 menu override (S081 §5.1);
 *  - the automation ramp (S081 P2);
 *  - the echo mark (S081 C-6/C-10).
 * Why: user, S098 Q2: the switch runs the same way whether tracks are
 * identical or not. A track whose content changes gets exactly this; a track
 * whose content is identical keeps all of it, so it cannot be heard to
 * switch. It is the same set that seq_setTrackPlayedScene() resets for a
 * per-track assignment.
 * Inputs: track 0..6 (the caller passes tracks 5 and 6 together).
 * Outputs: the track's sequencer state listed above.
 * Context: foreground, inside the caller's PRIMASK section. TIM3 reads all of
 * it, and seq_menuOverride[] is 64-bit.
 * Accessors: seq_selectActivePattern().
 * Affiliates: seq_setTrackPlayedScene() (the per-track twin),
 * seq_rollClearTrack(), seq_rampBreakTrack().
 */
static void seq_resetTrackForSceneChange(uint8_t track)
{
	seq_trackPlayState[track] = 0u;
	seq_trackRepeatCount[track] = 0u;
	seq_trackRndStepCount[track] = 0u;
	seq_trackLastConditionFired[track] = 0u;
	seq_rollClearTrack(track);
	seq_menuOverride[(track >= INSTRUMENT_SLOT_COUNT)
	                 ? (INSTRUMENT_SLOT_COUNT - 1u) : track] = 0u;
	seq_rampBreakTrack(track);
	seq_echoStep[track] = -1;
}
```

### A4. l. 2367–2506 — **modify**: replace the whole of `seq_selectActivePattern()`

**Remove:** l. 2367 (`void seq_selectActivePattern(uint8_t pattern)`) through
l. 2506 (the closing brace after `seq_clearSceneAutomationDirty();`).

**Add in its place:**

```c
void seq_selectActivePattern(uint8_t pattern, uint8_t keep_mask,
                             uint8_t effect_same)
{
	uint8_t change;
	uint8_t track;
	uint32_t primask;

	/*
	 * Switch playback to a newly selected Scene, decided per track
	 * (S098 P1; user, Q2).
	 *
	 * What: every track reads `pattern` from now on. A track whose bit is
	 * set in keep_mask was found identical by
	 * preset_sceneSwitchKeepMask(): its Pattern data, track settings and
	 * voice slot are equal in the Scene it played and in `pattern`. It keeps
	 * its cursor, DDA phase, play state, conditions, roll, length window,
	 * menu override, ramp, echo mark, queued triggers and MIDI note, as the
	 * Scene 17 switch keeps them (seq_switchToBackgroundScene()). Every other
	 * track gets today's per-track change: the reset
	 * (seq_resetTrackForSceneChange()), the realign to the master clock, the
	 * Colloid change mark, its queued triggers dropped and its MIDI note
	 * ended.
	 * Why: target T1. An instant switch to an identical Scene must be
	 * inaudible, whenever it happens. The former version reset every track
	 * on every change (S1-S11 in S098_P0 §2.2), which cut notes, restarted
	 * conditions and once-mode tracks, jumped the random mode and dropped the
	 * trigger TIM3 had just queued. The rule is the same whether all, some or
	 * none of the tracks are identical: a whole identical Scene is the case
	 * keep_mask = 0x7F.
	 * Inputs: pattern (validated by pat_patternValid()); keep_mask (bit t =
	 * track t identical; tracks 5 and 6 are forced to agree);
	 * effect_same (the Effect record and Effect Morph amount are identical).
	 * Outputs: active, pending and per-track Scene; per-track state of the
	 * changed tracks; FX clock (Effect changed only); LEDs; program change;
	 * Scene 17 release.
	 * Accessors: menu_perfSceneChangeExecute() (the only caller).
	 * Affiliates: preset_sceneSwitchKeepMask(), preset_startSceneSwitchApply()
	 * (the voice half), seq_switchToBackgroundScene().
	 */
	if (!pat_patternValid(pattern))
		return;
	keep_mask = (uint8_t)(keep_mask & 0x7Fu);
	if ((keep_mask & 0x60u) != 0x60u)
		keep_mask = (uint8_t)(keep_mask & (uint8_t)~0x60u);
	change = (uint8_t)(~keep_mask & 0x7Fu);

	/*
	 * S080 G7/G12: a Scene selection starts a fresh chain.
	 *
	 * What: remembers the outgoing Scene for the >NN conditions (only on a
	 * real change), restarts the bar/repetition counts and releases a `stp`
	 * freeze. Why: counts are per Scene activation; any executed change -
	 * manual, queued or chain - cancels pending conditional state (plan
	 * §4.5). A chain decides what plays next, not what sounds now, so this
	 * holds for kept tracks too. Affiliates: seq_chainServiceTick(),
	 * seq_chainEvaluate().
	 */
	if (pattern != seq_activePattern)
		seq_previousScene = seq_activePattern;
	seq_chainBarCount = 0u;
	seq_chainRepCount = 0u;
	seq_chainStopped = 0u;

	/*
	 * Move every track and reset the changed ones in one masked section
	 * (S098 P1).
	 *
	 * What: the read address of every track changes to `pattern`, and the
	 * per-track state of the changed tracks is reset, with TIM3 masked.
	 * Why: TIM3 must never see a changed track on the new Scene with its old
	 * roll, counters or play state, nor a 64-bit menu override half written.
	 * For a kept track only the read address moves, and both Scenes hold the
	 * same data. The scene-level switch always coalesces
	 * (S077 P2 §4.3, Q2), so seq_perTrackActive is 0 afterwards.
	 */
	primask = __get_PRIMASK();
	__disable_irq();
	seq_activePattern = pattern;
	seq_pendingPattern = pattern;
	for (track = 0u; track < NUM_TRACKS; track++) {
		seq_perTrackPattern[track] = pattern;
		if (change & (uint8_t)(1u << track))
			seq_resetTrackForSceneChange(track);
	}
	seq_perTrackActive = 0u;
	seq_loadPendigFlag = 0u;
	seq_newPatternAvailable = 0u;
	__set_PRIMASK(primask);

	/*
	 * Epochs move only when something changed (S081 C-6/C-10, P1B item 10;
	 * S098 P1).
	 *
	 * What: a changed track bumps the playback epoch (echo marks, MIDI
	 * note-mode re-assert) and the Scene epoch (a new LiveRecord take). The
	 * boundary ring is emptied only when no track is kept.
	 * Why: with nothing changed, the switch must leave LiveRecord's take, its
	 * latches and the boundary ring exactly as they are (as the Scene 17
	 * switch does, D35). LiveRecord re-resolves its target from the played
	 * Scene on every pass, so it records into `pattern` from now on. A kept
	 * track's ring entries are real boundaries of the music that continues,
	 * so only a switch with no kept track drops them, as before. A changed
	 * track's echo mark was expired by its reset above.
	 */
	if (change != 0u) {
		seq_playbackEpoch++;
		seq_sceneEpoch++;
	}
	if (keep_mask == 0u)
		seq_liveRecResetMarks();
	/* S078 §5.4: the played Scenes changed, so rebuild the effective cache. */
	seq_refreshTrackEffectiveParams();
	{
		/*
		 * S080 P2 Defect 14: realign and re-snapshot the looper atomically.
		 *
		 * What: each changed track moves to its master-clock position in
		 * the new Scene; kept tracks keep their position, because their
		 * Pattern, length, scale and play mode are identical. Every looped
		 * track keeps its loop-start tick while its snapshot is recomputed
		 * for the new Scene. Why: the TIM3 ISR must never wrap a track to
		 * the outgoing Scene's snapshot after the switch.
		 * Affiliates: seq_realignTrackPosition(),
		 * seq_loopRefreshSceneSnapshots().
		 */
		primask = __get_PRIMASK();
		__disable_irq();
		for (track = 0u; track < NUM_TRACKS; track++)
			if (change & (uint8_t)(1u << track))
				(void)seq_realignTrackPosition(track);
		seq_loopRefreshSceneSnapshots();
		__set_PRIMASK(primask);
	}
	/*
	 * The FX sequence follows the Effect, its overlays follow the tracks
	 * (S081 §3.3-4, R13; S098 P1).
	 *
	 * What: a changed Effect realigns the FX sequence on the master grid, as
	 * every Scene change did before. An identical Effect keeps its FX clock,
	 * step and locks (preset_startSceneSwitchApply() rebinds it without a
	 * reset). The Effect track overlays carry no track identity, so they are
	 * cleared when the Effect or any track changes; with nothing changed they
	 * stay. Why: target T1; an FX sequence in `rnd` would otherwise jump on
	 * every switch.
	 */
	if (!effect_same)
		seq_fxRealignToMasterClock();
	if (!effect_same || change != 0u)
		seq_requestEffectOverlayReset();
	/*
	 * Release Scene 17 on the first whole-Scene change (S082 C8, D9a, D10).
	 *
	 * What: once every track points at a resident Scene, Scene 17's storage
	 * becomes invalid and can never be selected again; AutoSave and overlap
	 * paste return because their Scene 17 gates fall away.
	 * Why: Scene 17 owns the sounding Effect and Scene-level state until a
	 * whole-Scene change replaces them; per-track moves never release it.
	 * The tracks found identical to Scene 17's copy keep playing unchanged
	 * (S098 P1).
	 * Inputs: new Scene p < 16 and an existing Scene 17.
	 * Outputs: scene_backgroundRelease().
	 * Accessors: seq_selectActivePattern() (SEQ press, MIDI PC, queued/chain).
	 * Affiliates: scene_backgroundRelease(), pat_regionFor(), the C9 gates.
	 */
	if (pattern < SCENE_COUNT && scene_backgroundExists())
		scene_backgroundRelease();
	led_notifyPatternChanged(seq_activePattern);
	seq_sendProgChg(seq_activePattern);
	/*
	 * End what a changed track was playing (S096 P3 C1, S083; S098 P1).
	 *
	 * What: for each changed track, the Colloid change decision is armed
	 * (it releases at the track's first step in the new Scene unless a
	 * trigger or sus-on step keeps it, plan §2.1 rule 6), the trigger TIM3
	 * queued for it is dropped and its MIDI note is ended (the new Scene may
	 * map it to another channel or note). Kept tracks get none of this.
	 * Why: before S098 this ran for every track (seq_markTrackChange(0x7F),
	 * voiceControl_noteOff(0xFF), midiOut_trackNoteOff(0xFF)). It dropped the
	 * step TIM3 had just fired when the switch landed between that tick and
	 * the next render pass, which made the result depend on when the switch
	 * happened. An identical track's mapping and note are the same in both
	 * Scenes, so its note must continue.
	 * Affiliates: seq_markTrackChange(), voiceControl_noteOff(),
	 * midiOut_trackNoteOff().
	 */
	if (change != 0u)
		seq_markTrackChange(change);
	for (track = 0u; track < NUM_TRACKS; track++) {
		if ((change & (uint8_t)(1u << track)) == 0u)
			continue;
		voiceControl_noteOff(track);
		midiOut_trackNoteOff(track);
	}
	/*
	 * S076 Rule B, per part (S098 P1): drop only the Scene-target restore
	 * bits of the changed tracks, slots and Effect.
	 * Affiliate: seq_clearSceneAutomationDirtyFor().
	 */
	seq_clearSceneAutomationDirtyFor(change, (uint8_t)(effect_same == 0u));
}
```

### A5. l. 5041 and l. 5059–5101 — **add** `seq_realignTrackPosition()`, **modify** `seq_realignTrackToMasterClock()`

**Add** at l. 5041 (the blank line between the closing brace of
`seq_realignActivePatternToMasterClock()` and the comment block that begins
`Realign a single track's step position`, l. 5042):

```c
/*
 * Put one track's cursor and DDA phase on the master clock (S098 P1).
 *
 * What: the position-only half of seq_realignTrackToMasterClock(). It
 * derives the step index and accumulator from seq_elapsedPpqTicks with the
 * track's effective length and scale and its play mode (S078 §2.4.5), sets
 * the pendulum direction, seq_lastMasterStep and, for the selected voice,
 * the chase LED. A stopped once-mode track is left parked.
 * Why: the whole-Scene switch realigns only its changed tracks, inside one
 * PRIMASK section, and must not repeat the per-call side effects of the full
 * realign: the epoch bumps, menu override, ramp break, Scene epoch, echo
 * mark and FX realign, which it handles once per switch and only for what
 * changed. The two callers share one formula.
 * Inputs: track 0..6, seq_perTrackPattern[], seq_effective* caches.
 * Outputs: seq_stepIndex[], seq_trackAccumulator[], seq_trackPlayState[]
 * (PIP_REV), seq_lastMasterStep[], chase LED. Returns 0 when nothing moved
 * (invalid track, or a stopped once-mode track), 1 otherwise.
 * Context: foreground; the caller masks TIM3 when it needs atomicity.
 * Accessors: seq_realignTrackToMasterClock(), seq_selectActivePattern().
 * Affiliates: seq_stepIndexForMode(), stepScale_ticksQ8().
 */
static uint8_t seq_realignTrackPosition(uint8_t track)
{
	const pat_scene_region_t *region;
	uint8_t len;
	uint8_t play_mode;
	uint16_t interval;
	uint32_t master_q8;
	uint32_t position;

	if (track >= NUM_TRACKS)
		return 0u;
	region = pat_sceneRegion(seq_perTrackPattern[track]);
	/* Effective (morphed) length and scale from the S078 §5.4 cache. */
	len = seq_effectiveTrackLength[track];
	if (len < 1u)
		len = NUM_STEPS_PER_BAR;
	play_mode = (region) ? region->track_play_mode[track] : 0u;
	if (play_mode >= 6u)
		play_mode = 0u;
	if ((play_mode == 4u || play_mode == 5u) &&
	    (seq_trackPlayState[track] & SEQ_PLAY_STATE_STOPPED)) {
		/* Stopped once-mode tracks are not restarted by realignment. */
		return 0u;
	}
	/*
	 * Single-track DDA phase realignment (S078 §2.4.5). Same Q8.8 phase and
	 * play-mode mapping as the all-track variant above.
	 */
	interval = stepScale_ticksQ8(seq_effectiveTrackScale[track]);
	master_q8 = (uint32_t)seq_elapsedPpqTicks << 8u;
	position = master_q8 / interval;
	seq_stepIndex[track] = seq_stepIndexForMode(play_mode, position, len);
	seq_trackAccumulator[track] = (uint16_t)(master_q8 % interval);
	if (play_mode == 2u) {
		if ((position % ((uint32_t)len * 2u)) >= len)
			seq_trackPlayState[track] |= SEQ_PLAY_STATE_PIP_REV;
		else
			seq_trackPlayState[track] &= (uint8_t)~SEQ_PLAY_STATE_PIP_REV;
	}
	seq_lastMasterStep[track] = (uint8_t)seq_stepIndex[track];
	if (track == menu_getActiveVoice()) {
		seq_ledState.chaseStep = seq_stepIndex[track];
		seq_ledState.dirty |= SEQ_LED_DIRTY_CHASE;
	}
	return 1u;
}

```

**Modify** `seq_realignTrackToMasterClock()`.

**Remove:** l. 5059–5101, from `const pat_scene_region_t *region;` through the
closing brace of the chase-LED `if`. This includes the local declarations, the
range and once-mode guards, and the comment block at l. 5081–5085.

**Add in their place:**

```c
	/*
	 * Position first (S098 P1: shared with the Scene switch). A stopped
	 * once-mode track, or an invalid track, is left untouched as before.
	 */
	if (!seq_realignTrackPosition(track))
		return;
```

Lines 5102–5126 (the menu override, epoch, ramp, Scene epoch, echo mark and FX
realign) are unchanged.

## B. `Core/Sequencer/sequencer.h`

### B1. l. 339–350 — **modify** the comment and prototype of `seq_selectActivePattern()`

**Remove** l. 339–350. **Add:**

```c
/*
 * Select the active Scene, decided per track (S098 P1; user, Q2).
 *
 * Inputs: pattern, a resident Scene (or 16) validated against PatternData;
 * keep_mask, bit t set when track t and its voice slot are identical in the
 * Scene the track plays and in `pattern` (preset_sceneSwitchKeepMask();
 * tracks 5 and 6 are forced to agree); effect_same, the Effect record and
 * Effect Morph amount are identical.
 * Outputs: every track reads `pattern`. Changed tracks are reset, realigned
 * to the master clock, have their queued triggers dropped and their MIDI note
 * ended, and get the Colloid change mark. Kept tracks continue untouched. The
 * FX sequence is realigned only when the Effect changed. The LEDs and the
 * program change follow, and Scene 17 is released when left.
 * Client: menu_perfSceneChangeExecute(). The voice half is
 * preset_startSceneSwitchApply().
 */
void seq_selectActivePattern(uint8_t pattern, uint8_t keep_mask,
                             uint8_t effect_same);
```

## C. `Core/DSP/Instruments/InstrumentManager.c`

No forward declaration is needed. C1 does not restore descriptor adapters
(see its comment), and the two restore helpers it calls are already
forward-declared at l. 389–392.

### C1. l. 1983 — **add** after the closing brace of `instrumentManager_clearRuntimeModulationTargetsForSlot()`, before the comment block of `instrumentManager_resetRuntimeSlotFromScene()`

```c
/*
 * Does one installed supplemental binding touch a changing part? (S098 P1)
 *
 * What: 1 when the binding's target lies in a changed slot or, with
 * effect_changed, is the Effect (a parameter or `fxm`). A Scene target is
 * resolved through its descriptor's voice_slot; an unresolved one counts as
 * touching. Slot decimation is a canonical voice parameter id.
 * Inputs: the installed record, changed slot mask, effect_changed.
 * Output: 0/1.
 * Accessors: instrumentManager_prepareModulationForSlotChange().
 * Affiliates: installed_mod_target_t, sceneModTarget_descriptor(),
 * instrumentParam_slot().
 */
static uint8_t instrumentManager_installedTouches(
    const installed_mod_target_t *installed, uint8_t changed_slots,
    uint8_t effect_changed)
{
    switch (installed->kind) {
    case INSTALLED_MOD_TARGET_SLOT_DECIMATION: {
        uint8_t slot = instrumentParam_slot(installed->target_id);

        return (slot < INSTRUMENT_SLOT_COUNT)
            ? (uint8_t)((changed_slots >> slot) & 1u) : 1u; }
    case INSTALLED_MOD_TARGET_SCENE_TARGET: {
        const scene_mod_target_descriptor_t *descriptor =
            sceneModTarget_descriptor(installed->target_id);

        if (!descriptor)
            return 1u;
        switch (descriptor->kind) {
        case SCENE_MOD_TARGET_KIND_VOICE_MORPH:
        case SCENE_MOD_TARGET_KIND_AUDIO_OUT:
        case SCENE_MOD_TARGET_KIND_FX_SEND:
            return (descriptor->voice_slot < INSTRUMENT_SLOT_COUNT)
                ? (uint8_t)((changed_slots >> descriptor->voice_slot) & 1u)
                : 1u;
        case SCENE_MOD_TARGET_KIND_SLOT6_TRACK7_AMP_DECAY:
            return (uint8_t)((changed_slots >> (INSTRUMENT_SLOT_COUNT - 1u)) &
                             1u);
        case SCENE_MOD_TARGET_KIND_EFFECT_MORPH:
            return (uint8_t)(effect_changed != 0u);
        default:
            return 1u;
        } }
    case INSTALLED_MOD_TARGET_EFFECT:
        return (uint8_t)(effect_changed != 0u);
    default:
        return 0u;
    }
}

/*
 * Prepare the modulation graph for a Scene switch that changes some slots
 * (S098 P1).
 *
 * What: detaches only what the changing slots and Effect make stale, and
 * returns the source mask the rebind cursor must revisit once the changed
 * slots have committed:
 *  - a changed source slot: both LFO pairs and its velocity binding are
 *    detached exactly as instrumentManager_clearAllRuntimeModulationTargets()
 *    detaches every source today (supplemental restore, node clear);
 *  - a kept source: a pair or velocity binding is detached only when its
 *    supplemental target (decimation, Scene target, Effect) touches a changed
 *    slot or the changed Effect, or when its velocity node points directly
 *    into a changed slot (that raw pointer must not survive the slot's
 *    memset; the restore would write the old baseline into the new image);
 *  - a kept source whose descriptor LFO adapter targets a changed slot is not
 *    detached (adapters are not detached today either; the reinstall replaces
 *    them), but it joins the rebind mask so the adapter is re-resolved
 *    against the new image.
 * Kept sources whose bindings stay inside kept slots are not touched at all:
 * their LFO keeps modulating without a gap.
 * Why: user, S098 Q2. A switch must not be heard on the tracks it keeps.
 * The former all-source teardown detached Morph, Effect, decimation,
 * Scene-target and velocity modulation of all six voices until every slot
 * had committed (up to DRUMSET_APPLY_FORCE_TICKS, about 1 s).
 * Inputs: changed_slots (bit s = slot s changes; 6 bits), effect_changed.
 * Outputs: detached bindings; returns changed_slots | the kept sources to
 * rebind.
 * Context: foreground, before any changed slot is reset.
 * Accessors: preset_startSceneSwitchApply().
 * Affiliates: instrumentManager_clearAllRuntimeModulationTargets() (the
 * all-source form, still used by Load/boot/copy applies),
 * instrumentManager_installedTouches(), preset_tickInstrumentApply() (the
 * masked rebind).
 */
uint8_t instrumentManager_prepareModulationForSlotChange(uint8_t changed_slots,
                                                         uint8_t effect_changed)
{
    uint8_t source;
    uint8_t rebind = 0u;

    changed_slots = (uint8_t)(changed_slots &
                              (uint8_t)((1u << INSTRUMENT_SLOT_COUNT) - 1u));
    for (source = 0u; source < INSTRUMENT_SLOT_COUNT; source++) {
        const uint8_t bit = (uint8_t)(1u << source);
        const uint8_t own = (uint8_t)((changed_slots & bit) != 0u);
        Lfo *lfo = instrumentManager_runtimeLfo(source);
        const ModulationNode *velocity = &velocityModulators[source];
        uint8_t pair;
        uint8_t clear;

        for (pair = 0u; pair < 2u; pair++) {
            const instrument_lfo_target_adapter_t *adapter =
                &lfo_descriptor_targets[source][pair];

            clear = (uint8_t)(own ||
                instrumentManager_installedTouches(
                    &lfo_installed_targets[source][pair], changed_slots,
                    effect_changed));
            if (clear) {
                instrumentManager_restoreLfoSupplementalTarget(source, pair);
                if (lfo)
                    modNode_clearDestination(pair ? &lfo->modTarget2
                                                  : &lfo->modTarget);
                rebind = (uint8_t)(rebind | bit);
            }
            if (adapter->active && adapter->slot < INSTRUMENT_SLOT_COUNT &&
                (changed_slots & (uint8_t)(1u << adapter->slot)) != 0u)
                rebind = (uint8_t)(rebind | bit);
        }
        clear = (uint8_t)(own ||
            instrumentManager_installedTouches(
                &velocity_installed_targets[source], changed_slots,
                effect_changed));
        if (velocity->destinationMode == MOD_NODE_DEST_DIRECT_PARAMETER &&
            instrumentParam_isVoiceParameter(velocity->destination) &&
            instrumentParam_slot(velocity->destination) < INSTRUMENT_SLOT_COUNT &&
            (changed_slots &
             (uint8_t)(1u << instrumentParam_slot(velocity->destination))) != 0u)
            clear = 1u;
        if (clear) {
            instrumentManager_restoreVelocitySupplementalTarget(source);
            modNode_clearDestination(&velocityModulators[source]);
            rebind = (uint8_t)(rebind | bit);
        }
    }
    return (uint8_t)(rebind | changed_slots);
}
```

**Checked in the tree:**

- `instrumentManager_runtimeLfo()` is used by the all-source clear at l. 1932,
  above this point.
- `velocityModulators[]` is used at l. 1937.
- `lfo_descriptor_targets` and `lfo_installed_targets` are declared at
  l. 176–188.
- `sceneModTarget_descriptor()` comes from `SceneModTargets.h`, included at
  l. 10.
- `MOD_NODE_DEST_DIRECT_PARAMETER` comes from `modulationNode.h`, included at
  l. 26.
- `instrumentParam_isVoiceParameter()` is used at l. 3497.
- The restore helpers are forward-declared at l. 389–392.

## D. `Core/DSP/Instruments/InstrumentManager.h`

### D1. l. 754 — **add** after `void instrumentManager_clearRuntimeModulationTargetsForSlot(uint8_t slot);`

```c
/*
 * Prepare the modulation graph for a Scene switch that changes some slots
 * (S098 P1).
 *
 * Inputs: changed_slots (bit s = slot s is replaced), effect_changed.
 * Output: the bindings the change makes stale are detached; returns the
 * source mask the Scene worker's rebind must revisit (the changed slots plus
 * every kept source whose binding points into one of them, or into the
 * changed Effect). Kept sources with bindings inside kept slots are left
 * running. Client: preset_startSceneSwitchApply().
 */
uint8_t instrumentManager_prepareModulationForSlotChange(uint8_t changed_slots,
                                                         uint8_t effect_changed);
```

## E. `Core/Bank/Scene/Preset/presetMorphEngine.c`

### E1. l. 358 — **add** after the closing brace of `presetMorph_requestVoice()`

```c
/*
 * Move the Morph worker to the Scene a switch selects (S098 P1).
 *
 * What: when the worker is working for `from`, it now works for `to`. The
 * request and pass bits of the kept slots stay, and the changed slots are
 * removed from both. If the interrupted priority slot is a changed slot,
 * the worker resumes the pass where it was interrupted.
 * Why: presetMorph_writeRuntimeBaseEx() refuses writes for a Scene that is
 * not active. Without the retarget, a pass running at the switch (the last
 * pass of a Morph sweep, for example) would never reach the runtime of a kept
 * voice. `to` holds the same endpoints and amounts for a kept slot, so the
 * continued pass writes exactly what it would have written. A changed slot
 * must not be finished with the outgoing pass amount over the new endpoints;
 * preset_startSceneSwitchApply() requests it afresh for `to`.
 * Inputs: from (the Scene playing before the switch), to, keep_slots
 * (bit s = slot s identical).
 * Outputs: morph_worker.scene_index and masks.
 * Context: foreground, before the switch; no worker tick runs in between.
 * Accessors: preset_prepareSceneSwitch().
 * Affiliates: presetMorph_requestVoice() (which restarts the worker when the
 * Scene differs), presetMorph_tick().
 */
void presetMorph_retargetScene(uint8_t from, uint8_t to, uint8_t keep_slots)
{
    uint8_t drop;

    if (morph_worker.scene_index != from || !scene_getConst(to))
        return;
    drop = (uint8_t)(~keep_slots & PRESET_MORPH_ALL_SLOTS_MASK);
    morph_worker.scene_index = to;
    morph_worker.requested_mask &= (uint8_t)~drop;
    morph_worker.pass_mask &= (uint8_t)~drop;
    morph_worker.pass_lfo_resolved_mask &= (uint8_t)~drop;
    if (morph_worker.priority_resume_valid &&
        (drop & PRESET_MORPH_SLOT_MASK(morph_worker.priority_slot)) != 0u) {
        morph_worker.slot = morph_worker.resume_slot;
        morph_worker.descriptor_index = morph_worker.resume_descriptor_index;
        morph_worker.priority_resume_valid = 0u;
    }
}
```

`presetMorph_tick()` skips a slot whose `pass_mask` bit is clear (its outer
`while`, l. ≈ 545). When the pass mask empties, the tick starts the next pass or
goes idle. So nothing else is needed.

## F. `Core/Bank/Scene/Preset/presetMorphEngine.h`

### F1. l. 59 — **add** after `void presetMorph_rebuildScene(uint8_t scene_index);`

```c
/*
 * Retarget the Morph worker across a Scene switch (S098 P1): a worker busy
 * for `from` continues for `to` with only the kept slots' work. Client:
 * preset_prepareSceneSwitch().
 */
void presetMorph_retargetScene(uint8_t from, uint8_t to, uint8_t keep_slots);
```

## G. `Core/Bank/Scene/Preset/presetManager.c`

### G1. l. 249 and l. 268 — **add** two statics and one macro

After l. 249 (`static uint16_t drumset_apply_pending_mask = 0u;`):

```c
/*
 * All six instrument slots as a mask (S098 P1).
 */
#define PM_ALL_SLOTS ((uint8_t)((1u << INSTRUMENT_SLOT_COUNT) - 1u))
/*
 * Sources the Scene worker rebinds when it finishes (S098 P1).
 *
 * What: bit s set = source slot s is revisited by the rebind cursor once
 * every pending slot of this worker has committed.
 * Why: a switch that keeps some slots must not reinstall their untouched
 * bindings. A reinstall restores the target to its base for a block and
 * clears the source's Morph and Effect LFO contributions.
 * preset_startDrumsetApply() (Load, boot, copy/clear) sets all six, as
 * before; preset_startSceneSwitchApply() sets only what
 * instrumentManager_prepareModulationForSlotChange() reported.
 * RAM: +1 B SRAM1 (approved ______). Owner: Preset.
 * Accessors: preset_startDrumsetApply(), preset_startSceneSwitchApply(),
 * preset_tickDrumsetApply(), preset_applyDeferredSceneSlotForTrigger().
 */
static uint8_t drumset_apply_rebind_mask = 0u;
```

After l. 268 (`static uint8_t instrument_apply_rebind_source = 0u;`):

```c
/*
 * Rebind cursor source mask (S098 P1).
 *
 * What: the rebind phase visits only the source slots set here. Every
 * Instrument apply that reaches the rebind through the Morph phase sets all
 * six; a Scene-worker rebind sets drumset_apply_rebind_mask.
 * RAM: +1 B SRAM1 (approved ______). Owner: Preset.
 * Accessors: preset_startSceneModulationRebind(), preset_tickInstrumentApply().
 */
static uint8_t instrument_apply_rebind_mask = PM_ALL_SLOTS;
```

### G2. l. 1913–1930 — **modify** `preset_startSceneModulationRebind()` to take the source mask

**Remove** l. 1923–1930. **Add:**

```c
static void preset_startSceneModulationRebind(uint8_t scene_index,
                                              uint8_t source_mask)
{
    /*
     * S098 P1: only the sources in source_mask are revisited. Load, boot and
     * copy/clear pass all six (through drumset_apply_rebind_mask), so their
     * behaviour is unchanged; a Scene switch passes the sources whose
     * bindings it detached or whose targets it replaced.
     */
    instrument_apply_active = 1u;
    instrument_apply_scene = scene_index;
    instrument_apply_phase = INSTRUMENT_APPLY_PHASE_TARGET_REBIND;
    instrument_apply_rebind_source = 0u;
    instrument_apply_rebind_mask = (uint8_t)(source_mask & PM_ALL_SLOTS);
    instrument_apply_morph_only = 0u;
}
```

Keep the comment block at l. 1913–1922. Append one line to it, before the
closing `*/`:

```c
 * S098 P1: source_mask limits the cursor to the sources a switch touched.
```

### G3. l. 2013–2014 — **modify** `preset_startDrumsetApply()`: also arm the rebind of all sources

After l. 2014 (`(uint16_t)((1u << INSTRUMENT_SLOT_COUNT) - 1u);`), **add:**

```c
    /* S098 P1: a full Scene apply rebinds every source, as before. */
    drumset_apply_rebind_mask = PM_ALL_SLOTS;
```

### G4. l. 2285 and l. 2348 — **modify** the two rebind starts

`preset_tickDrumsetApply()`, l. 2285, and
`preset_applyDeferredSceneSlotForTrigger()`, l. 2348. Replace
`preset_startSceneModulationRebind(drumset_apply_scene);` at both sites with:

```c
            preset_startSceneModulationRebind(drumset_apply_scene,
                                              drumset_apply_rebind_mask);
```

At l. 2348 the indentation is 8 spaces, not 12.

### G5. l. 3064–3080 — **modify** `preset_tickInstrumentApply()`: the masked cursor

After l. 3065 (`instrument_apply_rebind_source = 0u;` inside the Morph→rebind
transition), **add:**

```c
        /* S098 P1: an Instrument/Kit apply rebuilds every source. */
        instrument_apply_rebind_mask = PM_ALL_SLOTS;
```

Before l. 3068 (`if (instrument_apply_rebind_source < INSTRUMENT_SLOT_COUNT) {`),
**add:**

```c
    /*
     * Skip the sources this rebind does not own (S098 P1).
     *
     * What: advances the cursor past every source whose bit is clear in
     * instrument_apply_rebind_mask. Why: a Scene switch rebinds only the
     * sources whose bindings it detached or retargeted; reinstalling an
     * untouched source would restore its target to base for a block and
     * clear its Morph/Effect LFO contributions on a voice the switch kept.
     * Inputs: instrument_apply_rebind_mask (all six for every other client).
     */
    while (instrument_apply_rebind_source < INSTRUMENT_SLOT_COUNT &&
           (instrument_apply_rebind_mask &
            (uint8_t)(1u << instrument_apply_rebind_source)) == 0u)
        instrument_apply_rebind_source++;
```

### G6. l. 1348 — **add** after the closing brace of `preset_clearFxSendStepOverride()`

```c
/*
 * Clear one slot's step-automation overlays for a Scene switch (S098 P1).
 *
 * What: deactivates the slot's audio-out, FX-send and Morph step overrides
 * and, for slot 6, the generated track-7 decay override. Nothing is
 * re-applied here: the Scene worker commits the slot's routing and image
 * from the new Scene at its quiet point or trigger, and the mixer ramps the
 * send to the retained value.
 * Why: a switch replaces only the changed slots. Their overlays describe
 * the outgoing Scene's automation; a kept slot's overlays belong to
 * automation that keeps playing. The former whole-Scene clear in
 * preset_applySceneSettings() cleared them for all six slots.
 * Inputs: slot 0..5. Outputs: the four overlay records of that slot.
 * Accessors: preset_startSceneSwitchApply().
 * Affiliates: preset_clearAllAudioOutStepOverrides(),
 * preset_clearAllFxSendStepOverrides(),
 * presetMorph_clearStepAutomationOverride(),
 * instrumentManager_clearSlot6Track7StepDecayOverride().
 */
static void preset_clearSlotStepOverrides(uint8_t slot)
{
    if (slot >= INSTRUMENT_SLOT_COUNT)
        return;
    audio_out_step_override[slot].active = 0u;
    audio_out_step_override[slot].route = 0u;
    fx_send_step_override[slot].active = 0u;
    fx_send_step_override[slot].amount = 0u;
    presetMorph_clearStepAutomationOverride(slot);
    if (slot == INSTRUMENT_SLOT_COUNT - 1u)
        instrumentManager_clearSlot6Track7StepDecayOverride();
}
```

### G7. l. 3369 — **add** the shared field lists, the identity test and the switch apply

Insert at l. 3369, the blank line after the closing brace at l. 3368 and before
the comment block `Empty one Scene 17 track before a rebuild` (l. 3370). The
copy functions that follow use the lists (G8).

```c
/* =======================================================================
 * Scene switch continuity and the Scene 17 copy share one field list
 * (S098 P1; S082 D4)
 * ======================================================================= */

/*
 * The fields that make a track and a voice slot sound the way they do.
 *
 * What: X-macro lists of the per-track Pattern fields, the per-track bits,
 * the per-track Scene settings, the per-slot Scene settings and the slot-6
 * Kit settings. The Scene 17 build copies exactly these
 * (preset_copyBackgroundTrack(), preset_copyBackgroundSlot()), and the
 * Scene switch compares exactly these (preset_trackMatches(),
 * preset_slotMatches()).
 * Why: a switch may leave a track running only when the incoming track
 * sounds identical. Scene 17 makes the same promise when it copies a track
 * (D4). One list keeps the two from drifting: a field added to one is
 * added to both. The sizeof guards below fire when one of the three
 * structs gains a field, so the new field gets classified.
 * Not listed, on purpose:
 *  - chain_condition/chain_next: what plays next (Scene 17 drops them, D9b);
 *  - morph_amount: the PERF bulk-set mirror, not used by the runtime;
 *  - bus_comp: read live from the active Scene by BusCompressor.c;
 *  - pattern_change_bar/pattern_next;
 *  - morph_interpolation[]: derived, see preset_instrumentImageMatches().
 * Affiliates: the copy and match functions below.
 */
#define PRESET_TRACK_REGION_FIELDS(X) \
    X(track_length) X(track_scale) X(track_shuffle) X(track_play_mode)
#define PRESET_TRACK_REGION_BITS(X) \
    X(track_loop_exclude) X(track_auto_smooth)
#define PRESET_TRACK_SETTINGS_FIELDS(X) \
    X(midi_channel) X(midi_note) \
    X(track_morph_length) X(track_morph_scale) X(track_morph_shuffle)
#define PRESET_SLOT_SETTINGS_FIELDS(X) \
    X(voice_morph_amount) X(audio_out) X(fx_send_amount) X(fx_send_morph) \
    X(fader_setting)
#define PRESET_SLOT6_KIT_FIELDS(X) \
    X(slot6_track7_amp_envelope_decay) \
    X(slot6_track7_morph_amp_envelope_decay)

_Static_assert(sizeof(scene_settings_t) == 75u,
               "scene_settings_t changed: classify the new field in the S098 "
               "field lists (presetManager.c), then update 75");
_Static_assert(sizeof(kit_settings_t) == 2u,
               "kit_settings_t changed: classify the new field in the S098 "
               "field lists (presetManager.c), then update 2");
_Static_assert(sizeof(pat_scene_region_t) ==
               (PAT_STEPS_PER_SCENE * 2u) + (PAT_STACK_SIZE * 32u) +
               512u + 32u,
               "pat_scene_region_t changed: classify the new field in the "
               "S098 field lists (presetManager.c)");

/*
 * Compare one track's steps in two Scenes by content (S098 P1).
 *
 * What: for every step, the trigger bit and the block bytes read through
 * pat_rawReadBlock() must be equal. Pool offsets and the specials bit's
 * placement are not compared.
 * Why: two identical Patterns can hold their blocks at different offsets
 * (copy/clear, compaction, a Load into a fuller region), so raw address
 * entries can differ while the music is the same. A false "different" is
 * safe (the track then changes as it always did); a false "identical" is
 * not, so every byte of every block is compared.
 * Inputs: track 0..6, two Scene indices (0..16).
 * Output: 1 when identical.
 * Cost: at most 2 x 128 block reads of at most 136 B; empty steps copy
 * nothing. Foreground, once per executed Scene change.
 * Accessors: preset_trackMatches(); host test scene_identity.
 * Affiliates: pat_rawReadBlock(), preset_copyBackgroundTrack() (the copy
 * with the same per-step rule).
 */
static uint8_t preset_trackStepsMatch(uint8_t track, uint8_t scene_a,
                                      uint8_t scene_b)
{
    uint8_t block_a[PAT_RAW_BLOCK_MAX];
    uint8_t block_b[PAT_RAW_BLOCK_MAX];
    uint16_t entry_a;
    uint16_t entry_b;
    uint8_t bytes_a;
    uint8_t bytes_b;
    uint8_t step;

    for (step = 0u; step < NUM_STEPS; step++) {
        bytes_a = pat_rawReadBlock(scene_a, track, step, block_a, &entry_a);
        bytes_b = pat_rawReadBlock(scene_b, track, step, block_b, &entry_b);
        if ((entry_a & PAT_ADDR_TRIGGER_BIT) != (entry_b & PAT_ADDR_TRIGGER_BIT))
            return 0u;
        if (bytes_a != bytes_b)
            return 0u;
        if (bytes_a != 0u && memcmp(block_a, block_b, bytes_a) != 0)
            return 0u;
    }
    return 1u;
}

/*
 * Compare two Kit slots' instrument images (S098 P1).
 *
 * What: the type and the Normal and Morph endpoint images must be equal.
 * morph_interpolation[] is not compared.
 * Why: morph_interpolation[] is a derived runtime image. For the playing
 * Scene it holds the resolved amount including a step override or the LFO
 * Morph layer (presetMorph_getResolvedVoiceAmount()), so it differs from an
 * idle copy while the music is the same. The switch carries it over for a
 * kept slot (preset_prepareSceneSwitch()).
 * Inputs: two slot images. Output: 1 when identical.
 * Accessors: preset_slotMatches(); host test scene_identity.
 */
static uint8_t preset_instrumentImageMatches(const kit_instrument_slot_t *a,
                                             const kit_instrument_slot_t *b)
{
    return (uint8_t)(a->type == b->type &&
        memcmp(a->parameter_images.instrument_parameters,
               b->parameter_images.instrument_parameters,
               sizeof(a->parameter_images.instrument_parameters)) == 0 &&
        memcmp(a->parameter_images.morph_instrument_parameters,
               b->parameter_images.morph_instrument_parameters,
               sizeof(a->parameter_images.morph_instrument_parameters)) == 0);
}

/*
 * Is a track identical in two Scenes? (S098 P1)
 *
 * What: the per-track Pattern fields and bits, the per-track Scene settings
 * and every step (preset_trackStepsMatch()). A Scene compared with itself is
 * identical.
 * Inputs: track 0..6, two Scene indices. Output: 1 when identical.
 * Accessors: preset_sceneSwitchKeepMask().
 */
static uint8_t preset_trackMatches(uint8_t track, uint8_t scene_a,
                                   uint8_t scene_b)
{
    const pat_scene_region_t *ra = pat_sceneRegion(scene_a);
    const pat_scene_region_t *rb = pat_sceneRegion(scene_b);
    const scene_t *sa = scene_getConst(scene_a);
    const scene_t *sb = scene_getConst(scene_b);
    const uint8_t bit = (uint8_t)(1u << track);

    if (!ra || !rb || !sa || !sb || track >= NUM_TRACKS)
        return 0u;
    if (scene_a == scene_b)
        return 1u;
#define PM_MATCH_TRACK_REGION(f) if (ra->f[track] != rb->f[track]) return 0u;
    PRESET_TRACK_REGION_FIELDS(PM_MATCH_TRACK_REGION)
#undef PM_MATCH_TRACK_REGION
#define PM_MATCH_TRACK_BIT(f) if (((ra->f ^ rb->f) & bit) != 0u) return 0u;
    PRESET_TRACK_REGION_BITS(PM_MATCH_TRACK_BIT)
#undef PM_MATCH_TRACK_BIT
#define PM_MATCH_TRACK_SETTING(f) \
    if (sa->settings.f[track] != sb->settings.f[track]) return 0u;
    PRESET_TRACK_SETTINGS_FIELDS(PM_MATCH_TRACK_SETTING)
#undef PM_MATCH_TRACK_SETTING
    return preset_trackStepsMatch(track, scene_a, scene_b);
}

/*
 * Is a voice slot identical in two Scenes? (S098 P1)
 *
 * What: the instrument image (preset_instrumentImageMatches()), the per-slot
 * Scene settings and, for slot 6, the generated track-7 decay endpoints.
 * Inputs: slot 0..5, two Scene indices. Output: 1 when identical.
 * Accessors: preset_sceneSwitchKeepMask().
 */
static uint8_t preset_slotMatches(uint8_t slot, uint8_t scene_a,
                                  uint8_t scene_b)
{
    const scene_t *sa = scene_getConst(scene_a);
    const scene_t *sb = scene_getConst(scene_b);

    if (!sa || !sb || slot >= INSTRUMENT_SLOT_COUNT)
        return 0u;
    if (scene_a == scene_b)
        return 1u;
    if (!preset_instrumentImageMatches(&sa->kit.instruments[slot],
                                       &sb->kit.instruments[slot]))
        return 0u;
#define PM_MATCH_SLOT_SETTING(f) \
    if (sa->settings.f[slot] != sb->settings.f[slot]) return 0u;
    PRESET_SLOT_SETTINGS_FIELDS(PM_MATCH_SLOT_SETTING)
#undef PM_MATCH_SLOT_SETTING
    if (slot == INSTRUMENT_SLOT_COUNT - 1u) {
#define PM_MATCH_SLOT6_KIT(f) \
        if (sa->kit.settings.f != sb->kit.settings.f) return 0u;
        PRESET_SLOT6_KIT_FIELDS(PM_MATCH_SLOT6_KIT)
#undef PM_MATCH_SLOT6_KIT
    }
    return 1u;
}

/*
 * Is the Effect identical in two Scenes? (S098 P1)
 *
 * What: the whole Effect record (type, sequence settings, Normal and Morph
 * values, FX sequence steps) and the Scene's Effect Morph amount.
 * Why: an identical Effect is rebound without a reset
 * (effects_rebindSourceScene()), so its tails, FX clock, step and locks
 * continue. Records are only ever copied or zero-initialised whole, so a
 * byte compare is exact; any padding difference can only report
 * "different", which is safe.
 * Inputs: two Scene indices. Output: 1 when identical.
 * Accessors: preset_sceneSwitchKeepMask().
 */
static uint8_t preset_effectMatches(uint8_t scene_a, uint8_t scene_b)
{
    const scene_t *sa = scene_getConst(scene_a);
    const scene_t *sb = scene_getConst(scene_b);

    if (!sa || !sb)
        return 0u;
    if (scene_a == scene_b)
        return 1u;
    return (uint8_t)(sa->settings.effect_morph_amount ==
                         sb->settings.effect_morph_amount &&
                     memcmp(&sa->effect, &sb->effect, sizeof(sa->effect)) == 0);
}

/*
 * Slots whose tracks are all kept (S098 P1).
 *
 * What: slots 0..4 follow tracks 0..4; slot 5 is kept only when tracks 5 and
 * 6 (which share it) are both kept.
 * Inputs: track keep mask. Output: slot keep mask (6 bits).
 */
static uint8_t preset_keptSlotsOfTracks(uint8_t keep_tracks)
{
    uint8_t slots = (uint8_t)(keep_tracks & 0x1Fu);

    if ((keep_tracks & 0x60u) == 0x60u)
        slots = (uint8_t)(slots | 0x20u);
    return slots;
}

/*
 * Decide what a switch to `target` keeps (S098 P1; user, Q2).
 *
 * What: bit t of the result is set when track t, and the voice slot it
 * plays, are identical in the Scene the track plays now
 * (seq_getTrackPlayedScene()) and in `target`. Tracks 5 and 6 are kept only
 * together. *effect_same reports whether the active Scene's Effect equals
 * `target`'s. Scene 17 counts like any other played Scene.
 * Why: target T1. The switch runs the same procedure for every track and
 * changes only what differs: an identical Scene changes nothing audible; a
 * Scene with one edited track changes that track.
 * Inputs: target (0..15), the played Scenes, the active Scene.
 * Output: keep mask (7 bits); *effect_same (may be NULL).
 * DEV trace: one 'x' record per call with the result and the compare time.
 * Context: foreground, before any switch state moves.
 * Accessors: menu_perfSceneChangeExecute().
 * Affiliates: preset_trackMatches(), preset_slotMatches(),
 * preset_effectMatches(), seq_selectActivePattern(),
 * preset_startSceneSwitchApply().
 */
uint8_t preset_sceneSwitchKeepMask(uint8_t target, uint8_t *effect_same)
{
    uint8_t active = scene_getActiveIndex();
    uint8_t same;
    uint8_t keep = 0u;
    uint8_t track;
#if DEV_MODE_LOGGING
    uint32_t start_us = timebase_tim2Now();
#endif

    if (!scene_getConst(target) || !pat_patternValid(target)) {
        if (effect_same)
            *effect_same = 0u;
        return 0u;
    }
    same = preset_effectMatches(active, target);
    for (track = 0u; track < NUM_TRACKS; track++) {
        uint8_t played = seq_getTrackPlayedScene(track);
        uint8_t slot = (track >= INSTRUMENT_SLOT_COUNT)
            ? (uint8_t)(INSTRUMENT_SLOT_COUNT - 1u) : track;

        if (preset_trackMatches(track, played, target) &&
            preset_slotMatches(slot, played, target))
            keep = (uint8_t)(keep | (uint8_t)(1u << track));
    }
    if ((keep & 0x60u) != 0x60u)
        keep = (uint8_t)(keep & (uint8_t)~0x60u);
    if (effect_same)
        *effect_same = same;
#if DEV_MODE_LOGGING
    {
        uint32_t us = timebase_tim2Delta(timebase_tim2Now(), start_us);

        autosaveTrace_record(AUTOSAVE_TRACE_STAGE_SCENE_SWITCH,
                             (uint8_t)(keep | (same ? 0x80u : 0u)),
                             (uint32_t)target | ((uint32_t)active << 8u) |
                             ((us > 0xFFFFu ? 0xFFFFu : us) << 16u));
    }
#endif
    return keep;
}

/*
 * Carry the derived Scene state of the kept slots into `target`
 * (S098 P1).
 *
 * What: for each kept slot, copies morph_interpolation[] from the Scene its
 * track plays into `target`, then moves the Morph worker from the active
 * Scene to `target` (presetMorph_retargetScene()), keeping only the kept
 * slots' work.
 * Why: morph_interpolation[] is the only Scene-indexed state that differs
 * between two identical Scenes (it holds the playing Scene's resolved Morph,
 * including step overrides and the LFO Morph layer). The next resetting
 * trigger of a kept voice restores its base from the played Scene's image
 * (seq_restoreAutomatedParameters()). Without the carry, that restore would
 * read `target`'s idle image and step the sound. This is the whole handoff
 * a kept voice needs (S098_P0 §3); its runtime state is never touched.
 * Inputs: target, keep_tracks (from preset_sceneSwitchKeepMask()).
 * Outputs: target's morph_interpolation[] for kept slots; the Morph worker.
 * The image is not persisted, so nothing is marked dirty.
 * Context: foreground, immediately before scene_selectActive(target).
 * Accessors: menu_perfSceneChangeExecute().
 * Affiliates: presetMorph_retargetScene(), preset_keptSlotsOfTracks().
 */
void preset_prepareSceneSwitch(uint8_t target, uint8_t keep_tracks)
{
    scene_t *dst = scene_get(target);
    uint8_t keep_slots = preset_keptSlotsOfTracks(keep_tracks);
    uint8_t slot;

    if (!dst)
        return;
    for (slot = 0u; slot < INSTRUMENT_SLOT_COUNT; slot++) {
        const scene_t *src;

        if ((keep_slots & (uint8_t)(1u << slot)) == 0u)
            continue;
        src = scene_getConst(seq_getTrackPlayedScene(slot));
        if (!src || src == dst)
            continue;
        memcpy(dst->kit.instruments[slot].parameter_images.morph_interpolation,
               src->kit.instruments[slot].parameter_images.morph_interpolation,
               sizeof(dst->kit.instruments[slot].parameter_images.
                      morph_interpolation));
    }
    presetMorph_retargetScene(scene_getActiveIndex(), target, keep_slots);
}

/*
 * Apply a Scene switch to the voices, slot by slot (S098 P1; user, Q2).
 *
 * What: the voice half of the switch, after seq_selectActivePattern().
 *  - Changed slots (a slot whose track is not kept): the LFO phase is
 *    captured (S076 P2), the step-automation overlays are cleared, the
 *    Morph pass for the new Scene is requested, and the drumset worker
 *    commits the slot from the new Scene at its quiet point, at its next
 *    trigger, or after DRUMSET_APPLY_FORCE_TICKS. That is what every slot
 *    got from preset_startDrumsetApply() before.
 *  - The modulation graph: only the bindings the change makes stale are
 *    detached (instrumentManager_prepareModulationForSlotChange()), and the
 *    rebind at the end revisits only those sources.
 *  - Kept slots: nothing. Their runtime image, envelopes, phases, filter
 *    states, Colloid gate, sustain, glide and intervals, Tetrakt layers and
 *    S1 line, LFO phase, bindings and step overlays continue.
 *  - Effect: an identical Effect is rebound to the new Scene without a reset
 *    (effects_rebindSourceScene(), as for the Scene 17 switch); a changed
 *    one is activated as before (same type keeps its tails).
 *  - The PERF mirrors (Morph, bus compressor, Effect Morph) follow the new
 *    Scene; they only write parameter_values[].
 *  - An unfinished earlier worker is carried, not dropped: its pending
 *    slots and rebind sources join the new worker, which commits them from
 *    the new Scene. A kept slot's content is identical there; a changed
 *    one's is the new content anyway.
 * Why: target T1. preset_startDrumsetApply() reset every slot on every
 * Scene change (P1-P5 in S098_P0 §2.2). The rule is the same however many
 * slots are identical.
 * Inputs: keep_tracks, effect_same (from preset_sceneSwitchKeepMask()), the
 * active Scene (already the new one).
 * Outputs: drumset/instrument worker state, overlays, Effect binding,
 * mirrors.
 * Context: foreground. Accessors: menu_perfSceneChangeExecute().
 * Affiliates: preset_tickDrumsetApply(),
 * preset_applyDeferredSceneSlotForTrigger(), preset_startDrumsetApply()
 * (the full apply still used by Loads, boot and copy/clear).
 */
void preset_startSceneSwitchApply(uint8_t keep_tracks, uint8_t effect_same)
{
    const uint8_t scene_index = scene_getActiveIndex();
    const scene_t *scene = scene_getConst(scene_index);
    const uint8_t changed =
        (uint8_t)(~preset_keptSlotsOfTracks(keep_tracks) & PM_ALL_SLOTS);
    uint8_t carry_pending = 0u;
    uint8_t carry_rebind = 0u;
    uint8_t rebind;
    uint8_t slot;

    preset_ensureMorphInitialized();
    if (drumset_apply_active) {
        carry_pending = (uint8_t)(drumset_apply_pending_mask & PM_ALL_SLOTS);
        carry_rebind = drumset_apply_rebind_mask;
        if (instrument_apply_active &&
            instrument_apply_phase == INSTRUMENT_APPLY_PHASE_TARGET_REBIND)
            carry_rebind = (uint8_t)(carry_rebind |
                                     instrument_apply_rebind_mask);
    }
    for (slot = 0u; slot < INSTRUMENT_SLOT_COUNT; slot++) {
        if ((changed & (uint8_t)(1u << slot)) == 0u)
            continue;
        instrumentManager_captureLfoPhaseForSlot(slot);
        preset_clearSlotStepOverrides(slot);
    }
    rebind = instrumentManager_prepareModulationForSlotChange(
        changed, (uint8_t)(effect_same == 0u));
    if (scene)
        preset_syncSceneMorphMirrors(scene);
    preset_syncBusCompMirrors();
    preset_syncEffectMorphMirror();
    for (slot = 0u; slot < INSTRUMENT_SLOT_COUNT; slot++)
        if ((changed & (uint8_t)(1u << slot)) != 0u)
            presetMorph_requestVoice(scene_index, slot);
    if (!effect_same || !effects_rebindSourceScene(scene_index))
        effects_activateScene(scene_index);
    /* Changed slots are owned by the Scene worker from here; a per-track
     * single-voice commit for a kept slot stays pending (it now reads the
     * same content from the new Scene). */
    single_voice_pending_mask =
        (uint8_t)(single_voice_pending_mask & (uint8_t)~changed);
    drumset_apply_scene = scene_index;
    drumset_apply_pending_mask = (uint16_t)(carry_pending | changed);
    drumset_apply_rebind_mask = (uint8_t)(carry_rebind | rebind);
    drumset_apply_stall_ticks = 0u;
    if (drumset_apply_pending_mask != 0u) {
        drumset_apply_active = 1u;
        drumset_apply_voice = 0u;
    } else if (drumset_apply_rebind_mask != 0u) {
        /* Only bindings changed (a changed Effect): rebind now. */
        drumset_apply_active = 1u;
        preset_startSceneModulationRebind(scene_index,
                                          drumset_apply_rebind_mask);
    } else {
        drumset_apply_active = 0u;
    }
}
```

### G8. l. 3447–3464 and l. 3492–3505 — **modify** the Scene 17 copy functions to use the shared lists

In `preset_copyBackgroundTrack()`: **remove** l. 3447–3464 (from
`dst->track_length[track] = src->track_length[track];` through
`src_scene_data->settings.track_morph_shuffle[track];`). **Add:**

```c
    /*
     * The per-track fields Scene 17 copies are the fields a Scene switch
     * compares (S098 P1): one list, PRESET_TRACK_* above.
     */
#define PM_COPY_TRACK_REGION(f) dst->f[track] = src->f[track];
    PRESET_TRACK_REGION_FIELDS(PM_COPY_TRACK_REGION)
#undef PM_COPY_TRACK_REGION
#define PM_COPY_TRACK_BIT(f) \
    dst->f = (uint8_t)((dst->f & ~bit) | (src->f & bit));
    PRESET_TRACK_REGION_BITS(PM_COPY_TRACK_BIT)
#undef PM_COPY_TRACK_BIT
#define PM_COPY_TRACK_SETTING(f) \
    dst_scene->settings.f[track] = src_scene_data->settings.f[track];
    PRESET_TRACK_SETTINGS_FIELDS(PM_COPY_TRACK_SETTING)
#undef PM_COPY_TRACK_SETTING
```

In `preset_copyBackgroundSlot()`: **remove** l. 3492–3506 (from
`dst->settings.voice_morph_amount[dst_slot] =` through the closing brace of the
`if (dst_slot == INSTRUMENT_SLOT_COUNT - 1u)` block). L. 3491, the whole
instrument copy, stays; Scene 17 needs `morph_interpolation[]` too. **Add:**

```c
    /*
     * The per-slot fields Scene 17 copies are the fields a Scene switch
     * compares (S098 P1): one list, PRESET_SLOT_* above.
     */
#define PM_COPY_SLOT_SETTING(f) \
    dst->settings.f[dst_slot] = src->settings.f[src_slot];
    PRESET_SLOT_SETTINGS_FIELDS(PM_COPY_SLOT_SETTING)
#undef PM_COPY_SLOT_SETTING
    if (dst_slot == INSTRUMENT_SLOT_COUNT - 1u) {
#define PM_COPY_SLOT6_KIT(f) dst->kit.settings.f = src->kit.settings.f;
        PRESET_SLOT6_KIT_FIELDS(PM_COPY_SLOT6_KIT)
#undef PM_COPY_SLOT6_KIT
    }
```

Both copies are field-for-field what they were. The gate's "identical
behaviour" check applies.

## H. `Core/Bank/Scene/Preset/presetManager.h`

### H1. l. 587 — **add** after `void    preset_startLoadApply(uint16_t scene_mask);`

```c
/*
 * Scene switch, decided per track (S098 P1; user, Q2).
 *
 * preset_sceneSwitchKeepMask(target, &effect_same): bit t set when track t
 *   and its slot are identical in the Scene the track plays and in target
 *   (tracks 5 and 6 together); *effect_same when the Effect is identical.
 *   Runs before the switch. DEV: one 'x' trace record.
 * preset_prepareSceneSwitch(target, keep): carries morph_interpolation[] of
 *   the kept slots into target and retargets the Morph worker. Runs before
 *   scene_selectActive(target).
 * preset_startSceneSwitchApply(keep, effect_same): the voice half after
 *   seq_selectActivePattern(): changed slots through the Scene worker,
 *   modulation prepared and rebound only where it touches a change, the
 *   Effect rebound or activated, mirrors synced.
 * Client: menu_perfSceneChangeExecute().
 */
uint8_t preset_sceneSwitchKeepMask(uint8_t target, uint8_t *effect_same);
void    preset_prepareSceneSwitch(uint8_t target, uint8_t keep_tracks);
void    preset_startSceneSwitchApply(uint8_t keep_tracks, uint8_t effect_same);
```

## I. `Core/Menu/menu.c` — the executor

### I1. l. 10334–10374 — **modify** `menu_perfSceneChangeExecute()`

After l. 10334 (`{`), **add** the locals:

```c
    uint8_t keep;
    uint8_t effect_same;
```

**Replace** l. 10342 (`scene_selectActive(scene_index);`) with:

```c
    /*
     * Decide per track what the switch keeps, then carry the kept slots'
     * derived state (S098 P1; user, Q2).
     *
     * What: preset_sceneSwitchKeepMask() compares each track and its voice
     * slot in the Scene it plays and in scene_index, and the Effect.
     * preset_prepareSceneSwitch() copies the kept slots' Morph image into
     * scene_index and retargets the Morph worker. Both run before any switch
     * state moves, because they read the outgoing Scenes.
     * Why: target T1. An instant switch to an identical Scene is inaudible,
     * whenever it happens. A partly identical Scene changes only what
     * differs. One procedure serves PERF, MIDI PC, deferred, queued and
     * chain-fired changes, which all arrive here.
     * Affiliates: seq_selectActivePattern(), preset_startSceneSwitchApply().
     */
    keep = preset_sceneSwitchKeepMask(scene_index, &effect_same);
    preset_prepareSceneSwitch(scene_index, keep);
    scene_selectActive(scene_index);
```

**Replace** l. 10353 (`seq_selectActivePattern(scene_index);`) with:

```c
    seq_selectActivePattern(scene_index, keep, effect_same);
```

**Replace** l. 10374 (`preset_startDrumsetApply();`) with:

```c
    /*
     * The voice half of the switch (S098 P1): only the changed slots go
     * through the Scene worker; kept voices continue. With `restart` (pcr,
     * Q1) the restart above already ran; it moves every cursor but applies
     * nothing to the kept voices.
     * Affiliates: preset_startSceneSwitchApply().
     */
    preset_startSceneSwitchApply(keep, effect_same);
```

Append to the comment block at l. 10360–10371 (the `pcr` reset), before its
closing `*/`:

```c
     * S098 P1 (Q1): with pcr on, a switch to an identical Scene still
     * restarts; the restart is a musical action the user chose.
```

## J. `Core/Hardware/frontPanel/buttonHandler.c`

### J1. l. 1679–1680 — **modify** the `PAR_FOLLOW`-off double click

**Replace:**

```c
        if (isDouble)
            menu_perfModeSceneButtonPressed(seqButtonPressed);
```

**with:**

```c
        if (isDouble) {
            menu_perfModeSceneButtonPressed(seqButtonPressed);
            /*
             * The double-click realign is explicit (S098 P1).
             *
             * What: after the full Scene change, every track is put back on
             * the master clock, as the PAR_FOLLOW-on double click does.
             * Why: this branch relied on the Scene change realigning every
             * track. A switch now keeps identical tracks where they are
             * (target T1), so the documented "+ realign" of this gesture is
             * made explicit. A deferred (scd) change still realigns when it
             * fires, as before.
             * Affiliates: seq_realignActivePatternToMasterClock().
             */
            seq_realignActivePatternToMasterClock();
        }
```

Also append to the function's comment block (l. 1641–1657), after the line
`PAR_FOLLOW off, double click: full Scene change + coalesce + realign.`:

```c
 *       (S098 P1: the realign is an explicit call in both branches; a Scene
 *       change keeps identical tracks in place.)
```

## K. DEV trace stage `'x'`

### K1. `Core/Bank/Scene/AutosaveTrace.h` l. 386 — **add** after `AUTOSAVE_TRACE_STAGE_LOADSAVE_CLICK = 'k',`

```c
    /*
     * x: one executed whole-Scene change and what it kept (S098 P1, DEV
     * only). flags bits 0..6 = the keep mask (bit t = track t continued
     * untouched), bit 7 = the Effect was identical (rebound, not reset).
     * value32 = target Scene bits 0..7 | outgoing active Scene bits 8..15
     * (16 = Scene 17) | the identity compare time in microseconds bits
     * 16..31 (saturated at 0xFFFF). Producer: preset_sceneSwitchKeepMask()
     * (presetManager.c). Consumer: tools/diagnostic_tools/decode_devlogs.py
     * ('x' branch).
     */
    AUTOSAVE_TRACE_STAGE_SCENE_SWITCH = 'x',
```

`'x'` was checked as unused. The stages in use are
`DIJNLSAVMCPTRWFGXOEYBQUKZHcbhmsdwpfk`; uppercase `X` is taken, lowercase `x`
is free.

### K2. `tools/diagnostic_tools/decode_devlogs.py` l. 140, l. 188, l. 1207

After l. 140 (`"k": "AUTOSAVE_TRACE_STAGE_LOADSAVE_CLICK",`), **add:**

```python
    # x: S098 P1 Scene switch decision (keep mask, Effect, compare time).
    "x": "AUTOSAVE_TRACE_STAGE_SCENE_SWITCH",
```

After l. 188 (`"k": "menu_loadSaveTraceClick() (S097 P3)",`), **add:**

```python
    "x": "preset_sceneSwitchKeepMask() (S098 P1)",
```

After l. 1207 (the last line of the `'k'` branch,
`f"{statuses.get(fs, fs)}")`), **add:**

```python
    elif ch == "x":
        # S098 P1 Scene switch decision; layout in AutosaveTrace.h ('x').
        keep = flags & 0x7F
        target = value & 0xFF
        prev = (value >> 8) & 0xFF
        us = (value >> 16) & 0xFFFF
        kept = ",".join(str(t + 1) for t in range(7) if keep & (1 << t))
        prev_name = "17" if prev == 16 else str(prev + 1)
        detail = (f"{enum_name} via {producer}: Scene {prev_name} -> "
                  f"{target + 1}; tracks kept: {kept or 'none'}; Effect "
                  f"{'kept' if flags & 0x80 else 'changed'}; compare "
                  f"{us}{'+' if us == 0xFFFF else ''} us")
```

Check it on synthetic records before the device run, as was done for `'k'`.

## L. Host test `scene_identity`

### L1. `tools/dsp_test/Makefile` — **modify** l. 64 and l. 69, **add** a rule after l. 262

At l. 64, change `distortion dist_makeup_table` to
`distortion dist_makeup_table scene_identity`.

At l. 69, change `buscomp` to `buscomp scene_identity`.

After l. 262 (the blank line after the `seq_steplen` rule), **add:**

```make
# S098 P1: Scene switch identity (laws in modules/scene_identity.c). Break:
# comparing whole address entries (pool offsets included) must fail law 1.
scene_identity: | $(GEN)
	$(EX) --src $(ROOT)/Core/Bank/Scene/Preset/presetManager.c \
	  --func preset_trackStepsMatch --func preset_instrumentImageMatches \
	  --out $(GEN)/preset_identity.c
	$(HOSTCC) $(HOSTFLAGS) modules/scene_identity.c -o build/scene_identity
	./build/scene_identity
	mkdir -p build/break_identity
	sed 's/if ((entry_a & PAT_ADDR_TRIGGER_BIT) != (entry_b & PAT_ADDR_TRIGGER_BIT))/if (entry_a != entry_b)/' \
	  $(GEN)/preset_identity.c > build/break_identity/preset_identity.c
	@grep -q 'if (entry_a != entry_b)' build/break_identity/preset_identity.c || \
	  { echo "scene_identity_break: sed did not apply"; exit 1; }
	$(HOSTCC) -Ibuild/break_identity $(HOSTFLAGS) modules/scene_identity.c \
	  -o build/scene_identity_break
	@if ./build/scene_identity_break >/dev/null 2>&1; then \
	  echo "scene_identity_break: FAILED (a pool-offset compare was not caught)"; \
	  exit 1; else echo "scene_identity_break: caught (law 1 fails as expected)"; fi
```

### L2. `tools/dsp_test/modules/scene_identity.c` — **add** (new file, named by this schedule)

```c
/*
 * Scene switch identity host tests (S098 P1).
 *
 * What:       compiles the real preset_trackStepsMatch() and
 *             preset_instrumentImageMatches() from presetManager.c against a
 *             fake two-Scene Pattern store.
 *             Laws:
 *               1. the same steps stored at different pool offsets match;
 *               2. one changed block byte, a changed trigger bit, or a
 *                  block-less trigger against an empty step do not match;
 *               3. two instrument slots that differ only in
 *                  morph_interpolation[] match; a changed type, Normal or
 *                  Morph endpoint byte does not.
 * Why:        the switch keeps a track running only when it is identical
 *             (target T1). A false "identical" would let a changed track
 *             keep its old state; law 1 proves copies with a different pool
 *             layout still count as identical. Break proof: the Makefile
 *             rebuilds the fragment comparing whole address entries, and
 *             law 1 must fail.
 * Inputs:     build/gen/preset_identity.c.
 * Outputs:    one line per failure; "scene_identity failures: N"; exit 1 if
 *             N > 0.
 * Accessors:  make -C tools/dsp_test scene_identity.
 * Affiliates: presetManager.c, S098_P0_IMPLEMENTATION.md.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define NUM_STEPS               128u
#define PAT_RAW_BLOCK_MAX       136u
#define PAT_ADDR_TRIGGER_BIT    (1u << 15)
#define PAT_ADDR_SPECIALS_BIT   (1u << 14)
#define PAT_ADDR_OFFSET_MASK    0x3FFFu
#define PAT_ADDR_SENTINEL       0x3FFFu
#define INSTRUMENT_PARAM_COUNT  64u

typedef uint8_t instrument_param_value_t;
typedef enum { HOST_TYPE_DRM = 0, HOST_TYPE_COL = 6 } instrument_type_t;
typedef struct {
    instrument_param_value_t instrument_parameters[INSTRUMENT_PARAM_COUNT];
    instrument_param_value_t morph_instrument_parameters[INSTRUMENT_PARAM_COUNT];
    instrument_param_value_t morph_interpolation[INSTRUMENT_PARAM_COUNT];
} instrument_parameter_images_t;
typedef struct kit_instrument_slot {
    instrument_type_t type;
    instrument_parameter_images_t parameter_images;
} kit_instrument_slot_t;

/* Two Scenes, one track: an address entry and a byte count per step. */
static uint16_t host_entry[2][NUM_STEPS];
static uint8_t host_bytes[2][NUM_STEPS];
static uint8_t host_pool[2][8192];

static uint8_t pat_rawReadBlock(uint8_t scene_index, uint8_t track,
                                uint8_t step, uint8_t out[PAT_RAW_BLOCK_MAX],
                                uint16_t *entry_out)
{
    uint16_t entry = host_entry[scene_index][step];
    uint8_t bytes = host_bytes[scene_index][step];

    (void)track;
    if (entry_out)
        *entry_out = entry;
    if (bytes == 0u)
        return 0u;
    memcpy(out, &host_pool[scene_index][entry & PAT_ADDR_OFFSET_MASK], bytes);
    return bytes;
}

#include "preset_identity.c"

static void store_reset(void)
{
    uint8_t s;
    uint8_t step;

    for (s = 0u; s < 2u; s++)
        for (step = 0u; step < NUM_STEPS; step++) {
            host_entry[s][step] = PAT_ADDR_SENTINEL;
            host_bytes[s][step] = 0u;
        }
    memset(host_pool, 0, sizeof(host_pool));
}

static void store_put(uint8_t scene, uint8_t step, uint16_t offset,
                      const uint8_t *block, uint8_t bytes, uint8_t trigger)
{
    host_entry[scene][step] = (uint16_t)(offset | PAT_ADDR_SPECIALS_BIT |
                                         (trigger ? PAT_ADDR_TRIGGER_BIT : 0u));
    host_bytes[scene][step] = bytes;
    memcpy(&host_pool[scene][offset], block, bytes);
}

static void store_trigger_only(uint8_t scene, uint8_t step)
{
    host_entry[scene][step] = (uint16_t)(PAT_ADDR_SENTINEL |
                                         PAT_ADDR_TRIGGER_BIT);
    host_bytes[scene][step] = 0u;
}

static unsigned failures;

static void expect(int got, int want, const char *law)
{
    if (got != want) {
        failures++;
        printf("FAIL %s: got %d, want %d\n", law, got, want);
    }
}

static void same_two_scenes(void)
{
    static const uint8_t block[8] = { 0x03u, 0x01u, 0x80u, 0x45u,
                                      0x10u, 0x7Fu, 0x00u, 0x00u };

    store_reset();
    store_put(0u, 3u, 0u, block, sizeof(block), 1u);
    store_put(1u, 3u, 64u, block, sizeof(block), 1u);    /* other offset */
    store_trigger_only(0u, 10u);
    store_trigger_only(1u, 10u);
}

int main(void)
{
    kit_instrument_slot_t a;
    kit_instrument_slot_t b;
    uint8_t i;

    /* Law 1: different pool offsets, same content. */
    same_two_scenes();
    expect(preset_trackStepsMatch(0u, 0u, 1u), 1, "law 1 offsets");

    /* Law 2: a byte, a trigger bit, a block-less trigger. */
    same_two_scenes();
    host_pool[1][64 + 5] ^= 0x01u;
    expect(preset_trackStepsMatch(0u, 0u, 1u), 0, "law 2 byte");
    same_two_scenes();
    host_entry[1][3] &= (uint16_t)~PAT_ADDR_TRIGGER_BIT;
    expect(preset_trackStepsMatch(0u, 0u, 1u), 0, "law 2 trigger");
    same_two_scenes();
    store_trigger_only(0u, 20u);
    expect(preset_trackStepsMatch(0u, 0u, 1u), 0, "law 2 block-less");

    /* Law 3: the derived image is ignored, the endpoints are not. */
    memset(&a, 0, sizeof(a));
    for (i = 0u; i < INSTRUMENT_PARAM_COUNT; i++) {
        a.parameter_images.instrument_parameters[i] = i;
        a.parameter_images.morph_instrument_parameters[i] = (uint8_t)(127u - i);
        a.parameter_images.morph_interpolation[i] = (uint8_t)(i / 2u);
    }
    a.type = HOST_TYPE_COL;
    b = a;
    b.parameter_images.morph_interpolation[4] ^= 0x22u;
    expect(preset_instrumentImageMatches(&a, &b), 1, "law 3 interpolation");
    b = a;
    b.type = HOST_TYPE_DRM;
    expect(preset_instrumentImageMatches(&a, &b), 0, "law 3 type");
    b = a;
    b.parameter_images.instrument_parameters[7] ^= 0x01u;
    expect(preset_instrumentImageMatches(&a, &b), 0, "law 3 normal");
    b = a;
    b.parameter_images.morph_instrument_parameters[9] ^= 0x01u;
    expect(preset_instrumentImageMatches(&a, &b), 0, "law 3 morph");

    printf("scene_identity failures: %u\n", failures);
    return failures ? 1 : 0;
}
```

---

# P2 — T2 (Scene and Pattern Loads through Scene 17) and T3 (the LED)

## M. `Core/Bank/Scene/Preset/presetManager.c` — the background machine

### M1. l. 111 — **add** `pm_bg_kind` after `static uint8_t pm_bg_phase = 0u;`

```c
/*
 * Kind of the background load (S098 P2).
 *
 * What: PRESET_BG_KIND_BANK, PRESET_BG_KIND_SCENE or PRESET_BG_KIND_PATTERN
 * for the load the phases are running. Phases 1-3 and 5 are the same for
 * every kind. Phase 4 posts the kind's request. Only a Bank load drops
 * queued copy/clear jobs, can discard the dirty mask, unlocks the panel and
 * writes the MIDI CC0 result.
 * Why: target T2. A Scene or Pattern Load that replaces a played Scene must
 * first move playback onto Scene 17, exactly as a Bank Load does.
 * pm_request_type cannot carry the kind: preset_loadName() rewrites it while
 * the user browses, and the flag byte pm_bg_phase is full.
 * Inputs: preset_requestBackgroundBankLoad() (and phase 0 for MIDI),
 * preset_requestBackgroundSceneLoad().
 * Outputs: phases 1, 2 and 4, preset_bgEnd(),
 * preset_backgroundBankLoadRunning().
 * RAM: +1 B SRAM1 (approved ______). Owner: Preset.
 */
static uint8_t                   pm_bg_kind = PRESET_BG_KIND_BANK;
```

Also append to the `pm_bg_phase` comment block (l. 92–110), before its closing
`*/`:

```c
 * S098 P2: the same phases also run Scene and Pattern Loads (pm_bg_kind).
```

### M2. l. 3728 — **modify** `preset_requestBackgroundBankLoad()`: state the kind

After l. 3728 (`pm_request_slot = slot;`), **add:**

```c
    pm_bg_kind = PRESET_BG_KIND_BANK;     /* S098 P2 */
```

### M3. l. 3736–3739 — **modify** `preset_backgroundBankLoadRunning()`

**Replace** the body with:

```c
uint8_t preset_backgroundBankLoadRunning(void)
{
    /*
     * Bank loads only (S098 P2).
     *
     * What: a background Bank load is in flight. A Scene or Pattern Load on
     * the same phases is not one.
     * Why: MIDI input filtering (midiIn_dispatch()), the chain gate
     * (sequencer.c), the panel lock (preset_backgroundBankLoadLocked()) and
     * the Menu VOICE gate are Bank rules (S082 R7, D13). A Scene/Pattern
     * Load keeps today's Load-page hold (decision Q6).
     */
    return (uint8_t)((pm_bg_phase & PM_BG_PHASE_MASK) != 0u &&
                     pm_bg_kind == PRESET_BG_KIND_BANK);
}
```

`preset_requestBackgroundBankLoad()` keeps its own in-flight test on
`pm_bg_phase` (l. 3715), so a Bank request is still refused while a
Scene/Pattern Load runs.

### M4. l. 3734 — **add** `preset_requestBackgroundSceneLoad()` after the closing brace of `preset_requestBackgroundBankLoad()`

```c
/*
 * Request a Scene or Pattern Load that keeps playing (S098 P2).
 *
 * What: accepted at once and advanced by preset_tickBackgroundBankLoad().
 * Phase 1 decides whether Scene 17 is needed: the transport runs and a
 * destination is the active Scene or a Scene a track plays. If it is, phases
 * 2-3 quiesce, build Scene 17 from what is playing and move playback there,
 * then phase 4 posts the Load. If not, phase 4 posts it at once, exactly as
 * the Menu did before. Refused while any background load runs, while Preset
 * is busy, for an empty mask, and for a slot that does not exist (so Scene 17
 * is never built for nothing, as for a Bank, §4.1). A pending MIDI Bank
 * request is discarded, as a Menu Bank OK discards it (R5).
 * Why: target T2. A Load writes its destinations in place
 * (pat_initScene(), then the read). A played Scene then plays defaults and
 * partial content during the read and drifts (S097 P3 §9.2). On Scene 17,
 * playback never reads a region being rewritten. The user switches back
 * manually afterwards; with S098 P1 that switch changes only what the Load
 * changed.
 * Inputs: slot (zero-based library slot), scene_mask (destinations), kind
 * (PRESET_BG_KIND_SCENE or PRESET_BG_KIND_PATTERN).
 * Outputs: accepted (1) / refused (0); pm_bg_* state; completion through
 * PRESET_OP_SCENE_LOAD / PRESET_OP_PATTERN_LOAD as before.
 * Accessors: Menu Scene and Pattern Load OK.
 * Affiliates: preset_tickBackgroundBankLoad(), filesystem_sceneSlotExists(),
 * filesystem_patternSlotExists(), preset_loadSceneForScenes(),
 * preset_loadPatternForScenes().
 */
uint8_t preset_requestBackgroundSceneLoad(uint16_t slot, uint16_t scene_mask,
                                          uint8_t kind)
{
    if ((pm_bg_phase & PM_BG_PHASE_MASK) != 0u || pm_status != PRESET_IDLE ||
        scene_mask == 0u)
        return 0u;
    if (kind == PRESET_BG_KIND_SCENE) {
        if (!filesystem_sceneSlotExists(slot))
            return 0u;
    } else if (kind == PRESET_BG_KIND_PATTERN) {
        if (!filesystem_patternSlotExists(slot))
            return 0u;
    } else {
        return 0u;
    }
    pm_bg_pending_slot = PM_BG_PENDING_NONE;      /* R5 */
    pm_bg_kind = kind;
    pm_request_slot = slot;
    pm_kit_request_scene_mask = scene_mask;
    pm_bg_phase = 1u;                             /* never PM_BG_REPLACE */
    return 1u;
}
```

### M5. l. 3846–3850 — **modify** `preset_bgEnd()`: the CC0 result is a Bank result

**Replace:**

```c
    if (failed) {
        pm_bg_result = (uint8_t)(PRESET_BG_RESULT_FAILED |
                                 (midi ? PRESET_BG_RESULT_MIDI : 0u));
        menu_backgroundBankLoadAbort();
    }
```

**with:**

```c
    if (failed) {
        /*
         * S098 P2: only a Bank load reports through the MIDI CC0 result.
         * A failed Scene/Pattern Load ends the accepted Load/Save command
         * the same way (menu_backgroundBankLoadAbort() finishes any
         * command; its Bank preview re-request is Bank-type only).
         */
        if (pm_bg_kind == PRESET_BG_KIND_BANK)
            pm_bg_result = (uint8_t)(PRESET_BG_RESULT_FAILED |
                                     (midi ? PRESET_BG_RESULT_MIDI : 0u));
        menu_backgroundBankLoadAbort();
    }
    pm_bg_kind = PRESET_BG_KIND_BANK;
```

`bank_endLoadAvailability()` at l. 3843 already ends the availability session
on every exit. That includes the Scene/Pattern session started in M8.

### M6. l. 3900 — **add** the kind for a MIDI-origin Bank start (phase 0)

Before l. 3900 (`pm_bg_phase = (uint8_t)(PM_BG_ORIGIN_MIDI | PM_BG_REPLACE);`),
**add:**

```c
        pm_bg_kind = PRESET_BG_KIND_BANK;     /* S098 P2 */
```

### M7. l. 3914–3916 — **modify** phase 1: a Scene/Pattern Load without Scene 17 goes straight to phase 4

**Replace** l. 3916 (`preset_bgSetPhase(2u);`) with:

```c
        /*
         * S098 P2: a Bank request always quiesces (D12, D1/R6). A Scene or
         * Pattern Load quiesces only when it builds Scene 17; otherwise it
         * is posted at once, exactly as the Menu posted it before.
         */
        preset_bgSetPhase((pm_bg_kind == PRESET_BG_KIND_BANK ||
                           (pm_bg_phase & PM_BG_NEEDED) != 0u) ? 2u : 4u);
```

### M8. l. 3919–3922 — **modify** phase 2: copy/clear is waited for, not dropped, for Scene/Pattern Loads

**Replace:**

```c
        if ((pm_bg_phase & PM_BG_DISCARDED) == 0u) {
            ccSvc_dropQueued();
            filesystem_setAutosaveYield(1u);
        }
```

**with:**

```c
        if ((pm_bg_phase & PM_BG_DISCARDED) == 0u) {
            /*
             * S098 P2 (Q4): only a Bank load drops queued copy/clear jobs
             * (D12: a Bank replaces every Scene they name). A Scene/Pattern
             * Load leaves them queued; the ccSvc_busy() wait below lets
             * them finish first, bounded by PM_BG_WAIT_LIMIT_MS.
             */
            if (pm_bg_kind == PRESET_BG_KIND_BANK)
                ccSvc_dropQueued();
            filesystem_setAutosaveYield(1u);
        }
```

The Replace discard below (`if ((pm_bg_phase & PM_BG_REPLACE) != 0u)
autosave_discardDirtyMask();`) never runs for a Scene/Pattern Load, because
those never set `PM_BG_REPLACE` (M4).

### M9. l. 3995 — **add** the Scene/Pattern post at the top of phase 4

Immediately after l. 3995 (`case 4u: {`), before the Bank comment block,
**add:**

```c
        /*
         * Post a Scene or Pattern Load (S098 P2).
         *
         * What: the kind's ordinary request with the destination mask. When
         * it is accepted while the transport runs, the destinations are
         * marked unavailable until the Load ends (bank_beginLoadAvailability();
         * ended by the completion callback and by preset_bgEnd()). A refusal
         * while the card is busy is retried next pass, for at most
         * PM_BG_WAIT_LIMIT_MS; a refusal on an idle card fails the request.
         * The panel is never unlocked for these kinds (Q6).
         * Why: target T2 and decision Q5. A PERF press, MIDI PC, queued or
         * chain change must not select a Scene that is half written. While
         * Scene 17 plays, the chain is off anyway (17 has no chain), but a
         * direct running Load into an unplayed Scene had no protection.
         * Inputs: pm_bg_kind, pm_request_slot, act (= the destination mask).
         * Outputs: phase 5, or failure.
         * Affiliates: preset_loadSceneForScenes(), preset_loadPatternForScenes(),
         * bank_beginLoadAvailability(), menu_perfSceneChangeExecute() (refuses
         * an unavailable Scene).
         */
        if (pm_bg_kind != PRESET_BG_KIND_BANK) {
            uint8_t posted = (pm_bg_kind == PRESET_BG_KIND_SCENE)
                ? preset_loadSceneForScenes(pm_request_slot, act)
                : preset_loadPatternForScenes(pm_request_slot, act);

            if (posted) {
                if (seq_isRunning())
                    bank_beginLoadAvailability(act);
                preset_bgSetPhase(5u);
            } else if (filesystem_status() != FS_STATUS_BUSY ||
                       preset_bgWaitExpired()) {
                preset_bgEnd(1u);
            }
            return;
        }
```

`act` is `pm_kit_request_scene_mask`, read at l. 3877. The two requests
rewrite `pm_request_slot`, `pm_request_type` and `pm_kit_request_scene_mask`
with the same values, and set `pm_status`, so phase 5 waits for the Menu's
acknowledgement as it does for a Bank.

### M10. l. 496 and l. 606 — **add** the end of the availability session in the two completion callbacks

In `on_pattern_load_complete()`, after l. 496
(`preset_markRequestedScenesPresentOnSuccessfulLoad();`), and in
`on_scene_load_complete()`, after l. 606 (the same call), **add:**

```c
    /*
     * End the load-availability session (S098 P2).
     *
     * What: the destinations become selectable again as soon as the Load
     * has committed (or failed). Why: a running Scene/Pattern Load marks its
     * destinations unavailable when it is posted (preset_tickBackgroundBankLoad()
     * phase 4); the Menu completion that follows may repaint the PERF row
     * and accept a Scene change at once. Idempotent: a Load posted without a
     * session (stopped, the copy/clear reload job, the boot fallback) ends
     * nothing. preset_bgEnd() ends it again on every exit.
     * Affiliates: bank_beginLoadAvailability(), bank_endLoadAvailability().
     */
    bank_endLoadAvailability();
```

## N. `Core/Bank/Scene/Preset/presetManager.h`

### N1. l. 237 — **add** after `uint8_t preset_backgroundBankLoadRunning(void);`

```c
/*
 * Background Scene and Pattern Loads (S098 P2).
 *
 * The same phases as a Bank load (Scene 17 when a played Scene is replaced
 * while running, else a direct post). Kinds: PRESET_BG_KIND_BANK (internal,
 * set by preset_requestBackgroundBankLoad()), PRESET_BG_KIND_SCENE,
 * PRESET_BG_KIND_PATTERN. Returns 1 when accepted.
 * preset_backgroundBankLoadRunning()/Locked()/Unlocked() stay Bank-only.
 * Client: Menu Scene and Pattern Load OK.
 */
#define PRESET_BG_KIND_BANK     0u
#define PRESET_BG_KIND_SCENE    1u
#define PRESET_BG_KIND_PATTERN  2u
uint8_t preset_requestBackgroundSceneLoad(uint16_t slot, uint16_t scene_mask,
                                          uint8_t kind);
```

`pm_bg_kind` (M1) is initialised from `PRESET_BG_KIND_BANK`, so N1 must be in
the header that `presetManager.c` includes at l. 34. It is.

## O. `Core/Menu/menu.c` — Scene and Pattern Load OK

### O1. l. 16773–16779 — **modify** the Scene Load OK

**Replace:**

```c
                case SAVE_TYPE_SCENE:
                    if (preset_loadSceneForScenes(
                            menu_currentPresetNr[SAVE_TYPE_SCENE],
                            menu_kitLoadSceneMask)) {
                        commandAccepted = 1u;
                    }
                    break;
```

**with:**

```c
                case SAVE_TYPE_SCENE:
                    /*
                     * S098 P2: the Scene Load goes through the background
                     * request. While the transport runs and a destination
                     * is played, playback first moves onto Scene 17 (as a
                     * Bank Load does); otherwise it is posted at once as
                     * before. A refusal (Preset or a load busy, slot
                     * missing) is held and replayed like any refused OK.
                     */
                    if (preset_requestBackgroundSceneLoad(
                            menu_currentPresetNr[SAVE_TYPE_SCENE],
                            menu_kitLoadSceneMask, PRESET_BG_KIND_SCENE)) {
                        commandAccepted = 1u;
                    }
                    break;
```

### O2. l. 16813–16818 — **modify** the Pattern Load OK

**Replace:**

```c
                case SAVE_TYPE_PATTERN:
                    if (preset_loadPatternForScenes(
                            menu_currentPresetNr[SAVE_TYPE_PATTERN],
                            menu_kitLoadSceneMask))
                        commandAccepted = 1u;
                    break;
```

**with:**

```c
                case SAVE_TYPE_PATTERN:
                    /* S098 P2: as the Scene Load above (Scene 17 when a
                     * played Pattern is replaced while running). */
                    if (preset_requestBackgroundSceneLoad(
                            menu_currentPresetNr[SAVE_TYPE_PATTERN],
                            menu_kitLoadSceneMask, PRESET_BG_KIND_PATTERN))
                        commandAccepted = 1u;
                    break;
```

Unchanged, checked:

- **Completions.** `PRESET_OP_SCENE_LOAD` (l. 18820) and
  `PRESET_OP_PATTERN_LOAD` (l. 18997) keep their handling. With Scene 17
  active, `preset_startLoadApply()` (`presetManager.c:2129`) finds the active
  Scene is 16 and no slot playing a loaded Scene, so it applies nothing.
- **Refused OK.** The hold and replay at l. 16852–16855 (`menu_loadSaveOkRetry`)
  apply to these refusals as before.

## P. `Core/Menu/menu.c` — T3, the Scene 17 LED

### P1. l. 10514–10546 — **modify** `menu_syncQueuedSceneLed()` and its comment

**Replace** the comment block at l. 10514–10530 with:

```c
/*
 * Owner of both fast-blink slots (S080 P2 Defect 10; S082 P3 W1; S098 P2).
 *
 * What: slot QUEUED_SCENE fast-blinks the queued Scene's SEQ LED in PERF.
 * Slot BANK_LOAD fast-blinks the Load/Save mode LED (LED_MODE4):
 *  - whenever Scene 17 exists, that is while playback runs from it, in every
 *    mode, transport running or stopped (S098 T3);
 *  - while an unlocked background Bank load runs and the panel is not in
 *    Load/Save mode (S082 P3).
 * Both clear when their condition ends.
 * Why: re-deriving from state on every tick covers every way a queue, a mode
 * change, a load or a Scene 17 lifetime can start or end, without call sites
 * in each path. Scene 17 suspends AutoSave (D11) and has no Scene number or
 * SEQ LED. The flashing mode LED is the one reminder that playback is on the
 * temporary Scene until the user changes Scene (user, 2026-11-18: "should be
 * enough to alert the user"). The fast layer sits above the base and blink
 * states, so led_setMode2() repaints do not hide it.
 * Inputs: seq_queuedSceneTarget(), buttonHandler_getMode(),
 * preset_backgroundBankLoadUnlocked(), scene_backgroundExists().
 * Outputs: led_setFastBlinkSlot() on change only.
 * Accessors: menu_serviceDeferredSceneChange() (every buttonHandler_tick()).
 * Affiliates: LED_LAYER_FAST, LED_FAST_SLOT_*.
 */
```

**Replace** l. 10542–10543:

```c
    if (preset_backgroundBankLoadUnlocked() && mode != SELECT_MODE_LOAD_SAVE)
        want_load = LED_MODE4;
```

**with:**

```c
    if (scene_backgroundExists() ||
        (preset_backgroundBankLoadUnlocked() && mode != SELECT_MODE_LOAD_SAVE))
        want_load = LED_MODE4;
```

`scene_backgroundExists()` is already used in `menu.c` (l. 8392);
`SceneData.h` is included at l. 53.

## Q. `Core/Hardware/SD/filesystem.c` — Q3, the realign is a backstop

### Q1. l. 13315–13330 — **modify** the comment block of `filesystem_patternRealignPlayingTracks()` (comment only)

**Replace** the block with:

```c
/*
 * Realign the tracks that play a replaced Pattern (S097 P3 §9; backstop
 * since S098 P2).
 *
 * What: while the sequencer runs, refreshes the effective track-timing cache
 * from the new regions, then puts every track whose played Scene is in
 * scene_mask back on the master clock, as the PERF double-click does.
 * Why: a load writes the played region in place. pat_initScene() leaves the
 * Normal defaults (length 16, default scale) until the header is read, and
 * presetMorph_tick() feeds them to the DDA within a pass; the new Pattern can
 * also carry a different length, scale or play mode. Either way the track's
 * cursor and accumulator keep the old phase and stay off the grid. Tracks
 * playing other Scenes are not touched.
 * Since S098 P2 a running Menu Scene, Pattern or Bank Load that replaces a
 * played Scene moves those tracks onto Scene 17 first, so this finds no
 * track to realign. It stays (decision Q3) for the two cases that still
 * write a played region in place: a Load posted while stopped and then
 * started before the Pattern was read, and the copy/clear "reload Scene" job
 * (ccClear_runReloadScene()).
 * Inputs: the replaced-Scene mask. Outputs: seq_realignTrackToMasterClock()
 * for each matching track. Callers: the explicit Pattern replacement ends
 * (Scene Load phase 52, Pattern Load phase 14, Bank empty Scenes); never
 * filesystem_finish(), whose op_scene_load_scene_mask may be stale.
 */
```

---

## R. Order, gates and checks

**P1:**

1. **Baseline.** Before any edit, link the tree and keep the ELF and the
   loop/inlined-set dumps in `tools/dsp_test/baselines/S098/`.
2. **Apply** A–L.
3. **Build:** `make all && make img`. Check:
   - no new warnings;
   - the `_Static_assert`s pass (`scene_settings_t` 75 B, `kit_settings_t`
     2 B, `pat_scene_region_t` as in `PatternData.h`).
4. **Host tests:** `make -C tools/dsp_test all`. `scene_identity` must pass,
   and its break must be "caught".
5. **Loop gate** against the baseline:
   - 86/86 loops exact;
   - 77 oscillator and 58 mixer hot loops equal;
   - the same hot call set;
   - all 21 ISR/render bodies identical.

   `sequencer.c` changes in foreground functions only. If a TIM3 body moves,
   record it and stop.
6. **`link_budget.py`:** report the flash headroom and the `.bss` change. It
   must be +2 B.
7. **Device:** the P1 matrix in `S098_P0_SCENE_SWITCH_CORRECTIONS.md` §8. Copy
   the card to `SD_CARD_S098_P1`; decode the `'x'` records.

**P2**, after the P1 device check:

1. **Apply** M–Q.
2. **Build, host tests, loop gate** as for P1. `.bss` must be +1 B.
3. **Device:** the P2 matrix (§8 of the plan). Copy the card to
   `SD_CARD_S098_P2`.

**Rollback:** each phase is self-contained. P2 depends on P1 only for the
silent switch back from Scene 17.

---

## S. Checked against the tree (no open items)

- **`seq_selectActivePattern()`** has one caller, `menu.c:10353`. The
  signature change needs no other edit.
- **`preset_startSceneModulationRebind()`** has two callers (l. 2285, 2348),
  both updated in G4.
- **`preset_startDrumsetApply()`** keeps its other callers: `menu.c:686`,
  `copyOps.c:801` and `:919`, `presetManager.c:2135`, `main.c:1505`. Their
  rebind mask is all six (G3).
- **The Instrument apply starts** (`preset_startKitMorphApply()` l. 2521,
  `preset_armInstrumentMorphCommit()` l. 2574,
  `preset_startInstrumentApplyImage()` l. 2781) all reach the rebind through
  the Morph phase at l. 3064, where G5 sets the mask to all six.
- **Effect.** `effects_rebindSourceScene()` refuses a type mismatch, and
  `preset_startSceneSwitchApply()` then falls back to `effects_activateScene()`.
- **Morph worker.** Its pass skips slots whose `pass_mask` bit is clear
  (`presetMorph_tick()`), so E1's mask edit is safe mid-pass.
- **Background-load predicates.** `preset_backgroundBankLoadRunning()` is used
  by `MidiIn.c:172`, `sequencer.c:6254` and `menu.c:11227`; all three are Bank
  rules. `…Locked()` and `…Unlocked()` derive from it and from
  `PM_BG_UNLOCKED`, which only a Bank sets.
- **Slot-exists checks.** `filesystem_sceneSlotExists()` and
  `filesystem_patternSlotExists()` are public (`filesystem.h:1459`, `:1475`)
  and wrap the same `filesystem_librarySlotExists()` check as the requests.
- **The copy/clear reload job** (`clearOps.c:707`) and the boot fallback
  (`presetManager.c:4082`, from `main.c:1206/1223`) still call
  `preset_loadSceneForScenes()` directly. M10's session end is idempotent for
  them.
- **The LED.** `LED_FAST_SLOT_BANK_LOAD` and `LED_MODE4` exist
  (`ledHandler.h:151`, `:53`).
- **The trace stage.** `'x'` is free.
- **The host-test extract** (`extract.py --func`) handles static functions,
  as for `seq_rollClearTrack`.

## T. Statements this makes out of date

See `S098_P0_SCENE_SWITCH_CORRECTIONS.md` §10. In addition:

- **`STORAGE_SRAM_MANIFEST.md`:** the three bytes of §0.
- **`MODULE_INTERCHANGE_SPEC.md`** (`seq_clearSceneAutomationDirty()` row,
  l. 1185): Scene changes now use the per-part clear.

---

## Implementation progress log

### P1 — applied 2026-11-18

All P1 entries A1–L2 are in the tree. Notes taken while applying them:

- **A1–A5 (`sequencer.c`)** — done. `seq_clearSceneAutomationDirtyFor()` and
  `seq_resetTrackForSceneChange()` added; `seq_selectActivePattern()` now takes
  `keep_mask`/`effect_same` and resets/realigns/marks only changed tracks;
  `seq_realignTrackPosition()` added and `seq_realignTrackToMasterClock()`
  delegates to it. The FX realign/overlay reset now follows the Effect and
  changed tracks. `_Static_assert` values were checked in the tree first.
- **B1 (`sequencer.h`)** — prototype and contract comment updated.
- **C1/D1 (`InstrumentManager.c/.h`)** —
  `instrumentManager_prepareModulationForSlotChange()` and its
  `instrumentManager_installedTouches()` helper added. Kept sources with
  bindings wholly inside kept slots are untouched; descriptor-LFO adapters on
  a changed slot join the rebind mask but are not detached (as specified).
- **E1/F1 (`presetMorphEngine.c/.h`)** — `presetMorph_retargetScene()` added;
  the worker keeps only kept slots' request/pass bits and resumes an
  interrupted priority slot from `resume_slot`/`resume_descriptor_index`.
- **G1–G8 (`presetManager.c`)** — `PM_ALL_SLOTS`,
  `drumset_apply_rebind_mask`, `instrument_apply_rebind_mask`,
  `preset_clearSlotStepOverrides()`, the shared `PRESET_*_FIELDS` lists with
  their three `_Static_assert`s, the per-content identity tests,
  `preset_sceneSwitchKeepMask()`, `preset_prepareSceneSwitch()` and
  `preset_startSceneSwitchApply()` added; the two rebind sites and the masked
  cursor (`preset_tickInstrumentApply()`) updated; both Scene 17 copy
  functions now expand the shared lists.
  - `instrument_apply_rebind_mask` initialises to `PM_ALL_SLOTS` (63), so it
    is one `.data` byte, not `.bss`; `drumset_apply_rebind_mask` is the other
    approved byte (`.bss`). Total new RAM is the approved 2 B for P1.
- **H1 (`presetManager.h`)** — three prototypes and their contract added.
- **I1 (`menu.c`)** — executor decides, carries, switches, then applies the
  voice half. The `pcr` comment notes Q1.
- **J1 (`buttonHandler.c`)** — the `PAR_FOLLOW`-off double click now calls
  `seq_realignActivePatternToMasterClock()` explicitly; comment updated.
- **K1/K2** — DEV stage `'x'` added to `AutosaveTrace.h` (lowercase `x` was
  free) and decoded in `decode_devlogs.py`.
- **L1/L2** — `tools/dsp_test/modules/scene_identity.c` and the Makefile
  target added; the tree `all:` and `.PHONY` lists include `scene_identity`.

**P1 build (FS_OS=1, the S097 P3 image configuration).**

| Measure | Start-of-P1 ELF | After P1 | Delta |
|---------|-----------------|----------|-------|
| text | 679,952 | 683,160 | +3,208 |
| data | 432 | 432 | 0 |
| bss | 422,452 | 422,452 | 0 (section; the two bytes fit existing padding) |
| flash headroom | 73,280 | 70,072 | −3,208 |

The start-of-P1 ELF was the tree's last build; it is 432 B below the
`S097_P3` §11 figure of 680,384, so the true P1 growth against the latest
S097 P3 link is about 2,776 B. `FS_OS=0` builds are ~719.8 KB and are not
comparable (that flag is off by default until its retest; see Makefile).

**Gate results (baseline in `tools/dsp_test/baselines/S098/`):**

- `elf_loopdump.py --compare`: **86/86** oscillator/fold loops exact, 0
  renamed, 0 missing.
- `elf_hotloops.py --compare`: **58** mixer hot loops equal, 0 shrank,
  0 grew.
- `elf_symdump.py --compare` on the 12 captured ISR/render bodies:
  `mixer_calcNextSampleBlock()` and ten IRQ handlers identical;
  `audio_check_and_render.part.0` **grew 4 B** (`0x102c` → `0x1030`). The
  only semantic-neutral change is a branch-layout swap in inlined MIDI
  note-off code (`cbz r3,…; cmp r2,#15; bls.w` became
  `cmp r2,#15; bhi.n; ldr; cmp; bne.w`, plus one operand order). No
  per-sample loop, call set or TIM3 body moved; the scheduled “all 21
  identical” criterion is met for 11 of 12 captured bodies, and the twelfth
  is an equivalent layout change recorded here.
  *(Superseded by Assessment A.2: all 18 standalone bodies of the S097
  list compared, 17 identical.)*
- Host tests: `make -C tools/dsp_test all` green. `scene_identity` reports 0
  failures and its break build is caught (pool-offset compare fails law 1).

**P1 device matrix is not run here** (no hardware in this session). It is
listed in `S098_P0_SCENE_SWITCH_CORRECTIONS.md` §8 and remains for the user.

### P2 — applied 2026-11-18

All P2 entries M–Q are in the tree:

- **M1–M10 (`presetManager.c`)** — `pm_bg_kind` (`+1 B`, approved) with its
  comment; the Bank request and phase-0 MIDI start state the kind;
  `preset_backgroundBankLoadRunning()` is Bank-only; the new
  `preset_requestBackgroundSceneLoad()` validates the slot and destination
  mask; `preset_bgEnd()` records a MIDI CC0 result only for a Bank load and
  resets the kind; phase 1 skips the quiesce for a Scene/Pattern Load that
  does not build Scene 17; phase 2 waits for copy/clear instead of dropping
  it; phase 4 posts the kind's request and starts the availability session
  when the transport runs; both completion callbacks end that session.
- **N1 (`presetManager.h`)** — `PRESET_BG_KIND_BANK/SCENE/PATTERN` and the
  request prototype added.
- **O1/O2 (`menu.c`)** — the Scene and Pattern Load OK now call
  `preset_requestBackgroundSceneLoad()` with their kind; the refused-OK hold
  path is unchanged.
- **P1 (`menu.c`)** — `LED_MODE4` on the `LED_FAST_SLOT_BANK_LOAD` slot
  fast-blinks whenever `scene_backgroundExists()` is true, in every mode; the
  unlocked Bank load condition is kept. Comment updated.
- **Q1 (`filesystem.c`)** — the `filesystem_patternRealignPlayingTracks()`
  comment now names S098 P2 and the two cases it still covers.
- **K2 decoder** — verified on two synthetic `'x'` records (keep mask all
  seven with the Effect kept, Scene 17 → 10, 123 µs; and one kept track
  with a changed Effect, saturated compare time). Output was correct.

**P2 build (FS_OS=1).**

| Measure | After P1 | After P2 | P2 delta |
|---------|----------|----------|----------|
| text | 683,160 | 683,704 | +544 |
| data | 432 | 432 | 0 |
| bss section | 422,452 | 422,452 | 0 (symbols `pm_bg_kind` +1, padding) |
| flash used | 683,592 | 684,136 | +544 |
| flash headroom | 70,072 | 69,528 | −544 |

The three approved bytes are present as symbols
(`drumset_apply_rebind_mask`, `instrument_apply_rebind_mask`,
`pm_bg_kind`). The `.bss`/`.data` section totals do not move because the
linker's section tail padding absorbs them.

**P2 gates:** `elf_loopdump.py` **86/86 exact**; `elf_hotloops.py` **58**
equal, 0 grew; ISR/render comparison is identical to P1 (the same single
4-byte equivalent branch-layout change in `audio_check_and_render.part.0`,
unchanged by P2). `make -C tools/dsp_test all` green (the `scene_identity`
and its break build included). `make img` produced
`build/LXRV2_lxr02.img` (684,136 B payload).

**P2 device matrix is not run here.** It remains in
`S098_P0_SCENE_SWITCH_CORRECTIONS.md` §8 for the user.

### Items left for the user

- The P1 and P2 device matrices (no hardware in this session).
- The three approved RAM bytes: recorded as approved 2026-11-18 in the code
  comments at the user's instruction for this session.
- The out-of-date statements in §10/T above (`MEMORY.md`, specifications,
  `DRAFT_ONGOING_BUGS.md`) are unchanged by design.

---

## Assessment of the applied code (2026-11-18)

Written for the user, before the device run.

**Method:**

- every S098 site in the tree was read against entries A–Q of this schedule;
- the tree was rebuilt clean in a scratch copy, with both `FS_OS=0` (the
  Makefile default) and `FS_OS=1` (the configuration of the handed-over
  image);
- the full host suite was run;
- the loop gate and an ISR/render comparison were run against
  `tools/dsp_test/baselines/S098/lxr02_baseline.elf`.

The S097 P3 work, also uncommitted, was not re-reviewed.

### A.1 Conformance

| Entries | Result |
|---------|--------|
| A1–A5 `sequencer.c` | as scheduled. `seq_realignTrackToMasterClock()` was split in place: its old comment block now sits over `seq_realignTrackPosition()`'s new one, and the old block was re-added above the public function. The code is identical to the schedule |
| B1 `sequencer.h` | as scheduled |
| C1/D1 `InstrumentManager.c/.h` | as scheduled |
| E1/F1 `presetMorphEngine.c/.h` | as scheduled |
| G1–G8 `presetManager.c` | as scheduled. Line numbers moved by the insertions, as expected |
| H1, I1, K1, K2, L1, L2 | as scheduled |
| J1 `buttonHandler.c` | code as scheduled; its comment repeats an error that was in the schedule (F2) |
| M1–M10, N1, O1, O2, P1, Q1 | as scheduled |

No code deviates from the schedule.

### A.2 Independent build and gates

| Check | Result |
|-------|--------|
| Clean build `FS_OS=0` | exit 0. Every warning is older than S098; none is in an S098 file. text 720,320, data 432, bss 422,532; flash 720,752 / 753,664 B, headroom **32,912 B** |
| Clean build `FS_OS=1` | text **683,704** (+3,752 against the baseline ELF's 679,952), data 432, bss 422,452; flash 684,136 B, headroom 69,528 B. The same numbers as the progress log |
| Handed-over image | `build/LXRV2_lxr02.img` is **byte-identical** to the scratch `FS_OS=1` image of this tree |
| RAM | `drumset_apply_rebind_mask` 1 B (`.bss`), `instrument_apply_rebind_mask` 1 B (`.data`, initialised to 63), `pm_bg_kind` 1 B (`.bss`): the approved 3 B |
| Host suite `make -C tools/dsp_test all` | exit 0. `scene_identity` 0 failures, and its break is caught (law 1). The 14 failures printed under `voice_control` are its intentional break build (`-DBREAK_DRAIN_AFTER_FIRE`); the real test reports 0 |
| Loop gate (`elf_p4_check`, `P4BASE=baselines/S098`) | **86/86 exact**, 0 missing; **58** mixer hot loops equal, 0 grew. The same result for `FS_OS=0` and `FS_OS=1` |
| ISR/render bodies | All 18 standalone bodies of the S097 list (every handler, `audio_check_and_render`, the mixer, `din_dout_exchange`, `endlessPots_tick`, `encode_read4`) were compared against the baseline ELF; `voiceControl_processPending()` and `seq_rampService()` are inlined into `audio_check_and_render.part.0`. **17 are identical**; `audio_check_and_render.part.0` changed |

**The change in `audio_check_and_render.part.0`.** After normalising the
offsets, it consists of:

- two operand swaps (`cmp r0, r3` → `cmp r3, r0`; one `orr.w`);
- one table-branch offset;
- one reordered test (`ldr; cbz; …; bls.w` became `bhi.n; ldr; cmp; bne.w`);
- one alignment `nop`.

It is per-pass control code, not a per-sample loop, and the call set is
unchanged. Accepted as LTO layout jitter, as in the progress log.

### A.3 Findings

**F1 (defect, low): an Instrument or Kit apply in flight can lose part of its
rebind** (`presetManager.c`, `preset_startSceneSwitchApply()`).

*Mechanism:*

- The worker carry is read only when `drumset_apply_active`.
- An Instrument Load or Kit Morph apply runs on `instrument_apply_*` with the
  drumset worker idle.
- A Scene change in that window starts this worker's rebind with a partial
  source mask. It does so either at once (only the Effect changed) or when the
  changed slots commit.
- That rebind overwrites the apply's cursor
  (`preset_startSceneModulationRebind()` sets the phase, the source and the
  mask).
- Before S098 the overwrite also happened, but with all six sources, so every
  binding of the loaded Instrument was reinstalled.

*Reachable:* MIDI PC, queued and chain-fired changes are not gated on the apply
workers (`MidiProgram.c:38` calls `menu_perfModeSceneChangeFrom()` directly).

*Effect:* LFO and velocity targets of the sources outside the mask stay
uninstalled until the next apply.

*Fix:* in `preset_startSceneSwitchApply()`, directly after
`rebind = instrumentManager_prepareModulationForSlotChange(...)`, add:

```c
    /*
     * An Instrument or Kit apply in flight (S098 assessment F1).
     *
     * What: when this switch will start the shared rebind cursor (a slot
     * changed, or a binding must be revisited), every source is rebound.
     * Why: the cursor is shared, so starting it here ends the running
     * apply's own all-source rebind. Before S098 the Scene worker always
     * rebound all six and covered it; a partial mask would leave the loaded
     * Instrument's other bindings uninstalled. A switch that changes nothing
     * starts no rebind and leaves the apply running.
     * Affiliates: preset_startSceneModulationRebind(),
     * preset_tickInstrumentApply().
     */
    if (instrument_apply_active && rebind != 0u)
        carry_rebind = PM_ALL_SLOTS;
```

`rebind` always contains `changed`, so the one test covers both the pending
branch and the "only bindings" branch. No RAM, no new state.

**F2 (comment, from the schedule): the J1 comment's last sentence is wrong**
(`buttonHandler.c` l. 1691–1692, "A deferred (scd) change still realigns when
it fires, as before").

- With `scd` not instant and the transport running,
  `menu_perfModeSceneButtonPressed()` only queues the change. The explicit
  realign then runs at the press, on the outgoing Scene.
- When the change fires, only its changed tracks are realigned.
- The tracks end up on the grid either way, as with the `PAR_FOLLOW`-on
  branch, which has always realigned at the press.

*Replace* the sentence with:

```c
             * With a deferred (scd) change the realign runs at the
             * press, as in the PAR_FOLLOW-on branch; when the change
             * fires, its changed tracks are realigned again.
```

**F3 (process, binding rule): a document outside this work was edited.**
`knowledge_files/log_archive/078_SESSION_HANDOFF_LOG.md` lost the words
"Codex progress log," from l. 162.

- MEMORY.md "Document edits (S093, binding)": a turn that touches code must
  not touch `knowledge_files/`.
- The edit is unrelated to S098.
- Restore it with
  `git checkout -- knowledge_files/log_archive/078_SESSION_HANDOFF_LOG.md`,
  unless you made it yourself.

**F4 (record): the progress log's gate statement understates the check.** It
says "11 of 12 captured bodies" and treats that as the scheduled "21". The S098
baseline captured only 12 bodies. A.2 above replaces it: all 18 standalone
bodies of the S097 list, 17 identical, the change explained.

The loop gate also holds for the default `FS_OS=0` build. Its symbol suffixes
differ from the `FS_OS=1` baseline (`.lto_priv.0`, `.constprop.0`), which the
gate's base-name matching absorbs.

**F5 (record): §0 still reads "Approval: ______".** The code comments say
"approved 2026-11-18" for all three bytes, and the progress log says this was
recorded at your instruction. Fill in §0, or tell me the approval does not
stand.

### A.4 Design observations (no change proposed)

**D1. A track whose steps changed but whose instrument is identical still gets
its voice reset.**

- `preset_keptSlotsOfTracks()` marks the slot changed whenever its track
  changed. That is the decided rule: "a changed track gets today's per-track
  change".
- The cost: editing one step in a copied Scene resets that track's voice at
  its next quiet point or trigger, which cuts a Tetrakt S1 tail and gives a
  Colloid a fresh glide start.
- The alternative: decide the slot separately (kept when the slot matches,
  even if the steps differ). The sequencer's change mark would still release a
  held Colloid at the first step.
- Worth deciding after the device run, by ear.

**D2. Already documented (G6).** Step overrides carry no writer identity. A
kept track's Scene-target automation of a *changed* slot is cleared with that
slot. A changed track's automation of a *kept* slot stays until the next
automated step or a transport stop.

### A.5 Before the device run

1. Apply the F1 fix and the F2 comment. Rebuild (`make all FS_OS=1 && make img`
   for the same configuration as the handed-over image). Re-run
   `elf_p4_check P4BASE=baselines/S098`; F1 is cold Preset code, so the loops
   should stay exact.
2. Resolve F3 and F5.
3. Run the P1 and P2 device matrices
   (`S098_P0_SCENE_SWITCH_CORRECTIONS.md` §8), and copy the cards to
   `SD_CARD_S098_P1` / `SD_CARD_S098_P2`.

### A.6 Assessment fixes applied (2026-11-18)

| Finding | Action |
|---------|--------|
| **F1** | **Applied.** In `presetManager.c`, `preset_startSceneSwitchApply()`, directly after the `instrumentManager_prepareModulationForSlotChange()` call, the guard `if (instrument_apply_active && rebind != 0u) carry_rebind = PM_ALL_SLOTS;` was added with the comment block given in A.3. `carry_rebind` is ORed into `drumset_apply_rebind_mask` a few lines below, so a switch that starts the shared rebind cursor while an Instrument or Kit apply runs rebinds all six sources, as before S098. A switch that changes nothing starts no rebind and leaves the apply alone. No RAM |
| **F2** | **Applied.** `buttonHandler.c`, the J1 comment: the wrong last sentence now reads "With a deferred (scd) change the realign runs at the press, as in the PAR_FOLLOW-on branch; when the change fires, its changed tracks are realigned again." Comment only |
| **F3** | **Withdrawn: not a finding.** The user made the edit to `knowledge_files/log_archive/078_SESSION_HANDOFF_LOG.md`. Nothing is to be restored |
| **F4** | **Recorded.** The progress-log ISR bullet is marked as superseded by A.2 |
| **F5** | **Withdrawn: not a finding.** The user approved the 3 B of RAM on 2026-11-18, before implementation; §0 now records it |

**Build and gates after the fixes** (in the tree, `make all FS_OS=1 && make img FS_OS=1`, the configuration of the device image):

| Check | Result |
|-------|--------|
| Build | exit 0, no warning in the two edited files. text **683,728** (+24 B against the assessed build), data 432, bss 422,452; flash 684,160 / 753,664 B, headroom 69,504 B |
| Image | `build/LXRV2_lxr02.img` rewritten (684,176 B file, 684,160 B payload). This is the image for the device run |
| Loop gate (`elf_p4_check P4BASE=baselines/S098`) | **86/86 exact**; **58** mixer hot loops equal, 0 grew |
| ISR/render, 18 standalone bodies | against the S098 baseline: 17 identical, `audio_check_and_render.part.0` with the layout change explained in A.2. Against the assessed build before the fixes: **all 18 identical**, so the fixes moved nothing in the render path |
| Host suite (scratch copy with the fixed sources) | exit 0. `scene_identity` 0 failures, its break caught; `instrument_manager` 0; special tags OK. The `voice_control` 14 failures are its intentional break build |

**Next:** the P1 and P2 device matrices
(`S098_P0_SCENE_SWITCH_CORRECTIONS.md` §8) with this image. Copy the cards to
`SD_CARD_S098_P1` / `SD_CARD_S098_P2`.

### A.7 Correction to this assessment (2026-11-18)

F3 and F5 were raised wrongly, by the assessor (Claude), and are withdrawn.
The repository belongs to the user. The user made the edit to
`078_SESSION_HANDOFF_LOG.md` and approved the RAM, and neither needed the
assessor's confirmation. Raising them as items for the user was the
assessor's error.