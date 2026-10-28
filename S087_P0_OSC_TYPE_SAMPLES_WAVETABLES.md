# S087 P0 — Oscillator types: samples (`smp`/`slp`) and wavetables (`wtb`), startup document

**Session:** 087, startup (P0). No code yet.
**Branch:** `dev-ph7-voice_expansion`, at `e67879e2` (S086 P3/P4 committed)
plus the uncommitted S086 close-out documents.
**Written:** 2026-10-28, at the S086 close.
**Purpose:** everything Session 087 needs to plan and build the oscillator
`typ` row and the sample/wavetable types, including the sample work for
`pos`. Several source documents were deleted at the S086 close; this
document carries what they said (§10 lists where to recover them).

**Revisions:**
- **r0** (S086 close): the startup document.
- **r1** (S087 start, 2026-10-28): the start-of-session evaluation.
  - §11: the goal as understood, the S087 CPU rule, a cost outlook per
    new loop, what the spike code gives, the optimizer review, and the
    zero-crossing bitmap evaluation.
  - §12: open architectural questions A1–A15.
  - In-place corrections: §0.3, §2 rule 1, §5.4, §6.4, §7 Q1, §10. Each is
    marked "(r1)".
- **r2** (S087, 2026-10-28): your answers to A1–A15 folded in.
  - §11.7: the interpolation budget, measured on the linked image.
  - §12: decisions (§12.1); A3/A7/A8 re-evaluated (§12.2); new open
    questions A16–A20 (§12.3).
  - Updated in place: §2 rule 1, §6.3, §6.4, §7 Q9, §11.2, §11.3, §11.5.
    Each is marked "(r2)".
- **r3** (S087, 2026-10-28): A17, A19 and A20 decided.
  - **Two interpolation slots, as today,** switched by the global setting.
    The `run` mode shares them next session.
  - The check is **per slot:** a wavetable blend must not greatly exceed
    one of today's slots. The system budget is not assessed here.
  - The "N grants from a budget" model of r2 is withdrawn.
  - Updated: §2 rule 1, §6.4, §11.2, §11.3, §11.7, §12.1, §12.2, §12.3.
- **r4** (S087, 2026-10-28): two additions from the user.
  - **A17 addendum:** a `wtb` carrier can still be FM'd by the modulators,
    and the modulators can play stock waves, samples or wavetables. The
    per-slot check and the section estimates now cover this (§12.2).
  - **A21, the modulator `wav` list:** after the stock waves come
    `s01…` (samples, one-shot), `l01…` (samples, looped) and `w01…`
    (wavetables, first frame), in that order, up to the 127-entry limit
    (§12.1, §12.4).
  - **New open questions A22–A24** (§12.3).
  - Updated: §4.1, §4.2, §6.3, §12.1–§12.4.
- **r5** (S087, 2026-10-28): A16, A18, A22, A23 and A24 decided. No `A`
  question is open.
  - **The modulators get no new rows** (A22): no `typ` and no `pos`. The
    FM and Mod Osc pages do not change.
  - The only modulator expansion is the user content appended to their
    `wav` list (§12.4). With today's card that is `s01`, `l01` and `w01`.
  - Stage B shrinks to one `typ` row per instrument (osc1). D10 and Q5
    are moot for `pos`.
  - Updated: §0.1, §4, §6.5 D10, §7 Q4/Q5/Q10, §11.1, §12.
- **r6** (S087, 2026-10-28): the §7 questions and D1–D10 decided.
  - **Q1:** spike code is suspect, never settled truth (§2 rule 10).
  - **Q5:** what the current repository implements does not change.
  - **Q6:** no migration, confirmed.
  - **Other Qs:** the recommendations are accepted.
  - **D1–D10:** accepted, with the install-time rule (§6.5): extra work
    that makes the playback calculations simpler is done at install, as
    long as it does not unreasonably increase the installed size.
  - Updated: §0.3, §2, §6.5, §7, §11.4, §12.1 A15.
- **r7** (S087, 2026-10-28): can `typ` be step-automated, with no ramp
  and one value per step? Investigated in §12.5. **Yes, with four
  changes,** one of them to a shared gate. One decision is needed
  (retained request bytes). Note added to §4.3.
- **r8** (S087, 2026-10-28): retained request bytes decided.
  - **No remembered `wav` per type, and no restore of it.** The `wav`
    byte is used as it is; when the type switches, it is clamped to an
    entry that exists (§12.5).
  - The full schedule is `S087_P0_IMPLEMENTATION.md`.

---

## 0. Read this first

### 0.1 What S087 does

| Stage | Content |
|---|---|
| **A** | **Step-automation order (D6).** A step's own automation must be applied before its trigger fires. This is a prerequisite for sample slicing. Small, standalone, every trigger-time parameter. |
| **B** | **The `typ` row** on every oscillator, with the menu layout of §4, the selector rules, and the **modulator `pos` rows** (decide the Drum FM / HiHat modulator reset, D10). `typ` offers `wav` only until C/D land, so B is testable on its own. **(r5)** Only osc1 gets a `typ` row (one per instrument). There are no modulator `pos` rows and no new rows on the FM or Mod Osc pages (A22). |
| **C** | **Sample flash catalogue v2 and the installer** (`samples/`, `wavetables/`, natural order, WAV conversion, gain code, boot notice, RAM release). Testable before any new oscillator loop. |
| **D** | **`smp` (one-shot) and `slp` (loop)**: the Q20.12 reader with install-time padding, a fixed loop region, FM/sync/ring copies, sync-master rules, and **`pos` on samples (D1–D5, D7–D9)**. Replaces the v1 user samples (`wav` 8+). |
| **E** | **`wtb`**: power-of-two frames, a one-frame base loop, frame blending through the interpolation pool (§6.4); `pos` = frame position. |

**Each stage has its own CPU gate and hardware gate (§3). A stage starts
only after the previous one passes on hardware.**

**Not in S087 unless the user adds them:**
- the optimizer flag and the zero-crossing bitmap (S086 P1 stage S4);
- `pwm` (S5);
- the run modes `rst fre itp` and the render pool;
- `ReA`/`TrA`/`SwA`;
- the effects chain.

### 0.2 What S086 finished (built on by S087)

- **Stock table waves `Re3`/`Sw3`** (`SQ3`/`SW3`, `wav` 6–7): `[11][1024]`
  tables in program flash, generated by `tools/gen_stock_tables.py`, played
  by the Tri/Saw/Rec loops. User samples are now `wav` 8+
  (`OSC_SAMPLE_START` 8).
- **`pos` (`osc1_position`)** on all four instruments, cell 4 of the first
  oscillator screen. It is continuous:
  - **at a trigger:** start phase + `pos << 25`;
  - **while playing:** a per-block `phaseInc` bend, with the step kept in
    `OscInfo.positionBendStep`;
  - **it acts on:** Sin, Tri, Saw, Rec, Re3, Sw3 and `Cym` (`pos`/128 of the
    crash sample); noise and v1 user samples ignore it.
- **New gates:**
  - `elf86_p3_check`: the oscillator code against a P2 snapshot;
  - `elf86_mixer_check`: per-sample loops inlined into
    `mixer_calcNextSampleBlock()`;
  - `osc_pos`, `osc_stock`, `stock_tables`, `engine86_identity`.
- **Hardware:** everything passed (2026-10-28).
- **Record:** `knowledge_files/log_archive/086_SESSION_HANDOFF_LOG.md`.

### 0.3 Preconditions

1. **Commit** the S086 close-out documents.
2. **Reference code.** The spike trees `Helicase-dev-ph7-spike/` (`26fb7c3`)
   and `Helicase-dev-ph7-voice-pwm-spike/` (`4ad2f850` / `ef7d5445`) were
   **removed from the working tree at the S086 close**. The pwm spike's
   gitlink deletion is still unstaged.
   - S087 will want their sample reader, installer, catalogue, optimizer
     and host tests as reference (§6, §10).
   - **Decide (Q1):** restore them as read-only references outside the
     repository, or plan without them.
   - **(r1) Restored.** Both trees are back at the repository root as
     their own repositories (`26fb7c3`; `ef7d5445` on `4ad2f850`). They are
     reference only: take hunks, never whole files.
   - **(r6) Q1, user:** most of the spike code went in wrong. **Treat it
     with deep suspicion, never as settled truth** (§2 rule 10).
     `S085_SAMPLE_INDEX_SPEEDUP.md` is back at the root too (untracked).
3. **Freeze before editing.** Write `tools/dsp_test/snapshot87.sh` and
   `frozen87/`, the S086 pattern, before the first source edit. Then take
   the baselines (§3.2).

---

## 1. State at the S086 close (facts the plan builds on)

### 1.1 Waveforms and dispatch (`Core/DSPAudio/Oscillator.h/.c`)

| `wav` | Constant | Reader | `pos` today |
|---:|---|---|---|
| 0 | `SINE` | `calcSineBlock` (4,096 points, `<< 20`) | phase |
| 1–3 | `TRI SAW REC` | `calcWavetableOscBlock(table)` (`[11][1024]`, `<< 22`) | phase |
| 4 | `NOISE` | `calcNoiseBlock` (hardware RNG) | none |
| 5 | `CRASH` | `calcSampleOscBlock` (32,768 8-bit points + 1 guard, `<< 17`; the whole sample = 2³²) | `pos`/128 of the sample |
| 6–7 | `SQ3 SW3` (`Re3 Sw3`) | the table loops via `osc_stockTables[]` | phase |
| 8+ | v1 user samples | `calcUserSampleOsc*Block` (`<< 17`, ≤ 32,768 points; looped or held) | none |

**Helpers** (`Oscillator.h`):
- `osc_isTableWave()` (TRI/SAW/REC/stock);
- `osc_positionIsPhase()` (SINE, CRASH, table waves);
- `osc_positionTrigger()`;
- `osc_positionBendApply(osc, bend)` / `osc_positionBendRestore(osc)`;
- `OSC_STOCK_FIRST/LAST`;
- `OSC_POS_BEND_CAP` (64 = off);
- `OSC_POS_BEND_PER_STEP` (`(1<<25)/OUTPUT_DMA_SIZE`, asserted).

**`OscInfo` padding is now used:**

| Offset | Fields |
|---|---|
| 18–19 | `position`, `positionApplied` |
| 33 | `positionBendStep` (after `waveInterpNext`) |

- **Free padding left:** 2 bytes at 34–35, 1 byte after `baseNote` at 75.
- `sizeof(OscInfo)` = 72 (ARM, asserted). Voices: Drum 596, Snare 420,
  Cymbal 476, HiHat 484 B.
- `startPhase` (`uint32_t`, unused; `Drum_setPhaseVoice()` never called) is
  kept for a later clean-up.

**The engines:**
- **Trigger:** `voice->osc.phase += osc_positionTrigger(&voice->osc)` after
  the start-phase block.
