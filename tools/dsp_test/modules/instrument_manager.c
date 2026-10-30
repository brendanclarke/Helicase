/*
 * Instrument manager module host tests: the osc1_type selector row (S087
 * stage B).
 *
 * What:       compiles the real instrumentManager_targetValid() and
 *             seq_rampDtypeOk(), the real ROW_SELECT_SPECIAL macro, the
 *             Datatypes enum, MENU_OSC_TYPE and the Drum descriptor index enum
 *             (all extracted by name).
 *             Laws:
 *               3. the gate accepts osc1_type for AUTOMATION, refuses it for
 *                  MODULATION, and still refuses a non-morphable row without
 *                  SELECTOR;
 *               4. seq_rampDtypeOk(osc1_type) is 0 (no ramp);
 *               5. the row dtype's low nibble is DTYPE_MENU and its menu id
 *                  is MENU_OSC_TYPE (4);
 *               6. DRUM_PARAM_OSC1_RUN is appended last (value + 1 ==
 *                  DRUM_PARAM_COUNT), directly after osc1_type;
 *               7. the osc1_run row is a tagged DTYPE_ON_OFF selector whose
 *                  high nibble is ONOFF_TAG_OSC_RUN, with AUTOMATABLE |
 *                  SELECTOR and no ramp.
 *               8. the voice_chain row (S089 P2) is appended after osc1_run,
 *                  is a tagged DTYPE_ON_OFF selector (tag 2) with
 *                  AUTOMATABLE | SELECTOR and no ramp.
 *             (Laws 1-2, osc_setType(), are the oscillator module's type
 *             group.)
 * Why:        S087 P0 §12.5: typ is a selector, step-automatable, never
 *             morphed and never an LFO/velocity target. Break proofs: dropping
 *             the SELECTOR branch in the gate makes law 3 fail; giving the row
 *             DTYPE_0B127 makes law 4 fail (both built by the Makefile).
 * Inputs:     build/gen/im_*.c/inc from the instrument_manager rule.
 * Outputs:    one line per failure (first 40); "instrument_manager failures:
 *             N"; exit 1 if N > 0.
 * Accessors:  make -C tools/dsp_test instrument_manager.
 * Affiliates: InstrumentManager.c, sequencer.c, menu.h, MenuText.h,
 *             DrumParameters.c.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>

#define INSTRUMENT_SLOT_COUNT 6u
#define INSTRUMENT_PARAM_COUNT 64u
#define INSTRUMENT_VOICE_ID_COUNT 240u
#define INSTRUMENT_TOTAL_ID_COUNT 256u
#define TYPE_UINT8 3u
#define IM_SPECIAL_OSC_TYPE 20u
#define IM_SPECIAL_OSC_RUN 21u
#define IM_SPECIAL_VOICE_CHAIN 22u
#define IM_SPECIAL_OSC1 0x00u
#define MOD_NONE { 0u, 0u, INSTRUMENT_MOD_DOMAIN_NONE }
#define BIND_SPECIAL(member_, type_, special_) \
    { INSTRUMENT_BIND_INSTANCE_OFFSET, 0u, (uint8_t)(type_), (uint8_t)(special_) }
#define BIND(member_, type_) \
    { INSTRUMENT_BIND_INSTANCE_OFFSET, 0u, (uint8_t)(type_), 0u }

typedef uint8_t instrument_param_value_t;
typedef uint16_t instrument_param_id_t;

typedef enum {
    INSTRUMENT_TYPE_DRM = 0,
    INSTRUMENT_TYPE_SNR,
    INSTRUMENT_TYPE_CYM,
    INSTRUMENT_TYPE_HAT,
    INSTRUMENT_TYPE_UNKNOWN
} instrument_type_t;

typedef enum {
    INSTRUMENT_TARGET_MODULATION = 0,
    INSTRUMENT_TARGET_AUTOMATION
} instrument_target_use_t;

typedef enum {
    INSTRUMENT_BIND_NONE = 0,
    INSTRUMENT_BIND_INSTANCE_OFFSET,
    INSTRUMENT_BIND_SLOT_DECIMATION
} instrument_binding_kind_t;

typedef struct {
    instrument_param_value_t min_value;
    instrument_param_value_t max_value;
    uint8_t flags;
} instrument_mod_domain_t;

typedef struct {
    instrument_binding_kind_t kind;
    uint16_t offset;
    uint8_t parameter_type;
    uint8_t special;
} instrument_runtime_binding_t;

typedef struct {
    const char *file_key;
    const char *short_name;
    const char *long_name;
    const char *category;
    uint8_t dtype;
    uint8_t flags;
    instrument_mod_domain_t mod_domain;
    instrument_runtime_binding_t runtime;
} ParamDescriptor;

#define INSTRUMENT_PARAM_FLAG_MORPHABLE    0x01u
#define INSTRUMENT_PARAM_FLAG_MODULATABLE  0x02u
#define INSTRUMENT_PARAM_FLAG_AUTOMATABLE  0x04u
#define INSTRUMENT_PARAM_FLAG_SELECTOR     0x08u
#define INSTRUMENT_MOD_DOMAIN_NONE         0x00u
#define INSTRUMENT_MOD_DOMAIN_INTEGER      0x02u
#define INSTRUMENT_MOD_DOMAIN_DYNAMIC_MAX  0x04u

typedef struct {
    instrument_type_t type;
} kit_instrument_slot_t;

#include "im_dtypes.inc"
#include "im_menuid.inc"
#include "im_rowmacro.inc"
#include "im_onoffmacro.inc"
#include "im_onofftag.inc"
#include "im_index.inc"

static const ParamDescriptor g_typ_row = ROW_SELECT_SPECIAL(
    "osc1_type", "Oscilltr", "Type", "typ", MENU_OSC_TYPE, osc.oscType,
    TYPE_UINT8, IM_SPECIAL_OSC_TYPE | IM_SPECIAL_OSC1);

/* S088 P0: the run-mode row, from the real tag macro. */
static const ParamDescriptor g_run_row = ROW_SELECT_ONOFF_SPECIAL(
    "osc1_run", "Oscilltr", "Run Mode", "run", ONOFF_TAG_OSC_RUN,
    runMode, TYPE_UINT8, IM_SPECIAL_OSC_RUN | IM_SPECIAL_OSC1);

