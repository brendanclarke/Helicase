# S100 P2 — Implementation notes

**Plan:** `S100_P2_DELAY_FIXES.md` revision 3 (that document now
describes the as-built design, step 6 included). **Branch:**
`dev-ph7-alkaline`, base `6dc13b69`. **Started:** 2026-11-21.
**Status:** implemented, uncommitted.

Steps 1–5 record revision 3 as first built. Step 6 records your changes
after the first listening; where the two differ (pages, `ton`, `str`,
`wid`, `hld`, `dck`, the CPU and law counts), step 6 is the current state.

The plan's open questions are settled by your reply of 2026-11-21 ("no, that
sounds good"):
- `spr` stays relative;
- `xfb` ping-pongs the highs first;
- the main heads stay linear (no Hermite).

---

## Step 1 — roll back the withdrawn S100 change

`git checkout --` on the ten tracked files in plan §1. `git status` then
showed no modified tracked files.

## Step 2 — apply Appendix A

The Appendix A block was extracted from the plan, then `git apply --check`
and `git apply` were run. Nine files changed (+1,447 / −440), matching the
plan's measured worktree:
- `Core/DSP/Effects/EffectsManager.c`
- `Core/DSP/Effects/Space/SpaceEffect.c` and `.h`
- `Core/DSP/Effects/Space/SpaceParameters.c` and `.h`
- `Core/Menu/MenuLabels.c`
- `Core/Menu/menu.h`
- `tools/dsp_test/Makefile`
- `tools/dsp_test/modules/space.c`

## Step 3 — comment blocks

Most of Appendix A already carried What/Why comments from the measured
worktree. These had none or only one line, so they got full blocks:

| File | What was added |
|---|---|
| `Core/Menu/menu.h` | `LONG_STEREO` / `SHORT_STR`: renamed in place from `LONG_HEADS` / `SHORT_HED`, why, and that every later id is unchanged |
| `Core/Menu/MenuLabels.c` | the two renamed strings, `"str"` and `"Stereo"` |
| `SpaceEffect.c` `space_lane()` | What / Why / Cost; why linear and not Hermite (plan §4.9) |
| `SpaceEffect.c` `space_mono()` | the same, and what it replaces (`space_lin1()`) |
| `SpaceEffect.c` `space_tapeBegin()` step 2 | the loop's input gains: the Delay's mid/side with `kc` = gate/2 and `kx` = the allpass coefficient; the Echo's long head |
| `SpaceEffect.c` `space_tapeBegin()`, `hld` branch | the linear held loop, the gain law and the comb-tooth compensation, with their per-block cost |
| `SpaceEffect.c` `space_tapeBegin()`, normal branch | `fbk`, `deg`, each loop's playback LP and the `spr` darkening law |
| `SpaceEffect.c` `space_tapeBegin()` step 3 | the `str` laws for both types, with the pan and constant-power arithmetic |
| `SpaceParameters.h` | the S100 P2 re-order: shared indices, new and removed rows, stored-data effect |
| `SpaceParameters.c` | the `ton`/`wid` laws and defaults, `str`, the `xfb` side allpass, the Echo's `ton`/`wid`, `spr` and `str` |
| `tools/dsp_test/modules/space.c` | the `sp12_kicks()` helper |

The comments change no code: the CPU and the laws in step 4 match the
plan's worktree exactly.

## Step 4 — gates

### Space host laws

`make -C tools/dsp_test space` reports `space failures: 0` and
`crumpbit failures: 0`. 41 law lines pass. All nine break builds are
caught:

| Break build | Fails |
|---|---|
| `lin2` | SP11 |
| `xfb` | SP12 |
| `mono` | SP12 |
| `band` | SP13 |
| `hold` | SP8 |
| `deg` | SP14 |
| `wow` | SP15 |
| `str` | SP16 |
| `dark` | SP17 |

New laws, as measured:

| Law | Measured |
|---|---|
| SP8 `hld` | `fbk` 20 / `wid` 127: −0.63 dB; `fbk` 64: +1.83 dB; `fbk` 120 / `wid` 0: +14.12 dB |
| SP12 ping-pong | L 0.598/0.000 at D, R 0.400/0.001 at 2D, L 0.342/0.001 at 3D, all the input's polarity |
| SP12b | `xfb` 32: +8.2 dB; `xfb` 64: +8.3 dB |
| SP12c (`str` 0) | `xfb` 64 and 127: +0.00 dB on `xfb` 0 |
| SP12d (energy kept) | `xfb` 0: −5.68 dB; 32: −5.39 dB; 64: −5.65 dB |
| SP13 band | DC mean 0.2497; `wid` 0 rings at 968 Hz (want 961); `wid` 127: 200 Hz +0.08 dB, 4 kHz −0.61 dB |
| SP14 `deg` 127 | 0 non-zero samples from init; zero from 1.36 s after a burst at `fbk` 40; RMS −0.21 dB on `deg` 0 |
| SP15 `wow` | ±2.22 % at 127, ±0.006 % at 0 |
| SP16 `str` | 0: L = R sample for sample; 127, `spr` 126: L head 0.563 + tap 0.450 (0.260 on R), R head 0.423 + tap 0.407 |
| SP17 darkening | 0.404 at `wid` 127, 0.466 at `wid` 40 |

### Firmware

- `make all`: no errors; no new warnings in the changed files.
- `link_budget.py`: flash 732,248 / 753,664 B (headroom 21,416), ITCM
  15,432 / 16,384 B, DTCM statics 4,396 B, FXBUF unchanged (126,656 B,
  margin 3,776).
- `make img`: `build/LXRV2_lxr02.img`, 732,248 B payload, 732,264 B file.
- `space_cpu_check`:

  | Type | Total (cap) | With the feed (cap) |
  |---|---|---|
  | Delay | 225.9 (252.0) | 245.8 (272.0) |
  | Echo | 203.1 (210.5) | 223.1 (230.5) |
  | Reverb | 189.5, unchanged | |

### Whole host suite

`make -k -C tools/dsp_test all` was run on this tree and on a clean
worktree of `6dc13b69`. The failure lists are identical, 104 lines each,
and the only failures predate this work:
- `voice_control failures: 14` (laws 1, 4, 7 and 8);
- `alkaline`: "AL-OD0 golden file missing or short".

`make all` stops at the `alkaline` error, which is why `-k` was used. The
baseline worktree was removed afterwards.

Running the suite also regenerated tracked outputs:
- `tools/__pycache__/sampleoptimize.cpython-315.pyc`
- `tools/alkaline/__pycache__/alk_model.cpython-315.pyc`
- `tools/dsp_test/build/gen/*`
- `tools/dsp_test/build/space*`

No source outside the nine files in step 2 changed.

## Step 5 — on the device (first build)

Values here are revision 3's stored row values. Step 6 made `ton` and
`str` bipolar (page 0 = stored 63) and moved the pages; §6.10 has the
current list of things to listen for.

Copy `build/LXRV2_lxr02.img` to the card root and flash it (hold the main
encoder at power-on).

**First boot:** AutoSave bytes on Delay/Echo rows sit at their old indices,
so re-set those Scenes or start from a fresh Delay/Echo. Clear any FX lane
11 data that was on `hed`: its values 0..6 now set `str`, nearly mono.

**Things to listen for** (plan §9):

1. Your rows: `tim` 90, `fbk` 122, `ton` 63, `wid` 127, `spr` −1,
   `xfb` 62, the rest off. The repeats should build steadily to
   saturation; on the host, about −7 dBFS within 8 s.
2. `wid` toward 0 with `fbk` above 100: a ringing peak at `ton`. Sweep
   `ton` from 80 Hz to 12 kHz.
3. `xfb` 0 → 127: the highs start to ping-pong first, then the whole band
   at 127, with the same polarity. With `str` 0 (mono) the level should not
   drop.
4. `str` 0 → 64 → 127 with `spr` away from 0: mono, then one head a side,
   then the two taps filling the field between the heads' times. The Echo
   goes wider instead.
5. `hld` at `fbk` below 30 (level stays), 64 (slow rise), and 120 with
   `wid` 0 (rises).
6. `wow` up to 127: up to about 34 cents at 2.5 Hz plus flutter.
7. `deg`: no hiss at any setting. Above 63 the bit reduction comes in. At
   high `deg` with mid `fbk` a quiet tail can sustain, and the loudest
   level is lower. Both are accepted consequences of the −12 dBFS makeup.
