/*
 * debug_seek.c - Debug tool to see what happens when seeking in video
 * Compile: mex debug_seek.c -lavformat -lavcodec -lavutil
 * Usage: debug_seek('movie.mp4', frame_index)
 */

#include "mex.h"
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>

void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
    if (nrhs != 2) {
        mexErrMsgIdAndTxt("debug_seek:args", "Usage: debug_seek(filename, frame_index)");
    }

    char *filename = mxArrayToString(prhs[0]);
    int target_frame = (int)mxGetScalar(prhs[1]) - 1;

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

    AVRational frame_rate = av_guess_frame_rate(fmt_ctx, video_stream, NULL);

    mexPrintf("time_base: %d/%d\n", video_stream->time_base.num, video_stream->time_base.den);
    mexPrintf("frame_rate: %d/%d\n", frame_rate.num, frame_rate.den);
    mexPrintf("target_frame (0-indexed): %d\n", target_frame);

    /* Calculate target timestamp */
    int64_t target_ts = av_rescale_q(target_frame,
                                     av_inv_q(frame_rate),
                                     video_stream->time_base);
    mexPrintf("target_ts: %lld\n", (long long)target_ts);

    /* Seek */
    int ret = av_seek_frame(fmt_ctx, video_stream_idx, target_ts, AVSEEK_FLAG_BACKWARD);
    mexPrintf("seek result: %d\n", ret);
    avcodec_flush_buffers(codec_ctx);

    /* Decode first 10 frames after seek */
    mexPrintf("\nFirst 10 frames after seek:\n");
    mexPrintf("  decode#  PTS       calc_frame  pict_type\n");

    int decode_count = 0;
    while (decode_count < 10 && av_read_frame(fmt_ctx, pkt) >= 0) {
        if (pkt->stream_index == video_stream_idx) {
            ret = avcodec_send_packet(codec_ctx, pkt);
            if (ret >= 0) {
                while (avcodec_receive_frame(codec_ctx, frame) >= 0) {
                    int64_t pts = frame->pts;
                    int calc_frame = (int)av_rescale_q(pts,
                                                        video_stream->time_base,
                                                        av_inv_q(frame_rate));
                    char pict = (frame->pict_type == AV_PICTURE_TYPE_I) ? 'I' :
                                (frame->pict_type == AV_PICTURE_TYPE_P) ? 'P' :
                                (frame->pict_type == AV_PICTURE_TYPE_B) ? 'B' : '?';

                    mexPrintf("  %3d      %-9lld %-11d %c\n",
                              decode_count, (long long)pts, calc_frame, pict);
                    decode_count++;

                    if (decode_count >= 10) break;
                }
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
