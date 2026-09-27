# S072 Step 5 — Implementation Schedule: FX Bus, Sends, Fader Modes, Return

**Scope:** `EFFECTS_BUS_FEATURE_PLAN.md` §8.2–§8.4, §16 item 10, and §17.1
Step 5.

**This step adds:**

- the per-voice FX send, tapped post-decimation and pre-volume (A24/A25);
- the three fader modes `pre`, `pst`, and `fx` (A24, §8.3);
- a mono or stereo FX bus, chosen by the live type's input flag, summed with
  the mixer's saturating add (A24/A27);
- in-place float conversion, `effects_process()`, and the routed, level- and
  pan/balance-controlled return with jack fallback (§8.4).

**What becomes audible:**

- the stored per-voice `fx_send_amount[6]` and `fader_setting[6]`;
- the FX_SEND step automation overlay (targets 398..403), which the mixer now
  reads every block, so there is no longer a no-op apply path;
- the StereoFilter selected by the Step 4 dev hook.

**Not in this step:**

- files (Step 6);
- UI (Step 7);
- sequencer (Step 8);
- Effect automation and LFO (Step 9);
- fan-out (Step 10).

Effect parameters other than out/vol/pan stay at their type defaults until
Step 7. With `DEV_EFFECT_FORCE_TYPE 1u` that means `flt` at cutoff 64, LP.

**Status:** implemented and clean-linked. Production and diagnostic builds pass;
hardware listening gates remain for the bench.

**Line numbers** refer to the Step 4 tree (commit `1e47c56`).

---

## 0. Decisions and notes

### D1 — Return balance law for stereo-output Effects (confirm)

The plan says "pan (mono out) / balance (stereo out)" (§8.4) but does not
define the balance curve.

- **Proposal (stereo-output types):** a **linear balance with unity at
  centre**:
  - `gL = (pan <= 64) ? 1 : (127 - pan) / 63`
  - `gR = (pan >= 64) ? 1 : pan / 64`

  At `pan = 64` both channels pass at full level, so `vol = 127` returns the
  Effect at unity. Moving pan only attenuates the opposite side, which is the
  usual meaning of a stereo "balance" control.
- **Mono-output types** (none registered yet) use the voices'
  constant-power pan law (`squareRootLut[127-pan]`, `squareRootLut[pan]`).
  That law puts −3 dB on each side at centre. Applying it to a stereo return
  would make a centred `vol = 127` Effect 3 dB quieter than its bus, so it is
  not proposed for stereo returns.

### D2 — RAM: +32 B DTCM beyond the approved 256 B bus (acknowledgement requested)

The plan approved the 256 B FX bus (§16 item 10). Two small ramp memories are
also needed so that level changes stay click-free, like the voice ramp that
Step 2 made cover volume:

| Object | Region | Bytes | Why |
|---|---|---|---|
| `mixer_fx_bus` (union `sample_mx_t[2][32]` / `float[2][32]`) | DTCM `.dtcmz` | 256 | approved (§16 item 10) |
| `mixer_send_last_gain[6]` | DTCM `.dtcmz` | **24** | per-slot send gain ramp (send amount × send-side fader); send changes from knob, automation, or fader do not zipper |
| `mixer_fx_return_last_gain[2]` | DTCM `.dtcmz` | **8** | return L/R gain ramp (level × balance) |

- DTCM statics: 4,160 → **4,448 B**.
- The arena start moves `0x20001040` → `0x20001160`; the arena shrinks by
  288 B to **126,624 B**, still 3,744 B above the 120 KiB minimum.
- SRAM1 is unchanged.

### Notes (no decision needed)

1. **The mixer pulls the send values every block.** Each block it reads
   `preset_getEffectiveFxSendAmount()` (retained value or step override) and
   `scene_getVoiceFaderSetting()` for the active Scene. That is why FX_SEND
   automation becomes audible without a new apply path. The Preset setters
   stay "store only". Their comments are updated to name the mixer as the
   runtime consumer (§8).
2. **`off` costs nothing.** When the live type has no I/O flags (`off`), the
   bus is not cleared, accumulated, converted, processed, or returned. The
   send-gain ramp state is still updated each block, so enabling an Effect
   later does not start with a jump.
3. **The mono-input bus path is compiled but cannot be tested yet.** No
   registered type is mono-input. The path is symmetric with the stereo one
   and is exercised when the first mono type lands.
4. **Headroom.** Sends are pre-volume. Six loud voices at full send can sum
   above int16 full scale on the bus. The bus is int32 (`satAdd32`), the
   Effect sees float (1.0 = int16 full scale) and shapes overshoot with its
   own soft clipper (Step 4 float SVF), and the return clamps once before
   conversion. No clipping happens before the Effect.
5. **Scene switch.** The fader mode and send are read from the newly active
   Scene immediately, while voice images still apply under the deferred Scene
   worker. For at most a few blocks, the old voice runtime can therefore be
   sent with the new Scene's send/fader. This is the same transient class as
   the existing immediate audio-route switch; it is inaudible at normal
   settings.

---

## 1. Change index

