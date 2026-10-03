#!/usr/bin/env python3
"""
Build the LXRV2 update image from the linked application.

What: the single step between the linker and the SD card. It reads the raw
`lxr02.bin` (objcopy output), stamps the 32-byte `.image_check` block that
Core/Hardware/flashImage.c verifies at every boot, and writes
`LXRV2_lxr02.img`: a 16-byte header (magic "LXRV2IMG", little-endian payload
length, 8-bit checksum) followed by the stamped payload.

Image check stamp (Session 073): the linker places the block in sector 1,
right after the vector table, holding placeholders ("IMC?" and 0xFFFFFFFF
words). The stamp writes the magic "IMCK", one CRC32 per application sector
1..6, and the image length in bytes. Each CRC covers that sector's share of
the image with the block's own 32 bytes skipped; sectors the image does not
reach get the CRC32 of nothing (0). flashImage.c recomputes the same CRCs at
boot, so a sector the closed LXRV2 bootloader failed to erase or program is
reported by number instead of running as corrupt code.

Why one script (Session 074): the stamp used to be a separate tool run in the
Makefile `.bin` rule; it is folded in here so exactly one Python script is
needed to produce the image. The `.bin` is no longer modified: it stays the
raw objcopy output, and the stamped payload exists only inside the `.img`.

Inputs: argv[1] = nm executable, argv[2] = ELF, argv[3] = raw binary (read
only), argv[4] = output image. The CRC is Python's zlib.crc32 (reflected
CRC-32, poly 0xEDB88320), which flashImage.c must match. The checksum byte is
(~sum(payload)) & 0xFF, as the LXRV2 bootloader expects.
Output: the image, plus one "Written:" line. Any failure (the binary does not
have the layout the linker script promises) prints the reason to stderr,
deletes any previous image, and exits non-zero, so neither an unstamped nor a
stale image can be copied to the card.
Affiliates: STM32F765VIHx_FLASH.ld (.image_check, _eflash_load), the Makefile
`img` rule, Core/Hardware/flashImage.c.
"""
import os
import struct
import subprocess
import sys
import zlib

HEADER_MAGIC = b"LXRV2IMG"
FLASH_ORIGIN = 0x08008000
SECTOR_ENDS = (0x08010000, 0x08018000, 0x08020000,
               0x08040000, 0x08080000, 0x080C0000)
MAGIC_PLACEHOLDER = 0x3F434D49   # "IMC?"
MAGIC_STAMPED = 0x4B434D49       # "IMCK"
BLOCK_BYTES = 4 + 4 * len(SECTOR_ENDS) + 4


class BuildError(Exception):
    """A layout or input problem that must stop the image build."""


def symbols(nm, elf):
    """Return {symbol: address} from `nm` output for the linked ELF."""
    out = subprocess.run([nm, elf], check=True, capture_output=True,
                         text=True).stdout
    table = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3:
            table[parts[2]] = int(parts[0], 16)
    return table


def stamp(payload, s):
    """Validate the layout and stamp the image check block in place."""
    for name in ("_simage_check", "_eimage_check", "_eflash_load"):
        if name not in s:
            raise BuildError(f"symbol {name} missing from the ELF")
    start, end = s["_simage_check"], s["_eimage_check"]
    image_end = s["_eflash_load"]

    if end - start != BLOCK_BYTES:
        raise BuildError(f"check block is {end - start} B, "
                         f"expected {BLOCK_BYTES}")
    if len(payload) != image_end - FLASH_ORIGIN:
        raise BuildError(f"binary is {len(payload)} B; expected "
                         f"_eflash_load - origin = "
                         f"{image_end - FLASH_ORIGIN} B")
    if end > SECTOR_ENDS[0]:
        raise BuildError(f"check block ends at 0x{end:08X}, outside sector 1")
    off = start - FLASH_ORIGIN
    magic = struct.unpack_from("<I", payload, off)[0]
    if magic not in (MAGIC_PLACEHOLDER, MAGIC_STAMPED):
        raise BuildError(f"no check block at 0x{start:08X} "
                         f"(found 0x{magic:08X})")

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
        crc = zlib.crc32(bytes(payload[at(lo):at(a_hi)]))
        crc = zlib.crc32(bytes(payload[at(b_lo):at(hi)]), crc)
        crcs.append(crc & 0xFFFFFFFF)
        lo = sector_end
    struct.pack_into("<8I", payload, off, MAGIC_STAMPED, *crcs, len(payload))


def build(nm, elf, bin_path, img_path):
    """Stamp the payload and write the LXRV2 image; return a process status."""
    # Remove the previous image first, so a failure below cannot leave a
    # stale image that looks current.
    if os.path.exists(img_path):
        os.remove(img_path)
    try:
        payload = bytearray(open(bin_path, "rb").read())
        stamp(payload, symbols(nm, elf))
    except (BuildError, OSError, subprocess.CalledProcessError) as exc:
        print(f"build_lxrv2_img: {exc}", file=sys.stderr)
        return 1

    c = (~sum(payload)) & 0xFF
    with open(img_path, "wb") as out:
        out.write(HEADER_MAGIC + struct.pack("<II", len(payload), c) +
                  payload)
    print(f"Written:{img_path} ({len(payload)}b) "
          f"{'OK' if (sum(payload) + c) & 0xFF == 0xFF else 'FAIL'}")
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 5:
        print(f"Usage: {sys.argv[0]} <nm> <elf> <in.bin> <out.img>",
              file=sys.stderr)
        sys.exit(1)
    sys.exit(build(*sys.argv[1:5]))
