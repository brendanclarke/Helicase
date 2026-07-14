#include <ctype.h>

#include "fat_standard.h"

bool fat16_isEndOfChainMarker(uint16_t clusterNumber)
{
    return clusterNumber >= 0xFFF8;
}

// Pass the cluster number after fat32_decodeClusterNumber().
bool fat32_isEndOfChainMarker(uint32_t clusterNumber)
{
    return clusterNumber >= 0x0FFFFFF8;
}

/**
 * FAT32 cluster numbers are really only 28 bits, and the top 4 bits must be left alone and not treated as part of the
 * cluster number (so various FAT drivers can use those bits for their own purposes, or they can be used in later
 * extensions)
 */
uint32_t fat32_decodeClusterNumber(uint32_t clusterNumber)
{
    return clusterNumber & 0x0FFFFFFF;
}

// fat32 needs fat32_decodeClusterNumber() applied first.
bool fat_isFreeSpace(uint32_t clusterNumber)
{
    return clusterNumber == 0;
}

bool fat_isDirectoryEntryTerminator(fatDirectoryEntry_t *entry)
{
    return entry->filename[0] == 0x00;
}

bool fat_isDirectoryEntryEmpty(fatDirectoryEntry_t *entry)
{
    return (unsigned char) entry->filename[0] == FAT_DELETED_FILE_MARKER;
}

bool fat_isLongDirectoryEntry(const fatDirectoryEntry_t *entry)
{
    /*
     * Identify VFAT long filename fragments before ordinary object handling.
     *
     * The low four attribute bits equal 0x0f for an LFN entry. The upper bits
     * are not identity, so masking keeps the check tolerant of media written by
     * host drivers that preserve reserved bits differently.
     */
    return entry && ((entry->attrib & FAT_FILE_ATTRIBUTE_LFN) ==
                     FAT_FILE_ATTRIBUTE_LFN);
}

uint8_t fat_lfnChecksum(const uint8_t fatFilename[FAT_FILENAME_LENGTH])
{
    uint8_t sum = 0u;

    /*
     * Standard VFAT checksum tying an LFN fragment chain to one SFN entry.
     *
     * Every long-name fragment stores this value. Scanners must compare it
     * against the following short entry before trusting reconstructed display
     * text; otherwise stale fragments from a deleted file could name the wrong
     * object.
     */
    for (uint8_t i = 0u; i < FAT_FILENAME_LENGTH; i++)
        sum = (uint8_t)(((sum & 1u) ? 0x80u : 0u) + (sum >> 1u) +
                        fatFilename[i]);
    return sum;
}

bool fat_lfnCharAllowed(char c)
{
    /*
     * Validate the ASCII subset used by firmware-created VFAT names.
     *
     * FAT LFN storage is UTF-16LE, but the front panel editor and current
     * storage schema are printable ASCII. Rejecting slash and FAT-forbidden
     * punctuation here prevents one component API from accidentally accepting a
     * path or a name a desktop FAT driver would reject.
     */
    if (c < 0x20 || c > 0x7e)
        return false;
    switch (c) {
    case '"':
    case '*':
    case '/':
    case ':':
    case '<':
    case '>':
    case '?':
    case '\\':
    case '|':
    case 0x7f:
        return false;
    default:
        return true;
    }
}

char fat_lfnSanitizeChar(char c)
{
    /*
     * Convert unsupported display characters to a visible safe placeholder.
     *
     * Writers use this before encoding UTF-16LE fragments. Scanners do not use
     * it to silently accept arbitrary Unicode; unsupported host-created bytes
     * are handled by the scanner's substitution policy.
     */
    return fat_lfnCharAllowed(c) ? c : '_';
}

static char fat_compareFold(char c)
{
    /*
     * ASCII-only case fold used when a caller asks for compatibility matching.
     *
     * The new File/Dir test paths request case-sensitive matching, but keeping
     * this helper lets legacy wrappers keep ordinary FAT case-insensitive
     * behavior without teaching callers how VFAT display strings are stored.
     */
    if (c >= 'A' && c <= 'Z')
        return (char)(c + ('a' - 'A'));
    return c;
}

int8_t fat_compareDisplayName(const char *a, const char *b,
                              bool case_sensitive)
{
    /*
     * Compare two reconstructed display components under a declared policy.
     *
     * Inputs are NUL-terminated ASCII component names, never paths. Output is
     * strcmp-style ordering so callers can use the same helper for exact opens
     * and alphanumeric menu sorting. Case-sensitive mode is byte-for-byte and
     * is the policy required by the File/Dir asyncfatfs expansion tests.
     */
    if (!a)
        a = "";
    if (!b)
        b = "";

    for (;;) {
        char ca = *a++;
        char cb = *b++;
        if (!case_sensitive) {
            ca = fat_compareFold(ca);
            cb = fat_compareFold(cb);
        }
        if (ca != cb)
            return (ca < cb) ? -1 : 1;
        if (ca == '\0')
            return 0;
    }
}

