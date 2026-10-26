/*
 * ARM codegen check for the S084 VX2 DSP loops (VX2-10.10).
 *
 * What:       compiles the real stock-wave, wavetable, transient and shaped
 *             ZDF function text with the firmware flags so fpseq.py can
 *             report per-loop instruction counts.
 * Why:        the schedule's cost gates are measured on the target code, not
 *             on a host transcription.
 * Inputs:     generated fragments from the Makefile target.
 * Outputs:    build/ac_vx2.o, consumed by fpseq.py --report --all.
 * Accessors:  make -C tools/dsp_test armcheck-vx2.
 * Affiliates: Oscillator.c, OscillatorWavetable.c, transientGenerator.c,
 *             ResonantFilter.c, DriveShaper.h, fpseq.py.
 */
#include "config.h"
#include <string.h>
#include "SampleMemory.h"
#include "StockWaves.h"
#include "Oscillator.h"
#include "OscillatorWavetable.h"
#include "transientGenerator.h"
#include "InsertEffectsManager.h"
#include "DriveShaper.h"
#include "ResonantFilter.h"
#include "Samples.h"

#define WTB_INV_CYCLE 2.3283064365386963e-10f
#define TRANSIENT_BURST_DECAY 0.99773f
#define TRANSIENT_BURST_BASE_HZ 440.0f
#define TRANSIENT_INV_CYCLE 2.3283064365386963e-10f
#define TRANSIENT_SAMPLE_SHIFT 12u
#define TRANSIENT_PCM_END_PHASE 2311061504u

#include "vx2_fns_arm.c"
