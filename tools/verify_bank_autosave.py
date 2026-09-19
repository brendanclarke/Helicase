#!/usr/bin/env python3
"""Read-only validator for one copied Bank/AutoSave card fixture.

The validator checks the selected Bank tree against /.hcnames, settings.cfg,
and the newer valid .hcprms A/B record. It never writes the card root. The
wire offsets intentionally mirror Autosave.h so a failure prints the raw Bank
section needed to distinguish a bad field from a bad offset.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


RECORD_BYTES = 34768
BANK_OFFSET = 3920
BANK_SECTION_BYTES = 128
SCENE_OFFSET = 4048
SCENE_BYTES = 1920
SCENE_COUNT = 16
INSTRUMENTS_PER_KIT = 6
COMMIT_VALID = 0xA5


def crc32c(data: bytes) -> int:
    """Return reflected Castagnoli CRC32C with the firmware polynomial."""
    crc = 0xFFFFFFFF
    for value in data:
        crc ^= value
        for _ in range(8):
            crc = (crc >> 1) ^ 0x82F63B78 if crc & 1 else crc >> 1
    return (~crc) & 0xFFFFFFFF


def u16(data: bytes) -> int:
    return int.from_bytes(data, "little")


def text8(data: bytes) -> str:
    return data.rstrip(b"\0 ").decode("ascii", errors="replace")


def parse_assignments(path: Path) -> dict[str, str]:
    result: dict[str, str] = {}
    for line in path.read_text(encoding="ascii").splitlines():
        if "=" in line and not line.startswith("["):
            key, value = line.split("=", 1)
            result[key.strip()] = value.strip()
    return result


def parse_numbered(name: str, width: int) -> tuple[int, str] | None:
    match = re.fullmatch(rf"(\d{{{width}}})[ _](.*)", name)
    if not match:
        return None
    return int(match.group(1)), match.group(2).strip()


def parse_hcnames(path: Path) -> list[tuple[str, str, str]]:
    rows: list[tuple[str, str, str]] = []
    header_seen = False
    for line in path.read_text(encoding="ascii").splitlines():
        if not line:
            continue
        fields = line.split("\t")
        if not header_seen:
            if not fields[0].startswith("#types"):
                raise ValueError(f"HCNAMES missing #types header: {line!r}")
            header_seen = True
            continue  # the #types vocabulary header line
        if fields[0].startswith("#"):
            raise ValueError(f"HCNAMES unexpected header row: {line!r}")
        row_index = len(rows)
        if not 2 <= len(fields) <= 4:
            raise ValueError(f"HCNAMES malformed row {row_index}: {line!r}")
        if row_index >= 33 and len(fields) == 2:
            raise ValueError(f"HCNAMES Instrument row {row_index} lacks a type column: {line!r}")
        name = fields[0].strip()
        source = fields[1].strip()
        type_text = fields[2].strip() if row_index >= 33 else ""
        rows.append((name, source, type_text))
    return rows


def parse_kitset(path: Path) -> dict[int, tuple[str, str]]:
    result: dict[int, tuple[str, str]] = {}
    current: int | None = None
    for line in path.read_text(encoding="ascii").splitlines():
        section = re.fullmatch(r"\[slot([1-6])\]", line.strip())
        if section:
            current = int(section.group(1)) - 1
            continue
        if current is None or "=" not in line:
            continue
        key, value = (part.strip() for part in line.split("=", 1))
        if key == "type":
            old = result.get(current, ("", ""))
            result[current] = (value, old[1])
        elif key == "file":
            old = result.get(current, ("", ""))
            result[current] = (old[0], value)
    return result


def parse_scene_values(path: Path) -> list[int]:
    values = parse_assignments(path)
    result = [0] * 40

    def list_values(key: str, count: int) -> list[int]:
        raw = values.get(key, "")
        parsed = [int(item.strip(), 0) for item in raw.split(",") if item.strip()]
        if len(parsed) != count:
            raise ValueError(f"{path}: {key} expected {count} values")
        return parsed

    result[0] = int(values.get("morph_amount", "0"), 0)
    result[1:7] = list_values("voice_morph_amount", 6)
    result[7] = int(values.get("voice_decimation_all", "127"), 0)
    result[8:14] = list_values("audio_out", 6)
    result[14:20] = list_values("fx_send_amount", 6)
    result[20:26] = list_values("fader_setting", 6)
    result[26:33] = list_values("midi_channel", 7)
    result[33:40] = list_values("midi_note", 7)
    return result


def valid_record(path: Path) -> tuple[int, bytes] | None:
    data = path.read_bytes()
    if len(data) != RECORD_BYTES:
        return None
    if data[0:4] != b"HCPR" or data[4] != 1 or data[5] != COMMIT_VALID:
        return None
    expected = int.from_bytes(data[12:16], "little")
    crc_data = data[:12] + b"\0\0\0\0" + data[16:]
    if crc32c(crc_data) != expected:
        return None
    return int.from_bytes(data[8:12], "little"), data


def generation_newer(candidate: int, reference: int) -> bool:
    """Match the firmware's wrapping signed-difference generation rule."""
    difference = (candidate - reference) & 0xFFFFFFFF
    return difference != 0 and difference < 0x80000000


