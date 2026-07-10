/*
 * menu.c — LXR-02 menu system.
 * Ported from original LXR AVR menu.c by Julian Schmidt.
 *
 * Changes from original:
 *   - PROGMEM / pgm_read_* stripped — direct array access
 *   - local controls call owning modules directly; no front-panel protocol shim
 *   - parameters2[] morph buffer declared here
 *   - screensaver_touch() → stub
 *   - copyClear_isClearModeActive() → always returns 0
 *   - lockPotentiometerFetch() → stub (no pot fetch locking needed)
 *   - All sequencer-specific logic (led_setActive_step etc.) → stub
 *
 * DO NOT reorder or restructure — keep as close to original as possible.
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


#include "menu.h"
#include "CcNr2Text.h"
#include "screensaver.h"
#include "copyClearTools.h"
#include "menuPages.h"
#include "MenuText.h"
// #include "Parameters.h"
#include "ParameterArray.h"
#include "buttonHandler.h"
#include "lcd.h"
#include "ledHandler.h"
#include "endlessPots.h"
#include "AudioCodecManager.h"
#include "timebase.h"
#include "presetManager.h"
#include "filesystem.h"
#include "SampleMemory.h"
#include "sequencer.h"
#include "PatternData.h"
#include "SceneData.h"
#include "EuklidGenerator.h"
#include "SomGenerator.h"
#include "triggerJacks.h"
#include "MidiParser.h"
#include "modulationNode.h"
#include <string.h>
#include <stdint.h>

/* ---- stubs for original calls we haven't ported yet ---- */
// static inline void screensaver_touch(void){}
// static inline uint8_t copyClear_isClearModeActive(void){return 0;}
static inline void lockPotentiometerFetch(void){}

static uint8_t menu_TargetVoiceGapIndex = 0xFF;
static uint8_t menu_storageBusy = 0;
static uint8_t menu_deferSelectionRequest = 0;
static uint8_t menu_deferSelectionLoadKit = 0;
static uint8_t menu_lcdRefreshPending = 0;
static uint8_t menu_globalApplyActive = 0;
static uint8_t menu_globalApplyResetSave = 0;
static uint8_t menu_globalApplyRepaintAll = 0;
static uint16_t menu_globalApplyIndex = PAR_BEGINNING_OF_GLOBALS;

/* Runtime sound-apply completion flags.
**
** Kit, ALL, and performance loads all need the same six-voice modulation
** routing apply, but their follow-up work differs. These flags let the
** chunked apply path finish with the exact same UI/sequencer/global side
** effects that the old synchronous switch cases performed in one pass. */
static uint8_t menu_soundApplyActive = 0;
static uint8_t menu_soundApplyUpdateGap = 0;
static uint8_t menu_soundApplyResetSave = 0;
static uint8_t menu_soundApplyRepaintAll = 0;
static uint8_t menu_soundApplyStartGlobals = 0;
static uint8_t menu_soundApplyRequestPattern = 0;
static uint8_t menu_soundApplyApplyPerformanceGlobals = 0;
static uint8_t menu_soundApplyClearStorageBusy = 0;
static uint8_t menu_soundApplyShowStaleWarning = 0;
static fs_stale_warning_source_t menu_soundApplyStaleWarning = FS_STALE_WARNING_NONE;
static uint8_t menu_staleWarningActive = 0;
static uint16_t menu_staleWarningStart = 0;
static uint8_t menu_pendingAllStaleWarning = 0;

#define MENU_STALE_WARNING_MS 2000u

/* Globals can touch hardware/system settings rather than only DSP voice state.
** Before audio starts, applying all globals immediately is harmless. After
** audio starts, Session 023 amortizes the send across foreground passes so a
** globals/all-file load completion does not create one large control burst. */
static void menu_beginStorageMessage(const char *message)
{
    menu_storageBusy = 1;
    lcd_clear();
    lcd_home();
    lcd_string(message);
}

static void menu_normalizeSoundModTargets(uint8_t *values)
{
    uint8_t nmt = getNumModTargets();
    uint8_t i;

    for (i = 0; i < 6; i++) {
        if (values[PAR_VEL_DEST_1 + i] >= nmt)
            values[PAR_VEL_DEST_1 + i] = 0;
        if (values[PAR_TARGET_LFO1 + i] >= nmt)
            values[PAR_TARGET_LFO1 + i] = 0;
    }
}

static void menu_finishGlobalApply(void)
{
    menu_globalApplyActive = 0;
    if (menu_globalApplyResetSave)
        menu_resetSaveParameters();
    if (menu_globalApplyRepaintAll)
        menu_repaintAll();
    menu_globalApplyResetSave = 0;
    menu_globalApplyRepaintAll = 0;
}

static void menu_startGlobalApply(uint8_t resetSave, uint8_t repaintAll)
{
    if (audioCodec_renderCount == 0u) {
        menu_sendAllGlobals();
        if (resetSave)
            menu_resetSaveParameters();
        if (repaintAll)
            menu_repaintAll();
        return;
    }

    menu_globalApplyActive = 1;
    menu_globalApplyResetSave = resetSave;
    menu_globalApplyRepaintAll = repaintAll;
    menu_globalApplyIndex = PAR_BEGINNING_OF_GLOBALS;
}

static uint8_t menu_tickGlobalApply(void)
{
    uint8_t budget = 2u;

    if (!menu_globalApplyActive)
        return 0u;

    while (budget-- && menu_globalApplyIndex < NUM_PARAMS) {
        menu_parseGlobalParam(menu_globalApplyIndex,
                              parameter_values[menu_globalApplyIndex]);
        menu_globalApplyIndex++;
    }

    if (menu_globalApplyIndex >= NUM_PARAMS)
        menu_finishGlobalApply();

    return 1u;
}

static void menu_showStaleSettingsWarning(fs_stale_warning_source_t src)
{
    const char *line2 = (src == FS_STALE_WARNING_ALL) ? "check&save .all" : "check&save .glo";

    lcd_waitForIdle();
    lcd_clear();
    lcd_home();
    lcd_string("old settings");
    lcd_setcursor(0, 2);
    lcd_string(line2);
    lcd_waitForIdle();

    if (audioCodec_renderCount == 0u) {
        uint16_t t0 = time_sysTick;
        while ((uint16_t)(time_sysTick - t0) < MENU_STALE_WARNING_MS) { /* boot-only hold */ }
        return;
    }

    menu_storageBusy = 1u;
    menu_staleWarningActive = 1u;
    menu_staleWarningStart = time_sysTick;
}

/* Start loaded-sound apply.
**
** Before audio starts, keep the old synchronous behavior: boot loading can do
** all post-load work immediately because no DMA deadline exists yet. After
** audio has rendered at least once, only arm the chunked preset apply and let
** menu_tickSoundApply() finish the operation over foreground passes. */
static void menu_startSoundApply(uint8_t updateGap,
                                 uint8_t resetSave,
                                 uint8_t repaintAll,
                                 uint8_t startGlobals,
                                 uint8_t requestPattern,
                                 uint8_t applyPerformanceGlobals,
                                 uint8_t clearStorageBusy,
                                 uint8_t showStaleWarning,
                                 fs_stale_warning_source_t staleWarning)
{
    if (audioCodec_renderCount == 0u) {
        preset_sendDrumsetParameters();
        if (updateGap) {
            menu_TargetVoiceGapIndex = getModTargetGapIndex(
                parameter_values[PAR_TARGET_LFO1 + menu_activeVoice]);
        }
        if (applyPerformanceGlobals) {
            menu_parseGlobalParam(PAR_BPM, parameter_values[PAR_BPM]);
            menu_parseGlobalParam(PAR_BAR_RESET_MODE,
                                  parameter_values[PAR_BAR_RESET_MODE]);
        }
        if (startGlobals)
            menu_startGlobalApply(resetSave, repaintAll);
        if (requestPattern)
            /*
             * Loaded kit/all data can change pattern-level edit parameters.
             *
             * Old behavior: Menu asked frontPanelParser to send pattern values
             * back through the opcode path. New behavior: PatternData owns
             * pattern settings and copies the viewed pattern's values directly
             * into menu parameter_values before any repaint.
             *
             * Input: current viewed pattern from Menu. Output: pattern settings
             * such as change bar/next pattern are refreshed for display.
             *
             * Risk: this is the boot/no-audio synchronous path. The foreground
             * chunked path below has the same call in menu_finishSoundApply().
             */
            pat_applyPatternSettingsToMenu(menu_getViewedPattern());
        if (clearStorageBusy)
            menu_storageBusy = 0u;
        if (resetSave && !startGlobals)
            menu_resetSaveParameters();
        if (repaintAll && !startGlobals)
            menu_repaintAll();
        if (showStaleWarning)
            menu_showStaleSettingsWarning(staleWarning);
        return;
    }

    menu_soundApplyActive = 1u;
    menu_soundApplyUpdateGap = updateGap;
    menu_soundApplyResetSave = resetSave;
    menu_soundApplyRepaintAll = repaintAll;
    menu_soundApplyStartGlobals = startGlobals;
    menu_soundApplyRequestPattern = requestPattern;
    menu_soundApplyApplyPerformanceGlobals = applyPerformanceGlobals;
    menu_soundApplyClearStorageBusy = clearStorageBusy;
    menu_soundApplyShowStaleWarning = showStaleWarning;
    menu_soundApplyStaleWarning = staleWarning;
    preset_startDrumsetApply();
}

static void menu_finishSoundApply(void)
{
    menu_soundApplyActive = 0u;

    if (menu_soundApplyUpdateGap) {
        menu_TargetVoiceGapIndex = getModTargetGapIndex(
            parameter_values[PAR_TARGET_LFO1 + menu_activeVoice]);
    }

    if (menu_soundApplyApplyPerformanceGlobals) {
        menu_parseGlobalParam(PAR_BPM, parameter_values[PAR_BPM]);
        menu_parseGlobalParam(PAR_BAR_RESET_MODE,
                              parameter_values[PAR_BAR_RESET_MODE]);
    }

    if (menu_soundApplyStartGlobals)
        menu_startGlobalApply(menu_soundApplyResetSave,
                              menu_soundApplyRepaintAll);

    if (menu_soundApplyRequestPattern)
        /*
         * Completes the foreground/chunked equivalent of the boot-time pattern
         * refresh above. PatternData is called only after preset drumset apply
         * has finished so parameter_values does not mix old sound state with
         * newly loaded pattern settings.
         */
        pat_applyPatternSettingsToMenu(menu_getViewedPattern());

    if (menu_soundApplyClearStorageBusy)
        menu_storageBusy = 0u;

    /* When globals are started here, their existing finish path owns the
    ** reset/repaint flags. Non-container operations still do those follow-ups
    ** directly after the sound apply completes. */
    if (!menu_soundApplyStartGlobals) {
        if (menu_soundApplyResetSave)
            menu_resetSaveParameters();
        if (menu_soundApplyRepaintAll)
            menu_repaintAll();
    }

    if (menu_soundApplyShowStaleWarning) {
        if (menu_soundApplyStartGlobals &&
            menu_soundApplyStaleWarning == FS_STALE_WARNING_ALL) {
            menu_pendingAllStaleWarning = 1u;
        } else {
            menu_showStaleSettingsWarning(menu_soundApplyStaleWarning);
        }
    }

    menu_soundApplyUpdateGap = 0u;
    menu_soundApplyResetSave = 0u;
    menu_soundApplyRepaintAll = 0u;
    menu_soundApplyStartGlobals = 0u;
    menu_soundApplyRequestPattern = 0u;
    menu_soundApplyApplyPerformanceGlobals = 0u;
    menu_soundApplyClearStorageBusy = 0u;
    menu_soundApplyShowStaleWarning = 0u;
    menu_soundApplyStaleWarning = FS_STALE_WARNING_NONE;
}

/* Tick one bounded unit of loaded-sound apply.
**
** Returning 1 tells menu_pollPresetStatus() to stop for this pass, giving the
** main loop another audio_check_and_render() opportunity before any other
** preset completion work is handled. */
static uint8_t menu_tickSoundApply(void)
{
    if (!menu_soundApplyActive)
        return 0u;

    if (preset_tickDrumsetApply())
        return 1u;

    menu_finishSoundApply();
    return 1u;
}

static void menu_sendSoundParameter(uint16_t paramNr, uint8_t value)
{
    if (paramNr >= PAR_VEL_DEST_1 && paramNr <= PAR_VEL_DEST_6) {
        /*
         * Velocity modulation destinations are Preset voice routing state.
         *
         * The old front-panel opcode path only converted a menu parameter into
         * the same per-voice target write. Menu now resolves the mod target
         * table and calls Preset directly.
         *
         * Inputs: paramNr selects voice 0..5, value indexes modTargets[].
         * Output: Preset updates that voice's velocity modulation target.
         * Risk: value must already be a valid menu/modTargets index, matching
         * the historical menu contract.
         */
        uint8_t voiceNr = (uint8_t)(paramNr - PAR_VEL_DEST_1);
        uint16_t target = modTargets[value].param;
        preset_applyVelocityModTarget(voiceNr, target);
        return;
    }

    if (paramNr >= PAR_VOICE_LFO1 && paramNr <= PAR_VOICE_LFO6) {
        uint8_t lfoNr = (uint8_t)(paramNr - PAR_VOICE_LFO1);
        uint8_t newTargVal = getModTargetIdxFromGapIdx((uint8_t)(value - 1u),
                                                       menu_TargetVoiceGapIndex);
        parameter_values[PAR_TARGET_LFO1 + lfoNr] = newTargVal;
        value = newTargVal;
        paramNr = (uint16_t)(PAR_TARGET_LFO1 + lfoNr);
    }

    if (paramNr >= PAR_TARGET_LFO1 && paramNr <= PAR_TARGET_LFO6) {
        /*
         * LFO modulation destinations are Preset modulation routing state.
         *
         * Menu keeps the target-gap helper because it is display/menu mapping
         * logic. Preset receives the resolved parameter id and applies it to
         * the sound engine directly, replacing the parser CC bridge.
         *
         * Inputs: paramNr selects LFO voice index, value indexes modTargets[].
         * Output: Preset updates the LFO modulation target and Menu updates the
         * cached target-gap index used by the voice LFO selector.
         */
        uint8_t lfoNr = (uint8_t)(paramNr - PAR_TARGET_LFO1);
        uint16_t target = modTargets[value].param;
        menu_TargetVoiceGapIndex = getModTargetGapIndex(value);
        preset_applyLfoModTarget(lfoNr, target);
        return;
    }

    /*
     * Sound parameter writes now go straight to Preset.
     *
     * The front-panel parser previously split SET_P1/SET_P2-style opcodes by
     * parameter range. With one CPU, Menu can call the Preset API directly for
     * all sound parameters and leave true global parameters to
     * menu_parseGlobalParam().
     *
     * Input: paramNr/value are already the canonical menu parameter pair.
     * Output: Preset updates parameter_values/DSP state when requested by its
     * API. Risk: callers must keep using menu_parseGlobalParam for globals so
     * sequencer, MIDI, trigger, Pattern, and SOM side effects still occur.
     */
    if (paramNr < 128)
        preset_applySoundParameter(paramNr, value, 1);
    else if (paramNr < END_OF_SOUND_PARAMETERS)
        preset_applySoundParameter(paramNr, value, 1);
    else
        menu_parseGlobalParam(paramNr, value);
}

uint8_t menu_paramUsesMorphView(uint16_t paramNr)
{
    /*
     * Decide whether one menu parameter should read/write the morph endpoint.
     *
     * Why: SHIFT+VOICE mode is an overlay on ordinary voice pages, not a copy
     * of the page table. This guard keeps the alternate buffer restricted to
     * sound parameters that are actually being displayed on VOICE1..VOICE7.
     *
     * Inputs: paramNr is the canonical ParameterArray id from menuPages.
     * Output: nonzero means display/edit uses parameters2[] instead of
     * parameter_values[]. Clients: repaint and edit helpers below. Risk:
     * parameter 127 is still excluded because the active sound apply path cannot
     * encode it safely, and non-voice pages must never redirect to morph data.
     */
    if (!voiceModeShowMorph)
        return 0u;
    if (menu_activePage > VOICE7_PAGE)
        return 0u;
    if (paramNr == PAR_NONE || paramNr == 127u)
        return 0u;
    return (uint8_t)(paramNr < END_OF_SOUND_PARAMETERS);
}

uint8_t menu_getParameterDisplayValue(uint16_t paramNr)
{
    /*
     * Read one visible menu value from the active UI buffer.
     *
     * Inputs: canonical parameter id. Output: the value currently meant for
     * display: morph endpoint when menu_paramUsesMorphView() is true, otherwise
     * the normal parameter_values[] value. Clients are display formatting and
     * dtype clamp code that needs linked parameter values. Invalid ids read as
     * zero so a malformed page cell cannot index outside Menu-owned storage.
     */
    if (menu_paramUsesMorphView(paramNr))
        return parameters2[paramNr];
    if (paramNr < NUM_PARAMS)
        return parameter_values[paramNr];
    return 0u;
}

uint8_t *menu_getParameterEditPtr(uint16_t paramNr)
{
    /*
     * Return the mutable menu buffer slot for one editable parameter.
     *
     * Inputs: canonical parameter id. Output: pointer into parameters2[] for
     * morph voice mode, pointer into parameter_values[] otherwise, or NULL for
     * invalid ids. Clients: encoder and endless-pot edit paths. Confederates:
     * commits still go through menu_sendEditedParameter() so the write target
     * and side effects stay paired.
     */
    if (menu_paramUsesMorphView(paramNr))
        return &parameters2[paramNr];
    if (paramNr < NUM_PARAMS)
        return &parameter_values[paramNr];
    return 0;
}

