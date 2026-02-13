/**
 * @file frame_ring.h
 * @brief Lock-free SPSC ring buffer for captured frame data.
 *
 * The render thread produces frames (writes), the encoder thread
 * consumes them (reads).  Single-producer single-consumer; no locks
 * needed — just atomic head/tail indices.
 */

#ifndef FR_VIDEO_CAPTURE_FRAME_RING_H
#define FR_VIDEO_CAPTURE_FRAME_RING_H

#include <stdatomic.h>
#include <stdint.h>

/** Number of frame slots in the ring.  Must be a power of two. */
#define FR_FRAME_RING_CAPACITY 8

/** A single frame slot. */
typedef struct fr_frame_slot {
    uint8_t *pixels;       /**< Heap-allocated RGBA pixel buffer. */
    uint32_t frame_bytes;  /**< Size of pixel data in bytes. */
} fr_frame_slot_t;

/** SPSC frame ring buffer. */
typedef struct fr_frame_ring {
    fr_frame_slot_t slots[FR_FRAME_RING_CAPACITY];
    atomic_uint_fast32_t head;  /**< Producer writes here (render thread). */
    atomic_uint_fast32_t tail;  /**< Consumer reads here (encode thread). */
} fr_frame_ring_t;

/**
 * @brief Initialize the frame ring, allocating pixel buffers.
 * @param ring        Ring to initialize.
 * @param frame_bytes Size of each frame in bytes (w * h * 4).
 */
void fr_frame_ring_init(fr_frame_ring_t *ring, uint32_t frame_bytes);

/**
 * @brief Destroy the frame ring, freeing pixel buffers.
 * @param ring  Ring to destroy.  NULL is a safe no-op.
 */
void fr_frame_ring_destroy(fr_frame_ring_t *ring);

/**
 * @brief Push a frame into the ring (producer / render thread).
 *
 * Copies pixel data into the next available slot.  If the ring is
 * full, the write overwrites the oldest unread frame (tail advances).
 *
 * @param ring        Frame ring.
 * @param pixels      Source pixel data (RGBA, bottom-up).
 * @param frame_bytes Number of bytes to copy.
 * @return 1 if pushed normally, 0 if a frame was dropped to make room.
 */
int fr_frame_ring_push(fr_frame_ring_t *ring,
                       const uint8_t *pixels, uint32_t frame_bytes);

/**
 * @brief Pop a frame from the ring (consumer / encode thread).
 *
 * Returns a pointer to the oldest unread frame's pixel data.  The
 * pointer is valid until the next call to fr_frame_ring_pop().
 *
 * @param ring         Frame ring.
 * @param out_bytes    Receives the frame size in bytes.
 * @return Pointer to pixel data, or NULL if ring is empty.
 */
const uint8_t *fr_frame_ring_pop(fr_frame_ring_t *ring,
                                 uint32_t *out_bytes);

/**
 * @brief Query how many frames are available to read.
 * @param ring  Frame ring.
 * @return Number of unread frames.
 */
uint32_t fr_frame_ring_count(const fr_frame_ring_t *ring);

#endif /* FR_VIDEO_CAPTURE_FRAME_RING_H */
