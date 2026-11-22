# S101 P2 — Reductions: the worst-case CPU and the flash

**Date:** 2026-11-21. **Branch:** `dev-ph7-alkaline` (the S101 tree).
**Revision 2:** expanded after your answers of 2026-11-21.
**Revision 3** (your answers, 2026-11-21): the Colloid reductions are
acceptable; **no cap on Tetrakt or Colloid** (C3 withdrawn); the cap is
**144k**. New §A.7: four Tetrakts with two Advanced Colloids, and four
Colloids with two Advanced Colloids.

**Revision 4** (your answers, 2026-11-21, and the `SD_CARD_S101_P2` run):
- the Tetrakt items are fine; the Effect plumbing (E1, E2) is fine;
- no large cut to the Effect budget; **no Reverb quarter rate** (E3
  withdrawn);
- B4 and E4 explained in listening terms (§A.4 B4, §A.6b E4);
- `FS_OS=1` stays (§A.1b: the device test);
- further investigation:
  - three bit-exact Effect items: E5 (the Delay loop in two passes), E6 (a
    Reverb-only plate loop) and E7 (how the feed is counted);
  - the effects_service() cost now paired with its own type;
  - a correction to B3 (−2.1k → −0.6k per Tetrakt), which moves the
    binding case (§A.7).

**Status:**
- **Part B (flash):** F1, F3 and F4 are **built** (log: `S101_IMPLEMENTATION.md`;
  the first round's log is `S101_P1_IMPLEMENTATION.md`). F2 (the Cymbal/HiHat merge) is drafted in
  `DRAFT_HIHAT_CYMBAL_MERGE.md`.
- **Part A (CPU):** a plan for your decisions (§A.8). Nothing in it is built.
  The Colloid items (CL1–CL3) are accepted in principle (revision 3).
  Revision 4: the Tetrakt items (A2, B1 sub-block, B3) and E1/E2 are
  accepted.

**Revision 4 in short:**
1. **B3's saving was about 3.5 times too high.** Only the taps, the
   feedback pole and the ring write of the Tetrakt's 64-instruction space
   frame can go to half rate. The output frame (width allpass, DC blockers,
   saturating stores) stays at full rate. B3 is about **−0.6k per Tetrakt**,
   not −2.1k (§A.4).
2. **D1 on the device: −0.4k** (`alx` 18.3k → 17.9k). The linked-image
   figure was 0.45–0.55k (`S101_P1_IMPLEMENTATION.md` §4.3); the
   investigation's estimate was 1.0–1.3k. The half-rate items (B3, B4, CH1,
   CL3) are therefore re-derived from their linked loops here, and each is
   to be confirmed on a prototype build before it is relied on (§A.9).
3. **The Effect without any sound change:** today's worst user, the Delay
   during a feed with its effects_service(), is about 18.1k. A3 + E1 + E2
   + E5 bring it to **≈ 15.0k (−17 %), with the Delay's sound unchanged.**
4. **Revision 3's "0.7k over" assumed B4 and the larger B3.** With the
   corrections, no set of items without a sound or feature decision fits
   every Scene at 144k. The smallest sets that do are in §A.7.

**Your direction (2026-11-21):**
1. The control-rate and small changes: document them well. **§A.3, §A.4.**
2. The Colloid:
   - find exactly what its worst cases are;
   - no four oscillators across the board, but a particular worst offender
     may go to four;
   - **absolutely no dropping of stereo**;
   - look for other savings.

   **§A.5.**
3. The chorus may be reduced if it is above the Advanced margin. **§A.6.**
4. `FS_OS=1`, the wave-table savings and the transients: put them in
   directly. The transients go on `SD_CARD` as `LXR*.wav`, like the cymbal.
   **§B.3, built.**
5. The Cymbal/HiHat merge as a detailed draft. **`DRAFT_HIHAT_CYMBAL_MERGE.md`.**

Labels:
- **Measured:** the device trace, or the linked image's loops (S093 method:
  loops plus per-block paths ÷ 32).
- **Estimate:** the arithmetic is shown; to be replaced by the device probe.
- **Decision:** yours (§A.8).

---

## Part A — The worst-case CPU

### A.1 The target

| Run | Mixer average | Widget |
|---|---:|---:|
| `SD_CARD_S100`, Bank 076 Scene 5 (before B1/B3) | 146.4k (93.4 %) | 99 % |
| `SD_CARD_S101`, the same Scene (B1/B3) | 146.8k (93.7 %) | 96–97 % |
| `SD_CARD_S101_P2`, the same Scene, playing (D1, S1, F1/F3/F4; both `FS_OS` images) | 148.3–148.8k (94.7–95.0 %) | 97 %, 100 % for about 10 s after a Scene switch (§A.1b) |

- **The widget is queue pressure.** It sits about 2.5–3.5 points above the
  mixer's share: the trigger drain, the ISRs and the foreground.
- **"The worst case shows about 95 % median"** therefore means a worst-case
  mixer average of about **144k cycles a block (92 %)**.
- **The cap is 144k** (revision 3, your decision; revision 2 designed to
  142k). The S1 "outside the mixer" record (now built) replaces the
  2.5–3.5 point allowance with a measured one; the device run of §A.9 checks
  that 144k gives the 95 % widget.

### A.1b Device test: `FS_OS=1` against `FS_OS=0` (`SD_CARD_S101_P2`, revision 4)

**Your report (2026-11-21):** the S101 P2 image (F1/F3/F4) showed the
widget at 100 %. You then ran both:
- the `FS_OS=1` image, `build/LXRV2_lxr02.img` (666,716 B);
- the scratch `FS_OS=0` image (703,588 B; the same tree, `make all FS_OS=0`
  into the scratchpad).

On both, Bank 076 Scene 5 rose to 100 % after a Scene switch and settled at
97 % after about 10 s. There were no underruns, and the interface did not
bog. **Decision: `FS_OS=1` stays;** the reductions come from the voices.

**The two images run the same DSP code** (checked before the run, the
`FS_OS=1` image as the base):

| Gate | Result |
|---|---|
| `elf_p4_check` (oscillator loops) | 84 baseline loops, 84 exact |
| `elf_p4_check` (mixer, `elf_hotloops.py`) | 56 hot loops equal, 0 grew |
| `elf_s099_check` (engine loops) | 16 baseline loops, 16 exact |

Only `filesystem.c`'s flags differ, so any difference between the runs
would be foreground time.

**The trace (`asavetrc.bin`, 27,358 records, decoded with
`decode_devlogs.py`) holds five boots:**

| Boot | Records | Image (from its records) | Scenes |
|---|---|---|---|
| 1 | #0–#719 | the S101 P1 image (B1/B3): the same 720 records as `SD_CARD_S101`'s trace | 5 |
| 2 | #720–#13,466 | an image without S1 (no S1 records) | 5 → 4 → 1 → 4 → 9; Kit saves; Scene 8 slot 4 type browsing |
| 3 | #13,467–#22,923 | an S1 image: by your order, probably `FS_OS=1` | 9 → 5 |
| 4 | #22,924–#24,864 | an S1 image | 1 → 5 |
| 5 | #24,865–#27,357 | `FS_OS=0`: the image on the card is the scratch image, byte for byte | 5 → 1 → 5 |

The trace does not stamp the build, so boots 3 and 4 cannot be told apart
by their records.

**Scene 5 in the probe (cycles a block; window medians):**

| Section | S101 (boot 1) | Boot 3, playing | Boot 5, playing | Boot 3, transport stopped |
|---|---:|---:|---:|---:|
| `tkt` (slot 0) | 18,059 | 18,115 | 18,158 | 17,850 |
| `drm` (slot 1) | 13,721 | 13,834 | 13,954 | 13,377 |
| `alx` (slot 2) | 18,313 | 17,930 | 17,904 | 17,790 |
| `clx` (slot 3) | 19,945 | 19,922 | 21,217 | 19,857 |
| `col` (slot 4) | 17,506 | 17,549 | 17,648 | 17,433 |
| `hat` (slot 5) | 15,337 | 15,533 | 15,535 | 14,888 |
| Control | 7,001 | 6,998 | 6,793 | 6,989 |
| `effects_service()` | 3,013 | 3,149 | 3,074 | 3,021 |
| Set-up | 3,210 | 3,406 | 2,641 | 3,385 |
| SRR | 966 | 1,047 | 1,043 | 1,038 |
| Output / FX-send adds | 10,691 | 11,930 | 11,936 | 11,812 |
| Effect (the Delay) | 14,100 | 14,141 | 14,118 | **3,134** |
| Compressor | 4,624 | 4,635 | 4,591 | 4,559 |
| **Whole block, average** | **146,809** | **148,326** | **148,773** | **135,212** |
| Whole block, window maximum | 162–166k | 163–167k (104–107 %) | 163–168k (104–107 %) | 138k (88 %) |

