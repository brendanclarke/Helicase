# S101 — Implementation notes

**Date:** 2026-11-21. **Branch:** `dev-ph7-alkaline`, base `8476271a` ("delay
tweaks", the S100 P2 commit). **Status:** implemented, uncommitted, built.
- **B1/B3:** on the device (`SD_CARD_S101`, §6).
- **D1/S1:** built and gated, not yet on the device (§10).

**Plan:** `S101_P1_CPU_INVESTIGATION.md`. This document records what was built
from it, what was not, and why.

| Item | Status |
|---|---|
| B1 trace flushes off the 1 ms SD burst; Flush Finish ends its burst when flushed | **done**, device-checked (§1, §6) |
| B3 panel input scan after every render commit | **done**, device-checked (§2, §6) |
| B2 save poll only while both audio slots are full | **not built**: measured analysis shows it would lengthen saves (§3) |
| D1 Advanced Alkaline chorus at half rate | **done** (user, 2026-11-21: "absolutely") (§4) |
| S1 probe maxima and the span outside the mixer | **done** (RAM approved, 84 B, logging builds only) (§5) |
| B4 trace file hygiene | done by you on `SD_CARD_S101` (a fresh 5,760 B trace) |
| B5 release-build check | device step (§10) |
| D2 send-add constants, D3 control section | **not built**: measurement first (§8) |
| Is Scene 5 the worst case? | **no**: estimate in §7 |
| Comment corrections (investigation §9) | **done** in code comments (§1.3) |

**DSP:** one engine changed, the Advanced Alkaline's chorus (D1). Every
other per-sample loop is unchanged (§9.2).

**RAM:** +84 B, logging builds only (S1). A release build allocates nothing
new.

---

## 1. B1 — the SD poll burst

### 1.1 What changed

`filesystem_tick()`, `Core/Hardware/SD/filesystem.c:29190`:

```c
if (status == FS_STATUS_BUSY && filesystem_drainPollEligible()) {
    const uint32_t poll_start_us = timebase_tim2Now();

    while (timebase_tim2Delta(timebase_tim2Now(), poll_start_us) <
           AUTOSAVE_DRAIN_POLL_BUDGET_US) {
        if (current_op == FS_INTERNAL_OP_FLUSH_FINISH &&
            afatfs_isFlushed())
            break;
        afatfs_poll();
    }
}
```

**Before:** the same burst ran for the scalar and Pattern AutoSave drains,
Flush Finish and both trace flushes, unconditionally, for the full 1,000 µs.

**Now:**
1. **The drains** keep the full burst. Their speed is bound by the
   foreground share, not by this loop (§3).
2. **Trace flushes** (`AUTOSAVE_TRACE_FLUSH`, `PATTERN_TRACE_FLUSH`, logging
   builds only) get the burst only while core work waits for the facade:
   - `filesystem_coreSavePending()`: a save is pending (the condition the
     S099 P3 comment always described);
   - `menu_loadSaveStorageWaiting()`: a Load/Save page is open or a command
     is active, so a trace flush in flight when the page opens does not hold
     its first request back at one poll per pass.

   Otherwise a trace flush gets the ordinary one poll per pass.
3. **Flush Finish** checks `afatfs_isFlushed()` before each poll and stops
   the burst once AsyncFATFS has nothing left to write.
   - A trace flush syncs before it finishes (its phase 3), so the Flush
     Finish closing it had nothing to do and still spun 1 ms.
   - The same holds for any operation whose last write already completed
     (the scan and index operations).

New helper `filesystem_drainPollEligible()` (`filesystem.c:28958`): a
`switch` on `current_op`. The trace cases are under `#if DEV_MODE_LOGGING`,
as the trace operations and `filesystem_coreSavePending()` are.

### 1.2 New AsyncFATFS predicate

`afatfs_isFlushed()` (`asyncfatfs.c`, after `afatfs_sync()`; declared in
`asyncfatfs.h`):

```c
return (bool)(afatfs.cacheDirtyEntries == 0 && !afatfs.cacheFlushInProgress);
```

- **Read only:** it starts no write, unlike `afatfs_sync()`/`afatfs_flush()`.
- **At least as strict as `afatfs_sync()`'s true result:** a locked dirty
  sector keeps it false, so in that case the burst runs as before. It never
  ends a burst that `afatfs_sync()` would still continue.
- After the break, `filesystem_flushFinish_tick()` calls `afatfs_sync()`
  as before, gets true and completes. The completion path is unchanged.

### 1.3 Comment blocks

- `filesystem.c:29168–29189`: the burst block. It records both corrections,
  the S101 reason, and that the drains keep the full burst.
- `filesystem.c:28943–28957`: `filesystem_drainPollEligible()`: what, why,
  inputs, output, accessor.
- `asyncfatfs.c`, `afatfs_isFlushed()`: what, why, inputs, output,
  affiliates.
- `asyncfatfs.h`: the declaration's contract.
- `config.h`, `AUTOSAVE_DRAIN_POLL_BUDGET_US`: the trace-flush sentence now
  matches the code, plus an S101 note (Flush Finish early end; 1 ms of burst
  ≈ 20 ms of pass at a 93 % load).
- `config.h`, `AUTOSAVE_WRITER_INTERVAL_MS`: S101 note with the measured
  7.2–8.6 s save at this Scene's load (the S099 "3–4 s" holds only at lower
  load).

