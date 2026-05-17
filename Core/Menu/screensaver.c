/*
 * screensaver.c
 *
 * Created: 25.01.2013 17:08:02
 *  Author: Julian
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
 
//to expand the lifetime of OLED displays, we need a timeout var to go to
//screensaver mode if the controls are not touched for a while
#include "../Hardware/timebase.h"
#include "../Hardware/frontPanel/lcd.h"
#include <stdint.h>
#include <string.h>
#include "menu.h"

#define SCREENSAVER_ACTIVE 1
#define F_SYSTICK 1000UL  // Systemtakt in Hz - Definition als unsigned long beachten
#define F_CPU 216000000UL
//stuff for activation logic
volatile uint16_t screensaver_timer = 0;
#define SCREENSAVER_TIMEOUT (uint16_t)(2*F_SYSTICK) //[minutes]
// #define SCREENSAVER_SYSTICK (uint16_t)(((SCREENSAVER_TIMEOUT*60) / (1.f / ((F_CPU/1024.f)/256.f))))
#define SCREENSAVER_SYSTICK (uint16_t)(SCREENSAVER_TIMEOUT*60)
volatile uint8_t screensaver_Active=0;
volatile uint16_t screensaver_Timeout=SCREENSAVER_SYSTICK;


//stuff for fancy drawing
static uint16_t screensaver_processTime = 0;
#define SCREENSAVER_PROCESS_SPEED ((0.5f) / (1.f / ((F_CPU/1024.f)/256.f)))
enum
{
	CELL_OFF,
	CELL_1,
	CELL_2,
	CELL_3
};

uint8_t screensaver_activeCell = 0;
uint8_t screensaver_cellState = CELL_OFF;
static uint8_t screensaver_cellX = 0;
static uint8_t screensaver_cellY = 0;

uint16_t screensaver_rng =  0;	//*<temporary var for rng generation*/
//------------------------------------------------------------------
uint8_t screensaver_pseudoRng()
{
	screensaver_rng++;
	screensaver_rng *= 3;
	return (screensaver_rng >> 2) & 0xFF;
}
//------------------------------------------------------------------
uint8_t screensaver_isActive(void)
{
	return screensaver_Active;
}
//------------------------------------------------------------------
void screensaver_touch()
{
	#if SCREENSAVER_ACTIVE
	screensaver_timer = 0;
	screensaver_Timeout = SCREENSAVER_SYSTICK;

	if(screensaver_Active)
	{
		lcd_clear();
		screensaver_Active = 0;
		screensaver_cellState = CELL_OFF;
		menu_repaintAll();
		lcd_turnOn(1,0);
	}	
	#endif		
}
//------------------------------------------------------------------
//draw some fancy stuff on the LCD
void screensaver_process()
{
#if SCREENSAVER_ACTIVE
	if(time_sysTick >= screensaver_processTime )
	{
		uint8_t drawChar = 0;
		uint8_t drawActiveCell = 0;
		screensaver_processTime = (uint16_t)( time_sysTick + SCREENSAVER_PROCESS_SPEED);
		
		if(screensaver_cellState == CELL_OFF)
		{
			screensaver_activeCell = screensaver_pseudoRng()&0x1f; //value between 0 and 31
			if(screensaver_activeCell > 15)
			{
				screensaver_cellX = (uint8_t)(screensaver_activeCell - 16);
				screensaver_cellY = 1;
			}
			else
			{
				screensaver_cellX = screensaver_activeCell;
				screensaver_cellY = 0;
			}
		}
		
		switch(screensaver_cellState)
		{
			default:
			case CELL_OFF:
				if(lcd_queueFree() >= 1u)
					lcd_turnOff();
				screensaver_cellState = CELL_1;
				return;
				
				case CELL_1:
				// draw char stage 1 at retained x/y
				drawChar = 0xA5;
				drawActiveCell = 1;
				screensaver_cellState = CELL_2;
				break;
				
				case CELL_2:
				// draw char stage 2 at retained x/y
				drawChar = 0x6F;
				drawActiveCell = 1;
				screensaver_cellState = CELL_3;
				break;
				
				case CELL_3:
				// draw char stage 3 at retained x/y
				drawChar = 0x01;
				drawActiveCell = 1;
				screensaver_cellState = CELL_OFF;
				break;
		}

		if(drawActiveCell)
		{
			if(lcd_queueFree() < 4u)
				return;
			lcd_clear();
			lcd_setcursor(screensaver_cellX, (uint8_t)(screensaver_cellY + 1u));
			lcd_data(drawChar);
			lcd_turnOn(1,0);
		}
	}
#endif	
}
//------------------------------------------------------------------
void screensaver_check()
{
#if SCREENSAVER_ACTIVE
	if(parameter_values[PAR_SCREENSAVER_ON_OFF])
	{
		if( (!screensaver_Active) )
		{
			if(screensaver_timer >= screensaver_Timeout)
			{
				lcd_turnOff();
				screensaver_cellState = CELL_OFF;
				screensaver_processTime = time_sysTick;
				screensaver_Active = 1;	
			}
		}
		else
		{
			screensaver_process();
		}	
	}
#endif
};
//------------------------------------------------------------------
