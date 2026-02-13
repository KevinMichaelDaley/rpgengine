/**
 * @file encode_thread.h
 * @brief Encoder thread that consumes frames and writes to file.
 *
 * Runs as a dedicated pthread.  Polls the frame ring for completed
 * frames and pipes raw RGBA to ffmpeg for encoding (or writes raw
 * frames if ffmpeg is unavailable).  Never touches GL.
 */

#ifndef FR_VIDEO_CAPTURE_ENCODE_THREAD_H
#define FR_VIDEO_CAPTURE_ENCODE_THREAD_H

#include "frame_ring.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>

/** Encoder thread context. */
typedef struct fr_encode_thread {
    pthread_t           thread;
    fr_frame_ring_t    *ring;           /**< Borrowed — owned by video_capture. */
    int                 width;
    int                 height;
    int                 fps;
    char                output_path[512];
    FILE               *pipe;          /**< ffmpeg pipe or raw file handle. */
    atomic_int          stop_requested;
    atomic_uint_fast64_t frames_written;
} fr_encode_thread_t;

/**
 * @brief Start the encoder thread.
 * @param enc          Encoder context to initialize.
 * @param ring         Frame ring to consume from (borrowed).
 * @param width        Frame width.
 * @param height       Frame height.
 * @param fps          Target FPS for output video.
 * @param output_path  Output file path.
 * @return 0 on success, -1 on failure.
 */
int fr_encode_thread_start(fr_encode_thread_t *enc,
                           fr_frame_ring_t *ring,
                           int width, int height, int fps,
                           const char *output_path);

/**
 * @brief Signal the encoder thread to stop and join it.
 *
 * The thread will drain any remaining frames before exiting.
 *
 * @param enc  Encoder context.
 */
void fr_encode_thread_stop(fr_encode_thread_t *enc);

#endif /* FR_VIDEO_CAPTURE_ENCODE_THREAD_H */
