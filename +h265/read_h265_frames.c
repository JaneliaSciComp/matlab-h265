/*
 * read_h265_frames.c
 * MEX function to read a contiguous range of frames efficiently.
 * Seeks once to the start, then decodes sequentially through the range.
 *
 * Usage: frames = read_h265_frames(video_info, start_frame, end_frame)
 *   video_info  - struct returned by open_h265_video
 *   start_frame - 1-based starting frame index
 *   end_frame   - 1-based ending frame index (inclusive)
 *   frames      - grayscale: uint8 3D array (width x height x num_frames)
 *                 RGB: uint8 4D array (3 x width x height x num_frames)
 *
 * NOTE: Output is in row-major order for performance. Caller should use:
 *   permute(frames, [2 1 3])     for grayscale -> (height x width x num_frames)
 *   permute(frames, [3 2 1 4])   for RGB -> (height x width x 3 x num_frames)
 *
 * Compile with:
 *   mex read_h265_frames.c -lavformat -lavcodec -lavutil -lswscale
 */

#include "mex.h"
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <stdint.h>
#include "h265_decode_common.h"

void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
    int start_frame, end_frame, num_frames_to_read;
    int64_t pts_increment;

    AVFormatContext *fmt_ctx = NULL;
    AVCodecContext *codec_ctx = NULL;

    int video_stream_idx = -1;
    int width, height;
    int is_grayscale;

    /* Check arguments */
    if (nrhs != 3) {
        mexErrMsgIdAndTxt("read_h265_frames:nrhs",
            "Three inputs required: video_info, start_frame, end_frame");
    }
    if (nlhs > 1) {
        mexErrMsgIdAndTxt("read_h265_frames:nlhs", "One output allowed");
    }
    if (!mxIsStruct(prhs[0])) {
        mexErrMsgIdAndTxt("read_h265_frames:notStruct",
            "First argument must be video_info struct");
    }
    if (!mxIsDouble(prhs[1]) || mxGetNumberOfElements(prhs[1]) != 1) {
        mexErrMsgIdAndTxt("read_h265_frames:notScalar", "start_frame must be a scalar");
    }
    if (!mxIsDouble(prhs[2]) || mxGetNumberOfElements(prhs[2]) != 1) {
        mexErrMsgIdAndTxt("read_h265_frames:notScalar", "end_frame must be a scalar");
    }

    /* Extract fields from video_info struct */
    mxArray *dts_field = mxGetField(prhs[0], 0, "dts");
    mxArray *num_frames_field = mxGetField(prhs[0], 0, "num_frames");
    mxArray *width_field = mxGetField(prhs[0], 0, "width");
    mxArray *height_field = mxGetField(prhs[0], 0, "height");
    mxArray *fmt_ctx_field = mxGetField(prhs[0], 0, "fmt_ctx_ptr");
    mxArray *codec_ctx_field = mxGetField(prhs[0], 0, "codec_ctx_ptr");
    mxArray *stream_idx_field = mxGetField(prhs[0], 0, "video_stream_idx");
    mxArray *pts_inc_field = mxGetField(prhs[0], 0, "pts_increment");

    if (!dts_field || !num_frames_field || !width_field || !height_field ||
        !fmt_ctx_field || !codec_ctx_field || !stream_idx_field || !pts_inc_field) {
        mexErrMsgIdAndTxt("read_h265_frames:badStruct",
            "video_info must have all required fields");
    }

    int64_t *dts_array = (int64_t *)mxGetData(dts_field);
    int total_frames = (int)mxGetScalar(num_frames_field);
    width = (int)mxGetScalar(width_field);
    height = (int)mxGetScalar(height_field);
    pts_increment = *(int64_t *)mxGetData(pts_inc_field);

    fmt_ctx = (AVFormatContext *)(uintptr_t)(*(uint64_t *)mxGetData(fmt_ctx_field));
    codec_ctx = (AVCodecContext *)(uintptr_t)(*(uint64_t *)mxGetData(codec_ctx_field));
    video_stream_idx = (int)mxGetScalar(stream_idx_field);

    if (!fmt_ctx || !codec_ctx) {
        mexErrMsgIdAndTxt("read_h265_frames:nullPtr",
            "Invalid video_info: null pointers");
    }

    /* Get frame range (convert to 0-based) */
    start_frame = (int)mxGetScalar(prhs[1]) - 1;
    end_frame = (int)mxGetScalar(prhs[2]) - 1;

    if (start_frame < 0 || start_frame >= total_frames) {
        mexErrMsgIdAndTxt("read_h265_frames:invalidIndex",
            "start_frame must be between 1 and %d", total_frames);
    }
    if (end_frame < 0 || end_frame >= total_frames) {
        mexErrMsgIdAndTxt("read_h265_frames:invalidIndex",
            "end_frame must be between 1 and %d", total_frames);
    }
    if (end_frame < start_frame) {
        mexErrMsgIdAndTxt("read_h265_frames:invalidRange",
            "end_frame must be >= start_frame");
    }

    num_frames_to_read = end_frame - start_frame + 1;

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

    /* Create output array with swapped dimensions for row-major storage */
    uint8_t *out_data;
    size_t frame_size;
    if (is_grayscale) {
        /* width x height x frames (caller permutes to height x width x frames) */
        mwSize dims[3] = {width, height, num_frames_to_read};
        plhs[0] = mxCreateNumericArray(3, dims, mxUINT8_CLASS, mxREAL);
        frame_size = (size_t)height * width;
    } else {
        /* 3 x width x height x frames (caller permutes to height x width x 3 x frames) */
        mwSize dims[4] = {3, width, height, num_frames_to_read};
        plhs[0] = mxCreateNumericArray(4, dims, mxUINT8_CLASS, mxREAL);
        frame_size = (size_t)height * width * 3;
    }
    out_data = (uint8_t *)mxGetData(plhs[0]);

    /* Initialize decode state */
    H265DecodeState state;
    if (!init_decode_state(&state, codec_ctx, width, height, is_grayscale)) {
        mexErrMsgIdAndTxt("read_h265_frames:allocDecode", "Could not initialize decoder");
    }

    /* Decode frames in row-major order (fast memcpy, caller does permute) */
    int frames_captured = decode_frame_range_rowmajor(
        fmt_ctx, codec_ctx, video_stream_idx,
        dts_array, pts_increment,
        start_frame, end_frame,
        &state, out_data, frame_size);

    free_decode_state(&state);

    if (frames_captured < 0) {
        mexErrMsgIdAndTxt("read_h265_frames:decode", "Error during decoding");
    }

    if (frames_captured < num_frames_to_read) {
        mexErrMsgIdAndTxt("read_h265_frames:notFound",
            "Only captured %d of %d frames (%d missing)",
            frames_captured, num_frames_to_read, num_frames_to_read - frames_captured);
    }
}
