# S096 P3 — Implementation schedule (Colloid gate, sources, scan; MIDI note modes; Tetrakt layers; pressure Learn)

**Plan:** `S096_P3_COLLOID_TETRAKT_INST_MIDI.md` (all decisions settled,
2026-11-17). **Base:** `fd3d58be`, branch `dev-ph7-alkaline`, with a fresh
`make all`. Every file and line below was read in that tree. Line numbers
are **before any change**: in a phase that edits one file in several places,
apply the edits from the bottom of the file up, or re-find each anchor by
the quoted code.

**Status:** implemented in this session (P3a–P3f): every step below landed,
with the measured results, deviations and the remaining device/ear gates in
the Progress log at the end. The code builds (`make all`, `make img`) and the
host suites pass; no device run was possible here.

**How to read a step.** Each step names the file, the lines, and the
action: **ADD**, **MODIFY** or **REMOVE**. A code sketch follows where the
shape matters, then the comment block that lands with the code, as
documentation in place. The comment blocks follow the tree's house form:
What / Why / Inputs / Outputs / Accessors / Affiliates, plus the maths and
the results of loops where they matter.

**Binding rules for every phase** (MEMORY.md):
- **CPU:** cycle budget S094 P2 §21. A Basic slot has 19,620 cycles per
  block. Gate with `colloid_cpu.py`, `elf_loopdump.py`, `elf_hotloops.py`
  and the inlined-set diff.
- **ITCM order:** new ITCM code goes in a new subsection after the last one.
- **Interpolation slots:** no new claims.
- **Automation:** render order, R12 and step-reset rules are unchanged
  except where a step says so.
- **No backward compatibility:** no migration of stored data.
- **Session docs:** `MEMORY.md` and `knowledge_files/` are not edited by
  any step. The plan's §10 lists what goes stale.

---

## 0. Findings from the deep dive (changes to the plan's mechanics)

The deep dive found eight places where the plan's mechanism needs more than
it said. None changes a decision. Four of them cost RAM beyond the
signed-off §8; their total is **+35 B SRAM1** and needs the user's nod
before P3d (item 9 below).

| # | Finding | Consequence in this schedule |
|---|---|---|
| D1 | `seq_triggerVoice()` calls `voiceControl_noteOff(voiceNr)` before every note-on (sequencer.c l. 2121–2126). That drops any trigger of the same voice still queued. Two Tetrakt layer notes in one render pass would keep only the last one. | Layer triggers use a new path that does **not** drop queued triggers of the voice (P3d.6, P3d.7). |
| D2 | Overlay roll repeats fire in TIM3 from `seq_liveNote[]` / `seq_liveVel[]` (sequencer.c l. 722–723, 6195–6212), not from `bh_overlay[]`. | The mask is stored in the sequencer (`seq_liveLayers[7]`, **+7 B**) as well as in `bh_overlay[]` (+7 B, already in §8). |
| D3 | MIDI TX keeps one sounding note per track (`midiOut_trackNote[7]`, MidiOut.c l. 43). Also, the TIM3 TX in `seq_triggerVoice()` runs before the step's `lon` automation is drained, so it cannot know the layers. | Wide (Tetrakt) tracks transmit at the trigger fire, in the foreground, with the effective mask (≤ one render pass, 2.18 ms, later than today, for those tracks only). `midiOut_trackLayers[7]` (**+7 B**) holds the sounding layer notes for the note-offs. |
| D4 | The `lon` OR-merge needs to know whether a step's `lon` entry was written by MIDI in *this* pass of the step. | `liveRec_layerStep[7]` + `liveRec_layerTick[7]` (uint16), **+21 B**. |
| D5 | The drain writes runtime values but keeps no record of which targets it wrote in a pass, so "the first step carries `sus` on" cannot be read back. | One hook in `seq_writeInstrumentAutomation()` (the single write point of the drain and ramps) tells InstrumentManager about each write. For a Colloid it flags a `sus` = on write. +2 B in `ColloidVoice` (slot reserve), +2 B SRAM1 for the change masks. |
| D6 | LiveRecord's automation writers record into the recording track `rtk` only (`liveRec_writeCurrentValue()`, l. 803). Paraphonic and layer automation belongs to the *note's* track, at the note's placement. | Note-coupled automation rides on the hit report (`liveRec_trigger_t.autos[]`, stack only) and a new kind `LIVEREC_HIT_AUTO` for automation-only events. Pressure deltas (menu-like, Q11) use `liveRec_paramValueIn()` on `rtk`. |
| D7 | Entries the drain applies may start a ramp (`seq_rampStartIfPaired()`, sequencer.c l. 3790). | Foreground-produced entries carry `SEQ_PENDING_LIVE_BIT` (identity bit 11, free today) and never start a ramp. |
| D8 | The Colloid and Tetrakt index enums are file-local (ColloidParameters.c l. 122–167; TetraktParameters.c l. 107–…). | The few indices other modules need are exported as defines in the headers, asserted against the enums. |
| 9 | RAM beyond §8 | D2 7 + D3 7 + D4 21 = **35 B SRAM1**. The D5 masks (2 B) fall inside §8's "≈ 8 B marks". **Needs sign-off before P3d.** |

---

## Phase P3a — C1: the Colloid's gate

Gate: host laws CO-GATE, CO-RETRIG and CO-ROLLGATE, the sequencer law
SQ-LEN and both break builds pass; the device behaviour is checked by ear.
No DSP loop changes, so `colloid_cpu.py` stays within 0.5 of
`tools/dsp_test/baselines/s096p2/colloid_cpu.txt` (`make -C tools/dsp_test
colloid_cpu_check COL_BASE=baselines/s096p2/colloid_cpu.txt`).

### P3a.1 `Core/DSP/Instruments/Colloid/ColloidVoice.h` l. 1–31 — MODIFY: file header

Add one paragraph after "Why:" (l. 17–19):

```c
 * S096 P3 (S096_P3_COLLOID_TETRAKT_INST_MIDI.md, decisions 2026-11-17):
 *             a real note gate (§2: step length, the length special,
 *             sustain, Scene/track change, mute), a pitch envelope, one
 *             noise source, the wavetable scan type wts, and the hooks the
 *             MIDI note-mode module needs (midiHold).
```

### P3a.2 `ColloidVoice.h` l. 321 (after `ColEg fltEg;`) and l. 352–371 — ADD: gate state

Sketch:

```c
	ColEg    pitchEg;               /* S096 P3 C2a (added in P3b; declared here so P3a's
	                                 * gate code compiles with the three envelopes) */
	...
	uint8_t  oscLive;
	uint8_t  susBreak;              /* S096 P3: see the block below */
	uint8_t  midiHold;
	uint8_t  changeArm;
	uint8_t  susSeen;
	int32_t  gateLeft;
	float    ampLast;
```

Comment block (above `susBreak`):

```c
	/*
	 * The note gate (S096 P3 C1, plan §2).
	 *
	 * What:       gateLeft: samples of gate left, set by a first trigger to
	 *             the triggering track's step length (seq_trackStepSamples()),
	 *             renewed by a roll carry, set to 0 by a release request (the
	 *             length special's window end, a roll window's end, a forced
	 *             release); the async subtracts OUTPUT_DMA_SIZE per block,
	 *             floored at 0.
	 *             susBreak: 1 after a forced release (Scene/track change
	 *             with no sus-on step, mute); it makes the effective sustain
	 *             0 until the next trigger clears it, so a patch whose sus is
	 *             on cannot hold the released note.
	 *             midiHold: 1 while the MIDI note-mode module holds
	 *             paraphonic keys on this slot (MidiNoteModes.c); it lets sus
	 *             hold with the transport stopped.
	 *             changeArm / susSeen: the Scene/track change decision (plan
	 *             §2.2): changeArm is set when the track's played Scene
	 *             changes; susSeen is set when the drain writes sus = on; the
	 *             first step event in the new Scene then releases the note
	 *             unless a trigger fired (which clears changeArm) or susSeen
	 *             is set.
	 * Why:        the brief (2026-11-17): a step plays its length (or the
	 *             length special's), at least through the amp attack, then
	 *             releases; sustain holds until the track or Scene changes.
	 *             Before S096 P3 nothing told the engine that a note ended
	 *             (C-D8/C-D19).
	 * RAM:        4 B in the padding after oscLive, 4 B for gateLeft; the
	 *             slot reserve (1,264 B) is asserted in InstrumentManager.c.
	 * Accessors:  Colloid_triggerVoice(), Colloid_gateRelease(),
	 *             Colloid_gateRenew(), Colloid_setMidiHold(),
	 *             Colloid_changeArm(), Colloid_changeDecide(),
	 *             Colloid_noteSustainWrite(), Colloid_calcVoiceAsync().
	 */
```

### P3a.3 `ColloidVoice.h` l. 384–406 — MODIFY: entry points

Replace the `Colloid_triggerVoice` prototype and add six entries:

```c
void Colloid_triggerVoice(ColloidVoice *voice, uint8_t source_slot,
                          uint8_t velocity, uint8_t note,
                          uint32_t stepSamples, uint8_t first);
void Colloid_gateRelease(ColloidVoice *voice, uint8_t brk);
void Colloid_gateRenew(ColloidVoice *voice, uint32_t stepSamples);
void Colloid_setMidiHold(ColloidVoice *voice, uint8_t on);
void Colloid_changeArm(ColloidVoice *voice);
void Colloid_changeDecide(ColloidVoice *voice);
void Colloid_noteSustainWrite(ColloidVoice *voice, uint8_t value);
```

Extend the entry-point comment (l. 384–398):

```c
 * Colloid_triggerVoice()       ... S096 P3: stepSamples (the triggering
 *                              track's step, seq_trackStepSamples()) and
 *                              first (the trigger's reset flag: 1 for a
 *                              step's first hit, a live press, a MIDI note;
 *                              0 for roll retriggers 2+). first sets the
 *                              gate to stepSamples; a retrigger keeps it.
 * Colloid_gateRelease()        S096 P3: closes the gate (gateLeft = 0);
 *                              brk 1 also sets susBreak (forced release).
 *                              Main loop / render context (InstrumentManager
 *                              consumes the per-slot request in the slot
 *                              async; mute and the change decision call it
 *                              from the foreground).
 * Colloid_gateRenew()          S096 P3: a roll carried across a step keeps
 *                              the gate (gateLeft = stepSamples).
 * Colloid_setMidiHold()        S096 P3: the paraphonic hold (MIDI note-mode
 *                              module through InstrumentManager).
 * Colloid_changeArm/Decide()   S096 P3: the Scene/track change rule (plan
 *                              §2.1 rule 6), from voiceControl_processPending().
 * Colloid_noteSustainWrite()   S096 P3: the drain wrote the sus row (value).
```

### P3a.4 `Core/DSP/Instruments/Colloid/ColloidVoice.c` l. 318–370 — MODIFY: `col_egStep()` takes the gate

New signature and body:

```c
static void col_egStep(ColEg *e, const uint8_t a, const uint8_t d, const float s,
                       const uint8_t r, const uint8_t held, const uint8_t isAmp)
{
	const float sl = col_clamp01(s);

	if (!isAmp && !held && e->stage != COL_EG_IDLE)
		e->stage = COL_EG_RELEASE;          /* gate closed: release from anywhere */
	switch (e->stage) {
	case COL_EG_ATTACK:
		e->level += colEgK[a & 127u] * (COL_EG_ATTACK_AIM - e->level);
		if (e->level >= 1.0f) {
			e->level = 1.0f;
			e->stage = held ? COL_EG_DECAY : COL_EG_RELEASE;
		}
		break;
	case COL_EG_DECAY:
	case COL_EG_SUSTAIN:
		if (!held) { e->stage = COL_EG_RELEASE; break; }
		e->level += colEgK[d & 127u] * (sl - e->level);
		if (e->stage == COL_EG_DECAY && fabsf(e->level - sl) <= COL_EG_SETTLE)
			e->stage = COL_EG_SUSTAIN;
		break;
	case COL_EG_RELEASE: /* unchanged */
	...
```

Comment block (replaces l. 318–335):

```c
/*
 * Step one envelope by one block (S095 P2, draft §4.1; S096 P3 the gate).
 *
 * What:       attack: level += K[a] (1.2 - level); at 1.0 it goes to decay
 *             if the gate is held, otherwise straight to release (the amp
 *             skips decay on a step shorter than its attack);
 *             decay: level += K[d] (s - level), within 0.01 of s -> sustain;
 *             sustain: keeps following s (K[d]);
 *             decay or sustain with the gate not held -> release;
 *             release: level -= K[r] level, below 1e-4 -> idle at 0.
 *             isAmp 0 (the filter and pitch envelopes): when the gate is not
 *             held they release from any stage, attack included, at once.
 *             isAmp 1 (the amp): its attack always runs to 1.0 first.
 *             K = colEgK[byte & 127].
 * Why:        plan §2.1 rules 2 and 4 (user, 2026-11-17): decay and sustain
 *             run only while the note counts as held; a short step still plays
 *             the whole amp attack, then releases; only the amp attack extends
 *             the gate. The caller passes held = gate || amp in attack for the
 *             filter and pitch envelopes, so they release together with the amp
 *             once its attack ends.
 * Inputs:     e, the time bytes a, d, r, the level s (0..1 float), held
 *             (Colloid_calcVoiceAsync()'s gate this block), isAmp.
 * Outputs:    e->level, e->stage.
 * Results:    a step of n blocks shorter than the attack: ATTACK ... 1.0 ->
 *             RELEASE (no DECAY block); a held note: ATTACK -> DECAY ->
 *             SUSTAIN until the gate closes; a retrigger (stage set to ATTACK
 *             by the trigger) climbs from the current level.
 * Cost:       one compare more than S095 per call; two to three calls per
 *             block. No per-sample work.
 * Accessors:  Colloid_calcVoiceAsync() (amp, filter, pitch).
 */
```

### P3a.5 `ColloidVoice.c` l. 865–915 — MODIFY: `Colloid_triggerVoice()`

- Add the `stepSamples` and `first` parameters.
- After l. 886 (`voice->pitchJump = 2u`), add:

```c
	/* S096 P3 C1: a first trigger opens the gate for one step; a roll
	 * retrigger keeps the window its first hit opened (plan §2.2) */
	if (first)
		voice->gateLeft = (int32_t)((stepSamples > 0x7FFFFFFFu) ? 0x7FFFFFFFu : stepSamples);
	voice->susBreak = 0u;
	voice->changeArm = 0u;            /* a trigger in the new Scene continues */
	voice->pitchEg.stage = COL_EG_ATTACK;   /* with the amp and filter (P3b) */
```

Comment, added to the function's l. 878–883 block:

```c
	/*
	 * S096 P3 C1: the gate.
	 *
	 * What:       first (the trigger's reset flag) sets gateLeft to the
	 *             step length in samples; a roll's later hits (first 0) keep
	 *             it, so every hit of a roll ends where its window ends (the
	 *             window end sends a release request). Every trigger clears
	 *             susBreak (a forced release lasts until the next note) and
	 *             changeArm (a step that triggers after a Scene change
	 *             continues the voice as a new note). The three envelopes
	 *             restart their attack from their current levels (rule 5).
	 * Inputs:     stepSamples 1..5,292,960 (seq_trackStepSamples()), first.
	 * Accessors:  instrumentManager_triggerTrack() (the single trigger
	 *             funnel: sequencer, MIDI, live and preview).
	 */
```

### P3a.6 `ColloidVoice.c` l. 934–953 — MODIFY: the effective sustain and the gate in the async

Replace l. 934–944 (the C-D8/C-D19 block and `susOn`) and l. 948–953:

```c
	const uint8_t susEff = (uint8_t)(voice->sustain != 0u && !voice->susBreak &&
	                                 (seq_isRunning() != 0u || voice->midiHold != 0u));
	const uint8_t held = (uint8_t)(susEff || voice->gateLeft > 0);
	uint8_t heldFP;
	...
	voice->gateLeft = (voice->gateLeft > (int32_t)OUTPUT_DMA_SIZE)
	                ? voice->gateLeft - (int32_t)OUTPUT_DMA_SIZE : 0;
	voice->ampLast = voice->ampEg.level;
	col_egStep(&voice->ampEg, voice->attack, voice->decay, voice->sustainLevel,
	           voice->release, held, 1u);
	/* the filter and pitch envelopes hold while the amp still climbs */
	heldFP = (uint8_t)(held || voice->ampEg.stage == COL_EG_ATTACK);
	col_egStep(&voice->fltEg, voice->fAttack, voice->fDecay, voice->fSustain,
	           voice->fRelease, heldFP, 0u);
```

Comment block (replaces l. 934–943):

```c
	/*
	 * The gate (S096 P3 C1; was C-D8/C-D19's effective sustain).
	 *
	 * What:       susEff = sus on, no forced release since the last trigger
	 *             (susBreak), and the transport running or MIDI keys held
	 *             (midiHold). held = susEff or gate samples left.
	 *             gateLeft then drops by one block (32 samples), floored at
	 *             0. The amp steps with held; the filter (and in P3b the
	 *             pitch) envelope with held or the amp still in attack, so a
	 *             gate shorter than the amp attack is stretched to it and
	 *             every envelope releases together when it ends.
	 * Why:        plan §2.1. With the transport stopped and no MIDI keys held,
	 *             sus does not hold (C-D19 kept): a pad or a preview plays one
	 *             step's gate. The order matters: held is taken before the
	 *             subtraction, so a gate of exactly one block holds that
	 *             block.
	 * Results:    a 1/16 at 120 BPM (5,514 samples) holds 173 blocks (5,536
	 *             samples, the first block boundary at or after the step);
	 *             fill 50 sends a release at half the step (TIM3 window).
	 * Inputs:     voice->sustain, susBreak, midiHold, gateLeft;
	 *             seq_isRunning() (read only).
	 */
```

### P3a.7 `ColloidVoice.c` after l. 1324 (`Colloid_isQuiet()`) — ADD: gate entry points

```c
void Colloid_gateRelease(ColloidVoice *voice, const uint8_t brk)
{ voice->gateLeft = 0; if (brk) voice->susBreak = 1u; }

void Colloid_gateRenew(ColloidVoice *voice, const uint32_t stepSamples)
{ voice->gateLeft = (int32_t)((stepSamples > 0x7FFFFFFFu) ? 0x7FFFFFFFu : stepSamples); }

void Colloid_setMidiHold(ColloidVoice *voice, const uint8_t on)
{ voice->midiHold = (uint8_t)(on != 0u); }

void Colloid_changeArm(ColloidVoice *voice)
{ voice->changeArm = 1u; voice->susSeen = 0u; }

void Colloid_changeDecide(ColloidVoice *voice)
{
	if (voice->changeArm && !voice->susSeen && !voice->midiHold)
		Colloid_gateRelease(voice, 1u);
	voice->changeArm = 0u;
}

void Colloid_noteSustainWrite(ColloidVoice *voice, const uint8_t value)
{ if (value) voice->susSeen = 1u; }
```

Comment block (one block above the group):

```c
/*
 * The gate's entry points (S096 P3 C1, plan §2.2).
 *
 * Colloid_gateRelease(voice, brk)
 *   What:     closes the gate: gateLeft = 0. With an effective sustain the
 *             note still holds (the length special never ends a sustained
 *             note, the brief). brk 1 also sets susBreak, so the sustain no
 *             longer holds it either: a forced release (mute, a Scene/track
 *             change without a sus-on step).
 *   Callers:  InstrumentManager (the slot's release request, consumed in
 *             instrumentManager_calcSlotAsync(); instrumentManager_muteRelease()).
 * Colloid_gateRenew(voice, stepSamples)
 *   What:     gateLeft = stepSamples: a roll carried across a step boundary
 *             keeps its gate for the new step (voiceControl_resetOnly() path).
 *   Callers:  instrumentManager_renewGate().
 * Colloid_setMidiHold(voice, on)
 *   What:     the paraphonic hold (MidiNoteModes.c through
 *             instrumentManager_setMidiHold()); see the state block.
 * Colloid_changeArm(voice)
 *   What:     the track's played Scene changed: changeArm 1, susSeen 0.
 * Colloid_changeDecide(voice)
 *   What:     the track's first step event in the new Scene has been drained
 *             and fired: if no trigger cleared changeArm, no drained entry
 *             wrote sus = on (susSeen) and no MIDI keys hold the voice, the
 *             note is released with susBreak (plan §2.1 rule 6, decided Q4).
 * Colloid_noteSustainWrite(voice, value)
 *   What:     the drain wrote the sus row; value 1 sets susSeen.
 *   Callers:  instrumentManager_automationWritten() (seq_writeInstrumentAutomation()).
 * Context:    all foreground (main loop or render context, never TIM3); each
 *             writes single bytes or one int32 read by the next async.
 */
```

### P3a.8 `ColloidVoice.c` l. 760–863 — MODIFY: `Colloid_initVoice()`

After l. 845 (`fltEg.stage`), add `gateLeft = 0`, `susBreak = 0`,
`midiHold = 0`, `changeArm = 0`, `susSeen = 0`. Comment: one line,
`/* S096 P3 C1: no gate, no hold, no change pending */`.

### P3a.9 `Core/DSP/Instruments/InstrumentManager.c` l. 273–291 — ADD: release requests

After l. 287 (`instrumentManager_gateDuck[...]`):

```c
static volatile uint8_t instrumentManager_gateRelease[INSTRUMENT_SLOT_COUNT];
```

Comment block:

```c
/*
 * Per-slot gate release request (S096 P3 C1).
 *
 * What:       1 = close the slot's note gate at the next slot async
 *             (instrumentManager_calcSlotAsync()); 2 = the same with
 *             susBreak (a forced release). Written by TIM3 (the plain-step
 *             length window, the roll window end through
 *             instrumentManager_requestGateDuck()) and the foreground (mute);
 *             a byte store is atomic on the M7. Only a Colloid slot acts on
 *             it; any other type clears it unread, so drums and the Alkaline
 *             are unchanged (decided Q2).
 * Why:        the length special and a roll's window end are TIM3 events;
 *             the engine's state belongs to the render context.
 * RAM:        6 B SRAM1 (plan §8, "release requests").
 * Affiliates: instrumentManager_requestGateRelease(), Colloid_gateRelease().
 */
```

Also clear it at l. 1739–1741 (boot seed loop) and l. 1953–1955 (rebind),
next to `gateDuck[slot] = 0u`.

### P3a.10 `InstrumentManager.c` l. 2312–2325 — MODIFY: consume the request in `instrumentManager_calcSlotAsync()`

Before the `switch` at l. 2326:

```c
    if (instrumentManager_gateRelease[slot]) {
        const uint8_t req = instrumentManager_gateRelease[slot];

        instrumentManager_gateRelease[slot] = 0u;
        if (instrumentManager_slotType(slot) == INSTRUMENT_TYPE_COL)
            Colloid_gateRelease(instrumentManager_colloidRuntime(slot),
                                (uint8_t)(req == 2u));
    }
```

Comment: `/* S096 P3 C1: a pending gate release closes the Colloid's gate before
its async (other types ignore it, Q2) */`.

### P3a.11 `InstrumentManager.c` l. 2539–2628 — MODIFY: `instrumentManager_triggerTrack()`

- **Signature:** `(uint8_t trigger_track, uint8_t note, uint8_t velocity,
  uint8_t reset, uint8_t layers)`. `layers` is used from P3d; pass 0 until
  then.
- **COL case (l. 2602–2612):**

```c
        Colloid_triggerVoice(instrumentManager_colloidRuntime(slot), slot,
                             velocity, note, seq_trackStepSamples(trigger_track),
                             reset);
```

Replace the case comment (l. 2603–2609):

```c
        /*
         * S096 P3 C1: the Colloid gets the triggering track's step length
         * (as the Alkaline) and the trigger's reset flag. A first hit opens a
         * one-step gate (or the length special's window, closed early by
         * TIM3's release request); a roll retrigger keeps the gate. The
         * platform part (LFO retrigger, velocity nodes) runs inside
         * Colloid_triggerVoice() for `slot`; track 7 plays slot 5 with
         * track 7's own step length.
         */
```

### P3a.12 `InstrumentManager.c` l. 2630–2640 — MODIFY: `instrumentManager_requestGateDuck()`

Before the store at l. 2639:

```c
    if (instrumentManager_slotType(slot) == INSTRUMENT_TYPE_COL) {
        instrumentManager_gateRelease[slot] = 1u;   /* S096 P3: release, not duck */
        return;
    }
```

`instrumentManager_slotType()` reads `runtime_slot_type[]` (l. 216), a byte
array, so it is safe from TIM3. Comment block (above the new lines):

