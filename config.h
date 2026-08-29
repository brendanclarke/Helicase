/*
 * config.h
 *
 *  Created on: 17.05.2026
 * ------------------------------------------------------------------------------------------------------------------------
 *  Copyright 2026 Brendan Clarke
 *  brendanpaulclarke@gmail.com
 *  https://www.brendanclarke.com
 * ------------------------------------------------------------------------------------------------------------------------
 *  This file is part of the LXR02 Open-Source software.
 * ------------------------------------------------------------------------------------------------------------------------
 *  Redistribution and use of the LXR02 Open-Source, hardware driver code, or any derivative works are permitted
 *  provided that the following conditions are met:
 *
 *       - The code may not be sold, nor may it be used in a commercial product or activity.
 *
 *       - Redistributions that are modified from the original source must include the complete
 *         source code, including the source code for all components used by a binary built
 *         from the modified sources. However, as a special exception, the source code distributed
 *         need not include anything that is normally distributed (in either source or binary form)
 *         with the major components (compiler, kernel, and so on) of the operating system on which
 *         the executable runs, unless that component itself accompanies the executable.
 *
 *       - Redistributions must reproduce the above copyright notice, this list of conditions and the
 *         following disclaimer in the documentation and/or other materials provided with the distribution.
 * ------------------------------------------------------------------------------------------------------------------------
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 *   INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *   DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 *   SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 *   WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 *   USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * ------------------------------------------------------------------------------------------------------------------------
 */

/*
 * config.h
 *
 * LXR-02 hardware configuration for STM32F765.
 * All confirmed pin assignments in one place.
 * No code — #defines only.
 */

#ifndef CONFIG_H_
#define CONFIG_H_

/* -----------------------------------------------------------------------
** Feature flags
** ----------------------------------------------------------------------- */
#define USE_SD_CARD          1
/*
 * DEV_MODE_DIAGNOSTIC displays runtime information on the screen for the user
 * to assess how operations are proceeding. It does not and should not ever add
 * additional file interaction steps, since the diagnostic may be used to
 * assess in-situ file procedures.
 *
 * What: value 1 compiles only the existing boot OLED operation/phase
 * observers. Value 0 makes every boot display hook a no-op and passes NULL for
 * the filesystem observers. Why: screen instrumentation deliberately waits
 * for LCD completion so the last operation is visible if boot stalls; keeping
 * it independent prevents those waits from changing a file-timing diagnosis.
 * Inputs: main.c's pre-audio boot stages and read-only filesystem coordinates.
 * Outputs/effects: boot operation/phase information on the LCD only.
 * Affiliates: boot_showFilesystemStage(),
 * boot_showActiveFilesystemDiagnostic(), and the boot observer callbacks.
 */
#define DEV_MODE_DIAGNOSTIC 0

/*
 * DEV_MODE_LOGGING writes operation codes to file for use in debugging. It
 * must never print anything to the screen or otherwise delay operations
 * unnecessarily since logging may be used to assess timing failures in other
 * modules that might otherwise be obscured by screen write delays.
 *
 * What: value 1 compiles the eight-byte filesystem-operation register,
 * cooperative boot timeout, best-effort `/bootlog.bin` timeout writer, and
 * bounded autosave lifecycle trace. Value 0 removes those logging behaviors.
 * Why: a splash-screen stall or autosave regression otherwise loses the exact
 * operation coordinate when power is removed.
 * Inputs: main.c's pre-audio logging window and filesystem.c's foreground
 * polling paths. Outputs/effects: SRAM code capture during normal progress and
 * one bounded file write only after timeout recovery begins. This flag owns no
 * LCD calls or LCD waits. Affiliates: filesystem_bootLoggingBegin(),
 * filesystem_bootLoggingArm(), filesystem_tick(), AutosaveTrace.c, and the
 * LXR-02 SD shim.
 */
#define DEV_MODE_LOGGING    1

