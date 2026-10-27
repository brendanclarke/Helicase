# S085 P2 — Amp Envelope on Retrigger in `fre` / `itp`: Implementation Schedule

**Session:** 085, phase P2 (schedule; implemented 2026-10-27, see §11; hardware C9 pending)
**Branch:** `dev-ph7-voice_expansion` (S085 P1 uncommitted in the tree)
**Date:** 2026-10-27
**Line numbers:** read from the working tree on 2026-10-27, after the S085 P1
assessment fixes. They drift as edits land. Every change quotes its anchor
text: find the anchor before editing, and edit each file from the bottom up.

---

## 0. Read this first

### 0.1 Problem

The run modes `fre` and `itp` keep the oscillators and the filter running
through a retrigger, but every engine still restarts its **amp envelope** on
every trigger, in every run mode.

**Where the step comes from:** `slopeEg2_trigger()` (`SlopeEg2.c:55–71`)
leaves the envelope's raw value where it was and switches the state to attack.
The attack stage reads that raw value through a different curve:
- decay reads it as `f(v, slope)` (`SlopeEg2.c:136`);
- attack reads it as `f(v, invSlope)` (`SlopeEg2.c:127`).

The gain therefore jumps the moment the envelope enters attack.

With `f(v,k) = (1+k)v/(1+k|v|)` and the envelope halfway down its decay
(v = 0.5):

| Slope | Gain before | Gain after | Step |
|---:|---:|---:|---:|
| 0 | ≈ 0.00 | ≈ 1.00 | +1.0 |
| 32 | 0.25 | 0.75 | +0.50 |
| 64 | 0.50 | 0.50 | ≈ 0 |
| 96 | 0.76 | 0.24 | −0.51 |
| 127 | ≈ 1.00 | ≈ 0.00 | −1.0 |

**How each engine turns that into an audible step:**

| Engine | What happens at the retrigger block |
|---|---|
| Drum (`DrumVoice.c:268–278`) | Ramps its gain from the previous block to the new one, except when the attack increment is exactly 1.0 (attack setting 0) in attack or repeat. At attack 20–30 the step is ramped over one block (0.73 ms): that is the soft click heard on hardware. At attack 0 it is not ramped at all. |
| Snare, Cymbal, HiHat (`voicePost_mixAddGainDist()`, `voicePost_addGainDist()`) | Apply one constant gain per block, so the step lands on one sample. |

**Other steps at a retrigger:**
- **Velocity:** a new velocity steps the level as well, when `volumeMod` is
  on. Drum applies it as a separate per-block constant; Snare, Cymbal and
  HiHat multiply it into their constant gain.
- **Out of scope:**
  - The transient sample restarts on every trigger. It is the intended
    attack sound and is unchanged.
  - The pitch envelope (Drum, Snare) and the snap envelope also restart, but
    they change frequency, not amplitude. In `fre` the oscillator slew glides
    the jump over 160 samples; in `itp` the modulators glide and the
    carrier's jump is crossfaded. Neither makes a step.

### 0.2 Rules for this change (user, 2026-10-27)

- **`rst` stays exactly as it is.** Every new code path is armed only from a
  `fre`/`itp` trigger. In `rst` every new ramp runs flat (start value = end
  value), and the output is bit-identical to today. The `postchain` host
  test proves this (C7.1).
- **`fre`/`itp`: check whether the envelope has fully decayed, and ramp only
  if something is left.**
  - **Fully decayed:** the trigger starts a fresh attack, with no ramp.
  - **Something left:** the attack starts at the gain the voice is producing
    (gain match, C1), and the retrigger block ramps from the previous block's
    gain (and velocity) to the new one.
- **The ramp is one block** (32 samples, 0.73 ms; user).
- **Constant CPU:** the ramp arithmetic runs every sample of every block in
  every mode. Only its endpoints differ by mode.
- **RAM:** any growth needs explicit approval. This change grows four engine
  structs by 8 B each inside the fixed 1,176 B runtime slot (§0.5);
  approved by the user, 2026-10-27 (U4).

### 0.3 Design

**1. Gain match** (`slopeEg2_retriggerFrom()`, C1).
- On a `fre`/`itp` trigger the engine passes the gain it applied to its last
  block.
- The function inverts the attack curve: it sets the raw value v so that
  `f(v, invSlope)` equals that gain:

  ```
  g = (1+k)v/(1+kv)   =>   v = g / (1 + k(1 − g)),   k = invSlope
  ```
- **The denominator is always positive:** `invSlope` > −1, because
  `slopeEg2_setSlope()` caps the amount at ±0.9995. So 1 + k(1 − g) ≥ 1 + k
  ≥ 0.00025.
- **Result:** the attack starts at exactly the current level and rises at the
  attack rate. With the envelope fully decayed the gain is ≈ 0, so v ≈ 0: a
  fresh attack.
- **Repeat mode:** when `eg->repeat` > 0 the envelope jumps to full by design
  (`slopeEg2_trigger()` sets `value = 1`). The function keeps that
  behaviour, and the one-block ramp covers the jump.
- **Cost:** control rate, once per trigger: about 15–20 instructions,
  including one VDIV.

**2. "Something left" test** (`slopeEg2_isAudibleGain()`, C1).
- **Rule:** something is left when the gain the engine applied to its last
  block is at least one output step: `1/32768` (`SLOPEEG2_AUDIBLE_GAIN`).
