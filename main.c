/*
 * main.c
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

#include "config.h"
#include "clocks.h"
#include "AudioCodecManager.h"
#include "mixer.h"
#include "lcd.h"
#include "ledHandler.h"
#include "encoder.h"
#include "din.h"
#include "dout.h"
#include "adcPots.h"
#include "endlessPots.h"
#include "buttonHandler.h"
#include "timebase.h"
#include "globals.h"

#include "DrumVoice.h"
#include "Snare.h"
#include "CymbalVoice.h"
#include "HiHat.h"
#include "modulationNode.h"
#include "random.h"
#include "triggerJacks.h"
#include "sequencer.h"
#include "sequencerTimer.h"
#include "EuklidGenerator.h"
#include "SomGenerator.h"

#include "Uart.h"
#include "MidiParser.h"
#include "MidiVoiceControl.h"
#include "usb_manager.h"
#include "filesystem.h"
#include "SampleMemory.h"

#include "menu.h"
#include "screensaver.h"
#include "ParameterArray.h"
#include "presetManager.h"
#include "BankData.h"
#include "SceneData.h"
#include "InstrumentManager.h"

#include "memtest.h"
#include <stdint.h>


#define SCB_VTOR (*(volatile uint32_t*)0xE000ED08UL)
#define LCD_LIMIT_TICKS_PER_REFRESH 20 // lazy 20ms rate limit on time_sysTick

static void dsp_init(void)
{
    uint8_t i;

    initRng();
    /*
     * Initialize InstrumentManager's complete tagged runtime ownership.
     *
     * Inputs: RNG plus the boot-resident active Scene type for each slot.
     * Output: one initialized engine union member per visible slot before any
     * kit/preset value is applied. No engine module owns a permanent native
     * voice, so startup delegates all DSP runtime construction to the manager.
     */
    instrumentManager_runtimeInit();
    mixer_init();
    parameterArray_init();

    for (i = 0; i < 6; i++)
        modNode_init(&velocityModulators[i]);
}

static void boot_show_splash(void)
{
    lcd_clear();
    lcd_setcursor(0, 1);
    lcd_string("Sonic Potions");
    lcd_setcursor(0, 2);
    lcd_string("LXR Drums V0.37");
}

static inline uint32_t irq_getBasepri(void)
{
    uint32_t value;
    __asm volatile("mrs %0, basepri" : "=r"(value));
    return value;
}

static inline void irq_setBasepri(uint32_t value)
{
    __asm volatile("msr basepri, %0" :: "r"(value) : "memory");
    __asm volatile("dsb");
    __asm volatile("isb");
}

static inline uint32_t dsp_maskLowPriorityIrqs(void)
{
    /* Protect only one 32-frame DSP/control subblock from low-priority
    ** service work. BASEPRI leaves the timing-critical sources unmasked:
    ** SysTick, TIM1 encoder capture, TIM3 sequencer, EXTI timestamp edges,
    ** USART3, USB priority above the threshold if configured, and audio DMA.
    ** Do not replace this with cpsid/cpsie around a whole DMA slot; that would
    ** trade audio CPU spikes for avoidable MIDI/UI/clock latency. */
    uint32_t old = irq_getBasepri();
    irq_setBasepri(6u << 4);
    return old;
}

static void audio_check_and_render(void)
{
    /* Audio render.
    ** Hardware DMA halves stay large enough for scheduling slack, while the
    ** legacy DSP advances in OUTPUT_DMA_SIZE chunks. This keeps EG/LFO/control
    ** cadence aligned with LXR-master even though the F765 codec queue presents
    ** one complete AUDIO_DMA_FRAMES buffer to the I2S DMA layer. */
    while (audioCodec_queueFreeSlots() > 0) {
        sample_mx_t *buf  = audioCodec_getRenderBuffer();
        sample_mx_t *buf2 = audioCodec_getRenderBuffer2();

        for (uint32_t frame = 0; frame < AUDIO_DMA_FRAMES; frame += OUTPUT_DMA_SIZE) {
            uint32_t basepri;
            voiceControl_processPending();
            basepri = dsp_maskLowPriorityIrqs();
            mixer_calcNextSampleBlock(&buf[frame * 2], &buf2[frame * 2]);
            irq_setBasepri(basepri);
        }
        audioCodec_commitRenderBuffer();
    }
}

