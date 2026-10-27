/*
 * Natural-order and ordering-prefix check (S085 C17.3).
 *
 * What:       the C3.1/C3.2 card-order rules on the real storageTypes.c
 *             functions: numeric runs, case folding, zero ties, and the
 *             display-prefix strip.
 * Why:        G1 gate; the installer's file order depends on it.
 * Inputs:     build/gen/nat.c extracted from Core/Hardware/SD/storageTypes.c.
 * Outputs:    "N failures".
 * Accessors:  make -C tools/dsp_test natural_order.
 * Affiliates: filesystem_installCollect(), storage_stripOrderPrefix().
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

int8_t storage_naturalCompare(const char *a, const char *b);
const char *storage_stripOrderPrefix(const char *name);

static int failures;

static void less(const char *a, const char *b)
{
    if (storage_naturalCompare(a, b) >= 0) {
        printf("FAIL %s < %s\n", a ? a : "NULL", b ? b : "NULL");
        failures++;
    }
}

static void prefix(const char *in, const char *want)
{
    const char *got = storage_stripOrderPrefix(in);

    if (!got || strcmp(got, want) != 0) {
        printf("FAIL prefix(%s) = %s, want %s\n", in,
               got ? got : "NULL", want);
        failures++;
    }
}

int main(void)
{
    less("f2", "f10");
    less("a", "B");
    less("x1", "x01");
    less("frame 9", "frame 10");
    less("", "a");
    less(NULL, "a");
    if (storage_naturalCompare("f2", "f2") != 0) {
        printf("FAIL equal strings\n");
        failures++;
    }
    prefix("003 hvoice", "hvoice");
    prefix("12_x", "x");
    prefix("12", "12");
    prefix("12 ", "12 ");
    prefix("abc", "abc");
    printf("%d failures\n", failures);
    return failures ? 1 : 0;
}