- **Render:** `osc_positionBendApply(&osc, …)` / `osc_positionBendRestore()`
  around the osc1 render. Drum passes `modMode != DRUM_MOD_SNC`.

### 1.2 Descriptors and menu

| | Drum | Snare | Cymbal | HiHat |
|---|---:|---:|---:|---:|
| Descriptors | 40 | 39 | 40 | 40 |
| Capacity per slot (`INSTRUMENT_PARAM_COUNT`) | 64 | 64 | 64 | 64 |

- **The last row** is `osc1_position` (index 39; Snare 38).
- **Canonical IDs** are slot × 64 + index; appended rows keep every
  existing ID.
- `special_tags`: 159 rows.

**Page 1, screen 1 of every instrument:** `coa fin wav pos`.
- **Snare screen 2:** `noi mix`.
- **Drum FM page:** `amt frq wav mod`.
- **Cymbal/HiHat Mod Osc page:** `f1 f2 g1 g2` / `wav wav`.

### 1.3 Build (DEV, at `e67879e2`)

| Item | Value |
|---|---|
| text / data / bss | 658,872 / 424 / 430,296 |
| Flash | 659,296 / 753,664 B, **headroom 94,368 B** |
| ITCM | 8,512 / 16,384 B (7,872 free) |
| DTCM statics / FX margin | 4,472 / 3,712 B |

### 1.4 CPU baseline (static instructions per sample; `INSTRUMENTS_DSP_REFERENCE.md` §8)

| Loop | Plain | FM carrier | Sync slave (no reset / reset) | Ring carrier, ramped |
|---|---:|---:|---:|---:|
| Tri/Saw/Rec/Re3/Sw3 table | 24 | 35 | 34 / 39 | 31 |
| Sine | 23 | 30 | 33 / 38 | 30 |
| Crash | 30 | 38 | 36 / 41 | 33 |
| Noise | 12–13 | = plain | unsynced | 16–19 |
| v1 user sample | 40–41 (linked ≈ 41–43) | 48–49 (linked 50–51) | unsynced | 46–48 (linked 49–50) |

- **Drum oscillator section with user samples on both oscillators:** `fm`
  ≈ 92, `mix` ≈ 87, `rng` ≈ 90–91 (the narrowest margin), `snc` ≤ 48.
- **Cymbal/HiHat** with user samples on all three: ≈ 131 standalone.
- **Freeze these as numbers (§3.1) before stage D removes the v1 loops.**
  Afterwards "the dearest loop today" no longer exists in the code.

---

## 2. Rules (binding, from S085/S086)

1. **CPU rule, "no increase in CPU cycles overall" (user, S085).**
   - **The gate (a):** the dearest setting of any voice, in any mode, must
     not exceed that voice's dearest setting today.
   - **Reported only (b):** the same-wave comparisons.
   - **Per-oscillator ceiling:** a new type must not exceed the dearest loop
     the oscillator can select today (the v1 user-sample loops, §1.4), in
     the voice that runs the most oscillators (Cymbal/HiHat).
   - **(r1) S087 wording (user, 2026-10-28):** the worst-case CPU use of
     the oscillator section must not increase. **Any increase in a
     worst-case instruction count needs the user's explicit approval,
     stated with the exact increase in instructions.** How this applies to
     a mode whose own figure rises while staying below the section's worst
     case (Drum `snc`, §11.3) is question A1.
   - **(r2) A1 decided:** the limit is the oscillator section's worst case
     (probably `fm`). Other modes may rise as long as they stay below it.
     Modes are not frozen individually.
   - **(r2, r3) Interpolation is accounted per slot** (§11.7, §12.2).
     - There are two slots, as today, switched by the global setting.
     - Wavetable frame blending takes a slot and may lift a section above
       its ceiling. That is accepted.
     - Its cost per slot must not greatly exceed one of today's slots.
     - Without a free slot, the oscillator steps.
2. **Copies, not edits.** Never add a parameter or a test to an existing
   loop. Write separate copies per type × use (plain, FM carrier, sync
   slave, ring carrier).
3. **ITCM placement:** new ITCM code goes in its own subsection
   (`.itcm.s087`), `noinline, noclone`. No new constant-argument call sites
   into the existing dispatchers; call leaf loops directly
   (`osc_renderPlainWave()` pattern).
4. **No state live across an inlined render (S086 F1, new).**
   - The voice renders are inlined by LTO into `mixer_calcNextSampleBlock()`.
   - A local variable that survives a render raises register pressure
     there and adds reloads to unrelated per-sample loops.
   - Keep such state in the oscillator (the `positionBendStep` pattern).
   - Gate it with `elf86_mixer_check` (or its S087 successor).
5. **Constant cost.** No data-dependent per-sample work; use branchless
   selects or budget the dearer path.
6. **Measure, then accept:**
   - static counts, standalone and linked;
   - the mixer hot-loop gate;
   - then the device `cpu` widget with the six-slot worst-case Scene (G-C),
     using flash-resident sample/wavetable data;
   - flash stalls do not appear in static counts.
7. **RAM** needs the byte count, region, lifetime, owner and the user's
   approval (`STORAGE_SRAM_MANIFEST.md` §10). **Flash** must be budgeted:
   94,368 B are left for everything in S087 and later.
8. **Descriptors:** append only; a missing key loads 0; the special-tag
   check must pass.
9. **Header constants:** test what the firmware compiles. Extract the
   header's constants into the host test rather than restating them (S086
   F3).
10. **Spike code is suspect (user, S087 Q1).** Code from
    `Helicase-dev-ph7-spike/` or `Helicase-dev-ph7-voice-pwm-spike/` is a
    source of ideas only, and most of it went in wrong.
    - A stage plan may name a spike hunk only after re-deriving what it
      must do from this document and the current tree.
    - The plan states every difference the hunk has from that
      derivation.
    - A host test with break proofs shows the behaviour, not agreement
      with the spike.
    - Never assume a spike function, constant, layout or test is right
      because it "worked on hardware" there.
11. **The current repository's behaviour is kept (user, S087 Q5).**
    Today's trigger behaviour does not change: Drum osc2 and the HiHat
    modulators never reset, and Cymbal resets its modulators. The same
    applies to anything else the current tree implements, unless this
    document names the change.

---

## 3. Gates and tooling

### 3.1 Step 0 (before any edit)

1. `snapshot87.sh` → `frozen87/`: `Oscillator.c/.h`, `BufferTools.c/.h`,
   `SampleMemory.c/.h`, `sampleFlash.c`, `filesystem.c` (installer),
   the four engines, the four `*Parameters.c/.h`, `MenuText.h`, `menu.c`,
   `modulationNode.c`, `mixer.c`, `sequencer.c` (stage A).
2. **Baselines from a clean build:**
   - `elf_symdump.py` / `elf_loopdump.py` on the oscillator symbols
     (`OSC86_ELF_SYMS` plus the user-sample loops);
   - `elf86_mixer_snapshot` (the mixer hot loops);
   - `armcheck-oscmod` / `fpseq.py` for every existing loop, standalone and
     linked;
   - **record the §1.4 ceilings as fixed numbers.**
3. **Device:** the user records the six-slot `cpu` readings for the dearest
   Drum, Snare and Cymbal/HiHat Scenes, with v1 user samples on every
   oscillator.

### 3.2 Per stage

- the identity targets (`osc86_identity`-style, `engine86_identity`-style
  against `frozen87/`);
- the host test for the new behaviour (its law, identity with existing code
  where it should match, break proofs, run and recorded);
- `armcheck` counts against the frozen ceilings;
- the ELF oscillator and mixer hot-loop gates;
- `special_tags`;
- `make all && make img` figures;
- **device G-C**.

### 3.3 Existing tools (`tools/dsp_test/`, `DSP_TEST.md`)

| Tool | Use |
|---|---|
| `extract.py` (`--func`, `--object` incl. pointer-to-array declarators, `--lines`) | builds host fragments from the real source |
| `fpseq.py`, `armcheck/` | static per-sample counts |
| `elf_symdump.py`, `elf_loopdump.py` | whole-function / loop-body dumps and compares |
| `elf_hotloops.py` | layout-insensitive comparison of per-sample do-while loops (S086) |
| `test_osc_mod.c`, `test_osc_stock.c`, `test_osc_pos.c` | patterns for host tests with extracted dispatchers |
| `check_special_tags.py` | descriptor tag proof |

**The `osc_mod` target extracts `Oscillator.h` by line range:**
- defines: lines 49–65;
- `OscInfo`: lines 71–143.

Any edit above or inside the struct must update those ranges.

---

## 4. Stage B: the `typ` row, modulator `pos`, menu layout

From S086 P1 §3. User rules unchanged, except that `pos` already exists for
osc1.

### 4.1 Rows

| Short | Category / long | Meaning |
|---|---|---|
| `wav` | `Oscilltr` `Waveform` (`FM` / `Mod Osc` on those pages) | the selection: stock wave, sample number, wavetable number |
| `pos` | `Oscilltr` `Position` | the continuous control 0–127. Done for osc1. **(r5)** Not added for the modulators (A22) |
| `typ` | `Oscilltr` `Type` | `wav smp slp wtb` (+ `pwm` later). **(r5)** osc1 only |
| `run` | `Oscilltr` `RunMode` | not in S087 |

### 4.2 Placement (screen = four cells behind one SELECT)

| Instrument / page | Screen 1 | Screen 2 | Screen 3 |
|---|---|---|---|
| Drum Oscillator (page 1) | `coa fin wav pos` (done) | `typ` | — |
| Drum FM (page 4) | `amt frq wav mod` (unchanged) | `pos typ` (osc2) | — |
| Snare Oscillator (page 1) | `coa fin wav pos` (done) | `noi mix typ` | — |
| Cymbal/HiHat Oscillator (page 1) | `coa fin wav pos` (done) | `typ` | — |
| Cymbal/HiHat Mod Osc (page 4) | `f1 f2 g1 g2` (unchanged) | `wav wav pos pos` (osc2, osc3) | `typ typ` |

**(r5) A22 decided:** the modulators get no `typ` and no `pos` rows, and
the FM and Mod Osc pages stay as they are today:
- Drum FM: `amt frq wav mod` only;
- Cymbal/HiHat Mod Osc: `f1 f2 g1 g2` / `wav wav`.

The modulators reach user content only through their `wav` list (A21,
§12.4). The placement after r5:

| Instrument / page | Screen 1 | Screen 2 |
|---|---|---|
| Drum Oscillator (page 1) | `coa fin wav pos` (done) | `typ` |
| Drum FM (page 4) | `amt frq wav mod` (unchanged) | — |
| Snare Oscillator (page 1) | `coa fin wav pos` (done) | `noi mix typ` |
| Cymbal/HiHat Oscillator (page 1) | `coa fin wav pos` (done) | `typ` |
| Cymbal/HiHat Mod Osc (page 4) | `f1 f2 g1 g2` (unchanged) | `wav wav` (unchanged) |