def add_error(errors: list[str], message: str) -> None:
    errors.append(message)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("card_root", type=Path)
    parser.add_argument("bank_slot", type=int)
    args = parser.parse_args()
    root = args.card_root
    errors: list[str] = []

    if not 0 <= args.bank_slot <= 999:
        parser.error("bank_slot must be in the range 0..999")
    slot_text = f"{args.bank_slot:03d}"

    try:
        rows = parse_hcnames(root / ".hcnames")
    except (OSError, ValueError) as exc:
        add_error(errors, str(exc))
        rows = []
    if len(rows) != 129:
        add_error(errors, f"HCNAMES row count: expected 129, got {len(rows)}")

    bank_dirs = [
        item for item in (root / "Bank").iterdir()
        if item.is_dir() and parse_numbered(item.name, 3)
        and parse_numbered(item.name, 3)[0] == args.bank_slot
    ] if (root / "Bank").is_dir() else []
    if len(bank_dirs) != 1:
        add_error(errors, f"Bank slot {slot_text}: expected one directory, got "
                         f"{[item.name for item in bank_dirs]}")
        bank_dir = None
    else:
        bank_dir = bank_dirs[0]

    expected_scenes: dict[int, tuple[str, str, dict[int, tuple[str, str]], Path]] = {}
    child_mask = 0
    bank_name = ""
    if bank_dir is not None:
        parsed_bank = parse_numbered(bank_dir.name, 3)
        assert parsed_bank is not None
        _, bank_name = parsed_bank
        for child in bank_dir.iterdir():
            parsed_child = parse_numbered(child.name, 2)
            if not child.is_dir() or parsed_child is None:
                continue
            child_slot, scene_name = parsed_child
            if child_slot >= SCENE_COUNT or child_slot in expected_scenes:
                add_error(errors, f"Bank child invalid/duplicate: {child.name}")
                continue
            kits = [item for item in child.iterdir()
                    if item.is_dir() and item.name.startswith("Kit ")]
            if len(kits) != 1:
                add_error(errors, f"Scene {child.name}: expected one Kit directory")
                continue
            kit_dir = kits[0]
            kit_name = kit_dir.name[4:].strip()
            kitset = kit_dir / "kitset.kcg"
            try:
                members = parse_kitset(kitset)
            except OSError as exc:
                add_error(errors, str(exc))
                members = {}
            if set(members) != set(range(INSTRUMENTS_PER_KIT)):
                add_error(errors, f"{kitset}: expected six slot definitions")
            for instrument_slot, (instrument_type, filename) in members.items():
                member = kit_dir / filename
                if not member.is_file():
                    add_error(errors, f"{kitset}: missing member {filename!r}")
                elif f"type={instrument_type}" not in member.read_text(
                        encoding="ascii", errors="replace"):
                    add_error(errors, f"{member}: type does not match kitset")
            expected_scenes[child_slot] = (scene_name, kit_name, members, child)
            child_mask |= 1 << child_slot

    def check_row(row: int, expected_name: str, label: str) -> None:
        if row >= len(rows):
            return
        actual = rows[row][0]
        if actual != expected_name:
            add_error(errors, f"HCNAMES row {row} {label}: expected "
                             f"{expected_name!r}, got {actual!r}")

    if rows:
        check_row(0, bank_name, "Bank name")
        if rows[0][1] != slot_text:
            add_error(errors, f"HCNAMES row 0 Bank source: expected "
                             f"{slot_text!r}, got {rows[0][1]!r}")
        for scene in range(SCENE_COUNT):
            if scene not in expected_scenes:
                add_error(errors, f"missing Bank child Scene {scene:02d}")
                continue
            scene_name, kit_name, members, _ = expected_scenes[scene]
            check_row(1 + scene, scene_name, f"Scene {scene:02d}")
            check_row(17 + scene, kit_name, f"Kit {scene:02d}")
            for instrument in range(INSTRUMENTS_PER_KIT):
                filename = members.get(instrument, ("", ""))[1]
                expected_name = Path(filename).stem if filename else ""
                row = 33 + scene * INSTRUMENTS_PER_KIT + instrument
                check_row(row, expected_name,
                         f"Instrument {scene:02d}/{instrument + 1}")
                instrument_type = members.get(instrument, ("", ""))[0]
                if (filename and row < len(rows) and
                        rows[row][2] != instrument_type):
                    add_error(errors, "HCNAMES row %d Instrument type: expected %s, got %s" % (row, instrument_type, rows[row][2]))

    settings: dict[str, str] = {}
    try:
        settings = parse_assignments(root / "settings.cfg")
        active_bank = int(settings["active_bank"], 0)
        if active_bank != args.bank_slot:
            add_error(errors, f"settings.cfg active_bank: expected "
                             f"{args.bank_slot}, got {active_bank}")
    except (OSError, KeyError, ValueError) as exc:
        add_error(errors, f"settings.cfg: {exc}")

    records = []
    for name in (".hcprms1", ".hcprms2"):
        try:
            candidate = valid_record(root / name)
        except OSError as exc:
            add_error(errors, f"{name}: {exc}")
            candidate = None
        if candidate is not None:
            records.append((candidate[0], name, candidate[1]))
    if not records:
        add_error(errors, "neither AutoSave record is valid")
        winner_name = "none"
        record = bytes(RECORD_BYTES)
    else:
        winner = records[0]
        for candidate in records[1:]:
            if generation_newer(candidate[0], winner[0]):
                winner = candidate
        _, winner_name, record = winner

    raw_bank = record[BANK_OFFSET:BANK_OFFSET + 15]
    record_slot = u16(record[BANK_OFFSET:BANK_OFFSET + 2])
    record_name = text8(record[BANK_OFFSET + 2:BANK_OFFSET + 10])
    record_mask = u16(record[BANK_OFFSET + 10:BANK_OFFSET + 12])
    record_active = record[BANK_OFFSET + 12]
    record_voice_mask = u16(record[BANK_OFFSET + 13:BANK_OFFSET + 15])
    if record_slot != args.bank_slot:
        add_error(errors, f"{winner_name} Bank restore slot: expected "
                         f"{args.bank_slot}, got {record_slot}")
    if record_name != bank_name:
        add_error(errors, f"{winner_name} Bank name: expected {bank_name!r}, "
                         f"got {record_name!r}")

    bankset_values: dict[str, str] = {}
    if bank_dir is not None:
        try:
            bankset_values = parse_assignments(bank_dir / "bankset.bcg")
            expected_active = int(bankset_values["active_scene"], 0)
            expected_voice = int(bankset_values["scene_mask_voice_edit"], 16)
            if record_active != expected_active:
                add_error(errors, f"{winner_name} active_scene: expected "
                                 f"{expected_active}, got {record_active}")
            if record_voice_mask != expected_voice:
                add_error(errors, f"{winner_name} voice edit mask: expected "
                                 f"0x{expected_voice:04x}, got "
                                 f"0x{record_voice_mask:04x}")
        except (OSError, KeyError, ValueError) as exc:
            add_error(errors, f"bankset.bcg: {exc}")
    if record_mask != child_mask:
        add_error(errors, f"{winner_name} scene_present_mask: expected "
                         f"0x{child_mask:04x}, got 0x{record_mask:04x}")

    # Sample the active Scene and the first discovered child against text
    # sources; this keeps the check bounded while still crossing both source
    # and payload domains.
    samples = sorted({int(bankset_values.get("active_scene", "0"), 0),
                      next(iter(expected_scenes), 0)})
    for scene in samples:
        if scene not in expected_scenes:
            continue
        scene_name, kit_name, members, child = expected_scenes[scene]
        scene_base = SCENE_OFFSET + scene * SCENE_BYTES
        if text8(record[scene_base:scene_base + 8]) != scene_name:
            add_error(errors, f"{winner_name} Scene {scene:02d} name payload "
                             f"does not match sceneset.scg directory")
        try:
            values = parse_scene_values(child / "sceneset.scg")
            actual = list(record[scene_base + 8:scene_base + 48])
            if actual[:40] != values:
                add_error(errors, f"{winner_name} Scene {scene:02d} settings "
                                 f"payload does not match sceneset.scg")
        except (OSError, ValueError) as exc:
            add_error(errors, str(exc))
        kit_base = scene_base + 640
        if text8(record[kit_base:kit_base + 8]) != kit_name:
            add_error(errors, f"{winner_name} Scene {scene:02d} Kit name "
                             f"payload does not match kitset.kcg directory")
        for instrument, (instrument_type, filename) in members.items():
            inst_base = kit_base + 128 + instrument * 192
            actual_type = record[inst_base:inst_base + 3].decode(
                "ascii", errors="replace")
            actual_name = text8(record[inst_base + 3:inst_base + 11])
            if actual_type != instrument_type or actual_name != Path(filename).stem:
                add_error(errors, f"{winner_name} Scene {scene:02d} Instrument "
                                 f"{instrument + 1} payload does not match "
                                 f"kit member file {filename!r}")

    print(f"Bank AutoSave validation: {'PASS' if not errors else 'FAIL'}")
    print(f"  slot={slot_text} winner={winner_name} "
          f"present_mask=0x{record_mask:04x} expected=0x{child_mask:04x}")
    if errors:
        print("  Bank bytes @3920..3934: " + raw_bank.hex(" "))
        for error in errors:
            print(f"  - {error}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
