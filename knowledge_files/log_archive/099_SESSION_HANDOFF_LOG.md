# Session 099 — Chopped PWM Waves, Advanced BBD Chorus Types, AutoSave Trace Starvation Fix

```
DATE: 2026-11-19 / 2026-11-20
SESSION GOAL: Add chopped PWM waves (R-S, REx) to Colloid and Alkaline, create Advanced
              instrument types (clx, alx) with an Alpha Juno BBD chorus, rework the
              Advanced effect page, simplify the chorus rows, improve step editing UX,
              and fix AutoSave trace starvation.
COMPLETED: P1 chopped PWM waves and BBD chorus on both engines (phases P-A through P-E,
           plus review follow-ups including half-rate wet, amp-attack removal and PWM Sin
           removal); P2 device-round fixes (REx removal, R-S narrowing, library save
           freeze fix, effect page rework, chorus simplification, step editing); P3
           AutoSave trace starvation fix (three root causes found and fixed, VOICE
           step-edit return page fix). All code in the tree, builds and host tests green.
VERIFIED ON HARDWARE: P1 chop and chorus acceptance (cycle probes on SD_CARD_ADV/).
  P2 device cycle numbers confirmed (SD_CARD_ADV/asavetrc.bin). AutoSave fixes 1 and 2
  confirmed on device (SD_CARD_ATS_3, SD_CARD_ATS_4). Fix 3 and the §11 VOICE step-edit
  return page fix are built but not yet device-tested.

CHANGES THIS SESSION:
- tools/plots/gen_pwm_tables.py: chop tables (oscChopTables.h/.c), REx removal, Sin removal
- tools/plots/render_chop_audition.py: offline A/B audition renders for R-S
- tools/chorus/gen_chorus_tables.py: BbdChorusTables.h/.c
- Core/DSP/Oscillator/Oscillator.h/.c: OSC_PWM_TRI/SAW/REC, Sin form WS removed
- Core/DSP/Oscillator/oscPwmTables.h/.c: regenerated without Sin tables
- Core/DSP/Oscillator/oscChopTables.h/.c: new generated chop tables
- Core/DSP/Oscillator/oscPwmWarp.h: comments updated
- Core/DSP/Instruments/Colloid/ColloidVoice.h/.c: COL_PWM_* renumbered, chop block,
  col_chopDcInit(), col_chopPairAdd() in .itcm.s099p1, amp-attack minimum removed
- Core/DSP/Instruments/Colloid/ColloidOsc.h/.c: comments updated
- Core/DSP/Instruments/Colloid/ColloidParameters.c: clx rows, fx_* keys, inactive rules
- Core/DSP/Instruments/Alkaline/AlkalineVoice.h/.c: ALK_PWM_* updated, REx removed
- Core/DSP/Instruments/Alkaline/AlkalineChop.c: new file, fused ladder mode 5 chop
- Core/DSP/Instruments/Alkaline/AlkalineParameters.c: alx rows, fx_* keys
- Core/DSP/Instruments/Alkaline/DiodeLadder.h/.c: renderB2OscChop(), -fno-lto
- Core/DSP/Instruments/InstrumentManager.h/.c: clx/alx types, engine map, chorus bind,
  slotRight(), voice unit claim, ADV_FX_* constants, IM_SPECIAL_FX_TYPE
- Core/DSP/Effects/BbdChorus.h/.c: BBD chorus kernel, half-rate Colloid wet, dpt law
- Core/DSP/Effects/BbdChorusTables.h/.c: generated chorus tables
- Core/Menu/menu.h/.c: fx/dpt/rte/mix page, DTYPE_0B127_FX_DEPTH, step edit rename
  rfl→fil, menu_getMenuIndex()/menu_restoreMenuIndex(), library save freeze fix
- Core/Menu/MenuText.h: fxTypeNames, fxDepthNames, pwm wave names updated
- Core/Menu/MenuLabels.c: fx/rte/dpt/mix labels
- Core/Menu/menuPages.h: step edit screen order vel/nte/tim/fil then cdn/rol/rtm
- Core/Hardware/frontPanel/buttonHandler.c: VOICE SHIFT+step edit, held steps flash
- Core/Hardware/SD/filesystem.c: trace flushes last in ladder, winner cache kept,
  filesystem_coreSavePending(), full-rate SD polling, phase 56 clean-byte skip
- Core/Bank/Scene/Autosave.h/.c: autosave_maskByteClean()
- config.h: AUTOSAVE_CRC_BYTES_PER_TICK 512, AUTOSAVE_WRITER_INTERVAL_MS 1000,
  AUTOSAVE_DRAIN_POLL_BUDGET_US 1000
- Core/Hardware/storageTypes.h: clx/alx type tokens
- tools/dsp_test/modules/chorus.c: CH-TAP, CH-GUARD, CH-LAW, CH-MIX0, CH-SLEW,
  CH-SRC, CH-HALF, CH-ALK, CH-INIT, CH-DPT laws and breaks
- tools/dsp_test/modules/colloid.c: CO-CHP, CO-PWG, CO-GATE rewritten
- tools/dsp_test/modules/alkaline.c: AL-CH, AL-TY updated
- tools/dsp_test/modules/oscillator.c: PW1-PW10 Sin cases removed
- tools/dsp_test/Makefile: chorus target, break updates, P4 symbol list
- tools/init/make_init_col.py, make_init_alk.py: init.clx (63 rows), init.alx (35 rows)
- tools/diagnostic_tools/decode_devlogs.py: clx/alx type names
- tools/dsp_test/baselines/s099p1/: colloid_cpu, alk_cost, p4 snapshots
- tools/dsp_test/baselines/s099p2/: colloid_cpu, alk_cost post-P2
- Linker script: .itcm.s099p1 subsection after .itcm.s096p3

KNOWN ISSUES INTRODUCED: None.
KNOWN ISSUES RESOLVED: AutoSave trace starvation (diagnostic flush ahead of core writer);
  Colloid Advanced library save freeze (menu refusal left editModeActive stuck);
  VOICE step-edit return to wrong subpage.

NEXT SESSION RECOMMENDED GOAL: Device-test fix 3 (AutoSave full-rate polling and
  clean-byte skip) and the VOICE step-edit return page fix per §14 of P3. Then run
  the deferred P2 device matrices (R-S by ear, REx removal on stored Scenes, effect
  page and step edit UX). Then new feature work or the R-S low-frequency alias fix.
BLOCKERS: Fix 3 and the §11 fix not yet device-tested. The R-S low-frequency alias
  (P2 §6) is deferred until tested on device.

CRITICAL REMINDERS FOR NEXT SESSION:
- The AutoSave trace rule §0 is binding and permanent: diagnostics must NEVER take
  priority over core system functions. See P3 §0 for the six sub-rules.
- Fix 3's filesystem_coreSavePending() gate prevents trace flushes from starting
  while any save is pending. An in-flight flush is NOT cancelled; it finishes first
  at full poll rate.
- The validated-winner cache is kept after a clean drain (fix 2). Every lifecycle
  path that is not the clean-drain completion still clears it.
- AUTOSAVE_WRITER_INTERVAL_MS is now 1,000 ms (was 5,000). It also spaces the error
  retry and the non-semantic Pattern drain arm.
- AUTOSAVE_CRC_BYTES_PER_TICK is now 512 (was 128). The _Static_assert still holds.
- AUTOSAVE_DRAIN_POLL_BUDGET_US 1000u: 1 ms of full-rate SD polling per main-loop
  pass for core storage ops. The trace flush is also polled at full rate when it is
  already in flight.
- The Colloid's amp-attack minimum (S096 P3) is removed. All envelopes run a plain
  ADSR from the gate. A step shorter than the amp attack now releases mid-attack.
- BBD_COL_HALF_RATE (default 1) selects half-rate wet on the Colloid. The Alkaline
  always runs at full rate.
- REx is removed. COL_PWM_WAVE_COUNT 4, ALK_PWM_WAVE_COUNT 4. A stored REx byte
  (4) plays Rec on Colloid, R-S on Alkaline, with blank menu name until edited.
- PWM Sin is removed from all engines. Drum family pwm: Tri/Saw/Rec (OSC_PWM_WAVE_COUNT
  3). Colloid pwm: Tri/Saw/Rec/R-S (COL_PWM_WAVE_COUNT 4). The wav lists are unchanged.
- R-S width uses oscPwmDRec (0.39% floor), not oscPwmDSaw (2.54%). This supersedes D-5.
- Advanced types clx (Colloid) and alx (Alkaline) are INSTRUMENT_TYPE_CLX 7 and
  INSTRUMENT_TYPE_ALX 8. Engine map routes them to their Basic counterparts' engines.
- The effect page (fx/dpt/rte/mix) is on page 4 (5th SELECT click). Keys are fx_type,
  fx_depth, fx_rate, fx_mix. The old chorus_* keys no longer load.
- The dpt law: 0 I, 1 II, 2 1+2, 3–127 var→dbl. The path is continuous and never falls.
- Colloid 63 rows (csr removed), Alkaline 35 rows (tone/drive/width removed).
- Step edit: vel nte tim fil | cdn rol rtm. rfl renamed fil.
- DiodeLadder.o and AlkalineChop.o are -fno-lto. This prevents Drum loop regressions
  but costs the Alkaline +5 instructions per block on every mode.
```

