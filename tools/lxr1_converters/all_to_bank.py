#!/usr/bin/env python3
"""Convert LXR1 ``pNNN.all`` / ``pNNN.prf`` files into Helicase Bank folders.

Each file becomes ``Bank/NNN <name>/`` with ``bankset.bcg`` and eight
Bank-local Scenes ``00``..``07``, one per LXR1 pattern:

* every Scene embeds the same converted Kit (``Kit <name>/``);
* the LXR1 values that are Scene settings in Helicase go into every
  ``sceneset.scg``: the six voice audio-out routes and the seven track MIDI
  notes (from the kit) and, for ``.all``, the seven track MIDI channels
  (from the globals block; a ``.prf`` has none, so tracks stay ``glb``);
  track lengths and shuffle are in the Pattern and its Morph endpoints;
* every Scene holds its pattern as PAT4 (same conversion as
  ``pat_to_pattern.py``);
* all eight Scenes share one VOICE edit mask (``scene_mask_voice_edit_N =
  00ff``), so a sound edit in one reaches all of them, as the single LXR1
  kit did;
* LXR1 "next pattern after N bars" becomes the Scene chain (``cn1`` = bar
  count ``b01``..``b08``/``b16``/``b32``/``b64``, ``nx1`` = ``s01``..``s08``
  or ``r02``..``r08``);
* Scene 00 is the active Scene; no Effect is written (Scenes load ``off``).

True globals (BPM, bar reset, MIDI routing/filters, clock and quantisation
settings) are global in Helicase too and are not converted.

Usage:
    python3 tools/lxr1_converters/all_to_bank.py [options] INPUT [INPUT ...]

See LXR1_CONVERTERS.md for the full manual.
"""

from __future__ import annotations

import argparse
from pathlib import Path

import lxr1_common as common


def build_scenes(perf: common.Lxr1Performance, fw: common.Firmware,
                 partial_rolls: str, report: common.Report) -> list[tuple]:
    """Convert the eight patterns into (pattern, PAT4 bytes, chain) tuples.

    Everything is converted (and every PAT4 image verified) before anything
    is written, so a failing file leaves no partial Bank folder.
    """
    scenes = []
    for index, src in enumerate(perf.patterns.patterns):
        label = f"pattern {index + 1}"
        pattern = common.convert_pattern(src, perf.patterns.shuffle, fw,
                                         partial_rolls, report, label)
        scenes.append((pattern, common.build_pat4(pattern),
                       common.chain_for(index, src, report, label)))
    return scenes


def write_bank(bank_dir: Path, perf: common.Lxr1Performance,
               scenes: list[tuple], fw: common.Firmware) -> None:
    """Write bankset.bcg and the Scene folders 00..07 of one Bank."""
    base = common.safe_name(perf.name, "Bank")
    kit_name = common.safe_name(perf.kit.name, "Kit")
    audio_out, midi_note = common.scene_settings_from_kit(perf.kit)
    for index, (pattern, data, chain) in enumerate(scenes):
        # Bank-local Scene folders are "SS <name>"; the Pattern file inside
        # carries the same display name.
        scene_name = common.numbered_name(base, index + 1)
        scene_dir = bank_dir / f"{index:02d} {scene_name}"
        scene_dir.mkdir(parents=True)
        common.write_kit_dir(scene_dir / f"Kit {kit_name}", perf.kit, fw,
                             common.Report())
        (scene_dir / f"{scene_name}.pat").write_bytes(data)
        common.write_sceneset(scene_dir / "sceneset.scg", pattern, audio_out,
                              perf.midi_channels, midi_note, chain)
    common.write_bankset(bank_dir / "bankset.bcg", len(scenes))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("inputs", nargs="+", type=Path,
                        help=".all/.prf files or directories containing them")
    parser.add_argument("--out", type=Path,
                        default=common.REPO_ROOT / "SD_CARD" / "Bank",
                        help="Bank library directory (default SD_CARD/Bank)")
    parser.add_argument("--partial-rolls", choices=("first", "fill"),
                        default="first",
                        help="sub-step rhythms that do not fill the step: keep "
                             "the first hit (default) or roll with a short fill")
    common.add_numbering_arguments(parser, "Bank")
    common.add_target_encoding_argument(parser)
    common.add_common_arguments(parser)
    args = parser.parse_args()
    common.check_start_slot(args.start_slot)

    fw = common.Firmware(args.firmware_root)
    files = common.collect_inputs(args.inputs, (".all", ".prf"))
    slots = common.SlotAllocator(args.out, args.start_slot, args.overwrite,
                                 args.dry_run)
    report = common.Report()
    converted = []

    def convert(path: Path) -> None:
        perf = common.read_performance(path, args.target_encoding)
        if not perf.patterns.has_lengths:
            report.note("file predates track lengths; every track is 16 steps")
        number = common.lxr1_file_number(path) if args.keep_numbers else None
        if args.keep_numbers and number is None:
            raise ValueError("not a pNNN file name; drop --keep-numbers")
        # Convert the kit once with the shared report so its target notes
        # appear once per Bank, not once per Scene.
        for slot in range(1, common.INSTRUMENT_SLOT_COUNT + 1):
            common.instrument_values(perf.kit, slot, fw, report)
        scenes = build_scenes(perf, fw, args.partial_rolls, report)
        bank_slot = slots.allocate(number)
        bank_dir = args.out / f"{bank_slot:03d} {common.safe_name(perf.name, 'Bank')}"
        if not args.dry_run:
            write_bank(bank_dir, perf, scenes, fw)
        print(f"{path.name} '{perf.name}' (.{perf.kind} v{perf.version}) "
              f"-> {bank_dir}")
        converted.append(path)

    failures = common.convert_each(files, convert, report)
    print(f"{len(converted)} Bank(s) converted, {failures} failed"
          + (" (dry run, nothing written)" if args.dry_run else ""))
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
