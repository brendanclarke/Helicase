# LXR-02 Converters — Task Handoff Log

```
DATE: 2026-10-23
BRANCH: dev-ph6-midi (no firmware change; host tools and documents only)
TASK: Python scripts in tools/lxr2_converters/ that convert Erica Synths
      LXR-02 SD-card content into Helicase files/directories, "the same
      conversion for all the filetypes" as tools/lxr1_converters/:
        - NN-NAME.snd kits       -> Kit/NNN <name>/ directories
        - NN-NAME.pat patterns   -> PAT4 library Patterns
        - projNN/ projects       -> Banks (16 patterns per Bank, A..D)
        - NN-NAME.sng songs      -> Banks whose Scenes play the song
                                    through Scene chains
REFERENCE (given by the user):
        - https://github.com/erica-synths/lxr-desktop-app/ (LXR-02 firmware,
          GPL-3.0; cloned at a43cd4f, 2026-10-23)
COMPLETED:
  - tools/lxr2_converters/lxr2_common.py    LXR-02 readers + translation
  - tools/lxr2_converters/snd_to_kit.py     .snd -> Kit
  - tools/lxr2_converters/pat_to_pattern.py .pat -> Pattern/NNN *.pat
  - tools/lxr2_converters/prj_to_bank.py    projNN/ -> Banks
  - tools/lxr2_converters/sng_to_bank.py    .sng -> chained Banks
  - tools/lxr2_converters/LXR2_CONVERTERS.md readme, manual, reference
  - this log; one MEMORY.md "Volatile Notes" bullet
  - follow-up (same day, user decision): untouched default track MIDI
    notes (48 on all seven tracks) are written as `on`; task closed
VERIFIED (host only):
  - embedded LXR-02 tables identical to upstream a43cd4f: the 306-name
    ParamEnums enum (same evaluator as LXR1), modTargets[] (229 entries),
    the init kit (254 bytes)
  - a synthetic card written by an independent writer (mirroring
    presetManager.c) converts to the intended Kits, PAT4 steps, Banks,
    masks and chains, step by step (section 5)
  - 300 random patterns and kits convert; every PAT4 passes the shared
    verify_pat4(); every step the LXR-02 would play is exactly one trigger
NOT VERIFIED:
  - nothing has been loaded on Helicase hardware (section 9)
  - no real LXR-02 card content was available; every path is tested only
    with synthetic files built from the firmware source layout
  - audible equivalence of LXR-02 and Helicase DSP for the same values
    (section 3.2)
```

---

## 1. User instructions received during the task

| When | Instruction | Effect |
|---|---|---|
| Start | Read MEMORY.md; create converters for the LXR-02 formats like `tools/lxr1_converters/`, from the lxr-desktop-app source, for all file types, in `tools/lxr2_converters/`; detailed documentation like LXR1's; a handoff like `LXR1_CONVERTERS_IMPLEMENTATION.md` at the root. | Four scripts (section 4), the manual, this log. |
| Review | Open questions 2–4 (Bank grouping A..D, cutting songs at 16 Scenes, the shared writers staying in `lxr1_common.py`): "ok". | Kept as built (section 8). |
| Review | Question 1, after the distinction between the step-note pitch reference (63, unchanged) and the LXR-02 per-track MIDI note setting was explained: treat the untouched default (48 on every track) as Helicase's `on`, keep deliberately set notes; then close the task. | `scene_midi_settings()` and the docs changed (section 3.10); task closed. |

No other instructions were given. The LXR1 session's standing decisions
were carried over: BPM and true globals are not converted; values that are
Scene settings in Helicase (audio out, track MIDI channel/note) go into
`sceneset.scg`; MEMORY.md's "no backward compatibility" rule concerns
firmware storage and is untouched (host tools only).

## 2. Sources and how they were used

The repository was cloned into the session scratchpad; re-clone
`https://github.com/erica-synths/lxr-desktop-app` to repeat any of this.
It has five commits, all 2026-10-23; HEAD `a43cd4f`.

