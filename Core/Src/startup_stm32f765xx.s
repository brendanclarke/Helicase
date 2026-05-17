/*
 * Core/Src/startup_stm32f765xx.s
 *
 *  Created on: 17.05.2026
 * ------------------------------------------------------------------------------------------------------------------------
 *  Copyright 2026 Brendan Clarke
 *  brendanpaulclarke@gmail.com
 *  https://www.brendanclarke.com
 * ------------------------------------------------------------------------------------------------------------------------
 *  This file is part of the LXR02 Open-Source software.
 * ------------------------------------------------------------------------------------------------------------------------
 *  Redistribution and use of the LXR02 Open-Source, hardware driver code, or any derivative works are permitted
 *  provided that the following conditions are met:
 *
 *       - The code may not be sold, nor may it be used in a commercial product or activity.
 *
 *       - Redistributions that are modified from the original source must include the complete
 *         source code, including the source code for all components used by a binary built
 *         from the modified sources. However, as a special exception, the source code distributed
 *         need not include anything that is normally distributed (in either source or binary form)
 *         with the major components (compiler, kernel, and so on) of the operating system on which
 *         the executable runs, unless that component itself accompanies the executable.
 *
 *       - Redistributions must reproduce the above copyright notice, this list of conditions and the
 *         following disclaimer in the documentation and/or other materials provided with the distribution.
 * ------------------------------------------------------------------------------------------------------------------------
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 *   INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *   DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 *   SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 *   WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 *   USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * ------------------------------------------------------------------------------------------------------------------------
 */

  .syntax unified
  .cpu cortex-m7
  .fpu softvfp
  .thumb

.global g_pfnVectors
.global Default_Handler

.word _estack

  .section .text.Reset_Handler
  .weak Reset_Handler
  .type Reset_Handler, %function
Reset_Handler:
  ldr   sp, =_estack

  /* Ensure M7 TCM interfaces are enabled before copying INITCM code. */
  ldr   r0, =0xE000EF90      /* SCB->ITCMCR */
  movs  r1, #7               /* EN | RMW | RETEN */
  str   r1, [r0]
  ldr   r0, =0xE000EF94      /* SCB->DTCMCR */
  str   r1, [r0]
  dsb
  isb

  /* Copy .itcm from flash (LMA = _siitcm) to ITCM
  ** (VMA = _sitcm.._eitcm) before any INITCM functions can be called. */
  movs  r1, #0
  b     LoopCopyItcmInit
CopyItcmInit:
  ldr   r3, =_siitcm
  ldr   r3, [r3, r1]
  str   r3, [r0, r1]
  adds  r1, r1, #4
LoopCopyItcmInit:
  ldr   r0, =_sitcm
  ldr   r3, =_eitcm
  adds  r2, r0, r1
  cmp   r2, r3
  bcc   CopyItcmInit

  movs  r1, #0
  b     LoopCopyDataInit
CopyDataInit:
  ldr   r3, =_sidata
  ldr   r3, [r3, r1]
  str   r3, [r0, r1]
  adds  r1, r1, #4
LoopCopyDataInit:
  ldr   r0, =_sdata
  ldr   r3, =_edata
  adds  r2, r0, r1
  cmp   r2, r3
  bcc   CopyDataInit
  ldr   r2, =_sbss
  b     LoopFillZerobss
FillZerobss:
  movs  r3, #0
  str   r3, [r2], #4
LoopFillZerobss:
  ldr   r3, =_ebss
  cmp   r2, r3
  bcc   FillZerobss

  /* Copy .dtcm from flash (LMA = _sidtcm) to DTCM (VMA = _sdtcm.._edtcm).
  ** Mirrors the .data copy above. Required for INDTCM-tagged variables
  ** to have their flash-stored initialisers at runtime. */
  movs  r1, #0
  b     LoopCopyDtcmInit
CopyDtcmInit:
  ldr   r3, =_sidtcm
  ldr   r3, [r3, r1]
  str   r3, [r0, r1]
  adds  r1, r1, #4
LoopCopyDtcmInit:
  ldr   r0, =_sdtcm
  ldr   r3, =_edtcm
  adds  r2, r0, r1
  cmp   r2, r3
  bcc   CopyDtcmInit

  /* Zero .dtcmz (INDTCMZ — uninitialised DTCM data). */
  ldr   r2, =_sdtcmz
  b     LoopFillZeroDtcmz
