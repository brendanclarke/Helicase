/*
 * OscillatorWavetable.c — installed wavetable rendering (S084 VX2 VX2-5.4).
 *
 * Frames are played exactly as installed: any length, 8 or 16 bit, no mips
 * and no expansion. The oscillator reads two adjacent frames every sample so
 * the cost does not depend on the frame count or the frame length.
 *
 *  Modified on: 25.10.2026
 * ------------------------------------------------------------------------------------------------------------------------
 *  Modifications Copyright 2026 Brendan Clarke
 *  brendanpaulclarke@gmail.com
 *  https://www.brendanclarke.com
 * ------------------------------------------------------------------------------------------------------------------------
 *  The modifications to this file are part of the LXR02 Open-Source software.
 *  The same license and restrictions on use for the LXR software apply.
 * ------------------------------------------------------------------------------------------------------------------------
 */

#include "Oscillator.h"
#include "OscillatorWavetable.h"
#include "SampleMemory.h"
#include "config.h"
#include <string.h>

#define WTB_INV_CYCLE 2.3283064365386963e-10f /* 1 / 2^32 */

/*
 * Bind the selected wavetable (S084 VX2 VX2-5.4).
 *
 * What:       re-resolves the live index from sourceRequest, the type and the
 *             catalogue generation; caches the frame geometry and clears the
 *             frame ramp.
 * Why:        a reinstall or a smaller table set must never leave the
 *             renderer pointing into deleted data; the request byte survives
 *             a temporarily smaller install (P1 review fix F2).
 * Inputs:     osc.  Outputs: OscWtbState and osc->source.
 * Accessors:  the renderers (every block), the IM type/source writers.
 * Affiliates: sampleMemory_getWavetableInfo().
 */
void oscWavetable_bind(struct OscStruct *osc)
{
    uint8_t count;
    uint8_t index;
    uint32_t generation;

    if (!osc || osc->oscType != OSC_TYPE_WTB)
        return;
    count = sampleMemory_getNumWavetables();
    if (count == 0u) {
        osc->source = 0u;
        osc->wtb.base = 0u;
        osc->wtb.frame_len = 0u;
        osc->wtb.frame_count = 0u;
        osc->wtb.bits = 0u;
        osc->wtb.frame_bytes = 0u;
        osc->wtb.primed = 0u;
        return;
    }
    index = osc->sourceRequest;
    if (index >= count)
        index = (uint8_t)(count - 1u);
    osc->source = index;
    generation = sampleMemory_getGeneration();
    if (osc->wtb.base == 0u || osc->wtb.index != index ||
        osc->wtb.generation != generation) {
        WavetableInfo info;

        if (!sampleMemory_getWavetableInfo(index, &info)) {
            osc->wtb.base = 0u;
            return;
        }
        osc->wtb.base = info.offset;
        osc->wtb.frame_len = info.frame_len;
        osc->wtb.frame_count = info.frame_count;
        osc->wtb.bits = info.bits;
        osc->wtb.frame_bytes = (uint32_t)info.frame_len *
                               (uint32_t)(info.bits / 8u);
        osc->wtb.generation = generation;
        osc->wtb.index = index;
        osc->wtb.primed = 0u;
    }
}

/*
 * Render one wavetable block (S084 VX2 VX2-5.4).
 *
 * What:       the frame target is waveform x (count - 1) / 127 and ramps
 *             across the block; each sample reads both frame neighbours at
 *             the interpolated phase and blends them. 8-bit frames are
 *             sign-extended and shifted left by 8.
 * Why:        W1/W3; one index/fraction multiply per sample for any length.
 * Inputs:     osc, buf, size, gain.  Outputs: buf; phase, phaseInc, pos.
 * Accessors:  Oscillator.c type dispatch.
 * Affiliates: oscWavetable_bind(), oscWavetable_renderFm().
 */
