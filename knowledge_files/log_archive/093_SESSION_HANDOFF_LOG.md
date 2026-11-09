# Session 093 — Alkaline (TB-303) Instrument

## End of session

```
DATE: 2026-11-08
SESSION GOAL: Add the Alkaline (TB-303) instrument to the firmware, the
  card image and the documentation: P0 code, P1 overhead reductions, P2
  pre-implementation, P3 prep work, P4 phase 1 (the code into the build)
  and P5 phase 2 (storage, static gates, device trial, docs).
COMPLETED: P0-P4 complete and committed; P5 phase 2 complete for everything
  that can run without the device (P4-5 verification, P4-6 tools and gates
  G1-G8, P4-7 OPEN_TESTS entry, P4-8 documentation). The device trial
  (listening, make-up by ear, CPU worst case, storage round trips) remains
  open in OPEN_TESTS A1-A9.
VERIFIED ON HARDWARE: No - the device trial is the open item.

CHANGES THIS SESSION (P4/P5):
- Core/DSP/Instruments/Alkaline/*: r6 tables, trigger with platform calls,
  init bytes, 26-row descriptor table, init.alk
- Core/DSPAudio/DiodeLadder.h: ALK_NET_STATES 6 and layout text
- Core/DSP/Instruments/InstrumentManager.h/.c: fifth type, designated
  registry, union member, accessor, dispatch sites, quiet test, trigger
- Core/Sequencer/sequencer.h/.c: seq_trackStepSamples()
- Core/Menu/menu.c, MenuText.h: ONOFF_TAG_ALK_WAVE, alkWaveNames, Load label
- Core/Hardware/SD/storageTypes.h/.c: STORAGE_INSTRUMENT_ALK, registry ToText
- Makefile: include path, SRCS/DSP_SRCS, four -Ofast rules
- SD_CARD/Instrument/Alkaline/init.alk (new)
- tools/alkaline/: gen_alk_tables.py r6, control_cost.py --elf/--iters/
  --dispatch, make_init_alk.py, regenerated C0 references
- tools/dsp_test/: elf_loopdump.py normalisation, elf_loopsrc.py (new),
  Makefile P4 targets, modules/alkaline.c AL-P, modules/seq_steplen.c
- Documentation: INSTRUMENTS_DSP_REFERENCE.md, FILESYSTEM_SPEC.md,
  MIDI_NRPN_TABLE.md, MIDI_SPEC.md, CPU_USE_DSP_AUDIT.md, MEMORY.md,
  OPEN_TESTS.md, this log, 000_SESSION_INDEX.md

KNOWN ISSUES INTRODUCED:
- ITCM 14,512 -> 14,552 B (+40): entry code in five existing .itcm.s087
  blend functions after LTO symbol internalisation shifted the modulation
  globals to DTCM; no new ITCM code, per-sample loops unchanged. Accepted
  as LTO layout (P4 assessment A1), recorded against G8.
- elf_loopdump.py semantics: .lto_priv.N is stripped and an out-of-body
  branch target is written +EXIT. Both sides of every comparison are
  normalised identically; the baseline was re-dumped from base.elf.
- Cymbal's Instrument Load Morph row reads "CymbaMrp" (was "CymbalMr");
  intended (P4 section 4.7).
- FIXED (field report): the first boot with five types rejected the card's
  four-type /.hcnames header, regenerated the register with every row R and
  discarded every Scene's AutoSave payload. filesystem_validateHcnamesHeader()
  now accepts a header that is a leading part of the registry's token list
  (13 host cases); new types are appended only.
KNOWN ISSUES RESOLVED: None pre-existing.

NEXT SESSION RECOMMENDED GOAL: The S093 device trial (OPEN_TESTS A1-A9),
  then finalize the P4-8 figures with the measured device values (A1-A5)
  and tick the OPEN_TESTS items.
BLOCKERS: Hardware; the CPU widget baseline (A5); the ALK_MAKEUP_OCT
  listening decision (A4).

CRITICAL REMINDERS FOR NEXT SESSION:
- Alkaline oscillator: table waves only, osc_setTableFreq(); never
  osc_setFreq(). OSC_SAMPLE_START stays 6.
- ALK_NET_STATES 6 (r6, one balanced basis). Any model change: regenerate
  with gen_alk_tables.py and re-check P3 section 9.3.
- The registry is the storage schema: designated rows in enum order,
  3-character tokens, one TABLES entry per type. Add-a-type checklist:
  INSTRUMENTS_DSP_REFERENCE.md section 9.2.
- Known behavior, confirm not fix: tracks 6/7 share slot 5; a MIDI note-off
  does not end the Alkaline gate; roll hits get full gates.
- G5 is static (252.1 vs the Drum ceiling 261.3); the device cpu widget
  decides. Fallback is Advanced (K1, user's decision).
```

