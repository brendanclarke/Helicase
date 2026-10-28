/*
 * OscillatorWavetable — installed wavetables for the wtb type
 * (S084 VX2 VX2-5.3).
 *
 * What:       plays one catalogue wavetable: frames of any length, 8- or
 *             16-bit, exactly as stored. Every sample reads two adjacent
 *             frames at the same phase and blends them by the frame
 *             position, which the wav row (0..127) maps onto
 *             0..frame_count-1 and which ramps per sample across each block.
 *             The table index and fraction come from one 32 x 32 -> 64-bit
 *             multiply of the phase by the frame length, so every length
 *             costs the same.
 * Why:        plan §4.1.4, W1 (stored as supplied), W2 (optimised on the
 *             host), W3 (one level, no mips). Constant cost: both frames are
 *             always read; wtb never uses the render pool.
 * Inputs:     OscInfo (source, waveform, phase, phaseInc, phaseIncStep,
 *             fmMod, wtb state); the catalogue (SampleMemory).
 * Outputs:    rendered blocks.
 * Accessors:  Oscillator.c dispatch, InstrumentManager.c writers (bind).
 * Affiliates: SampleMemory.h (WavetableInfo), osc_setFreq() (2^32/cycle).
 */
#ifndef OSCILLATORWAVETABLE_H_
#define OSCILLATORWAVETABLE_H_
#include <stdint.h>

/*
 * Per-type wavetable state (S084 VX2 VX2-5.3).
 *
 * What:       the bound table's absolute frame-0 address, frame geometry,
 *             catalogue generation, selected index and the ramped frame
 *             position.
 * Why:        the renderer needs no catalogue read per block beyond this;
 *             the state shares OscInfo's pwm union member slot (VX2-5.2).
 * Writers:    oscWavetable_bind(), the renderers (pos, primed).
 * Readers:    the renderers.
 */
typedef struct {
    uint32_t base;          /* absolute address of frame 0; 0 = silent */
    float    pos;           /* current frame position */
    uint32_t frame_bytes;   /* frame_len * bits / 8 */
    uint32_t generation;    /* catalogue generation at bind */
    uint16_t frame_len;
    uint8_t  frame_count;
    uint8_t  bits;          /* 8 or 16 */
    uint8_t  index;         /* bound wavetable */
    uint8_t  primed;        /* pos is valid */
    uint16_t reserved;
} OscWtbState;              /* 24 B */

struct OscStruct;

/*
 * Bind the selected wavetable (S084 VX2 VX2-5.3).
 *
 * What:       when the source, type or catalogue generation changed,
 *             resolves min(sourceRequest, count - 1) into the state and
 *             clears primed. No tables binds silence (base = 0).
 * Why:        block rate; mirrors oscSample_bind(), including the P1 review
 *             fix F2 rule that the request byte survives a smaller install.
 * Inputs:     osc.  Outputs: state fields.
 * Accessors:  the renderers (every block, a cheap compare) and the IM
 *             type/source writers.
 * Affiliates: sampleMemory_getWavetableInfo(), SAMPLE_FLASH_PTR().
 */
void oscWavetable_bind(struct OscStruct *osc);

/*
 * Render one wavetable block (S084 VX2 VX2-5.3).
 *
 * What:       two frame reads plus a frame blend per sample; the frame
 *             position ramps linearly across the block; 8-bit frames are
 *             sign-extended and shifted left by 8.
 * Why:        W1/W3; constant cost for any frame length.
 * Inputs:     osc, buf, size, gain.  Outputs: buf; phase, phaseInc, pos.
 * Accessors:  Oscillator.c type dispatch.
 * Affiliates: oscWavetable_bind(), oscWavetable_renderFm().
 */
void oscWavetable_render(struct OscStruct *osc, int16_t *buf, uint8_t size,
                         float gain);

/*
 * Render one wavetable block with FM (S084 VX2 VX2-5.3).
 *
 * What:       oscWavetable_render with (mod x fmMod) << 19 added to the read
 *             phase, the existing calcFmBlock depth.
 * Inputs:     osc, modBuffer, buf, size, gain.
 * Outputs:    buf; state advanced.
 * Accessors:  Oscillator.c type dispatch.
 * Affiliates: oscWavetable_render().
 */
void oscWavetable_renderFm(struct OscStruct *osc, int16_t *modBuffer,
                           int16_t *buf, uint8_t size, float gain);
#endif
