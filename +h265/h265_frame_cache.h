/*
 * h265_frame_cache.h
 * GOP frame cache structure for H.265 video reading.
 *
 * The cache stores decoded frames from a GOP (Group of Pictures) to avoid
 * re-decoding when reading sequential frames within the same GOP.
 *
 * Each H265Reader instance has its own cache, stored as a pointer in the
 * video_info struct returned by open_h265_video.
 */

#ifndef H265_FRAME_CACHE_H
#define H265_FRAME_CACHE_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint8_t **frame_data;    /* Handle to frame data block (for reallocation) */
    int **frame_indices;     /* Handle to frame index array (for reallocation) */
    int num_frames;          /* Number of frames currently in cache */
    int capacity;            /* Maximum frames that fit in allocated block */
    int width;
    int height;
    int is_grayscale;        /* Output format: 1 for grayscale, 0 for RGB */
    size_t frame_size;       /* Size of each frame in bytes */
} H265FrameCache;

#endif /* H265_FRAME_CACHE_H */
