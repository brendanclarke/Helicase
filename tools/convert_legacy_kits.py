#!/usr/bin/env python3
"""Convert legacy flat Pxxx.SND kits into Phase 2 Kit/ directories.

The legacy file is an eight-byte display name followed by bytes indexed by the
current ParameterArray ``PAR_*`` enum values. That direct indexing was verified
against the original-hardware "Slak" kit: ``PAR_COARSE1`` is payload[8],
``PAR_FILTER_DRIVE_1`` is payload[128], and ``PAR_AUDIO_OUT1`` is payload[215].

The generated text schema mirrors Core/Hardware/SD/storageTypes.c: kitset.kcg
owns the six instrument files and audio routing, while each instrument file
owns only the parameters for its voice. Morph sections are initialized from the
same legacy bytes as params so old kits sound the same before a dedicated morph
save/edit pass exists.
"""

from __future__ import annotations

import re
import shutil
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SD_ROOT = ROOT / "SD_CARD"
PARAMETER_ARRAY_H = ROOT / "Core" / "Scene" / "Preset" / "ParameterArray.h"
PARAM_RENAME_TXT = ROOT / "param_rename.txt"

# Voice slots and extensions are fixed by storageTypes.c. The short filename
# prefix is six characters so prefix+d1/s1/c1/h1 still fits FAT 8.3 names.
INSTRUMENT_FILES = [
    ("drm", "d1", 1),
    ("drm", "d2", 2),
    ("drm", "d3", 3),
    ("snr", "s1", 4),
    ("cym", "c1", 5),
    ("hat", "h1", 6),
]

INSTRUMENT_RENAME_SECTIONS = {
    1: "DRUM",
    2: "DRUM",
    3: "DRUM",
    4: "SNARE",
    5: "CYMBAL",
    6: "HIHAT",
}

