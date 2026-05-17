/*
 * frontPanelParser.h — LXR-02 shim.
 *
 * On the original LXR (two-chip) this header is the API for the AVR↔STM32
 * inter-processor protocol. The AVR's frontPanelParser.h and the STM32
 * mainboard's frontPanelParser.h together defined the wire format and
 * the send/parse functions on each side.
 *
 * On LXR-02 (single-chip F765) the inter-processor protocol does not
 * exist. Sequencer/DSP code that calls frontPanel_sendData(LED_CC, ...)
 * is, post-port, addressing local memory directly — the LED state is in
 * dout_outputData[], the menu state is in menu.c, etc.
 *
 * This shim exists so sequencer.c, clockSync.c, MidiParser.c and friends
 * can land verbatim. It provides:
 *
 *   1. ALL the message-constant macros (LED_CC, SEQ_CC, CODEC_CC,
 *      VOICE_CC, SET_BPM, CC_2, CC_LFO_TARGET, CC_VELO_TARGET, STEP_CC,
 *      SET_P*_DEST/VAL, MAIN_STEP_CC, ARM_AUTOMATION_STEP, SAMPLE_CC,
 *      LED_*, VOICE_*, SEQ_*, SYSEX_*, …) with the same numeric values
 *      so file-format compatibility is preserved.
 *
 *   2. Stubs for the send functions (frontPanel_sendData, sendByte,
 *      sendMidiMsg). These are defined in frontPanelParser.c as empty.
 *      Replace one-by-one with direct calls into ledHandler/menu when
 *      wiring up sequencer→UI feedback.
 *
 *   3. The extern frontParser_* state vars sequencer.c reads
 *      (frontParser_activeTrack, frontParser_shownPattern, etc.).
 *      Defined in frontPanelParser.c and updated by menu.c when the
 *      user changes pattern/track.
 *
 * The file format identifier macros SYSEX_REQUEST_STEP_DATA etc. are
 * useless on F765 (no sysex round-trip needed) but kept defined so
 * untouched preset-load code references resolve. The pattern save/load
 * paths in presetManager.c are stubs anyway until the sequencer port.
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


#ifndef FRONTPANELPARSER_H_
#define FRONTPANELPARSER_H_

#include <stdint.h>
#include "MidiMessages.h"   /* for MidiMsg + standard MIDI constants */
#include "SeqStep.h"        /* StepData (used as buffer for protocol parse) */

/* ----------------------------------------------------------------------
** State vars (defined in frontPanelParser.c)
** ---------------------------------------------------------------------- */
extern volatile uint8_t  frontParser_newSeqDataAvailable;
extern volatile StepData frontParser_stepData;
extern volatile MidiMsg  frontParser_midiMsg;
extern uint8_t           frontPanel_sysexMode;
extern uint8_t           buttonHandler_selectedStep;

/* AVR-side protocol header had an `extern uint8_t frontPanel_sysexMode`
** plus these mainboard-side state vars. Sequencer reads activeTrack and
** shownPattern; we wire menu→these at every page change. */
extern uint8_t           frontParser_activeTrack;     /* 0..NUM_TRACKS-1 */
extern uint8_t           frontParser_shownPattern;    /* 0..NUM_PATTERN-1 */
extern uint8_t           frontParser_activeFrontTrack;/* legacy alias */
extern uint8_t           frontParser_sysexActive;
extern uint8_t           frontParser_activeStep;
extern uint8_t           frontParser_rxCnt;
extern uint16_t          frontParser_twoByteData;
extern uint8_t           frontParser_sysexBuffer[7];
extern uint16_t          frontParser_sysexSeqStepNr;

/* ----------------------------------------------------------------------
** Status bytes — verbatim from original AVR frontPanelParser.h
** Keep numeric values identical: file-format and any 3rd-party tools
** that sniff this protocol expect these.
** ---------------------------------------------------------------------- */
/* (NOTE_ON, MIDI_CC, SYSEX_START, SYSEX_END are pulled from MidiMessages.h.) */

