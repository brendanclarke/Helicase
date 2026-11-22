# S101 — CPU investigation: Bank 076 Scene 5 at 99 %

**Date:** 2026-11-21. **Branch:** `dev-ph7-alkaline` (S100 P2 tree, uncommitted).
**Status:** investigation (the record). Later the same day, B1 and B3 were
built and B2 was not: see `S101_P1_IMPLEMENTATION.md`. That document supersedes
the B2 and B3 text below: B2 would lengthen saves (§3 there), owed scans
are not served back to back, and the endless pots stay on the main-loop
cadence (§2.2 there).

**Sources:**
- `SD_CARD_S100/.hcprms1`: the live state, generation 3,117, committed
  (`.hcprms2` is generation 3,116). Bank 76 `CollidSt`, active Scene 4 (the
  fifth). The Bank folder copy on the card (`Bank/076 CollidSt/04 ColxTime`)
  is older and differs: Basic `alk` types, `cam` 92. The figures below use
  the live record.
- `SD_CARD_S100/asavetrc.bin` (12,503,144 B, 1,562,893 records), decoded with
  `tools/diagnostic_tools/decode_devlogs.py`:
  - `p` cycle-probe windows (2,048 blocks each, about 1.5 s);
  - `f` filesystem pass-probe records.
  The last 12 boots all ran this Scene's slot set.
- The image on the card is a `DEV_MODE_LOGGING 1` build (`config.h:88`)
  with the temporary 2,048-record trace ring (`config.h:699`).

Labels used below:
- **Measured:** read from the device trace.
- **Code:** read from the tree, with a line reference.
- **Estimate:** derived, with the arithmetic shown. Needs a measurement
  before it is relied on.

---

## 0. Summary

1. **The slow buttons are a foreground starvation problem, made worse by
   the CPU load. The trace shows no audio underruns.** Measured:
   - the mixer averages 146.4k of 156,705 cycles per block (93.4 %) while
     the Scene plays;
   - the foreground gets about 6.6 % of the time;
   - every millisecond of foreground work therefore stretches one main-loop
     pass by about 15–20 ms.
2. **The panel is sampled once per main-loop pass, and a press needs three
   equal samples.** Code:
   - `timebase_serviceFrontPanel()` runs once per pass;
   - `debounce_bit()` (`din.c:82`) needs three consecutive equal samples;
   - the second owed scan is dropped (`frontpanel_service_due = 0`).

   Measured: the worst pass in each second is 19.5–24 ms while playing
   (8.7 ms stopped). So a press, and the gap before the next press, must each
   last about 60 ms to register.
3. **The long passes come from a 1 ms busy SD poll per pass**
   (`AUTOSAVE_DRAIN_POLL_BUDGET_US`, S099 P3, `filesystem.c:29141`).
   - It runs for the AutoSave drains, for every Flush Finish, and for **both
     diagnostic trace flushes**.
   - The comment above it says a trace flush gets it "only when already in
     flight when an edit arrives". The code has no such condition: every
     trace flush, twice a second, gets it.
   - That gives a diagnostic core-level time, against the S099 P3 trace
     rule §0.
4. **While playing, one AutoSave save takes 7–8.6 s** (measured, 26 drains;
   the longest 16–20 s).
   - That is about 390 passes, each of at least 1 ms.
   - It is outside rule §0's 5 s durability design, and the panel stays
     sluggish for that whole time after every edit.
   - The S099 P3 "3–4 s" figure holds only at lower load.
5. **DSP:** the average fits the S094 §21 cap (152,004 in the mixer).
   - The worst single block in every window is 163–165k (104–106 %).
   - The spike is about 17k above the average while playing, and about 5k
     with the transport stopped.
   - The probe records no per-slot or per-section maxima, so the spike
     cannot be attributed yet.
6. **The cheapest real gains are not in the voices.** Recommended order
   (§7): background fixes B1–B3 first (no DSP change, they fix the
   symptom), then probe extension S1 to find the spike, then the DSP items
   with the user's sound decisions.