static void midi_service(void)
{
	uint8_t budget = 8;
	MidiMsg msg;

	uart_processMidi();

	while (budget-- && usb_getMidi(&msg)) {
		midiParser_parseMidiMessage(msg);
    }

    usb_tick();
}

static uint8_t prevBtn;
static void main_encoder_check()
{
    int8_t delta = encode_read4();
    uint8_t btn  = encode_readButton();
    if (delta || btn != prevBtn) {
        menu_parseEncoder(delta, btn);
        prevBtn = btn;
    }
}

static void endless_pot_check(void)
{/* Endless pots RV1-RV4 */
    uint8_t i;
    for (i = 0; i < ENDLESS_POT_COUNT; i++) {
        /* endlessPots_getDelta() is int32_t, but RV1-RV4 cannot produce
        ** large single-pass movement in normal use. If main-loop latency
        ** ever grows enough to accumulate big deltas, widen this path. */
        int8_t d = endlessPots_getDelta(i);
        if (d) {
            screensaver_touch();
            menu_parseKnobDelta(i, d);
        }
    }
}

static uint32_t last_repaint_tick;
static void service_knob_repaint()
{
    uint32_t now = time_sysTick;
    if ((now - last_repaint_tick) >= LCD_LIMIT_TICKS_PER_REFRESH)
    {
        last_repaint_tick = now;
        menu_serviceKnobRepaint();
    }
}

static void boot_delayMs(uint16_t ms)
{
    uint16_t t0 = time_sysTick;
    while ((uint16_t)(time_sysTick - t0) < ms) { /* boot-only hold */ }
}

/*
 * Development-only boot-screen instrumentation.
 *
 * What: CONFIG_DEV_MODE compiles the durable Boot/FS, FOp/FPhs, FPhs/FSub,
 * and HPhs/HRow OLED observers used to isolate a blocking storage phase. Why:
 * each observer may deliberately drain the LCD queue before filesystem work,
 * which is useful for diagnosis but must not replace the normal splash or add
 * boot latency in ordinary firmware. With development mode disabled, the
 * stage/active-operation macros below compile to no-ops and both filesystem
 * callback arguments become NULL; the underlying boot and `.hcnames` work
 * therefore runs in exactly the same order without any diagnostic display.
 */
#if CONFIG_DEV_MODE
static void boot_showHcnamesDiagnostic(uint8_t phase, uint16_t row)
{
    static uint8_t last_phase = 0xffu;
    static uint16_t last_update_tick = 0u;
    uint16_t now = time_sysTick;

    /*
     * Display the live HCNAMES writer coordinate without flooding the async
     * LCD queue.
     *
     * Inputs: phase/row observations from the blocking filesystem wrapper.
     * Output: row 1 shows `HPhs` and the phase number; row 2 shows `HRow` and
     * the next fixed-order SRAM row. Phase changes are displayed immediately,
     * while a streaming/stalled phase refreshes at 100 ms. Requiring room for
     * the complete two-row frame prevents a partial diagnostic from obscuring
     * the coordinate needed after a freeze. This hook observes boot only; it
     * does not pump, acknowledge, or otherwise alter filesystem state.
     */
    if (phase == last_phase &&
        (uint16_t)(now - last_update_tick) < 100u)
        return;
    if (lcd_queueFree() < 34u)
        return;

    last_phase = phase;
    last_update_tick = now;
    lcd_diagDisplayInt("HPhs", (int32_t)phase,
                       "HRow", (int32_t)row);
}