# This is the Python-side copy of storageTypes.c's key-to-ParameterArray maps.
# Keeping the PAR_* names instead of numeric offsets lets the converter check
# the live enum values before reading bytes from legacy .SND files.
INSTRUMENT_PARAMS = {
    1: [
        ("osc_wave", "PAR_OSC_WAVE_DRUM1"),
        ("coarse", "PAR_COARSE1"),
        ("fine", "PAR_FINE1"),
        ("mod_wave", "PAR_MOD_WAVE_DRUM1"),
        ("filter_freq", "PAR_FILTER_FREQ_1"),
        ("reso", "PAR_RESO_1"),
        ("velo_attack", "PAR_VELOA1"),
        ("velo_decay", "PAR_VELOD1"),
        ("vol_slope", "PAR_VOL_SLOPE1"),
        ("pitch_decay", "PAR_MOD_EG1"),
        ("mod_amount", "PAR_MODAMNT1"),
        ("pitch_slope", "PAR_PITCH_SLOPE1"),
        ("fm_amount", "PAR_FMAMNT1"),
        ("fm_freq", "PAR_FM_FREQ1"),
        ("volume", "PAR_VOL1"),
        ("pan", "PAR_PAN1"),
        ("drive", "PAR_DRIVE1"),
        ("voice_decimation", "PAR_VOICE_DECIMATION1"),
        ("freq_lfo", "PAR_FREQ_LFO1"),
        ("amount_lfo", "PAR_AMOUNT_LFO1"),
        ("filter_drive", "PAR_FILTER_DRIVE_1"),
        ("mix_mod", "PAR_MIX_MOD_1"),
        ("volume_mod_on_off", "PAR_VOLUME_MOD_ON_OFF1"),
        ("velo_mod_amt", "PAR_VELO_MOD_AMT_1"),
        ("vel_dest", "PAR_VEL_DEST_1"),
        ("wave_lfo", "PAR_WAVE_LFO1"),
        ("voice_lfo", "PAR_VOICE_LFO1"),
        ("target_lfo", "PAR_TARGET_LFO1"),
        ("retrigger_lfo", "PAR_RETRIGGER_LFO1"),
        ("sync_lfo", "PAR_SYNC_LFO1"),
        ("offset_lfo", "PAR_OFFSET_LFO1"),
        ("filter_type", "PAR_FILTER_TYPE_1"),
        ("transient_vol", "PAR_TRANS1_VOL"),
        ("transient_wave", "PAR_TRANS1_WAVE"),
        ("transient_freq", "PAR_TRANS1_FREQ"),
    ],
    2: [
        ("osc_wave", "PAR_OSC_WAVE_DRUM2"),
        ("coarse", "PAR_COARSE2"),
        ("fine", "PAR_FINE2"),
        ("mod_wave", "PAR_MOD_WAVE_DRUM2"),
        ("filter_freq", "PAR_FILTER_FREQ_2"),
        ("reso", "PAR_RESO_2"),
        ("velo_attack", "PAR_VELOA2"),
        ("velo_decay", "PAR_VELOD2"),
        ("vol_slope", "PAR_VOL_SLOPE2"),
        ("pitch_decay", "PAR_MOD_EG2"),
        ("mod_amount", "PAR_MODAMNT2"),
        ("pitch_slope", "PAR_PITCH_SLOPE2"),
        ("fm_amount", "PAR_FMAMNT2"),
        ("fm_freq", "PAR_FM_FREQ2"),
        ("volume", "PAR_VOL2"),
        ("pan", "PAR_PAN2"),
        ("drive", "PAR_DRIVE2"),
        ("voice_decimation", "PAR_VOICE_DECIMATION2"),
        ("freq_lfo", "PAR_FREQ_LFO2"),
        ("amount_lfo", "PAR_AMOUNT_LFO2"),
        ("filter_drive", "PAR_FILTER_DRIVE_2"),
        ("mix_mod", "PAR_MIX_MOD_2"),
        ("volume_mod_on_off", "PAR_VOLUME_MOD_ON_OFF2"),
        ("velo_mod_amt", "PAR_VELO_MOD_AMT_2"),
        ("vel_dest", "PAR_VEL_DEST_2"),
        ("wave_lfo", "PAR_WAVE_LFO2"),
        ("voice_lfo", "PAR_VOICE_LFO2"),
        ("target_lfo", "PAR_TARGET_LFO2"),
        ("retrigger_lfo", "PAR_RETRIGGER_LFO2"),
        ("sync_lfo", "PAR_SYNC_LFO2"),
        ("offset_lfo", "PAR_OFFSET_LFO2"),
        ("filter_type", "PAR_FILTER_TYPE_2"),
        ("transient_vol", "PAR_TRANS2_VOL"),
        ("transient_wave", "PAR_TRANS2_WAVE"),
        ("transient_freq", "PAR_TRANS2_FREQ"),
    ],
    3: [
        ("osc_wave", "PAR_OSC_WAVE_DRUM3"),
        ("coarse", "PAR_COARSE3"),
        ("fine", "PAR_FINE3"),
        ("mod_wave", "PAR_MOD_WAVE_DRUM3"),
        ("filter_freq", "PAR_FILTER_FREQ_3"),
        ("reso", "PAR_RESO_3"),
        ("velo_attack", "PAR_VELOA3"),
        ("velo_decay", "PAR_VELOD3"),
        ("vol_slope", "PAR_VOL_SLOPE3"),
        ("pitch_decay", "PAR_MOD_EG3"),
        ("mod_amount", "PAR_MODAMNT3"),
        ("pitch_slope", "PAR_PITCH_SLOPE3"),
        ("fm_amount", "PAR_FMAMNT3"),
        ("fm_freq", "PAR_FM_FREQ3"),
        ("volume", "PAR_VOL3"),
        ("pan", "PAR_PAN3"),
        ("drive", "PAR_DRIVE3"),
        ("voice_decimation", "PAR_VOICE_DECIMATION3"),
        ("freq_lfo", "PAR_FREQ_LFO3"),
        ("amount_lfo", "PAR_AMOUNT_LFO3"),
        ("filter_drive", "PAR_FILTER_DRIVE_3"),
        ("mix_mod", "PAR_MIX_MOD_3"),
        ("volume_mod_on_off", "PAR_VOLUME_MOD_ON_OFF3"),
        ("velo_mod_amt", "PAR_VELO_MOD_AMT_3"),
        ("vel_dest", "PAR_VEL_DEST_3"),
        ("wave_lfo", "PAR_WAVE_LFO3"),
        ("voice_lfo", "PAR_VOICE_LFO3"),
        ("target_lfo", "PAR_TARGET_LFO3"),
        ("retrigger_lfo", "PAR_RETRIGGER_LFO3"),
        ("sync_lfo", "PAR_SYNC_LFO3"),
        ("offset_lfo", "PAR_OFFSET_LFO3"),
        ("filter_type", "PAR_FILTER_TYPE_3"),
        ("transient_vol", "PAR_TRANS3_VOL"),
        ("transient_wave", "PAR_TRANS3_WAVE"),
        ("transient_freq", "PAR_TRANS3_FREQ"),
    ],
    4: [
        ("osc_wave", "PAR_OSC_WAVE_SNARE"),
        ("coarse", "PAR_COARSE4"),
        ("fine", "PAR_FINE4"),
        ("noise_freq", "PAR_NOISE_FREQ1"),
        ("mix", "PAR_MIX1"),
        ("filter_freq", "PAR_FILTER_FREQ_4"),
        ("reso", "PAR_RESO_4"),
        ("velo_attack", "PAR_VELOA4"),
        ("velo_decay", "PAR_VELOD4"),
        ("vol_slope", "PAR_VOL_SLOPE4"),
        ("repeat", "PAR_REPEAT4"),
        ("pitch_decay", "PAR_MOD_EG4"),
        ("mod_amount", "PAR_MODAMNT4"),
        ("pitch_slope", "PAR_PITCH_SLOPE4"),
        ("volume", "PAR_VOL4"),
        ("pan", "PAR_PAN4"),
        ("drive", "PAR_SNARE_DISTORTION"),
        ("voice_decimation", "PAR_VOICE_DECIMATION4"),
        ("freq_lfo", "PAR_FREQ_LFO4"),
        ("amount_lfo", "PAR_AMOUNT_LFO4"),
        ("filter_drive", "PAR_FILTER_DRIVE_4"),
        ("volume_mod_on_off", "PAR_VOLUME_MOD_ON_OFF4"),
        ("velo_mod_amt", "PAR_VELO_MOD_AMT_4"),
        ("vel_dest", "PAR_VEL_DEST_4"),
        ("wave_lfo", "PAR_WAVE_LFO4"),
        ("voice_lfo", "PAR_VOICE_LFO4"),
        ("target_lfo", "PAR_TARGET_LFO4"),
        ("retrigger_lfo", "PAR_RETRIGGER_LFO4"),
        ("sync_lfo", "PAR_SYNC_LFO4"),
        ("offset_lfo", "PAR_OFFSET_LFO4"),
        ("filter_type", "PAR_FILTER_TYPE_4"),
        ("transient_vol", "PAR_TRANS4_VOL"),
        ("transient_wave", "PAR_TRANS4_WAVE"),
        ("transient_freq", "PAR_TRANS4_FREQ"),
    ],
    5: [
        ("wave1", "PAR_WAVE1_CYM"),
        ("coarse", "PAR_COARSE5"),
        ("fine", "PAR_FINE5"),
        ("mod_osc1_freq", "PAR_MOD_OSC_F1_CYM"),
        ("mod_osc2_freq", "PAR_MOD_OSC_F2_CYM"),
        ("mod_osc1_gain", "PAR_MOD_OSC_GAIN1_CYM"),
        ("mod_osc2_gain", "PAR_MOD_OSC_GAIN2_CYM"),
        ("wave2", "PAR_WAVE2_CYM"),
        ("wave3", "PAR_WAVE3_CYM"),
        ("filter_freq", "PAR_FILTER_FREQ_5"),
        ("reso", "PAR_RESO_5"),
        ("velo_attack", "PAR_VELOA5"),
        ("velo_decay", "PAR_VELOD5"),
        ("vol_slope", "PAR_VOL_SLOPE5"),
        ("repeat", "PAR_REPEAT5"),
        ("volume", "PAR_VOL5"),
        ("pan", "PAR_PAN5"),
        ("drive", "PAR_CYMBAL_DISTORTION"),
        ("voice_decimation", "PAR_VOICE_DECIMATION5"),
        ("freq_lfo", "PAR_FREQ_LFO5"),
        ("amount_lfo", "PAR_AMOUNT_LFO5"),
        ("filter_drive", "PAR_FILTER_DRIVE_5"),
        ("volume_mod_on_off", "PAR_VOLUME_MOD_ON_OFF5"),
        ("velo_mod_amt", "PAR_VELO_MOD_AMT_5"),
        ("vel_dest", "PAR_VEL_DEST_5"),
        ("wave_lfo", "PAR_WAVE_LFO5"),
        ("voice_lfo", "PAR_VOICE_LFO5"),
        ("target_lfo", "PAR_TARGET_LFO5"),
        ("retrigger_lfo", "PAR_RETRIGGER_LFO5"),
        ("sync_lfo", "PAR_SYNC_LFO5"),
        ("offset_lfo", "PAR_OFFSET_LFO5"),
        ("filter_type", "PAR_FILTER_TYPE_5"),
        ("transient_vol", "PAR_TRANS5_VOL"),
        ("transient_wave", "PAR_TRANS5_WAVE"),
        ("transient_freq", "PAR_TRANS5_FREQ"),
    ],
    6: [
        ("wave1", "PAR_WAVE1_HH"),
        ("coarse", "PAR_COARSE6"),
        ("fine", "PAR_FINE6"),
        ("mod_osc1_freq", "PAR_MOD_OSC_F1"),
        ("mod_osc2_freq", "PAR_MOD_OSC_F2"),
        ("mod_osc1_gain", "PAR_MOD_OSC_GAIN1"),
        ("mod_osc2_gain", "PAR_MOD_OSC_GAIN2"),
        ("wave2", "PAR_WAVE2_HH"),
        ("wave3", "PAR_WAVE3_HH"),
        ("filter_freq", "PAR_FILTER_FREQ_6"),
        ("reso", "PAR_RESO_6"),
        ("velo_attack", "PAR_VELOA6"),
        ("decay_closed", "PAR_VELOD6_CLOSED"),
        ("decay_open", "PAR_VELOD6_OPEN"),
        ("vol_slope", "PAR_VOL_SLOPE6"),
        ("volume", "PAR_VOL6"),
        ("pan", "PAR_PAN6"),
        ("drive", "PAR_HAT_DISTORTION"),
        ("voice_decimation", "PAR_VOICE_DECIMATION6"),
        ("freq_lfo", "PAR_FREQ_LFO6"),
        ("amount_lfo", "PAR_AMOUNT_LFO6"),
        ("filter_drive", "PAR_FILTER_DRIVE_6"),
        ("volume_mod_on_off", "PAR_VOLUME_MOD_ON_OFF6"),
        ("velo_mod_amt", "PAR_VELO_MOD_AMT_6"),
        ("vel_dest", "PAR_VEL_DEST_6"),
        ("wave_lfo", "PAR_WAVE_LFO6"),
        ("voice_lfo", "PAR_VOICE_LFO6"),
        ("target_lfo", "PAR_TARGET_LFO6"),
        ("retrigger_lfo", "PAR_RETRIGGER_LFO6"),
        ("sync_lfo", "PAR_SYNC_LFO6"),
        ("offset_lfo", "PAR_OFFSET_LFO6"),
        ("filter_type", "PAR_FILTER_TYPE_6"),
        ("transient_vol", "PAR_TRANS6_VOL"),
        ("transient_wave", "PAR_TRANS6_WAVE"),
        ("transient_freq", "PAR_TRANS6_FREQ"),
    ],
}