8. `spr` with `wid` 127: the loop that runs ahead of `tim` is slightly
   darker.

**Not changed:** the Reverb, and the tracked `tools/dsp_test/build/
break_sp_lin1/` and `tools/dsp_test/build/space_break_lin1`, which are no
longer rebuilt.

---

## Step 6 — after the first listening (2026-11-21)

Your notes:
1. `ton` must be bipolar: `ton` 0 with `wid` 127 cuts nothing, raising
   `ton` cuts lows, lowering it cuts highs.
2. `wid`'s ends are right, but lowering it from the top cuts too fast.
3. The page order after `wid` should be `xfb spr str wow`, then
   `deg dck hld rev`.
4. `dck` does little: investigate, and double it at max if it works as
   designed.
5. Clicks at the edges of the buffer with `hld` and `rev`: interpolate them
   away.
6. (Sent mid-turn) `str` bipolar too.

This round supersedes the plan (`S100_P2_DELAY_FIXES.md`) on these points.

### 6.1 Investigation: the clicks (`hld`, `rev`)

**Probe:** a 220 Hz sine at 0.3 into the Delay (`tim` 64, `fbk` 60), with
the largest third difference of L per 0.25 s as the measure. The clean sine
gives about 0.002; a click shows as 0.05 or more.

| Case | Before `hld` | After `hld` (first build) |
|---|---|---|
| plain | 0.0019 | — |
| `rev` alone | 0.0015 | — (0.0011–0.0017, clean) |
| `hld` | 0.0019 | 0.44, then 0.21–0.29 every quarter second |
| `hld`, `wow` 64 | 0.0021 | 0.51, then 0.25–0.33 |
| `rev` + `hld` | 0.0015 | 0.045 |
| `rev` + `hld`, `wow` 64 | 0.0020 | 0.23, then 0.010–0.025 |

`rev` itself was clean; the clicks came from `hld`. Three causes, all in
`space_tapeBegin()`:

1. **The splice.** `hld` cut the input and switched the loop gain in one
   block. The held loop keeps the splice between the last input and the
   material one pass earlier, so the step came round on every pass.
2. **The shaper and playback LP** (`deg`'s drive and makeup, the cubic, the
   O8 bandwidth) also switched in one block.
3. **The read position.** The held read delay was rounded to a whole frame
   each block, and the `wow` swing faded ×0.9 a block in whole-frame steps.
   Each step was a jump of the read position (the `rev` + `wow` case).

### 6.2 Fix: the `hld` crossfade

- **`SPACE_HOLD_XF` = 69 blocks (50 ms).** `SpaceRuntime.hx` (the former
  padding byte, so no RAM) counts one block toward the row; x0/x1 are its
  value at the block's start and end, 0..1.
- **Input and loop gain** glide from x0 to x1 across every block, as
  per-frame ramps (`ps->ding`, `ps->dfbk`; two adds a frame in
  `space_delayRun()`).
- **Shaper, drive, makeup and playback LP** blend once a block by x1. The
  bit reduction switches at the crossfade's middle.
- **Read delay:** each one blends from glide + swing to the glide's nearest
  whole frame (exactly that frame once held), and ramps across the block,
  so it never jumps.
- **After** (SP18): `hld` 0.0019, `hld` + `wow` 64 0.0021, `hld` then
  release 0.0019, `rev` + `hld` 0.0015, `rev` + `hld` + `wow` 64 0.0020,
  `rev` 0.0017. That is the clean sine's own level.
- **Break build:** `space_break_holdxf` (one-block crossfade, no per-frame
  ramps: the first build) gives 0.28 and fails SP18.
- **What remains:** changing `tim` during `hld` moves the whole frame by one
  when the glide crosses a half frame, a ramped one-frame move. It was not
  probed.

### 6.3 Investigation: `dck`

`dck` works as designed (O1), but it was shallow:
- the wet gain is 1 − duck × `dck`, where duck follows the input's block
  peak (|L| + |R|, held at 1; 2 ms attack, 200 ms release);
- with the law `dck` = row/127, the deepest cut only equalled the input's
  peak level, so a −12 dBFS send ducked the repeats by about 6 dB.

