/*
 * Octave-index comparison (S073 Step 6).
 *
 * What:       compares the frozen log2f()-based wavetable selector against
 *             the current ten-edge selector for every positive float in the
 *             requested 0.001..65,536 Hz interval.
 * Why:        Step 6 is approved only when any changed table choice is
 *             confined to a few ulps of an octave edge.
 * Inputs:     generated oct_old.c and oct_new.c from the Makefile target.
 * Outputs:    mismatch count, edge distance and maximum cent error.
 * Accessors:  make -C tools/dsp_test octave.
 * Affiliates: Oscillator.c freqToTableIndex() and osc_octaveEdgeHz[].
 */
#include <float.h>
#include <stdint.h>
#include <stdio.h>
#include <math.h>

#include "oct_old.c"
#include "oct_new.c"

static float bits_float(uint32_t bits)
{
    union { uint32_t u; float f; } value = { bits };
    return value.f;
}

static uint32_t float_bits(float value)
{
    union { uint32_t u; float f; } bits = { 0u };
    bits.f = value;
    return bits.u;
}

static uint32_t unsigned_distance(uint32_t a, uint32_t b)
{
    return a >= b ? a - b : b - a;
}

static unsigned check_special(float value, const char *name)
{
    const uint8_t old_index = old_freqToTableIndex(value);
    const uint8_t new_index = new_freqToTableIndex(value);

    if (old_index == new_index)
        return 0u;
    printf("special mismatch %s: old=%u new=%u\n",
           name, (unsigned)old_index, (unsigned)new_index);
    return 1u;
}

int main(void)
{
    const uint32_t first = 0x3A83126Fu;
    const uint32_t last = 0x47800000u;
    uint32_t mismatches = 0u;
    uint32_t max_ulps = 0u;
    double max_cents = 0.0;
    uint8_t k;

    for (uint32_t bits = first; bits <= last; ++bits) {
        const float frequency = bits_float(bits);
        const uint8_t old_index = old_freqToTableIndex(frequency);
        const uint8_t new_index = new_freqToTableIndex(frequency);
        if (old_index == new_index)
            continue;

        uint32_t edge_bits = 0u;
        uint32_t edge_ulps = UINT32_MAX;
        double edge_cents = DBL_MAX;
        for (k = 0u; k < 10u; ++k) {
            const uint32_t candidate_bits = float_bits(new_osc_octaveEdgeHz[k]);
            const uint32_t candidate_ulps = unsigned_distance(bits, candidate_bits);
            const double candidate_cents = fabs(1200.0 *
                    log2((double)frequency / (double)new_osc_octaveEdgeHz[k]));
            if (candidate_ulps < edge_ulps) {
                edge_ulps = candidate_ulps;
                edge_bits = candidate_bits;
                edge_cents = candidate_cents;
            }
        }
        ++mismatches;
        if (edge_ulps > max_ulps)
            max_ulps = edge_ulps;
        if (edge_cents > max_cents)
            max_cents = edge_cents;
        printf("mismatch bits=0x%08x f=%.9g old=%u new=%u "
               "edge=0x%08x ulps=%u cents=%.9g\n",
               bits, (double)frequency, (unsigned)old_index,
               (unsigned)new_index, edge_bits, edge_ulps, edge_cents);
    }

    mismatches += check_special(0.0f, "0");
    mismatches += check_special(-1.0f, "-1");
    mismatches += check_special(INFINITY, "+inf");
    mismatches += check_special(NAN, "NaN");

    printf("octave mismatches: %u, max edge distance: %u ulps, "
           "max error: %.9g cents\n", mismatches, max_ulps, max_cents);
    return max_ulps > 8u || mismatches > 16u ? 1 : 0;
}
