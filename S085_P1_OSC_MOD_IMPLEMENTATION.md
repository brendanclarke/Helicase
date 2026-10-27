# S085 P1 — Drum `snc` / `rng`: implementation

**Session:** 085, phase P1. This document is the implementation schedule: it
specifies every change and does not change any firmware itself.
**Branch:** `dev-ph7-voice_expansion` at `1ea234ea` (code = the S084 roll
fix-up).
**Date:** 2026-10-27
**Plan:** `S085_P1_OSC_MOD_SNC_RNG.md` (r4; all recommendations accepted,
RAM accepted).

---

## 0. Read this first

### 0.1 How to use this document

- **Line numbers** refer to the tree at `1ea234ea`. Each change also names
  its anchor text.
  - Apply the changes **bottom-up within each file** (the highest line
    first), so the earlier line numbers stay valid.
  - The index in §3 is already in that order.
- **Every code block is the final text.** The comment blocks inside the code
  (What / Why / Inputs / Outputs / Accessors / Affiliates) are
  documentation-in-place: paste them with the code.
- **Change IDs:** `O-H` Oscillator.h, `O-C` Oscillator.c, `D-H` DrumVoice.h,
  `D-C` DrumVoice.c, `P` DrumParameters.c, `T` MenuText.h, `M` menu.c, `X`
  tools/dsp_test, `S` specifications.
- **Nothing else changes:**
  - Snare, Cymbal and HiHat;
  - every existing oscillator loop;
  - `BufferTools.c/.h` and `config.h`;
  - the Preset, Morph and MIDI code;
  - sample flash.

### 0.2 Deviations from the plan (found in the code deep-dive)

Each one was measured or traced during this deep-dive. §1 gives the
figures.

| # | Plan said | Implementation does | Why |
|---|---|---|---|
| **D1** | `rng` = carrier plain render + a ring pass in `BufferTools.c` (§4.2) | the ring law is **folded into per-waveform carrier copies** in `Oscillator.c` (`calc*RingBlock`), replacing the plain loop's float gain store. `BufferTools` is untouched. | Measured: the ring pass costs 9 per sample (scalar) or 7.5 (packed). With user-sample oscillators, `fm` = 41 + 49 = **90** and `mix` = 41 + 41 + 5 = 87. `rng` with a pass would be 41 + 41 + 7.5…9 = **89.5–91**: equal to `fm` or over it, which fails the gate (Q1 (a)). The folded copy costs plain + 0…4 (the user-sample copy measures the same as its plain loop), so `rng` = 41 + 41 = **82**, below `fm` for every waveform (§1.2). |
| **D2** | the clock is `modOsc.phase`, advanced arithmetically (§4.1) | a dedicated **`DrumVoice.syncClock`** (`uint32_t`, +4 B). `modOsc.phase` is not touched in `snc`. | A one-shot user sample on the FM page holds its phase at the sample's end. Advancing `modOsc.phase` freely while in `snc` would leave it far past the end, so on a switch back to `fm`/`mix`, `calcUserSampleOscBlock()` would read up to 64 KB past the sample. A sample near the top of sample flash would then read beyond flash (HardFault). A separate clock removes the hazard. Cost: `DrumVoice` 588 → 592 B; the tagged-slot reserve is 2 × `sizeof(DrumVoice)`, so `runtime_slots` +48 B (RAM accepted, r3). |
| **D3** | the sync loop keeps the plain loop's `× gain` (§4.3.2) | the sync copies store `oscOut` directly. `snc` always renders the slave at gain 1. | Saves the float gain multiply. It is exact: an `int16` × 1.0f stored back to `int16` is the same value, so depth 0 still equals the plain render bit for bit. |
| **D4** | G-A: `Drum_calcVoiceSyncBlock` disassembly identical (§5) | G-A compares the **callees** (every existing oscillator loop, `osc_setFreq`, `bufferTool_addBuffersSaturating`) byte for byte. In `Drum_calcVoiceSyncBlock`, the `fm`/`mix` paths are checked to make the same calls with the same arguments in the same order. | The dispatch itself has to change from `if` to `switch`: a few instructions per block (< 0.2 per sample), which is not loop cost. |
| **D5** | ITCM ≈ +1.0–1.5 KB (§8) | **≈ +2.85 KB** (2,848 B: the documented code compiled standalone; LTO may move it by a little) | Ring copies are needed for five waveform families (D1), including the user-sample loop (552 B). The two interpolation variants are inlined into their ITCM dispatchers, as `calcPeriodicInterpBlock()` is today. 12,216 B of ITCM are free. |
| **D6** | the reset list "128 B on the stack" | the same (`syncOver[32]`, SRAM2 stack) | It is not put in DTCM: the DTCM statics would eat the FX-arena margin (3,712 B). |

### 0.3 Precondition

MEMORY.md: the S084 hardware check V1–V7 gates S085, and it is still open
(plan R1). Run it, or confirm P1 may proceed alongside it.

---

## 1. Measured baseline and the cost gate

### 1.1 Method

- **Tool:** `tools/dsp_test/fpseq.py --report --all`, the per-sample
  instruction totals of each loop body.
- **Build:** `Core/DSPAudio/Oscillator.c` compiled exactly as the firmware's
  DSP rule (`-Ofast`, Cortex-M7 flags, the firmware include set) in this
  deep-dive.
- **New loops:** measured on the code in §4 itself, spliced into a scratch
  copy of `Oscillator.c/.h` and built with the same flags. That copy also
  compiled warning-free under `-Wall -Wextra` and passed the X4 host test
  (§5).
- **These are static counts, not cycles.** The gate is G-C on the device
  (§6).

### 1.2 Figures (instructions per sample)

| Waveform | Plain (today) | FM carrier (today) | **Ring copy (new)** | **Sync copy (new)**: no reset / reset path |
|---|---:|---:|---:|---:|
| Tri/Saw/Rec table | 24 | 35 | 28 | 34 / 39 |
| Sine | 23 | 30 | 27 | 33 / 38 |
| Crash | 30 | 38 | 30 | 36 / 41 |
| Noise | 12–13 (27 on the RNG wrap) | = plain (FM ignored) | 13 (25 on the wrap) | renders plain |
| User sample (main variants) | 40–41 | 48–49 | 40–41 | renders plain |

