/*
 * frontPanelParser.c — LXR-02 local protocol dispatcher.
 *
 * On the original two-chip LXR this translated UART protocol messages
 * between front-panel AVR and STM32 mainboard. On LXR-02 single-chip,
 * button/menu code still emits those protocol messages via
 * frontPanel_sendData(), and this file dispatches them locally.
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


#include "frontPanelParser.h"
#include "MidiParser.h"
#include "DrumVoice.h"
#include "Snare.h"
#include "CymbalVoice.h"
#include "HiHat.h"
#include "menu.h"
#include "ledHandler.h"
#include "modulationNode.h"
#include "buttonHandler.h"
#include "sequencer.h"
#include "triggerJacks.h"
#include "EuklidGenerator.h"
#include "SomGenerator.h"
#include <stdint.h>

/* ----------------------------------------------------------------------
** State (defined here; declared extern in the header)
** ---------------------------------------------------------------------- */
volatile uint8_t  frontParser_newSeqDataAvailable = 0;
volatile StepData frontParser_stepData = {0};
volatile MidiMsg  frontParser_midiMsg = {0};
uint8_t           frontPanel_sysexMode = 0;

uint8_t frontParser_activeTrack      = 0;
uint8_t frontParser_shownPattern     = 0;
uint8_t frontParser_activeFrontTrack = 0;
uint8_t frontParser_sysexActive      = 0;
uint8_t frontParser_activeStep       = 0;
uint8_t frontParser_rxCnt            = 0;
uint16_t frontParser_twoByteData     = 0;
uint8_t frontParser_sysexBuffer[7]   = {0};
uint16_t frontParser_sysexSeqStepNr  = 0;

static uint8_t frontParser_trackValid(uint8_t track)
{
    return (uint8_t)(track < NUM_TRACKS);
}

static uint8_t frontParser_patternValid(uint8_t pattern)
{
    return (uint8_t)(pattern < NUM_PATTERN);
}

static uint8_t frontParser_stepValid(uint8_t step)
{
    return (uint8_t)(step < NUM_STEPS);
}

static void frontParser_applyStepParamsToMenu(uint8_t stepNr)
{
    Step *step;
    uint8_t dest;

    if (!frontParser_trackValid(frontParser_activeTrack) ||
        !frontParser_patternValid(frontParser_shownPattern) ||
        !frontParser_stepValid(stepNr)) {
        return;
    }

    step = &seq_patternSet.seq_subStepPattern[frontParser_shownPattern]
                                           [frontParser_activeTrack]
                                           [stepNr];

    parameter_values[PAR_STEP_VOLUME] = (uint8_t)(step->volume & STEP_VOLUME_MASK);
    parameter_values[PAR_STEP_NOTE] = step->note;
    parameter_values[PAR_STEP_PROB] = step->prob;

    dest = step->param1Nr;
    if ((dest < 128u) && (dest != 0u)) dest--;
    if (dest < END_OF_SOUND_PARAMETERS)
        parameter_values[PAR_P1_DEST] = paramToModTarget[dest];

    dest = step->param2Nr;
    if ((dest < 128u) && (dest != 0u)) dest--;
    if (dest < END_OF_SOUND_PARAMETERS)
        parameter_values[PAR_P2_DEST] = paramToModTarget[dest];

    parameter_values[PAR_P1_VAL] = step->param1Val;
    parameter_values[PAR_P2_VAL] = step->param2Val;

    frontParser_activeStep = stepNr;
}

static void frontParser_applyPatternParamsToMenu(uint8_t patternNr)
{
    if (!frontParser_patternValid(patternNr)) return;

    parameter_values[PAR_PATTERN_BEAT] = seq_patternSet.seq_patternSettings[patternNr].changeBar;
    parameter_values[PAR_PATTERN_NEXT] = seq_patternSet.seq_patternSettings[patternNr].nextPattern;
}

