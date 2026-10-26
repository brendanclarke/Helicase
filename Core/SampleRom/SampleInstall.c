/*
 * SampleInstall.c — catalogue v2 writer (S084 VX2 VX2-2.2).
 *
 * Programs item data upward from SAMPLE_DATA_START_ADDRESS one 32-bit word at
 * a time, writes a 20 B catalogue entry as each item finishes, and commits the
 * header with its magic last. Everything here runs mode-switched with audio
 * suspended; there is no foreground or DSP caller.
 *
 *  Modified on: 25.10.2026
 * ------------------------------------------------------------------------------------------------------------------------
 *  Modifications Copyright 2026 Brendan Clarke
 *  brendanpaulclarke@gmail.com
 *  https://www.brendanclarke.com
 * ------------------------------------------------------------------------------------------------------------------------
 *  The modifications to this file are part of the LXR02 Open-Source software.
 *  The same license and restrictions on use for the LXR software apply.
 * ------------------------------------------------------------------------------------------------------------------------
 */

#include "SampleInstall.h"
#include "SampleMemory.h"
#include "sampleFlash.h"
#include "StockWaveGen.h"
#include <math.h>
#include <string.h>

/*
 * Install state (S084 VX2 VX2-2.2).
 *
 * What:       the next data address, the open item (start, stored size, name,
 *             format, wavetable frame length and count), the word carry, the
 *             source-frame carry for split reads, the peak of the open sample
 *             and the committed counts.
 * Why:        the walker streams file bytes straight to flash; this is the
 *             only install scratch. It measures 44 B (the schedule's estimate
 *             was 32 B; the extra 12 B keep the item name and the wavetable
 *             frame geometry so an entry is programmed only at finish). The
 *             public sample_install_fmt_t is compacted to four bytes here
 *             because rate is not needed after acceptance.
 * Inputs:     the begin/write/finish calls.  Outputs: flash programming and
 *             the reportable counts.
 * Accessors:  every function in this file.
 * Affiliates: sampleFlash_writeBytes(), StockWaveGen.c, SampleMemory.c.
 */
static struct {
    uint32_t write_addr;       /* next data word address (4-aligned) */
    uint32_t item_start;       /* first byte of the open item */
    uint32_t item_bytes;       /* stored bytes expected for the item */
    char     item_name[SAMPLE_DISPLAY_NAME_LEN];
    uint8_t  out_carry[4];     /* stored bytes waiting for a full word */
    uint8_t  in_carry[6];      /* one split source frame */
    uint8_t  out_carry_n;
    uint8_t  in_carry_n;
    uint16_t peak;             /* max |s| of the open sample */
    uint16_t frame_len;        /* open wavetable: points per frame */
    uint8_t  frame_count;      /* open wavetable: frames */
    uint8_t  flags;            /* open kind (bits 0..1) | stock flag */
    uint8_t  samples;
    uint8_t  wavetables;
    uint8_t  fmt_channels;
    uint8_t  fmt_bits;
    uint8_t  fmt_block;
    uint8_t  fmt_stored;
} sample_install;

_Static_assert(sizeof(sample_install) == 44u,
               "SampleInstall state is the approved install scratch");

#define INSTALL_OPEN_MASK      0x03u
#define INSTALL_OPEN_NONE      0u
#define INSTALL_OPEN_SAMPLE    1u
#define INSTALL_OPEN_WAVETABLE 2u
#define INSTALL_FLAG_STOCK     0x80u

#define INSTALL_BYTES_PER_SAMPLE 2u

static uint8_t sampleInstall_openKind(void)
{
    return (uint8_t)(sample_install.flags & INSTALL_OPEN_MASK);
}

static void sampleInstall_setOpenKind(uint8_t kind)
{
    sample_install.flags =
        (uint8_t)((sample_install.flags & ~INSTALL_OPEN_MASK) |
                  (kind & INSTALL_OPEN_MASK));
}

