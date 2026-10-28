/*
 * presetManager.c — LXR-02 preset load/save (asyncfatfs version).
 *
 * All SD card operations are delegated to filesystem.c. Load/save functions
 * post a request and return immediately, setting status to
 * PRESET_LOAD_IN_PROGRESS.
 *
 * When the filesystem completes, the completion callback sets status to
 * PRESET_UPDATE_READY and records which operation completed.
 *
 * The menu main loop calls preset_getStatus(). When it sees UPDATE_READY,
 * it applies post-load logic (mod target gap index, repaint, globals
 * apply, etc.) then calls preset_ackStatus() to return to IDLE.
 *
 * This replicates the natural delay the original LXR two-MCU design
 * provided: the AVR owned the SD card and trickle-fed parameters to
 * the STM32 via UART. The STM32 never saw parameters until the transfer
 * was complete.
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


#include "presetManager.h"
#include "menu.h"
#include "CcNr2Text.h"
#include "ParameterArray.h"
#include "sequencer.h"
#include "modulationNode.h"
#include "DrumVoice.h"
#include "CymbalVoice.h"
#include "HiHat.h"
#include "Snare.h"
#include "filesystem.h"
#include "storageTypes.h"
#include "presetMorphEngine.h"
#include "BankData.h"
#include "Autosave.h"
#include "AutosaveTrace.h"
#include "EffectsManager.h"
#include "copyClearService.h"
#include "mixer.h"
#include "timebase.h"
#include "valueShaper.h"
#include <string.h>
#include <stdint.h>

char preset_currentName[8];

/* -----------------------------------------------------------------------
** Status state machine
** ----------------------------------------------------------------------- */
static volatile preset_status_t  pm_status = PRESET_IDLE;
static volatile preset_op_type_t pm_completed_op = PRESET_OP_NONE;
/*
 * Success flag for the most recent filesystem completion.
 *
 * The old menu often treats failed load/save as "no completed op", but the
 * asyncfatfs expansion tests must distinguish a real zero-byte result from a
 * failed open/read. This flag lets Menu show an explicit ERR overlay while
 * leaving the existing operation enum and musical completion paths intact.
 */
static volatile uint8_t          pm_completed_ok = 0u;
static volatile uint16_t         pm_request_slot = 0;
static volatile uint8_t          pm_request_type = SAVE_TYPE_KIT;
/* Legacy persistence requests survive temporarily for format compatibility,
 * but they no longer occupy Load/Save menu types. Keeping these out of
 * menu.h prevents invisible UI slots from being selected by encoder stepping. */
enum {
    PRESET_REQUEST_LEGACY_MORPH = NUM_SAVE_TYPES,
    PRESET_REQUEST_PATTERN,
    PRESET_REQUEST_LEGACY_PERFORMANCE,
    PRESET_REQUEST_LEGACY_ALL,
};
static volatile uint8_t          pm_instrument_request_slot = 0u;
static volatile uint8_t          pm_instrument_request_scene = 0u;
static volatile instrument_type_t pm_instrument_request_type = INSTRUMENT_TYPE_UNKNOWN;
/* The selected Instrument row can be any of the shared cache's 1,000 entries. */
static volatile uint16_t         pm_instrument_request_index = 0u;
static volatile uint16_t         pm_kit_request_scene_mask = 0u;
static uint16_t                  pm_bank_load_failed_scene_mask = 0u;
/*
 * Background Bank Load orchestration state (+1 B SRAM1, S082 C7).
 *
 * What: low nibble = phase (0 idle / start a pending MIDI request, 1 decide,
 * 2 quiesce, 3 build or rebuild and switch, 4 post the load, 5 wait for
 * completion, 6 MIDI origin: wait for the Load:[Bank] preview); flags =
 * unlocked (bit 3), MIDI origin, Replace, Scene 17 needed, dirty mask
 * discarded.
 * Why: the request is accepted immediately and advanced one
 * bounded step per menu_pollPresetStatus() pass; the slot and outcome masks
 * reuse pm_request_slot and pm_kit_request_scene_mask, and the empty mask is
 * derived from Replace (empty = ~act) exactly as the Menu computed it.
 * Inputs: preset_requestBackgroundBankLoad().
 * Outputs: preset_tickBackgroundBankLoad(), preset_backgroundBankLoadRunning().
 * Lifetime: static. Owner: presetManager.c.
 * Affiliates: ccSvc_dropQueued(), filesystem_setAutosaveYield(),
 * preset_buildBackgroundScene(), seq_switchToBackgroundScene(),
 * seq_moveTracksToBackgroundScene().
 */
static uint8_t                   pm_bg_phase = 0u;
#define PM_BG_PHASE_MASK      0x07u
/*
 * The load is unlocked for the panel (S082 P3 W3, W4, D40).
 *
 * What: set on entry to phase 5 when the Bank Load was posted progressively
 * (Scene 17 load, or a direct load with the transport running); cleared with
 * every other flag by preset_bgEnd(). Phase values are 0-6, so bit 3 was
 * unused under the former 0x0F mask.
 * Why: Menu, ButtonHandler and the LED owner need one test for "the user may
 * work while this load finishes"; phases 1-4 and 6, and stopped loads, keep
 * the S082 P1 lock (D38).
 * Inputs: phase 4 acceptance. Outputs: preset_backgroundBankLoadUnlocked().
 * Affiliates: PRESET_BANK_LOAD_PROGRESSIVE, preset_backgroundBankLoadLocked().
 */
#define PM_BG_UNLOCKED        0x08u
#define PM_BG_ORIGIN_MIDI     0x10u
#define PM_BG_REPLACE         0x20u
/*
 * Scene 17 is needed for this request (S082 review F1).
 *
 * What: set by phase 1 when the transport runs and a played Scene is loaded or
 * emptied (or a track already plays Scene 17). Clear means "load directly":
 * no copy/clear drop, no AutoSave yield or discard, no Scene 17 build/switch.
 * Why: phase 3 used to build and switch to Scene 17 on every request, so even
 * a stopped Bank Load (D14) or a Merge that kept every played Scene (D25)
 * ended on Scene 17 with AutoSave suspended, changing the user's Scene
 * against S9/S25.
 * Accessors: preset_tickBackgroundBankLoad() phases 1 and 3.
 */
#define PM_BG_NEEDED          0x40u
#define PM_BG_DISCARDED       0x80u
#define PM_BG_PHASE_MIDI_UI   6u

/*
 * Pending MIDI Bank load request (S082 P1 D33).
 *
 * What: the Bank slot of the latest MIDI request not yet started, or
 * PM_BG_PENDING_NONE. Why: a MIDI request that arrives while Preset, the
 * filesystem facade or a Menu command is busy is deferred, latest wins, and
 * started by phase 0 once everything is idle; a request while a Bank load is
 * in flight is dropped instead (D13). A Menu Bank OK discards it (R5).
 * Inputs: preset_requestBackgroundBankLoad() (MIDI origin).
 * Outputs: preset_tickBackgroundBankLoad() phase 0.
 *
 * Second use while a load is in flight (S082 P1 assessment A8): from phase 1
 * to the return to phase 0 no request can be pending, because a MIDI request
 * during a load is dropped (D13) and a Menu request is refused. The register
 * then holds the time_sysTick at which the current bounded wait (phase 2, 4
 * or 6) began. Every return to phase 0 restores PM_BG_PENDING_NONE.
 * SRAM: 2 B, SRAM1 static (.data, initialiser 0xFFFF), owner Preset (accepted
 * 2026-10-18, S082 P1 §6); the second use adds no storage.
 * Affiliates: menu_backgroundBankLoadUiIdle(), menu_backgroundBankLoadUiBegin(),
 * preset_bgSetPhase(), preset_bgWaitExpired().
 */
#define PM_BG_PENDING_NONE    0xFFFFu
static uint16_t                  pm_bg_pending_slot = PM_BG_PENDING_NONE;

/*
 * Longest bounded wait of a background Bank load (S082 P1 assessment A8).
 *
 * What: 20 s of time_sysTick for each of phase 2 (copy/clear job, AutoSave
 * close-down, HCNAMES publication, facade idle), phase 4 (card busy before the
 * Bank Load is accepted) and phase 6 (MIDI Bank index and child preview).
 * Why: while a load runs every panel exit is dropped (R7), so a wait that never
 * ends would leave Load:[Bank] locked until a power cycle. The limit is far
 * above the slowest legitimate wait (a full HCNAMES rewrite or a 16-child
 * preview on a slow card) and fits the 16-bit tick (65 s wrap).
 * Accessors: preset_bgWaitExpired().
 */
#define PM_BG_WAIT_LIMIT_MS   20000u

/*
 * Result of the last background Bank load (S082 P1 D34).
 *
 * What: PRESET_BG_RESULT_NONE / _DONE / _FAILED in the low bits, plus
 * PRESET_BG_RESULT_MIDI when the load had MIDI origin. Why: the origin must
 * learn completion or failure (S083 sends CC0 TX from it); pm_bg_phase has
 * no free bit. Inputs: on_bank_load_complete() (phase 5), every failure
 * path of the tick, phase 6 refusal. Outputs:
 * preset_takeBackgroundBankLoadResult() (read and clear).
 * SRAM: 1 B, SRAM1 static, owner Preset (accepted 2026-10-18, S082 P1 §6).
 */
static uint8_t                   pm_bg_result = PRESET_BG_RESULT_NONE;

static void preset_markRequestedScenesPresentOnSuccessfulLoad(void)
{
    /*
     * Promote loaded target Scenes into the resident Bank present mask.
     *
     * Inputs: pm_kit_request_scene_mask was captured when the load was accepted,
     * and filesystem_status() still reflects the just-completed async request.
     * Output: successful Kit, Scene, and Instrument loads mark their destination
     * Scenes as present so PERF switching and MODE VOICE fan-out can address
     * them immediately. Failed loads do not change BankData, preserving the last
     * known resident child map. KitMrp/InstrumentMrp intentionally do not call
     * this helper because morph-only loads do not assign an empty Scene.
     */
    if (filesystem_status() == FS_STATUS_DONE) {
        bank_setScenePresentMask((uint16_t)(bank_scenePresentMask() |
                                           pm_kit_request_scene_mask));
    }
}

/* Runtime loaded-kit apply cursor.
 *
 * Why this changed: directory Kit files now parse into Scene-owned
 * kit_instrument_slot_t images, while the legacy implementation only applied
 * modulation routing from parameter_values[]. The cursor therefore owns the
 * bounded post-load bridge from Scene state to current DSP affiliates.
 *
 * Inputs: preset_startDrumsetApply() selects the active Scene and arms a bit
 * per instrument slot. preset_tickDrumsetApply() scans that mask for one slot
 * whose currently sounding amp envelope is quiet. Trigger dispatch can also
 * force one pending slot through preset_applyDeferredSceneSlotForTrigger().
 *
 * Outputs: mixer_audioRouting[], instrument runtime type/parameters,
 * supplemental modulation targets, and Morph interpolation are updated from
 * SceneData only when a slot commits. Immediate Scene-wide settings such as
 * Morph menu mirrors and global decimation apply at Scene switch time.
 */
static uint8_t drumset_apply_active = 0;
/*
 * A Scene activation normally waits for each pending voice's amp envelope to
 * become quiet before replacing that voice's runtime image. Continuous play
 * can keep an envelope non-quiet indefinitely, which leaves this foreground
 * worker active and consequently keeps Menu's accepted Load command from
 * reaching its terminal index restore and command cleanup. The existing
 * trigger-time force path already defines the accepted recovery behavior for
 * a voice that cannot wait; this one-second bound selects that path after
 * 1,000 main-tick passes without quiet progress. The value is a timing bound,
 * not a new persistence or audio image.
 */
#define DRUMSET_APPLY_FORCE_TICKS 1000u
/* Consecutive non-quiet foreground passes for the currently armed worker. */
static uint16_t drumset_apply_stall_ticks = 0u;
static uint8_t drumset_apply_voice = 0;
static uint8_t drumset_apply_scene = 0u;
static uint16_t drumset_apply_pending_mask = 0u;
/*
 * Per-track single-voice apply worker (S077 P2 §3.4).
 *
 * What: one bit per instrument slot awaiting a deferred per-track instrument
 * swap. The source Scene is not stored here; it is read from
 * seq_perTrackPattern[slot] at commit time, so no per-slot Scene RAM is needed
 * and a later reassignment simply re-arms. Why: assigning a track to another
 * Scene must load that track's slot from the played Scene using the same
 * envelope-quiet deferral as the drumset worker while leaving the other slots
 * untouched. Outputs: preset_tickSingleVoiceApply() commits at most one quiet
 * slot per foreground pass. RAM: +1 byte SRAM1. Owner: Preset. Affiliates:
 * preset_startSingleVoiceApply(), preset_tickSingleVoiceApply(),
 * preset_resetAndApplyKitVoiceImage(), instrumentManager_ampEnvelopeQuiet().
 */
static uint8_t single_voice_pending_mask = 0u;
static uint8_t instrument_apply_active = 0u;
static uint8_t instrument_apply_scene = 0u;
static uint8_t instrument_apply_phase = 0u;
static uint8_t instrument_apply_rebind_source = 0u;
static uint8_t instrument_apply_morph_only = 0u;
enum {
    INSTRUMENT_APPLY_PHASE_MORPH_REBUILD = 0u,
    INSTRUMENT_APPLY_PHASE_TARGET_REBIND
};
static uint8_t preset_morph_initialized = 0;

/*
 * Runtime-only Scene-setting step overlays for the VOICE superpage.
 *
 * Inputs: seq_applySceneAutomation() writes one slot's transient audio route
 * or FX-send amount. Outputs: the effective-value getters expose the overlay
 * to Menu while the active DSP owner receives its separate runtime update.
 * Retained SceneData, AutoSave, and Bank-clean state are never changed here.
 * Each table is six entries of {active,value}, or 12 bytes of normal SRAM.
 * Transport restore and preset_init() clear both tables.
 *
 * Affiliates: preset_applyVoiceAudioOutRuntime() owns the mixer route write;
 * mixer_calcNextSampleBlock() pulls the effective FX-send getter each block.
 * presetMorphEngine.c owns the equivalent per-voice Morph overlay and remains
 * separate because Morph has a worker.
 */
static struct {
    uint8_t active;
    uint8_t route;
} audio_out_step_override[INSTRUMENT_SLOT_COUNT];

static struct {
    uint8_t active;
    uint8_t amount;
} fx_send_step_override[INSTRUMENT_SLOT_COUNT];

preset_status_t preset_getStatus(void)
{
    return pm_status;
}

preset_op_type_t preset_getCompletedOp(void)
{
    return pm_completed_op;
}

uint8_t preset_getCompletedOk(void)
{
    return pm_completed_ok;
}

uint16_t preset_getRequestSlot(void)
{
    return pm_request_slot;
}

uint8_t preset_getRequestType(void)
{
    return pm_request_type;
}

uint8_t preset_getRequestScene(void)
{
    /*
     * Expose the single Scene captured for an Instrument action or Kit Save.
     *
     * Output: Preset's retained destination/source Scene index. Menu reads it
     * at async completion to apply the same Instrument coordinate or refresh
     * the same Kit Save HCNAMES block that the filesystem request captured.
     * This cannot be folded into preset_getRequestSlot(): library slot and
     * resident Scene are independent coordinates, and using later Menu state
     * would let an asynchronous completion target the wrong resident object.
     */
    return pm_instrument_request_scene;
}

uint16_t preset_getKitRequestSceneMask(void)
{
    /*
     * Expose the immutable resident Scene mask captured for a Kit-family load.
     *
     * Output: the exact mask accepted by preset_loadKitForScenes() or its Morph
     * counterpart. Menu uses it after normal full Kit Load to update one Kit
     * row plus six Instrument rows for every committed destination. Returning
     * the existing field adds no storage and avoids consulting a potentially
     * changed panel selection after the asynchronous load has completed.
     */
    return pm_kit_request_scene_mask;
}

void preset_ackStatus(void)
{
    pm_status = PRESET_IDLE;
    pm_completed_op = PRESET_OP_NONE;
    pm_completed_ok = 0u;
}

/* -----------------------------------------------------------------------
** Completion callbacks — fired by filesystem_tick() context
** ----------------------------------------------------------------------- */
static void preset_completeFilesystemOp(preset_op_type_t completed_op)
{
    fs_status_t fs_status = filesystem_status();
    uint8_t completed_ok = (uint8_t)(fs_status == FS_STATUS_DONE);
    uint8_t is_test_op = (uint8_t)(completed_op == PRESET_OP_TEST_SCAN ||
                                   completed_op == PRESET_OP_TEST_FILE_LOAD ||
                                   completed_op == PRESET_OP_TEST_DIR_LOAD ||
                                   completed_op == PRESET_OP_TEST_FILE_SAVE ||
                                   completed_op == PRESET_OP_TEST_DIR_SAVE);

    filesystem_ack();
    pm_completed_ok = completed_ok;
    if (completed_ok || is_test_op || completed_op != PRESET_OP_NONE) {
        /*
         * Preserve completion identity for operations that report failure.
         *
         * Older musical callers once collapsed failures to PRESET_OP_NONE,
         * which made the UI silently reset with no clue where filesystem.c had
         * stopped. Keep the requested op identity on error and let Menu decide
         * between success cleanup and an ERR overlay using pm_completed_ok and
         * filesystem_errorCode().
         */
        pm_completed_op = completed_op;
        pm_status = PRESET_UPDATE_READY;
    } else {
        pm_completed_op = PRESET_OP_NONE;
        pm_status = PRESET_UPDATE_READY;
    }
}

static void on_kit_load_complete(void)
{
    uint8_t scene_index;

    /*
     * Publish the complete retained Kit image only after the normal Kit request
     * reached its terminal successful filesystem callback.
     *
     * Inputs: the immutable pm_kit_request_scene_mask captured when
     * preset_loadKitForScenes() was accepted, and FS_STATUS_DONE before
     * preset_completeFilesystemOp() acknowledges the facade. Outputs: every
     * selected now-present Scene receives the existing whole-Kit dirty scope,
     * comprising its two live Kit settings and all six complete Instrument
     * regions. Why: filesystem_loadKitDirectory_tick() assigns kit_t directly
     * after validation, bypassing scalar SceneData/Preset setters. The marker
     * records the already-retained bytes only; it performs no I/O and does not
     * change HCNAMES, Menu state, or runtime application. Affiliates:
     * preset_markRequestedScenesPresentOnSuccessfulLoad(),
     * autosave_markKitDirty(), and preset_completeFilesystemOp().
     */
    preset_markRequestedScenesPresentOnSuccessfulLoad();
    if (filesystem_status() == FS_STATUS_DONE) {
        for (scene_index = 0u;
             scene_index < SCENE_COUNT && scene_index < 16u;
             scene_index++) {
            if ((pm_kit_request_scene_mask &
                 (uint16_t)(1u << scene_index)) != 0u) {
                autosave_markKitDirty(scene_index);
            }
        }
    }
    preset_completeFilesystemOp(PRESET_OP_KIT_LOAD);
}

static void on_kit_save_complete(void)
{
    /*
     * Report one completed normal Kit directory save.
     *
     * filesystem.c has already handled slot cleanup, directory creation,
     * member-file streaming, kitset emission, and final flush/error reporting.
     * Preset keeps this callback intentionally small: Menu only needs the
     * operation identity so it can clear Save busy state and repaint at the
     * top-row type selector.
     */
    preset_completeFilesystemOp(PRESET_OP_KIT_SAVE);
}

static void on_kit_morph_save_complete(void)
{
    /*
     * Report one completed KitMrp directory save.
     *
     * KitMrp uses the same directory writer as normal Kit Save, but filesystem
     * serializes the Morph Save projection: current interpolated values are
     * written into both normal and morph endpoint sections. Resident Scene kit
     * names and instrument stems are not changed by this export.
     */
    preset_completeFilesystemOp(PRESET_OP_KIT_MORPH_SAVE);
}

static void on_kit_morph_load_complete(void)
{
    /*
     * Complete a staged new-format Kit Morph load.
     *
     * Filesystem has parsed the selected Kit/ directory into its staging
     * buffer but has not committed it to SceneData. Naming this as KitMrp lets
     * Menu/Preset run the morph-endpoint copy path instead of the normal Kit
     * replacement/apply path.
     */
    preset_completeFilesystemOp(PRESET_OP_KIT_MORPH_LOAD);
}

static void on_morph_load_complete(void)
{
    preset_completeFilesystemOp(PRESET_OP_MORPH_LOAD);
}

static void on_globals_load_complete(void)
{
    /*
     * Apply the loaded AutoSave preference at the completed file boundary.
     *
     * Inputs: terminal keyed-settings status and normalized flat policy byte.
     * Output: successful boot or manual Settings Load updates only in-memory
     * autosave authorization; any runtime setup remains asynchronous until the
     * facade is acknowledged/idle. Errors preserve the current policy. Why:
     * menu_sendAllGlobals() intentionally has no file-lifecycle side effects.
     * Affiliates: main.c's early boot load, Menu Global apply, and
     * filesystem_setAutosaveEnabled().
     */
    if (filesystem_status() == FS_STATUS_DONE) {
        filesystem_setAutosaveEnabled(
            parameter_values[PAR_AUTOSAVE_ENABLED]);
    }
    preset_completeFilesystemOp(PRESET_OP_GLOBALS_LOAD);
}

static void on_pattern_load_complete(void)
{
    preset_markRequestedScenesPresentOnSuccessfulLoad();
    preset_completeFilesystemOp(PRESET_OP_PATTERN_LOAD);
}

static void on_all_load_complete(void)
{
    preset_completeFilesystemOp(PRESET_OP_ALL_LOAD);
}

static void on_performance_load_complete(void)
{
    preset_completeFilesystemOp(PRESET_OP_PERFORMANCE_LOAD);
}

static void on_name_load_complete(void)
{
    preset_completeFilesystemOp(PRESET_OP_NAME_LOAD);
}

static void on_instrument_load_complete(void)
{
    preset_markRequestedScenesPresentOnSuccessfulLoad();
    preset_completeFilesystemOp(PRESET_OP_INSTRUMENT_LOAD);
}

static void on_instrument_save_complete(void)
{
    /*
     * Complete one root Instrument Save.
     *
     * Filesystem has already written Instrument/<name.ext>, closed it, and
     * updated its Instrument browser cache. Preset reports a save completion
     * rather than an apply completion because no SceneData or DSP runtime state
     * changes when exporting a resident instrument.
     */
    preset_completeFilesystemOp(PRESET_OP_INSTRUMENT_SAVE);
}

static void on_instrument_temp_save_complete(void)
{
    /*
     * Report completion of the hidden normal Instrument Load source write.
     *
     * Input: filesystem has closed `.hctmp.<ext>` in one type directory.
     * Output: Menu may now load that type's `.hcindex` and permit the `kit`
     * row. No Scene/DSP/HCNAMES work occurs here. Affiliates: Menu entry
     * sequencing and filesystem_requestSaveInstrumentTemp().
     */
    preset_completeFilesystemOp(PRESET_OP_INSTRUMENT_TEMP_SAVE);
}

static void on_instrument_morph_temp_save_complete(void)
{
    /* Report completion of the hidden Morph-only InstrumentMrp baseline. */
    preset_completeFilesystemOp(PRESET_OP_INSTRUMENT_MORPH_TEMP_SAVE);
}

static void on_instrument_morph_save_complete(void)
{
    /*
     * Complete one root Instrument Morph Save.
     *
     * Filesystem has written Instrument/<name.ext> with the Morph Save
     * projection, where [params] and [morph] both hold the current
     * interpolated endpoint. Preset reports this as a separate completion so
     * Menu can reset nested Save UI without treating it as a normal Instrument
     * export or a legacy flat Morph file.
     */
    preset_completeFilesystemOp(PRESET_OP_INSTRUMENT_MORPH_SAVE);
}

static void on_instrument_morph_load_complete(void)
{
    /*
     * Complete a staged Instrument Morph load.
     *
     * The Instrument/ file was parsed by the normal single-instrument loader.
     * Preset will copy only same-type morphable normal endpoint values into the
     * resident destination slot, preserving slot identity and modulation
     * bindings.
     */
    preset_completeFilesystemOp(PRESET_OP_INSTRUMENT_MORPH_LOAD);
}

static void on_instrument_morph_temp_load_complete(void)
{
    /* The staged hidden file is committed through the Morph-only path. */
    preset_completeFilesystemOp(PRESET_OP_INSTRUMENT_MORPH_TEMP_LOAD);
}

