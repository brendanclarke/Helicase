/*
 * Catalogue v2 host gate (S087 stage C).
 *
 * What:       runs the real SampleMemory.c and SampleInstall.c over a RAM
 *             image of sectors 7-11 and checks the stored catalogue.
 *             Laws:
 *               1. entry sizes/offsets: a sample entry names its offset,
 *                  frames, loop region and a gain code;
 *               2. the two zero frames are present after every sample;
 *               3. the magic is programmed last: an aborted install reads
 *                  EMPTY, an erased image reads EMPTY, a v1 word reads STALE;
 *               4. a broken entry ends the count;
 *               5. STALE detection (same as law 3's v1 case);
 *               6. the gain code law: peak x 2^(code/24) <= 32767 and code+1
 *                  exceeds it;
 *               7. abortItem leaves no entry.
 * Why:        S087 P0 C-50; the catalogue is the source the oscillator bind
 *             reads, so a layout or counting mistake is a playback fault.
 *             Break proof: write the magic before the entries and the aborted
 *             install no longer reads EMPTY (run with -DBREAK_MAGIC_FIRST).
 * Inputs:     cat_host.h, catalogue_host.c, the real sources.
 * Outputs:    "catalogue failures: N"; exit 1 if N > 0.
 * Accessors:  make -C tools/dsp_test catalogue.
 * Affiliates: SampleMemory.h, SampleInstall.h, STORAGE_SRAM_MANIFEST.md.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "cat_host.h"
#include "SampleMemory.h"
#include "SampleInstall.h"
#include "sampleFlash.h"

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

static uint8_t *cat_base(void)
{
    return cat_host_image + (SAMPLE_CATALOGUE_START_ADDRESS - SAMPLE_ROM_START_ADDRESS);
}

static sample_catalogue_entry_t *entry_at(uint8_t index)
{
    return (sample_catalogue_entry_t *)(cat_base() +
        sizeof(sample_catalogue_header_t) + (uint32_t)index * SAMPLE_ENTRY_BYTES);
}

int main(void)
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

    printf("catalogue failures: %d\n", failures);
    return failures ? 1 : 0;
}
