/*
 * SampleInstall.c - modal sample/wavetable installer core (S087 stage C).
 *
 * What:       turns accepted WAV data into catalogue v2 items: block
 *             conversion (24->16 bit, stereo->mono, 8->16 for samples), the
 *             zero pad after each sample, the gain code from the peak, loop
 *             points from smpl, one entry per finished item, and the header
 *             with its magic written last.
 * Why:        S087 P0 §5.2, A11, A15; install-time work removes per-sample
 *             work and keeps the catalogue valid only after a completed
 *             install.
 * Inputs:     filesystem.c's install walker (fmt bytes, data blocks, smpl).
 * Outputs:    sample flash items and catalogue v2.
 * Accessors:  filesystem_installUserContentBlocking().
 * Affiliates: SampleMemory.h (layout), sampleFlash.c (program),
 *             test_wav_install.c and test_catalogue.c.
 * RAM:        the 128-byte output staging buffer, the write cursor and item
 *             bookkeeping (part of R-C3).
 */
#include "SampleInstall.h"
#include "sampleFlash.h"
#include <string.h>

#define SAMPLE_INSTALL_OUT_SIZE 128u

static uint8_t  out_buf[SAMPLE_INSTALL_OUT_SIZE];
static uint16_t out_len;
static uint32_t write_addr;
static uint32_t item_start;
static uint32_t frames_total;
static uint32_t frames_written;
static uint8_t  sample_items;
static uint8_t  wavetable_items;
static uint8_t  item_open;        /* 1 sample, 2 wavetable */
static uint8_t  item_stored_bits;
static uint8_t  item_channels;
static uint8_t  item_source_bits;
static uint16_t item_frame_len;
static char     item_name[SAMPLE_DISPLAY_NAME_LEN];
static int32_t  item_peak;

static uint16_t sampleInstall_le16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t sampleInstall_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

uint8_t sampleInstall_acceptFormat(const uint8_t *fmt, uint32_t bytes,
                                   sample_install_kind_t kind,
                                   sample_install_fmt_t *out)
{
    uint16_t tag;
    uint16_t channels;
    uint16_t bits;

    if (!fmt || !out || bytes < 16u)
        return 0u;
    tag = sampleInstall_le16(fmt);
    channels = sampleInstall_le16(fmt + 2u);
    bits = sampleInstall_le16(fmt + 14u);
    if (tag == 0xFFFEu) {
        if (bytes < 40u)
            return 0u;
        /* Subformat GUID starts with the 16-bit format tag. */
        if (sampleInstall_le16(fmt + 24u) != 1u)
            return 0u;
        tag = 1u;
    }
    if (tag != 1u)
        return 0u;
    if (channels != 1u && channels != 2u)
        return 0u;
    if (bits != 8u && bits != 16u && bits != 24u)
        return 0u;
    memset(out, 0, sizeof(*out));
    out->format_tag = tag;
    out->channels = channels;
    out->sample_rate = sampleInstall_le32(fmt + 4u);
    out->block_align = sampleInstall_le16(fmt + 12u);
    out->bits = bits;
    out->stored_bits = (kind == SAMPLE_INSTALL_KIND_SAMPLE) ? 16u : bits;
    return 1u;
}

static int sampleInstall_flush(void)
{
    uint32_t whole = out_len & ~3u;
    int rc = 0;

    if (whole != 0u)
        rc = sampleFlash_writeBytes(&write_addr, out_buf, whole);
    if (rc == 0 && (out_len & 3u) != 0u) {
        uint8_t tail[4] = { 0u, 0u, 0u, 0u };

        memcpy(tail, &out_buf[whole], (uint32_t)(out_len & 3u));
        /* Erased bytes read as 0xFF; sample data padding must be 0. */
        for (uint8_t i = (uint8_t)(out_len & 3u); i < 4u; i++)
            tail[i] = 0u;
        rc = sampleFlash_writeBytes(&write_addr, tail, 4u);
    }
    out_len = 0u;
    return rc;
}

static int sampleInstall_append(const uint8_t *data, uint32_t bytes)
{
    while (bytes != 0u) {
        uint32_t room = SAMPLE_INSTALL_OUT_SIZE - out_len;
        uint32_t take = bytes < room ? bytes : room;

        memcpy(&out_buf[out_len], data, take);
        out_len = (uint16_t)(out_len + take);
        data += take;
        bytes -= take;
        if (out_len == SAMPLE_INSTALL_OUT_SIZE &&
            sampleInstall_flush() != 0)
            return -1;
    }
    return 0;
}

static int32_t sampleInstall_sourceSample(const uint8_t *p, uint8_t bits)
{
    if (bits == 8u)
        return (int32_t)(((int32_t)p[0] - 128) << 8);
    if (bits == 16u)
        return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
    {
        int32_t v = (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                              ((uint32_t)p[2] << 16));

        if (v & 0x800000)
            v -= 0x1000000;
        return v >> 8;
    }
}

static void sampleInstall_notePeak(int32_t v)
{
    int32_t a = v < 0 ? -v : v;

    if (a > item_peak)
        item_peak = a;
}

