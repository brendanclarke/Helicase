/*
 * Oscillator type (typ) host gate (S087 stage B).
 *
 * What:       compiles the real OSC_TYPE_* block and OscInfo, osc_setType(),
 *             osc_positionTrigger(), instrumentManager_targetValid() and
 *             seq_rampDtypeOk(), plus the real ROW_SELECT_SPECIAL macro and
 *             the Drum descriptor enum.
 *             Laws:
 *               1. osc_setType() clamps to OSC_TYPE_SELECTABLE; an unchanged
 *                  type changes nothing (byte-compare of the struct);
 *               2. after a change positionApplied == position, the freq cache
 *                  is invalid, and waveform <= the new type's last entry;
 *               3. the gate accepts osc1_type for AUTOMATION, refuses it for
 *                  MODULATION, and still refuses a non-morphable row without
 *                  SELECTOR;
 *               4. seq_rampDtypeOk(osc1_type) is 0 (no ramp);
 *               5. the row dtype's low nibble is DTYPE_MENU and its menu id
 *                  is MENU_OSC_TYPE (4);
 *               6. DRUM_PARAM_OSC1_TYPE is appended last (value + 1 ==
 *                  DRUM_PARAM_COUNT).
 * Why:        S087 P0 §12.5: typ is a selector, step-automatable, never
 *             morphed and never an LFO/velocity target. Break proofs: dropping
 *             the SELECTOR branch in the gate makes law 3 fail; giving the row
 *             DTYPE_0B127 makes law 4 fail (both via the Makefile targets).
 * Inputs:     gen/ot_*.c/inc from the osc_type rule.
 * Outputs:    one line per failure (first 40); "osc_type failures: N";
 *             exit 1 if N > 0.
 * Accessors:  make -C tools/dsp_test osc_type.
 * Affiliates: Oscillator.h/.c, InstrumentManager.c, sequencer.c, menu.h,
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

#include "ot_dtypes.inc"
#include "ot_defs.inc"
#include "ot_oscinfo.inc"
#include "ot_menuid.inc"
#include "ot_rowmacro.inc"
#include "ot_index.inc"

static const ParamDescriptor g_typ_row = ROW_SELECT_SPECIAL(
    "osc1_type", "Oscilltr", "Type", "typ", MENU_OSC_TYPE, osc.oscType,
    TYPE_UINT8, IM_SPECIAL_OSC_TYPE | IM_SPECIAL_OSC1);

/* A non-morphable, non-selector row (the pre-S087 gate's refusal). */
static const ParamDescriptor g_plain_row = {
    "plain", "pln", "Plain", "Test", DTYPE_0B127, 0u,
    { 0u, 0u, INSTRUMENT_MOD_DOMAIN_NONE }, BIND_SPECIAL(0u, TYPE_UINT8, 0u)
};

static kit_instrument_slot_t g_slot = { INSTRUMENT_TYPE_DRM };
static int failures;

/* Stage-B stand-ins for the stage-D bind (the osc_setType clamp is the law). */
static inline uint8_t osc_isTyped(const OscInfo* o)
{
    return (uint8_t)(o->oscType >= OSC_TYPE_SMP && o->oscType <= OSC_TYPE_WTB);
}
uint8_t osc_userCount(uint8_t type) { return type == OSC_TYPE_WAV ? 8u : 0u; }
void osc_userBind(OscInfo* o) { (void)o; }
uint32_t osc_typedTriggerPhase(OscInfo* o) { o->positionApplied = o->position; return 0u; }

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
uint8_t modNode_getMaxWaveformIndex(void) { return 8u; }

#include "ot_pos.c"
#include "ot_settype.c"
#include "ot_gate.c"
#include "ot_ramp.c"

int main(void)
{
    OscInfo a;
    OscInfo before;

    /* 5: the real macro's dtype. */
    expect((g_typ_row.dtype & 0x0fu) == DTYPE_MENU, "law 5: typ dtype is DTYPE_MENU");
    expect((g_typ_row.dtype >> 4) == MENU_OSC_TYPE, "law 5: typ menu id 4");
    expect(MENU_OSC_TYPE == 4, "law 5: MENU_OSC_TYPE is 4");

    /* 6: appended last. */
    expect(DRUM_PARAM_OSC1_TYPE + 1u == DRUM_PARAM_COUNT,
           "law 6: osc1_type is the last Drum row");

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

    /* 1: clamp and no-op. */
    memset(&a, 0, sizeof(a));
    a.oscType = OSC_TYPE_MODLIST;
    a.waveform = 7u;
    a.position = 40u;
    a.positionApplied = 12u;
    a.freqCacheValid = 1u;
    before = a;
    osc_setType(&a, 200u);
    expect(a.oscType == OSC_TYPE_SELECTABLE - 1u, "law 1: clamps to SELECTABLE-1");
    expect(memcmp(&a, &before, sizeof(a)) != 0, "law 1: a real change is applied");
    before = a;
    osc_setType(&a, OSC_TYPE_SELECTABLE - 1u);
    expect(memcmp(&a, &before, sizeof(a)) == 0, "law 1: unchanged type is a no-op");

    /* 2: re-anchor, cache invalidation, wave clamp. */
    memset(&a, 0, sizeof(a));
    a.oscType = OSC_TYPE_SMP;   /* pretend a later stage left this type */
    a.waveform = 9u;
    a.position = 77u;
    a.positionApplied = 3u;
    a.freqCacheValid = 1u;
    a.phase = 1234u;
    osc_setType(&a, OSC_TYPE_WAV);
    expect(a.positionApplied == a.position, "law 2: positionApplied re-anchored");
    expect(a.freqCacheValid == 0u, "law 2: freq cache invalidated");
    expect(a.waveform <= OSC_STOCK_LAST, "law 2: waveform clamped to the stock list");

    printf("osc_type failures: %d\n", failures);
    return failures ? 1 : 0;
}
