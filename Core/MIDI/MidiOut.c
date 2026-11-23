/*
 * MidiOut.c - everything the LXR transmits (S083).
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

#include "MidiOut.h"
#include "MidiIn.h"
#include "MidiDin.h"
#include "MidiTransport.h"
#include "MidiNoteMap.h"
#include "MidiControl.h"
#include "MidiLearn.h"
#include "SceneData.h"
#include "BankData.h"
#include "presetManager.h"
#include "timebase.h"
#include "config.h"
#include "usb_manager.h"

/*
 * Per-track note-off record (plan 3.9, R-B).
 *
 * What: midiOut_trackNote[t] bit 7 = a note is sounding, bits 0..6 the note;
 * midiOut_trackChannel[t] the 1..16 channel it was sent on. Why: a
 * note-off must reach the channel and note of its own note-on even when
 * the track's channel or the note map changed in between, and several
 * tracks may share a channel. RAM: 14 B SRAM1 (replaces the sequencer's
 * 16 + 2 B channel cache). Writers: midiOut_trackNoteOn/Off(), each
 * holding PRIMASK across read, clear, send and record (TIM3 and the
 * foreground both call them; assessment F-2).
 */
static uint8_t midiOut_trackNote[7];
static uint8_t midiOut_trackChannel[7];
/*
 * S096 P3 T1 (finding D3): a wide track's sounding layer notes.
 *
 * What: bit k = the note (base + k) is sounding; midiOut_trackNote[] keeps
 * the base note and midiOut_trackChannel[] the channel. Why: a Tetrakt step
 * transmits one note per layer, and every one must be turned off. RAM: 7 B.
 * Writers: midiOut_trackLayers(); reader: midiOut_trackNoteOffLocked().
 */
static uint8_t midiOut_trackLayersBits[7];

static uint32_t midiOut_irqSave(void)
{
	uint32_t primask;
	__asm volatile ("mrs %0, primask\ncpsid i" : "=r" (primask) :: "memory");
	return primask;
}

static void midiOut_irqRestore(uint32_t primask)
{
	__asm volatile ("msr primask, %0" :: "r" (primask) : "memory");
}

/*
 * USB-MIDI code index for one message (USB-MIDI 1.0 table 4-1).
 *
 * What: channel messages use status >> 4; MTC and Song Select 0x2; Song
 * Position 0x3; Tune Request 0x5; realtime and SysEx bytes 0xF (single
 * byte, C-10). Why: usb_sendMidi() used status >> 4 for every message, so
 * forwarded SPP, MTC and SysEx were malformed on USB (plan 2 Framing).
 * Inputs: message. Output: code index 0..15.
 */
static uint8_t midiOut_usbCin(const MidiMsg *msg)
{
	if (msg->bits.sysxbyte)
		return 0x0Fu;
	if (msg->status < 0xF0u)
		return (uint8_t)(msg->status >> 4);
	switch (msg->status) {
	case MIDI_MTC_QFRAME:
	case MIDI_SONG_SEL:      return 0x02u;
	case MIDI_SONG_POSITION: return 0x03u;
	case MIDI_TUNE_REQUEST:  return 0x05u;
	default:                 return 0x0Fu;
	}
}

/* Contract in MidiOut.h. */
void midiOut_sendMsg(const MidiMsg *msg, uint8_t portMask)
{
	uint8_t bytes[3];
	uint8_t count;

	if (!msg)
		return;
	bytes[0] = msg->status;
	bytes[1] = msg->data1;
	bytes[2] = msg->data2;
	count = (uint8_t)(1u + msg->bits.length);
	if (portMask & MIDI_PORT_DIN)
		(void)midiDin_sendBytes(bytes, count);
	if (portMask & MIDI_PORT_USB) {
		/*
		 * A USB SysEx packet received from USB is never re-sent to USB
		 * (no USB -> USB route); a DIN SysEx byte goes as one single-byte
		 * packet. Every other message is one packet, unused bytes 0.
		 */
		usb_sendMidiPacket(midiOut_usbCin(msg), bytes[0],
		                   (count > 1u) ? bytes[1] : 0u,
		                   (count > 2u) ? bytes[2] : 0u);
	}
}

