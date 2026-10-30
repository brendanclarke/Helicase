/*
 * transientGenerator.c
 *
 * Renders the transient by kind: the original PCM clicks, installed samples
 * (Q20.12, catalogue gain), and 50 ms pitched bursts of the stock oscillator
 * waves. Shaping kinds output silence; the engines apply their effect.
 *
 *  Created on: 29.06.2012
 * ------------------------------------------------------------------------------------------------------------------------
 *  Copyright 2013 Julian Schmidt
 *  Julian@sonic-potions.com
 * ------------------------------------------------------------------------------------------------------------------------
 *  This file is part of the Sonic Potions LXR drumsynth firmware.
 *  Redistribution and use follow the original LXR terms.
 * ------------------------------------------------------------------------------------------------------------------------
 */

#include "transientGenerator.h"
#include "SampleMemory.h"
#include "StockWaves.h"
#include "Oscillator.h"
#include "Samples.h"
#include "wavetable.h"
#include "InsertEffectsManager.h"
#include "config.h"
#include <string.h>

#define TRANSIENT_BURST_DECAY    0.99773f   /* exp(-1 / (0.010 * 44108)) */
#define TRANSIENT_BURST_BASE_HZ  440.0f
#define TRANSIENT_INV_CYCLE      2.3283064365386963e-10f /* 1 / 2^32 */
#define TRANSIENT_SAMPLE_SHIFT   12u
#define TRANSIENT_PCM_END_PHASE  2311061504u  /* 2204 << 20 */

/*
 * Resolve the live wav byte against the type (S084 VX2 VX2-6.2).
 *
 * What:       sets kind, source and, for samples, the catalogue data
 *             address, frame count, gain code and generation.
 * Why:        the wav row means a shape, a PCM row, a sample or a stock wave
 *             depending on trt; one resolver keeps the renderer and the cell
 *             formatter in agreement.
 * Inputs:     the generator's type and requested wav byte.
 * Outputs:    kind, waveform (clamped), source, sample fields.
 * Accessors:  transient_setType(), transient_setWave(),
 *             transient_calcBlock() (generation change).
 * Affiliates: SampleMemory.h, transient_waveMax().
 */
static void transient_resolve(TransientGenerator *t)
{
    uint8_t samples;
    uint8_t wf;

    t->kind = TRANSIENT_KIND_NONE;
    t->source = 0u;
    t->dataAddr = 0u;
    t->frames = 0u;
    t->gainCode = 0u;
    t->generation = sampleMemory_getGeneration();

    if (t->type == TRANSIENT_TYPE_TRS) {
        t->waveform = (uint8_t)((t->waveRequest > 1u) ? 1u : t->waveRequest);
        t->kind = (t->waveform == 0u)
            ? TRANSIENT_KIND_SNAP : TRANSIENT_KIND_OFFSET;
        return;
    }
    if (t->type == TRANSIENT_TYPE_BUF) {
        t->waveform = (t->waveRequest >= INSERT_TRANSIENT_COUNT)
            ? (INSERT_TRANSIENT_COUNT - 1u) : t->waveRequest;
        t->kind = TRANSIENT_KIND_BUFFER;
        return;
    }
    if (t->type != TRANSIENT_TYPE_WAV) {
        t->waveform = 0u;
        return;
    }

    samples = sampleMemory_getNumSamples();
    wf = t->waveRequest;
    {
        const uint16_t max_wave = (uint16_t)(TRANSIENT_STOCK_PCM_COUNT +
            samples + OSC_STOCK_WAVE_COUNT - 1u);

        if (wf > max_wave)
            wf = (uint8_t)max_wave;
    }
    t->waveform = wf;
    if (wf < TRANSIENT_STOCK_PCM_COUNT) {
        t->kind = TRANSIENT_KIND_PCM;
        t->source = wf;
        return;
    }
    if (wf < (uint8_t)(TRANSIENT_STOCK_PCM_COUNT + samples)) {
        SampleInfo info = sampleMemory_getSampleInfo(
            (uint8_t)(wf - TRANSIENT_STOCK_PCM_COUNT));

        t->source = (uint8_t)(wf - TRANSIENT_STOCK_PCM_COUNT);
        t->dataAddr = info.offset;
        t->frames = info.size;
        t->gainCode = info.gain_code;
        t->kind = info.size ? TRANSIENT_KIND_SAMPLE : TRANSIENT_KIND_NONE;
        return;
    }
    {
        const uint8_t id = (uint8_t)(wf - TRANSIENT_STOCK_PCM_COUNT - samples);

        t->source = id;
        if (id == 0u)
            t->kind = TRANSIENT_KIND_SINE;
        else if (id == 4u)
            t->kind = TRANSIENT_KIND_NOISE;
        else if (id == 5u)
            t->kind = TRANSIENT_KIND_CRASH;
        else
            t->kind = TRANSIENT_KIND_TABLE;
    }
}

