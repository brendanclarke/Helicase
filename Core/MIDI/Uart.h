/*
 * Uart.h — ported from original LXR firmware by Julian Schmidt.
 *
 * LXR-02: MIDI DIN uses USART3 on PB10 (TX) / PB11 (RX), AF7.
 * RX/TX are interrupt-driven through small FIFOs. The main loop drains
 * RX with uart_processMidi(); TXE drains queued output bytes.
 *
 * The frontpanel-UART functions on the original LXR were the
 * inter-processor link to the AVR. On LXR-02 single-chip those calls
 * are no-ops (defined in frontPanelParser.c shim) — they exist only so
 * sequencer.c and presetManager.c land verbatim.
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


#ifndef UART_H_
#define UART_H_

#include <stdint.h>
#include "MidiMessages.h"

#define ACK   1
#define NACK -1

/* MIDI DIN — real implementation. */
void    initMidiUart(void);
void    uart_sendMidiByte(uint8_t data);
void    uart_sendMidi(MidiMsg msg);
void    uart_processMidi(void);
uint32_t uart_getMidiTxDropCount(void);
uint32_t uart_getMidiRealtimeTxDropCount(void);

/* Front-panel UART — STUBBED on F765 (no AVR to talk to).
** Defined in Core/MIDI/frontPanelParser.c. */
void    initFrontpanelUart(void);
void    uart_processFront(void);
void    uart_sendFrontpanelByte(uint8_t data);
void    uart_sendFrontpanelSysExByte(uint8_t data);
void    uart_clearFrontFifo(void);
void    uart_checkAndParse(void);   /* AVR-side name; same no-op stub */

#endif /* UART_H_ */