---

## Source Documents

This log retains all relevant detail from the following documents, which are deleted
after this log is written:

- `S099_P1_PWM_ADVANCED.md` — Plan (§1–§9, six phases P-A through P-F, decisions Q-1 through Q-16)
- `S099_P1_IMPLEMENTATION.md` — Line-level schedule (§0–§12), implementation log (§13), review (§14), follow-ups (§15)
- `S099_P2_ADV_FIXES.md` — Device-round fixes (§1–§11), gates (§7)
- `S099_P3_AUTOSAVE_TRACE_FIX.md` — AutoSave trace starvation (§0–§15), VOICE step-edit return (§11)

---

## Targets and Decisions

**Targets (user, 2026-11-19):**

- **P1.** Add chopped PWM waves (R-S, REx) to the Colloid and Alkaline engines, and
  create Advanced instrument types (clx, alx) with an Alpha Juno BBD chorus.
- **P2.** Fix device-round issues: REx removal, R-S narrowing, library save freeze,
  effect page rework, chorus simplification, step editing improvements.
- **P3.** Fix AutoSave trace starvation so that edits are durable within 5 seconds.

**Decisions (user, 2026-11-19):**

| # | Question | Decision |
|---|----------|----------|
| Q-1 | Scope and order | P-A → P-E, with P-F (drum-family chop) deferred. P-B and P-C on separate images |
| Q-2 | Drum-family chop | Deferred |
| Q-3 | ITCM priority for the chop loop (≈308 B of 424 B free) | Approved. D-10 not now. C2 drops off the chorus fallback list |
| Q-4 | Which hosts get REx | Colloid and Alkaline |
| Q-5 | REx curve by ear at P-A | Yes, the renders decide k and orientation |
| Q-6 | Advanced Colloid pass criterion | Median and p95 ≤ 21,803 cycles/block |
| Q-7 | Fallback authority if over | C1 and C3 without asking; C4 and C5 need user |
| Q-8 | Unit claim timing | On cho ≠ off; cho is a fixed selector, not automatable |
| Q-9 | Tokens, extensions, folders | clx / alx, .clx / .alx, /Instrument/ColAdv and /Instrument/AlkAdv |
| Q-10 | Chorus defaults | var rate 0.1–10 Hz log; cutoff 9 kHz tracking; equal-power mix; dry gain in |
| Q-11 | S089 P2 ceiling reading | Met when no Basic dearest rises and Advanced spends only the reserved share |
| Q-12 | Advanced Alkaline options | 12 dB wet always on; rows cbw, cdv, cwd (later removed in P2 §10) |
| Q-13 | S098 P0 on hardware | Tested and archived; no separate image needed |
| Q-14 | Level law and width floor | Peak-keeping level, oscPwmDSaw widths 2.54% floor (later narrowed to 0.39% in P2 §2) |
| Q-15 | Gate row lift | Lift 2 on both hosts |
| Q-16 | Audition renders location | Session scratchpad |
| — | Pos spread under LFO modulation (P2 §5) | Leave as-is (the fold law gives the best harmonic/cancellation trade-off) |
| — | R-S low-frequency alias (P2 §6) | Deferred until device tests |
| — | Colloid Advanced csr (P2 §10) | Removed; derived from route |
| — | Alkaline tone/drive/width (P2 §10) | Removed; derived from post-filter drive/depth |
| — | Effect screen placement (P2 §10) | Page 4 (5th SELECT click), not MIX |
| — | PWM Sin (P1 §15.10) | Removed from all engines |
| — | Amp-attack minimum (P1 §15.4) | Removed; all envelopes run plain ADSR from gate |

