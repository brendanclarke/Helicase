# S093 P4 — Alkaline Firmware Implementation Schedule (Phase 1: P4-0 … P4-4)

- **Session:** 093, phase 4: the firmware integration of the Alkaline (TB-303)
  instrument. This document is the literal code schedule for **P4-0 to P4-4**
  (the code into the firmware build and the card image). **Phase 2** (P4-5
  storage and platform record, P4-6 static gates and their tooling, P4-7
  device, P4-8 documentation, risks, the complete file index) is
  `S093_P5_FIRMWARE_IMPLEMENTATION_PHASE_2.md`.
- **Starts from:** `S093_P3_PREP_WORK_WAVES_KERNEL_CONTROL.md` (done). Already
  in the sources and host-tested:
  - Re3/Sw3 removed;
  - the B2R kernel with the fused oscillator read (`DiodeLadder_renderB2Osc`);
  - the N1 network at 64 samples (T2-2) in the generic phase/state form;
  - drive and the make-up;
  - the reduced control path (Tier 1, T2-3, batched divisions, pitch from
    the note).

  Nothing Alkaline is in the firmware build yet.
- **Verified against the tree, not assumed.** Every name, line, file format,
  include chain and tool option below was read in the S093 P3 working tree
  (HEAD `a482be4f` plus the uncommitted P3 changes). The new host code was
  compiled and run on scratch copies; nothing in the repository was changed
  to do so:
  - the new trigger and law AL-P: the Alkaline bench passes all ten groups;
  - `seq_trackStepSamples()` and law AL-T against the real `StepScale.c`,
    including the break build;
  - the M7 stub link of `control_cost.py` with the real headers;
  - `InstrumentManager.c` with the Alkaline includes and union member, using
    the firmware's compile line;
  - `elf_loopsrc.py` on the P3 `-g` twin.

  §7 lists what was verified and how. Line numbers shift as edits land:
  apply one file's changes from the bottom up, or search for the quoted
  anchor.
- **Change tags:** **ADD** (new code or file), **MODIFY** (replace the quoted
  lines), **REMOVE**, **NONE** (verified: no change needed, with the reason).
- **Comment style:** the project's block comment (What / Why / Inputs /
  Outputs / Accessors / Affiliates) on every function and every non-obvious
  variable, statement or loop, as in the code below. The comments are part of
  the change and go into the source as shown.
- **Binding rules** (`MEMORY.md`):
  - commits belong to the user;
  - no RAM beyond R1 (+2 B, approved);
  - no backward compatibility;
  - the CPU ceiling (whole-voice accounting with VDIV);
  - constant per-sample cost;
  - `ResonantFilter.c` twins and existing oscillator loops untouched;
  - no ITCM code.

---

## 0. Decisions

### 0.1 In force (user, S093)

| # | Decision |
|---|---|
| D2‴ | B2R as Basic, accepted after listening |
| (b) | T2-3 lagged accent solve: `ALK_ACC_SOLVES 1` |
| (c) | T2-4 nearest Res point: rejected, removed |
| r6 | 6-state network on the common balanced basis: applied in P4-0 (§2.1) |
| R1 | `menu_instrumentLoadIndex` +2 B `.bss` |
| D14 | Re3/Sw3 removed, no migration |
| D16 | drive ×1..×8 as `8^(v/127)` |
| B-2 | make-up gain through the OTA current, `ALK_MAKEUP_OCT` set by ear |
| T1-7 | `osc_setTableFreq()` |
| B-9 | tracks 6/7 share slot 5, no special handling |
| Fused | `ALK_FUSED_OSC 1` (bit-identical, −9 per sample) |
| Osc | the Alkaline oscillator stays a table wave (P3 §9.5) |

### 0.2 The code-dive findings and the user's answers (S093)

| # | Finding | Answer | Implemented |
|---|---|---|---|
| **F1** | No way to create the first `.alk`: Instrument Load only lists files, Save needs an Alkaline slot, and a slot's type only changes by loading a file of that type | Create `SD_CARD/Instrument/Alkaline/` and a first `init.alk` | §4.6: `SD_CARD/Instrument/Alkaline/init.alk` in the card image, written and checked by `tools/alkaline/make_init_alk.py` |
| **F2** | `storage_instrumentTypeToText()` hard-codes four types (`storageTypes.c` l. 421–427), so every Alkaline save would be refused | Five types now; the count must stay flexible, more may come | §6.3 ToText registry-driven. §5.3 / §4.4 / §4.7: the hidden assumptions a sixth type would hit, made explicit and guarded |
| **F3** | Menu page index = front-panel SELECT button (`menu.c` ≈ l. 4608), eight per type | Assign as you like | §4.2 |
| **F4** | A fresh slot's cells are all 0 (`instrumentManager_resetSlot()` l. 642) | Create the init file; add the instrument to Load/Save (already generalised) | §4.5 init defaults = `init.alk`. Load/Save confirmed registry-driven (§6.4); the one Load defect is fixed in §4.7 |

**F2: what a further type depends on, found in the tree.**

| Assumption | Where it is relied on | Before | Now |
|---|---|---|---|
| Type values are contiguous from 0 and in registry order | `.hcnames` header writer and validator walk `storage_instrumentTypeToText(0, 1, 2, …)` until NULL (`filesystem.c` l. 24616, l. 6614) | Unguarded | Registry rows use designated initialisers `[INSTRUMENT_TYPE_X] = …`, with `_Static_assert(INSTRUMENT_REGISTRY_COUNT == INSTRUMENT_TYPE_UNKNOWN)` (§5.3) |
| Type tokens are exactly 3 characters | HCNAMES rows copy `token[0..2]` (`filesystem.c` l. 24579–24586); AutoSave stores `AUTOSAVE_INSTRUMENT_TYPE_BYTES` (3) | Unguarded | Host check in `check_special_tags.py` (§4.4), run by `make special_tags` |
| Every registered type has a host tag check | `check_special_tags.py` `TABLES` | Silent if a type is missing | The same host check fails when a registry row has no `TABLES` entry (§4.4) |
| Type labels fit "…Mrp" in 8 cells | `menu_instrumentLoadCopyTypeLabel()` (`menu.c` l. 9666) | "Cymbal" → "CymbalMr"; an 8-character label shows no "Mrp", identical to its normal row | Truncate the label to 5 before "Mrp" (§4.7) |
| ToText | `storageTypes.c` l. 412–428 | Four-case switch | Registry lookup (§6.3) |

**Type-specific code left as it is, with the reason:**
- `instrumentManager_classifySpecialKey()` (l. 3869): legacy key rules for
  the four original tables. A new type's keys classify as `NONE` unless it
  uses an `osc1_` key, which is handled for every type.
- Default Scene types (`SceneData.c` l. 1127, `filesystem.c` l. 18241).
- `INSTRUMENT_TYPE_DRM` as a null fallback (menu, filesystem).
- The `"drm"` extension fallbacks for a NULL argument (`storageTypes.c`
  l. 475, 901; `filesystem.c` l. 11614, 12029).

---

## 1. Work order (phase 1)

| Phase | Content | Gate |
|---|---|---|
| **P4-0** | r6 (§2.1); gate baselines and the restored bench ELF targets (§2.2); counter default rows (§2.3) | `gen_alk_tables.py --check`; bench `alkaline` (AL-R on r6 2.018 ms / 0.363); baselines written |
| **P4-1** | Trigger with its platform part (§3.1); `seq_trackStepSamples()` (§3.2); the bench's stand-ins, trigger calls and AL-P (§3.3); AL-T as bench target `seq_steplen` (§3.4) | Bench `alkaline`, `seq_steplen` |
| **P4-2** | `AlkalineParameters.h/.c` (§4.1–§4.2); menu text and tag (§4.3); host registry and tag check (§4.4); init defaults (§4.5); `init.alk` (§4.6); Load label (§4.7) | `special_tags` |
| **P4-3** | InstrumentManager (§5) | compiles with P4-4 |
| **P4-4** | Makefile (§6.1); `storageTypes.h/.c` (§6.2, §6.3); verified-unchanged sites (§6.4) | `make all`; `link_budget.py` (§6.5) |

**Do P4-1 to P4-4 in one sitting.** The firmware does not link while the
registry, the dispatch sites and the Makefile are half done.

---

## 2. P4-0: r6, baselines and tooling

### 2.1 r6: the 6-state network

**`tools/alkaline/gen_alk_tables.py` l. 45 — MODIFY**

```python
# was: NET_STATES = 8                      # block-system states (S093 P3 experiment: --states)
NET_STATES = 6                      # block-system states: r6, the common balanced basis
                                    # (user, S093). reduced_basis() builds one basis for every
                                    # Res point from the trace-normalised Gramians summed over
                                    # 33 points, so the 9 table points stay in one basis and
                                    # the per-block Res blend in dl_netChunk() stays exact.
                                    # 8 restores the physical basis (res_point()'s full path).
```

**`Core/DSPAudio/DiodeLadder.h` l. 74–76 — MODIFY**

```c
/*
 * Block-system states of the slow network (S093 P3/P4: r6).
 *
 * What:       the size of DiodeLadderNet's phi/gam/cs and of the state z[].
 *             6 = the reduced network on one balanced basis common to all Res
 *             points (gen_alk_tables.py reduced_basis()); 8 = the physical
 *             capacitor basis.
 * Why:        r6 costs about 3.3 instructions per sample less (P3 §9.2 S5),
 *             its [SN] p7 ring is closer to the golden model (2.018 ms /
 *             0.363) and it is inaudible against 8 states (-43..-49 dB).
 *             Measured on the M7: DiodeLadderNet 288 B (432 at 8 states),
 *             DiodeLadder 232 B (248), AlkalineVoice 540 B (556).
 * Inputs:     none (compile time). The generated AlkalineTables.h #errors
 *             when its tables were made for another count.
 * Outputs:    the layouts of DiodeLadderNet and DiodeLadder.
 * Affiliates: DL_NET_ROWS (DiodeLadder.c: 6 / 2 phases = 3 rows per block);
 *             tools/alkaline/gen_alk_tables.py NET_STATES.
 */
#ifndef ALK_NET_STATES
#define ALK_NET_STATES 6u
#endif
```

**Same file, the stale state-count wording — MODIFY:**
- l. 61–62 ("8 states, physical capacitor basis") → "ALK_NET_STATES states
  (6, r6: the common balanced basis)".
- l. 72 ("108 floats (432 B) per point") → "72 floats (288 B) per point at
  r6".
- l. 104 ("about 270 B … (8 states)") → "232 B at r6 (measured,
  arm-none-eabi-gcc 14.2)".

**Regenerate and prove:**

```
python3 tools/alkaline/gen_alk_tables.py            # AlkalineTables.h/.c (r6), square unchanged
python3 tools/alkaline/gen_alk_tables.py --check
make -C tools/dsp_test alkaline alkaline_tables     # AL-R on B2R: 2.018 ms / 0.363
python3 tools/alkaline/check_c.py                   # regenerates alk_c_*_B2R.wav (the device reference)
```

C0 is no longer an exact comparison. It compares with the 8-state S093 P1
variants: expect about −45 dB and LSD60 ≈ 0.01 dB from them. The gate is the
distance to golden, which must match P3 §9.3 within ±0.01 dB.

### 2.2 Gate baselines and the restored ELF targets

**`tools/dsp_test/Makefile` — ADD** after the `alkaline_tables` target. These
replace the targets removed in `f3805478`, which pointed at frozen S085/S086
images that no longer exist. `FW_ELF` is not defined in this Makefile today
(checked), so it is added here.

```make
# ---------------------------------------------------------------------------
# S093 P4: linked-image loop gates for the Alkaline integration.
#
# What:       elf_p4_snapshot writes the baseline loop dumps of a known-good
#             image (the P3 image, before any Alkaline code is linked):
#               - every oscillator, filter, transient and fold symbol
#                 (P4_OSC_SYMS), with elf_loopdump.py;
#               - mixer_calcNextSampleBlock(), whose voice renders are inlined.
#             elf_p4_check dumps the current image the same way and compares:
#               - elf_loopdump.py --compare: 0 MISSING per-sample loops
#                 (RENAMED passes; dispatch fragments may move);
#               - elf_hotloops.py --compare on the mixer: 0 GREW. LTO re-lays
#                 the mixer out when the set of inlined engines changes
#                 (P3 §2.3), so a GREW is checked by source pairing on the -g
#                 twins (elf_loopsrc.py, S093_P5 §3.2) before it is accepted or
#                 rejected.
# Why:        MEMORY.md (S089 ceiling): adding Alkaline must not lengthen any
#             other engine's per-sample loop. The S086 targets were removed in
#             f3805478; this is their replacement against a P4 baseline.
# Inputs:     FW_ELF (default: the firmware build output); P4BASE (the
#             baseline folder).
# Outputs:    $(P4BASE)/osc.txt, $(P4BASE)/mixer.txt; build/p4_*_now.txt;
#             exit 1 on a MISSING or GREW.
# Accessors:  make -C tools/dsp_test elf_p4_snapshot | elf_p4_check.
# Affiliates: elf_loopdump.py, elf_hotloops.py, elf_loopsrc.py; S093_P3 §2.3.
FW_ELF ?= $(ROOT)/build/lxr02.elf
P4BASE ?= $(ROOT)/build/s093p4
P4_OSC_SYMS := SVF_calcBlockZDF calcFmBlock calcNextOscSampleBlock \
  calcNextOscSampleRingBlock calcNextOscSampleSyncBlock calcNoiseRingBlock \
  calcSampleOscFmBlock calcSampleOscRingBlock calcSampleOscSyncBlock \
  calcSineRingBlock calcSineSyncBlock calcWavetableOscRingBlock \
  calcWavetableOscSyncBlock oscSection_renderOldCarrier osc_buildLoopSyncList \
  osc_buildSyncList osc_renderTypedFmRamp osc_setFreq osc_slpFmBlock \
  osc_slpFmRampBlock osc_slpPlainBlock osc_slpRingBlock osc_slpSyncBlock \
  osc_smpFmBlock osc_smpFmRampBlock osc_smpPlainBlock osc_smpRingBlock \
  osc_smpSyncBlock osc_wtbBlendBlock osc_wtbFmBlendBlock osc_wtbFmBlendRampBlock \
  osc_wtbFmBlock osc_wtbFmRampBlock osc_wtbPlainBlock osc_wtbPlanBlock \
  osc_wtbRingBlendBlock osc_wtbRingBlock osc_wtbSyncBlendBlock osc_wtbSyncBlock \
  transient_calcUserBlock voiceFold_asnLoop voiceFold_chbLoop voiceFold_fd2Loop \
  voiceFold_process voiceFold_sinLoop voiceFold_triLoop busComp_processBlock

elf_p4_snapshot:
	mkdir -p $(P4BASE)
	python3 elf_loopdump.py --elf $(FW_ELF) $(foreach s,$(P4_OSC_SYMS),--sym $(s)) \
	  --out $(P4BASE)/osc.txt
	python3 elf_loopdump.py --elf $(FW_ELF) --sym mixer_calcNextSampleBlock \
	  --out $(P4BASE)/mixer.txt

elf_p4_check:
	mkdir -p build
	python3 elf_loopdump.py --elf $(FW_ELF) $(foreach s,$(P4_OSC_SYMS),--sym $(s)) \
	  --out build/p4_osc_now.txt
	python3 elf_loopdump.py --compare $(P4BASE)/osc.txt build/p4_osc_now.txt
	python3 elf_loopdump.py --elf $(FW_ELF) --sym mixer_calcNextSampleBlock \
	  --out build/p4_mixer_now.txt
	python3 elf_hotloops.py --compare $(P4BASE)/mixer.txt build/p4_mixer_now.txt
```

Add `elf_p4_snapshot elf_p4_check seq_steplen` to the `.PHONY` list
(l. 56).

**Baselines, before any P4-1 code:**

```
make all                                             # the P3 image (Alkaline not linked)
make -C tools/dsp_test elf_p4_snapshot               # tools/dsp_test/baselines/s093p4/{osc,mixer}.txt (S093 P5: was build/s093p4)
cp build/lxr02.elf build/s093p4/base.elf
make BUILD=build/s093p4/dbg MCU="-mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -g" \
     build/s093p4/dbg/lxr02.elf                      # the -g twin (P5 §3.2)
```

`MCU` is the Makefile's own variable (l. 11). The P3 twin
`build/s093p3/dbg/lxr02.elf` was built the same way.

Re-measure the Drum ceiling on `base.elf` (P5 §3.3) and record it; it is
the G5 reference.

### 2.3 The counter's default rows

