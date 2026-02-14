/**
 * @file encode_thread.c
 * @brief Encoder thread: consumes frames from ring, pipes to ffmpeg.
 *
 * If ffmpeg is not found on PATH, falls back to writing raw RGBA
 * frames to a binary file.  Never touches GL.
 */

#define _POSIX_C_SOURCE 200809L

#include "encode_thread.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/** Try to open an ffmpeg pipe for encoding.  Returns NULL on failure. */
static FILE *open_ffmpeg_pipe_(int width, int height, int fps,
                               const char *output_path) {
    /* Check if ffmpeg is available. */
    if (system("which ffmpeg > /dev/null 2>&1") != 0) {
        return NULL;
    }

    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
             "ffmpeg -y -f rawvideo -pixel_format rgba "
             "-video_size %dx%d -framerate %d "
             "-i pipe:0 "
             "-vf vflip "
             "-c:v libx264 -preset fast -crf 18 -pix_fmt yuv444p "
             "%s 2>/dev/null",
             width, height, fps, output_path);
    return popen(cmd, "w");
}

/** Encoder thread entry point. */
static void *encode_thread_fn_(void *user_data) {
    fr_encode_thread_t *enc = (fr_encode_thread_t *)user_data;

    /* Try ffmpeg first, fall back to raw file. */
    enc->pipe = open_ffmpeg_pipe_(enc->width, enc->height,
                                  enc->fps, enc->output_path);
    int using_ffmpeg = (enc->pipe != NULL);
    if (!using_ffmpeg) {
        /* Append .raw to path for raw RGBA output. */
        char raw_path[520];
        snprintf(raw_path, sizeof(raw_path), "%s.raw", enc->output_path);
        enc->pipe = fopen(raw_path, "wb");
    }

    if (!enc->pipe) {
        /* Can't write anywhere — bail. */
        return NULL;
    }

    struct timespec idle_ts = { .tv_sec = 0, .tv_nsec = 1000000 }; /* 1ms */

    while (!atomic_load_explicit(&enc->stop_requested,
                                  memory_order_acquire)) {
        uint32_t nbytes = 0;
        uint64_t ts_ns = 0;
        const uint8_t *pixels = fr_frame_ring_pop(enc->ring, &nbytes, &ts_ns);
        if (!pixels) {
            nanosleep(&idle_ts, NULL);
            continue;
        }

        /* Write the frame. */
        size_t written = fwrite(pixels, 1, nbytes, enc->pipe);
        if (written == nbytes) {
            atomic_fetch_add_explicit(&enc->frames_written, 1,
                                       memory_order_relaxed);
        }
    }

    /* Drain remaining frames. */
    for (;;) {
        uint32_t nbytes = 0;
        uint64_t ts_ns = 0;
        const uint8_t *pixels = fr_frame_ring_pop(enc->ring, &nbytes, &ts_ns);
        if (!pixels) { break; }

        size_t written = fwrite(pixels, 1, nbytes, enc->pipe);
        if (written == nbytes) {
            atomic_fetch_add_explicit(&enc->frames_written, 1,
                                       memory_order_relaxed);
        }
    }

    /* Close pipe/file. */
    if (using_ffmpeg) {
        pclose(enc->pipe);
    } else {
        fclose(enc->pipe);
    }
    enc->pipe = NULL;

    return NULL;
}

int fr_encode_thread_start(fr_encode_thread_t *enc,
                           fr_frame_ring_t *ring,
                           int width, int height, int fps,
                           const char *output_path) {
    if (!enc || !ring || !output_path) { return -1; }
    memset(enc, 0, sizeof(*enc));

    enc->ring   = ring;
    enc->width  = width;
    enc->height = height;
    enc->fps    = fps;
    snprintf(enc->output_path, sizeof(enc->output_path), "%s", output_path);

    atomic_init(&enc->stop_requested, 0);
    atomic_init(&enc->frames_written, 0);

    if (pthread_create(&enc->thread, NULL, encode_thread_fn_, enc) != 0) {
        return -1;
    }
    return 0;
}

void fr_encode_thread_stop(fr_encode_thread_t *enc) {
    if (!enc) { return; }
    atomic_store_explicit(&enc->stop_requested, 1, memory_order_release);
    pthread_join(enc->thread, NULL);
}
