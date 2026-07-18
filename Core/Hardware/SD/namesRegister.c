#include "namesRegister.h"

#include "asyncfatfs.h"

#include <string.h>

/*
 * On-card layout constants for the two-snapshot resident identity register.
 *
 * Two 512-byte header slots precede two nine-sector record banks. Only the
 * first 129 * 32 bytes of a bank are semantic; fixed sector alignment keeps
 * bank selection and future expansion independent of compiler struct layout.
 */
#define NAMES_HEADER_BYTES       32u
#define NAMES_RECORD_BYTES       32u
#define NAMES_RECORD_COUNT       129u
#define NAMES_HEADER_A_OFFSET    0u
#define NAMES_HEADER_B_OFFSET    512u
#define NAMES_BANK_A_OFFSET      1024u
#define NAMES_BANK_B_OFFSET      5632u
#define NAMES_FORMAT_VERSION     1u
#define NAMES_MAGIC_BYTES        8u
#define NAMES_NAME_BYTES         17u

static const uint8_t namesMagic[NAMES_MAGIC_BYTES] = {
    'L', 'X', 'R', 'N', 'A', 'M', 'E', 'S'
};

typedef enum {
    NAMES_JOB_NONE = 0,
    NAMES_JOB_MOUNT,
    NAMES_JOB_READ,
    NAMES_JOB_UPDATE
} namesJob_t;

typedef enum {
    NAMES_PHASE_OPEN_ROOT = 0,
    NAMES_PHASE_OPEN_FILE,
    NAMES_PHASE_WAIT_OPEN_FILE,
    NAMES_PHASE_MOUNT_SEEK_HEADER_A,
    NAMES_PHASE_MOUNT_READ_HEADER_A,
    NAMES_PHASE_MOUNT_SEEK_HEADER_B,
    NAMES_PHASE_MOUNT_READ_HEADER_B,
    NAMES_PHASE_MOUNT_SELECT_HEADER,
    NAMES_PHASE_MOUNT_SEEK_BANK,
    NAMES_PHASE_MOUNT_READ_BANK,
    NAMES_PHASE_INIT_TRUNCATE,
    NAMES_PHASE_INIT_WAIT_TRUNCATE,
    NAMES_PHASE_INIT_SEEK_BANK,
    NAMES_PHASE_INIT_WRITE_BANK,
    NAMES_PHASE_INIT_SYNC_BANK,
    NAMES_PHASE_INIT_SEEK_HEADER,
    NAMES_PHASE_INIT_WRITE_HEADER,
    NAMES_PHASE_INIT_SYNC_HEADER,
    NAMES_PHASE_READ_SEEK_RECORD,
    NAMES_PHASE_READ_RECORD,
    NAMES_PHASE_UPDATE_SEEK_SOURCE,
    NAMES_PHASE_UPDATE_READ_SOURCE,
    NAMES_PHASE_UPDATE_SEEK_DESTINATION,
    NAMES_PHASE_UPDATE_WRITE_DESTINATION,
    NAMES_PHASE_UPDATE_SYNC_BANK,
    NAMES_PHASE_UPDATE_SEEK_HEADER,
    NAMES_PHASE_UPDATE_WRITE_HEADER,
    NAMES_PHASE_UPDATE_SYNC_HEADER,
    NAMES_PHASE_CLOSE_FILE,
    NAMES_PHASE_WAIT_CLOSE_FILE,
    NAMES_PHASE_CLOSE_ROOT,
    NAMES_PHASE_WAIT_CLOSE_ROOT,
    NAMES_PHASE_FINISH
} namesPhase_t;

typedef struct {
    namesJob_t job;
    namesPhase_t phase;
    namesRegisterResult_t terminalResult;
    namesRegisterCallback_t callback;
    namesRegisterProvider_t provider;
    void *providerContext;
    afatfsDirHandle_t root;
    afatfsFilePtr_t file;
    volatile uint8_t openDone;
    volatile uint8_t closeDone;
    afatfsResultCode_t openResult;
    uint8_t header[2][NAMES_HEADER_BYTES];
    uint8_t headerValid[2];
    uint8_t candidateHeader;
    uint8_t activeHeader;
    uint8_t activeBank;
    uint32_t activeSequence;
    uint32_t expectedBankCrc;
    uint32_t runningBankCrc;
    uint16_t recordIndex;
    uint16_t requestedRecord;
    uint8_t ioOffset;
    uint8_t record[NAMES_RECORD_BYTES];
    char name[NAMES_NAME_BYTES];
} namesRegisterState_t;

/*
 * One module state owns all register I/O.
 *
 * It contains two 32-byte headers and one 32-byte record, never a resident-name
 * array. activeBank/sequence are non-location register metadata retained after
 * mount; FAT handles and providers exist only while a request is busy.
 */
