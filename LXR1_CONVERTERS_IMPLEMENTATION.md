# LXR1 Converters — Task Handoff Log

```
DATE: 2026-10-22
BRANCH: dev-ph6-midi (no firmware change; host tools and documents only)
TASK: Python scripts in tools/lxr1_converters/ that convert legacy LXR1
      (Sonic Potions LXR) SD-card files into Helicase files/directories:
        - .snd kit files        -> Kit/NNN <name>/ directories
        - .pat pattern files    -> PAT4 library Patterns
        - .all and .prf files   -> Bank directories, the same Kit in every
                                   Scene, every Scene's voice edit mask tied
REFERENCES (given by the user):
        - http://sonic-potions.com/public/SdCardImage.zip  (factory content)
        - https://github.com/SonicPotions/LXR               (parameter defs)
COMPLETED:
  - tools/lxr1_converters/lxr1_common.py   shared readers/writers
  - tools/lxr1_converters/snd_to_kit.py     .snd -> Kit
  - tools/lxr1_converters/pat_to_pattern.py .pat -> Pattern/NNN *.pat
  - tools/lxr1_converters/all_to_bank.py    .all/.prf -> Bank
  - tools/lxr1_converters/LXR1_CONVERTERS.md readme, manual, reference
  - this log
VERIFIED (host only):
  - all 30 factory kits convert; instrument values identical to the
    previously verified SD_CARD/Kit/001 Slak conversion
  - synthetic .pat/.all/.prf files with known content decode to the
    intended PAT4 steps, specials, automation, chains and Scene settings
  - every PAT4 written is re-decoded and checked (CRC32C, back-references,
    bitmap, specials, automation) before it is written
NOT VERIFIED:
  - nothing has been loaded on hardware yet (section 9)
  - no real LXR1 .pat/.all/.prf file was available; those paths are tested
    only with synthetic files built from the LXR source layout
```

---

## 1. User instructions received during the task

| When | Instruction | Effect |
|---|---|---|
| Start | Convert `.snd` → kit subdirectories; `.pat` → new `.pat` for the library; `.all`/`.prf` → bank directories with the same kit per Scene and all Scenes' voice edit masks tied together. | Three scripts, section 4. |
| Mid-task | "don't worry about BPM or settings from the legacy files." | The `.all` globals and `.prf` settings block are skipped. |
| Mid-task | "if the .all or .prf has scene settings (audio out etc), those should be converted in the banks." | Values that are Scene settings in Helicase are converted into every `sceneset.scg`: voice `audio_out` and track `midi_note` (from the kit), track `midi_channel` (from the `.all` globals). BPM, `pcr` and other true globals stay skipped. |
| Mid-task | Write this handoff log at the root, `LXR1_CONVERTERS.md` in the converter directory, and document the scripts internally. | Done; module and function docstrings plus comments at each non-obvious rule. |

Standing rule noted: MEMORY.md says not to *offer* migration/converter
options in plans ("No backward compatibility, ever"). This task was an
explicit user request for import tools; it does not change firmware storage
or add compatibility code to the firmware.

## 2. Sources and how they were used

The LXR repository was cloned (full history) into the session scratchpad;
re-clone `https://github.com/SonicPotions/LXR` to repeat any of this.
Master is `dee4968` (2015-10-23).

| Source | Used for |
|---|---|
| `front/LxrAvr/Parameters.h` (master) | Kit byte offsets. The enum is embedded in `lxr1_common.py` and was checked identical to upstream with the same evaluator. |
| `front/LxrAvr/Preset/presetManager.c` | `.snd`, `.pat`, `.all`, `.prf` layouts (`preset_saveDrumset`, `preset_writePatternData`, `preset_saveAll`). |
| `front/LxrAvr/Menu/Cc2Text.c` `modTargets[]` | Kit LFO/velocity target decoding (Nov 2013+ files). Same list as the old `tools/convert_legacy_kits.py` fixture. |
| `mainboard/.../Sequencer/sequencer.c/.h` | Step order (`seq_sendStepInfoToFront`), main-step masks, length/rotate nibbles, pattern change rules, random next, rotation start, shuffle. |
| `mainboard/.../MIDI/frontPanelParser.c` | Step automation numbering (`FRONT_SET_P1_DEST`: values below 128 stored +1). |
| `front/AVR/Menu/menuPages.h`, `menu.h`, `menu.c` at `74f5b23`/`0568533` | The 2013 page-reference target encoding. |
| `front/AVR/Parameters.h` history | The three kit generations (section 3.1). |
| Helicase `Core/DSP/Instruments/*/*Parameters.c` | Key order, morphable rows, target validity, automation IDs (parsed at run time). |
| Helicase `filesystem.c`, `storageTypes.c`, `PatternData.h`, `SceneData.c`, `sequencer.c`, `menu.c`, `MenuText.h` | PAT4 header/blocks, `sceneset.scg` writer order and defaults, `bankset.bcg` format, roll/timing/condition semantics, chain encodings, value-table equality. |
| `FILESYSTEM_SPEC.md`, `PATTERN_DYNAMIC_STACK.md`, `MIDI_SPEC.md` | Output contracts. |

