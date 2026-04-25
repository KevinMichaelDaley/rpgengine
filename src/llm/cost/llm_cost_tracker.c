/**
 * @file llm_cost_tracker.c
 * @brief Atomic cost tracker implementation.
 */

#include "ferrum/llm/llm_cost_tracker.h"

#include <string.h>

void llm_cost_tracker_init(llm_cost_tracker_t *ct) {
    if (!ct) return;
    atomic_store(&ct->total_usd, 0.0f);
}

float llm_cost_tracker_add(llm_cost_tracker_t *ct, float cost_usd) {
    if (!ct) return 0.0f;
    float expected = atomic_load(&ct->total_usd);
    float desired;
    do {
        desired = expected + cost_usd;
    } while (!atomic_compare_exchange_weak(&ct->total_usd, &expected, desired));
    return desired;
}

float llm_cost_tracker_get(const llm_cost_tracker_t *ct) {
    if (!ct) return 0.0f;
    return atomic_load(&ct->total_usd);
}
