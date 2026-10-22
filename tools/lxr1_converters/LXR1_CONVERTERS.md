# LXR1 Converters — Readme, Manual and Reference

Host-side Python tools that turn Sonic Potions LXR (LXR1) SD-card files into
Helicase library content:

| LXR1 file | Script | Helicase result |
|---|---|---|
| `pNNN.snd` (kit) | `snd_to_kit.py` | `Kit/NNN <name>/` (kitset + 6 Instruments) |
| `pNNN.pat` (8 patterns) | `pat_to_pattern.py` | `Pattern/NNN <name> <n>.pat` (one PAT4 per pattern) |
| `pNNN.all` / `pNNN.prf` (kit + 8 patterns) | `all_to_bank.py` | `Bank/NNN <name>/` (8 Scenes, one shared Kit, tied edit mask) |

`lxr1_common.py` holds everything the three scripts share: the LXR1 file
readers, the firmware descriptor loader, and the Helicase writers.

Contents:

1. [Quick start](#1-quick-start)
2. [Requirements](#2-requirements)
3. [Common behaviour](#3-common-behaviour)
4. [snd_to_kit.py](#4-snd_to_kitpy--kits)
5. [pat_to_pattern.py](#5-pat_to_patternpy--patterns)
6. [all_to_bank.py](#6-all_to_bankpy--banks)
7. [Putting the result on a card](#7-putting-the-result-on-a-card)
8. [What is and is not converted](#8-what-is-and-is-not-converted)
9. [Reference: LXR1 file formats](#9-reference-lxr1-file-formats)
10. [Reference: parameter and value mapping](#10-reference-parameter-and-value-mapping)
11. [Reference: pattern conversion rules](#11-reference-pattern-conversion-rules)
12. [Reference: Helicase output](#12-reference-helicase-output)
13. [Troubleshooting](#13-troubleshooting)
14. [Maintenance](#14-maintenance)

---

## 1. Quick start

From the repository root:

```sh
# Kits from an unzipped LXR1 card (keep p000 -> 000, p001 -> 001, ...)
python3 tools/lxr1_converters/snd_to_kit.py --keep-numbers /path/to/lxr1_card

# Patterns (each .pat gives up to 8 library Patterns in free slots)
python3 tools/lxr1_converters/pat_to_pattern.py /path/to/lxr1_card

# .all and .prf performances as Banks
python3 tools/lxr1_converters/all_to_bank.py /path/to/lxr1_card
```

By default the output goes into this repository's `SD_CARD/` tree
(`SD_CARD/Kit`, `SD_CARD/Pattern`, `SD_CARD/Bank`). Use `--out` to write
somewhere else, for example straight into a mounted card:

```sh
python3 tools/lxr1_converters/snd_to_kit.py --out /Volumes/HELICASE/Kit /path/to/lxr1_card
```

Add `--dry-run` to any command to see what would be written, with every
conversion note, without writing anything.

The reference factory content is
<http://sonic-potions.com/public/SdCardImage.zip> (30 kits, `P000.SND` …
`P029.SND`); it contains no `.pat`, `.all` or `.prf` files.

## 2. Requirements

- Python 3.9 or newer, standard library only (no packages to install).
- A Helicase source tree. The scripts read the instrument descriptor tables
  in `Core/DSP/Instruments/*/*Parameters.c` every time they run, so key
  order, target tokens and automation IDs always match that firmware. By
  default this is the repository the scripts live in; another tree can be
  named with `--firmware-root`.
- Run the scripts from any directory; they find `lxr1_common.py` next to
  themselves.

## 3. Common behaviour

### Inputs

Every script takes one or more `INPUT` arguments. Each is either a file or a
directory. A directory is scanned (not recursively) for the script's
extensions, case-insensitively (`P000.SND` and `p000.snd` both match).
macOS `._*` files are ignored.

### Library slots

Helicase library folders and files start with a three-digit slot,
`000`..`999` (`Kit/007 Rollin`, `Pattern/012 Synth 3.pat`). Existing entries
are never touched unless you ask:

| Option | Effect |
|---|---|
| (default) | Each output takes the lowest free slot at or after `--start-slot` (default 0). Occupied slots are skipped. |
| `--start-slot N` | Start the free-slot search at `N`. |
| `--keep-numbers` | Kits and Banks only: use the LXR1 file number (`p007.snd` → `007`). An occupied slot is refused. |
| `--overwrite` | With `--keep-numbers`: delete what occupies the slot, then write. |

`pat_to_pattern.py` always uses free slots, because one `.pat` produces up to
eight Patterns.

### Names

LXR1 names are 8 characters. Characters other than letters, digits, space,
`-` and `_` become spaces, runs of spaces collapse, and the result is
trimmed to the Helicase 8-character limit. `Moch.to.` → `Moch to`,
`Rollin'` → `Rollin`, `808cb!` → `808cb`. Names that need a number suffix
are shortened to fit: `AllTest` pattern 1 → `AllTes 1`.

Instrument files are named `<6-char prefix><d1|d2|d3|s1|c1|h1>.<ext>`
(`slakd1.drm`), the same scheme as the existing `SD_CARD/Kit` content.

### Shared options

| Option | Scripts | Meaning |
|---|---|---|
| `--out DIR` | all | Library directory to write into. |
| `--start-slot N` | all | First slot to consider (0..999). |
| `--dry-run` | all | Convert and report, write nothing. |
| `--firmware-root DIR` | all | Helicase source tree for the descriptor tables. |
| `--keep-numbers`, `--overwrite` | kit, bank | See above. |
| `--target-encoding auto\|pages\|modtargets` | kit, bank | How kit LFO/velocity target bytes are read (section 9.2). |
| `--partial-rolls first\|fill` | pattern, bank | How irregular sub-step rhythms are written (section 11.3). |
| `--keep-empty` | pattern | Also write LXR1 patterns that have no steps. |

### Output, notes and errors

Each converted file prints one line (`P000.SND 'Slak' (pages targets) ->
.../Kit/000 Slak`). Anything that could not be carried exactly is printed
underneath as an indented note, for example:

```
P026.SND 'Flux' (pages targets) -> SD_CARD/Kit/026 Flux
p005.pat 'Synth' pattern 1 -> SD_CARD/Pattern/000 Synth 1.pat
  p005.pat: pattern 1: 1 step(s) with irregular sub-step rhythms kept only their first hit
```

A file that cannot be read (truncated, unsupported version, unsupported
layout) prints `ERROR <file>: <reason>` and is skipped; the rest of the batch
continues. Each file is fully converted (and every PAT4 image verified) before
anything is written for it, so a failing file leaves nothing half-written.
The exit status is 0 when every file converted and 1 when any failed.

## 4. `snd_to_kit.py` — kits

```
python3 tools/lxr1_converters/snd_to_kit.py [--out DIR] [--keep-numbers]
        [--overwrite] [--start-slot N] [--target-encoding MODE]
        [--firmware-root DIR] [--dry-run] INPUT [INPUT ...]
```

Writes one `Kit/NNN <name>/` folder per `.snd`:

```
Kit/000 Slak/
  kitset.kcg
  slakd1.drm  slakd2.drm  slakd3.drm  slaks1.snr  slakc1.cym  slakh1.hat
```

- Every instrument parameter is copied (section 10).
- LFO and velocity targets are translated to Helicase tokens (section 10.3).
- `[morph]` holds the same values as `[params]`, so the Kit sounds the same at
  any Morph amount.
- The voice audio-out routes and the track MIDI notes in the `.snd` are
  Scene settings in Helicase and are not part of a Kit; they are dropped
  here (use `all_to_bank.py` for content that should keep them).

Example: the 30 factory kits into `SD_CARD/Kit` with their LXR1 numbers:

```sh
python3 tools/lxr1_converters/snd_to_kit.py --keep-numbers ~/Downloads/SdCardImage
```

## 5. `pat_to_pattern.py` — patterns

```
python3 tools/lxr1_converters/pat_to_pattern.py [--out DIR] [--start-slot N]
        [--keep-empty] [--partial-rolls first|fill]
        [--firmware-root DIR] [--dry-run] INPUT [INPUT ...]
```

An LXR1 `.pat` holds eight patterns; a Helicase library Pattern holds one.
Each LXR1 pattern that has at least one playing step becomes its own file:

```
Pattern/000 Synth 1.pat     (LXR1 pattern 1)
Pattern/001 Synth 3.pat     (LXR1 pattern 3; 2 was empty)
```

The number after the name is the LXR1 pattern number (1..8). With
`--keep-empty` all eight are written. The LXR1 "next pattern / repeat"
settings have no meaning for a single library Pattern and are not converted
here (they become Scene chains in `all_to_bank.py`). Conversion rules:
section 11.

## 6. `all_to_bank.py` — banks

```
python3 tools/lxr1_converters/all_to_bank.py [--out DIR] [--keep-numbers]
        [--overwrite] [--start-slot N] [--target-encoding MODE]
        [--partial-rolls first|fill] [--firmware-root DIR] [--dry-run]
        INPUT [INPUT ...]
```

Each `.all` or `.prf` becomes one Bank with eight Scenes, one per LXR1
pattern (all eight are written, even empty ones, so chain targets keep their
numbers):

```
Bank/001 AllTest/
  bankset.bcg
  00 AllTes 1/
    sceneset.scg
    AllTes 1.pat
    Kit AllTest/      (kitset.kcg + 6 Instruments)
  01 AllTes 2/
  ...
  07 AllTes 8/
```

- **Same Kit in every Scene.** The LXR1 performance has one kit; each Scene
  embeds an identical copy.
- **Voice edit masks tied together.** `bankset.bcg` puts Scenes 00..07 in
  one VOICE edit mask (`scene_mask_voice_edit_0..7=00ff`), so an Instrument,
  Kit or Effect edit in one Scene reaches all eight, as editing the single
  LXR1 kit did. Slots 08..15 keep their self-only defaults.
- **Scene settings from the LXR1 file** go into every `sceneset.scg`:
  - `audio_out`: the kit's six voice routes;
  - `midi_note`: the kit's seven track MIDI notes (LXR1 fixed note → Helicase
    track note root; 0 → `on`);
  - `midi_channel`: the seven track channels from the `.all` globals block
    (`.prf` files carry none, so tracks stay `glb`).
- **Pattern chaining.** LXR1 "go to pattern P every N bars" becomes the
  Scene chain `cn1`/`nx1` (section 11.6).
- **Active Scene** is 00. No Effect file is written; Scenes load with the
  Effect `off`.
- BPM, bar reset (`pcr`) and the other LXR1 globals are global settings in
  Helicase too and are not converted.

## 7. Putting the result on a card

1. Convert into the card's root folders (`--out /Volumes/<card>/Kit`, etc.),
   or into `SD_CARD/` and copy the new folders/files across.
2. Boot the Helicase. Boot rescans the Kit, Scene, Bank and Pattern
   libraries, canonicalises names and rewrites each `.hcindex`
   (`main.c` boot sequence, `filesystem_createLibraryIndexBlocking()`), so
   the new slots appear in the Load menus.
3. Load a Kit with Load:Kit, a Pattern with Load:Pattern, a Bank with
   Load:Bank.

The converters never write `.hcindex`, `.hcnames`, `settings.cfg` or AutoSave
files.

## 8. What is and is not converted

| LXR1 data | Result |
|---|---|
| All sound parameters of the six voices | Copied 1:1 into the Instrument files. |
| Open hi-hat decay (`PAR_VELOD6_OPEN`) | HiHat `amp_envelope_decay_choke` and the Kit's `slot6_track7_amp_envelope_decay`. |
| LFO target (voice + parameter) | Helicase `lfo_target_voice` / `lfo_target_param` (any voice). |
| Velocity target | Helicase `velo_mod_dest` if it is on the same voice, else `off` (Helicase velocity targets are voice-local). |
| Targets `srt` (all-voice sample rate), MIDI note, target-selector cells | `off` (no Helicase Instrument parameter); a note is printed. |
| Voice audio out | Scene `audio_out` (banks only). |
| Track MIDI note | Scene `midi_note` (banks only). |
| Track MIDI channel (`.all`) | Scene `midi_channel` (banks only). |
| Steps, velocity, note, probability | Pattern step trigger and specials (section 11). |
| Sub-steps | Micro-timing and rolls (section 11.3). |
| Step automation (2 per step) | Pattern step automation, where Helicase can automate the target. |
| Track length, track rotation | Track length; rotation baked into the step order. |
| Shuffle | Every track's shuffle. |
| Next pattern / change bar | Scene chain (banks only). |
| BPM, bar reset, MIDI routing/filters, clock, quantisation, screensaver | Not converted (global settings). |
| Morph kits (`.snd` loaded as morph) | Not distinguished; a `.snd` is always the Normal endpoint. |

New Helicase parameters with no LXR1 equivalent get the value that keeps the
LXR1 sound: `lfo_polarity` = 0 (`neg`, the original LXR LFO law),
`lfo_amount_2` = 0, `lfo_target_voice_2` = `self`, `lfo_target_param_2` =
`off`; Scene FX sends, Morph amounts and bus compressor are 0/`off`.

## 9. Reference: LXR1 file formats

Source: <https://github.com/SonicPotions/LXR>, `front/LxrAvr/Preset/
presetManager.c` (writers) and `mainboard/LxrStm32/src/Sequencer/
sequencer.c` (pattern layout). All multi-byte values are little-endian.

### 9.1 Kit parameter block

One byte per `enum ParamEnums` value (`front/LxrAvr/Parameters.h`) from 0 up
to `END_OF_SOUND_PARAMETERS`. Selected offsets: `PAR_COARSE1` 8,
`PAR_FILTER_DRIVE_1` 128, `PAR_VEL_DEST_1` 149, `PAR_VOICE_LFO1` 161,
`PAR_TARGET_LFO1` 167, `PAR_AUDIO_OUT1` 215, `PAR_MIDI_NOTE1` 221.
Short data is zero-filled on load (as the LXR1 does).

Three firmware generations exist:

| Generation | Sound bytes | Layout | Kit targets | Converter |
|---|---|---|---|---|
| V0.1x (early 2013) | 225 | different enum | — | refused with an error |
| V0.2x (LXR commit 74f5b23, July 2013; the factory kits) | 221 | final offsets | menu-page references | `pages` |
| Nov 2013 onward | 228 | final offsets + 7 MIDI notes | `modTargets[]` index | `modtargets` |

### 9.2 Kit target encodings (`--target-encoding`)

- **modtargets** — the byte indexes `modTargets[]` in
  `front/LxrAvr/Menu/Cc2Text.c` (205 entries; 0 = off, 1 = all-voice sample
  rate, 2..204 per-voice parameters). The entry also names the target voice.
- **pages** — the byte is `(sub-page << 3) | entry`, a cell of the voice's
  menu page table (`front/AVR/Menu/menuPages.h` at commit 74f5b23). Velocity
  targets use the owning voice's pages; LFO targets use the pages of
  `PAR_VOICE_LFOn`. Example: LFO voice 5, target 40 = sub-page 5 (Filter),
  entry 0 = cymbal filter frequency.
- **auto** (default) — `.snd` with fewer than 228 sound bytes → `pages`,
  otherwise `modtargets`; `.all`/`.prf` → `modtargets` (those formats were
  introduced together with modTargets).

A kit made on V0.2x firmware and later re-saved by newer firmware has 228
bytes but still page-encoded targets (the LXR1 never converted them). If such
a kit's LFO targets look wrong, convert it again with
`--target-encoding pages`.

### 9.3 `pNNN.snd`

```
0     8 bytes  name (space padded)
8     N bytes  kit parameter block (N = 221 or 228)
```

### 9.4 Pattern-set block (`pNNN.pat` after its 8-byte name)

```
steps      7,168 x 7 bytes   volume|0x80 active, prob, note,
                             p1 dest, p1 value, p2 dest, p2 value
                             step i: track = (i // 128) // 8,
                                     pattern = (i // 128) % 8,
                                     sub-step = i % 128
main       56 x uint16       pattern * 7 + track; bit m = main step m on
info       8 x 2 bytes       next pattern, change bar
shuffle    1 byte            0..127
lengths    56 bytes          optional; low nibble length in main steps
                             (0 = 16), high nibble rotation
```

A pattern has 16 main steps of 8 sub-steps (1/128 notes). A sub-step plays
only when its main step is on. Step automation destinations are
`ParamEnums` numbers in the mainboard's numbering: numbers below 128 are
stored +1, 128 and above unchanged; 0 or 0xFF = none. Next pattern 0..7 is a
pattern, 8..14 is `r2`..`r8` (random among the first 2..8 patterns). A file
without the length block (pre-2014) plays every track at 16 steps.

### 9.5 `pNNN.all` and `pNNN.prf`

```
0     8 bytes    name
8     1 byte     version (1 or 2)
9     64 bytes   .all: globals from PAR_BPM (251) on, 0xFF padded
                 .prf v2: BPM, bar reset, 0xFF padding
                 .prf v1: BPM only (1 byte, no padding)
+     512 bytes  kit parameter block, 0xFF padded
+     ...        pattern-set block (9.4)
```

## 10. Reference: parameter and value mapping

### 10.1 Values

All values copy unchanged: Helicase kept the LXR1 value domains and menu
tables (waveforms `Sin Tri Saw Rec Noi Cym`, filters `LP HP BP UBP Nch Pek
LP2 off`, LFO waves, sync rates, retrigger, audio out `St1 St2 L1 R1 L2 R2`,
pan 0..127 with 63 = centre).

### 10.2 Keys

Kit slots: 1–3 Drum (`.drm`), 4 Snare (`.snr`), 5 Cymbal (`.cym`), 6 HiHat
(`.hat`). `n` below is the voice number.

| Helicase key | Drum (n=1..3) | Snare (4) | Cymbal (5) | HiHat (6) |
|---|---|---|---|---|
| `osc1_wave` | `OSC_WAVE_DRUMn` | `OSC_WAVE_SNARE` | `WAVE1_CYM` | `WAVE1_HH` |
| `osc1_pitch_coarse` / `_fine` | `COARSEn` / `FINEn` | `COARSE4` / `FINE4` | `COARSE5` / `FINE5` | `COARSE6` / `FINE6` |
| `osc2_wave` | `MOD_WAVE_DRUMn` | — | `WAVE2_CYM` | `WAVE2_HH` |
| `osc2_pitch_coarse` | `FM_FREQn` | — | `MOD_OSC_F1_CYM` | `MOD_OSC_F1` |
| `osc2_mod_amount` | `FMAMNTn` | — | `MOD_OSC_GAIN1_CYM` | `MOD_OSC_GAIN1` |
| `osc2_mod_type` | `MIX_MOD_n` | — | — | — |
| `osc3_wave` / `osc3_pitch_coarse` / `osc3_mod_amount` | — | — | `WAVE3_CYM` / `MOD_OSC_F2_CYM` / `MOD_OSC_GAIN2_CYM` | `WAVE3_HH` / `MOD_OSC_F2` / `MOD_OSC_GAIN2` |
| `noise_freq` / `osc1_noise_mix` | — | `NOISE_FREQ1` / `MIX1` | — | — |
| `amp_envelope_attack` / `_decay` / `_slope` | `VELOAn` / `VELODn` / `VOL_SLOPEn` | `VELOA4` / `VELOD4` / `VOL_SLOPE4` | `VELOA5` / `VELOD5` / `VOL_SLOPE5` | `VELOA6` / `VELOD6_CLOSED` / `VOL_SLOPE6` |
| `amp_envelope_decay_choke` | — | — | — | `VELOD6_OPEN` |
| `amp_attack_repeat` | — | `REPEAT4` | `REPEAT5` | — |
| `pitch_envelope_decay` / `_amount` / `_slope` | `MOD_EGn` / `MODAMNTn` / `PITCH_SLOPEn` | `MOD_EG4` / `MODAMNT4` / `PITCH_SLOPE4` | — | — |
| `filter_freq` / `_reso` / `_drive` / `_type` | `FILTER_FREQ_n` / `RESO_n` / `FILTER_DRIVE_n` / `FILTER_TYPE_n` | same, n=4 | same, n=5 | same, n=6 |
| `instrument_vol` / `_pan` / `_decimation` | `VOLn` / `PANn` / `VOICE_DECIMATIONn` | n=4 | n=5 | n=6 |
| `instrument_drive` | `DRIVEn` | `SNARE_DISTORTION` | `CYMBAL_DISTORTION` | `HAT_DISTORTION` |
| `lfo_rate` / `_amount` / `_wave` / `_retrigger_voice` / `_sync` / `_offset` | `FREQ_LFOn` / `AMOUNT_LFOn` / `WAVE_LFOn` / `RETRIGGER_LFOn` / `SYNC_LFOn` / `OFFSET_LFOn` | n=4 | n=5 | n=6 |
| `velo_vol_on_off` / `velo_mod_amount` | `VOLUME_MOD_ON_OFFn` / `VELO_MOD_AMT_n` | n=4 | n=5 | n=6 |
| `transient_wave` / `_vol` / `_freq` | `TRANSn_WAVE` / `TRANSn_VOL` / `TRANSn_FREQ` | n=4 | n=5 | n=6 |
| `velo_mod_dest` | from `VEL_DEST_n` (10.3) | | | |
| `lfo_target_voice` / `lfo_target_param` | from `TARGET_LFOn` + `VOICE_LFOn` (10.3) | | | |

(`PAR_` prefixes omitted.) The script checks this table against the firmware
descriptors at start-up and stops if a key is missing or unknown.

### 10.3 Targets

1. The target byte is decoded (section 9.2) into the LXR1 parameter it
   names.
2. That parameter's Kit slot is the destination voice; its descriptor index
   on that slot's instrument type is the Helicase token.
3. The row must be a modulation target in Helicase (any descriptor except the
   target selectors). Otherwise the target becomes `off` (token 255) and a
   note is printed.
4. LFO: a destination equal to the LFO's own voice is written as `self`.
   With no target, the voice is `self` (or the stored LFO voice if valid).
5. Velocity: Helicase only offers the voice's own parameters, so a target on
   another voice becomes `off`.

Step automation uses the same map: the LXR1 destination becomes the
canonical ID `(slot − 1) × 64 + descriptor index`; values (0..127) copy
unchanged.

## 11. Reference: pattern conversion rules

### 11.1 Steps

One LXR1 main step becomes one Helicase step at the 1/16 scale (`track_scale`
76). Track length is the LXR1 track length (1..16 steps). An LXR1 rotation
`r` is baked in: Helicase step `j` holds LXR1 main step `(j + r) mod length`.
A main step that is off, or whose sub-steps are all off, is empty.

### 11.2 Specials

Taken from the first playing sub-step:

| LXR1 | Helicase special |
|---|---|
| volume 0..127 | velocity (100 = default, not stored) |
| note 0..127 | note (63 = default, not stored) |
| probability 127 | condition 0 (always) |
| probability 0..126 | condition r10..r95 (nearest 10 % bucket, same table as `tools/convert_pattern_specials.py`) |

### 11.3 Sub-steps: micro-timing and rolls

| Sub-step shape | Helicase |
|---|---|
| One sub-step `k` | Trigger; `k > 0` adds micro-timing `ceil(12.5 × k)` % (13, 25, 38, 50, 63, 75, 88), which the sequencer turns into exactly `3k` ticks of 96 PPQ. |
| Sub-steps from 0 to the end of the step, evenly 1, 2 or 4 apart | Roll at `128`, `64` or `32`, fill 100 — exact. |
| Anything else (flams, partial rolls, irregular rhythms) | `--partial-rolls first` (default): first hit only (with its micro-timing); a note is printed. `--partial-rolls fill`: if evenly spaced, a roll with fill `n × spacing / 8` — the hit count is exact, but the firmware silences the voice at the end of the fill, which cuts decays. Irregular shapes always keep the first hit. |

Per-sub-step volume differences inside a roll are not kept (the roll uses
the first hit's velocity, rhythm `off`).

### 11.4 Automation

The two automation slots of the first playing sub-step become the step's
automation (the LXR1 applies automation only on a step that triggers).
Destinations that are not Helicase Instrument parameters (all-voice sample
rate, MIDI notes, target selectors) are dropped with a note. A duplicate
target in the same step keeps the first value.

### 11.5 Shuffle

The pattern-set shuffle (0..127) is written unchanged to all seven tracks.
Both systems swing the off-beat 1/16 steps, but the curves differ, so heavy
LXR1 swing settings may need adjusting by ear.

### 11.6 Chains (banks only)

| LXR1 | Helicase Scene chain |
|---|---|
| next = this pattern | none (`nul`, `slf`) |
| next = pattern P, change bar C | `cn1` = `b0N` for N = C + 1 bars (N 1..8, else nearest of 16/32/64, with a note); `nx1` = `s0(P+1)` |
| next = 8..14 (`r2`..`r8`) | `cn1` as above; `nx1` = `r02`..`r08` |

The LXR1 counts bars by track 1's length; Helicase counts 16-step bars.

## 12. Reference: Helicase output

Authority: `knowledge_files/specification_reference/FILESYSTEM_SPEC.md` and
`PATTERN_DYNAMIC_STACK.md` §1–§4, §7.

- **Instrument files**: `format=helicase.instrument`, `version=1`,
  `type=`, `[params]` with every descriptor in table order, `[morph]` with
  every morphable row; LFO voice `self` when it points to its own slot.
- **kitset.kcg**: version 1, `slot6_track7_amp_envelope_decay` and its Morph
  twin from the open hi-hat decay, six `[slotN]` sections. No `audio_out`.
- **PAT4**: exactly 10,656 bytes; version 2, stack 256, header 160,
  generation 0, CRC32C over the file with bytes 14..17 as zero; track header
  (length, scale 76, shuffle, run mode `fwd`, loopable, `rmp` off); address
  entries with trigger/specials bits; dynamic blocks packed from pool offset 0
  in track/step order (big-endian back-reference header, flags, specials in
  flag order, little-endian automation entries `value << 9 | target`, padded
  to 4 bytes); bitmap bits for the used chunks and the upper 256 bytes 0xFF;
  nothing in the 136-byte swap block. Every image is decoded again and
  compared with the source (CRC, back-references, bitmap, no duplicate
  targets) before it is written.
- **sceneset.scg**: every key in the firmware writer's order; track Morph
  endpoints equal the Pattern's length/scale/shuffle, so Morph changes
  nothing.
- **bankset.bcg**: version 2, `active_scene=0`, 16 indexed edit-mask lines.

## 13. Troubleshooting

| Message | Meaning / action |
|---|---|
| `slot NNN is occupied (...)` | `--keep-numbers` hit an existing entry. Add `--overwrite`, or drop `--keep-numbers`. |
| `225-byte kit payload is the LXR V0.1x layout` | Pre-release (early 2013) kit; its parameter offsets differ from every later firmware and are not supported. |
| `pattern data is N bytes, expected at least 50305` | Truncated `.pat`/`.all`/`.prf` (the pattern-set block without its optional 56 length bytes is 50,305 bytes). |
| `file version N is newer than 2` | `.all`/`.prf` from an unknown firmware. |
| `firmware descriptor table not found` | Pass `--firmware-root` pointing at a Helicase source tree. |
| `key map is out of date with the firmware descriptors` | The firmware's instrument parameters changed; update `SLOT_KEY_MAP` / `HELICASE_ONLY_DEFAULTS` in `lxr1_common.py` (section 14). |
| `Pattern pool is full` | Cannot happen with LXR1 data (at most 112 blocks of ≤ 12 bytes); indicates a bug. |
| LFO targets on the wrong parameter | See 9.2: retry with `--target-encoding pages` or `modtargets`. |

## 14. Maintenance

- **Firmware instrument changes** (new or renamed descriptor keys): add the
  key to `SLOT_KEY_MAP` (LXR1 source) or `HELICASE_ONLY_DEFAULTS` (neutral
  value). The start-up coverage check names the keys involved.
- **PAT4 changes** (version, header bytes, special encoding): update the
  constants and `build_pat4()` / `verify_pat4()` in `lxr1_common.py`
  together.
- **Sceneset / bankset keys**: `write_sceneset()` mirrors
  `filesystem_nextScenesetLine()`; `write_bankset()` mirrors
  `storage_formatBanksetLine()`.
- **Self-checks** in `lxr1_common.py` (assertions at import) pin the LXR1
  enum offsets, the modTargets indices and the V0.2x page table; a failure
  there means a table was edited incorrectly.
