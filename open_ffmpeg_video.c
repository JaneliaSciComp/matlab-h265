/*
 * open_ffmpeg_video.c
 * MEX function to open a video file, build a DTS lookup table, and keep
 * the decoder open for fast subsequent frame reads.
 *
 * Usage: video_info = open_ffmpeg_video(filename)
 *
 * Returns a struct with fields:
 *   filename   - the video file path
 *   num_frames - total number of frames
 *   width      - frame width
 *   height     - frame height
 *   dts        - array of DTS values indexed by frame number (1 x num_frames)
 *   fmt_ctx_ptr    - pointer to AVFormatContext (for read_ffmpeg_frame)
 *   codec_ctx_ptr  - pointer to AVCodecContext (for read_ffmpeg_frame)
 *   video_stream_idx - video stream index
 *
 * IMPORTANT: Call close_ffmpeg_video(video_info) when done to free resources.
 *
 * Compile with:
 *   mex open_ffmpeg_video.c -lavformat -lavcodec -lavutil
 */

#include "mex.h"
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <stdlib.h>
#include <stdint.h>

/* HEVC NAL unit types that indicate open GOP */
#define HEVC_NAL_BLA_W_LP    16
#define HEVC_NAL_BLA_W_RADL  17
#define HEVC_NAL_BLA_N_LP    18
#define HEVC_NAL_IDR_W_RADL  19
#define HEVC_NAL_IDR_N_LP    20
#define HEVC_NAL_CRA_NUT     21
#define HEVC_NAL_RASL_N      8
#define HEVC_NAL_RASL_R      9

/*
 * Check HEVC packet for open GOP NAL unit types.
 * Returns the problematic NAL unit type if found, or -1 if OK.
 * For HVCC format (MP4), packets have 4-byte length prefixes before each NAL unit.
 */
static int check_hevc_packet_for_open_gop(const uint8_t *data, int size, int length_size)
{
    int pos = 0;

    while (pos + length_size < size) {
        /* Read NAL unit length (big-endian) */
        int nal_size = 0;
        for (int i = 0; i < length_size; i++) {
            nal_size = (nal_size << 8) | data[pos + i];
        }
        pos += length_size;

        if (nal_size <= 0 || pos + nal_size > size) {
            break;
        }

        /* Extract NAL unit type from first byte: bits 1-6 */
        int nal_unit_type = (data[pos] >> 1) & 0x3F;

        /* Check for open GOP indicators */
        if (nal_unit_type == HEVC_NAL_CRA_NUT ||
            nal_unit_type == HEVC_NAL_BLA_W_LP ||
            nal_unit_type == HEVC_NAL_BLA_W_RADL ||
            nal_unit_type == HEVC_NAL_BLA_N_LP ||
            nal_unit_type == HEVC_NAL_RASL_N ||
            nal_unit_type == HEVC_NAL_RASL_R) {
            return nal_unit_type;
        }

        pos += nal_size;
    }

    return -1;  /* No open GOP indicators found */
}