**RAM sign-off:** No new SRAM1 bytes. ColloidVoice +12 B (1,212 → 1,224) and
AlkalineVoice +76 B (564 → 640), both inside the existing 1,264 B union slot. ITCM:
306 B for col_chopPairAdd() in .itcm.s099p1, later offset by 856 B freed from PWM Sin
removal (net: −550 B ITCM). DTCM: voice units claimed from existing FxBuffer arena
(4,416 B per chorus slot while fx ≠ off).

---

## Phase P1 — Chopped PWM Waves and the Advanced BBD Chorus

### P-A: Offline Tables and Audition

Extended `tools/plots/gen_pwm_tables.py` with chop tables in a new generated pair
(`oscChopTables.h/.c`): `oscChopCurveRs[1024]` (the linear identity curve for R-S),
gate gain tables (`oscChopGainQ14`), `oscChopSpanF` / `oscChopInvSpanF`, and DC, level
and release tables per pos. Also generated the REx curve tables (later removed in P2).
Checks T10–T15 added to the `--check` mode. The existing `oscPwmTables.h/.c` stayed
byte-identical.

Offline audition renders (`tools/plots/render_chop_audition.py`) for R-S and REx at
multiple frequencies (55, 110, 440, 1760 Hz), positions (0, 24, 48, 96) and as pos sweeps,
with and without form T gate. Used to decide k = 5 for REx and high-first gate
orientation.

### P-B: Alkaline R-S / REx

Fused ladder mode 5: `DiodeLadder_renderB2OscChop()` in new `AlkalineChop.c` (compiled
`-fno-lto` to prevent Drum loop regressions). The dispatch in `alk_renderPwm()` calls
`Alkaline_renderChop()` for wave ≥ ALK_WAVE_RS. Mode 5 reuses `ff0` / `ffStep` / `rowB`
from the existing fused loop.

`ALK_PWM_WAVE_COUNT` set to 5 (Saw Sqr Tri R-S REx). Per-type clamp in
`Alkaline_setOscType()`. `DiodeLadder.o` also `-fno-lto` to keep the Drum loops exact.

Mode 5 cost: 262.8 per sample (≤ 263.7 target). Alkaline per-block cost: 1,370 (+5 from
`-fno-lto`, accepted as the cost of keeping Drum loops exact). Host laws: AL-CH (chop
form 2 against form 0), AL-TY (pwm clamp to R-S). Flash: 728,144. ITCM: 15,984.

### P-C: Colloid R-S / REx (Form T)

`col_chopDcInit()` (flash): computes the chop DC correction per block from start and end
widths, with no stored glide state. `col_chopPairAdd()` (306 B in ITCM section
`.itcm.s099p1`, placed after `.itcm.s096p3`): one loop for both R-S and REx, normalises
each oscillator's gate with a packed Q14 gain per pair and applies each oscillator's level
through the existing bus multiplies. Two-pass `col_chopBlock()` for the six-oscillator
mean (pass 1) and pair normalisation with chop application (pass 2).

`COL_PWM_WAVE_COUNT` 6 (Sin Tri Saw Rec R-S REx, before PWM Sin removal). The
`_Static_assert` changed form to `OSC_PWM_WAVE_COUNT + 2`. Host laws: CO-CHP (R-S and
REx), CO-PWG re-run with chop modes. ITCM: 16,288 / 16,384 (96 B free). Flash: 729,736.

### P-D: Advanced Colloid (clx)

`INSTRUMENT_TYPE_CLX` 7, the 8th instrument type, Advanced class. Token `clx`, extension
`.clx`, folder `/Instrument/ColAdv/`. The engine map (`instrumentManager_typeEngine()`)
routes `clx` to the Colloid engine. The clx runs the same oscillator, filter and amplifier
as the Basic Colloid, then a BBD chorus pass.

**BBD Chorus Kernel** (`BbdChorus.h/.c`): Alpha Juno-style bucket brigade delay chorus.
Five loop functions: `bbd_sideD` / `bbd_sideH` (full/half-rate L-R), `bbd_midA` /
`bbd_midH` (full/half-rate mid), `bbd_tapB` / `bbd_tapBH` (full/half-rate pass B).
Control: `bbdChorus_control()` with `bbd_sidePlan()` (inlined) computing read positions
and the tone/drive coefficients. Init: `bbdChorus_init()`. Tables: `BbdChorusTables.h/.c`
from `tools/chorus/gen_chorus_tables.py`.

