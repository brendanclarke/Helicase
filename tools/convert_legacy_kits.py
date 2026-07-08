#!/usr/bin/env python3
"""Convert legacy flat Pxxx.SND kits into the Phase 2 Kit/ folder layout."""

from __future__ import annotations

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SD_ROOT = ROOT / "SD_CARD"
PARAM_HEADER = ROOT / "Core" / "Scene" / "Preset" / "ParameterArray.h"
MIDI_MESSAGES_HEADER = ROOT / "Core" / "MIDI" / "MidiMessages.h"
INSTRUMENT_ENDPOINT_SIZE = 64
CC2_PAYLOAD_BASE = 128


DRUM_FIELDS = [
    "osc_wave", "coarse", "fine", "mod_wave", "filter_freq", "reso",
    "velo_attack", "velo_decay", "vol_slope", "pitch_decay", "mod_amount",
    "pitch_slope", "fm_amount", "fm_freq", "volume", "pan", "drive",
    "voice_decimation", "freq_lfo", "amount_lfo", "filter_drive",
    "mix_mod", "volume_mod_on_off", "velo_mod_amt", "vel_dest",
    "wave_lfo", "voice_lfo", "target_lfo", "retrigger_lfo", "sync_lfo",
    "offset_lfo", "filter_type", "transient_vol", "transient_wave",
    "transient_freq",
]

SNARE_FIELDS = [
    "osc_wave", "coarse", "fine", "noise_freq", "mix", "filter_freq",
    "reso", "velo_attack", "velo_decay", "vol_slope", "repeat",
    "pitch_decay", "mod_amount", "pitch_slope", "volume", "pan",
    "drive", "voice_decimation", "freq_lfo", "amount_lfo", "filter_drive",
    "volume_mod_on_off", "velo_mod_amt", "vel_dest", "wave_lfo",
    "voice_lfo", "target_lfo", "retrigger_lfo", "sync_lfo", "offset_lfo",
    "filter_type", "transient_vol", "transient_wave", "transient_freq",
]

CYMBAL_FIELDS = [
    "wave1", "coarse", "fine", "mod_osc1_freq", "mod_osc2_freq",
    "mod_osc1_gain", "mod_osc2_gain", "wave2", "wave3", "filter_freq",
    "reso", "velo_attack", "velo_decay", "vol_slope", "repeat", "volume",
    "pan", "drive", "voice_decimation", "freq_lfo", "amount_lfo",
    "filter_drive", "volume_mod_on_off", "velo_mod_amt", "vel_dest",
    "wave_lfo", "voice_lfo", "target_lfo", "retrigger_lfo", "sync_lfo",
    "offset_lfo", "filter_type", "transient_vol", "transient_wave",
    "transient_freq",
]

HAT_FIELDS = [
    "wave1", "coarse", "fine", "mod_osc1_freq", "mod_osc2_freq",
    "mod_osc1_gain", "mod_osc2_gain", "wave2", "wave3", "filter_freq",
    "reso", "velo_attack", "decay_closed", "decay_open", "vol_slope",
    "volume", "pan", "drive", "voice_decimation", "freq_lfo",
    "amount_lfo", "filter_drive", "volume_mod_on_off", "velo_mod_amt",
    "vel_dest", "wave_lfo", "voice_lfo", "target_lfo", "retrigger_lfo",
    "sync_lfo", "offset_lfo", "filter_type", "transient_vol",
    "transient_wave", "transient_freq",
]


DRUM_PARAM_TEMPLATE = [
    "PAR_OSC_WAVE_DRUM{n}", "PAR_COARSE{n}", "PAR_FINE{n}",
    "PAR_MOD_WAVE_DRUM{n}", "PAR_FILTER_FREQ_{n}", "PAR_RESO_{n}",
    "PAR_VELOA{n}", "PAR_VELOD{n}", "PAR_VOL_SLOPE{n}", "PAR_MOD_EG{n}",
    "PAR_MODAMNT{n}", "PAR_PITCH_SLOPE{n}", "PAR_FMAMNT{n}",
    "PAR_FM_FREQ{n}", "PAR_VOL{n}", "PAR_PAN{n}", "PAR_DRIVE{n}",
    "PAR_VOICE_DECIMATION{n}", "PAR_FREQ_LFO{n}", "PAR_AMOUNT_LFO{n}",
    "PAR_FILTER_DRIVE_{n}", "PAR_MIX_MOD_{n}", "PAR_VOLUME_MOD_ON_OFF{n}",
    "PAR_VELO_MOD_AMT_{n}", "PAR_VEL_DEST_{n}", "PAR_WAVE_LFO{n}",
    "PAR_VOICE_LFO{n}", "PAR_TARGET_LFO{n}", "PAR_RETRIGGER_LFO{n}",
    "PAR_SYNC_LFO{n}", "PAR_OFFSET_LFO{n}", "PAR_FILTER_TYPE_{n}",
    "PAR_TRANS{n}_VOL", "PAR_TRANS{n}_WAVE", "PAR_TRANS{n}_FREQ",
]

