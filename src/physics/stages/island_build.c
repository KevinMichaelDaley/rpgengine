/**
 * @file island_build.c
 * @brief Stage 10: Island Build — groups connected bodies into islands
 *        using union-find with max-island-size splitting.
 *
 * Static bodies do NOT merge islands (two dynamic bodies connected
 * only through a static body remain in separate islands).  However,
 * constraints involving one static body ARE assigned to the dynamic
 * body's island so the solver can resolve them.
 *
 * When max_island_bodies > 0, a two-pass union strategy is used:
 *   Pass 1 (strong): merge edges where at least one body has
 *     significant velocity, respecting the size cap.
 *   Pass 2 (weak): merge remaining edges only if the result stays
 *     within the cap.
 * This ensures tightly-coupled active groups stay together while
 * long resting chains naturally fragment.
 */

#include "ferrum/physics/island_build.h"

#include <math.h>

#include "ferrum/physics/body.h"
#include "ferrum/physics/constraint.h"
#include "ferrum/physics/island.h"
#include "ferrum/physics/phys_pool.h"

/** Speed threshold below which a body is considered "resting" for
 *  island splitting.  Both bodies must be below this for the edge
 *  to be classified as weak. */
#define ISLAND_SPLIT_SPEED_THRESH 0.5f

/* ── Helpers for size-capped union ─────────────────────────────── */

/**
 * @brief Union x and y only if the merged component stays within cap.
 *
 * @return true if the union was performed, false if skipped.
 */
static bool capped_union(phys_island_list_t *uf,
                          uint32_t *sizes,
                          uint32_t x, uint32_t y,
                          uint32_t cap)
{
    uint32_t rx = phys_uf_find(uf, x);
    uint32_t ry = phys_uf_find(uf, y);
    if (rx == ry) { return true; } /* already same component */

    uint32_t merged = sizes[rx] + sizes[ry];
    if (merged > cap) { return false; }

    /* Perform the union and update sizes on the new root. */
    phys_uf_union(uf, x, y);
    uint32_t new_root = phys_uf_find(uf, x);
    sizes[new_root] = merged;
    return true;
}

/**
 * @brief Compute linear speed (magnitude of linear_vel).
 */
static float body_speed(const phys_body_t *b)
{
    float vx = b->linear_vel.x;
    float vy = b->linear_vel.y;
    float vz = b->linear_vel.z;
    return sqrtf(vx * vx + vy * vy + vz * vz);
}

/**
 * @brief Check if a constraint edge is "strong" — at least one body
 *        has significant velocity.
 */
static bool is_strong_edge(const phys_body_t *bodies,
                           uint32_t a, uint32_t b)
{
    return body_speed(&bodies[a]) >= ISLAND_SPLIT_SPEED_THRESH ||
           body_speed(&bodies[b]) >= ISLAND_SPLIT_SPEED_THRESH;
}

/* ── Main entry point ─────────────────────────────────────────── */

