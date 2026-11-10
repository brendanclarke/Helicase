/*
 * SampleInstall.h - modal sample/wavetable installer core (S087 stage C).
 *
 * What:       format acceptance and block conversion for WAV data into
 *             catalogue v3 items: PCM 1-2 channels, 16/24 bit (S093 P8:
 *             8-bit refused), any rate; samples store 16-bit mono, each
 *             wavetable frame stores one 1,024-point 16-bit row.
 * Why:        the device converts depth and channels so most files install
 *             without the host tool (S087 P0 A15); the catalogue is valid only
 *             after the header's magic is written last.
 * Inputs:     filesystem.c's install walker (fmt bytes, data blocks, smpl).
 * Outputs:    sample flash items and the catalogue via SampleMemory.h.
 * Accessors:  filesystem_installUserContentBlocking().
 * Affiliates: SampleMemory.h (layout), sampleFlash.c, test_wav_install.c,
 *             test_catalogue.c.
 */
#ifndef SAMPLEINSTALL_H_
#define SAMPLEINSTALL_H_

#include "SampleMemory.h"
#include <stdint.h>

typedef enum {
    SAMPLE_INSTALL_KIND_SAMPLE = 0,
    SAMPLE_INSTALL_KIND_WAVETABLE
} sample_install_kind_t;

typedef struct {
    uint16_t format_tag;     /* 1 = PCM (extensible is normalised to 1) */
    uint16_t channels;       /* 1 or 2 */
    uint32_t sample_rate;    /* any rate; the host converts if it wants */
    uint16_t block_align;
    uint16_t bits;           /* source bits per sample */
    uint8_t  stored_bits;    /* 16 (S093 P8: samples and wavetable rows) */
} sample_install_fmt_t;

/*
 * Accept one WAV `fmt ` chunk (S087 stage C).
 *
 * What:       accepts PCM or WAVE_FORMAT_EXTENSIBLE with a PCM subformat,
 *             1-2 channels and 16/24-bit samples (S093 P8: 8-bit is refused,
 *             user D10); `stored_bits` is 16 for every kind.
 * Why:        S087 P0 §5.2/A15: the device converts depth and channels; rate
 *             conversion stays on the host so other rates play transposed.
 * Inputs:     fmt bytes (chunk body), kind, out.
 * Outputs:    1 accepted / 0 rejected; *out filled.
 * Accessors:  filesystem_parseWav().
 * Affiliates: sampleInstall_beginSample(), sampleInstall_beginWavetable().
 */
uint8_t sampleInstall_acceptFormat(const uint8_t *fmt, uint32_t bytes,
                                   sample_install_kind_t kind,
                                   sample_install_fmt_t *out);

void     sampleInstall_begin(void);
uint32_t sampleInstall_bytesFree(void);

uint8_t sampleInstall_beginSample(const char name[SAMPLE_DISPLAY_NAME_LEN],
                                  uint32_t frames,
                                  const sample_install_fmt_t *fmt);
uint8_t sampleInstall_writeFrames(const uint8_t *src, uint32_t frames);
uint8_t sampleInstall_finishSample(uint32_t loop_start, uint32_t loop_end,
                                   uint8_t has_loop);

/*
 * Open a wavetable item (S087 stage C; S093 P8 row storage).
 *
 * What:       validates a source frame length (16..2,048 points), frame count
 *             (1..128) and source format (16/24-bit mono/stereo), then reserves
 *             catalogue space for frame_count rows of 1,024 int16 points.
 * Why:        every source frame is converted to mono and resampled at install
 *             time; the stored depth is always 16, so there is no depth
 *             argument. The row layout is shared by every wavetable reader.
 * Inputs:     name (8-byte catalogue name), frame_len (source points per
 *             frame), frame_count, fmt (from sampleInstall_acceptFormat()).
 * Outputs:    open wavetable state; 1 when opened, 0 when invalid, full, busy
 *             or after an install write fault.
 * Accessors:  filesystem_installWavetableHere().
 * Affiliates: sampleInstall_writeFrame(), sampleInstall_finishWavetable(),
 *             WAVETABLE_ROW_POINTS / WAVETABLE_ROW_BYTES (SampleMemory.h).
 */
uint8_t sampleInstall_beginWavetable(const char name[SAMPLE_DISPLAY_NAME_LEN],
                                     uint16_t frame_len, uint8_t frame_count,
                                     const sample_install_fmt_t *fmt);

/*
 * Append source points to the open wavetable (S093 P8).
 *
 * What:       converts `points` interleaved 16/24-bit mono/stereo points to
 *             int16 mono in frame_buf, then writes each complete source frame
 *             as one resampled 1,024-point row. The caller supplies an aligned
 *             frame_len-point buffer borrowed from fs_list_cache_name.
 * Why:        filesystem_installFrames() delivers WAV data in chunks that can
 *             split a frame; source-point counting makes frame boundaries
 *             independent of chunk boundaries. The borrowed buffer avoids a
 *             new RAM allocation and the raw source frame is never stored.
 * Inputs:     src (interleaved source bytes), points (source points in src),
 *             frame_buf (at least source frame_len int16 points, 2-byte
 *             aligned, owned by the caller for the duration of this call).
 * Outputs:    1 on success; 0 on invalid input, overrun or flash write fault.
 * Accessors:  filesystem_installFrames().
 * Affiliates: sampleInstall_beginWavetable(), sampleInstall_finishWavetable(),
 *             sampleInstall_rowPoint() (SampleInstall.c).
 */
uint8_t sampleInstall_writeFrame(const uint8_t *src, uint16_t points,
                                 int16_t *frame_buf);

/*
 * Finish the open wavetable (S087 stage C; S093 P8 rows).
 *
 * What:       flushes staged row bytes and writes one catalogue entry whose
 *             frame_len is 1,024, bits is 16 and frame_count is the number of
 *             complete source frames converted to rows.
 * Why:        the catalogue format marks the one stored row geometry checked
 *             by sampleMemory_wavetableRangeValid(); partial source frames
 *             never become playable rows.
 * Inputs:     the open wavetable state and all complete rows written so far.
 * Outputs:    1 when the entry is committed; 0 on a write fault or when there
 *             are no complete frames.
 * Accessors:  filesystem_installWavetableHere().
 * Affiliates: sampleInstall_beginWavetable(), sampleInstall_writeFrame(),
 *             sampleMemory_wavetableRows().
 */
uint8_t sampleInstall_finishWavetable(void);

void     sampleInstall_abortItem(void);
uint8_t sampleInstall_commit(void);

/*
 * Largest gain code whose gain keeps the peak inside int16 (S087 stage C).
 *
 * What:       returns c in 0..255 with peak x 2^(c/24) <= 32767, 0 for
 *             silence; computed at install from the stored peak.
 * Why:        playback then needs no level analysis and one gain per block.
 * Inputs:     peak magnitude 0..32768. Outputs: gain code.
 * Accessors:  sampleInstall_finishSample().
 * Affiliates: sampleMemory_gainFromCode().
 */
uint8_t sampleInstall_gainCode(uint32_t peak);

#endif /* SAMPLEINSTALL_H_ */