/*
 * Stored bytes appended to the open item so far (S084 VX2 VX2-2.2).
 *
 * What:       programmed words since item_start plus the pending carry.
 * Why:        the converters stop at item_bytes so a chunk longer than the
 *             declared frames is ignored.
 * Inputs:     none.  Outputs: byte count.
 * Accessors:  sampleInstall_writeSampleData(), _writeFrameData().
 */
static uint32_t sampleInstall_itemWritten(void)
{
    return (sample_install.write_addr - sample_install.item_start) +
           (uint32_t)sample_install.out_carry_n;
}

static int sampleInstall_putByte(uint8_t b)
{
    sample_install.out_carry[sample_install.out_carry_n++] = b;
    if (sample_install.out_carry_n == 4u) {
        int rc = sampleFlash_writeBytes(&sample_install.write_addr,
                                        sample_install.out_carry, 4u);

        if (rc != 0)
            return rc;
        sample_install.out_carry_n = 0u;
    }
    return 0;
}

static int sampleInstall_putBytes(const uint8_t *p, uint32_t n)
{
    while (n-- != 0u) {
        int rc = sampleInstall_putByte(*p++);

        if (rc != 0)
            return rc;
    }
    return 0;
}

/*
 * Flush the word carry (S084 VX2 VX2-2.2).
 *
 * What:       pads the pending bytes with zeros and programs one final word.
 * Why:        every item ends on a 4-byte boundary so the next entry starts
 *             aligned and this path never leaves a partial word.
 * Inputs:     none.  Outputs: 0 or the flash status.
 * Accessors:  sampleInstall_finishSample(), _finishWavetable().
 */
static int sampleInstall_flushCarry(void)
{
    while (sample_install.out_carry_n != 0u) {
        int rc = sampleInstall_putByte(0u);

        if (rc != 0)
            return rc;
    }
    return 0;
}

/*
 * Peak-normalisation code for one sample (S084 VX2 VX2-2.2).
 *
 * What:       floor(24 * log2(32767 / peak)), clamped to 0..255; peak 0
 *             gives 0. The floor keeps gain x peak <= 32767.
 * Why:        D10 / Q15: matched levels, never clipped by the gain.
 * Inputs:     peak 0..32768.  Outputs: code.
 * Accessors:  sampleInstall_finishSample().
 * Affiliates: sampleMemory_gainFromCode() (the inverse).
 */
static uint8_t sampleInstall_gainCode(uint16_t peak)
{
    float code;

    if (peak == 0u)
        return 0u;
    code = floorf((float)SAMPLE_GAIN_STEPS_PER_OCTAVE *
                  log2f(32767.0f / (float)peak));
    if (code < 0.0f)
        return 0u;
    if (code > 255.0f)
        return 255u;
    return (uint8_t)code;
}

static void sampleInstall_storeFormat(const sample_install_fmt_t *fmt)
{
    sample_install.fmt_channels = (uint8_t)fmt->channels;
    sample_install.fmt_bits = (uint8_t)fmt->bits;
    sample_install.fmt_block = (uint8_t)fmt->block_align;
    sample_install.fmt_stored = fmt->stored_bits;
}

uint8_t sampleInstall_acceptFormat(const uint8_t *fmt, uint32_t fmt_bytes,
                                   sample_install_kind_t kind,
                                   sample_install_fmt_t *out)
{
    uint16_t tag;
    uint16_t channels;
    uint16_t bits;
    uint32_t rate;
    uint8_t pcm;

    if (!fmt || !out || fmt_bytes < 16u)
        return 0u;
    tag = (uint16_t)(fmt[0] | ((uint16_t)fmt[1] << 8));
    channels = (uint16_t)(fmt[2] | ((uint16_t)fmt[3] << 8));
    rate = (uint32_t)fmt[4] | ((uint32_t)fmt[5] << 8) |
           ((uint32_t)fmt[6] << 16) | ((uint32_t)fmt[7] << 24);
    bits = (uint16_t)(fmt[14] | ((uint16_t)fmt[15] << 8));

    pcm = (uint8_t)(tag == 1u);
    if (tag == 0xfffeu) {
        if (fmt_bytes < 40u)
            return 0u;
        pcm = (uint8_t)((fmt[24] == 1u) && (fmt[25] == 0u));
    }
    if (!pcm)
        return 0u;

    if (kind == SAMPLE_INSTALL_KIND_SAMPLE) {
        if (channels != 1u || bits != 16u || rate != 44100u)
            return 0u;
    } else {
        if (channels < 1u || channels > 2u)
            return 0u;
        if (bits != 8u && bits != 16u && bits != 24u)
            return 0u;
    }

    memset(out, 0, sizeof(*out));
    out->channels = channels;
    out->bits = bits;
    out->rate = rate;
    out->block_align = (uint16_t)(channels * (bits / 8u));
    out->stored_bits = (uint8_t)((bits == 8u) ? 8u : 16u);
    return 1u;
}

