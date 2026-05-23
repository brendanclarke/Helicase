/*
 * Core/Hardware/triggerJacks.c
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
 * triggerJacks.c - LXR-02 clock/reset jack backend.
 *
 * Confirmed hardware:
 *   PC13: CLK OUT, active high through DD1.
 *   PD4 : CLK IN, GPIO input pull-up, EXTI4 rising edge.
 *   PD5 : RST IN, GPIO input pull-up, EXTI5 rising edge.
 *
 * The EXTI handlers only clear pending flags, timestamp with TIM2, and enqueue
 * tiny events. Sequencer-facing work is drained from triggerJacks_tick() by the
 * TIM3 sequencer timing owner, so jack sync follows the same "source ISR
 * captures, sequencer owner decides" rule used for MIDI realtime.
 */

#include "triggerJacks.h"
#include "sequencer.h"
#include "timebase.h"
#include "mixer.h"
#include <stdint.h>

/* -----------------------------------------------------------------------
** GPIOC / PC13 CLK OUT
** ----------------------------------------------------------------------- */
#define GPIOC_BASE    0x40020800UL
#define GPIOC_MODER   (*((volatile uint32_t *)(GPIOC_BASE + 0x00UL)))
#define GPIOC_OTYPER  (*((volatile uint32_t *)(GPIOC_BASE + 0x04UL)))
#define GPIOC_OSPEEDR (*((volatile uint32_t *)(GPIOC_BASE + 0x08UL)))
#define GPIOC_PUPDR   (*((volatile uint32_t *)(GPIOC_BASE + 0x0CUL)))
#define GPIOC_BSRR    (*((volatile uint32_t *)(GPIOC_BASE + 0x18UL)))
#define PC13_HIGH()   (GPIOC_BSRR = (1UL << 13))
#define PC13_LOW()    (GPIOC_BSRR = (1UL << 29))

/* -----------------------------------------------------------------------
** GPIOD / PD4 CLK IN, PD5 RST IN, PD6/PD7 OUT1 jack detect
** ----------------------------------------------------------------------- */
#define GPIOD_BASE    0x40020C00UL
#define GPIOD_MODER   (*((volatile uint32_t *)(GPIOD_BASE + 0x00UL)))
#define GPIOD_PUPDR   (*((volatile uint32_t *)(GPIOD_BASE + 0x0CUL)))
#define GPIOD_IDR     (*((volatile uint32_t *)(GPIOD_BASE + 0x10UL)))
#define PD4_CLK_MASK  (1UL << 4)
#define PD5_RST_MASK  (1UL << 5)
#define PD6_OUT1L_MASK (1UL << 6)
#define PD7_OUT1R_MASK (1UL << 7)

/* -----------------------------------------------------------------------
** RCC / SYSCFG / EXTI
** ----------------------------------------------------------------------- */
#define RCC_AHB1ENR    (*((volatile uint32_t *)0x40023830UL))
#define RCC_APB2ENR    (*((volatile uint32_t *)0x40023844UL))
#define SYSCFG_EXTICR2 (*((volatile uint32_t *)0x4001380CUL))

#define EXTI_IMR       (*((volatile uint32_t *)0x40013C00UL))
#define EXTI_RTSR      (*((volatile uint32_t *)0x40013C08UL))
#define EXTI_FTSR      (*((volatile uint32_t *)0x40013C0CUL))
#define EXTI_PR        (*((volatile uint32_t *)0x40013C14UL))

/* -----------------------------------------------------------------------
** NVIC
** ----------------------------------------------------------------------- */
#define NVIC_ISER0     (*((volatile uint32_t *)0xE000E100UL))
#define NVIC_IPR(n)    (*((volatile uint8_t  *)(0xE000E400UL + (n))))
#define IRQ_EXTI4      10u
#define IRQ_EXTI9_5    23u

/* -----------------------------------------------------------------------
** Trigger event ring
** ----------------------------------------------------------------------- */
#define TRIGGER_EVENT_RING_SIZE       16u
#define TRIGGER_EVENT_RING_MASK       (TRIGGER_EVENT_RING_SIZE - 1u)
#define TRIGGER_EVENT_CLOCK           1u
#define TRIGGER_EVENT_RESET           2u
#define TRIGGER_CLOCK_RECENT_US       500000UL
#define TRIGGER_CLOCK_MIN_DELTA_US    200UL

typedef struct TriggerEvent {
	uint8_t type;
	uint8_t value;
	uint32_t timestampUs;
} TriggerEvent;