static void menu_sendEditedParameter(uint16_t paramNr, uint8_t value)
{
    /*
     * Commit one edited parameter to the correct owner.
     *
     * Why: active-kit edits and morph-endpoint edits share the same voice-page
     * UI but have different side effects. Active edits must update DSP and may
     * record automation through Preset. Morph edits must update parameters2[]
     * only, then refresh the current morph interpolation without overwriting
     * parameter_values[].
     *
     * Inputs: paramNr/value after dtype clamping. Outputs: either normal active
     * sound/global application or morph endpoint storage plus a morph refresh.
     * Clients: menu_encoderChangeParameter() and menu_parseKnobDelta(). Risk:
     * calling preset_applySoundParameter() for morph edits would collapse the
     * active kit and morph endpoint into the same value, destroying morph range.
     */
    if (menu_paramUsesMorphView(paramNr)) {
        if (paramNr >= PAR_VOICE_LFO1 && paramNr <= PAR_VOICE_LFO6) {
            uint8_t lfoNr = (uint8_t)(paramNr - PAR_VOICE_LFO1);
            uint8_t newTargVal = getModTargetIdxFromGapIdx((uint8_t)(value - 1u),
                                                           menu_TargetVoiceGapIndex);
            parameters2[PAR_TARGET_LFO1 + lfoNr] = newTargVal;
        } else if (paramNr >= PAR_TARGET_LFO1 && paramNr <= PAR_TARGET_LFO6) {
            menu_TargetVoiceGapIndex = getModTargetGapIndex(value);
        }
        preset_morph(parameter_values[PAR_MORPH]);
        return;
    }

    menu_sendSoundParameter(paramNr, value);
}

static void menu_setStorageMessage(const char *message, const char *detail)
{
    /* Modal sample/loop installs suspend audio before flash writes. The OLED
    ** driver is still async, so explicitly wait for each message to drain or
    ** the next blocking erase/program stage can leave half-rendered text on
    ** screen until the storage operation returns. */
    lcd_waitForIdle();
    lcd_clear();
    lcd_home();
    lcd_string(message);
    lcd_setcursor(0, 2);
    lcd_string(detail);
    lcd_waitForIdle();
}

static void menu_loadSamplesModal(void)
{
    /* Single visible load command: install /samples first, then append /loops.
    ** This preserves the old user-facing two-step behavior and per-stage
    ** progress text, but avoids exposing a separate SampLoop menu option and
    ** reinitializes audio only once after all flash work is complete. */
    uint16_t t0;
    uint8_t samplesOk;
    uint8_t loopsOk;
    const char *failedMessage = "Sample upload";

    menu_beginStorageMessage("Sample upload");
    lcd_setcursor(0, 2);
    lcd_string("Suspending...");

    seq_setRunning(0);
    audioCodec_suspend();

    t0 = time_sysTick;
    while ((uint16_t)(time_sysTick - t0) < 1000u) {
        /* One-second suspend/resume hardware test window. */
    }

    menu_setStorageMessage("Sample upload", "Writing flash");
    samplesOk = filesystem_installSamplesBlocking();
    menu_setStorageMessage("Loop upload", "Writing flash");
    loopsOk = filesystem_installLoopsBlocking();
    if (samplesOk) {
        failedMessage = "Loop upload";
    }

    audioCodec_resume();
    menu_setNumSamples(sampleMemory_getNumSamples());

    menu_storageBusy = 0;
    menu_resetSaveParameters();
    if (!samplesOk || !loopsOk) {
        lcd_clear();
        lcd_home();
        lcd_string(failedMessage);
        lcd_setcursor(0, 2);
        lcd_string("Failed");
    } else {
        menu_repaintAll();
    }
}

/* -----------------------------------------------------------------------
** Parameter storage
** ----------------------------------------------------------------------- */
uint8_t parameter_values[NUM_PARAMS];
uint8_t parameters2[END_OF_SOUND_PARAMETERS];
/*
 * Voice-page morph endpoint overlay flag.
 *
 * Why: SHIFT+VOICE uses the existing VOICE page tables and edit mechanics but
 * must resolve sound parameter values against parameters2[] instead of the
 * active-kit parameter_values[]. Input/client: buttonHandler sets this through
 * menu_setVoiceModeShowMorph(). Output: repaint, encoder, and endless-pot edit
 * helpers choose the correct Menu-owned buffer. Risk: this is intentionally a
 * view/edit overlay only; non-voice pages and non-sound parameters must stay on
 * parameter_values[].
 */
uint8_t voiceModeShowMorph = 0;

/* -----------------------------------------------------------------------
** Dtype table — exact port of original, PROGMEM removed
** ----------------------------------------------------------------------- */
const enum Datatypes parameter_dtypes[NUM_PARAMS] = {
    /*PAR_NONE*/              DTYPE_0B127,
    /*PAR_OSC_WAVE_DRUM1*/    DTYPE_MENU|(MENU_WAVEFORM<<4),
    /*PAR_OSC_WAVE_DRUM2*/    DTYPE_MENU|(MENU_WAVEFORM<<4),
    /*PAR_OSC_WAVE_DRUM3*/    DTYPE_MENU|(MENU_WAVEFORM<<4),
    /*PAR_OSC_WAVE_SNARE*/    DTYPE_MENU|(MENU_WAVEFORM<<4),
    /*NRPN_DATA_ENTRY_COARSE*/DTYPE_0B127,
    /*PAR_WAVE1_CYM*/         DTYPE_MENU|(MENU_WAVEFORM<<4),
    /*PAR_WAVE1_HH*/          DTYPE_MENU|(MENU_WAVEFORM<<4),
    /*PAR_COARSE1*/           DTYPE_0B127,
    /*PAR_FINE1*/             DTYPE_PM63,
    /*PAR_COARSE2*/           DTYPE_0B127,
    /*PAR_FINE2*/             DTYPE_PM63,
    /*PAR_COARSE3*/           DTYPE_0B127,
    /*PAR_FINE3*/             DTYPE_PM63,
    /*PAR_COARSE4*/           DTYPE_0B127,
    /*PAR_FINE4*/             DTYPE_PM63,
    /*PAR_COARSE5*/           DTYPE_0B127,
    /*PAR_FINE5*/             DTYPE_PM63,
    /*PAR_COARSE6*/           DTYPE_0B127,
    /*PAR_FINE6*/             DTYPE_PM63,
    /*PAR_MOD_WAVE_DRUM1*/    DTYPE_MENU|(MENU_WAVEFORM<<4),
    /*PAR_MOD_WAVE_DRUM2*/    DTYPE_MENU|(MENU_WAVEFORM<<4),
    /*PAR_MOD_WAVE_DRUM3*/    DTYPE_MENU|(MENU_WAVEFORM<<4),
    /*PAR_WAVE2_CYM*/         DTYPE_MENU|(MENU_WAVEFORM<<4),
    /*PAR_WAVE3_CYM*/         DTYPE_MENU|(MENU_WAVEFORM<<4),
    /*PAR_WAVE2_HH*/          DTYPE_MENU|(MENU_WAVEFORM<<4),
    /*PAR_WAVE3_HH*/          DTYPE_MENU|(MENU_WAVEFORM<<4),
    /*PAR_NOISE_FREQ1*/       DTYPE_0B127,
    /*PAR_MIX1*/              DTYPE_0B127,
    /*PAR_MOD_OSC_F1_CYM*/   DTYPE_0B127,
    /*PAR_MOD_OSC_F2_CYM*/   DTYPE_0B127,
    /*PAR_MOD_OSC_GAIN1_CYM*/DTYPE_0B127,
    /*PAR_MOD_OSC_GAIN2_CYM*/DTYPE_0B127,
    /*PAR_MOD_OSC_F1*/        DTYPE_0B127,
    /*PAR_MOD_OSC_F2*/        DTYPE_0B127,
    /*PAR_MOD_OSC_GAIN1*/     DTYPE_0B127,
    /*PAR_MOD_OSC_GAIN2*/     DTYPE_0B127,
    /*PAR_FILTER_FREQ_1*/     DTYPE_0B127,
    /*PAR_FILTER_FREQ_2*/     DTYPE_0B127,
    /*PAR_FILTER_FREQ_3*/     DTYPE_0B127,
    /*PAR_FILTER_FREQ_4*/     DTYPE_0B127,
    /*PAR_FILTER_FREQ_5*/     DTYPE_0B127,
    /*PAR_FILTER_FREQ_6*/     DTYPE_0B127,
    /*PAR_RESO_1*/            DTYPE_0B127,
    /*PAR_RESO_2*/            DTYPE_0B127,
    /*PAR_RESO_3*/            DTYPE_0B127,
    /*PAR_RESO_4*/            DTYPE_0B127,
    /*PAR_RESO_5*/            DTYPE_0B127,
    /*PAR_RESO_6*/            DTYPE_0B127,
    /*PAR_VELOA1*/            DTYPE_0B127,
    /*PAR_VELOD1*/            DTYPE_0B127,
    /*PAR_VELOA2*/            DTYPE_0B127,
    /*PAR_VELOD2*/            DTYPE_0B127,
    /*PAR_VELOA3*/            DTYPE_0B127,
    /*PAR_VELOD3*/            DTYPE_0B127,
    /*PAR_VELOA4*/            DTYPE_0B127,
    /*PAR_VELOD4*/            DTYPE_0B127,
    /*PAR_VELOA5*/            DTYPE_0B127,
    /*PAR_VELOD5*/            DTYPE_0B127,
    /*PAR_VELOA6*/            DTYPE_0B127,
    /*PAR_VELOD6_CLOSED*/     DTYPE_0B127,
    /*PAR_VELOD6_OPEN*/       DTYPE_0B127,
    /*PAR_VOL_SLOPE1*/        DTYPE_0B127,
    /*PAR_VOL_SLOPE2*/        DTYPE_0B127,
    /*PAR_VOL_SLOPE3*/        DTYPE_0B127,
    /*PAR_VOL_SLOPE4*/        DTYPE_0B127,
    /*PAR_VOL_SLOPE5*/        DTYPE_0B127,
    /*PAR_VOL_SLOPE6*/        DTYPE_0B127,
    /*PAR_REPEAT4*/           DTYPE_0B127,
    /*PAR_REPEAT5*/           DTYPE_0B127,
    /*PAR_MOD_EG1*/           DTYPE_0B127,
    /*PAR_MOD_EG2*/           DTYPE_0B127,
    /*PAR_MOD_EG3*/           DTYPE_0B127,
    /*PAR_MOD_EG4*/           DTYPE_0B127,
    /*PAR_MODAMNT1*/          DTYPE_0B127,
    /*PAR_MODAMNT2*/          DTYPE_0B127,
    /*PAR_MODAMNT3*/          DTYPE_0B127,
    /*PAR_MODAMNT4*/          DTYPE_0B127,
    /*PAR_PITCH_SLOPE1*/      DTYPE_0B127,
    /*PAR_PITCH_SLOPE2*/      DTYPE_0B127,
    /*PAR_PITCH_SLOPE3*/      DTYPE_0B127,
    /*PAR_PITCH_SLOPE4*/      DTYPE_0B127,
    /*PAR_FMAMNT1*/           DTYPE_0B127,
    /*PAR_FM_FREQ1*/          DTYPE_0B127,
    /*PAR_FMAMNT2*/           DTYPE_0B127,
    /*PAR_FM_FREQ2*/          DTYPE_0B127,
    /*PAR_FMAMNT3*/           DTYPE_0B127,
    /*PAR_FM_FREQ3*/          DTYPE_0B127,
    /*PAR_VOL1*/              DTYPE_0B127,
    /*PAR_VOL2*/              DTYPE_0B127,
    /*PAR_VOL3*/              DTYPE_0B127,
    /*PAR_VOL4*/              DTYPE_0B127,
    /*PAR_VOL5*/              DTYPE_0B127,
    /*PAR_VOL6*/              DTYPE_0B127,
    /*PAR_PAN1*/              DTYPE_PM63,
    /*PAR_PAN2*/              DTYPE_PM63,
    /*PAR_PAN3*/              DTYPE_PM63,
    /*NRPN_FINE*/             DTYPE_0B127,
    /*NRPN_COARSE*/           DTYPE_0B127,
    /*PAR_PAN4*/              DTYPE_PM63,
    /*PAR_PAN5*/              DTYPE_PM63,
    /*PAR_PAN6*/              DTYPE_PM63,
    /*PAR_DRIVE1*/            DTYPE_0B127,
    /*PAR_DRIVE2*/            DTYPE_0B127,
    /*PAR_DRIVE3*/            DTYPE_0B127,
    /*PAR_SNARE_DISTORTION*/  DTYPE_0B127,
    /*PAR_CYMBAL_DISTORTION*/ DTYPE_0B127,
    /*PAR_HAT_DISTORTION*/    DTYPE_0B127,
    /*PAR_VOICE_DECIMATION1*/ DTYPE_0B127,
    /*PAR_VOICE_DECIMATION2*/ DTYPE_0B127,
    /*PAR_VOICE_DECIMATION3*/ DTYPE_0B127,
    /*PAR_VOICE_DECIMATION4*/ DTYPE_0B127,
    /*PAR_VOICE_DECIMATION5*/ DTYPE_0B127,
    /*PAR_VOICE_DECIMATION6*/ DTYPE_0B127,
    /*PAR_VOICE_DECIMATION_ALL*/DTYPE_0B127,
    /*PAR_FREQ_LFO1*/         DTYPE_0B127,
    /*PAR_FREQ_LFO2*/         DTYPE_0B127,
    /*PAR_FREQ_LFO3*/         DTYPE_0B127,
    /*PAR_FREQ_LFO4*/         DTYPE_0B127,
    /*PAR_FREQ_LFO5*/         DTYPE_0B127,
    /*PAR_FREQ_LFO6*/         DTYPE_0B127,
    /*PAR_AMOUNT_LFO1*/       DTYPE_0B127,
    /*PAR_AMOUNT_LFO2*/       DTYPE_0B127,
    /*PAR_AMOUNT_LFO3*/       DTYPE_0B127,
    /*PAR_AMOUNT_LFO4*/       DTYPE_0B127,
    /*PAR_AMOUNT_LFO5*/       DTYPE_0B127,
    /*PAR_AMOUNT_LFO6*/       DTYPE_0B127,
    /*PAR_BANK_CHANGE*/       DTYPE_0B127,
    /*PAR_FILTER_DRIVE_1*/    DTYPE_0B127,
    /*PAR_FILTER_DRIVE_2*/    DTYPE_0B127,
    /*PAR_FILTER_DRIVE_3*/    DTYPE_0B127,
    /*PAR_FILTER_DRIVE_4*/    DTYPE_0B127,
    /*PAR_FILTER_DRIVE_5*/    DTYPE_0B127,
    /*PAR_FILTER_DRIVE_6*/    DTYPE_0B127,
    /*PAR_MIX_MOD_1*/         DTYPE_MIX_FM,
    /*PAR_MIX_MOD_2*/         DTYPE_MIX_FM,
    /*PAR_MIX_MOD_3*/         DTYPE_MIX_FM,
    /*PAR_VOLUME_MOD_ON_OFF1*/DTYPE_ON_OFF,
    /*PAR_VOLUME_MOD_ON_OFF2*/DTYPE_ON_OFF,
    /*PAR_VOLUME_MOD_ON_OFF3*/DTYPE_ON_OFF,
    /*PAR_VOLUME_MOD_ON_OFF4*/DTYPE_ON_OFF,
    /*PAR_VOLUME_MOD_ON_OFF5*/DTYPE_ON_OFF,
    /*PAR_VOLUME_MOD_ON_OFF6*/DTYPE_ON_OFF,
    /*PAR_VELO_MOD_AMT_1*/    DTYPE_0B127,
    /*PAR_VELO_MOD_AMT_2*/    DTYPE_0B127,
    /*PAR_VELO_MOD_AMT_3*/    DTYPE_0B127,
    /*PAR_VELO_MOD_AMT_4*/    DTYPE_0B127,
    /*PAR_VELO_MOD_AMT_5*/    DTYPE_0B127,
    /*PAR_VELO_MOD_AMT_6*/    DTYPE_0B127,
    /*PAR_VEL_DEST_1*/        DTYPE_TARGET_SELECTION_VELO,
    /*PAR_VEL_DEST_2*/        DTYPE_TARGET_SELECTION_VELO,
    /*PAR_VEL_DEST_3*/        DTYPE_TARGET_SELECTION_VELO,
    /*PAR_VEL_DEST_4*/        DTYPE_TARGET_SELECTION_VELO,
    /*PAR_VEL_DEST_5*/        DTYPE_TARGET_SELECTION_VELO,
    /*PAR_VEL_DEST_6*/        DTYPE_TARGET_SELECTION_VELO,
    /*PAR_WAVE_LFO1*/         DTYPE_MENU|(MENU_LFO_WAVES<<4),
    /*PAR_WAVE_LFO2*/         DTYPE_MENU|(MENU_LFO_WAVES<<4),
    /*PAR_WAVE_LFO3*/         DTYPE_MENU|(MENU_LFO_WAVES<<4),
    /*PAR_WAVE_LFO4*/         DTYPE_MENU|(MENU_LFO_WAVES<<4),
    /*PAR_WAVE_LFO5*/         DTYPE_MENU|(MENU_LFO_WAVES<<4),
    /*PAR_WAVE_LFO6*/         DTYPE_MENU|(MENU_LFO_WAVES<<4),
    /*PAR_VOICE_LFO1*/        DTYPE_VOICE_LFO,
    /*PAR_VOICE_LFO2*/        DTYPE_VOICE_LFO,
    /*PAR_VOICE_LFO3*/        DTYPE_VOICE_LFO,
    /*PAR_VOICE_LFO4*/        DTYPE_VOICE_LFO,
    /*PAR_VOICE_LFO5*/        DTYPE_VOICE_LFO,
    /*PAR_VOICE_LFO6*/        DTYPE_VOICE_LFO,
    /*PAR_TARGET_LFO1*/       DTYPE_TARGET_SELECTION_LFO,
    /*PAR_TARGET_LFO2*/       DTYPE_TARGET_SELECTION_LFO,
    /*PAR_TARGET_LFO3*/       DTYPE_TARGET_SELECTION_LFO,
    /*PAR_TARGET_LFO4*/       DTYPE_TARGET_SELECTION_LFO,
    /*PAR_TARGET_LFO5*/       DTYPE_TARGET_SELECTION_LFO,
    /*PAR_TARGET_LFO6*/       DTYPE_TARGET_SELECTION_LFO,
    /*PAR_RETRIGGER_LFO1*/    DTYPE_MENU|(MENU_RETRIGGER<<4),
    /*PAR_RETRIGGER_LFO2*/    DTYPE_MENU|(MENU_RETRIGGER<<4),
    /*PAR_RETRIGGER_LFO3*/    DTYPE_MENU|(MENU_RETRIGGER<<4),
    /*PAR_RETRIGGER_LFO4*/    DTYPE_MENU|(MENU_RETRIGGER<<4),
    /*PAR_RETRIGGER_LFO5*/    DTYPE_MENU|(MENU_RETRIGGER<<4),
    /*PAR_RETRIGGER_LFO6*/    DTYPE_MENU|(MENU_RETRIGGER<<4),
    /*PAR_SYNC_LFO1*/         DTYPE_MENU|(MENU_SYNC_RATES<<4),
    /*PAR_SYNC_LFO2*/         DTYPE_MENU|(MENU_SYNC_RATES<<4),
    /*PAR_SYNC_LFO3*/         DTYPE_MENU|(MENU_SYNC_RATES<<4),
    /*PAR_SYNC_LFO4*/         DTYPE_MENU|(MENU_SYNC_RATES<<4),
    /*PAR_SYNC_LFO5*/         DTYPE_MENU|(MENU_SYNC_RATES<<4),
    /*PAR_SYNC_LFO6*/         DTYPE_MENU|(MENU_SYNC_RATES<<4),
    /*PAR_OFFSET_LFO1*/       DTYPE_0B127,
    /*PAR_OFFSET_LFO2*/       DTYPE_0B127,
    /*PAR_OFFSET_LFO3*/       DTYPE_0B127,
    /*PAR_OFFSET_LFO4*/       DTYPE_0B127,
    /*PAR_OFFSET_LFO5*/       DTYPE_0B127,
    /*PAR_OFFSET_LFO6*/       DTYPE_0B127,
    /*PAR_FILTER_TYPE_1*/     DTYPE_MENU|(MENU_FILTER<<4),
    /*PAR_FILTER_TYPE_2*/     DTYPE_MENU|(MENU_FILTER<<4),
    /*PAR_FILTER_TYPE_3*/     DTYPE_MENU|(MENU_FILTER<<4),
    /*PAR_FILTER_TYPE_4*/     DTYPE_MENU|(MENU_FILTER<<4),
    /*PAR_FILTER_TYPE_5*/     DTYPE_MENU|(MENU_FILTER<<4),
    /*PAR_FILTER_TYPE_6*/     DTYPE_MENU|(MENU_FILTER<<4),
    /*PAR_TRANS1_VOL*/        DTYPE_0B127,
    /*PAR_TRANS2_VOL*/        DTYPE_0B127,
    /*PAR_TRANS3_VOL*/        DTYPE_0B127,
    /*PAR_TRANS4_VOL*/        DTYPE_0B127,
    /*PAR_TRANS5_VOL*/        DTYPE_0B127,
    /*PAR_TRANS6_VOL*/        DTYPE_0B127,
    /*PAR_TRANS1_WAVE*/       DTYPE_MENU|(MENU_TRANS<<4),
    /*PAR_TRANS2_WAVE*/       DTYPE_MENU|(MENU_TRANS<<4),
    /*PAR_TRANS3_WAVE*/       DTYPE_MENU|(MENU_TRANS<<4),
    /*PAR_TRANS4_WAVE*/       DTYPE_MENU|(MENU_TRANS<<4),
    /*PAR_TRANS5_WAVE*/       DTYPE_MENU|(MENU_TRANS<<4),
    /*PAR_TRANS6_WAVE*/       DTYPE_MENU|(MENU_TRANS<<4),
    /*PAR_TRANS1_FREQ*/       DTYPE_0B127,
    /*PAR_TRANS2_FREQ*/       DTYPE_0B127,
    /*PAR_TRANS3_FREQ*/       DTYPE_0B127,
    /*PAR_TRANS4_FREQ*/       DTYPE_0B127,
    /*PAR_TRANS5_FREQ*/       DTYPE_0B127,
    /*PAR_TRANS6_FREQ*/       DTYPE_0B127,
    /*PAR_AUDIO_OUT1*/        DTYPE_MENU|(MENU_AUDIO_OUT<<4),
    /*PAR_AUDIO_OUT2*/        DTYPE_MENU|(MENU_AUDIO_OUT<<4),
    /*PAR_AUDIO_OUT3*/        DTYPE_MENU|(MENU_AUDIO_OUT<<4),
    /*PAR_AUDIO_OUT4*/        DTYPE_MENU|(MENU_AUDIO_OUT<<4),
    /*PAR_AUDIO_OUT5*/        DTYPE_MENU|(MENU_AUDIO_OUT<<4),
    /*PAR_AUDIO_OUT6*/        DTYPE_MENU|(MENU_AUDIO_OUT<<4),
    /*PAR_MIDI_NOTE1*/        DTYPE_NOTE_NAME,
    /*PAR_MIDI_NOTE2*/        DTYPE_NOTE_NAME,
    /*PAR_MIDI_NOTE3*/        DTYPE_NOTE_NAME,
    /*PAR_MIDI_NOTE4*/        DTYPE_NOTE_NAME,
    /*PAR_MIDI_NOTE5*/        DTYPE_NOTE_NAME,
    /*PAR_MIDI_NOTE6*/        DTYPE_NOTE_NAME,
    /*PAR_MIDI_NOTE7*/        DTYPE_NOTE_NAME,
    /*PAR_ROLL*/              DTYPE_MENU|(MENU_ROLL_RATES<<4),
    /*PAR_MORPH*/             DTYPE_0B255,
    /*PAR_ACTIVE_STEP*/       DTYPE_0B127,
    /*PAR_STEP_VOLUME*/       DTYPE_0B127,
    /*PAR_STEP_PROB*/         DTYPE_0B127,
    /*PAR_STEP_NOTE*/         DTYPE_NOTE_NAME,
    /*PAR_EUKLID_LENGTH*/     DTYPE_1B128,
    /*PAR_EUKLID_STEPS*/      DTYPE_1B128,
    /*PAR_EUKLID_ROTATION*/   DTYPE_0B127,
    /*PAR_AUTOM_TRACK*/       DTYPE_0b1,
    /*PAR_P1_DEST*/           DTYPE_AUTOM_TARGET,
    /*PAR_P2_DEST*/           DTYPE_AUTOM_TARGET,
    /*PAR_P1_VAL*/            DTYPE_0B127,
    /*PAR_P2_VAL*/            DTYPE_0B127,
    /*PAR_SHUFFLE*/           DTYPE_0B127,
    /*PAR_PATTERN_BEAT*/      DTYPE_0B127,
    /*PAR_PATTERN_NEXT*/      DTYPE_MENU|(MENU_NEXT_PATTERN<<4),
    /*PAR_TRACK_LENGTH*/      DTYPE_1B128,
    /*PAR_POS_X*/             DTYPE_0B127,
    /*PAR_POS_Y*/             DTYPE_0B127,
    /*PAR_FLUX*/              DTYPE_0B127,
    /*PAR_SOM_FREQ*/          DTYPE_0B127,
    /*PAR_TRACK_ROTATION*/    DTYPE_1B16,
    /*PAR_TRACK_SCALE*/       DTYPE_MENU|(MENU_TRACK_SCALE<<4),
    /*PAR_TRACK_MIDI_CHAN*/   DTYPE_1B16,
    /*PAR_TRACK_MIDI_NOTE*/   DTYPE_NOTE_NAME,
    /*PAR_BPM*/               DTYPE_0B255,
    /*PAR_MIDI_CHAN_1*/        DTYPE_1B16,
    /*PAR_MIDI_CHAN_2*/        DTYPE_1B16,
    /*PAR_MIDI_CHAN_3*/        DTYPE_1B16,
    /*PAR_MIDI_CHAN_4*/        DTYPE_1B16,
    /*PAR_MIDI_CHAN_5*/        DTYPE_1B16,
    /*PAR_MIDI_CHAN_6*/        DTYPE_1B16,
    /*PAR_EXT_SYNC*/          DTYPE_MENU|(MENU_EXT_SYNC<<4),
    /*PAR_FOLLOW*/            DTYPE_ON_OFF,
    /*PAR_QUANTISATION*/      DTYPE_MENU|(MENU_SEQ_QUANT<<4),
    /*PAR_SCREENSAVER_ON_OFF*/DTYPE_ON_OFF,
    /*PAR_MIDI_MODE*/         DTYPE_MENU|(MENU_MIDI<<4),
    /*PAR_MIDI_CHAN_7*/        DTYPE_1B16,
    /*PAR_MIDI_ROUTING*/      DTYPE_MENU|(MENU_MIDI_ROUTING<<4),
    /*PAR_MIDI_FILT_TX*/      DTYPE_MENU|(MENU_MIDI_FILTERING<<4),
    /*PAR_MIDI_FILT_RX*/      DTYPE_MENU|(MENU_MIDI_FILTERING<<4),
    /*PAR_PRESCALER_CLOCK_IN*/ DTYPE_MENU|(MENU_PPQ<<4),
    /*PAR_PRESCALER_CLOCK_OUT1*/DTYPE_MENU|(MENU_PPQ<<4),
    /*PAR_PRESCALER_CLOCK_OUT2*/DTYPE_MENU|(MENU_PPQ<<4),
    /*PAR_TRIG_GATE_MODE*/    DTYPE_ON_OFF,
    /*PAR_BAR_RESET_MODE*/    DTYPE_ON_OFF,
    /*PAR_MIDI_CHAN_GLOBAL*/   DTYPE_1B16,
    /*PAR_OSC_WAVE_INTERP*/   DTYPE_ON_OFF,
};