static namesRegisterState_t namesState;

static uint16_t namesReadLe16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8u);
}

static uint32_t namesReadLe32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8u) |
           ((uint32_t)p[2] << 16u) | ((uint32_t)p[3] << 24u);
}

static void namesWriteLe16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8u);
}

static void namesWriteLe32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8u);
    p[2] = (uint8_t)(value >> 16u);
    p[3] = (uint8_t)(value >> 24u);
}

static uint32_t namesCrcContinue(uint32_t crc,
                                 const uint8_t *bytes,
                                 uint16_t length)
{
    uint16_t byteIndex;

    /*
     * Continue standard reflected CRC32 over one bounded serialized fragment.
     *
     * Each input byte is XORed into the low accumulator byte. Eight polynomial
     * steps then use an all-zero/all-one mask derived from bit zero; XOR with
     * 0xEDB88320 only when that bit was set. Callers seed 0xffffffff and invert
     * once after the complete header/record/bank. Affiliates: Phase 6 journal
     * CRC convention and the dual-bank publication checks.
     */
    for (byteIndex = 0u; byteIndex < length; byteIndex++) {
        uint8_t bit;
        crc ^= bytes[byteIndex];
        for (bit = 0u; bit < 8u; bit++) {
            uint32_t mask = (uint32_t)(0u - (crc & 1u));
            crc = (crc >> 1u) ^ (0xEDB88320u & mask);
        }
    }
    return crc;
}

static uint32_t namesCrc(const uint8_t *bytes, uint16_t length)
{
    return ~namesCrcContinue(0xffffffffu, bytes, length);
}

static uint32_t namesBankOffset(uint8_t bank)
{
    return bank ? NAMES_BANK_B_OFFSET : NAMES_BANK_A_OFFSET;
}

static uint32_t namesHeaderOffset(uint8_t header)
{
    return header ? NAMES_HEADER_B_OFFSET : NAMES_HEADER_A_OFFSET;
}

uint8_t namesRegister_recordIndex(namesRegisterDomain_t domain,
                                  uint8_t sceneIndex,
                                  uint8_t voiceSlot,
                                  uint16_t *recordIndexOut)
{
    uint16_t index;

    /*
     * Map a resident identity cell to its fixed record.
     *
     * Instrument arithmetic multiplies 16 Scene rows by six voices and adds
     * the voice coordinate. Kit and Scene occupy the following 16-record
     * ranges; Bank is the final singleton. No value here is an SD library slot.
     */
    if (!recordIndexOut || domain >= NAMES_REGISTER_DOMAIN_COUNT)
        return 0u;
    if (domain == NAMES_REGISTER_INSTRUMENT) {
        if (sceneIndex >= 16u || voiceSlot >= 6u)
            return 0u;
        index = (uint16_t)((uint16_t)sceneIndex * 6u + voiceSlot);
    } else if (domain == NAMES_REGISTER_KIT) {
        if (sceneIndex >= 16u)
            return 0u;
        index = (uint16_t)(96u + sceneIndex);
    } else if (domain == NAMES_REGISTER_SCENE) {
        if (sceneIndex >= 16u)
            return 0u;
        index = (uint16_t)(112u + sceneIndex);
    } else {
        index = 128u;
    }
    *recordIndexOut = index;
    return 1u;
}

static void namesRecordDomainKey(uint16_t index,
                                 uint8_t *domain,
                                 uint8_t *key)
{
    /* Convert only the fixed resident record address back to domain-local key. */
    if (index < 96u) {
        *domain = NAMES_REGISTER_INSTRUMENT;
        *key = (uint8_t)index;
    } else if (index < 112u) {
        *domain = NAMES_REGISTER_KIT;
        *key = (uint8_t)(index - 96u);
    } else if (index < 128u) {
        *domain = NAMES_REGISTER_SCENE;
        *key = (uint8_t)(index - 112u);
    } else {
        *domain = NAMES_REGISTER_BANK;
        *key = 0u;
    }
}

static void namesDefault(uint16_t index, char out[NAMES_NAME_BYTES])
{
    uint8_t key;
    uint8_t domain;

    memset(out, 0, NAMES_NAME_BYTES);
    namesRecordDomainKey(index, &domain, &key);
    if (domain == NAMES_REGISTER_INSTRUMENT) {
        memcpy(out, "inst_vo", 7u);
        out[7] = (char)('1' + (key % 6u));
    } else if (domain == NAMES_REGISTER_KIT) {
        memcpy(out, "Kit", 3u);
    } else if (domain == NAMES_REGISTER_SCENE) {
        memcpy(out, "Scene ", 6u);
        out[6] = (char)('0' + ((key + 1u) / 10u));
        out[7] = (char)('0' + ((key + 1u) % 10u));
    } else {
        memcpy(out, "none", 4u);
    }
}

