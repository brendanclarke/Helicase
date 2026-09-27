# S072 Step 2 — Implementation Schedule: Voice Volume Relocation (and Volume-Order Bug Fix)

**Scope:** `EFFECTS_BUS_FEATURE_PLAN.md` §8.1 and §17.1 Step 2. Voice volume
(`instrument_vol`) moves out of the four voice engines' render functions and
is applied by the mixer, after decimation and before pan. The runtime `vol`
field, its descriptor binding, and every writer (menu, Morph, LFO, Pattern
automation, Scene activation) are unchanged. The mixer reads the value through
a new `instrumentManager_runtimeVolume()`.

**Why this is its own step:** it fixes the volume-order bug on Snare,
Cymbal, and HiHat (D1, §0), where volume was applied before distortion
instead of last. It also prepares the Step 5 FX send, which taps
post-decimation and pre-volume (user decisions A24/A25). It touches all four
engines and the mixer, so it is verified alone before any bus exists.

**Status:** implementation complete for source/build scope. Clean production
build and static verification passed. Hardware listening gates remain pending
because no hardware run is available in this session.

**Line numbers** refer to the working tree after Step 1 (base `a0531ae` plus
the uncommitted Step 1 changes). The Step 2 files (`DrumVoice.c/.h`,
`Snare.c/.h`, `CymbalVoice.c/.h`, `HiHat.c/.h`, `InstrumentManager.c/.h`,
`mixer.c`) were not touched by Step 1, so their lines match `a0531ae`.

**RAM:** no change. One DTCM array is renamed and its meaning widened (§17);
no bytes are added or removed. The exact approved allocation remains 24 bytes
in DTCM for six combined-gain ramp values, with the same owner and lifetime as
the Step 1 mixer ramp array.

---

## 0. Decision D1 — resolved: bug fix, volume last on every engine

**User decision (2026-09-27):** channel volume must be the **last** stage on
every instrument engine. The Snare/Cymbal/HiHat ordering, where volume is
applied before distortion, is a **bug**. It is fixed in this step and is not
treated as a behaviour trade-off.

| Engine | Order before this step | Order after this step |
|---|---|---|
| Drum (`DrumVoice.c:284–345`) | … filter → amp EG → velocity → distortion (`:341`) → `vol` (`:344`) | … distortion → *(end)*; `vol` applied by the mixer |
| Snare (`Snare.c:204–254`) | … → **`velo × vol × EG`** (`:238`/`:248`) → distortion (`:252`) | … → `velo × EG` → distortion → *(end)*; `vol` in the mixer |
| Cymbal (`CymbalVoice.c:212–263`) | … → **`velo × vol × EG`** (`:251`/`:260`) → distortion (`:263`) | … → `velo × EG` → distortion → *(end)*; `vol` in the mixer |
| HiHat (`HiHat.c:231–282`) | … → **`velo × vol × EG`** (`:268`/`:277`) → distortion (`:281`) | … → `velo × EG` → distortion → *(end)*; `vol` in the mixer |

**Why it was a bug:**

- The distortion (`distortion.c:61`, `y = (1+k)·x / (1+k·|x|)`, with `k` from
  `instrument_drive`) is non-linear. On Snare, Cymbal, and HiHat, `vol`
  therefore also acted as a hidden drive control: lowering the volume reduced
  the saturation.
- Drum already applied `vol` last, so the four engines were inconsistent.
- After the fix, `vol` is a pure output level on every engine, and the Step 5
  FX send's "pre-volume" tap includes each voice's distortion.

**Expected audible effect of the fix:**

- With `drv = 0`, nothing changes apart from sub-LSB int16 rounding.
- Snare, Cymbal, or HiHat sounds with `drv > 0` and `vol < 127` become more
  saturated at the same `vol`, because the drive no longer depends on volume.
  Existing kits with that combination may need `drv` or `vol` retuned. This is
  the corrected behaviour, not a regression.
- Drum is unchanged.

### Informational (no decision needed)

- **Volume changes are now smoothed.** Before, `vol` was a constant
  multiplier per 32-frame block, so a knob, LFO, or automation change stepped
  at block boundaries. After, it joins the mixer's existing per-block linear
  gain ramp (currently used for the slider), so changes ramp across the block.
  This is audibly equal or smoother; LFO on `vol` loses its block-rate
  stepping.