/* -----------------------------------------------------------------------
** valueNames — indexed by NamesEnum
** ----------------------------------------------------------------------- */
static const Name valueNames[NUM_NAMES] = {
    {SHORT_EMPTY,CAT_EMPTY,LONG_EMPTY},
    {SHORT_COARSE,CAT_OSC,LONG_COARSE},
    {SHORT_FINE,CAT_OSC,LONG_FINE},
    {SHORT_ATTACK,CAT_VELO_EG,LONG_ATTACK},
    {SHORT_DECAY,CAT_VELO_EG,LONG_DECAY},
    {SHORT_DECAY,CAT_PITCH_EG,LONG_DECAY},
    {SHORT_MOD,CAT_PITCH_MOD,LONG_AMOUNT},
    {SHORT_FM_AMNT,CAT_FM,LONG_AMOUNT},
    {SHORT_FREQ,CAT_FM,LONG_FREQ},
    {SHORT_DRIVE,CAT_VOICE,LONG_DRIVE},
    {SHORT_VOL,CAT_VOICE,LONG_VOL},
    {SHORT_PAN,CAT_VOICE,LONG_PAN},
    {SHORT_NOISE,CAT_NOISE,LONG_FREQ},
    {SHORT_MIX,CAT_NOISE_OSC,LONG_MIX},
    {SHORT_REPEAT,CAT_VELO_EG,LONG_REPEAT_CNT},
    {SHORT_FREQ,CAT_FILTER,LONG_FREQ},
    {SHORT_FIL_RESO,CAT_FILTER,LONG_RESONANCE},
    {SHORT_FIL_TYPE,CAT_FILTER,LONG_TYPE},
    {SHORT_MOD_OSC1_FREQ,CAT_MOD_OSC,LONG_FREQ1},
    {SHORT_MOD_OSC2_FREQ,CAT_MOD_OSC,LONG_FREQ2},
    {SHORT_MOD_OSC1_GAIN,CAT_MOD_OSC,LONG_GAIN1},
    {SHORT_MOD_OSC2_GAIN,CAT_MOD_OSC,LONG_GAIN2},
    {SHORT_FREQ,CAT_LFO,LONG_FREQ},
    {SHORT_MOD,CAT_LFO,LONG_AMOUNT},
    {SHORT_WAVE,CAT_LFO,LONG_WAVE},
    {SHORT_DEST,CAT_LFO,LONG_DEST_PARAM},
    {SHORT_SYNC,CAT_LFO,LONG_CLOCKSYNC},
    {SHORT_RETRIGGER,CAT_LFO,LONG_RETRIGGER},
    {SHORT_OFFSET,CAT_LFO,LONG_OFFSET},
    {SHORT_VOICE,CAT_LFO,LONG_DEST_VOICE},
    {SHORT_SLOPE,CAT_VELO_EG,LONG_SLOPE},
    {SHORT_DECAY1,CAT_VELO_EG,LONG_DECAY_CLOSED},
    {SHORT_DECAY2,CAT_VELO_EG,LONG_DECAY_OPEN},
    {SHORT_WAVE,CAT_OSC,LONG_WAVE},
    {SHORT_WAVE,CAT_TRANS,LONG_WAVE},
    {SHORT_VOL,CAT_TRANS,LONG_VOL},
    {SHORT_FREQ,CAT_TRANS,LONG_FREQ},
    {SHORT_EQ_GAIN,CAT_EQ,LONG_GAIN},
    {SHORT_EQ_FREQ,CAT_EQ,LONG_FREQ},
    {SHORT_ROLL,CAT_PATTERN,LONG_ROLLRATE},
    {SHORT_MORPH,CAT_SOUND,LONG_MORPH},
    {SHORT_VELOCITY,CAT_STEP,LONG_VELOCITY},
    {SHORT_NOTE,CAT_STEP,LONG_NOTE},
    {SHORT_PROBABILITY,CAT_STEP,LONG_PROBABILITY},
    {SHORT_STEP,CAT_STEP,LONG_NUMBER},
    {SHORT_LENGTH,CAT_PATTERN,LONG_LENGTH},
    {SHORT_STEP,CAT_EUKLID,LONG_STEPS},
    {SHORT_ROTATION,CAT_EUKLID,LONG_ROTATION},
    {SHORT_BPM,CAT_GLOBAL,LONG_TEMPO},
    {SHORT_SYNC,CAT_GLOBAL,LONG_EXTERNAL_SYNC},
    {SHORT_CHANNEL,CAT_VOICE,LONG_MIDI_CHANNEL},
    {SHORT_OUT,CAT_VOICE,LONG_AUDIO_OUT},
    {SHORT_SR,CAT_VOICE,LONG_SAMPLE_RATE},
    {SHORT_REPEAT,CAT_PATTERN,LONG_REPEAT_CNT},
    {SHORT_NXT,CAT_PATTERN,LONG_NEXT_PAT},
    {SHORT_MODE,CAT_OSC,LONG_MODE},
    {SHORT_VOL,CAT_OSC,LONG_VOL},
    {SHORT_DRIVE,CAT_FILTER,LONG_DRIVE},
    {SHORT_DEST,CAT_VELOCITY,LONG_DEST_PARAM},
    {SHORT_FM_AMNT,CAT_VELOCITY,LONG_AMOUNT},
    {SHORT_VOL,CAT_VELOCITY,LONG_VOLUME_MOD},
    {SHORT_FETCH,CAT_PARAMETER,LONG_FETCH},
    {SHORT_FOLLOW,CAT_SEQUENCER,LONG_FOLLOW},
    {SHORT_QUANT,CAT_SEQUENCER,LONG_QUANTISATION},
    {SHORT_TRACK,CAT_SEQUENCER,LONG_AUTOMATION_TRACK},
    {SHORT_DEST,CAT_SEQUENCER,LONG_AUTOMATION_DEST},
    {SHORT_VALUE,CAT_SEQUENCER,LONG_AUTOMATION_VAL},
    {SHORT_SHUFFLE,CAT_SEQUENCER,LONG_SHUFFLE},
    {SHORT_SCREEN_SAVER,CAT_GLOBAL,LONG_SCREENSAVER},
    {SHORT_EMPTY,CAT_EMPTY,LONG_EMPTY},   /* TEXT_SKIP */
    {SHORT_X,CAT_GENERATOR,LONG_X},
    {SHORT_Y,CAT_GENERATOR,LONG_Y},
    {SHORT_FLUX,CAT_GENERATOR,LONG_FLUX},
    {SHORT_FREQ,CAT_GENERATOR,LONG_FREQ},
    {SHORT_MIDI,CAT_MIDI,LONG_MODE},
    {SHORT_MIDI_ROUTING,CAT_MIDI,LONG_MIDI_ROUTING},
    {SHORT_MIDI_FILT_TX,CAT_MIDI,LONG_MIDI_FILT_TX},
    {SHORT_MIDI_FILT_RX,CAT_MIDI,LONG_MIDI_FILT_RX},
    {SHORT_TRIGGER_IN,CAT_TRIGGER,LONG_TRIGGER_IN},
    {SHORT_TRIGGER_OUT1,CAT_TRIGGER,LONG_TRIGGER_OUT1},
    {SHORT_TRIGGER_OUT2,CAT_TRIGGER,LONG_TRIGGER_OUT2},
    {SHORT_MODE,CAT_TRIGGER,LONG_TRIGGER_GATE_MODE},
    {SHORT_BAR_RESET_MODE,CAT_SEQUENCER,LONG_BAR_RESET_MODE},
    {SHORT_CHANNEL,CAT_MIDI,LONG_MIDI_CHANNEL},
    {SHORT_CPU_USE,CAT_GLOBAL,LONG_CPU_USE_TIME},
    {SHORT_OSC_INTERP,CAT_GLOBAL,LONG_OSC_INTERP},
    {SHORT_SCALE,CAT_PATTERN,LONG_SCALE},
};

/* -----------------------------------------------------------------------
** State — exact match to original
** ----------------------------------------------------------------------- */
static uint8_t menuIndex = 0;

uint8_t menu_numSamples = 0;
uint8_t menu_currentPresetNr[NUM_PRESET_LOCATIONS];
uint8_t menu_shownPattern = 0;
uint8_t menu_currentBar = 0;
uint8_t menu_muteModeActive = 0;

