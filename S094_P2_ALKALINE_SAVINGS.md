# S094 P2 — Alkaline Savings

- **Status:** investigation and proposal (S094, 2026-11-11), revised after
  the second device run.
  - Done: AS-1 (the probe, §10), the overdrive change (§10.2) and the
    probe's second version (§14).
  - Measured: run 1, uncached (§11, settings §12); run 2, SRAM cached
    (§17).
  - **Budget adopted (user): §21.** Under a 97 % cap: four Basic slots at
    19,620, two Advanced slots at 21,803, the Effect at 16,634, and 500
    left unallocated.
    (§18's even split is kept as the record.)
  - **Implemented in the tree (§22):**
    - AS-6, the SRAM D-cache;
    - AS-10, `QADD`;
    - the Cymbal and HiHat moved to Basic: no current type is Advanced.
  - Open: §7 (AS-2, AS-5, AS-7, AS-8, AS-11, AS-12, AS-13).
  - Figures marked *measured* come from
  linked scratch builds of the current tree (S094 P1 after S5), counted with
  the project tools (§8). Figures marked *estimate* or *hypothesis* are not
  measured.
- **Trigger (user, device test):** PWM works. Three Alkaline instances pin the
  CPU and the interface lags badly; a fourth would not run. The Alkaline
  should come down to **at or very near the current Drum**. Removing PWM is
  allowed: a PWM-like sound is available to the user as a wavetable.
- **Binding rules that apply:**
  - S089 / S093 cost rules;
  - ITCM and RAM growth needs sign-off;
  - DEV logging allocations exist only in a `DEV_MODE_LOGGING` build;
  - no step of this plan edits `MEMORY.md` or `knowledge_files/` (§9 lists
    what goes out of date).

---

## 0. Summary (revised after run 2)

**Caching SRAM recovered 14.8 % of the block on the same Scene.** Six
Alkalines now run at 80.9 %; the original complaint was that three pinned
the CPU. The worst Scene measured (four Alkalines, two HiHats, CrumpBit on
grain, the compressor on) averages **84.0 %**, with a 94.7 % peak (§17).

| Measured | Uncached (run 1) | Cached (run 2) |
|---|---:|---:|
| FluxTime, whole block | 148,965 (95.1 %) | **126,979 (81.0 %)** |
| Six Alkalines | — | 126,853 (80.9 %) |
| Four Alkalines + two HiHats + grain | — | **131,561 (84.0 %)** |

**Budget (§21, user): a 97 % average cap, Basic and Advanced by slot count.**

| Item | Cycles per block | % |
|---|---:|---:|
| Outside the mixer (menu, filesystem, background) | 4,701 | 3.0 |
| Shared mixer (control, set-up, compressor; worst observed) | 12,784 | 8.2 |
| Effect (CrumpBit with the SVF back on `grn`/`rvb`) | 16,634 | 10.6 |
| Four Basic slots × 19,620 | 78,480 | 50.1 |
| Two Advanced slots × 21,803 | 43,606 | 27.8 |
| Unallocated under the cap (slack) | 500 | 0.3 |

Every slack in the budget is marked in §21.

- **Every current type is Basic** (the Cymbal and HiHat moved, §22).
- The Advanced class, at most two per Scene, is kept for a future type.
  The first candidate is an Alkaline with wavefolders and tone.

**Implemented (§22):** the SRAM D-cache (S = 0) and `QADD`. The stereo add
loops fell from 41-42 to 26 instructions per sample (dry) and from 56-57
to 33-34 (with a send).

**The largest shared item is the output add (§19):** 1,936-3,037 cycles per
slot, about 9.5 % of the block. Its saturating add compiles to a 64-bit add
and two compare-and-branch pairs per channel, where one `QADD` gives the
same result bit for bit. The recommended next build: the cache fix in the
tree, that change (M1) and the single conversion (M2). They are not needed
to meet the budget. They lower every Scene by about 3-5 % (estimate),
which keeps the menu and Load/Save fast.

---

## 1. Where the Alkaline stands (measured, current image)

| Path | Loop | Per block | Whole voice |
|---|---:|---:|---:|
| `wav` / `wtb`, no slot (`DiodeLadder_renderB2Osc`) | 221 | 1,367 | **263.7** |
| `wtb` with a slot (`…Osc2`) | 234 | 1,367 | 276.7 |
| `pwm` (`…OscPwm`, read form 2) | 215 | 1,330 | 256.6 |
| *S0, before S094 (for reference)* | *213 / 228* | *1,378* | *256.1 / 271.1* |
| *Drum dearest (S6, `basic_worst.txt`)* | | | *257.8* |

- **VDIV:** 3 per sample in the loop plus 9 per block (3.28 per sample),
  against the Drum's 3.
- **What S4 added:** +8 on the one-row loop (213 → 221). The three new live
  floats (`otaIn`, `toneC`, the tone state) pushed the loop into spill
  reloads: the function's stack loads and stores went from 30 to 41.

---

## 2. Measured options (linked scratch builds, §8)

| Variant | One-row loop | Blend loop | PWM loop | Per block | **No slot** | Slot held | What is lost |
|---|---:|---:|---:|---:|---:|---:|---|
| Current | 221 | 234 | 215 | 1,367 | **263.7** | 276.7 | — |
| **T: tone removed, overdrive kept** | **215** | **227** | 207 | 1,362 | **257.6** | **269.6** | the tone control |
| N: tone and overdrive removed (A3 kept) | 213 | 228 | 207 | 1,358 | 255.4 | 270.4 | both post-filter controls |
| A4: one expo pass (`ALK_EXPO_PASSES` 1) | 221 | 234 | 215 | 1,335 | 262.7 | 275.7 | the expo current's second pass (P9-D3 dropped it for sweep accuracy) |
| T + A4 | 215 | 227 | 207 | 1,330 | 256.6 | 268.6 | as T and A4 |
| P: Alkaline PWM removed | 221 | 234 | — | 1,366 | 263.7 | 276.7 | `typ pwm` on the Alkaline |

**Readings:**
- **The tone costs 6 per sample; the overdrive about 2.** Once the tone
  is gone, the overdrive's single extra live value no longer spills (215
  against S0's 213). The overdrive is the post-FX design's main feature
  (P9-A′: "the OTA is the circuit's own nonlinear stage"); the tone was its
  helper.
- **Removing the Alkaline PWM saves nothing on the worst path.** PWM is the
  Alkaline's cheapest path (256.6), and its per-block branch is not on the
  longest per-block path (`alk_renderWtb` 412 sets it, not `alk_renderPwm`
  375).
- **T reaches the Drum:** 257.6 against 257.8.

---

## 3. Why the device disagrees with the count (hypotheses, measured in step A)

### 3.1 Instruction fetch from flash

| Code | Bytes | Where | Runs |
|---|---:|---|---|
| `Alkaline_calcVoiceAsync` | 4,208 | flash | once per block per voice, mostly straight-line |
| `dl_slowUpdate` | 1,608 | flash | once per block per voice |
| `DiodeLadder_renderB2Osc` (`…Osc2` 1,484; `…OscPwm` 1,368) | 1,402 | flash | the loop, 32 times per block |
| `alk_render*`, sync block, trigger | ≈ 600 | flash | per block |
| *`mixer_calcNextSampleBlock`* | *12,644* | *flash* | *every block* |
| *the Drum's sync block / `SVF_calcBlockZDF`* | *2,408 / 3,312* | *flash* | *per block* |
| *the Drum's oscillator loops* | *—* | *ITCM* | *—* |

- **The Drum's per-sample oscillator loops run from ITCM:** no cache, no
  wait states.
- **The Alkaline's whole path runs from flash:**
  - flash sits behind the 16 KB instruction cache (7 wait states at
    216 MHz, through the ART accelerator);
  - one Alkaline's per-block working set is about 7.3 KB;
  - the mixer alone is 12.6 KB.
- **The consequence:** straight-line control code (the 4.2 KB async), used
  once per block, is likely refetched every block.
- **Order of magnitude (estimate, not measured):** 131 cache lines of
  32 B × ≈ 10 cycles per miss ≈ 1,300 cycles per block, about 40 cycles per
  sample per Alkaline. That is several times the whole difference in §2.

### 3.2 The loop's dependent arithmetic

- **Three VDIV per sample:** the continuants' reciprocal, the loop closure
  and the secant batch. They are serial: each feeds the next sample's or the
  next stage's operands.
- **Hidden latency:** the M7 does not overlap a 14-cycle VDIV with dependent
  work, and the tridiagonal forward/back substitution is one long chain. The
  instruction count charges a VDIV as 1 instruction (the S093 cycle proxy
  charges 14), and charges dependent float ops at 1 even when the pipeline
  stalls.
- **The Drum** carries the same 3 VDIV (SVF 2, distortion 1) but in shorter
  chains.

### 3.3 What step A must answer

1. Cycles per block of one Alkaline slot (async + sync), against one Drum
   slot at its dearest, both alone and with three Alkalines in the Scene
   (cache sharing).
2. The same with variant T, and with the Alkaline's ladder loop placed in
   ITCM (a scratch measurement image).

---

## 4. The PWM question

| Remove | CPU | Memory | Sound |
|---|---|---|---|
| **Alkaline PWM** (`typ` 2) | **0** on the worst path (it is the cheapest path) | flash −≈ 1.6 KB (the wrapper 1,368 B, `alk_renderPwm` 192 B) | the Alkaline's band-limited variable pulse and two-saw; a PWM wavetable under `wtb` gives a stepped or blended approximation |
| **Drum-side PWM** (`typ` 4 on Drum, Snare, Cymbal, HiHat) | the S3 decimator +1 on every SRR voice goes (the bisection tied it to the PWM dispatcher cases, `S094_P1_IMPLEMENTATION.md` S3 row) | **ITCM −≈ 2.6 KB** (13,984 → ≈ 11,450 of 16,384); flash −≈ 4 KB | PWM on the drum instruments; a wavetable is the substitute |
| Both | as above | ITCM −2.6 KB, flash −≈ 5.5 KB | as above |

- **Removing PWM does not, by itself, bring the Alkaline down.** It matters
  because of what the freed ITCM can hold.
- **ITCM free today:** 2,352 B. It holds one ladder wrapper (1,402 B) but
  not the wrapper and `dl_slowUpdate` (3,010 B).
- **With the drum-side PWM removed:** ≈ 4,930 B free. That holds:
  - `DiodeLadder_renderB2Osc` + `dl_slowUpdate` = 3,010 B;
  - plus `…Osc2` = 4,494 B.
- **The 4.2 KB async does not fit with them.** Moving part of it (the MEG
  and expo steps, already separate functions in `AlkalineControl.c`) is a
  second-stage option once A shows where the misses are.

**Recommendation:**
- **Keep the Alkaline PWM.** It costs nothing on the worst path, its
  sound is the band-limited pulse a wavetable cannot match at every width,
  and removing it buys only flash.
- **Remove the drum-side PWM only if step A shows the ITCM placement is
  worth it** (C). Then the trade is PWM on the drums for the Alkaline's
  speed.

---

## 5. Other levers (not measured here; for completeness)

| Lever | Size | Status |
|---|---|---|
| A1 staggered filter control (the filter chain every other block, ramped over 64 samples) | ≈ −8 to −10 instructions per sample (estimate, reductions draft §4.4) | the largest instruction lever; a sound change (control at 1.45 ms) and a two-block ramp in the loop |
| A2 knob-only expo terms from tables | ≈ −1 to −1.5 (estimate) | 2 KB of flash |
| The tone as a separate pass after the ladder | +10 per sample measured (post-FX `p9T2`) | worse than in the loop |
| Fewer VDIV in the loop | — | not found: the three divides are on one dependency chain (closure → secants), and the A-R kernel's two is unstable at Res full (S093 P3) |
| Recover the post-FX loop's spills by register-allocation tuning | ≈ −2 (the one-row loop is 221 against the spike's 219) | LTO-sensitive; the kind of tuning S2/S3 showed to be fragile |

