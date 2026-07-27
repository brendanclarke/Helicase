/*
 * Autosave.h -- compact working-Bank parameter autosave coordinator.
 *
 * The coordinator owns no static SRAM and performs no SD I/O. filesystem.c
 * lends it the existing typed-load stage union while no typed payload load is
 * active; filesystem remains the single AsyncFATFS owner.
 */
#ifndef AUTOSAVE_H_
#define AUTOSAVE_H_

#include <stdint.h>

#define AUTOSAVE_RECORD_COUNT 145u
#define AUTOSAVE_SCENE_RECORD_COUNT 9u
#define AUTOSAVE_SECTOR_BYTES 512u

typedef enum {
    AUTOSAVE_DOMAIN_BANKSET = 0u,
    AUTOSAVE_DOMAIN_SCENESET,
    AUTOSAVE_DOMAIN_KITSET,
    AUTOSAVE_DOMAIN_INSTRUMENT,
    AUTOSAVE_DOMAIN_SCENE_COMMIT
} autosave_domain_t;

typedef struct {
    uint8_t domain;
    uint8_t scene_index;
    uint8_t instrument_slot;
    uint16_t logical_index;
} autosave_record_key_t;

/*
 * This is a union member in filesystem.c, never a separately allocated
 * object. The two uint16_t tick arrays use the real 1 ms time_sysTick domain;
 * five- and thirty-second elapsed comparisons are safe across its 65.536 s
 * wrap when computed with unsigned subtraction.
 */
typedef struct {
    uint16_t first_dirty_tick[AUTOSAVE_RECORD_COUNT];
    uint16_t last_dirty_tick[AUTOSAVE_RECORD_COUNT];
    uint32_t pending_scene_batch_id[16u];
    uint8_t dirty_bits[(AUTOSAVE_RECORD_COUNT + 7u) / 8u];
    uint16_t in_flight_index;
    uint16_t active_bank_slot;
    uint32_t bank_epoch;
    uint32_t next_generation;
    uint32_t next_batch_id;
    uint16_t retry_after_tick;
    uint16_t format_sector;
    uint16_t io_offset;
    uint8_t initialized;
    uint8_t barrier_requested;
    uint8_t write_slot;
    uint8_t candidate_valid;
    uint8_t overlay_in_progress;
    uint8_t reset_in_progress;
    uint8_t header_valid;
    uint8_t reserved;
    uint16_t candidate_length;
    uint32_t candidate_generation;
    uint32_t candidate_batch_id;
    autosave_record_key_t current_key;
    /* Candidate payload is deliberately smaller than a sector: the largest
     * current Instrument payload is 130 bytes, while staging_buf remains the
     * only 512-byte SD transfer buffer. */
    uint8_t candidate_payload[160u];
} autosave_workspace_t;

void autosave_initWorkspace(autosave_workspace_t *workspace, uint16_t bank_slot);
uint8_t autosave_mutationAllowed(void);
uint8_t autosave_barrierRequested(void);
void autosave_requestLoadBarrier(void);
void autosave_releaseLoadBarrier(void);
void autosave_markBankset(void);
void autosave_markSceneset(uint8_t scene_index);
void autosave_markKitset(uint8_t scene_index);
void autosave_markInstrument(uint8_t scene_index, uint8_t slot);
void autosave_markSceneBatch(uint8_t scene_index, uint16_t domain_mask);
/* Rebuild the ledger after a typed loader has reused its union member. The
 * resulting background snapshot preserves both its import and any older dirty
 * slots without making the foreground load wait for SD writes. */
void autosave_markWholeBankSnapshot(void);
uint8_t autosave_selectDue(uint16_t now, autosave_record_key_t *key);
void autosave_writeCompleted(uint8_t ok);
uint8_t autosave_hasDirty(void);
uint16_t autosave_encodePayload(const autosave_record_key_t *key,
                                 uint8_t *dst, uint16_t capacity);
uint8_t autosave_applyPayload(const autosave_record_key_t *key,
                              const uint8_t *src, uint16_t length);
uint32_t autosave_crc32(const uint8_t *data, uint16_t length);
uint8_t autosave_keyFromLogical(uint16_t logical_index,
                                autosave_record_key_t *key);

#endif /* AUTOSAVE_H_ */