char currentDisplayBuffer[2][16];
char editDisplayBuffer[2][17];

uint8_t menu_activePage  = 0;
uint8_t menu_activeVoice = 0;
uint8_t menu_playedPattern = 0;
static uint8_t editModeActive = 0;
static uint8_t lastEncoderButton = 0;

#define MENU_CPU_USE_REFRESH_MS 500u
#define MENU_CPU_USE_AVG_SAMPLES 10u
static uint8_t  menu_cpuUseSamples[MENU_CPU_USE_AVG_SAMPLES];
static uint8_t  menu_cpuUseSampleIndex = 0;
static uint8_t  menu_cpuUseSampleCount = 0;
static uint16_t menu_cpuUseSampleSum = 0;
static uint8_t  menu_cpuUseAvgPercent = 0;
static uint16_t menu_cpuUseLastRefresh = 0;

static volatile struct {
    unsigned what  :3;
    unsigned state :4;
} menu_saveOptions;

/* -----------------------------------------------------------------------
** Forward declarations (static)
** ----------------------------------------------------------------------- */
static uint8_t has2ndPage(uint8_t menuPage);
static uint8_t checkScrollSign(uint8_t activePage, uint8_t activeParameter);
static void menu_repaintLoadSavePage(void);
static void menu_repaintGeneric(void);
void sendDisplayBuffer(void);
static void menu_moveToMenuItem(int8_t inc);
static void menu_encoderChangeParameter(int8_t inc);
static void menu_handleLoadSaveMenu(int8_t inc, uint8_t btnClicked);
static uint8_t menu_nameIsEmptySlot(void);
static uint8_t menu_isLoadSaveSelectionCurrent(void);
static void menu_requestCurrentLoadSaveSelection(uint8_t loadKitOnLoadPage);
static void menu_displayModTargetFull(uint8_t curParmVal);
static void menu_displayModTargetShort(uint8_t curParmVal, char *valueAsText, char inclVoice);
static uint8_t getMaxEntriesForMenu(uint8_t menuId);
static void getMenuItemNameForValue(uint8_t menuId, uint8_t curParmVal, char *buf);
static void menu_endlessPotMappingChanged(void);
static uint8_t menu_cpuUseWidgetVisible(void);
static void menu_formatCpuUsePercent3(char *buf);
static void menu_formatCpuUsePercent4(char *buf);


static uint8_t menu_nameIsEmptySlot(void)
{
    return (uint8_t)(memcmp(preset_currentName, "Empty   ", 8) == 0);
}

static uint8_t menu_isLoadSaveSelectionCurrent(void)
{
    uint8_t what = preset_getRequestType();

    if (what >= NUM_SAVE_TYPES)
        return 0;
    if (what >= SAVE_TYPE_GLO)
        return (uint8_t)(what == menu_saveOptions.what);
    return (uint8_t)(what == menu_saveOptions.what &&
                     preset_getRequestSlot() == menu_currentPresetNr[what]);
}

/* Request the filesystem action that corresponds to the current Load/Save page
 * selection.
 *
 * Why this exists: the Load/Save UI has two different browsing behaviors. Most
 * types only need an async name read while the encoder moves. Kit and morph kit
 * loading on the Load page historically loaded immediately when the slot
 * changed, and that behavior is preserved.
 *
 * Input: loadKitOnLoadPage is nonzero when an encoder movement on LOAD_PAGE
 * should initiate preset_loadDrumset() instead of just preset_loadName().
 * Outputs: starts a presetManager/filesystem request or sets
 * menu_deferSelectionRequest so menu_pollPresetStatus() can retry after the
 * single filesystem operation slot becomes free.
 *
 * Affiliates/clients: menu_handleLoadSaveMenu() calls this after type/slot
 * changes. preset_loadDrumset(slot, morph=0) is the path that initiates a
 * Phase 2 Kit/ directory load; preset_loadDrumset(slot, morph=1) still uses the
 * legacy morph .SND load path. preset_loadName() is the name-only client.
 */
static void menu_requestCurrentLoadSaveSelection(uint8_t loadKitOnLoadPage)
{
    uint8_t what = menu_saveOptions.what;
    uint8_t slot = (what < SAVE_TYPE_GLO) ? menu_currentPresetNr[what] : 0;

    menu_deferSelectionRequest = 0;
    menu_deferSelectionLoadKit = loadKitOnLoadPage;
    if (what >= SAVE_TYPE_GLO) {
        preset_loadName(0, what);
        return;
    }
    if (loadKitOnLoadPage &&
        menu_activePage == LOAD_PAGE &&
        (what == SAVE_TYPE_KIT || what == SAVE_TYPE_MORPH)) {
        if (!preset_loadDrumset(slot, (uint8_t)(what == SAVE_TYPE_MORPH)))
            menu_deferSelectionRequest = 1;
    } else {
        preset_loadName(slot, what);
        if (preset_getStatus() != PRESET_LOAD_IN_PROGRESS)
            menu_deferSelectionRequest = 1;
    }
}

/* -----------------------------------------------------------------------
** upr_three — uppercase 3 chars in place (exact port from original)
** ----------------------------------------------------------------------- */
void upr_three(char *buf)
{
    if (*buf >= 'a' && *buf <= 'z') (*buf) -= 32;
    buf++;
    if (*buf >= 'a' && *buf <= 'z') (*buf) -= 32;
    buf++;
    if (*buf >= 'a' && *buf <= 'z') (*buf) -= 32;
}

/* -----------------------------------------------------------------------
** Utility: number → string formatting (exact port)
** ----------------------------------------------------------------------- */
void numtostrpu(char *buf, uint8_t num, char pad)
{
    if (num > 99) { buf[0]=(char)('0'+(num/100)); num%=100; }
    else            buf[0]=pad;
    if (num > 9)  { buf[1]=(char)('0'+(num/10));  num%=10; }
    else if (buf[0]==pad) buf[1]=pad;
    else          buf[1]='0';
    buf[2]=(char)('0'+num);
}

void numtostrps(char *buf, int8_t num)
{
    if (num > 99)      { buf[0]=(char)('0'+(num/100)); num=(int8_t)(num%100); }
    else if (num < 0)  { buf[0]='-'; num=(int8_t)(-num); }
    else               buf[0]=' ';
    if (num > 9)       { buf[1]=(char)('0'+(num/10)); num=(int8_t)(num%10); }
    else if (buf[0]<'0') buf[1]=' ';
    else               buf[1]='0';
    buf[2]=(char)('0'+num);
}

void numtostru(char *buf, uint8_t num)
{
    uint8_t b=0;
    if (num>99) { buf[b++]=(char)('0'+(num/100)); num%=100; if(num<10) buf[b++]='0'; }
    if (num>9)  { buf[b++]=(char)('0'+(num/10));  num%=10; }
    buf[b]=(char)('0'+num);
}

static void menu_formatCpuUsePercent3(char *buf)
{
    uint8_t pct = menu_cpuUseAvgPercent;
    if (pct > 100u) pct = 100u;
    numtostrpu(buf, pct, ' ');
}

static void menu_formatCpuUsePercent4(char *buf)
{
    uint8_t pct = menu_cpuUseAvgPercent;
    if (pct > 100u) pct = 100u;
    numtostrpu(buf, pct, ' ');
    buf[3] = '%';
}

static void setNoteName(uint8_t num, char *buf)
{
    uint8_t n = num % 12;
    buf[0] = (char)((n>8 ? 60 : 67) + ((n+(n>4))/2));
    buf[1] = (n<5 && (n&1)) || (n>5 && !(n&1)) ? '#' : ' ';
    buf[2] = (char)(48+(num/12));
}

/* -----------------------------------------------------------------------
** Menu table lookup helpers
** ----------------------------------------------------------------------- */
static uint8_t getMaxEntriesForMenu(uint8_t menuId)
{
    switch (menuId) {
    case MENU_TRANS:         return (uint8_t)transientNames[0][0];
    case MENU_AUDIO_OUT:     return (uint8_t)outputNames[0][0];
    case MENU_FILTER:        return (uint8_t)filterTypes[0][0];
    case MENU_SYNC_RATES:    return (uint8_t)syncRateNames[0][0];
    case MENU_LFO_WAVES:     return (uint8_t)lfoWaveNames[0][0];
    case MENU_RETRIGGER:     return (uint8_t)retriggerNames[0][0];
    case MENU_SEQ_QUANT:     return (uint8_t)quantisationNames[0][0];
    case MENU_MIDI:          return (uint8_t)midiModes[0][0];
    case MENU_NEXT_PATTERN:  return (uint8_t)nextPatternNames[0][0];
    case MENU_WAVEFORM:      return (uint8_t)((uint8_t)waveformNames[0][0] + menu_numSamples);
    case MENU_ROLL_RATES:    return (uint8_t)rollRateNames[0][0];
    case MENU_MIDI_ROUTING:  return (uint8_t)midiRoutingNames[0][0];
    case MENU_MIDI_FILTERING:return (uint8_t)midiFilterNames[0][0];
    case MENU_PPQ:           return (uint8_t)ppqNames[0][0];
    case MENU_EXT_SYNC:      return (uint8_t)extSyncNames[0][0];
    case MENU_TRACK_SCALE:    return (uint8_t)trackScaleNames[0][0];
    default: return 0;
    }
}

static void menu_formatSampleShortName(uint8_t sampleIndex, char *buf)
{
    uint8_t display = (uint8_t)(sampleIndex + 1u);

    buf[0] = 's';
    if (display <= 99u) {
        buf[1] = (char)('0' + (display / 10u));
        buf[2] = (char)('0' + (display % 10u));
        return;
    }

    sampleIndex = (uint8_t)(display - 100u);
    buf[1] = (char)('A' + (sampleIndex / 10u));
    buf[2] = (char)('0' + (sampleIndex % 10u));
}

static void getMenuItemNameForValue(uint8_t menuId, uint8_t curParmVal, char *buf)
{
    const char *p = menuText_dash;
    switch (menuId) {
    case MENU_TRANS:          p = transientNames[curParmVal+1];     break;
    case MENU_AUDIO_OUT:      p = outputNames[curParmVal+1];        break;
    case MENU_FILTER:         p = filterTypes[curParmVal+1];        break;
    case MENU_WAVEFORM:
        if (curParmVal < (uint8_t)waveformNames[0][0]) {
            p = waveformNames[curParmVal+1]; break;
        } else {
            menu_formatSampleShortName((uint8_t)(curParmVal - waveformNames[0][0]), buf);
            return;
        }
    case MENU_SYNC_RATES:     p = syncRateNames[curParmVal+1];      break;
    case MENU_LFO_WAVES:      p = lfoWaveNames[curParmVal+1];       break;
    case MENU_RETRIGGER:      p = retriggerNames[curParmVal+1];     break;
    case MENU_SEQ_QUANT:      p = quantisationNames[curParmVal+1];  break;
    case MENU_MIDI:           p = midiModes[curParmVal+1];          break;
    case MENU_NEXT_PATTERN:   p = nextPatternNames[curParmVal+1];   break;
    case MENU_ROLL_RATES:     p = rollRateNames[curParmVal+1];      break;
    case MENU_MIDI_ROUTING:   p = midiRoutingNames[curParmVal+1];   break;
    case MENU_MIDI_FILTERING: p = midiFilterNames[curParmVal+1];    break;
    case MENU_PPQ:            p = ppqNames[curParmVal+1];           break;
    case MENU_EXT_SYNC:       p = extSyncNames[curParmVal+1];       break;
    case MENU_TRACK_SCALE:    p = trackScaleNames[curParmVal+1];    break;
    default: break;
    }
    buf[0]=p[0]; buf[1]=p[1]?p[1]:' '; buf[2]=p[2]?p[2]:' ';
}

/* -----------------------------------------------------------------------
** Mod target display helpers
** ----------------------------------------------------------------------- */
static void menu_displayModTargetFull(uint8_t curParmVal)
{
    if (modTargets[curParmVal].param == PAR_NONE) {
        memcpy(&editDisplayBuffer[1][0], menuText_off, 3);
        return;
    }
    uint8_t nameidx = modTargets[curParmVal].nameIdx;
    const char *cat = catNames[valueNames[nameidx].category];
    const char *lng = longNames[valueNames[nameidx].longName];
    uint8_t i;
    for (i=0;i<8&&cat[i];i++) editDisplayBuffer[1][i]=cat[i];
    for (i=0;i<8&&lng[i];i++) editDisplayBuffer[1][8+i]=lng[i];
}

static void menu_displayModTargetShort(uint8_t curParmVal, char *valueAsText, char inclVoice)
{
    if (modTargets[curParmVal].param == PAR_NONE) {
        memcpy(valueAsText, menuText_off, 3);
    } else {
        uint8_t off = 0;
        if (inclVoice)
            valueAsText[off++] = (char)(voiceFromModTargValue(curParmVal) + '0');
        const uint8_t name = modTargets[curParmVal].nameIdx;
        const char *sn = shortNames[valueNames[name].shortName];
        uint8_t k;
        for (k=off; k<3; k++) valueAsText[k] = sn[k-off] ? sn[k-off] : ' ';
    }
}

static void menu_displayCpuUseEdit(void)
{
    static const char title[] = "CPU use time";
    uint8_t i;

    memset(&editDisplayBuffer[0][0], ' ', 16);
    memset(&editDisplayBuffer[1][0], ' ', 16);

    for (i = 0; i < sizeof(title) - 1u && i < 16u; i++)
        editDisplayBuffer[0][i] = title[i];
    menu_formatCpuUsePercent4(&editDisplayBuffer[1][12]);
}

/* -----------------------------------------------------------------------
** has2ndPage / checkScrollSign
** ----------------------------------------------------------------------- */
static uint8_t has2ndPage(uint8_t menuPage)
{
    return (menuPages[menu_activePage][menuPage].top5 != TEXT_EMPTY) ? 1 : 0;
}

static uint8_t checkScrollSign(uint8_t activePage, uint8_t activeParameter)
{
    const uint8_t is2ndPage = (uint8_t)(activeParameter > 3);

    if (menu_activePage == MENU_MIDI_PAGE) {
        if (is2ndPage) {
            if ((activePage < NUM_SUB_PAGES-1) &&
                (menuPages[MENU_MIDI_PAGE][activePage+1].top1 != TEXT_EMPTY))
                return '*';
            else
                return '<';
        } else {
            if (has2ndPage(activePage)) {
                if (activePage > 0) return '*';
                else return '>';
            } else {
                if (activePage > 0) return '<';
                else return 0;
            }
        }
    }

    if (has2ndPage(activePage)) return is2ndPage ? '<' : '>';
    else return 0;
}

/* -----------------------------------------------------------------------
** sendDisplayBuffer — write changed characters to LCD.
**
** No hardware cursor used. Cursor state is rendered directly into
** editDisplayBuffer by the repaint functions (underscore char or brackets).
**
** Position tracking: after writing to (row, col) the LCD's internal address
** counter advances automatically. We omit lcd_setcursor when the next write
** position follows sequentially, keeping queue usage low.
** ----------------------------------------------------------------------- */
/* -----------------------------------------------------------------------
** Hardware cursor state.
** cur_want_*: desired state set each frame by menu_repaintLoadSavePage.
** cur_hw_on:  whether cursor was left on by the last sendDisplayBuffer.
**
** Invariant: cursor is ALWAYS turned off before character writes.
** This prevents the underline from following the DDRAM address counter
** across character write positions. Cursor is restored after writes.
**
** Cursor commands are only emitted when cursor is or was visible.
** When both cur_want_on and cur_hw_on are 0, no cursor commands are
** queued at all — the common case during normal menu navigation.
** ----------------------------------------------------------------------- */
static uint8_t cur_want_on  = 0;
static uint8_t cur_want_col = 0;
static uint8_t cur_want_row = 0;
static uint8_t cur_hw_on    = 0;

void sendDisplayBuffer(void)
{
    uint8_t i, j;
    uint8_t needed_ops = 0;

    if (cur_want_on || cur_hw_on)
        needed_ops++;

    for (i = 0; i < 2; i++) {
        for (j = 0; j < 16; j++) {
            char want = editDisplayBuffer[i][j];
            if (want == '\0') want = ' ';
            if (currentDisplayBuffer[i][j] != want)
                needed_ops = (uint8_t)(needed_ops + 2u);
        }
    }

    if (cur_want_on)
        needed_ops = (uint8_t)(needed_ops + 2u);

    /* The LCD queue drops individual bytes when full to protect audio. A
    ** menu frame is made of cursor/data pairs; dropping only one byte can
    ** desynchronise the LCD address and leave random-looking characters on
    ** screen. If the whole frame will not fit, skip it and retry from the
    ** main-loop poll path once TIM7 drains enough queue space. */
    if (needed_ops > lcd_queueFree()) {
        menu_lcdRefreshPending = 1;
        return;
    }

    /* If cursor is or was visible, turn it off before any character writes. */
    if (cur_want_on || cur_hw_on)
        lcd_turnOn(1, 0);

    for (i = 0; i < 2; i++) {
        for (j = 0; j < 16; j++) {
            char want = editDisplayBuffer[i][j];
            if (want == '\0') want = ' ';
            if (currentDisplayBuffer[i][j] == want) continue;

            /* Always emit setcursor before each data write. Verbatim
            ** original AVR behaviour. The position-tracking optimization
            ** that was here previously was removed because it loses sync
            ** if the LCD ever drops or mis-decodes a cursor-set command. */
            lcd_setcursor(j, (uint8_t)(i + 1));
            lcd_data((uint8_t)want);
            currentDisplayBuffer[i][j] = want;
        }
    }

    /* Restore cursor after all character writes. */
    if (cur_want_on) {
        lcd_setcursor(cur_want_col, cur_want_row);
        lcd_turnOn(1, 1);
        cur_hw_on = 1;
    } else {
        cur_hw_on = 0;
    }
    menu_lcdRefreshPending = 0;
}

