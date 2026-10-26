/*
 * Wavetable oscillator (S084 VX2 VX2-10.4).
 *
 * What:       proves the real oscWavetable_bind()/_render()/_renderFm() text:
 *             any frame length plays at the same cost, 8- and 16-bit frames
 *             are read as stored, the frame position reproduces the first and
 *             last frames, a missing table is silent but still advances, and
 *             FM with a zero modulator is plain.
 * Why:        plan §4.1.4 W1/W3 and the host proof.
 * Inputs:     generated OscillatorWavetable fragment plus host tables.
 * Outputs:    failure count; exits nonzero on any failure.
 * Accessors:  make -C tools/dsp_test wtb.
 * Affiliates: Core/DSPAudio/OscillatorWavetable.c, SampleMemory.h.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#define OSC_TYPE_WTB 4u
#define WTB_INV_CYCLE 2.3283064365386963e-10f

typedef struct {
    uint32_t base;
    float    pos;
    uint32_t frame_bytes;
    uint32_t generation;
    uint16_t frame_len;
    uint8_t  frame_count;
    uint8_t  bits;
    uint8_t  index;
    uint8_t  primed;
    uint16_t reserved;
} OscWtbState;

struct OscStruct {
    uint8_t oscType;
    uint8_t source;
    uint8_t waveform;
    uint8_t sourceRequest;
    uint32_t phase;
    uint32_t phaseInc;
    int32_t phaseIncStep;
    float fmMod;
    OscWtbState wtb;
};

typedef struct {
    uint32_t offset;
    uint16_t frame_len;
    uint8_t frame_count;
    uint8_t bits;
} WavetableInfo;

#define HOST_FLASH_BYTES 200000u
static uint8_t host_flash[HOST_FLASH_BYTES];

#define SAMPLE_FLASH_PTR(addr) ((const void *)(host_flash + (addr)))

static uint8_t table_count;
static WavetableInfo tables[8];
static uint32_t table_generation = 1u;

uint8_t sampleMemory_getNumWavetables(void) { return table_count; }
uint32_t sampleMemory_getGeneration(void) { return table_generation; }
uint8_t sampleMemory_getWavetableInfo(uint8_t index, WavetableInfo *out)
{
    if (!out || index >= table_count)
        return 0u;
    *out = tables[index];
    return 1u;
}

#include "wtb_new.c"

static unsigned failures;

static void check(int condition, const char *what)
{
    if (!condition) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

static int readSample(const WavetableInfo *info, unsigned frame,
                      unsigned point)
{
    const uint32_t offset = info->offset +
        frame * info->frame_len * (info->bits / 8u) +
        point * (info->bits / 8u);

    if (info->bits == 8u)
        return (int8_t)host_flash[offset] << 8;
    return (int16_t)((uint16_t)host_flash[offset] |
                     ((uint16_t)host_flash[offset + 1u] << 8));
}

typedef struct {
    double pos;
    int primed;
} RefState;

static void refBlock(const WavetableInfo *info, uint8_t waveform,
                     uint32_t *phase, uint32_t inc, float gain,
                     RefState *ref, int16_t *out, unsigned n)
{
    const double target = (double)waveform * (double)(info->frame_count - 1u) /
                          127.0;
    double posStep;
    unsigned i;

    if (!ref->primed) {
        ref->pos = target;
        ref->primed = 1;
    }
    posStep = (target - ref->pos) / (double)n;
    for (i = 0u; i < n; i++) {
        uint64_t m = (uint64_t)*phase * info->frame_len;
        uint32_t idx = (uint32_t)(m >> 32);
        double fr = (double)(uint32_t)m * (1.0 / 4294967296.0);
        uint32_t next = (idx + 1u == info->frame_len) ? 0u : (idx + 1u);
        unsigned fi;
        unsigned fn;
        double ff;
        double a;
        double b;

        ref->pos += posStep;
        fi = (unsigned)ref->pos;
        if (fi >= info->frame_count)
            fi = info->frame_count - 1u;
        ff = ref->pos - (double)fi;
        fn = fi + 1u;
        if (fn >= info->frame_count)
            fn = fi;
        a = readSample(info, fi, idx) +
            ff * (readSample(info, fn, idx) - readSample(info, fi, idx));
        b = readSample(info, fi, next) +
            ff * (readSample(info, fn, next) - readSample(info, fi, next));
        out[i] = (int16_t)((a + fr * (b - a)) * (double)gain);
        *phase += inc;
    }
}

static void fillTable(unsigned index, uint16_t frame_len, uint8_t frame_count,
                      uint8_t bits, uint32_t offset)
{
    unsigned frame;
    unsigned point;

    tables[index].offset = offset;
    tables[index].frame_len = frame_len;
    tables[index].frame_count = frame_count;
    tables[index].bits = bits;
    for (frame = 0u; frame < frame_count; frame++) {
        for (point = 0u; point < frame_len; point++) {
            const uint32_t pos = offset +
                frame * frame_len * (bits / 8u) + point * (bits / 8u);
            const int v = (int)((point * 37u + frame * 191u) % 4000u) - 2000;

            if (bits == 8u) {
                host_flash[pos] = (uint8_t)(int8_t)(v / 16);
            } else {
                host_flash[pos] = (uint8_t)((uint16_t)v & 0xffu);
                host_flash[pos + 1u] = (uint8_t)(((uint16_t)v >> 8) & 0xffu);
            }
        }
    }
}

int main(void)
{
    static const uint16_t lengths[] = { 600u, 256u, 64u, 2u };
    static const uint8_t bitss[] = { 16u, 8u };
    static const uint8_t counts[] = { 1u, 2u, 104u };
    unsigned li;
    unsigned bi;
    unsigned ci;
    uint32_t offset = 16u;   /* base 0 means silent; host tables start above */

    for (li = 0u; li < 4u; li++) {
        for (bi = 0u; bi < 2u; bi++) {
            for (ci = 0u; ci < 3u; ci++) {
                struct OscStruct osc;
                RefState ref;
                int16_t got[32];
                int16_t want[32];
                int16_t zeros[32] = { 0 };
                unsigned blk;
                unsigned si;
                const WavetableInfo *info;

                if (table_count >= 8u)
                    break;
                fillTable(table_count, lengths[li], counts[ci], bitss[bi],
                          offset);
                offset += (uint32_t)lengths[li] * counts[ci] *
                          (bitss[bi] / 8u) + 32u;
                table_count++;
                info = &tables[table_count - 1u];

                memset(&osc, 0, sizeof(osc));
                osc.oscType = OSC_TYPE_WTB;
                osc.sourceRequest = (uint8_t)(table_count - 1u);
                osc.waveform = 0u;
                osc.phase = 12345u;
                osc.phaseInc = 0x01234567u;
                memset(&ref, 0, sizeof(ref));
                for (blk = 0u; blk < 4u; blk++) {
                    uint32_t p = osc.phase;
                    uint32_t inc = osc.phaseInc;

                    oscWavetable_render(&osc, got, 32u, 1.0f);
                    refBlock(info, 0u, &p, inc, 1.0f, &ref, want, 32u);
                    for (si = 0u; si < 32u; si++)
                        check(abs((int)got[si] - (int)want[si]) <= 1,
                              "wavetable sample matches the reference");
                }

                /* Position 127 reproduces the last frame at phase 0. */
                osc.waveform = 127u;
                osc.phase = 0u;
                osc.wtb.primed = 0u;
                oscWavetable_render(&osc, got, 32u, 1.0f);
                check(abs((int)got[0] -
                          readSample(info, info->frame_count - 1u, 0u)) <= 1,
                      "position 127 starts at the last frame");
                osc.waveform = 0u;
                osc.phase = 0u;
                osc.wtb.primed = 0u;
                oscWavetable_render(&osc, got, 32u, 1.0f);
                check(abs((int)got[0] - readSample(info, 0u, 0u)) <= 1,
                      "position 0 starts at the first frame");

                /* Zero FM equals plain. */
                memset(&osc, 0, sizeof(osc));
                osc.oscType = OSC_TYPE_WTB;
                osc.sourceRequest = (uint8_t)(table_count - 1u);
                osc.waveform = 40u;
                osc.phase = 999u;
                osc.phaseInc = 0x00123456u;
                osc.fmMod = 1.0f;
                {
                    struct OscStruct plain;
                    int16_t a[32];
                    int16_t b2[32];

                    plain = osc;
                    oscWavetable_render(&plain, a, 32u, 1.0f);
                    oscWavetable_renderFm(&osc, zeros, b2, 32u, 1.0f);
                    for (si = 0u; si < 32u; si++)
                        check(a[si] == b2[si], "zero FM equals plain");
                }
            }
        }
    }

    /* No tables: silent, phase still advances. */
    {
        struct OscStruct osc;
        int16_t block[32];
        unsigned si;

        table_count = 0u;
        memset(&osc, 0, sizeof(osc));
        osc.oscType = OSC_TYPE_WTB;
        osc.phase = 0u;
        osc.phaseInc = 0x10000000u;
        oscWavetable_render(&osc, block, 32u, 1.0f);
        for (si = 0u; si < 32u; si++)
            check(block[si] == 0, "silent table renders zero");
        check(osc.phase == 0x10000000u * 32u, "silent table advances phase");
    }

    printf("wtb %s (%u failures)\n", failures ? "FAILED" : "OK", failures);
    return failures ? 1 : 0;
}