static void on_scene_load_complete(void)
{
    uint8_t scene_index;

    /*
     * This is a RAM-only completion witness, emitted before any presence
     * promotion or mutation marking. Its input is the filesystem terminal
     * status still associated with this callback and the immutable destination
     * mask captured by preset_loadSceneForScenes(). Bit 0 proves that the
     * callback observed FS_STATUS_DONE; value32 preserves the exact Scene
     * destinations the callback will iterate. Thus a missing R record proves
     * the callback was not reached, R with bit 0 clear proves a terminal error
     * entered it, and R with value zero proves the request arrived without a
     * destination. It performs no filesystem I/O and adds no retained state.
     */
    autosaveTrace_record(AUTOSAVE_TRACE_STAGE_SCENE_LOAD_COMPLETE,
                         (uint8_t)(filesystem_status() == FS_STATUS_DONE
                                       ? AUTOSAVE_TRACE_SCENE_LOAD_COMPLETE_FLAG_STATUS_DONE
                                       : 0u),
                         (uint32_t)pm_kit_request_scene_mask);

    /*
     * Complete a durable root Scene load.
     *
     * Inputs: filesystem terminal status and the retained destination mask.
     * Output: successful destinations become Bank-present before Menu begins
     * runtime apply. The filesystem already stages HCNAMES provenance with the
     * paired committed identity; Preset neither mirrors it nor dirties
     * settings.cfg.
     *
     * Inputs: FS_STATUS_DONE and the immutable destination mask captured by
     * preset_loadSceneForScenes(). Outputs: every selected now-present Scene
     * gets the complete Scene scope—live Scene settings, the current Effect
     * scope, the complete Kit scope, and the separate Pattern AutoSave dirty
     * bit. Why:
     * filesystem_commitSceneStage() assigns settings and kit_t directly before
     * Pattern I/O; marking there could publish a load whose later Pattern,
     * Effect, or HCNAMES work failed. This completion is the first boundary at
     * which the complete Scene replacement, including Pattern, is known to
     * have succeeded. Affiliates:
     * filesystem_commitSceneStage(),
     * preset_markRequestedScenesPresentOnSuccessfulLoad(),
     * autosave_markSceneWithPatternDirty(), and
     * preset_completeFilesystemOp().
     */
    preset_markRequestedScenesPresentOnSuccessfulLoad();
    if (filesystem_status() == FS_STATUS_DONE) {
        for (scene_index = 0u;
             scene_index < SCENE_COUNT && scene_index < 16u;
             scene_index++) {
            if ((pm_kit_request_scene_mask &
                 (uint16_t)(1u << scene_index)) != 0u) {
                /* The loaded directory-backed Pattern must be captured by
                 * AutoSave, but its generation stays above the existing A/B
                 * pair so one drain cannot resurrect pre-load content. */
                filesystem_patternAutosaveOnLoad(scene_index);
                autosave_markSceneWithPatternDirty(scene_index);
            }
        }
    }
    preset_completeFilesystemOp(PRESET_OP_SCENE_LOAD);
}

static void on_scene_save_complete(void)
{
    /*
     * Complete one root Scene directory save.
     *
     * Filesystem has already serialized sceneset.scg, the embedded Kit
     * directory, named v4 Pattern child, and named `.fx` v2 Effect child.
     * Preset reports only the
     * operation identity so Menu can clear Save busy state without starting any
     * runtime sound-apply work.
     */
    preset_completeFilesystemOp(PRESET_OP_SCENE_SAVE);
}

static void on_bank_load_complete(void)
{
    uint16_t completed_scene_mask;
    uint8_t scene_index;

    /*
     * End the availability session before Menu sees the completion
     * (S082 P3 D44).
     *
     * What: availability returns to the present mask, which the filesystem
     * has just committed (success) or left as the children set it (failure).
     * Why: Menu's completion repaints PERF and finishes the command from the
     * final state; a stale unavailable bit would keep a Scene dark.
     * Inputs: none. Outputs: BankData session closed.
     * Affiliates: bank_endLoadAvailability(), filesystem metadata commit.
     */
    bank_endLoadAvailability();

    /*
     * Complete one root Bank load.
     *
     * Filesystem has validated bankset.bcg and either loaded a Bank-local
     * Scene child or reported a valid empty Bank through
     * filesystem_lastBankLoadLoadedScene(). Menu/boot decide whether to run
     * the fallback chain after reading that bit.
     *
     * Inputs: FS_STATUS_DONE and filesystem_lastBankLoadSceneMask() before the
     * completion helper acknowledges operation scratch. Output: each set child
     * bit receives the complete Scene dirty scope, including Pattern; a valid
     * empty Bank yields no Scene marks. Why: the original request may name absent
     * children, while bank_scenePresentMask() also contains retained unselected
     * Scenes; only the completed effective-child mask identifies this Bank
     * Load's payload. BankData has already marked changed Bank fields through
     * its own setters. Affiliates: filesystem_lastBankLoadSceneMask(), Bank
     * phase-20 metadata commit, autosave_markSceneWithPatternDirty(), and
     * preset_completeFilesystemOp().
     */
    if (filesystem_status() == FS_STATUS_DONE) {
        completed_scene_mask = filesystem_lastBankLoadSceneMask();
        pm_bank_load_failed_scene_mask = filesystem_lastBankLoadFailedSceneMask();
        for (scene_index = 0u;
             scene_index < SCENE_COUNT && scene_index < 16u;
             scene_index++) {
            if ((completed_scene_mask & (uint16_t)(1u << scene_index)) != 0u) {
                /* The loaded Bank child owns the new resident Pattern. Mark
                 * its card-clean authority invalid while preserving the
                 * monotonic generation seeded from any existing A/B pair. */
                filesystem_patternAutosaveOnLoad(scene_index);
                autosave_markSceneWithPatternDirty(scene_index);
            }
        }
        /*
         * Option 2: publish card-clean authority for the completed effective
         * child mask. The Bank Load slot is bank_restoreBankSlot(), set by the
         * Bank phase-20 metadata commit. Publishing here (not during the commit)
         * keeps the clean bit separate from the Autosave whole-Scene marker that
         * the loop above also issues: the marker tracks persistence, while the
         * clean bit tracks equality to the on-card child. Same-slot loads OR
         * into existing bits; a different slot replaces the old authority.
         */
        bank_publishSdCleanAuthority(bank_restoreBankSlot(),
                                     completed_scene_mask);
    }
    /*
     * Record the background load's result for its origin (S082 P1 D34).
     *
     * What: when this completion belongs to a background request (phase 5),
     * DONE or FAILED from the terminal filesystem status, with the MIDI
     * flag when the request had MIDI origin. Child failures inside a DONE
     * load still show the normal overlay and count as DONE. The boot load
     * (no background phase) records nothing.
     * Inputs: pm_bg_phase, filesystem_status(). Outputs: pm_bg_result.
     * Affiliates: preset_takeBackgroundBankLoadResult().
     */
    if ((pm_bg_phase & PM_BG_PHASE_MASK) == 5u)
        pm_bg_result = (uint8_t)(
            ((filesystem_status() == FS_STATUS_DONE)
                 ? PRESET_BG_RESULT_DONE : PRESET_BG_RESULT_FAILED) |
            ((pm_bg_phase & PM_BG_ORIGIN_MIDI) ? PRESET_BG_RESULT_MIDI : 0u));
    preset_completeFilesystemOp(PRESET_OP_BANK_LOAD);
}

static void on_bank_save_complete(void)
{
    /*
     * Complete one root Bank save.
     *
     * The save writes bankset.bcg plus the selected Bank-local Scene children.
     * It does not imply runtime DSP changes, so Menu can share the ordinary
     * Save completion cleanup used by Scene Save.
     */
    preset_completeFilesystemOp(PRESET_OP_BANK_SAVE);
}

static void on_globals_save_complete(void)
{
    preset_completeFilesystemOp(PRESET_OP_GLOBALS_SAVE);
}

static void on_pattern_save_complete(void)
{
    preset_completeFilesystemOp(PRESET_OP_PATTERN_SAVE);
}

static void on_all_save_complete(void)
{
    preset_completeFilesystemOp(PRESET_OP_ALL_SAVE);
}

static void on_performance_save_complete(void)
{
    preset_completeFilesystemOp(PRESET_OP_PERFORMANCE_SAVE);
}

/*
 * Retired generic File/Dir diagnostics.
 *
 * Their menu types and filesystem caches were removed to keep SRAM within the
 * musical-name contract. Keep the old completion adapters out of the build so
 * no Preset path can issue a request for the retired diagnostic API.
 */
#if 0
static void on_test_scan_complete(void)
{
    /*
     * Complete a generic File/Dir test scan.
     *
     * No SceneData or DSP state is touched; Menu will read the filesystem
     * scan cache directly and repaint the temporary browser list.
     */
    preset_completeFilesystemOp(PRESET_OP_TEST_SCAN);
}

static void on_test_file_load_complete(void)
{
    preset_completeFilesystemOp(PRESET_OP_TEST_FILE_LOAD);
}

static void on_test_dir_load_complete(void)
{
    preset_completeFilesystemOp(PRESET_OP_TEST_DIR_LOAD);
}

static void on_test_file_save_complete(void)
{
    preset_completeFilesystemOp(PRESET_OP_TEST_FILE_SAVE);
}

static void on_test_dir_save_complete(void)
{
    preset_completeFilesystemOp(PRESET_OP_TEST_DIR_SAVE);
}

#endif

static fs_file_type_t preset_fileTypeFromSaveType(uint8_t what, uint8_t *hasName)
{
    if (hasName) *hasName = 1;

    switch (what) {
    case SAVE_TYPE_KIT:         return FS_FILE_KIT;
    case SAVE_TYPE_KIT_MORPH:   return FS_FILE_KIT;
    case SAVE_TYPE_SCENE:       return FS_FILE_SCENE;
    case SAVE_TYPE_BANK:        return FS_FILE_BANK;
    case SAVE_TYPE_PATTERN:     return FS_FILE_PATTERN;
    case SAVE_TYPE_GLO:
        if (hasName) *hasName = 0;
        return FS_FILE_SETTINGS;
    case SAVE_TYPE_SAMPLES:
    default:
        if (hasName) *hasName = 0;
        return FS_FILE_SAMPLES;
    }
}

/* Ensure the Scene Morph worker exists before any compatibility wrapper uses it.
 *
 * Why this helper exists: preset_init() is present but the current boot path
 * does not call it. Lazy initialization keeps the new Morph worker valid for
 * boot kit load, menu Morph edits, MIDI Morph edits, and tests without making
 * this bug fix depend on a separate main.c initialization reorder.
 *
 * Inputs: none. Output: presetMorph_init() has run exactly once. Clients are
 * preset_init(), preset_morph(), and preset_startDrumsetApply().
 */
static void preset_ensureMorphInitialized(void)
{
    if (preset_morph_initialized)
        return;
    presetMorph_init();
    preset_morph_initialized = 1u;
}

static void preset_syncSceneMorphMirrors(const scene_t *scene)
{
    uint8_t slot;

    /*
     * Mirror retained Scene Morph settings into the flat Menu buffer.
     *
     * Inputs: Scene settings record. Outputs: parameter_values[] entries used
     * by PERF `mrp` and `1vm..6vm` are updated for display/edit baselines.
     * This helper is intentionally Preset-local because Preset is the boundary
     * between SceneData's retained settings and legacy flat menu mirrors; Menu,
     * MIDI, and future Scene-file load should not each duplicate the mirror
     * rules.
     */
    if (!scene)
        return;
    parameter_values[PAR_MORPH] = scene->settings.morph_amount;
    for (slot = 0u; slot < INSTRUMENT_SLOT_COUNT; slot++)
        parameter_values[PAR_VOICE1_MORPH + slot] =
            scene->settings.voice_morph_amount[slot];
}

/* -----------------------------------------------------------------------
** preset_init — reset Preset async state and initialize Scene-owned morph
** application helpers. Filesystem mount is still handled by asyncfatfs.
** ----------------------------------------------------------------------- */
void preset_init(void)
{
    pm_status = PRESET_IDLE;
    pm_completed_op = PRESET_OP_NONE;
    pm_request_slot = 0;
    pm_request_type = SAVE_TYPE_KIT;
    preset_clearAllAudioOutStepOverrides(0u);
    preset_clearAllFxSendStepOverrides();
    preset_ensureMorphInitialized();
}

static uint8_t preset_applyInstrumentRuntimeValueInternal(uint8_t scene_index,
                                                          instrument_param_id_t id,
                                                          instrument_param_value_t value,
                                                          uint8_t recordAutomation,
                                                          uint8_t force)
{
    const kit_instrument_slot_t *instrument;
    const ParamDescriptor *descriptor;
    uint8_t slot;
    uint8_t index;

    (void)recordAutomation;

    if (!instrumentParam_isVoiceParameter(id))
        return 0u;
    slot = instrumentParam_slot(id);
    index = instrumentParam_local(id);
    instrument = scene_instrumentSlotConst(scene_index, slot);
    if (!instrument)
        return 0u;
    descriptor = instrumentManager_descriptor(instrument->type, index);
    if (!descriptor)
        return 0u;

    /*
     * Runtime apply is now descriptor-owned. The generic storage cell is
     * identified by descriptor index; the descriptor supplies the instrument
     * instance offset and parameter type. ParameterArray/PAR_* is no longer the
     * instrument meaning layer.
     */
    if (!force && scene_index != scene_getActiveIndex())
        return 1u;
    return instrumentManager_writeRuntime(slot, descriptor, value);
}

uint8_t preset_applyInstrumentRuntimeValue(uint8_t scene_index,
                                           instrument_param_id_t id,
                                           instrument_param_value_t value)
{
    /*
     * Public runtime apply for one canonical instrument parameter.
     *
     * Inputs: Scene index, slot/descriptor-index instrument ID, and descriptor
     * image value. Output: active Scene values are written through
     * instrument-owned runtime bindings; inactive Scene calls validate but do
     * not mutate live DSP state. Accessors and affiliates: SceneData supplies
     * slot/type lookup, InstrumentManager supplies descriptor lookup and runtime
     * instance binding, and presetMorphEngine calls this after building
     * morph_interpolation[].
     *
     * The public value is byte-sized because SceneData, files, Menu, and Morph
     * all retain descriptor values in byte space. Target selector bytes are
     * expanded by InstrumentManager only while applying runtime modulation
     * destinations.
     */
    return preset_applyInstrumentRuntimeValueInternal(scene_index, id, value, 0u, 0u);
}

uint8_t preset_applyInstrumentRuntimeValueForced(uint8_t scene_index,
                                                 instrument_param_id_t id,
                                                 instrument_param_value_t value)
{
    /*
     * Runtime apply that ignores the active-Scene write guard (S077 P2 §3.4).
     *
     * What: identical to preset_applyInstrumentRuntimeValue() but writes the
     * live runtime even when scene_index is not the active Scene. Why: the
     * per-track single-voice apply loads a slot from its played Scene, which may
     * differ from the global active Scene, so the descriptor image for that
     * Scene must still reach the voice runtime. Inputs: Scene index, slot/
     * descriptor-index instrument ID, and descriptor image value. Output: the
     * runtime value is written. Client: presetMorph_writeRuntimeBaseEx() when
     * force is set (per-track apply). Affiliate: the guarded public wrapper.
     */
    return preset_applyInstrumentRuntimeValueInternal(scene_index, id, value, 0u, 1u);
}

/*
 * Commit one validated Instrument endpoint cell and mark its exact wire cell.
 *
 * Inputs: destination Scene/slot, active-type descriptor index, endpoint
 * selection, and value after the public caller validates descriptor policy.
 * Output: only a changed retained byte is stored, then the matching normal or
 * Morph Autosave marker is called; equal values do nothing. Why: generic
 * descriptor setters cover every current/future instrument type without per-
 * type dirty switches. Affiliates: InstrumentManager registry validation,
 * preset_setInstrumentParameter(), supplemental normalization, and Autosave's
 * descriptor-indexed live getter. Derived interpolation is never marked.
 */
static void preset_storeInstrumentEndpoint(uint8_t scene_index,
                                           uint8_t slot,
                                           uint8_t descriptor_index,
                                           instrument_image_select_t image,
                                           instrument_param_value_t value)
{
    kit_instrument_slot_t *instrument =
        scene_instrumentSlot(scene_index, slot);
    instrument_param_value_t *storage;

    if (!instrument || descriptor_index >= INSTRUMENT_PARAM_COUNT)
        return;
    if (image == INSTRUMENT_IMAGE_MORPH) {
        storage = &instrument->parameter_images
            .morph_instrument_parameters[descriptor_index];
    } else {
        storage = &instrument->parameter_images
            .instrument_parameters[descriptor_index];
    }
    if (*storage == value)
        return;
    *storage = value;
    /*
     * Option 2: a committed Instrument normal/Morph byte invalidates the owning
     * Scene's card-clean bit. This is the Instrument-side complement of
     * SceneData's scalar funnels; an equal-value no-op was already rejected, so
     * this line runs only for real retained-data change. It stays separate from
     * the Autosave normal/Morph marker for the same source/authority reason.
     * Affiliates: bank_invalidateSdCleanScene(), filesystem Bank Save skip.
     */
    bank_invalidateSdCleanScene(scene_index);
    if (image == INSTRUMENT_IMAGE_MORPH) {
        autosave_markInstrumentMorphParameterDirty(
            scene_index, slot, descriptor_index);
    } else {
        autosave_markInstrumentNormalParameterDirty(
            scene_index, slot, descriptor_index);
    }
}

uint8_t preset_setInstrumentParameter(uint8_t scene_index, uint8_t slot,
                                      uint8_t descriptor_index,
                                      instrument_image_select_t image,
                                      uint8_t value)
{
    kit_instrument_slot_t *instrument = scene_instrumentSlot(scene_index, slot);
    const ParamDescriptor *descriptor;
    if (!instrument)
        return 0u;

    descriptor = instrumentManager_descriptor(instrument->type, descriptor_index);
    if (!descriptor ||
        !(descriptor->flags & INSTRUMENT_PARAM_FLAG_MORPHABLE)) {
        return 0u;
    }

    /*
     * Typed endpoint setter.
     *
     * Why this exists: Menu, MIDI translation, tests, and future instrument
     * import should not write the Scene arrays directly. The setter enforces
     * descriptor ownership/range, chooses the persisted endpoint, commits only
     * a changed byte through the generic endpoint/Autosave boundary, and
     * re-interpolates only the edited parameter so the runtime image and DSP
     * backend follow the Scene state. A parameter held by step automation keeps
     * its runtime value until the next trigger. Affiliate code: presetMorphEngine owns
     * morph_interpolation[], while preset_applyInstrumentRuntimeValueInternal()
     * owns the temporary legacy DSP mirror.
     */
    preset_storeInstrumentEndpoint(
        scene_index, slot, descriptor_index, image, value);

    if (scene_index == scene_getActiveIndex()) {
        scene_t *scene = scene_get(scene_index);
        preset_ensureMorphInitialized();
        /*
         * Apply the edited endpoint's interpolation now (S075 F3).
         *
         * Inputs: the endpoint stored above. Output: morph_interpolation[] and,
         * unless automation holds the parameter, the runtime value of this one
         * parameter at the voice's resolved Morph amount. The former raw write
         * ignored the automation overlay and a `Nvm`/LFO Morph amount. The whole
         * voice is no longer queued, so no other parameter's interpolation
         * changes and automated parameters are not rewritten.
         * Affiliates: presetMorph_applyParameterNow(),
         * seq_restoreAutomatedParameters().
         */
        if (scene)
            presetMorph_applyParameterNow(scene_index, slot,
                                          descriptor_index);
    }
    return 1u;
}

static uint8_t preset_setSupplementalParameterInternal(
                                        uint8_t scene_index, uint8_t slot,
                                        uint8_t descriptor_index,
                                        instrument_param_value_t value,
                                        uint8_t force)
{
    kit_instrument_slot_t *instrument = scene_instrumentSlot(scene_index, slot);
    const ParamDescriptor *descriptor;
    if (!instrument)
        return 0u;

    descriptor = instrumentManager_descriptor(instrument->type, descriptor_index);
    if (!descriptor)
        return 0u;

    if (descriptor->runtime.kind == INSTRUMENT_BIND_INSTANCE_OFFSET ||
        (descriptor->flags & INSTRUMENT_PARAM_FLAG_MORPHABLE)) {
        return 0u;
    }

    /*
     * Supplemental target cells are retained as byte tokens.
     *
     * Velocity target rows store a self-scoped byte token, and LFO target rows
     * store a local token interpreted through their paired target-voice row.
     * Preset retains the byte value unchanged; InstrumentManager expands it to
     * a canonical target ID only while applying the active Scene's runtime
     * modulation graph. A changed normal token is stored and exactly marked by
     * the generic endpoint helper before runtime apply; equal input creates no
     * dirty work.
     */
    preset_storeInstrumentEndpoint(
        scene_index, slot, descriptor_index, INSTRUMENT_IMAGE_MAIN, value);
    /* force > 0 lets the per-track single-voice apply install this source's
     * target tokens into the runtime even when the Scene is not active
     * (S077 P2 §3.4, Q13). */
    if (force || scene_index == scene_getActiveIndex())
        return instrumentManager_writeRuntime(slot, descriptor, value);
    return 1u;
}

uint8_t preset_setSupplementalParameter(uint8_t scene_index, uint8_t slot,
                                        uint8_t descriptor_index,
                                        instrument_param_value_t value)
{
    return preset_setSupplementalParameterInternal(
        scene_index, slot, descriptor_index, value, 0u);
}

/*
 * Resolve one slot's played Scene for per-track playback (S077 P2 §3.5).
 *
 * What: returns the resident Scene playback reads for the slot's track, from
 * seq_perTrackPattern[slot]. Why: the mixer resolves FX send, fader mode, and
 * per-voice Morph amount through a Scene index every block; for a per-track
 * override that index must be the track's played Scene, not the global active
 * Scene. Inputs: slot 0..5 (tracks 5+6 share slot 5). Output: resident Scene
 * index; seq_activePattern when no override is set. Context: foreground and
 * mixer (audio) context; a single-byte read of SRAM1 written atomically by the
 * button handler. Affiliates: seq_getTrackPlayedScene(),
 * preset_getEffectiveFxSendAmount(), mixer_faderGains().
 */
uint8_t preset_getSlotPlayedScene(uint8_t slot)
{
    if (slot >= INSTRUMENT_SLOT_COUNT)
        return scene_getActiveIndex();
    return seq_getTrackPlayedScene(slot);
}

uint8_t preset_applyKitAudioRouting(uint8_t scene_index, uint8_t slot)
{
    const scene_t *scene = scene_getConst(scene_index);
    uint8_t route;
    if (!scene || slot >= INSTRUMENT_SLOT_COUNT)
        return 0u;

    /*
     * Apply one Scene-owned per-voice routing byte.
     *
     * Input is a Scene/slot pair; output is the active mixer routing and
     * parameter_values[] compatibility byte. The mixer array remains the DSP
     * affiliate until routing is absorbed into a typed Scene apply backend.
     * Clients are the bounded loaded-kit/Scene apply cursor and VOICE mix
     * Scene-setting UI edits. Routing moved out of kit_settings_t so root Kit
     * swaps do not overwrite a Scene's output assignment.
     */
    route = scene_getVoiceAudioOut(scene_index, slot);
    if (route > MIXER_ROUTING_DAC2_R)
        route = MIXER_ROUTING_DAC1_STEREO;
    if (scene_index == scene_getActiveIndex()) {
        mixer_audioRouting[slot] = route;
    }
    return 1u;
}

void preset_applyVoiceAudioOutRuntime(uint8_t slot, uint8_t route)
{
    /*
     * Apply one voice's output route as a transient runtime overlay.
     *
     * Inputs: zero-based instrument slot and route in the mixer enum domain.
     * Output: active mixer routing changes without a SceneData write, an
     * AutoSave dirty mark, or Bank-clean invalidation. Client: Scene-target
     * step automation. Restore affiliate: preset_applyKitAudioRouting()
     * reads the retained Scene route at the transport boundary.
     */
    if (slot >= INSTRUMENT_SLOT_COUNT)
        return;
    if (route > MIXER_ROUTING_DAC2_R)
        route = MIXER_ROUTING_DAC1_STEREO;
    mixer_audioRouting[slot] = route;
}

void preset_setAudioOutStepOverride(uint8_t slot, uint8_t route)
{
    /*
     * Store one transient audio-out route for effective-value display.
     *
     * Inputs: zero-based instrument slot and mixer route. Output: the route is
     * retained in the runtime overlay without touching SceneData or AutoSave;
     * preset_applyVoiceAudioOutRuntime() performs the independent mixer write.
     * Client: sequencer Scene-target automation. Restore: the clear helper.
     */
    if (slot >= INSTRUMENT_SLOT_COUNT)
        return;
    if (route > MIXER_ROUTING_DAC2_R)
        route = MIXER_ROUTING_DAC1_STEREO;
    audio_out_step_override[slot].active = 1u;
    audio_out_step_override[slot].route = route;
}

void preset_clearAllAudioOutStepOverrides(uint8_t scene_index)
{
    uint8_t slot;

    /*
     * Clear every transient audio-out route overlay.
     *
     * Input: active Scene index supplied by the transport restore owner; it is
     * intentionally unused because the retained route restore is performed by
     * preset_applyKitAudioRouting() per dirty target. Output: all effective
     * getters fall back to retained SceneData, with no persistence mutation.
     */
    (void)scene_index;
    for (slot = 0u; slot < INSTRUMENT_SLOT_COUNT; slot++) {
        audio_out_step_override[slot].active = 0u;
        audio_out_step_override[slot].route = 0u;
    }
}

uint8_t preset_getEffectiveAudioOut(uint8_t scene_index, uint8_t slot)
{
    /*
     * Read one voice's effective audio-out route.
     *
     * Inputs: resident Scene index and zero-based voice slot. Output: the
     * active step overlay route when present, otherwise the retained Scene
     * route. This read-only bridge feeds the live Scene superpage display.
     */
    if (slot >= INSTRUMENT_SLOT_COUNT)
        return 0u;
    if (audio_out_step_override[slot].active)
        return audio_out_step_override[slot].route;
    return scene_getVoiceAudioOut(scene_index, slot);
}