def strip_c_comments(text: str) -> str:
    """Remove comments so stale enum notes cannot affect the conversion."""
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//.*", "", text)


def parse_param_enum() -> dict[str, int]:
    """Return ParameterArray enum values keyed by PAR_* symbol."""
    text = strip_c_comments(PARAMETER_ARRAY_H.read_text(encoding="ascii"))
    match = re.search(r"enum\s+ParamEnums\s*\{(.*?)\}\s*;", text, flags=re.S)
    if not match:
        raise RuntimeError(f"could not find ParamEnums in {PARAMETER_ARRAY_H}")

    values: dict[str, int] = {}
    current = -1
    for raw_entry in match.group(1).split(","):
        entry = raw_entry.strip()
        if not entry:
            continue
        if "=" in entry:
            name, expr = [part.strip() for part in entry.split("=", 1)]
            if expr in values:
                current = values[expr]
            else:
                current = int(expr, 0)
        else:
            name = entry
            current += 1
        values[name] = current

    return values


def parse_param_renames() -> dict[str, dict[str, str]]:
    """Return per-instrument legacy-key to file-key renames."""
    sections: dict[str, dict[str, str]] = {}
    current: str | None = None

    for line_nr, raw_line in enumerate(PARAM_RENAME_TXT.read_text(encoding="ascii").splitlines(), start=1):
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        if "=" not in line:
            current = line
            if current in sections:
                raise RuntimeError(f"duplicate rename section {current!r} at {PARAM_RENAME_TXT}:{line_nr}")
            sections[current] = {}
            continue
        if current is None:
            raise RuntimeError(f"rename outside a section at {PARAM_RENAME_TXT}:{line_nr}")
        if line.count("=") != 1:
            raise RuntimeError(f"bad rename line at {PARAM_RENAME_TXT}:{line_nr}: {line!r}")
        old_key, new_key = [part.strip() for part in line.split("=", 1)]
        if not old_key or not new_key:
            raise RuntimeError(f"empty rename key at {PARAM_RENAME_TXT}:{line_nr}")
        if old_key in sections[current]:
            raise RuntimeError(f"duplicate rename key {old_key!r} in {current} at {PARAM_RENAME_TXT}:{line_nr}")
        sections[current][old_key] = new_key

    return sections


