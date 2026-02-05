/*
 * debug_seek_ts.c - Debug seek with raw timestamp
 * mex debug_seek_ts.c -lavformat -lavcodec -lavutil
 * Usage: debug_seek_ts('movie.mp4', timestamp)
 */

#include "mex.h"
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>

void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
    char *filename = mxArrayToString(prhs[0]);
    int64_t target_ts = (int64_t)mxGetScalar(prhs[1]);

    AVFormatContext *fmt_ctx = NULL;
    AVCodecContext *codec_ctx = NULL;
    const AVCodec *codec = NULL;
    AVFrame *frame = NULL;
    AVPacket *pkt = NULL;

    avformat_open_input(&fmt_ctx, filename, NULL, NULL);
    avformat_find_stream_info(fmt_ctx, NULL);

    int video_stream_idx = -1;
    AVStream *video_stream = NULL;
    for (int i = 0; i < fmt_ctx->nb_streams; i++) {
        if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_idx = i;
            video_stream = fmt_ctx->streams[i];
            break;
        }
    }

    codec = avcodec_find_decoder(video_stream->codecpar->codec_id);
    codec_ctx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(codec_ctx, video_stream->codecpar);
    avcodec_open2(codec_ctx, codec, NULL);

    frame = av_frame_alloc();
    pkt = av_packet_alloc();

    mexPrintf("target_ts: %lld\n", (long long)target_ts);

    int ret = av_seek_frame(fmt_ctx, video_stream_idx, target_ts, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(codec_ctx);

    /* Decode first frame */
    while (av_read_frame(fmt_ctx, pkt) >= 0) {
        if (pkt->stream_index == video_stream_idx) {
            avcodec_send_packet(codec_ctx, pkt);
            if (avcodec_receive_frame(codec_ctx, frame) >= 0) {
                mexPrintf("First frame PTS: %lld\n", (long long)frame->pts);
                break;
            }
        }
        av_packet_unref(pkt);
    }

    av_packet_free(&pkt);
    av_frame_free(&frame);
    avcodec_free_context(&codec_ctx);
    avformat_close_input(&fmt_ctx);
    mxFree(filename);
}
