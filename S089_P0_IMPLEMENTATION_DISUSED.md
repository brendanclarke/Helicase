# S089 Phase 0 — SRR Move: Implementation Schedule

**Parent plan:** S089_P0_SRR_MOVE.md  
**Status:** Implemented 2026-10-30 — builds, host checks pass, hardware
verification pending. See the Implementation Record at the end of this file.
**Scope:** move sample-rate reduction state from mixer globals into each voice
engine struct; move the S&H processing loop from `mixer_decimateBlock()` into
each engine's `calcSyncBlock`; rewire the `INSTRUMENT_BIND_SLOT_DECIMATION`
binding to write the engine struct.

Every change is listed by file → line → action (ADD / MODIFY / REMOVE) with
the exact documentation block to accompany it.

---

## Change 1 — DrumVoice struct: add `decimationHeld`, document existing fields

**File:** `Core/DSP/Instruments/Drum/DrumVoice.h`  
**Lines:** 205–206 (existing vestigial members), insert after 206  
**Action:** MODIFY lines 205–206 (add documentation), ADD `decimationHeld` after line 206

DrumVoice already carries `decimationCnt` (float) and `decimationRate`
(float) at lines 205–206, initialized at DrumVoice.c:183–184 but never
read by the render. These become the live S&H state. Add the missing
`decimationHeld` (int16_t) and a documentation block for all three.

```c
/* REPLACE lines 205–206 with: */

	/*
	 * Sample-rate reduction state (S089 P0: moved from mixer globals).
	 *
	 * What:       per-voice sample & hold decimator. decimationRate is 0..1
	 *             (1 = full sample rate, no reduction; 0 = hold indefinitely).
	 *             decimationCnt is the fractional accumulator: each sample adds
	 *             decimationRate; when it reaches 1.0 the input sample is latched
	 *             into decimationHeld and the counter wraps by subtracting 1.0.
	 *             Between latch points the output repeats decimationHeld.
	 * Why:        SRR was previously a mixer-global array
	 *             (mixer_decimation_rate/cnt/voice_samples). Moving it into the
	 *             engine struct makes it part of the voice chain budget and
	 *             positions it for S089 P1 voice chain dispatch, where alternative
	 *             chains may omit or replace the S&H stage.
	 * Inputs:     decimationRate written by INSTRUMENT_BIND_SLOT_DECIMATION
	 *             (InstrumentManager.c) via the instrument_decimation descriptor
	 *             row. LFO and velocity supplemental targets also write it through
	 *             the same binding (INSTALLED_MOD_TARGET_SLOT_DECIMATION).
	 *             valueShaperI2F(value, -0.7f) maps 0..127 → 0..1 with a concave
	 *             curve that gives finer control at low sample rates.
	 * Outputs:    read by the S&H loop at the end of Drum_calcVoiceSyncBlock().
	 * Accessors:  Drum_initVoice() (rate=1, cnt=0, held=0);
	 *             instrumentManager_writeRuntime() SLOT_DECIMATION case.
	 * Affiliates: mixer_decimateBlock() (removed call, S089 P0); ROW_SLOT_DECIMATION
	 *             (DrumParameters.c); voicePostChain.h (precedes this in the chain).
	 */
	float decimationCnt;
	float decimationRate;
	int16_t decimationHeld;
```

**Size impact:** +2 B (int16_t) + 2 B alignment pad = +4 B. DrumVoice grows
from 588 → 592 B. Slot reserve recalculates as 2 × 592 = 1184 B.

---

## Change 2 — SnareVoice struct: add decimation state

**File:** `Core/DSP/Instruments/Snare/Snare.h`  
**Line:** 146 (before `} SnareVoice;`)  
**Action:** ADD three members + documentation block before the closing brace

Insert after `OscCarrierSnapshot snapshot;` (line 145), before `} SnareVoice;`
(line 147):

```c
/* ADD before line 147 (} SnareVoice;): */

	/*
	 * Sample-rate reduction state (S089 P0: moved from mixer globals).
	 *
	 * What:       per-voice sample & hold decimator. decimationRate is 0..1
	 *             (1 = full sample rate, no reduction; 0 = hold indefinitely).
	 *             decimationCnt is the fractional accumulator: each sample adds
	 *             decimationRate; on overflow the input is latched into
	 *             decimationHeld and the counter wraps. Between latch points
	 *             the output repeats decimationHeld.
	 * Why:        SRR was mixer-global (mixer_decimation_rate/cnt/voice_samples).
	 *             Moving it into the engine struct makes it part of the voice
	 *             chain budget for S089 P1 voice chain dispatch.
	 * Inputs:     decimationRate written by INSTRUMENT_BIND_SLOT_DECIMATION
	 *             (InstrumentManager.c) via the instrument_decimation descriptor.
	 *             LFO and velocity supplemental targets write through the same
	 *             binding. valueShaperI2F(value, -0.7f) maps 0..127 → 0..1.
	 * Outputs:    read by the S&H loop at the end of Snare_calcSyncBlockVoice().
	 * Accessors:  Snare_initVoice() (rate=1, cnt=0, held=0);
	 *             instrumentManager_writeRuntime() SLOT_DECIMATION case.
	 * Affiliates: mixer_decimateBlock() (removed call); ROW_SLOT_DECIMATION
	 *             (SnareParameters.c); voicePostChain.h.
	 */
	float decimationCnt;
	float decimationRate;
	int16_t decimationHeld;
```

**Size impact:** +12 B (4+4+2+2 pad). SnareVoice grows by 12 B.

---

## Change 3 — CymbalVoice struct: add decimation state

**File:** `Core/DSP/Instruments/Cymbal/CymbalVoice.h`  
**Line:** 139 (before `} CymbalVoice;`)  
**Action:** ADD three members + documentation block before the closing brace

Insert after `SnapEg snapEg;` (line 137), before `} CymbalVoice;` (line 139):