def validate_param_renames(param_renames: dict[str, dict[str, str]]) -> None:
    """Prove every instrument key has one unambiguous output name."""
    expected_sections = set(INSTRUMENT_RENAME_SECTIONS.values())
    unexpected_sections = sorted(set(param_renames) - expected_sections)
    if unexpected_sections:
        raise RuntimeError(
            f"unexpected rename sections in {PARAM_RENAME_TXT}: "
            f"{', '.join(unexpected_sections)}"
        )

    for section in sorted(expected_sections):
        renames = param_renames.get(section)
        if renames is None:
            raise RuntimeError(f"missing {section} section in {PARAM_RENAME_TXT}")

        slots = [
            slot
            for slot, rename_section in INSTRUMENT_RENAME_SECTIONS.items()
            if rename_section == section
        ]
        expected_keys = {
            key
            for slot in slots
            for key, _ in INSTRUMENT_PARAMS[slot]
        }
        missing = sorted(expected_keys - set(renames))
        unexpected = sorted(set(renames) - expected_keys)
        if missing:
            raise RuntimeError(
                f"{section} rename section is missing keys: {', '.join(missing)}"
            )
        if unexpected:
            raise RuntimeError(
                f"{section} rename section has unknown keys: "
                f"{', '.join(unexpected)}"
            )

        destination_keys = list(renames.values())
        duplicates = sorted({
            key for key in destination_keys if destination_keys.count(key) > 1
        })
        if duplicates:
            raise RuntimeError(
                f"{section} rename section has duplicate output keys: "
                f"{', '.join(duplicates)}"
            )


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
    cleaned = re.sub(r"\s+", " ", cleaned)
    return cleaned or fallback


