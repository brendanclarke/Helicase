# Session 085 — Handoff Log

```
DATE: 2026-10-27
SESSION GOAL: Implement oscillator features in staged steps with absolutely
              no increase in CPU cycles overall (user). First step: add
              `snc` (hard sync) and `rng` (ring mod) to the Drum `mod` row
              beside `fm` and `mix`. Drum only.
COMPLETED:
  Background: the first S085 attempt (full oscillator cherry-pick from the
      S084 spike: types, run modes, render pool) reached 100 % CPU on
      hardware with `pwm` on every instrument and was rolled back before
      this conversation (§2). Its tree is kept as
      Helicase-dev-ph7-voice-pwm-spike/ (reference only).
  P1 plan (S085_P1_OSC_MOD_SNC_RNG.md, r1-r4): cost-gated design for
      snc/rng; decisions Q1-Q12 (§4).
  P1 implementation schedule (S085_P1_OSC_MOD_IMPLEMENTATION.md): code
      deep-dive, measured baseline, paste-ready code with comment blocks,
      test bench additions; the documented code was compiled and its host
      test run (with break proofs) in a scratch copy before hand-over.
  P1 implementation (user/agent, commit 8812bffd) with deviations D7/D8;
      assessment (§9): conformance, gates rerun, baseline firmware rebuilt
      for a call-target comparison; findings F1-F4 (documentation only).
  Follow-ups after the P1 hardware pass:
      D9  snc/rng use the mod envelope with the fm response (amt^2 x env);
      D10 rng depth ramped per sample, snapped at a trigger.
  S086 startup plan written (S086_P1_OSC_TYPE_EXPANSION.md).
  Session close: amendments to the implementation document, this log, the
      index entry, specification updates, MEMORY.md, volatile notes.
VERIFIED ON HARDWARE: yes for P1 ("checks out completely fine on the
  hardware", user). After D9 the user heard rng "crunchy", which led to D10;
  the D10 re-test was not reported before the close.

CHANGES THIS SESSION (detail in §15):
- Core/DSPAudio/Oscillator.c/.h: the S085 P1 section: sync clock and
  reset-list builder, sync copies (sine/table/crash), ring copies
  (sine/table/noise/crash/user sample, per-sample depth ramp), interpolated
  sync/ring variants, osc_renderSyncWave/osc_renderPlainWave, the two
  dispatchers, the amount mappings; placed in .itcm.s085.
- Core/DSP/Instruments/Drum/DrumVoice.c/.h: DRUM_MOD_* values; mixOscs ->
  modMode; syncClock, ringSnap, ringDepthQ15; switch on the mod mode;
  Drum_modEnvDepth(); trigger sets ringSnap; comment on osc.fmMod.
- Core/DSP/Instruments/Drum/DrumParameters.c: osc2_mod_type binds modMode.
- Core/Menu/MenuText.h, menu.c: mixFmNames (fm mix snc rng),
  menu_getMixFmName(), clamp and automation max 3; menuText_mix/_fm removed.
- tools/dsp_test/: snapshot85.sh, frozen85/, elf_symdump.py,
  test_osc_mod.c, Makefile targets oscmod_identity, osc_mod,
  armcheck-oscmod, elf_osc_baseline, elf_osc_check; DSP_TEST.md.
- build/LXRV2_lxr02.img rebuilt.
- Specifications: INSTRUMENTS_DSP_REFERENCE, CPU_USE_DSP_AUDIT,
  STORAGE_SRAM_MANIFEST, MIDI_SPEC, FILESYSTEM_SPEC (§17).
- Root: S086_P1_OSC_TYPE_EXPANSION.md (new).

KNOWN ISSUES INTRODUCED: none open. Accepted behaviours: §16.
KNOWN ISSUES RESOLVED: Drum `mod` row could only hold fm/mix (a boolean
  test that would have played 2/3 as mix).

NEXT SESSION RECOMMENDED GOAL: re-test the D10 rng ramp on hardware, then
  Session 086 from S086_P1_OSC_TYPE_EXPANSION.md (stock waves, sample
  rework, wavetables, optimizer, then a pwm evaluation).
BLOCKERS: the S084 hardware matrix V1-V7 is still recorded as open
  (confirm closed or waived); the S086 plan's open questions Q1-Q11.

CRITICAL REMINDERS FOR NEXT SESSION:
- CPU rule (user): no increase in CPU cycles overall. Gate = the dearest
  setting of a voice in any mode <= that voice's dearest setting before the
  change; freeze the ceilings as numbers before replacing the v1 sample
  loops. Static counts (standalone AND linked) are evidence; the device
  cpu widget with the worst-case Scene is the acceptance test.
- Never edit an existing oscillator loop to add a feature: add separate
  copies; put new ITCM code in its own .itcm.<session> subsection; do not
  add constant-argument call sites to calcNextOscSampleBlock() (call the
  leaves, as osc_renderPlainWave() does); prove fm/mix unchanged with
  elf_osc_check.
- Drum modOsc is never reset on a trigger, in any mode (user Q6b). No
  change unless the user tests it and asks.
- rng/snc depth = amt^2 x mod envelope (the fm response). rng ramps its
  depth per sample; snapped on the first block after a trigger. When run
  modes return (S087), free-run is to try "ramp always" (user).
- The rng margin against fm with user samples is 1-2 instructions (linked).
  Any change to the ring copies or the user-sample loops must re-measure it.
```

---

## 1. Session overview and timeline

| When (2026-10-27) | Work |
|---|---|
| Before this conversation | **First S085 attempt.** The full cherry-pick of the S084 spike's oscillator work (`S085_OSC_MOD_CHERRYPICK.md`, `S085_P1_OSC_MOD_PLAN.md`, `S085_P1_IMPLEMENTATION.md`, `S085_P2_OSC_RESET_FOLLOWUP.md`) was implemented, reached 100 % CPU on hardware, and was rolled back. Its tree is `Helicase-dev-ph7-voice-pwm-spike/` (commits `4ad2f850` "failed osc expansion", `ef7d5445` "post-mortem docs"). The planning commit `1ea234ea` on `dev-ph7-voice_expansion` holds the cherry-pick documents. `S085_SAMPLE_INDEX_SPEEDUP.md` (root; committed in `b450d76a`) was written from the failed tree. |
| Plan | `S085_P1_OSC_MOD_SNC_RNG.md` r1 → r4 with the user (§4). |
| Schedule | `S085_P1_OSC_MOD_IMPLEMENTATION.md`: deep-dive, measurements, paste-ready code, test bench. The documented code was compiled and tested in the scratchpad (12 break proofs). |
| Implementation | Commit `b450d76a` (pre-implement: the plan r4, the schedule, the speedup note), then `8812bffd` "mod osc expansion implemented", with deviations D7/D8 and §11 of the schedule (its implementation log). |
| Assessment | §12 of the schedule: conformance, gates rerun, the baseline firmware rebuilt in a scratch worktree for a call-target comparison; F1–F4. |
| Hardware | P1 passed. |
| Follow-ups | D9 (mod envelope), then D10 (`rng` ramp) after "rng sounds crunchy". |
| Next-session plan | `S086_P1_OSC_TYPE_EXPANSION.md`. |
| Close-out | This log and the other close-out documents. |

