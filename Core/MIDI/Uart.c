/*
 * Uart.c — MIDI DIN output via USART3 on PB10 (TX) / PB11 (RX)
 *
 * Ported from the original LXR firmware by Julian Schmidt.
 * Adapted for the STM32F765 bare-metal register layout.
 *
 * Hardware (confirmed by board trace):
 *   PB10 → USART3_TX (AF7) → MIDI OUT connector
 *   PB11 → USART3_RX (AF7) ← 74AHCT125 level shifter ← optocoupler ← MIDI IN
 *
 * RX/TX are interrupt-driven through small FIFOs. The main loop drains
 * RX with uart_processMidi(); TXE drains queued output bytes. Realtime
 * MIDI status bytes have their own priority TX FIFO so clock/start/stop
 * can be interleaved ahead of ordinary note/CC traffic.
 *
 * MIDI spec: 31250 baud, 8N1, no flow control.
 *
 * STM32F765 USART register layout (RM0410 section 38.8):
 *   CR1 @ +0x00   BRR @ +0x0C   ISR @ +0x1C   TDR @ +0x28
 *   (differs from F4: SR @ +0x00, DR @ +0x04)
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


#include "Uart.h"
#include "FIFO.h"
#include "MidiParser.h"
#include "MidiRealtime.h"
#include "timebase.h"

/* -----------------------------------------------------------------------
** USART3 registers  (base 0x40004800, APB1)
** ----------------------------------------------------------------------- */
#define USART3_BASE   0x40004800UL
#define USART3_CR1    (*((volatile uint32_t *)(USART3_BASE + 0x00UL)))
#define USART3_CR2    (*((volatile uint32_t *)(USART3_BASE + 0x04UL)))
#define USART3_CR3    (*((volatile uint32_t *)(USART3_BASE + 0x08UL)))
#define USART3_BRR    (*((volatile uint32_t *)(USART3_BASE + 0x0CUL)))
#define USART3_ISR    (*((volatile uint32_t *)(USART3_BASE + 0x1CUL)))
#define USART3_ICR    (*((volatile uint32_t *)(USART3_BASE + 0x20UL)))
#define USART3_RDR    (*((volatile uint32_t *)(USART3_BASE + 0x24UL)))
#define USART3_TDR    (*((volatile uint32_t *)(USART3_BASE + 0x28UL)))

/* CR1 bits */
#define CR1_UE   (1UL << 0)
#define CR1_RE   (1UL << 2)
#define CR1_TE   (1UL << 3)
#define CR1_RXNEIE (1UL << 5)
#define CR1_TXEIE  (1UL << 7)

/* ISR bits */
#define ISR_FE   (1UL << 1)
#define ISR_NE   (1UL << 2)
#define ISR_ORE  (1UL << 3)
#define ISR_RXNE (1UL << 5)
#define ISR_TXE  (1UL << 7)   /* transmit data register empty */

/* -----------------------------------------------------------------------
** BRR for 31250 baud at 54 MHz PCLK1 (OVER16)
**   USARTDIV = 54,000,000 / 31,250 = 1728 (exact)
**   BRR = 1728 = 0x06C0
** ----------------------------------------------------------------------- */
#define USART3_BRR_31250  0x06C0UL

/* -----------------------------------------------------------------------
** GPIOB registers (base 0x40020400, AHB1)
** PB10 = TX:  MODER[21:20], OTYPER[10], OSPEEDR[21:20], PUPDR[21:20], AFRH[11:8]
** PB11 = RX:  MODER[23:22],              OSPEEDR[23:22], PUPDR[23:22], AFRH[15:12]
** ----------------------------------------------------------------------- */
#define GPIOB_BASE    0x40020400UL
#define GPIOB_MODER   (*((volatile uint32_t *)(GPIOB_BASE + 0x00UL)))
#define GPIOB_OTYPER  (*((volatile uint32_t *)(GPIOB_BASE + 0x04UL)))
#define GPIOB_OSPEEDR (*((volatile uint32_t *)(GPIOB_BASE + 0x08UL)))
#define GPIOB_PUPDR   (*((volatile uint32_t *)(GPIOB_BASE + 0x0CUL)))
#define GPIOB_AFRH    (*((volatile uint32_t *)(GPIOB_BASE + 0x24UL)))