---

## 6. Step A: the per-slot cycle probe (design; as built in §10)

| | |
|---|---|
| **What** | A `DEV_MODE_LOGGING`-only probe around each slot's `instrumentManager_calcSlotAsync()` and `instrumentManager_calcSlotSyncBlock()` in `mixer_calcNextSampleBlock()`: `DWT_CYCCNT` before and after (the counter is already enabled, `AudioCodecManager.c` l. 309–311); per slot, the block maximum and a running sum over 1,024 blocks. Shown on the existing DEV diagnostic screen or written to the trace. |
| **Why** | The instruction count prices the Alkaline about equal to the Drum, but the device does not agree; the decision between B, C and A1 needs cycles. |
| **RAM** | 6 slots × (max + sum + count) × 4 B = 72 B in a logging build only (the rule: a logging-off build allocates nothing). Needs sign-off. |
| **Cost** | Two counter reads per slot per block in a logging build; the logging-off build is unchanged (the probe compiles out). |
| **Test Scenes** | (1) six Drums at their dearest (`fm` on an `slp` carrier, `slp` modulator); (2) three Alkalines (`wav`, Res and drive high) + three Drums; (3) as (2) with variant T; (4) as (2) with the ladder wrapper in ITCM (a scratch image only, not shipped). |
| **Read-out** | Cycles per block per slot; the Alkaline/Drum ratio; the total against the budget (471,288 cycles per 96-frame half = 157,096 per 32-sample block for all six slots, the mixer and the Effect). |

---

## 7. Decisions for the user

| # | Question | Recommendation |
|---|---|---|
| AS-1 | Run step A (the DEV cycle probe, 72 B in logging builds only) before choosing | **Approved (user, 2026-11-11):** trace only, nothing on screen; built (§10) |
| AS-2 | Remove the tone, keep the overdrive (B) | **Yes:** −6.1 per sample, reaches the Drum's 257.8 on the instruction measure, keeps the main post-FX feature. The `alk_tone` row (30) and its mix-page cell go; the overdrive stays at row 29 |
| AS-3 | Alkaline PWM | **Keep:** it costs nothing on the worst path |
| AS-4 | Drum-side PWM removed to free ITCM for the Alkaline (C) | **Superseded by AS-12:** the ladder loop alone fits the free ITCM |
| AS-5 | A4 (one expo pass, −1.0) | Optional; it was dropped for the MEG strike's accuracy (P9-D3) and is not needed to reach the Drum |
| AS-6 | Cache SRAM: clear S (bit 18) in MPU region 0's RASR (`clocks.c`), §13 | **Done (user, §22):** measured −14.8 % on the same Scene (§17) |
| AS-7 | Re-base the CPU budget gates (S089 Basic 261.3, the Advanced ceiling) in measured cycles per block, with the probe as the acceptance tool | **Yes, after AS-6:** the instruction gates assume a cost per instruction the device does not achieve (§11) |
| AS-8 | Probe RAM 94 B in logging builds (AS-1 approved 72 B; the section hooks added 28 B) | **Sign-off needed;** logging builds only |
| AS-9 | The budget | **Decided (user): §21** (Basic 19,620 × 4, Advanced 21,803 × 2, Effect 16,634, 500 unallocated, under a 97 % cap). §18's even split is the record |
| AS-10 | `bufferTool_satAdd32()` as one `QADD` (bit-exact) | **Done (user, §22)** |
| AS-11 | One float-to-integer conversion per output channel (gain × pan folded) | **Yes, after AS-10:** not bit-exact (≤ 1 LSB at 24 bits; it removes an intermediate truncation) |
| AS-12 | The Alkaline's ladder loop (`DiodeLadder_renderB2Osc*`, 1.4 KB) in ITCM | **Optional (headroom):** it fits the free ITCM (2,400 B after §22); the drum-side PWM stays; not needed to meet §18 if M1 + M2 measure as estimated |
| AS-13 | The resonant SVF back on CrumpBit `grn` and `rvb` (S091 P5 F1 removed it), inside the 16,634 Effect share (§21) | **Yes (user):** estimated grain 16,260 with it (about 374 spare); to schedule |