INITCM void oscWavetable_render(struct OscStruct *osc, int16_t *buf,
                                uint8_t size, float gain)
{
    OscWtbState *w;
    uint32_t phase;
    uint32_t inc;
    int32_t step;
    float pos;
    float target;
    float posStep;
    uint8_t i;

    if (!osc || !buf)
        return;
    oscWavetable_bind(osc);
    w = &osc->wtb;
    phase = osc->phase;
    inc = osc->phaseInc;
    step = osc->phaseIncStep;
    if (w->base == 0u || w->frame_count == 0u || w->frame_len < 2u) {
        for (i = 0u; i < size; i++) {
            buf[i] = 0;
            phase += inc;
            inc += (uint32_t)step;
        }
        osc->phase = phase;
        osc->phaseInc = inc;
        return;
    }
    target = (float)osc->waveform * (float)(w->frame_count - 1u) / 127.0f;
    if (!w->primed) {
        w->pos = target;
        w->primed = 1u;
    }
    pos = w->pos;
    posStep = (target - pos) / (float)size;

    if (w->bits == 8u) {
        const int8_t *base = (const int8_t *)SAMPLE_FLASH_PTR(w->base);

        for (i = 0u; i < size; i++) {
            const uint64_t m = (uint64_t)phase * (uint64_t)w->frame_len;
            const uint32_t idx = (uint32_t)(m >> 32);
            const float fr = (float)(uint32_t)m * WTB_INV_CYCLE;
            const uint32_t n = (idx + 1u == w->frame_len) ? 0u : (idx + 1u);
            uint32_t fi;
            uint32_t fn;
            float ff;
            int32_t a;
            int32_t b;
            int32_t a2;
            int32_t b2;

            pos += posStep;
            fi = (uint32_t)pos;
            if (fi >= w->frame_count)
                fi = (uint32_t)(w->frame_count - 1u);
            ff = pos - (float)fi;
            fn = fi + 1u;
            if (fn >= w->frame_count)
                fn = fi;

            a = (int32_t)base[fi * w->frame_bytes + idx] << 8;
            b = (int32_t)base[fi * w->frame_bytes + n] << 8;
            a2 = (int32_t)base[fn * w->frame_bytes + idx] << 8;
            b2 = (int32_t)base[fn * w->frame_bytes + n] << 8;
            a = a + (int32_t)(ff * (float)(a2 - a));
            b = b + (int32_t)(ff * (float)(b2 - b));
            buf[i] = (int16_t)((float)(a + (int32_t)(fr * (float)(b - a))) *
                               gain);
            phase += inc;
            inc += (uint32_t)step;
        }
    } else {
        const int16_t *base = (const int16_t *)SAMPLE_FLASH_PTR(w->base);
        const uint32_t stride = w->frame_len;

        for (i = 0u; i < size; i++) {
            const uint64_t m = (uint64_t)phase * (uint64_t)w->frame_len;
            const uint32_t idx = (uint32_t)(m >> 32);
            const float fr = (float)(uint32_t)m * WTB_INV_CYCLE;
            const uint32_t n = (idx + 1u == w->frame_len) ? 0u : (idx + 1u);
            uint32_t fi;
            uint32_t fn;
            float ff;
            int32_t a;
            int32_t b;
            int32_t a2;
            int32_t b2;

            pos += posStep;
            fi = (uint32_t)pos;
            if (fi >= w->frame_count)
                fi = (uint32_t)(w->frame_count - 1u);
            ff = pos - (float)fi;
            fn = fi + 1u;
            if (fn >= w->frame_count)
                fn = fi;

            a = (int32_t)base[fi * stride + idx];
            b = (int32_t)base[fi * stride + n];
            a2 = (int32_t)base[fn * stride + idx];
            b2 = (int32_t)base[fn * stride + n];
            a = a + (int32_t)(ff * (float)(a2 - a));
            b = b + (int32_t)(ff * (float)(b2 - b));
            buf[i] = (int16_t)((float)(a + (int32_t)(fr * (float)(b - a))) *
                               gain);
            phase += inc;
            inc += (uint32_t)step;
        }
    }
    w->pos = pos;
    osc->phase = phase;
    osc->phaseInc = inc;
}

/*
 * Render one wavetable block with FM (S084 VX2 VX2-5.4).
 *
 * What:       oscWavetable_render with (mod x fmMod) << 19 added to the read
 *             phase; the unmodulated phase still advances the oscillator.
 * Why:        the existing calcFmBlock depth, constant cost.
 * Inputs:     osc, modBuffer, buf, size, gain.
 * Outputs:    buf; phase, phaseInc, pos advanced.
 * Accessors:  Oscillator.c type dispatch.
 * Affiliates: oscWavetable_render().
 */