**`tools/alkaline/control_cost.py` l. 206 (`main()`, `--rows`) — MODIFY**

```python
    ap.add_argument("--rows", type=int, default=3,
                    help="network rows solved per block (DL_NET_ROWS = ALK_NET_STATES / "
                         "phases: r6 at 64 samples = 6 / 2 = 3)")
```

The `ITERS` default at l. 49 (`("dl_slowUpdate", 1): 4`) is overwritten by
`--rows` at l. 208–209, so no change is needed there.

---

## 3. P4-1: engine completion

### 3.1 The trigger and its platform part

**What Drum does** (`DrumVoice.c` l. 227–255, read):
- `lfo_retrigger(source_slot)`;
- `modNode_updateValue(&velocityModulators[source_slot], vol/127.f)`;
- `instrumentManager_applyVelocityModulationTarget(source_slot, vol/127.f)`.

The Alkaline trigger makes the same three calls.

**`Core/DSP/Instruments/Alkaline/AlkalineVoice.h` l. 100–102 — MODIFY**
(the prototype and its contract)

```c
/*
 * Trigger the voice (S092 P0 §5.3/§5.4; the platform part S093 P4).
 *
 * What:       the LFO retrigger and the two velocity modulation paths for
 *             source_slot, as Drum_triggerVoice() (DrumVoice.c l. 242-255);
 *             then the 303 rules: accent at velocity >= ALK_ACCENT_VELOCITY
 *             (101); legato when the previous note was slide-flagged and its
 *             gate is still open (no MEG strike, no VEG trigger, the pitch
 *             glides); the slide flag latched for the next trigger; the gate
 *             7/12 of the step, or the step plus 7/12 after a slide-flagged
 *             note; the ACCENT lane one step long.
 * Why:        the LFO and the velocity nodes belong to the slot the voice
 *             occupies, not to the voice instance (InstrumentManager's tagged
 *             runtime), so the caller names the slot.
 * Inputs:     voice (the slot's runtime member, non-NULL: the caller's slot
 *             is always < INSTRUMENT_SLOT_COUNT); source_slot 0..5; velocity
 *             0..127; note (the step/MIDI note, 63 = the default); stepSamples
 *             (seq_trackStepSamples() of the triggering track, >= 1).
 * Outputs:    the voice's sequencing state; the slot's LFO phase and velocity
 *             nodes.
 * Accessors:  instrumentManager_triggerTrack() (the single trigger funnel:
 *             sequencer, MIDI, live and preview, InstrumentManager.c l. 2129).
 * Affiliates: Drum_triggerVoice(); lfo_retrigger() (lfo.h l. 141),
 *             modNode_updateValue() and velocityModulators[]
 *             (modulationNode.h), instrumentManager_applyVelocityModulation-
 *             Target() (InstrumentManager.h l. 543).
 */
void Alkaline_triggerVoice(AlkalineVoice *voice, uint8_t source_slot, uint8_t velocity,
                           uint8_t note, uint32_t stepSamples);
```

**`AlkalineVoice.c` l. 6–10 — MODIFY** (includes; `lfo.h` comes through
`AlkalineVoice.h` l. 33 and itself includes `modulationNode.h`, but the
direct include documents the dependency)

```c
#include <math.h>
#include "AlkalineVoice.h"
#include "AlkalineTables.h"
#include "wavetable.h"
#include "MidiPitch.h"
#include "modulationNode.h"     /* S093 P4: velocityModulators[], modNode_updateValue() */
#include "InstrumentManager.h"  /* S093 P4: instrumentManager_applyVelocityModulationTarget() */
```

**`AlkalineVoice.c` l. 63–87 — MODIFY** (the whole function)

**Keep one line verbatim:**
`const uint8_t legato = (uint8_t)(voice->slideLatch && voice->gateLeft > 0);`.
The bench's legato break build (`tools/dsp_test/Makefile` l. 300) seds
exactly that text, and AL-S must still catch it.

```c
/*
 * S092 P0 §5.3 / §5.4: accent from velocity; legato when the previous note
 * was slide-flagged and its gate is still held; the slide flag is latched
 * for the next trigger; the gate is 7/12 of a step, or the whole step plus
 * the next step's gate after a slide-flagged note. S093 P4: the platform
 * part first, as Drum_triggerVoice(). Contract: AlkalineVoice.h.
 */
void Alkaline_triggerVoice(AlkalineVoice *voice, const uint8_t source_slot,
                           const uint8_t velocity, const uint8_t note,
                           const uint32_t stepSamples)
{
	/* the velocity as the modulation system's 0..1 value (Drum: vol/127.f) */
	const float vel = (float)velocity * (1.0f / 127.0f);
	/* the normal gate: 7/12 of the step (3.5 of 6 clocks, D11), rounded to
	 * the nearest sample; counted down by 32 per block, so it stays open for
	 * every block that starts before its end (S093 P0 §5.3) */
	const int32_t gate = (int32_t)((stepSamples * ALK_GATE_NUM + ALK_GATE_DEN / 2u)
	                               / ALK_GATE_DEN);
	/* legato: the previous note asked to slide (latched at its trigger) and
	 * its gate is still open; a slide into a rest closes the gate first */
	const uint8_t legato = (uint8_t)(voice->slideLatch && voice->gateLeft > 0);

	/* --- platform part (S093 P4): LFO and velocity, per slot --- */
	/* restart the slot's LFO phase when its rtg mode asks for it */
	lfo_retrigger(source_slot);
	/* the velocity node drives direct descriptor targets (velo_mod_dest) */
	modNode_updateValue(&velocityModulators[source_slot], vel);
	/* targets that are not direct nodes (slot decimation, Scene targets):
	 * InstrumentManager applies or ignores them; it stays beside the node
	 * update so no trigger client needs to know which kind is installed */
	instrumentManager_applyVelocityModulationTarget(source_slot, vel);

	/* --- the 303 rules (unchanged from S093 P0) --- */
	voice->note = note;
	/* D1: velocity 101+ is an accent; the default step velocity 100 is not */
	voice->accentOn = (uint8_t)(velocity >= ALK_ACCENT_VELOCITY);
	/* the ACCENT lane is high for one step (S092 P4 E3) */
	voice->accentLeft = (int32_t)stepSamples;
	voice->gliding = legato;
	if (!legato) {
		/* a new note: jump the pitch, strike the MEG, trigger the VEG */
		voice->pitchJump = 1u;
		alkMeg_trigger(&voice->meg);
		alkVeg_trigger(&voice->veg);
	}
	/* this note's slide flag decides the next trigger (A10) */
	voice->slideLatch = (uint8_t)(voice->slide != 0u);
	/* a slide-flagged note holds its gate through the next step's gate */
	voice->gateLeft = voice->slideLatch ? (int32_t)stepSamples + gate : gate;
}
```

There is no NULL check, as in the current function: the only caller passes
`instrumentManager_alkalineRuntime(slot)` with `slot` ≤ 5
(`instrumentManager_triggerTrack()` clamps it at l. 2119–2122).

### 3.2 `seq_trackStepSamples()`

**`Core/Sequencer/sequencer.h` l. 291 — ADD after `uint16_t seq_getBpm(void);`**

```c
/*
 * One step of a track in samples (S093 P4, the Alkaline gate).
 *
 * What:       stepScale_ticksQ8(seq_effectiveTrackScale[track]) (96 PPQ,
 *             Q8.8: a 1/16 is 24 ticks = 6144) at the current tempo, in
 *             samples at 44,108 Hz (AudioCodecManager.c: PLLI2S N=271, R=2,
 *             /6), rounded to the nearest:
 *               samples = ticksQ8 x 44108 x 60 / (256 x 96 x bpm).
 *             A track >= NUM_TRACKS uses STEP_SCALE_DEFAULT (76, the 1/16).
 *             BPM 0 is treated as 1.
 * Why:        the 303 gate is 7/12 of a step (D11) and the ACCENT lane one
 *             step; the sequencer has no gate, so the engine is told the
 *             step length at each trigger (S092 P0 §5.4).
 * Inputs:     track 0..6 (the triggering track; 6 = track 7 on slot 5).
 *             Reads seq_effectiveTrackScale[] (the Morph-resolved scale,
 *             S078) and seq_tempo (written only by seq_setBpm(): menu, clock
 *             sync, trigger jacks).
 * Outputs:    samples per step, 1..5,292,960 (CC 127 = 8x at 1 BPM).
 *             120 BPM 1/16 = 5514.
 * Accessors:  instrumentManager_triggerTrack() (main loop).
 * Affiliates: StepScale.h; seq_getBpm(); Alkaline_triggerVoice(); host law
 *             AL-T (tools/dsp_test seq_steplen). Read-only: never writes the
 *             Pattern region (MEMORY.md S078).
 */
uint32_t seq_trackStepSamples(uint8_t track);
```

**`Core/Sequencer/sequencer.c` l. 1872 — ADD after `seq_getBpm()`
(l. 1868–1871)**

`StepScale.h` is already included (l. 68), and `seq_effectiveTrackScale`
(l. 764) and `seq_tempo` (l. 767) are file statics in scope.

```c
//------------------------------------------------------------------------------
/*
 * One step of a track in samples (S093 P4). Contract: sequencer.h.
 *
 * Concurrency: seq_effectiveTrackScale[] is written by the foreground
 * refresh (l. 1782) and read by TIM3; seq_tempo by seq_setBpm(). Both are
 * single aligned byte/halfword reads here, so no lock is needed: a value one
 * refresh old shifts one gate by that refresh.
 */
uint32_t seq_trackStepSamples(uint8_t track)
{
	/* the track's Morph-resolved step scale (CC position); the default 1/16
	 * for an out-of-range track index */
	const uint8_t scale = (track < NUM_TRACKS) ? seq_effectiveTrackScale[track]
	                                           : (uint8_t)STEP_SCALE_DEFAULT;
	/* ticks per step in Q8.8 at 96 PPQ: 1536 (CC 0) .. 49152 (CC 127) */
	const uint64_t ticksQ8 = (uint64_t)stepScale_ticksQ8(scale);
	/* the tempo, never 0 (seq_setBpm() clamps too; this guards boot) */
	const uint64_t bpm = (seq_tempo != 0u) ? (uint64_t)seq_tempo : 1u;
	/* samples = ticksQ8 x fs x 60 / (256 x 96 x bpm):
	 *   numerator ticksQ8 x 44108 x 60 = ticksQ8 x 2,646,480, in 64 bits: a
	 *   1/16 alone is 6144 x 2,646,480 = 1.6e10, past 2^32 (AL-T's break
	 *   build makes it 32-bit and fails 21 checks);
	 *   denominator 256 x 96 x bpm = 24,576 x bpm;
	 *   + den/2 rounds to the nearest sample.
	 * The M7 calls __aeabi_uldivmod once per trigger (main loop, not the
	 * audio path) */
	const uint64_t den = 24576u * bpm;
	const uint64_t samples = (ticksQ8 * 2646480u + den / 2u) / den;

	/* at most 5,292,960 (fits 32 bits); >= 1 so a gate always opens */
	return (samples > 0u) ? (uint32_t)samples : 1u;
}
```

### 3.3 The Alkaline bench: stand-ins, trigger calls, AL-P

**`tools/dsp_test/modules/alkaline.c` l. 75–78 — MODIFY.** `AlkalineVoice.host.c`
is extracted with `--body`, which drops the quoted includes (`extract.py`
l. 6), so these declarations must come before it, as the `Lfo` stand-in
does today.

```c
/* lfo.h / modulationNode.h / InstrumentManager.h: same-shape stand-ins for
 * what the engine references (S093 P4). The engine owns an Lfo and calls
 * lfo_init(); the trigger makes the three platform calls of
 * Drum_triggerVoice(). The bench counts the calls so law AL-P can prove the
 * trigger makes them for its slot. */
typedef struct { uint8_t bytes[124]; } Lfo;
typedef struct { float value; } ModulationNode;
ModulationNode velocityModulators[6];
static unsigned host_lfoRetriggers, host_veloTargets;   /* AL-P call counters */
static uint8_t host_lastSlot;                           /* AL-P: the slot the calls named */
void lfo_init(Lfo *lfo) { memset(lfo, 0, sizeof(*lfo)); }
void lfo_retrigger(uint8_t voiceNr) { host_lfoRetriggers++; host_lastSlot = voiceNr; }
void modNode_updateValue(ModulationNode *vm, float val) { vm->value = val; }
void instrumentManager_applyVelocityModulationTarget(uint8_t source_slot, float velocity_0_1)
{ (void)velocity_0_1; host_veloTargets++; host_lastSlot = source_slot; }
```

**Same file, all 15 calls — MODIFY: insert `0u, ` as the second argument.**
The lines are 161, 230, 232, 239, 246, 266, 271, 274, 316, 357, 386, 503,
505, 609 and 651. For example,
`Alkaline_triggerVoice(&v, 100u, 63u, ALK_STEP)` becomes
`Alkaline_triggerVoice(&v, 0u, 100u, 63u, ALK_STEP)`. The mechanical
replacement `Alkaline_triggerVoice(&X, ` → `Alkaline_triggerVoice(&X, 0u, `
hits exactly these 15.

**Same file, `test_sequencing()` l. 275 — ADD** after the last check
(`if (v.gliding) fail(G_S, "after the gate closed: not legato", …);`), on a
fresh voice so that no earlier state is disturbed:

```c
	/* AL-P (S093 P4): the trigger makes the platform calls of
	 * Drum_triggerVoice() for its own slot, with the velocity as 0..1 */
	memset(&v, 0, sizeof(v));
	Alkaline_initVoice(&v);
	host_lfoRetriggers = host_veloTargets = 0u;
	host_lastSlot = 0u;
	Alkaline_triggerVoice(&v, 3u, 127u, 63u, ALK_STEP);
	if (host_lfoRetriggers != 1u || host_veloTargets != 1u || host_lastSlot != 3u ||
	    velocityModulators[3].value != 1.0f)
		fail(G_S, "AL-P platform calls per trigger", host_lfoRetriggers, host_veloTargets);
```

`fail()` is `fail(int g, const char *what, double a, double b)` (l. 111).
AL-P reports under AL-S, the sequencing group.

**Verified:** on scratch copies, the bench compiled with HOSTFLAGS and
reported 0 failures in AL-B/R/S/N/AB/D/Q/F/O/X.

**`tools/alkaline/control_cost.py` `STUBS` (l. 65–92) — MODIFY.** The M7
stub link compiles the real headers: `AlkalineVoice.h` → `lfo.h` →
`modulationNode.h` gives the complete `ModulationNode`.

After `#include "AlkalineVoice.h"` add:

```c
#include "modulationNode.h"
```

After the `lfo_init()` stub add:

```c
/* S093 P4: the trigger's platform calls (the trigger is not on the block
 * path, so the stubs only satisfy the link) */
ModulationNode velocityModulators[6];
void lfo_retrigger(uint8_t voiceNr) { (void)voiceNr; }
void modNode_updateValue(ModulationNode *vm, float val) { vm->lastVal = val; }
void instrumentManager_applyVelocityModulationTarget(uint8_t source_slot, float velocity_0_1)
{ (void)source_slot; (void)velocity_0_1; }
```

and in `main()`:

```c
		Alkaline_triggerVoice(&v, 0u, 101u, 63u, 5514u);
```

**Verified:** a scratch copy linked for the M7 with the real headers and
counted 1,393 per block on the current 8-state tables, unchanged from P3.

### 3.4 Law AL-T: bench target `seq_steplen` (new)

The existing `sequencer` bench (`modules/sequencer.c`) is a pending-queue
test with its own stubs and `expect()`. AL-T is independent, so it gets its
own target and module, which compile the extracted function against the
real `StepScale.c`.

**`tools/dsp_test/Makefile` — ADD** after the `sequencer` target (l. 128–141):

