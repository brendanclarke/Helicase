/*
 * Core/Hardware/AudioCodecManager.c
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

/*
 * AudioCodecManager.c — LXR-02 audio path manager (STM32F765 port).
 *
 * This file consolidates all audio hardware setup and buffer management that
 * was previously spread across AudioCodecManager.c, audioTest.c, and
 * sineBufferTest.c. Call audioCodec_init() once from main() before the main
 * loop; everything else runs autonomously via DMA ISRs.
 *
 * =========================================================================
 * PIPELINE OVERVIEW
 * =========================================================================
 *
 *   main.c audio_check_and_render()
 *       writes sample_mx_t interleaved stereo into:
 *           audioOutBuffer [slot][0..AUDIO_DMA_FRAMES*2-1]
 *           audioOutBuffer2[slot][0..AUDIO_DMA_FRAMES*2-1]
 *       by stitching together canonical OUTPUT_DMA_SIZE DSP sub-blocks.
 *
 *   audioCodec_commitRenderBuffer()
 *       pushes slot into the 2-slot SPSC ready queue.
 *
 *   DMA1_Stream4_IRQHandler (HT or TC)  <-- refill master
 *       calls pack_audio_half(), which dequeues one slot from the ready
 *       queue and pack_half()-packs it into the just-consumed half of
 *       dma_buffer / dma_buffer2.
 *
 *   DMA1_Stream7_IRQHandler (HT or TC)  <-- slave
 *       clears its own flags; no pack needed — Stream 4 drives both buffers.
 *
 *   DMA1 circular mode
 *       feeds dma_buffer  → SPI2_DR (I2S2, DAC2 U24)
 *       feeds dma_buffer2 → SPI3_DR (I2S3, DAC1 U23)
 *
 * =========================================================================
 * 24-BIT PACK FORMAT
 * =========================================================================
 *
 *   Mixer writes sample_mx_t. I2S is configured for 24-bit data / 32-bit channel
 *   (DATLEN=01, CHLEN=1). SPI_DR is always 16-bit, so each 24-bit sample
 *   occupies two 16-bit halfword transfers (MSW then LSW). LSW is forced to
 *   lower 8 bits are dropped before pack, then the resulting signed 24-bit
 *   payload is emitted as MSW+LSW halfwords.
 *
 *   Per stereo frame in dma_buffer*:
 *       [MSW_L, LSW_L, MSW_R, LSW_R]  x  AUDIO_DMA_FRAMES  per half
 *   Total buffer: AUDIO_DMA_FRAMES * 8 halfwords (two halves).
 *
 * =========================================================================
 * SPSC READY QUEUE
 * =========================================================================
 *
 *   Two-slot queue replaces the original bCurrentSampleValid flag. Main loop
 *   commits fully rendered AUDIO_DMA_FRAMES slots; DMA ISR consumes them.
 *   The commit path briefly masks interrupts so ready_count/head/tail updates
 *   cannot race the DMA ISR.
 *
 *   Slots:  0 or 1  (index into audioOutBuffer* ping-pong)
 *   ready_count: 0 empty, 1 one queued slot, 2 full.
 *
 * =========================================================================
 * HARDWARE ASSIGNMENTS (confirmed, do not change)
 * =========================================================================
 *
 *   DAC1 (U23 CS4344):  I2S3  -> DMA1 Stream 7  IRQ47  (slave)
 *       PA15=I2S3_WS AF6  PC10=I2S3_CK AF6  PB5=I2S3_SD AF6  PC7=I2S3_MCK AF6
 *
 *   DAC2 (U24 CS4344):  I2S2  -> DMA1 Stream 4  IRQ15  (master, drives both)
 *       PB12=I2S2_WS AF5  PB13=I2S2_CK AF5  PB15=I2S2_SD AF5  PC6=I2S2_MCK AF5
 *
 *   PLLI2S: N=271, R=2 -> PLLI2SCLK = 135.5MHz -> I2S prescaler /6 -> Fs=44108Hz
 *
 * =========================================================================
 * ISR SCHEDULING NOTE
 * =========================================================================
 *
 *   Both DMA streams run circular with HTIE+TCIE. Stream 4 (IRQ15) fires
 *   at PCLK1/2-rate half-periods; Stream 7 (IRQ47) fires at the same rate
 *   but is a slave (no pack, flags only). Both ISRs share NVIC priority 4,
 *   so they can pre-empt the main loop but not each other.
 *
 *   Stream 4 owns ready_head while the main loop owns ready_tail. Both touch
 *   ready_count, so commit masks interrupts for the few stores that publish a
 *   newly rendered slot.
 *
 *   Future work: the two ISRs are not synchronised with each other. Stream 7
 *   may fire slightly ahead of or behind Stream 4, meaning DAC1 and DAC2 may
 *   drift by up to one ISR period (~1ms). For a stereo instrument where both
 *   DACs carry different outputs this could cause minor inter-channel phase
 *   error. A future fix would make Stream 7 the master and Stream 4 the
 *   slave, or use a single DMA stream with a larger interleaved buffer.
 *   For now the audible impact is negligible.
 */

