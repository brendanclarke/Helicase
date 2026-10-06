#include "presetMorphEngine.h"
#include "presetManager.h"
#include "InstrumentManager.h"
#include "SceneData.h"
#include "sequencer.h"
#include <stdint.h>

typedef struct {
    uint8_t scene_index;
    uint8_t slot;
    uint8_t descriptor_index;
    uint8_t requested_mask;
    uint8_t pass_mask;
    uint8_t pass_amount[INSTRUMENT_SLOT_COUNT];
    uint8_t pass_lfo_resolved_mask;
    uint8_t priority_resume_valid;
    uint8_t priority_slot;
    uint8_t resume_slot;
    uint8_t resume_descriptor_index;
    uint8_t active;
} preset_morph_worker_t;

/*
 * One hidden LFO voice-Morph contribution.
 *
 * Inputs: InstrumentManager supplies an endpoint direction and normalized
 * depth for one target/source/pair entry. Output: the bounded Morph resolver
 * converts that direction/depth into a signed delta from the current effective
 * base. This keeps the entry at two bytes, exactly matching the former
 * active+absolute-amount representation, while removing dispatch-time base
 * capture from the LFO composition contract.
 */
typedef struct {
    /* NONE is inactive; MAIN/MORPH select the endpoint direction. */
    uint8_t direction;
    /* Base-independent normalized contribution depth, 0..255. */
    uint8_t depth;
} preset_morph_lfo_contribution_t;

static preset_morph_worker_t morph_worker;
static preset_morph_lfo_contribution_t morph_lfo_contributions
    [INSTRUMENT_SLOT_COUNT][INSTRUMENT_SLOT_COUNT][2u];

/*
 * Per-slot step-automation Morph override.
 *
 * Inputs: seq_applySceneAutomation() supplies a transient amount for one
 * active Scene voice; transport-boundary restore clears the override. Output:
 * the Morph worker uses this amount as the effective base instead of the
 * retained Scene setting while the overlay is active. LFO contributions are
 * resolved around this base, and SceneData is never modified by this layer.
 * Lifetime: static runtime state only; it is cleared at boot and transport
 * restore. Owner: Preset Morph worker. Affiliate: Sequencer Scene-target
 * automation restore.
 */
static struct {
    uint8_t active;
    uint8_t amount;
} morph_step_override[INSTRUMENT_SLOT_COUNT];

#define PRESET_MORPH_ALL_SLOTS_MASK \
    ((uint8_t)((1u << INSTRUMENT_SLOT_COUNT) - 1u))
#define PRESET_MORPH_SLOT_MASK(slot) ((uint8_t)(1u << (slot)))

/*
 * Interpolate descriptor-owned Morph endpoints with the current 0..255 Morph
 * contract.
 *
 * Inputs: main endpoint, morph endpoint, and user-facing Morph amount where 0
 * is exactly main and 255 is exactly morph. Output: rounded descriptor byte
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
static instrument_param_value_t presetMorph_interpolate(
    instrument_param_value_t a,
    instrument_param_value_t b,
    uint8_t amount)
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
    return (instrument_param_value_t)(numerator / 255);
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

static uint8_t presetMorph_effectiveVoiceBase(const scene_t *scene,
                                              uint8_t slot)
{
    /*
     * Resolve the one authoritative Morph base for every worker path.
     *
     * Inputs: resident Scene pointer and zero-based voice slot. Output: the
     * transient step-automation base when active, otherwise the retained
     * Scene voice Morph amount. Keeping this choice in one helper prevents
     * queued, priority, synchronous, and LFO-resolve paths from composing
     * against different bases. It never writes SceneData or AutoSave.
     *
     * Affiliate: morph_step_override[] is the runtime-only Sequencer layer;
     * callers still decide whether the returned base is later interpolated or
     * combined with hidden LFO direction/depth contributions.
     */
    if (!scene || slot >= INSTRUMENT_SLOT_COUNT)
        return 0u;
    return morph_step_override[slot].active
        ? morph_step_override[slot].amount
        : scene->settings.voice_morph_amount[slot];
}

