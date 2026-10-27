/*
 * Host support for the S085 oscillator tests (S085 C17.1).
 *
 * What:       the small set of cross-module services the oscillator, section,
 *             sample, wavetable and pwm sources call, as instrumented host
 *             doubles: the sample/wavetable catalogue, the modulation-node
 *             waveform-interpolation gate, random noise and buffer add.
 * Why:        those owners (SampleMemory.c, modulationNode.c, random.c,
 *             BufferTools.c) pull firmware-only includes; the DSP arithmetic
 *             under test must remain the real source.
 * Inputs:     the test's catalogue setup.  Outputs: SampleInfo/WavetableInfo.
 * Accessors: test_osc_pwm.c, test_osc_sample.c, test_osc_run.c, test_wtb.c.
 * Affiliates: SampleMemory.h, Oscillator.h, BufferTools.h.
 */
#include "prelude_osc.h"
#include "SampleMemory.h"
#include "BufferTools.h"
#include "random.h"
#include "ResonantFilter.h"

const float sampleMemory_gainMantissa[SAMPLE_GAIN_STEPS_PER_OCTAVE] = {
    1.000000f, 1.029302f, 1.059463f, 1.090508f, 1.122462f, 1.155353f,
    1.189207f, 1.224054f, 1.259921f, 1.296840f, 1.334840f, 1.373954f,
    1.414214f, 1.455653f, 1.498307f, 1.542211f, 1.587401f, 1.633915f,
    1.681793f, 1.731073f, 1.781797f, 1.834008f, 1.887749f, 1.943064f,
};

#define HOST_MAX_SAMPLES 8u
#define HOST_MAX_TABLES  4u
#define HOST_SAMPLE_FRAMES 100000u
#define HOST_TABLE_FRAMES 4u
#define HOST_TABLE_LEN 32u

static int16_t host_samples[HOST_MAX_SAMPLES][HOST_SAMPLE_FRAMES];
static int16_t host_tables[HOST_MAX_TABLES]
                           [HOST_TABLE_FRAMES * HOST_TABLE_LEN];
static uint8_t host_sample_count;
static uint8_t host_table_count;
static uint8_t host_stock_valid;
static uint32_t host_generation = 1u;
static int16_t host_silence[8];
static uint8_t host_gain_code;      /* gain code of every sample (A6) */
static uint8_t host_table_bits = 16u; /* stored depth of every table (A6) */
static uint32_t host_rng_seed = 0x12345678u;

const void *host_flash_ptr(uint32_t address)
{
    if (address >= HOST_SAMPLE_FLASH_BASE &&
        address < HOST_SAMPLE_FLASH_BASE + (uint32_t)sizeof(host_samples))
        return (const uint8_t *)host_samples +
               (address - HOST_SAMPLE_FLASH_BASE);
    if (address >= HOST_TABLE_FLASH_BASE &&
        address < HOST_TABLE_FLASH_BASE + (uint32_t)sizeof(host_tables))
        return (const uint8_t *)host_tables +
               (address - HOST_TABLE_FLASH_BASE);
    if (address >= HOST_SILENCE_FLASH)
        return host_silence;
    return 0;
}

void host_catalogue_reset(void)
{
    unsigned i;

    host_sample_count = 0u;
    host_table_count = 0u;
    host_stock_valid = 0u;
    host_gain_code = 0u;
    host_table_bits = 16u;
    host_generation++;
    if (host_generation == 0u)
        host_generation = 1u;
    for (i = 0u; i < HOST_MAX_SAMPLES; i++)
        for (unsigned j = 0u; j < HOST_SAMPLE_FRAMES; j++)
            host_samples[i][j] = (int16_t)((j * 37u + i * 11u) & 0x7fffu);
    for (i = 0u; i < HOST_MAX_TABLES; i++)
        for (unsigned j = 0u; j < HOST_TABLE_FRAMES * HOST_TABLE_LEN; j++)
            host_tables[i][j] = (int16_t)((int)j * (int)(i + 1u) * 13 - 500);
}

void host_catalogue_set(uint8_t samples, uint8_t tables, uint8_t stock)
{
    host_sample_count = (samples > HOST_MAX_SAMPLES) ? HOST_MAX_SAMPLES : samples;
    host_table_count = (tables > HOST_MAX_TABLES) ? HOST_MAX_TABLES : tables;
    host_stock_valid = stock;
    host_generation++;
    if (host_generation == 0u)
        host_generation = 1u;
}

