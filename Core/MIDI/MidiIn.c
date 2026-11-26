/*
 * MidiIn.c - MIDI receive: framing, filters, consumers, routing (S083).
 *
 * Derived from MidiParser.c (original LXR firmware by Julian Schmidt); the
 * receive half only. The legacy CC/NRPN maps are retired with S083.
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

#include "MidiIn.h"
#include "MidiDin.h"
#include "MidiOut.h"
#include "MidiTransport.h"
#include "MidiNotes.h"
#include "MidiProgram.h"
#include "MidiControl.h"
#include "MidiLearn.h"
#include "usb_manager.h"
#include "presetManager.h"
#include "menu.h"
#include "config.h"

/* S083 Q13: the USB ring and the arrival stamps are sized from this. */
_Static_assert(sizeof(MidiMsg) == 6u, "MidiMsg must stay six bytes");
_Static_assert(sizeof(struct MidiBits) == 1u, "MidiBits must stay one byte");

/*
 * Shared MIDI settings (S083; S104 P1 adds `omni`).
 *
 * What: global channel (1..16, 0 = off), the packed filter byte (RX low
 * nibble, TX high nibble, MIDI_FILTER_* bits), the MRT route mask and
 * `omni`: the Omni channel O (1..16, 0 = off; S104 P1). A message on O acts
 * on the selected track (midiIn_omniTrack()).
 * Why: one owner for settings both directions read. Replaces
 * midi_MidiChannels[8] (8 B), midiParser_txRxFilter and midiParser_routing.
 * Writers: midiIn_set*() from menu_parseGlobalParam() (settings load and
 * menu edits). Readers: MidiIn, MidiOut, MidiTransport, MidiNoteMap loader
 * (B), MidiControl (E), MidiLearn (F), MidiProgram (D). Single-byte reads
 * are atomic, so TIM3 readers need no lock.
 */
static struct {
	uint8_t global;
	uint8_t filter;
	uint8_t route;
	uint8_t omni;		/* S104 P1: Omni channel O, 0 = off */
} midiIn_config = { 1u, 0xFFu, 0u, 0u };

/* DIN framer state (from MidiParser.c). */
enum {
	MIDI_IN_STATE_STATUS = 0,	/* waiting for status or running data */
	MIDI_IN_STATE_DATA1,
	MIDI_IN_STATE_DATA2,
	MIDI_IN_STATE_SYSEX,
	MIDI_IN_STATE_IGNORE		/* unknown status: skip data bytes */
};
static uint8_t midiIn_state = MIDI_IN_STATE_IGNORE;
static MidiMsg midiIn_dinMsg;	/* in-progress DIN message (6 B, R-D) */

void midiIn_setGlobalChannel(uint8_t channel)
{
	if (channel > 16u)
		channel = 16u;
	if (channel != midiIn_config.global)
		midiOut_trackNoteOff(0xFFu);
	midiIn_config.global = channel;
}

uint8_t midiIn_globalChannel(void)
{
	return midiIn_config.global;
}

/*
 * MIDI Omni channel O (S104 P1).
 *
 * What: midiIn_setOmniChannel() stores O (1..16; 0 or > 16 = off) after
 * releasing every note still held on the old O (midiNotes_omniReleaseAll(),
 * which ends paraphonic and gated voices with their own note-offs).
 * midiIn_omniTrack() is the one resolver every consumer asks first: for a
 * message on O it returns the selected track (menu_getActiveVoice(), 0..6),
 * else 0xFF. Why: a channel on O addresses "the selected track as if alone
 * on its own channel", whatever that track's `ch` (glb, off or a number) and
 * whatever G is: O wins when they are equal. The selected track is read per
 * message (no cache), because the user changes it at any time; held notes
 * keep their track through MidiNotes' latch instead. Inputs: channel 1..16.
 * Outputs: track or 0xFF. Context: foreground (midiIn_service()).
 * Accessors: menu_parseGlobalParam() (setter); midiNotes_handle(),
 * midiControl_voiceSlotsOnChannel(), midiControl_targetOnChannel(),
 * midiControl_handleCc(), midiProgram_programChange(),
 * midiProgram_bankChange(). Affiliates: MidiNotes' Omni latch.
 */