| Section loop | Per sample |
|---|---:|
| Saturating add (`mix`), packed | 5 (10 per 2 samples) |
| Sync reset-list builder (both paths; fpseq's second figure, 16, spans both branch paths plus the epilogue) | 7 |
| Ring pass, separate, scalar / packed (**rejected**, D1) | 9 / 7.5 |

### 1.3 Composition against the gate (Q1 (a): the dearest setting in any mode ≤ the dearest `fm`)

| Mode | Table | Sine | Crash | User sample | Dearest |
|---|---:|---:|---:|---:|---:|
| `fm` (today) = M + C_fm | 59 | 53 | 68 | **90** | **90** (ceiling) |
| `mix` (today) = M + C + 5 | 53 | 51 | 65 | 87 | 87 |
| `rng` = M + C_ring | 52 | 50 | 60 | 82 | 82 |
| `snc` = builder 7 + C_sync (reset path) | 46 | 45 | 48 | 7 + 41 (unsynced) | 48 |

- Waveform interpolation keeps the order:
  - `fm` interpolated carrier = M + 2 × C_fm + blend;
  - `rng` = M + 2 × C + blend with the ring;
  - `snc` = 7 + 2 × C_sync + blend.
- **(b), each mode at the same waves:** `rng` and `snc` are below `fm` for
  every waveform. Noise `rng` equals noise `fm` (13 vs 12–13).

---

## 2. Design summary (what the code does)

```
Drum_calcVoiceSyncBlock()  switch (modMode)
  fm  (0, default): modOsc plain (gain amt) -> modBuf; osc FM from modBuf            [unchanged]
  mix (1):          modOsc plain (gain amt) -> modBuf; osc plain (1-amt); QADD16     [unchanged]
  rng (3):          modOsc plain (gain amt) -> modBuf;
                    osc RING copy: buf[i] = SSAT16(osc_i x (K + modBuf[i]) >> 15), K = (1-amt) x 32768
  snc (2):          clockInc = osc_syncClockInc(modOsc)          (x32 for crash / user samples)
                    syncClock = osc_buildSyncList(syncClock, clockInc, syncOver[])
                    osc SYNC copy, gain 1: after sample i, if syncOver[i] != NONE:
                        phase += depth x (signed)(syncOver[i] x ratio - phase)
                    modOsc is not rendered and its phase is not changed
```

- **Sync slaves:** `Sin Tri Saw Rec Cym`.
- **Unsynced slaves:** `Noi` and user samples (they render plain).
- **Masters:** every `wav` is a clock at the FM-page frequency.
- **Waveform interpolation** (either mode): two partner renders + the
  existing blend. In `snc`, the partners use the same reset list.

---

## 3. Change index (apply in this order)

| ID | File | Lines (at `1ea234ea`) | Action |
|---|---|---|---|
| X1 | `tools/dsp_test/snapshot85.sh` | new | Add, **and run before any source edit** |
| X2 | `tools/dsp_test/elf_symdump.py` | new | Add |
| X3 | `tools/dsp_test/Makefile` | 29–30, append after 195 | Modify, Add |
| X4 | `tools/dsp_test/test_osc_mod.c` | new | Add |
| — | **Baseline** (§6 step 1) | — | build, dump, record |
| O-H1 | `Core/DSPAudio/Oscillator.h` | after 149 | Add |
| O-C1 | `Core/DSPAudio/Oscillator.c` | after 913 | Add |
| D-H2 | `Core/DSP/Instruments/Drum/DrumVoice.h` | 103–105 | Modify + Add |
| D-H1 | `Core/DSP/Instruments/Drum/DrumVoice.h` | after 56 | Add |
| D-C2 | `Core/DSP/Instruments/Drum/DrumVoice.c` | 313–326 | Replace |
| D-C1 | `Core/DSP/Instruments/Drum/DrumVoice.c` | 140–142 | Modify + Add |
| P1 | `Core/DSP/Instruments/Drum/DrumParameters.c` | 192 | Modify (+ comment) |
| T2 | `Core/Menu/MenuText.h` | after 108 | Add |
| T1 | `Core/Menu/MenuText.h` | 43–44 | Remove |
| M8 | `Core/Menu/menu.c` | 14028–14031 | Replace |
| M7 | `Core/Menu/menu.c` | 12958–12960 | Replace |
| M6 | `Core/Menu/menu.c` | 12916–12918 | Modify |
| M2 | `Core/Menu/menu.c` | after 11559 | Add |
| M5 | `Core/Menu/menu.c` | 6655–6659 | Modify |
| M4 | `Core/Menu/menu.c` | 6397–6400 | Replace |
| M3 | `Core/Menu/menu.c` | 3327–3329 | Replace |
| M1 | `Core/Menu/menu.c` | after 2040 | Add |
| X5 | `tools/dsp_test/DSP_TEST.md` | §3 table, §5 table | Add rows |
| S1–S6 | specifications | §8 | Modify / Add |

---

## 4. Firmware changes

### 4.1 `Core/DSPAudio/Oscillator.h`

#### O-H1 — Add after line 149 (`void osc_recalcFreq(OscInfo* osc);`), before line 151 (`#endif /* OSCILLATOR_H_ */`)

```c
//-----------------------------------------------------------
/*
 * Drum osc2 mod modes snc (hard sync) and rng (ring): oscillator side
 * (S085 P1).
 *
 * What:       OSC_SYNC_NONE; the sync clock (osc_syncClockInc,
 *             osc_buildSyncList); the sync and ring dispatchers
 *             (calcNextOscSampleSyncBlock, calcNextOscSampleRingBlock); and
 *             the amount mappings (osc_syncDepthQ30, osc_ringK). The
 *             dispatchers mirror calcNextOscSampleBlock(): the same
 *             waveform-interpolation entry, the same waveform switch and the
 *             same user-sample range check.
 * Why:        S085_P1_OSC_MOD_SNC_RNG.md §4; S085_P1_OSC_MOD_IMPLEMENTATION.md.
 *             The session rule is no increase in CPU. snc renders only the
 *             slave: the FM-page oscillator is a phase clock and is never
 *             rendered. rng folds the ring product into the carrier's own
 *             store instead of a separate pass. The existing plain and FM
 *             loops are not edited, so fm and mix compile unchanged.
 * Accessors:  Drum_calcVoiceSyncBlock() (DrumVoice.c) only.
 * Affiliates: Oscillator.c S085 P1 section; tools/dsp_test targets osc_mod,
 *             oscmod_identity, armcheck-oscmod, elf_osc_check.
 */

/*
 * "No reset after this sample" marker in a sync reset list (S085 P1).
 *
 * What:       over[i] == OSC_SYNC_NONE: the clock did not wrap after sample
 *             i. Any other value is the clock's overshoot past its wrap.
 *             That overshoot is always < the clock increment, so it can
 *             never equal this marker.
 * Accessors:  osc_buildSyncList() writes it; the sync copies in
 *             Oscillator.c read it.
 */
#define OSC_SYNC_NONE 0xFFFFFFFFu

/*
 * Sync clock increment for the FM-page (master) oscillator (S085 P1).
 *
 * What:       master->phaseInc, in "2^32 per nominal period" units.
 *             - Sine, table and noise phases already are.
 *             - Crash and user samples (a <<17 point index; 1,024 points per
 *               nominal period) are scaled << 5. They are first clamped to
 *               0x07FFFFFF so the shift cannot overflow; a master at or above
 *               fs then resets the slave on every sample.
 * Why:        plan A1/A4: in P1 every master is a clock at its frequency.
 *             Crash and user samples act as noise (one reset per nominal
 *             period), not as their audio.
 * Inputs:     master (read only). Its phaseInc is current:
 *             Drum_calcVoiceAsync() runs osc_setFreq() on both Drum
 *             oscillators every block, in every mode.
 * Outputs:    the clock increment.
 * Accessors:  Drum_calcVoiceSyncBlock().
 * Affiliates: osc_buildSyncList(); calcNextOscSampleSyncBlock() (the ratio).
 */
uint32_t osc_syncClockInc(const OscInfo* master);

/*
 * Build one block's sync reset list (S085 P1).
 *
 * What:       advances clock by clockInc once per sample. When the addition
 *             wraps (the clock passed 2^32 between samples i and i + 1),
 *             over[i] = the new clock value: the overshoot past the wrap.
 *             Otherwise over[i] = OSC_SYNC_NONE.
 * Why:        plan §4.3.1 / Q11: one list per block serves every slave copy.
 *             Later master kinds (looped samples, wavetable frames, the
 *             zero-crossing bitmap; plan §12) add a builder, not new slave
 *             loops.
 * Inputs:     clock (the clock phase at block start), clockInc, over[size],
 *             size (<= OUTPUT_DMA_SIZE).
 * Outputs:    over[0..size-1]; returns the clock phase at block end (the
 *             caller stores it in DrumVoice.syncClock).
 * Cost:       constant per sample (an add, a compare, a select, a store).
 * Accessors:  Drum_calcVoiceSyncBlock().
 * Affiliates: calcNextOscSampleSyncBlock(); OSC_SYNC_NONE.
 */
uint32_t osc_buildSyncList(uint32_t clock, const uint32_t clockInc,
                           uint32_t* over, const uint8_t size);

/*
 * Render one slave block under hard sync (S085 P1).
 *
 * What:       renders osc at gain 1, exactly as calcNextOscSampleBlock()
 *             would. After sample i, when over[i] != OSC_SYNC_NONE, the
 *             advanced phase is pulled toward the reset target:
 *                 target = over[i] x ratio, ratio = osc->phaseInc / clockInc;
 *                 phase += depth x (signed)(target - phase).
 *             The reset therefore lands on sample i + 1 (plan K4).
 *             - Synced: Sin, Tri, Saw, Rec, Crash.
 *             - Rendered plain (no sync): Noi, user samples (plan A3/A4).
 *             - Waveform interpolation: both partners use the same list.
 * Why:        plan Q10: amt is the sync depth (0 free-running, 1 hard sync).
 *             Only the slave is heard.
 * Inputs:     osc (the carrier), over[size] from osc_buildSyncList(),
 *             clockInc (the same value the list was built with), depthQ30
 *             (osc_syncDepthQ30()), buf[size], size.
 * Outputs:    buf; osc->phase (and osc->output for the plain fallbacks).
 * Cost:       one divide per call (the ratio), then the waveform's sync
 *             copy: S085_P1_OSC_MOD_IMPLEMENTATION.md §1.
 * Accessors:  Drum_calcVoiceSyncBlock().
 * Affiliates: the sync copies and osc_renderSyncWave() in Oscillator.c.
 */
void calcNextOscSampleSyncBlock(OscInfo* osc, const uint32_t* over,
                                const uint32_t clockInc, const int32_t depthQ30,
                                int16_t* buf, const uint8_t size);

/*
 * Render one carrier block with the ring law applied (S085 P1).
 *
 * What:       renders osc exactly as calcNextOscSampleBlock() would at
 *             gain 1, but stores
 *                 buf[i] = SSAT16(osc_i x (ringK + modBuf[i]) >> 15)
 *             instead of osc_i x gain. With modBuf = modulator x amt and
 *             ringK = (1 - amt) x 32768, this is
 *                 carrier x (1 - amt) + carrier x modulator x amt
 *             (plan K5). Every waveform is supported, including noise, user
 *             samples and waveform interpolation.
 * Why:        D1: a separate ring pass made rng as expensive as fm or more
 *             with user-sample oscillators. Folded into the store, it costs
 *             the plain loop + 0..4 instructions per sample.
 * Inputs:     osc (the carrier), modBuf[size] (the modulator block at gain
 *             amt), buf[size], size, ringK (osc_ringK()).
 * Outputs:    buf; osc->phase / osc->output as the plain render.
 * Accessors:  Drum_calcVoiceSyncBlock().
 * Affiliates: the ring copies in Oscillator.c; the mix path (the same
 *             modBuf).
 */
void calcNextOscSampleRingBlock(OscInfo* osc, const int16_t* modBuf,
                                int16_t* buf, const uint8_t size,
                                const int32_t ringK);

/*
 * Sync depth from the Drum amount (S085 P1).
 *
 * What:       amount (fmModAmount, 0..1) as Q30: 0 -> 0 (no pull), 1 ->
 *             0x40000000 (exact hard sync). Out-of-range or non-positive
 *             input clamps.
 * Why:        Q30 makes both ends exact. depth 0 is bit-identical to the
 *             free-running slave, and depth 1 lands exactly on the target.
 * Inputs:     amount.  Outputs: depthQ30.
 * Accessors:  Drum_calcVoiceSyncBlock() (once per block).
 * Affiliates: osc_syncPull() in Oscillator.c.
 */
static inline int32_t osc_syncDepthQ30(const float amount)
{
	if (!(amount > 0.0f))
		return 0;
	if (amount >= 1.0f)
		return 0x40000000;
	return (int32_t)(amount * 1073741824.0f);
}

/*
 * Ring dry gain from the Drum amount (S085 P1).
 *
 * What:       K = round((1 - amount) x 32768), 0..32768. K = 32768 at
 *             amount 0, so rng at amount 0 is the carrier exactly (the
 *             modulator block is then all zeros). K = 0 at amount 1 gives
 *             the pure ring product.
 * Inputs:     amount (fmModAmount, 0..1).  Outputs: K.
 * Range:      K + modBuf lies in [-32768, 65535]; times an int16 carrier it
 *             stays below 2^31.
 * Accessors:  Drum_calcVoiceSyncBlock() (once per block).
 * Affiliates: osc_ringSample() in Oscillator.c.
 */
static inline int32_t osc_ringK(const float amount)
{
	if (!(amount > 0.0f))
		return 32768;
	if (amount >= 1.0f)
		return 0;
	return (int32_t)((1.0f - amount) * 32768.0f + 0.5f);
}
```

### 4.2 `Core/DSPAudio/Oscillator.c`

#### O-C1 — Add after line 913 (`};`, the end of `calcSampleOsc()`), before line 914 (` //------…` above `osc_calcSineFreqValue()`)

**Notes:**
- The read and interpolation lines of every copy are the plain loops' lines
  verbatim:
  - table: lines 664–673;
  - sine: 304–315;
  - crash: 872–885;
  - noise: 527–539;
  - user sample: 813–865;
  - interpolation: 211–236.
- Do not "tidy" them. The depth-0 and ring identity tests depend on it.

```c
//-----------------------------------------------------------
/* =========================================================================
** S085 P1: Drum osc2 mod modes snc (hard sync) and rng (ring)
** =========================================================================
**
** What:       the sync clock and reset-list builder; sync and ring copies of
**             the plain block loops above; their two dispatchers. Each copy
**             repeats its plain loop's read and interpolation exactly. Only
**             the phase update (sync) or the final store (ring) differs.
** Why:        the existing loops must stay unchanged instruction for
**             instruction (fm and mix may not change cost or sound). So the
**             new behaviour lives in separate functions, not in extra
**             parameters on the shared loops: the S085 rollback's shared
**             plain/sync loops added cost to every plain render.
**             OSC_S085_LOOP keeps each copy out of line and unspecialised.
**             Each one then appears under its own name for fpseq.py and ITCM
**             accounting, and LTO cannot fold it into an existing loop.
** Accessors:  calcNextOscSampleSyncBlock(), calcNextOscSampleRingBlock(),
**             osc_buildSyncList(), osc_syncClockInc() (all called from
**             Drum_calcVoiceSyncBlock()).
** Affiliates: Oscillator.h S085 P1 block; DrumVoice.c; tools/dsp_test
**             (test_osc_mod.c, armcheck-oscmod, elf_osc_check).
*/
#if (INTERPOLATE_OSC != 1)
#error "S085 P1 sync/ring copies implement the interpolated read only (INTERPOLATE_OSC 1)"
#endif

#define OSC_S085_LOOP INITCM __attribute__((noinline, noclone))

/*
 * Apply one sync reset to an advanced slave phase (S085 P1).
 *
 * What:       target = over x ratioQ16 >> 16 is where the slave would be
 *             had it restarted at the clock's wrap instant (the overshoot
 *             correction, plan Q4). The result moves phase toward target by
 *             depthQ30 (0 = no move, 1 << 30 = exactly target), along the
 *             signed (short) way round the 32-bit phase circle.
 *             over == OSC_SYNC_NONE returns phase unchanged.
 * Why:        plan Q10 (amt = sync depth), K6 (the arithmetic runs on every
 *             sample; only the select depends on the list).
 * Inputs:     phase, already advanced by phaseInc (so the reset lands on the
 *             next sample, K4); over (the list entry); ratioQ16; depthQ30.
 * Outputs:    the next phase.
 * Range:      over < the clock increment, so target is below the slave
 *             increment (a clamped ratio only lowers it). |diff x depthQ30|
 *             < 2^61.
 * Accessors:  calcSineSyncBlock(), calcWavetableOscSyncBlock(),
 *             calcSampleOscSyncBlock(); tools/dsp_test osc_mod (S2, S3).
 */
static inline __attribute__((always_inline)) uint32_t osc_syncPull(
	const uint32_t phase, const uint32_t over, const uint32_t ratioQ16,
	const int32_t depthQ30)
{
	const uint32_t target = (uint32_t)(((uint64_t)over * ratioQ16) >> 16);
	const int32_t diff = (int32_t)(target - phase);
	const uint32_t pulled = phase +
		(uint32_t)(int32_t)(((int64_t)diff * depthQ30) >> 30);

	return (over != OSC_SYNC_NONE) ? pulled : phase;
}

/*
 * The ring law for one sample (S085 P1).
 *
 * What:       SSAT16((car x (ringK + mod)) >> 15). With mod = modulator x
 *             amt and ringK = (1 - amt) x 32768 this is car x (1 - amt) +
 *             car x modulator x amt (plan K5). The saturation catches the
 *             single full-scale case (-32768 x -32768 at amt 1).
 * Inputs:     car (the carrier sample), mod (the modulator block sample),
 *             ringK.  Outputs: the stored sample.
 * Accessors:  the ring copies below; tools/dsp_test osc_mod (R1).
 */
static inline __attribute__((always_inline)) int16_t osc_ringSample(
	const int16_t car, const int16_t mod, const int32_t ringK)
{
	return (int16_t)__SSAT(((int32_t)car * (ringK + (int32_t)mod)) >> 15, 16);
}

/*
 * Slave-to-clock increment ratio in Q16.16, once per block (S085 P1).
 *
 * What:       slaveInc / clockInc x 65536, clamped below 2^32. clockInc 0
 *             gives 0 (the builder then produces no resets anyway).
 * Why:        it turns a clock overshoot into slave phase units (plan
 *             §4.3.2). Float: one VDIV per block (about 0.5 cycles per
 *             sample) instead of a 64-bit integer divide.
 * Inputs:     slaveInc, clockInc.  Outputs: ratioQ16.
 * Accessors:  calcNextOscSampleSyncBlock(); tools/dsp_test osc_mod (M1).
 */
static uint32_t osc_syncRatioQ16(const uint32_t slaveInc, const uint32_t clockInc)
{
	float ratio;

	if (clockInc == 0u)
		return 0u;
	ratio = ((float)slaveInc / (float)clockInc) * 65536.0f;
	if (ratio > 4294967040.0f)
		ratio = 4294967040.0f;
	return (uint32_t)ratio;
}

/*
 * Whether a waveform has a sync copy (S085 P1).
 *
 * What:       1 for SINE, TRI, SAW, REC and CRASH; 0 for NOISE and user
 *             samples, which render unsynced in P1 (plan A3/A4).
 * Accessors:  calcPeriodicInterpSyncBlock() (the phase rule).
 * Affiliates: osc_renderSyncWave() (the same partition, as a switch).
 */
static inline uint8_t osc_syncWaveform(const uint8_t waveform)
{
	return (uint8_t)((waveform <= CRASH) && (waveform != NOISE));
}

/*
 * Sync clock increment for the FM-page oscillator (S085 P1).
 * Contract: Oscillator.h. Crash and user samples run in <<17 point units
 * (osc_calcSampleFreqValue: f/fs x 2^27), which is 1/32 of the 2^32-per-
 * period clock, hence << 5 after the clamp.
 */
uint32_t osc_syncClockInc(const OscInfo* master)
{
	uint32_t inc = master->phaseInc;

	if (master->waveform >= CRASH) {
		if (inc > 0x07FFFFFFu)
			inc = 0x07FFFFFFu;
		inc <<= 5;
	}
	return inc;
}

/*
 * Build one block's sync reset list (S085 P1).
 * Contract: Oscillator.h. `next < clock` is the carry of the 32-bit add,
 * and next is then the overshoot past the wrap (always < clockInc).
 */
OSC_S085_LOOP uint32_t osc_buildSyncList(uint32_t clock, const uint32_t clockInc,
                                         uint32_t* over, const uint8_t size)
{
	uint8_t i;

	for (i = 0u; i < size; i++) {
		const uint32_t next = clock + clockInc;

		over[i] = (next < clock) ? next : OSC_SYNC_NONE;
		clock = next;
	}
	return clock;
}

/*
 * Sine slave under sync (S085 P1).
 *
 * What:       calcSineBlock()'s read and interpolation (4,096-point table,
 *             <<20 phase); the store is oscOut (gain 1, D3); the phase
 *             update goes through osc_syncPull().
 * Inputs:     osc, over[size], ratioQ16, depthQ30, buf[size], size.
 * Outputs:    buf; osc->phase.
 * Accessors:  osc_renderSyncWave().
 * Affiliates: calcSineBlock() (the reference; must stay identical in its
 *             read), tools/dsp_test osc_mod S1/S2.
 */
static OSC_S085_LOOP void calcSineSyncBlock(OscInfo* osc, const uint32_t* over,
                                            const uint32_t ratioQ16,
                                            const int32_t depthQ30,
                                            int16_t* buf, const uint8_t size)
{
	uint8_t i;
	for(i=0;i<size;i++)
	{
		const uint32_t oscPhase = osc->phase;
		int16_t oscOut;

		const uint32_t index =  oscPhase;
		uint32_t  itg	= index>>20;

		oscOut = sine_table[itg++];
		const float frac = (index & 0xfffff) * 0.00000095367431640625f;
		oscOut += frac*(sine_table[itg] - oscOut);

		osc->phase = osc_syncPull(oscPhase + osc->phaseInc, over[i],
		                          ratioQ16, depthQ30);

		buf[i] = oscOut;
	}
}

/*
 * Tri/Saw/Rec slave under sync (S085 P1).
 *
 * What:       calcWavetableOscBlock()'s read and interpolation (1,024-point
 *             band-limited table at osc->tableOffset, <<22 phase); the
 *             store is oscOut (gain 1, D3); the phase update goes through
 *             osc_syncPull().
 * Inputs:     osc, table (sawTable / triTable / recTable), over[size],
 *             ratioQ16, depthQ30, buf[size], size.
 * Outputs:    buf; osc->phase.
 * Accessors:  osc_renderSyncWave().
 * Affiliates: calcWavetableOscBlock(); tools/dsp_test osc_mod S1/S2.
 */
static OSC_S085_LOOP void calcWavetableOscSyncBlock(OscInfo* osc,
                                                    const int16_t table[][1024],
                                                    const uint32_t* over,
                                                    const uint32_t ratioQ16,
                                                    const int32_t depthQ30,
                                                    int16_t* buf,
                                                    const uint8_t size)
{
	uint8_t i;
	for(i=0;i<size;i++)
	{
		const uint32_t oscPhase = osc->phase;
		int16_t oscOut ;
		uint32_t  itg	= oscPhase>>22;
		const uint32_t next = (itg + 1u) & 0x000003FFu;
		oscOut = table[osc->tableOffset][itg];
		const float frac = (oscPhase & 0x003FFFFF) * 2.384185791015625e-07f; // 1 / 2^22
		oscOut += frac*(table[osc->tableOffset][next] - oscOut);

		osc->phase = osc_syncPull(oscPhase + osc->phaseInc, over[i],
		                          ratioQ16, depthQ30);
		buf[i] = oscOut;
	}
}

/*
 * Crash slave under sync (S085 P1).
 *
 * What:       calcSampleOscBlock()'s read (32,768-point 8-bit crashSample,
 *             <<17 phase, (x - 127) x 256); the store is oscOut (gain 1,
 *             D3); the phase update goes through osc_syncPull(). A hard
 *             reset restarts the cymbal read near its start.
 * Note:       it reads crashSample[itg + 1] at itg = 32767, exactly as the
 *             plain loop does (unchanged behaviour, kept for identity).
 * Inputs:     osc, over[size], ratioQ16, depthQ30, buf[size], size.
 * Outputs:    buf; osc->phase.
 * Accessors:  osc_renderSyncWave().
 * Affiliates: calcSampleOscBlock(); plan A2/Q12 (crash slave syncs).
 */
static OSC_S085_LOOP void calcSampleOscSyncBlock(OscInfo* osc, const uint32_t* over,
                                                 const uint32_t ratioQ16,
                                                 const int32_t depthQ30,
                                                 int16_t* buf, const uint8_t size)
{
	uint8_t i;
	for(i=0;i<size;i++)
	{
		const uint32_t oscPhase = osc->phase;
		int16_t oscOut ;
		uint32_t  itg	= oscPhase>>17;

		oscOut = crashSample[itg++];
		const float frac = (oscPhase & 0x0001FFFF) * 0.00000762939453125f; // 1 / 2^17
		oscOut += frac*(crashSample[itg] - oscOut);
		oscOut = (oscOut - 127)*256;

		osc->phase = osc_syncPull(oscPhase + osc->phaseInc, over[i],
		                          ratioQ16, depthQ30);
		buf[i] = oscOut;
	}
}

/*
 * One waveform's sync render, without the interpolation check (S085 P1).
 *
 * What:       dispatches Sin/Tri/Saw/Rec/Crash to their sync copies. NOISE
 *             and user samples render through calcNextOscSampleBlock() at
 *             gain 1, unsynced (plan A3/A4). That call repeats its own
 *             interpolation check, which is false here: the caller already
 *             took the interpolation path, or the partner copy has
 *             interpolation cleared.
 * Inputs:     osc, over[size], ratioQ16, depthQ30, buf[size], size.
 * Outputs:    buf; osc state.
 * Accessors:  calcNextOscSampleSyncBlock(); calcPeriodicInterpSyncBlock().
 * Affiliates: osc_syncWaveform() (the same partition).
 */
static INITCM void osc_renderSyncWave(OscInfo* osc, const uint32_t* over,
                                      const uint32_t ratioQ16,
                                      const int32_t depthQ30,
                                      int16_t* buf, const uint8_t size)
{
	switch(osc->waveform)
	{
	case SINE:
		calcSineSyncBlock(osc, over, ratioQ16, depthQ30, buf, size);
		break;
	case SAW:
		calcWavetableOscSyncBlock(osc, sawTable, over, ratioQ16, depthQ30, buf, size);
		break;
	case TRI:
		calcWavetableOscSyncBlock(osc, triTable, over, ratioQ16, depthQ30, buf, size);
		break;
	case REC:
		calcWavetableOscSyncBlock(osc, recTable, over, ratioQ16, depthQ30, buf, size);
		break;
	case CRASH:
		calcSampleOscSyncBlock(osc, over, ratioQ16, depthQ30, buf, size);
		break;
	default:	/* NOISE and user samples: unsynced in P1 */
		calcNextOscSampleBlock(osc, buf, size, 1.f);
		break;
	}
}

/*
 * Waveform-interpolated slave under sync (S085 P1).
 *
 * What:       calcPeriodicInterpBlock() with each partner rendered through
 *             osc_renderSyncWave() against the same reset list, so both
 *             partners reset on the same samples. The blend is the plain
 *             one at gain 1.
 *             Phase rule: when the base partner has a sync copy, osc->phase
 *             takes the base partner's (synced) phase. Otherwise it takes
 *             the plain rule, start + phaseInc x size.
 * Why:        plan Q5: `wav` behaves the same in every mode. Cost is bounded
 *             by the existing two-unit interpolation limit
 *             (OSC_WAVE_INTERP_MAX_ACTIVE).
 * Inputs:     osc (interpolation active), over[size], ratioQ16, depthQ30,
 *             buf[size], size.
 * Outputs:    buf; osc->phase.
 * Placement:  no INITCM attribute, like calcPeriodicInterpBlock(); the
 *             compiler inlines it into its ITCM dispatcher, as it does
 *             calcPeriodicInterpBlock() into calcNextOscSampleBlock().
 * Accessors:  calcNextOscSampleSyncBlock().
 * Affiliates: calcPeriodicInterpBlock(); osc_interp_a/osc_interp_b scratch.
 */
static void calcPeriodicInterpSyncBlock(OscInfo* osc, const uint32_t* over,
                                        const uint32_t ratioQ16,
                                        const int32_t depthQ30,
                                        int16_t* buf, const uint8_t size)
{
	const uint8_t base = osc->waveform;
	const uint8_t next = osc->waveInterpNext;
	const float frac = osc->waveInterpFrac;
	const uint32_t startPhase = osc->phase;
	OscInfo oscA = *osc;
	OscInfo oscB = *osc;
	uint8_t i;

	oscA.waveform = base;
	osc_clearWaveInterp(&oscA);
	oscB.waveform = next;
	osc_clearWaveInterp(&oscB);

	osc_renderSyncWave(&oscA, over, ratioQ16, depthQ30, osc_interp_a, size);
	osc_renderSyncWave(&oscB, over, ratioQ16, depthQ30, osc_interp_b, size);

	for (i = 0; i < size; i++) {
		const int16_t a = osc_interp_a[i];
		const int16_t b = osc_interp_b[i];
		buf[i] = (int16_t)(a + frac * (b - a));
	}
	osc->phase = osc_syncWaveform(base) ? oscA.phase
	                                    : startPhase + (osc->phaseInc * (uint32_t)size);
}

/*
 * Render one slave block under hard sync (S085 P1).
 * Contract: Oscillator.h. Order: ratio (one VDIV), then the interpolation
 * entry (as calcNextOscSampleBlock()), then the waveform switch.
 */
INITCM void calcNextOscSampleSyncBlock(OscInfo* osc, const uint32_t* over,
                                       const uint32_t clockInc,
                                       const int32_t depthQ30,
                                       int16_t* buf, const uint8_t size)
{
	const uint32_t ratioQ16 = osc_syncRatioQ16(osc->phaseInc, clockInc);

	if (osc_waveInterpActive(osc)) {
		calcPeriodicInterpSyncBlock(osc, over, ratioQ16, depthQ30, buf, size);
		return;
	}
	osc_renderSyncWave(osc, over, ratioQ16, depthQ30, buf, size);
}

/*
 * Sine carrier with the ring law (S085 P1).
 *
 * What:       calcSineBlock() with the store replaced by
 *             osc_ringSample(oscOut, modBuf[i], ringK).
 * Inputs:     osc, modBuf[size], buf[size], size, ringK.
 * Outputs:    buf; osc->phase.
 * Accessors:  calcNextOscSampleRingBlock().
 * Affiliates: calcSineBlock(); tools/dsp_test osc_mod R1.
 */
static OSC_S085_LOOP void calcSineRingBlock(OscInfo* osc, const int16_t* modBuf,
                                            int16_t* buf, const uint8_t size,
                                            const int32_t ringK)
{
	uint8_t i;
	for(i=0;i<size;i++)
	{
		const uint32_t oscPhase = osc->phase;
		int16_t oscOut;

		const uint32_t index =  oscPhase;
		uint32_t  itg	= index>>20;

		oscOut = sine_table[itg++];
		const float frac = (index & 0xfffff) * 0.00000095367431640625f;
		oscOut += frac*(sine_table[itg] - oscOut);

		osc->phase = oscPhase + osc->phaseInc;

		buf[i] = osc_ringSample(oscOut, modBuf[i], ringK);
	}
}

/*
 * Tri/Saw/Rec carrier with the ring law (S085 P1).
 *
 * What:       calcWavetableOscBlock() with the store replaced by
 *             osc_ringSample(oscOut, modBuf[i], ringK).
 * Inputs:     osc, table, modBuf[size], buf[size], size, ringK.
 * Outputs:    buf; osc->phase.
 * Accessors:  calcNextOscSampleRingBlock().
 * Affiliates: calcWavetableOscBlock(); tools/dsp_test osc_mod R1.
 */
static OSC_S085_LOOP void calcWavetableOscRingBlock(OscInfo* osc,
                                                    const int16_t table[][1024],
                                                    const int16_t* modBuf,
                                                    int16_t* buf,
                                                    const uint8_t size,
                                                    const int32_t ringK)
{
	uint8_t i;
	for(i=0;i<size;i++)
	{
		const uint32_t oscPhase = osc->phase;
		int16_t oscOut ;
		uint32_t  itg	= oscPhase>>22;
		const uint32_t next = (itg + 1u) & 0x000003FFu;
		oscOut = table[osc->tableOffset][itg];
		const float frac = (oscPhase & 0x003FFFFF) * 2.384185791015625e-07f; // 1 / 2^22
		oscOut += frac*(table[osc->tableOffset][next] - oscOut);

		osc->phase = oscPhase + osc->phaseInc;
		buf[i] = osc_ringSample(oscOut, modBuf[i], ringK);
	}
}

/*
 * Noise carrier with the ring law (S085 P1).
 *
 * What:       calcNoiseBlock(): a new hardware-RNG value on each phase wrap,
 *             held between wraps; the store is replaced by
 *             osc_ringSample(osc->output, modBuf[i], ringK).
 * Inputs:     osc, modBuf[size], buf[size], size, ringK.
 * Outputs:    buf; osc->phase; osc->output.
 * Accessors:  calcNextOscSampleRingBlock().
 * Affiliates: calcNoiseBlock(); GetRngValue() (random.c).
 */
static OSC_S085_LOOP void calcNoiseRingBlock(OscInfo* osc, const int16_t* modBuf,
                                             int16_t* buf, const uint8_t size,
                                             const int32_t ringK)
{
	int i;
	for(i=0;i<size;i++)
	{
		const uint32_t lastPhase = osc->phase;
		osc->phase += osc->phaseInc;

		if(lastPhase > osc->phase)
		{
			//overflow happened -> phaseWrapped
			osc->output = (int16_t)(GetRngValue()); //normal pitched white noise
		}

		buf[i] = osc_ringSample(osc->output, modBuf[i], ringK);
	}
}

/*
 * Crash carrier with the ring law (S085 P1).
 *
 * What:       calcSampleOscBlock() with the store replaced by
 *             osc_ringSample(oscOut, modBuf[i], ringK).
 * Inputs:     osc, modBuf[size], buf[size], size, ringK.
 * Outputs:    buf; osc->phase.
 * Accessors:  calcNextOscSampleRingBlock().
 * Affiliates: calcSampleOscBlock().
 */
static OSC_S085_LOOP void calcSampleOscRingBlock(OscInfo* osc, const int16_t* modBuf,
                                                 int16_t* buf, const uint8_t size,
                                                 const int32_t ringK)
{
	uint8_t i;
	for(i=0;i<size;i++)
	{
		const uint32_t oscPhase = osc->phase;
		int16_t oscOut ;
		uint32_t  itg	= oscPhase>>17;

		oscOut = crashSample[itg++];
		const float frac = (oscPhase & 0x0001FFFF) * 0.00000762939453125f; // 1 / 2^17
		oscOut += frac*(crashSample[itg] - oscOut);
		oscOut = (oscOut - 127)*256;

		osc->phase = oscPhase + osc->phaseInc;
		buf[i] = osc_ringSample(oscOut, modBuf[i], ringK);
	}
}

/*
 * User-sample carrier with the ring law (S085 P1).
 *
 * What:       calcUserSampleOscBlock() (the sample cache, one-shot hold,
 *             looped wrap, interpolation across the loop seam) with the
 *             store replaced by osc_ringSample(oscOut, modBuf[i], ringK).
 *             Like the plain loop, an empty sample returns without writing
 *             buf (unchanged behaviour, kept for parity).
 * Inputs:     osc (waveform >= OSC_SAMPLE_START), modBuf[size], buf[size],
 *             size, ringK.
 * Outputs:    buf; osc->phase; osc sample cache.
 * Accessors:  calcNextOscSampleRingBlock().
 * Affiliates: calcUserSampleOscBlock(); osc_updateSampleCache();
 *             SampleMemory.
 */
static OSC_S085_LOOP void calcUserSampleOscRingBlock(OscInfo* osc,
                                                     const int16_t* modBuf,
                                                     int16_t* buf,
                                                     const uint8_t size,
                                                     const int32_t ringK)
{
	uint8_t sampleIndex = osc->waveform - OSC_SAMPLE_START;
	osc_updateSampleCache(osc, sampleIndex);
	uint32_t sampleSize = osc->sampleCacheSize;
	uint8_t looped = osc->sampleCacheLooped;

	//cast sample data to signed int16_t array
	const int16_t* sampleData = osc->sampleCacheData;
	if(sampleSize == 0) return;

	uint8_t i;
	for(i=0;i<size;i++)
	{
		const uint32_t oscPhase = osc->phase;
		int16_t oscOut ;
		uint32_t  itg	= oscPhase>>17;

		if(looped && itg >= sampleSize)
			itg %= sampleSize;

		oscOut = sampleData[itg];
		const float frac = (oscPhase & 0x0001FFFF) * 0.00000762939453125f; // 1 / 2^17
		if(looped && (itg + 1u) >= sampleSize)
			oscOut += frac*(sampleData[0] - oscOut);
		else
			oscOut += frac*(sampleData[itg + 1u] - oscOut);
		itg++;

		//one shot
		if(looped)
		{
			uint32_t nextPhase = oscPhase + osc->phaseInc;
			if(sampleSize < 32768u) {
				uint32_t loopPhase = sampleSize << 17;
				if(nextPhase >= loopPhase)
					nextPhase %= loopPhase;
			}
			osc->phase = nextPhase;
		}
		else if(itg < sampleSize)
		{
			osc->phase = oscPhase + osc->phaseInc;
		}
		buf[i] = osc_ringSample(oscOut, modBuf[i], ringK);
	}
}

/*
 * Waveform-interpolated carrier with the ring law (S085 P1).
 *
 * What:       calcPeriodicInterpBlock(): both partners rendered plain at
 *             gain 1 into the DTCM scratch, then blended. The blend's store
 *             is replaced by osc_ringSample(blend, modBuf[i], ringK). The
 *             phase follows the plain rule (start + phaseInc x size).
 * Placement:  no INITCM attribute, like calcPeriodicInterpBlock(); the
 *             compiler inlines it into its ITCM dispatcher, as it does
 *             calcPeriodicInterpBlock() into calcNextOscSampleBlock().
 * Inputs:     osc (interpolation active), modBuf[size], buf[size], size,
 *             ringK.
 * Outputs:    buf; osc->phase.
 * Accessors:  calcNextOscSampleRingBlock().
 * Affiliates: calcPeriodicInterpBlock(); OSC_WAVE_INTERP_MAX_ACTIVE.
 */
static void calcPeriodicInterpRingBlock(OscInfo* osc, const int16_t* modBuf,
                                        int16_t* buf, const uint8_t size,
                                        const int32_t ringK)
{
	const uint8_t base = osc->waveform;
	const uint8_t next = osc->waveInterpNext;
	const float frac = osc->waveInterpFrac;
	const uint32_t startPhase = osc->phase;
	OscInfo oscA = *osc;
	OscInfo oscB = *osc;
	uint8_t i;

	oscA.waveform = base;
	osc_clearWaveInterp(&oscA);
	oscB.waveform = next;
	osc_clearWaveInterp(&oscB);

	calcNextOscSampleBlock(&oscA, osc_interp_a, size, 1.f);
	calcNextOscSampleBlock(&oscB, osc_interp_b, size, 1.f);

	for (i = 0; i < size; i++) {
		const int16_t a = osc_interp_a[i];
		const int16_t b = osc_interp_b[i];
		const int16_t out = (int16_t)(a + frac * (b - a));
		buf[i] = osc_ringSample(out, modBuf[i], ringK);
	}
	osc->phase = startPhase + (osc->phaseInc * (uint32_t)size);
}

/*
 * Render one carrier block with the ring law (S085 P1).
 * Contract: Oscillator.h. It mirrors calcNextOscSampleBlock(), including the
 * user-sample range check that returns without writing buf.
 */
INITCM void calcNextOscSampleRingBlock(OscInfo* osc, const int16_t* modBuf,
                                       int16_t* buf, const uint8_t size,
                                       const int32_t ringK)
{
	if (osc_waveInterpActive(osc)) {
		calcPeriodicInterpRingBlock(osc, modBuf, buf, size, ringK);
		return;
	}

	switch(osc->waveform)
	{
	case SINE:
		calcSineRingBlock(osc, modBuf, buf, size, ringK);
		break;
	case SAW:
		calcWavetableOscRingBlock(osc, sawTable, modBuf, buf, size, ringK);
		break;
	case TRI:
		calcWavetableOscRingBlock(osc, triTable, modBuf, buf, size, ringK);
		break;
	case REC:
		calcWavetableOscRingBlock(osc, recTable, modBuf, buf, size, ringK);
		break;
	case NOISE:
		calcNoiseRingBlock(osc, modBuf, buf, size, ringK);
		break;
	case CRASH:
		calcSampleOscRingBlock(osc, modBuf, buf, size, ringK);
		break;
	default://sample playback
		if( osc->waveform - OSC_SAMPLE_START > sampleMemory_getNumSamples() ) return; // return if out of range

		calcUserSampleOscRingBlock(osc, modBuf, buf, size, ringK);
		break;
	}
}
```

### 4.3 `Core/DSP/Instruments/Drum/DrumVoice.h`

#### D-H2 — Modify lines 103–105, then add after them

**Before:**
```c
#if ENABLE_MIX_OSC
	uint8_t mixOscs;
#endif
```

**After:**
```c
#if ENABLE_MIX_OSC
	/*
	 * osc2 mod mode, DRUM_MOD_FM .. DRUM_MOD_RNG (S085 P1; was the boolean
	 * mixOscs).
	 *
	 * What:       written by the descriptor row osc2_mod_type (TYPE_UINT8)
	 *             from the menu, Morph, step automation and MIDI; never by
	 *             an LFO (MOD_NONE). Read once per block by
	 *             Drum_calcVoiceSyncBlock()'s switch.
	 * Why:        renamed because the byte now holds four modes. A boolean
	 *             test (the old `if(voice->mixOscs)`) would play snc and rng
	 *             as mix.
	 * Accessors:  Drum_initVoice(), Drum_calcVoiceSyncBlock(), the
	 *             descriptor binding (DrumParameters.c).
	 */
	uint8_t modMode;
#endif

	/*
	 * snc phase clock (S085 P1, deviation D2).
	 *
	 * What:       the clock that resets the carrier in snc. It is advanced by
	 *             osc_buildSyncList() once per snc block, at
	 *             osc_syncClockInc(&modOsc) per sample. Zeroed by
	 *             Drum_initVoice(). It is not reset on a trigger: like
	 *             modOsc, it runs free (user, plan Q6b).
	 * Why:        modOsc is not rendered in snc. Advancing modOsc.phase
	 *             instead would push a one-shot user-sample phase past the
	 *             sample's end, and a later fm/mix render would read beyond
	 *             the sample (and, near the top of sample flash, beyond
	 *             flash).
	 * Inputs/Outputs: the 32-bit clock phase (2^32 per nominal period).
	 * Accessors:  Drum_calcVoiceSyncBlock(), Drum_initVoice().
	 * Affiliates: Oscillator.h osc_syncClockInc() / osc_buildSyncList();
	 *             InstrumentManager.c runtime-slot reserve (2 x
	 *             sizeof(DrumVoice), so +8 B per slot).
	 */
	uint32_t syncClock;
```

#### D-H1 — Add after line 56 (`};`, the end of the `IS_PLAYING` enum), before line 57

```c
/*
 * Drum osc2 mod modes: the stored values of osc2_mod_type (S085 P1).
 *
 * What:       DRUM_MOD_FM  0  the carrier phase-modulated by the modulator;
 *             DRUM_MOD_MIX 1  carrier x (1 - amt) + modulator x amt;
 *             DRUM_MOD_SNC 2  the carrier hard-synced to a phase clock at
 *                             the FM-page frequency, amt = sync depth; only
 *                             the carrier is heard;
 *             DRUM_MOD_RNG 3  carrier x (1 - amt) + carrier x modulator x
 *                             amt.
 * Why:        0 and 1 keep their stored meaning, so old kits load unchanged;
 *             2 and 3 are appended (plan K1). Any other byte renders as fm
 *             (plan Q7).
 * Accessors:  DrumVoice.modMode; Drum_calcVoiceSyncBlock(); Drum_initVoice().
 * Affiliates: DrumParameters.c row osc2_mod_type (DTYPE_MIX_FM); MenuText.h
 *             mixFmNames (same order, count = DRUM_MOD_COUNT); menu.c
 *             menu_getMixFmName().
 */
#define DRUM_MOD_FM    0u
#define DRUM_MOD_MIX   1u
#define DRUM_MOD_SNC   2u
#define DRUM_MOD_RNG   3u
#define DRUM_MOD_COUNT 4u
```

### 4.4 `Core/DSP/Instruments/Drum/DrumVoice.c`

#### D-C2 — Replace lines 313–326

**Before (313–326):**
```c
	//calc next mod osc sampleBlock
	calcNextOscSampleBlock(&voice->modOsc,modBuf,size,voice->fmModAmount);

	if(voice->mixOscs)
	{
		//calc main osc buffer
		calcNextOscSampleBlock(&voice->osc,buf,size, (1.f-voice->fmModAmount));
		//add mod buffer to main osc buffer
		bufferTool_addBuffersSaturating(buf,modBuf,size);
	}
	else
	{
		calcNextOscSampleFmBlock(&voice->osc,modBuf,buf,size,1.0f);
	}
```

**After:**
```c
	/*
	 * Oscillator section by osc2 mod mode (S085 P1).
	 *
	 * What:       fm and mix make exactly the calls they made before, in the
	 *             same order, with the same arguments.
	 *             - rng: the modulator block at gain amt, then the carrier
	 *               through its ring copy (carrier x (K + modBuf) >> 15).
	 *             - snc: the FM-page oscillator is not rendered. Its
	 *               frequency drives syncClock, the block's reset list is
	 *               built from that clock, and the carrier renders through
	 *               its sync copy at gain 1 with amt as the sync depth.
	 *             modBuf is still fully rewritten by transient_calcBlock()
	 *             below, so its contents in snc do not matter.
	 * Why:        S085_P1_OSC_MOD_SNC_RNG.md §4. The session rule: no mode may
	 *             cost more than today's dearest fm setting (implementation
	 *             §1).
	 * Inputs:     voice->modMode, fmModAmount, osc, modOsc, syncClock; size
	 *             <= OUTPUT_DMA_SIZE (as modBuf already assumes).
	 * Outputs:    buf (the oscillator section); modBuf (fm, mix, rng);
	 *             syncClock (snc).
	 * Accessors:  this render function, through InstrumentManager.
	 * Affiliates: Oscillator.h S085 P1 block; DrumVoice.h DRUM_MOD_*;
	 *             DrumParameters.c osc2_mod_type.
	 */
	switch (voice->modMode)
	{
	case DRUM_MOD_MIX:
		//calc next mod osc sampleBlock
		calcNextOscSampleBlock(&voice->modOsc,modBuf,size,voice->fmModAmount);
		//calc main osc buffer
		calcNextOscSampleBlock(&voice->osc,buf,size, (1.f-voice->fmModAmount));
		//add mod buffer to main osc buffer
		bufferTool_addBuffersSaturating(buf,modBuf,size);
		break;

	case DRUM_MOD_SNC:
	{
		uint32_t syncOver[OUTPUT_DMA_SIZE];
		const uint32_t clockInc = osc_syncClockInc(&voice->modOsc);

		voice->syncClock = osc_buildSyncList(voice->syncClock, clockInc,
		                                     syncOver, size);
		calcNextOscSampleSyncBlock(&voice->osc, syncOver, clockInc,
		                           osc_syncDepthQ30(voice->fmModAmount),
		                           buf, size);
		break;
	}

	case DRUM_MOD_RNG:
		//calc next mod osc sampleBlock (gain amt, as in mix)
		calcNextOscSampleBlock(&voice->modOsc,modBuf,size,voice->fmModAmount);
		calcNextOscSampleRingBlock(&voice->osc, modBuf, buf, size,
		                           osc_ringK(voice->fmModAmount));
		break;

	case DRUM_MOD_FM:
	default:
		//calc next mod osc sampleBlock
		calcNextOscSampleBlock(&voice->modOsc,modBuf,size,voice->fmModAmount);
		calcNextOscSampleFmBlock(&voice->osc,modBuf,buf,size,1.0f);
		break;
	}
```

#### D-C1 — Modify lines 140–142, then add after them

**Before:**
```c
#if ENABLE_MIX_OSC
	voice->mixOscs = true;
#endif
```

**After:**
```c
#if ENABLE_MIX_OSC
	voice->modMode = DRUM_MOD_MIX;	/* S085 P1: was mixOscs = true (1 = mix) */
#endif
	/*
	 * snc clock start (S085 P1).
	 * What: the clock phase begins at 0 on every runtime reset (Scene or
	 * type change); triggers do not touch it. Why: deterministic start
	 * after a reset; free-running afterwards like modOsc (plan Q6b).
	 * Accessors: Drum_calcVoiceSyncBlock().
	 */
	voice->syncClock = 0u;
```

### 4.5 `Core/DSP/Instruments/Drum/DrumParameters.c`

#### P1 — Modify line 192, with a comment block inserted above it

**Before:**
```c
    ROW("osc2_mod_type", "FM", "Mode", "mod", DTYPE_MIX_FM, MOD_NONE, mixOscs, TYPE_UINT8),
```

**After:**
```c
    /*
     * S085 P1: osc2 mod mode, stored 0 fm, 1 mix, 2 snc, 3 rng
     * (DRUM_MOD_*). The key, row index, automation ID and NRPN ID are
     * unchanged. DTYPE_MIX_FM's clamp, automation max and names come from
     * MenuText.h mixFmNames (menu.c menu_getMixFmName()). The bound member
     * was renamed mixOscs -> modMode.
     */
    ROW("osc2_mod_type", "FM", "Mode", "mod", DTYPE_MIX_FM, MOD_NONE, modMode, TYPE_UINT8),
```

### 4.6 `Core/Menu/MenuText.h`

#### T2 — Add after line 108 (`};`, the end of `lfoPolarityNames`)

```c
/*
 * Drum osc2 mod mode names (S085 P1).
 *
 * What:       [0] = count (4), then "fm ", "mix", "snc", "rng" in stored
 *             order (DrumVoice.h DRUM_MOD_FM .. DRUM_MOD_RNG).
 * Why:        DTYPE_MIX_FM grew from two values to four. One table replaces
 *             the two literals menuText_fm / menuText_mix, so the names,
 *             clamp and automation maximum share one count.
 * Accessors:  menu.c menu_getMixFmName(), menu_clampCellValue(),
 *             menu_automationValueMax().
 * Affiliates: DrumParameters.c osc2_mod_type; DrumVoice.h DRUM_MOD_COUNT.
 */
static const char mixFmNames[][4] = {
    {4}, {"fm "},{"mix"},{"snc"},{"rng"},
};
```

#### T1 — Remove lines 43–44

```c
static const char menuText_mix[]   = "mix";
static const char menuText_fm[]    = "fm ";
```

**Why:** after M3/M4/M7/M8 these two literals have no reader. The only uses
are the six `DTYPE_MIX_FM` sites in `menu.c`; `grep` confirms no other file
uses them.

### 4.7 `Core/Menu/menu.c`

#### M8 — Replace lines 14028–14031 (`menu_repaintGeneric()`)

**Before:**
```c
            case DTYPE_MIX_FM:
                if (value==1) memcpy(&editDisplayBuffer[1][13], menuText_mix, 3);
                else               memcpy(&editDisplayBuffer[1][13], menuText_fm, 3);
                break;
```

**After:**
```c
            case DTYPE_MIX_FM:
                /* S085 P1: fm / mix / snc / rng (menu_getMixFmName()). */
                menu_getMixFmName(value, &editDisplayBuffer[1][13]);
                break;
```

#### M7 — Replace lines 12958–12960 (`menu_formatAutomationValue3()`)

**Before:**
```c
    case DTYPE_MIX_FM:
        memcpy(buf, (value == 1u) ? menuText_mix : menuText_fm, 3);
        return;
```

**After:**
```c
    case DTYPE_MIX_FM:
        /* S085 P1: fm / mix / snc / rng (menu_getMixFmName()). */
        menu_getMixFmName(value, buf);
        return;
```

#### M6 — Modify lines 12916–12918 (`menu_automationValueMax()`)

**Before:**
```c
    case DTYPE_ON_OFF:
    case DTYPE_MIX_FM:
        return 1u;
```

**After:**
```c
    case DTYPE_ON_OFF:
        return 1u;
    case DTYPE_MIX_FM:
        /* S085 P1: osc2 mod mode 0..3; step automation may select snc/rng. */
        return (uint8_t)(mixFmNames[0][0] - 1u);
```

#### M2 — Add after line 11559 (the closing `}` of `menu_getLfoPolarityName()`)

```c

/*
 * Format the Drum osc2 mod mode (DTYPE_MIX_FM) (S085 P1).
 *
 * What:       writes "fm ", "mix", "snc" or "rng" for the stored values
 *             0..3. Any other byte shows "fm ", matching the render, whose
 *             switch defaults to fm (plan Q7).
 * Why:        the dtype grew from a two-value ternary to four values. One
 *             helper keeps the six display sites (compact cell, single
 *             parameter edit, value-automation search, automation value,
 *             repaint) identical.
 * Inputs:     value (the stored byte), buf (3 characters, not terminated).
 * Outputs:    buf[0..2].
 * Accessors:  va_formatValue3(), menu_formatCellValue3(),
 *             menu_formatAutomationValue3(), menu_repaintGeneric().
 * Affiliates: MenuText.h mixFmNames; DrumVoice.h DRUM_MOD_*;
 *             menu_clampCellValue() and menu_automationValueMax() (the same
 *             count).
 */
static void menu_getMixFmName(uint8_t value, char *buf)
{
    const uint8_t count = (uint8_t)mixFmNames[0][0];
    const char *p;

    if (value >= count)
        value = 0u;
    p = mixFmNames[value + 1u];
    buf[0] = p[0];
    buf[1] = p[1] ? p[1] : ' ';
    buf[2] = p[2] ? p[2] : ' ';
}

/* S085 P1: mixFmNames holds the count plus fm, mix, snc, rng (DRUM_MOD_COUNT). */
_Static_assert(sizeof(mixFmNames) / sizeof(mixFmNames[0]) == 5u,
               "mixFmNames must list the four Drum osc2 mod modes");
```

#### M5 — Modify lines 6655–6659 (`menu_clampCellValue()`)

**Before:**
```c
    case DTYPE_MIX_FM:
    case DTYPE_ON_OFF:
    case DTYPE_0b1:
        if (*value > 1u) *value = 1u;
        break;
```

**After:**
```c
    case DTYPE_MIX_FM:
        /*
         * S085 P1: osc2 mod mode 0..3. Menu edits and MIDI writes both pass
         * here (MIDI_SPEC.md §5.5), so CC/NRPN 3..127 select rng.
         */
        if (*value > (uint16_t)(mixFmNames[0][0] - 1u))
            *value = (uint16_t)(mixFmNames[0][0] - 1u);
        break;
    case DTYPE_ON_OFF:
    case DTYPE_0b1:
        if (*value > 1u) *value = 1u;
        break;
```

#### M4 — Replace lines 6397–6400 (`menu_formatCellValue3()`)

**Before:**
```c
    case DTYPE_MIX_FM:
        if (value == 1u) memcpy(valueAsText, menuText_mix, 3);
        else            memcpy(valueAsText, menuText_fm, 3);
        break;
```

**After:**
```c
    case DTYPE_MIX_FM:
        /* S085 P1: fm / mix / snc / rng (menu_getMixFmName()). */
        menu_getMixFmName(value, valueAsText);
        break;
```

#### M3 — Replace lines 3327–3329 (`va_formatValue3()`)

**Before:**
```c
    case DTYPE_MIX_FM:
        memcpy(out, (value == 1u) ? menuText_mix : menuText_fm, 3);
        break;
```

**After:**
```c
    case DTYPE_MIX_FM:
        /* S085 P1: fm / mix / snc / rng (menu_getMixFmName()). */
        menu_getMixFmName(value, out);
        break;
```

#### M1 — Add after line 2040 (`static void menu_getLfoPolarityName(uint8_t value, char *buf);`)

```c
static void menu_getMixFmName(uint8_t value, char *buf);	/* S085 P1 */
```

---

## 5. Test bench changes (`tools/dsp_test/`)

### X1 — New `tools/dsp_test/snapshot85.sh` (run once, before any firmware edit)

```sh
#!/bin/sh
# Freeze the pre-S085 P1 sources (S085 P1 step 1).
#
# What:       copies the files S085 P1 edits into tools/dsp_test/frozen85/,
#             preserving their relative paths.
# Why:        the identity gate (oscmod_identity) needs the exact pre-change
#             text. frozen/ holds the pre-S073 sources and must not be
#             refrozen (snapshot.sh refuses).
# Inputs:     the working tree at 1ea234ea, before any S085 P1 source edit.
#             Run from anywhere; it cds to the repository root.
# Outputs:    tools/dsp_test/frozen85/<same relative paths>.
# Accessors:  the implementer, once (S085_P1_OSC_MOD_IMPLEMENTATION.md §6
#             step 1).
# Affiliates: Makefile oscmod_identity, elf_osc_baseline (writes
#             frozen85/elf_osc.txt).
set -eu
cd "$(dirname "$0")/../.."
DEST=tools/dsp_test/frozen85
[ -e "$DEST" ] && { echo "snapshot exists: $DEST (delete it deliberately to refreeze)"; exit 1; }
for f in \
  Core/DSPAudio/Oscillator.c Core/DSPAudio/Oscillator.h \
  Core/DSPAudio/BufferTools.c Core/DSPAudio/BufferTools.h \
  Core/DSP/Instruments/Drum/DrumVoice.c Core/DSP/Instruments/Drum/DrumVoice.h \
  Core/DSP/Instruments/Drum/DrumParameters.c Core/Menu/MenuText.h
do
  mkdir -p "$DEST/$(dirname "$f")"
  cp "$f" "$DEST/$f"
done
echo "frozen $(find "$DEST" -type f | wc -l | tr -d ' ') files into $DEST"
```

### X2 — New `tools/dsp_test/elf_symdump.py`

```python
#!/usr/bin/env python3
"""Dump and compare normalised disassembly of firmware symbols (S085 P1 G-A).

What:       disassembles the linked firmware ELF and writes, for every
            function whose base name is listed (any .constprop/.isra/.part
            suffix included), its instructions with absolute addresses
            removed. --compare reports baseline symbols that are missing or
            changed, and new symbols.
Why:        S085 P1 rule: fm and mix may not change. After LTO the oscillator
            loops live in clones such as calcNextOscSampleBlock.constprop.0.
            Their normalised text before and after the change proves the
            compiled loops are identical even though their addresses moved.
            New clones are allowed (new-code callers) and are listed.
Inputs:     --elf PATH --sym NAME ... --out PATH, or --compare BASE NOW.
Outputs:    the dump file; with --compare, a report, and exit 1 when a
            baseline symbol is missing or changed.
Accessors:  tools/dsp_test/Makefile elf_osc_baseline, elf_osc_check.
Affiliates: fpseq.py (static loop counts); frozen85/elf_osc.txt.
"""
import argparse
import re
import subprocess
import sys

HEAD = re.compile(r'^[0-9a-f]+ <([^>]+)>:$')
ROW = re.compile(r'^\s*[0-9a-f]+:\s+(.*)$')
TARGET = re.compile(r'\b[0-9a-f]+ (<[^>]+>)')
WORD = re.compile(r'\b0x[0-9a-f]{8}\b|\b[0-9a-f]{8}\b')
SUFFIX = re.compile(r'(\.(constprop|isra|part|cold)\.\d+)+$')


def base(name):
    return SUFFIX.sub('', name)


def dump(elf, wanted):
    text = subprocess.run(['arm-none-eabi-objdump', '-d', '--no-show-raw-insn', elf],
                          check=True, capture_output=True, text=True).stdout
    out, cur = {}, None
    for line in text.splitlines():
        m = HEAD.match(line)
        if m:
            cur = m.group(1) if base(m.group(1)) in wanted else None
            if cur:
                out[cur] = []
            continue
        if cur is None:
            continue
        if not line.strip():
            cur = None
            continue
        r = ROW.match(line)
        if r:
            ins = TARGET.sub(r'\1', r.group(1))
            ins = WORD.sub('ADDR', ins)
            out[cur].append(re.sub(r'\s+', ' ', ins).strip())
    return out


def write(path, syms):
    with open(path, 'w') as f:
        for name in sorted(syms):
            f.write(f'== {name}\n')
            for ins in syms[name]:
                f.write(ins + '\n')


def read(path):
    syms, cur = {}, None
    with open(path) as f:
        for line in f:
            line = line.rstrip('\n')
            if line.startswith('== '):
                cur = line[3:]
                syms[cur] = []
            elif cur is not None:
                syms[cur].append(line)
    return syms


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--elf')
    ap.add_argument('--sym', action='append', default=[])
    ap.add_argument('--out')
    ap.add_argument('--compare', nargs=2)
    a = ap.parse_args()
    if a.compare:
        old, new = read(a.compare[0]), read(a.compare[1])
        missing = sorted(set(old) - set(new))
        changed = sorted(n for n in old if n in new and old[n] != new[n])
        added = sorted(set(new) - set(old))
        for n in missing:
            print(f'MISSING {n}')
        for n in changed:
            print(f'CHANGED {n}')
        for n in added:
            print(f'ADDED   {n} (new code only; check its callers)')
        print(f'elf_osc: {len(old)} baseline symbols, {len(missing)} missing, '
              f'{len(changed)} changed, {len(added)} added')
        return 1 if (missing or changed) else 0
    if not (a.elf and a.out and a.sym):
        ap.error('--elf, --out and at least one --sym are required')
    syms = dump(a.elf, set(a.sym))
    if not syms:
        sys.exit('elf_symdump.py: no listed symbol found')
    write(a.out, syms)
    print(f'elf_symdump: {len(syms)} symbols -> {a.out}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
```

### X3 — `tools/dsp_test/Makefile`

#### X3a — Modify lines 29–30 (`.PHONY`)

**Before:**
```make
.PHONY: selftest filter special_tags pack postchain octave mixer \
        armcheck-filter armcheck-postchain armcheck-mixer clean
```

**After:**
```make
.PHONY: selftest filter special_tags pack postchain octave mixer \
        armcheck-filter armcheck-postchain armcheck-mixer clean \
        oscmod_identity osc_mod armcheck-oscmod elf_osc_baseline elf_osc_check
```

#### X3b — Append after line 195 (the end of the file)

```make

# ---------------------------------------------------------------------------
# S085 P1: Drum snc / rng (S085_P1_OSC_MOD_IMPLEMENTATION.md §5, §6).
#
# What:       oscmod_identity  the pre-change oscillator/buffer source text is
#                              unchanged (only the S085 P1 block is added);
#             osc_mod          host behaviour of the new code (test_osc_mod.c);
#             armcheck-oscmod  per-sample instruction counts of the new
#                              loops beside today's plain/FM loops (G-B);
#             elf_osc_baseline / elf_osc_check
#                              normalised disassembly of every existing
#                              oscillator loop in the linked firmware,
#                              before and after (G-A).
# Why:        S085 P1 rule: no increase in CPU, and fm/mix unchanged.
# Inputs:     frozen85/ (snapshot85.sh), the current sources, build/lxr02.elf.
# Accessors:  make -C tools/dsp_test <target>.
# Affiliates: elf_symdump.py, fpseq.py, extract.py, prelude.h.
# ---------------------------------------------------------------------------
FROZEN85 := frozen85
FW_ELF   := $(ROOT)/build/lxr02.elf
FW_INC   := $(addprefix -I$(ROOT)/,$(patsubst -I%,%,$(filter -I%, \
            $(shell sed -n '/^CFLAGS/,/^$$/p' $(ROOT)/Makefile))))
OSC_ELF_SYMS := calcSineBlock calcFmSineBlock calcFmBlock calcWavetableOscBlock \
                calcNoiseBlock calcSampleOscBlock calcSampleOscFmBlock \
                calcUserSampleOscBlock calcUserSampleOscFmBlock \
                calcNextOscSampleBlock calcNextOscSampleFmBlock \
                calcPeriodicInterpBlock calcPeriodicInterpFmBlock osc_setFreq \
                bufferTool_addBuffersSaturating
OSC_PLAIN := freq2PhaseIncr1024 freq2PhaseIncr32767 osc_clearWaveInterp \
             osc_updateSampleCache osc_waveInterpActive calcSineBlock \
             calcWavetableOscBlock calcNoiseBlock calcSampleOscBlock \
             calcUserSampleOscBlock calcPeriodicInterpBlock calcNextOscSampleBlock
OSC_S085  := osc_syncPull osc_ringSample osc_syncRatioQ16 osc_syncWaveform \
             osc_syncClockInc osc_buildSyncList calcSineSyncBlock \
             calcWavetableOscSyncBlock calcSampleOscSyncBlock osc_renderSyncWave \
             calcPeriodicInterpSyncBlock calcNextOscSampleSyncBlock \
             calcSineRingBlock calcWavetableOscRingBlock calcNoiseRingBlock \
             calcSampleOscRingBlock calcUserSampleOscRingBlock \
             calcPeriodicInterpRingBlock calcNextOscSampleRingBlock

# G-A source half: nothing removed or changed in Oscillator.c/.h (lines may
# only be added); BufferTools.c/.h byte-identical.
oscmod_identity:
	@for f in Core/DSPAudio/Oscillator.c Core/DSPAudio/Oscillator.h; do \
	  n=$$(diff $(FROZEN85)/$$f $(ROOT)/$$f | grep -c '^<' || true); \
	  echo "$$f: removed or changed lines: $$n"; test "$$n" -eq 0 || exit 1; \
	done
	cmp $(FROZEN85)/Core/DSPAudio/BufferTools.c $(ROOT)/Core/DSPAudio/BufferTools.c
	cmp $(FROZEN85)/Core/DSPAudio/BufferTools.h $(ROOT)/Core/DSPAudio/BufferTools.h
	@echo "oscmod_identity: OK"

# Host behaviour of the new code against the unchanged plain loops.
osc_mod: | $(GEN)
	$(EX) --src $(ROOT)/Core/DSPAudio/Oscillator.h --lines 49-55 --out $(GEN)/om_defs.inc
	$(EX) --src $(ROOT)/Core/DSPAudio/Oscillator.h --lines 61-92 --out $(GEN)/om_oscinfo.inc
	$(EX) --src $(ROOT)/Core/SampleRom/SampleMemory.h --lines 104-109 \
	  --out $(GEN)/om_sampleinfo.inc
	$(EX) --src $(ROOT)/Core/DSPAudio/Oscillator.c --lines 123-124 --out $(GEN)/om_scratch.inc
	$(EX) --src $(ROOT)/Core/DSPAudio/Oscillator.h --func osc_syncDepthQ30 \
	  --func osc_ringK --out $(GEN)/om_map.c
	$(EX) --src $(ROOT)/Core/DSPAudio/Oscillator.c $(call fn,$(OSC_PLAIN)) \
	  --out $(GEN)/om_plain.c
	$(EX) --src $(ROOT)/Core/DSPAudio/Oscillator.c $(call fn,$(OSC_S085)) \
	  --out $(GEN)/om_s085.c
	$(HOSTCC) $(HOSTFLAGS) test_osc_mod.c -o build/osc_mod -lm
	./build/osc_mod

# G-B: static per-sample counts, new loops beside the loops they replace.
# Sync copies print two loops (no-reset and reset paths); budget the larger.
armcheck-oscmod: | $(GEN)
	$(ARMCC) $(ARMFLAGS) $(FW_INC) -c $(ROOT)/Core/DSPAudio/Oscillator.c -o build/ac_osc.o
	$(FP) --obj build/ac_osc.o --ref osc_buildSyncList --new osc_buildSyncList --report --all
	$(FP) --obj build/ac_osc.o --ref calcWavetableOscBlock --new calcWavetableOscSyncBlock --report --all
	$(FP) --obj build/ac_osc.o --ref calcSineBlock --new calcSineSyncBlock --report --all
	$(FP) --obj build/ac_osc.o --ref calcSampleOscBlock --new calcSampleOscSyncBlock --report --all
	$(FP) --obj build/ac_osc.o --ref calcFmBlock --new calcWavetableOscRingBlock --report --all
	$(FP) --obj build/ac_osc.o --ref calcFmSineBlock --new calcSineRingBlock --report --all
	$(FP) --obj build/ac_osc.o --ref calcSampleOscFmBlock --new calcSampleOscRingBlock --report --all
	$(FP) --obj build/ac_osc.o --ref calcNoiseBlock --new calcNoiseRingBlock --report --all
	$(FP) --obj build/ac_osc.o --ref calcUserSampleOscFmBlock --new calcUserSampleOscRingBlock --report --all

# G-A firmware half. Run elf_osc_baseline once on the unchanged tree (after
# `make clean && make all` at the repository root); elf_osc_check after.
elf_osc_baseline:
	python3 elf_symdump.py --elf $(FW_ELF) $(foreach s,$(OSC_ELF_SYMS),--sym $(s)) \
	  --out $(FROZEN85)/elf_osc.txt

elf_osc_check: | $(GEN)
	python3 elf_symdump.py --elf $(FW_ELF) $(foreach s,$(OSC_ELF_SYMS),--sym $(s)) \
	  --out build/elf_osc_now.txt
	python3 elf_symdump.py --compare $(FROZEN85)/elf_osc.txt build/elf_osc_now.txt
```

### X4 — New `tools/dsp_test/test_osc_mod.c`

```c
/*
 * Drum snc / rng oscillator support (S085 P1 host gate).
 *
 * What:       runs the new Oscillator.c S085 P1 functions (extracted from the
 *             current source) against the unchanged plain loops (extracted
 *             from the same file):
 *               R1 ring copies == plain render at gain 1 + the exact integer
 *                  ring law, every carrier family, interpolation, amt 0..1;
 *                  amt 0 == the carrier exactly;
 *               S1 sync at depth 0 == the plain render, resets present;
 *               S2 sync at depth 1: a reset after sample k lands on k + 1 at
 *                  overshoot x ratio (K4, Q4);
 *               S3 osc_syncPull(): the signed (short) pull; NONE passes;
 *               S4 osc_buildSyncList() against a 64-bit reference;
 *               S5 osc_syncClockInc(): << 5 for crash/user samples, the
 *                  clamp, equal reset rates at equal frequency;
 *               S6 unsynced slaves (noise, user sample) == the plain render;
 *               S7 the interpolated slave: partners share the list; the
 *                  phase rule;
 *               M1 osc_syncDepthQ30() / osc_ringK() / osc_syncRatioQ16().
 * Why:        S085 P1 §7.1: the new code must be exact where it should
 *             equal existing code, and its laws must hold where it is new.
 *             Each case names the code break that must make it fail.
 * Inputs:     generated fragments (Makefile osc_mod).
 * Outputs:    one line per failure (first 40); "osc_mod failures: N";
 *             exit 1 if N > 0.
 * Accessors:  make -C tools/dsp_test osc_mod.
 * Affiliates: Oscillator.c/.h S085 P1 sections; DSP_TEST.md §5.
 */
#include <stdint.h>
#include <string.h>
#include <math.h>

#define REAL_FS ((float)44108.07291666667f)
#define OSC_S085_LOOP
#define INTERPOLATE_OSC 1
#define INTERPOLATE_FM_OSC 1
#define OSC_SYNC_NONE 0xFFFFFFFFu   /* must equal Oscillator.h (S085 P1) */

#include "om_defs.inc"
#include "om_sampleinfo.inc"
#include "om_oscinfo.inc"

/* Host data in place of the flash tables (old and new read the same data). */
int16_t sine_table[4097];
int16_t sawTable[11][1024];
int16_t triTable[11][1024];
int16_t recTable[11][1024];
unsigned char crashSample[32769];       /* +1: the plain loop reads [32768] */
static int16_t host_sample[4097];       /* 4,096 points + the zero pad word */
static uint32_t host_rng = 0x0850001u;
static uint8_t host_interp_on;

int16_t GetRngValue(void) { return (int16_t)(golden_rand(&host_rng) >> 16); }
uint8_t modNode_getWaveInterpEnabled(void) { return host_interp_on; }
uint32_t modNode_getWaveInterpGeneration(void) { return 7u; }
uint8_t sampleMemory_getNumSamples(void) { return 1u; }
uint32_t sampleMemory_getGeneration(void) { return 1u; }
uint8_t sampleMemory_isLooped(uint8_t index) { (void)index; return 0u; }
SampleInfo sampleMemory_getSampleInfo(uint8_t index)
{
    SampleInfo info;
    (void)index;
    memset(&info, 0, sizeof(info));
    return info;   /* never reached: tests pre-fill the sample cache */
}

void calcNextOscSampleBlock(OscInfo* osc, int16_t* buf, const uint8_t size,
                            const float gain);

#include "om_scratch.inc"
#include "om_plain.c"
#include "om_map.c"
#include "om_s085.c"

#define N OUTPUT_DMA_SIZE
static unsigned failures;

static void fail(const char *what, unsigned a, unsigned b, unsigned c)
{
    failures++;
    if (failures <= 40u)
        printf("FAIL %s (%u, %u, %u)\n", what, a, b, c);
}

static void fill_tables(void)
{
    uint32_t s = 0x5eed085u;

    for (unsigned i = 0u; i < 4097u; ++i)
        sine_table[i] = (int16_t)lrint(32767.0 * sin(2.0 * M_PI * (double)i / 4096.0));
    for (unsigned o = 0u; o < 11u; ++o)
        for (unsigned i = 0u; i < 1024u; ++i) {
            sawTable[o][i] = (int16_t)(golden_rand(&s) >> 16);
            triTable[o][i] = (int16_t)(golden_rand(&s) >> 16);
            recTable[o][i] = (int16_t)(golden_rand(&s) >> 16);
        }
    for (unsigned i = 0u; i < 32769u; ++i)
        crashSample[i] = (unsigned char)golden_rand(&s);
    for (unsigned i = 0u; i < 4096u; ++i)
        host_sample[i] = (int16_t)(golden_rand(&s) >> 16);
    host_sample[4096] = 0;
}

static OscInfo make_osc(uint8_t wave, uint32_t phase, uint32_t inc, uint8_t looped)
{
    OscInfo o;

    memset(&o, 0, sizeof(o));
    o.waveform = wave;
    o.waveInterpNext = wave;
    o.phase = phase;
    o.phaseInc = inc;
    o.tableOffset = 3u;
    if (wave >= OSC_SAMPLE_START) {   /* cache hit: no SampleMemory lookup */
        o.sampleCacheData = host_sample;
        o.sampleCacheSize = 4096u;
        o.sampleCacheIndex = (uint8_t)(wave - OSC_SAMPLE_START);
        o.sampleCacheGeneration = 1u;
        o.sampleCacheLooped = looped;
    }
    return o;
}

static void set_interp(OscInfo *o, uint8_t next, float frac)
{
    o->waveInterpNext = next;
    o->waveInterpFrac = frac;
    o->waveInterpGeneration = 7u;
}

static int16_t ring_ref(int16_t car, int16_t mod, int32_t k)
{
    const int32_t v = ((int32_t)car * (k + (int32_t)mod)) >> 15;
    return (int16_t)(v > 32767 ? 32767 : (v < -32768 ? -32768 : v));
}

/* R1. Break: >> 14 in osc_ringSample(); K taken from amt instead of 1 - amt. */
static void test_ring(void)
{
    static const uint8_t waves[] = { SINE, TRI, SAW, REC, NOISE, CRASH,
                                     OSC_SAMPLE_START, OSC_SAMPLE_START };
    static const float amts[] = { 0.0f, 1.0f / 127.0f, 0.5f, 126.0f / 127.0f, 1.0f };
    static const uint32_t phases[] = { 0x00000000u, 0x7fff1234u, 0xfffff000u };
    static const uint32_t incs[] = { 0x00800000u, 0x07000000u, 0x30000000u };

    for (unsigned w = 0u; w < 8u; ++w)
     for (unsigned a = 0u; a < 5u; ++a)
      for (unsigned p = 0u; p < 3u; ++p)
       for (unsigned interp = 0u; interp < 2u; ++interp) {
        const uint8_t isSample = (uint8_t)(waves[w] >= OSC_SAMPLE_START);
        const uint32_t ph = isSample ? (p == 2u ? (4090u << 17) : ((100u + 997u * p) << 17))
                                     : phases[p];
        const uint32_t inc = isSample ? ((p + 1u) << 17) : incs[p];
        OscInfo ref = make_osc(waves[w], ph, inc, (uint8_t)(w == 7u));
        OscInfo dut;
        int16_t mod[N], car[N], out[N];
        const int32_t k = osc_ringK(amts[a]);
        uint32_t rng;

        if (interp && waves[w] >= CRASH)
            continue;
        if (interp)
            set_interp(&ref, (uint8_t)(waves[w] + 1u), 0.3f);
        host_interp_on = (uint8_t)interp;
        dut = ref;
        for (unsigned i = 0u; i < N; ++i)
            mod[i] = (int16_t)(golden_rand(&host_rng) >> 16);
        mod[0] = -32768;
        mod[1] = 32767;
        if (amts[a] == 0.0f)       /* amt 0: the modulator block (gain amt) is 0 */
            memset(mod, 0, sizeof(mod));
        rng = host_rng;
        calcNextOscSampleBlock(&ref, car, N, 1.0f);
        host_rng = rng;
        calcNextOscSampleRingBlock(&dut, mod, out, N, k);
        for (unsigned i = 0u; i < N; ++i) {
            if (out[i] != ring_ref(car[i], mod[i], k))
                fail("R1 ring law", w, a, i);
            if (amts[a] == 0.0f && out[i] != car[i])
                fail("R1 amt 0 is the carrier", w, p, i);
        }
        if (dut.phase != ref.phase || dut.output != ref.output)
            fail("R1 oscillator state", w, a, p);
       }
    host_interp_on = 0u;
}

/* S1 and S6. Break: the pull applied regardless of depth (or of NONE). */
static void test_sync_depth0_and_unsynced(void)
{
    static const uint8_t waves[] = { SINE, TRI, SAW, REC, NOISE, CRASH, OSC_SAMPLE_START };
    static const uint32_t clockIncs[] = { 0x01000000u, 0x30000000u, 0xf0000000u };

    for (unsigned w = 0u; w < 7u; ++w)
     for (unsigned c = 0u; c < 3u; ++c)
      for (unsigned interp = 0u; interp < 2u; ++interp)
       for (unsigned hard = 0u; hard < 2u; ++hard) {
        const uint8_t synced = osc_syncWaveform(waves[w]);
        uint32_t over[N];
        unsigned resets = 0u;
        int16_t ref_out[N], out[N];
        OscInfo ref = make_osc(waves[w], waves[w] >= OSC_SAMPLE_START ? (200u << 17) : 0x12345678u,
                               waves[w] >= OSC_SAMPLE_START ? (2u << 17) : 0x02000000u, 1u);
        OscInfo dut;
        uint32_t rng;

        if (hard && synced)
            continue;              /* depth 1 is S2's job for synced waves */
        if (interp && (hard || waves[w] >= CRASH))
            continue;              /* an interpolation partner may sync (S7) */
        if (interp)
            set_interp(&ref, (uint8_t)(waves[w] + 1u), 0.6f);
        host_interp_on = (uint8_t)interp;
        dut = ref;
        (void)osc_buildSyncList(0xfff00000u, clockIncs[c], over, N);
        for (unsigned i = 0u; i < N; ++i)
            resets += (unsigned)(over[i] != OSC_SYNC_NONE);
        if (resets == 0u)
            fail("S1 setup: no resets in list", w, c, 0u);
        rng = host_rng;
        calcNextOscSampleBlock(&ref, ref_out, N, 1.0f);
        host_rng = rng;
        calcNextOscSampleSyncBlock(&dut, over, clockIncs[c], hard ? 0x40000000 : 0, out, N);
        /* depth 0 (any wave, plain or interpolated) or an unsynced wave at
        ** depth 1: identical to the plain render, samples and phase. */
        for (unsigned i = 0u; i < N; ++i)
            if (out[i] != ref_out[i])
                fail(hard ? "S6 unsynced slave differs" : "S1 depth 0 differs", w, c, i);
        if (dut.phase != ref.phase)
            fail("S1/S6 phase differs", w, c, interp);
       }
    host_interp_on = 0u;
}

/* S2. Break: reset applied before sample i; ratio inverted. */
static void test_sync_hard(void)
{
    static const uint8_t waves[] = { SINE, TRI, SAW, REC, CRASH };
    static const unsigned ks[] = { 0u, 5u, 31u };
    const uint32_t clockInc = 0x20000000u;
    const uint32_t over_k = 0x0abcdef0u;

    for (unsigned w = 0u; w < 5u; ++w)
     for (unsigned kk = 0u; kk < 3u; ++kk) {
        const unsigned k = ks[kk];
        const uint32_t slaveInc = (waves[w] == CRASH) ? 0x00300000u : 0x03000000u;
        const uint32_t ratio = osc_syncRatioQ16(slaveInc, clockInc);
        const uint32_t target = (uint32_t)(((uint64_t)over_k * ratio) >> 16);
        uint32_t over[N];
        int16_t ref_out[N], out[N];
        OscInfo dut = make_osc(waves[w], 0x40000000u, slaveInc, 0u);
        OscInfo ref = dut;

        for (unsigned i = 0u; i < N; ++i)
            over[i] = OSC_SYNC_NONE;
        over[k] = over_k;
        calcNextOscSampleSyncBlock(&dut, over, clockInc, 0x40000000, out, N);
        calcNextOscSampleBlock(&ref, ref_out, (uint8_t)(k + 1u), 1.0f);
        ref.phase = target;
        if (k + 1u < N)
            calcNextOscSampleBlock(&ref, ref_out + k + 1u, (uint8_t)(N - k - 1u), 1.0f);
        for (unsigned i = 0u; i < N; ++i)
            if (out[i] != ref_out[i])
                fail("S2 hard reset sample", w, k, i);
        if (dut.phase != ref.phase)
            fail("S2 hard reset phase", w, k, 0u);
     }
}

/* S3. Break: unsigned (long-way) difference; NONE not honoured. */
static void test_pull(void)
{
    if (osc_syncPull(0x12345678u, OSC_SYNC_NONE, 65536u, 0x40000000) != 0x12345678u)
        fail("S3 NONE passes", 0u, 0u, 0u);
    if (osc_syncPull(0x100u, 0xffffff00u, 65536u, 0x20000000) != 0u)
        fail("S3 short way round", 0u, 0u, 0u);
    if (osc_syncPull(0x80000000u, 0x00012345u, 3u << 16, 0x40000000) != 0x000369cfu)
        fail("S3 depth 1 = target", 0u, 0u, 0u);
    if (osc_syncPull(0u, 0x400u, 65536u, 0x10000000) != 0x100u)
        fail("S3 depth 0.25", 0u, 0u, 0u);
    if (osc_syncPull(0x55555555u, 0x400u, 65536u, 0) != 0x55555555u)
        fail("S3 depth 0", 0u, 0u, 0u);
}

/* S4. Break: carry test inverted; overshoot not the wrapped value. */
static void test_builder(void)
{
    static const uint32_t incs[] = { 0u, 1u, 0x00010000u, 0x12345678u, 0x80000000u, 0xffffffffu };
    static const uint32_t starts[] = { 0u, 0x7fffffffu, 0xfffffff0u };

    for (unsigned a = 0u; a < 6u; ++a)
     for (unsigned b = 0u; b < 3u; ++b) {
        uint32_t over[N];
        uint32_t c = starts[b];
        const uint32_t end = osc_buildSyncList(starts[b], incs[a], over, N);

        for (unsigned i = 0u; i < N; ++i) {
            const uint64_t n = (uint64_t)c + incs[a];
            const uint32_t expect = (n >> 32) ? (uint32_t)n : OSC_SYNC_NONE;

            if (over[i] != expect)
                fail("S4 list entry", a, b, i);
            if ((n >> 32) && (uint32_t)n >= incs[a])
                fail("S4 overshoot >= increment", a, b, i);
            c = (uint32_t)n;
        }
        if (end != c)
            fail("S4 end clock", a, b, 0u);
     }
}

/* S5. Break: the << 5 scaling missing; the clamp missing. */
static void test_clock(void)
{
    static const float freqs[] = { 55.0f, 440.0f, 3000.0f };
    OscInfo m = make_osc(TRI, 0u, 0x01234567u, 0u);

    if (osc_syncClockInc(&m) != 0x01234567u) fail("S5 table clock", 0u, 0u, 0u);
    m.waveform = NOISE;
    if (osc_syncClockInc(&m) != 0x01234567u) fail("S5 noise clock", 0u, 0u, 0u);
    m.waveform = CRASH;
    m.phaseInc = 0x00100000u;
    if (osc_syncClockInc(&m) != 0x02000000u) fail("S5 crash << 5", 0u, 0u, 0u);
    m.waveform = OSC_SAMPLE_START;
    m.phaseInc = 0x09000000u;
    if (osc_syncClockInc(&m) != 0xffffffe0u) fail("S5 clamp", 0u, 0u, 0u);

    for (unsigned f = 0u; f < 3u; ++f) {
        OscInfo tri = make_osc(TRI, 0u, freq2PhaseIncr1024(freqs[f]), 0u);
        OscInfo cr = make_osc(CRASH, 0u, freq2PhaseIncr32767(freqs[f]), 0u);
        const uint32_t a = osc_syncClockInc(&tri);
        const uint32_t b = osc_syncClockInc(&cr);
        uint32_t ca = 0u, cb = 0u, over[N];
        unsigned ra = 0u, rb = 0u;

        if ((a > b ? a - b : b - a) > 32u)
            fail("S5 equal clock at equal frequency", f, a, b);
        for (unsigned blk = 0u; blk < 2000u; ++blk) {
            ca = osc_buildSyncList(ca, a, over, N);
            for (unsigned i = 0u; i < N; ++i) ra += (unsigned)(over[i] != OSC_SYNC_NONE);
            cb = osc_buildSyncList(cb, b, over, N);
            for (unsigned i = 0u; i < N; ++i) rb += (unsigned)(over[i] != OSC_SYNC_NONE);
        }
        if ((ra > rb ? ra - rb : rb - ra) > 1u)
            fail("S5 equal reset rate", f, ra, rb);
    }
}

/* S7. Break: partners given different lists; the phase rule reversed. */
static void test_interp_sync(void)
{
    const uint32_t clockInc = 0x09000000u;
    uint32_t over[N];
    int16_t out[N], ra[N], rb[N];
    OscInfo dut = make_osc(SAW, 0x40000000u, 0x05000000u, 0u);
    OscInfo a, b;

    (void)osc_buildSyncList(0u, clockInc, over, N);
    set_interp(&dut, REC, 0.4f);
    host_interp_on = 1u;
    a = dut;
    a.waveInterpNext = SAW;
    a.waveInterpFrac = 0.0f;
    a.waveInterpGeneration = 0u;
    b = a;
    b.waveform = REC;
    b.waveInterpNext = REC;
    calcNextOscSampleSyncBlock(&dut, over, clockInc, 0x40000000, out, N);
    calcNextOscSampleSyncBlock(&a, over, clockInc, 0x40000000, ra, N);
    calcNextOscSampleSyncBlock(&b, over, clockInc, 0x40000000, rb, N);
    for (unsigned i = 0u; i < N; ++i)
        if (out[i] != (int16_t)(ra[i] + 0.4f * (rb[i] - ra[i])))
            fail("S7 interpolated blend", i, 0u, 0u);
    if (dut.phase != a.phase)
        fail("S7 phase follows the synced base", 0u, 0u, 0u);

    dut = make_osc(NOISE, 0x10000000u, 0x05000000u, 0u);
    set_interp(&dut, CRASH, 0.5f);
    calcNextOscSampleSyncBlock(&dut, over, clockInc, 0x40000000, out, N);
    if (dut.phase != 0x10000000u + 0x05000000u * (uint32_t)N)
        fail("S7 unsynced base keeps the plain phase rule", 0u, 0u, 0u);
    host_interp_on = 0u;
}

/* M1. Break: Q31 instead of Q30; K unclamped; ratio unclamped. */
static void test_maps(void)
{
    if (osc_syncDepthQ30(0.0f) != 0) fail("M1 depth 0", 0u, 0u, 0u);
    if (osc_syncDepthQ30(1.0f) != 0x40000000) fail("M1 depth 1", 0u, 0u, 0u);
    if (osc_syncDepthQ30(0.5f) != 0x20000000) fail("M1 depth 0.5", 0u, 0u, 0u);
    if (osc_syncDepthQ30(-1.0f) != 0) fail("M1 depth < 0", 0u, 0u, 0u);
    if (osc_syncDepthQ30(2.0f) != 0x40000000) fail("M1 depth > 1", 0u, 0u, 0u);
    if (osc_syncDepthQ30(NAN) != 0) fail("M1 depth NaN", 0u, 0u, 0u);
    if (osc_ringK(0.0f) != 32768) fail("M1 K at 0", 0u, 0u, 0u);
    if (osc_ringK(1.0f) != 0) fail("M1 K at 1", 0u, 0u, 0u);
    if (osc_ringK(0.5f) != 16384) fail("M1 K at 0.5", 0u, 0u, 0u);
    if (osc_ringK(0.25f) != 24576) fail("M1 K at 0.25", 0u, 0u, 0u);
    if (osc_syncDepthQ30(0.25f) != 0x10000000) fail("M1 depth 0.25", 0u, 0u, 0u);
    if (osc_ringK(NAN) != 32768) fail("M1 K NaN", 0u, 0u, 0u);
    if (osc_syncRatioQ16(100u, 0u) != 0u) fail("M1 ratio clock 0", 0u, 0u, 0u);
    if (osc_syncRatioQ16(0x01000000u, 0x01000000u) != 65536u) fail("M1 ratio 1", 0u, 0u, 0u);
    if (osc_syncRatioQ16(0xffffffffu, 1u) != 4294967040u) fail("M1 ratio clamp", 0u, 0u, 0u);
}

int main(void)
{
    fill_tables();
    test_ring();
    test_sync_depth0_and_unsynced();
    test_sync_hard();
    test_pull();
    test_builder();
    test_clock();
    test_interp_sync();
    test_maps();
    printf("osc_mod failures: %u\n", failures);
    return failures ? 1 : 0;
}
```

**Break proofs** (per DSP_TEST.md §6.1: apply each break, confirm the
case fails, restore). **Verified in this deep-dive:** the code in §4 was
spliced into a scratch copy of `Oscillator.c/.h` and this test was run on
it. Clean: `osc_mod failures: 0`. Every break below failed as listed.

| Break (in the firmware source) | Cases that failed |
|---|---|
| `>> 14` in `osc_ringSample()` | R1 ring law; R1 amt 0 is the carrier |
| `osc_ringK()` interior = `amount × 32768` | M1 K at 0.25 |
| `osc_ringK()` returns 0 for `amount <= 0` | R1 amt 0 is the carrier; M1 K at 0; M1 K NaN |
| `osc_syncPull()` returns `pulled` unconditionally | S2 hard reset sample/phase; S3 NONE passes |
| `osc_syncPull()` with an unsigned `diff` | S3 short way round |
| table sync copy pulls `oscPhase` before the advance (reset at *i*) | S2 hard reset sample/phase |
| `osc_syncRatioQ16(clockInc, slaveInc)` (inverted) | S2 hard reset sample/phase |
| `inc <<= 5` removed in `osc_syncClockInc()` | S5 crash << 5; clamp; equal clock; equal reset rate |
| builder tests `next > clock` | S4 list entry |
| `calcPeriodicInterpSyncBlock()` renders partner B plain | S7 interpolated blend |
| interpolation phase rule reversed | S7 phase follows the synced base |
| `osc_syncDepthQ30()` scaled to Q31 | M1 depth 0.25; M1 depth 0.5 |

### X5 — `tools/dsp_test/DSP_TEST.md`

**§3 Layout table:** add these rows after the `test_mixer.c` row:

```markdown
| `snapshot85.sh`, `frozen85/` | S085 P1: the pre-change `Oscillator.c/.h`, `BufferTools.c/.h`, `DrumVoice.c/.h`, `DrumParameters.c`, `MenuText.h`, and `elf_osc.txt` (the normalised firmware disassembly of every existing oscillator loop). `snapshot85.sh` refuses to run if `frozen85/` exists. |
| `elf_symdump.py` | Dumps or compares the normalised disassembly of listed firmware symbols (all LTO clones), with addresses removed (S085 P1 G-A). |
| `test_osc_mod.c` | Drum `snc`/`rng` oscillator support: the ring and sync copies against the unchanged plain loops, the builder, the clock and the mappings (S085 P1). |
```

**§5 Targets table:** add these rows before `clean`:

```markdown
| `oscmod_identity` | `Oscillator.c/.h` have no removed or changed line against `frozen85/`; `BufferTools.c/.h` identical | seconds | two `removed or changed lines: 0` lines, `oscmod_identity: OK` |
| `osc_mod` | `test_osc_mod.c` (R1, S1–S7, M1) | seconds | `osc_mod failures: 0` |
| `armcheck-oscmod` | per-sample counts of the S085 P1 loops beside the plain/FM loops they replace | seconds | the §1.2 figures of S085_P1_OSC_MOD_IMPLEMENTATION.md, within a few instructions |
| `elf_osc_baseline` / `elf_osc_check` | the linked firmware's existing oscillator loops, before and after | seconds | `0 missing, 0 changed` (new symbols allowed) |
```

---

## 6. Build and gate procedure

1. **Baseline (before any firmware edit):**
   1. Add X1–X4. Then run `sh tools/dsp_test/snapshot85.sh`.
   2. `make clean && make all` (the tree at `1ea234ea`).
   3. `make -C tools/dsp_test elf_osc_baseline`. This writes
      `frozen85/elf_osc.txt`.
   4. `python3 tools/link_budget.py arm-none-eabi-nm build/lxr02.elf`: record
      it (expected ITCM 4,168 / 16,384 B; flash headroom 143,952 B).
   5. **User:** six Drum slots at their dearest `fm` setting (user-sample
      modulator and carrier, an LFO on one carrier's `wav` for
      interpolation). Record the `cpu` reading and `UNR`.
2. **`rng` and the shared pieces:**
   - O-H1, O-C1, D-H1, D-H2, D-C1, D-C2, P1, T1, T2, M1–M8 (the whole
     firmware change; `snc` and `rng` land together, because they share the
     dispatch).
3. **Build:** `make clean && make all && make img`. Expect no new warnings.
   `_Static_assert`s: `sizeof(DrumVoice)` grows by 4; the slot assert holds,
   because the reserve tracks it.
4. **Host and static gates:**
   ```
   make -C tools/dsp_test oscmod_identity osc_mod armcheck-oscmod elf_osc_check special_tags
   ```
   - `special_tags` must report the same row count as before (no rows
     added).
   - Run the break proofs (§5 X4).
5. **Ledgers:** run `link_budget.py` again. Record ITCM (expected ≈ 7.0
   KB used), text, bss (+48 B `runtime_slots`) and DTCM (unchanged).
6. **Review `Drum_calcVoiceSyncBlock` (D4):**
   - **Locate:** `arm-none-eabi-objdump -d build/lxr02.elf`, the function
     that contains the Drum render. LTO may inline it into the
     InstrumentManager or mixer render.
   - **Check:** the `fm` path still calls `calcNextOscSampleBlock`
     (modulator, gain `fmModAmount`) and then `calcNextOscSampleFmBlock`;
     the `mix` path calls both renders and `bufferTool_addBuffersSaturating`.
     Both use the same clone symbols as in the baseline.
7. **User — device gate G-C and the listening matrix** (plan §7.3, H1–H6):
   - the same six-slot Scene in `snc` (a crash or table slave, then an
     interpolated slave) and in `rng` (user-sample carrier);
   - `cpu` no worse than step 1.5, zero underruns, the interface responsive.
8. **Specifications** (§8), then the session close-out (§9).

**Acceptance:**
- steps 4–5 all pass;
- G-C passes on the device;
- H4 (`fm`/`mix` A/B) sounds the same.

---

## 7. Risks during implementation

| # | Risk | Mitigation |
|---|---|---|
| I1 | LTO re-clones or re-inlines an existing loop because of the new callers (`osc_renderSyncWave()` and `calcPeriodicInterpRingBlock()` call `calcNextOscSampleBlock(…, 1.f)`). | `elf_osc_check`: baseline symbols must be unchanged; added clones are allowed only if reachable from new code (check their callers). If a baseline clone changes, call the new paths through a `noinline` wrapper or route the unsynced fallback to the leaf functions directly. |
| I2 | GCC turns the sync select into a branch (measured: two loop variants). The reset path costs about 5 more. | G-B budgets the reset path (the larger of the two loops). |
| I3 | `test_osc_mod.c`'s host extraction of `osc_updateSampleCache()` warns about the integer-to-pointer cast. | The tests pre-fill the cache, so the cast never runs. The warning is expected and harmless. |
| I4 | An LFO on the FM-page `wav` in `snc` still claims a waveform-interpolation unit (modulationNode.c) for an oscillator that is not rendered. | No CPU increase: the unit is simply unused. Another voice can lose interpolation in that block. Document it in INSTRUMENTS_DSP_REFERENCE §5.1. |
| I5 | Line drift: `menu.c` is large and edited often. | Use the anchors given with each M change; apply bottom-up. |
| I6 | `-Wextra`: a parameter becomes unused if the code is edited later. | None in the code as written. |

---

## 8. Specification changes

### S1 — `knowledge_files/specification_reference/dsp_instruments_effects/INSTRUMENTS_DSP_REFERENCE.md`

**§5.1, lines 288–295 (the Drum diagram).** Replace the first three lines
inside the code fence (289–291) with:

```
modMode fm  (0): modOsc --(gain fmModAmount)--> modBuf; osc phase-modulated by modBuf      --> buf
modMode mix (1): modOsc --(gain fmModAmount)--> modBuf; osc (gain 1−fmModAmount) + modBuf  --> buf
modMode rng (3): modOsc --(gain fmModAmount)--> modBuf; osc ring copy: osc·(K+modBuf)>>15,
                 K = (1−fmModAmount)·32768                                                  --> buf
modMode snc (2): modOsc not rendered; syncClock at the FM-page frequency --> reset list;
                 osc sync copy, gain 1, depth fmModAmount                                   --> buf
```

**§5.1, after line 308.** Add:

```markdown
- **Mod modes (S085 P1).** `osc2_mod_type` (`modMode`): 0 `fm`, 1 `mix`,
  2 `snc`, 3 `rng`; any other byte plays `fm`.
  - **`snc`:** the FM-page oscillator is a clock (`syncClock`, its own
    field) at the FM-page frequency, never rendered. `modOsc.phase` is not
    touched.
    - Sin, Tri, Saw and Rec masters reset the slave once per cycle. Crash
      and user-sample masters act as noise: the clock is their increment
      `<< 5`, one reset per nominal period.
    - Sin/Tri/Saw/Rec/Crash slaves sync. Noise and user-sample slaves
      render unsynced.
    - The reset lands on the sample after the wrap, at the overshoot ×
      slave/clock ratio. `amt` pulls the phase that far (0 free, 127 hard).
    - Only the slave is heard.
  - **`rng`:** carrier·(1−amt) + carrier·modulator·amt, folded into the
    carrier's store; every waveform.
  - **Trigger:** unchanged in every mode. `modOsc` and `syncClock` run free.
  - **An LFO on the FM-page `wav` in `snc`** still claims an interpolation
    unit, which is then unused.
  - Implementation: `S085_P1_OSC_MOD_IMPLEMENTATION.md`.
```

**§6.1, after the FM bullet (lines 375–377).** Add:

```markdown
- **Sync and ring (S085 P1):** `calcNextOscSampleSyncBlock()` /
  `calcNextOscSampleRingBlock()` mirror `calcNextOscSampleBlock()`. They
  dispatch to per-waveform copies of the plain loops (`calc*SyncBlock`,
  `calc*RingBlock`) whose reads are verbatim; only the phase update (sync)
  or the store (ring) differs.
  - The reset list (`osc_buildSyncList()`, `OSC_SYNC_NONE`) is built once
    per block from the clock (`osc_syncClockInc()`).
  - The existing loops are unchanged; `tools/dsp_test` `oscmod_identity`
    and `elf_osc_check` prove it.
  - Later master kinds (plan §12: looped samples, wavetable frames, the
    zero-crossing bitmap) add a list builder, not new slave loops.
```

**§8.1 table, after the `Saturating buffer add` row.** Add:

```markdown
| Sync reset-list builder (S085) | 7 | 0 |
| Sync slave Tri/Saw/Rec / sine / crash (no reset / reset path) | 34/39 / 33/38 / 36/41 | 0 (+1 per block) |
| Ring carrier Tri/Saw/Rec / sine / crash / noise / user sample | 28 / 27 / 30 / 13 / 40–41 | 0 |
```

**§8.2 table, after the `Drum (FM mode)` row.** Add:

```markdown
| Drum (`rng`) | mod osc 24 + ring carrier 28 + transient 22 + add 5 + filter 95 + post 32 | 206 instructions, 3 divisions |
| Drum (`snc`) | builder 7 + sync slave 39 + transient 22 + add 5 + filter 95 + post 32 | 200 instructions, 3 divisions + 1 per block |
```

Add the sentence: "With user-sample oscillators, the oscillator section is
`fm` 90, `mix` 87, `rng` 82, `snc` ≤ 48 (S085 P1 §1)."

**§5.5 (line 338).** After "The oscillator block functions are in ITCM",
add: "(since S085 P1 also the sync/ring copies, their dispatchers and the
reset-list builder; the two interpolation variants are inlined into their
ITCM dispatchers, like `calcPeriodicInterpBlock()`)".