/*
 * DEV_STALL_DETECTION enables cooperative stall detectors inside every
 * foreground filesystem state machine. Each detector fires once after its
 * phase stays unchanged for a site-specific tick threshold (20,000 to 50,000),
 * writes a diagnostic named error code visible in the menu error overlay,
 * records one AUTOSAVE_TRACE_STAGE_PHASE_STALL trace record (when
 * DEV_MODE_LOGGING is also 1), and aborts the operation with FS_STATUS_ERROR
 * so the menu is not frozen indefinitely.
 *
 * What: value 1 compiles all filesystem_pollPhaseStall() call sites, the
 * per-state-machine stall counter statics, and the counter resets in request
 * functions. Value 0 removes them entirely so no operation is ever aborted
 * on a tick-count basis. Why: during bench testing an operation may need to
 * run well beyond its normal threshold without being killed (e.g. a slow
 * card or a diagnostic pause), and stall detectors that abort would mask the
 * actual completion behavior. Inputs: none (compile-time only). Outputs:
 * when 1, each state machine can produce one bounded error-abort per request.
 * Affiliates: filesystem_pollPhaseStall(), every _tick function's stall
 * detector block, every request function's counter reset, AutosaveTrace.h
 * PHASE_STALL site constants.
 */
#define DEV_STALL_DETECTION 1

/*
 * Fixed boot-time AutoSave-ensure failure capsule geometry and schema.
 *
 * What: reserves eight fixed eight-byte records only in a logging build;
 * schema 2 changes E7 bytes 5..6 from schema 1's SD retry_count to elapsed
 * wait_ms. Why: SD response abandonment is now time-based and old captures
 * must remain distinguishable from new evidence. Inputs are logging-only
 * AsyncFATFS/SD snapshot producers. Output is the unchanged 64-byte suffix
 * after the eight-byte ASENSURE token. Ownership: filesystem.c; lifetime: one
 * boot attempt. No file is opened and no storage exists when logging is off.
 * Record size/count, linker placement, and product on-card formats do not
 * change. Affiliates: sdcardTransportSnapshot_t, DEV_MODES.md,
 * decode_devlogs.py, and devlog_unpack.py.
 */
#if DEV_MODE_LOGGING
#define HCPRMS_BOOT_CAPSULE_SCHEMA_VERSION 2u
#define HCPRMS_BOOT_CAPSULE_RECORD_BYTES   8u
#define HCPRMS_BOOT_CAPSULE_RECORD_COUNT   8u
#define HCPRMS_BOOT_CAPSULE_BYTES \
    (HCPRMS_BOOT_CAPSULE_RECORD_BYTES * HCPRMS_BOOT_CAPSULE_RECORD_COUNT)
#endif

/*
 * Maximum duration of one armed boot filesystem operation.
 *
 * What: supplies the millisecond deadline used only while DEV_MODE_LOGGING is
 * active before audio startup. Why: time_sysTick is a wrapping uint16_t, so the
 * interval must stay below 32,768 ms for unsigned elapsed-time comparison.
 * Inputs: one operation arm. Output: timeout after exactly ten seconds without
 * terminal completion. Affiliates: filesystem_bootLoggingArm(),
 * filesystem_tick(), and filesystem_blockPoll().
 */
#define BOOT_FILESYSTEM_TIMEOUT_MS 20000u