| # | File | Line(s) | Op | Summary |
|---|---|---|---|---|
| 1 | `Core/DSPAudio/mixer.h` | after 73 | add | `MIXER_FADER_PRE/POST/FX` mode constants |
| 2 | `Core/DSPAudio/mixer.c` | after 60 | add | `#include "SceneData.h"`, `#include "presetManager.h"` |
| 3 | `Core/DSPAudio/mixer.c` | after 78 | add | FX bus union, send ramp, return ramp state |
| 4 | `Core/DSPAudio/mixer.c` | after 462 | add | `mixer_faderGains()`, `mixer_addVoiceToFxBus()`, `mixer_floatToMx()`, `mixer_addFxReturnToOutput()` |
| 5 | `Core/DSPAudio/mixer.c` | 555–556 | add after | FX activity, bus clear, return route snapshot |
| 6 | `Core/DSPAudio/mixer.c` | 585–603 | modify | Per-slot loop: fader-mode gains; send tap |
| 7 | `Core/DSPAudio/mixer.c` | after 603 | add | Bus → float → `effects_process()` → return |
| 8 | `Core/DSPAudio/mixer.c` | 574–584 | modify | Render-loop comment names the send tap and fader modes |
| 9 | `Core/DSP/Effects/EffectsManager.h` | 98–104 | modify | `effect_io_t` contract: `r` buffer for stereo in or out |
| 10 | `Core/Bank/Scene/Preset/presetManager.c` | 1115–1122, 1135–1141, 1150–1156, 1186–1193, 1205–1211 | modify | Comments: mixer is the runtime consumer |
| 11 | `Core/Sequencer/sequencer.c` | 266–268, 290–292, 795–796, 844 | modify | Comments: FX_SEND overlay is audible |
| 12 | `Core/Menu/menu.c` | 3145–3147 | modify | Fader-label comment: labels are live behavior |
| 13 | Docs | — | modify | `BANK_PRESET_ARCHITECTURE`, `MODULE_INTERCHANGE_SPEC`, `SRAM_MANIFEST`, plan §8, `MEMORY` |

No change to `SceneData`, `Autosave`, storage, `EffectsManager.c`, or any
voice engine.

---

## 2. `mixer.h` — after line 73 (end of the routing enum)

```c
/*
 * Per-voice fader modes (Scene setting fader_setting[slot], 0..2; plan §8.3,
 * user rule A24). Values match the stored byte and the Menu labels
 * (menu_sceneSettingFaderName: pre / pst / fx).
 *
 * The voice signal s (decimated, pre-volume) feeds two parallel taps:
 *   mix  = s x vol x F_mix  -> pan -> voice route
 *   send = s x send x F_send -> FX bus
 * PRE : F_mix = fader, F_send = fader  (fader scales both)
 * POST: F_mix = fader, F_send = 1      (send ignores the fader)
 * FX  : F_mix = 1,     F_send = fader  (fader scales only the send; mix level
 *                                       is vol alone)
 * The fader never changes the stored FX Send parameter. Consumer:
 * mixer_calcNextSampleBlock(). Affiliates: SceneData fader_setting,
 * presetManager fader/send setters, Menu VOICE mix cells.
 */
#define MIXER_FADER_PRE   0u
#define MIXER_FADER_POST  1u
#define MIXER_FADER_FX    2u
```

---

## 3. `mixer.c` — includes, after line 60 (`#include "adcPots.h"`)

```c
#include "SceneData.h"      /* active Scene, fader_setting */
#include "presetManager.h"  /* preset_getEffectiveFxSendAmount() */
```

Both are read-only getters called once per slot per 32-frame block.

---

## 4. `mixer.c` — state, after line 78 (`INCCMZ float mixer_voice_last_gain[6];`)

```c
/*
 * FX send bus, one render block (Session 072 step 5; plan §8.2, §16 item 10).
 *
 * What: two channels x OUTPUT_DMA_SIZE samples, used in two phases within one
 * block. (1) Accumulate: voice sends are saturating-summed as sample_mx_t
 * (int16<<8, the mixer's own domain; user rule A24). (2) Process: the same
 * memory is reinterpreted in place as float (1.0 = int16 full scale) for
 * effects_process(), then read back for the return. The union makes the
 * in-place reuse explicit: no second 256-byte buffer (plan §16).
 * Channel 1 is used for stereo-input types, and for the right output of
 * stereo-output types.
 * Region: DTCM (hot every sample, never DMA-visible). Lifetime: one block;
 * contents are meaningless between blocks. Owner: mixer_calcNextSampleBlock()
 * only.
 */
typedef union {
	sample_mx_t mx[2][OUTPUT_DMA_SIZE];
	float       f[2][OUTPUT_DMA_SIZE];
} mixer_fx_bus_t;

static INDTCMZ mixer_fx_bus_t mixer_fx_bus;

/*
 * Last applied per-slot send gain = (fx_send / 127) x F_send (fader mode).
 *
 * What: the send gain at the end of the previous block, one float per slot
 * (24 B DTCM). Why: the send tap ramps from this value to the current gain
 * across the block, like mixer_voice_last_gain for the dry path, so send
 * edits, FX_SEND automation, and fader moves are click-free. Updated every
 * block even while the Effect is `off`, so enabling an Effect starts from the
 * true current gain. Owner: mixer_calcNextSampleBlock().
 */
static INDTCMZ float mixer_send_last_gain[6];

/*
 * Last applied Effect return gains, [0] left and [1] right = level x
 * pan/balance (8 B DTCM). Ramped across each block by
 * mixer_addFxReturnToOutput() so effect_level and effect_pan changes
 * (menu, Morph, and later sequencer/automation/LFO) are click-free. Owner:
 * mixer_calcNextSampleBlock().
 */
static INDTCMZ float mixer_fx_return_last_gain[2];
```

