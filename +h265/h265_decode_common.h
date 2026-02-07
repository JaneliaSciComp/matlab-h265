/*
 * h265_decode_common.h
 * Common decoding routines shared by read_h265_frame.c and read_h265_frames.c.
 *
 * Provides:
 * - Frame conversion from FFmpeg format to MATLAB column-major format
 * - Decode state management (allocation/cleanup of AVFrame, AVPacket, SwsContext)
 * - Simple frame range decoding
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
 * Convert an FFmpeg frame to MATLAB column-major format.
 * Handles both grayscale and RGB output.
 * NOTE: This is slow due to strided memory access.
 */
void convert_frame_to_matlab(AVFrame *out_frame, int width, int height,
                             int is_grayscale, uint8_t *out_data);

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
 * Decode frames in [target_start, target_end] into frame_buffer.
 *
 * Parameters:
 *   fmt_ctx, codec_ctx, video_stream_idx - FFmpeg contexts
 *   dts_array       - DTS lookup table for seeking
 *   pts_increment   - PTS units per frame
 *   target_start    - first frame index to capture (0-based)
 *   target_end      - last frame index to capture (0-based)
 *   state           - initialized decode state
 *   frame_buffer    - output buffer (must hold (target_end - target_start + 1) frames)
 *   frame_size      - size of each frame in bytes
 *
 * Returns: number of frames captured, or -1 on error
 */
int decode_frame_range(
    AVFormatContext *fmt_ctx, AVCodecContext *codec_ctx, int video_stream_idx,
    int64_t *dts_array, int64_t pts_increment,
    int target_start, int target_end,
    H265DecodeState *state,
    uint8_t *frame_buffer, size_t frame_size);

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