**§11 History.** Append:

```markdown
- Session 085 P1: Drum mod modes `snc` (hard sync to an FM-page clock, `amt`
  = depth) and `rng` (ring, folded into the carrier store); `mixOscs`
  renamed `modMode`; `DrumVoice.syncClock` (S085 P1 implementation
  document).
```

### S2 — `knowledge_files/specification_reference/dsp_instruments_effects/CPU_USE_DSP_AUDIT.md`

Append after item 35 (the end of "Session 075 additions"):

```markdown
### Session 085 P1 additions (record in `085_SESSION_HANDOFF_LOG.md`)

36. **ADDED (S085 P1): Drum `rng`.** The ring law is folded into
    per-waveform carrier copies. It costs the plain loop + 0…4 per sample
    and has no separate pass. A separate pass (9 scalar, 7.5 packed) was
    measured and rejected: with user-sample oscillators it made `rng` ≥
    `fm` (89.5–91 vs 90).
37. **ADDED (S085 P1): Drum `snc`.** The FM-page oscillator is not
    rendered. The cost is a 7-instruction reset-list builder plus the
    slave's sync copy (plain + 10…11 on the reset path) and one VDIV per
    block. The dearest `snc` (crash slave, 48) is below the dearest `fm`
    (user samples, 90).
38. **UNCHANGED (S085 P1): `fm` and `mix`.** The existing loops are
    byte-identical in the linked firmware (`elf_osc_check`). Device check:
    <`cpu` readings for the six-slot `fm` / `snc` / `rng` Scenes, to be
    filled at G-C>.
```