```make
# S093 P4: law AL-T. seq_trackStepSamples() against the real step-scale table
# and a double-precision reference. Break: the tick product in 32 bits (it
# overflows at every tempo) must fail AL-T.
seq_steplen: | $(GEN)
	$(EX) --src $(ROOT)/Core/Sequencer/sequencer.c --func seq_trackStepSamples \
	  --out $(GEN)/seq_stepsamples.c
	$(HOSTCC) $(HOSTFLAGS) -I$(ROOT)/Core/Sequencer modules/seq_steplen.c \
	  $(ROOT)/Core/Sequencer/StepScale.c -o build/seq_steplen -lm
	./build/seq_steplen
	mkdir -p build/break_steplen
	sed 's/const uint64_t ticksQ8 = (uint64_t)stepScale_ticksQ8(scale);/const uint32_t ticksQ8 = stepScale_ticksQ8(scale);/' \
	  $(GEN)/seq_stepsamples.c > build/break_steplen/seq_stepsamples.c
	@grep -q 'const uint32_t ticksQ8' build/break_steplen/seq_stepsamples.c || \
	  { echo "seq_steplen_break: sed did not apply"; exit 1; }
	$(HOSTCC) -Ibuild/break_steplen $(HOSTFLAGS) -I$(ROOT)/Core/Sequencer \
	  modules/seq_steplen.c $(ROOT)/Core/Sequencer/StepScale.c -o build/seq_steplen_break -lm
	@if ./build/seq_steplen_break >/dev/null 2>&1; then \
	  echo "seq_steplen_break: FAILED (the 32-bit product was not caught)"; \
	  exit 1; else echo "seq_steplen_break: caught (AL-T fails as expected)"; fi
```

Also add `seq_steplen` to `all:` (l. 60–62), after `sequencer`.

**`tools/dsp_test/modules/seq_steplen.c` — ADD (new file)**

```c
/*
 * seq_steplen.c - host law AL-T: seq_trackStepSamples() (S093 P4).
 *
 * What:       compiles the extracted firmware function (build/gen/
 *             seq_stepsamples.c) against the real StepScale.c and checks:
 *               - 40/120/300 BPM x the 1/32 (CC 38), 1/16 (CC 76, default)
 *                 and 1/8 (CC 93) stops: an exact table, and equality with a
 *                 double-precision round(ticksQ8 x 44108 x 60 / (256 x 96 x
 *                 bpm));
 *               - a track index >= NUM_TRACKS uses the default scale;
 *               - the longest step (CC 127 at 1 BPM) = 5,292,960;
 *               - BPM 0 is treated as 1.
 * Why:        the 64-bit tick product is the point: in 32 bits it overflows
 *             at every tempo (6144 x 2,646,480 > 2^32); the Makefile break
 *             build proves this law sees it (21 failures).
 * Inputs:     none.  Outputs: a line per failure; exit 1 on any.
 * Accessors:  make -C tools/dsp_test seq_steplen.
 * Affiliates: Core/Sequencer/sequencer.c seq_trackStepSamples(),
 *             StepScale.c (stepScale_q8Table, the musical stops).
 */
#include <stdint.h>
#include <stdio.h>
#include <math.h>
#include "StepScale.h"

/* the two sequencer statics the extracted function reads, owned here */
#define NUM_TRACKS 7u
static uint8_t seq_effectiveTrackScale[NUM_TRACKS];
static uint16_t seq_tempo = 120u;
#include "seq_stepsamples.c"

static int failures;

static void expect(int cond, const char *msg, unsigned a, unsigned b)
{
	if (!cond) {
		printf("FAIL: %s (%u, %u)\n", msg, a, b);
		failures++;
	}
}

int main(void)
{
	static const uint16_t bpms[3] = { 40u, 120u, 300u };
	/* the 1/32, 1/16 and 1/8 musical stops (StepScale.c stepScale_stops[]) */
	static const uint8_t scales[3] = { 38u, STEP_SCALE_DEFAULT, 93u };
	/* samples per step, [bpm][scale]; 120 BPM 1/16 = 5514 = the alkaline
	 * bench's ALK_STEP */
	static const uint32_t want[3][3] = {
		{ 8270u, 16541u, 33081u },
		{ 2757u,  5514u, 11027u },
		{ 1103u,  2205u,  4411u },
	};
	unsigned b, s;

	/* the table, and the double-precision reference at every point */
	for (b = 0u; b < 3u; b++)
		for (s = 0u; s < 3u; s++) {
			const double ref = floor((double)stepScale_ticksQ8(scales[s]) * 44108.0 * 60.0
			                         / (256.0 * 96.0 * bpms[b]) + 0.5);
			uint32_t got;

			seq_tempo = bpms[b];
			seq_effectiveTrackScale[2] = scales[s];
			got = seq_trackStepSamples(2u);
			expect(got == want[b][s], "AL-T table", got, want[b][s]);
			expect((double)got == ref, "AL-T against double", got, (unsigned)ref);
		}
	/* an out-of-range track index falls back to the 1/16 */
	seq_tempo = 120u;
	expect(seq_trackStepSamples(NUM_TRACKS) == 5514u, "AL-T default scale beyond NUM_TRACKS",
	       seq_trackStepSamples(NUM_TRACKS), 5514u);
	/* the longest step fits 32 bits, and BPM 0 counts as 1 */
	seq_tempo = 1u;
	seq_effectiveTrackScale[0] = 127u;
	expect(seq_trackStepSamples(0u) == 5292960u, "AL-T longest step",
	       seq_trackStepSamples(0u), 5292960u);
	seq_tempo = 0u;
	expect(seq_trackStepSamples(0u) == 5292960u, "AL-T BPM 0 as 1",
	       seq_trackStepSamples(0u), 5292960u);
	printf("seq_steplen: %s (%d failures)\n", failures ? "FAILED" : "OK", failures);
	return failures ? 1 : 0;
}
```

**Verified:** in scratch with the bench HOSTFLAGS (`-include prelude.h`),
this prints `OK (0 failures)`. The break build gives 21 failures.

---

## 4. P4-2: parameters, menu, init file

### 4.1 `Core/DSP/Instruments/Alkaline/AlkalineParameters.h` — ADD (new file)

```c
/*
 * AlkalineParameters.h - the Alkaline (TB-303) descriptor table and menu
 * layout (S093 P4).
 *
 * What:       the registry-facing exports of AlkalineParameters.c: the
 *             descriptor rows, their count, the eight voice sub-pages, the
 *             display label and the type flags (DrumParameters.h shape).
 * Why:        InstrumentManager's instrument_registry[] imports these, as it
 *             imports the four other engines' tables.
 * Accessors:  InstrumentManager.c (registry row); nothing else.
 * Affiliates: AlkalineVoice.h (the bound members); check_special_tags.py
 *             (TABLES['ALK']); tools/alkaline/make_init_alk.py (the keys).
 */
#ifndef ALKALINE_PARAMETERS_H_
#define ALKALINE_PARAMETERS_H_

#include "InstrumentManager.h"

/* 26 rows: 10 Alkaline rows, vol/pan, 8 LFO rows, 2 velocity rows, 4 LFO
 * target rows (asserted against alkaline_param_index_t in the .c) */
#define ALKALINE_PARAM_DESCRIPTOR_COUNT 26u
/* eight sub-pages: the front-panel SELECT1..8 buttons (menu.c ≈ l. 4608) */
#define ALKALINE_MENU_PAGE_COUNT        8u
/* Basic (S093 D2‴): may take any slot without counting towards the
 * two-Advanced limit; _ADVANCED only if the device gate fails (P5 K1) */
#define ALKALINE_INSTRUMENT_TYPE_FLAGS  INSTRUMENT_FLAG_BASIC

extern const ParamDescriptor alkaline_param_descriptors[];
extern const uint8_t alkaline_param_descriptor_count;
extern const instrument_menu_page_t alkaline_menu_pages[];
extern const uint8_t alkaline_menu_page_count;
extern const char alkaline_instrument_display_label[];
extern const uint8_t alkaline_instrument_type_flags;

#endif /* ALKALINE_PARAMETERS_H_ */
```

### 4.2 `Core/DSP/Instruments/Alkaline/AlkalineParameters.c` — ADD (new file)

Its macros are copied from `DrumParameters.c` l. 10–98 with `AlkalineVoice`
offsets. One difference is new: `ROW_SELECT_ONOFF` has no special argument.
`check_special_tags.py` reads the last argument of every `*_SPECIAL` macro as
its tag (`actual()`, l. 72–76). A `ROW_SELECT_ONOFF_SPECIAL(…, IM_SPECIAL_NONE)`
would therefore count as tagged `{IM_SPECIAL_NONE}` against an expected
empty set, and the gate would fail.