```c
/* ADD before line 139 (} CymbalVoice;): */

	/*
	 * Sample-rate reduction state (S089 P0: moved from mixer globals).
	 *
	 * What:       per-voice sample & hold decimator. decimationRate is 0..1
	 *             (1 = full sample rate, no reduction; 0 = hold indefinitely).
	 *             decimationCnt is the fractional accumulator: each sample adds
	 *             decimationRate; on overflow the input is latched into
	 *             decimationHeld and the counter wraps. Between latch points
	 *             the output repeats decimationHeld.
	 * Why:        SRR was mixer-global (mixer_decimation_rate/cnt/voice_samples).
	 *             Moving it into the engine struct makes it part of the voice
	 *             chain budget for S089 P1 voice chain dispatch.
	 * Inputs:     decimationRate written by INSTRUMENT_BIND_SLOT_DECIMATION
	 *             (InstrumentManager.c) via the instrument_decimation descriptor.
	 *             LFO and velocity supplemental targets write through the same
	 *             binding. valueShaperI2F(value, -0.7f) maps 0..127 → 0..1.
	 * Outputs:    read by the S&H loop at the end of Cymbal_calcSyncBlockVoice().
	 * Accessors:  Cymbal_initVoice() (rate=1, cnt=0, held=0);
	 *             instrumentManager_writeRuntime() SLOT_DECIMATION case.
	 * Affiliates: mixer_decimateBlock() (removed call); ROW_SLOT_DECIMATION
	 *             (CymbalParameters.c); voicePostChain.h.
	 */
	float decimationCnt;
	float decimationRate;
	int16_t decimationHeld;
```

**Size impact:** +12 B. CymbalVoice grows by 12 B.

---

## Change 4 — HiHatVoice struct: add decimation state

**File:** `Core/DSP/Instruments/HiHat/HiHat.h`  
**Line:** 146 (before `} HiHatVoice;`)  
**Action:** ADD three members + documentation block before the closing brace

Insert after `OscCarrierSnapshot snapshot;` (line 144), before
`} HiHatVoice;` (line 146):

```c
/* ADD before line 146 (} HiHatVoice;): */

	/*
	 * Sample-rate reduction state (S089 P0: moved from mixer globals).
	 *
	 * What:       per-voice sample & hold decimator. decimationRate is 0..1
	 *             (1 = full sample rate, no reduction; 0 = hold indefinitely).
	 *             decimationCnt is the fractional accumulator: each sample adds
	 *             decimationRate; on overflow the input is latched into
	 *             decimationHeld and the counter wraps. Between latch points
	 *             the output repeats decimationHeld.
	 * Why:        SRR was mixer-global (mixer_decimation_rate/cnt/voice_samples).
	 *             Moving it into the engine struct makes it part of the voice
	 *             chain budget for S089 P1 voice chain dispatch.
	 * Inputs:     decimationRate written by INSTRUMENT_BIND_SLOT_DECIMATION
	 *             (InstrumentManager.c) via the instrument_decimation descriptor.
	 *             LFO and velocity supplemental targets write through the same
	 *             binding. valueShaperI2F(value, -0.7f) maps 0..127 → 0..1.
	 * Outputs:    read by the S&H loop at the end of HiHat_calcSyncBlockVoice().
	 * Accessors:  HiHat_initVoice() (rate=1, cnt=0, held=0);
	 *             instrumentManager_writeRuntime() SLOT_DECIMATION case.
	 * Affiliates: mixer_decimateBlock() (removed call); ROW_SLOT_DECIMATION
	 *             (HiHatParameters.c); voicePostChain.h.
	 */
	float decimationCnt;
	float decimationRate;
	int16_t decimationHeld;
```

**Size impact:** +12 B. HiHatVoice grows by 12 B.

---

## Change 5 — Drum init: document existing init, add `decimationHeld`

**File:** `Core/DSP/Instruments/Drum/DrumVoice.c`  
**Lines:** 183–184 (existing init of vestigial members)  
**Action:** MODIFY lines 183–184 (add documentation), ADD `decimationHeld` init

```c
/* REPLACE lines 183–184 with: */

	/*
	 * S089 P0: sample-rate reduction init.
	 *
	 * What:       rate = 1.0 means the accumulator overflows every sample, so
	 *             held = buf[i] on every iteration and the output is bit-identical
	 *             to the input. This is the parameter default (instrument_decimation
	 *             row value 0 → valueShaperI2F(0, -0.7f) ≈ 0; value 127 → 1.0).
	 *             Counter at 0 and held at 0 give a clean start.
	 * Why:        a runtime-slot reset (memset + init) must produce the same audio
	 *             as before the SRR move: no sample rate reduction at default.
	 * Accessors:  Drum_calcVoiceSyncBlock() S&H loop; the SLOT_DECIMATION binding.
	 */
	voice->decimationCnt = 0.f;
	voice->decimationRate = 1.f;
	voice->decimationHeld = 0;
```

---

## Change 6 — Snare init: add decimation init

**File:** `Core/DSP/Instruments/Snare/Snare.c`  
**Line:** 121 (after `voice->snapshot.counter = 0u;`), before the closing `}`
of `Snare_initVoice` at line 122  
**Action:** ADD decimation init block

```c
/* ADD after line 121 (voice->snapshot.counter = 0u;): */

	/*
	 * S089 P0: sample-rate reduction init.
	 *
	 * What:       rate = 1.0 means the S&H accumulator overflows every sample,
	 *             producing bit-identical output. Counter at 0 and held at 0.
	 * Why:        a runtime-slot reset must produce no sample rate reduction at
	 *             default, identical to the pre-move behaviour.
	 * Accessors:  Snare_calcSyncBlockVoice() S&H loop; the SLOT_DECIMATION binding.
	 */
	voice->decimationCnt = 0.f;
	voice->decimationRate = 1.f;
	voice->decimationHeld = 0;
```

---

## Change 7 — Cymbal init: add decimation init

**File:** `Core/DSP/Instruments/Cymbal/CymbalVoice.c`  
**Line:** 138 (after `voice->snapshot.counter = 0u;`), before the closing `}`
of `Cymbal_initVoice` at line 139  
**Action:** ADD decimation init block

```c
/* ADD after line 138 (voice->snapshot.counter = 0u;): */

	/*
	 * S089 P0: sample-rate reduction init.
	 *
	 * What:       rate = 1.0 means the S&H accumulator overflows every sample,
	 *             producing bit-identical output. Counter at 0 and held at 0.
	 * Why:        a runtime-slot reset must produce no sample rate reduction at
	 *             default, identical to the pre-move behaviour.
	 * Accessors:  Cymbal_calcSyncBlockVoice() S&H loop; the SLOT_DECIMATION binding.
	 */
	voice->decimationCnt = 0.f;
	voice->decimationRate = 1.f;
	voice->decimationHeld = 0;
```