/*
 * Clear a single voice slot's audio-out step-automation override and
 * re-apply the Scene-stored route (S081 §5.2).
 *
 * What: clears the per-slot audio-out override and calls
 * preset_applyVoiceAudioOutRuntime(slot, scene_getVoiceAudioOut(A, slot)) to
 * restore the Scene base route. Why: unlike FX send, clearing the audio-out
 * override alone would leave the runtime on the last automation value until
 * the next trigger. The explicit re-apply makes the menu edit audible
 * immediately. Inputs: slot 0..5. Outputs: audio-out override cleared,
 * runtime route restored. Affiliates: menu_cellCommitValue SCENE_SETTING
 * AUDIO_OUT branch, preset_setAudioOutStepOverride,
 * preset_applyVoiceAudioOutRuntime, scene_getVoiceAudioOut.
 */
void preset_clearAudioOutStepOverride(uint8_t slot)
{
    if (slot >= INSTRUMENT_SLOT_COUNT)
        return;
    audio_out_step_override[slot].active = 0u;
    audio_out_step_override[slot].route = 0u;
    preset_applyVoiceAudioOutRuntime(slot,
        scene_getVoiceAudioOut(scene_getActiveIndex(), slot));
}

void preset_setFxSendStepOverride(uint8_t slot, uint8_t amount)
{
	/*
	 * Store one transient FX-send amount for the live mixer overlay.
	 *
	 * Inputs: zero-based voice slot and 0..127 amount. Output: runtime-only
	 * overlay state read by preset_getEffectiveFxSendAmount(), then pulled by
	 * mixer_calcNextSampleBlock() on the next render block; retained
	 * SceneData/AutoSave remain untouched. Client: sequencer Scene-target
	 * automation. Restore: the clear helper.
     */
    if (slot >= INSTRUMENT_SLOT_COUNT)
        return;
    if (amount > 127u)
        amount = 127u;
    fx_send_step_override[slot].active = 1u;
    fx_send_step_override[slot].amount = amount;
}

void preset_clearAllFxSendStepOverrides(void)
{
    uint8_t slot;

	/*
	 * Clear every transient FX-send display overlay.
	 *
	 * Inputs: none. Output: effective-value reads fall back to retained Scene
	 * settings and the mixer send ramp converges to the retained value on its
	 * next block. This helper owns the runtime lifetime of the sequencer overlay
	 * while the mixer owns the audible send application.
     */
    for (slot = 0u; slot < INSTRUMENT_SLOT_COUNT; slot++) {
        fx_send_step_override[slot].active = 0u;
        fx_send_step_override[slot].amount = 0u;
    }
}

uint8_t preset_getEffectiveFxSendAmount(uint8_t scene_index, uint8_t slot)
{
    uint8_t normal;
    uint8_t morph;
    uint8_t amount;

    /*
     * Read one voice's effective (audible) FX-send amount.
     *
     * Inputs: resident Scene index and zero-based voice slot. Output: the
     * active step overlay while present; otherwise the Normal/Morph endpoints
     * interpolated by the voice's resolved Morph amount (S075 F2-H):
     * normal + (morph - normal) * amount / 255, rounded. Equal endpoints
     * return without resolving Morph. Caller: mixer, every block. Menu uses
     * preset_getFxSendDisplayAmount() for endpoint display.
     */
    if (slot >= INSTRUMENT_SLOT_COUNT)
        return 0u;
    if (fx_send_step_override[slot].active)
        return fx_send_step_override[slot].amount;
    normal = scene_getVoiceFxSendAmount(scene_index, slot);
    morph = scene_getVoiceFxSendMorph(scene_index, slot);
    if (normal == morph)
        return normal;
    amount = presetMorph_getResolvedVoiceAmount(scene_index, slot);
    /* 127 * 255 + 127 fits uint16_t. */
    return (uint8_t)(((uint16_t)normal * (uint16_t)(255u - amount) +
                      (uint16_t)morph * amount + 127u) / 255u);
}

/*
 * Clear a single voice slot's FX-send step-automation override (S081 §5.2).
 *
 * What: clears the per-slot FX-send override so the value returns to the
 * Scene-stored amount. Why: R12/§5.2 says a menu edit of FX send clears that
 * slot's override. Only bulk-clear versions existed before; live record and
 * live erase need per-slot granularity. Inputs: slot 0..5. Outputs: the
 * slot's FX-send override is removed; the runtime reverts to the Scene base.
 * Affiliates: menu_cellCommitValue SCENE_SETTING FX_SEND branch,
 * preset_setFxSendStepOverride (set site), seq_applySceneAutomation FX_SEND
 * branch.
 */
void preset_clearFxSendStepOverride(uint8_t slot)
{
    if (slot >= INSTRUMENT_SLOT_COUNT)
        return;
    fx_send_step_override[slot].active = 0u;
    fx_send_step_override[slot].amount = 0u;
}

uint8_t preset_getFxSendDisplayAmount(uint8_t scene_index, uint8_t slot)
{
    /*
     * Value shown by the VOICE mix FX-send cell in Normal view (S075 F2-H).
     *
     * Output: active step override while one plays, otherwise the retained
     * Normal endpoint. Never the interpolated send; Morph view reads the
     * retained Morph endpoint directly, like instrument endpoint images.
     * Caller: menu_cellDisplayValue().
     */
    if (slot >= INSTRUMENT_SLOT_COUNT)
        return 0u;
    if (fx_send_step_override[slot].active)
        return fx_send_step_override[slot].amount;
    return scene_getVoiceFxSendAmount(scene_index, slot);
}

uint8_t preset_setVoiceAudioOut(uint8_t scene_index, uint8_t slot,
                                uint8_t route)
{
    /*
     * Retain and apply one Scene per-voice output route.
     *
     * Inputs: resident Scene index, zero-based instrument slot, and raw route
     * from menu/storage. Output: SceneData stores the route and active Scene
     * runtime routing is refreshed immediately. Clamping here uses mixer.h's
     * route enum, keeping SceneData independent of DSP headers.
     */
    if (!scene_get(scene_index) || slot >= INSTRUMENT_SLOT_COUNT)
        return 0u;
    if (route > MIXER_ROUTING_DAC2_R)
        route = MIXER_ROUTING_DAC1_STEREO;
    scene_setVoiceAudioOut(scene_index, slot, route);
    /*
     * S076 Rule A: a non-automation retained write supersedes any active
     * step override for this slot. preset_applyKitAudioRouting() (called
     * next) writes the retained route to the DSP mixer, and once the
     * override is inactive the effective getter returns the retained route
     * for display.
     * Input: active Scene guard. Output: audio_out_step_override[slot].active = 0.
     * Callers (upstream): menu_cellCommitValue() Scene-setting branch.
     * Affiliate: preset_setAudioOutStepOverride() (the automation setter),
     * preset_getEffectiveAudioOut() (the display bridge),
     * preset_applyVoiceAudioOutRuntime() (the DSP write).
     */
    if (scene_index == scene_getActiveIndex())
        audio_out_step_override[slot].active = 0u;
    return preset_applyKitAudioRouting(scene_index, slot);
}

uint8_t preset_setVoiceFxSendAmount(uint8_t scene_index, uint8_t slot,
                                    uint8_t amount)
{
	/*
	 * Retain one Scene FX-send amount for the block-rate mixer consumer.
	 *
	 * Inputs: resident Scene index, zero-based instrument slot, and 0..127
	 * amount. Output: SceneData retains the value; the active mixer pulls the
	 * effective amount on its next block, including any step overlay. Menu and
	 * storage callers therefore keep one owner boundary without a second runtime
	 * copy.
     */
    if (!scene_get(scene_index) || slot >= INSTRUMENT_SLOT_COUNT)
        return 0u;
    if (amount > 127u)
        amount = 127u;
    scene_setVoiceFxSendAmount(scene_index, slot, amount);
    /*
     * S076 Rule A: a non-automation retained write supersedes any active
     * step override for this slot. The mixer reads the effective getter
     * each block; once the override is inactive the retained value takes
     * over on the next block. No rebuild is needed because FX send has no
     * interpolation worker — the mixer reads the value directly.
     * Input: active Scene guard. Output: fx_send_step_override[slot].active = 0.
     * Callers (upstream): menu_cellCommitValue() Scene-setting branch,
     * clearOps.c `clear send`. Affiliate: preset_setFxSendStepOverride()
     * (the automation setter), preset_getEffectiveFxSendAmount() (the
     * mixer consumer), preset_getFxSendDisplayAmount() (the display
     * bridge).
     */
    if (scene_index == scene_getActiveIndex())
        fx_send_step_override[slot].active = 0u;
    return 1u;
}

uint8_t preset_setVoiceFxSendMorph(uint8_t scene_index, uint8_t slot,
                                   uint8_t amount)
{
    /*
     * Retain one Scene FX-send Morph endpoint (S075 F2-H).
     *
     * Inputs: resident Scene index, zero-based instrument slot, and 0..127
     * amount. Output: SceneData retains the value and marks its AutoSave cell;
     * the mixer reads it on the next block. Client: Menu Morph view and
     * clearOps.c `clear send`.
     */
    if (!scene_get(scene_index) || slot >= INSTRUMENT_SLOT_COUNT)
        return 0u;
    if (amount > 127u)
        amount = 127u;
    scene_setVoiceFxSendMorph(scene_index, slot, amount);
    /*
     * S076 Rule A: editing either endpoint of a morphable FX send releases
     * the step override so the interpolated result from retained Normal
     * and Morph endpoints takes effect. The mixer consumer
     * (preset_getEffectiveFxSendAmount) interpolates using the resolved
     * voice Morph amount when no step override is active.
     * Input: active Scene guard. Output: fx_send_step_override[slot].active = 0.
     * Callers (upstream): menu_cellCommitValue() Scene-setting Morph
     * branch, clearOps.c `clear send`. Affiliate:
     * preset_setVoiceFxSendAmount() (the Normal-endpoint twin, also
     * clears — Change 3).
     */
    if (scene_index == scene_getActiveIndex())
        fx_send_step_override[slot].active = 0u;
    return 1u;
}

uint8_t preset_setVoiceFaderSetting(uint8_t scene_index, uint8_t slot,
                                    uint8_t mode)
{
	/*
	 * Retain one Scene fader mode for the block-rate mixer consumer.
	 *
	 * Inputs: resident Scene index, zero-based instrument slot, and a
	 * 0..SCENE_FADER_SETTING_MAX mode (0..3). Output: SceneData retains the
	 * mode; the active mixer pulls it on its next block and applies the
	 * PRE/POST/FX/XFD topology beside the voice send tap (XFD, S074: the
	 * fader crossfades from the FX send at the bottom to the dry output at
	 * the top). Menu and storage callers remain independent of mixer
	 * internals.
     */
    if (!scene_get(scene_index) || slot >= INSTRUMENT_SLOT_COUNT)
        return 0u;
    if (mode > SCENE_FADER_SETTING_MAX)
        mode = SCENE_FADER_SETTING_MAX;
    scene_setVoiceFaderSetting(scene_index, slot, mode);
    return 1u;
}

uint8_t preset_setSlot6Track7AmpEnvelopeDecay(uint8_t scene_index,
                                              instrument_image_select_t image,
                                              uint8_t value,
                                              uint8_t record_automation)
{
    (void)record_automation;

    /*
     * Retain the generated non-Choke track-7 decay endpoint.
     *
     * Inputs: Scene index, endpoint image selector, 0..127 value, and the
     * normal automation-recording flag used by Menu edits. Output: the
     * generated Kit setting is updated in SceneData. This function exists
     * beside descriptor setters because the generated value has no descriptor
     * index and must never be saved into an instrument file. Future Scene mod
     * target work can extend this boundary with runtime apply/automation
     * capture without teaching Menu the kit_settings_t layout.
     */
    if (!scene_get(scene_index))
        return 0u;
    if (value > 127u)
        value = 127u;
    if (image == INSTRUMENT_IMAGE_MORPH)
        scene_setSlot6Track7MorphAmpEnvelopeDecay(scene_index, value);
    else
        scene_setSlot6Track7AmpEnvelopeDecay(scene_index, value);
    /*
     * S076 Rule A: a non-automation retained write supersedes any active
     * step decay override. The generated track-7 trigger path reads the
     * step override first when active; clearing it makes the next trigger
     * use the retained Kit value (or the LFO layer, if one is active).
     * Input: active Scene guard. Output:
     * instrumentManager_clearSlot6Track7StepDecayOverride() sets
     * slot6_track7_decay_step_active = 0.
     * Callers (upstream): menu_cellCommitValue() Kit setting branch,
     * InstrumentManager type-change normalization. Affiliate:
     * instrumentManager_setSlot6Track7StepDecayOverride() (the automation
     * setter), instrumentManager_getSlot6Track7StepDecay() (the trigger
     * consumer).
     */
    if (scene_index == scene_getActiveIndex())
        instrumentManager_clearSlot6Track7StepDecayOverride();
    return 1u;
}

void preset_applySceneSettings(uint8_t scene_index)
{
    const scene_t *scene = scene_getConst(scene_index);
    if (!scene || scene_index != scene_getActiveIndex())
        return;

    /*
     * S076 Rule B: Scene activation clears every Scene-target step override
     * left by the previous Scene's automation.
     *
     * What: deactivates all five persistent override families before the
     * new Scene's mirrors, routing, and Morph are applied. Without this
     * clear the new Scene's presetMorph_rebuildScene() reads stale
     * morph_step_override values from the previous Scene, the mixer
     * continues using the previous Scene's FX send override, and audio
     * routing does not switch.
     * Why: main-pattern automation sets these overrides per step, but the
     * overrides carry no Scene identity. A Scene switch means every
     * override from the previous Scene is stale; the new Scene's retained
     * values are the correct baseline.
     * Inputs: none (this function already validated that scene_index is the
     * active Scene). Outputs: all override .active flags are 0.
     * Effect Morph (effects_automation.morph_override_valid) is already
     * cleared by effects_activateScene() → effects_automationReset(),
     * which runs in preset_startDrumsetApply() at the same Scene-switch
     * boundary before this function is called. No duplicate clear needed.
     * Callers (upstream of this function): preset_startDrumsetApply()
     * (Scene switch), Bank Load, boot. Affiliates:
     * seq_restoreAllSceneAutomation() (the transport-boundary clear path,
     * unchanged).
     */
    preset_clearAllFxSendStepOverrides();
    preset_clearAllAudioOutStepOverrides(scene_index);
    presetMorph_clearAllStepAutomationOverrides(scene_index);
    instrumentManager_clearSlot6Track7StepDecayOverride();

    /*
     * Apply immediate Scene-wide settings that still have legacy mirrors.
     *
     * Inputs: active Scene index. Outputs: flat PERF mirrors are synchronized
     * from retained Scene settings and the PERF `fxm` mirror is refreshed. Instrument
     * runtime parameters, voice LFO slots/targets, audio out, future FX sends,
     * and fader assignments are deliberately excluded; the deferred slot worker
     * commits those per-instrument affiliates only when the old envelope is quiet
     * or when the new Scene pattern triggers that slot.
     *
     * This must not call preset_morph(), because preset_morph() is now the
     * user-facing bulk-set operation and would overwrite distinct per-voice Morph
     * values loaded from future sceneset.scg data.
    */
    preset_ensureMorphInitialized();
    preset_syncSceneMorphMirrors(scene);
    /*
     * Queue the newly active Scene's per-voice Morph image.
     *
     * Inputs: the active Scene whose retained Morph mirrors were just copied.
     * Output: all six voices enter the bounded Morph worker, closing the gap
     * between Scene-switch mirror refresh and the deferred per-slot image swap.
     * The call changes neither retained SceneData nor AutoSave state; the
     * deferred slot worker still performs the synchronous final apply when a
     * voice must be committed before its next trigger.
     *
     * Affiliate: presetMorph_rebuildScene() reads each Scene's voice-local
     * amount and preserves the existing one-descriptor-per-foreground-tick
     * budget.
     */
    presetMorph_rebuildScene(scene_index);
    /* S074: page mirrors follow the newly active Scene. */
    preset_syncBusCompMirrors();
    preset_syncEffectMorphMirror();
}

static void preset_storeSupplementalCell(uint8_t scene_index,
                                         uint8_t slot,
                                         kit_instrument_slot_t *instrument,
                                         uint8_t index,
                                         instrument_param_value_t value)
{
    /*
     * Keep all three generic images coherent for a non-morphable selector.
     *
     * Inputs: retained Scene/slot, slot pointer, descriptor index, and
     * normalized selector value. Output: the serialized normal cell uses the
     * common changed-value endpoint store and dirty marker; the nonserialized
     * Morph mirror and derived interpolation cell are repaired directly. This
     * helper is kept
     * separate from preset_setSupplementalParameter() because load-time
     * normalization must repair retained storage before that public setter
     * applies the active runtime binding; calling the setter three times would
     * repeat DSP side effects and still leave image ownership ambiguous.
     */
    if (!instrument || index >= INSTRUMENT_PARAM_COUNT)
        return;
    preset_storeInstrumentEndpoint(
        scene_index, slot, index, INSTRUMENT_IMAGE_MAIN, value);
    instrument->parameter_images.morph_instrument_parameters[index] = value;
    instrument->parameter_images.morph_interpolation[index] = value;
}

static void preset_normalizeLfoTargetPair(uint8_t scene_index,
                                          uint8_t source_slot,
                                          instrument_binding_kind_t voice_kind,
                                          instrument_binding_kind_t param_kind)
{
    kit_instrument_slot_t *instrument =
        scene_instrumentSlot(scene_index, source_slot);
    uint8_t voice_index;
    uint8_t param_index;
    instrument_param_value_t voice;
    instrument_target_token_t token;

    /*
     * Reconcile one retained LFO voice/parameter pair before runtime install.
     *
     * Inputs: Scene/source slot plus pair-specific binding kinds. Output: the
     * retained token is checked against the selected one-based target voice.
     * Voice namespaces store descriptor-local tokens; the Scene namespace
     * stores Scene target-table indices. Invalid pairs are changed to explicit
     * byte off before preset_applyKitVoiceSupplemental() installs the result.
     *
     * This cannot be folded into Menu normalization: files and type swaps reach
     * runtime without visiting Menu, and runtime must never follow a canonical
     * slot that contradicts the adjacent retained target-voice cell.
     */
    if (!instrument ||
        !instrumentManager_descriptorIndexForBinding(instrument->type,
                                                     voice_kind,
                                                     &voice_index) ||
        !instrumentManager_descriptorIndexForBinding(instrument->type,
                                                     param_kind,
                                                     &param_index)) {
        return;
    }
    voice = instrument->parameter_images.instrument_parameters[voice_index];
    token = instrument->parameter_images.instrument_parameters[param_index];
    if (!instrumentManager_lfoTargetVoiceValid(voice)) {
        voice = 1u;
        token = INSTRUMENT_TARGET_TOKEN_OFF;
    } else {
        instrument_param_id_t id =
            instrumentManager_lfoTargetIdFromToken(
                scene_index, source_slot, voice, token,
                INSTRUMENT_TARGET_MODULATION);
        token = instrumentManager_lfoTargetTokenFromId(
            scene_index, voice, id, INSTRUMENT_TARGET_MODULATION);
    }
    preset_storeSupplementalCell(
        scene_index, source_slot, instrument, voice_index, voice);
    preset_storeSupplementalCell(
        scene_index, source_slot, instrument, param_index, token);
}

static void preset_normalizeSlotModulationTargets(uint8_t scene_index,
                                                  uint8_t source_slot)
{
    kit_instrument_slot_t *instrument =
        scene_instrumentSlot(scene_index, source_slot);
    uint8_t velocity_index;

    /*
     * Normalize every supplemental destination owned by one source slot.
     *
     * Inputs: committed Scene and source slot. Output: both LFO pairs obey their
     * coupled voice/parameter contract, and velocity is either a valid target
     * for this source or off. Client: the all-source rebind loop after staged
     * Instrument commit and ordinary Kit-slot supplemental apply.
     */
    if (!instrument)
        return;
    preset_normalizeLfoTargetPair(scene_index, source_slot,
        INSTRUMENT_BIND_LFO_TARGET_VOICE,
        INSTRUMENT_BIND_LFO_TARGET_PARAM);
    preset_normalizeLfoTargetPair(scene_index, source_slot,
        INSTRUMENT_BIND_LFO_TARGET_VOICE_2,
        INSTRUMENT_BIND_LFO_TARGET_PARAM_2);
    if (instrumentManager_descriptorIndexForBinding(
            instrument->type, INSTRUMENT_BIND_VELOCITY_TARGET,
            &velocity_index)) {
        instrument_target_token_t target =
            instrument->parameter_images.instrument_parameters[velocity_index];
        if (!instrumentManager_targetValidForVelocitySource(
                scene_index, source_slot, target)) {
            target = INSTRUMENT_TARGET_TOKEN_OFF;
        }
        preset_storeSupplementalCell(
            scene_index, source_slot, instrument, velocity_index, target);
    }
}

static void preset_applyKitVoiceSupplementalInternal(uint8_t scene_index,
                                                     uint8_t voice,
                                                     uint8_t force)
{
    const kit_instrument_slot_t *instrument =
        scene_instrumentSlotConst(scene_index, voice);
    const instrument_registry_entry_t *entry;
    uint8_t i;

    /*
     * Apply one source slot's normalized non-image runtime bindings.
     *
     * Inputs: resident Scene/slot after its Morph runtime image is current.
     * Output: target selectors and other supplemental cells are installed in
     * descriptor order. Clients are Kit apply and Instrument's six-source
     * graph rebuild. This is separate from audio routing so rebind can revisit
     * all sources without rewriting unrelated mixer routes.
     */
    if (!instrument || voice >= INSTRUMENT_SLOT_COUNT)
        return;
    preset_normalizeSlotModulationTargets(scene_index, voice);
    entry = instrumentManager_registryEntry(instrument->type);
    if (!entry)
        return;
    for (i = 0u; i < entry->descriptor_count; i++) {
        const ParamDescriptor *descriptor = &entry->descriptors[i];
        if (descriptor->flags & INSTRUMENT_PARAM_FLAG_MORPHABLE)
            continue;
        (void)preset_setSupplementalParameterInternal(
            scene_index, voice, i,
            instrument->parameter_images.instrument_parameters[i], force);
    }
}

/*
 * Apply one source slot's normalized non-image runtime bindings.
 *
 * Inputs: resident Scene/slot after its Morph runtime image is current. Output:
 * target selectors are installed for the active Scene only. Client: the
 * all-source rebind loop after staged Instrument commit and ordinary Kit-slot
 * supplemental apply. Affiliate: preset_applyKitVoiceSupplementalForced().
 */
static void preset_applyKitVoiceSupplemental(uint8_t scene_index, uint8_t voice)
{
    preset_applyKitVoiceSupplementalInternal(scene_index, voice, 0u);
}

/*
 * Apply one source slot's supplemental bindings from a per-track played Scene
 * (S077 P2 §3.4, Q13).
 *
 * Inputs: the track's played Scene and slot. Output: the slot's target tokens
 * are installed into the live runtime even though the Scene is not active.
 * Client: preset_tickSingleVoiceApply() after committing one slot's image.
 * Guard: Scene-namespace target tokens still resolve through the active Scene's
 * target table (InstrumentManager's token expansion is active-Scene scoped);
 * voice-namespace LFO/velocity targets resolve slot-relative and are correct.
 */
static void preset_applyKitVoiceSupplementalForced(uint8_t scene_index,
                                                   uint8_t voice)
{
    preset_applyKitVoiceSupplementalInternal(scene_index, voice, 1u);
}

/* Reset and apply one loaded Scene slot's descriptor image, without binding
 * cross-slot modulation destinations.
 *
 * Why this boundary exists: a tagged runtime member must first be reset from
 * the active Scene's actual type, then receive routing and Morph-derived
 * descriptor values. LFO/velocity targets are deliberately excluded because
 * their target can be any other slot; binding them before every target member
 * has been reset would resolve a valid token through a stale runtime type.
 *
 * Inputs: active Scene index and zero-based slot. Outputs: the matching tagged
 * member, its Scene routing, and its morphable descriptor image are current.
 * Clients: synchronous boot activation, deferred Scene switching, and the
 * trigger-time replacement path. Affiliates: preset_applyKitVoiceSupplemental
 * owns the later all-source graph rebind, while InstrumentManager owns tagged
 * type construction and PresetMorph owns descriptor interpolation.
 */
