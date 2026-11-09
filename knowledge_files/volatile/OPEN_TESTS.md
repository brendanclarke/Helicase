# Open Tests and Hardware Verification Items

Extracted from MEMORY.md during the S087 refactor. These are tests that should
be performed when the opportunity arises, not blocking items for current work.

---

## Hardware soak / confirmation

- **S093 P4 Alkaline (TB-303) device trial (HIGH PRIORITY):**
  - A1: card from `SD_CARD/` (with `Instrument/Alkaline/init.alk`); boot
    completes; `/Instrument/Alkaline/.hcindex` written
  - A2: Instrument Load lists Alkaline after HiHat; `init` loads with the
    init values on every page; Morph rows read `AlkalMrp` / `CymbaMrp`
  - A3: S092 P4 pattern against `tools/alkaline/alk_c_acid_B2R.wav` and
    `alk_c_lowres_B2R.wav`; `res` sweep; drive x1/x4/x8 at low resonance
    first; saw and square
  - A4: `ALK_MAKEUP_OCT` by ear against a Drum at the same `vol`
  - A5: CPU worst case: Alkaline in the four Basic slots at Res full, drive
    x16 (S093 P6: 16^drive), accents and slides; `cpu` widget <= the Drum baseline; 0 underruns
    over 5 min. **Write down the widget figures**
  - A6: `.alk` save under a new name and reload; Kit/Scene/Bank; AutoSave
    after power cycle; `kit` snapshot restore
  - A7: Morph/LFO/automation on every row (LFO on `coa` moves the pitch);
    velocity 101 accents, 100 does not; NRPN 0-25; `rtg` restarts per trigger
  - A8: gate follows step scale and BPM changes; slide into a rest closes;
    external clock
  - A9 (W7): Kit with `sNN` modulators re-selected; Drum `wav` lists six waves
  - A10 (S093 P6): attack byte 0 against 127 after rests and long notes;
    running 16ths open at once (the circuit's behaviour)
  - A11 (S093 P6): decay byte 0 / 127 (−20 % / +50 %); an accented note
    keeps the 303's fast drop during its step, then its tail follows the
    stretched law (decay 127: ≈ 0.83 s → 1.17 s to 10 %, the model)
  - A12 (S093 P6): Res from the 303's maximum (byte ≈ 85) to 127: the top
    self-oscillates with a high cutoff, bounded; `pos` 0 / 64 on Saw and Sqr
  - A13 (S093 P6): drive byte 0 / 64 / 127 (x1 / x4 / x16); the make-up
    level by ear (B-2)
  - Confirm, not fix: tracks 6/7 share slot 5; note-off does not end the gate;
    roll hits get full gates
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
  rvb 163.9 (worst 165.3 ≤ 170 cap).
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