### S3 — `knowledge_files/specification_reference/STORAGE_SRAM_MANIFEST.md`

- **Header (after line 13).** Add an "S085 P1 changes" bullet with the
  measured figures from §6 step 5:
  - `DrumVoice` 588 → 592 B, so `runtime_slots` 7,056 → 7,104 B (+48 B
    SRAM1 `.bss`);
  - ITCM 4,168 → measured value (≈ +2.85 KB: the sync/ring copies, the
    dispatchers, the builder);
  - DTCM unchanged;
  - audio stack +128 B (`syncOver[32]`, SRAM2) in `snc`;
  - the user's RAM approval (S085 P1 plan r3).
- **Lines 10, 168 and 422.** Update the ITCM and `.bss` totals to the
  measured values.
- **§6 ITCM contents table (lines 494–503).** Add rows with the measured
  sizes (`arm-none-eabi-nm -S --size-sort build/lxr02.elf`):
  - `calcNextOscSampleSyncBlock`, `calcNextOscSampleRingBlock`,
    `osc_renderSyncWave`, `osc_buildSyncList`;
  - `calcUserSampleOscRingBlock`;
  - the sine/table/crash sync and ring copies;
  - `calcNoiseRingBlock`.
- **Line 599 (`runtime_slots`).** Change to: "7,104 | Six tagged 1,184 B
  engine slots; … The largest engine (`DrumVoice`) is 592 B (588 + the S085
  P1 `syncClock`); the reserve is twice that."