static void preset_resetAndApplyKitVoiceImage(uint8_t scene_index,
                                              uint8_t voice)
{
    if (!scene_instrumentSlotConst(scene_index, voice) ||
        voice >= INSTRUMENT_SLOT_COUNT) {
        return;
    }

    /*
     * Rebind the DSP runtime type at the same time as parameter commit.
     *
     * Input: the Scene slot selected by the activation worker (the active
     * Scene for a scene-level switch, or the track's played Scene for the
     * S077 P2 single-voice per-track apply). Output: InstrumentManager's
     * runtime type shadow and the concrete DSP voice object are reset from that
     * Scene immediately before routing/Morph descriptor values are copied.
     * The reset now resolves the incoming type from the applied Scene, so the
     * single-voice path commits the played Scene's instrument rather than the
     * active Scene's.
     */
    instrumentManager_resetRuntimeSlotFromScene(voice, scene_index);
    (void)preset_applyKitAudioRouting(scene_index, voice);
    /*
     * Commit the Morph interpolation image. An inactive played Scene uses the
     * forced variant so the runtime receives that Scene's descriptor values;
     * the active Scene keeps the normal path.
     */
    if (scene_index == scene_getActiveIndex())
        presetMorph_applyVoiceNow(scene_index, voice);
    else
        presetMorph_applyVoiceNowFromScene(scene_index, voice);
    /*
     * S076 P2: restore or reset the LFO phase based on the incoming Scene's
     * retrigger setting (S076 P2).
     *
     * What:       calls instrumentManager_restoreLfoPhaseIfNeeded(voice) which
     *             checks the incoming LFO's retrigger field:
     *             - LFO_RETRIGGER_SCENE: phase = phaseOffset (reset to the
     *               incoming Scene's configured start offset).
     *             - Otherwise: phase = captured snapshot from the outgoing
     *               Scene (continue the LFO cycle uninterrupted).
     * Why:        instrumentManager_resetRuntimeSlot() (called earlier in this
     *             function or already complete) zeroed the phase. The
     *             retrigger value was not available at reset time - it was
     *             written by presetMorph_applyVoiceNow() in the line above.
     *             This is the earliest point where both the retrigger setting
     *             and the scaled phaseOffset are in the runtime LFO.
     * Inputs:     zero-based voice slot; the incoming LFO's retrigger and
     *             phaseOffset fields (already written by descriptor apply);
     *             the static handoff struct (written by captureLfoPhases).
     * Outputs:    lfo->phase set to either phaseOffset or captured phase.
     * Guard:      runs for both the active Scene and the per-track played
     *             Scene, because the slot reset above zeroed the phase in both
     *             cases and the handoff snapshot must be honoured either way.
     * Affiliates: instrumentManager_captureLfoPhases() (the snapshot writer,
     *             called in preset_startDrumsetApply()),
     *             LFO_RETRIGGER_SCENE (lfo.h).
     */
    instrumentManager_restoreLfoPhaseIfNeeded(voice);
}

/* Start the existing bounded all-source modulation rebind cursor.
 *
 * Inputs: an active Scene whose every slot has already received its incoming
 * tagged runtime type and descriptor image. Outputs: preset_tickInstrumentApply
 * will normalize/install one source's velocity plus both LFO target pairs per
 * foreground pass. Reusing this cursor avoids a new retained allocation and
 * gives boot, deferred Scene selection, and normal Instrument Load the same
 * target-resolution ordering. Clients must keep the Scene apply gate active
 * until the cursor drains so Menu never releases a half-rebound graph.
 */
static void preset_startSceneModulationRebind(uint8_t scene_index)
{
    instrument_apply_active = 1u;
    instrument_apply_scene = scene_index;
    instrument_apply_phase = INSTRUMENT_APPLY_PHASE_TARGET_REBIND;
    instrument_apply_rebind_source = 0u;
    instrument_apply_morph_only = 0u;
}

/* Synchronously activate the complete active Scene before audio starts.
**
** Inputs: the Bank/Scene loader has committed and selected the active Scene;
** audioCodec_renderCount is still zero. Outputs: all six tagged runtime types,
** descriptor images, routing, and all-source LFO/velocity bindings match that
** one Scene before DMA audio can render. The transaction is deliberately
** four ordered phases: clear outgoing graph, reset/apply every image, drain
** pending Morph writes, and returns with a coherent pre-audio image. It does
** not install LFO/velocity targets: main.c starts the exact
** preset_startDrumsetApply()/preset_tickDrumsetApply() Scene-switch worker as
** soon as the audio engine is live. This avoids claiming that a boot-specific
** pre-audio binding sequence is equivalent to the already-working runtime
** Scene lifecycle. No retained state is allocated. */
void preset_sendDrumsetParameters(void)
{
    uint8_t voice;

    instrumentManager_clearAllRuntimeModulationTargets();
    preset_ensureMorphInitialized();
    preset_applySceneSettings(scene_getActiveIndex());
    /* Activate the retained Effect beside the synchronous Scene settings. */
    effects_activateScene(scene_getActiveIndex());
    for (voice = 0; voice < 6u; voice++)
        preset_resetAndApplyKitVoiceImage(scene_getActiveIndex(), voice);

    while (presetMorph_tick()) {
        /*
         * Resolve every pending descriptor interpolation before target install.
         *
         * Inputs: all six reset/image-applied members plus any retained Morph
         * worker requests from the loaded active Scene. Output: final
         * descriptor-domain bases are live before an LFO adapter snapshots its
         * destination. This is pre-audio only; runtime Scene switching keeps
         * the same ordering through its normal bounded workers. Affiliates:
         * presetMorph_applyVoiceNow(), preset_tickInstrumentApply(), and
         * instrumentManager_installLfoModulationTarget().
         */
    }

}

void preset_startDrumsetApply(void)
{
    /*
     * S076 P2: snapshot each slot's running LFO phase before the deferred
     * Scene worker resets runtime slots (S076 P2).
     *
     * What:       calls instrumentManager_captureLfoPhases() to read the
     *             current phase of each slot's LFO into the static handoff
     *             struct.
     * Why:        instrumentManager_resetRuntimeSlot() will zero the phase
     *             via memset + lfo_init. The snapshot preserves the running
     *             phase for slots whose incoming LFO does not have the scn
     *             retrigger set.
     * Inputs:     current runtime LFO phases (six uint32_t values).
     * Outputs:    lfo_scene_handoff populated and marked valid.
     * Placement:  before instrumentManager_clearAllRuntimeModulationTargets()
     *             because the mod target teardown does not modify LFO phase
     *             but must complete before any slot reset begins. The capture
     *             runs once per Scene worker, not per slot.
     * Affiliates: instrumentManager_restoreLfoPhaseIfNeeded() (the consumer,
     *             called per slot in preset_resetAndApplyKitVoiceImage()),
     *             instrumentManager_resetRuntimeSlot() (the reset that
     *             destroys the phase).
     */
    instrumentManager_captureLfoPhases();
    /*
     * Detach outgoing runtime targets before the deferred Scene worker starts.
     *
     * Inputs: the six currently tagged runtime members and their all-source
     * modulation graph. Output: no LFO or velocity node retains a pointer into
     * a member that the worker may overwrite after a quiet or trigger-time
     * handoff. This runs once per worker, not per slot reset, so later slots do
     * not erase destinations that earlier incoming slots have already rebuilt.
     */
    instrumentManager_clearAllRuntimeModulationTargets();
    preset_ensureMorphInitialized();
    preset_applySceneSettings(scene_getActiveIndex());
    /* The deferred Scene worker uses the same immediate Effect activation. */
    effects_activateScene(scene_getActiveIndex());
    drumset_apply_scene = scene_getActiveIndex();
    drumset_apply_pending_mask =
        (uint16_t)((1u << INSTRUMENT_SLOT_COUNT) - 1u);
    drumset_apply_active = 1u;
    drumset_apply_voice = 0u;
    /*
     * A scene-level switch coalesces all tracks (S077 P2 §4.3, Q2), so any
     * pending per-track single-voice commit is stale and dropped here.
     */
    single_voice_pending_mask = 0u;
    /* A new Scene worker must not inherit non-progress from its predecessor. */
    drumset_apply_stall_ticks = 0u;
}

/*
 * Apply per-voice Scene settings from a specific source Scene (S077 P2 §3.5).
 *
 * What: writes one slot's audio output route from the source Scene into the
 * live mixer array. Why: when a track plays from a non-active Scene, its
 * voice-level routing must come from the source Scene rather than the active
 * Scene. Inputs: slot 0..5, source Scene index. Output: mixer_audioRouting[]
 * updated for this slot only; no AutoSave or retained SceneData mutation.
 * Note: FX send, fader mode, and per-voice Morph amount are read live every
 * block by mixer_faderGains()/preset_getEffectiveFxSendAmount() through
 * preset_getSlotPlayedScene(), so they need no snapshot here and follow later
 * edits in the played Scene automatically. Not per-track (always from active
 * Scene): the Effect record, bus compressor, global Morph amount, and effect
 * Morph amount. Affiliates: preset_applyKitAudioRouting(), scene_getVoiceAudioOut().
 */
static void preset_applyPerVoiceSceneSettings(uint8_t slot,
                                              uint8_t source_scene)
{
    uint8_t route;

    if (slot >= INSTRUMENT_SLOT_COUNT || !scene_getConst(source_scene))
        return;
    route = scene_getVoiceAudioOut(source_scene, slot);
    if (route > MIXER_ROUTING_DAC2_R)
        route = MIXER_ROUTING_DAC1_STEREO;
    mixer_audioRouting[slot] = route;
}

/*
 * Commit one quiet slot from its played Scene for per-track playback
 * (S077 P2 §3.4).
 *
 * What: shared commit body for the deferred single-voice worker and the
 * trigger-time force path. Inputs: slot 0..5. Output: the slot's runtime image,
 * routing, and supplemental targets are applied from seq_perTrackPattern[slot].
 * Affiliates: preset_resetAndApplyKitVoiceImage(),
 * preset_applyPerVoiceSceneSettings(), preset_applyKitVoiceSupplementalForced().
 */
static void preset_commitSingleVoiceSlot(uint8_t slot)
{
    uint8_t source_scene;

    if (slot >= INSTRUMENT_SLOT_COUNT)
        return;
    source_scene = seq_getTrackPlayedScene(slot);
    preset_resetAndApplyKitVoiceImage(source_scene, slot);
    preset_applyPerVoiceSceneSettings(slot, source_scene);
    preset_applyKitVoiceSupplementalForced(source_scene, slot);
}

/*
 * Advance at most one deferred per-track single-voice apply (S077 P2 §3.4).
 *
 * What: scans single_voice_pending_mask round-robin and commits the first slot
 * whose amp envelope is quiet. Why: a per-track Scene assignment must hot-swap
 * that slot's instrument without clicking, exactly like the drumset worker, and
 * without disturbing the other five slots. Inputs: single_voice_pending_mask
 * and instrumentManager_ampEnvelopeQuiet(). Output: 1 when one slot committed
 * this pass, 0 otherwise. A continuously ringing slot is committed by the
 * trigger-time force path (preset_applyDeferredSceneSlotForTrigger()).
 * Affiliates: preset_commitSingleVoiceSlot().
 */
static uint8_t preset_tickSingleVoiceApply(void)
{
    uint8_t slot;

    if (single_voice_pending_mask == 0u)
        return 0u;
    for (slot = 0u; slot < INSTRUMENT_SLOT_COUNT; slot++) {
        uint8_t bit = (uint8_t)(1u << slot);

        if ((single_voice_pending_mask & bit) == 0u)
            continue;
        if (!instrumentManager_ampEnvelopeQuiet(slot))
            continue;
        preset_commitSingleVoiceSlot(slot);
        single_voice_pending_mask =
            (uint8_t)(single_voice_pending_mask & ~bit);
        return 1u;
    }
    return 0u;
}

/*
 * Arm a deferred single-voice apply for one slot from a played Scene
 * (S077 P2 §2.1, §3.4).
 *
 * What: a single-slot variant of preset_startDrumsetApply() for per-track Scene
 * assignment. It (1) supersedes the drumset worker for this slot by clearing the
 * slot's bit from drumset_apply_pending_mask, (2) captures the slot's LFO phase,
 * (3) clears this slot's runtime modulation targets only, (4) applies the source
 * Scene's audio routing, and (5) arms single_voice_pending_mask for the deferred
 * quiet-wait commit. Why: assigning a track to another Scene must load that
 * track's voice slot from the played Scene using the same envelope-quiet
 * deferral as the drumset worker, without replacing the other slots. Inputs:
 * slot 0..5, source Scene index (the caller passes seq_perTrackPattern[slot]).
 * Outputs: the deferred worker commits the slot on a later foreground pass, or
 * the trigger path forces it. Coexistence: clearing the drumset bit is the only
 * coordination needed - the drumset worker already skips cleared slots (plan
 * §3.4, Q6-b). For tracks 5+6 (HiHat pair) the caller invokes this for the one
 * shared slot. Affiliates: preset_tickSingleVoiceApply(),
 * instrumentManager_captureLfoPhaseForSlot(),
 * instrumentManager_clearRuntimeModulationTargetsForSlot(),
 * preset_applyPerVoiceSceneSettings().
 */
void preset_startSingleVoiceApply(uint8_t slot, uint8_t source_scene)
{
    uint8_t bit;

    if (slot >= INSTRUMENT_SLOT_COUNT || !scene_getConst(source_scene))
        return;
    bit = (uint8_t)(1u << slot);
    /* Supersede the drumset worker for this slot (Q6-b). */
    drumset_apply_pending_mask =
        (uint16_t)(drumset_apply_pending_mask & ~(uint16_t)bit);
    /* Snapshot the running LFO phase before the slot reset destroys it. */
    instrumentManager_captureLfoPhaseForSlot(slot);
    /* Detach this slot's outgoing modulation graph only. */
    instrumentManager_clearRuntimeModulationTargetsForSlot(slot);
    /* Apply the source Scene's routing now; the image commits when quiet. */
    preset_applyPerVoiceSceneSettings(slot, source_scene);
    single_voice_pending_mask = (uint8_t)(single_voice_pending_mask | bit);
}

/*
 * Report whether the Scene and Instrument apply workers are idle (S075).
 *
 * Contract in presetManager.h; reads the two worker flags only.
 */
uint8_t preset_applyWorkersIdle(void)
{
    return (uint8_t)(!drumset_apply_active && !instrument_apply_active &&
                     single_voice_pending_mask == 0u);
}

uint8_t preset_tickDrumsetApply(void)
{
    uint8_t checked;

    /*
     * Service the per-track single-voice worker first (S077 P2 §3.4). It
     * commits at most one quiet slot per pass; returning here yields to audio
     * exactly as the Scene worker does, so every existing caller polls it.
     */
    if (preset_tickSingleVoiceApply())
        return 1u;

    if (!drumset_apply_active)
        return 0u;

    if (drumset_apply_scene != scene_getActiveIndex()) {
        /*
         * A newer Scene switch supersedes this worker.
         *
         * Input: active Scene changed since the pending mask was armed. Output:
         * stale slot bits are discarded so the old Scene can never apply over
         * the current Scene. preset_startDrumsetApply() has already armed the
         * replacement worker for the new active Scene.
         */
        drumset_apply_active = 0u;
        drumset_apply_pending_mask = 0u;
        return 0u;
    }

    /*
     * Complete Scene activation only after every incoming member is live.
     *
     * Inputs: the image pass cleared its six pending bits. Outputs: the
     * existing Instrument apply cursor rebuilds all LFO/velocity sources
     * against the final tagged type vector, one source per foreground pass.
     * Keeping drumset_apply_active set until this cursor drains makes Menu's
     * normal sound-apply gate wait for graph validity without new state.
     */
    if (drumset_apply_pending_mask == 0u) {
        if (instrument_apply_active && preset_tickInstrumentApply())
            return 1u;
        drumset_apply_active = 0u;
        return 0u;
    }

    for (checked = 0u; checked < INSTRUMENT_SLOT_COUNT; checked++) {
        uint8_t voice = drumset_apply_voice;
        uint16_t bit = (uint16_t)(1u << voice);

        /*
         * Scan the pending bitmask round-robin.
         *
         * Inputs: drumset_apply_voice is the next slot to test, preserving fair
         * progress when several envelopes are ringing. Output: at most one quiet
         * slot is committed per foreground pass; non-quiet slots remain pending
         * for a later tick or trigger-time force-apply.
         */
        drumset_apply_voice++;
        if (drumset_apply_voice >= INSTRUMENT_SLOT_COUNT)
            drumset_apply_voice = 0u;
        if ((drumset_apply_pending_mask & bit) == 0u)
            continue;
        if (!instrumentManager_ampEnvelopeQuiet(voice)) {
            /*
             * No quiet result is observable for this pending voice. Count
             * foreground passes rather than wall-clock calls so the bound is
             * expressed in the same cadence that owns this cooperative
             * worker. Once the bound is reached, use the already-established
             * trigger-time force commit for this exact voice and clear the
             * episode counter. The pending-mask helper performs the normal
             * runtime-image commit; this branch only guarantees progress.
             */
            if (++drumset_apply_stall_ticks >= DRUMSET_APPLY_FORCE_TICKS) {
                drumset_apply_stall_ticks = 0u;
                preset_applyDeferredSceneSlotForTrigger(voice);
            }
            continue;
        }

        /* A quiet commit is real progress; a later voice starts a new wait. */
        drumset_apply_stall_ticks = 0u;

        preset_resetAndApplyKitVoiceImage(drumset_apply_scene, voice);
        drumset_apply_pending_mask =
            (uint16_t)(drumset_apply_pending_mask & ~bit);
        if (drumset_apply_pending_mask == 0u)
            preset_startSceneModulationRebind(drumset_apply_scene);
        return 1u;
    }

    return 0u;
}

void preset_applyDeferredSceneSlotForTrigger(uint8_t trigger_track)
{
    uint8_t voice;
    uint16_t bit;

    /*
     * Force a pending Scene slot to commit immediately before it triggers.
     *
     * Inputs: visible trigger track 0..6 from the sequencer/MIDI trigger queue.
     * Output: if that track maps to a pending instrument slot for the active
     * Scene, its runtime type, descriptor image, and audio route apply before
     * InstrumentManager receives the note trigger. Cross-slot LFO/velocity
     * bindings stay detached until every pending member is valid, then the
     * common all-source rebind cursor installs them without stale pointers.
     */
    /*
     * Force a pending per-track single-voice slot first (S077 P2 §3.4).
     *
     * What: if this trigger's slot has a deferred single-voice commit armed,
     * commit it from the track's played Scene before the note fires. Why: a
     * continuously ringing slot cannot reach the quiet threshold, so the trigger
     * is the accepted force point (same policy as the drumset worker). Inputs:
     * trigger_track 0..6. Output: the played Scene image, routing, and
     * supplemental targets are applied and the pending bit cleared.
     */
    if (trigger_track <= INSTRUMENT_SLOT_COUNT) {
        uint8_t svoice = (trigger_track >= INSTRUMENT_SLOT_COUNT)
            ? (INSTRUMENT_SLOT_COUNT - 1u) : trigger_track;
        uint8_t sbit = (uint8_t)(1u << svoice);

        if ((single_voice_pending_mask & sbit) != 0u) {
            presetMorph_prioritizeVoice(seq_getTrackPlayedScene(svoice),
                                        svoice);
            preset_commitSingleVoiceSlot(svoice);
            single_voice_pending_mask =
                (uint8_t)(single_voice_pending_mask & ~sbit);
        }
    }
    if (!drumset_apply_active ||
        drumset_apply_scene != scene_getActiveIndex()) {
        return;
    }
    if (trigger_track > INSTRUMENT_SLOT_COUNT)
        return;
    voice = (trigger_track >= INSTRUMENT_SLOT_COUNT)
        ? (INSTRUMENT_SLOT_COUNT - 1u)
        : trigger_track;
    bit = (uint16_t)(1u << voice);
    if ((drumset_apply_pending_mask & bit) == 0u)
        return;

    presetMorph_prioritizeVoice(drumset_apply_scene, voice);
    preset_resetAndApplyKitVoiceImage(drumset_apply_scene, voice);
    drumset_apply_pending_mask =
        (uint16_t)(drumset_apply_pending_mask & ~bit);
    if (drumset_apply_pending_mask == 0u)
        preset_startSceneModulationRebind(drumset_apply_scene);
}

static uint8_t preset_copyInstrumentNormalToMorphIfSameType(
    kit_instrument_slot_t *destination,
    const kit_instrument_slot_t *source)
{
    const instrument_registry_entry_t *entry;
    uint8_t index;
    uint8_t copied = 0u;

    /*
     * Copy one staged source normal endpoint into a resident morph endpoint.
     *
     * Inputs: destination resident slot and staged source slot. Output:
     * morphable descriptor values are copied by descriptor index only when the
     * instrument types match. A mismatch is a complete no-change for that slot,
     * which keeps KitMrp/InstrumentMrp per-instrument and avoids inventing
     * cross-type parameter mapping in the loader.
     */
    if (!destination || !source || destination->type != source->type)
        return 0u;
    entry = instrumentManager_registryEntry(destination->type);
    if (!entry)
        return 0u;
    for (index = 0u; index < entry->descriptor_count; index++) {
        const ParamDescriptor *descriptor = &entry->descriptors[index];
        if (!(descriptor->flags & INSTRUMENT_PARAM_FLAG_MORPHABLE))
            continue;
        destination->parameter_images.morph_instrument_parameters[index] =
            source->parameter_images.instrument_parameters[index];
        copied = 1u;
    }
    return copied;
}

static uint8_t preset_commitStagedInstrumentNormalToMorph(
    uint8_t scene_index,
    uint8_t slot)
{
    const kit_instrument_slot_t *staged =
        (const kit_instrument_slot_t *)filesystem_loadedInstrumentSlot();
    kit_instrument_slot_t *destination = scene_instrumentSlot(scene_index, slot);

    /*
     * Commit a staged Instrument file as a morph endpoint update.
     *
     * Unlike preset_startInstrumentApply(), this path does not replace the slot,
     * copy the display name, reset runtime, or clear/rebind modulation. If the
     * staged type no longer matches the resident destination type, the operation
     * is no-change. Otherwise only morphable normal endpoint bytes are copied,
     * and the Morph worker reapplies the current morph amount.
     */
    if (!destination || !staged || slot >= INSTRUMENT_SLOT_COUNT ||
        staged->type != destination->type ||
        staged->type != (instrument_type_t)pm_instrument_request_type) {
        return 0u;
    }
    return preset_copyInstrumentNormalToMorphIfSameType(destination, staged);
}

static uint8_t preset_copyInstrumentMorphToMorphIfSameType(
    kit_instrument_slot_t *destination,
    const kit_instrument_slot_t *source)
{
    const instrument_registry_entry_t *entry;
    uint8_t index;
    uint8_t copied = 0u;

    /*
     * Copy one staged source Morph endpoint into a resident Morph endpoint.
     *
     * Inputs: destination resident slot and staged source slot. Output:
     * Morphable descriptor cells are copied from the staged Morph image only
     * when the instrument types match. This restores the entry endpoint domain
     * captured by the hidden InstrumentMrp kit snapshot. A mismatch is a
     * complete no-change for that slot, which keeps InstrumentMrp
     * per-instrument and avoids inventing cross-type parameter mapping.
     */
    if (!destination || !source || destination->type != source->type)
        return 0u;
    entry = instrumentManager_registryEntry(destination->type);
    if (!entry)
        return 0u;
    for (index = 0u; index < entry->descriptor_count; index++) {
        const ParamDescriptor *descriptor = &entry->descriptors[index];
        if (!(descriptor->flags & INSTRUMENT_PARAM_FLAG_MORPHABLE))
            continue;
        destination->parameter_images.morph_instrument_parameters[index] =
            source->parameter_images.morph_instrument_parameters[index];
        copied = 1u;
    }
    return copied;
}

static uint8_t preset_commitStagedInstrumentMorphToMorph(
    uint8_t scene_index,
    uint8_t slot)
{
    const kit_instrument_slot_t *staged =
        (const kit_instrument_slot_t *)filesystem_loadedInstrumentSlot();
    kit_instrument_slot_t *destination = scene_instrumentSlot(scene_index, slot);

    /*
     * Commit the staged hidden InstrumentMrp baseline as a Morph restore.
     *
     * The hidden snapshot's meaningful payload is its [morph] section, whose
     * cells are the entry-time Morphable Morph endpoints. Unlike a pool
     * InstrumentMrp load, which copies the file's normal endpoints, this path
     * copies the staged Morph image back into the same resident Morph domain.
     * Type, Normal image, display name, HCNAMES source, and routing remain
     * untouched while the previewed endpoints return to their entry values.
     */
    if (!destination || !staged || slot >= INSTRUMENT_SLOT_COUNT ||
        staged->type != destination->type ||
        staged->type != (instrument_type_t)pm_instrument_request_type) {
        return 0u;
    }
    return preset_copyInstrumentMorphToMorphIfSameType(destination, staged);
}

