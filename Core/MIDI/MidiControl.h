/*
 * MidiControl.h
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

#ifndef MIDICONTROL_H_
#define MIDICONTROL_H_

/*
 * MidiControl - received control changes (S083, plan 3.5, C1-C3, C7, C8).
 *
 * What: for one CC under RXF C: CC2 (note action, G only) -> MidiNotes;
 * CC1 (Morph: global on G, never recorded; per voice on a voice channel,
 * recorded as an individual voice Morph edit); the NRPN protocol (CC 99/98
 * number, CC6 value, CC38 ignored, CC96/97 +/-1, RPN selects/deselects);
 * then MIDI Learn (capture, then lookup, Phase F). Every parameter write
 * goes through menu_commitTargetEdit() (a MIDI write is a menu edit, Q1).
 * Channel rule (C2): every ID works on G; on a voice channel only that
 * channel's voices' IDs (voice block and per-voice Scene targets: Nvm, Nou,
 * Nfx, 7dc, and len/scl/shf of its tracks). Consumption (C-7): CC98/99/
 * 100/101/38 are consumed on G or a voice channel; CC6/96/97 only when the
 * selected number is a valid target there.
 * Why: plan 3.5; replaces the legacy LXR CC/CC2 map (retired, Phase A).
 * Context: foreground. RAM: NRPN contexts 48 B SRAM1 (approved).
 * Affiliates: MidiIn (caller), MidiNoteMap (channels), MidiNotes (CC2),
 * MidiLearn (F), menu.c (commit), SceneModTargets, EffectsManager.
 */

#include <stdint.h>
#include "MidiDefs.h"

/* One CC (not CC0/CC32, which MidiProgram owns). Output: 1 = consumed. */
uint8_t  midiControl_handleCc(const MidiMsg *msg);
/* Received channel pressure (S096 P3 C4): a waiting Learn takes it, else the
** pressure rows apply. 1 = consumed. Caller: midiIn_dispatch(). */
uint8_t  midiControl_handlePressure(const MidiMsg *msg);
/*
 * 1 when target `id` may be written from `channel` in the active Scene:
 * on G every ID; on a voice channel the IDs of its voices (C2).
 * Accessors: midiControl_handleCc(), MidiLearn (kind v), MidiOut (G).
 */
uint8_t  midiControl_targetOnChannel(uint16_t id, uint8_t channel);
/* Write one target as a menu edit; 1 = valid target. (MidiLearn, NRPN.) */
uint8_t  midiControl_writeTarget(uint16_t id, uint8_t value7);
/*
 * Current 7-bit value of a target in Scene `scene_index` (Normal image;
 * Morph amounts translated back 255 -> 127, else /2; WIDE8 Effect rows
 * through effects_storeValue7()). Accessors: CC96/97 (+/-1), MidiOut
 * parameter TX (G). Mirrors liveRec_targetValue7() plus track length.
 * Always 0..127: Instrument bytes above 127 (target-selection tokens) are
 * clamped (S083 assessment F-4).
 */
uint8_t  midiControl_targetValue7(uint8_t scene_index, uint16_t id);
/* Voice slots (bit per slot 0..5) whose tracks use `channel` and it is
** not G. 0 = not a voice channel. */
uint8_t  midiControl_voiceSlotsOnChannel(uint8_t channel);
/* Effective channel of a target's voice in the active Scene (0 = none). */
uint8_t  midiControl_targetChannel(uint16_t id);

#endif /* MIDICONTROL_H_ */