| Source | Used for |
|---|---|
| `Core/Inc/Parameters.h` | Kit offsets and globals; embedded as `LXR2_PARAM_ENUM_SOURCE`, checked identical to upstream. `NUM_KIT_SLOTS` 64. |
| `Core/Src/Preset/presetManager.c` | Every file layout: `preset_saveDrumsetSD`/`preset_readDrumsetData` (`.snd`), `preset_writePatternData`/`preset_readPatternData`/`preset_loadPatternSD` (`.pat`, `PatV01..03`), `preset_saveSongSD` (`.sng`), `preset_saveProject`/`preset_loadProject` (project folder, `prj.nfo`, `glo.cfg`), `preset_saveTmpKit`/`preset_saveTmpPat` (`tmp.snd`/`tmp.pat`), `preset_makeFileName*` (`NN-NAME`, `pNNN`), `preset_init` (init-kit copy), `preset_loadDefaultGlobals`. |
| `Core/Inc/Preset/SeqStep.h`, `Core/Inc/Soundengine/Sequencer/sequencer.h` | `Step` (7 bytes), `StepTiming` bitfield, `NUM_STEPS` 64, `NUM_BARS` 4, `MAX_SONG_LENGTH` 64, `SONG_END`/`SONG_LOOP`, `PatternSet`. |
| `Core/Src/Soundengine/Sequencer/sequencer.c` | Playback: main-step bit only (`seq_intIsStepActive` returns 1), probability `rand(1..100) <= prob`, `getShiftValue`, `flamIdxToPattern`, `seq_getShiftFlamPattern`/`seq_processFlam`, length 0 = 16 and the 64 wrap, longest-track pattern advance, song END/LOOP, `seq_setPendingPatternActive` (kit autoload, track restart), `seq_resetNote`/`seq_clearTrack` defaults, `seq_triggerVoice` (automation of the current step). |
| `Core/Src/menu/Cc2Text.c` | `modTargets[]` (embedded as `LXR2_MOD_TARGETS`, checked identical), voice offsets. |
| `Core/Src/menu/menu.c` `parameter_dtypes[]`, `Core/Inc/menu/MenuText.h` | Value domains; table-by-table comparison with Helicase `MenuText.h` (section 3.4); 0-based slot display. |
| `Core/Src/Soundengine/MIDI/CortexParser.c`, `DSPAudio/automationNode.c`, `MIDI/MidiParser.c`, `MidiMessages.h` | Automation destination numbering (+1 below 128), `NO_AUTOMATION` 0xFF, fixed-note MIDI receive. |
| `DSPAudio/Oscillator.c`, `DrumVoice.c` | Note pitch reference `baseNote − SEQ_DEFAULT_NOTE` (60). |
| `Core/Src/Soundengine/FxBus.c`, `Core/Inc/Soundengine/FxBus.h` | FX types, `FX_OFF` pass-through to the FX routing output. |
| `Core/Src/Preset/initKit.c` | Init kit (embedded as `LXR2_INIT_KIT`, checked identical). |
| Helicase `tools/lxr1_converters/lxr1_common.py` | Imported: Helicase writers, `Firmware`, key map, LXR1 `modTargets`. Not modified. |
| Helicase `Core/DSPAudio/Oscillator.h`, `Core/MIDI/MidiPitch.h`, `Core/Menu/MenuText.h`, `Core/Sequencer/sequencer.c` (`seq_chainConditionWeight`, `seq_chainResolveNext`, `seq_chainAtRepetitionEnd`) | Waveform list (5 = crash), `MIDI_DEFAULT_TRIGGER_NOTE` 63, chain tokens and semantics. |
| `FILESYSTEM_SPEC.md` (Bank, Scene, sceneset), `PATTERN_DYNAMIC_STACK.md` (§6 roll table, micro-timing, §6.1 automation, §6.6 pcr/chains), `MIDI_SPEC.md` (§2.2, §4.1, §9), `EFFECTS_BUS_REFERENCE.md` (§1, §4.2a) | Output contracts and behaviour comparisons. |

## 3. Findings that shaped the design

### 3.1 The LXR-02 storage model differs from the LXR1

The LXR-02 has no `.all`/`.prf`. A *project* folder `/projNN` holds 64
kit, 64 pattern and 64 song slots as separate `NN-NAME.ext` files plus
`prj.nfo` and `glo.cfg`; a `.pat` holds **one** pattern of 7 × 64 steps
(4 bars × 16) with no sub-steps; songs are lists of pattern numbers. So
the LXR1 `.all`/`.prf` → Bank conversion became project → Banks, and songs
got their own script. Unsaved edits live in `/tmp`, which repeats slots of
the last project and is therefore read only when named.

### 3.2 Shared ancestry and the LXR1 kit image

