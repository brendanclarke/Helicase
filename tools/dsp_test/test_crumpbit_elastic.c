/*
 * CrumpBit elastic seating (S084 VX3 VX3-11.2).
 *
 * What:       runs the real rate/division/compact/seat/effective-min helpers
 *             extracted from CrumpBitEffect.c. Why: Gate G1 needs a direct
 *             witness that shrink keeps the newest bytes and growth is lazy.
 * Inputs:     deterministic wrapped and contiguous byte rings. Outputs: OK /
 *             failure count.
 * Accessors:  make -C tools/dsp_test test_crumpbit_elastic.
 * Affiliates: CrumpBitEffect.c/.h, FxBuffer share geometry and StepScale.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CRUMPBIT_DELAY_MAX_SAMPLES 70573.0f
#define CRUMPBIT_BUFFER_MIN_BYTES 20608u
#define CRUMPBIT_BUFFER_BYTES 70592u
#define CRUMPBIT_RATE_LN_SPAN 4.3820266f
#define CRUMPBIT_SAMPLES_PER_TICK 27567.5f
#define CRUMPBIT_DIVISION_NONE 0xFFu
#define CRUMPBIT_PARAM_RATE 7u
#define STEP_SCALE_COUNT 128u

typedef struct {
    uint8_t *base;
    uint32_t offset;
    uint32_t bytes;
} fx_share_t;

typedef struct {
    uint32_t length;
    uint32_t write_pos;
    uint32_t valid;
    float delay;
    float mix;
    float feedback;
    float gain_l;
    float gain_r;
    float ac_x_l;
    float ac_x_r;
    float ac_y_l;
    float ac_y_r;
    uint8_t bit_off;
    uint8_t bit_invert;
    uint8_t mix_raw;
    uint8_t feedback_raw;
    uint8_t rate;
    uint8_t sync;
    uint8_t pan_raw;
    uint8_t primed;
    uint32_t pending_length;
    float xfade_wet;
    uint8_t xfade;
} CrumpBitRuntime;

static uint16_t stepScale_ticksQ8(uint8_t cc)
{
    return (uint16_t)(256u + (uint16_t)cc * 64u);
}

static unsigned failures;

static void check(int condition, const char *what)
{
    if (!condition) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

#include "build/gen/crumpbit_elastic_new.c"

static void fill_ring(int8_t *ring, uint32_t length)
{
    uint32_t i;

    for (i = 0u; i < length; i++)
        ring[i] = (int8_t)((i * 13u + 7u) & 0x7Fu);
}

int main(void)
{
    static int8_t ring[CRUMPBIT_BUFFER_BYTES];
    fx_share_t share = { (uint8_t *)ring, 0u, CRUMPBIT_BUFFER_MIN_BYTES };
    CrumpBitRuntime rt;
    uint32_t keep = CRUMPBIT_BUFFER_MIN_BYTES - 2u;
    uint32_t i;

    memset(&rt, 0, sizeof(rt));
    fill_ring(ring, CRUMPBIT_BUFFER_BYTES);
    rt.length = CRUMPBIT_BUFFER_BYTES;
    rt.write_pos = 60000u;
    rt.valid = rt.write_pos;
    for (i = 0u; i < keep; i++)
        ring[i] = 0;
    crumpBit_seat(&rt, &share);
    check(rt.length == CRUMPBIT_BUFFER_MIN_BYTES, "shrink length");
    check(rt.write_pos == keep && rt.valid == keep, "shrink keeps newest count");
    for (i = 0u; i < keep; i++)
        check(ring[i] == (int8_t)(((60000u - keep + i) * 13u + 7u) & 0x7Fu),
              "contiguous shrink keeps newest bytes");

    share.bytes = CRUMPBIT_BUFFER_BYTES;
    crumpBit_seat(&rt, &share);
    check(rt.length == CRUMPBIT_BUFFER_MIN_BYTES &&
          rt.pending_length == CRUMPBIT_BUFFER_BYTES,
          "growth waits for ring wrap");
    check(crumpBit_effectiveMin(CRUMPBIT_PARAM_RATE, &share) == 0u,
          "preferred share reaches Rate zero");
    check(crumpBit_effectiveMin(CRUMPBIT_PARAM_RATE, &((fx_share_t){
              (uint8_t *)ring, 0u, CRUMPBIT_BUFFER_MIN_BYTES })) > 0u,
          "minimum share raises effective Rate");
    check(crumpBit_syncDivision(0u, 120u, CRUMPBIT_BUFFER_MIN_BYTES) < 128u,
          "Sync division fits share");
    check(crumpBit_rateSamples(0u) > crumpBit_rateSamples(127u),
          "Rate curve is monotonic");

    if (failures == 0u)
        printf("crumpbit_elastic OK (0 failures)\n");
    return failures != 0u;
}