void phys_stage_island_build(const phys_island_build_args_t *args)
{
    if (!args) { return; }
    if (!args->bodies || !args->islands_out || !args->arena) { return; }
    if (args->body_count == 0) { return; }
    if (!args->constraints || args->constraint_count == 0) {
        phys_island_list_init(args->islands_out, args->arena,
                              args->body_count, args->body_count);
        return;
    }

    const uint32_t max_cap = args->max_island_bodies;
    const bool splitting = (max_cap > 0);

    /* ── Step 1: Initialize union-find ─────────────────────────── */
    phys_island_list_init(args->islands_out, args->arena,
                          args->body_count, args->body_count);

    /* Per-component size array (only needed when splitting). */
    uint32_t *sizes = NULL;
    if (splitting) {
        sizes = phys_frame_arena_alloc(
            args->arena, args->body_count * sizeof(uint32_t),
            _Alignof(uint32_t));
        if (!sizes) { return; }
        for (uint32_t i = 0; i < args->body_count; ++i) { sizes[i] = 1; }
    }

    /* ── Step 2: Union dynamic-dynamic constraint pairs ────────── */
    if (!splitting) {
        /* Original path: unconditional merge. */
        for (uint32_t i = 0; i < args->constraint_count; ++i) {
            uint32_t a = args->constraints[i].body_a;
            uint32_t b = args->constraints[i].body_b;
            if (a >= args->body_count || b >= args->body_count) { continue; }
            if (phys_body_is_static(&args->bodies[a])) { continue; }
            if (phys_body_is_static(&args->bodies[b])) { continue; }
            phys_uf_union(args->islands_out, a, b);
        }
    } else {
        /* Two-pass merge with size cap.
         *
         * Pass 0 (joints): merge joint-connected pairs unconditionally.
         * Joints are structural constraints that must never be split
         * across islands — doing so would prevent the solver from
         * maintaining the joint.
         *
         * Pass 1 (strong edges): merge pairs where at least one body
         * has significant velocity, subject to the size cap.  These
         * represent active collisions that need coupled solving.
         *
         * Pass 2 (weak edges): merge remaining pairs (both bodies
         * near rest) only if the merged island stays within cap.
         * Resting chains naturally fragment here. */

        /* Pass 0: joints — unconditional merge (ignore cap).
         * Ghost bodies (NO_BROADPHASE) are excluded: they participate
         * in joints solved via XPBD independently, not via serial
         * island solving.  This prevents skeleton ghost chains from
         * merging all bones into one giant island. */
        for (uint32_t i = 0; i < args->constraint_count; ++i) {
            if (!args->constraints[i].is_joint) { continue; }
            uint32_t a = args->constraints[i].body_a;
            uint32_t b = args->constraints[i].body_b;
            if (a >= args->body_count || b >= args->body_count) { continue; }
            if (phys_body_is_static(&args->bodies[a])) { continue; }
            if (phys_body_is_static(&args->bodies[b])) { continue; }
            /* Skip joints involving ghost bodies — solved via XPBD. */
            if (args->bodies[a].flags & PHYS_BODY_FLAG_NO_BROADPHASE) { continue; }
            if (args->bodies[b].flags & PHYS_BODY_FLAG_NO_BROADPHASE) { continue; }

            phys_uf_union(args->islands_out, a, b);
            /* Update sizes for the merged root. */
            uint32_t root = phys_uf_find(args->islands_out, a);
            uint32_t rb   = phys_uf_find(args->islands_out, b);
            if (root != rb) {
                sizes[root] += sizes[rb];
            } else {
                /* Already merged (duplicate joint pair). */
                uint32_t ra = phys_uf_find(args->islands_out, a);
                sizes[ra] = sizes[ra]; /* no-op, already correct */
            }
        }

        /* Recompute sizes after joint merges (union may have reshuffled
         * roots).  Re-derive from find to be safe. */
        for (uint32_t i = 0; i < args->body_count; ++i) { sizes[i] = 0; }
        for (uint32_t i = 0; i < args->body_count; ++i) {
            uint32_t r = phys_uf_find(args->islands_out, i);
            sizes[r]++;
        }

        /* Pass 1: strong edges first. */
        for (uint32_t i = 0; i < args->constraint_count; ++i) {
            uint32_t a = args->constraints[i].body_a;
            uint32_t b = args->constraints[i].body_b;
            if (a >= args->body_count || b >= args->body_count) { continue; }
            if (phys_body_is_static(&args->bodies[a])) { continue; }
            if (phys_body_is_static(&args->bodies[b])) { continue; }

            if (is_strong_edge(args->bodies, a, b)) {
                capped_union(args->islands_out, sizes, a, b, max_cap);
            }
        }

        /* Pass 2: weak edges (both bodies near rest). */
        for (uint32_t i = 0; i < args->constraint_count; ++i) {
            uint32_t a = args->constraints[i].body_a;
            uint32_t b = args->constraints[i].body_b;
            if (a >= args->body_count || b >= args->body_count) { continue; }
            if (phys_body_is_static(&args->bodies[a])) { continue; }
            if (phys_body_is_static(&args->bodies[b])) { continue; }

            /* Only process edges not already merged in pass 1. */
            if (phys_uf_find(args->islands_out, a) ==
                phys_uf_find(args->islands_out, b)) {
                continue;
            }

            capped_union(args->islands_out, sizes, a, b, max_cap);
        }
    }

    /* ── Step 3: Map roots → island indices ────────────────────── */
    uint32_t *root_to_island = phys_frame_arena_alloc(
        args->arena, args->body_count * sizeof(uint32_t),
        _Alignof(uint32_t));
    if (!root_to_island) { return; }
    for (uint32_t i = 0; i < args->body_count; i++) {
        root_to_island[i] = UINT32_MAX;
    }

    /* Mark which dynamic bodies participate in ANY constraint. */
    uint8_t *connected = phys_frame_arena_alloc(
        args->arena, args->body_count * sizeof(uint8_t),
        _Alignof(uint8_t));
    if (!connected) { return; }
    for (uint32_t i = 0; i < args->body_count; i++) { connected[i] = 0; }

    for (uint32_t i = 0; i < args->constraint_count; ++i) {
        uint32_t a = args->constraints[i].body_a;
        uint32_t b = args->constraints[i].body_b;
        if (a < args->body_count && !phys_body_is_static(&args->bodies[a])) {
            connected[a] = 1;
        }
        if (b < args->body_count && !phys_body_is_static(&args->bodies[b])) {
            connected[b] = 1;
        }
    }

    /* Count bodies per island and assign island indices. */
    uint32_t *body_counts = phys_frame_arena_alloc(
        args->arena, args->body_count * sizeof(uint32_t),
        _Alignof(uint32_t));
    if (!body_counts) { return; }
    for (uint32_t i = 0; i < args->body_count; i++) { body_counts[i] = 0; }

    uint32_t island_count = 0;
    for (uint32_t i = 0; i < args->body_count; ++i) {
        if (!connected[i]) { continue; }
        uint32_t root = phys_uf_find(args->islands_out, i);
        if (root_to_island[root] == UINT32_MAX) {
            root_to_island[root] = island_count++;
        }
        body_counts[root_to_island[root]]++;
    }

    if (island_count == 0) { return; }

    /* Count constraints per island.  For static-dynamic constraints,
     * assign to the dynamic body's island. */
    uint32_t *constraint_counts = phys_frame_arena_alloc(
        args->arena, island_count * sizeof(uint32_t),
        _Alignof(uint32_t));
    if (!constraint_counts) { return; }
    for (uint32_t i = 0; i < island_count; i++) { constraint_counts[i] = 0; }

    for (uint32_t i = 0; i < args->constraint_count; ++i) {
        uint32_t a = args->constraints[i].body_a;
        uint32_t b = args->constraints[i].body_b;
        if (a >= args->body_count || b >= args->body_count) { continue; }

        /* Find the dynamic body to determine the island. */
        uint32_t dyn = UINT32_MAX;
        if (!phys_body_is_static(&args->bodies[a])) { dyn = a; }
        else if (!phys_body_is_static(&args->bodies[b])) { dyn = b; }
        if (dyn == UINT32_MAX) { continue; } /* static-static: skip */

        uint32_t root = phys_uf_find(args->islands_out, dyn);
        if (root_to_island[root] != UINT32_MAX) {
            constraint_counts[root_to_island[root]]++;
        }
    }

    /* ── Step 4: Allocate per-island arrays ────────────────────── */
    args->islands_out->count = island_count;
    for (uint32_t i = 0; i < island_count; ++i) {
        phys_island_t *isl = &args->islands_out->islands[i];
        isl->body_count = 0;
        isl->constraint_count = 0;
        isl->sleeping = false;
        isl->body_indices = phys_frame_arena_alloc(
            args->arena, body_counts[i] * sizeof(uint32_t),
            _Alignof(uint32_t));
        isl->constraint_indices = phys_frame_arena_alloc(
            args->arena, constraint_counts[i] * sizeof(uint32_t),
            _Alignof(uint32_t));
    }

    /* ── Step 5: Fill body indices ─────────────────────────────── */
    for (uint32_t i = 0; i < args->body_count; ++i) {
        if (!connected[i]) { continue; }
        uint32_t root = phys_uf_find(args->islands_out, i);
        uint32_t idx = root_to_island[root];
        if (idx < island_count) {
            phys_island_t *isl = &args->islands_out->islands[idx];
            if (isl->body_indices) {
                isl->body_indices[isl->body_count] = i;
            }
            isl->body_count++;
        }
    }

    /* ── Step 6: Fill constraint indices ───────────────────────── */
    for (uint32_t i = 0; i < args->constraint_count; ++i) {
        uint32_t a = args->constraints[i].body_a;
        uint32_t b = args->constraints[i].body_b;
        if (a >= args->body_count || b >= args->body_count) { continue; }

        uint32_t dyn = UINT32_MAX;
        if (!phys_body_is_static(&args->bodies[a])) { dyn = a; }
        else if (!phys_body_is_static(&args->bodies[b])) { dyn = b; }
        if (dyn == UINT32_MAX) { continue; }

        uint32_t root = phys_uf_find(args->islands_out, dyn);
        uint32_t idx = root_to_island[root];
        if (idx < island_count) {
            phys_island_t *isl = &args->islands_out->islands[idx];
            if (isl->constraint_indices) {
                isl->constraint_indices[isl->constraint_count] = i;
            }
            isl->constraint_count++;
        }
    }

    /* ── Step 7: Mark islands sleeping when ALL bodies are asleep ── */
    for (uint32_t i = 0; i < island_count; ++i) {
        phys_island_t *isl = &args->islands_out->islands[i];
        if (isl->body_count == 0) { continue; }

        bool all_sleeping = true;
        for (uint32_t j = 0; j < isl->body_count; ++j) {
            uint32_t bi = isl->body_indices[j];
            if (bi < args->body_count &&
                !phys_body_is_sleeping(&args->bodies[bi])) {
                all_sleeping = false;
                break;
            }
        }
        isl->sleeping = all_sleeping;
    }
}