/* Contract in MidiOut.h. */
void midiOut_forward(const MidiMsg *msg)
{
	uint8_t routes = midiIn_routeMask();
	uint8_t ports = 0u;

	if (!msg || routes == 0u)
		return;
	if (msg->bits.source == midiSourceUSB) {
		if (routes & MIDI_ROUTE_USB_TO_DIN)
			ports |= MIDI_PORT_DIN;
	} else {
		if (routes & MIDI_ROUTE_DIN_TO_DIN)
			ports |= MIDI_PORT_DIN;
		if (routes & MIDI_ROUTE_DIN_TO_USB)
			ports |= MIDI_PORT_USB;
	}
	if (ports)
		midiOut_sendMsg(msg, ports);
}

/* Contract in MidiOut.h. */
void midiOut_forwardRealtime(uint8_t status, uint8_t source)
{
	MidiMsg msg = {0};

	msg.status = status;
	msg.bits.source = (source == MIDI_REALTIME_SOURCE_USB)
	                      ? midiSourceUSB : midiSourceMIDI;
	midiOut_forward(&msg);
}

/* Contract in MidiOut.h. */
void midiOut_realtime(uint8_t status)
{
	MidiMsg msg = {0};

	if (!midiIn_txAllows(MIDI_FILTER_REALTIME))
		return;
	msg.status = status;
	midiOut_sendMsg(&msg, MIDI_PORT_BOTH);
}

/*
 * End one track's sounding note; the caller holds PRIMASK (assessment F-2).
 *
 * What: reads and clears the track's record and sends the note-off on the
 * channel the note-on used. Sent regardless of TXF. Why: the read, clear
 * and send must be one unit with any following note-on, otherwise TIM3
 * (sequencer) and the foreground (MIDI-in, overlay, preview) triggering
 * the same track could interleave and one note would never be turned off
 * downstream. Inputs: track 0..6. Outputs: record cleared, note-off queued
 * (midiOut_sendMsg(); both port drivers nest PRIMASK). Accessors:
 * midiOut_trackNoteOff(), midiOut_trackNoteOn().
 */
static void midiOut_trackNoteOffLocked(uint8_t t)
{
	uint8_t rec = midiOut_trackNote[t];
	uint8_t ch = midiOut_trackChannel[t];
	uint8_t layers = midiOut_trackLayersBits[t];

	midiOut_trackNote[t] = 0u;
	midiOut_trackLayersBits[t] = 0u;
	if ((rec & 0x80u) && ch >= 1u && ch <= 16u) {
		MidiMsg msg = {0};
		uint8_t k;

		msg.status = (uint8_t)(NOTE_OFF | (ch - 1u));
		msg.bits.length = 2u;
		if (layers == 0u) {
			/* 0x80 note-off, velocity 0 (some gear ignores 0x90 v0). */
			msg.data1 = (uint8_t)(rec & 0x7Fu);
			midiOut_sendMsg(&msg, MIDI_PORT_BOTH);
		} else {
			for (k = 0u; k < 4u; k++) {
				if ((layers & (uint8_t)(1u << k)) == 0u)
					continue;
				msg.data1 = (uint8_t)((rec & 0x7Fu) + k);
				midiOut_sendMsg(&msg, MIDI_PORT_BOTH);
			}
		}
	}
}

/* Contract in MidiOut.h. One PRIMASK section per track (F-2). */
void midiOut_trackNoteOff(uint8_t track)
{
	uint8_t first = (track == 0xFFu) ? 0u : track;
	uint8_t last = (track == 0xFFu) ? 6u : track;
	uint8_t t;

	if (track != 0xFFu && track > 6u)
		return;
	for (t = first; t <= last; t++) {
		uint32_t primask = midiOut_irqSave();

		midiOut_trackNoteOffLocked(t);
		midiOut_irqRestore(primask);
	}
}