---

## 2. B3 — panel input scan after every render commit

### 2.1 What changed

**`Core/Hardware/timebase.c`:**
- **The flag byte:** `frontpanel_service_due` now carries two flags,
  `FP_DUE_SCAN` (0x01) and `FP_DUE_SERVICE` (0x02). `TIM6_DAC_IRQHandler()`
  sets both every 2 ms (one `ORR`). Before, it was a counter capped at 2
  that its one consumer cleared to 0, i.e. already a flag. **No new RAM.**
- **`timebase_takeFrontPanelDue(flag)`** (static): tests the flag, then
  clears only that flag inside `cpsid`/`cpsie`.
- **New `timebase_serviceFrontPanelInput()`:** when `FP_DUE_SCAN` is set,
  runs `dout_latch()`, `din_dout_exchange()` (debounce and the button event
  ring), the output-jack detect and `encoder_tick()` (encoder button).
- **`timebase_serviceFrontPanel()`:** calls the input scan first, then, when
  `FP_DUE_SERVICE` is set, `endlessPots_tick()`, `liveRec_service()`,
  `patSvc_tick()`, `ccSvc_tick()`. Same order as before.

**`main.c`**, in `audio_check_and_render()`: one call to
`timebase_serviceFrontPanelInput()` after each
`audioCodec_commitRenderBuffer()`, outside the 32-frame render loop.

**`timebase.h`:** declaration and contract of the new function; the existing
function's comment names the services it now holds.

### 2.2 Decisions taken while building (they differ from the investigation's B3 text)

1. **Owed scans are not served back to back.** The investigation suggested
   looping while scans are owed. The debounce (`din.c:82`) counts three equal
   samples, so two scans at the same instant would count as two samples and
   shorten the debounce in time. One scan per 2 ms flag is kept: dropping an
   owed scan is correct for the debounce. The fix is the cadence.
2. **The endless pots stay on the main-loop cadence.**
   - `endlessPots_tick()` computes an angle for four pots per tick.
   - It moves to the `FP_DUE_SERVICE` half, so it runs once per main-loop
     pass when due, as before.
   - The render path gets only the cheap part: 5 SPI bytes at 13.5 MHz,
     41 debounce steps, two GPIO reads.
   - Encoder rotation is TIM1 capture and is not affected.

### 2.3 Effect

- **Cadence:**
  - the button chain is sampled after every committed DMA half (about every
    2.2 ms) whenever a scan is due;
  - inside one long foreground task the scan waits for that task: at most
    the 1 ms burst during a save;
  - the three-sample debounce therefore needs about 6–7 ms again, against
    about 60 ms at 20 ms passes.
- **What still waits for the pass:** taps are no longer lost, but their menu
  action waits for `buttonHandler_processEvents()`. That runs twice per pass,
  so during a save, about 20 ms.
- **Events per pass:** the event ring (64) now takes about ten scans per
  20 ms pass. Its overflow path is unchanged.
- **Context:** both callers are the main loop. The LED shadow, the debounce
  state and the ring keep their one foreground owner. The only render call
  site is the main loop (`main.c`; `AudioCodecManager.c:803` is the
  commented-out sine test), so the scan cannot run inside a button handler.
- **Cost (estimate):** about 5 µs per scan. While playing this Scene, scans
  rise from about 50 to about 460 a second: roughly 2 ms a second, about
  0.2 % CPU, taken from the foreground share. With the transport stopped,
  or at light load, the scan already ran at 500 Hz.

### 2.4 Comment blocks

- `timebase.c:119–137`: the two-flag byte: what, why, and the earlier
  counter's meaning.