**`mixer_init()` is unchanged.**

- `INDTCMZ` (defined at `config.h:776`, which `mixer.c` already includes) is
  zeroed at startup, so both ramps start at 0.
- The first live block therefore fades the send and return in from silence.
  That is the wanted power-up behavior, unlike `mixer_voice_last_gain`, which
  `mixer_init` seeds so the dry voices do not fade in.

---

## 5. `mixer.c` — helpers, after line 462 (end of `mixer_addVoiceInt16ToOutput()`)

```c
//-----------------------------------------------------------------------
/*
 * Resolve one slot's dry (mix) and send gains for this block.
 *
 * Inputs: slot 0..5 and the active Scene. Reads the slider (slider_vol), the
 * channel volume (instrumentManager_runtimeVolume), the effective FX send
 * (preset_getEffectiveFxSendAmount: step overlay or retained, 0..127), and
 * the fader mode (scene_getVoiceFaderSetting). Outputs: *mix_gain multiplies
 * the pre-volume block on the dry path; *send_gain on the send tap.
 * Implements the A24 table in mixer.h (MIXER_FADER_*); unknown modes behave
 * as PRE, the default. Why a helper: the mode table is the single place that
 * defines fader topology, keeping mixer_calcNextSampleBlock() readable.
 */
static void mixer_faderGains(uint8_t slot, uint8_t scene_index,
		float *mix_gain, float *send_gain)
{
	const float fader = slider_vol[slot];
	const float vol   = instrumentManager_runtimeVolume(slot);
	const float send  = preset_getEffectiveFxSendAmount(scene_index, slot) / 127.0f;

	switch (scene_getVoiceFaderSetting(scene_index, slot)) {
	case MIXER_FADER_POST:
		*mix_gain  = vol * fader;
		*send_gain = send;
		break;
	case MIXER_FADER_FX:
		*mix_gain  = vol;
		*send_gain = send * fader;
		break;
	case MIXER_FADER_PRE:
	default:
		*mix_gain  = vol * fader;
		*send_gain = send * fader;
		break;
	}
}

/*
 * Add one voice's send to the FX bus (accumulate phase).
 *
 * Inputs: decimated pre-volume int16 block, current/previous send gain (ramped
 * linearly across the block), the voice's constant-power pan gains, and the
 * bus width (stereo: pan applied, user rule A25; mono: no pan). Output:
 * saturating sums into mixer_fx_bus.mx (sample_mx_t = int16<<8), the same
 * summing as the dry mixer (A24). The float product is converted directly to
 * sample_mx_t (x256) without the dry path's int16 truncation, which is
 * slightly more precise on the bus. Skipped entirely by the caller when both
 * gains are zero (the common "no send" case).
 */
static void mixer_addVoiceToFxBus(const int16_t *data, const float gain,
		const float lastGain, const float panL, const float panR,
		const uint8_t stereo)
{
	uint8_t i;
	const float inv_size = 1.f / (OUTPUT_DMA_SIZE - 1.f);
	const float gain_delta = gain - lastGain;

	for (i = 0; i < OUTPUT_DMA_SIZE; i++)
	{
		const float g = lastGain + ((float)i * inv_size * gain_delta);
		const float v = (float)data[i] * g * 256.0f;

		if (stereo) {
			mixer_fx_bus.mx[0][i] = bufferTool_satAdd32(mixer_fx_bus.mx[0][i],
					(sample_mx_t)(v * panL));
			mixer_fx_bus.mx[1][i] = bufferTool_satAdd32(mixer_fx_bus.mx[1][i],
					(sample_mx_t)(v * panR));
		} else {
			mixer_fx_bus.mx[0][i] = bufferTool_satAdd32(mixer_fx_bus.mx[0][i],
					(sample_mx_t)v);
		}
	}
}

/*
 * Convert one normalized float return sample (1.0 = int16 full scale) to
 * sample_mx_t with a safe clamp.
 *
 * Why the clamp: float-to-int conversion out of range is undefined. +/-255
 * int16-full-scales x 8388352 (32767<<8) stays inside int32. The codec pack
 * (sampleMix_toS24) saturates to 24 bits later, as for voices.
 */
static inline sample_mx_t mixer_floatToMx(float x)
{
	if (x > 255.0f)
		x = 255.0f;
	else if (x < -255.0f)
		x = -255.0f;
	return (sample_mx_t)(x * 8388352.0f);
}

/*
 * Add the processed Effect return to the output buses (return phase).
 *
 * Inputs: jack-resolved route (mixer_checkOutJackAvailable, same routing
 * table as voices), processed float channels (r NULL for a mono-output
 * type), target L/R gains for this block (level x balance or pan; see
 * S072_ST5 §0 D1), and the output pointers in the mixer's interleaved
 * layout (DAC2 = output, DAC1 = output2, as mixer_addVoiceInt16ToOutput).
 * Gains ramp from mixer_fx_return_last_gain[]. Routing:
 *   stereo routes (DAC1/DAC2 STEREO): L -> left, R -> right;
 *   single-jack routes (L1/R1/L2/R2): (L + R) / 2 x the average of the two
 *     gains for stereo output types; the mono signal with the left-side gain
 *     for mono output types. This matches voices, whose single-jack routes
 *     ignore pan.
 * Output: saturating sums into the DAC buffers (A24).
 */
static void mixer_addFxReturnToOutput(uint8_t dest,
		const float *l, const float *r,
		const float gainL, const float gainR,
		sample_mx_t *outL, sample_mx_t *outR,
		sample_mx_t *outL2, sample_mx_t *outR2)
{
	uint8_t i;
	const float inv_size = 1.f / (OUTPUT_DMA_SIZE - 1.f);
	const float dL = gainL - mixer_fx_return_last_gain[0];
	const float dR = gainR - mixer_fx_return_last_gain[1];
	sample_mx_t *dstL = 0;
	sample_mx_t *dstR = 0;
	sample_mx_t *dstMono = 0;

	switch (dest) {
	case MIXER_ROUTING_DAC1_STEREO: dstL = outL2; dstR = outR2; break;
	case MIXER_ROUTING_DAC2_STEREO: dstL = outL;  dstR = outR;  break;
	case MIXER_ROUTING_DAC1_L:      dstMono = outL2; break;
	case MIXER_ROUTING_DAC1_R:      dstMono = outR2; break;
	case MIXER_ROUTING_DAC2_L:      dstMono = outL;  break;
	case MIXER_ROUTING_DAC2_R:      dstMono = outR;  break;
	default: return;
	}

	for (i = 0; i < OUTPUT_DMA_SIZE; i++)
	{
		const float t  = (float)i * inv_size;
		const float gL = mixer_fx_return_last_gain[0] + t * dL;
		const float gR = mixer_fx_return_last_gain[1] + t * dR;
		const float sL = l[i];
		const float sR = r ? r[i] : l[i];

		if (dstMono) {
			const float m = r ? 0.5f * (sL * gL + sR * gR) : sL * gL;
			*dstMono = bufferTool_satAdd32(*dstMono, mixer_floatToMx(m));
			dstMono += 2;
		} else {
			*dstL = bufferTool_satAdd32(*dstL, mixer_floatToMx(sL * gL));
			*dstR = bufferTool_satAdd32(*dstR, mixer_floatToMx(sR * gR));
			dstL += 2;
			dstR += 2;
		}
	}
	mixer_fx_return_last_gain[0] = gainL;
	mixer_fx_return_last_gain[1] = gainR;
}
```

