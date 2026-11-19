# S099 P1 — Chopped PWM waves and the Advanced BBD chorus: Plan

Date: 2026-11-19. Branch `dev-ph7-alkaline` at `62ca53bf`. That commit is
`756b5b70` plus the two drafts and the build image, so the drafts' tree
facts (names, sizes, line numbers) still describe this tree. This plan
draws on them and does no new code deep dive. The implementation schedule
re-checks every line reference before it is handed over.

**Status: proposal. Decisions needed (§7) before the schedule is written.**
No code is changed by this document.

Sources:
- `DRAFT_S-R_PWM_WAVE.md` (the S-R draft): the `R-S` and `REx` chopped PWM
  waves.
- `DRAFT_ADVANCED_BBD_CHORUS.md` (the chorus draft): an Alpha Juno BBD
  chorus in new Advanced Colloid and Alkaline types.

Decisions already taken (user, 2026-11-18/19). They are listed here so the
plan does not reopen them:
- **S-R:**
  - names `R-S` and `REx`, in the pwm list only (S-R D-1);
  - six oscillators in the Colloid, not eight;
  - Colloid gate read form T first (D-8), with form S and the ITCM moves
    held in reserve;
  - `REx` uses one fixed curve with no curvature control (D-11).
- **Chorus:**
  - the Colloid is the first host (D-9);
  - a `csr` source switch, `L-R` / `mid`, budgeted at `L-R` (D-2);
  - no hiss on the Colloid; hiss is a later Alkaline-only option.

---

## 1. Summary

- **One programme, six phases** (§3). Each phase ends at a device check, a
  probe or an ear test, before the next phase builds on it:
  - **P-A, offline:** generated tables and A/B audition renders. No
    firmware change.
  - **P-B, Alkaline `R-S`/`REx`:** fused ladder mode 5, in flash, with no
    ITCM.
  - **P-C, Colloid `R-S`/`REx`:** form T, one ITCM loop (≈ 308 B).
  - **P-D, Advanced Colloid:** a new type, the chorus kernel and the voice
    unit claim.
  - **P-E, Advanced Alkaline:** the same kernel in mid form, a stereo
    Alkaline output, and the §5.2 options.
  - **P-F, drum-family `R-S`/`REx`:** blocked on ITCM, so it is proposed as
    deferred.
- **Order across the drafts.**
  - The chop waves come first. Their lowest-risk host (Alkaline) leads, and
    in the Colloid they are cheaper than today's dearest mode.
  - The chorus then sits on a Colloid whose dearest mode has not moved: pwm
    Saw/Rec 6, 19,136 cycles median.
  - The Advanced Colloid inherits `R-S`/`REx` for free, because it runs the
    same engine.
- **The two drafts collide in three places:**
  - **ITCM:** 424 B are free. The chop loop takes ≈ 308 B, which leaves
    ≈ 116 B, so the chorus's fallback C2 (the chorus loop in ITCM,
    ≈ 150–280 B) is lost unless D-10's ≈ 810 B move is approved (Q-3).
  - **The Colloid's CPU margin:** the chorus fits only "just". The chop
    gives no headroom to the chorus, because the slot is charged at its
    dearest mode, and that mode stays pwm Saw/Rec 6 (Q-6).
  - **The `COL_PWM_WAVE_COUNT == OSC_PWM_WAVE_COUNT` assert**
    (`ColloidVoice.h:225`): with P-F deferred, the Colloid's count becomes
    the platform's + 2, not the + 1 the S-R draft §6.3 wrote before `REx`
    was added.
- **The most important open question is the Advanced Colloid's pass
  criterion** (Q-6). The chorus draft §9 gates "≤ 21,803 per window".
  Its own §4.4 shows that only the median clears that at CPI 1.4 and 1.6,
  and that the Basic Colloid's worst window (19,766) is already above the
  Basic budget.

---

## 2. What is built (by draft section)

### 2.1 From the S-R draft

