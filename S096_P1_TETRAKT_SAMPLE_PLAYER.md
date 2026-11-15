# Tetrakt: four-layer sample player (draft)

- **Status:** draft, revision 7, written 2026-11-14 from the working tree
  after `S095_P4_IMPLEMENTATION.md` §18. Nothing in the source was
  changed for this draft.
  - Figures marked *measured* come from the tree or from
    `build/lxr02.elf`. Figures marked *est.* are estimates from comparable
    loops in the tree, and each one is measured before a schedule is
    written.
  - **Every question is decided (§12).** The next step is the schedule
    (§13).
- **History:**
  - revision 1: the brief translated; eleven questions;
  - **revision 2 (user, 2026-11-14):**
    - the estimate (≈ 14,700 cycles per block at worst) is cheap, so the
      tone and drive stages are made better until the voice sits closer
      to the Basic cap (§5.3, §5.4, §9);
    - a sample-rate reduction (SRR) per layer, before the tone (§5.5);
    - pitch is continuous, with whole semitones at regular positions
      (§5.1);
    - drive per layer;
    - every other recommendation of revision 1 accepted (§12.1);
  - **revision 3 (user, 2026-11-14):**
    - **the layer order is drive → SRR → tone** (§2): the tone after the
      drive and the SRR filters what they add;
    - **velocity is the Drum's method:** `vel` (volume on/off) and one
      `amt` / `dst` pair. A per-layer destination such as `ton` applies
      to all four layers by the amount (§4.2);
    - the rows are fitted to 64 again (§8.1);
  - **revision 4 (user, 2026-11-14): the four `on` switches are one row,**
    a 4-bit mask (0x0 … 0xF) edited by four controls (§4.3). It frees
    three rows; two of them give back S1's return level and feedback tone
    (§8.1);
  - **revision 5 (user, 2026-11-14):** a locked step never changes, even
    when the base does (desired). The automation underline shows all four
    layer cells together wherever the row is locked, so the page makes
    clear that the layers lock at once (§4.3);
  - **revision 6 (user, 2026-11-14): the page layout, names and labels**
    (§8.2). The voice pan comes back as a row (`pan`, "Voice Panning"):
    an L–R balance of the whole voice, distinct from the width. The rows
    are 64 of 64 (§8.1);
  - **revision 7 (user, 2026-11-14):**
    - the 448 B shared work area is approved (§10);
    - the tune row shows the plain bipolar number; every 5th is a whole
      semitone and the ends are an octave (§5.1);
    - SRR runs as the drum and mixer `srt`: 127 is the full rate and the
      default (§5.5);
    - the trigger switch defaults to `A` (§4.3);
    - every remaining recommendation accepted (§12).
- **Brief (user, 2026-11-14):** an instrument that plays several samples
  at once, ideally four. Each sample has:
  - its own decay envelope;
  - its own pitch (±12 semitones over the bipolar row);
  - its own stereo position;
  - a bipolar tone control, and an envelope-to-tone amount;
  - its own drive.

  Also a slapback / short feedback line the samples can send to, and a
  stereo width derived from the envelope (no width at the transient,
  growing width after).

  Each sample has an automatable on/off. At each sequencer trigger, the
  samples that are on retrigger. The others play out their decay.
- **Name:** Tetrakt (four). It would be the eighth instrument type: the
  six in the registry (`drm snr cym hat alk col`), then the Katran draft
  (`DRAFT_KATRAN_DRONE_SYNTH.md`), then this.

---

## 0. Summary

| | Proposal |
|---|---|
| **Voice** | Four sample **layers** (A B C D), each a one-shot read of an installed sample. Each layer has its own sample, trigger switch, level, decay, tune, pan, drive, SRR, bipolar tone, tone envelope and space send. The voice has a volume, an L–R balance (`pan`), and the space stage |
| **Layer chain** | read → × decay × velocity → asymmetric soft-clip drive → SRR → 2-pole bipolar tone (LP ↔ flat ↔ HP, envelope-swept) → × level → pan / send (§2) |
| **Trigger** | A trigger restarts every layer whose bit is set in the `lon` mask, after a 0.73 ms fade if it was still sounding; the others keep sounding. A step lock on `lon` applies to that step's own trigger (§3) |
| **Layer switch** | **One row, `lon`, 4 bits (A = bit 0 … D = bit 3)**, shown on the page as four on/off cells, one per pot. Everywhere else (automation, MIDI, files) it is one selector with 16 named stops, `---` … `all` (§4.3) |
| **Space** | **S1:** one 50 ms line in one voice unit, two taps, the right one a detune offset earlier and wobbling. **W2:** each layer widens as it decays (an allpass on the right side, mixed in by the envelope). §6 |
| **Class** | **Basic.** Designed at ≈ 350 instructions per sample: ≈ 15,700 cycles per block at the Colloid's 1.4 cycles per instruction, ≈ 17,900 at the 1.6 planned here, against 19,620. A trim ladder covers a device result above that (§9) |
| **Velocity** | The Drum's three rows: `vel` (volume on/off), `amt`, `dst`. A destination that is a layer row (`tn*`, `dr*`, `tu*` …) applies to all four layers, each with the velocity of its own last retrigger (§4.2) |
| **Rows** | **64 of 64** (§8.1). Only the width's curve is fixed instead of a row |
| **Pages** | The user's layout (revision 6, §8.2): one parameter per screen across the four layers, pot 1 = layer A … pot 4 = layer D, short names with the layer letter (`smA` … `teD`) |
| **RAM** | Voice struct ≈ 1,010 B *est.* inside the 1,264 B slot reserve, plus a **448 B work area shared by every Tetrakt** (the mix buses and the read scratch; renders run one slot at a time), **approved** (user, 2026-11-14; §10). S1 takes one voice unit per voice from the Effect share, as the units are designed to |

---

## 1. What the tree already has

