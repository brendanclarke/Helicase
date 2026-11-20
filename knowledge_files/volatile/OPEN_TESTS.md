# Open Tests and Hardware Verification Items

Extracted from MEMORY.md during the S087 refactor. These are tests that should
be performed when the opportunity arises, not blocking items for current work.

---

## Hardware soak / confirmation

- **S093 P4/P6/P8 Alkaline (TB-303) device trial: PASSED** —
  hardware-tested 2026-11-10. A1–A13 all confirmed: boot with init.alk,
  Instrument Load listing, S092 pattern reference comparison, make-up level,
  CPU worst case (four Basic slots, Res full, drive ×16, accents/slides;
  widget ≤ Drum; 0 underruns over 5 min), save/load/AutoSave cycle,
  Morph/LFO/automation on every row, gate timing, external clock, wavetable
  playback, attack/decay/resonance/drive/pos parameter ranges,
  self-oscillation bounded. Tracks 6/7 share slot 5 confirmed; note-off
  does not end the gate; roll hits get full gates.
- **S094 P1–P4 voice DSP, SRAM D-cache, budget, PWM, Alkaline post-FX:
  PASSED** — hardware-tested 2026-11-12. Crash removal (card sample
  plays via `slp`), PWM on all four drum voices (pos law: 0 = 50 %,
  127 = narrowest), Alkaline VCA overdrive + tone (bit-identical at
  Overdrive 0 / Tone 127 — P9-D7), Alkaline PWM and Tri wave, LFO sync
  and rate table, SRAM D-cache fix (−14.8 % on same Scene, six Alkalines
  no longer pin CPU), QADD (bit-exact), CrumpBit SVF restored on grn/rvb
  (180.8 per frame ≤ 181 cap), cycle budget probe, PWM position law,
  memory allocation speed-ups (copy/clear scratch, AFATFS 16 sectors,
  write-back stack). 0 underruns over extended playback.
- **S090 P5 Load/Save speed and watchdog T1–T9 (HIGH PRIORITY):**
  - T1: Load page type cycling (Kit→Scene→Bank→Pattern) — names appear
    faster than before; trace `w` lines give each index-read duration
  - T2: Save page, same
  - T3: Nested Instrument Load type steps — faster than before
  - T4: Scene Save to a new slot — completes, OR within ~12 s the error
    overlay shows `W...` code. **Write down the code.** Then wait 10 s, power off
  - T5: T4 ×3, plus a save over an existing slot — each completes or ends
    with a `W...` code; never a permanent `...`
  - T6: Kit, Bank, Pattern Save — complete or end with a code
  - T7: Listen — no audio dropouts (CPU widget should not change during
    Load/Save)
  - T8: Same tests with playback stopped — not slower than before
  - T9: Leave a page during a save — no change in behaviour
  - **Bring back:** the `W...` code(s) and the SD card (`asavetrc.bin`);
    the `w` records and site-12 snapshot decide the C3-3 root-cause fix
- **S091 P4 CrumpBit H1–H9: PASSED** — hardware-tested 2026-11-06, all
  sub-types and grn checks confirmed.
- **S091 P5 CrumpBit efficiencies HW1–HW16: PASSED** — hardware-tested
  2026-11-06, all efficiency changes and follow-ups (pitch ±36 st, fine pitch,
  window glide) confirmed.
- **S091 P6 CrumpBit parameter cleanup: PASSED** — hardware-tested
  2026-11-06. Labels hook display, dly delay time curve, dpn→spn rename,
  snc removed from dly lanes, pit interval labels, grn below /64 rows, tap
  fade, all confirmed. Final CPU: dly 157.6, cmb 153.8, pit 154.1, grn 165.3,
  rvb 163.9 (worst 165.3 ≤ 170 cap). **Note:** grn/rvb SVF restored in
  S094 P2 §23; grn now 180.8, cap raised to 181.
- **S095 P1–P4 Space effects, Colloid, compressor, StereoFilter removal
  (open — needs device test):**
  - Space effects: boot, select Delay/Reverb/Echo, play through each;
    type-change feed-through (switch Delay→Reverb while tail is sounding);
    drain into OFF (tail fades to silence); slot-lock interplay with `itp`
  - Colloid: boot with `init.col`; Instrument Load listing; all 8 pages;
    12 oscillators audible; pair-loop pitch tracking; PWM warp forms; filter
    sweep; ADSR with sustain switch; mid/side stereo; CPU worst case (6
    Colloid slots, Basic budget)
  - Compressor retune: clean kit at `cam` 48/64/96 — deeper reduction than
    before; C5 live GR readout on compressor page; filter drive floor
    (every filtered voice +8 dB at `drv` 0); half makeup on voice
    distortion (distortion no longer the only way into compressor range)
  - StereoFilter removal: confirm type list shows OFF/CrumpBit/Delay/
    Reverb/Echo (5 total); old `flt` files on card parse as OFF
  - Sine table: all existing wavetable/LFO/oscillator reads sound correct
    after 4096→1024 reduction
  - CrumpBit `rvb` damping: `toneK[ton >> 1]` across full row (126/127
    both flat)
  - Flash: 720,084 B; ITCM: 15,232 / 16,384 B