static void frontParser_applyEuklidParamsToMenu(uint8_t trackNr)
{
    if (!frontParser_trackValid(trackNr)) return;
    parameter_values[PAR_EUKLID_LENGTH]   = euklid_getLength(trackNr);
    parameter_values[PAR_EUKLID_STEPS]    = euklid_getSteps(trackNr);
    parameter_values[PAR_EUKLID_ROTATION] = euklid_getRotation(trackNr);
}

static void frontParser_updateMainStepLeds(uint8_t trackNr, uint8_t patternNr)
{
    uint8_t i;

    if (!frontParser_trackValid(trackNr) || !frontParser_patternValid(patternNr)) {
        return;
    }

    for (i = 0; i < 16u; i++) {
        uint8_t on = seq_isMainStepActive(trackNr, i, patternNr);
        led_setValue(on, (uint8_t)(LED_STEP1 + i));
    }
}

static void frontParser_refreshGeneratedPatternLeds(uint8_t trackNr, uint8_t patternNr)
{
    if (patternNr != frontParser_shownPattern) {
        return;
    }

    frontParser_updateMainStepLeds(trackNr, patternNr);
}

void frontParser_handleLedMessage(uint8_t command, uint8_t value)
{
    switch (command) {
    case LED_CURRENT_STEP_NR:
    {
        uint8_t shownPattern;
        uint8_t playedPattern;

        if (!frontParser_stepValid(value)) return;

        shownPattern = menu_getViewedPattern();
        playedPattern = menu_playedPattern;

        if (shownPattern == playedPattern) {
            if ((menu_activePage < MENU_MIDI_PAGE) ||
                (menu_activePage == PERFORMANCE_PAGE) ||
                (menu_activePage == SEQ_PAGE) ||
                (menu_activePage == EUKLID_PAGE)) {
                led_setActive_step(value);
            }
        } else {
            led_clearActive_step();
        }
        break;
    }

    case LED_SEQ_BUTTON:
    {
        uint8_t mainStep = (uint8_t)((value & 0x7fu) / 8u);
        uint8_t on = 0;

        if (menu_activePage == PERFORMANCE_PAGE) return;
        if (mainStep >= 16u) return;

        if (frontParser_trackValid(frontParser_activeTrack) &&
            frontParser_patternValid(frontParser_shownPattern)) {
            on = seq_isMainStepActive(frontParser_activeTrack, mainStep, frontParser_shownPattern);
        }
        led_setValue(on, (uint8_t)(LED_STEP1 + mainStep));
        break;
    }

    case LED_SEQ_SUB_STEP:
    {
        uint8_t stepNr;
        uint8_t subStepRange;
        uint8_t ledOffset;
        uint8_t on = 0;

        if (!buttonHandler_getShift() && (buttonHandler_getMode() != SELECT_MODE_STEP)) {
            return;
        }

        stepNr = (uint8_t)(value & 0x7fu);
        subStepRange = buttonHandler_selectedStep;

        if ((stepNr < subStepRange) || (stepNr >= (uint8_t)(subStepRange + 8u))) {
            return;
        }

        if (frontParser_trackValid(frontParser_activeTrack) &&
            frontParser_patternValid(frontParser_shownPattern) &&
            frontParser_stepValid(stepNr)) {
            on = seq_isStepActive(frontParser_activeTrack, stepNr, frontParser_shownPattern);
        }

        ledOffset = (uint8_t)(stepNr - subStepRange);
        led_setValue(on, (uint8_t)(LED_PART_SELECT1 + ledOffset));
        break;
    }

    case LED_QUERY_SEQ_TRACK:
    {
        uint8_t trackNr = (uint8_t)(value >> 4);
        uint8_t patternNr = (uint8_t)(value & 0x7u);

        frontParser_updateTrackLeds(trackNr, patternNr);

        if (frontParser_trackValid(trackNr)) {
            parameter_values[PAR_TRACK_LENGTH] = seq_getTrackLength(trackNr);
            parameter_values[PAR_TRACK_ROTATION] = seq_getTrackRotation(trackNr);
        }
        break;
    }

    case LED_PULSE_BEAT:
        led_setValue((uint8_t)(value != 0u), LED_START_STOP);
        break;

    default:
        break;
    }
}

