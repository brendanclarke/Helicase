# DRAFT — Merge the Cymbal and HiHat instrument types into `cht`

**Date:** 2026-11-21. **Branch:** `dev-ph7-alkaline` (the S101 tree).
**Status:** draft for your decisions (§9). Nothing in this document is built.
**Origin:** `S101_P2_REDUCTIONS.md` §B.2 (F2), your request of 2026-11-21.

**Goal:** one instrument type, `cht`, that plays every existing Cymbal and
HiHat sound. Flash about −5 to −7 KB (estimate, §7). The per-sample cost is
unchanged.

---

## 1. The two types today

| | Cymbal | HiHat |
|---|---|---|
| Registry row | `INSTRUMENT_TYPE_CYM`, token `cym`, label `Cymbl`, `.cym`, folder `/Instrument/Cymbal` (`InstrumentManager.c:68`) | `INSTRUMENT_TYPE_HAT`, token `hat`, `.hat`, folder `/Instrument/HiHat` (`InstrumentManager.c:73`) |
| Class | Basic (`INSTRUMENT_FLAG_BASIC`, S094 P2) | Basic, plus `INSTRUMENT_FLAG_CHOKE` (0x04) |
| Voice code | `Cymbal/CymbalVoice.c` (505 lines), `CymbalVoice.h` | `HiHat/HiHat.c` (529 lines), `HiHat.h` |
| Descriptors | `cymbal_param_descriptors`, 43 rows | `hihat_param_descriptors`, 43 rows |
| Menu pages | `cymbal_menu_pages` (128 B) | `hihat_menu_pages` (128 B) |
| Flash, own symbols | 3,234 B | 3,298 B |
| Sync render | inlined into `mixer_calcNextSampleBlock` by LTO | the same |
| Runtime | `CymbalVoice cymbal` in the slot union (`InstrumentManager.c:292`) | `HiHatVoice hihat` (`:293`) |
| Card files (`SD_CARD/`) | 40 `.cym` | 40 `.hat`; plus 1,197 Kit/Bank files that name either type |

Both play the same signal path:
- two modulators summed into the FM carrier (`osc_renderTypedFm()` /
  `calcNextOscSampleFmBlock()`);
- the T+D chain or the WFd chain (`voiceFold_process()`), then
  `SVF_calcBlockZDF()` and `voicePost_addGainDist()`;
- SRR in the mixer.

The per-sample loops are the same functions.

---

## 2. The differences, from the code

Checked by diffing the two parameter files and the two voice files with the
type names normalised.

| # | Difference | Cymbal | HiHat | Where |
|---|---|---|---|---|
| D1 | Amp envelope repeat | row `amp_attack_repeat` (`oscVolEg.repeat`, `LONG_REPEAT_CNT`) on the envelope page | no row (the field exists in `SlopeEg2`, left at 0) | `CymbalParameters.c:242`, page row at `:160` |
| D2 | Open decay (VOICE7) | none | row `amp_envelope_decay_choke` (`decayOpen`, `IM_SPECIAL_HAT_DECAY_CHOKE`); `decayClosed` / `decayOpen` / `isOpen` in the voice; the trigger takes `isOpen` | `HiHatParameters.c:243–244`; `HiHat.c` trigger; `HiHat.h` struct |
| D3 | FM carrier gain | 1.0 | 0.5 | `CymbalVoice.c:429/431`; `HiHat.c:447/449` |
| D4 | WFd fold gains | `voiceFold_process(..., 1.0f, 1.0f)` | `(..., 2.0f, 0.5f)`: restores the 0.5 carrier into the fold, then back | `CymbalVoice.c:458`; `HiHat.c:482` |
| D5 | Modulators at a hit | phase reset to 0 when the run mode behaves as `rst` (`behavesAsRst`, S088 P0 §3) | not reset: `osc_modulatorTrigger()` only restarts a sample modulator | `CymbalVoice.c:224, 288`; `HiHat.c:303–304` |
| D6 | Note-off at the envelope's end | `TRIGGER_5` / `voiceControl_noteOff(TRIGGER_5)` | `TRIGGER_6`, or `TRIGGER_7` when `isOpen` | both `calcAsync` |
| D7 | Voice struct layout | `Lfo` before the envelope | `decayClosed`, `decayOpen`, `isOpen` added; `Lfo` / `SnapEg` after | `CymbalVoice.h`, `HiHat.h` |