### 4.3 Storage and behaviour

**Keys:**
- `oscN_type` for osc1 on every instrument, osc2 on Drum, osc2/osc3 on
  Cymbal/HiHat;
- `osc2_position` (Drum, Cymbal, HiHat) and `osc3_position` (Cymbal, HiHat).
- **(r5) Only `osc1_type`, on every instrument.** No modulator `typ` or
  `pos` keys (A22).

All are appended; a missing key loads 0 (`typ` 0 = `wav`, `pos` 0).

| Row | Rows added (rough) |
|---|---|
| `typ` | Drum 2, Snare 1, Cymbal 3, HiHat 3 |
| modulator `pos` | Drum 1, Cymbal 2, HiHat 2 |
| **(r5) after A22** | `typ`: Drum 1, Snare 1, Cymbal 1, HiHat 1; no modulator `pos` |

Check every table against the 64-row capacity, and update `special_tags`.

**`typ`:**
- **It is a selector row:** not morphable, stored single-ended, applied
  before the Morph image on every slot reset. Under SHIFT it shows the
  mirror. These are the rollback's F1/F2/D12 rules and need the
  `presetManager.c` selector handling.
- **Retained request bytes:** `typ → wav → typ` restores the previous
  `wav`/`pos`.
- **The formatter reads the stored `typ`**, from the same image as the cell
  value (S085 D5), not the runtime value.
- **(r7) Step automation:** `typ` is step-automatable with no ramp and one
  value per step, held until the voice's next trigger. It is not an LFO or
  velocity target. What this needs is in §12.5.

**Display:**
- `wav` under `smp`/`wtb` shows the 8-character name on the left and
  `sNN`/`wNN` on the right. Numbers above 99 use `sA0`, `sA1`, ….
- `pos` shows the number once, on the left, with no label text.

**Modulator `pos`:** **(r5) withdrawn (A22):** the modulators get no
`pos` row in S087. The text below is kept for reference only.
- **Same helpers:** `osc_positionTrigger()`; the Apply/Restore pair around
  each modulator render (§2 rule 4).
- **D10 resets:**
  - the Drum FM oscillator and the HiHat modulators **never reset** on a
    trigger today;
  - Cymbal resets its modulators to phase 0 (`CymbalVoice.c`).
  - With continuous `pos`, a change is audible even without a reset; a held
    value is not.
  - **Decide (Q5):** keep them free-running (pos = change only), or reset
    them on a trigger (a behaviour change; test on hardware first, user
    rule).

**The `wav` list and old kits:**
- `wav` 8+ are v1 user samples until stage D removes them. After that,
  samples live under `typ` `smp`/`slp`, and old kits that pointed at
  `wav` 8+ change sound.
- **No migration** (S085 Q8 / S086 Q6, re-confirm, Q6).

---

## 5. Stage C: sample flash catalogue v2 and the installer

From the spike (`S085_OSC_MOD_CHERRYPICK.md` §7, deleted; recover from
`git show 1ea234ea:S085_OSC_MOD_CHERRYPICK.md`).

### 5.1 Files the spike changed

- `Core/SampleRom/SampleMemory.h/.c`: the v2 layout, header, entries, a read
  API without RAM caches, the gain table.
- `Core/SampleRom/SampleInstall.h/.c` (new): WAV format acceptance, 24 → 16
  bit, stereo → mono, peak gain code, item writes,
  `sampleInstall_abortItem()`, completeness checks.
- `sampleFlash.c` (two lines).
- `storageTypes.c/.h`: `storage_naturalCompare()`.
- `filesystem.c` hunks:
  - `filesystem_parseWav()`, `_installCollect()`, `_installSampleTree()`,
    `_installWavetableTree()`, `_installWavetable()` (Pass A/B),
    `_installCopy()`;
  - bounded open retries;
  - long-name subfolder opens;
  - a depth bound;
  - removal of the v1 LFN/manifest helpers.
- `menu.c` `menu_loadSamplesModal()`.
- `main.c` `boot_showSampleCatalogueNotice()`.

### 5.2 Rules (user decisions carried)

- **Folders** `samples/` and `wavetables/`: files first, then subfolders, in
  natural order; depth limit 3. A numeric prefix orders the items and is
  stripped from the display name.
- **Wavetables:** every folder holding `.wav` files is one wavetable, named
  by the folder.
  - One stored depth and **one frame length per table**, a power of two
    (§6.4).
  - At most 128 tables and 128 frames.
- **Never expand:** 8-bit stays 8-bit, 24-bit becomes 16-bit, stereo becomes
  mono.
- **Gain code:** gain = 2^(code/24), applied once per block to `smp`/`slp`.
  There is no gain on wavetable frames.
- **Errors:** one unreadable file skips that item, not the install.
- **`/loops` is retired;** the v1 loop flag is ignored.
- **Boot notice:** a v1 install shows `Samples invalid` / `reload samples`.

### 5.3 RAM (approval needed)

| Change | Bytes |
|---|---:|
| `sample_manifest[120]` + count removed | −13,441 |
| `SampleMemory` caches removed | −5,040 |
| Installer batch, last-items and state added | +2,339 |
| **Net** | **≈ −16 KB** |

### 5.4 Flash map: correction

The spike reserved **61,120 B for a stock-wave block at `0x080C0000`**.
S086 moved the stock waves into program flash, so that block is **not
needed**. Item data can start at the sample-flash floor.

| Region | Spike address |
|---|---|
| Items | from `0x080C0000` |
| Catalogue v2 | `0x081FE000`, 8,192 B |

**(r1) Correction:** the spike code places the catalogue at `0x081FE800`,
6,144 B (`SAMPLE_CATALOGUE_START_ADDRESS`; 32 B header + 2 × 128 × 20 B
entries = 5,152 B). The table above does not match it. Settle the address
and size with the entry layout (A11), since loop points may make sample
entries larger.

### 5.5 Flash budget

Catalogue and installer code come out of the 94,368 B program-flash
headroom. The v1 manifest and loop code are removed in stage D. **Measure
both.**

---

## 6. Stages D and E: the readers

### 6.1 Costs on record (static, spike; `S085_SAMPLE_INDEX_SPEEDUP.md`, deleted; `git show b450d76a:S085_SAMPLE_INDEX_SPEEDUP.md`)

| Loop | Static | Δ vs table wave (24–25) | Where the cost comes from |
|---|---:|---:|---|
| `smp` | 40 | +15 | start clamp, end test, hold, next-point choice, "played-out → 0" select, phase wrap/hold |
| `slp` | 47 | +22 | the loop wrap into `[start, end)` per sample |
| `wtb` 8-bit / 16-bit | 40 / 43 | +15 / +18 | 64-bit multiply for arbitrary frame length; always 2 frames × 2 reads + 3 interpolations |
| `wtb` (cherry-pick measurement incl. FM) | 64 / FM 70 | | the always-blend loop |

- The spike shared one loop between plain and sync (`OSC_SHARED_LOOP`):
  +2 per sample. **Rule 2 forbids this;** use separate copies.
- **Flash stalls:** sample and wavetable data in sample flash can miss the
  16 KB D-cache. This is not counted; G-C must use real data.

### 6.2 Install-time preparation (removes per-sample work)

- **`smp`:** append silent frames at install. Reading past the end then
  returns 0, so the "played-out" select and the next-point hold go away.
  One cheap phase clamp stays, because high pitch can outrun the padding.
- **`slp`:** **fixed loop region** (the whole sample, or the file's `smpl`
  points; P1 Q7). Append copies of the loop's first points after the end,
  so the next-point read never needs a wrap test. The phase wrap itself (a
  compare and a subtract) stays.
- **The start clamp** exists only because FM can push the read before the
  start. It can be dropped from the non-FM copy.
- **Not removable:**
  - the per-sample fraction and interpolation;
  - flash stalls;
  - the gain (already per block).

### 6.3 `smp`/`slp` reader

- **Phase:** Q20.12 (`OSC_SAMPLE_PHASE_SHIFT` 12), i.e. ≤ 2²⁰ points per
  sample.
- **Bind:** once per block (a generation compare), reading catalogue v2 and
  the gain code. Index with `min(request, count − 1)`, so a shorter
  reinstall does not move a selection permanently.
- **Copies needed:**
  - plain;
  - FM carrier;
  - sync slave (or an explicit "unsynced" rule);
  - ring carrier (ramped);
  - the waveform-interpolation path.
  - **(r2) Drop the interpolation path:** samples never interpolate (A7).
    An LFO on `wav` under `smp`/`slp` steps between samples.
- **Sync master rules** (S085 plan §12, user):

  | Master | No optimizer flag | Flag set (S4, later) |
  |---|---|---|
  | stock waves | the 2³² phase clock (as today) | — |
  | `smp` | as noise: one reset per nominal period at `frq` | resets at the sample's zero crossings (bitmap) |
  | `slp` | reset when the sample loops | zero crossings |
  | `wtb` | reset when the frame loops (the phase wrap) | zero crossings per frame |

  - **(r4)** With the modulator flat list (A21), the master's list entry
    picks the rule: stock → phase clock; `sNN` → as noise; `lNN` → loop
    wrap; `wNN` → frame wrap (the first frame only, so the phase wrap).
  - Each master kind adds a reset-list builder; the slave copies stay.
  - The loop-wrap builder compares and subtracts at constant cost (one
    modulo per block only when the step exceeds the loop).
  - The crash table is treated as noise.
- **A one-shot `smp` on the Drum FM page** plays once after selection, then
  holds, because `modOsc` never resets (S085 R11/R12; Q5).

### 6.4 `wtb` reader

- **Power-of-two frames, guaranteed at install** (round up when the
  optimizer cannot shrink): `shift = 32 − log2(len)`, `mask = len − 1`,
  computed at bind. This removes the 64-bit multiply and the wrap compare.
- **Base loop = one frame:** 2 reads and 1 interpolation. `pos` selects the
  frame per block ("stepping").
