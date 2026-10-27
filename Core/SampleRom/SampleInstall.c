#include "SampleInstall.h"
#include "SampleMemory.h"
#include "sampleFlash.h"
#include "StockWaveGen.h"
#include <math.h>
#include <string.h>

/*
 * SampleInstall.c (S085 C2.2).
 *
 * What:       converts accepted WAV forms and programs samples/wavetable
 *             frames plus one catalogue entry per item; the installer core
 *             knows nothing about the card.
 * Why:        plan §5 item 8; every Load:[Samples] is a full install.
 * Accessors:  filesystem_installSamplesBlocking() only (audio suspended).
 * Affiliates: SampleMemory.h, sampleFlash.c, StockWaveGen.c.
 */

/*
 * Install state (S085 C2.2).
 *
 * What:       the write pointer, the open item (start, expected stored bytes,
 *             name, source format, wavetable geometry), the word carry
 *             (programming is per 32-bit word), the source-frame carry (a
 *             frame can split across two 512 B reads), the open sample's peak
 *             and the committed counts.
 * Why:        approved install scratch (S085 §0.5, 44 B); entries are
 *             programmed only at finish, so the name and geometry are kept.
 * Writers:    the functions below.  Readers: the same, and the counters.
 * Affiliates: sampleFlash_writeBytes(), StockWaveGen.c, SampleMemory.c.
 */
static struct {
    uint32_t write_addr;       /* next data word (4-aligned) */
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
    uint8_t  flags;            /* open kind (bits 0..1) | INSTALL_FLAG_STOCK */
    uint8_t  samples;
    uint8_t  wavetables;
    uint8_t  fmt_channels;
    uint8_t  fmt_bits;
    uint8_t  fmt_block;
    uint8_t  fmt_stored;
} sample_install;

_Static_assert(sizeof(sample_install) == 44u,
               "SampleInstall state is the approved 44 B install scratch");

#define INSTALL_OPEN_MASK        0x03u
#define INSTALL_OPEN_NONE        0u
#define INSTALL_OPEN_SAMPLE      1u
#define INSTALL_OPEN_WAVETABLE   2u
#define INSTALL_FLAG_STOCK       0x80u
#define INSTALL_SAMPLE_PAD_BYTES 4u   /* two zero frames after each sample */

static uint8_t sampleInstall_openKind(void)
{
    return (uint8_t)(sample_install.flags & INSTALL_OPEN_MASK);
}

static void sampleInstall_setOpenKind(uint8_t kind)
{
    sample_install.flags = (uint8_t)((sample_install.flags &
        (uint8_t)~INSTALL_OPEN_MASK) | (kind & INSTALL_OPEN_MASK));
}

/*
 * Stored bytes appended to the open item (S085 C2.2).
 *
 * What:       programmed bytes since item_start plus the bytes in the word
 *             carry.
 * Why:        the writers stop at item_bytes; the finish calls refuse a short
 *             item (-4) so no entry describes missing data.
 */
static uint32_t sampleInstall_itemWritten(void)
{
    return (sample_install.write_addr - sample_install.item_start) +
           (uint32_t)sample_install.out_carry_n;
}

/*
 * Append one stored byte through the word carry (S085 C2.2).
 *
 * What:       collects four bytes, then programs one word and advances
 *             write_addr.
 * Outputs:    0 or a sampleFlash status.
 */
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

/* Pad the word carry with zeros so the next item starts 4-aligned. */
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
 * Peak-normalisation code (S085 C2.2).
 *
 * What:       floor(24 x log2(32767 / peak)), 0..255; peak 0 gives 0.
 * Why:        the floor keeps gain x peak <= 32767, so playback never clips
 *             from the normalisation.
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
        pcm = (uint8_t)(fmt[24] == 1u && fmt[25] == 0u);
    }
    if (!pcm)
        return 0u;
    if (kind == SAMPLE_INSTALL_KIND_SAMPLE) {
        if (channels != 1u || bits != 16u || rate != 44100u)
            return 0u;
    } else if (channels < 1u || channels > 2u ||
               (bits != 8u && bits != 16u && bits != 24u)) {
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
    if (!fmt || fmt->block_align == 0u)
        return 0u;
    return (data_bytes / fmt->block_align) * (uint32_t)(fmt->stored_bits / 8u);
}