- **CPU is expected to fall slightly.** The move removes one 32-sample
  multiply pass from Drum (`bufferTool_addGain`) and one multiply per sample
  from Snare, Cymbal, and HiHat. It adds one `instrumentManager_runtimeVolume()`
  call and one multiply per slot per block in the mixer.
- **No new saturation risk.** `vol` is `value / 127` (0..1;
  `InstrumentManager.c:1703–1715`). The engine output after the move equals
  the output at `vol = 127`, which is already a legal setting. The
  distortion's output magnitude stays at or below 1 for inputs at or below 1,
  so the int16 range holds.

---

## 1. Change index

| # | File | Line(s) | Op | Summary |
|---|---|---|---|---|
| 1 | `Core/DSP/Instruments/Drum/DrumVoice.c` | 286–292 | modify | Render contract: output is pre-volume |
| 2 | `Core/DSP/Instruments/Drum/DrumVoice.c` | 343–344 | remove | Drop the final `vol` gain stage |
| 3 | `Core/DSP/Instruments/Drum/DrumVoice.h` | 65 | modify | `vol` member contract |
| 4 | `Core/DSP/Instruments/Snare/Snare.c` | 207–213 | modify | Render contract |
| 5 | `Core/DSP/Instruments/Snare/Snare.c` | 238, 248 | modify | Remove `vol` from the envelope multiply |
| 6 | `Core/DSP/Instruments/Snare/Snare.h` | 61 | modify | `vol` member contract |
| 7 | `Core/DSP/Instruments/Cymbal/CymbalVoice.c` | 215–220 | modify | Render contract |
| 8 | `Core/DSP/Instruments/Cymbal/CymbalVoice.c` | 251, 260 | modify | Remove `vol` from the envelope multiply |
| 9 | `Core/DSP/Instruments/Cymbal/CymbalVoice.h` | 61 | modify | `vol` member contract |
| 10 | `Core/DSP/Instruments/HiHat/HiHat.c` | 234–240 | modify | Render contract |
| 11 | `Core/DSP/Instruments/HiHat/HiHat.c` | 268, 277 | modify | Remove `vol` from the envelope multiply |
| 12 | `Core/DSP/Instruments/HiHat/HiHat.h` | 64 | modify | `vol` member contract |
| 13 | `Core/DSP/Instruments/InstrumentManager.h` | after 489 | add | `instrumentManager_runtimeVolume()` declaration |
| 14 | `Core/DSP/Instruments/InstrumentManager.c` | after 1502 | add | `instrumentManager_runtimeVolume()` definition |
| 15 | `Core/DSP/Instruments/InstrumentManager.c` | 1569–1576 | modify | `calcSlotSyncBlock` contract: mixer applies volume |
| 16 | `Core/DSPAudio/mixer.c` | 64 | modify | Rename `mixer_slider_last_gain` → `mixer_voice_last_gain` |
| 17 | `Core/DSPAudio/mixer.c` | 86 | modify | Init the combined last gain |
| 18 | `Core/DSPAudio/mixer.c` | 365–371 | modify | Fused-loop comment: the gain is slider × volume |
| 19 | `Core/DSPAudio/mixer.c` | 547–567 | modify | Per-slot loop applies slider × volume |
| 20 | `knowledge_files/specification_reference/MODULE_INTERCHANGE_SPEC.md` | 680 | modify | Add volume to the runtime dispatch family |
| 21 | `MEMORY.md` | Volatile Notes | add | One-line Step 2 note |

**Unchanged, verified:**

- The `vol` writers: the descriptor `instrument_vol` binding
  (`DrumParameters.c:188` and siblings), `instrumentManager_writeRuntime`, LFO
  adapters, Morph, and Pattern automation.
- The legacy `MidiParser.c:733–992` `vol` writes, which sit inside
  `#if 0` (from line 476) and are not compiled.
- `bufferTool_addGain()` stays: Drum's velocity stage still uses it.
- `adcPots.c/.h`: `slider_vol[]` meaning is unchanged.

---

## 2. `DrumVoice.c` — lines 286–292 (modify the in-function contract comment)

Before:

```c
	/*
	 * Render one drum runtime instance into a mono block.
	 *
 * Inputs: DrumVoice pointer, destination buffer, and block size. Output: buf
 * receives one explicit tagged-slot drum block. Mixer selects this object by
 * current runtime type rather than by a hardcoded drum voice index.
	 */
```

After:

```c
	/*
	 * Render one drum runtime instance into a mono, PRE-VOLUME block.
	 *
	 * Inputs: DrumVoice pointer, destination buffer, and block size. Output:
	 * buf receives oscillators, transient, filter, amp EG, velocity, and
	 * distortion, but NOT the channel volume (voice->vol). Mixer selects this
	 * object by current runtime type rather than a hardcoded drum index.
	 *
	 * Why pre-volume (Session 072, Effects Phase 5 step 2): the mixer applies
	 * voice->vol after decimation, combined with the slider gain ramp, so that
	 * the FX send can tap the same decimated signal before volume (user rules
	 * A24/A25). voice->vol remains the retained runtime value written by the
	 * descriptor/LFO/Morph/automation paths; it is read by
	 * instrumentManager_runtimeVolume(). Affiliates: mixer.c
	 * mixer_calcNextSampleBlock(), InstrumentManager.c.
	 */
```

## 3. `DrumVoice.c` — lines 343–344 (remove)

Before:

```c
	//channel volume
	bufferTool_addGain(buf,voice->vol,size);
}
```

After:

```c
	/*
	 * Channel volume is intentionally not applied here (Session 072 step 2).
	 * The mixer multiplies the decimated block by voice->vol x slider gain;
	 * see instrumentManager_runtimeVolume() and mixer_calcNextSampleBlock().
	 * Re-adding a gain stage here would apply volume twice.
	 */
}
```

## 4. `DrumVoice.h` — line 65 (modify)

Before: `	float	 	vol;		// volume of the voice`

After:

```c
	/*
	 * Channel volume 0..1 (descriptor instrument_vol / 127).
	 * Written by descriptor apply, LFO, Morph, and step automation; read only
	 * by instrumentManager_runtimeVolume() for the mixer, which applies it
	 * after decimation. The render function does not apply it (S072 step 2).
	 */
	float	 	vol;
```

---

## 5. `Snare.c` — lines 207–213 (modify the in-function contract comment)

Before:

```c
	/*
	 * Render one snare instance into a mono block.
	 *
	 * Inputs: SnareVoice pointer, destination buffer, and block size. Output:
 * buf receives noise, oscillator, transient, envelope, and distortion for
 * that tagged instance without relying on a fixed snare wrapper.
	 */
```

After:

```c
	/*
	 * Render one snare instance into a mono, PRE-VOLUME block.
	 *
	 * Inputs: SnareVoice pointer, destination buffer, and block size. Output:
	 * buf receives noise, oscillator, transient, amp envelope, optional
	 * velocity, and distortion for that tagged instance, but NOT the channel
	 * volume (voice->vol).
	 *
	 * Why (Session 072, Effects Phase 5 step 2; volume-order bug fix D1):
	 * volume used to multiply the signal before calcDistBlock(), so it also
	 * acted as a hidden drive control (a bug; volume must be the last stage).
	 * It is now a pure output level applied by the mixer after decimation,
	 * matching Drum, so the FX send can tap a pre-volume signal that includes
	 * this voice's distortion. Affiliates:
	 * instrumentManager_runtimeVolume(), mixer_calcNextSampleBlock().
	 */
```

## 6. `Snare.c` — lines 238 and 248 (modify)

- Line 238, before: `			buf[j] *=  voice->velo * voice->vol * voice->egValueOscVol;`
- Line 238, after: `			buf[j] *=  voice->velo * voice->egValueOscVol;`
- Line 248, before: `			buf[j] *=  voice->vol * voice->egValueOscVol;`
- Line 248, after: `			buf[j] *=  voice->egValueOscVol;`

Add one comment line immediately above the `if(voice->volumeMod)` at line
231:

```c
	/* Amp EG (and velocity) only; channel volume is applied by the mixer. */
```

## 7. `Snare.h` — line 61 (modify)

Same replacement as §4, with "the render function" referring to
`Snare_calcSyncBlockVoice()`.

---

