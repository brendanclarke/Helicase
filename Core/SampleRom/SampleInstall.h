/*
 * SampleInstall.h - modal sample/wavetable installer core (S087 stage C).
 *
 * What:       format acceptance and block conversion for WAV data into
 *             catalogue v2 items: PCM 1-2 channels, 8/16/24 bit, any rate;
 *             samples store 16-bit mono, wavetable frames store 8 or 16 bit.
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
    uint8_t  stored_bits;    /* 16 for samples; 8 or 16 for frames */
} sample_install_fmt_t;

/*
 * Accept one WAV `fmt ` chunk (S087 stage C).
 *
 * What:       accepts PCM or WAVE_FORMAT_EXTENSIBLE with a PCM subformat,
 *             1-2 channels and 8/16/24-bit samples; `stored_bits` is 16 for a
 *             sample and the table's depth for a frame.
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

uint8_t sampleInstall_beginWavetable(const char name[SAMPLE_DISPLAY_NAME_LEN],
                                     uint16_t frame_len, uint8_t frame_count,
                                     uint8_t bits,
                                     const sample_install_fmt_t *fmt);
uint8_t sampleInstall_writeFrame(const uint8_t *src, uint16_t frames);
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
