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

KIT_DIR_RE = re.compile(r"^(\d{3})[ _](.+)$")
DEFAULT_AUDIO_OUT = [2, 0, 0, 0, 0, 1]


def parse_numbered_dir(path: Path) -> tuple[int, str] | None:
    match = KIT_DIR_RE.match(path.name)
    if not match:
        return None
    return int(match.group(1)), match.group(2).strip()


def strip_legacy_audio_out(kitset_path: Path) -> list[int]:
    """Move legacy embedded Kit routing into Scene-owned sceneset data.

    Inputs: copied embedded ``kitset.kcg`` path. Outputs: a six-value route
    list for ``sceneset.scg``. Side effect: removes ``audio_out=`` lines from
    the copied kitset so generated Scene fixtures match the new writer.
    """
    audio_out: list[int] = []
    kept: list[str] = []
    for line in kitset_path.read_text(encoding="ascii").splitlines():
        stripped = line.strip()
        if stripped.startswith("audio_out="):
            try:
                audio_out.append(int(stripped.split("=", 1)[1]))
            except ValueError:
                index = len(audio_out)
                audio_out.append(DEFAULT_AUDIO_OUT[index]
                                 if index < len(DEFAULT_AUDIO_OUT) else 0)
            continue
        kept.append(line)
    kitset_path.write_text("\n".join(kept).rstrip() + "\n", encoding="ascii")
    if len(audio_out) != 6:
        return DEFAULT_AUDIO_OUT.copy()
    return [value if 0 <= value <= 5 else DEFAULT_AUDIO_OUT[i]
            for i, value in enumerate(audio_out)]


def make_thin_pattern_stub() -> str:
    return "\n".join(
        [
            "format=helicase.pattern",
            "version=1",
            "placeholder=1",
        ]
    )


def make_sceneset(scene_name: str, audio_out: list[int]) -> str:
    # Scene identity is the containing "NNN Name" directory, never a name=
    # field in sceneset.scg. The scene_name argument is retained so the call
    # site documents which directory identity this settings payload belongs to,
    # but the writer serializes only Scene-level values.
    _ = scene_name
    return "\n".join(
        [
            "format=helicase.sceneset",
            "version=1",
            "morph_amount=0",
            "voice_morph_amount=0,0,0,0,0,0",
            "voice_decimation_all=127",
            "midi_channel=1,2,3,4,5,6,7",
            "midi_note=63,63,63,63,63,63,63",
            "audio_out=" + ",".join(str(value) for value in audio_out),
            "fx_send_amount=0,0,0,0,0,0",
            "fader_setting=0,0,0,0,0,0",
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
    shutil.copytree(
        kit_dir,
        embedded_kit_dir,
        ignore=shutil.ignore_patterns(".DS_Store"),
    )
    audio_out = strip_legacy_audio_out(embedded_kit_dir / "kitset.kcg")

    (scene_dir / "sceneset.scg").write_text(
        make_sceneset(name, audio_out),
        encoding="ascii",
        newline="\n",
    )
    (scene_dir / "pattern.pat").write_text(
        make_thin_pattern_stub(),
        encoding="ascii",
        newline="\n",
    )
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