---

## Change 8 — HiHat init: add decimation init

**File:** `Core/DSP/Instruments/HiHat/HiHat.c`  
**Line:** 150 (after `voice->snapshot.counter = 0u;`), before the closing `}`
of `HiHat_initVoice` at line 151  
**Action:** ADD decimation init block

```c
/* ADD after line 150 (voice->snapshot.counter = 0u;): */

	/*
	 * S089 P0: sample-rate reduction init.
	 *
	 * What:       rate = 1.0 means the S&H accumulator overflows every sample,
	 *             producing bit-identical output. Counter at 0 and held at 0.
	 * Why:        a runtime-slot reset must produce no sample rate reduction at
	 *             default, identical to the pre-move behaviour.
	 * Accessors:  HiHat_calcSyncBlockVoice() S&H loop; the SLOT_DECIMATION binding.
	 */
	voice->decimationCnt = 0.f;
	voice->decimationRate = 1.f;
	voice->decimationHeld = 0;
```

---

## Change 9 — Drum render: add S&H loop

**File:** `Core/DSP/Instruments/Drum/DrumVoice.c`  
**Line:** 864 (after `voice->ampRampArm = 0u;`), before the volume comment
block at line 865  
**Action:** ADD S&H loop between the arm clear (line 864) and the volume
comment (line 865)

The S&H loop runs after the post-chain and arm clear, on the final
pre-volume buffer. This is the same position in the signal path as the
former `mixer_decimateBlock()` call — immediately after the engine render
returns to the mixer and before volume/pan are applied.

```c
/* ADD after line 864 (voice->ampRampArm = 0u;): */

	/*
	 * Sample-rate reduction (S089 P0: moved from mixer_decimateBlock).
	 *
	 * What:       sample & hold decimation loop. Each sample advances the
	 *             fractional accumulator (decimationCnt) by decimationRate.
	 *             When the accumulator reaches or exceeds 1.0, it wraps by
	 *             subtracting 1.0 and the current input sample is latched into
	 *             decimationHeld. Every output sample — whether freshly latched
	 *             or held — is written as decimationHeld, producing the
	 *             staircase waveform characteristic of sample rate reduction.
	 * Why:        the identical algorithm previously ran in mixer_decimateBlock()
	 *             (mixer.c:171–186) on mixer-global arrays. Moving it here makes
	 *             SRR part of the per-voice processing budget and positions it
	 *             for S089 P1 voice chain dispatch, where alternative chains may
	 *             replace the S&H with a different final stage.
	 * Signal path: this loop processes the same buffer at the same pipeline
	 *             position as before: after the post-chain (voicePost_drum) and
	 *             before the mixer applies channel volume and pan. The mixer's
	 *             decimateBlock call is removed (Change 14).
	 * Rate = 1.0: the accumulator overflows every sample, so held = buf[i] on
	 *             every iteration. The output is bit-identical to the input.
	 *             No branch bypass is needed.
	 * Locals:     cnt, rate, held are loaded from the struct before the loop and
	 *             stored back after, so the loop body operates on registers.
	 * Inputs:     voice->decimationRate (from SLOT_DECIMATION binding),
	 *             voice->decimationCnt (carried across blocks),
	 *             voice->decimationHeld (carried across blocks),
	 *             buf (the post-chain pre-volume block), size (block length).
	 * Outputs:    buf overwritten in place; struct members updated.
	 * Accessors:  this render function (Drum_calcVoiceSyncBlock).
	 * Affiliates: the SLOT_DECIMATION binding (InstrumentManager.c Change 13);
	 *             Drum_initVoice() (Change 5); mixer.c (Change 14, removed call).
	 */
	{
		float cnt  = voice->decimationCnt;
		float rate = voice->decimationRate;
		int16_t held = voice->decimationHeld;
		uint8_t i;
		for (i = 0; i < size; i++) {
			cnt += rate;
			if (cnt >= 1.f) {
				cnt -= 1.f;
				held = buf[i];
			}
			buf[i] = held;
		}
		voice->decimationCnt  = cnt;
		voice->decimationHeld = held;
	}
```

---

## Change 10 — Snare render: add S&H loop

**File:** `Core/DSP/Instruments/Snare/Snare.c`  
**Line:** 420 (after `voice->ampRampArm = 0u;`), before the closing `}` of the
post-chain scope at line 421, then before the function-closing `}` at line 422  
**Action:** ADD S&H loop after the post-chain scope closes (after line 421)

The Snare render's last action is the post-chain scope (lines 407–421)
which ends with `voice->ampRampArm = 0u;` inside a brace block. The S&H
loop goes after that scope closes, as the final operation before the
function returns.

```c
/* ADD after line 421 (closing } of post-chain scope): */

	/*
	 * Sample-rate reduction (S089 P0: moved from mixer_decimateBlock).
	 *
	 * What:       sample & hold decimation loop. Each sample advances the
	 *             fractional accumulator by decimationRate. On overflow (>= 1.0)
	 *             the counter wraps and the input sample is latched into
	 *             decimationHeld. Every output is decimationHeld.
	 * Why:        moved from mixer globals into the engine struct so SRR is part
	 *             of the voice chain budget for S089 P1.
	 * Signal path: after voicePost_mixAddGainDist, before mixer volume/pan.
	 * Rate = 1.0: bit-identical passthrough (accumulator overflows every sample).
	 * Inputs:     voice->decimationRate/Cnt/Held, buf, size.
	 * Outputs:    buf overwritten in place; struct members updated.
	 * Affiliates: SLOT_DECIMATION binding (InstrumentManager.c); Snare_initVoice();
	 *             mixer.c (removed decimateBlock call).
	 */
	{
		float cnt  = voice->decimationCnt;
		float rate = voice->decimationRate;
		int16_t held = voice->decimationHeld;
		uint8_t j;
		for (j = 0; j < size; j++) {
			cnt += rate;
			if (cnt >= 1.f) {
				cnt -= 1.f;
				held = buf[j];
			}
			buf[j] = held;
		}
		voice->decimationCnt  = cnt;
		voice->decimationHeld = held;
	}
```

