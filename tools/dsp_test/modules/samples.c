/*
 * Samples module host tests: catalogue v3, WAV install conversion and the
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
 *               install    1. PCM 16/24-bit mono and stereo and extensible
 *                             PCM are accepted; 8-bit (S093 P8, D10), float,
 *                             3+ channels and 32-bit are rejected, for samples
 *                             and wavetables;
 *                          2. samples store 16-bit mono; 24-bit converts and
 *                             stereo averages;
 *                          3. a 24-bit stereo wavetable frame is stored as a
 *                             16-bit mono 1,024-point row (S093 P8, O1); an
 *                             8-bit table does not open.
 *               rows       (S093 P8) the stored 1,024-point rows:
 *                          1. a frame written in chunks counts once;
 *                          2. entries and offsets describe 2,048 B rows;
 *                          3. a 1,024-point frame is stored exactly;
 *                          4. 600-point frames retain passband harmonics and
 *                             remove aliases, including at the band edge;
 *                          5. a 2,048-point frame retains harmonic 100 and
 *                             removes harmonic 700;
 *                          6. capacity counts rows: 128 frames do not fit
 *                             after a full sample, 100 do;
 *                          7. v3 commits LXC3 / 3; a v2 header reads STALE.
 *               order      1. digits compare as numbers (w2 < w10);
 *                          2. case-insensitive text order;
 *                          3. a numeric prefix plus '_', '-' or ' ' is
 *                             stripped, the extension dropped when asked; up
 *                             to 8 characters kept, a longer name shortened
 *                             to its first 4 and last 4; padded to 8.
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
#include <math.h>

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
    expect(sampleMemory_catalogueState() == SAMPLE_CATALOGUE_V2, "state valid v3 catalogue");
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
    /* S093 P8 (D10): 8-bit is refused for every kind */
    expect(!sampleInstall_acceptFormat(fmt, 40u, SAMPLE_INSTALL_KIND_SAMPLE, &out),
           "law 1: PCM 8 sample rejected");
    expect(!sampleInstall_acceptFormat(fmt, 40u, SAMPLE_INSTALL_KIND_WAVETABLE, &out),
           "law 1: PCM 8 wavetable rejected");
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
    /* Law 2c: 16-bit stereo averages (S093 P8: was 8-bit). */
    make_fmt(fmt, 1u, 2u, 16u);
    (void)sampleInstall_acceptFormat(fmt, 40u, SAMPLE_INSTALL_KIND_SAMPLE, &out);
    {
        const int16_t src[4] = { 1000, 3000, -2000, -4001 };
        expect(sampleInstall_beginSample("p16st   ", 2u, &out), "begin 16 stereo");
        expect(sampleInstall_writeFrames((const uint8_t *)src, 2u), "write 16 stereo");
        expect(sampleInstall_finishSample(0u, 0u, 0u), "finish 16 stereo");
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
        expect(stored16(e->offset, 0u) == 2000, "law 2: stereo average");
        expect(stored16(e->offset, 1u) == -3000, "law 2: stereo average, negative");
    }

    /* Law 3 (S093 P8, O1): a 24-bit stereo wavetable frame is stored as a
     * 16-bit mono row (1,024 points, so stored exactly, no resampling); an
     * 8-bit table does not open (D10). */
    catalogue_host_reset();
    sampleInstall_begin();
    make_fmt(fmt, 1u, 2u, 24u);
    expect(sampleInstall_acceptFormat(fmt, 40u, SAMPLE_INSTALL_KIND_WAVETABLE, &out),
           "law 3: PCM 24 stereo wavetable accepted");
    {
        static uint8_t src[1024u * 6u];
        static int16_t fbuf[WAVETABLE_FRAME_MAX];
        const sample_install_fmt_t f8 = { 1u, 1u, 44100u, 1u, 8u, 16u };
        uint32_t i;

        /* point i: left high word i x 16, right high word 64 - i x 16, so
         * the mono average of the two is 32 at every point */
        for (i = 0u; i < 1024u; i++) {
            const int32_t l = (int32_t)(i * 16u) << 8;
            const int32_t r = -((int32_t)(i * 16u) << 8) + (64 << 8);

            src[i * 6u + 0u] = (uint8_t)(l & 0xff);
            src[i * 6u + 1u] = (uint8_t)((l >> 8) & 0xff);
            src[i * 6u + 2u] = (uint8_t)((l >> 16) & 0xff);
            src[i * 6u + 3u] = (uint8_t)(r & 0xff);
            src[i * 6u + 4u] = (uint8_t)((r >> 8) & 0xff);
            src[i * 6u + 5u] = (uint8_t)((r >> 16) & 0xff);
        }
        expect(!sampleInstall_beginWavetable("w8      ", 16u, 1u, &f8),
               "law 3: an 8-bit table does not open");
        expect(sampleInstall_beginWavetable("w24st   ", 1024u, 1u, &out),
               "begin 24-bit stereo table");
        expect(sampleInstall_writeFrame(src, 1024u, fbuf), "write 24-bit stereo frame");
        expect(sampleInstall_finishWavetable(), "finish 24-bit stereo table");
    }
    expect(sampleInstall_commit(), "commit table");
    sampleMemory_refresh();
    expect(sampleMemory_getNumWavetables() == 1u, "one wavetable");
    {
        const wavetable_catalogue_entry_t *w = sampleMemory_wavetableEntry(0u);

        expect(w != NULL, "table entry");
        if (w) {
            const int16_t *row = sampleMemory_wavetableRows(w);

            expect(w->frame_len == WAVETABLE_ROW_POINTS && w->frame_count == 1u &&
                   w->bits == 16u, "law 3: the row geometry");
            expect(row[0] == 32 && row[1023] == 32,
                   "law 3: 24-bit stereo -> 16-bit mono");
        }
    }
}