static void boot_showFilesystemStage(uint8_t stage)
{
    /*
     * Publish one durable boot milestone before entering a blocking SD step.
     *
     * Input: the stage number documented in HCNAMES_IMPLEMENTATION.md. Output:
     * row 1 displays `Boot` plus that stage; row 2 displays the filesystem
     * facade status observed before the operation begins. lcd_waitForIdle()
     * is intentional diagnostic instrumentation: it guarantees the complete
     * marker has physically reached the OLED before firmware can stall inside
     * the following filesystem call. This helper does not start, pump,
     * acknowledge, or reorder any filesystem operation.
     */
    lcd_diagDisplayInt("Boot", (int32_t)stage,
                       "FS", (int32_t)filesystem_status());
    lcd_waitForIdle();
}

static void boot_showActiveFilesystemDiagnostic(void)
{
    static uint8_t last_op = 0xffu;
    static uint8_t last_phase = 0xffu;
    uint8_t op;
    uint8_t phase;

    /*
     * Flush each stage-11 filesystem transition before pumping it.
     *
     * Inputs: read-only operation/phase coordinates from filesystem.c.
     * Output: `FOp` identifies repair=1, Bank=2, Scene=3, Kit=4, flush=5,
     * other=0; `FPhs` is the active private phase. Repeated observations of
     * the same coordinate do nothing. On a transition, lcd_waitForIdle()
     * guarantees the marker is physically visible before filesystem_tick()
     * enters the next phase, including a phase whose first pump never returns.
     * The helper does not mutate or acknowledge filesystem state.
     */
    filesystem_getBootDiagnostic(&op, &phase);
    if (op == last_op && phase == last_phase)
        return;

    last_op = op;
    last_phase = phase;
    lcd_diagDisplayInt("FOp", (int32_t)op,
                       "FPhs", (int32_t)phase);
    lcd_waitForIdle();
}

static void boot_showFilesystemSubstep(uint8_t substep)
{
    static uint8_t last_substep = 0xffu;

    /*
     * Display the exact blocking component about to run inside repair phase 43.
     *
     * Input: stable FSub code reported synchronously by filesystem.c. Output:
     * row 1 retains the confirmed parent phase (43) and row 2 shows the
     * component substep. Duplicate reports are suppressed. lcd_waitForIdle()
     * guarantees the marker reaches the OLED before the component call begins,
     * so a call that never returns leaves its own number visible. This callback
     * performs no filesystem operation or acknowledgement.
     */
    if (substep == last_substep)
        return;
    last_substep = substep;
    lcd_diagDisplayInt("FPhs", 43,
                       "FSub", (int32_t)substep);
    lcd_waitForIdle();
}

#define BOOT_HCNAMES_DIAGNOSTIC_CALLBACK boot_showHcnamesDiagnostic
#define BOOT_SUBSTEP_DIAGNOSTIC_CALLBACK boot_showFilesystemSubstep
#else
/*
 * Production substitutions deliberately avoid evaluating filesystem status or
 * touching the LCD. NULL callbacks also prevent filesystem.c from entering any
 * observer path while preserving the writer and repair state machines.
 */
#define boot_showFilesystemStage(stage) ((void)(stage))
#define boot_showActiveFilesystemDiagnostic() ((void)0)
#define BOOT_HCNAMES_DIAGNOSTIC_CALLBACK NULL
#define BOOT_SUBSTEP_DIAGNOSTIC_CALLBACK NULL
#endif