/*
 * DEV_LOGGING_IWDG: catch a genuine pre-audio hard lockup that the
 * cooperative BOOT_FILESYSTEM_TIMEOUT_MS deadline above cannot see -- a raw
 * blocking call (SD_init()'s command loop, the bit-bang SPI byte clocking,
 * or a fixed timebase_holdPreAudioMs() hold) that never returns skips every
 * cooperative poll, so nothing after it ever runs to notice or log it.
 *
 * What: value 1 starts the STM32F765's independent watchdog (IWDG) once,
 * early in main(), and feeds it from the two foreground pumps --
 * filesystem_tick() and filesystem_blockPoll() -- both reachable only from
 * ordinary foreground code, never from an ISR. If the
 * foreground genuinely stops calling filesystem_tick() (a blocking call
 * above never returns), or keeps calling it forever without ever finishing
 * boot, feeding stops and the IWDG resets the MCU on its own free-running
 * hardware timer, independent of interrupts or software state. On the next
 * boot, RCC_CSR's IWDGRSTF flag identifies that reset cause; if a small
 * retained SRAM2 capsule (see filesystem.c) still holds a valid last-known
 * boot-log code from before the reset, that code is written to bootlog.bin
 * exactly like an ordinary cooperative timeout would, through the existing
 * filesystem_writeBootFailureLogBlocking() path -- no new on-card format.
 * Value 0 leaves the IWDG peripheral untouched and never starts it.
 *
 * This owns no NVIC/interrupt configuration and changes none: the IWDG has
 * no interrupt line on this part, only a free-running hardware reset, so
 * enabling it does not touch vector priorities or any existing ISR. Once
 * started for a boot the IWDG cannot be stopped in software (only fed or
 * left to expire), so it remains armed for the rest of that session,
 * including all of runtime after boot completes.
 *
 * Affiliates: DEV_LOGGING_IWDG_EXPIRE below, filesystem.c's retained boot
 * capsule, and main.c's single call to filesystem_devIwdgBootCheck()
 * immediately after filesystem_bootLoggingBegin().
 *
 * DEFAULT IS 0 AFTER A REGRESSION (2026-08-21). The first version of this
 * feature hung the instrument indefinitely on the boot splash: the IWDG init
 * spun on IWDG_SR waiting for a PR/RLR write to cross into the LSI clock
 * domain before it had written the 0xCCCC key that starts the LSI, and this
 * firmware enables the LSI nowhere else. It produced no timeout, no bootlog,
 * and no trace, because it ran before the card was mounted and the watchdog
 * itself had not started. filesystem_devIwdgStart() is now ordered correctly,
 * bounds every handshake against TIM6 milliseconds, and starts nothing at all
 * if the LSI never reports ready, so that specific hang cannot recur.
 *
 * It is nevertheless left OFF by default so a normal build carries no
 * watchdog risk. Before setting it back to 1, confirm every foreground path
 * that can legitimately run longer than the ~32.8 s period still reaches a
 * feed. Both known pumps are covered — filesystem_tick() and
 * filesystem_blockPoll(), the latter specifically so the modal sample install
 * cannot be reset part-way through a sampleFlash erase/program — but that
 * audit should be repeated for any new long-running blocking operation.
 * Enable it deliberately, for a hang-hunting session, not as a standing
 * default.
 */
#define DEV_LOGGING_IWDG 0

/*
 * Software ceiling, in milliseconds, on how long the pre-audio boot window
 * may keep feeding the IWDG before deliberately letting it lapse.
 *
 * What: measured against the free-running 32-bit systick_ticks (0.25ms
 * resolution, ~12.4-day wrap) rather than the wrapping 16-bit time_sysTick
 * used elsewhere in this file, because 2 minutes exceeds time_sysTick's safe
 * 32,768ms comparison range. Why: a foreground loop that keeps calling
 * filesystem_tick() forever without ever reaching filesystem_bootLoggingEnd()
 * -- e.g. a state-machine retry loop that never reaches DONE and never trips
 * the per-operation BOOT_FILESYSTEM_TIMEOUT_MS deadline -- is a hang
 * BOOT_FILESYSTEM_TIMEOUT_MS cannot always bound end-to-end; this is the
 * backstop that intentionally stops feeding once it elapses, letting the
 * IWDG's own native period reset the MCU shortly after. A call that never
 * returns at all is caught sooner, by the IWDG's native period alone.
 * Only meaningful while DEV_LOGGING_IWDG is 1. Affiliate:
 * filesystem_tick()'s IWDG feed gate.
 */
#define DEV_LOGGING_IWDG_EXPIRE 120000u

//if 1 the amp EGs will be calculated on a per sample basis
//takes too much calcuklation time
//if 0 they are calculated for each dma buffer once, only a smoothing LP will be calculated in the sync loop
#define AMP_EG_SYNC 0

