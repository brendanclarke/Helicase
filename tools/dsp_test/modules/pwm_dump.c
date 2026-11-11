/*
 * pwm_dump.c — dump the shipped pwm and stock waves for plotting (S094 P4).
 *
 * What:       renders the real typ-pwm plain render (osc_pwmRenderPlain(),
 *             the firmware's Oscillator.c through the host build) on the real
 *             stock tables (extracted from Core/DSPAudio/wavetable.c), for
 *             Sin/Tri/Saw/Rec at every pos value 0..127, plus the stock rows
 *             (typ wav) and the Alkaline 303 square row (alkSqrTable). One
 *             cycle is exactly 512 samples (phaseInc 2^23, about 86 Hz at
 *             REAL_FS), on octave row 2 (the row osc_setTableFreq() picks at
 *             that pitch), and two cycles are written.
 * Why:        S094 P4 investigates the pos -> width law by looking at what the
 *             device plays, not at a re-implementation.
 * Inputs:     none.
 * Outputs:    CSV on stdout: kind,wave,pos,i,value (int16).
 * Accessors:  make -C tools/dsp_test pwm_dump;
 *             tools/plots/plot_s094p4_pwm.py.
 * Affiliates: modules/oscillator.c (the same host includes and stand-ins).
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "osc_host.h"
#include "Oscillator.host.c"
#include "oscPwmTables.host.c"

/* the real stock tables, extracted from wavetable.c (const removed) */
#include "pd_sine_table.c"
#include "pd_sawTable.c"
#include "pd_triTable.c"
#include "pd_recTable.c"
/* the Alkaline 303 square (AlkalineSquareTable.c, extracted) */
#include "AlkalineSquareTable.host.c"

/* host stand-ins, as modules/oscillator.c */
static uint32_t host_rng = 0x0850001u;
int16_t GetRngValue(void) { return (int16_t)(golden_rand(&host_rng) >> 16); }
uint8_t modNode_interpClaim(void* osc) { (void)osc; return 0u; }

#define PD_ROW     2u            /* the octave row at about 86 Hz */
#define PD_CYCLE   512u          /* samples per cycle */
#define PD_INC     (1u << 23)    /* 2^32 / 512 */
#define PD_CYCLES  2u

static const char *const pd_names[4] = { "Sin", "Tri", "Saw", "Rec" };
static const uint8_t pd_waves[4] = { SINE, TRI, SAW, REC };

static void pd_row(const char *kind, const char *name, const int16_t *row)
{
    for (unsigned c = 0u; c < PD_CYCLES; ++c)
        for (unsigned i = 0u; i < PD_CYCLE; ++i)
            printf("%s,%s,-1,%u,%d\n", kind, name, c * PD_CYCLE + i,
                   row[(i * 1024u / PD_CYCLE) & 1023u]);
}

int main(void)
{
    int16_t buf[OUTPUT_DMA_SIZE];

    printf("kind,wave,pos,i,value\n");
    for (unsigned w = 0u; w < 4u; ++w)
        for (unsigned k = 0u; k < 128u; ++k) {   /* every pos value */
            OscInfo o;
            unsigned n = 0u;

            memset(&o, 0, sizeof(o));
            o.waveform = pd_waves[w];
            o.tableOffset = PD_ROW;
            o.phase = 0u;
            o.phaseInc = PD_INC;
            o.oscType = OSC_TYPE_PWM;
            o.sampleCacheLooped = OSC_BIND_PWM;
            o.position = (uint8_t)k;
            o.positionApplied = (uint8_t)k;
            while (n < PD_CYCLES * PD_CYCLE) {
                osc_pwmRenderPlain(&o, buf, OUTPUT_DMA_SIZE, 1.0f);
                for (unsigned i = 0u; i < OUTPUT_DMA_SIZE; ++i, ++n)
                    printf("pwm,%s,%u,%u,%d\n", pd_names[w], (uint8_t)k, n,
                           buf[i]);
            }
        }
    /* typ wav: the stock rows, one table point per sample pair */
    pd_row("wav", "Saw", sawTable[PD_ROW]);
    pd_row("wav", "Tri", triTable[PD_ROW]);
    pd_row("wav", "Rec", recTable[PD_ROW]);
    {
        int16_t sine[1024];

        for (unsigned i = 0u; i < 1024u; ++i)
            sine[i] = sine_table[i * 4u];
        pd_row("wav", "Sin", sine);
    }
    pd_row("alk", "Sqr", alkSqrTable[PD_ROW]);
    pd_row("alk", "Saw", sawTable[PD_ROW]);
    return 0;
}