## Phase record

### P0-P3 (earlier in S093, completed)

- P0: the Alkaline C functions (voice, control path, diode ladder, tables,
  square table) and the bench laws AL-B/R/S/N/AB/D/Q/F/O/X.
- P1: overhead reductions (B-R/A-R, N1 network, 7 V output, drive and
  make-up) and the SPICE/golden comparisons C0-C2.
- P2: the pre-implementation brief: B2R as Basic, the control-path cut list
  (Tier 1/Tier 2) and the budget method (whole voice, per-block work
  included).
- P3: prep work: the Drum ceiling frozen (261.3 per sample, P3 §1), Re3/Sw3
  removed, T2-2 64-sample network, the reduced control path; after the
  listening test (P3 §9) T2-3 accepted, T2-4 rejected, q128 rejected, the
  fused oscillator read, direct pitch, batched divisions and r6 adopted.

### P4 (phase 1, complete)

r6 tables; trigger + `seq_trackStepSamples()`; `AlkalineParameters.h/.c`;
menu tag and Load label; the `init.alk` card file; `InstrumentManager`
fifth type and dispatch; Makefile and `storageTypes`. Build: text 703,832 B,
data 420 B, bss 413,280 B; flash headroom 49,412 B; ITCM 14,552 B.

### P5 (phase 2, complete except the device trial)

- `control_cost.py --elf` (G4) with the A4 audio-loop weight:
  async 872 + sync block 332 + dispatch 14 = 1,218, 9 VDIV.
- `elf_loopsrc.py` (G6/G7 source attribution) and the P4 Drum ceiling
  re-measurement (`tools/dsp_test/baselines/s093p4/drum_ceiling.txt`).
- Gates G1-G8: G1 clean (headroom 49,412 B; R1 +2 B), G2 all host laws,
  G3 214/3 VDIV, G4 1,218/9, G5 252.1 <= 261.3, G6 0 hot loops grew,
  G7 93/93 exact and osc_setFreq 128, G8 14,552 (+40, A1).
- P4-7: OPEN_TESTS A1-A9.
- P4-8: the eight documentation targets updated with the measured static
  figures; device-only values are marked pending in OPEN_TESTS A5.

### P6 (parameter tweaks, implemented 2026-11-09)

- **Menu and rows.** `sld` to mod, `atk` + `dec` to AEG, `acc` to click,
  FILTER on one screen (`cut res env drv`), `pos` with OSC; `alk_attack`
  (26) and `osc1_position` (27) appended, 28 rows. `init.alk` regenerated
  (28 params, 22 morph rows). `special_tags` OK.
- **Attack.** SPICE trigger sweep 0.70..0.55 ms (`vca.py`), per-width fit
  (`fit_spice.py`): delay 0.65 → 0.60 ms, tau 1.20 → 2.60 ms; SPICE
  10→90 % 2.105 → 5.150 ms; firmware 2.176 → 5.078 ms. Byte 0 is the
  former VEG bit for bit (AL-A).
- **Decay.** `fit_meg_range.py` (new): stock t10 174.1 ms / 2.569 s,
  targets 139.3 ms / 3.853 s; `r_min` 54,069 Ω, `r_span` 1,560,243 Ω.
  Firmware AL-M: 139.3 ms / 3.852 s, accent 174.8 ms.
- **Resonance ×1.5.** `FeedbackNet` wiper transformer (continuity at k = 1:
  A/B 4.1e-8, C/D 7.2e-9); `RES_KMAX` 1.5; 13 Res points, 16 prewarp
  points (c held at 1.470 above k = 1), VR4b `min(k_eff, 1)`, SPICE E/F
  pair (`acid_k15`). Bench: stock ring 2.018 ms / 0.364, ×1.5 1.791 ms /
  0.823; AL-SO 1,770 LSB tail (peak 4,448) against 1.4 LSB at the 303 max.
- **Drive ×16.** `16^drive`; AL-D x1/x2/x4/x8/x16 = −29.18/−25.42/−21.98/
  −19.00/−17.13 dBFS, 0 full-scale samples; corner input argument 12.74.
- **`pos`.** Start phase at non-legato notes, 0 free-running; per-block
  bend (13 instructions); AL-PS passes.