| # | Change | Kind | Expected effect | Sound |
|---|---|---|---|---|
| B1 | No 1 ms busy poll for trace flushes (and their Flush Finish) unless a save is pending | foreground | Logging build: worst pass ~20 ms → a few ms, outside saves (estimate) | none |
| B2 | Fit the AutoSave/Flush Finish poll to the render: poll only while both audio slots are full (the `main_serviceBackgroundLoadIdle()` pattern) | foreground | Short passes during saves, and the save uses the idle window instead of delaying the render | none |
| B3 | Sample the panel at a fixed cadence: din exchange (plus encoder/pots tick) after each render commit, and serve owed scans instead of dropping them | foreground | No missed fast presses at any pass length; the menu action still waits for the pass | none |
| B4 | Remove `asavetrc.bin`/`pattrace.bin` before a test; consider a bounded trace file | card/DEV | The append-open seek walks every cluster of a 12.5 MB file (code) | none |
| S1 | Probe: per-slot and per-section maxima, plus `voiceControl_processPending()` + `seq_rampService()` time (outside the mixer today) | DEV | Attributes the 17k spike | none |
| D1 | Advanced Alkaline chorus wet at half rate, as the Colloid's already is | DSP | Measured chorus price +2.3–2.7k a block; half rate perhaps −1k (estimate) | changes (user) |
| D2 | Mixer output/send adds: per-block gain constants and incremental ramps | DSP | 10.8–11.3k section; perhaps −0.6–1.1k (estimate; check what `-Ofast` already folds) | LSB only (S1 class) |
| D3 | Control section (7.0k, 4.5 %): measure the six LFO dispatches and target restores; staggered control (S094 AS-7) | DSP | Unknown until measured | none to S1 |
| D4 | Bus compressor 4.6k (2.9 %) | user | On in this Scene (`cmp` 1); its cost is the user's to account (S074 exception) | choice |

---

## 1. The Scene (live, `.hcprms1` generation 3,117, Scene 4)

| Slot | Type | Name | CPU-relevant settings |
|---|---|---|---|
| 0 | `tkt` Tetrakt | `chipd3 1` | 4 layers (`lon` 1), drives 116/117/112/38, space time 55 / fbk 52, `wid` 127 |
| 1 | `drm` Drum | `fluxd2 2` | `typ` 0, `run` 2, chain 1 (WFd), filter type 5, LFO 1 → param 37 |
| 2 | `alx` **Advanced Alkaline** | `brezeld3` | `typ` 0 (wav), Saw, `drv` 127, LFO 127/127 → param 27; **chorus on** (`fx_type` 1, rate 62, depth 85, mix 33) |
| 3 | `clx` **Advanced Colloid** | `chips1 4` | `typ` 2 (pwm), wave 3 (**R-S chop**), filter type 5 (off; constant cost), LFO → pos/param 3; **chorus on** (`fx_type` 5, depth 127, mix 67) |
| 4 | `col` Colloid | `fluxc1 5` | `typ` 2 (pwm), wave 2, `spr` 119, `mot` 41, noise level 24, LFOs → pos/param 3 |
| 5 | `hat` HiHat | `fluxh1 6` | `typ` 4 (pwm), chain 1 (WFd), LFO sync 10 → param 32 on slots 5 and 4 |

- **Effect:** Delay (`dly`).
- **Bus compressor:** on (`cmp` 1, `cam` 31, `ctm` 38, `csc` 2).
- **FX sends:** 67, 4, 54, 0, 25, 16.
- **Advanced slots:** two (the maximum). Both chorus engines are running.

---

## 2. Measured cost (device probe, playing)

Medians of the boots at trace lines 1,434,935, 1,526,733 and 1,562,138. They
agree to within ±1 %. Cycles per 32-frame block; budget 156,705.

### 2.1 Slots

