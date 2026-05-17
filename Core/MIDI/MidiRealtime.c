/*
 * Core/MIDI/MidiRealtime.c
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
 * MidiRealtime.c
 *
 * Small SPSC-style ring for timestamped MIDI realtime events.
 */

#include "MidiRealtime.h"

#define MIDI_REALTIME_RING_SIZE 32u
#define MIDI_REALTIME_RING_MASK (MIDI_REALTIME_RING_SIZE - 1u)

static volatile MidiRealtimeEvent midiRealtime_ring[MIDI_REALTIME_RING_SIZE];
static volatile uint8_t midiRealtime_head = 0;
static volatile uint8_t midiRealtime_tail = 0;
static volatile uint32_t midiRealtime_dropCount = 0;

static uint32_t midiRealtime_irqSave(void)
{
	uint32_t primask;
	__asm volatile ("mrs %0, primask\ncpsid i" : "=r" (primask) :: "memory");
	return primask;
}

static void midiRealtime_irqRestore(uint32_t primask)
{
	__asm volatile ("msr primask, %0" :: "r" (primask) : "memory");
}

uint8_t midiRealtime_isStatus(uint8_t status)
{
	switch (status) {
	case MIDI_CLOCK:
	case MIDI_START:
	case MIDI_CONTINUE:
	case MIDI_STOP:
		return 1;
	default:
		return 0;
	}
}

void midiRealtime_push(uint8_t status, uint8_t source, uint32_t timestampUs)
{
	uint8_t next;
	uint32_t primask;

	if (!midiRealtime_isStatus(status))
		return;

	primask = midiRealtime_irqSave();
	next = (uint8_t)((midiRealtime_head + 1u) & MIDI_REALTIME_RING_MASK);
	if (next == midiRealtime_tail) {
		/* Realtime clock is more useful fresh than complete. Drop the oldest
		** event rather than blocking an ISR or letting a stale clock burst play
		** catch-up later. The counter is exposed for diagnostics. */
		midiRealtime_tail = (uint8_t)((midiRealtime_tail + 1u) & MIDI_REALTIME_RING_MASK);
		midiRealtime_dropCount++;
	}

	midiRealtime_ring[midiRealtime_head].status = status;
	midiRealtime_ring[midiRealtime_head].source = source;
	midiRealtime_ring[midiRealtime_head].timestampUs = timestampUs;
	midiRealtime_head = next;
	midiRealtime_irqRestore(primask);
}

uint8_t midiRealtime_pop(MidiRealtimeEvent *event)
{
	uint32_t primask = midiRealtime_irqSave();

	if (midiRealtime_tail == midiRealtime_head) {
		midiRealtime_irqRestore(primask);
		return 0;
	}

	*event = midiRealtime_ring[midiRealtime_tail];
	midiRealtime_tail = (uint8_t)((midiRealtime_tail + 1u) & MIDI_REALTIME_RING_MASK);
	midiRealtime_irqRestore(primask);
	return 1;
}

uint32_t midiRealtime_getDropCount(void)
{
	return midiRealtime_dropCount;
}
