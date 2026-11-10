# S093 P8 — Alkaline Wavetable Oscillator: Implementation Schedule

- **Session:** 093, phase 8. The literal code schedule for
  `S093_P8_WAVETABLE_REASSESSMENT.md` with its revision-2 decisions (§13
  there):
  - wavetables only on the Alkaline;
  - one stored 1,024-point 16-bit row per frame, shared by every instrument;
  - the install's frame buffer borrowed from the name cache;
  - 8-bit support removed throughout;
  - 24-bit and stereo sources kept through the conversion.
- **Revision 2 (2026-11-10):** amended for those decisions. The N2 layout
  (a copy beside the raw frames) of revision 1 is gone.
- **Status:** code implementation complete; host and linked firmware gates
  passed. Device checks in §13 were not run, and document updates P8-6 were
  omitted per the user's scope correction. Progress and measured deviations
  are recorded in §17.
- **Revision 3 (2026-11-10, after the hardware pass):** the Alkaline
  wavetable blends frames while holding an interpolation slot
  (`S093_P8_WAVETABLE_REASSESSMENT.md` §14, D12/D13). Implemented directly in
  the tree. The change list, the gates and the working notes are in **§18**,
  which overrides §6–§7 and §12.4 where they differ. **Status:** host and
  linked firmware gates passed; device checks (§18.5) pending.
- **Base:** the tree at `9c2a3de2` (P6 landed). Line numbers are that tree's.
- **Tags:**
  - **ADD**, **MODIFY**, **REMOVE**: a change, with its line anchor;
  - **NONE**: verified to need no change, with the reason.

  Comment blocks are part of each change.
- **Binding rules:**
  - the CPU ceiling (whole voice with per-block work, VDIV counted, ≤ 261.3);
  - constant per-sample cost (the longest path counts);
  - no other engine's per-sample loop may grow (S089; `elf_p4_check`);
  - RAM sign-off (the only RAM use is the approved name-cache loan, D8);
  - no new ITCM code;
  - the S087 Oscillator loops and the ZDF twins are not edited, **except the
    ten wavetable readers, for the 8-bit removal (user, D9)**;
  - no backward compatibility;
  - nothing needed goes in `build/`;
  - commits belong to the user.

## 17. Session implementation notes

- **2026-11-10, P8-0 complete:** read `MEMORY.md`, the P8 reassessment and
  this schedule; the user decisions in reassessment §13 govern where the
  earlier draft differs. Read the DSP CPU and oscillator mandates and checked
  the name-cache loan contract. The working tree was clean before edits. The
  schedule's only RAM use is the user-approved, temporary loan of the existing
  9,000 B `fs_list_cache_name`; the frame occupies at most 4,096 B. No RAM
  allocation is added. At P8-0, no source changes had been made.
- **2026-11-10, P8-1 source changes complete:** catalogue v3 now validates
  only 1,024-point, 16-bit wavetable rows; the installer converts each source
  frame into that row, counts source points across read chunks, and rejects
  8-bit WAV formats. The filesystem aligns and lends the existing name cache
  for the at-most 4,096 B source frame and returns it after the blocking
  install. This also fixes the old per-call frame counter that truncated the
  104-frame, 600-point `ak_hvox` table. Host and linked gates remain pending.
- **2026-11-10, P8-2 source changes complete:** removed the WTB8 bind kind and
  its six dispatch paths. All ten existing wavetable readers now use the
  fixed 1,024-point, 16-bit table read; `osc_wtbFrame()` advances by two bytes
  per point. The `OscInfo` layout and existing ITCM section assignments are
  unchanged. Host and linked loop gates remain pending.
- **2026-11-10, P8-3 source changes complete:** added Alkaline `typ` control
  with its own change-keyed wavetable bind, one-frame-per-block `pos` stepping,
  note-start frame selection and a silent zero-row fallback. Saw/Sqr and
  wtb now render through separate `noinline` wrappers, with the existing fused
  table loop shared out of line. No new runtime state was added; the silent
  row adds the scheduled 2,048 B of program flash. Host and linked gates
  remain pending.
- **2026-11-10, P8-4 source changes complete:** replaced Alkaline's selector
  `alk_wave` with image row `osc1_wave`, appended `osc1_type` at row 28, and
  connected the menu, dynamic wave domain and special writer to the Alkaline
  runtime. The typ list and Saw/Sqr names now have separate display tables.
  Host integration and linked gates remain pending.
- **2026-11-10, P8-5 host coverage changes complete:** added resampler and
  catalogue-v3 laws, 8-bit rejection coverage, Alkaline row-read/position/
  rebind/silence/type laws, fixed-read oscillator checks, and the two planned
  resampler and Alkaline row-step break builds. Updated the Alkaline init
  generator and regenerated its 29-row `init.alk`. Host, firmware and linked
  budget gates remain pending.
- **2026-11-10, P8-5 host gates passed:** `samples` reports 0 failures
  (harmonics within 0.0001 dB; non-harmonic bins −113.5 dBFS; band edge
  −0.0010 dB / −85.2 dBFS); catalogue and H=16 break builds were caught.
  `oscillator` and both FM break builds pass; `alkaline` reports 0 failures
  including AL-W (0 samples, phase or frame differences; empty-table peak 0)
  and catches both Alkaline breaks. `special_tags` reports 200 rows / 0
  mismatches and `init.alk` has 29 rows; manager and step-length tests pass
  with their break builds caught.
- **2026-11-10, firmware and linked gates passed:** `make clean && make all`
  links successfully with no new warning relative to the P6 build. The image
  uses 709,108 B of the 753,664 B flash budget (44,556 B headroom), 12,128 B
  ITCM, 4,392 B DTCM statics and 126,656 B FXBUF. The linked Alkaline fused
  loop is 214 instructions with 3 VDIV and one loop path; its longest block
  cost is 956 + 349 + 14 = 1,319, 9 VDIV, or at most 255.2 per whole voice.
  The ten wavetable reader loops measure 22, 33, 30, 41, 30, 42, 32, 45, 31
  and 42 instructions. Against P4, the first comparison found exactly the 31
  expected old reader loops missing and no other mismatch; mixer hot loops
  were 30 equal and 0 grown. Recorded the P8 oscillator and mixer snapshots
  in `tools/dsp_test/baselines/s093p8/`, made that baseline the check default,
  then `elf_p4_check` passed 77/77 exact loops and 32 mixer hot loops equal,
  0 grown.
- **2026-11-10, RAM sign-off:** built the unchanged P6 tree from `HEAD` in
  `/private/tmp` and compared its ELF symbols with P8. `.data` is 420 B and
  `.bss` is 413,280 B in both; no allocation was added. The only removed RAM
  symbol is the former one-byte stored-depth state. `out_buf` remains 128 B;
  its LTO-local symbol spelling differs. The approved frame-buffer loan is
  temporary use of the existing aligned name cache, not a new allocation.
- **2026-11-10, scope correction:** the user clarified “you don't edit logs
  and specs.” Restored the incidental edits to `MEMORY.md` and the three
  reference specifications. No project log or specification update was kept;
  the requested progress record in this implementation schedule is retained.
  The document-update phase P8-6 is therefore left out. No device-only checks
  in §13 were run in this session.
- **2026-11-10, comment audit:** expanded the public `.h` comments for the
  wavetable begin/write/finish APIs and the catalogue row accessor to match
  their detailed `.c` descriptions (format, buffer ownership, row layout,
  return behavior and callers).

---

## 0. Measured before writing this schedule

### 0.1 Method

Two copies of the tree were made in the session scratchpad and built with the
tree's `Makefile`, `make clean && make all`:
- **base:** unchanged, the control;
- **P8:** this schedule applied.

They were measured with the tree's tools:
- `elf_looppath.py --cond-heads --in-range` (loops);
- `control_cost.py --elf` (per block);
- `elf_p4_check` (every oscillator loop and the mixer's hot loops, against
  `baselines/s093p4`);
- `link_budget.py`;
- the host suites (§13).

The record, with every intermediate variant, is
`tools/dsp_test/baselines/s093p8/spike_linked.txt`.

The base reproduces the published P6 figures exactly:
- loop 214;
- per block 884 + 332 + 14 = 1,230;
- flash headroom 47,836 B;
- `elf_p4_check` 93/93 exact, mixer 0 grew.

### 0.2 Result: the budget holds

| | P6 (base) | P8 | Gate |
|---|---:|---:|---|
| Alkaline fused table loop (Saw/Sqr **and** wavetables) | 214 (inlined in the sync block) | **214** (`DiodeLadder_renderB2Osc()`, out of line, shared) | unchanged |
| Alkaline per block, longest path (`control_cost.py --elf`) | 1,230 (884 + 332 + 14), 9 VDIV | **1,319** (956 + 349 + 14), 9 VDIV | — |
| Alkaline whole voice, either mode | 252.4 | **≤ 255.2** | ≤ 261.3: **margin ≥ 6.1** |
| Wavetable readers of the other four instruments (per sample, §0.4) | 16-bit 24–44, 8-bit 26–48 | **22–45**: below the former 16-bit loop in nine readers, +1 in the sync blend; below the former worst case in all ten | no worst case grows |
| Other oscillator loops (`elf_p4_check`) | 93/93 exact | **62/62 exact**; the 31 loops of the ten wavetable readers are replaced (MISSING by design, §13) | 0 MISSING outside the readers |
| Mixer hot loops | 30 equal | **30 equal, 0 grew** | 0 grew |
| Flash `.text` | 689,856 B | 695,560 B (**+5,704 B**); headroom **44,556 B** | ≥ 16 KiB |
| ITCM | 14,552 B | **12,128 B** (−2,424 B: the 8-bit reader loops) | no new ITCM code |
| RAM `.bss` / `.data` / DTCM | — | **unchanged**; the name cache is borrowed during install (D8) | — |
| `sizeof(AlkalineVoice)` / `sizeof(OscInfo)` | 548 / 72 B | 548 / 72 B | unchanged |
| Build warnings (clean builds) | — | identical to the base | no new warning |

The Alkaline's longest per-block path is the `wtb` one with a bind, counted
as if a bind ran every block. That is the worst case: an LFO on `wav`
rebinds every block.

### 0.3 What the spike found and how the code answers it

| # | Finding (measured) | Consequence in this schedule |
|---|---|---|
| S1 | With the typed render beside the Saw/Sqr render in one function, the inlined table loop measured **216** (one extra float spill). It also measured 216 when the row was a pointer read from the oscillator, and 213 or 216 depending on unrelated one-line edits | §7.6: Saw/Sqr keep their own `noinline` function with the row computed as before (`alk_renderWav()`); wavetables have theirs (`alk_renderWtb()`). With two callers, `DiodeLadder_renderB2Osc()` stays out of line, and its loop is **214** whatever the wavetable code does |
| S2 | One-shot samples fit on their own (loop 215, per block 1,364), but their render call **grew two mixer hot loops of other engines by 1** (the same pair P7 hit). `wav` + `wtb` alone: 0 grew | Not in P8 (D7). `DiodeLadder.c` needs **no change** |
| S3 | Looped samples: loop 222–227 standalone; over the ceiling before any control work | Not offered |
| S4 | **An existing installer bug.** `sampleInstall_writeFrame()` adds one frame per *call*, but `filesystem_installFrames()` calls it once per 512-byte read. A 600-point 16-bit frame takes three calls; the card's `ak_hvox` (104 frames) is stored as **56** (reproduced on the host with the unmodified installer). Drum reaches only the first 56 frames today | §4.5: the installer counts points; the frame count is points / source frame length. Law rows-1 |
| S5 | The first resampler cost 155 instructions and 5 divisions per tap (about 68 ms per 600-point frame) | §4.4: the index steps instead of two modulos per tap; the kernel constants are computed once per frame. One division per tap remains |
| S6 | Resampler quality (host, law rows-4/5): harmonics within **0.0001 dB**, other bins below **−113.5 dBFS**; a 2,048-point frame's harmonic 700 at **−110.2 dBFS**; at 0.45 of the frame's Nyquist **−0.001 dB**, images **−85.2 dBFS** | Kernel H = 32. H = 16 / 24 measured worse at the band edge; the H = 16 build is a break proof |
| S7 | **Removing the 8-bit branch alone made the remaining 16-bit reader loops longer** by 1–4 instructions (GCC picks the wrapped next point by address instead of by index). Two other source forms of the wrap gave no better | §5.3: every stored frame is 1,024 points (checked in the catalogue, O3), so the readers use the **stock table read** (`ph >> 22`, mask `0x3FF`). It is bit-identical to the former read for 1,024-point frames (law W1) and the cheapest form measured (§0.4). **This is my choice, not one of your decisions;** without it the readers would cost 1–4 more per sample on 16-bit tables |

### 0.4 The wavetable readers of Drum, Snare, Cymbal and HiHat

Per-sample loop, `elf_looppath.py` on the linked images:

| Reader | P6: 16-bit / 8-bit copy | 8-bit branch removed only | **This schedule** (fixed 1,024-point read) |
|---|---|---:|---:|
| `osc_wtbPlainBlock` | 24 / 26 | 25 | **22** |
| `osc_wtbBlendBlock` | 34 / 38 | 37 | **33** |
| `osc_wtbFmBlock` | 32 / 34 | 33 | **30** |
| `osc_wtbFmBlendBlock` | 42 / 46 | 45 | **41** |
| `osc_wtbRingBlock` | 32 / 34 | 33 | **30** |
| `osc_wtbRingBlendBlock` | 43 / 47 | 46 | **42** |
| `osc_wtbSyncBlock` | 34 / 36 | 35 | **32** |
| `osc_wtbSyncBlendBlock` | 44 / 48 | 48 | **45** |
| `osc_wtbFmRampBlock` | 33 / 35 | 34 | **31** |
| `osc_wtbFmBlendRampBlock` | 43 / 47 | 46 | **42** |

- **The blend readers' second loop:** the five blend readers now also show
  their per-segment loop (at most two passes per block: the frame pair and its
  end sample) as a conditional loop: 61, 75, 85, 73, 74. Its path includes one
  pass through the per-sample loop, so it is not a per-sample figure.
- **Code size:** each reader is now one loop and 128–348 B smaller (§0.6).

### 0.5 Install time (estimates; measured on the device in P8-5)

- **Tap loop:** about 41 instructions plus one VDIV per tap.
- **Taps per row point:** 64 for frames up to 1,024 points; 2 × 32 × n / 1,024
  above that (128 at 2,048).
- **Trigonometry:** four `sinf`/`cosf` calls per row point.
- **Per frame:** about 20 ms for a 600-point frame. The card's `ak_hvox` adds
  about 2 s to an install; the largest table (128 × 2,048 points) about 5 s.
- **Where it runs:** inside the existing blocking install, with audio
  suspended.
- **Watchdog:** `DEV_LOGGING_IWDG` is 0. Its dev build runs the IWDG at the
  maximum period (about 32 s); `config.h` l. 196 asks for an audit of new long
  blocking work (§13).

### 0.6 Flash by symbol (base → P8)

Aliases (`__aeabi_dadd` = `__adddf3`, …) are listed once.

| Symbol | Δ B | What |
|---|---:|---|
| `Alkaline_calcVoiceSyncBlock` 3,000 → 16, `DiodeLadder_renderB2Osc` 0 → 2,932, `alk_renderWav` +104, `alk_renderWtb` +60 | +112 | the Alkaline table loop moves out of line, one copy |
| `alk_zeroRow` | +2,048 | the Alkaline's silent row (§7.1) |
| soft-double helpers (`__adddf3`, `__subdf3`, `__floatundidf`, `__floatdidf`, `__extendsfdf2`, `__floatsidf`, `__floatunsidf`) | +1,620 | used by newlib's `sinf`/`cosf` (install only) |
| `sinf`, `cosf`, `__sincosf_table`, `__inv_pio4` | +1,248 | the resampler's trigonometry (install only) |
| `filesystem_installFrames` (the installer, inlined) +910, `filesystem_installWavetableFolder` −116, `filesystem_parseWav` (now `.isra`) +8, `filesystem_installSampleFolder` +8, `sampleInstall_sourceSample` −50 | +760 | resampler, row writer, frame-count fix, 8-bit removal |
| ten wavetable readers (ITCM) | −2,420 | one loop each instead of two |
| `osc_userBind` −50, `osc_typedTriggerPhase` −20, `oscSection_renderOldCarrier` −4, `osc_renderTypedFmRamp` +2 | −72 | `OSC_BIND_WTB8` gone |
| `Alkaline_calcVoiceAsync` +210, `alk_wtbBind` +148, `Alkaline_triggerVoice` +112 | +470 | Alkaline wavetable control |
| `menu_handleLoadSaveMenu` +128, `mixer_calcNextSampleBlock` −44, menu/InstrumentManager/descriptors/`sampleMemory_refresh` (+182 − 32) | +234 | LTO layout and the rows/writers; the mixer's hot loops are unchanged (0 grew) |

---

## 1. Decisions applied in this schedule

| # | Decision | Source |
|---|---|---|
| D7 | **Wavetables only** on the Alkaline; `typ` list `wav`, `wtb` | user |
| D1′ | **One 1,024-point 16-bit row per frame, shared by every instrument**; no raw frames stored | user |
| D8 | **The install borrows `fs_list_cache_name` (9,000 B)** for the frame being resampled | user |
| D9 / D10 | **8-bit support off throughout**: the installer refuses 8-bit files (samples, loops, wavetables); the 8-bit wavetable store and read code is removed | user |
| D11 / O1 | **24-bit and stereo sources stay supported** through the conversion, for samples, loops and wavetables | user |
| O2 | The built-in 8-bit ROM sounds (crash wave, transients) are **out of scope** | user |
| O3 | The catalogue's wavetable `frame_len` and `bits` **stay, as checked marks** (1,024 and 16) | user |
| P8-D4 | `pos` on `wtb`: the frame, **one frame per block** toward the target; a non-legato note jumps to it | assessment |
| P8-D6 | The P7 document is kept as a record; marking it discarded is the user's (§14.3) | assessment |
| P8-D7 | **Catalogue v3:** magic `LXC3`, version 3; older cards read STALE and are reinstalled | this schedule (the format changes) |
| S7 | **The readers use the fixed 1,024-point read** (§0.3) | **this schedule (my choice, flagged)** |

**What you will get:**
- **Alkaline `typ`** on the OSC page, second screen: `wav` / `wtb`.
- **Alkaline `wav`:** `Saw`/`Sqr`, or under `wtb` the installed tables
  (`wNN`).
- **`pos` under `wtb`:** the frame (0 = first, 127 = last), stepping one
  frame per block; each non-legato note starts on its frame.
