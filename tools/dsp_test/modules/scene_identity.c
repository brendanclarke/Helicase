/*
 * Scene switch identity host tests (S098 P1).
 *
 * What:       compiles the real preset_trackStepsMatch() and
 *             preset_instrumentImageMatches() from presetManager.c against a
 *             fake two-Scene Pattern store.
 *             Laws:
 *               1. the same steps stored at different pool offsets match;
 *               2. one changed block byte, a changed trigger bit, or a
 *                  block-less trigger against an empty step do not match;
 *               3. two instrument slots that differ only in
 *                  morph_interpolation[] match; a changed type, Normal or
 *                  Morph endpoint byte does not.
 * Why:        the switch keeps a track running only when it is identical
 *             (target T1). A false "identical" would let a changed track
 *             keep its old state; law 1 proves copies with a different pool
 *             layout still count as identical. Break proof: the Makefile
 *             rebuilds the fragment comparing whole address entries, and
 *             law 1 must fail.
 * Inputs:     build/gen/preset_identity.c.
 * Outputs:    one line per failure; "scene_identity failures: N"; exit 1 if
 *             N > 0.
 * Accessors:  make -C tools/dsp_test scene_identity.
 * Affiliates: presetManager.c, S098_P0_IMPLEMENTATION.md.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define NUM_STEPS               128u
#define PAT_RAW_BLOCK_MAX       136u
#define PAT_ADDR_TRIGGER_BIT    (1u << 15)
#define PAT_ADDR_SPECIALS_BIT   (1u << 14)
#define PAT_ADDR_OFFSET_MASK    0x3FFFu
#define PAT_ADDR_SENTINEL       0x3FFFu
#define INSTRUMENT_PARAM_COUNT  64u

typedef uint8_t instrument_param_value_t;
typedef enum { HOST_TYPE_DRM = 0, HOST_TYPE_COL = 6 } instrument_type_t;
typedef struct {
    instrument_param_value_t instrument_parameters[INSTRUMENT_PARAM_COUNT];
    instrument_param_value_t morph_instrument_parameters[INSTRUMENT_PARAM_COUNT];
    instrument_param_value_t morph_interpolation[INSTRUMENT_PARAM_COUNT];
} instrument_parameter_images_t;
typedef struct kit_instrument_slot {
    instrument_type_t type;
    instrument_parameter_images_t parameter_images;
} kit_instrument_slot_t;

/* Two Scenes, one track: an address entry and a byte count per step. */
static uint16_t host_entry[2][NUM_STEPS];
static uint8_t host_bytes[2][NUM_STEPS];
static uint8_t host_pool[2][8192];

static uint8_t pat_rawReadBlock(uint8_t scene_index, uint8_t track,
                                uint8_t step, uint8_t out[PAT_RAW_BLOCK_MAX],
                                uint16_t *entry_out)
{
    uint16_t entry = host_entry[scene_index][step];
    uint8_t bytes = host_bytes[scene_index][step];

    (void)track;
    if (entry_out)
        *entry_out = entry;
    if (bytes == 0u)
        return 0u;
    memcpy(out, &host_pool[scene_index][entry & PAT_ADDR_OFFSET_MASK], bytes);
    return bytes;
}

#include "preset_identity.c"

static void store_reset(void)
{
    uint8_t s;
    uint8_t step;

    for (s = 0u; s < 2u; s++)
        for (step = 0u; step < NUM_STEPS; step++) {
            host_entry[s][step] = PAT_ADDR_SENTINEL;
            host_bytes[s][step] = 0u;
        }
    memset(host_pool, 0, sizeof(host_pool));
}

static void store_put(uint8_t scene, uint8_t step, uint16_t offset,
                      const uint8_t *block, uint8_t bytes, uint8_t trigger)
{
    host_entry[scene][step] = (uint16_t)(offset | PAT_ADDR_SPECIALS_BIT |
                                         (trigger ? PAT_ADDR_TRIGGER_BIT : 0u));
    host_bytes[scene][step] = bytes;
    memcpy(&host_pool[scene][offset], block, bytes);
}

static void store_trigger_only(uint8_t scene, uint8_t step)
{
    host_entry[scene][step] = (uint16_t)(PAT_ADDR_SENTINEL |
                                         PAT_ADDR_TRIGGER_BIT);
    host_bytes[scene][step] = 0u;
}

static unsigned failures;

static void expect(int got, int want, const char *law)
{
    if (got != want) {
        failures++;
        printf("FAIL %s: got %d, want %d\n", law, got, want);
    }
}

static void same_two_scenes(void)
{
    static const uint8_t block[8] = { 0x03u, 0x01u, 0x80u, 0x45u,
                                      0x10u, 0x7Fu, 0x00u, 0x00u };

    store_reset();
    store_put(0u, 3u, 0u, block, sizeof(block), 1u);
    store_put(1u, 3u, 64u, block, sizeof(block), 1u);    /* other offset */
    store_trigger_only(0u, 10u);
    store_trigger_only(1u, 10u);
}

int main(void)
{
    kit_instrument_slot_t a;
    kit_instrument_slot_t b;
    uint8_t i;

    /* Law 1: different pool offsets, same content. */
    same_two_scenes();
    expect(preset_trackStepsMatch(0u, 0u, 1u), 1, "law 1 offsets");

    /* Law 2: a byte, a trigger bit, a block-less trigger. */
    same_two_scenes();
    host_pool[1][64 + 5] ^= 0x01u;
    expect(preset_trackStepsMatch(0u, 0u, 1u), 0, "law 2 byte");
    same_two_scenes();
    host_entry[1][3] &= (uint16_t)~PAT_ADDR_TRIGGER_BIT;
    expect(preset_trackStepsMatch(0u, 0u, 1u), 0, "law 2 trigger");
    same_two_scenes();
    store_trigger_only(0u, 20u);
    expect(preset_trackStepsMatch(0u, 0u, 1u), 0, "law 2 block-less");

    /* Law 3: the derived image is ignored, the endpoints are not. */
    memset(&a, 0, sizeof(a));
    for (i = 0u; i < INSTRUMENT_PARAM_COUNT; i++) {
        a.parameter_images.instrument_parameters[i] = i;
        a.parameter_images.morph_instrument_parameters[i] = (uint8_t)(127u - i);
        a.parameter_images.morph_interpolation[i] = (uint8_t)(i / 2u);
    }
    a.type = HOST_TYPE_COL;
    b = a;
    b.parameter_images.morph_interpolation[4] ^= 0x22u;
    expect(preset_instrumentImageMatches(&a, &b), 1, "law 3 interpolation");
    b = a;
    b.type = HOST_TYPE_DRM;
    expect(preset_instrumentImageMatches(&a, &b), 0, "law 3 type");
    b = a;
    b.parameter_images.instrument_parameters[7] ^= 0x01u;
    expect(preset_instrumentImageMatches(&a, &b), 0, "law 3 normal");
    b = a;
    b.parameter_images.morph_instrument_parameters[9] ^= 0x01u;
    expect(preset_instrumentImageMatches(&a, &b), 0, "law 3 morph");

    printf("scene_identity failures: %u\n", failures);
    return failures ? 1 : 0;
}
