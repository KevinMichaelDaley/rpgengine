/**
 * @file manifold_cache_ops.c
 * @brief Manifold cache secondary operations: expire, touch, count.
 */

#include "ferrum/physics/manifold_cache.h"

#include <string.h>

/* ── Static helpers ─────────────────────────────────────────────── */

/**
 * Build a canonical pair key with the smaller body ID in the high bits.
 */
static uint64_t make_pair_key(uint32_t a, uint32_t b)
{
    uint32_t lo = (a < b) ? a : b;
    uint32_t hi = (a < b) ? b : a;
    return ((uint64_t)lo << 32) | (uint64_t)hi;
}

/**
 * Hash a 64-bit pair key (same algorithm as manifold_cache.c).
 */
static uint32_t hash_pair_key(uint64_t key)
{
    key ^= key >> 30;
    key *= 0xbf58476d1ce4e5b9ULL;
    key ^= key >> 27;
    key *= 0x94d049bb133111ebULL;
    key ^= key >> 31;
    return (uint32_t)key;
}

/**
 * Remove an entry from the hash table by its pair_key.
 *
 * After removal, performs backward-shift deletion to maintain the
 * linear-probing invariant (entries following the deleted slot are
 * re-probed if they would have been unreachable).
 */
static void hash_remove(phys_manifold_cache_t *cache, uint64_t pair_key)
{
    uint32_t slot = hash_pair_key(pair_key) & cache->hash_mask;

    /* Find the slot containing this key. */
    for (uint32_t i = 0; i < cache->hash_size; i++) {
        uint32_t idx = cache->hash_table[slot];
        if (idx == PHYS_CACHE_INVALID_INDEX) return; /* not found */
        if (cache->entries[idx].pair_key == pair_key) {
            /* Found — remove and repair the probe chain. */
            cache->hash_table[slot] = PHYS_CACHE_INVALID_INDEX;

            /* Backward-shift deletion: re-insert subsequent entries
             * that are part of the same probe chain. */
            uint32_t next = (slot + 1) & cache->hash_mask;
            while (cache->hash_table[next] != PHYS_CACHE_INVALID_INDEX) {
                uint32_t entry_idx = cache->hash_table[next];
                uint64_t k = cache->entries[entry_idx].pair_key;
                uint32_t natural = hash_pair_key(k) & cache->hash_mask;

                /* Check whether 'next' is displaced past 'slot'. If
                 * the natural home of this entry is at or before the
                 * gap we created, it must be moved back. We detect
                 * this with a circular comparison. */
                bool displaced;
                if (next >= slot) {
                    displaced = (natural <= slot) || (natural > next);
                } else {
                    /* slot is near the end, next wrapped around. */
                    displaced = (natural <= slot) && (natural > next);
                }

                if (displaced) {
                    cache->hash_table[slot] = entry_idx;
                    cache->hash_table[next] = PHYS_CACHE_INVALID_INDEX;
                    slot = next;
                }
                next = (next + 1) & cache->hash_mask;
            }
            return;
        }
        slot = (slot + 1) & cache->hash_mask;
    }
}

/**
 * Update the index stored in the hash table for a given pair_key.
 */
static void hash_update_index(phys_manifold_cache_t *cache,
                              uint64_t pair_key, uint32_t new_index)
{
    uint32_t slot = hash_pair_key(pair_key) & cache->hash_mask;
    for (uint32_t i = 0; i < cache->hash_size; i++) {
        uint32_t idx = cache->hash_table[slot];
        if (idx == PHYS_CACHE_INVALID_INDEX) return;
        if (cache->entries[idx].pair_key == pair_key) {
            cache->hash_table[slot] = new_index;
            return;
        }
        slot = (slot + 1) & cache->hash_mask;
    }
}

/* ── Public API (3 non-static functions) ────────────────────────── */

void phys_manifold_cache_expire(phys_manifold_cache_t *cache,
                                uint32_t current_tick, uint32_t max_age)
{
    if (!cache) return;

    uint32_t i = 0;
    while (i < cache->count) {
        phys_manifold_cache_entry_t *entry = &cache->entries[i];
        uint32_t age = current_tick - entry->last_used_tick;

        if (age > max_age) {
            /* Remove from hash table. */
            hash_remove(cache, entry->pair_key);

            /* Swap with the last entry to compact the dense array. */
            uint32_t last = cache->count - 1;
            if (i != last) {
                /* Update the hash table to point the swapped entry
                 * to its new index before moving it. */
                hash_update_index(cache, cache->entries[last].pair_key, i);
                cache->entries[i] = cache->entries[last];
            }
            cache->count--;
            /* Don't increment i — re-examine the swapped entry. */
        } else {
            i++;
        }
    }
}

void phys_manifold_cache_touch(phys_manifold_cache_t *cache,
                               uint32_t body_a, uint32_t body_b,
                               uint32_t tick)
{
    if (!cache) return;

    uint64_t key = make_pair_key(body_a, body_b);
    uint32_t slot = hash_pair_key(key) & cache->hash_mask;

    for (uint32_t i = 0; i < cache->hash_size; i++) {
        uint32_t idx = cache->hash_table[slot];
        if (idx == PHYS_CACHE_INVALID_INDEX) return;
        if (cache->entries[idx].pair_key == key) {
            cache->entries[idx].last_used_tick = tick;
            return;
        }
        slot = (slot + 1) & cache->hash_mask;
    }
}

uint32_t phys_manifold_cache_count(const phys_manifold_cache_t *cache)
{
    if (!cache) return 0;
    return cache->count;
}