Helicase ports LXR 0.37 (MEMORY.md "Project Goal"); the LXR-02 firmware
grew from the same code separately. The evaluated enums agree on every
offset 0..220 (only the NRPN placeholder names differ). The LXR-02 then
leaves 221..227 unused (the MIDI notes moved to the globals) and appends
PWM ×6, the FX block and a morph-kit byte (247 sound bytes). Value domains
match table by table except waveform 5 and audio out 6 (3.4). Hence
`lxr1_kit_image()`: copy 0..220, fix waveforms, re-encode targets, then
run the unchanged LXR1 kit pipeline. Whether the LXR-02's own DSP
revisions make the same value sound different was **not** examined.

### 3.3 Upstream comment drift

`Parameters.h` comments after offset 220 are stale (`PAR_ROLL /*228*/` is
247, `PAR_BPM /*250*/` is 272). The module evaluates the enum and asserts
the real values, as the LXR1 module does.

### 3.4 Value-domain differences

| Item | LXR-02 | Helicase | Decision |
|---|---|---|---|
| Waveform 5 | `PWM` | `Cym` (crash) | → `Rec` (3); pulse width noted, not carried |
| Audio out 6 | `FX` bus | — | → `PAR_FX_ROUTING`; exact with FX off (`FX_OFF` adds the bus to that output unchanged) |
| Step note | pitch = note − 60 | pitch = note − 63 | note + 3 |
| Probability | percent, `rand(1..100) <= p` | conditions r10..r95 | nearest bucket; 100 = always; 0 = never → step left out; 96..99 → r95 |
| Retrigger | off, v1..v6 | + `scn` | unchanged (subset) |
| FX types | `off drv rng cmp del` | `off flt cbt` | not converted (note) |

### 3.5 Flam and shift are exact in Helicase

`flamIdxToPattern()` spreads ×2/×3/×4/×6/×8 hits evenly over 24 ticks:
spacing 12/8/6/4/3 ticks = Helicase rolls `32`/`32t`/`64`/`64t`/`128`
(fill 100). A shift unit is 3 ticks; Helicase micro-timing
`ceil(12.5 u)` gives exactly `3u` ticks (same arithmetic as the LXR1
sub-steps). A negative shift plays the hit `3u` ticks early, i.e. in the
previous step: the converter moves it there with timing `8 − u` when that
step is free, moving whole runs of negative steps together (a fully shifted
track rotates by one step exactly). Only a run blocked by a played step
before it falls back to on-beat hits. Remaining inexactness: a forward
shifted flam whose hits spill into a step with its own hit (the LXR-02
interleaves; a Helicase roll stops at the next trigger) — reported.

### 3.6 Automation is the played step's

`seq_triggerVoice()` applies the automation of `seq_stepIndex` (the step
whose time it is), not of the step that owns the hit. A moved hit therefore
takes the automation stored at its new position, which reproduces the
LXR-02. The LXR-02 applies automation only on triggers, Helicase also on
empty steps, so automation is written only where a hit plays.

### 3.7 Kits, pattern mode and edit masks

With pattern mode `Aut` (default) `seq_setPendingPatternActive()` loads the
pattern's kit number; with `off` the kit stays (kit 00 after project load).
Each Scene embeds that kit; Scenes playing the same kit slot share a VOICE
edit mask, mirroring that editing a kit on the LXR-02 affects every
pattern using it. The groups partition the Scenes, so masks stay mutually
exclusive as Helicase requires.

### 3.8 Songs map onto `xNN` chains

The LXR-02 advances a song at the end of the pattern's longest track; the
Helicase condition `xNN` fires at the end of the Nth repetition (longest
track × scale). Consecutive repeats merge into one Scene (`x01..x08`,
`x16`, `x32`, `x64`; other counts split), `nx1` points to the next Scene,
`stp` ends, `s01` loops. One difference: the LXR-02 restarts all tracks on
every pattern change; Helicase does so on a chain fire only with `pcr` on,
and with `pcr` off counts repetitions on the running master clock, which
moves the change points when Scene lengths differ. The script notes this
per song; `pcr` is a global setting and is not written.

### 3.9 Untouched content

A new project fills every kit slot with the init kit (`preset_init()`
copies only parameters 0..238) and song 00 with `00, end`; a whole card
would otherwise convert hundreds of identical init kits and trivial songs.
Both are skipped by default (`--keep-init`, `--keep-default`). Empty
patterns are skipped as in LXR1 (`--keep-empty`).