//if 1 the 3 drum voices will have the option to mix the mod osc with the main osc, instead of modulating it
#define ENABLE_MIX_OSC 1
#define ENABLE_DRUM_SVF 1

// TODO DSP_PORT - should be declared in sequencer.h, some oscillator junk needs this defined for now.
#define SEQ_DEFAULT_NOTE 63


#define EG_SPEED 	1;//0.04125f
#define PITCH_AMOUNT_FACTOR 32

/* MEMTEST_ENABLED: if 1, runs memtest_run() at boot before normal init
** of menu/preset, displaying flash sector layout, dual-bank status,
** application boundary, sample-region contents. Read-only by default;
** holding BAR1 (PB7 GPIO, the SW43 button) at boot enables a
** destructive erase+write probe of sector 11 only. Set to 0 for
** production builds. */
#define MEMTEST_ENABLED      1

/* -----------------------------------------------------------------------
** Firmware version
** ----------------------------------------------------------------------- */
#define FIRMWARE_VERSION     "0.01"

/* -----------------------------------------------------------------------
** Flash layout
** Bootloader: sector 0 @ 0x08000000 (32KB)
** Application: sector 1+ @ 0x08008000
** ----------------------------------------------------------------------- */
#define APP_FLASH_ORIGIN     0x08008000UL

/* -----------------------------------------------------------------------
** Clock
** Running on 16MHz internal HSI.
** SysTick: 4kHz canonical LXR mainboard tick (`systick_ticks`).
** TIM6: 1kHz front-panel/service heartbeat (`time_sysTick`).
** ----------------------------------------------------------------------- */
#define SYSCLK_HZ            216000000UL
#define TIMEBASE_HZ          4000
#define SYSTICK_HZ           TIMEBASE_HZ
#define FRONTPANEL_TICK_HZ   1000
#define SYSTICK_TICKS_PER_MS (SYSTICK_HZ / 1000)

/*
 * Ordinary pause between autonomous autosave transactions.
 *
 * The writer compares this with the wrapping 16-bit `time_sysTick` using
 * unsigned subtraction, so it must remain below 32,768 ms. This five-second
 * cadence applies before the first runtime attempt and after an error,
 * recovery, read-only empty-mask check, or fully drained write. A successful
 * write that carries dirty bits forward uses the shorter continuation cadence
 * below instead. Neither cadence is a synchronous deadline: Load/Save pages
 * and ordinary filesystem work may defer the next background start.
 */
#define AUTOSAVE_WRITER_INTERVAL_MS 5000u

/*
 * Short pause between bounded writes while a durable backlog still exists.
 *
 * Input is Autosave.c's persistent canonical dirty record after a successful
 * target commit and normal filesystem flush. Output schedules the next complete
 * validation/drain transaction 250 ms later only when that owner still has a
 * dirty bit. Why: bounded backlog drains promptly without changing the
 * five-second debounce for new work, a complete record, or an error. This
 * remains below the 16-bit scheduler's 32,768 ms comparison limit. Affiliates:
 * filesystem_autosaveParameterDrain_tick() and its completion callback.
 */
#define AUTOSAVE_WRITER_CONTINUATION_INTERVAL_MS 250u

/*
 * Trailing debounce for autonomous settings.cfg persistence.
 *
 * What: supplies the one-second delay restarted by every changed Global-menu
 * value or successful Bank/Scene provenance update. Why: bursts must coalesce
 * without making a synchronous SD write from Menu or Preset completion code.
 * Inputs: filesystem_markSettingsDirty() and the wrapping 16-bit time_sysTick.
 * Output/effects: the idle filesystem scheduler may start one complete keyed
 * settings write after the last change. Affiliates: filesystem_tick() and the
 * existing FS_INTERNAL_OP_SAVE_GLOBALS state machine. This interval must stay
 * nonzero and below the wrapping timer's 32,768 ms half range.
 */
#define SETTINGS_AUTOWRITE_DEBOUNCE_MS 1000u