static void frontParser_handleSeqMessage(uint8_t command, uint8_t value)
{
    switch (command) {
    case SEQ_SET_ACTIVE_TRACK:
        if (frontParser_trackValid(value)) frontParser_activeTrack = value;
        break;

    case SEQ_SET_SHOWN_PATTERN:
        frontParser_shownPattern = (uint8_t)(value & 0x7u);
        break;

    case SEQ_REQUEST_STEP_PARAMS:
        frontParser_applyStepParamsToMenu(value);
        break;

    case SEQ_REQUEST_PATTERN_PARAMS:
        frontParser_applyPatternParamsToMenu(frontParser_shownPattern);
        break;

    case SEQ_REQUEST_EUKLID_PARAMS:
        frontParser_applyEuklidParamsToMenu(value);
        break;

    case SEQ_SET_PAT_BEAT:
        if (frontParser_patternValid(frontParser_shownPattern)) {
            seq_patternSet.seq_patternSettings[frontParser_shownPattern].changeBar = value;
            parameter_values[PAR_PATTERN_BEAT] = value;
        }
        break;

    case SEQ_SET_PAT_NEXT:
        if (frontParser_patternValid(frontParser_shownPattern)) {
            seq_patternSet.seq_patternSettings[frontParser_shownPattern].nextPattern = value;
            parameter_values[PAR_PATTERN_NEXT] = value;
        }
        break;

    case SEQ_SELECT_ACTIVE_STEP:
        if (frontParser_stepValid(value)) {
            seq_selectedStep = value;
            frontParser_activeStep = value;
        }
        break;

    case SEQ_NOTE:
        if (frontParser_trackValid(frontParser_activeTrack) &&
            frontParser_patternValid(frontParser_shownPattern) &&
            frontParser_stepValid(frontParser_activeStep)) {
            seq_patternSet.seq_subStepPattern[frontParser_shownPattern]
                                            [frontParser_activeTrack]
                                            [frontParser_activeStep].note = value;
            parameter_values[PAR_STEP_NOTE] = value;
        }
        break;

    case SEQ_VOLUME:
        if (frontParser_trackValid(frontParser_activeTrack) &&
            frontParser_patternValid(frontParser_shownPattern) &&
            frontParser_stepValid(frontParser_activeStep)) {
            Step *step = &seq_patternSet.seq_subStepPattern[frontParser_shownPattern]
                                                   [frontParser_activeTrack]
                                                   [frontParser_activeStep];
            step->volume &= (uint8_t)~STEP_VOLUME_MASK;
            step->volume |= (uint8_t)(value & STEP_VOLUME_MASK);
            parameter_values[PAR_STEP_VOLUME] = (uint8_t)(value & STEP_VOLUME_MASK);
        }
        break;

    case SEQ_PROB:
        if (frontParser_trackValid(frontParser_activeTrack) &&
            frontParser_patternValid(frontParser_shownPattern) &&
            frontParser_stepValid(frontParser_activeStep)) {
            seq_patternSet.seq_subStepPattern[frontParser_shownPattern]
                                            [frontParser_activeTrack]
                                            [frontParser_activeStep].prob = value;
            parameter_values[PAR_STEP_PROB] = value;
        }
        break;

    case SEQ_EUKLID_LENGTH:
        if (frontParser_trackValid(frontParser_activeTrack)) {
            uint8_t length = (uint8_t)((value >> 3) + 1u);
            euklid_setLength(frontParser_activeTrack, length);
            parameter_values[PAR_EUKLID_LENGTH] = length;
        }
        break;

    case SEQ_EUKLID_STEPS:
        if (frontParser_trackValid(frontParser_activeTrack)) {
            uint8_t steps = (uint8_t)((value >> 3) + 1u);
            uint8_t pattern = (uint8_t)(value & 0x7u);
            euklid_setSteps(frontParser_activeTrack, steps, pattern);
            parameter_values[PAR_EUKLID_STEPS] = steps;
            frontParser_refreshGeneratedPatternLeds(frontParser_activeTrack, pattern);
        }
        break;

    case SEQ_EUKLID_ROTATION:
        if (frontParser_trackValid(frontParser_activeTrack)) {
            uint8_t rotation = (uint8_t)(value >> 3);
            uint8_t pattern = (uint8_t)(value & 0x7u);
            euklid_setRotation(frontParser_activeTrack, rotation, pattern);
            parameter_values[PAR_EUKLID_ROTATION] = rotation;
            frontParser_refreshGeneratedPatternLeds(frontParser_activeTrack, pattern);
        }
        break;

    case SEQ_TRACK_LENGTH:
        if (frontParser_trackValid(frontParser_activeTrack)) {
            seq_setTrackLength(frontParser_activeTrack, value);
            parameter_values[PAR_TRACK_LENGTH] = value;
        }
        break;

    case SEQ_TRACK_ROTATION:
        if (frontParser_trackValid(frontParser_activeTrack)) {
            seq_setTrackRotation(frontParser_activeTrack, value);
            parameter_values[PAR_TRACK_ROTATION] = value;
        }
        break;

    case SEQ_SHUFFLE:
        seq_setShuffle((float)value / 127.0f);
        break;

    case SEQ_SET_AUTOM_TRACK:
        seq_setActiveAutomationTrack(value);
        break;

    case SEQ_SET_QUANT:
        seq_setQuantisation(value);
        break;

    case SEQ_POSX:
        som_setX(value);
        break;

    case SEQ_POSY:
        som_setY(value);
        break;

    case SEQ_FLUX:
        som_setFlux((float)value / 127.0f);
        break;

    case SEQ_SOM_FREQ:
    {
        uint8_t voice = (uint8_t)(value >> 4);
        uint8_t freq  = (uint8_t)(value & 0x0fu);
        som_setFreq(freq, voice);
        break;
    }

    case SEQ_MIDI_CHAN:
    {
        uint8_t voice = (uint8_t)(value >> 4);
        uint8_t channel = (uint8_t)(value & 0x0fu);

        if (voice < 8u) {
            if ((voice < 7u) && (midi_MidiChannels[voice] != channel)) {
                voiceControl_noteOff(voice);
            }
            midi_MidiChannels[voice] = channel;
        }
        break;
    }

    case SEQ_MIDI_ROUTING:
        midiParser_setRouting(value);
        break;

    case SEQ_MIDI_FILT_TX:
        midiParser_setFilter(1u, value);
        break;

    case SEQ_MIDI_FILT_RX:
        midiParser_setFilter(0u, value);
        break;

    case SEQ_BAR_RESET_MODE:
        seq_resetBarOnPatternChange = value;
        break;

    case SEQ_TRIGGER_IN_PPQ:
        triggerJacks_setClockInputPpq(value);
        break;

    case SEQ_TRIGGER_OUT1_PPQ:
        triggerJacks_setClockOut1Ppq(value);
        break;

    case SEQ_TRIGGER_OUT2_PPQ:
        triggerJacks_setClockOut2Ppq(value);
        break;

    case SEQ_TRIGGER_GATE_MODE:
        trigger_setGatemode(value);
        break;

    case SEQ_ROLL_RATE:
        seq_setRollRate(value);
        break;

    case SEQ_ROLL_ON_OFF:
    {
        uint8_t onOff = (uint8_t)(value >> 4);
        uint8_t voice = (uint8_t)(value & 0x0fu);
        if (frontParser_trackValid(voice)) {
            seq_setRoll(voice, (uint8_t)(onOff != 0u));
        }
        break;
    }

    case SEQ_CHANGE_PAT:
        seq_setNextPattern((uint8_t)(value & 0x07u));
        break;

    case SEQ_RUN_STOP:
        seq_setRunning((uint8_t)(value != 0u));
        buttonHandler_setRunStopState((uint8_t)(value != 0u));
        break;

    case SEQ_REC_ON_OFF:
        seq_setRecordingMode((uint8_t)(value != 0u));
        break;

    case SEQ_ERASE_ON_OFF:
        seq_setErasingMode((uint8_t)(value != 0u));
        break;

    case SEQ_MUTE_TRACK:
        if (frontParser_trackValid(value)) seq_setMute(value, 1);
        break;

    case SEQ_UNMUTE_TRACK:
        if (frontParser_trackValid(value)) seq_setMute(value, 0);
        break;

    default:
        /* _SEQUENCER_ADD_SPIKE_: commands not mapped yet remain intentional no-ops
        ** until the remaining two-chip parser behavior is folded in. */
        break;
    }
}