### S4 — `knowledge_files/specification_reference/FILESYSTEM_SPEC.md`

After line 1822 (the end of the oscillator key list), add:

```markdown
  `osc2_mod_type` (Drum) stores 0 `fm`, 1 `mix`, 2 `snc`, 3 `rng`
  (S085 P1). A missing key loads 0.
```

### S5 — `knowledge_files/specification_reference/MIDI_SPEC.md`

**§5.5, after the "clamped to the parameter's own domain" bullet (line
406–407).** Add:

```markdown
  - Drum `osc2_mod_type` (`mod`): 0 `fm`, 1 `mix`, 2 `snc`, 3–127 `rng`
    (S085 P1; before S085, 1–127 gave `mix`).
```

### S6 — `MODULE_INTERCHANGE_SPEC.md`, `BANK_PRESET_ARCHITECTURE.md`

No change: neither lists the `osc2_mod_type` values. The row, key and
storage are unchanged.

---

## 9. Close-out (after G-C)

- **`knowledge_files/log_archive/085_SESSION_HANDOFF_LOG.md`:**
  - the figures from §6 steps 1, 4, 5 and 7;
  - deviations D1–D6;
  - the break-proof results.
- **`MEMORY.md`:**
  - current working source and build figures;
  - **binding notes:** the `fm`/`mix` loops are frozen; any new oscillator
    mode is added as copies plus a list builder (plan §12); `syncClock` is
    not reset on a trigger (user Q6b).