| Item | Host | Draft § | Notes |
|---|---|---|---|
| `R-S` and `REx` tables and checks in `tools/plots/gen_pwm_tables.py` | all | §8, §6.4 | `oscPwmChopSpan[11]`, `oscPwmChopLift[128]`, `oscPwmLevelChopQ14/Q13[128]`, `oscPwmChopDc[128]`, the `REx` curve (k by ear), the linear identity curve (Colloid shared loop), `oscPwmRexDc`, `oscPwmLevelRexQ14`. Checks T10–T15, `--check` |
| Fused ladder mode 5 + `DiodeLadder_renderB2OscChop()` (flash) | Alkaline | §4.3, §6.1 | `R-S` and `REx` through a curve-table pointer (≈ 265, against the dearest 276.7) |
| The per-type wave count split (`wav` 3, `pwm` 5: Saw Sqr Tri R-S REx) | Alkaline | §6.1 | `Alkaline_setOscType()` clamp, `instrumentManager_waveDomainMax()`, `alkWaveNames` + menu assert, `menu_formatWaveCell()` |
| `col_chopDcInit()` (flash) + `col_chopPairAdd()` (ITCM, new `.itcm.s099p1` after `.itcm.s096p3`) | Colloid | §6.3, §6.4 | form T behind `COL_CHOP_READ_FORM`; one loop for both waves |
| `COL_PWM_RS` 4, `COL_PWM_REX` 5, `COL_PWM_WAVE_COUNT` 6, the pwm name list, the `col_oscControl()` chop and `dcM`/`dcS` work | Colloid | §6.3 | the assert changes form |
| Host laws AL-CH, CO-CHP (both waves), CO-PWG re-run | — | §8, §6.3 | with break proofs |

### 2.2 From the chorus draft

| Item | Host | Draft § | Notes |
|---|---|---|---|
| Kernel `cho_sideD`, `cho_monoA` (+ the ring-R mirror store), `cho_monoB`, `cho_control` | shared | §2.2–2.5, App. A | flash, `noinline` |
| A new Advanced Colloid type (token D-5) running the Colloid engine, then the chorus pass | Colloid | §7.1 | `INSTRUMENT_FLAG_ADVANCED`; shares the Colloid descriptor table if the registry allows it (checked in the schedule) |
| Rows `cho crt cdp cmx csr` (59 → 64); a new non-automatable selector shape for `cho`; MIX page second screen | Colloid | §7.2 | new tag lists in `MenuText.h` |
| Voice unit claim on `cho` ≠ off, through `instrumentManager_slotVoiceUnits()` | both | §3.2 | changes the stated FxBuffer rule (Q-8) |
| The quiet test extended by a block count; the FxBuffer handoff entry | both | §8 | |
| The Advanced Alkaline type, `right[32]`, `instrumentManager_slotRight()`, the mid form, `cbw`, §5.2 options | Alkaline | §4.5, §5.2, §7.2 | stereo mixer path for the Alkaline (R-1) |
| Host laws CH-TAP, CH-GUARD, CH-LAW, CH-MIX0, CH-SLEW, CH-SRC | — | §9 | new module file in `tools/dsp_test/modules/` |

### 2.3 Not in this plan

- Saw 4 / Saw 5 (+3-octave chops), Pulse 3: S-R draft §7.
- Hiss: a later Alkaline-only option.
- Colloid form S and the Colloid hot code in ITCM: in reserve (S-R D-8).
- Eight Colloid oscillators: shown not to fit.

---

## 3. Phases

Each phase leaves the Basic types instruction-identical outside the files it
names. Check this with `elf_osc_check`, `elf86_mixer_check`,
`elf_p4_check` and the inlined-set diff, as each draft lists. Each phase is
ready for the device only after `make all && make img` and the host module
suite pass.

### P-A — Offline (no firmware)

1. Extend `gen_pwm_tables.py` with the §2.1 tables and checks. The
   generated C goes in, but nothing calls it yet, and the image stays
   byte-identical apart from the unreferenced tables, which LTO drops.
2. Render the A/B audition files (S-R D-6/D-11):
   - `R-S`, `REx` at k = 4, 5 and 6, and the shipped Rec;
   - each at pos 0 / 24 / 48 / 96 and as a pos sweep;
   - at 55, 110, 440 and 1,760 Hz;
   - with and without the form T gate, so the low-note form T question can
     be heard before any firmware work.
3. **User output:** k for `REx`, the gate orientation (D-4), and a first
   view on form T at low notes.

### P-B — Alkaline `R-S` / `REx` (S-R stage 1)

- Mode 5 in `dl_renderB2Body()`, the new wrapper, the dispatch, the
  per-type counts and the menu names.
- AL-CH passes, and the other wrappers are instruction-identical.
- Mode 5's linked count plus per-block work is ≤ 263.7 per sample.
- **Device:** probe the cold first instance, which must be ≤ the
  Alkaline's measured worst (19,609); then an ear check.