/*
 * Contract in MidiOut.h. The previous note's off, the new note-on and the
 * record update form one PRIMASK section (F-2): at most two DIN queue
 * inserts and two USB packet writes, a few microseconds.
 */
void midiOut_trackNoteOn(uint8_t track, uint8_t channel, uint8_t note,
                         uint8_t velocity)
{
	MidiMsg msg = {0};
	uint32_t primask;

	if (track > 6u)
		return;
	primask = midiOut_irqSave();
	midiOut_trackNoteOffLocked(track);
	if (channel >= 1u && channel <= 16u && note <= 127u &&
	    midiIn_txAllows(MIDI_FILTER_NOTE)) {
		msg.status = (uint8_t)(NOTE_ON | (channel - 1u));
		msg.data1 = note;
		msg.data2 = (uint8_t)(velocity & 0x7Fu);
		msg.bits.length = 2u;
		midiOut_sendMsg(&msg, MIDI_PORT_BOTH);
		midiOut_trackNote[track] = (uint8_t)(0x80u | note);
		midiOut_trackChannel[track] = channel;
	}
	midiOut_irqRestore(primask);
}

/* Contract in MidiOut.h. */
void midiOut_programChange(uint8_t channel, uint8_t program)
{
	MidiMsg msg = {0};

	if (channel < 1u || channel > 16u || program > 127u ||
	    !midiIn_txAllows(MIDI_FILTER_PROGRAM))
		return;
	msg.status = (uint8_t)(PROG_CHANGE | (channel - 1u));
	msg.data1 = program;
	msg.bits.length = 1u;
	midiOut_sendMsg(&msg, MIDI_PORT_BOTH);
}

/*
 * Parameter TX state (S083 plan 3.9 C; RAM 8 B approved).
 * midiOut_nrpnKey[port]: last NRPN number sent per port as
 * ((channel - 1) << 9 | id) + 1, 0 = none, so the CC99/98 pair is resent
 * only when the number or channel changes. midiOut_pending: target + 1
 * waiting to be sent (0 = none). midiOut_lastSendMs: time_sysTick of the
 * last parameter send (throttle).
 */
static uint16_t midiOut_nrpnKey[2];
static uint16_t midiOut_pending;
static uint16_t midiOut_lastSendMs;

/* Contract in MidiOut.h. */
void midiOut_trackTrigger(uint8_t track, uint8_t scene_index,
                          uint8_t internal, uint8_t velocity)
{
	midiNoteMap_settings_t map;
	uint8_t channel;
	uint8_t note;

	midiNoteMap_load(scene_index, &map);
	/* S096 P3 T1: a wide (Tetrakt) track transmits at the trigger fire, in
	 * the foreground, with the mask that played (finding D3). */
	if ((map.wide & (uint8_t)(1u << track)) != 0u)
		return;
	if (!midiNoteMap_inverse(&map, track, internal, 0u, &channel, &note)) {
		midiOut_trackNoteOff(track);	/* Q4: no note for this pitch */
		return;
	}
	midiOut_trackNoteOn(track, channel, note, velocity);
}

/*
 * Transmit a wide track's trigger (S096 P3 T1, decided Q15; finding D3).
 *
 * What: ends the track's sounding notes (one or several layer notes), then
 * sends one note-on per layer in `mask`: the note that resolves back to
 * (track, internal, sub = layer), so a recorded MIDI part plays back the
 * same layers. Stores the base note and mask for the note-offs.
 * Inputs: track 0..6, scene_index (the track's played Scene), internal,
 * vel, mask 1..15. Context: foreground (the trigger fire); PRIMASK as
 * midiOut_trackNoteOn(). Latency: up to one render pass (2.18 ms) after
 * TIM3's step time, for wide tracks only.
 */
