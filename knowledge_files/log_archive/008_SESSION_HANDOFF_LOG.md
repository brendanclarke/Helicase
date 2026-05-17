# Session Handoff Template

## How to start a new session

Paste the following at the start of each conversation, filling in the bracketed fields:

---

**Project**: LXR-02 firmware port (STM32F765VIH6)  
**Session goal**: [e.g. "Port the sequencer engine from the original LXR AVR/STM32 source"]  
**Last session summary**: [paste the "End of session" block from the previous handoff, or "first session after hardware testing"]  
**Current tarball**: [confirm the tarball from the project files is the current source, or note if you have local changes]  
**Constraints today**: [e.g. "keep changes to sequencer files only", "don't touch USB", "15 minutes available"]

Key files to be aware of:
- Original LXR source is at `/tmp/LXR-master/` on the server (AVR: `front/LxrAvr/`, STM32F4: `mainboard/LxrStm32/src/`)
- Current port lives in the working tarball, extracted to `/home/claude/lxr02/`
- Knowledge files: HARDWARE_MAP.md, AVR_TO_F765_MIGRATION.md, FIRMWARE_STATE.md, ENHANCED_FEATURES.md

---

## End of session block

```
DATE: 2026-05-04 (refactor session)
SESSION GOAL: Fix audio buffer pipeline glitching — alternating 192-sample windows of phase-offset sine visible in Audacity waveform. Achieve clean audio output through mixer.c with all DSP voices enabled.
COMPLETED: Root cause diagnosed and fixed — SPSC queue scalar ready_slot replaced with proper 2-entry head/tail queue, removing shared RMW counter (ready_count). ISR signalling corrected to fire on both HT and TC. Pipeline verified clean via systematic sineBufferTest isolation. bCurrentSampleValid replaced with audioCodec_queueFreeSlots() pattern. All DSP voices (DrumVoice x3, Snare, Cymbal, HiHat) brought up cleanly. VLA stack corruption fixed in Snare and Cymbal. RNG init fixed (wrong AHB2ENR address, RMW replaced with direct write). GetRngValue() return type fixed (int16_t with full 32-bit cast). LFO noise waveform range fixed (0..1 float normalization). Compiler optimized to -O2 with explicit FPU enable. 78 startup underruns then stable thereafter.
VERIFIED ON HARDWARE: Cymbal sounding clean. 78 startup underruns then stable — fixed count confirms startup transient only, not a steady-state problem.

CHANGES THIS SESSION:
- AudioCodecManager.c: ready_slot scalar replaced with 2-entry SPSC queue (ready_queue[2], ready_head, ready_tail). ready_count removed entirely — fullness/emptiness derived from head/tail only (no shared RMW variable). audioCodec_queueFreeSlots() added. audioCodec_packHalf() exported. bCurrentSampleValid cleared in pack_audio_half() and set in commitRenderBuffer(). DMA1_Stream4 ISR calls pack_audio_half() on both HT and TC. DMA1_Stream7 ISR clears flags only (slave, no pack). CodecInit() updated — no ready_count reset.
- AudioCodecManager.h: audioCodec_queueFreeSlots() declared. audioCodec_packHalf() declared. ready_count extern removed.
- main.c: render loop changed from if(bCurrentSampleValid != SAMPLE_VALID) to while(audioCodec_queueFreeSlots() > 0) calcNextSampleBlock(). DSP pre-fill block removed. bCurrentSampleValid flag machinery retained but now driven correctly by CodecManager internals.
- sineBufferTest.c/h: created as isolated pipeline test harness during diagnosis; left aside after pipeline confirmed working (not in production build).
- DSPAudio/random.c: RCC_AHB2ENR address corrected (was 0x40023830, correct is 0x40023834 — was aliasing RCC_AHB1ENR). RNG_CR init changed from |= to direct write (RNG_CR = RNG_CR_RNGEN) to avoid RMW on unpowered peripheral. GetRngValue() return type changed to int16_t, returns (int16_t)RNG_DR — full 16-bit cast, no masking needed. initRng() retains clock enable and direct RNG_CR write.
- DSPAudio/random.h: GetRngValue() signature updated to return int16_t.
- DSPAudio/Oscillator.c: calcNoiseBlock() assignment osc->output = (int16_t)GetRngValue() — explicit cast required at all call sites despite matching types, to prevent compiler promotion through int at -O1/-O2.
- DSPAudio/lfo.c: LFO_NOISE case fixed — lfo->rnd = (float)(GetRngValue() & 0x7FFF) / 32767.0f. Previous code divided by 0xffffffff (~4.3B) producing near-zero float that corrupted modulation.
- DSPAudio/Snare.c: VLA int16_t transBuf[size] changed to static int16_t transBuf[OUTPUT_DMA_SIZE]. Stack corruption was corrupting all drum voices simultaneously when Snare was enabled.
- DSPAudio/CymbalVoice.c: VLA int16_t mod[size] and int16_t mod2[size] changed to static int16_t mod[OUTPUT_DMA_SIZE] and static int16_t mod2[OUTPUT_DMA_SIZE].
- clocks.c: FPU coprocessors CP10/CP11 enabled at top of sysclk_init() via write to CPACR (0xE000ED88). Previously relying on bootloader to enable FPU before handoff.
- Makefile: Optimization changed from -O1 to -O2. Significant DSP headroom increase — SVF_calcBlockZDF, tanhXdX, softClipTwo, fastTanh calls now inline rather than incurring full function call overhead per sample.
- audioTest.c: DMA1_Stream7_IRQHandler stub added (clears CTCIF7 and CHTIF7, no pack — Stream 4 is master). Required before audioTest_init() can be called because dma_init() enables Stream 7 HT+TC interrupts and the default handler is an infinite loop.

KNOWN ISSUES INTRODUCED: None.
KNOWN ISSUES RESOLVED: Audio buffer glitching — alternating 192-sample windows of phase-offset output (root cause: ready_slot scalar overwritten by second render before ISR consumed first slot). Stack corruption from VLAs in Snare and Cymbal. RNG peripheral not actually enabled due to wrong AHB2ENR address. RNG init RMW on unpowered peripheral causing AHB bus stall → underruns. GetRngValue() corrupt values corrupting all voices via DSP float arithmetic. LFO noise waveform outputting near-zero due to wrong divisor. FPU relying on bootloader enable instead of being owned by application. DSP load exceeding render budget at -O1.

NEXT SESSION RECOMMENDED GOAL: buttonHandler/ledHandler audit — compare processPress() case-by-case against original AVR buttonHandler_buttonPressed(). Button numbering is reversed from original (BUT_MODE1=31 vs original BUT_MODE1=35). LED index mapping needs verifying against shift-register chain order. Then wire sequencer.
BLOCKERS: None.

CRITICAL REMINDERS FOR NEXT SESSION:
- ALWAYS extract a fresh tarball and verify the working directory before writing any code
- The current known-good base is lxr02_new_refactored.zip (this session's output)
- GetRngValue() returns int16_t — cast explicitly at every assignment site ((int16_t)GetRngValue()). Without the explicit cast the compiler may promote through int and produce garbage values at -O2.
- LFO noise waveform: lfo->rnd must be normalized to 0..1 float — divide by 32767.0f, not 0xffffffff.
- VLAs are forbidden in DSP voice files — any int16_t buf[size] with a runtime size parameter must be static int16_t buf[OUTPUT_DMA_SIZE]. Silent stack corruption is the failure mode.
- RNG_CR must be initialized with a direct write (RNG_CR = RNG_CR_RNGEN), not RMW (|=). RMW on an unpowered peripheral causes AHB bus stall → audio underruns.
- RCC_AHB2ENR is at 0x40023834, NOT 0x40023830. 0x40023830 is RCC_AHB1ENR. Wrong address silently enables BKPSRAMEN instead of RNG clock.
- FPU must be explicitly enabled in sysclk_init() — do not rely on bootloader enable surviving the handoff.
- Audio render loop: while(audioCodec_queueFreeSlots() > 0) calcNextSampleBlock() — NOT if(bCurrentSampleValid != SAMPLE_VALID). The queue pattern is proven correct and non-racy.
- audioCodec_queueFreeSlots() is safe to call from main loop — both reads (ready_head, ready_tail) are single-byte LDRB, atomic on Cortex-M7, no RMW.
- LCD ring is head/tail-only SPSC (NO lcd_q_count). Do not reintroduce a shared count variable.
- DMA1_Stream7_IRQHandler must exist before audioTest_init() is called — dma_init() enables Stream 7 HT+TC interrupts. Missing handler → default handler → infinite loop.
- Stream 4 is the refill master. Stream 7 ISR clears flags only — no pack.
- DTCM is NOT DMA-accessible. DMA buffers must stay in SRAM1.
- Compiler must be -O2. DSP voices (SVF, tanh, softClip) are float-heavy and do not meet timing budget at -O1.
- 78 startup underruns is expected and normal — these happen before the main loop is running. Any underrun count that grows after boot indicates a real problem.
- SD card: bit-bang SPI on PC12/PD2/PC8/PD0. No SPI1 contention.
- EXTI_IMR = 0 must remain as the very first operation in main(), before sysclk_init()
- Do NOT add pull-down to PD4 or PD5
- lcd_init() before lcd_tim7_init()
- menu_init() calls memset on parameter_values — do not also memset in main()
- TIM7_SR defined locally in timebase.c at 0x40001410 — do not remove
- PE13/PE14: AF1 (TIM1_CH3/CH4), external 10kΩ pull-ups, no internal pull-up
- Encoder: Dannegger, last = new & 3, round-toward-zero divide in encode_read4(), read1/read2 permanently removed
- while → if in buttonHandler_processEvents() — intentional, do not revert
- DO NOT ATTEMPT TIM7 idle gating or broad repaint coalescing — confirmed broken in Session 6
```