/* control messages */
#define LED_CC                  0xb1
#define SEQ_CC                  0xb2
#define CODEC_CC                0xb3
#define VOICE_CC                0xb4
#define SET_BPM                 0xb5
#define CC_2                    0xb6  /* params above 127 */
#define CC_LFO_TARGET           0xb7
#define CC_VELO_TARGET          0xb8
#define STEP_CC                 0xb9  /* toggle sub-step */
#define SET_P1_DEST             0xba
#define SET_P2_DEST             0xbb
#define SET_P1_VAL              0xbc
#define SET_P2_VAL              0xbd
#define MAIN_STEP_CC            0xbe  /* toggle main step */
#define ARM_AUTOMATION_STEP     0xbf
#define SAMPLE_CC               0xc0
#define SAMPLE_START_UPLOAD     0x01
#define SAMPLE_COUNT            0x02

/* preset status bytes (different namespace from above — 0xb4/0xb5
** also appear here as PRESET_NAME/PRESET. The original protocol used
** context to disambiguate; keeping verbatim.) */
#define PRESET_NAME             0xb4
#define PRESET                  0xb5
#define PRESET_LOAD             0x01
#define PRESET_SAVE             0x02
#define PATTERN_LOAD            0x03

/* LED_CC sub-messages */
#define LED_CURRENT_STEP_NR     0x01
#define LED_SEQ_BUTTON          0x02
#define LED_QUERY_SEQ_TRACK     0x03
#define LED_PULSE_BEAT          0x04
#define LED_SEQ_SUB_STEP        0x05

/* VOICE_CC sub-messages */
#define VOICE_DECIMATION        0x03

/* SEQ_CC sub-messages */
#define SEQ_RUN_STOP            0x01
#define SEQ_MUTE_TRACK          0x09
#define SEQ_UNMUTE_TRACK        0x0a
#define SEQ_CHANGE_PAT          0x0b
#define SEQ_ROLL_ON             0x0c
#define SEQ_ROLL_OFF            0x0d
#define SEQ_REQUEST_STEP_PARAMS 0x0f
#define SEQ_ROLL_ON_OFF         0x10
#define SEQ_ROLL_RATE           0x11
#define SEQ_VOLUME              0x12
#define SEQ_NOTE                0x13
#define SEQ_PROB                0x14
#define SEQ_SET_ACTIVE_TRACK    0x15
#define SEQ_EUKLID_LENGTH       0x17
#define SEQ_EUKLID_STEPS        0x18
#define SEQ_REQUEST_EUKLID_PARAMS 0x19
#define SEQ_SET_SHOWN_PATTERN   0x1A
#define SEQ_REC_ON_OFF          0x1B
#define SEQ_REQUEST_PATTERN_PARAMS 0x1C
#define SEQ_SET_PAT_BEAT        0x1D
#define SEQ_SET_PAT_NEXT        0x1E
#define SEQ_CLEAR_TRACK         0x1f
#define SEQ_COPY_TRACK          0x20
#define SEQ_COPY_PATTERN        0x21
#define SEQ_SET_QUANT           0x22
#define SEQ_SET_AUTOM_TRACK     0x23
#define SEQ_SELECT_ACTIVE_STEP  0x24
#define SEQ_SHUFFLE             0x25
#define SEQ_TRACK_LENGTH        0x26
#define SEQ_CLEAR_PATTERN       0x27
#define SEQ_CLEAR_AUTOM         0x28
#define SEQ_POSX                0x29
#define SEQ_POSY                0x2a
#define SEQ_FLUX                0x2b
#define SEQ_SOM_FREQ            0x2c
#define SEQ_MIDI_CHAN           0x2d
#define SEQ_MIDI_MODE           0x2e
#define SEQ_MIDI_ROUTING        0x2f
#define SEQ_MIDI_FILT_TX        0x30
#define SEQ_MIDI_FILT_RX        0x31
#define SEQ_BAR_RESET_MODE      0x32
#define SEQ_ERASE_ON_OFF        0x33
#define SEQ_TRACK_ROTATION      0x34
#define SEQ_EUKLID_ROTATION     0x35
#define SEQ_TRIGGER_IN_PPQ      0x36
#define SEQ_TRIGGER_OUT1_PPQ    0x37
#define SEQ_TRIGGER_OUT2_PPQ    0x38
#define SEQ_TRIGGER_GATE_MODE   0x39