uint32_t sampleInstall_storedBytes(const sample_install_fmt_t *fmt,
                                   uint32_t data_bytes)
{
    uint32_t frames;

    if (!fmt || fmt->block_align == 0u)
        return 0u;
    frames = data_bytes / fmt->block_align;
    return frames * (uint32_t)(fmt->stored_bits / 8u);
}

uint32_t sampleInstall_bytesFree(void)
{
    if (sample_install.write_addr >= SAMPLE_CATALOGUE_START_ADDRESS)
        return 0u;
    return SAMPLE_CATALOGUE_START_ADDRESS - sample_install.write_addr;
}

uint8_t sampleInstall_sampleCount(void)
{
    return sample_install.samples;
}

uint8_t sampleInstall_wavetableCount(void)
{
    return sample_install.wavetables;
}

/*
 * Begin a full install (S084 VX2 VX2-2.2).
 *
 * What:       erases sectors 7..11, zeroes the state, sets the write address
 *             to SAMPLE_DATA_START_ADDRESS.
 * Why:        every Load:[Samples] replaces the whole catalogue; the stock
 *             block and the item region are rewritten together.
 * Inputs:     sample flash.  Outputs: 0 or the flash status.
 * Accessors:  filesystem_installSamplesBlocking().
 */
int sampleInstall_begin(void)
{
    int rc;

    memset(&sample_install, 0, sizeof(sample_install));
    sample_install.write_addr = SAMPLE_DATA_START_ADDRESS;
    sampleInstall_setOpenKind(INSTALL_OPEN_NONE);
    sampleFlash_init();
    rc = sampleFlash_eraseAllSamples();
    return rc;
}

/*
 * Generate and program the stock-wave block (S084 VX2 VX2-2.3).
 *
 * What:       runs the additive generator into erased flash at the stock
 *             block base and records that the commit may advertise it.
 * Why:        W4: the new stock waves exist only after an install, so the
 *             block is always matched to this firmware's recipes.
 * Inputs:     the recipes (StockWaveRecipes.c).  Outputs: 0 or the
 *             sampleFlash status.
 * Accessors:  filesystem_installSamplesBlocking().
 */
int sampleInstall_writeStockBlock(void)
{
    int rc = stockWaveGen_programAll(SAMPLE_STOCK_START_ADDRESS);

    if (rc == 0)
        sample_install.flags |= INSTALL_FLAG_STOCK;
    return rc;
}

int sampleInstall_beginSample(const char name[SAMPLE_DISPLAY_NAME_LEN],
                              const sample_install_fmt_t *fmt,
                              uint32_t data_bytes)
{
    uint32_t stored;
    uint32_t needed;

    if (!fmt || !name)
        return -1;
    if (sampleInstall_openKind() != INSTALL_OPEN_NONE)
        return -1;
    if (sample_install.samples >= SAMPLE_MAX_COUNT)
        return -2;
    if (fmt->block_align == 0u || fmt->stored_bits != 16u)
        return -3;

    stored = sampleInstall_storedBytes(fmt, data_bytes);
    if (stored == 0u)
        return -3;
    needed = (stored + 4u + 3u) & ~3u;  /* data, interpolation pad, align */
    if (needed > sampleInstall_bytesFree())
        return -3;

    sample_install.item_start = sample_install.write_addr;
    sample_install.item_bytes = stored;
    memcpy(sample_install.item_name, name, SAMPLE_DISPLAY_NAME_LEN);
    sampleInstall_storeFormat(fmt);
    sample_install.peak = 0u;
    sample_install.out_carry_n = 0u;
    sample_install.in_carry_n = 0u;
    sampleInstall_setOpenKind(INSTALL_OPEN_SAMPLE);
    return 0;
}

