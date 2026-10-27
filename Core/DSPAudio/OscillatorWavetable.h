/*
 * OscillatorWavetable — the wtb oscillator type (S085 C8.1).
 *
 * What:       plays an installed wavetable (frames of any length, 8- or
 *             16-bit, stored as supplied). src selects the table; the wav
 *             row is the frame position 0..127 across the table's frames.
 *             Each block selects one pair of neighbouring frames and ramps
 *             the blend between them; per sample it reads two points from
 *             each frame (one 32 x 32 -> 64 multiply gives index and
 *             fraction for any frame length) and blends.
 * Why:        plan §5 item 2, Q9 as revised by S085 D1: the frame pair is
 *             chosen once per block, so the worst case (fast position sweep)
 *             costs the same as a static position. The position moves at
 *             most one frame per block (about 1,378 frames/s); faster
 *             requests glide.
 * Inputs:     OscInfo (oscType, waveform = table index, waveRequest,
 *             source = table position, phase, phaseInc, phaseIncStep,
 *             fmMod, wtb).
 * Outputs:    rendered blocks.
 * Accessors:  Oscillator.c dispatch; InstrumentManager.c writers (bind).
 * Affiliates: SampleMemory.h (WavetableInfo), osc_setFreq() (2^32/cycle).
 */
#ifndef OSCILLATORWAVETABLE_H_
#define OSCILLATORWAVETABLE_H_

#include <stdint.h>

/*
 * Per-type wavetable state (S085 C8.1).
 *
 * What:       the bound table (absolute frame-0 address, bytes per frame,
 *             geometry, catalogue generation, index) and the frame position
 *             carried between blocks (pos, primed).
 * Why:        no catalogue read per block beyond the bind compare; shares
 *             OscInfo's union with OscPwmState (24 B <= 28 B).
 * Writers:    oscWavetable_bind(), the renderers (pos, primed),
 *             IM_SPECIAL_OSC_TYPE writer (clears base/primed on entering wtb).
 */
typedef struct {
    uint32_t base;          /* absolute address of frame 0; 0 = silent */
    float    pos;           /* frame position at the end of the last block */
    uint32_t frame_bytes;   /* frame_len x bits / 8 */
    uint32_t generation;    /* catalogue generation at bind */
    uint16_t frame_len;
    uint8_t  frame_count;
    uint8_t  bits;          /* 8 or 16 */
    uint8_t  index;         /* bound wavetable */
    uint8_t  primed;        /* pos is valid */
    uint16_t reserved;
} OscWtbState;

struct OscStruct;

/*
 * Bind the selected wavetable (S085 C8.1).
 *
 * What:       live index = min(waveform, count - 1); waveform is first
 *             re-expanded from waveRequest when the catalogue generation
 *             changed. On a changed index or generation (or base 0) reloads
 *             the geometry and clears primed; no tables binds silence
 *             (base 0, waveform 0).
 * Accessors:  the renderers (every block), the IM type/source writers.
 * Affiliates: sampleMemory_getWavetableInfo().
 */
void oscWavetable_bind(struct OscStruct *osc);

/*
 * Render one wavetable block (S085 C8.1).
 *
 * What:       see the header contract; 8-bit points are sign-extended and
 *             shifted left by 8; the FM variant adds (mod x fmMod) << 19 to
 *             the read phase. Silence still advances the phase.
 * Inputs:     osc, (modBuffer), buf, size, gain.
 * Outputs:    buf; phase, phaseInc, wtb.pos.
 * Accessors:  Oscillator.c type dispatch.
 */
void oscWavetable_render(struct OscStruct *osc, int16_t *buf, uint8_t size,
                         float gain);
void oscWavetable_renderFm(struct OscStruct *osc, int16_t *modBuffer,
                           int16_t *buf, uint8_t size, float gain);

/* Hard-sync slave render (S085 assessment A2); see OscSyncLane.h. */
struct OscSyncLane;
void oscWavetable_renderSync(struct OscStruct *osc, int16_t *buf, uint8_t size,
                             float gain, const struct OscSyncLane *lane);

#endif /* OSCILLATORWAVETABLE_H_ */