**Note on the single-jack mono sum** for a stereo-output type:
`0.5 × (L·gL + R·gR)` keeps a centred source at the same level as one stereo
channel, and keeps the balance meaningful (panning hard left mutes R's
contribution).

---

## 6. `mixer.c` — `mixer_calcNextSampleBlock()`

### 6.1 After line 556 (`bufferTool_clearBuffer32(output2,OUTPUT_DMA_SIZE*2);`) — add

```c
	/*
	 * FX bus setup for this block (Session 072 step 5; plan §8.2-§8.4).
	 *
	 * fx_io: the live Effect's I/O flags (0 for `off`, which disables every
	 * bus stage below). fx_stereo_in selects a stereo send (panned) or a mono
	 * send (unpanned, user rule A25). The return route is snapshotted like
	 * the voices' effectiveRouting, once per block. The bus is cleared only
	 * when used.
	 */
	const uint8_t fx_io = effects_activeIoFlags();
	const uint8_t fx_stereo_in = (uint8_t)((fx_io & EFFECT_IO_STEREO_IN) != 0u);
	const uint8_t fx_active = (uint8_t)(fx_io != 0u);
	const uint8_t fx_scene = scene_getActiveIndex();
	const effects_common_runtime_t *fx_common = effects_commonRuntime();
	const uint8_t fx_route = mixer_checkOutJackAvailable(fx_common->route);

	if (fx_active)
		bufferTool_clearBuffer32(&mixer_fx_bus.mx[0][0], OUTPUT_DMA_SIZE * 2);
```

### 6.2 Lines 585–603 — per-slot loop (modify)

Before:

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

After:

```c
	for(uint8_t slot=0u; slot<6u; slot++)
	{
		uint8_t pan;
		float voiceGain;
		float sendGain;
		instrumentManager_calcSlotSyncBlock(slot, sampleData, OUTPUT_DMA_SIZE);
		mixer_decimateBlock(slot,sampleData);
		/*
		 * sampleData is the decimated, PRE-VOLUME voice block: the common
		 * source of the two parallel taps (A24). The fader mode decides where
		 * the slider applies (mixer_faderGains, mixer.h MIXER_FADER_*).
		 */
		mixer_faderGains(slot, fx_scene, &voiceGain, &sendGain);
		pan = instrumentManager_runtimePan(slot);
		/*
		 * Send tap (Session 072 step 5). Skipped when no Effect is live or the
		 * send is silent across the whole ramp. The ramp state always
		 * advances, so re-enabling starts from the true current gain.
		 */
		if (fx_active &&
			(sendGain > 0.0f || mixer_send_last_gain[slot] > 0.0f))
			mixer_addVoiceToFxBus(sampleData, sendGain,
					mixer_send_last_gain[slot],
					squareRootLut[127-pan], squareRootLut[pan],
					fx_stereo_in);
		mixer_send_last_gain[slot] = sendGain;
		/* Dry tap: unchanged path, now with the fader-mode mix gain. */
		mixer_addVoiceInt16ToOutput(effectiveRouting[slot],
				squareRootLut[127-pan], squareRootLut[pan],
				sampleData, voiceGain, mixer_voice_last_gain[slot],
				&output[pos],&output[pos+1],&output2[pos],&output2[pos+1]);
		mixer_voice_last_gain[slot] = voiceGain;
	}
```