## 3. Findings that shaped the design

### 3.1 Three LXR1 kit generations

Scoring each historical `Parameters.h` against the factory data
(`VOICE_DECIMATIONn` = 127, pans near centre) settled which layout the
factory kits use:

| Generation | Sound bytes | Offsets | Kit targets |
|---|---|---|---|
| V0.1x (≤ `62ac6ef`, May 2013) | 225 | different enum | — |
| V0.2x (`74f5b23` Jul 2013 … `0568533`) | 221 | final | page references |
| `14c016e`/`37a338e` (Nov 2013) → master | 228 | final + 7 MIDI notes | `modTargets[]` |

The factory `P000..P029.SND` are 229 bytes = 8 + 221 → V0.2x. V0.1x kits are
refused (`snd_target_encoding()`), since their offsets differ everywhere.

### 3.2 Page-reference targets (V0.2x)

The first conversion attempt decoded factory LFO targets as `modTargets`
indices and produced nonsense (an LFO on voice 5 aimed at a voice-2
parameter). The V0.2x menu code shows the byte is `(sub-page << 3) | entry`
into `menuPages[voice-1][sub-page]` — velocity on the owning voice, LFO on
`PAR_VOICE_LFOn`. With that decoding the factory LFOs land where expected
(mostly filter frequency on the voice the kit names). `auto` picks the
encoding by payload size; `--target-encoding` overrides it for kits made on
V0.2x and re-saved by later firmware (the LXR1 never converted them).

### 3.3 Upstream comment drift

`Parameters.h` comments say `PAR_BPM /*250*/`, `PAR_BAR_RESET_MODE /*270*/`;
the real values are 251 and 271 (`PAR_TRACK_ROTATION` was inserted later).
The module's self-check caught this; the assertions now pin the real values.

### 3.4 Step automation numbering

The mainboard stores automation destinations below 128 as `PAR + 1` (its CC
numbering); 128+ unchanged. `Firmware.automation_id()` undoes it. 128 can
only be `PAR_FILTER_DRIVE_1` (127 = `PAR_BANK_CHANGE`, never a target).

### 3.5 Sub-steps vs Helicase specials

LXR1 main steps hold 8 sub-steps (1/128). Helicase has micro-timing (0..99 %
of the step, delay `floor(24 × t / 100)` ticks at 1/16) and rolls (rates
`128`=3, `64`=6, `32`=12 ticks; fill % of the step; fill < 100 ducks the voice
at the fill end via `instrumentManager_requestGateDuck()`). Consequences:

- micro-timing `ceil(12.5 k)` lands exactly on sub-step `k`;
- a roll is exact only when hits start at sub-step 0 and evenly cover the
  step (spacing 1/2/4, fill 100);
- a delayed or partial roll either spills extra hits (fill 100) or cuts the
  decay (fill < 100). Default `--partial-rolls first` keeps the first hit
  (with a note); `fill` writes the ducked roll with the exact hit count.

### 3.6 Other mapping decisions

- Values copy 1:1: Helicase menu tables equal the LXR1 ones (checked
  waveform, filter, audio out, LFO wave, sync, retrigger).
- `lfo_polarity` = 0 (`neg`, documented as the original LXR LFO law);
  `lfo_amount_2` 0; `lfo_target_voice_2` `self`; `lfo_target_param_2` off.
- Velocity targets on another voice become off (Helicase picker is
  voice-local); targets `srt`/MIDI note/selector cells become off.
- `[morph]` = `[params]`; Scene track Morph endpoints = Pattern values.
- LXR1 probability → condition buckets of `tools/convert_pattern_specials.py`.
- Shuffle copied 1:1 to all tracks (curves differ; approximate).
- Rotation baked into step order (Helicase has no rotation parameter).
- Chains: `b0N` (N = change bar + 1; nearest of 16/32/64 above 8, noted) and
  `s0P` / `r0N` (LXR1 next 8..14 = `r2..r8`).
- LXR1 track MIDI note (fixed note sent) → Helicase track note root
  (`nte`); 0 → `on`. Closest Helicase meaning, not identical (MIDI_SPEC §2.2).
- Banks always get all eight Scenes so chain targets keep their numbers.
- PAT4 `pattern_change_bar`/`pattern_next` written 0 (retired fields).

## 4. What was built

```
tools/lxr1_converters/
  lxr1_common.py      LXR1 enum/modTargets/V0.2x page tables, readers,
                      Firmware descriptor loader + key-coverage check,
                      Kit/Instrument writer, pattern conversion, PAT4
                      builder + verifier, sceneset/bankset writers,
                      slot allocator, per-file error handling
  snd_to_kit.py       .snd  -> Kit/NNN <name>/
  pat_to_pattern.py   .pat  -> Pattern/NNN <name> <n>.pat (non-empty ones)
  all_to_bank.py      .all/.prf -> Bank/NNN <name>/ (Scenes 00..07)
  LXR1_CONVERTERS.md  manual and reference
```