/* ============================================================== rows -- */
/* S093 P8 helpers: one frame of harmonics, and a harmonic's level in a
 * 1,024-point row (a single-bin DFT, dB re full scale 32767) */
static void make_frame(int16_t *x, int n, const int *h, const double *a, int k)
{
    int i, j;

    for (i = 0; i < n; i++) {
        double v = 0.0;

        for (j = 0; j < k; j++)
            v += a[j] * sin(6.283185307179586 * h[j] * i / n + 0.3 * j);
        x[i] = (int16_t)lrint(v);
    }
}

static double row_bin_db(const int16_t *row, int bin)
{
    double re = 0.0, im = 0.0;
    int i;

    for (i = 0; i < (int)WAVETABLE_ROW_POINTS; i++) {
        re += row[i] * cos(6.283185307179586 * bin * i / WAVETABLE_ROW_POINTS);
        im -= row[i] * sin(6.283185307179586 * bin * i / WAVETABLE_ROW_POINTS);
    }
    return 20.0 * log10(2.0 * sqrt(re * re + im * im) / WAVETABLE_ROW_POINTS / 32767.0 + 1e-12);
}

static void run_rows(void)
{
    const sample_install_fmt_t fw16 = { 1u, 1u, 44100u, 2u, 16u, 16u };
    static int16_t f600[3][600], f2048[2048], f1024[1024], fedge[600];
    static int16_t fbuf[WAVETABLE_FRAME_MAX];   /* the install's frame buffer */
    static const int h600[4] = { 1, 7, 50, 200 };
    static const double a600[4] = { 12000.0, 6000.0, 3000.0, 1500.0 };
    static const int h2048[2] = { 100, 700 };
    static const double a2048[2] = { 16000.0, 8000.0 };
    static const int h270[1] = { 270 };
    static const double a270[1] = { 16000.0 };
    const wavetable_catalogue_entry_t *w;
    const int16_t *rows;
    double worst_h = 0.0, worst_floor = -200.0;
    int f, i, b;

    for (f = 0; f < 3; f++)
        make_frame(f600[f], 600, h600, a600, 4);
    make_frame(f2048, 2048, h2048, a2048, 2);
    make_frame(fedge, 600, h270, a270, 1);
    for (i = 0; i < 1024; i++)
        f1024[i] = (int16_t)(i * 31 - 16000);
    catalogue_host_reset();
    sampleInstall_begin();
    /* table 0: three 600-point frames, each in three chunks (256 + 256 + 88) */
    expect(sampleInstall_beginWavetable("w600    ", 600u, 3u, &fw16), "rows: begin 600");
    for (f = 0; f < 3; f++) {
        expect(sampleInstall_writeFrame((const uint8_t *)&f600[f][0], 256u, fbuf), "rows: chunk 1");
        expect(sampleInstall_writeFrame((const uint8_t *)&f600[f][256], 256u, fbuf), "rows: chunk 2");
        expect(sampleInstall_writeFrame((const uint8_t *)&f600[f][512], 88u, fbuf), "rows: chunk 3");
    }
    expect(!sampleInstall_writeFrame((const uint8_t *)f600[0], 1u, fbuf),
           "rows law 1: a point past the opened geometry is refused");
    expect(sampleInstall_finishWavetable(), "rows: finish 600");
    /* table 1: one 2,048-point frame; 2: one 1,024-point frame; 3: the
     * band-edge frame */
    expect(sampleInstall_beginWavetable("w2048   ", 2048u, 1u, &fw16), "rows: begin 2048");
    expect(sampleInstall_writeFrame((const uint8_t *)f2048, 2048u, fbuf), "rows: write 2048");
    expect(sampleInstall_finishWavetable(), "rows: finish 2048");
    expect(sampleInstall_beginWavetable("w1024   ", 1024u, 1u, &fw16), "rows: begin 1024");
    expect(sampleInstall_writeFrame((const uint8_t *)f1024, 1024u, fbuf), "rows: write 1024");
    expect(sampleInstall_finishWavetable(), "rows: finish 1024");
    expect(sampleInstall_beginWavetable("wedge   ", 600u, 1u, &fw16), "rows: begin edge");
    expect(sampleInstall_writeFrame((const uint8_t *)fedge, 600u, fbuf), "rows: write edge");
    expect(sampleInstall_finishWavetable(), "rows: finish edge");
    expect(sampleInstall_commit(), "rows: commit");
    sampleMemory_refresh();
    expect(sampleMemory_getNumWavetables() == 4u, "rows: four tables");

    w = sampleMemory_wavetableEntry(0u);
    expect(w != NULL, "rows: table 0");
    if (w) {
        const wavetable_catalogue_entry_t *w1 = sampleMemory_wavetableEntry(1u);

        expect(w->frame_count == 3u, "rows law 1: chunked frames count once");
        expect(w->frame_len == WAVETABLE_ROW_POINTS && w->bits == 16u,
               "rows law 2: the entry describes 1,024-point 16-bit rows");
        rows = sampleMemory_wavetableRows(w);
        expect((const uint8_t *)rows == SAMPLE_FLASH_PTR(w->offset),
               "rows law 2: the rows are at the offset");
        expect(w1 && w1->offset == w->offset + 3u * WAVETABLE_ROW_BYTES,
               "rows law 2: the next item follows the 3 x 2,048 B");
        for (f = 0; f < 3; f++) {
            const int16_t *row = rows + f * WAVETABLE_ROW_POINTS;

            for (i = 0; i < 4; i++) {
                const double want = 20.0 * log10(a600[i] / 32767.0);
                const double got = row_bin_db(row, h600[i]);

                if (fabs(got - want) > worst_h) worst_h = fabs(got - want);
            }
            for (b = 0; b <= 512; b++) {
                if (b == 1 || b == 7 || b == 50 || b == 200)
                    continue;
                {
                    const double lv = row_bin_db(row, b);

                    if (lv > worst_floor) worst_floor = lv;
                }
            }
        }
        printf("rows: 600-point frames: harmonics within %.4f dB, other bins below %.1f dBFS\n",
               worst_h, worst_floor);
        expect(worst_h <= 0.05, "rows law 4: harmonic levels of a 600-point frame");
        expect(worst_floor <= -80.0, "rows law 4: no other content (images) in a 600-point frame");
    }
    w = sampleMemory_wavetableEntry(1u);
    if (w) {
        const int16_t *row = sampleMemory_wavetableRows(w);
        const double h100 = row_bin_db(row, 100) - 20.0 * log10(16000.0 / 32767.0);
        /* harmonic 700 of the frame folds to row bin 1024 - 700 = 324 if it
         * survives the kernel */
        const double h700 = row_bin_db(row, 324);

        printf("rows: 2,048-point frame: harmonic 100 %+.4f dB, harmonic 700 at %.1f dBFS\n",
               h100, h700);
        expect(fabs(h100) <= 0.05, "rows law 5: harmonic 100 of a 2,048-point frame");
        expect(h700 <= -80.0, "rows law 5: harmonic 700 removed");
    }
    w = sampleMemory_wavetableEntry(2u);
    if (w) {
        const int16_t *row = sampleMemory_wavetableRows(w);
        expect(memcmp(row, f1024, sizeof(f1024)) == 0,
               "rows law 3: a 1,024-point frame is stored exactly");
    }
    w = sampleMemory_wavetableEntry(3u);
    if (w) {
        const int16_t *row = sampleMemory_wavetableRows(w);
        const double lv = row_bin_db(row, 270) - 20.0 * log10(16000.0 / 32767.0);
        double img = -200.0;

        for (b = 0; b <= 512; b++)
            if (b != 270 && row_bin_db(row, b) > img)
                img = row_bin_db(row, b);
        printf("rows: band edge (harmonic 270 of 600): %+.4f dB, images %.1f dBFS\n", lv, img);
        expect(fabs(lv) <= 0.05, "rows law 4: harmonic 270 of a 600-point frame is flat");
        expect(img <= -80.0, "rows law 4: harmonic 270 leaves no image");
    }
    {
        const sample_catalogue_header_t *hd =
            (const sample_catalogue_header_t *)cat_base();

        expect(hd->magic == 0x3343584Cu && hd->version == 3u, "rows law 7: header LXC3 / 3");
    }

    /* law 6: fill the item region with one maximal sample, then a 128-frame
     * table: its rows (262,144 B) do not fit; 100 frames (204,800 B) do */
    catalogue_host_reset();
    sampleInstall_begin();
    {
        static int16_t big[4096];
        const sample_install_fmt_t fmt16 = { 1u, 1u, 44100u, 2u, 16u, 16u };
        uint32_t left = SAMPLE_MAX_FRAMES;

        expect(sampleInstall_beginSample("big     ", SAMPLE_MAX_FRAMES, &fmt16), "rows: begin big");
        while (left) {
            const uint32_t n = left < 4096u ? left : 4096u;

            (void)sampleInstall_writeFrames((const uint8_t *)big, n);
            left -= n;
        }
        expect(sampleInstall_finishSample(0u, 0u, 0u), "rows: finish big");
        printf("rows: %u B free after the big sample\n", (unsigned)sampleInstall_bytesFree());
        expect(!sampleInstall_beginWavetable("wfull   ", 16u, 128u, &fw16),
               "rows law 6: a table whose rows do not fit is refused");
        expect(sampleInstall_beginWavetable("wfit    ", 16u, 100u, &fw16),
               "rows law 6: a table whose rows fit is accepted");
        sampleInstall_abortItem();
    }

    /* law 7: a v2 header (the former magic, version 2) reads STALE */
    catalogue_host_reset();
    {
        uint8_t *hd = cat_base();
        const uint32_t m2 = 0x3243584Cu;
        const uint16_t v2 = 2u;

        memcpy(hd, &m2, 4u);
        memcpy(hd + 4, &v2, 2u);
        hd[6] = SAMPLE_ENTRY_BYTES;
        hd[7] = WAVETABLE_ENTRY_BYTES;
        cat_host_image[0] = 0u;     /* item region programmed: not EMPTY */
        sampleMemory_refresh();
        expect(sampleMemory_catalogueState() == SAMPLE_CATALOGUE_STALE,
               "rows law 7: a v2 catalogue reads STALE");
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
    expect(memcmp(name, "longight", 8u) == 0, "law 3: first 4 + last 4");
    storage_displayNameFromLfn(name, "kick_hard_01.wav", 1u);
    expect(memcmp(name, "kickd_01", 8u) == 0, "law 3: names differing at the end stay distinct");
}

int main(void)
{
    group("catalogue", run_catalogue);
    group("install", run_install);
    group("rows", run_rows);
    group("order", run_order);
    printf("samples failures: %d\n", failures);
    return failures ? 1 : 0;
}