- **Pitch, slide, accent, filter and VCA:** as the Saw.
- **With no table installed, `wtb` is silent.**
- **Every instrument plays the same 1,024-point rows.** Drum's frames of 1,024
  points or fewer sound as before within 0.0001 dB, apart from the top 5 % of
  the band (−1.2 dB at 0.483 of the frame's Nyquist); longer frames lose
  harmonics 512 and up.
- **8-bit WAV files are no longer installed** (samples, loops or wavetables);
  24-bit and stereo files are, as today.
- **Sample flash:** 2,048 B per wavetable frame (`ak_hvox`: 212,992 B).
- **Reinstall:** after flashing, run `Load:[Samples]` once (v3).

---

## 2. Work order

| Phase | Content | Gate |
|---|---|---|
| **P8-1** | Catalogue, installer, walker, name-cache loan (§3, §4) | `make -C tools/dsp_test samples` (rows and install laws; both breaks caught) |
| **P8-2** | Oscillator readers (§5) | `make -C tools/dsp_test oscillator` (W1–W8, FR1/FR2; both breaks caught) |
| **P8-3** | Alkaline voice (§6, §7) | `make -C tools/dsp_test alkaline` (16 groups; AL-X unchanged; AL-W, AL-TY; both breaks caught) |
| **P8-4** | Rows, menu, writer (§8–§10) | `special_tags`; `instrument_manager`; `seq_steplen` |
| **P8-5** | Tools, `init.alk` (§12); linked gates and the device (§13) | §13 |
| **P8-6** | **None.** This schedule edits no `MEMORY.md` or `knowledge_files/` file (`MEMORY.md` "Document edits"). The statements P8 makes out of date are listed in §14 for the user to log | — |

P8-1 builds on its own; P8-2 and P8-3 need P8-1.

---

## 3. `Core/SampleRom/SampleMemory.h` / `.c` — catalogue v3

### 3.1 `SampleMemory.h` l. 109–110 — MODIFY (magic and version)

```c
/* S093 P8: v3 = every wavetable frame stored as one 1,024-point 16-bit row
 * (WAVETABLE_ROW_POINTS below), no 8-bit tables. A v2 card holds frames of
 * any length and 8-bit tables, so it reads as STALE and is reinstalled (no
 * migration, user rule). */
#define SAMPLE_CATALOGUE_MAGIC         ((uint32_t)0x3343584C) /* "LXC3" */
#define SAMPLE_CATALOGUE_VERSION       3u
```

- `sampleMemory_refresh()` compares both, so a v2 card falls to its STALE
  branch (law rows-7).
- **NONE:** the state enum keeps its name `SAMPLE_CATALOGUE_V2` (three uses):
  it names "the valid catalogue".

### 3.2 `SampleMemory.h` after l. 117 (`WAVETABLE_FRAME_MAX`) — ADD

`WAVETABLE_FRAME_MIN` / `WAVETABLE_FRAME_MAX` keep their meaning: they now
bound the *source* frame length.

```c
/*
 * The stored wavetable frame (S093 P8).
 *
 * What:       every installed wavetable frame is one int16 row of
 *             WAVETABLE_ROW_POINTS points: the source frame (16..2,048 points,
 *             16- or 24-bit, mono or stereo) converted to 16-bit mono and
 *             resampled to 1,024 points (band-limited, periodic) at install
 *             time. WAVETABLE_ROW_BYTES = 2,048 B per frame; an entry's
 *             frame_len is WAVETABLE_ROW_POINTS and its bits 16.
 * Why:        S093 P8 (user, D1'/D9): one format for every instrument. A row
 *             has exactly the layout of a stock sawTable row, so the Alkaline
 *             plays a frame through its existing fused table loop
 *             (DiodeLadder_renderB2Osc()); Drum, Snare, Cymbal and HiHat read
 *             it through their wavetable readers, which take any frame length
 *             and now read 16-bit data only.
 * Accessors:  SampleInstall.c (writes), sampleMemory_wavetableRows(),
 *             sampleMemory_wavetableRangeValid(), the Oscillator readers.
 */
#define WAVETABLE_ROW_POINTS           1024u
#define WAVETABLE_ROW_BYTES            (WAVETABLE_ROW_POINTS * 2u)
```

### 3.3 `SampleMemory.h` after l. 235 (`sampleMemory_wavetableEntry()`) — ADD

```c
/* S093 P8: the first 1,024-point row of a wavetable (frame f is at
 * + f x WAVETABLE_ROW_POINTS); NULL for a NULL entry. */
const int16_t *sampleMemory_wavetableRows(const wavetable_catalogue_entry_t *entry);
```

### 3.4 `SampleMemory.h` l. 89 — MODIFY (comment)

`Sample flash catalogue v2 (S087 stage C).` becomes:

```c
 * Sample flash catalogue v3 (S087 stage C; S093 P8: wavetables stored as
 * 1,024-point 16-bit rows).
```

### 3.5 `SampleMemory.c` l. 109–115 and l. 120–122 — MODIFY (`sampleMemory_wavetableRangeValid()`); ADD after its closing brace

```c
    /* S093 P8: a stored frame is always one 1,024-point 16-bit row (O3:
     * the two fields stay as checked format marks) */
    if (entry->frame_len != WAVETABLE_ROW_POINTS)
        return 0u;
    if (entry->frame_count == 0u || entry->frame_count > WAVETABLE_MAX_FRAMES)
        return 0u;
    if (entry->bits != 16u)
        return 0u;
    if ((entry->offset & 3u) != 0u)
        return 0u;
    if (entry->offset < SAMPLE_ROM_START_ADDRESS + 4u)
        return 0u;
    /* S093 P8: frame_count rows of 2,048 B (a whole number of words) */
    bytes = (uint64_t)entry->frame_count * WAVETABLE_ROW_BYTES;
    return (uint8_t)(entry->offset + bytes <= SAMPLE_CATALOGUE_START_ADDRESS);
}
```

(The two `offset` checks between them are unchanged.)

```c
/*
 * The first row of a wavetable (S093 P8; see the header).
 *
 * What:       the entry's offset as a pointer to its first 1,024-point row.
 * Why:        one place states where a table's rows are, for the Alkaline
 *             bind; Drum's bind reads the same address from the entry.
 * Inputs:     entry (from sampleMemory_wavetableEntry(), so validated).
 * Outputs:    the row pointer, or NULL.
 * Accessors:  AlkalineVoice.c alk_wtbBind().
 * Affiliates: WAVETABLE_ROW_POINTS; osc_userBind() (Oscillator.c).
 */
const int16_t *sampleMemory_wavetableRows(const wavetable_catalogue_entry_t *entry)
{
    if (!entry)
        return (const int16_t *)0;
    return (const int16_t *)SAMPLE_FLASH_PTR(entry->offset);
}
```

**O3:** `frame_len` and `bits` stay in the entry and are checked, so a
corrupt or older entry is refused and the other instruments' bind keeps
reading `frame_len` as it does.

---

## 4. The install: `SampleInstall.c` / `.h` and `filesystem.c`

### 4.1 `SampleInstall.c` header l. 4–5 and l. 13 — MODIFY (comment)

```c
 * What:       turns accepted WAV data into catalogue v3 items: block
 *             conversion (24->16 bit, stereo->mono; S093 P8: 8-bit sources are
 *             refused), the zero pad after each sample, the gain code from the
 *             peak, loop points from smpl, each wavetable frame resampled to
 *             one 1,024-point row, one entry per finished item, and the header
 *             with its magic written last.
```

and l. 13: `catalogue v2.` becomes `catalogue v3.`

### 4.2 `SampleInstall.c` l. 22 — ADD `#include <math.h>` before it; l. 35 — REMOVE `static uint8_t  item_stored_bits;`; l. 253 — REMOVE `item_stored_bits = 16u;`

- **`<math.h>`:** `sinf`, `cosf`, `floorf`, `fabsf` for the resampler. The
  file is `-O2`, not a DSP file; install time only.
- **`item_stored_bits`:** every stored item is now 16-bit, so the variable
  has no reader left (its last readers were the 8-bit branches removed
  below). 1 B of `.bss` less, absorbed by alignment.

### 4.3 `SampleInstall.c` l. 95 and l. 103 — MODIFY (`sampleInstall_acceptFormat()`: 8-bit refused, 16-bit stored); l. 185–188 — MODIFY (`sampleInstall_sourceSample()`)

```c
    /* S093 P8 (user, D10): 16- or 24-bit only; an 8-bit file is refused
     * like any other unaccepted format, for samples, loops and wavetables */
    if (bits != 16u && bits != 24u)
        return 0u;
```

```c
    /* S093 P8: everything is stored 16-bit (samples and wavetable rows) */
    (void)kind;
    out->stored_bits = 16u;
```

```c
/* One source point in int16 units: 16-bit as it is, 24-bit by its top 16
 * bits (S093 P8: the 8-bit branch is gone, acceptFormat() refuses 8-bit). */
static int32_t sampleInstall_sourceSample(const uint8_t *p, uint8_t bits)
{
    if (bits == 16u)
```

- **An 8-bit file is skipped as before for any refused format:**
  `filesystem_parseWav()` fails, so the walker skips it (samples) or the
  folder installs nothing (wavetables, §4.8).
- **24-bit → 16-bit and stereo → mono** (`sampleInstall_mono()`) are
  unchanged (D11).

### 4.4 `SampleInstall.c` l. 343–459 — REPLACE (the wavetable path: open, resample, append, finish)

Everything from the `Open a wavetable item` comment to the end of
`sampleInstall_finishWavetable()` is replaced by the blocks below, in this
order.

**Open:**

```c
/*
 * Open a wavetable item (S087 stage C; S087 P1; S093 P8 one stored format).
 *
 * What:       validates the source frame geometry (WAVETABLE_FRAME_MIN..MAX
 *             points, 1..WAVETABLE_MAX_FRAMES frames) and the accepted format,
 *             refuses when the item's rows (frame_count x 2,048 B) do not fit
 *             below the catalogue, then records the start address, the source
 *             frame length and format and the item's name.
 * Why:        one table is one item: every frame of the folder is written
 *             through sampleInstall_writeFrame(), resampled to a 1,024-point
 *             16-bit row, and the entry written by sampleInstall_finishWavetable()
 *             describes the rows. S093 P8: the stored depth is always 16 (the
 *             former `bits` argument is gone, D9/D10).
 * Inputs:     name, frame_len (source points per frame), frame_count, fmt.
 * Outputs:    the open item state; 1 when opened, 0 when refused.
 * Accessors:  filesystem_installWavetableHere().
 * Affiliates: sampleInstall_writeFrame(), sampleInstall_finishWavetable().
 * S087 P1: refused after a write fault (install_fault).
 */
uint8_t sampleInstall_beginWavetable(const char name[SAMPLE_DISPLAY_NAME_LEN],
                                     uint16_t frame_len, uint8_t frame_count,
                                     const sample_install_fmt_t *fmt)
{
    if (!fmt || item_open || install_fault || wavetable_items >= WAVETABLE_MAX_COUNT)
        return 0u;
    if (frame_len < WAVETABLE_FRAME_MIN || frame_len > WAVETABLE_FRAME_MAX)
        return 0u;
    if (frame_count == 0u || frame_count > WAVETABLE_MAX_FRAMES)
        return 0u;
    if (fmt->bits != 16u && fmt->bits != 24u)
        return 0u;
    /* S093 P8: the item is its rows only, 2,048 B per frame */
    if ((uint64_t)frame_count * WAVETABLE_ROW_BYTES > sampleInstall_bytesFree())
        return 0u;
    item_start = write_addr;
    frames_total = frame_count;
    frames_written = 0u;
    item_frame_len = frame_len;
    item_channels = fmt->channels;
    item_source_bits = fmt->bits;
    item_peak = 0;
    out_len = 0u;
    item_open = 2u;
    memcpy(item_name, name, SAMPLE_DISPLAY_NAME_LEN);
    return 1u;
}
```

**The resampler** (the kernel, one row point, one row):

```c
/*
 * The resampling kernel of one table (S093 P8).
 *
 * What:       the constants of the periodic windowed sinc that turns one
 *             n-point frame into a 1,024-point row (sampleInstall_rowPoint()):
 *             r      input points per row point, n / 1,024;
 *             s      the kernel stretch, max(1, r): 1 when the frame is no
 *                    longer than the row (the cutoff is the frame's Nyquist),
 *                    r when it is longer (the cutoff is the row's Nyquist,
 *                    harmonic 512);
 *             inv_s  1 / s;
 *             half   the support each side, s x H input points;
 *             taps   2 x half, the taps per row point;
 *             ca, sa cos/sin of the sinc's step (pi / s per tap);
 *             cw, sw cos/sin of the window's step (pi / (s H) per tap).
 * Why:        they depend only on n, so they are computed once per frame
 *             (sampleInstall_writeRow()), not per row point: four sinf/cosf
 *             calls fewer per point. A stack local: no static RAM.
 * Accessors:  sampleInstall_rowKernel() (fills), sampleInstall_rowPoint().
 */
typedef struct {
    float r, s, inv_s, half;
    int32_t taps;
    float ca, sa, cw, sw;
} sample_install_row_kernel_t;

#define SAMPLE_INSTALL_ROW_HALF 32   /* H: zero crossings each side (64 taps up, 128 at n = 2,048) */
#define SAMPLE_INSTALL_PI       3.14159265358979f

/* S093 P8: fill the kernel constants for an n-point frame (see the type) */
static void sampleInstall_rowKernel(sample_install_row_kernel_t *kr, uint32_t n)
{
    kr->r = (float)n * (1.0f / (float)WAVETABLE_ROW_POINTS);
    kr->s = (n > WAVETABLE_ROW_POINTS) ? kr->r : 1.0f;
    kr->inv_s = 1.0f / kr->s;
    kr->half = kr->s * (float)SAMPLE_INSTALL_ROW_HALF;
    kr->taps = (int32_t)(2.0f * kr->half);
    kr->ca = cosf(SAMPLE_INSTALL_PI * kr->inv_s);
    kr->sa = sinf(SAMPLE_INSTALL_PI * kr->inv_s);
    kr->cw = cosf(SAMPLE_INSTALL_PI * kr->inv_s / (float)SAMPLE_INSTALL_ROW_HALF);
    kr->sw = sinf(SAMPLE_INSTALL_PI * kr->inv_s / (float)SAMPLE_INSTALL_ROW_HALF);
}

/*
 * One point of a frame's 1,024-point row (S093 P8).
 *
 * What:       the frame x[0..n-1] (one period, int16, in the borrowed frame
 *             buffer) evaluated at input position t = k x n / 1,024 by a
 *             periodic windowed sinc:
 *               y(t) = sum_m x[m mod n] h((t - m) / s) / sum_m h((t - m) / s)
 *               h(u) = sinc(u) w(u / H),  w(v) = 0.42 + 0.5 cos(pi v)
 *                                                + 0.08 cos(2 pi v) (Blackman)
 *             over |t - m| < s H (the kernel's constants: kr).
 * Why:        a frame is one cycle of a periodic wave, so the kernel wraps
 *             (m mod n); dividing by the summed weights makes the DC gain
 *             exactly 1 at every phase. Per tap the sinc and window terms
 *             turn by two fixed rotations instead of sinf/cosf calls; one
 *             VDIV per tap (the sinc's 1/(pi u)).
 *             Measured (host, S093 P8 §0): flat within 0.001 dB up to 0.45 of
 *             the frame's Nyquist with images below -85 dBFS; -1.2 dB at
 *             0.483 and -5.3 dB at 0.498 (the kernel's transition band); a
 *             2,048-point frame's harmonics past 512 below -110 dBFS.
 * Inputs:     x (the frame's first point), n (frame length, 16..2,048), k (row
 *             point 0..1,023), kr.
 * Outputs:    the point, rounded and saturated to int16 (a sharp edge's
 *             Gibbs overshoot can pass full scale).
 * Accessors:  sampleInstall_writeRow().
 * Affiliates: SAMPLE_INSTALL_ROW_HALF; tools/dsp_test modules/samples.c rows
 *             laws 4-5.
 */
static int16_t sampleInstall_rowPoint(const int16_t *x, uint32_t n, uint32_t k,
                                      const sample_install_row_kernel_t *kr)
{
    /* t: the row point's position in input points */
    const float t = (float)k * kr->r;
    const int32_t m0 = (int32_t)floorf(t - kr->half) + 1;     /* first tap with |t - m| < half */
    /* u = (t - m) / s, the sinc argument, at tap m0; it falls by 1/s per tap,
     * so sin(pi u) and cos(pi u / H) turn backwards by the kernel's steps */
    const float u0 = (t - (float)m0) * kr->inv_s;
    float sinA = sinf(SAMPLE_INSTALL_PI * u0), cosA = cosf(SAMPLE_INSTALL_PI * u0);
    float cosW = cosf(SAMPLE_INSTALL_PI * u0 / (float)SAMPLE_INSTALL_ROW_HALF);
    float sinW = sinf(SAMPLE_INSTALL_PI * u0 / (float)SAMPLE_INSTALL_ROW_HALF);
    float acc = 0.0f, wsum = 0.0f;
    float u = u0;
    /* the frame index of tap m0, wrapped into 0..n-1 once (m0 may be
     * negative or past the end); each tap then steps it by one and wraps at
     * n, so a short frame (n < 2 x half) is summed over several periods */
    int32_t mi = m0 % (int32_t)n;
    int32_t i;

    if (mi < 0)
        mi += (int32_t)n;
    for (i = 0; i < kr->taps; i++) {
        /* sinc(u) = sin(pi u) / (pi u), 1 at u = 0 */
        const float sinc = (fabsf(u) < 1e-6f) ? 1.0f
                         : sinA / (SAMPLE_INSTALL_PI * u);
        /* Blackman at v = u / H: cos(2 pi v) = 2 cos^2(pi v) - 1 */
        const float w = 0.42f + 0.5f * cosW + 0.08f * (2.0f * cosW * cosW - 1.0f);
        /* the taps lie inside |u| < H; the test gives a tap that rounding
         * puts on the edge zero weight */
        const float h = (fabsf(u) < (float)SAMPLE_INSTALL_ROW_HALF) ? sinc * w : 0.0f;
        float r;

        acc += (float)x[mi] * h;
        wsum += h;
        /* step u by -1/s: rotate both angles backwards */
        r = sinA * kr->ca - cosA * kr->sa;  cosA = cosA * kr->ca + sinA * kr->sa;  sinA = r;
        r = sinW * kr->cw - cosW * kr->sw;  cosW = cosW * kr->cw + sinW * kr->sw;  sinW = r;
        u -= kr->inv_s;
        if (++mi == (int32_t)n)
            mi = 0;
    }
    {
        /* normalised by the summed weights: DC gain exactly 1 at every phase */
        const float y = (wsum != 0.0f) ? acc / wsum : 0.0f;
        const float yr = y + ((y >= 0.0f) ? 0.5f : -0.5f);   /* round half away from 0 */

        return (int16_t)((yr > 32767.0f) ? 32767 : ((yr < -32768.0f) ? -32768 : (int32_t)yr));
    }
}

/*
 * Write one finished frame as its 1,024-point row (S093 P8).
 *
 * What:       a frame of exactly 1,024 points is written as it is; any other
 *             length is resampled point by point (sampleInstall_rowPoint()).
 *             Appended through the 128-byte staging buffer as little-endian
 *             int16, 2,048 B.
 * Why:        D1': the row is the only stored form of a frame.
 * Inputs:     x (the frame, item_frame_len int16 points in the borrowed
 *             buffer).
 * Outputs:    2,048 B appended; 0, or the write error.
 * Accessors:  sampleInstall_writeFrame() (when a frame's last point arrives).
 * Affiliates: WAVETABLE_ROW_POINTS; sampleMemory_wavetableRows() (reads it).
 */
static int sampleInstall_writeRow(const int16_t *x)
{
    const uint32_t n = item_frame_len;
    sample_install_row_kernel_t kr;
    uint32_t k;

    if (n != WAVETABLE_ROW_POINTS)
        sampleInstall_rowKernel(&kr, n);
    for (k = 0u; k < WAVETABLE_ROW_POINTS; k++) {
        const int16_t v = (n == WAVETABLE_ROW_POINTS) ? x[k]   /* already a row */
                        : sampleInstall_rowPoint(x, n, k, &kr);
        uint8_t out[2];

        out[0] = (uint8_t)((uint16_t)v & 0xffu);
        out[1] = (uint8_t)((uint16_t)v >> 8);
        if (sampleInstall_append(out, 2u) != 0)
            return -1;
    }
    return 0;
}
```

**Notes on the resampler:**
- **The rotations:** each tap multiplies `(cos, sin)` by the fixed step, so
  float error grows over at most 128 taps and restarts at every point (exact
  `sinf`/`cosf` at tap `m0`). Law rows-4/5 measures the result.
- **`h = 0` at the edge:** `taps = 2 x half` and `m0 = floor(t - half) + 1`,
  so `|u| < H` holds for every tap but, by rounding, the last one.
- **A 1,024-point source is stored as it is** (law rows-3), as is any
  24-bit or stereo source after conversion (install law 3).

**Append points (and the frame-count fix):**

```c
/*
 * Append wavetable points (S087 stage C; S093 P8: points into the frame
 * buffer, a row per finished frame).
 *
 * What:       converts `points` source points (16/24-bit, mono or stereo as
 *             opened) to 16-bit mono and stores them in frame_buf at the open
 *             frame's next positions; when a frame's last point arrives, the
 *             frame is written as its 1,024-point row (sampleInstall_writeRow()).
 *             frames_written counts the table's POINTS; a call that would pass
 *             the table's last point (frames_total x item_frame_len) is
 *             refused.
 * Why:        S093 P8 fix: filesystem_installFrames() calls this once per
 *             512-byte read, i.e. several times per frame for a frame over 512
 *             bytes (a 600-point 16-bit AKWF frame takes three calls). The
 *             former per-call frames_written++ stored 3 x 104 = 312 frames,
 *             truncated to 56 in the uint8_t frame_count. Counting points makes
 *             the count independent of how the caller chunks a frame; the
 *             position in the frame is frames_written mod item_frame_len.
 *             D1'/D8: the frame is assembled in the borrowed name cache, so
 *             the raw frame never reaches flash.
 * Inputs:     src (interleaved source points), points (in src), frame_buf (at
 *             least item_frame_len int16, 2-byte aligned: the caller's
 *             borrowed fs_list_cache_name).
 * Outputs:    frame_buf; a row appended per finished frame; frames_written;
 *             1, or 0 on a refusal or a write fault.
 * Accessors:  filesystem_installFrames() (SAMPLE_INSTALL_KIND_WAVETABLE); the
 *             host benches.
 * Affiliates: sampleInstall_finishWavetable(); sampleInstall_mono().
 */
uint8_t sampleInstall_writeFrame(const uint8_t *src, uint16_t points,
                                 int16_t *frame_buf)
{
    if (item_open != 2u || !src || !frame_buf)
        return 0u;
    /* points still owed to the table: never write past the opened geometry,
     * so the fit check in sampleInstall_beginWavetable() stays true */
    if ((uint64_t)frames_written + points >
        (uint64_t)frames_total * item_frame_len)
        return 0u;
    while (points-- != 0u) {
        uint8_t bytes;
        /* the point's place in its frame */
        const uint32_t at = frames_written % item_frame_len;

        frame_buf[at] = (int16_t)sampleInstall_mono(src, item_channels,
                                                    item_source_bits, &bytes);
        src += bytes;
        frames_written++;
        /* the frame's last point: write its row */
        if (at + 1u == item_frame_len && sampleInstall_writeRow(frame_buf) != 0)
            return 0u;
    }
    return 1u;
}
```

**Finish:**

```c
/*
 * Finish the open wavetable (S087 P1; S087 stage C; S093 P8 rows).
 *
 * What:       flushes the last row bytes, then programs the 20 B wavetable
 *             entry once, in the next free wavetable slot: offset = the first
 *             row, frame_len = WAVETABLE_ROW_POINTS, frame_count = the whole
 *             frames written (points / source frame length), bits = 16. A
 *             failed write closes the item without an entry; a failed entry
 *             write sets install_fault; the slot index is not reused.
 * Why:        catalogue entries follow the program-once rule like the item
 *             data; after a fault no further item may claim the same slot.
 *             S093 P8 (O3): frame_len and bits stay in the entry as checked
 *             format marks (sampleMemory_wavetableRangeValid()).
 * Inputs:     item_start, item_frame_len, frames_written (points), item_name,
 *             wavetable_items.
 * Outputs:    the entry in flash; wavetable_items + 1 on success; 1/0.
 * Accessors:  filesystem_installWavetableHere().
 * Affiliates: SampleMemory.h wavetable entry layout; sampleInstall_program();
 *             sampleMemory_wavetableRows().
 */
uint8_t sampleInstall_finishWavetable(void)
{
    wavetable_catalogue_entry_t entry;
    uint32_t entry_addr;
    uint32_t frames;

    if (item_open != 2u)
        return 0u;
    /* whole frames written (the walker writes exactly frame_len points per
     * frame file; a partial last frame wrote no row and is not counted);
     * item_frame_len is >= WAVETABLE_FRAME_MIN while a table is open */
    frames = frames_written / item_frame_len;
    if (sampleInstall_flush() != 0 || frames == 0u) {
        item_open = 0u;
        return 0u;
    }
    memset(&entry, 0, sizeof(entry));
    entry.offset = item_start;
    entry.frame_len = (uint16_t)WAVETABLE_ROW_POINTS;
    entry.frame_count = (uint8_t)frames;
    entry.bits = 16u;
    memcpy(entry.name, item_name, SAMPLE_DISPLAY_NAME_LEN);
    entry_addr = SAMPLE_CATALOGUE_START_ADDRESS +
                 sizeof(sample_catalogue_header_t) +
                 SAMPLE_MAX_COUNT * SAMPLE_ENTRY_BYTES +
                 (uint32_t)wavetable_items * WAVETABLE_ENTRY_BYTES;
    item_open = 0u;
    if (sampleInstall_program(&entry_addr, (const uint8_t *)&entry,
                              sizeof(entry)) != 0)
        return 0u;
    wavetable_items++;
    return 1u;
}
```

**Order:** each finished frame's row goes into the staging buffer as soon as
its last point arrives; the entry is programmed last. An interrupted install
leaves no entry, so the item is invisible, as before (program-once rule).

### 4.5 `SampleInstall.h` — MODIFY (comments, the two prototypes)

**l. 4–6:**

```c
 * What:       format acceptance and block conversion for WAV data into
 *             catalogue v3 items: PCM 1-2 channels, 16/24 bit (S093 P8: 8-bit
 *             refused), any rate; samples store 16-bit mono, wavetable frames
 *             store one 1,024-point 16-bit row each.
```

**l. 33:**

```c
    uint8_t  stored_bits;    /* 16 (S093 P8: samples and wavetable rows) */
```

**l. 40–41:**

```c
 *             1-2 channels and 16/24-bit samples (S093 P8: 8-bit is refused,
 *             user D10); `stored_bits` is 16 for every kind.
```

**l. 63–67:**

```c
/* S093 P8: a table's frames are assembled in frame_buf (at least frame_len
 * int16, 2-byte aligned: the install borrows fs_list_cache_name) and each
 * finished frame is written as one 1,024-point row; the stored depth is
 * always 16, so beginWavetable() takes no depth */
uint8_t sampleInstall_beginWavetable(const char name[SAMPLE_DISPLAY_NAME_LEN],
                                     uint16_t frame_len, uint8_t frame_count,
                                     const sample_install_fmt_t *fmt);
uint8_t sampleInstall_writeFrame(const uint8_t *src, uint16_t points,
                                 int16_t *frame_buf);
```

### 4.6 `filesystem.c` l. 1066–1067 — MODIFY (`fs_list_cache_name`: aligned); after l. 1978 — ADD (the size assert)

```c
/* S093 P8: 4-byte aligned, because the sample install lends it out as an
 * int16 wavetable frame buffer (filesystem_installUserContentBlocking()) */
static char fs_list_cache_name[FS_LIBRARY_NAME_CACHE_MAX]
                              [STORAGE_KIT_DISPLAY_NAME_LEN + 1u]
    __attribute__((aligned(4)));
```

```c
/* S093 P8: the install's wavetable frame buffer fits the name cache */
_Static_assert(sizeof(fs_list_cache_name) >= WAVETABLE_FRAME_MAX * sizeof(int16_t),
               "a wavetable source frame must fit the name cache");
```

- **The alignment:** the array is `char[1000][9]`, so the language promises
  only 1-byte alignment. It sits at `0x20024dfc` in both images, and the
  attribute makes that a guarantee. Its size is unchanged (9,000 B).
- **The assert:** 9,000 B ≥ 2,048 × 2 B.

### 4.7 `filesystem.c` l. 25397 — MODIFY (`filesystem_installFrames()`: the wavetable call)

```c
        } else if (!sampleInstall_writeFrame(sample_io_buf, (uint16_t)n,
                                             (int16_t *)(void *)fs_list_cache_name)) {
            /* S093 P8 (D8): the frame is assembled in the name cache, which
             * filesystem_installUserContentBlocking() holds for the install */
            return 0u;
```

### 4.8 `filesystem.c` l. 25459, l. 25477, l. 25506, l. 25538 — MODIFY / REMOVE (the wavetable walker: no depth choice)

- **l. 25459 (comment):** becomes

```c
 * What:       pass 1 counts the folder's .wav files and fixes the frame length
 *             and source format (S093 P8: 16- or 24-bit; an 8-bit file fails
 *             filesystem_parseWav() and is not counted, so an 8-bit folder
 *             installs nothing and a mixed one fails pass 2);
```

- **l. 25477 and l. 25506:** REMOVE `uint8_t bits = 8u;` and
  `bits = (uint8_t)(wav.fmt.bits == 8u ? 8u : 16u);`.
- **l. 25538:** `sampleInstall_beginWavetable(name, frame_len, count, bits,
  &first.fmt)` becomes `sampleInstall_beginWavetable(name, frame_len, count,
  &first.fmt)`.

**Folder rule:**
- pass 1 still requires every counted frame to share the first one's length,
  bits and channels;
- an 8-bit file fails `filesystem_parseWav()`, so it is not counted and pass 2
  stops on it;
- a 24-bit or stereo folder installs (O1).

### 4.9 `filesystem.c` before l. 25708 and after l. 25726 — ADD (`filesystem_installUserContentBlocking()`: take and return the name cache)

```c
    /*
     * S093 P8 (user, D8): hold the 9,000 B name cache for the install.
     *
     * What:       the array is lent exactly as to copy/clear (cleared, tagged
     *             FS_NAME_CACHE_COPYCLEAR, disposal ignored); the wavetable
     *             walker assembles each source frame in it (up to 2,048 int16 =
     *             4,096 B, sampleInstall_writeFrame()); it is returned and
     *             cleared at the end, so Load/Save reloads its index on the
     *             next entry, as after copy/clear.
     * Why:        D1': the raw frame is not stored, so the resampler reads it
     *             from RAM; no new RAM (D8). The public borrow requires an IDLE
     *             facade; this pass only needs it not BUSY (checked above), so
     *             it takes the loan here. A loan already held (copy/clear) is
     *             refused, as a busy facade.
     * Affiliates: filesystem_borrowNameCacheScratch() (the same steps),
     *             filesystem_returnNameCacheScratch().
     */
    if (fs_name_cache_borrowed)
        return 0u;
    filesystem_clearNameCacheStorage();
    fs_list_cache_kind = FS_NAME_CACHE_COPYCLEAR;
    fs_name_cache_borrowed = 1u;
    status = FS_STATUS_BUSY;
    current_op = FS_INTERNAL_OP_NONE;

    sampleInstall_begin();
```

```c
    if (!sampleInstall_commit())
        ok = 0u;
    /* S093 P8: return the name cache (cleared, domain NONE) */
    filesystem_returnNameCacheScratch();
```

**Why the install takes the loan itself rather than calling
`filesystem_borrowNameCacheScratch()`:**
- the public borrow refuses unless the facade is IDLE;
- the install only requires "not BUSY" (l. 25704), and the Load menu starts it
  after waiting out BUSY (`menu.c` l. 1083–1090). A DONE or ERROR status
  would otherwise refuse the install where today it runs.

The steps are the borrow's own (clear, tag, flag), with the same refusal
while copy/clear holds the loan.

**What the loan blocks:**
- while lent, the facade refuses every operation except the copy/clear name
  write (l. 29287);
- the install uses the blocking `filesystem_block*()` helpers, which do not
  pass that gate;
- the return clears the array, so Load/Save reloads its index on the next
  entry, as after copy/clear.

**NONE:**
- `filesystem_borrowNameCacheScratch()`, `filesystem_returnNameCacheScratch()`
  (l. 1989–2008): used as they are;
- the S075 contract comment in `filesystem.h` (l. 1290–1299) gains one line:
  "S093 P8: the sample install holds the same loan for its run
  (filesystem_installUserContentBlocking())".

---

## 5. `Core/DSPAudio/Oscillator.c` / `.h` — 16-bit, 1,024-point wavetable readers

### 5.1 `Oscillator.h` l. 189 — MODIFY (comment); l. 204 — REMOVE

```c
 *             fraction); OSC_BIND_NONE/SMP/SLP/WTB16 (S093 P8: WTB8 is
 *             gone with 8-bit tables; 3 stays unused); OSC_POS_SAMPLE_SPEED_CAP
```

REMOVE `#define OSC_BIND_WTB8             3u`.

`OSC_BIND_WTB16` keeps its value 4 and its name, so no other bind value or
caller changes.

### 5.2 `Oscillator.c` — the bind and the dispatch (`OSC_BIND_WTB8` removed)

| Line(s) | Tag | Change |
|---|---|---|
| 1532–1533 (`osc_userBind()` comment) | MODIFY | "A wavetable's kind is resolved to WTB8 or WTB16 from its entry BEFORE the comparison" becomes the text below |
| 1596–1607 | MODIFY | the depth choice goes (block below) |
| 1616–1617 | MODIFY | `(kind == OSC_BIND_WTB8 \|\| kind == OSC_BIND_WTB16)` becomes `(kind == OSC_BIND_WTB16)` |
| 1647–1648 | MODIFY | `(kind == OSC_BIND_WTB8 \|\| kind == OSC_BIND_WTB16 \|\| osc->oscType == OSC_TYPE_WTB)` becomes `(kind == OSC_BIND_WTB16 \|\| osc->oscType == OSC_TYPE_WTB)` |
| 2399, 2422, 2445, 2467, 2583, 3692 | REMOVE | `case OSC_BIND_WTB8:` (the four typed render dispatchers, the itp old-carrier render, the S089 P3 FM-ramp dispatcher); the `case OSC_BIND_WTB16:` below each stays |
| 2490–2491 (`osc_renderUserList()`) | MODIFY | `osc->sampleCacheLooped == OSC_BIND_WTB8 \|\| osc->sampleCacheLooped == OSC_BIND_WTB16` becomes `osc->sampleCacheLooped == OSC_BIND_WTB16` |

```c
 *             (stock, s, l, w). A wavetable binds as WTB16 only when its entry
 *             exists (S093 P8: every table is 16-bit), checked BEFORE the
 *             comparison with the stored bind. When
```

```c
	/* F9: a wavetable binds only with an entry (S093 P8: 16-bit only, so
	 * the entry no longer chooses a depth). */
	if (kind == OSC_BIND_WTB16) {
		const wavetable_catalogue_entry_t *w =
			sampleMemory_wavetableEntry(index);

		if (!w) {
			kind = OSC_BIND_NONE;
			index = 0u;
		}
	}
```

### 5.3 `Oscillator.c` l. 2687–2717 — MODIFY (`osc_wtbRead()`: the fixed 1,024-point read)

```c
/*
 * Wavetable frame helpers (S087 stage E; S093 P8: one stored format).
 *
 * What:       one frame's base pointer and one interpolated point at a cycle
 *             phase (2^32 per frame).
 * Why:        the one-frame and blend copies share the exact read. S093 P8
 *             (user, D1'/D9): every stored frame is one 1,024-point 16-bit row
 *             (WAVETABLE_ROW_POINTS; sampleMemory_wavetableRangeValid() refuses
 *             any other entry), so the read is the stock table read: the point
 *             is the phase's top 10 bits, its successor wraps by the mask, the
 *             fraction is the low 22 bits. For a 1,024-point frame this is the
 *             former any-length read ((ph x len) >> 32, its low word / 2^32)
 *             bit for bit: the low word is (ph & 0x3FFFFF) << 10, exact in a
 *             float. Without the multiply, the compare and the 8-bit branch
 *             (which split every reader into an 8- and a 16-bit loop), each
 *             reader has one loop, 1-3 instructions shorter than its former
 *             16-bit loop in nine readers and 1 longer in the sync blend (S093
 *             P8 schedule §0).
 * Inputs:     data (a frame's first point), phase.
 * Outputs:    the interpolated value.
 * Accessors:  the wtb loops below.
 * Affiliates: SampleMemory.h WAVETABLE_ROW_POINTS; the stock table loops
 *             (calcWavetableOscBlock(), the same read).
 */
_Static_assert(WAVETABLE_ROW_POINTS == 1024u,
               "osc_wtbRead() takes the point from the phase's top 10 bits");
static inline __attribute__((always_inline)) float osc_wtbRead(
	const uint8_t *data, uint32_t ph)
{
	/* the point at or before ph: the top 10 bits of the 2^32 cycle */
	const uint32_t idx = ph >> 22;
	/* its successor, point 1,023 wrapping to point 0 */
	const uint32_t n = (idx + 1u) & (WAVETABLE_ROW_POINTS - 1u);
	/* the fraction between them: the low 22 bits / 2^22, 0..1 */
	const float fr = (float)(ph & 0x003FFFFFu) * (1.0f / 4194304.0f);
	const int32_t a = ((const int16_t *)data)[idx];
	const int32_t b = ((const int16_t *)data)[n];

	return (float)a + fr * (float)(b - a);
}
```

**Why it is exact:**
- **Index:** for len 1,024, the former index `(ph x 1,024) >> 32` is
  `ph >> 22`.
- **Fraction:** the former fraction, the low word `(ph x 1,024) mod 2^32 =
  (ph & 0x3FFFFF) << 10`, has at most 22 significant bits, so it converts to
  float exactly; × 2^-32 equals `(ph & 0x3FFFFF) × 2^-22`.
- **Next point:** the wrap `(idx + 1) == 1,024 ? 0 : idx + 1` equals
  `(idx + 1) & 0x3FF`.

Law W1 checks the equality at 100,000 random phases.

### 5.4 `Oscillator.c` l. 2723 — MODIFY (`osc_wtbFrame()`: 2 bytes per point)

```c
	/* S093 P8: 2 bytes per point (16-bit only) */
	const uint32_t bytes = (uint32_t)wt->frame_len * 2u;
```

`wt->frame_len` is 1,024 for every valid entry (§3.5), so a frame is
2,048 B.

### 5.5 `Oscillator.c` — the ten readers' calls (mechanical)

| Line(s) | Tag | Change |
|---|---|---|
| 2874, 2975, 3073, 3174, 3574 | MODIFY | `osc_wtbRead(base, wt->bits, wt->frame_len, X)` becomes `osc_wtbRead(base, X)` (the one-frame readers: plain, FM, ring, sync, FM ramp) |
| 2919–2920, 3019–3020, 3124–3125, 3224–3225, 3630–3631 | REMOVE | `const uint8_t bits = wt->bits;` and `const uint32_t len = wt->frame_len;` (the five blend readers) |
| 2929–2930, 3031–3032, 3134–3135, 3234–3235, 3644–3645 | MODIFY | `osc_wtbRead(a, bits, len, X)` / `osc_wtbRead(b, bits, len, X)` become `osc_wtbRead(a, X)` / `osc_wtbRead(b, X)` |

**The loops this produces:** §0.4. Each reader has one loop. ITCM shrinks
by 2,424 B, and the ITCM subsections after `.itcm.s087` move down. No
section attribute changes.

**NONE:**
- `osc_wtbFrameOf()`, `osc_wtbFramePos()`, `osc_wtbPlanBlock()` and the pos
  helpers: they count frames, not points;
- the S087 sample (`smp`/`slp`) loops, the stock waves, the FM/ring/sync
  plumbing;
- every other ITCM function (62/62 loops exact, §0.2).
---

## 6. `Core/DSP/Instruments/Alkaline/AlkalineVoice.h`

### 6.1 l. 55 — MODIFY (the wave values' comment); ADD after l. 57 (`ALK_WAVE_SQUARE`)

```c
/* osc.waveform under typ wav (S093 P8: row osc1_wave; was the
 * AlkalineVoice.wave byte of row alk_wave). Under typ wtb the same byte is
 * the wavetable index. */
#define ALK_WAVE_SAW          0u
#define ALK_WAVE_SQUARE       1u
/*
 * The typ byte (row osc1_type, S093 P8).
 *
 * What:       the stored and displayed value of the Alkaline typ cell:
 *             0 wav (Saw/Sqr, the 303's VCO), 1 wtb (a wavetable frame);
 *             ALK_TYP_COUNT entries (alkTypeNames, MenuText.h).
 *             Alkaline_setOscType() maps it to OSC_TYPE_WAV / OSC_TYPE_WTB in
 *             osc.oscType.
 * Why:        S093 P8: wavetables only. Looped samples are over the Basic
 *             ceiling on their loop alone (P8 assessment §6); one-shot samples
 *             fit (loop 215) but their render grew two mixer loops of other
 *             engines by 1 (P8 schedule §0), so they are left out (user,
 *             S093 P8). osc.oscType keeps the platform's values so the shared
 *             wave-cell helpers (instrumentManager_osc1Type(), osc_userCount(),
 *             menu_formatWaveCell()) need no Alkaline map.
 * Accessors:  Alkaline_setOscType(); AlkalineParameters.c osc1_type row.
 */
#define ALK_TYP_WAV           0u
#define ALK_TYP_WTB           1u
#define ALK_TYP_COUNT         2u
/*
 * The Alkaline's wavetable bind state (S093 P8), kept in
 * osc.sampleCacheLooped.
 *
 * What:       ALK_BIND_NONE (nothing bound: the next block or trigger binds),
 *             ALK_BIND_WTB (a table's 1,024-point rows are bound). With
 *             osc.sampleCacheIndex (the bound table) and
 *             osc.sampleCacheGeneration (the catalogue generation) they are
 *             the key alk_wtbKeyChanged() compares each block.
 * Why:        the Alkaline binds its own tables (P8 §7): osc_userBind() is
 *             never called on an Alkaline oscillator (its callers are
 *             osc_setFreq(), osc_setType() and osc_typedTriggerPhase(), none of
 *             which an Alkaline reaches), so these values never meet the
 *             OSC_BIND_* ones in the same field.
 * Accessors:  AlkalineVoice.c (alk_wtbBind(), alk_wtbKeyChanged(),
 *             Alkaline_setOscType()).
 */
#define ALK_BIND_NONE         0u
#define ALK_BIND_WTB          1u
```

### 6.2 l. 61 — MODIFY (the `osc` member's comment)

```c
	/* VCO (S093 P8): oscType (typ), waveform (wav: Saw/Sqr; wtb: the
	 * table), position (pos), phase/phaseInc; under wtb the bind state in
	 * the sampleCache* fields: sampleCacheData = the 1,024-point row of the
	 * frame being played, sampleCacheSize = the table's frame count;
	 * positionApplied = the frame being played */
	OscInfo      osc;
```

### 6.3 l. 79 — REMOVE

```c
	uint8_t wave;              /* ALK_WAVE_* */
```

Saw/Sqr is `osc.waveform` under `wav` (the `osc1_wave` row binds it).
`sizeof(AlkalineVoice)` stays 548 B: the byte sat in a `uint8_t` run padded to
the next `int32_t` (measured with `arm-none-eabi-gcc`, §0.2).

### 6.4 After l. 147 (`Alkaline_isQuiet()`) — ADD

```c
/*
 * Set the oscillator type from the typ byte (S093 P8).
 *
 * What:       maps ALK_TYP_* to osc.oscType (0 wav, 1 and above wtb); clamps
 *             osc.waveform to the new type's list (wav: Saw/Sqr; wtb: the
 *             installed tables); clears the bind so the next block (or
 *             trigger) binds; re-anchors positionApplied to pos. The phase
 *             runs on (both types are 2^32 per cycle). An unchanged type is a
 *             no-op.
 * Why:        the platform's osc_setType() binds through Drum's generic
 *             osc_userBind() and reads the byte as OSC_TYPE_* (1 = smp); the
 *             Alkaline binds its own tables (P8 §7) and offers wav and wtb.
 * Inputs:     voice (non-NULL), typ (0..255).
 * Outputs:    osc.oscType, waveform, positionApplied, sampleCacheLooped.
 * Accessors:  InstrumentManager.c instrumentManager_writeSpecialRuntime()
 *             (IM_SPECIAL_OSC_TYPE, an Alkaline slot); the host bench.
 * Affiliates: ALK_TYP_*; alk_wtbBind() (AlkalineVoice.c).
 */
void Alkaline_setOscType(AlkalineVoice *voice, uint8_t typ);
```

### 6.5 File header l. 4–7 — MODIFY (comment)

"the VCO (the existing ITCM table loop on sawTable or the D8 square table)"
becomes "the VCO (the fused table loop on sawTable, the D8 square table or,
since S093 P8, a wavetable frame's 1,024-point row)".

---

## 7. `Core/DSP/Instruments/Alkaline/AlkalineVoice.c`

`AlkalineVoice.c` reaches `SampleMemory.h` through `AlkalineVoice.h` →
`Oscillator.h` l. 45, and `WAVETABLE_ROW_POINTS` with it; no include changes.

### 7.1 After l. 19 (`ALK_V561`) — ADD (the scale, the silent row, the frame rule, the bind, the key test, the trigger, the per-block control)

```c
/* S093 P8: a wavetable row enters the ladder at the saw's scale: a
 * full-scale frame (+-32,767 LSB) swings as far as the stock saw (+-32,625
 * LSB = the 303 saw's 5.35 V), times the block's drive */
#define ALK_WTB_SCALE_V  ALK_SAW_SCALE_V

/*
 * The silent row (S093 P8).
 *
 * What:       one 1,024-point int16 row of zeros in flash (2,048 B).
 * Why:        typ wtb with no wavetable installed still runs its ladder (the
 *             resonance and the network ring out as on a rest): the bind
 *             points at this row as a one-frame table, so the table loop
 *             plays silence with no branch and no other loop.
 * Accessors:  alk_wtbBind().
 */
static const int16_t alk_zeroRow[WAVETABLE_ROW_POINTS] = { 0 };

/*
 * pos -> the nearest frame of a table (S093 P8).
 *
 * What:       (pos x (frames - 1) + 63) / 127 for pos 0..127, so pos 0 is
 *             frame 0 and pos 127 the last frame; 0 for a one-frame table.
 * Why:        Drum's nearest-frame rule (Oscillator.c osc_wtbFrameOf(),
 *             static there); the division by the constant 127 is an integer
 *             multiply-shift or a UDIV, never a VDIV.
 * Inputs:     pos (osc.position & 127), frames (1..128).
 * Outputs:    the frame index, 0..frames - 1.
 * Accessors:  alk_wtbBind(), alk_wtbTrigger(), alk_wtbControl().
 */
static inline uint32_t alk_wtbFrameOf(const uint32_t pos, const uint32_t frames)
{
	return (frames > 1u) ? ((pos * (frames - 1u) + 63u) / 127u) : 0u;
}

/*
 * Bind the wavetable (S093 P8).
 *
 * What:       records the key (ALK_BIND_WTB, the table = osc.waveform
 *             clamped to the last table, the catalogue generation), then
 *             sampleCacheSize = the table's frame count, positionApplied =
 *             pos's frame (a new table starts at its pos: no glide from the
 *             old table's frame) and sampleCacheData = that frame's 1,024-point
 *             row (sampleMemory_wavetableRows() + frame x 1,024). With no
 *             table installed: alk_zeroRow as a one-frame table.
 * Why:        S093 P8 §7: the Alkaline's own bind replaces Drum's
 *             osc_userBind() (a generic dispatch and the increment every
 *             block, P7 F3); it runs only when the key changes, has no
 *             division by a variable and no 64-bit operation. noinline keeps
 *             its registers out of the async's wav path; the cost gate counts
 *             it as if it ran every block.
 * Inputs:     voice->osc (waveform, position).
 * Outputs:    osc.sampleCacheLooped, sampleCacheIndex, sampleCacheGeneration,
 *             sampleCacheSize, sampleCacheData, positionApplied.
 * Accessors:  alk_wtbControl() (the async), alk_wtbTrigger().
 * Affiliates: ALK_BIND_*; alk_wtbKeyChanged(); SampleMemory.h
 *             sampleMemory_wavetableEntry(), sampleMemory_wavetableRows().
 */
static __attribute__((noinline)) void alk_wtbBind(AlkalineVoice *voice)
{
	OscInfo *const osc = &voice->osc;
	const uint8_t count = sampleMemory_getNumWavetables();
	/* the table: the wav byte, clamped to the last one (0 with none) */
	const uint8_t index = (osc->waveform < count) ? osc->waveform
	                    : (uint8_t)(count ? count - 1u : 0u);
	const wavetable_catalogue_entry_t *e = sampleMemory_wavetableEntry(index);
	/* frames of the bound source: the table's, or 1 for the zero row */
	const uint32_t frames = e ? e->frame_count : 1u;
	const uint32_t frame = alk_wtbFrameOf((uint32_t)(osc->position & 127u), frames);

	osc->sampleCacheLooped = ALK_BIND_WTB;
	osc->sampleCacheIndex = index;
	osc->sampleCacheGeneration = sampleMemory_getGeneration();
	osc->sampleCacheSize = frames;
	osc->positionApplied = (uint8_t)frame;
	/* the row being played: the table's first row + frame x 1,024 */
	osc->sampleCacheData = e ? sampleMemory_wavetableRows(e) + frame * WAVETABLE_ROW_POINTS
	                         : alk_zeroRow;
}

/*
 * Whether the bound table no longer matches the voice (S093 P8).
 *
 * What:       1 when nothing is bound (ALK_BIND_NONE after a typ switch or a
 *             slot reset), the table (osc.waveform against the bound, clamped
 *             index) or the catalogue generation (an install since the bind)
 *             differ.
 * Why:        the bind runs on a change only. A waveform past the last table
 *             (a stored value after an install with fewer tables) never equals
 *             the clamped index, so it rebinds every block: the worst case the
 *             cost gate already counts, and the frame then follows pos at once
 *             instead of stepping.
 * Accessors:  alk_wtbControl(), alk_wtbTrigger().
 */
static inline uint8_t alk_wtbKeyChanged(const OscInfo *osc)
{
	return (uint8_t)(osc->sampleCacheLooped != ALK_BIND_WTB ||
	                 osc->waveform != osc->sampleCacheIndex ||
	                 sampleMemory_getGeneration() != osc->sampleCacheGeneration);
}

/*
 * A non-legato note under typ wtb (S093 P8).
 *
 * What:       binds first when the key changed (a table chosen on this step
 *             by automation drains before the trigger), then moves the frame
 *             to pos's frame at once, the row pointer with it. The phase runs
 *             on: the 303's VCO is free-running, and under wtb pos is the
 *             frame, not the start phase (P6's pos reset is the wav rule).
 * Why:        P8 D4: pos on wtb is the frame; a note starts on its frame
 *             rather than stepping toward it.
 * Inputs:     voice->osc (position, the bind).
 * Outputs:    osc.positionApplied, osc.sampleCacheData.
 * Accessors:  Alkaline_triggerVoice() (main loop: voiceControl_processPending(),
 *             not the voice's per-block cost).
 */
static void alk_wtbTrigger(AlkalineVoice *voice)
{
	OscInfo *const osc = &voice->osc;
	uint32_t frame;

	if (alk_wtbKeyChanged(osc))
		alk_wtbBind(voice);
	frame = alk_wtbFrameOf((uint32_t)(osc->position & 127u), osc->sampleCacheSize);
	/* the row pointer follows the frame: + (new - old) x 1,024 */
	osc->sampleCacheData += ((int32_t)frame - (int32_t)osc->positionApplied)
	                        * (int32_t)WAVETABLE_ROW_POINTS;
	osc->positionApplied = (uint8_t)frame;
}

/*
 * The per-block control of typ wtb (S093 P8 §7).
 *
 * What:       binds when the key changed; steps the frame at most one frame
 *             per block toward pos's frame (P8 D4, Drum's no-slot stepping),
 *             moving the row pointer with it; then phaseInc by
 *             osc_setTableFreq(), the stock waves' cycle law (a row is 1,024
 *             points, 2^32 per cycle; the octave index it also sets is not
 *             used by a row).
 * Why:        the replacement for Drum's per-block typed path (P7 measured
 *             +670 per block): one key compare, one nearest-frame product, one
 *             +1/-1/0 step and the frequency.
 * Inputs:     voice->osc (waveform, position, freq, pitchMod, modNodeValue,
 *             the bind).
 * Outputs:    osc.phaseInc, osc.tableOffset (unused), osc.positionApplied,
 *             osc.sampleCacheData; the bind.
 * Accessors:  Alkaline_calcVoiceAsync().
 */
static inline void alk_wtbControl(AlkalineVoice *voice)
{
	OscInfo *const osc = &voice->osc;
	uint32_t target;
	uint32_t cur;
	int32_t step;

	if (alk_wtbKeyChanged(osc))
		alk_wtbBind(voice);
	/* pos's frame, and the frame being played */
	target = alk_wtbFrameOf((uint32_t)(osc->position & 127u), osc->sampleCacheSize);
	cur = osc->positionApplied;
	/* one frame per block toward the target: +1, -1 or 0 */
	step = (int32_t)(target > cur) - (int32_t)(target < cur);
	osc->positionApplied = (uint8_t)((int32_t)cur + step);
	/* the row being played moves with the frame (P8 §4.3) */
	osc->sampleCacheData += step * (int32_t)WAVETABLE_ROW_POINTS;
	osc_setTableFreq(osc);
}
```

**Why the row pointer moves by `step x 1,024` instead of being recomputed
from the table's base:**
- the base is not kept (no field for it without RAM);
- the bind, the trigger and the control are the only writers of
  `positionApplied` and `sampleCacheData` under `wtb`;
- each moves both together, and `Alkaline_setOscType()` clears the bind, which
  rewrites both.

Law AL-W checks the pointer against `sampleMemory_wavetableRows() + frame x
1,024` every block for 3,000 blocks (§12.4).

### 7.2 l. 25 — MODIFY (init: the type and the wave)

```c
	/* S093 P8: typ wav, wav Saw (init.alk osc1_type 0, osc1_wave 0). The
	 * stock SAW (2) set here before was only an octave-index hint that
	 * osc_setTableFreq() never read. */
	voice->osc.oscType = OSC_TYPE_WAV;
	voice->osc.waveform = ALK_WAVE_SAW;
```

### 7.3 l. 52 — REMOVE

```c
	voice->wave = ALK_WAVE_SAW;
```

### 7.4 After l. 114 (`alkVeg_trigger(...)` in the non-legato branch) — ADD

The P6 block that follows (the `pos` comment and its `{ ... }`) becomes the
`else` arm, unchanged:

```c
		/* S093 P8: typ wtb starts on pos's frame (alk_wtbTrigger());
		 * Saw/Sqr keep the P6 start-phase rule below */
		if (voice->osc.oscType == OSC_TYPE_WTB) {
			alk_wtbTrigger(voice);
		} else
```

**Legato** (a slide): no branch runs. The wavetable keeps its frame and the
phase runs on, as Saw/Sqr.

### 7.5 l. 184–216 — MODIFY (the async's oscillator tail)

`osc_setTableFreq()` and the P6 bend block move, unchanged and indented one
level, into the `wav` arm; the `wtb` arm calls `alk_wtbControl()`:

```c
	/* S093 P8: Saw/Sqr (typ wav) keep the table law and the P6 pos bend;
	 * typ wtb takes its own per-block control (alk_wtbControl()) */
	if (voice->osc.oscType == OSC_TYPE_WAV) {
		osc_setTableFreq(&voice->osc);
		/* ... the S093 P6 pos-bend comment and block, unchanged, one tab deeper ... */
	} else {
		alk_wtbControl(voice);
	}
```

The control path after it (MEG, accent, expo, VEG, drive, Res network) is
unchanged and runs for both types.

### 7.6 l. 254–279 — MODIFY (`Alkaline_calcVoiceSyncBlock()` becomes a per-block dispatch over two `noinline` renders)

```c
/*
 * One block of typ wav: Saw or Sqr through the fused table loop (S093 P3;
 * moved out of Alkaline_calcVoiceSyncBlock() in S093 P8).
 *
 * What:       the pre-P8 sync block body, unchanged: the octave's row of the
 *             stock Saw or of the D8 square (tableOffset, set by
 *             osc_setTableFreq() in the async), each at its own scale, times
 *             the drive.
 * Why:        noinline, and the row computed here as before: the S093 P8
 *             spike measured the inlined table loop at 213 in this form, and
 *             at 216 when the row was a pointer read from the oscillator or
 *             when the type dispatch shared the function (register
 *             allocation, one extra float spill). Keeping the Saw/Sqr render
 *             in its own function, and the table loop out of line
 *             (DiodeLadder_renderB2Osc() has two callers, this and
 *             alk_renderWtb()), fixes the loop at the baseline's 214 whatever
 *             the wtb code does.
 * Inputs:     voice (the async ran this block); buf (output).
 * Outputs:    buf; osc.phase; the ladder state.
 * Accessors:  Alkaline_calcVoiceSyncBlock().
 * Affiliates: bench law AL-X (bit-identical to calcWavetableOscBlock() +
 *             DiodeLadder_renderB2()); control_cost.py ITERS (rank 1 of
 *             DiodeLadder_renderB2Osc is the audio loop).
 */
static __attribute__((noinline)) void alk_renderWav(AlkalineVoice *voice, int16_t *buf)
{
	/* S093 P8: Saw/Sqr is osc.waveform under typ wav (was AlkalineVoice.wave) */
	const uint8_t square = (uint8_t)(voice->osc.waveform == ALK_WAVE_SQUARE);

	/* ... the rest of the former Alkaline_calcVoiceSyncBlock() body,
	 * unchanged: the B2R comment, #if ALK_FUSED_OSC with the
	 * DiodeLadder_renderB2Osc() call on square ? alkSqrTable[...] :
	 * sawTable[...], #else calcWavetableOscBlock() + DiodeLadder_renderB2(),
	 * #endif (the former (void)size; line moves to the dispatch below) ... */
}
```

```c
/*
 * One block of typ wtb: a wavetable frame through the fused table loop
 * (S093 P8).
 *
 * What:       DiodeLadder_renderB2Osc() on the frame's 1,024-point row
 *             (osc.sampleCacheData, kept on the frame being played by
 *             alk_wtbBind(), alk_wtbTrigger() and alk_wtbControl()) at
 *             ALK_WTB_SCALE_V (the Saw's), times the drive. With no table
 *             installed the row is alk_zeroRow: silence.
 * Why:        P8 §4: a frame's row has a sawTable row's layout, so the same
 *             loop plays it with no new read code; the same function, too
 *             (DiodeLadder_renderB2Osc(), out of line with two callers).
 * Inputs:     voice (bound by the async); buf (output).
 * Outputs:    buf; osc.phase; the ladder state.
 * Accessors:  Alkaline_calcVoiceSyncBlock().
 * Affiliates: bench law AL-W.
 */
static __attribute__((noinline)) void alk_renderWtb(AlkalineVoice *voice, int16_t *buf)
{
	DiodeLadder_renderB2Osc(&voice->ladder, buf, voice->osc.sampleCacheData,
	                        &voice->osc.phase, voice->osc.phaseInc,
	                        ALK_WTB_SCALE_V * voice->driveGain,
	                        voice->iLadEnd, voice->iAbcEnd * voice->makeup);
}

void Alkaline_calcVoiceSyncBlock(AlkalineVoice *voice, int16_t *buf, const uint8_t size)
{
	(void)size;   /* the ladder always renders OUTPUT_DMA_SIZE */
	/*
	 * S093 P8: the per-block type dispatch.
	 *
	 * What:       wtb -> alk_renderWtb() (the frame's row), anything else
	 *             -> alk_renderWav() (Saw/Sqr).
	 * Why:        one branch per block, outside every loop; both feed the
	 *             one out-of-line table loop (S093 P8 spike: 214, mixer 0
	 *             grew; with the type code inlined beside it, 216).
	 */
	if (voice->osc.oscType == OSC_TYPE_WTB)
		alk_renderWtb(voice, buf);
	else
		alk_renderWav(voice, buf);
}
```

**Why two functions:** see §0.3 S1. The Saw/Sqr render keeps the exact
pre-P8 source in a function of its own, so its loop cannot move with the
wavetable code. With two callers, `DiodeLadder_renderB2Osc()` is linked once,
out of line. Its loop is 214, the same instructions as the baseline's inlined
loop; only the call adds per-block instructions (counted in the 1,319).

**`ALK_FUSED_OSC 0`** (the AL-X reference build): `wtb` stays fused. A
wavetable row has no `[11][1024]` table for `calcWavetableOscBlock()`, and
`alk_renderWtb()` has no `#if`.

### 7.7 After l. 285 (end of `Alkaline_isQuiet()`) — ADD

```c
/*
 * Set the oscillator type from the typ byte (S093 P8; see the header).
 */
void Alkaline_setOscType(AlkalineVoice *voice, const uint8_t typ)
{
	OscInfo *const osc = &voice->osc;
	/* the byte -> the platform type: 0 wav, 1 and above wtb */
	const uint8_t type = (typ == ALK_TYP_WAV) ? OSC_TYPE_WAV : OSC_TYPE_WTB;
	/* the number of entries of the new type's wav list */
	const uint8_t count = (type == OSC_TYPE_WAV) ? (uint8_t)(ALK_WAVE_SQUARE + 1u)
	                    : sampleMemory_getNumWavetables();
	const uint8_t last = count ? (uint8_t)(count - 1u) : 0u;

	if (type == osc->oscType)
		return;
	/* the phase runs on: wav and wtb are both 2^32 per cycle */
	osc->oscType = type;
	/* the stored byte is used as it is, clamped to an entry that exists
	 * (the platform's typ rule, S087 P0 r8) */
	if (osc->waveform > last)
		osc->waveform = last;
	/* bind on the next block or trigger; under wav, pos re-anchors so no
	 * bend follows the switch (under wtb the bind sets the frame) */
	osc->sampleCacheLooped = ALK_BIND_NONE;
	osc->positionApplied = (uint8_t)(osc->position & 127u);
}
```

**`sampleMemory_getNumWavetables()` here:** it runs in the writer, in the
main loop, not on the block path.

---

## 8. `Core/DSP/Instruments/Alkaline/AlkalineParameters.c` / `.h` — the rows

### 8.1 `AlkalineParameters.c` l. 20–23 and l. 27 — MODIFY (file header)

```c
 * Not offered (S092 P0 §5.5): srt (no SRR, D4), run, chn, fine pitch,
 *             velo_vol_on_off (the 303's velocity is the accent, D1).
 *             typ is offered since S093 P8 with two types, wav and wtb
 *             (samples are not: S093_P8_IMPLEMENTATION.md §0).
 * Accessors:  InstrumentManager registry; Menu (pages); Preset/Morph/LFO/
 *             automation through the registry; storage (keys, [morph] for
 *             the MORPHABLE rows only, storageTypes.c l. 981-990).
 * Affiliates: AlkalineVoice.h; MenuText.h (alkWaveNames, alkTypeNames,
 *             ONOFF_TAG_ALK_TYPE);
```

(l. 24–26, "Accessors", stay between them.)

### 8.2 After l. 52 (`MOD_PM63`) — ADD

```c
/* S093 P8: a wave row's domain (DrumParameters.c l. 57-59): integers, the
 * maximum from the slot's type (instrumentManager_waveDomainMax(): Saw/Sqr
 * under wav, the items under smp/wtb) */
#define MOD_WAVE  { 0u, 0u, (uint8_t)(INSTRUMENT_MOD_DOMAIN_INTEGER | INSTRUMENT_MOD_DOMAIN_DYNAMIC_MAX) }
```

### 8.3 l. 61–75 — MODIFY (`ROW_SELECT_ONOFF` becomes `ROW_SELECT_ONOFF_SPECIAL`)

The S093 P4 macro (comment and `#define`) is replaced. Its only user was the
`alk_wave` row (§8.5).

```c
/*
 * A tagged DTYPE_ON_OFF selector with a special writer (S093 P8; Drum's
 * ROW_SELECT_ONOFF_SPECIAL, DrumParameters.c l. 108-130).
 *
 * What:       DTYPE_ON_OFF | (tag << 4) with AUTOMATABLE | SELECTOR: never
 *             morphed (so not under [morph]), not an LFO/velocity target; the
 *             tag picks the name table in menu_onoffTagNames(); the runtime
 *             write goes to the IM_SPECIAL_* writer.
 * Why:        the Alkaline typ list (wav wtb) is its own two names, and
 *             every DTYPE_MENU id is taken (MenuText.h, S078), so the list is a
 *             tagged on/off sub-type as osc1_run's is.
 * Accessors:  the osc1_type row.
 */
#define ROW_SELECT_ONOFF_SPECIAL(key_, cat_, long_, short_, tag_, member_, type_, special_) \
    { key_, short_, long_, cat_, (uint8_t)(DTYPE_ON_OFF | ((tag_) << 4)), \
      (INSTRUMENT_PARAM_FLAG_AUTOMATABLE | INSTRUMENT_PARAM_FLAG_SELECTOR), \
      MOD_NONE, BIND_SPECIAL(member_, type_, special_) }
```

### 8.4 l. 126 — ADD after it (the enum)

```c
    ALK_PARAM_OSC1_TYPE,         /* 28 (S093 P8): wav / wtb */
```

### 8.5 l. 144–148 — MODIFY (row 0: `alk_wave` becomes `osc1_wave`)

```c
    /*
     * wav (S093 P8: Drum's osc1_wave row shape, DrumParameters.c l. 233).
     *
     * What:       under typ wav: 0 Saw (the stock sawTable, the 303 ramp
     *             within 0.5 %), 1 Sqr (the D8 per-octave Q8 square,
     *             AlkalineSquareTable.c); under wtb: the wavetable, named
     *             wNN. DTYPE_MENU | MENU_WAVEFORM, MOD_WAVE, an image cell
     *             bound to osc.waveform.
     * Why:        one cell names Saw/Sqr or the item by typ (P8 §8); the
     *             shared wave helpers key on "osc1_wave" and know the
     *             Alkaline case (instrumentManager_waveDomainMax(),
     *             menu_formatWaveCell()). As on Drum, Morph and the LFO step
     *             through the entries (under wav: Saw/Sqr switch).
     * Accessors:  alk_renderWav() (square = wav 1); alk_wtbBind() (the
     *             table); the menu wave helpers.
     * Affiliates: ALK_WAVE_SAW / ALK_WAVE_SQUARE; alkWaveNames.
     */
    ROW_MENU("osc1_wave", "Oscilltr", "Waveform", "wav", MENU_WAVEFORM, MOD_WAVE, osc.waveform, TYPE_UINT8),
```

**What changes for this cell:**
- **The key:** `alk_wave` becomes `osc1_wave`, with no migration. An older
  file's `alk_wave` is skipped (unknown keys are skipped, `FILESYSTEM_SPEC.md`
  l. 1821), and the absent `osc1_wave` and `osc1_type` load 0 (the slot is
  zeroed before parsing, l. 1928): Saw under `wav`. A file saved with Sqr
  loads as Saw.