/* -----------------------------------------------------------------------
** menu_repaintAll / menu_repaint
** ----------------------------------------------------------------------- */
void menu_repaintAll(void)
{
    memset(editDisplayBuffer,    ' ', 2*17);
    memset(currentDisplayBuffer, 127, 2*16);
    /* cur_hw_on stays as-is — sendDisplayBuffer will emit turnOn(off)
    ** before writes if it was on, then restore correctly after. */
    menu_repaint();
}

void menu_repaint(void)
{
    cur_want_on = 0; /* cleared here; save page sets it if needed */
    if (menu_activePage >= LOAD_PAGE && menu_activePage <= SAVE_PAGE)
        menu_repaintLoadSavePage();
    else
        menu_repaintGeneric();
    sendDisplayBuffer();
}

/* -----------------------------------------------------------------------
** menu_repaintLoadSavePage — close port of original
** ----------------------------------------------------------------------- */
/* Paint the two-line Load/Save page into editDisplayBuffer.
 *
 * Why this needed a Phase 2 change: the Load page now displays Kit/ directory
 * slots using filesystem_kitSlotName() from the scan cache, not the currently
 * loaded preset_currentName or a legacy .SND name header. The visible kit
 * number is one-based to match folders like 001 Name, while
 * menu_currentPresetNr[] remains zero-based for presetManager/filesystem calls.
 *
 * Inputs: menu_activePage, menu_saveOptions, menu_currentPresetNr[],
 * preset_currentName, editModeActive. Outputs: editDisplayBuffer plus optional
 * cursor intent for name editing. Clients: menu_repaint() whenever LOAD_PAGE or
 * SAVE_PAGE needs a redraw.
 */
static void menu_repaintLoadSavePage(void)
{
    /* Clear both rows so any indicator (>, [, ]) that moved from its
    ** previous position gets overwritten with a space and sent to the LCD.
    ** Without this, old indicators persist when using incremental repaint. */
    memset(&editDisplayBuffer[0][0], ' ', 16);
    memset(&editDisplayBuffer[1][0], ' ', 16);

    /* Top row */
    if (menu_activePage == SAVE_PAGE)
        memcpy(&editDisplayBuffer[0][0], "Save:", 5);
    else
        memcpy(&editDisplayBuffer[0][0], "Load:", 5);

    const char *toptxt = "Kit     ";
    switch (menu_saveOptions.what) {
    case SAVE_TYPE_KIT:         toptxt = "Kit     "; break;
    case SAVE_TYPE_PATTERN:     toptxt = "Pattern "; break;
    case SAVE_TYPE_MORPH:       toptxt = "MorphKit"; break;
    case SAVE_TYPE_GLO:         toptxt = "Settings"; break;
    case SAVE_TYPE_PERFORMANCE: toptxt = "Perform "; break;
    case SAVE_TYPE_ALL:         toptxt = "All     "; break;
    case SAVE_TYPE_SAMPLES:     toptxt = "Samples "; break;
    }
    memcpy(&editDisplayBuffer[0][6], toptxt, 8);

    if (menu_saveOptions.state == SAVE_STATE_EDIT_TYPE) {
        if (editModeActive) {
            editDisplayBuffer[0][5] = '[';
            editDisplayBuffer[0][14] = ']';
        } else {
            editDisplayBuffer[0][5] = ARROW_SIGN;
        }
    }

    /* Bottom row */
    if (menu_saveOptions.what < SAVE_TYPE_GLO) {
        uint8_t displayPreset = menu_currentPresetNr[menu_saveOptions.what];
        const char *displayName = preset_currentName;

        if (menu_activePage == LOAD_PAGE && menu_saveOptions.what == SAVE_TYPE_KIT) {
            displayPreset = (uint8_t)(displayPreset + 1u);
            displayName = filesystem_kitSlotName(menu_currentPresetNr[SAVE_TYPE_KIT]);
        }

        numtostrpu(&editDisplayBuffer[1][1], displayPreset, ' ');
        if (menu_saveOptions.state == SAVE_STATE_EDIT_PRESET_NR) {
            if (editModeActive) {
                editDisplayBuffer[1][0] = '[';
                editDisplayBuffer[1][4] = ']';
            } else {
                editDisplayBuffer[1][0] = ARROW_SIGN;
            }
        }
        memcpy(&editDisplayBuffer[1][5], displayName, 8);
    }

    if (menu_activePage == SAVE_PAGE) {
        if (menu_saveOptions.what < SAVE_TYPE_GLO) {
            if (menu_saveOptions.state >= SAVE_STATE_EDIT_NAME1 &&
                menu_saveOptions.state <= SAVE_STATE_EDIT_NAME8) {
                uint8_t ci = (uint8_t)(menu_saveOptions.state - SAVE_STATE_EDIT_NAME1);
                if (editModeActive) {
                    editDisplayBuffer[1][4+ci] = '[';
                    editDisplayBuffer[1][6+ci] = ']';
                } else {
                    /* Hardware underscore cursor beneath the name character.
                    ** Character in DDRAM is untouched — LCD controller draws
                    ** the underline in the 8th pixel row below the character. */
                    cur_want_on  = 1;
                    cur_want_col = (uint8_t)(5 + ci);
                    cur_want_row = 2;
                }
            }
        }
        memcpy(&editDisplayBuffer[1][14], menuText_ok, 2);
        if ((menu_saveOptions.state == SAVE_STATE_OK) ||
            (menu_saveOptions.what >= SAVE_TYPE_GLO && menu_saveOptions.state > SAVE_STATE_EDIT_TYPE)) {
            editDisplayBuffer[1][13] = ARROW_SIGN;
        }
    } else {
        /* Load page */
        if (menu_saveOptions.what != SAVE_TYPE_KIT && menu_saveOptions.what != SAVE_TYPE_MORPH) {
            memcpy(&editDisplayBuffer[1][14], menuText_ok, 2);
            if ((menu_saveOptions.state == SAVE_STATE_OK) ||
                (menu_saveOptions.what >= SAVE_TYPE_GLO && menu_saveOptions.state > SAVE_STATE_EDIT_TYPE)) {
                editDisplayBuffer[1][13] = ARROW_SIGN;
            }
        } else {
            editDisplayBuffer[1][14] = 0;
            editDisplayBuffer[1][15] = 0;
        }
    }
}

/* -----------------------------------------------------------------------
** menu_repaintGeneric — exact port of original
** ----------------------------------------------------------------------- */
static void menu_repaintGeneric(void)
{
    const uint8_t activeParameter = menuIndex & MASK_PARAMETER;
    const uint8_t activePage      = (uint8_t)((menuIndex & MASK_PAGE) >> PAGE_SHIFT);
    const Page *ap = &menuPages[menu_activePage][activePage];
    uint8_t curParmVal;
    char valueAsText[3];

    if (editModeActive) {
        uint8_t parName  = (&ap->top1)[activeParameter];
        uint16_t parNr   = (&ap->bot1)[activeParameter];

        if (parNr == PAR_RUNTIME_CPU_USE) {
            menu_displayCpuUseEdit();
            return;
        }

        curParmVal = menu_getParameterDisplayValue(parNr);

        memset(&editDisplayBuffer[0][0], ' ', 16);
        memset(&editDisplayBuffer[1][0], ' ', 16);

        if ((parameter_dtypes[parNr] & 0x0F) == DTYPE_AUTOM_TARGET) {
            memcpy(&editDisplayBuffer[0][0], "AutDst", 6);
            numtostru(&editDisplayBuffer[0][7], (uint8_t)(parNr - PAR_P1_DEST + 1));
            if (modTargets[curParmVal].param == PAR_NONE) {
                memcpy(&editDisplayBuffer[1][0], menuText_off, 3);
            } else {
                memcpy(&editDisplayBuffer[0][9], "Voice", 5);
                numtostru(&editDisplayBuffer[0][15], voiceFromModTargValue(curParmVal));
                menu_displayModTargetFull(curParmVal);
            }
        } else {
            /* Top row: category (0-7) + long name (8-15) */
            const char *cat = catNames[valueNames[parName].category];
            const char *lng = longNames[valueNames[parName].longName];
            uint8_t ci;
            for (ci=0; ci<8 && cat[ci]; ci++) editDisplayBuffer[0][ci] = cat[ci];
            for (ci=0; ci<8 && lng[ci]; ci++) editDisplayBuffer[0][8+ci] = lng[ci];

            /* Bottom row: value at col 13 */
            switch (parameter_dtypes[parNr] & 0x0F) {
            case DTYPE_TARGET_SELECTION_VELO:
            case DTYPE_TARGET_SELECTION_LFO:
                menu_displayModTargetFull(curParmVal);
                break;
            case DTYPE_MIX_FM:
                if (curParmVal==1) memcpy(&editDisplayBuffer[1][13], menuText_mix, 3);
                else               memcpy(&editDisplayBuffer[1][13], menuText_fm, 3);
                break;
            case DTYPE_ON_OFF:
                if (curParmVal==1) memcpy(&editDisplayBuffer[1][13], menuText_on, 3);
                else               memcpy(&editDisplayBuffer[1][13], menuText_off, 3);
                break;
            case DTYPE_MENU: {
                uint8_t menuId = (uint8_t)(parameter_dtypes[parNr] >> 4);
                if (menuId == MENU_WAVEFORM &&
                    curParmVal >= (uint8_t)waveformNames[0][0]) {
                    char sampleName[SAMPLE_DISPLAY_NAME_LEN + 1u];
                    uint8_t sampleIndex =
                        (uint8_t)(curParmVal - (uint8_t)waveformNames[0][0]);

                    sampleMemory_getDisplayName(sampleIndex, sampleName);
                    memcpy(&editDisplayBuffer[1][0], sampleName,
                           SAMPLE_DISPLAY_NAME_LEN);
                }
                getMenuItemNameForValue(menuId, curParmVal, &editDisplayBuffer[1][13]);
                break; }
            case DTYPE_PM63:
                numtostrps(&editDisplayBuffer[1][13], (int8_t)(curParmVal - 63));
                break;
            case DTYPE_NOTE_NAME:
                if (((parNr >= PAR_MIDI_NOTE1 && parNr <= PAR_MIDI_NOTE7) ||
                     parNr == PAR_TRACK_MIDI_NOTE) && curParmVal==0)
                    memcpy(&editDisplayBuffer[1][13], menuText_any, 3);
                else
                    setNoteName(curParmVal, &editDisplayBuffer[1][13]);
                break;
            case DTYPE_0b1:
                numtostrpu(&editDisplayBuffer[1][13], (uint8_t)(curParmVal+1), ' ');
                break;
            default:
            case DTYPE_0B127:
            case DTYPE_0B255:
            case DTYPE_1B16:
            case DTYPE_1B128:
            case DTYPE_0B15:
            case DTYPE_VOICE_LFO:
                numtostrpu(&editDisplayBuffer[1][13], curParmVal, ' ');
                break;
            }
        }
    } else {
        /* Normal mode: 4 columns of 3-char short names + values */
        const uint8_t is2ndPage = (uint8_t)((activeParameter > 3) ? 4 : 0);
        uint8_t i;

        /* Top row: 4 short names at cols 0,4,8,12 */
        memcpy(&editDisplayBuffer[0][0],
            shortNames[valueNames[(&ap->top1)[0+is2ndPage]].shortName], 3);
        memcpy(&editDisplayBuffer[0][4],
            shortNames[valueNames[(&ap->top1)[1+is2ndPage]].shortName], 3);
        memcpy(&editDisplayBuffer[0][8],
            shortNames[valueNames[(&ap->top1)[2+is2ndPage]].shortName], 3);
        memcpy(&editDisplayBuffer[0][12],
            shortNames[valueNames[(&ap->top1)[3+is2ndPage]].shortName], 3);

        /* Uppercase the active parameter's short name */
        upr_three(&editDisplayBuffer[0][(activeParameter % 4) * 4]);

        /* Scroll sign at col 15 */
        editDisplayBuffer[0][15] = (char)checkScrollSign(activePage, activeParameter);

        /* Bottom row: 4 values at cols 0,4,8,12 */
        for (i=0; i<4; i++) {
            const uint16_t parNr = (&ap->bot1)[i + is2ndPage];

            if (parNr == PAR_NONE) {
                memcpy(valueAsText, menuText_blank, 3);
            } else if (parNr == PAR_RUNTIME_CPU_USE) {
                menu_formatCpuUsePercent3(valueAsText);
            } else {
                curParmVal = menu_getParameterDisplayValue(parNr);
                switch (parameter_dtypes[parNr] & 0x0F) {
                case DTYPE_TARGET_SELECTION_VELO:
                case DTYPE_TARGET_SELECTION_LFO:
                    menu_displayModTargetShort(curParmVal, valueAsText, 0);
                    break;
                case DTYPE_AUTOM_TARGET:
                    menu_displayModTargetShort(curParmVal, valueAsText, 1);
                    break;
                case DTYPE_PM63:
                    numtostrps(valueAsText, (int8_t)(curParmVal - 63));
                    break;
                case DTYPE_NOTE_NAME:
                    if (((parNr >= PAR_MIDI_NOTE1 && parNr <= PAR_MIDI_NOTE7) ||
                         parNr == PAR_TRACK_MIDI_NOTE) && curParmVal==0)
                        memcpy(valueAsText, menuText_any, 3);
                    else
                        setNoteName(curParmVal, valueAsText);
                    break;
                case DTYPE_MIX_FM:
                    if (curParmVal==1) memcpy(valueAsText, menuText_mix, 3);
                    else               memcpy(valueAsText, menuText_fm, 3);
                    break;
                case DTYPE_ON_OFF:
                    if (curParmVal==1) memcpy(valueAsText, menuText_on, 3);
                    else               memcpy(valueAsText, menuText_off, 3);
                    break;
                case DTYPE_MENU: {
                    uint8_t menuId = (uint8_t)(parameter_dtypes[parNr] >> 4);
                    getMenuItemNameForValue(menuId, curParmVal, valueAsText);
                    break; }
                case DTYPE_0b1:
                    numtostrpu(valueAsText, (uint8_t)(curParmVal+1), ' ');
                    break;
                default:
                case DTYPE_0B127:
                case DTYPE_0B255:
                case DTYPE_1B16:
                case DTYPE_0B15:
                case DTYPE_VOICE_LFO:
                    numtostrpu(valueAsText, curParmVal, ' ');
                    break;
                }
            }
            memcpy(&editDisplayBuffer[1][4*i], valueAsText, 3);
        }
    }
}

/* -----------------------------------------------------------------------
** menu_encoderChangeParameter — exact port of original
** ----------------------------------------------------------------------- */
static void menu_encoderChangeParameter(int8_t inc)
{
    const uint8_t activeParameter = menuIndex & MASK_PARAMETER;
    const uint8_t activePage      = (uint8_t)((menuIndex & MASK_PAGE) >> PAGE_SHIFT);
    uint16_t paramNr = (&menuPages[menu_activePage][activePage].bot1)[activeParameter];

    if (paramNr == PAR_RUNTIME_CPU_USE || paramNr >= NUM_PARAMS)
        return;

    uint8_t *paramValue = menu_getParameterEditPtr(paramNr);

    if (!paramValue)
        return;

    /* Apply inc with proper saturation. The original AVR code only checked
    ** for the boundary value (255 for CW, >= -inc for CCW), which worked when
    ** |inc|=1 always. With encoder acceleration, |inc| can be up to 4 (or
    ** larger if you ever raise ACCEL_MAX_MULT), and the boundary checks must
    ** work for the full range. The dtype-specific clamps further down would
    ** catch overflow into the high range, but a uint8_t wrap from e.g. 250+10
    ** lands at 4, which the clamps don't see as out-of-range. */
    if (inc > 0) {
        int16_t sum = (int16_t)*paramValue + (int16_t)inc;
        *paramValue = (sum > 255) ? 255 : (uint8_t)sum;
    } else if (inc < 0) {
        int16_t sum = (int16_t)*paramValue + (int16_t)inc;
        *paramValue = (sum < 0) ? 0 : (uint8_t)sum;
    }

    switch (parameter_dtypes[paramNr] & 0x0F) {
    case DTYPE_TARGET_SELECTION_VELO: {
        uint8_t voiceNr = (uint8_t)(paramNr - PAR_VEL_DEST_1);
        if (*paramValue < modTargetVoiceOffsets[voiceNr].start) {
            *paramValue = (inc < 0) ? 0 : modTargetVoiceOffsets[voiceNr].start;
        } else if (*paramValue > modTargetVoiceOffsets[voiceNr].end) {
            *paramValue = modTargetVoiceOffsets[voiceNr].end;
        }
        /* _SEQUENCER_ADD_SPIKE_: target message send is centralized below via
        ** menu_sendSoundParameter(paramNr, *paramValue). */
        break; }
    case DTYPE_VOICE_LFO: {
        if (*paramValue < 1) *paramValue = 1;
        else if (*paramValue > 6) *paramValue = 6;
        /* _SEQUENCER_ADD_SPIKE_: linked TARGET_LFO update/sending is handled in
        ** menu_sendSoundParameter(), matching the AVR combined behavior. */
        break; }
    case DTYPE_TARGET_SELECTION_LFO:
    {
        uint8_t linkedVoice = menu_getParameterDisplayValue((uint16_t)(PAR_VOICE_LFO1+(paramNr - PAR_TARGET_LFO1)));
        uint8_t voiceNr;
        if (linkedVoice < 1u || linkedVoice > 6u)
            linkedVoice = 1u;
        voiceNr = (uint8_t)(linkedVoice - 1u);
        if(*paramValue < (modTargetVoiceOffsets[voiceNr].start)) {
            if(inc < 0) // going down, allow 0
                *paramValue=0;
            else // going up fix to start
                *paramValue = (modTargetVoiceOffsets[voiceNr].start);
        } else if (*paramValue > (modTargetVoiceOffsets[voiceNr].end)) {
            *paramValue = (modTargetVoiceOffsets[voiceNr].end);
        }
        menu_TargetVoiceGapIndex = getModTargetGapIndex(*paramValue);
        break;
    }
        // {
        // uint8_t voiceNr = (uint8_t)(parameter_values[PAR_VOICE_LFO1 + (paramNr - PAR_TARGET_LFO1)] - 1);
        // if (*paramValue < modTargetVoiceOffsets[voiceNr].start) {
        //     *paramValue = (inc < 0) ? 0 : modTargetVoiceOffsets[voiceNr].start;
        // } else if (*paramValue > modTargetVoiceOffsets[voiceNr].end) {
        //     *paramValue = modTargetVoiceOffsets[voiceNr].end;
        // }
        // menu_TargetVoiceGapIndex = getModTargetGapIndex(*paramValue);
        // break; 
        // }
    case DTYPE_AUTOM_TARGET: {
        uint8_t nmt = getNumModTargets();
        if (*paramValue >= nmt) *paramValue = (uint8_t)(nmt - 1);
        break; }
    case DTYPE_0B255: break;
    case DTYPE_1B16:
        if (*paramValue < 1) *paramValue = 1;
        else if (*paramValue > 16) *paramValue = 16;
        break;
    case DTYPE_1B128:
        if (*paramValue < 1) *paramValue = 1;
        else if (*paramValue > 128) *paramValue = 128;
        break;
    case DTYPE_0B15:
        if (*paramValue > 15) *paramValue = 15;
        break;
    case DTYPE_MIX_FM:
    case DTYPE_ON_OFF:
    case DTYPE_0b1:
        if (*paramValue > 1) *paramValue = 1;
        break;
    case DTYPE_MENU: {
        uint8_t menuId = (uint8_t)(parameter_dtypes[paramNr] >> 4);
        uint8_t numEntries = getMaxEntriesForMenu(menuId);
        if (*paramValue >= numEntries) *paramValue = (uint8_t)(numEntries - 1);
        break; }
    default:
    case DTYPE_0B127:
        if (*paramValue > 127) *paramValue = 127;
        break;
    }

    menu_sendEditedParameter(paramNr, *paramValue);
}

