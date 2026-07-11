#include "presetMorphEngine.h"
#include "presetManager.h"
#include "InstrumentManager.h"
#include "SceneData.h"
#include <stdint.h>

typedef struct {
    uint8_t scene_index;
    uint8_t slot;
    uint8_t descriptor_index;
    uint8_t requested_mask;
    uint8_t pass_mask;
    uint8_t pass_amount[INSTRUMENT_SLOT_COUNT];
    uint8_t active;
} preset_morph_worker_t;

static preset_morph_worker_t morph_worker;

#define PRESET_MORPH_ALL_SLOTS_MASK \
    ((uint8_t)((1u << INSTRUMENT_SLOT_COUNT) - 1u))
#define PRESET_MORPH_SLOT_MASK(slot) ((uint8_t)(1u << (slot)))

/*
 * Interpolate descriptor-owned Morph endpoints with the current 0..255 Morph
 * contract.
 *
 * Inputs: main endpoint, morph endpoint, and user-facing Morph amount where 0
 * is exactly main and 255 is exactly morph. Output: rounded descriptor image
 * value to write into morph_interpolation[] and, for the active Scene, into the
 * runtime DSP binding. Exact endpoint checks are deliberately kept outside the
 * arithmetic so descending ranges also land exactly on the stored endpoint.
 *
 * Why this uses descriptor images rather than parameter lists: instrument slot
 * types are swappable, and each instrument registry entry owns which descriptor
 * cells are morphable. The worker therefore scans descriptor flags for the
 * current slot type instead of naming drum/snare/cymbal/hihat parameters here.
 * The helper must stay separate from presetMorph_tick() because tick() owns the
 * bounded foreground iterator, while interpolation is the reusable value
 * contract shared by every morphable descriptor cell it visits.
 */
static uint16_t presetMorph_interpolate(uint16_t a, uint16_t b, uint8_t amount)
{
    int32_t numerator;

    if (amount == 0u)
        return a;
    if (amount == 255u)
        return b;

    numerator = (int32_t)a * 255 +
                ((int32_t)b - (int32_t)a) * amount;
    numerator += 127;
    if (numerator < 0)
        return 0u;
    return (uint16_t)(numerator / 255);
}

static uint8_t presetMorph_firstQueuedSlot(uint8_t mask)
{
    uint8_t slot;

    /*
     * Find the first queued Morph slot in a compact bitmask.
     *
     * Inputs: mask with one bit per instrument slot. Output: the first
     * zero-based slot with work, or INSTRUMENT_SLOT_COUNT when none exists.
     * This tiny iterator is kept separate because both pass start and pass
     * advancement need the same mask interpretation, while the descriptor walk
     * remains in presetMorph_tick().
     */
    for (slot = 0u; slot < INSTRUMENT_SLOT_COUNT; slot++) {
        if (mask & PRESET_MORPH_SLOT_MASK(slot))
            return slot;
    }
    return INSTRUMENT_SLOT_COUNT;
}

static void presetMorph_snapshotPassAmounts(const scene_t *scene)
{
    uint8_t slot;

    /*
     * Snapshot per-slot Morph amounts for one worker pass.
     *
     * Inputs: Scene settings record. Output: pass_amount[] captures the six
     * retained per-slot amounts that will be used for the current pass. A new
     * request that arrives mid-pass is queued in requested_mask for a later
     * complete pass instead of changing one slot's amount halfway through its
     * descriptor scan.
     */
    for (slot = 0u; slot < INSTRUMENT_SLOT_COUNT; slot++)
        morph_worker.pass_amount[slot] =
            scene ? scene->settings.voice_morph_amount[slot] : 0u;
}

static void presetMorph_beginPass(void)
{
    const scene_t *scene = scene_getConst(morph_worker.scene_index);

    /*
     * Begin a bounded pass over the currently requested slots.
     *
     * Inputs: requested_mask and retained Scene per-slot Morph amounts. Outputs:
     * pass_mask owns the slots for this pass, requested_mask keeps any later
     * requests, slot/descriptor_index are reset to the first queued slot, and
     * active reports whether tick() has work. This cannot be folded into
     * requestVoice/requestAll because requests can arrive while a pass is
     * already active and must not disturb that pass's amount snapshot.
     */
    morph_worker.requested_mask &= PRESET_MORPH_ALL_SLOTS_MASK;
    if (!scene || morph_worker.requested_mask == 0u) {
        morph_worker.active = 0u;
        morph_worker.pass_mask = 0u;
        return;
    }

    morph_worker.pass_mask = morph_worker.requested_mask;
    morph_worker.requested_mask = 0u;
    presetMorph_snapshotPassAmounts(scene);
    morph_worker.slot = presetMorph_firstQueuedSlot(morph_worker.pass_mask);
    morph_worker.descriptor_index = 0u;
    morph_worker.active = 1u;
}

