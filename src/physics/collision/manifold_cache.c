/**
 * @file manifold_cache.c
 * @brief Core manifold cache operations: init, destroy, find, get_or_create.
 */

#include "ferrum/physics/manifold_cache.h"

#include <stdlib.h>
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
 * Hash a 64-bit pair key using a mixing function (splitmix-style).
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
 * Find the hash table slot holding the entry for @p pair_key.
 *
 * @return The index into cache->entries, or PHYS_CACHE_INVALID_INDEX
 *         if not found.
 */
static uint32_t find_entry_index(const phys_manifold_cache_t *cache,
                                 uint64_t pair_key)
{
    uint32_t slot = hash_pair_key(pair_key) & cache->hash_mask;

    for (uint32_t i = 0; i < cache->hash_size; i++) {
        uint32_t idx = cache->hash_table[slot];
        if (idx == PHYS_CACHE_INVALID_INDEX) {
            return PHYS_CACHE_INVALID_INDEX; /* empty slot → not found */
        }
        if (cache->entries[idx].pair_key == pair_key) {
            return idx;
        }
        slot = (slot + 1) & cache->hash_mask;
    }
    return PHYS_CACHE_INVALID_INDEX;
}

/**
 * Return the smallest power of 2 that is >= @p n.
 */
static uint32_t next_pow2(uint32_t n)
{
    if (n == 0) return 1;
    n--;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    return n + 1;
}

/* ── Public API (4 non-static functions) ────────────────────────── */

int phys_manifold_cache_init(phys_manifold_cache_t *cache, uint32_t capacity)
{
    if (!cache) return -1;
    if (capacity == 0) capacity = 1;

    memset(cache, 0, sizeof(*cache));
    cache->capacity = capacity;

    /* Allocate dense entry array (zeroed). */
    cache->entries = calloc(capacity, sizeof(phys_manifold_cache_entry_t));
    if (!cache->entries) return -1;

    /* Hash table: next power of 2 >= capacity * 2 for ~50% load. */
    cache->hash_size = next_pow2(capacity * 2);
    cache->hash_mask = cache->hash_size - 1;
    cache->hash_table = malloc(cache->hash_size * sizeof(uint32_t));
    if (!cache->hash_table) {
        free(cache->entries);
        cache->entries = NULL;
        return -1;
    }

    /* Fill hash table with sentinel values. */
    memset(cache->hash_table, 0xFF,
           cache->hash_size * sizeof(uint32_t));

    return 0;
}

void phys_manifold_cache_destroy(phys_manifold_cache_t *cache)
{
    if (!cache) return;
    free(cache->entries);
    free(cache->hash_table);
    memset(cache, 0, sizeof(*cache));
}

phys_manifold_t *phys_manifold_cache_find(phys_manifold_cache_t *cache,
                                          uint32_t body_a, uint32_t body_b)
{
    if (!cache) return NULL;

    uint64_t key = make_pair_key(body_a, body_b);
    uint32_t idx = find_entry_index(cache, key);
    if (idx == PHYS_CACHE_INVALID_INDEX) return NULL;
    return &cache->entries[idx].manifold;
}

phys_manifold_t *phys_manifold_cache_get_or_create(phys_manifold_cache_t *cache,
                                                   uint32_t body_a,
                                                   uint32_t body_b,
                                                   uint32_t tick)
{
    if (!cache) return NULL;

    uint64_t key = make_pair_key(body_a, body_b);

    /* Try to find existing entry. */
    uint32_t idx = find_entry_index(cache, key);
    if (idx != PHYS_CACHE_INVALID_INDEX) {
        cache->entries[idx].last_used_tick = tick;
        return &cache->entries[idx].manifold;
    }

    /* Cache full — cannot insert. */
    if (cache->count >= cache->capacity) return NULL;

    /* Allocate a new entry at the end of the dense array. */
    idx = cache->count;
    cache->count++;

    phys_manifold_cache_entry_t *entry = &cache->entries[idx];
    entry->pair_key = key;
    entry->last_used_tick = tick;
    entry->flags = 0;

    /* Normalize body order for the manifold (smaller first). */
    uint32_t lo = (body_a < body_b) ? body_a : body_b;
    uint32_t hi = (body_a < body_b) ? body_b : body_a;
    phys_manifold_init(&entry->manifold, lo, hi);

    /* Insert into hash table via linear probing. */
    uint32_t slot = hash_pair_key(key) & cache->hash_mask;
    while (cache->hash_table[slot] != PHYS_CACHE_INVALID_INDEX) {
        slot = (slot + 1) & cache->hash_mask;
    }
    cache->hash_table[slot] = idx;

    return &entry->manifold;
}
