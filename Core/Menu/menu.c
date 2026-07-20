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
#include "BankData.h"
#include "SceneModTargets.h"
#include "config.h"
#include "EuklidGenerator.h"
#include "SomGenerator.h"
#include "triggerJacks.h"
#include "MidiParser.h"
#include "modulationNode.h"
#include <string.h>
#include <stdint.h>

#define MENU_COMPACT_SCREEN_CELLS 4u
#define MENU_STATIC_SUBPAGE_CELLS 8u
#define MENU_VOICE_SUBPAGE_SCREENS \
    (INSTRUMENT_MENU_PAGE_CELLS / MENU_COMPACT_SCREEN_CELLS)
#define MENU_VOICE_MIX_SUBPAGE 7u
/*
 * One VOICE mix Scene-setting screen is resolved against the currently
 * selected voice page.
 *
 * Inputs/outputs: menu_resolveSceneSettingCell() receives only a column-local
 * index 0..2 and derives the slot from menu_activePage via
 * menu_voicePageToSlot(). The math deliberately does not multiply by
 * INSTRUMENT_SLOT_COUNT: each VOICE page shows that voice's audio route, FX
 * send, and fader mode only, leaving column 4 blank. Changing this constant
 * changes the number of appended Scene-owned cells behind each mix sub-page.
 */
#define MENU_SCENE_SETTING_COUNT 3u
#define MENU_SCENE_SETTING_SCREENS \
    ((MENU_SCENE_SETTING_COUNT + MENU_COMPACT_SCREEN_CELLS - 1u) / \
     MENU_COMPACT_SCREEN_CELLS)

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
static uint8_t menu_instrumentApplyActive = 0u;
static uint8_t menu_instrumentApplySlot = 0u;
static uint8_t menu_staleWarningActive = 0;
static uint16_t menu_staleWarningStart = 0;
static uint8_t menu_pendingAllStaleWarning = 0;

#define MENU_STALE_WARNING_MS 2000u
#define MENU_TEST_RESULT_MS 2000u
#define MENU_INSTRUMENT_SAVE_NAME_LEN 8u

static char menu_testEditName[FS_TEST_NAME_MAX + 1u] = "test.bin";
static uint8_t menu_testResultActive = 0u;
static uint8_t menu_testResultError = 0u;
static uint16_t menu_testResultStart = 0u;
static fs_test_result_kind_t menu_testResultKind = FS_TEST_RESULT_BYTES_READY;
static uint8_t menu_testResultBytes[FS_TEST_RESULT_BYTES];
static char menu_testResultName[FS_TEST_NAME_MAX + 1u];

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
    (void)values;
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
            menu_TargetVoiceGapIndex = 0u;
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
        menu_TargetVoiceGapIndex = 0u;
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

static void menu_startInstrumentApply(uint8_t scene_index, uint8_t slot)
{
    /*
     * Start post-load commit/apply for one staged Instrument slot.
     *
     * Inputs: exact destination Scene and slot just loaded from Instrument/.
     * Output: Preset commits retained state and, for the audible Scene, arms the
     * six-slot runtime rebuild/target-rebind cursor. Menu holds input through
     * the completion boundary either way. This stays separate from
     * menu_startSoundApply() because Instrument browsing must not replace other
     * retained kit slots, Scene settings, globals, or patterns.
     */
    menu_instrumentApplyActive = 1u;
    menu_instrumentApplySlot = slot;
    preset_startInstrumentApply(scene_index, slot);
}

static void menu_startKitMorphApply(void)
{
    /*
     * Start post-load apply for a staged KitMrp operation.
     *
     * Output: Preset commits same-type morph endpoints and drains only the
     * Morph worker. Menu reuses the instrument-apply polling gate because this
     * is endpoint refresh work, not a normal Kit replacement that should run
     * Scene settings, routing, or target rebinds. KitMrp Load is live while
     * browsing Kit slots, so it intentionally does not reset SaveOptions to
     * the top row on completion.
     */
    menu_instrumentApplyActive = 1u;
    menu_instrumentApplySlot = 0u;
    menu_storageBusy = 1u;
    preset_startKitMorphApply();
}

static void menu_startInstrumentMorphApply(uint8_t scene_index, uint8_t slot)
{
    /*
     * Start post-load apply for one staged InstrumentMrp operation.
     *
     * Output: Preset copies only same-type morphable endpoint values into the
     * current destination slot and drains the Morph worker for the active
     * Scene. The browser remains open because slot identity and display name do
     * not change.
     */
    menu_instrumentApplyActive = 1u;
    menu_instrumentApplySlot = slot;
    menu_storageBusy = 1u;
    preset_startInstrumentMorphApply(scene_index, slot);
}

static uint8_t menu_tickInstrumentApply(void)
{
    /*
     * Advance one unit of Instrument Load post-apply work.
     *
     * Output: nonzero while Menu should return to the main loop. When Preset's
     * one-slot cursor finishes, storage input is unblocked and the current page
     * is repainted so the loaded descriptor/menu state remains visible in the
     * browser that launched it.
     */
    if (!menu_instrumentApplyActive)
        return 0u;
    if (preset_tickInstrumentApply())
        return 1u;
    menu_instrumentApplyActive = 0u;
    menu_storageBusy = 0u;
    (void)menu_instrumentApplySlot;
    menu_repaintAll();
    return 1u;
}