#include "AudioCodecManager.h"
#include <string.h>
#include <math.h>

/* =========================================================================
** SECTION 1: REGISTER DEFINITIONS
** =========================================================================
**
** All bare-metal register macros used in this file.
*/

/* RCC */
#define RCC_BASE        0x40023800UL
#define RCC_CR          (*((volatile uint32_t *)(RCC_BASE + 0x00UL)))
#define RCC_PLLCFGR     (*((volatile uint32_t *)(RCC_BASE + 0x04UL)))
#define RCC_AHB1RSTR    (*((volatile uint32_t *)(RCC_BASE + 0x10UL)))
#define RCC_APB1RSTR    (*((volatile uint32_t *)(RCC_BASE + 0x20UL)))
#define RCC_AHB1ENR     (*((volatile uint32_t *)(RCC_BASE + 0x30UL)))
#define RCC_APB1ENR     (*((volatile uint32_t *)(RCC_BASE + 0x40UL)))
#define RCC_PLLI2SCFGR  (*((volatile uint32_t *)(RCC_BASE + 0x84UL)))

/* GPIO */
#define GPIO_MODER(b)   (*((volatile uint32_t *)((b) + 0x00UL)))
#define GPIO_OSPEEDR(b) (*((volatile uint32_t *)((b) + 0x08UL)))
#define GPIO_AFRL(b)    (*((volatile uint32_t *)((b) + 0x20UL)))
#define GPIO_AFRH(b)    (*((volatile uint32_t *)((b) + 0x24UL)))
#define GPIOA_BASE      0x40020000UL
#define GPIOB_BASE      0x40020400UL
#define GPIOC_BASE      0x40020800UL

/* SPI/I2S */
#define SPI2_BASE       0x40003800UL
#define SPI2_CR2        (*((volatile uint32_t *)(SPI2_BASE + 0x04UL)))
#define SPI2_I2SCFGR    (*((volatile uint32_t *)(SPI2_BASE + 0x1CUL)))
#define SPI2_I2SPR      (*((volatile uint32_t *)(SPI2_BASE + 0x20UL)))
#define SPI2_DR_ADDR    (SPI2_BASE + 0x0CUL)

#define SPI3_BASE       0x40003C00UL
#define SPI3_CR2        (*((volatile uint32_t *)(SPI3_BASE + 0x04UL)))
#define SPI3_I2SCFGR    (*((volatile uint32_t *)(SPI3_BASE + 0x1CUL)))
#define SPI3_I2SPR      (*((volatile uint32_t *)(SPI3_BASE + 0x20UL)))
#define SPI3_DR_ADDR    (SPI3_BASE + 0x0CUL)

