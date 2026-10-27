/*
 * Fused voice post-chains (S073 Step 4).
 *
 * What:       performs the existing int16 post stages in one per-sample loop.
 * Why:        removes intermediate load/store passes while preserving every
 *             truncation, saturation and distortion division. Every sample
 *             and stage still runs; volumeMod is only a block-level gain select.
 * Inputs:     engine-specific filtered blocks, gain values and distortion.
 * Outputs:    the post-chain int16 block in place.
 * Accessors:  the four instrument calcSyncBlockVoice() functions.
 * Affiliates: BufferTools.h, distortion.h and the Step 4 golden/ARM checks.
 * S085 P2:    every chain ramps its gain from a "last" value; equal
 *             endpoints are the S073 loop exactly (rst unchanged).
 */
#ifndef VOICE_POST_CHAIN_H_
#define VOICE_POST_CHAIN_H_

#include <stdint.h>
#include "BufferTools.h"
#include "distortion.h"

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

#endif /* VOICE_POST_CHAIN_H_ */
