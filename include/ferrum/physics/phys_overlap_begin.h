/**
 * @file phys_overlap_begin.h
 * @brief Overlap-begin detection via arbitrary overlap test callback.
 *
 * Tests candidate body pairs for interior overlap on the settled
 * backbuffer. Tracks which pairs are currently overlapping and emits
 * events only when a pair first begins overlapping (not sustained).
 *
 * **Ownership:** Caller owns the context. Init allocates, destroy frees.
 * **Nullability:** All pointer params may be NULL (no-op / empty result).
 * **Thread safety:** NOT thread-safe. Call from a single thread only.
 */
#ifndef FERRUM_PHYSICS_OVERLAP_BEGIN_H
#define FERRUM_PHYSICS_OVERLAP_BEGIN_H

#include "ferrum/physics/phys_pair_set.h"
#include "ferrum/physics/phys_vec3.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Types ────────────────────────────────────────────────────── */

/** Candidate pair for overlap testing. */
typedef struct phys_overlap_pair {
    uint32_t body_a;
    uint32_t body_b;
} phys_overlap_pair_t;

/**
 * @brief Overlap-begin event emitted when two bodies first overlap.
 *
 * Contains the body indices and an estimated overlap center.
 */
typedef struct phys_overlap_begin_event {
    uint32_t    body_a;  /**< First body index. */
    uint32_t    body_b;  /**< Second body index. */
    phys_vec3_t center;  /**< Estimated center of overlap region. */
} phys_overlap_begin_event_t;

/* ── Forward declarations ─────────────────────────────────────── */

/**
 * @brief Overlap test callback.
 *
 * Called for each candidate pair. Returns true if the pair overlaps.
 * May write an estimated center to *out_center.
 *
 * @param user_ctx   Opaque context pointer (e.g. physics state).
 * @param body_a     First body index.
 * @param body_b     Second body index.
 * @param out_center If non-NULL and overlap detected, write center here.
 * @return true if bodies overlap, false otherwise.
 */
typedef bool (*phys_overlap_test_fn)(void *user_ctx,
                                     uint32_t body_a,
                                     uint32_t body_b,
                                     phys_vec3_t *out_center);

/**
 * @brief Overlap-begin detection context.
 *
 * Maintains a pair set to track sustained overlaps and a ring of
 * events for newly-begun overlaps each tick.
 */
typedef struct phys_overlap_begin_ctx {
    phys_pair_set_t              pair_set;       /**< Tracks active overlap pairs. */
    phys_overlap_begin_event_t  *events;         /**< Event output buffer. */
    uint32_t                     event_count;    /**< Events written this tick. */
    uint32_t                     event_capacity; /**< Max events per tick. */
} phys_overlap_begin_ctx_t;

/* ── API ──────────────────────────────────────────────────────── */

/**
 * @brief Initialize overlap-begin context.
 *
 * @param ctx            Context to initialize.
 * @param pair_capacity  Initial capacity for the pair set (power of 2 recommended).
 * @param event_capacity Maximum overlap-begin events per tick.
 * @return true on success, false on allocation failure or invalid args.
 */
bool phys_overlap_begin_init(phys_overlap_begin_ctx_t *ctx,
                             uint32_t pair_capacity,
                             uint32_t event_capacity);

/**
 * @brief Destroy overlap-begin context and free memory.
 * @param ctx Context to destroy. May be NULL (no-op).
 */
void phys_overlap_begin_destroy(phys_overlap_begin_ctx_t *ctx);

/**
 * @brief Run overlap detection for this tick.
 *
 * For each candidate pair, calls the overlap test. New overlaps (not
 * seen last tick) emit events. Pairs no longer overlapping are pruned.
 *
 * @param ctx          Context.
 * @param test_fn      Overlap test callback.
 * @param test_ctx     Opaque context passed to test_fn.
 * @param pairs        Array of candidate body pairs.
 * @param pair_count   Number of candidate pairs.
 * @param current_tick Current simulation tick.
 */
void phys_overlap_begin_update(phys_overlap_begin_ctx_t *ctx,
                               phys_overlap_test_fn test_fn,
                               void *test_ctx,
                               const phys_overlap_pair_t *pairs,
                               uint32_t pair_count,
                               uint32_t current_tick);

/**
 * @brief Return number of overlap-begin events from the last update.
 * @param ctx Context (may be NULL → returns 0).
 * @return Number of events.
 */
uint32_t phys_overlap_begin_count(const phys_overlap_begin_ctx_t *ctx);

/**
 * @brief Return pointer to the overlap-begin event array.
 * @param ctx Context (may be NULL → returns NULL).
 * @return Pointer to event array (valid until next update).
 */
const phys_overlap_begin_event_t *phys_overlap_begin_events(
    const phys_overlap_begin_ctx_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_PHYSICS_OVERLAP_BEGIN_H */