- **The plan (`S085_P1_OSC_MOD_SNC_RNG.md`):** mark §4.2 / §4.1 / §8 as
  superseded by D1, D2 and D5.

---

## 10. Not changed (explicit)

- **`Drum_triggerVoice()`:** the carrier reset only, as today. `modOsc` and
  `syncClock` are not touched (user Q6b).
- **`Drum_calcVoiceAsync()`:** `osc_setFreq()` still runs for both
  oscillators. `osc.fmMod` is still computed in every mode (unused in
  `snc`/`rng`; plan R10).
- **Engines and shared code:**
  - Snare, Cymbal and HiHat;
  - `InstrumentManager.c` (the slot reserve grows by itself), Preset,
    Morph, LiveRecord;
  - the MIDI modules;
  - `BufferTools.c/.h`, `config.h`, `modulationNode.c`;
  - the sample flash and the installer.
- **Tooling and data:**
  - `check_special_tags.py`, because no tag changes;
  - the converters, which only write 0/1;
  - `frozen/` (pre-S073), which is untouched.

---

## 11. Implementation log (2026-10-27)

This section records the session's actual measurements and the two
deviations found while making the gates pass. Everything in §4–§8 above is
implemented as written except D7 and D8 below.

### 11.1 Baseline (before any firmware edit)