INITCM void oscWavetable_renderFm(struct OscStruct *osc, int16_t *modBuffer,
                                  int16_t *buf, uint8_t size, float gain)
{
    OscWtbState *w;
    uint32_t phase;
    uint32_t inc;
    int32_t step;
    float pos;
    float target;
    float posStep;
    uint8_t i;

    if (!osc || !buf || !modBuffer)
        return;
    oscWavetable_bind(osc);
    w = &osc->wtb;
    phase = osc->phase;
    inc = osc->phaseInc;
    step = osc->phaseIncStep;
    if (w->base == 0u || w->frame_count == 0u || w->frame_len < 2u) {
        for (i = 0u; i < size; i++) {
            buf[i] = 0;
            phase += inc;
            inc += (uint32_t)step;
        }
        osc->phase = phase;
        osc->phaseInc = inc;
        return;
    }
    target = (float)osc->waveform * (float)(w->frame_count - 1u) / 127.0f;
    if (!w->primed) {
        w->pos = target;
        w->primed = 1u;
    }
    pos = w->pos;
    posStep = (target - pos) / (float)size;

    if (w->bits == 8u) {
        const int8_t *base = (const int8_t *)SAMPLE_FLASH_PTR(w->base);

        for (i = 0u; i < size; i++) {
            const uint32_t readPhase = phase +
                (uint32_t)((int32_t)((float)modBuffer[i] * osc->fmMod) << 19);
            const uint64_t m = (uint64_t)readPhase * (uint64_t)w->frame_len;
            const uint32_t idx = (uint32_t)(m >> 32);
            const float fr = (float)(uint32_t)m * WTB_INV_CYCLE;
            const uint32_t n = (idx + 1u == w->frame_len) ? 0u : (idx + 1u);
            uint32_t fi;
            uint32_t fn;
            float ff;
            int32_t a;
            int32_t b;
            int32_t a2;
            int32_t b2;

            pos += posStep;
            fi = (uint32_t)pos;
            if (fi >= w->frame_count)
                fi = (uint32_t)(w->frame_count - 1u);
            ff = pos - (float)fi;
            fn = fi + 1u;
            if (fn >= w->frame_count)
                fn = fi;

            a = (int32_t)base[fi * w->frame_bytes + idx] << 8;
            b = (int32_t)base[fi * w->frame_bytes + n] << 8;
            a2 = (int32_t)base[fn * w->frame_bytes + idx] << 8;
            b2 = (int32_t)base[fn * w->frame_bytes + n] << 8;
            a = a + (int32_t)(ff * (float)(a2 - a));
            b = b + (int32_t)(ff * (float)(b2 - b));
            buf[i] = (int16_t)((float)(a + (int32_t)(fr * (float)(b - a))) *
                               gain);
            phase += inc;
            inc += (uint32_t)step;
        }
    } else {
        const int16_t *base = (const int16_t *)SAMPLE_FLASH_PTR(w->base);
        const uint32_t stride = w->frame_len;

        for (i = 0u; i < size; i++) {
            const uint32_t readPhase = phase +
                (uint32_t)((int32_t)((float)modBuffer[i] * osc->fmMod) << 19);
            const uint64_t m = (uint64_t)readPhase * (uint64_t)w->frame_len;
            const uint32_t idx = (uint32_t)(m >> 32);
            const float fr = (float)(uint32_t)m * WTB_INV_CYCLE;
            const uint32_t n = (idx + 1u == w->frame_len) ? 0u : (idx + 1u);
            uint32_t fi;
            uint32_t fn;
            float ff;
            int32_t a;
            int32_t b;
            int32_t a2;
            int32_t b2;

            pos += posStep;
            fi = (uint32_t)pos;
            if (fi >= w->frame_count)
                fi = (uint32_t)(w->frame_count - 1u);
            ff = pos - (float)fi;
            fn = fi + 1u;
            if (fn >= w->frame_count)
                fn = fi;

            a = (int32_t)base[fi * stride + idx];
            b = (int32_t)base[fi * stride + n];
            a2 = (int32_t)base[fn * stride + idx];
            b2 = (int32_t)base[fn * stride + n];
            a = a + (int32_t)(ff * (float)(a2 - a));
            b = b + (int32_t)(ff * (float)(b2 - b));
            buf[i] = (int16_t)((float)(a + (int32_t)(fr * (float)(b - a))) *
                               gain);
            phase += inc;
            inc += (uint32_t)step;
        }
    }
    w->pos = pos;
    osc->phase = phase;
    osc->phaseInc = inc;
}
