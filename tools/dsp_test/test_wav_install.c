/*
 * WAV format/conversion host gate (S087 stage C).
 *
 * What:       the real sampleInstall_acceptFormat() and the block converters
 *             (via beginSample/writeFrames and beginWavetable/writeFrame) on
 *             synthetic data.
 *             Laws:
 *               1. PCM 8/16/24-bit mono and stereo are accepted; the
 *                  extensible PCM form is accepted; float, 3+ channels and
 *                  32-bit are rejected;
 *               2. samples always store 16-bit mono; 8-bit and 24-bit convert
 *                  correctly and stereo averages;
 *               3. an 8-bit wavetable frame stores 8-bit values (signed off
 *                  by 128); a 16-bit table stores 16-bit values.
 * Why:        S087 P0 A15: the device converts depth and channels so most
 *             files install without the host tool. The smpl inclusive-end
 *             rule is exercised on hardware (the parser lives behind SD I/O).
 * Inputs:     cat_host.h, catalogue_host.c, the real sources.
 * Outputs:    "wav_install failures: N"; exit 1 if N > 0.
 * Accessors:  make -C tools/dsp_test wav_install.
 * Affiliates: SampleInstall.c, SampleMemory.h.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "cat_host.h"
#include "SampleMemory.h"
#include "SampleInstall.h"

void catalogue_host_reset(void);
extern uint8_t *cat_host_image;

static int failures;

static void expect(int cond, const char *msg)
{
    if (!cond) {
        if (failures < 40)
            printf("FAIL: %s\n", msg);
        failures++;
    }
}

static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xffu);
    p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xffu);
    p[1] = (uint8_t)((v >> 8) & 0xffu);
    p[2] = (uint8_t)((v >> 16) & 0xffu);
    p[3] = (uint8_t)((v >> 24) & 0xffu);
}

static void make_fmt(uint8_t fmt[40], uint16_t tag, uint16_t channels,
                     uint16_t bits)
{
    memset(fmt, 0, 40u);
    put16(fmt, tag);
    put16(fmt + 2u, channels);
    put32(fmt + 4u, 44100u);
    put32(fmt + 8u, 44100u * channels * (bits / 8u));
    put16(fmt + 12u, (uint16_t)(channels * (bits / 8u)));
    put16(fmt + 14u, bits);
}

static int16_t stored16(uint32_t item_offset, uint32_t frame)
{
    const uint8_t *p = cat_host_image + item_offset - SAMPLE_ROM_START_ADDRESS +
                       frame * 2u;
    return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

int main(void)
{
    uint8_t fmt[40];
    sample_install_fmt_t out;
    const sample_catalogue_entry_t *e;

    /* Law 1: format acceptance. */
    make_fmt(fmt, 1u, 1u, 16u);
    expect(sampleInstall_acceptFormat(fmt, 40u, SAMPLE_INSTALL_KIND_SAMPLE, &out),
           "law 1: PCM 16 mono");
    expect(out.stored_bits == 16u, "law 1: samples store 16 bits");
    make_fmt(fmt, 1u, 2u, 24u);
    expect(sampleInstall_acceptFormat(fmt, 40u, SAMPLE_INSTALL_KIND_SAMPLE, &out),
           "law 1: PCM 24 stereo");
    make_fmt(fmt, 1u, 2u, 8u);
    expect(sampleInstall_acceptFormat(fmt, 40u, SAMPLE_INSTALL_KIND_SAMPLE, &out),
           "law 1: PCM 8 stereo");
    make_fmt(fmt, 0xFFFEu, 1u, 16u);
    put16(fmt + 24u, 1u);
    expect(sampleInstall_acceptFormat(fmt, 40u, SAMPLE_INSTALL_KIND_SAMPLE, &out),
           "law 1: extensible PCM");
    make_fmt(fmt, 3u, 1u, 32u);
    expect(!sampleInstall_acceptFormat(fmt, 40u, SAMPLE_INSTALL_KIND_SAMPLE, &out),
           "law 1: float rejected");
    make_fmt(fmt, 1u, 3u, 16u);
    expect(!sampleInstall_acceptFormat(fmt, 40u, SAMPLE_INSTALL_KIND_SAMPLE, &out),
           "law 1: three channels rejected");
    make_fmt(fmt, 1u, 1u, 32u);
    expect(!sampleInstall_acceptFormat(fmt, 40u, SAMPLE_INSTALL_KIND_SAMPLE, &out),
           "law 1: 32-bit rejected");

    /* Law 2a: 16-bit mono passes through. */
    catalogue_host_reset();
    sampleInstall_begin();
    make_fmt(fmt, 1u, 1u, 16u);
    (void)sampleInstall_acceptFormat(fmt, 40u, SAMPLE_INSTALL_KIND_SAMPLE, &out);
    {
        int16_t src[2] = { -1234, 5678 };
        expect(sampleInstall_beginSample("p16     ", 2u, &out), "begin 16 mono");
        expect(sampleInstall_writeFrames((const uint8_t *)src, 2u), "write 16 mono");
        expect(sampleInstall_finishSample(0u, 0u, 0u), "finish 16 mono");
    }
    /* Law 2b: 24-bit mono keeps the high 16 bits. */
    make_fmt(fmt, 1u, 1u, 24u);
    (void)sampleInstall_acceptFormat(fmt, 40u, SAMPLE_INSTALL_KIND_SAMPLE, &out);
    {
        const uint8_t src[6] = { 0x56u, 0x34u, 0x12u, 0x00u, 0xFFu, 0xFFu };
        expect(sampleInstall_beginSample("p24     ", 2u, &out), "begin 24 mono");
        expect(sampleInstall_writeFrames(src, 2u), "write 24 mono");
        expect(sampleInstall_finishSample(0u, 0u, 0u), "finish 24 mono");
    }
    /* Law 2c: 8-bit stereo averages. */
    make_fmt(fmt, 1u, 2u, 8u);
    (void)sampleInstall_acceptFormat(fmt, 40u, SAMPLE_INSTALL_KIND_SAMPLE, &out);
    {
        const uint8_t src[8] = { 128u, 128u, 255u, 255u, 0u, 0u, 128u, 128u };
        expect(sampleInstall_beginSample("p8st    ", 2u, &out), "begin 8 stereo");
        expect(sampleInstall_writeFrames(src, 2u), "write 8 stereo");
        expect(sampleInstall_finishSample(0u, 0u, 0u), "finish 8 stereo");
    }
    expect(sampleInstall_commit(), "commit samples");
    sampleMemory_refresh();
    expect(sampleMemory_getNumSamples() == 3u, "three samples");
    e = sampleMemory_sampleEntry(0u);
    if (e) {
        expect(stored16(e->offset, 0u) == -1234, "law 2: 16-bit value 0");
        expect(stored16(e->offset, 1u) == 5678, "law 2: 16-bit value 1");
    }
    e = sampleMemory_sampleEntry(1u);
    if (e) {
        expect(stored16(e->offset, 0u) == 0x1234, "law 2: 24-bit high word");
        expect(stored16(e->offset, 1u) == -1, "law 2: 24-bit negative");
    }
    e = sampleMemory_sampleEntry(2u);
    if (e) {
        expect(stored16(e->offset, 0u) == 0, "law 2: 8-bit stereo average");
        expect(stored16(e->offset, 1u) == 32512, "law 2: 8-bit stereo peak");
    }

    /* Law 3: wavetable depth. */
    catalogue_host_reset();
    sampleInstall_begin();
    make_fmt(fmt, 1u, 1u, 8u);
    (void)sampleInstall_acceptFormat(fmt, 40u, SAMPLE_INSTALL_KIND_WAVETABLE, &out);
    {
        const uint8_t src[16] = { 128u, 255u };
        expect(sampleInstall_beginWavetable("w8      ", 16u, 1u, 8u, &out),
               "begin 8-bit table");
        expect(sampleInstall_writeFrame(src, 16u), "write 8-bit frame");
        expect(sampleInstall_finishWavetable(), "finish 8-bit table");
    }
    expect(sampleInstall_commit(), "commit table");
    sampleMemory_refresh();
    expect(sampleMemory_getNumWavetables() == 1u, "one wavetable");
    {
        const wavetable_catalogue_entry_t *w = sampleMemory_wavetableEntry(0u);

        expect(w != NULL, "table entry");
        if (w) {
            const uint8_t *p = cat_host_image + w->offset - SAMPLE_ROM_START_ADDRESS;
            expect(w->frame_len == 16u && w->frame_count == 1u && w->bits == 8u,
                   "law 3: table geometry");
            expect(p[0] == 0u && p[1] == 127u, "law 3: 8-bit values stay 8-bit");
        }
    }

    printf("wav_install failures: %d\n", failures);
    return failures ? 1 : 0;
}