uint8_t trigger_dividerClockOut1 = PRE_4_PPQ;
uint8_t trigger_dividerClockOut2 = PRE_4_PPQ;
uint8_t trigger_prescalerClockInput = PRE_4_PPQ;

static volatile TriggerEvent trigger_eventRing[TRIGGER_EVENT_RING_SIZE];
static volatile uint8_t trigger_eventHead = 0;
static volatile uint8_t trigger_eventTail = 0;
static volatile uint32_t trigger_eventDropCount = 0;
static volatile uint32_t trigger_lastJackInputUs = 0;
static volatile uint8_t trigger_haveJackInput = 0;

static uint8_t trigger_nextPulseOut1 = 0xff;
static uint8_t trigger_gateMode = 0;
static uint8_t clk_out_state = 0;
static uint8_t trigger_havePulseTempo = 0;
static uint32_t trigger_lastPulseTempoUs = 0;

static uint32_t trigger_irqSave(void)
{
	uint32_t primask;
	__asm volatile ("mrs %0, primask\ncpsid i" : "=r" (primask) :: "memory");
	return primask;
}

static void trigger_irqRestore(uint32_t primask)
{
	__asm volatile ("msr primask, %0" :: "r" (primask) : "memory");
}

static void trigger_pushEvent(uint8_t type, uint8_t value, uint32_t timestampUs)
{
	uint8_t next;
	uint32_t primask = trigger_irqSave();

	next = (uint8_t)((trigger_eventHead + 1u) & TRIGGER_EVENT_RING_MASK);
	if (next == trigger_eventTail) {
		trigger_eventTail = (uint8_t)((trigger_eventTail + 1u) &
				TRIGGER_EVENT_RING_MASK);
		trigger_eventDropCount++;
	}

	trigger_eventRing[trigger_eventHead].type = type;
	trigger_eventRing[trigger_eventHead].value = value;
	trigger_eventRing[trigger_eventHead].timestampUs = timestampUs;
	trigger_eventHead = next;
	trigger_irqRestore(primask);
}

static uint8_t trigger_popEvent(TriggerEvent *event)
{
	uint32_t primask = trigger_irqSave();

	if (trigger_eventTail == trigger_eventHead) {
		trigger_irqRestore(primask);
		return 0;
	}

	*event = trigger_eventRing[trigger_eventTail];
	trigger_eventTail = (uint8_t)((trigger_eventTail + 1u) &
			TRIGGER_EVENT_RING_MASK);
	trigger_irqRestore(primask);
	return 1;
}

static uint8_t trigger_ppqMenuToPrescaler(uint8_t value)
{
	switch (value) {
	case 0: return PRE_1_PPQ;
	case 1: return PRE_4_PPQ;
	case 2: return PRE_8_PPQ;
	case 3: return PRE_16_PPQ;
	case 4: return PRE_32_PPQ;
	default: return PRE_4_PPQ;
	}
}

static uint8_t trigger_pulseSyncAllowed(void)
{
	const uint8_t source = seq_getExtSyncSource();
	return (uint8_t)((source == SEQ_EXT_SYNC_PULSE) ||
			(source == SEQ_EXT_SYNC_AUTO));
}

static void trigger_setClkOut(uint8_t isOn)
{
	clk_out_state = (uint8_t)(isOn != 0u);
	if (clk_out_state)
		PC13_HIGH();
	else
		PC13_LOW();
}

static void trigger_applyPulseTempo(uint32_t timestampUs)
{
	uint32_t deltaUs;
	float pulsesPerQuarter;
	float bpm;

	if (!trigger_havePulseTempo) {
		trigger_havePulseTempo = 1;
		trigger_lastPulseTempoUs = timestampUs;
		return;
	}

	deltaUs = timebase_tim2Delta(timestampUs, trigger_lastPulseTempoUs);
	trigger_lastPulseTempoUs = timestampUs;

	if (deltaUs < TRIGGER_CLOCK_MIN_DELTA_US)
		return;

	/* Input PPQ is stored as native 32-PPQ step spacing. For example,
	** PRE_4_PPQ == 8, so the jack produces four pulses per quarter. */
	pulsesPerQuarter = 32.0f / (float)trigger_prescalerClockInput;
	bpm = 60000000.0f / ((float)deltaUs * pulsesPerQuarter);
	if (bpm < 1.0f)
		bpm = 1.0f;
	if (bpm > 65535.0f)
		bpm = 65535.0f;
	seq_setBpm((uint16_t)(bpm + 0.5f));
}