**If AS-2 is taken, the implementation scope** (for its schedule):
- the tone stage and its two fields leave `dl_renderB2Body()` and
  `DiodeLadder`;
- `alkToneC` leaves the post tables;
- `alk_tone` leaves `AlkalineParameters.c`: the descriptor count goes
  31 → 30, and the mix page becomes `vol pan – drv`;
- `init.alk` and `make_init_alk.py` follow;
- AL-TN goes; AL-OD0 and its two breaks stay; the tone break goes.
- The stored files that hold `alk_tone` lose it (no migration).

---

## 8. Measurement record (2026-11-11)

- **Base:** the working tree after S094 P1 S5 (`build/lxr02.elf`; flash
  688,756 B, ITCM 14,032 B).
- **Variants:** scratch copies, `make all` (`-Ofast -flto`), each with one
  edit:
  - **T:** the tone line removed from `dl_renderB2Body()` and its two
    locals and store;
  - **N:** T plus the OTA argument back to `DL_INV_2VT`;
  - **A4:** `ALK_EXPO_PASSES 1u`;
  - **P:** the pwm branches removed from `AlkalineVoice.c` (typ 2 maps to
    wtb).
- **Tools:**
  - loops: `elf_looppath.py --cond-heads --in-range` on the three ladder
    wrappers;
  - per block: `tools/alkaline/control_cost.py --elf`;
  - sizes: `arm-none-eabi-nm -S`.

| Variant | `…Osc` | `…Osc2` | `…OscPwm` | async | sync block | per block |
|---|---:|---:|---:|---:|---:|---:|
| base | 221 | 234 | 215 | 937 | 416 | 1,367 |
| T | 215 | 227 | 207 | 937 | 411 | 1,362 |
| N | 213 | 228 | 207 | 937 | 407 | 1,358 |
| A4 | 221 | 234 | 215 | 905 | 416 | 1,335 |
| P | 221 | 234 | — | 936 | 416 | 1,366 |
| T + A4 | 215 | 227 | 207 | 905 | 411 | 1,330 |

Whole voice = loop + per block / 32.

---

## 9. Statements this would make out of date (for the user to log, if adopted)

- **`S094_P0_ALKALINE_POST_FX.md` / `S094_P1_VOICE_DSP_ADJUSTMENTS.md`:** the
  tone (P9-D1 / P9-A′) if AS-2 is taken; the accepted Alkaline figure
  (≈ 261.5 → 257.6).
- **`S094_P1_IMPLEMENTATION.md`:** S4 (`alk_tone`, AL-TN, the tone break) if
  AS-2 is taken.
- **`tools/dsp_test/baselines/s094p1/basic_worst.txt`:** the Basic worst
  case (263.7 → 257.8, the Drum, after AS-2).
- **`INSTRUMENTS_DSP_REFERENCE.md`:** §5.5 (Alkaline rows, chain) and §8.2.
- **`CPU_USE_DSP_AUDIT.md`:** the Alkaline item; a new item on the
  instruction-count / device-cycle gap if step A confirms it.
- **`MEMORY.md`:** the Alkaline row count and budget figure; ITCM figures if
  AS-4 is taken.
- **If AS-6 is taken:**
  - `Core/Hardware/clocks.c`'s region 0 comment (the S bit and why);
  - `CPU_USE_DSP_AUDIT.md` (a new item: SRAM was not D-cached from Session
    013 to S094);
  - the memory-map notes that describe region 0 as cached.