**Chorus design:**
- Ring buffer in the voice unit's FxBuffer allocation (4,416 B DTCM arena claimed per
  chorus slot while fx ≠ off).
- Two delay lines (L and R), each with a pre-pole, ring write, interpolated tap read,
  post-pole, tone filter, drive clip and wet/dry mix.
- Poles use the complement form (`y = x + (y − x)(1 − k)`) so cutoffs above 4.87 kHz
  are reachable (the direct form cannot hold k > 0.5).
- Dry gain: `out = x·gd + wet·gw`, with `gn = gd − 32768` so cmx 0 returns the input
  bit-exact (CH-MIX0).
- Longest delay: 990 samples (22.4 ms), not 1,022 (the mid form's pass B reads ring R
  after pass A has written the block's 32 samples).

**Voice unit claim:** on `cho` ≠ off via `IM_SPECIAL_CHORUS_TYPE` (later renamed
`IM_SPECIAL_FX_TYPE`) fixed selector. `instrumentManager_slotRight()` reads a per-slot
pointer table (24 B SRAM1). `instrumentManager_unitsWanted()` returns the unit count.
No handoff entries for the chorus unit.

ColloidVoice +12 B (1,212 → 1,224). 64 rows, 8 pages. `colloidParameters_cellInactive()`
extended for chorus rows. Host laws: CH-TAP, CH-GUARD, CH-LAW, CH-MIX0, CH-SLEW, CH-SRC,
CH-HALF, CH-ALK, CH-INIT. Flash: 734,688.

### P-E: Advanced Alkaline (alx)

`INSTRUMENT_TYPE_ALX` 8, the 9th instrument type, Advanced class. Token `alx`, extension
`.alx`, folder `/Instrument/AlkAdv/`. Mono-in chorus form
(`bbdChorus_renderAlkaline()`), stereo output via `right[32]`. AlkalineVoice +76 B
(564 → 640). 38 rows, MIX page with vol/pan/ton/drv plus chorus rows. Flash: 735,584.

### Review and Follow-Ups (§14–§15)

**F1 (Advanced Colloid over budget at p95):** the chorus measured 57.0 instructions/sample
against a target of ≈49. Trims applied: pre-pole state kept at sample scale (no shift on
ring store); writing loops end on write count (eliminating a spilled end pointer); guard
test reduced to one compare; loops inlined via `always_inline`; `bbd_sidePlan()` inlined.
Result: 51.8 instructions/sample. Still over target.

**C3 (half-rate wet) applied:** `BBD_COL_HALF_RATE` (default 1). Rings written and read at
fs/2; each ring sample is the mean of two pre-pole outputs (zero at 22 kHz). One
interpolated tap per output pair with linear interpolation between ring samples. Pre-pole,
post-pole and dry+wet mix stay at full rate. New loops: `bbd_sideH` (41/39 per pair),
`bbd_midH` (52), `bbd_tapBH` (29). The dearest form (mid) costs 47.0
instructions/sample. Host law CH-HALF confirms: images at −38 to −20 dB, folds at −14 to
−17 dB. The Alkaline always runs at full rate.

**F2 (Alkaline +5/block from -fno-lto):** neither Alkaline worst case (Basic 16,591,
Advanced est. 19,670 at CPI 1.6) is over its cap. No change made.

**Amp-attack minimum removed (§15.4):** the Colloid note is now the gate alone. All three
envelopes (amp, filter, pitch) run a plain ADSR from the gate. `col_egStep()` lost its
`isAmp` argument. `heldFP` removed. A step shorter than the amp attack now releases
mid-attack. Cost: every Colloid mode's per-block cost fell by 56 instructions. CO-GATE
rewritten. Flash: −120 B.

**PWM Sin removed from all engines (§15.10):** decision by user. Drum family pwm: 0 Tri,
1 Saw, 2 Rec (`OSC_PWM_WAVE_COUNT` 3). Colloid pwm: 0 Tri, 1 Saw, 2 Rec, 3 R-S, 4 REx
(`COL_PWM_WAVE_COUNT` 5). Alkaline unchanged (never had Sin). The wav lists are
unchanged. No migration: a stored Sin byte shifts values up by one. Flash: −2,592 (freed
Sin tables and WS ITCM loops). ITCM: −856 (five `osc_pwmWs*Block` loops removed). Final
P1: flash 733,144 (headroom 20,520); ITCM 15,432 (952 B free).

**P1 deviations from the schedule (13 total):** chop tables in new generated pair;
per-oscillator normalisation and level in the chop loop; float level tables; chop DC from
start/end widths with no stored state; chop pass in the sync block; chorus complement-form
poles; dry gain via `gn = gd − 32768`; longest delay 990 not 1,022; no chorus handoff
entries; `slotRight()` via pointer table; `slotType()` returns registry type; Colloid's
`isSin` spelling for register allocation; guard refresh as one compare.

### P1 Build (final, after §15.10)

| Measure | Start | After P1 | Delta |
|---------|-------|----------|-------|
| Flash | 720,776 B | 733,144 B | +12,368 |
| ITCM | 15,960 B | 15,432 B | −528 |
| ColloidVoice | 1,212 B | 1,224 B | +12 |
| AlkalineVoice | 564 B | 640 B | +76 |
| Union slot | 1,264 B | 1,264 B | 0 |

---

## Phase P2 — Advanced Fixes After the First Device Round

Input: the user's device round on `SD_CARD_ADV/` (a DEV_MODE_LOGGING build of the P1
§15.10 tree, image 733,160 B).

### §1: REx Removed