static uint8_t presetMorph_voiceHasLfoLayer(uint8_t slot)
{
    uint8_t source;
    uint8_t pair;

    /*
     * Test whether one voice has any active hidden LFO Morph contributions.
     *
     * Input: target voice slot. Output: nonzero if any source slot/pair is
     * currently driving that voice's secondary Morph layer. This small scan is
     * separate from the resolver because request/begin-pass code only needs a
     * yes/no answer, while the resolver must also sum shaped contributions.
     */
    if (slot >= INSTRUMENT_SLOT_COUNT)
        return 0u;
    for (source = 0u; source < INSTRUMENT_SLOT_COUNT; source++) {
        for (pair = 0u; pair < 2u; pair++) {
            if (morph_lfo_contributions[slot][source][pair].direction !=
                PRESET_MORPH_LFO_DIRECTION_NONE)
                return 1u;
        }
    }
    return 0u;
}

static uint8_t presetMorph_resolveLfoAmount(const scene_t *scene, uint8_t slot)
{
    uint8_t source;
    uint8_t pair;
    uint8_t base;
    int32_t effective;

    /*
     * Resolve the effective Morph amount for one LFO-modulated voice.
     *
     * Inputs: current Scene and target voice slot. Output: the current
     * effective base Morph plus all active direction/depth deltas, clamped to
     * 0..255. Each contribution is independent of the base that existed when
     * the LFO sample was dispatched; this resolver computes its endpoint delta
     * from the current step-automation-or-retained base.
     *
     * This cannot be folded into every descriptor interpolation because that
     * would recalculate the same LFO/base combination for every morphable
     * parameter in the voice. It also cannot run in LFO dispatch because
     * dispatch must not walk descriptor tables or update all morphed
     * parameters immediately.
     */
    if (!scene || slot >= INSTRUMENT_SLOT_COUNT)
        return 0u;
    base = presetMorph_effectiveVoiceBase(scene, slot);
    effective = base;
    for (source = 0u; source < INSTRUMENT_SLOT_COUNT; source++) {
        for (pair = 0u; pair < 2u; pair++) {
            const preset_morph_lfo_contribution_t *contribution =
                &morph_lfo_contributions[slot][source][pair];
            /*
             * Apply one contribution around the current effective base.
             *
             * MORPH scales the remaining distance to 255; MAIN scales the
             * distance to zero. The rounded integer math keeps both endpoints
             * exact at depth 255 and lets multiple source/pair deltas compose
             * before the final clamp.
             */
            if (contribution->direction ==
                PRESET_MORPH_LFO_DIRECTION_MORPH) {
                effective += ((int32_t)(255u - base) *
                              contribution->depth + 127) / 255;
            } else if (contribution->direction ==
                       PRESET_MORPH_LFO_DIRECTION_MAIN) {
                effective -= ((int32_t)base * contribution->depth + 127) /
                             255;
            }
        }
    }
    if (effective < 0)
        effective = 0;
    else if (effective > 255)
        effective = 255;
    return (uint8_t)effective;
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
    for (slot = 0u; slot < INSTRUMENT_SLOT_COUNT; slot++) {
        /*
         * Snapshot the effective base for this bounded pass.
         *
         * Inputs: retained Scene amount or an active step-automation
         * override. Output: a stable pass amount; a new step request is
         * queued for the next complete pass rather than changing this pass
         * halfway through its descriptor walk.
         */
        morph_worker.pass_amount[slot] =
            presetMorph_effectiveVoiceBase(scene, slot);
    }
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
    morph_worker.pass_lfo_resolved_mask = 0u;
    morph_worker.priority_resume_valid = 0u;
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
    morph_worker.pass_lfo_resolved_mask = 0u;
    morph_worker.priority_resume_valid = 0u;
    morph_worker.priority_slot = 0u;
    morph_worker.resume_slot = 0u;
    morph_worker.resume_descriptor_index = 0u;
    for (uint8_t slot = 0u; slot < INSTRUMENT_SLOT_COUNT; slot++) {
        morph_worker.pass_amount[slot] = 0u;
        morph_step_override[slot].active = 0u;
        morph_step_override[slot].amount = 0u;
    }
    for (uint8_t target = 0u; target < INSTRUMENT_SLOT_COUNT; target++) {
        for (uint8_t source = 0u; source < INSTRUMENT_SLOT_COUNT; source++) {
            for (uint8_t pair = 0u; pair < 2u; pair++) {
                morph_lfo_contributions[target][source][pair].direction =
                    PRESET_MORPH_LFO_DIRECTION_NONE;
                morph_lfo_contributions[target][source][pair].depth = 0u;
            }
        }
    }
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
        morph_worker.priority_resume_valid = 0u;
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
        morph_worker.priority_resume_valid = 0u;
        morph_worker.active = 0u;
    }
    morph_worker.requested_mask |= PRESET_MORPH_ALL_SLOTS_MASK;
    if (!morph_worker.active)
        presetMorph_beginPass();
}