| Slot | Block-rate | Render | Total | Window max | S094 §21 share |
|---|---:|---:|---:|---:|---:|
| `tkt` | 2,890 | 15,210 | **18,100** | 20,121 | 19,620 Basic |
| `drm` | 710 | 13,170 | **13,880** | 13,960 | 19,620 Basic |
| `alx` | 3,080 | 15,210 | **18,290** | 18,378 | 21,803 Advanced |
| `clx` | 4,340 | 15,620 | **19,970** | 20,068 (22,158 at line 1,311,000) | 21,803 Advanced |
| `col` | 3,350 | 14,180 | **17,540** | 17,777 | 19,620 Basic |
| `hat` | 890 | 14,610 | **15,500** | 15,597 | 19,620 Basic |
| **Six slots** | | | **103,280** | | 122,086 |

- **Tetrakt:** its render moves between 15.2k and 17.3k by window (flash
  read variance, the S096 P1 finding). At its high windows it is at or over
  the Basic share.
- **The chorus price, from the same slot with the Basic type** (boots at
  lines 1,214,999 and 1,311,000; same Scene otherwise): `alk` 15,639–16,041
  → `alx` 18,290, i.e. **+2.3–2.7k a block** for the Alkaline chorus at full
  rate. The Colloid comparison is confounded by different settings, so no
  figure for the `clx` chorus.

### 2.2 Mixer sections

| Section | Cycles | % |
|---|---:|---:|
| Control (itp count, modNode, velocity, LFO dispatch) | 7,000 | 4.5 |
| `effects_service()` | 3,015 | 1.9 |
| Set-up (routing, clears, slot loop) | 2,504 | 1.6 |
| SRR decimators | 965 | 0.6 |
| Output / FX-send adds | 10,756–11,294 | 6.9–7.2 |
| Effect: Delay (bus convert, process, return) | 14,197 | 9.1 |
| Bus compressor | 4,611 | 2.9 |
| **Sections** | **≈ 43,050** | **27.5** |

### 2.3 Whole mixer

| | Playing | Stopped (line 1,479,204) |
|---|---:|---:|
| Block average | **146,375** (93.4 %) | 133,834 (85.4 %) |
| Worst block per window (median) | **163,874** (104.6 %) | 139,121 (88.8 %) |
| Worst block overall | 165,474 | 150,831 |
| Worst − average | **≈ 17.5k** | ≈ 5.3k |
| Effect section | 14,197 | 3,194 (idle: `sk_idleBefore()`) |

- **Against S094 §21:**
  - Average: 146.4k is under the 152,004 in-mixer cap (97 %).
  - Basic slots: 65.0k against 78.5k.
  - Advanced slots: 38.3k against 43.6k.
  - Effect: 14.2k against 16.6k.
  - Everything outside the slots and the Effect: about 28.9k. The S094
    "shared mixer" line (12,784) names only control, set-up and the
    compressor, so the send adds and SRR evidently sat inside the S094 slot
    shares or have grown since. **To reconcile** before the shares are used
    as gates again.
- **Only 10.3k cycles a block (6.6 %) are left outside the mixer.** That
  must cover `voiceControl_processPending()`, `seq_rampService()`, every ISR
  and the whole foreground. S094 allotted 4,701 to the foreground and
  background alone.
- **The spike is playback-only.** Stopped, the Effect idles, no triggers
  fire, and TIM3 does no step work: the worst block is 5.3k above the
  average. Playing, it is 17.5k above. The candidates, none yet measured:
  - trigger work inside the slots' async (note-on set-up, block-rate);
  - TIM3 and other ISRs above BASEPRI 6, which preempt the mixer: the DWT
    window counts their cycles;
  - the Delay's conditional per-block paths (`space_tapeBegin()`: the `str`
    `sqrtf`, the `hld` division and `sqrtf`, the delay-time recompute on a
    row or BPM change);
  - cold I- and D-cache after a 1 ms SD poll: present in both states, so
    not the playback difference.
- The probe has no per-slot or per-section maximum (`mixer.c:1226`), so
  which of these it is cannot be read from this trace (S1).

---

## 3. Why the buttons stop responding

### 3.1 The main loop gives the foreground what the render leaves

The render is not preemptive (`main.c:183`, `main.c:1539`):
- `audio_check_and_render()` runs between every foreground task and
  renders whenever a queue slot is free;
- a foreground task runs to completion;
- each DMA half (2.18 ms) the render takes about 93–95 % and the
  foreground gets the rest.

