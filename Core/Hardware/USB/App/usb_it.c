/*
 * usb_it.c
 *
 * OTG_FS interrupt handler for LXR-02.
 * Ported from stm32f4xx_it.c in the original LXR firmware.
 *
 * OTG_FS_IRQHandler is IRQ67 on STM32F765. The vector table entry in
 * startup_stm32f765xx.s points to this symbol by name.
 */

#include "usb_bsp.h"
#include "usb_dcd_int.h"
#include "usb_manager.h"

/* USB_OTG_dev is the global device handle, defined in usb_manager.c */
extern USB_OTG_CORE_HANDLE USB_OTG_dev;

/* -----------------------------------------------------------------------
** EXTI stub handlers — clear pending and return for unused lines.
** CLK IN (EXTI4) and RST IN (EXTI5 via EXTI9_5) are owned by
** triggerJacks.c now; do not define those symbols here.
** ----------------------------------------------------------------------- */
#define EXTI_PR  (*((volatile uint32_t *)0x40013C14UL))

void EXTI0_IRQHandler(void)     { EXTI_PR = (1UL << 0); }
void EXTI1_IRQHandler(void)     { EXTI_PR = (1UL << 1); }
void EXTI2_IRQHandler(void)     { EXTI_PR = (1UL << 2); }
void EXTI15_10_IRQHandler(void) { EXTI_PR = (0x3FUL << 10); } /* bits 10-15*/

void OTG_FS_IRQHandler(void)
{
    USBD_OTG_ISR_Handler(&USB_OTG_dev);
}
