/*
 * write_h265_frame.c
 * MEX function to write a grayscale frame to an H.265 video file.
 * Automatically increments PTS for each frame.
 *
 * Usage: write_h265_frame(writer, frame)
 *   writer - struct returned by open_h265_write
 *   frame  - grayscale image as uint8 matrix (height x width)
 *
 * Compile with:
 *   mex write_h265_frame.c -lavformat -lavcodec -lavutil
 */

#include "mex.h"
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <stdint.h>

/* Must match the struct in open_ffmpeg_write.c */
typedef struct {
    int64_t next_pts;
    int64_t pts_increment;
} WriterState;

void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
    AVFormatContext *fmt_ctx = NULL;
    AVCodecContext *codec_ctx = NULL;
    AVFrame *frame = NULL;
    AVPacket *pkt = NULL;
    WriterState *state = NULL;
    int ret;
    int width, height;
    int stream_idx;

    /* Check arguments */
    if (nrhs != 2) {
        mexErrMsgIdAndTxt("write_h265_frame:nrhs",
            "Two inputs required: writer and frame");
    }
    if (!mxIsStruct(prhs[0])) {
        mexErrMsgIdAndTxt("write_h265_frame:notStruct",
            "First argument must be writer struct from open_ffmpeg_write");
    }
    if (!mxIsUint8(prhs[1]) || mxGetNumberOfDimensions(prhs[1]) != 2) {
        mexErrMsgIdAndTxt("write_h265_frame:notUint8",
            "Frame must be a 2D uint8 matrix");
    }

    /* Extract fields from writer struct */
    mxArray *width_field = mxGetField(prhs[0], 0, "width");
    mxArray *height_field = mxGetField(prhs[0], 0, "height");
    mxArray *fmt_ctx_field = mxGetField(prhs[0], 0, "fmt_ctx_ptr");
    mxArray *codec_ctx_field = mxGetField(prhs[0], 0, "codec_ctx_ptr");
    mxArray *frame_field = mxGetField(prhs[0], 0, "frame_ptr");
    mxArray *state_field = mxGetField(prhs[0], 0, "state_ptr");
    mxArray *stream_idx_field = mxGetField(prhs[0], 0, "stream_idx");

    if (!width_field || !height_field || !fmt_ctx_field || !codec_ctx_field ||
        !frame_field || !state_field || !stream_idx_field) {
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

    /* Validate pointers */
    if (!fmt_ctx || !codec_ctx || !frame || !state) {
        mexErrMsgIdAndTxt("write_h265_frame:nullPtr",
            "Invalid writer: null pointers. Was close_ffmpeg_write already called?");
    }

    /* Check frame dimensions */
    const mwSize *dims = mxGetDimensions(prhs[1]);
    if (dims[0] != height || dims[1] != width) {
        mexErrMsgIdAndTxt("write_h265_frame:badDimensions",
            "Frame dimensions (%d x %d) don't match writer (%d x %d)",
            (int)dims[0], (int)dims[1], height, width);
    }

    /* Get input data */
    uint8_t *in_data = (uint8_t *)mxGetData(prhs[1]);

    /* Make frame writable */
    ret = av_frame_make_writable(frame);
    if (ret < 0) {
        mexErrMsgIdAndTxt("write_h265_frame:makeWritable",
            "Could not make frame writable");
    }

    /* Copy data from MATLAB (column-major) to frame (row-major) */
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            frame->data[0][y * frame->linesize[0] + x] = in_data[x * height + y];
        }
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