def filename_prefix(name: str, fallback: str) -> str:
    cleaned = re.sub(r"[^A-Za-z0-9]+", "", name).lower()
    return (cleaned[:6] or fallback.lower())[:6]


def payload_value(payload: bytes, param_values: dict[str, int], param_name: str) -> int:
    """Read the legacy byte for one PAR_* value, failing on missing storage."""
    index = param_values[param_name]
    if index < 0 or index >= len(payload):
        raise ValueError(
            f"{param_name} maps to payload[{index}], but legacy file has "
            f"only {len(payload)} payload bytes"
        )
    return payload[index]


def instrument_values(
    payload: bytes,
    param_values: dict[str, int],
    param_renames: dict[str, dict[str, str]],
    slot: int,
) -> list[tuple[str, int]]:
    values = []
    section = INSTRUMENT_RENAME_SECTIONS[slot]
    renames = param_renames.get(section)
    if renames is None:
        raise RuntimeError(f"missing {section} section in {PARAM_RENAME_TXT}")

    expected_keys = {key for key, _ in INSTRUMENT_PARAMS[slot]}
    missing = sorted(expected_keys - set(renames))
    if missing:
        raise RuntimeError(f"{section} rename section is missing keys: {', '.join(missing)}")

    for key, param_name in INSTRUMENT_PARAMS[slot]:
        values.append((renames[key], payload_value(payload, param_values, param_name)))
    return values