void midiIn_setOmniChannel(uint8_t channel)
{
	if (channel > 16u)
		channel = 0u;
	if (channel != midiIn_config.omni)
		midiNotes_omniReleaseAll();
	midiIn_config.omni = channel;
}

uint8_t midiIn_omniChannel(void)
{
	return midiIn_config.omni;
}

uint8_t midiIn_omniTrack(uint8_t channel)
{
	const uint8_t o = midiIn_config.omni;
	uint8_t track;

	if (o == 0u || channel != o)
		return 0xFFu;
	track = menu_getActiveVoice();
	return (track < 7u) ? track : 0xFFu;
}

void midiIn_setFilter(uint8_t isTx, uint8_t value)
{
	if (isTx)
		midiIn_config.filter = (uint8_t)(((value & 0x0Fu) << 4) |
		                                 (midiIn_config.filter & 0x0Fu));
	else
		midiIn_config.filter = (uint8_t)((value & 0x0Fu) |
		                                 (midiIn_config.filter & 0xF0u));
}

uint8_t midiIn_rxAllows(uint8_t bit)
{
	return (uint8_t)((midiIn_config.filter & bit) != 0u);
}

uint8_t midiIn_txAllows(uint8_t bit)
{
	return (uint8_t)((midiIn_config.filter & (uint8_t)(bit << 4)) != 0u);
}

void midiIn_setRouting(uint8_t mrt)
{
	midiIn_config.route = midiDefs_routeMask(mrt);
}

uint8_t midiIn_routeMask(void)
{
	return midiIn_config.route;
}

/*
 * MIDI that may act while a Bank load is in flight (S082 P1 D13, D33).
 *
 * What: accepts system realtime (0xF8-0xFF), note on/off, control change
 * except a Bank Change (CC0 / CC32), and channel pressure (S096 P3: it acts
 * like a CC, a performance control). Everything else is rejected: program
 * change, pitch bend, polyphonic pressure, MTC quarter frame, song
 * position/select and the other system common messages.
 * Why: D33 drops non-realtime/note/CC input during a load, and D13 drops a
 * Bank Change and program change, which would otherwise start a second load
 * or a Scene change into Scenes that are being written. Clock and transport
 * keep playback in sync; notes and CCs keep the performance playing.
 * Inputs: msg (status, data1). Outputs: 1 = act on it, 0 = drop.
 * Accessors: midiIn_dispatch().
 * Affiliates: preset_backgroundBankLoadRunning(), MidiTransport (the realtime
 * queue path is not filtered).
 */
static uint8_t midiIn_acceptedDuringBankLoad(const MidiMsg *msg)
{
	uint8_t type = (uint8_t)(msg->status & 0xF0u);

	if (msg->status >= 0xF8u)
		return 1u;
	if (type == NOTE_ON || type == NOTE_OFF)
		return 1u;
	if (type == CHANNEL_PRESSURE)
		return 1u;   /* S096 P3 C4 */
	if (type == MIDI_CC)
		return (uint8_t)(msg->data1 != CC_BANK_CHANGE &&
		                 msg->data1 != CC_BANK_SELECT_LSB);
	return 0u;
}

/*
 * Offer one received message to its consumers, then route it (plan 3.2).
 *
 * What, in order:
 *  1. SysEx bytes/packets are pass-through only: forward and return.
 *  2. While a background Bank load runs, a message the S082 filter rejects
 *     is not consumed (S082 outcome note 13.2 item 4) and is forwarded.
 *  3. System common (MTC quarter frame, SPP, Song Select) goes to
 *     MidiTransport. These are transport messages: forwarded whether or
 *     not they acted (plan M5).
 *  4. Channel messages go to the consumers allowed by RXF:
 *     notes (N, MidiNotes, Phase B), CC0 (P, MidiProgram, Phase D),
 *     other CCs (C, MidiControl/MidiLearn, Phase E/F),
 *     program change (P, MidiProgram, Phase D).
 *  5. A message no consumer acted on is forwarded per MRT.
 * Why: consumed messages must not also reach the downstream device.
 * Inputs: one framed message with bits.source and stamp set.
 * Outputs: consumer actions; forwarding. Accessors: midiIn_frameDinByte(),
 * midiIn_service() (USB). Affiliates: midiOut_forward(),
 * midiIn_acceptedDuringBankLoad(), preset_backgroundBankLoadRunning().
 */