**Pass length ≈ foreground work in the pass ÷ the free fraction.**

| State | Mixer | Free (upper bound) | Worst pass per second (measured, median / p90) |
|---|---:|---:|---|
| Stopped | 85.4 % | 14.6 % | 8.7 / 8.7 ms |
| Playing, this Scene | 93.4 % | 6.6 % | 19.5–23.8 / 21.7–26.1 ms (max 56.6 ms) |

- **Stopped:** an 8.7 ms pass at about 12–14 % free means about 1.1–1.3 ms
  of foreground work in that pass.
- **Playing:** the same work at about 5–6 % free gives about 20 ms, which is
  the measured figure.
- **Which work:** the 1 ms busy poll (§3.3) plus about 0.1–0.3 ms of
  everything else. The pass that closes the worst gap of the second is a
  trace flush in 2,166 of 2,317 seconds, and an AutoSave drain in most of
  the rest.

**Consequence:** 1 % of CPU freed at this load shortens every pass by about
13–15 %. That is the coupling between "99 %" and "unresponsive".

### 3.2 Panel sampling depends on the pass

**Code:**
- TIM6 sets `frontpanel_service_due` every 2 ms (`timebase.c:140`, capped
  at 2).
- `timebase_serviceFrontPanel()` runs once per main-loop pass
  (`main.c:1555`). It does one `din_dout_exchange()` and then sets the
  count to 0 (`timebase.c:172`), so a second owed scan is dropped.
- `debounce_bit()` (`din.c:82`) reports a change only after three equal
  consecutive samples (`hist == 0x07` / `0x00`).

**Effect:**
- At 2 ms sampling a change needs 6 ms.
- At one sample per pass it needs three passes: about 60 ms at 20 ms
  passes, about 70 ms at the p90.
- So a tap shorter than about 40–60 ms, or a gap between taps shorter than
  that, is lost. Two quick presses merge into one.
- The event ring (`buttonHandler.c:78`, 64 entries) is not the limit; the
  sampling is.
- The encoder button and the pots are serviced in the same call
  (`encoder_tick()`, `endlessPots_tick()`).

### 3.3 The 1 ms busy poll

**Code:**
- `filesystem.c:29141–29154`: while the facade is BUSY with
  `AUTOSAVE_PARAMETER_DRAIN`, `AUTOSAVE_PATTERN_DRAIN`, `FLUSH_FINISH`,
  `AUTOSAVE_TRACE_FLUSH` or `PATTERN_TRACE_FLUSH`, `filesystem_tick()`
  spins `afatfs_poll()` for `AUTOSAVE_DRAIN_POLL_BUDGET_US` (1,000 µs) on
  every pass.
- The comment says the trace flush gets the rate only when "already in
  flight when an edit arrives". The condition does not test for a pending
  save, so every trace flush (`AUTOSAVE_TRACE_FLUSH_INTERVAL_MS` 500) and
  its closing Flush Finish spin for 1 ms on each of their passes.

**Measured** (four playing boots after line 1,434,935):

| `filesystem_tick()` ≥ 1 ms | Count | Median | Max |
|---|---:|---:|---:|
| `AUTOSAVE_PARAMETER_DRAIN` phase 13 | 13,454 | 1,024 µs | 1,192 µs |
| `FLUSH_FINISH` phase 0 | 12,651 | 1,009 µs | 1,054 µs |
| `AUTOSAVE_PARAMETER_DRAIN` phase 56 | 5,490 | 1,011 µs | 1,073 µs |
| `AUTOSAVE_PARAMETER_DRAIN` phase 24 | 656 | 1,025 µs | 1,464 µs |

- The trace flush's own ticks are not recorded (S090 P5 rule). Its passes
  appear only as the gap that the next pass closes: phase 1, "wait for the
  append open", in 94 % of the worst seconds.
- **This is diagnostic work holding core-level time.** It goes against the
  S099 P3 trace rule §0: diagnostics last, never delaying core work, and
  logging build = release for core functions.

### 3.4 An AutoSave save while playing

**Measured**, from the first slow drain tick to the drain's `ended` record:

