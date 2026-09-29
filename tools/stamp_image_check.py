#!/usr/bin/env python3
"""
Stamp the image check block into the application binary (Session 073).

What: the linker places a 32-byte `.image_check` block in sector 1, right
after the vector table, holding placeholders ("IMC?" and 0xFFFFFFFF words).
This tool rewrites it in `lxr02.bin` as the magic "IMCK", one CRC32 per
application sector 1..6, and the image length in bytes. Each CRC covers that
sector's share of the image with the block's own 32 bytes skipped. Sectors
the image does not reach get the CRC32 of nothing (0).

Why: Core/Hardware/flashImage.c recomputes the same CRCs at boot, so a
sector the closed LXRV2 bootloader failed to erase or program is reported by
number instead of running as corrupt code.

Inputs: argv[1] = nm executable, argv[2] = ELF, argv[3] = binary (edited in
place). The CRC is Python's zlib.crc32 (reflected CRC-32, poly 0xEDB88320),
which flashImage.c must match. Exit status is non-zero if the binary does
not have the layout the linker script promises; the Makefile then deletes
the binary so no unstamped image can be packaged.
Affiliates: STM32F765VIHx_FLASH.ld, Makefile .bin rule, flashImage.c.
"""
import struct
import subprocess
import sys
import zlib

FLASH_ORIGIN = 0x08008000
SECTOR_ENDS = (0x08010000, 0x08018000, 0x08020000,
               0x08040000, 0x08080000, 0x080C0000)
MAGIC_PLACEHOLDER = 0x3F434D49   # "IMC?"
MAGIC_STAMPED = 0x4B434D49       # "IMCK"
BLOCK_BYTES = 4 + 4 * len(SECTOR_ENDS) + 4


def symbols(nm, elf):
    out = subprocess.run([nm, elf], check=True, capture_output=True,
                         text=True).stdout
    table = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3:
            table[parts[2]] = int(parts[0], 16)
    return table


def fail(msg):
    print(f"stamp_image_check: {msg}", file=sys.stderr)
    return 1


def main():
    if len(sys.argv) != 4:
        return fail("usage: stamp_image_check.py <nm> <elf> <bin>")
    s = symbols(sys.argv[1], sys.argv[2])
    start, end = s["_simage_check"], s["_eimage_check"]
    image = bytearray(open(sys.argv[3], "rb").read())

    if end - start != BLOCK_BYTES:
        return fail(f"check block is {end - start} B, expected {BLOCK_BYTES}")
    image_end = s["_eflash_load"]
    if len(image) != image_end - FLASH_ORIGIN:
        return fail(f"binary is {len(image)} B; expected _eflash_load - "
                    f"origin = {image_end - FLASH_ORIGIN} B")
    if end > SECTOR_ENDS[0]:
        return fail(f"check block ends at 0x{end:08X}, outside sector 1")
    off = start - FLASH_ORIGIN
    magic = struct.unpack_from("<I", image, off)[0]
    if magic not in (MAGIC_PLACEHOLDER, MAGIC_STAMPED):
        return fail(f"no check block at 0x{start:08X} (found 0x{magic:08X})")

    def at(addr):
        return addr - FLASH_ORIGIN

    crcs = []
    lo = FLASH_ORIGIN
    for sector_end in SECTOR_ENDS:
        # Same arithmetic as flashImage.c: the sector's share of the image,
        # split around the check block.
        hi = min(sector_end, image_end)
        lo = min(lo, hi)
        a_hi = min(hi, max(lo, start))
        b_lo = max(lo, min(hi, end))
        crc = zlib.crc32(bytes(image[at(lo):at(a_hi)]))
        crc = zlib.crc32(bytes(image[at(b_lo):at(hi)]), crc)
        crcs.append(crc & 0xFFFFFFFF)
        lo = sector_end
    struct.pack_into("<8I", image, off, MAGIC_STAMPED, *crcs, len(image))
    open(sys.argv[3], "wb").write(image)

    reach = next(i + 1 for i, hi in enumerate(SECTOR_ENDS)
                 if image_end - 1 < hi)
    print(f"Image check: stamped at 0x{start:08X}, image ends in sector "
          f"{reach}; CRC32 s1..s6 = " + " ".join(f"{c:08X}" for c in crcs))
    return 0


if __name__ == "__main__":
    sys.exit(main())
