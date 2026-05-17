/*
 * clockSync.c
 *
 *  Created on: 03.02.2013
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

/*
 *  Modified on: 17.05.2026
 * ------------------------------------------------------------------------------------------------------------------------
 *  Modifications Copyright 2026 Brendan Clarke
 *  brendanpaulclarke@gmail.com
 *  https://www.brendanclarke.com
 * ------------------------------------------------------------------------------------------------------------------------
 *  The modifications to this file are part of the LXR02 Open-Source software.
 *  The same license and restrictions on use for the LXR software apply.
 * ------------------------------------------------------------------------------------------------------------------------
 */




#include "clockSync.h"
#include "timebase.h"

static float sync_window[WINDOW_SIZE];
static uint32_t sync_timeLastUs = 0;
static uint8_t sync_haveTimestamp = 0;
uint8_t sync_writePos = 0;
uint8_t sync_armed = 0;

uint8_t sync_clockCnt = 0;
//---------------------------------------------------------
uint16_t sync_calcBpm(float timePerPulse)
{
	// 1 minute has 60,000,000 us. MIDI clock is 24 pulses per quarter.
	// Phase 4 feeds this with TIM2 microsecond deltas captured at RX time.

	float quarterDuration = 24 * timePerPulse;
	float bpm = (60 * 1000 * 1000) / quarterDuration;

	return bpm>0?bpm:1;
}
//---------------------------------------------------------
//called by midi clock
void sync_tick()
{
	sync_tickTimestamp(timebase_tim2Now());
}
//---------------------------------------------------------
void sync_tickTimestamp(uint32_t timestampUs)
{
	float time;
	int i;

	if (!sync_haveTimestamp) {
		sync_timeLastUs = timestampUs;
		sync_haveTimestamp = 1;
		time = ((60.f * 1000.f * 1000.f) / (seq_getBpm() ? seq_getBpm() : 1)) / 24.f;
		for(i=0;i<WINDOW_SIZE;i++)
			sync_window[i] = time;
		sync_writePos = 0;
	} else {
		time = (float)timebase_tim2Delta(timestampUs, sync_timeLastUs);
		sync_timeLastUs = timestampUs;
	}

	sync_window[sync_writePos++] = time;
	sync_writePos &= WINDOW_MASK;

	//calc average time in microseconds between midi clock pulses
	float avg=0;
	for(i=0;i<WINDOW_SIZE;i++)
	{
		avg += sync_window[i];
	}
	avg /= WINDOW_SIZE;

	seq_setBpm(sync_calcBpm(avg));

	sync_clockCnt++;

	if(seq_isRunning())
	{
		//now handle sequencer transport (4 steps every 3 clocks)
		if(sync_clockCnt == 1 || sync_clockCnt >= 4) {
			sync_clockCnt = 1;
			seq_resetDeltaAndTick();
		}
	}
}
//---------------------------------------------------------
void sync_midiStartStop(uint8_t isStart)
{
	if(isStart) {
		// this message does NOT mean start immediately! it just 'arms' the seq to start at the next MIDI_CLOCK received
		sync_clockCnt = 0;
		seq_setRunning(1);
		sync_timeLastUs = timebase_tim2Now();
		sync_haveTimestamp = 0;
	} else {
		seq_setRunning(0);
		sync_haveTimestamp = 0;
	}
}
//---------------------------------------------------------
uint8_t sync_getClockCnt()
{
	return sync_clockCnt;
}
//---------------------------------------------------------