uint32_t sampleInstall_bytesFree(void)
{
    return (sample_install.write_addr >= SAMPLE_CATALOGUE_START_ADDRESS)
        ? 0u : (SAMPLE_CATALOGUE_START_ADDRESS - sample_install.write_addr);
}

uint8_t sampleInstall_sampleCount(void)    { return sample_install.samples; }
uint8_t sampleInstall_wavetableCount(void) { return sample_install.wavetables; }

/*
 * Begin a full install (S085 C2.2).
 *
 * What:       clears the state, sets write_addr to SAMPLE_DATA_START_ADDRESS
 *             and erases sectors 7..11 (stock block, items and catalogue
 *             together; the reader sees EMPTY until the commit).
 * Outputs:    0 or a sampleFlash status.
 * Accessors:  filesystem_installSamplesBlocking().
 */
int sampleInstall_begin(void)
{
    memset(&sample_install, 0, sizeof(sample_install));
    sample_install.write_addr = SAMPLE_DATA_START_ADDRESS;
    sampleFlash_init();
    return sampleFlash_eraseAllSamples();
}

/*
 * Generate the stock block (S085 C2.2).
 *
 * What:       stockWaveGen_programAll() at SAMPLE_STOCK_START_ADDRESS; on
 *             success marks the block so the commit advertises it.
 * Why:        plan §4.3.4: every install regenerates the stock waves, so the
 *             block always matches this firmware's recipes.
 * Accessors:  filesystem_installSamplesBlocking().
 */
int sampleInstall_writeStockBlock(void)
{
    int rc = stockWaveGen_programAll(SAMPLE_STOCK_START_ADDRESS);

    if (rc == 0)
        sample_install.flags |= INSTALL_FLAG_STOCK;
    return rc;
}

/*
 * Open one sample item (S085 C2.2).
 *
 * What:       checks the state, the 128-sample limit (Q7) and the fit
 *             (data + 4 B interpolation pad, rounded to a word) and records
 *             the item.
 * Outputs:    0, -1 (state), -2 (limit), -3 (no fit / bad form).
 */