static void menu_sendSoundParameter(uint16_t paramNr, uint8_t value)
{
    /*
     * Route flat menu parameters by ownership, not by MIDI CC packing range.
     *
     * Instrument sound parameters no longer live in ParameterArray's flat
     * namespace; descriptor-backed voice edits use preset_setInstrumentParameter().
     * The remaining flat ids include PERF Morph/Roll, Pattern, generator, MIDI,
     * trigger, and globals. Many of those ids are numerically below 128, so the
     * old "param < 128 means sound CC" test bypassed menu_parseGlobalParam()
     * and lost owner-specific side effects such as preset_morph().
     *
     * Inputs: canonical flat ParameterArray id and clamped byte value. Outputs:
     * only true legacy sound ids use preset_applySoundParameter(); all non-sound
     * flat ids run their typed owner path in menu_parseGlobalParam(). This stays
     * as a separate helper because menu_sendEditedParameter() also owns the
     * morph-endpoint edit overlay; collapsing both decisions would make it too
     * easy for future clients to route descriptor endpoint edits through the
     * active runtime parameter path.
     */
    if (paramNr < END_OF_SOUND_PARAMETERS)
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
        /*
         * Refresh descriptor Morph from retained per-voice amounts.
         *
         * After per-voice Morph, preset_morph() is the overall bulk-set
         * operation and would overwrite all six voice amounts. Morph endpoint
         * edits only need a runtime rebuild, so use Preset's non-mutating
         * rebuild boundary here.
         */
        preset_rebuildMorph();
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
** Dtype table
** ----------------------------------------------------------------------- */
const enum Datatypes parameter_dtypes[NUM_PARAMS] = {
    [PAR_NONE] = DTYPE_0B127,
    [PAR_ROLL] = DTYPE_MENU|(MENU_ROLL_RATES<<4),
    [PAR_MORPH] = DTYPE_0B255,
    [PAR_VOICE1_MORPH] = DTYPE_0B255,
    [PAR_VOICE2_MORPH] = DTYPE_0B255,
    [PAR_VOICE3_MORPH] = DTYPE_0B255,
    [PAR_VOICE4_MORPH] = DTYPE_0B255,
    [PAR_VOICE5_MORPH] = DTYPE_0B255,
    [PAR_VOICE6_MORPH] = DTYPE_0B255,
    [PAR_VOICE_DECIMATION_ALL] = DTYPE_0B127,
    [PAR_ACTIVE_STEP] = DTYPE_0B127,
    [PAR_STEP_VOLUME] = DTYPE_0B127,
    [PAR_STEP_PROB] = DTYPE_0B127,
    [PAR_STEP_NOTE] = DTYPE_NOTE_NAME,
    [PAR_EUKLID_LENGTH] = DTYPE_1B128,
    [PAR_EUKLID_STEPS] = DTYPE_1B128,
    [PAR_EUKLID_ROTATION] = DTYPE_0B127,
    [PAR_AUTOM_TRACK] = DTYPE_0b1,
    [PAR_P1_DEST] = DTYPE_AUTOM_TARGET,
    [PAR_P2_DEST] = DTYPE_AUTOM_TARGET,
    [PAR_P1_VAL] = DTYPE_0B127,
    [PAR_P2_VAL] = DTYPE_0B127,
    [PAR_SHUFFLE] = DTYPE_0B127,
    [PAR_PATTERN_BEAT] = DTYPE_0B127,
    [PAR_PATTERN_NEXT] = DTYPE_MENU|(MENU_NEXT_PATTERN<<4),
    [PAR_TRACK_LENGTH] = DTYPE_1B128,
    [PAR_POS_X] = DTYPE_0B127,
    [PAR_POS_Y] = DTYPE_0B127,
    [PAR_FLUX] = DTYPE_0B127,
    [PAR_SOM_FREQ] = DTYPE_0B127,
    [PAR_TRACK_ROTATION] = DTYPE_1B16,
    [PAR_TRACK_SCALE] = DTYPE_MENU|(MENU_TRACK_SCALE<<4),
    [PAR_TRACK_MIDI_CHAN] = DTYPE_1B16,
    [PAR_TRACK_MIDI_NOTE] = DTYPE_NOTE_NAME,
    [PAR_BPM] = DTYPE_0B255,
    [PAR_MIDI_CHAN_1] = DTYPE_1B16,
    [PAR_MIDI_CHAN_2] = DTYPE_1B16,
    [PAR_MIDI_CHAN_3] = DTYPE_1B16,
    [PAR_MIDI_CHAN_4] = DTYPE_1B16,
    [PAR_MIDI_CHAN_5] = DTYPE_1B16,
    [PAR_MIDI_CHAN_6] = DTYPE_1B16,
    [PAR_EXT_SYNC] = DTYPE_MENU|(MENU_EXT_SYNC<<4),
    [PAR_FOLLOW] = DTYPE_ON_OFF,
    [PAR_QUANTISATION] = DTYPE_MENU|(MENU_SEQ_QUANT<<4),
    [PAR_SCREENSAVER_ON_OFF] = DTYPE_ON_OFF,
    [PAR_MIDI_MODE] = DTYPE_MENU|(MENU_MIDI<<4),
    [PAR_MIDI_CHAN_7] = DTYPE_1B16,
    [PAR_MIDI_ROUTING] = DTYPE_MENU|(MENU_MIDI_ROUTING<<4),
    [PAR_MIDI_FILT_TX] = DTYPE_MENU|(MENU_MIDI_FILTERING<<4),
    [PAR_MIDI_FILT_RX] = DTYPE_MENU|(MENU_MIDI_FILTERING<<4),
    [PAR_PRESCALER_CLOCK_IN] = DTYPE_MENU|(MENU_PPQ<<4),
    [PAR_PRESCALER_CLOCK_OUT1] = DTYPE_MENU|(MENU_PPQ<<4),
    [PAR_PRESCALER_CLOCK_OUT2] = DTYPE_MENU|(MENU_PPQ<<4),
    [PAR_TRIG_GATE_MODE] = DTYPE_ON_OFF,
    [PAR_BAR_RESET_MODE] = DTYPE_ON_OFF,
    [PAR_MIDI_CHAN_GLOBAL] = DTYPE_1B16,
    [PAR_OSC_WAVE_INTERP] = DTYPE_ON_OFF,
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
    {SHORT_VOICE1_MORPH,CAT_VOICE,LONG_VOICE1_MORPH},
    {SHORT_VOICE2_MORPH,CAT_VOICE,LONG_VOICE2_MORPH},
    {SHORT_VOICE3_MORPH,CAT_VOICE,LONG_VOICE3_MORPH},
    {SHORT_VOICE4_MORPH,CAT_VOICE,LONG_VOICE4_MORPH},
    {SHORT_VOICE5_MORPH,CAT_VOICE,LONG_VOICE5_MORPH},
    {SHORT_VOICE6_MORPH,CAT_VOICE,LONG_VOICE6_MORPH},
};

/* -----------------------------------------------------------------------
** State — exact match to original
** ----------------------------------------------------------------------- */
static uint8_t menuIndex = 0;
/*
 * Current four-parameter screen for each voice SELECT sub-page.
 *
 * Static menu pages still use menuIndex parameter bits 0..3 and 4..7 as the
 * old two-screen model. Voice pages can now expose up to sixteen instrument
 * descriptor cells per SELECT button, so the selected screen lives outside
 * menuIndex and is remembered per sub-page. It intentionally survives voice
 * page and mode changes; pressing a SELECT button while already in VOICE mode
 * is the only normal action that resets a different sub-page to screen 0 or
 * advances the same sub-page to the next screen.
 */
static uint8_t menu_voiceSubPageScreen[NUM_SUB_PAGES];

uint8_t menu_numSamples = 0;
uint16_t menu_currentPresetNr[NUM_PRESET_LOCATIONS];
uint8_t menu_shownPattern = 0;
uint8_t menu_currentBar = 0;
uint8_t menu_muteModeActive = 0;

char currentDisplayBuffer[2][16];
char editDisplayBuffer[2][17];

uint8_t menu_activePage  = 0;
uint8_t menu_activeVoice = 0;
uint8_t menu_playedPattern = 0;

static uint8_t menu_instrumentLoadActive = 0u;
static uint8_t menu_instrumentLoadSlot = 0u;
static instrument_type_t menu_instrumentLoadType = INSTRUMENT_TYPE_DRM;
static uint8_t menu_instrumentSaveMode = 0u;
static char menu_instrumentSaveName[MENU_INSTRUMENT_SAVE_NAME_LEN + 1u] =
    "Inst    ";
/*
 * Instrument Morph browser mode is derived from the destination slot type.
 *
 * baseType is captured from the currently loaded slot when Instrument Load is
 * entered or its destination Scene changes. morphMode is only UI/dispatch
 * state: it inserts the one legal "...Mrp" row after that base type and asks
 * Preset to copy the selected file into the resident morph endpoint instead of
 * replacing the slot.
 */
static instrument_type_t menu_instrumentLoadBaseType = INSTRUMENT_TYPE_DRM;
static uint8_t menu_instrumentLoadMorphMode = 0u;
/* Each active type keeps a 0..999 selection into the one shared name cache. */
static uint16_t menu_instrumentLoadIndex[INSTRUMENT_TYPE_UNKNOWN];
/*
 * Load-menu Scene and Instrument-source state.
 *
 * Kit Load retains a bit for every selected Scene so a future 16-Scene bank
 * can toggle targets before one Kit request. Instrument Load has exactly one
 * destination Scene and begins with the kit-member source; only lower-row
 * encoder movement promotes it to an Instrument/ pool item. Keeping the shown
 * source distinct from the selected type prevents changing type from silently
 * changing sound or replacing the useful "kit <name>" entry display.
 */
static uint16_t menu_kitLoadSceneMask = 0u;
static uint8_t menu_loadSaveSourceScene = 0u;
static uint8_t menu_instrumentLoadScene = 0u;
/*
 * Load:[Bank] child-Scene preview cache.
 *
 * Why: a root Bank slot can exist while containing any subset of Bank-local
 * Scene child folders 00..15. The root Bank browser scan cannot answer that,
 * so Menu requests a per-slot filesystem preview scan as the encoder moves.
 * Inputs are the highlighted Bank slot and filesystem_bankChildSceneMask()
 * after completion. Outputs are SEQ LEDs and the default Bank Load mask. The
 * `valid` flag prevents LEDs from showing stale child bits while a different
 * Bank slot is now selected or still being scanned.
 */
static uint16_t menu_bankLoadPreviewMask = 0u;
static uint16_t menu_bankLoadPreviewSlot = 0u;
static uint8_t menu_bankLoadPreviewValid = 0u;
typedef enum {
    MENU_INSTRUMENT_SOURCE_KIT = 0,
    MENU_INSTRUMENT_SOURCE_POOL
} menu_instrument_source_t;
static menu_instrument_source_t menu_instrumentLoadSource =
    MENU_INSTRUMENT_SOURCE_KIT;
static instrument_type_t menu_instrumentLoadShownType = INSTRUMENT_TYPE_DRM;
static uint16_t menu_instrumentLoadShownIndex = 0u;
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
static uint8_t menu_voiceAbsolutePositionSelectable(uint8_t subPage,
                                                    uint8_t position);
static uint8_t menu_voiceSubPageScreenExists(uint8_t subPage, uint8_t screen);
static uint8_t menu_voiceFirstSelectableColumn(uint8_t subPage,
                                               uint8_t screen);
static uint8_t checkScrollSign(uint8_t activePage, uint8_t activeParameter);
static void menu_repaintLoadSavePage(void);
static void menu_repaintGeneric(void);
void sendDisplayBuffer(void);
static void menu_moveToMenuItem(int8_t inc);
static void menu_encoderChangeParameter(int8_t inc);
static void menu_handleLoadSaveMenu(int8_t inc, uint8_t btnClicked);
static void menu_handleLoadSaveKnobDelta(uint8_t knobNr, int8_t delta);
static void menu_loadSaveClearInstrumentVoiceBlinks(void);
static void menu_bankLoadPreviewComplete(void);
static void menu_requestBankLoadPreview(uint16_t slot);
static void menu_requestLibraryIndexLoad(uint8_t what);
static void menu_instrumentLoadClampIndex(void);
static void menu_instrumentLoadRequestSelection(void);
static void menu_instrumentLoadRefreshBaseType(uint8_t preserve_selected_type);
static void menu_instrumentLoadCopyTypeLabel(char *dest);
static void menu_instrumentLoadStepType(int8_t inc);
static void menu_refreshLoadSceneLeds(void);
static uint8_t menu_paramIsMorphAmount(uint16_t paramNr);
static uint8_t menu_isLoadSaveSelectionCurrent(void);
static void menu_requestCurrentLoadSaveSelection(uint8_t loadKitOnLoadPage);
static void menu_displayModTargetFull(uint8_t curParmVal);
static void menu_displayModTargetShort(uint8_t curParmVal, char *valueAsText);
static uint8_t getMaxEntriesForMenu(uint8_t menuId);
static void getMenuItemNameForValue(uint8_t menuId, uint8_t curParmVal, char *buf);
static void menu_getLfoPolarityName(uint8_t value, char *buf);
static void menu_endlessPotMappingChanged(void);
static uint8_t menu_cpuUseWidgetVisible(void);
static void menu_formatCpuUsePercent3(char *buf);
static void menu_formatCpuUsePercent4(char *buf);
static void menu_formatPresetNumber3(char *dst, uint16_t zero_based_slot);
static void menu_sendEditedParameter(uint16_t paramNr, uint8_t value);
static void setNoteName(uint8_t num, char *buf);

typedef enum {
    MENU_CELL_EMPTY = 0,
    MENU_CELL_STATIC,
    MENU_CELL_INSTRUMENT,
    MENU_CELL_KIT_SETTING,
    MENU_CELL_SCENE_SETTING
} menu_cell_kind_t;

typedef enum {
    MENU_KIT_SETTING_NONE = 0,
    MENU_KIT_SETTING_SLOT6_TRACK7_AMP_DECAY
} menu_kit_setting_kind_t;

typedef enum {
    MENU_SCENE_SETTING_AUDIO_OUT = 0,
    MENU_SCENE_SETTING_FX_SEND_AMOUNT,
    MENU_SCENE_SETTING_FADER_SETTING
} menu_scene_setting_kind_t;

typedef struct {
    menu_cell_kind_t kind;
    uint8_t text_id;
    uint16_t static_param;
    uint8_t kit_setting;
    uint8_t scene_setting;
    uint8_t slot;
    uint8_t descriptor_index;
    const ParamDescriptor *descriptor;
} menu_cell_t;

typedef struct {
    uint8_t scene_index;
    uint8_t source_slot;
    uint8_t target_pair;
    instrument_binding_kind_t target_voice_kind;
    instrument_binding_kind_t target_param_kind;
    uint8_t target_voice_index;
    uint8_t target_param_index;
    instrument_param_value_t raw_target_voice;
    uint8_t target_voice;
    uint8_t target_is_scene;
    uint8_t target_slot;
    instrument_target_token_t target_param_token;
} menu_lfo_target_context_t;

static uint8_t menu_isVoicePage(uint8_t page);
static uint8_t menu_voicePageToSlot(uint8_t page);
static menu_cell_t menu_resolveCellAbsolute(uint8_t subPage, uint8_t position);
static menu_cell_t menu_resolveVoiceCellAtScreen(uint8_t subPage,
                                                 uint8_t screen,
                                                 uint8_t column);
static menu_cell_t menu_resolveCell(uint8_t subPage, uint8_t position);
static uint8_t menu_voiceSubPageScreenCount(uint8_t subPage);
static uint8_t menu_cellDtype(const menu_cell_t *cell);
static uint16_t menu_cellDisplayValue(const menu_cell_t *cell);
static uint8_t menu_cellCommitValue(const menu_cell_t *cell, uint16_t value);
static uint8_t menu_cellIsEmpty(const menu_cell_t *cell);
static uint8_t menu_lfoTargetPairForKind(instrument_binding_kind_t kind,
                                         uint8_t *pair_out,
                                         uint8_t *is_voice_out,
                                         uint8_t *is_param_out,
                                         instrument_binding_kind_t *voice_kind_out,
                                         instrument_binding_kind_t *param_kind_out);
static uint8_t menu_cellIsLfoTargetVoice(const menu_cell_t *cell);
static uint8_t menu_cellIsLfoTargetParam(const menu_cell_t *cell);
static uint8_t menu_cellIsVelocityTargetParam(const menu_cell_t *cell);
static uint8_t menu_lfoTargetContext(const menu_cell_t *cell,
                                     menu_lfo_target_context_t *ctx);
static instrument_target_token_t menu_lfoTargetNormalizeToken(
    const menu_lfo_target_context_t *ctx, instrument_target_token_t token);
static uint8_t menu_lfoTargetCommitVoiceAndReconcile(
    const menu_cell_t *cell, const menu_lfo_target_context_t *ctx,
    uint16_t raw_voice);
static uint8_t menu_lfoTargetEditVoice(const menu_cell_t *cell, int16_t delta);
static uint8_t menu_lfoTargetEditParam(const menu_cell_t *cell, int16_t delta);
static uint16_t menu_lfoTargetDisplayValue(const menu_cell_t *cell,
                                           uint16_t raw);
static instrument_target_token_t menu_velocityTargetNormalize(
    const menu_cell_t *cell, instrument_target_token_t raw);
static uint8_t menu_velocityTargetEditParam(const menu_cell_t *cell,
                                            int16_t delta);
static uint16_t menu_velocityTargetDisplayValue(const menu_cell_t *cell,
                                                uint16_t raw);
static void menu_copyPaddedField(char *dst, const char *src, uint8_t width);
static void menu_formatInstrumentTargetShort(uint16_t target, char *valueAsText);
static void menu_displayInstrumentTargetFull(uint16_t target);
static void menu_formatCellValue3(const menu_cell_t *cell, char *valueAsText);
static void menu_clampCellValue(const menu_cell_t *cell, uint16_t *value);

static uint8_t menu_isVoicePage(uint8_t page)
{
    return (uint8_t)(page <= VOICE7_PAGE);
}

static uint8_t menu_voicePageToSlot(uint8_t page)
{
    if (page >= VOICE7_PAGE)
        return 5u;
    return page;
}

static menu_cell_t menu_resolveCellAbsolute(uint8_t subPage, uint8_t position)
{
    menu_cell_t cell;
    const Page *page;

    memset(&cell, 0, sizeof(cell));
    cell.kind = MENU_CELL_EMPTY;
    cell.static_param = PAR_NONE;
    cell.text_id = TEXT_EMPTY;
    cell.descriptor_index = INSTRUMENT_MENU_EMPTY;

    /*
     * Resolve one absolute menu cell without assuming one global parameter ID
     * namespace.
     *
     * Static pages still come from menuPages[][] and parameter_values[]. Voice
     * pages now come from the active Scene slot's instrument descriptors: the
     * descriptor supplies text/dtype and descriptor_index supplies the storage
     * cell in SceneData. This keeps old global/Pattern pages stable while
     * removing the false overlap between flat PAR_* ids and instrument ids.
     * Voice callers may pass absolute positions 0..15; static callers remain
     * limited to the old 0..7 Page structure.
     */
    if (subPage >= NUM_SUB_PAGES)
        return cell;

    if (menu_isVoicePage(menu_activePage)) {
        const kit_instrument_slot_t *slot;
        uint8_t slot_index = menu_voicePageToSlot(menu_activePage);

        if (position >= INSTRUMENT_MENU_PAGE_CELLS)
            return cell;

        slot = scene_instrumentSlotConst(scene_getActiveIndex(), slot_index);
        if (!slot)
            return cell;

        cell.descriptor =
            instrumentManager_voicePageDescriptorIndex(slot->type,
                                                       menu_activePage,
                                                       subPage,
                                                       position,
                                                       &cell.descriptor_index);
        if (!cell.descriptor &&
            cell.descriptor_index == INSTRUMENT_MENU_SKIP) {
            cell.kind = MENU_CELL_STATIC;
            cell.text_id = TEXT_SKIP;
            cell.static_param = PAR_NONE;
            return cell;
        }
        if (!cell.descriptor)
            return cell;

        if (menu_activePage == VOICE7_PAGE &&
            slot_index == 5u &&
            (instrumentManager_typeFlags(slot->type) & INSTRUMENT_FLAG_CHOKE)) {
            uint8_t choke_index;

            /*
             * Generic VOICE7 choke substitution.
             *
             * Inputs: the normal descriptor selected by the instrument's one
             * menu layout, the visible VOICE page, and the owning slot. Output:
             * when slot 6 hosts a Choke instrument and the base descriptor has
             * a `<key>_choke` sibling, this cell displays/edits the sibling
             * descriptor instead of the base descriptor. Menu owns the VOICE7
             * and slot-6 context; InstrumentManager owns the descriptor suffix
             * relationship, so neither layer has to duplicate the other's
             * rules. Affiliates are hihat_menu_pages[], SceneData descriptor
             * images, and modulation target lists where `_choke` rows remain
             * separately visible.
             */
            if (instrumentManager_chokeDescriptorIndexForBase(
                    slot->type, cell.descriptor_index, &choke_index)) {
                const ParamDescriptor *choke_descriptor =
                    instrumentManager_descriptor(slot->type, choke_index);
                if (choke_descriptor) {
                    cell.descriptor_index = choke_index;
                    cell.descriptor = choke_descriptor;
                }
            }
        }

        if (menu_activePage == VOICE7_PAGE &&
            slot_index == 5u &&
            !(instrumentManager_typeFlags(slot->type) & INSTRUMENT_FLAG_CHOKE) &&
            cell.descriptor->file_key &&
            strcmp(cell.descriptor->file_key, "amp_envelope_decay") == 0) {
            /*
             * Generated non-Choke track-7 decay cell.
             *
             * Inputs: the base amp_envelope_decay descriptor resolved for
             * slot 6 on VOICE7. Output: the cell borrows that descriptor's
             * display text/dtype but commits to kit_settings_t instead of the
             * instrument descriptor image arrays. This generated setting only
             * exists for non-Choke instruments; Choke instruments use real
             * `_choke` descriptors, and instruments without amp_envelope_decay
             * naturally fall through to the normal VOICE6-style cell.
             */
            cell.kind = MENU_CELL_KIT_SETTING;
            cell.kit_setting = MENU_KIT_SETTING_SLOT6_TRACK7_AMP_DECAY;
            cell.slot = slot_index;
            return cell;
        }

        cell.kind = MENU_CELL_INSTRUMENT;
        cell.slot = slot_index;
        return cell;
    }

    if (position >= MENU_STATIC_SUBPAGE_CELLS)
        return cell;
    page = &menuPages[menu_activePage][subPage];
    cell.text_id = (&page->top1)[position];
    cell.static_param = (&page->bot1)[position];
    if (cell.text_id == TEXT_EMPTY || cell.static_param == PAR_NONE)
        return cell;

    cell.kind = MENU_CELL_STATIC;
    return cell;
}

static menu_cell_t menu_resolveSceneSettingCell(uint8_t index)
{
    menu_cell_t cell;

    /*
     * Resolve one VOICE mix Scene-setting cell for the active voice.
     *
     * Inputs: column-local index in the appended one-screen Scene section.
     * Output: a MENU_CELL_SCENE_SETTING with scene_setting equal to the index
     * and slot equal to the current VOICE page's zero-based instrument slot.
     * This keeps VOICE1/mix on voice 1's audio_out, fx_send_amount, and
     * fader_setting, while VOICE2/mix edits voice 2's same three fields, and
     * so on. Column 4 returns MENU_CELL_EMPTY because there are only three
     * Scene settings per voice.
     *
     * Affiliates: menu_voiceSubPageScreenCount() appends exactly one screen;
     * menu_sceneSettingShortName(), menu_cellDisplayValue(), and
     * menu_cellCommitValue() consume the resolved slot/setting pair.
     */
    memset(&cell, 0, sizeof(cell));
    cell.kind = MENU_CELL_EMPTY;
    cell.static_param = PAR_NONE;
    cell.text_id = TEXT_EMPTY;
    cell.descriptor_index = INSTRUMENT_MENU_EMPTY;
    if (index >= MENU_SCENE_SETTING_COUNT)
        return cell;
    cell.kind = MENU_CELL_SCENE_SETTING;
    cell.scene_setting = index;
    cell.slot = menu_voicePageToSlot(menu_activePage);
    return cell;
}

static uint8_t menu_voiceInstrumentScreenExists(uint8_t subPage,
                                                uint8_t screen)
{
    uint8_t i;
    uint8_t start;

    /*
     * Test only the instrument-descriptor part of a VOICE sub-page.
     *
     * Inputs: SELECT sub-page and descriptor screen 0..3. Output: nonzero when
     * any instrument or generated Kit setting cell is selectable. Scene-setting
     * screens are intentionally excluded so menu_voiceSubPageScreenCount() can
     * append them only for the mix sub-page.
     */
    if (subPage >= NUM_SUB_PAGES || screen >= MENU_VOICE_SUBPAGE_SCREENS)
        return 0u;
    start = (uint8_t)(screen * MENU_COMPACT_SCREEN_CELLS);
    for (i = 0u; i < MENU_COMPACT_SCREEN_CELLS; i++) {
        menu_cell_t cell =
            menu_resolveCellAbsolute(subPage, (uint8_t)(start + i));
        if (!menu_cellIsEmpty(&cell) &&
            !(cell.kind == MENU_CELL_STATIC && cell.text_id == TEXT_SKIP)) {
            return 1u;
        }
    }
    return 0u;
}

static uint8_t menu_voiceInstrumentScreenCount(uint8_t subPage)
{
    uint8_t screen;
    uint8_t count = 0u;

    /*
     * Count populated instrument screens for one VOICE sub-page.
     *
     * The loop records the highest populated descriptor screen plus one rather
     * than stopping at the first empty screen, so sparse future descriptor rows
     * remain navigable. Output is 0..4.
     */
    if (subPage >= NUM_SUB_PAGES)
        return 0u;
    for (screen = 0u; screen < MENU_VOICE_SUBPAGE_SCREENS; screen++) {
        if (menu_voiceInstrumentScreenExists(subPage, screen))
            count = (uint8_t)(screen + 1u);
    }
    return count;
}

static uint8_t menu_voiceSubPageScreenCount(uint8_t subPage)
{
    uint8_t count = menu_voiceInstrumentScreenCount(subPage);

    /*
     * Count total selectable screens behind one VOICE SELECT sub-page.
     *
     * Inputs: SELECT sub-page. Output: instrument descriptor screens plus the
     * appended per-voice Scene-setting screen only for mix. This keeps non-mix
     * pages on their old 0..4 screen domain while each VOICE/mix page exposes
     * the selected voice's Scene-owned routing, FX send, and fader settings
     * after the instrument's own mix cells.
     */
    if (subPage == MENU_VOICE_MIX_SUBPAGE)
        count = (uint8_t)(count + MENU_SCENE_SETTING_SCREENS);
    return count;
}

static menu_cell_t menu_resolveVoiceCellAtScreen(uint8_t subPage,
                                                 uint8_t screen,
                                                 uint8_t column)
{
    uint8_t instrument_screens;

    /*
     * Resolve a visible VOICE compact cell by explicit screen and column.
     *
     * Inputs: SELECT sub-page, remembered screen, and visible column 0..3.
     * Output: instrument/generated Kit cells for descriptor screens, or the
     * selected voice's three Scene-setting cells for the appended mix screen.
     * This helper is the common affiliate for display, encoder navigation,
     * SELECT cycling, and endless-pot mapping, so all controls see the same
     * per-voice Scene-setting split.
     */
    if (column >= MENU_COMPACT_SCREEN_CELLS)
        column = (uint8_t)(column % MENU_COMPACT_SCREEN_CELLS);
    instrument_screens = menu_voiceInstrumentScreenCount(subPage);
    if (subPage == MENU_VOICE_MIX_SUBPAGE && screen >= instrument_screens) {
        uint8_t scene_screen = (uint8_t)(screen - instrument_screens);
        uint8_t scene_index =
            (uint8_t)(scene_screen * MENU_COMPACT_SCREEN_CELLS + column);
        return menu_resolveSceneSettingCell(scene_index);
    }
    return menu_resolveCellAbsolute(
        subPage,
        (uint8_t)(screen * MENU_COMPACT_SCREEN_CELLS + column));
}

static menu_cell_t menu_resolveCell(uint8_t subPage, uint8_t position)
{
    if (menu_isVoicePage(menu_activePage)) {
        uint8_t screen;
        if (subPage >= NUM_SUB_PAGES)
            subPage = 0u;
        screen = menu_voiceSubPageScreen[subPage];
        if (!menu_voiceSubPageScreenExists(subPage, screen)) {
            screen = 0u;
            menu_voiceSubPageScreen[subPage] = 0u;
        }
        return menu_resolveVoiceCellAtScreen(
            subPage, screen, (uint8_t)(position % MENU_COMPACT_SCREEN_CELLS));
    }
    return menu_resolveCellAbsolute(subPage, position);
}

static uint8_t menu_cellIsEmpty(const menu_cell_t *cell)
{
    return (uint8_t)(!cell || cell->kind == MENU_CELL_EMPTY);
}

static uint8_t menu_lfoTargetPairForKind(instrument_binding_kind_t kind,
                                         uint8_t *pair_out,
                                         uint8_t *is_voice_out,
                                         uint8_t *is_param_out,
                                         instrument_binding_kind_t *voice_kind_out,
                                         instrument_binding_kind_t *param_kind_out)
{
    uint8_t pair = 0xffu;
    uint8_t is_voice = 0u;
    uint8_t is_param = 0u;
    instrument_binding_kind_t voice_kind = INSTRUMENT_BIND_NONE;
    instrument_binding_kind_t param_kind = INSTRUMENT_BIND_NONE;

    /*
     * Resolve one LFO target binding into its selector pair.
     *
     * Inputs: a descriptor runtime binding kind from the currently edited
     * menu cell. Outputs: target-pair index, whether the edited cell is the
     * voice or parameter side, and the sibling binding kinds for that same
     * pair. This helper exists because pair behavior is shared while storage
     * cells are not: pair 1 must use lfo_target_voice/lfo_target_param, and
     * pair 2 must use lfo_target_voice_2/lfo_target_param_2. Keeping the map
     * here prevents display, encoder, endless-pot, and reconcile paths from
     * duplicating four enum checks or assuming descriptor indices.
     */
    switch (kind) {
    case INSTRUMENT_BIND_LFO_TARGET_VOICE:
        pair = 0u;
        is_voice = 1u;
        voice_kind = INSTRUMENT_BIND_LFO_TARGET_VOICE;
        param_kind = INSTRUMENT_BIND_LFO_TARGET_PARAM;
        break;
    case INSTRUMENT_BIND_LFO_TARGET_PARAM:
        pair = 0u;
        is_param = 1u;
        voice_kind = INSTRUMENT_BIND_LFO_TARGET_VOICE;
        param_kind = INSTRUMENT_BIND_LFO_TARGET_PARAM;
        break;
    case INSTRUMENT_BIND_LFO_TARGET_VOICE_2:
        pair = 1u;
        is_voice = 1u;
        voice_kind = INSTRUMENT_BIND_LFO_TARGET_VOICE_2;
        param_kind = INSTRUMENT_BIND_LFO_TARGET_PARAM_2;
        break;
    case INSTRUMENT_BIND_LFO_TARGET_PARAM_2:
        pair = 1u;
        is_param = 1u;
        voice_kind = INSTRUMENT_BIND_LFO_TARGET_VOICE_2;
        param_kind = INSTRUMENT_BIND_LFO_TARGET_PARAM_2;
        break;
    default:
        break;
    }

    if (pair == 0xffu)
        return 0u;
    if (pair_out)
        *pair_out = pair;
    if (is_voice_out)
        *is_voice_out = is_voice;
    if (is_param_out)
        *is_param_out = is_param;
    if (voice_kind_out)
        *voice_kind_out = voice_kind;
    if (param_kind_out)
        *param_kind_out = param_kind;
    return 1u;
}

static uint8_t menu_cellIsLfoTargetVoice(const menu_cell_t *cell)
{
    uint8_t is_voice = 0u;
    if (!cell || cell->kind != MENU_CELL_INSTRUMENT || !cell->descriptor)
        return 0u;
    (void)menu_lfoTargetPairForKind(cell->descriptor->runtime.kind, 0,
                                    &is_voice, 0, 0, 0);
    return is_voice;
}

static uint8_t menu_cellIsLfoTargetParam(const menu_cell_t *cell)
{
    uint8_t is_param = 0u;
    if (!cell || cell->kind != MENU_CELL_INSTRUMENT || !cell->descriptor)
        return 0u;
    (void)menu_lfoTargetPairForKind(cell->descriptor->runtime.kind, 0, 0,
                                    &is_param, 0, 0);
    return is_param;
}

static uint8_t menu_cellIsVelocityTargetParam(const menu_cell_t *cell)
{
    /*
     * Identify the descriptor cell that stores one voice's velocity target.
     *
     * Input: resolved menu cell. Output: nonzero only for an instrument-owned
     * velo_mod_dest binding. This cannot be inferred from dtype alone because
     * LFO target params share the target-selection dtype but use a paired
     * DstVoice context, while velocity uses the source slot as its context.
     */
    return (uint8_t)(cell &&
                     cell->kind == MENU_CELL_INSTRUMENT &&
                     cell->descriptor &&
                     cell->descriptor->runtime.kind ==
                        INSTRUMENT_BIND_VELOCITY_TARGET);
}

static void menu_sceneSettingShortName(const menu_cell_t *cell, char *dst)
{
    /*
     * Build compact three-character labels for VOICE mix Scene settings.
     *
     * Inputs: Scene-setting cell and three-byte destination. Output examples:
     * 1ou..6ou for audio routing, 1fx..6fx for retained FX send, and
     * 1fd..6fd for retained fader mode. The slot number is one-based so the
     * compact label identifies which voice the Scene setting edits.
     */
    uint8_t voice = (cell && cell->slot < INSTRUMENT_SLOT_COUNT)
        ? (uint8_t)(cell->slot + 1u) : 1u;
    dst[0] = (char)('0' + voice);
    switch (cell ? cell->scene_setting : MENU_SCENE_SETTING_AUDIO_OUT) {
    case MENU_SCENE_SETTING_FX_SEND_AMOUNT:
        dst[1] = 'f';
        dst[2] = 'x';
        break;
    case MENU_SCENE_SETTING_FADER_SETTING:
        dst[1] = 'f';
        dst[2] = 'd';
        break;
    default:
        dst[1] = 'o';
        dst[2] = 'u';
        break;
    }
}

static void menu_sceneSettingFaderName(uint8_t value, char *dst)
{
    /*
     * Format the retained fader mode domain.
     *
     * Inputs: stored 0..2 fader mode. Output: compact user text. These labels
     * are storage/UI placeholders until mixer/FX routing implements behavior:
     * pre = normal/pre-FX, pst = post-FX, fx  = FX-only.
     */
    if (value == 1u)
        memcpy(dst, "pst", 3);
    else if (value == 2u)
        memcpy(dst, "fx ", 3);
    else
        memcpy(dst, "pre", 3);
}

static uint8_t menu_cellDtype(const menu_cell_t *cell)
{
    if (!cell)
        return DTYPE_0B127;
    if (cell->kind == MENU_CELL_SCENE_SETTING) {
        if (cell->scene_setting == MENU_SCENE_SETTING_AUDIO_OUT)
            return (uint8_t)((MENU_AUDIO_OUT << 4) | DTYPE_MENU);
        if (cell->scene_setting == MENU_SCENE_SETTING_FADER_SETTING)
            return DTYPE_0B15;
        return DTYPE_0B127;
    }
    if (cell->kind == MENU_CELL_INSTRUMENT ||
        cell->kind == MENU_CELL_KIT_SETTING)
        return cell->descriptor ? cell->descriptor->dtype : DTYPE_0B127;
    if (cell->kind == MENU_CELL_STATIC && cell->static_param < NUM_PARAMS)
        return parameter_dtypes[cell->static_param];
    return DTYPE_0B127;
}

static uint16_t menu_cellDisplayValue(const menu_cell_t *cell)
{
    if (!cell)
        return 0u;
    if (cell->kind == MENU_CELL_KIT_SETTING) {
        /*
         * Display generated kit-setting cells.
         *
         * Inputs: resolved generated cell plus current Morph endpoint mode.
         * Output: retained kit-setting endpoint value. The cell borrows a
         * descriptor only for text/dtype; its value is Kit-owned and therefore
         * read through SceneData accessors rather than descriptor images.
         */
        if (cell->kit_setting ==
            MENU_KIT_SETTING_SLOT6_TRACK7_AMP_DECAY) {
            return voiceModeShowMorph
                ? scene_getSlot6Track7MorphAmpEnvelopeDecay(
                    scene_getActiveIndex())
                : scene_getSlot6Track7AmpEnvelopeDecay(scene_getActiveIndex());
        }
        return 0u;
    }
    if (cell->kind == MENU_CELL_INSTRUMENT) {
        const kit_instrument_slot_t *slot =
            scene_instrumentSlotConst(scene_getActiveIndex(), cell->slot);
        if (!slot || cell->descriptor_index >= INSTRUMENT_PARAM_COUNT)
            return 0u;
        return voiceModeShowMorph
            ? slot->parameter_images.morph_instrument_parameters[cell->descriptor_index]
            : slot->parameter_images.instrument_parameters[cell->descriptor_index];
    }
    if (cell->kind == MENU_CELL_SCENE_SETTING) {
        /*
         * Display retained Scene-owned VOICE mix settings.
         *
         * Inputs: active resident Scene and zero-based slot from the resolved
         * cell. Outputs: scalar value in the same domain used by sceneset.scg
         * and Preset setters. Morph endpoint display never changes these
         * values because Scene settings are not instrument morph endpoints.
         */
        uint8_t scene_index = scene_getActiveIndex();
        switch (cell->scene_setting) {
        case MENU_SCENE_SETTING_AUDIO_OUT:
            return scene_getVoiceAudioOut(scene_index, cell->slot);
        case MENU_SCENE_SETTING_FX_SEND_AMOUNT:
            return scene_getVoiceFxSendAmount(scene_index, cell->slot);
        case MENU_SCENE_SETTING_FADER_SETTING:
            return scene_getVoiceFaderSetting(scene_index, cell->slot);
        default:
            return 0u;
        }
    }
    if (cell->kind == MENU_CELL_STATIC)
        return menu_getParameterDisplayValue(cell->static_param);
    return 0u;
}

static uint8_t menu_cellCommitValue(const menu_cell_t *cell, uint16_t value)
{
    if (!cell)
        return 0u;
    if (cell->kind == MENU_CELL_KIT_SETTING) {
        uint16_t edit_mask = bank_sceneMaskVoiceEdit();
        uint8_t scene_index;
        uint8_t changed = 0u;

        /*
         * Commit generated kit-setting cells.
         *
         * Inputs: generated menu cell, edited value, and BankData's
         * scene_mask_voice_edit. Output: every masked resident Scene receives
         * the generated Kit endpoint selected by Morph edit mode. This cannot
         * use preset_setInstrumentParameter() because there is no descriptor
         * index or instrument-file storage for generated track-7 decay.
         */
        if (cell->kit_setting ==
            MENU_KIT_SETTING_SLOT6_TRACK7_AMP_DECAY) {
            for (scene_index = 0u;
                 scene_index < SCENE_COUNT && scene_index < 16u;
                 scene_index++) {
                if ((edit_mask & (uint16_t)(1u << scene_index)) == 0u)
                    continue;
                changed |= preset_setSlot6Track7AmpEnvelopeDecay(
                    scene_index,
                    voiceModeShowMorph ? INSTRUMENT_IMAGE_MORPH
                                       : INSTRUMENT_IMAGE_MAIN,
                    (uint8_t)value,
                    (uint8_t)(!voiceModeShowMorph &&
                              scene_index == scene_getActiveIndex()));
            }
            return changed;
        }
        return 0u;
    }
    if (cell->kind == MENU_CELL_INSTRUMENT) {
        uint16_t edit_mask = bank_sceneMaskVoiceEdit();
        uint8_t scene_index;
        uint8_t changed = 0u;
        if (!cell->descriptor)
            return 0u;
        for (scene_index = 0u;
             scene_index < SCENE_COUNT && scene_index < 16u;
             scene_index++) {
            if ((edit_mask & (uint16_t)(1u << scene_index)) == 0u)
                continue;
            if (cell->descriptor->flags & INSTRUMENT_PARAM_FLAG_MORPHABLE) {
                changed |= preset_setInstrumentParameter(
                    scene_index, cell->slot, cell->descriptor_index,
                    voiceModeShowMorph ? INSTRUMENT_IMAGE_MORPH
                                       : INSTRUMENT_IMAGE_MAIN,
                    (uint8_t)value,
                    (uint8_t)(!voiceModeShowMorph &&
                              scene_index == scene_getActiveIndex()));
            } else if (!voiceModeShowMorph) {
                changed |= preset_setSupplementalParameter(
                    scene_index, cell->slot, cell->descriptor_index, value);
            }
        }
        return changed;
    }
    if (cell->kind == MENU_CELL_SCENE_SETTING) {
        uint16_t edit_mask = bank_sceneMaskVoiceEdit();
        uint8_t scene_index;
        uint8_t changed = 0u;

        /*
         * Commit retained Scene-owned VOICE mix settings through Preset.
         *
         * Inputs: scene_mask_voice_edit, zero-based slot, and clamped UI
         * value. Outputs: each masked Scene retains the setting; active Scene
         * runtime side effects still occur only when the loop reaches the
         * active Scene because the Preset setters guard runtime by Scene index.
         */
        for (scene_index = 0u;
             scene_index < SCENE_COUNT && scene_index < 16u;
             scene_index++) {
            if ((edit_mask & (uint16_t)(1u << scene_index)) == 0u)
                continue;
            switch (cell->scene_setting) {
            case MENU_SCENE_SETTING_AUDIO_OUT:
                changed |= preset_setVoiceAudioOut(scene_index, cell->slot,
                                                   (uint8_t)value);
                break;
            case MENU_SCENE_SETTING_FX_SEND_AMOUNT:
                changed |= preset_setVoiceFxSendAmount(scene_index,
                                                       cell->slot,
                                                       (uint8_t)value);
                break;
            case MENU_SCENE_SETTING_FADER_SETTING:
                changed |= preset_setVoiceFaderSetting(scene_index,
                                                       cell->slot,
                                                       (uint8_t)value);
                break;
            default:
                break;
            }
        }
        return changed;
    }
    if (cell->kind == MENU_CELL_STATIC) {
        uint8_t *paramValue = menu_getParameterEditPtr(cell->static_param);
        if (!paramValue)
            return 0u;
        *paramValue = (uint8_t)value;
        menu_sendEditedParameter(cell->static_param, *paramValue);
        return 1u;
    }
    return 0u;
}

static uint8_t menu_lfoTargetContext(const menu_cell_t *cell,
                                     menu_lfo_target_context_t *ctx)
{
    const kit_instrument_slot_t *source;
    uint8_t voice_index;
    uint8_t param_index;
    instrument_param_value_t raw_voice;
    uint8_t pair;
    instrument_binding_kind_t voice_kind;
    instrument_binding_kind_t param_kind;

    /*
     * Resolve the two-cell LFO target editing context for one source slot/pair.
     *
     * Inputs: a resolved instrument menu cell, either lfo_target_voice or
     * lfo_target_param for pair 1 or pair 2, and an output context. Outputs:
     * source slot, pair id, sibling descriptor indices, raw/clamped target
     * voice, target slot, and current stored target parameter. The function
     * returns 0 when the cell is not an LFO target cell or the current source
     * instrument type does not expose both target bindings for that pair.
     *
     * Why this cannot be folded into the encoder/knob handlers: those handlers
     * should describe input mechanics, not know how an instrument registry maps
     * sibling storage cells. The same context is needed by display, encoder,
     * endless knobs, and later load normalization, so keeping one resolver
     * prevents each caller from making descriptor-index assumptions.
     *
     * Common accessors/affiliates: scene_instrumentSlotConst() reads the
     * current source slot type and generic SceneData storage;
     * instrumentManager_descriptorIndexForBinding() finds sibling descriptor
     * cells by binding kind; InstrumentManager target helpers validate the
     * selected target slot's current instrument descriptor flags.
     */
    if (!ctx || !cell || cell->kind != MENU_CELL_INSTRUMENT ||
        !cell->descriptor ||
        !menu_lfoTargetPairForKind(cell->descriptor->runtime.kind, &pair, 0, 0,
                                   &voice_kind, &param_kind))
        return 0u;

    memset(ctx, 0, sizeof(*ctx));
    ctx->scene_index = scene_getActiveIndex();
    ctx->source_slot = cell->slot;
    ctx->target_pair = pair;
    ctx->target_voice_kind = voice_kind;
    ctx->target_param_kind = param_kind;
    source = scene_instrumentSlotConst(ctx->scene_index, ctx->source_slot);
    if (!source)
        return 0u;

    if (!instrumentManager_descriptorIndexForBinding(
            source->type, voice_kind, &voice_index) ||
        !instrumentManager_descriptorIndexForBinding(
            source->type, param_kind, &param_index)) {
        return 0u;
    }

    raw_voice = source->parameter_images.instrument_parameters[voice_index];
    ctx->target_voice_index = voice_index;
    ctx->target_param_index = param_index;
    ctx->raw_target_voice = raw_voice;
    if (raw_voice < INSTRUMENT_TARGET_VOICE_FIRST)
        ctx->target_voice = 1u;
    else if (raw_voice > INSTRUMENT_TARGET_VOICE_SCENE)
        ctx->target_voice = INSTRUMENT_TARGET_VOICE_SCENE;
    else
        ctx->target_voice = raw_voice;
    ctx->target_is_scene =
        (uint8_t)(ctx->target_voice == INSTRUMENT_TARGET_VOICE_SCENE);
    ctx->target_slot = ctx->target_is_scene
        ? 0xffu
        : (uint8_t)(ctx->target_voice - 1u);
    ctx->target_param_token =
        source->parameter_images.instrument_parameters[param_index];
    return 1u;
}

static instrument_target_token_t menu_lfoTargetNormalizeToken(
    const menu_lfo_target_context_t *ctx, instrument_target_token_t token)
{
    instrument_param_id_t display_id;

    /*
     * Normalize an LFO target token against the selected target voice.
     *
     * Inputs: an already resolved LFO target context and a retained byte token.
     * Output: off or a byte token that is valid in the current namespace. Voice
     * namespaces store descriptor-local indices; the Scene namespace stores
     * Scene target-table indices. Display/runtime code expands the token only
     * long enough to reuse canonical target helpers.
     *
     * Why this is separate from the edit functions: voice edits, parameter
     * edits, display, and load normalization all need the same invariant:
     * lfo_target_param is either off, belongs to the selected target voice and
     * is modulatable for that voice slot's current instrument type, or belongs
     * to the Scene mod target namespace when DstVoice is `scn`.
     *
     * Common clients/affiliates: menu_lfoTargetEditVoice() preserves the local
     * descriptor across voice changes through this helper; display treats stale
     * values as off; InstrumentManager supplies canonical ID packing and
     * descriptor-flag validation.
     */
    if (!ctx || token == INSTRUMENT_TARGET_TOKEN_OFF) {
        return INSTRUMENT_TARGET_TOKEN_OFF;
    }

    display_id = instrumentManager_lfoTargetIdFromToken(
        ctx->scene_index, ctx->source_slot, ctx->target_voice, token,
        INSTRUMENT_TARGET_MODULATION);
    return instrumentManager_lfoTargetTokenFromId(
        ctx->scene_index, ctx->target_voice, display_id,
        INSTRUMENT_TARGET_MODULATION);
}

static uint8_t menu_lfoTargetCommitVoiceAndReconcile(
    const menu_cell_t *cell, const menu_lfo_target_context_t *ctx,
    uint16_t raw_voice)
{
    uint8_t voice;
    instrument_target_token_t reconciled;
    uint8_t ok;

    /*
     * Commit a target voice and reconcile the sibling target parameter.
     *
     * Inputs: the edited lfo_target_voice cell, its context before the voice
     * change, and the requested raw voice value. Outputs: the clamped voice is
     * stored in the source slot; lfo_target_param preserves its local byte
     * token only when that token is valid in the new namespace, otherwise it is
     * reset to off. Return value is nonzero when the voice commit succeeded.
     *
     * Why this cannot be folded into menu_cellCommitValue(): generic cell
     * commit deliberately knows only one descriptor cell at a time. LFO target
     * voice is a paired control: changing it must preserve or invalidate its
     * sibling target parameter. Keeping pair reconciliation here lets the
     * generic SceneData setter stay single-cell and reusable.
     *
     * Common accessors/affiliates: preset_setSupplementalParameter() writes
     * source slot storage and performs current runtime validation;
     * menu_lfoTargetNormalizeToken() applies target-slot descriptor rules;
     * InstrumentManager owns all target capability checks.
     */
    if (!cell || !ctx)
        return 0u;
    if (raw_voice < INSTRUMENT_TARGET_VOICE_FIRST)
        voice = INSTRUMENT_TARGET_VOICE_FIRST;
    else if (raw_voice > INSTRUMENT_TARGET_VOICE_SCENE)
        voice = INSTRUMENT_TARGET_VOICE_SCENE;
    else
        voice = (uint8_t)raw_voice;

    ok = preset_setSupplementalParameter(ctx->scene_index, ctx->source_slot,
                                         ctx->target_voice_index, voice);
    {
        menu_lfo_target_context_t next = *ctx;
        next.raw_target_voice = voice;
        next.target_voice = voice;
        next.target_is_scene =
            (uint8_t)(voice == INSTRUMENT_TARGET_VOICE_SCENE);
        next.target_slot = next.target_is_scene
            ? 0xffu
            : (uint8_t)(voice - 1u);
        reconciled =
            menu_lfoTargetNormalizeToken(&next, ctx->target_param_token);
        (void)preset_setSupplementalParameter(ctx->scene_index,
                                              ctx->source_slot,
                                              ctx->target_param_index,
                                              reconciled);
    }
    return ok;
}

static uint8_t menu_lfoTargetEditVoice(const menu_cell_t *cell, int16_t delta)
{
    menu_lfo_target_context_t ctx;
    int16_t next;

    /*
     * Apply encoder/knob movement to lfo_target_voice.
     *
     * Inputs: the resolved voice target cell and signed movement delta from
     * either the main encoder or an endless pot. Outputs: the target voice is
     * clamped to 1..INSTRUMENT_TARGET_VOICE_SCENE, where the final value is
     * displayed as `scn`, and the sibling target parameter is reconciled against the new
     * selected namespace.
     *
     * Why this is a separate edit helper: encoder and knob paths previously
     * edited the field as a generic 0..127 value. Both controls must share the
     * same clamp/reconcile rule, and that rule depends on sibling descriptor
     * storage rather than only the edited cell.
     *
     * Common clients/affiliates: menu_encoderChangeParameter(),
     * menu_parseKnobDelta(), preset_setSupplementalParameter(), and
     * InstrumentManager's registry-driven target validation.
     */
    if (!menu_lfoTargetContext(cell, &ctx))
        return 0u;
    next = (int16_t)ctx.target_voice + delta;
    if (next < (int16_t)INSTRUMENT_TARGET_VOICE_FIRST)
        next = INSTRUMENT_TARGET_VOICE_FIRST;
    else if (next > (int16_t)INSTRUMENT_TARGET_VOICE_SCENE)
        next = INSTRUMENT_TARGET_VOICE_SCENE;
    return menu_lfoTargetCommitVoiceAndReconcile(cell, &ctx, (uint16_t)next);
}

static uint8_t menu_lfoTargetEditParam(const menu_cell_t *cell, int16_t delta)
{
    menu_lfo_target_context_t ctx;
    instrument_target_token_t token;
    uint8_t steps;
    int8_t dir;

    /*
     * Apply encoder/knob movement to lfo_target_param.
     *
     * Inputs: the resolved target-parameter cell and signed movement delta.
     * Outputs: the source slot stores only off or a byte token in the selected
     * namespace. Non-modulatable descriptors and unsupported Scene targets are
     * skipped, so the picker exposes exactly one off position.
     *
     * Why this cannot be part of menu_encoderChangeParameter(): the same
     * filtered traversal is required for endless knobs and future normalization
     * work. It also depends on sibling lfo_target_voice storage and target
     * slot registry state, neither of which belongs in the generic numeric
     * edit path.
     *
     * Common clients/affiliates: both physical edit paths, SceneData's generic
     * per-slot storage, instrumentManager_stepTargetForSlot(), and the future
     * descriptor-aware DSP modulation adapter.
     */
    if (!menu_lfoTargetContext(cell, &ctx))
        return 0u;

    if (ctx.raw_target_voice != ctx.target_voice) {
        (void)preset_setSupplementalParameter(ctx.scene_index, ctx.source_slot,
                                              ctx.target_voice_index,
                                              ctx.target_voice);
    }

    token = menu_lfoTargetNormalizeToken(&ctx, ctx.target_param_token);
    if (delta == 0) {
        return preset_setSupplementalParameter(ctx.scene_index, ctx.source_slot,
                                               ctx.target_param_index, token);
    }

    dir = (delta > 0) ? 1 : -1;
    steps = (uint8_t)((delta > 0) ? delta : -delta);
    while (steps--) {
        instrument_target_token_t next = instrumentManager_stepLfoTargetToken(
            ctx.scene_index, ctx.target_voice, token, dir,
            INSTRUMENT_TARGET_MODULATION);
        if (next == token)
            break;
        token = next;
        if (token == INSTRUMENT_TARGET_TOKEN_OFF && dir < 0)
            break;
    }

    return preset_setSupplementalParameter(ctx.scene_index, ctx.source_slot,
                                           ctx.target_param_index, token);
}

static uint16_t menu_lfoTargetDisplayValue(const menu_cell_t *cell,
                                           uint16_t raw)
{
    menu_lfo_target_context_t ctx;
    instrument_target_token_t token;

    /*
     * Return the display-safe value for lfo_target_param.
     *
     * Inputs: a resolved menu cell and the raw stored target token. Output:
     * off or a temporary canonical target ID that belongs to the currently
     * selected namespace and is modulatable. This function does not mutate
     * SceneData.
     *
     * Why display normalization is separate from edit normalization: a stale
     * loaded value or instrument-type swap should not draw a target from the
     * wrong voice while the user is merely browsing. Edits will commit the
     * normalized value, but repaint can safely render stale data as off first.
     *
     * Common clients/affiliates: compact four-column rendering,
     * single-parameter target display, menu_lfoTargetNormalizeToken(), and
     * InstrumentManager descriptor validity.
     */
    if (!menu_cellIsLfoTargetParam(cell) ||
        !menu_lfoTargetContext(cell, &ctx)) {
        return raw;
    }
    token = menu_lfoTargetNormalizeToken(&ctx, (instrument_target_token_t)raw);
    return instrumentManager_lfoTargetIdFromToken(
        ctx.scene_index, ctx.source_slot, ctx.target_voice, token,
        INSTRUMENT_TARGET_MODULATION);
}

static instrument_target_token_t menu_velocityTargetNormalize(
    const menu_cell_t *cell, instrument_target_token_t raw)
{
    /*
     * Normalize a velocity destination token against its source voice.
     *
     * Inputs: resolved velo_mod_dest cell and raw stored byte. Output: off, a
     * local descriptor token on the same source slot, or the special token for
     * that voice's own Scene Morph target. This is separate from LFO
     * normalization because velocity has no DstVoice cell.
     */
    if (!menu_cellIsVelocityTargetParam(cell))
        return raw;
    return instrumentManager_targetValidForVelocitySource(
        scene_getActiveIndex(), cell->slot, raw)
        ? raw : INSTRUMENT_TARGET_TOKEN_OFF;
}

static uint8_t menu_velocityTargetEditParam(const menu_cell_t *cell,
                                            int16_t delta)
{
    instrument_target_token_t token;
    uint8_t steps;
    int8_t dir;

    /*
     * Edit one velocity destination cell through the mixed target list.
     *
     * Inputs: resolved velo_mod_dest cell and signed encoder/knob delta.
     * Output: source slot storage receives exactly one byte token: off, a
     * modulatable descriptor target on the same source slot, or the source
     * voice's own Morph Scene target. Non-modulatable descriptors are skipped
     * and there is only one off entry.
     *
     * This cannot use the generic DTYPE_TARGET_SELECTION path because velocity
     * targets are not numeric ranges and are not the old modTargets[] table.
     * The traversal depends on the source slot's current instrument type and
     * the shared SceneModTargets list, so it must call the descriptor/Scene
     * target browsers instead of incrementing raw stored IDs.
     */
    if (!menu_cellIsVelocityTargetParam(cell))
        return 0u;
    token = menu_velocityTargetNormalize(
        cell, (instrument_target_token_t)menu_cellDisplayValue(cell));
    if (delta == 0) {
        return menu_cellCommitValue(cell, token);
    }
    dir = (delta > 0) ? 1 : -1;
    steps = (uint8_t)((delta > 0) ? delta : -delta);
    while (steps--) {
        instrument_target_token_t next =
            instrumentManager_stepVelocityTargetForSource(
                scene_getActiveIndex(), cell->slot, token, dir);
        if (next == token)
            break;
        token = next;
        if (token == INSTRUMENT_TARGET_TOKEN_OFF && dir < 0)
            break;
    }
    return menu_cellCommitValue(cell, token);
}

static uint16_t menu_velocityTargetDisplayValue(const menu_cell_t *cell,
                                                uint16_t raw)
{
    /*
     * Return the display-safe velocity target value.
     *
     * Inputs: resolved menu cell and raw stored target. Output: stale values
     * render as off without mutating SceneData. Edits commit the same
     * normalization through menu_velocityTargetEditParam().
     */
    instrument_target_token_t token =
        menu_velocityTargetNormalize(cell, (instrument_target_token_t)raw);
    if (token == INSTRUMENT_TARGET_TOKEN_OFF)
        return INSTRUMENT_PARAM_INVALID;
    if (token == INSTRUMENT_TARGET_TOKEN_VOICE_MORPH)
        return sceneModTarget_voiceMorphId(cell ? cell->slot : 0xffu);
    return instrumentManager_targetIdFromTokenForSlot(
        scene_getActiveIndex(), cell ? cell->slot : 0xffu, token,
        INSTRUMENT_TARGET_MODULATION);
}

static void menu_copyPaddedField(char *dst, const char *src, uint8_t width)
{
    uint8_t i = 0u;

    /*
     * Menu labels are fixed-width LCD fields, but descriptor text is normal C
     * string data. Copy at most the field width, stop at the first terminator,
     * and pad the rest. This keeps short labels such as "FM" or "LFO" from
     * leaking adjacent string-literal bytes into the display.
     */
    if (src) {
        while (i < width && src[i]) {
            dst[i] = src[i];
            i++;
        }
    }
    while (i < width) {
        dst[i] = ' ';
        i++;
    }
}

static void menu_formatInstrumentTargetShort(uint16_t target, char *valueAsText)
{
    uint8_t slot;
    uint8_t local;
    const kit_instrument_slot_t *instrument;
    const ParamDescriptor *descriptor;

    /*
     * Compact descriptor-target renderer.
     *
     * Descriptor target values are canonical slot*64+descriptor IDs, not
     * indices into the old modTargets[] table. The compact display therefore
     * derives its text from the active Scene's slot type at paint time. Storage
     * remains just the canonical target id in the parameter cell.
     *
     * Output is only the target descriptor's own three-character short name.
     * Earlier code prefixed the voice number (for example "1wa" or "1co"),
     * but LFO target voice is already shown as its own adjacent parameter and
     * the old step-automation voice-prefixed display is being retired. Keeping
     * this renderer descriptor-only prevents redundant voice text from leaking
     * into any current or future descriptor-target browser.
     */
    if (target == INSTRUMENT_PARAM_INVALID ||
        (!instrumentParam_isVoiceParameter(target) &&
         !sceneModTarget_isSceneTarget(target))) {
        memcpy(valueAsText, menuText_off, 3);
        return;
    }

    if (sceneModTarget_isSceneTarget(target)) {
        sceneModTarget_formatShort(target, valueAsText);
        return;
    }

    slot = instrumentParam_slot(target);
    local = instrumentParam_local(target);
    instrument = scene_instrumentSlotConst(scene_getActiveIndex(), slot);
    descriptor = instrument
        ? instrumentManager_descriptor(instrument->type, local)
        : 0;
    if (!descriptor) {
        memcpy(valueAsText, menuText_off, 3);
        return;
    }

    menu_copyPaddedField(valueAsText, descriptor->short_name, 3u);
}

static void menu_displayInstrumentTargetFull(uint16_t target)
{
    uint8_t slot;
    uint8_t local;
    const kit_instrument_slot_t *instrument;
    const ParamDescriptor *descriptor;

    if (target == INSTRUMENT_PARAM_INVALID ||
        (!instrumentParam_isVoiceParameter(target) &&
         !sceneModTarget_isSceneTarget(target))) {
        memcpy(&editDisplayBuffer[1][0], menuText_off, 3);
        return;
    }

    if (sceneModTarget_isSceneTarget(target)) {
        sceneModTarget_formatFull(target, &editDisplayBuffer[1][0],
                                  &editDisplayBuffer[1][8]);
        return;
    }

    slot = instrumentParam_slot(target);
    local = instrumentParam_local(target);
    instrument = scene_instrumentSlotConst(scene_getActiveIndex(), slot);
    descriptor = instrument
        ? instrumentManager_descriptor(instrument->type, local)
        : 0;
    if (!descriptor) {
        memcpy(&editDisplayBuffer[1][0], menuText_off, 3);
        return;
    }

    /*
     * Full descriptor-target renderer.
     *
     * Inputs: a canonical target ID. Output: row 2 shows the target
     * descriptor's category in columns 0-7 and long name in columns 8-15. The
     * selected target voice is intentionally not repeated here: for LFO target
     * editing, voice is already a separate menu parameter, and repeating
     * "VoiceN" hides the category information the user needs to identify the
     * target. This function stays separate from the compact renderer because
     * the LCD field widths and user-facing purpose are different.
     *
     * Affiliates: SceneData supplies the active target slot type,
     * InstrumentManager resolves the descriptor for the canonical target, and
     * menu_copyPaddedField() keeps short category/long-name strings bounded to
     * their fixed LCD fields.
     */
    menu_copyPaddedField(&editDisplayBuffer[1][0], descriptor->category, 8u);
    menu_copyPaddedField(&editDisplayBuffer[1][8], descriptor->long_name, 8u);
}

static void menu_formatCellValue3(const menu_cell_t *cell, char *valueAsText)
{
    uint8_t dtype = (uint8_t)(menu_cellDtype(cell) & 0x0f);
    uint16_t raw = menu_cellDisplayValue(cell);
    uint8_t value = (raw > 255u) ? 255u : (uint8_t)raw;

    /*
     * Format one cell value for the compact four-column view. Instrument cells
     * share the same dtype vocabulary as static cells, but target cells may
     * store 16-bit canonical descriptor IDs. Those IDs display as off when the
     * invalid sentinel is stored; a later target-picker patch can replace the
     * numeric fallback with descriptor-name rendering.
     */
    if (dtype == DTYPE_TARGET_SELECTION_LFO && cell &&
        cell->kind == MENU_CELL_INSTRUMENT) {
        raw = menu_lfoTargetDisplayValue(cell, raw);
        value = (raw > 255u) ? 255u : (uint8_t)raw;
    } else if (dtype == DTYPE_TARGET_SELECTION_VELO && cell &&
               cell->kind == MENU_CELL_INSTRUMENT) {
        raw = menu_velocityTargetDisplayValue(cell, raw);
        value = (raw > 255u) ? 255u : (uint8_t)raw;
    } else if (dtype == DTYPE_VOICE_LFO && menu_cellIsLfoTargetVoice(cell)) {
        if (raw < INSTRUMENT_TARGET_VOICE_FIRST)
            raw = INSTRUMENT_TARGET_VOICE_FIRST;
        else if (raw > INSTRUMENT_TARGET_VOICE_SCENE)
            raw = INSTRUMENT_TARGET_VOICE_SCENE;
        value = (uint8_t)raw;
    }

    if (raw == INSTRUMENT_PARAM_INVALID &&
        (dtype == DTYPE_TARGET_SELECTION_VELO ||
         dtype == DTYPE_TARGET_SELECTION_LFO ||
         dtype == DTYPE_AUTOM_TARGET)) {
        memcpy(valueAsText, menuText_off, 3);
        return;
    }
    if (cell && cell->kind == MENU_CELL_SCENE_SETTING &&
        cell->scene_setting == MENU_SCENE_SETTING_FADER_SETTING) {
        menu_sceneSettingFaderName(value, valueAsText);
        return;
    }

    switch (dtype) {
    case DTYPE_TARGET_SELECTION_VELO:
    case DTYPE_TARGET_SELECTION_LFO:
        if (cell->kind == MENU_CELL_INSTRUMENT)
            menu_formatInstrumentTargetShort(raw, valueAsText);
        else
            menu_displayModTargetShort(value, valueAsText);
        break;
    case DTYPE_AUTOM_TARGET:
        /*
         * Do not revive the legacy voice-prefixed compact target text
         * ("1wa", "1co", ...). Step-automation targeting is scheduled for a
         * Phase 4 display rewrite, and current target browsers should show the
         * plain target short name only.
         */
        menu_displayModTargetShort(value, valueAsText);
        break;
    case DTYPE_PM63:
        numtostrps(valueAsText, (int8_t)(value - 63));
        break;
    case DTYPE_NOTE_NAME:
        if (cell->kind == MENU_CELL_STATIC &&
            cell->static_param == PAR_TRACK_MIDI_NOTE && value == 0u) {
            memcpy(valueAsText, menuText_any, 3);
        } else {
            setNoteName(value, valueAsText);
        }
        break;
    case DTYPE_MIX_FM:
        if (value == 1u) memcpy(valueAsText, menuText_mix, 3);
        else            memcpy(valueAsText, menuText_fm, 3);
        break;
    case DTYPE_ON_OFF:
        if (value == 1u) memcpy(valueAsText, menuText_on, 3);
        else            memcpy(valueAsText, menuText_off, 3);
        break;
    case DTYPE_LFO_POLARITY:
        menu_getLfoPolarityName(value, valueAsText);
        break;
    case DTYPE_MENU:
        getMenuItemNameForValue((uint8_t)(menu_cellDtype(cell) >> 4),
                                value, valueAsText);
        break;
    case DTYPE_0b1:
        numtostrpu(valueAsText, (uint8_t)(value + 1u), ' ');
        break;
    default:
    case DTYPE_0B127:
    case DTYPE_0B255:
    case DTYPE_1B16:
    case DTYPE_0B15:
    case DTYPE_VOICE_LFO:
        if (menu_cellIsLfoTargetVoice(cell) &&
            value == INSTRUMENT_TARGET_VOICE_SCENE) {
            memcpy(valueAsText, "scn", 3);
        } else {
            numtostrpu(valueAsText, value, ' ');
        }
        break;
    }
}

static void menu_clampCellValue(const menu_cell_t *cell, uint16_t *value)
{
    uint8_t dtype;
    if (!cell || !value)
        return;

    if (cell->kind == MENU_CELL_SCENE_SETTING) {
        /*
         * Clamp Scene-setting cells before generic dtype handling.
         *
         * audio_out uses the six-entry mixer route menu, FX send uses 0..127,
         * and fader mode uses 0..2 even though it borrows DTYPE_0B15 for basic
         * numeric editing/display plumbing.
         */
        if (cell->scene_setting == MENU_SCENE_SETTING_AUDIO_OUT) {
            if (*value > 5u)
                *value = 5u;
        } else if (cell->scene_setting == MENU_SCENE_SETTING_FADER_SETTING) {
            if (*value > 2u)
                *value = 2u;
        } else if (*value > 127u) {
            *value = 127u;
        }
        return;
    }

    dtype = (uint8_t)(menu_cellDtype(cell) & 0x0f);
    switch (dtype) {
    case DTYPE_AUTOM_TARGET: {
        uint8_t nmt = getNumModTargets();
        if (*value >= nmt)
            *value = (nmt > 0u) ? (uint16_t)(nmt - 1u) : 0u;
        break; }
    case DTYPE_TARGET_SELECTION_VELO:
    case DTYPE_TARGET_SELECTION_LFO:
        if (cell && cell->kind == MENU_CELL_INSTRUMENT) {
            /*
             * Instrument target selector cells retain byte tokens.
             *
             * Display helpers may temporarily expand those tokens to canonical
             * IDs, but commits to SceneData must stay in the compact storage
             * domain. Stale generic edits normalize to byte off rather than
             * leaking a display/runtime ID back into the parameter image.
             */
            if (*value > 255u)
                *value = INSTRUMENT_TARGET_TOKEN_OFF;
            break;
        }
        if (*value == INSTRUMENT_PARAM_INVALID)
            break;
        if (*value >= INSTRUMENT_VOICE_ID_COUNT &&
            !sceneModTarget_isSceneTarget(*value))
            *value = INSTRUMENT_PARAM_INVALID;
        break;
    case DTYPE_0B255:
        if (*value > 255u) *value = 255u;
        break;
    case DTYPE_1B16:
        if (*value < 1u) *value = 1u;
        else if (*value > 16u) *value = 16u;
        break;
    case DTYPE_1B128:
        if (*value < 1u) *value = 1u;
        else if (*value > 128u) *value = 128u;
        break;
    case DTYPE_0B15:
        if (*value > 15u) *value = 15u;
        break;
    case DTYPE_MIX_FM:
    case DTYPE_ON_OFF:
    case DTYPE_0b1:
        if (*value > 1u) *value = 1u;
        break;
    case DTYPE_LFO_POLARITY: {
        uint8_t n = (uint8_t)lfoPolarityNames[0][0];
        if (n == 0u)
            *value = 0u;
        else if (*value >= n)
            *value = (uint16_t)(n - 1u);
        break; }
    case DTYPE_MENU: {
        uint8_t n = getMaxEntriesForMenu((uint8_t)(menu_cellDtype(cell) >> 4));
        if (n == 0u)
            *value = 0u;
        else if (*value >= n)
            *value = (uint16_t)(n - 1u);
        break; }
    default:
    case DTYPE_0B127:
    case DTYPE_PM63:
    case DTYPE_NOTE_NAME:
    case DTYPE_VOICE_LFO:
        if (*value > 127u) *value = 127u;
        break;
    }
    if (menu_cellIsLfoTargetVoice(cell)) {
        if (*value < INSTRUMENT_TARGET_VOICE_FIRST)
            *value = INSTRUMENT_TARGET_VOICE_FIRST;
        else if (*value > INSTRUMENT_TARGET_VOICE_SCENE)
            *value = INSTRUMENT_TARGET_VOICE_SCENE;
    }
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

static uint8_t menu_currentSaveWouldOverwrite(void)
{
    /*
     * Compute persistent overwrite display from product identity.
     *
     * What: Returns nonzero whenever the pending Save action targets an
     * existing product object. For numbered saves, the slot number is the
     * identity. For root Instrument Save, the target filename plus type
     * extension is matched case-insensitively inside Instrument/.
     *
     * Why: `OW` warns about replacement, not about whether the edited text
     * differs from the old display name. A save to an occupied slot overwrites
     * even when the name is unchanged, and a case-only Instrument filename
     * match overwrites on FAT.
     *
     * Inputs: active Save submode, target slot, nested Instrument source type,
     * and the edited root Instrument stem. Output: nonzero for the `OW` LCD
     * affordance; no filesystem or menu state is mutated.
     *
     * Affiliates/clients: menu_repaintLoadSavePage(), filesystem slot caches,
     * filesystem_instrumentTargetExists(), Save OK click handlers.
     */
    if (menu_activePage != SAVE_PAGE)
        return 0u;
    if (menu_instrumentLoadActive && menu_instrumentSaveMode) {
        const kit_instrument_slot_t *slot =
            scene_instrumentSlotConst(menu_instrumentLoadScene,
                                      menu_instrumentLoadSlot);
        instrument_type_t type = slot ? slot->type : menu_instrumentLoadType;
        /*
         * Use the live source slot type for root Instrument overwrite.
         *
         * What: Derives the extension/type identity from SceneData at repaint
         * time, falling back to Menu's cached type only if the source slot is
         * unavailable.
         *
         * Why: the save request itself revalidates the live source slot before
         * writing. The `OW` indicator should match that accepted request target
         * if a resident instrument type changed after entering nested Save.
         *
         * Inputs: selected source Scene/voice and editable Instrument stem.
         * Outputs: nonzero when `stem.ext` already exists in the typed root
         * Instrument cache under case-insensitive comparison.
         *
         * Affiliates/clients: menu_instrumentSaveRequestSelection(),
         * filesystem_requestSaveInstrument(), filesystem_instrumentTargetExists().
         */
        return filesystem_instrumentTargetExists(type,
                                                 menu_instrumentSaveName);
    }
    if (menu_saveOptions.what == SAVE_TYPE_KIT ||
        menu_saveOptions.what == SAVE_TYPE_KIT_MORPH) {
        return filesystem_kitSlotExists(
            menu_currentPresetNr[menu_saveOptions.what]);
    }
    if (menu_saveOptions.what == SAVE_TYPE_SCENE) {
        /*
         * Scene Save replaces the numbered root Scene directory for the target
         * slot. Inputs: the direct 000..999 Scene slot currently shown on the
         * Save page. Output: nonzero makes the confirmation label `OW` instead
         * of `ok`, matching the actual recursive replacement that Scene Save
         * performs before writing sceneset.scg and its child directories.
         */
        return filesystem_sceneSlotExists(
            menu_currentPresetNr[SAVE_TYPE_SCENE]);
    }
    if (menu_saveOptions.what == SAVE_TYPE_BANK) {
        /*
         * Bank Save replaces/updates one root Bank slot.
         *
         * Inputs: direct root Bank slot 000..999. Output: nonzero selects the
         * `OW` confirmation label. Bank-local child Scenes are not queried here;
         * overwrite identity at this menu level is the root Bank directory.
         */
        return filesystem_bankSlotExists(
            menu_currentPresetNr[SAVE_TYPE_BANK]);
    }
    return 0u;
}

static uint8_t menu_loadSaveTypeIsRestored(uint8_t what)
{
    /*
     * Gate the promoted Load/Save type list in one place.
     *
     * File/Dir/sDir are still compiled asyncfatfs diagnostics, but their menu
     * reachability is a build-time development choice. CONFIG_DEV_MODE owns
     * only whether those test entries appear in the type cycle; the dispatch
     * branches and filesystem diagnostic helpers stay present for storage work.
     */
#if CONFIG_DEV_MODE
    if (what == SAVE_TYPE_FILE || what == SAVE_TYPE_DIR)
        return 1u;
    if (menu_activePage == SAVE_PAGE && what == SAVE_TYPE_SIMPLE_DIR)
        return 1u;
#endif
    if (menu_activePage == SAVE_PAGE &&
        (what == SAVE_TYPE_KIT ||
         what == SAVE_TYPE_KIT_MORPH ||
         what == SAVE_TYPE_SCENE ||
         what == SAVE_TYPE_BANK))
        return 1u;
    if (menu_activePage == LOAD_PAGE) {
        return (uint8_t)(what == SAVE_TYPE_KIT ||
                         what == SAVE_TYPE_KIT_MORPH ||
                         what == SAVE_TYPE_SCENE ||
                         what == SAVE_TYPE_BANK);
    }
    return 0u;
}

/*
 * Pot-1 and encoder type cycling use explicit lists instead of enum order.
 *
 * The enum still contains legacy/future save types so lower layers compile,
 * but UI reachability is the promotion gate. Adding a type to these arrays is
 * therefore a deliberate hardware-tested operation, not a side effect of
 * where a SAVE_TYPE_* value sits numerically.
 */
static const uint8_t menu_loadSaveLoadTypes[] = {
#if CONFIG_DEV_MODE
    SAVE_TYPE_FILE,
    SAVE_TYPE_DIR,
#endif
    SAVE_TYPE_KIT,
    SAVE_TYPE_KIT_MORPH,
    SAVE_TYPE_SCENE,
    SAVE_TYPE_BANK
};

static const uint8_t menu_loadSaveSaveTypes[] = {
#if CONFIG_DEV_MODE
    SAVE_TYPE_FILE,
    SAVE_TYPE_DIR,
    SAVE_TYPE_SIMPLE_DIR,
#endif
    SAVE_TYPE_KIT,
    SAVE_TYPE_KIT_MORPH,
    SAVE_TYPE_SCENE,
    SAVE_TYPE_BANK
};

static uint8_t menu_nextRestoredLoadSaveType(uint8_t current, int8_t inc)
{
    const uint8_t *restored_types =
        (menu_activePage == LOAD_PAGE) ? menu_loadSaveLoadTypes
                                       : menu_loadSaveSaveTypes;
    uint8_t count = (menu_activePage == LOAD_PAGE)
        ? (uint8_t)(sizeof(menu_loadSaveLoadTypes) /
                    sizeof(menu_loadSaveLoadTypes[0]))
        : (uint8_t)(sizeof(menu_loadSaveSaveTypes) /
                    sizeof(menu_loadSaveSaveTypes[0]));
    uint8_t index = 0u;
    uint8_t i;

    /*
     * Step through only the currently validated Load/Save entries.
     *
     * Inputs: current SAVE_TYPE_* and signed encoder direction. Output: the
     * next reachable type with wraparound. Keeping this as a small whitelist
     * prevents stale compiled branches from becoming panel-reachable just
     * because enum order happens to place them near Kit.
     */
    for (i = 0u; i < count; i++) {
        if (restored_types[i] == current) {
            index = i;
            break;
        }
    }
    if (inc < 0)
        index = (uint8_t)((index + count - 1u) % count);
    else if (inc > 0)
        index = (uint8_t)((index + 1u) % count);
    return restored_types[index];
}

static uint8_t menu_testObjectCount(uint8_t what)
{
    /*
     * Return the cached root browser count for the temporary File/Dir menus.
     *
     * Menu owns only the selected index in menu_currentPresetNr[]; filesystem
     * owns the immutable exact-case names discovered by asyncfatfs.
     */
    return (what == SAVE_TYPE_DIR) ? filesystem_testDirCount()
                                   : filesystem_testFileCount();
}

static const char *menu_testObjectName(uint8_t what, uint8_t index)
{
    return (what == SAVE_TYPE_DIR) ? filesystem_testDirName(index)
                                   : filesystem_testFileName(index);
}

static const char *menu_currentTestName(void)
{
    /*
     * Select the exact display component used by the current test operation.
     *
     * Load pages read from the filesystem scan cache; Save pages use the
     * bounded editor buffer. The storage request copies the returned text again
     * before it starts, so later encoder movement cannot retarget an in-flight
     * async operation.
     */
    if (menu_activePage == LOAD_PAGE) {
        uint8_t what = menu_saveOptions.what;
        uint8_t count = menu_testObjectCount(what);
        uint16_t index = menu_currentPresetNr[what];
        if (count == 0u)
            return "";
        if (index >= count)
            index = (uint16_t)(count - 1u);
        return menu_testObjectName(what, (uint8_t)index);
    }
    return menu_testEditName;
}

static void menu_requestTestScan(uint8_t what)
{
    /*
     * Refresh the root File/Dir test cache for Load pages.
     *
     * This is posted through Preset so completion follows the same poll path as
     * other storage operations. Save pages do not scan before editing because
     * overwrite behavior is intentionally delegated to the exact "w" open.
     */
    if (menu_activePage != LOAD_PAGE)
        return;
    if ((what == SAVE_TYPE_FILE && preset_scanTestFiles()) ||
        (what == SAVE_TYPE_DIR && preset_scanTestDirs())) {
        menu_storageBusy = 1u;
    } else {
        menu_deferSelectionRequest = 1u;
    }
}

static void menu_showTestResult(void)
{
    /*
     * Draw the two-second result overlay for File/Dir tests.
     *
     * Successful byte results display all four diagnostic bytes exactly once:
     * the top row shows bytes 0 and 1, and the bottom row shows bytes 2 and 3.
     * Directory-child results show the first eight characters of the child
     * directory name. Failed File/Dir test operations show ERR explicitly so
     * storage bugs do not look like an ordinary browser repaint.
     */
    memset(editDisplayBuffer, ' ', 2u * 17u);
    if (menu_testResultError) {
        /*
         * Show File/Dir test failures explicitly.
         *
         * Silent fallback to the browser hides the exact failure mode this
         * temporary storage surface is meant to reveal. A two-second ERR
         * overlay makes failed SFN/LFN opens visible without blocking the main
         * loop.
         */
        memcpy(&editDisplayBuffer[0][6], "ERR", 3u);
        if (menu_testResultName[0] != '\0') {
            for (uint8_t i = 0u; i < 8u; i++) {
                char c = menu_testResultName[i];
                editDisplayBuffer[1][4u + i] = c ? c : ' ';
            }
        } else {
            memcpy(&editDisplayBuffer[1][6], "ERR", 3u);
        }
        return;
    }
    if (menu_testResultKind == FS_TEST_RESULT_DIRECTORY) {
        memcpy(&editDisplayBuffer[0][0], "Dir:", 4u);
        for (uint8_t i = 0u; i < 8u; i++) {
            char c = menu_testResultName[i];
            editDisplayBuffer[0][5u + i] = c ? c : ' ';
        }
        memcpy(&editDisplayBuffer[1][0], "Dir:", 4u);
        for (uint8_t i = 0u; i < 8u; i++) {
            char c = menu_testResultName[i];
            editDisplayBuffer[1][5u + i] = c ? c : ' ';
        }
        return;
    }

    for (uint8_t row = 0u; row < 2u; row++) {
        for (uint8_t i = 0u; i < 2u; i++) {
            static const char hex[] = "0123456789ABCDEF";
            uint8_t col = (uint8_t)(i * 8u);
            uint8_t b = menu_testResultBytes[(row * 2u) + i];
            /*
             * Display all four diagnostic bytes exactly once.
             *
             * This layout fits two 0xNN tokens per 16-cell row and makes
             * byte-order/persistence errors visible. Repeating the top row
             * would hide short reads and duplicated payload generation.
             */
            editDisplayBuffer[row][col + 0u] = '0';
            editDisplayBuffer[row][col + 1u] = 'x';
            editDisplayBuffer[row][col + 2u] = hex[(b >> 4u) & 0x0fu];
            editDisplayBuffer[row][col + 3u] = hex[b & 0x0fu];
        }
    }
}

static uint8_t menu_completedOpIsTest(preset_op_type_t op)
{
    return (uint8_t)(op == PRESET_OP_TEST_SCAN ||
                     op == PRESET_OP_TEST_FILE_LOAD ||
                     op == PRESET_OP_TEST_DIR_LOAD ||
                     op == PRESET_OP_TEST_FILE_SAVE ||
                     op == PRESET_OP_TEST_DIR_SAVE);
}

static void menu_showFilesystemErrorOverlay(void)
{
    const char *code = filesystem_errorCode();

    menu_storageBusy = 0u;
    menu_testResultError = 1u;
    menu_testResultKind = FS_TEST_RESULT_BYTES_READY;
    memset(menu_testResultBytes, 0, sizeof(menu_testResultBytes));
    memset(menu_testResultName, 0, sizeof(menu_testResultName));
    if (code && code[0] != '\0')
        strncpy(menu_testResultName, code, FS_TEST_NAME_MAX);
    else
        strncpy(menu_testResultName, "FsErr", FS_TEST_NAME_MAX);
    menu_testResultActive = 1u;
    menu_testResultStart = time_sysTick;
    menu_repaintAll();
}

/* Request the filesystem action that corresponds to the current Load/Save page
 * selection.
 *
 * Inputs: whether a Load-page Kit number movement should load immediately;
 * selected UI type/slot and the Kit Scene mask are read from Menu state.
 * Outputs: starts a Preset request or records a retry after the one filesystem
 * operation slot becomes free. Clients: menu_handleLoadSaveMenu() and Load
 * page entry. Kit uses preset_loadKitForScenes() because the menu owns Scene
 * selection, while Settings/Samples retain their existing name/modal paths.
 */
static void menu_requestCurrentLoadSaveSelection(uint8_t loadKitOnLoadPage)
{
    uint8_t what = menu_saveOptions.what;
    uint16_t slot = (what < SAVE_TYPE_GLO) ? menu_currentPresetNr[what] : 0u;

    menu_deferSelectionRequest = 0;
    menu_deferSelectionLoadKit = loadKitOnLoadPage;
    if (what == SAVE_TYPE_FILE || what == SAVE_TYPE_DIR) {
        /*
         * Temporary asyncfatfs test entries do not use legacy preset-name
         * readers or numbered Kit/Scene slots. On Load, refresh the root
         * object cache; on Save, the visible name comes from menu_testEditName
         * and no filesystem request is needed until OK is clicked.
         */
        menu_requestTestScan(what);
        return;
    }
    if (what >= SAVE_TYPE_GLO) {
        preset_loadName(0, what);
        return;
    }
    if ((what == SAVE_TYPE_KIT || what == SAVE_TYPE_KIT_MORPH) &&
        !filesystem_libraryNameCacheLoaded(FS_LIBRARY_INDEX_KIT)) {
        menu_requestLibraryIndexLoad(what);
        return;
    }
    if (what == SAVE_TYPE_SCENE &&
        !filesystem_libraryNameCacheLoaded(FS_LIBRARY_INDEX_SCENE)) {
        menu_requestLibraryIndexLoad(what);
        return;
    }
    if (what == SAVE_TYPE_BANK &&
        !filesystem_libraryNameCacheLoaded(FS_LIBRARY_INDEX_BANK)) {
        menu_requestLibraryIndexLoad(what);
        return;
    }
    /*
     * Kit and KitMrp share the same browser slot but have different commit
     * semantics.
     *
     * Normal Kit Load replaces the selected Scene kits. KitMrp stages the same
     * Kit/ directory, then Preset copies staged normal endpoint values into the
     * already-loaded kits' morph endpoints. The display-name refresh path must
     * not call filesystem_requestLoadName() for either entry because new-format
     * Kit directories do not use the legacy flat name reader.
     */
    /*
     * Only Kit/KitMrp suppress automatic Load-page name/payload requests while
     * browsing without OK. Scene and Bank are also explicit-OK loads, but they
     * still need the code below: Scene copies its cached root name to the LCD, and
     * Bank additionally starts a read-only child-Scene preview so the SEQ LEDs are
     * correct immediately when entering Load:[Bank].
     */
    if (menu_activePage == LOAD_PAGE && what < SAVE_TYPE_GLO &&
        !loadKitOnLoadPage &&
        what != SAVE_TYPE_SCENE &&
        what != SAVE_TYPE_BANK)
        return;
    if (loadKitOnLoadPage && menu_activePage == LOAD_PAGE &&
        (what == SAVE_TYPE_KIT || what == SAVE_TYPE_KIT_MORPH) &&
        !filesystem_kitSlotExists(slot)) {
        /*
         * Empty Kit browser slots are normal UI state, not failed filesystem
         * operations. The Kit/ scan cache already proved there is no numbered
         * directory here, so do not start a load that would report KitL00.
         */
        memcpy(preset_currentName, filesystem_kitSlotName(slot), 8u);
        menu_repaintAll();
        return;
    }
    if (loadKitOnLoadPage && menu_activePage == LOAD_PAGE &&
        what == SAVE_TYPE_KIT) {
        if (!preset_loadKitForScenes(slot, menu_kitLoadSceneMask))
            menu_deferSelectionRequest = 1;
    } else if (loadKitOnLoadPage && menu_activePage == LOAD_PAGE &&
               what == SAVE_TYPE_KIT_MORPH) {
        if (!preset_loadKitMorphForScenes(slot, menu_kitLoadSceneMask))
            menu_deferSelectionRequest = 1;
    } else if (what == SAVE_TYPE_SCENE || what == SAVE_TYPE_BANK) {
        /*
         * Scene and Bank folders are explicit-OK operations.
         *
         * Encoder movement updates the displayed root library name only.
         * Unlike Kit Load, it must not start a Scene replacement while the user
         * scrolls across slots. Bank follows the same load rule, but Load:Bank
         * also posts a read-only child scan so the SEQ LEDs represent only
         * Scene folders present inside the highlighted Bank slot.
         */
        memcpy(preset_currentName,
               (what == SAVE_TYPE_BANK)
                   ? filesystem_bankSlotName(slot)
                   : filesystem_sceneSlotName(slot),
               8u);
        if (menu_activePage == LOAD_PAGE && what == SAVE_TYPE_BANK)
            menu_requestBankLoadPreview(slot);
    } else {
        preset_loadName(slot, what);
        if (preset_getStatus() != PRESET_LOAD_IN_PROGRESS)
            menu_deferSelectionRequest = 1;
    }
}

static void menu_instrumentIndexLoadComplete(void)
{
    /*
     * Finish one typed Instrument index load for either nested Load or Save.
     * The filesystem has replaced the selected type's general name cache;
     * clamping now makes a previously selected browser index safe when the
     * card contains fewer entries than the old cache. The same callback is
     * deliberately shared by every registry type so Drum has no special
     * browser contract left.
     */
    menu_storageBusy = 0u;
    menu_instrumentLoadClampIndex();
    menu_repaintAll();
}

static void menu_requestInstrumentIndexLoad(instrument_type_t type)
{
    /*
     * Request the selected type's own `.hcindex` whenever nested Instrument
     * Load or Save enters/selects a type. Inputs: a registry type captured by
     * Menu. Output: the UI remains locked until the foreground SD state
     * machine has loaded that type's general-purpose name cache. A rejected
     * request is deferred through the existing selection retry path, which
     * handles a still-busy filesystem without inventing a second queue.
     */
    filesystem_clearNameCache();
    menu_storageBusy = 1u;
    if (!filesystem_requestLoadInstrumentIndex(
            type, menu_instrumentIndexLoadComplete))
        menu_deferSelectionRequest = 1u;
}

static void menu_libraryIndexLoadComplete(void)
{
    /*
     * Publish one completed Kit, root Scene, or root Bank `.hcindex` load.
     *
     * The filesystem has already replaced the one shared name cache and its
     * slot occupancy map. Menu only releases the input lock and repaints; it
     * deliberately does not start a payload load because entering a top-level
     * Load row is browsing, while Scene Load remains explicit-OK and Kit
     * Load's instant-on-scroll policy is handled on later selection moves.
     */
    menu_storageBusy = 0u;
    menu_repaintAll();
}

static void menu_requestLibraryIndexLoad(uint8_t what)
{
    fs_library_index_kind_t kind;
    bool requested;

    /*
     * Request the only index that can supply the current top-level row.
     *
     * Inputs: SAVE_TYPE_KIT, SAVE_TYPE_KIT_MORPH, SAVE_TYPE_SCENE, or
     * SAVE_TYPE_BANK.
     * Output: the shared name cache is disposed before the asynchronous read,
     * keeping old-library names out of the LCD during the transition. A busy
     * filesystem is retried through Menu's existing deferred-selection path.
     */
    kind = (what == SAVE_TYPE_SCENE)
        ? FS_LIBRARY_INDEX_SCENE
        : (what == SAVE_TYPE_BANK)
            ? FS_LIBRARY_INDEX_BANK : FS_LIBRARY_INDEX_KIT;
    filesystem_clearNameCache();
    menu_storageBusy = 1u;
    requested = (kind == FS_LIBRARY_INDEX_SCENE)
        ? filesystem_requestLoadSceneIndex(menu_libraryIndexLoadComplete)
        : (kind == FS_LIBRARY_INDEX_BANK)
            ? filesystem_requestLoadBankIndex(menu_libraryIndexLoadComplete)
            : filesystem_requestLoadKitIndex(menu_libraryIndexLoadComplete);
    if (!requested)
        menu_deferSelectionRequest = 1u;
}

/* Refresh the Save page's resident display after a Kit/Scene/Bank save.
 *
 * What: copies the name from the just-rebuilt shared `.hcindex` cache into
 * preset_currentName for the slot that remains selected on the Save page.
 * Why: Save-page rendering uses preset_currentName while the save is being
 * edited; the filesystem refresh updates the shared cache but does not update
 * that UI buffer. Clearing the cache here would also make the current slot
 * appear stale or empty until the user changed type and re-entered it.
 * Inputs: completed Kit, KitMrp, root Scene, or root Bank save and the unchanged menu
 * slot. Output: the current Save type/slot stays selected and its visible name
 * matches the newly durable directory. Instrument and other saves do not use
 * this path because their name cache/domain has different lifecycle rules.
 */
static void menu_refreshSavedLibraryName(uint8_t completed_op)
{
    uint8_t what = (completed_op == PRESET_OP_SCENE_SAVE)
        ? SAVE_TYPE_SCENE
        : (completed_op == PRESET_OP_BANK_SAVE)
            ? SAVE_TYPE_BANK
            : (completed_op == PRESET_OP_KIT_MORPH_SAVE)
                ? SAVE_TYPE_KIT_MORPH : SAVE_TYPE_KIT;
    uint16_t slot = menu_currentPresetNr[what];
    const char *name = NULL;

    if (what == SAVE_TYPE_SCENE) {
        if (filesystem_sceneSlotExists(slot))
            name = filesystem_sceneSlotName(slot);
    } else if (what == SAVE_TYPE_BANK) {
        if (filesystem_bankSlotExists(slot))
            name = filesystem_bankSlotName(slot);
    } else if (filesystem_kitSlotExists(slot)) {
        name = filesystem_kitSlotName(slot);
    }
    if (name)
        memcpy(preset_currentName, name, 8u);
}

static void menu_bankLoadPreviewComplete(void)
{
    uint16_t slot = menu_currentPresetNr[SAVE_TYPE_BANK];

    /*
     * Apply one completed Load:[Bank] child preview scan.
     *
     * Inputs: filesystem_bankChildSceneMask() from the just-finished scan and
     * the Bank slot Menu requested when it started the scan. Output: if and
     * only if the user is still on Load:[Bank] for that same slot, the child
     * mask becomes both the LED selectable mask and the default Bank Load
     * request mask. This guards the async race where the encoder has already
     * scrolled to a different Bank before FAT iteration completes.
     */
    if (menu_activePage != LOAD_PAGE ||
        menu_instrumentLoadActive ||
        menu_saveOptions.what != SAVE_TYPE_BANK ||
        menu_bankLoadPreviewSlot != slot ||
        filesystem_status() != FS_STATUS_DONE) {
        return;
    }
    menu_bankLoadPreviewMask = filesystem_bankChildSceneMask();
    menu_bankLoadPreviewValid = 1u;
    menu_kitLoadSceneMask = menu_bankLoadPreviewMask;
    menu_refreshLoadSceneLeds();
    menu_repaintAll();
}

static void menu_requestBankLoadPreview(uint16_t slot)
{
    /*
     * Request the child-Scene LED preview for the highlighted Bank slot.
     *
     * Inputs: zero-based Bank slot from the preset-number row. Outputs:
     * preview validity is cleared immediately so stale child LEDs disappear;
     * missing root Bank slots use an empty mask; present root Bank slots post a
     * filesystem preview scan and defer if another operation is busy. The
     * completion callback verifies the slot again before repainting.
     */
    menu_bankLoadPreviewSlot = slot;
    menu_bankLoadPreviewMask = 0u;
    menu_bankLoadPreviewValid = 0u;
    menu_kitLoadSceneMask = 0u;
    if (!filesystem_bankSlotExists(slot)) {
        menu_bankLoadPreviewValid = 1u;
        menu_refreshLoadSceneLeds();
        return;
    }
    if (!filesystem_requestScanBankScenes(slot, menu_bankLoadPreviewComplete))
        menu_deferSelectionRequest = 1u;
    menu_refreshLoadSceneLeds();
}

static void menu_instrumentLoadClampIndex(void)
{
    uint16_t count = filesystem_instrumentCount(menu_instrumentLoadType);

    /*
     * Clamp the per-type Instrument Load browser index.
     *
     * Inputs: current selected type and cached index for that type. Output:
     * index is valid for the current filesystem cache, or zero when the list is
     * empty. This lives in Menu because Menu owns browser state while
     * filesystem owns only the immutable scan result.
     */
    if (menu_instrumentLoadType >= INSTRUMENT_TYPE_UNKNOWN)
        menu_instrumentLoadType = INSTRUMENT_TYPE_DRM;
    if (count == 0u) {
        menu_instrumentLoadIndex[menu_instrumentLoadType] = 0u;
        return;
    }
    if (menu_instrumentLoadIndex[menu_instrumentLoadType] >= count)
        menu_instrumentLoadIndex[menu_instrumentLoadType] =
            count - 1u;
}

static void menu_instrumentLoadRequestSelection(void)
{
    uint16_t count;
    uint16_t index;

    /*
     * Immediately load the selected Instrument/ file.
     *
     * Inputs: destination Scene/slot, selected type, morph-row flag, and
     * per-type browser index from Menu state. Output: normal rows replace the
     * destination slot; the morph row copies the staged file's normal endpoint
     * into the current slot's morph endpoint. Empty lists do not start a
     * request, and Preset still validates the same-type rule before accepting
     * an InstrumentMrp request.
     */
    menu_instrumentLoadClampIndex();
    count = filesystem_instrumentCount(menu_instrumentLoadType);
    if (count == 0u)
        return;
    index = menu_instrumentLoadIndex[menu_instrumentLoadType];
    if ((menu_instrumentLoadMorphMode &&
         preset_loadInstrumentMorph(menu_instrumentLoadScene,
                                    menu_instrumentLoadSlot,
                                    menu_instrumentLoadType,
                                    index)) ||
        (!menu_instrumentLoadMorphMode &&
         preset_loadInstrumentForScenes(menu_kitLoadSceneMask,
                                        menu_instrumentLoadSlot,
                                        menu_instrumentLoadType,
                                        index))) {
        /* Keep the asynchronous request exclusive without replacing the
         * Instrument browser with a transient progress screen. The cursor and
         * filename remain visible until the bounded completion/apply path
         * repaints them, which makes rapid pool browsing feel continuous. */
        menu_storageBusy = 1u;
    }
}

static void menu_instrumentLoadRefreshBaseType(uint8_t preserve_selected_type)
{
    const kit_instrument_slot_t *slot =
        scene_instrumentSlotConst(menu_instrumentLoadScene,
                                  menu_instrumentLoadSlot);
    instrument_type_t previous_type = menu_instrumentLoadType;

    /*
     * Refresh the InstrumentMrp base type from the destination slot.
     *
     * Inputs: current destination Scene/slot and whether a normal selected type
     * should be preserved when it remains legal. Output: baseType always
     * follows the resident slot type, because the only morph row allowed is the
     * currently loaded instrument type. If the selected normal type is no
     * longer valid for the destination, the browser falls back to baseType and
     * clears morph mode.
     */
    menu_instrumentLoadBaseType = slot ? slot->type : INSTRUMENT_TYPE_DRM;
    if (!instrumentManager_typeSelectableForSceneSlot(
            menu_instrumentLoadScene, menu_instrumentLoadSlot,
            menu_instrumentLoadBaseType)) {
        menu_instrumentLoadBaseType = INSTRUMENT_TYPE_DRM;
    }
    if (preserve_selected_type && !menu_instrumentLoadMorphMode &&
        instrumentManager_typeSelectableForSceneSlot(
            menu_instrumentLoadScene, menu_instrumentLoadSlot,
            previous_type)) {
        menu_instrumentLoadType = previous_type;
    } else {
        menu_instrumentLoadType = menu_instrumentLoadBaseType;
        menu_instrumentLoadMorphMode = 0u;
    }
    menu_instrumentLoadClampIndex();
}

static void menu_instrumentLoadCopyTypeLabel(char *dest)
{
    char label[9];
    const char *base_label = instrumentManager_typeDisplayLabel(
        menu_instrumentLoadType);

    /*
     * Build the fixed eight-character Load label used by the LCD.
     *
     * Normal rows show the instrument type label as before. The morph row
     * appends "Mrp" to the captured base type, producing labels such as
     * "DrumMrp ". Padding and truncation live here so repaint cannot resize or
     * corrupt the LCD field.
     */
    menu_copyPaddedField(label, base_label, 8u);
    if (menu_instrumentLoadMorphMode) {
        uint8_t len = 0u;
        while (len < 8u && label[len] != ' ')
            len++;
        if (len < 8u) label[len++] = 'M';
        if (len < 8u) label[len++] = 'r';
        if (len < 8u) label[len++] = 'p';
        while (len < 8u)
            label[len++] = ' ';
        label[8] = '\0';
    }
    memcpy(dest, label, 8u);
}

static void menu_instrumentLoadStepType(int8_t inc)
{
    uint8_t registry_count = instrumentManager_registryCount();
    uint8_t current_index = 0u;
    uint8_t i;
    int8_t direction = (inc < 0) ? -1 : 1;

    /*
     * Step through the logical Instrument Load type list.
     *
     * Inputs: signed encoder direction, current destination slot, and captured
     * base type. Output: selectable normal instrument types still follow the
     * registry order, but one extra morph row is inserted immediately after the
     * resident destination type. Other types do not expose "...Mrp" because a
     * mismatched morph load is explicitly a no-change operation.
     */
    if (registry_count == 0u || inc == 0)
        return;
    if (inc > 0 && !menu_instrumentLoadMorphMode &&
        menu_instrumentLoadType == menu_instrumentLoadBaseType) {
        menu_instrumentLoadMorphMode = 1u;
        return;
    }
    if (inc < 0 && menu_instrumentLoadMorphMode) {
        menu_instrumentLoadMorphMode = 0u;
        return;
    }
    for (i = 0u; i < registry_count; i++) {
        const instrument_registry_entry_t *entry =
            instrumentManager_registryEntryAt(i);
        if (entry && entry->type == menu_instrumentLoadType) {
            current_index = i;
            break;
        }
    }

    for (i = 0u; i < registry_count; i++) {
        uint8_t next_index;
        const instrument_registry_entry_t *entry;
        if (direction > 0)
            next_index = (uint8_t)((current_index + 1u + i) % registry_count);
        else
            next_index = (uint8_t)((current_index + registry_count - 1u - i) %
                                   registry_count);
        entry = instrumentManager_registryEntryAt(next_index);
        if (entry &&
            instrumentManager_typeSelectableForSceneSlot(
                menu_instrumentLoadScene, menu_instrumentLoadSlot,
                entry->type)) {
            menu_instrumentLoadType = entry->type;
            menu_instrumentLoadMorphMode =
                (uint8_t)(direction < 0 &&
                          entry->type == menu_instrumentLoadBaseType);
            menu_instrumentLoadClampIndex();
            if (!menu_instrumentLoadMorphMode)
                menu_requestInstrumentIndexLoad(menu_instrumentLoadType);
            return;
        }
    }
}

static void menu_instrumentSaveSeedName(void)
{
    const scene_t *scene = scene_getConst(menu_instrumentLoadScene);
    const kit_t *kit = scene ? &scene->kit : NULL;
    const char *source = "Inst    ";
    uint8_t i;
    uint8_t non_space = 0u;

    /*
     * Seed nested Instrument Save from the selected resident kit slot.
     *
     * Inputs: source Scene/voice retained by Instrument mode. Output: an
     * eight-character editable stem. The source is the slot's display name,
     * not a root Instrument browser entry, because Save exports the currently
     * loaded resident instrument even when it originally came from a Kit
     * folder or was edited in place.
     */
    if (kit && menu_instrumentLoadSlot < INSTRUMENT_SLOT_COUNT)
        source = kit->instrument_display_name[menu_instrumentLoadSlot];
    for (i = 0u; i < MENU_INSTRUMENT_SAVE_NAME_LEN; i++) {
        char c = source[i];
        if (c == '\0')
            c = ' ';
        menu_instrumentSaveName[i] = c;
        if (c != ' ')
            non_space = 1u;
    }
    if (!non_space)
        memcpy(menu_instrumentSaveName, "Inst    ",
               MENU_INSTRUMENT_SAVE_NAME_LEN);
    menu_instrumentSaveName[MENU_INSTRUMENT_SAVE_NAME_LEN] = '\0';
}

static void menu_instrumentSaveRequestSelection(void)
{
    uint8_t accepted;

    /*
     * Start one root Instrument Save from nested Save mode.
     *
     * Inputs are copied by Preset/filesystem at request acceptance time: source
     * Scene, source voice slot, the Normal/Morph projection selector, and the
     * editable eight-character stem. Output is menu_storageBusy only when the
     * async request was accepted, so failed validation leaves the menu
     * interactive instead of getting stuck.
     *
     * Affiliates: preset_saveInstrument() writes the normal endpoint image,
     * while preset_saveInstrumentMorph() writes the current interpolated Morph
     * endpoint into both [params] and [morph].
     */
    accepted = menu_instrumentLoadMorphMode
        ? preset_saveInstrumentMorph(menu_instrumentLoadScene,
                                     menu_instrumentLoadSlot,
                                     menu_instrumentSaveName)
        : preset_saveInstrument(menu_instrumentLoadScene,
                                menu_instrumentLoadSlot,
                                menu_instrumentSaveName);
    if (accepted) {
        menu_storageBusy = 1u;
    }
}

static uint8_t menu_instrumentSaveStepSelectionState(uint8_t state,
                                                     int8_t inc)
{
    /*
     * Move the nested Instrument Save selection cursor over visible fields.
     *
     * What: Skips SAVE_STATE_EDIT_PRESET_NR, which is a valid top-level
     * Load/Save row but has no meaning in root Instrument Save.
     *
     * Why: Instrument Save has only three selectable regions: the top
     * Normal/Mrp projection row, the eight filename characters, and OK/OW.
     * Letting selection movement pass through the hidden preset-number state
     * makes the type row feel unreachable and leaves the LCD without a cursor
     * for one encoder detent.
     *
     * Inputs: current save state and signed encoder delta while edit mode is
     * off. Output: the next visible Instrument Save state, clamped at both
     * ends. Affiliates/clients: menu_handleLoadSaveMenu(),
     * menu_repaintLoadSavePage(), menu_loadInstrumentVoicePressed().
     */
    if (inc < 0) {
        if (state == SAVE_STATE_OK)
            return SAVE_STATE_EDIT_NAME8;
        if (state > SAVE_STATE_EDIT_NAME1 &&
            state <= SAVE_STATE_EDIT_NAME8)
            return (uint8_t)(state - 1u);
        return SAVE_STATE_EDIT_TYPE;
    }
    if (inc > 0) {
        if (state == SAVE_STATE_EDIT_TYPE)
            return SAVE_STATE_EDIT_NAME1;
        if (state >= SAVE_STATE_EDIT_NAME1 &&
            state < SAVE_STATE_EDIT_NAME8)
            return (uint8_t)(state + 1u);
        if (state == SAVE_STATE_EDIT_NAME8)
            return SAVE_STATE_OK;
    }
    return state;
}

uint8_t menu_loadInstrumentIsActive(void)
{
    return menu_instrumentLoadActive;
}

static uint16_t menu_residentPresentSceneMask(void);
static uint16_t menu_allPhysicalSceneMask(void);
static uint16_t menu_loadSaveSelectableSceneMask(void);
void menu_refreshPerfSceneLeds(void)
{
    uint8_t scene_index;
    uint8_t active_scene = scene_getActiveIndex();
    uint16_t present_mask = menu_residentPresentSceneMask();

    /*
     * Repaint PERF-mode Scene selection on the 16 SEQ LEDs.
     *
     * Inputs: BankData's resident Scene-present mask, SceneData's active index,
     * and PatternData step activity per Scene. Outputs: Scenes with any retained
     * Pattern steps are lit steady, and the active resident Scene blinks even
     * when its Pattern is empty. PERF selection is still gated by the resident
     * present mask in menu_perfModeSceneButtonPressed(); the steady LED layer is
     * deliberately Pattern activity because the row answers "which Scenes have
     * something to play?" rather than "which child folders exist?".
     */
    for (scene_index = 0u; scene_index < 16u; scene_index++) {
        uint16_t bit = (uint16_t)(1u << scene_index);
        led_setBlinkLed((uint8_t)(LED_SEQ1 + scene_index), 0u);
        led_setValue(((present_mask & bit) != 0u) &&
                         pat_sceneHasActiveSteps(scene_index),
                     (uint8_t)(LED_SEQ1 + scene_index));
    }
    if (active_scene < 16u && (present_mask & (uint16_t)(1u << active_scene)))
        led_setBlinkLed((uint8_t)(LED_SEQ1 + active_scene), 1u);
}

void menu_perfModeSceneButtonPressed(uint8_t scene_index)
{
    /*
     * Switch the active resident Scene from PERF mode.
     *
     * Input: physical SEQ button index 0..15. Output: SceneData and BankData
     * active Scene records, viewed Pattern, and Sequencer runtime Pattern are
     * updated together. BankData drops scene_mask_voice_edit to the new active
     * Scene only when the new active Scene was not already in the edit set, and
     * Preset starts the bounded DSP apply for the newly audible Scene.
     */
    if (scene_index >= SCENE_COUNT || scene_index >= 16u ||
        !bank_scenePresent(scene_index)) {
        return;
    }
    scene_selectActive(scene_index);
    bank_selectActiveSceneForEditMask(scene_index);
    /*
     * PERF Scene selection maps one-to-one onto PatternData's Scene slot.
     *
     * Inputs: validated Scene button index. Outputs: Menu edits now target that
     * Scene's Pattern, and Sequencer playback reads the same Pattern immediately.
     * This keeps step edits single-Scene only; parameter fan-out continues to use
     * scene_mask_voice_edit and is not consulted by PatternData.
     */
    menu_setShownPattern(scene_index);
    seq_selectActivePattern(scene_index);
    preset_startDrumsetApply();
    menu_refreshPerfSceneLeds();
    menu_repaintAll();
}

void menu_refreshVoiceHeldSceneLeds(void)
{
    uint8_t scene_index;
    uint8_t active_scene = scene_getActiveIndex();
    uint16_t present_mask = menu_residentPresentSceneMask();
    uint16_t edit_mask = bank_sceneMaskVoiceEdit();

    /*
     * Repaint the temporary MODE VOICE held edit-mask view.
     *
     * Inputs: BankData's resident present mask, BankData's
     * scene_mask_voice_edit, and SceneData's active Scene. Outputs: every Scene
     * currently in the voice-edit fan-out mask is lit steady, and the active
     * Scene blinks when present. This is separate from PERF LEDs because PERF
     * shows selectable/active Scenes, while MODE VOICE held shows the edit
     * fan-out set that Scene parameter writes will address.
     */
    for (scene_index = 0u; scene_index < 16u; scene_index++) {
        uint16_t bit = (uint16_t)(1u << scene_index);
        led_setBlinkLed((uint8_t)(LED_SEQ1 + scene_index), 0u);
        led_setValue((uint8_t)((edit_mask & present_mask & bit) != 0u),
                     (uint8_t)(LED_SEQ1 + scene_index));
    }
    if (active_scene < 16u && (present_mask & (uint16_t)(1u << active_scene)))
        led_setBlinkLed((uint8_t)(LED_SEQ1 + active_scene), 1u);
}

uint8_t menu_voiceHeldSceneButtonPressed(uint8_t scene_index)
{
    /*
     * Toggle one Scene in the VOICE edit fan-out mask.
     *
     * Input: physical SEQ button index while VOICE is held. Output: BankData's
     * scene_mask_voice_edit flips that Scene bit when the Scene is present.
     * The active Scene cannot be removed because BankData normalizes the mask
     * after every toggle. The function returns nonzero when it consumes the
     * button so ButtonHandler does not reinterpret the press as step editing.
     */
    if (scene_index >= SCENE_COUNT || scene_index >= 16u)
        return 0u;
    if (!bank_scenePresent(scene_index))
        return 1u;
    bank_toggleSceneMaskVoiceEdit(scene_index);
    menu_refreshVoiceHeldSceneLeds();
    led_flashGroup(LED_FLASH_GROUP_SEQ, (uint16_t)(1u << scene_index));
    menu_repaintAll();
    return 1u;
}

static uint16_t menu_residentPresentSceneMask(void)
{
    uint16_t mask = bank_scenePresentMask();

    /*
     * Return the resident Scene availability mask used by Load/Save.
     *
     * Inputs: BankData's present mask. Output: a nonzero 16-bit mask when at
     * least one resident Scene is available. The fallback to the active Scene
     * keeps root/default boot usable before a Bank has supplied an explicit
     * child-present map.
     */
    if (mask == 0u && scene_getActiveIndex() < 16u)
        mask = (uint16_t)(1u << scene_getActiveIndex());
    return mask;
}

static uint16_t menu_allPhysicalSceneMask(void)
{
    uint8_t scene_index;
    uint16_t mask = 0u;

    /*
     * Return the mask of physical Scene buttons this build can address.
     *
     * Inputs: SCENE_COUNT and the fixed 16-button SEQ row. Output: a bounded
     * mask used by Load destination selection, where an empty resident Scene is
     * still a legal destination because loading Scene/Kit/Instrument data can
     * populate it. The explicit loop avoids undefined shifts if SCENE_COUNT
     * changes again.
     */
    for (scene_index = 0u;
         scene_index < SCENE_COUNT && scene_index < 16u;
         scene_index++) {
        mask = (uint16_t)(mask | (uint16_t)(1u << scene_index));
    }
    return mask;
}

static uint16_t menu_loadSaveSelectableSceneMask(void)
{
    /*
     * Resolve which Scene buttons the current Load/Save context may consume.
     *
     * Outputs:
     * - Load Kit/KitMrp/Scene/Instrument can target any physical Scene slot.
     * - Save Kit/KitMrp/Scene/Instrument can source only resident filled Scenes.
     * - Save Bank can include only resident filled Scenes.
     * - Load Bank can request only child Scene folders found in the highlighted
     *   Bank slot's preview scan.
     *
     * This distinction keeps "filled" as a source/Bank-child constraint, not a
     * general Load destination constraint.
     */
    if (menu_instrumentLoadActive)
        return menu_instrumentSaveMode
            ? menu_residentPresentSceneMask()
            : menu_allPhysicalSceneMask();
    if (menu_activePage == SAVE_PAGE)
        return menu_residentPresentSceneMask();
    if (menu_activePage == LOAD_PAGE &&
        menu_saveOptions.what == SAVE_TYPE_BANK) {
        uint16_t slot = menu_currentPresetNr[SAVE_TYPE_BANK];
        if (menu_bankLoadPreviewValid &&
            menu_bankLoadPreviewSlot == slot) {
            return menu_bankLoadPreviewMask;
        }
        return 0u;
    }
    if (menu_activePage == LOAD_PAGE && menu_saveOptions.what < SAVE_TYPE_GLO)
        return menu_allPhysicalSceneMask();
    return 0u;
}

static void menu_resetLoadSaveSceneSelection(void)
{
    uint16_t active_bit = (scene_getActiveIndex() < 16u)
        ? (uint16_t)(1u << scene_getActiveIndex())
        : 1u;
    uint16_t present_mask = menu_residentPresentSceneMask();

    /*
     * Reset the Scene-selection surface for the current Load/Save context.
     *
     * Inputs: active page/type, active Scene, and resident present mask.
     * Outputs: menu_kitLoadSceneMask becomes the multi-destination or multi-
     * child mask for operations that toggle Scenes; menu_loadSaveSourceScene
     * becomes the single source for save operations that write one object. Bank
     * Save defaults to every resident non-empty Scene, while other musical
     * operations default to the active Scene.
     */
    menu_loadSaveSourceScene = scene_getActiveIndex();
    if (menu_activePage == SAVE_PAGE && menu_saveOptions.what == SAVE_TYPE_BANK)
        menu_kitLoadSceneMask = present_mask;
    else if (menu_activePage == LOAD_PAGE &&
             menu_saveOptions.what == SAVE_TYPE_BANK)
        menu_kitLoadSceneMask = 0u;
    else
        menu_kitLoadSceneMask = active_bit;
}

static void menu_refreshLoadSceneLeds(void)
{
    uint8_t scene_index;
    uint16_t selectable_mask = menu_loadSaveSelectableSceneMask();
    uint8_t bank_load = (uint8_t)(
        menu_activePage == LOAD_PAGE &&
        !menu_instrumentLoadActive &&
        menu_saveOptions.what == SAVE_TYPE_BANK);

    /*
     * Repaint SEQ LEDs as Scene status while Load owns the front panel.
     *
     * Inputs: current Load/Save mode, selectable Scene mask, selected
     * destination/source state, and nested Instrument mode. Outputs: Load
     * destinations are steady when included; single Save sources blink; Bank
     * Load shows only scanned child folders, with the active Scene as the only
     * persistent blink. Short change feedback is handled by led_flashGroup() in
     * menu_loadSceneButtonPressed(), avoiding the eight-slot blink allocator for
     * 16-bit Scene masks.
     */
    if (menu_activePage != LOAD_PAGE && menu_activePage != SAVE_PAGE)
        return;
    for (scene_index = 0u; scene_index < 16u; scene_index++) {
        led_setBlinkLed((uint8_t)(LED_SEQ1 + scene_index), 0u);
        led_setValue(0u, (uint8_t)(LED_SEQ1 + scene_index));
    }
    if (menu_activePage == LOAD_PAGE &&
        !menu_instrumentLoadActive && menu_saveOptions.what >= SAVE_TYPE_GLO)
        return;
    for (scene_index = 0u;
         scene_index < SCENE_COUNT && scene_index < 16u;
         scene_index++) {
        uint16_t bit = (uint16_t)(1u << scene_index);
        uint8_t selected;
        if (menu_instrumentLoadActive) {
            selected = menu_instrumentSaveMode
                ? (uint8_t)(scene_index == menu_instrumentLoadScene)
                : (uint8_t)((menu_kitLoadSceneMask & bit) != 0u);
        } else if (menu_activePage == SAVE_PAGE &&
                   menu_saveOptions.what != SAVE_TYPE_BANK) {
            selected = (uint8_t)(scene_index == menu_loadSaveSourceScene);
        } else {
            selected = (uint8_t)((menu_kitLoadSceneMask & bit) != 0u);
        }
        if (menu_activePage == SAVE_PAGE &&
            menu_saveOptions.what != SAVE_TYPE_BANK) {
            led_setValue((uint8_t)((selectable_mask & bit) != 0u),
                         (uint8_t)(LED_SEQ1 + scene_index));
        } else {
            led_setValue((uint8_t)(selected && (selectable_mask & bit)),
                         (uint8_t)(LED_SEQ1 + scene_index));
        }
        if (selected &&
            ((menu_activePage == SAVE_PAGE &&
              menu_saveOptions.what != SAVE_TYPE_BANK) ||
             (bank_load && scene_index == scene_getActiveIndex())))
            led_setBlinkLed((uint8_t)(LED_SEQ1 + scene_index), 1u);
    }
}

uint8_t menu_loadSceneButtonPressed(uint8_t scene_index)
{
    uint16_t bit;

    /*
     * Interpret a physical SEQ press as a Scene selection in Load contexts.
     *
     * Input: zero-based physical Scene index. Output: nonzero only when Load
     * owns it. Kit mode toggles destination membership, including the active
     * Scene; Instrument mode selects one destination
     * without loading a file. This cannot live in ButtonHandler because its
     * policy depends on Menu's nested Instrument source/cursor state and must
     * repaint the LCD plus Scene LEDs as one UI transaction.
     */
    if ((menu_activePage != LOAD_PAGE && menu_activePage != SAVE_PAGE) ||
        scene_index >= SCENE_COUNT ||
        scene_index >= 16u)
        return 0u;
    if (menu_activePage == LOAD_PAGE &&
        !menu_instrumentLoadActive && menu_saveOptions.what >= SAVE_TYPE_GLO)
        return 0u;
    if (menu_instrumentLoadActive && menu_storageBusy) {
        /*
         * Consume Scene changes during an immutable Instrument transaction.
         *
         * Input is a physical SEQ press while filesystem/apply owns the captured
         * Scene/slot. Output is handled-without-mutation, so ButtonHandler does
         * not reinterpret the press and the completion coordinates cannot drift
         * from the staged payload. Scene selection becomes available again only
         * after menu_tickInstrumentApply() clears menu_storageBusy.
         */
        return 1u;
    }
    bit = (uint16_t)(1u << scene_index);
    if ((menu_loadSaveSelectableSceneMask() & bit) == 0u)
        return 1u;
    if (menu_instrumentLoadActive) {
        if (menu_instrumentSaveMode) {
            instrument_type_t previous_type = menu_instrumentLoadType;
            menu_instrumentLoadScene = scene_index;
            menu_loadSaveSourceScene = scene_index;
            menu_kitLoadSceneMask = bit;
            menu_instrumentLoadRefreshBaseType(1u);
            menu_instrumentSaveSeedName();
            if (menu_instrumentLoadType != previous_type &&
                !menu_instrumentLoadMorphMode)
                menu_requestInstrumentIndexLoad(menu_instrumentLoadType);
        } else {
            instrument_type_t previous_type = menu_instrumentLoadType;
            uint16_t next = (uint16_t)(menu_kitLoadSceneMask ^ bit);
            if (next != 0u)
                menu_kitLoadSceneMask = next;
            menu_instrumentLoadScene = scene_index;
            menu_instrumentLoadRefreshBaseType(1u);
            if (menu_instrumentLoadType != previous_type &&
                !menu_instrumentLoadMorphMode)
                menu_requestInstrumentIndexLoad(menu_instrumentLoadType);
        }
    } else if (menu_activePage == SAVE_PAGE) {
        if (menu_saveOptions.what == SAVE_TYPE_BANK) {
            uint16_t next = (uint16_t)(menu_kitLoadSceneMask ^ bit);
            if (next != 0u)
                menu_kitLoadSceneMask = next;
        } else {
            menu_loadSaveSourceScene = scene_index;
            menu_kitLoadSceneMask = bit;
        }
    } else {
        uint16_t next = (uint16_t)(menu_kitLoadSceneMask ^ bit);
        if (next != 0u)
            menu_kitLoadSceneMask = next;
    }
    menu_refreshLoadSceneLeds();
    led_flashGroup(LED_FLASH_GROUP_SEQ, bit);
    if (!menu_storageBusy)
        menu_repaintAll();
    return 1u;
}

void menu_loadInstrumentExit(void)
{
    /*
     * Exit nested Instrument Load/Save mode.
     *
     * Inputs: none. Output: normal Load/Save page state becomes active again
     * while the selected top-level type/kit slot are left untouched.
     * ButtonHandler calls this when the Load/Save mode button is pressed a
     * second time.
     */
    if (menu_loadInstrumentTransactionBusy())
        return;
    filesystem_clearNameCache();
    menu_instrumentLoadActive = 0u;
    menu_instrumentSaveMode = 0u;
    menu_loadSaveClearInstrumentVoiceBlinks();
    menu_refreshLoadSceneLeds();
    menu_repaintAll();
}

uint8_t menu_loadInstrumentVoicePressed(uint8_t voiceNr)
{
    const kit_instrument_slot_t *slot;

    /*
     * Consume a VOICE button as an Instrument Load destination or Save source.
     *
     * Inputs: zero-based VOICE button. Output: nonzero when Load/Save consumed
     * the press. Load mode treats the voice as a destination for Instrument/
     * browsing. Save mode treats it as the resident source to export into the
     * root Instrument/ pool. ButtonHandler owns LED blink feedback and skips
     * normal preview/mute/page behavior when this returns true.
     */
    if ((menu_activePage != LOAD_PAGE && menu_activePage != SAVE_PAGE) ||
        voiceNr >= INSTRUMENT_SLOT_COUNT)
        return 0u;
    if (menu_loadInstrumentTransactionBusy()) {
        /*
         * Consume destination switches while one Instrument transaction owns
         * immutable Scene/slot coordinates. No browser type/index, active voice,
         * or LED context changes here; the press is simply deferred by omission
         * until the current read/apply/rebind finishes.
         */
        return 1u;
    }
    menu_instrumentLoadActive = 1u;
    menu_instrumentSaveMode = (uint8_t)(menu_activePage == SAVE_PAGE);
    menu_instrumentLoadSlot = voiceNr;
    menu_instrumentLoadScene = scene_getActiveIndex();
    menu_loadSaveSourceScene = menu_instrumentLoadScene;
    menu_kitLoadSceneMask = (uint16_t)(1u << menu_instrumentLoadScene);
    menu_instrumentLoadSource = MENU_INSTRUMENT_SOURCE_KIT;
    menu_saveOptions.what = SAVE_TYPE_KIT;
    menu_saveOptions.state = SAVE_STATE_EDIT_TYPE;
    /*
     * Instrument Load/Save entry always starts on the selected top type row.
     *
     * Inputs: the selected VOICE button and current Save/Load page. Output:
     * both `Load:[Type]` and `Save:[Type]` enter with brackets on the top row,
     * matching top-level Load/Save entry and Pot-1 navigation.
     */
    editModeActive = 1u;
    slot = scene_instrumentSlotConst(menu_instrumentLoadScene, voiceNr);
    menu_instrumentLoadType = slot ? slot->type : INSTRUMENT_TYPE_DRM;
    if (!instrumentManager_typeSelectableForSceneSlot(
            menu_instrumentLoadScene, voiceNr, menu_instrumentLoadType)) {
        menu_instrumentLoadType = INSTRUMENT_TYPE_DRM;
    }
    menu_instrumentLoadBaseType = menu_instrumentLoadType;
    menu_instrumentLoadMorphMode = 0u;
    menu_instrumentLoadClampIndex();
    if (menu_instrumentSaveMode)
        menu_instrumentSaveSeedName();
    /*
     * Load and Save share the same nested Instrument browser. Refreshing the
     * selected type's index on both entry paths prevents Save from inheriting
     * a stale list merely because the previous page was Load or another type.
     */
    menu_requestInstrumentIndexLoad(menu_instrumentLoadType);
    menu_setActiveVoice(voiceNr);
    menu_refreshLoadSceneLeds();
    if (!menu_storageBusy)
        menu_repaintAll();
    return 1u;
}

uint8_t menu_loadInstrumentTransactionBusy(void)
{
    /*
     * Expose the nested Instrument transaction lock to gesture routing.
     *
     * Output is nonzero only when Instrument mode is active and its successful
     * filesystem request or bounded post-apply still owns menu_storageBusy.
     * ButtonHandler uses this before preview and mode mutation, which cannot be
     * protected by the encoder-only guard in menu_parseEncoder().
     */
    return (uint8_t)(menu_instrumentLoadActive && menu_storageBusy);
}

/*
 * Hardware Load/Save Pot-1 linearization helpers.
 *
 * The first endless pot does not merely cycle the current page's type enum.
 * It walks one logical ring:
 *
 *   Load main rows -> Instrument Load rows voice 1..6 -> Save main rows ->
 *   Instrument Save rows voice 1..6.
 *
 * Instrument Load rows expand by selectable instrument type for each voice,
 * inserting the same-type Morph Load row beside the normal row. Instrument
 * Save rows are two rows per voice: normal and Mrp. Applying a logical
 * position re-enters the same UI state a VOICE button would have selected,
 * including source/destination voice, top-row brackets, Scene LED refresh, and
 * voice blink feedback. Returning to a main row clears Instrument blink because
 * non-instrument Load/Save entries never own a blinking VOICE indicator.
 *
 * Affiliates/clients: menu_handleLoadSaveKnobDelta(), ButtonHandler's VOICE
 * Load/Save entry, LED voice feedback, and menu_requestCurrentLoadSaveSelection().
 */
static uint8_t menu_loadSaveRestoredTypeIndex(uint8_t page, uint8_t what)
{
    const uint8_t *types =
        (page == LOAD_PAGE) ? menu_loadSaveLoadTypes : menu_loadSaveSaveTypes;
    uint8_t count = (page == LOAD_PAGE)
        ? (uint8_t)(sizeof(menu_loadSaveLoadTypes) /
                    sizeof(menu_loadSaveLoadTypes[0]))
        : (uint8_t)(sizeof(menu_loadSaveSaveTypes) /
                    sizeof(menu_loadSaveSaveTypes[0]));
    uint8_t i;

    for (i = 0u; i < count; i++) {
        if (types[i] == what)
            return i;
    }
    return 0u;
}

static uint8_t menu_instrumentLoadOptionCountForVoice(uint8_t voice)
{
    const kit_instrument_slot_t *slot =
        scene_instrumentSlotConst(scene_getActiveIndex(), voice);
    instrument_type_t base_type = slot ? slot->type : INSTRUMENT_TYPE_DRM;
    uint8_t count = 0u;
    uint8_t i;

    for (i = 0u; i < instrumentManager_registryCount(); i++) {
        const instrument_registry_entry_t *entry =
            instrumentManager_registryEntryAt(i);
        if (!entry ||
            !instrumentManager_typeSelectableForSceneSlot(
                scene_getActiveIndex(), voice, entry->type))
            continue;
        count++;
        if (entry->type == base_type)
            count++;
    }
    return count;
}

static uint8_t menu_instrumentLoadOptionAt(uint8_t voice, uint8_t option,
                                           instrument_type_t *type,
                                           uint8_t *morph)
{
    const kit_instrument_slot_t *slot =
        scene_instrumentSlotConst(scene_getActiveIndex(), voice);
    instrument_type_t base_type = slot ? slot->type : INSTRUMENT_TYPE_DRM;
    uint8_t logical = 0u;
    uint8_t i;

    for (i = 0u; i < instrumentManager_registryCount(); i++) {
        const instrument_registry_entry_t *entry =
            instrumentManager_registryEntryAt(i);
        if (!entry ||
            !instrumentManager_typeSelectableForSceneSlot(
                scene_getActiveIndex(), voice, entry->type))
            continue;
        if (logical == option) {
            *type = entry->type;
            *morph = 0u;
            return 1u;
        }
        logical++;
        if (entry->type == base_type) {
            if (logical == option) {
                *type = entry->type;
                *morph = 1u;
                return 1u;
            }
            logical++;
        }
    }

    *type = base_type;
    *morph = 0u;
    return (uint8_t)(logical > 0u);
}

static uint8_t menu_instrumentLoadCurrentOptionForVoice(uint8_t voice)
{
    uint8_t count = menu_instrumentLoadOptionCountForVoice(voice);
    uint8_t i;

    for (i = 0u; i < count; i++) {
        instrument_type_t type;
        uint8_t morph;
        if (menu_instrumentLoadOptionAt(voice, i, &type, &morph) &&
            type == menu_instrumentLoadType &&
            morph == menu_instrumentLoadMorphMode)
            return i;
    }
    return 0u;
}

static uint16_t menu_instrumentLoadOptionTotal(void)
{
    uint16_t total = 0u;
    uint8_t voice;

    for (voice = 0u; voice < INSTRUMENT_SLOT_COUNT; voice++)
        total = (uint16_t)(total +
                           menu_instrumentLoadOptionCountForVoice(voice));
    return total;
}

static uint16_t menu_loadSaveLogicalPosition(void)
{
    uint16_t load_type_count =
        (uint16_t)(sizeof(menu_loadSaveLoadTypes) /
                   sizeof(menu_loadSaveLoadTypes[0]));
    uint16_t load_instrument_total = menu_instrumentLoadOptionTotal();
    uint16_t save_type_count =
        (uint16_t)(sizeof(menu_loadSaveSaveTypes) /
                   sizeof(menu_loadSaveSaveTypes[0]));

    if (menu_activePage == LOAD_PAGE && !menu_instrumentLoadActive)
        return menu_loadSaveRestoredTypeIndex(LOAD_PAGE,
                                              menu_saveOptions.what);
    if (menu_activePage == LOAD_PAGE && menu_instrumentLoadActive) {
        uint16_t index = load_type_count;
        uint8_t voice;
        for (voice = 0u; voice < menu_instrumentLoadSlot; voice++)
            index = (uint16_t)(index +
                               menu_instrumentLoadOptionCountForVoice(voice));
        return (uint16_t)(index +
            menu_instrumentLoadCurrentOptionForVoice(menu_instrumentLoadSlot));
    }
    if (menu_activePage == SAVE_PAGE && !menu_instrumentLoadActive)
        return (uint16_t)(load_type_count + load_instrument_total +
                          menu_loadSaveRestoredTypeIndex(SAVE_PAGE,
                                                         menu_saveOptions.what));
    return (uint16_t)(load_type_count + load_instrument_total +
                      save_type_count +
                      ((uint16_t)menu_instrumentLoadSlot * 2u) +
                      (menu_instrumentLoadMorphMode ? 1u : 0u));
}

static uint16_t menu_loadSaveLogicalCount(void)
{
    return (uint16_t)(
        (uint16_t)(sizeof(menu_loadSaveLoadTypes) /
                   sizeof(menu_loadSaveLoadTypes[0])) +
        menu_instrumentLoadOptionTotal() +
        (uint16_t)(sizeof(menu_loadSaveSaveTypes) /
                   sizeof(menu_loadSaveSaveTypes[0])) +
        ((uint16_t)INSTRUMENT_SLOT_COUNT * 2u));
}

static void menu_loadSaveClearInstrumentVoiceBlinks(void)
{
    uint8_t blink_voice;

    /*
     * Main Load/Save rows never own a blinking VOICE indicator.
     *
     * Nested Instrument rows blink the selected source/destination voice.
     * Clear only those per-voice blink flags when returning to top-level
     * File/Dir/Kit/etc. rows so the steady active-voice LED remains intact.
     */
    for (blink_voice = 0u; blink_voice < INSTRUMENT_SLOT_COUNT; blink_voice++)
        led_setBlinkLed((uint8_t)(LED_VOICE1 + blink_voice), 0u);
}

static void menu_loadSaveEnterTop(uint8_t page, uint8_t what)
{
    menu_activePage = page;
    filesystem_clearNameCache();
    menu_instrumentLoadActive = 0u;
    menu_instrumentSaveMode = 0u;
    menu_loadSaveClearInstrumentVoiceBlinks();
    menu_saveOptions.what = what;
    menu_saveOptions.state = SAVE_STATE_EDIT_TYPE;
    editModeActive = 1u;
    if (what == SAVE_TYPE_BANK)
        menu_currentPresetNr[SAVE_TYPE_BANK] = bank_restoreBankSlot();
    menu_resetLoadSaveSceneSelection();
    menu_requestCurrentLoadSaveSelection(0);
    menu_refreshLoadSceneLeds();
}

static void menu_loadSaveSetInstrumentVoiceLed(uint8_t voice)
{
    /*
     * Mirror the VOICE-button Instrument Load/Save feedback for pot navigation.
     *
     * Pot 1 can enter nested Instrument rows without passing through
     * ButtonHandler's voice-button branch. Menu already owns the selected
     * source/destination voice in that path, so it must also refresh the
     * physical voice LED: one active voice, and one blinking Instrument slot.
     */
    led_setActiveVoice(voice);
    menu_loadSaveClearInstrumentVoiceBlinks();
    led_setBlinkLed((uint8_t)(LED_VOICE1 + voice), 1u);
}

static void menu_loadSaveEnterInstrumentLoad(uint8_t voice, uint8_t option)
{
    instrument_type_t type;
    uint8_t morph;

    menu_activePage = LOAD_PAGE;
    menu_instrumentLoadActive = 1u;
    menu_instrumentSaveMode = 0u;
    menu_instrumentLoadSlot = voice;
    menu_instrumentLoadScene = scene_getActiveIndex();
    menu_loadSaveSourceScene = menu_instrumentLoadScene;
    menu_kitLoadSceneMask = (uint16_t)(1u << menu_instrumentLoadScene);
    menu_instrumentLoadSource = MENU_INSTRUMENT_SOURCE_KIT;
    menu_saveOptions.what = SAVE_TYPE_KIT;
    menu_saveOptions.state = SAVE_STATE_EDIT_TYPE;
    editModeActive = 1u;
    menu_instrumentLoadRefreshBaseType(0u);
    if (menu_instrumentLoadOptionAt(voice, option, &type, &morph)) {
        menu_instrumentLoadType = type;
        menu_instrumentLoadMorphMode = morph;
    }
    menu_instrumentLoadClampIndex();
    /*
     * Pot-1 can enter nested Instrument Load without going through the
     * VOICE-button entry function. Keep that alternate path equivalent: its
     * selected registry type must load its own `.hcindex` before the browser
     * exposes the cached list.
     */
    menu_requestInstrumentIndexLoad(menu_instrumentLoadType);
    menu_setActiveVoice(voice);
    menu_loadSaveSetInstrumentVoiceLed(voice);
    menu_refreshLoadSceneLeds();
}

static void menu_loadSaveEnterInstrumentSave(uint8_t voice, uint8_t morph)
{
    menu_activePage = SAVE_PAGE;
    menu_instrumentLoadActive = 1u;
    menu_instrumentSaveMode = 1u;
    menu_instrumentLoadSlot = voice;
    menu_instrumentLoadScene = scene_getActiveIndex();
    menu_loadSaveSourceScene = menu_instrumentLoadScene;
    menu_kitLoadSceneMask = (uint16_t)(1u << menu_instrumentLoadScene);
    menu_instrumentLoadSource = MENU_INSTRUMENT_SOURCE_KIT;
    menu_saveOptions.what = SAVE_TYPE_KIT;
    menu_saveOptions.state = SAVE_STATE_EDIT_TYPE;
    editModeActive = 1u;
    menu_instrumentLoadRefreshBaseType(0u);
    menu_instrumentLoadMorphMode = morph ? 1u : 0u;
    menu_instrumentSaveSeedName();
    /*
     * Save has a second nested-entry route through Pot 1. Refresh the same
     * typed index here as in the VOICE-button route so Save never inherits a
     * stale cache when it is entered directly from the top-level page.
     */
    menu_requestInstrumentIndexLoad(menu_instrumentLoadType);
    menu_setActiveVoice(voice);
    menu_loadSaveSetInstrumentVoiceLed(voice);
    menu_refreshLoadSceneLeds();
}

static void menu_loadSaveApplyLogicalPosition(uint16_t target)
{
    uint16_t load_type_count =
        (uint16_t)(sizeof(menu_loadSaveLoadTypes) /
                   sizeof(menu_loadSaveLoadTypes[0]));
    uint16_t save_type_count =
        (uint16_t)(sizeof(menu_loadSaveSaveTypes) /
                   sizeof(menu_loadSaveSaveTypes[0]));
    uint16_t load_instrument_total = menu_instrumentLoadOptionTotal();
    uint16_t cursor = load_type_count;
    uint8_t voice;

    if (target < load_type_count) {
        menu_loadSaveEnterTop(LOAD_PAGE, menu_loadSaveLoadTypes[target]);
        return;
    }
    if (target < (uint16_t)(load_type_count + load_instrument_total)) {
        uint16_t local = (uint16_t)(target - load_type_count);
        for (voice = 0u; voice < INSTRUMENT_SLOT_COUNT; voice++) {
            uint8_t count = menu_instrumentLoadOptionCountForVoice(voice);
            if (local < count) {
                menu_loadSaveEnterInstrumentLoad(voice, (uint8_t)local);
                return;
            }
            local = (uint16_t)(local - count);
        }
    }

    cursor = (uint16_t)(load_type_count + load_instrument_total);
    if (target < (uint16_t)(cursor + save_type_count)) {
        menu_loadSaveEnterTop(
            SAVE_PAGE, menu_loadSaveSaveTypes[target - cursor]);
        return;
    }

    target = (uint16_t)(target - cursor - save_type_count);
    voice = (uint8_t)(target / 2u);
    if (voice >= INSTRUMENT_SLOT_COUNT)
        voice = (uint8_t)(INSTRUMENT_SLOT_COUNT - 1u);
    menu_loadSaveEnterInstrumentSave(voice, (uint8_t)(target & 1u));
}

static char *menu_loadSaveActiveNameBuffer(void)
{
    if (menu_instrumentLoadActive && menu_instrumentSaveMode)
        return menu_instrumentSaveName;
    if (menu_activePage == SAVE_PAGE &&
        (menu_saveOptions.what == SAVE_TYPE_FILE ||
         menu_saveOptions.what == SAVE_TYPE_DIR ||
         menu_saveOptions.what == SAVE_TYPE_SIMPLE_DIR))
        return menu_testEditName;
    if (menu_activePage == SAVE_PAGE)
        return preset_currentName;
    return NULL;
}

static void menu_handleLoadSaveKnobDelta(uint8_t knobNr, int8_t delta)
{
    if (delta == 0)
        return;

    switch (knobNr) {
    case 0: {
        uint16_t count = menu_loadSaveLogicalCount();
        int32_t target;
        if (count == 0u)
            return;
        target = (int32_t)menu_loadSaveLogicalPosition() + (int32_t)delta;
        while (target < 0)
            target += count;
        while (target >= (int32_t)count)
            target -= count;
        menu_loadSaveApplyLogicalPosition((uint16_t)target);
        break; }

    case 1:
        if (menu_instrumentLoadActive && menu_instrumentSaveMode) {
            menu_saveOptions.state = SAVE_STATE_EDIT_NAME1;
            editModeActive = 1u;
        } else {
            menu_saveOptions.state = SAVE_STATE_EDIT_PRESET_NR;
            editModeActive = 1u;
            menu_handleLoadSaveMenu(delta, 0u);
        }
        break;

    case 2:
        if (editModeActive) {
            editModeActive = 0u;
        } else {
            menu_handleLoadSaveMenu(delta, 0u);
        }
        break;

    case 3:
        if (menu_saveOptions.state >= SAVE_STATE_EDIT_NAME1 &&
            menu_saveOptions.state <= SAVE_STATE_EDIT_NAME8) {
            editModeActive = 1u;
            menu_handleLoadSaveMenu(delta, 0u);
        }
        break;

    default:
        return;
    }

    if (!menu_storageBusy)
        menu_repaintAll();
}

uint8_t menu_loadSaveBarButtonPressed(uint8_t advance)
{
    char *name;
    uint8_t ci;

    if ((menu_activePage != LOAD_PAGE && menu_activePage != SAVE_PAGE) ||
        menu_storageBusy ||
        menu_saveOptions.state < SAVE_STATE_EDIT_NAME1 ||
        menu_saveOptions.state > SAVE_STATE_EDIT_NAME8)
        return 0u;
    name = menu_loadSaveActiveNameBuffer();
    if (!name)
        return 0u;

    ci = (uint8_t)(menu_saveOptions.state - SAVE_STATE_EDIT_NAME1);
    if (advance) {
        /*
         * BAR2 is insert-space-forward.
         *
         * It first leaves the current character untouched, moves to the next
         * editable cell, blanks that one cell, and exits edit brackets. If the
         * cursor is already at the last character, the press is consumed but
         * does not mutate the name.
         */
        if (ci < 7u) {
            menu_saveOptions.state++;
            name[ci + 1u] = ' ';
        } else {
            editModeActive = 0u;
            menu_repaintAll();
            return 1u;
        }
    } else {
        /*
         * BAR1 is delete/backspace.
         *
         * It blanks exactly the current character, exits edit brackets, then
         * moves the underline cursor left when there is a previous character.
         */
        name[ci] = ' ';
        if (ci > 0u)
            menu_saveOptions.state--;
    }
    editModeActive = 0u;
    menu_repaintAll();
    return 1u;
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

static void menu_formatPresetNumber3(char *dst, uint16_t zero_based_slot)
{
    uint16_t display = zero_based_slot;

    /*
     * Format library folder numbers independently from byte-valued parameters.
     *
     * Kit/Scene and other library slots now span 000..999, with slot 000 a
     * real save/load location. numtostrpu() remains a uint8_t helper for menu
     * parameter values, so this helper keeps Load/Save folder display from
     * wrapping above 255 and avoids the old slot+1 presentation.
     */
    if (display > 999u)
        display = 999u;
    dst[0] = (char)('0' + (display / 100u));
    dst[1] = (char)('0' + ((display / 10u) % 10u));
    dst[2] = (char)('0' + (display % 10u));
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

static void menu_getLfoPolarityName(uint8_t value, char *buf)
{
    const char *p;
    uint8_t count = (uint8_t)lfoPolarityNames[0][0];

    /*
     * Format the dedicated LFO polarity dtype.
     *
     * Inputs: raw stored polarity value, expected to match mod_node_polarity_t
     * values 0..2. Output: a three-character LCD field. This cannot use the
     * packed DTYPE_MENU helper because that helper has only four high-nibble
     * bits for table ids; the previous id 16 wrapped to id 0 and displayed the
     * track-scale table. Keeping this helper local lets compact view,
     * single-parameter edit view, and clamp logic share the same table without
     * inventing a bogus menu-table id.
     */
    if (count == 0u) {
        memcpy(buf, menuText_dash, 3);
        return;
    }
    if (value >= count)
        value = (uint8_t)(count - 1u);
    p = lfoPolarityNames[value + 1u];
    buf[0] = p[0];
    buf[1] = p[1] ? p[1] : ' ';
    buf[2] = p[2] ? p[2] : ' ';
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

static void menu_displayModTargetShort(uint8_t curParmVal, char *valueAsText)
{
    if (modTargets[curParmVal].param == PAR_NONE) {
        memcpy(valueAsText, menuText_off, 3);
    } else {
        const uint8_t name = modTargets[curParmVal].nameIdx;
        const char *sn = shortNames[valueNames[name].shortName];
        uint8_t k;
        /*
         * Legacy compact automation target text used to optionally replace the
         * first short-name column with a voice number, producing labels such as
         * "1wa" or "1co". That style is not used by the new descriptor target
         * selectors and should not be reintroduced while the Phase 4 automation
         * display rewrite is pending. This helper now always renders the plain
         * target short name only.
         */
        for (k=0; k<3; k++) valueAsText[k] = sn[k] ? sn[k] : ' ';
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
    menu_cell_t cell = menu_resolveCellAbsolute(menuPage, 4u);
    return (uint8_t)(!menu_cellIsEmpty(&cell));
}

static uint8_t menu_voiceAbsolutePositionSelectable(uint8_t subPage,
                                                    uint8_t position)
{
    menu_cell_t cell =
        menu_resolveVoiceCellAtScreen(
            subPage,
            (uint8_t)(position / MENU_COMPACT_SCREEN_CELLS),
            (uint8_t)(position % MENU_COMPACT_SCREEN_CELLS));

    /*
     * Decide whether one linear voice sub-page cell can receive focus.
     *
     * Inputs: SELECT sub-page and a linear position across all screens returned
     * by menu_voiceSubPageScreenCount(). Output: nonzero for a real cell, zero
     * for empty or skip cells. Scene-setting cells are naturally selectable
     * because menu_resolveVoiceCellAtScreen() returns them as real cells.
     */
    if (position >=
        (uint8_t)(menu_voiceSubPageScreenCount(subPage) *
                  MENU_COMPACT_SCREEN_CELLS)) {
        return 0u;
    }
    if (menu_cellIsEmpty(&cell))
        return 0u;
    if (cell.kind == MENU_CELL_STATIC && cell.text_id == TEXT_SKIP)
        return 0u;
    return 1u;
}

static uint8_t menu_voiceSubPageScreenExists(uint8_t subPage, uint8_t screen)
{
    uint8_t i;

    /*
     * Test whether one four-cell voice screen exists.
     *
     * Inputs: subPage is the SELECT button page and screen is 0..3. Output:
     * nonzero when any cell on that screen is selectable. Earlier code treated
     * the first cell as a screen sentinel, but sparse future layouts need to
     * allow screens such as "        pa4 pa5    ". The reader now uses the
     * same ordinary INSTRUMENT_MENU_EMPTY cells that the layout rows use, so no
     * additional empty-count flagging system is required.
     */
    if (subPage >= NUM_SUB_PAGES ||
        screen >= menu_voiceSubPageScreenCount(subPage))
        return 0u;
    for (i = 0u; i < MENU_COMPACT_SCREEN_CELLS; i++) {
        if (menu_voiceAbsolutePositionSelectable(
                subPage,
                (uint8_t)(screen * MENU_COMPACT_SCREEN_CELLS + i)))
            return 1u;
    }
    return 0u;
}

static uint8_t menu_voiceFirstSelectableColumn(uint8_t subPage,
                                               uint8_t screen)
{
    uint8_t i;
    uint8_t start;

    /*
     * Find the initial focus column for one visible voice screen.
     *
     * Inputs: SELECT sub-page and four-cell screen. Output: the first
     * selectable visible column, or zero when the screen is empty. SELECT uses
     * this after changing screens so sparse screens do not land the highlight
     * on a blank cell while the actual parameter sits later in the row.
     */
    if (subPage >= NUM_SUB_PAGES ||
        screen >= menu_voiceSubPageScreenCount(subPage))
        return 0u;
    start = (uint8_t)(screen * MENU_COMPACT_SCREEN_CELLS);
    for (i = 0u; i < MENU_COMPACT_SCREEN_CELLS; i++) {
        if (menu_voiceAbsolutePositionSelectable(subPage,
                                                 (uint8_t)(start + i)))
            return i;
    }
    return 0u;
}

static uint8_t checkScrollSign(uint8_t activePage, uint8_t activeParameter)
{
    const uint8_t is2ndPage = (uint8_t)(activeParameter > 3);

    if (menu_isVoicePage(menu_activePage)) {
        uint8_t screen;
        uint8_t hasNext;
        uint8_t total_screens;

        /*
         * Voice pages use up to four screens behind one SELECT sub-page.
         *
         * Screen 0 shows '>' when another screen exists, preserving the old
         * first-screen affordance. Later screens show '*' when there is still
         * another screen ahead or '<' when the next SELECT press will loop back
         * to the first screen. Single-screen sub-pages show no marker.
         *
         * menuIndex stores the sub-page in a shared bitfield used by voice,
         * MIDI, STEP, and other modes. During mode transitions that bitfield
         * can briefly contain a value outside the voice SELECT range; normalize
         * before indexing the voice-screen memory so stale mode state cannot
         * read or write past menu_voiceSubPageScreen[].
         */
        if (activePage >= NUM_SUB_PAGES)
            activePage = 0u;
        screen = menu_voiceSubPageScreen[activePage];
        total_screens = menu_voiceSubPageScreenCount(activePage);
        if (screen >= total_screens ||
            !menu_voiceSubPageScreenExists(activePage, screen)) {
            screen = 0u;
            menu_voiceSubPageScreen[activePage] = 0u;
        }
        if (activePage == MENU_VOICE_MIX_SUBPAGE && total_screens > 1u) {
            uint8_t instrument_screens =
                menu_voiceInstrumentScreenCount(activePage);
            /*
             * VOICE mix has two kinds of extra pages.
             *
             * Instrument-owned mix screens use '^' on the first screen and '*'
             * on later instrument screens. Scene-owned setting screens use '+'
             * and never '<', because SELECT cycling loops but the marker should
             * identify ownership instead of suggesting a back action.
             */
            if (screen >= instrument_screens)
                return '+';
            return (screen == 0u) ? '^' : '*';
        }
        hasNext = menu_voiceSubPageScreenExists(activePage,
                                                (uint8_t)(screen + 1u));
        if (screen == 0u)
            return hasNext ? '>' : 0u;
        return hasNext ? '*' : '<';
    }

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
 * number is the direct 000..999 library slot; 000 is a real folder prefix, not
 * an internal sentinel.
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

    if (menu_testResultActive) {
        menu_showTestResult();
        return;
    }

    if (menu_instrumentLoadActive) {
        uint16_t count;
        uint16_t index;
        uint16_t display_index;
        const kit_t *kit;

        /*
         * Paint nested Instrument Load/Save mode.
         *
         * What: This logic re-writes the visual representation of the Instrument Load menu
         * within the LCD editDisplayBuffer. It respects menu_saveOptions.state and
         * editModeActive to conditionally paint '[' ']' or '>' (ARROW_SIGN) on either
         * row 1 (Type) or row 2 (File), mirroring the original Load menu design.
         *
         * Why it must exist: Without conditional cursor painting, the user is blind to the 
         * active navigation state (Type vs File slot) and edit state (navigating rows vs
         * changing values). The prior UX hardcoded edit brackets '[]' on the bottom row
         * while defaulting interaction to the top row, which created a confusing and 
         * unusable menu.
         *
         * Inputs: selected type, destination Scene/slot, retained kit filename
         * stem, optional Instrument/ pool item, cursor state, and edit state.
         * Outputs: top row shows the type label; bottom row begins as
         * "kit  <name>" and changes to numbered pool display only after lower
         * row encoder movement. The two sources stay separate so selecting a
         * new type never performs an implicit file load or destroys the useful
         * kit-member identity on entry.
         * Clients: Called by menu_repaint() and menu_repaintAll() whenever menu_activePage
         * == LOAD_PAGE and a redraw is requested.
         * Accessors: filesystem_instrumentCount, filesystem_instrumentDisplayIndex, 
         * filesystem_instrumentName, instrumentManager_typeDisplayLabel.
         * Affiliates: menu_handleLoadSaveMenu drives the actual state changes this reflects.
         */
        if (menu_instrumentSaveMode) {
            /*
             * Paint nested Instrument Save mode.
             *
             * Inputs: source Scene/voice, source type, editable stem, cursor
             * state, Normal/Morph projection selector, and OK selection.
             * Output: a compact Save:[Type] page that uses the normal
             * eight-character encoder editor before writing a root
             * Instrument/<stem.ext> file. The source instrument type remains
             * fixed by the resident slot; the top row toggles only which
             * endpoint projection the writer uses.
             */
            memcpy(&editDisplayBuffer[0][0], "Save:", 5u);
            if (menu_saveOptions.state == SAVE_STATE_EDIT_TYPE) {
                /*
                 * Render the editable Normal/Morph projection row.
                 *
                 * Inputs: editModeActive and menu_instrumentLoadMorphMode.
                 * Output: brackets mean encoder movement toggles the save
                 * projection; an arrow means the row is selected but not being
                 * edited. The label itself is generated by the same helper used
                 * by Instrument Load so "DrumMrp" and similar rows stay padded
                 * and truncated identically.
                 */
                if (editModeActive) {
                    editDisplayBuffer[0][5] = '[';
                    editDisplayBuffer[0][14] = ']';
                } else {
                    editDisplayBuffer[0][5] = ARROW_SIGN;
                }
            }
            menu_instrumentLoadCopyTypeLabel(&editDisplayBuffer[0][6]);
            for (uint8_t i = 0u; i < MENU_INSTRUMENT_SAVE_NAME_LEN; i++) {
                char c = menu_instrumentSaveName[i];
                editDisplayBuffer[1][5u + i] = c ? c : ' ';
            }
            if (menu_saveOptions.state >= SAVE_STATE_EDIT_NAME1 &&
                menu_saveOptions.state <= SAVE_STATE_EDIT_NAME8) {
                uint8_t ci =
                    (uint8_t)(menu_saveOptions.state - SAVE_STATE_EDIT_NAME1);
                if (editModeActive) {
                    editDisplayBuffer[1][4u + ci] = '[';
                    editDisplayBuffer[1][6u + ci] = ']';
                } else {
                    cur_want_on = 1u;
                    cur_want_col = (uint8_t)(5u + ci);
                    cur_want_row = 2u;
                }
            }
            /*
             * Show root Instrument overwrite state in the nested Save surface.
             *
             * What: Reuses the shared Save identity query even though nested
             * Instrument Save returns before the generic Save-page renderer.
             *
             * Why: root Instrument overwrite is filename/type based rather
             * than numbered-slot based. The user must see `OW` for
             * case-insensitive matches such as `fiRstfile.snr` before OK
             * collapses all variants to the newly entered case.
             *
             * Inputs: menu_instrumentSaveName and source instrument type.
             * Outputs: bottom-right LCD affordance is either OK or OW.
             *
             * Affiliates/clients: menu_currentSaveWouldOverwrite(),
             * filesystem_instrumentTargetExists(), filesystem_saveInstrument_tick().
             */
            if (menu_currentSaveWouldOverwrite())
                memcpy(&editDisplayBuffer[1][14], "OW", 2u);
            else
                memcpy(&editDisplayBuffer[1][14], menuText_ok, 2u);
            if (menu_saveOptions.state == SAVE_STATE_OK)
                editDisplayBuffer[1][13] = ARROW_SIGN;
            return;
        }

        menu_instrumentLoadClampIndex();
        count = filesystem_instrumentCount(menu_instrumentLoadShownType);
        index = menu_instrumentLoadShownIndex;
        if (count > 0u && index >= count)
            index = count - 1u;
        display_index = filesystem_instrumentDisplayIndex(
            menu_instrumentLoadShownType, index);

        /* --- Top Row: Load Type --- */
        memcpy(&editDisplayBuffer[0][0], "Load:", 5);
        
        /* Render cursor/brackets for the top row if the state is editing the type */
        if (menu_saveOptions.state == SAVE_STATE_EDIT_TYPE) {
            if (editModeActive) {
                /* In edit mode, show brackets around the type name */
                editDisplayBuffer[0][5] = '[';
                editDisplayBuffer[0][14] = ']';
            } else {
                /* In selection mode, show a pointer arrow */
                editDisplayBuffer[0][5] = ARROW_SIGN;
            }
        }
        
        menu_instrumentLoadCopyTypeLabel(&editDisplayBuffer[0][6]);

        /* Render cursor/brackets for the kit-member/pool-source row. */
        if (menu_saveOptions.state == SAVE_STATE_EDIT_PRESET_NR) {
            if (editModeActive) {
                /* Bracket only the fixed Kit/list selector field, matching the
                 * normal `[000]name` Load geometry. The source name remains a
                 * read-only identity display, never part of the selection. */
                editDisplayBuffer[1][0] = '[';
                editDisplayBuffer[1][4] = ']';
            } else {
                /* In selection mode, use the same left-edge pointer as Load. */
                editDisplayBuffer[1][0] = ARROW_SIGN;
            }
        }

        if (menu_instrumentLoadSource == MENU_INSTRUMENT_SOURCE_KIT) {
            kit = scene_getConst(menu_instrumentLoadScene)
                ? &scene_getConst(menu_instrumentLoadScene)->kit : NULL;
            /* Preserve the normal `[slot]name` split: `kit` occupies the
             * three selector cells and the retained filename starts after the
             * closing bracket/spacing cell as `[kit]name`. */
            memcpy(&editDisplayBuffer[1][1], "kit", 3u);
            memcpy(&editDisplayBuffer[1][5],
                   kit ? kit->instrument_display_name[menu_instrumentLoadSlot]
                       : "Empty   ",
                   8u);
        } else {
            /* Format the pool position only after lower-row movement selected
             * a concrete file. The decimal math is explicit to preserve the
             * fixed four-cell LCD field and saturates the non-semantic cursor
             * number at 999 for large per-type libraries. */
            if (display_index > 999u)
                display_index = 999u;
            editDisplayBuffer[1][1] = (display_index >= 100u)
                ? (char)('0' + (display_index / 100u)) : ' ';
            editDisplayBuffer[1][2] = (display_index >= 10u)
                ? (char)('0' + ((display_index / 10u) % 10u)) : ' ';
            editDisplayBuffer[1][3] = (char)('0' + (display_index % 10u));
            memcpy(&editDisplayBuffer[1][5],
                   count ? filesystem_instrumentName(menu_instrumentLoadShownType,
                                                      index) : "Empty   ",
                   8u);
        }
        return;
    }

    /* Top row */
    if (menu_activePage == SAVE_PAGE)
        memcpy(&editDisplayBuffer[0][0], "Save:", 5);
    else
        memcpy(&editDisplayBuffer[0][0], "Load:", 5);

    const char *toptxt = "File    ";
    switch (menu_saveOptions.what) {
    case SAVE_TYPE_FILE:        toptxt = "File    "; break;
    case SAVE_TYPE_DIR:         toptxt = "Dir     "; break;
    case SAVE_TYPE_SIMPLE_DIR:  toptxt = "sDir    "; break;
    case SAVE_TYPE_KIT:         toptxt = "Kit     "; break;
    case SAVE_TYPE_KIT_MORPH:   toptxt = "KitMrp  "; break;
    case SAVE_TYPE_SCENE:       toptxt = "Scene   "; break;
    case SAVE_TYPE_BANK:        toptxt = "Bank    "; break;
    case SAVE_TYPE_GLO:         toptxt = "Settings"; break;
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

    if (menu_saveOptions.what == SAVE_TYPE_FILE ||
        menu_saveOptions.what == SAVE_TYPE_DIR ||
        menu_saveOptions.what == SAVE_TYPE_SIMPLE_DIR) {
        const char *name = menu_currentTestName();
        uint8_t count = menu_testObjectCount(menu_saveOptions.what);

        /*
         * Temporary File/Dir test presentation.
         *
         * Load pages show the selected root object from the exact-case scan
         * cache; Save pages show the bounded filename editor. Both modes keep
         * the right-side OK affordance because the selected action is explicit
         * and no older instant-load behavior should survive this test surface.
         */
        if (menu_activePage == LOAD_PAGE) {
            uint16_t displayPreset = menu_currentPresetNr[menu_saveOptions.what];
            if (count != 0u && displayPreset >= count)
                displayPreset = (uint16_t)(count - 1u);
            menu_formatPresetNumber3(&editDisplayBuffer[1][1], displayPreset);
            if (menu_saveOptions.state == SAVE_STATE_EDIT_PRESET_NR) {
                if (editModeActive) {
                    editDisplayBuffer[1][0] = '[';
                    editDisplayBuffer[1][4] = ']';
                } else {
                    editDisplayBuffer[1][0] = ARROW_SIGN;
                }
            }
        }
        for (uint8_t i = 0u; i < 8u; i++) {
            char c = name[i];
            editDisplayBuffer[1][5u + i] = c ? c : ' ';
        }
        if (menu_activePage == SAVE_PAGE &&
            menu_saveOptions.state >= SAVE_STATE_EDIT_NAME1 &&
            menu_saveOptions.state <= SAVE_STATE_EDIT_NAME8) {
            uint8_t ci = (uint8_t)(menu_saveOptions.state - SAVE_STATE_EDIT_NAME1);
            if (editModeActive) {
                editDisplayBuffer[1][4u + ci] = '[';
                editDisplayBuffer[1][6u + ci] = ']';
            } else {
                cur_want_on = 1u;
                cur_want_col = (uint8_t)(5u + ci);
                cur_want_row = 2u;
            }
        }
        memcpy(&editDisplayBuffer[1][14], menuText_ok, 2u);
        if (menu_saveOptions.state == SAVE_STATE_OK)
            editDisplayBuffer[1][13] = ARROW_SIGN;
        return;
    }

    /* Bottom row */
    if (menu_saveOptions.what < SAVE_TYPE_GLO) {
        uint16_t displayPreset = menu_currentPresetNr[menu_saveOptions.what];
        const char *displayName = preset_currentName;

        /*
         * KitMrp reuses the Kit browser presentation.
         *
         * Both Kit entries show the same numbered Kit/ scan-cache names. The
         * top-row type tells selection dispatch whether the staged Kit replaces
         * the resident kit or copies same-type normal endpoints into morph
         * storage.
         */
        if (menu_activePage == LOAD_PAGE && menu_saveOptions.what < SAVE_TYPE_GLO) {
            if (menu_saveOptions.what == SAVE_TYPE_BANK) {
                displayName = filesystem_bankSlotName(
                    menu_currentPresetNr[SAVE_TYPE_BANK]);
            } else if (menu_saveOptions.what == SAVE_TYPE_SCENE) {
                displayName = filesystem_sceneSlotName(
                    menu_currentPresetNr[SAVE_TYPE_SCENE]);
            } else {
                displayName = filesystem_kitSlotName(
                    menu_currentPresetNr[menu_saveOptions.what]);
            }
        }

        menu_formatPresetNumber3(&editDisplayBuffer[1][1], displayPreset);
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
        if (menu_currentSaveWouldOverwrite())
            memcpy(&editDisplayBuffer[1][14], "OW", 2);
        else
            memcpy(&editDisplayBuffer[1][14], menuText_ok, 2);
        if ((menu_saveOptions.state == SAVE_STATE_OK) ||
            (menu_saveOptions.what >= SAVE_TYPE_GLO && menu_saveOptions.state > SAVE_STATE_EDIT_TYPE)) {
            editDisplayBuffer[1][13] = ARROW_SIGN;
        }
    } else {
        /* Load page */
        if (menu_saveOptions.what >= SAVE_TYPE_GLO ||
            menu_saveOptions.what == SAVE_TYPE_SCENE ||
            menu_saveOptions.what == SAVE_TYPE_BANK) {
            /*
             * Explicit Load commands need a visible confirmation affordance.
             *
             * Kit/KitMrp still live-load on slot scroll and therefore hide the
             * OK text. Scene and Bank do not live-load; their row must show OK
             * so the user can move to it and click to perform the load.
             */
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
    char valueAsText[3];

    if (editModeActive) {
        menu_cell_t cell = menu_resolveCell(activePage, activeParameter);
        uint16_t curParmVal;
        uint8_t dtype;

        if (menu_cellIsEmpty(&cell))
            return;
        if (cell.kind == MENU_CELL_STATIC &&
            cell.static_param == PAR_RUNTIME_CPU_USE) {
            menu_displayCpuUseEdit();
            return;
        }

        curParmVal = menu_cellDisplayValue(&cell);
        dtype = (uint8_t)(menu_cellDtype(&cell) & 0x0f);
        if (dtype == DTYPE_TARGET_SELECTION_LFO &&
            cell.kind == MENU_CELL_INSTRUMENT) {
            curParmVal = menu_lfoTargetDisplayValue(&cell, curParmVal);
        } else if (dtype == DTYPE_TARGET_SELECTION_VELO &&
                   cell.kind == MENU_CELL_INSTRUMENT) {
            curParmVal = menu_velocityTargetDisplayValue(&cell, curParmVal);
        } else if (dtype == DTYPE_VOICE_LFO &&
                   menu_cellIsLfoTargetVoice(&cell)) {
            if (curParmVal < INSTRUMENT_TARGET_VOICE_FIRST)
                curParmVal = INSTRUMENT_TARGET_VOICE_FIRST;
            else if (curParmVal > INSTRUMENT_TARGET_VOICE_SCENE)
                curParmVal = INSTRUMENT_TARGET_VOICE_SCENE;
        }

        memset(&editDisplayBuffer[0][0], ' ', 16);
        memset(&editDisplayBuffer[1][0], ' ', 16);

        if (cell.kind == MENU_CELL_STATIC && dtype == DTYPE_AUTOM_TARGET) {
            uint8_t value = (curParmVal > 255u) ? 255u : (uint8_t)curParmVal;
            memcpy(&editDisplayBuffer[0][0], "AutDst", 6);
            numtostru(&editDisplayBuffer[0][7],
                      (uint8_t)(cell.static_param - PAR_P1_DEST + 1u));
            if (modTargets[value].param == PAR_NONE) {
                memcpy(&editDisplayBuffer[1][0], menuText_off, 3);
            } else {
                memcpy(&editDisplayBuffer[0][9], "Voice", 5);
                numtostru(&editDisplayBuffer[0][15], voiceFromModTargValue(value));
                menu_displayModTargetFull(value);
            }
        } else {
            uint8_t value = (curParmVal > 255u) ? 255u : (uint8_t)curParmVal;

            if (cell.kind == MENU_CELL_SCENE_SETTING) {
                menu_copyPaddedField(&editDisplayBuffer[0][0],
                                     "Scene", 8u);
                switch (cell.scene_setting) {
                case MENU_SCENE_SETTING_AUDIO_OUT:
                    menu_copyPaddedField(&editDisplayBuffer[0][8],
                                         "AudioOut", 8u);
                    break;
                case MENU_SCENE_SETTING_FX_SEND_AMOUNT:
                    menu_copyPaddedField(&editDisplayBuffer[0][8],
                                         "FX Send", 8u);
                    break;
                case MENU_SCENE_SETTING_FADER_SETTING:
                    menu_copyPaddedField(&editDisplayBuffer[0][8],
                                         "Fader", 8u);
                    break;
                default:
                    break;
                }
            } else if (cell.kind == MENU_CELL_INSTRUMENT ||
                       cell.kind == MENU_CELL_KIT_SETTING) {
                menu_copyPaddedField(&editDisplayBuffer[0][0],
                                     cell.descriptor->category, 8u);
                menu_copyPaddedField(&editDisplayBuffer[0][8],
                                     cell.descriptor->long_name, 8u);
            } else {
                const char *cat = catNames[valueNames[cell.text_id].category];
                const char *lng = longNames[valueNames[cell.text_id].longName];
                uint8_t ci;
                for (ci = 0u; ci < 8u && cat[ci]; ci++)
                    editDisplayBuffer[0][ci] = cat[ci];
                for (ci = 0u; ci < 8u && lng[ci]; ci++)
                    editDisplayBuffer[0][8u + ci] = lng[ci];
            }

            switch (dtype) {
            case DTYPE_TARGET_SELECTION_VELO:
            case DTYPE_TARGET_SELECTION_LFO:
                if (cell.kind == MENU_CELL_INSTRUMENT)
                    menu_displayInstrumentTargetFull(curParmVal);
                else if (curParmVal == INSTRUMENT_PARAM_INVALID)
                    memcpy(&editDisplayBuffer[1][0], menuText_off, 3);
                else
                    menu_displayModTargetFull(value);
                break;
            case DTYPE_MIX_FM:
                if (value==1) memcpy(&editDisplayBuffer[1][13], menuText_mix, 3);
                else               memcpy(&editDisplayBuffer[1][13], menuText_fm, 3);
                break;
            case DTYPE_ON_OFF:
                if (value==1) memcpy(&editDisplayBuffer[1][13], menuText_on, 3);
                else               memcpy(&editDisplayBuffer[1][13], menuText_off, 3);
                break;
            case DTYPE_LFO_POLARITY:
                menu_getLfoPolarityName(value, &editDisplayBuffer[1][13]);
                break;
            case DTYPE_0B15:
                if (cell.kind == MENU_CELL_SCENE_SETTING &&
                    cell.scene_setting == MENU_SCENE_SETTING_FADER_SETTING) {
                    menu_sceneSettingFaderName(value,
                                               &editDisplayBuffer[1][13]);
                    break;
                }
                numtostrpu(&editDisplayBuffer[1][13], value, ' ');
                break;
            case DTYPE_MENU: {
                uint8_t menuId = (uint8_t)(menu_cellDtype(&cell) >> 4);
                if (menuId == MENU_WAVEFORM && value >= (uint8_t)waveformNames[0][0]) {
                    char sampleName[SAMPLE_DISPLAY_NAME_LEN + 1u];
                    uint8_t sampleIndex =
                        (uint8_t)(value - (uint8_t)waveformNames[0][0]);

                    sampleMemory_getDisplayName(sampleIndex, sampleName);
                    memcpy(&editDisplayBuffer[1][0], sampleName,
                           SAMPLE_DISPLAY_NAME_LEN);
                }
                getMenuItemNameForValue(menuId, value, &editDisplayBuffer[1][13]);
                break; }
            case DTYPE_PM63:
                numtostrps(&editDisplayBuffer[1][13], (int8_t)(value - 63));
                break;
            case DTYPE_NOTE_NAME:
                if (cell.kind == MENU_CELL_STATIC &&
                    cell.static_param == PAR_TRACK_MIDI_NOTE && value==0)
                    memcpy(&editDisplayBuffer[1][13], menuText_any, 3);
                else
                    setNoteName(value, &editDisplayBuffer[1][13]);
                break;
            case DTYPE_0b1:
                numtostrpu(&editDisplayBuffer[1][13], (uint8_t)(value+1), ' ');
                break;
            default:
            case DTYPE_0B127:
            case DTYPE_0B255:
            case DTYPE_1B16:
            case DTYPE_1B128:
            case DTYPE_VOICE_LFO:
                if (menu_cellIsLfoTargetVoice(&cell) &&
                    value == INSTRUMENT_TARGET_VOICE_SCENE) {
                    memcpy(&editDisplayBuffer[1][13], "scn", 3);
                } else {
                    numtostrpu(&editDisplayBuffer[1][13], value, ' ');
                }
                break;
            }
        }
    } else {
        const uint8_t is2ndPage = menu_isVoicePage(menu_activePage)
            ? 0u : (uint8_t)((activeParameter > 3) ? 4 : 0);
        uint8_t i;

        for (i = 0u; i < 4u; i++) {
            menu_cell_t cell = menu_resolveCell(activePage,
                                                (uint8_t)(i + is2ndPage));
            if (cell.kind == MENU_CELL_STATIC && cell.text_id == TEXT_SKIP) {
                memcpy(&editDisplayBuffer[0][4u * i], menuText_blank, 3);
            } else if (cell.kind == MENU_CELL_SCENE_SETTING) {
                menu_sceneSettingShortName(&cell,
                                           &editDisplayBuffer[0][4u * i]);
            } else if (cell.kind == MENU_CELL_INSTRUMENT ||
                       cell.kind == MENU_CELL_KIT_SETTING) {
                menu_copyPaddedField(&editDisplayBuffer[0][4u * i],
                                     cell.descriptor->short_name, 3u);
            } else if (cell.kind == MENU_CELL_STATIC) {
                memcpy(&editDisplayBuffer[0][4u * i],
                       shortNames[valueNames[cell.text_id].shortName], 3);
            } else {
                memcpy(&editDisplayBuffer[0][4u * i], menuText_blank, 3);
            }
        }

        upr_three(&editDisplayBuffer[0][(activeParameter % 4) * 4]);
        editDisplayBuffer[0][15] = (char)checkScrollSign(activePage, activeParameter);

        for (i = 0u; i < 4u; i++) {
            menu_cell_t cell = menu_resolveCell(activePage,
                                                (uint8_t)(i + is2ndPage));
            if (menu_cellIsEmpty(&cell) ||
                (cell.kind == MENU_CELL_STATIC && cell.text_id == TEXT_SKIP)) {
                memcpy(valueAsText, menuText_blank, 3);
            } else if (cell.kind == MENU_CELL_STATIC &&
                       cell.static_param == PAR_RUNTIME_CPU_USE) {
                menu_formatCpuUsePercent3(valueAsText);
            } else {
                menu_formatCellValue3(&cell, valueAsText);
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
    menu_cell_t cell = menu_resolveCell(activePage, activeParameter);
    uint16_t value;

    if (menu_cellIsEmpty(&cell))
        return;
    if (cell.kind == MENU_CELL_STATIC &&
        cell.static_param == PAR_RUNTIME_CPU_USE)
        return;

    value = menu_cellDisplayValue(&cell);

    if (menu_cellIsLfoTargetVoice(&cell)) {
        (void)menu_lfoTargetEditVoice(&cell, inc);
        return;
    }
    if (menu_cellIsLfoTargetParam(&cell)) {
        (void)menu_lfoTargetEditParam(&cell, inc);
        return;
    }
    if (menu_cellIsVelocityTargetParam(&cell)) {
        (void)menu_velocityTargetEditParam(&cell, inc);
        return;
    }

    /*
     * Apply encoder acceleration in a value-local domain before committing to
     * the owner. Static cells eventually write parameter_values[]; instrument
     * cells write SceneData through Preset so active/morph endpoints and DSP
     * side effects stay paired.
     */
    if ((menu_cellDtype(&cell) & 0x0f) == DTYPE_TARGET_SELECTION_VELO ||
        (menu_cellDtype(&cell) & 0x0f) == DTYPE_TARGET_SELECTION_LFO) {
        if (value == INSTRUMENT_PARAM_INVALID && inc > 0)
            value = 0u;
        else if (value == INSTRUMENT_PARAM_INVALID)
            return;
        else if (inc > 0)
            value = (value < 65535u - (uint16_t)inc)
                ? (uint16_t)(value + (uint16_t)inc) : 65535u;
        else if (inc < 0) {
            uint16_t step = (uint16_t)(-inc);
            value = (value > step) ? (uint16_t)(value - step)
                                   : INSTRUMENT_PARAM_INVALID;
        }
    } else if (inc > 0) {
        uint32_t sum = (uint32_t)value + (uint32_t)inc;
        value = (sum > 65535u) ? 65535u : (uint16_t)sum;
    } else if (inc < 0) {
        uint16_t step = (uint16_t)(-inc);
        value = (value > step) ? (uint16_t)(value - step) : 0u;
    }

    menu_clampCellValue(&cell, &value);
    (void)menu_cellCommitValue(&cell, value);
}

/* -----------------------------------------------------------------------
** menu_moveToMenuItem — exact port of original (goto checkvalid logic)
** ----------------------------------------------------------------------- */
static void menu_moveToMenuItem(int8_t inc)
{
    int8_t activeParameter = (int8_t)(menuIndex & MASK_PARAMETER);
    int8_t activePage      = (int8_t)(menuIndex >> PAGE_SHIFT);
    uint8_t needLock = 0;
    uint8_t allowedSkips = 3;

    inc = (int8_t)(inc > 0 ? 1 : -1);

    if (menu_isVoicePage(menu_activePage)) {
        /*
         * Voice pages navigate real parameters across all populated screens.
         *
         * Inputs: encoder movement in compact-view navigation. Output:
         * activeParameter remains a visible column 0..3, but the remembered
         * sub-page screen changes when the next selectable cell lives on
         * another four-cell screen. Empty cells and INSTRUMENT_MENU_SKIP cells
         * are skipped. Unlike SELECT cycling, encoder navigation does not loop:
         * decrementing stops at the first selectable cell on screen 0, and
         * incrementing stops at the final selectable cell on the final populated
         * screen. On mix, those populated screens include appended Scene-owned
         * settings after the instrument descriptor screens.
         */
        uint8_t subPage = (uint8_t)activePage;
        uint8_t screen;
        int16_t candidate;

        if (subPage >= NUM_SUB_PAGES)
            subPage = 0u;
        screen = menu_voiceSubPageScreen[subPage];
        if (screen >= menu_voiceSubPageScreenCount(subPage) ||
            !menu_voiceSubPageScreenExists(subPage, screen)) {
            screen = 0u;
            menu_voiceSubPageScreen[subPage] = 0u;
        }
        if (activeParameter < 0 ||
            activeParameter >= (int8_t)MENU_COMPACT_SCREEN_CELLS) {
            activeParameter =
                (int8_t)menu_voiceFirstSelectableColumn(subPage, screen);
        }

        candidate = (int16_t)(screen * MENU_COMPACT_SCREEN_CELLS +
                              (uint8_t)activeParameter + inc);
        while (candidate >= 0 &&
               candidate < (int16_t)(menu_voiceSubPageScreenCount(subPage) *
                                      MENU_COMPACT_SCREEN_CELLS)) {
            if (menu_voiceAbsolutePositionSelectable(subPage,
                                                     (uint8_t)candidate)) {
                screen = (uint8_t)(candidate / MENU_COMPACT_SCREEN_CELLS);
                activeParameter =
                    (int8_t)(candidate % MENU_COMPACT_SCREEN_CELLS);
                menu_voiceSubPageScreen[subPage] = screen;
                menuIndex = (uint8_t)((subPage << PAGE_SHIFT) |
                                      (uint8_t)activeParameter);
                return;
            }
            candidate = (int16_t)(candidate + inc);
        }
        return;
    }

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

    {
        menu_cell_t cell =
            menu_resolveCell((uint8_t)activePage, (uint8_t)activeParameter);
        if (cell.kind == MENU_CELL_STATIC && cell.text_id == TEXT_SKIP) {
        if (allowedSkips--) goto checkvalid;
        else return;
        }
        if (menu_cellIsEmpty(&cell)) return;
    }

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
** movement calls menu_requestCurrentLoadSaveSelection(1), which posts a
** Scene-mask-aware Kit directory request for Kit/NNN Name. KitMrp is adjacent
** to Kit in the promoted type cycle and reuses the same numbered Kit browser
** while dispatching to the Morph Save/Load projection.
** ----------------------------------------------------------------------- */
static void menu_handleLoadSaveMenu(int8_t inc, uint8_t btnClicked)
{
    if (menu_instrumentLoadActive) {
        if (menu_instrumentSaveMode) {
            /*
             * Encoder handling for nested Instrument Save.
             *
             * Inputs: Save-page encoder delta/click state after the global
             * edit-mode toggle. Output: the top row toggles Normal vs Morph
             * Save projection, name characters wrap through printable ASCII,
             * and OK posts one root Instrument Save. This intentionally
             * bypasses pool browsing because Save exports the selected resident
             * source slot, not a file selected from Instrument/.
             */
            if (btnClicked && editModeActive &&
                menu_saveOptions.state == SAVE_STATE_OK) {
                menu_instrumentSaveRequestSelection();
                return;
            }
            if (editModeActive) {
                if (inc != 0 &&
                    menu_saveOptions.state == SAVE_STATE_EDIT_TYPE) {
                    /*
                     * Toggle the nested Instrument Save projection.
                     *
                     * Inputs: any encoder delta while the top row is in edit
                     * mode. Output: zero selects normal Instrument Save; one
                     * selects InstrumentMrp. The resident source type is not
                     * changed here, so filename extension and overwrite checks
                     * remain tied to SceneData's current slot type.
                     */
                    menu_instrumentLoadMorphMode =
                        (uint8_t)!menu_instrumentLoadMorphMode;
                } else if (inc != 0 &&
                    menu_saveOptions.state >= SAVE_STATE_EDIT_NAME1 &&
                    menu_saveOptions.state <= SAVE_STATE_EDIT_NAME8) {
                    uint8_t ci = (uint8_t)(menu_saveOptions.state -
                                           SAVE_STATE_EDIT_NAME1);
                    char c = (char)(menu_instrumentSaveName[ci] + inc);
                    if (c < 0x20)
                        c = 0x7e;
                    else if (c > 0x7e)
                        c = 0x20;
                    menu_instrumentSaveName[ci] = c;
                    menu_instrumentSaveName[MENU_INSTRUMENT_SAVE_NAME_LEN] =
                        '\0';
                }
            } else {
                if (inc != 0) {
                    /*
                     * Move across only the visible Instrument Save fields.
                     *
                     * Inputs: signed encoder delta while the nested Save page
                     * is in selection mode. Output: the selection cursor can
                     * land on the top projection row, each name character, and
                     * OK/OW, but never on the hidden preset-number row used by
                     * top-level Kit/Scene save.
                     */
                    menu_saveOptions.state =
                        menu_instrumentSaveStepSelectionState(
                            menu_saveOptions.state, inc);
                }
            }
            return;
        }

        /*
         * Encoder handling for nested Instrument Load.
         *
         * Inputs: normal Load-page encoder delta/click state. Outputs: without
         * edit mode, the cursor moves between type and file rows; in edit mode,
         * the encoder steps selectable types without loading, or moves from
         * kit-member display to a concrete sorted pool item and loads that
         * item. The normal Save/Load type bitfield is bypassed because this is
         * a destination-slot submode, not a SAVE_TYPE_*.
         */
        (void)btnClicked;
        if (editModeActive) {
            if (menu_saveOptions.state == SAVE_STATE_EDIT_TYPE) {
                if (inc != 0)
                    menu_instrumentLoadStepType(inc);
            } else {
                uint16_t count =
                    filesystem_instrumentCount(menu_instrumentLoadType);
                if (count > 0u && inc != 0) {
                    int32_t next;
                    if (menu_instrumentLoadSource == MENU_INSTRUMENT_SOURCE_KIT ||
                        menu_instrumentLoadShownType != menu_instrumentLoadType) {
                        next = (inc < 0) ? (int32_t)(count - 1u) : 0;
                    } else {
                        next = (int32_t)menu_instrumentLoadShownIndex +
                               (int32_t)inc;
                    }
                    if (next < 0)
                        next = 0;
                    else if (next >= (int32_t)count)
                        next = (int32_t)(count - 1u);
                    if (menu_instrumentLoadSource == MENU_INSTRUMENT_SOURCE_KIT ||
                        menu_instrumentLoadShownType != menu_instrumentLoadType ||
                        (uint16_t)next != menu_instrumentLoadShownIndex) {
                        menu_instrumentLoadSource = MENU_INSTRUMENT_SOURCE_POOL;
                        menu_instrumentLoadShownType = menu_instrumentLoadType;
                        menu_instrumentLoadShownIndex = (uint16_t)next;
                        menu_instrumentLoadIndex[menu_instrumentLoadType] =
                            (uint16_t)next;
                        menu_instrumentLoadRequestSelection();
                    }
                }
            }
        } else {
            if (inc < 0)
                menu_saveOptions.state = SAVE_STATE_EDIT_TYPE;
            else if (inc > 0)
                menu_saveOptions.state = SAVE_STATE_EDIT_PRESET_NR;
        }
        return;
    }

    if (btnClicked) {
        if ((editModeActive && menu_saveOptions.state == SAVE_STATE_OK) ||
            (menu_saveOptions.what >= SAVE_TYPE_GLO && menu_saveOptions.state > SAVE_STATE_EDIT_TYPE)) {

            if (menu_activePage == SAVE_PAGE) {
                switch (menu_saveOptions.what) {
                case SAVE_TYPE_FILE:
                    if (preset_saveTestFile(menu_currentTestName()))
                        menu_storageBusy = 1u;
                    break;
                case SAVE_TYPE_DIR:
                    if (preset_saveTestDir(menu_currentTestName()))
                        menu_storageBusy = 1u;
                    break;
                case SAVE_TYPE_SIMPLE_DIR:
                    if (preset_saveTestSimpleDir(menu_currentTestName()))
                        menu_storageBusy = 1u;
                    break;
                case SAVE_TYPE_KIT:
                    if (preset_saveDrumset(
                            menu_currentPresetNr[SAVE_TYPE_KIT], 0u,
                            menu_loadSaveSourceScene))
                        menu_storageBusy = 1u;
                    break;
                case SAVE_TYPE_KIT_MORPH:
                    if (preset_saveDrumset(
                            menu_currentPresetNr[SAVE_TYPE_KIT_MORPH], 1u,
                            menu_loadSaveSourceScene))
                        menu_storageBusy = 1u;
                    break;
                case SAVE_TYPE_SCENE:
                    if (preset_saveScene(
                            menu_currentPresetNr[SAVE_TYPE_SCENE],
                            menu_loadSaveSourceScene))
                        menu_storageBusy = 1u;
                    break;
                case SAVE_TYPE_BANK:
                    if (preset_saveBank(
                            menu_currentPresetNr[SAVE_TYPE_BANK],
                            menu_kitLoadSceneMask))
                        menu_storageBusy = 1u;
                    break;
                case SAVE_TYPE_GLO:     preset_saveGlobals(); break;
                default: break;
                }
                if (menu_saveOptions.what != SAVE_TYPE_FILE &&
                    menu_saveOptions.what != SAVE_TYPE_DIR &&
                    menu_saveOptions.what != SAVE_TYPE_SIMPLE_DIR)
                    /*
                     * Keep the active Kit/Scene cache alive while the save
                     * state machine runs. Its completion phase updates the
                     * saved slot in this cache and regenerates `.hcindex`;
                     * disposing here would make that refresh silently skip.
                     */
                    menu_resetSaveParameters();
            } else {
                switch (menu_saveOptions.what) {
                case SAVE_TYPE_FILE:
                    if (preset_loadTestFile(menu_currentTestName()))
                        menu_storageBusy = 1u;
                    break;
                case SAVE_TYPE_DIR:
                    if (preset_loadTestDir(menu_currentTestName()))
                        menu_storageBusy = 1u;
                    break;
                case SAVE_TYPE_SCENE:
                    if (preset_loadSceneForScenes(
                            menu_currentPresetNr[SAVE_TYPE_SCENE],
                            menu_kitLoadSceneMask)) {
                        menu_storageBusy = 1u;
                    } else {
                        menu_storageBusy = 0u;
                    }
                    break;
                case SAVE_TYPE_BANK:
                    if (preset_loadBank(
                            menu_currentPresetNr[SAVE_TYPE_BANK],
                            menu_kitLoadSceneMask)) {
                        menu_storageBusy = 1u;
                    } else {
                        menu_storageBusy = 0u;
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
            if (inc != 0) {
                uint8_t previous_type = menu_saveOptions.what;
                menu_saveOptions.what =
                    menu_nextRestoredLoadSaveType(menu_saveOptions.what, inc);
                /*
                 * A top-level type change is a cache-domain change even when
                 * the destination is Bank, File, or another non-index row.
                 * Dispose before requesting the new row so no prior Kit/Scene
                 * or Instrument names can survive one encoder detent.
                 */
                if (menu_saveOptions.what != previous_type)
                    filesystem_clearNameCache();
            }
            if (menu_saveOptions.what == SAVE_TYPE_BANK)
                menu_currentPresetNr[SAVE_TYPE_BANK] = bank_restoreBankSlot();
            menu_resetLoadSaveSceneSelection();
            menu_requestCurrentLoadSaveSelection(0);
            menu_refreshLoadSceneLeds();
            break;
        case SAVE_STATE_EDIT_PRESET_NR: {
            if (menu_saveOptions.what == SAVE_TYPE_FILE ||
                menu_saveOptions.what == SAVE_TYPE_DIR ||
                menu_saveOptions.what == SAVE_TYPE_SIMPLE_DIR) {
                uint8_t count = menu_testObjectCount(menu_saveOptions.what);
                int16_t maxPreset = (count == 0u) ? 0 : (int16_t)(count - 1u);
                int16_t newPreset =
                    (int16_t)menu_currentPresetNr[menu_saveOptions.what] +
                    (int16_t)inc;

                /*
                 * File/Dir Load browser index.
                 *
                 * The exact names live in filesystem's scan cache. This
                 * saturating index is the only state Menu owns, so scrolling
                 * cannot synthesize paths or fallback names that asyncfatfs
                 * has not actually reported.
                 */
                if (menu_activePage == SAVE_PAGE) {
                    menu_saveOptions.state = SAVE_STATE_EDIT_NAME1;
                    break;
                }
                if (newPreset < 0)
                    newPreset = 0;
                else if (newPreset > maxPreset)
                    newPreset = maxPreset;
                menu_currentPresetNr[menu_saveOptions.what] =
                    (uint16_t)newPreset;
                break;
            }
            /* Settings and Samples are unnumbered Load/Save choices. Keeping
             * this guard beside the kit-backed menu_currentPresetNr[] access
             * prevents their enum values from being treated as numbered preset
             * coordinates. Kit and KitMrp are the only entries below
             * SAVE_TYPE_GLO. */
            if (menu_saveOptions.what >= SAVE_TYPE_GLO) {
                menu_saveOptions.state = SAVE_STATE_EDIT_TYPE;
                break;
            }
            /* Saturating add - original `kit > 0` and `kit <= 125` checks
            ** assumed |inc|=1 and underflow/overflow on uint8_t wrap. Kit
            ** slots are now uint16_t because the directory layout exposes
            ** 000..999 folders, with 000 real, while the guard keeps non-kit
            ** menu entries away from the numbered-slot path. */
            int16_t maxPreset = (menu_saveOptions.what < SAVE_TYPE_GLO) ? 999 : 125;
            int16_t newPreset = (int16_t)menu_currentPresetNr[menu_saveOptions.what] + (int16_t)inc;
            if (newPreset < 0)        newPreset = 0;
            else if (newPreset > maxPreset) newPreset = maxPreset;
            menu_currentPresetNr[menu_saveOptions.what] = (uint16_t)newPreset;
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
                uint8_t ci = (uint8_t)(menu_saveOptions.state -
                                       SAVE_STATE_EDIT_NAME1);
                if (menu_saveOptions.what == SAVE_TYPE_FILE ||
                    menu_saveOptions.what == SAVE_TYPE_DIR ||
                    menu_saveOptions.what == SAVE_TYPE_SIMPLE_DIR) {
                    /*
                     * Temporary File/Dir save-name editor.
                     *
                     * The first eight characters are editable on the LCD. The
                     * buffer itself is long-name sized so future UI work can
                     * add horizontal scrolling without touching filesystem or
                     * asyncfatfs again.
                     */
                    char c = (char)(menu_testEditName[ci] + inc);
                    if (c < 0x20)
                        c = 0x7e;
                    else if (c > 0x7e)
                        c = 0x20;
                    menu_testEditName[ci] = c;
                    menu_testEditName[FS_TEST_NAME_MAX] = '\0';
                } else {
                    preset_currentName[ci] =
                        (char)(preset_currentName[ci] + inc);
                }
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
                uint8_t previous_state = menu_saveOptions.state;
                menu_saveOptions.state++;
                if (menu_activePage == SAVE_PAGE &&
                    (menu_saveOptions.what == SAVE_TYPE_KIT ||
                     menu_saveOptions.what == SAVE_TYPE_KIT_MORPH ||
                     menu_saveOptions.what == SAVE_TYPE_SCENE ||
                     menu_saveOptions.what == SAVE_TYPE_BANK) &&
                    previous_state == SAVE_STATE_EDIT_PRESET_NR &&
                    menu_saveOptions.state == SAVE_STATE_EDIT_NAME1) {
                    /*
                     * Seed Save name editing from resident identity, not from
                     * the selected library slot.
                     *
                     * Inputs: the user has just moved from slot number editing
                     * to character 1 on the Save page. Output: Kit/KitMrp,
                     * Scene, and Bank each use resident object identity instead
                     * of the selected target slot cache. This matters for empty
                     * target slots: the browser row may display `Empty`, but
                     * the save editor must begin with the currently loaded
                     * object name, such as Bank/000 Slak -> "Slak".
                     */
                    if (menu_saveOptions.what == SAVE_TYPE_BANK) {
                        memcpy(preset_currentName, bank_displayName(), 8u);
                    } else if (menu_saveOptions.what == SAVE_TYPE_SCENE) {
                        memcpy(preset_currentName,
                               scene_sceneDisplayName(menu_loadSaveSourceScene),
                               8u);
                    } else {
                        memcpy(preset_currentName,
                               scene_kitDisplayName(menu_loadSaveSourceScene),
                               8u);
                    }
                }
                if (menu_activePage == LOAD_PAGE) {
                    if (menu_saveOptions.state >= SAVE_STATE_EDIT_NAME1)
                        menu_saveOptions.state = SAVE_STATE_OK;
                    if (menu_saveOptions.state == SAVE_STATE_OK &&
                        menu_saveOptions.what < SAVE_TYPE_GLO &&
                        menu_saveOptions.what != SAVE_TYPE_SCENE &&
                        menu_saveOptions.what != SAVE_TYPE_BANK &&
                        menu_saveOptions.what != SAVE_TYPE_FILE &&
                        menu_saveOptions.what != SAVE_TYPE_DIR)
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
        menu_cell_t cell = menu_resolveCell(activePage, activeParameter);
        return (uint8_t)(cell.kind == MENU_CELL_STATIC &&
                         cell.static_param == paramNr);
    } else {
        const uint8_t is2ndPage = menu_isVoicePage(menu_activePage)
            ? 0u : (uint8_t)((activeParameter > 3) ? 4 : 0);
        uint8_t i;

        for (i = 0; i < 4; i++) {
            menu_cell_t cell =
                menu_resolveCell(activePage, (uint8_t)(i + is2ndPage));
            if (cell.kind == MENU_CELL_STATIC &&
                cell.static_param == paramNr)
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

static uint8_t menu_paramIsMorphAmount(uint16_t paramNr)
{
    /*
     * Identify flat Scene Morph amount controls.
     *
     * Inputs: canonical ParameterArray id. Output: nonzero for the overall
     * Morph bulk-set control and the six per-slot Morph controls. Clients use
     * this to keep encoder/endless-pot behavior consistent across Morph
     * controls without teaching Menu about instrument descriptors or slot
     * parameter lists.
     */
    if (paramNr == PAR_MORPH)
        return 1u;
    return (uint8_t)(paramNr >= PAR_VOICE1_MORPH &&
                     paramNr <= PAR_VOICE6_MORPH);
}

static void menu_updateEndlessPotScales(void)
{
    uint8_t activePage = (uint8_t)((menuIndex & MASK_PAGE) >> PAGE_SHIFT);
    uint8_t activeParameter = menuIndex & MASK_PARAMETER;
    uint8_t is2ndPage = menu_isVoicePage(menu_activePage)
        ? 0u : (uint8_t)((activeParameter > 3) ? 4 : 0);

    for (uint8_t knobNr = 0; knobNr < ENDLESS_POT_COUNT; knobNr++) {
        uint8_t useDouble = 0;
        if (menu_activePage != LOAD_PAGE && menu_activePage != SAVE_PAGE) {
            menu_cell_t cell =
                menu_resolveCell(activePage, (uint8_t)(knobNr + is2ndPage));
            useDouble = (uint8_t)(cell.kind == MENU_CELL_STATIC &&
                                  menu_paramIsMorphAmount(cell.static_param));
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
    if (menu_activePage == LOAD_PAGE || menu_activePage == SAVE_PAGE) {
        menu_handleLoadSaveKnobDelta(knobNr, delta);
        return;
    }

    const uint8_t activePage      = (uint8_t)((menuIndex & MASK_PAGE) >> PAGE_SHIFT);
    const uint8_t activeParameter = menuIndex & MASK_PARAMETER;
    const uint8_t is2ndPage       = menu_isVoicePage(menu_activePage)
        ? 0u : (uint8_t)((activeParameter > 3) ? 4 : 0);
    menu_cell_t cell =
        menu_resolveCell(activePage, (uint8_t)(knobNr + is2ndPage));
    uint16_t value;
    int32_t next;

    if (menu_cellIsEmpty(&cell)) return;
    if (cell.kind == MENU_CELL_STATIC &&
        cell.static_param == PAR_RUNTIME_CPU_USE) return;

    value = menu_cellDisplayValue(&cell);
    if (menu_cellIsLfoTargetVoice(&cell)) {
        if (menu_lfoTargetEditVoice(&cell, delta))
            menu_knobs_dirty = 1;
        return;
    }
    if (menu_cellIsLfoTargetParam(&cell)) {
        if (menu_lfoTargetEditParam(&cell, delta))
            menu_knobs_dirty = 1;
        return;
    }
    if (menu_cellIsVelocityTargetParam(&cell)) {
        if (menu_velocityTargetEditParam(&cell, delta))
            menu_knobs_dirty = 1;
        return;
    }

    if ((menu_cellDtype(&cell) & 0x0f) == DTYPE_TARGET_SELECTION_VELO ||
        (menu_cellDtype(&cell) & 0x0f) == DTYPE_TARGET_SELECTION_LFO) {
        if (value == INSTRUMENT_PARAM_INVALID && delta > 0)
            value = 0u;
        else if (value == INSTRUMENT_PARAM_INVALID)
            return;
        else {
            next = (int32_t)value + (int32_t)delta;
            if (next < 0) value = INSTRUMENT_PARAM_INVALID;
            else if (next > 65535) value = 65535u;
            else value = (uint16_t)next;
        }
    } else {
        next = (int32_t)value + (int32_t)delta;
        if (next < 0) next = 0;
        else if (next > 65535) next = 65535;
        value = (uint16_t)next;
    }

    menu_clampCellValue(&cell, &value);
    if (menu_cellCommitValue(&cell, value))
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
        menu_cell_t cell = menu_resolveCell(activePage, activeParameter);
        return (uint8_t)(cell.kind == MENU_CELL_STATIC &&
                         cell.static_param == PAR_RUNTIME_CPU_USE);
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

    /*
     * Poll deferred Scene-slot commits outside the storage-busy path.
     *
     * Inputs: PERF Scene switching and runtime loads may leave instrument slots
     * pending until their old amp envelopes fall below the quiet threshold.
     * Output: one quiet slot can be committed per foreground pass even after the
     * load UI has been released; trigger-time force-apply remains handled in
     * MidiVoiceControl before individual notes fire.
     */
    if (!menu_soundApplyActive && preset_tickDrumsetApply())
        return;

    if (menu_tickInstrumentApply())
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

    if (menu_testResultActive &&
        (uint16_t)(time_sysTick - menu_testResultStart) >= MENU_TEST_RESULT_MS) {
        /*
         * End the nonblocking File/Dir result overlay.
         *
         * The storage operation is already complete; this timer only controls
         * how long the copied byte/Dir result remains on the LCD before the
         * ordinary test menu is repainted.
         */
        menu_testResultActive = 0u;
        if (menu_activePage == LOAD_PAGE || menu_activePage == SAVE_PAGE)
            menu_repaintAll();
        menu_testResultError = 0u;
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

    if (!preset_getCompletedOk() &&
        !menu_completedOpIsTest(preset_getCompletedOp())) {
        /*
         * Failed OK/OW operations still return to the Load/Save type row.
         *
         * Ordinary filesystem failures used to collapse to PRESET_OP_NONE and
         * silently repaint/reset. Preserve the error from filesystem.c, but
         * reset the Load/Save cursor first whenever the busy flag proves the
         * user launched a command from OK/OW. Output after the overlay clears:
         * the pointer is back on the top row instead of stranded on OK or OW.
         * File/Dir test ops keep their detailed result branch below.
         */
        if (menu_storageBusy &&
            (menu_activePage == LOAD_PAGE || menu_activePage == SAVE_PAGE)) {
            filesystem_clearNameCache();
            menu_resetSaveParameters();
        }
        menu_showFilesystemErrorOverlay();
        preset_ackStatus();
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
         * chunked apply cursor applies descriptor-indexed runtime cells and
         * image values as it works. Running the old
         * parameter_values[] normalizer here would inspect stale flat data
         * before the real loaded Scene data has been applied.
         */
        menu_startSoundApply(1u, 0u, 1u, 0u, 0u, 0u, 0u, 0u,
                             FS_STALE_WARNING_NONE);
        break;
    }

    case PRESET_OP_KIT_MORPH_LOAD:
    {
        if ((menu_activePage == LOAD_PAGE || menu_activePage == SAVE_PAGE) &&
            !menu_isLoadSaveSelectionCurrent()) {
            retrySelectionAfterAck = 1;
            retrySelectionLoadKit = 1;
            break;
        }

        /*
         * KitMrp completion keeps the resident kit identity.
         *
         * The selected Kit/ directory has only been staged by filesystem.
         * Preset now copies same-type normal endpoint values into current morph
         * endpoints and drains the Morph worker without applying routing,
         * replacing types, or rebinding modulation targets.
         */
        menu_startKitMorphApply();
        break;
    }

    case PRESET_OP_SCENE_LOAD:
    {
        if ((menu_activePage == LOAD_PAGE || menu_activePage == SAVE_PAGE) &&
            !menu_isLoadSaveSelectionCurrent()) {
            retrySelectionAfterAck = 1;
            retrySelectionLoadKit = 0;
            break;
        }

        /*
         * Scene Load completion applies the active Scene's runtime surface.
         *
         * filesystem.c already replaced selected resident SceneData atomically.
         * Menu now starts the same bounded sound-apply cursor used by Kit Load,
         * but also refreshes pattern/menu/global-facing Scene setting displays.
         * Inputs are retained SceneData; outputs are mixer routing, instrument
         * runtime cells, Morph images, and a repaint after the cursor drains.
         */
        {
            /*
             * Scene Load is an explicit OK/OW operation, unlike live Kit slot
             * browsing. When launched from Load/Save, resetSave returns the
             * cursor to the type row after the sound-apply cursor drains; boot
             * Scene Load passes 0 so early startup does not mutate menu state.
             */
            uint8_t reset_save =
                (uint8_t)(menu_activePage == LOAD_PAGE ||
                          menu_activePage == SAVE_PAGE);
            menu_startSoundApply(1u, reset_save, 1u, 0u, 1u, 0u, 1u, 0u,
                                 FS_STALE_WARNING_NONE);
        }
        break;
    }

    case PRESET_OP_BANK_LOAD:
    {
        if ((menu_activePage == LOAD_PAGE || menu_activePage == SAVE_PAGE) &&
            !menu_isLoadSaveSelectionCurrent()) {
            retrySelectionAfterAck = 1;
            retrySelectionLoadKit = 0;
            break;
        }

        /*
         * Bank Load completion has two valid payload shapes.
         *
         * When filesystem_lastBankLoadLoadedScene() is nonzero, filesystem.c
         * has already committed one Bank-local Scene into the resident Scene
         * workspace and Menu starts the same runtime apply cursor as explicit
         * Scene Load. When it is zero, the Bank was intentionally empty:
         * acknowledge the Bank identity load first, then start the fallback
         * ladder so Preset's single-operation gate is idle before the new
         * Scene/Kit request is posted.
         */
        if (!preset_completedBankLoadedScene()) {
            preset_ackStatus();
            if (preset_loadFirstAvailableSceneOrKit()) {
                menu_storageBusy = 1u;
            } else {
                menu_storageBusy = 0u;
                menu_resetSaveParameters();
                menu_repaintAll();
            }
            return;
        }

        {
            uint8_t reset_save =
                (uint8_t)(menu_activePage == LOAD_PAGE ||
                          menu_activePage == SAVE_PAGE);
            menu_startSoundApply(1u, reset_save, 1u, 0u, 1u, 0u, 1u, 0u,
                                 FS_STALE_WARNING_NONE);
        }
        break;
    }

    case PRESET_OP_GLOBALS_LOAD:
    {
        fs_stale_warning_source_t stale_src = filesystem_takeStaleGlobalsWarning();
        menu_startGlobalApply((uint8_t)(menu_activePage == LOAD_PAGE || menu_activePage == SAVE_PAGE),
                              (uint8_t)(menu_activePage != LOAD_PAGE && menu_activePage != SAVE_PAGE));
        /*
         * settings.cfg has no legacy raw fallback warning. The stale-warning
         * path remains for .all compatibility, but standalone settings load
         * either applies keyed text or keeps defaults when the file is absent.
         */
        (void)stale_src;
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
        /*
         * Legacy morph-file load refreshes endpoint data. It must not call the
         * overall Morph bulk-set path because that would collapse distinct
         * per-voice Morph amounts. Rebuild from retained Scene values instead.
         */
        preset_rebuildMorph();
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

    case PRESET_OP_INSTRUMENT_LOAD:
        /*
         * Single Instrument load completion.
         *
         * Inputs: Preset request slot retained when the Instrument/ file load
         * was posted. Output: only that slot is applied through Preset's
         * one-slot cursor. This must not call menu_startSoundApply(), because
         * that path is intentionally a whole-Kit/Scene apply operation.
         */
        menu_startInstrumentApply(preset_getRequestScene(),
                                  preset_getRequestSlot());
        menu_refreshLoadSceneLeds();
        break;

    case PRESET_OP_INSTRUMENT_MORPH_LOAD:
        /*
         * Single InstrumentMrp completion.
         *
         * Inputs: the destination Scene/slot retained when the Instrument/ file
         * load was posted. Output: only that slot's morph endpoint is updated,
         * and only if the staged type still matches the resident slot type.
         */
        menu_startInstrumentMorphApply(preset_getRequestScene(),
                                       preset_getRequestSlot());
        menu_refreshLoadSceneLeds();
        break;

    case PRESET_OP_TEST_SCAN:
        /*
         * Root File/Dir scan completion.
         *
         * Clamp the browser index to the newly scanned cache and repaint the
         * exact-case display names. No preset state is applied because this is
         * only the asyncfatfs test browser surface. Failed scans use the same
         * ERR overlay as failed opens so temporary storage diagnostics never
         * fail silently.
         */
        menu_storageBusy = 0u;
        if (!preset_getCompletedOk()) {
            menu_testResultError = 1u;
            menu_testResultActive = 1u;
            menu_testResultStart = time_sysTick;
            menu_repaintAll();
            break;
        }
        if ((menu_saveOptions.what == SAVE_TYPE_FILE ||
             menu_saveOptions.what == SAVE_TYPE_DIR) &&
            menu_currentPresetNr[menu_saveOptions.what] >=
                menu_testObjectCount(menu_saveOptions.what)) {
            uint8_t count = menu_testObjectCount(menu_saveOptions.what);
            menu_currentPresetNr[menu_saveOptions.what] =
                (count == 0u) ? 0u : (uint16_t)(count - 1u);
        }
        menu_repaintAll();
        break;

    case PRESET_OP_TEST_FILE_LOAD:
    case PRESET_OP_TEST_DIR_LOAD:
    case PRESET_OP_TEST_FILE_SAVE:
    case PRESET_OP_TEST_DIR_SAVE:
    {
        const uint8_t *bytes = filesystem_testResultBytes();
        uint8_t completed_ok = preset_getCompletedOk();

        /*
         * Snapshot the filesystem test result for a timed LCD overlay.
         *
         * The result storage in filesystem.c is reused by later requests, so
         * Menu copies it before acking Preset. The overlay is Menu-owned and
         * expires by wall-clock tick without blocking audio or filesystem
         * polling. Failed test operations deliberately keep their op identity
         * through Preset so this branch can show ERR instead of silently
         * repainting the browser.
         */
        menu_storageBusy = 0u;
        menu_testResultError = (uint8_t)!completed_ok;
        menu_testResultKind = completed_ok ? filesystem_testResultKind()
                                           : FS_TEST_RESULT_BYTES_READY;
        memcpy(menu_testResultBytes, bytes, FS_TEST_RESULT_BYTES);
        memset(menu_testResultName, 0, sizeof(menu_testResultName));
        strncpy(menu_testResultName, filesystem_testResultName(),
                FS_TEST_NAME_MAX);
        menu_testResultActive = 1u;
        menu_testResultStart = time_sysTick;
        if (menu_activePage == LOAD_PAGE || menu_activePage == SAVE_PAGE) {
            /*
             * File/Dir diagnostic load/save commands are launched from the
             * same OK/OW confirmation row as product loads and saves. The
             * timed result overlay is preserved, but the underlying
             * SaveOptions state is restored immediately so the next repaint
             * gives a visible completion cue on the type row.
             */
            filesystem_clearNameCache();
            menu_instrumentLoadActive = 0u;
            menu_instrumentSaveMode = 0u;
            editModeActive = 1u;
            menu_saveOptions.state = SAVE_STATE_EDIT_TYPE;
        }
        menu_repaintAll();
        break;
    }

    case PRESET_OP_INSTRUMENT_SAVE:
    case PRESET_OP_INSTRUMENT_MORPH_SAVE:
    case PRESET_OP_KIT_SAVE:
    case PRESET_OP_KIT_MORPH_SAVE:
    case PRESET_OP_SCENE_SAVE:
    case PRESET_OP_BANK_SAVE:
    case PRESET_OP_GLOBALS_SAVE:
    case PRESET_OP_PATTERN_SAVE:
    case PRESET_OP_ALL_SAVE:
    case PRESET_OP_PERFORMANCE_SAVE:
        /*
         * Save completion cleanup.
         *
         * Normal and Morph-projected Kit/Instrument saves mutate only storage,
         * not resident SceneData or DSP runtime. They can therefore share the
         * same busy-state and Save UI reset path as the existing save
         * completions while preserving distinct operation enums for dispatch
         * and future result messaging.
        */
        menu_storageBusy = 0u;
        if (preset_getCompletedOp() == PRESET_OP_KIT_SAVE ||
            preset_getCompletedOp() == PRESET_OP_KIT_MORPH_SAVE ||
            preset_getCompletedOp() == PRESET_OP_SCENE_SAVE ||
            preset_getCompletedOp() == PRESET_OP_BANK_SAVE) {
            /*
             * The filesystem callback is intentionally delayed until the
             * directory rescan and `.hcindex` rewrite are complete. Keep that
             * fresh cache alive, copy its current slot into the Save editor's
             * display buffer, and then reset/repaint without changing type or
             * slot. Other save families still dispose their unrelated cache.
             */
            menu_refreshSavedLibraryName(preset_getCompletedOp());
        } else {
            filesystem_clearNameCache();
        }
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

    if (menu_isVoicePage(menu_activePage)) {
        /*
         * Voice SELECT buttons choose a sub-page and cycle its four-parameter screens.
         *
         * Inputs: subPageNr is the pressed SELECT button in VOICE mode. Output:
         * pressing a different SELECT button switches to that sub-page and
         * resets its screen to the first four parameters; pressing the same
         * SELECT button advances to the next four-parameter screen that
         * contains any selectable parameter, or loops back to the first when
         * no later screen contains one. The screen memory is per sub-page and
         * is not cleared by voice/mode changes.
         */
        if (subPageNr >= NUM_SUB_PAGES)
            subPageNr = 0u;
        if (activePage >= NUM_SUB_PAGES)
            activePage = subPageNr;
        if (subPageNr == activePage) {
            uint8_t screen = menu_voiceSubPageScreen[activePage];
            if (screen >= menu_voiceSubPageScreenCount(activePage) ||
                !menu_voiceSubPageScreenExists(activePage, screen)) {
                screen = 0u;
            }
            if (menu_voiceSubPageScreenExists(activePage,
                                              (uint8_t)(screen + 1u)))
                screen++;
            else
                screen = 0u;
            menu_voiceSubPageScreen[activePage] = screen;
            activeParameter =
                menu_voiceFirstSelectableColumn(activePage, screen);
        } else {
            activePage = subPageNr;
            menu_voiceSubPageScreen[activePage] = 0u;
            activeParameter =
                menu_voiceFirstSelectableColumn(activePage, 0u);
        }

        menuIndex = (uint8_t)((activePage << PAGE_SHIFT) | activeParameter);
        menu_endlessPotMappingChanged();
        return;
    }

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
                menu_cell_t nextCell =
                    menu_resolveCell((uint8_t)(activePage + 1u), 0u);
                if (activePage < NUM_SUB_PAGES-1 &&
                    !menu_cellIsEmpty(&nextCell))
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
    if (menu_isVoicePage(menu_activePage)) {
        /*
         * Repair a remembered voice screen after instrument/page changes.
         *
         * Inputs: current SELECT sub-page and its remembered screen. Output:
         * when an instrument swap leaves that screen empty, fall back to the
         * first screen, which every voice sub-page is expected to define.
         * The sub-page guard covers cross-mode entry where menuIndex may still
         * hold a non-voice sub-page value; repairing it here keeps later
         * display and knob paths on the first valid voice screen.
         */
        if (activePage >= NUM_SUB_PAGES)
            activePage = 0u;
        if (!menu_voiceSubPageScreenExists(activePage,
                                           menu_voiceSubPageScreen[activePage]))
            menu_voiceSubPageScreen[activePage] = 0u;
        menuIndex = (uint8_t)(
            (activePage << PAGE_SHIFT) |
            menu_voiceFirstSelectableColumn(activePage,
                                            menu_voiceSubPageScreen[activePage]));
        return;
    }
    if (!has2ndPage(activePage))
        menuIndex &= (uint8_t)(~MASK_PARAMETER);
}

/* -----------------------------------------------------------------------
** menu_switchPage — exact port (sequencer/LED calls stubbed where needed)
** ----------------------------------------------------------------------- */
void menu_switchPage(uint8_t pageNr)
{
    if (menu_storageBusy) return;

    if ((menu_activePage == LOAD_PAGE || menu_activePage == SAVE_PAGE) &&
        pageNr != LOAD_PAGE) {
        /*
         * Leaving the Load/Save surface disposes the shared browser names.
         * This covers top-level Kit/Scene/Bank exits as well as nested
         * Instrument exits; a later entry must reload the appropriate index
         * rather than inheriting names from the page that was left.
         */
        filesystem_clearNameCache();
    }
    if (menu_instrumentLoadActive) {
        filesystem_clearNameCache();
        menu_instrumentLoadActive = 0u;
        menu_instrumentSaveMode = 0u;
    }

    led_clearSequencerLeds();

    switch (pageNr) {
    case MENU_MIDI_PAGE: {
        menu_instrumentLoadActive = 0u;
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
        menu_instrumentLoadActive = 0u;
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
        if (menu_activePage == LOAD_PAGE) {
            menu_activePage = SAVE_PAGE;
            menu_instrumentLoadActive = 0u;
        } else {
            menu_activePage = LOAD_PAGE;
            /* Reset Kit targets on each fresh Load entry. The active Scene is
             * always selected so a current one-Scene build and future banks
             * share the same non-empty request invariant. */
            menu_kitLoadSceneMask =
                (uint16_t)(1u << scene_getActiveIndex());
        }
        /* Load/Save page toggles are type transitions, so reload on entry. */
        filesystem_clearNameCache();
        menu_resetSaveParameters();
        menu_requestCurrentLoadSaveSelection(0);
        menu_refreshLoadSceneLeds();
        break;

    default: /* voice pages */
        menu_instrumentLoadActive = 0u;
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
        /*
         * PERF owns the SEQ row as the 16-Scene selector/status display.
         *
         * menu_switchPage() clears sequencer LEDs before changing pages, so the
         * PERF Scene repaint must happen here, after the clear. Mute LEDs remain
         * on the VOICE row through buttonHandler_showMuteLEDs(); the SEQ row is
         * then repainted so steady LEDs show Scenes whose Pattern has active
         * steps and the active resident Scene blinks immediately on entry instead
         * of waiting for a later button press or async repaint.
         */
        buttonHandler_showMuteLEDs();
        menu_refreshPerfSceneLeds();
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
    /*
     * Reset Load/Save cursor state without reopening stale menu entries.
     *
     * Diagnostic entries and promoted musical entries are gated in the type
     * whitelist, so a completion from a legacy helper cannot leave the panel
     * parked on an unvalidated path. Kit is the fallback because it is a
     * real musical object on both Load and Save when CONFIG_DEV_MODE hides
     * File/Dir/sDir. Every Load/Save entry resets to the selected top-row type
     * field; slot/name selection is always a deliberate second movement. The
     * shared browser cache is intentionally not disposed here: this helper is
     * also called immediately after a Save request is posted, and the
     * filesystem needs the active cache to update and rewrite `.hcindex`.
     * Callers that actually leave or change the Load/Save type dispose it
     * explicitly before invoking this cursor reset.
     */
    if (!menu_loadSaveTypeIsRestored(menu_saveOptions.what))
        menu_saveOptions.what = SAVE_TYPE_KIT;

    menu_instrumentLoadActive = 0u;
    menu_instrumentSaveMode = 0u;
    menu_loadSaveClearInstrumentVoiceBlinks();
    editModeActive = 1;
    menu_saveOptions.state = SAVE_STATE_EDIT_TYPE;
    if (menu_saveOptions.what == SAVE_TYPE_BANK)
        menu_currentPresetNr[SAVE_TYPE_BANK] = bank_restoreBankSlot();
    menu_resetLoadSaveSceneSelection();
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
         * generator controls under Core/Bank/Scene/Pattern, not sequencer transport
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
        scene_setTrackMidiNote(menu_getViewedPattern(), track, value);
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
    {
        uint16_t edit_mask = bank_sceneMaskVoiceEdit();
        uint8_t scene_index;

        /*
         * Overall Morph is Scene performance state and follows the VOICE edit
         * Scene mask.
         *
         * Inputs: PERF Morph value and BankData's scene_mask_voice_edit.
         * Output: every masked Scene receives the bulk Morph amount; only the
         * active Scene updates the flat PERF mirrors and runtime Morph worker.
         */
        for (scene_index = 0u;
             scene_index < SCENE_COUNT && scene_index < 16u;
             scene_index++) {
            if ((edit_mask & (uint16_t)(1u << scene_index)) != 0u)
                preset_morphScene(scene_index, value);
        }
        break;
    }

    case PAR_VOICE1_MORPH:
    case PAR_VOICE2_MORPH:
    case PAR_VOICE3_MORPH:
    case PAR_VOICE4_MORPH:
    case PAR_VOICE5_MORPH:
    case PAR_VOICE6_MORPH:
        /*
         * Per-voice Morph is Scene-level performance state.
         *
         * Inputs: flat PERF parameter id and 0..255 value. Output: Preset
         * updates only the selected slot's retained Morph amount and queues
         * only that slot in the descriptor-driven Morph worker. This belongs
         * here, not in descriptor VOICE pages, because the PERF cells are
         * static Scene controls rather than instrument parameter descriptors.
         */
    {
        uint16_t edit_mask = bank_sceneMaskVoiceEdit();
        uint8_t scene_index;
        uint8_t slot = (uint8_t)(paramNr - PAR_VOICE1_MORPH);

        for (scene_index = 0u;
             scene_index < SCENE_COUNT && scene_index < 16u;
             scene_index++) {
            if ((edit_mask & (uint16_t)(1u << scene_index)) != 0u)
                preset_morphVoiceScene(scene_index, slot, value);
        }
        break;
    }

    case PAR_VOICE_DECIMATION_ALL:
        /*
         * Scene global decimation is retained with other Scene settings.
         *
         * Inputs: PERF "srt" value 0..127. Output: Preset stores the setting
         * and applies mixer_decimation_rate[6] using the same taper as the
         * legacy VOICE_DECIMATION_ALL MIDI CC. Keeping the write behind Preset
         * gives future sceneset.scg load/save one owner boundary.
         */
    {
        uint16_t edit_mask = bank_sceneMaskVoiceEdit();
        uint8_t scene_index;

        for (scene_index = 0u;
             scene_index < SCENE_COUNT && scene_index < 16u;
             scene_index++) {
            if ((edit_mask & (uint16_t)(1u << scene_index)) != 0u)
                preset_setVoiceDecimationAll(scene_index, value);
        }
        break;
    }

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
         * Retired pattern-repeat edit hook.
         *
         * The Pattern Settings row no longer exposes this parameter, and
         * PatternData ignores stale writes. Scene-level switching must own any
         * future repeat/advance feature so Pattern and Scene parameters remain
         * aligned.
         */
        pat_setPatternChangeBar(menu_getViewedPattern(), value);
        break;

    case PAR_PATTERN_NEXT:
        /*
         * Retired pattern-next edit hook.
         *
         * Pattern-only next targets are disabled. This compatibility branch keeps
         * old parameter ids harmless until the Phase 4 Pattern rebuild removes or
         * replaces them.
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
     * Output: the UI Pattern index follows the resident Scene/Pattern slot when
     * valid, otherwise it falls back to Scene 0. Risk: this setter does not
     * repaint LEDs or reload PatternData params; callers must do that explicitly.
     */
    menu_shownPattern = pat_patternValid(p) ? p : 0u;
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
     * Scene global sample-rate/decimation must default to full rate.
     *
     * SceneData also initializes voice_decimation_all to 127, but Menu's flat
     * parameter mirror is memset to zero above and can be used by early global
     * apply paths before a Scene settings apply has mirrored the retained
     * value. A zero here shapes mixer_decimation_rate[6] to 0, so the decimator
     * never refreshes voice samples and the unit presents as silent. Keep the
     * mirror's undefined/startup value aligned with the Scene default.
     */
    parameter_values[PAR_VOICE_DECIMATION_ALL] = 127u;
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