By ear, REx was too close to R-S. Removed from both engines:

| Engine | Before | After |
|--------|--------|-------|
| Colloid (Basic and Advanced) | Tri, Saw, Rec, R-S, REx | **Tri, Saw, Rec, R-S** (`COL_PWM_WAVE_COUNT` 4) |
| Alkaline (Basic and Advanced) | Saw, Sqr, Tri, R-S, REx | **Saw, Sqr, Tri, R-S** (`ALK_PWM_WAVE_COUNT` 4) |

Code removed: the REx curve (`rex_s`, `rex_S`, `CHOP_REX_K`) and every REx table from
`gen_pwm_tables.py`; `COL_PWM_REX`, `ALK_WAVE_REX`; the REx selects in `col_chopBlock()`
and `Alkaline_renderChop()`; `OSC_CHOP_REX_K` and `OSC_CHOP_LREF_REX`. Menu names and
asserts updated. Laws: AL-TY, AL-CH, CO-CHP, CO-PWG and the CO-PWM form bus updated.

A stored REx byte (4) is not migrated: plays Rec on Colloid (clamp past list), R-S on
Alkaline (`>= ALK_WAVE_RS`), blank menu name until edited.

### §2: R-S Narrowed to 0.39%

Width law changed from `oscPwmDSaw` (2.54% floor, decision D-5) to `oscPwmDRec` (0.39%
floor, 4 points at pos 127), linear in pos. pos 0 unchanged (50%, the Alpha Juno Saw 2).
Peak-keeping level stays tame: ×1.78 at pos 0, ×2.00 at pos 127 (`OSC_CHOP_LREF_RS`
1.996). Colloid code: removed the `col_oscControl()` overwrite with Saw's widths (the Rec
widths already written are now used). Alkaline code: `Alkaline_renderChop()` reads
`oscPwmDRec[k]`. CO-CHP gained a check that every R-S width equals `oscPwmDRec[pos]`.

### §3: Colloid Advanced Library Save Freeze Fixed

The OK press on a nested Instrument Save was refused (filesystem busy with trace flush),
but `menu_instrumentSaveRequestSelection()` did nothing on refusal, leaving
`editModeActive` stuck. This is the same freeze S097 P3 fixed for top-level Load/Save
rows.

Fix: on refusal, `menu_loadSaveDropClick(MENU_LS_CLICK_REFUSED)` restores the toggle and
the press is held for replay (`menu_loadSaveOkRetry`, up to `MENU_LS_OK_ATTEMPTS` 4). On
acceptance, the click is traced (stage `k`, ACCEPTED).
`menu_loadSaveOkRowHoldable()` now admits nested Instrument Save.

### §4: Device Cycle Numbers

From `SD_CARD_ADV/asavetrc.bin`, slot 3 as clx:

| Measure | Before S099 (Basic col) | S099 (clx) | Cap |
|---------|------------------------|------------|-----|
| Median cycles/block | 19,008 | **21,034** | 21,803 |
| p95 | 20,267 | **22,397** | 21,803 |
| Max | 21,482 | **23,076** | — |
| Whole mixer (largest avg window) | 92.7% | **94.4%** | 97% |

The Advanced step costs +2,026 cycles at the median. The host model predicted 2,025 at
CPI 1.4, confirming the instruction model holds. The p95 failure comes from the Colloid
itself (its Basic p95 20,267 was already over the Basic share 19,620).

### §5: Pos Spread Under LFO Modulation

Simulation showed the current fold law gives the best harmonic swing (34.2 dB at psp 0)
and changing cancellation (2.8 dB at mid psp). A full-cycle even lead/lag spread is nearly
static (4.5 dB harmonic, 0.4 dB cancellation). Decision: leave unchanged.

### §6: R-S Low-Frequency Alias

Measured: truncated gate reads are the main source (−40 to −50 dB inharmonic energy).
Interpolating the gate gives −57 dB alone, −75 to −85 dB with an exact ramp. Options E–H
documented with costs. Decision: deferred until device tests.

### §9: Advanced Effect Page (fx / dpt / rte / mix)

The chorus rows `cho` / `crt` / `cdp` / `cmx` renamed to `fx` / `dpt` / `rte` / `mix`
with keys `fx_type` / `fx_depth` / `fx_rate` / `fx_mix`. The old `chorus_*` keys no
longer load.

**fx** (Effect Type): off, chr. A fixed selector (`IM_SPECIAL_FX_TYPE`) whose writer
claims or releases the voice unit. `ADV_FX_OFF` / `ADV_FX_CHORUS` / `ADV_FX_COUNT`.

**dpt** (Effect Depth): `DTYPE_0B127_FX_DEPTH`, a tagged `DTYPE_0B127`. Values 0–2
display I, II, 1+2 (the Juno modes, which ignore rate); 3–127 display the number and
follow rate. The dpt law (`bbd_target()`):

| dpt | LFO rate | Centre (samples) | Sweep depth |
|-----|----------|-------------------|-------------|
| 0 I | 0.513 Hz | 154.6 | 81.4 |
| 1 II | 0.863 Hz | 154.6 | 81.4 |
| 2 1+2 | 9.75 Hz | 154.4 | 8.8 |
| 3→65 var | rte | 154.6 | 0 → 81.4, linear |
| 65→127 var→dbl | rte | 154.6 → 662 | 81.4 → 110 |

The path is continuous, never falls, and the depth's 1/8 slew and centre's 16-sample step
limit keep every dpt move a glide. Host law CH-DPT added.

**rte** (Effect Rate): 0–127, `bbdRateHz[]` (0.1–10 Hz). Not blanked under I, II or 1+2.

**mix** (Effect Mix): unchanged.

Category: the four rows are now "Effect" (`CAT_EFFECT`). Every effect cell except fx is
blank only with fx off.

