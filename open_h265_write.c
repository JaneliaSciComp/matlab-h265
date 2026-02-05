/*
 * open_h265_write.c
 * MEX function to open a video file for writing H.265 with closed GOP.
 *
 * Usage: writer = open_h265_write(filename, width, height, frame_rate)
 *   filename   - output file path (must end in .mp4)
 *   width      - frame width
 *   height     - frame height
 *   frame_rate - frame rate as [num, den] or scalar (e.g., [14997, 100] or 30)
 *
 * Returns a struct with encoder context pointers for write_h265_frame.
 * IMPORTANT: Call close_h265_write(writer) when done to flush and close.
 *
 * Compile with:
 *   mex open_h265_write.c -lavformat -lavcodec -lavutil -lswscale
 */

#include "mex.h"
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <stdint.h>
#include <string.h>

/* Mutable state that can be updated by write_ffmpeg_frame */
typedef struct {
    int64_t next_pts;
    int64_t pts_increment;
} WriterState;

void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
    char *filename;
    int width, height;
    int frame_rate_num, frame_rate_den;

    AVFormatContext *fmt_ctx = NULL;
    AVCodecContext *codec_ctx = NULL;
    const AVCodec *codec = NULL;
    AVStream *video_stream = NULL;
    AVFrame *frame = NULL;
    WriterState *state = NULL;
    int ret;

    /* Check arguments */
    if (nrhs != 4) {
        mexErrMsgIdAndTxt("open_h265_write:nrhs",
            "Four inputs required: filename, width, height, frame_rate");
    }
    if (!mxIsChar(prhs[0])) {
        mexErrMsgIdAndTxt("open_h265_write:notString", "Filename must be a string");
    }
    if (!mxIsDouble(prhs[1]) || mxGetNumberOfElements(prhs[1]) != 1) {
        mexErrMsgIdAndTxt("open_h265_write:notScalar", "Width must be a scalar");
    }
    if (!mxIsDouble(prhs[2]) || mxGetNumberOfElements(prhs[2]) != 1) {
        mexErrMsgIdAndTxt("open_h265_write:notScalar", "Height must be a scalar");
    }

    filename = mxArrayToString(prhs[0]);
    width = (int)mxGetScalar(prhs[1]);
    height = (int)mxGetScalar(prhs[2]);

    /* Parse frame rate - can be scalar or [num, den] */
    if (mxGetNumberOfElements(prhs[3]) == 1) {
        frame_rate_num = (int)mxGetScalar(prhs[3]);
        frame_rate_den = 1;
    } else if (mxGetNumberOfElements(prhs[3]) == 2) {
        double *fr = mxGetPr(prhs[3]);
        frame_rate_num = (int)fr[0];
        frame_rate_den = (int)fr[1];
    } else {
        mxFree(filename);
        mexErrMsgIdAndTxt("open_h265_write:badFrameRate",
            "Frame rate must be scalar or [num, den]");
    }

    /* Validate dimensions */
    if (width <= 0 || height <= 0) {
        mxFree(filename);
        mexErrMsgIdAndTxt("open_h265_write:badDimensions",
            "Width and height must be positive");
    }

    /* Allocate output format context */
    ret = avformat_alloc_output_context2(&fmt_ctx, NULL, NULL, filename);
    if (ret < 0 || !fmt_ctx) {
        mxFree(filename);
        mexErrMsgIdAndTxt("open_h265_write:allocFormat",
            "Could not allocate output format context");
    }

    /* Find H.265 encoder */
    codec = avcodec_find_encoder(AV_CODEC_ID_HEVC);
    if (!codec) {
        avformat_free_context(fmt_ctx);
        mxFree(filename);
        mexErrMsgIdAndTxt("open_h265_write:noCodec",
            "Could not find H.265 encoder. Is libx265 installed?");
    }

    /* Create video stream */
    video_stream = avformat_new_stream(fmt_ctx, NULL);
    if (!video_stream) {
        avformat_free_context(fmt_ctx);
        mxFree(filename);
        mexErrMsgIdAndTxt("open_h265_write:newStream",
            "Could not create video stream");
    }

    /* Allocate codec context */
    codec_ctx = avcodec_alloc_context3(codec);
    if (!codec_ctx) {
        avformat_free_context(fmt_ctx);
        mxFree(filename);
        mexErrMsgIdAndTxt("open_h265_write:allocCodec",
            "Could not allocate codec context");
    }

    /* Set codec parameters */
    codec_ctx->codec_id = AV_CODEC_ID_HEVC;
    codec_ctx->codec_type = AVMEDIA_TYPE_VIDEO;
    codec_ctx->width = width;
    codec_ctx->height = height;
    codec_ctx->time_base = (AVRational){frame_rate_den, frame_rate_num};
    codec_ctx->framerate = (AVRational){frame_rate_num, frame_rate_den};
    codec_ctx->pix_fmt = AV_PIX_FMT_GRAY8;  /* Native grayscale */
    codec_ctx->gop_size = 50;  /* Keyframe interval */

    /* Set x265 params for closed GOP */
    ret = av_opt_set(codec_ctx->priv_data, "x265-params",
                     "no-open-gop=1:keyint=50", 0);
    if (ret < 0) {
        avcodec_free_context(&codec_ctx);
        avformat_free_context(fmt_ctx);
        mxFree(filename);
        mexErrMsgIdAndTxt("open_h265_write:x265Params",
            "Could not set x265 params");
    }

    /* Some formats require global headers */
    if (fmt_ctx->oformat->flags & AVFMT_GLOBALHEADER) {
        codec_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    /* Open codec */
    ret = avcodec_open2(codec_ctx, codec, NULL);
    if (ret < 0) {
        avcodec_free_context(&codec_ctx);
        avformat_free_context(fmt_ctx);
        mxFree(filename);
        mexErrMsgIdAndTxt("open_h265_write:openCodec",
            "Could not open codec: %s", av_err2str(ret));
    }

    /* Copy codec parameters to stream */
    ret = avcodec_parameters_from_context(video_stream->codecpar, codec_ctx);
    if (ret < 0) {
        avcodec_free_context(&codec_ctx);
        avformat_free_context(fmt_ctx);
        mxFree(filename);
        mexErrMsgIdAndTxt("open_h265_write:codecParams",
            "Could not copy codec parameters");
    }

    video_stream->time_base = codec_ctx->time_base;
    video_stream->avg_frame_rate = (AVRational){frame_rate_num, frame_rate_den};
    video_stream->r_frame_rate = (AVRational){frame_rate_num, frame_rate_den};

    /* Open output file */
    if (!(fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&fmt_ctx->pb, filename, AVIO_FLAG_WRITE);
        if (ret < 0) {
            avcodec_free_context(&codec_ctx);
            avformat_free_context(fmt_ctx);
            mxFree(filename);
            mexErrMsgIdAndTxt("open_h265_write:openFile",
                "Could not open output file");
        }
    }

    /* Write file header */
    ret = avformat_write_header(fmt_ctx, NULL);
    if (ret < 0) {
        avio_closep(&fmt_ctx->pb);
        avcodec_free_context(&codec_ctx);
        avformat_free_context(fmt_ctx);
        mxFree(filename);
        mexErrMsgIdAndTxt("open_h265_write:writeHeader",
            "Could not write file header");
    }

    /* Allocate frame for encoding */
    frame = av_frame_alloc();
    if (!frame) {
        avio_closep(&fmt_ctx->pb);
        avcodec_free_context(&codec_ctx);
        avformat_free_context(fmt_ctx);
        mxFree(filename);
        mexErrMsgIdAndTxt("open_h265_write:allocFrame",
            "Could not allocate frame");
    }

    frame->format = codec_ctx->pix_fmt;
    frame->width = width;
    frame->height = height;

    ret = av_frame_get_buffer(frame, 0);
    if (ret < 0) {
        av_frame_free(&frame);
        avio_closep(&fmt_ctx->pb);
        avcodec_free_context(&codec_ctx);
        avformat_free_context(fmt_ctx);
        mxFree(filename);
        mexErrMsgIdAndTxt("open_h265_write:frameBuffer",
            "Could not allocate frame buffer");
    }

    /* Allocate mutable state struct */
    state = (WriterState *)malloc(sizeof(WriterState));
    if (!state) {
        av_frame_free(&frame);
        avio_closep(&fmt_ctx->pb);
        avcodec_free_context(&codec_ctx);
        avformat_free_context(fmt_ctx);
        mxFree(filename);
        mexErrMsgIdAndTxt("open_h265_write:allocState",
            "Could not allocate writer state");
    }

    /* Compute pts_increment: time_base units per frame */
    /* pts_increment = time_base.den * frame_rate.den / (time_base.num * frame_rate.num) */
    /* Since time_base = frame_rate.den / frame_rate.num, pts_increment = 1 */
    state->next_pts = 0;
    state->pts_increment = 1;  /* With our time_base setup, each frame is 1 time unit */

    /* Create output struct */
    const char *field_names[] = {"filename", "width", "height",
                                  "fmt_ctx_ptr", "codec_ctx_ptr", "frame_ptr",
                                  "state_ptr", "stream_idx"};
    plhs[0] = mxCreateStructMatrix(1, 1, 8, field_names);

    mxSetField(plhs[0], 0, "filename", mxCreateString(filename));
    mxSetField(plhs[0], 0, "width", mxCreateDoubleScalar((double)width));
    mxSetField(plhs[0], 0, "height", mxCreateDoubleScalar((double)height));
    mxSetField(plhs[0], 0, "fmt_ctx_ptr", mxCreateDoubleScalar((double)(uintptr_t)fmt_ctx));
    mxSetField(plhs[0], 0, "codec_ctx_ptr", mxCreateDoubleScalar((double)(uintptr_t)codec_ctx));
    mxSetField(plhs[0], 0, "frame_ptr", mxCreateDoubleScalar((double)(uintptr_t)frame));
    mxSetField(plhs[0], 0, "state_ptr", mxCreateDoubleScalar((double)(uintptr_t)state));
    mxSetField(plhs[0], 0, "stream_idx", mxCreateDoubleScalar(0));

    mxFree(filename);
}