- `sh tools/dsp_test/snapshot85.sh` froze eight files into
  `tools/dsp_test/frozen85/`.
- `make clean && make all`: `text=609,288`, `data=424`,
  `bss=430,200`; flash payload 609,712 B (headroom 143,952 B); ITCM
  4,168 / 16,384 B; DTCM 4,472 B; FXBUF 126,592 B (margin 3,712 B).
  This equals the S084 close figures in `MEMORY.md`.
- `make -C tools/dsp_test elf_osc_baseline` wrote
  `frozen85/elf_osc.txt` (12 linked symbols, all LTO clones).
- Baseline `fpseq.py --report --all` (main loop) reproduced §1.2:
  table 24, sine 23, crash 30, noise 12–13, user sample 40–41, FM table 35,
  FM sine 30, FM crash 38, FM user sample 40–41.

### 11.2 Build and ledgers (after the firmware change)

| Figure | Baseline | After | Delta |
|---|---:|---:|---:|
| `text` | 609,288 | 613,232 | +3,944 |
| `data` | 424 | 424 | 0 |
| `bss` | 430,200 | 430,248 | +48 |
| Flash payload | 609,712 | 613,656 | +3,944 |
| Flash headroom | 143,952 | 140,008 | −3,944 |
| ITCM | 4,168 | 7,560 | +3,392 |
| DTCM statics | 4,472 | 4,472 | 0 |
| FX arena margin | 3,712 | 3,712 | 0 |