- **It becomes an image cell:** Morph steps between entries (`wav`: Saw/Sqr
  switch), as on Drum.
- **The LFO and velocity can target it:** `MOD_WAVE`, its range from
  `instrumentManager_waveDomainMax()`, §10.1.
- **The `[morph]` section gains the row:** 23 rows, against 22.
- **Shared helpers:** `instrumentManager_osc()` resolves `osc1_` for
  `INSTRUMENT_TYPE_ALK` (`InstrumentManager.c` l. 2383), so every helper keyed
  on `"osc1_wave"` reaches the Alkaline oscillator.

### 8.6 After l. 208 (the `osc1_position` row) — ADD (row 28)

```c
    /*
     * S093 P8: typ, the oscillator type.
     *
     * What:       0 wav (Saw/Sqr through the fused table loop), 1 wtb (a
     *             wavetable frame's 1,024-point row through the same table
     *             loop); names alkTypeNames (tag ONOFF_TAG_ALK_TYPE).
     *             The writer (IM_SPECIAL_OSC_TYPE, an Alkaline slot) is
     *             Alkaline_setOscType(), which maps the byte to osc.oscType.
     * Why:        S093 P8: the wavetable oscillator under the Basic ceiling;
     *             samples are not offered (S093_P8_IMPLEMENTATION.md §0).
     * Accessors:  Alkaline_triggerVoice(), Alkaline_calcVoiceAsync(),
     *             Alkaline_calcVoiceSyncBlock() (osc.oscType).
     */
    ROW_SELECT_ONOFF_SPECIAL("osc1_type", "Oscilltr", "Type", "typ", ONOFF_TAG_ALK_TYPE, osc.oscType, TYPE_UINT8, IM_SPECIAL_OSC_TYPE | IM_SPECIAL_OSC1),
```

