/*
 * Core/Hardware/AudioCodecManager.h
 *
 *  Created on: 17.05.2026
 * ------------------------------------------------------------------------------------------------------------------------
 *  Copyright 2026 Brendan Clarke
 *  brendanpaulclarke@gmail.com
 *  https://www.brendanclarke.com
 * ------------------------------------------------------------------------------------------------------------------------
 *  This file is part of the LXR02 Open-Source software.
 * ------------------------------------------------------------------------------------------------------------------------
 *  Redistribution and use of the LXR02 Open-Source, hardware driver code, or any derivative works are permitted
 *  provided that the following conditions are met:
 *
 *       - The code may not be sold, nor may it be used in a commercial product or activity.
 *
 *       - Redistributions that are modified from the original source must include the complete
 *         source code, including the source code for all components used by a binary built
 *         from the modified sources. However, as a special exception, the source code distributed
 *         need not include anything that is normally distributed (in either source or binary form)
 *         with the major components (compiler, kernel, and so on) of the operating system on which
 *         the executable runs, unless that component itself accompanies the executable.
 *
 *       - Redistributions must reproduce the above copyright notice, this list of conditions and the
 *         following disclaimer in the documentation and/or other materials provided with the distribution.
 * ------------------------------------------------------------------------------------------------------------------------
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 *   INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *   DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 *   SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 *   WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 *   USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * ------------------------------------------------------------------------------------------------------------------------
 */

/*
 * AudioCodecManager.h — LXR-02 audio path manager (STM32F765 port).
 *
 * Public API for the audio codec manager. All audio hardware setup and
 * buffer management lives in AudioCodecManager.c. Call audioCodec_init()
 * once from main() before the main loop; everything else is driven by
 * DMA ISRs.
 *
 * Main-loop pattern:
 *   if (audioCodec_queueFreeSlots() > 0) {
 *       fill audioCodec_getRenderBuffer*() with AUDIO_DMA_FRAMES samples
 *       audioCodec_commitRenderBuffer();
 *   }
 *
 * Buffer layout:
 *   audioOutBuffer  [2][AUDIO_DMA_FRAMES*2] — DAC2 (I2S2, U24)
 *   audioOutBuffer2 [2][AUDIO_DMA_FRAMES*2] — DAC1 (I2S3, U23)
 *   Each slot: sample_mx_t interleaved stereo [L0,R0,L1,R1,...].
 *
 * 24-bit pack:
 *   Mixer writes widened sample_mx_t samples; the DMA ISR packs signed
 *   24-bit payloads as MSW+LSW pairs into dma_buffer / dma_buffer2.
 */

#ifndef AUDIOCODECMANAGER_H_
#define AUDIOCODECMANAGER_H_

#include "config.h"
#include "globals.h"
#include "sample_mix.h"
#include <stdint.h>

#if DMA_MODE_ACTIVE
/* DMA-side buffers, 24-bit packed. Layout per half:
**   AUDIO_DMA_FRAMES stereo frames x 4 halfwords each:
**     [MSW_L, LSW_L, MSW_R, LSW_R, ...]
**   Total: AUDIO_DMA_FRAMES * 8 halfwords (two halves). */
extern volatile int16_t dma_buffer  [AUDIO_DMA_FRAMES * 8];
extern volatile int16_t dma_buffer2 [AUDIO_DMA_FRAMES * 8];
#endif

/* Initialise the complete audio path (PLLI2S, GPIO, DMA, I2S).
** Call once from main() before the main loop. */
void audioCodec_init(void);

/* Modal stop/start helpers for operations that cannot run concurrently with
** audio, such as internal flash erase/program during sample install. */
void audioCodec_suspend(void);
void audioCodec_resume(void);

/* Legacy buffer-reset wrapper, retained for DSP source compatibility.
** Does not touch hardware. Prefer audioCodec_init() for a full reset. */
int  CodecInit(void);

/* Return a pointer to the current render slot in audioOutBuffer* for the
** caller to fill via mixer_calcNextSampleBlock(). */
sample_mx_t *audioCodec_getRenderBuffer(void);
sample_mx_t *audioCodec_getRenderBuffer2(void);

/* Number of free render slots: 0 (full), 1, or 2 (empty). */
uint8_t audioCodec_queueFreeSlots(void);

/* Percentage of elapsed CPU-cycle time, since the previous call, where the
** audio ready queue had at least one free slot. This is an audio refill
** pressure metric: higher values mean the foreground loop spent more time
** with audio work available. */
uint8_t audioCodec_getQueueFreePercent(void);

/* Commit the current render slot to the SPSC ready queue.
** After this call, the ISR may consume the slot at any time. */
void audioCodec_commitRenderBuffer(void);

/* Diagnostic counters. Read from main loop or test functions. */
extern volatile uint32_t audioCodec_underrunCount;
extern volatile uint32_t audioCodec_renderCount;

/* Test utility: generate a 440Hz sine wave into the current render slot
** and commit it. Used for audio path bringup before the mixer is ported.
** See the TEST FUNCTIONS block in main.c for usage. */
void audioCodec_renderSineBlock(void);

#endif /* AUDIOCODECMANAGER_H_ */
