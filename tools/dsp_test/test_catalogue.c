/*
 * Catalogue v2 writer and reader (S084 VX2 VX2-10.2).
 *
 * What:       compiles the real SampleInstall.c and SampleMemory.c against a
 *             RAM image of sectors 7..11 and proves the accepted forms, the
 *             frame conversions, split writes, fit/count limits, magic-last
 *             commit, STALE detection and exact read-back.
 * Why:        the installer and catalogue are the foundation of P2.
 * Inputs:     none (all data is synthesised).
 * Outputs:    failure count; exits nonzero on any failure.
 * Accessors:  make -C tools/dsp_test catalogue.
 * Affiliates: Core/SampleRom/SampleInstall.c, SampleMemory.c.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#define HOST_SAMPLE_BASE 0x080C0000u
#define HOST_SAMPLE_END  0x08200000u
#define HOST_REGION_BYTES (HOST_SAMPLE_END - HOST_SAMPLE_BASE)
static uint8_t host_flash[HOST_REGION_BYTES];

#define SAMPLE_FLASH_PTR(addr) \
    ((const void *)(host_flash + ((addr) - HOST_SAMPLE_BASE)))

#include "SampleMemory.h"
#include "SampleInstall.c"
#include "SampleMemory.c"

/*
 * Pad an 8-character catalogue name from a C string (host test convenience).
 * The firmware writers receive the already padded filesystem display name.
 */
static const char *padName8(const char *src)
{
    static char out[8];
    unsigned i;

    memset(out, ' ', sizeof(out));
    if (src)
        for (i = 0u; i < 8u && src[i] != '\0'; i++)
            out[i] = src[i];
    return out;
}

#define sampleInstall_beginSample(name_, fmt_, bytes_) \
    sampleInstall_beginSample(padName8(name_), (fmt_), (bytes_))
#define sampleInstall_beginWavetable(name_, fmt_, len_, count_) \
    sampleInstall_beginWavetable(padName8(name_), (fmt_), (len_), (count_))

static unsigned failures;