- **Bound to `osc.oscType`, as Drum's row is.** The binding's member is read
  back only by modulation-target resolution (`InstrumentManager.c` l. 2789),
  and a SELECTOR row is never a target. The writer maps the byte (§10.2), so
  the member holds `OSC_TYPE_WAV`/`OSC_TYPE_WTB` while the stored byte is
  0/1.
- **Tag:** `IM_SPECIAL_OSC_TYPE | IM_SPECIAL_OSC1` is what
  `check_special_tags.py` `expected()` (l. 51–52) and the firmware classifier
  (`InstrumentManager.c` l. 4033–4034) give for any `osc1_type` (§11).

### 8.7 l. 239–240 — MODIFY (page 0)

```c
    /* 0 OSC: coa tun wav pos | typ (S093 P8: Drum's second screen) */
    {{ ALK_PARAM_OSC1_PITCH_COARSE, ALK_PARAM_TUNING, ALK_PARAM_WAVE, ALK_PARAM_OSC1_POSITION, ALK_PARAM_OSC1_TYPE, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY, INSTRUMENT_MENU_EMPTY }},
```

Cell 4 is the first cell of screen 2, Drum's `typ` place
(`DrumParameters.c` l. 396–398). It replaces the first of the twelve
`INSTRUMENT_MENU_EMPTY`; 16 cells.

