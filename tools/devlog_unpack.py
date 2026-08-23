#!/usr/bin/env python3
"""Compact, low-token decoder for the LXR-02 development-mode trace files.

Same inputs/schema as decode_devlogs.py (which stays the human-readable,
fully-annotated reference decoder -- see its docstring and
knowledge_files/specification_reference/DEV_MODES.md for format authority).
This script imports decode_devlogs's lookup tables directly rather than
duplicating them, and instead of one prose sentence per record emits one
short line: index, tick, stage, flags/value hex, and a terse stage-specific
gloss. Intended for fast/cheap review (e.g. feeding a trace into an LLM
context) rather than for a human reading it end to end.

Usage mirrors decode_devlogs.py: no args scans SD_CARD/*.bin and writes
SD_CARD/logs/<stamp>_<stem>_compact.txt; one path argument decodes that file
to stdout only.
"""

from __future__ import annotations

import argparse
import time
from collections import Counter
from pathlib import Path

import decode_devlogs as dd

RECORD_BYTES = dd.RECORD_BYTES
CAPSULE_BYTES = dd.CAPSULE_BYTES


def compact_detail(ch: str, flags: int, value: int) -> str:
    if ch == "D":
        return f"off={value} {dd.payload_region_text(value)}"
    if ch == "I":
        scene = value & 0xF
        slot = (value >> 4) & 0x7
        expected = (value >> 8) & 0xFF
        published = (value >> 16) & 0xFF
        return (f"Scn{scene}.{slot} exp={expected} pub={published} "
                f"BASE={int(bool(flags & 1))} TRK={int(bool(flags & 2))} "
                f"ALLPUB={int(bool(flags & 4))}")
    if ch == "J":
        scene = value & 0xF
        slot = (value >> 4) & 0x7
        typ = (value >> 8) & 0xFF
        return (f"Scn{scene}.{slot} type={dd.instrument_type_text(typ)} "
                f"REQ={int(bool(flags & 1))} CALL={int(bool(flags & 2))}")
    if ch == "N":
        scene = value & 0xF
        slot = (value >> 4) & 0x7
        typ = (value >> 8) & 0xFF
        phase = flags & 0x7F
        return (f"Scn{scene}.{slot} type={dd.instrument_type_text(typ)} "
                f"phase={phase}:{dd.ENTRY_PHASES.get(phase, '?')} "
                f"FAIL={int(bool(flags & 0x80))}")
    if ch == "L":
        kind = value & 0x3
        scene = (value >> 2) & 0xF
        kind_name = "Kit" if kind == 0 else ("Scene" if kind == 1 else f"kind{kind}")
        return f"{kind_name} Scn{scene} TRK={int(bool(flags & 1))}"
    if ch == "H":
        scene = value & 0xFF
        snapshot_first = (value >> 8) & 0xFF
        live_first = (value >> 16) & 0xFF
        return (f"Bank child={scene} scratch_drift "
                f"snap=0x{snapshot_first:02x} live=0x{live_first:02x}")
    if ch == "R":
        return (f"DONE={int(bool(flags & 1))} mask=0x{value:04x} "
                f"{dd.scene_mask_text(value)}")
    if ch == "K":
        return (f"kind={'Save' if flags & 2 else 'Load'} "
                f"DONE={int(bool(flags & 1))} slot={value}")
    if ch == "S":
        return f"debounce_tick={value}"
    if ch == "A":
        return "admitted"
    if ch == "V":
        has_winner = bool(flags & 1)
        winner = "A" if (flags & 2) == 0 else "B"
        return f"winner={winner if has_winner else 'none'} gen={value}"
    if ch == "M":
        return f"dirty={int(bool(flags))} bytes={value}"
    if ch == "C":
        return f"exhausted={int(bool(flags))} patches={value}"
    if ch == "P":
        return f"target={'A' if flags == 0 else 'B'} gen={value}"
    if ch == "T":
        return f"status={'DONE' if flags & 1 else 'ERR'}"
    if ch == "W":
        return f"dirty={int(bool(flags & 1))} debounce_tick={value}"
    if ch == "F":
        if flags & 1:
            return f"gate_held pending={value}"
        if flags & 2:
            return "append_error"
        return f"flags=0x{flags:02x}"
    if ch == "G":
        return f"dropped={value}"
    if ch == "B":
        resident = (value >> 16) & 0xFFFF
        low = value & 0xFFFF
        if flags & 1:
            return f"drain resident=0x{resident:04x} off={low}"
        return (f"bankload resident=0x{resident:04x} "
                f"load=0x{low:04x} {dd.scene_mask_text(low)}")
    if ch == "X":
        site = flags & 0x07
        native = bool(flags & 0x08)
        phase = value & 0xFF
        slot = (value >> 8) & 0x3FF
        s = (f"site={dd.PHASE_STALL_SITES.get(site, site)} phase={phase} "
             f"slot={slot} NATDEL={int(native)}")
        if site == 0 and native:
            s += f" subphase={(value >> 18) & 0xFF}"
        elif site == 2:
            s += f" stream~={((value >> 18) & 0x3FFF) * 16}B"
        return s
    if ch == "O":
        elem = dd.SAVE_LIFECYCLE_TYPES.get(flags & 0x03, "?")
        cp = dd.SAVE_LIFECYCLE_CHECKPOINTS.get((flags >> 2) & 0x07, "?")
        failed = bool(flags & 0x80)
        slot = value & 0x3FF
        high = (value >> 16) & 0xFFFF
        s = f"{elem} {cp} slot={slot} FAIL={int(failed)}"
        if cp == "DELETE_RESULT" and failed:
            reason_id = (value >> 16) & 0xF
            s += f" reason={dd.DELETE_SLOT_REASONS.get(reason_id, reason_id)}"
            if reason_id == 7:
                detail_id = (value >> 20) & 0xF
                site_id = (value >> 24) & 0xFF
                s += (f" code={dd.AFATFS_RESULT_CODES.get(detail_id, detail_id)}"
                      f" site={dd.ASYNCFATFS_DELETE_TREE_FAILURE_SITES.get(site_id, site_id)}")
        elif elem == "Instrument" and cp == "CREATE_RESULT":
            s += f" crc32c_hi=0x{high:04x}"
        elif high:
            s += f" hi=0x{high:04x}"
        return s
    if ch == "E":
        op_id = value & 0xFF
        phase = (value >> 8) & 0xFF
        slot = (value >> 16) & 0x3FF
        op_name = (dd.FS_INTERNAL_OPS[op_id] if op_id < len(dd.FS_INTERNAL_OPS)
                   else f"op{op_id}")
        s = f"{op_name} phase={phase} slot={slot}"
        if flags & 0x02:
            s += " [rebuild]"
        if flags & 0x01:
            s += " see_last_O_DELETE_RESULT"
        return s
    if ch == "Y":
        slot = value & 0x3FF
        sfn = bool(value & (1 << 14))
        clus = bool(value & (1 << 15))
        return (f"slot={slot} target_clus_lo={(value >> 16) & 0xFFFF} "
                f"seen={flags} sfn={int(sfn)} clus={int(clus)}")
    return "?"


