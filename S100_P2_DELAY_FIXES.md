# S100 P2 — Delay and Echo fixes (revision 3, as built)

**Date:** 2026-11-21. **Branch:** `dev-ph7-alkaline`, base `6dc13b69`.
**Status:** implemented, uncommitted. The record of the implementation is
`S100_P2_IMPLEMENTATION.md`:
- steps 1–5: revision 3 as planned;
- step 6: your changes after the first listening.

This document describes the design **as built**. Where step 6 changed
revision 3, the section says so, and revision 3's figures are kept as the
record.

Revision 3 was built and measured in a scratch worktree of `6dc13b69`
before it was applied. Appendix A is that worktree's exact diff: it is what
step 2 applied, **not** the final code, since step 6 changed the tree on
top of it. Line references are to `6dc13b69`.

**Step 6 changed** (your notes of 2026-11-21, after the first build):

| Change | § |
|---|---|
| `ton` bipolar: 0 with `wid` 127 cuts nothing, + cuts lows, − cuts highs | §4.2 |
| `wid`: same ends, slow near the top | §4.2 |
| Pages: Delay `xfb spr str wow / deg dck hld rev` after `wid`; the Echo analogous | §4.1 |
| `str` bipolar: 0 one head a side, −63 mono, + taps / wider | §4.4 |
| `hld` crossfade (50 ms): the clicks at engage/release, with or without `rev` | §4.5 |
| `dck` twice as deep at 127 (it worked as designed, but shallow) | §4.10 |
| Fix: the Echo's loop was being darkened by `spr` | §4.8 |

**Revision 3** folds in your answers to revision 2's questions
(2026-11-21):

| Q | Answer | In this plan |
|---|---|---|
| Q1 RAM | approved | §5.2 |
| Q2 Echo taps | heads only, no cap raise | §4.4 |
| Q3 `deg` makeup | match at −12 dBFS | §4.7 |
| Q4 bit reduction | as built; to hear | §4.7 |
| Q5 `spr` darkening | on the tape low-pass; to try | §4.8 |
| Q6 Echo pages | as built; no migration needed | §4.1 |
| Q7 / your point 8 | the rotation's polarity flip makes a mono fold lose level as `xfb` rises: "be smarter" | **§4.3: new `xfb` (side allpass)** |
| Q9 `hld` | as built; to hear | §4.5 |
| Q10 Hermite | tried | **§4.9: measured, does not fit; decision needed** |

Q8 (`spr` symmetric around `tim`) was not answered. `spr` stays relative
(§8 Q1).

---

## 0. Summary