**Fix:** `dck` = 2 × row/127, with the wet gain held at 0 or above. The law
is shared, so this applies to the Delay, the Echo (`space_tapeBegin()`) and
the Reverb (`space_plateGains()`).

**Measured** (SP20, a steady −12 dBFS send): −6.1 dB at `dck` 64,
−65.3 dB at 127 (fully ducked). `space_break_dck` (the old law) fails
SP20.

### 6.4 `ton` bipolar and the `wid` curve

`space_bandLaw()`:

- **The room** is 10 Hz – 21 kHz, 11.04 octaves around 458 Hz.
- **`ton`** is `DTYPE_PM63` (default 0 = stored 63).
  - t = (row − 63)/63. The band's centre is 458 Hz × 2^(4.71 t) above 0
    (12 kHz at +63) and 458 Hz × 2^(2.52 t) below (80 Hz at −63).
  - At 0 with `wid` 127 the band is exactly the room. Raising `ton` moves
    the low edge up, since the top is held at 21 kHz; lowering it moves the
    top edge down.
- **`wid`:** N = 0.18 × 61.3^(1 − (1 − w)²) octaves. The ends are as
  before (Q 8 at 0, the full 11.04 octaves at 127), but the slope is 0 at
  the top:

  | `wid` | 127 | 117 | 111 | 64 | 32 |
  |---|---|---|---|---|---|
  | Width (octaves) | 11.04 | 10.8 | 10.3 | 4.0 | 1.1 |

  The earlier curve was at 8 octaves by row 117 under the new room.
- **Measured** (SP19):

  | Check | Result |
  |---|---|
  | `ton` 0 / `wid` 127: 40 Hz | −0.42 dB from `fbk` |
  | `ton` +63: 100 Hz / 8 kHz | −8.82 dB / +0.01 dB |
  | `ton` −63: 8 kHz / 100 Hz | −8.19 dB / +0.02 dB |
  | `wid` 111: 40 Hz / 8 kHz | −0.17 dB / −0.28 dB against `wid` 127 |
  | `wid` 0 ring (SP13b) | 463 Hz, centre 458 Hz |

- **Break build:** `space_break_wid` (the earlier curve) fails SP19.

### 6.5 `str` bipolar

`DTYPE_PM63`, default 0 (stored 63) = one head a side; −63 is the mono sum.

| Type | Above 0 |
|---|---|
| Delay | the two taps fade in (u = s/64, constant power, as before) |
| Echo | wider (M/S, the side up to ×2) |

SP16 (0 → mono, 127 → taps) still passes with the stored values;
`space_break_str` was updated to the new expression.

### 6.6 Page order

| Type | SELECT 2 | SELECT 3 | SELECT 4 | SELECT 5 |
|---|---|---|---|---|
| Delay (your order) | tim fbk ton wid | xfb spr str wow | deg dck hld rev | |
| Echo (analogous; you did not specify it) | tim fbk ton wid | spr str wow deg | dck e>r bln dcy | lsh hsh rtg |

- **Moved together:** enums, descriptors, the law table and the page-text
  comments. FX lanes name rows by enum, so they are unchanged.
- **Shared indices:** `spr`, `str` and `deg` now sit at different indices
  on the two types and are read by type. `tim`, `fbk`, `ton` and `wid` are
  still shared, and that is still asserted.

### 6.7 Found on the way: the Echo was darkened

The `spr` darkening was meant for the Delay only (plan §4.8: the Echo's
loop is its head at `tim`), but the code also darkened the Echo's loop when
`spr` was above 0. Fixed: the darkening's spread is 0 on the Echo.

### 6.8 Host-test changes

- **Row arrays:** the new order; `ton` and `str` default to stored 63.
- **SP13b:** the centre is now 458 Hz.
- **SP8:** "second 1" now starts 0.25 s into the hold, after the crossfade
  and the first passes that still carry the pre-hold level.
  - Without that, `fbk` 20 read −2.35 dB, because the first pass was the
    louder pre-hold material, not because the hold lost level.
  - Measured now: −0.85, +1.70 and +13.10 dB.
- **SP17:** the second check is now "`spr` 0 ⇒ L and R match" (1.000). The
  earlier "narrower `wid` darkens less" comparison measured the band's own
  effect on the high share, not the darkening.
