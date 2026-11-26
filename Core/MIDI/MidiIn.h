/*
 * MidiIn.h
 *
 *  Derived from MidiParser.h, original LXR firmware by Julian Schmidt.
 */

/*
 *  Modified on: 21.10.2026
 * ------------------------------------------------------------------------------------------------------------------------
 *  Modifications Copyright 2026 Brendan Clarke
 *  brendanpaulclarke@gmail.com
 *  https://www.brendanclarke.com
 * ------------------------------------------------------------------------------------------------------------------------
 *  The modifications to this file are part of the LXR02 Open-Source software.
 *  The same license and restrictions on use for the LXR software apply.
 * ------------------------------------------------------------------------------------------------------------------------
 */

#ifndef MIDIIN_H_
#define MIDIIN_H_

/*
 * MidiIn - MIDI receive: framing, filters, consumers, routing (S083).
 *
 * What: owns the MIDI settings the receive and transmit sides share
 * (G, RXF/TXF, MRT); frames DIN bytes into messages (running status,
 * SysEx pass-through, SPP and Song Select); takes USB messages from the
 * USB ring; offers each message to its consumers (notes, CCs, PC/CC0,
 * system common) after the RXF gate and the S082 Bank-load drop filter;
 * forwards per MRT every message no consumer acted on.
 * Why: plan 3.2 consume-then-route. Before S083 every message was
 * forwarded before it was parsed, so a consumed note also went out.
 * Context: foreground (midiIn_service() from the main loop).
 * RAM: 3 B configuration + 1 B framer state + 6 B in-progress message.
 * Affiliates: MidiDin (DIN bytes), usb_manager (USB ring), MidiTransport
 * (system common), MidiNotes (B), MidiProgram (D), MidiControl (E),
 * MidiOut (forwarding), menu.c (settings), preset (Bank-load state).
 */

#include <stdint.h>
#include "MidiDefs.h"

/* Menu: G = 0..16 (0 off). Ends every sounding track note first, because
** tracks following G change channel. Caller: menu_parseGlobalParam(). */
void    midiIn_setGlobalChannel(uint8_t channel);
uint8_t midiIn_globalChannel(void);
/* S104 P1, MIDI Omni channel O (0 off, 1..16). Setter: menu_parseGlobalParam()
** (releases the notes held on the old O). midiIn_omniTrack(channel): the
** selected track 0..6 when `channel` is O, else 0xFF; every channel consumer
** asks it first (contract in MidiIn.c). */
void    midiIn_setOmniChannel(uint8_t channel);
uint8_t midiIn_omniChannel(void);
uint8_t midiIn_omniTrack(uint8_t channel);
/* Menu: RXF (isTx 0) or TXF (isTx 1) as the 4-bit N R C P mask. */
void    midiIn_setFilter(uint8_t isTx, uint8_t value);
/* 1 when the RXF (or TXF) mask has `bit` (MIDI_FILTER_*). */
uint8_t midiIn_rxAllows(uint8_t bit);
uint8_t midiIn_txAllows(uint8_t bit);
/* Menu: MRT value 0..7 (midiDefs_routeMask()). */
void    midiIn_setRouting(uint8_t mrt);
uint8_t midiIn_routeMask(void);
/*
 * Main-loop service (replaces main.c midi_service()'s body).
 * What: frames up to MIDI_DIN_DRAIN_PER_PASS DIN bytes, takes up to
 * MIDI_USB_DRAIN_PER_PASS USB messages, dispatches each, then runs
 * midiOut_service() and usb_tick(). Context: foreground only.
 */
void    midiIn_service(void);

#endif /* MIDIIN_H_ */