/* DMA1 */
#define DMA1_BASE_ADDR  0x40026000UL
#define DMA1_HISR       (*((volatile uint32_t *)(DMA1_BASE_ADDR + 0x04UL)))
#define DMA1_HIFCR      (*((volatile uint32_t *)(DMA1_BASE_ADDR + 0x0CUL)))
#define DMA1_S4CR       (*((volatile uint32_t *)(DMA1_BASE_ADDR + 0x70UL)))
#define DMA1_S4NDTR     (*((volatile uint32_t *)(DMA1_BASE_ADDR + 0x74UL)))
#define DMA1_S4PAR      (*((volatile uint32_t *)(DMA1_BASE_ADDR + 0x78UL)))
#define DMA1_S4M0AR     (*((volatile uint32_t *)(DMA1_BASE_ADDR + 0x7CUL)))
#define DMA1_S7CR       (*((volatile uint32_t *)(DMA1_BASE_ADDR + 0xB8UL)))
#define DMA1_S7NDTR     (*((volatile uint32_t *)(DMA1_BASE_ADDR + 0xBCUL)))
#define DMA1_S7PAR      (*((volatile uint32_t *)(DMA1_BASE_ADDR + 0xC0UL)))
#define DMA1_S7M0AR     (*((volatile uint32_t *)(DMA1_BASE_ADDR + 0xC4UL)))
/* HISR/HIFCR Stream 4 bits */
#define DMA_HISR_TCIF4   (1UL << 5)
#define DMA_HISR_HTIF4   (1UL << 4)
#define DMA_HIFCR_CTCIF4 (1UL << 5)
#define DMA_HIFCR_CHTIF4 (1UL << 4)
/* HISR/HIFCR Stream 7 bits */
#define DMA_HISR_TCIF7   (1UL << 27)
#define DMA_HISR_HTIF7   (1UL << 26)
#define DMA_HIFCR_CTCIF7 (1UL << 27)
#define DMA_HIFCR_CHTIF7 (1UL << 26)

/* NVIC */
#define NVIC_ISER0      (*((volatile uint32_t *)0xE000E100UL))
#define NVIC_ISER1      (*((volatile uint32_t *)0xE000E104UL))
#define NVIC_ICER0      (*((volatile uint32_t *)0xE000E180UL))
#define NVIC_ICER1      (*((volatile uint32_t *)0xE000E184UL))
#define NVIC_ICPR0      (*((volatile uint32_t *)0xE000E280UL))
#define NVIC_ICPR1      (*((volatile uint32_t *)0xE000E284UL))
#define NVIC_IPR        ((volatile uint8_t *)0xE000E400UL)
#define IRQ_DMA1_S4     15    /* in ISER0 */
#define IRQ_DMA1_S7     47    /* in ISER1 (covers IRQ32-63) */

/* =========================================================================
** SECTION 2: BUFFER DECLARATIONS
** =========================================================================
**
** dma_buffer / dma_buffer2 must be in SRAM1 (not DTCM) — DMA cannot reach
** the Cortex-M7 DTCM region.
*/

volatile int16_t dma_buffer  [AUDIO_DMA_FRAMES * 8] __attribute__((section(".dma_nocache")));
volatile int16_t dma_buffer2 [AUDIO_DMA_FRAMES * 8] __attribute__((section(".dma_nocache")));

INDTCMZ sample_mx_t audioOutBuffer  [2][AUDIO_DMA_FRAMES * 2];
INDTCMZ sample_mx_t audioOutBuffer2 [2][AUDIO_DMA_FRAMES * 2];

/* =========================================================================
** SECTION 3: QUEUE STATE
** =========================================================================
*/

static volatile uint8_t ready_queue[2]  = {0, 0};
static volatile uint8_t ready_head      = 0;
static volatile uint8_t ready_tail      = 0;
static volatile uint8_t ready_count     = 0;
static volatile uint8_t render_slot     = 0;
static volatile uint8_t last_played_slot = 0;
static volatile uint8_t audio_hw_initialized = 0;
/*
 * Authoritative codec-hardware suspension state; no S058 allocation.
 *
 * Writers: init/resume clear it only after hardware is live; suspend sets it
 * only after I2S/DMA/PLLI2S stop and queue reset. Readers:
 * audioCodec_isSuspended(), main's renderer, and Menu storage ownership.
 * Volatile preserves observation across the foreground/IRQ hardware boundary.
 * This is a boolean, not a reference count; callers must not resume a state
 * they do not own.
 */
static volatile uint8_t audio_hw_suspended = 0;

volatile uint32_t audioCodec_underrunCount = 0;
volatile uint32_t audioCodec_renderCount   = 0;