int sampleInstall_beginSample(const char name[SAMPLE_DISPLAY_NAME_LEN],
                              const sample_install_fmt_t *fmt,
                              uint32_t data_bytes)
{
    uint32_t stored;
    uint32_t needed;

    if (!fmt || !name || sampleInstall_openKind() != INSTALL_OPEN_NONE)
        return -1;
    if (sample_install.samples >= SAMPLE_MAX_COUNT)
        return -2;
    if (fmt->block_align == 0u || fmt->stored_bits != 16u)
        return -3;
    stored = sampleInstall_storedBytes(fmt, data_bytes);
    needed = (stored + INSTALL_SAMPLE_PAD_BYTES + 3u) & ~3u;
    if (stored == 0u || needed > sampleInstall_bytesFree())
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
 * Program 16-bit sample data (S085 C2.2).
 *
 * What:       pairs bytes into int16 frames (carry across calls), tracks the
 *             peak magnitude and appends up to item_bytes.
 * Outputs:    0, -1, or a sampleFlash status.
 * Accessors:  filesystem_installCopy().
 */
int sampleInstall_writeSampleData(const uint8_t *data, uint32_t bytes)
{
    if (!data || sampleInstall_openKind() != INSTALL_OPEN_SAMPLE)
        return -1;
    while (bytes-- != 0u) {
        sample_install.in_carry[sample_install.in_carry_n++] = *data++;
        if (sample_install.in_carry_n < 2u)
            continue;
        sample_install.in_carry_n = 0u;
        if (sampleInstall_itemWritten() >= sample_install.item_bytes)
            continue;
        {
            const uint8_t lo = sample_install.in_carry[0];
            const uint8_t hi = sample_install.in_carry[1];
            const int16_t s = (int16_t)((uint16_t)lo | ((uint16_t)hi << 8));
            const uint16_t mag = (uint16_t)((s < 0) ? -(int32_t)s : s);
            int rc;

            if (mag > sample_install.peak)
                sample_install.peak = mag;
            rc = sampleInstall_putByte(lo);
            if (rc == 0)
                rc = sampleInstall_putByte(hi);
            if (rc != 0)
                return rc;
        }
    }
    return 0;
}

/*
 * Finish one sample and program its entry (S085 C2.2).
 *
 * What:       refuses a short item (-4, aborted, no entry); appends the
 *             4-byte zero pad (the last frame's interpolation reads inside
 *             owned flash), flushes the carry, programs the 20 B entry with
 *             the gain code.
 * Outputs:    0, -1, -4, or a sampleFlash status.
 */
int sampleInstall_finishSample(void)
{
    static const uint8_t zero_pad[INSTALL_SAMPLE_PAD_BYTES] = { 0, 0, 0, 0 };
    sample_catalogue_sample_t entry;
    uint32_t addr;
    int rc;

    if (sampleInstall_openKind() != INSTALL_OPEN_SAMPLE)
        return -1;
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
    entry.frames = sample_install.item_bytes / 2u;
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
 * Open one wavetable item (S085 C2.2).
 *
 * What:       checks the 128-table limit, the geometry (frame_len >= 2,
 *             1..128 frames, 8 or 16 stored bits) and the fit, then records
 *             the item. The first frame's source format is stored;
 *             setFrameFormat() replaces it per frame file.
 * Outputs:    0, -1, -2, -3.
 */
int sampleInstall_beginWavetable(const char name[SAMPLE_DISPLAY_NAME_LEN],
                                 const sample_install_fmt_t *fmt,
                                 uint16_t frame_len, uint8_t frame_count)
{
    uint32_t stored;

    if (!fmt || !name || sampleInstall_openKind() != INSTALL_OPEN_NONE)
        return -1;
    if (sample_install.wavetables >= WAVETABLE_MAX_COUNT)
        return -2;
    if (frame_len < 2u || frame_count == 0u ||
        frame_count > WAVETABLE_MAX_FRAMES ||
        (fmt->stored_bits != 8u && fmt->stored_bits != 16u))
        return -3;
    stored = (uint32_t)frame_len * frame_count * (fmt->stored_bits / 8u);
    if (((stored + 3u) & ~3u) > sampleInstall_bytesFree())
        return -3;
    sample_install.item_start = sample_install.write_addr;
    sample_install.item_bytes = stored;
    memcpy(sample_install.item_name, name, SAMPLE_DISPLAY_NAME_LEN);
    sampleInstall_storeFormat(fmt);
    sample_install.frame_len = frame_len;
    sample_install.frame_count = frame_count;
    sample_install.out_carry_n = 0u;
    sample_install.in_carry_n = 0u;
    sampleInstall_setOpenKind(INSTALL_OPEN_WAVETABLE);
    return 0;
}

/*
 * Per-frame source format (S085 C2.2).
 *
 * What:       switches the open table's source decoding (channels, bits,
 *             block) to the next frame file's own format, at a frame boundary
 *             only, and refuses a different stored depth.
 * Why:        a table may mix mono/stereo and 16/24-bit frame files; only
 *             the stored depth and length must agree (S084 P2 F4).
 * Outputs:    0 or -1.
 * Accessors:  filesystem_installWavetable() before each frame copy.
 */
int sampleInstall_setFrameFormat(const sample_install_fmt_t *fmt)
{
    if (!fmt || sampleInstall_openKind() != INSTALL_OPEN_WAVETABLE ||
        sample_install.in_carry_n != 0u ||
        fmt->stored_bits != sample_install.fmt_stored ||
        fmt->block_align == 0u || fmt->block_align > 6u)
        return -1;
    sampleInstall_storeFormat(fmt);
    return 0;
}

/*
 * Convert and program frame bytes (S085 C2.2).
 *
 * What:       assembles source frames through in_carry and stores each as
 *             8-bit signed (unsigned WAV 8-bit flipped) or 16-bit; stereo is
 *             averaged to mono; 24-bit is rounded to 16-bit and saturated.
 *             Stops at item_bytes.
 * Why:        never expand (8-bit stays 8-bit); every conversion keeps or
 *             reduces the size.
 * Outputs:    0, -1, or a sampleFlash status.
 * Accessors:  filesystem_installCopy().
 */
int sampleInstall_writeFrameData(const uint8_t *data, uint32_t bytes)
{
    const uint8_t *f = sample_install.in_carry;
    const uint8_t block = sample_install.fmt_block;

    if (!data || sampleInstall_openKind() != INSTALL_OPEN_WAVETABLE ||
        block == 0u || block > 6u)
        return -1;
    while (bytes-- != 0u) {
        sample_install.in_carry[sample_install.in_carry_n++] = *data++;
        if (sample_install.in_carry_n < block)
            continue;
        sample_install.in_carry_n = 0u;
        if (sampleInstall_itemWritten() >= sample_install.item_bytes)
            continue;
        {
            const uint8_t stereo = (uint8_t)(sample_install.fmt_channels == 2u);
            int32_t a;
            int32_t b;
            int32_t v;
            int rc;

            if (sample_install.fmt_bits == 8u) {
                a = (int32_t)(int8_t)(f[0] ^ 0x80u);
                b = stereo ? (int32_t)(int8_t)(f[1] ^ 0x80u) : a;
                v = (a + b) >> 1;
                rc = sampleInstall_putByte((uint8_t)(int8_t)v);
            } else {
                if (sample_install.fmt_bits == 16u) {
                    a = (int16_t)((uint16_t)f[0] | ((uint16_t)f[1] << 8));
                    b = stereo ? (int16_t)((uint16_t)f[2] |
                                           ((uint16_t)f[3] << 8)) : a;
                } else {
                    int32_t l = (int32_t)((uint32_t)f[0] |
                        ((uint32_t)f[1] << 8) | ((uint32_t)f[2] << 16));
                    int32_t r;

                    if (l & 0x00800000)
                        l |= (int32_t)0xff000000u;
                    r = l;
                    if (stereo) {
                        r = (int32_t)((uint32_t)f[3] |
                            ((uint32_t)f[4] << 8) | ((uint32_t)f[5] << 16));
                        if (r & 0x00800000)
                            r |= (int32_t)0xff000000u;
                    }
                    a = (l + 128) >> 8;
                    b = (r + 128) >> 8;
                    a = (a > 32767) ? 32767 : ((a < -32768) ? -32768 : a);
                    b = (b > 32767) ? 32767 : ((b < -32768) ? -32768 : b);
                }
                v = (a + b) >> 1;
                rc = sampleInstall_putByte((uint8_t)((uint16_t)v & 0xffu));
                if (rc == 0)
                    rc = sampleInstall_putByte((uint8_t)((uint16_t)v >> 8));
            }
            if (rc != 0)
                return rc;
        }
    }
    return 0;
}

/*
 * Finish one wavetable and program its entry (S085 C2.2).
 *
 * What:       refuses a short table (-4, aborted); flushes the carry;
 *             programs the 20 B entry (offset, frame length, frame count,
 *             stored bits, name).
 * Outputs:    0, -1, -4, or a sampleFlash status.
 */
int sampleInstall_finishWavetable(void)
{
    sample_catalogue_wavetable_t entry;
    uint32_t addr;
    int rc;

    if (sampleInstall_openKind() != INSTALL_OPEN_WAVETABLE)
        return -1;
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
 * Abandon the open item (S085 C2.2).
 *
 * What:       discards both carries and closes the item without an entry.
 *             write_addr already points past the programmed words, so the
 *             next item starts cleanly; the abandoned bytes stay unused until
 *             the next erase.
 * Why:        one unreadable file skips that file or table, not the install
 *             (S084 P2 F7).
 * Accessors:  filesystem.c on a read failure; the finish calls on -4.
 */
void sampleInstall_abortItem(void)
{
    sample_install.out_carry_n = 0u;
    sample_install.in_carry_n = 0u;
    sample_install.item_bytes = 0u;
    sampleInstall_setOpenKind(INSTALL_OPEN_NONE);
}

/*
 * Commit the catalogue (S085 C2.2).
 *
 * What:       programs the header without its magic (counts, stock flag,
 *             data end), then the magic word last; invalidates the D-cache
 *             over sample flash and refreshes the reader (new generation).
 * Why:        a catalogue is valid only after a complete install; a power
 *             loss before the magic leaves EMPTY.
 * Outputs:    0, -1 (an item still open), or a sampleFlash status.
 * Accessors:  filesystem_installSamplesBlocking().
 * Affiliates: sampleMemory_refresh().
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
    rc = sampleFlash_writeBytes(&addr, (const uint8_t *)&header + 4u,
                                (uint32_t)sizeof(header) - 4u);
    if (rc == 0)
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
