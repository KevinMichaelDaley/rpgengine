/**
 * @file island_build.c
 * @brief Stage 10: Island Build — groups connected bodies into islands
 *        using union-find, filtering out static-body constraints.
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

    /*
     * Filter constraints: exclude any pair where either body is static.
     * Static bodies must not merge islands — two dynamic bodies connected
     * only through a static body must remain in separate islands.
     */
    uint32_t filtered_count = 0;
    phys_constraint_t *filtered = NULL;

    if (args->constraints && args->constraint_count > 0) {
        filtered = phys_frame_arena_alloc(
            args->arena,
            args->constraint_count * sizeof(phys_constraint_t),
            _Alignof(phys_constraint_t));
        if (!filtered) { return; }

        for (uint32_t i = 0; i < args->constraint_count; ++i) {
            uint32_t a = args->constraints[i].body_a;
            uint32_t b = args->constraints[i].body_b;

            /* Skip if either body index is out of range or body is static. */
            if (a >= args->body_count || b >= args->body_count) { continue; }
            if (phys_body_is_static(&args->bodies[a])) { continue; }
            if (phys_body_is_static(&args->bodies[b])) { continue; }

            filtered[filtered_count++] = args->constraints[i];
        }
    }

    /* Initialize the island list with enough capacity. */
    phys_island_list_init(args->islands_out, args->arena,
                          args->body_count, args->body_count);

    /* Build islands from the filtered (dynamic-only) constraints. */
    phys_island_list_build(args->islands_out, filtered, filtered_count,
                           args->body_count, args->arena);
}