### 6.3 After line 603 (closing `}` of the slot loop), before the function's closing `}` — add

```c
	/*
	 * Process and return the FX bus (Session 072 step 5; plan §8.4).
	 *
	 * 1. Reinterpret the accumulated sample_mx_t bus as float in place
	 *    (x 1/8388352: 1.0 = int16 full scale).
	 * 2. effects_process() runs the live type (effects_service() already
	 *    resolved its parameters at the top of this block).
	 * 3. Add the return at effect_level with pan (mono output) or linear
	 *    balance (stereo output; S072_ST5 §0 D1) to the Effect's jack-resolved
	 *    route. The return is 100 % wet (A28).
	 * The right channel is passed when the type has stereo input OR stereo
	 * output (effect_io_t contract), so a mono-input/stereo-output type can
	 * write its right output there.
	 */
	if (fx_active)
	{
		const uint8_t fx_stereo_out =
			(uint8_t)((fx_io & EFFECT_IO_STEREO_OUT) != 0u);
		const uint8_t fx_channels_in = fx_stereo_in ? 2u : 1u;
		const float level = fx_common->level;
		const uint8_t fx_pan = fx_common->pan;
		float gainL;
		float gainR;
		fx_share_t share;
		effect_io_t io;
		uint8_t ch;
		uint8_t i;

		for (ch = 0; ch < fx_channels_in; ch++)
			for (i = 0; i < OUTPUT_DMA_SIZE; i++) {
				const sample_mx_t v = mixer_fx_bus.mx[ch][i];
				mixer_fx_bus.f[ch][i] = (float)v * (1.0f / 8388352.0f);
			}
		if (!fx_stereo_in && fx_stereo_out)
			for (i = 0; i < OUTPUT_DMA_SIZE; i++)
				mixer_fx_bus.f[1][i] = 0.0f;

		fxbuf_effectShare(&share);
		io.l = mixer_fx_bus.f[0];
		io.r = (fx_stereo_in || fx_stereo_out) ? mixer_fx_bus.f[1] : 0;
		io.frames = OUTPUT_DMA_SIZE;
		io.channels = fx_channels_in;
		io.share = &share;
		effects_process(&io);

		if (fx_stereo_out) {
			/* Linear balance, unity at centre (D1). */
			gainL = level * ((fx_pan <= 64u) ? 1.0f : (127u - fx_pan) / 63.0f);
			gainR = level * ((fx_pan >= 64u) ? 1.0f : fx_pan / 64.0f);
		} else {
			/* Constant-power pan, as voices. */
			gainL = level * squareRootLut[127u - fx_pan];
			gainR = level * squareRootLut[fx_pan];
		}
		mixer_addFxReturnToOutput(fx_route, mixer_fx_bus.f[0],
				fx_stereo_out ? mixer_fx_bus.f[1] : 0,
				gainL, gainR,
				&output[pos], &output[pos+1],
				&output2[pos], &output2[pos+1]);
	}
```

**Mono-output note:** for a mono-output type `r` is passed as NULL to the
return. On stereo routes the helper then plays `l` into both sides with the
pan gains. On single-jack routes it uses `l × gainL`. Mono single-jack voices
ignore pan; for the Effect return, a single-jack route with pan off-centre
simply uses the left-side gain. That is acceptable for the first mono type
and can be refined then.

### 6.4 Lines 574–584 — render-loop comment (modify)

After:

```c
	/*
	 * Render each storage slot with its loaded instrument type.
	 *
	 * Inputs: slot index chooses SceneData/instrument runtime through
	 * InstrumentManager; mixer state chooses routing, decimation, channel
	 * volume, pan tables, the fader mode, and the combined gain ramps. Output:
	 * each slot contributes one mono block to its routed stereo output pair
	 * (dry tap) and, when an Effect is live, a ramped send to the FX bus (send
	 * tap, Session 072 step 5). Both taps start from the same decimated,
	 * pre-volume block (user rules A24/A25).
	 */
```

---

## 7. `EffectsManager.h` — lines 98–104, `effect_io_t` comment (modify)

After:

```c
/*
 * One block passed to an Effect process operation.
 *
 * Samples are normalized floats where 1.0 is int16 full scale, processed in
 * place (the mixer's FX bus, Session 072 step 5). l always carries channel 0
 * (the mono or left input, and the mono or left output). r points at channel
 * 1 whenever the type has stereo input OR stereo output, and is NULL
 * otherwise. For a mono-input/stereo-output type, r arrives zeroed and the
 * type writes its right output there. channels is the INPUT channel count
 * (1 or 2). frames is the render-block length. share is the current FxBuffer
 * allocation for any buffer-using type.
 */
```

The struct is unchanged.

---

## 8. `presetManager.c` — comments only (modify)

- **`preset_setFxSendStepOverride()`, lines 1115–1122:**

```c
    /*
     * Store one transient FX-send amount (Session 072 step 5: audible).
     *
     * Inputs: zero-based voice slot and 0..127 amount. Output: runtime-only
     * overlay state; retained SceneData/AutoSave remain untouched. The mixer
     * reads it every block through preset_getEffectiveFxSendAmount(), so the
     * overlay is heard on the next block. Client: sequencer Scene-target
     * automation. Restore: preset_clearAllFxSendStepOverrides().
     */
```

- **`preset_clearAllFxSendStepOverrides()`, lines 1135–1141:**

