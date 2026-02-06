/*
 * read_h265_frame.c
 * MEX function to read a frame at index i from a video file using FFmpeg.
 * Uses the persistent decoder context from open_ffmpeg_video for fast access.
 *
 * Usage: frame = read_h265_frame(video_info, frame_index)
 *   video_info  - struct returned by open_ffmpeg_video
 *   frame_index - 1-based frame index
 *   frame       - grayscale (height x width) or RGB (height x width x 3) uint8
 *                 Output format depends on source video pixel format.
 *
 * Compile with:
 *   mex read_h265_frame.c -lavformat -lavcodec -lavutil -lswscale
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
    AVFrame *out_frame = NULL;
    AVPacket *pkt = NULL;
    struct SwsContext *sws_ctx = NULL;
    int is_grayscale;

    int video_stream_idx = -1;
    int ret;

    /* Check arguments */
    if (nrhs != 2) {
        mexErrMsgIdAndTxt("read_h265_frame:nrhs", "Two inputs required: video_info and frame_index");
    }
    if (nlhs > 1) {
        mexErrMsgIdAndTxt("read_h265_frame:nlhs", "One output allowed");
    }
    if (!mxIsStruct(prhs[0])) {
        mexErrMsgIdAndTxt("read_h265_frame:notStruct", "First argument must be video_info struct from open_ffmpeg_video");
    }
    if (!mxIsDouble(prhs[1]) || mxGetNumberOfElements(prhs[1]) != 1) {
        mexErrMsgIdAndTxt("read_h265_frame:notScalar", "Frame index must be a scalar");
    }

    /* Extract fields from video_info struct */
    mxArray *dts_field = mxGetField(prhs[0], 0, "dts");
    mxArray *num_frames_field = mxGetField(prhs[0], 0, "num_frames");
    mxArray *fmt_ctx_field = mxGetField(prhs[0], 0, "fmt_ctx_ptr");
    mxArray *codec_ctx_field = mxGetField(prhs[0], 0, "codec_ctx_ptr");
    mxArray *stream_idx_field = mxGetField(prhs[0], 0, "video_stream_idx");
    mxArray *pts_inc_field = mxGetField(prhs[0], 0, "pts_increment");

    if (!dts_field || !num_frames_field || !fmt_ctx_field || !codec_ctx_field || !stream_idx_field || !pts_inc_field) {
        mexErrMsgIdAndTxt("read_h265_frame:badStruct",
            "video_info must have dts, num_frames, fmt_ctx_ptr, codec_ctx_ptr, video_stream_idx, and pts_increment fields");
    }

    int64_t *dts_array = (int64_t *)mxGetData(dts_field);
    int num_frames = (int)mxGetScalar(num_frames_field);
    pts_increment = *(int64_t *)mxGetData(pts_inc_field);

    /* Extract pointers from struct */
    fmt_ctx = (AVFormatContext *)(uintptr_t)(*(uint64_t *)mxGetData(fmt_ctx_field));
    codec_ctx = (AVCodecContext *)(uintptr_t)(*(uint64_t *)mxGetData(codec_ctx_field));
    video_stream_idx = (int)mxGetScalar(stream_idx_field);

    /* Validate pointers */
    if (!fmt_ctx || !codec_ctx) {
        mexErrMsgIdAndTxt("read_h265_frame:nullPtr",
            "Invalid video_info: null pointers. Was close_ffmpeg_video already called?");
    }

    /* Get target frame (convert to 0-based) */
    target_frame = (int)mxGetScalar(prhs[1]) - 1;

    if (target_frame < 0 || target_frame >= num_frames) {
        mexErrMsgIdAndTxt("read_h265_frame:invalidIndex", "Frame index must be between 1 and %d", num_frames);
    }

    /* Look up the DTS for seeking, and compute target PTS for matching */
    target_dts = dts_array[target_frame];
    target_pts = (int64_t)target_frame * pts_increment;

    /* Check for is_gray override field, otherwise detect from pixel format */
    mxArray *is_gray_field = mxGetField(prhs[0], 0, "is_gray");
    if (is_gray_field && mxIsLogical(is_gray_field)) {
        is_grayscale = mxIsLogicalScalarTrue(is_gray_field);
    } else if (is_gray_field && mxIsDouble(is_gray_field)) {
        is_grayscale = (int)mxGetScalar(is_gray_field) != 0;
    } else {
        /* Fall back to auto-detection from pixel format */
        is_grayscale = (codec_ctx->pix_fmt == AV_PIX_FMT_GRAY8 ||
                        codec_ctx->pix_fmt == AV_PIX_FMT_GRAY16BE ||
                        codec_ctx->pix_fmt == AV_PIX_FMT_GRAY16LE);
    }

    /* Allocate frames and packet */
    frame = av_frame_alloc();
    out_frame = av_frame_alloc();
    pkt = av_packet_alloc();

    if (!frame || !out_frame || !pkt) {
        av_frame_free(&frame);
        av_frame_free(&out_frame);
        av_packet_free(&pkt);
        mexErrMsgIdAndTxt("read_h265_frame:allocFrame", "Could not allocate frame/packet");
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
                    av_frame_free(&out_frame);
                    mexErrMsgIdAndTxt("read_h265_frame:decode", "Error during decoding");
                }

                /* Check if this is the target frame by PTS */
                if (frame->pts == target_pts) {
                    /* Found the target frame - convert and return */
                    int width = codec_ctx->width;
                    int height = codec_ctx->height;
                    enum AVPixelFormat out_pix_fmt = is_grayscale ? AV_PIX_FMT_GRAY8 : AV_PIX_FMT_RGB24;

                    /* Setup output frame */
                    out_frame->format = out_pix_fmt;
                    out_frame->width = width;
                    out_frame->height = height;
                    av_frame_get_buffer(out_frame, 0);

                    /* Create scaler context */
                    sws_ctx = sws_getContext(width, height, codec_ctx->pix_fmt,
                                             width, height, out_pix_fmt,
                                             SWS_BILINEAR, NULL, NULL, NULL);

                    if (!sws_ctx) {
                        av_packet_free(&pkt);
                        av_frame_free(&frame);
                        av_frame_free(&out_frame);
                        mexErrMsgIdAndTxt("read_h265_frame:sws", "Could not create scaler context");
                    }

                    /* Convert to output format */
                    sws_scale(sws_ctx, (const uint8_t * const*)frame->data, frame->linesize,
                              0, height, out_frame->data, out_frame->linesize);

                    if (is_grayscale) {
                        /* Create MATLAB output array (height x width) */
                        plhs[0] = mxCreateNumericMatrix(height, width, mxUINT8_CLASS, mxREAL);
                        uint8_t *out_data = (uint8_t *)mxGetData(plhs[0]);

                        /* Copy data (MATLAB is column-major, so transpose) */
                        for (int y = 0; y < height; y++) {
                            for (int x = 0; x < width; x++) {
                                out_data[x * height + y] = out_frame->data[0][y * out_frame->linesize[0] + x];
                            }
                        }
                    } else {
                        /* Create MATLAB output array (height x width x 3) */
                        mwSize dims[3] = {height, width, 3};
                        plhs[0] = mxCreateNumericArray(3, dims, mxUINT8_CLASS, mxREAL);
                        uint8_t *out_data = (uint8_t *)mxGetData(plhs[0]);
                        size_t plane_size = (size_t)height * width;

                        /* Copy RGB data (convert from row-major interleaved to column-major planar) */
                        for (int y = 0; y < height; y++) {
                            for (int x = 0; x < width; x++) {
                                int rgb_idx = y * out_frame->linesize[0] + x * 3;
                                int matlab_idx = x * height + y;
                                out_data[matlab_idx] = out_frame->data[0][rgb_idx];                  /* R */
                                out_data[matlab_idx + plane_size] = out_frame->data[0][rgb_idx + 1]; /* G */
                                out_data[matlab_idx + 2 * plane_size] = out_frame->data[0][rgb_idx + 2]; /* B */
                            }
                        }
                    }

                    /* Cleanup and return */
                    sws_freeContext(sws_ctx);
                    av_packet_free(&pkt);
                    av_frame_free(&frame);
                    av_frame_free(&out_frame);
                    return;
                }
            }
        }
        av_packet_unref(pkt);
    }

    /* Flush decoder to get remaining buffered frames (needed for B-frames) */
    avcodec_send_packet(codec_ctx, NULL);
    while (1) {
        ret = avcodec_receive_frame(codec_ctx, frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        } else if (ret < 0) {
            break;
        }

        /* Check if this is the target frame by PTS */
        if (frame->pts == target_pts) {
            /* Found the target frame - convert and return */
            int width = codec_ctx->width;
            int height = codec_ctx->height;
            enum AVPixelFormat out_pix_fmt = is_grayscale ? AV_PIX_FMT_GRAY8 : AV_PIX_FMT_RGB24;

            /* Setup output frame */
            out_frame->format = out_pix_fmt;
            out_frame->width = width;
            out_frame->height = height;
            av_frame_get_buffer(out_frame, 0);

            /* Create scaler context */
            sws_ctx = sws_getContext(width, height, codec_ctx->pix_fmt,
                                     width, height, out_pix_fmt,
                                     SWS_BILINEAR, NULL, NULL, NULL);

            if (!sws_ctx) {
                av_packet_free(&pkt);
                av_frame_free(&frame);
                av_frame_free(&out_frame);
                mexErrMsgIdAndTxt("read_h265_frame:sws", "Could not create scaler context");
            }

            /* Convert to output format */
            sws_scale(sws_ctx, (const uint8_t * const*)frame->data, frame->linesize,
                      0, height, out_frame->data, out_frame->linesize);

            if (is_grayscale) {
                /* Create MATLAB output array (height x width) */
                plhs[0] = mxCreateNumericMatrix(height, width, mxUINT8_CLASS, mxREAL);
                uint8_t *out_data = (uint8_t *)mxGetData(plhs[0]);

                /* Copy data (MATLAB is column-major, so transpose) */
                for (int y = 0; y < height; y++) {
                    for (int x = 0; x < width; x++) {
                        out_data[x * height + y] = out_frame->data[0][y * out_frame->linesize[0] + x];
                    }
                }
            } else {
                /* Create MATLAB output array (height x width x 3) */
                mwSize dims[3] = {height, width, 3};
                plhs[0] = mxCreateNumericArray(3, dims, mxUINT8_CLASS, mxREAL);
                uint8_t *out_data = (uint8_t *)mxGetData(plhs[0]);
                size_t plane_size = (size_t)height * width;

                /* Copy RGB data (convert from row-major interleaved to column-major planar) */
                for (int y = 0; y < height; y++) {
                    for (int x = 0; x < width; x++) {
                        int rgb_idx = y * out_frame->linesize[0] + x * 3;
                        int matlab_idx = x * height + y;
                        out_data[matlab_idx] = out_frame->data[0][rgb_idx];                  /* R */
                        out_data[matlab_idx + plane_size] = out_frame->data[0][rgb_idx + 1]; /* G */
                        out_data[matlab_idx + 2 * plane_size] = out_frame->data[0][rgb_idx + 2]; /* B */
                    }
                }
            }

            /* Cleanup and return */
            sws_freeContext(sws_ctx);
            av_packet_free(&pkt);
            av_frame_free(&frame);
            av_frame_free(&out_frame);
            return;
        }
    }

    /* Frame not found */
    av_packet_free(&pkt);
    av_frame_free(&frame);
    av_frame_free(&out_frame);
    mexErrMsgIdAndTxt("read_h265_frame:notFound",
                      "Frame %d not found. target_pts=%lld, read %d packets with DTS range [%lld, %lld]",
                      target_frame + 1,
                      (long long)target_pts,
                      packets_read,
                      (long long)first_dts_seen,
                      (long long)last_dts_seen);
}