static void trigger_handleClockEvent(const TriggerEvent *event)
{
	if (!trigger_pulseSyncAllowed())
		return;

	seq_noteExtSyncActivity(SEQ_EXT_SYNC_PULSE, event->timestampUs);
	trigger_applyPulseTempo(event->timestampUs);

	if (seq_isRunning()) {
		/* Pulse sync is native 32 PPQ internally. Each external pulse names a
		** master step; the TIM3 sequencer owner derives intermediate substeps
		** from BPM until the next master pulse arrives. */
		seq_triggerNextMasterStep(trigger_prescalerClockInput);
	}
}

static void trigger_handleResetEvent(const TriggerEvent *event)
{
	if (!trigger_pulseSyncAllowed())
		return;

	seq_noteExtSyncActivity(SEQ_EXT_SYNC_PULSE, event->timestampUs);
	trigger_havePulseTempo = 0;
	seq_resetToPatternStart();
}

/* -----------------------------------------------------------------------
** EXTI4_IRQHandler - CLK IN on PD4, rising edge
** ----------------------------------------------------------------------- */
void EXTI4_IRQHandler(void)
{
	if (EXTI_PR & PD4_CLK_MASK) {
		uint32_t timestampUs = timebase_tim2Now();
		EXTI_PR = PD4_CLK_MASK;
		trigger_lastJackInputUs = timestampUs;
		trigger_haveJackInput = 1;
		trigger_pushEvent(TRIGGER_EVENT_CLOCK, 0, timestampUs);
	}
}

/* -----------------------------------------------------------------------
** EXTI9_5_IRQHandler - RST IN on PD5, rising edge
**
** Note: PD6/PD7 OUT1 detect is retained state, so it is polled by the
** 500Hz front-panel service rather than handled as EXTI edges.
** ----------------------------------------------------------------------- */
void EXTI9_5_IRQHandler(void)
{
	uint32_t pending = EXTI_PR & PD5_RST_MASK;

	if (pending & PD5_RST_MASK) {
		uint32_t timestampUs = timebase_tim2Now();
		EXTI_PR = PD5_RST_MASK;
		trigger_lastJackInputUs = timestampUs;
		trigger_haveJackInput = 1;
		trigger_pushEvent(TRIGGER_EVENT_RESET, 1u, timestampUs);
	}

}

void triggerJacks_init(void)
{
	/* Enable GPIOC/GPIOD and SYSCFG clocks. */
	RCC_AHB1ENR |= (1UL << 2) | (1UL << 3);
	RCC_APB2ENR |= (1UL << 14);
	(void)RCC_AHB1ENR;
	(void)RCC_APB2ENR;

	/* PC13: CLK OUT output, push-pull, initially low. */
	GPIOC_MODER  &= ~(3UL << 26);
	GPIOC_MODER  |=  (1UL << 26);
	GPIOC_OTYPER &= ~(1UL << 13);
	GPIOC_OSPEEDR &= ~(3UL << 26);
	GPIOC_OSPEEDR |=  (2UL << 26);
	GPIOC_PUPDR  &= ~(3UL << 26);
	trigger_setClkOut(0);

	/* PD4/PD5 are normally-low modular inputs. PD6/PD7 jack detect is a
	** switched contact: no plug grounds the pin, plug inserted opens it.
	** All four PD inputs need weak pull-ups so open states are retained high. */
	GPIOD_MODER &= ~((3UL << 8) | (3UL << 10) | (3UL << 12) | (3UL << 14));
	GPIOD_PUPDR &= ~((3UL << 8) | (3UL << 10) | (3UL << 12) | (3UL << 14));
	GPIOD_PUPDR |=  ((1UL << 8) | (1UL << 10) | (1UL << 12) | (1UL << 14));

	/* Route EXTI4/5 to port D. Keep EXTI6/7 disconnected because OUT1 jack
	** detect is sampled as retained state in the 500Hz front-panel service. */
	SYSCFG_EXTICR2 &= ~((0xFUL << 0) | (0xFUL << 4) | (0xFUL << 8) | (0xFUL << 12));
	SYSCFG_EXTICR2 |=  ((0x3UL << 0) | (0x3UL << 4));

	/* CLK IN and RST IN both follow modular-clock convention: normally low,
	** trigger on rising edge. PD6/PD7 EXTI stays disabled; jack-detect state is
	** polled and retained with PB4/PB6. */
	EXTI_IMR  &= ~(PD4_CLK_MASK | PD5_RST_MASK | PD6_OUT1L_MASK | PD7_OUT1R_MASK);
	EXTI_RTSR |=  (PD4_CLK_MASK | PD5_RST_MASK);
	EXTI_FTSR &= ~(PD4_CLK_MASK | PD5_RST_MASK);
	EXTI_RTSR &= ~(PD6_OUT1L_MASK | PD7_OUT1R_MASK);
	EXTI_FTSR &= ~(PD6_OUT1L_MASK | PD7_OUT1R_MASK);
	EXTI_PR    =  (PD4_CLK_MASK | PD5_RST_MASK | PD6_OUT1L_MASK | PD7_OUT1R_MASK);
	EXTI_IMR  |=  (PD4_CLK_MASK | PD5_RST_MASK);

	/* Seed OUT1 detect state once at init so mixer reads are valid immediately. */
	mixer_setOutJackDetectPD((uint8_t)((GPIOD_IDR & PD6_OUT1L_MASK) != 0u),
			(uint8_t)((GPIOD_IDR & PD7_OUT1R_MASK) != 0u));

	/* These handlers are tiny timestamp capture paths. Priority 3 keeps pulse
	** capture ahead of MIDI/USB foreground plumbing while leaving SysTick,
	** TIM6 button/service, and TIM7 LCD work above it. */
	NVIC_IPR(IRQ_EXTI4) = 3u << 4;
	NVIC_IPR(IRQ_EXTI9_5) = 3u << 4;
	NVIC_ISER0 |= (1UL << IRQ_EXTI4) | (1UL << IRQ_EXTI9_5);

}

