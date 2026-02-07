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
 * Frame Conversion
 * ============================================================================ */

/*
 * Convert an FFmpeg frame to MATLAB column-major format.
 * Handles both grayscale and RGB output.
 */
static inline void convert_frame_to_matlab(AVFrame *out_frame, int width, int height,
                                           int is_grayscale, uint8_t *out_data)
{
    if (is_grayscale) {
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                out_data[x * height + y] = out_frame->data[0][y * out_frame->linesize[0] + x];
            }
        }
    } else {
        size_t plane_size = (size_t)height * width;
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                int rgb_idx = y * out_frame->linesize[0] + x * 3;
                int matlab_idx = x * height + y;
                out_data[matlab_idx] = out_frame->data[0][rgb_idx];
                out_data[matlab_idx + plane_size] = out_frame->data[0][rgb_idx + 1];
                out_data[matlab_idx + 2 * plane_size] = out_frame->data[0][rgb_idx + 2];
            }
        }
    }
}

/* ============================================================================
 * Decode State Management
 * ============================================================================ */

/*
 * Initialize decode state. Returns 1 on success, 0 on failure.
 */
static inline int init_decode_state(H265DecodeState *state, AVCodecContext *codec_ctx,
                                    int width, int height, int is_grayscale)
{
    memset(state, 0, sizeof(H265DecodeState));

    state->width = width;
    state->height = height;
    state->is_grayscale = is_grayscale;
    state->frame_size = is_grayscale ? (size_t)width * height : (size_t)width * height * 3;

    state->frame = av_frame_alloc();
    state->out_frame = av_frame_alloc();
    state->pkt = av_packet_alloc();

    if (!state->frame || !state->out_frame || !state->pkt) {
        av_frame_free(&state->frame);
        av_frame_free(&state->out_frame);
        av_packet_free(&state->pkt);
        return 0;
    }

    enum AVPixelFormat out_pix_fmt = is_grayscale ? AV_PIX_FMT_GRAY8 : AV_PIX_FMT_RGB24;
    state->out_frame->format = out_pix_fmt;
    state->out_frame->width = width;
    state->out_frame->height = height;
    if (av_frame_get_buffer(state->out_frame, 0) < 0) {
        av_frame_free(&state->frame);
        av_frame_free(&state->out_frame);
        av_packet_free(&state->pkt);
        return 0;
    }

    state->sws_ctx = sws_getContext(width, height, codec_ctx->pix_fmt,
                                    width, height, out_pix_fmt,
                                    SWS_BILINEAR, NULL, NULL, NULL);
    if (!state->sws_ctx) {
        av_frame_free(&state->frame);
        av_frame_free(&state->out_frame);
        av_packet_free(&state->pkt);
        return 0;
    }

    return 1;
}

/*
 * Free decode state resources.
 */
static inline void free_decode_state(H265DecodeState *state)
{
    if (state->sws_ctx) sws_freeContext(state->sws_ctx);
    if (state->pkt) av_packet_free(&state->pkt);
    if (state->frame) av_frame_free(&state->frame);
    if (state->out_frame) av_frame_free(&state->out_frame);
    memset(state, 0, sizeof(H265DecodeState));
}

/* ============================================================================
 * Frame Range Decoding
 * ============================================================================ */

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
static inline int decode_frame_range(
    AVFormatContext *fmt_ctx, AVCodecContext *codec_ctx, int video_stream_idx,
    int64_t *dts_array, int64_t pts_increment,
    int target_start, int target_end,
    H265DecodeState *state,
    uint8_t *frame_buffer, size_t frame_size)
{
    int ret;
    int num_frames = target_end - target_start + 1;
    int frames_captured = 0;

    /* Track which frames we've captured */
    int *captured = (int *)mxCalloc(num_frames, sizeof(int));
    if (!captured) return -1;

    /* Seek to target start position */
    ret = av_seek_frame(fmt_ctx, video_stream_idx, dts_array[target_start], AVSEEK_FLAG_BACKWARD);
    if (ret < 0) {
        avformat_seek_file(fmt_ctx, video_stream_idx, INT64_MIN, 0, 0, 0);
    }
    avcodec_flush_buffers(codec_ctx);

    /* Decode until we have all frames */
    while (frames_captured < num_frames && av_read_frame(fmt_ctx, state->pkt) >= 0) {
        if (state->pkt->stream_index == video_stream_idx) {
            ret = avcodec_send_packet(codec_ctx, state->pkt);
            if (ret < 0) {
                av_packet_unref(state->pkt);
                continue;
            }

            while (frames_captured < num_frames) {
                ret = avcodec_receive_frame(codec_ctx, state->frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
                if (ret < 0) {
                    mxFree(captured);
                    return -1;
                }

                int frame_idx = (int)(state->frame->pts / pts_increment);

                /* Check if this frame is in our target range and not yet captured */
                if (frame_idx >= target_start && frame_idx <= target_end) {
                    int local_idx = frame_idx - target_start;
                    if (!captured[local_idx]) {
                        sws_scale(state->sws_ctx,
                                  (const uint8_t * const*)state->frame->data,
                                  state->frame->linesize, 0, state->height,
                                  state->out_frame->data, state->out_frame->linesize);

                        uint8_t *dest = frame_buffer + local_idx * frame_size;
                        convert_frame_to_matlab(state->out_frame, state->width,
                                                state->height, state->is_grayscale, dest);

                        captured[local_idx] = 1;
                        frames_captured++;
                    }
                }

                /* Release decoder's internal buffer reference */
                av_frame_unref(state->frame);
            }
        }
        av_packet_unref(state->pkt);
    }

    /* Flush decoder for remaining frames */
    if (frames_captured < num_frames) {
        avcodec_send_packet(codec_ctx, NULL);
        while (frames_captured < num_frames) {
            ret = avcodec_receive_frame(codec_ctx, state->frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
            if (ret < 0) break;

            int frame_idx = (int)(state->frame->pts / pts_increment);

            if (frame_idx >= target_start && frame_idx <= target_end) {
                int local_idx = frame_idx - target_start;
                if (!captured[local_idx]) {
                    sws_scale(state->sws_ctx,
                              (const uint8_t * const*)state->frame->data,
                              state->frame->linesize, 0, state->height,
                              state->out_frame->data, state->out_frame->linesize);

                    uint8_t *dest = frame_buffer + local_idx * frame_size;
                    convert_frame_to_matlab(state->out_frame, state->width,
                                            state->height, state->is_grayscale, dest);

                    captured[local_idx] = 1;
                    frames_captured++;
                }
            }

            /* Release decoder's internal buffer reference */
            av_frame_unref(state->frame);
        }
    }

    mxFree(captured);
    return frames_captured;
}

#endif /* H265_DECODE_COMMON_H */