- **S096 P1–P3 Tetrakt, Colloid extensions, MIDI note modes
  (open — needs device test):**
  - Tetrakt: boot with `init.tkt`; Instrument Load listing; all 8 pages;
    four layers audible; layer switch `lon` stops; fused sample read; space
    stage S1 delay (FxBuffer voice unit claim); W2 allpass width; R1
    fade-then-start retrigger; CPU worst case (Basic budget); pan/send per
    layer; SRR and tone per layer
  - Colloid gate C1: real note gate based on step length / sustain / Scene
    change; verify gate off silences, gate on sustains
  - Colloid pitch envelope C2a: ±24 semitone sweep, ADSR shape, pam
    resolution (0.38 st per step)
  - Colloid noise C2b: hardware RNG source, one-pole HP + TPT SVF, tone/
    width/pan shaping; verify `nos` type appears in oscillator type selector
  - Colloid wavetable scan C2c: `wts` type, 6-frame interpolated scan;
    verify scan glides while note plays
  - Colloid filter: UBP and off types; filter envelope range widened
  - Colloid oscillator counts: 10 for wav/wtb, 6 for pwm/wts/nos
  - MidiNoteModes: Tetrakt layer4 mode (four MIDI notes per track);
    Colloid paraphonic mode on own channel; normal mode on other types
  - Channel pressure Learn: pressure/note/velocity sources, delta/absolute
    modes
  - Instrument fixes P2: Delay/Echo linear interpolation (hermite → lerp),
    Colloid filter envelope range, Tetrakt spatializer ranges, Colloid PWM
    gliding fix, compressor sidechain duck easing
  - Flash: 743,016 B (headroom 10,648 B); ITCM: 15,976 / 16,384 B
  - IM-OVL: overlay interaction with Tetrakt layer triggers
  - LR-VAL: LiveRecord validation for Tetrakt four-note triggers
  - CO-NOISE: Colloid noise spectra verification (host test gap)
- **S097 P2/P3 program flash refactor, filesystem fixes
  (open — needs device test):**
  - Flash budget: 714,980 B (headroom 38,684 B, above 16 KiB warning);
    with FS_OS=1: 678,740 B (headroom 74,924 B)
  - ParamDescriptor compaction: verify all instrument pages display
    correct short/long/category labels via pool accessors
  - menuStaticPages / menu_pageRow(): page navigation unchanged
  - OS_SRCS / COLD_LAST: boot, play, save/load — no regression from
    link-order change; LTO jitter on DSP loops within ±1 instruction
  - Bank Save mask fix (§13): perform a Bank Save and verify the
    correct Scenes are written (masks now set after `filesystem_start()`)
  - Boot notice non-blocking overlay: boot with `DEV_MODE_LOGGING 1`
    and verify the boot notice draws while background tasks run;
    background budget now 30 µs/ms (3 % while playing)
  - Scene Load apply: load a Scene and verify patterns realign
    (`preset_startLoadApply(scene_mask)` +
    `filesystem_patternRealignPlayingTracks(scene_mask)`)
  - Held OK replay: hold OK during a save, verify it retries on
    completion (`menu_loadSaveOkRetry`)
  - DEV trace stages `'f'` and `'k'`: enable and confirm records appear
    in `asavetrc.bin`
- **S098 P1/P2 Scene Switch Corrections (open — needs device test):**
  - P1 matrix (`S098_P0_SCENE_SWITCH_CORRECTIONS.md` §8): per-track
    identity-based Scene switching; verify identical-Scene switch is
    inaudible, partial-match switch resets only changed tracks, DEV
    trace `'x'` records appear with correct keep masks
  - P2 matrix: Scene and Pattern Loads through Scene 17; verify played
    Scene moves to 17 before the Load writes, LED_MODE4 flashes while
    17 exists, Scene switch back after Load uses per-track identity
  - Assessment F1 guard: trigger MIDI PC during an in-flight Instrument
    apply and confirm all LFO/velocity bindings reinstall
  - Flash: FS_OS=1: 684,160 B (headroom 69,504 B); FS_OS=0: 720,320 B
    (headroom 32,912 B). ITCM unchanged
  - Copy SD cards to `SD_CARD_S098_P1` / `SD_CARD_S098_P2`; decode
    `'x'` records with `decode_devlogs.py`