int8_t fat_compareDisplayNameCasefoldThenCase(const char *a, const char *b)
{
    const char *rawA;
    const char *rawB;

    /*
     * Normalize NULLs once so both comparison passes can treat missing inputs
     * as empty display components. Callers use this for scanned FAT names and
     * save-target names, neither of which should be a path or a NULL pointer
     * once validation has succeeded, but this keeps the helper total.
     */
    if (!a)
        a = "";
    if (!b)
        b = "";
    rawA = a;
    rawB = b;

    /*
     * First pass: compare case-folded bytes.
     *
     * This pass implements the universal case-insensitive product identity. If
     * the folded bytes differ, the names are different product objects and
     * normal alphabetical order decides their relative position.
     */
    for (;;) {
        char ca = fat_compareFold(*a++);
        char cb = fat_compareFold(*b++);
        if (ca != cb)
            return (ca < cb) ? -1 : 1;
        if (ca == '\0')
            break;
    }

    /*
     * Second pass: compare original bytes only after folded equality.
     *
     * This does not make the product identity case-sensitive. It only chooses
     * the display winner when an externally edited card already contains
     * duplicate same-casefold names. ASCII uppercase letters sort before
     * lowercase letters, matching the required capital-first policy.
     */
    for (;;) {
        char ca = *rawA++;
        char cb = *rawB++;
        if (ca != cb)
            return (ca < cb) ? -1 : 1;
        if (ca == '\0')
            return 0;
    }
}

uint8_t fat_calculateFilenameCaseFlags(const char *filename)
{
    uint8_t flags = 0u;
    uint8_t baseHasAlpha = 0u;
    uint8_t baseAllLower = 1u;
    uint8_t extHasAlpha = 0u;
    uint8_t extAllLower = 1u;
    uint8_t i;

    /*
     * Derive the ntReserved lowercase-display flags for a caller-supplied 8.3
     * component before fat_convertFilenameToFATStyle() uppercases the raw SFN.
     *
     * FAT has only one lowercase bit for the base and one for the extension,
     * so this helper preserves all-lowercase system names such as kitset.kcg
     * while deliberately refusing to claim mixed-case names are preserved.
     * Mixed-case display requires VFAT LFN entries.
     */
    if (!filename)
        return 0u;

    /*
     * Only the characters that can fit into the raw 8.3 base/extension
     * participate in the case decision. The converter truncates beyond those
     * windows, so looking farther ahead would let discarded characters change
     * the display flags for the stored name.
     */
    for (i = 0u; i < 8u && filename[i] != '\0' && filename[i] != '.'; i++) {
        unsigned char c = (unsigned char)filename[i];
        if (isalpha(c)) {
            baseHasAlpha = 1u;
            if (!islower(c))
                baseAllLower = 0u;
        }
    }
    filename += i;
    if (*filename == '.')
        filename++;

    for (i = 0u; i < 3u && filename[i] != '\0'; i++) {
        unsigned char c = (unsigned char)filename[i];
        if (isalpha(c)) {
            extHasAlpha = 1u;
            if (!islower(c))
                extAllLower = 0u;
        }
    }

    if (baseHasAlpha && baseAllLower)
        flags |= FAT_NTRES_LOWERCASE_BASE;
    if (extHasAlpha && extAllLower)
        flags |= FAT_NTRES_LOWERCASE_EXT;
    return flags;
}

void fat_applyFilenameCaseFlags(char *filename, uint8_t ntReserved)
{
    /*
     * Apply FAT short-name lowercase-display bits after converting raw 8.3
     * text to printable prefix.ext form. This is display metadata only:
     * lookups remain case-insensitive and the raw directory entry remains
     * uppercase.
     */
    if (!filename)
        return;
    if (ntReserved & FAT_NTRES_LOWERCASE_BASE) {
        for (uint8_t i = 0u; filename[i] != '\0' && filename[i] != '.'; i++) {
            if (filename[i] >= 'A' && filename[i] <= 'Z')
                filename[i] = (char)(filename[i] + ('a' - 'A'));
        }
    }

    if (ntReserved & FAT_NTRES_LOWERCASE_EXT) {
        uint8_t i = 0u;
        while (filename[i] != '\0' && filename[i] != '.')
            i++;
        if (filename[i] == '.')
            i++;
        while (filename[i] != '\0') {
            if (filename[i] >= 'A' && filename[i] <= 'Z')
                filename[i] = (char)(filename[i] + ('a' - 'A'));
            i++;
        }
    }
}

/**
 * Convert the given "prefix.ext" style filename to the FAT format to be stored on disk.
 *
 * fatFilename must point to a buffer which is FAT_FILENAME_LENGTH bytes long. The buffer is not null-terminated.
 */
void fat_convertFilenameToFATStyle(const char *filename, uint8_t *fatFilename)
{
    for (int i = 0; i < 8; i++) {
        if (*filename == '\0' || *filename == '.') {
            *fatFilename = ' ';
        } else {
            *fatFilename = toupper((unsigned char)*filename);
            filename++;
        }
        fatFilename++;
    }

    if (*filename == '.') {
        filename++;
    }

    for (int i = 0; i < 3; i++) {
         if (*filename == '\0') {
             *fatFilename = ' ';
         } else {
             *fatFilename = toupper((unsigned char)*filename);
             filename++;
         }
         fatFilename++;
     }
}

/**
 * Convert the FAT format stored on disk to the style filename given as "prefix.ext".
 *
 * filename must point to a buffer which is FAT_FILENAME_LENGTH + 1 bytes long. The buffer IS null-terminated.
 */
void fat_convertFATStyleToFilename(const char *fatFilename, char *filename)
{
    for (int i = 0; i < 8; i++) {
        if (*fatFilename == ' ') { //*filename == '\0' || *filename == '.') {
            *filename = '\0';
        } else {
            *filename = *fatFilename;
            filename++;
        }
        fatFilename++;
    }

    if (*fatFilename != ' ')
    {
      *filename = '.';
      ++filename;
    }

    for (int i = 0; i < 3; i++) {
         if (*fatFilename == ' ') {
             *filename = '\0';
         } else {
             *filename = *fatFilename;
             fatFilename++;
         }
         filename++;
     }
     *filename = '\0';
}
