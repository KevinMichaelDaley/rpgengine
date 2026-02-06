/** @file
 * @brief Spatial hash grid implementation for broadphase collision detection.
 */

#include "ferrum/physics/spatial_grid.h"
#include "ferrum/physics/phys_pool.h"

#include <math.h>
#include <string.h>

/* ── Static helpers ─────────────────────────────────────────────── */

/** Initial capacity for a cell's body_indices array. */
#define GRID_CELL_INITIAL_CAPACITY 16u

/**
 * @brief Spatial hash for integer cell coordinates.
 *
 * Uses large primes to distribute cells uniformly across the hash table.
 */
static uint32_t phys_grid_hash(int32_t x, int32_t y, int32_t z) {
    return ((uint32_t)x * 73856093u) ^
           ((uint32_t)y * 19349663u) ^
           ((uint32_t)z * 83492791u);
}

/**
 * @brief Add a body index to a cell, allocating from the arena if needed.
 *
 * If the cell has no capacity, an initial allocation is made.  If the cell
 * is full, a larger array is allocated (old memory is abandoned in the arena).
 *
 * @return true on success, false if the arena is exhausted.
 */
static bool grid_cell_add(phys_grid_cell_t *cell, uint32_t body_index,
                           phys_frame_arena_t *arena) {
    /* Check if body is already in this cell. */
    for (uint32_t i = 0; i < cell->count; ++i) {
        if (cell->body_indices[i] == body_index) {
            return true; /* Already present — skip duplicate. */
        }
    }

    /* Grow if needed. */
    if (cell->count >= cell->capacity) {
        uint32_t new_cap = (cell->capacity == 0)
                               ? GRID_CELL_INITIAL_CAPACITY
                               : cell->capacity * 2;
        uint32_t *new_buf = (uint32_t *)phys_frame_arena_alloc(
            arena, new_cap * sizeof(uint32_t), _Alignof(uint32_t));
        if (!new_buf) {
            return false; /* Arena exhausted. */
        }
        /* Copy existing entries into the new buffer. */
        if (cell->count > 0 && cell->body_indices) {
            memcpy(new_buf, cell->body_indices, cell->count * sizeof(uint32_t));
        }
        cell->body_indices = new_buf;
        cell->capacity = new_cap;
    }

    cell->body_indices[cell->count++] = body_index;
    return true;
}

/**
 * @brief Check if a value is already in an array (linear scan).
 */
static bool index_array_contains(const uint32_t *arr, uint32_t count,
                                  uint32_t value) {
    for (uint32_t i = 0; i < count; ++i) {
        if (arr[i] == value) {
            return true;
        }
    }
    return false;
}

/* ── Public API (4 non-static functions) ────────────────────────── */

void phys_spatial_grid_init(phys_spatial_grid_t *grid, uint32_t cell_count,
                            float cell_size, struct phys_frame_arena *arena) {
    if (!grid || !arena || cell_count == 0 || cell_size <= 0.0f) {
        return;
    }

    grid->cell_count   = cell_count;
    grid->cell_mask    = cell_count - 1;
    grid->cell_size    = cell_size;
    grid->inv_cell_size = 1.0f / cell_size;
    grid->arena        = arena;

    /* Allocate the cell array from the arena and zero it. */
    size_t alloc_size = (size_t)cell_count * sizeof(phys_grid_cell_t);
    grid->cells = (phys_grid_cell_t *)phys_frame_arena_alloc(
        arena, alloc_size, _Alignof(phys_grid_cell_t));
    if (grid->cells) {
        memset(grid->cells, 0, alloc_size);
    }
}

void phys_spatial_grid_clear(phys_spatial_grid_t *grid) {
    if (!grid || !grid->cells) {
        return;
    }
    /* Zero all cells — counts and pointers reset. */
    memset(grid->cells, 0,
           (size_t)grid->cell_count * sizeof(phys_grid_cell_t));
}

void phys_spatial_grid_insert(phys_spatial_grid_t *grid, uint32_t body_index,
                              const phys_aabb_t *aabb) {
    if (!grid || !grid->cells || !aabb || !grid->arena) {
        return;
    }

    /* Compute cell coordinate range for the AABB. */
    int32_t min_cx = (int32_t)floorf(aabb->min.x * grid->inv_cell_size);
    int32_t min_cy = (int32_t)floorf(aabb->min.y * grid->inv_cell_size);
    int32_t min_cz = (int32_t)floorf(aabb->min.z * grid->inv_cell_size);
    int32_t max_cx = (int32_t)floorf(aabb->max.x * grid->inv_cell_size);
    int32_t max_cy = (int32_t)floorf(aabb->max.y * grid->inv_cell_size);
    int32_t max_cz = (int32_t)floorf(aabb->max.z * grid->inv_cell_size);

    /* Insert into every cell the AABB overlaps. */
    for (int32_t cz = min_cz; cz <= max_cz; ++cz) {
        for (int32_t cy = min_cy; cy <= max_cy; ++cy) {
            for (int32_t cx = min_cx; cx <= max_cx; ++cx) {
                uint32_t hash = phys_grid_hash(cx, cy, cz);
                uint32_t idx  = hash & grid->cell_mask;
                grid_cell_add(&grid->cells[idx], body_index, grid->arena);
            }
        }
    }
}

uint32_t phys_spatial_grid_query(const phys_spatial_grid_t *grid,
                                 const phys_aabb_t *aabb,
                                 uint32_t *out_indices, uint32_t max_results) {
    if (!grid || !grid->cells || !aabb || !out_indices || max_results == 0) {
        return 0;
    }

    /* Compute cell coordinate range for the query AABB. */
    int32_t min_cx = (int32_t)floorf(aabb->min.x * grid->inv_cell_size);
    int32_t min_cy = (int32_t)floorf(aabb->min.y * grid->inv_cell_size);
    int32_t min_cz = (int32_t)floorf(aabb->min.z * grid->inv_cell_size);
    int32_t max_cx = (int32_t)floorf(aabb->max.x * grid->inv_cell_size);
    int32_t max_cy = (int32_t)floorf(aabb->max.y * grid->inv_cell_size);
    int32_t max_cz = (int32_t)floorf(aabb->max.z * grid->inv_cell_size);

    uint32_t result_count = 0;

    for (int32_t cz = min_cz; cz <= max_cz; ++cz) {
        for (int32_t cy = min_cy; cy <= max_cy; ++cy) {
            for (int32_t cx = min_cx; cx <= max_cx; ++cx) {
                uint32_t hash = phys_grid_hash(cx, cy, cz);
                uint32_t idx  = hash & grid->cell_mask;
                const phys_grid_cell_t *cell = &grid->cells[idx];

                for (uint32_t i = 0; i < cell->count; ++i) {
                    uint32_t body_idx = cell->body_indices[i];
                    /* Deduplicate: only add if not already in results. */
                    if (!index_array_contains(out_indices, result_count,
                                              body_idx)) {
                        if (result_count >= max_results) {
                            return result_count;
                        }
                        out_indices[result_count++] = body_idx;
                    }
                }
            }
        }
    }

    return result_count;
}
