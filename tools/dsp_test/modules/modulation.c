/*
 * Modulation node module host tests: the sticky interpolation slots (S087
 * stage E, E-11).
 *
 * What:       the real modNode_interpClaim(), modNode_setWaveInterpEnabled()
 *             and modNode_resetTargets() (extract.py --func), over the real
 *             OscInfo (osc_host.h).
 *             Laws:
 *               1. at most two grants per block;
 *               2. a holder keeps its slot even when newcomers claim first;
 *               3. a holder that stops claiming frees the slot after one block;
 *               4. the toggle off grants nothing;
 *               5. (S087 P1, F11) both holders survive the 255-stamp wrap.
 *               6. (S088 P0 §3 3b) slots reserved for ongoing itp crossfades
 *                  are not granted to wavetable blending; two fades leave
 *                  none.
 *               A1-CTL (S101 P2 A1, the staggered slot control,
 *               modNode_controlBlock()):
 *                 1. from a forced start the full control runs on every
 *                    other block, and the LFO phase steps it passes add up
 *                    to the blocks elapsed (the rate is exact);
 *                 2. a velocity node whose base was refreshed in the gap is
 *                    re-applied in the very next block, and only that node;
 *                 3. a foreground restore (modNode_resetTargets()) makes the
 *                    next block run the full control with the steps it owes;
 *                 4. the dirty LFO sources are re-applied on every block in
 *                    between (instrumentManager_reapplyDirtyLfos()).
 *               Break (Makefile): the gap block ignoring the velocity dirty
 *               bits must fail A1-CTL 2; full runs always stepping 1 must
 *               fail A1-CTL 1.
 * Why:        S087 P0 A7/A8: a wavetable blend must not flip between blending
 *             and stepping at the block rate.
 * Inputs:     build/gen/modulation_interp.c, osc_host.h.
 * Outputs:    "modulation failures: N"; exit 1 if N > 0.
 * Accessors:  make -C tools/dsp_test modulation.
 * Affiliates: modulationNode.c, OSC_WAVE_INTERP_MAX_ACTIVE.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>

#include "osc_host.h"

static uint8_t modNode_waveInterpEnabled = 0u;
static uint32_t modNode_waveInterpGeneration = 1u;
static uint8_t modNode_waveInterpActiveCount = 0u;
static uint8_t modNode_interpReserved = 0u;
static uint8_t modNode_itpReserved = 0u;
static uint8_t modNode_interpLastStamp = 0u;

/* S101 P2 A1: the staggered control's stand-ins (counters per call). */
typedef struct { float lastVal; } ModulationNode;
static ModulationNode velocityModulators[6];
#include "modulation_ctl_macros.inc"
static uint8_t modNode_ctlState = MODNODE_CTL_FORCE;
static int ctl_full_runs, ctl_steps, ctl_reapply_lfos;
static int ctl_velo_updates[6], ctl_velo_restores[6];
static void modNode_updateValue(ModulationNode *vm, float v)
{
    (void)v;
    ctl_velo_updates[vm - velocityModulators]++;
}
static void modNode_reassignVeloMod(void)
{
    for (int i = 0; i < 6; i++)
        modNode_updateValue(&velocityModulators[i], velocityModulators[i].lastVal);
}
static void instrumentManager_dispatchRuntimeLfos(uint8_t steps)
{
    ctl_full_runs++;
    ctl_steps += steps;
}
static void instrumentManager_reapplyDirtyLfos(void) { ctl_reapply_lfos++; }

typedef void (*instrument_runtime_lfo_node_visitor_t)(void*, void*, void*);
void instrumentManager_visitRuntimeLfoNodes(
    instrument_runtime_lfo_node_visitor_t cb, void* ctx)
{
    (void)cb; (void)ctx;
}
static void modNode_restoreTarget(ModulationNode* t)
{
    ctl_velo_restores[t - velocityModulators]++;
}
static void modNode_restoreLfoPair(void* a, void* b, void* c)
{
    (void)a; (void)b; (void)c;
}

/* S095 P1: the Effect feed slot lock (plan §5.7, D19). */
static uint8_t mod_feed_lock;
uint8_t effects_feedSlotLock(void) { return mod_feed_lock; }

