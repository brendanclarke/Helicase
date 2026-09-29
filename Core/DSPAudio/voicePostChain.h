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
 * Drum amp ramp -> velocity -> distortion (S073 Step 4).
 * Inputs: filtered block, current/previous gains, selected velocity gain,
 *         distortion state and size. Outputs: buf.
 * Accessors: DrumVoice.c. Affiliates: shared BufferTools/distortion helpers.
 */
static inline void voicePost_drum(int16_t *buf, const float gain,
		const float lastGain, const float veloGain,
		const Distortion *dist, const uint8_t size)
{
	uint8_t i;
	const float inv_size = 1.f/(size-1.f);
	for(i=0;i<size;i++)
	{
		const int16_t eg = bufferTool_floatToInt16Store(
			buf[i] * bufferTool_interpolatedGain(i, inv_size, gain, lastGain));
		const int16_t vel = bufferTool_floatToInt16Store(eg * veloGain);
		buf[i] = distortion_curveSample16(dist, vel);
	}
}

/*
 * Snare mix -> saturating add -> amp gain -> distortion (S073 Step 4).
 * Inputs: filtered block, transient block, mix, selected amp gain, distortion
 *         state and size. Outputs: buf.
 * Accessors: Snare.c. Affiliates: shared BufferTools/distortion helpers.
 */
static inline void voicePost_mixAddGainDist(int16_t *buf, const int16_t *add,
		const float mix, const float ampGain,
		const Distortion *dist, const uint8_t size)
{
	uint8_t j;
	for(j=0;j<size;j++)
	{
		const int16_t m = bufferTool_floatToInt16Store(buf[j] * mix);
		const int16_t s = bufferTool_satAdd16(m, add[j]);
		const int16_t g = bufferTool_floatToInt16Store(s * ampGain);
		buf[j] = distortion_curveSample16(dist, g);
	}
}

/*
 * Cymbal/HiHat saturating add -> amp gain -> distortion (S073 Step 4).
 * Inputs: filtered block, transient block, selected amp gain, distortion
 *         state and size. Outputs: buf.
 * Accessors: CymbalVoice.c and HiHat.c. Affiliates: shared helpers.
 */
static inline void voicePost_addGainDist(int16_t *buf, const int16_t *add,
		const float ampGain, const Distortion *dist, const uint8_t size)
{
	uint8_t j;
	for(j=0;j<size;j++)
	{
		const int16_t s = bufferTool_satAdd16(buf[j], add[j]);
		const int16_t g = bufferTool_floatToInt16Store(s * ampGain);
		buf[j] = distortion_curveSample16(dist, g);
	}
}

#endif /* VOICE_POST_CHAIN_H_ */