| Boot (line) | Mixer | Drains | Duration |
|---|---:|---:|---|
| 1,434,935 | 92.4–93.4 % | 12 | 2.5, 5.0, 7.2–8.3 (×9), 15.9 s |
| 1,526,733 | 93.1–93.5 % | 14 shown | 2.5, 6.1, 7.3–8.6 (×11), 20.1 s |

- **Per drain:** about 390 passes of at least 1 ms (4,583 slow ticks over
  12 drains; 12,685 over 32).
- **Per save:** 390 passes × about 20 ms ≈ 8 s. So **every parameter edit
  is followed, about 1 s later, by about 8 s of 20 ms passes**, which is
  exactly when fast button work suffers.
- **Durability:** the saves themselves miss rule §0's 5 s deadline while
  this Scene plays. The S099 P3 figure (on the card about 3–4 s after an
  edit) was not taken at 93 % load.
- **In a release build** there is no trace flush, so the drains and their
  Flush Finish are the whole background story. The 8 s per save is the
  same, because the poll and the load are the same (estimate; §8).

### 3.5 Trace file growth (logging builds)

**Code:**
- `filesystem_autosaveTraceFlush_tick()` opens `asavetrc.bin` in append
  mode on every flush (`filesystem.c:6049`).
- AsyncFATFS seeks an append open to the end from the first cluster
  (`asyncfatfs.c:4890`, `afatfs_fseekInternal(file, file->logicalSize, …)`),
  one cluster at a time.
- So the open grows with the file. The card's file is 12.5 MB; the
  cluster size of the card was not checked (§8).
- Phase 1 (the open) closes 94 % of the worst passes.

---

## 4. Background options (no DSP change)

### B1 — Trace flushes off the busy poll

Apply the condition the comment already states.
- **What:** a trace flush, and the Flush Finish that closes it, get the
  1 ms poll only while a scalar or Pattern AutoSave is pending. Otherwise
  they keep the ordinary one poll per pass.
- **Effect:** outside saves, the logging build's worst pass falls from about
  20 ms to the release build's.
- **Cost:** a slower trace flush, which is allowed (diagnostic, last in
  queue).
- **Needs:** Flush Finish to know which operation it closes.
  `filesystem_finish()` chains Flush Finish after every operation, so the
  closing operation must be recorded when Flush Finish starts.

### B2 — The save poll fitted to the render

Replace "spin 1 ms per pass" with "spin while both audio slots are full,
up to a bound".
- **Precedent:** `main_serviceBackgroundLoadIdle()` (`main.c:249`):
  `filesystem_tickUncounted()` while `audioCodec_queueFreeSlots() == 0`,
  for at most `BG_LOAD_IDLE_FS_BUDGET_US` (200 µs).
- **Effect:**
  - the save's SD time comes from the idle window instead of from the
    render's slack;
  - the pass stays short because the poll stops when a slot frees;
  - each burst is bounded below 1 ms.
