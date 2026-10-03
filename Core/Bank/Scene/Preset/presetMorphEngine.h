#ifndef PRESET_MORPH_ENGINE_H_
#define PRESET_MORPH_ENGINE_H_

#include <stdint.h>

/*
 * Base-independent direction for one hidden LFO voice-Morph contribution.
 *
 * Inputs: InstrumentManager converts LFO polarity/source/amount into this
 * direction plus a normalized 0..255 depth. Output: the Morph worker computes
 * the signed delta from the effective base when it resolves the contribution.
 * NONE is inactive, MAIN moves toward amount 0, and MORPH moves toward amount
 * 255. The representation replaces the former active+absolute-amount pair
 * without increasing the contribution table's two-byte entry size.
 *
 * Affiliate: presetMorph_resolveLfoAmount() consumes this enum and the depth;
 * presetMorph_clearLfoSource() writes NONE/0 during route teardown.
 */
typedef enum {
    PRESET_MORPH_LFO_DIRECTION_NONE = 0,
    PRESET_MORPH_LFO_DIRECTION_MAIN,
    PRESET_MORPH_LFO_DIRECTION_MORPH
} PresetMorphLfoDirection;

/*
 * Rate-limited Scene Morph worker.
 *
 * Requests queue dirty instrument slots. tick() does at most one
 * descriptor-backed interpolation/application, which keeps DSP work bounded in
 * the foreground loop. The interpolation image is runtime state and is rebuilt
 * after every load rather than serialized.
 *
 * Accessors and clients: Preset setters mutate SceneData, then call
 * presetMorph_requestVoice() or presetMorph_requestAll(). The worker reads the
 * retained per-slot amounts when a pass begins. This interface stays separate
 * from Preset's public setters because Morph has to walk swappable instrument
 * slots and descriptor flags over time instead of applying one caller-selected
 * cell.
 */
void presetMorph_init(void);
void presetMorph_requestVoice(uint8_t scene_index, uint8_t slot);
void presetMorph_requestAll(uint8_t scene_index);
/*
 * Move one queued voice to the front of the bounded Morph worker.
 *
 * Inputs: resident Scene index and zero-based slot. Output: if the Morph worker
 * is already sweeping that Scene, the current cursor is saved, the requested
 * slot becomes the next work item, and the saved cursor resumes after that
 * slot finishes. If the worker is idle, this behaves like requestVoice().
 *
 * Client: deferred Scene switching asks for this immediately before a triggered
 * pending slot is force-applied. The force-apply path still computes the slot
 * synchronously from retained endpoints before the trigger; this priority API
 * keeps any concurrent bounded sweep from spending foreground ticks on less
 * urgent voices first.
 */
void presetMorph_prioritizeVoice(uint8_t scene_index, uint8_t slot);
uint8_t presetMorph_tick(void);
void presetMorph_rebuildScene(uint8_t scene_index);
/*
 * Synchronously rebuild one voice's Morph interpolation.
 *
 * Inputs: resident Scene index and zero-based slot. Output: the slot's
 * morph_interpolation[] image is rebuilt from the stored normal/morph endpoints
 * and the effective voice Morph amount; if the Scene is active, each rebuilt
 * descriptor is also written to the current DSP runtime before this function
 * returns.
 *
 * Client: deferred Scene switching. When the new Scene pattern triggers a slot
 * that has not yet reached the quiet threshold, Preset must swap that slot's
 * instrument parameters before the trigger is dispatched, so the bounded worker
 * cannot be used for that one-slot path.
 */
void presetMorph_applyVoiceNow(uint8_t scene_index, uint8_t slot);
/*
 * Set the hidden LFO Morph layer for one target voice.
 *
 * Inputs: Scene index, zero-based target voice slot, source LFO slot, target
 * pair index, base-independent direction, and normalized depth in the 0..255
 * domain. Output: the Morph worker records that one source contribution and
 * queues the target voice, but it does not mutate
 * SceneData.settings.voice_morph_amount[] and does not update PERF menu
 * values.
 *
 * This API exists because LFO Morph modulation is not the same operation as
 * preset_morphVoice(). Menu, velocity, and MIDI CC1 set the retained base
 * Morph value. LFO modulation is a secondary layer centered on the current
 * effective base, including a step-automation base when present, and must be
 * consumed by the bounded Morph worker so it never interpolates an entire
 * voice immediately from the audio/LFO dispatch path.
 */
void presetMorph_setVoiceLfoModulation(uint8_t scene_index,
                                       uint8_t target_slot,
                                       uint8_t source_slot,
                                       uint8_t target_pair,
                                       PresetMorphLfoDirection direction,
                                       uint8_t depth);
/*
 * Clear one LFO source/pair from every hidden Morph target.
 *
 * Inputs: source slot and target pair whose installed destination was changed
 * or disabled. Output: any voice receiving that contribution is queued so the
 * worker can fall back to its retained base Morph amount. This is separate from
 * presetMorph_setVoiceLfoModulation() because target installation changes know
 * the old source/pair but may no longer know which voice it previously drove.
 */
void presetMorph_clearLfoSource(uint8_t source_slot, uint8_t target_pair);

/*
 * Read the effective runtime Morph base for one voice.
 *
 * Inputs: resident Scene index and zero-based voice slot. Output: the active
 * step-automation Morph overlay when present, otherwise the retained Scene
 * amount. This is a read-only bridge for Menu's live Scene superpage and
 * diagnostics; LFO direction/depth resolution remains inside the bounded
 * Morph worker. It never changes SceneData or AutoSave state.
 */
uint8_t presetMorph_getEffectiveVoiceAmount(uint8_t scene_index,
                                             uint8_t slot);

/*
 * Read the resolved Morph amount of one voice (S075 F2-H).
 *
 * What: the amount the Morph worker uses: the step override or retained base,
 * plus active LFO contributions when this is the active Scene. Read-only; it
 * never changes SceneData, AutoSave or worker state. Output is 0..255, or 0
 * for an invalid Scene/slot. Client: preset_getEffectiveFxSendAmount().
 */
uint8_t presetMorph_getResolvedVoiceAmount(uint8_t scene_index,
                                           uint8_t slot);
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
 * interpolation, never the raw edited value, so with Morph above 0 the sound
 * changes by less than the edit. Automation always wins. Only this parameter's
 * interpolation can change, so the whole voice is not queued and no other
 * parameter is rewritten. The edit is heard at once in both views.
 * Inputs: resident Scene, slot 0..5, descriptor-local index of a morphable
 * parameter. Outputs: morph_interpolation[local] (any Scene); a runtime write
 * (active Scene, parameter not held). Non-morphable or invalid input: no-op.
 * Client: preset_setInstrumentParameter() (menu edits). Affiliates:
 * presetMorph_tick() (same maths), seq_automationHoldsParameter().
 */
void presetMorph_applyParameterNow(uint8_t scene_index, uint8_t slot,
                                   uint8_t local);

/*
 * Set/clear step-automation Morph overlays.
 *
 * Step automation replaces a retained Morph base only in the runtime worker;
 * LFO contributions are centered on that transient base. Clearing all
 * overlays queues restoration from retained Scene values at transport or
 * Pattern boundaries without marking the Scene dirty.
 */
void presetMorph_setStepAutomationOverride(uint8_t scene_index,
                                           uint8_t slot, uint8_t amount);
void presetMorph_clearAllStepAutomationOverrides(uint8_t scene_index);

#endif
