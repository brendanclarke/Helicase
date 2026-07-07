/*
 * copyClearTools.c
 *
 * Created: 26.01.2013 08:45:55
 *  Modified on 17.05.2026 by Brendan Clarke
 *  Author: Julian
 */ 
#include "copyClearTools.h"
#include <stdint.h>
#include <string.h>
#include "../Hardware/frontPanel/lcd.h"
#include "../Hardware/frontPanel/ledHandler.h"
#include "menu.h"
#include "PatternData.h"

uint8_t copyClear_Mode = MODE_NONE;
#define SRC_DST_NONE -1
int8_t buttonHandler_copySrc = SRC_DST_NONE;
int8_t buttonHandler_copyDst = SRC_DST_NONE;
static uint8_t copyClear_clearTarget = CLEAR_TRACK;


//-----------------------------------------------------------------------------
void copyClear_clearTrackAutom(uint8_t automTrack)
{
	/*
	 * Clears one automation lane for the active voice in the viewed pattern.
	 *
	 * This used to be sent through the front-panel parser as an automation
	 * command. PatternData now owns pattern/track/automation mutation, so the
	 * copy/clear menu calls pat_clearAutomation() directly.
	 *
	 * Inputs: automTrack selects automation lane 0 or 1. Pattern and voice are
	 * read from Menu because copy/clear acts on what the user is currently
	 * editing.
	 *
	 * Output: no return value; the selected automation lane is reset in pattern
	 * storage. LED/menu repaint is handled by the surrounding copy/clear flow.
	 *
	 * Risk: this assumes Menu's viewed pattern is the edit target, not
	 * necessarily the currently playing pattern when follow/performance modes
	 * diverge.
	 */
	uint8_t voice = menu_getActiveVoice();
	uint8_t pattern = menu_getViewedPattern();
	pat_clearAutomation(pattern, voice, automTrack);
};
//-----------------------------------------------------------------------------
void copyClear_clearCurrentPattern()
{
	/*
	 * Clears every track and automation lane in the viewed pattern.
	 *
	 * PatternData performs the mutation because Pattern owns all pattern
	 * storage. copyClearTools only clears the front-panel sequencer LEDs before
	 * the mutation so the user does not see stale active-step lights.
	 *
	 * Inputs: viewed pattern from Menu. Output: pattern data is reset in place.
	 * Risk: callers that need fresh track/menu params must reload them after
	 * this call because clearing the pattern does not repaint Menu by itself.
	 */
	uint8_t pattern = menu_getViewedPattern();
	led_clearSequencerLeds();
	pat_clearPattern(pattern);
};
//-----------------------------------------------------------------------------
void copyClear_executeClear()
{
	switch(copyClear_clearTarget)
	{
		default:
		case CLEAR_TRACK:
			copyClear_clearCurrentTrack();
		break;
		
		case CLEAR_PATTERN:
			copyClear_clearCurrentPattern();
		break;
		
		case CLEAR_AUTOMATION1:
			copyClear_clearTrackAutom(0);
		break;
		
		case CLEAR_AUTOMATION2:
			copyClear_clearTrackAutom(1);
		break;
	}
	
	copyClear_armClearMenu(0);
	copyClear_Mode = MODE_NONE;
};
//-----------------------------------------------------------------------------
void copyClear_reset()
{
	copyClear_Mode = MODE_NONE;
	led_clearAllBlinkLeds();
	buttonHandler_copySrc = buttonHandler_copyDst = SRC_DST_NONE;
	
};
//-----------------------------------------------------------------------------
void copyClear_setSrc(int8_t src, uint8_t type)
{
	buttonHandler_copySrc = src;
	copyClear_Mode = type;
};
//-----------------------------------------------------------------------------
void copyClear_setDst(int8_t dst, uint8_t type)
{
	buttonHandler_copyDst = dst;
	copyClear_Mode = type;
};
//-----------------------------------------------------------------------------
uint8_t copyClear_srcSet()
{
	return buttonHandler_copySrc != SRC_DST_NONE;
}
//-----------------------------------------------------------------------------
void copyClear_clearCurrentTrack()
{
	/*
	 * Clears the active voice/track in the viewed pattern.
	 *
	 * This is a direct PatternData mutation replacing the old parser clear-track
	 * opcode. Menu supplies the edit coordinates; ledHandler clears visible step
	 * LEDs; PatternData resets the track content.
	 *
	 * Inputs: active voice and viewed pattern from Menu. Output: selected track
	 * data is reset. Risk: PatternData intentionally does not touch LEDs, so any
	 * caller that remains on a step page must repaint after clearing.
	 */
	uint8_t voice = menu_getActiveVoice();
	uint8_t pattern = menu_getViewedPattern();
	led_clearSequencerLeds();
	pat_clearTrack(pattern, voice);
};
//-----------------------------------------------------------------------------
void copyClear_copyTrack()
{
	/*
	 * Copies one track inside the currently viewed pattern.
	 *
	 * buttonHandler_copySrc/Dst hold front-panel button indices while the copy
	 * gesture is active. copyClearTools converts them to 0..15 track indices and
	 * delegates the actual data copy to PatternData.
	 *
	 * Output: destination track is overwritten by source track, source/dest UI
	 * state is cleared, and sequencer LEDs are blanked so callers can repaint
	 * from the new destination data.
	 *
	 * Risk: the mask keeps historical button-index behavior. PatternData still
	 * validates the final track/pattern coordinates before writing.
	 */
	if(copyClear_Mode != MODE_COPY_TRACK)
	{
		return;
	}
	uint8_t src = (uint8_t)(buttonHandler_copySrc & 0xf);
	uint8_t dst = (uint8_t)(buttonHandler_copyDst & 0xf);
	uint8_t pattern = menu_getViewedPattern();
	led_clearSequencerLeds();
	pat_copyTrack(pattern, src, dst);
	buttonHandler_copySrc = buttonHandler_copyDst = SRC_DST_NONE;
};
//-----------------------------------------------------------------------------
void copyClear_copyPattern()
{
	/*
	 * Copies one full pattern to another pattern slot.
	 *
	 * PatternData owns the complete pattern array, including track steps and
	 * automation, so copyClearTools no longer serializes this through parser
	 * opcodes. The copy gesture state remains here because it is UI state.
	 *
	 * Output: destination pattern is overwritten, copy source/dest UI state is
	 * cleared, and visible sequencer LEDs are blanked for later repaint.
	 *
	 * Risk: any page showing pattern/track params must reload them after this
	 * call, because the data copy does not repaint Menu.
	 */
	if(copyClear_Mode != MODE_COPY_PATTERN)
	{
		return;
	}
	uint8_t src = (uint8_t)(buttonHandler_copySrc & 0xf);
	uint8_t dst = (uint8_t)(buttonHandler_copyDst & 0xf);
	led_clearSequencerLeds();
	pat_copyPattern(src, dst);
	buttonHandler_copySrc = buttonHandler_copyDst = SRC_DST_NONE;
};
//-----------------------------------------------------------------------------
void copyClear_copyBar()
{
	/* Copy one 16-step bar on the active track.
	 *
	 * COPY+SELECT stores source/destination SELECT indices here while
	 * PatternData owns the actual Step copy and track-length extension. Inputs:
	 * buttonHandler_copySrc/Dst are zero-based bars 0..7 from SELECT1..8; Menu
	 * supplies the current pattern/track. Output: destination bar is overwritten,
	 * length may extend, and copy gesture state is cleared for button release. */
	uint8_t src = (uint8_t)(buttonHandler_copySrc & 0x07);
	uint8_t dst = (uint8_t)(buttonHandler_copyDst & 0x07);
	uint8_t pattern = menu_getViewedPattern();
	uint8_t track = menu_getActiveVoice();

	if(copyClear_Mode != MODE_COPY_PATTERN)
		return;
	pat_copyBar(pattern, track, src, dst);
	buttonHandler_copySrc = buttonHandler_copyDst = SRC_DST_NONE;
}
//-----------------------------------------------------------------------------
uint8_t copyClear_isClearModeActive() 
{
	return (copyClear_Mode == MODE_CLEAR);
}
//-----------------------------------------------------------------------------
uint8_t copyClear_getClearTarget() 
{
	return copyClear_clearTarget;
}	
//-----------------------------------------------------------------------------
void copyClear_setClearTarget(uint8_t mode)
{
	copyClear_clearTarget = mode;
	if(copyClear_Mode == MODE_CLEAR)
	{
		//repaint
		copyClear_armClearMenu(1);
	}
}
//-----------------------------------------------------------------------------
void copyClear_armClearMenu(uint8_t isShown)
{
	if(isShown)
	{
		//TODO this wastes RAM!!!
		lcd_clear();
		lcd_setcursor(0,1);
		lcd_string("clear [");
		
		switch(copyClear_clearTarget)
		{
			default:
			case CLEAR_TRACK:
			lcd_string("track");
			break;
			
			case CLEAR_PATTERN:
			lcd_string("pattern");
			break;
			
			case CLEAR_AUTOMATION1:
			lcd_string("autom.1");
			break;
			
			case CLEAR_AUTOMATION2:
			lcd_string("autom.2");
			break;
		}
		lcd_string("]?");
		led_clearAllBlinkLeds();
		led_setValue(1,LED_COPY);

		
	}
	else
	{
		led_setValue(0,LED_COPY);
		menu_repaintAll();
	}
};
//-----------------------------------------------------------------------------
