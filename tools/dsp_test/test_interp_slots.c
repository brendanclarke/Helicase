/*
 * Sticky interpolation-slot host gate (S087 stage E, E-11).
 *
 * What:       the real modNode_interpClaim(), modNode_setWaveInterpEnabled()
 *             and modNode_resetTargets().
 *             Laws:
 *               1. at most two grants per block;
 *               2. a holder keeps its slot even when newcomers claim first;
 *               3. a holder that stops claiming frees the slot after one block;
 *               4. the toggle off grants nothing.
 * Why:        S087 P0 A7/A8: a wavetable blend must not flip between blending
 *             and stepping at the block rate.
 * Inputs:     gen/interp_slots.c.
 * Outputs:    "interp_slots failures: N"; exit 1 if N > 0.
 * Accessors:  make -C tools/dsp_test interp_slots.
 * Affiliates: modulationNode.c, OSC_WAVE_INTERP_MAX_ACTIVE.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>

#define REAL_FS ((float)44108.07291666667f)
#define OUTPUT_DMA_SIZE 32
#define OSC_WAVE_INTERP_MAX_ACTIVE 2u

#include "osc_sample_info.inc"

static uint8_t modNode_waveInterpEnabled = 0u;
static uint32_t modNode_waveInterpGeneration = 1u;
static uint8_t modNode_waveInterpActiveCount = 0u;
static uint8_t modNode_interpReserved = 0u;
static uint32_t modNode_interpLastGen = 0u;
static int velocityModulators[6];

typedef void (*instrument_runtime_lfo_node_visitor_t)(void*, void*, void*);
void instrumentManager_visitRuntimeLfoNodes(
    instrument_runtime_lfo_node_visitor_t cb, void* ctx)
{
    (void)cb; (void)ctx;
}
static void modNode_restoreTarget(void* t) { (void)t; }
static void modNode_restoreLfoPair(void* a, void* b, void* c)
{
    (void)a; (void)b; (void)c;
}

#include "interp_slots.c"

static int failures;

static void expect(int cond, const char *msg)
{
    if (!cond) {
        if (failures < 40)
            printf("FAIL: %s\n", msg);
        failures++;
    }
}

int main(void)
{
    OscInfo a, b, c;

    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));
    memset(&c, 0, sizeof(c));
    modNode_setWaveInterpEnabled(1u);
    modNode_resetTargets();

    /* Law 1: two grants, the third refused. */
    expect(modNode_interpClaim(&a) == 1u, "L1: first grant");
    expect(modNode_interpClaim(&b) == 1u, "L1: second grant");
    expect(modNode_interpClaim(&c) == 0u, "L1: third refused");

    /* Law 2: newcomers first in render order do not evict holders. */
    modNode_resetTargets();
    expect(modNode_interpClaim(&c) == 0u, "L2: newcomer refused while both reserved");
    expect(modNode_interpClaim(&a) == 1u, "L2: holder a keeps its slot");
    expect(modNode_interpClaim(&b) == 1u, "L2: holder b keeps its slot");

    /* Law 3: a holder that stops claiming frees its slot after one block. */
    modNode_resetTargets();          /* reserved = 2 */
    expect(modNode_interpClaim(&a) == 1u, "L3: a claims");
    modNode_resetTargets();          /* reserved = 1 (only a) */
    expect(modNode_interpClaim(&c) == 1u, "L3: a free slot is available");

    /* Law 4: the toggle off grants nothing. */
    modNode_setWaveInterpEnabled(0u);
    modNode_resetTargets();
    expect(modNode_interpClaim(&a) == 0u, "L4: off grants nothing");

    printf("interp_slots failures: %d\n", failures);
    return failures ? 1 : 0;
}