SNARE_PARAMS = [
    "PAR_OSC_WAVE_SNARE", "PAR_COARSE4", "PAR_FINE4", "PAR_NOISE_FREQ1",
    "PAR_MIX1", "PAR_FILTER_FREQ_4", "PAR_RESO_4", "PAR_VELOA4",
    "PAR_VELOD4", "PAR_VOL_SLOPE4", "PAR_REPEAT4", "PAR_MOD_EG4",
    "PAR_MODAMNT4", "PAR_PITCH_SLOPE4", "PAR_VOL4", "PAR_PAN4",
    "PAR_SNARE_DISTORTION", "PAR_VOICE_DECIMATION4", "PAR_FREQ_LFO4",
    "PAR_AMOUNT_LFO4", "PAR_FILTER_DRIVE_4", "PAR_VOLUME_MOD_ON_OFF4",
    "PAR_VELO_MOD_AMT_4", "PAR_VEL_DEST_4", "PAR_WAVE_LFO4",
    "PAR_VOICE_LFO4", "PAR_TARGET_LFO4", "PAR_RETRIGGER_LFO4",
    "PAR_SYNC_LFO4", "PAR_OFFSET_LFO4", "PAR_FILTER_TYPE_4",
    "PAR_TRANS4_VOL", "PAR_TRANS4_WAVE", "PAR_TRANS4_FREQ",
]

CYMBAL_PARAMS = [
    "PAR_WAVE1_CYM", "PAR_COARSE5", "PAR_FINE5", "PAR_MOD_OSC_F1_CYM",
    "PAR_MOD_OSC_F2_CYM", "PAR_MOD_OSC_GAIN1_CYM",
    "PAR_MOD_OSC_GAIN2_CYM", "PAR_WAVE2_CYM", "PAR_WAVE3_CYM",
    "PAR_FILTER_FREQ_5", "PAR_RESO_5", "PAR_VELOA5", "PAR_VELOD5",
    "PAR_VOL_SLOPE5", "PAR_REPEAT5", "PAR_VOL5", "PAR_PAN5",
    "PAR_CYMBAL_DISTORTION", "PAR_VOICE_DECIMATION5", "PAR_FREQ_LFO5",
    "PAR_AMOUNT_LFO5", "PAR_FILTER_DRIVE_5", "PAR_VOLUME_MOD_ON_OFF5",
    "PAR_VELO_MOD_AMT_5", "PAR_VEL_DEST_5", "PAR_WAVE_LFO5",
    "PAR_VOICE_LFO5", "PAR_TARGET_LFO5", "PAR_RETRIGGER_LFO5",
    "PAR_SYNC_LFO5", "PAR_OFFSET_LFO5", "PAR_FILTER_TYPE_5",
    "PAR_TRANS5_VOL", "PAR_TRANS5_WAVE", "PAR_TRANS5_FREQ",
]