void presetMorph_prioritizeVoice(uint8_t scene_index, uint8_t slot)
{
    const scene_t *scene = scene_getConst(scene_index);
    uint8_t bit;

    /*
     * Move one queued voice to the front of the bounded Morph sweep.
     *
     * Inputs: Scene/slot that has become urgent, normally because its deferred
     * Scene-switch slot is about to trigger. Output: the slot is guaranteed to
     * be in the current pass mask, removed from the later request mask, and the
     * worker cursor is moved to descriptor zero for that slot. If another slot
     * was mid-sweep, its slot/descriptor cursor is saved once and restored when
     * the priority slot completes.
     */
    if (!scene || slot >= INSTRUMENT_SLOT_COUNT)
        return;

    if (morph_worker.scene_index != scene_index) {
        /*
         * A priority request for a different Scene replaces the old worker.
         *
         * Input: the active/urgent Scene changed. Output: stale pass/request
         * masks are discarded because descriptor writes from the old Scene must
         * never resume after this Scene's trigger-time switch.
         */
        morph_worker.scene_index = scene_index;
        morph_worker.requested_mask = 0u;
        morph_worker.pass_mask = 0u;
        morph_worker.priority_resume_valid = 0u;
        morph_worker.active = 0u;
    }

    bit = PRESET_MORPH_SLOT_MASK(slot);
    morph_worker.requested_mask =
        (uint8_t)(morph_worker.requested_mask & ~bit);
    morph_worker.pass_mask = (uint8_t)(morph_worker.pass_mask | bit);
    /*
     * Priority application must use the same effective base as an ordinary
     * worker pass. Otherwise a trigger-time synchronous apply could briefly
     * replace a step-automation Morph value with retained Scene data.
     */
    morph_worker.pass_amount[slot] =
        presetMorph_effectiveVoiceBase(scene, slot);
    morph_worker.pass_lfo_resolved_mask =
        (uint8_t)(morph_worker.pass_lfo_resolved_mask & ~bit);

    if (!morph_worker.active) {
        morph_worker.slot = slot;
        morph_worker.descriptor_index = 0u;
        morph_worker.active = 1u;
        return;
    }

    if (morph_worker.slot != slot &&
        !morph_worker.priority_resume_valid &&
        morph_worker.slot < INSTRUMENT_SLOT_COUNT) {
        /*
         * Save the interrupted cursor once.
         *
         * Inputs: the current worker slot/descriptor index. Output: after the
         * priority slot finishes, presetMorph_tick() can continue from the same
         * descriptor position instead of restarting the whole pass or skipping
         * the old slot's remaining morphable cells.
         */
        morph_worker.resume_slot = morph_worker.slot;
        morph_worker.resume_descriptor_index = morph_worker.descriptor_index;
        morph_worker.priority_slot = slot;
        morph_worker.priority_resume_valid = 1u;
    }

    morph_worker.slot = slot;
    morph_worker.descriptor_index = 0u;
}