- `DrumVoice` is 592 B (was 588); `runtime_slots` is 7,104 B (was
  7,056), both static asserts still hold.
- ITCM is above the plan's ≈2,848 B estimate because the two dispatchers
  stay out of line plus the new `osc_renderPlainWave` (D8); 8,824 B of
  ITCM remain free.
- `make img` wrote a 613,656-byte image.
- No new compiler warning (only the four pre-existing newlib
  `_close/_lseek/_read/_write` linker notes).
- A final `make clean && make all && make img` reproduced every figure
  above; the gates were re-run on that clean build.

### 11.3 Gates

| Gate | Result |
|---|---|
| `make -C tools/dsp_test osc_mod` | `osc_mod failures: 0` (I3's expected integer-to-pointer warning only) |
| `make -C tools/dsp_test oscmod_identity` | `removed or changed lines: 0` for both files; `BufferTools` byte-identical; `OK` |
| `make -C tools/dsp_test elf_osc_check` | `0 missing, 0 changed, 0 added`; one `PADDING` note (D7) |
| `make -C tools/dsp_test armcheck-oscmod` | builder 7; sync table 34/39, sine 33/38, crash 36/41; ring table 28, sine 27, crash 30, noise 13, user sample 40; matches §1.2 |
| `make -C tools/dsp_test special_tags` | `155 rows, 0 mismatches` (no rows added) |
| Break proofs (§5 X4) | all twelve applied one at a time: every one produced a non-zero `osc_mod failures` count, and the source was restored after each |

The break proofs run: `>> 14` ring shift; `K` from `amt`; `K` ≤ 0
returning 0; the pull applied for `OSC_SYNC_NONE`; unsigned pull
difference; the table sync copy pulling `oscPhase` before the advance;
the ratio inverted; `<< 5` removed; the builder testing `next > clock`;
partner B rendered plain; the interpolation phase rule reversed; the depth
scaled to Q31. Two reverts initially landed at the first textual match
instead of their hunk (`osc_syncDepthQ30`/`osc_ringK`/`osc_syncClockInc`);
they were corrected before the final build, and the final source has zero
removed/changed lines against `frozen85/`.

### 11.4 Deviations found during implementation

| # | Plan said | Implementation does | Why |
|---|---|---|---|
| **D7** | every existing oscillator loop's disassembly identical apart from addresses and veneers | the new code is placed in `.itcm.s085` and `elf_symdump.py` treats a trailing run of literal-pool words as layout (`PADDING`) | putting the new functions inside `.itcm` pushed `calcUserSampleOscBlock` out of 16-bit tail-branch range and flipped a clone's tail to `b.w`. With `.itcm.s085` (the linker script already matches `*(.itcm.*)` after `*(.itcm)`), the instructions are identical; one previously unused pool word at the end of `calcNextOscSampleBlock.constprop.0` still differs. Instructions, branches and middle pool entries are still compared strictly. |
| **D8** | the sync/ring dispatchers are `INITCM` and the unsynced fallback calls `calcNextOscSampleBlock(osc, buf, size, 1.f)` | the dispatchers are `noinline`+`noclone` in `.itcm.s085`; a new `osc_renderPlainWave()` (leaf dispatch at gain 1) serves the unsynced fallback and the ring interpolation partners | the first build added constant-gain call sites to `calcNextOscSampleBlock()`, so LTO re-specialised its two baseline clones (they swapped roles and bodies). Routing the fallback and the ring partners to the leaves reproduces §7 I1's mitigation and restores the baseline clone text. `noinline` keeps the interpolated blend loops in ITCM instead of inlining them into the flash-resident Drum render. |

### 11.5 Not done here

- **Device gate G-C and the listening matrix H1–H6 (user):** six Drum slots
  at the baseline `fm` settings, then all `snc` (crash and interpolated
  slaves) and all `rng` (user-sample carrier); `cpu` no worse, zero
  underruns, interface responsive; `fm`/`mix` A/B against the S084 build.
- **S084 hardware check V1–V7** is still open and gates S085 (plan R1);
  the user confirmed P1 may be implemented now.
- **Session close-out** (§9): `085_SESSION_HANDOFF_LOG.md` and the
  `MEMORY.md` binding notes after G-C.

---

## 12. Assessment of the implementation (2026-10-27)

**Verdict:** the firmware change is correct and meets the CPU gate on every
build-time check. No code behaviour needs to change. Four documentation
corrections are open (§12.6, F1–F4). The device gate G-C, the listening
matrix and the session close-out are still to do (§11.5).

### 12.1 What was checked, and how

- **Every changed file was compared with this document:**
  - the firmware: `Oscillator.c/.h`, `DrumVoice.c/.h`,
    `DrumParameters.c`, `MenuText.h`, `menu.c`;
  - the tools: `snapshot85.sh`, `elf_symdump.py`, `test_osc_mod.c`, the
    Makefile;
  - the specifications.

  The §4–§5 code blocks were diffed against the added lines.
- **Rerun on the current `build/lxr02.elf`** (built after the last source
  edit): `oscmod_identity`, `osc_mod`, `elf_osc_check`, `special_tags` and
  `armcheck-oscmod`.
- **Baseline rebuilt** from `HEAD` (`b450d76a`, whose code is the
  pre-change tree) in a scratch git worktree, removed afterwards. It
  reproduced `frozen85/elf_osc.txt` byte for byte and ITCM 4,168 B. The two
  linked images were then compared (§12.4).
- **Loops measured in the linked image:** `fpseq.py` was run on the linked
  ELF as well as on standalone objects, so the LTO-built loops were
  measured too.

### 12.2 Conformance with §4–§5

| ID | Result |
|---|---|
| O-H1 | identical to §4.1 |
| O-C1 | identical to §4.2, except D7 (`OSC_S085_SECTION` `.itcm.s085`; `OSC_S085_DISPATCH` on both dispatchers) and D8 (`osc_renderPlainWave()`; it replaces the two `calcNextOscSampleBlock(…, 1.f)` fallbacks in `osc_renderSyncWave()` and `calcPeriodicInterpRingBlock()`). There are no removed or changed lines in the pre-existing text (`oscmod_identity`). |
| D-H1, D-H2, D-C1, D-C2, P1 | verbatim |
| T1, T2, M1–M8 | verbatim, at the planned anchors |
| X1 | verbatim |
| X2 | §5 plus the D7 `PADDING` rule (`strip_pool()`) |
| X3 | §5 plus `osc_renderPlainWave` in `OSC_S085` |
| X4 | §5 plus `#define OSC_S085_SECTION` / `OSC_S085_DISPATCH` (empty) |
| X5, S1–S5 | done, with measured figures; S6 none, as planned |

### 12.3 Gates (rerun for this assessment)

| Gate | Result |
|---|---|
| `oscmod_identity` | `removed or changed lines: 0` for both files; `BufferTools.c/.h` identical; `OK` |
| `osc_mod` | `osc_mod failures: 0`; only the expected I3 cast warning |
| `elf_osc_check` | `12 baseline symbols, 0 missing, 0 changed, 0 added`, one `PADDING` line (D7, §12.5) |
| `special_tags` | `155 rows, 0 mismatches` |
| `armcheck-oscmod` (standalone) | builder 7; sync table 34/39, sine 33/38, crash 36/41; ring table 28, sine 27, crash 30, noise 13/25, user sample 40/41: §1.2 exactly |
| Ledgers (`link_budget.py`, `size`) | text 613,232; data 424; bss 430,248; flash headroom 140,008 B; ITCM 7,560 / 16,384 B; DTCM 4,472 B; FX-arena margin 3,712 B. These match §11.2. |

### 12.4 The Drum render in the linked image (closes §6 step 6, D4)

- **Where it is:** LTO merges `Drum_calcVoiceSyncBlock()` into
  `mixer_calcNextSampleBlock()`. The call targets of that function were
  compared between the rebuilt baseline and the new image.
- **Unchanged:** 21 call targets, including every oscillator, FM, filter
  and post-chain callee.
- **`calcNextOscSampleBlock.constprop.0`:** 7 → 9 call sites. That is
  exactly the switch's new shape: the `rng` modulator call, and the `fm`
  modulator call, which is now a separate site instead of one shared with
  `mix`. It is the baseline clone in every case.
- **New:** one call each to `calcNextOscSampleSyncBlock`,
  `calcNextOscSampleRingBlock` and `osc_buildSyncList`.
- **No new clone of any existing function** appears. The function grew by
  75 lines: per-block dispatch, not per-sample work.
- **Result:** `fm` and `mix` call the same compiled loops with the same
  arguments as before. D4 holds in the linked image, not only in the
  source.

### 12.5 The two implementation deviations

**D7 (`.itcm.s085`, `PADDING`): accepted.**
- **Placement:** `STM32F765VIHx_FLASH.ld:148–151` places `*(.itcm)` before
  `*(.itcm.*)` inside the one copied `.itcm` output section. The new code
  therefore follows all existing ITCM code, is copied at boot, and stays
  under the 16 KB `ASSERT`.
- **The one difference:** at `calcNextOscSampleBlock.constprop.0`, the
  baseline had one more trailing `.word` (268 vs 267 lines). All 14
  PC-relative loads in that clone are identical. An instruction that still
  referenced the dropped word would now point past the function, so the
  word was alignment padding.
- **The tolerance is narrow:** `strip_pool()` removes only a trailing run
  of `.word ADDR`. Every instruction, branch and interior pool entry is
  still compared.
- **Residual risk:** a future change that removes a referenced trailing
  literal would also change the load that uses it, so the gate would still
  report CHANGED.

**D8 (`osc_renderPlainWave()`, `noinline` dispatchers): accepted.**
- **Behaviour:** host cases R1 (interpolated carriers) and S6 (noise and
  user-sample slaves) compare it against `calcNextOscSampleBlock()` and
  pass, so its output equals `calcNextOscSampleBlock()` at gain 1.
- **In the linked image:** LTO builds `osc_renderPlainWave.constprop.0`
  (728 B ITCM). Its loop counts reproduce the baseline gain-1 clone
  `calcNextOscSampleBlock.constprop.1` loop for loop (22, 23, 23, 37, 23,
  70, 82, 10, 94/95), so it adds ITCM bytes but no per-sample cost.
- **What it buys:** it keeps the baseline clones untouched (§7 I1). The
  `noinline` dispatchers keep the interpolated blend loops in ITCM rather
  than in the flash-resident mixer.

### 12.6 Findings

| # | Severity | Finding | Fix |
|---|---|---|---|
| F1 | Low (documentation-in-place) | Three comment blocks in `Oscillator.c` describe the pre-D8 code. `osc_renderSyncWave()` (lines 1253–1258) says NOISE and user samples "render through calcNextOscSampleBlock() … That call repeats its own interpolation check". `calcPeriodicInterpSyncBlock()` (1309–1311) and `calcPeriodicInterpRingBlock()` (1568–1570) say "Placement: no INITCM attribute, like calcPeriodicInterpBlock()". | Replace with the text below. |
| F2 | Low | `STORAGE_SRAM_MANIFEST.md:16` writes `itcm.s085`. | `.itcm.s085` |
| F3 | Low | The linked user-sample ring copy measures **42/43**; the standalone build gives 40/41. So `rng` with user-sample oscillators is ≈ **84** in the image, not 82. The gate still holds: `fm` 90, a margin of 6. `INSTRUMENTS_DSP_REFERENCE.md` §8.1/§8.2 and this document's §1.2/§1.3 quote 40–41 / 82. | State "40–41 standalone, 42–43 linked; `rng` ≈ 84". |
| F4 | Low | `tools/dsp_test/DSP_TEST.md:209`: the expected `elf_osc_check` output omits the `PADDING` line (D7). | "`0 missing, 0 changed`; one `PADDING` line for `calcNextOscSampleBlock.constprop.0` (D7)". |

No correctness, CPU or RAM defect was found.

**F1 replacement text.**

`osc_renderSyncWave()`, the What paragraph:
```c
 * What:       dispatches Sin/Tri/Saw/Rec/Crash to their sync copies. NOISE
 *             and user samples render unsynced through osc_renderPlainWave()
 *             (the leaf loops at gain 1, plan A3/A4; D8). No interpolation
 *             check is needed here: the caller already took the
 *             interpolation path, or the partner copy has interpolation
 *             cleared.
```

`calcPeriodicInterpSyncBlock()` and `calcPeriodicInterpRingBlock()`, the
Placement line:
```c
 * Placement:  .itcm.s085 (OSC_S085_SECTION, D7); the compiler inlines it
 *             into its noinline ITCM dispatcher (OSC_S085_DISPATCH, D8).
```

### 12.7 Still open (unchanged from §11.5)

- **Device gate G-C and the listening matrix H1–H6.** These are the
  acceptance tests; everything above is build-time evidence.
- **The S084 hardware check V1–V7.**
- **The session close-out:** `085_SESSION_HANDOFF_LOG.md`, plus the
  `MEMORY.md` current-state and binding notes. `MEMORY.md` still describes
  the S084 state.