/* -----------------------------------------------------------------------
** RCC
** ----------------------------------------------------------------------- */
#define RCC_AHB1ENR  (*((volatile uint32_t *)0x40023830UL))
#define RCC_APB1ENR  (*((volatile uint32_t *)0x40023840UL))

/* -----------------------------------------------------------------------
** NVIC
** ----------------------------------------------------------------------- */
#define NVIC_ISER1  (*((volatile uint32_t *)0xE000E104UL))
#define NVIC_IPR(n) (*((volatile uint8_t  *)(0xE000E400UL + (n))))
#define IRQ_USART3  39

/* -----------------------------------------------------------------------
** FIFOs — retained from original structure for future ISR-driven port
** ----------------------------------------------------------------------- */
static Fifo fifo_midiTx;
static Fifo fifo_midiRealtimeTx;
static Fifo fifo_midiRx;

static volatile uint32_t uart_midiTxDropCount = 0;
static volatile uint32_t uart_midiRealtimeTxDropCount = 0;

static uint32_t uart_irqSave(void)
{
	uint32_t primask;
	__asm volatile ("mrs %0, primask\ncpsid i" : "=r" (primask) :: "memory");
	return primask;
}

static void uart_irqRestore(uint32_t primask)
{
	__asm volatile ("msr primask, %0" :: "r" (primask) : "memory");
}

static uint8_t uart_isRealtimeByte(uint8_t data)
{
	/* MIDI realtime is 0xF8..0xFF and may be inserted between channel
	** message bytes. Prioritising those single-byte statuses keeps clock
	** output from waiting behind bursty note or CC streams. */
	return data >= MIDI_CLOCK;
}

/* -----------------------------------------------------------------------
** USART3_IRQHandler
** ----------------------------------------------------------------------- */
void USART3_IRQHandler(void)
{
	uint32_t isr = USART3_ISR;
	uint8_t data;

	if (isr & (ISR_FE | ISR_NE | ISR_ORE)) {
		USART3_ICR = (ISR_FE | ISR_NE | ISR_ORE);
		isr = USART3_ISR;
	}

	if (isr & ISR_RXNE) {
		uint32_t timestampUs = timebase_tim2Now();
		data = (uint8_t)USART3_RDR;
		if (midiRealtime_isStatus(data)) {
			/* MIDI realtime bytes can arrive between data bytes of a running
			** channel message. Timestamp and enqueue them here, but do not feed
			** them into the byte parser where they would perturb running status. */
			midiRealtime_push(data, MIDI_REALTIME_SOURCE_DIN, timestampUs);
		} else {
			(void)fifo_bufferIn(&fifo_midiRx, data);
		}
	}

	if (isr & ISR_TXE) {
		if (fifo_bufferOut(&fifo_midiRealtimeTx, &data)) {
			USART3_TDR = data;
		} else if (fifo_bufferOut(&fifo_midiTx, &data)) {
			USART3_TDR = data;
		} else {
			USART3_CR1 &= ~CR1_TXEIE;
		}
	}
}

