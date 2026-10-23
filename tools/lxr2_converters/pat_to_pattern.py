#!/usr/bin/env python3
"""Convert LXR-02 ``NN-NAME.pat`` patterns into Helicase library PAT4 files.

An LXR-02 ``.pat`` holds one pattern (7 tracks x 64 steps); each pattern
with at least one playing step becomes ``Pattern/NNN <name>.pat``
(``--keep-empty`` also writes empty ones). Per track and step:

* one LXR-02 step -> one Helicase 1/16 step; the track length (1..64) is
  kept and the pattern shuffle is written to every track;
* velocity copies; the note moves up by 3 (Helicase plays note 63 at the
  original pitch, the LXR-02 note 60), so every step keeps its pitch;
* probability (percent) becomes the nearest r10..r95 condition; 100 is
  always, 0 never plays and is left out;
* flam x2/x3/x4/x6/x8 becomes the roll `32`/`32t`/`64`/`64t`/`128` (exact);
* shift +1..+7 becomes micro-timing (exact, 1/8 step per unit); shift
  -1..-7 moves the hit into the previous step with micro-timing when that
  step is free, else it plays on the beat (with a note);
* the two automation slots of the step the hit plays in become step
  automation; targets Helicase cannot automate are dropped.

The pattern's kit number has no meaning for a library Pattern; it is shown
in the output line and used by ``prj_to_bank.py`` / ``sng_to_bank.py``.

Usage:
    python3 tools/lxr2_converters/pat_to_pattern.py [options] INPUT [INPUT ...]

INPUT is a ``.pat`` file, a project folder or a card root. See
LXR2_CONVERTERS.md for the full manual.
"""

from __future__ import annotations

import argparse
from pathlib import Path

import lxr2_common as common


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("inputs", nargs="+", type=Path,
                        help=".pat files, project folders or a card root")
    parser.add_argument("--out", type=Path,
                        default=common.REPO_ROOT / "SD_CARD" / "Pattern",
                        help="Pattern library directory (default SD_CARD/Pattern)")
    parser.add_argument("--keep-empty", action="store_true",
                        help="also write patterns that have no playing steps")
    common.add_numbering_arguments(parser, "Pattern")
    common.add_common_arguments(parser)
    args = parser.parse_args()
    common.check_start_slot(args.start_slot)

    fw = common.Firmware(args.firmware_root)
    files = common.collect_inputs(args.inputs, (".pat",))
    # Library Patterns are files, so occupancy is read from "NNN *.pat".
    slots = common.SlotAllocator(args.out, args.start_slot, args.overwrite,
                                 args.dry_run, suffix=".pat")
    report = common.Report()
    written, skipped = [], []

    if not args.dry_run:
        args.out.mkdir(parents=True, exist_ok=True)

    def convert(path: Path) -> None:
        pattern, number = common.read_pat(path)
        if not args.keep_empty and not pattern.has_content():
            skipped.append(path)
            return
        if args.keep_numbers and number is None and \
                not common.is_tmp_buffer(path):
            raise ValueError("not an NN-NAME / pNNN file name; drop "
                             "--keep-numbers")
        if pattern.version < common.PATTERN_VERSION_MAX:
            report.note(f"PatV0{pattern.version} file: no flam/shift data"
                        + ("" if pattern.version > 1 else ", no kit number"))
        # Convert and serialise first (build_pat4 also re-decodes and checks
        # the image) so a bad file writes nothing.
        data = common.build_pat4(common.convert_pattern(
            pattern, fw, report, ""))
        slot = slots.allocate(number if args.keep_numbers else None)
        if pattern.name:
            stem = common.safe_name(pattern.name, "Pattern")
        else:
            # Unnamed slot: name it after its project and slot number.
            base = common.safe_name(common.project_name(path.parent), "Pattern")
            stem = (common.tagged_name(base, f"{number:02d}")
                    if number is not None else base)
        target = args.out / f"{slot:03d} {stem}.pat"
        if not args.dry_run:
            target.write_bytes(data)
        kit = f", kit {pattern.kit_nr:02d}" if pattern.kit_nr is not None else ""
        print(f"{path.parent.name}/{path.name} '{pattern.name}' "
              f"(PatV0{pattern.version}{kit}) -> {target}")
        written.append(target)

    failures = common.convert_each(files, convert, report)
    print(f"{len(written)} Pattern file(s) written, {len(skipped)} empty "
          f"pattern(s) skipped, {failures} failed"
          + (" (dry run, nothing written)" if args.dry_run else ""))
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