/* DWT_CYCCNT-based queue pressure accounting. The queue state only changes
** when the foreground commits a rendered slot or the DMA ISR consumes one, so
** event timestamps are both cheaper and more accurate than timer sampling. */
#define DEMCR_REG          (*((volatile uint32_t *)0xE000EDFCUL))
#define DWT_CTRL_REG       (*((volatile uint32_t *)0xE0001000UL))
#define DWT_CYCCNT_REG     (*((volatile uint32_t *)0xE0001004UL))
#define DWT_LAR_REG        (*((volatile uint32_t *)0xE0001FB0UL))
#define DEMCR_TRCENA       (1UL << 24)
#define DWT_CTRL_CYCCNTENA (1UL << 0)
#define DWT_LAR_UNLOCK     0xC5ACCE55UL

static volatile uint32_t queue_last_cycle = 0;
static volatile uint32_t queue_window_total_cycles = 0;
static volatile uint32_t queue_window_free_cycles = 0;
static volatile uint8_t  queue_last_percent = 0;

/* =========================================================================
** SECTION 4: RENDER BUFFER PUBLIC API
** =========================================================================
*/

sample_mx_t *audioCodec_getRenderBuffer(void)  { return audioOutBuffer [render_slot]; }
sample_mx_t *audioCodec_getRenderBuffer2(void) { return audioOutBuffer2[render_slot]; }

static void audioCodec_resetQueueState(void)
{
    render_slot          = 0;
    ready_head           = 0;
    ready_tail           = 0;
    ready_count          = 0;
    last_played_slot     = 0;
    ready_queue[0]       = 0;
    ready_queue[1]       = 0;
}

static uint32_t irq_save(void)
{
    uint32_t primask;
    __asm volatile("mrs %0, primask\ncpsid i" : "=r"(primask) :: "memory");
    return primask;
}

static void irq_restore(uint32_t primask)
{
    __asm volatile("msr primask, %0" :: "r"(primask) : "memory");
}

static void audioCodec_initCycleCounter(void)
{
    DEMCR_REG |= DEMCR_TRCENA;
    DWT_LAR_REG = DWT_LAR_UNLOCK;
    DWT_CYCCNT_REG = 0;
    DWT_CTRL_REG |= DWT_CTRL_CYCCNTENA;

    queue_last_cycle = DWT_CYCCNT_REG;
    queue_window_total_cycles = 0;
    queue_window_free_cycles = 0;
    queue_last_percent = 0;
}

static void audioCodec_accountQueueState(void)
{
    uint32_t now = DWT_CYCCNT_REG;
    uint32_t elapsed = now - queue_last_cycle;

    queue_last_cycle = now;
    queue_window_total_cycles += elapsed;
    if (ready_count < 2u)
        queue_window_free_cycles += elapsed;
}

uint8_t audioCodec_queueFreeSlots(void)
{
    return (uint8_t)(2u - ready_count);
}

uint8_t audioCodec_getQueueFreePercent(void)
{
    uint32_t total;
    uint32_t free_cycles;
    uint8_t percent;
    uint32_t primask = irq_save();

    audioCodec_accountQueueState();
    total = queue_window_total_cycles;
    free_cycles = queue_window_free_cycles;
    queue_window_total_cycles = 0;
    queue_window_free_cycles = 0;
    irq_restore(primask);

    if (total == 0u) {
        percent = queue_last_percent;
    } else {
        uint32_t scaled = (uint32_t)(((uint64_t)free_cycles * 100u + (total / 2u)) / total);
        if (scaled > 100u)
            scaled = 100u;
        percent = (uint8_t)scaled;
        queue_last_percent = percent;
    }

    return percent;
}

void audioCodec_commitRenderBuffer(void)
{
    uint32_t primask = irq_save();
    audioCodec_accountQueueState();
    if (ready_count < 2u) {
        ready_queue[ready_tail] = render_slot;
        ready_tail  ^= 1u;
        render_slot ^= 1u;
        ready_count++;
    }
    irq_restore(primask);
    audioCodec_renderCount++;
}

/* =========================================================================
** SECTION 5: INTERNAL PACK HELPERS
** =========================================================================
*/

#define SAMPLE_S24_MAX           8388607
#define SAMPLE_S24_MIN          -8388608

static inline int32_t sampleMix_toS24(sample_mx_t x)
{
    int64_t s = (int64_t)x;

    if (s > SAMPLE_S24_MAX) s = SAMPLE_S24_MAX;
    if (s < SAMPLE_S24_MIN) s = SAMPLE_S24_MIN;
    return (int32_t)s;
}

