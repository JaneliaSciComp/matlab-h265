/*
 * read_h265_frames.c
 * MEX function to read a contiguous range of frames efficiently.
 * Seeks once to the start, then decodes sequentially through the range.
 *
 * Usage: frames = read_h265_frames(video_info, start_frame, end_frame)
 *   video_info  - struct returned by open_ffmpeg_video
 *   start_frame - 1-based starting frame index
 *   end_frame   - 1-based ending frame index (inclusive)
 *   frames      - grayscale: uint8 3D array (height x width x num_frames)
 *                 RGB: uint8 4D array (height x width x 3 x num_frames)
 *
 * Compile with:
 *   mex read_h265_frames.c -lavformat -lavcodec -lavutil -lswscale
 */

#include "mex.h"
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <stdint.h>
#include <string.h>

void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
    int start_frame, end_frame, num_frames_to_read;
    int64_t start_dts, end_dts;
    int64_t pts_increment;

    AVFormatContext *fmt_ctx = NULL;
    AVCodecContext *codec_ctx = NULL;
    AVFrame *frame = NULL;
    AVFrame *out_frame = NULL;
    AVPacket *pkt = NULL;
    struct SwsContext *sws_ctx = NULL;

    int video_stream_idx = -1;
    int ret;
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
            "First argument must be video_info struct from open_ffmpeg_video");
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

    /* Extract pointers from struct */
    fmt_ctx = (AVFormatContext *)(uintptr_t)(*(uint64_t *)mxGetData(fmt_ctx_field));
    codec_ctx = (AVCodecContext *)(uintptr_t)(*(uint64_t *)mxGetData(codec_ctx_field));
    video_stream_idx = (int)mxGetScalar(stream_idx_field);

    /* Validate pointers */
    if (!fmt_ctx || !codec_ctx) {
        mexErrMsgIdAndTxt("read_h265_frames:nullPtr",
            "Invalid video_info: null pointers. Was close_ffmpeg_video already called?");
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
    start_dts = dts_array[start_frame];
    end_dts = dts_array[end_frame];

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

    /* Create output array */
    uint8_t *out_data;
    size_t frame_size;
    if (is_grayscale) {
        /* Grayscale: height x width x num_frames */
        mwSize dims[3] = {height, width, num_frames_to_read};
        plhs[0] = mxCreateNumericArray(3, dims, mxUINT8_CLASS, mxREAL);
        frame_size = (size_t)height * width;
    } else {
        /* RGB: height x width x 3 x num_frames */
        mwSize dims[4] = {height, width, 3, num_frames_to_read};
        plhs[0] = mxCreateNumericArray(4, dims, mxUINT8_CLASS, mxREAL);
        frame_size = (size_t)height * width * 3;
    }
    out_data = (uint8_t *)mxGetData(plhs[0]);

    /* Track which frames we've captured */
    int *frame_captured = (int *)mxCalloc(num_frames_to_read, sizeof(int));
    int frames_captured = 0;

    /* Allocate frames and packet */
    frame = av_frame_alloc();
    out_frame = av_frame_alloc();
    pkt = av_packet_alloc();

    if (!frame || !out_frame || !pkt) {
        mxFree(frame_captured);
        av_frame_free(&frame);
        av_frame_free(&out_frame);
        av_packet_free(&pkt);
        mexErrMsgIdAndTxt("read_h265_frames:allocFrame", "Could not allocate frame/packet");
    }

    /* Setup output frame (reused for all conversions) */
    enum AVPixelFormat out_pix_fmt = is_grayscale ? AV_PIX_FMT_GRAY8 : AV_PIX_FMT_RGB24;
    out_frame->format = out_pix_fmt;
    out_frame->width = width;
    out_frame->height = height;
    if (av_frame_get_buffer(out_frame, 0) < 0) {
        mxFree(frame_captured);
        av_frame_free(&frame);
        av_frame_free(&out_frame);
        av_packet_free(&pkt);
        mexErrMsgIdAndTxt("read_h265_frames:allocBuffer", "Could not allocate output frame buffer");
    }

    /* Create scaler context */
    sws_ctx = sws_getContext(width, height, codec_ctx->pix_fmt,
                             width, height, out_pix_fmt,
                             SWS_BILINEAR, NULL, NULL, NULL);
    if (!sws_ctx) {
        mxFree(frame_captured);
        av_frame_free(&frame);
        av_frame_free(&out_frame);
        av_packet_free(&pkt);
        mexErrMsgIdAndTxt("read_h265_frames:sws", "Could not create scaler context");
    }

    /* Seek to start position */
    ret = av_seek_frame(fmt_ctx, video_stream_idx, start_dts, AVSEEK_FLAG_BACKWARD);
    if (ret < 0) {
        avformat_seek_file(fmt_ctx, video_stream_idx, INT64_MIN, 0, 0, 0);
    }
    avcodec_flush_buffers(codec_ctx);

    /* Decode frames until we have all requested frames */
    while (frames_captured < num_frames_to_read && av_read_frame(fmt_ctx, pkt) >= 0) {
        if (pkt->stream_index == video_stream_idx) {
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
                    sws_freeContext(sws_ctx);
                    mxFree(frame_captured);
                    av_packet_free(&pkt);
                    av_frame_free(&frame);
                    av_frame_free(&out_frame);
                    mexErrMsgIdAndTxt("read_h265_frames:decode", "Error during decoding");
                }

                /* Check if this frame is in our target range using PTS */
                int64_t frame_pts = frame->pts;

                /* Find which frame index this PTS corresponds to */
                for (int i = 0; i < num_frames_to_read; i++) {
                    int64_t target_pts = (int64_t)(start_frame + i) * pts_increment;
                    if (!frame_captured[i] && frame_pts == target_pts) {
                        /* Convert to output format */
                        sws_scale(sws_ctx, (const uint8_t * const*)frame->data, frame->linesize,
                                  0, height, out_frame->data, out_frame->linesize);

                        /* Copy to output array (MATLAB is column-major) */
                        uint8_t *frame_out = out_data + i * frame_size;
                        if (is_grayscale) {
                            for (int y = 0; y < height; y++) {
                                for (int x = 0; x < width; x++) {
                                    frame_out[x * height + y] = out_frame->data[0][y * out_frame->linesize[0] + x];
                                }
                            }
                        } else {
                            /* RGB: copy from row-major interleaved to column-major planar */
                            size_t plane_size = (size_t)height * width;
                            for (int y = 0; y < height; y++) {
                                for (int x = 0; x < width; x++) {
                                    int rgb_idx = y * out_frame->linesize[0] + x * 3;
                                    int matlab_idx = x * height + y;
                                    frame_out[matlab_idx] = out_frame->data[0][rgb_idx];                  /* R */
                                    frame_out[matlab_idx + plane_size] = out_frame->data[0][rgb_idx + 1]; /* G */
                                    frame_out[matlab_idx + 2 * plane_size] = out_frame->data[0][rgb_idx + 2]; /* B */
                                }
                            }
                        }

                        frame_captured[i] = 1;
                        frames_captured++;
                        break;
                    }
                }

                /* Stop if we have all frames */
                if (frames_captured == num_frames_to_read) {
                    break;
                }
            }
        }
        av_packet_unref(pkt);
    }

    /* Flush decoder to get remaining buffered frames */
    if (frames_captured < num_frames_to_read) {
        avcodec_send_packet(codec_ctx, NULL);
        while (frames_captured < num_frames_to_read) {
            ret = avcodec_receive_frame(codec_ctx, frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            } else if (ret < 0) {
                break;
            }

            int64_t frame_pts = frame->pts;
            for (int i = 0; i < num_frames_to_read; i++) {
                int64_t target_pts = (int64_t)(start_frame + i) * pts_increment;
                if (!frame_captured[i] && frame_pts == target_pts) {
                    sws_scale(sws_ctx, (const uint8_t * const*)frame->data, frame->linesize,
                              0, height, out_frame->data, out_frame->linesize);

                    uint8_t *frame_out = out_data + i * frame_size;
                    if (is_grayscale) {
                        for (int y = 0; y < height; y++) {
                            for (int x = 0; x < width; x++) {
                                frame_out[x * height + y] = out_frame->data[0][y * out_frame->linesize[0] + x];
                            }
                        }
                    } else {
                        /* RGB: copy from row-major interleaved to column-major planar */
                        size_t plane_size = (size_t)height * width;
                        for (int y = 0; y < height; y++) {
                            for (int x = 0; x < width; x++) {
                                int rgb_idx = y * out_frame->linesize[0] + x * 3;
                                int matlab_idx = x * height + y;
                                frame_out[matlab_idx] = out_frame->data[0][rgb_idx];                  /* R */
                                frame_out[matlab_idx + plane_size] = out_frame->data[0][rgb_idx + 1]; /* G */
                                frame_out[matlab_idx + 2 * plane_size] = out_frame->data[0][rgb_idx + 2]; /* B */
                            }
                        }
                    }

                    frame_captured[i] = 1;
                    frames_captured++;
                    break;
                }
            }
        }
    }

    /* Cleanup */
    sws_freeContext(sws_ctx);
    av_packet_free(&pkt);
    av_frame_free(&frame);
    av_frame_free(&out_frame);

    /* Check if we got all frames */
    if (frames_captured < num_frames_to_read) {
        int missing = num_frames_to_read - frames_captured;
        mxFree(frame_captured);
        mexErrMsgIdAndTxt("read_h265_frames:notFound",
            "Only captured %d of %d frames (%d missing)",
            frames_captured, num_frames_to_read, missing);
    }

    mxFree(frame_captured);
}
