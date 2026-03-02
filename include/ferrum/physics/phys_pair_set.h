/**
 * @file phys_pair_set.h
 * @brief Open-addressing hash set for tracking body pairs.
 *
 * Used by contact-begin and overlap-begin detectors to track which
 * body pairs were active on the previous tick.
 *
 * Keys are canonical pair keys: (min(a,b) << 32) | max(a,b).
 * Each entry stores a last_tick value for staleness pruning.
 *
 * Ownership: caller owns the set. init allocates, destroy frees.
 * Thread safety: none — caller must synchronize externally.
 */
#ifndef FERRUM_PHYSICS_PHYS_PAIR_SET_H
#define FERRUM_PHYSICS_PHYS_PAIR_SET_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Types ────────────────────────────────────────────────────── */

/**
 * @brief Entry in the pair set hash table.
 *
 * Sentinel: pair_key == 0 AND occupied == 0 means empty slot.
 */
typedef struct phys_pair_entry {
    uint64_t pair_key;    /**< Canonical key: (min_body << 32) | max_body. */
    uint32_t last_tick;   /**< Last tick this pair was seen. */
    uint8_t  occupied;    /**< 1 = occupied, 0 = empty slot. */
    uint8_t  pad_[3];     /**< Padding. */
} phys_pair_entry_t;

/**
 * @brief Open-addressing hash set for body pairs.
 *
 * Capacity must be a power of 2 (init rounds up if needed).
 * Uses linear probing.
 */
typedef struct phys_pair_set {
    phys_pair_entry_t *entries; /**< Flat array of slots. Owned. */
    uint32_t capacity;          /**< Total slots (power of 2). */
    uint32_t count;             /**< Active (occupied) entries. */
    uint32_t mask;              /**< capacity - 1, for fast modulo. */
} phys_pair_set_t;

/* ── Core API (phys_pair_set.c) ───────────────────────────────── */

/**
 * @brief Initialize the pair set with the given capacity.
 *
 * Capacity is rounded up to the next power of 2 if needed.
 *
 * @param set      Set to initialize. Must not be NULL.
 * @param capacity Desired capacity (> 0).
 * @return true on success, false on allocation failure or capacity == 0.
 *
 * Side effects: allocates entries array via calloc.
 * Ownership: caller must call phys_pair_set_destroy().
 */
bool phys_pair_set_init(phys_pair_set_t *set, uint32_t capacity);

/**
 * @brief Destroy the pair set and free memory.
 *
 * @param set Set to destroy. May be NULL (no-op).
 */
void phys_pair_set_destroy(phys_pair_set_t *set);

/**
 * @brief Insert or update a pair. Returns true if the pair is new.
 *
 * If the pair already exists, updates last_tick and returns false.
 * If the table is full, returns false without inserting.
 *
 * @param set      Set. Must not be NULL.
 * @param pair_key Canonical pair key.
 * @param tick     Current tick number.
 * @return true if newly inserted, false if updated or table full.
 */
bool phys_pair_set_upsert(phys_pair_set_t *set, uint64_t pair_key,
                           uint32_t tick);

/**
 * @brief Check if a pair key is in the set.
 *
 * @param set      Set. Must not be NULL.
 * @param pair_key Canonical pair key.
 * @return true if found.
 */
bool phys_pair_set_contains(const phys_pair_set_t *set, uint64_t pair_key);

/* ── Maintenance API (phys_pair_set_gc.c) ─────────────────────── */

/**
 * @brief Remove all entries from the set.
 * @param set Set. Must not be NULL.
 */
void phys_pair_set_clear(phys_pair_set_t *set);

/**
 * @brief Remove entries with last_tick < min_tick.
 *
 * After pruning, the hash table is compacted (re-inserted)
 * to fix probe chains broken by removal.
 *
 * @param set      Set. Must not be NULL.
 * @param min_tick Entries with last_tick < min_tick are removed.
 */
void phys_pair_set_prune_before(phys_pair_set_t *set, uint32_t min_tick);

/**
 * @brief Return the number of active entries.
 * @param set Set. Must not be NULL.
 * @return Active entry count.
 */
uint32_t phys_pair_set_count(const phys_pair_set_t *set);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_PHYSICS_PHYS_PAIR_SET_H */