/* S089 P2: the voice chain row, from the real tag macro. */
static const ParamDescriptor g_chn_row = ROW_SELECT_ONOFF_SPECIAL(
    "voice_chain", "Voice", "Chain", "chn", ONOFF_TAG_VOICE_CHAIN,
    chainType, TYPE_UINT8, IM_SPECIAL_VOICE_CHAIN);

/* A non-morphable, non-selector row (the pre-S087 gate's refusal). */
static const ParamDescriptor g_plain_row = {
    "plain", "pln", "Plain", "Test", DTYPE_0B127, 0u,
    { 0u, 0u, INSTRUMENT_MOD_DOMAIN_NONE }, BIND_SPECIAL(0u, TYPE_UINT8, 0u)
};

static kit_instrument_slot_t g_slot = { INSTRUMENT_TYPE_DRM };
static int failures;

static void expect(int cond, const char *msg)
{
    if (!cond) {
        if (failures < 40)
            printf("FAIL: %s\n", msg);
        failures++;
    }
}

/* --------------------------------------------------------- gate stubs -- */
uint8_t instrumentParam_isVoiceParameter(instrument_param_id_t id)
{
    (void)id;
    return 1u;
}
uint8_t instrumentParam_slot(instrument_param_id_t id)
{
    (void)id;
    return 0u;
}
uint8_t instrumentParam_local(instrument_param_id_t id)
{
    return (uint8_t)id;
}
const kit_instrument_slot_t *scene_instrumentSlotConst(uint8_t scene, uint8_t slot)
{
    (void)scene; (void)slot;
    return &g_slot;
}
const ParamDescriptor *instrumentManager_descriptor(instrument_type_t type,
                                                    uint8_t index)
{
    (void)type;
    return index == 0u ? &g_typ_row : &g_plain_row;
}
static uint8_t instrumentManager_descriptorSupportsModulationRange(
    const ParamDescriptor *d)
{
    return (uint8_t)((d->mod_domain.flags & INSTRUMENT_MOD_DOMAIN_DYNAMIC_MAX) != 0u);
}
static uint8_t instrumentManager_descriptorIsSlotDecimationTarget(
    const ParamDescriptor *d)
{
    return (uint8_t)(d->runtime.kind == INSTRUMENT_BIND_SLOT_DECIMATION);
}
uint8_t effectTarget_isEffectId(uint16_t id) { (void)id; return 0u; }
uint8_t effects_targetValid(uint8_t s, uint16_t id, instrument_target_use_t u)
{ (void)s; (void)id; (void)u; return 0u; }
uint8_t sceneModTarget_isSceneTarget(uint16_t id) { (void)id; return 0u; }

