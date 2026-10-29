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

/*
 * Write fault latch (S087 P1).
 *
 * What:       1 after a failed sector erase or a failed word write during this
 *             install; 0 otherwise. While set, no new item may begin.
 * Why:        after a failed write it is unknown which words of the attempted
 *             range were programmed; writing further items could program over
 *             them. Items finished before the fault stay valid and are
 *             published by sampleInstall_commit().
 * Inputs:     sampleInstall_begin() (erase), sampleInstall_program().
 * Outputs:    read by sampleInstall_beginSample()/beginWavetable().
 * Accessors:  this file only.
 * Affiliates: sampleFlash_writeWord() (read-back check, return 3).
 * RAM:        1 B .bss (within the approved R-C3 installer state).
 */
static uint8_t  install_fault;

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

/*
 * Program bytes at a cursor, program-once safe (S087 P1).
 *
 * What:       programs `bytes` bytes at *addr (4-aligned; a partial last word
 *             is padded with 0xFF by sampleFlash_writeBytes()) and ALWAYS
 *             advances *addr past every word attempted, whether the write
 *             succeeded or not. A failure sets install_fault.
 * Why:        S087 P0 impl. §11 F2: sampleFlash_writeBytes() leaves its cursor
 *             at the start of the range when a word fails, although earlier
 *             words of the range are already programmed; the installer must
 *             never write those words again.
 * Inputs:     addr (cursor, 4-aligned), data, bytes.
 * Outputs:    *addr = old *addr + bytes rounded up to 4; install_fault; 0 on
 *             success, the sampleFlash error otherwise.
 * Accessors:  sampleInstall_flush(), sampleInstall_finishSample(),
 *             sampleInstall_finishWavetable().
 * Affiliates: sampleFlash_writeBytes().
 */
static int sampleInstall_program(uint32_t *addr, const uint8_t *data,
                                 uint32_t bytes)
{
    const uint32_t end = *addr + ((bytes + 3u) & ~3u);
    uint32_t cursor = *addr;
    int rc = sampleFlash_writeBytes(&cursor, data, bytes);

    *addr = end;
    if (rc != 0)
        install_fault = 1u;
    return rc;
}

/*
 * Write the buffered item bytes (S087 P1; S087 stage C).
 *
 * What:       programs the whole words in out_buf, then a partial last word
 *             padded with zeros (sample and frame padding must read 0), each
 *             through sampleInstall_program(); empties the buffer.
 * Why:        the cursor must advance past every attempted word (F2).
 * Inputs:     out_buf, out_len, write_addr.
 * Outputs:    flash; write_addr; out_len = 0; 0 or the error.
 * Accessors:  sampleInstall_append(), sampleInstall_finishSample(),
 *             sampleInstall_finishWavetable(), sampleInstall_commit().
 * Affiliates: sampleInstall_program().
 */
