# S073 Session Startup: carry-over from Session 072

Read this before starting Session 073. The session order is:

1. `S073_FLASH_EXPANSION.md` (bootloader study, probe tests, and the
   sector-7 sample floor if Gate B passes);
2. `S073_CPU_USE_DSP_REDUCTION_REFACTOR.md`.

This file holds the Session 072 facts that those two plans depend on. The
`S072_ST*_IMPLEMENTATION.md` documents they cite may be deleted; the relevant
content is copied here and into
`knowledge_files/log_archive/072_SESSION_HANDOFF_LOG.md`.

---

## 1. Tree and build state at the start of S073

| Item | Value |
|---|---|
| Branch | `dev-ph5-effects` |
| HEAD | `58569ae` (S072 Steps 1–8). Steps 9, 10 and 11 are uncommitted in the working tree (you manage commits). |
| Final S072 link | `text=483,024`, `data=416`, `bss=426,336` |
| Flash payload | **483,440 B** of the 491,520 B window `0x08008000–0x0807FFFF` |
| Flash headroom | **8,080 B** |
| ITCM | 3,768 / 16,384 B |
| DTCM statics | 4,448 B |
| FXBUF arena | 126,624 B at `0x20001160`; margin 3,744 B over the 120 KiB ASSERT |
| `Reset_Handler` | `0x08053AB0` (sector 5). The flash plan's §1 table says `0x0805392C` from the ST9 tree; re-check with `nm` at test time. |
| Config | `DEV_MODE_DIAGNOSTIC 0`, `DEV_MODE_LOGGING 1`, `MEMTEST_ENABLED 1`, `ENABLE_EUKLID_PAGE 0`, `DEV_FXBUF_FORCE_VOICE_UNITS 0u`, `DEV_EFFECT_FORCE_TYPE 0u` |

**Numbers in the S073 plans that are now stale:**

- Both plans quote the ST9 baseline (`text=482,632`, payload 483,048 B,
  8,472 B free). Use the table above.
- The flash plan's T4 row ("Known-good production image, 483,048") should use
  the payload of whatever image is kept as known-good.
- S072 Steps 10–11 changed no DSP code, so the CPU plan's cost model and
  stage list are unaffected.

**Build commands:**

- **Use `make all`, not bare `make`.** In an incremental tree, `-include
  $(OBJS:.o=.d)` (Makefile ~161) precedes `all:`, so bare `make` builds only
  `build/main.o` and stops. This is logged debt, not fixed.
- For the flash plan's probe builds with command-line defines, use
  `make clean && make all FLASH_PROBE=1 …` and then `make img`. The plan
  already requires `make clean`, because `.d` files do not track `-D`
  values.
- Link budget: `python3 tools/link_budget.py arm-none-eabi-nm build/lxr02.elf`.
  - It hard-codes `FLASH_LIMIT = 0x08080000` (`tools/link_budget.py:23`);
    Phase C must change it.
  - `LINK_BUDGET_WARN_FLASH` (default 16384) sets the warning threshold.
- **Build-host quirk:** `lto1: internal compiler error: Bus error: 10` has
  twice left an empty `build/lxr02.elf`. It likely happens when two builds
  share `build/`. Delete the ELF and relink. Never run two builds at once.

---

## 2. Flash facts from S072 Step 1 (the SCOPING §5.5 investigation)

This replaces `S072_ST1_IMPLEMENTATION.md` §21, which the flash plan cites.