- **S089 P3 Drum FM ramp efficiency H1–H6 (open):**
  - H1: boot and play Drum `fm` with `slp`/`smp`/`wtb` — sounds correct
  - H2: A/B against the P2 image with `fm` carriers — no audible difference
  - H3: worst-case Scene `cpu` reads lower than P2
  - H4: `itp` on `fm` carriers — smooth crossfade
  - H5: Scene switch between `fm` types — no glitch
  - H6: non-`fm` Drum sounds unchanged
- **S088 P0 run modes hardware matrix H1–H17 (HIGH PRIORITY):**
  - H1: `rst` A/B (bit-identical to pre-S088 on real hardware)
  - H7/H8: `itp` audible / decayed retrigger
  - H11: carrier-only `itp` on Drum `fm`
  - H15: step automation of `run`
  - H16: worst-case CPU with all six voices in `itp`, both fade slots held
  - G1: Drum `fm` ceiling with depth ramp (hardware stress)
  - G2: resonance step in `fre`/`itp` (if audible, extend
    `SVF_recalcFreqSlew()` to step `q`)
- **S087 full hardware testing:**
  - P0+P1: sample install, playback (smp/slp/wtb), pos on samples, wavetable
    frame blending, modulator s/l/w list, type switching, step-automation
    order (verify a step's automation lands before its trigger for sample
    slicing)
  - P2: wav cell shows correct names under typed oscillators, modulator list
    s/l/w distinction, first 4 + last 4 name rule for long sample names
  - P3: one-shot sample mod oscillator restart, transient click sample
    playback and speed/volume controls
- **RV1-RV4 endless-pot noise** — long idle hardware soak needed, especially
  on global BPM page.
- **Sample loader naming/display/order** — hardware soak after LFN changes
  (sort is now natural order, `storage_naturalCompare()`).
- **S084 matrix V1–V7 confirmation** — recorded as open since S084; confirm
  closed or waived.
- **S081 hardware matrix** — every row from `081_SESSION_HANDOFF_LOG.md` §13.2.
- **Phase 5 hardware acceptance for S072 Steps 6–10** — still not reported
  (`072_SESSION_HANDOFF_LOG.md` §11).

## Untested code fixes

- **Load/save button display glitch** — fix is a two-line reorder in
  `menu_switchPage()` `case LOAD_PAGE:`: update `menu_activePage` BEFORE
  calling `menu_resetSaveParameters()`. Full details in
  `LOAD_SAVE_GLITCH_ASSESSMENT.md`. Not yet applied.
- **S078 STEP marker retry one-liner** — deferred CGRAM marker retry in
  `menu_serviceRuntimeWidgets()` excludes SEQ_PAGE (one-line fix not applied).

## Multi-destination / edge-case testing

- **Multi-destination HCNAMES exit** — single-destination Scene 015 is
  hardware-confirmed; multi-destination, deferred-exit, toggle, hazard, and
  failure fixtures still need evidence.
- **S082 F5 Scene 2 Case-3 invalidation** — instrumented, not tested.
- **S082 P3 A2 frozen-screen blink loss** — low priority.
- **S077 re-test rows 3.10/3.14/3.15** — carry-over from S077 P4.

## Diagnostic / reproduction

- **Boot timeout** — unexplained, not reproducible. Reproduce with
  `DEV_MODE_DIAGNOSTIC 1`, then `DEV_LOGGING_IWDG 1`. The IWDG code is
  implemented but UNVERIFIED on hardware.
- **`cpu` widget with `cmp` on** — verify reading accuracy.

## Unimplemented features

- **Dither global menu option** — dither exists per-voice in DrumVoice but no
  `PAR_DITHER` or menu option is wired. Complete plan in DITHER_AUDIT.md
  Steps 1–8.

## Code cleanup (non-blocking)

- **Stale comments** — `BusCompressor.h` 24 B, `BusCompressor.c` +0.45 %,
  `CrumpBitEffect.h` 73,632 B, the S074 C4 comment placement in `menu.c`,
  `main.c` about 532.
- **Name-cache ownership interlock** — the AutoSave writer reads the shared
  9,000-byte name cache live while Menu clears it from 18 call sites. Only
  the two hottest callers were closed. General hazard (torn AutoSave record)
  needs a proper interlock. Deliberately deferred.
- **S081 P3 A-1** — tapping an already-looped VOICE drops it on release
  (confirm behaviour).
- **S081 stale previous step** — when REC is armed while running.
- **S081 P4 B-2** — carry-over.
- **Stage `W` missing** from `tools/decode_devlogs.py`.