void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
    char *filename;
    AVFormatContext *fmt_ctx = NULL;
    AVCodecContext *codec_ctx = NULL;
    const AVCodec *codec = NULL;
    AVStream *video_stream = NULL;
    AVPacket *pkt = NULL;
    int video_stream_idx = -1;

    /* Check arguments */
    if (nrhs != 1) {
        mexErrMsgIdAndTxt("open_ffmpeg_video:nrhs", "One input required: filename");
    }
    if (!mxIsChar(prhs[0])) {
        mexErrMsgIdAndTxt("open_ffmpeg_video:notString", "Filename must be a string");
    }

    filename = mxArrayToString(prhs[0]);

    /* Open input file */
    if (avformat_open_input(&fmt_ctx, filename, NULL, NULL) < 0) {
        mxFree(filename);
        mexErrMsgIdAndTxt("open_ffmpeg_video:openFailed", "Could not open input file");
    }

    /* Find stream info */
    if (avformat_find_stream_info(fmt_ctx, NULL) < 0) {
        avformat_close_input(&fmt_ctx);
        mxFree(filename);
        mexErrMsgIdAndTxt("open_ffmpeg_video:streamInfo", "Could not find stream info");
    }

    /* Find video stream */
    for (int i = 0; i < fmt_ctx->nb_streams; i++) {
        if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_idx = i;
            video_stream = fmt_ctx->streams[i];
            break;
        }
    }

    if (video_stream_idx == -1) {
        avformat_close_input(&fmt_ctx);
        mxFree(filename);
        mexErrMsgIdAndTxt("open_ffmpeg_video:noVideo", "No video stream found");
    }

    /* Get dimensions */
    int width = video_stream->codecpar->width;
    int height = video_stream->codecpar->height;

    /* Get frame rate to compute pts_increment */
    AVRational frame_rate = av_guess_frame_rate(fmt_ctx, video_stream, NULL);
    if (frame_rate.num == 0 || frame_rate.den == 0) {
        avformat_close_input(&fmt_ctx);
        mxFree(filename);
        mexErrMsgIdAndTxt("open_ffmpeg_video:noFrameRate", "Could not determine frame rate");
    }

    /* pts_increment = time_base / frame_rate (in time_base units per frame)
     * = (time_base.den * frame_rate.den) / (time_base.num * frame_rate.num)
     * For exact integer result, the division must have no remainder. */
    AVRational time_base = video_stream->time_base;
    int64_t numerator = (int64_t)time_base.den * frame_rate.den;
    int64_t denominator = (int64_t)time_base.num * frame_rate.num;

    if (numerator % denominator != 0) {
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);
        mxFree(filename);
        mexErrMsgIdAndTxt("open_ffmpeg_video:badFrameRate",
            "Frame rate (%d/%d) and time base (%d/%d) are incompatible. "
            "PTS increment would be non-integer: %lld/%lld. "
            "Re-encode with a compatible frame rate.",
            frame_rate.num, frame_rate.den, time_base.num, time_base.den,
            (long long)numerator, (long long)denominator);
    }

    int64_t pts_increment = numerator / denominator;

    /* Find decoder */
    codec = avcodec_find_decoder(video_stream->codecpar->codec_id);
    if (!codec) {
        avformat_close_input(&fmt_ctx);
        mxFree(filename);
        mexErrMsgIdAndTxt("open_ffmpeg_video:noCodec", "Could not find decoder");
    }

    /* Allocate codec context */
    codec_ctx = avcodec_alloc_context3(codec);
    if (!codec_ctx) {
        avformat_close_input(&fmt_ctx);
        mxFree(filename);
        mexErrMsgIdAndTxt("open_ffmpeg_video:allocCodec", "Could not allocate codec context");
    }

    /* Copy codec parameters */
    if (avcodec_parameters_to_context(codec_ctx, video_stream->codecpar) < 0) {
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);
        mxFree(filename);
        mexErrMsgIdAndTxt("open_ffmpeg_video:codecParams", "Could not copy codec parameters");
    }

    /* Open codec */
    if (avcodec_open2(codec_ctx, codec, NULL) < 0) {
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);
        mxFree(filename);
        mexErrMsgIdAndTxt("open_ffmpeg_video:openCodec", "Could not open codec");
    }

    /* Allocate packet */
    pkt = av_packet_alloc();
    if (!pkt) {
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);
        mxFree(filename);
        mexErrMsgIdAndTxt("open_ffmpeg_video:allocPkt", "Could not allocate packet");
    }

    /* Check if this is HEVC and get NAL length size for open GOP detection */
    int is_hevc = (video_stream->codecpar->codec_id == AV_CODEC_ID_HEVC);
    int nal_length_size = 4;  /* Default for HVCC format */

    if (is_hevc && video_stream->codecpar->extradata_size >= 22) {
        /* HVCC format: byte 21 contains (lengthSizeMinusOne & 3) in bits 0-1 */
        nal_length_size = (video_stream->codecpar->extradata[21] & 0x03) + 1;
    }

    /* First pass: count frames and check for open GOP (HEVC only) */
    int num_frames = 0;
    while (av_read_frame(fmt_ctx, pkt) >= 0) {
        if (pkt->stream_index == video_stream_idx) {
            /* For HEVC, check for open GOP NAL unit types */
            if (is_hevc && pkt->data && pkt->size > 0) {
                int bad_nal = check_hevc_packet_for_open_gop(pkt->data, pkt->size, nal_length_size);
                if (bad_nal >= 0) {
                    const char *nal_name;
                    switch (bad_nal) {
                        case HEVC_NAL_CRA_NUT:   nal_name = "CRA (Clean Random Access)"; break;
                        case HEVC_NAL_BLA_W_LP:  nal_name = "BLA_W_LP (Broken Link Access)"; break;
                        case HEVC_NAL_BLA_W_RADL: nal_name = "BLA_W_RADL (Broken Link Access)"; break;
                        case HEVC_NAL_BLA_N_LP:  nal_name = "BLA_N_LP (Broken Link Access)"; break;
                        case HEVC_NAL_RASL_N:    nal_name = "RASL_N (Random Access Skipped Leading)"; break;
                        case HEVC_NAL_RASL_R:    nal_name = "RASL_R (Random Access Skipped Leading)"; break;
                        default:                 nal_name = "unknown"; break;
                    }
                    av_packet_unref(pkt);
                    av_packet_free(&pkt);
                    avcodec_free_context(&codec_ctx);
                    avformat_close_input(&fmt_ctx);
                    mxFree(filename);
                    mexErrMsgIdAndTxt("open_ffmpeg_video:openGOP",
                        "Video uses open GOP encoding (found NAL unit type %d: %s). "
                        "Open GOP videos have frames that cannot be decoded after seeking. "
                        "Please re-encode with closed GOP (e.g., -x265-params no-open-gop=1) "
                        "or without B-frames (e.g., -x265-params bframes=0).",
                        bad_nal, nal_name);
                }
            }
            num_frames++;
        }
        av_packet_unref(pkt);
    }

    if (num_frames == 0) {
        av_packet_free(&pkt);
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);
        mxFree(filename);
        mexErrMsgIdAndTxt("open_ffmpeg_video:noFrames", "No frames found in video");
    }

    /* Allocate DTS array and frame count array */
    int64_t *dts_array = (int64_t *)mxMalloc(num_frames * sizeof(int64_t));
    int *frame_count = (int *)mxCalloc(num_frames, sizeof(int));  /* initialized to 0 */
    if (!dts_array || !frame_count) {
        if (dts_array) mxFree(dts_array);
        if (frame_count) mxFree(frame_count);
        av_packet_free(&pkt);
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);
        mxFree(filename);
        mexErrMsgIdAndTxt("open_ffmpeg_video:allocArrays", "Could not allocate arrays");
    }

    /* Seek back to beginning */
    avformat_seek_file(fmt_ctx, video_stream_idx, INT64_MIN, 0, 0, 0);

    /* Second pass: build DTS lookup indexed by frame number (derived from PTS) */
    while (av_read_frame(fmt_ctx, pkt) >= 0) {
        if (pkt->stream_index == video_stream_idx) {
            /* Compute frame number from PTS */
            int64_t pts = pkt->pts;

            /* Verify PTS is aligned to pts_increment */
            if (pts % pts_increment != 0) {
                av_packet_unref(pkt);
                av_packet_free(&pkt);
                mxFree(frame_count);
                mxFree(dts_array);
                avcodec_free_context(&codec_ctx);
                avformat_close_input(&fmt_ctx);
                mxFree(filename);
                mexErrMsgIdAndTxt("open_ffmpeg_video:misalignedPTS",
                    "PTS %lld is not a multiple of pts_increment %lld. "
                    "Frame timing is inconsistent.",
                    (long long)pts, (long long)pts_increment);
            }

            int frame_num = (int)(pts / pts_increment);

            if (frame_num >= 0 && frame_num < num_frames) {
                dts_array[frame_num] = pkt->dts;
                frame_count[frame_num]++;
            }
        }
        av_packet_unref(pkt);
    }

    av_packet_free(&pkt);

    /* Verify each frame has exactly one packet */
    int pts_missing_count = 0;
    int pts_duplicate_count = 0;
    for (int i = 0; i < num_frames; i++) {
        if (frame_count[i] == 0) {
            pts_missing_count++;
        } else if (frame_count[i] > 1) {
            pts_duplicate_count++;
        }
    }
    mxFree(frame_count);

    if (pts_missing_count > 0) {
        mxFree(dts_array);
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);
        mxFree(filename);
        mexErrMsgIdAndTxt("open_ffmpeg_video:missingPTS",
            "%d of %d frames have no PTS mapping", pts_missing_count, num_frames);
    }
    if (pts_duplicate_count > 0) {
        mxFree(dts_array);
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);
        mxFree(filename);
        mexErrMsgIdAndTxt("open_ffmpeg_video:duplicatePTS",
            "%d frames have duplicate PTS mappings", pts_duplicate_count);
    }

    /* Seek back to beginning for subsequent reads */
    avformat_seek_file(fmt_ctx, video_stream_idx, INT64_MIN, 0, 0, 0);
    avcodec_flush_buffers(codec_ctx);

    /* Create output struct - keep fmt_ctx and codec_ctx open */
    const char *field_names[] = {"filename", "num_frames", "width", "height", "dts",
                                  "fmt_ctx_ptr", "codec_ctx_ptr", "video_stream_idx"};
    plhs[0] = mxCreateStructMatrix(1, 1, 8, field_names);

    /* Set filename */
    mxSetField(plhs[0], 0, "filename", mxCreateString(filename));

    /* Set num_frames */
    mxSetField(plhs[0], 0, "num_frames", mxCreateDoubleScalar((double)num_frames));

    /* Set width and height */
    mxSetField(plhs[0], 0, "width", mxCreateDoubleScalar((double)width));
    mxSetField(plhs[0], 0, "height", mxCreateDoubleScalar((double)height));

    /* Set dts array */
    mxArray *dts_mx = mxCreateDoubleMatrix(1, num_frames, mxREAL);
    double *dts_data = mxGetPr(dts_mx);
    for (int i = 0; i < num_frames; i++) {
        dts_data[i] = (double)dts_array[i];
    }
    mxSetField(plhs[0], 0, "dts", dts_mx);

    /* Store pointers as uint64 for use by read_ffmpeg_frame */
    mxSetField(plhs[0], 0, "fmt_ctx_ptr", mxCreateDoubleScalar((double)(uintptr_t)fmt_ctx));
    mxSetField(plhs[0], 0, "codec_ctx_ptr", mxCreateDoubleScalar((double)(uintptr_t)codec_ctx));
    mxSetField(plhs[0], 0, "video_stream_idx", mxCreateDoubleScalar((double)video_stream_idx));

    /* Free temporary arrays (but NOT fmt_ctx or codec_ctx - they stay open) */
    mxFree(dts_array);
    mxFree(filename);
}