### 8.8 `#undef` list l. 270 and l. 274 — MODIFY

- `#undef ROW_SELECT_ONOFF` becomes `#undef ROW_SELECT_ONOFF_SPECIAL`;
- `#undef MOD_WAVE` is added before `#undef MOD_PM63`.

### 8.9 `AlkalineParameters.h` l. 19–22 — MODIFY

```c
/* 29 rows: 10 Alkaline rows, vol/pan, 8 LFO rows, 2 velocity rows, 4 LFO
 * target rows, then (S093 P6, appended) alk_attack and osc1_position, and
 * (S093 P8, appended) osc1_type (asserted against alkaline_param_index_t in
 * the .c) */
#define ALKALINE_PARAM_DESCRIPTOR_COUNT 29u
```

---

## 9. `Core/Menu/MenuText.h` / `menu.c` — the names

### 9.1 `MenuText.h` l. 225–252 — MODIFY (tag 3 now names the type; add the type names; re-comment the wave names)

```c
/*
 * DTYPE_ON_OFF tag for the Alkaline type selector (S093 P8).
 *
 * What:       ONOFF_TAG_ALK_TYPE = 3u: the Alkaline osc1_type row's dtype
 *             byte is DTYPE_ON_OFF | (3 << 4) = 0x36. Tags 1 (osc1_run) and 2
 *             (voice_chain) stay. Tag 3 named the S093 P4 alk_wave row, which
 *             S093 P8 replaced by an osc1_wave MENU_WAVEFORM row; no row
 *             carries the old meaning.
 * Why:        S088 D12 reserved the high nibble of DTYPE_ON_OFF for tagged
 *             selectors (every DTYPE_MENU id and dtype value is taken, S078);
 *             menu.c resolves the name table per tag in menu_onoffTagNames().
 * Accessors:  ROW_SELECT_ONOFF_SPECIAL (AlkalineParameters.c, osc1_type).
 * Affiliates: alkTypeNames below; ALK_TYP_* (AlkalineVoice.h).
 */
#define ONOFF_TAG_ALK_TYPE      3u
/*
 * Alkaline type names (S093 P8).
 *
 * What:       [0] = count (2), then wav, wtb: the Alkaline typ byte 0..1
 *             (ALK_TYP_WAV / ALK_TYP_WTB).
 * Why:        display, clamp and automation range of the typ cell come from
 *             this count (the six tagged sites in menu.c); the Alkaline offers
 *             wavetables only (S093 P8). The count must equal ALK_TYP_COUNT
 *             (asserted in menu.c).
 * Accessors:  menu_onoffTagNames() (menu.c).
 */
static const char alkTypeNames[][4] = {
    {2}, {"wav"},{"wtb"},
};
/*
 * Alkaline waveform names (S093 P4; S093 P8: the osc1_wave cell under typ
 * wav).
 *
 * What:       [0] = count (2), then Saw, Sqr.
 * Why:        an Alkaline slot's wav list under typ wav is these two
 *             (ALK_WAVE_SAW 0, ALK_WAVE_SQUARE 1); menu_formatWaveCell() names
 *             them and instrumentManager_waveDomainMax() limits the range to
 *             them. The count must equal ALK_WAVE_SQUARE + 1 (asserted in
 *             menu.c).
 * Accessors:  menu_formatWaveCell() (menu.c).
 */
static const char alkWaveNames[][4] = {
    {2}, {"Saw"},{"Sqr"},
};
```

**Reusing tag 3** (instead of taking 4): no row carries the old meaning after
§8.5. The tag is a code constant and never stored, so there is nothing to
migrate.

### 9.2 `menu.c` after l. 12165 (`memset(codeDst, ...)` in `menu_formatWaveCell()`) — ADD

```c
    /*
     * S093 P8: an Alkaline slot's osc1 under wav lists Saw, Sqr.
     *
     * What:       value 0 -> "Saw", 1 -> "Sqr" (alkWaveNames), no code;
     *             any other value names nothing (blank), as a Drum value past
     *             its list.
     * Why:        the stock list (waveformNames: Sin, Tri, ...) is not the
     *             Alkaline's; the voice plays Saw for 0 and Sqr for 1
     *             (alk_renderWav()). Under wtb the Alkaline cell takes the
     *             shared branch below (wNN, the table name).
     * Accessors:  menu_formatWaveValue3() (compact cells, overlays, the voice
     *             automation display), the single-parameter painter.
     */
    if (isOsc1 && type == OSC_TYPE_WAV &&
        instrumentManager_runtimeType(cell->slot) == INSTRUMENT_TYPE_ALK) {
        if (value < (uint8_t)alkWaveNames[0][0]) {
            memcpy(nameDst, alkWaveNames[value + 1u], 3u);
            memcpy(codeDst, alkWaveNames[value + 1u], 3u);
        }
        return;
    }
```

`instrumentManager_runtimeType()` is public (`InstrumentManager.h` l. 891).

### 9.3 `menu.c` l. 12337–12358 — MODIFY (`menu_onoffTagNames()`: comment and the tag case)

- In the comment, "ONOFF_TAG_ALK_WAVE -> alkWaveNames (Saw, Sqr); every other"
  becomes "ONOFF_TAG_ALK_TYPE -> alkTypeNames (wav, wtb; S093 P8); every
  other".
- In "Affiliates:", `alkWaveNames` becomes `alkTypeNames`.
- The case:

```c
    case ONOFF_TAG_ALK_TYPE:    return alkTypeNames;
```

### 9.4 `menu.c` after l. 12508 (the `alkWaveNames` assert) — ADD

```c
/* S093 P8: the Alkaline typ list is wav, wtb (ALK_TYP_COUNT, AlkalineVoice.h) */
_Static_assert((sizeof(alkTypeNames) / sizeof(alkTypeNames[0])) - 1u == 2u,
               "alkTypeNames must list wav and wtb");
```

`menu.c` does not include `AlkalineVoice.h`, so the count is the literal 2,
as in the `alkWaveNames` assert above it.

---

## 10. `Core/DSP/Instruments/InstrumentManager.c` — the range and the writer

`AlkalineVoice.h` is included at l. 16; `instrumentManager_alkalineRuntime()`
is defined at l. 295, before both sites.

### 10.1 l. 847 — ADD after it (`instrumentManager_waveDomainMax()`)

```c
            /* S093 P8: an Alkaline slot under wav has two entries (Saw,
             * Sqr): the LFO/velocity range, the menu clamp, the edit count
             * and the typ-switch clamp (menu.c) all take it from here */
            if (osc && osc->oscType == OSC_TYPE_WAV &&
                instrumentManager_slotType(slot) == INSTRUMENT_TYPE_ALK)
                return (uint8_t)ALK_WAVE_SQUARE;
```

**Its function comment (l. 827–836):** the "What:" list gains "an Alkaline
slot under wav: ALK_WAVE_SQUARE (Saw, Sqr)".

**Through this one function:**
- `instrumentManager_waveCellCount()` (l. 872–876; the menu clamp, the edit
  count, the `typ`-switch clamp at `menu.c` l. 5336–5360);
- the LFO and velocity domains (`instrumentManager_descriptorModDomain()`).

### 10.2 l. 3899 — MODIFY (the `IM_SPECIAL_OSC_TYPE` writer)

```c
        /*
         * S093 P8: an Alkaline slot's typ byte goes to its own writer.
         *
         * What:       Alkaline_setOscType() maps 0 wav / 1 wtb to
         *             osc.oscType, clamps osc.waveform and clears the
         *             Alkaline bind; the wave domains refresh as for every
         *             type.
         * Why:        osc_setType() reads the byte as OSC_TYPE_* (1 = smp,
         *             which the Alkaline does not offer) and binds through
         *             Drum's osc_userBind(), whose cache fields the Alkaline
         *             uses for its own bind (the row of the frame being
         *             played; AlkalineVoice.h ALK_BIND_*).
         * Affiliates: AlkalineVoice.h Alkaline_setOscType(), ALK_TYP_*.
         */
        if (instrumentManager_slotType(slot) == INSTRUMENT_TYPE_ALK) {
            AlkalineVoice *alk = instrumentManager_alkalineRuntime(slot);

            if (!alk) return 0u;
            Alkaline_setOscType(alk, byteValue);
        } else {
            osc_setType(osc, byteValue);
        }
```

**Its case comment (l. 3883–3895):** "What:" adds "(an Alkaline slot:
Alkaline_setOscType())"; "Affiliates:" adds `Alkaline_setOscType()`.

**Every `typ` write goes through here:** the menu, Kit/Scene/Instrument
apply, restore and step automation
(`instrumentManager_writeRuntimeInternal()` → `writeSpecialRuntime()`).

**Load order** (rows apply in index order): `osc1_wave` (row 0) writes the
byte, then `osc1_type` (row 28) clamps it to the new type's list. This is
Drum's order.
---

## 11. Verified: no change needed

| Site | Why no change |
|---|---|
| `Core/DSPAudio/DiodeLadder.c` / `.h` | wavetables use `DiodeLadder_renderB2Osc()` as it is; one-shot samples are not in P8 (§0.3 S2) |
| The Alkaline and `osc_userBind()` | the Alkaline never calls `osc_setType()`, `osc_userBind()`, `osc_setFreq()` or the typed readers. `osc_userBind()`'s callers are `osc_setFreq()`, `osc_setType()` and `osc_typedTriggerPhase()`, so nothing else writes an Alkaline oscillator's `sampleCache*` fields. The `osc_recalcFreq()` callers (`InstrumentManager.c` l. 3728, 3735, the coarse/fine writers) set `osc.freq` only (`Oscillator.c` l. 1368) |
| `osc_userBind()`'s wavetable bind for the other instruments | it reads `entry->offset` (the first row) and `frame_len` (1,024) from the entry, so it binds the rows unchanged; only its depth choice goes (§5.2) |
| `instrumentManager_waveCellCount()` (l. 872–876), `instrumentManager_osc1Type()` (l. 887–892) | the first is `waveDomainMax + 1` (§10.1); the second returns the runtime `oscType`, which the writer keeps at the platform's values |
| `instrumentManager_waveInterpTarget()` (l. 2656–2673) and `buildModulationRange()` (l. 2726–2728) | for a `DYNAMIC_MAX` row the range needs a non-NULL oscillator; `instrumentManager_osc(slot, "osc1_wave")` returns the Alkaline's (l. 2383) |
| The `typ`-switch image clamp, `menu.c` l. 5321–5360 | keyed on `IM_SPECIAL_OSC_TYPE`; clamps the stored `osc1_wave` images to `waveCellCount()`, which reads the runtime type the writer has just set |
| `menu.c` `MENU_WAVEFORM` sites l. 3408, 6590, 6888, 13929, 15147; l. 12084; `menu_automationValueMax()` | through `menu_formatWaveValue3()` / `menu_formatWaveCell()` (§9.2) or `waveCellCount()` (§10.1); an Alkaline `wav` value past its list shows blank and plays Saw |
| The six tagged ON/OFF sites in `menu.c` | they take the table from `menu_onoffTagNames()` (§9.3) |
| `check_special_tags.py`; the firmware classifier (`InstrumentManager.c` l. 3987–4034) | `osc1_type` → `IM_SPECIAL_OSC_TYPE \| IM_SPECIAL_OSC1`; `osc1_wave` untagged. Run: 200 rows, 0 mismatches |
| `filesystem_parseWav()` | an 8-bit `fmt` now fails `sampleInstall_acceptFormat()` (§4.3), which it already reports as a refused file |
| The sample walker and `sampleInstall_writeFrames()` | samples were already stored 16-bit and counted per point |
| The built-in 8-bit ROM sounds (`crashSample`, `transientData`) | out of scope (O2) |
| The Advanced-place rule, Copy | the Alkaline stays Basic in both types |
| `modulationNode.c`, the interpolation pool | no Alkaline blend; Drum's blend claims are unchanged |
| `SceneData.h`, the Kit image | row 28 fits the 64-cell image (asserted, `AlkalineParameters.c` l. 132) |
| The linker script | no ITCM code is added; the wavetable readers shrink inside their existing sections |

---

## 12. Tools, bench and data

### 12.1 `tools/alkaline/control_cost.py` l. 66–68 — MODIFY (ITERS comment)

```python
    # S093 P4 (G4): on the linked image LTO inlines the fused B2R read into
    # the sync block, so its only loop is the audio loop; weight 0 (A4).
    # S093 P8: DiodeLadder_renderB2Osc has two callers (alk_renderWav,
    # alk_renderWtb) and stays out of line, so the audio loop is its rank 1
    # (above) and the sync block has none; the entry stays for a layout that
    # inlines it again (the guard below checks either)
    ("Alkaline_calcVoiceSyncBlock", 1): 0,
```

`("DiodeLadder_renderB2Osc", 1): 0` already exists (l. 63).

### 12.2 `control_cost.py` after l. 95 (the `calcWavetableOscBlock` stub in `STUBS`) — ADD

```c
/* S093 P8: the catalogue calls of the wtb bind (control rate; the stub link
 * only needs them to resolve; --elf costs the real ones) */
static const int16_t stub_rows[2][1024];
static const wavetable_catalogue_entry_t stub_entry = { 0u, 1024u, 2u, 16u, "stub    ", 0u, { 0u, 0u, 0u } };
uint8_t sampleMemory_getNumWavetables(void) { return 1u; }
uint32_t sampleMemory_getGeneration(void) { return 1u; }
const wavetable_catalogue_entry_t *sampleMemory_wavetableEntry(uint8_t index)
{ return index ? (const wavetable_catalogue_entry_t *)0 : &stub_entry; }
const int16_t *sampleMemory_wavetableRows(const wavetable_catalogue_entry_t *entry)
{ return entry ? stub_rows[0] : (const int16_t *)0; }
```

The stub link (no `--elf`) stays an estimate; **the gate is `--elf`**.

### 12.3 `control_cost.py` l. 215 — MODIFY (the weight-0 guard)

```python
        # S093 P8: the same check for the out-of-line fused table loop
        if base in ("Alkaline_calcVoiceSyncBlock", "DiodeLadder_renderB2Osc") \
                and it == 0 and (lv != 3 or length < 200):
```

### 12.4 `tools/dsp_test/modules/alkaline.c` — MODIFY

**Header, after l. 54 (the AL-PS entry) — ADD:**

```c
 *               AL-W  (S093 P8) typ wtb: the voice is bit-identical to the
 *                     table read of the bound frame's 1,024-point row
 *                     (calcWavetableOscBlock()'s arithmetic) then
 *                     DiodeLadder_renderB2(), block for block, phase too; the
 *                     frame steps one per block toward pos and a non-legato
 *                     note jumps to it (legato does not); an install rebinds;
 *                     an empty catalogue plays silence;
 *               AL-TY (S093 P8) Alkaline_setOscType(): 0 -> wav, 1 and above
 *                     -> wtb; wav clamps wav to Sqr, wtb to the last table;
 *                     the phase runs on; the bind is cleared; the same type
 *                     is a no-op.
```

**l. 69 — ADD after it:** `#include <stdlib.h>` (for `abs()` in AL-W).

**Groups, l. 115–117 — MODIFY:**

```c
enum { G_B, G_R, G_S, G_N, G_AB, G_D, G_Q, G_F, G_O, G_X, G_A, G_M, G_SO, G_PS, G_W, G_TY };
static const char *g_name[] = { "AL-B", "AL-R", "AL-S", "AL-N", "AL-AB", "AL-D", "AL-Q",
                                "AL-F", "AL-O", "AL-X", "AL-A", "AL-M", "AL-SO", "AL-PS",
                                "AL-W", "AL-TY" };
```

The name is **AL-TY**: `seq_steplen` already has a law AL-T.

**`wave` → `osc.waveform`, five lines — MODIFY:**
- l. 145: `v->wave == ALK_WAVE_SQUARE` becomes `v->osc.waveform == ALK_WAVE_SQUARE`;
- l. 169, 534, 815, 861: `v.wave = ` / `a.wave = ` become `v.osc.waveform = ` /
  `a.osc.waveform = `.

**Before `main()` (l. 883) — ADD (laws AL-W and AL-TY):**

