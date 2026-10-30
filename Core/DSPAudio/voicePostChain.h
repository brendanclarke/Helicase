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
 * Drum amp ramp -> velocity ramp -> distortion (S073 Step 4; S088 P0 §3 2b;
 * S088 P1 float-through).
 *
 * What:       amp and velocity gains are merged into one float multiply per
 *             sample and passed to distortion_curveSampleF() without an
 *             intermediate int16 truncation. The final int16 store happens
 *             inside the distortion curve.
 * Why:        the two successive int16 truncations quantised small-amplitude
 *             attack onsets to ±1 LSB, producing a HF puff audible on a pure
 *             sine retrigger. Keeping the product in float until distortion
 *             preserves sub-LSB detail through the curve.
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
	const float inv_size = (size > 1u) ? 1.f/(size-1.f) : 0.f;
	for(i=0;i<size;i++)
	{
		const float ag = bufferTool_interpolatedGain(i, inv_size, gain, lastGain);
		const float vg = bufferTool_interpolatedGain(i, inv_size,
		                                              veloGain, lastVeloGain);
		buf[i] = distortion_curveSampleF(dist, (float)buf[i] * ag * vg);
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
		const float ag = bufferTool_interpolatedGain(j, inv_size,
		                                              ampGain, lastAmpGain);
		buf[j] = distortion_curveSampleF(dist, (float)s * ag);
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
		const float ag = bufferTool_interpolatedGain(j, inv_size,
		                                              ampGain, lastAmpGain);
		buf[j] = distortion_curveSampleF(dist, (float)s * ag);
	}
}

#endif /* VOICE_POST_CHAIN_H_ */
