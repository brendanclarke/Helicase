/**
 * usb_bsp.c
 *
 * Board support package for USB OTG FS on LXR-02 (STM32F765VIH6).
 * Ported from the original LXR firmware by Julian Schmidt.
 * StdPeriph calls replaced with bare-metal register access.
 *
 * Hardware:
 *   PA11 = OTG_FS_DM  (AF10, no pull)
 *   PA12 = OTG_FS_DP  (AF10, no pull)
 *   OTG_FS clock: AHB2 bit 7
 *   OTG_FS IRQ: IRQ67 (ISER2 bit 3)
 *
 * The ADUM3160 USB isolator sits between the USB-B connector and PA11/PA12.
 * It is transparent to firmware — PA11/PA12 are driven exactly as they would
 * be without isolation. VBUS detection is therefore not needed from the MCU
 * side (handled by isolator hardware), so USB_CONNECTED/USB_ENABLE GPIO
 * pins from the original schematic are not implemented here.
 *
 * Delays:
 *   USB_OTG_BSP_uDelay uses a spin loop calibrated for 216MHz SYSCLK.
 *   USB_OTG_BSP_mDelay calls uDelay × 1000.
 */

#include "usb_bsp.h"
#include "usbd_conf.h"

/* -----------------------------------------------------------------------
** Register definitions
** ----------------------------------------------------------------------- */

/* GPIOA */
#define GPIOA_BASE      0x40020000UL
#define GPIOA_MODER     (*((volatile uint32_t *)(GPIOA_BASE + 0x00UL)))
#define GPIOA_OSPEEDR   (*((volatile uint32_t *)(GPIOA_BASE + 0x08UL)))
#define GPIOA_PUPDR     (*((volatile uint32_t *)(GPIOA_BASE + 0x0CUL)))
#define GPIOA_AFRH      (*((volatile uint32_t *)(GPIOA_BASE + 0x24UL)))

/* RCC */
#define RCC_AHB1ENR     (*((volatile uint32_t *)0x40023830UL))
#define RCC_AHB2ENR     (*((volatile uint32_t *)0x40023834UL))

/* NVIC
** OTG_FS = IRQ67
** ISER2  = 0xE000E108  (covers IRQ64..IRQ95)
** IPR67  = priority byte for IRQ67
*/
#define NVIC_ISER2      (*((volatile uint32_t *)0xE000E108UL))
#define NVIC_IPR(n)     (*((volatile uint8_t  *)(0xE000E400UL + (n))))
#define IRQ_OTG_FS      67

/* -----------------------------------------------------------------------
** USB_OTG_BSP_Init
**
** Configures PA11 (DM) and PA12 (DP) as AF10 (OTG_FS), high speed,
** no pull. Enables OTG_FS peripheral clock on AHB2.
** NVIC enable is done separately in USB_OTG_BSP_EnableInterrupt.
** ----------------------------------------------------------------------- */
void USB_OTG_BSP_Init(USB_OTG_CORE_HANDLE *pdev)
{
    (void)pdev;

    /* GPIOA clock (AHB1 bit 0) — safe to set even if already enabled */
    RCC_AHB1ENR |= (1UL << 0);
    (void)RCC_AHB1ENR;

    /* PA11 — OTG_FS_DM, AF10
    ** PA12 — OTG_FS_DP, AF10
    **
    ** MODER[23:22] = 10 (AF), MODER[25:24] = 10 (AF)
    ** OSPEEDR: very high speed (11) on both
    ** PUPDR:   no pull (00) on both — USB uses internal pull-up via DCTL
    ** AFRH[15:12] = 10 (AF10 = OTG_FS) for PA11
    ** AFRH[19:16] = 10 (AF10 = OTG_FS) for PA12
    */
    GPIOA_MODER   &= ~((3UL << 22) | (3UL << 24));
    GPIOA_MODER   |=   (2UL << 22) | (2UL << 24);
    GPIOA_OSPEEDR |=   (3UL << 22) | (3UL << 24);
    GPIOA_PUPDR   &= ~((3UL << 22) | (3UL << 24));  /* no pull */
    GPIOA_AFRH    &= ~((0xFUL << 12) | (0xFUL << 16));
    GPIOA_AFRH    |=   (10UL << 12) | (10UL << 16);

    /* OTG_FS clock: AHB2 bit 7 */
    RCC_AHB2ENR |= (1UL << 7);
    (void)RCC_AHB2ENR;
}

/* -----------------------------------------------------------------------
** USB_OTG_BSP_EnableInterrupt
**
** Enables the OTG_FS global interrupt (IRQ67) in the NVIC.
** Priority set to match the original (preempt=1, sub=3 → combined 0x1C
** in the top 4 bits of the IPR byte, which gives preempt priority 1
** with a 4-bit priority group).
** ----------------------------------------------------------------------- */
void USB_OTG_BSP_EnableInterrupt(USB_OTG_CORE_HANDLE *pdev)
{
    (void)pdev;

    /* IPR67: priority in top 4 bits. Preempt=1 → 0x10, sub=3 → 0x03.
    ** With PRIGROUP default (group 0x03 = 4-bit preempt, 0-bit sub):
    ** priority byte = preempt << 4 = 0x10. Close enough to original intent. */
    NVIC_IPR(IRQ_OTG_FS) = 0x50;   /* preempt priority 5, lower than TIM6 */

    /* ISER2 bit 3 enables IRQ67 (67 - 64 = 3) */
    NVIC_ISER2 |= (1UL << (IRQ_OTG_FS - 64));
}

/* -----------------------------------------------------------------------
** USB_OTG_BSP_uDelay
**
** Spin-loop delay in microseconds, calibrated for 216MHz SYSCLK.
** The loop body is ~3 cycles on Cortex-M7 with -O1:
**   cycles_per_us = 216 / 3 = 72 iterations per µs.
** Called from usb_core.c during USB reset and enumeration.
** ----------------------------------------------------------------------- */
void USB_OTG_BSP_uDelay(const uint32_t usec)
{
    /* 216MHz / 3 cycles per iteration = 72 ticks per µs */
    uint32_t count = usec * 72UL;
    while (count--) {
        __asm volatile("nop");
    }
}

/* -----------------------------------------------------------------------
** USB_OTG_BSP_mDelay
**
** Millisecond delay — calls uDelay 1000 times.
** Called during USB core init (20ms, 50ms waits).
** ----------------------------------------------------------------------- */
void USB_OTG_BSP_mDelay(const uint32_t msec)
{
    USB_OTG_BSP_uDelay(msec * 1000UL);
}
