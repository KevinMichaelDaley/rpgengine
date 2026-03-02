/**
 * @file phys_pair_set.c
 * @brief Core operations for the body pair hash set.
 *
 * Implements init, destroy, upsert, contains.
 * Uses open addressing with linear probing.
 */
#include "ferrum/physics/phys_pair_set.h"
#include <stdlib.h>
#include <string.h>

/* ── Internal helpers ─────────────────────────────────────────── */

/** Round up to the next power of 2. */
static uint32_t next_pow2(uint32_t v) {
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    v++;
    return v;
}

/** Hash a 64-bit pair key to a table slot. */
static uint32_t hash_key(uint64_t key, uint32_t mask) {
    /* Murmur-style finalizer. */
    key ^= key >> 33;
    key *= 0xff51afd7ed558ccdULL;
    key ^= key >> 33;
    key *= 0xc4ceb9fe1a85ec53ULL;
    key ^= key >> 33;
    return (uint32_t)(key & mask);
}

/* ── Public API ───────────────────────────────────────────────── */

bool phys_pair_set_init(phys_pair_set_t *set, uint32_t capacity) {
    if (!set || capacity == 0) return false;

    capacity = next_pow2(capacity);
    set->entries = (phys_pair_entry_t *)calloc(capacity, sizeof(phys_pair_entry_t));
    if (!set->entries) return false;

    set->capacity = capacity;
    set->count    = 0;
    set->mask     = capacity - 1;
    return true;
}

void phys_pair_set_destroy(phys_pair_set_t *set) {
    if (!set) return;
    free(set->entries);
    set->entries  = NULL;
    set->capacity = 0;
    set->count    = 0;
    set->mask     = 0;
}

bool phys_pair_set_upsert(phys_pair_set_t *set, uint64_t pair_key,
                           uint32_t tick) {
    if (!set || !set->entries) return false;
    if (set->count >= set->capacity) return false;

    uint32_t idx = hash_key(pair_key, set->mask);
    for (uint32_t i = 0; i < set->capacity; i++) {
        phys_pair_entry_t *e = &set->entries[idx];
        if (!e->occupied) {
            /* Empty slot — insert. */
            e->pair_key  = pair_key;
            e->last_tick = tick;
            e->occupied  = 1;
            set->count++;
            return true; /* was_new */
        }
        if (e->pair_key == pair_key) {
            /* Existing — update tick. */
            e->last_tick = tick;
            return false; /* not new */
        }
        idx = (idx + 1) & set->mask;
    }
    return false; /* table full (shouldn't happen if count < capacity) */
}

bool phys_pair_set_contains(const phys_pair_set_t *set, uint64_t pair_key) {
    if (!set || !set->entries) return false;

    uint32_t idx = hash_key(pair_key, set->mask);
    for (uint32_t i = 0; i < set->capacity; i++) {
        const phys_pair_entry_t *e = &set->entries[idx];
        if (!e->occupied) return false;
        if (e->pair_key == pair_key) return true;
        idx = (idx + 1) & set->mask;
    }
    return false;
}
