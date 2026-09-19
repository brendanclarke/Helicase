/*
 * Core/Menu/SplashAnimation.h
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

/*
 * SplashAnimation.h
 *
 * Boot-time "helicase" CGRAM splash animation. Replaces the old static
 * boot_show_splash() text screen in main.c. Blocking (spin-waits between
 * frames) — must only be called once, early in main(), before the audio
 * render loop and menu system are running.
 *
 * Uses the LAST 6 of the LCD's 8 CGRAM slots (locations 2-7). Locations
 * 2, 3 and 4 are also used by lcd_init() for the CHECK/HEART/BELL glyphs
 * (see lcd.c); splashAnimation_play() restores those three glyphs itself
 * once the animation finishes, so the menu system sees CGRAM exactly as
 * lcd_init() left it.
 *
 * Screen layout (16 cols x 2 rows), 'X' = one animated CGRAM character:
 *
 *   XXX voskomm 0.00
 *   XXX     helicase
 */

#ifndef SPLASH_ANIMATION_H_
#define SPLASH_ANIMATION_H_

#include <stdint.h>

/* Wait held between each animation frame, in milliseconds. */
#define SPLASH_ANIMATION_FRAME_DELAY_MS   40u

/* Number of times the full frame sequence is played before settling on
** the final frame (and leaving the "voskomm 0.00" / "helicase" text up
** through the rest of boot). */
#define SPLASH_ANIMATION_REPEAT_COUNT      3u

/*
 * Blocking. Plays the splash animation SPLASH_ANIMATION_REPEAT_COUNT
 * times and leaves the final frame + text on screen.
 *
 * Preconditions: lcd_init() and lcd_tim7_init() must already have run.
 * Must be called before anything else touches CGRAM locations 2-7 (i.e.
 * before menu_repaintAll() / any check/heart/bell glyph usage).
 */
void splashAnimation_play(void);

#endif /* SPLASH_ANIMATION_H_ */