static void namesSanitize(const char *source, char out[NAMES_NAME_BYTES])
{
    uint8_t length = 0u;

    /*
     * Copy one source stem/name, never a component path.
     * Printable bytes stop at NUL or a path separator. A period is ordinary
     * name text here because Kit, Scene, and Bank folder names may contain it;
     * the Instrument caller, which owns extension semantics, must remove its
     * extension before supplying a replacement. The 16-byte bound matches the
     * longest resident Instrument stem. Affiliates: filesystem's domain-aware
     * name extractors and namesBuildRecord() below.
     */
    memset(out, 0, NAMES_NAME_BYTES);
    while (source && length < 16u && source[length] != '\0' &&
           source[length] != '/' && source[length] != '\\') {
        char c = source[length];
        out[length] = (c >= 0x20 && c <= 0x7e) ? c : '_';
        length++;
    }
}

static void namesBuildRecord(uint16_t index,
                             uint32_t generation,
                             const char *name)
{
    uint8_t domain;
    uint8_t key;
    uint8_t length;
    char sanitized[NAMES_NAME_BYTES];

    /*
     * Serialize one compiler-independent 32-byte record.
     * Offsets 0..27 are CRC-covered data; the final four bytes contain the
     * inverted CRC32. The local 17-byte text is bounded stack scratch, not a
     * persistent name cache, and disappears before the asynchronous write.
     */
    namesSanitize(name, sanitized);
    length = (uint8_t)strnlen(sanitized, 16u);
    namesRecordDomainKey(index, &domain, &key);
    memset(namesState.record, 0, sizeof(namesState.record));
    namesState.record[0] = 1u;
    namesState.record[1] = domain;
    namesState.record[2] = key;
    namesState.record[3] = length;
    memcpy(&namesState.record[4], sanitized, length);
    namesWriteLe32(&namesState.record[24], generation);
    namesWriteLe32(&namesState.record[28],
                   namesCrc(namesState.record, 28u));
}

static uint8_t namesRecordValid(uint16_t index,
                                uint32_t generation,
                                char out[NAMES_NAME_BYTES])
{
    uint8_t domain;
    uint8_t key;
    uint8_t length;

    namesRecordDomainKey(index, &domain, &key);
    length = namesState.record[3];
    if (namesState.record[0] != 1u || namesState.record[1] != domain ||
        namesState.record[2] != key || length > 16u ||
        namesReadLe32(&namesState.record[24]) != generation ||
        namesReadLe32(&namesState.record[28]) !=
            namesCrc(namesState.record, 28u)) {
        namesDefault(index, out);
        return 0u;
    }
    memset(out, 0, NAMES_NAME_BYTES);
    memcpy(out, &namesState.record[4], length);
    return 1u;
}

static void namesBuildHeader(uint8_t header,
                             uint32_t sequence,
                             uint8_t bank,
                             uint32_t bankCrc)
{
    uint8_t *bytes = namesState.header[header];

    /* Header serialization covers format, selected bank, and complete-bank CRC. */
    memset(bytes, 0, NAMES_HEADER_BYTES);
    memcpy(bytes, namesMagic, NAMES_MAGIC_BYTES);
    namesWriteLe16(&bytes[8], NAMES_FORMAT_VERSION);
    namesWriteLe16(&bytes[10], NAMES_HEADER_BYTES);
    namesWriteLe16(&bytes[12], NAMES_RECORD_BYTES);
    namesWriteLe16(&bytes[14], NAMES_RECORD_COUNT);
    namesWriteLe32(&bytes[16], sequence);
    bytes[20] = bank;
    namesWriteLe32(&bytes[24], bankCrc);
    namesWriteLe32(&bytes[28], namesCrc(bytes, 28u));
}

static uint8_t namesHeaderValid(uint8_t header)
{
    const uint8_t *bytes = namesState.header[header];

    return (uint8_t)(memcmp(bytes, namesMagic, NAMES_MAGIC_BYTES) == 0 &&
        namesReadLe16(&bytes[8]) == NAMES_FORMAT_VERSION &&
        namesReadLe16(&bytes[10]) == NAMES_HEADER_BYTES &&
        namesReadLe16(&bytes[12]) == NAMES_RECORD_BYTES &&
        namesReadLe16(&bytes[14]) == NAMES_RECORD_COUNT &&
        bytes[20] < 2u &&
        namesReadLe32(&bytes[28]) == namesCrc(bytes, 28u));
}

static void namesOpenComplete(afatfsResultCode_t result,
                              afatfsFilePtr_t file)
{
    namesState.openResult = result;
    namesState.file = file;
    namesState.openDone = 1u;
}

static void namesCloseComplete(void)
{
    namesState.closeDone = 1u;
}