- **Model and harnesses.** C0 adds ×16; C1 adds `acid_k15` (−0.361 dB,
  LSD60 7.906 dB, recorded); C2 per step within 0.01 dB / 0.1 cent of the
  committed results; V3b targets +0.9 % / +1.3 %; V3d attack per point.
- **Static gates.** Host: 14 laws, 0 failures. Build: text 705,408 B, data
  420 B, bss 413,280 B; headroom 47,836 B; ITCM 14,552 B. CPU: loop 214
  (3 VDIV) unchanged, per block 884 + 332 + 14 = 1,230 (9 VDIV), whole
  voice 252.5 ≤ 261.3. `elf_p4_check`: 0 missing, 0 grew.
- **Docs.** `INSTRUMENTS_DSP_REFERENCE.md` §5.5/§8, `FILESYSTEM_SPEC.md`,
  `MIDI_NRPN_TABLE.md`, `MIDI_SPEC.md` §5.5, `CPU_USE_DSP_AUDIT.md` item 71,
  `MEMORY.md`.
- **Device trial still pending** (OPEN_TESTS): the attack after rests, the
  decay ends, the resonance top/self-oscillation, drive ×16, `pos`, and
  the worst-case `cpu` with four Alkalines at Res and drive full.

### P7 (samples/wavetables as oscillator types, implemented 2026-11-09)

- **Rows/menu.** Row 0 renamed `alk_wave` -> `osc1_wave` (Drum's
  `ROW_MENU`/`MENU_WAVEFORM`/`MOD_WAVE`, no migration); `osc1_type` (28)
  appended; OSC page `coa tun wav pos | typ`; 29 rows, `init.alk` 29 keys /
  23 morph rows. `ONOFF_TAG_ALK_WAVE` removed.
- **Voice.** `AlkalineVoice.wave` removed (size stays 548); Saw/Sqr reads
  `osc.waveform`; typed trigger/async paths call `osc_typedTriggerPhase()`,
  `osc_userBind()` and `osc_typedPositionApply()` (S2/S3); sync block
  dispatches to `alk_renderTyped()`.
- **Fused readers.** `DlSrc` + `DiodeLadder_renderB2Smp/Slp/Wtb/WtbBlend`
  in `dl_renderB2Body()` with a compile-time source; five `osc_*For()`
  accessors; `osc_wtbPlan_t` moved to `Oscillator.h`.
- **Measured.** AL-X2: 0 differing samples/phases in smp, slp, wtb16/8 blend
  and nearest, smp none; RMS 729/938/1958/2411/2059/2208/0. Linked loops:
  wav 215 (P6 214: +1 from linking the typed code, accepted by the user;
  `wav` ≈ 253.6), smp 217, slp 229, wtb 235/238, blend 255/264; per block
  1,900 (11 VDIV). Flash headroom 38,788 B; ITCM 14,552 B.
- **Admission (P7-D1).** Option A core implemented: a typed Alkaline counts
  as an Advanced place; `instrumentManager_selectorValueAdmitted()` refuses
  a typed value when two other Advanced places exist, before the stored
  image changes for a menu edit (S093 P7 §14 F1 fix) and in the runtime
  writer as the guard. A-1/A-2 (Load/Copy of a stored typed
  `typ` when no place is free) remain the user's open policy decision.
- **Gates.** Host 15 groups 0 failures; special_tags 200 rows; `make all`
  clean. `elf_p4_check`: 93/93 oscillator loops exact; **2 mixer hot loops
  GREW by 1** (HiHat `calcSampleOscFmBlock` 36→37, one mixer decimation
  10→11), source-paired with `elf_loopsrc.py --compare` on P6/current `-g`
  twins: LTO layout, not a P7 loop. This is a deviation from the schedule's
  0-grew expectation and is recorded for the user.

## Find here

- `S093_P4_FIRMWARE_IMPLEMENTATION.md` - phase 1 schedule and the
  implementation/assessment log.
- `S093_P5_FIRMWARE_IMPLEMENTATION_PHASE_2.md` - phase 2 schedule and the
  implementation log.
- `S093_P2_ALKALINE_B-R_PRE_IMPLEMENTATION.md`,
  `S093_P3_PREP_WORK_WAVES_KERNEL_CONTROL.md` - the budget and measured
  model figures.
- `knowledge_files/specification_reference/dsp_instruments_effects/INSTRUMENTS_DSP_REFERENCE.md`
  sections 3.1, 3.2, 5.5, 6.8, 8, 9.2.
- `knowledge_files/volatile/OPEN_TESTS.md` - A1-A9.
