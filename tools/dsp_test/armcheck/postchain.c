/*
 * ARM codegen check for fused voice post-chains (S073 Step 4).
 *
 * What:       compiles frozen separate passes and current fused helpers with
 *             the firmware DSP flags for fpseq.py comparison.
 * Why:        host bit identity must be accompanied by matching target VFP,
 *             conversion and saturation sequences for S0.
 * Inputs:     generated frozen pass fragments and current headers.
 * Outputs:    one ARM object consumed by armcheck-postchain.
 * Accessors:  tools/dsp_test/Makefile armcheck-postchain.
 * Affiliates: voicePostChain.h, BufferTools.h, distortion.h and fpseq.py.
 * S085 P2:    the chains ramp from "last" endpoints, so they now add value
 *             operations over the frozen passes even with equal endpoints;
 *             the fpseq.py runs are reports (--report) and osccost.py gives
 *             the per-sample cost. The S0 proof is the host postchain test.
 */
#include "config.h"
#include "BufferTools.h"
#include "distortion.h"
#include "voicePostChain.h"

#include "pc_old_bt_arm.c"
#include "pc_old_dist_arm.c"

typedef struct {
    float mix;
    float velo;
    float egValueOscVol;
    uint8_t volumeMod;
    Distortion distortion;
} VoiceStub;

__attribute__((noinline)) void ref_snare(int16_t *buf, int16_t *transBuf,
                                         VoiceStub *voice, uint8_t size)
{
#include "pc_old_snare_body.inc"
}

__attribute__((noinline)) void new_snare(int16_t *buf, int16_t *transBuf,
                                         VoiceStub *voice, uint8_t size)
{
    const float ampGain = voice->volumeMod ?
        voice->velo * voice->egValueOscVol : voice->egValueOscVol;
    voicePost_mixAddGainDist(buf, transBuf, voice->mix, ampGain, ampGain,
                             &voice->distortion, size);
}

__attribute__((noinline)) void ref_cymbal(int16_t *buf, int16_t *mod,
                                          VoiceStub *voice, uint8_t size)
{
#include "pc_old_cym_body.inc"
}

__attribute__((noinline)) void new_addgain(int16_t *buf, int16_t *mod,
                                           VoiceStub *voice, uint8_t size)
{
    const float ampGain = voice->volumeMod ?
        voice->velo * voice->egValueOscVol : voice->egValueOscVol;
    voicePost_addGainDist(buf, mod, ampGain, ampGain, &voice->distortion,
                          size);
}

__attribute__((noinline)) void new_drum(int16_t *buf, float gain,
                                        float lastGain, float velo,
                                        uint8_t volumeMod,
                                        const Distortion *dist, uint8_t size)
{
    const float veloGain = volumeMod ? velo : 1.0f;
    voicePost_drum(buf, gain, lastGain, veloGain, veloGain, dist, size);
}

/*
 * Ramped post-chains (S085 P2, C7.3).
 *
 * What:       the three chains with separate "last" endpoints, as the
 *             firmware calls them on an armed fre/itp retrigger block.
 * Why:        the cost report (osccost.py) for the worst case; the loops
 *             run the same instructions whatever the endpoint values.
 * Accessors:  tools/dsp_test/Makefile armcheck-postchain.
 * Affiliates: voicePostChain.h, osccost.py.
 */
__attribute__((noinline)) void ramp_drum(int16_t *buf, float gain,
                                         float lastGain, float veloGain,
                                         float lastVeloGain,
                                         const Distortion *dist, uint8_t size)
{
    voicePost_drum(buf, gain, lastGain, veloGain, lastVeloGain, dist, size);
}

__attribute__((noinline)) void ramp_snare(int16_t *buf, int16_t *transBuf,
                                          float mix, float ampGain,
                                          float lastAmpGain,
                                          const Distortion *dist, uint8_t size)
{
    voicePost_mixAddGainDist(buf, transBuf, mix, ampGain, lastAmpGain,
                             dist, size);
}

__attribute__((noinline)) void ramp_addgain(int16_t *buf, int16_t *mod,
                                            float ampGain, float lastAmpGain,
                                            const Distortion *dist,
                                            uint8_t size)
{
    voicePost_addGainDist(buf, mod, ampGain, lastAmpGain, dist, size);
}