void midiOut_trackLayers(uint8_t track, uint8_t scene_index, uint8_t internal,
                         uint8_t velocity, uint8_t mask)
{
	midiNoteMap_settings_t map;
	uint8_t channel;
	uint8_t base;
	uint32_t primask;
	uint8_t k;

	if (track > 6u)
		return;
	midiNoteMap_load(scene_index, &map);
	if ((map.wide & (uint8_t)(1u << track)) == 0u) {
		midiOut_trackTrigger(track, scene_index, internal, velocity);
		return;
	}
	if (!midiNoteMap_inverse(&map, track, internal, 0u, &channel, &base)) {
		midiOut_trackNoteOff(track);
		return;
	}
	primask = midiOut_irqSave();
	midiOut_trackNoteOffLocked(track);
	if (channel >= 1u && channel <= 16u && mask != 0u &&
	    midiIn_txAllows(MIDI_FILTER_NOTE)) {
		uint8_t sent = 0u;

		for (k = 0u; k < 4u; k++) {
			uint8_t chk;
			uint8_t note;
			MidiMsg msg = {0};

			if ((mask & (uint8_t)(1u << k)) == 0u)
				continue;
			if (!midiNoteMap_inverse(&map, track, internal, k, &chk, &note) ||
			    chk != channel)
				continue;
			msg.status = (uint8_t)(NOTE_ON | (channel - 1u));
			msg.data1 = note;
			msg.data2 = (uint8_t)(velocity & 0x7Fu);
			msg.bits.length = 2u;
			midiOut_sendMsg(&msg, MIDI_PORT_BOTH);
			sent |= (uint8_t)(1u << k);
		}
		if (sent != 0u) {
			midiOut_trackNote[track] = (uint8_t)(0x80u | base);
			midiOut_trackChannel[track] = channel;
			midiOut_trackLayersBits[track] = sent;
		}
	}
	midiOut_irqRestore(primask);
}

/*
 * Send one control change (parameter TX, CC0 after a Bank load).
 * What: CC `cc` = `value` on `channel` (1..16) to the ports in portMask.
 * The data bytes are masked to 7 bits (S083 assessment F-4): a byte
 * >= 0x80 would be read downstream as a status byte (0xFF = System
 * Reset). Callers: midiOut_sendParameter(), midiOut_service().
 */
static void midiOut_cc(uint8_t channel, uint8_t cc, uint8_t value,
                       uint8_t portMask)
{
	MidiMsg msg = {0};

	msg.status = (uint8_t)(MIDI_CC | ((channel - 1u) & 0x0Fu));
	msg.data1 = (uint8_t)(cc & 0x7Fu);
	msg.data2 = (uint8_t)(value & 0x7Fu);
	msg.bits.length = 2u;
	midiOut_sendMsg(&msg, portMask);
}

/*
 * Send one target's current value (learned CC, else NRPN; Q8 channel
 * rule). Inputs: id. Output: messages on both ports under TXF C.
 */
static void midiOut_sendParameter(uint16_t id)
{
	uint8_t global = midiIn_globalChannel();
	uint8_t value = midiControl_targetValue7(scene_getActiveIndex(), id);
	uint8_t cc;
	uint8_t voiceKind;
	uint8_t channel;
	uint8_t port;

	if (!midiIn_txAllows(MIDI_FILTER_CONTROL))
		return;
	if (midiLearn_ccForTarget(id, &cc, &voiceKind)) {
		channel = voiceKind ? midiControl_targetChannel(id) : global;
		if (channel != 0u)
			midiOut_cc(channel, cc, value, MIDI_PORT_BOTH);
		return;
	}
	channel = (global != 0u) ? global : midiControl_targetChannel(id);
	if (channel == 0u)
		return;
	for (port = 0u; port < 2u; port++) {
		uint8_t mask = (port == 0u) ? MIDI_PORT_DIN : MIDI_PORT_USB;
		uint16_t key = (uint16_t)((((uint16_t)(channel - 1u) << 9) | id) + 1u);

		if (midiOut_nrpnKey[port] != key) {
			midiOut_cc(channel, CC_NRPN_MSB, (uint8_t)(id >> 7), mask);
			midiOut_cc(channel, CC_NRPN_LSB, (uint8_t)(id & 0x7Fu), mask);
			midiOut_nrpnKey[port] = key;
		}
		midiOut_cc(channel, CC_DATA_ENTRY_MSB, value, mask);
	}
}