```c
/* ---------------------------------------------------------------- AL-W
 * S093 P8: typ wtb plays the bound frame's 1,024-point row through the same
 * fused table loop as Saw/Sqr. Install: one 3-frame table of 600-point 16-bit
 * frames (the card's AKWF length, so the copy is resampled) with different
 * harmonics per frame, and one 2-frame table of 1,024-point frames (copied as
 * they are). */
static void w_install(int second)
{
	static int16_t f600[3][600];
	static int16_t f1024[2][1024];
	const sample_install_fmt_t fw = { 1u, 1u, 44100u, 2u, 16u, 16u };
	int i, f;

	for (f = 0; f < 3; f++)
		for (i = 0; i < 600; i++)
			f600[f][i] = (int16_t)(28000.0 * sin(6.283185307 * i * (2 * f + 1) / 600.0));
	for (f = 0; f < 2; f++)
		for (i = 0; i < 1024; i++)
			f1024[f][i] = (int16_t)(20000.0 * sin(6.283185307 * i * (f + 3) / 1024.0));
	/* the install's frame buffer (the firmware borrows the name cache) */
	static int16_t fbuf[WAVETABLE_FRAME_MAX];

	catalogue_host_reset();
	sampleInstall_begin();
	if (!second) {
		(void)sampleInstall_beginWavetable("w600    ", 600u, 3u, &fw);
		for (f = 0; f < 3; f++)
			(void)sampleInstall_writeFrame((const uint8_t *)f600[f], 600u, fbuf);
		(void)sampleInstall_finishWavetable();
	}
	(void)sampleInstall_beginWavetable("w1024   ", 1024u, 2u, &fw);
	for (f = 0; f < 2; f++)
		(void)sampleInstall_writeFrame((const uint8_t *)f1024[f], 1024u, fbuf);
	(void)sampleInstall_finishWavetable();
	(void)sampleInstall_commit();
	sampleMemory_refresh();
}

/* pos -> nearest frame, the law alk_wtbFrameOf() implements */
static uint32_t w_frameOf(uint32_t pos, uint32_t frames)
{
	return (frames > 1u) ? ((pos * (frames - 1u) + 63u) / 127u) : 0u;
}

/* the reference block: the voice's async, then calcWavetableOscBlock()'s
 * table read (gain 1) of `row` and the plain B2R at the Saw scale */
static void w_ref(AlkalineVoice *v, const int16_t *row, int16_t *buf)
{
	uint32_t ph;
	int j;

	Alkaline_calcVoiceAsync(v);
	ph = v->osc.phase;
	for (j = 0; j < BLK; j++) {
		const uint32_t itg = ph >> 22;
		int16_t o = row[itg];

		o += (ph & 0x003FFFFFu) * 2.384185791015625e-07f
		   * (row[(itg + 1u) & 0x000003FFu] - o);
		ph += v->osc.phaseInc;
		buf[j] = o;
	}
	v->osc.phase = ph;
	DiodeLadder_renderB2(&v->ladder, buf, ALK_SAW_SCALE_V * v->driveGain,
	                     v->iLadEnd, v->iAbcEnd * v->makeup);
}

static void test_wavetable(void)
{
	AlkalineVoice a, b;
	const wavetable_catalogue_entry_t *e;
	uint32_t expect = 0u;
	unsigned diff = 0u, phd = 0u, frd = 0u;
	double rms = 0.0;
	int n, j;

	w_install(0);
	e = sampleMemory_wavetableEntry(0u);
	if (!e) { fail(G_W, "no table installed", 0, 0); return; }
	memset(&a, 0, sizeof(a));
	Alkaline_initVoice(&a);
	a.cutoff = 0.55f; a.reso = 0.6f; a.envMod = 0.6f; a.accent = 0.7f; a.drive = 0.3f;
	Alkaline_setOscType(&a, ALK_TYP_WTB);
	a.osc.waveform = 0u;
	b = a;
	/* W1, W2: 3,000 blocks of 16ths with accents and slides; pos jumps
	 * every 23 blocks (the frame steps toward it), a note jumps to it */
	for (n = 0; n < 3000; n++) {
		int16_t ba[BLK], bb[BLK];

		if (n % 23 == 0)
			a.osc.position = b.osc.position = (uint8_t)((n * 37) & 127);
		if (n % 172 == 0) {
			const uint8_t legato = (uint8_t)(a.slideLatch && a.gateLeft > 0);

			a.slide = b.slide = (uint8_t)((n / 172) % 4 == 2);
			Alkaline_triggerVoice(&a, 0u, (n / 172) % 3 ? 80u : 127u,
			                      (uint8_t)(40 + (n * 7) % 40), ALK_STEP);
			Alkaline_triggerVoice(&b, 0u, (n / 172) % 3 ? 80u : 127u,
			                      (uint8_t)(40 + (n * 7) % 40), ALK_STEP);
			if (!legato)    /* a note starts on pos's frame */
				expect = w_frameOf(a.osc.position & 127u, e->frame_count);
		}
		/* the block's frame: one step toward pos's frame */
		{
			const uint32_t target = w_frameOf(a.osc.position & 127u, e->frame_count);

			expect = (target > expect) ? expect + 1u : ((target < expect) ? expect - 1u : expect);
		}
		Alkaline_calcVoiceAsync(&a);
		Alkaline_calcVoiceSyncBlock(&a, ba, BLK);
		w_ref(&b, sampleMemory_wavetableRows(e) + expect * WAVETABLE_ROW_POINTS, bb);
		for (j = 0; j < BLK; j++) {
			diff += (ba[j] != bb[j]);
			rms += (double)ba[j] * ba[j];
		}
		phd += (a.osc.phase != b.osc.phase);
		frd += (a.osc.positionApplied != expect);
	}
	printf("AL-W wtb: %u sample(s), %u phase(s), %u frame(s) differ; RMS %.0f LSB\n",
	       diff, phd, frd, sqrt(rms / (3000.0 * BLK)));
	if (diff || phd) fail(G_W, "fused wtb != table read + B2R", diff, phd);
	if (frd) fail(G_W, "frame stepping law", frd, 0);
	if (rms == 0.0) fail(G_W, "wtb is silent", 0, 0);

	/* W3: an install rebinds at the next block: the table index 0 now names
	 * the 1,024-point table (generation changed) */
	w_install(1);
	{
		int16_t buf[BLK];
		const wavetable_catalogue_entry_t *e2 = sampleMemory_wavetableEntry(0u);

		Alkaline_calcVoiceAsync(&a);
		Alkaline_calcVoiceSyncBlock(&a, buf, BLK);
		if (!e2 || a.osc.sampleCacheGeneration != sampleMemory_getGeneration() ||
		    a.osc.sampleCacheSize != e2->frame_count ||
		    a.osc.sampleCacheData != sampleMemory_wavetableRows(e2) +
		                             (uint32_t)a.osc.positionApplied * WAVETABLE_ROW_POINTS)
			fail(G_W, "an install does not rebind", 0, 0);
	}

	/* W4: an empty catalogue plays silence through the zero row */
	catalogue_host_reset();
	sampleMemory_refresh();
	{
		AlkalineVoice z;
		int16_t buf[BLK];
		int peak = 0;

		memset(&z, 0, sizeof(z));
		Alkaline_initVoice(&z);
		z.reso = 1.0f; z.drive = 1.0f;
		Alkaline_setOscType(&z, ALK_TYP_WTB);
		for (n = 0; n < 400; n++) {
			if (n % 100 == 0)
				Alkaline_triggerVoice(&z, 0u, 127u, 60u, ALK_STEP);
			block(&z, buf, K_B2R);
			for (j = 0; j < BLK; j++)
				if (abs(buf[j]) > peak) peak = abs(buf[j]);
		}
		printf("AL-W empty catalogue: peak %d LSB\n", peak);
		if (peak != 0) fail(G_W, "no table is not silent", peak, 0);
	}
}

/* ---------------------------------------------------------------- AL-TY
 * S093 P8: the typ writer. */
static void test_typ(void)
{
	AlkalineVoice v;

	w_install(0);   /* two tables (w600, w1024): the wtb list's last entry is 1 */
	memset(&v, 0, sizeof(v));
	Alkaline_initVoice(&v);
	v.osc.phase = 0x12345678u;
	v.osc.waveform = 5u;
	Alkaline_setOscType(&v, ALK_TYP_WTB);
	if (v.osc.oscType != OSC_TYPE_WTB) fail(G_TY, "1 is wtb", v.osc.oscType, 0);
	if (v.osc.waveform != 1u) fail(G_TY, "wtb clamps to the last table", v.osc.waveform, 0);
	if (v.osc.phase != 0x12345678u) fail(G_TY, "the phase runs on", 0, 0);
	if (v.osc.sampleCacheLooped != ALK_BIND_NONE) fail(G_TY, "the bind is cleared", 0, 0);
	v.osc.sampleCacheLooped = ALK_BIND_WTB;
	Alkaline_setOscType(&v, 7u);
	if (v.osc.oscType != OSC_TYPE_WTB || v.osc.sampleCacheLooped != ALK_BIND_WTB)
		fail(G_TY, "the same type is a no-op", v.osc.oscType, 0);
	v.osc.waveform = 9u;
	Alkaline_setOscType(&v, ALK_TYP_WAV);
	if (v.osc.oscType != OSC_TYPE_WAV) fail(G_TY, "0 is wav", v.osc.oscType, 0);
	if (v.osc.waveform != ALK_WAVE_SQUARE) fail(G_TY, "wav clamps to Sqr", v.osc.waveform, 0);
	if (v.osc.phase != 0x12345678u) fail(G_TY, "the phase runs on (wtb -> wav)", 0, 0);
}
```

**`main()`, after `test_pos();` (l. 933) — ADD:**

```c
	test_wavetable();
	test_typ();
```

**What AL-W proves:**
- **W1, bit-identity.** Each block is compared with an independent
  reference: the same async, then `calcWavetableOscBlock()`'s arithmetic on
  the row the law computes itself, then the plain `DiodeLadder_renderB2()`.
  Output and phase are compared over 3,000 blocks with accents, slides and
  `pos` jumps.
- **W2, the frame law.** One step per block toward `pos`'s frame; a jump on a
  non-legato note.
- **W3:** an install rebinds the next block.
- **W4:** an empty catalogue is exactly silent.

The tables go through the real installer (with a host frame buffer), so the
600-point table plays its resampled rows.

### 12.5 `tools/dsp_test/modules/samples.c` — MODIFY

**l. 48 — ADD after it:** `#include <math.h>`.

**Header, l. 21–27 (the install laws) — MODIFY; before l. 28 (the `order` entry) — ADD the rows laws:**

```c
 *               install    1. PCM 16/24-bit mono and stereo and extensible
 *                             PCM are accepted; 8-bit (S093 P8, D10), float,
 *                             3+ channels and 32-bit are rejected, for samples
 *                             and wavetables;
 *                          2. samples store 16-bit mono; 24-bit converts and
 *                             stereo averages;
 *                          3. a 24-bit stereo wavetable frame is stored as a
 *                             16-bit mono 1,024-point row (S093 P8, O1); an
 *                             8-bit table does not open.
 *               rows       (S093 P8) the stored 1,024-point rows:
 *                          1. a frame written in 512-byte chunks (three
 *                             writeFrame() calls per 600-point frame, as
 *                             filesystem_installFrames() makes them) counts
 *                             once: frame_count is the frames, not the calls;
 *                             a point past the opened geometry is refused;
 *                          2. the entry is frame_len 1,024, bits 16, its rows
 *                             at offset, frame_count x 2,048 B; the next item
 *                             starts right after them;
 *                          3. a 1,024-point frame is stored exactly;
 *                          4. a 600-point frame: harmonics 1, 7, 50, 200 within
 *                             0.05 dB, every other bin below -80 dBFS; at the
 *                             band edge, harmonic 270 (0.45 x 600) within
 *                             0.05 dB and its images below -80 dBFS (break:
 *                             a kernel of H = 16);
 *                          5. a 2,048-point frame: harmonic 100 within
 *                             0.05 dB, harmonic 700 (past the row's 512)
 *                             below -80 dBFS;
 *                          6. the fit check counts the rows: a table of 128
 *                             frames (262,144 B) is refused in 253,948 B, one
 *                             of 100 frames (204,800 B) is accepted;
 *                          7. v3: the committed header is LXC3 / 3, and a v2
 *                             header reads STALE.
```

**Install law 1, l. 243–245 — MODIFY (8-bit refused for both kinds):**

```c
    /* S093 P8 (D10): 8-bit is refused for every kind */
    make_fmt(fmt, 1u, 2u, 8u);
    expect(!sampleInstall_acceptFormat(fmt, 40u, SAMPLE_INSTALL_KIND_SAMPLE, &out),
           "law 1: PCM 8 sample rejected");
    expect(!sampleInstall_acceptFormat(fmt, 40u, SAMPLE_INSTALL_KIND_WAVETABLE, &out),
           "law 1: PCM 8 wavetable rejected");
```

**Install law 2c, l. 280–288, and its checks l. 304–305 — MODIFY (16-bit stereo replaces 8-bit stereo):**

```c
    /* Law 2c: 16-bit stereo averages (S093 P8: was 8-bit). */
    make_fmt(fmt, 1u, 2u, 16u);
    (void)sampleInstall_acceptFormat(fmt, 40u, SAMPLE_INSTALL_KIND_SAMPLE, &out);
    {
        const int16_t src[4] = { 1000, 3000, -2000, -4001 };
        expect(sampleInstall_beginSample("p16st   ", 2u, &out), "begin 16 stereo");
        expect(sampleInstall_writeFrames((const uint8_t *)src, 2u), "write 16 stereo");
        expect(sampleInstall_finishSample(0u, 0u, 0u), "finish 16 stereo");
    }
```

```c
        expect(stored16(e->offset, 0u) == 2000, "law 2: stereo average");
        expect(stored16(e->offset, 1u) == -3000, "law 2: stereo average, negative");
```

**Install law 3, l. 308 to the end of `run_install()` — REPLACE (a 24-bit stereo frame becomes a 16-bit mono row; an 8-bit table does not open):**

```c
    /* Law 3 (S093 P8, O1): a 24-bit stereo wavetable frame is stored as a
     * 16-bit mono row (1,024 points, so stored exactly, no resampling); an
     * 8-bit table does not open (D10). */
    catalogue_host_reset();
    sampleInstall_begin();
    make_fmt(fmt, 1u, 2u, 24u);
    expect(sampleInstall_acceptFormat(fmt, 40u, SAMPLE_INSTALL_KIND_WAVETABLE, &out),
           "law 3: PCM 24 stereo wavetable accepted");
    {
        static uint8_t src[1024u * 6u];
        static int16_t fbuf[WAVETABLE_FRAME_MAX];
        const sample_install_fmt_t f8 = { 1u, 1u, 44100u, 1u, 8u, 16u };
        uint32_t i;

        /* point i: left high word i x 16, right high word 64 - i x 16, so
         * the mono average of the two is 32 at every point */
        for (i = 0u; i < 1024u; i++) {
            const int32_t l = (int32_t)(i * 16u) << 8;
            const int32_t r = -((int32_t)(i * 16u) << 8) + (64 << 8);

            src[i * 6u + 0u] = (uint8_t)(l & 0xff);
            src[i * 6u + 1u] = (uint8_t)((l >> 8) & 0xff);
            src[i * 6u + 2u] = (uint8_t)((l >> 16) & 0xff);
            src[i * 6u + 3u] = (uint8_t)(r & 0xff);
            src[i * 6u + 4u] = (uint8_t)((r >> 8) & 0xff);
            src[i * 6u + 5u] = (uint8_t)((r >> 16) & 0xff);
        }
        expect(!sampleInstall_beginWavetable("w8      ", 16u, 1u, &f8),
               "law 3: an 8-bit table does not open");
        expect(sampleInstall_beginWavetable("w24st   ", 1024u, 1u, &out),
               "begin 24-bit stereo table");
        expect(sampleInstall_writeFrame(src, 1024u, fbuf), "write 24-bit stereo frame");
        expect(sampleInstall_finishWavetable(), "finish 24-bit stereo table");
    }
    expect(sampleInstall_commit(), "commit table");
    sampleMemory_refresh();
    expect(sampleMemory_getNumWavetables() == 1u, "one wavetable");
    {
        const wavetable_catalogue_entry_t *w = sampleMemory_wavetableEntry(0u);

        expect(w != NULL, "table entry");
        if (w) {
            const int16_t *row = sampleMemory_wavetableRows(w);

            expect(w->frame_len == WAVETABLE_ROW_POINTS && w->frame_count == 1u &&
                   w->bits == 16u, "law 3: the row geometry");
            /* each point: (i x 16 + (64 - i x 16)) / 2 = 32 */
            expect(row[0] == 32 && row[1023] == 32, "law 3: 24-bit stereo -> 16-bit mono");
        }
    }
}
```

**Before l. 336 (`/* ==== order -- */`) — ADD (the rows group):**

```c
/* ============================================================== rows -- */
/* S093 P8 helpers: one frame of harmonics, and a harmonic's level in a
 * 1,024-point row (a single-bin DFT, dB re full scale 32767) */
static void make_frame(int16_t *x, int n, const int *h, const double *a, int k)
{
    int i, j;

    for (i = 0; i < n; i++) {
        double v = 0.0;

        for (j = 0; j < k; j++)
            v += a[j] * sin(6.283185307179586 * h[j] * i / n + 0.3 * j);
        x[i] = (int16_t)lrint(v);
    }
}

static double row_bin_db(const int16_t *row, int bin)
{
    double re = 0.0, im = 0.0;
    int i;

    for (i = 0; i < (int)WAVETABLE_ROW_POINTS; i++) {
        re += row[i] * cos(6.283185307179586 * bin * i / WAVETABLE_ROW_POINTS);
        im -= row[i] * sin(6.283185307179586 * bin * i / WAVETABLE_ROW_POINTS);
    }
    return 20.0 * log10(2.0 * sqrt(re * re + im * im) / WAVETABLE_ROW_POINTS / 32767.0 + 1e-12);
}

static void run_rows(void)
{
    const sample_install_fmt_t fw16 = { 1u, 1u, 44100u, 2u, 16u, 16u };
    static int16_t f600[3][600], f2048[2048], f1024[1024], fedge[600];
    static int16_t fbuf[WAVETABLE_FRAME_MAX];   /* the install's frame buffer */
    static const int h600[4] = { 1, 7, 50, 200 };
    static const double a600[4] = { 12000.0, 6000.0, 3000.0, 1500.0 };
    static const int h2048[2] = { 100, 700 };
    static const double a2048[2] = { 16000.0, 8000.0 };
    static const int h270[1] = { 270 };
    static const double a270[1] = { 16000.0 };
    const wavetable_catalogue_entry_t *w;
    const int16_t *rows;
    double worst_h = 0.0, worst_floor = -200.0;
    int f, i, b;

    for (f = 0; f < 3; f++)
        make_frame(f600[f], 600, h600, a600, 4);
    make_frame(f2048, 2048, h2048, a2048, 2);
    make_frame(fedge, 600, h270, a270, 1);
    for (i = 0; i < 1024; i++)
        f1024[i] = (int16_t)(i * 31 - 16000);
    catalogue_host_reset();
    sampleInstall_begin();
    /* table 0: three 600-point frames, each in three chunks (256 + 256 + 88) */
    expect(sampleInstall_beginWavetable("w600    ", 600u, 3u, &fw16), "rows: begin 600");
    for (f = 0; f < 3; f++) {
        expect(sampleInstall_writeFrame((const uint8_t *)&f600[f][0], 256u, fbuf), "rows: chunk 1");
        expect(sampleInstall_writeFrame((const uint8_t *)&f600[f][256], 256u, fbuf), "rows: chunk 2");
        expect(sampleInstall_writeFrame((const uint8_t *)&f600[f][512], 88u, fbuf), "rows: chunk 3");
    }
    expect(!sampleInstall_writeFrame((const uint8_t *)f600[0], 1u, fbuf),
           "rows law 1: a point past the opened geometry is refused");
    expect(sampleInstall_finishWavetable(), "rows: finish 600");
    /* table 1: one 2,048-point frame; 2: one 1,024-point frame; 3: the
     * band-edge frame */
    expect(sampleInstall_beginWavetable("w2048   ", 2048u, 1u, &fw16), "rows: begin 2048");
    expect(sampleInstall_writeFrame((const uint8_t *)f2048, 2048u, fbuf), "rows: write 2048");
    expect(sampleInstall_finishWavetable(), "rows: finish 2048");
    expect(sampleInstall_beginWavetable("w1024   ", 1024u, 1u, &fw16), "rows: begin 1024");
    expect(sampleInstall_writeFrame((const uint8_t *)f1024, 1024u, fbuf), "rows: write 1024");
    expect(sampleInstall_finishWavetable(), "rows: finish 1024");
    expect(sampleInstall_beginWavetable("wedge   ", 600u, 1u, &fw16), "rows: begin edge");
    expect(sampleInstall_writeFrame((const uint8_t *)fedge, 600u, fbuf), "rows: write edge");
    expect(sampleInstall_finishWavetable(), "rows: finish edge");
    expect(sampleInstall_commit(), "rows: commit");
    sampleMemory_refresh();
    expect(sampleMemory_getNumWavetables() == 4u, "rows: four tables");

    w = sampleMemory_wavetableEntry(0u);
    expect(w != NULL, "rows: table 0");
    if (w) {
        const wavetable_catalogue_entry_t *w1 = sampleMemory_wavetableEntry(1u);

        expect(w->frame_count == 3u, "rows law 1: chunked frames count once");
        expect(w->frame_len == WAVETABLE_ROW_POINTS && w->bits == 16u,
               "rows law 2: the entry describes 1,024-point 16-bit rows");
        rows = sampleMemory_wavetableRows(w);
        expect((const uint8_t *)rows == SAMPLE_FLASH_PTR(w->offset),
               "rows law 2: the rows are at the offset");
        expect(w1 && w1->offset == w->offset + 3u * WAVETABLE_ROW_BYTES,
               "rows law 2: the next item follows the 3 x 2,048 B");
        for (f = 0; f < 3; f++) {
            const int16_t *row = rows + f * WAVETABLE_ROW_POINTS;

            for (i = 0; i < 4; i++) {
                const double want = 20.0 * log10(a600[i] / 32767.0);
                const double got = row_bin_db(row, h600[i]);

                if (fabs(got - want) > worst_h) worst_h = fabs(got - want);
            }
            for (b = 0; b <= 512; b++) {
                if (b == 1 || b == 7 || b == 50 || b == 200)
                    continue;
                {
                    const double lv = row_bin_db(row, b);

                    if (lv > worst_floor) worst_floor = lv;
                }
            }
        }
        printf("rows: 600-point frames: harmonics within %.4f dB, other bins below %.1f dBFS\n",
               worst_h, worst_floor);
        expect(worst_h <= 0.05, "rows law 4: harmonic levels of a 600-point frame");
        expect(worst_floor <= -80.0, "rows law 4: no other content (images) in a 600-point frame");
    }
    w = sampleMemory_wavetableEntry(1u);
    if (w) {
        const int16_t *row = sampleMemory_wavetableRows(w);
        const double h100 = row_bin_db(row, 100) - 20.0 * log10(16000.0 / 32767.0);
        /* harmonic 700 of the frame folds to row bin 1024 - 700 = 324 if it
         * survives the kernel */
        const double h700 = row_bin_db(row, 324);

        printf("rows: 2,048-point frame: harmonic 100 %+.4f dB, harmonic 700 at %.1f dBFS\n",
               h100, h700);
        expect(fabs(h100) <= 0.05, "rows law 5: harmonic 100 of a 2,048-point frame");
        expect(h700 <= -80.0, "rows law 5: harmonic 700 removed");
    }
    w = sampleMemory_wavetableEntry(2u);
    if (w) {
        expect(memcmp(sampleMemory_wavetableRows(w), f1024, sizeof(f1024)) == 0,
               "rows law 3: a 1,024-point frame is stored exactly");
    }
    w = sampleMemory_wavetableEntry(3u);
    if (w) {
        const int16_t *row = sampleMemory_wavetableRows(w);
        const double lv = row_bin_db(row, 270) - 20.0 * log10(16000.0 / 32767.0);
        double img = -200.0;

        for (b = 0; b <= 512; b++)
            if (b != 270 && row_bin_db(row, b) > img)
                img = row_bin_db(row, b);
        printf("rows: band edge (harmonic 270 of 600): %+.4f dB, images %.1f dBFS\n", lv, img);
        expect(fabs(lv) <= 0.05, "rows law 4: harmonic 270 of a 600-point frame is flat");
        expect(img <= -80.0, "rows law 4: harmonic 270 leaves no image");
    }
    {
        const sample_catalogue_header_t *hd =
            (const sample_catalogue_header_t *)cat_base();

        expect(hd->magic == 0x3343584Cu && hd->version == 3u, "rows law 7: header LXC3 / 3");
    }

    /* law 6: fill the item region with one maximal sample, then a 128-frame
     * table: its rows (262,144 B) do not fit; 100 frames (204,800 B) do */
    catalogue_host_reset();
    sampleInstall_begin();
    {
        static int16_t big[4096];
        const sample_install_fmt_t fmt16 = { 1u, 1u, 44100u, 2u, 16u, 16u };
        uint32_t left = SAMPLE_MAX_FRAMES;

        expect(sampleInstall_beginSample("big     ", SAMPLE_MAX_FRAMES, &fmt16), "rows: begin big");
        while (left) {
            const uint32_t n = left < 4096u ? left : 4096u;

            (void)sampleInstall_writeFrames((const uint8_t *)big, n);
            left -= n;
        }
        expect(sampleInstall_finishSample(0u, 0u, 0u), "rows: finish big");
        printf("rows: %u B free after the big sample\n", (unsigned)sampleInstall_bytesFree());
        expect(!sampleInstall_beginWavetable("wfull   ", 16u, 128u, &fw16),
               "rows law 6: a table whose rows do not fit is refused");
        expect(sampleInstall_beginWavetable("wfit    ", 16u, 100u, &fw16),
               "rows law 6: a table whose rows fit is accepted");
        sampleInstall_abortItem();
    }

    /* law 7: a v2 header (the former magic, version 2) reads STALE */
    catalogue_host_reset();
    {
        uint8_t *hd = cat_base();
        const uint32_t m2 = 0x3243584Cu;
        const uint16_t v2 = 2u;

        memcpy(hd, &m2, 4u);
        memcpy(hd + 4, &v2, 2u);
        hd[6] = SAMPLE_ENTRY_BYTES;
        hd[7] = WAVETABLE_ENTRY_BYTES;
        cat_host_image[0] = 0u;     /* item region programmed: not EMPTY */
        sampleMemory_refresh();
        expect(sampleMemory_catalogueState() == SAMPLE_CATALOGUE_STALE,
               "rows law 7: a v2 catalogue reads STALE");
    }
}
```