def write_instrument(
    path: Path,
    instrument_type: str,
    values: list[tuple[str, int]],
) -> None:
    lines = [
        "format=helicase.instrument",
        "version=1",
        f"type={instrument_type}",
        "",
        "[params]",
    ]
    lines.extend(f"{key}={value}" for key, value in values)
    lines.extend(["", "[morph]"])
    lines.extend(f"{key}={value}" for key, value in values)
    lines.append("")
    path.write_text("\n".join(lines), encoding="ascii")


def write_kitset(
    path: Path,
    files: list[tuple[str, str, int]],
    payload: bytes,
    param_values: dict[str, int],
) -> None:
    lines = [
        "format=helicase.kitset",
        "version=1",
        "",
    ]

    for slot, (instrument_type, filename, _) in enumerate(files, start=1):
        audio_param = f"PAR_AUDIO_OUT{slot}"
        audio_out = payload_value(payload, param_values, audio_param)
        lines.extend([
            f"[slot{slot}]",
            f"type={instrument_type}",
            f"file={filename}",
            f"audio_out={audio_out}",
            "",
        ])

    path.write_text("\n".join(lines), encoding="ascii")


def convert_one(
    snd_path: Path,
    kit_root: Path,
    param_values: dict[str, int],
    param_renames: dict[str, dict[str, str]],
) -> None:
    legacy_slot = int(snd_path.stem[1:])
    data = snd_path.read_bytes()
    if len(data) < 8:
        raise ValueError(f"{snd_path} is too short for an eight-byte kit name")

    payload = data[8:]
    kit_display_name = sanitize_display_name(data[:8], f"Kit{legacy_slot:03d}")
    dir_name = f"{legacy_slot + 1:03d} {path_safe_name(kit_display_name, f'Kit{legacy_slot:03d}')}"
    kit_dir = kit_root / dir_name
    kit_dir.mkdir(parents=True, exist_ok=True)

    prefix = filename_prefix(kit_display_name, f"kit{legacy_slot:03d}")
    files = [
        (instrument_type, f"{prefix}{suffix}.{instrument_type}", slot)
        for instrument_type, suffix, slot in INSTRUMENT_FILES
    ]

    for instrument_type, filename, slot in files:
        write_instrument(
            kit_dir / filename,
            instrument_type,
            instrument_values(payload, param_values, param_renames, slot),
        )

    write_kitset(kit_dir / "kitset.kcg", files, payload, param_values)


def main() -> None:
    param_values = parse_param_enum()
    param_renames = parse_param_renames()
    validate_param_renames(param_renames)
    kit_root = SD_ROOT / "Kit"

    # The generated directory is a mirror of the legacy Pxxx.SND files, so stale
    # kit folders or macOS metadata must be removed before writing fresh output.
    if kit_root.exists():
        shutil.rmtree(kit_root)
    kit_root.mkdir()

    snd_files = sorted(SD_ROOT.glob("P[0-9][0-9][0-9].SND"))
    if not snd_files:
        raise RuntimeError(f"no legacy Pxxx.SND files found in {SD_ROOT}")

    for snd_path in snd_files:
        convert_one(snd_path, kit_root, param_values, param_renames)

    print(f"converted {len(snd_files)} legacy kits into {kit_root}")


if __name__ == "__main__":
    main()
