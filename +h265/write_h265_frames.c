/*
 * write_h265_frames.c
 * MEX function to write multiple frames to an h.265 video file.
 * Supports both grayscale (3D) and RGB (4D) input based on writer mode.
 * Automatically increments PTS for each frame.
 *
 * Usage: write_h265_frames(writer, frames)
 *   writer - struct returned by open_h265_write
 *   frames - grayscale as uint8 (height x width x num_frames)
 *            or RGB as uint8 (height x width x 3 x num_frames)
 *
 * Compile with:
 *   mex write_h265_frames.c -lavformat -lavcodec -lavutil -lswscale
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
    int num_frames;

    /* Check arguments */
    if (nrhs != 2) {
        mexErrMsgIdAndTxt("write_h265_frames:nrhs",
            "Two inputs required: writer and frames");
    }
    if (!mxIsStruct(prhs[0])) {
        mexErrMsgIdAndTxt("write_h265_frames:notStruct",
            "First argument must be writer struct from open_h265_write");
    }
    if (!mxIsUint8(prhs[1])) {
        mexErrMsgIdAndTxt("write_h265_frames:notUint8",
            "Frames must be a uint8 array");
    }
    mwSize ndims = mxGetNumberOfDimensions(prhs[1]);
    /* MATLAB drops trailing singleton dimensions, so:
     * - Grayscale single frame: 2D (height x width)
     * - Grayscale batch: 3D (height x width x num_frames)
     * - RGB single frame: 3D (height x width x 3)
     * - RGB batch: 4D (height x width x 3 x num_frames)
     */
    if (ndims < 2 || ndims > 4) {
        mexErrMsgIdAndTxt("write_h265_frames:badDims",
            "Frames must be 2D-4D array");
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
        mexErrMsgIdAndTxt("write_h265_frames:badStruct",
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
        mexErrMsgIdAndTxt("write_h265_frames:nullPtr",
            "Invalid writer: null pointers. Was close_ffmpeg_write already called?");
    }

    /* Check frame dimensions based on color mode */
    const mwSize *dims = mxGetDimensions(prhs[1]);
    if (is_color) {
        /* RGB mode: expect height x width x 3 (single) or height x width x 3 x num_frames (batch) */
        if (ndims == 3) {
            /* Single frame or batch - check if dims[2] is 3 (single) or num_frames */
            if (dims[0] != height || dims[1] != width || dims[2] != 3) {
                mexErrMsgIdAndTxt("write_h265_frames:badDimensions",
                    "RGB frames dimensions (%d x %d x %d) don't match writer (%d x %d x 3)",
                    (int)dims[0], (int)dims[1], (int)dims[2], height, width);
            }
            num_frames = 1;
        } else if (ndims == 4) {
            if (dims[0] != height || dims[1] != width || dims[2] != 3) {
                mexErrMsgIdAndTxt("write_h265_frames:badDimensions",
                    "RGB frames dimensions (%d x %d x %d x ...) don't match writer (%d x %d x 3 x N)",
                    (int)dims[0], (int)dims[1], (int)dims[2], height, width);
            }
            num_frames = (int)dims[3];
        } else {
            mexErrMsgIdAndTxt("write_h265_frames:badDimensions",
                "RGB frames must be 3D (single) or 4D (batch)");
        }
    } else {
        /* Grayscale mode: expect height x width (single) or height x width x num_frames (batch) */
        if (ndims == 2) {
            /* Single frame */
            if (dims[0] != height || dims[1] != width) {
                mexErrMsgIdAndTxt("write_h265_frames:badDimensions",
                    "Grayscale frame dimensions (%d x %d) don't match writer (%d x %d)",
                    (int)dims[0], (int)dims[1], height, width);
            }
            num_frames = 1;
        } else if (ndims == 3) {
            /* Batch of frames */
            if (dims[0] != height || dims[1] != width) {
                mexErrMsgIdAndTxt("write_h265_frames:badDimensions",
                    "Grayscale frames dimensions (%d x %d x ...) don't match writer (%d x %d x N)",
                    (int)dims[0], (int)dims[1], height, width);
            }
            num_frames = (int)dims[2];
        } else {
            mexErrMsgIdAndTxt("write_h265_frames:badDimensions",
                "Grayscale frames must be 2D (single) or 3D (batch)");
        }
    }

    /* Get input data */
    uint8_t *in_data = (uint8_t *)mxGetData(prhs[1]);

    /* Allocate packet once for all frames */
    pkt = av_packet_alloc();
    if (!pkt) {
        mexErrMsgIdAndTxt("write_h265_frames:allocPacket",
            "Could not allocate packet");
    }

    /* Allocate conversion buffer once */
    uint8_t *conv_buffer = NULL;
    if (is_color) {
        conv_buffer = (uint8_t *)mxMalloc(height * width * 3);
    } else {
        conv_buffer = (uint8_t *)mxMalloc(height * width);
    }
    if (!conv_buffer) {
        av_packet_free(&pkt);
        mexErrMsgIdAndTxt("write_h265_frames:allocBuffer",
            "Could not allocate conversion buffer");
    }

    /* Process each frame */
    size_t frame_size = is_color ? (size_t)height * width * 3 : (size_t)height * width;
    size_t plane_size = (size_t)height * width;

    for (int f = 0; f < num_frames; f++) {
        /* Make frame writable */
        ret = av_frame_make_writable(frame);
        if (ret < 0) {
            mxFree(conv_buffer);
            av_packet_free(&pkt);
            mexErrMsgIdAndTxt("write_h265_frames:makeWritable",
                "Could not make frame writable");
        }

        /* Get pointer to this frame's data */
        uint8_t *frame_data = in_data + f * frame_size;

        if (is_color) {
            /* RGB mode: convert from MATLAB column-major RGB to row-major RGB24, then to YUV420P */
            /* Convert from MATLAB (height x width x 3, column-major) to RGB24 (row-major, interleaved) */
            for (int y = 0; y < height; y++) {
                for (int x = 0; x < width; x++) {
                    int matlab_idx = x * height + y;  /* Column-major index for each plane */
                    int rgb_idx = (y * width + x) * 3;  /* Row-major interleaved RGB */
                    conv_buffer[rgb_idx + 0] = frame_data[matlab_idx];                  /* R */
                    conv_buffer[rgb_idx + 1] = frame_data[matlab_idx + plane_size];     /* G */
                    conv_buffer[rgb_idx + 2] = frame_data[matlab_idx + 2 * plane_size]; /* B */
                }
            }

            /* Convert RGB24 to YUV420P using swscale */
            uint8_t *src_data[1] = {conv_buffer};
            int src_linesize[1] = {width * 3};
            sws_scale(sws_ctx, (const uint8_t * const*)src_data, src_linesize,
                      0, height, frame->data, frame->linesize);
        } else {
            /* Grayscale mode: convert from MATLAB column-major to row-major GRAY8, then to YUV420P */
            /* Convert from MATLAB column-major to row-major */
            for (int y = 0; y < height; y++) {
                for (int x = 0; x < width; x++) {
                    conv_buffer[y * width + x] = frame_data[x * height + y];
                }
            }

            /* Convert GRAY8 to YUV420P using swscale */
            uint8_t *src_data[1] = {conv_buffer};
            int src_linesize[1] = {width};
            sws_scale(sws_ctx, (const uint8_t * const*)src_data, src_linesize,
                      0, height, frame->data, frame->linesize);
        }

        /* Set PTS and increment for next frame */
        frame->pts = state->next_pts;
        state->next_pts += state->pts_increment;

        /* Send frame to encoder */
        ret = avcodec_send_frame(codec_ctx, frame);
        if (ret < 0) {
            mxFree(conv_buffer);
            av_packet_free(&pkt);
            mexErrMsgIdAndTxt("write_h265_frames:sendFrame",
                "Error sending frame %d to encoder", f + 1);
        }

        /* Receive and write encoded packets */
        while (ret >= 0) {
            ret = avcodec_receive_packet(codec_ctx, pkt);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            } else if (ret < 0) {
                mxFree(conv_buffer);
                av_packet_free(&pkt);
                mexErrMsgIdAndTxt("write_h265_frames:receivePacket",
                    "Error receiving packet from encoder at frame %d", f + 1);
            }

            /* Rescale timestamps */
            av_packet_rescale_ts(pkt, codec_ctx->time_base,
                                 fmt_ctx->streams[stream_idx]->time_base);
            pkt->stream_index = stream_idx;

            /* Write packet */
            ret = av_interleaved_write_frame(fmt_ctx, pkt);
            if (ret < 0) {
                mxFree(conv_buffer);
                av_packet_free(&pkt);
                mexErrMsgIdAndTxt("write_h265_frames:writeFrame",
                    "Error writing packet to file at frame %d", f + 1);
            }
        }
    }

    mxFree(conv_buffer);
    av_packet_free(&pkt);
}