/* Contract in MidiOut.h. */
void midiOut_parameterChanged(uint16_t id)
{
	if (id >= MIDI_TARGET_OFF)
		return;
	if (midiOut_pending != 0u && midiOut_pending != (uint16_t)(id + 1u)) {
		/* another target is waiting: send it now so no change is lost */
		uint16_t older = (uint16_t)(midiOut_pending - 1u);

		midiOut_pending = 0u;
		midiOut_sendParameter(older);
		midiOut_lastSendMs = time_sysTick;
	}
	midiOut_pending = (uint16_t)(id + 1u);
}

/* Contract in MidiOut.h. */
void midiOut_trackProgramChange(uint8_t trackMask, uint8_t scene_index)
{
	midiNoteMap_settings_t map;
	uint16_t sent = 0u;
	uint8_t global = midiIn_globalChannel();
	uint8_t t;

	if (scene_index >= 16u)
		return;
	/*
	 * C-6: the tracks have just been assigned to `scene_index`, so their PC
	 * goes on the channel they now play there. Only the panel gesture calls
	 * this (buttonHandler_perfVoiceHeldSeqPressed() after
	 * seq_assignTracksToScene()); a received per-track PC is never echoed
	 * (Q9), so midiProgram_programChange() does not call it (assessment
	 * D-2). The HiHat pair moved together (F-7), so its partner's channel
	 * is included too.
	 */
	if (trackMask & 0x60u)
		trackMask |= 0x60u;
	midiNoteMap_load(scene_index, &map);
	for (t = 0u; t < 7u; t++) {
		uint8_t ch;

		if ((trackMask & (1u << t)) == 0u)
			continue;
		ch = midiNoteMap_effectiveChannel(&map, t);
		if (ch == 0u || ch == global || (sent & (1u << (ch - 1u))))
			continue;
		sent |= (uint16_t)(1u << (ch - 1u));
		midiOut_programChange(ch, scene_index);
	}
}

/*
 * Foreground transmit service (S083 plan 3.9).
 * What: (1) sends the pending parameter when the interval passed and the
 * DIN FIFO has room for a full NRPN (9 B); (2) after a Menu-origin Bank
 * load completes successfully, sends CC0 = Bank slot on G under TXF P
 * (a MIDI-origin load is not echoed, Q9; a failed load sends nothing).
 * Context: foreground (midiIn_service()). Affiliates:
 * preset_takeBackgroundBankLoadResult() (sole consumer of the S082 result
 * byte), bank_restoreBankSlot().
 */
void midiOut_service(void)
{
	uint8_t result;

	if (midiOut_pending != 0u &&
	    (uint16_t)(time_sysTick - midiOut_lastSendMs) >=
	        MIDI_OUT_PARAM_INTERVAL_MS &&
	    midiDin_txFree() >= 9u) {
		uint16_t id = (uint16_t)(midiOut_pending - 1u);

		midiOut_pending = 0u;
		midiOut_sendParameter(id);
		midiOut_lastSendMs = time_sysTick;
	}
	result = preset_takeBackgroundBankLoadResult();
	if ((result & PRESET_BG_RESULT_CODE) == PRESET_BG_RESULT_DONE &&
	    (result & PRESET_BG_RESULT_MIDI) == 0u &&
	    midiIn_txAllows(MIDI_FILTER_PROGRAM) &&
	    midiIn_globalChannel() != 0u && bank_restoreBankSlot() <= 127u)
		midiOut_cc(midiIn_globalChannel(), CC_BANK_CHANGE,
		           (uint8_t)bank_restoreBankSlot(), MIDI_PORT_BOTH);
}