def decode_trace_compact(data: bytes) -> list[str]:
    n = len(data) // RECORD_BYTES
    lines = [f"trace: {len(data)}B {n} records"]
    count: Counter[str] = Counter()
    for i in range(0, len(data) - len(data) % RECORD_BYTES, RECORD_BYTES):
        rec = data[i:i + RECORD_BYTES]
        stage, flags = rec[0], rec[1]
        tick = dd.u16(rec[2:4])
        value = dd.u32(rec[4:8])
        ch = chr(stage) if 32 <= stage < 127 else f"0x{stage:02x}"
        count[ch] += 1
        lines.append(f"{i // RECORD_BYTES:06d} t={tick:5d} {ch} "
                     f"f=0x{flags:02x} v=0x{value:08x} | "
                     f"{compact_detail(ch, flags, value)}")
    rem = len(data) % RECORD_BYTES
    if rem:
        lines.append(f"trailing {rem}B: {data[-rem:].hex(' ')}")
    lines.append("totals: " + ",".join(f"{k}={v}" for k, v in sorted(count.items())))
    return lines


def decode_capsule_compact(capsule: bytes) -> list[str]:
    if len(capsule) != CAPSULE_BYTES:
        return ["bad capsule length"]
    rows = [capsule[o:o + 8] for o in range(0, 64, 8)]
    if [r[0] for r in rows] != list(range(0xE0, 0xE8)) or rows[0][1] != 1:
        return ["unknown capsule schema: " + capsule.hex(" ")]
    context, progress, chunk, cursor, allocation, owner, cache, transport = rows
    flags = context[7]
    target = {0: "A", 1: "B"}.get(context[2], "?")
    return [
        f"E0 target={target} ensure_phase={context[3]} status={context[4]} "
        f"fileop={context[5]} append_phase={context[6]} "
        f"active={bool(flags & 0x80)} frozen={bool(flags & 1)}",
        f"E1 bytes_done={dd.u32(progress[1:5])} chunk_len={dd.u16(progress[5:7])} "
        f"zero_streak={progress[7]}",
        f"E2 written={dd.u16(chunk[1:3])} chunk_off={dd.u16(chunk[3:5])} "
        f"req={dd.u16(chunk[5:7])} gen={chunk[7]}",
        f"E3 cursor={dd.u32(cursor[1:5])} size={dd.u24(cursor[5:8])}",
        f"E4 search_clus={dd.u32(allocation[1:5])} sec_per_clus={allocation[5]} "
        f"wrapped={allocation[6]} flags=0x{allocation[7]:02x}",
        f"E5 prev_clus={dd.u32(owner[1:5])} cursor_clus={dd.u24(owner[5:8])}",
        f"E6 dirty={cache[1]} locked={cache[2]} reading={cache[3]} "
        f"writing={cache[4]} flush={cache[5]} "
        f"active_idx={'none' if cache[6] == 0xFF else cache[6]} full={cache[7]}",
        f"E7 sd_state={transport[1]} op={transport[2]} "
        f"off={dd.u16(transport[3:5])} retry={dd.u16(transport[5:7])} "
        f"cb_pending={transport[7]}",
    ]


