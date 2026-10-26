# S084 P1 — Voice Expansions: Plan, Assessment, Risks and Decisions

**Session:** 084, phase P1 (assessment and plan only; no firmware code changed)
**Branch:** `dev-ph6-midi` (S084 P0 roll fix-up landed, hardware check pending)
**Date:** 2026-10-25

**Revisions:**

- **r1:** first plan.
- **r2:** D2, D4, D12, D13; the 303/analog waves; PWM; waveshapers.
- **r3:** naming rule, elastic Effects, 2 runners, never dry, 3 s decay
  limit, the case catalogue, the open-question list.
- **r7:** code ownership and file plan (§5.8), with the new
  `Core/DSP/InsertEffects/` folder; O1–O3 accepted. Implementation is split
  into `S084_P1_IMPLEMENTATION.md`, `S084_P2_VOICE_EXPANSION_2.md` and
  `S084_P3_VOICE_EXPANSION_3.md`.
- **r6:** the card optimiser `tools/sampleoptimize.py` (written and tested,
  §4.1.4). Converter mips are dropped: the firmware plays one level.
  W1–W4 are settled (§6).
- **r5:** wavetables are stored exactly as supplied (no on-device expansion),
  with a host converter and the measured format comparison (§4.1.4); stock
  waves in a smaller oversampled-octave layout (§4.1.2); Q-N1, Q-N3–Q-N5
  accepted.
- **r4:** your answers to Q1–Q34 (§6). New in r4:
  - run mode per voice (`rst fre itp`);
  - `snc`/`rng`;
  - no migration and no retained installs;
  - wavetables from the `SD_CARD/loops/` format;
  - the transient type row;
  - Mix FX names and the `dpt`/`rte` parameters;
  - the deferred-swap review (§3.9).

**Plots and scripts:** [tools/plots/](tools/plots/README.md); references in
`tools/plots/reference/`.

| Figure | Section |
|---|---|
| [current_drive_profiles.png](tools/plots/current_drive_profiles.png) | §3.4, §3.5 |
| [stock_waves_303_analog.png](tools/plots/stock_waves_303_analog.png) | §4.1.2 |
| [pwm_families.png](tools/plots/pwm_families.png) | §4.1.3 |
| [wavetable_formats.png](tools/plots/wavetable_formats.png) | §4.1.4 |
| [waveshapers_static.png](tools/plots/waveshapers_static.png), [waveshapers_stateful.png](tools/plots/waveshapers_stateful.png) | §4.4 |

**Scope:** instrument voices only:

- oscillator type, new stock waves, PWM, wavetables, samples, run mode;
- sync and ring options beside `mix`/`fm`;
- transient types: shaping, PCM/samples, buffer options;
- filter drive types;
- one-shot LFOs and a consistent LFO range;
- the per-voice Mix FX that replaces drive + SRR;
- voice buffer reservations, tail handoff and elastic Effects (CrumpBit
  included);
- the deferred instrument swap, reused by Instrument-Load preview.

**How to read this.**

- §1 summary; §2 rules; §3 how the code works today (§3.9 is the
  deferred-swap review you asked for); §4 the plan per feature.
- §5 shared architecture (§5.8: which file owns what); §6 decisions; §7 risks; §8 order; §9
  verification.
- **§10 open questions: none (r7).**

Cost figures are static estimates until measured with `tools/dsp_test`
(`fpseq.py`) and on hardware.

---

## 1. Summary

| Feature | What it needs | CPU (worst case, per sample) | Memory | Risk |
|---|---|---|---|---|
| Oscillator type `wav/pwm/wtb/smp/slp` | per osc: `typ`, `src`; `wav` meaning depends on type | `wav` 23–38; `pwm` 30–48; `wtb` 45–60; `smp` 28–40 | inside the 1,176 B runtime slot; ITCM +3–5 KB | Medium |
| Run mode `rst/fre/itp` (per voice) | slew on every oscillator; voice-level crossfade using a shared render pool | `fre` +2 per osc; `itp` pool budgeted full (≈ 4 osc renders) | reuse/extend interp scratch | Medium |
| New stock waves `sq3 sw3 sqa tra swa` | generated into sample flash at install, oversampled-octave layout | as `wav` | 61,120 B sample flash | Low–medium |
| PWM `prc psw ptr pq3 pw3 swp` | computed | 30–48 | none | Low |
| Wavetables (`wavetables/`) | stored exactly as supplied (any length, 8/16-bit); card optimiser `tools/sampleoptimize.py` | 45–60 | exactly the files' size | Medium–high |
| Samples: loop, start | Q20.12 phase; `loops/` retired | ≤ today | none | Medium |
| Sync/ring (Drum) | `snc`, `rng` in the `mix/fm` row | +2…+6 | none | Low |
| Transient type row `trs/wav/buf` | type not modulatable; buffer options single-unit | **≤ 22, 0 divisions** | 1 voice unit for `buf` | Medium |
| Filter drive types (14) | `dty` row | ≤ `sft` + 6 | flash +6–12 KB | Medium |
| LFO: one-shots + `rnd` fix | 10 new waves; `rnd` 0..1 | control rate | +8 B per LFO | Low |
| Mix FX (`D+S` … `Rv2`) | `fxt` (not automatable) + `dpt`/`rte`/`amt` | **≤ decimator + distortion share** | 1–2 voice units | **High** |
| Buffer records, leases, tails | records outside the slot, 2 tail runners, overflow (D) or (H) | runners ≈ 1.8 %; drains ≈ 2.5 % if the headroom allows | ≈ 1.2 KB DTCM (approved) | **High** |
| Elastic Effects / CrumpBit | shrink keeping newest audio, grow at wrap, floor 20,608 B | one bounded copy per shrink | +4 B in the existing union | Medium |
| Deferred swap fix | real pre-fader level test, real-time force bound, fast swap on trigger; preview reuses it | ≈ 2 instr/sample per slot (peak tracking) | ≈ 32 B | Medium |

---

## 2. Rules that shape every item

1. **Constant CPU.**
   - No skipping silent, zero or inactive work.
   - The worst-case selection is budgeted.
   - Pools (crossfade renders, runners, drains) are budgeted as always busy.
2. **RAM approval** for new or larger statics, `INDTCM`, ITCM code or stack.
3. **Elastic buffers on both sides:**
   - voices reserve 50 or 100 ms per option, at most 100 ms per voice,
     never at boot as a blanket assignment;
   - every buffer-using Effect shrinks for the voices and grows back.
4. **Tails are never cut; a sound never runs dry** when it should have its
   effect.
5. **Voice effects decay within about 3 s** after the instrument's sound
   ends; no infinite feedback. Only the main Effect bus may.
