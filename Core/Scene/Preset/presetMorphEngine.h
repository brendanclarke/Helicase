#ifndef PRESET_MORPH_ENGINE_H_
#define PRESET_MORPH_ENGINE_H_

#include <stdint.h>

/*
 * Rate-limited Scene Morph worker.
 *
 * Requests only record the desired Scene amount and generation. tick() does
 * at most one descriptor-backed interpolation/application, which keeps DSP
 * work bounded in the foreground loop. The interpolation image is runtime
 * state and is rebuilt after every load rather than serialized.
 */
void presetMorph_init(void);
void presetMorph_request(uint8_t scene_index, uint8_t morph_amount);
uint8_t presetMorph_tick(void);
void presetMorph_rebuildScene(uint8_t scene_index);

#endif