### P-C — Colloid `R-S` / `REx` (form T)

- `col_chopDcInit()`, `col_chopPairAdd()` in `.itcm.s099p1` (needs the
  linker-script pattern and its comment), the control work and the menu.
- **ITCM sign-off (S-R D-9)** is requested with the measured linked size
  before the image is handed over.
- CO-CHP and CO-PWG pass.
- **Device:** chop 6 ≤ pwm Saw/Rec 6 on the same run, and ≤ 19,620; then
  the low-note ear check of form T. If form T fails, stop. Form S is a
  separate spike (S-R §6.3) and is not part of this phase.

### P-D — Advanced Colloid + chorus

- The type, rows, menu, kernel, claim/release, quiet test and handoff.
- Linked loop counts must not exceed the spike's: 22 per channel, 25 + 15
  mono. A larger count means the spills came back.
- **Device:**
  - an Advanced Colloid in pwm Saw/Rec 6, `rte` `str`, `cho` `II`, `csr`
    `L-R`, against the Q-6 criterion; then `csr` `mid`, which must be
    lower;
  - a `cho` toggle during play with a bus Delay running, to hear the
    reseat;
  - Scene change with the same type, which must carry over with no click.
- **If it is over:** the C1–C5 order (chorus draft §6), within what Q-7
  allows.

### P-E — Advanced Alkaline + chorus

- The type, mid form, `right[32]`, `slotRight()`, `cbw`, and the agreed
  §5.2 options (Q-12).
- **This phase touches the mixer path** (the Alkaline becomes a stereo
  slot while `cho` ≠ off). The mixer gate is `elf_hotloops.py` plus the
  inlined-set diff, never the mixer's byte size.
- **Device:** the two-Advanced Scene (Colloid + Alkaline + a bus Effect),
  compared with the S096 P3 Scene (R-1).

### P-F — Drum-family `R-S` / `REx` (S-R stage 2): proposed deferred

- Five role loops (≈ 0.9–1.0 KB, plus the interpolated `REx` curve read).
- They do not fit ITCM once P-C has taken its loop. Options are S-R D-2
  a/b/c (Q-2).

### Hardware rounds

- P-A needs only listening.
- P-B to P-E each produce one image for the device. P-B and P-C can share
  one image if the user prefers fewer flashes (Q-1).
- The commit before this work (`91977a08`, S098 P0) is marked untested on
  hardware. Any image from this plan also carries it (Q-13).

---

## 4. Memory and budgets (from the drafts; to be measured per phase)

| Resource | Now | This plan adds | After (*estimate*) |
|---|---|---|---|
| ITCM | 15,960 / 16,384 (424 free) | `col_chopPairAdd` ≈ 308 B | ≈ 116 B free |
| Flash headroom | 32,888 B | mode 5 wrapper ≈ 1.4 KB; chop tables ≈ 0.7 KB; `REx` + linear curve tables 4 KB; Colloid chop control; chorus kernel ≈ 0.3–0.6 KB; two Advanced types' descriptors (≈ 1.2 KB each if copied, ≈ 0 if shared); Advanced Alkaline options | ≈ 23–26 KB, above `link_budget.py`'s 16 KiB warning |
| DTCM | — | none new: one existing voice unit (4,416 B) claimed per chorus slot | Effect share worst case unchanged (100,160 B) |
| SRAM1 | union slot 1,264 B | `ColloidVoice` 1,212 → ≤ 1,224; `AlkalineVoice` 564 → ≤ 640 | union slot size unchanged |
| CPU, Basic | Colloid pwm 6 19,136; Alkaline dearest 276.7 per sample | none on any Basic type's dearest | unchanged |
| CPU, Advanced extra | 2,183 cycles per block | Colloid chorus `L-R` 1,980–2,610 | Q-6 |

**RAM sign-off is requested for:**
- the ITCM loop (P-C);
- the `ColloidVoice` and `AlkalineVoice` growth inside the existing union
  slot (P-D, P-E);
- the voice unit claim by a new owner (P-D).

---

## 5. Risks

- **R-1, the mixer peaks** (chorus draft §9). A real Scene already
  exceeds the per-block budget at its peaks. Two Advanced slots add up to
  ≈ 4,400 per block.
