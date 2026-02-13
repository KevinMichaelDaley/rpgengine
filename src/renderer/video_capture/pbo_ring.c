/**
 * @file pbo_ring.c
 * @brief PBO ring implementation for async GPU→CPU readback.
 *
 * All functions require a current GL context on the calling thread.
 */

#include "pbo_ring.h"
#include <string.h>

void fr_pbo_ring_init(fr_pbo_ring_t *ring, int width, int height) {
    if (!ring) { return; }
    memset(ring, 0, sizeof(*ring));
    ring->width  = width;
    ring->height = height;
    ring->frame_bytes = (uint32_t)(width * height * 4);

    /* Allocate PBOs sized for one RGBA frame each. */
    for (int i = 0; i < FR_PBO_RING_CAPACITY; i++) {
        glGenBuffers(1, &ring->slots[i].pbo);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, ring->slots[i].pbo);
        glBufferData(GL_PIXEL_PACK_BUFFER, ring->frame_bytes,
                     NULL, GL_STREAM_READ);
        ring->slots[i].fence = NULL;
        ring->slots[i].state = FR_PBO_SLOT_FREE;
    }
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
}

void fr_pbo_ring_destroy(fr_pbo_ring_t *ring) {
    if (!ring) { return; }
    for (int i = 0; i < FR_PBO_RING_CAPACITY; i++) {
        if (ring->slots[i].fence) {
            glDeleteSync(ring->slots[i].fence);
        }
        if (ring->slots[i].pbo) {
            glDeleteBuffers(1, &ring->slots[i].pbo);
        }
    }
    memset(ring, 0, sizeof(*ring));
}

int fr_pbo_ring_begin_readback(fr_pbo_ring_t *ring) {
    if (!ring) { return 0; }
    if (ring->count >= FR_PBO_RING_CAPACITY) { return 0; }

    fr_pbo_slot_t *slot = &ring->slots[ring->head];
    if (slot->state != FR_PBO_SLOT_FREE) { return 0; }

    /* Bind PBO and issue async readback from default framebuffer. */
    glBindBuffer(GL_PIXEL_PACK_BUFFER, slot->pbo);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, ring->width, ring->height,
                 GL_RGBA, GL_UNSIGNED_BYTE, NULL);

    /* Place a fence to know when the transfer completes. */
    slot->fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    slot->state = FR_PBO_SLOT_PENDING;

    ring->head = (ring->head + 1) % FR_PBO_RING_CAPACITY;
    ring->count++;
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    return 1;
}

int fr_pbo_ring_harvest(fr_pbo_ring_t *ring,
                        void (*on_frame)(const uint8_t *pixels,
                                         uint32_t frame_bytes,
                                         void *user_data),
                        void *user_data) {
    if (!ring || !on_frame) { return 0; }

    int harvested = 0;
    /* Walk from tail (oldest pending) forward. */
    int tail = (ring->head - ring->count + FR_PBO_RING_CAPACITY)
               % FR_PBO_RING_CAPACITY;

    for (int i = 0; i < ring->count; i++) {
        int idx = (tail + i) % FR_PBO_RING_CAPACITY;
        fr_pbo_slot_t *slot = &ring->slots[idx];

        if (slot->state != FR_PBO_SLOT_PENDING) { continue; }

        /* Non-blocking fence check. */
        GLenum result = glClientWaitSync(slot->fence, 0, 0);
        if (result == GL_TIMEOUT_EXPIRED ||
            result == GL_WAIT_FAILED) {
            /* Not ready yet — stop; older slots won't be ready either
             * since GPU processes in order. */
            break;
        }

        /* Fence signalled — map and deliver pixels. */
        glDeleteSync(slot->fence);
        slot->fence = NULL;

        glBindBuffer(GL_PIXEL_PACK_BUFFER, slot->pbo);
        const uint8_t *pixels = (const uint8_t *)glMapBufferRange(
            GL_PIXEL_PACK_BUFFER, 0, ring->frame_bytes, GL_MAP_READ_BIT);

        if (pixels) {
            on_frame(pixels, ring->frame_bytes, user_data);
            glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
        }

        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        slot->state = FR_PBO_SLOT_FREE;
        harvested++;
    }

    ring->count -= harvested;
    return harvested;
}