/*
 * Program 16-bit sample data (S084 VX2 VX2-2.2).
 *
 * What:       passes the source frames through unchanged, tracks the peak of
 *             the open sample and stops at item_bytes.
 * Why:        samples keep the v1 form; only the peak-gain byte is new (Q15).
 * Inputs:     data, bytes (any count).  Outputs: flash bytes; peak.
 * Accessors:  filesystem_installCopy().
 * Affiliates: sampleInstall_gainCode(), sampleInstall_putByte().
 */
int sampleInstall_writeSampleData(const uint8_t *data, uint32_t bytes)
{
    if (!data || sampleInstall_openKind() != INSTALL_OPEN_SAMPLE)
        return -1;

    while (bytes-- != 0u) {
        uint8_t b = *data++;

        sample_install.in_carry[sample_install.in_carry_n++] = b;
        if (sample_install.in_carry_n < 2u)
            continue;
        sample_install.in_carry_n = 0u;
        if (sampleInstall_itemWritten() >= sample_install.item_bytes)
            continue;
        {
            const uint8_t lo = sample_install.in_carry[0];
            const uint8_t hi = sample_install.in_carry[1];
            int16_t s = (int16_t)((uint16_t)lo | ((uint16_t)hi << 8));
            uint16_t magnitude = (uint16_t)((s < 0) ? -s : s);
            int rc;

            if (magnitude > sample_install.peak)
                sample_install.peak = magnitude;
            rc = sampleInstall_putByte(lo);
            if (rc != 0)
                return rc;
            rc = sampleInstall_putByte(hi);
            if (rc != 0)
                return rc;
        }
    }
    return 0;
}

/*
 * Convert and program wavetable frame bytes (S084 VX2 VX2-2.2).
 *
 * What:       consumes source bytes in any split, rebuilding whole source
 *             frames through in_carry; converts each frame to the stored
 *             form (8-bit signed or 16-bit, mono: stereo averaged; 24-bit
 *             rounded to 16) and appends it through the word writer.
 *             Stops counting at item_bytes, so trailing chunk bytes beyond
 *             frame_len x frame_count are ignored.
 * Why:        W1: frames are stored as supplied unless the oscillator cannot
 *             read the form; every conversion keeps or reduces the size.
 * Inputs:     data, bytes (any count), the open table's fmt.
 * Outputs:    flash bytes; the item byte count.
 * Accessors:  filesystem_installWavetable() (VX2-3.8).
 * Affiliates: sampleInstall_acceptFormat(), sampleInstall_putByte().
 */