Note: Snare's post-chain loop uses `j` as its iterator, so the S&H loop
follows the same convention.

---

## Change 11 — Cymbal render: add S&H loop

**File:** `Core/DSP/Instruments/Cymbal/CymbalVoice.c`  
**Line:** 454 (after `voice->ampRampArm = 0u;`), before the closing `}` of the
post-chain scope at line 455, then before the function-closing `}` at line 456  
**Action:** ADD S&H loop after the post-chain scope closes (after line 455)

```c
/* ADD after line 455 (closing } of post-chain scope): */

	/*
	 * Sample-rate reduction (S089 P0: moved from mixer_decimateBlock).
	 *
	 * What:       sample & hold decimation loop. On accumulator overflow the
	 *             input sample is latched; every output is the held sample.
	 * Why:        moved from mixer globals into the engine struct so SRR is part
	 *             of the voice chain budget for S089 P1.
	 * Signal path: after voicePost_addGainDist, before mixer volume/pan.
	 * Rate = 1.0: bit-identical passthrough.
	 * Inputs:     voice->decimationRate/Cnt/Held, buf, size.
	 * Outputs:    buf overwritten in place; struct members updated.
	 * Affiliates: SLOT_DECIMATION binding (InstrumentManager.c);
	 *             Cymbal_initVoice(); mixer.c (removed decimateBlock call).
	 */
	{
		float cnt  = voice->decimationCnt;
		float rate = voice->decimationRate;
		int16_t held = voice->decimationHeld;
		uint8_t j;
		for (j = 0; j < size; j++) {
			cnt += rate;
			if (cnt >= 1.f) {
				cnt -= 1.f;
				held = buf[j];
			}
			buf[j] = held;
		}
		voice->decimationCnt  = cnt;
		voice->decimationHeld = held;
	}
```

---

## Change 12 — HiHat render: add S&H loop

**File:** `Core/DSP/Instruments/HiHat/HiHat.c`  
**Line:** 473 (after `voice->ampRampArm = 0u;`), before the closing `}` of the
post-chain scope at line 474, then before the function-closing `}` at line 475  
**Action:** ADD S&H loop after the post-chain scope closes (after line 474)

```c
/* ADD after line 474 (closing } of post-chain scope): */

	/*
	 * Sample-rate reduction (S089 P0: moved from mixer_decimateBlock).
	 *
	 * What:       sample & hold decimation loop. On accumulator overflow the
	 *             input sample is latched; every output is the held sample.
	 * Why:        moved from mixer globals into the engine struct so SRR is part
	 *             of the voice chain budget for S089 P1.
	 * Signal path: after voicePost_addGainDist, before mixer volume/pan.
	 * Rate = 1.0: bit-identical passthrough.
	 * Inputs:     voice->decimationRate/Cnt/Held, buf, size.
	 * Outputs:    buf overwritten in place; struct members updated.
	 * Affiliates: SLOT_DECIMATION binding (InstrumentManager.c);
	 *             HiHat_initVoice(); mixer.c (removed decimateBlock call).
	 */
	{
		float cnt  = voice->decimationCnt;
		float rate = voice->decimationRate;
		int16_t held = voice->decimationHeld;
		uint8_t j;
		for (j = 0; j < size; j++) {
			cnt += rate;
			if (cnt >= 1.f) {
				cnt -= 1.f;
				held = buf[j];
			}
			buf[j] = held;
		}
		voice->decimationCnt  = cnt;
		voice->decimationHeld = held;
	}
```

---

## Change 13 — Binding rewrite: SLOT_DECIMATION writes engine struct

**File:** `Core/DSP/Instruments/InstrumentManager.c`  
**Lines:** 3906–3911  
**Action:** MODIFY — replace the mixer array write with a per-type engine
struct write

The existing case writes `mixer_decimation_rate[slot]`. The new case resolves
the engine struct for the slot's active type and writes its `decimationRate`
member. The `instrumentManager_runtimeInstance()` helper already resolves
slot → void* engine pointer; however, writing `decimationRate` needs the
typed pointer, so a type switch is required. This follows the same pattern
as `instrumentManager_runtimeLfo()` (lines 1412–1435).

```c
/* REPLACE lines 3906–3911 with: */

    case INSTRUMENT_BIND_SLOT_DECIMATION:
        /*
         * Write sample-rate reduction rate to the engine struct (S089 P0).
         *
         * What:       resolves the voice engine for this slot's current
         *             instrument type and writes its decimationRate member.
         *             valueShaperI2F with shape -0.7f maps the 0..127 parameter
         *             range to 0..1 with a concave curve: most of the pot
         *             travel covers the low-rate region where the staircase
         *             effect is audible, while the upper range quickly reaches
         *             full sample rate.
         * Why:        SRR state moved from mixer-global arrays into the engine
         *             struct (S089 P0). The supplemental modulation paths
         *             (LFO and velocity INSTALLED_MOD_TARGET_SLOT_DECIMATION)
         *             funnel through instrumentManager_writeRuntime() and hit
         *             this same case, so no modulation code needs changing.
         * Inputs:     slot (0..5), value (0..127 descriptor/modulation value).
         * Outputs:    engine->decimationRate updated.
         * Affiliates: instrumentManager_applySlotDecimationTarget() (calls this
         *             through writeRuntime); instrumentManager_slotDecimationBase()
         *             (reads Morph image, unchanged); the S&H loops in each
         *             engine's calcSyncBlock; Drum/Snare/Cymbal/HiHat initVoice.
         */
        if (slot >= INSTRUMENT_SLOT_COUNT)
            return 0u;
        {
            float rate = valueShaperI2F(
                (uint8_t)((value > 127u) ? 127u : value), -0.7f);
            switch (instrumentManager_slotType(slot)) {
            case INSTRUMENT_TYPE_DRM: {
                DrumVoice *v = instrumentManager_drumRuntime(slot);
                if (v) v->decimationRate = rate;
                break; }
            case INSTRUMENT_TYPE_SNR: {
                SnareVoice *v = instrumentManager_snareRuntime(slot);
                if (v) v->decimationRate = rate;
                break; }
            case INSTRUMENT_TYPE_CYM: {
                CymbalVoice *v = instrumentManager_cymbalRuntime(slot);
                if (v) v->decimationRate = rate;
                break; }
            case INSTRUMENT_TYPE_HAT: {
                HiHatVoice *v = instrumentManager_hihatRuntime(slot);
                if (v) v->decimationRate = rate;
                break; }
            default:
                return 0u;
            }
        }
        return 1u;
```

