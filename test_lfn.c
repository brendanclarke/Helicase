#include <stdio.h>
#include <stdint.h>
#include <string.h>

typedef enum {
    INSTRUMENT_TYPE_DRM = 0
} storage_instrument_type_t;

static char storage_displayFilenameChar(char src)
{
    return (src < ' ' || src > '~' || src == '*' || src == '?' || src == ':' ||
            src == '/' || src == '\\')
               ? '_'
               : src;
}

void storage_makeSavedInstrumentDisplayFilename(char *dst,
                                                uint8_t capacity,
                                                const char *stem,
                                                storage_instrument_type_t type,
                                                uint8_t one_based_voice,
                                                uint8_t force_voice_suffix)
{
    uint8_t pos = 0u;
    uint8_t last_meaningful = 0u;
    uint8_t stem_limit = 8; // SCENE_INSTRUMENT_STEM_LEN
    const char *ext = "";

    if (!dst || capacity < 2u)
        return;

    while (stem[pos] != '\0' && stem[pos] != '.' &&
           pos + 1u < capacity && pos < stem_limit) {
        char c = storage_displayFilenameChar(stem[pos]);
        dst[pos] = c;
        if (c != ' ' && c != '.')
            last_meaningful = (uint8_t)(pos + 1u);
        pos++;
    }

    pos = last_meaningful;

    if (force_voice_suffix && capacity > 9u) {
        while (pos < 7u)
            dst[pos++] = ' ';
        dst[pos++] = (one_based_voice >= 1u && one_based_voice <= 9u)
            ? (char)('0' + one_based_voice) : 'x';
    }

    if (pos + 4u < capacity) {
        dst[pos++] = '.';
        switch (type) {
        case INSTRUMENT_TYPE_DRM: ext = "drm"; break;
        default: break;
        }
        while (*ext != '\0')
            dst[pos++] = *ext++;
    }

    dst[pos] = '\0';
}

int main() {
    char lfn[13];
    storage_makeSavedInstrumentDisplayFilename(lfn, sizeof(lfn), "Kick 01 ", INSTRUMENT_TYPE_DRM, 0u, 0u);
    printf("LFN for 'Kick 01 ': '%s'\n", lfn);

    storage_makeSavedInstrumentDisplayFilename(lfn, sizeof(lfn), "1SHTSND1", INSTRUMENT_TYPE_DRM, 0u, 0u);
    printf("LFN for '1SHTSND1': '%s'\n", lfn);
    return 0;
}
