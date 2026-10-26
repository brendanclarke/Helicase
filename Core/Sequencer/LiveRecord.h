/*
 * LiveRecord.h
 *
 * S081 P1 live-record and live-erase engine (plan §4, §19.2).
 *
 * Lives in Core/Sequencer beside the Sequencer. Direction of dependency:
 * buttonHandler / Menu / MidiParser call into LiveRecord; LiveRecord calls into
 * the Sequencer, PatternStackService, EffectsManager and SceneData getters. The
 * Sequencer never calls LiveRecord. LiveRecord never calls Menu, buttonHandler
 * or MidiParser.
 *
 * Created on: 13.10.2026
 * Modifications Copyright 2026 Brendan Clarke
 * Part of the LXR02 open-source software under the same license as the LXR
 * firmware.
 */

#ifndef LIVEREC_H_
#define LIVEREC_H_

#include "stm32f4xx.h"
#include <stdint.h>

/*
 * Recording mode (S081 §6).
 *
 * OFF    idle. LATCH REC toggled on: latched parameters write their menu value
 * on the recording track each pass. OVERDUB REC held: only adjusted parameters
 * are written. ERASE SHIFT+CLEAR held: live erase (§10). OVERDUB and ERASE are
 * mutually exclusive.
 */
enum {
    LIVEREC_MODE_OFF = 0,
    LIVEREC_MODE_LATCH,
    LIVEREC_MODE_OVERDUB,
    LIVEREC_MODE_ERASE
};

/*
 * Trigger hit kinds (S081 E-1, §8.7-8.8).
 *
 * SINGLE      a plain single hit.
 * FLAM_GRACE  the soft leading hit of a flam (fires in overlayTrigger).
 * FLAM_MAIN   the flam's delayed main hit (fires in overlayTick).
 * ROLL_START  the first hit of a held roll (opens a span, S084 P4).
 * ROLL_HIT    later hits of a held roll, reported by the foreground drain
 *             (one report may cover several TIM3 hits). It carries the held
 *             rate; it opens a span on a track with none open (S084 P4).
 */
enum {
    LIVEREC_HIT_SINGLE = 0,
    LIVEREC_HIT_FLAM_GRACE,
    LIVEREC_HIT_FLAM_MAIN,
    LIVEREC_HIT_ROLL_START,
    LIVEREC_HIT_ROLL_HIT
};

/* Trigger sources (S081 §8.9, R2). */
enum {
    LIVEREC_SRC_OVERLAY = 0,
    LIVEREC_SRC_MIDI
};

/*
 * One trigger hit reported when it sounds (S081 §4.2; S083 Q6 age).
 *
 * Inputs: track 0..6, note, velocity, kind (LIVEREC_HIT_*), roll (the
 * step-roll index on ROLL_START and ROLL_HIT; 0 otherwise), rhythm (S084
 * P4: the roll's packed velocity rhythm, the overlay pv byte, on roll
 * kinds; 0 otherwise), source (LIVEREC_SRC_*),
 * note_is_default (1 when the note came from the track default, so a 'def'
 * records no special), age_us (S083: how long ago the hit actually
 * happened, in us; 0 = now). A MIDI note's age is its arrival-to-processing
 * delay, so placement uses the arrival time (Q6). Panel, overlay repeat and
 * sequencer hits pass 0. Stack only (+3 B per record since S083).
 */
typedef struct {
    uint8_t track;
    uint8_t note;
    uint8_t velocity;
    uint8_t kind;
    uint8_t roll;
    uint8_t rhythm;
    uint8_t source;
    uint8_t note_is_default;
    uint16_t age_us;
} liveRec_trigger_t;

/* Lifecycle. */
void liveRec_init(uint8_t activeTrack);
void liveRec_service(void);

/* Mode (buttonHandler). */
void    liveRec_setLatch(uint8_t on);
void    liveRec_overdubBegin(void);
void    liveRec_overdubEnd(void);
void    liveRec_eraseBegin(void);
void    liveRec_eraseEnd(void);
uint8_t liveRec_mode(void);
uint8_t liveRec_armed(void);
uint8_t liveRec_patternWriting(void);

/* Recording track (Menu). */
void    liveRec_onActiveTrackChanged(uint8_t track);
void    liveRec_setRecordTrack(uint8_t track);
uint8_t liveRec_recordTrack(void);
/* Is the active Scene's Pattern closed for editing? True only while live
** record or erase is writing (armed and running) AND the recording target
** T = P(rtk) differs from the active Scene A (S081 Q13, P1B item 1). */
uint8_t liveRec_activePatternLocked(void);

/* Triggers (buttonHandler overlay, MidiNotes). */
void    liveRec_triggerIn(const liveRec_trigger_t *t);
/* S084 P4: rate and rhythm of a held roll after an overlay change (0 ends
** it); release ends one track's roll. */
void    liveRec_rollRateChange(uint8_t track, uint8_t rollIndex,
                               uint8_t rhythm);
void    liveRec_rollRelease(uint8_t track);

/* Automation (Menu now, MidiParser later). S081 P3 (G-1): a voice or FX
** Morph edit captures its value at the edit and that value is written, so a
** global Morph change (never recorded) cannot leak into a pending Morph
** write. The global Morph (mrp) has no target and never reaches this call. */
void    liveRec_paramIn(uint16_t target9, uint8_t source);

/* Erase gestures (buttonHandler / Menu). */
void    liveRec_eraseParam(uint16_t target9, uint8_t fxLane);
/* SEQ hold erase on the active track (VOICE/STEP; S081 §10.2, F7). `track` is
** the active track; the mask is captured on the first press. */
void    liveRec_eraseSeqHeld(uint8_t track, uint8_t held);
/* FX-mode SEQ hold erase: counted per held SEQ button; while nonzero every FX
** lock lane is erased. Knob-erase lanes are separate (S081 §10.2, P1C 2/10). */
void    liveRec_eraseFxSeqHeld(uint8_t held);
void    liveRec_eraseTrackHeld(uint8_t track, uint8_t held);
/* Release every held erase gesture (SEQ/TRACK hold counts, specials, full);
** keeps erase mode, the knob-erase bitmap and the knob-erase FX lanes (S081
** §10.1, P1B item 8, P1C 2/16). Callers: handleModeButtons() and eraseEnd. */
void    liveRec_eraseReleaseHeld(void);

#endif /* LIVEREC_H_ */