---

## Change 14 — Mixer: remove decimateBlock call

**File:** `Core/DSPAudio/mixer.c`  
**Line:** 1015  
**Action:** REMOVE the `mixer_decimateBlock(slot, sampleData);` call

```c
/* REMOVE line 1015: */
//  mixer_decimateBlock(slot,sampleData);   /* S089 P0: moved into engine */
```

The `mixer_decimateBlock()` function definition (lines 171–186) and the
mixer-global arrays (lines 148–150) are left in place, guarded by
`#if USE_DECIMATOR`, as dead code for safe rollback. They can be removed
in a cleanup commit after hardware verification.

---

## Change 15 — Mixer comment update: "decimated" reference

**File:** `Core/DSPAudio/mixer.c`  
**Lines:** 1016–1018  
**Action:** MODIFY comment to reflect that decimation now happens inside the
engine render

```c
/* REPLACE lines 1016–1018: */
/* OLD: */
		/*
		 * sampleData is the decimated, pre-volume voice block. The fader mode
/* NEW: */
		/*
		 * sampleData is the pre-volume voice block (decimated inside the engine
		 * render since S089 P0, no longer by mixer_decimateBlock). The fader mode
```

---

## Change 16 — InstrumentManager calcSlotSyncBlock comment update

**File:** `Core/DSP/Instruments/InstrumentManager.c`  
**Lines:** 1977  
**Action:** MODIFY comment to reflect that engine now handles decimation

```c
/* REPLACE line 1977: */
/* OLD: */
     * the selected engine writes a mono voice block WITHOUT channel volume, or
/* NEW: */
     * the selected engine writes a mono, decimated voice block WITHOUT channel volume, or
```

**Lines:** 1977–1980 (the sentence about mixer responsibilities)  
**Action:** MODIFY

```c
/* REPLACE lines 1978–1980: */
/* OLD: */
     * silence for unknown slot/type. Mixer remains responsible for decimation,
     * channel volume (instrumentManager_runtimeVolume(), Session 072 step 2),
     * pan, routing, and slider interpolation after this call. The pre-volume
/* NEW: */
     * silence for unknown slot/type. Decimation now runs inside each engine's
     * calcSyncBlock (S089 P0). Mixer remains responsible for channel volume
     * (instrumentManager_runtimeVolume(), Session 072 step 2), pan, routing,
     * and slider interpolation after this call. The pre-volume
```

---

## Change 17 — Drum render volume comment update

**File:** `Core/DSP/Instruments/Drum/DrumVoice.c`  
**Lines:** 865–870  
**Action:** MODIFY comment — "decimated block" language now refers to the
engine's own S&H loop rather than the mixer

```c
/* REPLACE lines 865–870 with: */
	/*
	 * Channel volume is intentionally not applied here (Session 072 step 2).
	 * The mixer multiplies this block (decimated above since S089 P0) by
	 * voice->vol x slider gain; see instrumentManager_runtimeVolume() and
	 * mixer_calcNextSampleBlock(). Re-adding a gain stage here would apply
	 * volume twice.
	 */
```

---

## Change 18 — ROW_SLOT_DECIMATION comment updates (4 files)

The documentation block above each `ROW_SLOT_DECIMATION` macro references
`mixer_decimation_rate[slot]` as the binding destination. Update all four
to reference the engine struct.

### 18a — DrumParameters.c

**File:** `Core/DSP/Instruments/Drum/DrumParameters.c`  
**Line:** 155  
**Action:** MODIFY

```c
/* OLD (line 155): */
 * applies this binding through mixer_decimation_rate[slot]; the modulation and
/* NEW: */
 * applies this binding through the engine struct's decimationRate member
 * (S089 P0; was mixer_decimation_rate[slot]); the modulation and
```

### 18b — SnareParameters.c

**File:** `Core/DSP/Instruments/Snare/SnareParameters.c`  
**Line:** 151  
**Action:** MODIFY

```c
/* OLD (line 151): */
 * applies this binding through mixer_decimation_rate[slot]; the modulation and
/* NEW: */
 * applies this binding through the engine struct's decimationRate member
 * (S089 P0; was mixer_decimation_rate[slot]); the modulation and
```

### 18c — CymbalParameters.c

**File:** `Core/DSP/Instruments/Cymbal/CymbalParameters.c`  
**Line:** 149  
**Action:** MODIFY

```c
/* OLD (line 149): */
 * applies this binding through mixer_decimation_rate[slot]; the modulation and
/* NEW: */
 * applies this binding through the engine struct's decimationRate member
 * (S089 P0; was mixer_decimation_rate[slot]); the modulation and
```

### 18d — HiHatParameters.c

**File:** `Core/DSP/Instruments/HiHat/HiHatParameters.c`  
**Line:** 150  
**Action:** MODIFY

```c
/* OLD (line 150): */
 * applies this binding through mixer_decimation_rate[slot]; the modulation and
/* NEW: */
 * applies this binding through the engine struct's decimationRate member
 * (S089 P0; was mixer_decimation_rate[slot]); the modulation and
```

---

## Change 19 — InstrumentManager.c comment updates (2 sites)

### 19a — Target validator comment

**File:** `Core/DSP/Instruments/InstrumentManager.c`  
**Lines:** 920–922  
**Action:** MODIFY

```c
/* OLD (lines 920–922): */
         * supplemental voice target: it is descriptor-owned and image-backed,
         * but applies through mixer_decimation_rate[slot] rather than a
         * voice-struct member pointer. Keeping both cases in the shared
/* NEW: */
         * supplemental voice target: it is descriptor-owned and image-backed,
         * and applies through the engine struct's decimationRate member
         * (S089 P0; was mixer_decimation_rate[slot]). Keeping both cases in the shared
```