- `timebase.c`, `timebase_serviceFrontPanelInput()`: what, why, accessors,
  cost, and why owed scans are not run back to back.
- `main.c`, after the commit: why the call is after the commit and outside
  the 32-frame loop.
- `buttonHandler.h:5–9`, `buttonHandler.c:5–8`: the concurrency notes name
  the new scan path.
- `buttonHandler.c`, ring comment: several scans can fall in one pass, and
  the ring holds their events.

---

## 3. B2 — not built (measured reasoning)

The investigation proposed polling the save only while both audio slots are
full, so passes stay short. The trace says this would make saves longer.

- **Polling per save:** about 390 passes, each at least 1 ms (measured: 4,583
  slow drain ticks over 12 drains; 12,685 over 32). That is about 0.39–0.40 s
  of SD polling, plus the drain's own CPU.
- **Foreground share:** about 6.6 % of the time while this Scene plays (mixer
  93.4 %).
- **Lower bound:** even with the whole foreground, a save needs at least
  0.40 s / 0.066 ≈ **6 s**. The measured 7.2–8.6 s is close to that bound.
- **B2 would lower the save's share** (the idle window is only part of the
  foreground), so the save would get longer than 8 s, further past rule
  §0's 5 s design.
- **Buttons no longer need B2:** after B3 the scan does not depend on the
  pass length.

What can shorten a save at this load:
- **Free CPU:** each 1 % freed at 93 % shortens it by about 13–15 %.
- **Fewer SD bytes per save:** the section-CRC format already chosen
  (`VOLATILE_MEMORY.md` "Capture budget").
- **Cheaper SD polling:** the bit-bang driver clocks 16 bytes per poll.

Each is its own decision. None is built here.

---

## 4. D1 — the Advanced Alkaline chorus at half rate

