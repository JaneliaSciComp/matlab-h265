/*
 * close_h265_video.c
 * MEX function to close a video file and free FFmpeg resources.
 *
 * Usage: close_h265_video(video_info)
 *   video_info - struct returned by open_h265_video
 *
 * After calling this function, the video_info struct should not be used
 * with read_h265_frame.
 *
 * Compile with:
 *   mex close_h265_video.c -lavformat -lavcodec -lavutil
 */

#include "mex.h"
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <stdint.h>
#include <stdlib.h>
#include "h265_frame_cache.h"

/*
 * Free frame cache memory.
 */
static void free_frame_cache(H265FrameCache *cache)
{
    if (!cache) return;

    /* Free data through handles */
    if (cache->frame_data) {
        if (*cache->frame_data) {
            free(*cache->frame_data);
        }
        free(cache->frame_data);
    }
    if (cache->frame_indices) {
        if (*cache->frame_indices) {
            free(*cache->frame_indices);
        }
        free(cache->frame_indices);
    }
    free(cache);
}

void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
    AVFormatContext *fmt_ctx = NULL;
    AVCodecContext *codec_ctx = NULL;

    /* Check arguments */
    if (nrhs != 1) {
        mexErrMsgIdAndTxt("close_h265_video:nrhs", "One input required: video_info");
    }
    if (!mxIsStruct(prhs[0])) {
        mexErrMsgIdAndTxt("close_h265_video:notStruct", "Argument must be video_info struct from open_ffmpeg_video");
    }

    /* Extract pointer fields */
    mxArray *fmt_ctx_field = mxGetField(prhs[0], 0, "fmt_ctx_ptr");
    mxArray *codec_ctx_field = mxGetField(prhs[0], 0, "codec_ctx_ptr");
    mxArray *cache_ptr_field = mxGetField(prhs[0], 0, "cache_ptr");

    if (!fmt_ctx_field || !codec_ctx_field) {
        mexErrMsgIdAndTxt("close_h265_video:badStruct",
            "video_info must have fmt_ctx_ptr and codec_ctx_ptr fields");
    }

    /* Extract pointers */
    fmt_ctx = (AVFormatContext *)(uintptr_t)(*(uint64_t *)mxGetData(fmt_ctx_field));
    codec_ctx = (AVCodecContext *)(uintptr_t)(*(uint64_t *)mxGetData(codec_ctx_field));

    /* Extract and free cache if present */
    if (cache_ptr_field) {
        H265FrameCache *cache = (H265FrameCache *)(uintptr_t)(*(uint64_t *)mxGetData(cache_ptr_field));
        free_frame_cache(cache);
    }

    /* Free FFmpeg resources if not already freed */
    if (codec_ctx) {
        avcodec_free_context(&codec_ctx);
    }
    if (fmt_ctx) {
        avformat_close_input(&fmt_ctx);
    }

    /* Note: We can't modify the input struct to set pointers to 0,
     * so the caller should discard video_info after calling this function.
     * Calling read_h265_frame after close will result in undefined behavior. */
}
