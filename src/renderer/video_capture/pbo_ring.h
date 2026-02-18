/**
 * @file pbo_ring.h
 * @brief Internal PBO ring for async GPU→CPU framebuffer readback.
 *
 * Manages a fixed-size ring of OpenGL Pixel Buffer Objects with
 * associated fence sync objects.  All functions must be called from
 * the render thread (GL context must be current).
 */

#ifndef FR_VIDEO_CAPTURE_PBO_RING_H
#define FR_VIDEO_CAPTURE_PBO_RING_H

#include <glad/glad.h>
#include <stdint.h>

/** Maximum number of PBOs in the ring. */
#define FR_PBO_RING_CAPACITY 4

/** State of a single PBO slot. */
typedef enum fr_pbo_slot_state {
    FR_PBO_SLOT_FREE,       /**< Available for new readback. */
    FR_PBO_SLOT_PENDING,    /**< ReadPixels issued, fence not signalled. */
    FR_PBO_SLOT_READY,      /**< Fence signalled, data ready to map. */
} fr_pbo_slot_state_t;

/** A single PBO slot in the ring. */
typedef struct fr_pbo_slot {
    GLuint              pbo;          /**< PBO name. */
    GLsync              fence;        /**< Fence sync (NULL if free). */
    fr_pbo_slot_state_t state;        /**< Current slot state. */
    uint64_t            timestamp_ns; /**< When readback was initiated. */
} fr_pbo_slot_t;

/** PBO ring context. */
typedef struct fr_pbo_ring {
    fr_pbo_slot_t slots[FR_PBO_RING_CAPACITY];
    int           head;         /**< Next slot to use for readback. */
    int           count;        /**< Number of pending/ready slots. */
    int           width;        /**< Frame width in pixels. */
    int           height;       /**< Frame height in pixels. */
    uint32_t      frame_bytes;  /**< width * height * 4 (RGBA). */
} fr_pbo_ring_t;

/**
 * @brief Initialize the PBO ring, allocating GL buffer objects.
 * @param ring   Ring to initialize.  Must not be NULL.
 * @param width  Frame width in pixels.
 * @param height Frame height in pixels.
 * @note Side effects: creates GL PBOs.
 */
void fr_pbo_ring_init(fr_pbo_ring_t *ring, int width, int height);

/**
 * @brief Destroy the PBO ring, deleting GL objects.
 * @param ring  Ring to destroy.  NULL is a safe no-op.
 */
void fr_pbo_ring_destroy(fr_pbo_ring_t *ring);

/**
 * @brief Initiate an async readback of the default framebuffer.
 *
 * Binds the next free PBO, issues glReadPixels (async into PBO),
 * and places a fence.  Returns false if ring is full.
 *
 * @param ring  PBO ring.
 * @return 1 on success, 0 if ring is full (all slots in use).
 */
int fr_pbo_ring_begin_readback(fr_pbo_ring_t *ring);

/**
 * @brief Poll completed readbacks and map pixel data.
 *
 * Checks fences on pending slots.  For each signalled slot, maps
 * the PBO and calls the provided callback with the pixel pointer.
 * The callback must copy the data; the pointer is invalidated
 * after the callback returns (PBO is unmapped).
 *
 * @param ring      PBO ring.
 * @param on_frame  Callback receiving (pixels, frame_bytes, timestamp_ns, user_data).
 *                  pixels is width*height*4 RGBA bytes, bottom-up.
 *                  timestamp_ns is CLOCK_MONOTONIC time when readback was issued.
 * @param user_data Passed through to callback.
 * @return Number of frames harvested.
 */
int fr_pbo_ring_harvest(fr_pbo_ring_t *ring,
                        void (*on_frame)(const uint8_t *pixels,
                                         uint32_t frame_bytes,
                                         uint64_t timestamp_ns,
                                         void *user_data),
                        void *user_data);

#endif /* FR_VIDEO_CAPTURE_PBO_RING_H */
