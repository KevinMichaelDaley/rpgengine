#ifndef FERRUM_RENDERER_DEBUG_LINES_H
#define FERRUM_RENDERER_DEBUG_LINES_H

#include <stdbool.h>
#include <stddef.h>

#include "ferrum/math/vec3.h"

/** @file
 * @brief Small fixed-capacity debug line store (CPU-side).
 */

#ifdef __cplusplus
extern "C" {
#endif

/** A single debug line segment in world space. */
typedef struct fr_debug_line {
    vec3_t a;
    vec3_t b;
    double expire_time_s;
} fr_debug_line_t;

/** Fixed-capacity store for debug lines. */
typedef struct fr_debug_lines {
    fr_debug_line_t *lines;
    size_t capacity;
    size_t count;
    size_t head;
} fr_debug_lines_t;

/**
 * @brief Initialize a debug line store.
 *
 * Ownership: caller owns storage.
 * Nullability: all pointers must be non-NULL.
 * Error semantics: if capacity is 0, the store will remain empty.
 * Side effects: writes the store fields.
 */
void fr_debug_lines_init(fr_debug_lines_t *store, fr_debug_line_t *storage, size_t capacity);

/**
 * @brief Add a line segment with time-to-live.
 *
 * If the store is full, the oldest line is overwritten.
 *
 * @param store Store (non-NULL).
 * @param a Start point.
 * @param b End point.
 * @param now_time_s Current time in seconds.
 * @param ttl_s Time-to-live in seconds (<=0 drops the line).
 * @return true if the line was accepted.
 */
bool fr_debug_lines_add(fr_debug_lines_t *store, vec3_t a, vec3_t b, double now_time_s, double ttl_s);

/**
 * @brief Collect live lines into a vertex list and prune expired lines.
 *
 * Writes vertices as pairs: [a0, b0, a1, b1, ...].
 *
 * @param store Store (non-NULL).
 * @param now_time_s Current time.
 * @param out_vertices Output vertex buffer (non-NULL).
 * @param out_vertices_cap Capacity in vec3_t units.
 * @param out_vertex_count Output vertex count written (non-NULL).
 * @return true on success, false on invalid args or insufficient capacity.
 */
bool fr_debug_lines_collect_vertices(fr_debug_lines_t *store,
                                   double now_time_s,
                                   vec3_t *out_vertices,
                                   size_t out_vertices_cap,
                                   size_t *out_vertex_count);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_RENDERER_DEBUG_LINES_H */