/* ----------------------------------------------------------------------
** Local protocol send endpoint
** ---------------------------------------------------------------------- */
void frontPanel_sendData(uint8_t status, uint8_t data1, uint8_t data2)
{
    MidiMsg msg = {0};

    /* Preserve last message for legacy side channels that read this global. */
    frontParser_midiMsg.status = status;
    frontParser_midiMsg.data1 = data1;
    frontParser_midiMsg.data2 = data2;

    switch (status) {
    case LED_CC:
        frontParser_handleLedMessage(data1, data2);
        break;

    case SEQ_CC:
        frontParser_handleSeqMessage(data1, data2);
        break;

    case SAMPLE_CC:
        if (data1 == SAMPLE_COUNT) {
            menu_setNumSamples(data2);
        }
        break;

    case MIDI_CC:
        msg.status = MIDI_CC;
        msg.data1 = (uint8_t)((data1 + 1u) & 0x7fu);
        msg.data2 = data2;
        midiParser_ccHandler(msg, 1);
        if (frontParser_trackValid(frontParser_activeTrack)) {
            seq_recordAutomation(frontParser_activeTrack, msg.data1, msg.data2);
        }
        break;

    case CC_2:
        msg.status = MIDI_CC2;
        msg.data1 = data1;
        msg.data2 = data2;
        midiParser_ccHandler(msg, 1);
        if (frontParser_trackValid(frontParser_activeTrack)) {
            seq_recordAutomation(frontParser_activeTrack, (uint8_t)(data1 + 128u), msg.data2);
        }
        break;

    case CC_LFO_TARGET:
    {
        uint8_t value = (uint8_t)(((data1 & 0x01u) << 7) | data2);
        uint8_t lfoNr = (uint8_t)((data1 & 0xfeu) >> 1);

        switch (lfoNr) {
        case 0:
        case 1:
        case 2: modNode_setDestination(&voiceArray[lfoNr].lfo.modTarget, value); break;
        case 3: modNode_setDestination(&snareVoice.lfo.modTarget, value); break;
        case 4: modNode_setDestination(&cymbalVoice.lfo.modTarget, value); break;
        case 5: modNode_setDestination(&hatVoice.lfo.modTarget, value); break;
        default: break;
        }
        break;
    }

    case CC_VELO_TARGET:
    {
        uint8_t value = (uint8_t)(((data1 & 0x01u) << 7) | data2);
        uint8_t velModNr = (uint8_t)((data1 & 0xfeu) >> 1);
        if (velModNr < 6u)
            modNode_setDestination(&velocityModulators[velModNr], value);
        break;
    }

    case MAIN_STEP_CC:
    {
        uint8_t voiceNr = (uint8_t)(data1 >> 4);
        uint8_t patternNr = (uint8_t)(data1 & 0x7u);
        uint8_t stepNr = data2;

        if (frontParser_trackValid(voiceNr) &&
            frontParser_patternValid(patternNr) &&
            (stepNr < 16u)) {
            uint8_t on;
            seq_toggleMainStep(voiceNr, stepNr, patternNr);
            on = seq_isMainStepActive(voiceNr, stepNr, patternNr);
            led_setValue(on, (uint8_t)(LED_STEP1 + stepNr));
        }
        break;
    }

    case STEP_CC:
    {
        uint8_t voiceNr = (uint8_t)(data1 >> 4);
        uint8_t patternNr = (uint8_t)(data1 & 0x7u);
        uint8_t stepNr = data2;

        if (frontParser_trackValid(voiceNr) &&
            frontParser_patternValid(patternNr) &&
            frontParser_stepValid(stepNr)) {
            seq_toggleStep(voiceNr, stepNr, patternNr);

            if ((voiceNr == frontParser_activeTrack) &&
                (patternNr == frontParser_shownPattern)) {
                uint8_t subStepRange = buttonHandler_selectedStep;
                if ((stepNr >= subStepRange) && (stepNr < (uint8_t)(subStepRange + 8u))) {
                    uint8_t on = seq_isStepActive(voiceNr, stepNr, patternNr);
                    led_setValue(on, (uint8_t)(LED_PART_SELECT1 + (stepNr - subStepRange)));
                }
            }
        }
        break;
    }

    case ARM_AUTOMATION_STEP:
    {
        uint8_t stepNr = data1;
        uint8_t onOff = (uint8_t)(data2 & 0x40u);
        uint8_t trackNr = (uint8_t)(data2 & 0x3fu);

        if (frontParser_trackValid(trackNr)) {
            seq_armAutomationStep(stepNr, trackNr, onOff);
        }
        break;
    }

    case SET_P1_DEST:
    case SET_P2_DEST:
    {
        uint8_t value = (uint8_t)((data1 << 7) | data2);

        if (!frontParser_trackValid(frontParser_activeTrack) ||
            !frontParser_patternValid(frontParser_shownPattern) ||
            !frontParser_stepValid(seq_selectedStep)) {
            break;
        }

        if ((value != 0u) && (value < 128u)) value++;

        if (status == SET_P1_DEST) {
            seq_patternSet.seq_subStepPattern[frontParser_shownPattern]
                                            [frontParser_activeTrack]
                                            [seq_selectedStep].param1Nr = value;
        } else {
            seq_patternSet.seq_subStepPattern[frontParser_shownPattern]
                                            [frontParser_activeTrack]
                                            [seq_selectedStep].param2Nr = value;
        }
        break;
    }

    case SET_P1_VAL:
    case SET_P2_VAL:
    {
        uint8_t stepNr = data1;

        if (!frontParser_trackValid(frontParser_activeTrack) ||
            !frontParser_patternValid(frontParser_shownPattern) ||
            !frontParser_stepValid(stepNr)) {
            break;
        }

        if (status == SET_P1_VAL) {
            seq_patternSet.seq_subStepPattern[frontParser_shownPattern]
                                            [frontParser_activeTrack]
                                            [stepNr].param1Val = data2;
        } else {
            seq_patternSet.seq_subStepPattern[frontParser_shownPattern]
                                            [frontParser_activeTrack]
                                            [stepNr].param2Val = data2;
        }
        break;
    }

    case SET_BPM:
    {
        uint16_t bpm = (uint16_t)data1 | (uint16_t)(data2 << 7);
        if (bpm == 0u)
            bpm = 1u;
        seq_setBpm(bpm);
        break;
    }

    case VOICE_CC:
        if (data1 == VOICE_DECIMATION) {
            uint8_t targetTrack = frontParser_activeTrack;
            uint16_t paramNr;

            if (targetTrack > 6u) targetTrack = 6u;
            paramNr = (uint16_t)(PAR_VOICE_DECIMATION1 + targetTrack);
            if (paramNr < NUM_PARAMS) {
                parameter_values[paramNr] = data2;

                if (paramNr < 128u) {
                    msg.status = MIDI_CC;
                    msg.data1 = (uint8_t)((paramNr + 1u) & 0x7fu);
                    msg.data2 = data2;
                    midiParser_ccHandler(msg, 1);
                } else if (paramNr < END_OF_SOUND_PARAMETERS) {
                    msg.status = MIDI_CC2;
                    msg.data1 = (uint8_t)(paramNr - 128u);
                    msg.data2 = data2;
                    midiParser_ccHandler(msg, 1);
                }
            }
        }
        break;

    default:
        break;
    }
}