HAT_PARAMS = [
    "PAR_WAVE1_HH", "PAR_COARSE6", "PAR_FINE6", "PAR_MOD_OSC_F1",
    "PAR_MOD_OSC_F2", "PAR_MOD_OSC_GAIN1", "PAR_MOD_OSC_GAIN2",
    "PAR_WAVE2_HH", "PAR_WAVE3_HH", "PAR_FILTER_FREQ_6", "PAR_RESO_6",
    "PAR_VELOA6", "PAR_VELOD6_CLOSED", "PAR_VELOD6_OPEN",
    "PAR_VOL_SLOPE6", "PAR_VOL6", "PAR_PAN6", "PAR_HAT_DISTORTION",
    "PAR_VOICE_DECIMATION6", "PAR_FREQ_LFO6", "PAR_AMOUNT_LFO6",
    "PAR_FILTER_DRIVE_6", "PAR_VOLUME_MOD_ON_OFF6",
    "PAR_VELO_MOD_AMT_6", "PAR_VEL_DEST_6", "PAR_WAVE_LFO6",
    "PAR_VOICE_LFO6", "PAR_TARGET_LFO6", "PAR_RETRIGGER_LFO6",
    "PAR_SYNC_LFO6", "PAR_OFFSET_LFO6", "PAR_FILTER_TYPE_6",
    "PAR_TRANS6_VOL", "PAR_TRANS6_WAVE", "PAR_TRANS6_FREQ",
]


def parse_param_values() -> tuple[dict[str, int], int]:
    text = PARAM_HEADER.read_text(encoding="utf-8")
    body = text.split("enum ParamEnums", 1)[1].split("};", 1)[0]
    values: dict[str, int] = {}
    current = -1

    for raw in body.splitlines():
        line = raw.split("//", 1)[0].strip().rstrip(",").strip()
        if not line or not line.startswith("PAR_") and line not in {"END_OF_SOUND_PARAMETERS", "NUM_PARAMS"}:
            continue
        if "=" in line:
            name, expr = [part.strip() for part in line.split("=", 1)]
            if expr in values:
                value = values[expr]
            else:
                value = int(expr, 0)
            values[name] = value
            current = value
        else:
            current += 1
            values[line] = current

    return values, values["END_OF_SOUND_PARAMETERS"]


def parse_cc2_values() -> dict[str, int]:
    text = MIDI_MESSAGES_HEADER.read_text(encoding="utf-8")
    body = text.split("//for all parameters above 127", 1)[1].split("};", 1)[0]
    values: dict[str, int] = {}
    current = -1

    for raw in body.splitlines():
        line = raw.split("//", 1)[0].strip().rstrip(",").strip()
        if not line or not line.startswith("CC2_"):
            continue
        if "=" in line:
            name, expr = [part.strip() for part in line.split("=", 1)]
            value = values[expr] if expr in values else int(expr, 0)
            values[name] = value
            current = value
        else:
            current += 1
            values[line] = current

    return values


def sanitize_display_name(raw: bytes, fallback: str) -> str:
    chars = []
    for byte in raw:
        if 0x20 <= byte <= 0x7E:
            chars.append(chr(byte))
        else:
            chars.append(" ")
    name = "".join(chars).strip()
    return name or fallback


def path_safe_name(name: str, fallback: str) -> str:
    cleaned = re.sub(r"[^A-Za-z0-9 _-]+", "", name).strip()
    # Folder names now use "NNN Kit Name"; keep spaces readable while still
    # collapsing accidental whitespace runs from legacy eight-byte names.
    cleaned = re.sub(r"\s+", " ", cleaned)
    return cleaned or fallback


def filename_prefix(name: str, fallback: str) -> str:
    cleaned = re.sub(r"[^A-Za-z0-9]+", "", name).lower()
    return (cleaned[:6] or fallback.lower())[:6]


def cc2_name_for_param(name: str, param_indexes: dict[str, int]) -> str | None:
    if param_indexes[name] < param_indexes["PAR_FILTER_DRIVE_1"]:
        return None
    return f"CC2_{name.removeprefix('PAR_')}"


def param_value(
    params: bytes,
    param_indexes: dict[str, int],
    cc2_indexes: dict[str, int],
    name: str,
) -> int:
    cc2_name = cc2_name_for_param(name, param_indexes)
    if cc2_name is not None:
        idx = CC2_PAYLOAD_BASE + cc2_indexes[cc2_name]
    else:
        idx = param_indexes[name] - 1
    return params[idx] if idx < len(params) else 0


def append_endpoint(
    lines: list[str],
    section: str,
    fields: list[str],
    param_names: list[str],
    params: bytes,
    param_indexes: dict[str, int],
    cc2_indexes: dict[str, int],
) -> None:
    lines.append(section)
    for field, param_name in zip(fields, param_names):
        lines.append(f"{field}={param_value(params, param_indexes, cc2_indexes, param_name)}")
    for pad_index in range(len(fields), INSTRUMENT_ENDPOINT_SIZE):
        lines.append(f"_pad{pad_index:02d}=0")
    lines.append("")