static int32_t sampleInstall_mono(const uint8_t *p, uint8_t channels,
                                  uint8_t bits, uint8_t *frame_bytes)
{
    int32_t sum = sampleInstall_sourceSample(p, bits);

    if (channels == 2u)
        sum = (sum + sampleInstall_sourceSample(p + bits / 8u, bits)) / 2;
    *frame_bytes = (uint8_t)(channels * (bits / 8u));
    return sum;
}

uint8_t sampleInstall_beginSample(const char name[SAMPLE_DISPLAY_NAME_LEN],
                                  uint32_t frames,
                                  const sample_install_fmt_t *fmt)
{
    if (!fmt || item_open || sample_items >= SAMPLE_MAX_COUNT)
        return 0u;
    if (fmt->channels == 0u || fmt->bits == 0u)
        return 0u;
    if (frames > SAMPLE_MAX_FRAMES)
        frames = SAMPLE_MAX_FRAMES;
    if (frames == 0u)
        return 0u;
    if ((uint64_t)frames * 2u + 4u > sampleInstall_bytesFree())
        return 0u;
    item_start = write_addr;
    frames_total = frames;
    frames_written = 0u;
    item_stored_bits = 16u;
    item_channels = fmt->channels;
    item_source_bits = fmt->bits;
    item_peak = 0;
    out_len = 0u;
    item_open = 1u;
    memcpy(item_name, name, SAMPLE_DISPLAY_NAME_LEN);
    return 1u;
}

uint8_t sampleInstall_writeFrames(const uint8_t *src, uint32_t frames)
{
    if (item_open != 1u || !src)
        return 0u;
    while (frames-- != 0u) {
        uint8_t bytes;
        int32_t v = sampleInstall_mono(src, item_channels, item_source_bits,
                                       &bytes);
        uint8_t out[2];

        if (frames_written >= frames_total)
            break;
        sampleInstall_notePeak(v);
        out[0] = (uint8_t)(v & 0xff);
        out[1] = (uint8_t)((v >> 8) & 0xff);
        if (sampleInstall_append(out, 2u) != 0)
            return 0u;
        src += bytes;
        frames_written++;
    }
    return 1u;
}

uint8_t sampleInstall_finishSample(uint32_t loop_start, uint32_t loop_end,
                                   uint8_t has_loop)
{
    sample_catalogue_entry_t entry;
    static const uint8_t zeros[4] = { 0u, 0u, 0u, 0u };
    uint32_t entry_addr;

    if (item_open != 1u)
        return 0u;
    if (sampleInstall_flush() != 0)
        return 0u;
    /* Two zero frames after every sample (A4). */
    if (sampleFlash_writeBytes(&write_addr, zeros, 4u) != 0)
        return 0u;

    memset(&entry, 0, sizeof(entry));
    entry.offset = item_start;
    entry.frames = frames_total;
    if (has_loop && loop_start < loop_end && loop_end <= frames_total) {
        entry.loop_start = loop_start;
        entry.loop_end = loop_end;
        entry.flags = 1u;
    } else {
        entry.loop_start = 0u;
        entry.loop_end = frames_total;
    }
    memcpy(entry.name, item_name, SAMPLE_DISPLAY_NAME_LEN);
    entry.gain_code = sampleInstall_gainCode((uint32_t)item_peak);
    entry_addr = SAMPLE_CATALOGUE_START_ADDRESS +
                 sizeof(sample_catalogue_header_t) +
                 (uint32_t)sample_items * SAMPLE_ENTRY_BYTES;
    if (sampleFlash_writeBytes(&entry_addr, (const uint8_t *)&entry,
                               sizeof(entry)) != 0)
        return 0u;
    sample_items++;
    item_open = 0u;
    return 1u;
}

uint8_t sampleInstall_beginWavetable(const char name[SAMPLE_DISPLAY_NAME_LEN],
                                     uint16_t frame_len, uint8_t frame_count,
                                     uint8_t bits,
                                     const sample_install_fmt_t *fmt)
{
    if (!fmt || item_open || wavetable_items >= WAVETABLE_MAX_COUNT)
        return 0u;
    if (frame_len < WAVETABLE_FRAME_MIN || frame_len > WAVETABLE_FRAME_MAX)
        return 0u;
    if (frame_count == 0u || frame_count > WAVETABLE_MAX_FRAMES)
        return 0u;
    if (bits != 8u && bits != 16u)
        return 0u;
    if ((uint64_t)frame_len * frame_count * (bits / 8u) >
        sampleInstall_bytesFree())
        return 0u;
    item_start = write_addr;
    frames_total = frame_count;
    frames_written = 0u;
    item_stored_bits = bits;
    item_frame_len = frame_len;
    item_channels = fmt->channels;
    item_source_bits = fmt->bits;
    item_peak = 0;
    out_len = 0u;
    item_open = 2u;
    memcpy(item_name, name, SAMPLE_DISPLAY_NAME_LEN);
    return 1u;
}