/* -----------------------------------------------------------------------
** menu_moveToMenuItem — exact port of original (goto checkvalid logic)
** ----------------------------------------------------------------------- */
static void menu_moveToMenuItem(int8_t inc)
{
    int8_t activeParameter = (int8_t)(menuIndex & MASK_PARAMETER);
    int8_t activePage      = (int8_t)(menuIndex >> PAGE_SHIFT);
    uint8_t needLock = 0;
    uint8_t param;
    uint8_t allowedSkips = 3;

    inc = (int8_t)(inc > 0 ? 1 : -1);

checkvalid:
    activeParameter = (int8_t)(activeParameter + inc);

    if (inc > 0) {
        if (activeParameter == 4) {
            needLock = 1;
        } else if (activeParameter == 8) {
            if (menu_activePage == MENU_MIDI_PAGE) {
                needLock = 1;
                activeParameter = 0;
                activePage++;
                if (activePage >= NUM_SUB_PAGES) activePage = 0;
            } else {
                return;
            }
        }
    } else {
        if (activeParameter == 3) {
            needLock = 1;
        } else if (activeParameter == -1) {
            if (menu_activePage == MENU_MIDI_PAGE) {
                needLock = 1;
                activeParameter = 7;
                activePage--;
                if (activePage < 0) activePage = NUM_SUB_PAGES - 1;
            } else {
                return;
            }
        }
    }

    param = (&menuPages[menu_activePage][(uint8_t)activePage].top1)[(uint8_t)activeParameter];
    if (param == TEXT_SKIP) {
        if (allowedSkips--) goto checkvalid;
        else return;
    }
    if (param == TEXT_EMPTY) return;

    menuIndex = (uint8_t)((activePage << PAGE_SHIFT) | activeParameter);
    (void)needLock; /* lockPotentiometerFetch stubbed */
}

/* -----------------------------------------------------------------------
** menu_handleLoadSaveMenu
**
** Handles encoder and OK-button input while the active page is Load or Save.
**
** Inputs: inc is the signed encoder delta; btnClicked is nonzero for the OK
** button edge. Outputs: updates menu_saveOptions/menu_currentPresetNr, starts
** preset save/load requests, edits preset_currentName on the Save page, and
** requests display/name refreshes through menu_requestCurrentLoadSaveSelection.
**
** Phase 2 kit-load affiliate: when the Load page is editing a kit slot, encoder
** movement calls menu_requestCurrentLoadSaveSelection(1), which calls
** preset_loadDrumset(slot, 0). That eventually starts filesystem's directory
** kit loader for Kit/NNN Name. The Save page still follows the existing save
** paths until the new save functions are implemented.
** ----------------------------------------------------------------------- */
static void menu_handleLoadSaveMenu(int8_t inc, uint8_t btnClicked)
{
    if (btnClicked) {
        if ((editModeActive && menu_saveOptions.state == SAVE_STATE_OK) ||
            (menu_saveOptions.what >= SAVE_TYPE_GLO && menu_saveOptions.state > SAVE_STATE_EDIT_TYPE)) {

            if (menu_activePage == SAVE_PAGE) {
                switch (menu_saveOptions.what) {
                case SAVE_TYPE_PATTERN: preset_savePattern(menu_currentPresetNr[SAVE_TYPE_PATTERN]); break;
                case SAVE_TYPE_KIT:     preset_saveDrumset(menu_currentPresetNr[SAVE_TYPE_KIT], 0); break;
                case SAVE_TYPE_MORPH:   preset_saveDrumset(menu_currentPresetNr[SAVE_TYPE_MORPH], 1); break;
                case SAVE_TYPE_GLO:     preset_saveGlobals(); break;
                case SAVE_TYPE_PERFORMANCE: preset_saveAll(menu_currentPresetNr[SAVE_TYPE_PERFORMANCE], 0); break;
                case SAVE_TYPE_ALL:     preset_saveAll(menu_currentPresetNr[SAVE_TYPE_ALL], 1); break;
                default: break;
                }
                menu_resetSaveParameters();
            } else {
                switch (menu_saveOptions.what) {
                case SAVE_TYPE_PATTERN:
                    if (menu_nameIsEmptySlot()) {
                        menu_resetSaveParameters();
                    } else if (preset_loadPattern(menu_currentPresetNr[SAVE_TYPE_PATTERN])) {
                        menu_beginStorageMessage("Loading pattern");
                    }
                    break;
                case SAVE_TYPE_PERFORMANCE:
                    if (menu_nameIsEmptySlot()) {
                        menu_resetSaveParameters();
                    } else if (preset_loadAll(menu_currentPresetNr[SAVE_TYPE_PERFORMANCE], 0)) {
                        menu_beginStorageMessage("Loading pattern");
                    }
                    break;
                case SAVE_TYPE_ALL:
                    if (menu_nameIsEmptySlot()) {
                        menu_resetSaveParameters();
                    } else if (preset_loadAll(menu_currentPresetNr[SAVE_TYPE_ALL], 1)) {
                        menu_beginStorageMessage("Loading pattern");
                    }
                    break;
                case SAVE_TYPE_GLO:
                    preset_loadGlobals();
                    /* menu_resetSaveParameters deferred to menu_pollPresetStatus() */
                    break;
                case SAVE_TYPE_SAMPLES:
                    menu_loadSamplesModal();
                    break;
                default: break;
                }
            }
        }
    }

    if (editModeActive) {
        switch (menu_saveOptions.state) {
        case SAVE_STATE_EDIT_TYPE:
            if (inc < 0) {
                if (menu_saveOptions.what != 0) menu_saveOptions.what--;
            } else if (inc > 0) {
                if (menu_saveOptions.what < SAVE_TYPE_SAMPLES) menu_saveOptions.what++;
            }
            menu_requestCurrentLoadSaveSelection(0);
            break;
        case SAVE_STATE_EDIT_PRESET_NR: {
            /* Saturating add - original `kit > 0` and `kit <= 125` checks
            ** assumed |inc|=1 and underflow/overflow on uint8_t wrap. Kits use
            ** 0..127 internally so they can display/open 001..128 folders;
            ** the older numbered file pools remain capped at 0..125 here. */
            int16_t maxPreset = (menu_saveOptions.what == SAVE_TYPE_KIT) ? 127 : 125;
            int16_t newPreset = (int16_t)menu_currentPresetNr[menu_saveOptions.what] + (int16_t)inc;
            if (newPreset < 0)        newPreset = 0;
            else if (newPreset > maxPreset) newPreset = maxPreset;
            menu_currentPresetNr[menu_saveOptions.what] = (uint8_t)newPreset;
            if (inc != 0) {
                if (menu_activePage == LOAD_PAGE) {
                    /* Kit load reads the name in its own phase 2 -
                    ** don't post a separate name load that would
                    ** block the kit load (filesystem is single-operation). */
                    menu_requestCurrentLoadSaveSelection(1);
                } else {
                    /* Save page - just load the name for display */
                    menu_requestCurrentLoadSaveSelection(0);
                }
            }
            break; }
        default:
            if (inc != 0 && menu_saveOptions.state >= SAVE_STATE_EDIT_NAME1 &&
                menu_saveOptions.state <= SAVE_STATE_EDIT_NAME8) {
                preset_currentName[menu_saveOptions.state - SAVE_STATE_EDIT_NAME1] =
                    (char)(preset_currentName[menu_saveOptions.state - SAVE_STATE_EDIT_NAME1] + inc);
            }
            break;
        }
    } else {
        if (inc < 0) {
            if (menu_saveOptions.state != SAVE_STATE_EDIT_TYPE) {
                menu_saveOptions.state--;
                if (menu_activePage == LOAD_PAGE && menu_saveOptions.state >= SAVE_STATE_EDIT_NAME1)
                    menu_saveOptions.state = SAVE_STATE_EDIT_PRESET_NR;
            }
        } else if (inc > 0) {
            if (menu_saveOptions.state < SAVE_STATE_OK) {
                menu_saveOptions.state++;
                if (menu_activePage == LOAD_PAGE) {
                    if (menu_saveOptions.state >= SAVE_STATE_EDIT_NAME1)
                        menu_saveOptions.state = SAVE_STATE_OK;
                    if (menu_saveOptions.state == SAVE_STATE_OK &&
                        (menu_saveOptions.what == SAVE_TYPE_KIT || menu_saveOptions.what == SAVE_TYPE_MORPH))
                        menu_saveOptions.state = SAVE_STATE_EDIT_PRESET_NR;
                }
            }
        }
    }
}

/* -----------------------------------------------------------------------
** menu_parseEncoder — exact port of original
** ----------------------------------------------------------------------- */
void menu_parseEncoder(int8_t inc, uint8_t button)
{
    uint8_t btnClicked = 0;
    uint8_t oldPage = menu_activePage;
    uint8_t oldIndex = menuIndex;

    if (menu_storageBusy) {
        lastEncoderButton = button;
        return;
    }

    if (button != lastEncoderButton) {
        btnClicked = button;
        lastEncoderButton = button;
    } else if (inc == 0) {
        return; /* nothing changed */
    }

    screensaver_touch();

    /* Clear mode owns both encoder turn and encoder click.
    ** - Turn: select clear target.
    ** - Click: execute selected clear operation.
    ** While active, do NOT toggle regular menu edit mode. */
    if (copyClear_isClearModeActive()) {
        if (btnClicked) {
            copyClear_executeClear();
            return;
        }

        if (inc != 0) {
            uint8_t target = copyClear_getClearTarget();
            if (inc < 0) {
                if (target != CLEAR_TRACK) {
                    target--;
                }
            } else if (inc > 0) {
                if (target != CLEAR_AUTOMATION2) {
                    target++;
                }
            }
            copyClear_setClearTarget(target);
        }
        return;
    }

    if (btnClicked)
        editModeActive = (uint8_t)(1 - editModeActive);

    /* NOTE: original AVR did inc *= -1 here to correct encoder orientation.
    ** Our TIM1 input capture is wired for the same physical CW=positive sense
    ** so the inversion is NOT needed. */

    if (menu_activePage == LOAD_PAGE || menu_activePage == SAVE_PAGE) {
        menu_handleLoadSaveMenu(inc, btnClicked);
    } else if (inc != 0) {
        if (editModeActive) {
            menu_encoderChangeParameter(inc);
        } else {
            menu_moveToMenuItem(inc);
        }
    }

    if (menu_storageBusy)
        return;

    /* Full refresh only when edit mode toggled (layout changes completely).
    ** Encoder movement uses incremental repaint — only sends changed chars,
    ** keeping queue traffic minimal during fast spinning. On the original
    ** AVR, repaintAll on every event was fine (blocking). On async TIM7 it
    ** floods the queue and causes display corruption. */
    if (btnClicked)
        menu_repaintAll();
    else
        menu_repaint();

    if ((oldPage != menu_activePage || oldIndex != menuIndex) &&
        menu_activePage != LOAD_PAGE && menu_activePage != SAVE_PAGE) {
        menu_endlessPotMappingChanged();
    }
}

/* -----------------------------------------------------------------------
** menu_parseKnobDelta — RV1-RV4 endless pots → parameter columns
**
** Sets menu_knobs_dirty=1 instead of calling menu_repaintAll directly.
** When all 4 knobs are turned simultaneously, the main loop's per-iter
** for-loop calls this function up to 4 times in a row; coalescing those
** to a single repaint keeps LCD queue pressure bounded. Caller (main
** loop) consumes the flag with menu_serviceKnobRepaint() after the
** RV1-4 read loop.
**
** Scope: this collapse applies ONLY to the RV1-4 read path. All other
** input handlers (buttons, main encoder, kit load) keep their direct
** menu_repaint*() calls — different code paths, different timing risks,
** out of scope for this minimal change.
** ----------------------------------------------------------------------- */
volatile uint8_t menu_knobs_dirty = 0;

static uint8_t menu_paramVisible(uint16_t paramNr)
{
    const uint8_t activePage = (uint8_t)((menuIndex & MASK_PAGE) >> PAGE_SHIFT);
    const uint8_t activeParameter = menuIndex & MASK_PARAMETER;

    if (menu_activePage >= LOAD_PAGE && menu_activePage <= SAVE_PAGE)
        return 0;

    if (editModeActive) {
        return (uint8_t)((&menuPages[menu_activePage][activePage].bot1)
                         [activeParameter] == paramNr);
    } else {
        const uint8_t is2ndPage = (uint8_t)((activeParameter > 3) ? 4 : 0);
        uint8_t i;

        for (i = 0; i < 4; i++) {
            if ((&menuPages[menu_activePage][activePage].bot1)[i + is2ndPage] == paramNr)
                return 1;
        }
    }

    return 0;
}

void menu_notifyExternalParamChanged(uint16_t paramNr)
{
    /* MIDI CC and future external control paths can update parameter_values[]
    ** without passing through menu_parseKnobDelta(). Only request a display
    ** refresh when the changed value is already visible; the main loop's
    ** existing LCD_LIMIT_TICKS_PER_REFRESH gate coalesces fast CC streams. */
    if (menu_paramVisible(paramNr))
        menu_knobs_dirty = 1;
}

static void menu_updateEndlessPotScales(void)
{
    uint8_t activePage = (uint8_t)((menuIndex & MASK_PAGE) >> PAGE_SHIFT);
    uint8_t activeParameter = menuIndex & MASK_PARAMETER;
    uint8_t is2ndPage = (uint8_t)((activeParameter > 3) ? 4 : 0);

    for (uint8_t knobNr = 0; knobNr < ENDLESS_POT_COUNT; knobNr++) {
        uint8_t useDouble = 0;
        if (menu_activePage != LOAD_PAGE && menu_activePage != SAVE_PAGE) {
            uint16_t parNr = (&menuPages[menu_activePage][activePage].bot1)[knobNr + is2ndPage];
            useDouble = (uint8_t)(parNr == PAR_MORPH);
        }
        endlessPots_setDouble(knobNr, useDouble);
    }
}

static void menu_endlessPotMappingChanged(void)
{
    menu_updateEndlessPotScales();
    endlessPots_snapshotAll();
}

void menu_parseKnobDelta(uint8_t knobNr, int8_t delta)
{
    if (menu_storageBusy) return;

    if (knobNr >= ENDLESS_POT_COUNT) return;
    if (menu_activePage == LOAD_PAGE || menu_activePage == SAVE_PAGE) return;

    const uint8_t activePage      = (uint8_t)((menuIndex & MASK_PAGE) >> PAGE_SHIFT);
    const uint8_t activeParameter = menuIndex & MASK_PARAMETER;
    const uint8_t is2ndPage       = (uint8_t)((activeParameter > 3) ? 4 : 0);
    const uint16_t parNr          = (&menuPages[menu_activePage][activePage].bot1)[knobNr + is2ndPage];

    if (parNr == PAR_NONE || parNr >= NUM_PARAMS) return;

    uint8_t *pv = menu_getParameterEditPtr(parNr);
    int16_t inc = delta;

    if (!pv) return;

    int16_t next = (int16_t)(*pv) + inc;
    if (next < 0) next = 0;
    else if (next > 255) next = 255;
    *pv = (uint8_t)next;

    /* clamp by dtype */
    switch (parameter_dtypes[parNr] & 0x0F) {
    case DTYPE_TARGET_SELECTION_VELO: {
        uint8_t voiceNr = (uint8_t)(parNr - PAR_VEL_DEST_1);
        if (*pv < modTargetVoiceOffsets[voiceNr].start) {
            *pv = (inc < 0) ? 0 : modTargetVoiceOffsets[voiceNr].start;
        } else if (*pv > modTargetVoiceOffsets[voiceNr].end) {
            *pv = modTargetVoiceOffsets[voiceNr].end;
        }
        break; }
    case DTYPE_VOICE_LFO:
        if (*pv < 1) *pv = 1;
        else if (*pv > 6) *pv = 6;
        break;
    case DTYPE_TARGET_SELECTION_LFO: {
        uint8_t linkedVoice = menu_getParameterDisplayValue((uint16_t)(PAR_VOICE_LFO1 + (parNr - PAR_TARGET_LFO1)));
        uint8_t voiceNr;
        if (linkedVoice < 1u || linkedVoice > 6u)
            linkedVoice = 1u;
        voiceNr = (uint8_t)(linkedVoice - 1u);
        if (*pv < modTargetVoiceOffsets[voiceNr].start) {
            *pv = (inc < 0) ? 0 : modTargetVoiceOffsets[voiceNr].start;
        } else if (*pv > modTargetVoiceOffsets[voiceNr].end) {
            *pv = modTargetVoiceOffsets[voiceNr].end;
        }
        break; }
    case DTYPE_0B255: break;
    case DTYPE_1B16: if (*pv<1)*pv=1; else if(*pv>16)*pv=16; break;
    case DTYPE_1B128: if (*pv<1)*pv=1; else if(*pv>128)*pv=128; break;
    case DTYPE_0B15: if (*pv>15)*pv=15; break;
    case DTYPE_MIX_FM: case DTYPE_ON_OFF: case DTYPE_0b1: if(*pv>1)*pv=1; break;
    case DTYPE_MENU: { uint8_t n=getMaxEntriesForMenu((uint8_t)(parameter_dtypes[parNr]>>4)); if(*pv>=n)*pv=(uint8_t)(n-1); break; }
    default: case DTYPE_0B127: if(*pv>127)*pv=127; break;
    }

    menu_sendEditedParameter(parNr, *pv);
    menu_knobs_dirty = 1;
}