| Piece | Where | Use here |
|---|---|---|
| **Sample read** | `osc_smpPlainBlock()` (`Oscillator.c:1781`, ITCM `.itcm.s087`): Q19.12 phase, linear interpolation, the per-file gain code (`osc_bindGain()`); the read clamps at the end, and the two zero frames installed after each sample make a finished sample read 0 | One call per layer per block (Q9: reused). *Measured:* 24 instructions per sample (the loop in `baselines/s095p5/pwm8/before/osc.txt`). No new ITCM |
| **Sample catalogue** | `SampleMemory.h`: up to 128 samples (`SAMPLE_MAX_COUNT`), each up to 524,286 frames (`SAMPLE_MAX_FRAMES`, 11.9 s); mono 16-bit 44.1 kHz; `sampleMemory_sampleEntry()`, `sampleMemory_getNumSamples()`, display names | The layers' sample rows list the installed samples |
| **Per-block amp ramp** | Colloid: the envelope steps once per block and the level ramps linearly across the block's 32 samples | The layers' decays |
| **Envelope table** | `colEgK[128]` (`ColloidTables.c`): a per-block one-pole coefficient, time constant 1 ms … 10 s, log-spaced | The decay times (shared, no new table) |
| **TPT state-variable filter** | Colloid `ColSvf`: coefficients at the block's start and end, ramped per sample; type as an output mix | The tone stage's filter form (§5.3) |
| **Bipolar tone law** | Space / CrumpBit `ton`: LP below 63, HP above, 63 flat | The tone row's convention (S095 P4 §2.7) |
| **Division-free soft clip** | Colloid `col_drive()`: `x − (4/27) x³` clamped at ±1.5, reaching exactly 1.0 there | The drive's curve (§5.4). The distortion curve and the OTA Padé both divide every sample; four VDIV per sample would cost ≈ 56 cycles |
| **Drive make-up table** | S095 P4 C3: a generated 128-entry table (`distortionMakeup.c`, half make-up) | One of the same kind for this drive |
| **SRR law** | Drum `srt`: `mixer_decimation_rate = valueShaperI2F(v, −0.7)`, a float sample-and-hold counter (`mixer_decimateBlock()`, `mixer.c:174`; `InstrumentManager.c:4349`) | The per-layer SRR (§5.5), run inside the layer loop |
| **Stereo voice output** | `instrumentManager_slotRight()`: the mixer takes a right block from the voice (Colloid) | Tetrakt is a stereo voice |
| **Voice units** | `FxBuffer.h:93–100`: 12 units of 2,208 int16 samples (50.06 ms), at most 2 per slot, taken from the top of the Effect arena; `fxbuf_voiceAcquire()` / `fxbuf_voiceUnit()` | The S1 line. **No caller yet.** The claim and release are S095 P5 step 3, shared with the Katran draft and the Advanced types |
| **Trigger order** | `voiceControl_processPending()` (`MidiVoiceControl.c`): "Apply this step's own automation before any of its triggers" | The `lon` row can be stepped |
| **Slot reserve** | 1,264 B per tagged slot; `OscInfo` 72 B, `Lfo` 124 B (*measured*, `sizeof` on the tree) | §10 |

---

## 2. The layer

```
read --> x decay x velocity --> drive --> SRR --> tone (2-pole, bipolar, env-swept) --> x level --> pan L/R --> mix bus L/R
                                                                                                 \-> send --> S1
                                                                         right side: + W2 allpass x (1 - env)
```

| Stage | Per layer | Rate |
|---|---|---|
| Sample | the latched sample (§3.3), read at the layer's rate | per sample |
| Pitch | rate = 2^(semitones / 12), semitones = (note − 63) + the `tuA` law (§5.1) | per block |
| Decay × velocity | the decay envelope, ramped over the block, × the latched velocity when `vel` is on (§4.2); the R1 fade (§3.2) | per block, ramped per sample |
| Drive | gain, a bias that grows with the drive, the soft clip, the bias's DC removed (§5.4) | per sample; gains per block |
| SRR | sample-and-hold at the `srA` rate; 127 is the full rate (§5.5) | per sample |
| Tone | a TPT state-variable filter swept from LP through flat to HP by `tnA` + `teA` × env; coefficients ramped per sample (§5.3) | per block coefficients, per sample filter |
| Level | `lvA` × the drive's make-up | per block |
| Pan | the platform's pan law per layer (`pnA` … `pnD`, `DTYPE_PM63`, 63 centre) | per block gains |
| Voice balance | the voice's `pan` row: the mixer's per-slot pan, applied to the finished stereo output as L × panL, R × panR (`mixer_addVoiceStereoToOutput()`, as the Colloid). An attenuation balance: it does not move the layers within the image, and it is distinct from the width | in the mixer |
| Send | `spA` … `spD` into S1, after the pan's mid | per sample |
| Width (W2) | the right side's share through a first-order allpass, mixed in by (1 − env)² × `wid` (§6.2) | per sample |

**Why this order (revision 3, user):**
- **The tone last** filters everything before it: it can take the fizz
  off the drive and the steps off the SRR (an LP setting), or keep only
  their edge (HP). The envelope sweep then shapes the finished sound: a
  bright, crunchy hit that closes as it decays. That is the amp-then-
  cabinet order, and it makes the drive and the SRR sound better than
  they do last.
- **The SRR after the drive** crunches the driven signal, and its hold
  images of the drive's harmonics are what the tone then smooths.
- **Decay and velocity before the drive:** a hard hit drives harder, and
  a decaying tail cleans up as it falls, as a real overdriven sound does.
- **`lvA` after everything:** it is a mix level. Balancing a layer does
  not change how dirty it is.
- **What revision 2's order (tone before the drive) did differently:**
  the envelope moved the spectrum going into the drive, so a bright hit
  was also driven harder in its highs. With the tone last, the drive sees
  the raw sample, and the tone sweep is a filter on the result. That is
  the more usual sound, and it lets the tone tame the drive.

---

## 3. Triggers, on/off, and playing out

### 3.1 The rule

- At a trigger (sequencer, MIDI, pad, preview: the one funnel
  `instrumentManager_triggerTrack()`), every layer whose bit is set in
  `lon` (§4.3):
  - restarts its sample from the start;
  - restarts its decay at full level;
  - latches the trigger's velocity, and its sample (§3.3).
- A layer whose bit is clear is left alone: it keeps reading and decaying
  until it is silent.
- If no layer is on, the trigger restarts nothing. It still runs the
  platform part: the LFO retrigger.
