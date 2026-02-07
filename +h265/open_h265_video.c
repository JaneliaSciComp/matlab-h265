/*
 * open_h265_video.c
 * MEX function to open a video file, build a DTS lookup table, and keep
 * the decoder open for fast subsequent frame reads.
 *
 * Usage: video_info = open_h265_video(filename)
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
 *   cache_ptr  - pointer to GOP frame cache (initially empty)
 *
 * IMPORTANT: Call close_h265_video(video_info) when done to free resources.
 *
 * Compile with:
 *   mex open_h265_video.c -lavformat -lavcodec -lavutil
 */

#include "mex.h"
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/log.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "h265_frame_cache.h"

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

/*
 * Allocate an empty frame cache. Frame data block will be allocated on first read.
 * Uses mxMalloc + mexMakeMemoryPersistent for proper MEX memory management.
 */
static H265FrameCache *alloc_frame_cache(void)
{
    H265FrameCache *cache = (H265FrameCache *)mxMalloc(sizeof(H265FrameCache));
    if (!cache) return NULL;
    mexMakeMemoryPersistent(cache);

    /* Allocate handles */
    cache->frame_data = (uint8_t **)mxMalloc(sizeof(uint8_t *));
    cache->frame_indices = (int **)mxMalloc(sizeof(int *));

    if (!cache->frame_data || !cache->frame_indices) {
        if (cache->frame_data) mxFree(cache->frame_data);
        if (cache->frame_indices) mxFree(cache->frame_indices);
        mxFree(cache);
        return NULL;
    }
    mexMakeMemoryPersistent(cache->frame_data);
    mexMakeMemoryPersistent(cache->frame_indices);

    *cache->frame_data = NULL;     /* Allocated on first read */
    *cache->frame_indices = NULL;  /* Allocated on first read */
    cache->num_frames = 0;
    cache->capacity = 0;
    cache->width = 0;
    cache->height = 0;
    cache->is_grayscale = 0;
    cache->frame_size = 0;

    return cache;
}