**`main()`, after `group("install", run_install);` (l. 367) — ADD:**

```c
    group("rows", run_rows);
```

### 12.6 `tools/dsp_test/modules/oscillator.c` — MODIFY (16-bit tables, the fixed read)

**Header l. 30 — MODIFY:**

```c
 *               wtb    W1 the 1,024-point read equals the any-length read
 *                      (S093 P8); W2 the wrap; W3 8-bit tables are refused;
```

**`install_table()`, l. 895–905 — MODIFY (16-bit only, a frame buffer):**

```c
/* S093 P8: the install's frame buffer (the firmware borrows the name cache) */
static int16_t g_frameBuf[WAVETABLE_FRAME_MAX];

static void install_table(uint16_t len, const int16_t *f0, const int16_t *f1)
{
    /* S093 P8: 16-bit sources; the table is stored as 1,024-point rows */
    const sample_install_fmt_t fmt = { 1u, 1u, 44100u, 2u, 16u, 16u };

    catalogue_host_reset();
    sampleInstall_begin();
    (void)sampleInstall_beginWavetable("wt      ", len, 2u, &fmt);
    (void)sampleInstall_writeFrame((const uint8_t *)f0, len, g_frameBuf);
    (void)sampleInstall_writeFrame((const uint8_t *)f1, len, g_frameBuf);
```

**Its callers — MODIFY:** l. 935, 960, 1115, 1232: `install_table(16u, 16u, ` and
`install_table(64u, 16u, ` lose the `16u` depth argument.

**`run_wtb()` W1–W3, l. 924–957 — REPLACE:**

```c
    static int16_t row[1024];
    int16_t buf[2];
    OscInfo o;
    uint32_t s = 0x5093u;

    for (uint8_t i = 0u; i < 16u; i++) {
        f0[i] = (int16_t)(i * 1000);
        f1[i] = (int16_t)(30000 - i * 1000);
    }
    for (uint32_t i = 0u; i < 1024u; i++)
        row[i] = (int16_t)(golden_rand(&s) >> 16);
    install_table(16u, f0, f1);

    /* W1 (S093 P8): the fixed 1,024-point read equals the former any-length
     * read at len 1,024 bit for bit (index (ph x 1,024) >> 32, fraction its
     * low word / 2^32), at 100,000 phases */
    for (uint32_t k = 0u; k < 100000u; k++) {
        const uint32_t ph = golden_rand(&s);
        const uint64_t m = (uint64_t)ph * 1024u;
        const uint32_t idx = (uint32_t)(m >> 32);
        const uint32_t n = (idx + 1u == 1024u) ? 0u : idx + 1u;
        const float fr = (float)(uint32_t)m * (1.0f / 4294967296.0f);
        const float want = (float)row[idx] + fr * (float)(row[n] - row[idx]);

        if (osc_wtbRead((const uint8_t *)row, ph) != want) {
            expect(0, "W1 the 1,024-point read is the any-length read");
            break;
        }
    }
    /* W2: the last point, and the wrap to point 0 */
    expect(osc_wtbRead((const uint8_t *)row, 1023u << 22) == (float)row[1023],
           "W2 the last point");
    expect(osc_wtbRead((const uint8_t *)row, (1023u << 22) + (1u << 21)) ==
           (float)row[1023] + 0.5f * (float)(row[0] - row[1023]),
           "W2 the wrap interpolates to point 0");

    /* W3 (S093 P8, D10): an 8-bit table is refused, before and at begin */
    {
        uint8_t fmt8[16] = { 1u, 0u, 1u, 0u, 0x44u, 0xACu, 0u, 0u,
                             0x44u, 0xACu, 0u, 0u, 1u, 0u, 8u, 0u };
        const sample_install_fmt_t f8 = { 1u, 1u, 44100u, 1u, 8u, 16u };
        sample_install_fmt_t out;

        expect(!sampleInstall_acceptFormat(fmt8, 16u, SAMPLE_INSTALL_KIND_WAVETABLE, &out),
               "W3 an 8-bit wavetable format is refused");
        catalogue_host_reset();
        sampleInstall_begin();
        expect(!sampleInstall_beginWavetable("w8      ", 16u, 1u, &f8),
               "W3 an 8-bit table does not open");
    }
```

(The declarations above it change from `f0b[16]` to `row[1024]` plus the
random seed `s`; the frame fill drops `f0b` and fills `row` from
`golden_rand()`.)

**The 8-bit FR case — REMOVE:**
- l. 1218–1239: the `f0b` / `f1b` arrays and the `else` branch that installed the
  8-bit table;
- l. 1252: the `{ "wtb8", ... }` case row;
- l. 1195: `c->kind == 2u || c->kind == 3u` becomes `c->kind == 2u`.

W4–W8 and FR1/FR2 are unchanged and pass on the resampled rows: a 16- or
64-point frame's row point 0 is its point 0 exactly, because at t = 0 every
other tap sits on a zero of the sinc.

### 12.7 `tools/dsp_test/Makefile` — ADD (two break proofs)

**`samples` rule, after l. 226:**

```makefile
	mkdir -p build/break_rows
	sed 's|#define SAMPLE_INSTALL_ROW_HALF 32 |#define SAMPLE_INSTALL_ROW_HALF 16 |' \
	  $(ROOT)/Core/SampleRom/SampleInstall.c > build/break_rows/SampleInstall.c
	@grep -q 'SAMPLE_INSTALL_ROW_HALF 16 ' build/break_rows/SampleInstall.c || \
	  { echo "samples_break_rows: sed did not apply"; exit 1; }
	$(HOSTCC) $(HOSTFLAGS) -I$(ROOT)/Core/SampleRom $(CAT_HOST_INC) \
	  modules/samples.c $(ROOT)/Core/SampleRom/SampleMemory.c \
	  build/break_rows/SampleInstall.c catalogue_host.c \
	  -o build/samples_break_rows -lm
	@if ./build/samples_break_rows >/dev/null 2>&1; then \
	  echo "samples_break_rows: FAILED (H = 16 kernel not caught)"; exit 1; \
	else echo "samples_break_rows: caught (rows law 4 fails as expected)"; fi
```

**`alkaline` rule, after l. 330:**

```makefile
	mkdir -p build/break_alk_wtb
	sed 's/osc->sampleCacheData += step \* (int32_t)WAVETABLE_ROW_POINTS;/(void)step;/' \
	  $(GEN)/AlkalineVoice.host.c > build/break_alk_wtb/AlkalineVoice.host.c
	@grep -q '(void)step;' build/break_alk_wtb/AlkalineVoice.host.c || \
	  { echo "alkaline_break_wtb: sed did not apply"; exit 1; }
	$(HOSTCC) -Ibuild/break_alk_wtb $(HOSTFLAGS) $(OSC_HOST_INC) modules/alkaline.c \
	  $(CAT_HOST_SRC) -o build/alkaline_break_wtb -lm
	@if ./build/alkaline_break_wtb >/dev/null 2>&1; then \
	  echo "alkaline_break_wtb: FAILED (a frame row that does not follow pos not caught)"; exit 1; \
	else echo "alkaline_break_wtb: caught (law AL-W fails as expected)"; fi
```

**What each break proves:**
- **A kernel of H = 16:** band-edge images at −34.6 dBFS, so rows-4 fails.
- **A row pointer that does not follow the frame:** AL-W fails.

### 12.8 `tools/alkaline/make_init_alk.py` l. 47 and after l. 57 — MODIFY / ADD

```python
    "osc1_wave": "0", "osc1_pitch_coarse": "36", "alk_tuning": "63", "alk_slide": "0",
    ...
    # S093 P8 (appended row): typ wav (Saw/Sqr); osc1_wave (row 0, was
    # alk_wave) is an image row since P8, so it is under [morph] too
    "osc1_type": "0",
```

`MORPHABLE` (l. 43) lists `ROW_MENU`, so `osc1_wave` goes under `[morph]`.

### 12.9 `SD_CARD/Instrument/Alkaline/init.alk` — regenerated

Run `python3 tools/alkaline/make_init_alk.py`:

```diff
 [params]
-alk_wave=0
+osc1_wave=0
 ...
 osc1_position=0
+osc1_type=0

 [morph]
+osc1_wave=0
 osc1_pitch_coarse=36
```

29 `[params]` rows, 23 `[morph]` rows; `--check` passes.

---

## 13. Gates

All of these were run on the spike and passed (§0). After P8 lands they are
re-run on the tree.