### 19b — applySlotDecimationTarget helper comment

**File:** `Core/DSP/Instruments/InstrumentManager.c`  
**Lines:** 2724–2728  
**Action:** MODIFY

```c
/* OLD (lines 2724–2728): */
     * Inputs: canonical descriptor target ID and 0..127 value. Output:
     * InstrumentManager's existing supplemental runtime writer updates
     * mixer_decimation_rate[target_slot]. This helper keeps velocity and LFO
     * supplemental modulation paths from duplicating descriptor decode and
     * binding rules.
/* NEW: */
     * Inputs: canonical descriptor target ID and 0..127 value. Output:
     * InstrumentManager's supplemental runtime writer updates the engine
     * struct's decimationRate for the target slot (S089 P0; was
     * mixer_decimation_rate[target_slot]). This helper keeps velocity and LFO
     * supplemental modulation paths from duplicating descriptor decode and
     * binding rules.
```

---

## Summary: All Changes by File

| #   | File                                    | Lines      | Action | What                                      |
|-----|-----------------------------------------|------------|--------|-------------------------------------------|
| 1   | Drum/DrumVoice.h                        | 205–206+   | MOD+ADD| Document existing fields, add decimationHeld |
| 2   | Snare/Snare.h                           | before 147 | ADD    | 3 struct members + doc                    |
| 3   | Cymbal/CymbalVoice.h                    | before 139 | ADD    | 3 struct members + doc                    |
| 4   | HiHat/HiHat.h                           | before 146 | ADD    | 3 struct members + doc                    |
| 5   | Drum/DrumVoice.c                        | 183–184    | MOD    | Document init, add decimationHeld init    |
| 6   | Snare/Snare.c                           | after 121  | ADD    | Decimation init in Snare_initVoice        |
| 7   | Cymbal/CymbalVoice.c                    | after 138  | ADD    | Decimation init in Cymbal_initVoice       |
| 8   | HiHat/HiHat.c                           | after 150  | ADD    | Decimation init in HiHat_initVoice        |
| 9   | Drum/DrumVoice.c                        | after 864  | ADD    | S&H loop in Drum_calcVoiceSyncBlock       |
| 10  | Snare/Snare.c                           | after 421  | ADD    | S&H loop in Snare_calcSyncBlockVoice      |
| 11  | Cymbal/CymbalVoice.c                    | after 455  | ADD    | S&H loop in Cymbal_calcSyncBlockVoice     |
| 12  | HiHat/HiHat.c                           | after 474  | ADD    | S&H loop in HiHat_calcSyncBlockVoice      |
| 13  | InstrumentManager.c                     | 3906–3911  | MOD    | Binding writes engine struct              |
| 14  | mixer.c                                 | 1015       | REMOVE | mixer_decimateBlock call                  |
| 15  | mixer.c                                 | 1016–1018  | MOD    | Comment update                            |
| 16  | InstrumentManager.c                     | 1977–1980  | MOD    | calcSlotSyncBlock comment                 |
| 17  | Drum/DrumVoice.c                        | 865–870    | MOD    | Volume comment update                     |
| 18a | Drum/DrumParameters.c                   | 155        | MOD    | ROW_SLOT_DECIMATION comment               |
| 18b | Snare/SnareParameters.c                 | 151        | MOD    | ROW_SLOT_DECIMATION comment               |
| 18c | Cymbal/CymbalParameters.c               | 149        | MOD    | ROW_SLOT_DECIMATION comment               |
| 18d | HiHat/HiHatParameters.c                 | 150        | MOD    | ROW_SLOT_DECIMATION comment               |
| 19a | InstrumentManager.c                     | 920–922    | MOD    | Target validator comment                  |
| 19b | InstrumentManager.c                     | 2724–2728  | MOD    | applySlotDecimationTarget comment         |

**Total files touched:** 14  
**Total changes:** 22 (4 struct additions, 4 init additions, 4 S&H loop
additions, 1 binding rewrite, 1 call removal, 8 comment updates)

---

## Implementation Order

1. **Struct additions** (Changes 1–4) — all four headers, no functional change
2. **Init additions** (Changes 5–8) — all four init functions
3. **Binding rewrite** (Change 13) — InstrumentManager.c SLOT_DECIMATION case
4. **S&H loops** (Changes 9–12) — all four render functions
5. **Mixer call removal** (Change 14) — mixer.c line 1015
6. **Comment updates** (Changes 15–19) — all documentation references
7. **Build + verify** — compile, run bit-identity test with SRR at default

---

## Implementation Record (2026-10-30)

### What landed

All 22 changes from the schedule are applied, in the documented order, each
with its adjacent comment block:

| # | File | Where it landed | Status |
|---|------|-----------------|--------|
| 1 | `Drum/DrumVoice.h` | struct doc block + `decimationHeld` (line 206 area) | done |
| 2 | `Snare/Snare.h` | struct doc block + 3 members before `} SnareVoice` | done |
| 3 | `Cymbal/CymbalVoice.h` | struct doc block + 3 members before `} CymbalVoice` | done |
| 4 | `HiHat/HiHat.h` | struct doc block + 3 members before `} HiHatVoice` | done |
| 5 | `Drum/DrumVoice.c` | `Drum_initVoice()`: documented init + `decimationHeld = 0` | done |
| 6 | `Snare/Snare.c` | `Snare_initVoice()`: decimation init | done |
| 7 | `Cymbal/CymbalVoice.c` | `Cymbal_initVoice()`: decimation init | done |
| 8 | `HiHat/HiHat.c` | `HiHat_initVoice()`: decimation init | done |
| 9 | `Drum/DrumVoice.c` | S&H loop after `ampRampArm = 0u`, before volume comment | done |
| 10 | `Snare/Snare.c` | S&H loop after the post-chain scope | done |
| 11 | `Cymbal/CymbalVoice.c` | S&H loop after the post-chain scope | done |
| 12 | `HiHat/HiHat.c` | S&H loop after the post-chain scope | done |
| 13 | `InstrumentManager.c` | `INSTRUMENT_BIND_SLOT_DECIMATION` type switch writes `decimationRate` | done |
| 14 | `mixer.c` | `mixer_decimateBlock()` call removed (arrays/function kept) | done |
| 15 | `mixer.c` | `sampleData` comment updated | done |
| 16 | `InstrumentManager.c` | `calcSlotSyncBlock` comment updated | done |
| 17 | `Drum/DrumVoice.c` | volume comment updated | done |
| 18a–d | `*Parameters.c` x4 | `ROW_SLOT_DECIMATION` doc now names the engine member | done |
| 19a–b | `InstrumentManager.c` | validator + `applySlotDecimationTarget` comments | done |

