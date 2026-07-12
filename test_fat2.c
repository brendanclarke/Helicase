#include <stdio.h>
#include <string.h>
#include <stdint.h>

void fat_convertFATStyleToFilename(const char *fatFilename, char *filename)
{
    for (int i = 0; i < 8; i++) {
        if (*fatFilename == ' ') {
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

static char instrumentManager_lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c;
}

uint8_t instrumentManager_filenameMatchesType(const char *filename, const char *ext)
{
    uint8_t name_len = 0u;
    uint8_t ext_len = 0u;
    uint8_t i;
    if (!filename || !ext)
        return 0u;
    while (filename[name_len] != '\0')
        name_len++;
    while (ext[ext_len] != '\0')
        ext_len++;
    if (name_len < ext_len)
        return 0u;
    for (i = 0u; i < ext_len; i++) {
        if (instrumentManager_lower(filename[name_len - ext_len + i]) !=
            instrumentManager_lower(ext[i])) {
            return 0u;
        }
    }
    return 1u;
}

int main() {
    char out[20];
    char fat1[11] = {'0','0','1','_','S','L','A','K','C','Y','M'}; 
    fat_convertFATStyleToFilename(fat1, out);
    printf("out: '%s'\n", out);
    uint8_t match = instrumentManager_filenameMatchesType(out, ".cym");
    printf("match: %d\n", match);
    return 0;
}
