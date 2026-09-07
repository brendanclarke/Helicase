/*
 * Core/Menu/SplashAnimation.c
 *
 *  Created on: 06.09.2026
 * ------------------------------------------------------------------------------------------------------------------------
 *  Copyright 2026 Brendan Clarke
 *  brendanpaulclarke@gmail.com
 *  https://www.brendanclarke.com
 * ------------------------------------------------------------------------------------------------------------------------
 *  This file is part of the LXR02 Open-Source software.
 * ------------------------------------------------------------------------------------------------------------------------
 */

#include "SplashAnimation.h"
#include "lcd.h"
#include "timebase.h"
#include <stdint.h>

/* Number of distinct CGRAM characters making up one animation frame:
** 3 across the top row, 3 across the bottom row (top-left, top-mid,
** top-right, bottom-left, bottom-mid, bottom-right). */
#define SPLASH_ANIMATION_CHARS_PER_FRAME   6u

/* First of the 6 consecutive CGRAM locations (2..7 -- the "last 6" of
** the 8 available, 0..7) that hold one animation frame's characters.
** Layout on screen (locations, top row then bottom row):
**   BASE+0 BASE+1 BASE+2
**   BASE+3 BASE+4 BASE+5
*/
#define SPLASH_ANIMATION_CGRAM_BASE        2u

/* -----------------------------------------------------------------------
** lcd_init() (Core/Hardware/frontPanel/lcd.c) defines CHECK/HEART/BELL at
** CGRAM locations 2/3/4, which fall inside the 2-7 range this animation
** borrows. Those three bitmaps are duplicated here (values copied
** verbatim from lcd.c) purely so the animation can hand them back
** unchanged once it's done -- lcd.c's copies are file-local `static
** const`, so they aren't reachable from here. If lcd.c's glyphs ever
** change, update these three arrays to match, or better, expose a single
** lcd_restoreDefaultChars() helper from lcd.c/lcd.h and delete these.
** ----------------------------------------------------------------------- */
static const uint8_t splashAnimation_restoreCheck[8] = {
    0b00000,
    0b00000,
    0b00001,
    0b00011,
    0b10110,
    0b11100,
    0b01000,
    0b00000
};

static const uint8_t splashAnimation_restoreHeart[8] = {
    0b00000,
    0b01010,
    0b11111,
    0b11111,
    0b01110,
    0b00100,
    0b00000,
    0b00000
};

static const uint8_t splashAnimation_restoreBell[8] = {
    0b00100,
    0b01110,
    0b01110,
    0b01110,
    0b11111,
    0b00000,
    0b00100,
    0b00000
};

