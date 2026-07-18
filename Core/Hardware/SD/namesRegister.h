#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * Resident source-name domains stored in the SD-root `.names` register.
 *
 * What: addresses the 96 Instrument, 16 Kit, 16 Scene, and one Bank identity
 * cells that replace the same persistent strings in SceneData/BankData. Why:
 * library slots and FAT locations belong to Menu-driven real-tree resolution;
 * this enum identifies only which resident musical object owns a source name.
 * Inputs/outputs: callers combine a domain with resident Scene/voice indices;
 * namesRegister_recordIndex() validates and converts them to a serialized
 * record. Affiliates: filesystem Instrument/Kit/Scene/Bank completion paths and
 * Menu Save-name seeding.
 */
typedef enum {
    NAMES_REGISTER_INSTRUMENT = 0,
    NAMES_REGISTER_KIT,
    NAMES_REGISTER_SCENE,
    NAMES_REGISTER_BANK,
    NAMES_REGISTER_DOMAIN_COUNT
} namesRegisterDomain_t;

/*
 * Terminal result for one serialized register request.
 *
 * OK means the selected snapshot/record validated or the copy-on-write header
 * commit was synced. DEFAULTED means a corrupt/missing individual record was
 * replaced in the read result by a deterministic name; it never supplies a FAT
 * location. IO_ERROR covers open/read/write/seek/sync failure, and INVALID
 * rejects coordinates or names before accepting work.
 */
typedef enum {
    NAMES_REGISTER_RESULT_OK = 0,
    NAMES_REGISTER_RESULT_DEFAULTED,
    NAMES_REGISTER_RESULT_IO_ERROR,
    NAMES_REGISTER_RESULT_INVALID
} namesRegisterResult_t;

typedef void (*namesRegisterCallback_t)(namesRegisterResult_t result);

/*
 * Supply replacements while an inactive `.names` snapshot is streamed.
 *
 * Input recordIndex is the fixed resident-cell index 0..128. currentName is the
 * validated active value (or its deterministic fallback). Output nonzero asks
 * the register to serialize replacementOut instead; zero copies currentName.
 * The callback is synchronous and must not retain either pointer. It must use
 * only request-owned/static context because the update spans many poll ticks.
 */
typedef uint8_t (*namesRegisterProvider_t)(uint16_t recordIndex,
                                          const char *currentName,
                                          char replacementOut[17],
                                          void *context);

/*
 * Start validation/creation of `/.names`.
 *
 * The accepted operation opens root explicitly, selects the highest valid
 * header/bank pair, or creates deterministic defaults when neither snapshot is
 * usable. Output arrives once through callback. False means busy/invalid start
 * and guarantees no callback. Affiliates: namesRegister_tick() and boot before
 * resident payload loading.
 */
bool namesRegister_startMount(namesRegisterCallback_t callback);

/*
 * Read one resident source name without exposing library location data.
 *
 * Inputs are resident coordinates: Instrument uses sceneIndex and voiceSlot;
 * Kit/Scene use sceneIndex and ignore voiceSlot; Bank ignores both. Output is
 * copied into the module's one 17-byte result buffer and remains valid until
 * the next accepted register request. Use namesRegister_name() in callback.
 */
bool namesRegister_startRead(namesRegisterDomain_t domain,
                             uint8_t sceneIndex,
                             uint8_t voiceSlot,
                             namesRegisterCallback_t callback);

/*
 * Atomically update any subset of resident names with one-record SRAM usage.
 *
 * The provider is called for all 129 records as the active bank is copied to
 * the inactive bank. After the complete bank syncs, an alternating header
 * commits it. False accepts nothing; true guarantees one callback. No provider
 * output may contain a path or extension.
 */
bool namesRegister_startUpdate(namesRegisterProvider_t provider,
                               void *context,
                               namesRegisterCallback_t callback);

/* Advance one bounded unit after the caller has pumped afatfs_poll(). */
void namesRegister_tick(void);

/* Report whether one mount/read/update request owns the module. */
bool namesRegister_busy(void);

/* Borrow the most recent NUL-terminated read result until the next request. */
const char *namesRegister_name(void);

/*
 * Convert resident coordinates to the fixed 0..128 record address.
 *
 * Output is nonzero only for a valid coordinate and writes recordIndexOut.
 * This arithmetic is public for provider callbacks; it conveys resident-cell
 * address only and must never be interpreted as an SD library slot.
 */
uint8_t namesRegister_recordIndex(namesRegisterDomain_t domain,
                                  uint8_t sceneIndex,
                                  uint8_t voiceSlot,
                                  uint16_t *recordIndexOut);