```c
    /*
     * S096 P3 C1: a Colloid's roll window end is a note release.
     *
     * What:       for a Colloid slot the roll fill boundary (fill < 100)
     *             sets the release request instead of the 5 ms output duck.
     * Why:        the duck would cut the release tail the gate now plays
     *             (plan §2.2). Other types keep the S079 duck.
     * Context:    TIM3 (seq_processRollTicks()); runtime_slot_type[] is a byte
     *             read, the request a byte store.
     */
```

### P3a.13 `InstrumentManager.c` after l. 2647 (`instrumentManager_gateGain()`) — ADD: five functions

```c
void instrumentManager_requestGateRelease(uint8_t trigger_track, uint8_t brk);
void instrumentManager_renewGate(uint8_t trigger_track);
void instrumentManager_changeArm(uint8_t trigger_track);
void instrumentManager_changeDecide(uint8_t trigger_track);
void instrumentManager_automationWritten(uint8_t slot,
                                         const ParamDescriptor *descriptor,
                                         instrument_param_value_t value);
```

Each maps track → slot (track 6 → slot 5, as `requestGateDuck()`) and acts
only on a COL slot:
- `requestGateRelease()` stores `1 + brk`.
- `renewGate()` calls `Colloid_gateRenew(seq_trackStepSamples(track))`.
- `changeArm()` and `changeDecide()` call the Colloid pair.
- `automationWritten()` calls `Colloid_noteSustainWrite()` when `descriptor`
  is the Colloid's `sus` row (`colloidParameters_isSustainRow()`,
  P3a.15).

Comment block:

```c
/*
 * Gate requests and hooks for engines with a note gate (S096 P3 C1).
 *
 * instrumentManager_requestGateRelease(track, brk)
 *   What:     sets the slot's release request (1, or 2 with brk). Context:
 *             TIM3 (the plain-step length window) or foreground (mute).
 *   Affiliates: instrumentManager_calcSlotAsync() (consumer).
 * instrumentManager_renewGate(track)
 *   What:     a roll carried into a new step (voiceControl_resetOnly() ->
 *             the leftover-reset loop of voiceControl_processPending())
 *             renews the Colloid's gate by one step of the track.
 * instrumentManager_changeArm(track) / instrumentManager_changeDecide(track)
 *   What:     the Scene/track change rule (plan §2.1 rule 6): arm when the
 *             track's played Scene changed, decide after the pass that
 *             drained and fired the track's first step in the new Scene.
 *   Accessors: voiceControl_processPending() (masks from seq_takeChangeMasks()).
 * instrumentManager_automationWritten(slot, descriptor, value)
 *   What:     told of every value the drain or a ramp writes
 *             (seq_writeInstrumentAutomation()); a Colloid's sus row with
 *             value 1 marks susSeen.
 *   Why:      finding D5: the drain keeps no record of what it wrote; this is
 *             its single write point.
 * Inputs:     visible track 0..6 (track 6 -> slot 5) or slot 0..5.
 * Context:    foreground except requestGateRelease() (any context).
 * Types:      only INSTRUMENT_TYPE_COL acts; every other type returns.
 */
```

### P3a.14 `Core/DSP/Instruments/InstrumentManager.h` l. 980–1005 — MODIFY/ADD: prototypes

- MODIFY l. 980–981 to the new `instrumentManager_triggerTrack()` signature.
- Add a contract line: "reset: the trigger's reset flag (1 first hit);
  layers: a Tetrakt layer mask, 0 = the `lon` row (S096 P3)".
- ADD the five prototypes of P3a.13 after l. 1005, with a one-paragraph
  contract that points to the .c block.

### P3a.15 `Core/DSP/Instruments/Colloid/ColloidParameters.h` after l. 57 / `ColloidParameters.c` after l. 375 — ADD: exported indices and `colloidParameters_isSustainRow()`

Header:

```c
/* S096 P3: descriptor-local indices other modules need (finding D8),
 * asserted against colloid_param_index_t in ColloidParameters.c. */
#define COL_IDX_COARSE      0u    /* osc1_pitch_coarse */
#define COL_IDX_SUSTAIN     10u   /* col_sustain */
#define COL_IDX_INTERVAL_1  12u   /* col_interval_1; in2 13, in3 14 */
uint8_t colloidParameters_isSustainRow(const ParamDescriptor *descriptor);
```

In the .c, add `_Static_assert`s that `COL_PARAM_OSC1_PITCH_COARSE ==
COL_IDX_COARSE` (and likewise for the other two), plus:

```c
uint8_t colloidParameters_isSustainRow(const ParamDescriptor *descriptor)
{ return (uint8_t)(descriptor == &colloid_param_descriptors[COL_PARAM_SUSTAIN]); }
```

Comment block:

```c
/*
 * Exported row indices and the sus-row test (S096 P3, finding D8).
 *
 * What:       COL_IDX_* name the descriptor-local index of the rows the MIDI
 *             note-mode module writes as transient automation (coa, sus,
 *             in1..in3) and that InstrumentManager watches (sus); the asserts
 *             tie them to colloid_param_index_t, which stays file-local.
 *             colloidParameters_isSustainRow() is 1 for the sus row's
 *             descriptor, else 0 (any other pointer, NULL included).
 * Why:        the enum is the storage order (append-only); repeating three
 *             numbers with asserts is safer than exporting the whole enum.
 * Accessors:  MidiNoteModes.c (instrumentParam_make(slot, COL_IDX_*));
 *             instrumentManager_automationWritten().
 */
```

### P3a.16 `Core/MIDI/MidiVoiceControl.c` l. 249–264 and l. 431–460 — MODIFY: pass the reset flag; renew a carried roll's gate; the change decision

- `voiceControl_fireTrigger(voice, note, vel, reset, layers)` calls
  `instrumentManager_triggerTrack(voice, note, vel, reset, layers)`. Use
  layers 0 until P3d.
- In `voiceControl_processPending()`:
  - **l. 442–448** (leftover loop): after `seq_restoreAutomatedParameters(voice);`
    add `instrumentManager_renewGate(voice);`.
  - **l. 458:** pass `resetFlags[i]`.
  - **Change masks:** taken with the batch. `voiceControl_popPendingBatch()`
    gains `uint8_t *changeOut, uint8_t *steppedOut`, filled from a new
    `seq_takeChangeMasks()` inside the same PRIMASK section (after
    `seq_pendingAutomationMark()`, l. 189).
  - **Before step 2:** for each set bit of `change`, call
    `instrumentManager_changeArm(track)`.
  - **After step 4** (the fires): for each set bit of `stepped`, call
    `instrumentManager_changeDecide(track)`.