6. **Descriptor rows are append-only, at most 64 per type** (`slot·64 +
   local`, [InstrumentManager.h:19](Core/DSP/Instruments/InstrumentManager.h#L19)).
7. **Automation values are 7-bit.**
8. **No compatibility provisions** (Q12, Q7, user):
   - firmware changes do not try to keep old files sounding the same;
   - sample installs are invalid after the flash until the next install.
   - Bit-identical **host** proofs remain only where they guard code that
     must not change (the untouched filter type `sft`, CrumpBit at a
     constant share, StereoFilter).
9. **Foreground only** for InstrumentManager, FxBuffer, the modulation graph
   and swaps. **No VLAs.** DSP files are `-Ofast`.
10. **Descriptive names only**; never product, brand or designer names.
    **Effect and option names are capitalised** (`D+S`, `Fld`, `Res`).
11. **No new tools in the firmware test path.**

---

## 3. How it works today (assessment)

### 3.1 Oscillator

- **Waveform values:** `SINE 0` … `CRASH 5`, then user samples from 6
  ([Oscillator.h:49-55](Core/DSPAudio/Oscillator.h#L49-L55)).
- **Tables:** 11 band-limited octaves × 1,024 samples each. `sawTable`
  falls; `recTable` is high first; `triTable` starts at −1.
- **Waveform interpolation pool:** 2 oscillators per block
  ([config.h:881](config.h#L881),
  [Oscillator.c:211](Core/DSPAudio/Oscillator.c#L211)).
- **The `wav` modulation range is global**
  ([InstrumentManager.c:721](Core/DSP/Instruments/InstrumentManager.c#L721)).
- **User samples use `phase >> 17`**
  ([Oscillator.c:813](Core/DSPAudio/Oscillator.c#L813)). So only 0.74 s is
  addressable; longer samples restart.
- **Install:** loops come from `/loops`
  ([filesystem.c:24804-24809](Core/Hardware/SD/filesystem.c#L24804-L24809)).
  At most 120 samples; 5,040 B of SRAM1 caches.
- **Phase reset on trigger:**
  - every engine resets its **main** oscillator;
  - Cymbal also resets its modulators;
  - Drum's modulator, Snare's noise oscillator and HiHat's modulators run
    free;
  - Drum also clears its filter
    ([DrumVoice.c:196](Core/DSP/Instruments/Drum/DrumVoice.c#L196));
  - the amp envelope's attack continues from its current value
    (`slopeEg2_trigger()` does not zero it outside repeat mode).

### 3.2 FM / mix

- Drum `mixOscs`: 0 `fm`, 1 `mix`
  ([DrumVoice.c:316](Core/DSP/Instruments/Drum/DrumVoice.c#L316)).
- Cymbal and HiHat are FM only.

### 3.3 Transient

- Values 0..13: `Snp Ofs` + 12 stock 8-bit clicks of 50 ms
  ([transientGenerator.c:50-80](Core/DSPAudio/transientGenerator.c#L50-L80)).
- Playback ≈ 22 instructions, 0 divisions.
- Placement: Drum pre-filter; others post-filter.
- Stock peaks ≈ −0.1 dBFS.

### 3.4 Filter drive ([current_drive_profiles.png](tools/plots/current_drive_profiles.png) A, C, D, F)

- `0.4 + 6·(v/127)²` ([ResonantFilter.c:115](Core/DSPAudio/ResonantFilter.c#L115)).
- Padé clip ≈ `2·tanh(u/2)` ([ResonantFilter.c:252](Core/DSPAudio/ResonantFilter.c#L252)).
- LP is soft at every setting; the other types hard-clip above about
  `drv 49`.
- `ResonantFilter` is also the StereoFilter Effect state, so new state stays
  out of it.

### 3.5 Voice drive and SRR (figure B, E, F)

- `(1+k)x/(1+k|x|)` with up to ×255 small-signal gain, in the engine
  post-chain ([distortion.h:68](Core/DSPAudio/distortion.h#L68)).
- SRR in the mixer ([mixer.c:150](Core/DSPAudio/mixer.c#L150),
  [mixer.c:966](Core/DSPAudio/mixer.c#L966)).

### 3.6 LFO

- Eight waves ([MenuText.h:98](Core/Menu/MenuText.h#L98),
  [lfo.c:66](Core/DSPAudio/lfo.c#L66)).
- `rnd` spans −1..1 while the rest span 0..1, so it misbehaves against the
  polarity options.

### 3.7 Buffers and CrumpBit

- Arena 126,592 B; voice units 4,416 B (50 ms), 12 positions
  ([FxBuffer.h:95-100](Core/DSP/Effects/FxBuffer.h#L95-L100)).
- CrumpBit is fixed at 70,592 B. `crumpBit_seat()` mutes the loop on any
  length change.
- `fxbuf_handoff_t` has unit owner, rate and offset fields, but no voice
  records, leases or tails.

### 3.8 Your current wavetables (`SD_CARD/loops/`)

- 104 files `AKWF_hvoice_0001.wav` … `_0104.wav`, zero-padded.
- Every file: mono, 16-bit, 44,100 Hz, **600 samples** (one cycle), with
  `fmt`, `data`, `smpl` and `acid` chunks.
- They install today as 104 looped "samples".
- **Verdict:** the firmware accepts this format directly and stores it as is
  (124,800 B). On this set the same sound fits in 26–53 KB after the host
  optimiser (§4.1.4).

### 3.9 The deferred instrument swap (review requested in Q28)

**What it does.**

- A Scene switch arms `preset_startDrumsetApply()`. A per-track Scene change
  arms `preset_startSingleVoiceApply()`.
- The foreground workers (`preset_tickDrumsetApply()`,
  `preset_tickSingleVoiceApply()`, run from `menu_pollPresetStatus()` once
  per main-loop pass) commit a slot when
  `instrumentManager_ampEnvelopeQuiet()` is true.
- A trigger on a pending slot force-commits it first
  (`preset_applyDeferredSceneSlotForTrigger()`).

**What it measures.**

- [InstrumentManager.c:1386](Core/DSP/Instruments/InstrumentManager.c#L1386)
  treats the slot as quiet when the amp envelope is stopped or its **value**
  is ≤ 0.0001 (`INSTRUMENT_AMP_EG_QUIET_THRESHOLD`).
- That is **pre-fader and pre-volume** (it does not look post-fader), but it
  is **not the voice's level**:
  - **Drive after the envelope is ignored.** The voice drive follows the amp
    envelope with up to +48 dB small-signal gain. At envelope 0.0001 with
    `drv 127`, the voice can still be at about **−32 dBFS**. At `drv 64`
    (×3) about −70 dBFS. So a driven voice is swapped while clearly audible.
  - **The envelope value is not the signal.** An envelope at 0.0001 on a
    quiet oscillator is far below audibility, and on a hot one with
    resonance it is not. The threshold has no relation to the actual output.

**When the force path fires — likely too early.**

- [presetManager.c:2188](Core/Bank/Scene/Preset/presetManager.c#L2188):
  after `DRUMSET_APPLY_FORCE_TICKS` = 1,000 non-quiet passes, the worker
  force-commits the ringing voice **without a trigger**.
- The comment calls this "a one-second bound … 1,000 main-tick passes". But
  the worker runs once per **main-loop pass**
  ([main.c:1500](main.c#L1500)), which is not paced at 1 kHz.
  - An idle pass (render queue full) is a few tens of microseconds.
  - So 1,000 passes is likely **tens of milliseconds, not one second**.
  - Any voice still decaying when the Scene changes is then cut mid-decay.
    That matches your suspicion that the swap is early.
- This needs confirming by measurement: count passes against TIM2 in a DEV
  build. The code reading strongly suggests it.

**The fix** is part of this plan (§5.7) and is reused by Instrument-Load
preview.

---

## 4. Feature plans

### 4.1 Oscillator type

#### 4.1.1 Parameter model

| Row | Scope | Values | Morph / LFO / automation |
|---|---|---|---|
| `oscN_type` `typ` | per oscillator | `wav`, `pwm`, `wtb`, `smp`, `slp` | not Morphable, not LFO; automatable |
| `oscN_source` `src` | per oscillator | `pwm`: family; `wtb`: wavetable; `smp`/`slp`: sample | not Morphable, not LFO; automatable 0..127 |
| `oscN_wave` `wav` (existing) | per oscillator | `wav`: stock list; others: 0..127 control (width, frame position, start) | Morphable, LFO, automation |
| `run` | **per voice** (all oscillators) | `rst`, `fre`, `itp` (§4.1.6) | not Morphable, not LFO |

- **Rows:** Drum 2 osc × 2 + 1 = **+5 → 44**; Snare +3 → 41 (tonal osc
  `typ`/`src`, plus `run`); Cymbal 3 × 2 + 1 = **+7 → 46**; HiHat
  **+7 → 46**.
- **No migration** (Q12): old kits load with whatever their bytes mean now.

#### 4.1.2 `wav`: new stock waves ([stock_waves_303_analog.png](tools/plots/stock_waves_303_analog.png))

- **List:** `Sin Tri Saw Rec Noi Cym sq3 sw3 sqa tra swa`.
- `sq3`/`sw3` are digitised from your 303 captures; `tra` from the dirty
  triangle; `sqa`/`swa` are analog models.
- **Recipe** (k = 1..511):
  [stock_wave_harmonics.csv](tools/plots/stock_wave_harmonics.csv).
- **Generation:** the tables are generated at Load:[Samples] into the stock
  area of sample flash. They are computed additively from the recipe and
  programmed as they are computed, with no scratch RAM. Until the first
  install, they fall back to `Rec`/`Saw`/`Tri`.
- **Layout (r5, W4):** 11 octave levels, as today's stock tables, but each
  level only as long as it needs. The length is the power of two ≥ 8 × its
  harmonic count, at most 1,024.
  - **12,224 B per wave, 61,120 B for all five** (today's full-length
    layout would be 112,640 B).
  - Measured with the same method (`plot_wavetable_formats.py --stock`): the
    sound is the same at every note. Brightness is unchanged; aliasing and
    images are −55…−59 dB up to 1,760 Hz and −51 dB at 3,520 Hz, against
    −55 dB for the full layout.
  - A plain half-length pyramid would be smaller (4,064 B) but lets
    interpolation images rise to −25 dB at 3.5 kHz, so it is not used.
- **Fitted for now** (Q6). The waveform concerns are recorded in
  `SCOPING_TARGETS.md` §7.3 for the future 303 voice.

#### 4.1.3 `pwm` ([pwm_families.png](tools/plots/pwm_families.png))

| `src` | Family | Method | Est. (FM) |
|---|---|---|---|
| `prc` | default square | saw difference, exact band-limit | 30 (45) |
| `psw` | default saw | sum of two saws, exact band-limit | 30 (45) |
| `ptr` | default triangle | phase warp, mip-compensated | 30 (42) |
| `pq3` | 303 square | phase warp of `sq3` | 30 (42) |
| `pw3` | 303 saw | phase warp of `sw3` | 30 (42) |
| `swp` | sweep | sine → tri → saw → square → pulse | 32 (48) |

#### 4.1.4 `wtb`: wavetables (r5: store exactly what the user supplies)

**Measured on your example set:**
[wavetable_formats.png](tools/plots/wavetable_formats.png), from
`tools/plots/plot_wavetable_formats.py`. Each format is played the way the
oscillator would play it, at 44,108 Hz with linear interpolation, and
measured for two things:

- **alias + images:** energy off the note's harmonics, in dB, lower is
  cleaner;
- **brightness:** harmonic energy against the ideal band-limited note, in
  dB, 0 = nothing lost.

| Format (104 frames) | Flash | Alias+images 110 / 440 / 1,760 / 3,520 Hz | Brightness 110 / 880 Hz | Notes |
|---|---:|---|---|---|
| **raw** (the files as they are: 600 pt, 16-bit) | 124,800 B | −57 / −40 / −18 / −11 dB | 0.0 / 0.0 dB | reference |
| 256 pt, 16-bit | 53,248 B | −57 / −41 / −19 / −11 | −0.1 / 0.0 | indistinguishable from raw on this set; 2.3× smaller |
| 128 pt, 16-bit | 26,624 B | −57 / −40 / −19 / −11 | −0.2 / −0.1 | 4.7× smaller; −0.2 dB at low notes |
| 256 pt, 8-bit | 26,624 B | −57 / −38 / −19 / −11 | 0.0 / 0.0 | table SNR 45 dB (periodic, so heard as faint extra harmonics, not hiss) |
| 256 pt + mip pyramid | 103,168 B | −57 / −41 / −29 / −20 | −0.1 / −2.6 | cleaner high notes, duller mid-high notes, 2× the flash of 256 pt |
| 128 pt + mip pyramid | 49,920 B | −57 / −41 / −29 / −20 | −0.2 / −2.6 | as above |

What the numbers show:

- **The r4 mip proposal was wrong for this.** It expanded your set
  (1,536 B per frame, more than the raw 1,200 B) and dulled mid-high notes.
- **Your frames need far fewer than 600 points.** They carry 97 % or more of
  their energy in the first 63 harmonics, worst frame; 99.7 % in the first
  127. So 256 points sounds identical and 128 nearly so.
  - That is a property of *this* set. A bright set (e.g. raw saws) would
    lose real harmonics at 128 points.
  - So the choice must be per set, made by the user.
- **Aliasing at high notes** comes from playing any table above about
  440 Hz without band-limiting. Mips reduce it by about 9–11 dB at 1.7–3.5
  kHz, but cost flash and some brightness. Again a user trade-off.
- **Thinning frames does not work for this set.** Neighbouring `hvoice`
  frames are unrelated cycles, so interpolating across a dropped frame
  misses it entirely (median SNR about 2 dB when keeping 26 of 104). For a
  smooth morph table, thinning would save a lot.

**Model (W1):**

1. **The firmware never expands a wavetable.** It stores each frame exactly
   as supplied: the files' own length and bit depth, 8 or 16-bit. All
   frames of a folder must share length and depth; files that do not match
   the first are skipped and reported. There is no resampling, mip
   generation, DFT or scratch RAM on the device.
2. **Any frame length plays at the same cost.** One 32 × 32 → 64-bit
   multiply of the phase by the frame length gives both the integer index
   and the interpolation fraction, so 600 points costs the same as 256.
   8-bit frames are a sign-extending load.
3. **No mips.** The oscillator plays the one stored level. (r6: no tool
   produces mips, so the firmware does not read them; one block-rate level
   select can be added later if a mip-writing tool ever exists.)
4. **The flash a wavetable takes is the user's decision**, made with the
   optimiser (below). The `SD_CARD/loops/` set installs as-is at 124,800 B,
   or at 26–53 KB after converting with no audible loss.

**Card layout.**

- Root `wavetables/`; each subfolder is one wavetable, named by the folder
  (single-parameter view), short name `w01`….
- Frames are numbered by natural, numeric-aware sort of their filenames.
- Up to 128 frames, mapped onto `wav` 0..127 with interpolation.
- A wavetable that does not fit in the remaining sample flash is skipped
  whole and reported.

**The card optimiser: [tools/sampleoptimize.py](tools/sampleoptimize.py)**
(host, written and tested in r6).

- **Run it on a card folder:** `python3 tools/sampleoptimize.py [CARD]`.
  `CARD` defaults to `SD_CARD/`. Add `--dry-run` to see the report only. It
  needs numpy.
- **Backup:** `wavetables/` → `wavetables_old/` and `samples/` →
  `samples_old/`. An existing `_old` folder is kept and used as the source,
  so re-running re-optimises from the originals.
- **Wavetables** (every folder under `wavetables/`, recursively): each table
  is resampled to the smallest power-of-two length that **plays the same**.
  - The test plays candidate and original the way the oscillator does
    (linear interpolation, 16× finer phase grid).
  - Thresholds: no frame differs by more than −20 dB, and the median frame
    by at most −40 dB (`--worst-db`, `--typical-db`).
  - It never writes a larger table. The one exception is a table with mixed
    frame lengths, which is unified at its longest length because the
    firmware needs one length per table.
  - File names, and therefore frame order, are kept.
  - If band-limiting would clip, the whole table drops by one common factor
    (reported).
- **Samples:**
  - the tail more than 80 dB under the peak is trimmed (the onset never
    moves);
  - stereo, non-16-bit or non-44.1 kHz files are converted to mono
    16-bit 44.1 kHz so they install at all;
  - anything else is copied byte-for-byte.
- **Tested on a scratch copy of the card:**
  - **your `loops` set as `wavetables/hvoice`: 600 → 256 points, 124,800 B →
    53,248 B**; worst frame −23 dB, median −45 dB; one common level change of
    −0.14 dB to avoid clipping;
  - a bright saw table is kept at full length;
  - a minimal 64-point table is unchanged;
  - a stereo 24-bit 48 kHz sample is converted, with 0.5 s of silent tail
    trimmed;
  - your `01_cut.wav` is unchanged.
- Out of scope, as you said "that should be enough": 8-bit, frame thinning,
  mips and manual trade-offs. `tools/plots/plot_wavetable_formats.py` still
  shows those trade-offs for any folder.

**DSP:** two frame reads (always both) + blend, with the position ramped;
≈ 45 (FM 60), +2 for the length multiply.

#### 4.1.5 `smp` / `slp`: samples

- `loops/` is retired. Loop is chosen per oscillator; start position on
  `wav`.
- Samples carry their name, shown in the single-parameter view, as today.
- **Q20.12 sample phase:** every installable sample plays fully.
- **No retained installs** (Q7):
  - after the firmware flash, samples and loops are invalid (catalogue v2
    rejects the old index) until the next Load:[Samples];
  - that install is the first that can include wavetables;
  - nothing reads the old loop flag.
- **Up to 233 samples**, so that the transient `wav` list (12 stock
  transients + samples + 11 stock waves) stays within 256 options (§4.3).
  The SRAM1 caches are dropped and the flash catalogue is read directly
  (Q8).

#### 4.1.6 Run mode (per voice)

| Value | Behaviour | Cost |
|---|---|---|
| `rst` | today's trigger behaviour: phases reset, Drum's filter cleared | +0 |
| `fre` | **no oscillator resets.** **Every** oscillator of the voice slews its phase increment from the old to the new value over about 3 ms (main, modulators, noise). The filter keeps its state. | +2 per oscillator |
| `itp` | at trigger the **whole oscillator section** (every oscillator plus the FM/mix/sync/ring combination) keeps rendering in its old state beside the new, reset state; the two crossfade over about 3 ms. The filter keeps its state. | one extra oscillator-section render while busy |

- **Render pool for `itp` and waveform interpolation.** One shared pool
  measured in oscillator renders per block. `itp` takes one unit per
  oscillator: Drum and Snare 2, Cymbal and HiHat 3.
- **Priority:**
  1. `itp` claims first, lowest voice number first;
  2. waveform interpolation (the global `osc_wave_interp` on `wav`-type
     oscillators) takes whatever remains.
  - `wtb` frames never use the pool; they always render both frames.
- **Pool size: 4 units** (Q-N1). Budgeted full: about 4 × 50 = 200
  instructions, against today's 2-unit pool of about 100.
- **No free units:** that trigger behaves as `fre`. That is smooth, with no
  click, but no crossfade.
- **Fast parameter slew when state is kept** (your Q4 question, answered:
  **yes**).
  - With `fre`/`itp` the filter keeps ringing through the retrigger. Any
    parameter that jumps at that moment would step the running filter:
    velocity-modulated cutoff, a new step's automation, a Morph change.
  - Today these steps are per block (0.73 ms), which on a ringing,
    resonant filter can tick.
  - In `fre`/`itp` the filter cutoff coefficient and resonance get a
    one-pole slew of about 2 ms (three blocks). Control rate, no per-sample
    cost.
  - The amp envelope already continues from its current value and ramps
    per sample, so it needs nothing.
  - `rst` keeps today's behaviour exactly.

### 4.2 Sync and ring (Drum only)

- **The `mix`/`fm` row becomes four options:** `fm`, `mix`, `snc`, `rng`.
  - **`snc`:** the modulator is the master and the main oscillator the
    slave, reset on each master wrap (with the overshoot scaled). `amt`
    mixes slave and master. +4–6 instructions.
  - **`rng`:** main × modulator; `amt` is the wet amount. +2.
- **Cymbal and HiHat stay FM-only.**

### 4.3 Transient

#### 4.3.1 Transient type row (new, not modulatable, not automatable)

| `trt` | Content of the existing `wav` row | Automatable within the type |
|---|---|---|
| `trs` | non-PCM shaping: `Snp`, `Ofs` (room for future shaping modes) | yes |
| `wav` | the 12 stock PCM transients, then installed samples (up to 233), then the 11 stock oscillator waves: up to 256 options; automation reaches the first 128 | yes |
| `buf` | the single-buffer options (§4.3.3) | yes (every option is one unit, so the reservation never changes size) |

- Automation and modulation can **never** move between a buffer and a
  non-buffer option, because the type is fixed.
- **Rows:** +1 per instrument.
- **Sample transients:** one-shot at Q20.12, pitch via `frq`, ≈ 22
  instructions, peak-normalised by the catalogue gain byte (applied
  everywhere, Q15).
- **Stock oscillator waves as transients (Q14, accepted):** added to the
  `wav` list after the samples, as pitched 50 ms bursts with a fixed decay.
  - Placing them after the samples keeps the sample positions stable.
  - With the 256-entry cap, the sample maximum becomes 256 − 12 stock
    transients − 11 stock waves = **233**.

#### 4.3.2 Buffer options (`buf`; one unit each; cap ≤ 22, 0 divisions; decay ≤ 3 s)

| Option | Placement | Behaviour | Est. |
|---|---|---|---|
| `Res` cavity resonator | pre | damped feedback comb, tuned by `frq` (about 20 Hz–2 kHz), excited by the oscillators | 16–18 |
| `Plk` pluck | pre | string model excited by the stock `Clk` burst | 19–21 |
| `Tub` tube | pre | negative-feedback comb: odd harmonics | 16–18 |
| `Flm` flam | post | one delayed copy, 5–50 ms | 10–12 |
| `Bzz` stutter | post | loops the first N ms, decaying | 14–16 |
| `Rvs` reverse | post | records the first 50 ms, plays it reversed | 12–14 |
| `Spr` spring | post | long allpass with feedback (50 ms) | 14–16 |

All 100 ms transient options are removed.

### 4.4 Filter drive types ([waveshapers_static.png](tools/plots/waveshapers_static.png), [waveshapers_stateful.png](tools/plots/waveshapers_stateful.png))

- **Row:** `dty`. `drv` stays the input gain.
- **Types:** `sft rct art thf fdc bsw tsw lsw lsq sfd afd asf chb rsb`, as in
  the r3 table: functions, state, cost (≤ `sft` + 6, two divisions) and
  level trims.
- **Voice-only.** Shaper state lives in the engine struct.

| Value | Function | Level trim |
|---|---|---|
| `sft` | today's Padé soft clip | 0 dB |
| `rct` | rectifying (octave up), DC-blocked | +1.3 dB |
| `art` | alternate rectifying (octave down), DC-blocked | +2.0 dB |
| `thf` | threshold fold (repeating) | +4.5 dB |
| `fdc` | fifth-down crunch, DC-blocked | +2.7 dB |
| `bsw` | bit swap (nibbles) | +5.4 dB |
| `tsw` | tracking saw | 0 dB |
| `lsw` | low tracking saw (octave down) | 0 dB |
| `lsq` | low tracking square (octave down) | −2.5 dB |
| `sfd` | sine fold | +4.8 dB |
| `afd` | accelerating fold | +5.3 dB |
| `asf` | asymmetric fold | +3.4 dB |
| `chb` | Chebyshev 3rd | +3.3 dB |
| `rsb` | ring-sub | 0 dB |

### 4.5 LFO

- **Append:** `si1 tr1 su1 sd1 sq1 rn1 xu1 xd1 xtr xt1`.
  - One-shots run one cycle per retrigger, then hold.
  - `ofs` = start delay; `sq1` is inverted.
  - With retrigger `off`, a one-shot fires once at Scene activation.
- **`rnd` fix (Q17):** `rnd` and `rn1` produce 0..1 like every other wave,
  so the negative/positive/bipolar polarity options treat all waves the
  same. The `lfo.c` noise path will use the masked `GetRngValue() & 0x7FFF`
  divided by 32767.
  Existing sounds that relied on −1..1 noise change (accepted).

### 4.6 Voice Mix FX (replaces drive + SRR)

**Placement.**

- At the very end of each engine render, after the amp envelope and
  velocity.
- SRR moves out of the mixer into this stage.
- The FX send taps its output. Mono.

**Parameters.**

| Row | Short | Role | Automatable / LFO / Morph |
|---|---|---|---|
| `mix_fx_type` | `fxt` | type | **no** (not automatable, not modulatable) |
| `instrument_drive` (existing cell) | `dpt` | Depth: the "amount" control of the type | yes |
| `instrument_decimation` (existing cell) | `rte` | Rate: the rate-, frequency-, pitch- or time-linked control | yes |
| `mix_fx_amount` (new) | `amt` | wet / third control | yes |

**Types.** Capitalised names. All but `D+S` get ×1.25 input. Every feedback
or decay control obeys the 3 s limit.

| Type | `dpt` | `rte` | `amt` | Units | Est. |
|---|---|---|---|---:|---|
| `D+S` | drive | sample rate | — | — | the cap |
| `Fld` | fold depth | sample rate | symmetry | — | ≈ 26 |
| `Crs` | bit depth | sample rate | wet | — | ≈ 22 |
| `Rng` | depth (AM → ring) | carrier frequency | wet | — | ≈ 15 |
| `Pha` | depth/feedback | rate | wet | — | ≈ 25 |
| `Fla` | feedback | rate | wet | 1 | ≈ 24 |
| `Cho` | depth | rate | wet | 1 | ≈ 22 |
| `Ech` | feedback | time ≤ 50 ms | wet | 1 | ≈ 14 |
| `Ec2` | feedback | time ≤ 100 ms | wet | 2 | ≈ 14 |
| `Cmb` | feedback | pitch | wet | 1 | ≈ 14 |
| `Rev` (small room) | decay | damping | wet | 1 | ≈ 30 at half rate |
| `Rv2` (lo-fi room) | decay | damping | wet | 2 | ≈ 30 at half rate |

- `Rng`, `Pha`, `Fla`, `Cho` and `Cmb` are swapped as you asked, so `rte` is
  the rate, frequency or pitch control.
- I applied the same rule to `Ech`/`Ec2`: `rte` = time, `dpt` = feedback
  (Q-N4).
- The `2` names (`Ec2`, `Rv2`) mark the double-buffer types.

---

## 5. Shared architecture

### 5.1 Type-dependent modulation range for `wav`

`MOD_WAVE` resolves per slot and oscillator: `wav` type → 10, others → 127.
A type write rebinds the LFO adapters.

### 5.2 Type-dependent labels and value text

A per-instrument cell formatter hook (slot, row, value) provides, for
example:

- `sq3`, `w47%`, `f12` or `st34` for `wav`;
- `w01` with the folder name in the single-parameter view;
- sample names;
- the Mix FX labels.

### 5.3 Voice buffers, leases and tails

#### 5.3.1 Reservations

- **Size:** transient `buf` options reserve 1 unit (50 ms). Mix FX options
  reserve 1 or 2 units (`Ec2`, `Rv2`).
- **At most 2 units per voice.** The menus hide what does not fit:
  - with a transient `buf` option active, the Mix FX list hides `Ec2` and
    `Rv2`;
  - with `Ec2` or `Rv2` active, the transient type list hides `buf`.
- **Live writes:** neither type row is automatable, so live requests that
  would not fit cannot happen.
- **Load conflicts** (hand-edited or converted files): the Mix option
  downgrades to its 1-unit sibling (`Ec2` → `Ech`, `Rv2` → `Rev`). Nothing
  goes dry.
- **When:** only when an option is applied to a slot. That includes the
  restored Scene's own apply. Nothing is pre-assigned at boot.

#### 5.3.2 The elastic pool

- **Pool:** regular units (≤ 12) plus tail leases (≤ 12; one slot's
  outgoing units each). Worst case 24 units.
- **Effect floor 20,608 B.** Effects shrink and grow at block boundaries,
  without muting.
- FxBuffer coalesces one apply pass into one share change. The Effect is
  told before the voices write.

#### 5.3.3 Records (approved, DTCM)

- One record per slot per stage, outside the runtime-slot union. Fields:
  - `use_id`;
  - units and positions;
  - `dsp_state`;
  - `param_now`/`param_target`;
  - flags;
  - energy and silence tracking;
  - owner.
- 12 active + 2 runner records ≈ 1,120 B, plus ≈ 64 B of lease
  bookkeeping, in **DTCM `.dtcmz`** (Q21 approved). The arena shrinks by
  the same, rounded to 32 B.

#### 5.3.4 Decay limit

- **Feedback ceiling** per block: `g_max(d) = 10^(−d)` (d in seconds),
  which gives −60 dB within 3 s. The feedback control is scaled to
  0..`g_max(d)`.
- **Reverbs:** RT60 ≤ 3 s.
- **Expiry:** below −80 dBFS for 50 ms, or 3.5 s after the input stopped,
  with a 20 ms residue fade.

#### 5.3.5 Changes and tail runners

1. **Same `use_id`:** keep the buffer, ramp parameters, continue. A
   delay-time change **glides** (tape-style, Q23).
2. **Different use:** the old record becomes a tail on a lease; the slot
   reserves fresh units and its new option starts at once.
3. **Tail runners: 2** (≈ 1.8 %).
4. **Overflow:** drain lanes (D, ≈ 2.5 %) if step 0 shows the headroom,
   otherwise hold (H). Accepted recommendation.
5. Re-adoption of a returning tail (Q26) and separate tails rather than
   chained (Q25), both accepted.

#### 5.3.6 Change-case catalogue (updated for r4)

| # | Case | Behaviour |
|---|---|---|
| C1 | same option, same parameters | continue |
| C2 | same option, different parameters | continue; parameters ramp; delay time glides |
| C3 | Mix `Ech` → `Ec2` | same use: the second unit is reserved and the loop extends at the next wrap |
| C4 | Mix `Ec2` → `Ech` | different use: the 100 ms tail plays out on a lease; `Ech` starts fresh |
| C5 | different buffer options | separate tails; the new option starts at once |
| C6 | buffer → non-buffer | the tail plays out; the new option runs at once |
| C7 | non-buffer → buffer | fresh reservation; one share change |
| C8 | instrument type changes, option stays | same use: continue |
| C9 | A → B → A within the tail | the returning slot re-adopts its tail |
| C10 | automation | Mix `fxt` and transient `trt` are not automatable. Within transient `buf`, automating between options changes use at the trigger (single unit, same size): C5. |
| C11 | both stages change on one slot | two tails; overflow per §5.3.5 |
| C12 | Instrument-Load preview | the same deferred swap as a Scene or track change (§5.7): immediate if the slot is below the quiet threshold, fast swap on the next retrigger otherwise |
| C13 | load conflict | `Ec2`/`Rv2` downgrade to `Ech`/`Rev` |
| C14 | voices grow | CrumpBit shrinks keeping its newest audio (crossfaded read position, Rate display at the reachable maximum) |
| C15 | voices release | CrumpBit grows at its next wrap |
| C16 | Morph | parameters only (type rows are not Morphable) |

### 5.4 Elastic Effects and CrumpBit

**Effect contract.**

- `buffer_min_bytes` ≤ the 20,608 B floor.
- Shrinks keep the newest audio; grows may be lazy.
- A registry self-check enforces this.

**CrumpBit.**

1. min 20,608 B, preferred 70,592 B;
2. shrink by compacting the newest audio (≤ 0.16 ms copy, once per
   coalesced change);
3. grow at the write wrap;
4. on a shrink, crossfade the read position over one block (Q31);
5. sync divisions limited to the current loop;
6. the Rate row is `BUFFER_DEPENDENT` with `effective_max` (Q32);
7. `pending_length` (+4 B inside the 76 B union);
8. a host test of shrink/grow sequences, plus a hardware check.

### 5.5 Descriptor budget

| Instrument | Today | Osc (`typ`/`src` × osc + `run`) | Transient `trt` | `dty` | `fxt` + `amt` | Total (cap 64) |
|---|---:|---:|---:|---:|---:|---:|
| Drum | 39 | +5 | +1 | +1 | +2 | **48** |
| Snare | 38 | +3 | +1 | +1 | +2 | **45** |
| Cymbal | 39 | +7 | +1 | +1 | +2 | **50** |
| HiHat | 39 | +7 | +1 | +1 | +2 | **50** |

### 5.6 Memory and flash ledger

| Item | Region | Bytes | Approval |
|---|---|---|---|
| Engine struct growth | existing runtime-slot union | 0 new | none |
| Voice buffer records + lease bookkeeping | DTCM `.dtcmz` | ≈ 1,184 B | **approved (Q21)** |
| Render pool: 4 oscillator-render units (today 2 × 64 B scratch) | DTCM `.dtcmz` | +128 B (2 more 64 B scratch blocks) | **yes** |
| Swap level tracking: per-slot block peak and quiet counters (§5.7) | SRAM1 or DTCM | ≈ 32 B | **yes** |
| CrumpBit `pending_length` | existing Effect union | 0 new | none |
| Oscillator render code | ITCM | +3–5 KB | **yes** |
| Sample catalogue caches | SRAM1 | −5,040 B | — |
| Install-time scratch | none (wavetables are copied as supplied; stock waves are programmed as computed) | 0 | — |
| Flash total | program flash (144,112 B free) | ≈ 50–60 KB | report |
| Stock tables (oversampled-octave layout) | sample flash | 61,120 B | none |

### 5.7 Deferred instrument swap: fix and reuse

This applies to Scene changes, per-track Scene changes and Instrument-Load
preview. It replaces the behaviour described in §3.9.

1. **Measure the real voice level, pre-fader.**
   - Each slot keeps the peak of its rendered block before volume, fader
     and pan:
     - at the Mix FX stage **output** for non-buffer types (so `D+S` drive
       is included);
     - at the stage **input** for buffer types (their tails are carried by
       the handoff and must not hold the swap back).
   - Cost: about 2 instructions per sample per slot, folded into the
     existing post-chain loop.
2. **Quiet** means the block peak has stayed below **−72 dBFS for 8
   consecutive blocks** (about 6 ms) (Q-N3). That replaces the envelope
   test. It is a much lower level than today's effective swap point with
   drive, and it is the real signal.
3. **The force bound is real time, not passes.**
   - It is measured against a millisecond timebase: proposed 2 s.
   - The pass counter goes.
   - Before the bound, only a quiet result or a trigger commits.
4. **Fast swap on retrigger.**
   - A trigger on a pending slot commits at once, as today.
   - The new voice's first block subtracts a decaying copy of the old
     voice's last output sample (a declick offset decaying over about 1 ms).
   - So the swap never clicks, and the note is not delayed.
   - The same declick serves the real-time force bound.
5. **Reuse:**
   - Instrument-Load preview (each scroll step) arms the same single-voice
     deferred apply: immediate if quiet, fast swap on the next retrigger;
   - the preview's own load and scroll stay asynchronous (scrolling
     policy);
   - the Scene worker, the per-track worker and preview share one quiet
     test and one commit path.
6. **Buffers:** voice buffer records live outside the slot, so a swap never
   affects a ringing Mix FX or transient tail (§5.3).

---

### 5.8 Code ownership and file plan (r7)

#### 5.8.1 Layering rules

| Layer | Folder | Owns | May include | Must not |
|---|---|---|---|---|
| Building blocks | `Core/DSPAudio/` | engine-agnostic DSP: oscillators, filter, shapers, envelopes, delay-line primitives, the mixer | other DSPAudio headers, `config.h`, `SampleRom` read APIs | know about slots, descriptors, Scenes, FxBuffer or Menu |
| Instruments | `Core/DSP/Instruments/` | engines, descriptors, runtime slots, special writers, mod domains, the cell formatter, slot level and quiet state | DSPAudio, InsertEffects manager API, SampleRom catalogue | call FxBuffer directly |
| Insert effects (new) | `Core/DSP/InsertEffects/` | per-voice Mix FX types, transient buffer types, the voice buffer records, leases, tails and runners | DSPAudio, FxBuffer (only from `VoiceBuffer.c`) | include InstrumentManager, Menu or Scene data (the manager is handed slot and parameter values) |
| Bus effects | `Core/DSP/Effects/` | the Effect bus types, EffectsManager, FxBuffer (the arena, the elastic share, lease units) | DSPAudio | know voice record contents (it sees units and leases only) |
| Sample flash | `Core/SampleRom/` | the sample-flash layout and catalogue v2, install parsing, stock-wave generation | `sampleFlash` | touch the SD card (filesystem hands it data) |

**Rules:**

- Every file under `Core/DSPAudio/` and `Core/DSP/InsertEffects/` is render
  code and builds with `-Ofast`. Render functions never allocate, never
  call into Menu or storage, and never run in an ISR.
- Control entry points (apply, select, reserve, expire) are foreground only,
  as today.

#### 5.8.2 `Core/DSP/InsertEffects/` (new, as you asked)

One file per Mix FX type.

| File | Owns |
|---|---|
| `InsertEffect.h` | The type contract shared by Mix FX and transient buffer types:<br>- the ops `init` / `retarget` / `process` / `tail_process`;<br>- units (0/1/2);<br>- placement (pre/post, for transients);<br>- the three display labels;<br>- the 3-character name;<br>- the state budget (`INSERT_STATE_BYTES`, asserted per type). |
| `InsertEffectsManager.c/h` | The Mix FX registry and per-slot stage:<br>- type select/apply;<br>- the "hide what does not fit" queries for the menus;<br>- label lookup for the cell formatter;<br>- the ×1.25 input stage;<br>- the per-block render call made by the engines;<br>- the transient `buf` registry and render call. |
| `VoiceBuffer.c/h` | The per-slot, per-stage records (approved DTCM, §5.3.3) and their whole lifecycle:<br>- `use_id`, reserve, lease, re-adopt;<br>- the 2 tail runners and drains/hold (§5.3.5);<br>- expiry and the decay limit's −80 dBFS tracking;<br>- `beginPass`/`endPass` around an apply pass (one coalesced share change);<br>- the DEV owner dump.<br>**The only caller** of FxBuffer's voice and lease APIs. |
| `InsertDriveSrr.c` | `D+S`: the voice distortion curve (from `distortion.h`, unchanged) and the sample-and-hold decimator moved out of `mixer.c`. Must stay bit-identical to today's chain (host test). |
| `InsertFold.c` | `Fld` |
| `InsertCrush.c` | `Crs` |
| `InsertRing.c` | `Rng` |
| `InsertPhaser.c` | `Pha` |
| `InsertFlanger.c` | `Fla` |
| `InsertChorus.c` | `Cho` |
| `InsertEcho.c` | `Ech` and `Ec2`: one algorithm, two registry rows (1 and 2 units) (O1) |
| `InsertComb.c` | `Cmb` |
| `InsertRoom.c` | `Rev` and `Rv2` (O1) |
| `Transient/TransientResonator.c` | `Res` (O2 for the folder) |
| `Transient/TransientPluck.c` | `Plk` |
| `Transient/TransientTube.c` | `Tub` |
| `Transient/TransientFlam.c` | `Flm` |
| `Transient/TransientStutter.c` | `Bzz` |
| `Transient/TransientReverse.c` | `Rvs` |
| `Transient/TransientSpring.c` | `Spr` |

- **Self-contained types:** each type file defines its own state struct
  inside the record's `dsp_state` bytes (`_Static_assert` against
  `INSERT_STATE_BYTES`), its labels and its ops table. It exports one
  `const insert_effect_type_t`, declared in the manager header.
- No per-type `.h` is needed. Bus Effects have one because their descriptor
  tables are per type; insert types use the instrument's `dpt`/`rte`/`amt`
  rows.

#### 5.8.3 New files in `Core/DSPAudio/`

| File | Owns | Notes |
|---|---|---|
| `OscillatorPwm.c/h` | the `pwm` families `prc psw ptr pq3 pw3 swp`, the phase warp, mip compensation | render in ITCM |
| `OscillatorWavetable.c/h` | the `wtb` reader: any frame length (one 32 × 32 → 64 multiply), 8/16-bit, two-frame blend, position ramp | ITCM |
| `OscillatorSample.c/h` | `smp`/`slp`: Q20.12 phase, start position, loop wrap, FM sample read. The existing `calcUserSample*` code moves here from `Oscillator.c` | ITCM |
| `OscSection.c/h` | the per-voice oscillator section:<br>- the `fm`/`mix`/`snc`/`rng` combinations;<br>- run modes `rst`/`fre`/`itp`: the slew on every oscillator, the section crossfade;<br>- the filter-parameter slew request in `fre`/`itp`. | the engines call this instead of composing oscillators themselves |
| `OscRenderPool.c/h` | the shared 4-unit render pool:<br>- `itp` first, lowest voice first, then waveform interpolation;<br>- per-block arbitration;<br>- the scratch buffers (`osc_interp_*`, +2 blocks). | replaces the active-count logic now in `modulationNode.c` |
| `StockWaves.c/h` | read access to `sq3 sw3 sqa tra swa` in sample flash: oversampled-octave level offsets and lengths, level select, fallback to `Rec`/`Saw`/`Tri` | generation is in SampleRom |
| `DriveShaper.h` (+ `DriveShaper.c`) | the 14 filter drive types `g(u)` as `static inline` (they run inside the ZDF loop), their state struct, level trims; the `.c` holds block-rate helpers (tracker `1/period`) | `ResonantFilter.c` voice loop calls it; the float twin stays type 0 |
| `DelayLine.h` | `static inline` primitives for buffer DSP: compare-and-subtract ring wrap, interpolated tap, write, one-pole damping, allpass, `delay_feedbackCeiling()` (the 3 s rule, §5.3.4) | shared by every buffer type; no state of its own |

**Existing `Core/DSPAudio/` files that change (same owners):**

- **`Oscillator.c/h`:** `OscInfo` gains type, source and slew state.
  Dispatch by type. The stock-wave table pointer. Shrinks as the sample
  code moves out.
- **`ResonantFilter.c/h`:** the voice ZDF takes a shaper type and its state.
  Type 0 is unchanged; the float twin is unchanged.
- **`transientGenerator.c/h`:**
  - the `trs` and `wav` types: stock PCM, samples at Q20.12 with the
    catalogue gain, and stock-wave bursts;
  - it dispatches `buf` to InsertEffects.
- **`lfo.c/h`:** one-shot waves, delay state, the `rnd` fix.
- **`mixer.c`:** the decimator is removed (now `InsertDriveSrr.c`).
- **`modulationNode.c`:**
  - the waveform-interpolation slot counting moves to `OscRenderPool.c`;
  - the `wav` domain becomes type-dependent (with InstrumentManager).
- **`distortion.c/h`:** unchanged; `D+S` reuses the curve.

#### 5.8.4 Other folders

| Folder | New files | Changed files (owner keeps them) |
|---|---|---|
| `Core/DSP/Instruments/` | `InstrumentCellFormat.c/h`: the cell formatter hook (§5.2). Keeps `InstrumentManager.c` (3,654 lines) from growing. | `InstrumentManager.c/h`:<br>- special writers for the new rows, with `drv`/`srt` re-pointed to `dpt`/`rte`;<br>- the type-dependent domain;<br>- per-slot block peak, quiet state and swap declick (§5.7). |
| | | `*/…Parameters.c`: the new rows and menu cells. |
| | | `*/…Voice.c`: the engines use `OscSection`, the transient stage at pre/post, the shaped filter, and the insert stage. |
| `Core/DSP/Effects/` | — | `FxBuffer.c/h`: lease units, the 20,608 B floor, coalesced share changes, invariants.<br>`EffectsManager.c/h`: the elastic contract and the `min ≤ floor` self-check.<br>`CrumpBit/CrumpBitEffect.c`, `CrumpBitParameters.c`: §5.4. |
| `Core/SampleRom/` | `SampleInstall.c/h`: parse every accepted WAV form into frames or samples; build catalogue v2 (names, gains, wavetables, stock block); natural-order install.<br>`StockWaveGen.c/h`: additive generation of the stock tables, programmed as computed.<br>`StockWaveRecipes.c`: the recipe as flash constants, generated from `tools/plots/stock_wave_harmonics.csv`. | `SampleMemory.c/h`: catalogue v2 read API; the RAM caches go.<br>`sampleFlash.c/h`: unchanged guards. |
| `Core/Hardware/SD/` | — | `filesystem.c`: walks `samples/` and `wavetables/` (folders, natural order) and hands file data to `SampleInstall`. It owns the SD side only; the `/loops` pass goes.<br>`storageTypes.c/h`: `storage_naturalCompare()` (no asyncfatfs calls, per its rule). |
| `Core/Bank/Scene/Preset/` | — | `presetManager.c`: the swap workers use the real level, the real-time bound and the declick (§5.7); the single-voice path is reused by preview; apply passes are wrapped in `VoiceBuffer` `beginPass`/`endPass`. |
| `Core/Menu/` | — | `menu.c`: cells through the formatter hook; option hiding; preview through the shared swap.<br>`MenuText.h`: the simple enum lists (osc types, run modes, `fm/mix/snc/rng`, transient types, `dty`, LFO waves). Insert-type names live in their type files (O3). |
| `tools/` | `gen_stock_recipes.py` (CSV → `StockWaveRecipes.c`); `dsp_test` cases for each insert type, the shapers, CrumpBit compaction, swap level, the pool | `sampleoptimize.py` and `tools/plots/` already exist |

#### 5.8.5 Build

- **Include paths:** `-ICore/DSP/InsertEffects` and
  `-ICore/DSP/InsertEffects/Transient`.
- **`-Ofast` pattern rules:** add rules for `Core/DSP/InsertEffects/%.c`
  and `Core/DSP/InsertEffects/Transient/%.c`. This avoids one explicit rule
  per file, which is how voices and bus Effects are built today.
  - `VoiceBuffer.c` and the manager compile under the same rule.
  - They do no float work that depends on IEEE strictness, so that is
    harmless.
- **New `Core/DSPAudio/` files** are covered by the existing pattern rule
  once listed in `DSP_SRCS`.
- **Specifications** gain the new owners in the close-out:
  `MODULE_INTERCHANGE_SPEC.md` (call map), `INSTRUMENTS_DSP_REFERENCE.md`,
  `EFFECTS_MIXER_DSP_REFERENCE.md`, `STORAGE_SRAM_MANIFEST.md`.

---

## 6. Decisions (user, 2026-10-25)

**Earlier rounds:**

- D2 stock waves generated at install;
- D4 `wavetables/`;
- D12 elastic reservations, never at boot, and Effects shrink for voices;
- D13 tails never cut, records, 2 runners, never dry;
- the PWM set; the drive types; descriptive naming; the 3 s decay limit;
  the plots.

**r4 answers:**

| Q | Decision |
|---|---|
| Q3 | Run modes are `rst`, `fre`, `itp`, per voice. `rst` is today's behaviour (no separate legacy option). `fre` slews every oscillator. `itp` crossfades the whole oscillator section. `itp` has priority over waveform interpolation, lowest voice first. |
| Q4 | `rst` keeps today's filter behaviour; `fre`/`itp` keep the filter state. Fast parameter slew in `fre`/`itp`: yes (§4.1.6). |
| Q5 | `snc` (not `syn`) and `rng` join `fm`/`mix` on Drum. Cymbal and HiHat stay FM-only. |
| Q6 | Fitted waves for now. Concerns are recorded in `SCOPING_TARGETS.md` §7.3 for a future 303 voice. |
| Q7 | Nothing retained: installs are invalid after the flash until the next install, which is the first that can include wavetables. |
| Q10 | `SD_CARD/loops/` (AKWF, 600-sample single cycles) is the reference format. The installer accepts any series of single-cycle files that fits (§4.1.4). |
| Q11 | A wavetable is named by its folder (shown in the single-parameter view; short name `w01`…). Frames have numbers only, in the alphanumeric order of their files. Samples keep their names. |
| Q12 | No migration or compatibility provisions. |
| Q17 | `rnd` fixed to 0..1; all LFO waves behave the same under the polarity options. Fire at Scene activation. |
| Q18 | A = `dpt` (Depth), B = `rte` (Rate), with `rte` the rate/frequency/pitch-linked control. Swapped for `Rng Pha Fla Cho Cmb`. |
| Q21 | Approved: the records in DTCM. |
| Q23 | Delay-time changes glide. |
| Q27 | The Mix FX type is not automatable or modulatable; only `dpt`/`rte`/`amt` are. The transient gets a non-modulatable type row (`trs`/`wav`/`buf`), so automation never crosses between buffer and non-buffer options. Transient buffer options are single-unit (`Res` replaces `Rs1`; the 100 ms variants are gone; `Spr` is 50 ms). Mix FX: the small room is `Rev`, the lo-fi room `Rv2`. Options are capitalised. |
| Q28 | Preview uses the same deferred swap as Scene and track changes (immediate if quiet, fast swap on retrigger), with shared code. The swap is re-examined and fixed (§3.9, §5.7). |
| Other Qs | Recommendations accepted: Q1, Q2, Q8, Q9, Q13–Q16, Q19, Q20, Q22, Q24–Q26, Q29–Q34. |

**r5 answers:**

| Q | Decision |
|---|---|
| Q-N1 | Render pool of 4 oscillator-render units. |
| Q-N2 | Rejected: the firmware must not expand user wavetables. Storage is the user's choice; only change formats where sound is better **and** storage smaller. A host converter is the right direction (§4.1.4). |

**r6 (user "ok", plus the optimiser request):**

| Item | Decision |
|---|---|
| W1 | The firmware stores wavetables exactly as supplied (any length, 8/16-bit) and plays any length at the same cost. |
| W2 | Optimisation happens on the host: `tools/sampleoptimize.py` shrinks wavetables to the smallest length that plays the same, trims silent sample tails, makes non-conforming samples installable, and keeps `_old` backups. |
| W3 | Moot: no mips are produced, and the firmware reads a single level. |
| W4 | The stock waves use the oversampled-octave layout (61,120 B). |

**r7 (user "ok"):**

| Item | Decision |
|---|---|
| O1 | Variant pairs share one file per algorithm: `InsertEcho.c` (`Ech`, `Ec2`), `InsertRoom.c` (`Rev`, `Rv2`). |
| O2 | Transient buffer types live in `Core/DSP/InsertEffects/Transient/`, one file per type. |
| O3 | Insert types carry their own names and labels in their type files; simple enum lists stay in `MenuText.h`. |
| Phasing | P1 = foundations: measurement, LFO, the deferred-swap fix, the oscillator section and types (`wav`/`pwm`/`smp`/`slp`), run modes, sync/ring. P2 = sample flash, wavetables, stock waves, transient types `trs`/`wav`, drive types. P3 = elastic Effects and CrumpBit, voice buffers and tails, insert effects, transient `buf`. |
| Q-N3 | Swap when the real pre-fader peak is below −72 dBFS for 8 blocks; 2 s real-time bound; declick. |
| Q-N4 | `Ech`/`Ec2`: `rte` = time, `dpt` = feedback. |
| Q-N5 | Natural sort for frames, wavetable folders and samples. |

---

## 7. Risks

| # | Risk | Likelihood / impact | Mitigation |
|---|---|---|---|
| R1 | Worst-case CPU: heavier oscillators, the 4-unit render pool, stages at caps, runners, drains | Medium / high | Step 0 baseline; caps; measure after each step |
| R2 | ITCM growth | Low / medium | Report |
| R3 | Flash headroom | Medium / medium | `link_budget.py` per step |
| R4 | The 64-row cap (≤ 50 used) | Low | — |
| R5 | Every install is invalidated by the update; kits reference samples and wavetables by index | Certain / medium | Clear messaging at boot and on Load:[Samples] |
| R6 | Buffer machinery complexity | Medium / high | One owner; invariants; DEV owner dump; a host test per case |
| R7 | CrumpBit's delay range shrinks while voices hold buffers | Certain / low–medium | D12; Rate display |
| R8 | **The swap fix changes Scene-change timing:** voices swap later (when really quiet) or at their next trigger. A voice that never gets quiet and never retriggers (drone, repeat envelope) waits for the 2 s real-time bound. | Medium / medium | Declick on the force path; DEV pass/time logging to confirm §3.9 first |
| R9 | Moving SRR into the engines re-routes `srt`/`rte` bindings | Medium / medium | Test LFO/velocity on `rte` |
| R10 | Per-voice reverb quality | Medium / low | Half-rate lo-fi; clean reverb on the bus |
| R11 | Aliasing from shapers, sync, warped PWM | High / low | Pre-filter placement; mip compensation |
| R12 | UI: type-dependent cells, hidden options | Medium / medium | Formatter hook |
| R13 | Users store large unoptimised wavetables and run out of sample flash | Medium / low–medium | `tools/sampleoptimize.py` (plays the same, never larger); the installer reports skipped tables |
| R14 | Documentation drift | Medium / low | Specs updated in the same change |

---

## 8. Order of work

0. **Measure:**
   - worst-case `cpu`/underruns;
   - `fpseq.py` caps;
   - **the deferred-swap pass rate** (DEV build: passes per second, force
     events) to confirm §3.9.
1. **Quick wins:** LFO one-shots + `rnd` fix; `snc`/`rng`; Q20.12 sample
   phase.
2. **Deferred-swap fix (§5.7):** real level, quiet rule, real-time bound,
   fast-swap declick. This step comes early because it changes how every
   later feature swaps.
3. **Parameter groundwork:** osc rows, per-voice `run` row, transient type
   row, domain and formatter hooks, `smp`/`slp`, `prc psw ptr swp`.
4. **Elastic Effects and CrumpBit (§5.4)**, FxBuffer leases and coalescing.
5. **Catalogue v2 and install:**
   - stock waves (oversampled-octave layout); sample names and gains; up to
     233 samples;
   - `wavetables/` stored as supplied, natural-sort frames, folder names;
     `wtb` with any frame length, single level;
   - `tools/sampleoptimize.py` checked against the installer (its output
     installs and plays as reported);
   - `sq3 sw3 sqa tra swa`; `pq3 pw3`;
   - stock waves as transient options.
6. **Run modes:** `rst`/`fre`/`itp`, the render pool with priorities,
   parameter slew.
7. **Buffer machinery (§5.3)**, proved with `Ech` through every case.
8. **Transients:** `trs`/`wav`/`buf` types; buffer options.
9. **Mix FX:** SRR into the engines; types one by one.
10. **Filter drive types** (can move earlier).
11. **Instrument-Load preview on the shared swap path** (C12). It can land
    with step 2 if convenient.
12. **Close-out:** specifications, converter and optimiser notes. `SCOPING_TARGETS.md` is
    already updated for Q6.

---

## 9. Verification

- **Host proofs:**
  - `sft` unchanged;
  - CrumpBit unchanged at a constant share, and keeps its newest audio
    across shrinks and grows;
  - StereoFilter unchanged;
  - shapers match `plot_waveshapers.py`;
  - generated tables match the recipe CSV;
  - Q20.12 plays long samples fully;
  - decay limit: −60 dB within 3 s for every buffer type at every extreme.
- **Caps (`fpseq.py`):**
  - transients ≤ stock (0 `vdiv`);
  - Mix FX ≤ decimator + distortion share;
  - drive types ≤ `sft` + 6;
  - pool, runners and drains at budget.
- **Swap:**
  - a DEV build logs each swap's level and reason (quiet / trigger /
    bound);
  - no swap above −72 dBFS except by trigger or the bound;
  - no clicks at fast swaps;
  - preview behaves the same.
- **Buffer machinery:** a host test per case C1–C16.
- **Install:**
  - your `loops/` set copied to `wavetables/hvoice/` installs as one
    104-frame wavetable named `hvoice`, frames in file order, taking exactly
    124,800 B;
  - the same set converted to 256 and 128 points installs at 53,248 B and
    26,624 B and sounds as the report says;
  - samples show their names;
  - a reboot keeps everything;
  - the first boot after the flash shows the installs invalid until a new
    install.
- **Hardware:**
  - worst-case Scene;
  - listening per type;
  - Scene switches with ringing tails while CrumpBit runs: nothing cut,
    nothing dry, no early swap.

---

## 10. Open questions

None. O1–O3 were accepted in r7 (§6). The implementation is split into
three phases:

- **P1** — `S084_P1_IMPLEMENTATION.md`: the full schedule.
- **P2** — `S084_P2_VOICE_EXPANSION_2.md`: plan.
- **P3** — `S084_P3_VOICE_EXPANSION_3.md`: plan.

Questions found while implementing are raised in those documents.