void frontPanel_sendByte(uint8_t data)
{
    (void)data;
}

void frontPanel_sendMidiMsg(MidiMsg msg)
{
    frontPanel_sendData(msg.status, msg.data1, msg.data2);
}

void frontPanel_parseData(uint8_t data)
{
    (void)data;
}

void frontParser_parseUartData(unsigned char data)
{
    (void)data;
}

void frontParser_updateTrackLeds(const uint8_t trackNr, uint8_t patternNr)
{
    uint8_t i;
    uint8_t start;

    if (!frontParser_trackValid(trackNr) || !frontParser_patternValid(patternNr)) {
        return;
    }

    frontParser_activeFrontTrack = trackNr;

    frontParser_updateMainStepLeds(trackNr, patternNr);

    start = (uint8_t)((buttonHandler_selectedStep / 8u) * 8u);
    for (i = 0; i < 8u; i++) {
        uint8_t stepNr = (uint8_t)(start + i);
        uint8_t on = 0;
        if (frontParser_stepValid(stepNr)) {
            on = seq_isStepActive(trackNr, stepNr, patternNr);
        }
        led_setValue(on, (uint8_t)(LED_PART_SELECT1 + i));
    }
}

/* ----------------------------------------------------------------------
** Reverse-direction: sequencer → front panel SEQ_CC feedback.
** These commands must NOT route through frontPanel_sendData() because
** the SEQ_CC command values are identical in both directions and would
** re-trigger the action (direction collision — see SEQ-FPP_AUDIT.md §2.2).
** ---------------------------------------------------------------------- */
void seq_notifyFront(uint8_t command, uint8_t value)
{
    switch (command) {
    case SEQ_CHANGE_PAT:
    {
        uint8_t patNr = (uint8_t)(value & 0x07u);
        menu_playedPattern = patNr;

        if (parameter_values[PAR_FOLLOW]) {
            menu_setShownPattern(patNr);
            led_clearSequencerLeds();
            frontParser_handleLedMessage(LED_QUERY_SEQ_TRACK,
                (uint8_t)((frontParser_activeTrack << 4) | (patNr & 0x7u)));
        }

        if (buttonHandler_getMode() == SELECT_MODE_PERF) {
            led_clearAllBlinkLeds();
            led_initPerformanceLeds();
        }
        break;
    }

    case SEQ_TRACK_ROTATION:
        parameter_values[PAR_TRACK_ROTATION] = value;
        break;

    case SEQ_RUN_STOP:
        buttonHandler_setRunStopState(value);
        break;

    default:
        break;
    }
}

