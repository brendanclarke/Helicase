#!/usr/bin/env python3
"""Convert LXR-02 project folders (``projNN``) into Helicase Bank folders.

An LXR-02 project holds 64 patterns, 64 kits, 64 songs and the globals; a
Helicase Bank holds 16 Scenes. Each group of 16 patterns (00..15, 16..31,
32..47, 48..63, the LXR-02's own pattern banks) becomes one Bank
``Bank/NNN <project> A..D/`` with ``bankset.bcg`` and one Scene per pattern
that has playing steps; Scene slot SS is the pattern's place in its group
(pattern 21 -> Bank B, Scene 05), so empty patterns leave gaps:

* each Scene embeds the kit the LXR-02 plays with that pattern: its kit
  number when the pattern mode is ``Aut`` (the default), kit 00 when it is
  ``off``; Scenes that play the same kit share one VOICE edit mask, so a
  sound edit reaches all of them as it did on the LXR-02;
* every ``sceneset.scg`` gets the kit's six voice audio-out routes (a voice
  routed to the FX bus goes to the FX output) and, from ``glo.cfg``, the
  seven track MIDI channels and MIDI notes (fixed note -> track note root);
* every Scene holds its pattern as PAT4 (same conversion as
  ``pat_to_pattern.py``); no Effect is written (Scenes load ``off``);
* the lowest written Scene is the active Scene.

Songs are converted by ``sng_to_bank.py``. True globals (BPM, bar reset,
MIDI routing/filters, clock, the global MIDI channel) are global in
Helicase too and are not converted.

Usage:
    python3 tools/lxr2_converters/prj_to_bank.py [options] INPUT [INPUT ...]

INPUT is a project folder or a card root (all its ``projNN`` folders). See
LXR2_CONVERTERS.md for the full manual.
"""

from __future__ import annotations

import argparse
from pathlib import Path

import lxr2_common as common

GROUP_TAGS = "ABCD"


def project_scenes(project: common.Lxr2Project, fw: common.Firmware,
                   keep_empty: bool,
                   report: common.Report) -> list[list[common.SceneSpec]]:
    """Convert every pattern of a project into Scenes, 16 per Bank group.

    Each kit is translated once (its notes print once per project). Every
    PAT4 image is built and verified here, before anything is written.
    """
    kits: dict[int, tuple] = {}
    groups: list[list[common.SceneSpec]] = []
    base = common.safe_name(project.name, "Project")
    for group in range(common.NUM_SLOTS // common.BANK_SCENES):
        scenes = []
        for index in range(common.BANK_SCENES):
            slot = group * common.BANK_SCENES + index
            pattern = project.patterns[slot]
            if pattern is None or not pattern.has_content():
                if not keep_empty:
                    continue
                pattern = pattern or common.empty_pattern()
            label = f"pattern {slot:02d}"
            kit_slot, kit = project.kit_for(pattern, report, label)
            if kit_slot not in kits:
                kit_label = f"kit {kit_slot:02d}"
                kits[kit_slot] = (common.check_kit(kit, fw, report, kit_label),
                                  common.kit_audio_out(kit, report, kit_label))
            image, audio_out = kits[kit_slot]
            converted = common.convert_pattern(pattern, fw, report, label)
            name = common.safe_name(pattern.name,
                                    common.tagged_name(base, f"{slot:02d}"))
            scenes.append(common.SceneSpec(
                index, name, converted, common.build_pat4(converted),
                kit_slot, kit, image, audio_out))
        groups.append(scenes)
    return groups


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("inputs", nargs="+", type=Path,
                        help="project folders (projNN) or a card root")
    parser.add_argument("--out", type=Path,
                        default=common.REPO_ROOT / "SD_CARD" / "Bank",
                        help="Bank library directory (default SD_CARD/Bank)")
    parser.add_argument("--keep-empty", action="store_true",
                        help="write all 16 Scenes of every Bank, including "
                             "empty or missing patterns")
    common.add_common_arguments(parser)
    args = parser.parse_args()
    common.check_start_slot(args.start_slot)

    fw = common.Firmware(args.firmware_root)
    folders = common.collect_projects(args.inputs)
    slots = common.SlotAllocator(args.out, args.start_slot, False, args.dry_run)
    report = common.Report()
    written = []

    def convert(folder: Path) -> None:
        project = common.read_project(folder, report)
        midi_channel, midi_note = common.scene_midi_settings(project, report)
        groups = project_scenes(project, fw, args.keep_empty, report)
        if not any(groups):
            report.note("no pattern has playing steps; no Bank written")
            return
        base = common.safe_name(project.name, "Project")
        for group, scenes in enumerate(groups):
            if not scenes:
                continue
            bank_slot = slots.allocate()
            bank_name = common.tagged_name(base, GROUP_TAGS[group])
            bank_dir = args.out / f"{bank_slot:03d} {bank_name}"
            if not args.dry_run:
                common.write_bank(bank_dir, scenes, fw, midi_channel, midi_note)
            first = group * common.BANK_SCENES
            print(f"{folder.name} '{project.name}' patterns {first:02d}.."
                  f"{first + common.BANK_SCENES - 1:02d} -> {bank_dir} "
                  f"({len(scenes)} Scene(s))")
            written.append(bank_dir)

    failures = common.convert_each(folders, convert, report)
    print(f"{len(written)} Bank(s) from {len(folders) - failures} project(s), "
          f"{failures} failed"
          + (" (dry run, nothing written)" if args.dry_run else ""))
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
