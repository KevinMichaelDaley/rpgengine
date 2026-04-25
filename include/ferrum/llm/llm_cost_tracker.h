/**
 * @file llm_cost_tracker.h
 * @brief Thread-safe atomic cost tracker for LLM prompting.
 *
 * Tracks cumulative cost in USD across all LLM calls. Used by the
 * executor to enforce per-engine budget limits.
 */
#ifndef FERRUM_LLM_COST_TRACKER_H
#define FERRUM_LLM_COST_TRACKER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdatomic.h>
#include <stdint.h>

/* ── Cost tracker ──────────────────────────────────────────────── */

/**
 * @brief Thread-safe atomic cost tracker.
 *
 * Stores cumulative cost as atomic float. All operations are
 * lock-free (C11 atomic float on most platforms).
 */
typedef struct llm_cost_tracker {
    _Atomic float total_usd; /**< Cumulative cost since init. */
} llm_cost_tracker_t;

/* ── Lifecycle ─────────────────────────────────────────────────── */

/**
 * @brief Initialize tracker to zero.
 */
void llm_cost_tracker_init(llm_cost_tracker_t *ct);

/* ── Operations ────────────────────────────────────────────────── */

/**
 * @brief Atomically add cost to the tracker.
 * @return The new total cost after addition.
 */
float llm_cost_tracker_add(llm_cost_tracker_t *ct, float cost_usd);

/**
 * @brief Atomically read the current total cost.
 */
float llm_cost_tracker_get(const llm_cost_tracker_t *ct);

/**
 * @brief Compute estimated cost from token counts and pricing.
 */
float llm_cost_compute(int32_t input_tokens, int32_t output_tokens,
                       float input_cost_per_1k, float output_cost_per_1k);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_LLM_COST_TRACKER_H */