- **If AS-7 is taken:** the S089 cost rule and every instruction-based gate
  (`basic_worst.txt`, `advanced_ceiling.txt`, S093's per-sample charges)
  become secondary to measured cycles.
- **From §22 (done):**
  - Every statement that the Cymbal and HiHat are Advanced instruments
    (two per Kit) is out of date: `MEMORY.md`, `INSTRUMENTS_DSP_REFERENCE.md`
    and the Instrument Load / copy-paste specifications.
    `advanced_ceiling.txt` priced the Cymbal as the Advanced dearest.
  - The memory-map and audit notes that describe region 0 as cached
    (`CPU_USE_DSP_AUDIT.md`: a new item, SRAM uncached from Session 013 to
    S094).

---

## 10. Implemented (2026-11-11, user-approved)

### 10.1 Step A: the cycle probe (AS-1)

- **Where:** `Core/DSPAudio/mixer.c`, ahead of `mixer_calcNextSampleBlock()`,
  all under `#if DEV_MODE_LOGGING`; a logging-off build compiles the
  `MIXER_PROBE()` calls to nothing.
- **What it times (`DWT_CYCCNT`), per 32-frame block:**
  - per slot, block-rate work: `instrumentManager_recalcSlotFilter()` +
    `instrumentManager_calcSlotAsync()`;
  - per slot, render: `instrumentManager_calcSlotSyncBlock()`, the SRR
    decimator when the chain has it, and the output/FX-send add;
  - the whole `mixer_calcNextSampleBlock()` (average and maximum).
  The LFO dispatch, routing set-up, Effect and compressor are the whole block
  less the slot sums.
- **Output:** trace stage `'p'` (`AUTOSAVE_TRACE_STAGE_CPU_PROBE`,
  `AutosaveTrace.h`). Every 2,048 blocks (about 1.5 s) eight records:
  - flags 0..5: slot (bits 0..2) and `instrument_type_t` (bits 3..7); value
    = block-rate average << 16 | render average, each saturated at 0xFFFF;
  - flags 6: whole-block average; flags 7: whole-block maximum.
  `tools/diagnostic_tools/decode_devlogs.py` decodes them (`'p'` branch) with
  the per-slot sum, its share of the block and cycles per sample.
- **Budget:** 216 MHz / (`REAL_FS` 44,108.07 / 32) = **156,705 cycles per
  block** (§6's 157,096 assumed 44.0 kHz).
- **Cost:** RAM 66 B (`mixer_probe_*`, measured with `nm`; approved 72 B);
  flash +624 B against the S5 image (probe functions 412 B, the rest call
  sites); the five probe functions are `noinline` so LTO keeps them
  out of the render loops. Not on screen.
- **Not done:** the loop gates (`elf_p4_check`, source pairing) were not run
  on this image: it is a measurement image, and the probe calls sit between
  slot renders, not inside them.

### 10.2 Post-filter overdrive doubled at maximum (user)

- **Change:** `tools/alkaline/gen_alk_post_tables.py` `OVD_OCT` 4.0 → 5.0
  (OTA input gain x1..x16 → **x1..x32**; 127 now pushes twice as hard) and
  `OVD_MAKEUP_OCT` −2.0 → −2.5, keeping the make-up at 1/√gain (P9-D4's
  starting curve). `AlkalinePostTables.c/.h` regenerated (`--check` OK);
  `[0]` stays 1.0f, so overdrive 0 is unchanged (AL-OD0 golden: 0 LSB).
- **Level at 127 (AL-OV, host):** crest 3.39, RMS 2,926 LSB (overdrive 0:
  1,215; 64: 2,574). The make-up at 127 is −15.1 dB (was −12.0): a fully
  saturated signal is up to ~3 dB quieter at 127 than in S4, a quiet one
  ~3 dB louder. If it should keep S4's level at 127, `OVD_MAKEUP_OCT` −2.0 is
  the one-line alternative.
- **Cost:** none: table values only.
- **Comments updated:** `AlkalineVoice.h`, `AlkalineParameters.c`, the
  generator and the generated header (x1..x32).

### 10.3 Build and tests

- `make all && make img`: `build/LXRV2_lxr02.img` 689,380 B; flash headroom
  64,284 B; ITCM 14,032 B (unchanged).
- Host: `alkaline` (all laws 0 failures, breaks caught), `alk_post_tables`,
  `special_tags`, `make_init_alk --check` pass.

---

## 11. Step A result, run 1 (device, 2026-11-11)

- **Record:** `tools/dsp_test/baselines/s094p2/device_probe_run1.txt` (all
  112 windows; the user's `SD_CARD_CPU_TRACE/asavetrc.bin`).
- **Scene:** one Scene throughout (~2.8 min): `drm alk drm snr cym hat`.
  The figures are very stable (window to window within ~1 %).
- **Context:** the mixer runs in the foreground with the low-priority
  interrupts masked (`main.c` `audio_check_and_render()`), so these are the
  mixer's own cycles plus at most the audio DMA interrupt.

| Slot | Type | Block-rate | Render | Per block | Share of budget | Cycles/sample | Instruction model |
|---|---|---:|---:|---:|---:|---:|---:|
| 0 | drm | 1,170 | 18,552 | 19,721 | 12.6 % | 616 | ≤ 257.8 |
| 1 | **alk** | **2,443** | **19,634** | **22,077** | **14.1 %** | **690** | 256.6..276.7 |
| 2 | drm | 791 | 17,612 | 18,402 | 11.7 % | 575 | ≤ 257.8 |
| 3 | snr | 693 | 16,351 | 17,045 | 10.9 % | 533 | |
| 4 | cym | 766 | 18,386 | 19,152 | 12.2 % | 598 | |
| 5 | hat | 1,133 | 19,050 | 20,183 | 12.9 % | 631 | |
| | other | | | 32,385 | 20.7 % | | |
| | **whole block** | | | **148,965** | **95.1 %** | | max 169,070..175,697 (108-112 %) |

**Readings:**
1. **The device is about 2.3-2.6 times the instruction model for every
   engine, not only the Alkaline.** The Drums run 575-616 cycles per sample
   against at most 257.8 instructions. §0's premise (the Alkaline far
   dearer on the device than its count) does not hold for this Scene.
2. **The Alkaline is 1.12 x the dearer Drum here (1.20 x the other):**
   +2,356 / +3,675 cycles per block. Half the gap is block-rate work
   (2,443 against 791..1,170: about 1,300-1,650 more, close to §3.1's
   flash-fetch estimate of ~1,300 for the 4.2 KB async and 1.6 KB
   `dl_slowUpdate`); the other half is render (+1,100..2,000, which also
   carries the Drums' SRR).
3. **Any six-slot Scene is at the limit.** This Scene already averages
   95 % with one Alkaline, and every 1.5 s window holds at least one block
   over budget (the two-slot queue absorbs it; the foreground, menu
   included, gets what is left: the interface lag). Two more Alkalines in
   place of the Drums add about 6,000 per block: about 99 %, which is the
   reported pinning. Six Drums would sit near 94-96 % on the same basis
   (estimate).
4. **`other` is a fifth of the budget** (32,385 per block): LFO dispatch,
   routing and buffer clears, the Effect and the compressor. It is the
   largest single item in the block and has not been split.

**What this means for the options (estimates from run 1):**
- **B (tone removal, AS-2):** 6 instructions × ~2.5 ≈ 15 cycles per sample,
  ≈ 500 per block per Alkaline: a fifth of the gap to the dearer Drum.
  Not enough on its own for "at the Drum".
- **C (ITCM):** the block-rate excess (~1,300-1,650) is the part ITCM or a
  smaller per-block path could address; the 4.2 KB async does not fit the
  ITCM that removing the drum PWM frees (~2.6 KB + 2.35 KB spare), the
  1.4 KB ladder loop does. Needs the scratch-image measurement (§3.3 Q2).
- **Reaching "Alkaline = Drum" fixes the Alkaline's 1.1-1.2 x, not the
  95 %.** Headroom for the interface needs savings across the block: the
  `other` 20.7 % and the general 2.4 x device/model gap.

**Not answered yet (§3.3):** three Alkalines in one Scene (cache sharing);
variant T and the ITCM ladder on the device; how `other` divides (Effect,
compressor, LFO). Which Effect, if any, was active and the Alkaline's `typ`
in this Scene are not in the trace.

---

## 12. Settings evaluation (run 1 Scene)

**Source:** the AutoSave record on the returned card. `.hcprms2` is the
committed record (generation 1522). `.hcprms1` (generation 1523) was
uncommitted and 32,768 B long: a write in flight when the card was pulled.
The settings are also in the run-1 record
(`tools/dsp_test/baselines/s094p2/device_probe_run1.txt`).

**Scene 0 "FluxTime", Kit "Flux":**
- Effect CrumpBit (`cbt`) active. Sends: slot 0 = 44, slot 3 = 12,
  slot 4 = 127; the others 0.
- Bus compressor on (`cmp` 1).

| Slot | Instrument | Settings that set the path | Path against the type's dearest | Measured per block |
|---|---|---|---|---:|
| 0 | drm `chipd3` | osc1 **pwm** (pos 86); FM wav Rec at **amt 0** (mode fm); LP; **WFd** | below: pwm FM carrier (`osc_pwmDFmBlock` 32 against slp FM 51), wav modulator (slp 27) | 19,721 |
| 1 | alk `fluxd2` | typ **pwm** (pos 77); res 61; drive 64; overdrive 1; tone 0 | **the Alkaline's cheapest path** (256.6 against wtb with a slot 276.9) | 22,077 |
| 2 | drm `brezeld3` | osc1 wav Sin; FM wav Sin amt 41; Pek; **T+D**; run itp | below: wav carrier and modulator; the chain is the dearer T+D | 18,402 |
| 3 | snr `chips1` | osc1 wav user sample; noise 95; LP res 127 drv 127; WFd; LFO rnd at 127 | below the Snare's dearest | 17,045 |
| 4 | cym `fluxc1` | three wav Sin; UBP; WFd; overdrive 68; **send 127** | below: no slp modulators | 19,152 |
| 5 | hat `fluxh1` | osc1 smp; osc2 user sample; osc3 Sin; HP; WFd | below | 20,183 |

**Readings:**
1. **Most slots are below their dearest path.** At the same cycles per
   instruction (an estimate):
   - the Alkaline on wtb with a slot held would be about 23,800;
   - a Drum at its dearest about 22,000-23,000.

   A Scene of six dearest slots plus run 1's 32,385 of other work projects
   to about 165,000-172,000 cycles: **105-110 % of the 156,705 budget**.
   Such a Scene cannot run today, whichever instruments it uses.
2. **Work that runs whatever the settings** (the S089 constant-cost rule):
   - the FM path at amount 0 (slot 0);
   - SRR on all five non-Alkaline slots at `srt` 127 (about 11
     instructions per sample each: about 900 cycles per block each at
     run 1's ratio, an estimate);
   - the tone stage at any tone setting;
   - the compressor whenever `cmp` is on.

   Skipping these by setting would help a Scene like this one, but not the
   worst case. The rule exists so that the worst case is the one that
   gets measured, so skips are not recommended (§15, item 7).
3. **What the settings cannot explain:** a 2.3-2.7 x gap between
   instructions and cycles on every engine, including the simplest
   (Snare: 533 cycles per sample). That points at something every voice
   shares (§13).

---

## 13. SRAM is not D-cached (new finding)

**The configuration** (`Core/Hardware/clocks.c`, `sysclk_init()`):
- the I-cache is on;
- the D-cache is on;
- MPU region 0 covers 0x20000000-0x200FFFFF, which includes all of SRAM1
  and SRAM2, as Normal write-through (TEX 0, C 1, B 0), **S = 1
  (shareable)**;
- MPU region 1 makes the 4 KB `.dma_nocache` section non-cacheable.

**Why that turns the cache off:** on the Cortex-M7, CACR.SIWT (0xE000EF9C
bit 2) is 0 at reset. In that mode, Normal cacheable *shareable* memory is
treated as non-cacheable, and its programmed cache attributes are
ignored (ARM Cortex-M7 TRM, CACR). Nothing in the tree sets SIWT. So the
D-cache only ever serves flash constants, which use the default memory
map (non-shareable).

**The intent was otherwise.** Session 013 (log archive) enabled the D-cache
as a "high-impact fix", with region 0 described as "Write-Through,
No-Write-Allocate" so that SRAM would be cached.

**What runs uncached today** (measured with `nm` and `objdump`):

| Data | Where | Size | Touched |
|---|---|---:|---|
| The six voice runtimes (`runtime_slots`) | SRAM1 | 7,584 B | per sample (state loads and stores) and per block |
| The stack: spills, the 64 B `sampleData` block, locals | SRAM2 (top 0x20080000) | — | per sample |
| `scenes`, `slider_lut` (4 KB), parameter tables, `crumpBit_dataLines` | SRAM1 | — | per block or per sample |

In the loops themselves:
- `DiodeLadder_renderB2OscPwm`: 78 loads (20 from the stack) and 40
  stores (18 to the stack) in 347 instructions; the loop is the single
  215-instruction path.
- `SVF_calcBlockZDF`: 68 loads and 22 stores, mostly state and buffer,
  with almost no spills.

Each uncached load waits on the AXI bus.

**DMA audit (needed before caching):**
- The only DMA streams are the ADC (`adc_dma_buf`) and audio TX
  (`dma_buffer`, `dma_buffer2`). All three buffers are in `.dma_nocache`
  (MPU region 1, non-cacheable).
- USB is OTG FS with `dma_enable = 0`.
- The SD card path uses no DMA.
- Write-through keeps memory current for anything that reads it.

So clearing S makes reads cacheable without a coherency risk today. The
existing rule still holds: any future DMA receive buffer goes in
`.dma_nocache`.

**The fix (AS-6):**
- `MPU_RASR = (3UL << 24) | (1UL << 17) | (19UL << 1) | 1UL;` for region 0,
  i.e. drop `(1UL << 18)`.
- The alternative is setting CACR.SIWT. It has the same effect, but as a
  global bit; the region bit is explicit where the memory is described.
- Cost: 0 B RAM, 0 B flash.
- Possible follow-up after measuring: a write-back, write-allocate region
  for SRAM2 (the stack only; no DMA) to speed the spill stores too.

**Size: unknown until run 3.** If each stack load alone costs about 6
cycles more uncached than as a cache hit, the Alkaline loop's 20 stack
loads are about 120 cycles per sample: 17 % of its 690. State loads and
the other engines' buffer traffic come on top. This is an estimate to
rank the work, not a result.

---

## 14. Probe, second version, and the A/B image (as built)

**Sections added** (user: hooks around the mixer and the Effect). The
whole block is now attributed:
- per slot: block-rate (filter recalc + async) and render (now
  `calcSlotSyncBlock()` only);
- seven mixer sections (`MIXER_PROBE_SEC_*`, `mixer.c`):

| n | Section | Spans |
|---|---|---|
| 1 | control | itp count, `modNode_resetTargets()`, velocity, LFO dispatch |
| 2 | `effects_service()` | Scene Effect resolution |
| 3 | set-up | routing snapshot, buffer clears, slot-loop overhead |
| 4 | SRR | `mixer_decimateBlock()`, all slots |
| 5 | adds | fader gains, dry and FX-send adds, all slots |
| 6 | Effect | bus conversion, `effects_process()`, return (or the `off` reset) |
| 7 | compressor | `busComp_processBlock()` |

- **Records:** sixteen per 2,048-block window (§10.1's eight, plus):
  - flags 6 | n << 3, n = 1..7: section n's average cycles per block;
  - flags 6 | 8 << 3: MPU region 0's RASR, as a witness of which image ran
    (bit 18 set = SRAM not cached).
  - The decoder (`decode_devlogs.py`, `'p'`) names each one.
- **Run 1 against run 2:** run 1's per-slot render included that slot's SRR
  and output/send add; run 2 reports those as sections 4 and 5.
- **Cost:**
  - RAM 94 B (`mixer_probe_*`, `nm`): AS-8.
  - Flash 689,236 B against §10's 689,380 B: LTO layout moved by more
    than the probe grew.
  - Logging builds only; the logging-off build is unchanged.
- **Images** (`make all && make img`, both 689,252 B):

| Image | SRAM | Use |
|---|---|---|
| `build/LXRV2_lxr02.img` | S = 1, as today (the tree) | runs 2 and 4 |
| `build/s094p2_sram_cached/LXRV2_lxr02.img` | **S = 0** | runs 3 and 5 |

  The A/B image was built from a scratch copy whose only edit is
  region 0's RASR. The two binaries differ in one byte; the tree's
  `clocks.c` is unchanged.
- **Host checks:** the decoder reads run 1 unchanged and decodes
  synthetic section and RASR records.

---

## 15. Savings, ranked (revised)

| # | Lever | Affects | Size | Cost / loss | Status |
|---|---|---|---|---|---|
| 1 | **SRAM D-cache (S = 0)**, §13 | every slot, the mixer, the Effect | unknown; likely the largest (run 3 measures it) | none | A/B image built; AS-6 |
| 2 | Write-back stack region (SRAM2) | every spill store | after 1 | none (no DMA in SRAM2) | not built |
| 3 | The largest item of the 20.7 % | depends on run 2 | up to a fifth of the block | depends | run 2 |
| 4 | Alkaline: remove the tone (AS-2) | Alkaline | about 500 per block (6 instructions × ~2.5 × 32, estimate) | the tone control | open |
| 5 | Alkaline: A1 staggered filter control | Alkaline | −8 to −10 instructions: about 650-800 per block (estimate) | control at 1.45 ms; a two-block ramp | open |
| 6 | Alkaline: ladder loop in ITCM (AS-4) | Alkaline block-rate and render | decide on the cached figures | ITCM 1.4 KB; the drum PWM if more is needed | open |
| 7 | Skips by setting (SRR at `srt` 127, FM at amount 0, tone at 127) | typical Scenes only | SRR ≈ 900 per slot per block (estimate) | breaks the S089 constant-cost rule; the worst case is unchanged | not recommended |

**The target "the Alkaline at or near the Drum"** is 2,400-3,700 cycles per
block (11-17 %) away on run 1's figures. Item 1 may change both sides, so
the Alkaline-specific choice waits for run 3.

---

## 16. Next card run (test protocol; done: run 2, §17)

**One flash (user):** `build/s094p2_sram_cached/LXRV2_lxr02.img` (S = 0),
copied to the card root as `LXRV2_lxr02.img`. Then:
1. FluxTime, at least one minute of playback;
2. the worst-case Scene, at least one minute;
3. a Save and a Load during the run.

**What it measures:**
- **The cache effect: cached FluxTime against run 1 (uncached).**
  - The whole-block averages compare directly.
  - Run 1's per-slot render included that slot's SRR and output/send add,
    so the comparison is run 1's slot sums against the new slot sums plus
    sections 4 and 5.
- **The worst-case ceiling with the fix**, and how the other work divides
  (sections 1-7), in both Scenes.
- **Not measured:** the worst-case Scene uncached. It is estimated from
  FluxTime's cached-to-uncached ratio.
- The RASR witness record confirms that the S = 0 image ran.

---

## 17. Run 2: SRAM cached (device, 2026-11-11)

- **Record:** `tools/dsp_test/baselines/s094p2/device_probe_run2.txt`
  (segment averages, settings, all 258 windows).
- **Source:** the user's `SD_CARD_CACHE_FIX/asavetrc.bin`, from the S = 0
  image. Every window's RASR witness reads S = 0.
- **The Scenes:** the user edited Scene 0 through six setups. The final
  setup is in `.hcprms1` (generation 1547, committed):
  - slots 0-3: Alkaline on `wtb`, an LFO on position (the blend path);
  - slots 4-5: HiHat on `slp`, noise FM, T+D;
  - CrumpBit, switched to grain near the end;
  - sends on slots 0, 3 and 4; the compressor on.

### 17.1 The cache effect (FluxTime, against run 1)

| | Run 1, uncached | Run 2, cached | Change |
|---|---:|---:|---:|
| Whole block, average | 148,965 (95.1 %) | 126,979 (81.0 %) | **−21,986 (−14.8 %)** |
| Whole block, window maximum | 169,070-175,697 | 143,550 (91.6 %) | |
| Six slots, with their SRR and adds | 116,580 | 100,396 | −13.9 % |
| The rest (control, Effect service, set-up, Effect, compressor) | 32,385 | 26,523 | −18.1 % |

Per slot (run 2's adds shared out as measured: a dry slot 1,936, a send slot
2,214; SRR 475 a slot):

| Slot | Type | Run 1 | Run 2 | Change |
|---|---|---:|---:|---:|
| 0 | drm (send) | 19,721 | 17,448 | −11.5 % |
| 1 | **alk** | 22,077 | 17,614 | **−20.2 %** |
| 2 | drm | 18,402 | 16,252 | −11.7 % |
| 3 | snr (send) | 17,045 | 14,484 | −15.0 % |
| 4 | cym (send) | 19,152 | 17,233 | −10.0 % |
| 5 | hat | 20,183 | 17,366 | −14.0 % |

- The Alkaline gained most, as expected from its stack spills (§13).
- **The drum types gained 10-15 %, much more than §13's estimate of 2-5 %**:
  more of their state traffic was in uncached SRAM than the loop dumps
  suggested.
- The total gain (14.8 %) is above the 5-12 % estimated before the run.

### 17.2 All setups (cached)

| | Setup | Effect / compressor | Average | Peak |
|---|---|---|---:|---:|
| A | FluxTime: drm alk drm snr cym hat | cbt / on | 126,979 (81.0 %) | 91.6 % |
| B | drm drm drm snr cym hat | off / off | 94,834 (60.5 %) | 68.9 % |
| C | as B, five sends | cbt / on | 120,918 (77.2 %) | 86.5 % |
| D | six Alkalines (`wtb`) | cbt / on | 126,853 (80.9 %) | 92.7 % |
| E | four Alkalines + two HiHats | cbt / on | 130,359 (83.2 %) | 94.9 % |
| F | as E, grain | cbt grain / on | **131,561 (84.0 %)** | 94.7 % |

### 17.3 The shared mixer and the Effect (cached)

| Section | Range observed | Notes |
|---|---:|---|
| control (LFO dispatch, modNode) | 2,826-5,921 | highest with the drum kit's LFOs (C) |
| `effects_service()` | 1,070-4,057 | 1,070 with the Effect off |
| set-up | 1,752-2,494 | |
| SRR, per slot | 475-822 | 822 in B |
| adds, dry slot | 1,936 | B: six dry slots, 11,618 |
| adds, slot with a send | 2,214-3,037 | 3,037 in D-F; probably the Effect's stereo input (an inference) |
| Effect (CrumpBit) | 10,319-11,194 | grain the dearest |
| compressor | 4,284-4,369 | 78-92 when off |

### 17.4 The first instance of a type pays more

In D (six Alkalines):
- slot 0 costs 16,261 and slot 1 15,057;
- slots 2-5 cost 13,974-14,231 each.

**The first instance pays about 2,200 more:** about 940 in block-rate work
and 1,300 in render. That is the type's code fetched cold into the I-cache
once per block. Later instances find it warm.

The same effect is smaller on the other types: about 800 for the HiHat (E)
and about 700 for the Drum (B). A Scene pays this once per type, so a
Scene of mixed types is the worst case.

---

## 18. One budget: a 97 % cap, split evenly by seven (AS-9)

**Revision note:**
- The first version read the user's "extra 2 %" as 2 % above the cached
  image's 16 % headroom (an 82 % cap).
- **The user meant 2 % below the pre-fix level** (about 99 %, pinned with
  three Alkalines): **an average cap of about 97 %**.
- The first version also charged every slot as a cold first instance
  with a stereo send, and took each shared item at its worst from
  different setups. Under a 97 % cap that conservative charge still fits,
  so it is kept: it makes the rule per type and needs no Scene-level
  check.

**The worst Scene, as measured** (F: four Alkalines, two HiHats, CrumpBit
grain, the compressor on), with sends added to the three dry slots (+1,101
each, §17.3):

| Item | Cycles | % |
|---|---:|---:|
| Shared mixer (control 4,060, set-up 2,446, compressor 4,369) | 10,875 | 6.9 |
| Effect (`effects_service()` 3,953 + grain 11,194) | 15,147 | 9.7 |
| Six voices with their sends (average 18,130) | 108,778 | 69.4 |
| **Total** | **134,800** | **86.0** |

**The budget (average cap 97 % = 152,004 cycles per block):**

| Item | Cycles | % |
|---|---:|---:|
| Foreground and background (outside the cap) | 4,701 | 3.0 |
| Shared mixer, worst observed (control 5,921 in C; set-up 2,494; compressor 4,369) | 12,784 | 8.2 |
| **Each of seven shares (six voices + the Effect)** | **19,888** | **12.7** |

The shared mixer is charged at its worst observed, not at F's 10,875, so the
cap holds in any Scene. Correction: an earlier draft used 10,875 and
20,161 per share.

**The rule (replaces S089's Basic 261.3 and the Advanced ceiling):**
- one share for every instrument type and for the Effect;
- a type is measured as the first instance in a Scene (cold), at its
  dearest settings, with a stereo send (block-rate + render + SRR + its
  output/send add);
- the Effect is measured as `effects_service()` plus the Effect at its
  dearest subtype;
- measured with the probe;
- instruction counts stay as the host-side regression tool, not the gate.

Because every slot is charged its worst, no Scene can pass the cap.

**Types against a share (19,888), worst measured:**

| Type | Worst measured | Spare |
|---|---:|---:|
| Alkaline (F, slot 0: cold, send) | 16,572 + 3,037 = 19,609 | 279 |
| HiHat (F, slot 4: cold, send) | 15,089 + 474 + 3,037 = 18,600 | 1,288 |
| Drum (A, slot 0; SRR 822, stereo send) | 14,759 + 822 + 3,037 = 18,618 | 1,270 |
| Cymbal (A, slot 4; as Drum) | 14,544 + 822 + 3,037 = 18,403 | 1,485 |
| Snare (A, slot 3; as Drum) | 11,795 + 822 + 3,037 = 15,654 | 4,234 |
| Effect (CrumpBit grain, F) | 3,953 + 11,194 = 15,147 | 4,741 |

**Readings:**
- **Everything fits today.** The Alkaline is tightest (279 spare).
- **Not yet measured at their dearest:** Drum, Snare, Cymbal, and the
  HiHat with wavefolders. The user expects the last to be slightly
  dearer; it has 1,288 spare.
- **The trade at the cap:** the pre-fix FluxTime ran at 95 % with peaks of
  108-112 % (the render queue absorbed them) and a lagging menu. A Scene
  near 97 % brings that back for that Scene. Today's Scenes sit at
  60-86 %.
- **Each 1 % of the cap** = 1,567 cycles = 224 per share.

---

## 19. Reaching the budget (proposed, estimates on the cached figures)

**Shared mixer (every voice share gains):**

| # | Change | Why | Estimate | Exactness |
|---|---|---|---|---|
| M1 (AS-10) | `bufferTool_satAdd32()` as one `QADD` | Today it is a 64-bit add and two compare-and-branch pairs: about 8-9 instructions per channel per sample. The stereo add loops are 41-42 instructions per sample (`objdump`, `mixer_calcNextSampleBlock` and `mixer_addVoiceInt16ToOutputAndFx`). | adds: dry slot 1,936 → about 1,450; a send slot 3,037 → about 2,100 (−2,500 to −4,000 a block in all) | **bit-exact** (`QADD` is the same signed saturation) |
| M2 (AS-11) | one conversion per channel (gain × pan × 256 folded, no integer round trip) | two `vcvt`, two `vmov` and a multiply per stereo sample | −200 to −400 a slot | ≤ 1 LSB at 24 bits; removes a truncation |
| M3 | control: the LFO dispatch and modNode per block | 2,826-5,921, varying with the LFOs | −1,000 to −3,000 | to investigate |
| M4 | `effects_service()` only on change | 1,070-4,057 per block | −2,000 to −3,000 with the Effect on | to investigate |

**Alkaline:**

| # | Change | Estimate |
|---|---|---|
| AS-12 | The ladder loop in ITCM: its cold render penalty is about 1,300 (§17.4); 1.4 KB fits the 2,352 B free | −1,000 to −1,500 cold |
| AS-2 | Remove the tone (6 instructions per sample) | −300 to −400 |
| A1 | Staggered filter control (−8 to −10 instructions per sample; half the per-block control) | −400 to −700 |

**Drum types:** they share most of their code (SVF, oscillators,
distortion), so a cold-code saving there helps four types at once.
- `SVF_calcBlockZDF` (3.3 KB) does not fit the ITCM that is left once the
  Alkaline's loop is in.
- The per-type breakdown inside the render is not measured. It needs a
  probe inside the engine, not more mixer sections.

**Projection** (estimates, not measured):
- **Alkaline:** M1 + M2 (about −1,000 to −1,400 on its share), then
  AS-12 + AS-2 + A1 (−1,700 to −2,600): −2,700 to −4,000 against the
  3,078 needed. That reaches its share at the middle of the range.
- **HiHat, Drum, Cymbal:** M1 + M2 cover about half of their 1,900-2,400.
  M3/M4 add 300-850 to every share. The rest needs drum-path work or
  the 16 % reserve.

**Against the budget (§18):** no saving is required. The items above are
headroom:
- M1 + M2 lower every Scene: about 6,800-8,000 with six sends, about 4-5 %
  (estimate). That keeps the menu and Load/Save fast.
- AS-12, AS-2 and A1 widen the Alkaline's 279 spare.

**Recommended next build:** AS-6 in the tree, M1 and M2, then one run:
- Scene F with sends on all six slots;
- the HiHat with wavefolders;
- each drum type at its dearest settings (to confirm their shares).

---

## 20. Before and after: everything measured, against the even budget

**Sources:**
- "Before": run 1, FluxTime on the uncached image (§11).
- "After": run 2, the same FluxTime Scene, cached (§17, setup A).
- "Budget": §18.

All figures are cycles per 32-sample block (156,705 = 100 %) unless
marked.

### 20.1 The whole CPU

| Item | Before (uncached) | After (cached) | Budget (97 % cap) |
|---|---:|---:|---:|
| **Six voices**, with their SRR and output/send adds | 116,580 (74.4 %) | 100,396 (64.1 %) | 6 × 19,888 = 119,328 (76.1 %) |
| · instruments (block-rate + render) | not split | 85,572 (54.6 %) | inside each share |
| · SRR (five drum-type slots) | not split | 2,374 (1.5 %) | inside each share (≤ 822 a slot) |
| · output and FX-send adds | not split | 12,450 (7.9 %) | inside each share (≤ 3,037 a slot) |
| **Effect**: `effects_service()` + CrumpBit | ≈ 17,700 (11.3 %)¹ | 14,482 (9.2 %): 4,057 + 10,425 | 19,888 (12.7 %); grain measures 15,147 |
| **Shared mixer** | ≈ 14,700 (9.4 %)¹ | 12,041 (7.7 %) | 12,784 (8.2 %), worst observed |
| · control: itp count, modNode, velocity, LFO dispatch | not split | 5,244 (3.3 %) | 5,921 |
| · set-up: routing, buffer clears | not split | 2,487 (1.6 %) | 2,494 |
| · bus compressor | not split | 4,310 (2.8 %) | 4,369 |
| Effect + shared, as measured together | 32,385 (20.7 %) | 26,523 (16.9 %) | 32,672 (20.8 %) |
| Probe overhead (not attributed) | — | 60 (0.04 %) | — |
| **Mixer block, average** | **148,965 (95.1 %)** | **126,979 (81.0 %)** | **≤ 152,004 (97.0 %)** |
| Mixer block, peak in a 1.5 s window | 169,070-175,697 (108-112 %) | 143,550 (91.6 %) | not capped: the render queue absorbs peaks (run 1 ran with peaks up to 112 %) |
| **Outside the mixer** | **7,740 (4.9 %)** | **29,726 (19.0 %)** | **≥ 4,701 (3.0 %)** |
| · AutoSave / Pattern background work (budgeted, H records) | 0 measured (no edits in the window); cap 2.5 % playing, 5 % stopped | 0.17 % average, 0.66 % peak (5 s interval); scalar AutoSave only | cap unchanged: 2.5 % playing, 5 % stopped |
| · Load/Save, Bank-load idle work | idle time only (200 / 300 µs a pass when the render queue is full) | as before, more idle | as before |
| · menu, UI, MIDI, sequencer, interrupts | the rest (not measured) | the rest (not measured) | the rest |

¹ Run 1 did not split its 32,385 "other". These two lines divide it in run
2's proportions (Effect 54.6 %, shared 45.4 %). That is an estimate.

### 20.2 Per voice

| Slot | Type (FluxTime) | Before | After | Change | Worst measured for the type (cold, stereo send) | Share | Spare |
|---|---|---:|---:|---:|---:|---:|---:|
| 0 | drm | 19,721 | 17,448 | −11.5 % | 18,618 | 19,888 | 1,270 |
| 1 | alk | 22,077 | 17,614 | −20.2 % | 19,609 | 19,888 | 279 |
| 2 | drm | 18,402 | 16,252 | −11.7 % | (as slot 0) | 19,888 | |
| 3 | snr | 17,045 | 14,484 | −15.0 % | 15,654 | 19,888 | 4,234 |
| 4 | cym | 19,152 | 17,233 | −10.0 % | 18,403 | 19,888 | 1,485 |
| 5 | hat | 20,183 | 17,366 | −14.0 % | 18,600 | 19,888 | 1,288 |
| — | Effect | ≈ 17,700¹ | 14,482 (cmb) | | 15,147 (grain) | 19,888 | 4,741 |

- "After" shares run 2's adds out as measured: a dry slot 1,936, a send
  slot 2,214; SRR 475 a slot.
- The old gates were instruction counts: Basic 261.3 and Advanced 285.9
  per sample. They have no fixed cycle equivalent: the device ran at about
  2.3-2.7 cycles per instruction before the fix and about 1.6-1.9 after.

### 20.3 Load/Save timing (indicative)

These are operation medians from the `w` owner records:
- "before": every earlier session on the run-1 card, all on uncached
  images;
- "after": run 2.

The Scenes and the CPU load differ, so these are indicative only.

| Operation | Before: n, median | After: n, median |
|---|---|---|
| `LOAD_INDEX` | 59, 41 ms | 46, **10 ms** |
| `LOAD_LIBRARY_INDEX` | 213, 8 ms | 19, 0 ms |
| `SCAN_BANK_SCENES` | 53, 403 ms | 2, 192 ms |
| `UPDATE_HCNAMES_KIT` | 20, 699 ms | 14, **121 ms** |

### 20.4 Readings

1. **The cache fix moved 14.1 points out of the mixer.** The space outside
   the mixer went from 4.9 % to 19.0 %. That is where the faster menu and
   Load/Save come from.
2. **The budget gives the voices back slightly more than they had before
   the fix** (76.1 % against 74.4 %). The Effect and the shared mixer get
   20.8 % against 20.7 %.
3. **At the cap, only 3.0 % is left outside the mixer.** That is less than
   the 4.9 % the pre-fix FluxTime left, when the menu already lagged. The
   AutoSave/Pattern budget alone may take 2.5 % while playing.

   A Scene that reaches the cap would behave like the pre-fix worst case,
   or slightly worse in the UI. Today's Scenes reach 86 % at most, which
   leaves 14 %.
4. **What is not known without further testing:**
   - the UI, sequencer and MIDI share of the outside time;
   - the audio DMA interrupt's share (it can pre-empt the mixer, so it sits
     inside the slot figures);
   - each drum type at its dearest settings;
   - the HiHat with wavefolders.

---

## 21. Budget adopted: Basic and Advanced by slot count (user, AS-9)

**The cap:** 97 % average, as §18 (152,004 of 156,705 cycles per block).
The shares are set a little above what is measured or estimated, so that
a build-to-build drift does not cross them (user).

**Slack** below means a share's cycles that its worst measured (or
estimated) cost does not use. Budget figures are cycles per block.

| Item | Budget | % | Worst measured or estimated | Slack | Basis |
|---|---:|---:|---:|---:|---|
| Outside the mixer (reserve) | 4,701 | 3.0 | not measured | — (a reserve, not slack) | menu, MIDI, sequencer, filesystem; AutoSave/Pattern capped at 2.5 % playing |
| Shared mixer | 12,784 | 8.2 | 12,784 (worst of each section, from different setups) | 0 against that worst; **1,909 in F** (10,875) | control 5,921, set-up 2,494, compressor 4,369 (§17.3) |
| **Effect** | **16,634** | **10.6** | ≈ 16,260 (grain with the SVF restored, estimate) | **≈ 374** (estimate); 1,487 today without the SVF (15,147 measured) | AS-13 |
| **Basic, each of four slots** | **19,620** | **12.5** | 19,609 (Alkaline, cold, `wtb` blend, stereo send) | **11** (Alkaline); see below for the other types | user: 11 over the Alkaline's worst |
| **Advanced, each of two slots** | **21,803** | **13.9** | ≈ 20,700-21,200 (Alkaline with wavefolders, estimate) | **≈ 600-1,100** (estimate) | no current type is Advanced (§22) |
| **Unallocated under the cap** | **500** | **0.3** | — | **500** | 152,004 − 151,504 allocated |
| **Total** | **156,705** | **100** | | | |

**Slack in each Basic share (19,620),** from the worst measured before
`QADD`:

| Type | Worst measured | Slack |
|---|---:|---:|
| Alkaline | 19,609 | **11** |
| HiHat | 18,600 | **1,020** |
| Drum | 18,618 | **1,002** |
| Cymbal | 18,403 | **1,217** |
| Snare | 15,654 | **3,966** |

**Slack that is expected but not yet measured:**
- **`QADD` (§22):** about 490 more per dry slot and about 900 per slot
  with a send, in every Basic and Advanced share (estimate; the loops are
  15 and 23 instructions shorter per sample).
- **An Advanced slot holding a Basic type:** 21,803 − 19,620 = **2,183**.
  Today every slot holds a Basic type, because no type is Advanced.

**Slack in a whole Scene (charged by the rule):**

| Scene | Total | Under the 97 % cap |
|---|---:|---:|
| Today's types: six Basic slots at their worst (19,609), the Effect at its estimate (16,260), the shared mixer at its worst | 146,698 (93.6 %) | **5,306 slack (3.4 %)** |
| With two Advanced Alkalines at their estimate's top (21,200) and four Basic slots at 19,609 | 149,880 (95.6 %) | **≈ 2,124 slack (1.4 %)**, estimate |
| Every share full (the budget itself) | 151,504 (96.7 %) | **500 slack (0.3 %)** |

**The rule:**
- A Scene holds **at most two Advanced instruments**. Basic types may fill
  any slot.
- The limit already exists: `instrumentManager_typeSelectableForSceneSlot()`
  refuses a third Advanced type. It is applied to type selection and
  Instrument Load (`menu.c`) and to copy/paste (`copyOps.c`,
  `AUTOSAVE_TRACE_CC_DROP_ADVANCED_LIMIT`).
- A type is measured as before (§18): the first instance in a Scene
  (cold), its dearest settings, a stereo send, with the probe. The Effect
  is measured as `effects_service()` plus its dearest subtype.

**The Effect (16,634), by estimate:**
- **The added cost:** S091 P5 F1 replaced the resonant SVF (31 instructions
  per frame) with a one-pole (16) on `grn` and `rvb`. Restoring it adds 15.
- **On the device:** grain measured 11,194 cycles of processing for 166.5
  instructions per frame, about 2.1 cycles per instruction. The extra 15
  cost about 1,000 cycles per block.
- **Grain with the SVF:** 11,194 + about 1,000 + 4,057 (`effects_service()`,
  worst observed) = **about 16,260; 374 slack**.
- **The other subtypes:**
  - `rvb` is 163.9 instructions per frame against grain's 166.5;
  - `dly`, `cmb` and `pit` are lower still (`cmb` measured 10,425 for 153.8,
    the same ratio).

**The Advanced slot (21,803): the first candidate** is an Alkaline with
wavefolders and tone instead of the current drive and tone.
- **The cost:** the drum-side wavefolder chain is about 22 instructions
  per sample. The Alkaline's present output drive costs about 2 (inside the
  ladder's output stage).
- **The swap:** about +1,100 to +1,600 per block, depending on spills in
  the ladder loop (S4's three extra values cost 8 instructions per sample).
- **The total:** about 20,700-21,200; **600-1,100 slack** (estimate).

---

## 22. Implemented: cache fix, QADD, no Advanced types (user, 2026-11-11)

| Change | Where | What |
|---|---|---|
| **AS-6: SRAM D-cached** | `Core/Hardware/clocks.c`, `sysclk_init()` | Region 0 RASR loses S (bit 18): `(3UL << 24) \| (1UL << 17) \| (19UL << 1) \| 1UL`. The comment records why S must stay 0 and the DMA rule. The image holds `0x03020027` (`objdump`) |
| **AS-10: `QADD`** | `Core/DSPAudio/BufferTools.h`, `bufferTool_satAdd32()` | `return (sample_mx_t)__QADD(a, b);` (the existing `cmsis_intrinsics.h` intrinsic) in place of the int64 add and two tests: the same result for every input. `bufferTool_satSub32()` is unchanged |
| **Cymbal and HiHat Basic** | `CymbalParameters.h`, `HiHatParameters.h` | `INSTRUMENT_FLAG_BASIC` (the HiHat keeps `INSTRUMENT_FLAG_CHOKE`). A type with no class flag is not selectable, so Basic is set rather than Advanced cleared |
| Comment | `CymbalVoice.c` | "any Advanced-eligible voice slot" → "any voice slot" |

**The Advanced machinery is unchanged:**
- `INSTRUMENT_FLAG_ADVANCED`;
- `instrumentManager_advancedCountForScene()`;
- the two-per-Scene test;
- the copy/paste drop reason.

No current type carries the flag, so no Scene is limited. A future type
sets `INSTRUMENT_FLAG_ADVANCED` in its `*_INSTRUMENT_TYPE_FLAGS` and is
limited to two per Scene again.

**Measured on the build (`objdump`, `mixer_calcNextSampleBlock` and
`mixer_addVoiceInt16ToOutputAndFx`):**

| Loop | Before | After |
|---|---:|---:|
| Stereo dry add, per sample | 41-42 instructions (two 64-bit saturations) | **26** (two `QADD`) |
| Stereo add with a send, per sample | 56-57 | **33-34** (three `QADD`) |

- 49 `QADD` in the image.
- Flash 689,236 → 687,004 B (−2,232).
- ITCM 14,032 → 13,984 B: `satAdd32` is also inlined into ITCM code.

**Build and tests:**
- `make all && make img`: `build/LXRV2_lxr02.img` 687,004 B; flash headroom
  66,660 B; ITCM 13,984 / 16,384 B. The image is a logging build, so the
  cycle probe is in it.
- Host: `make -C tools/dsp_test all` exits 0.
  - `instrument_manager` failures 0;
  - special tags OK (202 rows);
  - every Alkaline law 0 failures;
  - every break build caught. The six FAIL lines in the log are
    `voice_control_break`'s expected failures.
- **Not run:** the loop gates (`elf_p4_check`, source pairing). `QADD`
  changes the mixer's inlined loops by design.
- **The A/B image** `build/s094p2_sram_cached/` is now redundant: the
  regular image has S = 0.