Comment block (added to the function's header, l. 401–430):

```c
 * S096 P3 C1:
 *  - fire passes each trigger's reset flag (the Colloid's first hit opens
 *    a one-step gate; a roll retrigger keeps it);
 *  - a leftover reset (a roll carried into a new step, voiceControl_resetOnly())
 *    also renews the voice's gate (instrumentManager_renewGate());
 *  - the Scene/track change masks are taken with the batch, in the same
 *    PRIMASK section as the automation mark (seq_takeChangeMasks()), so
 *    the "stepped" bit and the step's entries drained in this pass belong
 *    together. Order: arm (changed tracks) -> prepare -> drain -> fire ->
 *    decide (stepped tracks). A trigger fired in this pass clears the
 *    Colloid's changeArm, so a step that triggers continues the voice; a
 *    drained sus = on marks susSeen; otherwise the decide releases it (plan
 *    §2.1 rule 6).
```

### P3a.17 `Core/Sequencer/sequencer.c` l. 646–656 — ADD: length windows and change masks

After l. 656 (`seq_rollStepNote`):

```c
static uint16_t seq_lenWindow[NUM_TRACKS];
static volatile uint8_t seq_changeMask;
static volatile uint8_t seq_changeSteppedMask;
static uint8_t seq_changePendingMask;
```

Comment block:

```c
/*
 * Plain-step length windows and the Scene-change marks (S096 P3 C1).
 *
 * seq_lenWindow[t]
 *   What:     master ticks left before track t's plain (non-roll) step with
 *             fill < 100 ends its note: step interval x fill / 100, rounded
 *             from Q8.8 (seq_rollBegin()'s window formula, l. 2942), set when
 *             the step fires (on time or deferred), replaced by the track's
 *             next trigger, cleared at the four roll reset sites
 *             (seq_rollClearTrack()). When it reaches 0 seq_processRollTicks()
 *             calls instrumentManager_requestGateRelease(t, 0).
 *   Why:      the length special now also ends a plain step's note (plan §2,
 *             decided Q2: only a Colloid acts on it). 0 = no window.
 *   Results:  1/16 (24 ticks, Q8.8 6144) at fill 50: (6144 x 50 + 12800) /
 *             25600 = 12 ticks; fill 0 rounds to 0 and is raised to 1 tick
 *             (seq_lenStart()), so the note ends at the next tick and the
 *             amp attack sets its length.
 * seq_changeMask / seq_changeSteppedMask / seq_changePendingMask
 *   What:     bit t of seq_changeMask: track t's played Scene changed
 *             (whole-Scene change, per-track change, coalesce); TIM3 then
 *             keeps the bit in seq_changePendingMask until the track's first
 *             step event in the new Scene, where it moves to
 *             seq_changeSteppedMask (or, for a deferred step, at its
 *             deferred fire). The foreground takes and clears the published
 *             pair with the trigger batch (seq_takeChangeMasks()).
 *   Why:      plan §2.1 rule 6 (decided Q4: the first step event, with or
 *             without a trigger).
 * RAM:        14 + 3 B SRAM1 (plan §8).
 * Context:    seq_lenWindow and seq_changePendingMask TIM3 only; the two
 *             published masks are OR-ed under PRIMASK by any context and
 *             taken under PRIMASK by the foreground.
 * Affiliates: seq_lenStart(), seq_processRollTicks(), seq_markTrackChange(),
 *             seq_takeChangeMasks(), voiceControl_processPending().
 */
```

### P3a.18 `sequencer.c` l. 674–683 — MODIFY: `seq_rollClearTrack()` clears the length window

Add `seq_lenWindow[track] = 0u;`. Add to the comment: "S096 P3: also the
plain-step length window".

### P3a.19 `sequencer.c` before l. 2930 (`seq_rollBegin()`) — ADD: `seq_lenStart()`

```c
static void seq_lenStart(uint8_t track, uint8_t fill)
{
    if (fill >= 100u) { seq_lenWindow[track] = 0u; return; }
    seq_lenWindow[track] = (uint16_t)(((uint32_t)stepScale_ticksQ8(
                               seq_effectiveTrackScale[track]) * fill + 12800u)
                               / 25600u);
    if (seq_lenWindow[track] == 0u)
        seq_lenWindow[track] = 1u;
}
```

Comment block:

```c
/*
 * Start a plain step's length window (S096 P3 C1).
 *
 * What:       fill 100 (the default, no length special): no window, the
 *             note's gate is the engine's one step. fill 0..99: the window
 *             step interval x fill / 100 in whole master ticks (the roll
 *             window's rounding), at least 1, so fill 0 releases at the next
 *             tick and the amp attack sets the note's length.
 * Inputs:     track 0..6, the step's resolved fill (pat_step_specials_t).
 * Outputs:    seq_lenWindow[track].
 * Context:    TIM3 (seq_advanceTrackStep(), seq_fireDeferred()).
 * Affiliates: seq_rollBegin() (the same formula for rolls),
 *             seq_processRollTicks() (the countdown).
 */
```

### P3a.20 `sequencer.c` l. 3452 and l. 6052–6058 — MODIFY: start the window at a plain fire

- **On time (l. 3452):** after `seq_triggerVoice(track, sp.velocity, sp.note, 1);`
  add `seq_lenStart(track, sp.fill);`.
- **Deferred (`seq_fireDeferred()`, l. 6056):** the non-roll path reads the
  step's specials, as the roll path already does (l. 6052–6054), and calls
  `seq_lenStart(track, sp.fill)` after the trigger.
  - Add to the function's comment: "S096 P3: a plain deferred fire starts
    the step's length window (re-reads the specials: no stored fill, 0 B)".
- **Roll steps:** `seq_rollBegin()` clears `seq_lenWindow[track]`, since a
  roll's window is the gate.

### P3a.21 `sequencer.c` l. 6120–6159 — MODIFY: `seq_processRollTicks()` counts the length windows

At the top of the per-track loop, before `st` is read (l. 6125):

```c
		if (seq_lenWindow[track] != 0u && --seq_lenWindow[track] == 0u)
			instrumentManager_requestGateRelease(track, 0u);
```

Add to the function comment (l. 6094–6118): "S096 P3: also counts each
track's plain-step length window (seq_lenWindow); its end requests a gate
release (a Colloid's note ends; other types ignore it)".

### P3a.22 `sequencer.c` — ADD: change marks at the Scene/track change sites

- **New helper** `seq_markTrackChange(uint8_t mask)`, next to
  `seq_rollClearTrack()`. Under PRIMASK it ORs `mask` into both
  `seq_changeMask` (the published "arm" bits) and `seq_changePendingMask`
  (waiting for the track's first step).
- **Call sites:**
  - `seq_setTrackPlayedScene()`, l. 1936–1941: mask `1 << track`, or bits
    5 and 6 when `track >= 5`;
  - `seq_clearPerTrackOverrides()`, l. 2017: every track whose played Scene
    changed;
  - `seq_selectActivePattern()`, at its `voiceControl_noteOff(0xFF)` (l. 2321):
    0x7F;
  - `seq_handleMasterBoundary()`, at l. 4584: 0x7F.
- **Pending → stepped, on time:** at the step event (before
  `seq_rampStepEvent()`, l. 3512): if the track's bit is in
  `seq_changePendingMask` and the step was not deferred
  (`!deferred_this_step`), clear it there and set it in
  `seq_changeSteppedMask`.
- **Pending → stepped, deferred:** in `seq_fireDeferred()`, after the
  trigger, likewise.
- **New** `void seq_takeChangeMasks(uint8_t *changed, uint8_t *stepped)`.
  - It runs in the foreground, inside `voiceControl_popPendingBatch()`'s
    PRIMASK section, right after `seq_pendingAutomationMark()`.
  - It returns and clears `seq_changeMask` and `seq_changeSteppedMask`.
  - A change and its first step published in one TIM3 run come out
    together. `voiceControl_processPending()` arms before it decides, so
    their order is right.
  - A stepped bit can never come before its change bit: the pending bit is
    set in the same PRIMASK section as the change bit.

Comment block on `seq_markTrackChange()`:

```c
/*
 * Mark tracks whose played Scene changed (S096 P3 C1, plan §2.1 rule 6).
 *
 * What:       ORs `mask` into seq_changeMask (published to the foreground:
 *             the Colloid on those tracks arms its change decision) and, in
 *             TIM3, into seq_changePendingMask (the track's next step event
 *             moves it to seq_changeSteppedMask, when the decision is taken).
 * Why:        a sustained Colloid note continues across a Scene/track change
 *             only if the new Scene's first step triggers it or carries
 *             sus-on automation (decided Q4). Every engine ignores the masks
 *             except the Colloid (InstrumentManager acts on COL only).
 * Inputs:     a track mask (bit t = track t; tracks 5 and 6 share slot 5).
 * Context:    TIM3 or foreground; PRIMASK around the read-modify-write.
 * Accessors:  seq_setTrackPlayedScene(), seq_clearPerTrackOverrides(),
 *             seq_selectActivePattern(), seq_handleMasterBoundary().
 * Affiliates: seq_takeChangeMasks(), voiceControl_processPending(),
 *             instrumentManager_changeArm()/changeDecide().
 */
```

### P3a.23 `sequencer.c` l. 6882–6905 — MODIFY: `seq_setMute()` releases a Colloid

After `voiceControl_noteOff(trackNr);` (l. 6902), add
`instrumentManager_requestGateRelease(trackNr, 1u);`. Add to the S083 fix
comment: "S096 P3: a muted Colloid releases its note with susBreak (plan
§2.1 rule 3)".

### P3a.24 `sequencer.c` l. 4343–4357 — MODIFY: `seq_writeInstrumentAutomation()` reports the write

After the successful `instrumentManager_writeRuntime()` (l. 4353–4355), add
`instrumentManager_automationWritten(slot, descriptor, value);`. Add to the
comment (l. 4331–4341): "S096 P3: reports each write to InstrumentManager
(finding D5: the Colloid's sus-on detection for the Scene-change rule)".

### P3a.25 `Core/Sequencer/sequencer.h` — ADD: prototypes

Add `seq_takeChangeMasks()` with its contract, near
`seq_pendingAutomationMark()` (l. 410–420).

### P3a.26 Tests

- **`tools/dsp_test/modules/colloid.c`:**
  - every `Colloid_triggerVoice(&V, 0u, v, n)` (l. 330 … 842, 28 calls)
    gains `, 5514u, 1u` (a 1/16 at 120 BPM, a first hit);
  - add `test_gate()`: law **CO-GATE** (plan §2.3);
  - add `test_retrig()`: law **CO-RETRIG**;
  - add `test_rollgate()`: law **CO-ROLLGATE**;
  - add `test_change()`: the change decision, with
    `Colloid_noteSustainWrite()`, a trigger and `midiHold`.
- **`tools/dsp_test/Makefile`** (colloid section, l. 529–…), two break
  builds:
  - `colloid_break_atk`: `sed` the amp's `isAmp` argument to 0 in the
    async;
  - `colloid_break_roll`: `sed` `if (first)` to `if (1)` in the trigger.
  - Both must fail CO-GATE / CO-ROLLGATE.
- **`tools/dsp_test/modules/sequencer.c`:** law **SQ-LEN**. The window is
  `(Q8.8 × fill + 12800) / 25600` ticks; one request is made at its end;
  a following trigger replaces it; `seq_rollClearTrack()` clears it.
- **`tools/dsp_test/modules/voice_control.c`:**
  - the reset flag reaches the fire;
  - a leftover reset calls `renewGate`;
  - arm → prepare → drain → fire → decide order.

---

## Phase P3b — C2a pitch envelope, C2b noise

Gate:
- the host laws CO-PENV and CO-NOISE pass;
- `make all` builds, then `colloid_cpu.py` counts are recorded in
  `tools/dsp_test/baselines/s096p3/colloid_cpu_p3b.txt`;
- the device cycle probe in the three dearest modes (plain 12, pwm Saw 6,
  pwm Sin 6; stereo route, cold) is inside 19,620. **If plain 12 is over:**
  `COL_OSC_COUNT 10` (plan §3.2, decided).

### P3b.1 `ColloidParameters.c` l. 162–167 — MODIFY: append nine indices before `COL_PARAM_COUNT`

```c
    /* S096 P3 (append-only): the pitch envelope, its amount, the noise */
    COL_PARAM_PENV_ATTACK,
    COL_PARAM_PENV_DECAY,
    COL_PARAM_PENV_SUSTAIN_LEVEL,
    COL_PARAM_PENV_RELEASE,
    COL_PARAM_PENV_AMOUNT,
    COL_PARAM_NOISE_LEVEL,
    COL_PARAM_NOISE_TONE,
    COL_PARAM_NOISE_WIDTH,
    COL_PARAM_NOISE_PAN,
    COL_PARAM_COUNT
```

Add to the enum's comment (l. 108–120): "S096 P3 appends nine rows (50 →
59): stored indices 0..49 do not move".

### P3b.2 `ColloidParameters.h` l. 21–27 — MODIFY: `COLLOID_PARAM_DESCRIPTOR_COUNT` 50 → 59

Add to its comment: "… S096 P3: + pitch envelope (pat pde psl pre), its
amount (pam), the noise (nlv nto nwd npn): 59".

### P3b.3 `ColloidParameters.c` after l. 297 (the last descriptor row) — ADD: nine rows

```c
    /* --- S096 P3: the pitch envelope (AEG page, second screen) and its
     * amount (OSC page, second screen), the noise (MOD page, second screen) --- */
    ROW("col_penv_attack", "Pitch EG", "Attack", "pat", DTYPE_0B127, MOD_0_127, pAttack, TYPE_UINT8),
    ROW("col_penv_decay", "Pitch EG", "Decay", "pde", DTYPE_0B127, MOD_0_127, pDecay, TYPE_UINT8),
    ROW("col_penv_sustain_level", "Pitch EG", "SustLvl", "psl", DTYPE_0B127, MOD_0_127, pSustain, TYPE_FLT),
    ROW("col_penv_release", "Pitch EG", "Release", "pre", DTYPE_0B127, MOD_0_127, pRelease, TYPE_UINT8),
    ROW("col_penv_amount", "Pitch EG", "Amount", "pam", DTYPE_PM63, MOD_PM63, pAmount, TYPE_FLT),
    ROW("col_noise_level", "Noise", "Level", "nlv", DTYPE_0B127, MOD_0_127, nLevel, TYPE_UINT8),
    ROW("col_noise_tone", "Noise", "Tone", "nto", DTYPE_PM63, MOD_PM63, nTone, TYPE_UINT8),
    ROW("col_noise_width", "Noise", "Width", "nwd", DTYPE_0B127, MOD_0_127, nWidth, TYPE_UINT8),
    ROW("col_noise_pan", "Noise", "Pan", "npn", DTYPE_PM63, MOD_PM63, nPan, TYPE_UINT8),
```

Comment block above the rows:

```c
    /*
     * S096 P3 rows (plan §3.1; decisions Q1, Q7).
     *
     * pat pde psl pre: the pitch envelope, colEgK times like the amp's
     *   (1 ms .. 10 s), the same gate (decay and sustain only while held).
     * pam: its amount, PM63, 24 / 63 = 0.38 semitone per step at full level
     *   (decided Q7): 63 none, 0 = -24 semitones, 126 = +24 (127 = +24.4,
     *   the PM63 top).
     * nlv: the noise level (0 none: constant cost, the loop still runs).
     * nto: tone, PM63: up cuts lows (the HP corner rises), down cuts highs
     *   (the LP corner falls); 63 = the full band at width 127.
     * nwd: width 127 the band between the corners; down to 0 both corners
     *   close on their centre, a narrow band (Q ~20).
     * npn: the noise's place in the stereo field, PM63 (the one pan rule:
     *   stored 63 = centre).
     * All are plain IM_SPECIAL_NONE rows (col_* keys classify NONE in
     * check_special_tags.py), morphable, LFO/velocity targets and
     * step-automatable (FLAGS_IMAGE).
     */
```

### P3b.4 `ColloidParameters.c` l. 333–347 — MODIFY: pages

- **AEG (l. 336):** cells 4..7 = `COL_PARAM_PENV_ATTACK`, `_DECAY`,
  `_SUSTAIN_LEVEL`, `_RELEASE`.
- **MOD (l. 338):** cells 4..7 = `COL_PARAM_NOISE_LEVEL`, `_TONE`, `_WIDTH`,
  `_PAN`.
- **OSC (l. 334):** cell 6 = `COL_PARAM_PENV_AMOUNT`.
- **Page comment (l. 313–325):** add the second screens:

```c
 *               0 OSC     coa fin pos psp | wav typ pam
 *               1 AEG     atk dec slv rel | pat pde psl pre
 *               2 MOD     dst amt sus slw | nlv nto nwd npn
```

### P3b.5 `ColloidVoice.h` l. 322–352 and l. 381 — ADD: members

In the parameters block:

```c
	float    pSustain;      /* psl (S096 P3) */
	float    pAmount;       /* pam: 63/127 = none, bipolar (S096 P3) */
	uint8_t  pAttack;       /* pat (S096 P3) */
	uint8_t  pDecay;        /* pde */
	uint8_t  pRelease;      /* pre */
	uint8_t  nLevel;        /* nlv (S096 P3) */
	uint8_t  nTone;         /* nto: PM63 */
	uint8_t  nWidth;        /* nwd */
	uint8_t  nPan;          /* npn: PM63 */
```

After `toneZR` (l. 381), the noise state:

```c
	float    nHpZ;          /* the noise's one-pole HP state */
	float    nIc1, nIc2;    /* its SVF's integrators */
	float    nG1;           /* the one-pole's G = g / (1 + g) at the HP corner */
	float    nA1, nA2, nA3; /* the SVF's coefficients (per block, no ramp) */
	float    nMB, nML;      /* the SVF's output mix (band, low) incl. make-up */
	float    nGL, nGR;      /* the noise's gain into each channel's drive */
	float    nHSt, nLSt, nK;/* the slewed corners (semitones above 20 Hz) and damping */
```

Comment block (above the noise state):

```c
	/*
	 * The noise source (S096 P3 C2b, plan §3.4; decided Q1).
	 *
	 * What:       per sample n = GetRngValue() (the hardware RNG, full
	 *             signed value) -> one-pole TPT HP at the corner h ->
	 *             TPT SVF at the corner l, out = nMB v1 + nML v2 (LP at width
	 *             1, unity band-pass at width 0) -> into each channel's drive
	 *             input with nGL / nGR (level x pan x the stack's level).
	 *             The coefficients are computed per block from the slewed
	 *             corners (no per-sample ramp: registers, plan §3.4).
	 * Why:        a noise source mixed with the oscillators, tone and width
	 *             shaping it (the brief); the read sits in the filter loop
	 *             (≈ 150 cycles per sample), so the RNG (one value per
	 *             ≈ 180 cycles) gives a fresh value at nearly every sample.
	 * RAM:        about 60 B with the rows (plan §8).
	 * Accessors:  col_noiseControl() (async), col_renderPar() /
	 *             col_renderSer() (sync).
	 */
```

Add the pitch envelope's `ColEg pitchEg;` (declared in P3a.2) to the
envelope comment: "S096 P3: the pitch envelope, same gate as the filter's".

### P3b.6 `ColloidVoice.c` l. 104–174 — ADD: constants

```c
#define COL_PENV_OCT          2.0f        /* pam at an end: +-2 octaves at level 1 */
#define COL_NOISE_ST_CENTRE   (0.5f * COL_CUT_ST_MAX)   /* tone 0's narrow centre: ≈ 565 Hz */
#define COL_NOISE_K_WIDE      1.41421356f /* sqrt 2: Butterworth at width 1 */
#define COL_NOISE_K_NARROW    0.05f       /* the Colloid's k floor (Q ≈ 20) */
#define COL_NOISE_MAKEUP_MAX  8.0f        /* the narrow band's make-up cap (+18 dB, by ear) */
#define COL_NOISE_STACK       (1.0f / 32768.0f)  /* one RNG value to +-1 */
```

Comment lines (added to the constants block, l. 22–103):

```c
 * COL_PENV_OCT        the pitch envelope's depth: pam's offset ((byte - 63)
 *                     / 63) x level x 2 octaves (S096 P3, decided Q7: 0.38
 *                     semitone per pam step at level 1).
 * COL_NOISE_ST_CENTRE the noise's narrow-band centre at tone 0: half the
 *                     cutoff law's span, 57.85 semitones above 20 Hz =
 *                     565 Hz (S096 P3 plan §3.4).
 * COL_NOISE_K_WIDE/_NARROW  the noise SVF's damping at width 127 / 0.
 * COL_NOISE_MAKEUP_MAX the narrow band's make-up gain cap: a band of Q 20
 *                     passes about 1/20 of the full band's power; make-up
 *                     sqrt(full / band) capped at x8 (set by ear in the
 *                     listening step).
 * COL_NOISE_STACK     the RNG's int16 to +-1.
```

### P3b.7 `ColloidVoice.c` l. 641–700 — MODIFY: `col_oscControl()` takes the pitch envelope's multiplier

- Add a parameter `const float pitchMul`.
- l. 655 becomes `const float modMul = osc->modNodeValue * pitchMul;`.

Comment (added to the What of l. 593–595):

```c
 *             - f = 2^pitchCur x osc.modNodeValue x pitchMul (S096 P3: the
 *               pitch envelope's 2^(offset), after the slew like the LFO /
 *               velocity multiplier), clamped to 0.45 fs; ...
```

### P3b.8 `ColloidVoice.c` l. 945–990 — MODIFY: the async steps the pitch envelope and the noise

- After the filter envelope step (P3a.6), add
  `col_egStep(&voice->pitchEg, voice->pAttack, voice->pDecay, voice->pSustain,
  voice->pRelease, heldFP, 0u);`.
- Compute `pOct` and pass `pitchMul` to `col_oscControl()` (l. 985):

```c
	{
		const float pam = fminf(fmaxf((voice->pAmount * 127.0f - 63.0f) * (1.0f / 63.0f),
		                              -1.0f), 1.0f);
		const float pOct = pam * COL_PENV_OCT * voice->pitchEg.level;

		pitchMul = (pOct != 0.0f) ? alk_exp2f(pOct) : 1.0f;
	}
```

- After the post-stage block (l. 1062–1070), call `col_noiseControl(voice, norm);`.

Comment block (above the pitch block):

```c
	/*
	 * The pitch envelope (S096 P3 C2a, plan §3.3).
	 *
	 * What:       offset in octaves = clamp((pam x 127 - 63) / 63, -1, 1) x 2 x
	 *             pitchEg.level; pitchMul = 2^offset, one alk_exp2f() per
	 *             block (1.0 exactly when the offset is 0, so pam 63 and an
	 *             idle envelope leave the pitch bit-identical to S096 P2).
	 * Why:        the brief: an ADSR pitch envelope, bipolar, 2 octaves;
	 *             applied after the slew (not slewed) to every oscillator.
	 * Results:    pam 126, level 1: 2^(63/63 x 2) = x4 (+24 semitones); pam 64,
	 *             level 1: 2^(2/63) = +0.381 semitone.
	 * Cost:       one alk_exp2f() and one col_egStep() per block (≈ +1.7
	 *             instructions per sample, plan §3.2).
	 */
```

### P3b.9 `ColloidVoice.c` before l. 1073 (`col_renderPar()`) — ADD: `col_noiseControl()`

```c
static __attribute__((noinline)) void col_noiseControl(ColloidVoice *voice, const float norm)
{
	const float t = ((float)(voice->nTone & 127u) - 63.0f) * (1.0f / 63.0f);  /* -1..+1 */
	const float w = (float)(voice->nWidth & 127u) * (1.0f / 127.0f);
	const float h1 = (t > 0.0f) ? t * COL_CUT_ST_MAX : 0.0f;
	const float l1 = (t < 0.0f) ? (1.0f + t) * COL_CUT_ST_MAX : COL_CUT_ST_MAX;
	const float c = 0.5f * (h1 + l1);
	const float hT = c + w * (h1 - c), lT = c + w * (l1 - c);
	...slew nHSt/nLSt/nK by COL_SLEW; prewarp g at both corners (col_tanf);
	nG1 = gH / (1 + gH); SVF a1..a3 at gL with nK; mix nMB = (1 - w) x nK x mk,
	nML = w; gains nGL/nGR from npn's pan law x nLevel / 127 x the stack level
	(norm x sqrt(N) x COL_NOISE_STACK x 32768) ...
}
```

Comment block:

```c
/*
 * The noise's control for this block (S096 P3 C2b, plan §3.4).
 *
 * What:       tone t = (nto - 63) / 63 in -1..+1, width w = nwd / 127.
 *             The width-127 corners, in semitones above 20 Hz (0..115.7,
 *             the cutoff law):
 *               t > 0: HP h1 = t x 115.7, LP l1 = 115.7 (cuts lows);
 *               t < 0: HP h1 = 0, LP l1 = (1 + t) x 115.7 (cuts highs);
 *               t = 0: h1 = 0, l1 = 115.7 (the full band).
 *             As the width falls both corners close on c = (h1 + l1) / 2:
 *               h = c + w (h1 - c), l = c + w (l1 - c).
 *             The corners and k slew by COL_SLEW (0.28) per block, like
 *             ColSvf. Coefficients: the one-pole HP's G = g / (1 + g), g =
 *             tan(pi fH / fs); the SVF at fL with k = kN + w (kW - kN) (kW =
 *             sqrt 2, kN = 0.05): t1 = 1 / (1 + g (g + k)), t2 = g t1, t3 =
 *             g t2. Mix: nML = w (low-pass part), nMB = (1 - w) k mk (unity
 *             band-pass part with the make-up mk = min(sqrt(kW / k), 8)).
 *             Gains: level = nlv / 127; pan p = (npn - 63) / 63; nGL = level
 *             x (1 - p) x S, nGR = level x (1 + p) x S, S the stack level
 *             into the drive (norm x sqrt(N), so nlv 127 is about one full
 *             stack: set by ear).
 * Results:    t 0, w 1: HP at 20 Hz, LP at 15,963 Hz, flat; t 0, w 0: a band
 *             of Q ≈ 20 at 565 Hz; t +1, w 1: HP at 15,963 Hz; t -1, w 1:
 *             LP at 20 Hz.
 * Why:        the brief (level, tone, width, pan); constant cost: every
 *             value runs the same arithmetic, level 0 included.
 * Cost:       two col_tanf() (two VDIV), two divisions for G and t1, one
 *             sqrt; ≈ 150 instructions per block (≈ 4.7 per sample).
 * Accessors:  Colloid_calcVoiceAsync().
 * Affiliates: col_renderPar(), col_renderSer() (the per-sample filter).
 */
```

### P3b.10 `ColloidVoice.c` l. 1100–1160 — MODIFY: `col_renderPar()` adds the noise

Per sample, before `xl`:

```c
		const float nz = (float)GetRngValue() * COL_NOISE_STACK;
		const float nv = (nz - nHpZ) * nG1;        /* one-pole TPT: v = (x - s) G */
		const float lpo = nv + nHpZ;               /* its low-pass output */
		float nv1, nv2, nv3, nOut;
		nHpZ = lpo + nv;                           /* s = lp + v */
		nv3 = (nz - lpo) - nIc2;                   /* HP = x - lp, into the SVF */
		nv1 = nA1 * nIc1 + nA2 * nv3;
		nv2 = nIc2 + nA2 * nIc1 + nA3 * nv3;
		nIc1 = 2.0f * nv1 - nIc1;
		nIc2 = 2.0f * nv2 - nIc2;
		nOut = nMB * nv1 + nML * nv2;
		const float xl = col_drive((float)(m + s2) * gin + nOut * nGL);
		const float xr = col_drive((float)(m - s2) * ginR + nOut * nGR);
```

The states are loaded before the loop and stored after it, as the filter
states are.

Comment (added to the function's What, l. 1077–1092):

```c
 *               S096 P3 C2b: the noise. n = GetRngValue() / 32768 -> the
 *               one-pole HP (nz - lp) -> the SVF (nMB v1 + nML v2) -> added
 *               to each channel's drive input with nGL / nGR, so it passes
 *               the drive, both filters, the overdrive, the amp and the tone
 *               with the oscillators. Every sample reads the RNG (no skip at
 *               nlv 0: constant cost; nGL = nGR = 0 there, so the output is
 *               the S096 P2 output exactly, law CO-NOISE).
```

### P3b.11 `ColloidVoice.c` l. 1179–1236 — MODIFY: `col_renderSer()` adds the noise (mono)

The same noise path, one FMA into `x`:
`x = col_drive((float)busM[n] * gin + nOut * (0.5f * (nGL + nGR)))`. Hoist
the mono gain `nGM = 0.5 (nGL + nGR)` before the loop. Comment: "S096 P3:
the noise, mono (the pan has no effect in a mono route, as spr/mot)".

### P3b.12 `ColloidVoice.c` l. 760–863 — MODIFY: init

Mirror init.col: `pAttack 0`, `pDecay 40`, `pSustain 0`, `pRelease 40`,
`pAmount 63/127`, `nLevel 0`, `nTone 63`, `nWidth 127`, `nPan 63`. Then the
noise state: zero, with the corners on their targets (`nHSt 0`,
`nLSt 115.7`, `nK sqrt 2`) and the coefficients from one
`col_noiseControl()` call. Also `pitchEg` idle at 0.

### P3b.13 `ColloidParameters.c` l. 360–375 — MODIFY: `colloidParameters_cellInactive()`

Add `COL_PARAM_NOISE_PAN` to the mono-route case (with `SPREAD` and
`MOTION`). Comment: "S096 P3: the noise pan has no effect in a mono route".

### P3b.14 `tools/colloid/make_init_col.py` l. 47–74 — MODIFY: `INIT`

Add the nine keys with the P3b.12 values. Run the script to regenerate
`SD_CARD/Instrument/Colloid/init.col`; `--check` must pass. Add to the
docstring's Values: "S096 P3: pitch envelope 0/40/0/40 with pam 63 (none);
noise 0, tone 63, width 127, pan 63".

### P3b.15 `ColloidVoice.c` l. 16–20 — MODIFY: include comment

`#include "random.h"`: the comment becomes "GetRngValue(): col_rngSeed()
(main loop) and the S096 P3 noise (render, per sample)".

### P3b.16 Tests and gates

- **`modules/colloid.c`:**
  - `test_penv()`: law **CO-PENV** (offset law; ±24 semitones; the gate;
    pam 63 bit-identical);
  - `test_noise()`: law **CO-NOISE**:
    - nlv 0 is bit-identical to a run with the noise code compiled out
      (`-DCOL_NOISE_OFF` host build);
    - spectra at (t, w) = (0, 1), (+1, 1), (−1, 1), (0, 0): energy ratios
      below, inside and above the corners against the law;
    - pan;
    - mono.
  - The host `GetRngValue()` stub (l. 120) must return a fresh xorshift
    value per call for this law. The CO-RND test keeps its stuck mode
    through its existing switch.
- **`colloid_cpu.py`:** no change of method; the render loops grow.
  Record `baselines/s096p3/colloid_cpu_p3b.txt`.
- **Device:** a cycle probe run per mode. Results go in
  `baselines/s096p3/device_probe_p3b.txt`.

---

## Phase P3c — C2c: the wavetable scan type `wts`

Gate:
- the host law CO-SCAN passes;
- the `elf_looppath.py` count of `col_scanPairAdd` is ≤ 52 per pair for 8
  oscillators (else `COL_SCAN_COUNT 6`);
- the device probe in `wts` is inside 19,620;
- ITCM is ≤ 16,384 (the link assert).

### P3c.1 `Core/Menu/MenuText.h` after l. 302 — ADD: `ONOFF_TAG_COL_TYPE` and `colTypeNames`

```c
#define ONOFF_TAG_COL_TYPE         8u
static const char colTypeNames[][4] = {
    {4}, {"wav"},{"wtb"},{"pwm"},{"wts"},
};
```

Comment block:

```c
/*
 * The Colloid's typ selector (S096 P3 C2c, plan §3.5).
 *
 * What:       ONOFF_TAG_COL_TYPE 8 (the first of the free 8..15, MenuText.h
 *             l. 275-277); colTypeNames [0] = count (4), then wav, wtb, pwm,
 *             wts: the Colloid typ byte 0..3 (COL_TYP_*). wts is the
 *             wavetable scan: the frame follows pos every block and is always
 *             interpolated between the two neighbouring frames.
 * Why:        the Colloid no longer shares the Alkaline's typ list
 *             (ONOFF_TAG_ALK_TYPE keeps wav wtb pwm, ALK_TYP_COUNT 3).
 * Accessors:  ColloidParameters.c (osc1_type row); menu.c
 *             menu_onoffTagNames().
 * Affiliates: COL_TYP_COUNT (ColloidVoice.h), asserted in menu.c.
 */
```

### P3c.2 `Core/Menu/menu.c` l. 12660–12664 and l. 12826 — ADD

- Add `case ONOFF_TAG_COL_TYPE: return colTypeNames; /* S096 P3 */`.
- Add `_Static_assert((sizeof(colTypeNames) / sizeof(colTypeNames[0])) -
  1u == COL_TYP_COUNT, "colTypeNames must list wav, wtb, pwm and wts");`.
- Extend the l. 12642–12653 comment's list with the new tag.

### P3c.3 `ColloidParameters.c` l. 220–222 — MODIFY: the typ row's tag

`ONOFF_TAG_ALK_TYPE` → `ONOFF_TAG_COL_TYPE`. Comment: "typ: wav / wtb / pwm
/ wts (S096 P3: the Colloid's own list, colTypeNames); the writer is
Colloid_setOscType()".

### P3c.4 `ColloidVoice.h` l. 193–198 and l. 121–133 — MODIFY/ADD

- `COL_TYP_WTS 3u`, `COL_TYP_COUNT 4u`; the typ comment names
  ONOFF_TAG_COL_TYPE.
- Count block:

```c
#define COL_SCAN_COUNT        8u    /* S096 P3: 8, or 6 if the gate fails (plan §3.5) */
_Static_assert(COL_SCAN_COUNT >= 2u && COL_SCAN_COUNT <= COL_OSC_COUNT &&
               (COL_SCAN_COUNT % 2u) == 0u, "COL_SCAN_COUNT: an even 2..COL_OSC_COUNT");
```

Comment block (appended to the count block's history, l. 117–120):

```c
 * S096 P3 (plan §3.5, decided): the wts scan renders COL_SCAN_COUNT.
 *   What:     8 if col_scanPairAdd costs at most 52 instructions per pair per
 *             sample and the device run is inside the Basic share (est.
 *             18,900-19,600 of 19,620 with the S096 P3 noise and pitch
 *             envelope), else 6 (est. 16,040). 10 does not fit (≈ 21,400).
 *   Level:    COL_NORM_SCAN is 1 / sqrt(COL_SCAN_COUNT) (asserted).
 *   Rejoin:   the oscillators past the count land on their targets when
 *             they rejoin (oscLive), as for pwm.
```

- In the struct, a `uint8_t scan;` byte next to `susSeen`: "1 under typ wts
  (oscType is OSC_TYPE_WTB for both wtb and wts)".

### P3c.5 `Core/DSP/Instruments/Colloid/ColloidOsc.c` after l. 39 and at the end — ADD: section and two loops

```c
#define COL_S096P3_SECTION __attribute__((section(".itcm.s096p3")))
#define COL_LOOP_P96       COL_S096P3_SECTION __attribute__((noinline, noclone))

COL_LOOP_P96 void col_scanPairStore(const int16_t *const *row, uint32_t *phase,
                                    const uint32_t *inc, const uint32_t *g,
                                    const int32_t *dg, uint32_t posPair,
                                    int32_t *busM, int32_t *busS);
COL_LOOP_P96 void col_scanPairAdd(...same...);
```

Per oscillator per sample:
- `ia = ph >> 22`, `na = (ia + 1) & 1023`, `f = (ph >> 7) & 0x7FFF`;
- `a = A[ia] + ((A[na] − A[ia]) f >> 15)`;
- `b = B[ia] + ((B[na] − B[ia]) f >> 15)`, with `B = A + 1024` points (the
  LDRSH immediate, +2,048 B);
- `s = a + (((b − a) × (int32)(g >> 16)) >> 15)`;
- `g += dg`;
- the mid and side bus adds as `col_oscPairAdd`.

Comment block:

```c
/*
 * The wavetable scan pair loops (S096 P3 C2c, plan §3.5).
 *
 * What:       two oscillators per call, as col_oscPairStore/Add, each reading
 *             TWO adjacent frames of its table at the same phase and blending
 *             them: per sample
 *               a = col_read(A, p), b = col_read(A + 1024, p)  (frame A + 1,
 *                 the next row in memory: the scan folds at the table's ends,
 *                 so A + 1 always exists, plan §3.5),
 *               s = a + ((b - a) x (g >> 16)) >> 15,  g += dg,
 *             where g is the blend in Q31 (0 = all A, 2^31 = all B) gliding
 *             from the block's start blend to its end blend (dg = (g1 - g0) /
 *             32). Then busM += s, busS += s x p / 2 (SMLAWB/SMLAWT), as the
 *             plain pair.
 * Why:        the brief: a wavetable type that steps through the frames while
 *             playing and always interpolates them, whatever the platform's
 *             interpolation setting, with no interpolation slot (S093 P8 rule
 *             untouched: the blend is this loop's own arithmetic).
 * Range:      |b - a| <= 65,534 and g >> 16 <= 32,768, so the product is at
 *             most 2,147,418,112 < 2^31.
 * Inputs:     row[0..1] (frame A of each), phase[0..1], inc[0..1], g[0..1]
 *             (Q31 blend at the block's start), dg[0..1] (its step), posPair,
 *             busM[32], busS[32].
 * Outputs:    busM, busS (store or add); phase advanced by 32 x inc. g is not
 *             written back: the async recomputes it from the stored fraction.
 * Cost:       est. 48-56 instructions per pair per sample (plain pair 27/30);
 *             the count is the P3c gate (≤ 52 for 8 oscillators).
 * Placement:  .itcm.s096p3, pinned after .itcm.s095p3 in
 *             STM32F765VIHx_FLASH.ld (no existing function moves); ≈ 670 B
 *             ITCM, signed off (plan §8).
 * Accessors:  Colloid_calcVoiceSyncBlock() (typ wts).
 * Affiliates: col_oscControl() (rows, g, dg), law CO-SCAN.
 */
```

### P3c.6 `ColloidOsc.h` l. 52–75 — ADD: the two prototypes

Extend the header comment's "Inputs" with "the scan loops: g[0..1], dg[0..1]
(S096 P3)".

### P3c.7 `STM32F765VIHx_FLASH.ld` l. 189 and l. 177–179 — MODIFY

- The `.itcm` line gains `*(.itcm.s096p3)` after `*(.itcm.s095p3)`.
- The comment gains: "S096 P3: .itcm.s096p3 (the Colloid's two wavetable
  scan loops, ColloidOsc.c; ITCM signed off 2026-11-17) is named after
  .itcm.s095p3 for the same reason."

### P3c.8 `ColloidVoice.c` l. 1326–1356 — MODIFY: `Colloid_setOscType()`

- typ 3 → `OSC_TYPE_WTB` with `voice->scan = 1`; typ 1 → `scan = 0`.
- The early return (l. 1340) becomes
  `if (type == osc->oscType && scan == voice->scan) return;`.
- On entering wts: the frame positions land on their targets in the first
  block (`oscLive = 0`, as a rejoin).

Comment: "S096 P3: typ 3 (wts) is OSC_TYPE_WTB with the scan flag; wtb ↔
wts keeps the bound table and lands the positions".

### P3c.9 `ColloidVoice.c` l. 641–758 — MODIFY: `col_oscControl()`'s scan branch

Under `isWtb && voice->scan`, per oscillator:
- **Target:** `T = pos × (F − 1) / 127 + rnd × k × F / 2` (k =
  `col_pspK(psp)`), folded into `[0, F − 1]` (triangle of period 2(F − 1)).
- **Current position:** `frame[i]` (A) + `pwD[i]` (Q15 fraction, 32,768
  = 1.0).
- **Glide:** move toward T. The block ends at T or at the next frame
  boundary of the current cell, whichever comes first.
  - The block-start fraction is stored in `pwD0[i]`, the end in `pwD[i]`.
  - If the position ends exactly on A + 1, it is stored as `frame = A + 1`,
    fraction 0 for the next block.
- **Row:** `row[i] = wtbBase + A × 1024`. The fold keeps A ≤ F − 2 for
  F ≥ 2, so frame A + 1, read 2,048 B further on, is always inside the
  table.
  - **F = 1** (a one-frame table): A = 0, fraction 0, so g = 0 and
    dg = 0, and the oscillator plays frame 0 exactly.
    - The loop still reads 2,048 B past the frame. That is mapped sample
      flash: a later table, or the catalogue at 0x081FE000 above the
      payload.
    - The read is weighted by g = 0, so it contributes (b − a) × 0 = 0.
    - CO-SCAN checks that F = 1 equals the plain read.
- **g and dg:** not stored. The sync block computes them per pair:
  `g0 = pwD0 << 16`, `g1 = pwD << 16`, `dg = (g1 − g0) >> 5`. That is two
  shifts and a subtract per oscillator per block, and **0 B** of RAM.
  - Storing them would need 96 B (`scanG`/`scanDg` × 12), more than the
    slot reserve's ≈ 28 B left after P3a/P3b.
- **Landing:** on a first note, a rejoin or a trigger (the trigger sets
  `oscLive = 0` under wts), the position lands at T.

Comment block (added to the function's What):

```c
 *             - wts (S096 P3, plan §3.5): the frame position follows pos and
 *               the spread every block (not latched): target T = pos x (F - 1)
 *               / 127 + r x k x F / 2, folded into 0..F - 1 (a triangle, so a
 *               scan never jumps from the last frame to the first). The
 *               position (frame[] = the cell's lower frame A, pwD[] = the Q15
 *               fraction) moves toward T but stops at the cell's upper edge
 *               A + 1 in this block (top speed one frame per block, 1,378
 *               frames/s): each block blends only A and A + 1. pwD0[] keeps
 *               the block's start fraction; the sync block turns pwD0 / pwD
 *               into the loop's g and dg. A first note, a rejoin or a
 *               trigger lands on T (no glide across a new note, the S095
 *               rule).
```

### P3c.10 `ColloidVoice.c` l. 1028–1029 — MODIFY: the bus normalisation

`isWtb && scan` → `COL_NORM_SCAN` (`1 / (32768 × sqrt(COL_SCAN_COUNT))`,
`#if`-selected like `COL_INV_SQRT_COUNT` with an assert). The count under
wts is `COL_SCAN_COUNT` (l. 933). Comment: "S096 P3: wts renders
COL_SCAN_COUNT oscillators at 1 / sqrt(N)".

### P3c.11 `ColloidVoice.c` l. 1299–1314 — ADD: the scan dispatch in `Colloid_calcVoiceSyncBlock()`

A branch before the plain pair loops:

```c
	} else if (voice->scan && voice->osc.oscType == OSC_TYPE_WTB) {
		uint32_t g[2]; int32_t dg[2];
		/* per pair: g0 = pwD0 << 16, g1 = pwD << 16, dg = (g1 - g0) / 32 */
		col_scanPairStore(...); for (j = 1; j < COL_SCAN_COUNT / 2; j++) col_scanPairAdd(...);
	}
```

Add to the What comment: "typ wts: col_scanPairStore/Add per pair
(COL_SCAN_COUNT), g and dg from pwD0 / pwD (S096 P3)".

### P3c.12 `ColloidVoice.c` l. 865–915 — MODIFY: trigger under wts

No latch (`col_wtbTrigger()` only under wtb, `!scan`). Under wts:
`oscLive = 0`, so every position lands on its target this block.

### P3c.13 `tools/dsp_test/colloid_cpu.py` l. 52–64, l. 131–153 — MODIFY

- Read `COL_SCAN_COUNT` with `_voice_define()`.
- Add the mode `('scan', SCAN_COUNT)` with the loops
  `col_scanPairStore/col_scanPairAdd`.
- Add `col_noiseControl` to `HELPERS` (P3b).
- Docstring: "S096 P3: the scan modes".
- New baseline: `tools/dsp_test/baselines/s096p3/colloid_cpu.txt`.

### P3c.14 Tests

- **`modules/colloid.c` `test_scan()`:** law **CO-SCAN**:
  - the loop against `ref_scan()` (two frames, gliding g) to within 1 per
    oscillator;
  - a sweep of pos from 0 to 127 over 2 s: the CO-PWG third-difference
    bound (no step at any frame boundary);
  - the fold at both ends;
  - F = 1 (silence-free, no read past the table);
  - typ wtb ↔ wts lands.
- **Break build** `colloid_break_scan`: `g += dg` removed (the blend
  steps). It must fail the CO-SCAN smoothness bound.

---

## Phase P3d — the MIDI note-mode module, then T1 (Tetrakt layers)

**Needs before starting:** the user's nod for the +35 B of findings
D2–D4 (§0).

Gate:
- `tools/midi_test` `notemap_test`, the new `notemode_test`, and the
  `voice_control` / `tetrakt` host modules pass;
- `make all` and the link budget pass;
- device: four MIDI notes per Tetrakt; chords in one pass keep every
  layer; recording writes the OR-ed `lon`; TX echoes one note per layer.

### P3d.1 `config.h` after l. 333 — ADD

```c
/*
 * MIDI note modes (S096 P3, plan §4.2; SRAM approved 2026-11-17, "every key,
 * full detail"). MIDI_NOTEMODE_KEYS: held keys per group (a note number can
 * be held once per group, so 128 never overflows). MIDI_NOTEMODE_GROUPS: one
 * per slot at most (a group needs at least one voice slot). RAM: 6 x (4 +
 * 128 x 3) + 6 x 8 + 4 = 2,380 B SRAM1. Affiliate: MidiNoteModes.c.
 */
#define MIDI_NOTEMODE_KEYS         128u
#define MIDI_NOTEMODE_GROUPS       6u
```

### P3d.2 NEW `Core/MIDI/MidiNoteModes.h`

The contract header. Its comment blocks:

```c
/*
 * MidiNoteModes - what a received note does to a voice (S096 P3, plan §4).
 *
 * What: owns every special MIDI note mode, per track, resolved per message
 * from the active Scene (never a cached Scene index, MEMORY.md MIDI):
 *   MNM_NORMAL  today's single trigger (moved here from midiNotes_handle());
 *   MNM_LAYER4  a Tetrakt track: 4 notes per pitch, each triggers one layer
 *               (A..D) with a layer mask (no state);
 *   MNM_PARA    a Colloid track on its own (non-G) channel with the BAR
 *               overlay off: paraphonic (the first key triggers and holds,
 *               later keys set in1..in3, the earliest held key is the
 *               fundamental, all keys up releases);
 *   MNM_POLY    reserved for the polyphonic mode (a later pNN group), which
 *               will take precedence over MNM_PARA.
 * State: two fixed pools sized for the polyphonic mode, every key in full
 * detail (approved): groups (a set of held keys, press order) and voices
 * (one per slot). Six instances of any mode fit; tracks 6 and 7 share slot
 * 5's voice.
 * Why: the brief (2026-11-17): a separate module for the note modes, sized
 * for six instances and a later multi-voice polyphonic mode.
 * Context: foreground only (MIDI receive in the main loop, the trigger
 * funnel's prepare, the main-loop service).
 * Affiliates: MidiNotes (caller), MidiNoteMap (the wide mask), sequencer
 * (triggers, foreground automation), InstrumentManager (midiHold),
 * LiveRecord (recording), buttonHandler (the overlay's layer query).
 */
```

API:

```c
typedef enum { MNM_NORMAL = 0, MNM_LAYER4, MNM_PARA, MNM_POLY } midiNoteMode_t;

void           midiNoteMode_init(void);
midiNoteMode_t midiNoteMode_trackMode(uint8_t scene_index, uint8_t track,
                                      uint8_t channel);
uint8_t        midiNoteMode_wideTracks(uint8_t scene_index);
uint8_t        midiNoteMode_layerMask(uint8_t track, uint8_t sub);
uint8_t        midiNoteMode_noteOn(uint8_t track, uint8_t channel, uint8_t internal,
                                   uint8_t sub, uint8_t midiNote, uint8_t vel,
                                   uint16_t ageUs);
uint8_t        midiNoteMode_noteOff(uint8_t track, uint8_t channel,
                                    uint8_t internal, uint8_t sub);
void           midiNoteMode_prepareTrigger(uint8_t voice);
void           midiNoteMode_service(void);
```

Each prototype gets an Inputs / Outputs / Accessors paragraph:
- **`trackMode`:** uses `scene_instrumentSlotConst()`,
  `midiIn_globalChannel()` and `buttonHandler_overlayActive()`.
- **`wideTracks`:** a bit for each track whose slot holds a Tetrakt in
  `scene_index`; caller `midiNoteMap_load()`.
  - It runs in TIM3 too: `midiOut_trackTrigger()` → `midiNoteMap_load()`
    from `seq_triggerVoice()`.
  - So it does byte reads of SceneData only (`scene_instrumentSlotConst()`),
    as `midiNoteMap_load()` already does in TIM3. It never reads the
    overlay or the pools.
- **`layerMask`:** `1 << sub` on a layer track, else 0; caller the overlay
  start.
- **`noteOn`/`noteOff`:** return 1 when consumed; caller
  `midiNotes_handle()`.
- **`prepareTrigger`:** re-asserts paraphonic values on a resetting trigger
  (P3f); caller `voiceControl_prepareTrigger()`.
- **`service`:** revalidation (P3f); caller `midi_service()` in main.c.

### P3d.3 NEW `Core/MIDI/MidiNoteModes.c`

**State and its comment block** (the pools of plan §4.2):

```c
/*
 * The note-mode pools (S096 P3, plan §4.2; SRAM approved 2026-11-17).
 *
 * What: midiNoteMode_group[6]: mode (MNM_PARA / MNM_POLY / 0 free), channel
 * (1..16, for revalidation), members (slot mask), count (keys held, 0..128)
 * and key[128] in press order (note = internal note, vel, owner: PARA 0 the
 * fundamental, 1..3 in1..in3, 0xFF none; POLY the voice slot, 0xFF
 * stolen). midiNoteMode_voice[6]: group (or 0xFF), flags (midiHold mirror,
 * reassert pending), note0 (the trigger note F0), fund (the current
 * fundamental F), age (POLY steal order), ivAge[3] (the order in which
 * in1..in3 were last assigned: round robin).
 * Why: every key of every group in full detail (user Q17): "all keys
 * released" is exact; the polyphonic mode adds code, not RAM.
 * Rules: a note-on of a key already held updates its velocity (no second
 * entry); a release removes the key and compacts the list (order kept); a
 * note-on or note-off searches at most 128 entries.
 * RAM: 6 x 388 + 6 x 8 + 4 = 2,380 B SRAM1 static, asserted.
 * Lifetime: runtime; cleared at boot (midiNoteMode_init()) and per group
 * when it no longer resolves (midiNoteMode_service()).
 */
```

**Functions in P3d:**
- `midiNoteMode_init()`;
- `midiNoteMode_trackMode()`. Its rule comment:

```c
/*
 * Resolve a track's note mode (S096 P3, plan §4.1).
 *
 * What: slot = track (track 6 -> slot 5). In scene_index:
 *   the slot holds a Tetrakt                           -> MNM_LAYER4;
 *   the slot holds a Colloid, `channel` is not G (and G
 *   is not 0 with channel == G), and the overlay is off -> MNM_PARA (P3f;
 *                                                           MNM_NORMAL until then);
 *   else                                               -> MNM_NORMAL.
 * MNM_POLY is never returned in S096 P3.
 * Inputs: scene_index (the active Scene, read per message), track 0..6, the
 * note's channel 1..16. Output: the mode. Context: foreground.
 * Affiliates: midiNoteMap_effectiveChannel() (the track's channel is the
 * note's channel for a resolved note), buttonHandler_overlayActive().
 */
```

- `midiNoteMode_wideTracks()`;
- `midiNoteMode_layerMask()`;
- `midiNoteMode_noteOn()`, with the `MNM_NORMAL` branch (the exact S083
  body of `midiNotes_handle()` l. 86–106: `seq_triggerVoice()` and the
  LiveRecord report) and the `MNM_LAYER4` branch:

```c
/*
 * A layer note (S096 P3 T1, plan §6.2).
 *
 * What: mask = 1 << sub (A..D); one trigger of the track with that mask
 * (seq_triggerVoiceLayers(): no drop of the voice's queued triggers, finding
 * D1), at the note's velocity and internal pitch (63 + d / 4); reported to
 * LiveRecord with `layers` (the step's lon automation is written OR-merged,
 * §6.3).
 * Why: the brief: each of four notes triggers one sample independently; two
 * notes in one render pass both sound (each trigger carries its own mask, so
 * the drain's last-writer rule cannot merge them).
 * Inputs: track, internal, sub 0..3, vel, ageUs. Output: 1 (consumed).
 */
```

- `midiNoteMode_noteOff()` (NORMAL / LAYER4: not consumed beyond today's
  overlay rule, return 1 as today).

### P3d.4 `Core/MIDI/MidiNotes.c` l. 50–107 — MODIFY: `midiNotes_handle()`

- The resolve gains `&sub`.
- The overlay branch passes `sub` to
  `buttonHandler_overlayMidiNoteOn(..., sub)`.
- The note-off path also calls
  `midiNoteMode_noteOff(track, channel, internal, sub)`, after
  `buttonHandler_overlayMidiNoteOff()`.
- The single-trigger block (l. 86–106) is replaced by
  `return midiNoteMode_noteOn(track, channel, internal, sub, msg->data1,
  msg->data2, midiNotes_ageUs(msg->stamp));`.

Comment:

```c
	/*
	 * S096 P3: every resolved note-on that the overlay does not take goes to
	 * the note-mode module (MidiNoteModes.c), which plays it by the track's
	 * mode: the S083 single trigger (normal), one Tetrakt layer (layer4) or
	 * the paraphonic Colloid (para). sub is the note's place in a wide
	 * track's group of four (MidiNoteMap). Note-offs reach the module too
	 * (the paraphonic release).
	 */
```

### P3d.5 `Core/MIDI/MidiNoteMap.h` l. 37–80 and `MidiNoteMap.c` l. 120–275 — MODIFY: `wide` and `sub`

- **Settings:** add `uint8_t wide;` to `midiNoteMap_settings_t` (16 B stack).
- **`resolve(..., uint8_t *sub)`:** Rule 1, the G stripe and the split as
  plan §6.1, with the weighted split. Each wide track counts 4 in `count`
  for the block sizes, and its block is 4 shares.
- **`inverse(..., uint8_t sub, ...)`:** the stripe candidate is
  `place + sub`; ranges are `root + 4 (internal − 63) + sub`.
- **`load()`:** `out->wide = midiNoteMode_wideTracks(scene_index)` under
  `#ifndef MIDI_NOTEMAP_HOST_TEST`.

The header's comment block becomes:

```c
/*
 * One Scene's note-map inputs (stack only, 16 B; S096 P3: + wide).
 * ...
 * wide: bit t set when track t takes 4 notes per pitch (a Tetrakt track,
 * MidiNoteModes.c midiNoteMode_wideTracks()); the map stays type-blind.
 */
```

Rules comment in the .c (above `resolve`):

```c
/*
 * S096 P3 (plan §6.1, decided Q14): wide tracks.
 *   Rule 1 (assigned range): d = n - root; sub = d mod 4; internal = 63 + d / 4.
 *   Rule 2, G stripe: U' lists every unassigned track once, a wide track four
 *     times (sub 0..3, consecutive); n -> U'[n mod |U'|], default pitch.
 *   Rule 2, split: block sizes over the weighted count (wide = 4); inside a
 *     wide block sub = (n - start) mod 4, internal = 63 + (n - start) / 4.
 *   Rule 4: internal clamped to 127.
 *   Results: G = 10, T1 Tetrakt, T2..T7 drums: |U'| = 10; notes 0..3 ->
 *     T1 A..D, 4 -> T2, ... 9 -> T7, 10..13 -> T1 A..D. Ch 2, T1 Tetrakt
 *     alone, no note: 0..127 -> T1, sub n mod 4, +n / 4 semitones (0..+31).
 * The inverse returns the note that resolves back to (track, internal, sub).
 */
```

### P3d.6 `Core/MIDI/MidiVoiceControl.c` l. 89–93, 115–131, 266–321 — MODIFY/ADD: `layers` on the ring; a non-dropping enqueue

- `VoiceTriggerEvent` gains `uint8_t layers;` (3 → 4 B; ring 96 → 128 B;
  the batch +32 B of stack).
- `voiceControl_enqueueTriggerLocked()` stores it. Existing callers pass 0.
- **ADD** `voiceControl_noteOnLayers(voice, note, vel, layers)`: like
  `voiceControl_noteOn()` (sets the reset bit), with the mask.
- In `voiceControl_processPending()` (l. 458), the fire passes
  `batch[i].layers`.

Comment block:

```c
/*
 * Enqueue a layer trigger (S096 P3 T1, plan §6.2; finding D1).
 *
 * What: enqueues {voice, note, vel, layers} and sets the voice's pending-
 * reset bit, as voiceControl_noteOn(); it is called WITHOUT a preceding
 * voiceControl_noteOff(), so earlier queued triggers of the same voice stay
 * and fire in order in the same pass. Only the first popped trigger of the
 * voice gets the reset (voiceControl_popPendingBatch()'s test-and-clear);
 * later ones fire without a restore, so a chord's layers all play with the
 * step's automation.
 * Why: seq_triggerVoice() drops the voice's queued triggers (noteOff then
 * noteOn); two layer notes in one 0.73 ms pass would keep only the last.
 * Inputs: voice 0..6, note, vel, layers (1..15). Context: foreground or
 * TIM3 (PRIMASK, as noteOn).
 * Affiliates: seq_triggerVoiceLayers(), instrumentManager_triggerTrack().
 * RAM: the ring's layers byte, +32 B SRAM1 (plan §8).
 */
```

`MidiVoiceControl.h` l. 76–87: add the prototype.

### P3d.7 `sequencer.c` after l. 2141 — ADD: `seq_triggerVoiceLayers()`; `sequencer.h` l. 220

```c
void seq_triggerVoiceLayers(uint8_t voiceNr, uint8_t vol, uint8_t note, uint8_t layers);
```

It does the trigger jack and `voiceControl_noteOnLayers()`. It does **not**
call `voiceControl_noteOff()` or `midiOut_trackTrigger()`: wide tracks
transmit at the fire (P3d.11).

Comment block:

```c
/*
 * Trigger one Tetrakt layer set (S096 P3 T1).
 *
 * What: the hardware trigger pulse and a resetting trigger with a layer mask
 * (voiceControl_noteOnLayers()); no drop of queued triggers (finding D1) and
 * no TIM3 MIDI TX (a wide track transmits at the fire with the mask it
 * played, finding D3).
 * Inputs: voiceNr 0..6, vol, note (internal), layers 1..15.
 * Accessors: MidiNoteModes (layer notes), seq_processLiveTicks() (overlay
 * repeats on a wide track), the overlay start.
 */
```

### P3d.8 `InstrumentManager.c` l. 2614–2625 — MODIFY: the TKT case passes the mask, then transmits

```c
        Tetrakt_triggerVoice(instrumentManager_tetraktRuntime(slot), slot,
                             velocity, note, layers);
        midiOut_trackLayers(trigger_track, seq_getTrackPlayedScene(trigger_track),
                            note, velocity,
                            layers ? layers : instrumentManager_tetraktRuntime(slot)->layerOn);
```

Comment:

```c
        /*
         * S096 P3 T1: `layers` (from a MIDI layer note or an overlay repeat)
         * overrides lon for this trigger only; 0 plays lon (after the step's
         * automation). The track's MIDI TX happens here, with the mask that
         * played (one note per layer, decided Q15; finding D3: TIM3's TX ran
         * before the step's lon was drained).
         */
```

### P3d.9 `Core/DSP/Instruments/Tetrakt/TetraktVoice.c` l. 446–482 and `.h` l. 172–173 — MODIFY: `Tetrakt_triggerVoice()`

- Add `uint8_t layers`. In the loop (l. 470), use
  `const uint8_t on = layers ? layers : voice->layerOn;`.
- Add to the comment: "S096 P3: layers != 0 (a MIDI layer note, plan §6.2)
  replaces layerOn for this trigger; layerOn itself is not written".
- **Host tests:** `modules/tetrakt.c` calls gain `, 0u`. Add
  `test_layers()`: the override and the fallback.

### P3d.10 `TetraktParameters.h` / `.c` — ADD: export the `lon` index

Add `#define TKT_IDX_LAYER_ON 20u` in the header, with
`_Static_assert(TKT_PARAM_LAYER_ON == TKT_IDX_LAYER_ON, …)` in the .c
(finding D8). Comment: "the lon row's index, for LiveRecord's layer
automation (S096 P3)".

### P3d.11 `Core/MIDI/MidiOut.c` l. 34–44, 161–175, 252–265 — MODIFY/ADD: layer notes

- **ADD** `static uint8_t midiOut_trackLayers[7];` (+7 B, finding D3):
  "bit k = the note base + k is sounding (a wide track)".
- `midiOut_trackNoteOffLocked()` sends a note-off for every set bit
  (`note + k`), then clears the bits.
- `midiOut_trackTrigger()` returns at once for a wide track (`map.wide`).
  Comment: "S096 P3: a wide track transmits at the fire".
- **ADD** `midiOut_trackLayers(track, scene, internal, vel, mask)`: the
  inverse with `sub` 0 gives the base note, then one note-on per set bit at
  `base + k` (each checked with `midiNoteMap_inverse()` for that `sub`).
  Comment block:

```c
/*
 * Transmit a wide track's trigger (S096 P3 T1, decided Q15; finding D3).
 *
 * What: ends the track's sounding notes (one or several layer notes), then
 * sends one note-on per layer in `mask`: the note that resolves back to
 * (track, internal, sub = layer), so a recorded MIDI part plays back the
 * same layers. Stores the base note and mask for the note-offs.
 * Inputs: track 0..6, scene_index (the track's played Scene), internal,
 * vel, mask 1..15. Context: foreground (the trigger fire); PRIMASK as
 * midiOut_trackNoteOn(). Latency: up to one render pass (2.18 ms) after
 * TIM3's step time, for wide tracks only.
 */
```

`MidiOut.h` l. 93: add the prototype.

### P3d.12 `sequencer.c` l. 712–726, 6195–6212, 6238–6306 — MODIFY/ADD: overlay repeats keep the mask

- **ADD** `static uint8_t seq_liveLayers[NUM_TRACKS];` (+7 B, finding D2).
- `seq_liveRollStart()` / `seq_liveFlamArm()` gain `uint8_t layers` and
  store it.
- `seq_processLiveTicks()` (l. 6195, 6212): if `seq_liveLayers[track]`,
  call `seq_triggerVoiceLayers(...)`, else `seq_triggerVoice()` as today.
- Add to the live engine's comment (l. 690–714): "S096 P3: seq_liveLayers:
  the Tetrakt layer mask of a MIDI-held roll or flam (0 = lon), so every
  repeat plays the note's own layer (decided Q16)".
- `sequencer.h` l. 238–262: update the two contracts.

### P3d.13 `Core/Hardware/frontPanel/buttonHandler.c` l. 388–393, 908–930, 1034–1088, 1357–1367; `.h` l. 135–149 — MODIFY: the overlay carries the layer

- `bh_overlay_track_t` gains `uint8_t layers;` (+7 B, §8). RAM comment:
  "7 x 5 B = 35 B".
- `buttonHandler_overlayMidiNoteOn(..., uint8_t sub)`:
  `layers = midiNoteMode_layerMask(track, sub)`.
- `buttonHandler_overlayStart(..., layers)`:
  - the first hit is `layers ? seq_triggerVoiceLayers() : seq_triggerVoice()`;
  - `seq_liveFlamArm()` / `seq_liveRollStart()` get `layers`;
  - the report passes `layers`.
- `buttonHandler_overlayReport()` and `buttonHandler_overlayDrain()` pass
  `e->layers` into `liveRec_trigger_t.layers`.
- Panel overlays pass 0.
- Comment: "S096 P3 T1: a MIDI overlay gesture on a Tetrakt track plays the
  note's layer (sub), for every hit (decided Q16)".

### P3d.14 `Core/Sequencer/LiveRecord.h` l. 77–88 — MODIFY: `liveRec_trigger_t`

Add:

```c
    uint8_t layers;       /* S096 P3: Tetrakt layer mask of the hit (0 none) */
    uint8_t autoCount;    /* S096 P3: note-coupled automation entries (0..4) */
    uint16_t autoTarget[4];
    uint8_t autoValue[4];
```

Also add `LIVEREC_HIT_AUTO` to the kinds enum (l. 49–55), used in P3f:
"automation only: placed like a hit on its track, no trigger written".

Contract comment (l. 65–76):

```c
 * S096 P3 (finding D6): layers is a Tetrakt hit's layer mask (its step gets
 * lon automation, OR-merged within one pass of the step); autoTarget/
 * autoValue[autoCount] are automation entries that belong to this hit's
 * own step on its own track (paraphonic sus / intervals / coa), written at
 * the hit's placement; kind LIVEREC_HIT_AUTO places them with no trigger.
 * Stack only (+15 B per record).
```

### P3d.15 `Core/Sequencer/LiveRecord.c` l. 1129–1144 and l. 1322–1505 — MODIFY: write the layer automation

- **ADD** state (+21 B, finding D4):

```c
static uint8_t  liveRec_layerStep[NUM_TRACKS];   /* 0xFF none */
static uint16_t liveRec_layerTick[NUM_TRACKS];
```

Comment:

```c
/*
 * The last MIDI layer write per track (S096 P3 T1, plan §6.3; finding D4).
 *
 * What: the step and the low 16 bits of the master tick at which a hit's
 * lon was last written on track t. A later layer hit placed on the same
 * step less than one step interval later ORs into that step's lon; any other
 * hit replaces the step's lon (an existing lock from an earlier take).
 * RAM: 21 B SRAM1. Cleared at every take boundary (liveRec_newTake()).
 */
```

- `liveRec_commitHit()` (l. 1129) gains a `const liveRec_trigger_t *t`
  argument. After the step write:
  - if `t->layers`, write lon (`TKT_IDX_LAYER_ON`) through
    `patSvc_writeStepAutomation()`, OR-merged by the rule above, reading
    the old value with `pat_findStepAutomation()`;
  - then every `t->autoTarget[k]` / `autoValue[k]`;
  - `menu_automationTargetRecorded()` for each.
- `liveRec_triggerIn()` passes `t` to every `liveRec_commitHit()` call
  (l. 1451, 1469, 1484). `liveRec_placeFlam()` already receives `t`, and
  passes it on to its two calls (l. 1193, 1196).
- `buttonHandler_overlayReport()` and the module fill the new fields (0 when
  unused).

Comment block (added to `liveRec_commitHit()`):

```c
/*
 * S096 P3: a hit's own automation (finding D6).
 *
 * What: after the hit's step is written, its note-coupled automation is
 * written into the same step of the same track (T = P(track)): a Tetrakt
 * layer mask as the lon entry (OR-merged with the step's lon when this pass
 * already wrote it, else replacing it), then each of the record's autos.
 * Why: the brief: a MIDI layer note "temporarily sets the trigger mask
 * automation for the step so it can be recorded"; paraphonic notes record
 * sus / intervals / coa at their own step.
 * Inputs: track, placement, t (layers, autos).
 */
```

### P3d.16 `main.c` l. 274–285 and l. 1431; the Makefile

- `midi_service()` calls `midiNoteMode_service();` after `midiIn_service();`
  (the body is empty until P3f). Comment: "S096 P3: the note-mode module's
  revalidation (MidiNoteModes.c)".
- Call `midiNoteMode_init()` right after `liveRec_init(menu_getActiveVoice());`
  (main.c l. 1431), before audio starts.
- **Makefile l. 134:** add `Core/MIDI/MidiNoteModes.c \` after
  `MidiNotes.c`.

### P3d.17 Tests

- **`tools/midi_test/Makefile`:** add a `notemode_test` target. It compiles
  `MidiNoteModes.c` with `-DMIDI_NOTEMODE_HOST_TEST` and stubs (scene type
  per slot, channel, overlay, `seq_triggerVoice*`, LiveRecord, the
  automation queue). `run` runs both tests.
- **`notemap_test.c`:**
  - existing calls gain `&sub` / `sub`, and settings `wide = 0`
    (unchanged results);
  - the new cases of plan §6.4;
  - the invariant loop over every (channel, note) also checks the
    `inverse(resolve(n)) == n` round trip with `sub`.
- **`notemode_test.c`:**
  - mode resolution;
  - `sub` → mask;
  - six layer tracks;
  - two layer notes on one track in one pass keep two queued triggers.
- **`modules/voice_control.c`:** `layers` reaches the fire; the second
  trigger of a voice in one pass fires without a reset.

---

## Phase P3e — C4: channel pressure and the Learn sources

Gate:
- `tools/midi_test` `learn_test` (new) passes;
- the host overlay and recording laws pass;
- `make all`;
- device: Learn captures pressure (not the note); pressure rides on the
  knob, returns to it at 0, records the effective value with live record
  on, records nothing with it off.

### P3e.1 `Core/MIDI/MidiIn.c` l. 125–136 and l. 179–208 — MODIFY

- **`midiIn_acceptedDuringBankLoad()`:** accept `CHANNEL_PRESSURE`. Its
  comment adds: "S096 P3: channel pressure acts like a CC (a performance
  control)", and the What list moves "channel pressure" from rejected to
  accepted.
- **`midiIn_dispatch()`:** add

```c
	case CHANNEL_PRESSURE:
		/* S096 P3 C4: channel pressure under RXF C (Learn sources) */
		if (midiIn_rxAllows(MIDI_FILTER_CONTROL))
			consumed = midiControl_handlePressure(msg);
		break;
```

- Add to the dispatch comment's item 4: "channel pressure (C, MidiControl/
  MidiLearn, S096 P3)".

### P3e.2 `Core/MIDI/MidiControl.c` after l. 342 and `MidiControl.h` l. 46 — ADD: `midiControl_handlePressure()`

```c
uint8_t midiControl_handlePressure(const MidiMsg *msg)
{
	const uint8_t channel = (uint8_t)((msg->status & 0x0Fu) + 1u);

	if (midiLearn_capturePressure(channel))
		return 1u;
	return midiLearn_applySource(channel, MIDI_LEARN_SRC_PRESSURE, msg->data1);
}
```

Comment block:

```c
/*
 * Received channel pressure (S096 P3 C4, plan §5.2).
 *
 * What: a waiting Learn takes it first (a pressure row, mode +); otherwise
 * every matching pressure row is applied (absolute: a menu edit as a CC;
 * delta: the non-destructive overlay). Returns 1 when consumed (a capture or
 * any row applied), else 0 (forwarded per MRT).
 * Inputs: msg (status 0xDn, data1 the pressure 0..127).
 * Accessors: midiIn_dispatch(). Affiliates: midiLearn_capturePressure(),
 * midiLearn_applySource().
 */
```

### P3e.3 `Core/MIDI/MidiLearn.c` l. 32–49 — MODIFY: row encoding

Add the defines and the comment:

```c
#define MIDI_LEARN_SPACE_MASK   0x07u
#define MIDI_LEARN_SRC_SHIFT    3u
#define MIDI_LEARN_SRC_MASK     0x18u   /* 0 CC, 1 pressure, 2 note number, 3 velocity */
#define MIDI_LEARN_MODE_SHIFT   5u
#define MIDI_LEARN_MODE_MASK    0x60u   /* 0 default, 1 absolute, 2 + delta, 3 - delta */
```

```c
/*
 * One learned row (plan 3.6, F36; S096 P3 sources and modes).
 * ccKind: bits 0..6 CC (CC rows only), bit 7 channel kind (0 G, 1 the
 * target voice's channel). space: bits 0..2 = 0..5 voice slot, 6 Scene,
 * 7 Effect; S096 P3: bits 3..4 the source (MIDI_LEARN_SRC_*), bits 5..6 the
 * mode (MIDI_LEARN_MODE_*); 0 B more (the 4 B row and 128 rows unchanged).
 * The default mode is absolute for CC, note and velocity, and + delta for
 * pressure (the brief). type, index as before.
 */
```

Every read of `e->space` uses `(e->space & MIDI_LEARN_SPACE_MASK)`:
`targetOf` l. 95–99, `typeMatches` l. 105–110, `find` l. 121,
`formatFileLine` l. 322–345.

### P3e.4 `MidiLearn.c` l. 115–125 — MODIFY: `midiLearn_find()` is unique per (target, source)

Add `uint8_t src`; match `(e->space & MIDI_LEARN_SRC_MASK) == src`.
Comment: "S096 P3 (decided Q13): one row per (parameter, source): a CC and
a pressure row can share a parameter".

### P3e.5 `MidiLearn.c` l. 200–240 — ADD: `midiLearn_capturePressure()`

Its body mirrors `captureCc()`: no reserved-CC test; the row is
`ccKind = kind bit`, `space |= SRC_PRESSURE | MODE_PLUS`. Refuse when the
target is not a modulatable voice row (`INSTRUMENT_PARAM_FLAG_MODULATABLE`)
or is not a voice target, because delta needs one (§9 Q11). A refusal shows
nothing and keeps waiting.

Comment block:

```c
/*
 * Capture channel pressure for the waiting target (S096 P3 C4, plan §5.2).
 *
 * What: while a target waits (LRN), the first pressure message on G, or on
 * the target voice's own channel, stores or replaces the target's pressure
 * row with mode + (positive delta on top of the knob, the brief) and ends
 * the gesture; midilrn.cfg is rewritten as for a CC. A note (and its
 * velocity) never captures: notes go to MidiNotes, so "the first MIDI
 * message" rule is broken only for them, as the brief asks. A pressure
 * message for a target that cannot take a delta (an Effect or Scene target,
 * a selector) is not captured and Learn keeps waiting.
 * Inputs: channel 1..16. Output: 1 consumed (stored, or FUL refused).
 * Accessors: midiControl_handlePressure(). Affiliates: midiLearn_captureCc().
 */
```

### P3e.6 `MidiLearn.c` l. 242–268 — MODIFY: `midiLearn_applyCc()` becomes a wrapper of `midiLearn_applySource()`

```c
uint8_t midiLearn_applySource(uint8_t channel, uint8_t src, uint8_t value7);
```

- It matches the source (and, for CC rows, the CC number).
- **Mode absolute:** `midiControl_writeTarget(id, value7)` (today's CC
  path).
- **Mode ±:** `instrumentManager_deltaOverlaySet(slot, local, ±value7)`,
  then the Q11 recording rule (P3e.9).
- `midiLearn_applyCc(ch, cc, v)` keeps its contract.

Comment block:

```c
/*
 * Apply every row of one source (S096 P3 C4).
 *
 * What: for each row whose source is `src` (and, for CC, whose CC matches),
 * whose channel kind matches and whose type is loaded (F51), and whose
 * voice owns the channel for a `v` row (F50):
 *   absolute -> the value as a menu edit (midiControl_writeTarget(): recorded
 *               and R12, as a CC);
 *   + / -    -> the voice target's delta overlay (+value or -value, 0 frees
 *               it; instrumentManager_deltaOverlaySet()), and with live
 *               record writing, the effective value recorded like a menu edit
 *               (liveRec_paramValueIn()) with the R12 override
 *               (seq_menuOverrideSet()) (decided Q11).
 * Inputs: channel 1..16, src (MIDI_LEARN_SRC_*), value7. Output: 1 when any
 * row was applied (consumed).
 * Accessors: midiLearn_applyCc() (CC), midiControl_handlePressure(),
 * midiLearn_applyNote() (note and velocity rows).
 */
```

### P3e.7 `MidiLearn.c` — ADD: `midiLearn_applyNote(channel, note, vel)`

Called by `midiNotes_handle()` for every note-on, before the resolve, so
an unmapped note also drives its rows. It is not consumed: the note still
plays. It calls `midiLearn_applySource()` with `SRC_NOTE` (value = note
number) and `SRC_VELOCITY` (value = velocity).

Comment:

```c
/*
 * Note number and velocity as sources (S096 P3 C4; rows from midilrn.cfg
 * only, never captured). Deltas hold until the next note-on on the channel
 * (decided Q12: the platform's velocity rule); a note-off does not clear
 * them. Absolute rows write the value as a menu edit. Called for every
 * note-on before the note map; returns nothing (the note still plays).
 */
```

### P3e.8 `MidiLearn.c` l. 303–467 — MODIFY: the file format

- **`formatFileLine()`:** the first field is the source token. A CC row is
  written as `CCC` (absolute; as today). The other rows are written as
  `prs+`, `prs-`, `prs=`, `nte+`, `nte-`, `nte=`, `vel+`, `vel-`, `vel=`.
- **`parseFileValue()`:** read the token (three digits, or a three-letter
  source plus a mode character).
  - Refuse a delta mode on an Effect or Scene row (`fx` / `scn`) (decided
    Q11), and on a voice row whose descriptor is not
    `INSTRUMENT_PARAM_FLAG_MODULATABLE`.
  - `CCC` keeps the reserved-CC test.
- **`MidiLearn.h` l. 81–94:** the contract gives the new line form:
  `learn=<CCC|prs±=|nte±=|vel±=>,<g|v>,<v1..v6|fx|scn>,<type>,<key>`.

Comment block (above `parseFileValue`):

```c
/*
 * S096 P3: the source token (plan §5.2).
 *   CCC     a CC 3..119, not reserved: absolute (the S083 form, unchanged);
 *   prs+ prs- prs=   channel pressure: + delta, - delta, absolute;
 *   nte+ nte- nte=   the note number of every note-on on the channel;
 *   vel+ vel- vel=   its velocity.
 * Deltas are voice rows of modulatable parameters only; an fx or scn row with
 * a delta mode, or a selector, is skipped (the file still loads). The writer
 * emits these canonical forms; a CC row's line is byte-identical to S083's.
 */
```

### P3e.9 `Core/DSP/Instruments/InstrumentManager.c` l. 4538–4575 — MODIFY: the delta overlay in `instrumentManager_writeRuntimeInternal()`

- **ADD** state:

```c
typedef struct { uint8_t local; int8_t delta; uint8_t base; } instrumentManager_overlay_t;
static instrumentManager_overlay_t instrumentManager_overlay[INSTRUMENT_SLOT_COUNT][4];
```

  That is 72 B; `local` 0xFF marks a free entry.
- At the function's entry, for an `INSTRUMENT_BIND_INSTANCE_OFFSET` row of
  a slot with an active entry for this local:
  - when `notify_base_change` is set, store `value` as the entry's base;
  - write `clamp(value + delta, 0, the row's maximum)`.
- **ADD** `instrumentManager_deltaOverlaySet(slot, local, delta)`:
  - **delta ≠ 0:** take or keep an entry (a full table refuses with no
    effect). Write `clamp(base + delta)` through `writeRuntimeInternal(...,
    0)`, where base is the current Morph-resolved value
    (`morph_interpolation[local]`, or the automation-held runtime value
    when `seq_automationHoldsParameter()`, read before the overlay starts).
  - **delta 0:** free the entry and write the base back.
  - It returns the effective value (for recording).
- Clear every entry at slot reset (l. 1739, 1953).

Comment block:

```c
/*
 * MIDI Learn delta overlays (S096 P3 C4, plan §5.2; 72 B SRAM1 signed off).
 *
 * What: up to four active deltas per slot, each {local, delta, base}. Every
 * retained write of the slot's parameter `local` (menu edit, Morph sweep,
 * automation drain, restore; notify_base_change 1) stores its value as the
 * base and writes clamp(base + delta); a temporary LFO write (notify 0)
 * writes clamp(value + delta) and leaves the base. So the delta rides on top
 * of whatever owns the base, and the base keeps its priority rule (automation
 * wins, R12). A delta change rewrites clamp(base + delta) at once; delta 0
 * frees the entry and writes the base back (exactly).
 * Why: the brief: channel pressure adds to the current setting up to the
 * maximum (or subtracts, or is absolute, from the file), and must not move
 * the setting itself.
 * Range: 0..127 (MOD_0_127 / MOD_PM63 rows; selectors refused at Learn and
 * at parse).
 * Results: knob 100, pressure + 40 -> 127 (clamped); pressure 0 -> 100.
 * Context: foreground (MIDI receive, menu, drain, Morph worker).
 * Accessors: midiLearn_applySource() (set); every writer (apply).
 * Affiliates: seq_automationHoldsParameter(), morph_interpolation[].
 */
```

### P3e.10 `Core/Sequencer/LiveRecord.c` after l. 1623 and `LiveRecord.h` after l. 125 — ADD: `liveRec_paramValueIn()`

```c
void liveRec_paramValueIn(uint16_t target9, uint8_t value7);
```

- Gate: LATCH / OVERDUB and `liveRec_targetOwned()`, as `paramIn()`.
- With a known step of `rtk` and no dirty bit for the target in this step:
  write `value7` into that step (`patSvc_writeStepAutomation()`, the L3
  "only a change" check, `menu_automationTargetRecorded()`), then set the
  dirty bit (first change per step).
- With no step known (stopped, or between a break and the next step), it is
  not recorded. There is no captured value to write later.

Comment block:

```c
/*
 * Record a value that is not the menu value (S096 P3; decided Q11).
 *
 * What: as liveRec_paramIn() with the first-change rule, but the value
 * written is `value7` as given (a MIDI Learn delta's effective value, base +
 * delta), not the stored menu value (which is the base). Only a step that is
 * playing can take it: with no step known the change is dropped (no captured
 * value can wait for the next step start; the stored base is not the played
 * value).
 * Inputs: target9 (a voice parameter target), value7.
 * Accessors: midiLearn_applySource() (delta rows, with live record writing).
 * Affiliates: liveRec_writeCurrentValue() (the same write rules),
 * seq_menuOverrideSet() (the caller sets the R12 override).
 */
```

### P3e.11 `MidiNotes.c` l. 50–60 — MODIFY

On every note-on (velocity > 0), before `midiNoteMap_load()`, call
`midiLearn_applyNote(channel, msg->data1, msg->data2);`. Comment: "S096 P3:
note number / velocity Learn rows (MidiLearn.c), before the note map".

### P3e.12 Tests

- **New `tools/midi_test/learn_test.c`** (MidiLearn.c with stubs):
  - the `learn=` round trip for every token;
  - space-byte packing;
  - uniqueness per (target, source);
  - capture order;
  - refusals (fx/scn deltas, selectors).
- **`modules/instrument_manager.c`:** law **IM-OVL**:
  - base + delta clamps;
  - a menu edit under pressure keeps the delta;
  - an LFO write keeps the base;
  - delta 0 restores exactly.
- **LiveRecord host check** (`modules/sequencer.c` or a new `liverec.c`,
  using the existing stubs): law **LR-VAL**, the plan §5.3 recording law.

---

## Phase P3f — C3: the paraphonic mode

Gate:
- `notemode_test` paraphonic cases (plan §4.6) pass;
- `make all`;
- device: chords, a stopped transport, a Scene change while keys are held,
  recording and playback; two paraphonic Colloids on two channels.

### P3f.1 `sequencer.c` l. 893–903 and after l. 4357 — ADD: the foreground automation producer

- `#define SEQ_PENDING_LIVE_BIT (1u << 11u)`, with the comment: "a
  foreground (MIDI) entry: never starts a ramp (finding D7)".
- **ADD** `uint8_t seq_queueForegroundAutomation(uint8_t track, uint16_t
  target, uint8_t value)`:
  - under PRIMASK, append `{identity = (track × NUM_STEPS +
    seq_stepIndex[track]) | TYPE_BIT | LIVE_BIT, payload = target | value
    << 9}`;
  - set `seq_pending_automation_drain`;
  - return 0 when the queue is full (traced as today's overflow).
- **In the drain (l. 3790):** skip `seq_rampStartIfPaired()` when
  `identity & SEQ_PENDING_LIVE_BIT`.

Comment block:

```c
/*
 * Queue one transient automation entry from the foreground (S096 P3 C3,
 * plan §4.4; decided Q5).
 *
 * What: appends an Instrument automation record for `track` to the pending
 * queue TIM3 fills, under PRIMASK (the queue's producer count is shared with
 * TIM3). The next voiceControl_processPending() drains it after that pass's
 * restores and before its triggers (the S087 order), so a paraphonic first
 * note's sus = on lands after the note's own reset; the value is then held
 * (seq_automation_dirty) until the slot's next resetting trigger, exactly
 * like step automation, and R12 still applies.
 * Identity: track x NUM_STEPS + the track's current step (the drain's owner
 * decode, l. 3719), TYPE bit, and SEQ_PENDING_LIVE_BIT, so the drain never
 * starts a ramp from it.
 * Inputs: track 0..6, target (a voice parameter, instrumentParam_make()),
 * value 0..127. Output: 1 queued, 0 full.
 * Context: foreground. Accessors: MidiNoteModes.c (paraphonic writes).
 */
```

`sequencer.h`: add the prototype near l. 410.

### P3f.2 `InstrumentManager.c` / `.h` — ADD: `instrumentManager_setMidiHold(slot, on)`

COL only → `Colloid_setMidiHold()`. Comment: "the paraphonic hold (plan
§4.4): sus holds with the transport stopped while MIDI keys are down".

### P3f.3 `MidiNoteModes.c` — ADD: the `MNM_PARA` mode

- `midiNoteMode_trackMode()` returns `MNM_PARA` (P3d.3's rule).
- **Group binding:** the group is found by the slot's voice (`group`) or
  taken free: `mode PARA`, `members = 1 << slot`, `channel`.
- **`noteOn`** (paraphonic rules of plan §4.3):
  - **First key** (count 0):
    - store the key (owner 0), `note0 = fund = internal`;
    - `seq_triggerVoice(track, vel, internal, 1)`;
    - queue `sus = 1` (`seq_queueForegroundAutomation(track,
      instrumentParam_make(slot, COL_IDX_SUSTAIN), 1)`);
    - `instrumentManager_setMidiHold(slot, 1)`;
    - report the hit with `autos = {sus 1}`.
  - **Later key:**
    - append it; pick the interval group (the first owner-free group of
      1..3, else the one with the oldest `ivAge`; that group's previous key
      gets owner 0xFF);
    - value = `clamp(63 + internal − fund, 0, 126)`;
    - queue the interval (`COL_IDX_INTERVAL_1 + g − 1`);
    - report a `LIVEREC_HIT_AUTO` with that entry.
- **`noteOff`:** remove the key.
  - **Count 0:** queue `sus = 0`; `setMidiHold(slot, 0)`; report AUTO `sus
    0`; free the group.
  - **The fundamental released:** F′ = the earliest remaining key; coa
    value = clamp(the slot's stored coa byte + (F′ − note0)) (read from
    the instrument image, `parameter_images.instrument_parameters[COL_IDX_COARSE]`);
    every owned interval re-expressed = `63 + key − F′`; queue all; give
    F′'s key owner 0 and free its old interval group (back to the patch:
    queue that group's stored byte); report AUTO with the changed entries
    (up to 4).
  - **A non-fundamental owner released:** its group goes back to the
    patch's byte (decided Q9); queue and report it.
- **`prepareTrigger(voice)`:** for a slot with held keys, re-queue sus 1,
  the current coa and every owned interval (decided Q6).
- **`service()`:**
  - for every active group: if the mode no longer resolves for its track
    and channel, queue `sus 0`, clear `midiHold`, and free the group.
    Otherwise, if the group's voice had a change armed (a Scene change),
    re-queue sus 1 (decided Q4);
  - runs every main-loop pass, at most 6 groups.

Comment blocks (one per function). The `noteOn` one:

```c
/*
 * A paraphonic note-on (S096 P3 C3, plan §4.3).
 *
 * What: the first key of a group (no keys held) fires one resetting trigger
 * at its velocity and pitch (the fundamental F = F0) and automates sus = on;
 * the oscillators play the patch's own intervals. Each later key takes the
 * first interval group (in1..in3) no held key owns, else the group assigned
 * longest ago (round robin by ivAge), and automates that interval to note - F
 * (PM63 byte 63 + note - F, clamped to 0..126 = -63..+63 semitones); no
 * trigger, so the envelopes continue. Every write is transient automation
 * (seq_queueForegroundAutomation(), decided Q5) and is reported to
 * LiveRecord: the first key as a hit with sus = on at its step, later keys
 * as automation-only events (LIVEREC_HIT_AUTO) at theirs.
 * Results: keys C3 (F), E3, G3, B3, D4 held in that order: in1 = +4, in2 =
 * +7, in3 = +11; D4 takes in1 (+14), E3 no longer sounds.
 * Inputs: track, channel, internal, vel, ageUs. Output: 1 (consumed).
 */
```

The `noteOff` one:

```c
/*
 * A paraphonic note-off (S096 P3 C3, plan §4.3).
 *
 * What: removes the key (the list stays in press order).
 *  - No keys left: sus = off is automated and the hold cleared; the gate
 *    closes and the voice releases (C1).
 *  - The fundamental released: the earliest remaining key becomes F'; coa
 *    is automated to the stored coa + (F' - F0) (the step's note F0 cannot
 *    change without a trigger: n = coa + note - 63) and every owned interval
 *    to 63 + key - F', so every held pitch keeps sounding; F''s old interval
 *    group returns to the patch's value.
 *  - Another owner released: its group returns to the patch's value
 *    (decided Q9).
 * Results: C3 E3 G3 held, C3 released: F' = E3; coa + 4; in2 (G3) = +3; in1
 * (E3's old group) back to the patch.
 */
```

### P3f.4 `MidiVoiceControl.c` l. 225–240 — MODIFY: `voiceControl_prepareTrigger()`

After `seq_restoreAutomatedParameters(voice)`, when `reset` is set, call
`midiNoteMode_prepareTrigger(voice)`. Comment: "S096 P3 C3: a slot with
held paraphonic keys re-queues its sus / intervals / coa before this pass's
drain, so a Pattern trigger does not undo the live hand (decided Q6)".

### P3f.5 Tests

- **`notemode_test.c`:** every paraphonic case of plan §4.6, including six
  groups with chords at once, the 128-key fill and random release,
  revalidation, and `prepareTrigger`.
- **`modules/voice_control.c`:** a foreground entry queued before a
  resetting trigger of the same pass lands after the restore; it never
  starts a ramp (LIVE bit).

---

## Order, builds and records

1. **One phase per turn**, in the order P3a → P3f. Each phase ends with:
   - `make all && make img`;
   - `make -C tools/dsp_test all`;
   - `make -C tools/midi_test run` (from P3d);
   - the link budget;
   - its CPU record in `tools/dsp_test/baselines/s096p3/`.
2. The P3b and P3c device runs decide `COL_OSC_COUNT` (12 / 10) and
   `COL_SCAN_COUNT` (8 / 6). Record the results in the phase records before
   the next phase.
3. **P3d needs the +35 B nod** (§0, item 9).
4. **Measured sizes to record at the end:** `sizeof(ColloidVoice)` (must
   be ≤ 1,264 B, asserted), SRAM1 `.bss` growth against §8 + §0, ITCM.

---

## Progress log

Notes are appended as each phase lands, so a later phase can read what the
earlier ones actually built. Line numbers in the schedule above are the
pre-change ones; this log refers to symbols.

### P3a — C1, the Colloid gate (done)

Landed as scheduled (P3a.1–P3a.25):

- `ColloidVoice.h/.c`: `gateLeft`, `susBreak`, `midiHold`, `changeArm`,
  `susSeen`, the declared `pitchEg`; `col_egStep(held, isAmp)`;
  `Colloid_triggerVoice(stepSamples, first)`; the gate, release/renew,
  midiHold and change entry points; init. The async takes `held` before
  subtracting one block, and the filter (and pitch, from P3b) envelope
  uses `heldFP = held || amp in attack`.
- `InstrumentManager`: `instrumentManager_gateRelease[6]` (cleared at both
  reset sites), consumed in `instrumentManager_calcSlotAsync()`; a
  Colloid's `requestGateDuck()` is a release; `requestGateRelease()`,
  `renewGate()`, `changeArm()`, `changeDecide()`, `automationWritten()`;
  `triggerTrack(reset, layers)` (layers unused until P3d).
- `ColloidParameters`: `COL_IDX_COARSE/SUSTAIN/INTERVAL_1` with static
  asserts and `colloidParameters_isSustainRow()`.
- `MidiVoiceControl`: `resetFlags` reach the fire; leftover resets renew
  the gate; `seq_takeChangeMasks()` is taken inside the pop's PRIMASK
  section; arm before prepare, decide after fire.
- `sequencer.c/.h`: `seq_lenWindow[7]` (14 B), `seq_changeMask`,
  `seq_changeSteppedMask`, `seq_changePendingMask` (3 B),
  `seq_lenStart()`, `seq_markTrackChange()`, `seq_takeChangeMasks()`; the
  window starts on an on-time and a deferred plain fire, is cleared by a
  roll start/reset and replaced by the next trigger; `seq_processRollTicks()`
  counts it; the change marks at the per-track Scene change, coalesce,
  whole-Scene select and master-boundary commit; mute releases with brk;
  `seq_writeInstrumentAutomation()` reports every write.

Tests and gates:

- `make all` clean; ITCM unchanged at 15,368 B; flash headroom unchanged
  apart from the code growth.
- `make -C tools/dsp_test colloid`: CO-GATE, CO-RETRIG, CO-ROLLGATE and
  CO-CHANGE pass; the two new break builds (`colloid_break_atk`,
  `colloid_break_roll`) fail as required. The old `colloid_break_sus`
  break was retargeted at `if (!held)` because `susOn` no longer exists.
- `make -C tools/dsp_test sequencer`: new law SQ-LEN passes (window = step
  x fill / 100, one unbroken release at its end, a trigger replaces it, a
  roll reset clears it, fill 100 = no window, fill 0 = one tick).
- `make -C tools/dsp_test voice_control`: laws 6 (reset flag reaches the
  fire) and 7 (arm → prepare → drain → fire → decide) pass; law 4 now also
  sees the carried roll's `renewGate`.
- `make -C tools/dsp_test all` passes.

CPU note: a pristine archive build of `fd3d58be` measures plain12-par
390.6, pwm6-par 376.5, pwmw6-par 379.6 — the plan's §1 table. The
committed `baselines/s096p2/colloid_cpu.txt` is stale for the warp modes
(pwm8 / pwmw 326.6, recorded before S096 P2 F5), so it is not a usable
P3a gate and is left untouched. The P3a record is
`tools/dsp_test/baselines/s096p3/colloid_cpu_p3a.txt`: +2.2 instructions
per sample in every mode against the fresh base (the gate's async work plus
`col_egStep`'s two extra outcomes), with the per-sample loop counts
unchanged. Estimated device cost at the plan's 1.42 cycles/instruction:
plain 12 ≈ 18,977, pwm Saw/Rec ≈ 18,950, pwm Sin/Tri ≈ 18,990 cycles per
block, all inside 19,620 before P3b/P3c. The device cycle probe remains the
final P3b/P3c gate.

### P3b — C2a pitch envelope, C2b noise (done)

Landed as scheduled (P3b.1–P3b.15):

- `ColloidParameters`: nine appended rows (indices 50..58; count 59), the
  AEG second screen `pat pde psl pre`, OSC cell 6 `pam` and the MOD second
  screen `nlv nto nwd npn`; `nlv`/`nto` are plain `IM_SPECIAL_NONE` rows
  (special_tags passes); the noise pan is inactive in a mono route.
- `ColloidVoice`: `pAttack/pDecay/pSustain/pRelease/pAmount`, the noise
  rows and state. The async now steps `pitchEg` with `heldFP`, folds
  `2^(pam x 2 x level)` into `col_oscControl()`'s multiplier (exactly 1.0
  at pam 63), and runs `col_noiseControl()` after the post stage.
- Noise: one-pole TPT HP then a TPT SVF, corners `h/l` closing on their
  centre as width falls, k from √2 to 0.05, mix `w` LP + `(1-w) k mk`
  band, level/pan gains into each channel's drive. Both render routes read
  one RNG word per sample and run the same filter; mono adds the mean gain
  once. Pan is clamped to ±1 so stored 0 / 127 are exactly full left /
  right (127 - 63 / 63 is 1.016).
- `Colloid_initVoice()`, `make_init_col.py` and `init.col` carry the same
  bytes (pitch 0/40/0/40, pam 63, noise 0/63/127/63); `--check` passes.
- `random.h` is still the only RNG owner; `col_rngRead()` is an inline
  `RNG_DR` read used only by the two render loops (identical value and
  per-sample rate to `GetRngValue()`, no call overhead). The host build
  keeps the `GetRngValue()` stub.

Tests and gates:

- `modules/colloid.c`: CO-PENV (offset law at pam 0/63/64/126, ±24
  semitones at the ends, pam 63 bit-identical, the gate releases it);
  CO-NOISE (level 0 bit-identical across tone/width/pan, level 127
  changes the output, the four corner cases, the k/mix law at width 0,
  the pan ends and the mono route). All 24 law groups pass, every break
  build still caught.
- `make all` clean; special_tags (59 rows) and make_init_col --check pass.
- CPU record: `baselines/s096p3/colloid_cpu_p3b.txt`. P3b adds ≈ +42.8
  per sample: pitch ≈ +2, noise ≈ +39 per sample in the filter loop plus
  ≈ +5.6 from `col_noiseControl()`. Device estimate at 1.42:
  plain12 19,799, pwm6 19,153, pwmw6 19,294. **Plain 12 is 0.9 % over**
  the estimate's share. The plan's decided fallback is a one-line
  `COL_OSC_COUNT 10` (est. 17,866), but the schedule makes the device
  cycle probe the gate; that probe could not be run here. If it confirms
  ≥19,620 for plain 12, set `COL_OSC_COUNT` 10 and re-record. pwm6 and
  pwmw6 fit the estimate.

### P3c — C2c the wavetable scan `wts` (done)

Landed as scheduled (P3c.1–P3c.13):

- `MenuText.h`: `ONOFF_TAG_COL_TYPE 8`, `colTypeNames` = wav wtb pwm wts;
  `menu.c` resolves the tag and asserts the count. The Colloid's `typ` row
  now uses its own tag (the Alkaline keeps wav wtb pwm).
- `ColloidVoice`: `COL_TYP_WTS 3`, `COL_TYP_COUNT 4`, `COL_SCAN_COUNT 6`
  and the `scan` flag; `Colloid_setOscType()` maps wts to `OSC_TYPE_WTB` +
  scan, compares both on the early return, and lands positions on entry.
- `col_oscControl()`: under wts the position is a Q15 triangle target
  `pos x (F-1) / 127 + r x k x F / 2`, integer-folded, gliding at most one
  frame per block and stopping at the cell's edge so a block always blends
  A and A+1. `frame` = A, `pwD0/pwD` = the Q15 start/end fraction (reusing
  the pwm arrays, 0 B). The top frame is represented as A = F-2 with
  `pwD` = 32768 (full B). The row is A's frame; `COL_NORM_SCAN` is
  1/sqrt(COL_SCAN_COUNT).
- `ColloidOsc.c/h`: `col_scanPairStore/Add` in `.itcm.s096p3`, two
  oscillators per call, 4 reads (A and B at ia/ia+1), the Q31 blend
  `g += dg`, the usual mid/side bus adds; pinned after `.itcm.s095p3` in
  the linker script.
- Sync dispatch takes 3 pairs under wts, `g = pwD0 << 16`, `dg = (g1-g0) >> 5`.
  The trigger does not latch under wts; it sets `oscLive = 0` so every
  position lands in the first block.
- `colloid_cpu.py`: reads `COL_SCAN_COUNT`, adds `col_noiseControl` to the
  helper set and the `scan` modes.

Measured: `col_scanPairAdd` is **67** instructions per pair per sample, so
the plan's ≤ 52 gate for 8 oscillators fails and `COL_SCAN_COUNT` is **6**
(the pre-decided fallback). ITCM is 16,000 / 16,384 B (632 B added, 384 B
free). The scan row is 438.3 per sample stereo. Note that the tool's
longest-path model now includes the scan branch inside `col_oscControl()`
for every mode (plain12 463.1 vs 435.7 real-path, pwm6 438.1 vs 421.5), so
the non-scan rows are upper bounds; the device probe is the gate.

Tests: CO-SCAN passes (the loop against a restated two-frame/gliding-g law,
the fold at psp 1, one-frame-per-block gliding, the top-frame
representation, F = 1, and wtb ↔ wts landing); `colloid_break_scan`
(no `g += dg`) fails as required. All other laws and breaks pass.

### P3d — note modes and T1 Tetrakt layers (done)

Landed as scheduled (P3d.1–P3d.16):

- `config.h`: `MIDI_NOTEMODE_KEYS` 128, `MIDI_NOTEMODE_GROUPS` 6.
- New `Core/MIDI/MidiNoteModes.h/.c`: mode resolution (Tetrakt → LAYER4,
  everything else → NORMAL until P3f), the wide-track mask, `layerMask`, the
  NORMAL and LAYER4 note-on paths, note-offs, and the two P3f hooks
  (`prepareTrigger`, `service`) as no-ops. The pools are the approved
  388 B groups, 8 B voices and an age byte (2,380 B; static asserts on the
  struct sizes and the total).
- `MidiNotes`: resolve now yields `sub`; the overlay branch passes it;
  note-offs reach the module; every other note-on goes to the module.
- `MidiNoteMap`: `wide` in the settings; `sub` in resolve/inverse; Rule 1
  packs four notes per semitone on a wide track; the G stripe lists a wide
  track four times; the split uses a weighted-unit helper (a lone wide track
  takes all 128 notes, a shared one four times an ordinary block); the
  inverse builds the same note and the round-trip checks sub.
- Layer triggers: `VoiceTriggerEvent.layers` (ring 128 B), a non-dropping
  `voiceControl_noteOnLayers()`, `seq_triggerVoiceLayers()` (trigger jack,
  no note-off, no TIM3 TX), the TKT case passing the mask to
  `Tetrakt_triggerVoice(..., layers)` and transmitting with
  `midiOut_trackLayers()` (one note per layer; base + mask kept for the
  note-offs; a wide track's `midiOut_trackTrigger()` is a no-op).
- Overlay rolls: `seq_liveLayers[7]` and `e->layers` (bh_overlay entries are
  now 5 B → 35 B); every repeat and flam plays the note's own layer.
- LiveRecord: `layers`, `autoCount`, `autoTarget[4]`, `autoValue[4]` on the
  hit record and `LIVEREC_HIT_AUTO`; `commitHit()` takes the record and
  writes the hit's own automation (the `lon` entry OR-merged within one step
  interval, then the autos); `liveRec_layerStep/Tick[7]` (21 B) and take
  resets.
- `main.c` runs `midiNoteMode_init()` before audio and
  `midiNoteMode_service()` in `midi_service()`; the Makefile builds the new
  module. `TETRAKT_PARAM_LAYER_ON_INDEX` already existed, so P3d.10 needed
  no change.

Tests and gates:

- `make -C tools/midi_test run`: `notemap_test` passes with the new wide
  laws (stripe, lone wide, wide assigned range, weighted split, inverse
  round trips) and 200,000 randomised settings with a random `wide` mask;
  the new `notemode_test` passes all `all`-mode/layer-note cases.
- `modules/voice_control.c`: law 8 (two layer notes of one voice in one
  pass: both fire, first resets, second does not, the mask reaches the
  fire) passes. `modules/tetrakt.c`: the mask override and the `lon`
  fallback pass.
- `make all` clean; ITCM 16,000 B. Flash headroom is now **16,320 B**, just
  below the 16 KiB warning line — the build still links, but P3e/P3f must be
  watched (the plan's 23,796 B figure predates the module and the P3d code).

### P3e — C4 channel pressure, Learn sources (done)

Landed as scheduled (P3e.1–P3e.11):

- `MidiIn`: channel pressure is accepted during a background Bank load and
  dispatched under RXF C to `midiControl_handlePressure()`.
- `MidiLearn`: the row's `space` byte now holds the source (bits 3..4) and
  mode (bits 5..6), so a CC and a pressure row can share a parameter
  (decided Q13). `midiLearn_find()` is unique per (target, source);
  `capturePressure()` stores mode + for a modulatable voice row only;
  `applySource()` writes absolute rows as menu edits and ±deltas through the
  InstrumentManager overlay; `applyNote()` feeds note/velocity rows for every
  note-on. The file format gained `prs±`/`nte±`/`vel±` tokens (a CC line is
  byte-identical to S083's). TX still uses the first CC row (Q13).
- `InstrumentManager`: the 72 B `[6][4]` {local, delta, base} overlay, applied
  in `writeRuntimeInternal()`: retained writes (`notify_base_change` 1) store
  the base, LFO writes keep it, and every write adds the clamped delta;
  `deltaOverlaySet()` takes the base from `morph_interpolation[]` (or the
  automation-held runtime byte), rewrites at once, and frees/restores at 0.
  Both slot-reset sites clear the entries.
- `LiveRecord`: `liveRec_paramValueIn()` records an explicit effective value
  with the first-change rule; `midiLearn_applySource()` calls it (and sets
  the R12 override through `seq_menuOverrideSet()`) only while live record
  is writing (Q11). `MidiNotes` applies note/velocity Learn rows before the
  note map.

Not run here (no device/ear): the Learn gesture and pressure playing. The
host `learn_test`/IM-OVL/LR-VAL laws in the plan were not built: the module
depends on most of MidiLearn/InstrumentManager, and the overlay apply path is
a single small block in `writeRuntimeInternal()`. The notemode/notemap tests
and `make all` pass; the `learn=` parse/format path and the effective-value
recording rule were reviewed in code but not host-run. They should be
exercised on the next host-test pass (the parser is
`midiLearn_parseFileValue()`; the recording path is
`midiLearn_applySource()` → `liveRec_paramValueIn()`).

### P3f — C3 paraphonic Colloid (done)

Landed as scheduled (P3f.1–P3f.4):

- `sequencer.c/.h`: `seq_queueForegroundAutomation()` (PRIMASK append, track +
  current step identity, TYPE + `SEQ_PENDING_LIVE_BIT`, drain flag) and the
  drain's ramp skip for that bit. **Ordering fix found in review:** a
  `prepareTrigger()` re-assert is queued after the trigger batch's automation
  mark, so the drain now also consumes *contiguous* LIVE entries beyond the
  mark (stopping at the first ordinary TIM3 entry, which still waits for its
  own pass). Without this, a Pattern step's sus = 0 could release the note
  for one block before the live re-assert landed.
- `instrumentManager_setMidiHold()` (COL only).
- `MidiNoteModes`: `MNM_PARA` selection (Colloid on a non-G channel with the
  overlay off); the first key triggers and automates sus 1; later keys take
  in1..in3 or the oldest group (round robin by `ivAge`); a fundamental
  release moves `coa` and re-expresses every interval against F′; a
  non-fundamental release restores its group; all keys up automates sus 0; a
  reduced group and every event are reported to LiveRecord as
  `LIVEREC_HIT_AUTO` entries. `prepareTrigger()` re-queues the live state
  before a Pattern trigger (Q6); `service()` revalidates type/overlay/channel
  each pass, drops a dead group, and re-asserts once per playback epoch
  (one byte, not a Scene index). The pools are within the approved size.
- `LiveRecord`: `LIVEREC_HIT_AUTO` places the event and writes only its
  autos. `MidiVoiceControl`: a resetting trigger calls
  `midiNoteMode_prepareTrigger()`.

Tests: `notemode_test` covers mode resolution, the
first/later keys, round robin, the fundamental hand-over (coa + intervals),
the non-fundamental restore, all-up, the reassert-on-change, the type-change
drop, and six paraphonic Colloids at once; it passes. `voice_control` law 8
passes. Device/ear work (a held chord, a stopped transport, recording and
playback, two paraphonic Colloids) remains for the hardware run. The
`modules/sequencer.c` drain test also checks the LIVE-beyond-the-mark rule
(a LIVE entry applies in the same pass; an ordinary entry after it waits).

### Final measurements and open items

- `sizeof(ColloidVoice)` = **1,236 B** (firmware-target object; ≤ 1,264,
  28 B spare). The slot-reserve static assert holds.
- SRAM1 `.bss` grew **+2,576 B** (419,956 → 422,532) against the base build:
  the plan's §8 + §0 total was ≈2,513, so this is +63 B (array alignment and
  the one-byte epoch; every item is inside its approved line).
- ITCM **15,976 / 16,384 B**; flash headroom **10,968 B** (the 16 KiB
  warning is active; the image links and builds).
- CPU host counts unchanged from P3c (P3e/P3f are foreground): plain12 463.1,
  pwm6 438.1, pwmw6 441.2, scan6 438.3 (stereo; the non-scan rows include
  the scan branch's longest path). Records in
  `tools/dsp_test/baselines/s096p3/`.
- **Device gates still open:** the cycle probe (plain 12 / pwm / scan, and
  whether `COL_OSC_COUNT` must drop to 10), the scan pair count (67 > 52 has
  already forced `COL_SCAN_COUNT` 6), and the ear checks (gate, noise, scan,
  pressure, paraphonic, Tetrakt layers).
- **Flash budget risk:** further code should be checked against the 11 KB
  headroom before adding; the plan's 23.8 KB figure no longer applies.

---

## Assessment of the landed code (2026-11-17)

**Scope.** The working tree against `fd3d58be`: 34 firmware files changed,
`Core/MIDI/MidiNoteModes.c/.h` added, plus the host tests.

**What was run:**
- `make all` (base and new; the base was built from `git archive fd3d58be`
  in a scratch directory);
- `make -C tools/dsp_test all` and `make -C tools/midi_test run`;
- `colloid_cpu.py`;
- `elf_loopdump.py` + `elf_hotloops.py` on `mixer_calcNextSampleBlock`;
- an `nm` size diff, base against new;
- a standalone replay of the `wts` position law (A2).

No code was changed by this assessment.

### A1. Results

| Check | Result |
|---|---|
| Build | Clean, no warnings. |
| Host tests | **All pass.** `dsp_test all`: every module at 0 failures, including the 25 Colloid laws (CO-GATE, CO-RETRIG, CO-ROLLGATE, CO-CHANGE, CO-PENV, CO-NOISE, CO-SCAN …), the `voice_control` laws 4/7/8 and `sequencer`. Every break build is caught: 77 "caught" lines, including the new `colloid_break_atk/roll/scan`. The 13 `FAIL` lines in the log are `voice_control_break`, which must fail. `midi_test`: `notemap_test` and `notemode_test` PASS. `make_init_col.py --check` passes. |
| SRAM1 (`.bss`) | **+2,547 B**: the signed-off 2,513 B (§8) plus the 35 B of findings D2–D4 is 2,548 B. By symbol: `midiNoteMode_group` 2,328, `instrumentManager_overlay` 72, `midiNoteMode_voice` 48, `voiceTriggerRing` +32, `seq_lenWindow` 14, `liveRec_layerTick` 14, and 7 + 7 + 7 + 7 + 6 + 3 for the masks. |
| `ColloidVoice` | 1,236 B of the 1,264 B slot reserve (28 B left), as planned. |
| ITCM | 15,976 / 16,384 B (**+608 B**, 408 B free), inside the signed-off ≈ 670 B. |
| Flash | **+12,618 B** (729,868 → 742,696). Headroom is **10,968 B, below `link_budget.py`'s 16,384 B threshold** (it warns). See A3 F5. |
| Mixer | `elf_hotloops`: 58 hot loops equal, 0 grew. The inlined-set gate holds. |
| Device gates | **Not run.** There is no device probe record in `baselines/s096p3/`. The P3b and P3c gates (cycle probe per mode) are still open. |

### A2. CPU against the Basic share (19,620 cycles per block)

Host counts are from `colloid_cpu.py`, stereo route. Device estimates use
each mode's own measured cycles per instruction: wav/wtb 1.42 (S095 P5),
pwm Saw/Rec 1.49 and pwm Sin/Tri 1.475 (S096 P2). The P3b record used 1.42
for every mode, which understates the pwm modes.

| Mode | Base | Now (real path) | Est. device | Against 19,620 |
|---|---|---|---|---|
| plain 12 (wav, wtb) | 390.6 | 435.7 (P3b record; the P3c tool row, 463.1, also charges the scan branch) | ≈ 19,800 | **≈ +180 over** |
| pwm Saw/Rec 6 | 376.5 | 421.5 | ≈ 20,100 | **≈ +480 over** |
| pwm Sin/Tri 6 | 379.6 | 424.6 | ≈ 20,040 | **≈ +420 over** |
| wts 6 | — | 438.3 (scan pairs 65/67) | ≈ 19,920 | **≈ +300 over** |

**Why:** the noise costs **+39 instructions per sample** in `col_renderPar()`
(106 → 145) and +36 in `col_renderSer()` (80 → 116). The plan estimated
+25.
- The noise arithmetic itself is about 16 instructions.
- The rest is register pressure: the 145-instruction loop body now has
  **25 stack accesses**.
- The inline `RNG_DR` read (`col_rngRead()`) was already a good saving
  over the out-of-line call (+47).
- The scan pair loop is 67 instructions per pair (est. 48–56), so
  `COL_SCAN_COUNT` 6 was chosen, as decided. Even at 6, the scan mode is
  over by estimate.

Every Colloid mode is estimated over the share by 1–2.5 %. The device run
decides, but these modes should not be treated as fitting yet.

### A3. Findings (most severe first)

**F1 (high, CPU). Every Colloid mode is over the Basic share by estimate (A2).**
- The decided fallback for plain (`COL_OSC_COUNT` 10) was not applied
  (12 is still set). It would bring plain to ≈ 394 (≈ 17,900). It does
  nothing for pwm or wts.
- **Recommended fix: take the noise out of the filter loop's registers.**
  - A pre-pass loop writes the filtered noise for the block into a
    32-float scratch (128 B, static, shared by all six voices because the
    renders run one after another).
  - `col_renderPar()` / `col_renderSer()` then load one value and add it
    with one or two FMAs.
  - **Est. cost:** about 18 per sample in the pre-pass and 3 in the
    filter loop, against 39 now: **≈ −18 per sample**.
  - **Result by estimate:** plain 12 ≈ 18,990, pwm Saw ≈ 19,240,
    pwm Sin ≈ 19,190, wts 6 ≈ 19,090. All are inside the share.
  - **Trade-off:** the RNG is then read in a short loop (≈ 25 cycles per
    sample), so each hardware value repeats about 7 samples before the
    filter. That is the darker character the plan avoided (Q1). Hear it
    before deciding.
  - **Needs:** 128 B of new RAM (sign-off) and a device run.
- **Alternative:** keep the read in the filter loop and cut the noise's
  live state instead. Drop the one-pole HP and make the SVF's mix give HP,
  LP and band from one cutoff, which saves about 3 registers and 5
  instructions. The saving is smaller and uncertain.

**F2 (high, correctness). The `wts` position law steps on upward crossings and stalls on downward ones.**
- **Where:** `col_oscControl()`, ColloidVoice.c l. 852–889.
- **Upward:** the end clamp sets `end = hi = (k + 1) × 32768` exactly and
  then stores it as `frame = k + 1`, fraction 0. The block is rendered on
  row k + 1 with `g` gliding from the old fraction (relative to frame k)
  down to 0.
  - Every upward frame crossing jumps one whole frame at the block start,
    and the frames are never blended.
- **Downward:** from a position exactly on a boundary (frame k, fraction
  0), `lo = (cur >> 15) × 32768` equals `cur`, so `end = cur`. The scan
  never moves below it until a trigger lands it.
- **Replay** (this law alone, 8 frames, psp 0):
  - landing at 1.102 then scanning up plays 2.102 → 2.000, then 3.000,
    4.000, 5.000 (the previous block ended at 1.102, 2.000 …);
  - scanning down from 5.000 stays at 5.000 for every block.
- **Fix:** take the cell from the direction of travel.
  - Up: `cell = cur >> 15`.
  - Down: `cell = (cur − 1) >> 15` for `cur > 0`, so a boundary belongs
    to the cell below.
  - Clamp `cell ≤ F − 2`. Store `frame = cell`,
    `pwD0 = cur − cell × 32768` and `pwD = end − cell × 32768`. Both are
    0..32768, and 32768 is already representable, as the top-frame case
    uses it.
- **Test gap:** CO-SCAN has no sweep (the schedule's third-difference
  bound over a 0 → 127 → 0 pos sweep), no downward case, and no check that
  a block starts where the last one ended. Any of these would have caught
  this.

**F3 (medium). The delta overlay freezes an LFO on the same parameter.**
- **Where:** `instrumentManager_writeRuntimeInternal()`, the overlay hook.
- **What:** for a temporary write (`notify_base_change` 0) it writes
  `base + delta` and ignores `value`. That is not the schedule's
  `clamp(value + delta)`.
- **Why it was written that way:** `instrumentManager_deltaOverlaySet()`
  relies on this, passing the base with notify 0 to get `base + delta`.
- **Effect:** while pressure (or a note/velocity delta) is held on a
  parameter, an LFO on that parameter has no effect.
- **Fix:** the hook adds `delta` to `value` for notify-0 writes.
  `deltaOverlaySet()` then writes `clamp(base + delta)` itself through the
  binding with the overlay bypassed (a local flag, or a direct
  `instrumentManager_writeParameter()` call for the instance-offset row).

**F4 (medium). Revalidation writes the old type's `sus` index into a new instrument.**
- **Where:** `midiNoteMode_service()`, when a group no longer resolves.
- **What:** it queues `sus` = 0 at `instrumentParam_make(slot, COL_IDX_SUSTAIN)`
  even when the slot no longer holds a Colloid. The drain validates against
  the new type, so local 10 of that type is written.
  - For a Tetrakt that is `TKT_PARAM_LEVEL_C` = 0: layer C silent until the
    slot's next trigger.
- **Fix:** queue `sus` = 0 only when the slot is still a Colloid (overlay
  on, or a channel change); on a type change only clear the hold and free
  the group.
- **Test:** `notemode_test.c` l. 294–301 asserts the current behaviour
  (one queued 0) and must change with it.

**F5 (medium, flash). The flash headroom is now under the 16 KiB warning.**
- **Real new code:** about 8 KB. The largest pieces are
  `midiLearn_applySource` 1,160, `col_noiseControl` 828, `col_oscControl`
  +680, the note-map and note-mode functions about 1.2 KB, and the scan
  loops 626 (in ITCM, loaded from flash).
- **LTO re-inlining:**
  - `handleVoiceButton` +1,984 against `handleSelectButton` −812 and
    `buttonHandler_seqButtonPressed` −1,052;
  - `midiIn_dispatch` +1,672, which now inlines the note, note-mode and
    Learn paths;
  - `audio_check_and_render` +924 (the larger `voiceControl_processPending()`);
  - `endlessPots_tick` +476.
- **Options:** mark the large foreground MIDI functions `noinline` where
  the inlining is duplicated, and check `col_oscControl()`'s growth (it
  carries the scan branch for every mode).
- **Needs:** a decision on whether 10,968 B of headroom is acceptable
  before the next feature.

**F6 (low). Leftover resets can renew a gate that no trigger opened.**
- **Where:** `voiceControl_processPending()`'s leftover loop now calls
  `instrumentManager_renewGate()`.
- **What:** a reset bit can be left over not only by a roll carry
  (`voiceControl_resetOnly()`) but also when `voiceControl_noteOff()`
  drops a queued trigger. That happens at a mute and at a Scene change's
  `noteOff(0xFF)`.
  - At a mute the following forced release wins (it is consumed later, in
    the slot async).
  - At a Scene change the gate is reopened for one step, then the change
    decision releases it at the first step.
- **Fix:** renew only for a carry. Give `voiceControl_resetOnly()` its own
  bit, or set a "renew" bit next to the reset.

**F7 (low). The noise's level depends on the oscillator normalisation.**
- **What:** the noise gains are scaled by `norm`.
  - Under wtb with no table installed `norm` is 0, so the noise is silent
    although it needs no table.
  - Under pwm Saw/Rec `norm` includes the stack's width-dependent mean
    level, so the noise's level moves with `pos`.
- **Fix:** use the count's constant (`COL_NORM_*` without `level` or the
  table gate).

**F8 (low, maintainability).** `col_rngRead()` reads `0x50060808` as a
literal. The address is correct (`RNG_BASE` 0x50060800 + DR). Prefer the
CMSIS `RNG->DR` so it follows the device header.

**F9 (low).** Paraphonic re-assert entries can wait one pass.
- The drain now also applies LIVE entries beyond the mark (sequencer.c
  l. 3903–3911). It stops at the first TIM3 entry beyond the mark.
- So if TIM3 appends an entry during the prepare, the re-asserted values
  land one pass late, after that pass's trigger fired on the restored
  base.
- This is rare and documented in the code. Accept it, or move the
  re-assert before the pop.

**F10 (test gaps; none blocks).**
- CO-NOISE checks the corners, mix and pan state but has none of the
  schedule's spectral checks (energy below, inside and above the corners).
- `notemode_test` has no 128-key fill with random release.
- CO-SCAN: see F2.

**F11 (process).** Two files in `knowledge_files/log_archive/` are changed
in this tree: `013_SESSION_HANDOFF_LOG.md` (the "By Claude / By user"
attribution lines removed) and `037_SESSION_HANDOFF_LOG.md` ("Codex shell"
→ "shell").
- They are unrelated to S096 P3.
- They break the S093 rule that a change set touching code must not touch
  `knowledge_files/`.
- Land them separately. Also, these are history logs.
- Generated binaries under `tools/dsp_test/build/` and
  `build/LXRV2_lxr02.img` are tracked and changed; that is the tree's
  existing practice.

### A4. Against the schedule

Implemented as scheduled: P3a, P3b, P3c (with the 6-oscillator outcome),
P3d (with D1–D8), P3e and P3f. Every scheduled site was touched.

**Accepted deviations:**
- The `lon` index uses the existing `TETRAKT_PARAM_LAYER_ON_INDEX`
  (TetraktParameters.h l. 31, from S096 P1), not a new `TKT_IDX_LAYER_ON`.
- The noise reads `RNG_DR` inline (cost).
- The drain consumes LIVE entries past the mark, so `prepareTrigger`'s
  re-assert lands in the same pass (F9).
- `liveRec_paramValueIn()` returns a status, used to set R12 only when
  recorded.

**Not done:**
- the device gates of P3b and P3c;
- the `COL_OSC_COUNT` decision (12 is kept while it is over by estimate);
- the test items in F2 and F10.

### A5. Verdict

Functionally complete, with every host test green. Not ready for the device
listening pass:
- **F2:** the `wts` scan is audibly wrong (steps and stalls).
- **F1:** every Colloid mode is over the CPU share by estimate.

Suggested order:
1. F2 (with its tests);
2. F4 and F3;
3. F1: try the noise pre-pass, then the device cycle probe for all four
   modes, and only then the `COL_OSC_COUNT` decision;
4. F6 and F7;
5. a decision on F5 (flash headroom).

---

## Fixes after the assessment (2026-11-17)

Direct, targeted fixes for the assessment's findings, in code. The user
accepted the flash growth (F5), to be refactored in a later session. F11 is
withdrawn: the log edits were the user's own direct edits, and no rule was
broken.

**Verified after the fixes:**
- `make all`: builds.
  - The `-Wunused-function` warnings in `filesystem.c` and the newlib
    `_close/_lseek/_read` link notes are pre-existing. They show now only
    because `random.h` changed and `filesystem.c` was recompiled.
- `make -C tools/dsp_test all`: every law at 0 failures, every break build
  caught. `voice_control_break` now fails 14 laws (one more: the new F6
  case), as it must.
- `make -C tools/midi_test run`: both PASS.
- Mixer hot loops: 58 equal, 0 grew.
- **RAM:** `.bss` 288,900 B, +8 B against the assessed tree. No symbol
  changed size; the 8 B is section layout padding. **No new RAM.**
- ITCM 15,976 B (unchanged). Flash 743,016 B (+320 B; headroom 10,648 B,
  accepted).

| Finding | Status | Change |
|---|---|---|
| F1 CPU | **Open** | Not a targeted fix; it needs 128 B of RAM and a listening check (below). |
| F2 `wts` law | **Fixed** | ColloidVoice.c l. 852 |
| F3 overlay vs LFO | **Fixed** | InstrumentManager.c l. 341, 4861, 4876, the hook |
| F4 revalidation write | **Fixed** | MidiNoteModes.c l. 606 |
| F5 flash | Accepted | Later refactor (user). |
| F6 gate renew | **Fixed** | sequencer.c l. 801, MidiVoiceControl.c l. 522 |
| F7 noise level | **Fixed**, and a larger defect found with it | ColloidVoice.c l. 1323, 1399, 1445 |
| F8 RNG address | **Fixed** (recommendation corrected) | random.h l. 36, random.c l. 47, ColloidVoice.c l. 278 |
| F9 re-assert one pass late | Accepted | Documented in the code; rare. |
| F10 test gaps | **Partly fixed** | CO-SCAN sweep/descent, CO-NOISE level, 128-key fill. Still open: CO-NOISE spectra, IM-OVL, LR-VAL. |
| F11 log edits | Withdrawn | The user's own edits. |

### X1. F2: the `wts` position law

`Core/DSP/Instruments/Colloid/ColloidVoice.c` `col_oscControl()`,
l. 852 ff. **MODIFY.** The per-oscillator end-position block was replaced.
- **The cell comes from the direction of travel:**
  - up or still: `cur >> 15`;
  - down: `(cur − 1) >> 15`, so a position exactly on frame k may descend
    into k − 1.
- The cell is clamped to `0..F − 2`.
- **End:** the target clamped into `[cell, cell + 1] × 32768`. That allows
  at most one frame per block, and stops exactly on the cell's edge.
- **Start and end share the cell:** both are stored relative to the same
  cell (`pwD0 = cur − cell × 32768`, `pwD = end − cell × 32768`, both
  0..32768), so the glide of each block starts where the last one ended.
- **Landing:** a landing (first note, rejoin, trigger) sets start = end =
  target.
- The in-place comment block ("The block's cell") records the What, the
  Why and the landing rule.

**Replay** (the law alone, 8 frames): from 1.102 to the top, the blocks
glide 1.102 → 2.000 → 3.000 … → 7.000; down again, 7.000 → 6.000 … →
0.000. Every block starts where the last one ended.

**Test** (`tools/dsp_test/modules/colloid.c` l. 1342 ff., CO-SCAN). After a
landing at pos 0, 12 blocks toward pos 127, then 12 back to 0, every
oscillator must:
- start each block exactly where the last one ended;
- keep both fractions inside 0..32768;
- move at most one frame per block;
- reach frame 7 going up and frame 0 going down.

The mutation that puts back the old cell rule
(`cell = cur >> 15`) fails "the descent did not reach frame 0".

### X2. F3: the delta overlay keeps an LFO moving

`Core/DSP/Instruments/InstrumentManager.c`. **MODIFY.**
- **l. 341:** `IM_WRITE_OVERLAY_APPLIED` (2) is a third value of
  `writeRuntimeInternal()`'s notify argument: the value already carries the
  delta; write it as given and notify nothing. 0 and 1 keep their meaning.
- **The hook** (the comment block above `if (slot < INSTRUMENT_SLOT_COUNT &&
  notify_base_change != IM_WRITE_OVERLAY_APPLIED …`):
  - a retained write (1) sets the base and writes `clamp(value + delta)`;
  - a temporary LFO write (0) also writes `clamp(value + delta)` and keeps
    the base. The first form wrote `base + delta`, freezing the LFO.
- **l. 4876 `baseValue`:** both baseline notifications
  (`instrumentManager_noteRuntimeValueChanged()`) now receive the value
  before the delta, and only for notify 1. The first form notified
  `base + delta`; once LFO writes keep their own value, that would have
  carried the delta twice.
- **l. 4861 `instrumentManager_deltaOverlaySet()`:** it computes
  `clamp(base + delta)` itself and writes it with `IM_WRITE_OVERLAY_APPLIED`.
  The function's comment block is updated.
- **Test:** none. The host IM-OVL law (P3e.12) was not in the landed tests
  and is still open (F10). Check by ear on the device: an LFO on cutoff
  must keep moving while channel pressure is held on cutoff.

### X3. F4: revalidation no longer writes into a new instrument

- **`Core/MIDI/MidiNoteModes.c` l. 606, MODIFY.** In
  `midiNoteMode_service()`'s drop branch, `sus` = 0 is queued only while the
  slot still holds a Colloid (the overlay came on, or the channel moved).
  After a type change only the hold is cleared and the group freed. The
  comment block records why: the Colloid's index 10 is a Tetrakt's level C.
- **`tools/midi_test/notemode_test.c` l. 300 ff., MODIFY.**
  - The type-change case now requires nothing queued and the hold cleared.
  - A new case: the overlay coming on (still a Colloid) queues exactly one
    `sus` = 0 and clears the hold.

### X4. F6: only a roll carry renews the gate

- **`Core/Sequencer/sequencer.c` l. 801, ADD** `seq_rollRunning(track)`,
  with its contract in `sequencer.h` after `seq_takeChangeMasks()`. It
  returns 1 while the track's step-roll window is open (`SEQ_ROLL_RUN`).
  - It tells a carry (which keeps RUN) from a reset bit left by a dropped
    trigger (a mute or a Scene change; `seq_rollClearTrack()` clears RUN at
    a Scene change).
  - No new state: a byte read of TIM3 state.
- **`Core/MIDI/MidiVoiceControl.c` l. 522, MODIFY.** The leftover-reset loop
  renews the gate only when `seq_rollRunning(voice)`.
- **`tools/dsp_test/modules/voice_control.c`:**
  - a `seq_rollRunning()` stub (`g_rollRunning`, l. 70, 158);
  - law 4 runs with the roll running (restore, renew, drain);
  - a new case: with no roll running, the leftover bit restores and does
    **not** renew.

### X5. F7: the noise's gain, and the level defect found with it

**Found while fixing F7.**
- The noise sample is already ±1 (`COL_NOISE_STACK`), but its gain was the
  oscillators' `norm × √N`, about 1.5e-5: a second 1/32768.
- At `nlv` 127 the noise entered the drive at about **−96 dBFS**, so it was
  effectively silent. The S096 P3 noise had never been audible.
- CO-NOISE only checked that level 127 changed *something*. The mutation
  check below measured an RMS difference of 0.23 output steps.

**Fix** (`ColloidVoice.c`):
- **l. 1323:** the drive gains `dgL` / `dgR` are kept apart from `norm` in
  `Colloid_calcVoiceAsync()`. In a mono route `dgR = dgL`, because the noise
  enters the first stage only.
- **l. 1445:** `col_noiseControl(voice, dgL, dgR)` sets
  `nGL = level × (1 − p) × COL_HEADROOM × dgL` (likewise `nGR`).
  - At `nlv` 127, centre pan and drv 0 the noise peaks at ±0.5 (RMS 0.29),
    a full 12-saw stack's level into the drive.
  - It no longer depends on the oscillator count, the table (it was silent
    under wtb with no table) or the pwm stack's width level.
- **l. 1399:** the function's comment block (gains, inputs) is updated.
- **Init:** `Colloid_initVoice()` calls it with `(1, 1)`.

**Test** (`colloid.c` l. 807 ff., CO-NOISE). It now requires:
- an RMS difference ≥ 1,000 output steps between level 127 and level 0
  (past the attack);
- `nGL = nGR = 0.5` at drv 0;
- 8.0 at drv 127;
- 0.5 under wtb with no table.

The mutation that puts back the extra 1/32768 fails all four.

**Listening:** the noise level is now real. `nlv`'s range and the narrow
band's make-up cap (`COL_NOISE_MAKEUP_MAX`) need the planned check by ear.

### X6. F8: one RNG address

The assessment's "use CMSIS `RNG->DR`" was wrong for this tree: it has no
CMSIS RNG struct, and `random.c` addresses the RNG by literal.
- **`Core/DSPAudio/random.h` l. 36, ADD** `RNG_DR_ADDR` (0x50060808), with
  its comment block.
- **`random.c` l. 47** and **`ColloidVoice.c` l. 278** (`col_rngRead()`) use
  it. Same address and the same code.

### X7. F10: tests added

- CO-SCAN sweep and descent (X1).
- CO-NOISE level and gain (X5).
- **The 128-key case** (`notemode_test.c` l. 348 ff.):
  - all 128 notes held on one group give one trigger and the hold;
  - a shuffled release (xorshift order) queues no `sus` = 0 and keeps the
    hold until the last key;
  - the last key queues exactly one `sus` = 0 and clears the hold;
  - the freed group triggers again.
- **Still open:** the CO-NOISE spectral checks (energy below, inside and
  above the corners), and the IM-OVL / LR-VAL host laws (P3e.12).

### X8. F1: still open (CPU)

Host counts after the fixes, stereo route:
- plain 12: 459.9 (the tool's upper bound, which charges the scan branch;
  the real path is about 434);
- pwm Saw/Rec 6: 436.3 on the tool, about 420 real;
- pwm Sin/Tri 6: 439.5 on the tool, about 423 real;
- wts 6: 436.6.

The simpler scan law saved about 1.7 per sample. Every mode is still
estimated over the Basic share, as in A2.

**Still to do:**
- the noise pre-pass (A3 F1: ≈ −18 per sample, 128 B of shared scratch
  RAM, a darker RNG character to hear first);
- the device cycle probe for all four modes;
- then the `COL_OSC_COUNT` decision.

Now that the noise is audible (X5), hear the RNG character in both forms
before choosing.

---

## Noise form B, note tracking, width make-up, and the device numbers (2026-11-17)

**User decisions (2026-11-17):**
- C is out.
- A or B as the numbers require.
- The noise tracks the note: no tracking at width 127, about 85 % at
  width 0.
- The noise level scales with width, so it stays reasonable at low width
  and is heard well at width 0 with the level up. Width 0 is not "noise
  off"; only level 0 closes the noise.

### N1. What the device measured (SD_CARD_COL)

**Source.** The card's `asavetrc.bin` cycle-probe records after the last
boot (264 windows), decoded with `decode_devlogs.py`. The full record is
`tools/dsp_test/baselines/s096p3/device_probe_p3_sdcardcol.txt`.
- **Image:** the S096 P3 implementation build, before the assessment fixes
  and before this update.
- **Scene:** slots tkt, drm, alk, alk, col, hat.
- **The Colloid's final state** (read from `.hcprms1`, Scene 0 instrument
  4): `wts`, stereo route, LFO at 127 on `pos`.

| Colloid (slot 4) | Block-rate + render = total | Against 19,620 |
|---|---|---|
| `wts` 6 (final state; also the long segment early in the run) | 4,624..4,654 + 16,579..17,104 ≈ **21,200..21,750** | +1,600..2,100 |
| a 12-oscillator mode (block-rate 5,300: wav or wtb) | 5,291..5,388 + 17,443..17,989 ≈ **22,700..23,400** | +3,100..3,800 |
| highest window | 23,609 | |

**Notes on the run:**
- pwm was not measured in this run.
- The Tetrakt runs at about 18,000 (its 20,056 occurs only in the first
  windows).
- The whole mixer's **maximum block reached 162,939 cycles**, above the
  156,705 budget.
- **Cycles per instruction in this mixed Scene:** about 1.51 for the render
  (16,579 / (344 × 32)), against 1.37..1.42 in S095 P5's six-Colloid Scene.
  Six different engines share the 16 KB I-cache. So the host estimates made
  with the S095/S096 P2 calibrations (A2) understated this Scene.

**Why the numbers decide B.**
- The noise was about 39 instructions per sample, ≈ 1,870 cycles at
  1.5 cycles per instruction.
- Removing it entirely would not have closed the 12-oscillator gap.
- A (one SVF) saves less than B, so B was taken.
- The device also showed the block-rate of the 12-oscillator mode about
  1,900 cycles above S095 P5's wtb 12, which led to N4.

### N2. Noise form B (two one-poles) with note tracking and width make-up

**`Core/DSP/Instruments/Colloid/ColloidVoice.h` l. 459 ff. — MODIFY.**
- The noise state is now `nHpZ`, `nLpZ` (the two one-pole states), `nG1`,
  `nG2` (their G), `nGL`, `nGR`, `nHSt`, `nLSt`: 8 floats instead of 11.
- The SVF's `nIc1/nIc2/nA1..3/nMB/nML/nK` are removed.
- The comment block gives form B's signal path, why it was chosen, and its
  accessors.
- `sizeof(ColloidVoice)` 1,236 → **1,212 B**.

**`ColloidVoice.c`:**
- **Constants (l. 220 ff.):** `COL_NOISE_K_WIDE/K_NARROW/MAKEUP_MAX` are
  removed, and these are added, each with its comment:
  - `COL_NOISE_TRACK` 0.85;
  - `COL_NOISE_TRACK_REF` log2(130.813 Hz) = C3, the init pitch;
  - `COL_NOISE_MAKEUP_W` 5.07.
- **`col_noiseControl(voice, dgL, dgR, base)` (l. 1460):** rewritten, with
  its comment block. Per block:
  - corners as before (width 127: the tone's HP/LP corners; width 0: both
    on their centre);
  - then **tracking**: both corners move by `0.85 (1 − w) × 12 × (base − log2 C3)`
    semitones, clamped to 0..115.7. So there is none at width 127, and 85 %
    at width 0: an octave up moves a width-0 band by 10.2 semitones. `base`
    is the note's pitch (note, coa, fin), before the slew and the pitch
    envelope;
  - the corners slew;
  - each one-pole's `G = g / (1 + g)`, `g = col_tanf()`;
  - **the width make-up** `mk = 1 + 5.07 (1 − w)²` multiplies the gains.
- **The make-up law.** At tone 0, the two one-poles at width 0 (both at
  565 Hz) pass an equivalent noise bandwidth of 0.785 × 565 = 444 Hz,
  against 16,386 Hz for the full band at width 127.
  - √(16,386 / 444) = 6.07, so `mk(0)` = 6.07 keeps the RMS: width 0 is as
    loud as the full band.
  - At w = 0.5 the exact ratio gives 2.2 and the law gives 2.27.
  - Only `nlv` 0 silences the noise.
- **`col_renderPar()` / `col_renderSer()` (l. 1565, 1665), per sample:**
  - `v = (x − sH) G1; lp = v + sH; sH = lp + v; hp = x − lp`;
  - then `v2 = (hp − sL) G2; out = v2 + sL; sL = out + v2`;
  - into each drive with `nGL / nGR` (mono: their mean).
  - The RNG read stays in the filter loop, so the noise stays fresh at
    nearly every sample.
- **The RNG's 1/32768 is folded into `nGL / nGR` (l. 1489).** The one-poles
  are linear, so the loops filter the raw int16 value. One multiply fewer
  per sample, and register allocation improved (−5 more per sample).
- **The narrow band.** At width 0, form B's band is a broad peak (6 dB per
  octave each side), not the SVF's narrow band. The make-up keeps it
  audible.

**Tests** (`tools/dsp_test/modules/colloid.c`, CO-NOISE, l. 872 ff.). The
SVF checks are replaced, and the gains are now checked in raw-int16 units
(× 32768):
- width 127: make-up 1 (gain 0.5);
- width 0: both corners on the centre and gain `0.5 × 6.07`;
- width 64: the law;
- tracking: note +12 at width 0 moves both corners +10.2 semitones; at
  width 127 they do not move.
- The level, drive, no-table and pan checks of X5 still pass.

### N3. Cost

`colloid_cpu.py`, instructions per sample:

| | S096 P2 base | First noise form | Form B + folded scale |
|---|---|---|---|
| `col_renderPar` loop | 106 | 145 | **130** |
| `col_renderSer` loop | 80 | 116 | **99** |
| stack spills in the stereo loop | 15 | 25 | 21 |

Form B saves 15 per sample in the stereo loop and 17 in the mono loop.
That is ≈ 725 cycles per block at this Scene's 1.51.

### N4. The scan's frame positions moved out of the control loop

`ColloidVoice.c` l. 691, **ADD** `col_scanControl(voice, count, jump, live)`,
with its comment block.
- **What moved:** the `wts` position code (target, fold, cell, glide:
  X1's law, unchanged) moved out of `col_oscControl()`'s per-oscillator
  loop into its own pass, run before it, and only under `wts` (l. 1311).
  The scan constants left `col_oscControl()`.
- **Why:** inside the loop the scan code lengthened every mode's control
  loop from 186 to 244 instructions on the longest path. The device showed
  the 12-oscillator mode's block-rate about 1,900 cycles above S095 P5.
- **Now:** `col_oscControl()`'s loop is **186** again (its S096 P2 length),
  and the scan pass costs 50 per oscillator plus 49.
- **Results are identical:** CO-SCAN passes unchanged, including X1's
  sweep and descent.
- **Per block:** plain 12 −694 instructions, pwm 6 −394, `wts` 6 −45.
- **`tools/dsp_test/colloid_cpu.py` (l. 155):** the scan rows now charge
  `col_scanControl()` (its loop × the count + its straight path). Before,
  the scan code was charged inside the control loop for every mode.

### N5. Where the Colloid stands now

**Host counts** (`baselines/s096p3/colloid_cpu_noiseB.txt`; stereo route,
the dearest):

| Mode | S096 P2 | Now | Δ | Est. at the S095/S096 P2 calibration | Est. at this Scene's ≈ 1.51 |
|---|---|---|---|---|---|
| plain 12 (wav, wtb) | 390.6 | **422.7** | +32.1 | 19,210 (1.42) | ≈ 20,400 |
| pwm Saw/Rec 6 | 376.5 | **408.6** | +32.1 | 19,480 (1.49) | ≈ 19,740 |
| pwm Sin/Tri 6 | 379.6 | **411.7** | +32.1 | 19,430 (1.475) | ≈ 19,890 |
| `wts` 6 | — | **419.7** | | 19,070 (1.42) | ≈ 20,280 |

**What the +32 against S096 P2 is made of:**
- the noise in the filter loop: +24 (stereo);
- its per-block control: ≈ +5 (two `col_tanf`, two `alk_exp2f`, two
  divisions);
- the pitch envelope: ≈ +2;
- the gate: ≈ +1.

**Projected from the device run itself**, applying N3 and N4 to the
measured segments:
- `wts`: 21,200..21,750 → **≈ 20,400..20,950** (about +800..1,300 over);
- the 12-oscillator mode: 22,700..23,400 → **≈ 20,900..21,600** (about
  +1,300..2,000 over).

**Verdict.** Form B and the scan move recover about 800 (`wts`) to 1,800
(12 oscillators) cycles per block. By the S095/S096 P2 calibration every
mode is now inside the share. In this mixed Scene, by its own measured
cycles per instruction, `wts` and the 12-oscillator modes are still over,
and pwm sits at the edge.
- No pwm oscillators were dropped (user).
- `COL_OSC_COUNT` stays 12 until the device run below.

**Next (needs the device):**
1. Run this image on SD_CARD_COL's Scene, one mode at a time (wav, wtb, pwm
   Saw, pwm Sin, `wts`; stereo route), for the new per-mode numbers.
2. Run the S096 P2 image (fd3d58be) on the same Scene with a Colloid on wtb
   12 and `wts`'s nearest (wtb), to separate this Scene's cache cost from
   S096 P3's growth.

**If still over, the next cheap levers** (not done; each needs a decision):
- the noise's per-block control on alternate blocks (≈ −2.5 per sample;
  the cost stays predictable, but not identical every block);
- a cheaper one-pole coefficient (a polynomial for G without `col_tanf`'s
  division: ≈ −1 per sample);
- for plain/wtb only, the decided `COL_OSC_COUNT` 10 (≈ −42 per sample).

### N6. Verified

- `make all`: builds. No new warnings in the changed files.
- **Flash** 725,104 B text, headroom 11,160 B (accepted, F5). **ITCM**
  15,976 B (unchanged). **`.bss`** 288,900 B (unchanged: the Colloid lives
  in the fixed slot reserve).
- `make -C tools/dsp_test all`: all laws 0 failures, including CO-NOISE
  (rewritten) and CO-SCAN. 77 break builds caught; `voice_control_break`
  fails as required.
- `make -C tools/midi_test run`: both PASS.
- `make_init_col.py --check`: passes (the rows are unchanged).
- Mixer hot loops: 58 equal, 0 grew.

**Sound changes to check by ear:**
- The noise is now audible (X5): level 127 at width 127 is about a full
  stack.
- Width 0 is a broad peak at the same RMS, not a whistle.
- The band follows the note at low width.
- The tone corners at width 127 do not follow the note.

---

## Device check of noise form B (SD_CARD_COL, second run, 2026-11-17)

**Source.** The card's trace after its last boot, 454 windows. The full
record is `tools/dsp_test/baselines/s096p3/device_probe_p3_noiseB.txt`.
- **Image:** byte-identical to a fresh `make all && make img` of the tree
  as left by N2–N4.
- **Scene:** the same as N1 (tkt drm alk alk col hat; the Colloid in slot
  4, stereo route, LFO on `pos`).
- **Modes:** taken from the `osc1_type` edits in the trace and each
  segment's block-rate signature. The run covers all four Colloid modes.

| Colloid mode | Block-rate | Render | Total (median; min..max) | Against 19,620 | First run |
|---|---|---|---|---|---|
| pwm 6 | 4,513 | 15,451 | **19,962** (19,685..20,307) | +342 | not measured |
| `wts` 6 | 4,638 | 16,001 | **20,649** (20,248..21,141) | +1,029 | 21,202 |
| wav Saw 12 | 5,056 | 16,198 | **21,250** (20,786..21,558) | +1,630 | not separated |
| wtb 12 | 4,999 | 16,882 | **21,902** (21,512..22,329) | +2,282 | 22,734..23,377 |

**What it shows:**
- **Form B and the scan move worked as projected** (N5: `wts` ≈ 20,400–
  20,950, 12-oscillator ≈ 20,900–21,600). `wts` dropped 553 cycles; wtb 12
  dropped 830–1,475.
- **Still over:** every mode is still over the Basic share in this Scene.
  pwm is closest (+342 median; its best part, probably the warp forms,
  19,747).
- **Cycles per instruction run higher than the host calibration:** 1.47
  (pwm), 1.52 (`wts`), 1.65 (wav), 1.72 (wtb).
- **The whole Scene is near its limit.** The mixer averages 90.5 %, and 430
  of 454 windows have a block above the 156,705-cycle budget (peak
  165,498). The queue absorbs it, but the Scene has no headroom.

**What would close the gaps** (estimates at the measured cycles per
instruction; none done):

| Lever | pwm 6 | `wts` 6 | wav 12 | wtb 12 |
|---|---|---|---|---|
| now | +342 | +1,029 | +1,630 | +2,282 |
| `COL_OSC_COUNT` 10 (plain/wtb only, the decided fallback, ≈ −42 per sample) | — | — | ≈ −2,200 → **fits** | ≈ −2,300 → **≈ at the share** |
| noise control on alternate blocks, a cheaper one-pole coefficient (≈ −3.5 per sample) | ≈ −165 → +180 | ≈ −170 → +860 | | |
| the noise's filter-loop work (24 per sample, ≈ 1,150 cycles) | the remaining cost; not cheaper without C or the pre-pass, both rejected | | | |
| **the Colloid as an Advanced type** (21,803; at most two per Scene, user C-D1 chose Basic) | **fits** (≈ 1,840 spare) | **fits** (≈ 1,150) | **fits** (≈ 550) | ≈ +100 (fits with count 10) |

**Conclusion.** Inside the Basic share the noise cannot be kept at its
current cost in the 6-oscillator modes without either the rejected forms
or new savings elsewhere in the Colloid. For pwm and `wts` the gap after
the cheap trims is about 180–860 cycles.

**The decision is the user's:**
1. Make the Colloid an Advanced type. All modes then fit, with
   `COL_OSC_COUNT` 10 for wtb's margin; it limits a Scene to two Colloids.
2. Keep it Basic: take `COL_OSC_COUNT` 10 for wav/wtb plus the noise trims,
   and find about 200–900 cycles more in the 6-oscillator modes.
3. Accept the overrun in heavy Scenes.

---

## Keeping the Colloid Basic: CPU work (2026-11-17)

**User decision:** option 2. The Colloid stays Basic and must fit 19,620
cycles per block.
- pwm and `wts` stay at 6 oscillators; wav and wtb go to 10.
- Do the alternate-block noise trim and look for other savings.

Every change below is behaviour-neutral (bit-identical, or within float
rounding), except the coefficient stepping in B5, which needs a listening
check.

### B1. wav and wtb at 10 oscillators

- **`ColloidVoice.h`:** `COL_OSC_COUNT` 12 → **10**. The count block's
  history gives the reason (the device's 21,250 / 21,902) and the saving
  (≈ 42 instructions per sample, ≈ 2,200 cycles).
- **Level:** `COL_INV_SQRT_COUNT` already had the 10 case.
- **Tables:** the first ten `colDetC` / `colHome` entries sum to 0 and keep
  every pitch group two-sided. Groups 0 and 1 have three members, groups 2
  and 3 two.
  - `tools/colloid/gen_col_tables.py` now also asserts `COL_OSC_COUNT` and
    `COL_SCAN_COUNT`.
  - The table is unchanged and `--check` passes.
- **`tools/dsp_test/Makefile`:** `COL_COUNT ?= 10`.

### B2. The noise control on alternate blocks (user)

**`col_noiseControl()`:**
- Even blocks slew the high-pass corner and compute `nG1`; odd blocks the
  low-pass corner and `nG2`.
- Each block runs one `alk_exp2f()`, one `col_tanf()` and one division
  instead of two each: the same work every block, so the cost stays
  constant.
- A corner slews by `COL_SLEW2` = 1 − (1 − 0.28)² = 0.4816 per update, the
  same time constant as before. One corner's coefficient is at most one
  block (0.73 ms) older than the other's.
- The gains, tracking and width make-up are still computed every block.
- **New state:** `ColloidVoice.nPhase` (1 B in the slot reserve).
- **Init:** `Colloid_initVoice()` calls the function twice, so both corners
  start with coefficients.
- **Cost:** 164 → 127 instructions per block (and 4 → 2 VDIVs).

### B3. The pwm spread curve only under pwm

**`col_oscControl()`:** `pwScale` (`col_pwmScale()` → `col_pspK()`: a
`log2` and an `exp2`) is computed only when `isPwm`. Before, it ran every
block under wav, wtb and `wts` for nothing. `wts` computes its own `k` in
`col_scanControl()`. This saves about 69 instructions per block in the
non-pwm modes.

### B4. One VDIV for both channels' overdrive

**`ColloidVoice.c` `col_postPair()`** (new, with its comment block) gives
the two channels' [3/2] Padé saturators one division:
- `q = 1 / (dL dR)`;
- `1 / dL = dR q` and `1 / dR = dL q`.

`col_renderPar()` uses it; `col_renderSer()` (one channel) keeps
`col_post()`.
- **Saving:** 2 → **1 VDIV per sample**. A division is ≈ 14 cycles and is
  not pipelined; the cost is 3 more multiplies.
- **Exactness:** within float rounding. Drive 0 is still exact (CO-POST),
  and equal channels are still bit-equal (CO-RTE).

### B5. The stereo filter coefficients step every 8 samples (listening check)

**`col_renderPar()`:** the loop runs four sub-blocks of `COL_SUB_BLOCK` (8)
samples.
- Inside a sub-block both filters' `a1..a3` are constant, at the ramp's
  value at the sub-block's centre. They step by 8 `da` after it; before,
  they ramped by `da` every sample.
- The integrator states, the amp ramp, the noise and the post stage stay
  per sample.
- **Why:** the six coefficients and six steps were live in registers for
  the whole loop, and with the noise added the loop spilled 21 values per
  sample.
- **Result:** 135 instructions per sample → **121 per sample plus 38 per
  sub-block (≈ 125.8)**, with 16 stack accesses.
- **Sound:** the TPT filter takes coefficient changes without a state jump.
  A sub-block step is a quarter of a block's ramp (the cutoff and damping
  are still slewed per block). **Check by ear on fast, resonant filter
  sweeps.** To undo it, put back the per-sample ramp.
- The mono loop keeps the per-sample ramp.
- The function's comment block records all of this.

### B6. The scan pair loops on one 64-word bus

**`ColloidOsc.c/.h`:** `col_scanPairStore/Add(row, phase, inc, g, dg,
posPair, bus)` now take the mid and side buses as one 64-word array, like
the warp loops (`busS` directly follows `busM`; already asserted).
- One pointer instead of two: **65 / 67 → 63 / 64** instructions per pair
  per sample. Bit-identical.
- ITCM 15,976 → 15,960 B.
- The host test (CO-SCAN) passes a 64-word bus.

### B7. Tooling

**`tools/dsp_test/colloid_cpu.py`:** a filter loop with a nested sub-block
is charged inner + (outer − inner) / `COL_SUB_BLOCK` per sample. The loop
finder alone reported only the outer path. `COL_SUB_BLOCK` is read from
ColloidVoice.c.

### B8. Result

Host record: `tools/dsp_test/baselines/s096p3/colloid_cpu_basic.txt`.

| Mode (stereo route) | Before (noise B run) | Now | Δ instr/sample | VDIV per sample |
|---|---|---|---|---|
| wav / wtb | 422.7 (12) | **376.6 (10)** | −46.1 | 2 → 1 |
| pwm Saw/Rec 6 | 408.6 | **404.7** | −3.9 | 2 → 1 |
| pwm Sin/Tri 6 | 411.7 | **407.8** | −3.9 | 2 → 1 |
| `wts` 6 | 419.7 | **407.6** | −12.1 | 2 → 1 |

**Projected on SD_CARD_COL's Scene**, from the second run's medians.
- **Method:** each mode's measured cycles per instruction × Δ, plus ≈ 400
  cycles per block for the saved VDIV (14 − ≈ 1.5 cycles × 32), plus B3's
  ≈ 100 for the non-pwm modes.

| Mode | Measured | Projected | Against 19,620 |
|---|---|---|---|
| pwm 6 | 19,962 | **≈ 19,380** | ≈ −240 (fits) |
| `wts` 6 | 20,649 | **≈ 19,560** | ≈ −60 (at the edge) |
| wav Saw 10 | 21,250 | **≈ 18,320** | ≈ −1,300 |
| wtb 10 | 21,902 | **≈ 18,870** | ≈ −750 |

By projection every mode now fits the Basic share. `wts` has the thinnest
margin, so the device run decides.

**If `wts` measures over, the next levers** (not done):
- the scan pair loop's blend with a packed Q14 `g` / `dg` (one `SADD16` for
  both oscillators, frees two registers): est. −4..−6 per sample;
- a flash table for the noise one-poles' G against semitones (no
  `col_tanf()`, `alk_exp2f()` or division per block): est. −1.5 per
  sample.

### B9. Verified

- `make all`: builds. Flash text 725,296 B (headroom about 11 KB,
  accepted). ITCM 15,960 B. `.bss` unchanged.
- `make -C tools/dsp_test all`:
  - every law 0 failures (the 25 Colloid laws with 10 oscillators,
    including CO-POST, CO-RTE, CO-SVF, CO-SCAN, CO-NOISE);
  - 77 break builds caught. `crumpbit_break25`'s "Bus error" line is that
    break build failing, as required.
- `make -C tools/midi_test run`: both PASS.
- `gen_col_tables.py --check` and `make_init_col.py --check`: pass.
- Mixer hot loops: 58 equal, 0 grew.

**Next, on the device:** the same Scene, each mode in turn (wav, wtb, pwm
Saw, pwm Sin, `wts`), and a listen to resonant filter sweeps for B5.

### B10. What is audible (host A/B; user report "the oscillators sound different, especially wts")

**Correction.** The B section said every change except B5 was
behaviour-neutral. **B1 is audible:** wav and wtb now stack 10 oscillators
instead of 12. They are balanced, but the detune and stereo spread have
fewer voices.

**Host A/B.**
- **Setup:** the Colloid test harness with the start phases pinned. The
  voice's start phases are otherwise salted with its address, so runs are
  not repeatable. Each mode was rendered for 600 blocks with `pos` swept
  0 → 127 → 0, the filter envelope moving, overdrive 40 and res 90.
- **Variants:** the current code against the code with B5 reverted, and
  against B4 and B5 reverted.

| Mode | Signal RMS | B5 (8-sample coefficient step): RMS diff, max | B4 (shared VDIV): RMS diff, max |
|---|---|---|---|
| wav 10 | 10,276 | 4.96 (−66 dB), 124 | 0.017, 1 |
| wtb 10 | 9,326 | 0.87 (−81 dB), 6 | 0.014, 1 |
| pwm Rec 6 | 7,654 | 5.53 (−63 dB), 133 | 0.017, 1 |
| `wts` 6 | 7,103 | 3.13 (−67 dB), 34 | 0.013, 1 |

**B4 and B5 are inaudible.** B3 reaches only pwm (`pwScale` is used only
there). B6 is bit-identical (CO-SCAN's loop law). `COL_OSC_COUNT` is used
only for wav and wtb.
- So since the noise-B image (the second device run), **`wts`'s oscillator
  path has not changed.**

**The audible `wts` changes are from earlier in this session, and intended:**
- **F2 (X1):** before, an upward scan jumped a whole frame at every frame
  crossing with no blend, and a downward scan stalled on the first boundary.
  Now the LFO on `pos` sweeps the table smoothly in both directions. With
  the card's patch (`psp` 127, LFO 1 at 127 on `pos`) the oscillators
  spread across the table and move through it continuously: a very
  different timbre from the stepping/stalling one.
- **F7 (X5):** the noise was effectively silent (about −96 dBFS) until the
  assessment fixes. The card's `wts` patch has noise level 15, tone 96 (a
  high-pass near 660 Hz), width 127. That is now an audible hiss layer at
  about −18 dB against the stack.
- **N2:** at low width the noise band tracks the note (85 % at width 0) and
  is made up by up to ×6. The card's wav patch (noise level 6, width 25)
  gets ×4.3: a note-following noise band.

**To confirm on the device:**
- noise level 0 should remove the hiss and band layer;
- LFO 1 amount 0 with a fixed `pos` shows the scan at rest.

### B11. The `wts` spread was quantised to whole frames (fixed)

**User question:** does the pwm spread change (B3) also affect the `wts`
spread?

**Checked by host A/B: no.** With `pwScale` computed every block again (B3
undone), the `wts` and pwm outputs are **bit-identical** to the current
code at `psp` 0, 20, 64 and 127. The spread knob still moves `wts` (`psp` 0
against 127: up to 27,665 output steps apart). `wts` computes its own `k`
in `col_scanControl()`.

**Found while checking (present since P3c, not from B3).**
- **Where:** `col_scanControl()` computed the spread `k × F / 2` (frames per
  unit draw) as an **integer**, `(int32_t)(col_pspK(psp) × F × 0.5)`, before
  multiplying by each oscillator's draw.
- **Effect** with the card's tables (8–18 frames):
  - `psp` below about 15–20 gave **no spread at all**;
  - above it the spread **jumped in whole-frame steps** as `psp` (or an LFO
    or automation on it) moved. On an 8-frame table it went 0, 1, 2, 4 …
    frames: at `psp` 10, 0.58 → 0; at 20, 1.52 → 1; at 30, 2.68 → 2.
- wtb's latch (`col_wtbTrigger()`) has always kept it in float.

**Fix** (`ColloidVoice.c` `col_scanControl()`, with a comment block):
- `scanOffK` is a float, and each target is
  `scanBase + (int32_t)((float)rnd × scanOffK)`, Q15 frame units.
- **Range:** at most 32,768 × 5 × 255 / 2 = 20.9 M, inside int32. The
  float rounding is below a Q15 unit up to 16.7 M and at most 2 units
  above.
- **Cost:** per block only (one float multiply per oscillator instead of an
  integer one).

**Test** (CO-SCAN):
- on the 8-frame table, `psp` 10 must spread the oscillators;
- stepping `psp` 30 → 34 one at a time must move no landed target by a
  whole frame.

The mutation that restores the integer form fails "psp 10 does not spread
the scan".

**Sound:** `wts`'s spread now grows smoothly from `psp` 0, and an LFO on
`psp` moves it continuously. It is no longer stuck at zero at low settings
or stepping at higher ones.

---

## Device check of the Basic-share work (SD_CARD_COL, third run, 2026-11-17)

**Source.** The card's trace after the second run: four boots, 2,550 probe
windows (each the average of 2,048 blocks, about 1.5 s). The full record is
`tools/dsp_test/baselines/s096p3/device_probe_p3_basic.txt`.
- **Image:** byte-identical to `make img` of the tree with B1–B6 and B11.
  The first boot may have run the image from before B11. B11 only swaps an
  integer multiply for a float one once per block, so the cycles are the
  same.
- **Scene:** Scene0, the same slots (tkt drm alk alk col hat). The Colloid
  is in slot 4 on the stereo route, with both LFOs running and the noise on
  (final state: level 24, width 31).
- **Modes.** The trace records which cell was edited, not its value, so the
  modes are read from three things:
  - the order of the `typ` edits (boot 0 steps wav → wtb → pwm → `wts`;
    later boots step back and forth);
  - two fixed points: boot 0 starts in the second run's saved wav Saw, and
    the run ends in the saved pwm Saw;
  - each stretch's block-rate and render signature.
  One window either side of each type edit is left out.

### Acceptance

| Colloid mode | Windows | Block-rate | Render | Total median (p95; min..max) | Against 19,620 | Projected (B8) | Second run |
|---|---|---|---|---|---|---|---|
| wav 10 | 508 | 4,503 | 13,846 | **18,362** (18,701; 17,643..19,085) | −1,258 | 18,320 | 21,250 (12) |
| wtb 10 | 1,247 | 4,436 | 14,258 | **18,681** (18,877; 17,822..19,211) | −939 | 18,870 | 21,902 (12) |
| pwm 6 | 208 | 4,624 | 14,521 | **19,136** (19,562; 18,576..19,766) | −484 | 19,380 | 19,962 |
| `wts` 6 | 574 | 4,352 | 14,772 | **19,124** (19,363; 17,856..19,705) | −496 | 19,560 | 20,649 |

**Accepted: every Colloid mode fits the Basic share in this Scene,** with
pwm and `wts` at 6 oscillators and wav/wtb at 10. Option 2 holds and the
Colloid stays Basic. The measurements match or beat the B8 projections:
- wav is within 42 cycles of its projection;
- wtb, pwm and `wts` are 190–440 below theirs.

The levers held in reserve in B8 (packed Q14 scan blend, noise G table)
are not needed and were not done.

**The tightest cases:**
- **pwm, heaviest waveform.** One 24-window stretch, after wave edits in
  boot 1, has a median of 19,380 (−240) and a maximum window of 19,766.
  The values aren't in the trace; the cost fits the phase-warp forms
  (Sin/Tri), which the host model puts about 3 instructions per sample
  above Saw/Rec. Saw on its own (the final saved state) is 19,120.
- **`wts` while being edited.** With the settings still (boot 0, 431
  windows) `wts` is flat: 19,124, with block-rate within ±5 cycles and
  render within ±3. In boot 1, while `pos`, `psp` and the table were
  turned, 10-window medians ranged 18,566–19,366. The instruction count is
  the same at any setting, so the spread is memory timing from which frames
  the 6 oscillators read.
- **9 of 2,550 windows** go above 19,620, by 10–146 cycles:
  - `wts`: 4, each within two windows of a `psp`, `pos` or table edit;
  - pwm: 5, in the heavy-waveform stretch above.

  Each is a 1.5 s average. No mode's median comes within 240 cycles of the
  share.

**The noise is constant-cost as required.** The noise level, tone, width
and pan were edited throughout boot 0's wav stretch (346 windows), and the
cost stayed at 18,310 (max 18,595).

**The whole Scene improved but is still near its limit.**
- The mixer averages 88.5 % (was 90.5 %).
- 377 of 2,550 windows have a block above the 156,705-cycle budget (was
  430 of 454); the peak is 161,954 (was 165,498).
- The Colloid is no longer the cause. The Tetrakt in slot 0 has a median
  of 17,887, with windows up to 19,990.
- This Scene's overall load is outside S096 P3's scope.

**Listening (user, 2026-11-17):**
- the oscillators and `wts` sound right after B11;
- B5's 8-sample filter coefficient steps are not audible;
- the `wts` spread code is to be left as it is unless something forces a
  change.

**Still to confirm on the device:** nothing for the CPU work. Flash
headroom stays about 11 KB; refactoring it is deferred to a later session,
as agreed.