- `lon` is a selector row (`ROW_SELECT_ONOFF`: automatable, never
  morphed), so step locks, Pattern automation and MIDI all reach it. It is
  not an LFO target. A step lock on step N applies to step N's trigger:
  the automation drain runs before the triggers in the same pass.

**Example:** `lon` = `AC` (A kick, C hat). Each step restarts A and C. A
step locked to `ACD` also restarts D, a crash, which rings out under the
following `AC` hits.

### 3.2 Restarting a layer that is still sounding (R1, decided)

- **What:** the retriggered layer fades its old read to 0 over one block
  (32 samples, 0.73 ms), and the new read starts in the next block. The
  fade is in the decay gain, at the head of the chain, so the drive, SRR
  and tone see it go to 0 and the tone's state rings down with it.
- **Cost:** none per sample; one flag per layer.
- **Who waits:** only a layer that was still sounding (above the quiet
  threshold). A silent layer starts in the trigger's own block. 0.73 ms
  is below a flam, and the same for every layer, so the layers keep their
  relative timing.

### 3.3 What latches at the trigger (decided)

| Value | Latched at the layer's retrigger | Live (per block) |
|---|---|---|
| Sample choice (`smA`) | **yes**: a change while one is playing would jump into the middle of another file. As the Colloid's `wtb` frames (C-D4) | |
| Velocity | **yes**: a soft hit does not change the ringing layers | |
| Level, decay, pitch, pan, drive, SRR, tone, send | | **yes**, so LFO, Morph and automation move a playing layer |

---

## 4. Rows

### 4.1 Per layer (10 × 4 = 40), plus the layer switch (1)

Short names carry the layer letter (revision 6). The table gives layer A;
B, C, D are the same rows with their own letter. Category and long name
are the descriptor's two label strings (as `ROW(key, cat, long, short)`).

| Row (layer A) | Short | Category / long | Stored | Law | Notes |
|---|---|---|---|---|---|
| Sample | `smA` | Sample / Sample | 0 … N−1 (installed samples) | the catalogue index; past the last, the last | dynamic domain, as the `wtb` list (`INSTRUMENT_MOD_DOMAIN_DYNAMIC_MAX`). The page shows the catalogue's display names. With no samples installed, the layer is silent |
| Tune | `tuA` | Sample / Tune | PM63 (63 = 0), shown −63 … +63 | 20 cents per step; a whole semitone on every multiple of 5; ±12 from ±60 to the ends (§5.1) | continuous (decided); default 0 |
| Level | `lvA` | Sample / Level | 0 … 127 | v / 127, after the drive's make-up | the layer's mix level (§2) |
| Decay | `dyA` | Envelope / Decay | 0 … 127 | `colEgK[v]`: time constant 1 ms … 10 s, log-spaced | §5.2 |
| Pan | `pnA` | Sample / Pan | PM63 | the platform's pan law | the one-pan rule (`MEMORY.md`) |
| Trigger | `onA` | Sample / Trigger | one bit of `lon` | §3, §4.3 | **not a row per layer:** one of the four cells of the switch row |
| Drive | `drA` | Sample / Drive | 0 … 127 | §5.4; 0 = exact bypass | per layer (decided) |
| SRR | `srA` | Sample / SampleRt | 0 … 127 | 127 the full rate, lower is coarser: the drum / mixer `srt` law (§5.5) | default 127 |
| Space send | `spA` | Sample / Space | 0 … 127 | v / 127 into S1 | |
| Tone | `tnA` | Sample / Tone | PM63 | below 63 LP, above HP, 63 flat (§5.3) | the bipolar `ton` convention |
| Tone envelope | `teA` | Tone / Envelope | PM63 | adds (`teA` − 63) × env to `tnA` per block | positive: brighter at the hit, darker as it decays; negative: the reverse |