---

## Session 8 — 2026-05-04 (Audio Buffer Pipeline Fix + DSP Voice Bring-up)

**Goal**: Diagnose and fix audio buffer glitching. Bring up all DSP voices cleanly through mixer.c.

**Working tarball at start**: `lxr02_seq_dsp_prep.tar.gz`  
**Working tarball at end**: `lxr02_new_refactored.zip`

---

### Part 1 — Symptom identification

User observed: every second 192-sample window in the output waveform was a phase-shifted version of the sine, alternating perfectly. Scaling OUTPUT_DMA_SIZE from 48 to 96 doubled the window size exactly, confirming the bug was in the buffer pipeline, not the DSP.

The waveform showed two sines of the same frequency, ~90° phase apart, alternating every 96 samples (one DMA half-period). This is consistent with the two slots of the ping-pong buffer being played back in alternating order with the oscillator's phase state having advanced by ~90° between renders.

---

### Part 2 — Root cause diagnosis (long investigation path)

Several ISR signalling strategies were tried without effect:
- Moving `bCurrentSampleValid = 0` from inside `pack_audio_half()` to TC only
- Moving it to both HT and TC
- Pre-filling the queue before DMA starts
- Using `while(ready_count < 2)` in the main loop (caused UI freeze — infinite spin)
- Using `while(ready_count < 2 && renders < 2)` (broke with irregular glitch intervals)