```c
    /*
     * Clear every transient FX-send overlay.
     *
     * Inputs: none. Output: effective reads fall back to retained Scene
     * settings; the mixer's next block sends at the retained amount (its
     * ramp makes the change click-free). No separate DSP restore is needed.
     */
```

- **`preset_getEffectiveFxSendAmount()`, lines 1150–1156:**

```c
    /*
     * Read one voice's effective FX-send amount.
     *
     * Inputs: resident Scene index and zero-based voice slot. Output: the
     * active step overlay amount when present, otherwise retained SceneData.
     * Clients: the live Scene superpage display, and the mixer's per-block
     * send gain (mixer_faderGains, Session 072 step 5).
     */
```

- **`preset_setVoiceFxSendAmount()`, lines 1186–1193:**

```c
    /*
     * Retain one Scene FX-send amount.
     *
     * Inputs: resident Scene index, zero-based instrument slot, and 0..127
     * amount. Output: SceneData retains the value. No runtime write is needed:
     * the mixer pulls the effective amount every block (Session 072 step 5),
     * so Menu/storage callers keep one owner boundary.
     */
```

- **`preset_setVoiceFaderSetting()`, lines 1205–1211:**

```c
    /*
     * Retain one Scene fader mode (0 pre, 1 pst, 2 fx; mixer.h MIXER_FADER_*).
     *
     * Inputs: resident Scene index, zero-based instrument slot, and 0..2 mode.
     * Output: SceneData retains the mode; the mixer reads it every block
     * (mixer_faderGains, Session 072 step 5), so no runtime apply is needed.
     */
```

---

## 9. `sequencer.c` — comments only (modify)

- **Lines 266–268** (`seq_restoreAllSceneAutomation` header): replace "and the
  readable FX-send overlay return to retained values; FX_SEND still has no
  DSP bus owner until Phase 5." with "and the FX-send overlay return to
  retained values (the mixer reads the effective send every block, Session
  072 step 5)."
- **Lines 290–292:** replace "FX send has only the readable overlay until the
  Phase 5 bus exists." with "FX send is restored by clearing its overlay; the
  mixer picks up the retained value on its next block."
- **Lines 795–796** (`seq_applySceneAutomation` header): replace "FX_SEND is
  accepted as a no-op because its Phase 5 runtime bus does not exist yet."
  with "FX_SEND sets a runtime overlay that the mixer applies on its next block
  (Session 072 step 5)."
- **Line 844:** replace
  `/* Store a displayable runtime overlay until the FX bus owns the value. */`
  with
  `/* Runtime FX-send overlay; the mixer reads it every block (step 5). */`

There is no code change in the sequencer.

---

## 10. `menu.c` — lines 3145–3147 (modify, comment only)

Replace "These labels are storage/UI placeholders until mixer/FX routing
implements behavior: pre = normal/pre-FX, pst = post-FX, fx = FX-only." with:
"Labels match the live mixer behavior (mixer.h MIXER_FADER_*, Session 072
step 5): pre = fader scales mix and send; pst = fader scales mix only (send
pre-fader); fx = fader scales send only (mix set by volume)."

---

## 11. Documentation (same change set)

- **`BANK_PRESET_ARCHITECTURE.md`:**
  - Target table row `398–403 … No-op (Phase 5 FX bus) | Stubbed` →
    `… Mixer send (per block) | Live (Session 072 step 5)`.
  - Overlay section "FX Send: … Apply path is no-op until Phase 5 FX bus" →
    "the mixer reads the effective send every block".
  - Add the fader-mode table (mixer.h).
- **`MODULE_INTERCHANGE_SPEC.md`:**
  - mixer: send tap, fader modes, FX bus, `effects_process()`, return routing.
  - Preset: fader/send setters are store-only, and the mixer pulls them.
- **`SRAM_MANIFEST.md`:**
  - DTCM: `mixer_fx_bus` 256 B, `mixer_send_last_gain` 24 B,
    `mixer_fx_return_last_gain` 8 B;
  - statics 4,448 B; arena 126,624 B at `0x20001160`.
- **`EFFECTS_BUS_FEATURE_PLAN.md` §8:**
  - §8.2 still names the ramp variable `mixer_slider_last_gain`; it is now
    `mixer_voice_last_gain`, plus the new send ramp.
  - §8.4: record the D1 balance law.
- **`MEMORY.md` Volatile Notes:** "S072 Step 5: FX bus live. Pre-volume send
  per fader mode (`pre`/`pst`/`fx`), bus summed in `sample_mx_t`, processed as
  float in place, returned at effect level with balance/pan to the Effect
  route. FX_SEND automation is audible."

---

## 12. Build and verification gates

**Build:**

1. `make clean && make all` succeeds with no new warnings.
2. `link_budget.py`: DTCM statics **4,448 B**, FXBUF **126,624 B** at
   `0x20001160`. Record the flash delta (expected under 1.5 KiB).

**Hardware, production build** (`DEV_MODE_DIAGNOSTIC 0`, so every Scene is
`off`):

3. **No change with `off`:** normal play is identical to Step 4 and the `cpu`
   widget reads the same. Fader modes now change the *dry* level:
   - `fx` mode makes the slider inert for the dry signal;
   - `pre` and `pst` behave as before.
4. The `fx` fader-mode dry behavior matches the table: with the slider at
   minimum in `fx` mode, the voice is still heard at its volume.

