/*
 * Catalogue-v2 and installer check (S085 C17.3).
 *
 * What:       runs the real SampleMemory.c and SampleInstall.c against a RAM
 *             image of sectors 7..11: EMPTY/STALE/V2 states, commit and
 *             reload, names/offsets/frames/gain, limits, fit, mixed frame
 *             formats, abort, short items, torn installs and entry bounds.
 * Why:        G1 gate for plan §5 item 8.
 * Inputs:     host_sampleflash_stub.c, prelude_cat.h.
 * Outputs:    "N failures"; exits nonzero on failure.
 * Accessors:  make -C tools/dsp_test catalogue.
 * Affiliates: SampleMemory.c, SampleInstall.c, sampleFlash.c.
 */
#include "prelude_cat.h"
#include "SampleMemory.h"
#include "SampleInstall.h"

uint8_t *host_cat_image;
static int failures;

#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL %s\n", msg); failures++; } \
} while (0)

static sample_install_fmt_t fmt16(void)
{
    sample_install_fmt_t f;

    memset(&f, 0, sizeof(f));
    f.channels = 1u;
    f.bits = 16u;
    f.rate = 44100u;
    f.block_align = 2u;
    f.stored_bits = 16u;
    return f;
}

static const char *name8(const char *s)
{
    static char buf[SAMPLE_DISPLAY_NAME_LEN];

    memset(buf, ' ', sizeof(buf));
    memcpy(buf, s, strlen(s) > sizeof(buf) ? sizeof(buf) : strlen(s));
    return buf;
}

static void install_one_sample(const char *name, int16_t peak)
{
    const sample_install_fmt_t f = fmt16();
    uint8_t data[8];
    const int16_t frames[4] = { peak, -peak, peak / 2, 0 };

    for (int i = 0; i < 4; i++) {
        data[2 * i] = (uint8_t)((uint16_t)frames[i] & 0xffu);
        data[2 * i + 1] = (uint8_t)((uint16_t)frames[i] >> 8);
    }
    CHECK(sampleInstall_beginSample(name8(name), &f, sizeof(data)) == 0,
          "begin sample");
    CHECK(sampleInstall_writeSampleData(data, sizeof(data)) == 0,
          "write sample");
    CHECK(sampleInstall_finishSample() == 0, "finish sample");
}