void triggerJacks_toggleClkOut(void)
{
	trigger_setClkOut((uint8_t)!clk_out_state);
}

void triggerJacks_isrTick(void)
{
	/* EXTI owns CLK/RST edge capture now. TIM6 still calls this hook so the
	** call graph remains stable, but no GPIO polling is needed. */
}

uint8_t triggerJacks_tick(void)
{
	TriggerEvent event;
	uint8_t processed = 0;
	uint8_t budget = TRIGGER_EVENT_RING_SIZE;

	while (budget-- && trigger_popEvent(&event)) {
		processed = 1;
		if (event.type == TRIGGER_EVENT_CLOCK)
			trigger_handleClockEvent(&event);
		else if (event.type == TRIGGER_EVENT_RESET)
			trigger_handleResetEvent(&event);
	}

	return processed;
}

uint8_t triggerJacks_displayActive(void)
{
	return 0;
}

uint8_t triggerJacks_clockInputRecently(uint32_t nowUs)
{
	if (!trigger_haveJackInput)
		return 0;
	return (uint8_t)(timebase_tim2Delta(nowUs, trigger_lastJackInputUs) <=
			TRIGGER_CLOCK_RECENT_US);
}

void triggerJacks_setClockInputPpq(uint8_t menuValue)
{
	trigger_prescalerClockInput = trigger_ppqMenuToPrescaler(menuValue);
	trigger_havePulseTempo = 0;
}

void triggerJacks_setClockOut1Ppq(uint8_t menuValue)
{
	trigger_dividerClockOut1 = trigger_ppqMenuToPrescaler(menuValue);
	trigger_nextPulseOut1 = 0;
}

void triggerJacks_setClockOut2Ppq(uint8_t menuValue)
{
	(void)menuValue;
	/* LXR-02 has one physical clock output. OUT2 is kept as a preset/global
	** compatibility no-op until the UI is changed to expose different meaning. */
}

void trigger_clockTick(uint8_t pos)
{
	if (pos == 1u)
		trigger_nextPulseOut1 = 0;

	if (pos >= trigger_nextPulseOut1) {
		trigger_setClkOut(1);
		trigger_nextPulseOut1 = (uint8_t)(trigger_nextPulseOut1 +
				trigger_dividerClockOut1);
	} else {
		trigger_setClkOut(0);
	}
}

void trigger_triggerVoice(uint8_t voiceNr, uint8_t onOff)
{
	(void)voiceNr;
	(void)onOff;
	/* No individual analog voice trigger outputs exist on LXR-02. */
}

uint8_t trigger_isGateModeOn(void)
{
	return trigger_gateMode;
}

void trigger_setGatemode(uint8_t onOff)
{
	trigger_gateMode = onOff;
}

void trigger_allOff(void)
{
	trigger_setClkOut(0);
}

void trigger_tickPhaseCounter(void)
{
	/* The original F4 trigger backend advanced non-master steps from the audio
	** block. This port derives those steps from the BPM estimated from pulse
	** intervals in the TIM3 sequencer timing owner. */
}

void trigger_reset(uint8_t value)
{
	if (value) {
		trigger_nextPulseOut1 = 0;
		trigger_havePulseTempo = 0;
	}
}