static void pack_half(volatile int16_t *dst, const sample_mx_t *src)
{
    for (uint32_t i = 0; i < AUDIO_DMA_FRAMES; i++) {
        int32_t left = sampleMix_toS24(src[2*i + 0]);
        int32_t right = sampleMix_toS24(src[2*i + 1]);
        uint32_t left_frame = ((uint32_t)(left & 0x00FFFFFF)) << 8;
        uint32_t right_frame = ((uint32_t)(right & 0x00FFFFFF)) << 8;

        dst[4*i + 0] = (int16_t)(left_frame >> 16);   /* MSW left  */
        dst[4*i + 1] = (int16_t)(left_frame & 0xFFFF); /* LSW left  */
        dst[4*i + 2] = (int16_t)(right_frame >> 16);  /* MSW right */
        dst[4*i + 3] = (int16_t)(right_frame & 0xFFFF); /* LSW right */
    }
}

static void pack_audio_half(uint8_t half)
{
    volatile int16_t *dst1 = &dma_buffer [half ? AUDIO_DMA_FRAMES * 4 : 0];
    volatile int16_t *dst2 = &dma_buffer2[half ? AUDIO_DMA_FRAMES * 4 : 0];

    uint8_t slot;
    audioCodec_accountQueueState();
    if (ready_count > 0u) {
        slot = ready_queue[ready_head];
        ready_head ^= 1u;
        ready_count--;
    } else {
        audioCodec_underrunCount++;
        slot = last_played_slot;
    }
    last_played_slot = slot;
    pack_half(dst1, audioOutBuffer [slot]);
    pack_half(dst2, audioOutBuffer2[slot]);
}

/* =========================================================================
** SECTION 6: DMA ISR HANDLERS
** =========================================================================
**
** Stream 4 (IRQ15) is the refill master. HT -> repack first half (half=0),
** TC -> repack second half (half=1). Both dma_buffer and dma_buffer2 are
** packed in the same call to keep both DAC outputs frame-aligned.
**
** Stream 7 (IRQ47) is the slave for DAC1 (I2S3). It only clears flags.
*/

void DMA1_Stream4_IRQHandler(void)
{
    uint32_t hisr = DMA1_HISR;
    if (hisr & DMA_HISR_TCIF4) {
        DMA1_HIFCR = DMA_HIFCR_CTCIF4;
        pack_audio_half(1);
    }
    if (hisr & DMA_HISR_HTIF4) {
        DMA1_HIFCR = DMA_HIFCR_CHTIF4;
        pack_audio_half(0);
    }
}

void DMA1_Stream7_IRQHandler(void)
{
    uint32_t hisr = DMA1_HISR;
    if (hisr & DMA_HISR_TCIF7) {
        DMA1_HIFCR = DMA_HIFCR_CTCIF7;
        /* Slave — no pack. Stream 4 is the refill master for both DACs. */
    }
    if (hisr & DMA_HISR_HTIF7) {
        DMA1_HIFCR = DMA_HIFCR_CHTIF7;
        /* Slave — no pack. */
    }
}

/* =========================================================================
** SECTION 7: HARDWARE INITIALISATION (static helpers)
** =========================================================================
**
** Previously in audioTest.c. Now private to this translation unit.
**
** plli2s_init: start PLLI2S for I2S clocking.
**   The if(!(HSERDY)) guard is never entered after sysclk_init() — HSE is
**   always up by then. The guard body exists only as a safe fallback.
**   sysclk_init() sets PLLM=16 and PLLSRC=HSE (shared with PLLI2S), so
**   those fields are inherited here without being re-written.
**   See clocks.c comment on PLLCFGR for the full shared-field rationale.
*/
static void plli2s_init(void)
{
    if (!(RCC_CR & (1UL << 17))) {
        RCC_CR |= (1UL << 16);
        uint32_t t = 200000UL;
        while (!(RCC_CR & (1UL << 17)) && --t);
        RCC_PLLCFGR &= ~((0x3FUL << 0) | (1UL << 22));
        RCC_PLLCFGR |= (16UL << 0) | (1UL << 22);
    }
    if (RCC_CR & (1UL << 26)) {
        RCC_CR &= ~(1UL << 26);
        while (RCC_CR & (1UL << 27));
    }

    /* N=271, R=2, Q=2 -> PLLI2SCLK = 135.5MHz -> Fs = 44108Hz */
    RCC_PLLI2SCFGR = (271UL << 6) | (2UL << 20) | (2UL << 28);
    RCC_CR |= (1UL << 26);
    while (!(RCC_CR & (1UL << 27)));
}