static uint8_t preset_commitStagedKitNormalToMorph(void)
{
    const kit_t *source = filesystem_loadedKit();
    uint8_t scene_index;
    uint8_t active_queued = 0u;

    /*
     * Commit staged Kit normal endpoints into resident Kit morph endpoints.
     *
     * This deliberately preserves destination kit slot types, display names,
     * audio routing, and supplemental modulation bindings. For each slot, a
     * matching source/destination instrument type copies morphable descriptor
     * values by index; a mismatch skips the whole slot as a no-change. The
     * generated slot-6/track-7 morph setting follows the same matching-slot
     * rule.
     */
    if (!source)
        return 0u;
    for (scene_index = 0u;
         scene_index < SCENE_COUNT && scene_index < 16u;
         scene_index++) {
        scene_t *scene;
        uint8_t slot;
        if ((pm_kit_request_scene_mask &
             (uint16_t)(1u << scene_index)) == 0u) {
            continue;
        }
        scene = scene_get(scene_index);
        if (!scene)
            continue;
        for (slot = 0u; slot < INSTRUMENT_SLOT_COUNT; slot++) {
            if (preset_copyInstrumentNormalToMorphIfSameType(
                    &scene->kit.instruments[slot],
                    &source->instruments[slot])) {
                /*
                 * Record one successful KitMrp endpoint import at its
                 * retained-data boundary.
                 *
                 * Inputs: a selected resident Scene/slot and a nonzero result
                 * from preset_copyInstrumentNormalToMorphIfSameType(), which
                 * proves the source and destination types matched and one or
                 * more Morphable destination cells were assigned. Output:
                 * Autosave offers exactly that slot's Morphable Morph
                 * descriptor cells to its existing tracking/Bank-present map.
                 * Why: KitMrp preserves type, Normal image, names, routing,
                 * and modulation ownership, so a whole-Kit or whole-Instrument
                 * marker would over-publish unrelated payload. This is outside
                 * the active Scene runtime branch: inactive Bank-present Scenes
                 * also need persistence, while only the active Scene needs
                 * presetMorph_requestVoice(). Affiliates:
                 * autosave_markInstrumentMorphDirty(),
                 * presetMorph_requestVoice(), and the staged Kit lifetime
                 * established by on_kit_morph_load_complete().
                */
                autosave_markInstrumentMorphDirty(scene_index, slot);
                /*
                 * Option 2: KitMrp changed the resident Morph endpoint image,
                 * so the owning Scene can no longer be skipped as card-clean.
                 * The slot-6 generated decay below invalidates through
                 * SceneData; this covers the direct instrument-image copy that
                 * otherwise bypasses the scalar funnels.
                 */
                bank_invalidateSdCleanScene(scene_index);
                if (scene_index == scene_getActiveIndex()) {
                    presetMorph_requestVoice(scene_index, slot);
                    active_queued = 1u;
                }
            }
        }
        if (INSTRUMENT_SLOT_COUNT > 5u &&
            scene->kit.instruments[5].type == source->instruments[5].type) {
            /*
             * Commit the KitMrp source's generated track-7 normal decay as the
             * destination Morph endpoint through SceneData's named setter.
             *
             * Inputs: a selected Scene, matching slot-6 types, and the bounded
             * staged normal decay value. Output: the generated Morph-decay byte
             * changes only when different and then offers
             * AUTOSAVE_KIT_PARAM_SLOT6_TRACK7_MORPH_DECAY to its existing dirty
             * map. Why: this endpoint is a Kit setting rather than an
             * Instrument descriptor, so the per-slot Morph marker cannot
             * represent it. The setter preserves the normal owner rule—store
             * first, then mark—and leaves runtime scheduling to the existing
             * active-Scene branch. Affiliates:
             * scene_setSlot6Track7MorphAmpEnvelopeDecay(),
             * scene_storeKitParameterByte(), and presetMorph_requestVoice().
             */
            scene_setSlot6Track7MorphAmpEnvelopeDecay(
                scene_index,
                source->settings.slot6_track7_amp_envelope_decay);
            if (scene_index == scene_getActiveIndex()) {
                presetMorph_requestVoice(scene_index, 5u);
                active_queued = 1u;
            }
        }
    }
    return active_queued;
}

void preset_startKitMorphApply(void)
{
    /*
     * Commit staged KitMrp endpoints and arm a morph-only runtime refresh.
     *
     * KitMrp changes morph endpoint storage only. It must not use the normal
     * drumset apply cursor because that cursor reapplies Scene settings,
     * routing, and supplemental target bindings for a replaced kit. Here the
     * resident kit identity is preserved and only the Morph worker is drained.
     */
    instrument_apply_active = 0u;
    preset_ensureMorphInitialized();
    if (preset_commitStagedKitNormalToMorph()) {
        instrument_apply_active = 1u;
        instrument_apply_scene = scene_getActiveIndex();
        instrument_apply_phase = INSTRUMENT_APPLY_PHASE_MORPH_REBUILD;
        instrument_apply_rebind_source = 0u;
        instrument_apply_morph_only = 1u;
    }
}

void preset_startInstrumentMorphApply(uint8_t scene_index, uint8_t slot)
{
    /*
     * Commit staged InstrumentMrp endpoints and arm a morph-only refresh.
     *
     * InstrumentMrp preserves the destination slot identity. It copies no
     * display name, performs no routing apply, and does not clear/rebind
     * modulation targets. A type mismatch is a no-change operation; the cursor
     * remains inactive and Menu will simply unlock on the next poll.
     *
     * The staged-image source is origin-dependent. A pool InstrumentMrp load
     * commits the file's normal endpoints into the resident Morph endpoints,
     * while the hidden reversible kit baseline commits its captured entry
     * Morph endpoints through the matching Morph-to-Morph copy. The filesystem
     * origin flag remains valid until the next request reuses operation
     * scratch, and this completion runs before any new request is posted.
     */
    instrument_apply_active = 0u;
    preset_ensureMorphInitialized();
    if (filesystem_loadedInstrumentWasMorphTemporary()
            ? preset_commitStagedInstrumentMorphToMorph(scene_index, slot)
            : preset_commitStagedInstrumentNormalToMorph(scene_index, slot)) {
        /*
         * InstrumentMrp copied only compatible Morphable endpoint values into
         * the retained destination. Mark precisely that same descriptor domain
         * now: type, Normal endpoints, name, HCNAMES source, routing, and
         * runtime interpolation did not change. This remains outside the
         * active-Scene branch because inactive resident Scene data still needs
         * persistence even though it requires no immediate audible refresh.
        */
        autosave_markInstrumentMorphDirty(scene_index, slot);
        /*
         * Option 2: InstrumentMrp changed the resident Morph image. The
         * destination Scene's card-clean bit clears because its saved payload
         * no longer equals the on-card Bank child. Affiliate:
         * bank_invalidateSdCleanScene().
         */
        bank_invalidateSdCleanScene(scene_index);
        if (scene_index == scene_getActiveIndex()) {
            presetMorph_requestVoice(scene_index, slot);
            instrument_apply_active = 1u;
            instrument_apply_scene = scene_index;
            instrument_apply_phase = INSTRUMENT_APPLY_PHASE_MORPH_REBUILD;
            instrument_apply_rebind_source = 0u;
            instrument_apply_morph_only = 1u;
        }
    }
}

/*
 * Move `self` LFO voice selectors of a copied Instrument to its new slot.
 *
 * Inputs: destination image, its type, the one-based source and destination
 * slot numbers. Output: every LFO voice-selector cell (both LFOs) in the
 * Normal, Morph and interpolation images that held the source slot now holds
 * the destination slot; every other selector (other voices, `scn`, `fx`) is
 * unchanged. Why: Kit/Instrument Save writes `self` for a selector equal to
 * the own slot, and a resident copy must keep the same meaning (S075, spec
 * §4.4 `copy instrument`). Caller: preset_startInstrumentApplyImage().
 */
static void preset_retargetSelfLfoVoice(kit_instrument_slot_t *slot_image,
                                        uint8_t source_voice,
                                        uint8_t dest_voice)
{
    instrument_parameter_images_t *images;
    uint8_t i;

    if (!slot_image || source_voice == dest_voice)
        return;
    images = &slot_image->parameter_images;
    for (i = 0u; i < INSTRUMENT_PARAM_COUNT; i++) {
        const ParamDescriptor *descriptor =
            instrumentManager_descriptor(slot_image->type, i);

        if (!descriptor)
            break;
        if (descriptor->runtime.kind != INSTRUMENT_BIND_LFO_TARGET_VOICE &&
            descriptor->runtime.kind != INSTRUMENT_BIND_LFO_TARGET_VOICE_2)
            continue;
        if (images->instrument_parameters[i] == source_voice)
            images->instrument_parameters[i] = dest_voice;
        if (images->morph_instrument_parameters[i] == source_voice)
            images->morph_instrument_parameters[i] = dest_voice;
        if (images->morph_interpolation[i] == source_voice)
            images->morph_interpolation[i] = dest_voice;
    }
}

static void preset_startInstrumentApplyImage(const kit_instrument_slot_t *staged,
                                             uint16_t destination_mask,
                                             uint8_t slot,
                                             uint8_t source_slot,
                                             instrument_type_t expected_type,
                                             uint8_t mark_autosave_whole_instrument)
{
    uint8_t target_scene_index;
    uint8_t active_scene_touched = 0u;

    /*
     * Commit one validated Instrument image and arm the bounded runtime apply.
     *
     * Inputs: an immutable typed stage, exact destination Scene mask, voice
     * slot, expected type, and an accepted-operation persistence decision.
     * Output: every selected Scene receives audio parameters and the active
     * Scene receives the existing ordered modulation-clear/reset/routing/Morph
     * apply. The public normal loader and the reversible `kit` restore both
     * use this one lifecycle path, so a restore cannot leave stale runtime
     * targets or differ from a file load. Only a root-pool load enables the
     * whole-Instrument AutoSave marker; temporary restore deliberately does
     * not manufacture a new retained-data mutation.
     *
     * Affiliates: preset_startInstrumentApply(),
     * preset_loadInstrumentTemp(), filesystem_loadedInstrumentSlot(),
     * and filesystem_instrumentLoadPreviewOriginal().
     *
     * S075: source_slot names the slot the image came from. Loads pass
     * source_slot == slot (unchanged behaviour); preset_startInstrumentCopy()
     * passes the resident source slot so `self` LFO selectors are moved to
     * the destination slot after the assignment below.
     */
    instrument_apply_active = 0u;
    if (!staged || slot >= INSTRUMENT_SLOT_COUNT ||
        staged->type >= INSTRUMENT_TYPE_UNKNOWN ||
        staged->type != expected_type) {
        return;
    }
    if ((destination_mask & (uint16_t)(1u << scene_getActiveIndex())) != 0u)
        instrumentManager_clearAllRuntimeModulationTargets();
    for (target_scene_index = 0u;
         target_scene_index < SCENE_COUNT && target_scene_index < 16u;
         target_scene_index++) {
        scene_t *scene;

        if ((destination_mask & (uint16_t)(1u << target_scene_index)) == 0u)
            continue;
        scene = scene_get(target_scene_index);
        if (!scene)
            continue;
        /*
         * Publish this committed destination before deriving its AutoSave map.
         *
         * Inputs: a valid Scene selected by the immutable accepted Instrument
         * request and its existing resident kit storage. Output: an otherwise
         * absent physical Scene becomes Bank-present before the assignment and
         * whole-Instrument marker below; an already-present Scene is unchanged.
         * Why: AutoSave deliberately maps only Bank-present Scene payloads, so
         * relying solely on the earlier asynchronous completion notification
         * leaves a timing-sensitive gap in which a valid Instrument commit can
         * be real but its complete payload has no map and is silently skipped.
         * This is the commit's authoritative retained-data boundary, and the
         * Bank setter itself records its changed present-mask bytes when needed.
         * Affiliates: preset_markRequestedScenesPresentOnSuccessfulLoad(),
         * autosave_scenePayloadBase(), and autosave_markWholeInstrumentDirty().
         */
        bank_setScenePresentMask((uint16_t)(bank_scenePresentMask() |
                                            (uint16_t)(1u << target_scene_index)));
        scene->kit.instruments[slot] = *staged;
        /*
         * S075: an Instrument copied from another slot keeps "self" LFO
         * targets pointing at itself. Kit Save writes `self` for a selector
         * equal to the own slot; a resident copy applies the same rule by
         * moving source_slot+1 selectors to slot+1 in the Normal, Morph and
         * interpolation images before the runtime binds them. Loads pass
         * source_slot == slot and are unchanged.
         */
        if (source_slot != slot && source_slot < INSTRUMENT_SLOT_COUNT)
            preset_retargetSelfLfoVoice(&scene->kit.instruments[slot],
                                        (uint8_t)(source_slot + 1u),
                                        (uint8_t)(slot + 1u));
        /*
         * Option 2: a root Instrument Load or reversible kit restore replaces
         * the resident instrument image from a source other than the exact Bank
         * child, so this Scene's card-clean bit must clear. Direct assignment
         * bypasses preset_storeInstrumentEndpoint(), hence the explicit call.
         */
        bank_invalidateSdCleanScene(target_scene_index);
        {
            uint8_t trace_flags = mark_autosave_whole_instrument
                ? AUTOSAVE_TRACE_INSTRUMENT_COMMIT_FLAG_REQUESTED : 0u;
            uint32_t trace_value =
                ((uint32_t)target_scene_index <<
                 AUTOSAVE_TRACE_INSTRUMENT_COMMIT_SCENE_SHIFT) |
                ((uint32_t)slot <<
                 AUTOSAVE_TRACE_INSTRUMENT_COMMIT_SLOT_SHIFT) |
                ((uint32_t)staged->type <<
                 AUTOSAVE_TRACE_INSTRUMENT_COMMIT_TYPE_SHIFT);

            /*
             * Witness the decision immediately beside the committed owner.
             * Inputs: Menu's captured provenance decision and this loop's
             * exact destination. Output: one existing RAM trace record says
             * whether a whole-marker was requested and actually called; no
             * persistence bit, SceneData value, or runtime ordering changes.
             * Why: I records diagnose inside the marker, but only this point
             * can prove a completed root Instrument reached that marker gate.
             * Affiliates: Menu's completion classifier and Autosave's I event.
             */
            if (mark_autosave_whole_instrument) {
                autosave_markWholeInstrumentDirty(target_scene_index, slot);
                trace_flags |= AUTOSAVE_TRACE_INSTRUMENT_COMMIT_FLAG_CALLED;
            }
            autosaveTrace_record(AUTOSAVE_TRACE_STAGE_INSTRUMENT_COMMIT,
                                 trace_flags, trace_value);
        }
        /*
         * Do not attach a filename or display-name copy to the resident slot.
         *
         * Why: filesystem has already placed the successful eight-cell name in
         * the operation identity block for its targeted HCNAMES update. Inputs:
         * validated staged descriptor and destination coordinates. Output:
         * audio data only; name authority remains `/.hcnames`.
         * Affiliates: filesystem_loadedInstrumentSlot(), HCNAMES Instrument
         * writer, and Menu's Instrument session exit.
         */
        if (target_scene_index == scene_getActiveIndex())
            active_scene_touched = 1u;
    }

    if (!active_scene_touched)
        return;

    instrumentManager_resetRuntimeSlot(slot);
    (void)preset_applyKitAudioRouting(scene_getActiveIndex(), slot);
    preset_ensureMorphInitialized();
    presetMorph_requestAll(scene_getActiveIndex());
    instrument_apply_active = 1u;
    instrument_apply_scene = scene_getActiveIndex();
    instrument_apply_phase = INSTRUMENT_APPLY_PHASE_MORPH_REBUILD;
    instrument_apply_rebind_source = 0u;
    instrument_apply_morph_only = 0u;
}

void preset_startInstrumentApply(uint8_t scene_index,
                                 uint8_t slot,
                                 uint8_t mark_autosave_whole_instrument)
{
    const kit_instrument_slot_t *staged =
        (const kit_instrument_slot_t *)filesystem_loadedInstrumentSlot();
    uint16_t destination_mask = pm_kit_request_scene_mask
        ? pm_kit_request_scene_mask
        : (uint16_t)(1u << scene_index);

    /*
     * Apply the validated filesystem Instrument candidate through the shared
     * image commit helper.
     *
     * Inputs: callback-captured Scene/slot plus Preset's immutable request
     * type/mask plus Menu's immutable root-pool versus temporary-restore
     * decision. Output: the Instrument result commits through the same safe
     * runtime ordering, while only a root-pool replacement marks its complete
     * retained Instrument payload for AutoSave. Affiliates:
     * on_instrument_load_complete() and Menu completion polling.
     */
    preset_startInstrumentApplyImage(staged, destination_mask, slot, slot,
                                     (instrument_type_t)pm_instrument_request_type,
                                     mark_autosave_whole_instrument);
}

/*
 * Commit one resident Instrument slot onto a slot in a set of Scenes (S075).
 *
 * Contract in presetManager.h. The source image is read live from the
 * resident source Scene (user: source reads are live); it is never modified
 * because a destination equal to the source coordinates assigns identical
 * bytes and the `self` retarget only touches destination copies.
 */
void preset_startInstrumentCopy(uint8_t src_scene, uint8_t src_slot,
                                uint16_t dst_mask, uint8_t dst_slot)
{
    const scene_t *source = scene_getConst(src_scene);

    if (!source || src_slot >= INSTRUMENT_SLOT_COUNT ||
        dst_slot >= INSTRUMENT_SLOT_COUNT || dst_mask == 0u)
        return;
    preset_startInstrumentApplyImage(&source->kit.instruments[src_slot],
                                     dst_mask, dst_slot, src_slot,
                                     source->kit.instruments[src_slot].type,
                                     1u);
}

uint8_t preset_saveInstrumentTemp(uint8_t source_scene, uint8_t source_slot)
{
    /*
     * Save one voice as the hidden reversible normal-Load source.
     *
     * Inputs: Menu's entry Scene/voice. Output: Preset holds the immutable
     * coordinates until filesystem writes `.hctmp.<ext>`; completion is a
     * dedicated UI sequencing event, not an Instrument Save or name update.
     * Affiliates: on_instrument_temp_save_complete() and Menu entry handling.
     */
    filesystem_ack();
    if (!filesystem_requestSaveInstrumentTemp(source_scene, source_slot,
                                              on_instrument_temp_save_complete))
        return 0u;
    pm_status = PRESET_LOAD_IN_PROGRESS;
    pm_completed_op = PRESET_OP_NONE;
    pm_request_slot = source_slot;
    pm_instrument_request_scene = source_scene;
    pm_instrument_request_slot = source_slot;
    return 1u;
}

uint8_t preset_saveInstrumentMorphTemp(uint8_t source_scene,
                                       uint8_t source_slot)
{
    /*
     * Save the entry Morph endpoints for the reversible InstrumentMrp row.
     *
     * Inputs: Menu's entry Scene/voice. Output: the existing hidden temporary
     * filename receives a Morph-only projection; no normal image is staged or
     * persisted as the restore contract. Completion is UI sequencing only.
     */
    filesystem_ack();
    if (!filesystem_requestSaveInstrumentMorphTemp(
            source_scene, source_slot, on_instrument_morph_temp_save_complete))
        return 0u;
    pm_status = PRESET_LOAD_IN_PROGRESS;
    pm_completed_op = PRESET_OP_NONE;
    pm_request_slot = source_slot;
    pm_instrument_request_scene = source_scene;
    pm_instrument_request_slot = source_slot;
    return 1u;
}

uint8_t preset_loadInstrumentTemp(uint8_t destination_scene,
                                  uint8_t destination_slot,
                                  instrument_type_t type)
{
    /*
     * Nothing loads into Scene 17 (S082 P1 R1, D36).
     *
     * What: a destination Scene outside the resident range (index 16) is
     * refused before any Preset or filesystem state changes.
     * Why: this loader takes a Scene index, and scene_get(16) is valid while
     * Scene 17 exists, so a stray caller could otherwise write into 17 (and a
     * 16-bit 1u << 16 destination mask would be undefined). The Menu passes
     * the home Scene (menu_instrumentEntryScene()); this is the backstop.
     * Inputs: destination_scene. Outputs: 0 (refused) for index >= SCENE_COUNT.
     * Affiliates: menu_instrumentEntryScene(), scene_backgroundHomeIndex().
     */
    if (destination_scene >= SCENE_COUNT)
        return 0u;
    /*
     * Route the direct `kit` row through the normal Instrument apply path.
     *
     * Inputs: the exact temporary-file Scene/voice/type context retained by
     * Menu. Output: the regular Instrument completion applies parsed data to
     * that voice, while filesystem suppresses HCNAMES publication for the
     * hidden file. Affiliates: on_instrument_load_complete() and Menu cursor.
     */
    filesystem_ack();
    if (!filesystem_requestLoadInstrumentTemp(destination_scene,
                                              destination_slot,
                                              type,
                                              on_instrument_load_complete))
        return 0u;
    pm_status = PRESET_LOAD_IN_PROGRESS;
    pm_completed_op = PRESET_OP_NONE;
    pm_request_slot = destination_slot;
    pm_instrument_request_scene = destination_scene;
    pm_instrument_request_slot = destination_slot;
    pm_instrument_request_type = type;
    return 1u;
}

uint8_t preset_loadInstrumentMorphTemp(uint8_t destination_scene,
                                       uint8_t destination_slot,
                                       instrument_type_t type)
{
    /*
     * Nothing loads into Scene 17 (S082 P1 R1, D36).
     *
     * What: a destination Scene outside the resident range (index 16) is
     * refused before any Preset or filesystem state changes.
     * Why: this loader takes a Scene index, and scene_get(16) is valid while
     * Scene 17 exists, so a stray caller could otherwise write into 17 (and a
     * 16-bit 1u << 16 destination mask would be undefined). The Menu passes
     * the home Scene (menu_instrumentEntryScene()); this is the backstop.
     * Inputs: destination_scene. Outputs: 0 (refused) for index >= SCENE_COUNT.
     * Affiliates: menu_instrumentEntryScene(), scene_backgroundHomeIndex().
     */
    if (destination_scene >= SCENE_COUNT)
        return 0u;
    /*
     * Load the hidden Morph-only InstrumentMrp baseline.
     *
     * Inputs: the exact destination Scene/slot/type retained by Menu. Output:
     * the existing Instrument stage receives the Morph snapshot and the
     * completion path performs the same-type Morph-only commit, preserving
     * type, Normal image, and HCNAMES identity.
     */
    filesystem_ack();
    if (!filesystem_requestLoadInstrumentMorphTemp(
            destination_scene, destination_slot, type,
            on_instrument_morph_temp_load_complete))
        return 0u;
    pm_status = PRESET_LOAD_IN_PROGRESS;
    pm_completed_op = PRESET_OP_NONE;
    pm_request_slot = destination_slot;
    pm_instrument_request_scene = destination_scene;
    pm_instrument_request_slot = destination_slot;
    pm_instrument_request_type = type;
    return 1u;
}

uint8_t preset_tickInstrumentApply(void)
{
    /*
     * Advance one bounded unit of the staged Instrument runtime transaction.
     *
     * Inputs are captured by preset_startInstrumentApply(). First, one Morph
     * descriptor per tick is reapplied across all six slots so no stale direct
     * runtime value survives graph clearing. Second, one source slot per tick
     * normalizes and reinstalls supplemental targets against the committed
     * descriptor graph. Output remains nonzero until both phases finish, which
     * keeps Menu's destination-changing controls locked throughout.
     */
    if (!instrument_apply_active)
        return 0u;
    if (instrument_apply_phase == INSTRUMENT_APPLY_PHASE_MORPH_REBUILD) {
        if (presetMorph_tick())
            return 1u;
        if (instrument_apply_morph_only) {
            instrument_apply_active = 0u;
            instrument_apply_morph_only = 0u;
            return 0u;
        }
        instrument_apply_phase = INSTRUMENT_APPLY_PHASE_TARGET_REBIND;
        instrument_apply_rebind_source = 0u;
        return 1u;
    }
    if (instrument_apply_rebind_source < INSTRUMENT_SLOT_COUNT) {
        /*
         * Rebuild every source rather than only the replaced destination.
         *
         * Any LFO/velocity owner may address the committed slot. Processing
         * one source per foreground pass bounds work while ensuring no stale
         * cross-slot pointer or mismatched loaded target pair survives.
         */
        preset_applyKitVoiceSupplemental(instrument_apply_scene,
                                         instrument_apply_rebind_source);
        instrument_apply_rebind_source++;
        return 1u;
    }
    instrument_apply_active = 0u;
    instrument_apply_morph_only = 0u;
    return 0u;
}
/* -----------------------------------------------------------------------
** preset_loadDrumset — post async kit load request.
** ----------------------------------------------------------------------- */
uint8_t preset_loadDrumset(uint16_t presetNr, uint8_t isMorph)
{
    fs_file_type_t type = isMorph ? FS_FILE_MORPH : FS_FILE_KIT;
    fs_completion_cb_t cb = isMorph ? on_morph_load_complete : on_kit_load_complete;
    uint8_t target_scene = scene_backgroundHomeIndex();

    filesystem_ack();
    pm_status = PRESET_LOAD_IN_PROGRESS;
    pm_completed_op = PRESET_OP_NONE;
    pm_request_slot = presetNr;
    pm_request_type = isMorph ? PRESET_REQUEST_LEGACY_MORPH : SAVE_TYPE_KIT;
    /*
     * Kit Load destination on Scene 17 (S082 C3).
     *
     * What: the legacy Kit Load mask names the playing Scene's home Scene
     * while Scene 17 is active, because a 16-bit mask cannot hold bit 16 and
     * Kit Load never targets Scene 17 (S082 P1 D18).
     * Why: scene_getActiveIndex() returns 16 during a background Bank load;
     * shifting 1u by 16 and truncating to uint16_t would silently produce an
     * empty request.
     * Inputs: Scene 17 state and its captured home Scene.
     * Outputs: pm_kit_request_scene_mask and the filesystem request mask.
     * Accessors: Menu's legacy Kit Load path.
     * Affiliates: scene_backgroundHomeIndex(),
     * filesystem_requestLoadKitForScenes().
     */
    if (target_scene >= SCENE_COUNT)
        target_scene = 0u;
    pm_kit_request_scene_mask = (uint16_t)(1u << target_scene);
    if ((!isMorph && filesystem_requestLoadKitForScenes(
                         presetNr,
                         (uint16_t)(1u << target_scene), cb)) ||
        (isMorph && filesystem_requestLoad(type, presetNr, cb)))
        return 1;
    pm_status = PRESET_IDLE;
    return 0;
}