## 8. `CymbalVoice.c` — lines 215–220 (modify the in-function contract comment)

Before:

```c
	/*
	 * Render one cymbal instance into a mono block.
	 *
	 * Inputs: CymbalVoice pointer, destination buffer, and block size. Output:
 * buf receives the FM cymbal, transient, envelope, and distortion output for
 * that tagged instance. InstrumentManager selects it through the runtime tag.
	 */
```

After:

```c
	/*
	 * Render one cymbal instance into a mono, PRE-VOLUME block.
	 *
	 * Inputs: CymbalVoice pointer, destination buffer, and block size. Output:
	 * buf receives the FM cymbal, transient, amp envelope, optional velocity,
	 * and distortion for that tagged instance, but NOT the channel volume
	 * (voice->vol). InstrumentManager selects it through the runtime tag.
	 *
	 * Why (Session 072, Effects Phase 5 step 2; volume-order bug fix D1):
	 * volume is the last stage, a pure output level applied by the mixer
	 * after decimation. It previously multiplied the signal before
	 * calcDistBlock() and so also set the drive (a bug). The FX send taps the
	 * pre-volume signal including distortion. Affiliates:
	 * instrumentManager_runtimeVolume(), mixer_calcNextSampleBlock().
	 */
```

## 9. `CymbalVoice.c` — lines 251 and 260 (modify)

- Line 251: `voice->velo * voice->vol * voice->egValueOscVol` →
  `voice->velo * voice->egValueOscVol`
- Line 260: `voice->vol * voice->egValueOscVol` → `voice->egValueOscVol`

Add the same one-line comment above `if(voice->volumeMod)` (line 245) as in §6.

## 10. `CymbalVoice.h` — line 61 (modify)

Same replacement as §4 (render function `Cymbal_calcSyncBlockVoice()`).

---

## 11. `HiHat.c` — lines 234–240 (modify the in-function contract comment)

Before:

```c
	/*
	 * Render one hihat instance into a mono block.
	 *
	 * Inputs: HiHatVoice pointer, destination buffer, and block size. Output:
 * buf receives the hihat FM, transient, envelope, and distortion output for
 * that tagged instance selected by InstrumentManager.
	 */
```

After:

```c
	/*
	 * Render one hihat instance into a mono, PRE-VOLUME block.
	 *
	 * Inputs: HiHatVoice pointer, destination buffer, and block size. Output:
	 * buf receives the hihat FM, transient, amp envelope (open or closed decay
	 * as triggered), optional velocity, and distortion for that tagged
	 * instance, but NOT the channel volume (voice->vol).
	 *
	 * Why (Session 072, Effects Phase 5 step 2; volume-order bug fix D1):
	 * volume is the last stage, a pure output level applied by the mixer
	 * after decimation. It previously multiplied the signal before
	 * calcDistBlock() and so also set the drive (a bug). The FX send taps the
	 * pre-volume signal including distortion. Track 7 (the shared slot-6
	 * voice) uses this same instance and volume. Affiliates:
	 * instrumentManager_runtimeVolume(), mixer_calcNextSampleBlock().
	 */
```

## 12. `HiHat.c` — lines 268 and 277 (modify)

- Line 268: `voice->velo * voice->vol * voice->egValueOscVol` →
  `voice->velo * voice->egValueOscVol`
- Line 277: `voice->vol * voice->egValueOscVol` → `voice->egValueOscVol`

Add the same one-line comment above `if(voice->volumeMod)` (line 262) as in §6.

## 13. `HiHat.h` — line 64 (modify)

Same replacement as §4 (render function `HiHat_calcSyncBlockVoice()`).

---

## 14. `InstrumentManager.h` — insert after line 489 (`uint8_t instrumentManager_runtimePan(uint8_t slot);`)