### §10: Chorus Simplification

`csr` removed from clx (derived from route: L-R when stereo, mid when mono serial).
`cbw`/`cdv`/`cwd` removed from alx (tone fixed at `BBD_ALK_TONE` 73 / 9 kHz; drive
follows post-filter drive byte; width from `BBD_ALK_WIDTH(dpt)`). Effect screen moved to
page 4 (5th SELECT click). Colloid 64→63 rows. Alkaline 38→35 rows.

### §11: Step Editing

Screen order: vel / nte / tim / fil (screen 1), cdn / rol / rtm (screen 2). `rfl` renamed
`fil` (category "Step", long name "Fill"). Behaviour unchanged.

**VOICE mode SHIFT+step:** opens step edit page (SEQ_PAGE sub-page 1); further steps join;
every held step flashes; edits go to every held step; released step stops flashing; last
release returns to the VOICE page that was showing. `bh_voiceStepEdit` stores cursor+1 on
open, restores on last release. A mode change closes the edit.

**STEP mode:** every held step now flashes (not just the selected one).
`led_clearAllBlinkLeds()` runs only when no other step is held.

### P2 Build

Flash after §9: 730,296 B (headroom 23,368). Flash after §10/§11: 730,232 B (headroom
23,432). ITCM: 15,432 unchanged. `elf_s099_check` 15/15 exact. Host costs unchanged from
P1. Colloid host: chop6-par 387.8, clx-pwm6-par 449.7. All 87 breaks caught.

---

## Phase P3 — AutoSave Trace Starvation and Save Latency

### §0: The Binding Rule (Permanent)

**Development logging, tracing, probes and every other diagnostic must NEVER take priority
over a core system function.** Core functions: AutoSave (scalar parameter drain, Pattern
drains), settings/MIDI Learn/HCNAMES persistence, Load/Save, audio, sequencer and UI.

Six sub-rules:
1. Last in the queue. No exceptions.
2. Never block, delay or reorder core work.
3. A logging build must behave like a release build for every core function.
4. Review every new diagnostic against rules 1–3 before it ships.
5. Core persistence is designed and measured against its real deadline: an edit is durable
   within 5 s.
6. Being last in the queue is not enough. A diagnostic must not even start while core work
   is pending.

### Root Cause 1 (§3): Trace Came Before the Writer

`filesystem_tick()` hands the filesystem facade to background claimants in a fixed order.
Session 048 (commit `599b992b`) placed the AutoSave Trace flush ahead of the AutoSave
scalar writer on purpose ("persist a pending diagnostic batch before an eligible AutoSave
drain"). A logging build always has pending trace records (the S094 P2 cycle probe writes
16 records every 1.5 s), so the trace flush took the facade in nearly every idle pass.
The writer could never arm (boot 4: no S record) or admit a due drain (boot 3: armed but
never admitted). Evidence: `SD_CARD_ADV_2`, 82–91% of pass probes inside trace flushes.

### Fix 1 (§4, Commit `a0ebcf08`)

Moved both diagnostic flushes (AutoSave Trace, PatternTrace) to the end of the
`filesystem_tick()` ladder, after every core claimant. New order:

1. MIDI Learn writer
2. Settings writer
3. Deferred HCNAMES flush
4. **AutoSave scalar writer**
5. AutoSave Pattern drain
6. AutoSave non-semantic Pattern drain
7. AutoSave Trace flush (diagnostic)
8. PatternTrace flush (diagnostic)

Removed the 17-line comment that justified the old placement. Every gate, debounce,
deadline, file format and drain state machine unchanged. Flash: 730,304 B.

### Root Cause 2 (§9): 13–14 s Save Time

Even with fix 1, the writer armed at the first edit but a save took 13–14 s: 5 s debounce
(`AUTOSAVE_WRITER_INTERVAL_MS 5000`) + 3.7 s CRC validation (2 × 34,768 B at 128 B/tick)
+ 4.8 s copy. An edit followed by "wait 5 s, power off" could never survive.

### Fix 2 (§9.1, Commit `dd81f19c`)

1. `AUTOSAVE_CRC_BYTES_PER_TICK` 128 → 512: reads the whole `staging_buf` per tick. The
   existing `_Static_assert` still holds. Cuts validation, copy and boot validation to 4×
   fewer passes.

2. The validated-winner cache is kept after a clean drain
   (`filesystem_autosaveWriterCompleted()`). Before, the clean branch cleared
   `fs_autosave_winner_cached`. Now: if `have_winner`, the cache is set to the new target
   (winner ^ 1, generation + 1, probe + 1 after a publish; unchanged after a no-publish).
   The consumer (drain phase 0) loads from the cache, skips validation (phases 1–5) and
   goes straight to phase 56. Every other lifecycle path still clears the cache (11 sites
   checked). No new RAM (reuses existing cache variables).

3. `AUTOSAVE_WRITER_INTERVAL_MS` 5,000 → 2,000 ms (later cut to 1,000 in fix 3).

Device result (`SD_CARD_ATS_4`): cache worked (V record flags 0x08, 0 ms validation). But
save still took 14.2 s due to the SD driver's poll rate.

### Root Cause 3 (§10.3): Trace Flush Still Blocked; Single SD Poll; Slow Mask Scan

1. The trace flush still blocked the writer between edits: it held the facade for hundreds
   of ms while seeking and appending to the multi-megabyte `asavetrc.bin`.
2. Core storage was polled once per main-loop pass, each poll moving 16 bytes
   (`SDCARD_BURST_SIZE`). At 4–24 ms per pass, a 512 B sector took 130 ms.
3. Phase 56's capture scan walked 30,848 bits one at a time under a 50 µs budget cap:
   557 ms.

### Fix 3 (§10.4, Commit `dd81f19c`)

