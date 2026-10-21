/*
 * Core/DSP/Instruments/PlaitsKick/PlaitsKickVoice.h
 *
 *  Created on: 21.10.2026
 * -----------------------------------------------------------------------------
 *  The kick DSP is a C translation of Plaits' AnalogBassDrum and Overdrive
 *  (plaits/dsp/drums/analog_bass_drum.h, plaits/dsp/fx/overdrive.h):
 *
 *  Copyright 2016 Emilie Gillet.
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a
 *  copy of this software and associated documentation files (the "Software"),
 *  to deal in the Software without restriction, including without limitation
 *  the rights to use, copy, modify, merge, publish, distribute, sublicense,
 *  and/or sell copies of the Software, and to permit persons to whom the
 *  Software is furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 *  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 *  DEALINGS IN THE SOFTWARE.
 * -----------------------------------------------------------------------------
 *  This file is part of the LXR02 Open-Source software.
 * -----------------------------------------------------------------------------
 */

#ifndef PLAITS_KICK_VOICE_H_
#define PLAITS_KICK_VOICE_H_

#include "config.h"
#include "distortion.h"
#include "SlopeEg2.h"
#include "lfo.h"

/*
 * Plaits analog kick runtime (one tagged InstrumentManager slot member).
 *
 * What: the bridged-T kick of Plaits (trigger pulse into a resonant band-pass
 * whose frequency is pushed by an attack FM pulse and by its own output),
 * Plaits' overdrive, and the LXR voice parts every engine shares so the
 * generic code needs no special case: the amp envelope (Scene-switch quiet
 * test, track-7 alternate decay), velocity, distortion, LFO, vol and pan.
 * The kick is self-enveloped; the amp envelope defaults fully open.
 *
 * Rows (PlaitsKickParameters.c) write: note, fine (raw bytes), harmonics,
 * tone, decay (0..1, the Plaits Harmonics/Timbre/Morph macros), and the shared
 * rows. Every DSP coefficient is derived once per block in
 * PlaitsKick_calcVoiceAsync(), so an LFO costs nothing extra.
 * Accessors: PlaitsKickVoice.c, InstrumentManager.c dispatchers.
 */
typedef struct {
	/* Plaits AnalogBassDrum state. */
	int32_t pulse_remaining;
	int32_t fm_pulse_remaining;
	float pulse;
	float pulse_height;
	float pulse_lp;
	float fm_pulse_lp;
	float retrig_pulse;
	float lp_out;
	float tone_lp;
	float svf_s1;
	float svf_s2;
	/* Plaits Overdrive gain ramps. */
	float od_pre;
	float od_post;

	/* Per-block coefficients (PlaitsKick_calcVoiceAsync). */
	float f0;
	float q;
	float tone_f;
	float exciter_leak;
	float scale;
	float attack_fm;
	float self_fm;
	float drive;
	float accent;

	/* Row values. */
	float harmonics;
	float tone;
	float decay;
	uint8_t note;
	uint8_t fine;
	uint8_t baseNote;
	uint8_t trigger;

	/* Shared LXR voice parts. */
	float vol;
	float velo;
	uint8_t pan;
	uint8_t volumeMod;
	Lfo lfo;
	SlopeEg2 oscVolEg;
	float ampFilterInput;
	float lastGain;
	Distortion distortion;
} PlaitsKickVoice;

void PlaitsKick_initVoice(PlaitsKickVoice *voice);
void PlaitsKick_triggerVoice(PlaitsKickVoice *voice, const uint8_t source_slot,
                             const uint8_t vol, const uint8_t note);
void PlaitsKick_calcVoiceAsync(PlaitsKickVoice *voice);
void PlaitsKick_calcVoiceSyncBlock(PlaitsKickVoice *voice, int16_t *buf,
                                   const uint8_t size);

#endif /* PLAITS_KICK_VOICE_H_ */