/* -----------------------------------------------------------------------
** initMidiUart
**
** USART3 at 31250 baud, PB10 TX / PB11 RX, interrupt-driven RX/TX.
** ----------------------------------------------------------------------- */
void initMidiUart(void)
{
	fifo_init(&fifo_midiTx);
	fifo_init(&fifo_midiRealtimeTx);
	fifo_init(&fifo_midiRx);

	/* Clock enables: GPIOB (AHB1 bit 1), USART3 (APB1 bit 18) */
	RCC_AHB1ENR |= (1UL << 1);
	(void)RCC_AHB1ENR;
	RCC_APB1ENR |= (1UL << 18);
	(void)RCC_APB1ENR;

	/* PB10 — USART3_TX, AF7; PB11 — USART3_RX, AF7
	**   MODER          = 10 (AF mode)
	**   OTYPER[10]     = 1  (open-drain — MIDI TX drives a current loop)
	**   OSPEEDR        = 10 (50 MHz)
	**   PUPDR TX       = 01 (pull-up)
	**   PUPDR RX       = 00 (externally driven by the MIDI input circuit)
	**   AFRH           = 7  (AF7 = USART3)
	*/
	GPIOB_MODER   &= ~((3UL << 20) | (3UL << 22));
	GPIOB_MODER   |=  ((2UL << 20) | (2UL << 22));
	GPIOB_OTYPER  |=  (1UL << 10);
	GPIOB_OSPEEDR &= ~((3UL << 20) | (3UL << 22));
	GPIOB_OSPEEDR |=  ((2UL << 20) | (2UL << 22));
	GPIOB_PUPDR   &= ~((3UL << 20) | (3UL << 22));
	GPIOB_PUPDR   |=  (1UL << 20);
	GPIOB_AFRH    &= ~((0xFUL << 8) | (0xFUL << 12));
	GPIOB_AFRH    |=  ((7UL << 8) | (7UL << 12));

	/* USART3: 8N1, 31250 baud, RX/TX, RX interrupt. TXE is enabled
	** only when bytes are queued. */
	USART3_CR1 = 0;
	USART3_CR2 = 0;
	USART3_CR3 = 0;
	USART3_BRR = USART3_BRR_31250;
	USART3_ICR = (ISR_FE | ISR_NE | ISR_ORE);

	NVIC_IPR(IRQ_USART3) = 5u << 4;
	NVIC_ISER1 |= (1UL << (IRQ_USART3 - 32));

	USART3_CR1 = CR1_RXNEIE | CR1_RE | CR1_TE | CR1_UE;
}

/* -----------------------------------------------------------------------
** uart_sendMidiByte — queued TX
** ----------------------------------------------------------------------- */
void uart_sendMidiByte(uint8_t data)
{
	uint32_t primask = uart_irqSave();

	if (uart_isRealtimeByte(data)) {
		if (fifo_bufferIn(&fifo_midiRealtimeTx, data))
			USART3_CR1 |= CR1_TXEIE;
		else
			uart_midiRealtimeTxDropCount++;
		uart_irqRestore(primask);
		return;
	}

	if (fifo_bufferIn(&fifo_midiTx, data))
		USART3_CR1 |= CR1_TXEIE;
	else
		uart_midiTxDropCount++;
	uart_irqRestore(primask);
}

/* -----------------------------------------------------------------------
** uart_sendMidi — identical structure to original
** ----------------------------------------------------------------------- */
void uart_sendMidi(MidiMsg msg)
{
	uint32_t primask = uart_irqSave();

	uart_sendMidiByte(msg.status);
	if (msg.bits.length) {
		uart_sendMidiByte(msg.data1);
		if (msg.bits.length > 1)
			uart_sendMidiByte(msg.data2);
	}
	uart_irqRestore(primask);
}

/* -----------------------------------------------------------------------
** uart_processMidi — drain pending DIN input into the MIDI parser.
** ----------------------------------------------------------------------- */
void uart_processMidi(void)
{
	uint8_t budget = 16;
	uint8_t data;
	while (budget-- && fifo_bufferOut(&fifo_midiRx, &data))
		midiParser_parseUartData(data);
}

uint32_t uart_getMidiTxDropCount(void)
{
	return uart_midiTxDropCount;
}

uint32_t uart_getMidiRealtimeTxDropCount(void)
{
	return uart_midiRealtimeTxDropCount;
}