#include "modulation_interp.c"

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

    /* Law 5 (S087 P1, F11): a stamp is never 0, so no wrap frees a holder. */
    {
        int kept = 1;

        modNode_setWaveInterpEnabled(1u);
        memset(&a, 0, sizeof(a));
        memset(&b, 0, sizeof(b));
        modNode_resetTargets();
        (void)modNode_interpClaim(&a);
        (void)modNode_interpClaim(&b);
        for (unsigned i = 0u; i < 300u; i++) {
            modNode_resetTargets();
            if (modNode_interpClaim(&a) != 1u ||
                modNode_interpClaim(&b) != 1u) {
                kept = 0;
                break;
            }
        }
        expect(kept, "L5: holders survive the 255-stamp wrap");
    }

    /* Law 6 (S088 P0 §3 3b): itp fades pre-claim shared slots. */
    {
        OscInfo d;

        memset(&a, 0, sizeof(a));
        memset(&d, 0, sizeof(d));
        modNode_setWaveInterpEnabled(1u);
        modNode_waveInterpActiveCount = 0u;  /* no wtb grant last block */
        modNode_setItpReserved(1u);
        modNode_resetTargets();              /* reserved = 1 */
        expect(modNode_interpClaim(&d) == 1u,
               "L6: one wavetable grant fits beside a fade");
        expect(modNode_interpClaim(&a) == 0u,
               "L6: the fade's slot is not stolen");

        memset(&d, 0, sizeof(d));
        modNode_waveInterpActiveCount = 0u;
        modNode_setItpReserved(2u);
        modNode_resetTargets();              /* reserved = 2 */
        expect(modNode_interpClaim(&d) == 0u,
               "L6: two fades leave no wavetable slot");
        modNode_setItpReserved(0u);
    }

    /*
     * L7 (S095 P1, plan L-1): with the feed lock set, at most one slot is
     * granted per block (the lock is a phantom grant placed by
     * modNode_resetTargets()): a newcomer takes the one slot, others are
     * refused; two holders of the last block keep only one between them;
     * with the lock cleared, two are granted again from the second block.
     */
    {
        memset(&a, 0, sizeof(a));
        memset(&b, 0, sizeof(b));
        memset(&c, 0, sizeof(c));
        modNode_setWaveInterpEnabled(1u);
        mod_feed_lock = 1u;
        modNode_resetTargets();
        expect(modNode_interpClaim(&a) == 1u, "L7: locked block grants a");
        expect(modNode_interpClaim(&b) == 0u, "L7: locked block refuses b");
        expect(modNode_interpClaim(&c) == 0u, "L7: locked block refuses c");
        /*
         * Unlocked: in the first block the phantom still counts as last
         * block's grant (the documented one-block edge: holder a keeps its
         * slot, newcomer b waits); from the next block both are granted.
         */
        mod_feed_lock = 0u;
        modNode_resetTargets();
        expect(modNode_interpClaim(&a) == 1u, "L7: unlocked block keeps a");
        modNode_resetTargets();
        expect(modNode_interpClaim(&a) == 1u, "L7: unlocked grants a");
        expect(modNode_interpClaim(&b) == 1u, "L7: unlocked grants b");
        /* Two holders when the lock arrives: only one keeps its slot. */
        mod_feed_lock = 1u;
        modNode_resetTargets();
        expect((uint8_t)(modNode_interpClaim(&a) + modNode_interpClaim(&b)) == 1u,
               "L7: locked block keeps one of two holders");
        mod_feed_lock = 0u;
    }

    /* A1-CTL (S101 P2 A1): the staggered slot control. */
    {
        int b, ok_alt = 1;

        modNode_ctlState = MODNODE_CTL_FORCE;    /* the boot state */
        ctl_full_runs = ctl_steps = ctl_reapply_lfos = 0;
        for (b = 0; b < 40; b++) {
            const int before = ctl_full_runs;

            modNode_controlBlock();
            /* block 0 forced, then 2, 4, ...: a run on even blocks only */
            if ((ctl_full_runs != before) != ((b & 1) == 0))
                ok_alt = 0;
        }
        expect(ok_alt, "A1-CTL 1: the full control runs on every other block");
        /* 40 blocks: the forced first run steps 1, then 19 runs of 2; one
         * block is owed after the last run */
        expect(ctl_steps + 1 == 40, "A1-CTL 1: the LFO steps add up to the blocks");
        expect(ctl_reapply_lfos == 20, "A1-CTL 4: dirty LFOs re-applied on every gap block");

        /* A1-CTL 2: velocity node 3 refreshed in the gap */
        memset(ctl_velo_updates, 0, sizeof(ctl_velo_updates));
        memset(ctl_velo_restores, 0, sizeof(ctl_velo_restores));
        modNode_ctlState = 0u;                    /* just after a full run */
        modNode_ctlState |= (uint8_t)(1u << 3);   /* a base write on node 3 */
        modNode_controlBlock();                   /* the gap block */
        {
            int only3 = (ctl_velo_updates[3] == 1 && ctl_velo_restores[3] == 1);

            for (int i = 0; i < 6; i++)
                if (i != 3 && (ctl_velo_updates[i] || ctl_velo_restores[i]))
                    only3 = 0;
            expect(only3, "A1-CTL 2: the refreshed velocity node, and only it, re-applies next block");
        }

        /* A1-CTL 3: a foreground restore forces the next block */
        modNode_ctlState = 0u;
        ctl_full_runs = ctl_steps = 0;
        modNode_resetTargets();                   /* e.g. modNode_setDestination() */
        modNode_controlBlock();
        expect(ctl_full_runs == 1 && ctl_steps == 1,
               "A1-CTL 3: a foreground restore runs the full control next block (1 step owed)");
        modNode_controlBlock();                   /* gap */
        modNode_controlBlock();                   /* owed run: 2 steps */
        expect(ctl_full_runs == 2 && ctl_steps == 3,
               "A1-CTL 3: then the alternation resumes with the owed steps");
    }

    printf("modulation failures: %d\n", failures);
    return failures ? 1 : 0;
}
