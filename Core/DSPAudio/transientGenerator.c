/*
 * transientGenerator.c
 *
 *  Created on: 29.06.2012
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



#include "transientGenerator.h"
#include "SampleMemory.h"
#include <string.h>
//---------------------------------------------------------------
void transient_init(TransientGenerator* transient)
{
	transient->pitch 	= 1.f;
	transient->output 	= 0;
	transient->phase	= 0;
	transient->waveform	= 0;
	transient->volume	= 1.f;
};
//---------------------------------------------------------------
/*
 * Select the transient entry (S087 P3: user one-shots).
 *
 * What:       stores any row value 0..127 as it is: 0 Snp, 1 Ofs, then the
 *             installed samples from TRANSIENT_USER_FIRST (2) on (S101: the
 *             twelve built-in clicks left the firmware for the card files
 *             samples/LXR*.wav). A value past the installed samples is resolved by
 *             transient_calcBlock() to the last entry that exists. Values
 *             above 127 (not producible by the row) fall back to 0 as before.
 * Why:        the stored byte is used as it is and clamped to an existing entry
 *             at render, as the modulator wav list is (S087 P0 r8 / A23); a
 *             reinstall with fewer samples must not silence or reset the row.
 * Inputs:     waveform (the row byte).  Outputs: transient->waveform.
 * Accessors:  instrumentManager_writeSpecialRuntime() (IM_SPECIAL_TRANSIENT_WAVE).
 * Affiliates: transient_calcBlock(), transient_entryCount().
 */
void transient_setWaveform(TransientGenerator* transient, const uint8_t waveform)
{
	if(waveform < 128u)
		transient->waveform = waveform;
	else
		transient->waveform = 0;
}
//---------------------------------------------------------------
uint8_t transient_entryCount(void)
{
	const uint16_t count = (uint16_t)(TRANSIENT_USER_FIRST +
	                                  sampleMemory_getNumSamples());

	return (uint8_t)(count > 128u ? 128u : count);
}
//---------------------------------------------------------------
void transient_trigger(TransientGenerator* transient)
{
	//reset the phase to the beginning when the generator is triggered
	transient->phase	= 0;
}
//---------------------------------------------------------------
/*
 * One block of a user one-shot sample transient (S087 P3).
 *
 * What:       plays catalogue sample `index` once from the trigger: Q19.12
 *             phase (TRANSIENT_USER_PHASE_SHIFT), linear interpolation, speed
 *             = pitch (frq) x the file's own rate, level = vol x the sample's
 *             install gain code (peak-normalised, as osc1 smp). At the end the
 *             phase holds at frames << 12, where the two zero pad frames after
 *             every sample read 0, so the tail is silent until the next
 *             transient_trigger() sets the phase to 0.
 * Why:        S087 P3: user one-shots in the transient list. The entry is read
 *             from the catalogue each block, so the generator gains no RAM and
 *             a reinstall can never leave a stale pointer. A phase past the
 *             sample's end is clamped to the end, so a change of entry mid-hit
 *             is silent until the next trigger.
 * Inputs:     transient (phase, pitch, volume), index (< sample count), buf,
 *             size.
 * Outputs:    buf[0..size-1]; transient->phase.
 * Cost:       a separate per-sample loop from the built-in click loop, which
 *             is unchanged (see S087 P3 notes for the measured counts).
 * Accessors:  transient_calcBlock().
 * Affiliates: sampleMemory_sampleEntry(), sampleMemory_gainFromCode(),
 *             osc_smpPlainBlock() (the same read and gain law).
 */