/*
 * Write one Morph-derived base value to the voice runtime, unless step
 * automation holds that parameter (S075 F3).
 *
 * What: for the active Scene, applies `value` through
 * preset_applyInstrumentRuntimeValue(), except when
 * seq_automationHoldsParameter(slot, local) is set. The caller has already
 * stored `value` in morph_interpolation[local], which is what the next
 * trigger restores, so a held parameter picks up the new base at that trigger
 * instead of losing its automation mid-note.
 * Why: automation always wins (user rule). A menu edit, a Morph change, an LFO
 * on Morph, `Nvm` automation, a MIDI CC or an Instrument/Kit apply can queue
 * the whole voice; before S075 F3 every automated parameter of that voice
 * snapped back to its base while the note was still sounding.
 * Inputs: Scene, slot 0..5, descriptor-local index, interpolated value.
 * Output: one runtime write, or none (inactive Scene, held parameter).
 * Callers: presetMorph_tick(), presetMorph_applyVoiceNow(),
 * presetMorph_applyParameterNow(). Affiliates: seq_restoreAutomatedParameters(),
 * seq_restoreAllAutomation(), preset_applyInstrumentRuntimeValue().
 */
static void presetMorph_writeRuntimeBase(uint8_t scene_index, uint8_t slot,
                                         uint8_t local,
                                         instrument_param_value_t value)
{
    if (scene_index != scene_getActiveIndex())
        return;
    if (seq_automationHoldsParameter(slot, local))
        return;
    (void)preset_applyInstrumentRuntimeValue(
        scene_index, instrumentParam_make(slot, local), value);
}

uint8_t presetMorph_tick(void)
{
    scene_t *scene;

    if (!morph_worker.active)
        return 0u;
    scene = scene_get(morph_worker.scene_index);
    if (!scene) {
        morph_worker.active = 0u;
        morph_worker.priority_resume_valid = 0u;
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

        if (morph_worker.descriptor_index == 0u &&
            !(morph_worker.pass_lfo_resolved_mask &
              PRESET_MORPH_SLOT_MASK(morph_worker.slot)) &&
            presetMorph_voiceHasLfoLayer(morph_worker.slot)) {
            /*
             * Resolve one active LFO Morph amount as its own worker item.
             *
             * Inputs: the current pass slot and stored LFO contribution table.
             * Output: pass_amount[slot] receives the effective base-plus-LFO
             * Morph amount that later descriptor interpolation will use. The
             * function returns after this voice-level resolve so an
             * LFO-modulated Morph voice adds one bounded work item, while
             * actual descriptor interpolation remains one parameter per tick.
             */
            morph_worker.pass_amount[morph_worker.slot] =
                presetMorph_resolveLfoAmount(scene, morph_worker.slot);
            morph_worker.pass_lfo_resolved_mask |=
                PRESET_MORPH_SLOT_MASK(morph_worker.slot);
            return 1u;
        }

        while (morph_worker.descriptor_index < INSTRUMENT_PARAM_COUNT) {
            uint8_t local = morph_worker.descriptor_index++;
            const ParamDescriptor *descriptor =
                instrumentManager_descriptor(instrument->type, local);
            instrument_param_value_t value;

            if (!descriptor ||
                !(descriptor->flags & INSTRUMENT_PARAM_FLAG_MORPHABLE)) {
                continue;
            }
            value = presetMorph_interpolate(
                instrument->parameter_images.instrument_parameters[local],
                instrument->parameter_images.morph_instrument_parameters[local],
                morph_worker.pass_amount[morph_worker.slot]);
            instrument->parameter_images.morph_interpolation[local] = value;
            /* Held automation keeps its runtime value until the trigger. */
            presetMorph_writeRuntimeBase(morph_worker.scene_index,
                                         morph_worker.slot, local, value);
            return 1u;
        }
        morph_worker.pass_mask &=
            (uint8_t)(~PRESET_MORPH_SLOT_MASK(morph_worker.slot));
        if (morph_worker.priority_resume_valid &&
            morph_worker.slot == morph_worker.priority_slot) {
            /*
             * Resume the interrupted bounded sweep after a priority slot.
             *
             * Inputs: priority_resume_* captured the worker cursor before the
             * urgent slot jumped ahead. Output: normal Morph processing
             * continues from the saved descriptor index, while the completed
             * priority slot stays cleared from pass_mask. If the saved slot was
             * also cleared by another synchronous apply, the top of the loop will
             * skip it and advance normally.
             */
            morph_worker.slot = morph_worker.resume_slot;
            morph_worker.descriptor_index =
                morph_worker.resume_descriptor_index;
            morph_worker.priority_resume_valid = 0u;
            continue;
        }
        morph_worker.slot++;
        morph_worker.descriptor_index = 0u;
    }

    if (morph_worker.requested_mask != 0u) {
        presetMorph_beginPass();
    } else {
        morph_worker.active = 0u;
        morph_worker.pass_mask = 0u;
        morph_worker.priority_resume_valid = 0u;
    }
    return 0u;
}