/* SysEx sub-protocol */
#define SYSEX_REQUEST_STEP_DATA       0x01
#define SYSEX_SEND_STEP_DATA          0x02
#define SYSEX_REQUEST_MAIN_STEP_DATA  0x03
#define SYSEX_SEND_MAIN_STEP_DATA     0x04
#define SYSEX_REQUEST_PATTERN_DATA    0x05
#define SYSEX_SEND_PAT_LEN_DATA       0x06

/* Random pattern sentinels (see sequencer.h SEQ_NEXT_RANDOM/_PREV) */
#define SEQ_NEXT_RANDOM         0x08
#define SEQ_NEXT_RANDOM_PREV    0x09

/* ----------------------------------------------------------------------
** Send functions — STUBBED on F765
**
** When sequencer/DSP source files call these to push state to the front
** panel, the calls compile and link but do nothing. Replace each call
** site with a direct call into the relevant subsystem when wiring:
**
**   frontPanel_sendData(LED_CC, LED_CURRENT_STEP_NR, step)
**     → ledHandler_setStep(step) [or similar]
**
**   frontPanel_sendData(SEQ_CC, SEQ_RUN_STOP, 1)
**     → menu state update (run/stop indicator) + ledHandler
**
**   frontPanel_sendData(MIDI_CC, paramNr, val)
**     → mixer_setParam(paramNr, val) [DSP — for parameter automation]
** ---------------------------------------------------------------------- */
void frontPanel_sendData(uint8_t status, uint8_t data1, uint8_t data2);
void frontPanel_sendByte(uint8_t data);
void frontPanel_sendMidiMsg(MidiMsg msg);

/* Reverse-direction handler: sequencer/MIDI → front panel LED updates.
** Non-static so sequencer.c can bypass frontPanel_sendData(). */
void frontParser_handleLedMessage(uint8_t command, uint8_t value);

/* Reverse-direction handler: sequencer → front panel SEQ_CC feedback.
** Routes pattern-change ack, run/stop notification, rotation reset.
** Must NOT go through frontPanel_sendData() (direction collision). */
void seq_notifyFront(uint8_t command, uint8_t value);

/* Decoupled sequencer → LED state buffer.
** Sequencer writes during seq_tick(); seq_ledState_process() drains
** it each main-loop iteration before led_tickHandler(). */
#define SEQ_LED_DIRTY_CHASE    0x01
#define SEQ_LED_DIRTY_BEAT     0x02
#define SEQ_LED_DIRTY_REC_SUB  0x04
#define SEQ_LED_DIRTY_REC_MAIN 0x08

typedef struct {
    volatile uint8_t dirty;
    volatile uint8_t chaseStep;
    volatile uint8_t beatPulse;
    volatile uint8_t recordSubStep;
    volatile uint8_t recordMainStep;
} SeqLedState;

extern SeqLedState seq_ledState;
void seq_ledState_process(void);

/* Receive-side stub — original AVR side parsed inter-processor data here.
** Not used on F765 single-chip. Provided in case anything references it. */
void frontPanel_parseData(uint8_t data);

/* STM32-mainboard side had a parser too; sequencer.c does not call it
** but parserModule.c may. Stub. */
void frontParser_parseUartData(unsigned char data);
void frontParser_updateTrackLeds(const uint8_t trackNr, uint8_t patternNr);

#endif
