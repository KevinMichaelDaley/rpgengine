/**
 * @file interest.c
 * @brief Interest management query with spatial filtering and budgeting.
 *
 * Non-static functions: 1 (net_interest_query).
 *
 * Uses a simple insertion sort on a scratch candidate list.  This is
 * O(n²) but fine for the expected entity counts (hundreds).  The
 * candidate list is stack-allocated with a hard cap of 256 entries;
 * if more candidates pass the filter, the farthest are discarded.
 */

#include "ferrum/net/interest.h"
#include <math.h>
#include <string.h>

/** Max candidates we sort (stack-allocated). */
#define MAX_CANDIDATES 256

/** Candidate: entity index + squared distance for sorting. */
typedef struct {
    uint32_t entity_idx;
    uint16_t entity_id;
    float dist_sq;
    uint32_t serialized_size;
} candidate_t;

int net_interest_query(const net_interest_entity_t *entities,
                       uint32_t entity_count,
                       const float viewpoint[3],
                       const net_interest_config_t *config,
                       net_interest_result_t *result) {
    if (!viewpoint || !config || !result || !result->entity_ids) {
        return NET_INTEREST_ERR_INVALID;
    }

    result->count = 0;
    result->total_bytes = 0;

    if (entity_count == 0 || !entities) {
        return NET_INTEREST_OK;
    }

    float radius_sq = config->radius * config->radius;

    /* Pass 1: filter by radius + dirty, collect candidates. */
    candidate_t candidates[MAX_CANDIDATES];
    uint32_t n_cand = 0;

    for (uint32_t i = 0; i < entity_count; i++) {
        if (!entities[i].dirty) { continue; }

        float dx = entities[i].pos[0] - viewpoint[0];
        float dy = entities[i].pos[1] - viewpoint[1];
        float dz = entities[i].pos[2] - viewpoint[2];
        float dsq = dx * dx + dy * dy + dz * dz;

        if (dsq > radius_sq) { continue; }

        if (n_cand < MAX_CANDIDATES) {
            candidates[n_cand].entity_idx = i;
            candidates[n_cand].entity_id = entities[i].entity_id;
            candidates[n_cand].dist_sq = dsq;
            candidates[n_cand].serialized_size = entities[i].serialized_size;
            n_cand++;
        }
    }

    /* Pass 2: insertion sort by (dist_sq, entity_id) for determinism. */
    for (uint32_t i = 1; i < n_cand; i++) {
        candidate_t key = candidates[i];
        uint32_t j = i;
        while (j > 0 &&
               (candidates[j - 1].dist_sq > key.dist_sq ||
                (candidates[j - 1].dist_sq == key.dist_sq &&
                 candidates[j - 1].entity_id > key.entity_id))) {
            candidates[j] = candidates[j - 1];
            j--;
        }
        candidates[j] = key;
    }

    /* Pass 3: budget-constrained selection. */
    uint32_t bytes_used = 0;
    for (uint32_t i = 0; i < n_cand; i++) {
        if (bytes_used + candidates[i].serialized_size > config->budget_bytes) {
            break;
        }
        if (result->count >= result->capacity) { break; }

        result->entity_ids[result->count++] = candidates[i].entity_id;
        bytes_used += candidates[i].serialized_size;
    }

    result->total_bytes = bytes_used;
    return NET_INTEREST_OK;
}
