/*
 * SampleInstall — write catalogue v2 from WAV data handed over by the
 * filesystem (S084 VX2 VX2-2.1).
 *
 * What:       accepts and converts every supported WAV form, programs item
 *             data upward from SAMPLE_DATA_START_ADDRESS, writes one 20 B
 *             catalogue entry as each item finishes, generates the stock
 *             block, and commits the header (magic last).
 * Why:        P2-A, W1: samples keep today's form (mono, 16-bit, 44.1 kHz);
 *             wavetable frames are stored as supplied, reduced only when the
 *             form is wider than the oscillator reads (24-bit -> 16-bit,
 *             stereo -> mono). There is no install-time scratch beyond the
 *             32 B state in SampleInstall.c.
 * Inputs:     format chunks and data bytes from filesystem.c (modal, audio
 *             suspended).
 * Outputs:    sample flash; SampleMemory refresh on commit.
 * Accessors:  filesystem.c installer (VX2-3.x).
 * Affiliates: SampleMemory.h (layout, records), sampleFlash.c (writes),
 *             StockWaveGen.c (stock block), tools/dsp_test/test_catalogue.c.
 */
#ifndef SAMPLEINSTALL_H_
#define SAMPLEINSTALL_H_

#include <stdint.h>
#include "SampleMemory.h"

typedef enum {
    SAMPLE_INSTALL_KIND_SAMPLE = 0,
    SAMPLE_INSTALL_KIND_FRAME
} sample_install_kind_t;

/*
 * One accepted WAV format (S084 VX2 VX2-2.1).
 *
 * What:       the source shape and the stored width after conversion.
 * Why:        samples and wavetable frames share one parser and one writer;
 *             only the acceptance rule and the conversion differ.
 * Inputs:     filled by sampleInstall_acceptFormat().  Outputs: read by the
 *             write functions.
 * Accessors:  filesystem.c installer, SampleInstall.c.
 */
typedef struct {
    uint16_t channels;      /* 1 or 2 */
    uint16_t bits;          /* 8, 16 or 24 */
    uint32_t rate;          /* accepted but not used for playback pitch */
    uint16_t block_align;   /* bytes per source frame */
    uint8_t  stored_bits;   /* 8 or 16 after conversion */
    uint8_t  reserved;
} sample_install_fmt_t;

/*
 * Accept one WAV fmt chunk (S084 VX2 VX2-2.1).
 *
 * What:       parses 16..40 bytes. Accepts tag 1 (PCM) and tag 0xFFFE
 *             (extensible) with the PCM sub-format GUID (first 2 bytes = 1
 *             at offset 24). SAMPLE: channels 1, 16 bits, 44,100 Hz. FRAME:
 *             channels 1 or 2, 8/16/24 bits, any rate. stored_bits is 8 for
 *             8-bit sources, else 16.
 * Why:        P2-A accepted forms; frames are W1.
 * Inputs:     fmt, fmt_bytes, kind.  Outputs: 1 and *out when accepted.
 * Accessors:  filesystem_parseWav() (VX2-3.6).
 * Affiliates: sampleInstall_storedBytes().
 */
uint8_t  sampleInstall_acceptFormat(const uint8_t *fmt, uint32_t fmt_bytes,
                                    sample_install_kind_t kind,
                                    sample_install_fmt_t *out);

/*
 * Stored size of a source data chunk (S084 VX2 VX2-2.1).
 *
 * What:       frames x stored_bits/8, where frames = data_bytes/block_align.
 * Why:        the wavetable fit check runs before any byte is written.
 * Inputs:     fmt, data_bytes.  Outputs: stored bytes.
 * Accessors:  filesystem_installWavetable() (VX2-3.8).
 */
uint32_t sampleInstall_storedBytes(const sample_install_fmt_t *fmt,
                                   uint32_t data_bytes);

/*
 * Install session control (S084 VX2 VX2-2.1).
 *
 * What:       begin erases sectors 7..11 and resets the state; the write
 *             functions program one sample or one wavetable; commit programs
 *             the header and its magic last.
 * Why:        every Load:[Samples] is a full install; the v1 append mode is
 *             gone with /loops.
 * Inputs:     item names/formats/data.  Outputs: 0 ok; -1 state; -2 count
 *             limit; -3 no fit; -4 incomplete item (finish calls, VX2 F8);
 *             >= 1 a sampleFlash status.
 * Accessors:  filesystem.c installer.
 * Affiliates: sampleFlash.c, stockWaveGen_programAll().
 */
int      sampleInstall_begin(void);
int      sampleInstall_writeStockBlock(void);
int      sampleInstall_beginSample(const char name[SAMPLE_DISPLAY_NAME_LEN],
                                   const sample_install_fmt_t *fmt,
                                   uint32_t data_bytes);
int      sampleInstall_writeSampleData(const uint8_t *data, uint32_t bytes);
int      sampleInstall_finishSample(void);
int      sampleInstall_beginWavetable(const char name[SAMPLE_DISPLAY_NAME_LEN],
                                      const sample_install_fmt_t *fmt,
                                      uint16_t frame_len, uint8_t frame_count);
int      sampleInstall_writeFrameData(const uint8_t *data, uint32_t bytes);
int      sampleInstall_finishWavetable(void);
int      sampleInstall_commit(void);

/*
 * Item recovery and per-frame formats (S084 VX2 review fixes F4, F7, F8).
 *
 * What:       abortItem() closes the open item without an entry (its bytes
 *             stay unused); setFrameFormat() switches the open table's source
 *             decoding to the next frame file's own format at a frame
 *             boundary (same stored depth required). The finish calls return
 *             -4 and abort when fewer bytes than declared were written.
 * Why:        one unreadable file skips that file or table instead of the
 *             whole install (F7); frames of different source forms convert
 *             correctly (F4); no entry ever describes missing data (F8).
 * Inputs:     the open item; the next frame's fmt.
 * Outputs:    open kind none (abort); 0 / -1 (setFrameFormat).
 * Accessors:  filesystem.c installer.
 * Affiliates: the begin/write/finish calls above.
 */
void     sampleInstall_abortItem(void);
int      sampleInstall_setFrameFormat(const sample_install_fmt_t *fmt);

/*
 * Install progress (S084 VX2 VX2-2.1).
 *
 * What:       bytes still free before the catalogue, and the committed
 *             counts.
 * Why:        the installer's skip decisions and the LCD summary (D13).
 * Inputs:     none.  Outputs: byte count / entry counts.
 * Accessors:  filesystem.c installer, menu.c summary.
 */
uint32_t sampleInstall_bytesFree(void);
uint8_t  sampleInstall_sampleCount(void);
uint8_t  sampleInstall_wavetableCount(void);

#endif /* SAMPLEINSTALL_H_ */