```c
#include "AlkalineParameters.h"
#include "AlkalineVoice.h"
#include "menu.h"
#include "MenuText.h"
#include <stddef.h>

/*
 * Alkaline parameter source of truth (S093 P4).
 *
 * What:       what each generic Kit storage cell means when the slot holds an
 *             Alkaline: file key, menu text, dtype, Morph/modulation flags,
 *             modulation domain and the runtime binding into AlkalineVoice.
 *             The pattern is DrumParameters.c; every row is on one line
 *             (check_special_tags.py and make_init_alk.py parse them).
 * Why:        S092 P0 §5.2 / S093 P2 §6: the 303 controls (waveform, coarse,
 *             tuning, slide, cutoff, resonance, env mod, decay, accent), the
 *             drive extra (E2), and the platform rows every engine shares
 *             (vol, pan, the LFO pair, velocity), so Morph, LFO, automation,
 *             MIDI and storage work unchanged.
 * Not offered (S092 P0 §5.5): srt (no SRR, D4), run, chn, pos, typ (the
 *             oscillator stays a table wave, S093 P3 §9.5), fine pitch,
 *             velo_vol_on_off (the 303's velocity is the accent, D1).
 * Accessors:  InstrumentManager registry; Menu (pages); Preset/Morph/LFO/
 *             automation through the registry; storage (keys, [morph] for
 *             the MORPHABLE rows only, storageTypes.c l. 981-990).
 * Affiliates: AlkalineVoice.h; MenuText.h (alkWaveNames, ONOFF_TAG_ALK_WAVE);
 *             tools/alkaline/make_init_alk.py; SD_CARD/Instrument/Alkaline/
 *             init.alk.
 */

/* Load-menu label and type flags, imported by the registry. "Alkaline" is 8
 * cells; the Morph row shows "AlkalMrp" (§4.7). */
const char alkaline_instrument_display_label[] = "Alkaline";
const uint8_t alkaline_instrument_type_flags = ALKALINE_INSTRUMENT_TYPE_FLAGS;

/* A normal sound cell: in both images, Morph-interpolated, a modulation
 * destination and step-automatable; written under [params] and [morph]. */
#define FLAGS_IMAGE \
    (INSTRUMENT_PARAM_FLAG_MORPHABLE | INSTRUMENT_PARAM_FLAG_MODULATABLE | \
     INSTRUMENT_PARAM_FLAG_AUTOMATABLE)

/* Runtime binding into the slot's AlkalineVoice, with an optional
 * special-writer tag (S073 Step 2). */
#define BIND_SPECIAL(member_, type_, special_) \
    { INSTRUMENT_BIND_INSTANCE_OFFSET, (uint16_t)offsetof(AlkalineVoice, member_), type_, special_ }
#define BIND(member_, type_) BIND_SPECIAL(member_, type_, IM_SPECIAL_NONE)

/* Modulation domains in descriptor units (DrumParameters.c l. 51-56). */
#define MOD_NONE  { 0u, 0u, INSTRUMENT_MOD_DOMAIN_NONE }
#define MOD_0_127 { 0u, 127u, INSTRUMENT_MOD_DOMAIN_CONTINUOUS }
#define MOD_PM63  { 0u, 127u, INSTRUMENT_MOD_DOMAIN_CONTINUOUS }

/* Row shapes (DrumParameters.c l. 70-98). */
#define ROW(key_, cat_, long_, short_, dtype_, mod_, member_, type_) \
    { key_, short_, long_, cat_, dtype_, FLAGS_IMAGE, mod_, BIND(member_, type_) }
#define ROW_SPECIAL(key_, cat_, long_, short_, dtype_, mod_, member_, type_, special_) \
    { key_, short_, long_, cat_, dtype_, FLAGS_IMAGE, mod_, BIND_SPECIAL(member_, type_, special_) }
#define ROW_MENU(key_, cat_, long_, short_, menu_, mod_, member_, type_) \
    { key_, short_, long_, cat_, (uint8_t)(DTYPE_MENU | (menu_ << 4)), FLAGS_IMAGE, mod_, BIND(member_, type_) }
/*
 * A tagged DTYPE_ON_OFF selector without a special writer (S093 P4).
 *
 * What:       DTYPE_ON_OFF | (tag << 4) with AUTOMATABLE | SELECTOR: never
 *             morphed (so not under [morph]), not an LFO/velocity target; the
 *             tag picks the name table in menu_onoffTagNames(). Same as
 *             DrumParameters.c ROW_SELECT_ONOFF_SPECIAL with IM_SPECIAL_NONE.
 * Why:        a plain TYPE_UINT8 store needs no writer, and the host tag check
 *             treats every *_SPECIAL row as tagged (check_special_tags.py
 *             actual()), so a NONE tag must not be spelt out.
 */
#define ROW_SELECT_ONOFF(key_, cat_, long_, short_, tag_, member_, type_) \
    { key_, short_, long_, cat_, (uint8_t)(DTYPE_ON_OFF | ((tag_) << 4)), \
      (INSTRUMENT_PARAM_FLAG_AUTOMATABLE | INSTRUMENT_PARAM_FLAG_SELECTOR), \
      MOD_NONE, BIND(member_, type_) }
/* Cells with no AlkalineVoice member (target selectors), flags 0: [params]
 * only. */
#define ROW_NOBIND(key_, cat_, long_, short_, dtype_, bind_kind_) \
    { key_, short_, long_, cat_, dtype_, 0u, MOD_NONE, { bind_kind_, 0u, 0u, IM_SPECIAL_NONE } }
/* Image cells applied through a supplemental binding (velocity amount). */
#define ROW_NOBIND_IMAGE(key_, cat_, long_, short_, dtype_, mod_, bind_kind_) \
    { key_, short_, long_, cat_, dtype_, FLAGS_IMAGE, mod_, { bind_kind_, 0u, 0u, IM_SPECIAL_NONE } }

/*
 * Alkaline-local descriptor indices.
 *
 * What:       the order of alkaline_param_descriptors[]; Scene/Kit storage,
 *             automation IDs, LFO/velocity tokens and NRPN numbers (slot x 64
 *             + index) use these numbers, and the file writer emits keys in
 *             this order.
 * Why:        the menu layout names cells by enum, not by number; a new row
 *             is appended before ALK_PARAM_COUNT so every stored index stays.
 */
typedef enum {
    ALK_PARAM_WAVE = 0u,
    ALK_PARAM_OSC1_PITCH_COARSE,
    ALK_PARAM_TUNING,
    ALK_PARAM_SLIDE,
    ALK_PARAM_CUTOFF,
    ALK_PARAM_RESO,
    ALK_PARAM_ENVMOD,
    ALK_PARAM_DECAY,
    ALK_PARAM_ACCENT,
    ALK_PARAM_DRIVE,
    ALK_PARAM_INSTRUMENT_VOL,
    ALK_PARAM_INSTRUMENT_PAN,
    ALK_PARAM_LFO_RATE,
    ALK_PARAM_LFO_AMOUNT,
    ALK_PARAM_LFO_AMOUNT_2,
    ALK_PARAM_LFO_WAVE,
    ALK_PARAM_LFO_RETRIGGER_VOICE,
    ALK_PARAM_LFO_POLARITY,
    ALK_PARAM_LFO_SYNC,
    ALK_PARAM_LFO_OFFSET,
    ALK_PARAM_VELO_MOD_AMOUNT,
    ALK_PARAM_VELO_MOD_DEST,
    ALK_PARAM_LFO_TARGET_VOICE,
    ALK_PARAM_LFO_TARGET_PARAM,
    ALK_PARAM_LFO_TARGET_VOICE_2,
    ALK_PARAM_LFO_TARGET_PARAM_2,
    ALK_PARAM_COUNT
} alkaline_param_index_t;

_Static_assert(ALKALINE_PARAM_DESCRIPTOR_COUNT == ALK_PARAM_COUNT,
               "ALKALINE_PARAM_DESCRIPTOR_COUNT must match alkaline_param_index_t");
_Static_assert(ALK_PARAM_COUNT <= INSTRUMENT_PARAM_COUNT,
               "Alkaline rows must fit the 64-cell Kit image");

/*
 * Alkaline parameter descriptors.
 *
 * TYPE_FLT rows store byte / 127 into a 0..1 float (instrumentManager_
 * writeParameter(), InstrumentManager.c l. 2177); AlkalineVoice converts at
 * control rate. Categories use the Drum vocabulary (Oscilltr, Filter, Voice,
 * LFO, Velocity).
 */
const ParamDescriptor alkaline_param_descriptors[] = {
    /* wav: 0 Saw (the stock sawTable, which matches the 303 ramp within
     * 0.5 %), 1 Sqr (the D8 per-octave Q8 square, AlkalineSquareTable.c); a
     * selector, so Morph never blends the two tables; any other stored value
     * plays Saw (AlkalineVoice.c: square = (wave == ALK_WAVE_SQUARE)) */
    ROW_SELECT_ONOFF("alk_wave", "Oscilltr", "Waveform", "wav", ONOFF_TAG_ALK_WAVE, wave, TYPE_UINT8),
    /* coa: the existing coarse writer (midiFreq high byte, osc_recalcFreq(),
     * which only sets osc.freq; the Alkaline async recomputes the pitch from
     * midiFreq and the note every block); its LFO/velocity target is
     * osc.modNodeValue, which osc_setTableFreq() multiplies in (T1-7);
     * 36 = C2 at the default note 63 ([SN] p7) */
    ROW_SPECIAL("osc1_pitch_coarse", "Oscilltr", "Coarse", "coa", DTYPE_0B127, MOD_0_127, osc.modNodeValue, TYPE_SPECIAL_F, IM_SPECIAL_PITCH_COARSE | IM_SPECIAL_OSC1),
    /* tun: +-700 cents ([SN] p1, D6) by (tuning x 127 - 63) / 63 x 7/12 octave
     * at control rate; 63 = centre */
    ROW("alk_tuning", "Oscilltr", "Tuning", "tun", DTYPE_PM63, MOD_PM63, tuning, TYPE_FLT),
    /* sld: latched at each trigger, decides the next note (A10); an image
     * ON/OFF cell like Drum's velo_vol_on_off */
    ROW("alk_slide", "Oscilltr", "Slide", "sld", DTYPE_ON_OFF, MOD_NONE, slide, TYPE_UINT8),
    /* the five 303 knobs: VR3 cutoff, VR4 resonance (both gangs), VR5 env
     * mod, VR6 decay, VR7 accent, as tapers in AlkalineControl.c */
    ROW("alk_cutoff", "Filter", "Cutoff", "cut", DTYPE_0B127, MOD_0_127, cutoff, TYPE_FLT),
    ROW("alk_reso", "Filter", "Resnance", "res", DTYPE_0B127, MOD_0_127, reso, TYPE_FLT),
    ROW("alk_envmod", "Filter", "Env Mod", "env", DTYPE_0B127, MOD_0_127, envMod, TYPE_FLT),
    ROW("alk_decay", "Filter", "Decay", "dec", DTYPE_0B127, MOD_0_127, decay, TYPE_FLT),
    ROW("alk_accent", "Voice", "Accent", "acc", DTYPE_0B127, MOD_0_127, accent, TYPE_FLT),
    /* drv: the ladder input x 8^(v/127) (D16) with the output make-up
     * 2^(ALK_MAKEUP_OCT (1 - v/127)) (B-2); 0 = the 303's own drive */
    ROW("alk_drive", "Filter", "Overdriv", "drv", DTYPE_0B127, MOD_0_127, drive, TYPE_FLT),
    /* platform rows (DrumParameters.c l. 257-258): the mixer reads them
     * through instrumentManager_runtimeVolume()/_runtimePan() */
    ROW("instrument_vol", "Voice", "Volume", "vol", DTYPE_0B127, MOD_0_127, vol, TYPE_FLT),
    ROW("instrument_pan", "Voice", "Panning", "pan", DTYPE_PM63, MOD_PM63, pan, TYPE_UINT8),
    /* the LFO pair: one oscillator, two destinations (DrumParameters.c
     * l. 270-281) */
    ROW_SPECIAL("lfo_rate", "LFO", "Frequncy", "frq", DTYPE_0B127, MOD_0_127, lfo.modNodeValue, TYPE_SPECIAL_F, IM_SPECIAL_LFO_RATE),
    ROW("lfo_amount", "LFO", "Amount", "am1", DTYPE_0B127, MOD_0_127, lfo.modTarget.amount, TYPE_FLT),
    ROW("lfo_amount_2", "LFO", "Amount 2", "am2", DTYPE_0B127, MOD_0_127, lfo.modTarget2.amount, TYPE_FLT),
    ROW_MENU("lfo_wave", "LFO", "Waveform", "wav", MENU_LFO_WAVES, MOD_NONE, lfo.waveform, TYPE_UINT8),
    ROW_MENU("lfo_retrigger_voice", "LFO", "Retriggr", "rtg", MENU_RETRIGGER, MOD_NONE, lfo.retrigger, TYPE_UINT8),
    ROW("lfo_polarity", "LFO", "Polarity", "pol", DTYPE_LFO_POLARITY, MOD_NONE, lfo.polarity, TYPE_UINT8),
    ROW_MENU("lfo_sync", "LFO", "ClockSnc", "snc", MENU_SYNC_RATES, MOD_NONE, lfo.sync, TYPE_UINT8),
    ROW_SPECIAL("lfo_offset", "LFO", "Offset", "ofs", DTYPE_0B127, MOD_NONE, lfo.phaseOffset, TYPE_UINT32, IM_SPECIAL_LFO_OFFSET),
    /* velocity (DrumParameters.c l. 283-284) */
    ROW_NOBIND_IMAGE("velo_mod_amount", "Velocity", "Amount", "amt", DTYPE_0B127, MOD_NONE, INSTRUMENT_BIND_VELOCITY_AMOUNT),
    ROW_NOBIND("velo_mod_dest", "Velocity", "DstParam", "dst", DTYPE_TARGET_SELECTION_VELO, INSTRUMENT_BIND_VELOCITY_TARGET),
    /* the LFO destinations (DrumParameters.c l. 285-288) */
    ROW_NOBIND("lfo_target_voice", "LFO", "DstVoice", "vo1", DTYPE_VOICE_LFO, INSTRUMENT_BIND_LFO_TARGET_VOICE),
    ROW_NOBIND("lfo_target_param", "LFO", "DstParam", "ds1", DTYPE_TARGET_SELECTION_LFO, INSTRUMENT_BIND_LFO_TARGET_PARAM),
    ROW_NOBIND("lfo_target_voice_2", "LFO", "DstVoice2", "vo2", DTYPE_VOICE_LFO, INSTRUMENT_BIND_LFO_TARGET_VOICE_2),
    ROW_NOBIND("lfo_target_param_2", "LFO", "DstParam2", "ds2", DTYPE_TARGET_SELECTION_LFO, INSTRUMENT_BIND_LFO_TARGET_PARAM_2),
};

/* Runtime registry count used by InstrumentManager bounds checks. */
const uint8_t alkaline_param_descriptor_count =
    (uint8_t)(sizeof(alkaline_param_descriptors) / sizeof(alkaline_param_descriptors[0]));

_Static_assert(sizeof(alkaline_param_descriptors) / sizeof(alkaline_param_descriptors[0]) ==
                   ALK_PARAM_COUNT,
               "alkaline_param_descriptors must have one row per index");

/* An empty 16-cell page (pages 1, 3, 4: no amp envelope, FM or transient
 * controls; Snare ships an all-empty page 3 the same way). */
#define ALK_PAGE_EMPTY \
    {{ INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, \
       INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, \
       INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, \
       INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY }}

/*
 * Alkaline voice menu layout (F3: the user left the assignment open).
 *
 * What:       eight sub-pages at the index of the front-panel SELECT button
 *             that means the same thing for every engine (menu.c ≈ l. 4608:
 *             subPage = SELECTn - 1): 0 oscillator, 1 amp envelope, 2 pitch/
 *             velocity, 3 FM, 4 transient, 5 filter, 6 LFO, 7 mix. Each page
 *             is 16 cells shown as four-cell screens.
 * Why:        a 303 voice in the places a Drum player looks: the 303 pitch
 *             controls under OSC (coa tun wav sld), the filter under FILTER
 *             (cut res env dec, then drv on the second screen), accent with
 *             vol/pan under MIX.
 */
const instrument_menu_page_t alkaline_menu_pages[] = {
    /* 0 OSC: coa tun wav sld */
    {{ ALK_PARAM_OSC1_PITCH_COARSE, ALK_PARAM_TUNING, ALK_PARAM_WAVE, ALK_PARAM_SLIDE, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY }},
    /* 1 amp envelope: none (the VEG is fixed, D10) */
    ALK_PAGE_EMPTY,
    /* 2 velocity: dst amt (Cymbal's layout: cells 0-1) */
    {{ ALK_PARAM_VELO_MOD_DEST, ALK_PARAM_VELO_MOD_AMOUNT, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY }},
    /* 3 FM, 4 transient: none */
    ALK_PAGE_EMPTY,
    ALK_PAGE_EMPTY,
    /* 5 FILTER: cut res env dec | drv */
    {{ ALK_PARAM_CUTOFF, ALK_PARAM_RESO, ALK_PARAM_ENVMOD, ALK_PARAM_DECAY, ALK_PARAM_DRIVE, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY }},
    /* 6 LFO: the Drum layout (DrumParameters.c l. 411) */
    {{ ALK_PARAM_LFO_RATE, ALK_PARAM_LFO_SYNC, ALK_PARAM_LFO_WAVE, ALK_PARAM_LFO_OFFSET, ALK_PARAM_LFO_RETRIGGER_VOICE, ALK_PARAM_LFO_POLARITY, ALK_PARAM_LFO_AMOUNT, ALK_PARAM_LFO_AMOUNT_2, ALK_PARAM_LFO_TARGET_VOICE, ALK_PARAM_LFO_TARGET_PARAM, ALK_PARAM_LFO_TARGET_VOICE_2, ALK_PARAM_LFO_TARGET_PARAM_2, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY }},
    /* 7 MIX: acc vol pan */
    {{ ALK_PARAM_ACCENT, ALK_PARAM_INSTRUMENT_VOL, ALK_PARAM_INSTRUMENT_PAN, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY }},
};

const uint8_t alkaline_menu_page_count =
    (uint8_t)(sizeof(alkaline_menu_pages) / sizeof(alkaline_menu_pages[0]));

_Static_assert(ALKALINE_MENU_PAGE_COUNT ==
                   (sizeof(alkaline_menu_pages) / sizeof(alkaline_menu_pages[0])),
               "ALKALINE_MENU_PAGE_COUNT must match alkaline_menu_pages");

#undef FLAGS_IMAGE
#undef BIND_SPECIAL
#undef BIND
#undef ROW
#undef ROW_SPECIAL
#undef ROW_MENU
#undef ROW_SELECT_ONOFF
#undef ROW_NOBIND
#undef ROW_NOBIND_IMAGE
#undef ALK_PAGE_EMPTY
#undef MOD_PM63
#undef MOD_0_127
#undef MOD_NONE
```

**Firmware self-check:** `instrumentManager_specialTagSelfCheck()`
(l. 3955) compares every row's tag with `classifySpecialKey()`:
- `osc1_pitch_coarse` → `PITCH_COARSE | OSC1`;
- `lfo_rate` / `lfo_offset` → their tags;
- every `alk_*` key, `instrument_vol/pan` and the targets → `NONE`.

All match the table.

### 4.3 Menu text and the ON/OFF tag

**`Core/Menu/MenuText.h` l. 224 — ADD** after `voiceChainNames` (l. 222–224)

```c
/*
 * DTYPE_ON_OFF tag for the Alkaline waveform selector (S093 P4).
 *
 * What:       ONOFF_TAG_ALK_WAVE = 3u: the alk_wave row's dtype byte is
 *             DTYPE_ON_OFF | (3 << 4) = 0x36. Tags 1 (osc1_run) and 2
 *             (voice_chain) stay.
 * Why:        S088 D12 reserved the high nibble of DTYPE_ON_OFF for tagged
 *             selectors (every DTYPE_MENU id and dtype value is taken, S078);
 *             menu.c resolves the name table per tag in menu_onoffTagNames().
 * Accessors:  ROW_SELECT_ONOFF (AlkalineParameters.c, alk_wave).
 * Affiliates: alkWaveNames below; ALK_WAVE_SAW/ALK_WAVE_SQUARE
 *             (AlkalineVoice.h l. 56-57).
 */
#define ONOFF_TAG_ALK_WAVE      3u
/*
 * Alkaline waveform names (S093 P4).
 *
 * What:       [0] = count (2), then Saw, Sqr. Same shape as voiceChainNames:
 *             a leading count byte, then 3+1-character entries.
 * Why:        display, clamp and automation range of the wav cell all come
 *             from this count (the six tagged sites in menu.c), so 0..1 is
 *             the whole domain; the count must equal ALK_WAVE_SQUARE + 1
 *             (asserted in menu.c).
 * Accessors:  menu_onoffTagNames() (menu.c).
 */
static const char alkWaveNames[][4] = {
    {2}, {"Saw"},{"Sqr"},
};
```

**`Core/Menu/menu.c` l. 12321–12341 — MODIFY** (`menu_onoffTagNames()`, the
comment and the body)

```c
/*
 * Name table of a tagged DTYPE_ON_OFF cell (S088 D12; S089 P2; S093 P4).
 *
 * What:       returns the 3-character name table for a dtype high-nibble tag:
 *             ONOFF_TAG_VOICE_CHAIN -> voiceChainNames (T+D, WFd);
 *             ONOFF_TAG_ALK_WAVE -> alkWaveNames (Saw, Sqr); every other
 *             non-zero tag -> oscRunNames (rst, fre, itp), exactly as before.
 *             Entry [0][0] is the count; names start at [1].
 * Why:        one helper keeps display, clamp, automation max and the
 *             step-edit line in agreement for every tag. Its six callers in
 *             this file: l. 3387, 6570, 6864, 13869, 13915, 15099.
 * Inputs:     tag (dtype >> 4, non-zero).  Outputs: the table.
 * Accessors:  menu_formatCellValue3(), va_formatValue3(), menu_clampCellValue(),
 *             menu_automationValueMax(), menu_formatAutomationValue3(), the
 *             single-parameter step-edit display.
 * Affiliates: ONOFF_TAG_* / oscRunNames / voiceChainNames / alkWaveNames
 *             (MenuText.h).
 */
static const char (*menu_onoffTagNames(uint8_t tag))[4]
{
    switch (tag) {
    case ONOFF_TAG_VOICE_CHAIN: return voiceChainNames;
    case ONOFF_TAG_ALK_WAVE:    return alkWaveNames;
    default:                    return oscRunNames;
    }
}
```

**`menu.c` l. 12484 — ADD** after the `voiceChainNames` assert
(l. 12482–12484)

```c
/* S093 P4: the Alkaline waveform names list both stored values
 * (ALK_WAVE_SAW 0, ALK_WAVE_SQUARE 1, AlkalineVoice.h). */
_Static_assert((sizeof(alkWaveNames) / sizeof(alkWaveNames[0])) - 1u == 2u,
               "alkWaveNames must list Saw and Sqr");
```

**NONE: the six callers** (l. 3387, 6570, 6864, 13869, 13915, 15099). Each
calls the helper and takes its range from `names[0][0]`.

### 4.4 `tools/dsp_test/check_special_tags.py` — MODIFY (TABLES, plus a registry check, F2)

**l. 1–10 (docstring), `What:` — MODIFY** to read:

```python
What:       classifies each descriptor key with the pre-S073 string rules and
            compares the result with its ROW_SPECIAL/ROW_MENU_SPECIAL tag; and
            (S093 P4) checks the instrument registry against the invariants
            the storage code relies on: rows in enum order and contiguous
            from 0 (the .hcnames header walks the type values until
            storage_instrumentTypeToText() returns NULL), 3-character type
            tokens (HCNAMES rows and AutoSave copy exactly 3 bytes), and one
            TABLES entry per registered type.
```

**l. 15–20 — MODIFY**

```python
TABLES = {
    'DRM': 'Core/DSP/Instruments/Drum/DrumParameters.c',
    'SNR': 'Core/DSP/Instruments/Snare/SnareParameters.c',
    'CYM': 'Core/DSP/Instruments/Cymbal/CymbalParameters.c',
    'HAT': 'Core/DSP/Instruments/HiHat/HiHatParameters.c',
    # S093 P4: the coarse row classifies as PITCH_COARSE | OSC1 (the osc1_
    # branch is type-independent here and in the firmware classifier,
    # InstrumentManager.c l. 3876); every alk_* key and the platform rows
    # classify as NONE or as their Drum twins.
    'ALK': 'Core/DSP/Instruments/Alkaline/AlkalineParameters.c',
}
ENUM = 'Core/DSP/Instruments/InstrumentManager.h'
REGISTRY = 'Core/DSP/Instruments/InstrumentManager.c'
```

**`expected()` — NONE.** Its `osc2_`/`osc3_`/`noise_` branches name types,
and Alkaline has none of those keys.

**ADD before `def main():`**

