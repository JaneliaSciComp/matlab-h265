/*
 * read_h265_frame.c
 * MEX function to read a frame at index i from a video file using FFmpeg.
 * Uses the persistent decoder context from open_h265_video for fast access.
 *
 * Optimization: Uses GOP frame cache stored in video_info. Subsequent requests
 * for frames in the same GOP are served from cache without re-decoding.
 *
 * Usage: frame = read_h265_frame(video_info, frame_index)
 *   video_info  - struct returned by open_h265_video
 *   frame_index - 1-based frame index
 *   frame       - grayscale (height x width) or RGB (height x width x 3) uint8
 *                 Output format depends on is_gray field in video_info.
 *
 * Compile with:
 *   mex read_h265_frame.c -lavformat -lavcodec -lavutil -lswscale
 */

#include "mex.h"
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <stdint.h>
#include <string.h>
#include "h265_frame_cache.h"
#include "h265_decode_common.h"

/* ============================================================================
 * Cache Helper Functions
 * ============================================================================ */

static int find_in_cache(H265FrameCache *cache, int frame_index)
{
    if (!cache || cache->num_frames == 0) return -1;
    int *indices = *cache->frame_indices;
    for (int i = 0; i < cache->num_frames; i++) {
        if (indices[i] == frame_index) {
            return i;
        }
    }
    return -1;
}

static uint8_t *get_cache_frame(H265FrameCache *cache, int cache_idx)
{
    return *cache->frame_data + cache_idx * cache->frame_size;
}

static void add_to_cache(H265FrameCache *cache, int frame_index, const uint8_t *frame_data)
{
    if (!cache || cache->num_frames >= cache->capacity) return;

    int idx = cache->num_frames;
    memcpy(get_cache_frame(cache, idx), frame_data, cache->frame_size);
    (*cache->frame_indices)[idx] = frame_index;
    cache->num_frames++;
}

static void clear_cache(H265FrameCache *cache)
{
    if (cache) {
        cache->num_frames = 0;
    }
}

static int ensure_cache_capacity(H265FrameCache *cache, int new_capacity)
{
    if (!cache || new_capacity <= cache->capacity) return 1;

    uint8_t *new_data = (uint8_t *)mxRealloc(*cache->frame_data,
                                              new_capacity * cache->frame_size);
    int *new_indices = (int *)mxRealloc(*cache->frame_indices,
                                         new_capacity * sizeof(int));

    if (!new_data || !new_indices) {
        if (new_data && new_data != *cache->frame_data) mxFree(new_data);
        if (new_indices && new_indices != *cache->frame_indices) mxFree(new_indices);
        return 0;
    }

    mexMakeMemoryPersistent(new_data);
    mexMakeMemoryPersistent(new_indices);
    *cache->frame_data = new_data;
    *cache->frame_indices = new_indices;
    cache->capacity = new_capacity;
    return 1;
}

static void init_cache_format(H265FrameCache *cache, int width, int height, int is_grayscale)
{
    if (!cache) return;
    cache->num_frames = 0;
    cache->width = width;
    cache->height = height;
    cache->is_grayscale = is_grayscale;
    cache->frame_size = is_grayscale ? (size_t)width * height : (size_t)width * height * 3;
}

/* ============================================================================
 * GOP Decoding - decodes entire GOP containing target frame into cache
 * ============================================================================ */

