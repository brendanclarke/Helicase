#!/usr/bin/env python3
"""Convert LXR-02 ``NN-NAME.snd`` kits into Helicase ``Kit/NNN <name>/`` folders.

Each kit becomes ``kitset.kcg`` plus six instrument files (three ``.drm``,
``.snr``, ``.cym``, ``.hat``) keyed by the firmware descriptor tables. The
LXR-02 kit shares its parameter offsets and value domains with the LXR1 kit,
so values copy unchanged, with three exceptions:

* waveform ``PWM`` becomes ``Rec`` (Helicase has no PWM; its waveform 5 is
  the ``Cym`` crash wave), and the pulse width is not carried;
* LFO and velocity targets are re-encoded from the LXR-02 ``modTargets``
  list; targets on PWM or FX parameters become off;
* the kit's FX block and all-voice sample rate (``srt``) have no Helicase
  Kit field and are reported, not converted.

Not carried: the six ``audio_out`` routes. They are Scene settings in
Helicase; ``prj_to_bank.py`` and ``sng_to_bank.py`` put them into each
Scene's ``sceneset.scg``.

Usage:
    python3 tools/lxr2_converters/snd_to_kit.py [options] INPUT [INPUT ...]

INPUT is a ``.snd`` file, a project folder (``projNN``), or a card root (all
its ``projNN`` folders). Untouched init-kit slots are skipped unless
``--keep-init`` is given. Output defaults to ``SD_CARD/Kit``. Slots are the
lowest free slots at or after ``--start-slot`` unless ``--keep-numbers``
reuses the LXR-02 slot (``07-NAME.snd`` -> ``007``). See LXR2_CONVERTERS.md
for the full manual.
"""

from __future__ import annotations

import argparse
from pathlib import Path

import lxr2_common as common


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("inputs", nargs="+", type=Path,
                        help=".snd files, project folders or a card root")
    parser.add_argument("--out", type=Path,
                        default=common.REPO_ROOT / "SD_CARD" / "Kit",
                        help="Kit library directory (default SD_CARD/Kit)")
    parser.add_argument("--keep-init", action="store_true",
                        help="also convert slots that still hold the "
                             "untouched LXR-02 init kit")
    common.add_numbering_arguments(parser, "Kit")
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
    converted, skipped = [], []

    def convert(path: Path) -> None:
        kit, number = common.read_snd(path)
        if kit.is_untouched_init_kit() and not args.keep_init:
            skipped.append(path)
            return
        if args.keep_numbers and number is None and \
                not common.is_tmp_buffer(path):
            raise ValueError("not an NN-NAME / pNNN file name; drop "
                             "--keep-numbers")
        # Translate and convert every instrument before claiming a slot, so
        # a failure never leaves a half-written Kit folder behind; this pass
        # also prints the conversion notes (also in a dry run).
        image = common.check_kit(kit, fw, report, "")
        kit_slot = slots.allocate(number if args.keep_numbers else None)
        fallback = f"Kit {number:02d}" if number is not None else "Kit"
        kit_dir = args.out / f"{kit_slot:03d} {common.safe_name(kit.name, fallback)}"
        if not args.dry_run:
            common.write_kit_dir(kit_dir, image, fw, common.Report())
        print(f"{path.parent.name}/{path.name} '{kit.name}' -> {kit_dir}")
        converted.append(path)

    failures = common.convert_each(files, convert, report)
    print(f"{len(converted)} kit(s) converted, {len(skipped)} untouched init "
          f"kit(s) skipped, {failures} failed"
          + (" (dry run, nothing written)" if args.dry_run else ""))
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
