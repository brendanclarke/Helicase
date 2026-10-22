#!/usr/bin/env python3
"""Convert LXR1 ``pNNN.snd`` kits into Helicase ``Kit/NNN <name>/`` folders.

Each kit becomes ``kitset.kcg`` plus six instrument files (three ``.drm``,
``.snr``, ``.cym``, ``.hat``) keyed by the firmware descriptor tables. Values
copy unchanged (both systems share the LXR1 value domains); LFO and velocity
targets are translated from the LXR1 encodings to Helicase tokens. Both the
2013 factory-kit format and the later format are read (see
``--target-encoding``); the pre-release V0.1x layout is refused.

Not carried: the six ``audio_out`` routes and the seven track MIDI notes.
They are Scene settings in Helicase, so a Kit has no field for them;
``all_to_bank.py`` puts them into each Scene's ``sceneset.scg``.

Usage:
    python3 tools/lxr1_converters/snd_to_kit.py [options] INPUT [INPUT ...]

INPUT is a ``.snd`` file or a directory of them (for example an unzipped LXR1
card). Output defaults to ``SD_CARD/Kit``. Slots are the lowest free slots at
or after ``--start-slot`` unless ``--keep-numbers`` reuses each file's LXR1
number (``p007.snd`` -> ``007``). See LXR1_CONVERTERS.md for the full manual.
"""

from __future__ import annotations

import argparse
from pathlib import Path

import lxr1_common as common


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("inputs", nargs="+", type=Path,
                        help=".snd files or directories containing them")
    parser.add_argument("--out", type=Path,
                        default=common.REPO_ROOT / "SD_CARD" / "Kit",
                        help="Kit library directory (default SD_CARD/Kit)")
    common.add_numbering_arguments(parser, "Kit")
    common.add_target_encoding_argument(parser)
    common.add_common_arguments(parser)
    args = parser.parse_args()
    common.check_start_slot(args.start_slot)

    # The descriptor tables define key order, morphable rows and target
    # tokens, so they are loaded (and checked against the key map) first.
    fw = common.Firmware(args.firmware_root)
    files = common.collect_inputs(args.inputs, (".snd",))
    slots = common.SlotAllocator(args.out, args.start_slot, args.overwrite,
                                 args.dry_run)
    report = common.Report()
    converted = []

    def convert(path: Path) -> None:
        kit = common.read_snd(path, args.target_encoding)
        number = common.lxr1_file_number(path) if args.keep_numbers else None
        if args.keep_numbers and number is None:
            raise ValueError("not a pNNN file name; drop --keep-numbers")
        # Build every instrument before claiming a slot, so a failure never
        # leaves a half-written Kit folder behind. This pass also collects
        # the target notes (also in a dry run); the write pass repeats the
        # same pure conversion silently.
        for slot in range(1, common.INSTRUMENT_SLOT_COUNT + 1):
            common.instrument_values(kit, slot, fw, report)
        kit_slot = slots.allocate(number)
        kit_dir = args.out / f"{kit_slot:03d} {common.safe_name(kit.name, 'Kit')}"
        if not args.dry_run:
            common.write_kit_dir(kit_dir, kit, fw, common.Report())
        print(f"{path.name} '{kit.name}' ({kit.target_encoding} targets) "
              f"-> {kit_dir}")
        converted.append(path)

    failures = common.convert_each(files, convert, report)
    print(f"{len(converted)} kit(s) converted, {failures} failed"
          + (" (dry run, nothing written)" if args.dry_run else ""))
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