int sampleInstall_writeFrameData(const uint8_t *data, uint32_t bytes)
{
    const uint8_t *frame = sample_install.in_carry;
    const uint8_t block = sample_install.fmt_block;

    if (!data || sampleInstall_openKind() != INSTALL_OPEN_WAVETABLE ||
        block == 0u || block > 6u)
        return -1;

    while (bytes-- != 0u) {
        uint8_t b = *data++;

        if (sample_install.in_carry_n >= block)
            sample_install.in_carry_n = 0u;
        sample_install.in_carry[sample_install.in_carry_n++] = b;
        if (sample_install.in_carry_n < block)
            continue;
        sample_install.in_carry_n = 0u;
        if (sampleInstall_itemWritten() >= sample_install.item_bytes)
            continue;
        {
            const uint8_t channels = sample_install.fmt_channels;
            const uint8_t bits = sample_install.fmt_bits;
            int32_t a;
            int32_t b2;
            int32_t value;
            int rc;

            if (bits == 8u) {
                a = (int32_t)(int8_t)(frame[0] ^ 0x80u);
                b2 = (channels == 2u)
                    ? (int32_t)(int8_t)(frame[1] ^ 0x80u) : a;
                value = (a + b2) >> 1;
                rc = sampleInstall_putByte((uint8_t)(int8_t)value);
            } else if (bits == 16u) {
                a = (int32_t)(int16_t)((uint16_t)frame[0] |
                                       ((uint16_t)frame[1] << 8));
                b2 = (channels == 2u)
                    ? (int32_t)(int16_t)((uint16_t)frame[2] |
                                         ((uint16_t)frame[3] << 8)) : a;
                value = (a + b2) >> 1;
                rc = sampleInstall_putByte((uint8_t)((uint16_t)value & 0xffu));
                if (rc == 0)
                    rc = sampleInstall_putByte(
                        (uint8_t)(((uint16_t)value >> 8) & 0xffu));
            } else {
                int32_t l = (int32_t)((uint32_t)frame[0] |
                                      ((uint32_t)frame[1] << 8) |
                                      ((uint32_t)frame[2] << 16));
                int32_t r;

                if (l & 0x00800000)
                    l |= (int32_t)0xff000000u;
                r = l;
                if (channels == 2u) {
                    r = (int32_t)((uint32_t)frame[3] |
                                  ((uint32_t)frame[4] << 8) |
                                  ((uint32_t)frame[5] << 16));
                    if (r & 0x00800000)
                        r |= (int32_t)0xff000000u;
                }
                a = (l + 128) >> 8;
                b2 = (r + 128) >> 8;
                if (a > 32767) a = 32767;
                if (a < -32768) a = -32768;
                if (b2 > 32767) b2 = 32767;
                if (b2 < -32768) b2 = -32768;
                value = (a + b2) >> 1;
                rc = sampleInstall_putByte((uint8_t)((uint16_t)value & 0xffu));
                if (rc == 0)
                    rc = sampleInstall_putByte(
                        (uint8_t)(((uint16_t)value >> 8) & 0xffu));
            }
            if (rc != 0)
                return rc;
        }
    }
    return 0;
}

/*
 * Finish one sample and program its catalogue entry
 * (S084 VX2 VX2-2.2).
 *
 * What:       appends the 4-byte zero pad (the interpolation guard v1 also
 *             had), flushes the word carry, and writes the 20 B entry with
 *             the peak-normalisation gain byte.
 * Why:        samples as today plus the Q15 gain; the pad keeps the last
 *             frame's interpolation inside owned flash.
 * Inputs:     the open sample.  Outputs: 0, -4 when fewer bytes than
 *             declared were written (the item is aborted, no entry; S084 VX2
 *             review fix F8), or a flash status.
 * Accessors:  filesystem_installSampleFiles().
 */
int sampleInstall_finishSample(void)
{
    static const uint8_t zero_pad[4] = { 0, 0, 0, 0 };
    sample_catalogue_sample_t entry;
    uint32_t addr;
    int rc;

    if (sampleInstall_openKind() != INSTALL_OPEN_SAMPLE)
        return -1;
    /* S084 VX2 review fix F8: refuse an entry for data that never arrived. */
    if (sampleInstall_itemWritten() != sample_install.item_bytes) {
        sampleInstall_abortItem();
        return -4;
    }
    rc = sampleInstall_putBytes(zero_pad, sizeof(zero_pad));
    if (rc == 0)
        rc = sampleInstall_flushCarry();
    if (rc != 0) {
        sampleInstall_setOpenKind(INSTALL_OPEN_NONE);
        return rc;
    }

    memset(&entry, 0xff, sizeof(entry));
    entry.offset = sample_install.item_start;
    entry.frames = sample_install.item_bytes / INSTALL_BYTES_PER_SAMPLE;
    memcpy(entry.name, sample_install.item_name, SAMPLE_DISPLAY_NAME_LEN);
    entry.gain_code = sampleInstall_gainCode(sample_install.peak);

    addr = SAMPLE_CATALOGUE_SAMPLES_ADDRESS +
           (uint32_t)sample_install.samples * sizeof(entry);
    rc = sampleFlash_writeBytes(&addr, (const uint8_t *)&entry,
                                (uint32_t)sizeof(entry));
    sampleInstall_setOpenKind(INSTALL_OPEN_NONE);
    if (rc != 0)
        return rc;
    sample_install.samples++;
    return 0;
}

