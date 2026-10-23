# LXR-02 Converters — Readme, Manual and Reference

Host-side Python tools that turn Erica Synths LXR-02 SD-card content into
Helicase library content:

| LXR-02 content | Script | Helicase result |
|---|---|---|
| `NN-NAME.snd` (kit) | `snd_to_kit.py` | `Kit/NNN <name>/` (kitset + 6 Instruments) |
| `NN-NAME.pat` (one pattern) | `pat_to_pattern.py` | `Pattern/NNN <name>.pat` (PAT4) |
| `projNN/` (project: 64 patterns + kits + globals) | `prj_to_bank.py` | up to 4 Banks `Bank/NNN <project> A..D/`, one Scene per pattern |
| `NN-NAME.sng` (song) | `sng_to_bank.py` | `Bank/NNN <song>/`, Scenes in song order, chained |

`lxr2_common.py` holds the LXR-02 readers and the LXR-02 → Helicase
translation. The Helicase writers themselves (Instrument/Kit files, PAT4,
`sceneset.scg`, slot allocation) are shared with the LXR1 converters and
live in `../lxr1_converters/lxr1_common.py` (section 2).

Contents:

1. [Quick start](#1-quick-start)
2. [Requirements](#2-requirements)
3. [Common behaviour](#3-common-behaviour)
4. [snd_to_kit.py](#4-snd_to_kitpy--kits)
5. [pat_to_pattern.py](#5-pat_to_patternpy--patterns)
6. [prj_to_bank.py](#6-prj_to_bankpy--projects)
7. [sng_to_bank.py](#7-sng_to_bankpy--songs)
8. [Putting the result on a card](#8-putting-the-result-on-a-card)
9. [What is and is not converted](#9-what-is-and-is-not-converted)
10. [Reference: LXR-02 file formats](#10-reference-lxr-02-file-formats)
11. [Reference: parameter and value mapping](#11-reference-parameter-and-value-mapping)
12. [Reference: pattern conversion rules](#12-reference-pattern-conversion-rules)
13. [Reference: song conversion rules](#13-reference-song-conversion-rules)
14. [Reference: Helicase output](#14-reference-helicase-output)
15. [Troubleshooting](#15-troubleshooting)
16. [Maintenance](#16-maintenance)

---

## 1. Quick start

From the repository root, with an LXR-02 card mounted or copied to disk:

```sh
# Kits from every project on the card (untouched init-kit slots skipped)
python3 tools/lxr2_converters/snd_to_kit.py /Volumes/LXR02

# Patterns (one library Pattern per non-empty pattern)
python3 tools/lxr2_converters/pat_to_pattern.py /Volumes/LXR02

# Each project as Banks (16 patterns per Bank)
python3 tools/lxr2_converters/prj_to_bank.py /Volumes/LXR02

# Each song as a self-playing Bank
python3 tools/lxr2_converters/sng_to_bank.py /Volumes/LXR02
```

Any argument can also be a single project folder (`/Volumes/LXR02/proj03`)
or a single file. By default the output goes into this repository's
`SD_CARD/` tree (`SD_CARD/Kit`, `SD_CARD/Pattern`, `SD_CARD/Bank`). Use
`--out` to write somewhere else, for example straight into a mounted
Helicase card:

```sh
python3 tools/lxr2_converters/prj_to_bank.py --out /Volumes/HELICASE/Bank /Volumes/LXR02/proj03
```

Add `--dry-run` to any command to see what would be written, with every
conversion note, without writing anything.

Unsaved work: the LXR-02 keeps edits that were not saved into a project in
the card folder `/tmp`. Save the project on the LXR-02 before converting,
or pass `/tmp` (or `tmp/tmp.snd`, `tmp/tmp.pat`) explicitly (section 3).

## 2. Requirements

- Python 3.9 or newer, standard library only (no packages to install).
- A Helicase source tree. The scripts read the instrument descriptor tables
  in `Core/DSP/Instruments/*/*Parameters.c` every time they run, so key
  order, target tokens and automation IDs always match that firmware. By
  default this is the repository the scripts live in; another tree can be
  named with `--firmware-root`.
- `tools/lxr1_converters/lxr1_common.py` next to this folder. The LXR-02
  scripts import the Helicase writers from it (`lxr2_common.py` adds that
  folder to the import path), so both converter sets write identical
  Helicase structures. Keep the two folders together.
- Run the scripts from any directory; they find their modules themselves.

## 3. Common behaviour

### Inputs

Every script takes one or more `INPUT` arguments:

| Input | `snd_to_kit` / `pat_to_pattern` / `sng_to_bank` | `prj_to_bank` |
|---|---|---|
| A file | that file | — (needs a folder) |
| A project folder (`projNN`, or any folder of `NN-*` files) | its `.snd` / `.pat` / `.sng` files | that project |
| A card root (a folder with `projNN` sub-folders) | the files of every `projNN`, in number order | every `projNN` |

Extensions match case-insensitively; macOS `._*` files are ignored. When a
card root is given, `/tmp` is **not** read: it repeats slots of the last
project used, so converting both would duplicate content. Name it
explicitly to convert it:

- `tmp/NN-NAME.*` files are ordinary slot files (unsaved edits of slot NN);
- `tmp/tmp.snd` and `tmp/tmp.pat` are the LXR-02 edit buffers (the kit and
  pattern being edited when the LXR-02 was switched off). They have no name
  and are named `Tmp NN` after the slot they were edited from.

### What is skipped by default

| Script | Skipped | Option to include |
|---|---|---|
| `snd_to_kit.py` | kit slots still holding the untouched LXR-02 init kit (a new project fills all 64 slots with it) | `--keep-init` |
| `pat_to_pattern.py` | patterns with no step that plays | `--keep-empty` |
| `prj_to_bank.py` | Scenes for patterns with no step that plays (or no file); Banks with no Scene | `--keep-empty` (all 16 Scenes of all 4 Banks) |
| `sng_to_bank.py` | empty songs; the default song every project has (pattern 00, then end) | `--keep-default` (empty songs are always skipped) |

"A step that plays" means: its step button is on, it lies within its
track's length, and its probability is above 0.

### Library slots

Helicase library folders and files start with a three-digit slot,
`000`..`999`. Existing entries are never touched unless you ask:

| Option | Effect |
|---|---|
| (default) | Each output takes the lowest free slot at or after `--start-slot` (default 0). Occupied slots are skipped. |
| `--start-slot N` | Start the free-slot search at `N`. |
| `--keep-numbers` | Kits and Patterns only: use the LXR-02 slot number (`07-NAME.snd` → `007`, old `p007.snd` → `007`). An occupied slot is refused. Every project uses slots 00..63, so convert one project per run with this option. The `tmp.snd` / `tmp.pat` edit buffers always take a free slot. |
| `--overwrite` | With `--keep-numbers`: delete what occupies the slot, then write. |

Banks always use free slots: a project gives up to four Banks and each song
one more, so LXR-02 numbers cannot be kept.

### Names

LXR-02 names are 8 characters; a slot whose name is blank or `Empty` (the
LXR-02 shows unnamed slots as `Empty`) counts as unnamed. Characters other
than letters, digits, space, `-` and `_` become spaces, runs of spaces
collapse, and the result is trimmed to the Helicase 8-character limit
(the same rule as the LXR1 converters).

| Output | Name | Unnamed fallback |
|---|---|---|
| Kit folder | kit name | `Kit NN` |
| Library Pattern | pattern name | `<project> NN` (`TestP 17`) |
| Project Bank | `<project> A`..`D` (patterns 00–15, 16–31, 32–47, 48–63) | project folder name |
| Scene in a Bank | pattern name | `<project> NN` |
| Song Bank | song name | `<project> sNN` (`Test s03`) |
| Kit inside a Scene | `Kit <kit name>` | `Kit Kit NN` |

`NN` is always the LXR-02 slot number as the LXR-02 shows it and as its
file names use it: 00..63. Instrument files are named
`<6-char prefix><d1|d2|d3|s1|c1|h1>.<ext>` (`boomd1.drm`), as for LXR1
kits.

### Shared options

| Option | Scripts | Meaning |
|---|---|---|
| `--out DIR` | all | Library directory to write into. |
| `--start-slot N` | all | First slot to consider (0..999). |
| `--dry-run` | all | Convert and report, write nothing. |
| `--firmware-root DIR` | all | Helicase source tree for the descriptor tables. |
| `--keep-numbers`, `--overwrite` | kit, pattern | See "Library slots". |
| `--keep-init` | kit | Also convert untouched init kits. |
| `--keep-empty` | pattern, project | Also write empty patterns / Scenes. |
| `--keep-default` | song | Also convert the default song. |

### Output, notes and errors

Each converted item prints one line, for example:

```
proj00/01-Boom.snd 'Boom' -> SD_CARD/Kit/001 Boom
proj00/00-Beat.pat 'Beat' (PatV03, kit 01) -> SD_CARD/Pattern/000 Beat.pat
proj00 'TestPrj' patterns 16..31 -> SD_CARD/Bank/001 TestPr B (2 Scene(s))
proj00/01-Song.sng 'Song' (6 entries, loop) -> SD_CARD/Bank/002 Song (3 Scene(s))
```

Anything that could not be carried exactly is printed underneath as an
indented note, labelled with the kit or pattern slot it concerns:

```
  proj00: kit 01: voice 1 PWM waveform written as Rec (pulse width 40 not carried; Helicase has no PWM)
  proj00: pattern 00: 1 negatively shifted step(s) could not move into the previous step (it has its own hit) and play on the beat
```

Section 9 lists every note and what it means. A file or project that cannot
be read (truncated, unknown version) prints `ERROR <name>: <reason>` and is
skipped; the rest of the batch continues. In a project, one unreadable slot
file fails the whole project, as it fails the LXR-02's project load. Each
item is fully converted (and every PAT4 image verified) before anything is
written for it. The exit status is 0 when everything converted and 1 when
anything failed.

## 4. `snd_to_kit.py` — kits

```
python3 tools/lxr2_converters/snd_to_kit.py [--out DIR] [--keep-init]
        [--keep-numbers] [--overwrite] [--start-slot N]
        [--firmware-root DIR] [--dry-run] INPUT [INPUT ...]
```

Writes one `Kit/NNN <name>/` folder per kit:

```
Kit/001 Boom/
  kitset.kcg
  boomd1.drm  boomd2.drm  boomd3.drm  booms1.snr  boomc1.cym  boomh1.hat
```

- Every instrument parameter is copied (section 11).
- Waveform `PWM` is written as `Rec`; the pulse width is not carried.
- LFO and velocity targets are translated to Helicase tokens; targets on a
  PWM or FX parameter become off.
- `[morph]` holds the same values as `[params]`, so the Kit sounds the same
  at any Morph amount.
- The voice audio-out routes are Scene settings in Helicase and are not part
  of a Kit (use `prj_to_bank.py` / `sng_to_bank.py` to keep them). The kit's
  FX block and all-voice sample rate are reported, not converted.

Example: the kits of project 03 into their own slot numbers:

```sh
python3 tools/lxr2_converters/snd_to_kit.py --keep-numbers /Volumes/LXR02/proj03
```

## 5. `pat_to_pattern.py` — patterns

```
python3 tools/lxr2_converters/pat_to_pattern.py [--out DIR] [--keep-empty]
        [--keep-numbers] [--overwrite] [--start-slot N]
        [--firmware-root DIR] [--dry-run] INPUT [INPUT ...]
```

An LXR-02 `.pat` holds one pattern of 7 tracks × 64 steps; it becomes one
Helicase library Pattern, `Pattern/NNN <name>.pat`. The pattern's kit number
has no meaning for a library Pattern; it is shown in the output line
(`kit 01`). Conversion rules: section 12. All three LXR-02 pattern versions
(`PatV01`..`PatV03`) are read; the older two have no flam/shift data (and
`PatV01` no kit number), which is reported.

## 6. `prj_to_bank.py` — projects

```
python3 tools/lxr2_converters/prj_to_bank.py [--out DIR] [--keep-empty]
        [--start-slot N] [--firmware-root DIR] [--dry-run] INPUT [INPUT ...]
```

An LXR-02 project holds 64 patterns, a Helicase Bank 16 Scenes. Each group
of 16 patterns becomes one Bank, and each pattern with a playing step
becomes the Scene at its place in the group:

```
Bank/000 TestPr A/          (patterns 00..15)
  bankset.bcg
  00 Beat/                  (pattern 00)
    sceneset.scg
    Beat.pat
    Kit Boom/               (kitset.kcg + 6 Instruments)
  05 Old/                   (pattern 05; 01..04 were empty)
Bank/001 TestPr B/          (patterns 16..31)
  01 TestP 17/              (pattern 17, unnamed)
  04 Fill/                  (pattern 20)
```

Helicase allows gaps between Bank Scene slots, so the Scene number always
tells you the pattern number (`B` + Scene `04` = pattern 20).

- **The kit each pattern plays with.** With the LXR-02 pattern mode `Aut`
  (the default) a pattern loads its own kit number when it starts; each
  Scene embeds that kit. With pattern mode `off` the kit never changes, so
  every Scene embeds kit 00 (the kit the LXR-02 loads with the project). A
  missing kit file is replaced by the LXR-02 init kit (with a note).
- **Shared kits share an edit mask.** Scenes that play the same LXR-02 kit
  slot are put in one VOICE edit mask in `bankset.bcg`, so an Instrument,
  Kit or Effect edit in one reaches the others, as editing that kit did on
  the LXR-02. Scenes with different kits keep their own mask (Helicase masks
  never overlap).
- **Scene settings** in every `sceneset.scg`:
  - `audio_out`: the kit's six voice routes; a voice routed to the LXR-02
    FX bus goes to the FX output (section 11.4);
  - `midi_channel`, `midi_note`: the seven track channels and notes from
    `glo.cfg` (a fixed track note becomes the track note root; notes still
    at the LXR-02 default, 48 on every track, become `on`; section 11.5).
- **Active Scene** is the lowest written Scene. No Effect file is written;
  Scenes load with the Effect `off`. There are no chains (the LXR-02 only
  chains patterns in songs: `sng_to_bank.py`).
- BPM, bar reset, MIDI routing/filters, clock settings, the global MIDI
  channel and the screen saver are global settings in Helicase too and are
  not converted.

## 7. `sng_to_bank.py` — songs

```
python3 tools/lxr2_converters/sng_to_bank.py [--out DIR] [--keep-default]
        [--start-slot N] [--firmware-root DIR] [--dry-run] INPUT [INPUT ...]
```

A song names patterns by number, so it is converted together with the
project folder it lives in. Each song becomes one Bank whose Scenes are the
song's entries in play order, chained so the Bank plays the song on its
own:

```
song 01 'Song' = 00 00 00 05 20 20 (loop)

Bank/002 Song/
  00 Beat/   pattern 00, chain x03 -> s02
  01 Old/    pattern 05, chain x01 -> s03
  02 Fill/   pattern 20, chain x02 -> s01   (loop back)
```

- Consecutive entries of the same pattern become one Scene that repeats
  (`x01`..`x08`, `x16`, `x32`, `x64`; other counts are split, e.g. 9 =
  `x08` + `x01` over two Scenes).
- The last Scene stops playback (`stp`) when the song ends with `end`, or
  returns to Scene 00 when it loops. A one-Scene loop has no chain.
- Kits, edit masks, audio out and MIDI settings are written as by
  `prj_to_bank.py`.
- A Bank holds 16 Scenes. A longer song keeps its first 16 Scenes and stops
  after the last (with a note).
- **Set `pcr` (PC reset) on** in the Helicase global settings when the
  song's patterns differ in length (the script prints a note then). The
  LXR-02 restarts every track at each pattern change; Helicase does so for a
  chain-fired change only with `pcr` on. With `pcr` off, Helicase counts
  repetitions on the running master clock, so a change can come early.

Start the song by selecting Scene 00 of the Bank and pressing play.

## 8. Putting the result on a card

1. Convert into the card's root folders (`--out /Volumes/<card>/Kit`, etc.),
   or into `SD_CARD/` and copy the new folders/files across.
2. Boot the Helicase. Boot rescans the Kit, Scene, Bank and Pattern
   libraries, canonicalises names and rewrites each `.hcindex`, so the new
   slots appear in the Load menus.
3. Load a Kit with Load:Kit, a Pattern with Load:Pattern, a Bank with
   Load:Bank. For song Banks, set `pcr` on if section 7 says so.

The converters never write `.hcindex`, `.hcnames`, `settings.cfg` or
AutoSave files.

## 9. What is and is not converted

| LXR-02 data | Result |
|---|---|
| All sound parameters of the six voices | Copied 1:1 into the Instrument files (section 11). |
| Waveform `PWM` (5) | `Rec` (3); note "PWM waveform written as Rec". |
| PWM amount (`PAR_PWM_n`) | Not carried (Helicase has no PWM); named in that note. |
| Open hi-hat decay | HiHat `amp_envelope_decay_choke` and the Kit's `slot6_track7_amp_envelope_decay`. |
| LFO target (voice + parameter) | Helicase `lfo_target_voice` / `lfo_target_param` (any voice). |
| Velocity target | Helicase `velo_mod_dest` if on the same voice, else off (Helicase velocity targets are voice-local). |
| Targets on PWM, FX P1..P4, the all-voice sample rate, a target selector | Off, with a note. |
| All-voice sample rate (`srt`) | Not carried; note when not 127 (Helicase retired it). |
| Kit FX block (type, routing, dry/wet, parameters) | Not converted; note when the FX type is not `off`. Helicase's Effect types (`flt`, `cbt`) do not match `drv rng cmp del`. |
| Voice audio out 0..5 | Scene `audio_out` (banks only). |
| Voice audio out `FX` (6) | Scene `audio_out` = the FX output (`PAR_FX_ROUTING`); note when the FX was on. |
| Track MIDI channel / note (`glo.cfg`) | Scene `midi_channel` / `midi_note` (banks only); the untouched default notes (48 on all seven tracks) become `on`; note when tracks share a note on one channel. |
| Pattern mode (`glo.cfg`) | Decides which kit each Scene embeds (banks only). |
| Steps, velocity, note, probability | Step trigger and specials (section 12). |
| Flam | Roll, exact. |
| Shift | Micro-timing or a move to the previous step (section 12.4). |
| Step automation (2 per step) | Step automation where Helicase can automate the target. |
| Track length (1..64) | Track length. |
| Pattern shuffle | Every track's shuffle (approximate curve). |
| Pattern kit number | Scene kit (banks); shown for library Patterns. |
| Songs | Chained Banks (section 13). |
| BPM, bar reset, MIDI routing/filters/global channel, clock, quantisation, screen saver, follow | Not converted (global settings). |
| Morph kit destination, kit Morph | Not converted; `[morph]` = `[params]`. |

New Helicase parameters with no LXR-02 equivalent get the value that keeps
the LXR-02 sound: `lfo_polarity` = 0 (`neg`, the original LXR LFO law),
`lfo_amount_2` = 0, `lfo_target_voice_2` = `self`, `lfo_target_param_2` =
off; Scene FX sends, Morph amounts and bus compressor are 0/`off`.

Every note the scripts print:

| Note | Meaning |
|---|---|
| `voice N PWM waveform written as Rec (pulse width P not carried …)` | An oscillator of voice N used `PWM`. |
| `voice N waveform value V is out of range; set to Sin` | Corrupt waveform byte (above 5). |
| `voice N <velocity\|LFO> target PAR_… is an LXR-02 <PWM\|FX> parameter …; set to off` | See the table above. |
| `voice N LFO target PAR_… has no Helicase Instrument parameter; set to off` | Target is the all-voice sample rate or a target selector. |
| `voice N velocity target is on voice M; … set to off` | Cross-voice velocity target. |
| `all-voice sample rate (srt) V not carried` | `srt` was not at full rate. |
| `FX '<type>' (dry/wet D) not converted` | The kit had an effect on. |
| `voice N played through FX '<type>'; now dry on <out>` | Banks: the voice was routed to the FX bus. |
| `kit NN has no file; using the LXR-02 init kit` | A pattern's kit slot has no `.snd`. |
| `kit number K is invalid; using kit 00` | Corrupt pattern kit number. |
| `pattern NN has no file; its Scene is empty` | A song names a pattern slot without a file. |
| `no glo.cfg; MIDI channels use the LXR-02 default (channel 1)` | The project folder has no `glo.cfg`; track notes are then the untouched default and become `on`. |
| `track MIDI notes are the LXR-02 default (48 on every track); written as `on` (no note root)` | Section 11.5. |
| `tracks a, b share MIDI note N on channel C; Helicase gives it to track a` | Deliberately set notes that coincide: Helicase gives a note root to one track only (MIDI_SPEC.md §4.1). |
| `N step(s) with probability 0 never play on the LXR-02 and were left out` | Section 12.2. |
| `N negatively shifted step(s) could not move into the previous step …` | Section 12.4. |
| `N flam(s) shifted into the next step end at that step's hit …` | Section 12.3. |
| `N step note(s) above 124 clamped to 127 after the +3 pitch offset` | Section 12.2. |
| `N automation value(s) aimed at parameters Helicase cannot automate were dropped` | Section 12.5. |
| `track T length L plays as 64` | Corrupt length above 64. |
| `PatV0N file: no flam/shift data[, no kit number]` | Older pattern version. |
| `slot NN: several .ext files (…); using …` | Two `NN-*` files for one slot; the LXR-02 also loads only one. |
| `song needs N Scenes after merging repeats; the first 16 are kept …` | Section 13. |
| `Scenes differ in length: set pcr (PC reset) on …` | Section 7. |

## 10. Reference: LXR-02 file formats

Source: <https://github.com/erica-synths/lxr-desktop-app> (commit
`a43cd4f`, 2026-10-23), `Core/Src/Preset/presetManager.c` (readers and
writers), `Core/Inc/Parameters.h` (offsets),
`Core/Src/Soundengine/Sequencer/sequencer.c` (playback). Multi-byte values
are little-endian.

### 10.1 Card layout

```
/proj.idx              last project number (1 byte; not read)
/projNN/               NN = 00..63, one folder per project
  prj.nfo              project name, 8 bytes
  glo.cfg              globals, 30 bytes (10.6)
  NN-NAME.snd          kit slot NN (00..63)
  NN-NAME.pat          pattern slot NN
  NN-NAME.sng          song slot NN
/tmp/                  unsaved edits of the current project
  NN-NAME.snd|pat|sng  edited slots
  tmp.snd, tmp.pat     edit buffers (10.2, 10.3)
```

`NAME` is the first five characters of the slot's name with spaces and
unusual characters replaced by `_`. The LXR-02 finds a slot by the pattern
`NN-*` (first match) and falls back to the old name `pNNN.ext` (`p007.snd`).
Missing slot files are allowed: the slot simply keeps what was loaded
before.

### 10.2 Kit — `NN-NAME.snd`

```
0     8 bytes    name ("Empty" or blank = unnamed)
8     247 bytes  one byte per enum ParamEnums value 0..246
```

Total 255 bytes. Short files are zero-filled on load (as the firmware
does); extra bytes are ignored. Selected offsets: `PAR_COARSE1` 8,
`PAR_VELOD6_OPEN` 61, `PAR_VOICE_DECIMATION_ALL` 114, `PAR_FILTER_DRIVE_1`
128, `PAR_VEL_DEST_1` 149, `PAR_VOICE_LFO1` 161, `PAR_TARGET_LFO1` 167,
`PAR_AUDIO_OUT1` 215, unused 221..227 (the old MIDI notes),
`PAR_PWM_1..6` 228..233, `PAR_FX_TYPE` 234, `PAR_FX_ROUTING` 235,
`PAR_FX_DRY_WET` 236, `PAR_FX_DIST_TYPE` 237, `PAR_FX_P1..P4` 238..241,
`PAR_FX_RATIO` 242, `PAR_FX_WAVE` 243, `PAR_FX_REVERB_TYPE` 244,
`PAR_FX_DELAY_TYPE` 245, `PAR_MORPH_KIT_DEST` 246. (The comments in
`Parameters.h` after offset 220 are stale; the converter evaluates the
enum.)

`tmp.snd`: 1 byte (the kit slot being edited) + the 247 parameter bytes,
no name.

Init kit (`Core/Src/Preset/initKit.c`): name `Initkit` + 246 bytes. A new
project gives all 64 kit slots the init kit's first 239 parameters (the
copy in `preset_init()` stops there). `snd_to_kit.py` recognises an
untouched slot by the name `Initkit` plus those 239 bytes.

### 10.3 Pattern — `NN-NAME.pat`

```
0     8 bytes    name
8     6 bytes    version tag "PatV02" / "PatV03" (absent in version 1)
+     pattern data:
        per track (7):
          64 x 7 bytes   steps: volume (|0x80 legacy active bit),
                         probability 0..100, note, p1 dest, p1 value,
                         p2 dest, p2 value
          64 bytes       (PatV03 only) timing: flam = low nibble,
                         shift = high nibble
        per track (7), per bar (4):
          uint16         main steps; bit n = step 16*bar + n on
        1 byte           shuffle 0..127
        7 bytes          track lengths (0 = 16; optional in PatV01)
        1 byte           (PatV02+) kit number 0..63
```

| Version | Data bytes | File bytes |
|---|---|---|
| PatV01 | 3,200 (3,193 without lengths) | 3,208 |
| PatV02 | 3,201 | 3,215 |
| PatV03 | 3,649 | 3,663 |

`tmp.pat`: 1 byte (the pattern slot) + PatV03 data, no name, no tag.

Step semantics (`sequencer.c`):

- A step plays when its main-step bit is on (the `0x80` volume bit is no
  longer used) and a random 1..100 is ≤ its probability.
- Volume 0..127 is the trigger velocity; a cleared step has volume 127,
  probability 100, note 60, no automation, timing `0x70`.
- Flam `0..5` = off, ×2, ×3, ×4, ×6, ×8: that many evenly spaced hits over
  the 24-tick step.
- Shift raw `0..14` = −7..+7 (raw 7 = 0); one unit = 3 ticks = 1/8 step. A
  negative shift plays the hit in the previous step's time; a positive one
  delays it (flam hits past the step end run into the next step).
- Automation destinations are parameter numbers stored like the LXR1:
  below 128 stored +1, 128 and above unchanged; `0xFF` or 0 = none. They
  apply when a step triggers, using the automation of the step whose time
  the hit plays in.
- A track plays `length` steps (0 = 16, at most 64). The pattern advances
  (in a song) when its longest track ends.

### 10.4 Song — `NN-NAME.sng`

```
0     8 bytes    name
8     64 bytes   pattern numbers 0..63; 64 = end (stop playback),
                 65 = loop (back to the first entry)
```

All 64 entries used = loop. A new project's song 00 is `00, end, …`.

### 10.5 Project name — `prj.nfo`

8 bytes, the project name.

### 10.6 Globals — `glo.cfg`

One byte per parameter from `PAR_BPM` (272) to `PAR_PATTERN_MODE` (301):

| Byte | Parameter | Byte | Parameter |
|---:|---|---:|---|
| 0 | BPM | 13 | MIDI routing |
| 1..6 | MIDI channel tracks 1..6 | 14, 15 | MIDI TX / RX filter |
| 7 | fetch | 16..18 | clock prescalers |
| 8 | follow | 19 | trigger gate mode |
| 9 | quantisation | 20 | bar reset mode |
| 10 | screen saver | 21 | global MIDI channel |
| 11 | MIDI mode | 22..28 | MIDI note tracks 1..7 (0 = send the step note) |
| 12 | MIDI channel track 7 | 29 | pattern mode (0 `off`, 1 `Aut`) |

Defaults when missing (`preset_loadDefaultGlobals()`): channels 1, notes 48,
pattern mode `Aut`.

## 11. Reference: parameter and value mapping

### 11.1 Shared offsets

The LXR-02 and the Helicase both descend from LXR 0.37: kit offsets 0..220
are identical to the LXR1 kit (checked when `lxr2_common.py` is imported)
and the value domains are the same (filter, LFO wave, sync, retrigger,
transient and FM-mode tables, 0..127 ranges, pan 63 = centre). The
converter therefore translates an LXR-02 kit into the LXR1 kit layout
(`lxr1_kit_image()`) and writes it with the LXR1 kit writer; the Helicase
key ↔ parameter table is section 10.2 of
`../lxr1_converters/LXR1_CONVERTERS.md`.

The LXR-02 DSP itself was developed separately from the Helicase after
both left LXR 0.37. The converter keeps every value; whether every value
sounds identical on both machines is not verified (see the handoff log).

### 11.2 Waveforms

| Value | LXR-02 | Helicase | Written |
|---:|---|---|---|
| 0..4 | Sin Tri Saw Rec Noi | same | unchanged |
| 5 | PWM | Cym (crash) | 3 (`Rec`) |

Applies to every oscillator waveform: drum osc and FM-osc waves, snare
wave, cymbal and hi-hat waves 1..3.

### 11.3 Targets

Kit `PAR_VEL_DEST_n` and `PAR_TARGET_LFOn` bytes index the LXR-02
`modTargets[]` list (`Core/Src/menu/Cc2Text.c`, 229 entries: 0 off,
1 all-voice sample rate, then 38/38/38/37/38/38 entries for voices 1..6).
Each block lists the voice's parameters plus `PWM_n` and the shared
`FX_P1..P4`. The converter decodes the byte to its parameter name and
re-encodes it as that name's LXR1 index; from there the LXR1 rules apply:

1. The parameter's Kit slot is the destination voice; its descriptor index
   on that slot's instrument type is the Helicase token.
2. The row must be a Helicase modulation target; otherwise off (255) with
   a note.
3. LFO: a destination on the LFO's own voice is written as `self`. With no
   target, the voice is the stored LFO voice (an invalid one becomes 1, as
   on the LXR-02).
4. Velocity: a destination on another voice becomes off.

PWM and FX targets have no Helicase parameter and become off. A byte of
229 or more loads as 0 (off), as on the LXR-02.

### 11.4 Audio out

| LXR-02 | Helicase `audio_out` |
|---|---|
| 0..5 `St1 St2 L1 R1 L2 R2` | same |
| 6 `FX` | `PAR_FX_ROUTING` (0..5): where the FX bus outputs. With FX `off` the LXR-02 bus passes the voices there unchanged, so this is exact; with an effect on, the voice is dry (note). |

### 11.5 MIDI settings

| LXR-02 (`glo.cfg`) | Helicase Scene setting |
|---|---|
| track channel 1..16 | `midi_channel` 1..16 |
| track note 0 (send the step note) | `midi_note` 0 (`on`) |
| track note 1..127 (send this note; answer only to it) | `midi_note` = that note (track note root) |

A Helicase root is the start of the track's note range and plays the
default pitch, so the closest meaning, not an identical one (the LXR1
converters make the same choice). Where several LXR-02 tracks answer to
one note on one channel, Helicase gives it to the lowest track only.

**Untouched default.** A new LXR-02 project sets every track note to 48
(`preset_loadDefaultGlobals()`). Written literally, all seven tracks would
get root 48 and only track 1 would receive MIDI notes. When all seven notes
are 48 (or `glo.cfg` is missing), the notes were never set, so every track
gets Helicase's own default `on` (no root) and MIDI input behaves like a new
Helicase Scene. A project with any other note setting is converted as
stored. Channels are always copied. This affects MIDI only, never how a
pattern plays or sounds. (The step-note pitch reference, 60 on the LXR-02
and 63 on Helicase, is a separate matter: section 12.2.)

## 12. Reference: pattern conversion rules

### 12.1 Steps and tracks

One LXR-02 step becomes one Helicase step at the 1/16 scale (`track_scale`
76). Track length 1..64 is kept (Helicase tracks hold 128 steps). Steps
beyond a track's length are not converted (the LXR-02 never plays them).
The pattern shuffle 0..127 is written to all seven tracks; both systems
delay the off-beat 1/16 steps but the curves differ, so heavy shuffle may
need adjusting by ear.

### 12.2 Specials

| LXR-02 | Helicase special |
|---|---|
| volume 0..127 | velocity (100 = default, not stored) |
| note n | note n + 3 (63 = default, not stored). The LXR-02 plays note 60 at the sound's own pitch, Helicase note 63, so every step keeps its pitch. Notes 125..127 clamp to 127 (note). |
| probability 100 | condition 0 (always) |
| probability 1..99 | nearest of r10..r90 / r95 (conditions 1..10), ties up; 96..99 → r95 so a chance step never becomes certain |
| probability 0 | the step never plays: left out (note) |

### 12.3 Flam → roll

| Flam | Hits per step | Spacing (96-PPQ ticks) | Helicase roll |
|---|---:|---:|---|
| ×2 | 2 | 12 | `32` (5) |
| ×3 | 3 | 8 | `32t` (6) |
| ×4 | 4 | 6 | `64` (7) |
| ×6 | 6 | 4 | `64t` (8) |
| ×8 | 8 | 3 | `128` (9) |

Fill 100, rhythm off: exact. A flam that is also shifted forward runs past
the step end; when the next step has its own hit, the LXR-02 interleaves
both, while a Helicase roll ends at the next trigger (note).

### 12.4 Shift → micro-timing

Helicase micro-timing delays a 1/16 step by `floor(24 × t / 100)` ticks;
`t = ceil(12.5 × u)` lands exactly on `3u` ticks:

| Shift units u | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|---|---:|---:|---:|---:|---:|---:|---:|
| Helicase timing % | 13 | 25 | 38 | 50 | 63 | 75 | 88 |

- **Shift +u:** the step gets timing for u. Exact.
- **Shift −u:** the hit sounds `3u` ticks before its step, i.e. at
  `24 − 3u` ticks into the previous step. It moves to the previous Helicase
  step with timing for `8 − u`, if that step is free. A run of consecutive
  negatively shifted steps moves together when the step before the run does
  not play (or the run is the whole track); each hit then lands exactly
  where the LXR-02 plays it, and step 0 wraps to the last step of the track.
  When the step before the run plays a hit of its own, the run's hits stay
  on their own steps without the shift (note): Helicase holds one trigger
  per step.

### 12.5 Automation

A hit takes the two automation slots of the LXR-02 step whose time it plays
in (for a moved hit, the step it moved into), because the LXR-02 applies
the current step's automation when it triggers. Steps without a hit get no
automation: the LXR-02 applies automation only on a trigger, while Helicase
would also play automation on a step without one. Destinations decode like
the LXR1 (below 128 stored +1); the parameter becomes the canonical ID
`(slot − 1) × 64 + descriptor index`. Values copy (0..127). Destinations
with no Helicase Instrument parameter (PWM, FX, all-voice sample rate,
target selectors) are dropped (note); a duplicate target keeps the first
value. Both systems hold automation until the next trigger of the voice.

## 13. Reference: song conversion rules

1. Entries are read until `end` (64) or `loop` (65); all 64 used = loop.
   Other values above 63 end the song.
2. Consecutive entries of the same pattern merge into a run. A run of
   length n is split into chain counts from {64, 32, 16, 8, 7, …, 1},
   largest first; each part is one Scene.
3. Scenes are numbered 00, 01, … in play order. Scene i gets
   `cn1` = `xNN` (its count; condition 3..13) and `nx1` = `s(i+2)` (the
   next Scene; value 3 + i + 1). The last Scene gets `nx1` = `stp` (43) for
   `end` or `s01` (3) for `loop`. A song of one Scene that loops gets no
   chain (`nul`/`slf`).
4. More than 16 Scenes: the first 16 are written, the 16th stops.

`xNN` fires at the end of the Nth repetition of the Scene (its longest
track), which is where the LXR-02 advances a song. Scenes that repeat the
same pattern are separate Scenes only when the song visits the pattern
again later; each Scene embeds its own copy of the pattern and kit.

## 14. Reference: Helicase output

The Instrument files, `kitset.kcg`, PAT4 files and `sceneset.scg` are
written by the shared LXR1 writers; their exact format is section 12 of
`../lxr1_converters/LXR1_CONVERTERS.md` (authority:
`knowledge_files/specification_reference/FILESYSTEM_SPEC.md` and
`PATTERN_DYNAMIC_STACK.md`). Every PAT4 image is decoded again and checked
(CRC32C, back-references, bitmap, specials, automation) before it is
written. LXR-02 specifics:

- **bankset.bcg**: version 2; `active_scene` = the lowest written Scene; 16
  indexed lines `scene_mask_voice_edit_N`. Scenes that play the same LXR-02
  kit slot share one mask (e.g. `0005` for Scenes 00 and 02); every other
  slot, written or not, keeps its self-only mask.
- **Bank folders** may have gaps between Scene slots (FILESYSTEM_SPEC:
  "Scene slot numbers inside a bank do not need to be contiguous").
- **sceneset.scg**: `chain_condition` / `chain_next` carry the song chain in
  slot 1 (slot 2 is 0); project Banks have no chains. Track Morph endpoints
  equal the Pattern's length/scale/shuffle, so Morph changes nothing.

## 15. Troubleshooting

| Message | Meaning / action |
|---|---|
| `slot NNN is occupied (...)` | `--keep-numbers` hit an existing entry. Add `--overwrite`, or drop `--keep-numbers`. |
| `slot NNN assigned twice in this run` | `--keep-numbers` with several projects (each uses 00..63). Convert one project per run. |
| `not an NN-NAME / pNNN file name; drop --keep-numbers` | The file name carries no LXR-02 slot. |
| `truncated in the … (PatV0N needs N data bytes)` | A damaged pattern file. In a project, the whole project fails, as on the LXR-02. |
| `pattern version N is not supported` / `unreadable version tag` | Not an LXR-02 pattern, or from an unknown firmware. |
| `glo.cfg: N bytes, expected 30` | Damaged globals file. |
| `no NN-*.snd or NN-*.pat files; not an LXR-02 project folder` | `prj_to_bank.py` / `sng_to_bank.py` was given a folder without slot files (for a card root, check it has `projNN` folders). |
| `no LXR-02 input files found` | No matching files in the inputs. |
| `firmware descriptor table not found` | Pass `--firmware-root` pointing at a Helicase source tree. |
| `key map is out of date with the firmware descriptors` | The firmware's instrument parameters changed; update `lxr1_common.py` (section 16). |
| `ModuleNotFoundError: lxr1_common` | `tools/lxr1_converters/` is missing next to this folder. |
| Song changes come too early or late | Set `pcr` on (section 7). |
| LFO destination looks wrong | Check the LXR-02 kit on the LXR-02; targets are decoded from `modTargets[]` (section 11.3). |

## 16. Maintenance

- **Shared writers.** `lxr2_common.py` imports the Helicase writers,
  `Firmware`, `SLOT_KEY_MAP` and the LXR1 `modTargets` list from
  `../lxr1_converters/lxr1_common.py`. Changes there change both converter
  sets; re-run both test sets (handoff logs) after editing it.
- **Firmware instrument changes** (new or renamed descriptor keys): update
  `SLOT_KEY_MAP` / `HELICASE_ONLY_DEFAULTS` in `lxr1_common.py`; the
  start-up coverage check names the keys involved.
- **PAT4, sceneset, bankset changes**: `build_pat4()` / `verify_pat4()` and
  `write_sceneset()` live in `lxr1_common.py`; `write_bankset()` (masks per
  Scene) is in `lxr2_common.py` and mirrors `storage_formatBanksetLine()`.
- **Helicase table changes** this converter depends on: the roll-rate
  indices (`FLAM_ROLL_INDEX`), the chain tokens (`CHAIN_REPEAT_CONDITIONS`,
  `CHAIN_NEXT_*`, from `MenuText.h` `chainConditionNames`/`chainNextNames`),
  the waveform list (`HELICASE_WAVE_REC`) and the default trigger note
  (`NOTE_OFFSET`, from `MIDI_DEFAULT_TRIGGER_NOTE`).
- **LXR-02 firmware changes**: the enum (`LXR2_PARAM_ENUM_SOURCE`),
  `modTargets` (`LXR2_MOD_TARGETS`) and init kit (`LXR2_INIT_KIT`) are
  copies of the upstream source with import-time assertions. They were
  checked identical to commit `a43cd4f` with `evaluate_c_enum()` and the
  `Cc2Text.c` / `initKit.c` arrays; repeat that check for a new release.