```python
def registry_check():
    """S093 P4 (F2): the registry invariants a further type must keep.

    What:   reads instrument_type_t (InstrumentManager.h) and the registry
            rows ([INSTRUMENT_TYPE_X] = { INSTRUMENT_TYPE_X, "tok", ...) of
            InstrumentManager.c. Fails when the rows are not in enum order,
            a designator disagrees with its row's type, a token is not 3
            lowercase characters, the row count differs from the real types
            (UNKNOWN excluded), or a registered type has no TABLES entry.
    Returns the number of problems (each printed).
    """
    enum = re.search(r'typedef enum \{(.*?)\} instrument_type_t;',
                     open(ENUM).read(), re.S).group(1)
    types = [t for t in re.findall(r'INSTRUMENT_TYPE_(\w+)', enum) if t != 'UNKNOWN']
    rows = re.findall(r'\[INSTRUMENT_TYPE_(\w+)\]\s*=\s*\{\s*INSTRUMENT_TYPE_(\w+),\s*"([^"]*)"',
                      open(REGISTRY).read())
    bad = 0
    if [r[0] for r in rows] != types:
        print(f'registry: rows {[r[0] for r in rows]} differ from enum {types}')
        bad += 1
    for desig, typ, tok in rows:
        if desig != typ:
            print(f'registry: [{desig}] holds INSTRUMENT_TYPE_{typ}')
            bad += 1
        if not re.fullmatch(r'[a-z0-9]{3}', tok):
            print(f'registry: {typ} token "{tok}" is not 3 lowercase characters')
            bad += 1
        if typ not in TABLES:
            print(f'registry: {typ} has no TABLES entry in check_special_tags.py')
            bad += 1
    return bad
```

**`main()` — MODIFY**: start it with `bad = registry_check()` (the line
`bad = rows = 0` becomes `rows = 0` after it). The final `print` and return
are unchanged, so a registry problem fails `make special_tags`.

**`tools/dsp_test/Makefile` l. 244–245 (`special_tags`) — MODIFY** (the
init-file check rides the same gate):

```make
special_tags:
	cd $(ROOT) && python3 tools/dsp_test/check_special_tags.py
	cd $(ROOT) && python3 tools/alkaline/make_init_alk.py --check
```

### 4.5 Init defaults equal to `init.alk`

**`Core/DSP/Instruments/Alkaline/AlkalineVoice.c` l. 35–42 — MODIFY**

```c
	/* S093 P4: the bytes of SD_CARD/Instrument/Alkaline/init.alk, as TYPE_FLT
	 * stores them (byte / 127), so a voice before its first apply and a
	 * freshly loaded init.alk are the same sound: the five 303 knobs at 64
	 * (the nearest byte to half rotation), Tuning at its centre 63, drive 0
	 * (x1), vol 100 */
	voice->cutoff = 64.0f / 127.0f;
	voice->reso = 64.0f / 127.0f;
	voice->envMod = 64.0f / 127.0f;
	voice->decay = 64.0f / 127.0f;
	voice->accent = 64.0f / 127.0f;
	voice->tuning = 63.0f / 127.0f;
	voice->drive = 0.0f;
	voice->vol = 100.0f / 127.0f;
```

`pan` 63, `wave` Saw and `slide` 0 (l. 43–45) already match. So do the
oscillator's `midiFreq` (coarse 36, fine byte 63, l. 27), which `init.alk`
cannot reach for the fine byte (no fine row), and `lfo_init()`. The bench
laws set their own knob values, so none depends on these.

### 4.6 The first instrument: `SD_CARD/Instrument/Alkaline/init.alk` (F1)

**The format, from the writer, not assumed.** Per
`storage_formatInstrumentLineView()` (`storageTypes.c` l. 1040–1134), matched
against `SD_CARD/Instrument/Drum/rollind1.drm`:
- the lines `format=helicase.instrument`, `version=1`, `type=<tok>`, then an
  empty line and `[params]`;
- every descriptor with a file key, in table order, as `key=value`;
- an empty line, `[morph]`, then only the `INSTRUMENT_PARAM_FLAG_MORPHABLE`
  rows (`storage_descriptorWritableInSection()` l. 981–990). Here that
  leaves out `alk_wave` (a selector), `velo_mod_dest` and the four LFO
  targets;
- a final empty line;
- LF line endings;
- an LFO voice selector equal to the instrument's own one-based slot is
  written as `self` (l. 1100–1113). The parser maps it back to the loading
  slot (l. 2313–2318).

Target selectors `off` are `255` (`INSTRUMENT_TARGET_TOKEN_OFF` 0xff,
`InstrumentManager.h` l. 47).

**The file — ADD** (exact bytes, LF; generated by the script below):

```
format=helicase.instrument
version=1
type=alk

[params]
alk_wave=0
osc1_pitch_coarse=36
alk_tuning=63
alk_slide=0
alk_cutoff=64
alk_reso=64
alk_envmod=64
alk_decay=64
alk_accent=64
alk_drive=0
instrument_vol=100
instrument_pan=63
lfo_rate=0
lfo_amount=0
lfo_amount_2=0
lfo_wave=0
lfo_retrigger_voice=0
lfo_polarity=0
lfo_sync=0
lfo_offset=0
velo_mod_amount=0
velo_mod_dest=255
lfo_target_voice=self
lfo_target_param=255
lfo_target_voice_2=self
lfo_target_param_2=255

[morph]
osc1_pitch_coarse=36
alk_tuning=63
alk_slide=0
alk_cutoff=64
alk_reso=64
alk_envmod=64
alk_decay=64
alk_accent=64
alk_drive=0
instrument_vol=100
instrument_pan=63
lfo_rate=0
lfo_amount=0
lfo_amount_2=0
lfo_wave=0
lfo_retrigger_voice=0
lfo_polarity=0
lfo_sync=0
lfo_offset=0
velo_mod_amount=0

```

**How it reaches the device:**
- **Boot** (`filesystem_createBootIndexBlocking()` l. 29431) runs
  `filesystem_repairInstrumentNamesBlocking()` over every registry type
  (l. 30403). `init` is already an 8.3-safe stem, so no rename. Boot then
  scans and writes `/Instrument/Alkaline/.hcindex`.
- **A card without the folder:** boot skips it.
  - Repair: a missing parent finishes DONE (l. 10535–10538).
  - Index: a missing subdirectory finishes DONE outside runtime recovery
    (l. 4752–4766).
  - Instrument Save creates it (l. 17875–17895).

**`tools/alkaline/make_init_alk.py` — ADD (new file)**

```python
#!/usr/bin/env python3
"""Write and check the first Alkaline instrument, init.alk (S093 P4, F1).

What:       reads the descriptor rows of Core/DSP/Instruments/Alkaline/
            AlkalineParameters.c (one row per line, as check_special_tags.py)
            and writes SD_CARD/Instrument/Alkaline/init.alk in the firmware
            writer's exact form (storageTypes.c storage_formatInstrumentLineView):
              format=helicase.instrument / version=1 / type=alk / blank /
              [params] every key in table order / blank /
              [morph] the MORPHABLE rows only (macros ROW, ROW_SPECIAL,
              ROW_MENU, ROW_NOBIND_IMAGE carry FLAGS_IMAGE; ROW_SELECT_ONOFF
              and ROW_NOBIND do not) / blank; LF endings.
            Values (INIT): the five 303 knobs 64, Tuning 63, coarse 36 (C2),
            drive 0, Saw, slide off, vol 100, pan 63; LFO and velocity off:
            the LFO voices `self` (the writer's own-slot token), the target
            selectors 255 (INSTRUMENT_TARGET_TOKEN_OFF).
Why:        S093 P4 F1 (user): Instrument Load only lists files and a slot's
            type only changes by loading one, so the card image carries a
            first Alkaline. Every key is written: a missing key loads 0
            (instrumentManager_resetSlot()), which for coarse, tuning and vol
            would be 8 Hz, -700 cents and silence.
Inputs:     --check: the committed file must equal what this script writes,
            and every table key must have an INIT value (and no extra).
Outputs:    the file (default mode); with --check, a message and exit 1 on
            a difference.
Accessors:  python3 tools/alkaline/make_init_alk.py [--check];
            make -C tools/dsp_test special_tags (runs --check).
Affiliates: AlkalineVoice.c Alkaline_initVoice() (the same bytes);
            storage_instrumentParseLine() (the reader).
"""
import argparse
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
TABLE = os.path.join(ROOT, "Core", "DSP", "Instruments", "Alkaline", "AlkalineParameters.c")
OUT = os.path.join(ROOT, "SD_CARD", "Instrument", "Alkaline", "init.alk")
# one descriptor row: macro name, then the quoted file key
ROW = re.compile(r'^\s*(ROW[A-Z_]*)\("([^"]+)"')
# rows whose macro sets INSTRUMENT_PARAM_FLAG_MORPHABLE (FLAGS_IMAGE)
MORPHABLE = {"ROW", "ROW_SPECIAL", "ROW_MENU", "ROW_NOBIND_IMAGE"}

# init bytes per key; every key of the table must be here (and only those)
INIT = {
    "alk_wave": "0", "osc1_pitch_coarse": "36", "alk_tuning": "63", "alk_slide": "0",
    "alk_cutoff": "64", "alk_reso": "64", "alk_envmod": "64", "alk_decay": "64",
    "alk_accent": "64", "alk_drive": "0", "instrument_vol": "100", "instrument_pan": "63",
    "lfo_rate": "0", "lfo_amount": "0", "lfo_amount_2": "0", "lfo_wave": "0",
    "lfo_retrigger_voice": "0", "lfo_polarity": "0", "lfo_sync": "0", "lfo_offset": "0",
    "velo_mod_amount": "0", "velo_mod_dest": "255",
    "lfo_target_voice": "self", "lfo_target_param": "255",
    "lfo_target_voice_2": "self", "lfo_target_param_2": "255",
}


def rows():
    """(macro, key) for every descriptor row, in table order."""
    with open(TABLE) as fh:
        return [(m.group(1), m.group(2)) for m in map(ROW.match, fh) if m]


def text(r):
    """The file text, line for line as the firmware writer emits it."""
    lines = ["format=helicase.instrument", "version=1", "type=alk", "", "[params]"]
    lines += [f"{k}={INIT[k]}" for _, k in r]
    lines += ["", "[morph]"]
    lines += [f"{k}={INIT[k]}" for m, k in r if m in MORPHABLE]
    lines += [""]
    return "\n".join(lines) + "\n"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()
    r = rows()
    keys = [k for _, k in r]
    missing = [k for k in keys if k not in INIT]
    extra = [k for k in INIT if k not in keys]
    if missing or extra:
        sys.exit(f"make_init_alk: {len(r)} rows; no INIT value for {missing}; "
                 f"INIT keys not in the table {extra}")
    want = text(r)
    if a.check:
        have = open(OUT, newline="").read() if os.path.exists(OUT) else None
        if have != want:
            sys.exit(f"make_init_alk: {OUT} differs from the table (rerun without --check)")
        print(f"make_init_alk: init.alk matches the table ({len(r)} rows)")
        return
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", newline="\n") as fh:
        fh.write(want)
    print("wrote", OUT)


if __name__ == "__main__":
    main()
```

**`SD_CARD/Instrument/Alkaline/` — ADD (new directory)** holding `init.alk`
only. The firmware writes `.hcindex` and `.hctmp.alk` on the device, as in
the four other folders. Copying `SD_CARD/` to a card installs it.

### 4.7 The Load label keeps "Mrp" for any label length (F2)

**`Core/Menu/menu.c` l. 9666–9693 — MODIFY**
(`menu_instrumentLoadCopyTypeLabel()`, the comment and the Morph branch)

```c
static void menu_instrumentLoadCopyTypeLabel(char *dest)
{
    char label[9];
    const char *base_label = instrumentManager_typeDisplayLabel(
        menu_instrumentLoadType);

    /*
     * Build the fixed eight-character Load label used by the LCD.
     *
     * What:       normal rows show the instrument type label, padded to 8.
     *             The morph row shows at most 5 characters of the label
     *             followed by "Mrp": "DrumMrp ", "HiHatMrp", "CymbaMrp",
     *             "AlkalMrp".
     * Why:        S093 P4: with a fifth type (and more to come) the label
     *             length is not bounded by the four originals. Before, "Mrp"
     *             was appended only where it fitted: Cymbal showed "CymbalMr"
     *             and an 8-character label such as "Alkaline" would show no
     *             Mrp at all, identical to its normal row. Padding and
     *             truncation live here so repaint cannot resize or corrupt the
     *             LCD field.
     * Inputs:     menu_instrumentLoadType, menu_instrumentLoadMorphMode.
     * Outputs:    dest[0..7].
     * Affiliates: instrumentManager_typeDisplayLabel(); FILESYSTEM_SPEC.md
     *             l. 268 (`<Type>Mrp`).
     */
    menu_copyPaddedField(label, base_label, 8u);
    if (menu_instrumentLoadMorphMode) {
        uint8_t len = 0u;
        /* the label's own length (padding is spaces) */
        while (len < 8u && label[len] != ' ')
            len++;
        /* keep room for the three-character suffix */
        if (len > 5u)
            len = 5u;
        label[len++] = 'M';
        label[len++] = 'r';
        label[len++] = 'p';
        while (len < 8u)
            label[len++] = ' ';
        label[8] = '\0';
    }
    memcpy(dest, label, 8u);
}
```

Drum and HiHat show the same as before. Cymbal's Morph row changes from
"CymbalMr" to "CymbaMrp". No host test or spec pins the old text (checked:
`tools/`, `knowledge_files/specification_reference/`).

---

## 5. P4-3: InstrumentManager

### 5.1 `Core/DSP/Instruments/InstrumentManager.h` l. 63–69 — MODIFY (the enum)

```c
typedef enum {
    INSTRUMENT_TYPE_DRM = 0,
    INSTRUMENT_TYPE_SNR,
    INSTRUMENT_TYPE_CYM,
    INSTRUMENT_TYPE_HAT,
    /*
     * S093 P4: the Alkaline (TB-303) type, before UNKNOWN.
     *
     * What:       value 4; UNKNOWN moves from 4 to 5.
     * Why:        UNKNOWN is the count of real types: every type-sized array
     *             (menu_instrumentLoadIndex[], menu.c l. 1728: +2 B, R1) and
     *             every `type >= INSTRUMENT_TYPE_UNKNOWN` guard grow with it.
     *             A new type goes here, before UNKNOWN, with a registry row
     *             at the same index (InstrumentManager.c static assert and
     *             check_special_tags.py registry_check()).
     * Stored data: no stored value changes meaning. Types are stored as text
     *             tokens (AutoSave, .hcnames, instrument files, midilrn.cfg);
     *             the numeric value lives only in RAM. The .hcnames header
     *             lists the tokens in enum order, so a card's /.hcnames is
     *             rebuilt once on the first boot with five types
     *             (filesystem_validateHcnamesHeader(), filesystem.c l. 6591:
     *             that is its purpose).
     */
    INSTRUMENT_TYPE_ALK,
    INSTRUMENT_TYPE_UNKNOWN
} instrument_type_t;
```

### 5.2 `InstrumentManager.c` l. 1–22 — MODIFY (includes)

Add after l. 5 (`#include "HiHatParameters.h"`):

```c
#include "AlkalineParameters.h"   /* S093 P4: registry row */
```

Add after l. 14 (`#include "HiHat.h"`):

```c
#include "AlkalineVoice.h"        /* S093 P4: the runtime member and its entry points */
#include "sequencer.h"            /* S093 P4: seq_trackStepSamples() (read-only) for the
                                   * Alkaline gate */
```

**Include chain, verified:**
- `sequencer.h` includes `stm32f4xx.h`, `globals.h` and `PatternData.h`
  (l. 10–12);
- `PatternData.h` includes `InstrumentManager.h` (l. 13), which l. 1 has
  already included, so its guard makes the second inclusion empty;
- the scratch compile of `InstrumentManager.c` with both includes and the
  union member, on the firmware's own command line (`-O2 -flto -Wall
  -Wextra`), gave no diagnostics.

### 5.3 `InstrumentManager.c` l. 24–55 — MODIFY (the registry, F2)

```c
/*
 * Registry entries contain the instrument-owned meaning of generic slot
 * storage.
 *
 * ParameterArray/SceneData allocate the amount of per-slot storage. These
 * descriptors define what each storage cell means for each instrument type
 * and how the value reaches that instrument's runtime instance. The
 * extension and storage_directory fields are intentionally adjacent in every
 * row: filesystem.c can classify a filename and navigate to its `.hcindex`
 * folder through one registry record, with no parallel type-folder table.
 *
 * S093 P4 (F2: the type count is open-ended): rows are designated by their
 * type, so row index == type value. The .hcnames header writer/validator
 * walk the type values 0, 1, 2, ... through storage_instrumentTypeToText()
 * until NULL (filesystem.c l. 6614, 24616); a gap or a reordering would end
 * that walk early. The count assert below and check_special_tags.py
 * registry_check() (3-character tokens, enum order) hold the invariants. A
 * new type adds: an enum value before UNKNOWN, a row here, its
 * <Type>Parameters.h/.c, its runtime member and dispatch cases, a TABLES
 * entry, and its folder under SD_CARD/Instrument.
 */
