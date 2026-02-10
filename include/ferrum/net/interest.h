/**
 * @file interest.h
 * @brief Interest management and per-tick bandwidth budgeting.
 *
 * Filters which entities a client receives based on spatial proximity
 * and dirty state, then selects the highest-priority subset that fits
 * within a per-tick byte budget.
 *
 * Priority policy: closer entities rank higher (lower distance = higher
 * priority).  Ties are broken by entity_id for determinism.
 *
 * Ownership: all arrays are caller-provided.  No dynamic allocation.
 * NULL-safe: all public functions check for NULL inputs.
 */

#ifndef FERRUM_NET_INTEREST_H
#define FERRUM_NET_INTEREST_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Status codes ──────────────────────────────────────────────── */

#define NET_INTEREST_OK           0
#define NET_INTEREST_ERR_INVALID -1

/* ── Types ─────────────────────────────────────────────────────── */

/**
 * @brief Entity descriptor for interest queries.
 *
 * Provides the spatial position, dirty flag, and estimated wire size
 * for one entity.  The caller populates an array of these each tick.
 */
typedef struct net_interest_entity {
    uint16_t entity_id;       /**< Unique entity identifier. */
    float pos[3];             /**< World-space position (x, y, z). */
    uint32_t serialized_size; /**< Estimated bytes if sent this tick. */
    uint8_t dirty;            /**< Non-zero if entity has changed. */
} net_interest_entity_t;

/**
 * @brief Configuration for an interest query.
 */
typedef struct net_interest_config {
    float radius;             /**< Interest radius (world units). */
    uint32_t budget_bytes;    /**< Max bytes to send this tick. */
} net_interest_config_t;

/**
 * @brief Result of an interest query: selected entity IDs.
 *
 * Entities are sorted by priority (closest first).
 * Caller provides the entity_ids array.
 */
typedef struct net_interest_result {
    uint16_t *entity_ids;     /**< Caller-owned output array. */
    uint32_t capacity;        /**< Max entries in entity_ids. */
    uint32_t count;           /**< Entries written. */
    uint32_t total_bytes;     /**< Sum of serialized_size for selected. */
} net_interest_result_t;

/* ── Public API ────────────────────────────────────────────────── */

/**
 * @brief Query which entities should be replicated this tick.
 *
 * Filters by: within radius, dirty flag set.
 * Sorts by: distance ascending (ties broken by entity_id).
 * Budgets by: accumulating serialized_size until budget_bytes exceeded.
 *
 * @param entities    Array of entity descriptors (may be NULL if count is 0).
 * @param entity_count Number of entities.
 * @param viewpoint   Observer position [3] (non-NULL).
 * @param config      Interest config (non-NULL).
 * @param result      Output result (non-NULL, entity_ids pre-allocated).
 * @return NET_INTEREST_OK on success,
 *         NET_INTEREST_ERR_INVALID if required args are NULL.
 *
 * Side effects: writes result->entity_ids and result->count/total_bytes.
 */
int net_interest_query(const net_interest_entity_t *entities,
                       uint32_t entity_count,
                       const float viewpoint[3],
                       const net_interest_config_t *config,
                       net_interest_result_t *result);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_NET_INTEREST_H */