**Hardware, diagnostic build** (`DEV_MODE_DIAGNOSTIC 1`,
`DEV_EFFECT_FORCE_TYPE 1u`). The active Scene becomes `flt`: LP, cutoff 64,
level 127, centre, route St1.

5. **Send:**
   - Raise one voice's FX send on the VOICE mix sub-page. A filtered copy is
     heard on St1, alongside the dry voice.
   - Send 0 gives no filtered copy.
   - The send ramps smoothly while the knob moves.
6. **Fader modes**, per voice, with the send raised:
   - **`pre`:** the slider fades dry and filtered copy together.
   - **`pst`:** the slider fades only the dry; the filtered copy stays.
   - **`fx`:** the slider fades only the filtered copy; the dry is set by
     `vol` alone.
7. **Pre-volume tap:** lower the voice `vol`. The dry drops but the filtered
   copy does not.
8. **Stereo send panning:** hard-pan a voice. Its filtered copy is panned the
   same way on the return (stereo bus).
9. **FX_SEND automation:** program a step automation on `1fx..6fx`. The send
   follows the step and is restored at transport stop.
10. **Jack fallback:** the Effect route St1 with nothing in MAIN but a cable in
    OUT2 moves the return to OUT2, the same as voices.
11. **Headroom:** six voices at full send and full volume produce no wrap or
    crackle; the filter's soft clip shapes the overload.
12. **CPU:** record the `cpu` widget with `flt` active and all six sends
    raised, against Step 4.
13. **Scene switching** between the `flt` Scene and an `off` Scene while
    playing: the return stops immediately on `off` (by design) and there is
    no crash.
14. **Carry-over Step 4 checks** (ST4 §18.3), on the same card copy:
    - boot screen `r0`;
    - `.hcprms` Effect region `66 6C 74` + `00 10 04 00 7F 40 40 00 00 00 …`
      (closes ST3 gate 7);
    - PERF `mrp` marks Scene parameter 40 (`fxm_amt`).
15. Restore `DEV_MODE_DIAGNOSTIC 0` and `DEV_EFFECT_FORCE_TYPE 0u` afterwards.

**Not testable yet:** Effect parameter edits (Step 7), and the mono-input bus
path (no mono type registered).

**Rollback:** revert the change set. There is no data or file format change.

---

## 13. Implementation notes and measured results

Implementation completed on 2026-09-27.

- `mixer.h` now publishes `MIXER_FADER_PRE`, `MIXER_FADER_POST`, and
  `MIXER_FADER_FX` beside the routing enum. The adjacent block defines the
  dry/send equations and is the source comment for the Scene byte domain.
- `mixer.c` now owns a 256-byte DTCM union for the two-channel 32-frame bus,
  six send ramp origins (24 B), and two return ramp origins (8 B). The mixer
  reads the effective Scene FX send and fader mode once per slot per block,
  taps the decimated pre-volume signal, and keeps the dry path on the existing
  voice gain ramp.
- The live Effect I/O flags select mono/stereo input and output handling. Bus
  accumulation uses `bufferTool_satAdd32()` in `sample_mx_t`; conversion to
  normalized float and `effects_process()` are in place. Return gains ramp
  across the block, use the D1 linear-unity stereo balance law or the voice
  constant-power mono pan law, and route through `mixer_checkOutJackAvailable()`.
- The `effect_io_t` header contract now documents the mono-input/stereo-output
  case: `r` is a zeroed output channel while `channels` remains the input
  channel count. Preset, sequencer, and Menu comments now describe the live
  mixer consumer rather than a deferred/no-op path.
- Reference docs were updated in `BANK_PRESET_ARCHITECTURE.md`,
  `MODULE_INTERCHANGE_SPEC.md`, `SRAM_MANIFEST.md`,
  `EFFECTS_BUS_FEATURE_PLAN.md`, and `MEMORY.md`. The SRAM manifest records
  the approved DTCM expansion and the new arena boundary.

### Build results

- Production clean build (`DEV_MODE_DIAGNOSTIC=0`,
  `DEV_EFFECT_FORCE_TYPE=0u`): passed. `text=465,352`, `data=416`,
  `bss=425,936`; flash use `465,768 / 491,520 B`; DTCM statics `4,448 B`;
  FXBUF `126,624 B` at `0x20001160`, margin `3,744 B` above the 120 KiB
  minimum.
- Diagnostic clean build (`DEV_MODE_DIAGNOSTIC=1`,
  `DEV_EFFECT_FORCE_TYPE=1u`): passed. `text=467,712`, `data=420`,
  `bss=425,944`; the same DTCM/FXBUF budget holds and the forced `flt`
  registry path links successfully. Configuration was restored to production
  values and the final clean build was rerun.
- `arm-none-eabi-nm` confirms `mixer_fx_bus=256 B`,
  `mixer_send_last_gain=24 B`, `mixer_fx_return_last_gain=8 B`, and the
  existing `mixer_voice_last_gain=24 B` in DTCM. `git diff --check` passes.
- The builds retain pre-existing warnings from async FATFS, filesystem,
  USB-driver, and the linker/newlib stubs; no new warning is emitted from the
  ST5 source changes.

### Bench gates still outstanding

The firmware-only work is complete, but the hardware gates in §12 still need
the approved diagnostic image on a board: audible send/fader behavior in all
three modes, pre-volume independence, stereo panning, FX_SEND transport
restore, jack fallback, headroom, CPU comparison, and Scene switching.

