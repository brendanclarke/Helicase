#!/usr/bin/env python3
"""Validate compact typed Instrument indexes against a copied SD tree.

Inputs: one card-root path containing Instrument/<registry-directory>.
Outputs: actionable per-type diagnostics and exit status 0 only when each
.hcindex is sorted, unique, free of reserved/blank rows, and exactly represents
the physical user Instrument files. The tool is read-only. It mirrors firmware
index identity rules so captured-card acceptance can detect the `.hctmp`/blank
row regression without relying on front-panel behavior alone.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path
from typing import Dict, List, Optional, Tuple


MAX_STEM_LENGTH = 8
MAX_INDEX_ROWS = 1000
REGISTRY = (
    ("Drum", ".drm"),
    ("Snare", ".snr"),
    ("Cymbal", ".cym"),
    ("HiHat", ".hat"),
)


def ascii_fold(value: str) -> str:
    """Apply the firmware's ASCII-only FAT case fold."""

    return "".join(
        chr(ord(char) + (ord("a") - ord("A")))
        if "A" <= char <= "Z"
        else char
        for char in value
    )


def product_order(value: str) -> Tuple[str, str]:
    """Return the firmware's case-fold-then-raw-case sort key."""

    return ascii_fold(value), value


def reserved_temp_component(name: str, extension: str) -> bool:
    """Recognize `.hctmp.<ext>` and its generated short-alias family."""

    if ascii_fold(name) == ascii_fold(f".hctmp{extension}"):
        return True
    if name.startswith("."):
        return False
    dot = name.find(".")
    if dot < 0 or ascii_fold(name[dot + 1 :]) != ascii_fold(extension[1:]):
        return False
    base = name[:dot]
    if ascii_fold(base) == "hctmp":
        return True
    if len(base) <= 6 or ascii_fold(base[:6]) != "hctmp~":
        return False
    return bool(base[6:]) and all("0" <= char <= "9" for char in base[6:])


def normalize_stem(stem: str) -> str:
    """Mirror storage_copyDisplayName() for a validated ASCII stem."""

    return stem[:MAX_STEM_LENGTH].ljust(MAX_STEM_LENGTH, " ")


def usable_stem(stem: str) -> bool:
    """Require one printable, non-space character in the eight cells."""

    return any("!" <= char <= "~" for char in stem[:MAX_STEM_LENGTH])


def stem_reserved_temp(stem: str, extension: str) -> bool:
    """Apply the reserved alias test after removing fixed-width padding."""

    return reserved_temp_component(stem.rstrip(" "), extension)


def decode_line(raw: bytes) -> Tuple[Optional[str], Optional[str]]:
    """Decode one firmware-style index line without changing spaces."""

    if raw.endswith(b"\r"):
        raw = raw[:-1]
    try:
        text = raw.decode("ascii")
    except UnicodeDecodeError:
        return None, "contains non-ASCII bytes"
    return text, None


def read_index(index_path: Path, type_name: str, extension: str) -> Tuple[List[str], List[str]]:
    """Read and structurally validate one compact typed index."""

    errors: List[str] = []
    rows: List[str] = []
    try:
        data = index_path.read_bytes()
    except FileNotFoundError:
        return rows, [f"{type_name}: missing .hcindex ({index_path})"]
    except OSError as exc:
        return rows, [f"{type_name}: cannot read .hcindex: {exc}"]

    raw_lines = data.split(b"\n")
    if data.endswith(b"\n"):
        raw_lines.pop()
    for row_number, raw_line in enumerate(raw_lines, start=1):
        text, error = decode_line(raw_line)
        if error:
            errors.append(f"{type_name}: row {row_number}: {error}")
            continue
        assert text is not None
        field = text.split(",", 1)[0]
        if not field:
            errors.append(f"{type_name}: row {row_number}: blank stem")
            continue
        if len(field) > MAX_STEM_LENGTH:
            errors.append(
                f"{type_name}: row {row_number}: over-eight-character stem {field!r}"
            )
            continue
        if any(not (" " <= char <= "~") for char in field):
            errors.append(f"{type_name}: row {row_number}: non-printable stem {field!r}")
            continue
        normalized = normalize_stem(field)
        if not usable_stem(normalized):
            errors.append(f"{type_name}: row {row_number}: all-space/blank stem")
            continue
        if stem_reserved_temp(normalized, extension):
            errors.append(f"{type_name}: row {row_number}: reserved temporary stem {field!r}")
            continue
        if len(rows) >= MAX_INDEX_ROWS:
            errors.append(f"{type_name}: row {row_number}: exceeds {MAX_INDEX_ROWS} rows")
            continue
        if rows:
            previous = rows[-1]
            if (ascii_fold(previous) == ascii_fold(normalized) or
                    product_order(previous) >= product_order(normalized)):
                errors.append(
                    f"{type_name}: row {row_number}: duplicate/unsorted stem {field!r}"
                )
                continue
        rows.append(normalized)
    return rows, errors


