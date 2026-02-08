/*
 * h265_frame_cache.h
 * GOP frame cache structure for h.265 video reading.
 *
 * The cache stores decoded frames from a GOP (Group of Pictures) to avoid
 * re-decoding when reading sequential frames within the same GOP.
 *
 * Frames are stored in a MATLAB array (column-major, already transposed)
 * so returning a frame is just a memcpy with no per-frame transpose needed.
 *
 * Each H265Reader instance has its own cache, stored as a pointer in the
 * video_info struct returned by open_h265_video.
 */

#ifndef H265_FRAME_CACHE_H
#define H265_FRAME_CACHE_H

#include "mex.h"
#include <stdint.h>
#include <stddef.h>

typedef struct {
    mxArray *frames;         /* MATLAB array holding cached frames (column-major) */
    int num_frames;          /* Number of frames currently in cache */
    int start_frame;         /* First frame index in cache */
    int width;
    int height;
    int is_grayscale;        /* Output format: 1 for grayscale, 0 for RGB */
    size_t frame_size;       /* Size of each frame in bytes */
} H265FrameCache;

#endif /* H265_FRAME_CACHE_H */