static void plli2s_stop(void)
{
    RCC_CR &= ~(1UL << 26);
    while (RCC_CR & (1UL << 27));
}

static void gpio_init(void)
{
    RCC_AHB1ENR |= (1UL<<0)|(1UL<<1)|(1UL<<2);
    (void)RCC_AHB1ENR;

    /* PA15 = I2S3_WS AF6 */
    GPIO_MODER(GPIOA_BASE)   &= ~(3UL << 30);
    GPIO_MODER(GPIOA_BASE)   |=  (2UL << 30);
    GPIO_OSPEEDR(GPIOA_BASE) |=  (3UL << 30);
    GPIO_AFRH(GPIOA_BASE)    &= ~(0xFUL << 28);
    GPIO_AFRH(GPIOA_BASE)    |=  (6UL  << 28);

    /* PB5=I2S3_SD AF6, PB12=I2S2_WS AF5, PB13=I2S2_CK AF5, PB15=I2S2_SD AF5 */
    GPIO_MODER(GPIOB_BASE)   &= ~((3UL<<10)|(3UL<<24)|(3UL<<26)|(3UL<<30));
    GPIO_MODER(GPIOB_BASE)   |=   (2UL<<10)|(2UL<<24)|(2UL<<26)|(2UL<<30);
    GPIO_OSPEEDR(GPIOB_BASE) |=   (3UL<<10)|(3UL<<24)|(3UL<<26)|(3UL<<30);
    GPIO_AFRL(GPIOB_BASE)    &= ~(0xFUL << 20);
    GPIO_AFRL(GPIOB_BASE)    |=  (6UL   << 20);
    GPIO_AFRH(GPIOB_BASE)    &= ~((0xFUL<<16)|(0xFUL<<20)|(0xFUL<<28));
    GPIO_AFRH(GPIOB_BASE)    |=   (5UL<<16) |(5UL<<20) |(5UL<<28);

    /* PC6=I2S2_MCK AF5, PC7=I2S3_MCK AF6, PC10=I2S3_CK AF6 */
    GPIO_MODER(GPIOC_BASE)   &= ~((3UL<<12)|(3UL<<14)|(3UL<<20));
    GPIO_MODER(GPIOC_BASE)   |=   (2UL<<12)|(2UL<<14)|(2UL<<20);
    GPIO_OSPEEDR(GPIOC_BASE) |=   (3UL<<12)|(3UL<<14)|(3UL<<20);
    GPIO_AFRL(GPIOC_BASE)    &= ~((0xFUL<<24)|(0xFUL<<28));
    GPIO_AFRL(GPIOC_BASE)    |=   (5UL<<24) |(6UL<<28);
    GPIO_AFRH(GPIOC_BASE)    &= ~(0xFUL<<8);
    GPIO_AFRH(GPIOC_BASE)    |=  (6UL<<8);
}