- **What is tested:**
  - Drum: `ampFilterInput` (the end of its last ramp).
  - Snare, Cymbal, HiHat: `egValueOscVol` (their last block's gain).
- **Why not reuse `instrumentManager_ampEnvelopeQuiet()`**
  (`InstrumentManager.c:1516`): it tests the raw envelope value (≤ 0.0001)
  and the stopped state. The raw value is not the gain. At slope 127 a raw
  value of 0.0001 still gives a gain of about 0.29. A finished decay also
  sits at raw 0 in state `EG_D`, never `EG_STOPPED`.

**3. One-block ramp, armed per voice** (`ampRampArm`, C3–C6).
- **Arming:** the trigger sets `ampRampArm` only for `fre`/`itp` with
  something left.
- **The next rendered block** ramps from the previous block's applied gain to
  the new one:
  - Drum: it lets the existing per-block envelope ramp run at attack 0 too
    (C3.3), and ramps the velocity gain (C3.4).
  - Snare, Cymbal, HiHat: they ramp the whole amp gain, envelope × velocity
    (C4–C6).
- **Clearing:** the render clears the flag.
- **Every other block** passes equal endpoints (a flat ramp).

**4. Post-chain ramps** (C2).
- `voicePost_drum()` gains a velocity ramp.
- `voicePost_mixAddGainDist()` and `voicePost_addGainDist()` gain an
  amp-gain ramp.
- All three reuse `bufferTool_interpolatedGain()`
  (`BufferTools.h:96`), the helper Drum's envelope ramp already uses.
- With equal endpoints, `lastGain + frac·(gain − lastGain)` is exactly
  `gain`, so the output is bit-identical.

### 0.4 Cost

Measured with `osccost.py` on `arm-none-eabi-gcc -Ofast`, Cortex-M7, as the
worst-case instructions per sample of each post-chain loop:

| Loop | Today | After P2 | Change |
|---|---:|---:|---:|
| Drum `voicePost_drum()` (envelope ramp + velocity ramp) | 32 | 35 | +3 |
| Snare `voicePost_mixAddGainDist()` | 30 | 35 | +5 |
| Cymbal/HiHat `voicePost_addGainDist()` | 25 | 30 | +5 |

**Totals:**
- Per sample: at most +30 (six Snare/Cymbal/HiHat-class slots), about
  0.6 % of the 4,897-cycle sample period. The cost runs in every mode.
- Per trigger in `fre`/`itp`: one gain match, ≈ 15–20 instructions.
- Per block: Drum's async gains one flag test; each render gains one select
  and two stores.

C7.3 re-measures every row after the edit.

### 0.5 RAM (approved by the user, 2026-10-27, U4)

| Struct | Added | Today | After (estimate) | Free in the 1,176 B slot |
|---|---|---:|---:|---:|
| `DrumVoice` | `float lastVeloGain; uint8_t ampRampArm;` | 832 | 840 | 336 |
| `SnareVoice` | `float lastAmpGain; uint8_t ampRampArm;` | 664 | 672 | 504 |
| `CymbalVoice` | the same | 772 | 780 | 396 |
| `HiHatVoice` | the same | 780 | 788 | 388 |

- **Static RAM:** no change. The six runtime slots stay 7,056 B; the
  structs grow inside the fixed reserve, and the `_Static_assert`s in
  `InstrumentManager.c` keep guarding it.
- **Padding:** the byte counts include padding to 4 B. C7.4 measures them.

### 0.6 Decisions (approved by the user, 2026-10-27)

| # | Point | Approved | Not chosen |
|---|---|---|---|
| U1 | The "something left" test | **gain ≥ 1/32768 (one output step), on the gain the engine applied** | the raw-value test of `instrumentManager_ampEnvelopeQuiet()` (wrong for steep slopes, §0.3) |
| U2 | Where a `fre`/`itp` attack starts | **at the current gain, always** (≈ 0 when decayed, so a fresh attack) | when fully decayed, the old `slopeEg2_trigger()` (continues from the raw value, which at extreme slopes can jump to full) |
| U3 | Drum velocity ramp | **yes**, +3 per sample (otherwise a new velocity still steps the level when `volumeMod` is on) | no velocity ramp (Drum stays 32) |
| U4 | RAM: +8 B in each engine struct inside the fixed slot (§0.5) | **approved** | — |
| U5 | Snare/Cymbal/HiHat in `fre`/`itp` | **ramp only the retrigger block** | ramp every block in `fre`/`itp` |

### 0.7 Files

| File | Action | Change |
|---|---|---|
| `Core/DSPAudio/SlopeEg2.h` | Add | C1.1 |
| `Core/DSPAudio/SlopeEg2.c` | Add | C1.2 |
| `Core/DSPAudio/voicePostChain.h` | Modify (3 functions) | C2 |
| `Core/DSP/Instruments/Drum/DrumVoice.h` / `.c` | Add fields; modify init, trigger, async, render | C3 |
| `Core/DSP/Instruments/Snare/Snare.h` / `.c` | Add fields; modify init, trigger, render | C4 |
| `Core/DSP/Instruments/Cymbal/CymbalVoice.h` / `.c` | Add fields; modify init, trigger, render | C5 |
| `Core/DSP/Instruments/HiHat/HiHat.h` / `.c` | Add fields; modify init, trigger, render | C6 |
| `tools/dsp_test/test_postchain.c`, `armcheck/postchain.c`, `Makefile` | Modify | C7.1–C7.3 |
| `tools/dsp_test/test_amp_retrigger.c` | New | C7.2 |
| `tools/dsp_test/breakproofs.py` | Add breaks | C7.5 |
| Specifications, the 085 log, `MEMORY.md` | Modify | C8 |

### 0.8 Gates

| Gate | Checks |
|---|---|
| G1 | `make all` clean, no new warnings; `make img` |
| G2 | `postchain`: **0 differing samples** (flat ramps equal today's loops: `rst` unchanged) |
| G3 | `ampretrig` (new): 0 failures |
| G4 | `armcheck-postchain` reports 35 / 35 / 30 (C7.3); `armcheck-osc` unchanged |
| G5 | `breakproofs`: every break, old and new, fails its test |
| G6 | `osc_run`, `osc_s0`, `special_tags` and the rest of the suite unchanged |
| G7 | hardware matrix C9 |

---

## 1. C1 — `SlopeEg2`: gain-matched retrigger and the audible test

### C1.1 `Core/DSPAudio/SlopeEg2.h` — declarations

- **Where:** after line 70, `float slopeEg2_calcDecay(uint8_t data2);`, and
  before line 73, `#endif /* SLOPEEG2_H_ */`.
- **Action:** **Add.**

```c
/*
 * Smallest gain that still produces output (S085 P2).
 *
 * What:       one int16 output step, 1/32768. A voice whose last applied
 *             amp gain is below this is silent: its envelope has fully
 *             decayed for every practical purpose.
 * Why:        the fre/itp retrigger ramps only when something is left of the
 *             previous note (user, 2026-10-27). The test must use the applied
 *             gain, not the raw envelope value: at slope 127 a raw value of
 *             0.0001 still gives a gain of about 0.29, and a finished decay
 *             sits at raw 0 in EG_D, never EG_STOPPED.
 * Accessors:  slopeEg2_isAudibleGain().
 * Affiliates: instrumentManager_ampEnvelopeQuiet() (the Scene-swap test,
 *             which keeps its raw-value rule; not used here).
 */
#define SLOPEEG2_AUDIBLE_GAIN (1.0f / 32768.0f)

/*
 * Whether an applied amp gain is still audible (S085 P2).
 *
 * What:       1 when gain >= SLOPEEG2_AUDIBLE_GAIN, else 0.
 * Why:        arms the one-block retrigger ramp only when there is a level
 *             to ramp from.
 * Inputs:     the gain the engine applied to its last block (Drum
 *             ampFilterInput; Snare/Cymbal/HiHat egValueOscVol).
 * Outputs:    0 or 1.
 * Accessors:  the four engines' *_triggerVoice() in fre/itp.
 * Affiliates: slopeEg2_retriggerFrom(), each engine's ampRampArm.
 */
static inline uint8_t slopeEg2_isAudibleGain(const float gain)
{
	return (uint8_t)(gain >= SLOPEEG2_AUDIBLE_GAIN);
}

/*
 * Retrigger the attack from the gain the voice is producing (S085 P2).
 *
 * What:       sets the envelope to attack with its raw value chosen so the
 *             attack curve returns exactly gainNow:
 *             v = g / (1 + invSlope * (1 - g)), clamped to [0, 1].
 *             gainNow is clamped to [0, 1] first. In repeat mode it falls
 *             back to slopeEg2_trigger(), which starts at full by design.
 * Why:        slopeEg2_trigger() keeps the raw value but switches from the
 *             decay curve f(v, slope) to the attack curve f(v, invSlope), so
 *             the gain steps by up to the full range at a retrigger (half a
 *             decay at slope 96: 0.76 -> 0.24). Matching the gain removes
 *             the step at every attack setting above 0; at attack 0 the
 *             first calc reaches 1.0 and the engines' one-block ramp covers
 *             that jump. A fully decayed voice passes gain ~0 and gets a
 *             fresh attack.
 * Inputs:     eg (attack, invSlope, repeat), gainNow (the last applied
 *             gain).
 * Outputs:    eg->value, eg->state = EG_A (or EG_REPEAT), eg->repeatCnt.
 * Cost:       control rate, once per fre/itp trigger: one VDIV and a few
 *             compares.
 * Accessors:  Drum_triggerVoice(), Snare_triggerVoice(),
 *             Cymbal_triggerVoice(), HiHat_triggerVoice() when the run mode
 *             is fre or itp. rst keeps slopeEg2_trigger().
 * Affiliates: slopeEg2_calc() (the curve this inverts),
 *             slopeEg2_setSlope() (invSlope > -1 keeps the denominator
 *             positive), slopeEg2_isAudibleGain().
 */
void slopeEg2_retriggerFrom(SlopeEg2* eg, float gainNow);
```

### C1.2 `Core/DSPAudio/SlopeEg2.c` — definition

- **Where:** after line 71, the closing `}` of `slopeEg2_trigger()`, and its
  separator line 72.
- **Action:** **Add.**

```c
/*
 * Retrigger the attack from the gain the voice is producing (S085 P2;
 * declared in SlopeEg2.h).
 *
 * Implementation note: the attack stage returns
 * f(v, k) = (1+k)v / (1+k|v|) with k = invSlope (slopeEg2_calc(), EG_A).
 * For v >= 0, solving g = f(v, k) for v gives v = g / (1 + k(1 - g)).
 * slopeEg2_setSlope() keeps the amount in +-0.9995, so invSlope > -1 and
 * the denominator is at least 1 + invSlope >= 0.00025 (never zero or
 * negative). The clamps keep the raw value in the range slopeEg2_calc()
 * expects.
 */
void slopeEg2_retriggerFrom(SlopeEg2* eg, float gainNow)
{
	float v;

	if (eg->repeat) {
		/* Repeat mode starts at full by design; the engines ramp the jump. */
		slopeEg2_trigger(eg);
		return;
	}
	if (gainNow < 0.0f)
		gainNow = 0.0f;
	if (gainNow > 1.0f)
		gainNow = 1.0f;
	v = gainNow / (1.0f + eg->invSlope * (1.0f - gainNow));
	if (v < 0.0f)
		v = 0.0f;
	if (v > 1.0f)
		v = 1.0f;
	eg->value = v;
	eg->state = EG_A;
	eg->repeatCnt = eg->repeat;
}
//--------------------------------------------------
```

---

## 2. C2 — `voicePostChain.h`: ramped endpoints

**What changes:** each fused post-chain gains a "last" argument and ramps
from it to the current value across the block, with the same
`bufferTool_interpolatedGain()` arithmetic Drum's envelope ramp already
uses.

**Why `rst` is unchanged:** with equal endpoints the ramp returns the
current value exactly, so passing last = current reproduces today's output
(C7.1). The four engines are the only firmware callers. The test bench
callers are updated in C7.1 and C7.3.

### C2.1 `voicePost_drum()` — velocity ramp (lines 21–39)

- **Where:** the comment block at lines 21–25 and the function at lines
  26–39.
- **Action:** **Modify** (replace both).

```c
/*
 * Drum amp ramp -> velocity ramp -> distortion (S073 Step 4; velocity ramp
 * S085 P2).
 *
 * What:       per sample: buf x (lastGain -> gain ramp), truncated to int16;
 *             x (lastVeloGain -> veloGain ramp), truncated; distortion.
 *             Both ramps share frac = i / (size - 1).
 * Why:        S073 fused the passes, keeping every truncation point. S085 P2
 *             adds the velocity ramp: a fre/itp retrigger with a new velocity
 *             (volumeMod on) otherwise steps the level on the first sample of
 *             the block. With lastVeloGain == veloGain the ramp returns
 *             veloGain exactly, so rst and every non-retrigger block are
 *             bit-identical to the S073 loop.
 * Inputs:     filtered block; envelope gain endpoints (gain, lastGain);
 *             velocity gain endpoints (veloGain, lastVeloGain; 1.0 when
 *             volumeMod is off); distortion; size.
 * Outputs:    buf (pre-volume).
 * Cost:       35 instructions per sample (was 32).
 * Accessors:  Drum_calcVoiceSyncBlock(); tools/dsp_test postchain and
 *             armcheck-postchain.
 * Affiliates: bufferTool_interpolatedGain(), DrumVoice lastGain/
 *             ampFilterInput/lastVeloGain/ampRampArm, distortion.h.
 */
static inline void voicePost_drum(int16_t *buf, const float gain,
		const float lastGain, const float veloGain,
		const float lastVeloGain, const Distortion *dist,
		const uint8_t size)
{
	uint8_t i;
	const float inv_size = 1.f/(size-1.f);
	for(i=0;i<size;i++)
	{
		const int16_t eg = bufferTool_floatToInt16Store(
			buf[i] * bufferTool_interpolatedGain(i, inv_size, gain, lastGain));
		const int16_t vel = bufferTool_floatToInt16Store(
			eg * bufferTool_interpolatedGain(i, inv_size, veloGain,
			                                 lastVeloGain));
		buf[i] = distortion_curveSample16(dist, vel);
	}
}
```

### C2.2 `voicePost_mixAddGainDist()` — amp ramp (lines 41–59)

- **Where:** the comment block at lines 41–46 and the function at lines
  47–59.
- **Action:** **Modify** (replace both).

```c
/*
 * Snare mix -> saturating add -> amp ramp -> distortion (S073 Step 4; amp
 * ramp S085 P2).
 *
 * What:       per sample: buf x mix, truncated; saturating add of the tonal
 *             block; x (lastAmpGain -> ampGain ramp), truncated; distortion.
 * Why:        S073 fused the passes. S085 P2 ramps the amp gain so a fre/itp
 *             retrigger with level left moves from the previous block's gain
 *             (envelope x velocity) to the new one over the block instead of
 *             on one sample. With lastAmpGain == ampGain the ramp returns
 *             ampGain exactly, so rst and every non-retrigger block are
 *             bit-identical to the S073 loop.
 * Inputs:     filtered noise block, tonal block (add), mix, amp gain
 *             endpoints (ampGain, lastAmpGain), distortion, size.
 * Outputs:    buf (pre-volume).
 * Cost:       35 instructions per sample (was 30).
 * Accessors:  Snare_calcSyncBlockVoice(); tools/dsp_test postchain and
 *             armcheck-postchain.
 * Affiliates: bufferTool_interpolatedGain(), SnareVoice lastAmpGain/
 *             ampRampArm.
 */
static inline void voicePost_mixAddGainDist(int16_t *buf, const int16_t *add,
		const float mix, const float ampGain, const float lastAmpGain,
		const Distortion *dist, const uint8_t size)
{
	uint8_t j;
	const float inv_size = 1.f/(size-1.f);
	for(j=0;j<size;j++)
	{
		const int16_t m = bufferTool_floatToInt16Store(buf[j] * mix);
		const int16_t s = bufferTool_satAdd16(m, add[j]);
		const int16_t g = bufferTool_floatToInt16Store(
			s * bufferTool_interpolatedGain(j, inv_size, ampGain,
			                                lastAmpGain));
		buf[j] = distortion_curveSample16(dist, g);
	}
}
```

### C2.3 `voicePost_addGainDist()` — amp ramp (lines 61–77)

- **Where:** the comment block at lines 61–66 and the function at lines
  67–77.
- **Action:** **Modify** (replace both).

```c
/*
 * Cymbal/HiHat saturating add -> amp ramp -> distortion (S073 Step 4; amp
 * ramp S085 P2).
 *
 * What:       per sample: saturating add of the transient block; x
 *             (lastAmpGain -> ampGain ramp), truncated; distortion.
 * Why:        as voicePost_mixAddGainDist(): the retrigger block ramps
 *             from the previous block's gain; equal endpoints are
 *             bit-identical to the S073 loop.
 * Inputs:     filtered block, transient block (add), amp gain endpoints,
 *             distortion, size.
 * Outputs:    buf (pre-volume).
 * Cost:       30 instructions per sample (was 25).
 * Accessors:  Cymbal_calcSyncBlockVoice(), HiHat_calcSyncBlockVoice();
 *             tools/dsp_test postchain and armcheck-postchain.
 * Affiliates: bufferTool_interpolatedGain(), CymbalVoice/HiHatVoice
 *             lastAmpGain/ampRampArm.
 */
static inline void voicePost_addGainDist(int16_t *buf, const int16_t *add,
		const float ampGain, const float lastAmpGain,
		const Distortion *dist, const uint8_t size)
{
	uint8_t j;
	const float inv_size = 1.f/(size-1.f);
	for(j=0;j<size;j++)
	{
		const int16_t s = bufferTool_satAdd16(buf[j], add[j]);
		const int16_t g = bufferTool_floatToInt16Store(
			s * bufferTool_interpolatedGain(j, inv_size, ampGain,
			                                lastAmpGain));
		buf[j] = distortion_curveSample16(dist, g);
	}
}
```

**Also update** the header's file comment (lines 1–12). Add after its
`Affiliates:` line:

```c
 * S085 P2:    every chain ramps its gain from a "last" value; equal
 *             endpoints are the S073 loop exactly (rst unchanged).
```

---

## 3. C3 — Drum

### C3.1 `DrumVoice.h` — new fields

- **Where:** inside `#if (AMP_EG_SYNC==0)`, after line 101,
  `float lastGain;`.
- **Action:** **Add.**

```c
	/*
	 * Retrigger ramp state (S085 P2).
	 *
	 * What:       lastVeloGain = the velocity gain the post-chain applied to
	 *             the previous block (velo, or 1.0 with volumeMod off);
	 *             ampRampArm = 1 for the one block after a fre/itp trigger
	 *             that found the voice still audible.
	 * Why:        that block ramps the envelope gain (even at attack 0) and
	 *             the velocity gain from the previous block's values, so the
	 *             retrigger has no step. Every other block uses equal
	 *             endpoints (rst is unchanged).
	 * Writers:    Drum_triggerVoice() (arm), Drum_calcVoiceSyncBlock()
	 *             (lastVeloGain every block; clears the arm),
	 *             Drum_initVoice().
	 * Readers:    Drum_calcVoiceAsync(), Drum_calcVoiceSyncBlock().
	 * Affiliates: voicePost_drum(), slopeEg2_isAudibleGain(); the 1,176 B
	 *             slot _Static_assert in InstrumentManager.c.
	 */
	float lastVeloGain;
	uint8_t ampRampArm;
```

### C3.2 `DrumVoice.c` — init

- **Where:** after line 135, `slopeEg2_init(&voice->oscVolEg);`.
- **Action:** **Add.**

```c
	/* S085 P2: no retrigger ramp pending; the first block starts flat. */
	voice->lastVeloGain = 1.0f;
	voice->ampRampArm = 0u;
```

### C3.3 `DrumVoice.c` — trigger: gain-matched attack in `fre`/`itp`

- **Where:** line 226, `slopeEg2_trigger(&voice->oscVolEg);`.
- **Action:** **Modify** (replace that one line).

```c
	/*
	 * Amp envelope retrigger by run mode (S085 P2).
	 *
	 * What:       rst: slopeEg2_trigger(), exactly as before. fre/itp: the
	 *             attack starts at the gain the last block ended on
	 *             (ampFilterInput; slopeEg2_retriggerFrom()), and when that
	 *             gain is still audible the next block ramps from it
	 *             (ampRampArm). A fully decayed voice gets a fresh attack and
	 *             no ramp.
	 * Why:        slopeEg2_trigger() switches the raw value from the decay to
	 *             the attack curve, a gain step of up to full scale; at
	 *             attack 0 the async also skipped the per-block ramp. That
	 *             was the retrigger click in fre/itp.
	 * Inputs:     section.runMode, ampFilterInput, the envelope.
	 * Outputs:    oscVolEg, ampRampArm.
	 * Affiliates: Drum_calcVoiceAsync() (ramp at attack 0 when armed),
	 *             Drum_calcVoiceSyncBlock() (velocity ramp),
	 *             slopeEg2_retriggerFrom(), slopeEg2_isAudibleGain().
	 */
#if (AMP_EG_SYNC != 0)
#error "S085 P2 amp retrigger assumes AMP_EG_SYNC == 0 (ampFilterInput)"
#endif
	if (voice->section.runMode == OSC_RUN_RST) {
		slopeEg2_trigger(&voice->oscVolEg);
	} else {
		const float gainNow = voice->ampFilterInput;

		slopeEg2_retriggerFrom(&voice->oscVolEg, gainNow);
		voice->ampRampArm = slopeEg2_isAudibleGain(gainNow);
	}
```

The next line, `voice->velo = vol/127.f;` (227), stays. The new velocity
takes effect in the next block, ramped from `lastVeloGain` when armed
(C3.5).

### C3.4 `DrumVoice.c` — async: keep the ramp at attack 0 when armed

- **Where:** line 268, the condition
  `if( (voice->oscVolEg.attack == 1 ) && ((voice->oscVolEg.state == EG_A) || (voice->oscVolEg.state == EG_REPEAT)) )`.
- **Action:** **Modify** (the condition only; both branches unchanged).

```c
	/*
	 * S085 P2: an armed fre/itp retrigger block always ramps from the last
	 * gain, so attack 0 (and a repeat-mode restart) does not jump to full in
	 * one sample. Unarmed blocks keep the original rule exactly.
	 */
	if( (voice->oscVolEg.attack == 1 ) &&
	    ((voice->oscVolEg.state == EG_A) || (voice->oscVolEg.state == EG_REPEAT)) &&
	    !voice->ampRampArm )
```

### C3.5 `DrumVoice.c` — render: velocity ramp, clear the arm

- **Where:** lines 384–386, the call
  `voicePost_drum(buf, voice->ampFilterInput, voice->lastGain, voice->volumeMod ? voice->velo : 1.0f, &voice->distortion, size);`.
- **Action:** **Modify** (replace the call). The S073 comment block above it
  stays; add this block after it.

```c
	/*
	 * Velocity ramp endpoints and the arm (S085 P2).
	 *
	 * What:       the block's velocity gain ramps from lastVeloGain when
	 *             this is the armed retrigger block, else it is flat. After
	 *             the block, lastVeloGain records the gain just applied and
	 *             the arm is cleared (one block only).
	 * Why:        a new velocity at a fre/itp retrigger with level left would
	 *             step the output; rst and ordinary blocks stay bit-identical.
	 * Inputs:     volumeMod, velo, lastVeloGain, ampRampArm.
	 * Outputs:    buf; lastVeloGain; ampRampArm = 0.
	 * Affiliates: voicePost_drum(), Drum_triggerVoice().
	 */
	{
		const float veloGain = voice->volumeMod ? voice->velo : 1.0f;
		const float lastVeloGain = voice->ampRampArm
			? voice->lastVeloGain : veloGain;

		voicePost_drum(buf, voice->ampFilterInput, voice->lastGain,
		               veloGain, lastVeloGain, &voice->distortion, size);
		voice->lastVeloGain = veloGain;
		voice->ampRampArm = 0u;
	}
```

- **Legacy branch:** the `#if defined(USE_AMP_FILTER) || (USE_FILTER_DRIVE != 0)`
  branch (lines 359–368) is compiled out in every build and is not changed.
  It also never clears `ampRampArm`, so it must not be enabled without
  revisiting this.
- **Ordering in a block:** the trigger is processed before the mixer; then
  `calcSlotAsync()` reads the arm (C3.4); then the render reads and clears
  it (C3.5). The arm is never seen by a later block.

---

## 4. C4 — Snare

### C4.1 `Snare.h` — new fields

- **Where:** after line 94, `float egValueOscVol;`.
- **Action:** **Add.**

```c
	/*
	 * Retrigger ramp state (S085 P2).
	 *
	 * What:       lastAmpGain = the amp gain (envelope x velocity when
	 *             volumeMod is on) the post-chain applied to the previous
	 *             block; ampRampArm = 1 for the one block after a fre/itp
	 *             trigger that found the voice still audible.
	 * Why:        that block ramps from the previous gain, so neither the
	 *             envelope restart nor a new velocity steps the output; all
	 *             other blocks use equal endpoints (rst unchanged).
	 * Writers:    Snare_triggerVoice() (arm), Snare_calcSyncBlockVoice()
	 *             (lastAmpGain every block; clears the arm),
	 *             Snare_initVoice().
	 * Readers:    Snare_calcSyncBlockVoice().
	 * Affiliates: voicePost_mixAddGainDist(), slopeEg2_isAudibleGain(); the
	 *             1,176 B slot _Static_assert in InstrumentManager.c.
	 */
	float lastAmpGain;
	uint8_t ampRampArm;
```

### C4.2 `Snare.c` — init

- **Where:** after line 105, `slopeEg2_init(&voice->oscVolEg);`.
- **Action:** **Add.**

```c
	/* S085 P2: no retrigger ramp pending. */
	voice->lastAmpGain = 0.0f;
	voice->ampRampArm = 0u;
```

### C4.3 `Snare.c` — trigger

- **Where:** line 175, `slopeEg2_trigger(&voice->oscVolEg);`.
- **Action:** **Modify** (replace that line).

```c
	/*
	 * Amp envelope retrigger by run mode (S085 P2).
	 *
	 * What:       rst: slopeEg2_trigger(), exactly as before. fre/itp: the
	 *             attack starts at the last block's envelope gain
	 *             (egValueOscVol) and, when audible, the next block ramps
	 *             from the previous amp gain (ampRampArm).
	 * Why:        the decay -> attack curve switch and the per-block constant
	 *             gain stepped the output on the retrigger sample.
	 * Inputs:     section.runMode, egValueOscVol, the envelope.
	 * Outputs:    oscVolEg, ampRampArm.
	 * Affiliates: Snare_calcSyncBlockVoice(), slopeEg2_retriggerFrom(),
	 *             slopeEg2_isAudibleGain().
	 */
	if (voice->section.runMode == OSC_RUN_RST) {
		slopeEg2_trigger(&voice->oscVolEg);
	} else {
		const float gainNow = voice->egValueOscVol;

		slopeEg2_retriggerFrom(&voice->oscVolEg, gainNow);
		voice->ampRampArm = slopeEg2_isAudibleGain(gainNow);
	}
```

### C4.4 `Snare.c` — render

- **Where:** lines 284–290, the block
  `const float ampGain = …; voicePost_mixAddGainDist(buf, transBuf, voice->mix, ampGain, &voice->distortion, size);`.
- **Action:** **Modify** (replace the block contents). The S073 comment
  above it stays; add this block before the code.

```c
	/*
	 * Amp ramp endpoints and the arm (S085 P2).
	 *
	 * What:       ampGain as before (velo x envelope, or the envelope). The
	 *             armed retrigger block ramps from lastAmpGain; other blocks
	 *             are flat. Afterwards lastAmpGain records ampGain and the
	 *             arm is cleared.
	 * Why:        removes the retrigger step in fre/itp; rst and ordinary
	 *             blocks stay bit-identical.
	 * Inputs:     volumeMod, velo, egValueOscVol, lastAmpGain, ampRampArm.
	 * Outputs:    buf; lastAmpGain; ampRampArm = 0.
	 * Affiliates: voicePost_mixAddGainDist(), Snare_triggerVoice().
	 */
	{
		const float ampGain = voice->volumeMod
			? voice->velo * voice->egValueOscVol
			: voice->egValueOscVol;
		const float lastAmpGain = voice->ampRampArm
			? voice->lastAmpGain : ampGain;

		voicePost_mixAddGainDist(buf, transBuf, voice->mix, ampGain,
		                         lastAmpGain, &voice->distortion, size);
		voice->lastAmpGain = ampGain;
		voice->ampRampArm = 0u;
	}
```

---

## 5. C5 — Cymbal

### C5.1 `CymbalVoice.h` — new fields

- **Where:** after line 87, `float egValueOscVol;`.
- **Action:** **Add** the C4.1 block with these names changed:
  - `Snare_triggerVoice()` → `Cymbal_triggerVoice()`;
  - `Snare_calcSyncBlockVoice()` → `Cymbal_calcSyncBlockVoice()`;
  - `Snare_initVoice()` → `Cymbal_initVoice()`;
  - `voicePost_mixAddGainDist()` → `voicePost_addGainDist()`.

### C5.2 `CymbalVoice.c` — init

- **Where:** after line 115, `slopeEg2_init(&voice->oscVolEg);`.
- **Action:** **Add** the C4.2 lines.

### C5.3 `CymbalVoice.c` — trigger

- **Where:** line 196, `slopeEg2_trigger(&voice->oscVolEg);`.
- **Action:** **Modify** with the C4.3 code and comment. In the comment,
  change `Snare_calcSyncBlockVoice()` to `Cymbal_calcSyncBlockVoice()`.

### C5.4 `CymbalVoice.c` — render

- **Where:** lines 299–302, the block
  `const float ampGain = …; voicePost_addGainDist(buf, mod, ampGain, &voice->distortion, size);`.
- **Action:** **Modify.**

```c
	/*
	 * Amp ramp endpoints and the arm (S085 P2).
	 *
	 * What:       as Snare (C4.4): the armed retrigger block ramps from
	 *             lastAmpGain; other blocks are flat; then record and clear.
	 * Why:        removes the retrigger step in fre/itp; rst unchanged.
	 * Inputs:     volumeMod, velo, egValueOscVol, lastAmpGain, ampRampArm.
	 * Outputs:    buf; lastAmpGain; ampRampArm = 0.
	 * Affiliates: voicePost_addGainDist(), Cymbal_triggerVoice().
	 */
	{
		const float ampGain = voice->volumeMod
			? voice->velo * voice->egValueOscVol
			: voice->egValueOscVol;
		const float lastAmpGain = voice->ampRampArm
			? voice->lastAmpGain : ampGain;

		voicePost_addGainDist(buf, mod, ampGain, lastAmpGain,
		                      &voice->distortion, size);
		voice->lastAmpGain = ampGain;
		voice->ampRampArm = 0u;
	}
```

---

## 6. C6 — HiHat

### C6.1 `HiHat.h` — new fields

- **Where:** after line 93, `float egValueOscVol;`.
- **Action:** **Add** the C4.1 block with the names changed to the `HiHat_`
  functions and `voicePost_addGainDist()`.

### C6.2 `HiHat.c` — init

- **Where:** after line 128,
  `voice->decayOpen = voice->oscVolEg.decay;`. That is after the envelope
  init (line 126) and the decay caches (127–128).
- **Action:** **Add** the C4.2 lines.

### C6.3 `HiHat.c` — trigger

- **Where:** line 202, `slopeEg2_trigger(&voice->oscVolEg);`. It comes
  after line 200 selects the open or closed decay; keep that order.
- **Action:** **Modify** with the C4.3 code. Comment as C4.3, with
  `HiHat_calcSyncBlockVoice()`, plus this note:

```c
	 * Note:       the open/closed decay is selected first (line above), so
	 *             the matched attack decays at the new note's rate; switching
	 *             the decay rate does not step the level.
```

### C6.4 `HiHat.c` — render

- **Where:** lines 310–313, the block
  `const float ampGain = …; voicePost_addGainDist(buf, mod1, ampGain, &voice->distortion, size);`.
- **Action:** **Modify** as C5.4, with `mod1` and the `HiHat_` names.

---

## 7. C7 — Tests and probes

### C7.1 `tools/dsp_test/test_postchain.c` — `rst` stays bit-identical

- **Where:** in `compare_engine()`, the four calls at lines 97–115.
- **Action:** **Modify.** Pass equal endpoints, so the comparison against
  the frozen S073 passes still requires **0 differing samples**:

```c
        voicePost_drum(new_buf, gain, last_gain,
                       volume_mod ? velo : 1.0f,
                       volume_mod ? velo : 1.0f,   /* S085 P2: flat ramp */
                       &new_voice.distortion, OUTPUT_DMA_SIZE);
...
        voicePost_mixAddGainDist(new_buf, new_add, mix,
                                 volume_mod ? velo * gain : gain,
                                 volume_mod ? velo * gain : gain,
                                 &new_voice.distortion, OUTPUT_DMA_SIZE);
...
        voicePost_addGainDist(new_buf, new_add,
                              volume_mod ? velo * gain : gain,
                              volume_mod ? velo * gain : gain,
                              &new_voice.distortion, OUTPUT_DMA_SIZE);
        (both Cymbal and HiHat calls)
```

Add to the file's header comment:

```c
 * S085 P2:    the chains now take "last" endpoints; equal endpoints must
 *             reproduce the frozen passes exactly (the rst guarantee).
```

### C7.2 `tools/dsp_test/test_amp_retrigger.c` (new) and target `ampretrig`

- **File:** `tools/dsp_test/test_amp_retrigger.c` (**New file**).
- **Compiles:** the real `Core/DSPAudio/SlopeEg2.c` and the real
  `voicePostChain.h`, with `prelude_osc.h` and the `hostcompat/` include
  path, as `osc_run` does.

```c
/*
 * Amp envelope retrigger and post-chain ramp checks (S085 P2).
 *
 * What:       1. gain match: for slope 0..127 (step 1), gains
 *                {0, 1/32768, 0.01, 0.1, 0.25, 0.5, 0.75, 0.99, 1} and
 *                attack settings {1, 20, 30, 64, 127}:
 *                slopeEg2_retriggerFrom(g) then the attack curve at the new
 *                raw value (slopeEg2_calcSlopeValue(value, invSlope)) equals
 *                g within 1e-5; the following slopeEg2_calc() outputs are
 *                non-decreasing until the envelope reaches EG_D (no dip
 *                after the retrigger); state is EG_A.
 *             2. repeat mode: repeat > 0 gives value 1 and EG_REPEAT (the
 *                documented fallback).
 *             3. audible test: slopeEg2_isAudibleGain() is 0 below 1/32768
 *                and 1 at and above it.
 *             4. ramp wiring: on a constant input block, with distortion
 *                shape 0, voicePost_addGainDist() and
 *                voicePost_mixAddGainDist() with last != current produce
 *                sample 0 at the last gain and sample 31 at the current
 *                gain, monotonic in between; voicePost_drum() does the same
 *                for its velocity endpoints. The reference values come from
 *                the same truncation steps, written out in the test.
 *             5. no step: a simulated Snare-style sequence (decay at slope
 *                96 to gain ~0.76, fre retrigger at attack 25, the armed
 *                ramp block, then three attack blocks) has a largest
 *                sample-to-sample gain change no bigger than one block's
 *                normal attack increment.
 * Why:        the P2 gate; every check fails on a broken change (C7.5).
 * Outputs:    "N failures"; nonzero exit on failure.
 * Accessors:  make -C tools/dsp_test ampretrig.
 * Affiliates: SlopeEg2.c, voicePostChain.h, test_postchain.c.
 */
```

**`Makefile` target** (after `osc_s0`):

```make
# S085 P2: gain-matched amp retrigger and the post-chain ramps.
ampretrig: | $(GEN)
	$(HOSTCC) $(OSCFLAGS) test_amp_retrigger.c \
	  $(ROOT)/Core/DSPAudio/SlopeEg2.c $(ROOT)/Core/DSPAudio/distortion.c \
	  -o build/ampretrig -lm
	./build/ampretrig
```

Also add `ampretrig` to `.PHONY`. `slopeEg2_calcSlopeValue()` is non-static
in `SlopeEg2.c` without a declaration; the test declares it `extern`.

### C7.3 `tools/dsp_test/armcheck/postchain.c` and `Makefile` — cost report

- **`armcheck/postchain.c`, lines 36–66:**
  - `new_snare()` and `new_addgain()` pass `ampGain` twice;
  - `new_drum()` passes the velocity gain twice;
  - add `ramp_snare()`, `ramp_addgain()` and `ramp_drum()` wrappers that
    take separate last values (the firmware's worst case).
- **`Makefile` lines 268–273:**
  - The three `$(FP)` comparisons now differ by design: the ramp adds value
    operations even when its endpoints are equal at run time. Add
    `--report` to each.
  - Add `python3 osccost.py --obj build/ac_postchain.o --func new_drum
    --func new_snare --func new_addgain --func ramp_drum --func ramp_snare
    --func ramp_addgain`.
- **Expected:** 35 / 35 / 30. Record the figures in C8.
- **What carries the S0 proof now:** the host `postchain` test (C7.1). The
  ARM check becomes a cost report for these three loops.

### C7.4 Engine sizes

- **Run:** the `sizeof` probe from the S085 assessment (§22 A10:
  `arm-none-eabi-gcc` with the firmware include paths, `nm -S`).
- **Record:** the measured sizes in `STORAGE_SRAM_MANIFEST.md`.
- **Expected:** +8 B each.

### C7.5 `tools/dsp_test/breakproofs.py` — new breaks

Add to `breaks`:

| ID | Defect | Target | Must fail |
|---|---|---|---|
| B27 | `slopeEg2_retriggerFrom()` inverts with `eg->slope` instead of `eg->invSlope` | `ampretrig` | gain match |
| B28 | `slopeEg2_retriggerFrom()` keeps the old raw value (no match) | `ampretrig` | gain match, no step |
| B29 | `voicePost_addGainDist()` ignores `lastAmpGain` (ramp from `ampGain`) | `ampretrig` | ramp wiring |
| B30 | `voicePost_drum()` velocity ramp with endpoints swapped | `ampretrig` | ramp wiring |
| B31 | `SLOPEEG2_AUDIBLE_GAIN` set to 0.01 | `ampretrig` | audible test |
| B32 | `voicePost_mixAddGainDist()` ramps the wrong way (`gain` ↔ `lastAmpGain`) | `postchain` | 0 differing (flat calls still pass) **and** `ampretrig` ramp wiring |

---

## 8. C8 — Documentation

| Document | Update |
|---|---|
| `INSTRUMENTS_DSP_REFERENCE.md` §5.1–§5.4 | Engine triggers: rst unchanged; fre/itp gain-matched attack with a one-block ramp when audible. Post-chains: the ramps. |
| `INSTRUMENTS_DSP_REFERENCE.md` §12.6 | New subsection "Amp envelope on a fre/itp retrigger": the problem table (§0.1), the inverse formula, the audible test, the arm lifecycle, repeat mode, what still restarts (transient, pitch and snap envelopes). |
| `INSTRUMENTS_DSP_REFERENCE.md` §8.1, §12.11 | Post-chain rows: Drum 32 → 35, Snare 30 → 35, Cymbal/HiHat 25 → 30; per-voice worst cases +3/+5. |
| `CPU_USE_DSP_AUDIT.md` "S085 oscillator cost record" | The P2 post-chain costs and the system total (+30 at most). |
| `STORAGE_SRAM_MANIFEST.md` "S085" | The measured engine sizes after P2. |
| `tools/dsp_test/DSP_TEST.md` §9 | `ampretrig`; the changed `postchain`/`armcheck-postchain` meaning; B27–B32. |
| `085_SESSION_HANDOFF_LOG.md` | Add "§17 S085 P2: amp envelope on retrigger" (the problem, the decisions U1–U5, the changes, the costs, the RAM, the tests, the hardware results). |
| `MEMORY.md` "Oscillator rules (S085)" | Add a bullet: "fre/itp retrigger: attack starts at the current gain (`slopeEg2_retriggerFrom()`), one-block ramp only when audible (≥ 1/32768); rst calls `slopeEg2_trigger()` unchanged". |

---

## 9. C9 — Hardware matrix (user)

| # | Check | Pass |
|---|---|---|
| P2-H1 | Drum, `fre` and `itp`, retrigger halfway through the decay at attack 0, 20, 30, 64 and slope 0, 32, 64, 96, 127 | no click; the level continues and rises with the attack |
| P2-H2 | the same with Snare, Cymbal and HiHat (HiHat open and closed) | no click |
| P2-H3 | retrigger after the sound has fully decayed, in `fre`/`itp` | a normal attack from silence |
| P2-H4 | `volumeMod` on, alternating velocities 30/127 on fast retriggers in `fre`/`itp` | the level changes without a click |
| P2-H5 | amp envelope repeat mode on, `fre`/`itp` retrigger | the jump to full is ramped (soft onset), no click |
| P2-H6 | `rst` A/B against the build before P2, every engine | sounds identical |
| P2-H7 | worst-case Scene (S085 H11) | no underruns; `cpu` figure within about +0.6 % of the S085 figure |
| P2-H8 | what still restarts on a fre/itp retrigger, by design | the transient sample restarts (the intended attack sound); set the transient off to hear the envelope alone. The pitch envelope (Drum, Snare) and the snap envelope also restart, but their pitch jump is not a step: in `fre` every oscillator's increment glides to the new pitch over 160 samples (the slew re-targets each block, so the glide merges into the new sweep); in `itp` the modulators glide and the carrier's jump lies inside the 132-sample crossfade; the phase never jumps, so there is no amplitude step |

---

## 10. Order of work

1. **C1** (`SlopeEg2`), then the `ampretrig` checks 1–3 (C7.2).
2. **C2** (post-chains) with **C7.1**: `postchain` must report 0 differing
   samples before any engine change.
3. **C3–C6** (engines), then `make all`, then **C7.3** (cost report) and
   **C7.4** (sizes).
4. **C7.2** checks 4–5, then **C7.5** (`make breakproofs`).
5. **C8**, then **C9** on hardware.

---

## 11. Implementation log (S085 P2, 2026-10-27)

Notes are added as each step lands. The schedule above is followed as
written; any deviation is listed here with its reason.

### 11.0 Baseline (before any edit)

- `make -C tools/dsp_test postchain`: **0 differing samples**.
- `armcheck-postchain`: three `fpseq.py` MATCH results (Drum 16/16, Snare
  15/15, Cymbal/HiHat 12/12).
- `osccost.py` on `build/ac_postchain.o`: `new_drum` 32, `new_snare` 30,
  `new_addgain` 25 worst instructions per sample, one VDIV each. These are
  the "Today" figures in §0.4.

### 11.1 C1 — `SlopeEg2` (done)

- `SlopeEg2.h`: `SLOPEEG2_AUDIBLE_GAIN`, `slopeEg2_isAudibleGain()` and
  the `slopeEg2_retriggerFrom()` declaration added after
  `slopeEg2_calcDecay()`, with the C1.1 comment blocks.
- `SlopeEg2.c`: `slopeEg2_retriggerFrom()` added after `slopeEg2_trigger()`
  with the C1.2 comment block. Code as scheduled.

### 11.2 C7.2 checks 1–3 (done) — one deviation

- `tools/dsp_test/test_amp_retrigger.c` created with checks 1–3, built with
  the `OSCFLAGS` include set against the real `SlopeEg2.c`.
- **Deviation (tolerance of check 1):** with the flat 1e-5 of C7.2 the
  check failed only at slope 127, gains 0.75 and 0.99 (miss 6e-5 and 9e-5).
  Cause: at slope 127, `invSlope` ≈ −0.99975, and the attack curve near
  v = 1 has a slope of several thousand, so the nearest float v cannot hit
  g closer than that. This is a float-representation limit of the raw value
  that `slopeEg2_calc()` itself uses, not an inversion error (−80 dB, and
  the armed block ramps over it anyway). The tolerance is now
  `1e-5 + 8·FLT_EPSILON·v·|df/dv|` (`match_tolerance()`); that extra term is
  far below 1e-5 everywhere except the steepest slopes. The no-dip check
  uses the same tolerance against g. Result: **0 failures**.
- The host build shows one pre-existing warning in `SlopeEg2.c:52`
  (`slopeEg2_setSlope(eg,0.5f)` truncates to 0); untouched (out of scope).

### 11.3 C2 + C7.1 (done)

- `voicePostChain.h`: the three chains replaced with the C2.1–C2.3 code and
  comment blocks; the file comment gains the S085 P2 line.
- `test_postchain.c`: the four calls pass equal endpoints; header line
  added. `make postchain`: **post-chain differing samples: 0** (G2).

### 11.4 C3–C6 engines (done)

- **Drum** (`DrumVoice.h`/`.c`): fields after `lastGain` (C3.1); init
  (C3.2); trigger with the `AMP_EG_SYNC` `#error` guard (C3.3); async
  condition gains `!voice->ampRampArm` (C3.4); render velocity ramp, record
  and clear (C3.5). The legacy `USE_AMP_FILTER` branch is untouched.
- **Snare / Cymbal / HiHat:** fields after `egValueOscVol`, init, trigger,
  render exactly as C4–C6 (HiHat init after the decay caches; HiHat trigger
  after the open/closed decay select, with the C6.3 note).
- No other code changed.

### 11.5 G1, C7.3, C7.4 (done)

- **G1:** `make all` builds with no compiler warnings. The only warnings
  are the usual newlib stub link warnings (`_close`/`_lseek`/`_read`/
  `_write`) and the LTO serial-jobs note. `make img`:
  `build/LXRV2_lxr02.img` 643,616 B OK.
- **Build (DEV config):** `text=643,192` (+832 vs S085 close 642,360),
  `data=424`, `bss=414,100` (**unchanged**). Flash 643,616 / 753,664
  (110,048 B free). ITCM 12,432 (unchanged), DTCM 4,468, FXBUF margin
  3,712: all unchanged.
- **C7.3:** `armcheck/postchain.c` passes equal endpoints in `new_*` and
  adds `ramp_drum/ramp_snare/ramp_addgain`; the Makefile's three `$(FP)`
  runs take `--report`, and an `osccost.py` line follows. Results:

  | Loop | Before | `new_*` (equal endpoints visible to GCC) | `ramp_*` (firmware shape) |
  |---|---:|---:|---:|
  | Drum | 32 | 32 | **35** |
  | Snare | 30 | 30 | **35** |
  | Cymbal/HiHat | 25 | 25 | **30** |

  The `new_*` wrappers pass one variable twice, so `-Ofast` folds
  `last + frac·(g − last)` to `g`. They now stand for "flat call with
  endpoints known equal at compile time", which no firmware call site is.
  The fpseq totals still match (16/16, 15/15, 12/12) for the same reason.
- **Constant cost confirmed in the firmware shape:** each engine file was
  compiled alone with the firmware DSP flags (no LTO), and `osccost.py`
  was run on its render function. Each has exactly **one** post-chain loop
  (no versioning on `ampRampArm`): Drum 35, Snare 35, Cymbal 30, HiHat 30
  (worst = best). The extra block-rate cost is the `1/(size−1)` VDIV now
  computed in the Snare/Cymbal/HiHat chains (Drum had it already), once
  per block.
- **C7.4 sizes** (`arm-none-eabi-gcc`, firmware flags, `sizeof` probe +
  `nm -S`):

  | Struct | Before (A10) | After P2 | Change | Free in 1,176 B slot |
  |---|---:|---:|---:|---:|
  | `DrumVoice` | 832 | **836** | +4 | 340 |
  | `SnareVoice` | 664 | **672** | +8 | 504 |
  | `CymbalVoice` | 772 | **776** | +4 | 400 |
  | `HiHatVoice` | 780 | **788** | +8 | 388 |

  Within the approved +8 B each (U4). Drum and Cymbal had tail padding
  that absorbed the `uint8_t`. The slot `_Static_assert`s pass.

### 11.6 C7.2 checks 4–5, C7.5, G2–G6 (done) — one addition to check 5

- **Checks 4–5** added to `test_amp_retrigger.c`. The reference values for
  check 4 are written out (`ref_ramp`, `ref_trunc`, `ref_dist`), not the
  helpers under test. Check 4 also covers the falling direction
  (last 0.8 → 0.2) as well as rising.
- **Addition to check 5 (dip criterion):** the C7.2 wording ("largest
  sample-to-sample change ≤ one block's attack increment") is blind to B28.
  Under B28 the armed ramp spreads the 0.76 → 0.26 drop over 32 samples,
  a largest step of 0.01743 against an attack increment of 0.01744 at that
  raw value. So check 5 also requires that **no sample falls below the gain
  before the trigger** (minus 3 LSB). A dip is the downward form of the
  same step. Verified by hand: with only check 5 enabled, B28 gives 127
  "no-step dip" failures.
- **Check 5 figures (correct build):** gain before 0.7599, largest sample
  step 0.05677, which equals one attack-block increment (0.05677). This is
  the ordinary block-to-block step of Snare's per-block gain, not the
  retrigger. `make ampretrig`: **0 failures** (G3).
- **C7.5:** B27–B32 added to `breakproofs.py`. B32 is listed under
  `ampretrig`; its `postchain` half was checked by hand in a scratch copy:
  `post-chain differing samples: 0` (blind by design, flat calls), while
  `ampretrig` fails (`mixAddGain ramp`, `no-step step`).
- **G5:** `make breakproofs`: **all 32 breaks FAIL (good)**, B1–B26
  unchanged.
- **G2:** `postchain` 0 differing. **G4:** `armcheck-postchain` cost
  35 / 35 / 30 (`ramp_*`); `armcheck-osc` rc 0, no oscillator source
  changed.
- **G6:** `osc_run`, `osc_s0`, `osc_pwm`, `osc_sample`, `wtb`,
  `special_tags` (177 rows, 0 mismatches), `natural_order`, `catalogue`,
  `stock_gen`, `filter` (S1 PASS), `pack` 0, `octave` (unchanged report),
  `mixer` 0, `selftest`: all pass, results as before.
- **Host warnings seen in `ampretrig`, both pre-existing:** `RAND_MAX`
  redefined by `dither.h` (osc_run prints it too) and `SlopeEg2.c:52`
  (above).

### 11.7 C8 documentation (done)

- `INSTRUMENTS_DSP_REFERENCE.md`:
  - §5.1–§5.4: the engine triggers and post-chains;
  - **new §12.6.1** "Amp envelope on a `fre`/`itp` retrigger";
  - §8.1 post-chain rows (35/35/30);
  - §12.11: a post-chain paragraph;
  - also §8.2 (per-voice estimates: post 35/35/30, totals 218/195/235)
    and §12.12 (`ampretrig`; 32 breaks), so neither quotes stale figures.
- `CPU_USE_DSP_AUDIT.md` "S085 oscillator cost record": the P2 post-chain
  table, system +30 per sample, per-block/per-trigger costs, build.
- `STORAGE_SRAM_MANIFEST.md` "S085": measured sizes after P2 and the
  allocation record (bytes, owner, lifetime, U4 approval).
- `tools/dsp_test/DSP_TEST.md`: §5 rows for `postchain` and
  `armcheck-postchain` updated; §9.2 `ampretrig` and 32 breaks; **new
  §9.6** (`ampretrig`, the changed meaning of `postchain`/
  `armcheck-postchain`, B27–B32).
- `085_SESSION_HANDOFF_LOG.md`: **§17** (problem, U1–U5, changes, cost,
  RAM, build, tests, deviations, hardware rows).
- `MEMORY.md`: the "Oscillator rules (S085)" bullet as C8 words it, and
  P2-H1–H8 added to the "Next" line.

### 11.8 Status

- **Gates:**

  | Gate | Result |
  |---|---|
  | G1 | pass |
  | G2 | 0 differing |
  | G3 | 0 failures |
  | G4 | 35/35/30; `armcheck-osc` rc 0 |
  | G5 | 32/32 |
  | G6 | unchanged |
  | G7 | the C9 hardware matrix: **pending (user)** |

- **Deviations from the schedule:** two, both in the test only (§11.2
  tolerance, §11.6 dip criterion). The firmware code is as scheduled.
- **Docs beyond the §8 list:** `INSTRUMENTS_DSP_REFERENCE.md` §8.2/§12.12,
  the `DSP_TEST.md` §5 rows, and the `MEMORY.md` "Next" line, corrected
  for consistency.
- **Nothing committed** (commits belong to the user).

---

## 12. Assessment of the P2 implementation (review, 2026-10-27)

**Verdict:** the firmware code is as scheduled in every change (C1–C6).
The tests (C7) and the documents (C8) are done. I re-ran every gate and got
the implementer's results. The two test deviations in §11 are justified.
No defects were found in the P2 code.

The sporadic click on a Drum with high FM in `fre` (reported by the user on
hardware) is **not** an amp-envelope effect and is not caused by P2. It
comes from the Drum's FM depth, which follows the pitch envelope and jumps
at every trigger (§12.3).

### 12.1 Verification repeated

| Check | Result |
|---|---|
| `make all` (DEV) | clean (newlib stub link notes only); `text=643,192 data=424 bss=414,100`; flash 643,616 / 753,664 (110,048 B free); ITCM 12,432 (unchanged) |
| `postchain` (G2, the `rst` guarantee) | 0 differing samples |
| `ampretrig` (G3) | 0 failures |
| `armcheck-postchain` (G4) | `ramp_drum` 35, `ramp_snare` 35, `ramp_addgain` 30 (the firmware shape); `new_*` 32/30/25 (equal endpoints folded by GCC, §11.5) |
| `breakproofs` (G5) | 32 / 32 fail as required; B27–B32 each caught by `ampretrig` |
| `osc_run`, `osc_s0`, `special_tags` (G6) | 0 failures, 0 failures, 177 / 0 |

### 12.2 Code review against the schedule

| Item | Finding |
|---|---|
| C1 `slopeEg2_retriggerFrom()` | As scheduled: clamps, the inverse `g / (1 + invSlope(1 − g))`, repeat fallback, `EG_A`, `repeatCnt`. |
| C1 `slopeEg2_isAudibleGain()`, `SLOPEEG2_AUDIBLE_GAIN` | As scheduled (U1). |
| C2 post-chains | As scheduled. With equal endpoints they are bit-identical to the S073 passes (`postchain` 0). |
| C3 Drum | Trigger: `rst` calls `slopeEg2_trigger()`; `fre`/`itp` match on `ampFilterInput` and arm, with the `AMP_EG_SYNC` guard. Async: the attack-0 no-ramp branch is skipped only when armed. Render: velocity ramp from `lastVeloGain` when armed, then record and clear. Order in a block: trigger, then async reads the arm, then the render clears it. Correct. |
| C4–C6 Snare / Cymbal / HiHat | Match on `egValueOscVol`; arm; the armed block ramps the whole amp gain (envelope × velocity) from `lastAmpGain`; record and clear every block. HiHat keeps the open/closed decay select before the match. |
| RAM | Drum +4, Snare +8, Cymbal +4, HiHat +8 B (measured, §11.5): within U4. Static RAM unchanged. |

**Behaviour notes (correct, recorded for the user):**
1. **A run-mode change between a trigger and the next render.** The armed
   block still ramps once, even if the run mode became `rst` in between.
   This is harmless, and an `rst` trigger never arms.
2. **HiHat choke in `fre`/`itp`.** A closed hit on a ringing open hat now
   starts at the open hat's current level and falls at the closed decay
   rate. It is not cut to the closed hat's attack curve. This is the gain
   match working as designed; check it by ear (added as P2-H10).
3. **The audible test reads the envelope gain only, not × velocity.** With
   `volumeMod` on and a very low previous velocity, the arm may be set
   although the output is near silent. The ramp then starts from near zero,
   which is harmless.

### 12.3 Why high FM on a `fre` Drum still clicks sporadically (user report)

**Setup:** low-pitched, long-decay kick; sine carrier at 26 Hz; run mode
`fre`; high FM amount; amp attack 1. Sporadic clicks that disappear when the
FM amount is 0, or when the run mode is `itp`.

**Cause:** the Drum's FM depth follows the **pitch envelope**, and nothing
slews it.

- **The depth tracks the pitch envelope:** `Drum_calcVoiceAsync()`
  (`DrumVoice.c:294`) sets `voice->osc.fmMod = voice->fmModAmount *
  egPitchVal`. `egPitchVal` is the pitch envelope (`DecayEg`).
  `DecayEg_trigger()` (`Decay.c:60–63`) sets it straight back to 1.0 on
  every trigger, in every run mode. On the first block after a retrigger
  the FM depth therefore jumps from its decayed value (near 0 on a long
  kick) to almost the full `fmModAmount`.
- **The Drum's "FM" is phase modulation:** the carrier loop reads the sine
  at `phase + (mod[i] × fmMod) << 17` (`calcFmSineBlock()`; the table and
  stock FM loops use `<< 19`). A jump in `fmMod` at a block boundary is an
  instant jump in the **read phase** of `mod[i] × Δ fmMod`. The waveform
  jumps, which is the click.
- **Why the `fre` slew doesn't cover it:** the slew glides the oscillators'
  phase **increments**, that is their pitch. It does not touch the FM
  depth. P2 ramps the amp gain, not the FM depth either.
- **Why it is sporadic:** the jump is proportional to the modulator's
  instantaneous value at the trigger. A trigger near a modulator zero
  crossing hardly moves the read phase; one near a modulator peak moves it
  by up to several cycles. With a slow modulator on a low kick, the value
  at the trigger varies a lot from hit to hit.
- **Why FM amount 0 removes it:** `fmMod` = 0 before and after, so the read
  phase never jumps.
- **Why `itp` removes it:** the `itp` old-carrier copy (`itpOld`) is taken
  at the trigger with the **old** `fmMod`. Only `voice->osc` is updated by
  the async, never the copy, so the old carrier keeps the old depth and
  stays continuous. The new carrier has the jump, but it fades in from
  zero weight over 132 samples, so the jump is hidden.
- **Scope:** only Drum has an envelope-driven FM depth. Cymbal and HiHat
  FM depths are their constant modulator gains. Snare has no FM.

**Fix options (not implemented; for the user to choose):**

| Option | What | Cost (worst instructions per sample) | Notes |
|---|---|---|---|
| F-A | Ramp the depth on the armed block: in the Drum section, scale `modBuf` by a per-sample ramp from `fmMod_old / fmMod_new` to 1 before the carrier FM. Flat (×1) on every other block. | ≈ +4, Drum only (one multiply and store per sample on `modBuf`); constant | Drum-only; no oscillator loop changes; `rst` flat, so bit-identical; reuses the `ampRampArm` lifecycle. Ratios above 1 need care: when `fmMod_new` is near 0, clamp, or ramp the depth itself instead. |
| F-B | A per-sample `fmMod` ramp inside every FM carrier loop (sine, table, stock, crash, pwm, smp/slp, wtb FM) | ≈ +2 in every FM loop, for every engine | Touches many ITCM loops and their S0 proofs; more general than needed. |
| F-C | Accept it: `itp` already removes it, as the user found | 0 | Document that high Drum FM wants `itp`. |

The recommendation is F-A if `fre` must be click-free with high FM;
otherwise F-C. The ramp is one block (0.73 ms), matching the amp ramp. A
full-scale phase jump spread over 0.73 ms is a fast phase glide, not a
step.

### 12.4 Other remaining retrigger step sources (unchanged by P2; for completeness)

- **Transient sample:** restarts by design.
- **LFO retrigger:** `lfo_retrigger()` on the voice's own track restarts any
  LFO set to retrigger from that track; its targets step at the next
  block.
- **Velocity-modulated parameters other than volume** (filter, pitch, FM
  amount): they are rewritten at the trigger without a ramp. A
  velocity-to-FM-amount target would add to §12.3.
- **`rst`:** unchanged by rule; it resets phases and clicks as before.

### 12.5 Hardware rows added

| # | Check | Pass |
|---|---|---|
| P2-H9 | Drum `fre`, sine carrier at a low note, high FM amount, long pitch decay; fast retriggers | no click (F-A implemented, §13); in `itp` none |
| P2-H10 | HiHat `fre`/`itp`: a closed hit on a ringing open hat | the choke falls quickly from the current level, with no click; check the length by ear |

### 12.6 Status

- **P2 code:** accepted.
- **Gates:** G1–G6 pass; G7 (hardware, P2-H1–H10) is pending.
- **Open decision:** F-A / F-C for the Drum FM depth (§12.3). **Decided: F-A, implemented (§13).**
- **Nothing committed.**

---

## 13. F-A: Drum FM depth ramp (implemented 2026-10-27)

**Request (user):** "do F-A, make the direct targeted fix". The cause is in
§12.3.

### 13.1 What changed

| File | Change |
|---|---|
| `Core/DSPAudio/OscSection.c` | **Add** `oscSection_rampModDepth()` (static inline): `modBuf[i] *= from + (i/(size−1))·(1 − from)`, truncated to int16. **Modify:** the Drum render is now `oscSection_renderDrumRamped(…, float fmRampFrom)`. Its fm branch calls the ramp between the modulator render and the carrier FM render, every fm block. It forces `fmRampFrom = 1` when an `itp` old carrier is present. **Add** `oscSection_renderDrum()` as the flat wrapper (`fmRampFrom = 1`), kept for the host tests and the S0 proof. Contract comment blocks on all three. |
| `Core/DSPAudio/OscSection.h` | **Add** the `oscSection_renderDrumRamped()` declaration with its contract block. `oscSection_renderDrum()` is unchanged. |
| `Core/DSP/Instruments/Drum/DrumVoice.h` | **Add** `float lastFmMod` (contract block) after `ampRampArm`. **Remove** the unused `float egValueOscVol` (no reader or writer anywhere; Drum keeps its envelope gain in `ampFilterInput`), so P2 stays within the approved +8 B. |
| `Core/DSP/Instruments/Drum/DrumVoice.c` | **Init:** `lastFmMod = 0`. **Render:** computes `fmRatio = lastFmMod / fmMod` every block (denominator guarded, clamped 0..1). It passes `fmRampFrom = ampRampArm && fmMod > 0 ? fmRatio : 1` to `oscSection_renderDrumRamped()`, then records `lastFmMod = fmMod`. Contract block at the call. |
| `tools/dsp_test/test_osc_run.c` | **Add** `test_fm_depth_ramp()` (§13.3), run in mode `run`/`all`. |
| `tools/dsp_test/armcheck/probe_section.c`, `Makefile` | **Add** probe `p_fm_ramp`; `armcheck-osc` reports it. |
| `tools/dsp_test/breakproofs.py` | **Add** B33–B35. |
| Documents | `INSTRUMENTS_DSP_REFERENCE.md` §12.6.1 (F-A paragraph) and §12.11 (Drum fm 135); `CPU_USE_DSP_AUDIT.md` (F-A cost); `STORAGE_SRAM_MANIFEST.md` (Drum 840, the field swap); `DSP_TEST.md` §9.2, new §9.7; `085_SESSION_HANDOFF_LOG.md` §17.7; `MEMORY.md` (oscillator rules bullet). |

**Behaviour:**
- **The armed block:** on a `fre` (or unit-less `itp`) retrigger with level
  left, the carrier's FM depth glides from the previous block's value to the
  new one over one block (0.73 ms). The read phase moves continuously
  instead of jumping by `mod × Δ fmMod`.
- **Every other block, `rst`, `mix`/`snc`/`rng`, and an active `itp`
  crossfade:** unchanged. The factor is exactly 1.0.

### 13.2 Measurements

| Item | Value |
|---|---|
| `make all` | clean; `text=643,368` (+176 against P2), `data=424`, `bss=414,100` (unchanged); flash 643,792 / 753,664 (109,872 B free); ITCM 12,432 (unchanged) |
| Cost of the ramp | **15** worst instructions per sample, every Drum fm block (`p_fm_ramp`). §12.3 estimated ≈ 4; that was low, because the ramp is its own pass over the modulator buffer. |
| Drum `fm` per-voice worst | 120 → **135**. Still below Drum `snc` (162) and Cymbal/HiHat (178), so the system worst case does not change. |
| `DrumVoice` | **840 B** (832 before P2; 836 after P2 without F-A). `lastFmMod` +4, `egValueOscVol` −4. P2 total +8, within U4. |

### 13.3 Tests

`osc_run` "fm depth ramp":
1. **Exact wiring:** the modulator block equals a plain render scaled by
   the written-out ramp, and the carrier equals an FM render of that block.
2. **Flat:** `renderDrumRamped(…, 1.0f)` equals `renderDrum()`.
3. **Forced flat under `itp`:** with an old carrier present, the modulator
   block is unscaled.
4. **No step:**
   - **Setup:** 16 modulator × 8 carrier start phases; depth 0.15, then
     0.8, at amount 0.8.
   - **Result:** the ramped boundary step is never above the block's own
     largest step + 3 LSB (int16 truncation). The worst ramped boundary
     step is **294**, against **65,311** unramped.
   - **Test design:** the first version used one fixed phase. That was
     blind: the modulator was near zero and the carrier at a flat peak, so
     the unramped step was only 15. The sweep replaced it, with an explicit
     sensitivity check (unramped > 8000).

**Gates:**

| Gate | Result |
|---|---|
| `osc_run` | 0 failures |
| `osc_s0` | 0 failures; the S0 proof runs the flat path |
| `postchain` | 0 differing |
| `ampretrig` | 0 failures |
| `special_tags` | 177 / 0 |
| `natural_order`, `catalogue`, `stock_gen` | 0 failures |
| `breakproofs` | **35 / 35** fail as required |

**New breaks:**

| ID | Defect | Caught by |
|---|---|---|
| B33 | the ramp ignores its start | exact wiring |
| B34 | the ramp is not forced flat under `itp` | the forced-flat check |
| B35 | the ramp runs the wrong way | exact wiring |

### 13.4 Notes

- **The clamp:** the ratio is clamped to ≤ 1, so the scaled modulator never
  exceeds its rendered value and cannot overflow int16. A retrigger whose
  new depth is *below* the old one keeps that part of the step. That needs a
  lowered FM amount at the moment of the retrigger, because the pitch
  envelope restarts at its maximum.
- **Pre-existing hardware behaviour (not changed):** the FM read offset
  `(uint32_t)(mod × fmMod)` compiles to `VCVT.U32`, which saturates negative
  products to 0. On hardware the Drum carrier is phase-modulated by the
  positive half of the modulator only; the host build wraps instead. This
  also made the pre-F-A click depend on the modulator's sign at the
  trigger. It is recorded in `INSTRUMENTS_DSP_REFERENCE.md` §12.6.1. Ask
  the user before changing it, since it is part of the S0-identical sound.
- **Hardware row P2-H9** (§12.5) now expects no click in `fre` with F-A.
- **Nothing committed.**

## 14. Menu names, the `wav`/`mod` row swap, and no `pwm` on the Cymbal/HiHat modulators (2026-10-27)

User requests, in order:
1. Rename the waveforms to a uniform style.
2. Swap what the `wav` and `src` rows carry, and rename `src` to `mod`.
3. Keep the Cymbal/HiHat FM oscillators off the expanded types (they crashed).
   This was then narrowed: only `pwm` caused the crash, so put the other
   types back and exclude only `pwm` on the FM oscillators.

### 14.1 Names (`Core/Menu/MenuText.h`)

| List | Before | After |
|---|---|---|
| `waveformNames` 6–10 (`wav` type) | `sq3 sw3 sqa tra swa` | `Re3 Sw3 ReA TrA SwA` |
| `pwmFamilyNames` 0–5 (`pwm` type) | `prc psw ptr pq3 pw3 swp` | `Rec Saw Tri Re3 Sw3 Swp` |

- **Unchanged:** the code constants (`SQ3`…`SWA`, `OSC_PWM_PRC`…`SWP`). The
  tools' plot labels are not menu text and were left alone.
- **The table counts did not change,** so the `menu.c` count asserts still hold.

### 14.2 The row swap

| Row (key) | Field | Before | After |
|---|---|---|---|
| `wav` (`oscN_wave`) | `waveform` | stock wave / pwm width / sample start / table position | the **selection**: stock wave 0–10, pwm family 0–5, sample 0..count−1, wavetable 0..count−1 |
| `mod` (`oscN_source`), was `src` | `source` | selector: pwm family / sample / table | the **continuous control** 0–127: pwm width, sample start, table position (`wav` ignores it) |

The swap was done at the field level. `waveform` is always the `wav` row and
`source` is always the `mod` row. Velocity direct writes, LFO adapters, Morph
and the writers therefore each act on one field per row.

**Code changes:**

| File | Change |
|---|---|
| `Oscillator.h` | Comments on `waveform`/`source`/requests. New `uint8_t freqCacheSource` in existing padding (`OscInfo` stays 124 B). |
| `Oscillator.c` `osc_setFreq()` | Cache key adds `source`: the pwm width is `source`, and velocity and the LFO write it directly. |
| `OscillatorPwm.c/.h` | The family comes from `waveform` (`oscPwm_prepare`, `oscPwm_block`); `c = source / 127`. |
| `OscillatorSample.c/.h` `oscSample_bind()` | Index = `min(waveform, count−1)`. On a catalogue-generation change, `waveform` is first re-expanded from `waveRequest`, so a smaller reinstall never loses the choice. With no samples, `waveform = 0`. `sampleStart = source · frames >> 7`. |
| `OscillatorWavetable.c/.h` `oscWavetable_bind()` | The same index rule. `oscWavetable_pair()` takes `osc->source` as the position. |
| `InstrumentManager.c/.h` | `oscWaveMaxForType`: wav 10, pwm 5, smp/slp/wtb count−1 (0 if none). `oscSourceMaxForType`: 127. The `OSC_WAVE` writer rebinds smp/slp/wtb and clears the frequency cache. The `OSC_SOURCE` writer rebinds smp/slp (start). Comments updated, including `isSelectorDescriptor` (now typ, run). |
| `*Parameters.c` (all four) | Each `oscN_source` row goes from `ROW_SELECT_SPECIAL(... "Source", "src" ...)` to `ROW_SPECIAL(... "Mod", "mod", DTYPE_0B127, MOD_0_127, ..., TYPE_UINT8, IM_SPECIAL_OSC_SOURCE \| sel)`. It is now Morphable, an LFO/velocity target and automatable. Keys and indices are unchanged. |
| `InstrumentCellFormat.c/.h` | `OSC_WAVE` → STOCK_WAVE / PWM_FAMILY / SAMPLE / WAVETABLE by the stored type. `OSC_SOURCE` → new `CELL_FMT_NUMBER`. Removed the kinds `NO_SOURCE`, `PWM_WIDTH`, `SAMPLE_START`, `WTB_POSITION`, the `aux` field and `cellFormat_storedSource()`. |
| `menu.c` | Full view: `wav` keeps a sample's or table's 8-character name (or the stock wave name) on the left and the compact value on the right. `mod` shows only its number, once, at `line[0..2]`. The width/start/frame text is gone. Comments updated. |
| `presetManager.c` | Comments only (selector rows: typ, run). The behaviour follows the flags: `mod` is no longer a selector, so it is stored in both endpoints and Morphed. |

**Behaviour to know:**
- **Saved content is not migrated (Q8).** A kit saved before the swap with a
  non-`wav` type loads with the two values exchanged. For example, a sample
  index loads as the start, and the old start loads as the sample index,
  clamped to the installed count. `wav`-type kits are unaffected.
- **`wav` under `smp`/`slp`/`wtb` is now Morphable and modulatable.** Morphing
  or LFO-sweeping it steps through the installed samples or tables. Before
  the swap the selection sat on a non-morphable selector. Changing the index
  costs one `sampleMemory_getSampleInfo()` / `getWavetableInfo()` in that
  block's bind. That cost is bounded and is part of the per-block worst case.
- **Drum FM page:** it now shows two cells labelled `mod`: `osc2_mod_type`
  (long name "Mode", fm/mix/snc/rng) and the new `osc2` `mod` row (long name
  "Mod"). This is not changed; it is flagged to the user.

### 14.3 No `pwm` on the Cymbal/HiHat modulators

**The rule:** new `instrumentManager_oscTypeAllowed(instrumentType, special,
oscType)` returns 0 only for `pwm` on Cymbal/HiHat `osc2`/`osc3`. The main
oscillators, Drum `osc2` and the Snare keep all five types.

**Where it applies:**

| Site | Effect |
|---|---|
| `IM_SPECIAL_OSC_TYPE` writer | A disallowed `pwm` is written as `wav` (file load, AutoSave, automation, MIDI). |
| `menu_clampCellValue()` | An edit that lands on `pwm` moves to `smp` when coming from `wav`, or to `wav` when coming from above. The encoder therefore steps `wav ↔ smp`, and an absolute pot never stores `pwm`. |
| `InstrumentCellFormat.c` | A stored `pwm` displays as `wav`, and the `wav` row formats as a `wav`-type row, matching what plays. |

- **Rows:** the Cymbal/HiHat `osc2_type`/`osc3_type`/`osc2_source`/`osc3_source`
  rows were briefly removed under request 3, then restored when the request
  was narrowed. They are identical to before apart from `src` → `mod`; the
  descriptor counts are unchanged.
- **Root cause of the crash: not found.** The pwm paths read correctly:
  - the family is clamped in `oscPwm_prepare()` and `pwm_familyLevel()`
    falls back to saw;
  - table reads are masked;
  - `osc_setFreq()` prepares each modulator every block.

  This is the user's chosen exclusion, not a fix. A pwm Drum `osc2` has not
  been reported to crash.

### 14.4 Wavetable looping (user report: "user wavetables don't loop")

Checked; there is no code change.
- **Renderer:** the `wtb` renderer wraps its phase every 2^32 cycle, and
  the point after the last reads point 0 (`nx`).
- **No end test:** `oscSample_isFinished()` is `smp` only, the engines'
  restart is `smp`/`slp` only, and `oscSection_masterCycle()` treats `wtb`
  as a full-wrap master.
- **Installed data:** the `ak_hvox` files on the card are 256-point,
  16-bit, single-cycle frames, periodically resampled. The step across the
  cycle boundary is no larger than the steps inside the cycle.
- **Host test:** the existing `wtb` block-continuity test passes.

**Status:** open. The user's exact symptom is needed to go further, for
example "stops after one cycle", "click at the cycle boundary" or "the
position does not wrap".

### 14.5 Gates (all run 2026-10-27)

| Gate | Result |
|---|---|
| `make all` | clean (pre-existing warnings only). Flash 644,124 / 753,664 B, **ITCM 12,520 / 16,384 B** (+88 B, `osc_setFreq` cache key and binds), DTCM 4,468, FXBUF margin 3,712 |
| struct sizes (ARM) | `OscInfo` 124, Drum 840, Cymbal 776, HiHat 788, Snare 672: unchanged |
| `osc_run` | 0 failures. Tests moved to the new roles: `slave_of(type, sel, ctl)`, pwm family in `waveform` and width in `source`, sample/table index in `waveform` and start/position in `source`. The re-expansion test now exercises the generation path (`7 of 5 → 4`, reinstall of 8 → 7). |
| `osc_s0` | 0 failures |
| `postchain` | 0 differing |
| `ampretrig` | 0 failures |
| `special_tags` | 177 rows / 0 mismatches |
| `breakproofs` | **35 / 35** fail as required. B14 moved to the new bind: `osc->waveRequest = index;` after the clamp. |
| `armcheck-osc` | pwm loops unchanged (prc/psw 49 + 2 = 51, FM 56, lane 63; swp 46/48/56/50). `probe_pwm.c` now sets `l.waveform = fam`. |

### 14.6 Docs updated

- `INSTRUMENTS_DSP_REFERENCE.md`: §12 intro, §12.1 rows and history, §12.2,
  §12.3 table, §12.3.1–§12.3.4, §12.4 cache key, §12.10.
- `FILESYSTEM_SPEC.md`: empty-catalogue line, stored selections, key meanings,
  the Cymbal/HiHat `pwm` rule.
- `MIDI_SPEC.md`: S085 rows.
- `BANK_PRESET_ARCHITECTURE.md`: selector rows are now typ/run.
- `STORAGE_SRAM_MANIFEST.md`, `OSC_INTERP_AUDIT.md`: names.
- `GENERATOR_PAGE_SPEC_DRAFT.md`: §2.6.
- `MEMORY.md`: the S085 bullets.
- `085_SESSION_HANDOFF_LOG.md`: §17.8.

### 14.7 Hardware rows (the user runs them)

| ID | Check | Expect |
|---|---|---|
| P2-H11 | `wav` row under each type | names: `Re3 Sw3 ReA TrA SwA`; pwm `Rec Saw Tri Re3 Sw3 Swp`; smp/slp/wtb `sNN`/`wNN` with the 8-character name on the left in the full view |
| P2-H12 | `mod` row | a plain number; the full view shows it once, on the left; pwm width, sample start and table position all respond |
| P2-H13 | Cymbal/HiHat `osc2`/`osc3` `typ` | the encoder steps `wav ↔ smp` (never `pwm`); `smp`/`slp`/`wtb` play without a crash |
| P2-H14 | LFO or velocity on `mod` | width, start and position modulate; the LFO restores its base on removal |

Nothing committed.