void presetMorph_init(void)
{
    morph_worker.scene_index = 0u;
    morph_worker.slot = 0u;
    morph_worker.descriptor_index = 0u;
    morph_worker.requested_mask = 0u;
    morph_worker.pass_mask = 0u;
    for (uint8_t slot = 0u; slot < INSTRUMENT_SLOT_COUNT; slot++)
        morph_worker.pass_amount[slot] = 0u;
    morph_worker.active = 0u;
}

void presetMorph_requestVoice(uint8_t scene_index, uint8_t slot)
{
    if (!scene_getConst(scene_index) || slot >= INSTRUMENT_SLOT_COUNT)
        return;

    /*
     * Queue one Scene voice for Morph interpolation.
     *
     * Inputs: Scene index and zero-based instrument slot. The amount is read
     * from Scene-retained per-slot Morph settings when a pass begins. Output:
     * the selected slot bit is queued for the foreground worker. This does not
     * mutate SceneData; Preset and future Scene-file load own retained setting
     * writes so the worker remains a bounded apply engine, not a storage owner.
     */
    if (morph_worker.scene_index != scene_index) {
        morph_worker.scene_index = scene_index;
        morph_worker.requested_mask = 0u;
        morph_worker.pass_mask = 0u;
        morph_worker.active = 0u;
    }
    morph_worker.requested_mask |= PRESET_MORPH_SLOT_MASK(slot);
    if (!morph_worker.active)
        presetMorph_beginPass();
}

void presetMorph_requestAll(uint8_t scene_index)
{
    if (!scene_getConst(scene_index))
        return;

    /*
     * Queue all instrument slots for Morph interpolation.
     *
     * Inputs: Scene index whose per-slot Morph amounts are already retained.
     * Output: all six slot bits are queued. The overall Morph user control uses
     * this after it writes every per-slot amount; Scene apply/load uses it to
     * rebuild without overwriting distinct per-voice values.
     */
    if (morph_worker.scene_index != scene_index) {
        morph_worker.scene_index = scene_index;
        morph_worker.requested_mask = 0u;
        morph_worker.pass_mask = 0u;
        morph_worker.active = 0u;
    }
    morph_worker.requested_mask |= PRESET_MORPH_ALL_SLOTS_MASK;
    if (!morph_worker.active)
        presetMorph_beginPass();
}

uint8_t presetMorph_tick(void)
{
    scene_t *scene;

    if (!morph_worker.active)
        return 0u;
    scene = scene_get(morph_worker.scene_index);
    if (!scene) {
        morph_worker.active = 0u;
        return 0u;
    }

    while (morph_worker.pass_mask != 0u &&
           morph_worker.slot < INSTRUMENT_SLOT_COUNT) {
        kit_instrument_slot_t *instrument =
            &scene->kit.instruments[morph_worker.slot];

        if (!(morph_worker.pass_mask &
              PRESET_MORPH_SLOT_MASK(morph_worker.slot))) {
            morph_worker.slot++;
            morph_worker.descriptor_index = 0u;
            continue;
        }

        while (morph_worker.descriptor_index < INSTRUMENT_PARAM_COUNT) {
            uint8_t local = morph_worker.descriptor_index++;
            const ParamDescriptor *descriptor =
                instrumentManager_descriptor(instrument->type, local);
            uint16_t value;
            instrument_param_id_t id;

            if (!descriptor ||
                !(descriptor->flags & INSTRUMENT_PARAM_FLAG_MORPHABLE)) {
                continue;
            }
            value = presetMorph_interpolate(
                instrument->parameter_images.instrument_parameters[local],
                instrument->parameter_images.morph_instrument_parameters[local],
                morph_worker.pass_amount[morph_worker.slot]);
            instrument->parameter_images.morph_interpolation[local] = value;
            id = instrumentParam_make(morph_worker.slot, local);
            if (morph_worker.scene_index == scene_getActiveIndex())
                preset_applyInstrumentRuntimeValue(morph_worker.scene_index,
                                                   id, value);
            return 1u;
        }
        morph_worker.pass_mask &=
            (uint8_t)(~PRESET_MORPH_SLOT_MASK(morph_worker.slot));
        morph_worker.slot++;
        morph_worker.descriptor_index = 0u;
    }

    if (morph_worker.requested_mask != 0u) {
        presetMorph_beginPass();
    } else {
        morph_worker.active = 0u;
        morph_worker.pass_mask = 0u;
    }
    return 0u;
}

void presetMorph_rebuildScene(uint8_t scene_index)
{
    /*
     * Rebuild all Morph interpolation from retained per-voice Scene settings.
     *
     * Inputs: Scene index. Output: all slots are queued without changing the
     * Scene global Morph mirror or any per-slot amount. Clients are Scene apply,
     * Kit load apply, and non-mutating endpoint refresh paths.
     */
    presetMorph_requestAll(scene_index);
}