void transient_init(TransientGenerator* transient)
{
    if (!transient)
        return;
    transient->output = 0;
    transient->type = TRANSIENT_TYPE_TRS;
    transient->waveRequest = 0u;
    transient->phase = 0u;
    transient->pitch = 1.0f;
    transient->waveform = 0u;
    transient->kind = TRANSIENT_KIND_SNAP;
    transient->source = 0u;
    transient->gainCode = 0u;
    transient->volume = 1.0f;
    transient->midiFreq = 0u;
    transient->burstLeft = 0u;
    transient->dataAddr = 0u;
    transient->frames = 0u;
    transient->generation = sampleMemory_getGeneration();
    transient->env = 0.0f;
}

/*
 * Change the transient type (S084 VX2 VX2-6.1).
 *
 * What:       clamps to TRANSIENT_TYPE_SELECTABLE - 1 and re-resolves with the
 *             retained request byte.
 * Why:        P1 review fix F2: a type round trip keeps the chosen entry.
 * Inputs:     type.  Outputs: type, waveform, kind, source.
 * Accessors:  IM_SPECIAL_TRANSIENT_TYPE writer.
 * Affiliates: transient_resolve().
 */
void transient_setType(TransientGenerator* transient, uint8_t type)
{
    if (!transient)
        return;
    transient->type = (type < TRANSIENT_TYPE_SELECTABLE)
        ? type : (uint8_t)(TRANSIENT_TYPE_SELECTABLE - 1u);
    transient_resolve(transient);
}

/*
 * Change the transient wav byte (S084 VX2 VX2-6.1).
 *
 * What:       records the request byte and re-resolves the live value.
 * Inputs:     wave.  Outputs: waveRequest, waveform, kind, source.
 * Accessors:  IM_SPECIAL_TRANSIENT_WAVE writer.
 * Affiliates: transient_resolve().
 */
void transient_setWave(TransientGenerator* transient, uint8_t wave)
{
    if (!transient)
        return;
    transient->waveRequest = wave;
    transient_resolve(transient);
}

/*
 * Maximum wav byte for a type (S084 VX2 VX2-6.1).
 *
 * What:       trs -> 1; wav -> 12 + samples + 11 - 1; buf -> the seven
 * InsertEffects transient options.
 * Why:        the writers, the cell formatter and the Menu must agree.
 * Inputs:     type.  Outputs: maximum.
 * Accessors:  InstrumentCellFormat.c, the IM transient writers.
 */
uint8_t transient_waveMax(uint8_t type)
{
    uint16_t max_wave;

    switch (type) {
    case TRANSIENT_TYPE_TRS:
        return 1u;
    case TRANSIENT_TYPE_WAV:
        max_wave = (uint16_t)(TRANSIENT_STOCK_PCM_COUNT +
            sampleMemory_getNumSamples() + OSC_STOCK_WAVE_COUNT - 1u);
        return (max_wave > 255u) ? 255u : (uint8_t)max_wave;
    case TRANSIENT_TYPE_BUF:
        return (uint8_t)(INSERT_TRANSIENT_COUNT - 1u);
    default:
        return 0u;
    }
}