def decode_bootlog_compact(data: bytes) -> list[str]:
    if len(data) < RECORD_BYTES:
        return [f"bootlog: too short: {data.hex(' ')}"]
    token = data[:RECORD_BYTES].decode("ascii", errors="replace")
    lines = [f"bootlog: token={token!r} "
             f"({dd.BOOT_CODES.get(token, ('?', 'unknown'))[0]})"]
    if token == "ASENSURE" and len(data) == RECORD_BYTES + CAPSULE_BYTES:
        lines.extend(decode_capsule_compact(data[RECORD_BYTES:]))
    elif len(data) != RECORD_BYTES:
        lines.append(f"suffix({len(data) - RECORD_BYTES}B): "
                     f"{data[RECORD_BYTES:].hex(' ')}")
    return lines


def render(name: str, data: bytes) -> str:
    fmt = dd.sniff_format(name, data)
    lines = decode_bootlog_compact(data) if fmt == "boot" else decode_trace_compact(data)
    return f"# {name} ({fmt})\n" + "\n".join(lines) + "\n"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("path", nargs="?", type=Path,
                         help="decode this single trace file to stdout")
    parser.add_argument("--sdcard", type=Path,
                         default=Path(__file__).resolve().parent.parent / "SD_CARD",
                         help="SD card directory to scan when no path is given")
    args = parser.parse_args()

    if args.path:
        print(render(args.path.name, args.path.read_bytes()), end="")
        return

    traces = sorted(args.sdcard.glob("*.bin"))
    if not traces:
        raise SystemExit(f"no *.bin trace files found in {args.sdcard}")
    logs_dir = args.sdcard / "logs"
    logs_dir.mkdir(parents=True, exist_ok=True)
    stamp = time.strftime("%S%M%H%d%m%Y")
    for path in traces:
        data = path.read_bytes()
        out_path = logs_dir / f"{stamp}_{path.stem}_compact.txt"
        out_path.write_text(render(path.name, data), encoding="utf-8")
        print(f"decoded {path.name} ({len(data)}B) -> {out_path}")


if __name__ == "__main__":
    main()
