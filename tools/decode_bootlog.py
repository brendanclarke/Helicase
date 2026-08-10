#!/usr/bin/env python3
"""Read-only decoder for the boot logger's optional ASENSURE capsule."""

from __future__ import annotations

import argparse
from pathlib import Path


CAPSULE_BYTES = 64
TOKEN_BYTES = 8


def u16(data: bytes) -> int:
    return int.from_bytes(data, "little")


def u24(data: bytes) -> int:
    return int.from_bytes(data + b"\0", "little")


def u32(data: bytes) -> int:
    return int.from_bytes(data, "little")


def decode_capsule(capsule: bytes) -> None:
    if len(capsule) != CAPSULE_BYTES:
        raise ValueError("capsule must be exactly 64 bytes")
    rows = [capsule[offset : offset + 8] for offset in range(0, 64, 8)]
    expected = list(range(0xE0, 0xE8))
    if [row[0] for row in rows] != expected or rows[0][1] != 1:
        print("unknown HCPRMS capsule schema; raw:", capsule.hex(" "))
        return

    context, progress, chunk, cursor, allocation, owner, cache, transport = rows
    flags = context[7]
    allocation_flags = allocation[7]
    cluster_bytes = allocation[5] * 512
    print("HCPRMS capsule schema:", context[1])
    print("target:", {0: "A (.hcprms1)", 1: "B (.hcprms2)"}.get(context[2], "unknown"))
    print("ensure phase:", context[3], "facade status:", context[4])
    print("file operation:", context[5], "append phase:", context[6],
          "frozen:", bool(flags & 1))
    print("application: bytes_done=", u32(progress[1:5]),
          "chunk_len=", u16(progress[5:7]), "zero_write_streak=", progress[7])
    print("last fwrite: written=", u16(chunk[1:3]), "chunk_offset=", u16(chunk[3:5]),
          "requested=", u16(chunk[5:7]), "generation=", chunk[7])
    print("file: cursor=", u32(cursor[1:5]), "logical_size=", u24(cursor[5:8]))
    print("allocator: search_cluster=", u32(allocation[1:5]),
          "sectors_per_cluster=", allocation[5], "cluster_bytes=", cluster_bytes,
          "wrapped=", allocation[6], "full=", bool(allocation_flags & 1),
          "file_available=", bool(allocation_flags & 2),
          "bytes_done_at_cluster_boundary=", bool(allocation_flags & 4))
    print("allocator owner: previous_cluster=", u32(owner[1:5]),
          "cursor_cluster=", u24(owner[5:8]))
    print("cache: dirty=", cache[1], "locked=", cache[2], "reading=", cache[3],
          "writing=", cache[4], "flush=", cache[5],
          "active_index=", None if cache[6] == 0xFF else cache[6], "full=", cache[7])
    print("SD: state=", transport[1], "operation=", transport[2],
          "offset=", u16(transport[3:5]), "retry_count=", u16(transport[5:7]),
          "callback_pending=", transport[7])


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("bootlog", type=Path)
    args = parser.parse_args()
    data = args.bootlog.read_bytes()
    if len(data) < TOKEN_BYTES:
        raise SystemExit(f"invalid bootlog: {len(data)} bytes, expected at least 8")
    token = data[:TOKEN_BYTES].decode("ascii", errors="replace")
    print("boot token:", repr(token), "bytes:", len(data))
    if len(data) == TOKEN_BYTES:
        return
    if token != "ASENSURE" or len(data) != TOKEN_BYTES + CAPSULE_BYTES:
        print("unexpected suffix; raw:", data[TOKEN_BYTES:].hex(" "))
        return
    decode_capsule(data[TOKEN_BYTES:])


if __name__ == "__main__":
    main()
