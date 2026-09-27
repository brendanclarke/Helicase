#!/usr/bin/env python3
"""
Link budget report for the LXR-02 application image (Session 072).

What: reads linker symbols from the ELF via arm-none-eabi-nm and prints flash
use vs the 480 KiB application region, ITCM use, DTCM statics, and the size of
the .dtcm_fxbuf FX/voice audio arena.

Why: every Phase 5 step must be measured, and flash headroom is the tightest
Phase 5 risk. `size` alone cannot show headroom, and its Berkeley `bss` column
includes the NOLOAD arena, which is misleading.

Inputs: argv[1] = nm executable, argv[2] = ELF path. Optional env
LINK_BUDGET_WARN_FLASH (bytes, default 16384) prints a warning below it.
Output is text on stdout; exit status is always 0 because linker ASSERTs are
the enforcing guards. Affiliates: the linker symbols and FxBuffer.h.
"""
import os
import subprocess
import sys

FLASH_ORIGIN = 0x08008000
FLASH_LIMIT = 0x08080000
ITCM_BYTES = 16 * 1024
DTCM_ORIGIN = 0x20000000
FXBUF_MIN = 122880


def symbols(nm, elf):
    out = subprocess.run([nm, elf], check=True, capture_output=True,
                         text=True).stdout
    table = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3:
            table[parts[2]] = int(parts[0], 16)
    return table


def main():
    if len(sys.argv) != 3:
        print("usage: link_budget.py <nm> <elf>")
        return 0
    s = symbols(sys.argv[1], sys.argv[2])
    warn = int(os.environ.get("LINK_BUDGET_WARN_FLASH", "16384"))

    used = s["_eflash_load"] - FLASH_ORIGIN
    limit = FLASH_LIMIT - FLASH_ORIGIN
    head = FLASH_LIMIT - s["_eflash_load"]
    print(f"Flash : {used:,} / {limit:,} B used, headroom {head:,} B")
    if head < warn:
        print(f"WARNING: flash headroom {head:,} B < {warn:,} B threshold "
              "(see S072_ST1_IMPLEMENTATION.md §16 growth paths)")

    print(f"ITCM  : {s['_eitcm']:,} / {ITCM_BYTES:,} B")
    print(f"DTCM  : statics {s['_edtcmz'] - DTCM_ORIGIN:,} B")
    if "_sfxbuf" in s and "_efxbuf" in s:
        arena = s["_efxbuf"] - s["_sfxbuf"]
        print(f"FXBUF : {arena:,} B at 0x{s['_sfxbuf']:08X} "
              f"(min {FXBUF_MIN:,}, margin {arena - FXBUF_MIN:,})")
    else:
        print("FXBUF : arena symbols absent")
    return 0


if __name__ == "__main__":
    sys.exit(main())