1. **Oversize links fail by design.** `STM32F765VIHx_FLASH.ld:201–204`
   asserts `_etext <= 0x08080000` and
   `_eflash_load <= ORIGIN(FLASH) + LENGTH(FLASH)` ("Application load image
   overlaps sample flash region"). No oversize `.bin` can be produced.
2. **The image end is `_eflash_load = LOADADDR(.dtcm) + SIZEOF(.dtcm)`**
   (`.ld:138`). The flash image is `.text`, then the `.itcm`, `.data` and
   `.dtcm` load images. `lxr02.bin` equals `_eflash_load − 0x08008000`,
   because `objcopy -O binary` emits loadable sections only.
   - This is why the DTCM arena `.dtcm_fxbuf` is **NOLOAD**. It never enters
     the image and startup never touches it.
   - A probe section in sector 6 must be a loadable section *outside*
     `FLASH`, as the plan's `PROBE` region is, so the normal gate
     `lxr02.bin == _eflash_load − 0x08008000` still holds for
     `FLASH_PROBE=0`.
3. **The packer has no size check.** `tools/build_lxrv2_img.py` writes the
   16-byte `LXRV2IMG` header (magic, payload size, **8-bit** additive
   checksum) followed by the payload.
4. **Bootloader behaviour on a payload over `0x78000` bytes is unknown**
   (closed sector-0 binary). This is the one real unknown the flash plan
   tests.
5. **The sample floor is sector 6**, hard-coded in three places:
   - `SampleMemory.h` `SAMPLE_ROM_START_ADDRESS 0x08080000`;
   - `sampleFlash.c:170` and `:217` (`sector < 6u` guards);
   - `memtest.c:136` `ERASE_SECTOR_FLOOR 6`.
6. **Largest flash consumers** (126,812 B of constant tables, about 28 % of
   the image):
   - `crashSample` 32,768 B;
   - `transientData` 26,460 B;
   - `sawTable`, `triTable`, `recTable` 22,528 B each.

   Largest code objects: `main` 9,024 B, `filesystem_tick` 7,896 B,
   `menu_repaintGeneric` 7,864 B, and the Scene/Bank load/save ticks
   6.5–7.6 KB each. The S072 additions are listed in §4.
7. **Growth paths as ranked in S072** (the flash plan's §8 fallbacks):

   | Rank | Path | Gain | Note |
   |---|---|---|---|
   | 1 | `-Os` for cold control modules via per-file rules | several KiB | the same mechanism as the existing `-Ofast` DSP rule |
   | 2 | Large constant tables as installed data in the sample region | up to about 124 KiB | needs an install path |
   | 3 | Sample floor → sector 7 | +256 KiB | the S073 flash plan; needs item 4 answered |
   | 4 | Remove dead legacy code | small | |

   Additional candidates noted in S072 ST7:
   - the compiled-in but unreachable Euklid generator and SOM page code
     (`ENABLE_EUKLID_PAGE 0` hides only the UI);
   - unused `filesystem.c` static helpers (probably already dropped by LTO).
8. **Never run: the local oversize-link check.** Temporarily add a 40 KiB
   `const` array, confirm the link fails with the load-image ASSERT, then
   revert. It costs one minute and is a sensible preliminary to Phase C's
   growth drill.

**Flash rate during Phase 5, for scale:**

| Change | Size |
|---|---|
| Start of S072 → end | 34,356 B → 8,080 B |
| Largest single steps | ST4 `SVF_calcBlockZDFFloat` 2,772 B; ST6 storage ~4.9 KB; ST7 page ~5.4 KB; ST9 ~3.9 KB |
| LTO re-partitioning | shifted about ±1.4 KB with no source change (ST1) |

---

## 3. What the flash plan must keep working

S073 test images (T0, probe builds, Phase C) must boot the full product. The
Phase 5 Effect system is in them and is **not yet hardware-accepted** for
Steps 6–10.

**Keep the known-good image.** Build it from the current tree (the S072
final state) before Phase A. Keep it off-card and on a second card, as the
plan says.

**Card state.**

- The current firmware uses AutoSave **HCPR v3** and **161-row** `.hcnames`.
  An older card is rejected until you delete the root `.hcnames`,
  `.hcnamtmp`, `.hcprms1` and `.hcprms2` once.
- **Samples:** the flash plan erases sectors 6–11. Scenes that reference
  sample waveforms play silence until you reinstall with Load:[Samples].
  Nothing in Phase 5 stores sample data.

**Where report screens go.**

- The plan puts its memtest report screens "right after `dsp_init()`, next
  to the FxBf diagnostic". In `main.c`, `dsp_init()` is at ~535 and
  `boot_showFxBufDiagnostic()` at ~538.
- `dsp_init()` also runs `fxbuf_init()` and `effects_init()`, so screens
  placed after it see a fully initialised Effect system.

**Phase C sample floor → sector 7.** Nothing in the Effect system touches
sample flash. The DTCM arena is RAM. The Effect registry and descriptor
tables are ordinary `.rodata`.

**RAM policy.**

- Phase A/B tooling must add no RAM; the flash plan already states this.
- The DTCM arena is elastic: any new `INDTCM`/`INDTCMZ` static shrinks it.
  The linker ASSERT keeps it ≥ 120 KiB (margin now 3,744 B).
- **Do not place probe or verification state in DTCM.**

**Regression pass after Phase C.**

- Include the Phase 5 checks: Scene/Bank Load with a `.fx`, the FX send and
  return, AutoSave restore of the Effect.
- The compact list is in `072_SESSION_HANDOFF_LOG.md` §11 (B, C).

---

## 4. Facts for the CPU refactor plan

**The plan's anchors still match the S072 final tree:**

| Anchor | Location |
|---|---|
| `SVF_calcBlockZDF()` | `ResonantFilter.c:154` |
| `SVF_calcBlockZDFFloat()` | ~332 (it has a `#error` guard: it mirrors only the non-shaper configuration) |
| `instrumentManager_osc()` | `InstrumentManager.c:1820` |
| `instrumentManager_writeSpecialRuntime()` | `InstrumentManager.c:2917` |
| `mixer_addVoiceInt16ToOutput()` | `mixer.c:409` |
| `mixer_addVoiceToFxBus()` | `mixer.c:543` |
| `mixer_calcNextSampleBlock()` | `mixer.c:714` (per-slot loop ~810–835, `mixer_faderGains` at 823) |
| `freqToTableIndex()` | `Oscillator.c:63` |

Re-verify before editing; S073's own changes move them.

### 4.1 Phase 5 DSP path: what the CPU plan touches

**Render order per 32-frame block** (inside `audio_check_and_render()`):

1. `voiceControl_processPending()` → `seq_drainPendingAutomation()`.
   - The drain applies voice, Scene and Effect automation, and handles
     Effect step markers and the reset latch.
   - It must stay after `voiceControl_processPending()`: the S065 ordering
     invariant.
2. `mixer_calcNextSampleBlock()`:
   - `instrumentManager_dispatchRuntimeLfos()` (LFO → Effect entries via
     `effects_setLfoContribution()`);
   - filter recalc and async;
   - `effects_service()`;
   - the per-slot render, decimate, send tap and dry tap;
   - FX convert → `effects_process()` → return.
   - Profiler stage 4 in the CPU plan is `effects_service()`; stage 12 is the
     FX convert, process and return.

**`effects_service()` cost.**

- It rescans every descriptor of the active type each block: about 10–15
  cycles per row, about 100 cycles for `flt` (7 rows).
- It adds `effects_lfoResolve()` (a 12-entry scan) for `fxm` every block and
  for each LFO-targeted row.
- `write_param` runs only on change. For `flt`, `stereoFilter_writeParam()`
  recomputes SVF coefficients (`valueShaperF2F`, `SVF_setReso`,
  `SVF_setDrive`) when freq, reso or drive change. An LFO on `frq`
  therefore recomputes coefficients every block.

**`off` costs nothing on the bus.** When the live type has no I/O flags, the
bus is not cleared, summed, converted or processed. The send-gain ramp still
updates every block, and **must keep doing so** (re-enabling an Effect
depends on it).

**CPU plan Step 1 (batch the ZDF divisions) applies to both variants.**
`SVF_calcBlockZDFFloat()` is the float-I/O twin used only by Effects:

- no `__SSAT`;
- float-suffixed literals;
- compiled at `-Ofast` in `DSP_SRCS` via `StereoFilterEffect.c`, while the
  function itself is in `ResonantFilter.c`.

The float stereo filter runs **two** SVF instances (L and R) with linked
coefficients: `stereoFilter_linkCoefficients()` copies `f`, `g`, `q` and
`drive` from left to right. Mirror any state or coefficient layout change
there.

**CPU plan Step 5 (dry + send in one pass): contract to preserve.**

- The send taps the **decimated, pre-volume** block.
- Dry gain = `vol × F_mix`; send gain = `fxSend/127 × F_send` (fader modes
  in `mixer.h` `MIXER_FADER_*`).
- The send converts `float × 256` directly to `sample_mx_t`, *without* the
  dry path's int16 truncation. The two expressions differ on purpose.
- **Stereo-input types** get the voice panned into L and R with
  `squareRootLut`. **Mono-input types** get it unpanned in L only.
- `mixer_send_last_gain[slot]` is updated every block, even when the send is
  skipped. The skip happens when `!fx_active` or when both gains are 0.
- The bus accumulates with `bufferTool_satAdd32()`.

**CPU plan Step 2 (string-free special writers).**

- `instrumentManager_writeSpecialRuntime()` is unchanged by S072.
- S072 did add `INSTALLED_MOD_TARGET_EFFECT` and the shared static
  `instrumentManager_lfoDirectionDepth()` (the S071 voice-Morph encoder,
  extracted for voice Morph, `fxm` and Effect rows).
- Effect parameters never go through `writeSpecialRuntime()`. They are
  applied by `effects_service()` → `write_param`.

**CPU plan Step 0 (stress Scene).** It needs `flt` with every send at 127:

- select it on the Effect page (SHIFT+PERF → `typ` → click in, turn to
  `StFilter`, click out); or
- drop in this hand-written `.fx` (stem ≤ 8 characters):

  ```
  format=helicase.effect
  version=2
  type=flt

  [params]
  effect_audio_out=0
  effect_level=127
  effect_pan=64
  filter_freq=40
  filter_reso=90
  filter_drive=0
  filter_type=0
  ```

  For worst-case CPU, raise `filter_reso`/`filter_drive` and add LFOs
  targeting `fx` `frq` (LFO DstVoice `fx`).

**RAM decisions still open** (from the CPU plan):

- D1: the Step 0 profiler, 120 B SRAM1, diagnostic builds only;
- D4: a 4 B DTCM PRNG. It would shrink the FX arena by 4 B plus alignment,
  still above the ASSERT margin.

Both need your acknowledgement under the `MEMORY.md` RAM policy.

**Flash for the CPU plan.** Worst case about +2 KB against 8,080 B free. It
fits even without the flash expansion, but it leaves about 6 KB.

**Regression for every CPU step.**

- Include the Phase 5 FX checks: send/return in all fader modes, stereo
  panning of sends, `flt` at high reso/drive, and Scene switch to `off`.
- The listening A/B should include a Scene with `flt` active, because Steps 1
  and 5 change the FX path.

---

## 5. Phase 5 items not yet hardware-verified

These are relevant to S073 test sessions. Run them on the known-good image
before or alongside S073, if time allows. Full procedures:
`072_SESSION_HANDOFF_LOG.md` §11.

- **B. Storage fixtures:**
  - `.fx` legacy, v2, missing, malformed, and a partial Bank;
  - Scene/Bank save round trip;
  - reboot Case 1/2;
  - `none.fx` blank-name save.
- **C. FX bus:** send, the three fader modes, pre-volume tap, pan, FX_SEND
  automation, jack fallback, headroom, CPU.
- **D. Effect page walk-through**, including the `typ` + SELECT interruption
  regression.
- **E. FX sequencer remainder:** mode × length × scale, `sel` persistence,
  Morph lane, A17 alignment, persistence, CPU.
- **F. Automation and LFO:** end rule, fallback, two tracks, probability
  hold, reset, `fxm`, LFO `fx`/`fxm`, the "together" test, rebind, Kit round
  trip.
- **G. Gate and fan-out:** mismatch rejection, fan-out, type fan-out,
  directional re-validation, load and boot repair, VOICE regression.

---

## 6. Small carried debt (fix only if you choose; none blocks S073)

These are listed in `SCOPING_TARGETS.md` → "Session 072 carried debt".

- **The Makefile default goal** (`.DEFAULT_GOAL := all`) and the echoed
  link-budget recipe comments (`@#`). S073 edits the Makefile anyway
  (`FLASH_PROBE`), so this would be a natural moment. It is still your
  decision, because it is outside the S073 plan scope.
- **`fxbuf_init()` handoff-reset order** (diagnostic builds only).
- **`modNode_waveInterpGeneration`** initializer (`INCCMZ` discards `= 1u`).
- **The linker comment** "Stack lives at top of SRAM1 (0x20080000)".
  `0x20080000` is the top of **SRAM2**. The flash plan edits the linker
  header comment in Phase C, so this is the same place.
- **The `mixer.c` ~739 duplicated comment line.** The CPU plan's Step 5 edits
  that function.
- **FX return ramp not reset while `off`**: a design note, no action.
