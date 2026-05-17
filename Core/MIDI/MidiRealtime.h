/*
 * Core/MIDI/MidiRealtime.h
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
 * MidiRealtime.h
 *
 * Timestamped MIDI realtime event queue.
 *
 * Session 019 Phases 4/6: realtime bytes are classified at the RX edge and
 * consumed by the TIM3 sequencer timing owner. This keeps clock/start/stop
 * from waiting behind SysEx, channel-message parsing, LCD, SD, or UI work,
 * without doing sequencer work in USART/USB interrupt context.
 */

#ifndef MIDIREALTIME_H_
#define MIDIREALTIME_H_

#include <stdint.h>
#include "MidiMessages.h"

#define MIDI_REALTIME_SOURCE_DIN   ((uint8_t)midiSourceMIDI)
#define MIDI_REALTIME_SOURCE_USB   ((uint8_t)midiSourceUSB)
#define MIDI_REALTIME_SOURCE_PULSE 2u

typedef struct {
	uint8_t status;
	uint8_t source;
	uint32_t timestampUs;
} MidiRealtimeEvent;

uint8_t midiRealtime_isStatus(uint8_t status);
void midiRealtime_push(uint8_t status, uint8_t source, uint32_t timestampUs);
uint8_t midiRealtime_pop(MidiRealtimeEvent *event);
uint32_t midiRealtime_getDropCount(void);

#endif /* MIDIREALTIME_H_ */