/* ----------------------------------------------------------------------
** Decoupled sequencer → LED state buffer and processor.
** ---------------------------------------------------------------------- */
SeqLedState seq_ledState = {0};

void seq_ledState_process(void)
{
    uint8_t d = seq_ledState.dirty;
    if (!d) return;
    seq_ledState.dirty = 0;

    if (d & SEQ_LED_DIRTY_BEAT)
        frontParser_handleLedMessage(LED_PULSE_BEAT, seq_ledState.beatPulse);

    if (d & SEQ_LED_DIRTY_CHASE)
        frontParser_handleLedMessage(LED_CURRENT_STEP_NR, seq_ledState.chaseStep);

    if (d & SEQ_LED_DIRTY_REC_SUB)
        frontParser_handleLedMessage(LED_SEQ_SUB_STEP, seq_ledState.recordSubStep);

    if (d & SEQ_LED_DIRTY_REC_MAIN)
        frontParser_handleLedMessage(LED_SEQ_BUTTON, seq_ledState.recordMainStep);
}

/* ----------------------------------------------------------------------
** Frontpanel-UART stubs — declared in Uart.h, no-op on F765.
** ---------------------------------------------------------------------- */
#include "Uart.h"

void initFrontpanelUart(void) { }
void uart_processFront(void) { }
void uart_sendFrontpanelByte(uint8_t data) { (void)data; }
void uart_sendFrontpanelSysExByte(uint8_t data) { (void)data; }
void uart_clearFrontFifo(void) { }
void uart_checkAndParse(void) { }
