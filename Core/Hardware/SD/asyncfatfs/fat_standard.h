#pragma once

#include <stdint.h>
#include <stdbool.h>

#define MBR_PARTITION_TYPE_FAT16     0x06
#define MBR_PARTITION_TYPE_FAT32     0x0B
#define MBR_PARTITION_TYPE_FAT32_LBA 0x0C
#define MBR_PARTITION_TYPE_FAT16_LBA 0x0E

// Signature bytes found at index 510 and 511 in the volume ID sector
#define FAT_VOLUME_ID_SIGNATURE_1 0x55
#define FAT_VOLUME_ID_SIGNATURE_2 0xAA

#define FAT_DIRECTORY_ENTRY_SIZE 32
#define FAT_SMALLEST_LEGAL_CLUSTER_NUMBER 2

#define FAT_MAXIMUM_FILESIZE 0xFFFFFFFF

#define FAT12_MAX_CLUSTERS 4084
#define FAT16_MAX_CLUSTERS 65524

#define FAT_FILE_ATTRIBUTE_READ_ONLY 0x01
#define FAT_FILE_ATTRIBUTE_HIDDEN    0x02
#define FAT_FILE_ATTRIBUTE_SYSTEM    0x04
#define FAT_FILE_ATTRIBUTE_VOLUME_ID 0x08
#define FAT_FILE_ATTRIBUTE_DIRECTORY 0x10
#define FAT_FILE_ATTRIBUTE_ARCHIVE   0x20
#define FAT_FILE_ATTRIBUTE_LFN       0x0fu

#define FAT_FILENAME_LENGTH 11
#define FAT_DELETED_FILE_MARKER 0xE5
#define FAT_LFN_LAST_LONG_ENTRY 0x40u
#define FAT_LFN_CHARS_PER_ENTRY 13u

/*
 * FAT short-name case preservation bits.
 *
 * Raw 8.3 names are stored uppercase in directoryEntry.filename. These bits in
 * directoryEntry.ntReserved tell FAT-aware readers to display the base and/or
 * extension as lowercase. They do not make FAT lookups case-sensitive and they
 * cannot represent mixed-case text; exact mixed-case display must use VFAT LFN
 * entries.
 */
#define FAT_NTRES_LOWERCASE_BASE 0x08u
#define FAT_NTRES_LOWERCASE_EXT  0x10u

#define FAT_MAKE_DATE(year, month, day)     (day | (month << 5) | ((year - 1980) << 9))
#define FAT_MAKE_TIME(hour, minute, second) ((second / 2) | (minute << 5) | (hour << 11))

typedef enum {
    FAT_FILESYSTEM_TYPE_INVALID,
    FAT_FILESYSTEM_TYPE_FAT12,
    FAT_FILESYSTEM_TYPE_FAT16,
    FAT_FILESYSTEM_TYPE_FAT32,
} fatFilesystemType_e;

typedef struct mbrPartitionEntry_t {
    uint8_t bootFlag;
    uint8_t chsBegin[3];
    uint8_t type;
    uint8_t chsEnd[3];
    uint32_t lbaBegin;
    uint32_t numSectors;
} __attribute__((packed)) mbrPartitionEntry_t;

typedef struct fat16Descriptor_t {
    uint8_t driveNumber;
    uint8_t reserved1;
    uint8_t bootSignature;
    uint32_t volumeID;
    char volumeLabel[11];
    char fileSystemType[8];
} __attribute__((packed)) fat16Descriptor_t;

typedef struct fat32Descriptor_t {
    uint32_t FATSize32;
    uint16_t extFlags;
    uint16_t fsVer;
    uint32_t rootCluster;
    uint16_t fsInfo;
    uint16_t backupBootSector;
    uint8_t reserved[12];
    uint8_t driveNumber;
    uint8_t reserved1;
    uint8_t bootSignature;
    uint32_t volumeID;
    char volumeLabel[11];
    char fileSystemType[8];
} __attribute__((packed)) fat32Descriptor_t;

typedef struct fatVolumeID_t {
    uint8_t jmpBoot[3];
    char oemName[8];
    uint16_t bytesPerSector;
    uint8_t sectorsPerCluster;
    uint16_t reservedSectorCount;
    uint8_t numFATs;
    uint16_t rootEntryCount;
    uint16_t totalSectors16;
    uint8_t media;
    uint16_t FATSize16;
    uint16_t sectorsPerTrack;
    uint16_t numHeads;
    uint32_t hiddenSectors;
    uint32_t totalSectors32;
    union {
        fat16Descriptor_t fat16;
        fat32Descriptor_t fat32;
    } fatDescriptor;
} __attribute__((packed)) fatVolumeID_t;

typedef struct fatDirectoryEntry_t {
    char filename[FAT_FILENAME_LENGTH];
    uint8_t attrib;
    uint8_t ntReserved;
    uint8_t creationTimeTenths;
    uint16_t creationTime;
    uint16_t creationDate;
    uint16_t lastAccessDate;
    uint16_t firstClusterHigh;
    uint16_t lastWriteTime;
    uint16_t lastWriteDate;
    uint16_t firstClusterLow;
    uint32_t fileSize;
} __attribute__((packed)) fatDirectoryEntry_t;

uint32_t fat32_decodeClusterNumber(uint32_t clusterNumber);

bool fat32_isEndOfChainMarker(uint32_t clusterNumber);
bool fat16_isEndOfChainMarker(uint16_t clusterNumber);

bool fat_isFreeSpace(uint32_t clusterNumber);

bool fat_isDirectoryEntryTerminator(fatDirectoryEntry_t *entry);
bool fat_isDirectoryEntryEmpty(fatDirectoryEntry_t *entry);
/*
 * VFAT long-name helpers shared by asyncfatfs scanners and writers.
 *
 * Why these live beside the FAT directory structs: long filename fragments are
 * part of the on-disk FAT directory grammar, not a Kit/Scene storage rule.
 * Inputs are raw 8.3 directory names or ASCII display components. Outputs are
 * the checksum/comparison decisions used to bind an LFN chain to the following
 * SFN entry. Callers: asyncfatfs create/open matching, object enumeration, and
 * any future delete/rename path that must touch the whole VFAT entry chain.
 */
bool fat_isLongDirectoryEntry(const fatDirectoryEntry_t *entry);
uint8_t fat_lfnChecksum(const uint8_t fatFilename[FAT_FILENAME_LENGTH]);
bool fat_lfnCharAllowed(char c);
char fat_lfnSanitizeChar(char c);
int8_t fat_compareDisplayName(const char *a, const char *b,
                              bool case_sensitive);

uint8_t fat_calculateFilenameCaseFlags(const char *filename);
void fat_applyFilenameCaseFlags(char *filename, uint8_t ntReserved);
void fat_convertFilenameToFATStyle(const char *filename, uint8_t *fatFilename);
void fat_convertFATStyleToFilename(const char *fatFilename, char *filename);