What the run shows:
1. **No `FS_OS` difference.** Boot 3 and boot 5 differ by 0.3 % in
   Scene 5, inside window noise. The `clx` difference (19.9k against 21.2k)
   is the Advanced Colloid's known spread (21.0k median, 23.1k high, §A.6),
   not the filesystem.
2. **Why the widget reads 100 % after a Scene switch:**
   - the switch's own block: the outside-mixer span peaks once at
     139,599–295,411 cycles (0.9–1.9 blocks), the Scene apply;
   - then the AutoSave drain, admitted 1.0 s after the switch (the writer
     interval, `AUTOSAVE_WRITER_INTERVAL_MS` 1000), runs for **9.4–14.9 s**
     (boot 3: 12.7 s; boot 5: 9.4 s and 14.9 s), in its 1 ms slices (`SLOW
     filesystem_tick()` median 1,022 µs, longest 1,457 µs);
   - on a 95 % mixer the drain's background budget (30 µs/ms) and its
     slices push the queue to empty at the widget's samples; the widget
     averages ten samples over 5 s;
   - it settles when the drain ends: that is the "about 10 s".

   This matches your observation on both images.
3. **The worst case while playing is 7–8 % above the average.** Each
   1.5 s window holds blocks at 103–107 %, which the two-block queue
   absorbs (no underrun). The S1 section maxima show where they come from:
   single blocks with a slot at 23–31 % or the compressor at 9–11 % (four
   times its average) are ISR and cache events landing in a section. No
   section's DSP varies that much.
4. **The trigger drain is large on note blocks.** The outside-mixer span
   (`voiceControl_processPending()` + `seq_rampService()`) averages
   0.6 % but peaks at **13.8–27.8k cycles (8.8–17.7 %)** on a block that
   triggers several voices, against 1.6k with the transport stopped. That
   is foreground, absorbed by the queue; it is new information (S1).
5. **The Effect idles when the transport stops:** 14.1k → 3.1k (the S095 P1
   rule: below −90 dBFS the process stops and the return ramps out). The
   stopped 135.2k is not a worst case; the playing figure is.
6. **D1 measured:** `alx` 18.3k → 17.9k, **−0.4k** (−0.5 to −0.6k allowing
   for S1's per-slot bookkeeping below). The linked image predicted
   0.45–0.55k.
7. **S1 costs about 1.5k a block in logging builds.** The send-adds section
   rose 10.7k → 11.9k with no change to its code; `drm`, `hat`, set-up and
   SRR rose 0.1–0.2k each. The cause is `mixer_probeAdd()`'s max-word
   lookup and saturating add on every probe call. Release builds do not
   compile the probe (`DEV_MODE_LOGGING`). The estimates below are
   release-like (the S100 costs, before S1), so a logging-build device check
   reads about 1.5k higher (Q-C9).
8. **Underruns:** five, one per boot, each about 0.3 s after the boot settle
   (`BOOT_SETTLE END`). Boot 1 (S101 P1) has one too, so this is a start-up
   artefact that was already there. **None while playing.**
9. **Long main-loop passes follow the load, not `-Os`.**
   - In the idle phase: 2.1 ms median in boot 3 (mostly transport
     stopped), 6.5 ms in boots 4–5 (playing at 93–95 %).
   - In the drain phase: 33–37 ms median. These measure the gap between
     `filesystem_tick()` calls while B1 holds the drain back, not busy time.
10. **A decoder label is out of date.** `decode_devlogs.py` calls the writer
    deadline a "five-second debounce"; it is 1,000 ticks (1 s). This is a
    tool fix for later.

### A.2 The worst case today

Every slot at the dearest measured cost of a type it can hold:
- window medians from the S100 trace, D-cache on;
- or the S094 §21 share where that is higher;
- the Colloid rows from S096 P3's per-mode device probe.

| Part | Worst case | Source |
|---|---:|---|
| 4 Basic slots | **4 × 20.4k = 81.6k** | Tetrakt 20.4k median, 21.9k high (S100 trace line 673,343). Colloid 19.1–19.4k (§A.5). Basic Alkaline 16.5k, HiHat 15.5k, Drum 14.6k, Cymbal 14.5k, Snare 11.7k. |
| 2 Advanced slots | **2 × 21.0k = 42.0k** | Advanced Colloid 21.0k median, **23.1k high** (line 1,232,240): over the 21,803 Advanced share at its peaks. Advanced Alkaline 18.6k after D1. |
| Effect | **15.2k** | Delay 14.2k measured at 234.5 instructions a frame; ≈ 15.2k during a type-change feed (254.5). The other users: §A.6b. |
| Control section | 7.0k | measured |
| `effects_service()` | 4.0k | 3.0k measured with the Delay; 4.0k with CrumpBit (S094 run 2) |
| Set-up + SRR | 3.5k | measured |
| Output / FX-send adds | 10.8k | measured |
| Bus compressor | 4.6k | measured, when on |
| **Mixer total** | **≈ 168.8k (108 %)** | |

The gap to 144k is **about 25k cycles a block.**

**Revision 4:** `effects_service()` depends on the type, so it belongs with
the Effect, not the shared sections. The Delay's 15.2k comes with 3.0k
(3.0–3.15k in `SD_CARD_S101_P2`), not CrumpBit's 4.0k. Today's worst
pairing is therefore the Delay during a feed with its own service:
14.2 + 0.94 (the feed) + 3.0 = **18.1k**, and the total about 167.8k.
§A.6b and §A.7 count it this way.

### A.3 Tier A: control-rate and loop-form changes (no audible change intended)

Each entry:
- **Code:** the function and line in today's tree.
- **Mechanism:** what runs today.
- **Change:** what the step builds.
- **Measured basis / estimate:** the figures.
- **Risks / proof:** the host law and break, and the device check.

#### A1 — Staggered slot control (S094 AS-7, open)

**Code:** `mixer_calcNextSampleBlock()`'s control section (`mixer.c`, before
`MIXER_PROBE_SEC(CTL)`):
- `modNode_setItpReserved()`;
- `modNode_resetTargets()` (`modulationNode.c:516`);
- `modNode_reassignVeloMod()` (`:589`);
- `instrumentManager_dispatchRuntimeLfos()` (`InstrumentManager.c:2386`),
  which calls `lfo_dispatchNextValue()` (`lfo.c:150`) for each slot.

**Mechanism today, every block, all six slots:**
1. `modNode_resetTargets()` restores every velocity and LFO target to its
   base value: six velocity nodes, and the LFO pairs through
   `instrumentManager_visitRuntimeLfoNodes(modNode_restoreLfoPair)`.
2. `modNode_reassignVeloMod()` re-applies the six velocity modulations.
3. Each slot's LFO advances one block and writes its two targets
   (`modNode_updateValuePolarity()`, `instrumentManager_updateLfoAdapters()`).

**Measured:** 7.0k cycles a block (4.5 %). The straight paths are
`modNode_resetTargets` 199 and `lfo_dispatchNextValue` 56 per call, plus
their callees' setters.

**Change:** three slots a block (even, then odd).
- A slot's restore, velocity re-apply and LFO dispatch run together, every
  other block.
- Its LFO advances **two** blocks' phase when it runs (`phaseInc × 2`), so
  the rate is unchanged.
- The restore/re-apply pair must stay paired per slot: restoring all
  targets but re-applying only half would drop the other half's modulation
  for a block.
- The itp reservation (`modNode_setItpReserved()`) and the interpolation-slot
  generation stay every block (they are not per slot).

**Estimate:** −3.0k a block (about half of the per-slot share of the 7.0k).

**Sound:** LFO and velocity targets update every 1.45 ms instead of
0.73 ms.
- Targets that a voice itself ramps (filter cutoff via
  `SVF_recalcFreqSlew()`, the amp ramp) are unaffected.
- A fast LFO on pitch could step audibly (689 Hz update steps). **Listen**:
  LFO `frq` 127 on pitch, square and triangle.

**Proof:**
- **Host law:** the LFO phase after N blocks is the same as unstaggered
  (rate exact), and every slot's targets are restored and re-applied in the
  same block.
- **Break:** restore all slots but re-apply half; the law must fail.
- **Device:** the control section in the probe.

#### A2 — Tetrakt layer control staggered

**Code:** `Tetrakt_calcVoiceAsync()` (`TetraktVoice.c:489`) calls
`tkt_layerControl()` (`:226`) for each of the four layers.

