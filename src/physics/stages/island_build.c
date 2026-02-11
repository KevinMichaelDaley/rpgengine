/**
 * @file island_build.c
 * @brief Stage 10: Island Build — groups connected bodies into islands
 *        using union-find.
 *
 * Static bodies do NOT merge islands (two dynamic bodies connected
 * only through a static body remain in separate islands).  However,
 * constraints involving one static body ARE assigned to the dynamic
 * body's island so the solver can resolve them.
 */

#include "ferrum/physics/island_build.h"

#include "ferrum/physics/body.h"
#include "ferrum/physics/constraint.h"
#include "ferrum/physics/island.h"
#include "ferrum/physics/phys_pool.h"

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

    /* ── Step 1: Initialize union-find ─────────────────────────── */
    phys_island_list_init(args->islands_out, args->arena,
                          args->body_count, args->body_count);

    /* ── Step 2: Union ONLY dynamic-dynamic constraint pairs ───── */
    for (uint32_t i = 0; i < args->constraint_count; ++i) {
        uint32_t a = args->constraints[i].body_a;
        uint32_t b = args->constraints[i].body_b;
        if (a >= args->body_count || b >= args->body_count) { continue; }

        /* Skip if either body is static — static bodies must not
         * merge islands together. */
        if (phys_body_is_static(&args->bodies[a])) { continue; }
        if (phys_body_is_static(&args->bodies[b])) { continue; }

        phys_uf_union(args->islands_out, a, b);
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
