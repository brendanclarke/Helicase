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
  re-measurement (`build/s093p4/drum_ceiling.txt`).
- Gates G1-G8: G1 clean (headroom 49,412 B; R1 +2 B), G2 all host laws,
  G3 214/3 VDIV, G4 1,218/9, G5 252.1 <= 261.3, G6 0 hot loops grew,
  G7 93/93 exact and osc_setFreq 128, G8 14,552 (+40, A1).
- P4-7: OPEN_TESTS A1-A9.
- P4-8: the eight documentation targets updated with the measured static
  figures; device-only values are marked pending in OPEN_TESTS A5.

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