int main(void)
{
    #define EXTI_IMR (*((volatile uint32_t *)0x40013C00UL))
    #define EXTI_PR  (*((volatile uint32_t *)0x40013C14UL))
    uint8_t show_unsupported_card_warning = 0;

    EXTI_PR  = 0xFFFFFFFFUL;
    EXTI_IMR = 0x00000000UL;

    sysclk_init();
    SCB_VTOR = APP_FLASH_ORIGIN;
    __asm volatile("cpsie i" ::: "memory");
    time_initSysTick();

    lcd_init();
    lcd_tim7_init();
    boot_show_splash();
    encode_init();
    din_init();
    dout_init();

    endlessPots_init();
    adc_init();
    led_init();
    time_initTimer();

    triggerJacks_init();
    sampleMemory_init();
    /*
     * Establish retained Scene type ownership before constructing tagged DSP
     * runtime members.
     *
     * Inputs: power-on Scene/Bank SRAM. Outputs: scene_initAll() writes a
     * valid descriptor type into every resident instrument slot before
     * dsp_init() calls instrumentManager_runtimeInit(). This order is required
     * because DRM is enum value zero: initializing runtime first would mistake
     * raw BSS for six valid Drum assignments and let a loaded Snare/Cymbal/Hat
     * descriptor write through the wrong tagged member until a later switch.
     * Bank init remains adjacent because boot may load a Bank, an empty Bank,
     * a root Scene fallback, or a root Kit fallback from this defined state.
     * Affiliates: SceneData's default-type construction, BankData container
     * identity, InstrumentManager tagged slots, and Preset's post-load apply.
     */
    scene_initAll();
    bank_init();
    dsp_init();
    seq_init();
    euklid_init();
    som_init();

    initMidiUart();
    usb_init();
    adc_checkPots();

    /* Memory layout test (one-shot, gated on MEMTEST_ENABLED in config.h).
    ** Uncomment to enable: */
    // memtest_run();

    /* -----------------------------------------------------------------
    ** SD card init — BEFORE audioCodec_init().
    **
    ** filesystem_initCardAndMountBlocking() brings the card to SPI mode,
    ** initializes asyncfatfs, and polls until the filesystem is ready.
    ** Then the initial kit + globals are loaded. This is all blocking,
    ** but audio isn't running yet so there's nothing to starve.
    **
    ** After audioCodec_init(), all SD operations are non-blocking.
    ** ----------------------------------------------------------------- */
    {
        boot_showFilesystemStage(1u);  /* card init + asyncfatfs mount */
        uint8_t sd_ok = filesystem_initCardAndMountBlocking();
        show_unsupported_card_warning = filesystem_bootDetectedUnsupportedCard();

        /* Menu init — must be before preset load (memsets parameter_values) */
        menu_init();
        menu_setNumSamples(sampleMemory_getNumSamples());

        if (sd_ok) {
            /*
             * (The root-level `.hcindex` boot marker generation has been moved
             * to run after the Instrument scan so it can write the cache).
             */

            /* Synchronous kit scan (blocking at boot, OK) */
            boot_showFilesystemStage(2u);
            filesystem_requestScanKits(NULL);
            while (filesystem_status() == FS_STATUS_BUSY)
                filesystem_tick();
            filesystem_ack();

            /*
             * Persist the just-scanned Kit names in slot order.
             *
             * The filesystem owns one shared name cache, so this write must
             * happen before the Scene scan reuses that cache. Empty 000..999
             * rows are retained in `/Kit/.hcindex`; their position, not
             * alphabetic order, is the library identity used by Load/Save.
             */
            boot_showFilesystemStage(3u);
            (void)filesystem_createLibraryIndexBlocking(FS_LIBRARY_INDEX_KIT);

            /*
             * Synchronous Scene/ scan.
             *
             * Inputs: mounted SD card before audio starts. Output: the root
             * Scene library cache is populated so Load:[Scene] can browse
             * numbered Scene folders without kicking off a scan from the menu
             * foreground. This mirrors Kit/ scan timing and is safe here
             * because audio rendering has not started yet.
             */
            boot_showFilesystemStage(4u);
            filesystem_requestScanScenes(NULL);
            while (filesystem_status() == FS_STATUS_BUSY)
                filesystem_tick();
            filesystem_ack();

            /*
             * Persist root Scene names before the shared cache is reused by
             * any later library operation. Scenes intentionally use the root
             * `/Scene/` directory; Bank-local child Scenes are not included in
             * this index and remain Bank operation scratch.
             */
            boot_showFilesystemStage(5u);
            (void)filesystem_createLibraryIndexBlocking(
                FS_LIBRARY_INDEX_SCENE);

            /*
             * Synchronous Bank/ scan.
             *
             * Inputs: mounted SD card and the root Bank directory. Output:
             * filesystem's root Bank cache is ready before the boot load
             * chooses the first available Bank. This scan is separate from
             * Scene/ because Bank-local children use a two-digit namespace and
             * must not populate the root Scene library browser.
             */
            boot_showFilesystemStage(6u);
            filesystem_requestScanBanks(NULL);
            while (filesystem_status() == FS_STATUS_BUSY)
                filesystem_tick();
            filesystem_ack();

            /*
             * Persist the root Bank scan in slot order before the shared name
             * cache is reused by Instrument boot indexing. Bank-local child
             * Scenes are intentionally excluded; `/Bank/.hcindex` contains
             * only the root Bank display name for each 000..999 row.
             */
            boot_showFilesystemStage(7u);
            (void)filesystem_createLibraryIndexBlocking(
                FS_LIBRARY_INDEX_BANK);

            /*
             * Scan and create fresh per-type `.hcindex` files one type at a
             * time. The filesystem owns one shared Instrument name cache, so
             * each scan is written before that cache is disposed for the next
             * registry type. The blocking helper is restricted to boot, before
             * audio starts; runtime Save refreshes use the same state machine
             * through filesystem_tick().
             */
            boot_showFilesystemStage(8u);
            (void)filesystem_createBootIndexBlocking();

            /*
             * Boot through the current top-level container ladder.
             *
             * Inputs: scan caches populated above and settings.cfg already
             * loaded. Output: the last successfully loaded/saved Bank slot
             * loads first, defaulting to Bank 000 when settings are absent. If
             * that Bank
             * contains no child Scene, menu_pollPresetStatus() acknowledges the
             * successful Bank identity load and starts the root Scene/root Kit
             * fallback. The bounded two-pass loop below exists for that valid
             * empty-Bank case: first pass completes Bank, second pass completes
             * the fallback payload if one was posted.
             */
            {
                uint16_t boot_bank_slot = bank_restoreBankSlot();

                /*
                 * Instrument index generation above intentionally disposed
                 * the one shared name cache. Rehydrate the root Bank index
                 * before selecting the initial container; otherwise the Bank
                 * directory can exist on the card while the cache reports no
                 * valid Bank slot and the boot load silently falls through.
                 */
                boot_showFilesystemStage(9u);
                filesystem_requestLoadBankIndex(NULL);
                while (filesystem_status() == FS_STATUS_BUSY)
                    filesystem_tick();
                filesystem_ack();

                /*
                 * boot_bank_slot is the root Bank cache coordinate retained in
                 * BankData. It is read once so the existence check and load
                 * request use the same value. The Bank loader receives an
                 * all-Scenes mask because only the selected Bank folder knows
                 * which child Scenes actually exist; it intersects this
                 * request with the discovered child-present mask before
                 * loading.
                 */
                filesystem_setBootSubstepDiagnostic(
                    BOOT_SUBSTEP_DIAGNOSTIC_CALLBACK);
                boot_showFilesystemStage(10u);
                if (filesystem_bankSlotExists(boot_bank_slot)) {
                    preset_loadBank(boot_bank_slot, 0xffffu);
                } else {
                    /* No Bank is available: load the root Scene index before
                     * asking the existing fallback ladder to choose Scene/Kit.
                     * This cache transition is safe because no Bank payload is
                     * being loaded in this branch. */
                    filesystem_requestLoadSceneIndex(NULL);
                    while (filesystem_status() == FS_STATUS_BUSY)
                        filesystem_tick();
                    filesystem_ack();
                    if (filesystem_sceneSlotExists(
                            filesystem_firstSceneSlot())) {
                        preset_loadFirstAvailableSceneOrKit();
                    } else {
                        /* The Scene index is empty, so replace it with the
                         * Kit index before asking the same fallback helper to
                         * choose the first available Kit. */
                        filesystem_requestLoadKitIndex(NULL);
                        while (filesystem_status() == FS_STATUS_BUSY)
                            filesystem_tick();
                        filesystem_ack();
                        preset_loadFirstAvailableSceneOrKit();
                    }
                }
            }
            boot_showFilesystemStage(11u);
            for (uint8_t boot_load_pass = 0u;
                 boot_load_pass < 2u;
                 boot_load_pass++) {
                while (preset_getStatus() == PRESET_LOAD_IN_PROGRESS) {
                    boot_showActiveFilesystemDiagnostic();
                    filesystem_tick();
                }
                menu_pollPresetStatus();  /* apply Bank/Scene/Kit + ack */
                if (preset_getStatus() != PRESET_LOAD_IN_PROGRESS)
                    break;
            }
            filesystem_setBootSubstepDiagnostic(NULL);

            /*
             * Do not regenerate `/.hcnames` from resident SRAM at boot.
             *
             * Inputs: the initial Bank/Scene/Kit fallback ladder above has
             * completed. Output: existing root HCNAMES Scene rows survive
             * boot unchanged. Scene names are now card-owned metadata and no
             * longer exist in scene_t, so the old snapshot writer would erase
             * every unselected Scene row after a mask-selective Bank Load.
             * Successful root Scene/Bank operations create or update only the
             * rows they own through the shared cache; no boot-time directory
             * scan or second SRAM name store is required. Affiliates:
             * filesystem_loadBankDirectory_tick(), root Scene updates, and
             * the retained diagnostic stage numbering (stage 12 is skipped).
             */

            /* Load globals via presetManager */
            boot_showFilesystemStage(13u);
            preset_loadGlobals();
            while (preset_getStatus() == PRESET_LOAD_IN_PROGRESS)
                filesystem_tick();
            menu_pollPresetStatus();  /* apply globals + ack */
        } else {
            /* SD card not detected — menu_init already ran above */
        }
    }


    /* Initialise audio path: PLLI2S, GPIO, DMA circular streams, I2S.
    ** AFTER all blocking SD operations. From this point forward, SD
    ** operations are non-blocking via filesystem_tick() in the main loop. */
    boot_showFilesystemStage(14u);  /* pre-audio filesystem boot completed */
    audioCodec_init();
    /*
     * Replay the selected boot Scene through the exact runtime Scene-switch
     * transaction once DMA audio is live.
     *
     * Inputs: the pre-audio loader has selected and image-applied the active
     * Scene; audioCodec_init() has brought up the DMA/I2S control lifecycle.
     * Output: the ordinary deferred worker clears the pre-audio modulation
     * graph, reapplies all six tagged members, and rebinds every LFO/velocity
     * source in the same order as a manual Scene switch. This must call the
     * public worker starter rather than reconstructing its pending mask here:
     * hardware proved pre-audio target installation differs from the live
     * target-edit path. The existing drumset_apply_* cursor owns the short
     * post-startup transition; no SRAM or new boot state is allocated.
     * Affiliates: preset_sendDrumsetParameters(),
     * preset_startDrumsetApply(), preset_tickDrumsetApply(), and
     * menu_pollPresetStatus().
     */
    preset_startDrumsetApply();
    prevBtn = 0;
    last_repaint_tick = 0;

	menu_start();
    if (show_unsupported_card_warning) {
        /* Session 025: FAT12/exFAT or non-MBR layouts can look like "card
        ** present" but are unsupported by LXR. Hold this modal warning long
        ** enough to read before normal boot UI resumes. */
        lcd_clear();
        lcd_setcursor(0, 1);
        lcd_string("Unsupported card");
        lcd_setcursor(0, 2);
        lcd_string("use MBR-FAT32");
        lcd_waitForIdle();
        boot_delayMs(5000u);
    }
	sequencerTimer_init();

	for (;;) {
		// audio check and render each operation
		// to see if there is one of 2 forward buffer slots free to calc
		audio_check_and_render();
		/*
		 * TIM3 owns MIDI realtime, CLK/RST jack events, and seq_tick(), so it
		 * can mark sequencer LED state dirty while playback advances.
		 *
		 * ledHandler owns physical LEDs and needs Menu/button context to know
		 * which track/pattern/step is visible. Drain those dirty flags here in
		 * the foreground main loop instead of doing LED/menu reads from the
		 * sequencer timer path.
		 */
		led_processSeqLedState();
        audio_check_and_render();        
        // foreground front-panel hardware service, scheduled by TIM6 at 500Hz
        timebase_serviceFrontPanel();
        audio_check_and_render();
        // service DIN/USB MIDI input and flush USB MIDI output
        midi_service();
        audio_check_and_render();
        // main encoder touch
        main_encoder_check();    
        audio_check_and_render();
        // 4 front endless pots touch NB: how expensive is tan calc?
        endless_pot_check();
        audio_check_and_render();
        // repaint visible knob/external-MIDI parameter changes at a capped LCD rate
        service_knob_repaint();
        audio_check_and_render();
        // refresh read-only runtime widgets at their own display cadence
        menu_serviceRuntimeWidgets();
        audio_check_and_render();
        // always-on slider gain refresh (applied as separate multiply in mixer)
        adc_checkPots();
        audio_check_and_render();
        // start screensaver?
        screensaver_check();
        audio_check_and_render();
        // tick the update leds async
        led_tickHandler();
        audio_check_and_render();
        // if a button changed, resolve it
        buttonHandler_processEvents();
        audio_check_and_render();
        // tick the update buttons async
        buttonHandler_tick();
        audio_check_and_render();
        // tick the preset change async
        menu_pollPresetStatus();
        audio_check_and_render();
        // tick one queued morph parameter
        preset_morphTick();
        audio_check_and_render();
        // tick the SD read async
        filesystem_tick();
        audio_check_and_render();
    }
}


