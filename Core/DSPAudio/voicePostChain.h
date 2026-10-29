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
 */
#ifndef VOICE_POST_CHAIN_H_
#define VOICE_POST_CHAIN_H_

#include <stdint.h>
#include "BufferTools.h"
#include "distortion.h"

/*
 * Drum amp ramp -> velocity ramp -> distortion (S073 Step 4; S088 P0 §3 2b).
 *
 * What:       the existing amp ramp (lastGain -> gain) is unchanged. A second
 *             ramp (lastVeloGain -> veloGain) is added around the velocity
 *             multiply, so a velocity step on a fre/itp retrigger glides over
 *             the armed block instead of stepping at its first sample.
 * Why:        without it a velocity step (for example 0.3 -> 1.0) at a
 *             non-zero gain is an audible pop. On every other block the
 *             caller passes lastVeloGain == veloGain, so the ramp evaluates to
 *             exactly veloGain and the output is bit-identical to S073.
 * Inputs:     filtered buf, current/previous amp gains, current/previous
 *             velocity gains (volumeMod ? velo : 1.0f), distortion state,
 *             block size.
 * Outputs:    buf overwritten in place.
 * Accessors:  Drum_calcVoiceSyncBlock() (DrumVoice.c).
 * Affiliates: bufferTool_interpolatedGain(); DrumVoice.lastVeloGain and
 *             DrumVoice.ampRampArm; the S073 Step 4 golden checks.
 */
static inline void voicePost_drum(int16_t *buf, const float gain,
		const float lastGain, const float veloGain,
		const float lastVeloGain,
		const Distortion *dist, const uint8_t size)
{
	uint8_t i;
	/* size is OUTPUT_DMA_SIZE in the mixer; the size==1 guard keeps a host
	 * call from evaluating 0 x infinity (NaN) in the ramp fraction. */
	const float inv_size = (size > 1u) ? 1.f/(size-1.f) : 0.f;
	for(i=0;i<size;i++)
	{
		const int16_t eg = bufferTool_floatToInt16Store(
			buf[i] * bufferTool_interpolatedGain(i, inv_size, gain, lastGain));
		/* Velocity ramp: flat (exactly veloGain) unless the caller arms it. */
		const float vg = bufferTool_interpolatedGain(i, inv_size,
		                                              veloGain, lastVeloGain);
		const int16_t vel = bufferTool_floatToInt16Store(eg * vg);
		buf[i] = distortion_curveSample16(dist, vel);
	}
}

/*
 * Snare mix -> saturating add -> amp gain ramp -> distortion (S073 Step 4;
 * S088 P0 §3 Layer 2b).
 *
 * What:       adds a lastAmpGain endpoint; the amp multiply ramps from it to
 *             ampGain across the block instead of using a constant gain.
 * Why:        a velocity or amp-EG step on a fre/itp retrigger produces a pop
 *             without the ramp. Callers pass lastAmpGain == ampGain on every
 *             non-armed block (and in rst), where the interpolation is
 *             exactly ampGain and the output stays bit-identical to S073.
 * Inputs:     filtered buf, transient block, mix, current/previous amp gain,
 *             distortion state, block size.
 * Outputs:    buf overwritten in place.
 * Accessors:  Snare_calcSyncBlockVoice() (Snare.c).
 * Affiliates: bufferTool_interpolatedGain(); SnareVoice.lastAmpGain and
 *             ampRampArm; the S073 Step 4 golden checks.
 */
static inline void voicePost_mixAddGainDist(int16_t *buf, const int16_t *add,
		const float mix, const float ampGain, const float lastAmpGain,
		const Distortion *dist, const uint8_t size)
{
	uint8_t j;
	/* size is OUTPUT_DMA_SIZE in the mixer; see voicePost_drum(). */
	const float inv_size = (size > 1u) ? 1.f/(size-1.f) : 0.f;
	for(j=0;j<size;j++)
	{
		const int16_t m = bufferTool_floatToInt16Store(buf[j] * mix);
		const int16_t s = bufferTool_satAdd16(m, add[j]);
		/* Amp ramp: flat (exactly ampGain) unless the caller arms it. */
		const float ag = bufferTool_interpolatedGain(j, inv_size,
		                                              ampGain, lastAmpGain);
		const int16_t g = bufferTool_floatToInt16Store(s * ag);
		buf[j] = distortion_curveSample16(dist, g);
	}
}

/*
 * Cymbal/HiHat saturating add -> amp gain ramp -> distortion (S073 Step 4;
 * S088 P0 §3 Layer 2b).
 *
 * What:       adds a lastAmpGain endpoint; same ramp rule as
 *             voicePost_mixAddGainDist().
 * Why:        same click and bit-identity argument as the Snare chain.
 * Inputs:     filtered buf, transient/mod block, current/previous amp gain,
 *             distortion state, block size.
 * Outputs:    buf overwritten in place.
 * Accessors:  Cymbal_calcSyncBlockVoice(), HiHat_calcSyncBlockVoice().
 * Affiliates: bufferTool_interpolatedGain(); the engine's lastAmpGain/
 *             ampRampArm; the S073 Step 4 golden checks.
 */
static inline void voicePost_addGainDist(int16_t *buf, const int16_t *add,
		const float ampGain, const float lastAmpGain,
		const Distortion *dist, const uint8_t size)
{
	uint8_t j;
	/* size is OUTPUT_DMA_SIZE in the mixer; see voicePost_drum(). */
	const float inv_size = (size > 1u) ? 1.f/(size-1.f) : 0.f;
	for(j=0;j<size;j++)
	{
		const int16_t s = bufferTool_satAdd16(buf[j], add[j]);
		/* Amp ramp: flat (exactly ampGain) unless the caller arms it. */
		const float ag = bufferTool_interpolatedGain(j, inv_size,
		                                              ampGain, lastAmpGain);
		const int16_t g = bufferTool_floatToInt16Store(s * ag);
		buf[j] = distortion_curveSample16(dist, g);
	}
}

#endif /* VOICE_POST_CHAIN_H_ */
