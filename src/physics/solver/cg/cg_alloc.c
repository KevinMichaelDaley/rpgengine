/**
 * @file cg_alloc.c
 * @brief Arena allocation for sparse CG solver workspace.
 *
 * 1 non-static function: phys_cg_alloc.
 */

#include "ferrum/physics/solver/cg_solve.h"
#include "ferrum/physics/phys_pool.h"

#include <string.h>

bool phys_cg_alloc(cg_system_t *sys,
                   phys_frame_arena_t *arena,
                   uint32_t max_rows)
{
    if (!sys || !arena || max_rows == 0) return false;
    if (max_rows > CG_MAX_ROWS) max_rows = CG_MAX_ROWS;

    memset(sys, 0, sizeof(*sys));

    /* Allocate all arrays from the frame arena.  Alignment = 16
     * for potential SIMD in the future. */
    const size_t align = 16;

    sys->A_rows = phys_frame_arena_alloc(arena,
        max_rows * sizeof(cg_sparse_row_t), align);
    sys->diag_inv = phys_frame_arena_alloc(arena,
        max_rows * sizeof(float), align);
    sys->rhs = phys_frame_arena_alloc(arena,
        max_rows * sizeof(float), align);
    sys->lambda = phys_frame_arena_alloc(arena,
        max_rows * sizeof(float), align);
    sys->residual = phys_frame_arena_alloc(arena,
        max_rows * sizeof(float), align);
    sys->search_dir = phys_frame_arena_alloc(arena,
        max_rows * sizeof(float), align);
    sys->z = phys_frame_arena_alloc(arena,
        max_rows * sizeof(float), align);
    sys->Ap = phys_frame_arena_alloc(arena,
        max_rows * sizeof(float), align);
    sys->lambda_min = phys_frame_arena_alloc(arena,
        max_rows * sizeof(float), align);
    sys->lambda_max = phys_frame_arena_alloc(arena,
        max_rows * sizeof(float), align);
    sys->row_constraint = phys_frame_arena_alloc(arena,
        max_rows * sizeof(uint32_t), align);
    sys->row_sub = phys_frame_arena_alloc(arena,
        max_rows * sizeof(uint8_t), align);

    /* Check all allocations succeeded. */
    if (!sys->A_rows || !sys->diag_inv || !sys->rhs ||
        !sys->lambda || !sys->residual || !sys->search_dir ||
        !sys->z || !sys->Ap || !sys->lambda_min || !sys->lambda_max ||
        !sys->row_constraint || !sys->row_sub) {
        memset(sys, 0, sizeof(*sys));
        return false;
    }

    return true;
}