static int decode_gop_to_cache(
    AVFormatContext *fmt_ctx, AVCodecContext *codec_ctx, int video_stream_idx,
    int64_t *dts_array, int64_t pts_increment, int target_frame,
    H265DecodeState *state, H265FrameCache *cache)
{
    int ret;
    int found_target = 0;
    int first_keyframe_seen = 0;

    /* Temporary buffer for frame conversion */
    uint8_t *temp_frame = (uint8_t *)mxMalloc(cache->frame_size);
    if (!temp_frame) return -1;

    /* Seek to target position */
    ret = av_seek_frame(fmt_ctx, video_stream_idx, dts_array[target_frame], AVSEEK_FLAG_BACKWARD);
    if (ret < 0) {
        avformat_seek_file(fmt_ctx, video_stream_idx, INT64_MIN, 0, 0, 0);
    }
    avcodec_flush_buffers(codec_ctx);

    /* Decode until we find target and hit next GOP boundary */
    while (av_read_frame(fmt_ctx, state->pkt) >= 0) {
        if (state->pkt->stream_index == video_stream_idx) {
            int is_keyframe = (state->pkt->flags & AV_PKT_FLAG_KEY) != 0;

            if (is_keyframe) {
                if (first_keyframe_seen) {
                    if (found_target) {
                        /* Found target and hit next GOP - done */
                        av_packet_unref(state->pkt);
                        break;
                    }
                    /* Haven't found target - clear cache for new GOP */
                    clear_cache(cache);
                }
                first_keyframe_seen = 1;
            }

            ret = avcodec_send_packet(codec_ctx, state->pkt);
            if (ret < 0) {
                av_packet_unref(state->pkt);
                continue;
            }

            while (1) {
                ret = avcodec_receive_frame(codec_ctx, state->frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
                if (ret < 0) {
                    mxFree(temp_frame);
                    return -1;
                }

                int frame_idx = (int)(state->frame->pts / pts_increment);

                /* Convert and add to cache */
                sws_scale(state->sws_ctx,
                          (const uint8_t * const*)state->frame->data,
                          state->frame->linesize, 0, state->height,
                          state->out_frame->data, state->out_frame->linesize);

                convert_frame_to_matlab(state->out_frame, state->width,
                                        state->height, state->is_grayscale, temp_frame);

                /* Release decoder's internal buffer reference */
                av_frame_unref(state->frame);

                if (cache->num_frames >= cache->capacity) {
                    if (!ensure_cache_capacity(cache, cache->capacity * 2)) {
                        mxFree(temp_frame);
                        return -1;
                    }
                }
                add_to_cache(cache, frame_idx, temp_frame);

                if (frame_idx == target_frame) {
                    found_target = 1;
                }
            }
        }
        av_packet_unref(state->pkt);
    }

    /* Flush decoder to get any remaining buffered frames */
    if (!found_target) {
        avcodec_send_packet(codec_ctx, NULL);
        while (1) {
            ret = avcodec_receive_frame(codec_ctx, state->frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
            if (ret < 0) break;

            int frame_idx = (int)(state->frame->pts / pts_increment);

            sws_scale(state->sws_ctx,
                      (const uint8_t * const*)state->frame->data,
                      state->frame->linesize, 0, state->height,
                      state->out_frame->data, state->out_frame->linesize);

            convert_frame_to_matlab(state->out_frame, state->width,
                                    state->height, state->is_grayscale, temp_frame);

            /* Release decoder's internal buffer reference */
            av_frame_unref(state->frame);

            if (cache->num_frames >= cache->capacity) {
                ensure_cache_capacity(cache, cache->capacity * 2);
            }
            add_to_cache(cache, frame_idx, temp_frame);

            if (frame_idx == target_frame) {
                found_target = 1;
            }
        }
    }

    /* Always flush decoder buffers to release internal frame references */
    avcodec_flush_buffers(codec_ctx);

    mxFree(temp_frame);
    return found_target ? 0 : -1;
}

/* ============================================================================
 * Main MEX function
 * ============================================================================ */

void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
    int target_frame;
    int64_t pts_increment;

    AVFormatContext *fmt_ctx = NULL;
    AVCodecContext *codec_ctx = NULL;
    int is_grayscale;
    int video_stream_idx = -1;

    /* Check arguments */
    if (nrhs != 2) {
        mexErrMsgIdAndTxt("read_h265_frame:nrhs", "Two inputs required: video_info and frame_index");
    }
    if (nlhs > 1) {
        mexErrMsgIdAndTxt("read_h265_frame:nlhs", "One output allowed");
    }
    if (!mxIsStruct(prhs[0])) {
        mexErrMsgIdAndTxt("read_h265_frame:notStruct", "First argument must be video_info struct");
    }
    if (!mxIsDouble(prhs[1]) || mxGetNumberOfElements(prhs[1]) != 1) {
        mexErrMsgIdAndTxt("read_h265_frame:notScalar", "Frame index must be a scalar");
    }

    /* Extract fields from video_info struct */
    mxArray *dts_field = mxGetField(prhs[0], 0, "dts");
    mxArray *num_frames_field = mxGetField(prhs[0], 0, "num_frames");
    mxArray *fmt_ctx_field = mxGetField(prhs[0], 0, "fmt_ctx_ptr");
    mxArray *codec_ctx_field = mxGetField(prhs[0], 0, "codec_ctx_ptr");
    mxArray *stream_idx_field = mxGetField(prhs[0], 0, "video_stream_idx");
    mxArray *pts_inc_field = mxGetField(prhs[0], 0, "pts_increment");
    mxArray *cache_ptr_field = mxGetField(prhs[0], 0, "cache_ptr");

    if (!dts_field || !num_frames_field || !fmt_ctx_field || !codec_ctx_field ||
        !stream_idx_field || !pts_inc_field || !cache_ptr_field) {
        mexErrMsgIdAndTxt("read_h265_frame:badStruct", "video_info missing required fields");
    }

    int64_t *dts_array = (int64_t *)mxGetData(dts_field);
    int num_frames = (int)mxGetScalar(num_frames_field);
    pts_increment = *(int64_t *)mxGetData(pts_inc_field);

    fmt_ctx = (AVFormatContext *)(uintptr_t)(*(uint64_t *)mxGetData(fmt_ctx_field));
    codec_ctx = (AVCodecContext *)(uintptr_t)(*(uint64_t *)mxGetData(codec_ctx_field));
    H265FrameCache *cache = (H265FrameCache *)(uintptr_t)(*(uint64_t *)mxGetData(cache_ptr_field));
    video_stream_idx = (int)mxGetScalar(stream_idx_field);

    if (!fmt_ctx || !codec_ctx || !cache) {
        mexErrMsgIdAndTxt("read_h265_frame:nullPtr", "Invalid video_info: null pointers");
    }

    /* Get target frame (convert to 0-based) */
    target_frame = (int)mxGetScalar(prhs[1]) - 1;
    if (target_frame < 0 || target_frame >= num_frames) {
        mexErrMsgIdAndTxt("read_h265_frame:invalidIndex", "Frame index must be between 1 and %d", num_frames);
    }

    /* Check for is_gray field */
    mxArray *is_gray_field = mxGetField(prhs[0], 0, "is_gray");
    if (is_gray_field && mxIsLogical(is_gray_field)) {
        is_grayscale = mxIsLogicalScalarTrue(is_gray_field);
    } else if (is_gray_field && mxIsDouble(is_gray_field)) {
        is_grayscale = (int)mxGetScalar(is_gray_field) != 0;
    } else {
        is_grayscale = (codec_ctx->pix_fmt == AV_PIX_FMT_GRAY8 ||
                        codec_ctx->pix_fmt == AV_PIX_FMT_GRAY16BE ||
                        codec_ctx->pix_fmt == AV_PIX_FMT_GRAY16LE);
    }

    int width = codec_ctx->width;
    int height = codec_ctx->height;

    /* Check if cache is initialized */
    int cache_initialized = (cache->width > 0 && cache->height > 0);

    /* Check cache for frame */
    if (cache_initialized) {
        int cache_idx = find_in_cache(cache, target_frame);
        if (cache_idx >= 0) {
            /* Cache hit */
            if (is_grayscale) {
                plhs[0] = mxCreateNumericMatrix(height, width, mxUINT8_CLASS, mxREAL);
            } else {
                mwSize dims[3] = {height, width, 3};
                plhs[0] = mxCreateNumericArray(3, dims, mxUINT8_CLASS, mxREAL);
            }
            memcpy(mxGetData(plhs[0]), get_cache_frame(cache, cache_idx), cache->frame_size);
            return;
        }
    }

    /* Cache miss - decode GOP */
    if (!cache_initialized) {
        init_cache_format(cache, width, height, is_grayscale);
        if (!ensure_cache_capacity(cache, 60)) {
            mexErrMsgIdAndTxt("read_h265_frame:allocCache", "Could not allocate cache");
        }
    } else {
        clear_cache(cache);
    }

    /* Initialize decode state */
    H265DecodeState state;
    if (!init_decode_state(&state, codec_ctx, width, height, is_grayscale)) {
        mexErrMsgIdAndTxt("read_h265_frame:allocDecode", "Could not initialize decoder");
    }

    /* Decode GOP into cache */
    int result = decode_gop_to_cache(fmt_ctx, codec_ctx, video_stream_idx,
                                     dts_array, pts_increment, target_frame,
                                     &state, cache);
    free_decode_state(&state);

    if (result < 0) {
        mexErrMsgIdAndTxt("read_h265_frame:decode", "Error decoding GOP");
    }

    /* Return frame from cache */
    int cache_idx = find_in_cache(cache, target_frame);
    if (cache_idx >= 0) {
        if (is_grayscale) {
            plhs[0] = mxCreateNumericMatrix(height, width, mxUINT8_CLASS, mxREAL);
        } else {
            mwSize dims[3] = {height, width, 3};
            plhs[0] = mxCreateNumericArray(3, dims, mxUINT8_CLASS, mxREAL);
        }
        memcpy(mxGetData(plhs[0]), get_cache_frame(cache, cache_idx), cache->frame_size);
        return;
    }

    mexErrMsgIdAndTxt("read_h265_frame:notFound", "Frame %d not found", target_frame + 1);
}
