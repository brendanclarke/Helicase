# S093 P4 — Alkaline Firmware Implementation Schedule

- **Session:** 093, phase 4: the firmware integration of the Alkaline
  instrument (the brief's "P2-2"), its gates and the device trial.
- **Starts from:** `S093_P3_PREP_WORK_WAVES_KERNEL_CONTROL.md`, which is
  done. Already in the sources and host-tested:
  - Re3/Sw3 removed;
  - the B2R kernel with the fused oscillator read (`DiodeLadder_renderB2Osc`);
  - the N1 network at 64 samples (T2-2);
  - drive and the make-up;
  - the reduced control path (Tier 1, T2-3, batched divisions, pitch from
    the note).

  Nothing Alkaline is in the firmware build yet.
- **Detail sources:** `S093_P2_ALKALINE_B-R_PRE_IMPLEMENTATION.md` §5 (voice),
  §6 (parameters and menu), §7 (integration sites), §10 (gates), §12.3
  (storage and platform). This schedule supersedes their order and adds what
  P3 changed.
- **Binding rules** (`MEMORY.md`):
  - commits belong to the user;
  - no RAM beyond R1 (+2 B, approved);
  - no backward compatibility;
  - the CPU ceiling (whole-voice accounting with VDIV);
  - constant per-sample cost;
  - `ResonantFilter.c` twins and existing oscillator loops untouched;
  - no ITCM code planned.

---

## 0. Before the first line of P4 code

**The budget is met** (P3 §9.4): B2R ≈ 256.5 instructions per sample
against the frozen Drum ≈ 261, about 5 under, in the shipped form:
- (b) T2-3 accepted;
- (c) T2-4 rejected;
- the fused oscillator read, the batched divisions, pitch from the note.

**r6, the 6-state network, is adopted (user, S093)** (P3 §9.2 S5).
- **Why:** ≈ 3.3 per sample more (≈ 253, 8 under Drum); its ring is closer
  to the golden model; it is inaudible against the 8-state form (−43…−49 dB,
  LSD60 ≤ 0.02 dB; `tools/alkaline/listen/alk_net_<set>_r6.wav`).
- **Steps, the first work of P4-0:**
  1. `gen_alk_tables.py`: `NET_STATES = 6` (the common balanced basis,
     `reduced_basis()`); regenerate; `--check` passes.
  2. `DiodeLadder.h`: `ALK_NET_STATES 6u` as the default. The tables' `#error`
     guard ties them to the struct.
  3. Bench `alkaline` green: AL-R on r6 is 2.018 ms / 0.363; AL-N reads only
     `ga`/`d` and is layout-independent.
  4. `check_c.py`:
     - C0 compares the port with the 8-state S093 P1 variants, so it is no
       longer exact (expect about −45 dB, LSD60 ≈ 0.01; the distances to
       golden are the gate);
     - C2 regenerates `alk_c_*_B2R.wav`, the device reference.
  5. `control_cost.py`: the default `--rows 3` (DL_NET_ROWS) is ≈ 1,285 per
     block.
  6. Record the new sizes:
     - `DiodeLadderNet` 72 floats (288 B; 9 points 2,592 B, −1,296 B);
     - `DiodeLadder` and `AlkalineVoice` smaller (measure with `sizeof`).

**Decided for P4 and binding** (S093 P3):
- `ALK_ACC_SOLVES 1`, `ALK_FUSED_OSC 1` and (after step 1–2 above)
  `ALK_NET_STATES 6u` are the shipped defaults. With the fused path the plain
  `DiodeLadder_renderB2()` is unreferenced; LTO drops it, as it drops B-R and
  A-R.
- **Alkaline's oscillator stays a table wave** (saw/square) unless the class
  changes. A switchable sample/wavetable oscillator makes the dearest setting
  the unfused path plus that reader plus `osc_setFreq()`, ≈ 266–275, which is
  over Drum (P3 §9.5).

## 1. Work order

| Phase | Content | Gate |
|---|---|---|
| **P4-0** Baselines and tooling | Snapshot the P3 image (`build/lxr02.elf` of P3) as the gate baseline: oscillator loop dump, mixer loop dump, Drum ceiling (P3 §1). Restore the bench Makefile ELF targets removed in `f3805478` (`elf_osc_check`, `elf86_mixer_check`, `elf86_mixer_snapshot`) against a P3 baseline, or record the by-hand commands. Apply the r6 choice (§0) | Baselines written; `make -C tools/dsp_test alkaline` still green (laws AL-B … AL-X) |
| **P4-1** Engine completion | Trigger signature and platform part; the sequencer step query (§2); the bench's trigger calls (AL-B, AL-S, AL-D, AL-Q, AL-X, `--pattern`, `--sweep`) to the new signature with stubs | Bench `alkaline` (AL-S unchanged); a new law AL-T for the step length |
| **P4-2** Parameters and menu | `AlkalineParameters.c/.h`, menu pages, menu text, the ON/OFF tag (§3) | `special_tags` with the ALK table |
| **P4-3** InstrumentManager | Type, registry, runtime union, every dispatch site (§4) | `make all`; the `_Static_assert`s; the instrument-manager bench |
| **P4-4** Build and other type users | Makefile, `storageTypes.h`, `menu_instrumentLoadIndex` (+2 B, R1), audit of type-sized arrays (§5) | `make all`; `link_budget.py` headroom recorded |
| **P4-5** Storage and platform | `.alk` end to end, Scene type selection, NRPN (§6) | Host benches where they exist; device list §8 |
| **P4-6** Static gates | Linked loop counts, per-block count on the linked image, mixer and oscillator loop gates (§7) | All of §7 |
| **P4-7** Device | Listening, make-up by ear, CPU with four Alkalines, storage round trips, MIDI, W7 (§8) | User sign-off |
| **P4-8** Documentation and log | §9 | Session log written |

Do P4-1 to P4-4 in one sitting. The firmware does not link while the
registry and the dispatch sites are half done.

---

## 2. P4-1: engine completion

| Item | Change |
|---|---|
| Trigger | `Alkaline_triggerVoice(AlkalineVoice *voice, uint8_t source_slot, uint8_t velocity, uint8_t note, uint32_t stepSamples)`. Add the platform part, as `Drum_triggerVoice()` (`DrumVoice.c` ≈ l. 241–254): `lfo_retrigger(source_slot)`, `modNode_updateValue(&velocityModulators[source_slot], velocity / 127.f)`, `instrumentManager_applyVelocityModulationTarget(source_slot, velocity / 127.f)`. The sequencing (accent ≥ 101, legato, slide latch, gate in samples) is unchanged. The bench stubs the three calls |
| LFO | The real `Lfo` replaces the bench's same-size stand-in (`lfo.h`); `Alkaline_initVoice()` already calls `lfo_init()` |
| Step length | New read-only `uint32_t seq_trackStepSamples(uint8_t track)` in `sequencer.c/.h`: `ticksQ8 × 44108 × 60 / (256 × 96 × bpm)` with `ticksQ8 = stepScale_ticksQ8(seq_effectiveTrackScale[track])`, **in 64-bit** (a 16th already overflows 32 bits: 6144 × 44108 × 60 ≈ 1.6e10). BPM clamped ≥ 1 (`seq_getBpm()`, which follows external clock, `sequencer.c` ≈ l. 6611). A track ≥ `NUM_TRACKS` uses the default scale. Reads only; never writes the Pattern region (MEMORY.md S078) |
| Caller | `instrumentManager_triggerTrack()` passes `seq_trackStepSamples(trigger_track)`. InstrumentManager gains a sequencer include: the one new dependency, kept to this read-only query |
| Law AL-T (new) | Step samples at 40/120/300 BPM and at three step scales match the formula; the gate stays 7/12 of it |

---

## 3. P4-2: parameters and menu

**Rows** (`AlkalineParameters.c`, pattern `DrumParameters.c`; each row on one
line; `BIND` = `offsetof(AlkalineVoice, …)`;
`ALKALINE_PARAM_DESCRIPTOR_COUNT` asserted against the enum;
`ALKALINE_INSTRUMENT_TYPE_FLAGS INSTRUMENT_FLAG_BASIC` (`_ADVANCED` only if
the device gate fails, K1); label `"Alkaline"`):

| # | Key | Short / long, category | dtype, mod | Binding |
|---:|---|---|---|---|
| 0 | `alk_wave` | `wav` / "Waveform", "303" | ON/OFF tag `ONOFF_TAG_ALK_WAVE` = 3 (`{2}, "Saw", "Sqr"`), selector, MOD_NONE | `ROW_SELECT_ONOFF_SPECIAL(..., wave, TYPE_UINT8, IM_SPECIAL_NONE)` |
| 1 | `osc1_pitch_coarse` | `coa` / "Coarse", "303" | `DTYPE_0B127`, MOD_0_127, default 36 | `ROW_SPECIAL(..., osc.modNodeValue, TYPE_SPECIAL_F, IM_SPECIAL_PITCH_COARSE \| IM_SPECIAL_OSC1)` |
| 2 | `alk_tuning` | `tun` / "Tuning", "303" | `DTYPE_PM63`, MOD_PM63 | `ROW(..., tuning, TYPE_FLT)` (±700 cents) |
| 3 | `alk_slide` | `sld` / "Slide", "303" | `DTYPE_ON_OFF`, MOD_NONE | `ROW(..., slide, TYPE_UINT8)` |
| 4–8 | `alk_cutoff`, `alk_reso`, `alk_envmod`, `alk_decay`, `alk_accent` | `cut` `res` `env` `dec` (VCF), `acc` (Voice) | `DTYPE_0B127`, MOD_0_127 | `ROW(..., cutoff/reso/envMod/decay/accent, TYPE_FLT)` |
| 9 | `alk_drive` | `drv` / "Drive", "VCF" | `DTYPE_0B127`, MOD_0_127, default 0 | `ROW(..., drive, TYPE_FLT)` (×1..×8; make-up follows, P3 §3.1) |
| 10, 11 | `instrument_vol`, `instrument_pan` | as Drum | as Drum | `vol` TYPE_FLT, `pan` TYPE_UINT8 |
| 12–19 | the eight LFO rows | as Drum | as Drum | on `lfo.*` |
| 20, 21 | `velo_mod_amount`, `velo_mod_dest` | as Drum | as Drum | `ROW_NOBIND_IMAGE` / `ROW_NOBIND` (no `velo_vol_on_off`) |
| 22–25 | the four LFO target rows | as Drum | as Drum | `ROW_NOBIND` |

- **Defaults** (the cells a fresh Kit gets) must equal `Alkaline_initVoice()`.
  Today init sets 0.5 for cut/res/env/dec/acc, which no byte gives exactly
  (63/127 = 0.496). Choose 64 (0.504) or 63 for those rows and set init to
  the same `byte / 127`:
  - `tun` 63 (centre); `coa` 36 (C2); `drv` 0; `wav` 0 (saw); `sld` 0;
  - `vol` as Drum's default (init has 0.8 = byte 102); `pan` 63.
  - Check where the Kit defaults live (the per-type default table used by
    `instrumentManager_resetSlot()`).
- **Not offered:** `srt` (no SRR), `run`, `chn`, `pos`, `typ`, fine pitch.
- **Menu pages** (16 cells, four-cell screens, `INSTRUMENT_MENU_EMPTY`
  padding): `303` coa tun wav sld; `VCF` cut res env dec, drv; `Voice` acc
  vol pan; LFO as Drum; velocity dst amt.
- **Menu text:** `ONOFF_TAG_ALK_WAVE`, `alkWaveNames` in `MenuText.h`; a
  branch in `menu_onoffTagNames()` (`menu.c` ≈ l. 12340). The tag system is
  generic: display, clamp and automation range follow the names' count. Read
  the six `DTYPE_ON_OFF` tag sites once to confirm.
- **`check_special_tags.py`:** add
  `'ALK': 'Core/DSP/Instruments/Alkaline/AlkalineParameters.c'` to `TABLES`;
  the coarse row must classify as `PITCH_COARSE | OSC1`.
- **The P3 voice members** the rows bind to: `wave`, `tuning`, `slide`,
  `cutoff`, `reso`, `envMod`, `decay`, `accent`, `drive`, `vol`, `pan`,
  `osc`, `lfo` (`AlkalineVoice.h`).

---

## 4. P4-3: InstrumentManager (line numbers from the S093 tree; they shift)

| Site | Change |
|---|---|
| `InstrumentManager.h` enum | `INSTRUMENT_TYPE_ALK` before `INSTRUMENT_TYPE_UNKNOWN` (4 → 5). Safe: every stored instrument type is a text token (AutoSave, `.hcnames`, `midilrn.cfg`; S093 P2 I-2) |
| `instrument_registry[]` | `{ INSTRUMENT_TYPE_ALK, "alk", alkaline_instrument_display_label, ".alk", "Alkaline", ALKALINE_INSTRUMENT_TYPE_FLAGS, alkaline_param_descriptors, ALKALINE_PARAM_DESCRIPTOR_COUNT, alkaline_menu_pages, ALKALINE_MENU_PAGE_COUNT }` |
| Runtime union | `AlkalineVoice alkaline;` + `_Static_assert(sizeof(AlkalineVoice) <= INSTRUMENT_RUNTIME_SLOT_BYTES)` (540 B against 1,264 B) + `instrumentManager_alkalineRuntime(slot)` |
| `runtimeInstance`, `runtimeLfo` | The voice; `&voice->lfo` |
| `runtimeInit`, `resetRuntimeSlotFromScene` | `Alkaline_initVoice()` |
| `ampEnvelopeQuiet` | ALK branch → `Alkaline_isQuiet()` (before the `SlopeEg2` path, which returns 1 for a type without one) |
| `runtimePan`, `runtimeVolume` | `voice->pan`, `voice->vol` |
| `slotRunMode`, `slotVoiceChain`, `slotHasActiveItpFade` | Default (0) |
| `recalcSlotFilter` | No-op |
| `calcSlotAsync`, `calcSlotSyncBlock` | `Alkaline_calcVoiceAsync()`, `Alkaline_calcVoiceSyncBlock(…, buf, size)`; both are `noinline` (`AlkalineVoice.h`) |
| `slotUsesSrr` | Not listed: returns 0 |
| `triggerTrack` | `Alkaline_triggerVoice(voice, slot, velocity, note, seq_trackStepSamples(trigger_track))` |
| `oscBySelector` | `IM_SPECIAL_OSC1` → `&voice->osc`. The coarse writer then calls `osc_recalcFreq()`, which only sets `freq`; the voice's block code recomputes the pitch and calls `osc_setTableFreq()` |
| `_filter`, `_ampEg`, `_distortion`, `_transient` | Return 0 |
| `applySlot6AlternateDecay` | No change: no `amp_envelope_decay` row, so track 7's alternate does nothing on Alkaline (B-9 accepted: tracks 6/7 share the voice) |
| `typeSelectableForSceneSlot` (I-3) | Check the Basic/Advanced count with the new type |

---

## 5. P4-4: build and other type users

| File | Change |
|---|---|
| `Makefile` | `-ICore/DSP/Instruments/Alkaline`. `DSP_SRCS` + `Core/DSPAudio/DiodeLadder.c` (the `Core/DSPAudio` `-Ofast` rule covers it) + `AlkalineVoice.c`, `AlkalineControl.c`, `AlkalineTables.c`, `AlkalineSquareTable.c` with explicit `-Ofast` rules like `DrumVoice.o` (Makefile ≈ l. 209). `AlkalineParameters.c` in `SRCS` |
| `storageTypes.h` | `#define STORAGE_INSTRUMENT_ALK INSTRUMENT_TYPE_ALK` |
| `menu.c` | `menu_instrumentLoadIndex[INSTRUMENT_TYPE_UNKNOWN]` grows by one entry: **+2 B `.bss` (R1, approved)**. The only `[INSTRUMENT_TYPE_UNKNOWN]`-sized array (S093 P2 audit) |
| `SceneData.c`, `filesystem.c` default Scene | Unchanged (Drum/Snare/Cymbal/HiHat) |

**Flash, expected** (P3 standalone objects): about 33 KB in all.
- `DiodeLadder.o` about 2.8 KB once LTO drops the uncalled kernels (kept:
  B2R fused, the slow update, init, setNet);
- `AlkalineControl.o` 3.6 KB; `AlkalineVoice.o` 1.3 KB;
- tables about 2.9 KB with r6; square table 22.5 KB;
- parameters and menu about 2–3 KB.

That leaves about 53 KB of the 86,172 B headroom.

---

## 6. P4-5: storage and platform

- **I-1 `.alk` end to end:**
  - `/Instrument/Alkaline` created at boot (the registry-driven boot-index
    stage) and seen by `filesystem_trimDirectoryTailsBlocking()`;
  - `.hcindex` per type;
  - HCNAMES type fields;
  - Load/Save type stepping (open item M1 touches it);
  - the `kit` snapshot (`pm_instrument_snapshot`, 129 B = type + 64 + 64
    cells: 26 rows fit);
  - AutoSave restore and the boot settle.
- **I-3 Scene type selection:** the Basic/Advanced count.
- **I-4 NRPN mapping:** whatever assigns NRPN numbers from descriptors, plus
  `MIDI_NRPN_TABLE.md`.
- **MIDI Learn:** keys resolve through `instrumentManager_typeFromText("alk")`
  (`MidiLearn.c` ≈ l. 416); nothing else needed.

---

## 7. P4-6: static gates

| # | Gate | Pass |
|---|---|---|
| G1 | `make all`; `link_budget.py` | Clean; headroom ≥ 16 KiB (expected ≈ 52 KB) |
| G2 | `make -C tools/dsp_test alkaline alkaline_tables oscillator stock_tables special_tags` (one by one: `make all` stops at `run_modes`, S093 P0 O7) | All green, breaks caught |
| G3 | Linked loop: `elf_looppath.py --elf build/lxr02.elf --sym DiodeLadder_renderB2Osc --cond-heads --in-range` (the fused loop, oscillator included) | ≈ 213 + 3 VDIV (LTO may move it a little) |
| G4 | Linked per block: run `tools/alkaline/control_cost.py`'s walk on `build/lxr02.elf` (extend it with `--elf`; on the linked image the oscillator calls are real) | ≈ 1,285 (r6), 9 VDIV |
| G5 | Whole voice against the frozen Drum (P3 §1, re-measured on the P4 image) | ≤ Drum: ≈ 253 (r6) against ≈ 261 |
| G6 | Mixer: loop dump of `mixer_calcNextSampleBlock` + `elf_hotloops.py` against the P4-0 baseline, **paired by source attribution** (a `-g` twin, P3 §1) because LTO re-lays out the mixer when a fifth engine joins | No other engine's per-sample loop grew |
| G7 | Oscillator loops: `elf_loopdump.py --compare` over the oscillator symbols (P3 §2.3 list) | 0 missing per-sample loops (dispatch fragments may move). `osc_setTableFreq()` is linked for the first time; in P3 its presence moved `osc_setFreq()`'s layout, so check that its per-block path stays 128 |
| G8 | ITCM | Unchanged (14,512 B) |

---

## 8. P4-7: device

1. **Listening:**
   - an Alkaline track plays the S092 P4 pattern;
   - compare with `check_c.py` C2's `tools/alkaline/alk_c_acid_B2R.wav` and
     `alk_c_lowres_B2R.wav` (regenerated after r6 in P4-0);
   - a `res` sweep, as `tools/alkaline/listen/alk_net_sweep_r6.wav`;
   - drive ×1/×4/×8, **low resonance at high drive first** (B2R's weak spot);
   - saw and square.
2. **Make-up by ear (B-2):** set `ALK_MAKEUP_OCT` against a Drum at the same
   `vol`, keeping AL-D's headroom (rerun AL-D after a change).
3. **CPU (binding):**
   - the worst-case Scene with **Alkaline in all four Basic slots** at their
     dearest (Res full, drive ×8, accents, slides), the two Advanced slots at
     theirs;
   - the `cpu` widget ≤ the baseline, 0 underruns;
   - if Alkaline ever goes Advanced (K1), the same with Alkaline in both
     Advanced slots.
4. **Storage:**
   - save/load `.alk`;
   - Kit/Scene/Bank round trips;
   - AutoSave restore after a power cycle;
   - Load/Save type stepping;
   - the `kit` snapshot restore.
5. **Modulation and MIDI:**
   - Morph, LFO and step automation on every row (an LFO on `coa` must move
     the pitch: T1-7 keeps `modNodeValue`);
   - MIDI velocity → accent (≥ 101);
   - MIDI note on;
   - NRPN rows.
6. **W7 (Re3/Sw3 removal):**
   - a Kit with `sNN` modulators re-selected;
   - the `wav` menu shows six built-in waves.
7. **Known behaviour to confirm, not fix:**
   - tracks 6/7 share slot 5 (B-9);
   - MIDI note-off does not end the gate (B-6);
   - roll hits each get a full gate (B-7).

Add the device tests to `knowledge_files/volatile/OPEN_TESTS.md`.

---

## 9. P4-8: documentation and log

- `INSTRUMENTS_DSP_REFERENCE.md`:
  - §3.1/§3.2: the type and its dispatch;
  - §5: the rows;
  - §8: its cost, the whole-voice table of P3 §5, linked;
  - a section on `DiodeLadder.c` and the control path;
  - the T2-2/A-R note.
- `FILESYSTEM_SPEC.md`: the `.alk` keys and directory.
- `MIDI_NRPN_TABLE.md`, `MIDI_SPEC.md`: the Alkaline rows.
- `CPU_USE_DSP_AUDIT.md`: an entry with the measured figures and the device
  result.
- `MEMORY.md`:
  - the frozen Drum figure (≈ 261, 3 VDIV) beside the whole-voice rule;
  - `OSC_SAMPLE_START` 6;
  - the Alkaline binding rules that emerge: no `osc_setFreq()` on the
    Alkaline oscillator (`osc_setTableFreq()`, table waves only); the fused
    read; `ALK_NET_STATES` 6 on the common basis; a switchable sample or
    wavetable oscillator is a class decision (P3 §9.5).
- The S093 P3 session log (not yet written) with P4's, or separately.
- The session log in `knowledge_files/log_archive/` and its index entry.

---

## 10. Risks

| # | Risk | Handling |
|---|---|---|
| K1 | The budget: ≈ 8 under on static figures with r6 (§0) | The device gate decides; Advanced remains the fallback |
| K2 | LTO re-lays out the mixer when the fifth engine joins (P3 saw it from removing two case labels) | `noinline` on both Alkaline entry points; G6 by source attribution |
| K3 | Control-rate figures are static longest paths | G4 on the linked image; the device `cpu` widget |
| K4 | `osc_setTableFreq()` returns level 10 for a NaN frequency (`osc_setFreq()` 0) | The Alkaline pitch is always finite; the contract says so |
| K5 | A-R is unstable with the 64-sample network | A-R is not called; if ever chosen, use the 32-sample network |
| K6 | The bench `make all` stops at `run_modes` (O7) | Run the targets one by one, or fix O7 in P4-0 |
| K7 | A later switchable sample/wavetable oscillator raises the dearest setting over Drum (P3 §9.5) | Not in this prototype; a class decision if wanted |
| K8 | r6's tables depend on the reduced basis, which the generator recomputes from the model each time | `gen_alk_tables.py --check` proves the committed tables; any model change regenerates the basis and needs the P3 §9.3 checks again |
| K9 | The per-block figures exclude the trigger path (`Alkaline_triggerVoice()`), which runs once per step | Not per-block work; the linked count covers it if it lands on a block path |
