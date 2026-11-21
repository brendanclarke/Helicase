# S100 P1 — Background Noise Investigation

**Date:** 2026-11-20. **Branch:** `dev-ph7-alkaline` at `c930dcf5`.
**Scope:** §1–§6 are from code reading. §7 adds host measurements: the
current Delay/Echo and two candidate fixes were built and measured in a
scratch worktree, which has since been removed. The working tree is
unchanged. §6 lists the hardware tests; §7 is the fix design, waiting for a
decision.

---

## 0. The question

With all voices muted and nothing sent to the FX bus, the main output plays a
loud, constant hiss. The hiss stops while firmware is updating or samples are
loading. It seems to have appeared during the mixer-level and FX-bus work
(S095 P1–P4). The compressor makes it louder from about `cam` 70, which is
expected; the base hiss is the problem. Voice maths is mostly float and the
mixer is 24-bit, so where does a constant noise come from?

## 1. Short answer

1. **The firmware path is exact digital zero when the voices are idle,
   except for the Effect.** Every voice's amplitude stage ends at exactly 0.0,
   and the mixer, the sends and the compressor map 0 to 0 (§3). Muting stops
   triggers, not renders, and an idle render is silence.
2. **The Effect is the one stage in the firmware that makes noise without
   input.** The Space **Delay** and **Echo** inject a tape hiss into their
   feedback loop whenever `deg` > 0 and `fbk` > 0. The hiss is added inside
   the Effect, so a zero send does not stop it. `deg` also raises the loop's
   small-signal gain by up to 2×, so the loop amplifies its own hiss, and
   from `fbk` row 50 at `deg` 127 the loop reaches unity and sustains itself
   (§4). Because the hiss keeps the Effect above −90 dBFS, the
   stopped-transport idle never mutes the return either. This arrived in
   S095 P1, in the FX-bus work, which fits the timing.
3. **"Silent while audio is off" does not prove the hiss is digital.**
   `audioCodec_suspend()` stops both I2S peripherals and PLLI2S, so MCLK
   stops. Without MCLK the CS4344 DACs stop converting and their analog
   output idles. Anything that exists only while the DACs are clocked goes
   silent with the suspend: the DAC's own noise floor, MCLK/SCLK coupling
   into the analog stage, and digital noise. The observation cannot tell
   these apart (§2).
4. **The `cam` observation is a useful clue.** With nothing playing, the
   compressor's gain is its makeup (+3.3 dB at `cam` 70, +12.2 dB at
   `cam` 127). If the steady hiss rises with `cam`, it is in the data before
   the compressor, which points to the Effect. If it does not change, it
   enters after the digital path (DAC or analog). This needs the test in §6
   T2 to be sure.

**Most likely cause:** a Delay or Echo with `deg` above 0 on the active
Scene. **Next most likely:** an analog or clock-coupled floor that has always
been there and is now easier to hear. §6 separates the two in a few minutes
at the panel.

**Host measurement (§7.4):** with zero input from power-on, today's Delay
settles at −71 dBFS RMS (`deg` 64, `fbk` 50) and −62 dBFS RMS (`deg` 127,
`fbk` 40). At `deg` 127, `fbk` 60 it oscillates on its own at −19 dBFS. With
`deg` 0 it is exact zero. The Echo is the same within 4 dB.

**Fix (§7):** both candidate degradations below give exact zero with no
input and exact zero once a tail has died, and both pass the CPU caps. The
recommendation is **B** (the hiss scaled by an envelope of the tape level)
together with **D2** (`deg` no longer raises the loop gain).

---

## 2. What the observations prove

### 2.1 Audio off = clocks off