static void namesFileOperationComplete(afatfsFilePtr_t file)
{
    /*
     * Complete an in-place file operation without changing handle ownership.
     *
     * ftruncate returns the same still-open file, unlike fclose's argumentless
     * callback. The pointer equality guards accidental completion from another
     * handle while the register remains serialized; output is one phase flag.
     */
    if (file == namesState.file)
        namesState.closeDone = 1u;
}

static afatfsOperationStatus_e namesReadBytes(uint8_t *buffer, uint8_t size)
{
    uint32_t count = afatfs_fread(namesState.file,
                                  buffer + namesState.ioOffset,
                                  (uint32_t)(size - namesState.ioOffset));
    namesState.ioOffset = (uint8_t)(namesState.ioOffset + count);
    if (namesState.ioOffset == size)
        return AFATFS_OPERATION_SUCCESS;
    if (count == 0u && afatfs_feof(namesState.file))
        return AFATFS_OPERATION_FAILURE;
    return AFATFS_OPERATION_IN_PROGRESS;
}

static afatfsOperationStatus_e namesWriteBytes(const uint8_t *buffer,
                                               uint8_t size)
{
    uint32_t count = afatfs_fwrite(namesState.file,
                                   buffer + namesState.ioOffset,
                                   (uint32_t)(size - namesState.ioOffset));
    namesState.ioOffset = (uint8_t)(namesState.ioOffset + count);
    return namesState.ioOffset == size ? AFATFS_OPERATION_SUCCESS
                                       : AFATFS_OPERATION_IN_PROGRESS;
}

static void namesBeginClose(namesRegisterResult_t result)
{
    namesState.terminalResult = result;
    namesState.phase = NAMES_PHASE_CLOSE_FILE;
}

static bool namesStart(namesJob_t job, namesRegisterCallback_t callback)
{
    uint8_t activeHeader = namesState.activeHeader;
    uint8_t activeBank = namesState.activeBank;
    uint32_t activeSequence = namesState.activeSequence;

    if (namesState.job != NAMES_JOB_NONE || !callback)
        return false;
    memset(&namesState, 0, sizeof(namesState));
    /*
     * Read/update requests retain only committed snapshot selectors across the
     * per-request reset. Mount deliberately starts from zero so stale geometry
     * cannot survive a remount; no resident name bytes are retained here.
     */
    if (job != NAMES_JOB_MOUNT) {
        namesState.activeHeader = activeHeader;
        namesState.activeBank = activeBank;
        namesState.activeSequence = activeSequence;
    }
    namesState.job = job;
    namesState.phase = NAMES_PHASE_OPEN_ROOT;
    namesState.callback = callback;
    namesState.terminalResult = NAMES_REGISTER_RESULT_IO_ERROR;
    return true;
}

bool namesRegister_startMount(namesRegisterCallback_t callback)
{
    return namesStart(NAMES_JOB_MOUNT, callback);
}

bool namesRegister_startRead(namesRegisterDomain_t domain,
                             uint8_t sceneIndex,
                             uint8_t voiceSlot,
                             namesRegisterCallback_t callback)
{
    uint16_t index;

    if (!namesRegister_recordIndex(domain, sceneIndex, voiceSlot, &index) ||
        namesState.activeSequence == 0u || !namesStart(NAMES_JOB_READ, callback))
        return false;
    namesState.requestedRecord = index;
    return true;
}

bool namesRegister_startUpdate(namesRegisterProvider_t provider,
                               void *context,
                               namesRegisterCallback_t callback)
{
    if (!provider || namesState.activeSequence == 0u ||
        !namesStart(NAMES_JOB_UPDATE, callback))
        return false;
    namesState.provider = provider;
    namesState.providerContext = context;
    return true;
}

bool namesRegister_busy(void)
{
    return namesState.job != NAMES_JOB_NONE;
}

const char *namesRegister_name(void)
{
    return namesState.name;
}

static void namesSelectMountCandidate(void)
{
    uint8_t first = 0u;

    namesState.headerValid[0] = namesHeaderValid(0u);
    namesState.headerValid[1] = namesHeaderValid(1u);
    if (!namesState.headerValid[0] && !namesState.headerValid[1]) {
        namesState.phase = NAMES_PHASE_INIT_TRUNCATE;
        return;
    }
    if (!namesState.headerValid[0] ||
        (namesState.headerValid[1] &&
         namesReadLe32(&namesState.header[1][16]) >
             namesReadLe32(&namesState.header[0][16]))) {
        first = 1u;
    }
    namesState.candidateHeader = first;
    namesState.activeBank = namesState.header[first][20];
    namesState.activeSequence = namesReadLe32(&namesState.header[first][16]);
    namesState.expectedBankCrc = namesReadLe32(&namesState.header[first][24]);
    namesState.runningBankCrc = 0xffffffffu;
    namesState.recordIndex = 0u;
    namesState.phase = NAMES_PHASE_MOUNT_SEEK_BANK;
}