def write_instrument(
    path: Path,
    instrument_type: str,
    kit_name: str,
    source_name: str,
    source_file: str,
    slot: int,
    fields: list[str],
    param_names: list[str],
    params: bytes,
    param_indexes: dict[str, int],
    cc2_indexes: dict[str, int],
) -> None:
    lines = [
        "format=helicase.instrument",
        "version=1",
        f"type={instrument_type}",
        f"slot={slot}",
        f"kit_name={kit_name}",
        f"source_name={source_name}",
        f"source_file={source_file}",
        "",
    ]
    append_endpoint(lines, "[params]", fields, param_names, params, param_indexes, cc2_indexes)
    append_endpoint(lines, "[morph]", fields, param_names, params, param_indexes, cc2_indexes)
    path.write_text("\n".join(lines), encoding="ascii")


def write_kitset(
    path: Path,
    params: bytes,
    param_indexes: dict[str, int],
    cc2_indexes: dict[str, int],
    files: list[tuple[str, str]],
) -> None:
    lines = [
        "format=helicase.kitset",
        "version=1",
    ]
    lines.append("")

    for slot, (instrument_type, filename) in enumerate(files, start=1):
        lines.extend([
            f"[slot{slot}]",
            f"type={instrument_type}",
            f"file={filename}",
            f"audio_out={param_value(params, param_indexes, cc2_indexes, f'PAR_AUDIO_OUT{slot}')}",
        ])
        lines.append("")

    path.write_text("\n".join(lines), encoding="ascii")


def main() -> None:
    param_indexes, sound_param_count = parse_param_values()
    cc2_indexes = parse_cc2_values()
    kit_root = SD_ROOT / "Kit"
    kit_root.mkdir(exist_ok=True)

    for snd_path in sorted(SD_ROOT.glob("P[0-9][0-9][0-9].SND")):
        legacy_slot = int(snd_path.stem[1:])
        data = snd_path.read_bytes()
        source_name = sanitize_display_name(data[:8], f"Kit{legacy_slot:03d}")
        dir_name = f"{legacy_slot + 1:03d} {path_safe_name(source_name, f'Kit{legacy_slot:03d}')}"
        kit_dir = kit_root / dir_name
        kit_dir.mkdir(exist_ok=True)

        params = data[8:8 + sound_param_count]
        if len(params) < sound_param_count:
            params = params + bytes(sound_param_count - len(params))
        trailing = data[8 + sound_param_count:]
        prefix = filename_prefix(source_name, f"kit{legacy_slot:03d}")

        files = [
            ("drm", f"{prefix}d1.drm"),
            ("drm", f"{prefix}d2.drm"),
            ("drm", f"{prefix}d3.drm"),
            ("snr", f"{prefix}s1.snr"),
            ("cym", f"{prefix}c1.cym"),
            ("hat", f"{prefix}h1.hat"),
        ]

        drum_param_sets = [
            [name.format(n=1) for name in DRUM_PARAM_TEMPLATE],
            [name.format(n=2) for name in DRUM_PARAM_TEMPLATE],
            [name.format(n=3) for name in DRUM_PARAM_TEMPLATE],
        ]
        instrument_specs = [
            (files[0][1], "drm", 1, DRUM_FIELDS, drum_param_sets[0]),
            (files[1][1], "drm", 2, DRUM_FIELDS, drum_param_sets[1]),
            (files[2][1], "drm", 3, DRUM_FIELDS, drum_param_sets[2]),
            (files[3][1], "snr", 4, SNARE_FIELDS, SNARE_PARAMS),
            (files[4][1], "cym", 5, CYMBAL_FIELDS, CYMBAL_PARAMS),
            (files[5][1], "hat", 6, HAT_FIELDS, HAT_PARAMS),
        ]

        for filename, instrument_type, slot, fields, param_names in instrument_specs:
            write_instrument(
                kit_dir / filename,
                instrument_type,
                source_name,
                source_name,
                snd_path.name,
                slot,
                fields,
                param_names,
                params,
                param_indexes,
                cc2_indexes,
            )

        write_kitset(
            kit_dir / "kitset.kcg",
            params,
            param_indexes,
            cc2_indexes,
            files,
        )


if __name__ == "__main__":
    main()