/*
 * Restart the transient (S084 VX2 VX2-6.2).
 *
 * What:       phase to the start, burst envelope to 1, burst length to
 *             50 ms. S084 VX2 review N2: a sine burst starts at
 *             phase 1024 << 20, the zero crossing of sine_table (the table
 *             holds -cos, so index 0 is -full scale and index 1024 is 0,
 *             rising); the former start at index 0 stepped from 0 to -32767
 *             on every trigger. The other bursts keep phase 0 (their table
 *             starts are the waveforms' own edges, a deliberate click).
 * Inputs:     transient (kind).  Outputs: phase, env, burstLeft.
 * Accessors:  the four engines' trigger functions.
 */
void transient_trigger(TransientGenerator* transient)
{
    if (!transient)
        return;
    transient->phase = (transient->kind == TRANSIENT_KIND_SINE)
        ? (1024u << 20) : 0u;
    transient->env = 1.0f;
    transient->burstLeft = TRANSIENT_BURST_SAMPLES;
}

/*
 * Render one transient block by kind (S084 VX2 VX2-6.2).
 *
 * What:       PCM: the original loop. SAMPLE: one-shot Q20.12 read with the
 *             catalogue gain, pitch from frq. Stock bursts: a 50 ms pitched
 *             read of the stock wave at f = 440 Hz x pitch, decaying with
 *             tau = 10 ms (-43 dB at the gate, D5). trs kinds output silence
 *             (their effect is applied by the engine).
 * Why:        plan §4.3.1, Q14 (bursts), Q15 (gain everywhere); cost cap
 *             = today's 22 instructions per sample, 0 divisions (D6).
 * Inputs:     transient state, buf, size.
 * Outputs:    buf; phase, env, burstLeft.
 * Accessors:  the four engines' render functions.
 * Affiliates: transient_resolve(), sampleMemory_gainFromCode(),
 *             stockWave_level(), osc_freqToTableIndex().
 *
 * S084 VX2 review fixes (F5, F9):
 * F9:   any catalogue generation change re-resolves a `wav`-type transient,
 *       not only a SAMPLE kind: the stock waves follow the samples in the
 *       list, so a changed sample count moves every burst entry too.
 * F5:   the SAMPLE loop reads one frame per sample (no interpolation): with
 *       interpolation it measured 29 instructions against the 22 cap, and
 *       D6 drops interpolation in that case (the PCM path is not
 *       interpolated either). The burst kinds no longer share one loop with
 *       a per-sample switch on the block-constant kind: each kind has its
 *       own loop (table: Sin/Tri/Saw/Rec/sq3..swa, with the sine as a
 *       4,096-point level; noise; crash), so each is specialised and
 *       measurable with fpseq.py. The table loop reads one point per
 *       sample: interpolated it measured 26 instructions, so D6 drops the
 *       interpolation there too (S084_P2_IMPLEMENTATION.md §17).
 */