At that point the user correctly identified the underlying cause: **`ready_count` is a shared volatile variable incremented in the main loop (`commitRenderBuffer`) and decremented in the ISR (`pack_audio_half`). Neither `++` nor `--` is atomic on Cortex-M7 — they are read-modify-write, and an ISR firing between load and store corrupts the count.** This is the same class of bug that was fixed in the LCD queue (Session 6 — `lcd_q_count` race).

Diagnostic: `R:461` renders per 2 seconds vs expected `919` confirmed renders were firing once per full DMA cycle, not once per half. `U:` underruns matched `R:` exactly — every single ISR call saw an empty queue. The scalar `ready_slot` was being overwritten by the second render before the ISR could consume the first slot.

**Actual root cause**: `ready_slot` was a scalar, not a queue. When both HT and TC fired before the main loop ran, the second render overwrote `ready_slot`, losing the first render. The ISR always read the most recently committed slot, so slots alternated but were never consumed in FIFO order.

---

### Part 3 — Fix: SPSC head/tail queue, no shared counter

`ready_slot` replaced with a 2-entry SPSC queue. `ready_count` removed entirely — fullness and emptiness derived from head/tail only, matching the pattern used for the LCD queue fix in Session 6.

**Queue invariants:**
- `ready_head` written by ISR only
- `ready_tail` written by main loop only
- Empty: `ready_head == ready_tail` → 2 free slots
- Full: `ready_head == (ready_tail ^ 1u)` → 0 free slots
- Otherwise: 1 free slot
- Both are single-byte loads/stores → atomic on Cortex-M7, no RMW, no race

