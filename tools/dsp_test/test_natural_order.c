/*
 * Natural-order and display-name host gate (S087 stage C).
 *
 * What:       the real storage_naturalCompare() and
 *             storage_displayNameFromLfn().
 *             Laws:
 *               1. digits compare as numbers: w2 < w10, 01_cut == 1_cut in
 *                  numeric value (the remaining text decides);
 *               2. case-insensitive text order;
 *               3. a leading numeric prefix plus '_', '-' or ' ' is stripped,
 *                  the extension is dropped when asked, and the name is padded
 *                  to 8 characters.
 * Why:        S087 P0 §5.2: install order and the 8-character catalogue names
 *             depend on these two rules.
 * Inputs:     gen/natural_order.c.
 * Outputs:    "natural_order failures: N"; exit 1 if N > 0.
 * Accessors:  make -C tools/dsp_test natural_order.
 * Affiliates: storageTypes.c, tools/sampleoptimize.py natural_key().
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "natural_order.c"

static int failures;

static void expect(int cond, const char *msg)
{
    if (!cond) {
        if (failures < 40)
            printf("FAIL: %s\n", msg);
        failures++;
    }
}

int main(void)
{
    char name[8];

    expect(storage_naturalCompare("w2", "w10") < 0, "law 1: w2 < w10");
    expect(storage_naturalCompare("w10", "w2") > 0, "law 1: w10 > w2");
    expect(storage_naturalCompare("01_cut", "01_cut") == 0, "law 1: equal");
    expect(storage_naturalCompare("AKWF_hvoice_0001.wav",
                                  "AKWF_hvoice_0002.wav") < 0,
           "law 1: zero-padded frame order");
    expect(storage_naturalCompare("Saw", "saw") == 0, "law 2: case-insensitive");
    expect(storage_naturalCompare("abc", "abd") < 0, "law 2: text order");

    storage_displayNameFromLfn(name, "01_cut.wav", 1u);
    expect(memcmp(name, "cut     ", 8u) == 0, "law 3: prefix and extension");
    storage_displayNameFromLfn(name, "ak_hvox", 0u);
    expect(memcmp(name, "ak_hvox ", 8u) == 0, "law 3: table folder name");
    storage_displayNameFromLfn(name, "2-wave.wav", 1u);
    expect(memcmp(name, "wave    ", 8u) == 0, "law 3: dash prefix");
    storage_displayNameFromLfn(name, "3 wave.wav", 1u);
    expect(memcmp(name, "wave    ", 8u) == 0, "law 3: space prefix");
    storage_displayNameFromLfn(name, "longer_than_eight.wav", 1u);
    expect(memcmp(name, "longer_t", 8u) == 0, "law 3: truncation");

    printf("natural_order failures: %d\n", failures);
    return failures ? 1 : 0;
}