| Gate | Command | Pass (spike result) |
|---|---|---|
| Host: catalogue | `make -C tools/dsp_test samples` | catalogue / install / rows / order 0 failures; rows: harmonics ±0.0001 dB, floor −113.5 dBFS, 2,048-point h700 −110.2 dBFS, band edge −0.001 dB / −85.2 dBFS; `samples_break`, `samples_break_rows` caught |
| Host: oscillator | `make -C tools/dsp_test oscillator` | every group 0 failures (W1 at 100,000 phases); `oscillator_break_fmr1/2` caught |
| Host: Alkaline | `make -C tools/dsp_test alkaline` | 16 groups, 0 failures; AL-W 0 samples / 0 phases / 0 frames differ (RMS 1,732 LSB), empty catalogue peak 0; `alkaline_break`, `alkaline_break_wtb` caught |
| Host: others | `make -C tools/dsp_test special_tags instrument_manager seq_steplen` (one by one) | 0 failures, every break caught; 200 rows, 0 mismatches; `init.alk` 29 rows |
| Build | `make clean && make all` | no warning beyond the base's |
| Alkaline loop | `elf_looppath.py --elf build/lxr02.elf --sym DiodeLadder_renderB2Osc --cond-heads --in-range --top 2` | **214**, 3 VDIV, one loop |
| Alkaline per block | `control_cost.py --elf build/lxr02.elf` | **1,319**, 9 VDIV → whole voice **≤ 255.2** |
| Readers | `elf_looppath.py` on the ten `osc_wtb*Block` symbols | §0.4 column "this schedule" |
| `elf_p4_check` | `make -C tools/dsp_test elf_p4_check` | against `s093p4`: 62 exact, 31 MISSING, all in the ten readers; mixer 30 equal, **0 grew**. The `osc` compare stops the rule on MISSING, so the mixer compare is run on its own: `python3 elf_loopdump.py --elf ../../build/lxr02.elf --sym mixer_calcNextSampleBlock --out build/p4_mixer_now.txt && python3 elf_hotloops.py --compare baselines/s093p4/mixer.txt build/p4_mixer_now.txt` |
| Re-baseline | `make -C tools/dsp_test elf_p4_snapshot P4BASE=baselines/s093p8` | after the two lines above pass. `P4BASE` in the Makefile then moves to `baselines/s093p8`, with the reason in its comment (the ten readers' 8-bit copies removed, D9) |
| Budget | `link_budget.py arm-none-eabi-nm build/lxr02.elf` | headroom **44,556 B** ≥ 16 KiB; ITCM **12,128 B** |
| RAM | `arm-none-eabi-nm -S` diff against the P6 image | no new RAM symbol; `.bss` unchanged |

**Device (user):**
- **Install.** `Load:[Samples]` with `ak_hvox`: completes; time it against P6
  (estimate +2 s, §0.5). The Load page shows its names again on the next
  entry (the name cache was lent).
- **Old card.** A card installed by P6 shows the STALE notice until it is
  reinstalled.
- **8-bit files.** An 8-bit sample and an 8-bit wavetable folder on the card
  are not installed; a 24-bit stereo wavetable folder is.
- **Drum `wtb` on `ak_hvox`.** All 104 frames are reached (`pos` high); the
  sound is unchanged by ear.
- **Alkaline `typ`/`wav`.** `typ` shows `wav`/`wtb`. `wav` shows Saw/Sqr, or
  the table names under `wtb`. Switching `typ` clamps `wav`.
- **Alkaline sound.** A `wtb` pattern with slides and accents. A `pos` sweep
  steps through the frames; a note after a `pos` change starts on its frame.
  Res and drive full; aliasing at high notes is a listening item.
- **CPU worst case.** Four Alkalines in `wtb` with an LFO on `wav`, plus
  Drums in `wtb` blend, in the worst-case Scene: 0 underruns over 5 minutes.
- **`DEV_LOGGING_IWDG` 1 build only.** A full install with the largest table
  does not trip the watchdog (`config.h` l. 196 audit).

---

## 14. Statements to be logged (P8-6: for the user; not edited by this schedule)

`MEMORY.md` "Document edits (user, S093, binding)": no step of this schedule
edits `MEMORY.md` or anything under `knowledge_files/`. Revision 2 first
scheduled those edits here (a documents phase). During the implementation,
`MEMORY.md` and three specifications were edited, then restored to the
commit (§17, "scope correction"). Those edits, and every other statement P8
makes out of date, are listed below **for the user to log**.

Line numbers are those of the current files. For `MEMORY.md`, that includes
its new "Document edits" section.

### 14.1 Edits made during the implementation, then restored

| File | What was edited | Where it is recorded now |
|---|---|---|
| `MEMORY.md`, the Alkaline rule (now l. 224–240) | replaced by the text quoted below | quoted below, verbatim as the implementer wrote it |
| `MEMORY.md`, Sample Flash (now l. 444–459) | replaced by the text quoted below | quoted below, verbatim |
| `knowledge_files/specification_reference/FILESYSTEM_SPEC.md` | edited and restored; its contents now equal commit `ff837fce` | the edited text is not kept in the tree. Its out-of-date statements are listed in §14.2 |
| `knowledge_files/specification_reference/dsp_instruments_effects/INSTRUMENTS_DSP_REFERENCE.md` | edited and restored, as above | §14.2 |
| `knowledge_files/specification_reference/dsp_instruments_effects/CPU_USE_DSP_AUDIT.md` | edited and restored, as above | §14.2 |

**The implementer's `MEMORY.md` Alkaline rule (restored; to be logged):**

```markdown
- **Alkaline (S093 P4/P6/P8, binding):** the voice is `AlkalineVoice` (548 B
  after P6) in the fifth tagged slot; Basic. Its `osc1_type` is `wav` or
  `wtb`: `wav` reads the stock `sawTable` or `alkSqrTable[octave]` selected by
  `osc1_wave` (row 0); `wtb` reads one installed wavetable frame's shared
  1,024-point int16 row. `osc1_wave` selects Saw/Sqr in `wav` and the
  catalogue table in `wtb`; `osc1_position` selects/steps the frame in `wtb`.
  The pitch is set with `osc_setTableFreq()` — **never** `osc_setFreq()` (the
  async recomputes pitch from `midiFreq` and the note each block; coarse only
  sets `osc.freq`). `OSC_SAMPLE_START` stays **6**: the Alkaline's stock
  tables are not user-sample catalogue entries. One-shot and looped samples,
  and `run`, are not offered. The two modes render through separate noinline
  functions and share the existing fused B2R table loop
  (`DiodeLadder_renderB2Osc`, 214 instructions, bit-identical, AL-W/AL-X;
  `ALK_FUSED_OSC 1`). The linked worst-case Alkaline block is 1,319
  instructions / 9 VDIV; whole voice is ≤255.2 per sample against the 261.3
  ceiling. `ALK_NET_STATES` is **6** on one balanced basis common to every Res
  point (r6) at 64 samples; any model change regenerates the tables
  (`tools/alkaline/gen_alk_tables.py`) and re-checks P3 §9.3. The type
  registry is the storage schema: designated rows in enum order, 3-character
  tokens, one `TABLES` entry per type; the add-a-type checklist is
  `INSTRUMENTS_DSP_REFERENCE.md` §9.2.
```

**The implementer's `MEMORY.md` Sample Flash section (restored; to be logged):**

```markdown
Samples and wavetables are installed from `Load:[Samples]`: walks `/samples`
(natural order, depth 3) and `/wavetables` (each subfolder = one wavetable),
erases sectors 7–11, installs, commits catalogue v3.

- **Accepted formats:** PCM WAV, 16- or 24-bit, mono or stereo. 8-bit PCM is
  refused for both samples and wavetables. Samples store as 16-bit mono;
  wavetables convert each source frame to one 1,024-point 16-bit mono row.
- **Catalogue v3 (S093 P8):** 8 KB at `0x081FE000`. 32 B header (magic
  "LXC3" programmed last), 128 × 28 B sample entries, 128 × 20 B wavetable
  entries. Each wavetable entry stores `frame_count × 2,048 B`; `frame_len`
  is 1,024 and `bits` is 16. P6/v2 catalogues read STALE and must be
  reinstalled.
  Details: `STORAGE_SRAM_MANIFEST.md` §4, `087_SESSION_HANDOFF_LOG.md` §5.
- **Install RAM:** the modal installer temporarily borrows the existing
  9,000 B `fs_list_cache_name` for the source frame (maximum 2,048 int16 / 4,096
  B), then clears and returns it. No new RAM is allocated; the browser reloads
  its index on the next entry.
```

(The rest of the section, from "Sort: natural order", was unchanged.)

### 14.2 Statements now out of date (`MEMORY.md` and `knowledge_files/`)

**`MEMORY.md`:**

| Lines | Statement now | What P8 changed |
|---|---|---|
| 139–141 (Copy/clear) | "Name cache lent for 161 B HCNAMES remap" | the name cache is also lent to the modal sample install, for one wavetable source frame (≤ 4,096 B, D8) |
| 145–151 (Names in RAM) | lists the cache's uses | the install is a new use of `fs_list_cache_name` (D8) |
| 224–240 (Alkaline rule) | `alk_wave`; "a table wave"; "`typ`/`run` are not offered"; "a switchable sample/wavetable Alkaline oscillator is a class decision" | `osc1_wave` row 0 and `osc1_type` row 28 (`wav`/`wtb`); loop 214, per block 1,319, ≤ 255.2 (§14.1 text) |
| 259–262 (Drum FM ramp twins) | "ITCM 15,240 / 16,384 B after P3" | ITCM 12,128 B after P8; the two twins' loops are 31 and 42 (§0.4) |
| 444–459 (Sample Flash) | catalogue v2; "mono 16-bit 44.1 kHz PCM WAV" | catalogue v3; formats, rows, loan (§14.1 text) |

**`knowledge_files/specification_reference/FILESYSTEM_SPEC.md`:**

| Lines | Statement now | What P8 changed |
|---|---|---|
| 472 | `fs_list_cache_name` described as the browser-index cache | also lent to the sample install (D8) |
| 858–862 | the name-buffer loan is copy/clear's | the sample install takes the same loan for its run (§4.9) |
| 1866–1867 | `init.alk`: 28 keys, 22 MORPHABLE rows (all but `alk_wave`, …) | 29 keys, 23 `[morph]` rows (`osc1_wave` is an image row) |
| 1979–1987 | Alkaline row 0 `alk_wave`, `DTYPE_ON_OFF`, tag `ONOFF_TAG_ALK_WAVE` (3) | row 0 `osc1_wave` (`MENU_WAVEFORM`); row 28 `osc1_type` (tag `ONOFF_TAG_ALK_TYPE` 3: `wav`/`wtb`) |
| 2453, 2480, 2497 | "catalogue v2" | catalogue v3 |
| 2458, 2634–2635 | "Only mono 16-bit 44.1 kHz PCM WAV files are accepted" | 16- or 24-bit, mono or stereo; 8-bit refused (D10, D11). Already inaccurate before P8, which accepted 8/16/24-bit, mono or stereo |
| 2482–2484 | "any frame length (A16)" | source frames 16..2,048 points, stored as one 1,024-point 16-bit row each; frame count = points / frame length |
| 2497–2501 | wavetables "written to flash with catalogue v2 entries"; blending uses the interpolation slots | stored as rows; Drum's blend is unchanged; the Alkaline steps frames, without a blend |

**`knowledge_files/specification_reference/STORAGE_SRAM_MANIFEST.md`:**

| Lines | Statement now | What P8 changed |
|---|---|---|
| 539–563 | catalogue v2 layout, magic `LXC2` | v3, `LXC3`; a wavetable entry's `frame_len` 1,024, `bits` 16, item = `frame_count × 2,048 B` |
| 577–600 | "Only mono 16-bit 44.1 kHz PCM WAV files are accepted"; "Wavetable limits: any frame length" | formats as above; frames resampled to rows; frame count from points |
| 802 | `fs_list_cache_name` lent to copy/clear | also lent to the sample install (≤ 4,096 B used) |

**`knowledge_files/specification_reference/dsp_instruments_effects/INSTRUMENTS_DSP_REFERENCE.md`:**

| Lines | Statement now | What P8 changed |
|---|---|---|
| 565–582 (§5.5) | 28 rows; the oscillator "stays a table wave (S093 P3 §9.5)" | 29 rows; `typ` `wav`/`wtb`; the `wtb` path (§6–§7 of this schedule) |
| 667–669 | 22 image rows; `alk_wave` is a SELECTOR | 23 image rows; `osc1_wave` is an image row; `osc1_type` is the selector |
| 675, 1460, 1476–1477, 1538 | Alkaline 1,230 per block, 252.5 per sample | per block 1,319, ≤ 255.2 (loop 214 unchanged) |
| 890 onward (wavetable waves) | the any-length read, 8- and 16-bit frames | the fixed 1,024-point 16-bit read (§5.3) |
| 1046 | install-time preparation, "catalogue v2" | v3; frames resampled to rows |
| 1071–1072 | ramp twins "47 (16-bit) / 43 (8-bit)", "33 (8-bit) / 35 (16-bit)" | 42 and 31, one loop each (§0.4) |
| 1823 | "sample flash catalogue v2" | v3 |

**`knowledge_files/specification_reference/dsp_instruments_effects/CPU_USE_DSP_AUDIT.md`:**

| Lines | Statement now | What P8 changed |
|---|---|---|
| 1014 | `wtb` blend FM ramp 86 → 77 | the twin's loop is now 42 (§0.4); the pass-versus-twin saving is to be re-derived |
| 1136–1168 (item 71) | Alkaline per block 1,230, 252.5 per sample | per block 1,319, ≤ 255.2, the `wtb` mode; a P8 item: the readers' loops (§0.4), ITCM −2,424 B, flash +5,704 B |

**Other files under `knowledge_files/`:**

| File, lines | Statement now | What P8 changed |
|---|---|---|
| `MIDI_SPEC.md` 393–394 | "Alkaline … has 28 rows (0–27)"; `alk_wave` is a selector | 29 rows (0–28); `osc1_wave` (row 0), `osc1_type` (row 28) |
| `MIDI_SPEC.md` 437 | "`alk_wave` (S093 P4): 0 `Saw`, 1 `Sqr`" | `osc1_wave`: Saw/Sqr under `wav`, the table under `wtb`; `osc1_type`: 0 `wav`, 1 `wtb` |
| `MODULE_INTERCHANGE_SPEC.md` 1119, 1669 | the name-cache borrow API is listed under copy/clear | the sample install also takes the loan |
| `COPYCLEAR_UTILITIES.md` 114, 723 | the 9 kB name buffer is lent to copy/clear | a second holder: the sample install |
| `REPOSITORY_LAYOUT.md` 141 | `SampleMemory.c/h` "catalogue v2 flags" | catalogue v3 |
| `volatile/OPEN_TESTS.md` | — | the device items of §13 |
| `log_archive/` (the 093 log) | — | a P8 entry: the spike, S1–S7, the decisions (D1′–D11, O1–O3), the frame-count bug, the scope correction |

### 14.3 Outside `MEMORY.md` and `knowledge_files/`

| File | State |
|---|---|
| `MIDI_NRPN_TABLE.md` (root) l. 32, 86, 91, 110 | not edited. It still names `alk_wave` and indices 0–27; row 0 is `osc1_wave` and row 28 `osc1_type`. It is a specification-type document, so it is listed here for the user rather than scheduled |
| `S093_P7_WAVETABLE_SAMPLES_OSC_IMPLEMENTATION.md` (header) | not edited: the "Discarded … superseded by S093_P8_IMPLEMENTATION.md" note (P8-D6) is the user's to add |
| Code comments (`filesystem.h`, `filesystem.c`, `menu.c`, "catalogue v2" → v3, the loan's second client) | done in the implementation (§17) |
| `tools/dsp_test/Makefile` `P4BASE` | moved to `baselines/s093p8` in the implementation (§17) |

---

## 15. Left out, and what each would take

| Item | State | What it would take |
|---|---|---|
| One-shot samples on the Alkaline | fit the voice (loop 215, per block 1,364), but the render call grows two mixer loops of other engines by 1 | a layout that leaves the mixer alone (for example its own `-fno-lto` unit), re-gated; or your acceptance of the +1s |
| Looped samples on the Alkaline | loop 222–227 alone; over the ceiling | a per-block saving of at least 5–8 per sample |
| Blending between frames on the Alkaline | two reads per sample | an Advanced place or the interpolation pool |
| Per-octave band-limited rows (the catalogue's reserved mip bits) | aliasing at high notes, as Drum | sample flash up to ×11 per table; no CPU change |
| The resampler's top 5 % of the band | −1.2 dB at 0.483, −5.3 dB at 0.498 of the frame's Nyquist | a longer kernel (install time) or an exact periodic (DFT) resample with a larger buffer (the name cache has room: 9,000 B) |
| 8-bit built-in ROM sounds | unchanged (O2) | — |

---

## 16. Verified in the tree for this schedule

| Fact | Where |
|---|---|
| The fused loop reads one `[1024]` int16 row, Q10.22 | `DiodeLadder.c` l. 273–313, 414–420 |
| The Saw/Sqr sync block, the async pitch tail, the trigger | `AlkalineVoice.c` l. 254–279, 184–216, 110–140 |
| `OscInfo` bind fields, `positionApplied`; 72 B | `Oscillator.h` l. 213–315 |
| `osc_userBind()`, its depth choice and its callers | `Oscillator.c` l. 1552–1651 (depth l. 1596–1607); callers `osc_setFreq()`, `osc_setType()` (l. 1459), `osc_typedTriggerPhase()` |
| `OSC_BIND_WTB8` uses | `Oscillator.h` l. 204; `Oscillator.c` l. 1616, 1647, 2399, 2422, 2445, 2467, 2490, 2583, 3692 |
| The wavetable read and frame helpers; the ten readers' calls | `Oscillator.c` l. 2687–2726; l. 2874–3645 |
| The readers' 8- and 16-bit loop copies | P6 image, `elf_looppath.py` (§0.4); the 8-bit ones hold the `ldrsb` loads |
| Catalogue layout, magic/version, range checks | `SampleMemory.h` l. 89–179; `SampleMemory.c` l. 104–124, 126–180 |
| The installer's formats, 8-bit paths, `writeFrame()` per call, the fit check | `SampleInstall.c` l. 70–106, 185–198, 343–459 |
| The walker's chunks, depth choice and install entry | `filesystem.c` l. 23845, 25378–25400, 25459–25538, 25699–25731 |
| The name-cache loan and its gate | `filesystem.c` l. 1066, 1956–2008, 29287; `filesystem.h` l. 1290–1301 |
| The Load menu's wait before the install | `menu.c` l. 1075–1091 |
| D-cache invalidation after flash writes | `sampleFlash.c` l. 179–191, 222, 272 |
| `DEV_LOGGING_IWDG` 0; IWDG at maximum period when on | `config.h` l. 200; `filesystem.c` l. 28168–28179 |
| All 16 `DTYPE_MENU` ids taken; ON/OFF tags 1–3 | `MenuText.h` l. 22–40, 160–252 |
| The tagged ON/OFF selector with a special writer | `DrumParameters.c` l. 108–130 |
| The `typ` writer, the wave domain, the classifier, `instrumentManager_osc()` for ALK | `InstrumentManager.c` l. 3881–3901, 838–857, 3987–4034, 2383 |
| Unknown keys skipped; absent keys load 0 | `FILESYSTEM_SPEC.md` l. 1821, 1928 |
| The built-in 8-bit ROM sounds | `Samples.c` l. 67; `transientTables.c` l. 63; `Oscillator.c` l. 529–554 |

---

## 18. Revision 3: Alkaline wavetable frame blending with an interpolation slot

- **Decisions:** `S093_P8_WAVETABLE_REASSESSMENT.md` §14:
  - D12: the Alkaline wavetable blends frames while it holds one of the two
    interpolation slots, as Drum, Snare, Cymbal and HiHat do;
  - D13: the slot-held voice's over-budget (271.5 against 261.3, measured on
    the prototype) is accepted for now.
- **Why:** on the device the P8 wavetables did not sound interpolated. By
  design (P8-D4): the voice stepped one whole frame per block and never
  claimed a slot.
- **Base:** the tree at `1be8f375` (P8 implemented and passed). Line numbers
  in §18.1 are that tree's.
- **Binding rules** (as §0 header, plus):
  - the claim is made in the async, never in the sync render. A claim in
    the render moved two mixer hot loops by +1 in the prototype (reassessment
    §14.1);
  - no new RAM: the frame position reuses `OscInfo.waveInterpFrac`
    (documented unused since S087 P1), and the grant flag fills the padding
    byte after `AlkalineVoice.pitchJump`;
  - no document under `knowledge_files/` or `MEMORY.md` is edited by this
    revision. Statements made out of date are listed in §18.4.

### 18.1 Change list

| # | File | Change |
|---|---|---|
| R3-1 | `Core/DSPAudio/DiodeLadder.c` | `dl_renderB2Body()` gains `rowB`, `ff0`, `ffStep` and a `fused == 2` branch (two rows at one phase, mixed by a per-sample ramp); the existing wrappers pass `0, 0.0f, 0.0f`; ADD `DiodeLadder_renderB2Osc2()`, `noinline` |
| R3-2 | `Core/DSPAudio/DiodeLadder.h` | ADD the `DiodeLadder_renderB2Osc2()` prototype with its contract |
| R3-3 | `Core/DSP/Instruments/Alkaline/AlkalineVoice.h` | ADD `uint8_t wtbSlot` after `pitchJump` (padding); MODIFY the `osc` member's comment (the bind fields under wtb) |
| R3-4 | `Core/DSPAudio/Oscillator.h` | MODIFY the `waveInterpFrac` comment (used by the Alkaline wtb) |
| R3-5 | `AlkalineVoice.c` | REPLACE `alk_wtbFrameOf()` by `alk_wtbFramePos()` (the fractional frame position); MODIFY `alk_wtbBind()` (first row; position jumps to pos), `alk_wtbTrigger()` (position jumps to pos), `alk_wtbControl()` (no stepping; the claim); REPLACE `alk_renderWtb()` (glide, blend with a slot, nearest frame without); MODIFY `Alkaline_initVoice()` and `Alkaline_setOscType()` (clear the grant and the stamp) |
| R3-6 | `tools/alkaline/control_cost.py` | ITERS: `DiodeLadder_renderB2Osc2` rank 1 = 0 (the audio loop), with the B2R guard; STUBS: `modNode_interpClaim()` |
| R3-7 | `tools/dsp_test/modules/alkaline.c` | the claim stub switchable; AL-W rewritten for the glide and the blend (with and without a slot) |
| R3-8 | `tools/dsp_test/Makefile` | the wtb break build retargeted (the row-pointer step is gone): the crossfade ramp and the one-frame glide limit |

### 18.2 Gates

| Gate | Pass |
|---|---|
| Host `make -C tools/dsp_test alkaline` | 0 failures; every break build caught |
| Host `oscillator`, `samples` | unchanged, 0 failures |
| `make clean && make all` | links; no new warning |
| Loops (`elf_looppath.py`) | `DiodeLadder_renderB2Osc` ≤ 214; `DiodeLadder_renderB2Osc2` measured (prototype 228) |
| Per block (`control_cost.py --elf`) | measured (prototype 1,391) |
| `elf_p4_check` | oscillators 77/77 exact; mixer 0 grew |
| `link_budget.py` | ITCM unchanged (12,128 B); flash headroom recorded |
| RAM | `.data` / `.bss` unchanged |

### 18.3 Working notes

- **2026-11-10, R3-1/R3-2 (ladder) done:**
  - `dl_renderB2Body()` takes `rowB`, `ff0`, `ffStep`, plus a `fused == 2`
    branch: two rows interpolated in float at the shared phase, mixed by a
    per-sample ramp `ff`;
  - `DiodeLadder_renderB2()` and `DiodeLadder_renderB2Osc()` pass
    `0, 0.0f, 0.0f`. `fused` is a constant in each wrapper, so their code is
    unchanged;
  - `DiodeLadder_renderB2Osc2()` is added `noinline` (the prototype measured
    231 inlined, 228 out of line), and its prototype and contract are in
    `DiodeLadder.h`.
- **2026-11-10, R3-3/R3-4 (headers) done:**
  - `AlkalineVoice.wtbSlot` is added after `pitchJump`, in the padding byte
    before `gateLeft`, with its contract;
  - the `osc` member's comment describes the revision-3 bind fields;
  - `OscInfo.waveInterpFrac` gets a full comment block (the Alkaline wtb
    frame position; still unused by the other instruments).
- **2026-11-10, R3-5 (`AlkalineVoice.c`) done:**
  - `alk_wtbFrameOf()` (nearest frame) is replaced by `alk_wtbFramePos()`
    (fractional, Drum's `osc_wtbFramePos()` law);
  - the bind keeps the first row and sets `waveInterpFrac` to pos's
    position;
  - the trigger sets `waveInterpFrac` to pos's position;
  - `alk_wtbControl()` drops the per-block step and claims a slot after
    `osc_setTableFreq()`, for tables of two frames or more, into
    `wtbSlot`;
  - `alk_renderWtb()` does the glide (≤ 1 frame per block, stopping at the
    next frame boundary), the blend through `DiodeLadder_renderB2Osc2()`
    with a slot, and the nearest frame without one;
  - `Alkaline_initVoice()` and `Alkaline_setOscType()` clear `wtbSlot` and
    `osc.interpStamp`. The type switch's clear matches Drum's
    `osc_setType()` (S087 A18) and was not in the prototype.
  - The code of the control and the render is the measured prototype's
    (`v8`); the comments are new.
- **2026-11-10, firmware gates (revision 3) passed:**
  - **Build:** `make clean && make all` links. The warnings are the existing
    set (newlib stubs, USB, filesystem, splash, pattern); none is in a
    changed file.
  - **Image:** text 710,416 B; flash 710,836 / 753,664 B (headroom 42,828 B;
    P8 44,556 B, so −1,728 B for the second loop copy and the render). ITCM
    12,128 B (unchanged). FXBUF 126,656 B, margin 3,776 B (unchanged).
  - **Loops (`elf_looppath.py --cond-heads --in-range`):**
    `DiodeLadder_renderB2Osc` **213**, 3 VDIV (P8: 214);
    `DiodeLadder_renderB2Osc2` **228**, 3 VDIV. `alk_renderWtb` and the sync
    block have no loop of their own.
  - **Per block (`control_cost.py --elf --iters DiodeLadder_renderB2Osc2:1=0`):**
    async 957 + sync block 407 + dispatch 14 = **1,378** (43.1 per sample),
    9 VDIV. The prototype was 1,391; the bind no longer computes a frame
    row (`alk_wtbBind` 46 → 33).
  - **Whole voice:**
    - no slot: 213 + 43.1 = **256.1** (≤ 261.3);
    - slot held: 228 + 43.1 = **271.1**: over 261.3 by 9.8, the over-budget
      accepted in D13. The +15 is charged to the pool, whose per-slot
      ceiling is the `itp` fade's 45.
  - **`elf_p4_check`:** oscillators 77/77 exact; mixer 32 hot loops equal,
    0 shrank, 0 grew. Run on a copy of the ELF at a path without spaces
    (the Makefile does not quote `FW_ELF`).
  - **RAM:** every `.data` / `.bss` symbol has the same size as on the P8
    image (`nm -S` diff empty). `.dtcmz` is 3,884 B against 3,880 B: the same
    20 objects (3,879 B) in a different LTO order, with 5 B of alignment
    gaps in place of 1 (DTCM statics 4,392 → 4,396 B). No object was added.
- **2026-11-10, R3-6 (`control_cost.py`) done:**
  - ITERS gives `DiodeLadder_renderB2Osc2` rank 1 weight 0 (the audio
    loop), so `--elf` no longer needs an `--iters` override;
  - the B2R guard (3 VDIV, length ≥ 200) covers it;
  - STUBS add a granting `modNode_interpClaim()` for the standalone link.
  - **Results:** `--elf` gives 1,378 per block; the standalone run (no LTO,
    claim stub) gives 1,425 (async 1,001 + sync 410 + 14).
- **2026-11-10, R3-7/R3-8 (bench) done:**
  - **Claim stub:** `modNode_interpClaim()` grants by `w_grant` and counts
    its calls.
  - **AL-W, rewritten:**
    - W1/W2: the voice against a reference over 3,000 blocks, three runs
      (no slot, slot, a slot that comes and goes every 50 blocks). Without a
      slot the reference is the table read of the nearest frame + the plain
      B2R; with one, `DiodeLadder_renderB2Osc2()` on the restated pair and
      ramp. The frame position is checked exactly against the restated
      glide law;
    - W5: the two-row loop against the frames mixed in float, truncated and
      fed to the plain B2R, plus a check that frame B is really read;
    - W3: an install rebinds (first row, position = pos);
    - W4: an empty catalogue is silent and never claims.
  - **AL-TY:** a type change clears `interpStamp` and `wtbSlot`.
  - **Makefile:** the old row-step break is replaced by two:
    `alkaline_break_wtb` (the blend's ramp held at 0) and
    `alkaline_break_glide` (the frame boundary one frame further on). The
    index comment is updated.
- **2026-11-10, host gates (revision 3) passed:**
  - `alkaline`: every law 0 failures, with these results:
    - AL-W no slot / slot / slot on/off: 0 samples, 0 phases, 0 positions
      differ (0 / 3,000 / 1,500 blended blocks; 2,840 blocks between frames;
      RMS 1,739 / 1,412 / 1,583 LSB);
    - two-row loop: at most 1 LSB from the float-mix reference (peak
      5,270 LSB), 3,977 LSB from frame A alone at mix ≈ 1;
    - empty catalogue: peak 0, 0 claims.
  - `alkaline_break`, `alkaline_break_wtb` and `alkaline_break_glide` are all
    caught.
  - `oscillator`, `samples` and `instrument_manager`: 0 failures, every break
    caught. `special_tags`: 200 rows, 0 mismatches.
  - **Fixed along the way:** the first W5 run held the currents of the
    note's first block (VEG closed), so the loop was compared on silence
    (peak 0) and passed vacuously. It now holds the currents after 12 blocks
    and fails on silence.
  - **Tooling note:** the generated `tools/dsp_test/build/gen/Oscillator.host.c`
    was older than the P8 `Oscillator.c` and still named `OSC_BIND_WTB8`,
    and its make rule did not rebuild it. Deleting the generated pair fixed
    it. Delete `build/gen/Oscillator.host.*` when an `Oscillator.c` edit
    does not show up in a host build.
- **2026-11-10, revision 3 complete in the tree.** Device checks are in
  §18.5. No commit (commits belong to the user).

### 18.4 Statements made out of date (for the user to log; not edited here)

- `MEMORY.md`: the Alkaline wavetable rule (one frame per block, no slot).
- `knowledge_files/specification_reference/`:
  - `dsp_instruments_effects/INSTRUMENTS_DSP_REFERENCE.md` §5.5 (Alkaline
    wtb: stepping) and the §8.2 S093 P8 block's "decided, not yet in the
    tree" (now in the tree, 256.1 / 271.1 per sample);
  - `dsp_instruments_effects/CPU_USE_DSP_AUDIT.md` item 78 (the same: now in
    the tree, per block 1,378);
  - `OSC_INTERP_AUDIT.md` "Slot budget": the Alkaline is now a slot user (in
    the tree);
  - `STORAGE_SRAM_MANIFEST.md`: `OscInfo.waveInterpFrac` is used again
    (Alkaline wtb only); `AlkalineVoice.wtbSlot` sits in former padding;
    `.dtcmz` is 3,884 B (LTO layout, no new object).
- `S093_P8_WAVETABLE_REASSESSMENT.md` §14.1: the prototype figures (per
  block 1,391; 256.5 / 271.5) are superseded by the tree's (1,378;
  256.1 / 271.1).

### 18.5 Device checks (user)

| Check | Pass |
|---|---|
| A `pos` sweep on a wavetable (knob, then an LFO) | smooth morph, no stepping or zipper |
| The global oscillator interpolation setting off | the nearest frame (stepping), as Drum |
| Two Drum wavetables holding both slots, then the Alkaline | the Alkaline steps (no slot) until one is released |
| Alkaline wtb with no table installed | silence, no claim |
| `cpu` widget, worst-case Scene with the Alkaline holding a slot | ≤ 100 %, 0 underruns (D13 accepted the per-voice over-budget) |
- **2026-11-10, card image:** the clean build removed the tracked
  `build/LXRV2_lxr02.img` (`make clean` deletes `build/`). It was rebuilt
  from the revision-3 image with `make img` (CRC-stamped, 710,836 B
  payload), so the tracked image matches the source. Copy it to the SD card
  root for the §18.5 checks.
