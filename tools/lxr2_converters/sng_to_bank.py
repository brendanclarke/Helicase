#!/usr/bin/env python3
"""Convert LXR-02 songs (``NN-NAME.sng``) into chained Helicase Banks.

An LXR-02 song is a list of up to 64 pattern numbers that play one after
another, each to the end of its longest track, then stops (``end``) or
starts again (``loop``). A song only names patterns, so it is converted
together with the project folder it lives in (its patterns and kits).

Each song becomes ``Bank/NNN <song name>/`` whose Scenes are the song's
entries in play order, chained so the Bank plays the song by itself:

* consecutive entries of the same pattern merge into one Scene that repeats
  (chain condition ``x01``..``x08``, ``x16``, ``x32``, ``x64``; longer or
  other counts are split over several Scenes);
* each Scene's chain ``cn1``/``nx1`` moves to the next Scene (``sNN``); the
  last one stops playback (``stp``) for ``end`` or returns to Scene 00 for
  ``loop``; a one-Scene loop needs no chain;
* Scenes carry the pattern, its kit, the audio-out routes and the MIDI
  channels/notes exactly as ``prj_to_bank.py`` writes them, and Scenes that
  play the same kit share one VOICE edit mask;
* a song longer than 16 Scenes keeps its first 16 (the last then stops).

Every project has a default song (pattern 00, then end); it is skipped
unless ``--keep-default`` is given. Empty songs are always skipped.

Usage:
    python3 tools/lxr2_converters/sng_to_bank.py [options] INPUT [INPUT ...]

INPUT is a ``.sng`` file (inside its project folder), a project folder or a
card root. See LXR2_CONVERTERS.md for the full manual.
"""

from __future__ import annotations

import argparse
from pathlib import Path

import lxr2_common as common

STOP_NAMES = {"stop": "end", "loop": "loop"}


def song_scenes(song: common.Lxr2Song, project: common.Lxr2Project,
                fw: common.Firmware,
                report: common.Report) -> list[common.SceneSpec]:
    """Plan, convert and chain the Scenes of one song (nothing written)."""
    plan, ending = common.song_plan(song)
    if len(plan) > common.BANK_SCENES:
        report.note(f"song needs {len(plan)} Scenes after merging repeats; "
                    f"the first {common.BANK_SCENES} are kept and playback "
                    "stops after the last")
        plan, ending = plan[:common.BANK_SCENES], "stop"
    chains = common.song_chains(plan, ending)
    base = common.safe_name(project.name, "Project")
    kits: dict[int, tuple] = {}
    converted: dict[int, tuple] = {}
    scenes = []
    for index, ((slot, _), chain) in enumerate(zip(plan, chains)):
        if slot not in converted:
            # A pattern used by several Scenes is converted, and its kit
            # resolved, once (so its notes print once).
            label = f"pattern {slot:02d}"
            pattern = project.patterns[slot]
            if pattern is None:
                report.note(f"{label} has no file; its Scene is empty")
                pattern = common.empty_pattern()
            kit_slot, kit = project.kit_for(pattern, report, label)
            pattern_out = common.convert_pattern(pattern, fw, report, label)
            converted[slot] = (pattern, kit_slot, kit, pattern_out,
                               common.build_pat4(pattern_out))
        pattern, kit_slot, kit, pattern_out, pat4 = converted[slot]
        if kit_slot not in kits:
            kit_label = f"kit {kit_slot:02d}"
            kits[kit_slot] = (common.check_kit(kit, fw, report, kit_label),
                              common.kit_audio_out(kit, report, kit_label))
        image, audio_out = kits[kit_slot]
        name = common.safe_name(pattern.name,
                                common.tagged_name(base, f"{slot:02d}"))
        scenes.append(common.SceneSpec(index, name, pattern_out, pat4,
                                       kit_slot, kit, image, audio_out, chain))
    # The LXR-02 restarts every track at a pattern change. Helicase does so
    # for a chain-fired change only with pcr on; with pcr off it counts
    # repetitions on the running master clock, which shifts the change
    # points when Scenes differ in length.
    if len({max(scene.pattern.length) for scene in scenes}) > 1:
        report.note("Scenes differ in length: set pcr (PC reset) on so each "
                    "Scene restarts and counts its repeats from its own start, "
                    "as the LXR-02 does")
    return scenes


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("inputs", nargs="+", type=Path,
                        help=".sng files, project folders or a card root")
    parser.add_argument("--out", type=Path,
                        default=common.REPO_ROOT / "SD_CARD" / "Bank",
                        help="Bank library directory (default SD_CARD/Bank)")
    parser.add_argument("--keep-default", action="store_true",
                        help="also convert songs that are still the project "
                             "default (pattern 00, then end)")
    common.add_common_arguments(parser)
    args = parser.parse_args()
    common.check_start_slot(args.start_slot)

    fw = common.Firmware(args.firmware_root)
    files = common.collect_inputs(args.inputs, (".sng",))
    slots = common.SlotAllocator(args.out, args.start_slot, False, args.dry_run)
    report = common.Report()
    projects: dict[Path, common.Lxr2Project | ValueError] = {}
    written, skipped = [], []

    def project_of(path: Path) -> common.Lxr2Project:
        """Read each song's project folder once (a read error is kept and
        reported for every song of that folder)."""
        folder = path.parent
        if folder not in projects:
            try:
                projects[folder] = common.read_project(folder, report)
            except ValueError as error:
                projects[folder] = error
        result = projects[folder]
        if isinstance(result, ValueError):
            raise ValueError(f"project {folder.name}: {result}")
        return result

    def convert(path: Path) -> None:
        song = common.read_sng(path)
        patterns, ending = song.sequence()
        if not patterns or (song.is_default() and not args.keep_default):
            skipped.append(path)
            return
        project = project_of(path)
        midi_channel, midi_note = common.scene_midi_settings(project, report)
        scenes = song_scenes(song, project, fw, report)
        bank_slot = slots.allocate()
        number = song.slot if song.slot is not None else 0
        fallback = common.tagged_name(common.safe_name(project.name, "Project"),
                                      f"s{number:02d}")
        bank_dir = args.out / f"{bank_slot:03d} {common.safe_name(song.name, fallback)}"
        if not args.dry_run:
            common.write_bank(bank_dir, scenes, fw, midi_channel, midi_note)
        print(f"{path.parent.name}/{path.name} '{song.name}' ({len(patterns)} "
              f"entries, {STOP_NAMES[ending]}) -> {bank_dir} "
              f"({len(scenes)} Scene(s))")
        written.append(bank_dir)

    failures = common.convert_each(files, convert, report)
    print(f"{len(written)} song Bank(s) written, {len(skipped)} empty or "
          f"default song(s) skipped, {failures} failed"
          + (" (dry run, nothing written)" if args.dry_run else ""))
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
