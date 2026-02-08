/*
 * h265_decode_common.h
 * Common decoding routines shared by read_h265_frame.c and read_h265_frames.c.
 *
 * Provides:
 * - Decode state management (allocation/cleanup of AVFrame, AVPacket, SwsContext)
 * - Frame range decoding with row-major output
 */

#ifndef H265_DECODE_COMMON_H
#define H265_DECODE_COMMON_H

#include "mex.h"
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <stdint.h>
#include <string.h>

/* ============================================================================
 * Decode State Structure
 * ============================================================================ */

typedef struct {
  AVFrame *frame;           /* Decoded frame from codec */
  AVFrame *out_frame;       /* Converted output frame */
  AVPacket *pkt;            /* Packet for reading */
  struct SwsContext *sws_ctx;  /* Color space converter */
  int width;
  int height;
  int is_grayscale;
  size_t frame_size;        /* Size of one frame in bytes */
} H265DecodeState;

/* ============================================================================
 * Function Declarations
 * ============================================================================ */

/*
 * Copy an FFmpeg frame in row-major order (fast sequential memcpy).
 * Caller should use MATLAB permute() to convert to column-major.
 * For grayscale: output is width x height (use permute([2 1]))
 * For RGB: output is stored as height rows of width*3 bytes
 */
void copy_frame_rowmajor(AVFrame *out_frame, int width, int height,
                         int is_grayscale, uint8_t *out_data);

/*
 * Initialize decode state. Returns 1 on success, 0 on failure.
 */
int init_decode_state(H265DecodeState *state, AVCodecContext *codec_ctx,
                      int width, int height, int is_grayscale);

/*
 * Free decode state resources.
 */
void free_decode_state(H265DecodeState *state);

/*
 * Decode frames in [target_start, target_end] into frame_buffer using row-major copy.
 * Same as decode_frame_range but uses fast row-major copy instead of column-major transpose.
 * Caller must use MATLAB permute() on the result.
 */
int decode_frame_range_rowmajor(
    AVFormatContext *fmt_ctx, AVCodecContext *codec_ctx, int video_stream_idx,
    int64_t *dts_array, int64_t pts_increment,
    int target_start, int target_end,
    H265DecodeState *state,
    uint8_t *frame_buffer, size_t frame_size);

#endif /* H265_DECODE_COMMON_H */