```c
/*
 * Read the current slot instrument's channel volume for the mixer.
 *
 * Inputs: zero-based render slot 0..5. Output: the tagged runtime member's
 * vol field (0..1, descriptor instrument_vol / 127, including any live LFO,
 * Morph, or step-automation value already written to it), or 0.0f for an
 * unknown/empty slot type, which renders silence anyway.
 *
 * Why (Session 072, Effects Phase 5 step 2): the voice engines no longer
 * apply volume; the mixer applies it after decimation so that the FX send can
 * tap the decimated, pre-volume signal. Keeping the type switch here, beside
 * instrumentManager_runtimePan(), preserves InstrumentManager as the only
 * module that knows which engine struct occupies a slot.
 *
 * Clients: mixer.c mixer_init() and mixer_calcNextSampleBlock() (once per
 * slot per 32-frame block). Foreground render context only. Affiliates:
 * DrumVoice/SnareVoice/CymbalVoice/HiHatVoice vol members.
 */
float instrumentManager_runtimeVolume(uint8_t slot);
```

## 15. `InstrumentManager.c` — insert after line 1502 (closing `}` of `instrumentManager_runtimePan()`)

```c

float instrumentManager_runtimeVolume(uint8_t slot)
{
    /*
     * Return the tagged runtime channel volume for one render slot.
     *
     * Mirrors instrumentManager_runtimePan(): resolve the slot's current
     * runtime type, borrow that engine member, and read its vol field. An
     * unknown type or missing instance returns 0.0f; such a slot renders
     * silence in instrumentManager_calcSlotSyncBlock(), so no audible path
     * depends on the fallback. Contract and clients: InstrumentManager.h.
     */
    switch (instrumentManager_slotType(slot)) {
    case INSTRUMENT_TYPE_DRM: {
        DrumVoice *voice = instrumentManager_drumRuntime(slot);
        return voice ? voice->vol : 0.0f; }
    case INSTRUMENT_TYPE_SNR: {
        SnareVoice *voice = instrumentManager_snareRuntime(slot);
        return voice ? voice->vol : 0.0f; }
    case INSTRUMENT_TYPE_CYM: {
        CymbalVoice *voice = instrumentManager_cymbalRuntime(slot);
        return voice ? voice->vol : 0.0f; }
    case INSTRUMENT_TYPE_HAT: {
        HiHatVoice *voice = instrumentManager_hihatRuntime(slot);
        return voice ? voice->vol : 0.0f; }
    default:
        return 0.0f;
    }
}
```

## 16. `InstrumentManager.c` — lines 1569–1576 (modify the `calcSlotSyncBlock` comment)

Before:

```c
    /*
     * Render the current slot instrument into one mono audio block.
     *
     * Inputs: zero-based render slot, output buffer, and block size. Output:
     * the selected engine writes a mono voice block, or silence for unknown
     * slot/type. Mixer remains responsible for decimation, pan, routing, and
     * slider interpolation after this call.
     */
```

After:

```c
    /*
     * Render the current slot instrument into one mono, pre-volume block.
     *
     * Inputs: zero-based render slot, output buffer, and block size. Output:
     * the selected engine writes a mono voice block WITHOUT channel volume, or
     * silence for unknown slot/type. Mixer remains responsible for decimation,
     * channel volume (instrumentManager_runtimeVolume(), Session 072 step 2),
     * pan, routing, and slider interpolation after this call. The pre-volume
     * block is also the FX-send tap point from Phase 5 step 5 onward.
     */
```

---

## 17. `mixer.c` — line 64 (modify: rename and document)

Before: `INCCMZ float mixer_slider_last_gain[6];`

After:

```c
/*
 * Last applied per-slot output gain = slider_vol[slot] x voice volume.
 *
 * What: the gain used at the end of the previous 32-frame block, one float
 * per render slot (24 B DTCM, unchanged allocation; renamed from
 * mixer_slider_last_gain in Session 072 step 2 because it now also carries
 * the channel volume). Why: mixer_addVoiceInt16ToOutput() ramps linearly from
 * this value to the current combined gain across the block, so slider AND
 * volume changes (knob, LFO, Morph, automation) are click-free.
 * Inputs: mixer_init() seeds it; mixer_calcNextSampleBlock() updates it
 * after each slot. Affiliates: adcPots.c slider_vol[],
 * instrumentManager_runtimeVolume().
 */
INCCMZ float mixer_voice_last_gain[6];
```

## 18. `mixer.c` — line 86 (modify, in `mixer_init()`)

- Before: `		mixer_slider_last_gain[i]   = slider_vol[i];`
- After: `		mixer_voice_last_gain[i]    = slider_vol[i] * instrumentManager_runtimeVolume(i);`