- **Blending (user rule):**
  - blending two frames claims **one of the two interpolation units**
    (today `OSC_WAVE_INTERP_MAX_ACTIVE` = 2, shared with `wav` waveform
    interpolation);
  - with no unit free, the oscillator steps.
  - **Claim order:** waveform interpolation and `wtb` blends compete equally
    (ongoing `itp` crossfades would come first once run modes exist).
  - The pool is budgeted as always full: 2 × the dearest unit render.
    **Measure whether a `wtb` blend unit is dearer than today's dearest
    unit.**
  - **(r1) How the pool works today:**
    - `modulationNode.c` claims a unit while it writes an LFO or velocity
      value to a `wav` row. Claims are first come, first served, across
      all slots, and `modNode_resetTargets()` resets the count every block.
    - A claimed oscillator runs `calcPeriodicInterpBlock()`: two full
      renders into scratch buffers, then a float blend loop. That function
      runs from flash, not ITCM.
    - **The dearest unit today:** interpolation between two v1 user samples
      on an FM carrier. That costs one extra FM user-sample render (48–51)
      plus the blend.
    - A `wtb` frame blend has no `wav` write to claim at. It must claim at
      render time instead (A8).
  - **(r2) Superseded by A7/A8 (§12.2):**
    - only wavetables use the two slots; `wav`, `smp` and `slp` always
      step;
    - (r3) the slot count stays 2; a wavetable blend must not greatly
      exceed today's per-slot cost (§11.7); next session's `run` mode
      shares the slots;
    - claims are made at render time and are sticky;
    - the power-of-two rule is reopened (A16); **(r5)** dropped: any frame
      length, `UMULL` indexing.
- **Open (Q9):**
  - when to claim (every block, only while `pos` moves, or only between
    frames; the worst case is the same);
  - the fallback (snap to the nearest frame, or one frame per block toward
    the target);
  - losing a unit mid-glide (snap to nearest?).
- **Sync master:** the frame wrap.

### 6.5 `pos` on samples: the decisions (D1–D10)

**Why samples need their own rule.** `Cym` fits the S086 rule because its
32,768 points are exactly one full 2³² phase and it loops. Real samples do
not:
- they have arbitrary length;
- a one-shot has an end;
- a loop region need not be the whole sample.

These decisions must be in the S087 plan before any sample code. Each has a
recommendation (from S086 P4 §6).

| # | Decision | Why it is open | Recommendation |
|---|---|---|---|
| **D1** | **Phase format and arithmetic.** | The first rule in S086 P4 Q2 used the v1 `<< 17` units, which cap samples at 32,768 points. | Q20.12. Start = ((`pos` × len) >> 7) << 12. One step over one 32-sample block is exactly **d × len** per sample (len/128 points × 2¹² / 32). It is exact for every length up to 2²⁰ points, and \|d × len\| ≤ 2²⁶ fits `int32`. |
| **D2** | **What `pos` refers to in `slp`.** | The spike looped from the `pos` start to the end, so its loop moved with `pos`. With continuous `pos` the loop would shift on every LFO step. | **Fixed loop region** (P1 Q7). `pos` is a fraction of the **whole sample**. A start before the loop plays the lead-in, then loops. A bend moves the playhead, then wraps it into the loop region. |
| **D3** | **Circular or linear difference.** | S086 treats 127 → 0 as +1 step, because phase wraps. A one-shot is not periodic: 127 → 0 must jump back almost the whole sample. | **Per type:** circular for periodic content (stock waves, `Cym`); **linear** (−127..+127) for `smp`/`slp`, then the edge rule (D4) and the loop wrap (D2). The type chooses the rule inside the apply helper. |
| **D4** | **One-shot edges.** | The v1 reader stops at the end. A backward bend near 0 must not wrap the phase into an out-of-range index. | Clamp at the start (0). Moving past the end ends the hit early. **A finished one-shot stays finished until the next trigger** (no revival by `pos`). The clamp applies to the block's total bend, at block level. |
| **D5** | **Bend size scales with length.** One step is len/128 points. | 1 s sample: one step ≈ 11× speed for a block; 10 s: ≈ 108×. Long-sample LFO scrubs smear and alias (samples are not band-limited). | **A time-based cap for samples** (points per block, e.g. ≤ 4× playback speed), separate from `OSC_POS_BEND_CAP`. A larger change glides over several blocks. Tune by ear. |
| **D6** | **Step automation must be exact at the hit.** | `main.c` runs `voiceControl_processPending()` (triggers) before `seq_drainPendingAutomation()`, and a step's first trigger restores the Morph base first. So a step's `pos` reaches the voice one block after its hit and glides in (S086 P3 Q14; inaudible for phase, passed on hardware). For **per-step sample starts (slicing)** each slice would start in the wrong place and smear. | **Stage A:** a step's own automation must land before its trigger fires (drain first, or let the trigger consume the voice's pending values). It affects every trigger-time parameter, so it needs its own plan and tests. Whether the sequencer publishes a step's values a tick before its trigger has not been traced: trace it first. |
| **D7** | **Re-anchoring on a type or source change.** | `positionApplied` means a phase offset for `wav`, a point offset for `smp`/`slp`, and nothing for `wtb` (`pos` = frame position). | Any change of `typ`, waveform family or sample re-anchors: `positionApplied = position`, no bend that block. Today's non-phase waves already consume the delta, which is the same rule. |
| **D8** | **No live state across the render** (S086 F1). | The sample bend needs the length factor at restore. | Keep the step in `positionBendStep` (`int8_t`) and recompute step × len at restore from the bound sample's length, which cannot change inside a block. No new field. Gate with the mixer hot-loop check. |
| **D9** | **Sync slave reset target.** | The spike restarted a synced sample at its start + overshoot. With continuous `pos`, "start" could mean the trigger start or the current offset. | The reset target is the **`pos` start point**, as at a trigger. No bend in `snc` (S086 P3 Q12), so the two agree. Decide with the sync-master rules (§6.3). |
| **D10** | **Modulators that never reset** (Drum FM, HiHat). | For `smp` on those oscillators the trigger start never happens. | Decide in stage B with the modulator `pos` rows (Q5). **(r5)** No modulator `pos` (A22). What remains is S085 R11/R12: an `s01…` one-shot on the never-reset Drum osc2 or HiHat modulators plays once after selection, then stays silent (§12.4). No change is made. |

**(r6) D1–D10 accepted (user).** The install-time rule:
- **Extra work that makes the playback calculations simpler is done when
  the sample is installed,** as long as it does not unreasonably increase
  the installed size.
- **What the installer prepares, and what it costs:**

  | Prepared at install | Removes at play time | Size per item |
  |---|---|---:|
  | two zero frames after every sample | the `smp` played-out select and next-point hold (§6.2, A4) | 4 B |
  | fixed loop region (`smpl` loop 1 when present, else the whole sample; Q7) stored as start/end in the entry | the loop region tracking `pos` (D2) | in the 28 B entry |
  | gain code | the level analysis | in the entry |
  | `wtb` frame length, count, depth and byte stride | the per-bind geometry arithmetic | in the entry |
  | (later, A13) the zero-crossing bitmap | the crossing search | 6.25 % (16-bit) |

- **Every new D-stage calculation must be checked against the rule.** If
  a per-block or per-sample step can become a stored value at a few bytes
  per item, it moves to the installer. The stage-D plan lists each
  candidate and its byte cost.
- **Not taken:** a loop-start copy after the loop end. It would remove
  the `slp` next-point select (≈ 2 per sample), but it conflicts with the
  `smp` zero pad at the same address whenever the loop ends at the file
  end (A4).

**`pos` per type, the full picture:**

| `typ` | At a trigger | While playing |
|---|---|---|
| `wav` (done) | start phase + `pos`/128 cycle (Cym: of the sample) | the per-block `phaseInc` bend, circular |
| `smp` | start = `pos`/128 of the sample | linear bend in Q20.12 (D1), capped (D5), clamped at 0; past the end = ended (D4) |
| `slp` | start = `pos`/128 of the whole sample | linear bend, capped, wrapped into the fixed loop region (D2) |
| `wtb` | — | frame position, read every block; blending through the interpolation pool |

---

## 7. Open questions for the start of S087

| # | Question | Recommendation |
|---|---|---|
| **Q1** | Restore the spike trees as read-only references (outside the repository), or plan without them? | Restore them outside the repository. The reader, installer and host tests save time; take hunks only, never whole files. **(r1) Done:** restored at the repository root (§0.3) |
| **Q2** | Stage order A → B → C → D → E? | Yes. A is small and unblocks slicing; B makes the menu/storage work testable with `typ = wav` only |
| **Q3** | D1–D10 as recommended in §6.5? | Yes; record them in the stage-D plan |
| **Q4** | Keys `oscN_type`, `osc2_position`, `osc3_position`? | Yes. **(r5)** Only `osc1_type` (A22) |
| **Q5** | Drum FM and HiHat modulators: keep free-running (pos acts on changes only), or reset on a trigger? | Keep free-running in S087; trial a reset only on the user's request. **(r5)** Free-running, unchanged; there is no modulator `pos` (A22) |
| **Q6** | Confirm no migration for kits whose `wav` pointed at v1 user samples (since S086 they already moved from 6+ to 8+) | Confirm |
| **Q7** | `slp`: fixed loop region = the whole sample, or the file's `smpl` loop points when present? | `smpl` points when present, else the whole sample; fixed at install |
| **Q8** | `wtb`: `pos` = frame position; `wav` = the wavetable number? | Yes |
| **Q9** | `wtb` blend claims, fallback and slot loss (§6.4) | Claim only while `pos` sits between frames; snap to the nearest frame on fallback; decide by ear. **(r2) Decided:** every `wtb` oscillator claims every block while the global setting is on (A7). Grants are sticky (A8). Without a grant: the nearest frame |
| **Q10** | Cymbal/HiHat modulators: which types may they select? | Decide from the measured three-oscillator worst case per type. **(r5)** Decided: the stock waves plus `s`/`l`/`w` entries in `wav` (A21, §12.4); no `typ` |
| **Q11** | Sample `pos` cap (D5): the points-per-block limit | Start at 4× playback speed; tune at the hardware gate |
| **Q12** | Flash: the catalogue/installer/reader code against the 94,368 B headroom. Set a budget per stage? | Yes; measure after each stage and stop if a stage exceeds its share |
| **Q13** | `ReA`/`TrA`/`SwA` (+22,528 B each) and the S086 level difference (Re3 ≈ 8 dB, Sw3 ≈ 2 dB quieter than Rec/Saw) | Not in S087; revisit after the flash budget for stage C–E is known |

**(r6) Decisions (user, 2026-10-28):**

| # | Decision |
|---|---|
| Q1 | The trees are restored, but **spike code is suspect, not settled truth** (§2 rule 10) |
| Q2 | Accepted: A → B → C → D → E |
| Q3 | Accepted: D1–D10 as recommended, with the install-time rule (§6.5) |
| Q4 | Accepted, narrowed by A22: the only new key is `osc1_type` |
| Q5 | **Do not change what the current repository implements** (§2 rule 11) |
| Q6 | **Confirmed:** no migration |
| Q7 | Accepted: the first `smpl` loop when present, else the whole sample; fixed at install |
| Q8 | Accepted: `wtb` `pos` = frame position; `wav` = wavetable number (A20) |
| Q9 | Decided in r3 (A7/A8) |
| Q10 | Decided in r5 (A21/A22) |
| Q11 | Accepted: start at 4× playback speed; tune at the hardware gate |
| Q12 | Accepted: a flash budget per stage, measured after each stage |
| Q13 | Accepted: not in S087 |