```c
static volatile uint8_t ready_queue[2] = {0, 0};
static volatile uint8_t ready_head = 0;  /* ISR reads/advances */
static volatile uint8_t ready_tail = 0;  /* main loop writes/advances */

/* commitRenderBuffer — main loop only */
void audioCodec_commitRenderBuffer(void) {
    if (ready_head != (ready_tail ^ 1u)) {   /* not full */
        ready_queue[ready_tail] = render_slot;
        ready_tail ^= 1u;
        render_slot ^= 1u;
    }
    audioCodec_renderCount++;
    bCurrentSampleValid = SAMPLE_VALID;
}

/* pack_audio_half — ISR only */
uint8_t slot;
if (ready_head != ready_tail) {   /* not empty */
    slot = ready_queue[ready_head];
    ready_head ^= 1u;
} else {
    audioCodec_underrunCount++;
    slot = last_played_slot;
}
```

**Main loop render pattern:**
```c
/* Render exactly as many blocks as the queue can accept */
uint8_t free = audioCodec_queueFreeSlots();
while (free--) calcNextSampleBlock();
```

`audioCodec_queueFreeSlots()` is safe to call from main loop — reads `ready_head` and `ready_tail` each as single LDRB. No shared RMW anywhere in the render path.

`bCurrentSampleValid` is retained in the codebase for compatibility but is no longer the primary render trigger — `queueFreeSlots()` supersedes it.

---

### Part 4 — Pipeline isolation (sineBufferTest)

When the fix did not immediately resolve the problem in `AudioCodecManager.c`, a systematic isolation approach was used: a self-contained `sineBufferTest.c` was written with its own queue, buffers, ISR, hardware init, and sine generator. This confirmed the pipeline was sound in isolation.

Elements were then migrated back into `AudioCodecManager` one at a time:
1. Queue and pack logic → `AudioCodecManager.c` (`audioCodec_packHalf()` exported)
2. Sine generator render → `sineBufferTest_renderBlock()` calls `audioCodec_getRenderBuffer()` / `audioCodec_commitRenderBuffer()`
3. Hardware init → `audioTest_init()` called from `sineBufferTest_init()`
4. `DMA1_Stream7_IRQHandler` stub added before `audioTest_init()` was called (required: `dma_init()` enables Stream 7 HT+TC interrupts; without a handler the default handler is an infinite loop)
5. Stream 7 stub replaced with proper flag-clear-only handler
6. Sine stub in `mixer.c` (`mixer_calcNextSampleBlockTest`) replacing `calcNoiseBlock` etc., confirmed clean
7. Real `mixer_calcNextSampleBlock` wired in

`sineBufferTest.c/h` left aside after pipeline confirmed — not in production build.

---

### Part 5 — DSP voice bring-up: VLA stack corruption (Snare, Cymbal)

**Symptom**: Uncommenting the Snare block in `mixer_calcNextSampleBlock` immediately corrupted all three drum voices — not just Snare output.

**Root cause**: VLAs (variable-length arrays) in `Snare_calcSyncBlock` and `Cymbal_calcSyncBlock`:
```c
int16_t transBuf[size];   /* Snare — 192 bytes on stack */
int16_t mod[size];        /* Cymbal — 192 bytes on stack */
int16_t mod2[size];       /* Cymbal — 192 bytes on stack */
```
On Cortex-M7, stack overflow is silent and corrupts whatever memory sits below the stack — in this case, DSP voice state (all voices, not just the one calling the VLA function). The all-voices corruption pattern is the signature of this failure mode.

**Fix**: Changed all three to `static`:
```c
static int16_t transBuf[OUTPUT_DMA_SIZE];
static int16_t mod[OUTPUT_DMA_SIZE];
static int16_t mod2[OUTPUT_DMA_SIZE];
```
These functions are only called from the main loop — no reentrancy concern.

**Rule**: VLAs are forbidden in DSP voice files. Any `int16_t buf[size]` with a runtime-determined size parameter must be `static int16_t buf[OUTPUT_DMA_SIZE]`.

---

### Part 6 — RNG initialization bugs (random.c)

Two bugs in `initRng()`, both causing audio corruption when the function was called:

**Bug 1 — Wrong `RCC_AHB2ENR` address:**
```c
/* Wrong — this is RCC_AHB1ENR: */
#define RCC_AHB2ENR (* (volatile uint32_t *) 0x40023830UL)

/* Correct: */
#define RCC_AHB2ENR (* (volatile uint32_t *) 0x40023834UL)
```
The wrong address aliased `RCC_AHB1ENR`. The `|=` set bit 6 there (BKPSRAMEN, harmless), but the RNG clock was never actually enabled. The subsequent write to `RNG_CR` at `0x50060800` with the peripheral unpowered caused an AHB bus error / stall, manifesting as DMA underruns.

