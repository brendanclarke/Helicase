/*
 * Samples module host tests: catalogue v2, WAV install conversion and the
 * install order and names (S087 stage C, S087 P1).
 *
 * What:       runs the real SampleMemory.c and SampleInstall.c over a RAM
 *             image of sectors 7-11 (catalogue_host.c models NOR programming:
 *             bits only clear, a mismatch returns 3), plus the real natural-
 *             order helpers from storageTypes.c.
 *               catalogue  1. entry sizes/offsets: a sample entry names its
 *                             offset, frames, loop region and a gain code;
 *                          2. the two zero frames are present after every
 *                             sample;
 *                          3. the magic is programmed last: an uncommitted
 *                             install reads EMPTY, an erased image reads
 *                             EMPTY, a v1 word reads STALE;
 *                          4. a broken entry ends the count;
 *                          5. STALE detection (law 3's v1 case);
 *                          6. the gain code law: peak x 2^(code/24) <= 32767
 *                             and code+1 exceeds it;
 *                          7. abortItem leaves no entry.
 *               install    1. PCM 8/16/24-bit mono and stereo and extensible
 *                             PCM are accepted; float, 3+ channels and 32-bit
 *                             are rejected;
 *                          2. samples store 16-bit mono; 8- and 24-bit convert
 *                             and stereo averages;
 *                          3. an 8-bit wavetable frame stores 8-bit values
 *                             (signed off by 128).
 *               order      1. digits compare as numbers (w2 < w10);
 *                          2. case-insensitive text order;
 *                          3. a numeric prefix plus '_', '-' or ' ' is
 *                             stripped, the extension dropped when asked, the
 *                             name padded to 8 characters.
 * Why:        the catalogue is what the oscillator bind reads; a layout,
 *             count or conversion mistake is a playback fault (S087 P0 C-50,
 *             A15, §5.2). Break proof: the Makefile's samples rule builds a
 *             SampleInstall.c that publishes the header in begin(), and
 *             catalogue law 3 must fail.
 * Inputs:     cat_host.h, catalogue_host.c, build/gen/samples_order.c.
 * Outputs:    one line per failure (first 40) and one count per group;
 *             exit 1 on any failure.
 * Accessors:  make -C tools/dsp_test samples.
 * Affiliates: SampleMemory.h, SampleInstall.h, storageTypes.c,
 *             tools/sampleoptimize.py natural_key(), STORAGE_SRAM_MANIFEST.md.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "cat_host.h"
#include "SampleMemory.h"
#include "SampleInstall.h"
#include "sampleFlash.h"
#include "samples_order.c"

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

static void group(const char *name, void (*run)(void))
{
    const int before = failures;

    run();
    printf("samples %-9s failures: %d\n", name, failures - before);
}

/* ========================================================= catalogue -- */
static uint8_t *cat_base(void)
{
    return cat_host_image + (SAMPLE_CATALOGUE_START_ADDRESS - SAMPLE_ROM_START_ADDRESS);
}

static sample_catalogue_entry_t *entry_at(uint8_t index)
{
    return (sample_catalogue_entry_t *)(cat_base() +
        sizeof(sample_catalogue_header_t) + (uint32_t)index * SAMPLE_ENTRY_BYTES);
}