---

## 8. Order of work

0. **Preconditions** (§0.3); Q1–Q13 answered; step 0 baseline (§3.1).
1. **Stage A:** plan (`S087_P1_…`), trace the sequencer publish order,
   implement, host test, hardware.
2. **Stage B:** plan (`S087_P2_…`) with the selector, storage and menu work;
   implement with `typ` = `wav` only; hardware.
3. **Stage C:** plan; catalogue v2 + installer + boot notice + RAM release;
   host tests (natural order, catalogue, WAV conversion); hardware install
   tests.
4. **Stage D:** plan with D1–D9; the reader copies; `pos` on samples;
   removal of the v1 loops; gates against the frozen ceilings; hardware G-C.
5. **Stage E:** plan; `wtb` with stepping and pool blending; gates;
   hardware G-C.

**Each stage gets its own `S087_Pn_*` plan before code, and an
implementation record afterwards** (the S086 pattern: plan → schedule with
cited lines and comment blocks → implementation → assessment → fixes →
hardware).

---

## 9. Lessons from S086 to apply

1. **"The same way as the existing feature" means the same mechanism.**
   - The first S086 P2 attempt generated stock waves at install time into
     sample flash: wrong, discarded.
   - Read the existing implementation before planning a variant of it.
2. **Measure the real data before copying a recipe.** Only `sawTable`
   followed the band-limit taper the plan assumed; `recTable`/`triTable`
   use a looser one.
3. **A plan's claim "no per-sample change" must be checked where LTO puts
   the code.** The voice renders live inside `mixer_calcNextSampleBlock()`;
   the oscillator gates alone missed F1.
4. **Host tests must compile the header's own constants** (F3); break-proof
   every check.
5. **Keep comment blocks true to the code** after every fix; descriptor-row
   blocks are documentation too.

---

## 10. Sources

| Content | Where now |
|---|---|
| S086 record (stock waves, `pos`, assessment, fixes, gates) | `knowledge_files/log_archive/086_SESSION_HANDOFF_LOG.md` |
| Oscillator reference (`pos`, stock waves, costs, how to modify) | `INSTRUMENTS_DSP_REFERENCE.md` §5–§6, §8–§10 |
| Test bench | `tools/dsp_test/DSP_TEST.md` |
| S086 type-expansion plan (stages S1–S5, menu §3, Q1–Q11) | `git show e56bb957:S086_P1_OSC_TYPE_EXPANSION.md` |
| Sample/wavetable speed-up and the pool rule | `S085_SAMPLE_INDEX_SPEEDUP.md` (root, untracked, restored r1); also `git show b450d76a:S085_SAMPLE_INDEX_SPEEDUP.md` |
| Sync rules for sample/wavetable masters, the zero-crossing bitmap design, and the optimizer flag | `git show b450d76a:S085_P1_OSC_MOD_SNC_RNG.md` §12; `085_SESSION_HANDOFF_LOG.md` §4.5 (the crossing measurements); `git show e56bb957:S086_P1_OSC_TYPE_EXPANSION.md` §2.2, §4.4 |
| The optimizer | `Helicase-dev-ph7-voice-pwm-spike/tools/sampleoptimize.py` (the newer of the two copies; §11.5) |
| What the spike built (catalogue v2, installer, `smp`/`slp`/`wtb`, menu formatting) | `git show 1ea234ea:S085_OSC_MOD_CHERRYPICK.md` |
| S085 snc/rng design and lessons | `085_SESSION_HANDOFF_LOG.md` |
| Spike code | restored at the repository root (r1; §0.3, Q1): `Helicase-dev-ph7-spike/` (`26fb7c3`), `Helicase-dev-ph7-voice-pwm-spike/` (`4ad2f850` / `ef7d5445`), each its own repository |

---

## 11. S087 start: evaluation (r1, 2026-10-28)

All instruction figures in this section are **static estimates** from
reading the code, not measurements. Each one must be measured
(`fpseq.py`, standalone and linked) before it is relied on, and static
counts do not include flash stalls.

### 11.1 The session goal as understood