**Mechanism today:** each layer's block-rate work, every block:
- the envelope step;
- drive, bias and wet;
- the tone SVF's coefficient targets and their per-sample ramp steps
  (`da1..da3`);
- the pan and send gains;
- the SRR rate.

The post loop (`tkt_layerPost()`, `:553`) ramps the envelope gain
(`gStep = (env - envLast) / 32`) and the tone coefficients across the block.

**Measured:** `tkt_layerControl` 346 instructions per layer per block
(`elf_blockpath.py`), 1,384 a block for four layers: about 43 per sample.

**Change:** two layers a block. A layer's control runs every other block and
computes its targets for **two** blocks ahead. Its ramps (envelope gain,
coefficient steps) are then spread over 64 samples:
- either the post loop takes a per-layer "first or second half" flag and
  uses half-steps;
- or the control writes the mid-point target for the first block and the
  end target for the second.

  The second needs no post-loop change. Recommended.

**Estimate:** −692 instructions a block × 1.52 = **−1.05k cycles per
Tetrakt**, −4.2k with four Tetrakts.

**A2q (revision 4): one layer a block.** Each layer's control runs every
fourth block (2.9 ms), with its targets four blocks ahead, the same
mid-point method in quarters.
- **Estimate:** −1,038 a block, **−1.58k per Tetrakt** (0.53k more than
  A2).
- **Sound:** S1–S2. The envelope becomes straight segments of 2.9 ms. At
  the shortest decays (a few ms) that is coarse, but a trigger still runs
  its layer's control at once. Listen on `dcy` 0–10 hits.
- **Proof:** A2's law with the quarter stagger.
- **Use:** only if the four-Tetrakt Scene needs it (§A.7).

**Per block today** (`tetrakt_cpu.py`, revision 4): 2,641 a block (82.5 a
sample).
- 4 × 346 is layer control (A2's target).
- The rest:
  - binds 4 × 133: only in a block where every layer starts;
  - the ring's wrap segments 4 × 57;
  - the post-loop entries 4 × 69;
  - `Tetrakt_calcVoiceAsync` 68, `tkt_spaceOut` 138, and the sync block
    15.
- Nothing else is a large per-block target.

**Considered and rejected (revision 4): two layers in one loop.** Keeping
the buses in registers across a layer pair would save 6 bus loads and
stores per paired layer (−12 a sample, bit-exact). But each layer holds
about 22 live floats, so two layers need about 44 against the 32 FPU
registers, and the spills would cost more than the saving.

**Sound:** the envelope and coefficient slopes change once per 1.45 ms
instead of per block. The decay envelope is smooth at that rate; a velocity
or tone-envelope jump at a trigger is the case to check. The trigger itself
must still run the layer's control at once (`Tetrakt_triggerVoice()`), so a
hit never waits a block.

**Proof:**
- **Host law:** a layer's output with staggered control is within the S1
  bound of the every-block form, decays monotone, and a trigger lands in the
  same block.
- **Break:** no trigger-time control; the law must fail.

#### A3 — `effects_service()` staggered

**Code:** `effects_service()` (`EffectsManager.c:2228`), every block: the
Effect's Morph, LFO and sequencer lane resolution (`effects_laneTable`,
`effects_lfoResolve`) and the writes into the type's runtime.

**Measured:** 3.0k cycles a block; the straight path 167 instructions plus
the writers.

**Change:** resolve half the Effect rows per block, alternating. A changed
row (a menu edit, a lane step, an R13 write) is resolved in the block it
arrives: it is flagged and wins the next block's turn.

**Estimate:** −1.5k.

**Sound:** an LFO- or Morph-driven Effect row updates every 1.45 ms. The
Space types already glide their delay time (150 ms) and band coefficients,
so the step is inaudible. CrumpBit's grain and `dly` rows are to check by
ear.

**Proof:**
- **Host law:** every row reaches its target within two blocks, an edited
  row within one.
- **Break:** drop the edit flag.

#### A4 — Bus compressor output stage (corrected from revision 1)

**Correction:** revision 1 proposed "the gain every 4 samples". The gain is
**already** per block: `exp2f()` once in `busComp_processBlock()`
(`BusCompressor.c:262`), ramped across the block (`gk_step`).

**Mechanism today, per sample (71 instructions):** the power-sum detector;
the two-band split (one-pole LP per channel); the HF-share saturator
`ul × (c_out + c_out3 × ul²) + hl × g_hf` per channel; the ceiling knee.

**Change:** keep the detector and the gain. Simplify the output stage:
- one saturator on the full band, instead of the split's LP + HF share;
- the ceiling knee folded into the cubic's clamp.

**Estimate:** −25 instructions a sample ≈ **−1.4k** at 1.8 cycles an
instruction.

**Sound:** S2–S3. The split exists so that the HF does not saturate as hard
as the body (S074 / S095 P4 retune). This is a sound decision: **moved to
Tier B in effect**, kept here for continuity.

**Proof:** the `buscomp` host laws (S074) with the new stage's bounds; your
ear.

#### A5 — Send-add constants and incremental ramps (D2)

**Code:** `mixer_addVoiceInt16ToOutput()` (`mixer.c:437`),
`mixer_addVoiceInt16ToOutputAndFx()` (`:634`) and the stereo twins
(`:889`).

**Mechanism today, per sample:**
- the dry gain `lastGain + i × inv_size × gain_delta` (an int→float convert
  and two multiplies);
- the same again for the send;
- then `× 256.0f`, and in the stereo send `× panL`, `× panR`.

**Measured:** the section 10.8k cycles a block, 10.8–11.3k depending on how
many voices are stereo.

**Change:**
- per-block constants (`gain × 256`, `send × 256 × pan`);
- incremental ramps (`g += dg`), as the Tetrakt and the compressor already
  ramp.

**Estimate:** 2–4 instructions a sample per slot, **−0.6 to −1.1k**.
Before building: `elf_hotloops.py` on the linked loops, to see what `-Ofast`
already folds. If it already hoists the constants, the saving is the
convert and one multiply only.

**Sound:** S1 (last-bit differences in the ramp).

**Proof:** the host bench `col_mixer_funcs` (the extracted mixer adds)
against the restated ramp within 1 LSB; the hot-loop gate.

#### A6 — Alkaline staggered control (S094 AS-7 for the Alkaline)

**Code:** `Alkaline_calcVoiceAsync()`: the S093 P8 per-block stages:

| Stage | Instructions |
|---|---:|
| `alkMeg_step` | 247 |
| `dl_slowUpdate` | 228 |
| `alk_expoCurrent` | 190 |
| `alkAcc_step` | 129 |
| others | ≈ 190 |
| **Per block** | **1,319 (9 VDIV), about 41 a sample** |

