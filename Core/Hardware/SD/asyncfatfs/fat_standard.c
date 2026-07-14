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