- **What S087 builds:** a `typ` row selects `wav` (today's oscillator),
  `smp` (one-shot sample), `slp` (looped sample) or `wtb` (wavetable).
  These replace the v1 user-sample tail of `wav` (8+).
- **What they rest on:**
  - catalogue v2 and a new installer (`samples/`, `wavetables/`);
  - the step-automation order fix (stage A, for sample slicing);
  - the `typ` rows (stage B; r5: osc1 only, A22).
- **How it is staged:** A → E. Each stage gets its own plan, CPU gate and
  hardware gate.
- **The binding constraint is CPU.** No voice's oscillator section may get
  dearer in its worst case. The new types must therefore be cheaper per
  sample than the spike's loops (40–70 static). The levers:
  - **Install-time preparation:** zero padding, a fixed loop region, and
    power-of-two frames.
  - **Separate copies per use:** plain, FM, sync and ring, with no shared
    loop and no lane test.
  - **Pooled frame blending:** a `wtb` oscillator renders one frame; a
    two-frame blend costs one of the two interpolation units.
- **`pos`:** gains a meaning per type (D1–D10). On `wtb` it is the frame
  position.
- **Spike code is reference only.** Its bind, catalogue and installer
  logic can be reused as hunks. Its per-sample loops cannot: they carry
  the shared-loop lane test, the run-mode slew, the 64-bit multiply, the
  always-on frame blend and a data-dependent `%`.
- **Stages A and B** were not re-traced here. §4 and D6 stand as written.

### 11.2 The ceilings to freeze at step 0

| Voice / item | Worst case today (static per sample) | Note |
|---|---:|---|
| Drum section, `fm`, user samples on both oscillators | 90 standalone / ≈ 92 linked | the Drum ceiling |
| Drum `rng` | 87–89 / ≈ 90–91 | narrowest margin against `fm` |
| Drum `mix` | 87 | not measured linked |
| Drum `snc` | 48 | the user-sample slave renders unsynced |
| Snare: tonal oscillator + noise | ≈ 40–41 + 12–13 | **measure** (never recorded as a section figure) |
| Cymbal/HiHat: 2 plain modulators + FM carrier, user samples on all three | ≈ 41 + 41 + 49 = 131 standalone | **measure linked** |
| Interpolation slot: one slot, worst case | (r2) ≈ +64 standalone / ≈ +66 linked over the plain render, plus a few per sample of per-block set-up | (r3) **the per-slot reference** for wavetable blends (§11.7); 2 slots per block, system-wide |

These numbers stay fixed after stage D removes the v1 loops (§2 rule 1).

### 11.3 Cost outlook per new loop (estimates)

| Loop | Spike figure | What changes | Estimate | Ceiling (same role) | Margin |
|---|---:|---|---:|---:|---:|
| `wtb` one frame, plain | 40 / 43 (8/16-bit, blending always) | power-of-two shift/mask; one frame; no lane test; no slew; the 8-bit × 256 folded into the block gain | ≈ 24–26 (the table loop plus a register shift) | 40–43 | ≈ 15 |
| `wtb` FM carrier | 70 (incl. blend) | as above | ≈ 35–37 | 48–51 | ≈ 12 |
| `wtb` ring carrier | — | as the table ring copy | ≈ 31–33 | 46–50 | ≈ 15 |
| `wtb` sync slave | shared loop | separate copy, as the table sync copy | ≈ 39–41 | `snc` 48 incl. builder 7 | ≈ 0 |
| `wtb` blend (one slot) | in every loop | a separate two-frame copy per role, used only with a slot | ≈ 35–37, i.e. **≈ +11–15** over one frame in the same role | (r3) today's per-slot cost, ≈ +66 (§11.7) | ≈ 50 under |
| `smp` plain | 40 | the installer's two zero frames make the played-out select and the next-point hold unnecessary; no start clamp (no FM); no lane; no slew; one phase clamp at the end | ≈ 28–31 | 40–43 | ≈ 10 |
| `slp` plain | 47 | fixed loop region; phase wrap = compare + subtract; the next-point select may stay (A4) | ≈ 30–33 | 40–43 | ≈ 8 |
| `smp`/`slp` FM carrier | not reported | + the FM read offset; the read index clamped (or wrapped) at both ends (A5) | ≈ 40–45 | 48–51 | **≈ 5–9, the tightest** |
| `smp`/`slp` ring carrier | — | + the ring law | ≈ 36–41 | 46–50 | ≈ 8 |
| `smp`/`slp` sync slave | shared loop | separate copy | ≈ 40–46 | new in `snc` | — |
| Builder: periodic (exists) | — | — | 7 | — | — |
| Builder: loop wrap (`slp` master) | — | compare + subtract, constant cost | ≈ 9–12 | — | — |
| Builder: zero-crossing bitmap | not built | §11.6 | ≈ 12–15 | — | — |

**What follows:**
- **Every per-oscillator role has margin** if the estimates hold. The
  tightest is the sample FM carrier.
- **Cymbal/HiHat with samples on all three** ≈ 31 + 31 + 45 ≈ 107,
  against 131.
- **Drum `snc` is the one figure that can rise.** With a sample or
  wavetable slave, the section is the builder (7–15) plus the slave copy
  (40–46): ≈ 47–61 against today's 48.
  - That is up to **≈ +13 over today's `snc`**.
  - It stays under the Drum ceiling (92), so it passes gate (a). Whether
    the S087 wording needs explicit approval for it is A1.
- **(r2) A1 decided:** the `snc` rise is allowed, because it stays under
  the section's worst case.
- **(r2) Frame lengths that are not a power of two** (A16): one `UMULL`
  gives both the index and the fraction. With a compare for the
  next-point wrap, this costs **≈ +1–2 per sample** over shift/mask
  (estimate). The spike's +15 over the table loop came mostly from the
  always-on blend, the lane test and the slew, not from the multiply.
- **Per-block work stays outside the loops:** bind, generation compare,
  gain decode, frame choice. It comes to ≈ 20–40 instructions per block
  (≈ 1 per sample), like the `pos` bend. Report it.
- **Flash stalls (not counted):**
  - The v1-sample G-C baseline already contains sample streams from
    flash.
  - Short power-of-two `wtb` frames stay in the D-cache better than v1
    samples did.
  - Long samples read at high speed are the risk case. Test them on G-C.

### 11.4 What the spike code gives (reference findings)

**(r6) Read under §2 rule 10.** "Reusable" below means "a candidate worth
re-deriving", never "correct". Each candidate is re-derived, re-tested
and compared with this document before any of it is used.

- **`OscillatorSample.c` (pwm spike):**
  - **Reusable:** the bind (generation compare, request re-expansion,
    `min(request, count − 1)`), `oscSample_restart()` and the Q20.12 speed
    law. The law is `inc = f × 2²² / REAL_FS`, so a sample plays at its
    original speed at f = REAL_FS / 1,024 ≈ 43.07 Hz. That matches v1
    (`<< 17`, 1,024 points per nominal period), so `coa`/`fin` keep their
    meaning for samples.
  - **Not reusable (the loop body):**
    - the lane test (`OSC_SHARED_LOOP`);
    - the per-sample slew (a run-mode feature, not in S087);
    - `%` for deep FM (a data-dependent divide);
    - a loop region that starts at the `pos` start (the D2 problem).
- **`OscillatorWavetable.c`:**
  - **Reusable:** the bind.
  - **Not reusable:**
    - the frame pair is chosen per block in float (fine, but it assumes a
      blend every block);
    - the loop uses the 64-bit multiply, always blends, and carries the
      lane test.
- **`SampleInstall.c`:**
  - It already appends **two zero frames after every sample**
    (`INSTALL_SAMPLE_PAD_BYTES` 4), so the `smp` padding of §6.2 exists.
  - It accepts **samples only as mono 16-bit 44,100 Hz**. The host
    optimizer converts everything else. Conversion on the device (24 → 16
    bit, stereo → mono, any rate) applies to **wavetable frames only**.
    §5.2 reads as if it applied to samples too (A15).
  - There is no 8-bit sample storage; `wtb` stores 8 or 16 bits.
- **`filesystem_parseWav()`** stops once it has `fmt ` and `data`. Many
  editors write `smpl` after `data`, so the loop points of Q7 would never
  be seen. The walker must read to the end of the file.
- **Catalogue v2 sample entry (20 B):** offset, frames, name and gain
  code, plus 3 reserved bytes. It has **no loop fields**; Q7 needs them
  (A11).
- **ITCM:**
  - The v1 loops stage D removes: `calcUserSampleOscBlock` 396 B,
    `…FmBlock` 444 B, `…RingBlock` 448 B. Together **1,288 B**.
  - One table sync or ring copy is 176–196 B.
  - Free today: 7,872 B, so ≈ 9,160 B after stage D.
  - **The full copy matrix** would be ≈ 22–28 loops at ≈ 180–300 B, i.e.
    ≈ 4–7 KB:
    - `smp`/`slp` × {plain, FM, sync, ring};
    - `wtb` × {plain, FM, sync, ring} × {8, 16-bit};
    - the blend copies;
    - two builders.

    It fits only with restrictions (A3).

### 11.5 `tools/sampleoptimize.py`: evaluation

**The two copies:** the copies in the two spikes differ only in how they
read the dB thresholds and in their flash constants. Take the pwm-spike
copy.

**What is sound:**
- the "plays the same" criterion: the playback error, read the way the
  oscillator reads;
- band-limited shortening;
- lengthening by linear interpolation, which reproduces exactly what the
  oscillator played;
- `_old` backups and `--dry-run`.

**Defects against the S087 rules:**

1. **Non-power-of-two lengths survive.** A uniform table with no shorter
   passing length is copied unchanged (e.g. 600 points). So is one whose
   shorter form "would not be smaller". §6.4 needs the next power of two
   **even when that is larger**: 600 → 1,024 is +71 % flash for that
   table.
   - **(r2) Superseded by A10:** the optimizer must never output more than
     the original size. A table it cannot shrink is left alone and
     installs verbatim. That only works if the reader accepts any frame
     length (A16).
2. **Extra chunks are dropped.** Re-encoded files lose `smpl`, so loop
   points are lost (Q7). Python's `wave` module cannot write extra chunks,
   so the optimizer needs a small RIFF writer. The `lxrO` flag (§11.6)
   needs the same writer.
3. **Files that need no change are byte-copied,** so they cannot carry a
   flag.
4. **Only the top level of `samples/` is optimized.** Subfolders are
   copied through unchanged, although the installer reads three levels
   deep. A non-conforming file in a subfolder then never installs.
5. **8-bit samples become 16-bit.** This is consistent with the spike
   installer, but contradicts the "never expand" wording of §5.2 (A15).
6. **Wavetables are always written as 16-bit,** so an 8-bit table doubles
   its depth whenever it is re-encoded.
7. **The tail trim (−80 dB) ignores loops.** It must not cut into a
   `smpl` loop. Trimming after the loop end changes the `smp` playback of
   the same item (A4).
   - **(r2) Every sample can loop (A6).** Without `smpl`, the loop is the
     whole sample, so trimming the tail shortens the loop (A18).
8. **Stale constant:** `SAMPLE_FLASH_BYTES` still includes the 61,120 B
   stock block that S086 made unnecessary.
9. **Rate:** files are converted to 44,100 Hz, while `REAL_FS` is
   44,108 Hz. Playback is ≈ 0.3 cents sharp. Harmless; keep.
10. **Single-file wavetables are not split.** The common format (e.g.
    2,048-point frames concatenated in one WAV, with a `clm ` chunk) is
    read as one huge frame (A14).

### 11.6 The zero-crossing bitmap: evaluation

**Status: no implementation exists.** Both spike trees were searched:
code, tools and documents. The bitmap was designed and never built:
- S085 P1 r3: a crossing *list* for the crash, measured, then dropped;
- r4 §12: the bitmap, recorded as future work;
- S086 P1 §2.2/§4.4: planned as stage S4, not reached.

**The recorded design:**
- **Format:** 1 bit per point, set where an upward crossing starts, with
  hysteresis.
- **Storage:** after the item in sample flash. That is 6.25 % of a 16-bit
  item and 12.5 % of an 8-bit `wtb` item.
- **When it is made:** at install, only for files the optimizer flagged
  (an `lxrO` RIFF chunk before `data`). The flag becomes a catalogue flag
  bit.
- **Play time:** a reset-list builder reads the two words covering the
  master's step, finds a set bit with `RBIT` + `CLZ`, and derives the
  overshoot. That is ≈ 12–15 static per sample at constant cost.

**Where it would act:**
- **Only in Drum `snc`.** It acts when osc2 (the master, which is not
  rendered) is a flagged `smp`/`slp`/`wtb`.
- **Nothing else is affected:** no other mode, voice or loop.
- **Cost:** builder +5–8 over the periodic one, in `snc` only (§11.3,
  A1); one builder of ≈ 100–150 B ITCM; flash as above.

**Findings:**
1. **Density decides everything, and hysteresis is undefined.**
   - The crash has one upward crossing every 5.3 points.
   - A sample master moves 1,024 × f / fs points per output sample (≈ 10
     at 440 Hz).
   - So dense material resets the slave on almost every sample. That
     sounds like the "as noise" rule it was meant to improve on.
   - Only a hysteresis rule makes the bitmap worth having. None is
     defined: a fixed fraction of the peak? chosen per file by the
     optimizer?
2. **For wavetables it adds little.** A single-cycle frame usually has
   one upward crossing per cycle. That is the frame-wrap reset shifted by
   a constant phase: S085's A5 argument for table waves. It differs only
   for frames with several crossings.
3. **First or last crossing.** `RBIT` + `CLZ` finds the *first* crossing
   in the step, but the slave's phase should follow the *last* one. A
   plain `CLZ` on the masked window finds the last and saves the `RBIT`.
4. **The 32-point window** covers a sample master only up to ≈ 1.38 kHz
   (1,024 points per nominal period). For frames it reaches ≈ 5.5 kHz at
   256 points and ≈ 1.38 kHz at 1,024. Above that, crossings are missed.
   Masking the window to the step needs a variable shift, which is still
   constant cost.
5. **A one-shot master holds at its end,** so it gives no more resets
   after its first pass. Drum osc2 never resets on a trigger (S085
   §12.4).

**Recommendation:**
- **Build neither the builder nor bitmap generation in S087.** P0 §0.1
  already excludes them, and finding 1 leaves the musical value unproven.
- **But make the stage-C formats bitmap-ready now,** at almost no cost.
  Otherwise the formats need a v3 change later:
  - `filesystem_parseWav()` walks every chunk (needed for `smpl` anyway);
  - the catalogue entry gets a flags byte with one bit reserved for
    "bitmap present";
  - the bitmap position is *derived* (item offset + stored bytes + pad,
    rounded to 4), so no address field is needed;
  - the optimizer's RIFF writer (needed for `smpl` anyway) can carry
    `lxrO`.
- **Before any later build,** run a host listening test (Python, real
  material): bitmap sync with candidate hysteresis rules, against "as
  noise" and the loop-wrap rule. Decide by ear.


### 11.7 What one interpolation slot costs today (r2, r3)

**Source:** loop bodies of the linked image `build/lxr02.elf` (`0e2d61dd`),
dumped with `elf_loopdump.py`. The user-sample loop figures are the
recorded `fpseq.py` numbers (§1.4); their CFG has several paths, so the
loop dump does not give one clean total for them.

**How a slot works today:**
- **The setting:** the Global setting `PAR_OSC_WAVE_INTERP`
  (`modNode_setWaveInterpEnabled()`) turns it on.
- **The claim:** `modulationNode.c` grants a slot to the first two `wav`
  rows written by an LFO or velocity in each block, in any voice and any
  oscillator role. `instrumentManager_waveInterpTarget()` makes every
  `*_wave` row eligible, FM carriers included.
- **A granted oscillator** renders both neighbouring waveforms in full
  into `osc_interp_a/b` (DTCM), then runs a blend loop.

| Role | Path | Blend loop (measured, linked) | Unit overhead over the same role without interpolation, dearest waves (v1 user samples) |
|---|---|---:|---:|
| Plain | `calcPeriodicInterpBlock` (flash) | 15 | (2 × 40–41 + 15) − 40–41 ≈ **55–56** (linked ≈ 56–58) |
| FM carrier | `calcPeriodicInterpFmBlock` (flash) | 15 | (2 × 48–49 + 15) − 48–49 ≈ **63–64** (linked ≈ **65–66**) |
| Sync slave | `calcPeriodicInterpSyncBlock` (inlined, ITCM) | 13 | the user-sample slave renders unsynced: ≈ 53–54 |
| Ring carrier | `calcPeriodicInterpRingBlock` (inlined, ITCM) | 22 (blend + ring law) | (2 × 40–41 + 22) − 46–48 ≈ 54–58 (linked ≈ 58–59) |

**Per-block set-up** comes on top: two 72 B `OscInfo` copies and two
extra dispatches per granted oscillator. It is not measured. Estimate a
few instructions per sample.

**The per-slot reference (r3):**
- **The dearest slot today** is an FM carrier interpolating two v1 user
  samples: ≈ **+66 per sample** over its plain render (linked loops), plus
  the per-block set-up.
- **The S087 check:** a wavetable blend in any role must not greatly
  exceed this per slot (A17). The system budget is not assessed here.
- **Freeze** the linked figure, set-up included, at step 0, before stage D
  removes the v1 loops.

---

## 12. Architectural questions (r1; decisions r2, r3)

### 12.1 Decisions (user, 2026-10-28)

| # | Decision |
|---|---|
| **A1** | The limit is the **oscillator section's worst case**, probably `fm`. Other modes may rise as long as they stay below it; modes are not frozen individually. Drum `snc` with a sample or wavetable slave (≈ 47–61) is allowed. |
| **A2** | Accepted: a per-block `oscType` branch in each engine render, behind one inline helper, calling `.itcm.s087` type renderers; the mixer hot-loop gate on the first build. Removing the v1 `default:` case is an expected, allow-listed dispatcher change. |
| **A3** | Re-evaluated in §12.2. Wavetable frame blending **must** take one of the two interpolation slots and step when none is free; this was a major fault of both spikes. It may lift a section above its ceiling. The per-slot cost must not greatly exceed today's slot (§11.7). |
| **A4** | Accepted: zero padding after every sample; `slp` keeps one next-point select (≈ +2). |
| **A5** | Accepted: the increment is reduced modulo the loop length once per block, every block; the FM copy wraps the read index once each way, then clamps; a `pos` start after the loop end is clamped into the loop; the tail after a `smpl` loop end plays only under `smp`. |
| **A6** | **Every sample can loop.** No fades and no end-click handling for one-shots; the amp envelope is the user's tool. **A loop must be correct when its end is not a zero crossing:** the seam interpolates from the last loop point to the loop's first point (A4's select), never into the zero pad. Host test: a loop whose end and start values differ plays the interpolated seam, with no zero dip. |
| **A7** | **Samples, loops and stock waves never interpolate** when `wav` is modulated; they step. **Only wavetables interpolate (frame blending), always while the global setting is on,** until both slots are taken, then they step. This retires `wav` waveform interpolation: existing kits with an LFO on `wav` will step (no migration). |
| **A8** | Re-evaluated in §12.2. |
| **A9** | Range 16–2,048 points. "Optimizer only, the installer rejects other lengths" is superseded by A10 (A16). |
| **A10** | The optimizer may improve wavetables (shrink, band-limit; later, mip levels), but **its output must not exceed the original size**. Wavetables it does not optimize install **verbatim** and alias. A mip-level set must fit inside the original size, e.g. a shrunken base plus its halvings. |
| **A11** | Accepted: the catalogue at `0x081FE000`, 8,192 B; 28 B sample entries (loop start/end, a flags byte with "looped", "bitmap" and "mip levels" bits); the bitmap position derived. |
| **A12** | Accepted: master clock in the master's own units; `slp` loop-wrap builder; `wtb` periodic (frame wrap); unflagged `smp` as noise; the `pos` start as the slave's reset target; a one-shot master on the never-reset osc2 gives resets on its first pass only. |
| **A13** | Accepted: no bitmap code in S087; the stage-C formats are made bitmap-ready (§11.6). |
| **A14** | Accepted with r2 changes: optimizer items 2–4 and 8 land with stage C. Item 1 is replaced by A10 and A16. Item 7 is A18. Item 10 only on request. |
| **A15** | Accepted: the installer converts sample bit depth and channels on the device, the same way as for frames. **(r6)** The frame converter is re-derived and tested, not copied (§2 rule 10). Rate conversion stays on the host. |
| **A17** | **Two slots, as today,** switched by the global setting. Next session's `run` mode (the spike's rolled-back run modes) will share them. "Budget" here means the system budget, which is not assessed in this document. The S087 check is per slot: a wavetable blend must not greatly exceed today's per-slot cost (§11.7). |
| **A19** | The global toggle `PAR_OSC_WAVE_INTERP` **keeps its menu text and its place.** It switches the two interpolation slots: wavetable frame blending now, and the `run` mode's use later. |
| **A20** | Confirmed. **Changing the table is a step.** The `wav` row (which wavetable plays) never blends. **The frame index (`pos`) is what interpolates.** |
| **A17 addendum (r4)** | A `wtb` carrier can still be FM'd by the modulators, and the modulators can play stock waves, samples or wavetables (A21). The per-slot check and the section worst case must hold for every modulator choice (§12.2). |
| **A21 (r4)** | **The modulators' `wav` list:** the stock waves, then `s01–sNN` (every sample, one-shot), then `l01–lNN` (every sample, looped), then `w01–wNN` (every wavetable, its first frame). The list stops at the 127-entry limit. The details and consequences are in §12.4. |
| **A16 (r5)** | Accepted: **any frame length.** One `UMULL` gives the index and the fraction, with a compare for the next-point wrap (≈ +1–2 per sample, to be measured in the stage-E probe). The power-of-two rule of §6.4 is dropped. The optimizer never grows a table (A10). |
| **A18 (r5)** | Accepted: the optimizer trims a sample's tail **only after a `smpl` loop end**. A sample without `smpl` (its loop is the whole sample) is not trimmed. |
| **A22 (r5)** | **The modulators get no `typ` and no `pos`.** No parameter is added to the FM or Mod Osc sections in S087. Their only expansion is the user content appended to `wav` (A21, §12.4). `typ` exists on osc1 only. |
| **A23 (r5)** | Accepted: the stored value is the list position. A reinstall with different counts changes what a modulator plays, and a position past the end clamps to the last entry. No migration; documented. |
| **A24 (r5)** | Each wavetable **folder** is one entry, and only its **first frame** (the first file in natural order) appears. The list is the stock waves, then `s`, `l`, `w`, cut off at the limit in that order; the last value is 127 (as v1). **Today's card adds three entries:** `s01` = `samples/01_cut.wav` (one-shot), `l01` = the same file, looped, and `w01` = `wavetables/ak_hvox/AKWF_hvoice_0001.wav`, always looped. The table's other 103 frames are not on the list. |

### 12.2 A3, A7 and A8 re-evaluated: two slots, checked per slot

**The slots:** two per block, system-wide, while the global setting is on.
From S087 on, only `wtb` frame blends use them (A7). Next session's `run`
mode will share them (A17).

**The per-slot check (A17):**

| | Cost per sample |
|---|---:|
| Today's dearest slot (§11.7) | ≈ +66 over the plain render |
| A `wtb` blend: a dedicated two-frame copy per role (estimate) | ≈ +11–15 over the one-frame loop |
| Margin | ≈ 50 under |

- **What the blend copy adds:** two more loads, a subtract, a fused
  multiply-add, and the per-sample step of the frame fraction.
- **Not the render-twice-and-blend path:** that would cost a second
  render plus the blend, about +40.
- **Measure** every role (plain, FM, sync, ring) and both depths in
  stage E.

**A `wtb` carrier under FM (r4, A17 addendum):**
- **The slot's cost does not depend on the modulators.**
  - The modulators render once per block into the mod buffer, whether
    the carrier blends or not.
  - The FM blend copy reads that buffer once per sample and applies the
    same read offset to both frames.
  - So the slot adds only the second frame's reads and the blend, ≈
    +11–15, against today's dearest slot of ≈ +66 (itself an FM carrier).
- **The FM depth scale is shared.** Table FM adds `mod × fmMod << 19` to a
  2³² cycle phase. The v1 samples (`<< 14` on `<< 17`) and the spike's
  Q20.12 samples (`<< 9`) apply the same fraction of a 1,024-point
  nominal period. The `wtb` copies use `<< 19` on the frame phase and the
  sample copies `<< 9`, so `amt` means the same for every type. Host test:
  the same modulator gives the same read offset on each type.
- **The section's worst case with modulators of any type** (static,
  estimated, without the blend / with the blend):

  | Voice | Composition | Without blend | With blend | Frozen ceiling |
  |---|---|---:|---:|---:|
  | Drum `fm` | `slp` modulator ≈ 30–33 + `wtb` FM carrier ≈ 35–37 | ≈ 65–70 | ≈ 76–85 | ≈ 92 |
  | Drum `fm` | `slp` modulator + `smp`/`slp` FM carrier ≈ 40–45 | ≈ 70–78 | — (samples step) | ≈ 92 |
  | Cymbal/HiHat | two `slp` modulators + `wtb` FM carrier | ≈ 95–103 | ≈ 106–118 | ≈ 131 |
  | Cymbal/HiHat | two `slp` modulators + `smp`/`slp` FM carrier | ≈ 100–111 | — | ≈ 131 |

  If these estimates hold, a blending `wtb` carrier stays **under** its
  section's frozen ceiling even while it holds a slot. A modulator never
  blends (A22: `wNN` plays the first frame only, §12.4), so only an osc1
  can hold a slot.

