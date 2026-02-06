/*
 * close_h265_write.c
 * MEX function to flush the encoder and close the video file.
 *
 * Usage: close_h265_write(writer)
 *   writer - struct returned by open_h265_write
 *
 * This flushes any remaining frames from the encoder, writes the file
 * trailer, and frees all resources.
 *
 * Compile with:
 *   mex close_h265_write.c -lavformat -lavcodec -lavutil -lswscale
 */

#include "mex.h"
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <stdint.h>
#include <stdlib.h>

void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
    AVFormatContext *fmt_ctx = NULL;
    AVCodecContext *codec_ctx = NULL;
    AVFrame *frame = NULL;
    AVPacket *pkt = NULL;
    void *state = NULL;
    struct SwsContext *sws_ctx = NULL;
    int ret;
    int stream_idx;

    /* Check arguments */
    if (nrhs != 1) {
        mexErrMsgIdAndTxt("close_h265_write:nrhs",
            "One input required: writer struct");
    }
    if (!mxIsStruct(prhs[0])) {
        mexErrMsgIdAndTxt("close_h265_write:notStruct",
            "Argument must be writer struct from open_ffmpeg_write");
    }

    /* Extract fields from writer struct */
    mxArray *fmt_ctx_field = mxGetField(prhs[0], 0, "fmt_ctx_ptr");
    mxArray *codec_ctx_field = mxGetField(prhs[0], 0, "codec_ctx_ptr");
    mxArray *frame_field = mxGetField(prhs[0], 0, "frame_ptr");
    mxArray *state_field = mxGetField(prhs[0], 0, "state_ptr");
    mxArray *stream_idx_field = mxGetField(prhs[0], 0, "stream_idx");
    mxArray *sws_ctx_field = mxGetField(prhs[0], 0, "sws_ctx_ptr");

    if (!fmt_ctx_field || !codec_ctx_field || !frame_field || !state_field || !stream_idx_field) {
        mexErrMsgIdAndTxt("close_h265_write:badStruct",
            "Writer struct is missing required fields");
    }

    fmt_ctx = (AVFormatContext *)(uintptr_t)(*(uint64_t *)mxGetData(fmt_ctx_field));
    codec_ctx = (AVCodecContext *)(uintptr_t)(*(uint64_t *)mxGetData(codec_ctx_field));
    frame = (AVFrame *)(uintptr_t)(*(uint64_t *)mxGetData(frame_field));
    state = (void *)(uintptr_t)(*(uint64_t *)mxGetData(state_field));
    stream_idx = (int)mxGetScalar(stream_idx_field);
    if (sws_ctx_field) {
        sws_ctx = (struct SwsContext *)(uintptr_t)(*(uint64_t *)mxGetData(sws_ctx_field));
    }

    /* Check if already closed */
    if (!fmt_ctx || !codec_ctx) {
        mexWarnMsgIdAndTxt("close_h265_write:alreadyClosed",
            "Writer appears to already be closed");
        return;
    }

    /* Allocate packet for flushing */
    pkt = av_packet_alloc();
    if (!pkt) {
        mexErrMsgIdAndTxt("close_h265_write:allocPacket",
            "Could not allocate packet");
    }

    /* Flush encoder by sending NULL frame */
    ret = avcodec_send_frame(codec_ctx, NULL);
    if (ret < 0 && ret != AVERROR_EOF) {
        av_packet_free(&pkt);
        mexWarnMsgIdAndTxt("close_h265_write:flushError",
            "Error flushing encoder");
    }

    /* Receive and write remaining packets */
    while (1) {
        ret = avcodec_receive_packet(codec_ctx, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        } else if (ret < 0) {
            av_packet_free(&pkt);
            mexWarnMsgIdAndTxt("close_h265_write:receiveError",
                "Error receiving packet during flush");
            break;
        }

        /* Rescale timestamps */
        av_packet_rescale_ts(pkt, codec_ctx->time_base,
                             fmt_ctx->streams[stream_idx]->time_base);
        pkt->stream_index = stream_idx;

        /* Write packet */
        ret = av_interleaved_write_frame(fmt_ctx, pkt);
        if (ret < 0) {
            mexWarnMsgIdAndTxt("close_h265_write:writeError",
                "Error writing packet during flush");
        }
    }

    av_packet_free(&pkt);

    /* Write file trailer */
    ret = av_write_trailer(fmt_ctx);
    if (ret < 0) {
        mexWarnMsgIdAndTxt("close_h265_write:trailerError",
            "Error writing file trailer");
    }

    /* Free resources */
    if (sws_ctx) {
        sws_freeContext(sws_ctx);
    }
    if (state) {
        free(state);
    }
    if (frame) {
        av_frame_free(&frame);
    }
    if (codec_ctx) {
        avcodec_free_context(&codec_ctx);
    }
    if (fmt_ctx) {
        if (!(fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
            avio_closep(&fmt_ctx->pb);
        }
        avformat_free_context(fmt_ctx);
    }
}
