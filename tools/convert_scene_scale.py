#!/usr/bin/env python3
"""Offline StepScale migration utility (S078 P1 step 6).

S078 replaced the 14-entry discrete step-scale table with a 128-position
continuous log curve whose retained byte is a 7-bit CC value. No on-device
migration is performed: existing PAT4 files and .fx files keep their stored
byte and are interpreted on the new curve. Run this script on a card image
directory (or on individual files) to rewrite old content so it plays at the
musical division the user originally chose.

What it rewrites:
  * PAT4 Pattern files: the seven per-track track_scale bytes in the 160-byte
    header (offset 48 + track*16 + 1). The file CRC32C is recomputed.
  * .fx Effect files: only a numeric step_scale= value is remapped. The
    symbolic tokens ("1/16", "1/32t", ...) already name the same musical
    division on the new curve and are left untouched.

Old index -> new CC (by musical duration):
    0  1/64           ->   0
    1  1/32 triplet   ->  16
    2  1/32           ->  38
    3  1/16 triplet   ->  54
    4  1/16 (default) ->  76
    5  1/8 triplet    ->  83
    6  dotted 1/16    ->  86
    7  1/8            ->  93
    8  1/4 triplet    -> 100
    9  dotted 1/8     -> 103
   10  1/4            -> 110
   11  1/2            -> 127
   12  1 bar          -> 127   (exceeds the new 8.0x maximum)
   13  2 bars         -> 127   (exceeds the new 8.0x maximum)

Usage:
    python3 tools/convert_scene_scale.py [--dry-run] PATH [PATH ...]

PATH may be a single file or a directory (scanned recursively). Files that are
not PAT4 or .fx content are skipped. Exit status is nonzero if any file could
not be rewritten.
"""

from __future__ import annotations

import argparse
import os
import sys

# Old 14-entry index -> new 128-position CC.
REMAP_TABLE = {
    0: 0,
    1: 16,
    2: 38,
    3: 54,
    4: 76,
    5: 83,
    6: 86,
    7: 93,
    8: 100,
    9: 103,
    10: 110,
    11: 127,
    12: 127,
    13: 127,
}

# PAT4 v4 header geometry (mirrors PatternData.h).
PAT4_MAGIC = b"PAT4"
PATTERN_FILE_HEADER_BYTES = 160
PATTERN_FILE_TRACK_HEADER_BYTES = 16
PATTERN_FILE_TRACK_BASE = 48
PATTERN_FILE_ADDRESS_BYTES = 1792
PATTERN_FILE_BITMAP_BYTES = 512
PATTERN_FILE_CRC_OFFSET = 14
NUM_TRACKS = 7


def crc32c(data: bytes) -> int:
    """CRC32C (Castagnoli, reflected), matching autosave_recordCrcBegin/Finish."""
    crc = 0xFFFFFFFF
    for value in data:
        crc ^= value
        for _ in range(8):
            crc = (crc >> 1) ^ 0x82F63B78 if crc & 1 else crc >> 1
            crc &= 0xFFFFFFFF
    return (~crc) & 0xFFFFFFFF


def pat4_length(header_size: int, stack_size: int) -> int:
    return (
        header_size
        + PATTERN_FILE_ADDRESS_BYTES
        + PATTERN_FILE_BITMAP_BYTES
        + stack_size * 32
    )


def convert_pat4(data: bytearray, path: str, dry_run: bool) -> int:
    """Remap one PAT4 file in place. Returns the number of bytes changed."""
    if len(data) < PATTERN_FILE_HEADER_BYTES or data[0:4] != PAT4_MAGIC:
        return -1
    header_size = data[8] | (data[9] << 8)
    stack_size = data[6] | (data[7] << 8)
    if header_size < PATTERN_FILE_HEADER_BYTES:
        raise ValueError("%s: header_size %d < 160" % (path, header_size))
    total = pat4_length(header_size, stack_size)
    if len(data) != total:
        raise ValueError("%s: size %d != expected %d" % (path, len(data), total))

    changed = 0
    for track in range(NUM_TRACKS):
        offset = (PATTERN_FILE_TRACK_BASE
                  + track * PATTERN_FILE_TRACK_HEADER_BYTES + 1)
        old = data[offset]
        if old in REMAP_TABLE:
            new = REMAP_TABLE[old]
            if new != old:
                data[offset] = new
                changed += 1

    if changed == 0:
        return 0
    if dry_run:
        return changed

    # Recompute the header CRC32C with bytes 14..17 treated as zero.
    zeroed = bytearray(data)
    zeroed[PATTERN_FILE_CRC_OFFSET:PATTERN_FILE_CRC_OFFSET + 4] = b"\x00\x00\x00\x00"
    crc = crc32c(bytes(zeroed))
    data[PATTERN_FILE_CRC_OFFSET:PATTERN_FILE_CRC_OFFSET + 4] = crc.to_bytes(4, "little")
    return changed


def convert_fx(text: str, dry_run: bool):
    """Remap a numeric step_scale= value in one .fx text file."""
    changed = 0
    out_lines = []
    for line in text.splitlines(keepends=True):
        key, sep, value = line.partition("=")
        if sep and key.strip() == "step_scale":
            raw = value.strip()
            if raw.isdigit():
                old = int(raw)
                if old in REMAP_TABLE and 0 <= old <= 13:
                    new = REMAP_TABLE[old]
                    if new != old:
                        if not dry_run:
                            ending = "\n" if line.endswith("\n") else ""
                            line = key + "=" + str(new) + ending
                        changed += 1
        out_lines.append(line)
    if changed and not dry_run:
        return changed, "".join(out_lines)
    return changed, text


def iter_paths(paths):
    for path in paths:
        if os.path.isdir(path):
            for root, _dirs, files in os.walk(path):
                for name in sorted(files):
                    yield os.path.join(root, name)
        else:
            yield path


def main(argv) -> int:
    parser = argparse.ArgumentParser(
        description="Migrate old StepScale bytes (S078).")
    parser.add_argument("--dry-run", action="store_true",
                        help="report changes without writing")
    parser.add_argument("paths", nargs="+",
                        help="files or directories to migrate")
    args = parser.parse_args(argv)

    failures = 0
    for path in iter_paths(args.paths):
        try:
            with open(path, "rb") as handle:
                data = bytearray(handle.read())
        except OSError as exc:
            print("skip %s: %s" % (path, exc), file=sys.stderr)
            continue

        if data[0:4] == PAT4_MAGIC:
            try:
                changed = convert_pat4(data, path, args.dry_run)
            except ValueError as exc:
                print(str(exc), file=sys.stderr)
                failures += 1
                continue
            if changed < 0:
                continue
            if changed == 0:
                print("ok   %s: already migrated" % path)
                continue
            if not args.dry_run:
                with open(path, "wb") as handle:
                    handle.write(data)
            verb = "plan" if args.dry_run else "done"
            print("%s %s: %d track scale(s)" % (verb, path, changed))
            continue

        if path.lower().endswith(".fx"):
            try:
                text = data.decode("utf-8")
            except UnicodeDecodeError:
                continue
            changed, new_text = convert_fx(text, args.dry_run)
            if changed == 0:
                print("ok   %s: no numeric step_scale to migrate" % path)
                continue
            if not args.dry_run:
                with open(path, "w", encoding="utf-8") as handle:
                    handle.write(new_text)
            verb = "plan" if args.dry_run else "done"
            print("%s %s: step_scale remapped" % (verb, path))

    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