// DIAGNOSTIC OPTIONS FOR MAIN LOOP
        /* -----------------------------------------------------------------
        ** BUFFER UNDER-RUN DIAGNOSTIC/LCD diagnostic display function example:
        ** Drop this into main loop or inline function and it 
        ** shows underrun and render counts on the LCD every 2 seconds.
        ** non-blocking, kinda dirty as far as menu refreshes go, but it won't
        ** hang anything permanently. Use with any labels/values you want to see
        ** ----------------------------------------------------------------- */
        // {
        //     static uint32_t last_tick = 0;
        //     uint32_t now = systick_ticks;

        //     if ((now - last_tick) >= (2000u * SYSTICK_TICKS_PER_MS)) {
        //         last_tick = now;
        //         lcd_diagDisplayInt("UNR", (int32_t)audioCodec_underrunCount,
        //                            "RND", (int32_t)audioCodec_renderCount);
        //     }
        // }
        /* -----------------------------------------------------------------
        ** FLOAT DISPLAY EXAMPLE
        ** Shows a float on the LCD every 2 seconds, exponential-style.
        ** ----------------------------------------------------------------- */
        // {
        //     static uint32_t last_tick = 0;
        //     uint32_t now = systick_ticks;

        //     if ((now - last_tick) >= (2000u * SYSTICK_TICKS_PER_MS)) {
        //         last_tick = now;
        //         lcd_diagDisplayInt("FL1", (float)some_float_var_1,
        //                            "FL2", (float)some_float_var_2);
        //     }
        // }

        /* =================================================================
        ** TEST FUNCTIONS
        ** =================================================================
        **
        ** The blocks below contain test code that can be enabled by
        ** uncommenting. They are not compiled out — they live here so their
        ** include dependencies are visible and their usage is documented.
        **
        ** TEST 1: Sine wave audio path test
        ** ----------------------------------
        ** Replaces mixer_calcNextSampleBlock() with a 440Hz sine generator
        ** on all four outputs. Useful to verify DMA, I2S, and DACs before
        ** the full mixer is running.
        **
        ** To use:
        **   1. Comment out the "Audio render" block above.
        **   2. Uncomment the block below.
        **
        ** audioCodec_renderSineBlock() is defined in AudioCodecManager.c.
        */
        /*
        if (audioCodec_queueFreeSlots() > 0)
            audioCodec_renderSineBlock();
        */
