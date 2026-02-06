/*
 * write_h265_frame.c
 * MEX function to write a frame to an H.265 video file.
 * Supports both grayscale (2D) and RGB (3D) input based on writer mode.
 * Automatically increments PTS for each frame.
 *
 * Usage: write_h265_frame(writer, frame)
 *   writer - struct returned by open_h265_write
 *   frame  - grayscale image as uint8 (height x width) or RGB as uint8 (height x width x 3)
 *
 * Compile with:
 *   mex write_h265_frame.c -lavformat -lavcodec -lavutil -lswscale
 */

#include "mex.h"
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <stdint.h>

/* Must match the struct in open_h265_write.c */
typedef struct {
    int64_t next_pts;
    int64_t pts_increment;
    int is_color;  /* 0 for grayscale, 1 for RGB */
} WriterState;

void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
    AVFormatContext *fmt_ctx = NULL;
    AVCodecContext *codec_ctx = NULL;
    AVFrame *frame = NULL;
    AVPacket *pkt = NULL;
    WriterState *state = NULL;
    struct SwsContext *sws_ctx = NULL;
    int ret;
    int width, height;
    int stream_idx;
    int is_color;

    /* Check arguments */
    if (nrhs != 2) {
        mexErrMsgIdAndTxt("write_h265_frame:nrhs",
            "Two inputs required: writer and frame");
    }
    if (!mxIsStruct(prhs[0])) {
        mexErrMsgIdAndTxt("write_h265_frame:notStruct",
            "First argument must be writer struct from open_h265_write");
    }
    if (!mxIsUint8(prhs[1])) {
        mexErrMsgIdAndTxt("write_h265_frame:notUint8",
            "Frame must be a uint8 array");
    }
    mwSize ndims = mxGetNumberOfDimensions(prhs[1]);
    if (ndims != 2 && ndims != 3) {
        mexErrMsgIdAndTxt("write_h265_frame:badDims",
            "Frame must be 2D (grayscale) or 3D (RGB)");
    }

    /* Extract fields from writer struct */
    mxArray *width_field = mxGetField(prhs[0], 0, "width");
    mxArray *height_field = mxGetField(prhs[0], 0, "height");
    mxArray *fmt_ctx_field = mxGetField(prhs[0], 0, "fmt_ctx_ptr");
    mxArray *codec_ctx_field = mxGetField(prhs[0], 0, "codec_ctx_ptr");
    mxArray *frame_field = mxGetField(prhs[0], 0, "frame_ptr");
    mxArray *state_field = mxGetField(prhs[0], 0, "state_ptr");
    mxArray *stream_idx_field = mxGetField(prhs[0], 0, "stream_idx");
    mxArray *sws_ctx_field = mxGetField(prhs[0], 0, "sws_ctx_ptr");
    mxArray *is_color_field = mxGetField(prhs[0], 0, "is_color");

    if (!width_field || !height_field || !fmt_ctx_field || !codec_ctx_field ||
        !frame_field || !state_field || !stream_idx_field || !sws_ctx_field || !is_color_field) {
        mexErrMsgIdAndTxt("write_h265_frame:badStruct",
            "Writer struct is missing required fields");
    }

    width = (int)mxGetScalar(width_field);
    height = (int)mxGetScalar(height_field);
    fmt_ctx = (AVFormatContext *)(uintptr_t)(*(uint64_t *)mxGetData(fmt_ctx_field));
    codec_ctx = (AVCodecContext *)(uintptr_t)(*(uint64_t *)mxGetData(codec_ctx_field));
    frame = (AVFrame *)(uintptr_t)(*(uint64_t *)mxGetData(frame_field));
    state = (WriterState *)(uintptr_t)(*(uint64_t *)mxGetData(state_field));
    stream_idx = (int)mxGetScalar(stream_idx_field);
    sws_ctx = (struct SwsContext *)(uintptr_t)(*(uint64_t *)mxGetData(sws_ctx_field));
    is_color = (int)mxGetScalar(is_color_field);

    /* Validate pointers */
    if (!fmt_ctx || !codec_ctx || !frame || !state) {
        mexErrMsgIdAndTxt("write_h265_frame:nullPtr",
            "Invalid writer: null pointers. Was close_ffmpeg_write already called?");
    }

    /* Check frame dimensions based on color mode */
    const mwSize *dims = mxGetDimensions(prhs[1]);
    if (is_color) {
        /* RGB mode: expect height x width x 3 */
        if (ndims != 3 || dims[0] != height || dims[1] != width || dims[2] != 3) {
            mexErrMsgIdAndTxt("write_h265_frame:badDimensions",
                "RGB frame dimensions (%d x %d x %d) don't match writer (%d x %d x 3)",
                (int)dims[0], (int)dims[1], (int)(ndims == 3 ? dims[2] : 1), height, width);
        }
    } else {
        /* Grayscale mode: expect height x width */
        if (ndims != 2 || dims[0] != height || dims[1] != width) {
            mexErrMsgIdAndTxt("write_h265_frame:badDimensions",
                "Grayscale frame dimensions (%d x %d) don't match writer (%d x %d)",
                (int)dims[0], (int)dims[1], height, width);
        }
    }

    /* Get input data */
    uint8_t *in_data = (uint8_t *)mxGetData(prhs[1]);

    /* Make frame writable */
    ret = av_frame_make_writable(frame);
    if (ret < 0) {
        mexErrMsgIdAndTxt("write_h265_frame:makeWritable",
            "Could not make frame writable");
    }

    if (is_color) {
        /* RGB mode: convert from MATLAB column-major RGB to row-major RGB24, then to YUV420P */
        uint8_t *rgb_buffer = (uint8_t *)mxMalloc(height * width * 3);
        if (!rgb_buffer) {
            mexErrMsgIdAndTxt("write_h265_frame:allocRgb",
                "Could not allocate RGB buffer");
        }

        /* Convert from MATLAB (height x width x 3, column-major) to RGB24 (row-major, interleaved) */
        size_t plane_size = (size_t)height * width;
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                int matlab_idx = x * height + y;  /* Column-major index for each plane */
                int rgb_idx = (y * width + x) * 3;  /* Row-major interleaved RGB */
                rgb_buffer[rgb_idx + 0] = in_data[matlab_idx];                  /* R */
                rgb_buffer[rgb_idx + 1] = in_data[matlab_idx + plane_size];     /* G */
                rgb_buffer[rgb_idx + 2] = in_data[matlab_idx + 2 * plane_size]; /* B */
            }
        }

        /* Convert RGB24 to YUV420P using swscale */
        uint8_t *src_data[1] = {rgb_buffer};
        int src_linesize[1] = {width * 3};
        sws_scale(sws_ctx, (const uint8_t * const*)src_data, src_linesize,
                  0, height, frame->data, frame->linesize);

        mxFree(rgb_buffer);
    } else {
        /* Grayscale mode: convert from MATLAB column-major to row-major GRAY8, then to YUV420P */
        uint8_t *gray_buffer = (uint8_t *)mxMalloc(height * width);
        if (!gray_buffer) {
            mexErrMsgIdAndTxt("write_h265_frame:allocGray",
                "Could not allocate grayscale buffer");
        }

        /* Convert from MATLAB column-major to row-major */
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                gray_buffer[y * width + x] = in_data[x * height + y];
            }
        }

        /* Convert GRAY8 to YUV420P using swscale */
        uint8_t *src_data[1] = {gray_buffer};
        int src_linesize[1] = {width};
        sws_scale(sws_ctx, (const uint8_t * const*)src_data, src_linesize,
                  0, height, frame->data, frame->linesize);

        mxFree(gray_buffer);
    }

    /* Set PTS and increment for next frame */
    frame->pts = state->next_pts;
    state->next_pts += state->pts_increment;

    /* Allocate packet */
    pkt = av_packet_alloc();
    if (!pkt) {
        mexErrMsgIdAndTxt("write_h265_frame:allocPacket",
            "Could not allocate packet");
    }

    /* Send frame to encoder */
    ret = avcodec_send_frame(codec_ctx, frame);
    if (ret < 0) {
        av_packet_free(&pkt);
        mexErrMsgIdAndTxt("write_h265_frame:sendFrame",
            "Error sending frame to encoder");
    }

    /* Receive and write encoded packets */
    while (ret >= 0) {
        ret = avcodec_receive_packet(codec_ctx, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        } else if (ret < 0) {
            av_packet_free(&pkt);
            mexErrMsgIdAndTxt("write_h265_frame:receivePacket",
                "Error receiving packet from encoder");
        }

        /* Rescale timestamps */
        av_packet_rescale_ts(pkt, codec_ctx->time_base,
                             fmt_ctx->streams[stream_idx]->time_base);
        pkt->stream_index = stream_idx;

        /* Write packet */
        ret = av_interleaved_write_frame(fmt_ctx, pkt);
        if (ret < 0) {
            av_packet_free(&pkt);
            mexErrMsgIdAndTxt("write_h265_frame:writeFrame",
                "Error writing packet to file");
        }
    }

    av_packet_free(&pkt);
}
