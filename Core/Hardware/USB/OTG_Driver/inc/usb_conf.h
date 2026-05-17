/**
  ******************************************************************************
  * @file    usb_conf.h
  * Ported from original LXR firmware by Julian Schmidt.
  * LXR-02 changes:
  *   - stm32f4xx.h replaced with stdint.h + __IO define
  *   - USE_USB_OTG_FS defined (FS core only, no HS/ULPI)
  *   - VBUS_SENSING_ENABLED intentionally NOT defined (ADUM3160 handles isolation)
  ******************************************************************************
  */

#ifndef __USB_CONF__H__
#define __USB_CONF__H__

#include <stdint.h>

/* __IO maps to volatile — used throughout the USB library for register fields */
#ifndef __IO
#define __IO volatile
#endif

/* FS core only — embedded PHY, device mode */
#ifndef USE_USB_OTG_FS
 #define USE_USB_OTG_FS
#endif

#ifdef USE_USB_OTG_FS
 #define USB_OTG_FS_CORE
#endif

#ifndef USE_DEVICE_MODE
 #define USE_DEVICE_MODE
#endif

#ifndef USB_OTG_FS_CORE
 #ifndef USB_OTG_HS_CORE
  #error "USB_OTG_HS_CORE or USB_OTG_FS_CORE should be defined"
 #endif
#endif

#ifndef USE_DEVICE_MODE
 #ifndef USE_HOST_MODE
  #error "USE_DEVICE_MODE or USE_HOST_MODE should be defined"
 #endif
#endif

#ifndef USE_USB_OTG_HS
 #ifndef USE_USB_OTG_FS
  #error "USE_USB_OTG_HS or USE_USB_OTG_FS should be defined"
 #endif
#endif

/****************** USB OTG FS FIFO sizes (device mode) ***********************/
#ifdef USB_OTG_FS_CORE
 #define RX_FIFO_FS_SIZE      128
 #define TX0_FIFO_FS_SIZE     128
 #define TX1_FIFO_FS_SIZE     128
 #define TX2_FIFO_FS_SIZE       0
 #define TX3_FIFO_FS_SIZE       0
 #define TXH_NP_FS_FIFOSIZ     96
 #define TXH_P_FS_FIFOSIZ      96
#endif

/****************** Compiler keywords *****************************************/
#ifndef __packed
 #define __packed  __attribute__((__packed__))
#endif

#ifndef __ALIGN_BEGIN
 #define __ALIGN_BEGIN
#endif
#ifndef __ALIGN_END
 #define __ALIGN_END
#endif

#endif /* __USB_CONF__H__ */