void presetMorph_rebuildScene(uint8_t scene_index)
{
    /*
     * Rebuild all Morph interpolation from retained per-voice Scene settings.
     *
     * Inputs: Scene index. Output: all slots are queued without changing the
     * Scene global Morph mirror or any per-slot amount. Clients are explicit
     * Morph rebuild/edit paths; active Scene switching uses
     * presetMorph_applyVoiceNow() per slot so instrument parameters can wait for
     * the quiet threshold or the next trigger.
     */
    presetMorph_requestAll(scene_index);
}

void presetMorph_applyVoiceNow(uint8_t scene_index, uint8_t slot)
{
    scene_t *scene = scene_get(scene_index);
    kit_instrument_slot_t *instrument;
    uint8_t local;
    uint8_t amount;

    /*
     * Commit one slot's Morph-derived runtime image immediately.
     *
     * Inputs: Scene/slot selected by the deferred Scene-switch worker. Outputs:
     * all morphable descriptor cells in morph_interpolation[] are updated, and
     * active-Scene runtime writes are complete before the caller returns. This
     * mirrors presetMorph_tick() but intentionally walks the whole slot so a
     * trigger-time Scene swap cannot fire with half-old instrument parameters.
     * Parameters held by step automation keep their runtime value; only
     * morph_interpolation[] is updated for them (S075 F3,
     * presetMorph_writeRuntimeBase()).
     */
    if (!scene || slot >= INSTRUMENT_SLOT_COUNT)
        return;
    instrument = &scene->kit.instruments[slot];
    /*
     * Synchronous Scene-switch application also honors a live step overlay.
     * The overlay is normally restored before a transport/Pattern boundary,
     * but this guard keeps trigger-time priority behavior consistent if a
     * caller applies a slot while the overlay is still active.
     */
    amount = presetMorph_effectiveVoiceBase(scene, slot);
    if (presetMorph_voiceHasLfoLayer(slot))
        amount = presetMorph_resolveLfoAmount(scene, slot);

    for (local = 0u; local < INSTRUMENT_PARAM_COUNT; local++) {
        const ParamDescriptor *descriptor =
            instrumentManager_descriptor(instrument->type, local);
        instrument_param_value_t value;

        /*
         * Walk descriptor indices rather than raw storage cells.
         *
         * Input: descriptor flags for the slot's stored type. Output: only
         * morphable cells are interpolated/applied; supplemental selectors such
         * as LFO target voice/param stay with Preset's slot supplemental apply.
         */
        if (!descriptor ||
            !(descriptor->flags & INSTRUMENT_PARAM_FLAG_MORPHABLE)) {
            continue;
        }
        value = presetMorph_interpolate(
            instrument->parameter_images.instrument_parameters[local],
            instrument->parameter_images.morph_instrument_parameters[local],
            amount);
        instrument->parameter_images.morph_interpolation[local] = value;
        /* Held automation keeps its runtime value until the trigger. */
        presetMorph_writeRuntimeBase(scene_index, slot, local, value);
    }

    if (morph_worker.scene_index == scene_index) {
        /*
         * Remove the synchronously committed slot from any bounded pass.
         *
         * Inputs: the worker may already have queued this slot before a
         * trigger-time apply. Output: both pending masks drop the slot bit so a
         * later foreground tick does not replay stale descriptor writes over the
         * freshly committed runtime.
         */
        morph_worker.requested_mask =
            (uint8_t)(morph_worker.requested_mask &
                      ~PRESET_MORPH_SLOT_MASK(slot));
        morph_worker.pass_mask =
            (uint8_t)(morph_worker.pass_mask &
                      ~PRESET_MORPH_SLOT_MASK(slot));
        if (morph_worker.priority_resume_valid &&
            morph_worker.priority_slot == slot) {
            /*
             * A synchronous priority apply completes outside presetMorph_tick().
             *
             * Inputs: trigger-time Scene switching may call
             * presetMorph_prioritizeVoice(), then immediately call this helper
             * to commit the whole slot before the note fires. Output: the
             * bounded worker resumes at the saved cursor on its next tick
             * instead of remaining parked on a slot whose pass bit was removed.
             */
            morph_worker.slot = morph_worker.resume_slot;
            morph_worker.descriptor_index =
                morph_worker.resume_descriptor_index;
            morph_worker.priority_resume_valid = 0u;
        }
    }
}

