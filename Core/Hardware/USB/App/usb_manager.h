/*
 * usb_manager.h
 *
 * Ported from original LXR firmware by Julian Schmidt.
 * stm32f4xx.h dependency replaced with stdint.h.
 * USB_DETECT_PIN/PORT removed (not used in LXR-02, see usb_manager.c).
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


#ifndef USB_MANAGER_H_
#define USB_MANAGER_H_

#include <stdint.h>
#include "usbd_conf.h"
#include "usbd_usr.h"
#include "usb_midi_core.h"
#include "MidiMessages.h"

void    usb_init(void);
void    usb_stop(void);
void    usb_start(void);
void    usb_tick(void);
void    usb_sendMidi(MidiMsg msg);
uint8_t usb_getMidi(MidiMsg *msg);
void    usb_flushMidi(void);

#endif /* USB_MANAGER_H_ */