**Bug 2 — RMW on unpowered peripheral:**
Even with the address fixed, `RNG_CR |= RNG_CR_RNGEN` is a read-modify-write on `0x50060800`. If the peripheral clock is not yet stable when the read happens, this can stall the AHB2 bus. Changed to a direct write:
```c
RNG_CR = RNG_CR_RNGEN;  /* was: RNG_CR |= RNG_CR_RNGEN */
```

---

### Part 7 — GetRngValue() return type and call-site casting

`GetRngValue()` was returning `uint32_t`. `osc->output` in `OscInfo` is `int16_t`. Without an explicit cast at the assignment site, the compiler may promote through `int` and produce corrupt values at `-O2` depending on instruction scheduling.

**Fix**: Changed `GetRngValue()` to return `int16_t` with a full 32-bit cast:
```c
int16_t GetRngValue(void) {
    return (int16_t)RNG_DR;
}
```
Explicit `(int16_t)` cast required at every assignment site in `Oscillator.c` and anywhere else `GetRngValue()` is called, despite the matching types — the compiler promotion behavior at `-O2` makes this non-negotiable.

---

### Part 8 — LFO noise waveform range bug (lfo.c)

**Bug**: LFO noise case was dividing by `0xffffffff` (~4.3 billion):
```c
lfo->rnd = GetRngValue() & 0x7FFF;
lfo->rnd = lfo->rnd / (float)0xffffffff;   /* BUG: near-zero result */
```
With `GetRngValue()` returning at most `32767`, dividing by `~4.3B` produces a near-zero float. That near-zero value in the modulation chain corrupted voice parameters downstream.

**Fix**:
```c
case LFO_NOISE:
    if (overflow) {
        lfo->rnd = (float)(GetRngValue() & 0x7FFF) / 32767.0f;
    }
    return lfo->rnd;
    break;
```
Result: clean 0..1 float, matches what all other LFO waveforms return.

---

### Part 9 — FPU enable and compiler optimization

**FPU**: Application was relying on the bootloader enabling the FPU coprocessors (CP10/CP11 in CPACR) before jumping to the app. Application now owns this:
```c
/* Top of sysclk_init(), before anything else */
*(volatile uint32_t *)0xE000ED88UL |= (0xFUL << 20);
__asm volatile ("dsb");
__asm volatile ("isb");
```

**Optimization**: Makefile changed from `-O1` to `-O2`. `SVF_calcBlockZDF`, `tanhXdX`, `softClipTwo`, and `fastTanh` are called per sample per voice. At `-O1` none of these inline — each incurs full function call overhead. At `-O2` they inline and the DSP load drops significantly. With Cymbal active at `-O1`, UNR ≈ RND (renders barely keeping pace). At `-O2`, UNR stabilized at 78 (startup transient only) and stayed flat.

**78 startup underruns**: Fixed count that does not grow after boot. These occur before the main loop starts rendering — the DMA ISR fires during the boot/init sequence before `calcNextSampleBlock` has been called. Normal and expected. Any underrun count that grows during operation indicates a real timing problem.

---

## Known Issues / Technical Debt (end of Session 8)

### Audio / RNG
- `GetRngValue()` return type is now `int16_t`. Explicit cast `(int16_t)GetRngValue()` required at every assignment site. Without it, compiler promotion through `int` produces corrupt values at `-O2`.
- LFO noise `lfo->rnd` division fixed. If other LFO waveforms use similar normalization patterns, verify they divide by the correct range.
- 78 startup underruns expected and normal.

### High Priority
1. **buttonHandler/ledHandler audit**: Not yet done. Button numbering reversed from original (BUT_MODE1=31 vs original BUT_MODE1=35). Action logic in `processPress()` needs case-by-case audit against original `buttonHandler_buttonPressed()`. LED index mapping needs verifying against shift-register chain order.

### Medium Priority
2. TIM2 not initialised — needed for CLK IN BPM interval measurement and MIDI RX timestamping
3. MidiParser RX not connected to sequencer/parameter system
4. Preset save: pattern/all/performance stubs in place; real implementations need sequencer data structures
5. Morph buffer not connected (`parameters2[]` declared, unused)
6. Slider-to-parameter mapping not designed
7. Screensaver timing visibly off (clock recalculation needed)
8. `copyClearTools.c`: frontPanel calls commented out, need direct replacement