void presetMorph_setVoiceLfoModulation(uint8_t scene_index,
                                       uint8_t target_slot,
                                       uint8_t source_slot,
                                       uint8_t target_pair,
                                       PresetMorphLfoDirection direction,
                                       uint8_t depth)
{
    /*
     * Store one hidden LFO Morph contribution and queue its target voice.
     *
     * Inputs: Scene index, target voice slot, source LFO slot, target pair,
     * endpoint direction, and base-independent normalized depth. Output: the
     * contribution table is updated and the target voice is queued for bounded
     * Morph apply. This does not touch SceneData or PERF mirrors, preserving
     * the difference between retained base Morph and the invisible LFO layer.
     * NONE/0 clears the selected contribution without a separate setter.
     */
    if (!scene_getConst(scene_index) ||
        target_slot >= INSTRUMENT_SLOT_COUNT ||
        source_slot >= INSTRUMENT_SLOT_COUNT ||
        target_pair > 1u) {
        return;
    }
    if (direction > PRESET_MORPH_LFO_DIRECTION_MORPH)
        direction = PRESET_MORPH_LFO_DIRECTION_NONE;
    morph_lfo_contributions[target_slot][source_slot][target_pair].direction =
        (uint8_t)direction;
    morph_lfo_contributions[target_slot][source_slot][target_pair].depth =
        depth;
    presetMorph_requestVoice(scene_index, target_slot);
}

