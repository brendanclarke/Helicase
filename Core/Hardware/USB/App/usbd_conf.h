/**
 * usbd_conf.h
 *
 * USB device library configuration for LXR-02.
 * Ported from original LXR firmware by Julian Schmidt.
 * No F4-specific dependencies — usb_conf.h include removed (already
 * included transitively via usb_bsp.h where needed).
 */

#ifndef __USBD_CONF__H__
#define __USBD_CONF__H__

#define USBD_CFG_MAX_NUM            1
#define USBD_ITF_MAX_NUM            2
#define USB_MAX_STR_DESC_SIZ        64
#define USBD_EP0_MAX_PACKET_SIZE    64

#define MIDI_OUT_EP                 0x01
#define MIDI_IN_EP                  0x81
#define MIDI_PACKET_SIZE            0x40

#endif /* __USBD_CONF__H__ */
