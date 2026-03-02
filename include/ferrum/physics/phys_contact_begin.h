/**
 * @file phys_contact_begin.h
 * @brief Contact-begin detection from the manifold cache.
 *
 * Tracks which body pairs had active contacts on the previous tick.
 * After each physics tick, call phys_contact_begin_update() with the
 * settled manifold cache to detect new contact-begin events.
 *
 * Thread safety: none — intended for single-thread use (physics thread
 * post-tick callback or main thread between ticks).
 *
 * Ownership: caller owns the context. init allocates, destroy frees.
 */
#ifndef FERRUM_PHYSICS_PHYS_CONTACT_BEGIN_H
#define FERRUM_PHYSICS_PHYS_CONTACT_BEGIN_H

#include <stdbool.h>
#include <stdint.h>
#include "ferrum/physics/phys_pair_set.h"
#include "ferrum/physics/phys_vec3.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declaration — defined in manifold_cache.h. */
struct phys_manifold_cache;

/* ── Types ────────────────────────────────────────────────────── */

/**
 * @brief Output event for a newly detected collision contact.
 */
typedef struct phys_contact_begin_event {
    uint32_t    body_a;   /**< Index of body A (lower). */
    uint32_t    body_b;   /**< Index of body B (higher). */
    phys_vec3_t point;    /**< World-space contact point (first point). */
    phys_vec3_t normal;   /**< Contact normal (A → B). */
    float       impulse;  /**< Normal impulse magnitude at first point. */
} phys_contact_begin_event_t;

/**
 * @brief Context for contact-begin detection.
 *
 * Contains the pair tracking set and an output event buffer.
 */
typedef struct phys_contact_begin_ctx {
    phys_pair_set_t             pair_set;       /**< Tracks seen pairs. */
    phys_contact_begin_event_t *events;         /**< Output event buffer. Owned. */
    uint32_t                    event_count;    /**< Events produced this tick. */
    uint32_t                    event_capacity; /**< Max events per tick. */
} phys_contact_begin_ctx_t;

/* ── API ──────────────────────────────────────────────────────── */

/**
 * @brief Initialize the contact-begin tracker.
 *
 * @param ctx            Context to initialize. Must not be NULL.
 * @param pair_capacity  Capacity of the pair tracking set (rounded to pow2).
 * @param event_capacity Max contact-begin events per tick.
 * @return true on success, false on allocation failure.
 *
 * Side effects: allocates pair set and event buffer.
 * Ownership: caller must call phys_contact_begin_destroy().
 */
bool phys_contact_begin_init(phys_contact_begin_ctx_t *ctx,
                             uint32_t pair_capacity,
                             uint32_t event_capacity);

/**
 * @brief Destroy the contact-begin tracker and free memory.
 * @param ctx Context. May be NULL (no-op).
 */
void phys_contact_begin_destroy(phys_contact_begin_ctx_t *ctx);

/**
 * @brief Detect new contact-begin events from the manifold cache.
 *
 * Iterates all active entries in the cache (last_used_tick == current_tick).
 * For each pair not previously tracked, emits a contact_begin event.
 * Prunes pairs not seen this tick from the tracking set.
 *
 * After this call, use phys_contact_begin_count() and
 * phys_contact_begin_events() to read the results.
 *
 * @param ctx          Context. Must not be NULL.
 * @param cache        Settled manifold cache (read-only). Must not be NULL.
 * @param current_tick Current physics tick number.
 *
 * Side effects: modifies ctx->pair_set and ctx->events.
 */
void phys_contact_begin_update(phys_contact_begin_ctx_t *ctx,
                               const struct phys_manifold_cache *cache,
                               uint32_t current_tick);

/**
 * @brief Return the number of contact-begin events from the last update.
 * @param ctx Context. Must not be NULL.
 * @return Event count (0 if no new contacts).
 */
uint32_t phys_contact_begin_count(const phys_contact_begin_ctx_t *ctx);

/**
 * @brief Return pointer to the contact-begin event array.
 *
 * Valid until the next call to phys_contact_begin_update().
 *
 * @param ctx Context. Must not be NULL.
 * @return Pointer to events array (may be NULL if count == 0).
 */
const phys_contact_begin_event_t *phys_contact_begin_events(
    const phys_contact_begin_ctx_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_PHYSICS_PHYS_CONTACT_BEGIN_H */