### Findings and deviations

1. **The plan's size figures predate S088.** The schedule says DrumVoice is
   588 B; it is actually 632 B at this revision (`STORAGE_SRAM_MANIFEST.md`
   line 22 records the S088 P0 growth). The applied change is still +4 B:
   DrumVoice is now **636 B** (two floats + int16 + 2 B alignment pad).
   `INSTRUMENT_RUNTIME_SLOT_BYTES` is derived as `2 x sizeof(DrumVoice)`, so
   the tagged slot reserve went 1,264 -> **1,272 B** per slot and `runtime_slots`
   went 7,584 -> **7,632 B** (6 x 8 B, +48 B regular SRAM). This is inside the
   user-approved expansion quoted in the plan (+4 B DrumVoice, +12 B per other
   engine); the four `_Static_assert`s still pass.
2. **Snare/Cymbal/HiHat grow by 12 B each** (4 + 4 + 2 + 2 pad) and remain
   below the 636 B slot ceiling; no reserve change beyond the DrumVoice term.
3. **Binding shape.** Change 13 is applied with the per-type `switch` shown in
   the schedule, using the existing `instrumentManager_slotType()`,
   `instrumentManager_drumRuntime()` / `..._snareRuntime()` / `..._cymbalRuntime()`
   / `..._hihatRuntime()` accessors. `instrumentManager_slotType()` is already
   forward-declared at the top of `InstrumentManager.c`, so no new declaration
   was needed. `mixer.h` is still included there; its only use was the removed
   array write, so the include can go with the cleanup commit.
4. **Reset re-applies the rate.** `instrumentManager_resetRuntimeSlotFromScene()`
   memsets the slot, runs `*_initVoice()` (rate = 1), then the caller applies
   the full Morph/descriptor image (`presetMorph_applyVoiceNow()`), which writes
   `instrument_decimation` again. This matches the old end state, and it also
   clears the S&H accumulator/held sample on a Scene/type rebind where the
   mixer globals previously carried over. With rate = 1 that carry-over was
   zero anyway; at fractional rates a rebind now starts clean.
5. **Unknown-type slots.** `instrumentManager_calcSlotSyncBlock()` still
   zero-fills the buffer for an unknown slot/type and does not run an engine,
   so those transient blocks are no longer decimated. This only affects the
   temporary state during a type/Scene transition and removes a stale
   held-sample leak from that path.
6. **Rate = 1 is exact.** `valueShaperI2F()` at 127 evaluates to exactly 1.0f
   (identical numerator/denominator), so the accumulator wraps every sample
   and the loop is a bit-identical pass-through from any carried state.

### Verification performed

- **Firmware build:** `make all` compiles and links `build/lxr02.elf`
  (text 676,744 / data 420 / bss 412,936). `runtime_slots` is 0x1dd0 = 7,632 B
  in the linked image, confirming the +48 B slot-reserve recalc. `make img`
  produced `build/LXRV2_lxr02.img` (677,164 B payload, checksum OK).
  Note: `make all`'s final report step calls `tools/link_budget.py`, which is
  absent from this checkout (pre-existing; not caused by this change), so the
  make target reports an error after a successful link.
- **Host module tests:** `oscillator`, `instrument_manager` (including its
  break builds) and `special_tags` pass. `make -C tools/dsp_test all` stops at
  `run_modes` because `modules/run_modes.c` declares only
  `distortion_curveSample16` while the extracted `voicePostChain.h` now calls
  `distortion_curveSampleF`; that is a pre-existing S088 test-bench gap in
  untouched files, not an S089 P0 failure.
- **Algorithm equivalence harness** (temporary, `/tmp/s089_srr/eq.c`): the
  removed `mixer_decimateBlock()` body and the new per-voice loop were driven
  side by side with identical inputs. All 128 rates produced by
  `valueShaperI2F(v, -0.7f)` over 200 blocks of 32 samples matched exactly in
  output samples, `cnt` and `held`. A further 1,000 blocks with rate = 1.0 and
  random carried `cnt`/`held` were bit-identical to the input. Shaper probe:
  `0 -> 0`, `64 -> 0.152054`, `127 -> 1.0`.
- **Generated test fragments:** running the bench regenerated
  `tools/dsp_test/build/gen/im_gate.c`, `build/break_gate/im_gate.c` (both now
  carry the updated comment) and `build/gen/rm_postchain.c` (already stale
  against the S088 `voicePostChain.h`).
- **Cost:** the moved loop is the same text with the same `-Ofast` flags, so
  total per-sample work is unchanged; the plan's ~15 cycles/sample is now
  attributed to the voice chain instead of the mixer. An ARM instruction
  count of the isolated loop was not taken because LTO inlines all four
  engine renders into `mixer_calcNextSampleBlock()` (expected per the S086
  carried-debt note), so a loop body cannot be attributed to one engine in
  the linked image without a spike build.

### Not done here (deliberate)

- Mixer dead code left in place per Change 14: `mixer_decimation_rate/cnt/`
  `voice_samples`, `mixer_decimateBlock()`, its `mixer.h` extern, and the
  `mixer_init()` seeds. They are unreferenced by the render path and are
  dropped by `--gc-sections`; remove them in the post-hardware cleanup commit.
- `mixer.c` line ~1002 still lists "decimation" among mixer-state duties in the
  slot-loop comment. Left untouched because it belongs to the same cleanup
  pass as the arrays.
- `STORAGE_SRAM_MANIFEST.md` still records `runtime_slots` 7,584 B and
  DrumVoice 632 B; update that row to 7,632 B / 636 B in the cleanup commit.

### Hardware / listening checks still open (plan §8)

1. Bit-identity at SRR default (rate = 127): same pattern as the pre-change
   image, every sample equal.