static const instrument_registry_entry_t instrument_registry[] = {
    [INSTRUMENT_TYPE_DRM] =
    { INSTRUMENT_TYPE_DRM, "drm", drum_instrument_display_label, ".drm", "Drum",
      DRUM_INSTRUMENT_TYPE_FLAGS,
      drum_param_descriptors, DRUM_PARAM_DESCRIPTOR_COUNT,
      drum_menu_pages, DRUM_MENU_PAGE_COUNT },
    [INSTRUMENT_TYPE_SNR] =
    { INSTRUMENT_TYPE_SNR, "snr", snare_instrument_display_label, ".snr", "Snare",
      SNARE_INSTRUMENT_TYPE_FLAGS,
      snare_param_descriptors, SNARE_PARAM_DESCRIPTOR_COUNT,
      snare_menu_pages, SNARE_MENU_PAGE_COUNT },
    [INSTRUMENT_TYPE_CYM] =
    { INSTRUMENT_TYPE_CYM, "cym", cymbal_instrument_display_label, ".cym", "Cymbal",
      CYMBAL_INSTRUMENT_TYPE_FLAGS,
      cymbal_param_descriptors, CYMBAL_PARAM_DESCRIPTOR_COUNT,
      cymbal_menu_pages, CYMBAL_MENU_PAGE_COUNT },
    [INSTRUMENT_TYPE_HAT] =
    { INSTRUMENT_TYPE_HAT, "hat", hihat_instrument_display_label, ".hat", "HiHat",
      HIHAT_INSTRUMENT_TYPE_FLAGS,
      hihat_param_descriptors, HIHAT_PARAM_DESCRIPTOR_COUNT,
      hihat_menu_pages, HIHAT_MENU_PAGE_COUNT },
    /*
     * S093 P4: Alkaline. Token/extension "alk"/".alk"; directory
     * /Instrument/Alkaline (8 characters; SD_CARD/Instrument/Alkaline in the
     * card image with init.alk). Basic: may take any slot.
     */
    [INSTRUMENT_TYPE_ALK] =
    { INSTRUMENT_TYPE_ALK, "alk", alkaline_instrument_display_label, ".alk", "Alkaline",
      ALKALINE_INSTRUMENT_TYPE_FLAGS,
      alkaline_param_descriptors, ALKALINE_PARAM_DESCRIPTOR_COUNT,
      alkaline_menu_pages, ALKALINE_MENU_PAGE_COUNT },
};

#define INSTRUMENT_REGISTRY_COUNT \
    ((uint8_t)(sizeof(instrument_registry) / sizeof(instrument_registry[0])))

/* S093 P4: one row per real type and no gap. A designated row past the last
 * type would grow the array (and fail here); a missing row would leave a
 * zeroed hole (and fail here, or registry_check() for the order). */
_Static_assert(INSTRUMENT_REGISTRY_COUNT == INSTRUMENT_TYPE_UNKNOWN,
               "instrument_registry needs exactly one row per instrument_type_t value");
```

Keep `registryCount()` / `registryEntryAt()` / `registryEntry()`
(l. 280–318) unchanged. Their comment "without … depending on enum
contiguity" (l. 296–299) becomes "rows are indexed by type (S093 P4);
clients still enumerate through this accessor".

### 5.4 `InstrumentManager.c` l. 171–189 — MODIFY (the runtime union)

```c
typedef union {
    DrumVoice drum;
    SnareVoice snare;
    CymbalVoice cymbal;
    HiHatVoice hihat;
    AlkalineVoice alkaline;   /* S093 P4: 540 B at r6 (measured); slot reserve 1,264 B */
    uint8_t reserved[INSTRUMENT_RUNTIME_SLOT_BYTES];
} InstrumentRuntimeSlot;
```

Add after the HiHat assert (l. 186–187):

```c
/* S093 P4: the Alkaline voice fits the tagged slot (no RAM growth: the union
 * is sized by INSTRUMENT_RUNTIME_SLOT_BYTES = 2 x sizeof(DrumVoice) =
 * 2 x 632 B). */
_Static_assert(sizeof(AlkalineVoice) <= INSTRUMENT_RUNTIME_SLOT_BYTES,
               "AlkalineVoice exceeds tagged slot reserve");
```

### 5.5 `InstrumentManager.c` l. 245 — ADD (the accessor, after `instrumentManager_hihatRuntime()` l. 242–245)

```c
/*
 * The slot's Alkaline runtime (S093 P4).
 *
 * What:       the tagged union member for slot, or NULL out of range. Like
 *             the four other accessors it does not check the slot's type:
 *             every caller switches on instrumentManager_slotType() first.
 * Accessors:  the dispatch sites of §5.6 (S093_P4).
 */
static AlkalineVoice *instrumentManager_alkalineRuntime(uint8_t slot)
{
    return (slot < INSTRUMENT_SLOT_COUNT) ? &runtime_slots[slot].alkaline : 0;
}
```

### 5.6 The dispatch sites (`InstrumentManager.c`) — MODIFY, one case each

Each case is added before `default:`. Every line was read; the shapes match
the four existing cases.

| Function (line) | Add |
|---|---|
| `instrumentManager_runtimeInstance()` (l. 1393; switch l. 1404–1410) | `case INSTRUMENT_TYPE_ALK: return instrumentManager_alkalineRuntime(slot);` |
| `instrumentManager_runtimeLfo()` (l. 1413–1431) | `case INSTRUMENT_TYPE_ALK: { AlkalineVoice *voice = instrumentManager_alkalineRuntime(slot); return voice ? &voice->lfo : 0; }` |
| `instrumentManager_runtimeInit()` (switch l. 1481–1498) | `case INSTRUMENT_TYPE_ALK: Alkaline_initVoice(instrumentManager_alkalineRuntime(slot)); break;` (no slot argument, like Snare/Cymbal/HiHat: `lfo_init()` takes only the Lfo, `lfo.h` l. 122) |
| `instrumentManager_resetRuntimeSlotFromScene()` (switch l. 1614–1629) | the same line |
| `instrumentManager_runtimePan()` (l. 1727–1745) | `case INSTRUMENT_TYPE_ALK: { AlkalineVoice *voice = instrumentManager_alkalineRuntime(slot); return voice ? voice->pan : 64u; }` |
| `instrumentManager_runtimeVolume()` (l. 1756–1774) | `case INSTRUMENT_TYPE_ALK: { AlkalineVoice *voice = instrumentManager_alkalineRuntime(slot); return voice ? voice->vol : 0.0f; }` |
| `instrumentManager_calcSlotAsync()` (switch l. 1980–1995) | `case INSTRUMENT_TYPE_ALK: Alkaline_calcVoiceAsync(instrumentManager_alkalineRuntime(slot)); break;` |
| `instrumentManager_calcSlotSyncBlock()` (switch l. 2014–2030) | `case INSTRUMENT_TYPE_ALK: Alkaline_calcVoiceSyncBlock(instrumentManager_alkalineRuntime(slot), buf, size); break;` |
| `instrumentManager_triggerTrack()` (switch l. 2136–2155) | below |
| `instrumentManager_osc()` (l. 2240–2273, key form) | before `return 0;`: `if (type == INSTRUMENT_TYPE_ALK) { AlkalineVoice *voice = instrumentManager_alkalineRuntime(slot); if (!voice) return 0; if (strncmp(key, "osc1_", 5) == 0) return &voice->osc; }` |
| `instrumentManager_oscBySelector()` (l. 2289–2321) | `case INSTRUMENT_TYPE_ALK: { AlkalineVoice *voice = instrumentManager_alkalineRuntime(slot); if (!voice) return 0; if (selector == IM_SPECIAL_OSC1) return &voice->osc; return 0; }` |

Each case carries a one-line comment, for example:

```c
    case INSTRUMENT_TYPE_ALK:   /* S093 P4: the Alkaline voice owns its LFO like every engine */
```

The two render dispatchers carry the timing contract:

```c
    case INSTRUMENT_TYPE_ALK:
        /*
         * S093 P4: Alkaline_calcVoiceAsync() (control rate: pitch, MEG,
         * accent network, expo, VEG, prewarp, setNet; about 915 instructions
         * per block, no libm) and Alkaline_calcVoiceSyncBlock() (the fused
         * oscillator + B2R loop, 213 per sample, and the slow network chunk).
         * Both are noinline (AlkalineVoice.h l. 106-107): LTO must not
         * inline them into mixer_calcNextSampleBlock() (S086 F1 register
         * pressure).
         */
        Alkaline_calcVoiceSyncBlock(instrumentManager_alkalineRuntime(slot), buf, size);
        break;
```

**`instrumentManager_ampEnvelopeQuiet()` (l. 1502–1522) — MODIFY**: add
after `if (slot >= INSTRUMENT_SLOT_COUNT) return 1u;` (l. 1514–1515) and
before `ampEg = instrumentManager_ampEg(slot);` (l. 1516):

```c
    /*
     * S093 P4: Alkaline has no SlopeEg2, so the generic test below would call
     * it quiet (no envelope) while a note sounds. Its own test: gate closed,
     * ACCENT low and the OTA current (before the make-up) below 1e-7 A
     * (Alkaline_isQuiet(), AlkalineVoice.h l. 119).
     */
    if (instrumentManager_slotType(slot) == INSTRUMENT_TYPE_ALK) {
        const AlkalineVoice *voice = instrumentManager_alkalineRuntime(slot);
        return voice ? Alkaline_isQuiet(voice) : 1u;
    }
```

**`instrumentManager_triggerTrack()` (l. 2103–2156) — MODIFY**: add before
`default:` (l. 2152). The parameter names are verified: `trigger_track`,
`note`, `velocity`, and the local `slot`.

```c
    case INSTRUMENT_TYPE_ALK:
        /*
         * S093 P4: the Alkaline needs the triggering track's step length for
         * its gate (7/12 of a step) and ACCENT lane (one step). trigger_track,
         * not slot: track 7 plays slot 5 with its own scale (B-9: the two
         * tracks share one voice's slide latch and gate). The platform part
         * (LFO retrigger, velocity nodes) runs inside Alkaline_triggerVoice()
         * for `slot`, as in Drum_triggerVoice(). The slot-5 alternate decay
         * above finds no amp_envelope_decay row and does nothing.
         */
        Alkaline_triggerVoice(instrumentManager_alkalineRuntime(slot), slot,
                              velocity, note, seq_trackStepSamples(trigger_track));
        break;
```

### 5.7 Verified unchanged in InstrumentManager (NONE)

| Site (line) | Why no change |
|---|---|
| `instrumentManager_filter()` (2323), `_ampEg()` (2343), `_pitchEg()` (2363), `_distortion()` (2377), `_transient()` (2397) | `default:` returns 0, right for a type without the stage; every special writer that uses them returns 0 ("not applied") |
| `instrumentManager_slotRunMode()` (1789), `_slotVoiceChain()` (1817), `_slotHasActiveItpFade()` (1907) | `default:` gives rst / T+D / 0. Alkaline offers no `run`/`chn` and claims no `itp` slot |
| `instrumentManager_recalcSlotFilter()` (1837) | `default:` does nothing; the Alkaline prewarp is inside its async |
| `instrumentManager_slotUsesSrr()` (2033) | `default:` returns 0: no SRR (D4) |
| `instrumentManager_applySlot6AlternateDecay()` (2054) | Finds no `amp_envelope_decay` row in the Alkaline table, so it does nothing (B-9) |
| `instrumentManager_classifySpecialKey()` (3869) and `_specialTagSelfCheck()` (3955) | The self-check walks the registry. The Alkaline rows classify as tagged (§4.2) |
| `instrumentManager_advancedCountForScene()` (361), `_typeSelectableForSceneSlot()` (389) | Flag-driven: Basic is selectable in every slot (l. 402–403) |
| `instrumentManager_resetSlot()` (642) | LFO voice cells 1, targets `0xff`; the rest 0. `init.alk` sets every key |
| The coarse writer `IM_SPECIAL_PITCH_COARSE` (l. 3603–3610) | Writes `midiFreq`'s high byte and calls `osc_recalcFreq()` (`Oscillator.c` l. 1373–1389), which only sets `freq`. The Alkaline async recomputes the pitch from `midiFreq` and the note every block (`AlkalineVoice.c` l. 104–124) |

---

## 6. P4-4: build and other type users

### 6.1 `Makefile`

**l. 38 — ADD after `-ICore/DSP/Instruments/HiHat \`:**

```make
          -ICore/DSP/Instruments/Alkaline \
```

This include path is shared by every source, so `DiodeLadder.c` in
`Core/DSPAudio` also finds `AlkalineTables.h`.

**l. 115 (`SRCS`) — ADD after `Core/DSP/Instruments/HiHat/HiHatParameters.c \`:**

```make
  Core/DSP/Instruments/Alkaline/AlkalineParameters.c \