static void namesRejectMountCandidate(void)
{
    uint8_t other = (uint8_t)(namesState.candidateHeader ^ 1u);

    namesState.headerValid[namesState.candidateHeader] = 0u;
    if (!namesState.headerValid[other]) {
        namesState.phase = NAMES_PHASE_INIT_TRUNCATE;
        return;
    }
    namesState.candidateHeader = other;
    namesState.activeBank = namesState.header[other][20];
    namesState.activeSequence = namesReadLe32(&namesState.header[other][16]);
    namesState.expectedBankCrc = namesReadLe32(&namesState.header[other][24]);
    namesState.runningBankCrc = 0xffffffffu;
    namesState.recordIndex = 0u;
    namesState.phase = NAMES_PHASE_MOUNT_SEEK_BANK;
}

void namesRegister_tick(void)
{
    afatfsOperationStatus_e status;

    if (namesState.job == NAMES_JOB_NONE)
        return;
    switch (namesState.phase) {
    case NAMES_PHASE_OPEN_ROOT:
        namesState.root = afatfs_openRoot();
        if (namesState.root)
            namesState.phase = NAMES_PHASE_OPEN_FILE;
        return;

    case NAMES_PHASE_OPEN_FILE:
        namesState.openDone = 0u;
        if (!afatfs_fopenChild(namesState.root, ".names", "r+",
                               AFATFS_CREATE_OR_OPEN,
                               AFATFS_MATCH_CASE_SENSITIVE, NULL,
                               namesOpenComplete))
            return;
        namesState.phase = NAMES_PHASE_WAIT_OPEN_FILE;
        return;

    case NAMES_PHASE_WAIT_OPEN_FILE:
        if (!namesState.openDone)
            return;
        if (namesState.openResult != AFATFS_RESULT_OK || !namesState.file) {
            namesBeginClose(NAMES_REGISTER_RESULT_IO_ERROR);
            return;
        }
        namesState.ioOffset = 0u;
        namesState.phase = namesState.job == NAMES_JOB_MOUNT
            ? NAMES_PHASE_MOUNT_SEEK_HEADER_A
            : namesState.job == NAMES_JOB_READ
                ? NAMES_PHASE_READ_SEEK_RECORD
                : NAMES_PHASE_UPDATE_SEEK_SOURCE;
        return;

    case NAMES_PHASE_MOUNT_SEEK_HEADER_A:
        status = afatfs_fseek(namesState.file, NAMES_HEADER_A_OFFSET,
                              AFATFS_SEEK_SET);
        if (status == AFATFS_OPERATION_SUCCESS) {
            namesState.ioOffset = 0u;
            namesState.phase = NAMES_PHASE_MOUNT_READ_HEADER_A;
        } else if (status == AFATFS_OPERATION_FAILURE) {
            namesState.phase = NAMES_PHASE_INIT_TRUNCATE;
        }
        return;

    case NAMES_PHASE_MOUNT_READ_HEADER_A:
        status = namesReadBytes(namesState.header[0], NAMES_HEADER_BYTES);
        if (status == AFATFS_OPERATION_SUCCESS)
            namesState.phase = NAMES_PHASE_MOUNT_SEEK_HEADER_B;
        else if (status == AFATFS_OPERATION_FAILURE)
            namesState.phase = NAMES_PHASE_INIT_TRUNCATE;
        return;

    case NAMES_PHASE_MOUNT_SEEK_HEADER_B:
        status = afatfs_fseek(namesState.file, NAMES_HEADER_B_OFFSET,
                              AFATFS_SEEK_SET);
        if (status == AFATFS_OPERATION_SUCCESS) {
            namesState.ioOffset = 0u;
            namesState.phase = NAMES_PHASE_MOUNT_READ_HEADER_B;
        } else if (status == AFATFS_OPERATION_FAILURE) {
            namesState.phase = NAMES_PHASE_INIT_TRUNCATE;
        }
        return;

    case NAMES_PHASE_MOUNT_READ_HEADER_B:
        status = namesReadBytes(namesState.header[1], NAMES_HEADER_BYTES);
        if (status == AFATFS_OPERATION_SUCCESS)
            namesState.phase = NAMES_PHASE_MOUNT_SELECT_HEADER;
        else if (status == AFATFS_OPERATION_FAILURE) {
            memset(namesState.header[1], 0, NAMES_HEADER_BYTES);
            namesState.phase = NAMES_PHASE_MOUNT_SELECT_HEADER;
        }
        return;

    case NAMES_PHASE_MOUNT_SELECT_HEADER:
        namesSelectMountCandidate();
        return;

    case NAMES_PHASE_MOUNT_SEEK_BANK:
        status = afatfs_fseek(namesState.file,
            (int32_t)(namesBankOffset(namesState.activeBank) +
                      (uint32_t)namesState.recordIndex * NAMES_RECORD_BYTES),
            AFATFS_SEEK_SET);
        if (status == AFATFS_OPERATION_SUCCESS) {
            namesState.ioOffset = 0u;
            namesState.phase = NAMES_PHASE_MOUNT_READ_BANK;
        } else if (status == AFATFS_OPERATION_FAILURE) {
            namesRejectMountCandidate();
        }
        return;

    case NAMES_PHASE_MOUNT_READ_BANK:
        status = namesReadBytes(namesState.record, NAMES_RECORD_BYTES);
        if (status == AFATFS_OPERATION_FAILURE) {
            namesRejectMountCandidate();
            return;
        }
        if (status != AFATFS_OPERATION_SUCCESS)
            return;
        namesState.runningBankCrc = namesCrcContinue(
            namesState.runningBankCrc, namesState.record, NAMES_RECORD_BYTES);
        namesState.recordIndex++;
        if (namesState.recordIndex < NAMES_RECORD_COUNT) {
            namesState.phase = NAMES_PHASE_MOUNT_SEEK_BANK;
            return;
        }
        if (~namesState.runningBankCrc != namesState.expectedBankCrc) {
            namesRejectMountCandidate();
            return;
        }
        namesState.activeHeader = namesState.candidateHeader;
        namesBeginClose(NAMES_REGISTER_RESULT_OK);
        return;

    case NAMES_PHASE_INIT_TRUNCATE:
        namesState.closeDone = 0u;
        if (!afatfs_ftruncate(namesState.file, namesFileOperationComplete))
            return;
        namesState.phase = NAMES_PHASE_INIT_WAIT_TRUNCATE;
        return;

    case NAMES_PHASE_INIT_WAIT_TRUNCATE:
        if (!namesState.closeDone)
            return;
        namesState.activeBank = 0u;
        namesState.activeHeader = 0u;
        namesState.activeSequence = 1u;
        namesState.recordIndex = 0u;
        namesState.runningBankCrc = 0xffffffffu;
        namesState.phase = NAMES_PHASE_INIT_SEEK_BANK;
        return;

    case NAMES_PHASE_INIT_SEEK_BANK:
        status = afatfs_fseek(namesState.file,
            (int32_t)(NAMES_BANK_A_OFFSET +
                      (uint32_t)namesState.recordIndex * NAMES_RECORD_BYTES),
            AFATFS_SEEK_SET);
        if (status == AFATFS_OPERATION_SUCCESS) {
            char fallback[NAMES_NAME_BYTES];
            namesDefault(namesState.recordIndex, fallback);
            namesBuildRecord(namesState.recordIndex, 1u, fallback);
            namesState.runningBankCrc = namesCrcContinue(
                namesState.runningBankCrc, namesState.record,
                NAMES_RECORD_BYTES);
            namesState.ioOffset = 0u;
            namesState.phase = NAMES_PHASE_INIT_WRITE_BANK;
        } else if (status == AFATFS_OPERATION_FAILURE) {
            namesBeginClose(NAMES_REGISTER_RESULT_IO_ERROR);
        }
        return;

    case NAMES_PHASE_INIT_WRITE_BANK:
        if (namesWriteBytes(namesState.record, NAMES_RECORD_BYTES) !=
            AFATFS_OPERATION_SUCCESS)
            return;
        namesState.recordIndex++;
        if (namesState.recordIndex < NAMES_RECORD_COUNT) {
            namesState.phase = NAMES_PHASE_INIT_SEEK_BANK;
        } else {
            namesState.expectedBankCrc = ~namesState.runningBankCrc;
            namesState.phase = NAMES_PHASE_INIT_SYNC_BANK;
        }
        return;

    case NAMES_PHASE_INIT_SYNC_BANK:
        if (!afatfs_sync())
            return;
        namesBuildHeader(0u, 1u, 0u, namesState.expectedBankCrc);
        namesState.phase = NAMES_PHASE_INIT_SEEK_HEADER;
        return;

    case NAMES_PHASE_INIT_SEEK_HEADER:
        status = afatfs_fseek(namesState.file, NAMES_HEADER_A_OFFSET,
                              AFATFS_SEEK_SET);
        if (status == AFATFS_OPERATION_SUCCESS) {
            namesState.ioOffset = 0u;
            namesState.phase = NAMES_PHASE_INIT_WRITE_HEADER;
        } else if (status == AFATFS_OPERATION_FAILURE) {
            namesBeginClose(NAMES_REGISTER_RESULT_IO_ERROR);
        }
        return;

    case NAMES_PHASE_INIT_WRITE_HEADER:
        if (namesWriteBytes(namesState.header[0], NAMES_HEADER_BYTES) ==
            AFATFS_OPERATION_SUCCESS)
            namesState.phase = NAMES_PHASE_INIT_SYNC_HEADER;
        return;

    case NAMES_PHASE_INIT_SYNC_HEADER:
        if (!afatfs_sync())
            return;
        namesBeginClose(NAMES_REGISTER_RESULT_OK);
        return;

    case NAMES_PHASE_READ_SEEK_RECORD:
        status = afatfs_fseek(namesState.file,
            (int32_t)(namesBankOffset(namesState.activeBank) +
                (uint32_t)namesState.requestedRecord * NAMES_RECORD_BYTES),
            AFATFS_SEEK_SET);
        if (status == AFATFS_OPERATION_SUCCESS) {
            namesState.ioOffset = 0u;
            namesState.phase = NAMES_PHASE_READ_RECORD;
        } else if (status == AFATFS_OPERATION_FAILURE) {
            namesBeginClose(NAMES_REGISTER_RESULT_IO_ERROR);
        }
        return;

    case NAMES_PHASE_READ_RECORD:
        status = namesReadBytes(namesState.record, NAMES_RECORD_BYTES);
        if (status == AFATFS_OPERATION_FAILURE) {
            namesBeginClose(NAMES_REGISTER_RESULT_IO_ERROR);
        } else if (status == AFATFS_OPERATION_SUCCESS) {
            uint8_t valid = namesRecordValid(namesState.requestedRecord,
                                             namesState.activeSequence,
                                             namesState.name);
            namesBeginClose(valid ? NAMES_REGISTER_RESULT_OK
                                  : NAMES_REGISTER_RESULT_DEFAULTED);
        }
        return;

    case NAMES_PHASE_UPDATE_SEEK_SOURCE:
        status = afatfs_fseek(namesState.file,
            (int32_t)(namesBankOffset(namesState.activeBank) +
                (uint32_t)namesState.recordIndex * NAMES_RECORD_BYTES),
            AFATFS_SEEK_SET);
        if (status == AFATFS_OPERATION_SUCCESS) {
            namesState.ioOffset = 0u;
            namesState.phase = NAMES_PHASE_UPDATE_READ_SOURCE;
        } else if (status == AFATFS_OPERATION_FAILURE) {
            namesBeginClose(NAMES_REGISTER_RESULT_IO_ERROR);
        }
        return;

    case NAMES_PHASE_UPDATE_READ_SOURCE:
        status = namesReadBytes(namesState.record, NAMES_RECORD_BYTES);
        if (status == AFATFS_OPERATION_FAILURE) {
            namesBeginClose(NAMES_REGISTER_RESULT_IO_ERROR);
            return;
        }
        if (status == AFATFS_OPERATION_SUCCESS) {
            char current[NAMES_NAME_BYTES];
            char replacement[NAMES_NAME_BYTES];
            uint32_t nextGeneration = namesState.activeSequence + 1u;

            (void)namesRecordValid(namesState.recordIndex,
                                   namesState.activeSequence, current);
            memset(replacement, 0, sizeof(replacement));
            if (namesState.provider(namesState.recordIndex, current,
                                    replacement,
                                    namesState.providerContext)) {
                namesBuildRecord(namesState.recordIndex, nextGeneration,
                                 replacement);
            } else {
                namesBuildRecord(namesState.recordIndex, nextGeneration,
                                 current);
            }
            namesState.runningBankCrc = namesState.recordIndex == 0u
                ? namesCrcContinue(0xffffffffu, namesState.record,
                                   NAMES_RECORD_BYTES)
                : namesCrcContinue(namesState.runningBankCrc,
                                   namesState.record, NAMES_RECORD_BYTES);
            namesState.phase = NAMES_PHASE_UPDATE_SEEK_DESTINATION;
        }
        return;

    case NAMES_PHASE_UPDATE_SEEK_DESTINATION:
        status = afatfs_fseek(namesState.file,
            (int32_t)(namesBankOffset((uint8_t)(namesState.activeBank ^ 1u)) +
                (uint32_t)namesState.recordIndex * NAMES_RECORD_BYTES),
            AFATFS_SEEK_SET);
        if (status == AFATFS_OPERATION_SUCCESS) {
            namesState.ioOffset = 0u;
            namesState.phase = NAMES_PHASE_UPDATE_WRITE_DESTINATION;
        } else if (status == AFATFS_OPERATION_FAILURE) {
            namesBeginClose(NAMES_REGISTER_RESULT_IO_ERROR);
        }
        return;

    case NAMES_PHASE_UPDATE_WRITE_DESTINATION:
        if (namesWriteBytes(namesState.record, NAMES_RECORD_BYTES) !=
            AFATFS_OPERATION_SUCCESS)
            return;
        namesState.recordIndex++;
        if (namesState.recordIndex < NAMES_RECORD_COUNT) {
            namesState.phase = NAMES_PHASE_UPDATE_SEEK_SOURCE;
        } else {
            namesState.expectedBankCrc = ~namesState.runningBankCrc;
            namesState.phase = NAMES_PHASE_UPDATE_SYNC_BANK;
        }
        return;

    case NAMES_PHASE_UPDATE_SYNC_BANK:
        if (!afatfs_sync())
            return;
        namesBuildHeader((uint8_t)(namesState.activeHeader ^ 1u),
                         namesState.activeSequence + 1u,
                         (uint8_t)(namesState.activeBank ^ 1u),
                         namesState.expectedBankCrc);
        namesState.phase = NAMES_PHASE_UPDATE_SEEK_HEADER;
        return;

    case NAMES_PHASE_UPDATE_SEEK_HEADER:
        status = afatfs_fseek(namesState.file,
            (int32_t)namesHeaderOffset((uint8_t)(namesState.activeHeader ^ 1u)),
            AFATFS_SEEK_SET);
        if (status == AFATFS_OPERATION_SUCCESS) {
            namesState.ioOffset = 0u;
            namesState.phase = NAMES_PHASE_UPDATE_WRITE_HEADER;
        } else if (status == AFATFS_OPERATION_FAILURE) {
            namesBeginClose(NAMES_REGISTER_RESULT_IO_ERROR);
        }
        return;

    case NAMES_PHASE_UPDATE_WRITE_HEADER:
        if (namesWriteBytes(namesState.header[namesState.activeHeader ^ 1u],
                            NAMES_HEADER_BYTES) == AFATFS_OPERATION_SUCCESS)
            namesState.phase = NAMES_PHASE_UPDATE_SYNC_HEADER;
        return;

    case NAMES_PHASE_UPDATE_SYNC_HEADER:
        if (!afatfs_sync())
            return;
        namesState.activeHeader ^= 1u;
        namesState.activeBank ^= 1u;
        namesState.activeSequence++;
        namesBeginClose(NAMES_REGISTER_RESULT_OK);
        return;

    case NAMES_PHASE_CLOSE_FILE:
        if (!namesState.file) {
            namesState.phase = NAMES_PHASE_CLOSE_ROOT;
            return;
        }
        namesState.closeDone = 0u;
        if (!afatfs_fclose(namesState.file, namesCloseComplete))
            return;
        namesState.phase = NAMES_PHASE_WAIT_CLOSE_FILE;
        return;

    case NAMES_PHASE_WAIT_CLOSE_FILE:
        if (!namesState.closeDone)
            return;
        namesState.file = NULL;
        namesState.phase = NAMES_PHASE_CLOSE_ROOT;
        return;

    case NAMES_PHASE_CLOSE_ROOT:
        if (!namesState.root) {
            namesState.phase = NAMES_PHASE_FINISH;
            return;
        }
        namesState.closeDone = 0u;
        if (!afatfs_fclose(namesState.root, namesCloseComplete))
            return;
        namesState.phase = NAMES_PHASE_WAIT_CLOSE_ROOT;
        return;

    case NAMES_PHASE_WAIT_CLOSE_ROOT:
        if (!namesState.closeDone)
            return;
        namesState.root = NULL;
        namesState.phase = NAMES_PHASE_FINISH;
        return;

    case NAMES_PHASE_FINISH:
    {
        namesRegisterCallback_t callback = namesState.callback;
        namesRegisterResult_t result = namesState.terminalResult;
        namesJob_t completedJob = namesState.job;
        uint8_t activeHeader = namesState.activeHeader;
        uint8_t activeBank = namesState.activeBank;
        uint32_t activeSequence = namesState.activeSequence;
        char resultName[NAMES_NAME_BYTES];

        memcpy(resultName, namesState.name, sizeof(resultName));
        memset(&namesState, 0, sizeof(namesState));
        if (completedJob == NAMES_JOB_MOUNT &&
            result != NAMES_REGISTER_RESULT_OK) {
            activeHeader = 0u;
            activeBank = 0u;
            activeSequence = 0u;
        }
        if (completedJob == NAMES_JOB_MOUNT ||
            completedJob == NAMES_JOB_UPDATE) {
            namesState.activeHeader = activeHeader;
            namesState.activeBank = activeBank;
            namesState.activeSequence = activeSequence;
        } else {
            namesState.activeHeader = activeHeader;
            namesState.activeBank = activeBank;
            namesState.activeSequence = activeSequence;
            memcpy(namesState.name, resultName, sizeof(resultName));
        }
        callback(result);
        return;
    }

    default:
        namesBeginClose(NAMES_REGISTER_RESULT_IO_ERROR);
        return;
    }
}