| # | Change (as built) | § |
|---|---|---|
| 0 | Roll back the S100 `ton` 5 Hz DC block (done) | §1 |
| 1 | Pages: SELECT 2 = `tim fbk ton wid` (Delay and Echo); the rest as in §4.1 | §4.1 |
| 2 | `ton`/`wid`: the loop band-pass, defined by its edges in 10 Hz – 21 kHz; `ton` bipolar, 0 + `wid` 127 cuts nothing | §4.2 |
| 3 | `xfb`: mid kept, side through an allpass; 127 = true ping-pong, **no polarity flip**; a mono fold never loses level | §4.3 |
| 4 | `str` replaces `hed`, bipolar: −63 mono, 0 one head a side, +64 two taps in the field (Delay) / wider (Echo) | §4.4 |
| 5 | Echo gets `spr` (its R head's time) | §4.4 |
| 6 | `hld` follows `fbk`, and crossfades in and out over 50 ms | §4.5 |
| 7 | `wow`: depth as pitch (0 – 2 %), rates 0.5 – 2.5 Hz and 6 – 12 Hz; applied after the glide | §4.6 |
| 8 | `deg`: no noise; equal-amplitude drive at −12 dBFS; bit reduction 16 → 8 above row 63 | §4.7 |
| 9 | `spr` darkens the Delay loop that runs ahead of `tim`, scaled by `wid` | §4.8 |
| 10 | `dck`: twice as deep at 127, the gain held at 0 or above | §4.10 |

| Measured | Today (`6dc13b69`) | Revision 3 | **As built** |
|---|---|---|---|
| Delay, instructions/frame (cap 252.0) | 251.6 | 225.9 | **234.5** |
| Echo, instructions/frame (cap 210.5) | 210.0 | 203.1 | **205.5** |
| Reverb | 189.5 | 189.5 | 189.5 |
| Flash | 730,936 B | 732,248 B | **732,536 B** (+1,600; headroom 21,128) |
| ITCM | 15,432 B | 15,432 B | 15,432 B |
| Space header (fixed 448 B prefix) | 392 B | 424 B | 424 B (+32, approved) |
| `SpaceRuntime` (limit 100 B) | 88 B | 96 B | 96 B (+8, approved) |
| Echo stack per block | — | +128 B | +128 B (approved) |
| Host laws | — | 41 lines; 9 break builds | **51 lines pass, 0 failures; 12 break builds caught** |

Each row adds about 124.5 cycles per block to the manager's service, about
1.8 per frame against the cap (the cap model in `space_cpu.py`):

- The Delay has one more row than today, so its effective cap is about
  250.2. As built it sits **about 16 below** it.
- The Echo has two more rows, so its effective cap is about 206.8. As
  built it sits **about 1.3 below** it.

---

## 1. Roll back first (done, implementation step 1)

Restore the files the withdrawn S100 change touched (all tracked, all
uncommitted):

```
git checkout -- Core/DSP/Effects/Space/SpaceEffect.c \
  build/LXRV2_lxr02.img \
  tools/dsp_test/__pycache__/elf_blockpath.cpython-315.pyc \
  tools/dsp_test/__pycache__/elf_looppath.cpython-315.pyc \
  tools/dsp_test/build/break_sp_lin1/SpaceEffect.host.c \
  tools/dsp_test/build/break_sp_lin2/SpaceEffect.host.c \
  tools/dsp_test/build/gen/SpaceEffect.host.c \
  tools/dsp_test/build/space \
  tools/dsp_test/build/space_break_lin1 \
  tools/dsp_test/build/space_break_lin2
```

Appendix A applies to `6dc13b69` after this.

---

## 2. Why the feedback did not build (record)

No code suppresses the feedback on purpose; the suppression came from the
loop's design.

- **DC latch-up.** At `ton` ≤ 0 (stored ≤ 63) the loop had no low cut, so
  its gain at DC was the whole `fbk`: 1.163 at row 122. Drum DC grew until
  the loop sat at about ±0.65 DC against the soft clip, where the clip is
  nearly flat. The repeats then lost most of their gain. Touching `ton` +1
  turned the high-pass on and knocked the loop off the rail.
- **Frozen high-pass.** At `ton` ≤ 0 the high-pass coefficient was 0, so
  its state froze.
- **`spr` + linear `xfb` cancellation.** An L/R average of two heads that
  `spr` sets apart cancels part of the low end. Your rows stuck at
  −19.5 dBFS.
- **This also explains S100 P1b.** D2 (makeup = 1/drive) was withdrawn for
  "high feedback stopped building to saturation". The DC latch produces
  exactly that symptom.

The old `hed` (A = D/3, B = 2D/3, C = D) was the wet **and** the feedback.
`hed A` repeated at `tim`/3. No setting gave ping-pong: `xfb` 127 was a
50/50 L/R average.

---

## 3. The loop, as built

```
heads (L lane at dL, R lane at dR)  ──► output (str: cm, cx)
taps (str above 0, Delay): A on L at dL+(dR−dL)/3, B on R at dR−(dR−dL)/3 ──► output (cn, cf)

loop input (Delay): mid = (L + R)/2 as is; side = (L − R)/2 through the xfb allpass
                    L loop = mid + side', R loop = mid − side'
loop input (Echo):  the head at tim
  → playback LP (tape speed, deg bandwidth, O8, spr darkening; open under hld)
  → band-pass (ton, wid; peak gain 1; no gain below 10 Hz)
  → drive × kb → x − c3·x³ → × makeup         (deg; linear under hld)
  → record LP:  rec = in·ing + fbk·y          (hld: ing 0, fbk = the hold gain)
  → int16 record: × 2^−b, truncate toward zero, << b   (deg bit reduction)

hld on/off: input, loop gain, shaper, playback LP and read delay crossfade
over 50 ms (§4.5)
```

The band-pass and the shaper are only in the loop. The heads and taps are
clean reads.

---

## 4. Design

### 4.1 Rows and pages

As built (step 6; the Delay in your order, the Echo analogous, since you
did not specify it):

| | SELECT 2 | SELECT 3 | SELECT 4 | SELECT 5 |
|---|---|---|---|---|
| Delay | tim fbk ton wid | xfb spr **str** wow | deg dck hld rev | |
| Echo | tim fbk ton wid | **spr** **str** wow deg | dck e>r bln dcy | lsh hsh rtg |

Revision 3 had Delay `spr xfb deg wow / hld rev str dck` and Echo
`spr deg wow e>r / bln dcy lsh hsh / str dck rtg`.

- **Shared indices:** `tim`, `fbk`, `ton` and `wid` have the same index on
  both types (3–6), which is asserted. `spr`, `str` and `deg` differ
  between the types and are read by type.
- **Rows:** the Delay has 12 (today 11) and the Echo 15 (today 13).
- **New keys:** `dly_width`, `dly_stereo`, `ech_width`, `ech_spread`,
  `ech_stereo`.
- **Labels:** `str` / "Stereo" re-use the `hed` / "Heads" label entries,
  renamed `SHORT_STR` / `LONG_STEREO`.
- **FX lanes:**
  - Delay: lane 1 → `wid`; lane 11 → `str`.
  - Echo: lane 11 → `str`. Its `wid` and `spr` have no lane, because all
    16 lanes are in use.

### 4.2 `ton` / `wid`: the loop band-pass

A TPT state-variable band-pass, one per loop: two on the Delay, one on the
Echo. Its peak gain is normalised to 1 at every width (folded into the
drive), so `fbk` reaches unity at row 100.

As built (step 6):

| | Law |
|---|---|
| Room | 10 Hz – 21 kHz: 11.04 octaves around 458 Hz |
| `ton` | bipolar (`DTYPE_PM63`, 0 = stored 63, the default); t = (row − 63)/63; centre 458 Hz × 2^(4.71 t) above 0 (12 kHz at +63), × 2^(2.52 t) below (80 Hz at −63) |
| `wid` | width N = 0.18 × 61.3^(1 − (1 − w)²) octaves, w = row/127 |
| Edges | centre·2^(∓N/2), held inside the room |
| Coefficients | both edges warped exactly (W = tan(πf/fs) via `sinf`/`cosf`), g = √(W1·W2), 2R = (W2 − W1)/g |

The width curve's slope is 0 at the top:

| `wid` | 127 | 117 | 111 | 64 | 32 | 0 |
|---|---|---|---|---|---|---|
| Width (octaves) | 11.04 | 10.8 | 10.3 | 4.0 | 1.1 | 0.18 (Q 8) |

- **`ton` 0 with `wid` 127 cuts nothing:** the band is the whole room.
  - Raising `ton` moves the low edge up and cuts the lows (100 Hz −8.8 dB
    at +63, 8 kHz kept).
  - Lowering it moves the top edge down and cuts the highs (8 kHz −8.2 dB
    at −63, 100 Hz kept).
  - With a narrow `wid`, `ton` places the peak (80 Hz – 12 kHz).
- **Revision 3 had** a unipolar `ton`, fc = 80 Hz × 150^(ton/127), with
  N = 0.18 × 111^w up to 20 octaves. `ton` then did almost nothing at
  `wid` 127, and lowering `wid` from the top narrowed the band too fast.
- **No sub-audio in the loop.** Nothing below 10 Hz passes, so the loop
  never latches onto DC or slow sub-audio.
- **Q 8 at `wid` 0, not narrower.** A narrower band than the comb's tooth
  spacing at short `tim` makes the loop die.

### 4.3 `xfb`: mid kept, side through an allpass (new in revision 3)

**Your point.** With the rotation of revision 2, mid (L + R) was turned into
side (L − R) on every pass. A mono fold then kept only cos(angle)
of each repeat, and at 127 every other repeat cancelled in mono. The flip
was also audible as a polarity change on each L→R crossing.

**Why a fixed mix cannot fix it.** Consider any mix that does not depend
on frequency.

- **No-swap and swap can't be joined.** No-swap is the identity; the true
  swap keeps mid and inverts side. Every mix in between that keeps the
  loop's level is a rotation. A rotation reaches a swap only with one side
  inverted, which is revision 2's polarity flip.
- **A mid-point mix loses level.** Any mix that is not level-keeping loses
  energy wherever `spr` sets the heads apart; the L/R average stalled the
  same way.

**What it does now.** Mid always passes untouched. Side passes through a
first-order allpass, with gain exactly 1 at every frequency. Its phase is
0° below a turnover frequency and 180° above it, and `xfb` sets the
turnover:

| `xfb` | Turnover | Effect |
|---|---|---|
| 0 | none (coefficient exactly 1) | two independent loops |
| 32 | about 2.7 kHz | the highs ping-pong, the rest stays on its side |
| 64 | about 320 Hz | everything above ~320 Hz ping-pongs |
| 127 | 5 Hz | true ping-pong: L→R→L, **same polarity** |

- **Mono keeps its level.** Mid is never touched, so a mono fold (`str`
  −63) keeps its level at every `xfb`: SP12c measures +0.00 dB at `xfb` 64
  and 127 against 0.
- **The loop's gain stays exactly `fbk`** at every frequency, so there is
  no stall where `spr` sets the heads apart. In SP12d, a stereo noise
  tail's energy decays −5.39 / −5.65 dB at `xfb` 32 / 64, against −5.68 at
  0. A side scaled instead of allpassed loses −12.7 / −9.9.
- **Cost:** +8 per frame (225.9 against revision 2's 217.8). The rotation's
  two coefficients become one, plus one allpass state (header size
  unchanged).
- **Musically**, rising `xfb` lets more of the spectrum ping-pong, from the
  top down (§8 Q2: or from the bottom up).

### 4.4 `str`: the stereo field (replaces `hed`)

The heads are L's lane at L's time and R's lane at R's time (`spr`). `str`
sets only the output; the loop recirculates the heads at `tim`.

`str` is bipolar (step 6; `DTYPE_PM63`, 0 = stored 63, the default):

| `str` (page) | Delay | Echo |
|---|---|---|
| −63 | mono: L = R = (L head + R head)/2 | the same |
| −63 – 0 | mono → stereo | the same |
| 0 | exactly one head a side | the same |
| 0 – +64 | two taps fade in, constant power: A (L's lane, ⅓ of the way from L's time to R's) panned to −⅓; B (R's lane, ⅓ from R's time) to +⅓ | wider: M/S, side up to ×2, constant power; no taps (your Q2) |

Revision 3 used stored 0 / 64 / 127 for mono / one head a side / full taps.

- **The taps move with `spr`.** They fall on the heads at `spr` 0; at
  `spr` +63 (L at D/2) they are at 2D/3 and 5D/6.
- **Under `rev`** they are silent and not read.
- **Echo:** its two heads read the mono tape at the Delay's `spr` law (the
  longer at `tim`). The loop recirculates the head at `tim`.

### 4.5 `hld`

- **Input stops; the loop runs linear.** No cubic, no `deg` drive, no bit
  reduction, and the tape low-pass is open. The read delay settles on a
  whole frame and `wow` fades out, so the reads are exact.
- **Hold gain** (row r): 1 for r ≤ 30. Above that, growth of 6 dB/s ×
  ((r − 30)/97)², times the narrow band's worst comb-tooth loss
  √(1 + (Q·fs/(fc·D))²). The level then still grows at `wid` 0.
- **Crossfade** (step 6, after the first build clicked): engaging and
  releasing `hld` take 50 ms (`SPACE_HOLD_XF`, 69 blocks; the counter is
  `SpaceRuntime.hx`, the former padding byte).
  - The input and the loop gain glide as per-frame ramps.
  - The shaper, drive, makeup and playback LP blend once a block; the bit
    reduction switches at the middle.
  - Each read delay blends from glide + swing to the nearest whole frame,
    ramped across the block.
  - **Why:** the first build switched all of this in one block. The held
    loop kept that step as a splice and clicked on every pass, and the
    read rounding stepped by whole frames.
  - **Measured** (largest third difference of a 220 Hz sine): 0.2–0.5
    before, 0.0015–0.0021 after (the clean sine's level), in every `hld`,
    `rev` and `wow` combination (SP18).
- **Measured** (`tim` 40; as built, the reference second taken 0.25 s
  into the hold, after the crossfade):

  | `fbk` | `wid` | Change, reference second → second 4 |
  |---|---|---|
  | 20 | 127 | −0.85 dB (revision 3: −0.63) |
  | 64 | 127 | +1.70 dB (+1.83) |
  | 120 | 0 | +13.1 dB (+14.1) |

### 4.6 `wow`

**Depth is pitch** (swing = p·fs/(8f) samples), so it no longer depends on
`tim`.

| | Rate | Peak pitch (× g², g = row/127) |
|---|---|---|
| Wow | 0.5 – 2.5 Hz | 0 – 2 % |
| Flutter | 6 – 12 Hz | 0 – 0.3 % |
| Drift | random walk | about the wow depth |

- **The swing is added after the 150 ms glide**, which had been filtering
  it. Tracking that needs `SpaceRuntime.dr[2]`.
- **Measured:** ±2.22 % at 127, ±0.006 % at 0.

### 4.7 `deg`

- **No noise:** the hiss is removed.
- **Drive:** ×1 to ×6 into the cubic clip, with a makeup that keeps a
  −12 dBFS sine's RMS (your Q3).
- **Bandwidth loss and O8** stay as today.
- **Bit reduction:** from 16 bits at row 63 to 8 bits at row 127, scaled,
  truncated toward zero (no limit cycle) and shifted back. At 16 bits it
  is bit-identical to today.
- **Measured:**
  - exact 0.0 with no input;
  - a tail at `fbk` 40 reaches exact 0.0 at 1.36 s;
  - the RMS within −0.21 dB of `deg` 0.
- **Known consequences** (accepted):
  - at `deg` 127 the loop reaches unity for quiet signals at `fbk` 56;
  - the self-oscillation ceiling is 10.5 dB lower.

### 4.8 `spr` darkening

The Delay loop that runs ahead of `tim` gets its playback low-pass
coefficient × (1 − 0.3 × spread × wid/127).

- **Measured** (SP17): the high-frequency share of L's repeats against R's
  is 0.404 at `spr` +63 and `wid` 127, and 1.000 at `spr` 0.
- **On the Echo** the loop is the head at `tim`, so nothing is darkened.
  The revision 3 code did darken the Echo's loop when `spr` was above 0;
  step 6 fixed that.

### 4.9 Hermite main heads (your Q10): measured, does not fit

A 4-point Hermite read on the Delay's two main heads was built and
measured:

| Build | Delay | Against the effective cap (≈ 250.2) |
|---|---|---|
| Linear heads (revision 3) | 225.9 | 24 below |
| Hermite, Catmull-Rom form | 265.2 | 15 over |
| Hermite, de Soras form (fewer operations) | 261.1 | 11 over |

It costs +35 to +39 per frame. §S18 had recorded about +30; the rest is
register spills, since the loop now also carries the allpass, the taps and
the `deg` scale.

What it buys: the 4 kHz repeat ratio at `wid` 127 goes from −0.61 dB to
−0.30 dB per pass (less dulling of repeats), and impulse peaks are higher
(0.693 against 0.598). Linear heads were kept (your answer to §8 Q3). As
built, the Delay is at 234.5 (step 6 added 8.6), so Hermite fits even less
now.

### 4.10 `dck` (step 6)

- **Investigated:** it worked as designed (O1), but shallow. The wet gain
  is 1 − duck × `dck`, where duck follows the input's block peak (|L| + |R|,
  held at 1; 2 ms attack, 200 ms release). With `dck` = row/127, the
  deepest cut only equalled the input's peak, so a −12 dBFS send ducked the
  repeats by about 6 dB.
- **As built:** `dck` = 2 × row/127, the wet gain held at 0 or above. The
  law is shared, so this applies to the Delay, the Echo and the Reverb.
- **Measured** (SP20, a steady −12 dBFS send): −6.1 dB at 64, −65.3 dB at
  127 (fully ducked).

---

## 5. Verification

### 5.1 CPU (`space_cpu.py`, instructions per frame)

| Build | Delay | Echo |
|---|---|---|
| Today | 251.6 | 210.0 |
| Revision 2 (rotation) | 217.8 | 203.2 |
| Revision 3 | 225.9 (loop 205 + per block 605/32 + 2.0) | 203.1 |
| Revision 3 + Hermite | 261.1 – 265.2 over | — |
| **As built (step 6)** | **234.5** (loop 211 + per block 689/32 + 2.0) | **205.5** (per block 992/32) |

Step 6's cost:
- **Delay +8.6:** the `hld` crossfade's two per-frame ramps and its
  per-block blend.
- **Echo +2.4:** the shared per-block code.

### 5.2 RAM (approved)

- Space header 392 → 424 B (inside the fixed 448 B prefix).
- `SpaceRuntime` 88 → 96 B (limit 100 B).
- Echo stack +128 B per block.

Step 6 added none: `hx` is the former padding byte. Sizes were checked
with the compiler.

### 5.3 Host laws (as built)

`make -C tools/dsp_test space` reports `space failures: 0` and
`crumpbit failures: 0`; 51 law lines pass. SP1–SP5, SP7 and SP9–SP11 pass
with their rows in the new order.

| Law | Measured |
|---|---|
| SP8 `hld` follows `fbk` | −0.85 / +1.70 / +13.1 dB |
| SP12 ping-pong at `xfb` 127, same polarity | L 0.598/0.000 at D, R 0.400/0.001 at 2D, L 0.342/0.001 at 3D, all positive |
| SP12b `xfb` 32 / 64, `spr` 62, `fbk` 110 builds | +8.2 / +8.3 dB (≥ +4) |
| SP12c `str` −63 (mono) keeps its level as `xfb` rises | +0.00 / +0.00 dB at `xfb` 64 / 127 (±1) |
| SP12d the `xfb` mix keeps the loop's energy | −5.39 / −5.65 dB at 32 / 64, against −5.68 at 0 (±1) |
| SP13a no DC latch | mean 0.2497 |
| SP13b `wid` 0 rings at the centre | 463 Hz (want 458 ± 5 %; revision 3: 968 / 961) |
| SP13c `wid` 127 flat | 200 Hz +0.08 dB, 4 kHz −0.61 dB |
| SP14 `deg` | 0 non-zero samples from init; zero from 1.36 s; RMS −0.21 dB |
| SP15 `wow` | ±2.22 % / ±0.006 % |
| SP16 `str` | −63: L = R; +64: L head 0.563 + tap 0.450 (0.260 on R), R head 0.423 + tap 0.407 |
| SP17 `spr` darkening | 0.404 at `spr` +63 (< 0.6), 1.000 at `spr` 0 (0.9 – 1.1) |
| SP18 no clicks, `hld` / `rev` | 0.0015 – 0.0021 in all six cases (< 0.01) |
| SP19 `ton` / `wid` | `ton` 0: 40 Hz −0.42 dB; +63: 100 Hz −8.82, 8 kHz +0.01; −63: 8 kHz −8.19, 100 Hz +0.02; `wid` 111: −0.17 / −0.28 dB |
| SP20 `dck` | −6.1 dB at 64, −65.3 dB at 127 |

**Break builds** (each must fail; all are caught):

| Build | Change | Fails |
|---|---|---|
| `space_break_lin2` | frame-stepped `rev`/tap read | SP11 |
| `space_break_xfb` | side scaled, not allpassed | SP12d |
| `space_break_mono` | mid halved | SP12c |
| `space_break_band` | no band-pass | SP13 |
| `space_break_hold` | no growth | SP8 |
| `space_break_deg` | makeup 1/drive | SP14 |
| `space_break_wow` | swing at 0.3 | SP15 |
| `space_break_str` | taps silent | SP16 |
| `space_break_dark` | no darkening | SP17 |
| `space_break_holdxf` | `hld` in one step (the first build) | SP18 |
| `space_break_wid` | revision 3's width curve | SP19 |
| `space_break_dck` | the old `dck` depth | SP20 |

### 5.4 Your rows (host, revision 3: 55 Hz kick each beat, L+R dBFS RMS per second, 1 → 8 s)

Measured on revision 3, not re-measured as built. Step 6 does not change
these rows' sound: at `ton` 0 / `wid` 127 the band is the full room, as it
was. Rows (revision 3's stored values): `tim` 90, `fbk` 122, `ton` 63
(page 0), `wid` 127, `spr` −1, `xfb` 62, `deg` 0, `wow` 0, `hld` off,
`rev` off, `str` 64 (now stored 63 = page 0), `dck` 0.

| Variation | Level |
|---|---|
| As listed | −19.9 → **−7.0** |
| `spr` 0, `xfb` 127 (ping-pong) | −19.6 → −7.2 |
| `xfb` 127, `str` −63 (mono ping-pong) | −21.1 → −6.6 (no cancellation) |
| `str` +64 (stored 127) | −16.6 → −4.3 |
| `str` −63 (stored 0, mono) | −23.6 → −9.4 |
| `deg` 127 | −20.2 → −13.5 (the accepted ceiling) |

---

## 6. Stored data

No migration, no backward compatibility (your Q6; standing rule).

- **Rows stored by index** (AutoSave, LFO targets, Pattern automation) play
  from other rows on first boot.
- **`.fx` files:** `*_heads` keys are ignored. The new keys load their
  defaults: `wid` 127, `str` stored 63 (page 0), Echo `spr` 63.
  `dly_tone`/`ech_tone` keep the byte with a new meaning: stored 63 is
  page 0, the full band at `wid` 127.
- **Rows moved twice:** revision 3, then step 6's page order. Re-set the
  Delay/Echo Scenes after flashing.
- **MIDI Learn:** learns on `*_heads` no longer resolve.
- **FX lane 11:** old `hed` values 0..6 now set `str` stored 0..6 (page
  −63 .. −57), nearly mono.
  Clear those lanes.

---

## 7. Schedule (done: `S100_P2_IMPLEMENTATION.md` steps 1–6)

1. Run §1.
2. Apply Appendix A (`git apply` on the diff block). It touches:
   - `Core/DSP/Effects/Space/SpaceEffect.c` and `.h`
   - `Core/DSP/Effects/Space/SpaceParameters.c` and `.h`
   - `Core/DSP/Effects/EffectsManager.c`
   - `Core/Menu/menu.h` and `Core/Menu/MenuLabels.c`
   - `tools/dsp_test/modules/space.c`
   - `tools/dsp_test/Makefile`
3. Run the gates:
   - `make -C tools/dsp_test space`: `space failures: 0`, nine break builds
     caught (twelve as built, after step 6);
   - `make all && make img`;
   - `make -C tools/dsp_test space_cpu_check`.
4. Leftovers: the tracked `tools/dsp_test/build/break_sp_lin1/` and
   `tools/dsp_test/build/space_break_lin1` are no longer rebuilt.
5. Device and ear (§9).
6. (Step 6) Your notes after the first listening: §4.1, §4.2, §4.4, §4.5,
   §4.8, §4.10.

---

## 8. Open questions (all closed)

Your answer, 2026-11-21: "no, that sounds good". So Q1 kept `spr`
relative, Q2 ping-pongs the highs first, and Q3 kept linear heads.

- **Q1, `spr` around `tim` (unanswered from revision 2).** Today `spr` is
  relative: the longer channel plays `tim` and the other runs ahead.
  - Symmetric (one ahead, one behind) needs tape up to `tim` × √2, so the
    longest `tim` drops by that factor.
  - The plan keeps relative unless you say otherwise.
- **Q2, `xfb` direction.** Rising `xfb` ping-pongs the highs first (the
  turnover moves down from 22 kHz). The other way round (the lows first) is
  a sign change in the coefficient, at no cost. Which do you want to hear?
- **Q3, Hermite.** It doesn't fit beside the allpass and the taps (§4.9).
  Choose one:
  - (a) keep linear heads (this plan);
  - (b) Hermite and raise the Delay cap to about 263 (the CPU ceiling rule
    needs your explicit approval);
  - (c) Hermite and pay for it. Two candidates, about 6–8 per frame
    together (estimated, not measured), so not enough alone:
    - move the type-change dry ramp out of the frame loop (S095 P4 D-a
      kept it there for constant cost);
    - measure the ducking peak on four samples a block instead of every
      frame.

---

## 9. Not verified (device and ear)

- **Band and loop:** the band-pass at narrow `wid` and the `ton` extremes;
  ringing at `fbk` > 100.
- **Stereo:** the `xfb` turnover sweep (highs ping-pong first) by ear;
  `str` taps; the Echo's widening.
- **Modulation:** `wow` at its new maximum and the flutter now reaching the
  output.
- **`deg`:** bit reduction and the makeup by ear.
- **`hld`:** the growth rates by ear; the 50 ms crossfade, with and
  without `rev`. Changing `tim` during `hld` moves the held whole frame by
  a ramped one frame; that was not probed.
- **Step 6, by ear:** `ton` bipolar; the `wid` curve; `str` bipolar; `dck`
  at its new depth.
- **CPU:** the figures come from the instruction model, not a device cycle
  probe.

## 10. Statements this makes out of date (for you to act on)

- `SPACE_GROUP_EFFECTS_MODULE_REFERENCE.md` and
  `SPACE_GROUP_EFFECTS_MODELLING_REFERENCE.md`:
  - the row tables and the laws in §4;
  - the loop diagram (§3);
  - the `xfb` side-allpass rule;
  - the Echo's two heads and `spr`.
- `S095_P1_FX_DELAY_RVB_ECHO.md`: O6 (heads), D23 (cross-feed), D5 (hiss),
  D10 (`hld` at unity) and the `wow` depth are replaced.
- `MEMORY.md` "Space effects (S095 P1/P4)": if the reference documents
  treat O6, D5, D10 or D23 as binding, those change.
- `tools/dsp_test/space_cpu.py` docstring: "the extra heads" no longer
  exist. The caps are unchanged.
- **Step 6:** in the same reference documents, also:
  - `ton` and `str` bipolar;
  - the room-based band law and the `wid` curve;
  - the `hld` crossfade;
  - O1/O14's ducking depth (`dck` = 2 × row/127).

---

## Appendix A — revision 3's measured diff (against `6dc13b69`)

This is the diff that implementation step 2 applied. Step 6 then changed
`SpaceEffect.c` and `.h`, `SpaceParameters.c` and `.h`,
`tools/dsp_test/modules/space.c` and `tools/dsp_test/Makefile` on top of
it (§4.1, §4.2, §4.4, §4.5, §4.8, §4.10). The tree, not this appendix, is
the as-built code.

```diff
diff --git a/Core/DSP/Effects/EffectsManager.c b/Core/DSP/Effects/EffectsManager.c
index 5c444a00..60804670 100644
--- a/Core/DSP/Effects/EffectsManager.c
+++ b/Core/DSP/Effects/EffectsManager.c
@@ -136,17 +136,18 @@ static const effect_registry_entry_t effects_registry[EFFECT_TYPE_COUNT] = {
      * type: format_value3 receives no type id). S095 P4 T1 (SD1): the `mix`
      * row is gone; lanes 2..15 keep their rows, so FX-lane data on them
      * keeps its meaning, and lane 1 takes the one row that had no lane
-     * (Reverb `siz`, Echo `hsh`; the Delay has none, so its lane 1 is
-     * empty).
+     * (Reverb `siz`, Echo `hsh`). S100 P2: `str` takes `hed`'s lane (11);
+     * the Delay's lane 1 takes `wid`; the Echo's `wid` and `spr` have no
+     * lane (its sixteen are in use).
      */
     {
         space_delayToken3, space_delayAbbrev5, space_delayFull8,
         EFFECT_IO_STEREO_IN | EFFECT_IO_STEREO_OUT,
         space_delayDescriptors, SPACE_DELAY_PARAM_COUNT,
         { EFFECT_LANE_MORPH_SOURCE,
-          EFFECT_LANE_NONE, SPACE_DLY_TIME, SPACE_DLY_FEEDBACK, SPACE_DLY_TONE,
+          SPACE_DLY_WIDTH, SPACE_DLY_TIME, SPACE_DLY_FEEDBACK, SPACE_DLY_TONE,
           SPACE_DLY_DEGRADE, SPACE_DLY_WOW, SPACE_DLY_XFEED, SPACE_DLY_SPREAD,
-          SPACE_DLY_REVERSE, SPACE_DLY_HOLD, SPACE_DLY_HEADS, SPACE_DLY_DUCK,
+          SPACE_DLY_REVERSE, SPACE_DLY_HOLD, SPACE_DLY_STEREO, SPACE_DLY_DUCK,
           EFFECT_COMMON_PARAM_LEVEL, EFFECT_COMMON_PARAM_PAN,
           EFFECT_LANE_NONE },
         NULL, &space_delayOps, &space_delayUi,
@@ -174,7 +175,7 @@ static const effect_registry_entry_t effects_registry[EFFECT_TYPE_COUNT] = {
         { EFFECT_LANE_MORPH_SOURCE,
           SPACE_ECH_HIGH_SHELF, SPACE_ECH_TIME, SPACE_ECH_FEEDBACK, SPACE_ECH_TONE,
           SPACE_ECH_DEGRADE, SPACE_ECH_WOW, SPACE_ECH_TO_REVERB,
-          SPACE_ECH_BLEND, SPACE_ECH_DECAY, SPACE_ECH_LOW_SHELF, SPACE_ECH_HEADS,
+          SPACE_ECH_BLEND, SPACE_ECH_DECAY, SPACE_ECH_LOW_SHELF, SPACE_ECH_STEREO,
           SPACE_ECH_DUCK, SPACE_ECH_ROUTING,
           EFFECT_COMMON_PARAM_LEVEL, EFFECT_COMMON_PARAM_PAN },
         NULL, &space_echoOps, &space_echoUi,
diff --git a/Core/DSP/Effects/Space/SpaceEffect.c b/Core/DSP/Effects/Space/SpaceEffect.c
index eb10da50..76c3c0c0 100644
--- a/Core/DSP/Effects/Space/SpaceEffect.c
+++ b/Core/DSP/Effects/Space/SpaceEffect.c
@@ -74,39 +74,51 @@
 /* ---------------------------------------------------------------------- */
 
 /*
- * One tape channel's loop filters: play (the playback LP, which also
- * carries `ton`'s low-pass side), hp (`ton`'s high-pass one-pole state),
- * rec (the record LP).
+ * One tape channel's loop filters: play (the playback LP), s1/s2 (the
+ * loop band-pass's two integrator states, S100 P2), rec (the record LP).
  */
 typedef struct {
     float play;
-    float hp;
+    float s1;
+    float s2;
     float rec;
 } space_chan_t;
 
 /*
  * The tape's coefficients (write_param, from the rows' laws): fbk (D4), spr
  * (D23), bw (the playback LP at speed 1), comp (O8: its level-dependent
- * loss), tlp/thp (`ton`: below 63 the playback LP's cutoff ceiling, above
- * 63 a one-pole high-pass; flat at 63: 1 and 0), drive/makeup/hiss (`deg`,
- * D5), hg[3] (the heads D/3, 2D/3, D, O6, normalised), wow/dck/xfb (the
- * linear rows: wow depth, ducking, cross-feed). S095 P4 T1: no wet mix;
- * the Space types are 100 % wet.
+ * loss), a1/a2/a3 (the loop band-pass, S100 P2: `ton` the centre, `wid`
+ * the width), kb (its 2R, the peak normalisation), hq (Q x fs / centre:
+ * `hld`'s narrow-band compensation), dg (the `deg` drive), drive (dg x kb,
+ * into the clip), makeup (`deg`'s equal-amplitude makeup), wr/fr/aw/af (`wow`:
+ * the wow and flutter phase steps per block and their depths in samples),
+ * dck (ducking), xa (`xfb`'s side allpass coefficient, S100 P2).
+ * bsc/bsh: `deg`'s bit reduction, the record scaled by bsc = 2^-bsh before
+ * its int16 conversion (which rounds toward zero: magnitude truncation, so
+ * the loop cannot hold a limit cycle) and shifted back by bsh.
+ * S095 P4 T1: no wet mix; the Space types are 100 % wet. S100 P2: no hiss.
  */
 typedef struct {
     float fbk;
     float spr;
     float bw;
     float comp;
-    float tlp;
-    float thp;
+    float a1;
+    float a2;
+    float a3;
+    float kb;
+    float hq;
+    float dg;
     float drive;
     float makeup;
-    float hiss;
-    float hg[3];
-    float wow;
+    float bsc;
+    uint32_t bsh;
+    float wr;
+    float fr;
+    float aw;
+    float af;
     float dck;
-    float xfb;
+    float xa;
 } space_tapeK_t;
 
 /*
@@ -170,8 +182,8 @@ typedef struct {
 
 /*
  * The header: the tape's coefficients, two channels' filters, the wow LFO
- * (wph/fph: wow and flutter phases -1..1, walk: the drift, rnd: the noise
- * word), then the plate. The Delay uses the tape part, the Reverb the
+ * (wph/fph: wow and flutter phases -1..1, walk: the drift, rnd: the drift's
+ * random word), xs (the Delay's side allpass state, `xfb`), then the plate. The Delay uses the tape part, the Reverb the
  * plate part, the Echo both (channel 0 of the tape).
  */
 typedef struct {
@@ -181,6 +193,7 @@ typedef struct {
     float fph;
     float walk;
     uint32_t rnd;
+    float xs;
     space_plate_t pl;
 } space_hdr_t;
 
@@ -246,13 +259,13 @@ static inline __attribute__((always_inline)) int32_t space_idx(float x,
 }
 
 /*
- * Linear reads at tape position x (S096 P2 F1): the extra heads (D/3, 2D/3)
- * and the reverse head's two windows, which read the nearest frame before.
+ * A linear read at tape position x (S096 P2 F1): the reverse head's two
+ * windows, which read the nearest frame before (S100 P2: the D/3 and 2D/3
+ * heads and their mono read space_lin1() are gone with `hed`).
  *
- * What:       space_lin1(): the Echo's mono int16 tape, t[i] + f (t[i + 1] -
- *             t[i]). space_lin2(): the Delay's stereo frames (L low, R high
- *             in one 32-bit word), the same per channel; one load for frame
- *             i and one for i + 1.
+ * What:       space_lin2(): the Delay's stereo frames (L low, R high in one
+ *             32-bit word), t[i] + f (t[i + 1] - t[i]) per channel; one
+ *             load for frame i and one for i + 1.
  * Why:        a nearest-frame read is clean only while its position moves
  *             exactly one frame per sample. Wow, flutter and drift (and the
  *             reverse windows, and a slowed tape at long delays) move it by
@@ -264,16 +277,6 @@ static inline __attribute__((always_inline)) int32_t space_idx(float x,
  * Cost:       per head about 15 instructions a frame more than the nearest
  *             read (S096 P2 §1.6).
  */
-static inline __attribute__((always_inline)) float space_lin1(
-    const int16_t *t, float x, int32_t nw)
-{
-    float f;
-    const int32_t i = space_idx(x, nw, &f);
-    const float a = (float)t[i];
-
-    return a + f * ((float)t[i + 1] - a);
-}
-
 static inline __attribute__((always_inline)) void space_lin2(
     const uint32_t *tape, float x, int32_t nw, float *l, float *r)
 {
@@ -316,11 +319,20 @@ static inline __attribute__((always_inline)) float space_clip1(float x)
  * One block of the tape, per block: p (the write position at the block
  * start), va (frames per output frame), n/nw (the
  * length), d[c]/dd[c] (each channel's delay and its per-frame ramp),
- * ph[c]/dph[c] (the reverse heads' phases and steps), g1..g3 (the heads,
- * O6; each x the valid gate: 0 until `valid` covers both channels' heads,
- * else 1/32768), fbk, xfb,
- * ing (the loop: feedback, cross-feed, input), rk (the record LP), bw (the
- * playback LP at this speed), dry/ddry and wg/dwg (the output's ramps).
+ * ph[c]/dph[c] (the reverse heads' phases and steps), fbk, ing (the loop
+ * gain and the input), rk (the record LP), bw[c] (each loop's playback LP:
+ * the tape speed, O8, `spr`'s darkening; 1 under `hld`), and S100 P2:
+ * - kc/kx: the loop inputs. The Delay: mid = kc (L + R), side = kc (L - R)
+ *   (kc = gate / 2), the side through the `xfb` allpass of coefficient kx,
+ *   L loop = mid + side', R loop = mid - side'. The Echo: kc x the head at
+ *   `tim`, one loop;
+ * - cm/cx/cn/cf: the output, L = cm x L head + cx x R head + cn x tap A +
+ *   cf x tap B, R mirrored (`str`);
+ * every one of these x the valid gate (0 until `valid` covers both
+ * channels' heads, else 1/32768);
+ * - c3/drv/mk/bsc/bsh: the shaper's cubic, drive and makeup, the record's
+ *   bit reduction (`deg`; under `hld` linear: 0, kb, 1, none: 1, 0);
+ * dry/ddry and wg/dwg (the output's ramps).
  */
 typedef struct {
     float p;
@@ -330,71 +342,111 @@ typedef struct {
     float dd[2];
     float ph[2];
     float dph[2];
-    float g1;
-    float g2;
-    float g3;
     float fbk;
-    float xfb;
     float ing;
     float rk;
-    float bw;
+    float bw[2];
+    float kc;
+    float kx;
+    float cm;
+    float cx;
+    float cn;
+    float cf;
+    float c3;
+    float drv;
+    float mk;
+    float bsc;
+    uint32_t bsh;
     float dry;
     float ddry;
     float wg;
     float dwg;
 } space_pass_t;
 
+
+
 /*
  * One loop channel after its heads (plan §4.1): the playback LP (its
- * bandwidth falls with the level per block, O8, and `ton`'s low side caps
- * it), `ton`'s high-pass, the drive and soft clip (unity small-signal
- * gain, §S18) with makeup, the hiss,
- * then the record LP of in x ing + fbk x loop. The cubic clip is odd, so
- * the loop makes no DC (no DC block, §S18). Returns the record value.
+ * bandwidth falls with the level per block, O8), the loop band-pass (S100
+ * P2), the drive and soft clip x - c3 x^3 with makeup (`deg`; c3 0 under
+ * `hld`: linear to the clip), then the record LP of in x ing + fbk x loop.
+ * Returns the record value. S100 P2: no hiss.
  */
 static inline __attribute__((always_inline)) float space_loop(
-    space_chan_t *c, float x, float in, float nz, float bw, float thp,
-    float drive, float makeup, float rk, float ing, float fbk)
+    space_chan_t *c, float x, float in, float bw, float a1, float a2,
+    float a3, float c3, float drive, float makeup, float rk, float ing,
+    float fbk)
 {
+    float v1;
+    float v2;
+    float v3;
     float y;
 
     c->play += bw * (x - c->play);
-    c->hp += thp * (c->play - c->hp);
-    y = space_clip1((c->play - c->hp) * drive);
+    /*
+     * S100 P2: the loop band-pass (TPT state-variable filter, band output
+     * v1; drive carries its normalisation, so the peak gain is 1 at every
+     * `wid`). It has no gain at DC, so the loop cannot latch onto DC.
+     */
+    v3 = c->play - c->s2;
+    v1 = a1 * c->s1 + a2 * v3;
+    v2 = c->s2 + a2 * c->s1 + a3 * v3;
+    c->s1 = 2.0f * v1 - c->s1;
+    c->s2 = 2.0f * v2 - c->s2;
+    y = space_clip1(v1 * drive);
     /*
      * x - x^3 / 3: slope 1 at 0 (§S18: the 1.5 x - 0.5 x^3 clip had a small-
      * signal gain of 1.5, so `fbk` unity and `hld` grew), 2/3 at the clip.
      */
-    y = y * (1.0f - (1.0f / 3.0f) * y * y) * makeup + nz;
+    y = y * (1.0f - c3 * y * y) * makeup;
     c->rec += rk * (in * ing + fbk * y - c->rec);
     return c->rec;
 }
 
+/*
+ * A linear read of one lane of the Delay's stereo tape (L lane 0, R lane
+ * 1) at position x: t[2i + lane] + f (t[2i + 2 + lane] - t[2i + lane]).
+ */
+static inline __attribute__((always_inline)) float space_lane(
+    const int16_t *t, float x, int32_t nw, uint32_t lane)
+{
+    float f;
+    const int32_t i = space_idx(x, nw, &f);
+    const int16_t *q = t + 2 * i + lane;
+
+    return (float)q[0] + f * ((float)q[2] - (float)q[0]);
+}
+
 /*
  * The Delay's block (plan §4.1): both channels in one pass.
  *
  * What, per frame (the write position p advancing by va):
- * 1. heads per channel: D through a linear read (§S18; the plan's Hermite
- *    was over the cap), or with
- *    `rev` two heads reading backwards through L's last D in triangular
- *    crossfaded windows, both channels from each frame load (O4); D/3 and
- *    2D/3 at L's time for both channels (O6); the weighted sum, gated by
- *    `valid`, is the wet (the clean read, D5). S096 P2 F1: the reverse
- *    windows and the D/3, 2D/3 heads read linearly (space_lin2(), two
- *    frame loads each); they read the nearest frame before, which slipped a
- *    frame whenever wow or the reverse ramp moved them by fractions;
- * 2. the output: in x dry + wet x wg (both ramped), and the input peak
- *    (the ducking follower, per block);
- * 3. the loop: the cross-feed (D23: each channel takes xfb of the other's
- *    heads, the same frame), each channel's chain (space_loop()), one hiss
- *    word for both;
- * 4. the record: L,R packed into one saturating 32-bit write at floor(p),
- *    and into the guard when floor(p) < 3.
+ * 1. heads: L's lane at L's delay dl, R's lane at R's delay dr, linear (or
+ *    with `rev` two windows reading backwards through L's last D, both
+ *    lanes from each frame load, O4; S096 P2 F1: linear);
+ * 2. `str`'s taps (S100 P2): A on L's lane at dl + (dr - dl) / 3, B on R's
+ *    lane at dr - (dr - dl) / 3, panned +-1/3, so the four reads spread
+ *    across the field in time order and move with `spr`; with `spr` 0 they
+ *    fall on the heads. Not read under `rev` (silent there);
+ * 3. the output: in x dry + wg x (cm L + cx R + cn A + cf B), R mirrored
+ *    (`str`: mono .. one head a side .. the taps in the field), and the
+ *    input peak (the ducking follower, per block);
+ * 4. the loop (S100 P2): mid and side of the heads; the side through a
+ *    first-order allpass (`xfb`: its turnover 22 kHz .. 5 Hz), so the band
+ *    above the turnover swaps sides each pass and the band below stays
+ *    (127: all of it, a true ping-pong, no polarity flip); the mid is never
+ *    touched, so a mono fold (`str` 0) keeps its level at every `xfb`, and
+ *    the loop's gain is exactly `fbk` at every frequency (no stall where
+ *    `spr` sets the heads apart); then each channel's chain (space_loop());
+ * 5. the record: L,R packed into one saturating 32-bit write at floor(p)
+ *    (`deg`'s bit reduction: x bsc, << bsh), and into the guard when
+ *    floor(p) < 3.
+ * The heads and taps are output reads: the band-pass is only in the loop.
  * Every read lands at least SPACE_D_MIN behind the block start, so no read
  * sees this block's writes.
  * Inputs: the tape (L,R frames), the header, the pass, io. Outputs: io in
- * place, the tape, the header's channel states and noise word; returns the
- * block's input peak (|L| + |R|).
+ * place, the tape, the header's channel states; returns the block's input
+ * peak (|L| + |R|).
  * Out of line and unswitched: one copy of the loop (flash, §S18).
  */
 static SPACE_LOOP float
@@ -402,24 +454,28 @@ space_delayRun(uint32_t *tape, space_hdr_t *h, const space_pass_t *ps,
                uint8_t rev, float *l, float *r, uint32_t frames)
 {
     const int16_t *t = (const int16_t *)(const void *)tape;
-    const float third = 1.0f / 3.0f;
     const float n = ps->n;
     const int32_t nw = (int32_t)n;
     const float va = ps->va;
-    const float g1 = ps->g1;
-    const float g2 = ps->g2;
-    const float g3 = ps->g3;
-    const float xfb = ps->xfb;
+    const float kc = ps->kc;
+    const float kx = ps->kx;
+    const float cm = ps->cm;
+    const float cx = ps->cx;
+    const float cn = ps->cn;
+    const float cf = ps->cf;
     const float fbk = ps->fbk;
     const float ing = ps->ing;
     const float rk = ps->rk;
-    /* O8 per block: louder repeats are duller (the level at the start). */
-    const float bwl = ps->bw * (1.0f - h->k.comp * fabsf(h->ch[0].play));
-    const float bwr = ps->bw * (1.0f - h->k.comp * fabsf(h->ch[1].play));
-    const float hiss = h->k.hiss;
-    const float thp = h->k.thp;
-    const float drive = h->k.drive;
-    const float makeup = h->k.makeup;
+    const float bwl = ps->bw[0];
+    const float bwr = ps->bw[1];
+    const float a1 = h->k.a1;
+    const float a2 = h->k.a2;
+    const float a3 = h->k.a3;
+    const float c3 = ps->c3;
+    const float drive = ps->drv;
+    const float makeup = ps->mk;
+    const float bsc = ps->bsc;
+    const uint32_t bsh = ps->bsh;
     const float ddry = ps->ddry;
     const float dwg = ps->dwg;
     const float ddl = ps->dd[0];
@@ -435,7 +491,7 @@ space_delayRun(uint32_t *tape, space_hdr_t *h, const space_pass_t *ps,
     float dry = ps->dry;
     float wg = ps->wg;
     float peak = 0.0f;
-    uint32_t rn = h->rnd;
+    float xs = h->xs;
     uint32_t m;
 
     SPACE_ONCE
@@ -444,11 +500,10 @@ space_delayRun(uint32_t *tape, space_hdr_t *h, const space_pass_t *ps,
         const float xr = r[m];
         float yl;
         float yr;
-        float f;
+        float ta;
+        float tb;
         float al;
         float ar;
-        float nz;
-        int32_t i;
 
         /* 1. Heads. */
         if (rev) {
@@ -460,8 +515,6 @@ space_delayRun(uint32_t *tape, space_hdr_t *h, const space_pass_t *ps,
             const float pb = pl + 0.5f - (float)(int32_t)(pl + 0.5f);
             const float wl = 1.0f - fabsf(2.0f * pl - 1.0f);
             const float x0 = p + n - SPACE_D_MIN;
-            float al;
-            float ar;
             float bl;
             float br;
 
@@ -472,50 +525,46 @@ space_delayRun(uint32_t *tape, space_hdr_t *h, const space_pass_t *ps,
             yr = br + wl * (ar - br);
             pl += dpl;
             pl -= (float)(int32_t)pl;
+            /* No taps under `rev` (their gains are 0, space_tapeBegin()). */
+            ta = 0.0f;
+            tb = 0.0f;
         } else {
-            /* Linear (§S18: the Hermite read cost 30 a frame over the cap). */
-            const int16_t *q;
-
-            i = space_idx(p - dl + n, nw, &f);
-            q = t + 2 * i;
-            yl = (float)q[0] + f * ((float)q[2] - (float)q[0]);
-            i = space_idx(p - dr + n, nw, &f);
-            q = t + 1 + 2 * i;
-            yr = (float)q[0] + f * ((float)q[2] - (float)q[0]);
-        }
-        /*
-         * D/3 and 2D/3 at L's time for both channels: each 32-bit frame load
-         * gives L and R (§S18); S096 P2 F1: read linearly (were nearest).
-         */
-        {
-            float h1l;
-            float h1r;
-            float h2l;
-            float h2r;
-
-            space_lin2(tape, p - dl * third + n, nw, &h1l, &h1r);
-            space_lin2(tape, p - dl * (2.0f * third) + n, nw, &h2l, &h2r);
-            yl = g3 * yl + g1 * h1l + g2 * h2l;
-            yr = g3 * yr + g1 * h1r + g2 * h2r;
+            /*
+             * Linear (§S18: the Hermite read cost 30 a frame over the cap;
+             * S100 P2 measured +39 here, still over).
+             */
+            yl = space_lane(t, p - dl + n, nw, 0u);
+            yr = space_lane(t, p - dr + n, nw, 1u);
+            /* 2. `str`'s taps a third of the way in from each head (S100 P2). */
+            const float dz = (dr - dl) * (1.0f / 3.0f);
+
+            ta = space_lane(t, p - (dl + dz) + n, nw, 0u);
+            tb = space_lane(t, p - (dr - dz) + n, nw, 1u);
         }
-        /* 2. The output and the input peak. */
-        l[m] = xl * dry + yl * wg;
-        r[m] = xr * dry + yr * wg;
+        /* 3. The output and the input peak. */
+        l[m] = xl * dry + wg * (cm * yl + cx * yr + cn * ta + cf * tb);
+        r[m] = xr * dry + wg * (cm * yr + cx * yl + cn * tb + cf * ta);
         dry += ddry;
         wg += dwg;
         peak = __builtin_fmaxf(peak, fabsf(xl) + fabsf(xr));
-        /* 3. The loop. */
-        rn = rn * 1664525u + 1013904223u;
-        nz = hiss * (float)(int32_t)rn;
-        al = space_loop(&cl, yl + xfb * (yr - yl), xl, nz, bwl, thp, drive,
-                        makeup, rk, ing, fbk);
-        ar = space_loop(&cr, yr + xfb * (yl - yr), xr, nz, bwr, thp, drive,
-                        makeup, rk, ing, fbk);
-        /* 4. The record: L low, R high. */
+        /* 4. The loop: mid as is, side through the `xfb` allpass. */
+        {
+            const float mid = kc * (yl + yr);
+            const float sd = kc * (yl - yr);
+            const float sp = kx * sd + xs;
+
+            xs = sd - kx * sp;
+            al = space_loop(&cl, mid + sp, xl, bwl, a1, a2, a3, c3, drive,
+                            makeup, rk, ing, fbk);
+            ar = space_loop(&cr, mid - sp, xr, bwr, a1, a2, a3, c3, drive,
+                            makeup, rk, ing, fbk);
+        }
+        /* 5. The record: L low, R high, `deg`'s bit reduction. */
         {
             const uint32_t w = (uint32_t)p;
-            const uint32_t v = ((uint32_t)space_q15(al) & 0xFFFFu) |
-                               ((uint32_t)space_q15(ar) << 16);
+            const uint32_t v =
+                (((uint32_t)space_q15(al * bsc) << bsh) & 0xFFFFu) |
+                ((uint32_t)space_q15(ar * bsc) << (bsh + 16u));
 
             tape[w] = v;
             if (w < SPACE_TAPE_GUARD)
@@ -528,68 +577,78 @@ space_delayRun(uint32_t *tape, space_hdr_t *h, const space_pass_t *ps,
     }
     h->ch[0] = cl;
     h->ch[1] = cr;
-    h->rnd = rn;
+    h->xs = xs;
     return peak;
 }
 
+/* A linear read of the Echo's mono tape at position x. */
+static inline __attribute__((always_inline)) float space_mono(
+    const int16_t *t, float x, int32_t nw)
+{
+    float f;
+    const int32_t i = space_idx(x, nw, &f);
+    const float a = (float)t[i];
+
+    return a + f * ((float)t[i + 1] - a);
+}
+
 /*
- * The Echo's mono tape (plan §4.3: the Delay's laws on one channel, a
- * linear main read, the Delay's D/3 and 2D/3 heads, no `rev` or cross-feed).
- * S096 P2 F1: the D/3 and 2D/3 heads read linearly (space_lin1(); they read
- * the nearest frame before, which slipped under wow).
- * What, per frame: heads -> e[m] (the echo, gated: the wet), the loop
- * chain, the record of in[m] + ka x pmq[m / 4] (the routed plate, quarter
- * rate) at floor(p), int16 mono with the guard; and the plate's input
- * pi[m] = ce x echo + cm x in[m] (the routings fold in here, §S18).
+ * The Echo's mono tape (plan §4.3: the Delay's laws on one loop, no `rev`
+ * or cross-feed). S100 P2: two heads on the one tape, the long one at `tim`
+ * (d[0] here; it is the loop's) and the short one `tim` / `spr` (d[1]).
+ * No extra taps (a centre tap measured +21 a frame, over the cap). The
+ * caller passes the outputs so that e0 is the long head's side.
+ * What, per frame: the heads -> e0[m], e1[m] (`str`'s mix,
+ * gated: the wet), the long head (gated) -> the loop chain, the record of
+ * in[m] + ka x pmq[m / 4] (the routed plate, quarter rate) at floor(p),
+ * int16 mono with `deg`'s bit reduction and the guard; and the plate's
+ * input pi[m] = ce x the loop head + cmi x in[m] (the routings fold in
+ * here, §S18). No `hld` on the Echo: input 1, cubic 1/3.
  */
 static SPACE_LOOP void space_echoTapeRun(
     int16_t *t, space_hdr_t *h, const space_pass_t *ps, const float *in,
-    const float *pmq, float ka, float *pi, float ce, float cm, float *e,
-    uint32_t frames)
+    const float *pmq, float ka, float *pi, float ce, float cmi, float *e0,
+    float *e1, uint32_t frames)
 {
-    const float third = 1.0f / 3.0f;
     const float n = ps->n;
     const int32_t nw = (int32_t)n;
     const float va = ps->va;
-    const float g1 = ps->g1;
-    const float g2 = ps->g2;
-    const float g3 = ps->g3;
+    const float kc = ps->kc;
+    const float cm = ps->cm;
+    const float cx = ps->cx;
     const float fbk = ps->fbk;
-    const float ing = ps->ing;
     const float rk = ps->rk;
-    const float bw = ps->bw * (1.0f - h->k.comp * fabsf(h->ch[0].play));
-    const float hiss = h->k.hiss;
-    const float thp = h->k.thp;
-    const float drive = h->k.drive;
-    const float makeup = h->k.makeup;
-    const float dd = ps->dd[0];
+    const float bw = ps->bw[0];
+    const float a1 = h->k.a1;
+    const float a2 = h->k.a2;
+    const float a3 = h->k.a3;
+    const float drive = ps->drv;
+    const float makeup = ps->mk;
+    const float bsc = ps->bsc;
+    const uint32_t bsh = ps->bsh;
+    const float dd0 = ps->dd[0];
+    const float dd1 = ps->dd[1];
     space_chan_t c = h->ch[0];
     float p = ps->p;
-    float d = ps->d[0];
-    uint32_t rn = h->rnd;
+    float d0 = ps->d[0];
+    float d1 = ps->d[1];
     uint32_t m;
 
     SPACE_ONCE
     for (m = 0u; m < frames; m++) {
-        float f;
+        const float y0 = space_mono(t, p - d0 + n, nw);
+        const float y1 = space_mono(t, p - d1 + n, nw);
+        const float lp = kc * y0;
         float y;
-        int32_t i;
-
-        i = space_idx(p - d + n, nw, &f);
-        y = (float)t[i];
-        y += f * ((float)t[i + 1] - y);
-        /* S096 P2 F1: the extra heads read linearly (were nearest) */
-        y = g3 * y + g1 * space_lin1(t, p - d * third + n, nw) +
-            g2 * space_lin1(t, p - d * (2.0f * third) + n, nw);
-        e[m] = y;
-        pi[m] = ce * y + cm * in[m];
-        rn = rn * 1664525u + 1013904223u;
-        y = space_loop(&c, y, in[m] + ka * pmq[m >> 2],
-                       hiss * (float)(int32_t)rn, bw, thp, drive, makeup, rk,
-                       ing, fbk);
+
+        e0[m] = cm * y0 + cx * y1;
+        e1[m] = cm * y1 + cx * y0;
+        pi[m] = ce * lp + cmi * in[m];
+        y = space_loop(&c, lp, in[m] + ka * pmq[m >> 2], bw, a1, a2, a3,
+                       1.0f / 3.0f, drive, makeup, rk, 1.0f, fbk);
         {
             const uint32_t w = (uint32_t)p;
-            const int16_t v = (int16_t)space_q15(y);
+            const int16_t v = (int16_t)((uint32_t)space_q15(y * bsc) << bsh);
 
             t[w] = v;
             if (w < SPACE_TAPE_GUARD)
@@ -597,10 +656,10 @@ static SPACE_LOOP void space_echoTapeRun(
         }
         p += va;
         p -= (p >= n) ? n : 0.0f;
-        d += dd;
+        d0 += dd0;
+        d1 += dd1;
     }
     h->ch[0] = c;
-    h->rnd = rn;
 }
 
 /* ---------------------------------------------------------------------- */
@@ -1131,13 +1190,14 @@ static SPACE_SIZE float space_onePoleHalf(float fc)
 /* The laws a row drives (write_param), per type in row order (3..). */
 enum {
     SPACE_LAW_NONE = 0, SPACE_LAW_TIME, SPACE_LAW_SPR,
-    SPACE_LAW_FBK, SPACE_LAW_XFB, SPACE_LAW_TONE, SPACE_LAW_DEG,
+    SPACE_LAW_FBK, SPACE_LAW_XFB, SPACE_LAW_DEG,
     SPACE_LAW_WOW, SPACE_LAW_HOLD, SPACE_LAW_REV,
-    SPACE_LAW_HED, SPACE_LAW_DCK, SPACE_LAW_PRE, SPACE_LAW_SIZ,
+    SPACE_LAW_DCK, SPACE_LAW_PRE, SPACE_LAW_SIZ,
     SPACE_LAW_DCY, SPACE_LAW_DIF, SPACE_LAW_LSH, SPACE_LAW_LSF,
     SPACE_LAW_HSH, SPACE_LAW_HSF, SPACE_LAW_MOD, SPACE_LAW_WID,
     SPACE_LAW_FRZ, SPACE_LAW_SHM, SPACE_LAW_INT, SPACE_LAW_ENV,
-    SPACE_LAW_E2R, SPACE_LAW_BLN, SPACE_LAW_RTG, SPACE_LAW_RTONE
+    SPACE_LAW_E2R, SPACE_LAW_BLN, SPACE_LAW_RTG, SPACE_LAW_RTONE,
+    SPACE_LAW_BAND
 };
 
 /*
@@ -1145,25 +1205,105 @@ enum {
  * column 0 is now row 3, `tim` / `pre`).
  */
 static const uint8_t space_laws[3][SPACE_ROWS_MAX] = {
-    /* Delay: tim spr fbk xfb ton deg wow hld rev hed dck */
-    { SPACE_LAW_TIME, SPACE_LAW_SPR, SPACE_LAW_FBK, SPACE_LAW_XFB,
-      SPACE_LAW_TONE, SPACE_LAW_DEG, SPACE_LAW_WOW, SPACE_LAW_HOLD,
-      SPACE_LAW_REV, SPACE_LAW_HED, SPACE_LAW_DCK },
+    /*
+     * Delay (S100 P2): tim fbk ton wid spr xfb deg wow hld rev str dck
+     * (`str` is read per block from its row: no law).
+     */
+    { SPACE_LAW_TIME, SPACE_LAW_FBK, SPACE_LAW_BAND, SPACE_LAW_BAND,
+      SPACE_LAW_SPR, SPACE_LAW_XFB, SPACE_LAW_DEG, SPACE_LAW_WOW,
+      SPACE_LAW_HOLD, SPACE_LAW_REV, SPACE_LAW_NONE, SPACE_LAW_DCK },
     /* Reverb: pre siz dcy dif lsh lsf hsh hsf mod wid frz shm int dtn env dck ton */
     { SPACE_LAW_PRE, SPACE_LAW_SIZ, SPACE_LAW_DCY, SPACE_LAW_DIF,
       SPACE_LAW_LSH, SPACE_LAW_LSF, SPACE_LAW_HSH, SPACE_LAW_HSF,
       SPACE_LAW_MOD, SPACE_LAW_WID, SPACE_LAW_FRZ, SPACE_LAW_SHM,
       SPACE_LAW_INT, SPACE_LAW_INT, SPACE_LAW_ENV, SPACE_LAW_DCK,
       SPACE_LAW_RTONE },
-    /* Echo: tim fbk ton deg wow e>r bln dcy lsh hsh hed dck rtg */
-    { SPACE_LAW_TIME, SPACE_LAW_FBK, SPACE_LAW_TONE, SPACE_LAW_DEG,
-      SPACE_LAW_WOW, SPACE_LAW_E2R, SPACE_LAW_BLN, SPACE_LAW_DCY,
-      SPACE_LAW_LSH, SPACE_LAW_HSH, SPACE_LAW_HED, SPACE_LAW_DCK,
-      SPACE_LAW_RTG },
+    /* Echo (S100 P2): tim fbk ton wid spr deg wow e>r bln dcy lsh hsh str dck rtg */
+    { SPACE_LAW_TIME, SPACE_LAW_FBK, SPACE_LAW_BAND, SPACE_LAW_BAND,
+      SPACE_LAW_SPR, SPACE_LAW_DEG, SPACE_LAW_WOW, SPACE_LAW_E2R,
+      SPACE_LAW_BLN, SPACE_LAW_DCY, SPACE_LAW_LSH, SPACE_LAW_HSH,
+      SPACE_LAW_NONE, SPACE_LAW_DCK, SPACE_LAW_RTG },
 };
 
 _Static_assert((int)SPACE_ECH_TIME == (int)SPACE_DLY_TIME,
                "the tape types share the `tim` row index");
+_Static_assert((int)SPACE_ECH_FEEDBACK == (int)SPACE_DLY_FEEDBACK &&
+               (int)SPACE_ECH_TONE == (int)SPACE_DLY_TONE &&
+               (int)SPACE_ECH_WIDTH == (int)SPACE_DLY_WIDTH &&
+               (int)SPACE_ECH_SPREAD == (int)SPACE_DLY_SPREAD,
+               "the tape types share the `fbk` `ton` `wid` `spr` row indices");
+
+/*
+ * The loop band-pass (S100 P2), from the stored rows, so the order the rows
+ * arrive in does not matter.
+ * What:
+ * - the centre asked for: fc = 80 Hz x 150^(ton / 127) (80 Hz .. 12 kHz);
+ * - the width in octaves: N = 0.18 x 111^(wid / 127) (0.18: Q 8, a resonant
+ *   peak; 20 at 127);
+ * - the band's -3 dB edges fc 2^(-N/2) .. fc 2^(N/2), held inside 10 Hz ..
+ *   21 kHz, so `wid` 127 passes 10 Hz .. 21 kHz at any `ton` and the loop
+ *   never passes sub-audio (no DC or slow latch);
+ * - the filter for those edges, exact after the bilinear warp: each edge
+ *   warped, W = tan(pi f / fs) (sinf / cosf, already linked), g = the
+ *   warped centre sqrt(W1 W2), kb = 2R = (W2 - W1) / g; drive = dg x kb
+ *   (the peak gain is 1 at every width, so `fbk` unity stays at row 100);
+ *   hq = fs / (kb x sqrt(f1 f2)), `hld`'s narrow-band compensation
+ *   (space_tapeBegin()). S100 P2: warping only the centre had put the
+ *   digital top edge of `wid` 127 at 12.7 kHz.
+ * Callers: space_writeParam() (SPACE_LAW_BAND, SPACE_LAW_DEG).
+ */
+static SPACE_SIZE void space_bandLaw(const SpaceRuntime *rt, space_tapeK_t *k)
+{
+    const float ton = (float)rt->rows[SPACE_DLY_TONE - 3u] * (1.0f / 127.0f);
+    const float wid = (float)rt->rows[SPACE_DLY_WIDTH - 3u] * (1.0f / 127.0f);
+    const float fc = 80.0f * exp2f(7.2288187f * ton);
+    const float h = exp2f(0.5f * 0.18f * exp2f(6.7944483f * wid));
+    const float f1 = __builtin_fmaxf(fc / h, 10.0f);
+    const float f2 = __builtin_fminf(fc * h, 21000.0f);
+    const float x1 = 3.14159265f * f1 * (1.0f / SK_FS);
+    const float x2 = 3.14159265f * f2 * (1.0f / SK_FS);
+    const float w1 = sinf(x1) / cosf(x1);
+    const float w2 = sinf(x2) / cosf(x2);
+    const float g = sqrtf(w1 * w2);
+    const float kb = (w2 - w1) / g;
+    const float a1 = 1.0f / (1.0f + g * (g + kb));
+
+    k->a1 = a1;
+    k->a2 = g * a1;
+    k->a3 = g * g * a1;
+    k->kb = kb;
+    k->hq = SK_FS / (kb * sqrtf(f1 * f2));
+    k->drive = k->dg * kb;
+}
+
+/*
+ * `deg`'s equal-amplitude makeup (S100 P2): the RMS of a -12 dBFS sine
+ * through drive d and the clip x - x^3 / 3, matched to the sine's own RMS,
+ * relative to drive 1 (so `deg` 0 keeps makeup 1). Sixteen points of a
+ * quarter period (the clip is odd and symmetric). Cold: `deg` changes only.
+ */
+static SPACE_SIZE float space_degRms(float d)
+{
+    static const float q[16] = {
+    0.0490677f, 0.1467305f, 0.2429802f, 0.3368899f,
+    0.4275551f, 0.5141027f, 0.5956993f, 0.6715590f,
+    0.7409511f, 0.8032075f, 0.8577286f, 0.9039893f,
+    0.9415441f, 0.9700313f, 0.9891765f, 0.9987955f
+    };
+    float si = 0.0f;
+    float so = 0.0f;
+    uint32_t j;
+
+    for (j = 0u; j < 16u; j++) {
+        const float x = 0.25f * q[j];
+        const float u = space_clip1(d * x);
+        const float y = u * (1.0f - (1.0f / 3.0f) * u * u);
+
+        si += x * x;
+        so += y * y;
+    }
+    return sqrtf(si / so);
+}
 
 /* A flag bit of rt->flags set or cleared. */
 static SPACE_SIZE void space_flag(SpaceRuntime *rt, uint8_t bit, uint8_t on)
@@ -1212,42 +1352,59 @@ static __attribute__((cold)) SPACE_SIZE void space_writeParam(void *rt_void,
     switch (space_laws[rt->tag][index - EFFECT_COMMON_PARAM_COUNT]) {
     case SPACE_LAW_SPR: k->spr = space_sprRatio(value); break;
     case SPACE_LAW_FBK: k->fbk = space_fbkGain(value); break;
-    case SPACE_LAW_XFB: k->xfb = 0.5f * g; break;
-    case SPACE_LAW_WOW: k->wow = 0.005f * g; break;
-    case SPACE_LAW_DCK: k->dck = g; break;
-    case SPACE_LAW_TONE: {
-        /* Bipolar: LP 200 Hz..18 kHz below 63, HP 20 Hz..2 kHz above. */
-        const float t = ((float)value - 63.0f) * (1.0f / 63.0f);
+    case SPACE_LAW_XFB: {
+        /*
+         * S100 P2: the side allpass's turnover, 22 kHz x (5 / 22000)^g
+         * (22 kHz .. 5 Hz), coefficient (tan - 1) / (tan + 1): DC keeps
+         * its side, Nyquist swaps, the turnover between. Row 0: exactly 1
+         * (no swap). A frequency-independent mix cannot both keep the loop
+         * gain at `fbk` and keep the mid (a swap and the identity are not
+         * joined by any rotation without a polarity flip); an allpass on
+         * the side does both.
+         */
+        const float x = 3.14159265f * 22000.0f * exp2f(-12.1032155f * g) *
+                        (1.0f / SK_FS);
+        const float t = sinf(x) / cosf(x);
 
-        k->tlp = (t < 0.0f) ? space_onePoleK(18000.0f * exp2f(6.5f * t))
-                            : 1.0f;
-        k->thp = (t > 0.0f) ? space_onePoleK(20.0f * exp2f(6.6f * t)) : 0.0f;
+        k->xa = (value == 0u) ? 1.0f : (t - 1.0f) / (t + 1.0f);
         break;
     }
-    case SPACE_LAW_DEG:
-        /* D5: drive x1..x6, bandwidth 16 kHz down 2 octaves, hiss -72..-48 dBFS. */
-        k->drive = 1.0f + 5.0f * g;
-        k->makeup = 1.0f / (1.0f + 2.0f * g);
-        k->bw = space_onePoleK(16000.0f * exp2f(-2.0f * g));
-        k->comp = 0.25f + 0.5f * g;
-        k->hiss = (value == 0u) ? 0.0f
-                : exp2f((-72.0f + 24.0f * g) * 0.16609640f) *
-                  (1.0f / 2147483648.0f);
+    case SPACE_LAW_WOW: {
+        /*
+         * S100 P2: depth as pitch, so it is the same at every `tim`, and
+         * rates that rise with the row: wow 0.5 .. 2.5 Hz, peak pitch
+         * 0 .. 2 % (34 cents); flutter 6 .. 12 Hz, 0 .. 0.3 %; both depths
+         * x g^2. The parabolic LFO's peak slope is 8 f per unit depth, so
+         * a depth of p fs / (8 f) samples gives a pitch peak of p.
+         */
+        const float fw = 0.5f * (1.0f + 4.0f * g);
+        const float ff = 6.0f * (1.0f + g);
+
+        k->wr = 2.0f * fw * (float)SK_FRAMES_MAX / SK_FS;
+        k->fr = 2.0f * ff * (float)SK_FRAMES_MAX / SK_FS;
+        k->aw = 0.02f * g * g * SK_FS / (8.0f * fw);
+        k->af = 0.003f * g * g * SK_FS / (8.0f * ff);
         break;
-    case SPACE_LAW_HED: {
+    }
+    case SPACE_LAW_DCK: k->dck = g; break;
+    case SPACE_LAW_BAND: space_bandLaw(rt, k); break;
+    case SPACE_LAW_DEG: {
         /*
-         * O6: the stops' head sets in knob order A B C AB AC BC ABC (bit 0
-         * D/3, bit 1 2D/3, bit 2 D; S095 P4: the stored value is the stop).
+         * D5, S100 P2: no noise. Drive x1..x6 into the clip with an
+         * equal-amplitude makeup (space_degRms(): a -12 dBFS sine keeps its
+         * level), bandwidth 16 kHz down 2 octaves, the O8 loss, and above
+         * row 63 the record's bit reduction, 16 -> 8 bits at 127.
          */
-        static const uint8_t sets[SPACE_HEADS_STOPS] = { 1u, 2u, 4u, 3u, 5u,
-                                                         6u, 7u };
-        const uint8_t s = sets[space_step(value, SPACE_HEADS_STOPS)];
-        const float h = 1.0f / sqrtf((float)((s & 1u) + ((s >> 1) & 1u) +
-                                             ((s >> 2) & 1u)));
-
-        k->hg[0] = (s & 1u) ? h : 0.0f;
-        k->hg[1] = (s & 2u) ? h : 0.0f;
-        k->hg[2] = (s & 4u) ? h : 0.0f;
+        const uint32_t bits = (value <= 63u) ? 16u
+                            : 16u - ((uint32_t)(value - 63u) * 8u + 32u) / 64u;
+
+        k->dg = 1.0f + 5.0f * g;
+        k->makeup = space_degRms(k->dg) / space_degRms(1.0f);
+        space_bandLaw(rt, k);
+        k->bw = space_onePoleK(16000.0f * exp2f(-2.0f * g));
+        k->comp = 0.25f + 0.5f * g;
+        k->bsh = 16u - bits;
+        k->bsc = 1.0f / (float)(1u << k->bsh);
         break;
     }
     case SPACE_LAW_HOLD: space_flag(rt, SPACE_F_HOLD, value != 0u); break;
@@ -1345,6 +1502,7 @@ static SPACE_SIZE uint32_t space_enter(SpaceRuntime *rt, uint8_t tag,
     rt->v = 1.0f;
     rt->siz = 1.0f;
     rt->d[0] = rt->d[1] = SPACE_D_MIN;
+    rt->dr[0] = rt->dr[1] = SPACE_D_MIN;
     /*
      * The dry starts at the outgoing type's (plan §5.3 step 5) and glides
      * to 0. S095 P4 (SD2): with no handoff written (OFF, boot) it starts
@@ -1358,6 +1516,11 @@ static SPACE_SIZE uint32_t space_enter(SpaceRuntime *rt, uint8_t tag,
         return 0u;
     memset(sk_state(share), 0, sizeof(space_hdr_t));
     space_hdr(share)->rnd = 0x5DE1A9u;
+    /* S100 P2: a clean shaper and record until the rows arrive. */
+    space_hdr(share)->k.dg = 1.0f;
+    space_hdr(share)->k.makeup = 1.0f;
+    space_hdr(share)->k.bsc = 1.0f;
+    space_hdr(share)->k.xa = 1.0f;
     /* The Echo's plate has no rows for these (plan §4.3). */
     space_hdr(share)->pl.dif = 0.7f;
     space_hdr(share)->pl.wid = 1.0f;
@@ -1554,31 +1717,61 @@ static __attribute__((cold)) SPACE_SIZE void space_export(const void *rt_void,
     }
 }
 
+/*
+ * `hld`'s growth (S100 P2): 6 dB a second at `fbk` 127, as the factor's
+ * first-order slope (ln 10 / 20 x 6) per second of one pass; `spr`'s
+ * darkening of the loop that runs ahead of `tim`, at full spread and full
+ * width (the playback LP's coefficient x 0.7: 16 kHz -> about 7 kHz).
+ */
+#define SPACE_HOLD_RATE      0.6907755f
+#define SPACE_HOLD_ROW       30u
+#define SPACE_SPR_DARK       0.3f
+
 /*
  * The tape's block set-up (Delay, Echo; plan §4.1).
  *
  * What:
  * 1. the time: CrumpBit's `dly` curve at the tempo (D3; only when the row
- *    or the tempo moved), R = L x spr (D23, the Delay); the tape slows to
- *    fit the longer delay (floor 1/4); wow (0.6 Hz), flutter (7 Hz) and a
- *    drift move the delays by up to +-0.5 % (R's inverted); each delay is
- *    floored at SPACE_D_MIN and clamped to the tape, and glides (150 ms),
- *    as does the speed (`spr` relative: the longer channel plays `tim`);
- * 2. the heads' gains (O6) x the valid gate; the loop's gains (`hld`: no
- *    input, unity loop, D10); the record LP and the playback bandwidth at
- *    the tape speed;
- * 3. the output ramps: dry from the outgoing type's to 0 (plan §5.3 step
+ *    or the tempo moved), the shorter channel `tim` / ratio (D23: `spr` is
+ *    relative, the longer plays `tim`); the tape slows to fit the longer
+ *    delay (floor 1/4); each delay is floored at SPACE_D_MIN and clamped
+ *    to the tape, and glides (150 ms), as does the speed; S100 P2 wow:
+ *    wow, flutter and a drift add a swing in samples set as pitch (k.aw,
+ *    k.af) after the glide, R's inverted, held within half of the delay
+ *    above the floor and inside the tape; `hld` fades the swing and reads
+ *    on a whole frame (no interpolation loss in the held loop); dr[c] is
+ *    the delay the reads used last;
+ * 2. the loop (S100 P2): the inputs (the Delay's mid and side with the
+ *    `xfb` allpass; the Echo's head at `tim`), and either
+ *    - `hld`: no input; the shaper linear (no cubic, no `deg` drive or bit
+ *      reduction) and the playback LP open, so the held loop keeps its
+ *      level; the loop gain 1 up to `fbk` row 30, above it 1 + 6 dB/s x
+ *      ((row - 30) / 97)^2 per pass, times the narrow band's worst comb
+ *      tooth loss sqrt(1 + (hq / D)^2), so it grows even at `wid` 0;
+ *    - else `fbk`, the input, `deg`'s shaper and bit reduction, and each
+ *      loop's playback LP: the tape speed, O8, and `spr`'s darkening of the
+ *      loop that runs ahead of `tim` (x (1 - 0.3 x spread x width));
+ * 3. `str` (S100 P2): the output's head and tap gains: 0 .. 64 mono to one
+ *    head a side (cm, cx), 64 .. 127: the Delay's taps in (constant power,
+ *    cn, cf at +-1/3 pan; none under `rev`), the Echo's heads wider (M/S,
+ *    the side up to x 2, constant power); all x the valid gate;
+ * 4. the output ramps: dry from the outgoing type's to 0 (plan §5.3 step
  *    5; S095 P4 T1: only the handoff uses it); wet = 1 x the ducking (O1).
- * Inputs: runtime, header, frames, nch (2 Delay, 1 Echo). Outputs: the
- * pass; v1o: the block-end speed (space_tapeEnd()).
+ * Inputs: runtime, header, frames. Outputs: the pass; v1o: the block-end
+ * speed (space_tapeEnd()).
  */
 static SPACE_SIZE void space_tapeBegin(SpaceRuntime *rt, space_hdr_t *h,
                                        space_pass_t *ps, uint32_t frames,
-                                       uint32_t nch, float *v1o)
+                                       float *v1o)
 {
     const float inv = 1.0f / (float)frames;
     const float cap = (float)(rt->tape_n - SPACE_TAPE_GUARD);
     const uint16_t bpm = seq_getBpm();
+    const uint8_t echo = (uint8_t)(rt->tag == SPACE_TAG_ECHO);
+    const float str = (float)rt->rows[(echo ? SPACE_ECH_STEREO
+                                            : SPACE_DLY_STEREO) - 3u];
+    /* The channel that runs ahead of `tim` (`spr` relative). */
+    const uint32_t sh = (h->k.spr >= 1.0f) ? 0u : 1u;
     float gate = 1.0f / 32768.0f;
     float t[2];
     float v1;
@@ -1599,39 +1792,53 @@ static SPACE_SIZE void space_tapeBegin(SpaceRuntime *rt, space_hdr_t *h,
      */
     t[0] = rt->t_samples;
     t[1] = t[0];
-    if (nch == 2u) {
-        if (h->k.spr >= 1.0f)
-            t[0] = t[1] / h->k.spr;
-        else
-            t[1] = t[0] * h->k.spr;
-    }
+    if (sh == 0u)
+        t[0] = t[1] / h->k.spr;
+    else
+        t[1] = t[0] * h->k.spr;
     v1 = cap / rt->t_samples;
     v1 = __builtin_fminf(__builtin_fmaxf(v1, SPACE_SPEED_MIN), 1.0f);
-    h->wph += 2.0f * 0.6f * SK_FRAMES_MAX / SK_FS;
+    h->wph += h->k.wr;
     h->wph -= (h->wph >= 1.0f) ? 2.0f : 0.0f;
-    h->fph += 2.0f * 7.0f * SK_FRAMES_MAX / SK_FS;
+    h->fph += h->k.fr;
     h->fph -= (h->fph >= 1.0f) ? 2.0f : 0.0f;
     h->rnd = h->rnd * 1664525u + 1013904223u;
     h->walk += 0.005f * ((float)(int32_t)h->rnd * (1.0f / 2147483648.0f) -
                          h->walk);
-    /* Parabolic sines, +-1; wow and flutter 0.6 : 0.25, plus the drift. */
-    s = (2.4f * h->wph * (1.0f - fabsf(h->wph)) +
-         h->fph * (1.0f - fabsf(h->fph)) + h->walk) * h->k.wow;
-    for (c = 0u; c < nch; c++) {
-        float d = t[c] * v1 * (c ? 1.0f - s : 1.0f + s);
-
-        d = __builtin_fminf(__builtin_fmaxf(d, SPACE_D_MIN), cap);
-        d = rt->d[c] + SPACE_GLIDE_K32 * (d - rt->d[c]);
-        ps->d[c] = rt->d[c];
-        ps->dd[c] = (d - rt->d[c]) * inv;
+    /*
+     * Parabolic sines, +-1 (x 4), in output samples, plus the drift. S100
+     * P2: the swing is added after the glide (the 150 ms glide had filtered
+     * it: 0.39 of a 2.5 Hz wow, 0.09 of 12 Hz flutter). Under `hld` the
+     * swing fades (x 0.9 a block) and the delay settles on a whole frame,
+     * so the held loop's reads are exact.
+     */
+    s = 4.0f * (h->wph * (1.0f - fabsf(h->wph)) * h->k.aw +
+                h->fph * (1.0f - fabsf(h->fph)) * h->k.af) +
+        h->walk * h->k.aw;
+    for (c = 0u; c < 2u; c++) {
+        float b = __builtin_fminf(__builtin_fmaxf(t[c] * v1, SPACE_D_MIN), cap);
+        float sw;
+        float d;
+
+        sw = (rt->flags & SPACE_F_HOLD) ? 0.9f * (rt->dr[c] - rt->d[c])
+                                        : (c ? -s : s) * v1;
+        b = rt->d[c] + SPACE_GLIDE_K32 * (b - rt->d[c]);
+        sw = __builtin_fminf(__builtin_fmaxf(sw, 0.5f * (SPACE_D_MIN - b)),
+                             __builtin_fminf(0.5f * (b - SPACE_D_MIN), cap - b));
+        d = b + sw;
+        if (rt->flags & SPACE_F_HOLD)
+            d = (float)(int32_t)(d + 0.5f);
+        ps->d[c] = rt->dr[c];
+        ps->dd[c] = (d - ps->d[c]) * inv;
         /*
          * The heads sound once the tape holds them: valid covers the delay
          * and the read's next frame (§S18: the old `+ 4` never opened at
          * the longest delays, where valid saturates at the tape length).
          */
-        if ((float)rt->valid < __builtin_fmaxf(d, rt->d[c]) + 2.0f)
+        if ((float)rt->valid < __builtin_fmaxf(d, ps->d[c]) + 2.0f)
             gate = 0.0f;
-        rt->d[c] = d;
+        rt->d[c] = b;
+        rt->dr[c] = d;
     }
     v1 = rt->v + SPACE_GLIDE_K32 * (v1 - rt->v);
     ps->p = rt->pos;
@@ -1639,14 +1846,81 @@ static SPACE_SIZE void space_tapeBegin(SpaceRuntime *rt, space_hdr_t *h,
     ps->n = (float)rt->tape_n;
     ps->ph[0] = rt->rph[0];
     ps->dph[0] = 2.0f * ps->va / __builtin_fmaxf(ps->d[0] - SPACE_D_MIN, 1.0f);
-    ps->g1 = gate * h->k.hg[0];
-    ps->g2 = gate * h->k.hg[1];
-    ps->g3 = gate * h->k.hg[2];
-    ps->fbk = (rt->flags & SPACE_F_HOLD) ? 1.0f : h->k.fbk;
-    ps->ing = (rt->flags & SPACE_F_HOLD) ? 0.0f : 1.0f;
-    ps->xfb = h->k.xfb;
-    ps->bw = __builtin_fminf(h->k.bw * ps->va, h->k.tlp);
     ps->rk = 0.25f + 0.75f * ps->va;
+    /* 2. The loop. */
+    ps->kc = echo ? gate : 0.5f * gate;
+    ps->kx = echo ? 0.0f : h->k.xa;
+    if (rt->flags & SPACE_F_HOLD) {
+        const uint8_t row = rt->rows[SPACE_DLY_FEEDBACK - 3u];
+        float gh = 1.0f;
+
+        if (row > SPACE_HOLD_ROW) {
+            const float x = (float)(row - SPACE_HOLD_ROW) * (1.0f / 97.0f);
+            /* One pass in output frames, and the half comb-tooth term. */
+            const float dp = __builtin_fmaxf(rt->d[0], rt->d[1]) / ps->va;
+            const float q = h->k.hq / dp;
+
+            gh = (1.0f + SPACE_HOLD_RATE * x * x * dp * (1.0f / SK_FS)) *
+                 sqrtf(1.0f + q * q);
+        }
+        ps->fbk = gh;
+        ps->ing = 0.0f;
+        ps->bw[0] = ps->bw[1] = 1.0f;
+        ps->c3 = 0.0f;
+        ps->drv = h->k.kb;
+        ps->mk = 1.0f;
+        ps->bsc = 1.0f;
+        ps->bsh = 0u;
+    } else {
+        const float bw = h->k.bw * ps->va;
+        const float dev = __builtin_fminf(
+            fabsf((float)rt->rows[SPACE_DLY_SPREAD - 3u] - 63.0f) *
+                (1.0f / 63.0f), 1.0f);
+        const float dk = 1.0f - SPACE_SPR_DARK * dev *
+                                    (float)rt->rows[SPACE_DLY_WIDTH - 3u] *
+                                    (1.0f / 127.0f);
+
+        ps->bw[0] = bw * (1.0f - h->k.comp * fabsf(h->ch[0].play)) *
+                    ((sh == 0u) ? dk : 1.0f);
+        ps->bw[1] = bw * (1.0f - h->k.comp * fabsf(h->ch[1].play)) *
+                    ((sh == 1u) ? dk : 1.0f);
+        ps->fbk = h->k.fbk;
+        ps->ing = 1.0f;
+        ps->c3 = 1.0f / 3.0f;
+        ps->drv = h->k.drive;
+        ps->mk = h->k.makeup;
+        ps->bsc = h->k.bsc;
+        ps->bsh = h->k.bsh;
+    }
+    /* 3. `str`. */
+    if (str <= 64.0f) {
+        const float a = str * (1.0f / 64.0f);
+
+        ps->cm = gate * 0.5f * (1.0f + a);
+        ps->cx = gate * 0.5f * (1.0f - a);
+        ps->cn = 0.0f;
+        ps->cf = 0.0f;
+    } else if (echo) {
+        /* The Echo: no taps; wider than one head a side (M/S, side x 2). */
+        const float a = 1.0f + (str - 64.0f) * (1.0f / 63.0f);
+        const float cm = 0.5f * (1.0f + a);
+        const float cx = 0.5f * (1.0f - a);
+        const float nr = gate / sqrtf(cm * cm + cx * cx);
+
+        ps->cm = cm * nr;
+        ps->cx = cx * nr;
+        ps->cn = 0.0f;
+        ps->cf = 0.0f;
+    } else {
+        const float u = (rt->flags & SPACE_F_REVERSE)
+                            ? 0.0f : (str - 64.0f) * (1.0f / 63.0f);
+        const float nr = gate / sqrtf(1.0f + u * u);
+
+        ps->cm = nr;
+        ps->cx = 0.0f;
+        ps->cn = 0.8660254f * u * nr;
+        ps->cf = 0.5f * u * nr;
+    }
     /*
      * S095 P4 T1: the dry glides to 0 (one-pole, 0.1 per block, as before;
      * -20 dB after about 22 blocks, 16 ms) and the wet is the ducking alone.
@@ -1712,7 +1986,7 @@ static SPACE_SIZE void space_delayProcess(void *rt_void, effect_io_t *io)
     if (!space_ioOk(rt, io) || rt->tape_n < 2u * (uint32_t)SPACE_D_MIN)
         return;
     h = space_hdr(io->share);
-    space_tapeBegin(rt, h, &ps, io->frames, 2u, &v1);
+    space_tapeBegin(rt, h, &ps, io->frames, &v1);
     peak = space_delayRun((uint32_t *)(void *)(io->share->base +
                                                SK_PREFIX_BYTES),
                           h, &ps, (uint8_t)(rt->flags & SPACE_F_REVERSE),
@@ -1856,13 +2130,15 @@ static SPACE_SIZE void space_reverbProcess(void *rt_void, effect_io_t *io)
 
 /*
  * The Echo's output: the plate (quarter rate) to full rate by linear
- * interpolation (four frames per tick), blended with the echo, wet =
- * e + bln (plate - e), and mixed in, both gains ramped. Also returns the
+ * interpolation (four frames per tick), blended with the echo (S100 P2:
+ * L and R, `str`), wet = e + bln (plate - e), and mixed in, both gains
+ * ramped. Also returns the
  * plate's mono at its rate into pm[ticks] (the loop routing's input next
  * block).
  */
-static SPACE_LOOP void space_echoOut(float *l, float *r, const float *e,
-                                     const float *ol, const float *orr,
+static SPACE_LOOP void space_echoOut(float *l, float *r, const float *el,
+                                     const float *er, const float *ol,
+                                     const float *orr,
                                      float *prev, float bln, float *pm,
                                      float dry, float ddry, float wg,
                                      float dwg, uint32_t ticks)
@@ -1879,12 +2155,13 @@ static SPACE_LOOP void space_echoOut(float *l, float *r, const float *e,
 
         for (j = 0u; j < 4u; j++) {
             const uint32_t n = 4u * t + j;
-            const float en = e[n];
+            const float enl = el[n];
+            const float enr = er[n];
 
             al += dl;
             ar += dr;
-            l[n] = l[n] * dry + (en + bln * (al - en)) * wg;
-            r[n] = r[n] * dry + (en + bln * (ar - en)) * wg;
+            l[n] = l[n] * dry + (enl + bln * (al - enl)) * wg;
+            r[n] = r[n] * dry + (enr + bln * (ar - enr)) * wg;
             dry += ddry;
             wg += dwg;
         }
@@ -1911,7 +2188,10 @@ static SPACE_SIZE void space_echoProcess(void *rt_void, effect_io_t *io)
     space_pass_t ps;
     float m[SK_FRAMES_MAX];
     float ti[SK_FRAMES_MAX];
-    float e[SK_FRAMES_MAX];
+    float el[SK_FRAMES_MAX];
+    float er[SK_FRAMES_MAX];
+    float *e0 = el;
+    float *e1 = er;
     float ol[SK_FRAMES_MAX / 4u];
     float orr[SK_FRAMES_MAX / 4u];
     int16_t *ring;
@@ -1927,7 +2207,22 @@ static SPACE_SIZE void space_echoProcess(void *rt_void, effect_io_t *io)
     ring = (int16_t *)(void *)(io->share->base + SK_PREFIX_BYTES);
     e2r = h->pl.e2r;
     peak = space_monoPeak(io->l, io->r, m, io->frames);
-    space_tapeBegin(rt, h, &ps, io->frames, 1u, &v1);
+    space_tapeBegin(rt, h, &ps, io->frames, &v1);
+    /*
+     * S100 P2: the run takes the long head (`tim`, the loop's) first; with
+     * L the short one (`spr` above 63) the heads and the outputs swap.
+     */
+    if (h->k.spr >= 1.0f) {
+        float x = ps.d[0];
+
+        ps.d[0] = ps.d[1];
+        ps.d[1] = x;
+        x = ps.dd[0];
+        ps.dd[0] = ps.dd[1];
+        ps.dd[1] = x;
+        e0 = er;
+        e1 = el;
+    }
     /*
      * The tape's input takes the plate's last mono block (r>e, loop); the
      * plate's input is built by the tape pass (e>r: e2r x echo + 0.1 x in;
@@ -1941,11 +2236,11 @@ static SPACE_SIZE void space_echoProcess(void *rt_void, effect_io_t *io)
                           : ((route == SPACE_ROUTING_LOOP) ? 1.0f : 0.0f),
                       (route == SPACE_ROUTING_E2R) ? 0.1f
                           : ((route == SPACE_ROUTING_LOOP) ? 0.0f : 1.0f),
-                      e, io->frames);
+                      e0, e1, io->frames);
     ticks = space_plateBlock(rt, &h->pl, ring, NULL, 1u, ti, NULL, ol, orr,
                              io->frames);
     space_tapeEnd(rt, &ps, io->frames, v1, 2.0f * peak);
-    space_echoOut(io->l, io->r, e, ol, orr, h->pl.up, h->pl.bln,
+    space_echoOut(io->l, io->r, el, er, ol, orr, h->pl.up, h->pl.bln,
                   h->pl.sh, ps.dry, ps.ddry, ps.wg, ps.dwg, ticks);
     /* S095 P4 T1: the outgoing tail is heard at full wet (no `mix` row). */
     sk_feedMix(io->share, EFFECT_TYPE_ECHO, io->l, io->r, io->frames,
diff --git a/Core/DSP/Effects/Space/SpaceEffect.h b/Core/DSP/Effects/Space/SpaceEffect.h
index de2a0024..1357ed28 100644
--- a/Core/DSP/Effects/Space/SpaceEffect.h
+++ b/Core/DSP/Effects/Space/SpaceEffect.h
@@ -59,7 +59,9 @@
  * - the tape (Delay, Echo): tape_n (frames, without the 3 guard frames),
  *   valid (frames written since the seat, saturating), pos (the write
  *   position in frames, fractional: the tape runs at speed v), d[c] (each
- *   channel's delay in tape frames), v (speed), rph[c] (the reverse head's phase 0..1);
+ *   channel's delay in tape frames, after the glide), v (speed), rph[c] (the reverse head's phase 0..1),
+ *   dr[c] (S100 P2: the delay the reads used at the block end: d[c] after the glide plus
+ *   the wow swing, whole under `hld`; dr - d is the swing);
  * - wet (the wet gain the next block ramps from: the ducking, and on the
  *   Reverb the gate/bloom; S095 P4: no `mix` row, so 1 is the top),
  *   dry (the dry gain, gliding from the outgoing type's to 0; it exists
@@ -83,6 +85,7 @@ typedef struct {
     float    d[2];
     float    v;
     float    rph[2];
+    float    dr[2];
     float    wet;
     float    dry;
     float    duck;
diff --git a/Core/DSP/Effects/Space/SpaceParameters.c b/Core/DSP/Effects/Space/SpaceParameters.c
index 3172144e..b737fad2 100644
--- a/Core/DSP/Effects/Space/SpaceParameters.c
+++ b/Core/DSP/Effects/Space/SpaceParameters.c
@@ -47,7 +47,8 @@ const char space_echoFull8[]   = "Echo";
  * Delay rows (plan §4.1; D3, D4, D5, D10, D23; options O1, O3, O4, O6, O8,
  * O9). 100 % wet since S095 P4 T1 (the Effect is a send; the level is the
  * common `vol`). Defaults: a musical starting point (time row 64 = a
- * StepScale stop, feedback 64, flat tone, clean tape).
+ * StepScale stop, feedback 64, the loop band-pass at full width, clean
+ * tape). S100 P2: SELECT 2 is tim fbk ton wid; `str` replaces `hed`.
  */
 const effect_param_descriptor_t space_delayDescriptors[] = {
     EFFECT_COMMON_ROWS(INSTRUMENT_PARAM_FLAG_AUTOMATABLE,
@@ -56,16 +57,19 @@ const effect_param_descriptor_t space_delayDescriptors[] = {
     EFFECT_ROW_FLAGS("dly_time", CAT_DELAY, LONG_TIME, SHORT_MICROTIMING, DTYPE_0B127,
                      EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 64u, 127u,
                      EFFECT_PARAM_FLAG_BUFFER_DEPENDENT),
+    EFFECT_ROW("dly_feedback", CAT_DELAY, LONG_FEEDBACK, SHORT_FBK, DTYPE_0B127,
+               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 64u, 127u),
+    /* S100 P2: the loop band-pass's centre and width (127 = full band). */
+    EFFECT_ROW("dly_tone", CAT_DELAY, LONG_TONE, SHORT_TON, DTYPE_0B127,
+               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 63u, 127u),
+    EFFECT_ROW("dly_width", CAT_DELAY, LONG_WIDTH, SHORT_WID, DTYPE_0B127,
+               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 127u, 127u),
     /* D23: continuous R/L time ratio 1/2..2 (63 = equal). */
     EFFECT_ROW("dly_spread", CAT_DELAY, LONG_SPREAD, SHORT_SPR, DTYPE_PM63,
                EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 63u, 127u),
-    EFFECT_ROW("dly_feedback", CAT_DELAY, LONG_FEEDBACK, SHORT_FBK, DTYPE_0B127,
-               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 64u, 127u),
     /* D23: continuous L/R feedback cross-feed (0 stereo .. 127 ping-pong). */
     EFFECT_ROW("dly_xfeed", CAT_DELAY, LONG_XFEED, SHORT_XFB, DTYPE_0B127,
                EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 0u, 127u),
-    EFFECT_ROW("dly_tone", CAT_DELAY, LONG_TONE, SHORT_TON, DTYPE_PM63,
-               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 63u, 127u),
     EFFECT_ROW("dly_degrade", CAT_DELAY, LONG_DEGRADE, SHORT_DEG, DTYPE_0B127,
                EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 0u, 127u),
     EFFECT_ROW("dly_wow", CAT_DELAY, LONG_WOWFLUTR, SHORT_WOW, DTYPE_0B127,
@@ -74,11 +78,9 @@ const effect_param_descriptor_t space_delayDescriptors[] = {
                INSTRUMENT_PARAM_FLAG_AUTOMATABLE, EFFECT_MOD_NONE, 0u, 1u),
     EFFECT_ROW("dly_reverse", CAT_DELAY, LONG_REVERSE, SHORT_REV, DTYPE_ON_OFF,
                INSTRUMENT_PARAM_FLAG_AUTOMATABLE, EFFECT_MOD_NONE, 0u, 1u),
-    /* S095 P4: seven stops 0..6, one per detent (A B C AB AC BC ABC). */
-    EFFECT_ROW("dly_heads", CAT_DELAY, LONG_HEADS, SHORT_HED, DTYPE_0B127,
-               INSTRUMENT_PARAM_FLAG_MORPHABLE |
-               INSTRUMENT_PARAM_FLAG_AUTOMATABLE,
-               EFFECT_MOD_NONE, SPACE_HEADS_DEFAULT, SPACE_HEADS_STOPS - 1u),
+    /* S100 P2: the stereo field (0 mono, 64 one head a side, 127 + taps). */
+    EFFECT_ROW("dly_stereo", CAT_DELAY, LONG_STEREO, SHORT_STR, DTYPE_0B127,
+               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 64u, 127u),
     EFFECT_ROW("dly_duck", CAT_DELAY, LONG_DUCKING, SHORT_DCK, DTYPE_0B127,
                EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 0u, 127u),
 };
@@ -146,7 +148,12 @@ const effect_param_descriptor_t space_echoDescriptors[] = {
                      EFFECT_PARAM_FLAG_BUFFER_DEPENDENT),
     EFFECT_ROW("ech_feedback", CAT_ECHO, LONG_FEEDBACK, SHORT_FBK, DTYPE_0B127,
                EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 64u, 127u),
-    EFFECT_ROW("ech_tone", CAT_ECHO, LONG_TONE, SHORT_TON, DTYPE_PM63,
+    EFFECT_ROW("ech_tone", CAT_ECHO, LONG_TONE, SHORT_TON, DTYPE_0B127,
+               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 63u, 127u),
+    EFFECT_ROW("ech_width", CAT_ECHO, LONG_WIDTH, SHORT_WID, DTYPE_0B127,
+               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 127u, 127u),
+    /* S100 P2: the R head's time (the Delay's `spr` law). */
+    EFFECT_ROW("ech_spread", CAT_ECHO, LONG_SPREAD, SHORT_SPR, DTYPE_PM63,
                EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 63u, 127u),
     EFFECT_ROW("ech_degrade", CAT_ECHO, LONG_DEGRADE, SHORT_DEG, DTYPE_0B127,
                EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 0u, 127u),
@@ -162,11 +169,8 @@ const effect_param_descriptor_t space_echoDescriptors[] = {
                EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 63u, 127u),
     EFFECT_ROW("ech_high_shelf", CAT_PLATE, LONG_HIGHDAMP, SHORT_HSH, DTYPE_0B127,
                EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 40u, 127u),
-    /* S095 P4: seven stops 0..6, one per detent (A B C AB AC BC ABC). */
-    EFFECT_ROW("ech_heads", CAT_ECHO, LONG_HEADS, SHORT_HED, DTYPE_0B127,
-               INSTRUMENT_PARAM_FLAG_MORPHABLE |
-               INSTRUMENT_PARAM_FLAG_AUTOMATABLE,
-               EFFECT_MOD_NONE, SPACE_HEADS_DEFAULT, SPACE_HEADS_STOPS - 1u),
+    EFFECT_ROW("ech_stereo", CAT_ECHO, LONG_STEREO, SHORT_STR, DTYPE_0B127,
+               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 64u, 127u),
     EFFECT_ROW("ech_duck", CAT_ECHO, LONG_DUCKING, SHORT_DCK, DTYPE_0B127,
                EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 0u, 127u),
     /* S095 P4: three stops 0..2 (e>r r>e lp). */
@@ -199,23 +203,22 @@ _Static_assert(SPACE_ECHO_PARAM_ENUM_COUNT == SPACE_ECHO_PARAM_COUNT,
  *
  * S095 P4 (T2, user 2026-11-13): only the rows that need a label to be
  * played keep one: `tim` (the synced stops and the clamped row), and the
- * stepped rows `hed`, `frz`, `rtg` (names, not numbers). `spr`, `fbk`,
+ * stepped rows `frz`, `rtg` (names, not numbers; `hed` until S100 P2). `spr`, `fbk`,
  * `xfb`, `dck`, `pre`, `dcy`, `int`, `dtn`, `env` show plain numbers; their
  * laws are unchanged, so the number is a position on the same curve.
  */
 enum {
     SPACE_TXT_NONE = 0,
     SPACE_TXT_TIME,      /* `tim`: the row the DSP plays, CrumpBit's labels (D3) */
-    SPACE_TXT_HEADS,     /* `hed`: the head set (O6), letters (S095 P4) */
     SPACE_TXT_FREEZE,    /* `frz`: off / on / shm (O12) */
     SPACE_TXT_ROUTE      /* `rtg`: e>r / r>e / lp (O16) */
 };
 
-/* tim spr fbk xfb ton deg wow hld rev hed dck */
+/* tim fbk ton wid spr xfb deg wow hld rev str dck (S100 P2) */
 static const uint8_t space_delayText[SPACE_DELAY_PARAM_COUNT - 3u] = {
     SPACE_TXT_TIME, SPACE_TXT_NONE, SPACE_TXT_NONE, SPACE_TXT_NONE,
     SPACE_TXT_NONE, SPACE_TXT_NONE, SPACE_TXT_NONE, SPACE_TXT_NONE,
-    SPACE_TXT_NONE, SPACE_TXT_HEADS, SPACE_TXT_NONE
+    SPACE_TXT_NONE, SPACE_TXT_NONE, SPACE_TXT_NONE, SPACE_TXT_NONE
 };
 
 /* pre siz dcy dif lsh lsf hsh hsf mod wid frz shm int dtn env dck ton */
@@ -227,28 +230,18 @@ static const uint8_t space_reverbText[SPACE_REVERB_PARAM_COUNT - 3u] = {
     SPACE_TXT_NONE
 };
 
-/* tim fbk ton deg wow e>r bln dcy lsh hsh hed dck rtg */
+/* tim fbk ton wid spr deg wow e>r bln dcy lsh hsh str dck rtg (S100 P2) */
 static const uint8_t space_echoText[SPACE_ECHO_PARAM_COUNT - 3u] = {
     SPACE_TXT_TIME, SPACE_TXT_NONE, SPACE_TXT_NONE, SPACE_TXT_NONE,
     SPACE_TXT_NONE, SPACE_TXT_NONE, SPACE_TXT_NONE, SPACE_TXT_NONE,
-    SPACE_TXT_NONE, SPACE_TXT_NONE, SPACE_TXT_HEADS, SPACE_TXT_NONE,
-    SPACE_TXT_ROUTE
+    SPACE_TXT_NONE, SPACE_TXT_NONE, SPACE_TXT_NONE, SPACE_TXT_NONE,
+    SPACE_TXT_NONE, SPACE_TXT_NONE, SPACE_TXT_ROUTE
 };
 
 /* The stepped rows' names (O12, O16). */
 static const char space_frzNames[3][4] = { "off", " on", "shm" };
 static const char space_rtgNames[3][4] = { "e>r", "r>e", "lp " };
 
-/*
- * The seven `hed` stops in knob order (O6; S095 P4 Q3: letters, one per
- * head, A = D/3, B = 2D/3, C = D, so a label cannot be read as a number or
- * a knob position). The order is the one §S18 settled: single heads, then
- * pairs, then all three.
- */
-static const char space_hedNames[SPACE_HEADS_STOPS][4] = {
-    "  A", "  B", "  C", " AB", " AC", " BC", "ABC"
-};
-
 /*
  * The value text of a Space row (S095 P1 plan §6 item 8; S095 P4 T2).
  * What: out[0..2] for the row's kind (above); the stepped kinds also when
@@ -282,9 +275,6 @@ static __attribute__((optimize("Os"))) uint8_t space_formatValue(const uint8_t *
         crumpBit_formatDelayRow((value > limit) ? limit : value, out);
         return 1u;
     }
-    case SPACE_TXT_HEADS:
-        memcpy(out, space_hedNames[space_step(value, SPACE_HEADS_STOPS)], 3u);
-        return 1u;
     case SPACE_TXT_FREEZE:
         memcpy(out, space_frzNames[space_step(value, 3u)], 3u);
         return 1u;
diff --git a/Core/DSP/Effects/Space/SpaceParameters.h b/Core/DSP/Effects/Space/SpaceParameters.h
index 36ccd5fc..d9b10a40 100644
--- a/Core/DSP/Effects/Space/SpaceParameters.h
+++ b/Core/DSP/Effects/Space/SpaceParameters.h
@@ -38,19 +38,20 @@
  */
 typedef enum {
     SPACE_DLY_TIME = EFFECT_COMMON_PARAM_COUNT,  /* 3  CrumpBit's `dly` curve, share-truncated (D3) */
-    SPACE_DLY_SPREAD,     /* 4  R/L time ratio 2^((spr-63)/63); the longer is `tim` */
-    SPACE_DLY_FEEDBACK,   /* 5  0..1.20, unity at 100 (S095 P4) */
-    SPACE_DLY_XFEED,      /* 6  L/R feedback cross-feed, continuous (D23) */
-    SPACE_DLY_TONE,       /* 7  loop tone: LP < 63 = flat < HP */
-    SPACE_DLY_DEGRADE,    /* 8  drive, head bandwidth, hiss (D5) */
-    SPACE_DLY_WOW,        /* 9  wow + flutter + drift depth */
-    SPACE_DLY_HOLD,       /* 10 on: input 0, loop gain 1 (D10) */
-    SPACE_DLY_REVERSE,    /* 11 reverse playback head (O4) */
-    SPACE_DLY_HEADS,      /* 12 seven head sets, stepped 0..6 (O6, S095 P4) */
-    SPACE_DLY_DUCK,       /* 13 wet ducked by the input (O1) */
+    SPACE_DLY_FEEDBACK,   /* 4  0..1.20, unity at 100 (S095 P4) */
+    SPACE_DLY_TONE,       /* 5  loop band-pass centre 80 Hz..12 kHz (S100 P2) */
+    SPACE_DLY_WIDTH,      /* 6  loop band-pass width: 0 resonant .. 127 full band */
+    SPACE_DLY_SPREAD,     /* 7  R/L time ratio 2^((spr-63)/63); the longer is `tim` */
+    SPACE_DLY_XFEED,      /* 8  side allpass: 0 stereo .. 127 ping-pong, mid kept (S100 P2) */
+    SPACE_DLY_DEGRADE,    /* 9  drive, head bandwidth, hiss (D5) */
+    SPACE_DLY_WOW,        /* 10 wow + flutter + drift depth */
+    SPACE_DLY_HOLD,       /* 11 on: input 0, the held loop's gain from `fbk` (D10, S100 P2) */
+    SPACE_DLY_REVERSE,    /* 12 reverse playback head (O4) */
+    SPACE_DLY_STEREO,     /* 13 `str`: 0 mono, 64 one head a side, 127 + taps (S100 P2) */
+    SPACE_DLY_DUCK,       /* 14 wet ducked by the input (O1) */
     SPACE_DELAY_PARAM_ENUM_COUNT
 } space_delay_param_t;
-#define SPACE_DELAY_PARAM_COUNT   14u
+#define SPACE_DELAY_PARAM_COUNT   15u
 
 typedef enum {
     SPACE_RVB_PREDELAY = EFFECT_COMMON_PARAM_COUNT,  /* 3  0..1.4 s, share-truncated */
@@ -77,25 +78,27 @@ typedef enum {
 typedef enum {
     SPACE_ECH_TIME = EFFECT_COMMON_PARAM_COUNT,  /* 3  the Delay's law, mono tape */
     SPACE_ECH_FEEDBACK,   /* 4  as the Delay's */
-    SPACE_ECH_TONE,       /* 5 */
-    SPACE_ECH_DEGRADE,    /* 6 */
-    SPACE_ECH_WOW,        /* 7 */
-    SPACE_ECH_TO_REVERB,  /* 8  `e>r`: the amount between tape and plate */
-    SPACE_ECH_BLEND,      /* 9  `bln`: echo .. plate in the wet */
-    SPACE_ECH_DECAY,      /* 10 plate RT60 */
-    SPACE_ECH_LOW_SHELF,  /* 11 */
-    SPACE_ECH_HIGH_SHELF, /* 12 */
-    SPACE_ECH_HEADS,      /* 13 (O15), stepped 0..6 */
-    SPACE_ECH_DUCK,       /* 14 (O14) */
-    SPACE_ECH_ROUTING,    /* 15 e>r / r>e / plate in the loop, stepped 0..2 (O16) */
+    SPACE_ECH_TONE,       /* 5  as the Delay's (S100 P2) */
+    SPACE_ECH_WIDTH,      /* 6  as the Delay's (S100 P2) */
+    SPACE_ECH_SPREAD,     /* 7  the R head's time, as the Delay's `spr` (S100 P2) */
+    SPACE_ECH_DEGRADE,    /* 8 */
+    SPACE_ECH_WOW,        /* 9 */
+    SPACE_ECH_TO_REVERB,  /* 10 `e>r`: the amount between tape and plate */
+    SPACE_ECH_BLEND,      /* 11 `bln`: echo .. plate in the wet */
+    SPACE_ECH_DECAY,      /* 12 plate RT60 */
+    SPACE_ECH_LOW_SHELF,  /* 13 */
+    SPACE_ECH_HIGH_SHELF, /* 14 */
+    SPACE_ECH_STEREO,     /* 15 `str`, as the Delay's (S100 P2) */
+    SPACE_ECH_DUCK,       /* 16 (O14) */
+    SPACE_ECH_ROUTING,    /* 17 e>r / r>e / plate in the loop, stepped 0..2 (O16) */
     SPACE_ECHO_PARAM_ENUM_COUNT
 } space_echo_param_t;
-#define SPACE_ECHO_PARAM_COUNT    16u
+#define SPACE_ECHO_PARAM_COUNT    18u
 
 /*
  * Stepped rows' values (the DSP and the labels read the same codes).
  *
- * S095 P4 (T2): `hed`, `frz` and `rtg` store the stop itself (0..6, 0..2),
+ * S095 P4 (T2): `frz` and `rtg` (and `hed` until S100 P2) store the stop itself (0..6, 0..2),
  * one stop per knob detent, as CrumpBit's `sub` and `typ` do. S095 P1 §S18
  * had stored them 0..127 in equal zones (about 18 detents per stop); the
  * label then had no relation to the knob position. A value above the last
@@ -107,8 +110,6 @@ typedef enum {
 #define SPACE_FREEZE_ON           1u
 #define SPACE_FREEZE_SHIMMER      2u   /* O12 */
 #define SPACE_FREEZE_STOPS        3u
-#define SPACE_HEADS_STOPS         7u   /* hed: A B C AB AC BC ABC */
-#define SPACE_HEADS_DEFAULT       2u   /* "  C", D only */
 #define SPACE_ROUTING_E2R         0u   /* echo into plate (§4.3) */
 #define SPACE_ROUTING_R2E         1u   /* plate into echo */
 #define SPACE_ROUTING_LOOP        2u   /* plate inside the echo loop */
diff --git a/Core/Menu/MenuLabels.c b/Core/Menu/MenuLabels.c
index eb736ba1..73e19e06 100644
--- a/Core/Menu/MenuLabels.c
+++ b/Core/Menu/MenuLabels.c
@@ -73,7 +73,7 @@ const char shortNames[][4] = {
     {"sdt"},{"sto"},{"tnA"},{"tnB"},{"tnC"},{"tnD"},{"teA"},{"teB"},{"teC"},
     {"teD"},{"wid"},{"bof"},{"biv"},{"fbk"},{"sub"},{"spn"},{"aux"},{"p2"},
     {"p3"},{"den"},{"lng"},{"bal"},{"dmp"},{"xfb"},{"deg"},{"wow"},{"hld"},
-    {"rev"},{"hed"},{"dck"},{"siz"},{"dcy"},{"dif"},{"lsh"},{"lsf"},{"hsh"},
+    {"rev"},{"str"},{"dck"},{"siz"},{"dcy"},{"dif"},{"lsh"},{"lsf"},{"hsh"},
     {"hsf"},{"frz"},{"shm"},{"int"},{"e>r"},{"bln"},
     /* S099 P1: the chorus rows; S099 P2: fx rte dpt mix (the effect's; csr,
      * cbw, cdv and cwd removed) */
@@ -176,7 +176,7 @@ const char longNames[][10] = {
     {"Envelope"},{"BitOff"},{"BitInv"},{"Rate"},{"SubType"},{"Switch"},
     {"SubPan"},{"Aux"},{"Pitch2"},{"Pitch3"},{"Density"},{"Balance"},
     {"FinePtch"},{"Damping"},{"XFeed"},{"Degrade"},{"WowFlutr"},{"Hold"},
-    {"Reverse"},{"Heads"},{"Ducking"},{"PreDelay"},{"Size"},{"Diffusn"},
+    {"Reverse"},{"Stereo"},{"Ducking"},{"PreDelay"},{"Size"},{"Diffusn"},
     {"LowShelf"},{"LowFreq"},{"HighDamp"},{"HighFreq"},{"Modulatn"},
     {"Freeze"},{"Interval"},{"GateBlom"},{"EchoRvb"},{"Blend"},
     /* S099 P1: the chorus rows */
diff --git a/Core/Menu/menu.h b/Core/Menu/menu.h
index c6c7ae8c..eedc8626 100644
--- a/Core/Menu/menu.h
+++ b/Core/Menu/menu.h
@@ -273,7 +273,7 @@ enum longNamesEnum {
     LONG_BITINV, LONG_RATE, LONG_SUBTYPE, LONG_SWITCH, LONG_SUBPAN, LONG_AUX,
     LONG_PITCH2, LONG_PITCH3, LONG_DENSITY, LONG_BALANCE, LONG_FINEPTCH,
     LONG_DAMPING, LONG_XFEED, LONG_DEGRADE, LONG_WOWFLUTR, LONG_HOLD,
-    LONG_REVERSE, LONG_HEADS, LONG_DUCKING, LONG_PREDELAY, LONG_SIZE,
+    LONG_REVERSE, LONG_STEREO, LONG_DUCKING, LONG_PREDELAY, LONG_SIZE,
     LONG_DIFFUSN, LONG_LOWSHELF, LONG_LOWFREQ, LONG_HIGHDAMP, LONG_HIGHFREQ,
     LONG_MODULATN, LONG_FREEZE, LONG_INTERVAL, LONG_GATEBLOM, LONG_ECHORVB,
     LONG_BLEND,
@@ -346,7 +346,7 @@ enum shortNamesEnum {
     SHORT_TED, SHORT_WID, SHORT_BOF, SHORT_BIV, SHORT_FBK, SHORT_SUB,
     SHORT_SPN, SHORT_AUX, SHORT_P2, SHORT_P3, SHORT_DEN, SHORT_LNG,
     SHORT_BAL, SHORT_DMP, SHORT_XFB, SHORT_DEG, SHORT_WOW, SHORT_HLD,
-    SHORT_REV, SHORT_HED, SHORT_DCK, SHORT_SIZ, SHORT_DCY, SHORT_DIF,
+    SHORT_REV, SHORT_STR, SHORT_DCK, SHORT_SIZ, SHORT_DCY, SHORT_DIF,
     SHORT_LSH, SHORT_LSF, SHORT_HSH, SHORT_HSF, SHORT_FRZ, SHORT_SHM,
     SHORT_INT, SHORT_E_TO_R, SHORT_BLN,
     /* S099 P1: the chorus rows; S099 P2: the effect's fx rte dpt mix (were
diff --git a/tools/dsp_test/Makefile b/tools/dsp_test/Makefile
index 9ab3264e..903b278b 100644
--- a/tools/dsp_test/Makefile
+++ b/tools/dsp_test/Makefile
@@ -1625,10 +1625,10 @@ space: crumpbit
 	  --macro EFFECT_TYPE_ECHO --out $(GEN)/sp_types.c
 	$(EX) --src $(ROOT)/Core/DSP/Effects/Space/SpaceParameters.h \
 	  --typedef space_delay_param_t --typedef space_reverb_param_t \
-	  --typedef space_echo_param_t --macro SPACE_HEADS_STOPS \
+	  --typedef space_echo_param_t \
 	  --macro SPACE_FREEZE_SHIMMER --macro SPACE_FREEZE_OFF \
 	  --macro SPACE_FREEZE_STOPS --macro SPACE_ROUTING_STOPS \
-	  --macro SPACE_HEADS_DEFAULT --macro SPACE_ROUTING_E2R \
+	  --macro SPACE_ROUTING_E2R \
 	  --macro SPACE_ROUTING_R2E --macro SPACE_ROUTING_LOOP \
 	  --macro SPACE_INTERVAL_ROW_MIN --macro SPACE_INTERVAL_ROW_MAX \
 	  --out $(GEN)/sp_params.c
@@ -1638,9 +1638,9 @@ space: crumpbit
 	  --out $(GEN)/SpaceEffect.host.c
 	$(HOSTCC) $(HOSTFLAGS) modules/space.c -o build/space -lm
 	./build/space
-	@# S096 P2 F1: the stereo and mono linear reads without their fraction
-	@# (back to a frame-stepped read) must fail SP11.
-	mkdir -p build/break_sp_lin2 build/break_sp_lin1
+	@# S096 P2 F1: the stereo linear read without its fraction (back to a
+	@# frame-stepped read) must fail SP11 (S100 P2: the mono read is gone).
+	mkdir -p build/break_sp_lin2
 	sed 's/\*l = al + f \* /*l = al + 0.0f * /' \
 	  $(GEN)/SpaceEffect.host.c > build/break_sp_lin2/SpaceEffect.host.c
 	@grep -q '\*l = al + 0.0f \* ' build/break_sp_lin2/SpaceEffect.host.c || \
@@ -1649,14 +1649,79 @@ space: crumpbit
 	@if ./build/space_break_lin2 >/dev/null 2>&1; then \
 	  echo "space_break_lin2: FAILED (a frame-stepped Delay head not caught)"; exit 1; \
 	else echo "space_break_lin2: caught (law SP11 fails as expected)"; fi
-	sed 's/return a + f \* ((float)t\[i + 1\] - a);/return a + 0.0f * ((float)t[i + 1] - a);/' \
-	  $(GEN)/SpaceEffect.host.c > build/break_sp_lin1/SpaceEffect.host.c
-	@grep -q 'return a + 0.0f \* ((float)t' build/break_sp_lin1/SpaceEffect.host.c || \
-	  { echo "space_break_lin1: sed did not apply"; exit 1; }
-	$(HOSTCC) -Ibuild/break_sp_lin1 $(HOSTFLAGS) modules/space.c -o build/space_break_lin1 -lm
-	@if ./build/space_break_lin1 >/dev/null 2>&1; then \
-	  echo "space_break_lin1: FAILED (a frame-stepped Echo head not caught)"; exit 1; \
-	else echo "space_break_lin1: caught (law SP11 fails as expected)"; fi
+	@# S100 P2: a side scaled by the coefficient instead of allpassed
+	@# (frequency-independent, so not unity: the loop loses where `spr`
+	@# sets the heads apart) must fail SP12d; a loop without its band-pass
+	@# (the head straight into the clip) must fail SP13a (the DC latch);
+	@# the mid halved must fail SP12c (mono level).
+	mkdir -p build/break_sp_xfb build/break_sp_band build/break_sp_mono
+	sed 's/const float sp = kx \* sd + xs;/const float sp = kx * sd + 0.0f * xs;/' \
+	  $(GEN)/SpaceEffect.host.c > build/break_sp_xfb/SpaceEffect.host.c
+	@grep -q 'const float sp = kx \* sd + 0.0f \* xs;' build/break_sp_xfb/SpaceEffect.host.c || \
+	  { echo "space_break_xfb: sed did not apply"; exit 1; }
+	$(HOSTCC) -Ibuild/break_sp_xfb $(HOSTFLAGS) modules/space.c -o build/space_break_xfb -lm
+	@if ./build/space_break_xfb >/dev/null 2>&1; then \
+	  echo "space_break_xfb: FAILED (a non-allpass side not caught)"; exit 1; \
+	else echo "space_break_xfb: caught (law SP12 fails as expected)"; fi
+	sed 's/const float mid = kc \* (yl + yr);/const float mid = 0.5f * kc * (yl + yr);/' \
+	  $(GEN)/SpaceEffect.host.c > build/break_sp_mono/SpaceEffect.host.c
+	@grep -q 'const float mid = 0.5f \* kc \* (yl + yr);' build/break_sp_mono/SpaceEffect.host.c || \
+	  { echo "space_break_mono: sed did not apply"; exit 1; }
+	$(HOSTCC) -Ibuild/break_sp_mono $(HOSTFLAGS) modules/space.c -o build/space_break_mono -lm
+	@if ./build/space_break_mono >/dev/null 2>&1; then \
+	  echo "space_break_mono: FAILED (a lost mid not caught)"; exit 1; \
+	else echo "space_break_mono: caught (law SP12 fails as expected)"; fi
+	sed 's/y = space_clip1(v1 \* drive);/y = space_clip1(c->play * (1.0f + 0.0f * v1));/' \
+	  $(GEN)/SpaceEffect.host.c > build/break_sp_band/SpaceEffect.host.c
+	@grep -q 'c->play \* (1.0f + 0.0f \* v1)' build/break_sp_band/SpaceEffect.host.c || \
+	  { echo "space_break_band: sed did not apply"; exit 1; }
+	$(HOSTCC) -Ibuild/break_sp_band $(HOSTFLAGS) modules/space.c -o build/space_break_band -lm
+	@if ./build/space_break_band >/dev/null 2>&1; then \
+	  echo "space_break_band: FAILED (a loop without its band-pass not caught)"; exit 1; \
+	else echo "space_break_band: caught (law SP13 fails as expected)"; fi
+	@# S100 P2: each new law must fail on the old or broken law.
+	mkdir -p build/break_sp_hold build/break_sp_deg build/break_sp_wow \
+	  build/break_sp_str build/break_sp_dark
+	sed 's/SPACE_HOLD_RATE \* x \* x/0.0f * x * x/' \
+	  $(GEN)/SpaceEffect.host.c > build/break_sp_hold/SpaceEffect.host.c
+	@grep -q '0.0f \* x \* x' build/break_sp_hold/SpaceEffect.host.c || \
+	  { echo "space_break_hold: sed did not apply"; exit 1; }
+	$(HOSTCC) -Ibuild/break_sp_hold $(HOSTFLAGS) modules/space.c -o build/space_break_hold -lm
+	@if ./build/space_break_hold >/dev/null 2>&1; then \
+	  echo "space_break_hold: FAILED (the held loop's growth removed (unity at every row) not caught)"; exit 1; \
+	else echo "space_break_hold: caught (law SP8 fails as expected)"; fi
+	sed 's/k->makeup = space_degRms(k->dg) \/ space_degRms(1.0f);/k->makeup = 1.0f \/ k->dg;/' \
+	  $(GEN)/SpaceEffect.host.c > build/break_sp_deg/SpaceEffect.host.c
+	@grep -q 'k->makeup = 1.0f / k->dg;' build/break_sp_deg/SpaceEffect.host.c || \
+	  { echo "space_break_deg: sed did not apply"; exit 1; }
+	$(HOSTCC) -Ibuild/break_sp_deg $(HOSTFLAGS) modules/space.c -o build/space_break_deg -lm
+	@if ./build/space_break_deg >/dev/null 2>&1; then \
+	  echo "space_break_deg: FAILED (the makeup back to 1 / drive (not equal amplitude) not caught)"; exit 1; \
+	else echo "space_break_deg: caught (law SP14 fails as expected)"; fi
+	sed 's/(c ? -s : s) \* v1;/(c ? -s : s) * v1 * 0.3f;/' \
+	  $(GEN)/SpaceEffect.host.c > build/break_sp_wow/SpaceEffect.host.c
+	@grep -q '(c ? -s : s) \* v1 \* 0.3f;' build/break_sp_wow/SpaceEffect.host.c || \
+	  { echo "space_break_wow: sed did not apply"; exit 1; }
+	$(HOSTCC) -Ibuild/break_sp_wow $(HOSTFLAGS) modules/space.c -o build/space_break_wow -lm
+	@if ./build/space_break_wow >/dev/null 2>&1; then \
+	  echo "space_break_wow: FAILED (the wow swing at 0.3 of its depth (as the glide had filtered it) not caught)"; exit 1; \
+	else echo "space_break_wow: caught (law SP15 fails as expected)"; fi
+	sed 's/? 0.0f : (str - 64.0f) \* (1.0f \/ 63.0f);/? 0.0f : 0.0f * str;/' \
+	  $(GEN)/SpaceEffect.host.c > build/break_sp_str/SpaceEffect.host.c
+	@grep -q '? 0.0f : 0.0f \* str;' build/break_sp_str/SpaceEffect.host.c || \
+	  { echo "space_break_str: sed did not apply"; exit 1; }
+	$(HOSTCC) -Ibuild/break_sp_str $(HOSTFLAGS) modules/space.c -o build/space_break_str -lm
+	@if ./build/space_break_str >/dev/null 2>&1; then \
+	  echo "space_break_str: FAILED (the Delay's taps silent not caught)"; exit 1; \
+	else echo "space_break_str: caught (law SP16 fails as expected)"; fi
+	sed 's/#define SPACE_SPR_DARK       0.3f/#define SPACE_SPR_DARK       0.0f/' \
+	  $(GEN)/SpaceEffect.host.c > build/break_sp_dark/SpaceEffect.host.c
+	@grep -q '#define SPACE_SPR_DARK       0.0f' build/break_sp_dark/SpaceEffect.host.c || \
+	  { echo "space_break_dark: sed did not apply"; exit 1; }
+	$(HOSTCC) -Ibuild/break_sp_dark $(HOSTFLAGS) modules/space.c -o build/space_break_dark -lm
+	@if ./build/space_break_dark >/dev/null 2>&1; then \
+	  echo "space_break_dark: FAILED (no `spr` darkening not caught)"; exit 1; \
+	else echo "space_break_dark: caught (law SP17 fails as expected)"; fi
 
 # S095 P1 (§S18): the Space types' instructions per frame (space_cpu.py).
 space_cpu_check:
diff --git a/tools/dsp_test/modules/space.c b/tools/dsp_test/modules/space.c
index 1aaf796d..b3eccdc4 100644
--- a/tools/dsp_test/modules/space.c
+++ b/tools/dsp_test/modules/space.c
@@ -72,18 +72,30 @@ void fxbuf_effectShare(fx_share_t *out) { *out = sp_share; }
  *       delay (+-2 samples) at spr 63; at spr 126 R plays `tim` and L half
  *       of it (relative spread, §S18);
  *   SP2 Delay stability: noise at -6 dBFS for 4 s through fbk 127, deg 127,
- *       every `hed` stop, `rev` and `hld` on and off, `spr` and `tim`
- *       moving:
+ *       `wid` 0 and 127 (S100 P2), `rev` and `hld` on and off, `spr` and
+ *       `tim` moving:
  *       finite and below 4.0 every sample;
  *   SP3 Reverb: an impulse's tail is present (energy 0.2-0.7 s > 0) and
  *       decays (0.7-1.2 s below 0.2-0.7 s at dcy 64); 10 s of frz 2 with
  *       shm 127 after noise stays finite and below 8.0 (R-7, shortened);
  *   SP4 Echo: each routing gives a finite, bounded, non-silent output for
  *       an impulse, and the three differ;
- *   SP6-SP10 (§S18): `hed` steps 0..6 in knob order (S095 P4); the longest `tim` returns
- *       on the long channel (spr relative); `hld` does not build; Reverb
+ *   SP7, SP9, SP10 (§S18): the longest `tim` returns
+ *       on the long channel (spr relative); Reverb
  *       `ton` darkens below 127; the stopped-transport idle ramps, idles and
  *       wakes;
+ *   SP12 (S100 P2) ping-pong: `xfb` 127, an impulse on L only returns
+ *       on L at D, R at 2D, L at 3D, the other side below 1 % each time,
+ *       every repeat the input's polarity; SP12b at `xfb` 32 and 64 with
+ *       `spr` 62 and fbk 110, kicks build; SP12c `str` 0 (mono) keeps
+ *       its level at every `xfb`; SP12d the mix keeps the loop's energy;
+ *   SP13 (S100 P2) the loop band-pass: no DC latch (a DC input at fbk 127
+ *       leaves the output's mean below the input's); `wid` 0 rings at the
+ *       `ton` centre (+-5 %); `wid` 127 passes 200 Hz and 4 kHz repeats
+ *       at the `fbk` gain (+-1 dB);
+ *   SP8 (S100 P2) `hld` follows `fbk`; SP14 `deg` adds no noise and keeps
+ *       the level; SP15 `wow` depth as pitch; SP16 `str`; SP17 `spr`'s
+ *       darkening (each law's own comment);
  *   SP5 the feed: a Delay holding a tone exports, a Reverb enters with the
  *       record: the feed runs with the slot lock set; at full wet its output
  *       carries the tail while the feed runs; the feed and
@@ -154,11 +166,14 @@ static int sp_bounded(const float *x, uint32_t n, float lim)
     return 1;
 }
 
-/* tim spr fbk xfb ton deg wow hld rev hed dck (S095 P4: no `mix`) */
-#define SP_DLY_ROWS 11u
-static const uint8_t sp_dlyRows[SP_DLY_ROWS] = { 64u, 63u, 0u, 0u, 63u,
-                                                 0u, 0u, 0u, 0u,
-                                                 SPACE_HEADS_DEFAULT, 0u };
+/* A row's column in a rows[] array (rows 3.. of the type). */
+#define SP_COL(row_) ((row_) - 3u)
+
+/* tim fbk ton wid spr xfb deg wow hld rev str dck (S100 P2) */
+#define SP_DLY_ROWS 12u
+static const uint8_t sp_dlyRows[SP_DLY_ROWS] = { 64u, 0u, 63u, 127u, 63u,
+                                                 0u, 0u, 0u, 0u, 0u, 64u,
+                                                 0u };
 
 static void law_sp1(void)
 {
@@ -179,7 +194,7 @@ static void law_sp1(void)
         float mr = 0.0f;
 
         memcpy(rows, sp_dlyRows, sizeof(rows));
-        rows[1] = pass ? 126u : 63u;
+        rows[SP_COL(SPACE_DLY_SPREAD)] = pass ? 126u : 63u;
         cb_bpm = 120u;
         sp_enter(&space_delayOps, NULL, rows, SP_DLY_ROWS);
         /* Settle the delay glide from the floor (150 ms one-pole). */
@@ -201,11 +216,11 @@ static void law_sp1(void)
         }
         /* spr relative (§S18): at 126 R plays `tim` and L half of it. */
         snprintf(msg, sizeof msg, "SP1 spr %u: L at %u want %.0f (peak %.3f)",
-                 rows[1], pl - at, pass ? 0.5f * t : t, ml);
+                 rows[SP_COL(SPACE_DLY_SPREAD)], pl - at, pass ? 0.5f * t : t, ml);
         expect(fabsf((float)(pl - at) - (pass ? 0.5f * t : t)) <= 2.0f &&
                    ml > 0.3f, msg);
         snprintf(msg, sizeof msg, "SP1 spr %u: R at %u want %.0f (peak %.3f)",
-                 rows[1], pr - at, t, mr);
+                 rows[SP_COL(SPACE_DLY_SPREAD)], pr - at, t, mr);
         expect(fabsf((float)(pr - at) - t) <= 2.0f && mr > 0.3f, msg);
     }
 }
@@ -220,17 +235,19 @@ static void law_sp2(void)
     int ok = 1;
 
     memcpy(rows, sp_dlyRows, sizeof(rows));
-    rows[0] = 40u;
-    rows[2] = 127u;
-    rows[3] = 64u;
-    rows[5] = 127u;
-    rows[6] = 127u;
-    rows[7] = 1u;   /* hld on part of the time (written below too) */
+    rows[SP_COL(SPACE_DLY_TIME)] = 40u;
+    rows[SP_COL(SPACE_DLY_FEEDBACK)] = 127u;
+    rows[SP_COL(SPACE_DLY_XFEED)] = 64u;
+    rows[SP_COL(SPACE_DLY_DEGRADE)] = 127u;
+    rows[SP_COL(SPACE_DLY_WOW)] = 127u;
+    rows[SP_COL(SPACE_DLY_HOLD)] = 1u;   /* hld on part of the time (written below too) */
     sp_enter(&space_delayOps, NULL, rows, SP_DLY_ROWS);
     for (b = 0u; b < 5513u && ok; b++) {
         if ((b & 511u) == 0u) {
-            space_delayOps.write_param(&sp_rt, SPACE_DLY_HEADS,
-                                       (uint8_t)((b >> 9) % SPACE_HEADS_STOPS));
+            space_delayOps.write_param(&sp_rt, SPACE_DLY_WIDTH,
+                                       ((b >> 9) & 1u) ? 0u : 127u);
+            space_delayOps.write_param(&sp_rt, SPACE_DLY_TONE,
+                                       (uint8_t)((b >> 6) & 127u));
             space_delayOps.write_param(&sp_rt, SPACE_DLY_HOLD,
                                        (uint8_t)((b >> 10) & 1u));
             space_delayOps.write_param(&sp_rt, SPACE_DLY_REVERSE,
@@ -305,11 +322,11 @@ static void law_sp3(void)
     expect(ok, "SP3 Reverb freeze+shimmer: a sample is not finite or above 8.0");
 }
 
-/* tim fbk ton deg wow e>r bln dcy lsh hsh hed dck rtg */
-#define SP_ECH_ROWS 13u
-static const uint8_t sp_echRows[SP_ECH_ROWS] = { 64u, 64u, 63u, 0u, 0u,
-                                                 64u, 48u, 48u, 63u, 40u,
-                                                 SPACE_HEADS_DEFAULT, 0u, 0u };
+/* tim fbk ton wid spr deg wow e>r bln dcy lsh hsh str dck rtg (S100 P2) */
+#define SP_ECH_ROWS 15u
+static const uint8_t sp_echRows[SP_ECH_ROWS] = { 64u, 64u, 63u, 127u, 63u,
+                                                 0u, 0u, 64u, 48u, 48u, 63u,
+                                                 40u, 64u, 0u, 0u };
 
 static void law_sp4(void)
 {
@@ -326,7 +343,7 @@ static void law_sp4(void)
         int ok = 1;
 
         memcpy(rows, sp_echRows, sizeof(rows));
-        rows[12] = (uint8_t)route;   /* rtg stops 0, 1, 2 */
+        rows[SP_COL(SPACE_ECH_ROUTING)] = (uint8_t)route;   /* rtg stops 0, 1, 2 */
         sp_enter(&space_echoOps, NULL, rows, SP_ECH_ROWS);
         sum[route] = 0.0;
         for (b = 0u; b < 3000u; b++) {
@@ -395,41 +412,6 @@ static void law_sp5(void)
            msg);
 }
 
-/*
- * SP6 (§S18; S095 P4): `hed` steps A B C AB AC BC ABC for the stored
- * values 0..6, in order, each its own head set; a value above 6 plays ABC;
- * the default is C (D only).
- */
-static void law_sp6(void)
-{
-    static const uint8_t sets[SPACE_HEADS_STOPS] = { 1u, 2u, 4u, 3u, 5u, 6u, 7u };
-    uint32_t v;
-    uint8_t seen = 0u;
-    int ok = 1;
-    char msg[120];
-
-    sp_enter(&space_delayOps, NULL, sp_dlyRows, SP_DLY_ROWS);
-    for (v = 0u; v <= SPACE_HEADS_STOPS; v++) {
-        /* v == SPACE_HEADS_STOPS is one past the last stop: it plays ABC. */
-        const uint8_t st = (v < SPACE_HEADS_STOPS) ? (uint8_t)v
-                                                   : (uint8_t)(SPACE_HEADS_STOPS - 1u);
-        fx_share_t sh;
-        const space_tapeK_t *k;
-        uint8_t mask;
-
-        space_delayOps.write_param(&sp_rt, SPACE_DLY_HEADS, (uint8_t)v);
-        fxbuf_effectShare(&sh);
-        k = &space_hdr(&sh)->k;
-        mask = (uint8_t)((k->hg[0] > 0.0f) | ((k->hg[1] > 0.0f) << 1) |
-                         ((k->hg[2] > 0.0f) << 2));
-        ok = ok && mask == sets[st];
-        seen |= (uint8_t)(1u << st);
-    }
-    snprintf(msg, sizeof msg, "SP6 hed: stops %d, seen 0x%02x, default %u",
-             ok, seen, (unsigned)SPACE_HEADS_DEFAULT);
-    expect(ok && seen == 0x7Fu && SPACE_HEADS_DEFAULT == 2u, msg);
-}
-
 /*
  * SP7 (§S18): the longest `tim` the arena holds (its clamp) returns on the
  * long channel at the curve's time, for spr 0 (L long) and 127 (R long).
@@ -457,11 +439,12 @@ static void law_sp7(void)
         lim = space_timeLimit(SPACE_TAG_DELAY, &sp_share);
         t = crumpBit_delaySamples(lim, 120u);
         memcpy(rows, sp_dlyRows, sizeof(rows));
-        rows[0] = lim;
-        rows[1] = pass ? 127u : 0u;
+        rows[SP_COL(SPACE_DLY_TIME)] = lim;
+        rows[SP_COL(SPACE_DLY_SPREAD)] = pass ? 127u : 0u;
         sp_enter(&space_delayOps, NULL, rows, SP_DLY_ROWS);
         /* Start at the target: no glide from the floor. */
         sp_rt.d[0] = sp_rt.d[1] = (float)(sp_rt.tape_n - 3u);
+        sp_rt.dr[0] = sp_rt.dr[1] = sp_rt.d[0];
         sp_rt.v = (float)(sp_rt.tape_n - 3u) / t;
         for (b = 0u; b < (uint32_t)(1.1f * t) / 32u + 8u; b++) {
             for (n = 0u; n < 32u; n++)
@@ -475,43 +458,75 @@ static void law_sp7(void)
         }
         snprintf(msg, sizeof msg,
                  "SP7 tim %u (limit) spr %u: the long side returns at %u, "
-                 "want %.0f +-1%% (peak %.3f)", lim, rows[1], pk - at, t, mk);
+                 "want %.0f +-1%% (peak %.3f)", lim, rows[SP_COL(SPACE_DLY_SPREAD)],
+                 pk - at, t, mk);
         expect(mk > 0.1f && fabsf((float)(pk - at) - t) < 0.01f * t, msg);
     }
 }
 
-/* SP8 (§S18): `hld` sustains without building (4 s, fbk 127, deg 0). */
-static void law_sp8(void)
+/*
+ * SP8 (S100 P2): `hld` follows `fbk`. tim 40, a noise burst below about
+ * 3 kHz, then hold; the L+R energy of second 4 against second 1 of the hold:
+ * (a) fbk 20, wid 127: the same level, +-1 dB;
+ * (b) fbk 64, wid 127: grows slowly, +1 .. +6 dB (the law: 0.74 dB/s);
+ * (c) fbk 120, wid 0: grows although the band is narrow, at least +6 dB.
+ */
+static double sp8_run(uint8_t fbk, uint8_t wid, double *e1)
 {
     uint8_t rows[SP_DLY_ROWS];
     float l[32];
     float r[32];
-    double e1 = 0.0;
     double e4 = 0.0;
+    float lp_l = 0.0f;
+    float lp_r = 0.0f;
     uint32_t b;
     uint32_t n;
-    char msg[160];
 
     memcpy(rows, sp_dlyRows, sizeof(rows));
-    rows[0] = 40u;
-    rows[2] = 127u;
+    rows[SP_COL(SPACE_DLY_TIME)] = 40u;
+    rows[SP_COL(SPACE_DLY_FEEDBACK)] = fbk;
+    rows[SP_COL(SPACE_DLY_WIDTH)] = wid;
+    cb_bpm = 120u;
     sp_enter(&space_delayOps, NULL, rows, SP_DLY_ROWS);
+    *e1 = 0.0;
     for (b = 0u; b < 7000u; b++) {
         if (b == 1500u)
             space_delayOps.write_param(&sp_rt, SPACE_DLY_HOLD, 1u);
-        for (n = 0u; n < 32u; n++)
-            l[n] = r[n] = (b < 1400u) ? 0.2f * sp_noise() : 0.0f;
+        for (n = 0u; n < 32u; n++) {
+            /* Noise below about 3 kHz (a one-pole), as programme. */
+            lp_l += 0.35f * (((b < 1400u) ? 0.1f * sp_noise() : 0.0f) - lp_l);
+            lp_r += 0.35f * (((b < 1400u) ? 0.1f * sp_noise() : 0.0f) - lp_r);
+            l[n] = lp_l;
+            r[n] = lp_r;
+        }
         sp_block(&space_delayOps, l, r);
         for (n = 0u; n < 32u; n++) {
+            const double v = (double)l[n] * l[n] + (double)r[n] * r[n];
+
             if (b >= 1500u && b < 2878u)
-                e1 += (double)l[n] * l[n];
+                *e1 += v;
             else if (b >= 5622u)
-                e4 += (double)l[n] * l[n];
+                e4 += v;
         }
     }
-    snprintf(msg, sizeof msg,
-             "SP8 hld: energy second 1 %.3g, second 4 %.3g (no growth)", e1, e4);
-    expect(e1 > 1e-3 && e4 <= 1.05 * e1, msg);
+    return 10.0 * log10(e4 / *e1);
+}
+
+static void law_sp8(void)
+{
+    double e1;
+    double g;
+    char msg[160];
+
+    g = sp8_run(20u, 127u, &e1);
+    snprintf(msg, sizeof msg, "SP8a hld fbk 20 wid 127: second 4 %+.2f dB on second 1 (+-1)", g);
+    expect(e1 > 1e-3 && fabs(g) <= 1.0, msg);
+    g = sp8_run(64u, 127u, &e1);
+    snprintf(msg, sizeof msg, "SP8b hld fbk 64 wid 127: second 4 %+.2f dB on second 1 (+1 .. +6)", g);
+    expect(e1 > 1e-3 && g >= 1.0 && g <= 6.0, msg);
+    g = sp8_run(120u, 0u, &e1);
+    snprintf(msg, sizeof msg, "SP8c hld fbk 120 wid 0: second 4 %+.2f dB on second 1 (>= +6)", g);
+    expect(e1 > 1e-9 && g >= 6.0, msg);
 }
 
 /* SP9 (§S18): Reverb `ton` 0 darkens the tail; 127 matches the flat path. */
@@ -602,16 +617,16 @@ static void law_sp10(void)
 }
 
 /*
- * SP11 (S096 P2 F1): no frame slips in the extra heads or the reverse head.
+ * SP11 (S096 P2 F1): no frame slips in the heads or the reverse head.
  *
  * What:       a 440 Hz sine at 0.5 into the Delay (feedback 0) and the Echo
  *             (feedback 0), 4 s after the glides settle; the largest third
  *             difference of the output L. A smooth (or smoothly wobbled)
  *             sine gives about 1.2e-4 and the linear main head's moving
  *             fraction about 4e-3; a one-frame slip of this sine 0.06 ..
- *             0.09. Cases: Delay heads ABC at wow 127; Delay reverse (heads
- *             C) at wow 0 and 127; Echo heads ABC at wow 127. Each must stay
- *             below 0.012.
+ *             0.09. Cases: Delay at wow 127; Delay reverse at wow 0 and
+ *             127; Echo at wow 127 (S100 P2: one head, no `hed`). Each must
+ *             stay below 0.012.
  * Why:        the D/3 and 2D/3 heads and the reverse windows read the
  *             nearest frame before S096 P2, which slipped as soon as wow
  *             (or the reverse ramp) moved their positions by fractions: the
@@ -651,10 +666,10 @@ static float sp11_run(const effect_type_ops_t *ops, const uint8_t *rows, uint32_
 
 static void law_sp11(void)
 {
-    static const struct { uint8_t hed, rev, wow; const char *what; } dly[3] = {
-        { 6u, 0u, 127u, "Delay heads ABC, wow 127" },
-        { SPACE_HEADS_DEFAULT, 1u, 0u, "Delay reverse, wow 0" },
-        { SPACE_HEADS_DEFAULT, 1u, 127u, "Delay reverse, wow 127" },
+    static const struct { uint8_t rev, wow; const char *what; } dly[3] = {
+        { 0u, 127u, "Delay, wow 127" },
+        { 1u, 0u, "Delay reverse, wow 0" },
+        { 1u, 127u, "Delay reverse, wow 127" },
     };
     char msg[160];
     uint32_t c;
@@ -664,10 +679,9 @@ static void law_sp11(void)
         float mx;
 
         memcpy(rows, sp_dlyRows, sizeof(rows));
-        rows[2] = 0u;            /* fbk */
-        rows[6] = dly[c].wow;    /* wow */
-        rows[8] = dly[c].rev;    /* rev */
-        rows[9] = dly[c].hed;    /* hed */
+        rows[SP_COL(SPACE_DLY_FEEDBACK)] = 0u;
+        rows[SP_COL(SPACE_DLY_WOW)] = dly[c].wow;
+        rows[SP_COL(SPACE_DLY_REVERSE)] = dly[c].rev;
         mx = sp11_run(&space_delayOps, rows, SP_DLY_ROWS);
         snprintf(msg, sizeof msg, "SP11 %s: largest third difference %.4f (< 0.012)",
                  dly[c].what, mx);
@@ -678,16 +692,646 @@ static void law_sp11(void)
         float mx;
 
         memcpy(rows, sp_echRows, sizeof(rows));
-        rows[1] = 0u;            /* fbk */
-        rows[4] = 127u;          /* wow */
-        rows[10] = 6u;           /* hed ABC */
+        rows[SP_COL(SPACE_ECH_FEEDBACK)] = 0u;
+        rows[SP_COL(SPACE_ECH_WOW)] = 127u;
         mx = sp11_run(&space_echoOps, rows, SP_ECH_ROWS);
-        snprintf(msg, sizeof msg, "SP11 Echo heads ABC, wow 127: largest third difference %.4f (< 0.012)",
+        snprintf(msg, sizeof msg, "SP11 Echo, wow 127: largest third difference %.4f (< 0.012)",
                  mx);
         expect(mx < 0.012f, msg);
     }
 }
 
+/*
+ * SP12 (S100 P2): `xfb` 127 ping-pongs. An impulse on L only (spr 63, fbk
+ * 100, wid 127): the first three repeats land on L at D, R at 2D and L at
+ * 3D (+-2 samples), each with the other side below 1 % of it.
+ */
+static void law_sp12(void)
+{
+    uint8_t rows[SP_DLY_ROWS];
+    const float t = crumpBit_delaySamples(64u, 120u);
+    const uint32_t at = 3200u;
+    float pkl[3] = { 0.0f, 0.0f, 0.0f };
+    float pkr[3] = { 0.0f, 0.0f, 0.0f };
+    float sg[3] = { 0.0f, 0.0f, 0.0f };
+    uint32_t pos[3] = { 0u, 0u, 0u };
+    float l[32];
+    float r[32];
+    uint32_t b;
+    uint32_t n;
+    uint32_t k;
+    int ok = 1;
+    char msg[200];
+
+    memcpy(rows, sp_dlyRows, sizeof(rows));
+    rows[SP_COL(SPACE_DLY_FEEDBACK)] = 100u;
+    rows[SP_COL(SPACE_DLY_XFEED)] = 127u;
+    cb_bpm = 120u;
+    sp_enter(&space_delayOps, NULL, rows, SP_DLY_ROWS);
+    for (b = 0u; b < 9000u; b++) {
+        memset(l, 0, sizeof(l));
+        memset(r, 0, sizeof(r));
+        sp_block(&space_delayOps, l, r);
+    }
+    for (b = 0u; b < (uint32_t)(3.5f * t + (float)at) / 32u; b++) {
+        for (n = 0u; n < 32u; n++) {
+            l[n] = (b * 32u + n == at) ? 1.0f : 0.0f;
+            r[n] = 0.0f;
+        }
+        sp_block(&space_delayOps, l, r);
+        for (n = 0u; n < 32u; n++) {
+            const float f = (float)(b * 32u + n) - (float)at;
+
+            /* Window k: (k + 0.5) D .. (k + 1.5) D around repeat k + 1. */
+            for (k = 0u; k < 3u; k++)
+                if (f > ((float)k + 0.5f) * t && f < ((float)k + 1.5f) * t) {
+                    if (fabsf(l[n]) > pkl[k]) {
+                        pkl[k] = fabsf(l[n]);
+                        if (k != 1u) {
+                            pos[k] = (uint32_t)f;
+                            sg[k] = l[n];
+                        }
+                    }
+                    if (fabsf(r[n]) > pkr[k]) {
+                        pkr[k] = fabsf(r[n]);
+                        if (k == 1u) {
+                            pos[k] = (uint32_t)f;
+                            sg[k] = r[n];
+                        }
+                    }
+                }
+        }
+    }
+    for (k = 0u; k < 3u; k++) {
+        const float on = (k == 1u) ? pkr[k] : pkl[k];
+        const float off = (k == 1u) ? pkl[k] : pkr[k];
+
+        ok = ok && on > 0.2f && off < 0.01f * on && sg[k] > 0.0f &&
+             fabsf((float)pos[k] - (float)(k + 1u) * t) <= 2.0f;
+    }
+    snprintf(msg, sizeof msg,
+             "SP12 ping-pong: L %.3f/%.3f at %u, R %.3f/%.3f at %u, "
+             "L %.3f/%.3f at %u (D %.0f)", pkl[0], pkr[0], pos[0], pkr[1],
+             pkl[1], pos[1], pkl[2], pkr[2], pos[2], t);
+    expect(ok, msg);
+}
+
+/*
+ * SP12b (S100 P2): at `xfb` 64, `spr` 62 (R 1 % shorter) and fbk 110, a
+ * 55 Hz kick every beat builds: the L+R level of second 8 is at least 4 dB
+ * above second 1 (measured +6.6; the L/R average +0.6, the old law +2.5). An L/R average at this row cancelled the low end where
+ * the two times differ, and the loop sat at one level.
+ */
+/* L+R (or, mono, L) energy per second of 8 s of a kick every beat. */
+static void sp12_kicks(uint8_t fbk, uint8_t spr, uint8_t xfb, uint8_t str,
+                       double *e)
+{
+    uint8_t rows[SP_DLY_ROWS];
+    float l[32];
+    float r[32];
+    uint32_t b;
+    uint32_t n;
+    uint32_t t = 0u;
+
+    memcpy(rows, sp_dlyRows, sizeof(rows));
+    rows[SP_COL(SPACE_DLY_TIME)] = 90u;
+    rows[SP_COL(SPACE_DLY_FEEDBACK)] = fbk;
+    rows[SP_COL(SPACE_DLY_SPREAD)] = spr;
+    rows[SP_COL(SPACE_DLY_XFEED)] = xfb;
+    rows[SP_COL(SPACE_DLY_STEREO)] = str;
+    for (b = 0u; b < 8u; b++)
+        e[b] = 0.0;
+    cb_bpm = 120u;
+    sp_enter(&space_delayOps, NULL, rows, SP_DLY_ROWS);
+    for (b = 0u; b < 8u * 1378u; b++) {
+        for (n = 0u; n < 32u; n++, t++) {
+            const uint32_t ph = t % 22054u;
+
+            l[n] = r[n] = (ph < 8000u)
+                ? 0.5f * expf(-(float)ph / 2000.0f) *
+                      sinf(6.2831853f * 55.0f * (float)ph / 44108.07f)
+                : 0.0f;
+        }
+        sp_block(&space_delayOps, l, r);
+        for (n = 0u; n < 32u; n++)
+            e[b / 1378u] += (double)l[n] * l[n] + (double)r[n] * r[n];
+    }
+}
+
+static void law_sp12b(void)
+{
+    static const uint8_t xfb[2] = { 32u, 64u };
+    double e[8];
+    char msg[160];
+    uint32_t c;
+
+    for (c = 0u; c < 2u; c++) {
+        sp12_kicks(110u, 62u, xfb[c], 64u, e);
+        snprintf(msg, sizeof msg,
+                 "SP12b xfb %u, spr 62, fbk 110: second 8 is %+.1f dB on second 1 (>= +4)",
+                 xfb[c], 10.0 * log10(e[7] / e[0]));
+        expect(e[7] >= 2.51 * e[0], msg);
+    }
+}
+
+/*
+ * SP12c (S100 P2): `str` 0 (mono) keeps its level as `xfb` rises: kicks,
+ * fbk 90, spr 63; the energy of seconds 2..8 at xfb 64 and 127 within 1 dB
+ * of xfb 0 (a rotation lost cos(angle) of the mid each pass: every other
+ * repeat cancelled in mono at 127).
+ */
+static void law_sp12c(void)
+{
+    static const uint8_t xfb[3] = { 0u, 64u, 127u };
+    double e[8];
+    double s[3];
+    char msg[200];
+    uint32_t c;
+    uint32_t k;
+
+    for (c = 0u; c < 3u; c++) {
+        sp12_kicks(90u, 63u, xfb[c], 0u, e);
+        s[c] = 0.0;
+        for (k = 1u; k < 8u; k++)
+            s[c] += e[k];
+    }
+    snprintf(msg, sizeof msg,
+             "SP12c str 0: xfb 64 %+.2f dB, xfb 127 %+.2f dB on xfb 0 (+-1)",
+             10.0 * log10(s[1] / s[0]), 10.0 * log10(s[2] / s[0]));
+    expect(fabs(10.0 * log10(s[1] / s[0])) <= 1.0 &&
+               fabs(10.0 * log10(s[2] / s[0])) <= 1.0, msg);
+}
+
+/* SP13 helper: run the Delay (tim 20) on a source for nb blocks. */
+static void sp13_run(uint8_t fbk, uint8_t ton, uint8_t wid,
+                     float (*src)(uint32_t), uint32_t nb, float *out)
+{
+    uint8_t rows[SP_DLY_ROWS];
+    float l[32];
+    float r[32];
+    uint32_t b;
+    uint32_t n;
+
+    memcpy(rows, sp_dlyRows, sizeof(rows));
+    rows[SP_COL(SPACE_DLY_TIME)] = 20u;
+    rows[SP_COL(SPACE_DLY_FEEDBACK)] = fbk;
+    rows[SP_COL(SPACE_DLY_TONE)] = ton;
+    rows[SP_COL(SPACE_DLY_WIDTH)] = wid;
+    cb_bpm = 120u;
+    sp_enter(&space_delayOps, NULL, rows, SP_DLY_ROWS);
+    /* Settle the delay glide from the floor (150 ms one-pole), as SP1. */
+    for (b = 0u; b < 9000u; b++) {
+        memset(l, 0, sizeof(l));
+        memset(r, 0, sizeof(r));
+        sp_block(&space_delayOps, l, r);
+    }
+    for (b = 0u; b < nb; b++) {
+        for (n = 0u; n < 32u; n++)
+            l[n] = r[n] = src(b * 32u + n);
+        sp_block(&space_delayOps, l, r);
+        for (n = 0u; n < 32u; n++)
+            out[b * 32u + n] = l[n];
+    }
+}
+
+static float sp13_dc(uint32_t i) { (void)i; return 0.25f; }
+static float sp13_burst(uint32_t i) { return (i < 2000u) ? 0.3f * sp_noise() : 0.0f; }
+static float sp13_f;
+static float sp13_tone(uint32_t i)
+{
+    return (i < 1500u) ? 0.25f * sinf(6.2831853f * sp13_f * (float)i / 44108.07f)
+                       : 0.0f;
+}
+static float sp13_buf[44100u * 6u];
+
+/*
+ * The peak of out[] in [a, b).
+ */
+static float sp13_peak(const float *x, uint32_t a, uint32_t b)
+{
+    float m = 0.0f;
+
+    for (; a < b; a++)
+        m = __builtin_fmaxf(m, fabsf(x[a]));
+    return m;
+}
+
+/*
+ * SP13 (S100 P2): the loop band-pass.
+ * (a) no DC latch: a constant 0.25 input at fbk 127 for 6 s: the output's
+ *     mean (last second) is the input's own DC played once, 0.25 +-0.01
+ *     (before S100 P2 the loop latched to about 0.65 against the clip);
+ * (b) wid 0 resonates at the ton centre: a noise burst at fbk 110, ton 63
+ *     (785 Hz) rings; zero crossings over the last second give 785 Hz
+ *     +-5 %;
+ * (c) wid 127 is nearly flat: 200 Hz and 4 kHz bursts at fbk 80 keep the
+ *     repeat-to-repeat ratio (repeats 2 and 3) at 0.8 +-1 dB.
+ */
+static void law_sp13(void)
+{
+    const uint32_t nb = (44100u * 6u) / 32u;
+    const float d = crumpBit_delaySamples(20u, 120u);
+    const float fc = 80.0f * exp2f(7.2288187f * 63.0f / 127.0f);
+    double mean = 0.0;
+    uint32_t zc = 0u;
+    uint32_t i;
+    uint32_t c;
+    char msg[200];
+
+    sp13_run(127u, 63u, 127u, sp13_dc, nb, sp13_buf);
+    for (i = nb * 32u - 44108u; i < nb * 32u; i++)
+        mean += sp13_buf[i];
+    mean /= 44108.0;
+    snprintf(msg, sizeof msg, "SP13a no DC latch: output mean %.4f (0.25 +-0.01)", mean);
+    expect(fabs(mean - 0.25) < 0.01, msg);
+
+    sp13_run(110u, 63u, 0u, sp13_burst, nb, sp13_buf);
+    for (i = nb * 32u - 44108u; i < nb * 32u; i++)
+        zc += (sp13_buf[i - 1u] < 0.0f) != (sp13_buf[i] < 0.0f);
+    snprintf(msg, sizeof msg,
+             "SP13b wid 0 rings at %.0f Hz, want %.0f +-5 %% (peak %.3f)",
+             0.5f * (float)zc, fc, sp13_peak(sp13_buf, nb * 32u - 44108u, nb * 32u));
+    expect(fabsf(0.5f * (float)zc - fc) < 0.05f * fc &&
+               sp13_peak(sp13_buf, nb * 32u - 44108u, nb * 32u) > 0.05f, msg);
+
+    for (c = 0u; c < 2u; c++) {
+        float p2;
+        float p3;
+        float db;
+
+        sp13_f = c ? 4000.0f : 200.0f;
+        sp13_run(80u, 63u, 127u, sp13_tone, (uint32_t)(4.0f * d) / 32u + 8u,
+                 sp13_buf);
+        p2 = sp13_peak(sp13_buf, (uint32_t)(1.9f * d), (uint32_t)(2.9f * d));
+        p3 = sp13_peak(sp13_buf, (uint32_t)(2.9f * d), (uint32_t)(3.9f * d));
+        db = 20.0f * log10f(p3 / p2 / 0.8f);
+        snprintf(msg, sizeof msg,
+                 "SP13c wid 127, %.0f Hz: repeat ratio %.3f, %+.2f dB from fbk 0.8",
+                 sp13_f, p3 / p2, db);
+        expect(p2 > 0.05f && fabsf(db) < 1.0f, msg);
+    }
+}
+
+/*
+ * SP14 (S100 P2): `deg` adds no noise and keeps the level.
+ * (a) deg 127, fbk 90, wid 127, from init with zero input: exact 0.0 for
+ *     4 s; after a 0.3 s noise burst, exact 0.0 again within 8 s (fbk 40:
+ *     below the row where the makeup's quiet-signal gain, 1.79 at deg 127,
+ *     takes the loop to unity);
+ * (b) a -12 dBFS 1 kHz burst, fbk 100: the second repeat's RMS at deg 127
+ *     is within 1.5 dB of deg 0's (equal amplitude).
+ */
+static float sp14_rep2(uint8_t deg)
+{
+    uint8_t rows[SP_DLY_ROWS];
+    const float d = crumpBit_delaySamples(64u, 120u);
+    float l[32];
+    float r[32];
+    double pk = 0.0;
+    uint32_t b;
+    uint32_t n;
+
+    memcpy(rows, sp_dlyRows, sizeof(rows));
+    rows[SP_COL(SPACE_DLY_FEEDBACK)] = 100u;
+    rows[SP_COL(SPACE_DLY_DEGRADE)] = deg;
+    cb_bpm = 120u;
+    sp_enter(&space_delayOps, NULL, rows, SP_DLY_ROWS);
+    for (b = 0u; b < 9000u; b++) {
+        memset(l, 0, sizeof(l));
+        memset(r, 0, sizeof(r));
+        sp_block(&space_delayOps, l, r);
+    }
+    for (b = 0u; b < (uint32_t)(2.9f * d) / 32u; b++) {
+        for (n = 0u; n < 32u; n++) {
+            const uint32_t i = b * 32u + n;
+
+            l[n] = r[n] = (i < 4000u)
+                ? 0.25f * sinf(6.2831853f * 1000.0f * (float)i / 44108.07f)
+                : 0.0f;
+        }
+        sp_block(&space_delayOps, l, r);
+        for (n = 0u; n < 32u; n++)
+            if ((float)(b * 32u + n) > 1.9f * d)
+                pk += (double)l[n] * l[n];
+    }
+    return (float)sqrt(pk);
+}
+
+static void law_sp14(void)
+{
+    uint8_t rows[SP_DLY_ROWS];
+    float l[32];
+    float r[32];
+    uint32_t b;
+    uint32_t n;
+    uint32_t nz = 0u;
+    uint32_t last = 0u;
+    float p0;
+    float p1;
+    char msg[160];
+
+    memcpy(rows, sp_dlyRows, sizeof(rows));
+    rows[SP_COL(SPACE_DLY_FEEDBACK)] = 90u;
+    rows[SP_COL(SPACE_DLY_DEGRADE)] = 127u;
+    sp_enter(&space_delayOps, NULL, rows, SP_DLY_ROWS);
+    for (b = 0u; b < 5513u; b++) {
+        memset(l, 0, sizeof(l));
+        memset(r, 0, sizeof(r));
+        sp_block(&space_delayOps, l, r);
+        for (n = 0u; n < 32u; n++)
+            nz += (l[n] != 0.0f) || (r[n] != 0.0f);
+    }
+    snprintf(msg, sizeof msg, "SP14a deg 127 from init, no input: %u non-zero samples in 4 s", nz);
+    expect(nz == 0u, msg);
+    rows[SP_COL(SPACE_DLY_FEEDBACK)] = 40u;
+    sp_enter(&space_delayOps, NULL, rows, SP_DLY_ROWS);
+    for (b = 0u; b < 11025u; b++) {
+        for (n = 0u; n < 32u; n++)
+            l[n] = r[n] = (b < 413u) ? 0.3f * sp_noise() : 0.0f;
+        sp_block(&space_delayOps, l, r);
+        for (n = 0u; n < 32u; n++)
+            if (l[n] != 0.0f || r[n] != 0.0f)
+                last = b;
+    }
+    snprintf(msg, sizeof msg, "SP14a deg 127 fbk 40 after a burst: last non-zero at %.2f s (< 7.5)",
+             (double)last * 32.0 / 44108.0);
+    expect(last < 11025u - 690u, msg);
+    p0 = sp14_rep2(0u);
+    p1 = sp14_rep2(127u);
+    snprintf(msg, sizeof msg, "SP14b deg 127 second repeat RMS %.3f, deg 0 %.3f (%+.2f dB, +-1.5)",
+             p1, p0, 20.0f * log10f(p1 / p0));
+    expect(p0 > 1.0f && fabsf(20.0f * log10f(p1 / p0)) <= 1.5f, msg);
+}
+
+/*
+ * SP15 (S100 P2): `wow` depth as pitch. A 1 kHz tone (fbk 0, tim 64): the
+ * largest deviation of the wet's zero-crossing rate over 2 s (after 2 s
+ * of settling) at wow 127 is 1.2 .. 3.5 % (the law: 2 % wow + 0.3 %
+ * flutter), at wow 0 below 0.1 %.
+ */
+static float sp15_run(uint8_t wow)
+{
+    uint8_t rows[SP_DLY_ROWS];
+    float l[32];
+    float r[32];
+    float prev = 0.0f;
+    float last = -1.0f;
+    float lo = 1e9f;
+    float hi = 0.0f;
+    uint32_t b;
+    uint32_t n;
+    uint32_t i = 0u;
+    uint32_t k = 0u;
+    float iv[8] = { 0 };
+    float acc = 0.0f;
+
+    memcpy(rows, sp_dlyRows, sizeof(rows));
+    rows[SP_COL(SPACE_DLY_WOW)] = wow;
+    cb_bpm = 120u;
+    sp_enter(&space_delayOps, NULL, rows, SP_DLY_ROWS);
+    for (b = 0u; b < 5513u; b++) {
+        for (n = 0u; n < 32u; n++, i++)
+            l[n] = r[n] = 0.5f * sinf(6.2831853f * 1000.0f * (float)i / 44108.07f);
+        sp_block(&space_delayOps, l, r);
+        for (n = 0u; n < 32u; n++) {
+            const float x = l[n];
+
+            if (b > 2757u && prev < 0.0f && x >= 0.0f) {
+                /* The crossing by linear interpolation. */
+                const float at = (float)(b * 32u + n) - x / (x - prev);
+
+                if (last >= 0.0f) {
+                    /* An 8-period mean, so the reading is smooth. */
+                    acc += (at - last) - iv[k & 7u];
+                    iv[k & 7u] = at - last;
+                    if (++k >= 8u) {
+                        const float f = 8.0f * 44108.07f / acc;
+
+                        lo = __builtin_fminf(lo, f);
+                        hi = __builtin_fmaxf(hi, f);
+                    }
+                }
+                last = at;
+            }
+            prev = x;
+        }
+    }
+    return 100.0f * 0.5f * (hi - lo) / 1000.0f;
+}
+
+static void law_sp15(void)
+{
+    const float d0 = sp15_run(0u);
+    const float d1 = sp15_run(127u);
+    char msg[160];
+
+    snprintf(msg, sizeof msg, "SP15 wow pitch: 127 +-%.2f %% (1.2 .. 3.5), 0 +-%.3f %% (< 0.1)", d1, d0);
+    expect(d1 >= 1.2f && d1 <= 3.5f && d0 < 0.1f, msg);
+}
+
+/*
+ * SP16 (S100 P2): `str`. (a) str 0: L and R are the same, sample for
+ * sample, for a stereo input; (b) str 127, spr 126 (L at D/2, R at D): an
+ * impulse on both returns on L at D/2 and at 2D/3 (tap A, L's lane a third
+ * of the way to R's time), on R at D and at 5D/6 (tap B), each tap at least
+ * 0.2 of its head; and the taps are only in the field: L's tap at 2D/3 is
+ * louder on L than on R.
+ */
+static void law_sp16(void)
+{
+    uint8_t rows[SP_DLY_ROWS];
+    const float t = crumpBit_delaySamples(64u, 120u);
+    const uint32_t at = 3200u;
+    float l[32];
+    float r[32];
+    float hl = 0.0f;
+    float ml = 0.0f;
+    float hr = 0.0f;
+    float mr = 0.0f;
+    float xr = 0.0f;
+    uint32_t b;
+    uint32_t n;
+    int same = 1;
+    char msg[200];
+
+    memcpy(rows, sp_dlyRows, sizeof(rows));
+    rows[SP_COL(SPACE_DLY_STEREO)] = 0u;
+    rows[SP_COL(SPACE_DLY_SPREAD)] = 100u;
+    rows[SP_COL(SPACE_DLY_FEEDBACK)] = 60u;
+    sp_enter(&space_delayOps, NULL, rows, SP_DLY_ROWS);
+    for (b = 0u; b < 3000u; b++) {
+        for (n = 0u; n < 32u; n++) {
+            l[n] = 0.3f * sp_noise();
+            r[n] = 0.3f * sinf((float)(b * 32u + n) * 0.03f);
+        }
+        sp_block(&space_delayOps, l, r);
+        for (n = 0u; n < 32u; n++)
+            same = same && l[n] == r[n];
+    }
+    expect(same, "SP16a str 0: L and R the same, sample for sample");
+
+    memcpy(rows, sp_dlyRows, sizeof(rows));
+    rows[SP_COL(SPACE_DLY_STEREO)] = 127u;
+    rows[SP_COL(SPACE_DLY_SPREAD)] = 126u;
+    cb_bpm = 120u;
+    sp_enter(&space_delayOps, NULL, rows, SP_DLY_ROWS);
+    for (b = 0u; b < 9000u; b++) {
+        memset(l, 0, sizeof(l));
+        memset(r, 0, sizeof(r));
+        sp_block(&space_delayOps, l, r);
+    }
+    for (b = 0u; b < (uint32_t)(1.2f * t + (float)at) / 32u; b++) {
+        for (n = 0u; n < 32u; n++)
+            l[n] = r[n] = (b * 32u + n == at) ? 1.0f : 0.0f;
+        sp_block(&space_delayOps, l, r);
+        for (n = 0u; n < 32u; n++) {
+            const float f = (float)(b * 32u + n) - (float)at;
+
+            if (fabsf(f - 0.5f * t) < 4.0f) hl = __builtin_fmaxf(hl, fabsf(l[n]));
+            if (fabsf(f - (2.0f / 3.0f) * t) < 4.0f) {
+                ml = __builtin_fmaxf(ml, fabsf(l[n]));
+                xr = __builtin_fmaxf(xr, fabsf(r[n]));
+            }
+            if (fabsf(f - t) < 4.0f) hr = __builtin_fmaxf(hr, fabsf(r[n]));
+            if (fabsf(f - (5.0f / 6.0f) * t) < 4.0f) mr = __builtin_fmaxf(mr, fabsf(r[n]));
+        }
+    }
+    snprintf(msg, sizeof msg,
+             "SP16b str 127 spr 126: L head %.3f tap %.3f (on R %.3f), R head %.3f tap %.3f",
+             hl, ml, xr, hr, mr);
+    expect(hl > 0.2f && hr > 0.2f && ml > 0.2f * hl && mr > 0.2f * hr &&
+               ml > xr, msg);
+}
+
+/*
+ * SP17 (S100 P2): `spr` darkens the loop that runs ahead of `tim`, more at
+ * full width. spr 126 (L at D/2), fbk 100, xfb 0, a noise burst: the high
+ * share (first-difference energy / energy) of L's repeats 2..6 against R's
+ * repeats 1..3, below 0.6 at wid 127 (0.86 without the darkening: the
+ * shorter loop makes more passes) and above that at wid 40 (a narrower
+ * band: less darkening).
+ */
+static float sp17_run(uint8_t wid)
+{
+    uint8_t rows[SP_DLY_ROWS];
+    const float t = crumpBit_delaySamples(64u, 120u);
+    float l[32];
+    float r[32];
+    float pl = 0.0f;
+    float pr = 0.0f;
+    double el = 0.0;
+    double dl = 0.0;
+    double er = 0.0;
+    double dr = 0.0;
+    uint32_t b;
+    uint32_t n;
+
+    memcpy(rows, sp_dlyRows, sizeof(rows));
+    rows[SP_COL(SPACE_DLY_SPREAD)] = 126u;
+    rows[SP_COL(SPACE_DLY_FEEDBACK)] = 100u;
+    rows[SP_COL(SPACE_DLY_WIDTH)] = wid;
+    cb_bpm = 120u;
+    sp_enter(&space_delayOps, NULL, rows, SP_DLY_ROWS);
+    for (b = 0u; b < 9000u; b++) {
+        memset(l, 0, sizeof(l));
+        memset(r, 0, sizeof(r));
+        sp_block(&space_delayOps, l, r);
+    }
+    for (b = 0u; b < (uint32_t)(3.4f * t) / 32u; b++) {
+        for (n = 0u; n < 32u; n++)
+            l[n] = r[n] = (b * 32u + n < 400u) ? 0.3f * sp_noise() : 0.0f;
+        sp_block(&space_delayOps, l, r);
+        for (n = 0u; n < 32u; n++) {
+            const float f = (float)(b * 32u + n);
+
+            if (f > 1.4f * t) {
+                el += (double)l[n] * l[n];
+                dl += (double)(l[n] - pl) * (l[n] - pl);
+            }
+            if (f > 1.4f * t) {
+                er += (double)r[n] * r[n];
+                dr += (double)(r[n] - pr) * (r[n] - pr);
+            }
+            pl = l[n];
+            pr = r[n];
+        }
+    }
+    return (float)((dl / el) / (dr / er));
+}
+
+static void law_sp17(void)
+{
+    const float w127 = sp17_run(127u);
+    const float w40 = sp17_run(40u);
+    char msg[160];
+
+    snprintf(msg, sizeof msg,
+             "SP17 spr darkening: L/R high share wid 127 %.3f (< 0.6), wid 40 %.3f (%.3f .. 1.0)",
+             w127, w40, w127);
+    expect(w127 < 0.6f && w40 > w127 && w40 <= 1.0f, msg);
+}
+
+/*
+ * SP12d (S100 P2): the `xfb` mix keeps the loop's energy. fbk 100, spr 30
+ * (R 0.71 x L's time), a quiet stereo noise burst (independent L and R, so
+ * the side is as large as the mid): the L+R energy of second 4 against
+ * second 1 of the tail at xfb 32 and 64 is within 1 dB of the same at
+ * xfb 0 (two independent loops). A side scaled by the coefficient, or an
+ * L/R average, loses the side every pass.
+ */
+static double sp12d_run(uint8_t xfb)
+{
+    uint8_t rows[SP_DLY_ROWS];
+    float l[32];
+    float r[32];
+    double e1 = 0.0;
+    double e4 = 0.0;
+    uint32_t b;
+    uint32_t n;
+
+    memcpy(rows, sp_dlyRows, sizeof(rows));
+    rows[SP_COL(SPACE_DLY_TIME)] = 40u;
+    rows[SP_COL(SPACE_DLY_FEEDBACK)] = 100u;
+    rows[SP_COL(SPACE_DLY_SPREAD)] = 30u;
+    rows[SP_COL(SPACE_DLY_XFEED)] = xfb;
+    cb_bpm = 120u;
+    sp_enter(&space_delayOps, NULL, rows, SP_DLY_ROWS);
+    for (b = 0u; b < 9000u; b++) {
+        memset(l, 0, sizeof(l));
+        memset(r, 0, sizeof(r));
+        sp_block(&space_delayOps, l, r);
+    }
+    for (b = 0u; b < 6000u; b++) {
+        for (n = 0u; n < 32u; n++) {
+            l[n] = (b < 100u) ? 0.05f * sp_noise() : 0.0f;
+            r[n] = (b < 100u) ? 0.05f * sp_noise() : 0.0f;
+        }
+        sp_block(&space_delayOps, l, r);
+        for (n = 0u; n < 32u; n++) {
+            const double v = (double)l[n] * l[n] + (double)r[n] * r[n];
+
+            if (b >= 200u && b < 1578u)
+                e1 += v;
+            else if (b >= 4334u && b < 5712u)
+                e4 += v;
+        }
+    }
+    return 10.0 * log10(e4 / e1);
+}
+
+static void law_sp12d(void)
+{
+    const double d0 = sp12d_run(0u);
+    const double d32 = sp12d_run(32u);
+    const double d64 = sp12d_run(64u);
+    char msg[200];
+
+    snprintf(msg, sizeof msg,
+             "SP12d energy kept: second 4 on second 1 at xfb 0 %+.2f, 32 %+.2f, 64 %+.2f dB (32, 64 within 1 of 0)",
+             d0, d32, d64);
+    expect(fabs(d32 - d0) <= 1.0 && fabs(d64 - d0) <= 1.0, msg);
+}
+
 int main(void)
 {
     law_sp1();
@@ -695,12 +1339,20 @@ int main(void)
     law_sp3();
     law_sp4();
     law_sp5();
-    law_sp6();
     law_sp7();
     law_sp8();
     law_sp9();
     law_sp10();
     law_sp11();
+    law_sp12();
+    law_sp12b();
+    law_sp12c();
+    law_sp12d();
+    law_sp13();
+    law_sp14();
+    law_sp15();
+    law_sp16();
+    law_sp17();
     printf("space failures: %d\n", failures);
     return failures ? 1 : 0;
 }
```
