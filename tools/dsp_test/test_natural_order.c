/*
 * Natural-order name comparison (S084 VX2 VX2-10.1).
 *
 * What:       proves the real storage_naturalCompare()/storage_stripOrderPrefix()
 *             text: numeric runs order by value, zero padding is a tie-break,
 *             case is folded, and the folder order prefix is stripped.
 * Why:        the installer's frame and sample order depends on these rules.
 * Inputs:     generated storageTypes fragment.
 * Outputs:    failure count; exits nonzero on any failure.
 * Accessors:  make -C tools/dsp_test natural_order.
 * Affiliates: Core/Hardware/SD/storageTypes.c.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "storage_natural.c"

static unsigned failures;

static void check(int condition, const char *what)
{
    if (!condition) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

static void expectOrder(const char *a, const char *b)
{
    if (storage_naturalCompare(a, b) > 0) {
        printf("FAIL: expected %s before %s\n", a, b);
        failures++;
    }
    if (storage_naturalCompare(b, a) <= 0) {
        printf("FAIL: expected %s after %s\n", b, a);
        failures++;
    }
}

int main(void)
{
    char names[200][12];
    unsigned i;
    unsigned j;

    expectOrder("f2", "f10");
    expectOrder("f2", "f02");
    check(storage_naturalCompare("A1", "a1") == 0, "case folded");
    expectOrder("frame9.wav", "frame10.wav");
    expectOrder("x", "x0");
    check(storage_naturalCompare("", "") == 0, "empty equal");
    check(storage_naturalCompare("", "a") < 0, "empty first");
    check(storage_naturalCompare("a", "") > 0, "empty last");

    {
        char long_a[49];
        char long_b[49];
        memset(long_a, 'a', 48);
        memset(long_b, 'a', 48);
        long_a[48] = '\0';
        long_b[48] = '\0';
        check(storage_naturalCompare(long_a, long_b) == 0, "48-char equal");
    }

    check(strcmp(storage_stripOrderPrefix("000 hvoice"), "hvoice") == 0,
          "strip 000 hvoice");
    check(strcmp(storage_stripOrderPrefix("12_a"), "a") == 0, "strip 12_a");
    check(strcmp(storage_stripOrderPrefix("123"), "123") == 0,
          "bare digits unchanged");
    check(strcmp(storage_stripOrderPrefix("2024 mix"), "mix") == 0,
          "strip 2024 mix");
    check(strcmp(storage_stripOrderPrefix("abc"), "abc") == 0,
          "no prefix unchanged");

    /* Total and transitive on a deterministic pseudo-random set. */
    for (i = 0u; i < 200u; i++) {
        unsigned value = (i * 37u) % 997u;

        sprintf(names[i], "%c%d%s", (char)('a' + (i % 5u)), (int)value,
                (i & 1u) ? "x" : "y");
        for (j = 0u; j < i; j++) {
            const int8_t c = storage_naturalCompare(names[i], names[j]);
            const int8_t r = storage_naturalCompare(names[j], names[i]);

            if ((c == 0) != (r == 0) || (c < 0) != (r > 0)) {
                printf("FAIL: antisymmetry %s / %s\n", names[i], names[j]);
                failures++;
                break;
            }
        }
    }
    for (i = 0u; i < 200u; i += 7u) {
        for (j = 0u; j < 200u; j += 11u) {
            unsigned k;

            for (k = 0u; k < 200u; k += 13u) {
                const int8_t ab = storage_naturalCompare(names[i], names[j]);
                const int8_t bc = storage_naturalCompare(names[j], names[k]);
                const int8_t ac = storage_naturalCompare(names[i], names[k]);

                if (ab <= 0 && bc <= 0 && ac > 0) {
                    printf("FAIL: transitivity %s %s %s\n",
                           names[i], names[j], names[k]);
                    failures++;
                }
            }
        }
    }

    printf("natural_order %s (%u failures)\n", failures ? "FAILED" : "OK",
           failures);
    return failures ? 1 : 0;
}