uint8_t sampleInstall_writeFrame(const uint8_t *src, uint16_t frames)
{
    if (item_open != 2u || !src)
        return 0u;
    while (frames-- != 0u) {
        uint8_t bytes;
        int32_t v = sampleInstall_mono(src, item_channels, item_source_bits,
                                       &bytes);

        if (item_stored_bits == 8u) {
            /* 8-bit frames store signed int8 (0 = WAV's 128). */
            uint8_t out = (uint8_t)(v >> 8);

            if (sampleInstall_append(&out, 1u) != 0)
                return 0u;
        } else {
            uint8_t out[2];

            out[0] = (uint8_t)(v & 0xff);
            out[1] = (uint8_t)((v >> 8) & 0xff);
            if (sampleInstall_append(out, 2u) != 0)
                return 0u;
        }
        src += bytes;
    }
    frames_written++;
    return 1u;
}

uint8_t sampleInstall_finishWavetable(void)
{
    wavetable_catalogue_entry_t entry;
    uint32_t entry_addr;

    if (item_open != 2u)
        return 0u;
    if (sampleInstall_flush() != 0)
        return 0u;
    memset(&entry, 0, sizeof(entry));
    entry.offset = item_start;
    entry.frame_len = item_frame_len;
    entry.frame_count = (uint8_t)frames_written;
    entry.bits = item_stored_bits;
    memcpy(entry.name, item_name, SAMPLE_DISPLAY_NAME_LEN);
    entry_addr = SAMPLE_CATALOGUE_START_ADDRESS +
                 sizeof(sample_catalogue_header_t) +
                 SAMPLE_MAX_COUNT * SAMPLE_ENTRY_BYTES +
                 (uint32_t)wavetable_items * WAVETABLE_ENTRY_BYTES;
    if (sampleFlash_writeBytes(&entry_addr, (const uint8_t *)&entry,
                               sizeof(entry)) != 0)
        return 0u;
    wavetable_items++;
    item_open = 0u;
    return 1u;
}

void sampleInstall_abortItem(void)
{
    if (item_open) {
        write_addr = item_start;
        out_len = 0u;
        item_open = 0u;
    }
}

void sampleInstall_begin(void)
{
    sample_catalogue_header_t header;
    uint32_t addr;

    sampleFlash_init();
    (void)sampleFlash_eraseAllSamples();
    /* Mark a v2 install in progress: no magic, so it reads EMPTY if aborted. */
    memset(&header, 0, sizeof(header));
    header.magic = 0xFFFFFFFFu;
    header.version = SAMPLE_CATALOGUE_VERSION;
    header.sample_entry_bytes = SAMPLE_ENTRY_BYTES;
    header.wavetable_entry_bytes = WAVETABLE_ENTRY_BYTES;
    addr = SAMPLE_CATALOGUE_START_ADDRESS;
    (void)sampleFlash_writeBytes(&addr, (const uint8_t *)&header, sizeof(header));
    out_len = 0u;
    write_addr = SAMPLE_ROM_START_ADDRESS + 4u;
    item_start = write_addr;
    frames_total = 0u;
    frames_written = 0u;
    sample_items = 0u;
    wavetable_items = 0u;
    item_open = 0u;
    item_peak = 0;
}

uint32_t sampleInstall_bytesFree(void)
{
    if (write_addr >= SAMPLE_CATALOGUE_START_ADDRESS)
        return 0u;
    return SAMPLE_CATALOGUE_START_ADDRESS - write_addr;
}

uint8_t sampleInstall_commit(void)
{
    sample_catalogue_header_t header;
    uint32_t addr;
    int rc;

    if (item_open)
        return 0u;
    if (sampleInstall_flush() != 0)
        return 0u;
    memset(&header, 0, sizeof(header));
    header.magic = 0xFFFFFFFFu;
    header.version = SAMPLE_CATALOGUE_VERSION;
    header.sample_entry_bytes = SAMPLE_ENTRY_BYTES;
    header.wavetable_entry_bytes = WAVETABLE_ENTRY_BYTES;
    header.sample_count = sample_items;
    header.wavetable_count = wavetable_items;
    header.data_end = write_addr;
    addr = SAMPLE_CATALOGUE_START_ADDRESS;
    rc = sampleFlash_writeBytes(&addr, (const uint8_t *)&header, sizeof(header));
    if (rc != 0)
        return 0u;
    /* The magic is programmed last: an aborted install leaves no v2 catalogue. */
    if (sampleFlash_writeWord(SAMPLE_CATALOGUE_START_ADDRESS,
                              SAMPLE_CATALOGUE_MAGIC) != 0)
        return 0u;
    sampleFlash_invalidateRange(SAMPLE_ROM_START_ADDRESS,
                                SAMPLE_CATALOGUE_END_ADDRESS -
                                SAMPLE_ROM_START_ADDRESS);
    sampleMemory_refresh();
    return 1u;
}

uint8_t sampleInstall_gainCode(uint32_t peak)
{
    uint8_t code = 0u;

    if (peak == 0u)
        return 0u;
    while (code < 255u) {
        uint8_t next = (uint8_t)(code + 1u);
        float gain = sampleMemory_gainFromCode(next);

        if ((float)peak * gain > 32767.0f)
            break;
        code = next;
    }
    return code;
}