/*
 * Bound live payload sampling inside one debounced autosave transaction.
 *
 * Input is Autosave.c's canonical mask after OR-merging the valid winner's
 * file-carried completeness bits. Output is at most this many stable payload
 * offset/value patches captured before the one transformed copy begins. Why:
 * parameter reads and patch-cache growth remain predictable even when a whole
 * Scene is dirty. The selected 1,536 entries occupy 4,608 filesystem-owned
 * bytes; beside Autosave.c's 3,856-byte canonical mask, combined storage stays
 * 8,464 bytes below the temporary 9,000-byte ceiling. It can capture about
 * three current Scenes per generation. Affiliates: Autosave.c's live-byte
 * projection and filesystem.c's parameter-drain cache.
 */
#define AUTOSAVE_PARAMETER_GETS_PER_WRITE 1536u

/*
 * Bound mutation-mask classification work performed by one foreground tick.
 *
 * Input is the retained payload scan cursor. Output advances by no more than
 * this many bit positions before returning to the main loop. This is distinct
 * from AUTOSAVE_PARAMETER_GETS_PER_WRITE: nonexistent/padding cells consume
 * scan work but no live get. Affiliates: filesystem_autosaveParameterDrain_tick().
 */
#define AUTOSAVE_MASK_BITS_PER_TICK 256u

/*
 * Bound CRC32C CPU work performed by one cooperative filesystem pass.
 *
 * Input is a contiguous interval of one 34,768-byte hidden AutoSave record.
 * Output limits initial-image generation, candidate validation, recovery, and
 * transformed-copy checksum work to this many bytes before filesystem.c yields
 * to the main loop. Why: a full-record CRC previously monopolized the
 * foreground long enough to starve audio rendering; this is a work budget, not
 * an SD transfer delay, timer interval, or filesystem pacing mechanism.
 * Affiliates: Autosave.c's bounded initial-image updater and filesystem.c's
 * AutoSave ensure/writer state machines.
 */
#define AUTOSAVE_CRC_BYTES_PER_TICK 128u

/*
 * Retained autosave-trace capacity. The default is 64 records (512 bytes);
 * this diagnostic pass temporarily retains 2048 records (16,384 bytes) in
 * the DEV_MODE_LOGGING-only SRAM1 ring. Restore the effective value to the
 * default when the trace extension experiment is complete.
 */
#define AUTOSAVE_TRACE_RECORD_COUNT_DEFAULT 64u
#define AUTOSAVE_TRACE_RECORD_COUNT 2048u /* TEMPORARY approved expansion */

/*
 * Minimum idle interval between background autosave-trace append attempts.
 * Input is time_sysTick's wrapping millisecond clock; output keeps diagnostic
 * trace I/O below settings persistence and the autosave writer in the shared
 * filesystem scheduler. This is observability cadence only, never a writer
 * debounce or durability policy, and applies solely when DEV_MODE_LOGGING is 1.
 */
#define AUTOSAVE_TRACE_FLUSH_INTERVAL_MS 500u

/* -----------------------------------------------------------------------
** Display — WS0010 OLED 16×2, 4-bit parallel
**   RS  = PE12
**   E   = PE11
**   DB4 = PE7
**   DB5 = PE8
**   DB6 = PE9
**   DB7 = PE10
** ----------------------------------------------------------------------- */
#define LCD_COLS             16
#define LCD_ROWS             2

/* -----------------------------------------------------------------------
** Shift registers — SPI1 full-duplex
**   74HC595 × 5 (LED output, 40 LEDs, MSB first)
**   74HC165 × 5 (button input, 40 buttons, active HIGH)
**
**   MOSI = PA7  (AF5)
**   SCK  = PB3  (AF5)
**   MISO = PA6  (AF5)
**   LATCH/LOAD = PB2  (GPIO, active HIGH pulse)
**
**   LED bit = 39 - button_index
** ----------------------------------------------------------------------- */
#define NUM_INPUTS           40       /* shift-register buttons           */
#define NUM_OUTS             40       /* shift-register LEDs              */
#define SR_CHAIN_BYTES       5        /* 5 bytes = 40 bits                */