- **Open question (measure):** whether the idle window at 93 % load (about
  0.1–0.15 ms per DMA half) moves a save within 5 s.
  - If not, the save's speed is bounded by the CPU load, and only freeing
    CPU (§5) or a smaller write fixes it.
  - The section-CRC format already decided (`VOLATILE_MEMORY.md` "Capture
    budget") is that smaller write.
- **Do not raise `AUTOSAVE_PARAMETER_GETS_PER_WRITE`** (standing note).

### B3 — Panel sampling at a fixed cadence

- **What:**
  - split `timebase_serviceFrontPanel()`: keep `liveRec_service()`,
    `patSvc_tick()` and `ccSvc_tick()` where they are, and call the cheap
    part (`dout_latch()`, `din_dout_exchange()`, jack detect,
    `encoder_tick()`, `endlessPots_tick()`) from `audio_check_and_render()`
    after each `audioCodec_commitRenderBuffer()`, and in the main loop as
    today;
  - serve the owed count (loop while due) instead of zeroing it.
- **Effect:**
  - the scan runs at least once per DMA half (2.18 ms), whatever the pass
    length, except inside one long task (1 ms today, about 0.2 ms after
    B2);
  - the debounce works at about 6–7 ms again;
  - events wait in the 64-entry ring for `buttonHandler_processEvents()`;
  - no tap is lost; its action still waits for the pass.
- **Cost:** about 5 SPI bytes and 41 debounce steps per scan, at 500 Hz.
  Not measured; a few µs each.
- **Check first:**
  - the LED shadow is written by the foreground in the same context, so
    there is no new concurrency;
  - `din_dout_exchange()`'s SPI wait has a timeout (`din.c:129`);
  - the render path must not grow a call that LTO inlines into the mixer:
    keep the call outside the 32-frame loop, after the commit.

### B4 — Trace file hygiene

- **Now, no code:** delete `asavetrc.bin` and `pattrace.bin` from the card
  before a test session (decode them first). The append seek then starts
  short again.
- **Later:** a bounded trace file (fixed size, wrap or rotate). This is a
  format decision for the user; nothing here depends on it.

### B5 — Measure the release build

- **Why:** the logging build carries the probe (about 30 out-of-line calls a
  block, `mixer.c:1184`), 16 `p` records per window, the `f` records and
  the trace flush.
- **What:** one hardware check of the same Scene on a
  `DEV_MODE_LOGGING 0` image tells how much of the button symptom is the
  logging build's own.
  - Without the probe the widget is the only measure.
  - Note the widget figure with transport running, and the feel of fast
    presses before and after an edit.

---

## 5. DSP options (the CPU rule applies: no skipping, worst case only)

None of these changes a voice's sound class without the user's decision.
Each must be gated as `CPU_USE_DSP_AUDIT.md` §Binding Mandates and S093
require: per-block work included, `elf_hotloops.py` and the inlined-set
diff, then the device probe.

### S1 — Probe extension (DEV only, first)

- **Add:**
  - per-slot block-rate and render maxima and per-section maxima per window
    (12 + 7 values; the trace format has room for new flag codes);
  - the cycles spent in `voiceControl_processPending()` +
    `seq_rampService()` per block (outside `mixer_calcNextSampleBlock()`
    today, so unmeasured);
  - the worst block's per-section split.
- **RAM:** about 80–100 B, DEV only. Needs sign-off under the RAM rule.
- **Why:** the 17.5k playback spike decides whether "max CPU" work belongs
  in the voices (note-on), the Effect, or the ISRs. Without it any
  reduction is a guess.

### D1 — Advanced Alkaline chorus at half rate

- **Code:** `BbdChorus.c:551`: `st->half = !mono && BBD_COL_HALF_RATE`.
  The Colloid's (stereo) rings run at half rate; the Alkaline's (mono-in)
  run at full rate.
- **Measured:** the Alkaline chorus costs +2.3–2.7k a block (§2.1).
- **Estimate:** half rate removes roughly half of the per-sample wet work,
  about −1.0–1.3k a block per `alx`, i.e. 0.7–0.8 % CPU. Not measured; the
  chorus's per-block and clock work does not halve.
- **Sound:** changes (S2–S3: wet bandwidth, aliasing of the modulated read).
  User decision, with a listening A/B. The S099 P1 review chose full rate
  for the Alkaline deliberately, so re-open it only with the measured
  saving.

### D2 — Output/send adds (10.8–11.3k, the largest shared item)

- **Code:** `mixer.c:634` and the siblings. Per sample:
  - `lastGain + (float)i * inv_size * gain_delta` (one int→float convert,
    two multiplies) for the dry ramp, and the same again for the send;
  - then `× 256.0f`, and in the stereo send `× panL`, `× panR`.
- **Option:** per-block constants (`gain × 256`, `send × 256 × pan`) and
  incremental ramps (`g += dg`).
- **Estimate:** 2–4 instructions per sample per slot, about 400–800 a
  block, 0.6–1.1k cycles at the measured CPI.
- **First:** check the linked loops with `elf_hotloops.py`. `-Ofast` may
  already reassociate the constants, in which case the saving is the convert
  and one multiply only.
- **Sound:** an incremental float ramp is not bit-identical (last-bit
  differences in the ramp), so S1 class; the host comparison decides.

### D3 — Control section (7.0k, 4.5 %)

- **What runs:**
  - six velocity target restores and the LFO target restores
    (`modNode_resetTargets()`);
  - six velocity re-applies (`modNode_reassignVeloMod()`);
  - six `lfo_dispatchNextValue()` calls (`InstrumentManager.c:2386`).
- **This Scene:** every slot except the Tetrakt has an LFO on a live target
  (Colloid positions, the Alkaline's row 27, the HiHat's row 32 on two
  slots). A target write may run a descriptor setter with its own
  recomputation.
- **Next step:** `control_cost.py` / `elf_blockpath.py` on the dispatch
  path, per target type. Then decide: a cheaper setter path, or S094 AS-7
  (staggered control, open since S094 P2).
- **No figure until measured.**

### D4 — Bus compressor (4.6k, 2.9 %)

- It does no work only while `cmp` is off: the S074 exception; the user
  accounts for its cost.
- It is on in this Scene. Turning it off here is the user's call.
- No code change is proposed.

### Not recommended

- **Caching `effects_service()` (3.0k) on change:** it lowers the average,
  not the worst case (an FX Morph sweep or lane writes every block), which
  the CPU rule forbids counting as headroom.
- **Anything that stops a silent voice, a send at 0, or a slot's chorus at
  mix 0:** the binding DSP CPU policy.
- **Raising AutoSave capture budgets:** standing note.

### Per-instrument worst cases to keep in view

- **`clx` in this Scene:** 19,970 median, 22,158 at its worst window
  (line 1,311,000, Basic `alk` beside it). It is the dearest slot and
  exceeds the Advanced share (21,803) at that window.
  - It is the R-S chop PWM mode with the half-rate chorus.
  - Which Colloid mode is dearest on the device after S099 was not re-run
    in this trace.
- **`tkt`:** 18.1k median, 20.1k in its high windows (flash read variance),
  over the Basic share there. No cheap option found here.

---

## 6. What the 99 % widget shows

`audioCodec_getQueueFreePercent()` is queue pressure, not MCU use
(`MEMORY.md` Audio Pipeline). At a 93.4 % mixer average, plus the trigger
drain, the ISRs and the foreground, the queue is nearly never free, so the
widget reads 99–100 %. That agrees with the probe; it does not by itself
mean underruns. The trace has one underrun per boot, at start, in `idle`
phase 0.

---

## 7. Recommended order

1. **B4 now** (card hygiene) **and B5** (one release-build check) to
   separate logging cost from product cost.
2. **B1 + B3** together: small, foreground-only, and they address the
   reported symptom directly.
3. **B2,** with a device measurement of save duration while playing this
   Scene. Target under 5 s; report the figure either way.
4. **S1,** then attribute the 17.5k spike.
5. **D2 and D3** (measurement first), then **D1** with the user's sound
   decision. Each gated on the device probe of this Scene.

---

## 8. Not verified

- **Release build:** pass lengths and save durations. All figures here come
  from the logging build (B5).
- **The card's FAT cluster size,** and so the number of cluster steps in
  the append seek (§3.5).
- **Typical pass length:** the `f` probe records only the worst pass per
  second.
- **D1/D2 savings:** estimates; need a build and the probe.
- **The source of the playback spike** (S1).
- **Whether B2's idle window can complete a save within 5 s** at this
  Scene's load.

## 9. Statements this makes out of date (for you to act on)

- `filesystem.c:29136–29141`, the comment block, says a trace flush gets
  the poll budget only when already in flight as an edit arrives; the
  condition does not do that (§3.3).
- `config.h:560–567` (`AUTOSAVE_WRITER_INTERVAL_MS` comment), and
  S099 P3 §9–§10: "an edit is on the card about 3–4 s after it is made"
  holds only at low load. While this Scene plays, a save takes 7–8.6 s
  (§3.4).
- `MEMORY.md` "Load/Save never freezes" bullet: "While playing there is no
  free render time (CPU widget 99–100 %)". Measured: about 6.6 % free at
  93.4 %, and the main-loop pass then stretches by about 15–20× the
  foreground work (§3.1).