[AudioCodecManager.c:710](Core/Hardware/AudioCodecManager.c#L710)
`audioCodec_suspend()` calls `audioCodec_stopHardware(1)` (clears I2SE on
SPI2 and SPI3, disables both DMA streams, resets SPI2/SPI3/DMA1) and then
`plli2s_stop()`. Firmware update runs the bootloader, which never starts
I2S. So in both of the "quiet" states:

- MCLK (256 × Fs ≈ 11.3 MHz), SCLK and LRCK are all stopped on both DACs;
- the CS4344s are unclocked and stop converting. The data sheet describes a
  power-down state when MCLK is removed. The data sheet is not in the tree;
  confirm the exact behaviour there;
- the SPI2/SPI3 DMA traffic and the render loop also stop.

So "it's not heard while audio is off" rules out only noise sources that do
not depend on the audio clocks or the DSP (supply hum, a noisy analog stage
with nothing to do with the DAC). It does **not** separate:

| Source | Present while clocked | Present while suspended |
|---|---|---|
| Digital noise in the sample stream (firmware) | yes | no |
| CS4344 output noise floor | yes | no (DAC unclocked) |
| MCLK/SCLK edges coupling into the output stage or ground | yes | no |
| Analog stage noise unrelated to the DAC | yes | yes |

### 2.2 The compressor test

[BusCompressor.c:325-330](Core/DSPAudio/BusCompressor.c#L325-L330): with the
bus silent, the detector reads about −240 dBFS, gain reduction is 0, and the
applied gain is the static makeup `M = −GR(−20 dBFS)·(1 − 0.3a)`:

| `cam` | 0 | 48 | 64 | 70 | 96 | 127 |
|---|---:|---:|---:|---:|---:|---:|
| Idle gain (makeup) | 0 dB | +0.8 | +2.4 | +3.3 | +7.5 | +12.2 |

The makeup curve is steepest between 64 and 96, which matches the "about 70"
the user hears. The compressor runs on one pair only (`cmp St1` = `output2`
= DAC1/MAIN, `St2` = `output` = DAC2/OUT2). It amplifies only what is already
in that pair's data, so:

- hiss rises with `cam` → the hiss is digital and comes before the
  compressor;
- hiss does not change with `cam` → the hiss enters after the sample stream
  (DAC, clocks, analog).

If the hiss the user heard rising was only while playing, this test has not
been done yet; it is T2 below.

---

## 3. What the firmware outputs with every voice idle

Checked in the code on this branch:

| Stage | Idle output | Where |
|---|---|---|
| Drum (and the drum family's post-chain) | Amp EG `slopeEg2_calc()` reaches exactly 0 in `EG_D`; distortion maps 0 → 0. The old LXR RNG dither (`bufferTool_multiplyWithFloatBufferDithered`) is compiled only under `USE_AMP_FILTER`, which is not defined. | [DrumVoice.c:928-971](Core/DSP/Instruments/Drum/DrumVoice.c#L928-L971), [SlopeEg2.c](Core/DSPAudio/SlopeEg2.c) |
| Snare, HiHat, Cymbal | Same EG; `voicePost_mixAddGainDist()` / `voicePost_addGainDist()` multiply the oscillator *and* the transient/noise sum by the amp gain, so the noise oscillator is gated too. | [voicePostChain.h:74-119](Core/DSPAudio/voicePostChain.h#L74-L119) |
| Colloid | `col_egStep()` release ends at exactly 0 below 1e-4; the S096 P3 noise is inside the amp-gated stack. | [ColloidVoice.c:443-500](Core/DSP/Instruments/Colloid/ColloidVoice.c#L443-L500) |
| Alkaline | OTA current decays below 1e-7 (`Alkaline_isQuiet()`); 1e-7 × full scale is far below one int16 step, so the output rounds to 0. | [AlkalineVoice.c:798](Core/DSP/Instruments/Alkaline/AlkalineVoice.c#L798) |
| BBD chorus (S099) | Integer, no noise source. The `>> 16` one-poles can hold a −1 LSB DC residue (constant, not hiss). | [BbdChorus.c](Core/DSP/Instruments/Chorus/BbdChorus.c) |
| Tetrakt | Layers go to `TKT_STAGE_IDLE`. Its sample tail was not traced in detail. | [TetraktVoice.c:291](Core/DSP/Instruments/Tetrakt/TetraktVoice.c#L291) |
| Mixer dry and send | `data · gain · 256` straight to `sample_mx_t` (S088 P1; no int16 step); 0 in → 0 out. | [mixer.c:437-470](Core/DSPAudio/mixer.c#L437-L470) |
| Effect | **Not silent:** see §4. | |
| Bus compressor | Multiplies whatever is there by the makeup (§2.2); 0 stays 0. | [BusCompressor.c](Core/DSPAudio/BusCompressor.c) |
| Pack | Clamp to signed 24 bit and rotate; 0 → 0. | [AudioCodecManager.c:384-430](Core/Hardware/AudioCodecManager.c#L384-L430) |

**The "24-bit mixer" point.** The mixer itself is 24-bit: since S088 P1 the
dry path converts `data · gain · 256` directly, with no int16 truncation. The
precision limits are earlier and later: every voice renders int16, and the
Space tape and plate rings and the chorus ring store int16. None of these
makes noise without a signal; they set a quantisation floor around −96 dBFS
under a signal. They cannot explain an idle hiss.

---

## 4. The Space Delay/Echo hiss

### 4.1 Mechanism

[SpaceEffect.c:1227-1235](Core/DSP/Effects/Space/SpaceEffect.c#L1227-L1235)
(`SPACE_LAW_DEG`, g = `deg`/127):

```
drive  = 1 + 5g
makeup = 1 / (1 + 2g)
hiss   = (deg == 0) ? 0 : 2^((−72 + 24g)/6.02) / 2^31     (× a 32-bit LCG word)
```

[SpaceEffect.c:355-371](Core/DSP/Effects/Space/SpaceEffect.c#L355-L371)
(`space_loop()`, once per frame per channel):

```
y   = clip1((play − hp) · drive)
y   = y · (1 − y²/3) · makeup + nz            ← hiss added here, every frame
rec += rk · (in · ing + fbk · y − rec)         ← written to the tape
```

- The hiss is a uniform white noise with peak −72 dBFS at `deg` 1 and
  −48 dBFS at `deg` 127 (RMS 4.8 dB lower), relative to int16 full scale (the
  bus's 1.0). It is generated by an LCG
  ([SpaceEffect.c:508](Core/DSP/Effects/Space/SpaceEffect.c#L508), Echo
  [:586](Core/DSP/Effects/Space/SpaceEffect.c#L586)), not by the input.
- It enters the record through `fbk · y`: with `fbk` 0 nothing reaches the
  tape; with any `fbk` > 0 it is recorded, played back by the heads (the wet
  is the clean head read) and recirculated. The send amount plays no part.
- **Loop gain.** For small signals the clip is linear (slope 1 at 0), so the
  loop gain at low frequency is `fbk × drive × makeup = fbk × (1 + 5g)/(1 + 2g)`.
  The makeup only partly offsets the drive, so `deg` raises the small-signal
  loop gain:

  | `deg` | 0 | 16 | 32 | 64 | 96 | 127 |
  |---|---:|---:|---:|---:|---:|---:|
  | drive × makeup | 1.00 | 1.30 | 1.50 | 1.75 | 1.90 | 2.00 |
  | Hiss peak (dBFS) | none | −69.0 | −66.0 | −59.9 | −53.9 | −48.0 |
  | `fbk` row where the loop reaches unity | 100 | 77 | 67 | 57 | 53 | 50 |

  `fbk` is linear to 1.0 at row 100 (`space_fbkGain()`,
  [SpaceEffect.c:208](Core/DSP/Effects/Space/SpaceEffect.c#L208)). So with
  `deg` set, the loop reaches unity at much lower `fbk` rows than the page
  suggests.
- **Below unity** the recirculation raises the noise by about
  `1 / (1 − G²)` in power (a single comb with flat loop gain G; the playback
  and record low-passes make high frequencies lower). Measured (§7.4): `deg`
  64, `fbk` 50 (G ≈ 0.88) idles at −71 dBFS RMS at the Effect output, and
  `deg` 127, `fbk` 40 at −62 dBFS RMS. Both figures are before the return
  level and before the compressor makeup (+3.3 dB at `cam` 70). The CS4344's
  own floor is around −98 dB, so this hiss is 25–35 dB above the DAC floor,
  which fits "extraordinarily high".
- **At or above unity** the hiss alone starts the loop, which grows until the
  clip holds it. Measured at `deg` 127, `fbk` 60 with zero input from
  power-on: −19 dBFS RMS (Echo −23). That would sound like a sustained wash
  or roar, not a quiet hiss.
- **Character:** low-passed (`bw` 16 kHz falling 2 octaves with `deg`) and
  comb-coloured at the delay time. At short `tim` it can sound pitched. A DAC
  floor is flat and white.

### 4.2 Why the idle mute does not catch it

[EffectsManager.c:2431-2443](Core/DSP/Effects/EffectsManager.c#L2431-L2443)
and [SpaceKit.h:110](Core/DSP/Effects/Space/SpaceKit.h#L110): with the
transport **stopped**, the return ramps to 0 over 1 s only after the Effect's
input and output stay below −90 dBFS (`SK_IDLE_QUIET`) for 14 blocks. The
self-made hiss keeps the output above −90 dBFS, so the idle never engages.
With the transport **running** the Effect always processes. In both cases the
hiss stays on.

### 4.3 Other Effect types

- **CrumpBit:** silence is code 128, which decodes to exactly 0.0, and the AC
  coupling has no noise source. With feedback the line decays "to the 8-bit
  line's rounding": a held or toggling code is one 8-bit step, 1/128 of full
  scale (−42 dBFS). The host law CB8 allows the mean code to sit within
  ±8 LSB of silence during self-oscillation. Not traced further; T1 covers it.
- **Reverb:** no LCG or RNG in its signal path (the only other LCG,
  [SpaceEffect.c:1614](Core/DSP/Effects/Space/SpaceEffect.c#L1614), drives
  the `wow` random walk). Its int16 rings with zero input should decay to
  zero. Not proven; T1 covers it.

---

## 5. Ranked hypotheses

| # | Hypothesis | For | Against |
|---|---|---|---|
| H1 | Space Delay/Echo `deg` hiss, amplified by the `deg` loop gain, on the active Scene | Only continuous digital source in the code; send-independent; not idled; arrived with the FX-bus work (S095 P1); rises with `cam` | Needs `deg` > 0 and `fbk` > 0 on the Scene in use |
| H2 | DAC/analog floor or MCLK/SCLK coupling, present whenever the DACs are clocked | Fits "silent while suspended" just as well (§2.1) | Does not explain "crept in"; would not rise with `cam` |
| H3 | CrumpBit or Reverb residue | Possible rounding residue or limit cycle | No noise source found in the code |
| H4 | A voice not reaching zero | — | Every voice's amp stage was checked to end at 0 (§3) |

H2 needs one more remark. If the S095 P4 retune (`−8 − 32a` threshold, −20 dBFS
makeup reference) or the Colloid/Tetrakt gain staging left typical program
levels lower than before, the monitor gain would go up and an old floor would
become more noticeable. That is a listening-level effect, not a new source.

---

## 6. Tests (device and ear)

Run on the Scene where the hiss is heard, transport stopped, all tracks muted.
Each test answers one question.

- **T1, Effect off.** Set the Scene's Effect type to `OFF` and wait for the
  drain (at most 6 s). Set `cmp` off.
  - Hiss gone → the Effect (H1/H3). Restore the type and set `deg` 0: if the
    hiss goes, it is H1. Note the `deg`, `fbk`, `tim` and return `vol`
    values.
  - Hiss stays → not the firmware's Effect. Go to T3.
- **T2, `cam` sweep at idle.** Effect back as it was, `cmp St1`, sweep `cam`
  0 → 127, listening on MAIN.
  - Hiss rises about 12 dB over the sweep → digital, before the compressor.
  - Hiss unchanged → after the sample stream.
  - Repeat with `cmp St2` while listening on MAIN: MAIN should no longer
    change. This confirms which pair the noise rides on.
- **T3, digital zero with clocks running.** This needs a throwaway build:
  clear both 32-frame output blocks at the end of
  `mixer_calcNextSampleBlock()` (after `busComp_processBlock()`), so the DACs
  receive exact zeros with MCLK running. Do not keep this build.
  - Hiss still there → H2 (DAC floor, clock coupling, supply). Not a
    firmware DSP issue.
  - Hiss gone → something in §3 is not silent on the device; take an output
    recording.
- **T4, spectrum.** Record MAIN into an audio interface with the hiss on.
  - A low-passed, comb-rippled spectrum that follows `tim` → H1.
  - Flat white → DAC/analog floor.
  - Discrete tones (MCLK, SCLK, LRCK sub-harmonics, the 1,378 Hz block
    rate, the 459 Hz DMA half rate) → clock or render-activity coupling.
- **T5 (optional), older image.** Flash an S094 image (before the Space
  types) and compare the idle floor on the same Scene with Effect `OFF`.
  - Use a spare SD card: that firmware's AutoSave will write to the card,
    and newer files are not readable by older images (no backward
    compatibility).
  - Sample flash catalogue v3 dates from S093, so installed samples stay
    valid.
  - If the floor is the same, H2 has always been there.

---

## 7. Degradation without idle hiss (design, for decision)

Nothing below is in the tree. Each option was built from `c930dcf5` in a
scratch worktree, linked with `make all`, gated with `space_cpu.py`, run
through the `space` host laws, and measured with a scratch idle program on
the host build. The worktree has been removed.

### 7.1 Requirements

1. With no input, the Delay and Echo output **exact zero**: no hiss from an
   empty tape.
2. After a tail has died, the output returns to exact zero. The
   stopped-transport idle (§4.2) can then engage again.
3. While repeats are sounding, `deg` keeps its character: drive, bandwidth
   loss, compression and a noise.
4. **No per-frame cost increase** (CPU ceiling, `MEMORY.md`). The measured
   headroom at `c930dcf5` is tiny: Delay 251.6 of cap 252.0, Echo 210.0 of
   210.5 instructions per frame.
5. No new RAM allocation.

### 7.2 Option A: modulation noise (a different way)

The noise multiplies the signal instead of adding to it. One line in
`space_loop()`:

```c
/* today */  y = y * (1.0f - (1.0f / 3.0f) * y * y) * makeup + nz;
/* A     */  y = y * (1.0f - (1.0f / 3.0f) * y * y) * (makeup + nz);
```

- **Silence:** `y` = 0 makes the noise term 0 exactly, sample by sample. No
  state, no threshold.
- **Character:** this is tape *modulation noise*: a gain jitter of relative
  size `nz/makeup`. Under broadband repeats it sounds like hiss riding on
  the signal. Under low, sparse repeats (a kick tail) it follows the
  waveform, is zero at every zero crossing, and pulses at twice the
  fundamental: closer to grit or fizz than to a smooth hiss bed.
- **Level:** with today's law unchanged, the jitter peak relative to the
  signal is `−72 + 24g − 20·log10(makeup)` dB: −72 dB at `deg` 1, −38.5 dB
  at `deg` 127 (makeup 1/3). A ratio law is a tuning choice (§7.6).
- **Cost (measured):** Delay **250.6** (−1.0 per frame: the add folds into
  the existing multiply), Echo **210.0** (unchanged). Feed totals 270.6 and
  229.9.
- **RAM:** none.
- **Host laws:** `space` 24 ok, 0 failures (baseline 24 ok).

### 7.3 Option B: an envelope on the hiss (as asked)

The hiss stays additive, but its amplitude is scaled once per block by an
envelope of the tape's playback level:

```c
/* space_delayRun(), per block: the O8 reads, kept in locals */
const float al0  = fabsf(h->ch[0].play);
const float ar0  = fabsf(h->ch[1].play);
const float bwl  = ps->bw * (1.0f - h->k.comp * al0);
const float bwr  = ps->bw * (1.0f - h->k.comp * ar0);
const float henv = __builtin_fmaxf(al0 + ar0, h->hiss_env * 0.9856f);
const float hiss = h->k.hiss * henv;            /* was: h->k.hiss */
...                                             /* frame loop unchanged */
h->hiss_env = henv;

/* space_echoTapeRun(): the same with a0 = fabsf(h->ch[0].play) alone */
/* space_hdr_t: float hiss_env; after rnd */
```

- **Envelope source:** the playback one-pole state `ch[c].play` at the block
  start. This is the level the tape feeds back. The O8 tape compression
  already reads it once per block, so no new read is added. Attack is
  instant (`fmaxf`); release is ×0.9856 per block, τ ≈ 50 ms.
- **Why proportional, not a gate.** A fixed hiss opened by a level gate
  would keep itself open: the recirculated hiss is part of the tape level
  the gate reads. With the hiss proportional to the envelope, each pass adds
  noise of relative size k (≤ −30 dB), so the loop's power gain rises only
  by `(1 + k²)` (≤ 0.1 %). The tape still decays, the hiss decays with it,
  and an empty tape (int16 zeros) gives an envelope of 0 and a hiss of 0
  exactly.
- **Character:** a conventional hiss bed under the repeats. It follows the
  tail down over about 50 ms and is gone when the tape is empty. There is no
  zero-crossing modulation as in A. The envelope is one sample per block of
  a low-passed state, so high-frequency repeats are under-read; the peak
  hold smooths this. Listen for it (§7.7).
- **Level:** with today's law unchanged, the hiss peak is
  `−72 + 24g` dB relative to the envelope. The Delay's envelope is
  |L| + |R|, about 6 dB above one channel; the Echo's is the mono level. To
  make them match, scale the Delay's sum by 0.5 (one multiply per block).
- **Cost (measured):** Delay **251.9** (+0.3 per frame from the per-block
  lines over 32 frames, leaving 0.1 under the cap), Echo **208.2** (−1.8:
  the loop shortened by 2 under the new register allocation). Feed totals
  271.8 and 228.1. The Delay's remaining 0.1 is the weak point of this
  option.
- **RAM:** +4 B inside the existing Space header (`space_hdr_t` 392 → 396 B
  of the fixed 448 B `SK_STATE_BYTES`; sizes taken from the compiler). The
  arena prefix does not grow and nothing new is allocated, but per the RAM
  rule this still needs sign-off: 4 B, FX arena prefix (DTCM), owner
  `SpaceEffect.c`.
- **Host laws:** `space` 0 failures; the break builds are still caught.

### 7.4 Measured idle output (host, Effect output before the return)

Default rows otherwise. Each case runs 11 s: "silent" is zero input from
`init`; "burst" is 0.5 s of uniform noise (peak −12 dBFS) followed by zero
input. RMS is over the last second. "Zero from" is the time the output
became exact 0.0 and stayed there.

**Delay**

| `fbk` | `deg` | Today, silent | Today, burst | A, burst | B, burst | B + D2, burst |
|---:|---:|---|---|---|---|---|
| 50 | 0 | 0 | zero from 2.64 s | zero from 2.64 s | zero from 2.64 s | zero from 2.64 s |
| 50 | 64 | **−71.0 dBFS** | −71.0, never 0 | zero from 9.41 s | zero from 9.54 s | zero from 2.64 s |
| 40 | 127 | **−62.2** | −62.2, never 0 | zero from 5.84 s | zero from 5.85 s | zero from 2.11 s |
| 65 | 32 | **−72.4** | −39.1, never 0 | −39.1, never 0 | −39.1, never 0 | zero from 3.86 s |
| 60 | 127 | **−19.2** (self-oscillating) | −19.4 | −19.4 (sustains) | −19.4 (sustains) | zero from 3.17 s |
| 90 | 127 | — | — | — | — | −112.7, zero from 10.29 s |

A and B are exact zero in every "silent" case, and so are A and B with D2.

**Echo** (today, silent): −75.0 (`fbk` 50 / `deg` 64), −66.1 (40 / 127),
−76.5 (65 / 32), −22.6 (60 / 127). A and B: exact zero in every silent case
and in the same burst cases as the Delay. B + D2 reaches zero in every
burst case within 2.1–3.7 s.

Reading the table:

- A and B both remove the idle hiss (requirement 1) and are
  indistinguishable on this test.
- Neither fixes the cases where `deg` has pushed the loop to unity or
  above. At `fbk` 65 / `deg` 32 the loop gain is about 0.98, and the tail is
  still at −39 dBFS after 10 s. At `fbk` 60 / `deg` 127 it is 1.2, so the
  loop sustains forever, never idles, and becomes a drone from one hit. This
  is the same "caught out" problem in a different form. It is what D2 fixes.

### 7.5 D2: `deg` must not raise the loop gain

Today the small-signal loop gain is `fbk × (1 + 5g)/(1 + 2g)`: up to
2 × `fbk`, so `deg` 127 moves unity from `fbk` row 100 to row 50 (§4.1). The
page shows none of this.

- **Change:** in `SPACE_LAW_DEG`, `k->makeup = 1 / (1 + 5g)` (= 1/drive;
  was `1 / (1 + 2g)`). The small-signal loop gain is then exactly
  `space_fbkGain(fbk)` at every `deg`, so unity stays at row 100 and 1.2 at
  row 127, as the page and the S095 P4 law intend.
- **What it changes in the sound:** `deg` still lowers the clip point
  relative to the signal (drive ×1..×6) and still adds bandwidth loss,
  compression and the noise. Every repeat now loses the factor
  `(1 + 2g)/(1 + 5g)` it used to gain: −2.3 dB at `deg` 16, −4.9 dB at 64,
  −6.0 dB at 127.
  - A clean repeat loses this factor once per pass, so at a given `fbk` the
    tail is shorter than today whenever `deg` > 0.
  - The clipped ceiling becomes `(2/3)/drive`: −19.1 dBFS at `deg` 127
    instead of −13.1 today.
  - Users who set `fbk` by ear with `deg` up will hear shorter tails and may
    need to raise `fbk`; the page value then means what it says.
- **Cost:** law code only (`space_writeParam()`, cold). The measured B + D2
  build gives the same gate totals as B (Delay 251.9, Echo 208.2). `space`
  laws: 0 failures, including SP2's `fbk` 127 / `deg` 127 stability run.
- **Stored data:** `deg` and `fbk` bytes keep their keys and change
  meaning. No migration (standing rule). A saved Scene with high `deg` and
  middle `fbk` that relied on the self-sustain will decay instead.

### 7.6 Recommendation

**B + D2.**

- B is the change you described. It keeps a real hiss bed under the repeats,
  which is what `deg` is meant to sound like, and it is gone when the tape
  is empty.
- D2 makes `fbk` mean the same thing at every `deg`. Without it, a `deg`
  setting still turns a mid `fbk` into an endless drone that blocks the
  idle.
- Choose **A + D2** instead if the Delay's 0.1 instructions of headroom
  under B is unacceptable, or if the modulation-noise character is
  preferred. A frees 1.0 per frame on the Delay.

Laws to set at implementation (tuning values, set by ear):

- B: keep `k->hiss = 2^((−72 + 24g)/6.02) / 2^31` as a ratio to the
  envelope (as measured), or raise it if the bed sounds too quiet under
  normal repeats. Start with the Delay sum × 0.5, so the Delay and Echo
  match.
- A: `k->hiss = makeup × 2^((−K + 24g)/6.02) / 2^31`, with K about 52 for a
  relative S/N of about 57 dB at `deg` 1 and about 33 dB at `deg` 127.

### 7.7 Implementation scope (when approved)

- `Core/DSP/Effects/Space/SpaceEffect.c` only: `space_hdr_t.hiss_env` (B),
  `space_delayRun()` and `space_echoTapeRun()` (B, or the one line in
  `space_loop()` for A), and `SPACE_LAW_DEG` (D2 makeup, hiss law). The
  `memset` in init already clears the new field.
- Host test `tools/dsp_test/modules/space.c`: new law **SP12, idle
  silence**:
  - Delay and Echo at `deg` 127 and 64, `fbk` 40 / 60 / 90, from `init` with
    zero input, give exact 0.0 for 10 s;
  - after a 0.5 s burst, they reach exact 0.0 within a bound (12 s for
    `fbk` 90);
  - break build: today's additive hiss (`* makeup + nz` with `hiss` not
    scaled) must fail SP12.
- Gates: `make all`, `make -C tools/dsp_test space space_cpu_check`
  (Delay ≤ 252.0, Echo ≤ 210.5, feed caps), `link_budget.py`.
- Device and ear:
  - the hiss bed's level and its release under real repeats (B);
  - the clipped-repeat loudness at high `deg` (D2);
  - after a stop, the return idles within about 1 s of the tail ending.

---

## 8. Not verified

- The CS4344's behaviour when MCLK stops (data sheet; not in the tree).
- The `deg`, `fbk` and type values on the Scene where the hiss is heard.
  The card's files were not read; T1 still applies.
- Tetrakt's idle tail, the Reverb's int16 decay to zero, and CrumpBit's
  feedback residue were not simulated. The §7.4 program covers only the
  Delay and Echo.
- The §7.4 burst cases used the default `tim` 64 and `ton` 63; the
  envelope's under-read of high-frequency repeats (B) is an ear item.

## 9. Statements this makes out of date (for the user to act on)

- `MIXER_SYSTEM_DSP_REFERENCE.md` §3.2 "Dry path, per sample" still shows
  `s16 = (int16)(data[i] · g)` then `<< 8`. Since S088 P1 the code is
  `sm = (sample_mx_t)(data[i] · g · 256)` with no int16 step. §6.1 "int16
  truncation on the dry path only" and §2 "every add … is
  `bufferTool_satAdd32()`" (now `__QADD()`, S094 P2 §22) are stale for the
  same reason.
- `SPACE_GROUP_EFFECTS_MODELLING_REFERENCE.md` and
  `SPACE_GROUP_EFFECTS_MODULE_REFERENCE.md` do not describe `deg` at all (no
  mention of the hiss, drive or makeup). Once §7 is implemented they need the
  `deg` law, the hiss envelope (or the modulation-noise form), and D2's
  loop-gain rule.
- `S095_P1_FX_DELAY_RVB_ECHO.md` D-6 ("the hiss sits at about −48 dBFS")
  describes an absolute hiss. Under B or A the hiss is relative to the
  signal.
