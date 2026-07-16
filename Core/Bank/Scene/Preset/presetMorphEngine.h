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
uint8_t presetMorph_tick(void);
void presetMorph_rebuildScene(uint8_t scene_index);
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
