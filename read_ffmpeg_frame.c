/*
 * read_ffmpeg_frame.c
 * MEX function to read a frame at index i from a video file using FFmpeg.
 * Uses the persistent decoder context from open_ffmpeg_video for fast access.
 *
 * Usage: frame = read_ffmpeg_frame(video_info, frame_index)
 *   video_info  - struct returned by open_ffmpeg_video
 *   frame_index - 1-based frame index
 *   frame       - grayscale image as uint8 matrix (height x width)
 *
 * Compile with:
 *   mex read_ffmpeg_frame.c -lavformat -lavcodec -lavutil -lswscale
 */

#include "mex.h"
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <stdint.h>

void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
    int target_frame;
    int64_t target_dts;
    int64_t target_pts;
    int64_t pts_increment;

    AVFormatContext *fmt_ctx = NULL;
    AVCodecContext *codec_ctx = NULL;
    AVFrame *frame = NULL;
    AVFrame *gray_frame = NULL;
    AVPacket *pkt = NULL;
    struct SwsContext *sws_ctx = NULL;

    int video_stream_idx = -1;
    int ret;

    /* Check arguments */
    if (nrhs != 2) {
        mexErrMsgIdAndTxt("read_ffmpeg_frame:nrhs", "Two inputs required: video_info and frame_index");
    }
    if (nlhs > 1) {
        mexErrMsgIdAndTxt("read_ffmpeg_frame:nlhs", "One output allowed");
    }
    if (!mxIsStruct(prhs[0])) {
        mexErrMsgIdAndTxt("read_ffmpeg_frame:notStruct", "First argument must be video_info struct from open_ffmpeg_video");
    }
    if (!mxIsDouble(prhs[1]) || mxGetNumberOfElements(prhs[1]) != 1) {
        mexErrMsgIdAndTxt("read_ffmpeg_frame:notScalar", "Frame index must be a scalar");
    }

    /* Extract fields from video_info struct */
    mxArray *dts_field = mxGetField(prhs[0], 0, "dts");
    mxArray *num_frames_field = mxGetField(prhs[0], 0, "num_frames");
    mxArray *fmt_ctx_field = mxGetField(prhs[0], 0, "fmt_ctx_ptr");
    mxArray *codec_ctx_field = mxGetField(prhs[0], 0, "codec_ctx_ptr");
    mxArray *stream_idx_field = mxGetField(prhs[0], 0, "video_stream_idx");
    mxArray *pts_inc_field = mxGetField(prhs[0], 0, "pts_increment");

    if (!dts_field || !num_frames_field || !fmt_ctx_field || !codec_ctx_field || !stream_idx_field || !pts_inc_field) {
        mexErrMsgIdAndTxt("read_ffmpeg_frame:badStruct",
            "video_info must have dts, num_frames, fmt_ctx_ptr, codec_ctx_ptr, video_stream_idx, and pts_increment fields");
    }

    double *dts_array = mxGetPr(dts_field);
    int num_frames = (int)mxGetScalar(num_frames_field);
    pts_increment = (int64_t)mxGetScalar(pts_inc_field);

    /* Extract pointers from struct */
    fmt_ctx = (AVFormatContext *)(uintptr_t)mxGetScalar(fmt_ctx_field);
    codec_ctx = (AVCodecContext *)(uintptr_t)mxGetScalar(codec_ctx_field);
    video_stream_idx = (int)mxGetScalar(stream_idx_field);

    /* Validate pointers */
    if (!fmt_ctx || !codec_ctx) {
        mexErrMsgIdAndTxt("read_ffmpeg_frame:nullPtr",
            "Invalid video_info: null pointers. Was close_ffmpeg_video already called?");
    }

    /* Get target frame (convert to 0-based) */
    target_frame = (int)mxGetScalar(prhs[1]) - 1;

    if (target_frame < 0 || target_frame >= num_frames) {
        mexErrMsgIdAndTxt("read_ffmpeg_frame:invalidIndex", "Frame index must be between 1 and %d", num_frames);
    }

    /* Look up the DTS for seeking, and compute target PTS for matching */
    target_dts = (int64_t)dts_array[target_frame];
    target_pts = (int64_t)target_frame * pts_increment;

    /* Allocate frames and packet */
    frame = av_frame_alloc();
    gray_frame = av_frame_alloc();
    pkt = av_packet_alloc();

    if (!frame || !gray_frame || !pkt) {
        av_frame_free(&frame);
        av_frame_free(&gray_frame);
        av_packet_free(&pkt);
        mexErrMsgIdAndTxt("read_ffmpeg_frame:allocFrame", "Could not allocate frame/packet");
    }

    /* Seek using the DTS from our lookup table */
    ret = av_seek_frame(fmt_ctx, video_stream_idx, target_dts, AVSEEK_FLAG_BACKWARD);
    if (ret < 0) {
        /* Seek failed, try from beginning */
        avformat_seek_file(fmt_ctx, video_stream_idx, INT64_MIN, 0, 0, 0);
    }
    avcodec_flush_buffers(codec_ctx);

    /* Track first and last DTS seen for error reporting */
    int64_t first_dts_seen = AV_NOPTS_VALUE;
    int64_t last_dts_seen = AV_NOPTS_VALUE;
    int packets_read = 0;

    /* Decode frames until we find the one with target DTS */
    while (av_read_frame(fmt_ctx, pkt) >= 0) {
        if (pkt->stream_index == video_stream_idx) {
            /* Track DTS for error reporting */
            if (first_dts_seen == AV_NOPTS_VALUE) {
                first_dts_seen = pkt->dts;
            }
            last_dts_seen = pkt->dts;
            packets_read++;

            ret = avcodec_send_packet(codec_ctx, pkt);
            if (ret < 0) {
                av_packet_unref(pkt);
                continue;
            }

            while (ret >= 0) {
                ret = avcodec_receive_frame(codec_ctx, frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    break;
                } else if (ret < 0) {
                    av_packet_free(&pkt);
                    av_frame_free(&frame);
                    av_frame_free(&gray_frame);
                    mexErrMsgIdAndTxt("read_ffmpeg_frame:decode", "Error during decoding");
                }

                /* Check if this is the target frame by PTS */
                if (frame->pts == target_pts) {
                    /* Found the target frame - convert to grayscale and return */
                    int width = codec_ctx->width;
                    int height = codec_ctx->height;

                    /* Setup grayscale frame */
                    gray_frame->format = AV_PIX_FMT_GRAY8;
                    gray_frame->width = width;
                    gray_frame->height = height;
                    av_frame_get_buffer(gray_frame, 0);

                    /* Create scaler context */
                    sws_ctx = sws_getContext(width, height, codec_ctx->pix_fmt,
                                             width, height, AV_PIX_FMT_GRAY8,
                                             SWS_BILINEAR, NULL, NULL, NULL);

                    if (!sws_ctx) {
                        av_packet_free(&pkt);
                        av_frame_free(&frame);
                        av_frame_free(&gray_frame);
                        mexErrMsgIdAndTxt("read_ffmpeg_frame:sws", "Could not create scaler context");
                    }

                    /* Convert to grayscale */
                    sws_scale(sws_ctx, (const uint8_t * const*)frame->data, frame->linesize,
                              0, height, gray_frame->data, gray_frame->linesize);

                    /* Create MATLAB output array (height x width) */
                    plhs[0] = mxCreateNumericMatrix(height, width, mxUINT8_CLASS, mxREAL);
                    uint8_t *out_data = (uint8_t *)mxGetData(plhs[0]);

                    /* Copy data (MATLAB is column-major, so transpose) */
                    for (int y = 0; y < height; y++) {
                        for (int x = 0; x < width; x++) {
                            out_data[x * height + y] = gray_frame->data[0][y * gray_frame->linesize[0] + x];
                        }
                    }

                    /* Cleanup and return */
                    sws_freeContext(sws_ctx);
                    av_packet_free(&pkt);
                    av_frame_free(&frame);
                    av_frame_free(&gray_frame);
                    return;
                }
            }
        }
        av_packet_unref(pkt);
    }

    /* Frame not found */
    av_packet_free(&pkt);
    av_frame_free(&frame);
    av_frame_free(&gray_frame);
    mexErrMsgIdAndTxt("read_ffmpeg_frame:notFound",
                      "Frame %d not found. target_pts=%lld, read %d packets with DTS range [%lld, %lld]",
                      target_frame + 1,
                      (long long)target_pts,
                      packets_read,
                      (long long)first_dts_seen,
                      (long long)last_dts_seen);
}