- **New:** SP18 (clicks), SP19 (`ton`, `wid`), SP20 (`dck`), and break
  builds `holdxf`, `wid` and `dck`.
  - `holdxf` first undid only the crossfade length. With the per-frame
    ramps still in place SP18 passed, so it now also removes the ramps:
    the first build's one-step switch. It gives 0.28 and fails SP18.
- **`space_break_str`:** its pattern follows the bipolar expression.

### 6.8b Files and comment blocks (step 6)

Step 6 touched six of the nine files:
- `SpaceEffect.c` and `.h`
- `SpaceParameters.c` and `.h`
- `tools/dsp_test/modules/space.c`
- `tools/dsp_test/Makefile`

`EffectsManager.c` (the lanes name rows by enum), `menu.h` and
`MenuLabels.c` are unchanged since step 3.

Comment blocks added or rewritten in step 6:

| Where | What it covers |
|---|---|
| `space_bandLaw()` | the room, the bipolar `ton` law, the `wid` curve with its numbers, the edges, why nothing below 10 Hz passes |
| `SPACE_LAW_DCK` | the investigation, the old and new depth, the clamp |
| `space_plateGains()` and `space_tapeBegin()` | the ducking clamp |
| `SPACE_HOLD_XF` | why 50 ms |
| `space_tapeBegin()` | the crossfade counter, the read-delay blend, the normal/`hld` loop block (the crossfade, the Echo darkening fix), the bipolar `str` laws |
| `space_pass_t` | the per-frame ramps |
| `space_delayRun()` | its header and the two ramps |
| `SpaceRuntime.hx` | the crossfade counter in the former padding byte |
| `SpaceParameters.h` | the re-order note, every row's comment |
| `SpaceParameters.c` | the Delay and Echo row comments, bipolar `ton`/`str`, `dck` |
| `space.c` | the row arrays, SP8, SP13b, SP17–SP20 |
| `Makefile` | the three new break builds |

### 6.9 Gates

- **Space laws:** `make -C tools/dsp_test space` reports
  `space failures: 0` and `crumpbit failures: 0`. 51 law lines pass, and
  all twelve break builds are caught: lin2, xfb, mono, band, hold, deg,
  wow, str, dark, holdxf, wid, dck.
- **CPU** (`space_cpu_check`):

  | Type | Total (cap) | With the feed (cap) | Before this round |
  |---|---|---|---|
  | Delay | 234.5 (252.0) | 254.5 (272.0) | 225.9 |
  | Echo | 205.5 (210.5) | 225.4 (230.5) | 203.1 |
  | Reverb | 189.5 | | 189.5 |

  - The Delay's +8.6 is the two per-frame ramps (loop 205 → 211) and the
    longer per-block path (605 → 689).
  - The Echo's +2.4 is the per-block path (917 → 992).
- **RAM:** none added. `hx` is the former padding byte; the header and
  `SpaceRuntime` are unchanged.
- **Firmware:** `make all` / `make img` succeed with no new warnings.
  Flash 732,536 / 753,664 B (headroom 21,128 B); ITCM 15,432 B; image
  `build/LXRV2_lxr02.img`.
- **Whole host suite** (`make -k all`), run twice:
  - **Second run:** identical to the `6dc13b69` baseline (only the two
    pre-existing failures: `voice_control` 14, the `alkaline` golden file).
  - **First run:** a Colloid law also failed once ("CO-PWG full-slew width
    clicks", 0.00549 against its limit). It passed four further runs
    (three alone, one in the full suite), and the Colloid code is untouched
    by this work. It is intermittent and unrelated; worth a look in its own
    session.

### 6.10 To listen for

- **`ton`:** at 0 (`wid` 127) nothing is cut; + cuts the lows, − cuts the
  highs; with `wid` low it sweeps the peak.
- **`wid`:** a slower narrowing from the top.
- **`str`:** 0 is one head a side, − toward mono, + the taps (Delay) or
  wider (Echo).
- **`dck` at max:** the repeats fully ducked while the input plays.
- **`hld` on and off** (with `rev`, with `wow`): no clicks. Engaging and
  releasing take 50 ms.

**First boot:** the rows moved again, so re-set the Delay/Echo Scenes.