static void transient_calcSampleBlock(TransientGenerator* transient,
                                      const uint8_t index, int16_t* buf,
                                      const uint8_t size)
{
	const sample_catalogue_entry_t *entry = sampleMemory_sampleEntry(index);
	const int16_t *data;
	uint32_t endPh;
	uint32_t ph;
	uint32_t inc;
	float g;
	uint8_t i;

	if (!entry || entry->frames == 0u)
	{
		memset(buf, 0, size * sizeof(int16_t));
		return;
	}
	data = (const int16_t *)SAMPLE_FLASH_PTR(entry->offset);
	endPh = entry->frames << TRANSIENT_USER_PHASE_SHIFT;
	g = transient->volume * sampleMemory_gainFromCode(entry->gain_code);
	inc = (uint32_t)(transient->pitch * (float)(1u << TRANSIENT_USER_PHASE_SHIFT));
	ph = transient->phase;
	if (ph > endPh)
		ph = endPh;
	for (i = 0u; i < size; i++)
	{
		const uint32_t idx = ph >> TRANSIENT_USER_PHASE_SHIFT;
		const int32_t a = data[idx];
		const int32_t d = (int32_t)data[idx + 1u] - a;
		/* Integer interpolation (12-bit fraction x 17-bit difference fits
		 * int32), then one float gain: two instructions fewer per sample than
		 * a float interpolation, keeping the loop at or below the built-in
		 * click loop. */
		const int32_t v = a + ((d * (int32_t)(ph &
		                  ((1u << TRANSIENT_USER_PHASE_SHIFT) - 1u)))
		                  >> TRANSIENT_USER_PHASE_SHIFT);

		buf[i] = (int16_t)((float)v * g);
		ph += inc;
		if (ph > endPh)
			ph = endPh;
	}
	transient->phase = ph;
}
//---------------------------------------------------------------
/*
 * Resolve and play a user one-shot entry (S087 P3).
 *
 * What:       a value from TRANSIENT_USER_FIRST on plays catalogue sample
 *             (value - TRANSIENT_USER_FIRST). A value past the installed
 *             samples is clamped in place to the last sample. With no sample
 *             installed the block is silent and the value is kept (S101: there
 *             is no built-in click left to fall back to, and clamping to the
 *             entry below would select Ofs, the offset mode, which changes the
 *             oscillator's start phase; the row then plays again as soon as
 *             samples are installed).
 * Why:        S087 P0 r8 / A23 rule, as the modulator wav list: the render
 *             always plays an entry that exists. Kept out of line (noinline) so
 *             the built-in click path of transient_calcBlock() keeps its
 *             per-block cost: it gains no catalogue calls and no extra
 *             registers (the call is a tail call).
 * Inputs:     transient (waveform >= TRANSIENT_USER_FIRST), buf, size.
 * Outputs:    buf (the sample, or silence after the no-sample clamp);
 *             phase; waveform when clamped.
 * Accessors:  transient_calcBlock().
 * Affiliates: transient_calcSampleBlock(), transient_setWaveform().
 */
static __attribute__((noinline)) void transient_calcUserBlock(
	TransientGenerator* transient, int16_t* buf, const uint8_t size)
{
	const uint8_t samples = sampleMemory_getNumSamples();
	uint8_t index = (uint8_t)(transient->waveform - TRANSIENT_USER_FIRST);

	if(samples == 0u)
	{
		/* No sample installed: silence, the row value kept (see above). */
		memset(buf, 0, size * sizeof(int16_t));
		return;
	}
	if(index >= samples)
	{
		index = (uint8_t)(samples - 1u);
		transient->waveform = (uint8_t)(TRANSIENT_USER_FIRST + index);
	}
	transient_calcSampleBlock(transient, index, buf, size);
}
//---------------------------------------------------------------
void transient_calcBlock(TransientGenerator* transient, int16_t* buf, const uint8_t size)
{
	/*
	 * Entry dispatch (S087 P3; S101).
	 *
	 * What:       0/1 (snap EG, offset) render silence as before; every other
	 *             value is a card sample (transient_calcUserBlock()).
	 * Why:        S101 (S101_P2_REDUCTIONS.md §B.3 F4, user 2026-11-21): the
	 *             twelve built-in 8-bit clicks (transientData, 26,460 B of
	 *             flash, and their 22-instruction loop) left the firmware. Their
	 *             data is now SD_CARD/samples/LXRClk.wav .. LXRSp2.wav, one-shot
	 *             samples played by the user path, as the crash wave became
	 *             LXRcym.wav in S094. No migration (standing rule): a stored
	 *             value 2..13 now selects installed sample 0..11.
	 */
	if(transient->waveform<=1)
	{
		memset(buf,0,size*sizeof(int16_t));
		return; //snapEg and offset
	}
	transient_calcUserBlock(transient, buf, size);
}
//---------------------------------------------------------------