**The copy matrix and ITCM (A3):**

| Group | Copies |
|---|---|
| `smp`, `slp` | {plain, FM, sync, ring} × 2 = 8; no interpolation path (A7) |
| `wtb`, one frame | {plain, FM, sync, ring} × {16-bit, 8-bit} = 8 (8-bit stays: wavetables install verbatim, A10) |
| `wtb`, blend | {plain, FM, sync, ring} × {16-bit, 8-bit} = 8 |
| Builders and the type renderer | the `slp` loop-wrap builder; the per-type renderer entry |

- **Size:** ≈ 25 loops at ≈ 180–300 B ≈ **5.5–6.5 KB**, against ≈ 9.2 KB
  free after stage D. About 3 KB would remain for the `run` mode next
  session and for `pwm`.
- **Any frame length is accepted** (A16, r5), so the same loops serve
  every length, with no extra copies.
- **Recommendation:** build the full matrix. Run a size probe at the start
  of stages D and E. If ITCM runs short, drop the sync and ring blend
  copies first: a blending `wtb` in Drum `snc`/`rng` would then step.

**Claims (A8):**
- **When:** at render time, every block, for every `wtb` oscillator,
  while the setting is on (Q9).
- **Sticky, not rotating.**
  - Rotating the first claimant each block would flip oscillators between
    blending and stepping every block, audible at the block rate (≈ 1.38
    kHz).
  - Instead, a holder keeps its slot while it stays `wtb` and the setting
    stays on. A newcomer takes a slot only when one is free.
  - **Mechanism:**
    - At block start, last block's grants are reserved for their holders.
    - A newcomer is granted only if (grants made this block + unclaimed
      reservations) < 2.
    - Each grant is stamped with the block generation. A holder that
      stops rendering (type change, instrument swap) frees its slot after
      one block, so nothing leaks.
- **Sharing with the `run` mode next session:** the S085 order was
  ongoing `itp` crossfades first (never cut), then interpolation, then new
  crossfades. Sticky grants fit that order: an ongoing crossfade already
  holds its slot, and a new one takes only a free slot. **Keep the claim
  code general** (claimant kind, priority), so `itp` slots in without
  rewriting it.
- **Without a slot:** the nearest frame (Q9). With two slots and up to six
  blending `wtb` oscillators (osc1 in each of six slots; A22), most step
  in a dense Scene. That is the intended fallback.
- **State:**
  - a grant stamp in the oscillator: `OscInfo` padding at 34–35, 0 B;
  - two or three global bytes (count, reservations): **needs RAM
    approval**, with region and owner, in the stage-E plan.