int main(void)
{
    sample_install_fmt_t f;
    char display[SAMPLE_DISPLAY_NAME_LEN + 1u];
    SampleInfo info;
    WavetableInfo tinfo;

    host_cat_image = malloc(1310720u);
    CHECK(host_cat_image != NULL, "allocate image");
    if (!host_cat_image)
        return 1;
    memset(host_cat_image, 0xff, 1310720u);
    sampleMemory_init();
    CHECK(sampleMemory_catalogueState() == SAMPLE_CATALOGUE_EMPTY,
          "erased image is EMPTY");

    /* A v1 count word reads STALE. */
    host_cat_image[0] = 3u;
    host_cat_image[1] = 0u;
    host_cat_image[2] = 0u;
    host_cat_image[3] = 0u;
    sampleMemory_refresh();
    CHECK(sampleMemory_catalogueState() == SAMPLE_CATALOGUE_STALE,
          "v1 count word is STALE");
    memset(host_cat_image, 0xff, 1310720u);
    sampleMemory_refresh();

    /* Install 3 samples and 2 wavetables. */
    CHECK(sampleInstall_begin() == 0, "begin install");
    CHECK(sampleInstall_writeStockBlock() == 0, "stock block");
    f = fmt16();
    install_one_sample("one", 16384);
    install_one_sample("two", -8192);
    install_one_sample("thr", 4096);
    CHECK(sampleInstall_sampleCount() == 3u, "three samples");

    CHECK(sampleInstall_beginWavetable(name8("wt1"), &f, 4u, 2u) == 0,
          "begin table 1");
    {
        uint8_t frame[8] = { 1, 0, 2, 0, 3, 0, 4, 0 };
        CHECK(sampleInstall_writeFrameData(frame, sizeof(frame)) == 0,
              "frame 1");
        CHECK(sampleInstall_writeFrameData(frame, sizeof(frame)) == 0,
              "frame 2");
        CHECK(sampleInstall_finishWavetable() == 0, "finish table 1");
    }
    CHECK(sampleInstall_beginWavetable(name8("wt2"), &f, 4u, 1u) == 0,
          "begin table 2");
    {
        uint8_t frame[8] = { 5, 0, 6, 0, 7, 0, 8, 0 };
        CHECK(sampleInstall_writeFrameData(frame, sizeof(frame)) == 0,
              "table 2 frame");
        CHECK(sampleInstall_finishWavetable() == 0, "finish table 2");
    }
    CHECK(sampleInstall_commit() == 0, "commit");
    sampleMemory_refresh();
    CHECK(sampleMemory_catalogueState() == SAMPLE_CATALOGUE_V2, "V2 after commit");
    CHECK(sampleMemory_getNumSamples() == 3u, "reload sample count");
    CHECK(sampleMemory_getNumWavetables() == 2u, "reload table count");
    CHECK(sampleMemory_stockValid() != 0u, "stock flag");

    info = sampleMemory_getSampleInfo(0u);
    sampleMemory_getDisplayName(0u, display);
    CHECK(strncmp(display, "one", 3) == 0, "sample name");
    CHECK(info.size == 4u, "sample frames");
    CHECK(info.gain_code == 23u, "gain code for peak 16384");
    CHECK(info.offset >= SAMPLE_DATA_START_ADDRESS &&
          info.offset < SAMPLE_CATALOGUE_START_ADDRESS, "sample offset");

    CHECK(sampleMemory_getWavetableInfo(0u, &tinfo) != 0u, "table info");
    CHECK(tinfo.frame_len == 4u && tinfo.frame_count == 2u &&
          tinfo.bits == 16u, "table geometry");
    sampleMemory_getWavetableName(1u, display);
    CHECK(strncmp(display, "wt2", 3) == 0, "table name");

    /* Limits, fit, short item and abort-then-continue. */
    CHECK(sampleInstall_begin() == 0, "second install");
    CHECK(sampleInstall_beginSample(name8("x"), &f, 3000000u) == -3,
          "oversize sample refused");
    CHECK(sampleInstall_beginSample(name8("x"), &f, 8u) == 0, "short begin");
    {
        uint8_t half[4] = { 0, 0, 0, 0 };
        CHECK(sampleInstall_writeSampleData(half, sizeof(half)) == 0,
              "short write");
        CHECK(sampleInstall_finishSample() == -4, "short sample refused");
    }
    CHECK(sampleInstall_beginSample(name8("x"), &f, 8u) == 0, "abort begin");
    sampleInstall_abortItem();
    install_one_sample("ok", 1);
    for (int i = 1; i < 128; i++)
        install_one_sample("lim", 1);
    CHECK(sampleInstall_beginSample(name8("x"), &f, 8u) == -2,
          "129th sample refused");

    /* Mixed per-frame formats: mono16, stereo16 and mono24. */
    CHECK(sampleInstall_beginWavetable(name8("mix"), &f, 4u, 3u) == 0,
          "begin mixed table");
    {
        uint8_t mono16[8] = { 1, 0, 2, 0, 3, 0, 4, 0 };
        uint8_t st16[16] = { 1, 0, 1, 0, 2, 0, 2, 0,
                             3, 0, 3, 0, 4, 0, 4, 0 };
        sample_install_fmt_t sf = f;
        sample_install_fmt_t s24 = f;

        sf.channels = 2u;
        sf.block_align = 4u;
        s24.bits = 24u;
        s24.block_align = 3u;
        CHECK(sampleInstall_writeFrameData(mono16, sizeof(mono16)) == 0,
              "mixed frame mono16");
        CHECK(sampleInstall_setFrameFormat(&sf) == 0, "stereo format");
        CHECK(sampleInstall_writeFrameData(st16, sizeof(st16)) == 0,
              "mixed frame stereo16");
        CHECK(sampleInstall_setFrameFormat(&s24) == 0, "24-bit format");
        CHECK(sampleInstall_writeFrameData(mono16, 12u) == 0,
              "mixed frame mono24");
        CHECK(sampleInstall_finishWavetable() == 0, "finish mixed table");
    }
    CHECK(sampleInstall_commit() == 0, "third commit");
    sampleMemory_refresh();
    CHECK(sampleMemory_getNumSamples() == 128u, "128 samples committed");
    CHECK(sampleMemory_getNumWavetables() == 1u, "mixed table installed");

    /* Torn install: entries without magic read EMPTY. */
    CHECK(sampleInstall_begin() == 0, "torn begin");
    install_one_sample("torn", 1);
    sampleMemory_refresh();
    CHECK(sampleMemory_catalogueState() == SAMPLE_CATALOGUE_EMPTY,
          "torn install is EMPTY");

    /* Entry bounds stop the count. */
    CHECK(sampleInstall_begin() == 0, "bounds begin");
    install_one_sample("bnd", 1);
    CHECK(sampleInstall_commit() == 0, "bounds commit");
    sampleMemory_refresh();
    {
        sample_catalogue_sample_t *e =
            (sample_catalogue_sample_t *)SAMPLE_FLASH_PTR(
                SAMPLE_CATALOGUE_SAMPLES_ADDRESS);

        e->offset = SAMPLE_CATALOGUE_START_ADDRESS;
        sampleMemory_refresh();
        CHECK(sampleMemory_getNumSamples() == 0u,
              "entry outside the item region stops the count");
    }

    free(host_cat_image);
    printf("%d failures\n", failures);
    return failures ? 1 : 0;
}