/* SW43 — separate GPIO (BAR1 button — new on LXR-02, no original LXR equivalent)
**   Switch = PB7  (input, active HIGH)
**   LED    = PB8  (output, active HIGH)
**
** Reserved for future firmware. Currently used at boot to gate the
** memtest write-probe (hold BAR1 at power-on → destructive sector-11
** test runs). No runtime function in normal operation.
** ----------------------------------------------------------------------- */
#define SW43_BUTTON_IDX      40       /* matches BUT_BAR1 in buttonHandler.h */

/* -----------------------------------------------------------------------
** Main encoder — digital quadrature + push switch
**   A  = PE13  (input, pull-up)
**   B  = PE14  (input, pull-up)
**   SW = PE15  (input, pull-up, active LOW)
** ----------------------------------------------------------------------- */

/* -----------------------------------------------------------------------
** Endless pots RV1-4 — analog sine/cosine outputs, sampled by ADC1
**
**   RV1: A=PB0/ADC1_IN8   B=PB1/ADC1_IN9
**   RV2: A=PC4/ADC1_IN14  B=PC5/ADC1_IN15
**   RV3: A=PC2/ADC1_IN12  B=PC3/ADC1_IN13
**   RV4: A=PC0/ADC1_IN10  B=PC1/ADC1_IN11
** ----------------------------------------------------------------------- */
#define ENDLESS_POT_COUNT       4
/* +/- raw ADC counts out of 4096 before a baselined endless pot is treated
** as intentionally moving. */
#define ENDLESS_POT_DEADZONE    10
/* Milliseconds before a moving endless pot is naively re-baselined from the
** current raw A/B values, regardless of whether it emitted a delta. TIM6
** samples at 1kHz, so this front-panel ms value is also the tick count. */
#define ENDLESS_POT_TIMEOUT_MS        10000
/* Milliseconds after the last emitted endless-pot delta before the current
** raw A/B values are cached as the new quiet baseline. */
#define ENDLESS_POT_DELTA_TIMEOUT_MS  50

/* -----------------------------------------------------------------------
** Sliders RV5-10 — single-ended linear, 0–3.3V → ADC1
**   RV5  = PA4 / ADC1_IN4
**   RV6  = PA5 / ADC1_IN5
**   RV7  = PA3 / ADC1_IN3
**   RV8  = PA2 / ADC1_IN2
**   RV9  = PA1 / ADC1_IN1
**   RV10 = PA0 / ADC1_IN0
**
**   All 14 ADC channels (sliders + endless-pot A/B) scanned continuously
**   via ADC1 DMA circular buffer.
** ----------------------------------------------------------------------- */
#define ADC_POT_COUNT        6        /* sliders RV5-RV10                 */
#define ADC_POT_HYSTERESIS   0       /* raw counts dead-band (~1%)       */
/* Deadzone for slider pots, in raw 12-bit ADC counts.
** Below this value output clamps to 0.0f; above (4095 - SLIDER_DEADZONE)
** output clamps to 1.0f. */
#define SLIDER_DEADZONE      50
/* Log taper depth for slider gain mapping (dB).
** 0 dB = linear. Higher values give more "audio taper":
** finer control near low volumes, steeper rise near the top.
**
** adcPots.c builds a 4096-entry LUT from this value at boot. Changing this
** define still changes the curve, but the main loop no longer pays repeated
** powf() cost while audio is running. */
#define SLIDER_LOG_TAPER_DB  30.0f

/* -----------------------------------------------------------------------
** ADC DMA buffer layout (14 channels total, indices into adc_dma_buf[])
**   [0]  IN0  PA0  RV10
**   [1]  IN1  PA1  RV9
**   [2]  IN2  PA2  RV8
**   [3]  IN3  PA3  RV7
**   [4]  IN4  PA4  RV5
**   [5]  IN5  PA5  RV6
**   [6]  IN8  PB0  RV1-A
**   [7]  IN9  PB1  RV1-B
**   [8]  IN10 PC0  RV4-A
**   [9]  IN11 PC1  RV4-B
**  [10]  IN12 PC2  RV3-A
**  [11]  IN13 PC3  RV3-B
**  [12]  IN14 PC4  RV2-A
**  [13]  IN15 PC5  RV2-B
** ----------------------------------------------------------------------- */
#define ADC_DMA_BUF_LEN      14