- **R-2, the linked CPI on the Colloid chorus.** At 1.6 CPI, `L-R` leaves
  57 cycles against the median.
- **R-3, form T at low notes.** The spurs are 10–25 dB above the shipped
  Rec below about 200 Hz. If the ear rejects it, P-C stops and form S
  needs its own spike.
- **R-4, the ITCM is spent.** After P-C, every later ITCM user needs D-10
  or an ITCM review. That includes the drum-family chop (P-F), chorus C2,
  form S with hot-code moves, and anything else.
- **R-5, LTO register moves.** New `noinline` flash functions can still
  shift inlining in `mixer_calcNextSampleBlock()`. The inlined-set diff
  runs at every phase, not only at the end.

---

## 6. Tests and gates (summary)

- **Host:**
  - S-R: T10–T15, AL-CH and CO-CHP, each with its break proofs; CO-PWG
    re-run.
  - Chorus: CH-TAP, CH-GUARD, CH-LAW, CH-MIX0, CH-SLEW and CH-SRC, each
    with its break proofs.
  - The full `make -C tools/dsp_test all` suite at every phase.
- **Static:**
  - `elf_looppath.py` / `elf_blockpath.py` on the linked image, for every
    new loop and its per-block work;
  - `tools/alkaline/control_cost.py` and a `colloid_cpu.py` row;
  - `elf_osc_check`, `elf86_mixer_check`, `elf_p4_check` and the
    inlined-set diff;
  - `link_budget.py`.
- **Device:** the probe on the D-cached image, cold first instance, as
  each phase names. Records go to `tools/dsp_test/baselines/s099p1/`, not
  `build/`. The spike sources and counts from both drafts also go there.
- **Ear:**
  - S-R: `REx` k, gate orientation, form T at low notes, and level law.
  - Chorus: the Juno modes, the `var` range, cutoff, mix and dry gain.

---

## 7. Questions for the user

### Programme

| # | Question | Recommendation |
|---|---|---|
| Q-1 | **Scope and order:** P-A → P-B → P-C → P-D → P-E, with P-F deferred. Is the Advanced Alkaline (P-E) in this programme, or does it wait until the Colloid chorus has passed on the device? And may P-B and P-C share one hardware image? | P-A to P-E in that order. P-E starts only after P-D's probe passes. Separate images for P-B and P-C, so a form T failure is not mixed up with the Alkaline result |
| Q-2 | **The drum-family chop (S-R D-2):** defer it, or do it now in flash (a), mixed (b), or after an ITCM review (c)? | Defer. It is the only host with no free ITCM path, and the Colloid and Alkaline cover the sound |
| Q-3 | **ITCM priority:** the chop loop takes ≈ 308 of the 424 B free, which removes chorus fallback C2. Approve D-9 on those terms? And D-10 (the ≈ 810 B helper move) still "not now"? | Approve D-9 for the chop loop. D-10 is not now. C2 drops off the chorus's fallback list, and C1/C3/C4/C5 remain |
| Q-4 | **Which hosts get `REx`?** The S-R draft costs it on all three, but its decisions name only the Colloid explicitly | Colloid and Alkaline (P-B, P-C). The drum family follows P-F |
| Q-5 | **The `REx` curve and gate orientation by ear:** accept the P-A audition renders as the deciding step (k from 4–6, high-first or low-first), with the result fixed in the generated table before P-B? | Yes. Changing k after P-B means regenerating one table, but the audition costs nothing |

### Chorus on the Colloid

