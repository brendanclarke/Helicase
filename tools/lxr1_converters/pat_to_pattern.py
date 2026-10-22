#!/usr/bin/env python3
"""Convert LXR1 ``pNNN.pat`` pattern sets into Helicase library PAT4 files.

An LXR1 ``.pat`` holds eight patterns; a Helicase library file holds one.
Each LXR1 pattern with at least one playing step becomes
``Pattern/NNN <name> <1..8>.pat`` (``--keep-empty`` also writes the empty
ones). Per pattern and track:

* one LXR1 main step -> one Helicase 1/16 step; track length 1..16 is kept
  and the LXR1 track rotation is baked into the step order;
* the first playing sub-step gives velocity, note and probability (as the
  nearest r10..r95 condition) and, if it is not sub-step 0, micro-timing;
* evenly spaced sub-steps that fill the step become a roll (128/64/32);
  other shapes keep their first hit (``--partial-rolls fill`` writes a roll
  with a shorter fill instead, which ducks the voice at the fill end);
* the two automation slots of that sub-step become step automation; targets
  Helicase cannot automate are dropped;
* the pattern-set shuffle is written to every track.

LXR1 next-pattern/repeat settings only exist between Scenes, so they are
converted by ``all_to_bank.py``, not here.

Usage:
    python3 tools/lxr1_converters/pat_to_pattern.py [options] INPUT [INPUT ...]

See LXR1_CONVERTERS.md for the full manual.
"""

from __future__ import annotations

import argparse
from pathlib import Path

import lxr1_common as common


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("inputs", nargs="+", type=Path,
                        help=".pat files or directories containing them")
    parser.add_argument("--out", type=Path,
                        default=common.REPO_ROOT / "SD_CARD" / "Pattern",
                        help="Pattern library directory (default SD_CARD/Pattern)")
    parser.add_argument("--keep-empty", action="store_true",
                        help="also write LXR1 patterns that have no steps")
    parser.add_argument("--partial-rolls", choices=("first", "fill"),
                        default="first",
                        help="sub-step rhythms that do not fill the step: keep "
                             "the first hit (default) or roll with a short fill")
    common.add_common_arguments(parser)
    args = parser.parse_args()
    common.check_start_slot(args.start_slot)

    fw = common.Firmware(args.firmware_root)
    files = common.collect_inputs(args.inputs, (".pat",))
    # Library Patterns are files, so occupancy is read from "NNN *.pat".
    # Slots are always fresh: one LXR1 file yields up to eight Patterns, so
    # its file number cannot be reused as a slot.
    slots = common.SlotAllocator(args.out, args.start_slot, False,
                                 args.dry_run, suffix=".pat")
    report = common.Report()
    written = []

    if not args.dry_run:
        args.out.mkdir(parents=True, exist_ok=True)

    def convert(path: Path) -> None:
        name, pattern_set = common.read_pat(path)
        if not pattern_set.has_lengths:
            report.note("file predates track lengths; every track is 16 steps")
        base = common.safe_name(name, "Pattern")
        # Convert and serialise all eight first (build_pat4 also re-decodes
        # and checks each image) so a bad file writes nothing.
        images = []
        for index, src in enumerate(pattern_set.patterns):
            if not args.keep_empty and not common.pattern_has_content(src):
                continue
            label = f"pattern {index + 1}"
            pattern = common.convert_pattern(src, pattern_set.shuffle, fw,
                                             args.partial_rolls, report, label)
            images.append((index, label, common.build_pat4(pattern)))
        for index, label, data in images:
            slot = slots.allocate()
            stem = common.numbered_name(base, index + 1)
            target = args.out / f"{slot:03d} {stem}.pat"
            if not args.dry_run:
                target.write_bytes(data)
            print(f"{path.name} '{name}' {label} -> {target}")
            written.append(target)

    failures = common.convert_each(files, convert, report)
    print(f"{len(written)} Pattern file(s) from {len(files) - failures} LXR1 "
          f"file(s), {failures} failed"
          + (" (dry run, nothing written)" if args.dry_run else ""))
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
