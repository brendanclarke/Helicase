/*
 * screensaver.h
 *
 * Created: 25.01.2013 17:07:52
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
 


#ifndef SCREENSAVER_H_
#define SCREENSAVER_H_

#include <stdint.h>

void screensaver_touch();
void screensaver_check();
uint8_t screensaver_isActive(void);

extern volatile uint16_t screensaver_timer;

#endif /* SCREENSAVER_H_ */
