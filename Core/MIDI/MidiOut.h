/*
 * MidiOut.h
 *
 *  Created on: 21.10.2026
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

#ifndef MIDIOUT_H_
#define MIDIOUT_H_

/*
 * MidiOut - everything the LXR transmits (S083).
 *
 * What: the only module that writes to the DIN and USB ports. It frames
 * messages for each port (USB: cable 0 and the correct code index for
 * channel, system-common, SysEx and realtime messages), forwards received
 * messages per MRT, sends LXR-generated realtime, note, program-change,
 * Bank-change and parameter messages under the TXF filter, keeps one
 * note-off record per track, and (Phase G) suppresses echoes and coalesces
 * parameter output.
 * Why: plan 3.1/3.9. Before S083 the sequencer and the parser each wrote
 * the ports, USB system messages used code index = status >> 4 (wrong for
 * SPP and SysEx), and the note-off cache was one note per channel, so two
 * tracks on one channel could leave a hanging note.
 * Context: foreground and TIM3 (sequencer notes, clock, realtime
 * forwarding). Every port write runs under the driver's PRIMASK section.
 * RAM: per-track note-off 14 B (R-B); Phase G adds 8 B.
 * Affiliates: MidiDin.c, usb_manager.c, MidiIn.c (configuration),
 * sequencer.c, MidiTransport.c, MidiNoteMap.c (G).
 */

#include <stdint.h>
#include "MidiDefs.h"

/* Send one message to the ports in portMask, framed per port. Bypasses TXF:
** callers apply the filter. Context: any. */
void midiOut_sendMsg(const MidiMsg *msg, uint8_t portMask);
/*
 * Forward a received message per MRT (plan 3.2).
 * Inputs: message with bits.source set. Output: copies to the routed
 * ports; never to its own source port's same direction pair for USB
 * (USB -> USB never), DIN -> DIN only when routed. TXF does not apply
 * (pass-through follows MRT only, plan M3). Accessor: midiIn_dispatch().
 */
void midiOut_forward(const MidiMsg *msg);
/* Forward one realtime byte per MRT (TIM3; always, consumed or not). */
void midiOut_forwardRealtime(uint8_t status, uint8_t source);
/* LXR-generated realtime (Clock, Start, Continue, Stop) to both ports,
** under TXF R. Context: TIM3 and foreground. */
void midiOut_realtime(uint8_t status);
/*
 * Track note output (plan 3.9 N).
 * What: sends the track's previous note-off first (if one is sounding),
 * then note-on (channel, note, velocity) under TXF N, and records note and
 * channel for the next note-off. channel 0 sends nothing (no channel).
 * Context: TIM3 and foreground. The previous note-off, the note-on and the
 * record update run as one PRIMASK section, so concurrent triggers of one
 * track cannot leave a note hanging downstream (S083 assessment F-2).
 * Phase A callers pass the old channel/note rule; Phase G passes the
 * inverse-mapped note (midiOut_trackTrigger()).
 */
void midiOut_trackNoteOn(uint8_t track, uint8_t channel, uint8_t note,
                         uint8_t velocity);
/* Note-off for one track's sounding note, or every track (0xFF). Sent
** regardless of TXF so a note started before a filter change still ends.
** Each track's read, clear and send is one PRIMASK section (F-2). */
void midiOut_trackNoteOff(uint8_t track);
/* Program change on `channel` (1..16) under TXF P. */
void midiOut_programChange(uint8_t channel, uint8_t program);
/* Foreground service (Phase G: parameter coalescing, CC0 after a load). */
void midiOut_service(void);
/*
 * Track trigger output (plan 3.9 N, S083 Q4, C-2, C-6).
 * What: maps (track, internal note) through the inverse note map of the
 * Scene the track plays and sends it with midiOut_trackNoteOn(); when the
 * pitch has no note on the track's range nothing is sent (the previous
 * note still ends). Inputs: track 0..6, played Scene, internal note,
 * velocity. Context: TIM3 and foreground. Accessors: seq_triggerVoice(),
 * seq_previewVoice() (every LXR trigger: sequencer, overlay, preview,
 * MIDI-in, C-3).
 */
void midiOut_trackTrigger(uint8_t track, uint8_t scene_index,
                          uint8_t internal, uint8_t velocity);
/*
 * Transmit a wide (Tetrakt) track's trigger (S096 P3 T1): ends the track's
 * sounding notes, then one note-on per layer in `mask`, the inverse note for
 * that (track, internal, sub). Stores the base note and mask for the
 * note-offs. Context: foreground (the trigger fire). A non-wide track is
 * passed to midiOut_trackTrigger(). Full contract in MidiOut.c.
 */
void midiOut_trackLayers(uint8_t track, uint8_t scene_index, uint8_t internal,
                         uint8_t velocity, uint8_t mask);
/*
 * A panel edit changed target `id` (plan 3.9 C).
 * What: marks it pending; midiOut_service() sends its current value as the
 * learned CC (first row for the current type) or as NRPN, at most every
 * MIDI_OUT_PARAM_INTERVAL_MS and only with room in the DIN FIFO. A change
 * to another target sends the pending one first. MIDI-originated writes
 * never call this (no echo, R6). Accessor: menu.c panel edit paths (G-7).
 */
void midiOut_parameterChanged(uint16_t id);
/*
 * Per-track Scene change PCs (plan 3.9 P, C-8): one PC (Scene number) on
 * each distinct effective channel of the moved tracks in the target Scene
 * (C-6), except G and none; the HiHat pair counts as one move (F-7).
 * Accessor: buttonHandler_perfVoiceHeldSeqPressed() (panel only; a MIDI
 * per-track PC is not echoed).
 */
void midiOut_trackProgramChange(uint8_t trackMask, uint8_t scene_index);

#endif /* MIDIOUT_H_ */