static void run_catalogue(void)
{
    const sample_install_fmt_t fmt16 = { 1u, 1u, 44100u, 2u, 16u, 16u };
    int16_t data[4] = { 1000, -2000, 3000, -4000 };
    const sample_catalogue_entry_t *e0;
    const sample_catalogue_entry_t *e1;

    /* Erased image. */
    catalogue_host_reset();
    sampleMemory_refresh();
    expect(sampleMemory_catalogueState() == SAMPLE_CATALOGUE_EMPTY,
           "an erased image is EMPTY");
    expect(sampleMemory_getNumSamples() == 0u, "no samples on an empty image");

    /* A v1 install leaves the count word at the item base. */
    (void)sampleFlash_writeWord(SAMPLE_ROM_START_ADDRESS, 3u);
    sampleMemory_refresh();
    expect(sampleMemory_catalogueState() == SAMPLE_CATALOGUE_STALE,
           "law 5: a v1 count word reads STALE");

    /* Two samples. */
    catalogue_host_reset();
    sampleInstall_begin();
    expect(sampleInstall_beginSample("s01     ", 4u, &fmt16), "begin sample 1");
    expect(sampleInstall_writeFrames((const uint8_t *)data, 4u), "write sample 1");
    expect(sampleInstall_finishSample(0u, 0u, 0u), "finish sample 1");
    expect(sampleInstall_beginSample("s02     ", 2u, &fmt16), "begin sample 2");
    expect(sampleInstall_writeFrames((const uint8_t *)data, 2u), "write sample 2");
    expect(sampleInstall_finishSample(0u, 2u, 1u), "finish sample 2");
    expect(sampleInstall_commit(), "commit");
    sampleMemory_refresh();
    expect(sampleMemory_catalogueState() == SAMPLE_CATALOGUE_V2, "state V2");
    expect(sampleMemory_getNumSamples() == 2u, "two samples");

    e0 = sampleMemory_sampleEntry(0u);
    expect(e0 != NULL, "entry 0 exists");
    if (e0) {
        expect(e0->offset == SAMPLE_ROM_START_ADDRESS + 4u, "law 1: first offset");
        expect(e0->frames == 4u, "law 1: frames");
        expect(e0->loop_start == 0u && e0->loop_end == 4u,
               "law 1: whole sample loop without smpl");
        expect(e0->gain_code != 0u, "law 1: a gain code is stored");
        expect(memcmp(e0->name, "s01     ", 8u) == 0, "law 1: name");
        {
            const uint8_t *pad = cat_host_image +
                (e0->offset - SAMPLE_ROM_START_ADDRESS) + e0->frames * 2u;
            expect(pad[0] == 0u && pad[1] == 0u && pad[2] == 0u && pad[3] == 0u,
                   "law 2: two zero frames after the sample");
        }
    }
    e1 = sampleMemory_sampleEntry(1u);
    expect(e1 != NULL, "entry 1 exists");
    if (e1) {
        expect(e1->loop_start == 0u && e1->loop_end == 2u &&
               (e1->flags & 1u) != 0u, "law 1: smpl loop recorded");
        expect(e1->offset >= SAMPLE_ROM_START_ADDRESS + 16u,
               "law 1: second item after the pad");
    }

    /* Law 4: a broken entry ends the count. */
    if (e1) {
        entry_at(1u)->offset = 0x081FF000u;
        sampleMemory_refresh();
        expect(sampleMemory_getNumSamples() == 1u,
               "law 4: a broken entry ends the count");
    }

    /* Law 6: the gain code law over several peaks. */
    {
        static const uint32_t peaks[] = { 32767u, 16384u, 1000u, 100u, 1u };

        for (uint32_t i = 0u; i < sizeof(peaks) / sizeof(peaks[0]); i++) {
            uint8_t code = sampleInstall_gainCode(peaks[i]);
            float g = sampleMemory_gainFromCode(code);

            expect((float)peaks[i] * g <= 32767.0f,
                   "law 6: gain keeps the peak in range");
            if (code < 255u)
                expect((float)peaks[i] * sampleMemory_gainFromCode((uint8_t)(code + 1u)) >
                       32767.0f, "law 6: the next code would clip");
        }
        expect(sampleInstall_gainCode(32767u) == 0u, "law 6: full scale is code 0");
        expect(sampleInstall_gainCode(0u) == 0u, "law 6: silence is code 0");
    }

    /* Law 7: abortItem leaves no entry. */
    catalogue_host_reset();
    sampleInstall_begin();
    (void)sampleInstall_beginSample("bad     ", 4u, &fmt16);
    (void)sampleInstall_writeFrames((const uint8_t *)data, 4u);
    sampleInstall_abortItem();
    expect(sampleInstall_commit(), "commit after abort");
    sampleMemory_refresh();
    expect(sampleMemory_getNumSamples() == 0u, "law 7: no entry after abort");

    /* Law 3: an install that never commits reads EMPTY. */
    catalogue_host_reset();
    sampleInstall_begin();
    (void)sampleInstall_beginSample("part    ", 4u, &fmt16);
    (void)sampleInstall_writeFrames((const uint8_t *)data, 4u);
    sampleMemory_refresh();
    expect(sampleMemory_catalogueState() == SAMPLE_CATALOGUE_EMPTY,
           "law 3: an uncommitted install reads EMPTY");
}

/* =========================================================== install -- */
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

static void run_install(void)
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
}

/* ============================================================= order -- */
static void run_order(void)
{
    char name[8];

    expect(storage_naturalCompare("w2", "w10") < 0, "law 1: w2 < w10");
    expect(storage_naturalCompare("w10", "w2") > 0, "law 1: w10 > w2");
    expect(storage_naturalCompare("01_cut", "01_cut") == 0, "law 1: equal");
    expect(storage_naturalCompare("AKWF_hvoice_0001.wav",
                                  "AKWF_hvoice_0002.wav") < 0,
           "law 1: zero-padded frame order");
    expect(storage_naturalCompare("Saw", "saw") == 0, "law 2: case-insensitive");
    expect(storage_naturalCompare("abc", "abd") < 0, "law 2: text order");

    storage_displayNameFromLfn(name, "01_cut.wav", 1u);
    expect(memcmp(name, "cut     ", 8u) == 0, "law 3: prefix and extension");
    storage_displayNameFromLfn(name, "ak_hvox", 0u);
    expect(memcmp(name, "ak_hvox ", 8u) == 0, "law 3: table folder name");
    storage_displayNameFromLfn(name, "2-wave.wav", 1u);
    expect(memcmp(name, "wave    ", 8u) == 0, "law 3: dash prefix");
    storage_displayNameFromLfn(name, "3 wave.wav", 1u);
    expect(memcmp(name, "wave    ", 8u) == 0, "law 3: space prefix");
    storage_displayNameFromLfn(name, "longer_than_eight.wav", 1u);
    expect(memcmp(name, "longer_t", 8u) == 0, "law 3: truncation");
}

int main(void)
{
    group("catalogue", run_catalogue);
    group("install", run_install);
    group("order", run_order);
    printf("samples failures: %d\n", failures);
    return failures ? 1 : 0;
}