### 3.10 Untouched default track MIDI notes (user decision)

The LXR-02 track MIDI note (`glo.cfg` bytes 22..28, `PAR_MIDI_NOTE1..7`)
is a MIDI setting, unrelated to the step-note pitch reference. Set, it
makes the track send that note and answer only to it; a new project sets
48 on all seven tracks (`preset_loadDefaultGlobals()`). Converted
literally, Helicase would give seven tracks root 48 and only track 1 would
receive notes (MIDI_SPEC.md §4.1). When all seven notes are 48, or
`glo.cfg` is missing, `scene_midi_settings()` now writes `midi_note` 0
(`on`) on every track with a note; any other setting is converted as
stored. Channels are always copied. MIDI only; playback and sound are
unaffected.

## 4. What was built

```
tools/lxr2_converters/
  lxr2_common.py      LXR-02 enum/modTargets/init-kit tables + self-checks,
                      input/project discovery, readers (.snd, .pat V1..V3,
                      .sng, glo.cfg, prj.nfo, tmp buffers), kit translation
                      to the LXR1 layout, audio-out/MIDI Scene settings,
                      pattern conversion (flam, shift, probability,
                      automation), song planning and chains, Bank writer
                      (per-kit edit masks); imports the Helicase writers
                      from ../lxr1_converters/lxr1_common.py
  snd_to_kit.py       .snd -> Kit/NNN <name>/
  pat_to_pattern.py   .pat -> Pattern/NNN <name>.pat
  prj_to_bank.py      projNN/ -> Bank/NNN <project> A..D/
  sng_to_bank.py      .sng -> Bank/NNN <song>/ (chained)
  LXR2_CONVERTERS.md  manual and reference
```

Behaviour common to all scripts follows the LXR1 converters: inputs are
files, project folders or a card root; default output is
`SD_CARD/<Library>`; free slots from `--start-slot`, `--keep-numbers` (+
`--overwrite`) for kits and patterns; `--dry-run`; `--firmware-root`;
everything converted and verified before writing; bad files reported
(`ERROR <name>: ...`) and skipped; exit 1 on any failure.

`lxr1_common.py` was **not** modified; the LXR1 converters' output is
unchanged.

## 5. Test record (host)

Commands were run from the repository root with output into the session
scratchpad. The synthetic card generator (`make_lxr2_card.py`) and the PAT4
dump helper (`decode_pat4.py`) stayed in the scratchpad; no test file was
added to the repository (as for LXR1).

| Test | Result |
|---|---|
| Embedded tables vs upstream a43cd4f | Enum (306 names), `modTargets` (229), init kit (254 B): identical. |
| File sizes written by the independent generator | `.snd` 255, PatV03 3,663, PatV02 3,215, PatV01 3,208, `.sng` 72, `glo.cfg` 30, `tmp.snd` 248, `tmp.pat` 3,650: all read. |
| `snd_to_kit.py --keep-numbers` on the card + `tmp.snd` | Untouched init kit skipped; `01-Boom` → `001 Boom`; `tmp.snd` → free slot `000 Tmp 01`. All 191 mapped values (six slots) equal the source bytes; PWM wave → 3; LFO 1 (voice 3 `FILTER_FREQ_3`) → voice 3 token 7; velocity 1 (`VELOD1`) → token 12; PWM and FX targets → 255 with notes; srt and FX notes. Re-run refused the occupied slot. |
| `pat_to_pattern.py` | Planted steps decode as intended: prob 50 → `r50`; flam ×3 → roll 6; shift +2 → timing 25; shift −1 → previous step, timing 88; run of two −2 shifts after a free step → both moved, timing 75; −3 after a played step → on the beat (note); prob 0 left out (note); automation IDs 7 and 9 (drum 1 `filter_freq`, `filter_drive`), FX target dropped (note); note 72 → 75, 125 → 127 (note); flam ×2 + shift +5 → roll 5, timing 63, next hit → cut note; length-4 track all −1 → rotated exactly; lengths 16/12/16/64/16/16/4. PatV02/PatV01 noted; unnamed `p017` → `TestP 17`. |
| `prj_to_bank.py` | Bank A Scenes 00, 05; Bank B Scenes 01 (pattern 17), 04 (pattern 20); `active_scene` 0 / 1; audio out `1,3,0,0,0,0` (voice 1 FX → St2, note); MIDI `10,10,2,3,4,5,10` / `36,0,0,0,0,0,36` with the shared-note note; missing kit 02 → init kit (note). Pattern mode `off` → every Scene `Kit Initkit` (kit 00). `--keep-empty` → 4 Banks × 16 Scenes. No `glo.cfg`/`prj.nfo` → defaults note, folder name. |
| `sng_to_bank.py` | `00 00 00 05 20 20 loop` → 3 Scenes, chains `5/4`, `3/5`, `4/3`, masks `0005`; 9 × 00 + 05 end → `10/4`, `3/5`, `3/43`; 20 alternating entries → 16 Scenes, last `stp`, masks `5555`/`aaaa`; default and empty songs skipped; `--keep-default` converts the default; one missing-kit note per pattern; pcr note when lengths differ. |
| Errors | Truncated pattern (alone, and inside a project → project fails), `PatV04`, `PatVxx`, a folder without slot files: each `ERROR`, batch continues, exit 1. |
| `--dry-run` (all four scripts) | Nothing written. |
| Fuzz: 300 random patterns (V1..V3, half with all steps on) and kits | No exception; every PAT4 verified; triggers = steps that play. |
| Re-run after the final edits | Kit, Pattern and project-Bank output byte-identical; all 31 PAT4 files pass size/magic/CRC32C. |
| Default-note follow-up | Synthetic card (notes `36,0,0,0,0,0,36`): unchanged, shared-note note kept, card Banks byte-identical to the previous run. Copy with all notes 48: `midi_note=0,0,0,0,0,0,0`, channels kept (`10,10,2,3,4,5,10`), default note printed. Project without `glo.cfg`: `midi_channel=1,…`, `midi_note=0,…`. |