Add a comment line above it:

```c
		/* Seed the ramp at the combined gain so the first block does not fade
		** in; requires instrumentManager_runtimeInit() first (dsp_init order). */
```

`main.c` `dsp_init()` already calls `instrumentManager_runtimeInit()` before
`mixer_init()`, so the order is satisfied.

## 19. `mixer.c` — lines 365–371 (modify the fused-loop comment in `mixer_addVoiceInt16ToOutput()`)

Before:

```c
	/* Session 023 fused three formerly separate per-voice passes:
	**   1. interpolate slider_vol from the previous 32-frame block,
	**   2. convert legacy int16 voice output into signed-24 sample_mx_t,
	**   3. pan/route/add to the four output buses.
```

After:

```c
	/* Session 023 fused three formerly separate per-voice passes:
	**   1. interpolate the output gain from the previous 32-frame block
	**      (slider_vol x channel volume since Session 072 step 2; `gain` and
	**      `lastGain` are that combined value),
	**   2. convert legacy int16 voice output into signed-24 sample_mx_t,
	**   3. pan/route/add to the four output buses.
```

The rest of the comment and the loop body are unchanged. The function already
multiplies each sample by the ramped gain, so passing the combined gain adds
no per-sample work.

## 20. `mixer.c` — lines 547–567 (modify the per-slot render loop)

Before (lines 555–567):

```c
	for(uint8_t slot=0u; slot<6u; slot++)
	{
		uint8_t pan;
		instrumentManager_calcSlotSyncBlock(slot, sampleData, OUTPUT_DMA_SIZE);
		mixer_decimateBlock(slot,sampleData);
		pan = instrumentManager_runtimePan(slot);
		mixer_addVoiceInt16ToOutput(effectiveRouting[slot],
				squareRootLut[127-pan], squareRootLut[pan],
				sampleData, slider_vol[slot], mixer_slider_last_gain[slot],
				&output[pos],&output[pos+1],&output2[pos],&output2[pos+1]);
		mixer_slider_last_gain[slot] = slider_vol[slot];
	}
```

After:

```c
	for(uint8_t slot=0u; slot<6u; slot++)
	{
		uint8_t pan;
		float voiceGain;
		instrumentManager_calcSlotSyncBlock(slot, sampleData, OUTPUT_DMA_SIZE);
		mixer_decimateBlock(slot,sampleData);
		/*
		 * sampleData is now the decimated, PRE-VOLUME voice block. Step 5 taps
		 * the FX send here. Channel volume is applied only through the combined
		 * output gain below (Session 072 step 2).
		 */
		voiceGain = slider_vol[slot] * instrumentManager_runtimeVolume(slot);
		pan = instrumentManager_runtimePan(slot);
		mixer_addVoiceInt16ToOutput(effectiveRouting[slot],
				squareRootLut[127-pan], squareRootLut[pan],
				sampleData, voiceGain, mixer_voice_last_gain[slot],
				&output[pos],&output[pos+1],&output2[pos],&output2[pos+1]);
		mixer_voice_last_gain[slot] = voiceGain;
	}
```

In the render-loop comment above (lines 548–554), extend the sentence "mixer
state chooses routing, decimation, pan tables, and slider interpolation" to
"…decimation, channel volume, pan tables, and the combined slider × volume
gain ramp".

---

## 21. `MODULE_INTERCHANGE_SPEC.md` — line 680 (modify)

- Before: `| instrumentManager_runtimeInstance() / trigger/filter/async/sync/pan/LFO dispatch family | … | mixer, … |`
- After: add `volume` to the family name, and add this sentence: "Engines
  render pre-volume; `instrumentManager_runtimeVolume()` supplies the channel
  volume that the mixer applies after decimation (Session 072 step 2)."

## 22. `MEMORY.md` — Volatile Notes (add)

```
- S072 Step 2: voice engines render pre-volume; the mixer applies
  slider x instrumentManager_runtimeVolume() after decimation. Fixes the
  Snare/Cymbal/HiHat bug where `vol` was applied before distortion (D1).
```

---

## 23. Build and verification gates

**Build:**