#include "im_gate.c"
#include "im_ramp.c"

int main(void)
{
    /* 5: the real macro's dtype. */
    expect((g_typ_row.dtype & 0x0fu) == DTYPE_MENU, "law 5: typ dtype is DTYPE_MENU");
    expect((g_typ_row.dtype >> 4) == MENU_OSC_TYPE, "law 5: typ menu id 4");
    expect(MENU_OSC_TYPE == 4, "law 5: MENU_OSC_TYPE is 4");

    /* 6: osc1_run after osc1_type; S089 P2: voice_chain is now last. */
    expect(DRUM_PARAM_OSC1_RUN == DRUM_PARAM_OSC1_TYPE + 1u,
           "law 6: osc1_run follows osc1_type");
    expect(DRUM_PARAM_VOICE_CHAIN == DRUM_PARAM_OSC1_RUN + 1u,
           "law 6: voice_chain follows osc1_run");
    expect(DRUM_PARAM_VOICE_CHAIN + 1u == DRUM_PARAM_COUNT,
           "law 6: voice_chain is the last Drum row");

    /* 7: the tagged ONOFF row (S088 P0 §2.1 D12). */
    expect((g_run_row.dtype & 0x0fu) == DTYPE_ON_OFF,
           "law 7: run dtype low nibble is DTYPE_ON_OFF");
    expect((g_run_row.dtype >> 4) == ONOFF_TAG_OSC_RUN,
           "law 7: run dtype high nibble is ONOFF_TAG_OSC_RUN");
    expect(g_run_row.flags ==
               (INSTRUMENT_PARAM_FLAG_AUTOMATABLE |
                INSTRUMENT_PARAM_FLAG_SELECTOR),
           "law 7: run is automatable+selector, never morph/mod");
    expect(seq_rampDtypeOk(&g_run_row) == 0u, "law 7: run never ramps");

    /* 8: the voice chain row (S089 P2): tag 2, selector, no ramp. */
    expect((g_chn_row.dtype & 0x0fu) == DTYPE_ON_OFF,
           "law 8: chn dtype low nibble is DTYPE_ON_OFF");
    expect((g_chn_row.dtype >> 4) == ONOFF_TAG_VOICE_CHAIN,
           "law 8: chn dtype high nibble is ONOFF_TAG_VOICE_CHAIN");
    expect(ONOFF_TAG_VOICE_CHAIN != ONOFF_TAG_OSC_RUN,
           "law 8: chn and run tags differ");
    expect(g_chn_row.flags ==
               (INSTRUMENT_PARAM_FLAG_AUTOMATABLE |
                INSTRUMENT_PARAM_FLAG_SELECTOR),
           "law 8: chn is automatable+selector, never morph/mod");
    expect(seq_rampDtypeOk(&g_chn_row) == 0u, "law 8: chn never ramps");

    /* 3: the gate. */
    expect(instrumentManager_targetValid(0u,
               (instrument_param_id_t)(0u), INSTRUMENT_TARGET_AUTOMATION) == 1u,
           "law 3: typ valid for automation");
    expect(instrumentManager_targetValid(0u,
               (instrument_param_id_t)(0u), INSTRUMENT_TARGET_MODULATION) == 0u,
           "law 3: typ invalid for modulation");
    expect(instrumentManager_targetValid(0u,
               (instrument_param_id_t)(1u), INSTRUMENT_TARGET_AUTOMATION) == 0u,
           "law 3: a plain non-morphable row stays invalid");

    /* 4: no ramp. */
    expect(seq_rampDtypeOk(&g_typ_row) == 0u, "law 4: typ never ramps");

    printf("instrument_manager failures: %d\n", failures);
    return failures ? 1 : 0;
}