uint8_t preset_loadKitForScenes(uint16_t presetNr, uint16_t scene_mask)
{
    /*
     * Post the Scene-targeted Kit directory load used by current Load UI.
     *
     * Inputs: Kit browser slot and selected Scene bitmask. Output: asynchronous
     * filesystem work plus Preset completion context. This is separate from
     * preset_loadDrumset() because that legacy public function still accepts a
     * morph compatibility flag; callers that select Scenes need an explicit,
     * unambiguous mask rather than an overloaded boolean.
     *
     * pm_kit_request_scene_mask is captured before the asynchronous request so
     * on_kit_load_complete() can promote exactly the loaded destination Scenes
     * into BankData's resident-present mask after filesystem success.
     */
    filesystem_ack();
    pm_status = PRESET_LOAD_IN_PROGRESS;
    pm_completed_op = PRESET_OP_NONE;
    pm_request_slot = presetNr;
    pm_request_type = SAVE_TYPE_KIT;
    pm_kit_request_scene_mask = scene_mask;
    if (filesystem_requestLoadKitForScenes(presetNr, scene_mask,
                                           on_kit_load_complete))
        return 1u;
    pm_status = PRESET_IDLE;
    return 0u;
}

uint8_t preset_saveDrumset(uint16_t presetNr, uint8_t isMorph,
                           uint8_t source_scene)
{
    /*
     * Post normal Kit Save or KitMrp Save to the shared directory writer.
     *
     * Inputs are captured here because filesystem work is asynchronous: the
     * target slot, selected source Scene, editable eight-cell display name, and
     * Morph projection flag must remain fixed even if the user moves the panel
     * before the SD operation completes. The Morph flag is not a legacy flat
     * .SND request; it selects the new-format snapshot-twice projection inside
     * storage/filesystem.
     */
    filesystem_ack();
    pm_status = PRESET_LOAD_IN_PROGRESS;
    pm_completed_op = PRESET_OP_NONE;
    pm_request_slot = presetNr;
    pm_request_type = isMorph ? SAVE_TYPE_KIT_MORPH : SAVE_TYPE_KIT;
    /*
     * Reuse the existing single-Scene request coordinate for Kit Save.
     *
     * Input: source_scene accepted by the Save request. Output: Menu can read
     * the same immutable source after the physical Kit and `/Kit/.hcindex` are
     * durable, even though menu_resetSaveParameters() has already reset its UI
     * Scene selection. No additional request-state byte is introduced.
     */
    pm_instrument_request_scene = source_scene;
    if (filesystem_requestSaveKitDirectory(presetNr,
                                           source_scene,
                                           preset_currentName,
                                           isMorph,
                                           isMorph ? on_kit_morph_save_complete
                                                   : on_kit_save_complete)) {
        return 1u;
    }
    pm_status = PRESET_IDLE;
    return 0u;
}

uint8_t preset_loadKitMorphForScenes(uint16_t presetNr, uint16_t scene_mask)
{
    /*
     * Post a new-format Kit Morph load.
     *
     * Inputs: Kit browser slot and selected Scene mask. Output: filesystem
     * parses the same Kit/ directory used by normal Kit Load but stages only;
     * Preset later copies staged normal endpoint values into resident morph
     * endpoints for matching instrument types. Mismatched slots are no-change
     * by design, so KitMrp remains per-instrument instead of replacing kit
     * membership.
     */
    filesystem_ack();
    pm_status = PRESET_LOAD_IN_PROGRESS;
    pm_completed_op = PRESET_OP_NONE;
    pm_request_slot = presetNr;
    pm_request_type = SAVE_TYPE_KIT_MORPH;
    pm_kit_request_scene_mask = scene_mask;
    if (filesystem_requestLoadKitMorphForScenes(presetNr, scene_mask,
                                                on_kit_morph_load_complete))
        return 1u;
    pm_status = PRESET_IDLE;
    return 0u;
}

uint8_t preset_loadSceneForScenes(uint16_t presetNr, uint16_t scene_mask)
{
    /*
     * Post an explicit root Scene Load request.
     *
     * Inputs: root Scene library slot and destination Scene mask from Menu.
     * Output: nonzero only when filesystem accepts the staged multi-file
     * Scene load. Scene Load is separate from Kit Load because its completion
     * may replace settings, pattern, effect state, and the embedded Kit.
     */
    filesystem_ack();
    pm_status = PRESET_LOAD_IN_PROGRESS;
    pm_completed_op = PRESET_OP_NONE;
    pm_request_slot = presetNr;
    pm_request_type = SAVE_TYPE_SCENE;
    pm_kit_request_scene_mask = scene_mask;
    if (filesystem_requestLoadSceneForScenes(presetNr, scene_mask,
                                             on_scene_load_complete))
        return 1u;
    pm_status = PRESET_IDLE;
    return 0u;
}

uint8_t preset_saveScene(uint16_t presetNr, uint8_t source_scene)
{
    /*
     * Post a root Scene Save request.
     *
     * Inputs: target root Scene slot plus selected source Scene and edited
     * preset_currentName captured here. Output: filesystem-owned asynchronous
     * directory write. Scene Save is not a runtime mutation, so completion only
     * repaints/clears UI busy state.
     */
    filesystem_ack();
    pm_status = PRESET_LOAD_IN_PROGRESS;
    pm_completed_op = PRESET_OP_NONE;
    pm_request_slot = presetNr;
    pm_request_type = SAVE_TYPE_SCENE;
    /*
     * Retain the resident source independently of the root target slot.
     *
     * Input: accepted Scene Save coordinate. Output: the asynchronous DONE
     * callback can update exactly this Scene's provenance without consulting
     * later Menu selection. Why: pm_request_slot is the library destination,
     * not a resident index. Affiliates: on_scene_save_complete().
     */
    pm_instrument_request_scene = source_scene;
    if (filesystem_requestSaveSceneDirectory(presetNr,
                                             source_scene,
                                             preset_currentName,
                                             on_scene_save_complete)) {
        return 1u;
    }
    pm_status = PRESET_IDLE;
    return 0u;
}

uint8_t preset_loadBank(uint16_t presetNr, uint16_t act_mask,
                        uint16_t empty_mask, uint8_t flags)
{
    /*
     * Post a root Bank Load with explicit act/empty masks (S082 B3).
     *
     * Inputs: root Bank library slot, act_mask (Scenes loaded from child),
     * empty_mask (Scenes emptied by Replace), flags (PRESET_BANK_LOAD_BOOT for
     * the boot path, REPLACE for a stated runtime Replace and PROGRESSIVE for
     * an unlocked load, S082 P3). Output: nonzero when filesystem accepts the
     * Bank load.
     */
    filesystem_ack();
    pm_status = PRESET_LOAD_IN_PROGRESS;
    pm_completed_op = PRESET_OP_NONE;
    pm_request_slot = presetNr;
    pm_request_type = SAVE_TYPE_BANK;
    pm_kit_request_scene_mask = act_mask;
    if (filesystem_requestLoadBank(presetNr, act_mask, empty_mask,
                                   flags, on_bank_load_complete))
        return 1u;
    pm_status = PRESET_IDLE;
    return 0u;
}

uint8_t preset_saveBank(uint16_t presetNr, uint16_t act_mask,
                        uint16_t empty_mask)
{
    uint8_t source_scene = scene_backgroundHomeIndex();

    /*
     * Post a multi-Scene Bank Save with explicit act/empty masks (S082 B3).
     *
     * Inputs: root Bank slot, act_mask (resident Scenes written to their child),
     * empty_mask (children removed from the slot). Output: filesystem writes
     * bankset.bcg and one Bank-local child folder per act_mask bit.
     *
     * S082 C3/D20: while Scene 17 is active the manifest's active Scene is its
     * captured home Scene. Scene 17 has no Bank number and is never saved as
     * the active Scene.
     */
    filesystem_ack();
    pm_status = PRESET_LOAD_IN_PROGRESS;
    pm_completed_op = PRESET_OP_NONE;
    pm_request_slot = presetNr;
    pm_request_type = SAVE_TYPE_BANK;
    pm_kit_request_scene_mask = act_mask;
    if (filesystem_requestSaveBank(presetNr,
                                   source_scene,
                                   preset_currentName,
                                   act_mask,
                                   empty_mask,
                                   0u,
                                   on_bank_save_complete)) {
        return 1u;
    }
    pm_status = PRESET_IDLE;
    return 0u;
}

uint8_t preset_completedBankLoadedScene(void)
{
    return filesystem_lastBankLoadLoadedScene();
}

uint16_t preset_bankLoadFailedSceneMask(void)
{
    return pm_bank_load_failed_scene_mask;
}

/*
 * Count the pool chunks one source track needs in Scene 17 (S082 C5).
 *
 * What: reads every step block of the track and sums its encoded size in
 * four-byte chunks. Why: the per-track build must decide whether the whole
 * track fits before publishing any of it (D3). Inputs: source Scene/track.
 * Outputs: required chunks. Accessors: preset_copyBackgroundTrack().
 * Affiliates: pat_rawReadBlock(), pat_rawBlockBytes().
 */
static uint16_t preset_backgroundTrackChunks(uint8_t src_scene, uint8_t track)
{
    uint8_t block[PAT_RAW_BLOCK_MAX];
    uint16_t entry;
    uint16_t chunks = 0u;
    uint8_t step;

    for (step = 0u; step < NUM_STEPS; step++) {
        uint8_t bytes = pat_rawReadBlock(src_scene, track, step, block,
                                         &entry);

        chunks = (uint16_t)(chunks + (uint16_t)(bytes / 4u));
    }
    return chunks;
}

/*
 * Empty one Scene 17 track before a rebuild copies into it (S082 P1 D30).
 *
 * What: publishes every step of the track empty (trigger off), which frees
 * its old blocks in the background region.
 * Why: a rebuild candidate's old Scene 17 blocks must neither count against
 * the fit check nor be played when the track does not fit (D30: it is left
 * empty).
 * Inputs: track 0..6. Outputs: Scene 17 track empty, pool chunks freed.
 * Context: foreground; the track plays a resident Scene, not 16, so the ISR
 * does not read these entries.
 * Accessors: preset_buildBackgroundScene() (rebuild only).
 * Affiliates: pat_rawPublishEmpty(), pat_rawFreeChunks().
 */
static void preset_clearBackgroundTrack(uint8_t track)
{
    uint8_t step;

    for (step = 0u; step < NUM_STEPS; step++)
        pat_rawPublishEmpty(SCENE_BACKGROUND_INDEX, track, step,
                            PAT_RAW_TRIGGER_OFF);
}

/*
 * Copy one source track's Pattern data into Scene 17 (S082 C5).
 *
 * What: copies the track's step blocks and block-less triggers, then the
 * track parameters, MIDI assignment and Morph endpoints. A track whose blocks
 * do not fit in the background region's free pool is left empty (D3); a
 * placement failure during publication publishes every step empty again so a
 * failed track never keeps a partial copy.
 * Why: Scene 17 must sound exactly like the per-track mix that is playing
 * (D4), without the shared source region. Inputs: track and its played Scene.
 * Outputs: 1 when the track is copied, 0 when it is left empty.
 * Accessors: preset_buildBackgroundScene().
 * Affiliates: pat_rawReadBlock(), pat_rawPlace(), pat_rawPublishEmpty(),
 * pat_rawFreeChunks(), scene_get(), scene_getConst().
 */
static uint8_t preset_copyBackgroundTrack(uint8_t track, uint8_t src_scene)
{
    uint8_t block[PAT_RAW_BLOCK_MAX];
    uint16_t entry;
    uint8_t step;
    uint8_t clear_step;
    uint8_t bytes;
    uint16_t needed;
    const pat_scene_region_t *src = pat_sceneRegion(src_scene);
    pat_scene_region_t *dst = pat_sceneRegionMut(SCENE_BACKGROUND_INDEX);
    scene_t *dst_scene = scene_get(SCENE_BACKGROUND_INDEX);
    const scene_t *src_scene_data = scene_getConst(src_scene);
    uint8_t bit = (uint8_t)(1u << track);

    if (!src || !dst || !dst_scene || !src_scene_data)
        return 0u;
    needed = preset_backgroundTrackChunks(src_scene, track);
    if (needed > pat_rawFreeChunks(SCENE_BACKGROUND_INDEX))
        return 0u;
    for (step = 0u; step < NUM_STEPS; step++) {
        bytes = pat_rawReadBlock(src_scene, track, step, block, &entry);
        if (bytes != 0u) {
            uint8_t trigger = (uint8_t)((entry & PAT_ADDR_TRIGGER_BIT)
                                            ? PAT_RAW_TRIGGER_ON
                                            : PAT_RAW_TRIGGER_OFF);

            if (!pat_rawPlace(SCENE_BACKGROUND_INDEX, track, step, block,
                              trigger)) {
                for (clear_step = 0u; clear_step < NUM_STEPS; clear_step++)
                    pat_rawPublishEmpty(SCENE_BACKGROUND_INDEX, track,
                                        clear_step,
                                        PAT_RAW_TRIGGER_OFF);
                return 0u;
            }
        } else if ((entry & PAT_ADDR_TRIGGER_BIT) != 0u) {
            pat_rawPublishEmpty(SCENE_BACKGROUND_INDEX, track, step,
                                PAT_RAW_TRIGGER_ON);
        }
    }
    dst->track_length[track] = src->track_length[track];
    dst->track_scale[track] = src->track_scale[track];
    dst->track_shuffle[track] = src->track_shuffle[track];
    dst->track_play_mode[track] = src->track_play_mode[track];
    dst->track_loop_exclude = (uint8_t)((dst->track_loop_exclude & ~bit) |
                                        (src->track_loop_exclude & bit));
    dst->track_auto_smooth = (uint8_t)((dst->track_auto_smooth & ~bit) |
                                       (src->track_auto_smooth & bit));
    dst_scene->settings.midi_channel[track] =
        src_scene_data->settings.midi_channel[track];
    dst_scene->settings.midi_note[track] =
        src_scene_data->settings.midi_note[track];
    dst_scene->settings.track_morph_length[track] =
        src_scene_data->settings.track_morph_length[track];
    dst_scene->settings.track_morph_scale[track] =
        src_scene_data->settings.track_morph_scale[track];
    dst_scene->settings.track_morph_shuffle[track] =
        src_scene_data->settings.track_morph_shuffle[track];
    return 1u;
}

/*
 * Copy one voice slot's Scene data and its captured Instrument name (D29)
 * into Scene 17 (S082 C5).
 *
 * What: copies the Instrument image, voice Morph amount, audio routing, FX
 * send endpoints, fader mode and (for the shared slot 6) the Kit-level
 * generated decay endpoints from the played Scene.
 * Why: a per-track Scene 17 must reproduce each slot's voice exactly (D4).
 * Inputs: destination slot, source Scene, source slot.
 * Outputs: Scene 17's slot data updated.
 * Accessors: preset_buildBackgroundScene().
 * Affiliates: scene_get(), scene_getConst(),
 * filesystem_backgroundNameCaptureInstrument().
 */
static void preset_copyBackgroundSlot(uint8_t dst_slot, uint8_t src_scene,
                                      uint8_t src_slot)
{
    scene_t *dst = scene_get(SCENE_BACKGROUND_INDEX);
    const scene_t *src = scene_getConst(src_scene);

    if (!dst || !src || dst_slot >= INSTRUMENT_SLOT_COUNT ||
        src_slot >= INSTRUMENT_SLOT_COUNT)
        return;
    dst->kit.instruments[dst_slot] = src->kit.instruments[src_slot];
    dst->settings.voice_morph_amount[dst_slot] =
        src->settings.voice_morph_amount[src_slot];
    dst->settings.audio_out[dst_slot] = src->settings.audio_out[src_slot];
    dst->settings.fx_send_amount[dst_slot] =
        src->settings.fx_send_amount[src_slot];
    dst->settings.fx_send_morph[dst_slot] =
        src->settings.fx_send_morph[src_slot];
    dst->settings.fader_setting[dst_slot] =
        src->settings.fader_setting[src_slot];
    if (dst_slot == INSTRUMENT_SLOT_COUNT - 1u) {
        dst->kit.settings.slot6_track7_amp_envelope_decay =
            src->kit.settings.slot6_track7_amp_envelope_decay;
        dst->kit.settings.slot6_track7_morph_amp_envelope_decay =
            src->kit.settings.slot6_track7_morph_amp_envelope_decay;
    }
    /*
     * The slot's name follows its data (S082 P1 D29).
     *
     * What: the captured Instrument name of dst_slot is copied from the same
     * resident Scene and slot as the image. Why: per-track playback mixes
     * slots from several Scenes (D2); the name must match the voice.
     * Inputs: dst_slot, src_scene, src_slot. Outputs: one capture row.
     * Affiliates: filesystem_backgroundNameCaptureInstrument().
     */
    filesystem_backgroundNameCaptureInstrument(dst_slot, src_scene, src_slot);
}

static uint8_t preset_backgroundCandidateTracks(uint16_t replaced);

/*
 * Build, or rebuild in place, Scene 17 from what is playing
 * (S082 P1 D2-D4, D9, D19, D29-D31).
 *
 * What: fresh build: copies the active Scene's Scene-level state (settings
 * without chain, Effect, FX sequence, bus compressor, Morph) and all its
 * captured names, then either the whole Pattern region (no per-track
 * overrides) or, per track in T1..T7 order, the track and its voice slot from
 * the track's played Scene. Rebuild in place: only the tracks whose played
 * resident Scene the load replaces are emptied and then copied; Scene 17's
 * Scene-level state and every other track stay. A track that does not fit is
 * left empty and later tracks are still tried (D3, D30). T6/T7's shared slot
 * follows T6 when T6 fits.
 * Why: Scene 17 must sound exactly like what is playing (D4), so the switch
 * changes only the read address (C6); a rebuild must not disturb tracks the
 * load does not touch (D31).
 * Inputs: rebuild_in_place, replaced_mask (act | empty), active Scene,
 * seq_perTrackPattern[], resident Scenes, pool capacity, HCNAMES mirror.
 * Outputs: scene_background, pat_background_region, the name capture;
 * returns the mask of tracks copied.
 * Context: foreground, before the switch or the per-track move; no written
 * track is read by the ISR yet.
 * Accessors: preset_tickBackgroundBankLoad() phase 3.
 * Affiliates: preset_backgroundCandidateTracks(), preset_clearBackgroundTrack(),
 * preset_copyBackgroundTrack(), preset_copyBackgroundSlot(),
 * pat_snapshotScene(), filesystem_backgroundNamesCaptureScene(),
 * scene_backgroundBegin().
 */
uint8_t preset_buildBackgroundScene(uint8_t rebuild_in_place,
                                    uint16_t replaced_mask)
{
    uint8_t active = scene_getActiveIndex();
    uint8_t copied = 0u;
    uint8_t track;
    uint8_t candidates;
    scene_t *dst;
    const scene_t *src;

    if (active >= SCENE_COUNT)
        active = scene_backgroundHomeIndex();
    if (active >= SCENE_COUNT)
        return 0u;
    if (!scene_backgroundExists())
        rebuild_in_place = 0u;
    if (!rebuild_in_place)
        scene_backgroundBegin(active);
    dst = scene_get(SCENE_BACKGROUND_INDEX);
    src = scene_getConst(active);
    if (!dst || !src) {
        if (!rebuild_in_place)
            scene_backgroundRelease();
        return 0u;
    }
    if (!rebuild_in_place) {
        *dst = *src;
        /*
         * D9b: Scene 17 has no chain of its own. A chain evaluated while a
         * background load runs must never fire a queued Scene change.
         */
        dst->settings.chain_condition[0] = 0u;
        dst->settings.chain_condition[1] = 0u;
        dst->settings.chain_next[0] = 0u;
        dst->settings.chain_next[1] = 0u;
        filesystem_backgroundNamesCaptureScene(active);
        if (!seq_perTrackActive) {
            pat_snapshotScene(active);
            return 0x7Fu;
        }
        pat_rawRegionReset(SCENE_BACKGROUND_INDEX);
    }
    candidates = rebuild_in_place
        ? preset_backgroundCandidateTracks(replaced_mask) : 0x7Fu;
    for (track = 0u; track < NUM_TRACKS; track++) {
        uint8_t src_scene = seq_perTrackPattern[track];
        uint8_t bit = (uint8_t)(1u << track);

        if ((candidates & bit) == 0u)
            continue;                   /* D31: not affected by this load */
        if (src_scene >= SCENE_COUNT)
            src_scene = active;         /* fresh build only (no track on 16) */
        if (rebuild_in_place)
            preset_clearBackgroundTrack(track);  /* D30: free, then fit */
        if (!preset_copyBackgroundTrack(track, src_scene))
            continue;
        copied = (uint8_t)(copied | bit);
        if (track < INSTRUMENT_SLOT_COUNT - 1u) {
            preset_copyBackgroundSlot(track, src_scene, track);
        } else if (track == INSTRUMENT_SLOT_COUNT - 1u) {
            preset_copyBackgroundSlot(track, src_scene, track);
        } else if ((copied & (uint8_t)(1u <<
                                       (INSTRUMENT_SLOT_COUNT - 1u))) == 0u) {
            preset_copyBackgroundSlot((uint8_t)(INSTRUMENT_SLOT_COUNT - 1u),
                                      src_scene,
                                      (uint8_t)(INSTRUMENT_SLOT_COUNT - 1u));
        }
    }
    if (!rebuild_in_place) {
        pat_scene_region_t *dst_region =
            pat_sceneRegionMut(SCENE_BACKGROUND_INDEX);
        const pat_scene_region_t *src_region = pat_sceneRegion(active);

        if (dst_region && src_region) {
            dst_region->pattern_change_bar = src_region->pattern_change_bar;
            dst_region->pattern_next = src_region->pattern_next;
        }
    }
    return copied;
}

/*
 * Tracks whose played resident Scene a Bank load replaces (S082 P1 D31).
 *
 * What: bit t is set when track t plays a resident Scene (0..15) that is in
 * the load's replaced mask (act | empty).
 * Why: one rule for "a played Scene is replaced" (D25) and for which tracks a
 * rebuild in place copies into Scene 17 (D31). Tracks on Scene 17 and tracks
 * on unaffected resident Scenes are never candidates.
 * Inputs: replaced mask, seq_perTrackPattern[]. Outputs: 7-bit track mask.
 * Accessors: preset_backgroundScene17Needed(), preset_buildBackgroundScene(),
 * preset_tickBackgroundBankLoad() phase 3.
 * Affiliates: seq_perTrackPattern[], SCENE_COUNT.
 */
static uint8_t preset_backgroundCandidateTracks(uint16_t replaced)
{
    uint8_t track;
    uint8_t mask = 0u;

    for (track = 0u; track < NUM_TRACKS; track++) {
        uint8_t played = seq_perTrackPattern[track];

        if (played < SCENE_COUNT &&
            (replaced & (uint16_t)(1u << played)) != 0u)
            mask = (uint8_t)(mask | (uint8_t)(1u << track));
    }
    return mask;
}

/*
 * Decide whether a Bank load needs Scene 17 (S082 P1 D2-D5, D14, D25, D31).
 *
 * What: Scene 17 is needed when the transport runs and the load replaces or
 * empties the active resident Scene or any track's played resident Scene.
 * While Scene 17 is already active it is needed only when a track still on a
 * resident Scene would lose that Scene (rebuild in place); otherwise the load
 * runs directly and Scene 17 is unchanged (D31).
 * Why: a played Scene's content must not disappear under the transport; a
 * load that touches only unplayed Scenes runs directly. The former
 * "Scene 17 is active -> always rebuild" pulled every track onto 17 (D31).
 * Inputs: replaced = act | empty, seq_isRunning(), active Scene,
 * seq_perTrackPattern[]. Output: 1 = build or rebuild, 0 = direct load.
 * Accessors: preset_tickBackgroundBankLoad() phase 1.
 * Affiliates: preset_backgroundCandidateTracks().
 */
static uint8_t preset_backgroundScene17Needed(uint16_t replaced)
{
    uint8_t active;

    if (!seq_isRunning())
        return 0u;
    active = scene_getActiveIndex();
    if (active < SCENE_COUNT &&
        (replaced & (uint16_t)(1u << active)) != 0u)
        return 1u;
    return (uint8_t)(preset_backgroundCandidateTracks(replaced) != 0u);
}

/*
 * Request a Bank load that keeps playing (S082 P1 §4, D1-D35).
 *
 * What: Menu origin: refused when the slot does not exist in the resident
 * Bank index (§4.1) or Preset is busy; otherwise accepted at once and advanced
 * by the tick. MIDI origin: dropped while a Bank load is in flight (D13);
 * otherwise held as the one pending request (latest wins, D33), which phase 0
 * starts when Preset, the facade and the Menu are idle, by entering
 * Load:[Bank] exactly as an OK would (D7, D32). A Menu request discards a
 * pending MIDI one (R5). The mode is stated by Menu (Merge flag); MIDI is
 * always Replace with all 16 (Q15).
 * Why: a Bank change must not interrupt what is playing (D1-D5); MIDI CC0
 * (S083) must reach the same path; a missing slot must be refused before any
 * Scene 17 work.
 * Inputs: slot, act/empty masks (Menu), origin | PRESET_BANK_REQUEST_MERGE.
 * Outputs: accepted (1) / refused or dropped (0); pm_bg_phase or
 * pm_bg_pending_slot.
 * Accessors: Menu Bank OK (`menu.c:14090`), S083 MIDI CC0.
 * Affiliates: preset_tickBackgroundBankLoad(), filesystem_bankSlotExists(),
 * menu_backgroundBankLoadUiBegin(), preset_backgroundBankLoadRunning().
 */
