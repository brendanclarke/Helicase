#ifndef PRESET_MORPH_ENGINE_H_
#define PRESET_MORPH_ENGINE_H_

#include <stdint.h>

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
 * pair index, active flag, and the LFO-shaped Morph amount in the 0..255
 * domain. Output: the Morph worker records that one source contribution and
 * queues the target voice, but it does not mutate
 * SceneData.settings.voice_morph_amount[] and does not update PERF menu
 * values.
 *
 * This API exists because LFO Morph modulation is not the same operation as
 * preset_morphVoice(). Menu, velocity, and MIDI CC1 set the retained base
 * Morph value. LFO modulation is a secondary layer centered on that base and
 * must be consumed by the bounded Morph worker so it never interpolates an
 * entire voice immediately from the audio/LFO dispatch path.
 */
void presetMorph_setVoiceLfoModulation(uint8_t scene_index,
                                       uint8_t target_slot,
                                       uint8_t source_slot,
                                       uint8_t target_pair,
                                       uint8_t active,
                                       uint8_t amount);
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

#endif