static void midiIn_dispatch(const MidiMsg *msg)
{
	uint8_t consumed = 0u;
	uint8_t type;

	if (msg->bits.sysxbyte) {
		midiOut_forward(msg);
		return;
	}
	if (preset_backgroundBankLoadRunning() &&
	    !midiIn_acceptedDuringBankLoad(msg)) {
		midiOut_forward(msg);
		return;
	}
	if (msg->status >= 0xF0u) {
		(void)midiTransport_handleSystemCommon(msg);
		midiOut_forward(msg);
		return;
	}
	type = (uint8_t)(msg->status & 0xF0u);
	switch (type) {
	case NOTE_ON:
	case NOTE_OFF:
		/* N1-N6: notes resolve through the active Scene's note map. */
		if (midiIn_rxAllows(MIDI_FILTER_NOTE))
			consumed = midiNotes_handle(msg);
		break;
	case MIDI_CC:
		/*
		 * CC0 = Bank change under RXF P (F37); CC32 (Bank LSB) is reserved
		 * and not consumed. Other CCs under RXF C (CC2 here until Phase E).
		 */
		if (msg->data1 == CC_BANK_CHANGE) {
			if (midiIn_rxAllows(MIDI_FILTER_PROGRAM))
				consumed = midiProgram_bankChange(
				    (uint8_t)((msg->status & 0x0Fu) + 1u), msg->data2);
		} else if (msg->data1 != CC_BANK_SELECT_LSB &&
		           midiIn_rxAllows(MIDI_FILTER_CONTROL)) {
			/* E: CC1, CC2, NRPN, Learn (plan 3.5/3.6). */
			consumed = midiControl_handleCc(msg);
		}
		break;
	case PROG_CHANGE:
		if (midiIn_rxAllows(MIDI_FILTER_PROGRAM))
			consumed = midiProgram_programChange(
			    (uint8_t)((msg->status & 0x0Fu) + 1u), msg->data1);
		break;
	case CHANNEL_PRESSURE:
		/* S096 P3 C4: channel pressure under RXF C (Learn sources) */
		if (midiIn_rxAllows(MIDI_FILTER_CONTROL))
			consumed = midiControl_handlePressure(msg);
		break;
	default:
		break;
	}
	if (!consumed)
		midiOut_forward(msg);
}

/*
 * Frame one DIN byte (from midiParser_parseUartData(), S083 changes).
 *
 * What: builds channel messages with running status, passes SysEx through
 * one byte at a time (sysxbyte), frames MTC quarter frame and Song Select
 * (1 data byte) and, new in S083, Song Position Pointer (2 data bytes) and
 * Tune Request (0). Each message carries the arrival stamp of its first
 * byte: the status byte, or under running status the first data byte.
 * Why: SPP was framed as a 0-byte message (plan 2 Framing); Q6 needs the
 * arrival time. Realtime bytes never reach here (MidiDin diverts them), so
 * running status is undisturbed. Inputs: byte and its 16-bit stamp.
 * Output: midiIn_dispatch() per complete message. Accessor:
 * midiIn_service().
 */