- **Retiring `wav` interpolation (A7, A19):**
  - Stop the claim for `wav` rows in `modulationNode.c`.
  - The existing interpolation paths in the dispatchers then stay dormant
    until stage D removes them, together with the v1 `default:` case, as
    one expected identity change. `osc_interp_a/b` (128 B DTCM) are
    released then, unless the stage-E design reuses them. The blend copies
    of §12.2 need no scratch.
  - **Order:** if stage D lands before E, remove the paths and stop the
    claim in D; otherwise stop the claim in E.

### 12.3 Still open

**None (r5).** A16, A18, A22, A23 and A24 are decided (§12.1). The
questions that remain are the per-stage ones: Q2–Q13 where not yet
answered, and D1–D9 for the stage-D plan.

### 12.4 The modulator `wav` list (A21, A22, A24; r4, r5)

**Which oscillators:** Drum osc2 (FM page), Cymbal/HiHat osc2 and osc3.
Snare has no modulator. **No other row on these oscillators changes**
(A22).

| Values | Entries | Plays with |
|---|---|---|
| 0–7 | `Sin Tri Saw Rec Noi Cym Re3 Sw3` (unchanged) | the existing loops |
| 8 … 8 + S − 1 | `s01`…: every sample, one-shot | the `smp` plain loop |
| 8 + S … 8 + 2S − 1 | `l01`…: every sample, looped (fixed loop region, A5) | the `slp` plain loop |
| 8 + 2S … | `w01`…: every wavetable folder, **its first frame** (first file in natural order), always looped | the `wtb` one-frame plain loop at frame 0 |
| last value 127 | the list stops; entries past the limit do not exist (A24) | — |

**With today's card:** S = 1 and one table folder, so the list is 0–7 plus
`s01` (8), `l01` (9) and `w01` (10).

**Rules:**
- **Mapping:** the bind turns the value into (kind, index) once per block
  from the catalogue counts: two compares and a subtract, outside the
  loops.
- **No new copies:** modulators render plain only (Drum osc2 in
  `fm`/`mix`/`rng`; the Cymbal/HiHat modulators), and those copies are
  already in the §12.2 matrix. In Drum `snc`, osc2 is the sync master,
  and its rule follows the entry (§6.3, r4).
- **Never interpolated (A7):** an LFO, velocity or Morph on a modulator's
  `wav` row steps through the list, across kinds. A `wNN` entry is one
  frame, so it never blends and never takes a slot.
- **No `pos` (A22):**
  - a modulator's `sNN`/`lNN` starts at the sample's beginning;
  - a `wNN` starts at the trigger phase the engine already uses for
    periodic waves.
- **Resets are unchanged:**
  - Drum osc2 and the HiHat modulators never reset;
  - Cymbal resets its modulators.
  - So an `sNN` one-shot on a never-reset modulator plays once after
    selection or power-up, then stays silent (S085 R11/R12). This is
    known; no change.
- **Display:**
  - the 8-character name on the left and the code on the right (`s01`,
    `l01`, `w01`);
  - numbers above 99 use the §4.3 form (`sA0`, …).
- **Out of range:** after a reinstall, a value past the end clamps to the
  last entry (A23).
- **What it replaces:** the v1 tail (`wav` 8+ = v1 user samples, looped
  per the v1 install flag), removed in stage D.
- **Stages:**
  - `s`/`l` entries land with stage D, `w` entries with stage E;
  - until D, `wav` 8+ stays v1;
  - stage B does not touch the modulators.

### 12.5 Can `typ` be step-automated? (investigation, r7)

**The ask (user):** `typ` takes step automation. It never ramps, even on a
track with `rmp` on: one value per step.

**Answer: yes.** Ramps already exclude it at no cost, but today's tree
blocks automation of a row that is not morphable. Four changes are
needed, and one rule must be decided.

#### What the current tree does (traced)

| Piece | Where | Finding for `typ` |
|---|---|---|
| Ramp eligibility | `sequencer.c` `seq_rampDtypeOk()` | Only automatable rows whose `dtype & 0x0F` is numeric (`0B127`, `PM63`, `0B255`, `0B15`, `1B16`, `1B128`) ramp. `DTYPE_MENU` (waveforms) and `DTYPE_MIX_FM` (the Drum `mod` row) always jump. **A menu-dtype `typ` never ramps, with no new code.** |
| dtype space | `menu.h` `enum Datatypes` | 16 values, 0–15, all used. A new `DTYPE_OSC_TYPE` would be 16, and the `& 0x0F` read would see `DTYPE_0B255`, which **ramps**. This is the same wrap-around trap the LFO-polarity comment records. **Use `DTYPE_MENU` with a menu-table id instead:** id **4 is free** (`MenuText.h`: 0–3 and 5–15 are used), so `MENU_OSC_TYPE` = 4. |
| Automation target gate | `InstrumentManager.c:797` `instrumentManager_targetValid()` | A voice row must be **`MORPHABLE`** to be any target, automation included. A selector `typ` (not morphable, §4.3) **is refused today**. No current row is automatable without being morphable: every member-bound row uses `FLAGS_IMAGE` (morphable + modulatable + automatable), and `ROW_NOBIND` rows have flags 0. |
| Morph | `presetMorphEngine.c` `presetMorph_interpolate()` | Linear for every morphable row, menus included. A morphable `typ` would pass through `smp`/`slp` between `wav` and `wtb`. **So `typ` cannot simply be made morphable like `mod`;** it stays a selector. |
| Restore before a trigger | `sequencer.c` `seq_restoreAutomatedParameters()` | Every automated (dirty) row of the slot is rewritten from `morph_interpolation[local]`, whether morphable or not. The Morph sweep skips rows that are not morphable, so **nothing keeps `morph_interpolation[typ]` current**. A restore would write a stale value (0 = `wav`). |
| Endpoint and supplemental setters | `presetManager.c:1034`, `:1092` | The endpoint setter refuses rows that are not morphable; the supplemental setter refuses member-bound (`INSTANCE_OFFSET`) rows. There is **no write path today** for a member-bound selector. Stage B needs one anyway (§4.3). |
| Runtime write | `instrumentManager_writeRuntimeInternal()` | No dtype clamp at write time: the raw byte reaches the member or the special writer. So a `wav` byte is not clamped by the `typ` in force when it is written. |
| Automation timing | D6 / stage A | Today the trigger (with its restore) fires first, and the step's automation lands one block (0.73 ms) later. |

#### The changes needed

1. **The dtype:** `typ` = `DTYPE_MENU | (MENU_OSC_TYPE << 4)`, id 4, table
   `wav smp slp wtb`. No ramp code changes; the existing dtype test
   excludes it.
2. **The flags:**
   - `AUTOMATABLE` set;
   - `MORPHABLE` clear (a selector);
   - `MODULATABLE` clear and `MOD_NONE` (no LFO or velocity, like `mod`);
   - plus a new **`INSTRUMENT_PARAM_FLAG_SELECTOR`** (bit 0x08 is free).
3. **The gate:** `instrumentManager_targetValid()` accepts, for
   `INSTRUMENT_TARGET_AUTOMATION` only, a row that is `SELECTOR` and
   `AUTOMATABLE` but not `MORPHABLE`.
   - Modulation still requires `MORPHABLE` and `MODULATABLE`, so LFO and
     velocity stay refused.
   - **This gate is shared** by live record, the automation target menu,
     the drain's validation and MIDI Learn/NRPN. All of them then accept
     `typ`, which is intended. The stage-B plan must list every caller and
     test each one.
4. **The restore value:** every selector apply (menu edit, Kit/Scene/track
   /Instrument apply, slot reset; §4.3) also writes
   `morph_interpolation[typ]` = the stored `typ`. The trigger restore then
   returns the voice to its Kit `typ`. Host test: automate `typ` on one
   step, trigger the next step without an entry, and expect the Kit
   value.

#### The runtime rules automation needs

- **One value per step, held until the voice's next trigger.** At that
  trigger the restore returns the Kit `typ`, unless that step has its own
  entry. This is the existing automation model. Ramps never touch it.
- **Order inside a step must not matter.** A step may hold both `typ` and
  `wav` entries, in either order.
  - The `typ` runtime writer **must not rewrite `wav`**. `wav` stays a raw
    byte, read by whatever type is in force and clamped at the per-block
    bind (`min(wav, count − 1)`).
  - The write path does no clamping, so this already holds.
- **A type change re-anchors** (extends D7).
  - Phase units differ: `wav`/`wtb` use 2³² per cycle, `smp`/`slp` use
    Q20.12 points.
  - Into `smp`/`slp`, the phase becomes the `pos` start (D1).
  - Between `wav` and `wtb`, the phase is kept.
  - From `smp`/`slp` into `wav`/`wtb`, the phase becomes the `pos` trigger
    phase.
  - On a step without a trigger, this is a mid-note switch and clicks.
    That is accepted, as for any mid-note change.
- **It depends on stage A (D6).**
  - Without it, every step that changes `typ` plays its first 0.73 ms in
    the old type. For a sample, that means the wrong source at the attack.
  - Stage A's order must be: restore the held rows → apply the step's own
    entries → fire the trigger. Today the restore runs inside the trigger
    funnel, so stage A must keep the restore ahead of the step's entries.
    `typ` is the strictest case of a rule stage A sets for every row.
- **Cost:**
  - nothing per sample;
  - a type change costs one bind in the next block (the bind already runs
    every block) and one engine-branch outcome;
  - a `wtb` oscillator automated away from `wtb` frees its interpolation
    slot after one block (§12.2).
- **Modulator `wav` rows** (§12.4) are already automatable menu rows. Their
  entries step through the flat list, and nothing changes there.

#### The decision needed: retained request bytes

- **The issue:** §4.3 carries the rollback's rule "`typ → wav → typ`
  restores the previous `wav`/`pos`", i.e. a remembered `wav` per type.
  - If the runtime `typ` writer swaps in a remembered byte, a pattern's
    playback depends on what was last chosen in the menu. It would not be
    reproducible from the pattern.
  - It would also make the order of `typ` and `wav` entries matter.
- **Options:**
  - (a) Keep the remembered bytes **only in the menu edit path**. Turning
    `typ` in the menu also writes the remembered `wav` as an ordinary
    edit. The runtime writer used by automation, restore and apply never
    touches `wav`.
  - (b) Drop the remembered bytes. `wav` is one byte read by the current
    type.
- **Recommendation:** (a).

**(r8) Decided (user): neither (a) nor (b) as written.** Nothing is
remembered and nothing is restored. The `wav` byte is used as it is:
- **On a type switch** the runtime value is clamped to an entry that
  exists for the new type. A menu switch also clamps the stored value.
- **Every block,** the bind clamps it to an entry that exists.
- §4.3's "retained request bytes" rule is withdrawn.