```

The descriptor table is control code, so it builds at ordinary `-O2` like
the other `*Parameters.c` (Makefile comment l. 207–208).

**`DSP_SRCS` (l. 148–175) — ADD**, each with `\`, at these places in the
list:
- `Core/DSP/Instruments/Alkaline/AlkalineControl.c`,
  `Core/DSP/Instruments/Alkaline/AlkalineSquareTable.c`,
  `Core/DSP/Instruments/Alkaline/AlkalineTables.c` and
  `Core/DSP/Instruments/Alkaline/AlkalineVoice.c` after `BusCompressor.c`
  (l. 151);
- `Core/DSPAudio/DiodeLadder.c` after `Decay.c` (l. 153).

**l. 220 — ADD after the HiHat rule (l. 218–220).** The rules are per file
because a pattern rule over `Alkaline/` would also catch
`AlkalineParameters.c`, which must stay `-O2`. `DiodeLadder.c` is covered by
the `Core/DSPAudio/%.o` rule (l. 205–207).

```make
# S093 P4: the Alkaline engine and its control path are DSP: -Ofast like the
# other engines (the P3 counts and the constant-cost loops assume it). The two
# table files are data; -Ofast changes nothing for them and keeps them with
# their users.
$(BUILD)/Core/DSP/Instruments/Alkaline/AlkalineVoice.o: Core/DSP/Instruments/Alkaline/AlkalineVoice.c | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS_DSP) $< -o $@
$(BUILD)/Core/DSP/Instruments/Alkaline/AlkalineControl.o: Core/DSP/Instruments/Alkaline/AlkalineControl.c | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS_DSP) $< -o $@
$(BUILD)/Core/DSP/Instruments/Alkaline/AlkalineTables.o: Core/DSP/Instruments/Alkaline/AlkalineTables.c | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS_DSP) $< -o $@
$(BUILD)/Core/DSP/Instruments/Alkaline/AlkalineSquareTable.o: Core/DSP/Instruments/Alkaline/AlkalineSquareTable.c | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS_DSP) $< -o $@
```

### 6.2 `Core/Hardware/SD/storageTypes.h` l. 141 — ADD after `STORAGE_INSTRUMENT_HAT`

```c
#define STORAGE_INSTRUMENT_ALK     INSTRUMENT_TYPE_ALK   /* S093 P4 */
```

### 6.3 `Core/Hardware/SD/storageTypes.c` l. 412–428 — MODIFY (F2)

`storageTypes.c` includes `storageTypes.h` (l. 18), and that header includes
`InstrumentManager.h` (l. 26). So `instrumentManager_registryEntry()`
(`InstrumentManager.h` l. 348) and the `type_text` field (l. 37) are in
scope, and no new include is needed.

```c
const char *storage_instrumentTypeToText(storage_instrument_type_t type)
{
    /*
     * Convert runtime instrument type to storage schema text (S093 P4:
     * registry-driven).
     *
     * What:       the registry row's type_text ("drm", "snr", "cym", "hat",
     *             "alk", and any later type), the lowercase token of
     *             kitset.kcg, instrument headers, .hcnames and AutoSave; NULL
     *             for an unregistered type. The four existing types return
     *             the same strings as the former switch.
     * Why:        a hard-coded switch gave NULL for a fifth type, so every
     *             Alkaline save (Instrument, Kit, Scene, Bank, AutoSave) would
     *             be refused before writing. storage_instrumentTypeFromText()
     *             (l. 395) already reads the registry; one source for both
     *             directions, and no edit here for a further type.
     * Inputs:     type.  Outputs: the token or NULL; NULL still rejects an
     *             impossible save before a partial file is written, and still
     *             ends the .hcnames header walk at the first unregistered
     *             value (filesystem.c l. 6614, 24616).
     * Accessors:  storage_instrumentTypeExtension() (l. 430); the instrument
     *             line formatter (l. 1050); filesystem.c save and HCNAMES
     *             paths.
     * Affiliates: instrument_registry[] (InstrumentManager.c).
     */
    const instrument_registry_entry_t *entry = instrumentManager_registryEntry(type);

    return entry ? entry->type_text : NULL;
}
```

### 6.4 Verified unchanged outside InstrumentManager (NONE)

| Site | Why |
|---|---|
| `menu.c` l. 1728 `menu_instrumentLoadIndex[INSTRUMENT_TYPE_UNKNOWN]` | Grows by one entry with the enum: **+2 B `.bss` (R1)** |
| Load type list: `menu_instrumentLoadStepType()` (l. 9695), `menu_instrumentLoadOptionCountForVoice()` (l. 11174), `menu_instrumentLoadOptionAt()` (l. 11196) | Walk `instrumentManager_registryCount()` / `registryEntryAt()` and filter by `typeSelectableForSceneSlot()`: Alkaline appears after HiHat, with its Morph row |
| Load guards `menu.c` l. 8498, 9498 | `>= INSTRUMENT_TYPE_UNKNOWN`, which moves with the enum |
| Instrument Save request (`filesystem.c` ≈ l. 35190–35213) | Guards `type >= INSTRUMENT_TYPE_UNKNOWN`, then saves the slot's own type; the writer is descriptor-driven (§4.6) |
| Instrument Save folder (`filesystem.c` l. 17859–17895) | Opens `instrumentManager_storageDirectory(type)` and creates it when missing |
| Instrument Load (`filesystem.c` l. 17445–17556) | Folder from the registry; `instrumentManager_resetSlot()` then the file's lines |
| Boot repair and index (`filesystem.c` l. 29431, 30403) | Both walk the registry; a missing type folder finishes DONE |
| HCNAMES rows and header (`filesystem.c` l. 24579, 24616, 6591) | Use ToText (now registry-driven); 3-character tokens (§4.4 check). The header gains `\talk`, so an existing `/.hcnames` is rebuilt once (§5.1) |
| AutoSave type field (`filesystem.c` l. 30859) | `instrumentManager_typeFromText()`, 3 bytes |
| `storage_instrumentTypeFromText()` l. 395, `storage_instrumentFilenameMatchesType()` l. 405 | Registry-driven |
| `presetManager.c` `pm_instrument_snapshot` (l. 2817–2825) and restore (≈ l. 2887–2925) | 129 B = type + 64 + 64 cells; restore guards `>= UNKNOWN` and loops `entry->descriptor_count` (26) |
| `MidiControl.c` NRPN (l. 316–329) | The NRPN number is the target ID (`slot × 64 + index`). Writes go through `menu_commitTargetEdit()` (`menu.c` l. 5674), which looks up `instrumentManager_descriptor(type, index)` and refuses an index without a row; reads clamp to the 64-cell image |
| `MidiLearn.c` l. 416 | `instrumentManager_typeFromText(field[3])` and `descriptorIndexByKey()` |
| `SceneData.c` l. 1127–1128, `filesystem.c` l. 18241 (default Scene types) | The default Scene stays Drum/Snare/Cymbal/HiHat (S093 P2 §7.2) |
| `storageTypes.c` l. 59 (HiHat key aliases), l. 1705 (effect stem) | Type-specific and unrelated |
| `mixer.c` | Dispatches every engine through InstrumentManager; Alkaline's `slotUsesSrr()` is 0 |

### 6.5 Build gate for phase 1

```
make all                                  # clean; record link_budget.py headroom (expected ≈ 53 KB)
arm-none-eabi-size build/lxr02.elf        # .bss +2 B against build/s093p4/base.elf (R1)
make -C tools/dsp_test special_tags alkaline alkaline_tables oscillator sequencer seq_steplen stock_tables
python3 tools/alkaline/make_init_alk.py   # first time: writes SD_CARD/Instrument/Alkaline/init.alk
```

Run the bench targets one by one; the bench `make all` stops at
`run_modes` (O7). Then phase 2:
`S093_P5_FIRMWARE_IMPLEMENTATION_PHASE_2.md`.

---

## 7. What was verified, and how

| Item | How |
|---|---|
| Descriptor macros, field order, LFO/velocity rows, menu page shape | Read `DrumParameters.c/.h`, `InstrumentManager.h` l. 280–346 |
| `AlkalineVoice` fields and prototypes | Read `AlkalineVoice.h` l. 56–119, `.c` l. 20–124 |
| Drum trigger platform calls | Read `DrumVoice.c` l. 227–255 |
| Every InstrumentManager dispatch site and its shape | Read each function (§5.6 lines) |
| Coarse writer → `osc_recalcFreq()` sets only `freq` | Read InstrumentManager.c l. 3603–3610, `Oscillator.c` l. 1373–1389 |
| Sequencer statics, StepScale API, stops, sample rate | Read `sequencer.c` l. 764–767, 1859–1871; `StepScale.h/.c`; `AudioCodecManager.c` l. 106 |
| Include chain `sequencer.h` → `PatternData.h` → `InstrumentManager.h` | Read; scratch compile of `InstrumentManager.c` (firmware command line, no diagnostics) |
| Struct sizes (540 / 232 / 288 / 632 B) | `arm-none-eabi-gcc` 14.2 size probe at 6 and 8 states |
| New trigger + AL-P; the legato break sed still applies | Scratch Alkaline bench: 0 failures in all ten groups |
| `seq_trackStepSamples()` + AL-T + break build | Scratch: OK; break 21 failures; M7 object calls `__aeabi_uldivmod` once |
| `control_cost.py` stub link with the real headers | Scratch copy: links, 1,393 per block (8 states), unchanged |
| Instrument file format, `[morph]` rule, `self`, `255` | Read `storageTypes.c` l. 981–1134, 2305–2318; compared with `SD_CARD/Instrument/Drum/rollind1.drm` |
| Boot / Save / Load / HCNAMES / AutoSave / snapshot / NRPN / Learn are registry-driven | Read every site in §6.4 |
| `check_special_tags.py` would fail on a `*_SPECIAL` NONE tag | Read `actual()` l. 72–76 |
| Load "Mrp" label defect | Read `menu.c` l. 9666–9693; no test or spec pins the old text |
| `FW_ELF` absent from the bench Makefile; `MCU` is the Makefile's variable | Read both Makefiles |
| `elf_loopsrc.py` (P5 §3.2) | Scratch run on `build/s093p3/dbg/lxr02.elf`: 86 loops attributed; self-compare 0 grew; path-with-spaces bug found and fixed in the schedule |
| `AlkalineParameters.h/.c` (§4.1–§4.2) | Extracted from this document and compiled for the M7 (`-O2 -Wall -Wextra`): no diagnostics, 1,635 B text |
| `check_special_tags.py` with `TABLES['ALK']` and `registry_check()` (§4.4) | Extracted and run on the five tables and the §5.1/§5.3 code: 197 rows, 0 mismatches; a 4-character token is reported |
| `make_init_alk.py` (§4.6) | Extracted and run: its output is byte-identical to the §4.6 listing; `--check` passes |

Left for the device or your ear: the CPU worst case, the make-up level, the
listening checks, and the storage round trips on real hardware (P5 §4).

---

## 8. Implementation log (this session)

Working tree: HEAD `db98c287` ("alkaline pre-implementation final", the P3
code). Each entry names the phase gate and the evidence.

### P4-0 (done)

- **r6 applied.** `tools/alkaline/gen_alk_tables.py` `NET_STATES = 6` (the
  `# was:` line in §2.1 is the schedule's annotation; the committed source
  carries the new statement and its explanation only).
  `gen_alk_tables.py` regenerated `AlkalineTables.h/.c`; `--check` passes and
  `AlkalineSquareTable.c` is byte-identical, as expected.
- **`DiodeLadder.h`.** `ALK_NET_STATES` is `6u` with the schedule's block
  comment; layout text is 72 floats (288 B) per point and 232 B for
  `DiodeLadder`. Also corrected the adjacent stale indices the count change
  invalidates (`gam_rows0..ALK_NET_STATES-1`, the two asymptote rows,
  `z[ALK_NET_STATES + 2]`), documentation only.
- **C0 gate.** `python3 tools/alkaline/check_c.py` (all parts) rerun and the
  device reference `alk_c_*_B2R.wav` regenerated. B2R against golden, LSD60:
  acid ×1/×4/×8 = 0.239 / 0.877 / 0.931; lowres = 0.271 / 1.156 /
  2.244. P3 §9.3 has 0.24 / 0.88 / 0.94 and 0.27 / 1.16 / 2.24: every point
  within ±0.01 dB. C1/C2 move ≤ 0.03 dB from the 8-state run.
- **Bench.** `make -C tools/dsp_test alkaline`: AL-R B2R and B-R 2.018 ms /
  0.363 per ring cycle (was 2.040 / 0.364 at 8 states); all ten groups 0
  failures; the legato break is still caught (AL-S).
- **ELF targets.** `tools/dsp_test/Makefile`: `FW_ELF`, `P4BASE`,
  `P4_OSC_SYMS`, `elf_p4_snapshot`, `elf_p4_check` added after
  `alkaline_tables`; `elf_p4_snapshot elf_p4_check seq_steplen` added to
  `.PHONY` (`seq_steplen` lands with P4-1).
- **Baselines (before any P4-1 code).** `make all`: text 667,072 B, data
  420 B, bss 413,284 B; flash headroom 86,172 B. `elf_p4_snapshot` wrote
  `build/s093p4/osc.txt` (85 loops in 44 symbols at first, re-dumped to 93
  loops in 48 symbols after the `elf_loopdump.py` normalisation below, from
  the same image) and `mixer.txt` (51 loops); `build/s093p4/base.elf` is the
  P3 image and
  `build/s093p4/dbg/lxr02.elf` the `-g` twin (same text/data/bss).
- **Drum ceiling re-measured on `base.elf`** (P4 §2.2): 51 + 30 + 22 + 2.5
  + 92 (2 VDIV) + 25 (1 VDIV) = 222.5 per sample; 391 + 531 = 922 per block
  (/32 = 28.8); SRR 10. Charged 261.3 per sample, 3 VDIV — identical to P3
  §1. Recorded in `build/s093p4/drum_ceiling.txt`. (`elf_loopsrc.py`, the
  source-paired GREW check, is a P4-6/P5 §3.2 deliverable and is not in the
  tree yet.)
- **Counter default.** `control_cost.py --rows` defaults to 3; the r6 voice
  measures 1,285 per block (async 915 + sync 356 + dispatch 14), 9 VDIV,
  40.2 per sample.

### P4-1 (done)

- **Trigger.** `AlkalineVoice.h` carries the new contract and signature;
  `AlkalineVoice.c` adds the two direct includes (`modulationNode.h`,
  `InstrumentManager.h`) and `Alkaline_triggerVoice(voice, source_slot,
  velocity, note, stepSamples)`: the three platform calls of
  `Drum_triggerVoice()` for `source_slot`, then the unchanged 303 rules. The
  legato line the bench's break-build sed matches is still verbatim.
- **Step length.** `sequencer.h` / `sequencer.c` add `seq_trackStepSamples()`
  (96 PPQ Q8.8 x 44,108 Hz, 64-bit product, nearest sample, default 1/16 for
  a track >= NUM_TRACKS, BPM 0 as 1), main-loop/read-only.
- **Bench.** `modules/alkaline.c`: the Lfo stand-in block grew the
  ModulationNode/InstrumentManager stand-ins with AL-P call counters; all 15
  trigger calls pass `0u` as the slot; AL-P asserts one LFO retrigger, one
  velocity target and slot 3 at velocity 127. `alkaline` reports 0 failures
  in all ten groups (AL-S carries AL-P) and the legato break is still caught.
- **`control_cost.py`** stubs add the platform calls; the trigger call is the
  new signature. It still links for the M7 and measures 1,285 per block.
- **AL-T.** New `modules/seq_steplen.c` and `seq_steplen` target (also in
  `all:`): table + double-precision reference at 40/120/300 BPM x the 1/32,
  1/16, 1/8 stops; out-of-range track; 5,292,960 longest step; BPM 0. The
  generated 32-bit break build fails as designed. `seq_steplen: OK (0
  failures)`.

### P4-2 (done)

- **`AlkalineParameters.h/.c`** added exactly as §4.1/§4.2: 26 rows (10
  303/drive, vol/pan, 8 LFO, 2 velocity, 4 targets), eight `alkaline_menu_pages`
  (OSC, empty amp, velocity, empty FM, empty transient, FILTER, LFO, MIX),
  the display label/type flags, and the `_Static_assert`s (26 rows, fits 64
  cells, 8 pages). `ROW_SELECT_ONOFF` is spelt without a `NONE` tag, as
  required by `check_special_tags.py`'s `actual()`.
- **Menu text.** `MenuText.h`: `ONOFF_TAG_ALK_WAVE 3u` and `alkWaveNames`
  (Saw, Sqr). `menu.c`: `menu_onoffTagNames()` is a three-way switch and the
  count assert follows the `voiceChainNames` assert. The six callers are
  unchanged and take the range from `names[0][0]`.
- **Host registry check.** `check_special_tags.py` gains `TABLES['ALK']`,
  `ENUM`, `REGISTRY`, `registry_check()` and `bad = registry_check()` in
  `main()`; `special_tags` now also runs `make_init_alk.py --check`.
  **Ordering note:** between §4.4 and §5.3 the registry check can only see
  the old non-designated rows, so the gate is red until P4-3 lands (it then
  reads `[INSTRUMENT_TYPE_*]`). Final result after P4-3: `special tags OK
  (197 rows, 0 mismatches)`; the init check matches.
- **Init defaults.** `Alkaline_initVoice()` now loads the exact bytes of
  `init.alk` (64/127 for the five 303 knobs, 63/127 tuning, drive 0, vol
  100/127); pan, Saw and slide were already equal.
- **`init.alk`.** `tools/alkaline/make_init_alk.py` added and run;
  `SD_CARD/Instrument/Alkaline/init.alk` is 54 lines, LF, 26 `[params]` keys
  and the 20 `[morph]` rows (`MORPHABLE` macros only), with `self` for the two
  LFO voices and `255` for the target selectors. `--check` byte-compares it
  to the table.
- **Load label.** `menu_instrumentLoadCopyTypeLabel()` truncates the base
  label to five before appending `Mrp`: Drum `DrumMrp `, HiHat `HiHatMrp`,
  Cymbal now `CymbaMrp` (was `CymbalMr`), Alkaline `AlkalMrp`.

### P4-3 (done)

- `INSTRUMENT_TYPE_ALK` = 4 before `UNKNOWN` (= 5), with the full comment
  block (arrays, guards, text-only storage, one-time `.hcnames` rebuild).
- `InstrumentManager.c`: the two includes, the five designated registry rows
  plus the ALK row and the count assert, the union member and its
  `_Static_assert`, `instrumentManager_alkalineRuntime()`, and every listed
  dispatch site (instance, LFO, runtime init, Scene reset, pan, volume,
  async, sync block with the noinline/timing comment, trigger with the
  `trigger_track` step length, `osc()` key form, `oscBySelector()`), plus the
  Alkaline branch in `instrumentManager_ampEnvelopeQuiet()`. The
  `registryCount()` comment now says rows are indexed by type.
- The §5.7 sites are untouched; the build's `_Static_assert`s and
  `special_tags` confirm the registry order/tokens, and the device
  diagnostic path (`classifySpecialKey`) matches every ALK row.

### P4-4 (done)

- **Makefile:** `-ICore/DSP/Instruments/Alkaline`, `AlkalineParameters.c` in
  `SRCS`, the four engine/table files in `DSP_SRCS` after `BusCompressor.c`,
  `DiodeLadder.c` after `Decay.c`, and four per-file `-Ofast` rules after the
  HiHat rule. `DiodeLadder.c` uses the `Core/DSPAudio/%.o` rule.