## 6. Relation to existing tools

- `tools/lxr1_converters/lxr1_common.py` is a runtime dependency (imported
  through `sys.path`). Editing it changes both converter sets; re-run both
  test records. Moving the Helicase writers into a neutral module would
  remove the cross-folder import (open question 8.4).
- `tools/convert_legacy_kits.py` predates both converter sets and was not
  touched.

## 7. Known limitations

1. PWM (waveform and amount) has no Helicase equivalent; `Rec` is the
   closest.
2. The LXR-02 kit FX (distortion, ring mod, compressor, delay) is not
   converted; voices routed to it play dry on the FX output.
3. A forward-shifted flam that spills into a step with its own hit is cut
   at that hit; a negatively shifted run after a played step plays on the
   beat.
4. Shuffle curves differ; values are copied 1:1.
5. LXR-02 fixed track notes become Helicase note roots (closest meaning,
   as for LXR1). Deliberately set notes that coincide on one channel go to
   the lowest track only; the untouched default (48 everywhere) becomes `on`.
6. Songs: at most 16 Scenes per Bank; `pcr` must be on for songs whose
   patterns differ in length.
7. A project's `/tmp` overlay is not merged automatically.
8. No real LXR-02 content was available; audible DSP equivalence not checked.

## 8. Open questions — resolved 2026-10-23

1. LXR-02 default track MIDI notes (all seven = 48): written as `on`;
   deliberately set notes are kept (section 3.10).
2. Project Banks group patterns 00–15/16–31/32–47/48–63 as A..D: kept.
3. Songs longer than 16 Scenes are cut (the 16th stops): kept.
4. The shared Helicase writers stay in `lxr1_common.py`: kept.
5. Skipping untouched init kits, the default song and empty patterns by
   default: not raised separately; covered by the "ok" and kept.

No questions remain open. The task is closed; what is left is hardware
testing (section 9).

## 9. Next steps / hardware checks

1. Convert a real LXR-02 card (or the factory content, if Erica Synths
   publishes it) with `--dry-run` first; check the notes.
2. Load a converted Kit and compare it by ear with the LXR-02 (filters,
   envelopes, FM and LFO depth), since the LXR-02 DSP was developed
   separately (3.2).
3. Load a converted Pattern: check flams (rolls), shifted steps
   (micro-timing and moved hits), probabilities, automation lanes and track
   lengths up to 64.
4. Load a project Bank: check the Scene-to-pattern numbering, the kit per
   Scene and the edit-mask fan-out (edit a Scene, look at another Scene
   that used the same LXR-02 kit), audio-out routes and MIDI settings.
5. Load a song Bank with `pcr` on: start Scene 00, check repeats, the order
   and the stop/loop at the end.
6. Record the results here or in the next session log.