void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
    char *filename;

    /* Suppress FFmpeg info messages (only show warnings and errors) */
    av_log_set_level(AV_LOG_WARNING);

    AVFormatContext *fmt_ctx = NULL;
    AVCodecContext *codec_ctx = NULL;
    const AVCodec *codec = NULL;
    AVStream *video_stream = NULL;
    AVPacket *pkt = NULL;
    int video_stream_idx = -1;

    /* Check arguments */
    if (nrhs != 1) {
        mexErrMsgIdAndTxt("open_h265_video:nrhs", "One input required: filename");
    }
    if (!mxIsChar(prhs[0])) {
        mexErrMsgIdAndTxt("open_h265_video:notString", "Filename must be a string");
    }

    filename = mxArrayToString(prhs[0]);

    /* Open input file */
    if (avformat_open_input(&fmt_ctx, filename, NULL, NULL) < 0) {
        mxFree(filename);
        mexErrMsgIdAndTxt("open_h265_video:openFailed", "Could not open input file");
    }

    /* Find stream info */
    if (avformat_find_stream_info(fmt_ctx, NULL) < 0) {
        avformat_close_input(&fmt_ctx);
        mxFree(filename);
        mexErrMsgIdAndTxt("open_h265_video:streamInfo", "Could not find stream info");
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
        mexErrMsgIdAndTxt("open_h265_video:noVideo", "No video stream found");
    }

    /* Get dimensions */
    int width = video_stream->codecpar->width;
    int height = video_stream->codecpar->height;

    /* Get frame rate to compute pts_increment */
    AVRational frame_rate = av_guess_frame_rate(fmt_ctx, video_stream, NULL);
    if (frame_rate.num == 0 || frame_rate.den == 0) {
        avformat_close_input(&fmt_ctx);
        mxFree(filename);
        mexErrMsgIdAndTxt("open_h265_video:noFrameRate", "Could not determine frame rate");
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
        mexErrMsgIdAndTxt("open_h265_video:badFrameRate",
            "Frame rate (%d/%d) and time base (%d/%d) are incompatible. "
            "PTS increment would be non-integer: %lld/%lld. "
            "Re-encode with a compatible frame rate.",
            frame_rate.num, frame_rate.den, time_base.num, time_base.den,
            (long long)numerator, (long long)denominator);
    }

    int64_t pts_increment = numerator / denominator;

    /* Find software decoder (explicitly avoid hardware decoders) */
    codec = avcodec_find_decoder(video_stream->codecpar->codec_id);
    if (!codec) {
        avformat_close_input(&fmt_ctx);
        mxFree(filename);
        mexErrMsgIdAndTxt("open_h265_video:noCodec", "Could not find decoder");
    }

    /* Verify this is a software decoder, not hardware */
    if (codec->capabilities & AV_CODEC_CAP_HARDWARE) {
        avformat_close_input(&fmt_ctx);
        mxFree(filename);
        mexErrMsgIdAndTxt("open_h265_video:hwDecoder",
            "Got hardware decoder '%s', but software decoding is required", codec->name);
    }

    /* Allocate codec context */
    codec_ctx = avcodec_alloc_context3(codec);
    if (!codec_ctx) {
        avformat_close_input(&fmt_ctx);
        mxFree(filename);
        mexErrMsgIdAndTxt("open_h265_video:allocCodec", "Could not allocate codec context");
    }

    /* Copy codec parameters */
    if (avcodec_parameters_to_context(codec_ctx, video_stream->codecpar) < 0) {
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);
        mxFree(filename);
        mexErrMsgIdAndTxt("open_h265_video:codecParams", "Could not copy codec parameters");
    }

    /* Open codec */
    if (avcodec_open2(codec_ctx, codec, NULL) < 0) {
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);
        mxFree(filename);
        mexErrMsgIdAndTxt("open_h265_video:openCodec", "Could not open codec");
    }

    /* Allocate packet */
    pkt = av_packet_alloc();
    if (!pkt) {
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);
        mxFree(filename);
        mexErrMsgIdAndTxt("open_h265_video:allocPkt", "Could not allocate packet");
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
                    mexErrMsgIdAndTxt("open_h265_video:openGOP",
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
        mexErrMsgIdAndTxt("open_h265_video:noFrames", "No frames found in video");
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
        mexErrMsgIdAndTxt("open_h265_video:allocArrays", "Could not allocate arrays");
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
                mexErrMsgIdAndTxt("open_h265_video:misalignedPTS",
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
        mexErrMsgIdAndTxt("open_h265_video:missingPTS",
            "%d of %d frames have no PTS mapping", pts_missing_count, num_frames);
    }
    if (pts_duplicate_count > 0) {
        mxFree(dts_array);
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);
        mxFree(filename);
        mexErrMsgIdAndTxt("open_h265_video:duplicatePTS",
            "%d frames have duplicate PTS mappings", pts_duplicate_count);
    }

    /* Seek back to beginning for subsequent reads */
    avformat_seek_file(fmt_ctx, video_stream_idx, INT64_MIN, 0, 0, 0);
    avcodec_flush_buffers(codec_ctx);

    /* Check for is_grayscale metadata tag */
    int is_grayscale = -1;  /* -1 means not specified in metadata */
    AVDictionaryEntry *tag = av_dict_get(fmt_ctx->metadata, "is_grayscale", NULL, 0);
    if (tag && tag->value) {
        is_grayscale = (strcmp(tag->value, "1") == 0) ? 1 : 0;
    }

    /* Allocate empty GOP frame cache (will be populated on first read) */
    H265FrameCache *frame_cache = alloc_frame_cache();
    if (!frame_cache) {
        mxFree(dts_array);
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);
        mxFree(filename);
        mexErrMsgIdAndTxt("open_h265_video:allocCache", "Could not allocate frame cache");
    }

    /* Create output struct - keep fmt_ctx, codec_ctx, and cache open */
    const char *field_names[] = {"filename", "num_frames", "width", "height", "dts",
                                  "fmt_ctx_ptr", "codec_ctx_ptr", "video_stream_idx", "pts_increment",
                                  "time_base_num", "time_base_den", "frame_rate_num", "frame_rate_den",
                                  "is_grayscale", "cache_ptr"};
    plhs[0] = mxCreateStructMatrix(1, 1, 15, field_names);

    /* Helper variables for typed arrays */
    mxArray *mx_int32;
    mxArray *mx_int64;
    mxArray *mx_uint64;

    /* Set filename */
    mxSetField(plhs[0], 0, "filename", mxCreateString(filename));

    /* Set num_frames, width, height (double for MATLAB convenience) */
    mxSetField(plhs[0], 0, "num_frames", mxCreateDoubleScalar((double)num_frames));
    mxSetField(plhs[0], 0, "width", mxCreateDoubleScalar((double)width));
    mxSetField(plhs[0], 0, "height", mxCreateDoubleScalar((double)height));

    /* Set dts array (int64) */
    mxArray *dts_mx = mxCreateNumericMatrix(1, num_frames, mxINT64_CLASS, mxREAL);
    int64_t *dts_data = (int64_t *)mxGetData(dts_mx);
    for (int i = 0; i < num_frames; i++) {
        dts_data[i] = dts_array[i];
    }
    mxSetField(plhs[0], 0, "dts", dts_mx);

    /* Store pointers as uint64 */
    mx_uint64 = mxCreateNumericMatrix(1, 1, mxUINT64_CLASS, mxREAL);
    *(uint64_t *)mxGetData(mx_uint64) = (uint64_t)(uintptr_t)fmt_ctx;
    mxSetField(plhs[0], 0, "fmt_ctx_ptr", mx_uint64);

    mx_uint64 = mxCreateNumericMatrix(1, 1, mxUINT64_CLASS, mxREAL);
    *(uint64_t *)mxGetData(mx_uint64) = (uint64_t)(uintptr_t)codec_ctx;
    mxSetField(plhs[0], 0, "codec_ctx_ptr", mx_uint64);

    /* Set video_stream_idx (double for MATLAB convenience) */
    mxSetField(plhs[0], 0, "video_stream_idx", mxCreateDoubleScalar((double)video_stream_idx));

    /* Set pts_increment (int64) */
    mx_int64 = mxCreateNumericMatrix(1, 1, mxINT64_CLASS, mxREAL);
    *(int64_t *)mxGetData(mx_int64) = pts_increment;
    mxSetField(plhs[0], 0, "pts_increment", mx_int64);

    /* Set time_base (int32) */
    mx_int32 = mxCreateNumericMatrix(1, 1, mxINT32_CLASS, mxREAL);
    *(int32_t *)mxGetData(mx_int32) = time_base.num;
    mxSetField(plhs[0], 0, "time_base_num", mx_int32);

    mx_int32 = mxCreateNumericMatrix(1, 1, mxINT32_CLASS, mxREAL);
    *(int32_t *)mxGetData(mx_int32) = time_base.den;
    mxSetField(plhs[0], 0, "time_base_den", mx_int32);

    /* Set frame_rate (int32) */
    mx_int32 = mxCreateNumericMatrix(1, 1, mxINT32_CLASS, mxREAL);
    *(int32_t *)mxGetData(mx_int32) = frame_rate.num;
    mxSetField(plhs[0], 0, "frame_rate_num", mx_int32);

    mx_int32 = mxCreateNumericMatrix(1, 1, mxINT32_CLASS, mxREAL);
    *(int32_t *)mxGetData(mx_int32) = frame_rate.den;
    mxSetField(plhs[0], 0, "frame_rate_den", mx_int32);

    /* Set is_grayscale (-1 if not in metadata, 0 or 1 otherwise) */
    mxSetField(plhs[0], 0, "is_grayscale", mxCreateDoubleScalar((double)is_grayscale));

    /* Store cache pointer as uint64 */
    mx_uint64 = mxCreateNumericMatrix(1, 1, mxUINT64_CLASS, mxREAL);
    *(uint64_t *)mxGetData(mx_uint64) = (uint64_t)(uintptr_t)frame_cache;
    mxSetField(plhs[0], 0, "cache_ptr", mx_uint64);

    /* Free temporary arrays (but NOT fmt_ctx, codec_ctx, or cache - they stay open) */
    mxFree(dts_array);
    mxFree(filename);
}