1. `make all` succeeds with no new warnings.
2. `link_budget.py` shows flash within ±200 B of the Step 1 figure (458,528 B);
   an LTO shift of that size is expected. DTCM statics are unchanged
   (4,084 B), and the arena is unchanged (126,976 B).
3. `grep -n "voice->vol" Core/DSP/Instruments/*/*.c` shows only the four
   `= 0.8f` init lines and no render-path uses.
   `grep -n mixer_slider_last_gain -r Core` finds nothing.

**Hardware, production build.** Compare against the Step 1 image, on the same
card and Scene:

4. **Level identity, drive 0.** For each of Drum, Snare, Cymbal, and HiHat,
   with `drv = 0`, test `vol` at 127, 90, and 40 against the Step 1 image.
   There should be no audible level or timbre difference, and the `cpu`
   widget should read equal or lower.
5. **Bug-fix check (D1).** Snare, Cymbal, and HiHat with `drv` around 80:
   sweep `vol` from 127 down to 20.
   - The saturation character must stay constant while only the level falls.
   - In the Step 1 image, the drive audibly thins as `vol` drops.
   - Drum under the same settings must be identical to Step 1.
6. **Modulation paths:**
   - LFO targeting `vol` (voice-local and cross-voice): audible and smooth,
     with no block-rate stepping.
   - Velocity `vel on`: unchanged.
   - Pattern automation of `vol`: applies, and restores on the next trigger.
   - Morph between two `vol` endpoints: unchanged.
7. **Transitions:** Scene switch, Instrument Load, and Kit Load while playing,
   between kits with different `vol`. There should be no new clicks; volume
   changes now ramp across one block.
8. **Track 7 / slot 6:** HiHat open and closed triggers both follow slot-6
   volume. A non-Choke slot-6 instrument's track 7 does too.
9. **Decimation:** set a low `srt` with an LFO on `vol` and listen for
   artefacts. Expect none beyond the existing decimation character.
10. **Sliders:** full range on each voice; at minimum they are silent, as
    before.

**Rollback:** revert the commit. There is no data, file, or RAM format change.

---

## 24. Doc and plan follow-through

- `EFFECTS_BUS_FEATURE_PLAN.md` §8.1: add one line saying that D1 was
  resolved as a bug fix, with volume last on every engine (2026-09-27).
- `SCOPING_TARGETS.md`: no entry needed; the bug is fixed in this step.
- No `SRAM_MANIFEST.md` change: a renamed symbol of the same size. Update
  only if the manifest lists DTCM statics by name; it currently does not list
  `mixer_slider_last_gain`.

---

## 25. Working implementation notes

### 25.1 Source changes recorded

- The four engine renderers now document and produce mono pre-volume blocks.
  Drum no longer calls its final `bufferTool_addGain()` with `voice->vol`;
  Snare, Cymbal, and HiHat no longer include `voice->vol` in their envelope
  multiply before `calcDistBlock()`.
- The retained `vol` field remains in every engine header. Its adjacent
  contract states that descriptor, LFO, Morph, and step-automation writers
  continue to own the value while the mixer is the sole audio consumer.
- `instrumentManager_runtimeVolume()` was added beside the existing dynamic
  pan dispatch. It reads the current tagged runtime member and returns zero
  for unknown/empty slots.
- `mixer_slider_last_gain` was renamed to `mixer_voice_last_gain`; the same
  six-float DTCM allocation now stores the previous block's combined
  `slider_vol × instrumentManager_runtimeVolume()` gain. The mixer computes
  this combined gain after decimation and ramps it through the existing fused
  output loop.
- The Step 5 FX-send tap location is now explicitly documented immediately
  after decimation and before the combined output gain, although no FX bus is
  enabled by this step.

### 25.2 Allocation and ownership confirmation

No new RAM was requested or allocated. The changed state is the existing
six-element `float` ramp array: 6 × 4 = 24 bytes in DTCM, foreground/audio
render lifetime, owned by `mixer.c`. The existing `slider_vol[]` remains the
ADC-owned input; the runtime `vol` fields remain InstrumentManager/engine
runtime state. This preserves the project reservation policy.

### 25.3 Verification recorded

Clean production verification passed on 2026-09-27:

- `make clean && make` completed successfully; no new warnings were emitted
  from the Step 2 files. Existing warnings remain in AsyncFATFS, USB packed
  attributes, linker syscall stubs, and the LTO serial-compilation note.