| # | Question | Recommendation |
|---|---|---|
| Q-6 | **The pass criterion for the Advanced Colloid.** The chorus draft §9 says "≤ 21,803 per window". By §4.4 that holds only at CPI 1.2, because the Basic Colloid's own worst window (19,766, next to edits) already exceeds 19,620. Which measure? | Median and p95 ≤ 21,803, with windows next to an edit reported but not gating. This is the basis on which the Basic Colloid was accepted at 19,766 max. The two-Advanced Scene (R-1) is judged on the whole-mixer figure against S096 P3 |
| Q-7 | **The fallback authority** if P-D's probe is over: may the implementer apply C1 (drop the pre-pole) and C3 (half-rate wet, after its image check) without asking? C4 changes the Basic Colloid's noise, and C5 removes the user's switch | C1 may be applied without asking. C3, C4 and C5 each need a user decision with the probe result in hand |
| Q-8 | **Unit claim timing (chorus D-1):** claim on `cho` ≠ off, with `cho` made non-automatable (a new row shape), changing the FxBuffer rule "never at a row change"? Or claim at type assignment? | On `cho` ≠ off, as the brief asks. The rule change is recorded for the user's spec update |
| Q-9 | **Tokens, extensions and folder names (chorus D-5):** `clx` / `alx`? The Instrument folder names and file extensions are needed too, since the type registry is the storage schema | `clx` / `alx`, with extensions and folders following each type's existing pattern. The schedule lists them for approval |
| Q-10 | **Defaults by ear (chorus D-6/D-7):** the `var` rate range, the wet cutoff and its tracking, the dry/wet balance, and the dry gain (+2 per sample). Is the dry gain in or out? It is in the budget figure | Log 0.1–10 Hz; cutoff ≈ 8–10 kHz with tracking; equal balance. Dry gain **in**, because it is already counted, and taking it out later saves 2 per sample in reserve |
| Q-11 | **Rulings (S-R D-7 / chorus D-8):** accept the S089 P2 ceiling as met when no Basic type's dearest rises and an Advanced type spends only the reserved Advanced share? | Yes. This is the reading the S094 PWM type was accepted under |

### The rest

| # | Question | Recommendation |
|---|---|---|
| Q-12 | **The Advanced Alkaline options (chorus D-10):** which of overload clip, soft overload, 12 dB wet, ensemble tap and width get rows? | Overload clip + 12 dB wet + `cbw`. Ensemble after an ear check. Width if a row is free |
| Q-13 | **S098 P0 is untested on hardware.** Test it on its own before P-B's image, or accept that the first image carries both changes? | Test S098 P0 first if a hardware round is due anyway. A Scene-load fault and a new-wave fault are easier to tell apart on separate images |
| Q-14 | **Level law and the width floor (S-R D-5):** peak-keeping level, with the table stored as Q13 (×2.0 does not fit Q14), and the `oscPwmDSaw` widths with a 2.54 % floor? | Yes, both. An RMS-matched level is an ear option at P-A |
| Q-15 | **Row lift (S-R D-3):** lift 2 everywhere in the Alkaline and the Colloid. The drum family's per-pos lift waits for P-F | Yes |
| Q-16 | **Where the P-A audition renders go:** they are not records, so not `tools/dsp_test/baselines/` | The session scratchpad, handed over by path. Only the generator script changes stay in the tree |

---

## 8. Statements this work makes out of date (for the user to act on)

This plan edits nothing in `MEMORY.md` or `knowledge_files/`. Both drafts'
lists apply (S-R draft §10, chorus draft §11). Combined:

- **`MEMORY.md` §DSP:**
  - Alkaline: `ALK_WAVE_COUNT` 3 for both lists; fused modes 3/4 only.
  - The PWM oscillator type: the wave list, if P-F is done.
  - Drum FM ramp twins: "ITCM 13,984 / 16,384". **Already stale:** it is
    15,960, and ≈ 16,268 after P-C.
  - The ITCM subsection order: it now ends at `.itcm.s096p3`, and P-C adds
    `.itcm.s099p1`.
  - "Cymbal/HiHat now Basic … No current type is Advanced."
- **The FxBuffer rule** "claimed at type assignment, never at a row change"
  (`InstrumentManager.c:466` comment, `EFFECTS_BUS_REFERENCE.md`,
  `EFFECTS_MODULES_DSP_REFERENCE.md`), if Q-8 goes as recommended. Also the
  voice-unit owners.
- **`ALKALINE_MODULE_REFERENCE.md`** §3.1 and §7.1.
- **`INSTRUMENTS_DSP_REFERENCE.md`:**
  - §5.5, §6.1 and §8.1/§8.2;
  - the Advanced type entries;
  - the add-a-type checklist use.
- **The Colloid reference:** the pwm wave list, the assert's form, the ITCM
  total, and the Advanced type.
- **`CPU_USE_DSP_AUDIT.md`:** the pwm chop rows, and the first Advanced
  slots with their measured cost.
- **`knowledge_files/drafts/DRAFT_COLLOID_ALKALINE_ADVANCED_FX.md`:** the
  stale counts listed in chorus draft §11.

---

## 9. Next step

After the §7 answers, `S099_P1_IMPLEMENTATION.md` gives the per-step,
per-file schedule for P-A and P-B first, with every name and line checked
in the tree. The later phases are scheduled after each device round.