2. Low SRR (~20) staircase identical before/after on a sustained sound.
3. LFO assigned to `SampleRt`: smooth sweep, restores to base when cleared.
4. Velocity assigned to `SampleRt`: onset modulation and restore on retrigger.

---

## Post-Implementation Assessment (2026-10-30)

All 22 scheduled changes verified against live source. Each change matches
the schedule's intent, documentation blocks, and algorithm.

### Verified correct

| Area | Verdict | Detail |
|------|---------|--------|
| **Struct additions (1–4)** | Match | All four headers carry the doc block and three members (`decimationCnt`, `decimationRate`, `decimationHeld`). DrumVoice reuses its existing two fields and adds only `decimationHeld`; the other three add all three. |
| **Init additions (5–8)** | Match | All four `initVoice()` functions set rate=1, cnt=0, held=0 with the scheduled doc block. Drum uses integer literals (`0`, `1`) vs. the other three using float suffixes (`0.f`, `1.f`); both are correct C for float assignment. |
| **Binding rewrite (13)** | Match | `INSTRUMENT_BIND_SLOT_DECIMATION` case now resolves per-type and writes `decimationRate` through the typed runtime accessors. Guard, clamp, and `valueShaperI2F` call preserved. |
| **S&H loops (9–12)** | Match | All four render functions carry the loop after the post-chain/arm-clear, before the function returns. Algorithm is identical to the removed `mixer_decimateBlock()`. Drum uses `i`, others use `j`, matching their surrounding loop conventions. |
| **Mixer removal (14)** | Match | `mixer_decimateBlock(slot, sampleData)` call is gone. Arrays, function definition, and `mixer.h` externs left as dead code per plan. |
| **Comment updates (15–19)** | Match | All eight comment sites updated. The calcSlotSyncBlock, volume, ROW_SLOT_DECIMATION, target-validator, and applySlotDecimationTarget comments all reference the engine struct and cite S089 P0. |

### Minor deviations from schedule (non-issues)

1. **Change 19a wording:** schedule said `"and applies through the engine
   struct's decimationRate member"`, implementation kept `"but applies
   through …"`. The surrounding clause (`"rather than a voice-struct member
   pointer"`) makes `"but"` grammatically correct in context. The `"rather
   than"` phrasing is a historical artifact of the original comment's
   contrast between supplemental and pointer bindings; it no longer describes
   the current state (decimationRate *is* a voice-struct member now). This is
   cosmetic and does not affect correctness; can be cleaned up in the
   post-hardware pass.

2. **Drum init literals:** schedule specified `0.f` / `1.f`; implementation
   uses `0` / `1` (matching the pre-existing style from the vestigial init).
   Functionally identical — the compiler promotes to float.

### Findings beyond schedule

1. **Change 19a residual inaccuracy:** the comment at InstrumentManager.c:923
   now says `"applies through the engine struct's decimationRate member …
   rather than a voice-struct member pointer"`. Since `decimationRate` *is* a
   voice-struct member, the `"rather than"` clause is misleading. The
   original comment's intent was to contrast the supplemental binding
   (SLOT_DECIMATION) with the instance-offset binding (INSTANCE_OFFSET) —
   that contrast is still valid but the wording should drop `"rather than a
   voice-struct member pointer"` since the binding now writes exactly that.
   **Recommend:** fix in the cleanup commit.

2. **mixer.h include in InstrumentManager.c** (line 15) is now unreferenced
   by code — only by `"was mixer_…"` strings in comments. The implementation
   record correctly flags this for cleanup.

3. **STORAGE_SRAM_MANIFEST.md** row stale (636 B DrumVoice, 7632 B
   runtime_slots) — correctly flagged for cleanup.

### Verdict

Implementation is **correct and complete** per the schedule. No functional
issues found. Three cosmetic items (Change 19a wording, Drum init literals,
manifest row) are flagged for the post-hardware cleanup commit. Ready for
hardware verification (plan §8 items 1–4).

---

## Hardware Verification — Fix 1: noinline S&H extract

**Problem:** Interface freeze on worst-case scene switch after the SRR move.

**Root cause:** LTO inlines all four engine renders into
`mixer_calcNextSampleBlock()`, producing a single 13,904-byte function (87% of
the 16 KB I-cache). The SRR move added four copies of the S&H loop (~400 bytes)
where the old code had one inlined `mixer_decimateBlock` (~100 bytes). On worst-
case scenes (all 6 voice paths exercised plus math library calls), the enlarged
function exceeded the I-cache conflict threshold, adding enough stall cycles
to overrun the block deadline. The render interrupt monopolized the CPU and
starved the main loop.

**Fix:** Extract the S&H loop into a single `__attribute__((noinline))` function
(`voice_decimateBlock`, mixer.c) called from all four engines via
`voicePostChain.h`. LTO cannot inline it, so the function body exists once
(74 bytes) outside the mixer function. The four inline loops are replaced by
four `bl` call sites (~8 bytes each).

**Result:**
| Metric | Pre-fix | Post-fix | Δ |
|--------|---------|----------|---|
| `mixer_calcNextSampleBlock` | 13,904 B | 13,580 B | −324 B |
| `voice_decimateBlock` | (inlined) | 74 B | — |
| .img | 677,180 B | 676,924 B | −256 B |
| text | 676,744 B | 676,504 B | −240 B |
| bss | 412,936 B | 412,936 B | 0 |

**Files touched:**
- `Core/DSPAudio/mixer.c` — added `voice_decimateBlock()` (noinline)
- `Core/DSPAudio/voicePostChain.h` — added declaration
- `Core/DSP/Instruments/Drum/DrumVoice.c` — replaced inline S&H with call
- `Core/DSP/Instruments/Snare/Snare.c` — replaced inline S&H with call
- `Core/DSP/Instruments/Cymbal/CymbalVoice.c` — replaced inline S&H with call
- `Core/DSP/Instruments/HiHat/HiHat.c` — replaced inline S&H with call

**Signal path:** unchanged. The function receives pointers to the engine struct's
decimation members and operates on the same buffer at the same pipeline position.
**Constant CPU rule:** unchanged. The function always runs, even at rate = 1.0.
**P1 compatibility:** the voice chain dispatch can call `voice_decimateBlock()` as
one of its chain stages, or replace it with an alternative final stage.
