/**
 * @file island.c
 * @brief Island list initialization, clearing, and building from constraints.
 */

#include <string.h>

#include "ferrum/physics/island.h"
#include "ferrum/physics/constraint.h"
#include "ferrum/physics/phys_pool.h"

/* ── Static helpers ────────────────────────────────────────────── */

/**
 * @brief Reset union-find arrays so each element is its own root.
 */
static void reset_union_find(phys_island_list_t *list, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        list->parent[i] = i;
        list->rank[i] = 0;
    }
}

/* ── phys_island_list_init ─────────────────────────────────────── */

void phys_island_list_init(phys_island_list_t *list,
                           struct phys_frame_arena *arena,
                           uint32_t max_bodies,
                           uint32_t max_islands) {
    if (!list || !arena) {
        return;
    }
    memset(list, 0, sizeof(*list));

    list->islands = phys_frame_arena_alloc(
        (phys_frame_arena_t *)arena,
        max_islands * sizeof(phys_island_t),
        _Alignof(phys_island_t));

    list->parent = phys_frame_arena_alloc(
        (phys_frame_arena_t *)arena,
        max_bodies * sizeof(uint32_t),
        _Alignof(uint32_t));

    list->rank = phys_frame_arena_alloc(
        (phys_frame_arena_t *)arena,
        max_bodies * sizeof(uint32_t),
        _Alignof(uint32_t));

    if (!list->islands || !list->parent || !list->rank) {
        /* Arena ran out of space; leave list zeroed. */
        memset(list, 0, sizeof(*list));
        return;
    }

    list->capacity = max_islands;
    list->uf_size = max_bodies;
    list->count = 0;

    reset_union_find(list, max_bodies);
}

/* ── phys_island_list_clear ────────────────────────────────────── */

void phys_island_list_clear(phys_island_list_t *list) {
    if (!list) {
        return;
    }
    list->count = 0;
    if (list->parent && list->rank) {
        reset_union_find(list, list->uf_size);
    }
}

/* ── phys_island_list_build ────────────────────────────────────── */

void phys_island_list_build(phys_island_list_t *list,
                            const struct phys_constraint *constraints,
                            uint32_t constraint_count,
                            uint32_t body_count,
                            struct phys_frame_arena *arena) {
    if (!list || !arena) {
        return;
    }

    list->count = 0;

    if (constraint_count == 0 || !constraints || body_count == 0) {
        return;
    }

    /* Step 1: Re-init union-find for current body_count. */
    uint32_t uf_count = (body_count < list->uf_size) ? body_count : list->uf_size;
    reset_union_find(list, uf_count);

    /* Step 2: Union all constraint pairs. */
    for (uint32_t i = 0; i < constraint_count; i++) {
        uint32_t a = constraints[i].body_a;
        uint32_t b = constraints[i].body_b;
        if (a < uf_count && b < uf_count) {
            phys_uf_union(list, a, b);
        }
    }

    /* Step 3: Map roots to island indices.
     * root_to_island[root] gives the island index for that root.
     * Initialize to UINT32_MAX meaning "not yet assigned". */
    uint32_t *root_to_island = phys_frame_arena_alloc(
        (phys_frame_arena_t *)arena,
        body_count * sizeof(uint32_t),
        _Alignof(uint32_t));
    if (!root_to_island) {
        return;
    }
    for (uint32_t i = 0; i < body_count; i++) {
        root_to_island[i] = UINT32_MAX;
    }

    /* Count bodies per island using a temp array. */
    uint32_t *body_counts = phys_frame_arena_alloc(
        (phys_frame_arena_t *)arena,
        list->capacity * sizeof(uint32_t),
        _Alignof(uint32_t));
    uint32_t *constraint_counts = phys_frame_arena_alloc(
        (phys_frame_arena_t *)arena,
        list->capacity * sizeof(uint32_t),
        _Alignof(uint32_t));
    if (!body_counts || !constraint_counts) {
        return;
    }
    memset(body_counts, 0, list->capacity * sizeof(uint32_t));
    memset(constraint_counts, 0, list->capacity * sizeof(uint32_t));

    /* Track which bodies are connected (appear in constraints). */
    bool *connected = phys_frame_arena_alloc(
        (phys_frame_arena_t *)arena,
        body_count * sizeof(bool),
        _Alignof(bool));
    if (!connected) {
        return;
    }
    memset(connected, 0, body_count * sizeof(bool));

    for (uint32_t i = 0; i < constraint_count; i++) {
        if (constraints[i].body_a < body_count) {
            connected[constraints[i].body_a] = true;
        }
        if (constraints[i].body_b < body_count) {
            connected[constraints[i].body_b] = true;
        }
    }

    /* First pass: assign island indices to roots, count bodies per island. */
    uint32_t island_count = 0;
    for (uint32_t i = 0; i < body_count && island_count < list->capacity; i++) {
        if (!connected[i]) {
            continue;
        }
        uint32_t root = phys_uf_find(list, i);
        if (root_to_island[root] == UINT32_MAX) {
            root_to_island[root] = island_count;
            island_count++;
        }
        body_counts[root_to_island[root]]++;
    }

    /* Count constraints per island. */
    for (uint32_t i = 0; i < constraint_count; i++) {
        uint32_t root = phys_uf_find(list, constraints[i].body_a);
        if (root < body_count && root_to_island[root] != UINT32_MAX) {
            uint32_t idx = root_to_island[root];
            if (idx < island_count) {
                constraint_counts[idx]++;
            }
        }
    }

    /* Step 4: Allocate per-island arrays and initialize islands. */
    for (uint32_t i = 0; i < island_count; i++) {
        phys_island_t *island = &list->islands[i];
        island->body_count = 0; /* Will fill in second pass. */
        island->constraint_count = 0;
        island->sleeping = false;
        island->skip     = false;

        island->body_indices = phys_frame_arena_alloc(
            (phys_frame_arena_t *)arena,
            body_counts[i] * sizeof(uint32_t),
            _Alignof(uint32_t));
        if (!island->body_indices) { return; }

        island->constraint_indices = phys_frame_arena_alloc(
            (phys_frame_arena_t *)arena,
            constraint_counts[i] * sizeof(uint32_t),
            _Alignof(uint32_t));
        if (!island->constraint_indices) { return; }
    }

    /* Step 5: Second pass — fill body indices. */
    for (uint32_t i = 0; i < body_count; i++) {
        if (!connected[i]) {
            continue;
        }
        uint32_t root = phys_uf_find(list, i);
        uint32_t idx = root_to_island[root];
        if (idx < island_count) {
            phys_island_t *island = &list->islands[idx];
            if (island->body_indices) {
                island->body_indices[island->body_count] = i;
            }
            island->body_count++;
        }
    }

    /* Step 6: Fill constraint indices. */
    for (uint32_t i = 0; i < constraint_count; i++) {
        uint32_t root = phys_uf_find(list, constraints[i].body_a);
        if (root < body_count && root_to_island[root] != UINT32_MAX) {
            uint32_t idx = root_to_island[root];
            if (idx < island_count) {
                phys_island_t *island = &list->islands[idx];
                if (island->constraint_indices) {
                    island->constraint_indices[island->constraint_count] = i;
                }
                island->constraint_count++;
            }
        }
    }

    list->count = island_count;
}
