#!/usr/bin/env python3
"""Populate SD_CARD/Scene from existing numbered Kit folders.

Default behavior intentionally converts only ``001 Slak`` so the first Scene
folder can be reviewed before creating a full library. Use ``--all`` to convert
every numbered Kit folder under the source directory.
"""

from __future__ import annotations

import argparse
import re
import shutil
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCRIPT_DIR = Path(__file__).resolve().parent
DEFAULT_KIT_ROOT = ROOT / "SD_CARD" / "Kit"
DEFAULT_SCENE_ROOT = ROOT / "SD_CARD" / "Scene"

NUM_TRACKS = 7
NUM_STEPS = 128
PATTERN_COUNT = 1
PATTERN_NAME_BYTES = 8
STEP_RECORD_SIZE = 9
INSTRUMENT_PARAM_INVALID = 0xFFFF
PAT_DEFAULT_NOTE = 63
PAT_DEFAULT_PROB = 127
PAT_DEFAULT_VOLUME = 100
PAT_DEFAULT_TRACK_LENGTH = 16
TRACK_SCALE_OFF = 10

KIT_DIR_RE = re.compile(r"^(\d{3})[ _](.+)$")


def parse_numbered_dir(path: Path) -> tuple[int, str] | None:
    match = KIT_DIR_RE.match(path.name)
    if not match:
        return None
    return int(match.group(1)), match.group(2).strip()


def short_display_name(name: str) -> bytes:
    encoded = name.encode("ascii", "replace")[:PATTERN_NAME_BYTES]
    return encoded.ljust(PATTERN_NAME_BYTES, b" ")


def empty_step_record() -> bytes:
    param_lo = INSTRUMENT_PARAM_INVALID & 0xFF
    param_hi = (INSTRUMENT_PARAM_INVALID >> 8) & 0xFF
    return bytes(
        [
            PAT_DEFAULT_VOLUME,
            PAT_DEFAULT_PROB,
            PAT_DEFAULT_NOTE,
            param_lo,
            param_hi,
            0,
            param_lo,
            param_hi,
            0,
        ]
    )


def make_empty_bridge_pattern(scene_name: str) -> bytes:
    data = bytearray()
    data += short_display_name(scene_name)

    step = empty_step_record()
    for _ in range(NUM_TRACKS * PATTERN_COUNT * NUM_STEPS):
        data += step

    for _ in range(NUM_TRACKS * PATTERN_COUNT):
        data += (0).to_bytes(2, "little")

    data += bytes([0, 0])

    for _ in range(NUM_TRACKS * PATTERN_COUNT):
        data += bytes([PAT_DEFAULT_TRACK_LENGTH])

    for _ in range(NUM_TRACKS * PATTERN_COUNT):
        data += bytes([0, TRACK_SCALE_OFF])

    for _ in range(NUM_TRACKS * PATTERN_COUNT):
        data += bytes([0])

    return bytes(data)


def make_sceneset(scene_name: str) -> str:
    return "\n".join(
        [
            "format=helicase.sceneset",
            "version=1",
            f"name={scene_name}",
            "morph_amount=0",
            "voice_morph_amount=0,0,0,0,0,0",
            "voice_decimation_all=127",
            "midi_channel=1,2,3,4,5,6,7",
            "midi_note=63,63,63,63,63,63,63",
            "",
        ]
    )


def convert_kit_to_scene(kit_dir: Path, scene_root: Path, overwrite: bool) -> Path:
    parsed = parse_numbered_dir(kit_dir)
    if parsed is None:
        raise ValueError(f"not a numbered Kit directory: {kit_dir}")

    slot, name = parsed
    scene_dir = scene_root / f"{slot:03d} {name}"
    embedded_kit_dir = scene_dir / f"Kit {name}"

    if scene_dir.exists():
        if not overwrite:
            raise FileExistsError(f"{scene_dir} already exists; pass --overwrite")
        shutil.rmtree(scene_dir)

    scene_dir.mkdir(parents=True, exist_ok=True)
    shutil.copytree(kit_dir, embedded_kit_dir)

    (scene_dir / "sceneset.scg").write_text(
        make_sceneset(name),
        encoding="ascii",
        newline="\n",
    )
    (scene_dir / "pattern.pat").write_bytes(make_empty_bridge_pattern(name))
    (scene_dir / "effects.fx").write_text(
        "\n".join(
            [
                "format=helicase.effect",
                "version=1",
                "placeholder=1",
                "",
            ]
        ),
        encoding="ascii",
        newline="\n",
    )
    return scene_dir


def find_kit_dirs(source: Path, convert_all: bool) -> list[Path]:
    kits = []
    for path in sorted(source.iterdir()):
        if not path.is_dir():
            continue
        parsed = parse_numbered_dir(path)
        if parsed is None:
            continue
        if not (path / "kitset.kcg").is_file():
            continue
        if convert_all or path.name == "001 Slak":
            kits.append(path)
    return kits


def default_source_dir() -> Path:
    for path in SCRIPT_DIR.iterdir():
        if path.is_dir() and parse_numbered_dir(path) and (path / "kitset.kcg").is_file():
            return SCRIPT_DIR
    return DEFAULT_KIT_ROOT


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, default=default_source_dir())
    parser.add_argument("--dest", type=Path, default=DEFAULT_SCENE_ROOT)
    parser.add_argument("--all", action="store_true")
    parser.add_argument("--overwrite", action="store_true")
    args = parser.parse_args()

    source = args.source.resolve()
    dest = args.dest.resolve()
    if not source.is_dir():
        raise SystemExit(f"source Kit directory not found: {source}")

    kits = find_kit_dirs(source, args.all)
    if not kits:
        raise SystemExit("no matching Kit directories found")

    dest.mkdir(parents=True, exist_ok=True)
    for kit_dir in kits:
        scene_dir = convert_kit_to_scene(kit_dir, dest, args.overwrite)
        print(scene_dir.relative_to(ROOT))

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
