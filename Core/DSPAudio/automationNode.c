/*
 * automationNode.c
 *
 *  Created on: 12.02.2013
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

#include "automationNode.h"

static uint16_t autoNode_sanitizeDestination(uint16_t dest)
{
	/*
	 * Bound one legacy automation destination before it can touch MIDI CC state.
	 *
	 * Inputs: dest is the Step-stored automation destination. Historically this
	 * was a CC/CC2 number where 1..254 are valid and NO_AUTOMATION (0xff) means
	 * off; zero is not valid because MIDI_CC data1 zero underflows the legacy
	 * parser's parameter index. Newer PatternData code can also carry wider
	 * sentinel values such as
	 * INSTRUMENT_PARAM_INVALID (0xffff). Output: valid legacy destinations pass
	 * through; every wide/stale/off sentinel collapses to NO_AUTOMATION. This
	 * must live beside autoNode_setDestination() because this module owns the
	 * midiParser_originalCcValues[] index and is the last defense before it.
	 */
	return (dest > 0u && dest < NO_AUTOMATION) ? dest : NO_AUTOMATION;
}

//-------------------------------------------------------------
void autoNode_init(AutomationNode* node)
{
	node->destination = NO_AUTOMATION;
}
//-------------------------------------------------------------
void autoNode_setDestination(AutomationNode* node, uint16_t dest)
{
	uint16_t oldDest;
	dest = autoNode_sanitizeDestination(dest);
	oldDest = autoNode_sanitizeDestination(node->destination);

	//reset lastDest
	if(oldDest != NO_AUTOMATION)
	{
		MidiMsg msg;

		if(oldDest > 127) {
			msg.status = MIDI_CC2;
			msg.data1 = oldDest - 127 -1;
		} else {
			msg.status = MIDI_CC;
			msg.data1 = oldDest;
		}
		msg.data2 = midiParser_originalCcValues[oldDest];
		midiParser_ccHandler(msg,0);
	}

	//set new destination
	node->destination = dest;
}
//-------------------------------------------------------------
void autoNode_updateValue(AutomationNode* node, uint8_t val)
{
	uint16_t dest = autoNode_sanitizeDestination(node->destination);

	if(dest != NO_AUTOMATION) {
		MidiMsg msg;

		if(dest > 127) {
			msg.status = MIDI_CC2;
			msg.data1 = dest - 127 -1; //todo why +1 offset?
		} else {
			msg.status = MIDI_CC;
			msg.data1 = dest;
		}
		msg.data2 = val;
		midiParser_ccHandler(msg,0);
	}
}
//-------------------------------------------------------------