---

## 14. Assessment (review of the implemented Step 5 tree)

Reviewed on 2026-09-27 against this schedule. I read the full working-tree
diff and did a clean rebuild.

### 14.1 Build

- **Clean rebuild:** `make clean && make all`, exit 0.
  - `link_budget.py`: flash 465,768 / 491,520 B (headroom **25,752 B**, −1,800 B
    against Step 4). DTCM statics **4,448 B**. FXBUF **126,624 B** at
    `0x20001160`, 3,744 B above the minimum. These match §0 D2 and §13.
- **Warnings:** 20 in the build log. None comes from `mixer.c`, `mixer.h`,
  `EffectsManager.h`, `presetManager.c`, `sequencer.c`, `SceneData.c` or
  `menu.c`. The pre-existing warning set is unchanged.

### 14.2 Code against schedule

| § | Item | Result |
|---|---|---|
| 2 | `MIXER_FADER_*` block in `mixer.h` | Matches, verbatim. |
| 3 | `SceneData.h`, `presetManager.h` includes | Matches. |
| 4 | Bus union, send ramp, return ramp (DTCM) | Matches. `nm` sizes 256 / 24 / 8 B. |
| 5 | `mixer_faderGains` | Equivalent. Written as a PRE base with POST/FX overrides instead of a switch; unknown modes still behave as PRE. |
| 5 | `mixer_addVoiceToFxBus`, `mixer_floatToMx`, `mixer_addFxReturnToOutput` | Match. Routing table, stride 2, clamp and ramp origins are correct. |
| 6.1 | FX I/O snapshot, bus clear | Matches. |
| 6.2 | Per-slot send tap and fader gains | Matches. The send ramp state now updates after the dry write instead of before it. That has no effect: the two taps are independent. |
| 6.3 | Convert → process → return | Matches, with one harmless difference: the conversion loop converts both channels even for a mono-input type. Channel 1 was cleared, so it converts zeros (32 extra conversions). |
| 6.4 | Render-loop comment | Not replaced. The schedule's §6.4 text was not applied; the old comment at `mixer.c:800-810` still says "combined slider x volume gain ramp". The inner per-slot comment was updated instead and covers the same facts. |
| 7 | `effect_io_t` contract comment | Matches. |
| 8–10 | Preset, sequencer, Menu, SceneData comments | Match in substance. |
| 11 | Docs | Updated (see §13). |

### 14.3 Findings

1. **Garbled comment (cosmetic)**, at `mixer.c:739-740` in the
   `effects_service()` comment inside `mixer_calcNextSampleBlock()` (Output
   paragraph).
   - The sentence was edited in place and now repeats "common runtime values
     and type DSP state are current for the" on two lines.
   - Fix when convenient. This is a comment only, with no behavior impact.
2. **Mixed indentation in new `presetManager.c` comment blocks (cosmetic).**
   - The five rewritten blocks (lines 1116–1124, 1137–1144, 1156–1159 body
     only, 1190–1198, 1210–1217) open and fill with tabs but close `*/` with
     the original four spaces.
   - The rest of the file is space-indented. This is whitespace only.
3. **Return ramp is not reset while the Effect is `off`.** This is observed, and
   acceptable as is.
   - `mixer_fx_return_last_gain[]` keeps its last value while `fx_active` is
     0.
   - Re-enabling an Effect therefore ramps the return from the old gain, not
     from 0. The re-activated type starts from reset DSP state, so its output
     begins near silence and no click results.
   - No change is needed. It is recorded so a future type that produces output
     immediately on init (for example a tone generator) can revisit it.
4. **Render-loop comment (§6.4) not applied.** See the table above. The
   documentation is slightly stale, with no behavior impact.

No correctness defects were found.

### 14.4 Hardware (user report, production build)

- The firmware runs normally, with nothing obviously wrong.
- **Fader mode `fx`:** the voice's dry level is no longer attenuated by the
  slider. This passes §12 gates 3 and 4: the dry-path fader topology is
  confirmed on hardware.
- **SHIFT+PERF shows a blank page.** Session 072 has not changed SHIFT+PERF:
  `buttonHandler.c` has no diff, and `menu.c` changed only a comment. The page
  is still the pre-existing `EUKLID_PAGE` route (plan §3). Step 7 replaces it
  with the FX menu. This is not a Step 5 regression.
- **"No positive observables" is expected** in the production build.
  - Every Scene's Effect is `off`, and nothing in Steps 1–5 can select another
    type, except the diagnostic dev hook (`DEV_MODE_DIAGNOSTIC 1`,
    `DEV_EFFECT_FORCE_TYPE 1u`).
  - The audible gates in §12 (5–13) need that diagnostic image. They remain
    open unless you ran it.
  - Step 6 gives the first production-build observable: a hand-written `.fx`
    with `type=flt` in a Scene folder loads a live filter through Scene or
    Bank Load, and survives reboot through AutoSave.

### 14.5 Carry-over to Step 6 bench session

- §12 gates 5–13: diagnostic-image listening tests, which remain optional.
  Step 6 makes the same checks possible in the production image with a
  fixture `.fx`, so they can be run there instead.
- §12 gate 14: the Step 4 carry-over checks (`r0`, the `.hcprms` Effect region
  token, the PERF `mrp` index 40). The `.hcprms` check is superseded by
  Step 6's header bump (version 3), which regenerates the record.