void presetMorph_clearLfoSource(uint8_t source_slot, uint8_t target_pair)
{
    uint8_t target;
    uint8_t scene_index = scene_getActiveIndex();

    /*
     * Remove one source LFO pair from every hidden Morph target.
     *
     * Inputs: source slot and pair whose modulation destination was cleared or
     * changed. Output: each affected target voice is queued so the worker
     * recomputes from remaining contributions or from the current effective
     * base (step override when active, otherwise retained Scene data). This
     * helper exists because install/clear code should not have to remember
     * which voice the old Scene target pointed at.
     */
    if (source_slot >= INSTRUMENT_SLOT_COUNT || target_pair > 1u)
        return;
    for (target = 0u; target < INSTRUMENT_SLOT_COUNT; target++) {
        if (morph_lfo_contributions[target][source_slot][target_pair].direction !=
            PRESET_MORPH_LFO_DIRECTION_NONE) {
            morph_lfo_contributions[target][source_slot][target_pair].direction =
                PRESET_MORPH_LFO_DIRECTION_NONE;
            morph_lfo_contributions[target][source_slot][target_pair].depth = 0u;
            presetMorph_requestVoice(scene_index, target);
        }
    }
}

uint8_t presetMorph_getEffectiveVoiceAmount(uint8_t scene_index,
                                             uint8_t slot)
{
    const scene_t *scene = scene_getConst(scene_index);

    /*
     * Read the effective Morph base used by the live runtime display.
     *
     * Inputs: resident Scene index and zero-based voice slot. Output: the
     * active step-automation base when present, otherwise the retained Scene
     * Morph amount. LFO direction/depth composition remains private to the
     * bounded worker; this bridge exposes only the step-versus-retained base
     * needed by the live Scene superpage without exposing worker storage.
     */
    if (!scene || slot >= INSTRUMENT_SLOT_COUNT)
        return 0u;
    return presetMorph_effectiveVoiceBase(scene, slot);
}

uint8_t presetMorph_getResolvedVoiceAmount(uint8_t scene_index,
                                           uint8_t slot)
{
    const scene_t *scene = scene_getConst(scene_index);

    /*
     * Resolve the same base/LFO amount used by the Morph worker (contract in
     * presetMorphEngine.h, S075 F2-H). LFO contributions are runtime state of
     * the active Scene only; another resident Scene resolves to its base.
     * Equal FX-send endpoints avoid calling this getter in the mixer.
     */
    if (!scene || slot >= INSTRUMENT_SLOT_COUNT)
        return 0u;
    if (scene_index == scene_getActiveIndex() &&
        presetMorph_voiceHasLfoLayer(slot))
        return presetMorph_resolveLfoAmount(scene, slot);
    return presetMorph_effectiveVoiceBase(scene, slot);
}

/*
 * Re-interpolate and apply one voice parameter now (S075 F3).
 *
 * What: after a menu edit of one Normal or Morph endpoint, computes that
 * parameter's interpolated value with the amount the voice is playing with
 * (presetMorph_getResolvedVoiceAmount(): step override or retained amount,
 * plus any LFO layer on the active Scene), stores it in
 * morph_interpolation[local], and writes it to the runtime unless step
 * automation holds the parameter.
 * Why: a menu edit only sets an endpoint, and the sound follows the
 * interpolation, never the raw edited value. Automation always wins. Only
 * this parameter's interpolation can change, so the whole voice is not
 * queued and no other parameter is rewritten. The edit is heard at once in
 * both views.
 * Inputs: resident Scene, slot 0..5, descriptor-local index of a morphable
 * parameter. Outputs: morph_interpolation[local] (any Scene); a runtime write
 * (active Scene, parameter not held). Non-morphable or invalid input: no-op.
 * Client: preset_setInstrumentParameter() (menu edits). Affiliates:
 * presetMorph_tick() (same maths), seq_automationHoldsParameter().
 */