/* Consume the knobs-dirty flag and repaint if set. Call once per main
** loop iteration AFTER the RV1-4 read loop. Multiple knob turns in the
** same iteration produce one repaint. */
void menu_serviceKnobRepaint(void)
{
    if (menu_storageBusy)
        return;

    if (menu_knobs_dirty) {
        menu_knobs_dirty = 0;
        // menu_repaintAll();
        menu_repaintGeneric();
        sendDisplayBuffer();
    }
}

static uint8_t menu_cpuUseWidgetVisible(void)
{
    uint8_t activePage = (uint8_t)((menuIndex & MASK_PAGE) >> PAGE_SHIFT);
    uint8_t activeParameter = menuIndex & MASK_PARAMETER;

    if (menu_activePage != MENU_MIDI_PAGE)
        return 0;
    if (editModeActive) {
        uint16_t parNr = (&menuPages[menu_activePage][activePage].bot1)[activeParameter];
        return (uint8_t)(parNr == PAR_RUNTIME_CPU_USE);
    }
    return (uint8_t)(activePage == 1u && activeParameter >= 4u);
}

void menu_serviceRuntimeWidgets(void)
{
    uint16_t now = time_sysTick;
    uint8_t sample;

    if (menu_storageBusy)
        return;

    if ((uint16_t)(now - menu_cpuUseLastRefresh) < MENU_CPU_USE_REFRESH_MS)
        return;
    menu_cpuUseLastRefresh = now;

    sample = audioCodec_getQueueFreePercent();
    if (menu_cpuUseSampleCount < MENU_CPU_USE_AVG_SAMPLES) {
        menu_cpuUseSamples[menu_cpuUseSampleIndex] = sample;
        menu_cpuUseSampleSum += sample;
        menu_cpuUseSampleCount++;
    } else {
        menu_cpuUseSampleSum -= menu_cpuUseSamples[menu_cpuUseSampleIndex];
        menu_cpuUseSamples[menu_cpuUseSampleIndex] = sample;
        menu_cpuUseSampleSum += sample;
    }

    menu_cpuUseSampleIndex++;
    if (menu_cpuUseSampleIndex >= MENU_CPU_USE_AVG_SAMPLES)
        menu_cpuUseSampleIndex = 0;

    if (menu_cpuUseSampleCount != 0u)
        menu_cpuUseAvgPercent = (uint8_t)((menu_cpuUseSampleSum + (menu_cpuUseSampleCount / 2u)) /
                                          menu_cpuUseSampleCount);

    if (!screensaver_isActive() && menu_cpuUseWidgetVisible())
        menu_repaint();
}

/* -----------------------------------------------------------------------
** menu_pollPresetStatus — call from main loop each iteration.
**
** Checks if an async SD operation completed (preset_getStatus() ==
** PRESET_UPDATE_READY) and applies the post-load/save work that
** previously happened synchronously in the old blocking code.
**
** This replicates the natural delay the original LXR AVR/UART design
** provided: parameters arrive after the SD transfer completes, not
** before the function returns.
** ----------------------------------------------------------------------- */
void menu_pollPresetStatus(void)
{
    uint8_t retrySelectionAfterAck = 0;
    uint8_t retrySelectionLoadKit = 0;

    /* Sound apply runs before globals because ALL/performance completion first
    ** installs loaded modulation routing, then starts any global apply that
    ** belongs to the container. Keep both paths one bounded unit per
    ** foreground pass. */
    if (menu_tickSoundApply())
        return;

    if (menu_tickGlobalApply())
        return;

    if (menu_pendingAllStaleWarning) {
        /* ALL loads first finish kit/pattern/global application and allow the
        ** "Loading pattern" UI to settle. Only then show the stale-globals
        ** warning for the requested 2 seconds. */
        menu_pendingAllStaleWarning = 0u;
        menu_showStaleSettingsWarning(FS_STALE_WARNING_ALL);
        return;
    }

    if (menu_staleWarningActive) {
        if ((uint16_t)(time_sysTick - menu_staleWarningStart) >= MENU_STALE_WARNING_MS) {
            menu_staleWarningActive = 0;
            menu_storageBusy = 0;
            menu_repaintAll();
        } else {
            return;
        }
    }

    if (preset_getStatus() != PRESET_UPDATE_READY) {
        if (!screensaver_isActive() &&
            menu_lcdRefreshPending && lcd_queueFree() >= 72u) {
            menu_repaint();
        }
        if (menu_deferSelectionRequest &&
            preset_getStatus() == PRESET_IDLE &&
            !menu_storageBusy &&
            (menu_activePage == LOAD_PAGE || menu_activePage == SAVE_PAGE)) {
            menu_requestCurrentLoadSaveSelection(menu_deferSelectionLoadKit);
        }
        return;
    }

    switch (preset_getCompletedOp()) {
    case PRESET_OP_KIT_LOAD:
    {
        if ((menu_activePage == LOAD_PAGE || menu_activePage == SAVE_PAGE) &&
            !menu_isLoadSaveSelectionCurrent()) {
            retrySelectionAfterAck = 1;
            retrySelectionLoadKit = 1;
            break;
        }

        /*
         * Directory Kit load completion is Scene-owned now.
         *
         * Why this differs from ALL/performance below: root Kit folders parse
         * instrument files into scene_t.kit, not parameter_values[]. Preset's
         * chunked apply cursor mirrors Scene supplementals and image values
         * into the legacy Menu/DSP affiliates as it works. Running the old
         * parameter_values[] normalizer here would inspect stale flat data
         * before the real loaded Scene data has been applied.
         */
        menu_startSoundApply(1u, 0u, 1u, 0u, 0u, 0u, 0u, 0u,
                             FS_STALE_WARNING_NONE);
        break;
    }

    case PRESET_OP_GLOBALS_LOAD:
    {
        fs_stale_warning_source_t stale_src = filesystem_takeStaleGlobalsWarning();
        menu_startGlobalApply((uint8_t)(menu_activePage == LOAD_PAGE || menu_activePage == SAVE_PAGE),
                              (uint8_t)(menu_activePage != LOAD_PAGE && menu_activePage != SAVE_PAGE));
        /* For boot/load glo.cfg, show the warning immediately after starting
        ** the deferred global apply. The warning is informational: filesystem.c
        ** already loaded a safe subset/default fallback before we get here. */
        if (stale_src == FS_STALE_WARNING_GLO)
            menu_showStaleSettingsWarning(stale_src);
        break;
    }

    case PRESET_OP_MORPH_LOAD:
        if ((menu_activePage == LOAD_PAGE || menu_activePage == SAVE_PAGE) &&
            !menu_isLoadSaveSelectionCurrent()) {
            retrySelectionAfterAck = 1;
            retrySelectionLoadKit = 1;
            break;
        }
        menu_normalizeSoundModTargets(parameters2);
        preset_morph(parameter_values[PAR_MORPH]);
        menu_repaintAll();
        break;

    case PRESET_OP_NAME_LOAD:
        if (menu_activePage == LOAD_PAGE || menu_activePage == SAVE_PAGE) {
            if (menu_isLoadSaveSelectionCurrent()) {
                preset_applyLoadedName();
                menu_repaintAll();
            } else {
                retrySelectionAfterAck = 1;
            }
        } else {
            preset_applyLoadedName();
        }
        break;

    case PRESET_OP_PATTERN_LOAD:
        pat_applyPatternSettingsToMenu(menu_getViewedPattern());
        menu_storageBusy = 0;
        menu_resetSaveParameters();
        menu_repaintAll();
        break;

    case PRESET_OP_ALL_LOAD:
    {
        fs_stale_warning_source_t stale_src = filesystem_takeStaleGlobalsWarning();
        menu_normalizeSoundModTargets(parameter_values);
        menu_startSoundApply(0u, 1u, 1u, 1u, 1u, 0u, 1u,
                             (uint8_t)(stale_src == FS_STALE_WARNING_ALL),
                             stale_src);
        break;
    }

    case PRESET_OP_PERFORMANCE_LOAD:
        menu_normalizeSoundModTargets(parameter_values);
        menu_startSoundApply(0u, 1u, 1u, 0u, 1u, 1u, 1u, 0u,
                             FS_STALE_WARNING_NONE);
        break;

    case PRESET_OP_KIT_SAVE:
    case PRESET_OP_MORPH_SAVE:
    case PRESET_OP_GLOBALS_SAVE:
    case PRESET_OP_PATTERN_SAVE:
    case PRESET_OP_ALL_SAVE:
    case PRESET_OP_PERFORMANCE_SAVE:
        /* Save complete - reset save UI */
        menu_resetSaveParameters();
        break;

    default:
        if (menu_storageBusy) {
            menu_storageBusy = 0;
            menu_resetSaveParameters();
        } else if (menu_activePage == LOAD_PAGE || menu_activePage == SAVE_PAGE) {
            if (menu_isLoadSaveSelectionCurrent()) {
                menu_repaintAll();
            } else {
                retrySelectionAfterAck = 1;
            }
        }
        break;
    }

    preset_ackStatus();
    if (retrySelectionAfterAck) {
        menu_requestCurrentLoadSaveSelection(retrySelectionLoadKit);
    }
}

/* -----------------------------------------------------------------------
** menu_switchSubPage — exact port of original toggle logic
** ----------------------------------------------------------------------- */
void menu_switchSubPage(uint8_t subPageNr)
{
    lockPotentiometerFetch();
    editModeActive = 0;

    uint8_t activeParameter = menuIndex & MASK_PARAMETER;
    uint8_t activePage      = (uint8_t)((menuIndex & MASK_PAGE) >> PAGE_SHIFT);

    if (subPageNr == activePage) {
        /* toggle between 1st and 2nd half of this sub-page */
        if (activeParameter < 4) {
            if (has2ndPage(activePage))
                activeParameter = 4;
            else if (menu_activePage == MENU_MIDI_PAGE) {
                activePage = 0;
                activeParameter = 0;
            }
        } else {
            if (menu_activePage == MENU_MIDI_PAGE) {
                if (activePage < NUM_SUB_PAGES-1 &&
                    menuPages[menu_activePage][activePage+1].top1 != TEXT_EMPTY)
                    activePage++;
                else
                    activePage = 0;
            }
            activeParameter = 0;
        }
    } else {
        activePage = subPageNr;
        if (activeParameter > 3 && has2ndPage(activePage))
            activeParameter = 4;
        else
            activeParameter = 0;
    }

    menuIndex = (uint8_t)((activePage << PAGE_SHIFT) | activeParameter);
    menu_endlessPotMappingChanged();
}

/* -----------------------------------------------------------------------
** menu_resetActiveParameter — exact port
** ----------------------------------------------------------------------- */
void menu_resetActiveParameter(void)
{
    uint8_t activePage = (uint8_t)((menuIndex & MASK_PAGE) >> PAGE_SHIFT);
    if (!has2ndPage(activePage))
        menuIndex &= (uint8_t)(~MASK_PARAMETER);
}

/* -----------------------------------------------------------------------
** menu_switchPage — exact port (sequencer/LED calls stubbed where needed)
** ----------------------------------------------------------------------- */
void menu_switchPage(uint8_t pageNr)
{
    if (menu_storageBusy) return;

    led_clearSequencerLeds();

    switch (pageNr) {
    case MENU_MIDI_PAGE: {
        uint8_t toggle = (menu_activePage == MENU_MIDI_PAGE);
        menu_setVoiceModeShowMorph(0u);
        menu_activePage = MENU_MIDI_PAGE;
        editModeActive = 0;
        lockPotentiometerFetch();
        if (toggle) {
            menu_switchSubPage(menu_getSubPage());
        } else {
            /* Entering globals from a different mode (voice / seq / etc).
            ** menuIndex's sub-page bits carry whatever sub-page the user was
            ** on in the previous mode. Voice sub-pages and global sub-pages
            ** are unrelated — landing on global sub-page 2 because the user
            ** was on voice AEG (sub-page 2) shows a non-existent page.
            ** Reset menuIndex so we always enter globals at sub-page 0. */
            menuIndex = 0;
        }
        break; }

    case PERFORMANCE_PAGE:
    case PATTERN_SETTINGS_PAGE:
    case SEQ_PAGE:
        menu_setVoiceModeShowMorph(0u);
        menu_activePage = pageNr;
        editModeActive = 0;
        lockPotentiometerFetch();
        if (pageNr == SEQ_PAGE) {
            /*
             * STEP mode's front page is a track-settings view.
             *
             * menu_switchPage() repaints before returning, so the PatternData
             * values must be copied into parameter_values here, not only in the
             * button-layer LED refresh that runs after the page switch. If this
             * sync happens after repaint, the LCD appears one voice-button
             * press behind even though menu_activeVoice is already correct.
             */
            pat_applyTrackSettingsToMenu(menu_getViewedPattern(), menu_getActiveVoice());
        }
        break;

    case LOAD_PAGE:
        menu_setVoiceModeShowMorph(0u);
        if (menu_activePage == LOAD_PAGE)
            menu_activePage = SAVE_PAGE;
        else
            menu_activePage = LOAD_PAGE;
        menu_resetSaveParameters();
        menu_requestCurrentLoadSaveSelection(0);
        break;

    default: /* voice pages */
        if (pageNr > VOICE7_PAGE)
            menu_setVoiceModeShowMorph(0u);
        menu_activePage = pageNr;
        if (pageNr < 7)
            menu_setActiveVoice(pageNr);
        editModeActive = 0;
        lockPotentiometerFetch();
        {
            /*
             * Entering a voice page also changes the active Pattern track view.
             *
             * Menu owns active voice/page state, ledHandler owns the physical
             * step/bar LEDs, and PatternData owns track edit parameters.
             * The old parser query has been replaced by those direct calls.
             *
             * Inputs: active voice from Menu and shown pattern from Menu.
             * Output: visible sequencer LEDs and track-scoped parameter_values
             * match the newly selected voice.
             */
            uint8_t trackNr = menu_getActiveVoice();
            uint8_t patternNr = menu_shownPattern;
            led_updatePatternTrack(trackNr, patternNr, buttonHandler_selectedStep);
            pat_applyTrackSettingsToMenu(patternNr, trackNr);
        }
        break;
    }

    /* LED updates */
    if (pageNr == PERFORMANCE_PAGE) {
        /* _SEQUENCER_ADD_SPIKE_: PERF page must show mute-state LEDs like AVR. */
        buttonHandler_showMuteLEDs();
    } else {
        led_setActiveVoiceLeds((uint8_t)(1 << menu_getActiveVoice()));
        menu_muteModeActive = 0;
    }

    menu_resetActiveParameter();
    menu_endlessPotMappingChanged();
    menu_repaintAll();
}

/* -----------------------------------------------------------------------
** menu_resetSaveParameters
** ----------------------------------------------------------------------- */
void menu_resetSaveParameters(void)
{
    if (menu_saveOptions.what >= SAVE_TYPE_GLO) {
        menu_saveOptions.state = SAVE_STATE_EDIT_TYPE;
        menu_saveOptions.what  = SAVE_TYPE_KIT;
    } else {
        editModeActive = 1;
        menu_saveOptions.state = SAVE_STATE_EDIT_PRESET_NR;
    }
    menu_repaintAll();
}

/* -----------------------------------------------------------------------
** menu_sendAllGlobals / menu_parseGlobalParam
** ----------------------------------------------------------------------- */
void menu_sendAllGlobals(void)
{
    uint16_t i;
    for (i = PAR_BEGINNING_OF_GLOBALS; i < NUM_PARAMS; i++)
        menu_parseGlobalParam(i, parameter_values[i]);
}