**Decision** (user, 2026-11-21): "absolutely, if that's the worst case
instrument put in the half-rate chorus".
- **Measured in Scene 5:** the Advanced Colloid is the dearer Advanced slot
  (19,970 cycles against the Advanced Alkaline's 18,290). The Colloid's
  chorus was already at half rate.
- **So what this change does:** it moves the only full-rate chorus, the
  Alkaline's, to the same form. It lowers the Advanced Alkaline's cost in
  every Scene; it does not lower the dearest Advanced slot of Scene 5.

### 4.1 What changed

**`Core/DSP/Instruments/Chorus/BbdChorus.h`:**
- **New `BBD_ALK_HALF_RATE`** (default 1), with a what / why / sound /
  inputs / accessors block. 0 builds the full-rate loops (the bench keeps
  both proven), like `BBD_COL_HALF_RATE`.
- **Comments updated:** the delay-range comment (the rules hold at half rate
  for the Alkaline's one-ring pass B, as for the mid form), the
  `BBD_COL_HALF_RATE` block (the Alkaline no longer "always runs full
  rate"), and `BbdChorusState.half`.

**`Core/DSP/Instruments/Chorus/BbdChorus.c`:**
- **`bbd_alkWH()`, the write pass**, per output pair:
  - the dry copy to the right block;
  - the pre pole at the full rate on both samples;
  - the ring sample `clip(((p0 + p1) << 2) × gDrv)`: the pair's mean times
    the BBD drive, hard-clipped.
  - `(p0 + p1) << 2` is the mean × 8 of the full-rate form's `p << 3`,
    without the mean's floor.
  - The guard is refreshed after the block that wrote index 0.
- **`bbd_alkTH()`, one side's tap at half rate:**
  - one interpolated tap per pair;
  - both post poles (the 12 dB wet) at the full rate, on `(tq + t) / 2` for
    the first output, then on `t`;
  - the dry + wet gains at the full rate (the dry is exact).
- **`bbd_alkAH()` = `bbd_alkWH()` + `bbd_alkTH()`** on the left (pass A).
  Pass B is `bbd_alkTH()` on the right, on the same ring, from pass A's dry
  copy.
- **`bbdChorus_renderAlkaline()`** selects the forms by `BBD_ALK_HALF_RATE`
  and advances the write count 16 a block.
- **`bbdChorus_init()`:** `st->half = mono ? BBD_ALK_HALF_RATE :
  BBD_COL_HALF_RATE`, so `bbd_sidePlan()` halves the read position.
- **Unchanged:**
  - the delay range in output samples (65..990 = 32.5..495 ring samples,
    against 16 written a block);
  - the tail rule (`BBD_TAIL_BLOCKS`, set by the delay in output samples);
  - `bbdChorus_control()` (142 a block);
  - the unit layout.

### 4.2 Why two loops (measured)

- **The first build** put the whole pair in one loop (`bbd_sideH()`'s
  shape). It measured **60 instructions a pair with about a dozen stack
  loads and stores**: the pre pole, drive, ring write, tap and both post
  poles need more registers than the M7 has.
- **Splitting** the write from the tap removes every stack access:

  | Loop (`elf_looppath.py --cond-heads --in-range`) | Instructions |
  |---|---:|
  | `bbd_alkWH()` (write) | 17 a pair |
  | `bbd_alkTH()` (tap), left and right | 33 a pair each |

- **The split is bit-identical to the one-loop form.** A scratch harness
  ran both on the same 20,000 random blocks (random tone, drive, mix, input
  and delays) and compared every output sample and the whole ring: 0 blocks
  differ. Per state variable, the operations and their order are the same.
  The taps read at least 32.5 ring samples back, so writing the block first
  changes nothing they read.

### 4.3 Cost

Instructions per output sample, the S093 method: the loops plus the
per-block paths ÷ 32.

| | Loops | `bbdChorus_renderAlkaline` per block | `bbdChorus_control` per block | Per sample |
|---|---:|---:|---:|---:|
| Before (full rate: `bbd_alkA` 34, `bbd_alkB` 20) | 54.0 | 43 | 142 | **59.8** |
| After (`bbd_alkWH` 17 + 2 × `bbd_alkTH` 33, a pair ÷ 2) | 41.5 | 58 | 142 | **47.8** |

- **−12.0 instructions a sample**, about −385 a block.
- **In cycles:** about **0.45–0.55k a block** at the device's 1.2–1.4 cycles
  an instruction, about 0.3 % CPU. That is below the investigation's
  1.0–1.3k estimate.
- **Why only that much:** the post poles and the output gains, which stay at
  the full rate, are most of the chorus loop. Halving them would be a larger
  sound change (a half-rate wet output) and was not proposed.
- **Device figure:** to come from the S1 probe (§10).

### 4.4 Sound

The Colloid's C3 trade, now also on the Advanced Alkaline:
- the wet's highs above about 11 kHz fold and image (bounded by CH-AHALF);
- the BBD drive's clip acts on one ring sample per pair, so its harmonics
  are made at the ring's 22 kHz rate and fold around 11 kHz;
- the dry is exact;
- at 6 kHz the wet is 2.46 dB below the full-rate form, the same as the
  Colloid's half-rate form;
- **to listen for** (§10): the chorus at high `dpt`, and high `drv` (the
  clip).

### 4.5 Host laws (`modules/chorus.c`, `make -C tools/dsp_test chorus`)

- **CH-AHALF (new):** the half-rate Alkaline against the full-rate one, on
  both outputs (left = pass A, right = pass B), CH-HALF's method (154 output
  samples, wet only, drive ×1). Measured:

  | Check | Bound | Measured |
  |---|---|---|
  | 1 kHz level, half against full | ±0.5 dB | −0.07 dB |
  | Images of 2 / 5 kHz at 22,054 − f | ≤ −18 dB re the wet | −43.1 / −24.3 dB |
  | Folds of 15 / 18 kHz | ≤ −10 dB re the input | −16.3 / −17.7 dB |
  | 6 kHz half / full ratio against the Colloid form's | within 0.3 dB | −2.46 against −2.46 dB |

  The last check proves the two post poles. The pair mean and the tap
  interpolation are the same structure as the Colloid's, and the poles run
  at the full rate in both forms, so the ratios must match.
- **CH-GUARD (extended):** `bbd_alkAH()` at the BBD drive ×1 against an
  exact circular ring of pair means, over 1,000 blocks at delays swept over
  65..990.
- **CH-ALK (adapted):**
  - the drive-0 ring law checks the input at full rate, and the floor of
    each pair's mean (16 a block) at half rate;
  - a new check of the write count's step;
  - the 16 kHz 12 dB-wet law now runs the full-rate loops by name (16 kHz
    is above the half-rate ring's band; the half form is held to the full
    one by CH-AHALF).
- **Builds:** the default (both half) and `-DBBD_COL_HALF_RATE=0
  -DBBD_ALK_HALF_RATE=0` (both full) (Makefile). All eleven groups pass in
  both.
- **Break proofs:**
  - **`chorus_break_ahalf` (new):** removes one post pole from
    `bbd_alkTH()`. Caught: CH-AHALF fails, the 6 kHz ratio −1.82 against
    −2.46 dB.
  - **A guard break limited to `bbd_alkWH()`**, run once in the scratchpad:
    CH-GUARD fails ("half-rate Alkaline guard not refreshed").
  - **The three S099 breaks** are still caught.

---

## 5. S1 — probe maxima and the span outside the mixer (logging builds only)

**RAM approved** (user, 2026-11-21): 84 B, SRAM1 `.bss`, `DEV_MODE_LOGGING`
only, owner `mixer.c`.

### 5.1 What changed

**`Core/DSPAudio/mixer.c`**, the S094 P2 probe:
- **New arrays:**

  | Array | Bytes | Content |
  |---|---:|---|
  | `mixer_probe_async_max[6]` | 24 | per slot, block-rate |
  | `mixer_probe_sync_max[6]` | 24 | per slot, render |
  | `mixer_probe_sec_max[7]` | 28 | per section |
  | `mixer_probe_outside_sum`, `mixer_probe_outside_max` | 4 + 4 | outside the mixer |

- **How each max word works:** it holds `(this block's running sum << 16) |
  the window's maximum`.
  - `mixer_probeAdd()` finds the max word that shadows the accumulator it
    was handed (`mixer_probeMaxWord()`, by address) and adds the span to the
    high half, saturated.
  - `mixer_probeEnd()` folds the high half into the low half at every block
    end (`mixer_probeFold()`).
  - So a slot's two block-rate spans (filter, async) and a section's
    several spans per block (six set-up, SRR and send-add spans) count as
    one block, with no extra RAM.
- **No call site in `mixer_calcNextSampleBlock()` changed.** Only the
  out-of-line probe bodies did, so the mixer's loops stay (§9.2).
- **New `mixer_probeNow()` / `mixer_probeOutside(start)`** (noinline,
  declared in `mixer.h` under `DEV_MODE_LOGGING`): the outside-mixer span's
  sum and maximum.
- **Fifteen more records** at each window end (31 in all):

  | Flags | Value |
  |---|---|
  | `6 \| (9 + slot) << 3` | that slot's largest block-rate `<< 16` \| largest render |
  | `6 \| (16 + section) << 3` | that section's largest block |
  | `6 \| 24 << 3` | outside-mixer average per block |
  | `6 \| 25 << 3` | outside-mixer largest block |

**`main.c`:** in the 32-frame loop, under `DEV_MODE_LOGGING`, one local
`probe_start = mixer_probeNow()` before `voiceControl_processPending()`, and
`mixer_probeOutside(probe_start)` after `seq_rampService()`.
- This measures the trigger drain and the ramps, which run outside the
  mixer and were not measured before.
- The span counts any ISR that preempts it (it runs without the BASEPRI
  mask).

**`Core/Bank/Scene/AutosaveTrace.h`:** the `p` stage's layout comment now
lists the new codes.

**`tools/diagnostic_tools/decode_devlogs.py`:** decodes the new codes
("slotN MAX block-rate / render", "mixer <section> MAX block", "outside the
mixer average / maximum"). Self-tested on synthetic records of each code.

### 5.2 What the maxima mean

- **Per slot and per section:** the largest single block of that slot or
  section in the 2,048-block window (about 1.5 s).
- **Not recorded:** which block the whole-block maximum was. The per-section
  split of the worst block would need 19 more words and was not in the
  approved RAM. The per-slot and per-section maxima are each their own
  largest block, so their sum can exceed the whole-block maximum.
- **Cost:** a range test and a saturating add per probe call, and a 19-word
  fold per block (about 100 cycles, after the block's own measurement).
  Logging builds only.

---

## 6. B1/B3 on the device (`SD_CARD_S101`, 2026-11-21)

**Image:** `SD_CARD_S101/LXRV2_lxr02.img`, 732,604 B: the B1/B3 build, before
D1 and S1. You removed the old trace first (B4): `asavetrc.bin` is 5,760 B,
one boot, 33 probe windows (about 50 s). Scene: the same slots (`tkt drm
alx clx col hat`).

| | Before (`SD_CARD_S100`) | After (`SD_CARD_S101`) |
|---|---|---|
| CPU widget (yours) | 99 % | **96–97 %** |
| Mixer average (probe) | 146.4k (93.4 %) | 146.8k (93.7 %): unchanged, B1/B3 change no DSP |
| Mixer worst block per window (median / max) | 163.9k / 165.5k | 162.3k / 166.3k |
| Worst main-loop pass per second (median) | 19.5–24 ms | **6.5 ms** (4.4–6.5 ms in most seconds) |
| Worst pass per second (p90 / max) | 21.7–26.1 / 56.6 ms | 24.0 / 41.3 ms (one Load/Save visit, below) |
| `filesystem_tick()` ≥ 1 ms | about 32,000 records over four boots | **none** |
| Underruns | one at boot | one at boot |

- **The widget drop is foreground time, not DSP.** The widget is audio queue
  pressure. With the 1 ms bursts gone from the trace-flush passes, the
  render catches up sooner and the queue sits fuller. The mixer itself costs
  the same.
- **The long seconds** (18–26 s, 17–39 ms) are one Load/Save visit: a Kit
  Save page (`O` lifecycle records), an HCNAMES Kit load, a library index
  read.
  - With the page open, a trace flush gets the burst by design (§1.1, item
    2).
  - Even the idle passes there ran 21–26 ms from the page's own work.
  - Before S101 every second looked like that; now only the Load/Save visit
    does.
- **Not covered by this run:**
  - no AutoSave drain ran (no edit was saved), so the save duration is not
    re-measured;
  - you did not report how fast taps felt.

---

## 7. Is Scene 5 the worst case? No (estimate)

The CPU rule counts headroom only against the worst case: every slot at its
dearest setting (`CPU_USE_DSP_AUDIT.md` §Binding Mandates). Scene 5 is far
from that in its Basic slots.

| | Scene 5 (measured) | Worst case (the S094 §21 shares, or measured) |
|---|---:|---:|
| 4 Basic slots | 65.0k (`tkt` 18.1, `drm` 13.9, `col` 17.5, `hat` 15.5) | **78.5k** (4 × 19,620; the Tetrakt alone measured a 19,995 median and 21,893 high in S096 P1) |
| 2 Advanced slots | 38.3k (`alx` 18.3, `clx` 20.0) | **43.6k** (2 × 21,803; `clx` reached 22,158 in one window) |
| Effect | 14.2k (Delay) | **16.6k** (the share; CrumpBit `grn` 16.3k in S094) |
| Control, `effects_service`, set-up, SRR, send adds | 24.3k | 24.3k (measured; it grows with stereo voices and active sends) |
| Bus compressor | 4.6k | 4.6k when on |
| **Mixer total** | **146.4k (93.4 %)** | **≈ 167.6k (107 %)**; ≈ 163.0k (104 %) with the compressor off |

What the table means:
1. **A worst-case Scene would overrun.** Its mixer alone would average
   104–107 % of the block, before the trigger drain, the ISRs and the
   foreground. It would underrun, not just feel sluggish. The gap between
   Scene 5 and that is about 17–21k cycles a block, mostly in the Basic
   slots (13.5k).
2. **The S094 §21 budget does not close on today's measurements.**
   - It allotted 12,784 to the shared mixer (control, set-up, compressor),
     plus 4,701 outside the mixer.
   - Measured today: 28.9k outside the slots and the Effect (24.3k + the
     compressor).
   - Either the slot shares were meant to include each slot's send adds and
     SRR (about 2k a slot), or these sections grew since S094. Either way,
     4 × 19,620 + 2 × 21,803 + 16,634 + 28.9k does not fit 152,004.
3. **What would settle it:** a worst-case test Scene, measured with the S1
   probe (§10 step 5). The per-type dearest settings are recorded in the
   module references:
   - Drum, Snare, Cymbal/HiHat: `INSTRUMENTS_DSP_REFERENCE.md`,
     `CPU_USE_DSP_AUDIT.md` items 73–80;
   - Alkaline: `ALKALINE_MODULE_REFERENCE.md` §5 (R-S, 262.8 a sample);
   - Colloid, Tetrakt: their module references;
   - Space Delay: `space_cpu.py`, 234.5 a frame, 254.5 with the feed.

   The Scene to build:
   - four Basic slots at their dearest (for example two Tetrakts at four
     layers with full drive, a Drum and a HiHat at their dearest chains);
   - both Advanced slots at their dearest modes with the chorus on;
   - the Delay with every row active;
   - the compressor on;
   - every send above 0.

   The S1 maxima then give each slot's and section's largest block.
4. **What that means for reductions:** in Scene 5 the Basic slots have
   13.5k of unused share, and the Scene still reaches 99 % on the widget.
   The fixed shared costs (24.3k) and the Basic dearest settings are where
   the worst case is decided. D2 (the send adds, 10.8–11.3k) and D3 (control,
   7.0k) are the largest shared items; §8.

---

## 8. Not built, and what each needs

### D2 — send-add constants

`elf_hotloops.py` on `mixer_addVoiceInt16ToOutputAndFx()` and its stereo
twin, to see what `-Ofast` already folds (the per-sample ramp, the
`× 256.0f`, the send pan). Estimated −0.6–1.1k a block. S1 class (last-bit
differences in the ramp). Your decision after the figure.

### D3 — control section

`control_cost.py` / `elf_blockpath.py` on `instrumentManager_dispatchRuntimeLfos()`
and the target restores. Scene 5 has an LFO on a live target in five slots.
No figure yet.

---

## 9. Gates (the final image: B1 + B3 + D1 + S1)

### 9.1 Build

`make all && make img` succeed. Image `build/LXRV2_lxr02.img`.

| | Baseline (`8476271a` tree) | B1 + B3 (on `SD_CARD_S101`) | **Final** |
|---|---:|---:|---:|
| Flash | 732,536 B | 732,588 B | **733,548 B** (+1,012; headroom 20,116 B) |
| ITCM | 15,432 B | 15,432 B | 15,432 B |
| DTCM statics | 4,396 B | 4,396 B | 4,396 B |
| FXBUF margin | 3,776 B | 3,776 B | 3,776 B |
| RAM symbols (name + size) | 972 | 972, identical | **977**: the five S1 arrays (84 B) added, every other symbol identical |

- **`size`:** `data` +4 B, `bss` +80 B against the baseline: the 84 B of S1.
  B1/B3 alone were layout only.
- **Warnings:** none in the S101 files beyond the pre-existing ones
  (`SplashAnimation.c`, unused `filesystem.c` helpers, `asyncfatfs.c:1354`
  `eraseCount`, the newlib stubs).
- **Flash:** +1,012 B in all. B1/B3 +52 B, the new chorus loops
  (`bbdChorus_renderAlkaline` 330 → 474 B), and the S1 probe bodies
  (`mixer_probeEnd` 264 → 712 B; the `mixer_probeAdd` clones 36–40 →
  124–132 B).
- **Baseline:** built from the same tree before the S101 edits. Its ELF and
  loop snapshots were kept in the session scratchpad, not in `build/`.

### 9.2 DSP loops (CPU rule)

Against a snapshot of the baseline image (the tracked baselines predate
S100):

- **`make -C tools/dsp_test elf_p4_check`:** 85 baseline loops, 85 exact,
  0 renamed, 0 missing. Mixer: **59 hot loops equal, 0 shrank, 0 grew**.
- **`make -C tools/dsp_test elf_s099_check`:** 15 of 15 exact (Alkaline
  B2R, Colloid oscillator and filter loops, Tetrakt sync block).
- **Functions that changed size against the B1/B3 image:**
  - `bbdChorus_renderAlkaline` 330 → 474 B (D1, intended, §4.3);
  - `bbdChorus_init` (the `half` line);
  - the probe functions (S1);
  - `mixer_calcNextSampleBlock` +12 B;
  - `audio_check_and_render.part.0` (the S1 local);
  - UI, Menu, AutoSave-mark and Preset setters (LTO re-partition).

  No other voice, oscillator, filter, Effect or Space function changed.
- **`mixer_calcNextSampleBlock` +12 B, by `objdump` diff** (addresses
  normalised; 25 changed lines):
  - two probe calls now go to different `mixer_probeAdd` clones (LTO made a
    third), one an instruction shorter;
  - alignment padding;
  - six `ldr` became `ldr.w`: the same instruction in the 32-bit encoding,
    because the merged probe globals moved further from their base. These
    sit in per-block code after a loop's closing branch, with the same
    instruction count.
  - No per-sample loop changed (the hot-loop gate above).
- **`audio_check_and_render.part.0`** (the render driver, with
  `voiceControl_processPending()` and `seq_rampService()` inlined):
  - B3 added the inlined flag test and `bl` after the commit;
  - S1 adds `mixer_probeNow()` / `mixer_probeOutside()` around the trigger
    drain (logging builds only);
  - LTO re-allocated registers through the inlined drain;
  - it holds no per-sample loop: per-block and per-trigger control code,
    outside the mixer.
- **`TIM6_DAC_IRQHandler`:** 80 → 64 B (B3).

### 9.3 Host suite

`make -k -C tools/dsp_test all` (final tree): identical to the S100 P2
baseline apart from the new chorus laws. The only failures are the two
pre-existing ones: `voice_control` 14, and the `alkaline` golden file
(AL-OD0, missing).
- **Chorus:** every group passes in both builds, and all four breaks are
  caught (§4.5).
- **No host program compiles** `filesystem.c`, `timebase.c`, `main.c`,
  AsyncFATFS, the button code or `mixer.c`'s probe, so no law covers B1, B3
  or S1. Their tests are the device (§6, §10).

---

## 10. On the device (the final image)

1. **Flash:** copy `build/LXRV2_lxr02.img` to the card root and flash.
   Optionally delete `asavetrc.bin` / `pattrace.bin` first for a clean
   trace.
2. **Bank 076, Scene 5, playing:**
   - **the widget:** expect about 96 %, or a little under (D1 is about
     0.3 %);
   - **taps:** fast single and double taps; then edit a parameter and keep
     tapping for about 10 s (a save runs about 8 s at this load);
   - **listen** to the Advanced Alkaline's chorus (slot 2): high `dpt`
     (the doubler end), and high `drv` (the BBD clip now at the ring's
     22 kHz rate). Compare with the Colloid's, which has had this form since
     S099.
3. **Trace, decoded with `decode_devlogs.py`:**
   - **`p` "slot2 alx"** average and "slot2 MAX": the D1 figure on the
     device (expect about 0.45–0.55k below 18,290);
   - **the new MAX records:** which slot or section makes the 17.5k spike
     (the S1 question of the investigation, §2.3);
   - **"outside the mixer" average and maximum:** the trigger drain's
     share, measured for the first time;
   - **`f`:** an AutoSave drain's duration (first slow tick to "ended"),
     to confirm B1 did not slow saves.
4. **B5 (optional):** the same Scene on a `DEV_MODE_LOGGING 0` build.
5. **The worst-case Scene (§7):** build it, play it with the probe on, and
   send the trace. That gives the worst case the CPU rule asks for, and
   whether it fits at all.

---

## 11. Statements this makes out of date (for you to act on)

The code comments were updated in this session. These records were not:

- **`MEMORY.md` AutoSave trace rule §0 bullet / S099 P3 records:** "an edit
  is on the card about 3–4 s after it is made" holds only at lower load.
  Measured while Bank 076 Scene 5 plays: 7.2–8.6 s per save.
- **`MEMORY.md` "Load/Save never freezes" bullet:** "While playing there is
  no free render time (CPU widget 99–100 %)". Measured: about 6.6 % free at
  a 93.4 % mixer average; the widget now reads 96–97 % in that Scene.
- **`MEMORY.md` Colloid / Alkaline bullets, and
  `ALKALINE_MODULE_REFERENCE.md`:** the Advanced Alkaline's chorus is now
  half rate (`BBD_ALK_HALF_RATE`, S101), like the Colloid's; "full-rate
  wet, mono-in stereo-out" is out of date.
- **`MEMORY.md` "Cycle-based budget (S094 P2 §21)":** the shares do not
  close on today's measurements (§7, item 2).
- **`MEMORY.md` RAM:** +84 B SRAM1, logging builds only (S1 probe).
- **`MENU_UI_SPEC.md` / `HARDWARE_MAP.md`, wherever they say the button scan
  runs once per main-loop pass from `timebase_serviceFrontPanel()`:** it now
  also runs after every render commit (`timebase_serviceFrontPanelInput()`),
  and the endless pots tick stays on the main-loop cadence.
- **`FILESYSTEM_SPEC.md` / `AUTOSAVE.md`, wherever they describe the S099
  P3 poll burst:** trace flushes get it only while a save is pending or
  Load/Save waits, and Flush Finish ends it once AsyncFATFS is flushed
  (`afatfs_isFlushed()`, new in `ASYNCFATFS_REFERENCE.md`'s API).
- **`DEV_MODES.md` / the `p` stage description:** 31 records per window, the
  new MAX and outside-mixer codes (§5.1).
- **`CPU_USE_DSP_AUDIT.md`:** new items for:
  - the S101 device measurements (investigation §2);
  - the half-rate Alkaline chorus (§4.3);
  - the worst-case estimate (§7);
  - the S094 §21 "shared mixer" line (12,784) against the measured 28.9k.
- **`tools/dsp_test/DSP_TEST.md`:** the chorus module's laws (CH-AHALF) and
  the fourth break.