**Change:** run the slow-moving stages on alternate blocks, with their
outputs ramped over two:
- `dl_slowUpdate` (the ladder's slow network update);
- `alkAcc_step` (the accent capacitor).

The envelope (`alkMeg_step`) and the expo current stay per block: they set
the 303's attack and the filter sweep.

**Estimate:** −(228 + 129) / 2 ≈ −180 instructions a block ≈ **−0.35k per
Alkaline.**

**Why it matters:** with no type cap (revision 3) the Alkaline only sets a
worst case through the Advanced Alkaline (18.6k), which is below the Advanced
Colloid. A6 is therefore **optional**: it helps Scenes heavy in Alkalines,
not the worst case of §A.7.

**Sound:** the accent's slow charge and the ladder's slow network move at
689 Hz. To check against the S093 P6 golden runs (AL-M, AL-SO, AL-R).

**Proof:** the Alkaline laws within their bounds; the AL golden comparison.

### A.4 Tier B: small sound changes (S2–S3)

#### B1 — Tetrakt tone coefficients per block

**Code:** `tkt_layerPost()` (`TetraktVoice.c:553`): `a1 += da1; a2 += da2;
a3 += da3;` per sample per layer (the tone SVF's coefficient ramp).

**Change:** the coefficients held for the block (the targets set in
`tkt_layerControl()`); the per-sample ramp removed.

**Measured basis:** 3 adds × 4 layers = 12 instructions a sample, plus their
live registers (the loop is at 57–60).

**Estimate:** −0.58k per Tetrakt; −2.3k with four.

**Sound:** a fast tone-envelope sweep steps once per block (0.73 ms). The
Colloid already ramps per sub-block (`COL_SUB_BLOCK`): a sub-block ramp
(every 8 samples) is the middle way, at about −0.4k.

**Proof:** a third-difference click law on a tone-envelope sweep (the
CO-PWG method), with the bound at the sub-block form's.

#### B2 — Tetrakt: one SRR per voice (reserve)

**Code:** `tkt_layerPost()`: `cnt += rate; if (cnt >= 1) { cnt -= 1; hold =
x; }` per layer.

**Change:** one SRR on the summed buses, before the space stage.

**Feature change:** the four `tkt_srr_a..d` rows become one `tkt_srr`.

**Estimate:** about 15 instructions a sample, −0.7k per Tetrakt.

**Only if** B3 is refused (revision 3). **Revision 4:** with B3 corrected to
−0.6k, B2 is one of the two Tetrakt items of package Q (§A.7), to decide
after the device run of package P.

#### B3 — Tetrakt space stage at half rate

**Code:** `tkt_spaceOut()` (`TetraktVoice.c:711`): the S1 delay line (50 ms,
one FxBuffer voice unit) and the W2 envelope-derived allpass width.

**Measured:** the frame loop is 64 instructions (`tetrakt_cpu.py`: "spaceOut
frame 64"). Revision 3 read this as "64 + 29 = 93".

**Change:** the chorus's C3 form:
- the delay line written and tapped once per output pair (the pair's mean
  in; the tap interpolated between this and the previous one for the first
  output);
- the width allpass and the output mix at the full rate.

**Estimate, corrected in revision 4:**

| Part of the 64-instruction frame (`tkt_spaceOut()`, `TetraktVoice.c:711`) | Instructions | At half rate? |
|---|---:|---|
| Two taps: two halfword loads, a subtract, two conversions, a fused multiply-add and the scale each | ≈ 18 | yes |
| The feedback one-pole (`lp += fbK (mid − lp)`) | 3 | yes |
| The ring write (`(send + fb lp) × 32,767`, saturate, store) and its send load | ≈ 7 | yes |
| Three pointer increments | 3 | yes |
| `tkt_outFrame()`: the W2 allpass and its mix, two DC blockers, two saturating stores; the bus loads and the `tap × slv` adds | ≈ 33 | **no** (full rate in B3) |

- **Saved:** ≈ 31 ÷ 2 ≈ 15.5 a sample.
- **Added:** the pair mean of the send, and the interpolated taps for the
  odd output: about +3.
- **Net:** about −12.5 a sample × 32 × 1.52 = **−0.6k per Tetrakt** (−2.4k
  with four).
- **Revision 3's −43 a sample (−2.1k)** halved nearly the whole frame,
  including `tkt_outFrame()`, which B3 keeps at full rate. That error is the
  same kind as D1's investigation estimate (§A.1b, item 6).

**Sound:** the space's wet above about 11 kHz folds and images, as on the
two choruses.

**Proof:** a CH-HALF-style law (level at 1 kHz within 0.5 dB, images and
folds bounded) and the TK-SPACE laws.

#### B4 — Delay and Echo tape loops at half rate (explained in revision 4)

**Code:** `space_delayRun()` (`SpaceEffect.c:471`); `space_echoTapeRun()`
(`:645`). The Reverb's plate already runs at half rate (`plateRun` ×0.5)
and the Echo's plate at a quarter.

**What runs today (the Delay; the Echo is the same on one mono tape):**
- **The tape:** a stereo int16 ring at 44.1 kHz. Each output frame writes
  one tape frame, and four heads read it: L's and R's main heads, and
  `str`'s two taps a third of the way in.
- **Speed:** at the minimum Effect share it holds about 30,600 frames,
  about **0.69 s** at full speed. A longer `tim` slows the tape (`va` =
  capacity ÷ time) down to the ¼ floor, which serves the 2.77 s maximum.
  A slow tape is darker: the playback LP is `bw × va`, and the record LP
  `rk = 0.25 + 0.75 va` closes.
- **Each frame:**
  1. the four head reads;
  2. the output (`dry × in + wet × heads`);
  3. the loop: mid/side, the `xfb` allpass, then per channel the playback
     LP, the band-pass, the drive and the `x − c3 x³` clip, and the record
     LP;
  4. the record, packed with `deg`'s bit reduction.
- **The heads are output reads.** The band-pass and the playback LP sit
  only in the feedback path. So at a full-speed tape (`tim` under about
  0.69 s) **the first repeat is a full-band copy of the input** (the record
  LP is open, `rk` = 1). Only the second and later repeats pass through the
  loop's filters. At the brightest loop settings (`ton` 0, `wid` 127) those
  reach 21 kHz too.

**What B4 builds:**
- The tape runs at **22.05 kHz**: one tape frame per pair of output frames.
- Before recording, the input passes the **half-band decimator the Reverb
  already uses** for its plate (`SpaceEffect.c:723`: 11 taps, +0.01 dB to
  5 kHz, −6 dB at 11 kHz, −58.8 dB above 17 kHz).
- The four heads, the whole loop chain and the record run **once per
  pair**.
- For the output, the heads are interpolated back to 44.1 kHz between this
  pair's read and the last.
- The dry, the output mix, the `dry`/`wg` ramps and the ducking follower
  stay at 44.1 kHz.

**What you would hear:**
1. **The dry: no change.**
2. **The first repeat loses the top octave.** Every repeat, including the
   first, is flat to about 5 kHz, −6 dB at 11 kHz, and has nothing above.
   This is the most audible change: a hat, a cymbal or a Tetrakt transient
   comes back like a tape or BBD echo, not a digital copy. Later repeats
   change less, because at most `ton`/`wid` settings the loop's own filters
   already take that band out on every pass.
3. **Almost no new tones from the cut.**
   - Above 17 kHz the half-band removes the input (−58.8 dB).
   - Between 11 and 17 kHz, its transition band lets a little through, and
     that folds to 5–11 kHz, well down in level: a faint residue on very
     bright material.
   - The proposal uses this filter, not the cheaper pair mean, because a
     pair mean would fold a 15 kHz hat partial to about 7 kHz at only
     −6 dB: a metallic ring in every echo.
   - The interpolator's images (11–22 kHz) are faint.
4. **High `deg` drive sounds a little rougher.** The clip and the bit
   reduction run in a 22 kHz loop, so their harmonics above 11 kHz fold
   back into the band instead of above it: a slightly grainier distortion
   at high drive. At low drive there is no difference.
5. **Long delays come out brighter and cleaner than today.** The tape holds
   twice the time per frame:
   - full speed now reaches about **1.39 s** instead of 0.69 s;
   - the 2.77 s maximum runs at ½ speed instead of ¼;
   - so the darkening and coarsening a slowed tape adds at long times
     mostly goes away.

   If you want to keep today's "slowing tape" character, the speed can be
   mapped as today (½ the computed `va`). That is a choice (Q-C6).
6. **Unchanged:** the time and its glide, wow/flutter/drift, `rev`'s
   windows, `hld`, the `xfb` ping-pong and `str`'s taps. Positions stay
   fractional and interpolated, so nothing steps.

**Cost, from the linked loop (revision 4: revision 3's 129 a frame
halved too much):**
- **The Delay's frame loop is 211 instructions.** Of those, about 22 stay
  at full rate: the two input loads, the output pair, the peak and the
  ramps. About 189 go to half rate.
- **New per frame:** about 94.5 (half rate) + 22 + 4 (the wet's
  interpolation) + about 10 (the stereo half-band) = 130.5, plus the 23.5
  per block, **≈ 155 a frame (from 234.5): −80 a frame, −3.7k.**
- **The Echo:** its tape loop of 100 becomes about 65 (the mono half-band
  ≈ 5), so 205.5 → about 170: **−1.7k.**
- After B4 the dearest Space type is the Reverb during a feed (§A.6b).
- The D1/B3 lesson applies: this figure is to be confirmed on a prototype
  build (§A.9).

**Proof:**
- the Space laws (`make -C tools/dsp_test space`, 51 lines) with a
  half-rate twin of the S100 P2 rows;
- an alias law: the fold of a 15 kHz and an 18 kHz input in the 0–11 kHz
  wet stays under the half-band's own bound (measured on the Reverb's
  decimator first). It must break with a pair mean;
- `space_cpu.py` against its caps.

#### B5 — The compressor output stage (A4)

A sound decision, listed here: −1.4k.

### A.5 The Colloid: its exact worst cases

#### Per mode

Linked image (`colloid_cpu.py`, instructions a sample, stereo route `par` =
`rte str`, the dearest route) beside the S096 P3 device probe (cycles a
block, stereo, noise on):

| Mode | Oscillators | Loops | Per block ÷ 32 | **Total** | Device median (max) |
|---|---:|---:|---:|---:|---|
| `wav` (plain) | 10 | 272.2 | 102.2 | **374.4** | 18,362 (19,085) |
| `wtb` (plain loops) | 10 | 272.2 | 102.2 | 374.4 | 18,681 (19,211) |
| `pwm` Saw/Rec | 6 | 323.2 | 79.5 | **402.8** | 19,136 (19,766) |
| `pwmw` Tri (warp) | 6 | 326.2 | 79.6 | **405.9** | (in pwm's run) |
| `wts` scan | 6 | 316.2 | 90.0 | **406.3** | 19,124 (19,705) |
| R-S chop (S099) | 6 | 295.2 | 92.5 | 387.8 | not measured |
| Advanced (`clx`) `pwm` + chorus | 6 | | | **449.7** | 21.0k median, 23.1k high (S100 trace) |

#### There is no single worst offender

`pwm`, `pwmw` and `wts` are within 1 % of each other (402.8–406.3, device
19.1k each). Taking one of them to four oscillators would not lower the
Colloid's worst case: the other two would still set it. Only all three
together would (≈ −66 a sample, −3.1k), which is the across-the-board cut you
ruled out. **Recommendation: keep six.**

#### Where a six-oscillator mode spends its time (`pwm` Saw/Rec, stereo)

| Part | Instructions a sample | Measured by |
|---|---:|---|
| 3 pair loops (`col_pwmPairStore` / `col_pwmPairAdd`) | 3 × 66 = 198 | `elf_looppath.py` |
| Stereo filter, saturator, tone, noise (`col_renderPar`, `ColloidVoice.c:1632`) | ≈ 125 (154 longest path) | `elf_looppath.py` |
| Per block (2,544 a block ÷ 32), of which: | 79.5 | `elf_blockpath.py`, `colloid_cpu.py` |
| — `col_oscControl()` (`:922`): 352 straight + 6 × 134 per oscillator + `col_pspK` 69 | 38.3 | |
| — `Colloid_calcVoiceAsync()` 386, `col_svfControl` 99, `col_noiseControl` 127, 3 × `col_egStep` 32 | 22.7 | |
| — `Colloid_calcVoiceSyncBlock` 134, `col_renderPar` entry 123, pair-loop entries | 10.9 | |

On the device the block-rate work is **4.4–4.6k of the 19.1k cycles (24 %)**.

#### Colloid savings that keep six oscillators and stereo

| ID | Change | Basis | Estimate | Sound |
|---|---|---|---:|---|
| **CL1** | **Staggered oscillator control.** `col_oscControl()` updates half the oscillators per block (alternating), each with a two-block pitch and width target. The pair loops' width glide (`pwPos0` → `pwPos`) then spans 64 samples; the S096 P2 F5 glide law must hold. | 134 instructions per oscillator per block | **−0.6k** (6 oscillators); −1.0k (10) | pitch and width targets move every 1.45 ms; the per-sample glide keeps them smooth. S1. |
| **CL2** | **Saturator without VDIV.** `col_postPair()` (`:398`) computes `z (27 + z²) / (27 + 9 z²)` for both channels with one VDIV (about 14 cycles) per sample. An odd polynomial on the clamped `abs(z) ≤ COL_P32_ZMAX` (degree 7, 4 FMAs) replaces the division. | 1 VDIV a sample | **−0.2 to −0.3k** | S1 if fitted to the rational's error bound |
| **CL3** | **Half-rate noise.** The noise source (an RNG read, a one-pole HP and LP, two mix gains: about 14 instructions a sample in `col_renderPar`) runs once per output pair, held or interpolated. | ≈ 14 a sample; revision 4: the two mix gains stay at full rate and the hold/interpolation adds about 2, so about −4 a sample | **−0.2k** (revision 3: −0.3k) | the noise band to 11 kHz (S2): the noise is already tone-shaped (`nTone`) |
| **CL4** | **Trim `col_oscControl()`.** 134 per oscillator: the pitch slew, `alk_exp2f`, the increment, the width target and slew, the row choice, the motion LFO. Candidates: one `alk_exp2f` per oscillator shared with the interval, the width tables read once per block for the stack. | 6 × 134 a block | to measure | none |
| **CL5** (revision 3) | **Noise and filter control on alternate blocks:** `col_noiseControl()` 127 + `col_svfControl()` 99 instructions a block | 226 a block | **−0.17k** | S1 (the filter coefficients already ramp per sub-block) |
| CL1 in thirds (revision 3) | CL1 with each oscillator's control every third block | | −0.2k more | S1 |
| CL-X | (Ruled out) stereo route removal; 6 → 4 oscillators in all three dearest modes. | | | |

**Colloid worst after CL1–CL3:** 19.4k → about **18.2k** (estimate); with
CL1 in thirds and CL5 (revision 3, §A.7): about **17.8k**. Revision 4, with
CL3 at −0.2k: about **17.9k**.

**CL4, a first look (revision 4).** `col_oscControl()` costs 134
instructions per oscillator per block, 804 for six. Trimming a quarter of
it (sharing the `alk_exp2f` of the pitch with the interval, and reading the
width tables once per stack) would be about −200 a block, **−0.3k per
Colloid-engine voice**: −1.5k over the five such voices of the binding
Scene. No sound change. Still to be measured on a prototype; §A.7 counts it
at −0.25k a voice.

### A.6 The Advanced margin and the chorus

| Advanced type | Median | High window | Share | Margin |
|---|---:|---:|---:|---|
| Advanced Colloid (`clx`) | 21.0k | **23.1k** | 21,803 | **over at its peaks** |
| Advanced Alkaline (`alx`), after D1 | ≈ 18.6k | ≈ 18.9k | 21,803 | 2.9k under |

The Advanced Colloid exceeds the Advanced share, so its chorus is to be
reduced (your rule).

**The chorus on the Colloid today** (S099 P1 review C3, half rate, L-R form):
- `bbd_sideH()` 41 instructions per pair per side, i.e. 41 a sample for both
  sides;
- plus `bbdChorus_control` 142 and the render's 63 a block (6.4 a sample);
- **about 47 a sample, ≈ 1.6k cycles a block on the device** (`clx` 21.0k
  against `col` about 19.4k).

**CH1: quarter-rate rings on the Colloid's chorus.**
- **Change:** the ring written and tapped once per four outputs (the
  4-sample mean in; the tap interpolated linearly across the four), the
  post pole and the dry + wet at the full rate.
- **Estimate:** −8 to −9 instructions a sample ≈ **−0.4k.**
- **Sound:** the wet band to about 5.5 kHz. The chorus's post pole is at
  9 kHz (`BBD_COL_TONE`) and tracks down to 5 kHz at long delays, so the
  top octave of the wet is lost. Your ear decides (S3).
- **Proof:** a CH-HALF-style law at quarter rate.

**Advanced Colloid after CL1–CL3 + CH1:** 21.0k → about **19.4k** (high
window about 21.5k), inside the 21,803 share; with CL1 in thirds and CL5,
about **19.0k**; revision 4 (CL3 at −0.2k): about **19.1k**.

CH1's −8 to −9 a sample was worked out from the loop's split (the post pole
and the dry + wet left at full rate), so it is not the D1/B3 kind of error.
It is still to be confirmed on a prototype (§A.9).

### A.6b The Effect: its worst-case users and the Effect budget (revisions 3–4)

**The Effect section has two parts:**
- **the plumbing:** the bus conversion (`mixer_fx_bus` int → float, 10
  instructions a frame), `effects_process()` (28 a block), and the return
  (`mixer_addFxReturnToOutput()`, `mixer.c:1039`: two per-sample gain ramps,
  the float → int conversion and a saturating add per channel);
- **the type's process.**

**Measured:**
- **The plumbing alone:** the Effect section with the transport stopped,
  where `sk_idleBefore()` skips the process. **3.2k** a block (S100 trace,
  boot at line 1,479,204).
- **The Delay:** 14.2k in all (S100 trace), so its process is about 11.0k:
  234.5 instructions a frame × 32 = 7,504, i.e. **1.47 cycles an
  instruction.**

Revision 2 scaled the other types by the Delay's whole 14.2k; this table
splits plumbing and process.

#### Every Effect user

Instructions a frame from `space_cpu.py` and `crumpbit_cpu.py` (linked
image). Cycles = instructions × 32 × 1.47 + 3.2k plumbing (estimate),
except where measured.

| Type | Instructions a frame | With the feed (during a type change) | Effect section, cycles | With the feed |
|---|---:|---:|---:|---:|
| **Delay** | 234.5 | 254.5 | **14.2k (measured)** | ≈ 15.2k |
| Echo | 205.5 | 225.4 | ≈ 12.9k | ≈ 13.8k |
| Reverb | 189.5 | 209.5 | ≈ 12.1k | ≈ 13.1k |
| CrumpBit `grn` (resonant SVF, S094 P2 §23) | 182.9 | (no feed) | ≈ 11.8k (S094 device: 11.2k before the SVF came back) | — |
| CrumpBit `rvb` | 181.3 | — | ≈ 11.7k | — |
| CrumpBit `dly` / `pit` / `cmb` | 158.6 / 155.0 / 154.8 | — | ≈ 10.6k / 10.5k / 10.5k (S094 device about 10.4k) | — |
| OFF | 0 | — | 0.05k (no bus) | — |

- **The feed:** the outgoing Space type's tail is mixed by the incoming
  type's process (`sk_feedMix()`, 16 a frame plus 127 a block) for up to
  6 s after a type change. It is part of the worst case, because a type
  change can happen in the worst Scene.
- **`effects_service()` depends on the type:**
  - 3.0k with the Delay (S100);
  - **4.0k with CrumpBit** (S094 run 2: `fxsvc` 3,953–4,057).

  Revision 3 put 4.0k (2.0k after A3) in the shared sections for every
  Scene. **Revision 4 pairs each type with its own service**: a Scene
  cannot run the Delay's process and CrumpBit's service at once.

#### Every Effect user with its own `effects_service()` (revision 4)

Cycles a block (k): the Effect section plus its service. The Space types
are shown during a feed (+0.94k: 20 a frame × 32 × 1.47); the feed does not
apply to CrumpBit.

| Type | Today | After A3 + E1 + E2 (accepted / no sound change) | + E5 / E6 (bit-exact) | + B4 (sound) |
|---|---:|---:|---:|---:|
| **Delay** | 14.2 + 0.94 + 3.0 = **18.1** | **15.7** | **15.0** (E5) | ≈ 12.0 |
| Echo | 12.9 + 0.94 + 3.0 = 16.8 | 14.4 | 14.4 | ≈ 12.7 |
| Reverb | 12.1 + 0.94 + 3.0 = 16.1 | 13.6 | 13.2 (E6) | 13.2 |
| CrumpBit `grn` | 11.8 + 4.0 = 15.8 | 12.9 | 12.9 (12.6 with E4) | 12.9 |
| CrumpBit `rvb` | 11.7 + 4.0 = 15.7 | 12.8 | 12.8 | 12.8 |
| CrumpBit `dly` / `pit` / `cmb` | 14.6 / 14.5 / 14.5 | 11.7 / 11.6 / 11.6 | — | — |
| **Worst** | **Delay, 18.1** | **Delay, 15.7** | **Delay, 15.0** | **Reverb, 13.2** |

**Without any sound change the worst falls 18.1k → 15.0k (−17 %)**, and the
Delay stays the dearest type. That is the "plumbing" route you accepted,
plus A3 and the bit-exact E5. B4 (−1.8k more on the worst, because the
Reverb then sets it) is the only Effect item left with a sound change.

#### Ways to lower the Effect's worst case (revision 4)

| ID | Change | Saving | Sound | Status |
|---|---|---:|---|---|
| **E1** | **Return ramps incremental, constants per block** (A5's method on `mixer_addFxReturnToOutput()`) | ≈ −8 instructions a frame, **−0.4k** | S1 (last-bit ramp differences) | **accepted** |
| **E2** | **A float FX bus.** The send adds (`mixer_addVoice*AndFx`) accumulate the send into `mixer_fx_bus.f` directly, instead of an int32 bus and the conversion loop. | the 10-instruction conversion loop, **−0.5k** | S1. The int bus saturated at full scale on an overloaded sum; the float bus does not (the Effect sees > 1.0). Each type's input clip then decides. | **accepted** |
| **E5** | **The Delay loop in two passes** (below) | ≈ −10 to −20 a frame, **≈ −0.7k** | **none: bit-exact** | new; recommended |
| **E6** | **A Reverb-only plate loop** (below) | ≈ −5 to −10 a frame, **≈ −0.4k** | **none: bit-exact** | new; matters only with B4 |
| **E7** | **The feed counted as a transient** (below) | −0.94k on the Space types | none (an accounting decision) | new; your decision |
| **B4** | Delay and Echo tapes at half rate (§A.4, explained there) | Delay −3.7k, Echo −1.7k; the worst becomes the Reverb, 13.2k | the wet flat to 5 kHz, nothing above 11 kHz; long delays brighter | your decision |
| **E4** | CrumpBit `grn`'s grain plan on alternate blocks (below) | **≈ −0.2 to −0.4k** (revision 3's −0.6k counted the whole 846 a block) | S1 (below) | reserve: `grn` is never the worst unless B4 and E7 are both taken |
| ~~E3~~ | ~~The Reverb plate at a quarter rate~~ | | | **withdrawn** (your answer: avoid it) |

##### E5 — the Delay loop in two passes (bit-exact)

**Today:** `space_delayRun()`'s normal-head path is 211 instructions a frame
(the `rev` path 206). Of those:
- **26 are stack reloads and 2 are stack stores** (`vldr`/`vstr [sp]`);
- 23 are register moves;
- 15 are conversions.

The loop keeps about **39 floats live** (22 block constants, the two
channels' four filter states each, and the positions, ramps, peak and
`xs`) against the FPU's 32 registers. So the compiler parks about ten
constants on the stack and reloads them every frame.

**Change:** the code itself says every read lands at least `SPACE_D_MIN`
(36 frames) behind the block start, so no read sees this block's writes.
The block can therefore be split:
1. **Pass 1:** the positions and the four lane reads (the two heads, or
   `rev`'s two windows, and `str`'s two taps) for all 32 frames, into four
   32-float arrays. Its live set is about 10 values.
2. **Pass 2:** the output, the loop and the record, reading the arrays. Its
   live set is about 30 values.

The arithmetic and its order are unchanged, so **the output is
bit-identical**.

**Estimate:**
- −20 to −25 (most of the 28 spill instructions);
- +8 (four stores in pass 1, four loads in pass 2);
- **net −10 to −20 a frame, about −0.7k.** To measure on a prototype.

**RAM:** 4 × 32 floats = **512 B of stack** inside the Effect process (or
2 × 16 frames: 256 B, slightly more loop overhead). It is transient stack,
not a static, but it is a RAM change: **your sign-off**, after a stack
headroom check.

**Proof:**
- **Host law:** the two-pass Delay matches today's output sample for sample
  over the S100 P2 rows (every `str`, `spr`, `rev`, `hld`, `xfb`, `wow`,
  and `tim` through the speed floor).
- **Break:** pass 1 reading one frame late; the law must fail.
- `space_cpu.py`.

The Echo's tape loop (100 a frame) has only 4 spills: no gain there.

##### E6 — a Reverb-only plate loop (bit-exact)

**Today:** `space_plateRun()` serves both the Reverb (two halves, three taps
per side, the shimmer line: 219 a tick at half rate, ≈ 109.5 a frame) and
the Echo (one half: 149). Its Reverb path has:
- **22 stack reloads and 10 stack stores**;
- a `var`-indexed write offset (`space_plateW[var][k]`) on each of its 13
  writes;
- the `three` tests.

**Change:** a second copy of the loop for the Reverb only, with `three`
constant and the write offsets taken once per block. The Echo keeps the
shared one.

**Estimate:** −10 to −20 a tick, i.e. **−5 to −10 a frame, about −0.4k**,
and **about +0.9 KB of flash** (one more loop). To measure.

**When it matters:** only after B4, when the Reverb becomes the worst type.

**Proof:** the plate's output bit-identical to today's (host); a break with
one write offset off by one.

##### E7 — how the feed is counted (an accounting decision)

**What the feed is:**
- **When:** for up to 6 s after an Effect type change, while the outgoing
  Space type's tail plays out.
- **How:** `sk_feedMix()` (16 a frame plus 127 a block, about 0.94k) mixes
  that tail through the incoming type.
- **How long:** it ends at −80 dBFS held for 10 ms, or at 6 s.

**The two readings:**
- **Count it (today's rule, the default):** the worst case includes a type
  change in the worst Scene.
- **Treat it as a bounded transient:** the two-block queue cannot absorb
  6 s, so for those seconds the widget would read about 0.6 point higher
  in a Scene already at the cap. Nothing underruns, because the cap leaves
  the block 8 % short of its budget.

**Saving:** 0.94k on every Space type's worst. **Recommendation:** keep
counting it unless a Scene needs exactly this 0.9k.

##### E4 — what staggering the grain plan does to the sound

**What `grn` does today, every 32-frame block (0.73 ms):**
- **one decision:** is a grain due in this block? A deferred (stolen-voice)
  trigger first, else the Density countdown;
- **one candidate** (position, pitch, pan) from one hardware RNG word,
  whether used or not;
- **the run state:** the voices' window ramps from their age, and their
  gains from the pan bytes (a voice idle at the block start is parked).

The grains themselves (their window, read speed, pan and the echo line)
play per sample in `crumpBit_grainSpan()`.

**What E4 changes:** the decision and the candidate move to every other
block (1.45 ms). The run state, the grains and the echo stay per block and
per sample.

**What you would hear:**
1. **The grains themselves: no change.** Same windows, pitch, pan,
   position, freeze and echo.
2. **Rhythmic Density (65..127, tempo-locked): no change.** The trigger
   frame inside the pair of blocks is still exact.
3. **Random Density (0..62):** intervals are random in [0, 2 M], with a
   floor of one block today and two blocks with E4.
   - Over most of the range M is many blocks, so the average density is
     the same.
   - At the densest settings, where intervals reach the floor, the most
     triggers a second halves, from about 1,378 to about 689.
   - But `grn` has two voices, and a steal waits out a 128-frame fade
     (4–5 blocks). So at those settings the cloud is already limited by
     steals, and the audible density changes less than that.
4. **Grain ends** land on a two-block boundary: up to 1.4 ms shorter
   instead of up to 0.7 ms (the fades unchanged).
5. **A deferred trigger** after a steal starts up to 0.7 ms later.

**In short:** at most a slightly sparser cloud at the extreme random-
density settings; nothing elsewhere.

**Saving:** only the decision and the candidate move, not the run state,
so about half of the 846 a block. That is **≈ −0.2 to −0.4k**, not
revision 3's −0.6k.

**Status:** with each type paired with its own service, `grn` (12.9k) is
never the worst type unless both B4 and E7 are taken. **E4 is not
recommended for now** (reserve).

#### The Effect budget (revision 4)

| With | Worst | Effect + its service | vs today (18.1k) |
|---|---|---:|---:|
| Today | Delay + feed | 18.1k | — |
| **A3 + E1 + E2 + E5 (no sound change)** | **Delay + feed** | **≈ 15.0k** | **−3.1k (−17 %)** |
| the same + E7 (the feed as a transient) | Delay | ≈ 14.1k | −4.0k |
| the same + B4 + E6 | Reverb + feed | ≈ 13.2k | −4.9k |

**Recommendation:**
- **Set the Effect budget to 15.0k** (the Effect section plus its
  `effects_service()`), reached with no sound change.
- The Delay keeps its full band.
- B4 is the reserve if the device run of §A.9 shows the Scenes still over.

### A.7 The worst cases under the 144k cap (revision 4)

All figures in thousands of cycles a block. They are estimates from the
measured costs of §A.2–§A.6b, at the mixed-Scene cycles per instruction,
unless marked. Cap **144k (91.9 %)**.

**What changed from revision 3:**
1. **B3:** −0.6k per Tetrakt, not −2.1k (§A.4).
2. **The Effect:** each type with its own `effects_service()`. The worst is
   the Delay during a feed, **15.0k** with A3, E1, E2 and E5 (§A.6b).
   Revision 3 used 12.2k for the Effect plus 2.0k of service in the shared
   sections, which assumed B4.
3. **CL3** −0.2k (was −0.3k); **CL4** counted at −0.25k per Colloid-engine
   voice (to measure).

#### The common items

| Item | Effect on the worst case |
|---|---|
| A1 (staggered slot control) | control 7.0 → 4.0 |
| Set-up + SRR | 3.5 (unchanged) |
| A5 (send-add constants) | send adds 10.8 → 10.0 |
| **Shared sections (revision 4: without `effects_service()`)** | **17.5** |
| Compressor | **4.6** (on; B5 would make it 3.2) |
| **Common** | **22.1** |
| Effect with its service, A3 + E1 + E2 + E5 | **15.0** (the Delay during a feed) |
| Colloid: CL1 (in thirds), CL2, CL3, CL5, CL4 | 19.4 → **17.65** |
| Advanced Colloid: the same + CH1 | 21.0 → **18.85** |

**The Tetrakt, by item set** (from 20.4k):

| Set | Items | Tetrakt |
|---|---|---:|
| **T-a** (accepted) | A2 + B1 (sub-block) + B3 | **18.35** |
| T-b | A2q + B1 (sub-block) + B3 | 17.82 |
| T-c | A2 + B1 (sub-block) + B2 + B3 | 17.65 |
| **T-d** | A2q + B1 (sub-block) + B2 + B3 | **17.12** |

With B1 in its full per-block form instead of the sub-block form, each set
is 0.18k lower.

#### The packages

| Package | Contents | Decisions in it |
|---|---|---|
| **P** | Every accepted or bit-exact item: A1, A3, A5; E1, E2, E5; A2, B1 (sub-block), B3 (T-a); CL1 (thirds), CL2, CL3, CL4, CL5; CH1 | none beyond those already accepted (CH1 after its listening A/B) |
| **Q** | P with the Tetrakt at T-d (A2q, B2) | A2q (S1–S2); **B2: the four `tkt_srr_a..d` rows become one `tkt_srr`** (a feature change) |
| **R** | Q + B4 + E6 | B4 (the Delay's wet to 11 kHz, §A.4) |
| R′ | P + A2q + B4 + E6 (no Tetrakt feature change) | A2q, B4 |

#### The Scenes

| Scene | P | Q | R | R′ |
|---|---:|---:|---:|---:|
| **1:** 4 Tetrakts + 2 Advanced Colloids | 148.2 (−4.2) | 143.3 (+0.7) | **141.5 (+2.5)** | 144.3 (−0.3) |
| **3:** 3 Colloids + 1 Tetrakt + 2 Advanced Colloids | 146.1 (−2.1) | 144.9 (−0.9) | **143.1 (+0.9)** | 143.8 (+0.2) |
| **2:** 4 Colloids + 2 Advanced Colloids, mixed-Scene CPI | 145.4 (−1.4) | 145.4 (−1.4) | **143.6 (+0.4)** | 143.6 (+0.4) |
| 2, one-engine CPI (−7 % on the voices, §A.7 revision 3) | 137.8 (+6.2) | 137.8 (+6.2) | 136.0 (+8.0) | 136.0 (+8.0) |
| **Bank 076 Scene 5** (measured 148.3–148.8k today, logging build) | ≈ 135.8 (+8.2) | ≈ 134.6 | ≈ 130.9 | ≈ 131.6 |

The arithmetic:
- **Scene 1** = 4 × Tetrakt + 2 × 18.85 + 22.1 + Effect. For example, Q:
  4 × 17.12 + 37.7 + 22.1 + 15.0 = 143.3.
- **Scene 3** = 3 × 17.65 + Tetrakt + 37.7 + 22.1 + Effect.
- **Scene 2** = 4 × 17.65 + 37.7 + 22.1 + Effect.
- **Scene 5** = the measured sections minus each item's saving:
  - control −3.0;
  - service −1.55;
  - send adds −0.8;
  - Effect −1.6;
  - `tkt` −2.05 (T-a);
  - `clx` −2.1;
  - `col` −1.7.

  That is −12.7k with P. Q adds the Tetrakt's T-d (−1.2k); R and R′ add
  B4 on the Delay (−3.7k; Scene 5 plays the Delay, with no feed while it
  plays). Release builds run about 1.5k lower (no S1).

**What the table says:**
1. **No package without a sound or feature decision fits every Scene.**
   With P, the four-Tetrakt Scene is about 4k over. That comes from the B3
   correction, not from the Effect.
2. **The smallest set that fits every Scene at the mixed-Scene CPI is R:**
   - the Tetrakt at T-d (A2q and B2);
   - B4 on the Delay and Echo;
   - with E6.
3. **Without B4 (Q):**
   - the four-Tetrakt Scene fits (+0.7);
   - Scenes 2 and 3 are 0.9–1.4k over at the mixed-Scene CPI.

   That CPI (1.51) comes from a Scene with six different engines. Scenes 2
   and 3 run one and two engines, and the six-Colloid Scene measured 7 %
   less (§A.7 revision 3, S095 P5). At even half that gain, Scenes 2 and 3
   fit under Q. **So Q may well fit on the device without B4. The test
   Scenes of §A.9 decide.**
4. **R′ (no Tetrakt feature change, but B4)** sits at the cap in Scene 1
   (−0.3).
5. **Scene 5**, your real worst Scene, drops from 94.7–95.0 % to about
   86.7 % with P alone. The widget would read about 89–90 %.

**Recommendation:**
1. **Build P first and measure** test Scenes 1–3 and Scene 5 (§A.9). P
   needs no new sound decision: everything in it is accepted or
   bit-exact. The device then gives the true Tetrakt and Colloid figures,
   including the half-rate items' real savings and the CPI of
   one- and two-engine Scenes.
2. **Then close what is left, in this order of least change:**
   1. A2q (Tetrakt envelope segments 2.9 ms; S1–S2);
   2. B2 (one Tetrakt SRR; a feature change);
   3. E7 (count the feed as a transient);
   4. B4 (the Delay's wet band);
   5. B5 (the compressor).

   **My expectation: Q (A2q + B2) is enough, and the Effect keeps its full
   band.**

### A.8 Decisions

| Q | Question | Recommendation |
|---|---|---|
| Q-C1 | The cap: worst-case mixer average ≤ 144k, checked on the device | **decided** (revision 3) |
| Q-C2 | Tier A: A1, A3, A5 (A6 optional: §A.3) | yes; listen to A1 on a fast pitch LFO |
| Q-C3 | Tetrakt: A2 + B1 (sub-block) + B3 | **accepted** (revision 4). B3 is now −0.6k, not −2.1k |
| Q-C3b | Tetrakt, if the device run needs it: A2q (one layer's control a block) and B2 (one `tkt_srr` row instead of four) | after the P run (§A.7); A2q first |
| Q-C4 | Colloid: CL1 (in thirds), CL2, CL3, CL5; CL4 | CL1–CL3 accepted (revision 3); CL1 in thirds, CL5 and CL4 recommended (CL4 has no sound change) |
| Q-C5 | Advanced Colloid chorus: CH1 (quarter-rate rings)? | yes, after a listening A/B (wet band to 5.5 kHz) |
| Q-C6 | Effect: B4 (Delay and Echo tapes at half rate, §A.4)? And if yes: keep today's "slowing tape" at long delays (½ the speed), or let long delays run faster and brighter? | **reserve:** only if the device run of P + Q leaves a Scene over 144k |
| Q-E1 | Effect plumbing: E1 (return ramps) and E2 (a float FX bus)? | **accepted** (revision 4) |
| Q-E2 | E3: the Reverb plate at a quarter rate | **withdrawn** (your answer, revision 4) |
| Q-E3 | E4: CrumpBit's grain plan on alternate blocks? | reserve; never needed unless B4 and E7 are both taken |
| Q-E4 | The Effect budget: 15.0k (the section plus its `effects_service()`), down from 18.1k, with no sound change | yes |
| Q-E5 | E5 (the Delay loop in two passes, bit-exact): approve **512 B of stack** in the Effect process (or 256 B for two 16-frame halves)? | yes, after a stack headroom check |
| Q-E6 | E6 (a Reverb-only plate loop, bit-exact, +0.9 KB flash)? | only with B4 |
| Q-E7 | The feed (≤ 6 s after a type change, 0.94k): count it in the worst case, or treat it as a bounded transient? | count it (today's rule) unless a Scene needs exactly this 0.9k |
| Q-C7 | Compressor: B5? | yours; the last reserve |
| Q-C8 | Build the worst-case test Scenes first and measure them with the S1 probe? | **yes, first** (§A.9) |
| Q-C9 | The S1 maxima cost about 1.5k a block in logging builds (§A.1b). Put them behind a sub-flag (`MIXER_PROBE_MAX`, off by default, on for an investigation), so that the logging build you run reads like a release build? | yes. It is diagnostic time in the mixer, which the trace rule says diagnostics must not take. It needs no RAM |
| Q-F1 | `FS_OS=1` | **decided: stays** (revision 4, §A.1b) |
| — | C3 / C5 (type caps) | **withdrawn** (revision 3) |

### A.9 How each step is proven

1. **First, three test Scenes on today's image** (S1 maxima, outside-mixer
   span, widget), each with the Delay at every row active, the compressor on
   and every send above 0:
   1. four Tetrakts (four layers, full drive and SRR, the space on, long
      samples tuned up: the most flash traffic) + two Advanced Colloids (`pwm`
      stereo, the chorus L-R);
   2. four Colloids (`wts` and `pwmw` stereo, two each) + two Advanced
      Colloids;
   3. three Colloids + one Tetrakt + two Advanced Colloids;
   4. the Effect users: Scene 3 with the Effect switched through Delay,
      Echo, Reverb and each CrumpBit sub-type, including a type change while
      playing (the feed). That measures the per-type Effect and
      `effects_service()` cost of §A.6b.

   Each run measures today's figure and the CPI of a one- and two-engine
   Scene. **Bank 076 Scene 5** is the fifth check: real material,
   148.3–148.8k today (§A.1b).
2. **Each step on its own,** with:
   - its host law and a break proof (a law counts only if the old code
     fails it);
   - `elf_p4_check` / `elf_s099_check` against the step before;
   - the S093 per-block cost method (loops plus per block ÷ 32, VDIV
     counted);
   - **revision 4:** for a half-rate item (B3, CH1, CL3, and B4 if taken),
     the linked-loop figure is checked against this report's estimate
     before the next step. D1 and B3 both showed that a reading of the
     source can be two to three times too high;
   - the test Scenes on the device.
3. **The order (package P first):**
   1. Q-C9 (the S1 maxima behind a sub-flag), so that every later device
      figure reads release-like;
   2. Tier A (A1, A3, A5) and the Effect plumbing (E1, E2);
   3. E5 (bit-exact, after the stack sign-off);
   4. the Colloid items (CL1 in thirds, CL2, CL3, CL4, CL5), then CH1 with
      its listening check;
   5. the Tetrakt items (A2, B1 sub-block, B3);
   6. **the device run of package P** (the five Scenes);
   7. only what that run shows is needed: A2q, then B2, then E7, B4 (+ E6),
      B5.

---

## Part B — Flash

### B.1 Where the flash goes (before S101 P2's builds)

| Build | Flash used | Headroom |
|---|---:|---:|
| Logging, the S101 D1/S1 image | 733,548 B | 20,116 B |
| Release (`DEV_MODE_LOGGING 0`), the same tree | 714,168 B | 39,496 B |

The largest groups were Filesystem/SD 146,880 B, Menu 93,938 B, the three
stock wave tables 67,584 B, Alkaline 48,136 B and Transients 26,780 B
(revision 1's table).

### B.2 Cymbal + HiHat merge (F2)

See `DRAFT_HIHAT_CYMBAL_MERGE.md`. Revision 1's finding stands: the voices
differ in carrier gain, fold gains, modulator reset and the note-off, beyond
`repeat` and the open decay. The draft proposes one `chr cym/hat` row so
every existing sound converts exactly. About −5 to −7 KB (estimate).

### B.3 Built in S101 P2 (log: `S101_IMPLEMENTATION.md`)

| ID | Change | Saving (measured) | Sound / feature |
|---|---|---:|---|
| **F1** | `FS_OS=1` by default (`filesystem.c` at `-Os`, the TIM3/MIDI-reachable helpers at `-O2`) | −36,896 B (on its own) | none. Device-tested against `FS_OS=0` in revision 4 (§A.1b): no difference; stays |
| **F3** | One `stockTableRows[31][1024]` array: triTable levels 0..10, recTable and sawTable levels 1..10 (their level 0 served only fundamentals below 16.35 Hz) | −4,096 B | saw/rec below 16.35 Hz read level 1 (edge ripple only); the triangle and the Alkaline square unchanged |
| **F4** | The 12 built-in transients (`transientData`, 26,460 B, and their click loop) leave the firmware; the card carries them as `SD_CARD/samples/LXRClk.wav` … `LXRSp2.wav`, played by the existing one-shot sample path | −26,460 B and the loop | see the implementation log: no migration; three rows play 0.9–2.3 dB louder (normalised at install) |
| **Total** | | **733,548 → 666,700 B (−66,848 B); ITCM 15,432 → 15,424 B** | |

### B.4 Remaining flash options

| ID | Change | Saving | Notes |
|---|---|---:|---|
| F2 | Cymbal + HiHat merge | about −5 to −7 KB | the draft's decisions |
| F3b | The triangle's levels 1–2 onto level 3 (they differ by 57 / 114 LSB) | −4 KB | needs a triangle-only clamp at every triangle read site, including the pwm triangle's lift (S101 found the lift sensitive: CO-PWG) |
| F5 | The Alkaline square onto `recTable` | −22,528 B | changes the SPICE-fitted Alkaline square: not recommended |
| F6 | `-Os` for the Menu and button-handler objects (`COLD_LAST` method) | not measured; −10 to −20 % of about 114 KB is plausible | foreground speed matters at a 93 % load: measure the `f` pass times too |
| F7 | Logging-build trim (19,380 B is logging) | release builds already have it | sub-flags per probe family for logging builds |
