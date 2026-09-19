/*
 * Core/Hardware/frontPanel/IO/adcPots.c
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
 * adcPots.c
 *
 * Slider potentiometers RV5-RV10.
 * Reads from the shared ADC DMA buffer (adc_dma_buf[], owned by endlessPots.c).
 */

#include "adcPots.h"
#include "endlessPots.h"      /* for adc_dma_buf */
#include "config.h"
#include <math.h>

/* Slider index map: adc_dma_buf index for each slider (RV5..RV10) */
static const uint8_t POT_ADC_IDX[ADC_POT_COUNT] = {
    ADC_IDX_RV5, ADC_IDX_RV6, ADC_IDX_RV7,
    ADC_IDX_RV8, ADC_IDX_RV9, ADC_IDX_RV10
};

/* Usable ADC range once deadzone is removed on both ends */
#define SLIDER_RAW_MIN   SLIDER_DEADZONE
#define SLIDER_RAW_MAX   (4095U - SLIDER_DEADZONE)
#define SLIDER_RAW_SPAN  ((float)(SLIDER_RAW_MAX - SLIDER_RAW_MIN))
#define SLIDER_RAW_MASK  0x0fffu
#define SLIDER_LUT_ENTRIES 1024u

/* Public slider volume cache [0.0, 1.0], RV5..RV10.
**
** Session 023 moved the expensive log/dB taper calculation out of the
** foreground loop. The LUT stores native floats at 1,024 four-code ADC nodes;
** adjacent raw-code quartets share one node and are deliberately not interpolated.
** SLIDER_LOG_TAPER_DB therefore remains a compile-time knob without powf()
** calls every time adc_checkPots() runs. */
float slider_vol[ADC_POT_COUNT];
static float slider_lut[SLIDER_LUT_ENTRIES];

static inline float slider_raw_to_float(uint16_t raw)
{
    if (raw <= SLIDER_RAW_MIN) return 0.0f;
    if (raw >= SLIDER_RAW_MAX) return 1.0f;

    const float linear = (float)(raw - SLIDER_RAW_MIN) / SLIDER_RAW_SPAN;

    if (SLIDER_LOG_TAPER_DB <= 0.0f) {
        return linear;
    }

    /* Normalized dB-domain taper:
    ** raw_gain goes from min_gain..1, then normalized to 0..1 so
    ** the deadzone still clamps to true silence. */
    const float min_gain = powf(10.0f, -SLIDER_LOG_TAPER_DB / 20.0f);
    const float raw_gain = powf(10.0f, ((linear - 1.0f) * SLIDER_LOG_TAPER_DB) / 20.0f);
    return (raw_gain - min_gain) / (1.0f - min_gain);
}

static void slider_build_lut(void)
{
    /*
     * Build the 4,096-byte native-float slider LUT once at boot.
     *
     * Input: LUT index 0..1023 maps to four-code raw index << 2. Output: the
     * existing deadzone/log transfer result in one float node. adc polling uses
     * raw >> 2 and intentionally performs no inter-node interpolation, so this
     * reduces only attenuator step resolution, not float calculation precision.
     */
    for (uint32_t index = 0; index < SLIDER_LUT_ENTRIES; index++)
        slider_lut[index] = slider_raw_to_float((uint16_t)(index << 2u));
}

static void slider_refreshVolumes(void)
{
    /*
     * Refresh all slider gain caches through the same non-interpolated lookup.
     *
     * Input: six 12-bit DMA samples. Output: six full-float slider_vol gains.
     * adc_init() and adc_checkPots() are affiliates; sharing this loop prevents
     * their LUT indexing rules from diverging. Mixer block smoothing remains a
     * separate audio-control concern and does not interpolate this LUT.
     */
    for (uint8_t i = 0; i < ADC_POT_COUNT; i++) {
        uint16_t cur = adc_dma_buf[POT_ADC_IDX[i]];
        slider_vol[i] = slider_lut[(cur & SLIDER_RAW_MASK) >> 2u];
    }
}

/* -----------------------------------------------------------------------
** Public API
** ----------------------------------------------------------------------- */
void adc_init(void)
{
    /* Hardware already started by endlessPots_init().
    ** Seed slider multipliers from current DMA values. */
    slider_build_lut();
    slider_refreshVolumes();
}

void adc_checkPots(void)
{
    slider_refreshVolumes();
}

uint16_t adc_getPotRaw(uint8_t i)
{
    if (i >= ADC_POT_COUNT) return 0;
    return adc_dma_buf[POT_ADC_IDX[i]];
}

uint8_t adc_getPotValue(uint8_t i)
{
    if (i >= ADC_POT_COUNT) return 0;
    return (uint8_t)(slider_vol[i] * 100.0f);
}