void presetMorph_applyParameterNow(uint8_t scene_index, uint8_t slot,
                                   uint8_t local)
{
    scene_t *scene = scene_get(scene_index);
    kit_instrument_slot_t *instrument;
    const ParamDescriptor *descriptor;
    instrument_param_value_t value;

    /*
     * Contract in presetMorphEngine.h. Same interpolation as the sweep
     * (presetMorph_interpolate() over the retained Normal/Morph images) at
     * the resolved amount, so the sweep later writes the same value.
     */
    if (!scene || slot >= INSTRUMENT_SLOT_COUNT ||
        local >= INSTRUMENT_PARAM_COUNT)
        return;
    instrument = &scene->kit.instruments[slot];
    descriptor = instrumentManager_descriptor(instrument->type, local);
    if (!descriptor ||
        !(descriptor->flags & INSTRUMENT_PARAM_FLAG_MORPHABLE))
        return;
    value = presetMorph_interpolate(
        instrument->parameter_images.instrument_parameters[local],
        instrument->parameter_images.morph_instrument_parameters[local],
        presetMorph_getResolvedVoiceAmount(scene_index, slot));
    instrument->parameter_images.morph_interpolation[local] = value;
    presetMorph_writeRuntimeBase(scene_index, slot, local, value);
}

void presetMorph_setStepAutomationOverride(uint8_t scene_index,
                                           uint8_t slot, uint8_t amount)
{
    /*
     * Set one voice's transient step-automation Morph base.
     *
     * Inputs: active Scene index, zero-based voice slot, and 0..255 Morph
     * amount. Output: the bounded Morph worker replaces the retained Scene
     * base for this slot without changing SceneData, AutoSave state, or PERF
     * retained values. LFO contributions are composed around this base.
     * Client: seq_applySceneAutomation(); restore: clearAll below.
     */
    if (slot >= INSTRUMENT_SLOT_COUNT || !scene_getConst(scene_index))
        return;
    morph_step_override[slot].active = 1u;
    morph_step_override[slot].amount = amount;
    presetMorph_requestVoice(scene_index, slot);
}

void presetMorph_clearAllStepAutomationOverrides(uint8_t scene_index)
{
    uint8_t slot;
    uint8_t any = 0u;

    /*
     * Clear every transient step-automation Morph base.
     *
     * Inputs: active Scene index. Output: overridden slots are queued for a
     * complete rebuild from retained Scene endpoint values; no retained Scene
     * or AutoSave value is written. Client: transport-boundary Scene restore.
     */
    for (slot = 0u; slot < INSTRUMENT_SLOT_COUNT; slot++) {
        if (morph_step_override[slot].active) {
            morph_step_override[slot].active = 0u;
            morph_step_override[slot].amount = 0u;
            any = 1u;
        }
    }
    if (any && scene_getConst(scene_index))
        presetMorph_rebuildScene(scene_index);
}

/*
 * Clear the step-automation Morph base for one voice (S076 Rule A).
 *
 * What: deactivates morph_step_override[slot] so the effective Morph base
 * falls back to the retained Scene amount. Unlike the clearAll helper this
 * does NOT queue a rebuild; the caller is a retained-value setter that will
 * queue its own rebuild or retained-base commit immediately after.
 * Why: a non-automation write (menu edit, MIDI CC1, copy/clear, type change)
 * must supersede any active step override for that slot so the user's edit
 * is audible and visible. The full transport-boundary clear
 * (presetMorph_clearAllStepAutomationOverrides) continues to own bulk
 * restore with rebuild.
 * Inputs: zero-based instrument slot 0..INSTRUMENT_SLOT_COUNT-1.
 * Output: the slot's morph_step_override.active is set to 0. Out-of-range
 * slot is a no-op.
 * Callers: preset_morphVoiceScene(), preset_morphScene().
 * Affiliates: presetMorph_setStepAutomationOverride() (the setter),
 * presetMorph_clearAllStepAutomationOverrides() (the transport clear),
 * presetMorph_effectiveVoiceBase() (the consumer that checks .active),
 * presetMorph_getEffectiveVoiceAmount() (the display bridge).
 */
void presetMorph_clearStepAutomationOverride(uint8_t slot)
{
    if (slot >= INSTRUMENT_SLOT_COUNT)
        return;
    morph_step_override[slot].active = 0u;
}