1. **`filesystem_coreSavePending()`** gate: no trace flush starts while any save is
   pending. Checks `autosave_maskHasDirty()`, `fs_autosave_writer_armed`,
   `fs_autosave_recovery_pending`, Pattern and non-semantic Pattern dirty masks, settings
   dirty, and HCNAMES dirty. Inside `#if DEV_MODE_LOGGING` (release builds have no trace
   scheduler). An in-flight flush is NOT cancelled; it finishes first at full poll rate.

2. **`AUTOSAVE_DRAIN_POLL_BUDGET_US 1000u`:** full-rate SD polling loop for core storage
   ops. After the existing one-or-four `afatfs_poll()`, if the op is parameter drain,
   Pattern drain, flush finish, or trace flush (in-flight only), a `while` loop polls for
   up to 1 ms of `timebase_tim2Now()` time. Expected: about one sector per 1 ms.

3. **`autosave_maskByteClean()`** in phase 56: skips clean mask bytes (8 bits at a time
   instead of 1). A single-byte read of `autosave_dirty_mask[]` is atomic on Cortex-M7.
   If an interrupt sets a bit just after the read, it stays set and the writer's 250 ms
   continuation captures it. Expected: 3,856 byte reads instead of 30,848 locked bit takes.

4. **`AUTOSAVE_WRITER_INTERVAL_MS` 2,000 → 1,000 ms.**

Expected timing: edit → durable ≈ 2 s (from 14.35 s). Not yet measured on device.

### §11: VOICE Step-Edit Return Page

`menu_getMenuIndex()` / `menu_restoreMenuIndex()` (new accessors in `menu.c`): save and
restore the menu cursor with proper `lockPotentiometerFetch()`, `editModeActive = 0`,
`menu_endlessPotMappingChanged()`, `va_refreshAutomationLeds()` and repaint.
`bh_voiceStepEdit` stores `menuIndex + 1` on open (was `= 1u`). On last release, the
saved cursor is restored after `menu_switchPage()`. Not yet device-tested.

### P3 Build

Flash: 730,936 B (730,952 B on disk). No new RAM. No new warnings. Host suite: exit 0,
87 breaks caught. ITCM: 15,432 unchanged.

### P3 Evidence Summary

| Claim | Status |
|-------|--------|
| Trace-first ordering starved the writer | Proven (ADV_2: boot 4 no S, boot 3 never admitted, 82–91% trace probes) |
| Fix 1 lets the writer arm | Proven on device (ATS_3: S at first edit) |
| 5 s debounce + 128 B chunk cannot meet 5 s | Proven (arithmetic from measured drain times) |
| Fix 2's cache skips validation | Proven on device (ATS_4: V flags 0x0b, 0 ms) |
| Fix 2 published a runtime edit that survived reboot | Proven on device (ATS_4: generation 2,827) |
| Save still took 14.35 s after fix 2 | Proven (ATS_4 step-by-step trace) |
| Fix 3 gets edit durable in ≈2 s | Not yet measured (estimate from §10.5) |
| §11 returns to previous subpage | Built but not tested |

---

## Files Changed

