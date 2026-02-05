/*
 * debug_prev_keyframe.c - Decode from the keyframe BEFORE a given DTS
 * mex debug_prev_keyframe.c -lavformat -lavcodec -lavutil
 * Usage: debug_prev_keyframe('movie.mp4', target_dts)
 *
 * Seeks to the keyframe before target_dts, then decodes up to and including target_dts
 */

#include "mex.h"
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>

void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
    if (nrhs != 2) {
        mexErrMsgIdAndTxt("debug_prev_keyframe:args", "Usage: debug_prev_keyframe(filename, target_dts)");
    }

    char *filename = mxArrayToString(prhs[0]);
    int64_t target_dts = (int64_t)mxGetScalar(prhs[1]);

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

    /* First, find the keyframe AT target_dts by seeking to it */
    mexPrintf("Target DTS: %lld\n", (long long)target_dts);
    mexPrintf("Seeking to find keyframe at or before target_dts...\n");

    av_seek_frame(fmt_ctx, video_stream_idx, target_dts, AVSEEK_FLAG_BACKWARD);

    /* Read first packet to find where we landed */
    int64_t keyframe_at_target = -1;
    if (av_read_frame(fmt_ctx, pkt) >= 0) {
        if (pkt->stream_index == video_stream_idx) {
            keyframe_at_target = pkt->dts;
            mexPrintf("Keyframe at/before target: DTS %lld\n", (long long)keyframe_at_target);
        }
        av_packet_unref(pkt);
    }

    /* Now seek to BEFORE that keyframe to find the previous one */
    if (keyframe_at_target > 0) {
        mexPrintf("\nNow seeking to find the PREVIOUS keyframe (before DTS %lld)...\n", (long long)keyframe_at_target);
        av_seek_frame(fmt_ctx, video_stream_idx, keyframe_at_target - 1, AVSEEK_FLAG_BACKWARD);
    }
    avcodec_flush_buffers(codec_ctx);

    /* Read and decode packets until we pass target_dts */
    mexPrintf("\nPackets and decoded frames:\n");
    mexPrintf("%-8s %-10s %-6s | %-10s %-6s\n", "pkt#", "pkt_dts", "KEY?", "frame_dts", "type");
    mexPrintf("--------------------------------------------\n");

    int pkt_count = 0;
    int done = 0;

    while (!done && av_read_frame(fmt_ctx, pkt) >= 0) {
        if (pkt->stream_index == video_stream_idx) {
            pkt_count++;
            int64_t pkt_dts = pkt->dts;
            const char *key_str = (pkt->flags & AV_PKT_FLAG_KEY) ? "KEY" : "";

            mexPrintf("%-8d %-10lld %-6s", pkt_count, (long long)pkt_dts, key_str);

            int ret = avcodec_send_packet(codec_ctx, pkt);
            if (ret < 0) {
                mexPrintf(" | send_packet failed\n");
                av_packet_unref(pkt);
                continue;
            }

            /* Try to receive frames */
            int frame_received = 0;
            while ((ret = avcodec_receive_frame(codec_ctx, frame)) >= 0) {
                char pict_type = (frame->pict_type == AV_PICTURE_TYPE_I) ? 'I' :
                                 (frame->pict_type == AV_PICTURE_TYPE_P) ? 'P' :
                                 (frame->pict_type == AV_PICTURE_TYPE_B) ? 'B' : '?';

                if (!frame_received) {
                    mexPrintf(" | %-10lld %-6c\n", (long long)frame->pkt_dts, pict_type);
                } else {
                    mexPrintf("%-8s %-10s %-6s | %-10lld %-6c\n", "", "", "", (long long)frame->pkt_dts, pict_type);
                }
                frame_received = 1;
            }

            if (!frame_received) {
                mexPrintf(" | (EAGAIN)\n");
            }

            /* Stop after we've processed packets past target_dts */
            if (pkt_dts > target_dts + 1000) {
                done = 1;
            }
        }
        av_packet_unref(pkt);
    }

    /* Flush decoder to get remaining frames */
    mexPrintf("\nFlushing decoder:\n");
    avcodec_send_packet(codec_ctx, NULL);
    int ret;
    while ((ret = avcodec_receive_frame(codec_ctx, frame)) >= 0) {
        char pict_type = (frame->pict_type == AV_PICTURE_TYPE_I) ? 'I' :
                         (frame->pict_type == AV_PICTURE_TYPE_P) ? 'P' :
                         (frame->pict_type == AV_PICTURE_TYPE_B) ? 'B' : '?';
        mexPrintf("         %-10s %-6s | %-10lld %-6c\n", "", "", (long long)frame->pkt_dts, pict_type);
    }

    av_packet_free(&pkt);
    av_frame_free(&frame);
    avcodec_free_context(&codec_ctx);
    avformat_close_input(&fmt_ctx);
    mxFree(filename);
}