FillZeroDtcmz:
  movs  r3, #0
  str   r3, [r2], #4
LoopFillZeroDtcmz:
  ldr   r3, =_edtcmz
  cmp   r2, r3
  bcc   FillZeroDtcmz

  bl    main
  bx    lr
.size Reset_Handler, .-Reset_Handler

  .section .text.Default_Handler,"ax",%progbits
Default_Handler:
Infinite_Loop:
  b Infinite_Loop
.size Default_Handler, .-Default_Handler

  .section .isr_vector,"a",%progbits
  .type g_pfnVectors, %object
g_pfnVectors:
  .word _estack
  .word Reset_Handler
  .word NMI_Handler
  .word HardFault_Handler
  .word MemManage_Handler
  .word BusFault_Handler
  .word UsageFault_Handler
  .word 0
  .word 0
  .word 0
  .word 0
  .word SVC_Handler
  .word DebugMon_Handler
  .word 0
  .word PendSV_Handler
  .word SysTick_Handler
  /* IRQ0..IRQ55: Default_Handler for all except the ones we define */
  .word Default_Handler   /* IRQ0  WWDG */
  .word Default_Handler   /* IRQ1  PVD */
  .word Default_Handler   /* IRQ2  TAMP_STAMP */
  .word Default_Handler   /* IRQ3  RTC_WKUP */
  .word Default_Handler   /* IRQ4  FLASH */
  .word Default_Handler   /* IRQ5  RCC */
  .word Default_Handler   /* IRQ6  EXTI0 */
  .word Default_Handler   /* IRQ7  EXTI1 */
  .word Default_Handler   /* IRQ8  EXTI2 */
  .word Default_Handler   /* IRQ9  EXTI3 */
  .word EXTI4_IRQHandler  /* IRQ10 EXTI4 — CLK IN PD4 */
  .word Default_Handler   /* IRQ11 DMA1_S0 */
  .word Default_Handler   /* IRQ12 DMA1_S1 */
  .word Default_Handler   /* IRQ13 DMA1_S2 */
  .word Default_Handler   /* IRQ14 DMA1_S3 */
  .word DMA1_Stream4_IRQHandler   /* IRQ15 DMA1_S4 — DAC2 (I2S2) HT/TC */
  .word Default_Handler   /* IRQ16 DMA1_S5 */
  .word Default_Handler   /* IRQ17 DMA1_S6 */
  .word Default_Handler   /* IRQ18 ADC */
  .word Default_Handler   /* IRQ19 CAN1_TX */
  .word Default_Handler   /* IRQ20 CAN1_RX0 */
  .word Default_Handler   /* IRQ21 CAN1_RX1 */
  .word Default_Handler   /* IRQ22 CAN1_SCE */
  .word EXTI9_5_IRQHandler /* IRQ23 EXTI9_5 — RST IN PD5 */
  .word Default_Handler   /* IRQ24 TIM1_BRK_TIM9 */
  .word Default_Handler   /* IRQ25 TIM1_UP_TIM10 */
  .word Default_Handler   /* IRQ26 TIM1_TRG_COM_TIM11 */
  .word TIM1_CC_IRQHandler        /* IRQ27 TIM1_CC — encoder input capture */
  .word Default_Handler   /* IRQ28 TIM2 */
  .word TIM3_IRQHandler   /* IRQ29 TIM3 — sequencer timing owner */
  .word Default_Handler   /* IRQ30 TIM4 */
  .word Default_Handler   /* IRQ31 I2C1_EV */
  .word Default_Handler   /* IRQ32 I2C1_ER */
  .word Default_Handler   /* IRQ33 I2C2_EV */
  .word Default_Handler   /* IRQ34 I2C2_ER */
  .word Default_Handler   /* IRQ35 SPI1 */
  .word Default_Handler   /* IRQ36 SPI2 */
  .word Default_Handler   /* IRQ37 USART1 */
  .word Default_Handler   /* IRQ38 USART2 */
  .word USART3_IRQHandler /* IRQ39 USART3 — MIDI DIN RX/TX */
  .word Default_Handler   /* IRQ40 EXTI15_10 */
  .word Default_Handler   /* IRQ41 RTC_Alarm */
  .word Default_Handler   /* IRQ42 OTG_FS_WKUP */
  .word Default_Handler   /* IRQ43 TIM8_BRK_TIM12 */
  .word Default_Handler   /* IRQ44 TIM8_UP_TIM13 */
  .word Default_Handler   /* IRQ45 TIM8_TRG_COM_TIM14 — unused */
  .word Default_Handler   /* IRQ46 TIM8_CC */
  .word DMA1_Stream7_IRQHandler   /* IRQ47 DMA1_S7 — DAC1 (I2S3) HT/TC */
  .word Default_Handler   /* IRQ48 FMC */
  .word Default_Handler   /* IRQ49 SDMMC1 */
  .word Default_Handler   /* IRQ50 TIM5 */
  .word Default_Handler   /* IRQ51 SPI3 */
  .word Default_Handler   /* IRQ52 UART4 */
  .word Default_Handler   /* IRQ53 UART5 */
  .word TIM6_DAC_IRQHandler    /* IRQ54 TIM6_DAC */
  .word TIM7_IRQHandler        /* IRQ55 TIM7 — LCD state machine (10kHz) */
  .rept 11
  .word Default_Handler        /* IRQ56..IRQ66 */
  .endr
  .word OTG_FS_IRQHandler      /* IRQ67 OTG_FS */
  .rept 30
  .word Default_Handler        /* IRQ68..IRQ97 */
  .endr

  .weak NMI_Handler
  .thumb_set NMI_Handler,Default_Handler
  .weak HardFault_Handler
  .thumb_set HardFault_Handler,Default_Handler
  .weak MemManage_Handler
  .thumb_set MemManage_Handler,Default_Handler
  .weak BusFault_Handler
  .thumb_set BusFault_Handler,Default_Handler
  .weak UsageFault_Handler
  .thumb_set UsageFault_Handler,Default_Handler
  .weak SVC_Handler
  .thumb_set SVC_Handler,Default_Handler
  .weak DebugMon_Handler
  .thumb_set DebugMon_Handler,Default_Handler
  .weak PendSV_Handler
  .thumb_set PendSV_Handler,Default_Handler
  .weak SysTick_Handler
  .thumb_set SysTick_Handler,Default_Handler
  /* OTG_FS: weak alias — usb_it.c defines a strong OTG_FS_IRQHandler */
  .weak OTG_FS_IRQHandler
  .thumb_set OTG_FS_IRQHandler,Default_Handler
  .weak EXTI0_IRQHandler
  .thumb_set EXTI0_IRQHandler,Default_Handler
  .weak EXTI1_IRQHandler
  .thumb_set EXTI1_IRQHandler,Default_Handler
  .weak EXTI2_IRQHandler
  .thumb_set EXTI2_IRQHandler,Default_Handler
  .weak EXTI3_IRQHandler
  .thumb_set EXTI3_IRQHandler,Default_Handler
  .weak EXTI4_IRQHandler
  .thumb_set EXTI4_IRQHandler,Default_Handler
  .weak EXTI9_5_IRQHandler
  .thumb_set EXTI9_5_IRQHandler,Default_Handler
  .weak EXTI15_10_IRQHandler
  .thumb_set EXTI15_10_IRQHandler,Default_Handler
  /* DMA1_Stream4/Stream7: strong symbols in AudioCodecManager.c when audio
  ** path is wired. Weak aliases so the binary still links during early
  ** integration if those handlers aren't yet defined. */
  .weak DMA1_Stream4_IRQHandler
  .thumb_set DMA1_Stream4_IRQHandler,Default_Handler
  .weak DMA1_Stream7_IRQHandler
  .thumb_set DMA1_Stream7_IRQHandler,Default_Handler
  .weak USART3_IRQHandler
  .thumb_set USART3_IRQHandler,Default_Handler
  .weak TIM3_IRQHandler
  .thumb_set TIM3_IRQHandler,Default_Handler
  /* TIM6_DAC: strong symbol in timebase.c — no weak alias needed */
  /* TIM7: strong symbol in timebase.c — no weak alias needed */
