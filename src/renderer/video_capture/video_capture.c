/**
 * @file video_capture.c
 * @brief Public API glue for GPU-buffered video capture.
 */

#define _POSIX_C_SOURCE 200809L

#include "ferrum/renderer/video_capture.h"
#include "pbo_ring.h"
#include "frame_ring.h"
#include "encode_thread.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

/** Read CLOCK_MONOTONIC in nanoseconds. */
static uint64_t vc_clock_ns_(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/** Full video capture context. */
struct fr_video_capture {
    fr_pbo_ring_t       pbo_ring;
    fr_frame_ring_t     frame_ring;
    fr_encode_thread_t  encoder;
    int                 width;
    int                 height;
    uint64_t            capture_interval_ns; /**< Min ns between captures. */
    uint64_t            last_capture_ns;     /**< Timestamp of last readback. */
};

/** Callback from PBO harvest: copies pixels into the frame ring. */
static void pbo_harvest_cb_(const uint8_t *pixels, uint32_t frame_bytes,
                             uint64_t timestamp_ns, void *user_data) {
    (void)timestamp_ns;
    fr_frame_ring_t *ring = (fr_frame_ring_t *)user_data;
    fr_frame_ring_push(ring, pixels, frame_bytes, 0);
}

fr_video_capture_t *fr_video_capture_create(
        const fr_video_capture_desc_t *desc) {
    if (!desc || desc->width <= 0 || desc->height <= 0 || !desc->output_path) {
        return NULL;
    }

    fr_video_capture_t *cap = (fr_video_capture_t *)calloc(
        1, sizeof(fr_video_capture_t));
    if (!cap) { return NULL; }

    cap->width  = desc->width;
    cap->height = desc->height;

    uint32_t frame_bytes = (uint32_t)(desc->width * desc->height * 4);

    /* Initialize GPU PBO ring. */
    fr_pbo_ring_init(&cap->pbo_ring, desc->width, desc->height);

    /* Initialize CPU frame ring. */
    fr_frame_ring_init(&cap->frame_ring, frame_bytes);

    /* Set up decimation: only capture at target FPS rate. */
    int fps = desc->fps > 0 ? desc->fps : 60;
    cap->capture_interval_ns = 1000000000ULL / (uint64_t)fps;
    cap->last_capture_ns = 0;
    if (fr_encode_thread_start(&cap->encoder, &cap->frame_ring,
                               desc->width, desc->height, fps,
                               desc->output_path) != 0) {
        fr_pbo_ring_destroy(&cap->pbo_ring);
        fr_frame_ring_destroy(&cap->frame_ring);
        free(cap);
        return NULL;
    }

    return cap;
}

void fr_video_capture_submit_frame(fr_video_capture_t *cap) {
    if (!cap) { return; }

    /* Step 1: Harvest any completed PBO readbacks → CPU frame ring.
     * This runs GL commands (map/unmap) on the render thread. */
    fr_pbo_ring_harvest(&cap->pbo_ring, pbo_harvest_cb_,
                        &cap->frame_ring);

    /* Step 2: Decimate — only initiate a readback if enough time has
     * elapsed since the last one to match the target FPS. */
    uint64_t now = vc_clock_ns_();
    if (cap->last_capture_ns == 0 ||
        (now - cap->last_capture_ns) >= cap->capture_interval_ns) {
        fr_pbo_ring_begin_readback(&cap->pbo_ring);
        cap->last_capture_ns = now;
    }
}

void fr_video_capture_destroy(fr_video_capture_t *cap) {
    if (!cap) { return; }

    /* Flush: harvest all remaining PBOs (blocking wait). */
    for (int attempt = 0; attempt < 100; attempt++) {
        int n = fr_pbo_ring_harvest(&cap->pbo_ring, pbo_harvest_cb_,
                                    &cap->frame_ring);
        if (cap->pbo_ring.count == 0) { break; }
        /* Brief spin if PBOs still pending — give GPU time. */
        (void)n;
        struct timespec ts = { .tv_sec = 0, .tv_nsec = 1000000 };
        nanosleep(&ts, NULL);
    }

    /* Stop encoder thread (drains remaining CPU frames). */
    fr_encode_thread_stop(&cap->encoder);

    /* Clean up GPU and CPU resources. */
    fr_pbo_ring_destroy(&cap->pbo_ring);
    fr_frame_ring_destroy(&cap->frame_ring);
    free(cap);
}

uint64_t fr_video_capture_frames_written(const fr_video_capture_t *cap) {
    if (!cap) { return 0; }
    return atomic_load_explicit(
        &((fr_encode_thread_t *)&cap->encoder)->frames_written,
        memory_order_relaxed);
}