Notes:
- **D3 and D4 together:** in the T+D chain the HiHat's carrier is 6 dB lower
  into the filter than the Cymbal's at the same settings. In WFd the fold
  sees the same depth (×2 in), and the output is 6 dB lower (×0.5 out). The
  two types' sounds at equal rows differ in level and in filter or
  distortion drive.
- **D5:** with free-running modulators (HiHat) every hit starts the FM at a
  different phase, which is a noise-like hit-to-hit variation. The Cymbal in
  `rst` repeats each hit exactly.
- **D6:** both use fixed trigger numbers from the original LXR's fixed slots
  (Cymbal = voice 5, HiHat = voices 6/7), whatever slot the instrument sits
  in. In any other slot the note-off goes to the wrong track: a defect in
  both today, to fix in the merge.

---

## 3. The merged type `cht`

### 3.1 Identity

| Item | Proposal |
|---|---|
| Enum | one `INSTRUMENT_TYPE_CHT`, replacing `INSTRUMENT_TYPE_CYM` and `INSTRUMENT_TYPE_HAT` |
| Token / extension | `cht` / `.cht` |
| Display label | `CyHat` (5 characters, like `Cymbl`) |
| Folder | `/Instrument/CymHat` (≤ 8 characters) |
| Class | Basic |
| Init file | `SD_CARD/Instrument/CymHat/init.cht` (by `make_init_*` script, as for the other types) |
| Engine | the Cymbal's code (`CymbalVoice.c` renamed `ChtVoice.c` / `.h`), with D1 kept and D3–D5 made rows |

### 3.2 Rows

The 43 Cymbal rows, plus one:

| Key | Short | Values | Meaning |
|---|---|---|---|
| `amp_attack_repeat` (existing, Cymbal) | `rep` | 0..127 | as today; 0 = no repeat (a HiHat) |
| `cht_character` (new) | `chr` | `cym` / `hat` | `cym`: carrier 1.0, fold gains 1/1, modulators reset in `rst`; `hat`: carrier 0.5, fold gains 2/0.5, modulators free-running |

- **Why one row rather than two (Q-M2):** D3/D4 and D5 are what makes
  today's types sound different at equal rows. A single switch converts every
  existing sound exactly (a `.hat` gets `chr hat`, a `.cym` gets `chr cym`).
  Two rows (carrier level; modulator reset) would allow the two mixed
  combinations as well.
- **Cost:**
  - **D3/D4:** the carrier gain and fold gains are per-block constants
    already (`osc_renderTypedFm(..., gain)`, `voiceFold_process(..., in,
    out)`), so a row selects them per block: no per-sample cost.
  - **D5:** a branch at the trigger only.
- **Where the row lives:** on the oscillator page, or the envelope page next
  to `rep`. The page layout is to agree.

### 3.3 The open decay (D2) and VOICE7

**Today:**
- the Kit setting `slot6_track7_amp_envelope_decay` (and
  `slot6_track7_morph_amp_envelope_decay`) gives every **non-Choke**
  instrument in slot 6 its decay when track 7 triggers it
  (`SceneData.c:605`, `scene_setSlot6Track7AmpEnvelopeDecay()`);
- the HiHat, the only Choke type, uses its own `amp_envelope_decay_choke`
  row instead, and Menu substitutes that descriptor on VOICE7.

**After the merge:**
- **No Choke type.** `cht` in slot 6 uses the Kit's track-7 decay like every
  other type.
- **The open decay moves** from the instrument file to the Kit. One value per
  Kit (slot 6 only), where today a HiHat carries its open decay with it.
  Your premise is that this mechanism already covers it.
