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

#endif