static void midiIn_frameDinByte(uint8_t data, uint16_t stamp)
{
	if (data & 0x80u) {
		midiIn_dinMsg.bits.sysxbyte = 0u;
		midiIn_dinMsg.bits.source = midiSourceMIDI;
		midiIn_dinMsg.stamp = stamp;
		if ((data & 0xF0u) == 0xF0u) {
			midiIn_dinMsg.status = data;
			midiIn_dinMsg.data1 = 0u;
			midiIn_dinMsg.data2 = 0u;
			switch (data) {
			case SYSEX_START:
				midiIn_state = MIDI_IN_STATE_SYSEX;
				midiIn_dinMsg.bits.length = 0u;
				midiIn_dinMsg.bits.sysxbyte = 1u;
				midiIn_dispatch(&midiIn_dinMsg);
				return;
			case SYSEX_END:
				/* a stray end outside SysEx is ignored (as before) */
				if (midiIn_state == MIDI_IN_STATE_SYSEX) {
					midiIn_state = MIDI_IN_STATE_STATUS;
					midiIn_dinMsg.bits.length = 0u;
					midiIn_dinMsg.bits.sysxbyte = 1u;
					midiIn_dispatch(&midiIn_dinMsg);
				}
				return;
			case MIDI_MTC_QFRAME:
			case MIDI_SONG_SEL:
				midiIn_state = MIDI_IN_STATE_DATA1;
				midiIn_dinMsg.bits.length = 1u;
				return;
			case MIDI_SONG_POSITION:
				midiIn_state = MIDI_IN_STATE_DATA1;
				midiIn_dinMsg.bits.length = 2u;
				return;
			case MIDI_TUNE_REQUEST:
				midiIn_dinMsg.bits.length = 0u;
				midiIn_state = MIDI_IN_STATE_STATUS;
				midiIn_dispatch(&midiIn_dinMsg);
				return;
			default:	/* 0xF4/0xF5 undefined: ignore their data */
				midiIn_state = MIDI_IN_STATE_IGNORE;
				midiIn_dinMsg.bits.length = 0u;
				return;
			}
		}
		/* channel status (from midiParser_handleStatusByte()) */
		midiIn_dinMsg.status = data;
		switch (data & 0xF0u) {
		case NOTE_OFF:
		case NOTE_ON:
		case MIDI_AT:
		case MIDI_CC:
		case MIDI_PITCH_WHEEL:
			midiIn_dinMsg.bits.length = 2u;
			midiIn_state = MIDI_IN_STATE_DATA1;
			break;
		default:	/* PROG_CHANGE, CHANNEL_PRESSURE */
			midiIn_dinMsg.bits.length = 1u;
			midiIn_state = MIDI_IN_STATE_DATA1;
			break;
		}
		return;
	}
	switch (midiIn_state) {
	case MIDI_IN_STATE_STATUS:
		/* running status: only after a channel message */
		if (midiIn_dinMsg.bits.length == 0u || midiIn_dinMsg.status >= 0xF0u)
			return;
		midiIn_dinMsg.stamp = stamp;
		midiIn_dinMsg.data1 = data;
		if (midiIn_dinMsg.bits.length == 2u) {
			midiIn_state = MIDI_IN_STATE_DATA2;
			return;
		}
		midiIn_dinMsg.data2 = 0u;
		break;
	case MIDI_IN_STATE_DATA1:
		midiIn_dinMsg.data1 = data;
		if (midiIn_dinMsg.bits.length == 2u) {
			midiIn_state = MIDI_IN_STATE_DATA2;
			return;
		}
		midiIn_dinMsg.data2 = 0u;
		break;
	case MIDI_IN_STATE_DATA2:
		midiIn_dinMsg.data2 = data;
		break;
	case MIDI_IN_STATE_SYSEX:
		{
			MidiMsg sx = {0};

			sx.status = data;	/* payload byte, sent as-is */
			sx.bits.sysxbyte = 1u;
			sx.bits.source = midiSourceMIDI;
			sx.stamp = stamp;
			midiIn_dispatch(&sx);
		}
		return;
	default:
		return;
	}
	midiIn_state = MIDI_IN_STATE_STATUS;
	midiIn_dispatch(&midiIn_dinMsg);
	/* system common messages do not establish running status */
	if (midiIn_dinMsg.status >= 0xF0u)
		midiIn_dinMsg.bits.length = 0u;
}

/* Contract in MidiIn.h. */
void midiIn_service(void)
{
	uint8_t budget;
	uint8_t byte;
	uint16_t stamp;
	MidiMsg msg;

	budget = MIDI_DIN_DRAIN_PER_PASS;
	while (budget-- && midiDin_rxPop(&byte, &stamp))
		midiIn_frameDinByte(byte, stamp);
	budget = MIDI_USB_DRAIN_PER_PASS;
	while (budget-- && usb_getMidi(&msg))
		midiIn_dispatch(&msg);
	midiLearn_service();
	midiOut_service();
	usb_tick();
}
