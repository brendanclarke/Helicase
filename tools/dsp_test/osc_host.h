/*
 * Host build of the real oscillator (modules/oscillator.c, modules/modulation.c).
 *
 * What:       supplies what Oscillator.h/.c take from the firmware headers that
 *             extract.py --body strips (config.h, wavetable.h, Samples.h,
 *             random.h, the CMSIS APSR read), then includes the real header as
 *             build/gen/Oscillator.host.h. The flash tables are writable here so
 *             each test group can load its own content. The catalogue comes from
 *             the real SampleMemory.c/SampleInstall.c over cat_host.h's RAM
 *             image.
 * Why:        the oscillator is tested as the whole real file, so no line range
 *             or function list goes stale when Oscillator.c changes.
 * Inputs:     -include prelude.h and cat_host.h (the Makefile), build/gen.
 * Outputs:    the OscInfo type and every Oscillator.h declaration.
 * Accessors:  modules/oscillator.c, modules/modulation.c.
 * Affiliates: config.h (REAL_FS, INTERPOLATE_*, OSC_WAVE_INTERP_MAX_ACTIVE),
 *             wavetable.h, Samples.h, random.h, MidiPitch.h.
 */
#ifndef OSC_HOST_H
#define OSC_HOST_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <math.h>

#include "SampleMemory.h"
#include "SampleInstall.h"
#include "MidiPitch.h"

/* config.h */
#define REAL_FS ((float)44108.07291666667f)
#define INTERPOLATE_OSC 1
#define INTERPOLATE_FM_OSC 1
#define OSC_WAVE_INTERP_MAX_ACTIVE 2u

/* wavetable.h / Samples.h (writable on the host) */
#define TABLESIZE 4096
extern int16_t sine_table[TABLESIZE + 1];
extern int16_t sawTable[11][1024];
extern int16_t triTable[11][1024];
extern int16_t recTable[11][1024];
extern int16_t sq3Table[11][1024];
extern int16_t sw3Table[11][1024];
extern unsigned char crashSample[32769];

/* random.h */
int16_t GetRngValue(void);

/* cmsis_intrinsics.h: calcNoise() reads the overflow flag (no host meaning). */
typedef union {
    struct { uint32_t _reserved0:28; uint32_t V:1; uint32_t C:1; uint32_t Z:1;
             uint32_t N:1; } b;
    uint32_t w;
} APSR_Type;
static inline uint32_t __get_APSR(void) { return 0u; }

/* modulationNode.h */
uint8_t modNode_interpClaim(void* osc);

/* catalogue_host.c */
void catalogue_host_reset(void);

#include "Oscillator.host.h"
#endif /* OSC_HOST_H */
