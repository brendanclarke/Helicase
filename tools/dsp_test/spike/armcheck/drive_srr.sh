#!/bin/sh
# D+S Mix insert cost probe (S084 VX3 remediation §18.4; S086 eval §2.3).
#
# What:       writes build/ds_probe.c, which includes InsertDriveSrr.c and wraps
#             its process path (size 32) in a noinline probe function; compiles
#             it with the firmware -Ofast flags; looplen.py then reports the
#             probe's loops (the drive and drive-0 paths show as separate loops).
# Inputs:     $1 = ARM compiler command line (ARMCC ARMFLAGS), $2 = the source
#             root (src/ for the spike text, or the repository after a retry).
# Accessors:  make -C tools/dsp_test/spike armcheck-ds.
set -e
CC="$1"
SRC="$2"
IE="$SRC/Core/DSP/InsertEffects"
printf '#include "%s/InsertDriveSrr.c"\n__attribute__((noinline)) void probe_InsertDriveSrr(void *s, int16_t *buf, const insert_block_ctx_t *c) { driveSrr_process(s,buf,32,c); }\n' \
  "$IE" > build/ds_probe.c
$CC -fno-unroll-loops -I"$SRC/Core/DSP/Effects" -c build/ds_probe.c -o build/ds_probe.o
