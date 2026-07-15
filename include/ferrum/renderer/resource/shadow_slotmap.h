/**
 * @file shadow_slotmap.h
 * @brief First-fit contiguous-run allocator over the layers of a shadow
 *        texture array (or slots of a shadow atlas). Pure bookkeeping, no GL.
 *
 * Each stationary light claims a contiguous run of layers (N cascades for its
 * static map, 1 for its dynamic map); the slotmap hands those out and reclaims
 * them. Storage is caller-provided (one byte per layer), so there is no dynamic
 * allocation. Deliberately trivial (linear scan) -- layer counts are tiny.
 */
#ifndef FERRUM_RENDERER_RESOURCE_SHADOW_SLOTMAP_H
#define FERRUM_RENDERER_RESOURCE_SHADOW_SLOTMAP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Layer-occupancy bitmap over caller-provided backing (1 byte/layer). */
typedef struct shadow_slotmap {
    uint8_t *slots;     /**< borrowed: `capacity` bytes (0=free, 1=used). */
    uint32_t capacity;
    uint32_t used;
} shadow_slotmap_t;

/**
 * @brief Initialise over @p backing (@p capacity bytes), all layers free.
 *        No-op if @p m or @p backing is NULL, or @p capacity is 0.
 */
void shadow_slotmap_init(shadow_slotmap_t *m, uint8_t *backing, uint32_t capacity);

/**
 * @brief Reserve the first free run of @p count contiguous layers. Returns the
 *        base layer index, or -1 if @p count is 0, exceeds capacity, no run
 *        fits, or @p m is NULL. The map is unchanged on failure.
 */
int32_t shadow_slotmap_alloc(shadow_slotmap_t *m, uint32_t count);

/**
 * @brief Release the run [@p base, @p base+@p count). Out-of-range layers are
 *        clamped/ignored; only currently-used layers reduce the used count.
 */
void shadow_slotmap_free(shadow_slotmap_t *m, uint32_t base, uint32_t count);

/** @brief Number of currently-used layers (0 if @p m is NULL). */
uint32_t shadow_slotmap_used(const shadow_slotmap_t *m);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_RENDERER_RESOURCE_SHADOW_SLOTMAP_H */