uint8_t preset_requestBackgroundBankLoad(uint16_t slot, uint16_t act_mask,
                                         uint16_t empty_mask, uint8_t origin)
{
    uint8_t merge;

    merge = (uint8_t)((origin & PRESET_BANK_REQUEST_MERGE) != 0u);
    origin = (uint8_t)(origin & (uint8_t)~PRESET_BANK_REQUEST_MERGE);
    if ((pm_bg_phase & PM_BG_PHASE_MASK) != 0u)
        return 0u;                  /* D13/D33: drop while a load is in flight */
    if (origin == PRESET_BANK_ORIGIN_MIDI) {
        if (slot >= STORAGE_BANK_MAX_SLOTS)
            return 0u;
        pm_bg_pending_slot = slot;  /* D33: latest wins; phase 0 starts it */
        return 1u;
    }
    if (pm_status != PRESET_IDLE)
        return 0u;
    if (!filesystem_bankSlotExists(slot))
        return 0u;                  /* §4.1: never build Scene 17 for nothing */
    pm_bg_pending_slot = PM_BG_PENDING_NONE;     /* R5 */
    pm_request_slot = slot;
    pm_kit_request_scene_mask = act_mask;
    (void)empty_mask;
    pm_bg_result = PRESET_BG_RESULT_NONE;
    pm_bg_phase = (uint8_t)(1u | (merge ? 0u : PM_BG_REPLACE));
    return 1u;
}

uint8_t preset_backgroundBankLoadRunning(void)
{
    return (uint8_t)((pm_bg_phase & PM_BG_PHASE_MASK) != 0u);
}

/*
 * Panel state of a background Bank load (S082 P3 D38, D40).
 *
 * What: Unlocked = phase 5 of a progressive load (the user may leave
 * Load/Save and work). Locked = any other running phase, or a stopped load
 * (the S082 P1 R7 panel drop still applies).
 * Why: the P1 gates dropped every panel action while a load ran; P3 keeps
 * that only for the locked part.
 * Inputs: pm_bg_phase. Outputs: boolean; single-byte read, ISR-safe.
 * Accessors: ButtonHandler MODE/SHIFT+REC, menu_switchPage(), Menu storage
 * gates, frozen Load:[Bank] screen, completion, the LED owner.
 * Affiliates: PM_BG_UNLOCKED, preset_backgroundBankLoadRunning().
 */
uint8_t preset_backgroundBankLoadUnlocked(void)
{
    return (uint8_t)((pm_bg_phase & PM_BG_UNLOCKED) != 0u);
}

uint8_t preset_backgroundBankLoadLocked(void)
{
    return (uint8_t)(preset_backgroundBankLoadRunning() &&
                     !preset_backgroundBankLoadUnlocked());
}

/*
 * Read and clear the last background Bank load result (S082 P1 D34).
 *
 * What: returns pm_bg_result and resets it to NONE.
 * Why: the origin of a load needs one completion signal; a read-and-clear
 * byte lets S083 send exactly one CC0 reply per load.
 * Inputs: pm_bg_result. Outputs: PRESET_BG_RESULT_* (| _MIDI).
 * Accessors: S083 MIDI service.
 * Affiliates: on_bank_load_complete(), preset_tickBackgroundBankLoad().
 */
uint8_t preset_takeBackgroundBankLoadResult(void)
{
    uint8_t result = pm_bg_result;

    pm_bg_result = PRESET_BG_RESULT_NONE;
    return result;
}

/*
 * Enter a background-load phase and stamp a bounded wait (S082 P1 A8).
 *
 * What: replaces the phase nibble, keeping the flag bits; entering a waiting
 * phase (2, 4 or 6) records time_sysTick in pm_bg_pending_slot, which holds
 * no request while a load is in flight (see its declaration).
 * Why: one place starts every wait clock, so no wait can run unbounded.
 * Inputs: phase. Outputs: pm_bg_phase, wait start.
 * Accessors: preset_tickBackgroundBankLoad().
 * Affiliates: preset_bgWaitExpired(), PM_BG_WAIT_LIMIT_MS.
 */
static void preset_bgSetPhase(uint8_t phase)
{
    pm_bg_phase = (uint8_t)((pm_bg_phase & (uint8_t)~PM_BG_PHASE_MASK) |
                            (phase & PM_BG_PHASE_MASK));
    if (phase == 2u || phase == 4u || phase == PM_BG_PHASE_MIDI_UI)
        pm_bg_pending_slot = time_sysTick;
}

/*
 * Report whether the current bounded wait has run out (S082 P1 A8).
 *
 * What: nonzero once PM_BG_WAIT_LIMIT_MS have passed since the waiting phase
 * was entered (wrap-safe 16-bit subtraction).
 * Inputs: time_sysTick, pm_bg_pending_slot (wait start).
 * Outputs: boolean. Accessors: phases 2, 4 and 6 of the tick.
 * Affiliates: preset_bgSetPhase(), preset_bgEnd().
 */
static uint8_t preset_bgWaitExpired(void)
{
    return (uint8_t)((uint16_t)(time_sysTick - pm_bg_pending_slot) >=
                     PM_BG_WAIT_LIMIT_MS);
}

/*
 * End a background Bank load (S082 P1 D34, A8).
 *
 * What: releases the AutoSave yield, returns to phase 0, restores the pending
 * register to PM_BG_PENDING_NONE and, for a failure, records FAILED (with the
 * MIDI flag for MIDI origin) and ends the accepted Load/Save command.
 * Why: every exit (completion, refusal, failure, timeout) must leave the same
 * clean state: no yield, no stale wait stamp mistaken for a pending slot, and
 * no command stuck on `...`. A failure after the Scene 17 switch keeps
 * playback on 17 (§4.5).
 * Inputs: failed, pm_bg_phase. Outputs: pm_bg_phase, pm_bg_pending_slot,
 * pm_bg_result, yield, Menu command state.
 * Accessors: preset_tickBackgroundBankLoad().
 * Affiliates: filesystem_setAutosaveYield(), menu_backgroundBankLoadAbort(),
 * bank_endLoadAvailability().
 */
static void preset_bgEnd(uint8_t failed)
{
    uint8_t midi = (uint8_t)((pm_bg_phase & PM_BG_ORIGIN_MIDI) != 0u);

    filesystem_setAutosaveYield(0u);
    /*
     * Close any availability session (S082 P3). What: idempotent backstop
     * for every exit, including failures before the filesystem accepted the
     * request. Affiliates: bank_endLoadAvailability(), on_bank_load_complete().
     */
    bank_endLoadAvailability();
    pm_bg_phase = 0u;
    pm_bg_pending_slot = PM_BG_PENDING_NONE;
    if (failed) {
        pm_bg_result = (uint8_t)(PRESET_BG_RESULT_FAILED |
                                 (midi ? PRESET_BG_RESULT_MIDI : 0u));
        menu_backgroundBankLoadAbort();
    }
}

/*
 * Advance the background Bank Load one bounded phase per poll pass
 * (S082 P1 §4, D1-D39).
 *
 * What: phase 0 starts a pending MIDI request when everything is idle; phases
 * 1-5 are: decide whether Scene 17 is needed; drop queued copy/clear work and
 * abort AutoSave, wait for the facade and pattern snapshot; build/rebuild and
 * switch/move (Scene 17 only); load the Bank; wait for completion. Phase 6
 * (MIDI origin): wait for the Load:[Bank] preview, then act as OK. Each of
 * the waits in phases 2, 4 and 6 is bounded by PM_BG_WAIT_LIMIT_MS; on expiry
 * the load ends FAILED (A8). The result is recorded for
 * preset_takeBackgroundBankLoadResult().
 * Inputs: pm_bg_phase, pm_request_slot, pm_kit_request_scene_mask.
 * Outputs: Scene 17 state or a direct load; preset status; pm_bg_result.
 * Accessors: menu_pollPresetStatus().
 * Affiliates: all preset_background* and preset_bg* helpers, ccSvc_busy(),
 * filesystem_status(), filesystem_patternSnapshotInUse(),
 * menu_backgroundBankLoadUiIdle(), menu_backgroundBankLoadUiPoll(),
 * menu_backgroundNamesChanged(), seq_moveTracksToBackgroundScene(),
 * preset_backgroundCandidateTracks().
 */
void preset_tickBackgroundBankLoad(void)
{
    uint8_t phase = (uint8_t)(pm_bg_phase & PM_BG_PHASE_MASK);
    uint16_t act = pm_kit_request_scene_mask;
    uint16_t empty = (pm_bg_phase & PM_BG_REPLACE)
        ? (uint16_t)~act : 0u;

    /*
     * Start a deferred MIDI request once everything is idle (D33, D7, D32).
     *
     * What: with no load in flight, a pending MIDI slot is started when
     * Preset, the facade and the Menu are idle: the UI is taken to
     * Load:[Bank] at that slot (abandoning any Menu state) and phase 6 waits
     * for its preview, exactly as a user would before OK.
     * Inputs: pm_bg_pending_slot, menu_backgroundBankLoadUiIdle().
     * Outputs: phase 6 with MIDI origin and Replace; pending cleared (the
     * register then holds the phase 6 wait start).
     */
    if (phase == 0u) {
        if (pm_bg_pending_slot == PM_BG_PENDING_NONE ||
            pm_status != PRESET_IDLE || !menu_backgroundBankLoadUiIdle())
            return;
        pm_request_slot = pm_bg_pending_slot;
        pm_bg_pending_slot = PM_BG_PENDING_NONE;
        pm_bg_result = PRESET_BG_RESULT_NONE;
        menu_backgroundBankLoadUiBegin(pm_request_slot);
        pm_bg_phase = (uint8_t)(PM_BG_ORIGIN_MIDI | PM_BG_REPLACE);
        preset_bgSetPhase(PM_BG_PHASE_MIDI_UI);
        return;
    }

    switch (phase) {
    case 1u: {
        uint16_t replaced = (uint16_t)(act | empty);

        /*
         * Decide Scene 17 once (D2-D5, D14, D25, D31). Every request then
         * drops background work (phase 2: D12 copy/clear, D1/R6 AutoSave
         * aborted, card idle); only a Scene 17 request builds.
         */
        if (preset_backgroundScene17Needed(replaced))
            pm_bg_phase |= PM_BG_NEEDED;
        preset_bgSetPhase(2u);
        return; }
    case 2u:
        if ((pm_bg_phase & PM_BG_DISCARDED) == 0u) {
            ccSvc_dropQueued();
            filesystem_setAutosaveYield(1u);
        }
        if (ccSvc_busy() || filesystem_status() == FS_STATUS_BUSY ||
            filesystem_patternSnapshotInUse()) {
            /* A8: nothing has been built yet; a timeout just fails. */
            if (preset_bgWaitExpired())
                preset_bgEnd(1u);
            return;
        }
        /*
         * Drop the outgoing Bank's pending AutoSave state (D1, D25, R6).
         *
         * What: the facade is idle (any drain aborted and closed), so a
         * Replace discards the dirty mask on every path, with or without
         * Scene 17; a Merge keeps it and Menu re-marks the whole Bank after
         * the load. Then build (Scene 17 needed) or load directly.
         */
        if ((pm_bg_phase & PM_BG_DISCARDED) == 0u) {
            if ((pm_bg_phase & PM_BG_REPLACE) != 0u)
                autosave_discardDirtyMask();
            pm_bg_phase |= PM_BG_DISCARDED;
        }
        preset_bgSetPhase((pm_bg_phase & PM_BG_NEEDED) ? 3u : 4u);
        return;
    case 3u: {
        /*
         * Build and switch, or rebuild and move only the affected tracks
         * (D2-D5, D19, D27-D31).
         *
         * What: fresh build: build Scene 17 and switch every track to it;
         * the view follows only with PAR_FOLLOW on (D27). Rebuild in place:
         * copy the candidate tracks and move only them (D31); the active
         * Scene is already 17 and the view is never moved (D28). Either way
         * the Menu drops any stale Scene 17 name scratch (D29).
         */
        uint16_t replaced = (uint16_t)(act | empty);
        uint8_t rebuild = scene_backgroundExists();

        if (!preset_buildBackgroundScene(rebuild, replaced) &&
            !scene_backgroundExists()) {
            preset_bgEnd(1u);
            return;
        }
        if (rebuild) {
            seq_moveTracksToBackgroundScene(
                preset_backgroundCandidateTracks(replaced));
        } else {
            seq_switchToBackgroundScene();
            (void)scene_selectActive(SCENE_BACKGROUND_INDEX);
            /*
             * The sounding Effect follows playback onto Scene 17
             * (S082 P2 fix 1).
             *
             * What: retargets EffectsManager's per-block source from the home
             * Scene to Scene 17 without touching the running Effect; a type
             * mismatch (never expected: 17 is a copy of the active Scene)
             * falls back to the full activation.
             * Why: the Bank load is about to overwrite the home Scene's Effect
             * record; the Effect must keep reading the identical copy, or it
             * changes level and parameters mid-load (S082_P1 D4).
             * Inputs: Scene 17 built and active. Outputs: effects_state source.
             * Affiliates: effects_rebindSourceScene(), effects_activateScene(),
             * preset_buildBackgroundScene().
             */
            if (!effects_rebindSourceScene(SCENE_BACKGROUND_INDEX))
                effects_activateScene(SCENE_BACKGROUND_INDEX);
            menu_setPlayedPattern(SCENE_BACKGROUND_INDEX);
            if (parameter_values[PAR_FOLLOW])
                menu_setShownPattern(SCENE_BACKGROUND_INDEX);
        }
        menu_backgroundNamesChanged();
        menu_refreshPerfSceneLeds();
        preset_bgSetPhase(4u);
        return; }
    case 4u: {
        /*
         * Post the ordinary two-mask Bank Load; wait while the card is busy
         * (P1-5). Decide the unlock (S082 P3 D40).
         *
         * What: a refusal while the facade is BUSY is retried next pass, for
         * at most PM_BG_WAIT_LIMIT_MS (A8); a refusal on an idle facade is a
         * real failure. The load is progressive, and the panel unlocks at
         * phase 5, when Scene 17 carries playback, or when the transport runs
         * and no played Scene is replaced (a direct D25/D31 load). A load
         * requested stopped keeps the P1 lock: it replaces the active Scene in
         * place and runs fast with the codec suspended.
         * Why: the unlock must start exactly when the filesystem has accepted
         * the request and BankData has marked the affected Scenes unavailable,
         * never earlier.
         * Inputs: act/empty, PM_BG_NEEDED, seq_isRunning(),
         * preset_backgroundScene17Needed().
         * Outputs: phase 5 (+ PM_BG_UNLOCKED), or failure.
         * Affiliates: preset_loadBank(), FS_BANK_FLAG_PROGRESSIVE.
         */
        uint8_t progressive = (uint8_t)(
            (pm_bg_phase & PM_BG_NEEDED) != 0u ||
            (seq_isRunning() &&
             !preset_backgroundScene17Needed((uint16_t)(act | empty))));
        uint8_t flags = (uint8_t)(
            ((pm_bg_phase & PM_BG_REPLACE) ? PRESET_BANK_LOAD_REPLACE : 0u) |
            (progressive ? PRESET_BANK_LOAD_PROGRESSIVE : 0u));

        if (preset_loadBank(pm_request_slot, act, empty, flags)) {
            preset_bgSetPhase(5u);
            if (progressive)
                pm_bg_phase |= PM_BG_UNLOCKED;
        } else if (filesystem_status() != FS_STATUS_BUSY ||
                   preset_bgWaitExpired()) {
            preset_bgEnd(1u);
        }
        return; }
    case 5u:
        if (pm_status == PRESET_IDLE)
            preset_bgEnd(0u);
        return;
    case PM_BG_PHASE_MIDI_UI: {
        /*
         * MIDI origin: wait for the Load:[Bank] preview, then act as OK
         * (D7, §4.1, Q15).
         *
         * What: the Menu reports WAIT while the Bank index or child preview
         * runs; READY with the Replace/all-16 masks it would compute on OK;
         * REFUSED when the slot is missing (the page shows it as Menu would)
         * or the preview failed. READY continues at phase 1; REFUSED, or a
         * WAIT longer than PM_BG_WAIT_LIMIT_MS (A8), ends the command with a
         * FAILED result.
         */
        uint16_t a = 0u, e = 0u;
        uint8_t ui = menu_backgroundBankLoadUiPoll(pm_request_slot, &a, &e);

        if (ui == MENU_BG_UI_WAIT) {
            if (preset_bgWaitExpired())
                preset_bgEnd(1u);
            return;
        }
        if (ui == MENU_BG_UI_REFUSED) {
            preset_bgEnd(1u);
            return;
        }
        pm_kit_request_scene_mask = a;
        preset_bgSetPhase(1u);
        return; }
    default:
        preset_bgEnd(0u);
        return;
    }
}

uint8_t preset_loadFirstAvailableSceneOrKit(void)
{
    uint16_t slot;

    /*
     * Start the Bank empty/failure fallback chain.
     *
     * Order is root Scene, then root Kit, then SRAM defaults. The helpers
     * return STORAGE_*_MAX_SLOTS as the absent sentinel because slot 000 is a
     * valid library slot and cannot mean "none".
     */
    slot = filesystem_firstSceneSlot();
    if (slot < STORAGE_SCENE_MAX_SLOTS)
        return preset_loadSceneForScenes(slot, 1u);
    slot = filesystem_firstKitSlot();
    if (slot < STORAGE_KIT_MAX_SLOTS)
        return preset_loadKitForScenes(slot, 1u);
    return 0u;
}

/* -----------------------------------------------------------------------
** preset_loadGlobals — post one accepted async globals load request.
**
** Inputs: mounted filesystem and the single Preset operation gate. Output:
** returns one only after filesystem accepts Settings load and Preset enters
** PRESET_LOAD_IN_PROGRESS; returns zero with Preset idle on rejection. Menu's
** explicit OK lifecycle relies on this result so `...` never represents a
** request that was not posted. Affiliate: presetManager.h declaration.
** ----------------------------------------------------------------------- */
uint8_t preset_loadGlobals(void)
{
    filesystem_ack();
    pm_status = PRESET_LOAD_IN_PROGRESS;
    pm_completed_op = PRESET_OP_NONE;
    pm_request_slot = 0;
    pm_request_type = SAVE_TYPE_GLO;
    if (!filesystem_requestLoad(FS_FILE_SETTINGS, 0, on_globals_load_complete)) {
        pm_status = PRESET_IDLE;
        return 0u;
    }
    return 1u;
}

/* -----------------------------------------------------------------------
** preset_saveGlobals — post one accepted async globals save request.
**
** Inputs/outputs mirror preset_loadGlobals(): a one result owns the later
** completion callback, while zero leaves the confirmation row selectable.
** Why: explicit Save:[Settings] needs the same accepted/rejected contract as
** Kit, Scene, Bank, Instrument, and test commands. Affiliate: header API.
** ----------------------------------------------------------------------- */
uint8_t preset_saveGlobals(void)
{
    filesystem_ack();
    pm_status = PRESET_LOAD_IN_PROGRESS;
    pm_completed_op = PRESET_OP_NONE;
    pm_request_slot = 0;
    pm_request_type = SAVE_TYPE_GLO;
    if (!filesystem_requestSave(FS_FILE_SETTINGS, 0, on_globals_save_complete)) {
        pm_status = PRESET_IDLE;
        return 0u;
    }
    return 1u;
}

/* -----------------------------------------------------------------------
** preset_loadName — post async name load request.
** ----------------------------------------------------------------------- */
char* preset_loadName(uint16_t presetNr, uint8_t what)
{
    uint8_t hasName = 0;
    fs_file_type_t type = preset_fileTypeFromSaveType(what, &hasName);

    pm_request_slot = presetNr;
    pm_request_type = what;

    if (!hasName) {
        memcpy(preset_currentName, "        ", 8);
        return preset_currentName;
    }

    memcpy(preset_currentName, "        ", 8);
    filesystem_ack();
    pm_status = PRESET_LOAD_IN_PROGRESS;
    pm_completed_op = PRESET_OP_NONE;
    if (!filesystem_requestLoadName(type, presetNr, on_name_load_complete))
        pm_status = PRESET_IDLE;
    return preset_currentName;
}

void preset_applyLoadedName(void)
{
    memcpy(preset_currentName, filesystem_loadedName(), 8);
}

uint8_t preset_loadInstrument(uint8_t destination_scene,
                              uint8_t destination_slot,
                              instrument_type_t type,
                              uint16_t browser_index)
{
    /*
     * Nothing loads into Scene 17 (S082 P1 R1, D36).
     *
     * What: a destination Scene outside the resident range (index 16) is
     * refused before any Preset or filesystem state changes.
     * Why: this loader takes a Scene index, and scene_get(16) is valid while
     * Scene 17 exists, so a stray caller could otherwise write into 17 (and a
     * 16-bit 1u << 16 destination mask would be undefined). The Menu passes
     * the home Scene (menu_instrumentEntryScene()); this is the backstop.
     * Inputs: destination_scene. Outputs: 0 (refused) for index >= SCENE_COUNT.
     * Affiliates: menu_instrumentEntryScene(), scene_backgroundHomeIndex().
     */
    if (destination_scene >= SCENE_COUNT)
        return 0u;
    return preset_loadInstrumentForScenes((uint16_t)(1u << destination_scene),
                                          destination_slot,
                                          type,
                                          browser_index);
}

uint8_t preset_loadInstrumentForScenes(uint16_t destination_scene_mask,
                                       uint8_t destination_slot,
                                       instrument_type_t type,
                                       uint16_t browser_index)
{
    uint8_t scene_index;
    uint8_t request_scene = 0u;
    uint16_t valid_mask = 0u;

    /*
     * Post one immutable staged-Instrument request for one or more Scenes.
     *
     * Inputs: destination Scene mask, slot, selected type, and browser cache
     * index. Output: one filesystem read from Instrument/; after completion
     * preset_startInstrumentApply() commits the staged slot to every valid
     * masked Scene. The representative request_scene is only the staging
     * coordinate needed by the parser and compatibility completion accessors.
     */
    for (scene_index = 0u; scene_index < SCENE_COUNT && scene_index < 16u;
         scene_index++) {
        if ((destination_scene_mask & (uint16_t)(1u << scene_index)) != 0u &&
            scene_instrumentSlotConst(scene_index, destination_slot)) {
            valid_mask = (uint16_t)(valid_mask | (uint16_t)(1u << scene_index));
            if (valid_mask == (uint16_t)(1u << scene_index))
                request_scene = scene_index;
        }
    }
    if (valid_mask == 0u)
        return 0u;
    filesystem_ack();
    if (!filesystem_requestLoadInstrument(request_scene, destination_slot,
                                          type, browser_index,
                                          on_instrument_load_complete)) {
        return 0u;
    }
    pm_status = PRESET_LOAD_IN_PROGRESS;
    pm_completed_op = PRESET_OP_NONE;
    pm_request_slot = destination_slot;
    pm_request_type = SAVE_TYPE_KIT;
    pm_instrument_request_scene = request_scene;
    pm_instrument_request_slot = destination_slot;
    pm_instrument_request_type = type;
    pm_instrument_request_index = browser_index;
    pm_kit_request_scene_mask = valid_mask;
    return 1u;
}

uint8_t preset_saveInstrument(uint8_t source_scene,
                              uint8_t source_slot,
                              const char *display_name)
{
    /*
     * Post one immutable root Instrument Save request.
     *
     * Inputs: resident source Scene/voice and the user-edited file stem. Output
     * is nonzero only after filesystem accepts the write. Completion context
     * retains the source coordinates for UI bookkeeping, but no runtime apply
     * follows because saving does not mutate resident SceneData.
     */
    filesystem_ack();
    if (!filesystem_requestSaveInstrument(source_scene,
                                          source_slot,
                                          display_name,
                                          on_instrument_save_complete)) {
        return 0u;
    }
    pm_status = PRESET_LOAD_IN_PROGRESS;
    pm_completed_op = PRESET_OP_NONE;
    pm_request_slot = source_slot;
    pm_request_type = SAVE_TYPE_KIT;
    pm_instrument_request_scene = source_scene;
    pm_instrument_request_slot = source_slot;
    pm_instrument_request_type = INSTRUMENT_TYPE_UNKNOWN;
    pm_instrument_request_index = 0u;
    return 1u;
}