/* Slider indices into adc_dma_buf */
#define ADC_IDX_RV10         0
#define ADC_IDX_RV9          1
#define ADC_IDX_RV8          2
#define ADC_IDX_RV7          3
#define ADC_IDX_RV5          5   /* PA5 IN5 — physical RV5 slider */
#define ADC_IDX_RV6          4   /* PA4 IN4 — physical RV6 slider */

/* Endless-pot indices into adc_dma_buf */
#define ADC_IDX_RV1A         6
#define ADC_IDX_RV1B         7
#define ADC_IDX_RV4A         8
#define ADC_IDX_RV4B         9
#define ADC_IDX_RV3A         10
#define ADC_IDX_RV3B         11
#define ADC_IDX_RV2A         12
#define ADC_IDX_RV2B         13

/* Endless-pot A/B index pairs [pot][0=A, 1=B] */
#define ADC_IDX_ENDLESS_A(n)  ((const uint8_t[]){ADC_IDX_RV1A, ADC_IDX_RV2A, \
                                ADC_IDX_RV3A, ADC_IDX_RV4A}[n])
#define ADC_IDX_ENDLESS_B(n)  ((const uint8_t[]){ADC_IDX_RV1B, ADC_IDX_RV2B, \
                                ADC_IDX_RV3B, ADC_IDX_RV4B}[n])

/* -----------------------------------------------------------------------
** DSP / Audio configuration
**
** Mirrors original LXR mainboard config.h. Values match the original
** so the DSP source files port verbatim. Numbers below are NOT to be
** treated as LXR-02 specific defaults — they are the original LXR
** values, present here so the DSP code compiles unchanged.
**
** NUM_VOICES = 3 is the count of "drum" voices using DrumVoice.c. The
** total voice count is 6 (3 drums + 1 snare + 1 cymbal + 1 hihat). This
** discrepancy is original and intentional — keep as-is.
** ----------------------------------------------------------------------- */
#define NUM_VOICES           3
#define DMA_MODE_ACTIVE      1
/* OUTPUT_DMA_SIZE matches the effective LXR-master DSP/control block size.
** AudioCodecManager.h defines DMA_MODE_ACTIVE only after including config.h,
** so the reference's later #if DMA_MODE_ACTIVE override does not reduce this
** to 16 during the normal audio build. The F765 codec layer packs several of
** these reference blocks into one larger DMA half below. */
#define OUTPUT_DMA_SIZE      32   /* DSP frames per mixer block (0.73ms @ 44108Hz) */
/* Hardware frames per DMA half. 96 is the measured stable session-023
** compromise: enough queue slack for foreground UI/storage bursts while each
** hardware slot remains exactly three canonical 32-frame DSP/control blocks.
** 128 requires enlarging the 4KB .dma_nocache MPU/linker window; 64 reduced
** slack enough to freeze/glitch during pattern load testing. */
#define AUDIO_DMA_FRAMES     96   /* hardware frames per DMA half (2.18ms @ 44108Hz) */
#if (AUDIO_DMA_FRAMES % OUTPUT_DMA_SIZE) != 0
#error "AUDIO_DMA_FRAMES must be an integer multiple of OUTPUT_DMA_SIZE"
#endif
#define USE_DAC2             1
#define I2S_TEST_TONE        0   /* 1 = bypass DSP and emit four-output sine test */
#define I2S_TEST_TONE_HZ     440.0f
#define AUDIO_RENDER_IN_DMA_ISR 0 /* 1 = diagnostic only; full mixer in DMA ISR can starve UI */

// wether to interpolate the oscillator wavetables or not
// TODO DAC_PORT - I think ok to leave this, setting to 0 will cause other ifndef
// errors further along, it isn't really a viable option any more.
#define INTERPOLATE_OSC 1
#define INTERPOLATE_FM_OSC 1

