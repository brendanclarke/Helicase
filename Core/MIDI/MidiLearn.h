/*
 * MidiLearn.h
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

#ifndef MIDILEARN_H_
#define MIDILEARN_H_

/*
 * MidiLearn - learned CCs and the Learn gesture (S083, plan 3.6, C4-C6,
 * MIDI_SPEC 6).
 *
 * What: the device-wide table (up to MIDI_LEARN_MAX_ENTRIES rows of 4 B:
 * CC + channel kind, target space, type, index); the capture state of the
 * SHIFT+PLAY+REC gesture; the lookup that routes a received CC to its
 * targets (all matching rows, macros); the TX lookup; and the midilrn.cfg
 * `learn=` line format (S083 §16; the table was in settings.cfg before).
 * It is the module that coordinates CCs into the other modules: matches
 * are written through MidiControl, which commits them as menu edits (Q1).
 * Rules: one CC -> many parameters (one row each); one parameter -> at most
 * one CC (rows are unique on target space + type + index; re-learning
 * replaces CC and channel kind, F49); a row applies only while its type is
 * loaded in the active Scene's slot / Effect (Scene 17 while active, F51);
 * kind `g` = on G, kind `v` = on the target voice's own channel (voice and
 * per-voice Scene targets only); a `v` row affects only its own slot even
 * when several voices share the channel (F50); reserved CCs never learn.
 * Why: plan F6/F27; no fan-out, no Scene storage, no UI remove (delete the
 * line from midilrn.cfg).
 * Context: foreground. RAM: 518 B SRAM1 static (approved ~519).
 * Affiliates: MidiControl (capture/lookup caller, writes), menu.c (target
 * selection, LRN/FUL display, page-change cancel), buttonHandler (gesture,
 * LEDs), filesystem.c (midilrn.cfg load/write, write request),
 * MidiOut (TX CC).
 */

#include <stdint.h>

/*
 * S096 P3 C4: the source of a learned row (plan §5.2). The row's `space`
 * bits 3..4 hold it; bits 5..6 hold the mode (0 default, 1 absolute,
 * 2 + delta, 3 - delta). A CC's default is absolute; pressure's is +delta
 * (the brief); note and velocity default absolute.
 */
enum {
    MIDI_LEARN_SRC_CC = 0,
    MIDI_LEARN_SRC_PRESSURE,
    MIDI_LEARN_SRC_NOTE,
    MIDI_LEARN_SRC_VELOCITY
};

/* Empty the table (start of the boot midilrn.cfg load). */
void     midiLearn_clearAll(void);
uint8_t  midiLearn_count(void);

/* Gesture (buttonHandler, menu). */
void     midiLearn_arm(void);		/* SHIFT+PLAY+REC */
void     midiLearn_cancel(void);	/* SHIFT/PLAY/REC press, page change */
uint8_t  midiLearn_active(void);	/* armed, waiting or showing FUL */
/* 1 while a touched cell becomes the target (armed, or re-target). */
uint8_t  midiLearn_selectingTarget(void);
void     midiLearn_selectTarget(uint16_t id);	/* menu: touched cell */
/* Target shown as `LRN` (or `FUL`), MIDI_TARGET_OFF when none. */
uint16_t midiLearn_displayTarget(void);
uint8_t  midiLearn_showingFull(void);
void     midiLearn_service(void);	/* FUL timeout (foreground) */

/*
 * Receive (MidiControl, non-reserved CCs only).
 * captureCc(): while a target waits for its CC, a valid CC (not reserved;
 * on G, or on the target voice's channel for voice targets) stores or
 * replaces the row and is consumed (1). applyCc(): writes every matching
 * row's target; 1 when any was written (consumed).
 */
uint8_t  midiLearn_captureCc(uint8_t channel, uint8_t cc);
uint8_t  midiLearn_applyCc(uint8_t channel, uint8_t cc, uint8_t value7);
/*
 * S096 P3: pressure capture (the first pressure message after a target) and
 * the generic source apply. applySource() matches src (and, for a CC row,
 * `cc`), then writes absolute values as a menu edit or applies a ±delta
 * overlay (with the Q11 recording rule). applyNote() feeds note-number and
 * velocity rows for every note-on; it is not consumed.
 */
uint8_t  midiLearn_capturePressure(uint8_t channel);
uint8_t  midiLearn_applySource(uint8_t channel, uint8_t src, uint8_t cc,
                               uint8_t value7);
void     midiLearn_applyNote(uint8_t channel, uint8_t note, uint8_t vel);

/* Transmit: the first row for `id` matching the active type. Output: 1
** with *cc and *voiceKind (1 = send on the voice's channel). */
uint8_t  midiLearn_ccForTarget(uint16_t id, uint8_t *cc,
                               uint8_t *voiceKind);

/*
 * midilrn.cfg (F34, S083 §16).
 * formatFileLine(): line `index` of the table as
 * "learn=<CCC|prs±=|nte±=|vel±=>,<g|v>,<v1..v6|fx|scn>,<type>,<key>\n"
 * (Scene rows have no type field). A row whose names cannot be resolved writes a "#" comment
 * line so the line count stays exact. Returns the line length in bytes
 * (the filesystem_writeTextLine() contract); 0 when `index` is past the
 * table or cap < 32. Caller: filesystem_nextMidiLearnLine().
 * parseFileValue(): one `learn=` value; stores or replaces its row (1); a
 * malformed or unresolvable line is skipped (0), the file still loads.
 * Caller: filesystem_parseMidiLearnLine().
 */
uint8_t  midiLearn_formatFileLine(uint8_t index, char *dst, uint16_t cap);
uint8_t  midiLearn_parseFileValue(const char *value);

#endif /* MIDILEARN_H_ */
