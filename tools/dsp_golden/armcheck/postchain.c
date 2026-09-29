/*
 * ARM codegen check for fused voice post-chains (S073 Step 4).
 *
 * What:       compiles frozen separate passes and current fused helpers with
 *             the firmware DSP flags for fpseq.py comparison.
 * Why:        host bit identity must be accompanied by matching target VFP,
 *             conversion and saturation sequences for S0.
 * Inputs:     generated frozen pass fragments and current headers.
 * Outputs:    one ARM object consumed by armcheck-postchain.
 * Accessors:  tools/dsp_golden/Makefile armcheck-postchain.
 * Affiliates: voicePostChain.h, BufferTools.h, distortion.h and fpseq.py.
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
    voicePost_mixAddGainDist(buf, transBuf, voice->mix, ampGain,
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
    voicePost_addGainDist(buf, mod, ampGain, &voice->distortion, size);
}

__attribute__((noinline)) void new_drum(int16_t *buf, float gain,
                                        float lastGain, float velo,
                                        uint8_t volumeMod,
                                        const Distortion *dist, uint8_t size)
{
    voicePost_drum(buf, gain, lastGain,
                   volumeMod ? velo : 1.0f, dist, size);
}