def physical_files(directory: Path, type_name: str, extension: str) -> Tuple[Dict[str, List[str]], List[str]]:
    """Collect typed user files while excluding implementation objects."""

    errors: List[str] = []
    files_by_key: Dict[str, List[str]] = {}
    try:
        entries = sorted(directory.iterdir(), key=lambda path: path.name)
    except FileNotFoundError:
        return files_by_key, [f"{type_name}: missing directory ({directory})"]
    except OSError as exc:
        return files_by_key, [f"{type_name}: cannot scan directory: {exc}"]

    for path in entries:
        if not path.is_file():
            continue
        name = path.name
        if ascii_fold(name) == ".hcindex":
            continue
        if reserved_temp_component(name, extension):
            continue
        if not ascii_fold(name).endswith(ascii_fold(extension)):
            continue
        stem = name.split(".", 1)[0]
        if len(stem) == 0:
            errors.append(f"{type_name}: file {name!r}: blank derived stem")
            continue
        if len(stem) > MAX_STEM_LENGTH:
            errors.append(f"{type_name}: file {name!r}: over-eight-character stem")
            continue
        if any(not (" " <= char <= "~") for char in stem):
            errors.append(f"{type_name}: file {name!r}: non-printable derived stem")
            continue
        normalized = normalize_stem(stem)
        if not usable_stem(normalized):
            errors.append(f"{type_name}: file {name!r}: blank derived stem")
            continue
        key = ascii_fold(normalized)
        files_by_key.setdefault(key, []).append(name)
    return files_by_key, errors


def validate_type(root: Path, type_name: str, extension: str) -> list[str]:
    """Validate one registry directory and return actionable diagnostics."""

    directory = root / "Instrument" / type_name
    index_path = directory / ".hcindex"
    index_missing = not index_path.is_file()
    index_rows, errors = read_index(index_path, type_name, extension)
    physical, physical_errors = physical_files(directory, type_name, extension)
    errors.extend(physical_errors)

    index_by_key: Dict[str, List[int]] = {}
    for row_number, row in enumerate(index_rows, start=1):
        index_by_key.setdefault(ascii_fold(row), []).append(row_number)

    # A missing index is already one metadata defect; do not emit one
    # misleading "absent" diagnostic for every valid physical file behind it.
    if not index_missing:
        for key, row_numbers in index_by_key.items():
            row = index_rows[row_numbers[0] - 1]
            files = physical.get(key, [])
            if not files:
                errors.append(
                    f"{type_name}: row {row_numbers[0]} {row!r}: no physical {extension} file"
                )
            elif len(files) != 1:
                errors.append(
                    f"{type_name}: row {row_numbers[0]} {row!r}: resolves to {len(files)} files {files!r}"
                )

        for key, files in physical.items():
            if key not in index_by_key:
                errors.append(f"{type_name}: physical file(s) absent from index: {files!r}")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("card_root", type=Path, help="mounted or copied SD-card root")
    args = parser.parse_args()

    all_errors: List[str] = []
    for type_name, extension in REGISTRY:
        all_errors.extend(validate_type(args.card_root, type_name, extension))
    if all_errors:
        for error in all_errors:
            print(error, file=sys.stderr)
        print(f"FAIL: {len(all_errors)} Instrument index issue(s)", file=sys.stderr)
        return 1
    print("PASS: all four typed Instrument indexes match their physical user files")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