- **The Choke machinery goes:** about 90 references in 18 files:

  | File | References |
  |---|---:|
  | `InstrumentManager.c` | 24 |
  | `menu.c` | 17 |
  | `copyOps.c` | 11 |
  | `HiHatParameters.c` | 9 |
  | `sequencer.c` | 8 (comments; the choke pair is type-independent) |
  | `storageTypes.c` | 5 |
  | `InstrumentManager.h` | 5 |
  | `HiHat.c` | 5 |
  | `SceneData.h` | 3 |
  | `SceneData.c`, `Autosave.c` | 2 each |
  | `copyOps.h`, `MidiVoiceControl.c`, `SceneModTargets.h`, `presetMorphEngine.c`, `presetManager.c`, `PatternData.c`, `HiHat.h` | 1 each |

  It includes `INSTRUMENT_FLAG_CHOKE`, `IM_SPECIAL_HAT_DECAY_CHOKE`, the
  VOICE7 descriptor substitution, the copy/clear and storage cases.
- **Stays:** the sequencer's choke pair (tracks 6 and 7 share slot 6's voice
  and Scene: `sequencer.c` "HiHat choke pair") is type-independent.
- **The trigger:** `isOpen` becomes "triggered by track 7", the same test
  the Kit decay already uses for non-Choke types. The voice keeps one
  decay; the Kit's track-7 value replaces it for that hit, as for the other
  types.

### 3.4 The note-off (D6)

Replace the fixed `TRIGGER_5` / `TRIGGER_6` / `TRIGGER_7` with the slot's
own track (and track 7 for a slot-6 open hit), from the trigger's
`source_slot`.

This is a fix: today both types silence the wrong track when not in their
original slot. It needs one byte if the voice must remember its track;
`isOpen` already exists in the HiHat struct, so the `cht` struct can reuse
its place (no growth over `HiHatVoice`).

---

## 4. Storage and compatibility

**The standing rule:** no backward compatibility, ever (no migration,
aliasing or converters).

**Without a waiver:**
- every `.cym` / `.hat` file (40 + 40 on `SD_CARD/`);
- every Kit, Scene and Bank file that names `type=cym` / `type=hat`
  (1,197 on `SD_CARD/`);
- every AutoSave record (`.hcprms1/2`) and every `.hcnames` row that holds
  those tokens

stop loading. Each slot falls back to the reject path.