| File | Change | Phase |
|------|--------|-------|
| `tools/plots/gen_pwm_tables.py` | Chop tables (oscChopTables.h/.c), REx removal, Sin removal, checks T10–T15 | P1, P2 |
| `tools/plots/render_chop_audition.py` | Offline A/B audition renders for R-S | P1 |
| `tools/chorus/gen_chorus_tables.py` | BbdChorusTables.h/.c | P1 |
| `Core/DSP/Oscillator/Oscillator.h` | `OSC_PWM_TRI/SAW/REC`, `OSC_PWM_WAVE_COUNT` 3, Sin form WS removed | P1 |
| `Core/DSP/Oscillator/Oscillator.c` | Form WS loops removed, `osc_pwmPlan()` Tri/Saw/Rec, role renderers D and WT only | P1 |
| `Core/DSP/Oscillator/oscPwmTables.h/.c` | Regenerated without Sin tables | P1 |
| `Core/DSP/Oscillator/oscChopTables.h/.c` | New generated chop tables | P1 |
| `Core/DSP/Oscillator/oscPwmWarp.h` | Comments updated | P1 |
| `Core/DSP/Instruments/Colloid/ColloidVoice.h` | `COL_PWM_*` renumbered, `COL_PWM_WAVE_COUNT` 5→4, `COL_PWM_IS_WARP` updated, `_Static_assert` | P1, P2 |
| `Core/DSP/Instruments/Colloid/ColloidVoice.c` | `col_chopDcInit()`, `col_chopPairAdd()` (.itcm.s099p1), `col_chopBlock()`, `col_egStep()` amp-attack removed, `heldFP` removed, REx removed, Sin removed | P1, P2 |
| `Core/DSP/Instruments/Colloid/ColloidOsc.h/.c` | Comments updated | P1, P2 |
| `Core/DSP/Instruments/Colloid/ColloidParameters.c` | clx rows, `fx_*` keys, inactive rules, 64→63 rows | P1, P2 |
| `Core/DSP/Instruments/Alkaline/AlkalineVoice.h` | `ALK_PWM_WAVE_COUNT` 5→4, `ALK_WAVE_REX` removed | P1, P2 |
| `Core/DSP/Instruments/Alkaline/AlkalineVoice.c` | Comments updated | P1, P2 |
| `Core/DSP/Instruments/Alkaline/AlkalineChop.c` | New file: `Alkaline_renderChop()`, fused mode 5, `-fno-lto` | P1, P2 |
| `Core/DSP/Instruments/Alkaline/AlkalineParameters.c` | alx rows, `fx_*` keys, 38→35 rows | P1, P2 |
| `Core/DSP/Instruments/Alkaline/DiodeLadder.h/.c` | `DiodeLadder_renderB2OscChop()`, `-fno-lto` | P1 |
| `Core/DSP/Instruments/InstrumentManager.h` | `INSTRUMENT_TYPE_CLX/ALX`, `INSTRUMENT_FLAG_ADVANCED`, `ADV_FX_*`, `IM_SPECIAL_FX_TYPE` | P1, P2 |
| `Core/DSP/Instruments/InstrumentManager.c` | Engine map, `writeFxType()`, `chorusBind()`, `unitsWanted()`, `slotRight()`/`slotRightUpdate()`, pointer table, quiet tails, all clx/alx cases | P1, P2 |
| `Core/DSP/Effects/BbdChorus.h` | `BBD_COL_HALF_RATE`, `BBD_DPT_*` constants, loop and struct declarations, state | P1, P2 |
| `Core/DSP/Effects/BbdChorus.c` | Five loop functions, half-rate variants, `bbdChorus_control()`, `bbd_target()`, `bbdChorus_init()`, `bbdChorus_renderAlkaline()` | P1, P2 |
| `Core/DSP/Effects/BbdChorusTables.h/.c` | Generated chorus delay tables | P1 |
| `Core/Menu/menu.h` | `DTYPE_TAG_FX_DEPTH`, `DTYPE_0B127_FX_DEPTH`, `menu_getMenuIndex()`, `menu_restoreMenuIndex()` | P2, P3 |
| `Core/Menu/menu.c` | fx/dpt/rte/mix page, `menu_formatDtype3()`, library save freeze fix, step edit cursor save/restore | P2, P3 |
| `Core/Menu/MenuText.h` | `fxTypeNames`, `fxDepthNames`, `colPwmWaveNames`, `alkPwmWaveNames` updated | P1, P2 |
| `Core/Menu/MenuLabels.c` | fx/rte/dpt/mix labels | P2 |
| `Core/Menu/menuPages.h` | Step edit screen order vel/nte/tim/fil then cdn/rol/rtm | P2 |
| `Core/Hardware/frontPanel/buttonHandler.c` | VOICE SHIFT+step edit with cursor save/restore, held steps flash | P2, P3 |
| `Core/Hardware/SD/filesystem.c` | Trace flushes last in ladder (fix 1), winner cache kept (fix 2), `filesystem_coreSavePending()` (fix 3), full-rate SD polling (fix 3), phase 56 clean-byte skip (fix 3) | P3 |
| `Core/Bank/Scene/Autosave.h` | `autosave_maskByteClean()` declaration | P3 |
| `Core/Bank/Scene/Autosave.c` | `autosave_maskByteClean()` implementation | P3 |
| `config.h` | `AUTOSAVE_CRC_BYTES_PER_TICK` 512, `AUTOSAVE_WRITER_INTERVAL_MS` 1000, `AUTOSAVE_DRAIN_POLL_BUDGET_US` 1000 | P3 |
| `Core/Hardware/storageTypes.h` | clx/alx type tokens, extensions, folder names | P1 |
| Linker script | `.itcm.s099p1` subsection after `.itcm.s096p3` | P1 |
| `tools/dsp_test/modules/chorus.c` | CH-TAP, CH-GUARD, CH-LAW, CH-MIX0, CH-SLEW, CH-SRC, CH-HALF, CH-ALK, CH-INIT, CH-DPT laws and breaks | P1, P2 |
| `tools/dsp_test/modules/colloid.c` | CO-CHP, CO-PWG, CO-GATE rewritten, CO-EG breaks updated | P1 |
| `tools/dsp_test/modules/alkaline.c` | AL-CH, AL-TY updated | P1, P2 |
| `tools/dsp_test/modules/oscillator.c` | PW1–PW10 Sin cases removed | P1 |
| `tools/dsp_test/Makefile` | chorus target, break updates, P4 symbol list | P1 |
| `tools/init/make_init_col.py` | init.clx (63 rows) | P1, P2 |
| `tools/init/make_init_alk.py` | init.alx (35 rows) | P1, P2 |
| `tools/diagnostic_tools/decode_devlogs.py` | clx/alx type names | P1 |
| `tools/dsp_test/baselines/s099p1/` | Colloid CPU, Alkaline cost, P4 snapshots | P1 |
| `tools/dsp_test/baselines/s099p2/` | Colloid CPU, Alkaline cost post-P2 | P2 |

---

## Items Left for the User

- **Fix 3 and §11 device test** per P3 §14: flash the 730,952 B image, test scalar edit
  (5 s wait), step edit, both together, after a Load/Save page visit, and the VOICE
  step-edit return page.
- **P2 device matrices:** R-S by ear at the new 0.39% floor, REx removal on stored Scenes,
  effect page and step edit UX, library save confirmation on clx and alx.
- **R-S low-frequency alias** (P2 §6): options E–H documented, deferred until device tests.
- **D-G5 at CPI > 1.4:** if p95 is over 21,803, C1 (drop pre-pole) is the remaining
  option; needs user decision.
- **The other `#if DEV_MODE_LOGGING` blocks** (P3 §5, §15): not yet audited against §0.
- **ADV_2 boot 5 drain error** (P3 §6): phase 41, unexplained, not repeated on later cards.
- **Legato** (P2 §11): the `leg` roll value and its rules wait for answers to the eight
  questions asked in the session.

---

## Out of Scope

- Drum-family chop (P-F / Q-2): deferred, blocked on ITCM.
- Hiss: a later Alkaline-only option.
- Colloid form S and hot-code ITCM moves (D-10): not now.
- Eight Colloid oscillators: shown not to fit.
- Saw 4 / Saw 5 (+3-octave chops), Pulse 3.
- Ensemble tap: deferred until ear check.
