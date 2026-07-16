#!/usr/bin/env python3
"""Populate SD_CARD/Bank from an existing root Scene folder.

Default behavior creates the initial bridge fixture:

    SD_CARD/Bank/000 Slak/bankset.bcg
    SD_CARD/Bank/000 Slak/00 Slak/<scene payload>

Bank identity is the containing ``NNN Name`` directory. Bank-local Scene
identity is the containing ``SS Name`` directory. No generated file stores its
own name.
"""

from __future__ import annotations

import argparse
import re
import shutil
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SCENE_ROOT = ROOT / "SD_CARD" / "Scene"
DEFAULT_BANK_ROOT = ROOT / "SD_CARD" / "Bank"

SCENE_DIR_RE = re.compile(r"^(\d{3})[ _](.+)$")


def parse_root_scene_dir(path: Path) -> tuple[int, str] | None:
    match = SCENE_DIR_RE.match(path.name)
    if not match:
        return None
    return int(match.group(1)), match.group(2).strip()


def make_bankset(active_scene: int) -> str:
    return "\n".join(
        [
            "format=helicase.bankset",
            "version=1",
            f"active_scene={active_scene}",
            "",
        ]
    )


def populate_bank_from_scene(
    scene_dir: Path,
    bank_root: Path,
    bank_slot: int,
    child_slot: int,
    overwrite: bool,
) -> Path:
    parsed = parse_root_scene_dir(scene_dir)
    if parsed is None:
        raise ValueError(f"not a numbered Scene directory: {scene_dir}")
    if not 0 <= bank_slot <= 999:
        raise ValueError("bank slot must be 000..999")
    if not 0 <= child_slot <= 15:
        raise ValueError("Bank-local Scene slot must be 00..15")

    _, name = parsed
    bank_dir = bank_root / f"{bank_slot:03d} {name}"
    child_dir = bank_dir / f"{child_slot:02d} {name}"

    if bank_dir.exists() and overwrite:
        shutil.rmtree(bank_dir)
    elif bank_dir.exists() and any(bank_dir.iterdir()):
        raise FileExistsError(f"{bank_dir} already exists; pass --overwrite")

    bank_dir.mkdir(parents=True, exist_ok=True)
    shutil.copytree(
        scene_dir,
        child_dir,
        ignore=shutil.ignore_patterns(".DS_Store"),
    )
    (bank_dir / "bankset.bcg").write_text(
        make_bankset(child_slot),
        encoding="ascii",
        newline="\n",
    )
    return bank_dir


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path,
                        default=DEFAULT_SCENE_ROOT / "000 Slak")
    parser.add_argument("--dest", type=Path, default=DEFAULT_BANK_ROOT)
    parser.add_argument("--bank-slot", type=int, default=0)
    parser.add_argument("--scene-slot", type=int, default=0)
    parser.add_argument("--overwrite", action="store_true")
    args = parser.parse_args()

    source = args.source.resolve()
    dest = args.dest.resolve()
    if not source.is_dir():
        raise SystemExit(f"source Scene directory not found: {source}")

    bank_dir = populate_bank_from_scene(
        source,
        dest,
        args.bank_slot,
        args.scene_slot,
        args.overwrite,
    )
    print(bank_dir.relative_to(ROOT))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