static int sampleInstall_flush(void)
{
    const uint32_t whole = out_len & ~3u;
    int rc = 0;

    if (whole != 0u)
        rc = sampleInstall_program(&write_addr, out_buf, whole);
    if (rc == 0 && (out_len & 3u) != 0u) {
        uint8_t tail[4] = { 0u, 0u, 0u, 0u };

        memcpy(tail, &out_buf[whole], (uint32_t)(out_len & 3u));
        rc = sampleInstall_program(&write_addr, tail, 4u);
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

/*
 * Open a sample item (S087 stage C; S087 P1).
 *
 * What:       validates the accepted format, clamps the frame count to
 *             SAMPLE_MAX_FRAMES, refuses when the item does not fit below the
 *             catalogue, then records the start address, the frame count and
 *             the item's name, format and peak state.
 * Why:        one item at a time, written forward from write_addr; the entry
 *             that makes it visible is written by sampleInstall_finishSample().
 * Inputs:     name, frames, fmt (sampleInstall_acceptFormat()).
 * Outputs:    the open item state; 1 when opened, 0 when refused (bad format,
 *             an item already open, the count limit, no space).
 * Accessors:  filesystem_installSampleFile().
 * Affiliates: sampleInstall_writeFrames(), sampleInstall_finishSample().
 * S087 P1: refused after a write fault (install_fault).
 */
uint8_t sampleInstall_beginSample(const char name[SAMPLE_DISPLAY_NAME_LEN],
                                  uint32_t frames,
                                  const sample_install_fmt_t *fmt)
{
    if (!fmt || item_open || install_fault || sample_items >= SAMPLE_MAX_COUNT)
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

/*
 * Finish the open sample (S087 P1; S087 stage C).
 *
 * What:       flushes the last sample bytes, programs the two zero frames
 *             after the data (the smp end pad, A4), then programs the 28 B
 *             sample entry once in the next free sample slot (loop points from
 *             smpl when valid, else the whole sample; the gain code from the
 *             peak). Any failed write sets install_fault and closes the item.
 * Why:        program-once rule (S087 P0 impl. §11 F1/F2); the pad must exist
 *             for the smp reader, and the entry is the only thing that makes
 *             the item visible.
 * Inputs:     loop_start, loop_end (exclusive), has_loop; item_start,
 *             frames_total, item_name, item_peak, sample_items.
 * Outputs:    pad and entry in flash; sample_items + 1 on success; 1/0.
 * Accessors:  filesystem_installSampleFile().
 * Affiliates: sampleInstall_gainCode(); SampleMemory.h sample entry layout;
 *             sampleInstall_program().
 */
uint8_t sampleInstall_finishSample(uint32_t loop_start, uint32_t loop_end,
                                   uint8_t has_loop)
{
    sample_catalogue_entry_t entry;
    static const uint8_t zeros[4] = { 0u, 0u, 0u, 0u };
    uint32_t entry_addr;

    if (item_open != 1u)
        return 0u;
    if (sampleInstall_flush() != 0 ||
        sampleInstall_program(&write_addr, zeros, 4u) != 0) {
        item_open = 0u;
        return 0u;
    }
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
    item_open = 0u;
    if (sampleInstall_program(&entry_addr, (const uint8_t *)&entry,
                              sizeof(entry)) != 0)
        return 0u;
    sample_items++;
    return 1u;
}

/*
 * Open a wavetable item (S087 stage C; S087 P1).
 *
 * What:       validates the frame geometry (WAVETABLE_FRAME_MIN..MAX points,
 *             1..WAVETABLE_MAX_FRAMES frames, 8 or 16 stored bits) and the
 *             accepted format, refuses when the item does not fit below the
 *             catalogue, then records the start address, the frame geometry
 *             and the item's name and format.
 * Why:        one table is one item: every frame of the folder is written
 *             through sampleInstall_writeFrame(), and the entry written by
 *             sampleInstall_finishWavetable() describes the whole table.
 * Inputs:     name, frame_len, frame_count, bits, fmt.
 * Outputs:    the open item state; 1 when opened, 0 when refused.
 * Accessors:  filesystem_installWavetableHere().
 * Affiliates: sampleInstall_writeFrame(), sampleInstall_finishWavetable().
 * S087 P1: refused after a write fault (install_fault).
 */
uint8_t sampleInstall_beginWavetable(const char name[SAMPLE_DISPLAY_NAME_LEN],
                                     uint16_t frame_len, uint8_t frame_count,
                                     uint8_t bits,
                                     const sample_install_fmt_t *fmt)
{
    if (!fmt || item_open || install_fault || wavetable_items >= WAVETABLE_MAX_COUNT)
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

/*
 * Finish the open wavetable (S087 P1; S087 stage C).
 *
 * What:       flushes the last frame bytes, then programs the 20 B wavetable
 *             entry once, in the next free wavetable slot. A failed entry write
 *             sets install_fault; the slot index is not reused.
 * Why:        catalogue entries follow the program-once rule like the item
 *             data; after a fault no further item may claim the same slot.
 * Inputs:     item_start, item_frame_len, frames_written, item_stored_bits,
 *             item_name, wavetable_items.
 * Outputs:    the entry in flash; wavetable_items + 1 on success; 1/0.
 * Accessors:  filesystem_installWavetableHere().
 * Affiliates: SampleMemory.h wavetable entry layout; sampleInstall_program().
 */
uint8_t sampleInstall_finishWavetable(void)
{
    wavetable_catalogue_entry_t entry;
    uint32_t entry_addr;

    if (item_open != 2u)
        return 0u;
    if (sampleInstall_flush() != 0) {
        item_open = 0u;
        return 0u;
    }
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
    item_open = 0u;
    if (sampleInstall_program(&entry_addr, (const uint8_t *)&entry,
                              sizeof(entry)) != 0)
        return 0u;
    wavetable_items++;
    return 1u;
}

/*
 * Drop the open item (S087 P1; S087 stage C).
 *
 * What:       discards the buffered, unwritten bytes and closes the item. The
 *             write cursor is NOT moved back: the item's words already
 *             programmed stay programmed and are skipped (lost space), and the
 *             next item starts at the first word never written. No catalogue
 *             entry is written for the dropped item.
 * Why:        S087 P0 impl. §11 F2: rewinding made the next item program over
 *             programmed words; its read-back failed and it was lost too.
 * Inputs:     item_open, out_len.
 * Outputs:    item_open = 0; out_len = 0; write_addr unchanged.
 * Accessors:  filesystem_installSampleFile(), filesystem_installWavetableHere(),
 *             sampleInstall_commit().
 * Affiliates: sampleInstall_program() (always moves the cursor past every word
 *             it attempted).
 */
void sampleInstall_abortItem(void)
{
    if (item_open) {
        out_len = 0u;
        item_open = 0u;
    }
}

/*
 * Start an install (S087 P1; S087 stage C).
 *
 * What:       erases sectors 7..11 and resets the installer state. Nothing is
 *             programmed: the catalogue header is written once, by
 *             sampleInstall_commit(). A failed erase sets install_fault, so no
 *             item is written into flash that is not erased.
 * Why:        S087 P0 impl. §11 F1: a header written here could not be
 *             rewritten at commit. An interrupted install reads EMPTY because
 *             the whole catalogue, magic included, stays erased.
 * Inputs:     none.
 * Outputs:    erased sample flash; write_addr at the first item address;
 *             counters cleared; install_fault from the erase result.
 * Accessors:  filesystem_installUserContentBlocking().
 * Affiliates: sampleFlash_eraseAllSamples(); sampleInstall_commit();
 *             sampleMemory_refresh() (EMPTY/STALE detection reads word 0 of
 *             the item region, which an S087 install never programs).
 */
void sampleInstall_begin(void)
{
    sampleFlash_init();
    install_fault = (uint8_t)(sampleFlash_eraseAllSamples() != 0); { sample_catalogue_header_t bh; uint32_t ba; memset(&bh, 0, sizeof(bh)); bh.magic = SAMPLE_CATALOGUE_MAGIC; bh.version = SAMPLE_CATALOGUE_VERSION; bh.sample_entry_bytes = SAMPLE_ENTRY_BYTES; bh.wavetable_entry_bytes = WAVETABLE_ENTRY_BYTES; ba = SAMPLE_CATALOGUE_START_ADDRESS; (void)sampleFlash_writeBytes(&ba, (const uint8_t *)&bh, sizeof(bh)); }
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

/*
 * Publish the catalogue (S087 P1; S087 stage C).
 *
 * What:       closes an open item (abort), flushes the output buffer, then
 *             programs the catalogue header exactly once: words 1..7 (version,
 *             entry sizes, counts, flags, data_end, reserved = 0) first, and
 *             the magic word (word 0) last. Invalidates the D-cache over sample
 *             flash and refreshes the read API.
 * Why:        a flash word can be programmed once after the erase, and
 *             sampleFlash_writeWord() verifies each word: the header must not
 *             be written at begin and again here (S087 P0 impl. §11 F1). The
 *             magic last keeps an interrupted install readable as EMPTY: until
 *             the magic is programmed, word 0 is erased.
 * Inputs:     the counters of finished items (sample_items, wavetable_items),
 *             write_addr (the end of the item data).
 * Outputs:    the header in flash; the refreshed catalogue; 1 when the header
 *             and the magic were programmed, else 0.
 * Accessors:  filesystem_installUserContentBlocking().
 * Affiliates: sampleMemory_refresh(); SampleMemory.h header layout;
 *             sampleFlash_writeBytes()/writeWord().
 */
uint8_t sampleInstall_commit(void)
{
    sample_catalogue_header_t header;
    uint32_t addr;

    if (item_open)
        sampleInstall_abortItem();
    (void)sampleInstall_flush();
    memset(&header, 0, sizeof(header));
    header.magic = 0xFFFFFFFFu;            /* not written here; see below */
    header.version = SAMPLE_CATALOGUE_VERSION;
    header.sample_entry_bytes = SAMPLE_ENTRY_BYTES;
    header.wavetable_entry_bytes = WAVETABLE_ENTRY_BYTES;
    header.sample_count = sample_items;
    header.wavetable_count = wavetable_items;
    header.data_end = write_addr;
    addr = SAMPLE_CATALOGUE_START_ADDRESS + 4u;
    if (sampleFlash_writeBytes(&addr, (const uint8_t *)&header + 4u,
                               (uint32_t)sizeof(header) - 4u) != 0)
        return 0u;
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
