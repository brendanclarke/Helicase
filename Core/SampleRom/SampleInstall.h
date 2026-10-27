/*
 * SampleInstall.h — catalogue v2 installer core (S085 C2.1).
 *
 * What:       accepts WAV formats, converts and programs samples and
 *             wavetable frames into the item region, programs one catalogue
 *             entry per finished item, writes the stock block, and commits
 *             the header (magic last).
 * Why:        plan §5 item 8: Load:[Samples] installs samples/ and
 *             wavetables/ into catalogue v2; the firmware never expands data
 *             (8-bit stays 8-bit, 24-bit becomes 16-bit, stereo becomes mono).
 * Inputs:     formats, names and data chunks from filesystem.c.
 * Outputs:    sample flash; counts for the summary.
 * Accessors:  filesystem.c installer (C3.6) only; audio is suspended.
 * Affiliates: SampleMemory.h (layout and records), sampleFlash.c (erase and
 *             program), StockWaveGen.c (stock block), test_catalogue.c.
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
 * One accepted WAV format (S085 C2.1).
 *
 * What:       the source shape and the stored width after conversion.
 * Why:        samples and frames share one parser and writer; only the
 *             acceptance rule and the conversion differ.
 * Inputs:     filled by sampleInstall_acceptFormat().
 * Outputs:    read by the begin/write functions.
 * Accessors:  filesystem.c, SampleInstall.c.
 */
typedef struct {
    uint16_t channels;      /* 1 or 2 */
    uint16_t bits;          /* 8, 16 or 24 */
    uint32_t rate;          /* accepted, not used for pitch */
    uint16_t block_align;   /* bytes per source frame (<= 6) */
    uint8_t  stored_bits;   /* 8 or 16 after conversion */
    uint8_t  reserved;
} sample_install_fmt_t;

/*
 * Accept one WAV fmt chunk (S085 C2.1).
 *
 * What:       parses 16..40 bytes; accepts tag 1 (PCM) and tag 0xFFFE
 *             (extensible) with the PCM sub-format (bytes 24..25 == 1, 0).
 *             SAMPLE: mono, 16-bit, 44,100 Hz only (tools/sampleoptimize.py
 *             converts anything else on the host). FRAME: mono or stereo,
 *             8/16/24-bit, any rate. stored_bits = 8 for 8-bit sources,
 *             else 16.
 * Why:        the accepted forms of plan §5 item 8.
 * Inputs:     fmt bytes, their count, kind.  Outputs: 1 and *out, or 0.
 * Accessors:  filesystem_parseWav().
 * Affiliates: sampleInstall_storedBytes().
 */
uint8_t  sampleInstall_acceptFormat(const uint8_t *fmt, uint32_t fmt_bytes,
                                    sample_install_kind_t kind,
                                    sample_install_fmt_t *out);

/*
 * Stored size of a data chunk (S085 C2.1).
 *
 * What:       (data_bytes / block_align) x stored_bits / 8.
 * Why:        fit checks run before any byte is programmed.
 * Accessors:  sampleInstall_beginSample(), filesystem_installWavetable().
 */
uint32_t sampleInstall_storedBytes(const sample_install_fmt_t *fmt,
                                   uint32_t data_bytes);

/*
 * Install session (S085 C2.1).
 *
 * What:       begin erases sectors 7..11 and resets the state;
 *             writeStockBlock generates the five stock waves; the
 *             sample/wavetable calls program one item and its entry; commit
 *             programs the header and its magic last, then refreshes the
 *             reader.
 * Why:        every Load:[Samples] is a full install (no append, /loops
 *             retired).
 * Inputs:     names, formats, data.
 * Outputs:    0 ok; -1 state error; -2 count limit; -3 does not fit / bad
 *             geometry; -4 incomplete item (finish calls); >= 1 a sampleFlash
 *             status.
 * Accessors:  filesystem.c installer.
 * Affiliates: sampleFlash.c, stockWaveGen_programAll(), sampleMemory_refresh().
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
int      sampleInstall_setFrameFormat(const sample_install_fmt_t *fmt);
int      sampleInstall_writeFrameData(const uint8_t *data, uint32_t bytes);
int      sampleInstall_finishWavetable(void);
void     sampleInstall_abortItem(void);
int      sampleInstall_commit(void);

/*
 * Install progress (S085 C2.1).
 *
 * What:       bytes free before the catalogue; committed entry counts.
 * Accessors:  filesystem.c installer, menu.c summary.
 */
uint32_t sampleInstall_bytesFree(void);
uint8_t  sampleInstall_sampleCount(void);
uint8_t  sampleInstall_wavetableCount(void);

#endif /* SAMPLEINSTALL_H_ */