void transient_calcBlock(TransientGenerator* transient, int16_t* buf,
                         const uint8_t size)
{
    uint8_t i;

    if (!transient || !buf)
        return;
    if (transient->type == TRANSIENT_TYPE_WAV &&
        transient->generation != sampleMemory_getGeneration())
        transient_resolve(transient);

    switch (transient->kind) {
    case TRANSIENT_KIND_NONE:
    case TRANSIENT_KIND_SNAP:
    case TRANSIENT_KIND_OFFSET:
    case TRANSIENT_KIND_BUFFER:
        memset(buf, 0, (size_t)size * sizeof(int16_t));
        return;

    case TRANSIENT_KIND_PCM:
        for (i = 0u; i < size; i++) {
            uint32_t phase = transient->phase >> 20;

            buf[i] = transient->volume *
                (transientData[transient->source][phase] << 8) *
                (phase < TRANSIENT_SAMPLE_LENGTH);
            transient->phase +=
                (transient->phase < TRANSIENT_PCM_END_PHASE) *
                (uint32_t)(transient->pitch * (1 << 20));
        }
        return;

    case TRANSIENT_KIND_SAMPLE: {
        const int16_t *data =
            (const int16_t *)SAMPLE_FLASH_PTR(transient->dataAddr);
        const uint32_t inc = (uint32_t)(transient->pitch * 4096.0f);
        const uint32_t frames = transient->frames;
        const float gain =
            transient->volume * sampleMemory_gainFromCode(transient->gainCode);
        uint32_t phase = transient->phase;

        if (!data || frames == 0u) {
            memset(buf, 0, (size_t)size * sizeof(int16_t));
            return;
        }
        for (i = 0u; i < size; i++) {
            const uint32_t index = phase >> TRANSIENT_SAMPLE_SHIFT;

            if (index >= frames) {
                buf[i] = 0;         /* one-shot end: hold silence */
                continue;
            }
            buf[i] = (int16_t)((float)data[index] * gain);
            phase += inc;
        }
        transient->phase = phase;
        return; }

    default:
        break;
    }

    {
        const uint16_t burst = (size < transient->burstLeft)
            ? size : transient->burstLeft;
        const float f = TRANSIENT_BURST_BASE_HZ * transient->pitch;
        const uint32_t inc =
            (uint32_t)((f * 4294967296.0f) / REAL_FS);
        const float vol = transient->volume;
        uint32_t phase = transient->phase;
        float env = transient->env;

        switch (transient->kind) {
        case TRANSIENT_KIND_SINE:
        case TRANSIENT_KIND_TABLE: {
            const int16_t *tab = sine_table;
            uint8_t shift = 20u;            /* sine: 4,096 points */
            uint32_t mask = 4095u;

            if (transient->kind == TRANSIENT_KIND_TABLE) {
                uint8_t level = osc_freqToTableIndex(f);

                if (level > 10u)
                    level = 10u;
                if (transient->source >= 6u) {
                    const stock_level_t lv = stockWave_level(
                        (uint8_t)(transient->source - 6u), level);

                    tab = lv.tab;
                    shift = lv.shift;
                    mask = lv.mask;
                } else {
                    tab = (transient->source == 1u) ? triTable[level]
                        : (transient->source == 2u) ? sawTable[level]
                        : recTable[level];
                    shift = 22u;
                    mask = 1023u;
                }
            }
            (void)mask;
            for (i = 0u; i < burst; i++) {
                /* D6: one read per sample (the interpolated read measured
                ** 26 instructions against the 22 cap; S084 VX2 §17). The
                ** index needs no mask: phase >> shift < table length. */
                buf[i] = (int16_t)((float)tab[phase >> shift] * env * vol);
                env *= TRANSIENT_BURST_DECAY;
                phase += inc;
            }
            break; }
        case TRANSIENT_KIND_NOISE:
            for (i = 0u; i < burst; i++) {
                phase = phase * 1664525u + 1013904223u;
                buf[i] = (int16_t)((float)(int16_t)(phase >> 16) * env * vol);
                env *= TRANSIENT_BURST_DECAY;
            }
            break;
        case TRANSIENT_KIND_CRASH:
        default:
            for (i = 0u; i < burst; i++) {
                buf[i] = (int16_t)(((float)crashSample[phase >> 17] - 127.0f) *
                                   256.0f * env * vol);
                env *= TRANSIENT_BURST_DECAY;
                phase += inc;
            }
            break;
        }
        if (burst < size)
            memset(&buf[burst], 0,
                   (size_t)(size - burst) * sizeof(int16_t));
        transient->phase = phase;
        transient->env = env;
        transient->burstLeft = (uint16_t)(transient->burstLeft - burst);
    }
}
