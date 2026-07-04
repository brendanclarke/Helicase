/*
 * presetManager.h — LXR-02 preset load/save (asyncfatfs version).
 *
 * All load/save functions are asynchronous — they post a request to filesystem
 * and return immediately. The caller must poll preset_getStatus() to know
 * when the operation completes.
 *
 * Status lifecycle:
 *   PRESET_IDLE → preset_loadDrumset() → PRESET_LOAD_IN_PROGRESS
 *     → filesystem completes read → PRESET_UPDATE_READY
 *     → menu calls preset_applyPending() → PRESET_IDLE
 *
 * The menu main loop calls preset_pollStatus() each iteration. When
 * UPDATE_READY is seen, it applies post-load logic (mod target gap index,
 * repaint, etc.) and clears the status back to IDLE.
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

#ifndef PRESETMANAGER_H_
#define PRESETMANAGER_H_
#include <stdint.h>

/* -----------------------------------------------------------------------
** Async operation status
** ----------------------------------------------------------------------- */
typedef enum {
    PRESET_IDLE             = 0,
    PRESET_LOAD_IN_PROGRESS = 1,
    PRESET_UPDATE_READY     = 2,
} preset_status_t;

/* Which type of load completed — tells the menu what post-load work to do */
typedef enum {
    PRESET_OP_NONE,
    PRESET_OP_KIT_LOAD,
    PRESET_OP_MORPH_LOAD,
    PRESET_OP_GLOBALS_LOAD,
    PRESET_OP_NAME_LOAD,
    PRESET_OP_KIT_SAVE,
    PRESET_OP_MORPH_SAVE,
    PRESET_OP_GLOBALS_SAVE,
    PRESET_OP_PATTERN_LOAD,
    PRESET_OP_PATTERN_SAVE,
    PRESET_OP_ALL_LOAD,
    PRESET_OP_ALL_SAVE,
    PRESET_OP_PERFORMANCE_LOAD,
    PRESET_OP_PERFORMANCE_SAVE,
} preset_op_type_t;

extern char preset_currentName[8];

void    preset_init(void);

/* -----------------------------------------------------------------------
** Status polling — call from main loop (or menu tick) each iteration.
** Returns current status. When UPDATE_READY, caller should do post-load
** work then call preset_ackStatus() to clear back to IDLE.
** ----------------------------------------------------------------------- */
preset_status_t  preset_getStatus(void);
preset_op_type_t preset_getCompletedOp(void);
uint8_t          preset_getRequestSlot(void);
uint8_t          preset_getRequestType(void);
void             preset_ackStatus(void);

/* -----------------------------------------------------------------------
** Load/save — all async, return immediately.
** ----------------------------------------------------------------------- */

/* Drumset (kit). isMorph=1 reads/writes the morph kit buffer/path. */
uint8_t preset_loadDrumset(uint8_t presetNr, uint8_t isMorph);
void    preset_saveDrumset(uint8_t presetNr, uint8_t isMorph);

/* Globals — single GLO.CFG file. */
void    preset_loadGlobals(void);
void    preset_saveGlobals(void);

/* Pattern — async direct serializer in filesystem.c. */
uint8_t preset_loadPattern(uint8_t presetNr);
void    preset_savePattern(uint8_t presetNr);

/* All / Performance — async container serializers in filesystem.c. */
void    preset_saveAll(uint8_t presetNr, uint8_t isAll);
uint8_t preset_loadAll(uint8_t presetNr, uint8_t isAll);

/* Read 8-byte preset name from file header (any type). */
char*   preset_loadName(uint8_t presetNr, uint8_t what);
void    preset_applyLoadedName(void);

/* Send loaded parameters to DSP synchronously. Use this before audio starts;
** runtime load completion should use the chunked apply API below so it cannot
** monopolize one foreground pass. */
void    preset_sendDrumsetParameters(void);

/* Runtime chunked sound apply. preset_tickDrumsetApply() performs at most one
** voice of modulation-routing work and returns non-zero while that pass did
** foreground work. The menu owns operation-specific UI/global follow-up after
** the tick function reports idle. */
void    preset_startDrumsetApply(void);
uint8_t preset_tickDrumsetApply(void);

/* Morph — rate-limited front-panel CC parameter dump. */
void    preset_morph(uint8_t morph);
void    preset_morphTick(void);
uint8_t preset_getMorphValue(uint16_t index, uint8_t morph);

#endif