**Method (as S081–S084):**
1. A plan with numbered questions and revisions.
2. A line-level schedule with paste-ready code and comment blocks (What /
   Why / Inputs / Outputs / Accessors / Affiliates).
3. Implementation.
4. An assessment appended to the schedule.
5. Targeted follow-ups, each discussed before code ("let me know how you're
   going to implement that before writing code").

**New in S085:**
- Every cost claim was measured with `fpseq.py`, on the standalone object
  and, after the build, on the linked ELF.
- The documented code itself was compiled and tested before hand-over.

**State at close:**
- Branch `dev-ph7-voice_expansion`. `8812bffd` holds P1.
- The D9/D10 follow-ups, the F1/F4 fixes and all close-out documents are
  uncommitted.
- Both failed trees are in the repository root:
  - `Helicase-dev-ph7-spike/` (`26fb7c3`);
  - `Helicase-dev-ph7-voice-pwm-spike/` (`4ad2f850`, `ef7d5445`).

---

## 2. The failed first S085 attempt (rollback record)

**Source:** `Helicase-dev-ph7-voice-pwm-spike/S085_POST_MORTEM.md`,
`S085_POST_MORTEM_OSC_RUN.md`, `S085_POST_MORTEM_MENU.md`.

### 2.1 What failed

- **The symptom:** with every instrument's oscillators on `pwm`, the CPU
  reached 100 % and the interface stalled. A Cymbal/HiHat FM modulator on
  `pwm` "crashed"; no code fault was found, and the probable cause was the
  same overload.
- **Why the budget did not hold:**
  1. It was never measured on hardware. The CPU row H11 was left to the end
     and never gated the work.
  2. There was no cap against the whole frame.
  3. The oscillator worst case grew about five-fold.
  4. `pwm` was the dearest type, and Cymbal/HiHat run three oscillators.
  5. The render pool was extra load.
  6. Cost estimates were wrong; for example the FM depth ramp was
     estimated at 4 instructions and measured 15.

### 2.2 The rollback's static costs (instructions per sample, `osccost.py`)

**Per voice, oscillator section:**

| Voice | Pre-S085 | S085 (failed) |
|---|---:|---:|
| Cymbal/HiHat | 98 | 178 (2 × `Swp` modulator 58 + `Swp` FM carrier 62) |
| Drum `fm` | 68 | 135 (incl. the FM depth ramp, 15) |
| Drum `snc` | — | 162 (lane 18 + master 58 + slave 70 + mix 16) |
| Drum `rng` | — | 135 (58 + 58 + ring law 19) |
| Drum `mix` | 60 + add | 116 + add |
| Snare | 46 | 74 |
| Render pool | 2 × ≤ 35 | ≤ 210 |
| System | — | ≈ 1,278 per sample |

**Per type (plain / FM / sync slave):**

| Type | Plain | FM | Sync slave |
|---|---:|---:|---:|
| table | 25 | 36 | 35 |
| `pwm` | 42–58 | 47–62 | 53–70 (`Swp` segment 2 the dearest) |
| `smp` | 40 | 45 | 53 |
| `slp` | 47 | 53 | 61 |
| `wtb` | 40/43 | 44/48 | 51/54 |

**ITCM:** separate sync copies of every loop filled ITCM to 16,272 B. The
shared plain/sync loops (`OSC_SHARED_LOOP`) brought it to 12,432 B, at +2
per sample on every plain render.

### 2.3 Requirements carried forward (post-mortem §4) and how P1 applied them

1. **Measure before accepting.**
   - P1 measured the baseline first.
   - P1 measured every new loop, standalone and linked.
   - The user's device check gates.
2. **Cap against the whole frame.** P1's gate is the per-voice worst case
   against today's (Q1).
3. **Restrict types per oscillator where needed.** P1: noise and user
   samples are unsynced slaves.
4. **One component at a time, each with its own hardware check.** P1 is
   mod modes only.

### 2.4 Menu decisions recorded there (for S086)

The rollback's rows were:
- `typ` (`wav pwm smp slp wtb`);
- `wav` (the selection);
- `mod` (the continuous control: pwm width, sample start, wavetable
  position);
- `run` (`rst fre itp`).

**Other decisions:**
- stock wave names `Re3 Sw3 ReA TrA SwA`;
- `pwm` family names without a `p` prefix;
- `typ`/`run` as selector rows (not morphable, stored single-ended, applied
  before the Morph image);
- retained request bytes;
- `pwm` excluded on the Cymbal/HiHat modulators.

S086 replaces `mod` with `pos` (§14.3).

---

## 3. Starting state (code at `1ea234ea`, the S084 end)

### 3.1 Drum oscillator section (`Drum_calcVoiceSyncBlock()`)

```
modBuf = modOsc rendered plain, gain amt (fmModAmount)      call 1, every mode
mix:  buf = osc plain, gain (1 - amt); buf = QADD16(buf, modBuf)
fm:   buf = osc FM from modBuf, gain 1   (osc.fmMod = amt x pitch envelope)
```

- **`osc2_mod_type`:** `DTYPE_MIX_FM`, `MOD_NONE`, `FLAGS_IMAGE` (morphable,
  automatable), bound to `uint8_t mixOscs`.
- **The mode test** was a boolean (`if (voice->mixOscs)`), so values 2/3
  would have played as `mix`.
- **`amt` (`osc2_mod_amount`)** is `MOD_0_127` / `TYPE_FLT` = value/127,
  0..1; an LFO range of 0..1.
- **The pitch EG and snap EG** modulate only `osc` (the carrier).
- **The FM page has only `frq`** (`osc2_pitch_coarse`), no fine row.
- **The modulator's `phaseInc` is constant within a block:**
  `osc_setFreq()` runs in `Drum_calcVoiceAsync()`, which the mixer runs
  before the render (mixer.c ≈ 882 before ≈ 965). There is no per-sample
  slew.

### 3.2 Trigger

- **The carrier is reset in every mode:** Sine to `1024 + …`, Tri/Saw/Rec
  to `0xff<<20`, both × the transient offset; other waves to 0.
- **`modOsc` is never reset; it runs free.**
- **`startPhase`:** `Drum_setPhaseVoice()` writes it on both oscillators,
  but nothing calls the function and nothing reads the field (the use is
  commented out in the trigger).
- **Cymbal resets its modulators** (`CymbalVoice.c:160`).

### 3.3 Wave types and their phase

| `wav` | Loop | One nominal period | phaseInc from f |
|---|---|---|---|
| `Sin` | `calcSineBlock` (4,096 + 1 table, `<<20`) | 2^32 | f/fs × 2^32 |
| `Tri Saw Rec` | `calcWavetableOscBlock` (11 octaves × 1,024, `<<22`) | 2^32 | f/fs × 2^32 |
| `Noi` | `calcNoiseBlock` (RNG on each wrap) | 2^32 | f/fs × 2^32 |
| `Cym` | `calcSampleOscBlock` (32,768-point 8-bit `crashSample`, 32 KB, `<<17`) | 2^27 (the phase wraps every 32 periods) | f/fs × 2^27 |
| user sample (v1 tail, `wav` ≥ 6) | `calcUserSampleOscBlock` (`<<17`; a one-shot holds, a looped sample wraps at `size << 17`) | 2^27 | f/fs × 2^27 |

### 3.4 Measured baseline (`fpseq.py --report --all`, firmware `-Ofast` flags and include set)

| Loop | Plain | FM carrier |
|---|---:|---:|
| table | 24 | 35 |
| sine | 23 | 30 |
| crash | 30 | 38 |
| noise | 12–13 (27 on the RNG wrap) | = plain |
| user sample (main variants) | 40–41 | 48–49 |

| Other | Figure |
|---|---|
| Saturating add (`mix`) | 10 per 2 samples = 5 per sample |
| `calcUserSampleOscBlock` / FM | compiled as 10 unswitched variants; the large "loops" (115–178) are spans across variants, not per-sample paths |
| Linked image | user-sample FM 50/51; plain ≈ 41–43 |
| Build | `text=609,288`, `data=424`, `bss=430,200`; flash headroom 143,952 B; ITCM 4,168 / 16,384 B; DTCM 4,472 B; FX-arena margin 3,712 B |

**Drum oscillator section with user samples:**

| Mode | Standalone | Linked |
|---|---:|---:|
| `fm` | 41 + 49 = 90 | ≈ 92 |
| `mix` | 41 + 41 + 5 = 87 | — |

---

## 4. Plan (`S085_P1_OSC_MOD_SNC_RNG.md`) and decisions

### 4.1 Revisions

| Revision | Content |
|---|---|
| r1 | First plan and nine questions; the in-loop-carry `snc` with the master audible and a mix law; `rng` as a ring pass |
| r2 | Q1 (a), Q2 no probe, Q7b (a). Q3 explained. New user question: what `amt` does in `snc` → Q10. |
| r3 | Q10 = sync depth; Q6b = trigger unchanged; Q5 interpolation supported; Q4/Q6/Q8/Q9 accepted; RAM accepted. A zero-crossing index for crash and user samples was proposed. |
| r4 | Crash: no index, treated as noise. Sync enabled for stock waves only. Sample/wavetable sync rules moved to "future" (§14.1). Q11 (the reset list), Q12 (crash slave syncs) accepted. |

### 4.2 Decisions

| # | Question | Decision (user) |
|---|---|---|
| Q1 | What must not increase? | **(a)** the dearest setting in any mode ≤ today's dearest `fm` (the system worst case); (b) per-same-waves reported |
| Q2 | A DEV DWT cycle probe? | **No**; the user checks on the device (`cpu` widget, underruns) |
| Q3 | Sample masters in `snc` | P1: act as noise (one reset per nominal period at `frq`). Future: §14.1 |
| Q4 | Overshoot correction on the reset | **Kept** (target = overshoot × slave/clock ratio) |
| Q5 | Waveform interpolation on the `snc` slave | **Supported** (two sync renders from the same list + the blend) |
| Q6 | ITCM placement; RAM | **ITCM; RAM accepted** |
| Q6b | Reset the FM-page oscillator on a trigger in `snc`/`rng`? | **No: leave the trigger as it is** in every mode; change only if the user tests it and asks |
| Q7 | Out-of-range mode bytes | the `switch` default plays `fm`; the menu shows `fm` |
| Q7b | Morph across the row | **stays morphable**; a Morph `fm` → `rng` steps through `mix`, `snc` |
| Q8 | Rename `mixOscs` | **`modMode`** |
| Q9 | Ceiling for later stages | the dearest loop each oscillator can select today, in the voice with the most oscillators (Cymbal/HiHat) |
| Q10 | What `amt` does in `snc` | **Sync depth**: 0 free-running, 127 hard sync; only the slave is heard; the master is not rendered. This replaced the earlier `snc` mix law. |
| Q11 | Reset list or carry inside the slave loop? | **The list** (≈ +2 per sample and 128 B of stack, but later master kinds add only a builder) |
| Q12 | Crash as the slave | **Syncs** ("treat crash as noise" applies to the master role) |

**Rejected options for `amt` in `snc` (Q10):**
- the master/slave mix law (it needs the master rendered);
- a reset phase offset;
- a slave pitch ratio (it duplicates coarse and the pitch EG).

### 4.3 Carried constants

| ID | Rule |
|---|---|
| K1 | Stored 0 `fm`, 1 `mix`, 2 `snc`, 3 `rng`; display `fm mix snc rng` |
| K2 | `snc`: `modOsc` (FM page) = master, `osc` (carrier) = slave |
| K4 | The slave renders through sample i; the reset lands on i + 1 |
| K5 | `rng` = car·(1 − d) + car·mod·d |
| K6 | Constant cost: the reset arithmetic runs every sample; select by flag |

**Replaced:**
- **K3:** the `snc` mix law, replaced by the depth (Q10).
- **K7:** "no resets after a one-shot master ends" became moot (sample
  masters are noise).

### 4.4 Resolved ambiguities

| Point | Resolution |
|---|---|
| Master | `modOsc` as a clock; the pitch EG sweeps the slave |
| `amt` per mode | `fm` depth; `mix` share; `snc` sync depth; `rng` wet amount (from D9: amt² × env for `fm`/`snc`/`rng`) |
| Noise slave | unsynced (the precedent: the FM carrier ignores FM on noise) |
| Noise master | its own 2^32 clock |
| Crash master | as noise: increment `<< 5`, one reset per nominal period |
| User-sample master | as noise |
| User-sample slave | unsynced in P1 |
| Table waves as master | the clock wrap (their zero crossing is once per cycle too; an index would only shift the resets by a constant phase of an unheard wave) |
| Snare, Cymbal, HiHat | untouched |

### 4.5 The explanation behind Q3 (for future sync work)

**Sync means:** reset the slave when the master starts a new cycle.
"Zero crossing" is the analog way to detect that moment.

**Digitally,** the phase overflow is the exact cycle start: one add and a
carry. That makes it free for Sin/Tri/Saw/Rec/Noi.

**User samples have no such cycle:**
- a one-shot stops at its end;
- a looped sample wraps at its length by modulo, which costs a divide per
  sample (the rollback's 18-instruction lane).

**Zero-crossing detection on the master's audio fails three ways:**
- samples, crash and noise cross many times per period;
- it needs the master rendered;
- at `amt` 0 the gained master block is silent.

**The r3 idea and its outcome:** a precomputed crossing index
(Q15.17 positions; the divides done at upload). Measured on the crash:
6,198 upward crossings over 32,768 points (one every 5.3 points; max gap
30), so 4-byte entries need ≈ 24.8 KB against a 32 KB sample.

**Alternatives measured:**

| Encoding | Size |
|---|---:|
| `uint32`, keep crossings ≥ 8 points apart | 12.6 KB |
| `uint16`, ≥ 8 apart | 6.3 KB |
| `uint16`, ≥ 32 apart | 1.9 KB |
| **Bitmap: 1 bit per point** | **4 KB** (12.5 % of the crash; 6.25 % of a 16-bit sample) |

**The bitmap was chosen for the future; the crash was dropped (r4).**
- **Bitmap mechanics:** read the two words covering the step, find the
  first set bit with `RBIT`/`CLZ`, derive the overshoot; about 12–15 per
  sample; no search.
- **A step over 32 points per sample** looks only at the first 32 points.

---

## 5. Design as built

### 5.1 Dispatch (`Drum_calcVoiceSyncBlock()`, final)

```
switch (modMode)
  mix (1): modOsc plain (gain amt) -> modBuf; osc plain (1 - amt); QADD16(buf, modBuf)   [unchanged]
  snc (2): clockInc = osc_syncClockInc(&modOsc)
           syncClock = osc_buildSyncList(syncClock, clockInc, syncOver[32] on the stack)
           calcNextOscSampleSyncBlock(&osc, syncOver, clockInc,
                                      osc_syncDepthQ30(Drum_modEnvDepth(voice)), buf)
  rng (3): target = osc_ringDepthQ15(Drum_modEnvDepth(voice))
           start  = ringSnap ? target : ringDepthQ15;  ringSnap = 0
           step   = size ? (target - start) / size : 0
           modOsc plain (gain 1.0f) -> modBuf
           calcNextOscSampleRingBlock(&osc, modBuf, buf, size, start, step)
           ringDepthQ15 = start + step x size          (the value reached, not the target)
  fm (0) / default: modOsc plain (gain amt) -> modBuf; osc FM from modBuf   [unchanged]
```

- `transient_calcBlock()` fully rewrites `modBuf` afterwards (a memset or
  per-sample writes), so `modBuf` is unused in `snc`.
- `Drum_modEnvDepth(voice)` = `fmModAmount × osc.fmMod` = amt² × env
  (§10.1).

### 5.2 `snc`

- **Clock (`osc_syncClockInc()`):**
  - `modOsc.phaseInc` for Sin/Tri/Saw/Rec/Noi;
  - for `waveform >= CRASH` (crash, user samples): `min(inc, 0x07FFFFFF)
    << 5`, i.e. 2^32 per nominal period. At or above fs the master resets
    the slave every sample.
  - **The clock is `DrumVoice.syncClock`** (D2). It is zeroed by
    `Drum_initVoice()`, never reset on a trigger, and advanced only in
    `snc`. `modOsc.phase` is untouched in `snc`, so the modulator resumes
    where it was when the mode changes.
- **Reset list (`osc_buildSyncList()`):**
  - `next = clock + inc`;
  - `over[i] = (next < clock) ? next : OSC_SYNC_NONE` (`0xFFFFFFFF`). The
    overshoot is always < inc, so it never equals the marker.
  - It returns the end clock. 7 instructions per sample on both paths.
- **Slave copies** (`calcSineSyncBlock`, `calcWavetableOscSyncBlock`
  (table as an argument), `calcSampleOscSyncBlock`):
  - the plain loop's read and interpolation verbatim;
  - the store is `oscOut` (gain 1, D3);
  - the phase update goes through `osc_syncPull(oscPhase + phaseInc,
    over[i], ratioQ16, depthQ30)`:
    - target = `(over × ratioQ16) >> 16`;
    - diff = (int32)(target − phase), the short way round;
    - pulled = phase + ((int64)diff × depthQ30 >> 30);
    - it returns pulled only when `over != NONE`.
  - At depth 0 the output is bit-identical to the plain loop. At depth
    1 << 30 it lands exactly on the target. The reset lands on i + 1 (K4).
  - GCC compiles the select as a branch (two loop variants); the reset path
    is budgeted.
- **Ratio (`osc_syncRatioQ16()`):**
  - float `slaveInc / clockInc × 65536`, clamped to 4,294,967,040;
  - 0 when the clock increment is 0;
  - one VDIV per call (two per block with interpolation).
- **Depth (`osc_syncDepthQ30()`):** amount 0..1 → Q30, both ends exact;
  ≤ 0 and NaN → 0; ≥ 1 → `0x40000000`.
- **Unsynced slaves:**
  - Noi and user samples go through `osc_renderPlainWave()`, the leaf loops
    at gain 1 (D8).
  - `osc_renderSyncWave()` dispatches the per-waveform choice without an
    interpolation check.
- **Interpolated slave (`calcPeriodicInterpSyncBlock()`):**
  - both partners render through `osc_renderSyncWave()` from the same list,
    then the plain blend;
  - `osc->phase` = the base partner's synced phase when the base has a sync
    copy (`osc_syncWaveform()`), else start + inc × size.
- **How it sounds:**
  - depth 127 is classic hard sync (sweep the slave with coarse or the
    pitch EG);
  - intermediate depth moves continuously from free to locked;
  - **the FM-page `wav` cell has no audible effect** (every master is a
    clock at `frq`).

### 5.3 `rng`

- **Law (final, D10):**
  - G = (32768 − d) + ((d × mod) >> 15);
  - out = SSAT16((car × G) >> 15);
  - d in Q15 (0..32768) per sample: d_i = d0 + (i + 1) × step.
- **Ranges:** |d × mod| ≤ 2^30; G ∈ [−32768, 32768]; car × G fits int32.
  d = 0 is the carrier exactly; d = 32768 is the pure ring product.
- **Copies:** `calcSineRingBlock`, `calcWavetableOscRingBlock`,
  `calcNoiseRingBlock`, `calcSampleOscRingBlock`,
  `calcUserSampleOscRingBlock`, and `calcPeriodicInterpRingBlock` (partners
  via `osc_renderPlainWave()`, the ring applied in the blend).
- **The dispatcher (`calcNextOscSampleRingBlock(osc, modBuf, buf, size,
  depthQ15, depthStepQ15)`)** mirrors `calcNextOscSampleBlock()`,
  including the user-sample range check that returns without writing
  `buf`.
- **Depth mapping (`osc_ringDepthQ15()`):** round(amount × 32768); ≤ 0 and
  NaN → 0; ≥ 1 → 32768. It replaced `osc_ringK()`, the P1 dry gain
  K = (1 − amt) × 32768, used with a modulator at gain `amt`.

### 5.4 Placement and attributes

- **`OSC_S085_SECTION`:** `section(".itcm.s085")`. The linker
  (`STM32F765VIHx_FLASH.ld:148–151`) places `*(.itcm) *(.itcm.*)`, so the
  subsection follows all existing ITCM code inside the one copied output
  section.
- **`OSC_S085_LOOP`:** that section + `noinline, noclone`, for every copy
  and the builder.
- **`OSC_S085_DISPATCH`:** that section + `noinline, noclone`, for the two
  public dispatchers. This keeps the interpolated blends in ITCM instead of
  inlined into the flash-resident Drum render (LTO merges the Drum render
  into `mixer_calcNextSampleBlock()`).
- **`#error`** unless `INTERPOLATE_OSC == 1`: the copies implement only the
  interpolated read.

### 5.5 Menu, MIDI, Morph, storage

**Menu:**
- `MenuText.h` `mixFmNames[][4] = {{4}, "fm ", "mix", "snc", "rng"}`;
  `menuText_mix`/`menuText_fm` removed.
- `menu_getMixFmName()`: any byte ≥ 4 shows `fm`, matching the render;
  `_Static_assert` 5 rows.
- It serves the six `DTYPE_MIX_FM` sites: `va_formatValue3()`,
  `menu_formatCellValue3()`, `menu_clampCellValue()` (clamp to 3),
  `menu_automationValueMax()` (3), `menu_formatAutomationValue3()`,
  `menu_repaintGeneric()`.

**MIDI:** a MIDI write is clamped by `menu_clampCellValue()`, so CC/NRPN 2
gives `snc` and 3–127 give `rng` (before S085, 1–127 gave `mix`).

**Morph:** steps through the modes (Q7b). An LFO can't target the row
(`MOD_NONE`).

**Storage:**
- No new rows, keys, automation IDs or NRPN IDs. `special_tags` is
  unchanged at 155 rows.
- Old kits load with the same meaning.
- An older firmware plays 2/3 as `mix`.
- The LXR1/LXR-02 converters write only 0/1.

---

## 6. Deviations from the plan (all accepted)

| # | Plan | Built | Why |
|---|---|---|---|
| D1 | `rng` = plain carrier + a ring pass in `BufferTools.c` | the ring law folded into per-waveform carrier copies | Measured: the pass cost 9 (scalar) or 7.5 (packed), so `rng` would be 89.5–91 vs `fm` 90 with user samples, failing the gate. A packed `SMULBB` form can't take K + mod (it exceeds int16). |
| D2 | the clock in `modOsc.phase` | `DrumVoice.syncClock` (+4 B) | Advancing a one-shot user-sample phase past its end would make a later `fm`/`mix` render read up to 64 KB past the sample, beyond flash near the top of sample flash (HardFault). |
| D3 | the sync loops multiply by the gain | store `oscOut` | `snc` is always gain 1; the result is exact |
| D4 | `Drum_calcVoiceSyncBlock` disassembly identical | the callees byte-identical; the Drum dispatch checked by call targets (§9.3) | the `if` → `switch` change is unavoidable per-block code |
| D5 | ITCM +1.0–1.5 KB | P1 +3,392 B; final +3,624 B | ring copies for five families; interpolation variants inlined into the dispatchers; D8 |
| D6 | the list on the stack | same (`syncOver[32]`, 128 B, SRAM2 stack) | not DTCM: the FX-arena margin |
| D7 | new code inside `.itcm` | `.itcm.s085`; `elf_symdump.py` `PADDING` rule | Appending inside `.itcm` moved `calcUserSampleOscBlock` out of 16-bit tail-branch range and turned a clone's tail into `b.w`. The subsection keeps every existing instruction. One unused trailing literal-pool word at `calcNextOscSampleBlock.constprop.0` remains; all 14 PC-relative loads there are identical. |
| D8 | the unsynced fallback via `calcNextOscSampleBlock(…, 1.f)` | `osc_renderPlainWave()` calling the leaf loops; `noinline` dispatchers | New constant-gain call sites made LTO re-specialise the two baseline clones (they swapped roles and bodies). In the image, `osc_renderPlainWave.constprop.0` (728 B ITCM) reproduces the baseline gain-1 clone loop for loop (22, 23, 23, 37, 23, 70, 82, 10, 94/95), so no per-sample cost. |
| D9 | `snc` depth / `rng` amount = `amt` | d = amt² × env (§10.1) | user, after the P1 hardware pass |
| D10 | `rng`: modulator at gain `amt`, K per block | modulator at gain 1, per-sample Q15 depth ramp, snap after a trigger (§10.2) | user: "rng sounds crunchy" (a 1.4 kHz per-block zipper) |

---

## 7. Measurements

### 7.1 New loops (instructions per sample)

| Loop | Standalone | Linked |
|---|---:|---:|
| `osc_buildSyncList` | 7 (both paths; fpseq's second figure, 16, spans both branches plus the epilogue) | 7 |
| Sync table / sine / crash (no reset / reset) | 34/39, 33/38, 36/41 | same |
| Ring P1 table / sine / crash / noise / user sample | 28, 27, 30, 13/25, 40/41 | user sample 42/43 |
| Ring ramped (D10) table / sine / crash / noise / user sample | 31, 30, 33, 16/18, 46/48 | 31, 30, 33, 16/19, 49/50 |

**Rejected prototypes:**
- the separate ring pass: scalar 9, packed (`SHADD16`/`SMULBB`/`SMULTT`/
  `PKHBT`) 7.5;
- the first sync prototype with a gain multiply: 34/39 (the same; the
  compiler branched the select).

### 7.2 Oscillator section compositions (instructions per sample)

| Mode | Table | Sine | Crash | User sample (dearest) |
|---|---:|---:|---:|---:|
| `fm` | 24+35 = 59 | 53 | 68 | 90 (linked ≈ 92) |
| `mix` | 53 | 51 | 65 | 87 |
| `rng` (P1) | 52 | 50 | 60 | 82 (linked ≈ 84) |
| `rng` (ramped, final) | 55 | 53 | 63 | ≈ 87 (linked ≈ 90–91) |
| `snc` | 7 + 39 = 46 | 45 | 48 | 48 (unsynced user slave) |

**The `rng` modulator binding:**
- In `rng` (gain 1.0f) the modulator call binds to the baseline gain-1
  clone `calcNextOscSampleBlock.constprop.1`, the one waveform
  interpolation uses. It is unchanged and slightly cheaper than
  `.constprop.0`, which `fm` and `mix` use.
- **Final gate:** the dearest `rng` ≈ 90–91 vs the dearest `fm` ≈ 92
  (linked). It passes, by the narrowest margin of the four modes.
- **Same-wave comparisons (reported):** sine `rng` ties with `fm`; a noise
  carrier is +3 over `fm`-with-noise (`fm` ignores FM on noise).

### 7.3 Build trail (DEV, `make all && make img`)

| Point | text | data | bss | Flash payload / headroom | ITCM |
|---|---:|---:|---:|---|---:|
| Baseline (`1ea234ea`) | 609,288 | 424 | 430,200 | 609,712 / 143,952 | 4,168 |
| P1 (`8812bffd`) | 613,232 | 424 | 430,248 | 613,656 / 140,008 | 7,560 |
| D9 (mod envelope) | 613,248 | 424 | 430,248 | 613,672 / 139,992 | 7,560 |
| D10 (`rng` ramp), final | 613,536 | 424 | 430,296 | 613,960 / 139,704 | 7,792 |

- **No new warnings.** Only the four newlib `_close/_lseek/_read/_write`
  linker notes, and the LTO serial-compilation note.
- **DTCM** 4,472 B and the FX-arena margin 3,712 B are unchanged
  throughout.

**ITCM by function (final, `nm -S`, bytes):**

| Function | Bytes |
|---|---:|
| `calcNextOscSampleSyncBlock` | 396 |
| `calcNextOscSampleRingBlock` | 552 |
| `osc_renderSyncWave.constprop.0` | 232 |
| `osc_renderPlainWave.constprop.0` | 728 |
| `osc_buildSyncList` | 54 |
| Sync copies: table / sine / crash | 176 / 172 / 176 |
| Ring copies: table / sine / noise / crash / user sample | 192 / 184 / 108 / 196 / 448 |

---

## 8. Test bench additions (`tools/dsp_test/`)

| Item | What |
|---|---|
| `snapshot85.sh` → `frozen85/` | the pre-change `Oscillator.c/.h`, `BufferTools.c/.h`, `DrumVoice.c/.h`, `DrumParameters.c`, `MenuText.h`; plus `elf_osc.txt` (12 linked symbols) |
| `elf_symdump.py` | dumps normalised disassembly of listed symbols (all `.constprop/.isra/.part/.cold` clones), with absolute addresses and branch-target numbers removed; `--compare` reports MISSING / CHANGED / ADDED; `PADDING` for a trailing run of `.word ADDR` only (D7) |
| `oscmod_identity` | no removed or changed line in `Oscillator.c/.h` against `frozen85/` (added lines only); `BufferTools.c/.h` byte-identical |
| `osc_mod` | `test_osc_mod.c` on code extracted from the current source (`extract.py`): the plain loops, the S085 functions, the header mappings, `OscInfo`/`SampleInfo` by line ranges, `osc_interp_a/b`; host stubs for RNG, SampleMemory and modulationNode; the tests pre-fill the sample cache |
| `armcheck-oscmod` | compiles `Oscillator.c` with the firmware include set (`FW_INC` parsed from the root Makefile `CFLAGS` block) and reports every new loop beside the loop it replaces |
| `elf_osc_baseline` / `elf_osc_check` | G-A on the linked image |

**Test cases (final):**

| Case | Check |
|---|---|
| R1 | ring law, constant depth, every carrier family incl. interpolation and looped/one-shot samples, against an independent reference |
| R2 | the ramped law; depth 0 = carrier exactly (any modulator) |
| R3 | the integer law vs the float law car·(1 − d) + car·mod·d within 2 LSB |
| S1 | sync at depth 0 == the plain render (samples and phase), resets present |
| S2 | depth 1: a reset after sample k lands on k + 1 at overshoot × ratio |
| S3 | `osc_syncPull()`: NONE passes, the short way round, depth 1 = target, depth 0.25 |
| S4 | the builder against a 64-bit reference; overshoot < inc |
| S5 | clock scaling (`<< 5`, the clamp) and equal reset rates at equal frequency for `Tri` and crash |
| S6 | unsynced slaves == the plain render |
| S7 | interpolated partners share the list; the phase rule |
| M1 | `osc_syncDepthQ30()`, `osc_ringDepthQ15()`, `osc_syncRatioQ16()` at 0, 0.25, 0.5, 1, < 0, > 1, NaN |

**Break proofs (each made its case fail; verified in scratch copies):**
- the ring `>> 14`;
- the ring depth interior formula;
- K/depth 0 at amount ≤ 0;
- the pull applied unconditionally;
- an unsigned pull difference;
- reset at i (pull before the advance);
- the ratio inverted;
- `<< 5` removed;
- the builder carry inverted;
- interpolation partner B plain;
- the interpolation phase rule reversed;
- depth in Q31;
- (D10) the depth stepped after use, the step ignored, (1 − d) for d.

**Host-test pitfalls found while writing it:**
- `0x12345 × 3` is `0x369CF`;
- at amt 0 the old law needed a zero modulator block;
- an interpolated noise → crash slave syncs its crash partner, so depth-1
  identity cases skip interpolation.

---

## 9. Assessment of the implementation (schedule §12)

### 9.1 Conformance

| Change | Result |
|---|---|
| O-H1, the Drum, menu and parameter changes, X1 | verbatim |
| O-C1 | verbatim except D7/D8 |
| X2/X3/X4 | plus the D7/D8 additions |

### 9.2 Gates rerun

| Gate | Result |
|---|---|
| `oscmod_identity` | OK |
| `osc_mod` | 0 failures |
| `elf_osc_check` | 0 missing, 0 changed, 0 added, one `PADDING` line |
| `special_tags` | 155 / 0 |
| `armcheck-oscmod` | the documented figures exactly |
| Ledgers | equal to the implementation log |

### 9.3 Drum render in the linked image (closes D4)

- **Method:** the baseline was rebuilt from `HEAD` in a scratch git
  worktree, which reproduced `frozen85/elf_osc.txt` byte for byte. The call
  targets of `mixer_calcNextSampleBlock()` (into which LTO merges the Drum
  render) were then compared.
- **Unchanged:** 21 call targets.
- **`calcNextOscSampleBlock.constprop.0`:** 7 → 9 call sites (the `rng`
  modulator and `fm`'s separate modulator call).
- **New:** one call each to the sync dispatcher, the ring dispatcher and the
  builder.
- **No new clone of any existing function;** +75 lines of per-block code.

### 9.4 Findings, all fixed at the close

| # | Finding | Fix |
|---|---|---|
| F1 | stale comments in `osc_renderSyncWave()` and the two interpolated variants' "Placement" lines | now name `osc_renderPlainWave()` and `.itcm.s085` |
| F2 | `STORAGE_SRAM_MANIFEST.md` `itcm.s085` | `.itcm.s085` |
| F3 | linked vs standalone user-sample ring figures | superseded by the D10 figures, stated both ways in the DSP reference |
| F4 | `DSP_TEST.md` `elf_osc_check` expected output | now includes the `PADDING` line |

No correctness, CPU or RAM defect was found.

---

## 10. Follow-ups after the P1 hardware pass

### 10.1 D9: the mod envelope in `snc` and `rng`

**User:** "the snc and rng modes don't use the mod envelope the way that fm
does. they should" — and "use amt² for all three, same response as fm".

**Facts:**
- `Drum_calcVoiceAsync()` sets `osc.fmMod = fmModAmount × egPitchVal`
  every block, in every mode. `egPitchVal` is the PitchMod decay envelope
  (`DecayEg`, 1 at the trigger, decaying to 0).
- The PitchMod amount (`egPitchModAmount`) scales only the pitch sweep.
- `fm`'s index = (modulator × amt) × `osc.fmMod` = amt² × env.

**Built:**
- `static inline float Drum_modEnvDepth(voice)` = `fmModAmount ×
  osc.fmMod`, used as the `snc` depth and the `rng` amount. `fm` is
  unchanged.
- **Edge case:** `DecayEg_calc()` subtracts past zero, so one block can be
  slightly negative. Both mappings clamp it.

**Effect:**
- over a hit, `snc` fades to a free-running slave and `rng` to the dry
  carrier;
- a long PitchMod decay gives roughly the old static sound;
- `mix` uses `amt` alone (user: only `snc`/`rng` named).

**Cost:** one multiply per block; +16 B text.

### 10.2 D10: the `rng` depth ramp

**Problem:** with D9, d stepped once per 32-sample block in both the
modulator gain and the dry gain, which is audible as a 1.4 kHz zipper.

**Built:**
- **The modulator renders at gain 1.** The ring copies apply d per sample
  (the law in §5.3).
- **Ramp state (`DrumVoice.int32_t ringDepthQ15`):** the value reached at
  the end of the last `rng` block. The reached value is stored, not the
  target, so the integer step never drifts.
- **Snap (`DrumVoice.uint8_t ringSnap`, in the padding after `modMode`):**
  - set by `Drum_triggerVoice()` (and by init);
  - the first `rng` block after a trigger starts at its own d (step 0),
    then clears it.
  - **Why snap (user choice):** hits start at full depth, as `fm` hits do,
    and the carrier phase resets on the same trigger anyway.
  - **The alternative, "ramp always"** (a 0.73 ms fade-in of the ring per
    hit): **the user wants to try it in the free-run run mode when the run
    modes return (S087).**

**Not ramped: `snc`.** Its depth acts only at reset instants and puts no
step into the audio (user agreed). A ramp would cost +1 per sample if ever
needed.

**Cost:**
- the ring copies +3 per sample; one integer divide per block;
- RAM: `DrumVoice` 592 → 596 B, `runtime_slots` +48 B;
- ITCM +232 B.

---

## 11. Resources (RAM policy record)

| Item | Bytes | Region / owner |
|---|---:|---|
| `DrumVoice.syncClock` (D2) | +4 per Drum | `runtime_slots` (SRAM1, InstrumentManager); the reserve is 2 × `sizeof(DrumVoice)`, so +8 per slot, +48 total |
| `DrumVoice.ringDepthQ15` (D10) | +4 per Drum | same: +48 total |
| `DrumVoice.ringSnap` (D10) | 0 | padding after `modMode` |
| `syncOver[32]` | 128 | audio stack (SRAM2), `snc` blocks only |
| ITCM code | +3,624 | `.itcm.s085` (ITCM is RAM under the policy) |

- **Totals:** `DrumVoice` 588 → 596 B; `runtime_slots` 7,056 → 7,152 B;
  `.bss` 430,200 → 430,296 (+96).
- **Approval:** the user accepted the RAM at plan r3 ("other
  recommendations accepted; ram accepted") and confirmed the +48 B of D10
  ("recommended, snap").

---

## 12. Hardware record

| Build | Result |
|---|---|
| P1 (`8812bffd`) | "P1 checks out completely fine on the hardware" (user) |
| D9 | `rng` "sounds crunchy" → D10 |
| D10 | built, gates passed; the user's re-test was not reported before the close |

**Device rows still worth running on the final build** (the plan's device
matrix):
- **H1:** `rng` `amt` sweep, every `wav` on both oscillators.
- **H2:** `snc` `amt` 0 → 127; slave sweep; each stock slave; LFO on
  `amt`.
- **H3:** `snc` with each FM-page wave at one `frq` (the same reset rate).
- **H4:** `fm`/`mix` A/B against the S084 build.
- **H5:** names, clamps, step automation 0–3, MIDI 0–3 and 127, Morph
  across the row, kit save and reload.
- **H6:** the six-slot `cpu` comparison, especially `rng` with user samples
  (the narrowest margin).
- **D10:** a very short PitchMod decay (no crunch); the attack per hit
  (snap).

---

## 13. Lessons recorded (for every later oscillator session)

1. **Never edit an existing loop to add a feature.** Add copies (plain, FM
   carrier, sync slave, ring carrier) and prove the old ones unchanged
   (`oscmod_identity` for source, `elf_osc_check` for the image).
2. **Put new ITCM code in its own subsection** (`.itcm.sNNN`). Appending
   inside `.itcm` moves later code and can turn short branches long (D7).
3. **Do not add constant-argument call sites to existing dispatchers.** LTO
   re-specialises their clones (D8). Call the leaves through a new
   dispatcher.
4. **Mark new loops `noinline, noclone`.** They then have their own
   symbols for `fpseq.py` and the ITCM ledger.
5. **Measure standalone and linked.** They differ by 1–3 instructions on
   the user-sample loops.
6. **A per-block parameter that scales amplitude needs a per-sample ramp,**
   or it zippers (D10). Phase-domain parameters (the FM index, the sync
   depth) tolerate per-block steps.
7. **Check the hazards of state you stop updating** (D2: a frozen or
   advanced phase is read later by another mode's loop).

---

## 14. Future planning recorded this session

### 14.1 Sample and wavetable sync (plan §12, user r4)

| FM-page (master) source | Optimizer flag present | No flag |
|---|---|---|
| One-shot sample (`smp`) | resets at the zero crossings (bitmap) | **as noise**: one reset per nominal period at `frq` |
| Looped sample (`slp`) | zero crossings (bitmap) | **reset when the sample loops** |
| Wavetable (`wtb`) | zero crossings (bitmap, per frame) | **reset when the frame loops** (the phase wrap) |

- **Each master kind is a reset-list builder;** the slave copies are
  unchanged.
- **The loop-wrap builder:** a constant-cost compare and subtract; one
  modulo per block only when the step exceeds the loop length.
- **The bitmap:**
  - 1 bit per point (an upward crossing with hysteresis), stored after the
    item in sample flash (6.25 %);
  - `RBIT`/`CLZ` on the two words covering the step; about 12–15 per
    sample;
  - with interpolated frames, the nearer frame's bitmap is chosen once per
    block (to confirm then).
- **The optimizer flag:**
  - a private RIFF chunk (e.g. `lxrO`: a version byte and a flags byte,
    bit 0 = "generate the crossing bitmap"), written **before** `data`
    (the rollback's `filesystem_parseWav()` stops at `fmt `+`data`);
  - the optimizer must rewrite every file it outputs (it byte-copies
    unchanged files today; Python's `wave` module cannot write custom
    chunks: a small RIFF writer is needed);
  - wavetables: flagged when all their frame files are;
  - the installer sets a catalogue v2 flag bit and generates the bitmap
    from the installed data (memory-mapped flash, no RAM buffer);
  - a stale flag is harmless (the bitmap is computed from the data).
- **One-shot end:** no crossings after the last point; the master position
  holds. With no `modOsc` reset, a one-shot master drives resets only on
  its first pass (the same as in `fm`/`mix` today).

### 14.2 Run modes (S087)

The `rng` depth "ramp always" (no snap) is to be tried in the free-run
mode.

### 14.3 S086 (user, end of S085): `S086_P1_OSC_TYPE_EXPANSION.md`

**Stages, each with its own device CPU gate:**
- S1: the new stock waves (`Re3 Sw3 ReA TrA SwA`);
- S2: the sample rework (`smp`/`slp`, catalogue v2, the installer);
- S3: `wtb`;
- S4: the sample/wavetable optimizer with the flag and the bitmap;
- S5: re-examine `pwm` without blowing up the CPU (evaluate first).

**Menu (user):**
- the first oscillator screen's fourth cell is **`pos`**, category
  `Oscilltr`, long name `Position`: the sample start position or the
  wavetable position, and possibly the **phase offset** for the stock
  waves;
- the **second oscillator screen** holds `typ`, and `run` (S087).
- `pos` replaces the rollback's continuous `mod` row, which removes the
  Drum FM page's `mod`/`mod` clash.

**Proposals in that plan (open questions there):**
- the phase offset adds `pos`/128 × 2^32 to today's start phase (0 =
  unchanged), stored in the unused `OscInfo.startPhase`;
- the Snare screen 1 becomes `coa fin wav pos` (`noi mix` move to screen
  2);
- keys `oscN_type`/`oscN_position`.

**Inputs carried for S086:**
- `S085_SAMPLE_INDEX_SPEEDUP.md`: power-of-two wavetable frames; sample end
  padding; the wavetable frame blend uses an interpolation pool slot with
  fallback stepping (user rule);
- the rollback's per-type costs (§2.2);
- the rollback's menu decisions (§2.4).

---

## 15. Affected files and API changes

### 15.1 Files

| File | Change | Commit |
|---|---|---|
| `Core/DSPAudio/Oscillator.c` | the S085 P1 section after `calcSampleOsc()` (§5); F1 comments | P1 `8812bffd`; D10, F1 uncommitted |
| `Core/DSPAudio/Oscillator.h` | the S085 P1 block before `#endif` | P1; D10 uncommitted |
| `Core/DSP/Instruments/Drum/DrumVoice.h` | `DRUM_MOD_*`; `modMode`; `syncClock`; `ringSnap`; `ringDepthQ15` | P1; D10 uncommitted |
| `Core/DSP/Instruments/Drum/DrumVoice.c` | init; trigger `ringSnap`; the `osc.fmMod` comment; `Drum_modEnvDepth()`; the switch | P1; D9/D10 uncommitted |
| `Core/DSP/Instruments/Drum/DrumParameters.c` | `osc2_mod_type` → `modMode` | P1 |
| `Core/Menu/MenuText.h`, `menu.c` | §5.5 | P1 |
| `tools/dsp_test/*` | §8 | P1; D10 tests uncommitted |
| `build/LXRV2_lxr02.img` | rebuilt | — |
| Specifications | §17 | P1 partly; the close uncommitted |
| `S086_P1_OSC_TYPE_EXPANSION.md` | new | uncommitted |

### 15.2 API

| API | Change |
|---|---|
| `OSC_SYNC_NONE` | new macro (`Oscillator.h`) |
| `osc_syncClockInc(master)` | new |
| `osc_buildSyncList(clock, clockInc, over, size)` | new; returns the end clock |
| `calcNextOscSampleSyncBlock(osc, over, clockInc, depthQ30, buf, size)` | new |
| `calcNextOscSampleRingBlock(osc, modBuf, buf, size, depthQ15, depthStepQ15)` | new (P1 signature `…, ringK` replaced in D10) |
| `osc_syncDepthQ30(amount)`, `osc_ringDepthQ15(amount)` | new static inline; `osc_ringK()` removed (D10) |
| Static in `Oscillator.c` | `osc_syncPull`, `osc_ringSample`, `osc_syncRatioQ16`, `osc_syncWaveform`, `osc_renderPlainWave`, `osc_renderSyncWave`, `calc{Sine,WavetableOsc,SampleOsc}SyncBlock`, `calc{Sine,WavetableOsc,Noise,SampleOsc,UserSampleOsc}RingBlock`, `calcPeriodicInterp{Sync,Ring}Block`; macros `OSC_S085_SECTION/_LOOP/_DISPATCH` |
| `DrumVoice` | `mixOscs` → `modMode`; + `syncClock`, `ringSnap`, `ringDepthQ15`; `DRUM_MOD_FM/MIX/SNC/RNG/COUNT`; static `Drum_modEnvDepth()` |
| Menu | `mixFmNames`, `menu_getMixFmName()`; `menuText_mix`/`menuText_fm` removed |

---

## 16. Accepted behaviours and known limits (not bugs)

**`snc`:**
- The FM-page `wav` has no audible effect; crash and user-sample masters
  act as noise.
- Noise and user-sample slaves are unsynced.
- The first reset after a hit falls at a different place each hit
  (`modOsc`/`syncClock` run free; Q6b).
- An LFO on the FM-page `wav` still claims a waveform-interpolation unit
  for an unrendered oscillator (no CPU increase; another voice may lose
  interpolation that block).

**Drum FM oscillator (all modes):**
- A one-shot user sample there never restarts (no `modOsc` reset). It
  plays once after selection or power-up, then holds.

**`rng`:**
- The margin against `fm` is 1–2 instructions (linked, user samples).
- At the same waves: sine ties with `fm`; noise is +3 over `fm`-with-noise.
- The snap makes the first block of a hit start at full ring depth.

**Row and storage:**
- MIDI 2–127 changed meaning (2 `snc`, 3–127 `rng`).
- A Morph across the row steps through the modes.
- A stray byte > 3 plays and shows `fm`.
- An older firmware plays 2/3 as `mix`.

**Pre-existing, kept for parity (copied verbatim into the new loops):**
- the crash read of `crashSample[32768]` at the last point;
- an empty or out-of-range user sample returns without writing `buf`.

---

## 17. Specification reference updates (S085 close)

| Document | Update |
|---|---|
| `INSTRUMENTS_DSP_REFERENCE.md` | §5.1: the Drum diagram, the mod modes, the mod envelope, the `rng` ramp, the trigger. §5.5: ITCM. §6.1: the sync/ring oscillator support (functions, laws, placement). §8.1–§8.2: costs. §9.3: how to add an oscillator mode or type under the CPU rule. §10: pitfalls (D2, D7, D8, zipper). §11: history. |
| `CPU_USE_DSP_AUDIT.md` | items 36–39 (`rng`, `snc`, `fm`/`mix` unchanged, the `rng` ramp) |
| `STORAGE_SRAM_MANIFEST.md` | the S085 build, `runtime_slots` 7,152 B, `DrumVoice` 596 B, the ITCM table with every S085 function |
| `MIDI_SPEC.md` §5.5 | the `osc2_mod_type` value domain and the change of meaning |
| `FILESYSTEM_SPEC.md` | `osc2_mod_type` values 0–3 |
| `tools/dsp_test/DSP_TEST.md` | the layout and target rows for the S085 tools |

---

## 18. Carry-forward

1. **Hardware:** re-test D10 and run the §12 device rows on the final
   build.
2. **Commit state:** D9, D10, F1/F4 and all close-out documents are
   uncommitted on `dev-ph7-voice_expansion` (P1 is `8812bffd`).
3. **Disposable (user's plan):** root `S085_P1_OSC_MOD_SNC_RNG.md` and
   `S085_P1_OSC_MOD_IMPLEMENTATION.md`; this log and the specifications
   carry them.
   - **Keep until S086 is done:** `S085_OSC_MOD_CHERRYPICK.md`,
     `S085_SAMPLE_INDEX_SPEEDUP.md`, `S086_P1_OSC_TYPE_EXPANSION.md` and
     both failed trees (the S086 inputs).
4. **Next:** S086 per `S086_P1_OSC_TYPE_EXPANSION.md`; its open questions
   Q1–Q11 first; then S087 run modes (with the free-run "ramp always"
   trial).
5. **Still open from S084:** hardware V1–V7 (confirm closed or waived); the
   spike's unassigned items. **From earlier sessions:** F5 (Scene 2 Case-3
   invalidation), S082 P3 A2, S081 P3 A-1 (decision), the production-build
   re-measure (S081–S085 unmeasured), pre-existing warnings
   (`volatile/S078_WORKING_NOTES.md`).
6. **`.gitignore`:**
   - it lists `Helicase-dev-ph7-spike/`, which now lives in the root again
     (correct);
   - `Helicase-dev-ph7-voice-pwm-spike/` is not ignored (it shows as
     untracked); add it if it should stay out of commits.