### Lower Priority / Future
9. SampleRom: `SampleMemory.c` is a safe no-op stub. Real implementation requires F765 `flash_if.c` (sector 6-11 layout confirmed)
10. SELECT_1 LED dark at boot — faithful reproduction of original LXR bug. Fix in repo 2 only.
11. `Parameters_reference.h.bak` — left for reference. Delete in a cleanup pass once full port verified.

## Critical Reminders for Next Session

### Audio Pipeline
- **Render loop**: `while(audioCodec_queueFreeSlots() > 0) calcNextSampleBlock()` — proven correct, non-racy, non-starving
- `audioCodec_queueFreeSlots()` reads `ready_head` and `ready_tail` as single-byte LDRB — atomic on Cortex-M7. No barrier needed.
- LCD ring and audio queue both use head/tail-only SPSC. Do not reintroduce any shared RMW counter in either.
- `DMA1_Stream7_IRQHandler` must exist (flag-clear only). Stream 4 is the refill master.
- 78 startup underruns is normal. Growing underrun count during operation is not.

### DSP / Voices
- **VLAs are forbidden in DSP voice files.** All `int16_t buf[size]` with runtime size → `static int16_t buf[OUTPUT_DMA_SIZE]`. Silent stack corruption of all voices is the failure mode.
- `(int16_t)GetRngValue()` cast required at every call site. Non-negotiable at `-O2`.
- LFO noise: `lfo->rnd = (float)(GetRngValue() & 0x7FFF) / 32767.0f`
- Compiler must be `-O2`. DSP voices do not meet timing budget at `-O1`.
- FPU explicitly enabled in `sysclk_init()` — do not rely on bootloader.

### RNG
- `RCC_AHB2ENR` = `0x40023834` (NOT `0x40023830` which is `RCC_AHB1ENR`)
- `RNG_CR = RNG_CR_RNGEN` (direct write, NOT `|=`)
- `GetRngValue()` returns `int16_t`, casts to `(int16_t)RNG_DR`

### General / Hardware (unchanged from Session 7)
- EXTI_IMR = 0 must remain as the very first operation in main(), before sysclk_init()
- Do NOT add pull-down to PD4 or PD5
- `lcd_init()` before `lcd_tim7_init()`
- `menu_init()` calls memset on parameter_values — do not also memset in main()
- TIM7_SR defined locally in timebase.c at 0x40001410 — do not remove
- 24-bit audio: `int16_t` MSW+LSW packed, I2SCFGR DATLEN=01/CHLEN=1
- SD card: bit-bang SPI on PC12/PD2/PC8/PD0. No SPI1 contention.
- PE13/PE14: AF1 (TIM1_CH3/CH4), external 10kΩ pull-ups, no internal pull-up
- Encoder: Dannegger, `last = new & 3`, round-toward-zero divide in `encode_read4()`, read1/read2 permanently removed
- `while → if` in `buttonHandler_processEvents()` — intentional, do not revert
- `sendDisplayBuffer` emits `lcd_setcursor` before every data byte — do not add position-tracking optimization
- `menu_knobs_dirty` / `menu_serviceKnobRepaint()` is RV1-4-only — do not extend to other input paths
- **DO NOT ATTEMPT TIM7 idle gating** — wakeup race confirmed broken (Session 6)
- **DO NOT ATTEMPT broad repaint coalescing across all input paths** — confirmed broken (Session 6)
- **DO NOT use `val >>= 2` in `encode_read4()`** — asymmetric floor division, fixed with round-toward-zero divide (Session 7)
- DTCM is NOT DMA-accessible — never tag a DMA buffer with INDTCM/INDTCMZ
- Two systick counters: `time_sysTick` (uint16_t), `systick_ticks` (uint32_t). Both 1kHz. Don't merge.
- TIM1 IRQ27, TIM6 IRQ54, TIM7 IRQ55
- DMA1 Stream 4 (I2S2/DAC2) = IRQ15; DMA1 Stream 7 (I2S3/DAC1) = IRQ47. Both in HISR/HIFCR.
- `audioCodec_init()` is the single entry point — replaces CodecInit() + audioTest_init() chain
- Stream 4 is the refill master; Stream 7 clears flags only
- `GetRngValue()` calls must cast explicitly: `(int16_t)GetRngValue()` at every assignment site
