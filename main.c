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
#include "SceneData.h"

#include "memtest.h"
#include <stdint.h>


#define SCB_VTOR (*(volatile uint32_t*)0xE000ED08UL)
#define LCD_LIMIT_TICKS_PER_REFRESH 20 // lazy 20ms rate limit on time_sysTick

static void dsp_init(void)
{
    uint8_t i;

    initRng();
    initDrumVoice();
    Snare_init();
    Cymbal_init();
    HiHat_init();
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
    dsp_init();
    /*
     * SceneData now owns every stored Pattern/Kit/parameter image. Initialize
     * it before Sequencer/Menu/filesystem clients obtain Scene-indexed views.
     */
    scene_initAll();
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
        uint8_t sd_ok = filesystem_initCardAndMountBlocking();
        show_unsupported_card_warning = filesystem_bootDetectedUnsupportedCard();

        /* Menu init — must be before preset load (memsets parameter_values) */
        menu_init();
        menu_setNumSamples(sampleMemory_getNumSamples());

        if (sd_ok) {
            /* Synchronous kit scan (blocking at boot, OK) */
            filesystem_requestScanKits(NULL);
            while (filesystem_status() == FS_STATUS_BUSY)
                filesystem_tick();
            filesystem_ack();

            /* Load kit 0 via presetManager (sets up status/callbacks) */
            preset_loadDrumset(0, 0);
            while (preset_getStatus() == PRESET_LOAD_IN_PROGRESS)
                filesystem_tick();
            menu_pollPresetStatus();  /* apply kit + ack */

            /* Load globals via presetManager */
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
    audioCodec_init();
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