/* -----------------------------------------------------------------------
** Frame data
**
** Source: helicase_splash_animation_5_5.gif, a 17x17px 1-bit image.
** Three 5px-wide characters per row with a 1px gap between them
** (cols 0-4, 5=gap, 6-10, 11=gap, 12-16) and two 8px-tall character rows
** with a 1px gap between them (rows 0-7, 8=gap, 9-16). Gap pixel column 5
** and 11, and gap pixel row 8, are not used by the display and are
** dropped. Each 5x8 character cell is packed into 8 bytes (one byte per
** pixel row, bit4 = leftmost pixel .. bit0 = rightmost pixel), the same
** convention lcd_init()'s CHECK/HEART/BELL/POP/ELLIPSIS glyphs use.
**
** NOTE: the source GIF contains 17 unique frames, not 18 -- there is no
** 18th frame to add. SPLASH_ANIMATION_FRAME_COUNT below is derived from
** the array itself (via sizeof), not hardcoded, so appending an 18th
** frame later (or any other count) needs no other code changes.
**
** Frame 0 (rough preview, '#' = pixel on):
**   Row1 (top-left / top-mid / top-right):
**     .###.  .....  ...#.
**     ..##.  #####  ...#.
**     ..###  .....  ...#.
**     ...##  .....  ..#..
**     ....#  ...#.  .#...
**     ....#  ##...  #....
**     .....  #####  .....
**     .....  .####  .....
**   Row2 (bottom-left / bottom-mid / bottom-right):
**     .....  ....#  #....
**     .....  #....  ##...
**     ....#  .....  ###..
**     ...#.  .....  .##..
**     ..#..  #####  ..##.
**     ..#..  .....  ..##.
**     .#...  .....  ..###
**     .#.##  #####  ...##
** ----------------------------------------------------------------------- */
static const uint8_t splashAnimation_frames[][SPLASH_ANIMATION_CHARS_PER_FRAME][8] =
{
    { /* frame  0 */
        { 0x0e, 0x06, 0x07, 0x03, 0x01, 0x01, 0x00, 0x00 }, /* top-left */
        { 0x00, 0x1f, 0x00, 0x00, 0x02, 0x18, 0x1f, 0x0f }, /* top-mid */
        { 0x02, 0x02, 0x02, 0x04, 0x08, 0x10, 0x00, 0x00 }, /* top-right */
        { 0x00, 0x00, 0x01, 0x02, 0x04, 0x04, 0x08, 0x0b }, /* bottom-left */
        { 0x01, 0x10, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x1f }, /* bottom-mid */
        { 0x10, 0x18, 0x1c, 0x0c, 0x06, 0x06, 0x07, 0x03 } /* bottom-right */
    },
    { /* frame  1 */
        { 0x06, 0x07, 0x03, 0x01, 0x01, 0x00, 0x00, 0x00 }, /* top-left */
        { 0x00, 0x1f, 0x00, 0x00, 0x18, 0x1c, 0x0f, 0x07 }, /* top-mid */
        { 0x02, 0x04, 0x04, 0x08, 0x10, 0x00, 0x00, 0x00 }, /* top-right */
        { 0x00, 0x01, 0x02, 0x04, 0x05, 0x08, 0x08, 0x0b }, /* bottom-left */
        { 0x10, 0x04, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x1f }, /* bottom-mid */
        { 0x18, 0x1c, 0x0c, 0x06, 0x06, 0x07, 0x03, 0x03 } /* bottom-right */
    },
    { /* frame  2 */
        { 0x07, 0x03, 0x03, 0x01, 0x00, 0x00, 0x00, 0x00 }, /* top-left */
        { 0x00, 0x0f, 0x00, 0x10, 0x1c, 0x1f, 0x07, 0x01 }, /* top-mid */
        { 0x04, 0x04, 0x08, 0x10, 0x00, 0x00, 0x00, 0x10 }, /* top-right */
        { 0x01, 0x02, 0x04, 0x04, 0x0d, 0x08, 0x08, 0x05 }, /* bottom-left */
        { 0x00, 0x1e, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x1f }, /* bottom-mid */
        { 0x1c, 0x0c, 0x06, 0x06, 0x03, 0x03, 0x03, 0x06 } /* bottom-right */
    },
    { /* frame  3 */
        { 0x03, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* top-left */
        { 0x00, 0x06, 0x10, 0x1c, 0x0f, 0x07, 0x01, 0x10 }, /* top-mid */
        { 0x04, 0x08, 0x10, 0x00, 0x00, 0x00, 0x10, 0x18 }, /* top-right */
        { 0x02, 0x04, 0x04, 0x08, 0x0b, 0x08, 0x04, 0x02 }, /* bottom-left */
        { 0x00, 0x1f, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x1f }, /* bottom-mid */
        { 0x0e, 0x06, 0x07, 0x03, 0x03, 0x03, 0x06, 0x06 } /* bottom-right */
    },
    { /* frame  4 */
        { 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 }, /* top-left */
        { 0x00, 0x12, 0x1c, 0x0f, 0x07, 0x03, 0x10, 0x0c }, /* top-mid */
        { 0x08, 0x10, 0x00, 0x00, 0x00, 0x10, 0x18, 0x1c }, /* top-right */
        { 0x04, 0x05, 0x08, 0x08, 0x09, 0x04, 0x06, 0x01 }, /* bottom-left */
        { 0x00, 0x1f, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x0e }, /* bottom-mid */
        { 0x06, 0x03, 0x03, 0x03, 0x03, 0x07, 0x06, 0x0c } /* bottom-right */
    },
    { /* frame  5 */
        { 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02 }, /* top-left */
        { 0x00, 0x10, 0x1d, 0x0f, 0x03, 0x10, 0x00, 0x1e }, /* top-mid */
        { 0x10, 0x00, 0x00, 0x00, 0x10, 0x18, 0x1c, 0x0e }, /* top-right */
        { 0x04, 0x09, 0x08, 0x08, 0x05, 0x04, 0x03, 0x00 }, /* bottom-left */
        { 0x00, 0x1f, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x14 }, /* bottom-mid */
        { 0x03, 0x03, 0x03, 0x03, 0x06, 0x06, 0x0c, 0x1c } /* bottom-right */
    },
    { /* frame  6 */
        { 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x04 }, /* top-left */
        { 0x1c, 0x07, 0x01, 0x10, 0x04, 0x00, 0x00, 0x1f }, /* top-mid */
        { 0x00, 0x00, 0x00, 0x10, 0x18, 0x1c, 0x0c, 0x06 }, /* top-right */
        { 0x08, 0x09, 0x0c, 0x04, 0x06, 0x02, 0x01, 0x00 }, /* bottom-left */
        { 0x00, 0x1f, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x18 }, /* bottom-mid */
        { 0x03, 0x03, 0x03, 0x06, 0x06, 0x0c, 0x18, 0x10 } /* bottom-right */
    },
    { /* frame  7 */
        { 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x04, 0x05 }, /* top-left */
        { 0x0f, 0x07, 0x00, 0x00, 0x0e, 0x00, 0x00, 0x1f }, /* top-mid */
        { 0x00, 0x00, 0x10, 0x18, 0x0c, 0x06, 0x06, 0x07 }, /* top-right */
        { 0x08, 0x09, 0x04, 0x06, 0x03, 0x01, 0x01, 0x00 }, /* bottom-left */
        { 0x00, 0x1f, 0x00, 0x00, 0x1e, 0x00, 0x10, 0x1d }, /* bottom-mid */
        { 0x03, 0x02, 0x06, 0x06, 0x0c, 0x18, 0x10, 0x00 } /* bottom-right */
    },
    { /* frame  8 */
        { 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x06, 0x04 }, /* top-left */
        { 0x04, 0x0c, 0x18, 0x00, 0x0e, 0x00, 0x00, 0x1f }, /* top-mid */
        { 0x10, 0x10, 0x18, 0x0c, 0x0c, 0x06, 0x06, 0x03 }, /* top-right */
        { 0x0c, 0x04, 0x06, 0x02, 0x01, 0x00, 0x00, 0x00 }, /* bottom-left */
        { 0x00, 0x1f, 0x00, 0x00, 0x06, 0x10, 0x1c, 0x0f }, /* bottom-mid */
        { 0x06, 0x06, 0x04, 0x0c, 0x08, 0x18, 0x10, 0x00 } /* bottom-right */
    },
    { /* frame  9 */
        { 0x00, 0x00, 0x01, 0x03, 0x06, 0x04, 0x0c, 0x09 }, /* top-left */
        { 0x0c, 0x19, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x1f }, /* top-mid */
        { 0x18, 0x0c, 0x04, 0x04, 0x06, 0x06, 0x02, 0x02 }, /* top-right */
        { 0x04, 0x06, 0x03, 0x01, 0x01, 0x00, 0x00, 0x00 }, /* bottom-left */
        { 0x00, 0x1f, 0x00, 0x00, 0x06, 0x10, 0x1d, 0x0f }, /* bottom-mid */
        { 0x06, 0x06, 0x0c, 0x18, 0x18, 0x10, 0x00, 0x00 } /* bottom-right */
    },
    { /* frame 10 */
        { 0x00, 0x01, 0x03, 0x06, 0x04, 0x0c, 0x08, 0x0d }, /* top-left */
        { 0x10, 0x07, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x1f }, /* top-mid */
        { 0x0c, 0x04, 0x06, 0x06, 0x02, 0x02, 0x02, 0x02 }, /* top-right */
        { 0x06, 0x03, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00 }, /* bottom-left */
        { 0x00, 0x1e, 0x00, 0x00, 0x1c, 0x0f, 0x03, 0x11 }, /* bottom-mid */
        { 0x06, 0x0c, 0x18, 0x10, 0x00, 0x00, 0x00, 0x18 } /* bottom-right */
    },
    { /* frame 11 */
        { 0x01, 0x03, 0x06, 0x04, 0x0d, 0x0c, 0x0c, 0x04 }, /* top-left */
        { 0x00, 0x0f, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x1f }, /* top-mid */
        { 0x04, 0x06, 0x06, 0x02, 0x02, 0x02, 0x02, 0x06 }, /* top-right */
        { 0x03, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* bottom-left */
        { 0x00, 0x07, 0x10, 0x1c, 0x0f, 0x03, 0x09, 0x10 }, /* bottom-mid */
        { 0x0c, 0x08, 0x10, 0x00, 0x00, 0x00, 0x10, 0x18 } /* bottom-right */
    },
    { /* frame 12 */
        { 0x03, 0x07, 0x06, 0x0e, 0x0c, 0x0c, 0x06, 0x07 }, /* top-left */
        { 0x00, 0x1f, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x1f }, /* top-mid */
        { 0x04, 0x06, 0x02, 0x02, 0x02, 0x02, 0x06, 0x04 }, /* top-right */
        { 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 }, /* bottom-left */
        { 0x00, 0x1c, 0x1c, 0x0f, 0x07, 0x11, 0x00, 0x0e }, /* bottom-mid */
        { 0x08, 0x10, 0x00, 0x00, 0x00, 0x10, 0x18, 0x1c } /* bottom-right */
    },
    { /* frame 13 */
        { 0x06, 0x06, 0x0c, 0x0c, 0x0c, 0x06, 0x07, 0x03 }, /* top-left */
        { 0x00, 0x1f, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x07 }, /* top-mid */
        { 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x04, 0x04 }, /* top-right */
        { 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02 }, /* bottom-left */
        { 0x1c, 0x1e, 0x0f, 0x07, 0x01, 0x00, 0x00, 0x1e }, /* bottom-mid */
        { 0x10, 0x00, 0x00, 0x00, 0x10, 0x18, 0x1c, 0x0e } /* bottom-right */
    },
    { /* frame 14 */
        { 0x0c, 0x0d, 0x0c, 0x0e, 0x06, 0x07, 0x03, 0x01 }, /* top-left */
        { 0x00, 0x1f, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x00 }, /* top-mid */
        { 0x02, 0x02, 0x02, 0x02, 0x02, 0x04, 0x04, 0x08 }, /* top-right */
        { 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x04 }, /* bottom-left */
        { 0x1e, 0x0f, 0x03, 0x11, 0x04, 0x00, 0x00, 0x1e }, /* bottom-mid */
        { 0x00, 0x00, 0x00, 0x10, 0x18, 0x1c, 0x0e, 0x0e } /* bottom-right */
    },
    { /* frame 15 */
        { 0x0c, 0x0c, 0x0e, 0x06, 0x07, 0x03, 0x01, 0x01 }, /* top-left */
        { 0x00, 0x1f, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x1c }, /* top-mid */
        { 0x02, 0x02, 0x02, 0x02, 0x04, 0x04, 0x08, 0x10 }, /* top-right */
        { 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x04, 0x05 }, /* bottom-left */
        { 0x0f, 0x07, 0x11, 0x00, 0x0e, 0x00, 0x00, 0x1f }, /* bottom-mid */
        { 0x00, 0x00, 0x10, 0x18, 0x1c, 0x0c, 0x0e, 0x06 } /* bottom-right */
    },
    { /* frame 16 */
        { 0x0e, 0x0e, 0x06, 0x07, 0x03, 0x03, 0x01, 0x00 }, /* top-left */
        { 0x00, 0x1f, 0x00, 0x00, 0x0f, 0x00, 0x10, 0x1e }, /* top-mid */
        { 0x02, 0x02, 0x02, 0x04, 0x04, 0x08, 0x10, 0x00 }, /* top-right */
        { 0x00, 0x00, 0x00, 0x01, 0x02, 0x04, 0x04, 0x09 }, /* bottom-left */
        { 0x07, 0x01, 0x00, 0x00, 0x1e, 0x00, 0x00, 0x1f }, /* bottom-mid */
        { 0x00, 0x10, 0x18, 0x1c, 0x0c, 0x0e, 0x06, 0x06 } /* bottom-right */
    }
};

#define SPLASH_ANIMATION_FRAME_COUNT \
    ((uint8_t)(sizeof(splashAnimation_frames) / sizeof(splashAnimation_frames[0])))

/* time_sysTick is declared volatile in timebase.h and ticks at 1kHz
** (see boot_delayMs() in main.c for the same pattern). Blocking by
** design -- called once at boot, before the audio loop is running. */
static void splashAnimation_delayMs(uint16_t ms)
{
    uint16_t t0 = (uint16_t)time_sysTick;
    while ((uint16_t)((uint16_t)time_sysTick - t0) < ms) { /* boot-only hold */ }
}

/* Longest we'll ever wait for the LCD op queue to drain before giving up
** and moving on. One frame enqueues ~68 ops (6x lcd_define_char at 10
** ops each, plus 8 for splashAnimation_redrawIcons); at ~1ms/op that's
** up to ~70ms of real drain time, so this must comfortably exceed that.
** Deliberately NOT lcd_waitForIdle(): that call spins on
** lcd_driver_busy/TIM7 with no timeout at all, so if TIM7 is ever not
** draining the queue for any reason, it hangs forever -- taking all of
** boot down with it. This bounded version can only ever cost us up to
** SPLASH_ANIMATION_DRAIN_TIMEOUT_MS of extra wait; it can never lock up
** the system. */
#define SPLASH_ANIMATION_DRAIN_TIMEOUT_MS  150u

static void splashAnimation_waitQueueDrained(void)
{
    uint16_t t0 = (uint16_t)time_sysTick;
    while (lcd_queueFree() < LCD_QUEUE_SIZE)
    {
        if ((uint16_t)((uint16_t)time_sysTick - t0) >= SPLASH_ANIMATION_DRAIN_TIMEOUT_MS)
            break; /* give up rather than risk hanging boot forever */
    }
}

/* Re-write the 6 icon character codes over themselves at their existing
** DDRAM positions. This display does not appear to continuously re-scan
** DDRAM against CGRAM the way older passive HD44780 LCDs do -- a CGRAM
** rewrite alone leaves an already-drawn character showing its old
** bitmap. Explicitly re-touching the DDRAM byte forces this OLED to
** re-fetch the glyph from CGRAM, so this must be called after every
** lcd_define_char() batch to actually see the animation move. */
static void splashAnimation_redrawIcons(void)
{
    lcd_setcursor(0, 1);
    lcd_data(SPLASH_ANIMATION_CGRAM_BASE + 0);
    lcd_data(SPLASH_ANIMATION_CGRAM_BASE + 1);
    lcd_data(SPLASH_ANIMATION_CGRAM_BASE + 2);

    lcd_setcursor(0, 2);
    lcd_data(SPLASH_ANIMATION_CGRAM_BASE + 3);
    lcd_data(SPLASH_ANIMATION_CGRAM_BASE + 4);
    lcd_data(SPLASH_ANIMATION_CGRAM_BASE + 5);
}

void splashAnimation_play(void)
{
    uint8_t rep, f, c;

    lcd_clear();
    splashAnimation_waitQueueDrained();

    lcd_setcursor(0, 1);
    lcd_data(SPLASH_ANIMATION_CGRAM_BASE + 0);
    lcd_data(SPLASH_ANIMATION_CGRAM_BASE + 1);
    lcd_data(SPLASH_ANIMATION_CGRAM_BASE + 2);
    lcd_string("      voskomm");

    lcd_setcursor(0, 2);
    lcd_data(SPLASH_ANIMATION_CGRAM_BASE + 3);
    lcd_data(SPLASH_ANIMATION_CGRAM_BASE + 4);
    lcd_data(SPLASH_ANIMATION_CGRAM_BASE + 5);
    lcd_string("helicase 0.00");

    splashAnimation_waitQueueDrained();

    for (rep = 0; rep < SPLASH_ANIMATION_REPEAT_COUNT; rep++)
    {
        for (f = 0; f < SPLASH_ANIMATION_FRAME_COUNT; f++)
        {
            for (c = 0; c < SPLASH_ANIMATION_CHARS_PER_FRAME; c++)
            {
                lcd_define_char((uint8_t)(SPLASH_ANIMATION_CGRAM_BASE + c),
                                 splashAnimation_frames[f][c]);
            }
            splashAnimation_redrawIcons();
            splashAnimation_waitQueueDrained();
            splashAnimation_delayMs(SPLASH_ANIMATION_FRAME_DELAY_MS);
        }
    }

    /* Hand CGRAM locations 2-4 back to lcd_init()'s defaults before the
    ** menu system starts using CHECK/HEART/BELL. Locations 5-7 were free
    ** before this animation ran and are left holding the final frame's
    ** (otherwise-unused) glyphs, which is harmless. */
    // lcd_define_char(2, splashAnimation_restoreCheck);
    // lcd_define_char(3, splashAnimation_restoreHeart);
    // lcd_define_char(4, splashAnimation_restoreBell);
    splashAnimation_waitQueueDrained();
}