void menu_parseGlobalParam(uint16_t paramNr, uint8_t value)
{
    switch (paramNr) {
    case PAR_MIDI_CHAN_7:
        paramNr = (uint16_t)(paramNr - 5u);
        /* fall through */
    case PAR_MIDI_CHAN_1:
    case PAR_MIDI_CHAN_2:
    case PAR_MIDI_CHAN_3:
    case PAR_MIDI_CHAN_4:
    case PAR_MIDI_CHAN_5:
    case PAR_MIDI_CHAN_6:
    case PAR_MIDI_CHAN_GLOBAL:
    {
        /*
         * MIDI channel parameters are MIDI parser configuration.
         *
         * The menu value is stored as 1..16 for display; MidiParser stores
         * zero-based channels. Voice 7 is the historical global channel slot.
         * This direct call replaces a parser opcode that only forwarded the
         * same channel assignment.
         */
        uint8_t voice;
        uint8_t channel = (uint8_t)(value - 1u);
        if (paramNr == PAR_MIDI_CHAN_GLOBAL)
            voice = 7u;
        else
            voice = (uint8_t)(paramNr - PAR_MIDI_CHAN_1);
        midiParser_setChannel(voice, channel);
        break;
    }

    case PAR_POS_X:
        /*
         * SOM parameters now call the SOM generator directly. These are
         * generator controls under Core/Scene/Pattern, not sequencer transport
         * messages. Inputs are menu-scaled 0..127 values.
         */
        som_setX(value);
        break;

    case PAR_POS_Y:
        som_setY(value);
        break;

    case PAR_FLUX:
        /*
         * Flux is stored by SOM as 0.0..1.0, while the menu exposes 0..127.
         * Menu performs that UI-scale conversion at the direct call site.
         */
        som_setFlux((float)value / 127.0f);
        break;

    case PAR_SOM_FREQ:
        /*
         * SOM frequency is per active voice/track, so Menu supplies the active
         * voice and SOM owns the generator state mutation.
         */
        som_setFreq(value, menu_getActiveVoice());
        break;

    case PAR_TRACK_LENGTH:
        /*
         * Track length is per-pattern/per-track data.
         *
         * PatternData owns the mutation; Menu supplies the viewed pattern and
         * active voice because this edit targets what the user is looking at,
         * not necessarily the currently playing pattern.
         */
        pat_setTrackLength(menu_getViewedPattern(), menu_getActiveVoice(), value);
        break;

    case PAR_TRACK_SCALE:
        /*
         * Track scale is Pattern-owned runtime timing metadata. Menu supplies
         * the active track/viewed pattern because the STEP front page edits the
         * track currently in front of the user; PatternData stores the menu
         * index and Sequencer reads the exact ratio during playback scheduling.
         */
        pat_setTrackScale(menu_getViewedPattern(), menu_getActiveVoice(), value);
        break;

    case PAR_TRACK_MIDI_CHAN:
    {
        /*
         * Pattern-owned current-track MIDI channel for the STEP front page.
         *
         * PatternData is the storage owner for the track-settings page. The
         * legacy PAR_MIDI_CHAN_* parameter and MidiParser channel are mirrored
         * as a compatibility output so existing note routing keeps following
         * the currently edited track setting until Phase 4/Scene routing fully
         * replaces the old global MIDI channel ownership.
         */
        uint8_t track = menu_getActiveVoice();
        uint16_t realParam = (track == 6u)
            ? PAR_MIDI_CHAN_7
            : (uint16_t)(PAR_MIDI_CHAN_1 + track);
        scene_setTrackMidiChannel(menu_getViewedPattern(), track, value);
        parameter_values[realParam] = value;
        menu_parseGlobalParam(realParam, value);
        break;
    }

    case PAR_TRACK_MIDI_NOTE:
    {
        /*
         * Pattern-owned current-track MIDI note for the STEP front page.
         *
         * PatternData stores the value shown on the track settings page. The
         * legacy per-voice note override is mirrored through Preset so existing
         * MIDI playback/input code still sees the edited note while the broader
         * Scene/Instrument ownership is pending.
         */
        uint8_t track = menu_getActiveVoice();
        uint16_t realParam = (uint16_t)(PAR_MIDI_NOTE1 + track);
        scene_setTrackMidiNote(menu_getViewedPattern(), track, value);
        parameter_values[realParam] = value;
        preset_applySoundParameter(realParam, value, 1);
        break;
    }

    case PAR_SHUFFLE:
        /*
         * Shuffle is a per-track Pattern timing setting.
         *
         * Menu supplies the viewed pattern and active track because the STEP
         * front-page second half edits the track currently in front of the
         * user. PatternData stores the value and Sequencer queries it per track
         * when scheduling due events. This replaces the old global
         * seq_shuffle bridge.
         */
        pat_setTrackShuffle(menu_getViewedPattern(), menu_getActiveVoice(), value);
        break;

    case PAR_AUTOM_TRACK:
        /*
         * Active automation lane is Pattern edit context. Menu writes through
         * PatternData so automation recording/editing state is centralized
         * outside the front-panel parser.
         */
        pat_setActiveAutomationTrack(value);
        break;

    case PAR_P1_DEST:
    case PAR_P2_DEST:
    {
        /*
         * Automation destination edits mutate the selected Pattern step.
         *
         * Inputs: PAR_ACTIVE_STEP is the currently selected absolute step,
         * active voice/viewed pattern come from Menu, and P1/P2 chooses
         * automation lane 0/1. The menu value indexes modTargets[] and the
         * Pattern stores the resolved parameter id.
         *
         * Output: PatternData updates the step automation destination.
         * Risk: pat_setSelectedStep() preserves the old side effect where the
         * active step was also pushed through the opcode path before the lane
         * destination changed.
         */
        uint16_t tmp = modTargets[value].param;
        pat_setSelectedStep(parameter_values[PAR_ACTIVE_STEP]);
        pat_setStepAutomationDestination(menu_getViewedPattern(), menu_getActiveVoice(),
                                         parameter_values[PAR_ACTIVE_STEP],
                                         (uint8_t)(paramNr == PAR_P1_DEST ? 0u : 1u),
                                         tmp);
        break;
    }

    case PAR_P1_VAL:
        /*
         * Automation lane value for selected step, lane 0. PatternData owns the
         * step mutation; Menu only supplies current edit coordinates.
         */
        pat_setStepAutomationValue(menu_getViewedPattern(), menu_getActiveVoice(),
                                   parameter_values[PAR_ACTIVE_STEP], 0u, value);
        break;

    case PAR_P2_VAL:
        /*
         * Automation lane value for selected step, lane 1. Kept separate from
         * P1 for readability because the menu parameters are distinct.
         */
        pat_setStepAutomationValue(menu_getViewedPattern(), menu_getActiveVoice(),
                                   parameter_values[PAR_ACTIVE_STEP], 1u, value);
        break;

    case PAR_QUANTISATION:
        /*
         * Quantisation affects recording/playback timing, so it remains a
         * Sequencer setting and is called directly here.
         */
        seq_setQuantisation(value);
        break;

    case PAR_SCREENSAVER_ON_OFF:
        break;

    case PAR_BPM:
        /*
         * BPM is Sequencer transport timing. Menu clamps zero to one before
         * calling Sequencer because the sequencer cannot run at 0 BPM.
         */
        if (value == 0u) {
            value = 1u;
            parameter_values[PAR_BPM] = 1u;
        }
        seq_setBpm(value);
        break;

    case PAR_EXT_SYNC:
        /*
         * External sync source changes Sequencer clocking behavior. The menu
         * value is already the runtime enum value expected by Sequencer.
         */
        seq_setExtSyncSource(value);
        break;

    case PAR_MORPH:
        preset_morph(value);
        break;

    case PAR_VOICE_DECIMATION1:
    case PAR_VOICE_DECIMATION2:
    case PAR_VOICE_DECIMATION3:
    case PAR_VOICE_DECIMATION4:
    case PAR_VOICE_DECIMATION5:
    case PAR_VOICE_DECIMATION6:
    case PAR_VOICE_DECIMATION_ALL:
        /*
         * Voice decimation is a sound parameter even though it appears in the
         * global/performance area. Preset owns sound-engine application, so the
         * menu dispatches it directly there.
         */
        preset_applySoundParameter((uint16_t)(PAR_VOICE_DECIMATION1 +
                                  (paramNr - PAR_VOICE_DECIMATION1)),
                                  value, 1);
        break;

    case PAR_ROLL:
        /*
         * Roll rate controls Sequencer performance behavior. It is not Pattern
         * storage, so it remains a direct Sequencer call.
         */
        seq_setRollRate(value);
        break;

    case PAR_EUKLID_LENGTH:
    {
        /*
         * Euclidean generator length is generator state under Pattern.
         *
         * Length changes can constrain the effective number of steps, so this
         * preserves the old behavior by re-applying the current steps value
         * after length changes, then repainting the visible pattern track.
         *
         * Risk: LED repaint reads PatternData generated by Euklid. If Euklid
         * later stops writing directly to pattern steps, this repaint target
         * must follow that new interface.
         */
        uint8_t length = value;
        uint8_t pattern = menu_shownPattern;
        uint8_t steps;

        euklid_setLength(menu_getActiveVoice(), length);

        steps = parameter_values[PAR_EUKLID_STEPS];
        euklid_setSteps(menu_getActiveVoice(), steps, pattern);
        led_updatePatternTrack(menu_getActiveVoice(), pattern, buttonHandler_selectedStep);
        break;
    }

    case PAR_EUKLID_STEPS:
    {
        /*
         * Euclidean steps regenerate the active voice in the shown pattern.
         * Menu supplies active voice/shown pattern, EuklidGenerator mutates the
         * pattern, and ledHandler repaints the currently selected track view.
         */
        uint8_t steps = value;
        uint8_t pattern = menu_shownPattern;
        euklid_setSteps(menu_getActiveVoice(), steps, pattern);
        led_updatePatternTrack(menu_getActiveVoice(), pattern, buttonHandler_selectedStep);
        break;
    }

    case PAR_EUKLID_ROTATION:
    {
        /*
         * Euclidean rotation regenerates the active voice in the shown pattern.
         * This belongs in EuklidGenerator because it is generator logic, with
         * the LED repaint kept here as UI feedback.
         */
        uint8_t rotation = value;
        uint8_t pattern = menu_shownPattern;
        euklid_setRotation(menu_getActiveVoice(), rotation, pattern);
        led_updatePatternTrack(menu_getActiveVoice(), pattern, buttonHandler_selectedStep);
        break;
    }

    case PAR_PATTERN_BEAT:
        /*
         * Pattern change bar is pattern-level data. PatternData owns the value;
         * Menu supplies the viewed pattern because this is an edit operation.
         */
        pat_setPatternChangeBar(menu_getViewedPattern(), value);
        break;

    case PAR_PATTERN_NEXT:
        /*
         * Next-pattern setting is pattern-level data for the viewed pattern,
         * not an immediate Sequencer pattern switch.
         */
        pat_setPatternNext(menu_getViewedPattern(), value);
        break;

    case PAR_ACTIVE_STEP:
        /*
         * Active step changes reload step edit fields from PatternData.
         *
         * This does not mutate the step; it copies note/velocity/probability
         * and automation lane data for the selected step into menu parameters.
         */
        pat_applyStepToMenu(menu_getViewedPattern(), menu_getActiveVoice(), value);
        break;

    case PAR_STEP_PROB:
        /*
         * Step probability mutates the selected Pattern step for the active
         * voice/viewed pattern.
         */
        pat_setStepProbability(menu_getViewedPattern(), menu_getActiveVoice(),
                               parameter_values[PAR_ACTIVE_STEP], value);
        break;

    case PAR_STEP_NOTE:
        /*
         * Step note mutates the selected Pattern step. PatternData validates
         * pattern/track/step coordinates and owns the stored note value.
         */
        pat_setStepNote(menu_getViewedPattern(), menu_getActiveVoice(),
                        parameter_values[PAR_ACTIVE_STEP], value);
        break;

    case PAR_STEP_VOLUME:
        /*
         * Step volume/velocity mutates the selected Pattern step. This direct
         * call replaces the old sequencer-step opcode.
         */
        pat_setStepVolume(menu_getViewedPattern(), menu_getActiveVoice(),
                          parameter_values[PAR_ACTIVE_STEP], value);
        break;

    case PAR_MIDI_ROUTING:
        /*
         * MIDI routing/filter parameters are MidiParser configuration. Menu is
         * the UI source of the new value; MidiParser owns runtime behavior.
         */
        midiParser_setRouting(value);
        break;

    case PAR_MIDI_FILT_TX:
        /*
         * MIDI transmit filter is MidiParser runtime configuration. Direction
         * 1 is TX by historical API convention.
         */
        midiParser_setFilter(1u, value);
        break;

    case PAR_MIDI_FILT_RX:
        /*
         * MIDI receive filter is MidiParser runtime configuration. Direction
         * 0 is RX by historical API convention.
         */
        midiParser_setFilter(0u, value);
        break;

    case PAR_PRESCALER_CLOCK_IN:
        /*
         * Trigger-jack prescalers are hardware trigger configuration, so Menu
         * writes them directly to the trigger-jack owner instead of routing a
         * parser command through the removed bridge.
         */
        triggerJacks_setClockInputPpq(value);
        break;

    case PAR_PRESCALER_CLOCK_OUT1:
        /*
         * Clock output 1 prescaler belongs to triggerJacks hardware state.
         */
        triggerJacks_setClockOut1Ppq(value);
        break;

    case PAR_PRESCALER_CLOCK_OUT2:
        /*
         * Clock output 2 prescaler belongs to triggerJacks hardware state.
         */
        triggerJacks_setClockOut2Ppq(value);
        break;

    case PAR_TRIG_GATE_MODE:
        /*
         * Gate mode belongs to the trigger output driver. Menu passes through
         * the selected value directly because no front-panel protocol boundary
         * remains.
         */
        trigger_setGatemode(value);
        break;

    case PAR_BAR_RESET_MODE:
        /*
         * Bar-reset mode is still Sequencer-owned global state and was only
         * ever proxied by parser opcodes. Direct assignment preserves existing
         * storage while making the ownership explicit.
         */
        seq_resetBarOnPatternChange = value;
        break;

    case PAR_OSC_WAVE_INTERP:
        modNode_setWaveInterpEnabled((uint8_t)(value ? 1u : 0u));
        break;

    default:
        break;
    }
}

void menu_sendAllParameters(void)
{
    uint16_t i;
    for (i = 0; i < END_OF_SOUND_PARAMETERS; i++)
        menu_sendSoundParameter(i, parameter_values[i]);
}

/* -----------------------------------------------------------------------
** Accessors
** ----------------------------------------------------------------------- */
uint8_t menu_getActivePage(void)   { return menu_activePage; }
uint8_t menu_getActiveVoice(void)  { return menu_activeVoice; }
void    menu_setActiveVoice(uint8_t v) { menu_activeVoice = v; }
uint8_t menu_areMuteLedsShown(void){ return menu_muteModeActive; }
uint8_t menu_getSubPage(void)      { return (uint8_t)((menuIndex & MASK_PAGE) >> PAGE_SHIFT); }
void    menu_setNumSamples(uint8_t n) { menu_numSamples = n; }
void menu_setVoiceModeShowMorph(uint8_t onOff)
{
    /*
     * Set the voice-page morph endpoint overlay.
     *
     * Why: buttonHandler owns the SHIFT+VOICE gesture, but Menu owns the
     * parameter buffer used by repaint/edit code. Input onOff is boolean.
     * Output: voiceModeShowMorph is updated and the next repaint/edit resolves
     * voice-page sound parameters against the matching buffer. Confederates:
     * buttonHandler also owns the MODE1 blink feedback for this flag.
     */
    voiceModeShowMorph = (uint8_t)(onOff != 0u);
    menu_endlessPotMappingChanged();
}

void menu_showStepTrackSettingsFirstHalf(void)
{
    /*
     * Show the first half of STEP's track-settings front page.
     *
     * Why: selecting a different track in STEP mode should land on the primary
     * settings row (length, scale, MIDI channel, note), while re-pressing the
     * same track can toggle to the second half. Inputs: current Menu state.
     * Output: menuIndex selects SEQ_PAGE subpage 0 parameter 0 and endless-pot
     * snapshots update for the visible columns.
     */
    menuIndex = 0u;
    menu_endlessPotMappingChanged();
}

void menu_toggleStepTrackSettingsHalf(void)
{
    /*
     * Toggle STEP's track-settings front page between first and second halves.
     *
     * Why: repeated VOICE presses in STEP mode should act like a compact page
     * toggle without entering the per-step editor. Inputs: current menuIndex.
     * Output: activeParameter moves between 0 and 4 on SEQ_PAGE subpage 0, and
     * endless-pot mappings are refreshed for the newly visible half.
     */
    uint8_t activeParameter = menuIndex & MASK_PARAMETER;
    menuIndex = (activeParameter < 4u) ? 4u : 0u;
    menu_endlessPotMappingChanged();
}
void    menu_setShownPattern(uint8_t p)
{
    /*
     * Stores the pattern slot currently being viewed/edited by the UI.
     *
     * This is intentionally Menu state, not Sequencer next-pattern state:
     * pattern view can differ from playback during performance/follow modes.
     * PatternData callers use menu_getViewedPattern() when an edit should
     * target what the user is looking at.
     *
     * Input: p is the viewed pattern index supplied by button/menu navigation.
     * Output: during the single-pattern bridge, menu_shownPattern is pinned to 0. Risk: this setter does not repaint
     * LEDs or reload PatternData params; callers must do that explicitly.
     */
    menu_shownPattern = 0;
    (void)p;
}
uint8_t menu_getViewedPattern(void) { return menu_shownPattern; }

/* -----------------------------------------------------------------------
** menu_init — exact port
** ----------------------------------------------------------------------- */
void menu_init(void)
{
    paramToModTargetInit();

    memset(menu_currentPresetNr, 0, sizeof(menu_currentPresetNr));
    memset(parameter_values, 0, sizeof(parameter_values));
    memset(parameters2, 0, sizeof(parameters2));

    parameter_values[PAR_EUKLID_LENGTH] = 16;
    parameter_values[PAR_EUKLID_STEPS]  = 16;
    parameter_values[PAR_ROLL]          = 8;
    parameter_values[PAR_BPM]           = 120;
    parameter_values[PAR_TRACK_SCALE]   = TRACK_SCALE_OFF;
    parameter_values[PAR_OSC_WAVE_INTERP] = 0;
    /*
     * Wave interpolation is a sound-engine global that is applied immediately
     * at boot because there is no parser/global-apply pass between zeroed menu
     * defaults and the oscillator runtime state.
     */
    modNode_setWaveInterpEnabled(0);

    /* Switch to voice 1, light MODE1 and voice 1 LEDs */
    // led_setMode2(SELECT_MODE_VOICE);  /* MODE1 lit */
    // menu_switchPage(VOICE1_PAGE);
    menu_shownPattern = 0;
    menu_activeVoice = 0;
    // lcd_clear();
    // led_setActiveVoice(0);

    // menu_repaintAll();
}

void menu_start(void)
{
    led_setActiveVoice(0);
    led_setMode2(SELECT_MODE_VOICE);  /* MODE1 lit */
    menu_switchPage(VOICE1_PAGE);
    lcd_clear();
    menu_repaintAll();
}