static void check(int condition, const char *what)
{
    if (!condition) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

void sampleFlash_init(void) { }
void sampleFlash_lock(void) { }
void sampleFlash_invalidateRange(uint32_t address, uint32_t bytes)
{
    (void)address;
    (void)bytes;
}
int sampleFlash_eraseAllSamples(void)
{
    memset(host_flash, 0xff, sizeof(host_flash));
    return 0;
}
int sampleFlash_writeWord(uint32_t address, uint32_t data)
{
    const uint32_t offset = address - HOST_SAMPLE_BASE;
    uint32_t old;

    if ((address & 3u) != 0u || offset + 4u > sizeof(host_flash))
        return -1;
    old = (uint32_t)host_flash[offset] |
          ((uint32_t)host_flash[offset + 1u] << 8) |
          ((uint32_t)host_flash[offset + 2u] << 16) |
          ((uint32_t)host_flash[offset + 3u] << 24);
    if ((old & data) != data)
        return 3;   /* a one over a programmed zero, as on the chip */
    host_flash[offset] = (uint8_t)(data & 0xffu);
    host_flash[offset + 1u] = (uint8_t)((data >> 8) & 0xffu);
    host_flash[offset + 2u] = (uint8_t)((data >> 16) & 0xffu);
    host_flash[offset + 3u] = (uint8_t)((data >> 24) & 0xffu);
    return 0;
}
int sampleFlash_writeBytes(uint32_t *address, const uint8_t *data,
                           uint32_t bytes)
{
    uint32_t addr = *address;
    uint32_t i;

    for (i = 0u; i < bytes; i += 4u) {
        uint32_t word = 0xffffffffu;
        uint32_t k;

        for (k = 0u; k < 4u && i + k < bytes; k++)
            word = (word & ~(0xffu << (k * 8u))) |
                   ((uint32_t)data[i + k] << (k * 8u));
        {
            const int rc = sampleFlash_writeWord(addr, word);
            if (rc != 0)
                return rc;
        }
        addr += 4u;
    }
    *address = addr;
    return 0;
}
int stockWaveGen_programAll(uint32_t base)
{
    (void)base;
    return 0;
}

static void makeFmt(uint8_t *fmt, uint16_t channels, uint16_t bits,
                    uint32_t rate)
{
    memset(fmt, 0, 40u);
    fmt[0] = 1u; fmt[1] = 0u;                 /* PCM */
    fmt[2] = (uint8_t)(channels & 0xffu);
    fmt[3] = (uint8_t)(channels >> 8);
    fmt[4] = (uint8_t)(rate & 0xffu);
    fmt[5] = (uint8_t)((rate >> 8) & 0xffu);
    fmt[6] = (uint8_t)((rate >> 16) & 0xffu);
    fmt[7] = (uint8_t)((rate >> 24) & 0xffu);
    fmt[12] = (uint8_t)((channels * (bits / 8u)) & 0xffu);
    fmt[14] = (uint8_t)(bits & 0xffu);
    fmt[15] = (uint8_t)(bits >> 8);
}

static int16_t readLe16(uint32_t address)
{
    const uint32_t off = address - HOST_SAMPLE_BASE;

    return (int16_t)((uint16_t)host_flash[off] |
                     ((uint16_t)host_flash[off + 1u] << 8));
}

static void beginFresh(void)
{
    check(sampleInstall_begin() == 0, "erase succeeds");
    sampleMemory_refresh();
    check(sampleMemory_catalogueState() == SAMPLE_CATALOGUE_EMPTY,
          "erased flash is EMPTY");
}

static void sampleFormat(sample_install_fmt_t *fmt)
{
    uint8_t raw[40];

    makeFmt(raw, 1u, 16u, 44100u);
    check(sampleInstall_acceptFormat(raw, 16u, SAMPLE_INSTALL_KIND_SAMPLE,
                                     fmt) == 1u, "sample format accepted");
}

static void installSample(const int16_t *frames, uint32_t count,
                          const char *name)
{
    sample_install_fmt_t fmt;

    sampleFormat(&fmt);
    check(sampleInstall_beginSample(name, &fmt, count * 2u) == 0,
          "begin sample");
    check(sampleInstall_writeSampleData((const uint8_t *)frames,
                                        count * 2u) == 0, "write sample");
    check(sampleInstall_finishSample() == 0, "finish sample");
}

int main(void)
{
    static int16_t data[2400];
    static uint8_t snapshot[HOST_REGION_BYTES];
    uint8_t fmtbuf[40];
    sample_install_fmt_t fmt;
    uint32_t i;

    /* ---- 1. gain codes ------------------------------------------------ */
    beginFresh();
    for (i = 0u; i < 2400u; i++)
        data[i] = 0;
    data[0] = 32767;
    installSample(data, 1200u, "peak");
    data[0] = 16383;
    installSample(data, 1200u, "half");
    data[0] = 1000;
    installSample(data, 1200u, "quiet");
    data[0] = 0;
    installSample(data, 1200u, "zero");
    data[0] = 16384;
    installSample(data, 1200u, "round");
    check(sampleInstall_commit() == 0, "commit");
    check(sampleMemory_catalogueState() == SAMPLE_CATALOGUE_V2, "v2 state");
    check(sampleMemory_getNumSamples() == 5u, "five samples");
    check(sampleMemory_getSampleInfo(0u).gain_code == 0u, "32767 -> 0");
    check(sampleMemory_getSampleInfo(1u).gain_code == 24u, "16383 -> 24");
    check(sampleMemory_getSampleInfo(2u).gain_code == 120u, "1000 -> 120");
    check(sampleMemory_getSampleInfo(3u).gain_code == 0u, "0 -> 0");
    check(sampleMemory_getSampleInfo(4u).gain_code == 23u, "16384 -> 23");
    for (i = 0u; i < 5u; i++) {
        const SampleInfo info = sampleMemory_getSampleInfo((uint8_t)i);
        const float g = sampleMemory_gainFromCode(info.gain_code);
        const uint16_t peak = (uint16_t)((i == 0u) ? 32767u :
            (i == 1u) ? 16383u : (i == 2u) ? 1000u :
            (i == 3u) ? 0u : 16384u);

        check((double)g * (double)peak <= 32767.0,
              "gain never clips the peak");
    }
    {
        char name[9];
        sampleMemory_getDisplayName(0u, name);
        check(memcmp(name, "peak    ", 8u) == 0, "display name");
    }

    /* ---- 2. frame conversions ---------------------------------------- */
    beginFresh();
    makeFmt(fmtbuf, 2u, 24u, 48000u);
    check(sampleInstall_acceptFormat(fmtbuf, 40u, SAMPLE_INSTALL_KIND_FRAME,
                                     &fmt) == 1u, "stereo 24-bit frames");
    check(fmt.stored_bits == 16u && fmt.block_align == 6u,
          "frame geometry");
    {
        uint8_t frame[24];
        int32_t l;
        int32_t r;
        unsigned f;

        for (f = 0u; f < 4u; f++) {
            static const int32_t lv[4] = { 0x100000, -0x400000,
                                           0x100000, -0x800000 };
            static const int32_t rv[4] = { 0x100000, 0,
                                           -0x100000, 0 };

            l = lv[f]; r = rv[f];
            frame[f * 6u + 0u] = (uint8_t)(l & 0xff);
            frame[f * 6u + 1u] = (uint8_t)((l >> 8) & 0xff);
            frame[f * 6u + 2u] = (uint8_t)((l >> 16) & 0xff);
            frame[f * 6u + 3u] = (uint8_t)(r & 0xff);
            frame[f * 6u + 4u] = (uint8_t)((r >> 8) & 0xff);
            frame[f * 6u + 5u] = (uint8_t)((r >> 16) & 0xff);
        }

        check(sampleInstall_beginWavetable("st24", &fmt, 2u, 2u) == 0,
              "begin stereo 24-bit table");
        check(sampleInstall_writeFrameData(frame, 24u) == 0,
              "write stereo 24-bit frames");
        check(sampleInstall_finishWavetable() == 0, "finish table");
    }
    makeFmt(fmtbuf, 1u, 8u, 44100u);
    check(sampleInstall_acceptFormat(fmtbuf, 16u, SAMPLE_INSTALL_KIND_FRAME,
                                     &fmt) == 1u, "8-bit frames");
    {
        uint8_t bytes[4] = { 0x00u, 0xffu, 0x80u, 0x81u };

        check(sampleInstall_beginWavetable("u8", &fmt, 2u, 2u) == 0,
              "begin 8-bit table");
        check(sampleInstall_writeFrameData(bytes, 4u) == 0,
              "write 8-bit frames");
        check(sampleInstall_finishWavetable() == 0, "finish 8-bit table");
    }
    check(sampleInstall_commit() == 0, "commit frames");
    check(sampleMemory_getNumWavetables() == 2u, "two tables");
    {
        WavetableInfo info;
        uint32_t off;

        check(sampleMemory_getWavetableInfo(0u, &info) == 1u &&
              info.bits == 16u && info.frame_len == 2u &&
              info.frame_count == 2u, "16-bit table read back");
        check(readLe16(info.offset) == (int16_t)4096, "24-bit -> 16-bit");
        check(readLe16(info.offset + 2u) == (int16_t)-8192,
              "negative 24-bit rounded");
        check(readLe16(info.offset + 4u) == (int16_t)0,
              "stereo averaged");
        check(readLe16(info.offset + 6u) == (int16_t)-16384,
              "stereo average of -32768 and 0");
        check(sampleMemory_getWavetableInfo(1u, &info) == 1u &&
              info.bits == 8u, "8-bit table read back");
        off = info.offset - HOST_SAMPLE_BASE;
        check((int8_t)host_flash[off] == -128, "8-bit unsigned -> signed");
        check((int8_t)host_flash[off + 2u] == 0, "0x80 -> 0");
        check((int8_t)host_flash[off + 3u] == 1, "0x81 -> 1");
    }

    /* ---- 3. split writes --------------------------------------------- */
    {
        uint8_t sample[1000];
        uint32_t pos;

        beginFresh();
        for (i = 0u; i < sizeof(sample); i++)
            sample[i] = (uint8_t)((i * 37u) & 0xffu);
        sampleFormat(&fmt);
        check(sampleInstall_beginSample("split", &fmt, sizeof(sample)) == 0,
              "begin split");
        pos = 0u;
        while (pos < sizeof(sample)) {
            const uint32_t chunk = ((pos * 29u + 7u) % 517u) + 1u;
            const uint32_t n = (pos + chunk > sizeof(sample))
                ? (uint32_t)(sizeof(sample) - pos) : chunk;

            check(sampleInstall_writeSampleData(&sample[pos], n) == 0,
                  "split write");
            pos += n;
        }
        check(sampleInstall_finishSample() == 0, "finish split");
        check(sampleInstall_commit() == 0, "commit split");
        memcpy(snapshot, host_flash, sizeof(snapshot));

        beginFresh();
        check(sampleInstall_beginSample("split", &fmt, sizeof(sample)) == 0,
              "begin split whole");
        check(sampleInstall_writeSampleData(sample, sizeof(sample)) == 0,
              "write split whole");
        check(sampleInstall_finishSample() == 0, "finish split whole");
        check(sampleInstall_commit() == 0, "commit split whole");
        check(memcmp(snapshot, host_flash, sizeof(snapshot)) == 0,
              "split writes equal one write");
    }

    /* ---- 4. fit refusal ------------------------------------------------ */
    beginFresh();
    sampleFormat(&fmt);
    check(sampleInstall_beginSample("huge", &fmt,
            (SAMPLE_CATALOGUE_START_ADDRESS - SAMPLE_DATA_START_ADDRESS)) == -3,
          "oversized sample refused");
    check(sampleInstall_beginSample("tiny", &fmt, 4u) == 0,
          "next item still installs");
    check(sampleInstall_writeSampleData((const uint8_t *)data, 4u) == 0,
          "tiny write");
    check(sampleInstall_finishSample() == 0, "tiny finish");

    /* ---- 5. count limits ---------------------------------------------- */
    beginFresh();
    sampleFormat(&fmt);
    for (i = 0u; i < SAMPLE_MAX_COUNT; i++) {
        check(sampleInstall_beginSample("s", &fmt, 2u) == 0, "count begin");
        check(sampleInstall_writeSampleData((const uint8_t *)data, 2u) == 0,
              "count write");
        check(sampleInstall_finishSample() == 0, "count finish");
    }
    check(sampleInstall_beginSample("s", &fmt, 2u) == -2,
          "234th sample refused");
    makeFmt(fmtbuf, 1u, 8u, 44100u);
    check(sampleInstall_acceptFormat(fmtbuf, 16u, SAMPLE_INSTALL_KIND_FRAME,
                                     &fmt) == 1u, "limit frame fmt");
    for (i = 0u; i < WAVETABLE_MAX_COUNT; i++) {
        static const uint8_t two8[2] = { 0x80u, 0x90u };

        check(sampleInstall_beginWavetable("w", &fmt, 2u, 1u) == 0,
              "table count begin");
        /* S084 VX2 F8: the declared frame must be written before finish. */
        check(sampleInstall_writeFrameData(two8, 2u) == 0,
              "table count write");
        check(sampleInstall_finishWavetable() == 0, "table count finish");
    }
    check(sampleInstall_beginWavetable("w", &fmt, 2u, 1u) == -2,
          "129th table refused");

    /* ---- 5b. S084 VX2 review fixes F4, F7, F8 ------------------------- */
    {
        /*
         * F4: one table of three 2-point frames from three source forms
         * (mono 16-bit, stereo 16-bit, mono 24-bit). Every frame is decoded
         * with its own format through sampleInstall_setFrameFormat(), so the
         * stored table is exactly 1000, -1000 | 3000 (avg of 2000/4000),
         * -3000 (avg of -2000/-4000) | 5000, -5000.
         */
        sample_install_fmt_t f16m;
        sample_install_fmt_t f16s;
        sample_install_fmt_t f24m;
        static const uint8_t m16[4] = { 0xE8, 0x03, 0x18, 0xFC };   /* 1000, -1000 */
        static const uint8_t s16[8] = { 0xD0, 0x07, 0xA0, 0x0F,     /* 2000, 4000 */
                                        0x30, 0xF8, 0x60, 0xF0 };   /* -2000, -4000 */
        static const uint8_t m24[6] = { 0x00, 0x88, 0x13,           /* 5000 << 8 */
                                        0x00, 0x78, 0xEC };         /* -5000 << 8 */
        uint32_t table;

        beginFresh();
        makeFmt(fmtbuf, 1u, 16u, 48000u);
        check(sampleInstall_acceptFormat(fmtbuf, 16u, SAMPLE_INSTALL_KIND_FRAME,
                                         &f16m) == 1u, "F4 fmt m16");
        makeFmt(fmtbuf, 2u, 16u, 48000u);
        check(sampleInstall_acceptFormat(fmtbuf, 16u, SAMPLE_INSTALL_KIND_FRAME,
                                         &f16s) == 1u, "F4 fmt s16");
        makeFmt(fmtbuf, 1u, 24u, 48000u);
        check(sampleInstall_acceptFormat(fmtbuf, 16u, SAMPLE_INSTALL_KIND_FRAME,
                                         &f24m) == 1u, "F4 fmt m24");
        table = SAMPLE_DATA_START_ADDRESS;
        check(sampleInstall_beginWavetable("mix", &f16m, 2u, 3u) == 0,
              "F4 begin");
        check(sampleInstall_setFrameFormat(&f16m) == 0 &&
              sampleInstall_writeFrameData(m16, 4u) == 0, "F4 frame 1");
        check(sampleInstall_setFrameFormat(&f16s) == 0 &&
              sampleInstall_writeFrameData(s16, 8u) == 0, "F4 frame 2");
        check(sampleInstall_setFrameFormat(&f24m) == 0 &&
              sampleInstall_writeFrameData(m24, 6u) == 0, "F4 frame 3");
        check(sampleInstall_finishWavetable() == 0, "F4 finish");
        check(readLe16(table + 0u) == 1000 && readLe16(table + 2u) == -1000,
              "F4 mono 16-bit frame");
        check(readLe16(table + 4u) == 3000 && readLe16(table + 6u) == -3000,
              "F4 stereo frame decoded as stereo");
        check(readLe16(table + 8u) == 5000 && readLe16(table + 10u) == -5000,
              "F4 24-bit frame decoded as 24-bit");
        /* A different stored depth (8-bit) is refused for a 16-bit table. */
        makeFmt(fmtbuf, 1u, 8u, 48000u);
        {
            sample_install_fmt_t f8;

            check(sampleInstall_acceptFormat(fmtbuf, 16u,
                      SAMPLE_INSTALL_KIND_FRAME, &f8) == 1u, "F4 fmt 8");
            check(sampleInstall_beginWavetable("dep", &f16m, 2u, 1u) == 0,
                  "F4 depth begin");
            check(sampleInstall_setFrameFormat(&f8) == -1,
                  "F4 different stored depth refused");
            sampleInstall_abortItem();
        }

        /* F8: finishing with fewer bytes than declared aborts the item. */
        sampleFormat(&fmt);
        check(sampleInstall_beginSample("short", &fmt, 8u) == 0, "F8 begin");
        check(sampleInstall_writeSampleData((const uint8_t *)data, 4u) == 0,
              "F8 partial write");
        check(sampleInstall_finishSample() == -4, "F8 incomplete refused");
        /* F7: after an abort (or the F8 refusal) the next item installs. */
        check(sampleInstall_beginSample("next", &fmt, 2u) == 0,
              "F7 next item begins");
        check(sampleInstall_writeSampleData((const uint8_t *)data, 2u) == 0,
              "F7 next write");
        sampleInstall_abortItem();
        check(sampleInstall_beginSample("after", &fmt, 2u) == 0,
              "F7 begin after abort");
        check(sampleInstall_writeSampleData((const uint8_t *)data, 2u) == 0 &&
              sampleInstall_finishSample() == 0, "F7 finish after abort");
        check(sampleInstall_sampleCount() == 1u,
              "F7/F8 only the complete sample has an entry");
    }

    /* ---- 6. magic-last commit ---------------------------------------- */
    beginFresh();
    sampleFormat(&fmt);
    check(sampleInstall_beginSample("a", &fmt, 2u) == 0, "begin a");
    check(sampleInstall_writeSampleData((const uint8_t *)data, 2u) == 0,
          "write a");
    check(sampleInstall_finishSample() == 0, "finish a");
    check(sampleMemory_catalogueState() == SAMPLE_CATALOGUE_EMPTY,
          "no commit: EMPTY");
    check(sampleInstall_commit() == 0, "commit a");
    check(sampleMemory_catalogueState() == SAMPLE_CATALOGUE_V2,
          "commit: V2");
    check(sampleMemory_getNumSamples() == 1u, "one sample");

    /* ---- 7. v1 image reads STALE ------------------------------------- */
    memset(host_flash, 0xff, sizeof(host_flash));
    host_flash[0] = 7u;
    host_flash[1] = 0u;
    host_flash[2] = 0u;
    host_flash[3] = 0u;
    sampleMemory_refresh();
    check(sampleMemory_catalogueState() == SAMPLE_CATALOGUE_STALE,
          "v1 count word: STALE");

    /* ---- 8. corrupt entry ends the count ----------------------------- */
    beginFresh();
    sampleFormat(&fmt);
    check(sampleInstall_beginSample("a", &fmt, 2u) == 0, "begin corrupt a");
    check(sampleInstall_writeSampleData((const uint8_t *)data, 2u) == 0,
          "write corrupt a");
    check(sampleInstall_finishSample() == 0, "finish corrupt a");
    check(sampleInstall_beginSample("b", &fmt, 2u) == 0, "begin corrupt b");
    check(sampleInstall_writeSampleData((const uint8_t *)data, 2u) == 0,
          "write corrupt b");
    check(sampleInstall_finishSample() == 0, "finish corrupt b");
    check(sampleInstall_commit() == 0, "commit corrupt");
    check(sampleMemory_getNumSamples() == 2u, "two valid entries");
    {
        const uint32_t off = SAMPLE_CATALOGUE_SAMPLES_ADDRESS +
            sizeof(sample_catalogue_sample_t) - HOST_SAMPLE_BASE;

        host_flash[off] = 0x00u;
        sampleMemory_refresh();
        check(sampleMemory_getNumSamples() == 1u,
              "corrupt entry ends the count");
    }

    printf("catalogue %s (%u failures)\n", failures ? "FAILED" : "OK",
           failures);
    return failures ? 1 : 0;
}