/* Maximum number of oscillator waveform targets that may use waveform
** interpolation in one 32-frame DSP block. Extra eligible targets snap to
** their integer waveform ID for that block to bound CPU cost. This keeps user
** samples eligible for interpolation while preventing every modulated voice
** from doubling oscillator render work at once. */
#define OSC_WAVE_INTERP_MAX_ACTIVE 2u


/* SYNTH_FS — omit. Original was the StdPeriph enum I2S_AudioFreq_44k passed
**           to CodecInit(SYNTH_FS). Our register-level I2S init in
**           audioTest.c writes I2SPR directly. Not used anywhere downstream. */

/* REAL_FS / REAL_FS2 — actual sample rate produced by our PLLI2S+prescaler.
**   PLLI2SCLK = 16MHz × 271 / 16 / 2 = 135.5 MHz
**   I2SPR: MCKOE=1, ODD=0, I2SDIV=6, 24-bit (CHLEN=1)
**   Fs = 135,500,000 / (256 × (2×6 + 0)) = 44108.0729 Hz
**   (NOT the original's 44002.76 — different PLLI2S config) */
#define REAL_FS    ((float)44108.07291666667f)
#define REAL_FS2   (REAL_FS / 2.f)

/* LED_BLINK_TIME — omit. Defined in original mainboard config.h but never
**                 referenced in any mainboard source file. The AVR side
**                 had its own definition (250ms / 16.384 tick ratio).
**                 Add a fresh one if/when our LED-blink logic needs it. */

/* -----------------------------------------------------------------------
** Memory placement attributes for DSP hot data
**
** Original LXR puts hot-path DSP state in CCM via INCCM (loaded from
** flash) and INCCMZ (zero-init). On STM32F765 the equivalent memory is
** DTCM — same single-cycle access, no DMA accessibility, smaller working
** set than the F4 CCM (128KB vs 64KB on F4 — bigger here).
**
** INDTCM  — placed in DTCM, initialised from a flash copy at startup
** INDTCMZ — placed in DTCM, zero-initialised at startup
**
** INCCM and INCCMZ are aliased to INDTCM/INDTCMZ so DSP source files
** that use the original LXR macros port verbatim. New LXR-02 code
** should use INDTCM/INDTCMZ directly to make the placement explicit.
**
** ⚠ DTCM is NOT DMA-accessible. Anything touched by DMA — audio
**   buffers, ADC scan buffer, USB endpoints — MUST stay in plain SRAM1.
**   Do not tag DMA buffers with INDTCM/INDTCMZ.
** ----------------------------------------------------------------------- */
#define INDTCM  __attribute__((section(".dtcm")))
#define INDTCMZ __attribute__((section(".dtcmz")))

/* ITCM hot-code placement switches.
**
** Oscillator placement is enabled for CPU monitor A/B testing. Filter and
** distortion placement stay disabled because the forced out-of-line ITCM pass
** measured worse on the hardware CPU monitor.
*/
#ifndef ENABLE_OSC_INITCM_CODE
#define ENABLE_OSC_INITCM_CODE 1
#endif

#ifndef ENABLE_EFFECT_INITCM_CODE
#define ENABLE_EFFECT_INITCM_CODE 0
#endif

#if ENABLE_OSC_INITCM_CODE
#define INITCM  __attribute__((section(".itcm")))
#else
#define INITCM
#endif

#if ENABLE_EFFECT_INITCM_CODE
#define INITCM_EFFECT __attribute__((section(".itcm")))
#define INITCM_EFFECT_NOINLINE __attribute__((section(".itcm"), noinline, noclone))
#else
#define INITCM_EFFECT
#define INITCM_EFFECT_NOINLINE
#endif

#define INITCM_NOINLINE INITCM_EFFECT_NOINLINE

/* Compatibility aliases for verbatim porting from original LXR DSP src. */
#define INCCM   INDTCM
#define INCCMZ  INDTCMZ

#endif /* CONFIG_H_ */