- **`storageTypes.h`:** `STORAGE_INSTRUMENT_ALK`. **`storageTypes.c`:**
  `storage_instrumentTypeToText()` is a registry lookup, so all five types
  save and the `.hcnames` header walk gains `alk` and still stops at
  `UNKNOWN` (NULL).
- **Build (fresh `BUILD=build/s093p4/final`, identical to `make all`):**
  text 703,832 B, data 420 B, bss 413,280 B; flash headroom **49,412 B**
  (>= 16 KiB); ITCM 14,552 B; DTCM statics 4,392 B; FXBUF margin 3,776 B;
  no warnings from the changed sources.
- **RAM:** `menu_instrumentLoadIndex` 8 -> 10 B (**+2 B, R1**). The `.bss`
  section total is unchanged (the +2 sits in its alignment slack); the
  summary's `bss` figure is 4 B lower because LTO re-groups the
  `modNode_*` DTCM zeroed symbols (.dtcmz 3,884 -> 3,880 B). No other
  type-sized array exists (`rg '\[INSTRUMENT_TYPE_UNKNOWN'` finds only this
  one).
- **G2 (host laws), one by one:** `special_tags` (197 rows/0), `alkaline`
  (ten groups/0, AL-S break caught), `alkaline_tables`, `oscillator`,
  `sequencer`, `seq_steplen` (AL-T break caught), `stock_tables` all pass.
- **`elf_p4_check`:** 93 baseline loops, **93 exact, 0 renamed, 0 missing**;
  the mixer compares **30 hot loops equal, 0 grew** (no Alkaline loop leaked
  into `mixer_calcNextSampleBlock`; the `noinline` boundary held).
- **G3/G7 static figures:** `DiodeLadder_renderB2Osc` is inlined into
  `Alkaline_calcVoiceSyncBlock.constprop.0`, whose fused loop is **214**
  (expected ~213) with **3 VDIV**; `Alkaline_calcVoiceAsync`'s longest static
  path is 872 (its loops are added by control_cost's `--elf` mode in P5);
  `osc_setFreq`'s per-block path stays **128**.
- **G6/G7 tooling correction (not in the literal schedule, documented here):**
  LTO renamed the four existing wavetable blend functions to
  `*.lto_priv.0` and moved long-branch/veneer distances when the fifth engine
  joined, so `elf_loopdump.py` reported 15 loop bodies MISSING although every
  per-sample loop is unchanged (46/47/48 instructions by `elf_looppath.py`).
  `elf_loopdump.py` now (a) strips `.lto_priv.N` in `SUFFIX` like
  `elf_looppath.py` already does and (b) writes a branch to a target outside
  the body as `+EXIT` (layout, not loop cost). The baseline was re-dumped
  from the same `base.elf` with the same extractor, so both sides are
  normalised identically; the 93/93 result above is against that dump.
- **G8 observation:** ITCM is **14,552 B, +40 B** over the P3 image. No ITCM
  function or subsection was added: the four `.itcm.s087` blend functions
  keep their per-sample loop lengths but grew 10-18 B each in their entry
  code after `AlkalineVoice.c`'s new `modulationNode` references changed LTO
  symbol internalisation/addressing. 1,832 B of ITCM remains; phase 2 should
  record this against G8's "unchanged" wording (it is LTO layout, not new
  ITCM code).

---

## 9. Assessment of the phase 1 implementation (commit `570f7926`)

**Method.** An independent check of commit `570f7926` against this schedule
and against `db98c287` (the P3 code), without relying on the §8 log:
- the source diffs, read site by site;
- the new files compared byte for byte with the schedule's code blocks;
- a clean firmware build into a separate directory;
- every host gate rerun;
- the linked image measured with `elf_looppath.py`, `elf_blockpath.py`, the
  `control_cost.py` walk and a `-g` twin (`build/s093p4/dbg_now`);
- the original `elf_loopdump.py` (from `db98c287`) run beside the modified
  one.

Nothing in the tree was changed. Scratch work lives outside the repository;
the `-g` twin and a clean build are under `build/s093p4/` (`dbg_now/`,
`assess/`).

### 9.1 Verdict

Phase 1 is implemented as scheduled, and every gate that can run without the
device passes. The Alkaline voice measures **252.1 per sample on the linked
image** (214 + 1,218 / 32, 3 + 9 VDIV) against the Drum ceiling of 261.3.
This is the first linked whole-voice figure.

Three items need recording or a decision; none blocks phase 2:
- **A1:** ITCM +40 B, and the per-block entry paths of three wavetable-blend
  functions grew by 1–3 instructions. Recommendation: accept.
- **A2:** an unscheduled change to `elf_loopdump.py`. Justified; keep it.
- **A4:** `control_cost.py --elf` as scheduled in P5 §3.1 would exit on the
  linked image; one `ITERS` entry fixes it.

Two corrections to my own schedule follow, in A3 and A7.

### 9.2 Conformance

| Area | Result |
|---|---|
| `AlkalineParameters.h/.c`, `make_init_alk.py`, `modules/seq_steplen.c`, `SD_CARD/Instrument/Alkaline/init.alk` | **Byte-identical** to the §4.1, §4.2, §4.6 and §3.4 blocks |
| `InstrumentManager.h/.c` (enum, includes, designated registry + count assert, union + assert, accessor, 11 dispatch cases, quiet test, trigger) | As §5, line for line |
| `AlkalineVoice.h/.c` (prototype + contract, includes, trigger, init bytes) | As §3.1 / §4.5; the legato line is verbatim (the break sed applies) |
| `sequencer.h/.c`, `storageTypes.h/.c`, `MenuText.h`, `menu.c` (tag helper, assert, Load label), `Makefile` | As §3.2, §6.2–§6.3, §4.3, §4.7, §6.1 |
| `DiodeLadder.h`, `gen_alk_tables.py`, regenerated tables | As §2.1, plus documentation-only corrections of indices that the state count made stale (§8) |
| `control_cost.py`, `check_special_tags.py`, bench `Makefile`, `modules/alkaline.c` (15 calls + AL-P) | As §2.2–§2.3, §3.3–§3.4, §4.4 |
| `tools/dsp_test/elf_loopdump.py` | **Not in the schedule**: A2 |
| Entry points in the image | `Alkaline_calcVoiceAsync` and `Alkaline_isQuiet` are reached by tail call (`b.w`) from `instrumentManager_calcSlotAsync` / `_ampEnvelopeQuiet`; `Alkaline_calcVoiceSyncBlock.constprop.0` is called from `mixer_calcNextSampleBlock` (an IPA-CP clone: `noinline` keeps it out of the mixer but does not prevent cloning); `Alkaline_triggerVoice` from the render path's trigger drain; `Alkaline_initVoice` from boot and the Scene reset |

### 9.3 Gates, rerun

| Gate | Measured here | Pass |
|---|---|---|
| G1 build | Clean build into `build/s093p4/assess`: image byte-identical to `build/lxr02.elf`; text 703,832 B, data 420 B, bss 413,280 B; flash headroom **49,412 B**; **0 warnings** from the changed sources (the 10 warnings in the log are all in untouched files) | Yes |
| R1 | `menu_instrumentLoadIndex` 8 → 10 B. `.bss` section 279,644 B on both images; `.dtcmz` 3,884 → 3,880 B (A1) | Yes |
| G2 host laws | `special_tags` 197 rows / 0; `alkaline` 10 groups / 0, legato break caught; `alkaline_tables`; `oscillator` (all groups 0, FR1/FR2 breaks caught); `sequencer` (break caught); `seq_steplen` OK, break caught; `stock_tables`; `gen_alk_tables.py --check` matches | Yes |
| C0 (§2.1) | B2R against golden, LSD60: acid 0.239 / 0.877 / 0.931, lowres 0.271 / 1.156 / 2.244. Against P3 §9.3 every point is within ±0.01 | Yes (A3) |
| G3 audio loop | Linked fused loop **214**, 3 VDIV, inside `Alkaline_calcVoiceSyncBlock.constprop.0` (standalone 213) | Yes (±3) |
| G4 per block (linked) | **1,218** = async 872 + sync block 332 + dispatch 14, **9 VDIV**. The stub count is 1,285 (async 915 + 356 + 14) | Yes (A4) |
| G5 whole voice | 214 + 1,218 / 32 = **252.1** against 261.3: margin ≈ 9 | Yes, statically. The device decides (K1) |
| G6 mixer | `elf_p4_check`: 30 hot loops equal, 0 grew. The 21 other fragments are not compared (§9.5) | Yes |
| G7 oscillators | Modified tool: 93 / 93 exact. The original tool: 15 MISSING, all explained (A2). Per-sample loops measured directly on both images are identical (A1 table). `osc_setFreq` per block 128 on both | Yes |
| G8 ITCM | 14,512 → **14,552 B** (+40); 1,832 B free | **No, literally**; A1 |

### 9.4 Findings

**A1. ITCM +40 B; small per-block changes in the wavetable-blend functions.**
- **What moved.** Only the five ITCM blend functions changed size:
  `osc_wtbBlendBlock` +12, `osc_wtbFmBlendBlock` +10,
  `osc_wtbFmBlendRampBlock` −6, `osc_wtbRingBlendBlock` +18,
  `osc_wtbSyncBlendBlock` +12. Four of them are now `*.lto_priv.0`.
- **Measured on both images:**

  | Function | Per-sample loops (base → now) | Per block (base → now) |
  |---|---|---|
  | `osc_wtbBlendBlock` | 38, 34 → 38, 34 | 110 → 110 |
  | `osc_wtbFmBlendBlock` | 46, 42 → 46, 42 | 120 → **121** |
  | `osc_wtbFmBlendRampBlock` | 47, 43 → 47, 43 | 122 → 119 |
  | `osc_wtbRingBlendBlock` | 47, 43 → 47, 43 | 134 → **137** |
  | `osc_wtbSyncBlendBlock` | 48, 44 → 48, 44 | 109 → **111** |
  | `osc_wtbPlainBlock` | 26, 24 → 26, 24 | 42 → 42 |
  | `osc_setFreq` | — | 128 → 128 |

- **Cause, confirmed in the disassembly.** The functions read four
  interpolation globals: `modNode_waveInterpGeneration`,
  `modNode_interpLastStamp`, `modNode_interpReserved` and
  `modNode_waveInterpActiveCount`.
  - In the base image they sat next to `wavetable_count` in SRAM1 and were
    read through one literal-pool base pointer plus offsets.
  - In the new image LTO placed them in the DTCM zero section
    (`0x20001100–0x20001106`). Each now has its own literal load (three
    `ldr rX, [pc]` in place of one), and `.dtcmz` changes by 4 B.

  This is a data-layout consequence of the new LTO partition (AlkalineVoice.c
  now references `modulationNode`), not new logic. The per-sample loops are
  unchanged.
- **Cost.** At most +3 instructions per block (+0.09 per sample) on a slot
  that plays a wavetable blend. The loads now go to zero-wait DTCM rather
  than SRAM1, so cycles may not grow at all. The blend is outside the
  frozen Drum figure (pool-limited system-wide, P3 §1).
- **Recommendation:** accept as LTO layout. Record G8 as "14,552 B, +40 B
  of entry code in five existing functions, no new ITCM code". Because it
  touches another engine's per-block path, it is listed here for the user's
  sign-off. If it is not accepted, the remedy is to keep those globals
  together (one struct in `modulationNode.c`) so a single base pointer
  serves them, whatever partition LTO chooses.

**A2. `elf_loopdump.py` change (unscheduled): justified; keep it.**
- **What changed:**
  - `SUFFIX` also strips `.lto_priv.N`, as `elf_looppath.py` already did;
  - a branch target outside the loop body is written `+EXIT`.
- **The original tool on the same two images:** 85 baseline loops, 70
  exact, 15 MISSING, 74 loops now. Its 15 MISSING come from two effects:
  - the four blend functions renamed `*.lto_priv.0` were not dumped at all,
    because the old suffix pattern failed to match them (85 → 74 loops);
  - exit offsets moved (`osc_setFreq`'s `b.w +14158`).
- **With the change:** both images give 93 loops in 48 symbols, all exact,
  and the direct measurements in A1 agree.
- **What it gives up:** a change that only moves a loop exit is no longer
  reported. That is layout, not loop cost.
- **Baseline:** re-dumped with the new extractor from the same `base.elf`,
  which is legitimate because both sides are normalised identically.
- **Follow-up:** P5 should record the semantics change in the tool list
  (`elf_loopdump.py` docstring done; P5 §3.4 G7 text).

**A3. Schedule correction (mine): the C0 expectation in §2.1 named the
wrong reference.**
- §2.1 said C0 against the S093 P1 variants would be "about −45 dB and
  LSD60 ≈ 0.01 dB".
- In fact C0's "variant" is the frozen S093 P1 kernel. Before r6 the C build
  already differed from it by LSD60 0.10–0.22 dB, from T2-2 and T2-3
  (`db98c287` results: 0.118 / 0.168 / 0.207 / 0.101 / 0.137 / 0.216).
- r6 moves those by at most 0.011 dB (now 0.112 / 0.160 / 0.197 / 0.102 /
  0.140 / 0.221). The ≈ 0.01 figure is r6 against the 8-state C build
  (P3 §9.3).
- The gate (distance to golden) is right and passes; only the sentence was
  wrong.

**A4. G4 on the linked image: P5 §3.1 needs one more `ITERS` entry.**
- LTO unrolled every fixed-count control loop (accent 4, MEG 4, expo 3, the
  3-row network chunk) and inlined every callee, `DiodeLadder_renderB2Osc`
  included.
- So the linked `Alkaline_calcVoiceAsync` has no loops and no calls, and
  `Alkaline_calcVoiceSyncBlock` has one loop: the 214-instruction audio
  loop.
- `control_cost.py --elf` as written in P5 §3.1 would exit on it ("no
  iteration count for loop 1 of Alkaline_calcVoiceSyncBlock (length 214)").
- **The fix:**
  - add `("Alkaline_calcVoiceSyncBlock", 1): 0` to `ITERS`, the audio loop
    weighted 0 like the other render loops;
  - the `--iters` machinery stays for a future layout;
  - correct the P5 §3.1 "why" text: the current image inlines everything,
    so the standalone and linked counts differ by LTO, not by missing loops.
- With that entry, the walk gives 872 (9 VDIV) + 332 + 14 = 1,218.

**A5. Fused loop 214 against 213.**
- +1 on the linked image.
- Inside G3's ±3 tolerance, and already included in G5's 252.1.

**A6. The Drum ceiling on the P4 image (P5 §3.3) is not yet re-measured
end to end.**
- §8 re-measured it on `base.elf` only.
- On the new image:
  - the parts outside the mixer are identical: `Drum_calcVoiceAsync` 63,
    `slopeEg2_calc` 28, dispatch 14, `SVF_calcBlockZDF` per block 161,
    `osc_setFreq` 128;
  - the mixer's hot loops are 30 / 30 equal.
- Still uncompared: the mixer's 21 non-hot fragments, which hold the
  inlined Drum render entries (`Drum_calcVoiceSyncBlock` 163,
  `voicePost_drum`'s entry).
- P5 §3.3 does that with `elf_loopsrc.py`, which is not in the tree yet,
  using the `-g` twin built for this assessment
  (`build/s093p4/dbg_now/lxr02.elf`, identical text/data/bss).
- G5's margin (≈ 9) is far larger than any plausible change there.

**A7. Schedule ordering (mine).**
- §4.4's `registry_check()` reads designated registry rows, which only
  arrive with §5.3 (P4-3). So `special_tags` is red between P4-2 and P4-3,
  as §8 notes.
- "Do P4-1 to P4-4 in one sitting" covers it. For a later type, the
  add-a-type checklist (P5 §5) should list the designated registry row
  before the tag gate.

**Note.**
- The commit includes build outputs: `tools/dsp_test/build/*` (binaries and
  generated host files), `build/s093p4/*` and `build/s093p3/cost/stubs.c`.
- The `tools/dsp_test/build` files were already tracked; the `build/s093p4`
  baselines are new.
- They hold no source; record for awareness only.

### 9.5 Carried to phase 2

| Item | Where |
|---|---|
| `elf_loopsrc.py` (not in the tree) | P5 §3.2 |
| `control_cost.py --elf` with the A4 `ITERS` entry | P5 §3.1 |
| The Drum ceiling on the P4 image, the mixer's non-hot fragments by source | P5 §3.3, A6 |
| G8 wording and the A1 decision | P5 §3.4, user |
| `elf_loopdump.py` semantics in the gate text | P5 §3.4 G7, A2 |
| Device trial (CPU worst case, listening, make-up, storage round trips, Load labels) | P5 §4 |