Behaviour common to all scripts: inputs are files or directories; default
output is this repository's `SD_CARD/<Library>`; free slots from
`--start-slot` (never overwrite) or `--keep-numbers` (+ `--overwrite`);
`--dry-run`; `--firmware-root`; each file converted fully before writing;
bad files reported (`ERROR <file>: ...`) and skipped; exit 1 on any failure.
The full option list and every mapping table are in `LXR1_CONVERTERS.md`.

Firmware coupling: the descriptor tables are parsed at run time, and start-up
fails with the exact keys if the key map and the firmware disagree
(`Firmware._check_key_coverage()`). Parsed counts match FILESYSTEM_SPEC: Drum
39, Snare 38, Cymbal 39, HiHat 39 (5 target-selector rows each).

## 5. Test record (host)

Commands were run from the repository root with output into the session
scratchpad.

| Test | Result |
|---|---|
| `snd_to_kit.py --keep-numbers` on the 30 factory kits | 30 converted, 0 failed; all detected as `pages`. |
| Slak (`P000`) vs existing `SD_CARD/Kit/001 Slak` | All `[params]` values identical; only difference `lfo_target_voice` `0` → `self` (0 was an invalid LXR1 voice). New keys added with neutral values. |
| Factory LFO targets after the page decoding | Consistent with each kit's stored LFO voice (e.g. Rollin, Moch to, Emott). One note: `P023` velocity target pointed at its own `VEL_DEST` menu cell → off. |
| Synthetic `.pat` (plain hits, two automations incl. a ≥128 target, full 1/128 roll, sub-step-3 hit with prob 64 and note 70, flam, 1/32 roll, length 8 + rotation 2, sub-step without main step, unautomatable target, chain data) | Decoded PAT4 as intended: automation IDs 7 (filter_freq) and 9 (filter_drive); timing 38; condition 6; roll 9; roll 5; flam → first hit (default) or roll 9 fill 25 (`fill`); rotation moved step 2 to 0; empty patterns skipped. |
| Synthetic `.all` / `.prf` | 8 Scenes each; tied masks `00ff`; MIDI channels 1..7 from `.all`, `glb` for `.prf`; audio out `2,0,0,0,0,1` from Slak; chains `15/5` (b02 → s03) and `17/21` (b04 → r03). |
| Re-run after refactor | Kit, Pattern and Bank output byte-identical. |
| Occupied slot with `--keep-numbers` | Refused; with `--overwrite` replaced. |
| 233-byte (V0.1x) `.snd` | `ERROR ... V0.1x layout`, other files continue, exit 1. |
| `--dry-run` | Nothing written, notes printed. |

No unit-test file was added; the PAT4 self-verification runs on every write
and the module has import-time assertions for its tables.

## 6. Relation to existing tools

- `tools/convert_legacy_kits.py` (the original factory-kit generator for
  `SD_CARD/Kit`) depends on legacy `PAR_*` names that no longer exist in
  `ParameterArray.h`; it now only upgrades an existing tree. The new
  converter is self-contained and supersedes it for fresh conversion. It was
  not modified or deleted (user's call).
- `tools/populate_scene_directory.py` writes obsolete Scene content (text
  pattern stub, `.fx` v1 placeholder, `midi_note=63`); not used.

## 7. Known limitations

1. Sub-step rhythms other than evenly spaced whole-step rolls are
   approximated (section 3.5). Per-sub-step volumes inside a roll are lost.
2. Shuffle curves differ; values are copied 1:1.
3. LXR1 bar counting follows track 1's length; Helicase chains count
   16-step bars.
4. LXR1 MIDI note semantics (fixed note) map to Helicase note roots.
5. Kits re-saved by newer LXR firmware may need `--target-encoding pages`
   (cannot be detected from the file).
6. No real `.pat`/`.all`/`.prf` sample was available.

## 8. Open questions for the user (no action taken)

- Whether the default `--partial-rolls first` is the right trade-off, or
  the ducked roll (`fill`) is preferred.
- Whether empty LXR1 patterns should still produce Bank Scenes (currently
  yes, to keep chain numbering).
- Whether `tools/convert_legacy_kits.py` should be retired.

## 9. Next steps / hardware checks

1. Convert the factory kits into a card's `Kit/` (or a test slot range) and
   boot; check the Load:Kit list shows them and a few load and sound like
   the existing converted kits (Slak, Rollin, Emott — the latter two have
   LFO targets).
2. Check LFO destinations on the VOICE LFO page (`vo1`/`ds1`), as decoded
   from the factory bytes: Rollin voice 2 and voice 4 → voice 3 filter
   frequency, Rollin voice 1 → voice 3 LFO amount; Moch to voice 1 → voice 2
   filter frequency; Emott voice 5 → voice 4 transient volume.
3. Convert a real LXR1 `.pat` and `.all`; load a Pattern and a Bank; check
   step positions, micro-timing, rolls, automation lanes, track lengths, the
   edit-mask fan-out (edit an Instrument in Scene 00, look at Scene 03) and
   chain behaviour.
4. Record results here or in the next session log.
