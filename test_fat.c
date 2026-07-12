#include <stdio.h>
#include <string.h>

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

int main() {
    char out[20];
    
    char fat1[11] = {'0','0','1','_','S','L','A','~','C','Y','M'}; // 001_SLA~.CYM? No, 8 chars: 001_SLA~ (8), CYM (3)
    fat_convertFATStyleToFilename(fat1, out);
    printf("fat1: '%s'\n", out);

    char fat2[11] = {'A','B','C',' ',' ',' ',' ',' ','T','X','T'};
    fat_convertFATStyleToFilename(fat2, out);
    printf("fat2: '%s'\n", out);

    return 0;
}