/*
 * Abandon the open item (S084 VX2 review fix F7).
 *
 * What:       closes the open sample or wavetable without programming its
 *             catalogue entry: the source and word carries are discarded and
 *             the open kind returns to none. write_addr already points at the
 *             next unprogrammed word (carried bytes are never programmed), so
 *             the next item starts cleanly after the abandoned bytes, which
 *             stay unused (and unreferenced) until the next erase.
 * Why:        a read failure in one file used to abort the whole install,
 *             leaving sample flash erased; now that file (or that table) is
 *             skipped and the install continues. Calling it with nothing open
 *             is harmless.
 * Inputs:     the install state.  Outputs: open kind none.
 * Accessors:  filesystem.c (read failures), the finish calls (F8).
 * Affiliates: sampleInstall_begin*(), sampleInstall_finish*().
 */
void sampleInstall_abortItem(void)
{
    sample_install.out_carry_n = 0u;
    sample_install.in_carry_n = 0u;
    sample_install.item_bytes = 0u;
    sampleInstall_setOpenKind(INSTALL_OPEN_NONE);
}

/*
 * Set the source format of the next wavetable frame
 * (S084 VX2 review fix F4).
 *
 * What:       replaces the open table's source decoding parameters
 *             (channels, source bits, block size) with this frame file's
 *             own, at a frame boundary only, provided it stores to the same
 *             depth as the table (8- or 16-bit).
 * Why:        beginWavetable() stored the first file's source format and
 *             writeFrameData() decoded every frame with it, so a stereo or
 *             24-bit frame after a mono 16-bit first frame (same stored depth,
 *             same length, so accepted) was decoded with the wrong block size
 *             and corrupted the rest of the table. W1 allows any accepted
 *             form per file; only the stored length and depth must agree.
 * Inputs:     fmt of the next frame file.
 * Outputs:    0 ok; -1 nothing open, mid-frame, or a different stored depth.
 * Accessors:  filesystem_installWavetable() Pass B, before each copy.
 * Affiliates: sampleInstall_acceptFormat(), sampleInstall_writeFrameData().
 */
int sampleInstall_setFrameFormat(const sample_install_fmt_t *fmt)
{
    if (!fmt || sampleInstall_openKind() != INSTALL_OPEN_WAVETABLE)
        return -1;
    if (sample_install.in_carry_n != 0u)
        return -1;
    if (fmt->stored_bits != sample_install.fmt_stored ||
        fmt->block_align == 0u || fmt->block_align > 6u)
        return -1;
    sampleInstall_storeFormat(fmt);
    return 0;
}

int sampleInstall_beginWavetable(const char name[SAMPLE_DISPLAY_NAME_LEN],
                                 const sample_install_fmt_t *fmt,
                                 uint16_t frame_len, uint8_t frame_count)
{
    uint32_t stored;
    uint32_t needed;

    if (!fmt || !name)
        return -1;
    if (sampleInstall_openKind() != INSTALL_OPEN_NONE)
        return -1;
    if (sample_install.wavetables >= WAVETABLE_MAX_COUNT)
        return -2;
    if (frame_len < 2u || frame_count == 0u ||
        frame_count > WAVETABLE_MAX_FRAMES)
        return -3;
    if (fmt->stored_bits != 8u && fmt->stored_bits != 16u)
        return -3;

    stored = (uint32_t)frame_len * (uint32_t)frame_count *
             (uint32_t)(fmt->stored_bits / 8u);
    needed = (stored + 3u) & ~3u;
    if (stored == 0u || needed > sampleInstall_bytesFree())
        return -3;

    sample_install.item_start = sample_install.write_addr;
    sample_install.item_bytes = stored;
    memcpy(sample_install.item_name, name, SAMPLE_DISPLAY_NAME_LEN);
    sampleInstall_storeFormat(fmt);
    sample_install.peak = 0u;
    sample_install.frame_len = frame_len;
    sample_install.frame_count = frame_count;
    sample_install.out_carry_n = 0u;
    sample_install.in_carry_n = 0u;
    sampleInstall_setOpenKind(INSTALL_OPEN_WAVETABLE);
    return 0;
}

