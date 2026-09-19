/*
 * CymbalVoice.h
 *
 *  Created on: 16.06.2012
 * ------------------------------------------------------------------------------------------------------------------------
 *  Copyright 2013 Julian Schmidt
 *  Julian@sonic-potions.com
 * ------------------------------------------------------------------------------------------------------------------------
 *  This file is part of the Sonic Potions LXR drumsynth firmware.
 * ------------------------------------------------------------------------------------------------------------------------
 *  Redistribution and use of the LXR code or any derivative works are permitted
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


#ifndef CYMBALVOICE_H_
#define CYMBALVOICE_H_

#include "stm32f4xx.h"
#include "Oscillator.h"
#include "config.h"
#include "ResonantFilter.h"
#include "SlopeEg2.h"
#include "random.h"
#include "transientGenerator.h"
#include "BufferTools.h"
#include "lfo.h"
#include "distortion.h"
#include "snapEg.h"

typedef struct CymbalStruct
{

	OscInfo 	osc;		// the tonal oscillator
	OscInfo 	modOsc;		// the mod osc1
	OscInfo 	modOsc2;	// the mod osc2
	float		fmModAmount1;
	float		fmModAmount2;

	float	 	vol;		// volume of the voice
	//float		panL;		// [0:1]
	//float		panR;		// [0:1]
	uint8_t pan;
	//float 		panModifier;

	float 		velo;

	int32_t		noiseSample;

	ResonantFilter filter;
	uint8_t		filterType;

	Lfo 		lfo;
	TransientGenerator transGen;
	Distortion distortion;

	SlopeEg2	oscVolEg;
	float 		egValueOscVol;

	uint8_t 	volumeMod;	//modulate volume by midi velocity if 1

	SnapEg 		snapEg;

} CymbalVoice;

/*
 * Pointer-based cymbal runtime entry points for tagged instrument slots.
 *
 * Inputs: a caller-owned CymbalVoice instance plus the logical source slot for
 * trigger/LFO/velocity affiliation where needed. Outputs: the same DSP state
 * changes on the supplied instance, suitable for any slot that currently hosts
 * a cymbal. InstrumentManager owns that instance as a tagged slot member;
 * callers must not retain it across a Scene/type reset.
 */
void Cymbal_initVoice(CymbalVoice *voice);
void Cymbal_setPanVoice(CymbalVoice *voice, const uint8_t pan);
void Cymbal_triggerVoice(CymbalVoice *voice, const uint8_t source_slot,
                         const uint8_t vel, const uint8_t note);
void Cymbal_calcSyncBlockVoice(CymbalVoice *voice, int16_t* buf,
                               const uint8_t size);
void Cymbal_calcAsyncVoice(CymbalVoice *voice);


#endif /* CYMBALVOICE_H_ */