static void dma_init(void)
{
    RCC_AHB1ENR |= (1UL << 21);
    (void)RCC_AHB1ENR;

    /* SxCR: DIR=mem->periph, CIRC, MINC, PSIZE=halfword, MSIZE=halfword,
    ** PL=high, HTIE, TCIE. See RM0410 S8.5.5. */
    const uint32_t cr_base     = (2UL<<16)|(1UL<<13)|(1UL<<11)|(1UL<<10)
                                |(1UL<<8) |(1UL<<6);
    const uint32_t cr_with_irqs = cr_base | (1UL<<4) | (1UL<<3);

    /* DAC1 -> DMA1 Stream 7 (slave) */
    DMA1_S7CR   = 0; while (DMA1_S7CR & 1UL);
    DMA1_HIFCR  = (0x3FUL << 22);
    DMA1_S7PAR  = SPI3_DR_ADDR;
    DMA1_S7M0AR = (uint32_t)dma_buffer;
    DMA1_S7NDTR = AUDIO_DMA_FRAMES * 8;
    DMA1_S7CR   = cr_with_irqs | 1UL;

    /* DAC2 -> DMA1 Stream 4 (master) */
    DMA1_S4CR   = 0; while (DMA1_S4CR & 1UL);
    DMA1_HIFCR  = 0x3FUL;
    DMA1_S4PAR  = SPI2_DR_ADDR;
    DMA1_S4M0AR = (uint32_t)dma_buffer2;
    DMA1_S4NDTR = AUDIO_DMA_FRAMES * 8;
    DMA1_S4CR   = cr_with_irqs | 1UL;

    /* NVIC: IRQ15 (S4) and IRQ47 (S7), priority 4.
    ** Priority 4 keeps audio below UI timers (1-2) and above USB (5). */
    NVIC_IPR[IRQ_DMA1_S4] = (4U << 4);
    NVIC_IPR[IRQ_DMA1_S7] = (4U << 4);
    NVIC_ISER0 = (1UL << IRQ_DMA1_S4);
    NVIC_ISER1 = (1UL << (IRQ_DMA1_S7 - 32));
}

static void i2s_init(void)
{
    RCC_APB1ENR |= (1UL<<14)|(1UL<<15);
    (void)RCC_APB1ENR;

    /* I2SCFGR: I2SMOD=1, I2SCFG=10 (master tx), DATLEN=01 (24-bit), CHLEN=1.
    ** I2SPR: MCKOE=1, I2SDIV=6. CR2: TXDMAEN. I2SE last. */
    SPI3_I2SCFGR = (1UL<<11)|(2UL<<8)|(1UL<<1)|(1UL<<0);
    SPI3_I2SPR   = (1UL<<9)|6UL;
    SPI3_CR2    |= (1UL<<1);
    SPI3_I2SCFGR|= (1UL<<10);

    SPI2_I2SCFGR = (1UL<<11)|(2UL<<8)|(1UL<<1)|(1UL<<0);
    SPI2_I2SPR   = (1UL<<9)|6UL;
    SPI2_CR2    |= (1UL<<1);
    SPI2_I2SCFGR|= (1UL<<10);
}

static void audioCodec_stopHardware(uint8_t resetPeripherals)
{
    NVIC_ICER0 = (1UL << IRQ_DMA1_S4);
    NVIC_ICER1 = (1UL << (IRQ_DMA1_S7 - 32));

    SPI2_CR2 &= ~(1UL << 1);
    SPI3_CR2 &= ~(1UL << 1);
    SPI2_I2SCFGR &= ~(1UL << 10);
    SPI3_I2SCFGR &= ~(1UL << 10);

    DMA1_S4CR &= ~1UL;
    DMA1_S7CR &= ~1UL;
    for (uint32_t t = 0; (DMA1_S4CR & 1UL) && t < 1000000UL; t++) { }
    for (uint32_t t = 0; (DMA1_S7CR & 1UL) && t < 1000000UL; t++) { }

    DMA1_HIFCR = 0x3FUL | (0x3FUL << 22);
    NVIC_ICPR0 = (1UL << IRQ_DMA1_S4);
    NVIC_ICPR1 = (1UL << (IRQ_DMA1_S7 - 32));

    if (resetPeripherals) {
        RCC_APB1RSTR |= (1UL << 14) | (1UL << 15);
        RCC_AHB1RSTR |= (1UL << 21);
        for (volatile uint32_t t = 0; t < 64u; t++) { }
        RCC_APB1RSTR &= ~((1UL << 14) | (1UL << 15));
        RCC_AHB1RSTR &= ~(1UL << 21);
        (void)RCC_APB1RSTR;
        (void)RCC_AHB1RSTR;
    }
}

/* =========================================================================
** SECTION 8: INITIALISATION
** =========================================================================
*/

/* audioCodec_init — initialise the complete audio path.
** Call once from main() before the main loop. Zeroes all buffers, resets
** queue state, then brings up PLLI2S, GPIO, DMA, and I2S in that order. */
void audioCodec_init(void)
{
    memset((void*)dma_buffer,  0, sizeof(dma_buffer));
    memset((void*)dma_buffer2, 0, sizeof(dma_buffer2));
    memset(audioOutBuffer,     0, sizeof(audioOutBuffer));
    memset(audioOutBuffer2,    0, sizeof(audioOutBuffer2));

    audioCodec_resetQueueState();
    audioCodec_underrunCount = 0;
    audioCodec_renderCount   = 0;
    audioCodec_initCycleCounter();

    plli2s_init();
    gpio_init();
    dma_init();
    i2s_init();
    audio_hw_initialized = 1;
    audio_hw_suspended = 0;
}