int sampleInstall_finishWavetable(void)
{
    sample_catalogue_wavetable_t entry;
    uint32_t addr;
    int rc;

    if (sampleInstall_openKind() != INSTALL_OPEN_WAVETABLE)
        return -1;
    /* S084 VX2 review fix F8: every declared frame must have been written. */
    if (sampleInstall_itemWritten() != sample_install.item_bytes) {
        sampleInstall_abortItem();
        return -4;
    }
    rc = sampleInstall_flushCarry();
    if (rc != 0) {
        sampleInstall_setOpenKind(INSTALL_OPEN_NONE);
        return rc;
    }

    memset(&entry, 0xff, sizeof(entry));
    entry.offset = sample_install.item_start;
    entry.frame_len = sample_install.frame_len;
    entry.frame_count = sample_install.frame_count;
    entry.bits = sample_install.fmt_stored;
    memcpy(entry.name, sample_install.item_name, SAMPLE_DISPLAY_NAME_LEN);

    addr = SAMPLE_CATALOGUE_WAVETABLES_ADDRESS +
           (uint32_t)sample_install.wavetables * sizeof(entry);
    rc = sampleFlash_writeBytes(&addr, (const uint8_t *)&entry,
                                (uint32_t)sizeof(entry));
    sampleInstall_setOpenKind(INSTALL_OPEN_NONE);
    if (rc != 0)
        return rc;
    sample_install.wavetables++;
    return 0;
}

/*
 * Commit catalogue v2 (S084 VX2 VX2-2.2).
 *
 * What:       programs header bytes 4..31 (counts, stock flag, data_end,
 *             stock_start, reserved = 0xFF), then the magic word; invalidates
 *             the D-cache over sectors 7..11 and refreshes SampleMemory.
 * Why:        the magic last makes a partial install read as EMPTY or STALE,
 *             never as a half catalogue.
 * Inputs:     the completed install state.  Outputs: 0 or a flash status.
 * Accessors:  filesystem_installSamplesBlocking().
 */
int sampleInstall_commit(void)
{
    sample_catalogue_header_t header;
    uint32_t addr = SAMPLE_CATALOGUE_START_ADDRESS + 4u;
    int rc;

    if (sampleInstall_openKind() != INSTALL_OPEN_NONE)
        return -1;

    memset(&header, 0xff, sizeof(header));
    header.version = SAMPLE_CATALOGUE_VERSION;
    header.entry_bytes = (uint16_t)sizeof(sample_catalogue_sample_t);
    header.sample_count = sample_install.samples;
    header.wavetable_count = sample_install.wavetables;
    header.stock_valid =
        (uint8_t)((sample_install.flags & INSTALL_FLAG_STOCK) ? 1u : 0u);
    header.reserved0 = 0u;
    header.data_end = sample_install.write_addr;
    header.stock_start = SAMPLE_STOCK_START_ADDRESS;

    rc = sampleFlash_writeBytes(&addr,
                                (const uint8_t *)&header + 4u,
                                (uint32_t)sizeof(header) - 4u);
    if (rc != 0)
        return rc;
    rc = sampleFlash_writeWord(SAMPLE_CATALOGUE_START_ADDRESS,
                               SAMPLE_CATALOGUE_MAGIC);
    if (rc != 0)
        return rc;

    sampleFlash_invalidateRange(SAMPLE_ROM_START_ADDRESS,
                                SAMPLE_FLASH_END_ADDRESS -
                                    SAMPLE_ROM_START_ADDRESS);
    sampleMemory_refresh();
    return 0;
}