void sampleMemory_init(void) {}
void sampleMemory_refresh(void) { host_catalogue_reset(); }
uint8_t sampleMemory_catalogueState(void) { return SAMPLE_CATALOGUE_V2; }
uint32_t sampleMemory_getGeneration(void) { return host_generation; }
uint8_t sampleMemory_getNumSamples(void) { return host_sample_count; }
uint8_t sampleMemory_getNumWavetables(void) { return host_table_count; }
uint8_t sampleMemory_stockValid(void) { return host_stock_valid; }

SampleInfo sampleMemory_getSampleInfo(uint8_t index)
{
    SampleInfo info;
    const unsigned frames = (HOST_SAMPLE_FRAMES - 4u);

    memset(&info, 0, sizeof(info));
    if (index >= host_sample_count) {
        info.offset = HOST_SILENCE_FLASH;
        return info;
    }
    info.size = frames;
    info.offset = HOST_SAMPLE_FLASH_BASE +
                  (uint32_t)index * (uint32_t)sizeof(host_samples[0]);
    info.gain_code = host_gain_code;
    return info;
}

uint8_t sampleMemory_getWavetableInfo(uint8_t index, WavetableInfo *out)
{
    if (!out || index >= host_table_count)
        return 0u;
    out->offset = HOST_TABLE_FLASH_BASE +
                  (uint32_t)index * (uint32_t)sizeof(host_tables[0]);
    out->frame_len = HOST_TABLE_LEN;
    out->frame_count = HOST_TABLE_FRAMES;
    out->bits = host_table_bits;
    return 1u;
}

void sampleMemory_getDisplayName(uint8_t index, char name[SAMPLE_DISPLAY_NAME_LEN + 1u])
{
    (void)index;
    if (name) {
        memcpy(name, "host    ", SAMPLE_DISPLAY_NAME_LEN);
        name[SAMPLE_DISPLAY_NAME_LEN] = '\0';
    }
}

void sampleMemory_getWavetableName(uint8_t index, char name[SAMPLE_DISPLAY_NAME_LEN + 1u])
{
    (void)index;
    if (name) {
        memcpy(name, "table   ", SAMPLE_DISPLAY_NAME_LEN);
        name[SAMPLE_DISPLAY_NAME_LEN] = '\0';
    }
}

int16_t GetRngValue(void)
{
    return (int16_t)golden_rand(&host_rng_seed);
}

/*
 * Test hooks added for the S085 assessment tests (A6, A7).
 *
 * host_set_gain_code(): the catalogue gain code every sample reports (the
 *     smp/slp gain test). host_set_table_bits(): 8 or 16, the stored depth
 *     every wavetable reports; with 8 the renderer reads the same host bytes
 *     as int8 (the 8-bit wtb path). Both bump the generation so bound
 *     oscillators re-read the entry. host_rng_reset(): restarts the noise
 *     random sequence so the frozen and current noise loops see the same
 *     values (osc_s0).
 */
void host_set_gain_code(uint8_t code)
{
    host_gain_code = code;
    host_generation++;
}

void host_set_table_bits(uint8_t bits)
{
    host_table_bits = bits;
    host_generation++;
}

void host_rng_reset(void)
{
    host_rng_seed = 0x12345678u;
}

const int16_t *host_sample_data(uint8_t index)
{
    return host_samples[index];
}

const int16_t *host_table_data(uint8_t index)
{
    return host_tables[index];
}

uint8_t modNode_getWaveInterpEnabled(void) { return 0u; }
uint32_t modNode_getWaveInterpGeneration(void) { return 0u; }

void bufferTool_addBuffersSaturating(int16_t *a, int16_t *b, const uint8_t size)
{
    uint8_t i;

    for (i = 0u; i < size; i++) {
        const int32_t v = (int32_t)a[i] + (int32_t)b[i];
        a[i] = (int16_t)((v > 32767) ? 32767 : ((v < -32768) ? -32768 : v));
    }
}

void SVF_recalcFreq(ResonantFilter *filter) { (void)filter; }
void SVF_recalcFreqFrom(ResonantFilter *filter, float fUsed)
{
    (void)filter;
    (void)fUsed;
}