- `arm-none-eabi-size build/lxr02.elf`: `text=458208`, `data=416`,
  `bss=419100`, `dec=877724`.
- Link-budget report: Flash `458,624 / 491,520 B`, headroom `32,896 B`;
  ITCM `3,768 B`; DTCM statics `4,084 B`; FXBUF `126,976 B` at
  `0x20001000`, with `4,096 B` above the approved minimum.
- `mixer_voice_last_gain` is six floats (24 B) at `0x20000f50`; the prior
  `mixer_slider_last_gain` symbol is absent. The four renderers contain no
  executable channel-volume multiply before distortion.
- `make img` completed and wrote `build/LXRV2_lxr02.img` at 458,624 bytes.
- `git diff --check` passed.

Hardware level identity, drive-order, modulation, transition, Track 7,
decimation, and slider listening gates require the device and remain
explicitly pending until a hardware run is available.

---

## 26. Review assessment (2026-09-27, post-implementation)

**Verdict:** accepted. The Step 2 source matches this schedule (§2–§22)
exactly. The build reproduces §25.3, and the user reports that the hardware
run checks out with no major problems. Step 2 is closed.

### 26.1 What was checked

- **Diff against the Step 1 working tree**, per scheduled file:
  - All four engines match. Drum's final `bufferTool_addGain(buf,voice->vol,size)`
    is removed and replaced by the §3 comment. Snare, Cymbal, and HiHat
    multiply `velo × EG` or `EG` only, with the §6 one-line comment above each
    `if(voice->volumeMod)`.
  - The four `vol` member comments match §4.
  - `instrumentManager_runtimeVolume()` is declared and defined as in
    §14–§15, beside `instrumentManager_runtimePan()`. The `calcSlotSyncBlock`
    contract matches §16.
  - `mixer.c` matches §17–§20: the rename, the seed in `mixer_init()`, the
    fused-loop comment, and the per-slot `voiceGain` loop with the pre-volume
    tap comment.
- **Static gates** (§23 items 1–3):
  - `make clean && make all` succeeds (see the note in §26.3).
  - `grep voice->vol` finds only the four `= 0.8f` inits and comment text; no
    render-path use remains.
  - `mixer_slider_last_gain` appears only in the rename comment.
  - `mixer_voice_last_gain` is 24 B at `0x20000F50`.
  - `instrumentManager_runtimeVolume` is 68 B of text.
- **Link budget**, on an independent clean rebuild: `text=458,208`,
  `data=416`, `bss=419,100`. Flash is 458,624 B (32,896 B headroom), **+96 B
  over Step 1**, within the ±200 B gate. DTCM statics 4,084 B and the FX
  arena 126,976 B are both unchanged.
- **Hardware** (§23 items 4–10), user-reported 2026-09-27: "step 2 firmware
  checks out on the hardware; no major problems observed". The D1
  volume-order fix is therefore in service.

### 26.2 Allocation

No RAM change, as scheduled: the same 24-byte DTCM array was renamed.

### 26.3 Notes and still-open items

1. **Step 1 follow-ups are still unapplied** (from ST1 §26.3). Both are in
   Step 1 files, so neither affects Step 2's verdict:
   - the `fxbuf_init()` order: `fxbuf_handoffResetAll()` still runs after the
     self-test and forced-unit loop (`FxBuffer.c:237`), so forced dev units
     carry a handoff rate of 0;
   - the echoed Makefile recipe comments (use `@#`).
2. **Commit before Step 3.** Steps 1 and 2 are both still uncommitted on
   `dev-ph5-effects`. Step 3 changes the AutoSave wire geometry and the
   `scene_t` layout. Committing Steps 1 and 2 first, as separate commits,
   keeps each step independently revertible and bisectable, which was the
   premise of splitting them.
3. **Transient empty ELF.** During review, one incremental parallel
   `make -j8 all` reported "`build/lxr02.elf` is empty" at the objcopy
   stage. It did not recur on a clean rebuild; the likely cause is a
   concurrent build touching the same `build/` directory. No source issue.
4. The pre-existing bare-`make` default-goal issue (ST1 §26.3 item 3) remains
   open for your decision.