**File keys** (proposed; the descriptor's first string, never shown): `tkt_sample_a`,
`tkt_tune_a`, `tkt_level_a`, `tkt_decay_a`, `tkt_pan_a`, `tkt_drive_a`,
`tkt_srr_a`, `tkt_space_a`, `tkt_tone_a`, `tkt_tone_env_a`, and `_b`, `_c`,
`_d`; the switch `tkt_layer_on`.

### 4.2 Velocity: the Drum's method (revision 3, user)

**Rows (3):** as the Drum (`DrumParameters.c:286–287`): `velo_vol_on_off`
(`vel`, on/off), `velo_mod_amount` (`amt`) and `velo_mod_dest` (`dst`).

- **`vel` on:** each retriggered layer's level is scaled by its velocity,
  in the decay × velocity stage before the drive (§2). A soft hit is
  quieter and a little cleaner, a hard one louder and dirtier. The drive's
  half make-up keeps the level following the velocity.
- **`amt` / `dst`:** one destination, one amount, as on every engine.
  - **`dst` a layer row** (tune, level, decay, pan, drive, SRR, space,
    tone, tone envelope): the destination is the kind of row, and the
    amount × velocity is applied to that row on **all four layers**. The
    target list names each kind once with a `*` for "all four": `tu*`,
    `lv*`, `dy*`, `pn*`, `dr*`, `sr*`, `sp*`, `tn*`, `te*`. `tn*` means
    the tone of A, B, C and D.
  - **`dst` a voice row** (`vol`, `pan`, `stm`, `sfb`, `sdt`, `sto`,
    `wid`, `slv`): applied to that row.
  - The samples (`sm*`) and the trigger switch are not velocity targets
    (selectors).
- **Per layer:** each layer uses the velocity of **its own last
  retrigger** (Q11, latched). A soft hit that retriggers A and C moves
  A's and C's tone. The crash ringing on D keeps the velocity it was hit
  with.
- **How it differs from the platform node:** the platform's velocity
  node writes one descriptor's runtime value. Tetrakt takes the node's
  amount and target, and its async adds amount × v_layer to the chosen
  row of each layer per block. The schedule checks the target list
  (`DTYPE_TARGET_SELECTION_VELO`) so that it names each kind once (the
  layer A row standing for the group, shown as `tn*` rather than `tnA`),
  and that `instrumentManager_applyVelocityModulationTarget()` leaves a
  Tetrakt target to the voice.
- **Cost:** per block only (four adds and clamps); nothing per sample.

### 4.3 The layer switch: one row, four controls (revision 4, user)

**The row:** `tkt_layer_on` (`lon`, Sample / Trigger), stored 0 … 15. Bit 0 is
layer A, bit 3 layer D. It is a selector (`ROW_SELECT_ONOFF`) with a
`DTYPE_ON_OFF` name tag (a new `ONOFF_TAG_TKT_LAYERS`; tags 1 … 6 are
taken, and the tag is the dtype's high nibble, so there is room). Its 16
names are the layer letters, in value order:

| Value | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Name | `---` | `A` | `B` | `AB` | `C` | `AC` | `BC` | `ABC` | `D` | `AD` | `BD` | `ABD` | `CD` | `ACD` | `BCD` | `all` |

Every combination fits the 3-character cell. This is the P4 `hed` precedent
(head sets named by letters, `SpaceParameters.c` `space_hedNames`), so
every 3-character view (step automation, lane search, the MIDI Learn
list) names the set without new code.

**The page: four controls.**
- **What you see:** the MOD page's first screen shows the four layers as
  four cells, `onA` … `onD` (Sample / Trigger), each `on` or `off`. Pot n
  toggles layer n (turn right: on, left: off). The encoder on a cell
  toggles it as well. A bit cell's short name is the row's base (`on`)
  plus its layer letter, so the registry field that names the switch row
  is all the menu needs.
- **The commit:** each edit commits the whole row (the mask with that bit
  changed) through the normal path (`menu_commitTargetEdit()`), so R12,
  AutoSave, the edit mask fan-out and MIDI echo see one row.
- **What is new in the menu:** an instrument page cell can only name a
  descriptor today (`instrument_menu_page_t.descriptor_index`, with
  `INSTRUMENT_MENU_EMPTY` 0xFF and `_SKIP` 0xFE). Revision 4 adds a cell
  code for "bit b of the type's switch row", with the row named by a
  registry field. The menu resolves it to the row plus a bit, formats
  `on`/`off`, edits by setting or clearing the bit, and finds the row's
  automation (underline, search) as for the whole row.

  This is the one piece of platform work the switch needs. The schedule
  writes it with its own host or device checks.

**Behaviour that follows from one row:**

| Path | Behaviour |
|---|---|
| Step lock | A lock stores the whole mask: the step's layer set. Locking C on a step also fixes A, B and D at their values when the lock was made. With four rows, the others would have followed their base. Here a lock reads as "this step plays these layers" |
| Pattern automation, live record | One target. Recording a toggle records the new mask |
| MIDI | For now one CC / NRPN, value 0 … 15 (the selector clamp holds values above 15 at `all`). **A Tetrakt-specific MIDI rule is deferred (user, 2026-11-14), to be worked out later together with the Colloid's** |
| Files, AutoSave | One key, stored as the number |
| Morph, LFO | Neither (a selector, as `on` was) |
| Default | `A` (1): a fresh Tetrakt triggers layer A only; the other layers are switched in as they are set up (user, T-Q7) |

**What the whole-mask lock changes in practice** (T-Q8, decided). The
switch is read only at a trigger, so the only thing that matters is the
set each trigger sees. That set is the step's lock if it has one, else the
base:
- **A lock is a snapshot of all four; a partial lock does not exist.**
  - Lock step 5 to add the crash while the base is `AC`: step 5 stores
    `ACD`. Turn B on in the base later, and every unlocked step plays
    `ABC`, but step 5 still plays `ACD` until it is re-locked. With four
    rows, a lock on D alone would have followed the base and played
    `ABCD`.
  - Live recording does the same: a recorded toggle on a step writes the
    whole mask there, so that step stops following later base changes.
  - On the step page a lock shows and edits all four bits. "Unlock D
    only" does not exist: the step either keeps its mask lock or loses it
    (and plays the base).
- **No difference from R12.** At a trigger the menu override is cleared
  (`seq_restoreAutomatedParameters()`) before the step's automation is
  drained, and the switch matters only at the trigger. A panel edit
  therefore never overrides a locked step's set, with one row or with
  four.
- **Less Pattern memory.** A locked step uses one automation entry from
  the shared Pattern pool, not up to four.
- **Wanted (user):** a locked step never changes, even when the base
  does. The page shows this through the underline below.

**The automation underline: the four cells always together** (revision 5,
user). The four layer cells are one row, so every view that marks
automation marks all four or none:

| View | Rule |
|---|---|
| VOICE page, the automation-presence search (`va_scanService()`, `MENU_UI_SPEC.md` §10) | If any step of the track has a `lon` entry, all four layer cells are underlined. The search matches the row's target and sets the marker of every visible cell that names a bit of it |
| A held step (the step-automation overlay, `va_resolveHeldValue()`) | If the step has a `lon` lock, all four cells are underlined, each showing its own bit of the locked mask (`on` / `off`). If it has none, no cell is underlined, and the four show the base |
| Editing a layer on a held step | Toggling one cell writes the whole mask as the step's lock (§4.3). From that moment all four cells are underlined, so the page shows the step became a snapshot of the four, not a lock on one layer |
| The step-automation list and editor | One entry, `lon`, with the mask's name (`ACD`) |

- **The LCD:** an underlined cell uses one marker glyph, and the menu
  keeps four CGRAM slots (2 … 5) for them (`va_queueMarkerTransaction()`,
  `MENU_UI_SPEC.md` §4.3). A screen has four cells, so a fully underlined
  layer screen uses at most the four slots, as any screen of four
  automated rows does today.
- **The working value:** while a held step's cell is being edited, the
  overlay keeps a working value per cell (`va_workingValue[]`). The four
  layer cells share one, the mask, so a toggle on one cell redraws the
  other three from the same value.
- **Unchanged:** the trigger rule, playing out, a lock applying to its own
  step, and recording (each recorded change stores the mask at that
  moment).

---

## 5. Laws

The laws are written for layer A (`tuA`, `drA` …); B, C and D are the
same with their own rows.

### 5.1 Pitch (continuous, whole semitones every 5 steps)

- **The law:** d = `tuA` − 63 (−63 … +63), clamped to ±60;
  semitones = d / 5.
  - Each step is 20 cents.
  - **Every 5th step is a whole semitone:** ±5 is ±1, ±35 is ±7 (a
    fifth), ±60 is ±12 (an octave).
  - The last three steps at each end hold ±12, so the brief's ±12 is
    exact at the ends and the rest of the row is uniform.
- **The rate:** `inc` = 2^((note − 63 + semitones) / 12) × 4,096 (Q19.12:
  1.0 plays at the recorded speed), once per block per layer with
  `alk_exp2f()`.
  - The trigger note transposes all four layers together; 63 is the
    default note.
  - At +12 the read steps 2 frames per sample, and the zero padding still
    ends it cleanly.
- **The page:** the plain bipolar number, as every PM63 row (the one-pan
  rule: stored 63 shows 0, the ends show −63 and +63).
  - **The whole semitones are on the multiples of 5:** `5` is +1, `35` is
    +7 (a fifth), `-25` is −5.
  - **The ends are an octave:** `60` … `63` all play +12, and `-60` …
    `-63` all play −12.
  - No label hook: the number is the value (S095 P4's plain-number rule).
- **Aliasing:** linear interpolation above 0 semitones aliases as the
  drum `smp` path does.

### 5.2 Decay

- **The curve:** level ← level × (1 − k), with k = `colEgK[dec]` per
  block, linearly ramped across the block: an exponential, analogue
  shape.
- **The end:** below 1e-4 (−80 dB) the layer is quiet, and its level
  holds 0 until its next retrigger.
- **No attack:** a layer that starts from silence starts at its sample's
  own onset. R1 covers a restart.
- **Quiet:** the voice is quiet (`instrumentManager_ampEnvelopeQuiet()`)
  when all four layers are.

### 5.3 Tone: a 2-pole bipolar filter, swept by the envelope

Revision 1 had two one-poles (6 dB/oct). Revision 2 spends the budget on a
12 dB/oct state-variable filter:

- **The filter:** one TPT state-variable filter per layer (the Colloid's
  `ColSvf` form), with damping R fixed at 0.6. That is Q ≈ 0.83: a slight
  presence bump at the corner, for the ear in §13.
  - It gives LP, BP and HP from one state, and LP + 2R·BP + HP = x
    exactly.
- **The effective tone** per block: t = `tnA` + (`teA` − 63) × env,
  clamped to 0 … 126; u = (t − 63) / 63, −1 … +1.
- **The output:** out = x − w_L·(2R·BP + HP) − w_H·(LP + 2R·BP).
  - **u < 0 (LP side):** w_H = 0. w_L rises from 0 to 1 over the first
    eighth of the side, so it is 1 for u ≤ −1/8. The cutoff falls from
    18 kHz (u = 0) to 120 Hz (u = −1).
  - **u > 0 (HP side):** w_L = 0. w_H rises the same way. The cutoff
    rises from 20 Hz (u = 0) to 3 kHz (u = +1).
  - **u = 0:** both weights are 0 and out = x exactly. A flat tone is
    the dry sample.
- **Why one state:** the envelope sweeps u across 0 while the layer
  plays. One filter, always running, changes only its output weights,
  and both are 0 at the crossing, so the sweep is continuous. Revision
  1's two one-poles needed a second state to manage the same thing.
- **Smooth sweeps:** a short decay with a large `teA` moves the cutoff a
  long way in the first few blocks.
  - The coefficients (a1 a2 a3) and the weights are computed at the
    block's end and ramped per sample from the block's start, as
    `ColSvf`.
  - The per-block cost is one division per layer (a1 = 1 / (1 + 2Rg +
    g²)) and a tan from a generated 128-point table, interpolated.
- **Ranges by ear:** 120 Hz … 18 kHz and 20 Hz … 3 kHz, set in the
  listening step.

### 5.4 Drive: asymmetric soft clip that warms as it is pushed

Revision 1 had the plain cubic. Revision 2:

- **Gain:** g = 2^(5 × `drA` / 127), × 1 … × 32 (the Alkaline's overdrive
  range).
- **Asymmetry:** a bias b = 0.25 × (`drA` / 127)² is added before the
  clip, and the clip of the bias is subtracted after:
  y = clip(g·x + b) − clip(b).
  - Low drive is a symmetric warmth (odd harmonics). As the drive rises,
    the bias brings in even harmonics, a "tube-like" thickening.
  - The subtraction keeps a silent input silent (no static offset).
- **The clip:** `x − (4/27) x³`, clamped at ±1.5 (exactly ±1.0 there).
  Division-free.
- **The bias's moving DC:** asymmetry rectifies a little of the signal.
  One DC blocker (a one-pole HP at about 8 Hz) on each of the summed L/R
  buses removes it, so it runs twice, not eight times.
- **Make-up:** m(`drA`) from a generated table (half make-up, as S095 P4
  C3), folded into `lvA` per block.
- **`drA` 0 is an exact bypass:** the dry signal is crossfaded in over the
  first 8 steps (`COL_POST_WET_STEPS`, the Colloid's rule). The cost
  stays constant: the clip always runs.
- **Considered, not proposed:**
  - **2× oversampling of the clip:** +≈ 12 per layer, against aliasing
    that the cubic (3rd harmonic only, until the clamp) mostly avoids.
  - **ADAA:** a division per sample per layer (4 VDIV, ≈ 56 cycles per
    sample).

### 5.5 SRR per layer (new)

- **What:** a sample-and-hold on the driven signal, after the drive and
  before the tone (§2). It uses the `mixer_decimateBlock()` method: a
  float counter adds the rate each sample and takes a new sample when it
  passes 1.
- **The law:** rate = `valueShaperI2F(srA, −0.7)`, exactly the drum and
  mixer `srt` law (`InstrumentManager.c:4349`): 127 is the full rate and
  the default, and lower values hold longer (user, T-Q4).
- **Pitch and SRR are independent:** the hold rate is in output samples,
  so a pitched-up layer keeps its grit.
- **Cost:** ≈ 6 per sample per layer. It always runs (constant CPU); at
  127 the counter takes a new sample every sample, so the output is the
  input exactly.

---

## 6. The space stage (decided: S1 + W2)

Both run every block, whatever their amounts (the constant-CPU rule).

### 6.1 S1: one 50 ms line, two taps

```
sum of sends (mono) --> line (1 voice unit, 2,208 samples) --> tap L at t
                         ^                                 \-> tap R at t x (1 - o) + wobble
                         \--- feedback (from the taps' mid), one-pole LP <---/
```

- **The line:** one voice unit holds 50.06 ms of int16. The layers' sends
  sum into it; the feedback is the two taps' mid through a one-pole LP
  (`sto`, Space / Tone).
- **The taps:** the left tap reads at time t (`stm`, Space / Time,
  1 … 50 ms). The right tap reads at t × (1 − `sdt` × 0.15) (`sdt`,
  Space / Detune), plus a slow wobble of a few
  samples. That is the "detune offset": the two sides at slightly
  different times and slightly moving pitch.
- **Output:** the left tap to the left output, the right to the right,
  both at `slv` (Space / Level). `sfb` (Space / Feedback) sets the
  repeats.
- **Cost:** ≈ 25 … 30 instructions per sample *est.* (one write, two
  fractional reads, the feedback LP, two adds).
- **Memory:** one unit per Tetrakt (4,416 B from the Effect share, given
  back when the type leaves the slot). Six Tetrakts take six of the twelve
  units and leave six for Katran or the Advanced types.

### 6.2 W2: each layer widens as it decays

- **What:** per layer, the right channel's share passes through a
  first-order allpass (a fixed coefficient per layer, so the four
  decorrelate differently). It is mixed in by (1 − env)² × `wid` (Space /
  Width).
- **What you hear:** at the hit, env ≈ 1, so the layer is exactly as
  panned, a centred transient. As it decays, the phase difference
  between the sides grows and the tail spreads.
- **The curve (1 − env)²** is fixed: revision 1's `wsh` row went to make
  room for the SRR rows. The width arrives late in the decay, which keeps
  the body focused.
- **Cost:** ≈ 5 per sample per layer (one stage).
  - Two stages (revision 1's W2) give a stronger decorrelation for
    another ≈ 5 per layer. That is the first item to add if the device
    leaves room (§9.3).

---

## 7. Signal flow and render order

| Step | Rate | What |
|---|---|---|
| 1 | per block (async) | layer control: the decay step, the R1 fades, the velocity's contribution per layer (§4.2), the pitch `inc`, u and the tone's end coefficients and weights (one division per layer), the drive gain, bias, clip(b) and make-up, the pan and send gains, the SRR rate, the W2 mix; S1 tap times and wobble |
| 2 | per block (sync), per layer | `osc_smpPlainBlock()` into the 32-sample int16 scratch (gain 1) |
| 3 | per sample, per layer | load; × decay × velocity (ramped, with the R1 fade); drive; SRR; tone (SVF, ramped coefficients and weights); × level; pan into the float L/R buses (W2 on the right share); × send into the send bus |
| 4 | per sample | S1: the line write, the two taps, the feedback; the taps added to L/R |
| 5 | per sample | the DC blockers on L/R; pack L into the mixer's buffer and R into `right[]`, saturated to int16 |

Steps 2 and 3 are two loops per layer: the existing ITCM read, then one
new post loop (in flash). The read is reused (Q9, decided).

---

## 8. Parameters and pages

### 8.1 Rows: 64 of 64

| Group | Rows | Count |
|---|---|---|
| Layers A–D | `sm tu lv dy pn dr sr sp tn te` (+ the letter) × 4 | 40 |
| The trigger switch | `lon`, shown as `onA` … `onD` (4 bits, §4.3) | 1 |
| Voice | `vol` (`instrument_vol`, Voice / Volume); `pan` (`instrument_pan`, Voice / Panning: the L–R balance, §2) | 2 |
| LFO (the platform's) | the pair's 8 rows (`frq am1 am2 wav rtg pol snc ofs`); the targets `vo1 ds1 vo2 ds2` | 12 |
| Velocity (the Drum's) | `vel amt dst` (§4.2) | 3 |
| Space | `stm` Time, `sfb` Feedback, `sdt` Detune, `sto` Tone, `slv` Level (S1); `wid` Width (W2) | 6 |
| **Total** | | **64** |

- **The limit:** the Kit image holds 64 cells per instrument
  (`INSTRUMENT_PARAM_COUNT`). All 64 are used. Rows are append-only from
  the first release, so a later row means trading one away (§12.2 T-Q2
  lists the order).
- **Fixed instead of a row:** only W2's curve, (1 − env)².
- **The voice pan:** a row again (revision 6). Revision 3 had fixed it at
  centre.
- **No global tune row:** the trigger note transposes the four together,
  and an LFO can target any layer's tune.

### 8.2 Pages (revision 6, user)

Each SELECT sub-page holds up to four screens of four cells
(`INSTRUMENT_MENU_PAGE_CELLS` 16). On every per-layer screen pot 1 is
layer A and pot 4 layer D. Labels are category / long name.

| SELECT | Screen | Cells | Labels |
|---|---|---|---|
| 1 OSC | 1 | `smA smB smC smD` | Sample / Sample |
| | 2 | `tuA tuB tuC tuD` | Sample / Tune |
| 2 AEG | 1 | `lvA lvB lvC lvD` | Sample / Level |
| | 2 | `dyA dyB dyC dyD` | Envelope / Decay |
| | 3 | `pnA pnB pnC pnD` | Sample / Pan |
| 3 MOD | 1 | `onA onB onC onD` (the `lon` bit cells, §4.3) | Sample / Trigger |
| | 2 | the velocity rows, as the Drum's page: `dst amt vel` | Velocity / … |
| 4 FM | 1 | `drA drB drC drD` | Sample / Drive |
| | 2 | `srA srB srC srD` | Sample / SampleRt |
| 5 click | 1 | `spA spB spC spD` (the sends into the space stage) | Sample / Space |
| | 2 | `stm sfb sdt sto` | Space / Time, Feedback, Detune, Tone |
| 6 FILTER | 1 | `tnA tnB tnC tnD` | Sample / Tone |
| | 2 | `teA teB teC teD` | Tone / Envelope |
| 7 LFO | 1 … 3 | the standard LFO screens: `frq am1 am2 wav`, `rtg pol snc ofs`, `vo1 ds1 vo2 ds2` | as every engine |
| 8 MIX | 1 | `vol pan wid slv` | Voice / Volume, Voice / Panning, Space / Width, Space / Level |

- **MIX has one instrument screen.** The next screen of the MIX page is
  the Scene and fader settings (the platform's). Nothing else goes on MIX,
  so that screen stays second.
- **Long names fit:** every long name is at most 8 characters
  (`SampleRt`, `Feedback`, `Envelope`).
- **The SELECT buttons keep the platform's labels** (OSC … MIX). The
  screen's category and long name say what each cell is, as on the
  Colloid.

---

## 9. Cost

### 9.1 Composition (*est.*, instructions per sample, dearest settings)

| Part | Per layer | × 4 | Notes |
|---|---|---|---|
| Read (`osc_smpPlainBlock`) | 24 | 96 | *measured* loop length; ITCM |
| Scratch store and load | 3 | 12 | the price of reusing the read |
| SRR | 6 | 24 | §5.5 |
| Tone (SVF core 8, the two weighted outputs 3, the coefficient and weight ramps 3) | 14 | 56 | §5.3 |
| Decay × velocity ramp | 1 | 4 | |
| Drive (gain, bias, clip, minus clip(b), dry/wet) | 9 | 36 | §5.4 |
| Level, pan into the L/R buses, send (three bus loads and stores) | 11 | 44 | |
| W2 (one allpass stage, mixed) | 5 | 20 | §6.2 |
| **Layers** | **73** | **292** | |
| S1 | | 25 … 30 | |
| DC blockers on L/R | | 6 | |
| Output pack | | 8 | |
| Control per block / 32 | | ≈ 22 … 26 | four layers: decay, fades, `alk_exp2f`, the tone's tan lookup and division, drive and pan gains, SRR rate; S1 taps |
| **Total** | | **≈ 353 … 362** | |

### 9.2 Against the Basic share (19,620 cycles per block)

| Cycles per instruction | Where the figure comes from | Cycles per block at ≈ 355 | Margin |
|---|---|---|---|
| 1.4 | the Colloid, measured (oscillator loops in ITCM) | ≈ 15,900 | ≈ 3,700 |
| **1.6** | **planned here**: one ITCM read loop, a flash post loop that stays in the I-cache, four sample streams through the D-cache, no per-sample division | ≈ 18,200 | ≈ 1,400 |
| 1.8 | the drum voices' worst (S094 P2 run 2: ≈ 14,800 cycles for ≈ 258 instructions) | ≈ 20,400 | over by ≈ 800 |

- **"Closer to the Basic cap":** at the planned 1.6, the margin is about
  the Colloid's (≈ 1,600). At the Colloid's own 1.4 there is room for the
  additions in §9.3.
- **The sample streams:** four layers read four places in sample flash
  at once. The flash region is cached, and a 32-byte line holds 16 frames,
  so a sequential stream misses about once per 16 samples. The device run
  decides the cycles per instruction (§13 step 3).

### 9.3 Trim ladder (if the device puts it over) and additions (if under)

| If over: in this order | Saves per sample *est.* |
|---|---|
| 1. W2 on the summed bus, driven by the level-weighted envelope of the four, instead of per layer | ≈ 14 |
| 2. The tone's weight ramp per block instead of per sample (the coefficient ramp stays) | ≈ 4 |
| 3. S1's wobble fixed (the offset stays) | ≈ 4 |
| 4. SRR dropped (its four rows become spares) | ≈ 24 |

| If under: in this order | Costs per sample *est.* |
|---|---|
| 1. W2 at two allpass stages | +20 |
| 2. 2× oversampling of the drive's clip | +48 |

---

## 10. Memory (*est.*)

| Item | Bytes |
|---|---|
| `Lfo` (the platform pair, *measured*) | 124 |
| 4 × `OscInfo` (the read's state, so `osc_smpPlainBlock()` is reused as it is; *measured* 72 each) | 288 |
| 4 × layer state: the row bytes (10; plus the 1-byte `lon` once for the voice), level and ramp (8), the R1 flag and latched velocity (8; the velocity also feeds §4.2's per-layer contribution), the SVF states, coefficients, ramps and weights (48), the drive's gain, bias, clip(b) and wet (16), the pan and send gains (12), the SRR counter, rate and hold (12), the W2 state, coefficient and mix (12) | ≈ 4 × 127 = 508 |
| `right[32]` int16 (the mixer reads it) | 64 |
| S1: write index, tap phases, wobble phase, feedback LP state | ≈ 24 |
| DC blocker states (L, R) | 8 |
| **Voice struct** | **≈ 1,016** against the 1,264 B reserve |
| **Shared work area, one for every Tetrakt:** the read scratch (64 B), the float L/R buses (256 B), the send bus (128 B) | **448, new RAM** |

- **Why a shared area:** the renders run one slot at a time (the mixer
  calls each slot's async and sync in turn), so the buses are never in use
  by two Tetrakts at once. Inside the struct they would make it about
  1,460 B, over the reserve.
- **Approved (user, 2026-11-14):** 448 B of `.bss` in SRAM1 (D-cached
  since S094 P2), owned by the Tetrakt voice file.
  - DTCM is earmarked for delay lines, and the free SRAM1 for Pattern
    data. 448 B is 0.5 % of the 87,404 B SRAM1 left.
- **The alternative without new RAM**, not taken (struct ≈ 1,256 B, 8 B
  under the reserve: too tight to plan on):
  - keep the read's five fields (data, size, phase, step, gain: 20 B) in
    the layer state instead of a whole `OscInfo`, with a Tetrakt copy of
    the read loop: −208 B of RAM, but
    +≈ 100 B of ITCM (needs approval) and one more loop to gate;
  - the buses stay in the struct.
- **The voice unit (S1)** comes from the Effect arena by the FxBuffer
  contract. The owner claims and clears it at type assignment, never at a
  row change.

---

## 11. Storage and the platform

- **New type, no migration:** token `tkt`, extension `.tkt`, folder
  `/Instrument/Tetrakt`, label "Tetrakt" (decided). The rows are
  append-only from the first release.
- **Morph:** every continuous row is morphable. The samples (`smA` …),
  `lon` and `vel` are selectors (never morphed). Morph moves a playing
  layer's level, tune, pan, drive, SRR and tone. The sample changes at the next retrigger
  (§3.3).
- **LFO:** the platform pair, as every engine. Any row except the
  selectors can be a target, one row each (`tuA`, `pnB`, `tnC`, `srD` …).
  Only velocity has the "all four" targets (§4.2).
- **Velocity:** the Drum's `vel` / `amt` / `dst`, a layer destination
  applied to all four (§4.2).
- **The voice pan:** the `pan` row (`instrument_pan`), read by the mixer
  through the Tetrakt branch of `instrumentManager_runtimePan()`, as every
  type. The function's default branch returns 64, a pre-existing
  off-by-one against the one-pan rule (63 = centre); a Tetrakt slot never
  reaches it.
- **The slot-6 alternate decay** (track 7 on slot 6) looks for an
  `amp_envelope_decay` row. Tetrakt has none (four decay rows with their
  own keys), so track 7 plays slot 6 as an ordinary hit, as the Colloid
  does.
- **Sample install:** samples come from `Load:[Samples]` as today. A
  Scene that names a sample index plays whatever is at that index on the
  card (the drum `smp` rule).

---

## 12. Questions

### 12.1 Decided (user, 2026-11-14)

| # | Decision |
|---|---|
| Q1 | `tkt`, `.tkt`, `/Instrument/Tetrakt`, "Tetrakt" (Morph row "TetraMrp") |
| Q2 | Basic |
| Q3 | S1: one voice unit, two taps, detune offset |
| Q4 | W2: the envelope-derived width (§6.2) |
| Q5 | Pitch continuous, with whole semitones at regular positions: every 5th step (§5.1) |
| Q6 | Drive per layer |
| Q7 | R1: fade, then start |
| Q8 | `lvl` per layer, included |
| Q9 | Reuse `osc_smpPlainBlock()` |
| Q10 | The sample latched at the layer's retrigger |
| Q11 | Velocity latched per layer at its retrigger |
| R2 brief | Tone and drive improved toward the Basic cap (§5.3, §5.4, §9); SRR per layer before the tone (§5.5) |
| R3 order | read → decay × velocity → drive → SRR → tone → level → pan / send (§2) |
| R3 velocity | the Drum's method: `vel` on/off and one `amt` / `dst`; a layer destination applies to all four layers (§4.2) |
| R4 switch | the four on/off switches are one 4-bit row, `lon`, with four controls on the page (§4.3) |
| T-Q8 | a step lock on `lon` stores the whole mask (§4.3); a locked step never changes when the base does (wanted) |
| R5 underline | wherever `lon` is automated or locked, all four layer cells are underlined together (§4.3) |
| MIDI | a Tetrakt MIDI rule (and the Colloid's) deferred, to be worked out later |
| R6 layout | the pages, short names and labels of §8.2; the voice `pan` as a row (an L–R balance, distinct from the width); MIX keeps one instrument screen |

### 12.2 Decided in revision 7 (user, 2026-11-14)

| # | Decision |
|---|---|
| T-Q1 | The 448 B shared work area: approved (SRAM1 `.bss`, owned by the Tetrakt voice file; §10) |
| T-Q2 | Rows: 64 of 64, no spare. If a row is wanted later, the next to go are, in order: LFO `ofs`, S1 `sdt` (a fixed offset), W2 `wid` (a fixed depth) |
| T-Q3 | The tune row shows the plain bipolar number; the whole semitones are on the multiples of 5, and the ends are an octave (§5.1) |
| T-Q4 | SRR: the drum / mixer `srt` law, 127 = the full rate and the default (§5.5) |
| T-Q5 | Tone: damping R 0.6; LP 18 kHz … 120 Hz, HP 20 Hz … 3 kHz; final values by ear (§13 step 5) |
| T-Q6 | Drive: the bias up to 0.25 at full drive; final value by ear |
| T-Q7 | `lon` defaults to `A` |

Nothing is open.

---

## 13. Order of work

1. **Host model** (`tools/dsp_test/modules/tetrakt.c`), on the real
   `osc_smpPlainBlock()`. Laws:
   - **trigger:** for every `lon` value, the set layers restart and the
     clear ones continue bit-identically; R1 reaches 0 in one block; a
     fresh voice (`lon` `A`) restarts layer A only;
   - **decay:** monotone, and it goes quiet;
   - **tune:** `tuA` 63 at note 63 reads at 1.0; every 5th step is a
     whole semitone to 1e-6; ±60 … ±63 read at 2.0 / 0.5;
   - **SRR:** `srA` 127 is the input exactly; the hold rate follows the
     `srt` law;
   - **tone:** u = 0 is the input exactly; a sweep across u = 0 under
     the envelope has no step larger than the signal's own; the LP/HP
     ends match their cutoffs;
   - **drive:** `drA` 0 is the input exactly; a zero input gives zero
     output at any drive (the bias subtraction); after the DC blocker the
     mean of a driven sine is 0;
   - **W2:** env 1 gives L/R exactly as panned;
   - **S1:** the taps read at t and t × (1 − o); the feedback is bounded
     at the maximum.

   Loop counts come from `elf_looppath.py` / `elf_blockpath.py`, against
   §9.1.
2. **Voice units:** the claim and release at type assignment (S095 P5
   step 3).
3. **The menu's bit cell** (§4.3): the cell code, resolve, format
   (`onA` … `onD`), edit and automation lookup. Checked on the device on
   the MOD page:
   - pots and encoder;
   - a step lock;
   - MIDI Learn on the row;
   - the underline: all four cells under the search when any step has a
     `lon` entry; all four on a held, locked step, each showing its bit;
     none on a held step without a lock; all four from the moment one
     cell is toggled on a held step.
4. **Firmware type, rows, the §8.2 pages; device probe** (the cycle probe, trace
   stage `p`): six Tetrakts with four layers playing long samples at +12,
   every stage at its dearest, against 19,620. §9.3 decides any trim or
   addition from the measured cycles per instruction.
5. **Listening:** the default decays, the tone's damping and ranges, the
   drive's bias and make-up share, the SRR curve, S1's offset and wobble,
   and W2's depth.