/*
 * Report whether the codec manager completed a hardware suspend.
 *
 * What/why: expose audio_hw_suspended so foreground clients can suppress DSP
 * and verify ownership without duplicating state. Input: none. Output: 0 before
 * init/while live, 1 after suspend until resume completes. Side effects: none;
 * no IRQ, register, queue, or transport change. Affiliates: suspend/resume,
 * main.c audio_check_and_render(), and Menu's no-playback lifecycle.
 */
uint8_t audioCodec_isSuspended(void)
{
    return audio_hw_suspended;
}

void audioCodec_suspend(void)
{
    uint32_t primask;

    if (!audio_hw_initialized || audio_hw_suspended)
        return;

    primask = irq_save();
    audioCodec_stopHardware(1);
    plli2s_stop();
    audioCodec_accountQueueState();
    audioCodec_resetQueueState();
    audio_hw_suspended = 1;

    irq_restore(primask);
}

void audioCodec_resume(void)
{
    uint32_t primask;

    if (!audio_hw_initialized || !audio_hw_suspended)
        return;

    primask = irq_save();

    memset((void*)dma_buffer,  0, sizeof(dma_buffer));
    memset((void*)dma_buffer2, 0, sizeof(dma_buffer2));
    memset(audioOutBuffer,     0, sizeof(audioOutBuffer));
    memset(audioOutBuffer2,    0, sizeof(audioOutBuffer2));
    audioCodec_resetQueueState();
    audioCodec_initCycleCounter();

    plli2s_init();
    gpio_init();
    dma_init();
    i2s_init();
    audio_hw_suspended = 0;

    irq_restore(primask);
}

/* CodecInit — legacy compatibility wrapper. Resets buffer and queue state
** but does not reconfigure hardware. Retained so ported DSP source files
** that reference CodecInit() link without modification. In normal use,
** call audioCodec_init() instead. */
int CodecInit(void)
{
    memset(audioOutBuffer,  0, sizeof(audioOutBuffer));
    memset(audioOutBuffer2, 0, sizeof(audioOutBuffer2));
    render_slot       = 0;
    audioCodec_resetQueueState();
    audioCodec_underrunCount = 0;
    audioCodec_renderCount   = 0;
    return 0;
}

/* =========================================================================
** SECTION 9: TEST UTILITIES
** =========================================================================
**
** audioCodec_renderSineBlock — generates a 440Hz sine wave into the current
** render slot and commits it. Moved here from sineBufferTest.c.
**
** To use in the main loop (for audio path bringup before the mixer is
** ready), uncomment the test block in main.c and call this function instead
** of mixer_calcNextSampleBlock(). See the TEST FUNCTIONS section in main.c.
*/

#define SINE_HZ        440.0f
#define SINE_AMP       16000
#define SINE_TWO_PI    6.28318530718f

static float sine_phase = 0.0f;
static const float sine_phase_inc = SINE_TWO_PI * SINE_HZ / 44108.0f;

void audioCodec_renderSineBlock(void)
{
    if (audioCodec_queueFreeSlots() == 0) return;

    sample_mx_t *buf  = audioCodec_getRenderBuffer();
    sample_mx_t *buf2 = audioCodec_getRenderBuffer2();

    for (uint32_t i = 0; i < AUDIO_DMA_FRAMES; i++) {
        int16_t s = (int16_t)(sinf(sine_phase) * SINE_AMP);
        sine_phase += sine_phase_inc;
        if (sine_phase >= SINE_TWO_PI) sine_phase -= SINE_TWO_PI;
        sample_mx_t sm = sampleMix_fromInt16(s);
        buf [2*i + 0] = sm;
        buf [2*i + 1] = sm;
        buf2[2*i + 0] = sm;
        buf2[2*i + 1] = sm;
    }
    audioCodec_commitRenderBuffer();
}