**Two ways (Q-M5):**
1. **Re-create the sounds** as `.cht` files by hand.
2. **A one-off host converter** (a waiver of the rule for this change only),
   `tools/lxr2_converters/cym_hat_to_cht.py`:
   - **`.cym` → `.cht`:** rows as they are, plus `cht_character=cym`;
   - **`.hat` → `.cht`:** rows as they are; `amp_attack_repeat=0`;
     `cht_character=hat`; `amp_envelope_decay_choke` (and its Morph cell)
     **copied into the Kit** that holds the instrument, as
     `slot6_track7_amp_envelope_decay` (and the Morph key), only when the
     instrument sits in slot 6;
   - **Kit / Scene / Bank files:** `type=cym|hat` → `type=cht`, file names
     `.cym|.hat` → `.cht`;
   - **AutoSave and `.hcnames`:** not converted. Delete `.hcprms1/2`,
     `.hcnames`, `.hcnamtmp` before the first boot (the S075 F2 precedent).
   - **Report:** a HiHat outside slot 6 loses its open decay (it had no
     effect there either: VOICE7 is slot 6's).

---

## 5. Work list (for the plan)

Each item to be checked against the tree when the plan is written (no
unverified item in a handed-over plan).

1. **Measure first:**
   - a scratch build with the HiHat row folded into the Cymbal's, to
     measure the flash and check the loops (`elf_p4_check`, `elf_s099_check`,
     the inlined set);
   - the Cymbal and HiHat loops are the same functions, so 0 GREW is
     expected.
2. **`InstrumentManager.h/.c`** (52 type references):
   - the enum: one `INSTRUMENT_TYPE_CHT`, and every later type moves down by
     one;
   - check that nothing stores the numeric type (the HCPR and the files store
     tokens; the probe and the decoder use numbers:
     `decode_devlogs.py`'s `'p'` type map changes);
   - the registry row; the engine map; the union member; every `case CYM` /
     `case HAT`; the flags; `instrumentManager_runtimePan()`, the special
     writers, the reset, the quiet test.
3. **Voice:**
   - `CymbalVoice.c/.h` → `ChtVoice.c/.h`;
   - D3–D5 under `cht_character`;
   - D6 from the source track;
   - delete `HiHat/HiHat.c/.h` and `HiHatParameters.c/.h` (git keeps them);
   - the descriptor table `cht_param_descriptors`: 44 rows (43 + `chr`), the
     special-writer tag for `chr` (`IM_SPECIAL_*` count +1,
     `check_special_tags.py`).
4. **Choke removal:** §3.3's 18 files. Menu's VOICE7 page shows the Kit's
   track-7 decay for slot 6 as it does for the other types.
5. **Storage:**
   - `storageTypes.c/.h` (`STORAGE_INSTRUMENT_CYM/HAT` → `CHT`; the HAT
     special case at `storageTypes.c:59`);
   - the folder and index;
   - `filesystem.c` (1 reference), `SceneData.c` (1).
6. **MIDI:** the NRPN map rows (`MIDI_NRPN_TABLE.md`), `MidiVoiceControl.c`
   (the track-7 comment).
7. **Tools:**
   - the LXR1/LXR2 importers (`lxr1_common.py:384–390`: voice 5 → `cht chr
     cym`, voice 6 → `cht chr hat`);
   - `make_init_*` for `init.cht`;
   - `check_special_tags.py`;
   - `decode_devlogs.py` (the probe's type names);
   - the host benches that name Cymbal or HiHat.
8. **Card:**
   - `SD_CARD/Instrument/CymHat/` with `init.cht`;
   - `SD_CARD/Instrument/Cymbal/` and `HiHat/` removed (or converted, Q-M5).

---

## 6. CPU

- **Per sample:** unchanged. The loops are the same; the new row selects
  per-block constants.
- **Worst case:** the dearest Cymbal or HiHat setting is the same chain on
  both (`CPU_USE_DSP_AUDIT.md` item 76), so the type's worst case is the
  Cymbal's.
- **I-cache:** a Scene that held both types now runs one engine, a small
  CPI gain at most. Not counted.

## 7. Flash (estimate; step 1 measures it)

| Item | Bytes |
|---|---:|
| One descriptor table (860), menu pages (128), label (6) | ≈ 994 |
| One `calcAsync` (396), init (≈ 410), trigger (≈ 1,460) | ≈ 2,266 |
| One inlined sync render in the mixer (by comparison with `Drum_calcVoiceSyncBlock` 2,408) | ≈ 1,500–2,400 |
| The HAT/CYM cases, registry and storage code, the Choke code | ≈ 1,000–2,000 |
| New `chr` row and its writer | ≈ −150 |
| **Total** | **≈ −5 to −7 KB** |

## 8. Gates

- **Host:** a new law that each `chr` setting reproduces its former type bit
  for bit:
  - `chr cym` against the pre-merge Cymbal, `chr hat` against the
    pre-merge HiHat;
  - same rows, same seed, a block-for-block compare (the closed hat; the
    open hat with the Kit's track-7 decay set to the old `decayOpen`);
  - break: swap D3's gains.
- **Elf:** `elf_p4_check` and `elf_s099_check` 0 GREW; the inlined-set diff
  (one engine fewer in the mixer).
- **Device:**
  - the Scene 5 HiHat (slot 5) and a Cymbal Scene, before and after (by
    ear);
  - the open hat on track 7;
  - the probe's `hat` / `cht` slot cost unchanged.

## 9. Decisions

| Q | Question | Recommendation |
|---|---|---|
| Q-M1 | Merge into one `cht` type as above? | yes |
| Q-M2 | D3–D5: one `chr cym/hat` row, or two rows (carrier level; modulator reset)? | **one row**: both existing sound sets convert exactly; fewer menu cells |
| Q-M3 | D6: fix the note-off to the slot's own track? | yes (a defect today in any non-original slot) |
| Q-M4 | Open decay: the Kit's track-7 value only (Choke removed)? | yes, as you proposed |
| Q-M5 | Compatibility: re-create, or one waiver for the host converter (§4)? | yours: the rule is standing |
| Q-M6 | Names: token `cht`, label `CyHat`, folder `CymHat`? | as proposed, or yours |