uint8_t preset_saveInstrumentMorph(uint8_t source_scene,
                                   uint8_t source_slot,
                                   const char *display_name)
{
    /*
     * Post one immutable root Instrument Morph Save request.
     *
     * Inputs: resident source Scene/voice plus the edited Instrument stem.
     * Output: nonzero only when filesystem accepts the write. The request type
     * is KitMrp because the nested Instrument Save surface is reached from the
     * Kit/KitMrp Load-Save context, but completion stays instrument-specific so
     * Menu can clear nested busy state exactly like normal Instrument Save.
     * Affiliates: filesystem_requestSaveInstrumentMorph(), storageTypes'
     * Morph Save write view, and menu_instrumentSaveRequestSelection().
     */
    filesystem_ack();
    if (!filesystem_requestSaveInstrumentMorph(
            source_scene,
            source_slot,
            display_name,
            on_instrument_morph_save_complete)) {
        return 0u;
    }
    pm_status = PRESET_LOAD_IN_PROGRESS;
    pm_completed_op = PRESET_OP_NONE;
    pm_request_slot = source_slot;
    pm_request_type = SAVE_TYPE_KIT_MORPH;
    pm_instrument_request_scene = source_scene;
    pm_instrument_request_slot = source_slot;
    pm_instrument_request_type = INSTRUMENT_TYPE_UNKNOWN;
    pm_instrument_request_index = 0u;
    return 1u;
}

uint8_t preset_loadInstrumentMorph(uint8_t destination_scene,
                                   uint8_t destination_slot,
                                   instrument_type_t type,
                                   uint16_t browser_index)
{
    const kit_instrument_slot_t *destination;
    /*
     * Nothing loads into Scene 17 (S082 P1 R1, D36).
     *
     * What: a destination Scene outside the resident range (index 16) is
     * refused before any Preset or filesystem state changes.
     * Why: this loader takes a Scene index, and scene_get(16) is valid while
     * Scene 17 exists, so a stray caller could otherwise write into 17 (and a
     * 16-bit 1u << 16 destination mask would be undefined). The Menu passes
     * the home Scene (menu_instrumentEntryScene()); this is the backstop.
     * Inputs: destination_scene. Outputs: 0 (refused) for index >= SCENE_COUNT.
     * Affiliates: menu_instrumentEntryScene(), scene_backgroundHomeIndex().
     */
    if (destination_scene >= SCENE_COUNT)
        return 0u;
    destination =
        scene_instrumentSlotConst(destination_scene, destination_slot);

    /*
     * Post an Instrument Morph load for the destination slot's current type.
     *
     * The type equality check is intentional. InstrumentMrp is an endpoint
     * update for the currently loaded slot, not a type-changing operation. The
     * parser stages a full Instrument file through the normal loader, but the
     * later commit copies only same-type morphable normal endpoint values into
     * the resident morph image.
     */
    if (!destination || destination_slot >= INSTRUMENT_SLOT_COUNT ||
        type >= INSTRUMENT_TYPE_UNKNOWN || destination->type != type) {
        return 0u;
    }

    filesystem_ack();
    if (!filesystem_requestLoadInstrument(destination_scene, destination_slot,
                                          type, browser_index,
                                          on_instrument_morph_load_complete)) {
        return 0u;
    }
    pm_status = PRESET_LOAD_IN_PROGRESS;
    pm_completed_op = PRESET_OP_NONE;
    pm_request_slot = destination_slot;
    pm_request_type = SAVE_TYPE_KIT_MORPH;
    pm_instrument_request_scene = destination_scene;
    pm_instrument_request_slot = destination_slot;
    pm_instrument_request_type = type;
    pm_instrument_request_index = browser_index;
    return 1u;
}

/* Retired File/Dir diagnostic request adapters; see disabled callbacks above. */
#if 0
uint8_t preset_scanTestFiles(void)
{
    /*
     * Post the temporary Load:[File] root scan.
     *
     * This is a storage diagnostic operation, not a musical preset action.
     * Preset participates only so Menu can reuse the existing async completion
     * poll and avoid calling filesystem.c directly from encoder handlers.
     */
    filesystem_ack();
    if (!filesystem_requestScanTestFiles(on_test_scan_complete))
        return 0u;
    pm_status = PRESET_LOAD_IN_PROGRESS;
    pm_completed_op = PRESET_OP_NONE;
    pm_request_type = SAVE_TYPE_FILE;
    pm_request_slot = 0u;
    return 1u;
}

uint8_t preset_scanTestDirs(void)
{
    filesystem_ack();
    if (!filesystem_requestScanTestDirs(on_test_scan_complete))
        return 0u;
    pm_status = PRESET_LOAD_IN_PROGRESS;
    pm_completed_op = PRESET_OP_NONE;
    pm_request_type = SAVE_TYPE_DIR;
    pm_request_slot = 0u;
    return 1u;
}

uint8_t preset_loadTestFile(const char *display_name)
{
    filesystem_ack();
    if (!filesystem_requestLoadTestFile(display_name,
                                        on_test_file_load_complete))
        return 0u;
    pm_status = PRESET_LOAD_IN_PROGRESS;
    pm_completed_op = PRESET_OP_NONE;
    pm_request_type = SAVE_TYPE_FILE;
    pm_request_slot = 0u;
    return 1u;
}

uint8_t preset_loadTestDir(const char *display_name)
{
    filesystem_ack();
    if (!filesystem_requestLoadTestDir(display_name,
                                       on_test_dir_load_complete))
        return 0u;
    pm_status = PRESET_LOAD_IN_PROGRESS;
    pm_completed_op = PRESET_OP_NONE;
    pm_request_type = SAVE_TYPE_DIR;
    pm_request_slot = 0u;
    return 1u;
}

uint8_t preset_saveTestFile(const char *display_name)
{
    filesystem_ack();
    if (!filesystem_requestSaveTestFile(display_name,
                                        on_test_file_save_complete))
        return 0u;
    pm_status = PRESET_LOAD_IN_PROGRESS;
    pm_completed_op = PRESET_OP_NONE;
    pm_request_type = SAVE_TYPE_FILE;
    pm_request_slot = 0u;
    return 1u;
}

uint8_t preset_saveTestDir(const char *display_name)
{
    filesystem_ack();
    if (!filesystem_requestSaveTestDir(display_name,
                                       on_test_dir_save_complete))
        return 0u;
    pm_status = PRESET_LOAD_IN_PROGRESS;
    pm_completed_op = PRESET_OP_NONE;
    pm_request_type = SAVE_TYPE_DIR;
    pm_request_slot = 0u;
    return 1u;
}

uint8_t preset_saveTestSimpleDir(const char *display_name)
{
    filesystem_ack();
    if (!filesystem_requestSaveTestSimpleDir(display_name,
                                             on_test_dir_save_complete))
        return 0u;
    pm_status = PRESET_LOAD_IN_PROGRESS;
    pm_completed_op = PRESET_OP_NONE;
    pm_request_type = SAVE_TYPE_SIMPLE_DIR;
    pm_request_slot = 0u;
    return 1u;
}

#endif

/*
 * Compatibility stubs for retired File/Dir diagnostics.
 *
 * Inputs are intentionally ignored and output is always "not started": the
 * Load/Save chooser no longer exposes these types, and retaining callers must
 * not allocate or repopulate the removed filesystem diagnostic caches.
 */
uint8_t preset_scanTestFiles(void) { return 0u; }
uint8_t preset_scanTestDirs(void) { return 0u; }
uint8_t preset_loadTestFile(const char *name) { (void)name; return 0u; }
uint8_t preset_loadTestDir(const char *name) { (void)name; return 0u; }
uint8_t preset_saveTestFile(const char *name) { (void)name; return 0u; }
uint8_t preset_saveTestDir(const char *name) { (void)name; return 0u; }
uint8_t preset_saveTestSimpleDir(const char *name) { (void)name; return 0u; }

/* =======================================================================
** Pattern / all / performance entry points
** ======================================================================= */

#define FILE_VERSION 2

uint8_t preset_loadPattern(uint16_t presetNr)
{
    filesystem_ack();
    pm_status = PRESET_LOAD_IN_PROGRESS;
    pm_completed_op = PRESET_OP_NONE;
    pm_request_slot = presetNr;
    pm_request_type = PRESET_REQUEST_PATTERN;
    if (filesystem_requestLoad(FS_FILE_PATTERN, presetNr, on_pattern_load_complete))
        return 1;
    pm_status = PRESET_IDLE;
    return 0;
}

uint8_t preset_loadPatternForScenes(uint16_t presetNr, uint16_t scene_mask)
{
    filesystem_ack();
    pm_status = PRESET_LOAD_IN_PROGRESS;
    pm_completed_op = PRESET_OP_NONE;
    pm_request_slot = presetNr;
    pm_request_type = PRESET_REQUEST_PATTERN;
    pm_kit_request_scene_mask = scene_mask;
    if (filesystem_requestLoadPatternForScenes(presetNr, scene_mask,
                                               on_pattern_load_complete))
        return 1;
    pm_status = PRESET_IDLE;
    return 0;
}

uint8_t preset_savePattern(uint16_t presetNr)
{
    filesystem_ack();
    pm_status = PRESET_LOAD_IN_PROGRESS;
    pm_completed_op = PRESET_OP_NONE;
    pm_request_slot = presetNr;
    pm_request_type = PRESET_REQUEST_PATTERN;
    if (!filesystem_requestSave(FS_FILE_PATTERN, presetNr, on_pattern_save_complete)) {
        pm_status = PRESET_IDLE;
        return 0u;
    }
    return 1u;
}

void preset_saveAll(uint8_t presetNr, uint8_t isAll)
{
    fs_file_type_t type = isAll ? FS_FILE_ALL : FS_FILE_PERFORMANCE;
    fs_completion_cb_t cb = isAll ? on_all_save_complete : on_performance_save_complete;

    filesystem_ack();
    pm_status = PRESET_LOAD_IN_PROGRESS;
    pm_completed_op = PRESET_OP_NONE;
    pm_request_slot = presetNr;
    pm_request_type = isAll ? PRESET_REQUEST_LEGACY_ALL :
                              PRESET_REQUEST_LEGACY_PERFORMANCE;
    if (!filesystem_requestSave(type, presetNr, cb))
        pm_status = PRESET_IDLE;
}

uint8_t preset_loadAll(uint8_t presetNr, uint8_t isAll)
{
    fs_file_type_t type = isAll ? FS_FILE_ALL : FS_FILE_PERFORMANCE;
    fs_completion_cb_t cb = isAll ? on_all_load_complete : on_performance_load_complete;

    filesystem_ack();
    pm_status = PRESET_LOAD_IN_PROGRESS;
    pm_completed_op = PRESET_OP_NONE;
    pm_request_slot = presetNr;
    pm_request_type = isAll ? PRESET_REQUEST_LEGACY_ALL :
                              PRESET_REQUEST_LEGACY_PERFORMANCE;
    if (filesystem_requestLoad(type, presetNr, cb))
        return 1;
    pm_status = PRESET_IDLE;
    return 0;
}

static uint8_t preset_interpolate(uint8_t a, uint8_t b, uint8_t x)
{
    uint16_t fixedPointValue = (uint16_t)(((a * 256) + (b - a) * x));
    uint8_t result = (uint8_t)(fixedPointValue / 256);
    return (uint8_t)((fixedPointValue & 0xff) < 0x7f ? result : result + 1);
}

void preset_morph(uint8_t morph)
{
    /*
     * Set overall Scene Morph by writing all per-voice Morph amounts and the
     * Effect Morph (S083 follow-up: global Morph sets all Morph).
     *
     * Inputs: user-facing 0..255 Morph amount from PERF, global MIDI CC1, or
     * Scene-load fallback. Outputs: the Scene global mirror, all six
     * per-slot Morph amounts and the Effect Morph are retained, flat PERF menu
     * mirrors (`fxm` included) are updated,
     * and the Morph worker is queued for every instrument slot. Runtime Morph
     * is still per voice; this function is only the bulk-set operation that
     * gives the user one overall control.
     */
    preset_morphScene(scene_getActiveIndex(), morph);
}

void preset_morphScene(uint8_t scene_index, uint8_t morph)
{
    scene_t *scene;

    /*
     * Set overall Morph for a specific resident Scene.
     *
     * Inputs: Scene index from active UI, MIDI, or scene_mask_voice_edit fan-
     * out and 0..255 Morph amount. Outputs: retained Scene global Morph,
     * all six per-voice Morph amounts and the Effect Morph (applied as an
     * `fxm` menu edit, S083 follow-up) update. Nothing is recorded. Runtime/PERF mirrors are updated
     * only for the active Scene, because inactive Scenes are stored state until
     * selected. The overall value and each per-voice value use SceneData's
     * change-aware owner setters, so retained storage precedes exact dirty bits
     * and identical values do not schedule a write.
     */
    preset_ensureMorphInitialized();
    scene = scene_get(scene_index);
    if (!scene)
        return;
    scene_setMorphAmount(scene_index, morph);
    scene_setAllVoiceMorphAmounts(scene_index, morph);
    /*
     * Global Morph sets all Morph, the Effect Morph included (S083
     * follow-up). Before, only the retained byte was written: a Pattern
     * `fxm` override or an Effect-page menu hold kept the old amount
     * sounding and the `fxm` mirror stayed stale, so FX Morph did not
     * follow. effects_setMorphAmountFromGlobal() applies it as an `fxm`
     * menu edit of this one Scene (Rule A override clear, menuHoldMorph on
     * the active Scene), never recorded, like the per-voice amounts below.
     */
    (void)effects_setMorphAmountFromGlobal(scene_index, morph);
    /*
     * S076 Rule A: bulk Morph set from PERF / global MIDI CC1 supersedes
     * every active step-automation Morph override. Each slot is cleared
     * individually through the single-slot helper. The clear precedes the
     * active-Scene mirror sync and rebuild so the worker uses the newly
     * retained amounts.
     * Input: active Scene guard. Output: all morph_step_override[].active = 0.
     * Callers (upstream): preset_morph() (PERF global Morph, MIDI CC1
     * global). Affiliate: preset_morphVoiceScene() (the per-slot twin —
     * Change 6).
     */
    if (scene_index == scene_getActiveIndex()) {
        for (uint8_t s = 0u; s < INSTRUMENT_SLOT_COUNT; s++)
            presetMorph_clearStepAutomationOverride(s);
    }
    if (scene_index == scene_getActiveIndex()) {
        preset_syncSceneMorphMirrors(scene);
        preset_syncEffectMorphMirror();	/* PERF/Effect `fxm` display */
        presetMorph_requestAll(scene_index);
    }
}

void preset_morphVoice(uint8_t slot, uint8_t morph)
{
    /*
     * Set one Scene voice's Morph amount.
     *
     * Inputs: zero-based instrument slot and 0..255 Morph amount. Output: only
     * that slot's Scene Morph amount and PERF mirror are updated, and only that
     * slot is queued for descriptor Morph interpolation. This function exists
     * separately from preset_morph() because global Morph is a bulk-set
     * operation while MIDI CC1 on a voice channel and PERF 1vm..6vm edits must
     * preserve the other five slot amounts.
     */
    preset_morphVoiceScene(scene_getActiveIndex(), slot, morph);
}

void preset_morphVoiceScene(uint8_t scene_index, uint8_t slot, uint8_t morph)
{
    /*
     * Set one Scene/voice Morph amount.
     *
     * Inputs: resident Scene index, zero-based instrument slot, and 0..255
     * Morph amount. Outputs: retained per-voice Morph state updates for any
     * valid Scene; only the active Scene updates flat PERF mirrors and queues
     * descriptor interpolation.
     */
    if (slot >= INSTRUMENT_SLOT_COUNT || !scene_get(scene_index))
        return;
    preset_ensureMorphInitialized();
    scene_setVoiceMorphAmount(scene_index, slot, morph);
    /*
     * S076 Rule A: a non-automation Morph write supersedes any active step
     * override for this slot. The clear precedes the active-Scene rebuild
     * request so presetMorph_effectiveVoiceBase() returns the newly retained
     * amount when the worker processes the queued slot.
     * Input: active Scene guard. Output: morph_step_override[slot].active = 0.
     * Callers (upstream): preset_morphVoice() (PERF, MIDI CC1 per-voice),
     * menu_cellCommitValue() Morph cells. Affiliate:
     * presetMorph_setStepAutomationOverride() (the automation setter),
     * presetMorph_effectiveVoiceBase() (the worker consumer).
     */
    if (scene_index == scene_getActiveIndex())
        presetMorph_clearStepAutomationOverride(slot);
    if (scene_index == scene_getActiveIndex()) {
        parameter_values[PAR_VOICE1_MORPH + slot] = morph;
        presetMorph_requestVoice(scene_index, slot);
    }
}

void preset_rebuildMorph(void)
{
    /*
     * Requeue Morph from retained Scene values without changing them.
     *
     * Inputs: none; the active Scene supplies all six per-slot Morph amounts.
     * Output: the descriptor-driven Morph worker rebuilds the runtime
     * interpolation image. Clients are endpoint-load/edit refresh paths that
     * must not call preset_morph(), because that would overwrite distinct
     * per-voice Morph amounts with the global bulk-set amount.
     */
    uint8_t scene_index = scene_getActiveIndex();

    preset_ensureMorphInitialized();
    presetMorph_rebuildScene(scene_index);
}

/*
 * preset_resetSlotMorphToNormal — equalise one slot's Morph to its Normal.
 *
 * What:       iterates the descriptor table of the slot's current instrument
 *             type and, for every descriptor whose flags include
 *             INSTRUMENT_PARAM_FLAG_MORPHABLE, copies the retained Normal
 *             image byte onto the retained Morph image byte. Non-morphable
 *             descriptor rows are untouched.
 * Why:        the "reset morph" clears (track- and Scene-level) need one shared
 *             per-slot morphable-copy loop. Writing the retained Scene image
 *             directly and marking once afterwards is the same pattern the
 *             KitMrp/InstrumentMrp endpoint commits use; going through
 *             preset_setInstrumentParameter() per byte would re-interpolate
 *             each descriptor and re-check automation for a batch the caller
 *             rebuilds with a single preset_rebuildMorph().
 * Inputs:     scene_index (0..15) and slot (0..5). The slot's current type
 *             selects the descriptor table.
 * Outputs:    the count of Morph bytes changed (0 for invalid coordinates or an
 *             empty descriptor table). On change, marks the slot's Morph
 *             AutoSave scope and clears the Scene's card-clean bit. Does NOT
 *             queue the Morph worker.
 * Accessors:  scene_instrumentSlot(), instrumentManager_registryEntry(),
 *             INSTRUMENT_PARAM_FLAG_MORPHABLE, autosave_markInstrumentMorphDirty(),
 *             bank_invalidateSdCleanScene().
 * Affiliates: preset_copySlotNormalToMorph() below,
 *             ccClear_runResetMorphTrack(), ccClear_runResetSceneMorph(),
 *             preset_rebuildMorph().
 */
uint8_t preset_resetSlotMorphToNormal(uint8_t scene_index, uint8_t slot)
{
    kit_instrument_slot_t *dst = scene_instrumentSlot(scene_index, slot);
    const instrument_registry_entry_t *entry;
    instrument_parameter_images_t *images;
    uint8_t changed = 0u;
    uint8_t i;

    if (!dst)
        return 0u;
    entry = instrumentManager_registryEntry(dst->type);
    if (!entry || !entry->descriptors)
        return 0u;
    images = &dst->parameter_images;
    for (i = 0u; i < entry->descriptor_count; i++) {
        if (!(entry->descriptors[i].flags & INSTRUMENT_PARAM_FLAG_MORPHABLE))
            continue;
        if (images->morph_instrument_parameters[i] !=
            images->instrument_parameters[i]) {
            images->morph_instrument_parameters[i] =
                images->instrument_parameters[i];
            changed++;
        }
    }
    if (changed) {
        autosave_markInstrumentMorphDirty(scene_index, slot);
        bank_invalidateSdCleanScene(scene_index);
    }
    return changed;
}

/*
 * preset_copySlotNormalToMorph — copy src Normal onto dst Morph.
 *
 * What:       reads the source slot's Normal image and writes every morphable
 *             descriptor byte into the destination slot's Morph image, by
 *             descriptor index. Returns 0 with no side effects when the source
 *             and destination instrument types differ (a different type's
 *             descriptor indices are meaningless, so the member is silently
 *             skipped).
 * Why:        the "inst -> morph" track copy and the "scene -> morph" Scene
 *             copy share this cross-Scene, per-slot morphable-endpoint loop.
 *             Matching types guarantee identical descriptor layouts, so an
 *             index-by-index copy needs no key remapping.
 * Inputs:     src_scene/src_slot (Normal source) and dst_scene/dst_slot (Morph
 *             target), all coordinates valid.
 * Outputs:    the count of Morph bytes changed (0 for invalid coordinates or a
 *             type mismatch). On change, marks the destination slot's Morph
 *             AutoSave scope and clears the destination Scene's card-clean bit.
 *             Does NOT queue the Morph worker.
 * Accessors:  scene_instrumentSlot(), instrumentManager_registryEntry(),
 *             autosave_markInstrumentMorphDirty(), bank_invalidateSdCleanScene().
 * Affiliates: preset_resetSlotMorphToNormal() above, ccCopy_runMorphTrack(),
 *             ccCopy_runSceneMorph().
 */
uint8_t preset_copySlotNormalToMorph(uint8_t src_scene, uint8_t src_slot,
                                     uint8_t dst_scene, uint8_t dst_slot)
{
    const kit_instrument_slot_t *src =
        scene_instrumentSlotConst(src_scene, src_slot);
    kit_instrument_slot_t *dst = scene_instrumentSlot(dst_scene, dst_slot);
    const instrument_registry_entry_t *entry;
    instrument_parameter_images_t *dst_images;
    uint8_t changed = 0u;
    uint8_t i;

    if (!src || !dst || src->type != dst->type)
        return 0u;
    entry = instrumentManager_registryEntry(dst->type);
    if (!entry || !entry->descriptors)
        return 0u;
    dst_images = &dst->parameter_images;
    for (i = 0u; i < entry->descriptor_count; i++) {
        if (!(entry->descriptors[i].flags & INSTRUMENT_PARAM_FLAG_MORPHABLE))
            continue;
        if (dst_images->morph_instrument_parameters[i] !=
            src->parameter_images.instrument_parameters[i]) {
            dst_images->morph_instrument_parameters[i] =
                src->parameter_images.instrument_parameters[i];
            changed++;
        }
    }
    if (changed) {
        autosave_markInstrumentMorphDirty(dst_scene, dst_slot);
        bank_invalidateSdCleanScene(dst_scene);
    }
    return changed;
}

/* S075: the former global `srt` setter was retired with its Scene field. */

/*
 * Keep SceneData and the four S074 page mirrors on the same field order.
 *
 * A compile-time failure here means ParameterArray and SceneData no longer
 * describe the same contiguous cmp/cam/ctm/csc group.
 */
_Static_assert(PAR_BUS_COMP_SIDECHAIN - PAR_BUS_COMP_MODE + 1 ==
                   SCENE_BUS_COMP_FIELD_COUNT,
               "bus compressor mirrors must match SceneData field order");

void preset_setBusCompSetting(uint8_t scene_index, uint8_t field,
                              uint8_t value)
{
    /*
     * Retain one S074 bus compressor setting for one Scene.
     *
     * Inputs: Scene index, field, and any byte. Outputs: clamped SceneData
     * storage plus AutoSave/card-clean ownership; the active Scene's mirror
     * is updated only when this Scene is active, so a fan-out never displays a
     * non-active Scene's value. No runtime push is needed.
     */
    if (!scene_get(scene_index) || field >= SCENE_BUS_COMP_FIELD_COUNT)
        return;
    value = scene_busCompClamp(field, value);
    scene_setBusCompSetting(scene_index, field, value);
    if (scene_index == scene_getActiveIndex())
        parameter_values[PAR_BUS_COMP_MODE + field] = value;
}

void preset_syncBusCompMirrors(void)
{
    const uint8_t scene_index = scene_getActiveIndex();
    uint8_t field;

    /*
     * Copy the active Scene's retained cmp/cam/ctm/csc values into the flat
     * page mirrors. Callers are Scene activation, page edits and the Global
     * bulk-apply guard; SceneData is read-only in this helper.
     */
    for (field = 0u; field < SCENE_BUS_COMP_FIELD_COUNT; field++)
        parameter_values[PAR_BUS_COMP_MODE + field] =
            scene_getBusCompSetting(scene_index, field);
}

/*
 * Copy the active Scene's Effect Morph amount into the PERF `fxm` mirror.
 *
 * What: read-only refresh of parameter_values[PAR_EFFECT_MORPH] from
 * SceneData. Why: retained Effect Morph changes on the Effect page, through
 * edit-mask fan-out, Scene activation, and copy/clear. Output: one mirror byte;
 * no retained write and no AutoSave mark. Affiliates: Scene apply, Menu's
 * `fxm` commit path, and EffectsManager.
 */
void preset_syncEffectMorphMirror(void)
{
    parameter_values[PAR_EFFECT_MORPH] =
        scene_getEffectMorphAmount(scene_getActiveIndex());
}

void preset_morphTick(void)
{
    /*
     * Compatibility tick called from main.c.
     *
     * Output is intentionally ignored by the caller; the worker itself limits
     * work to one descriptor-backed image value per call. Loaded-kit apply also
     * ticks the same worker while a load completion is active, so both boot and
     * runtime loads share one Morph path.
     */
    (void)presetMorph_tick();
}

uint8_t preset_getMorphValue(uint16_t index, uint8_t morph)
{
    if (index >= END_OF_SOUND_PARAMETERS)
        return 0;
    return preset_interpolate(parameter_values[index], parameters2[index], morph);
}
