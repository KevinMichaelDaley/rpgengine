/**
 * @file constraint_color.c
 * @brief Greedy lowest-degree-first graph coloring for constraints.
 *
 * Algorithm:
 *   1. Build per-body adjacency lists: for each body, store the
 *      indices of all constraints that reference it.
 *   2. Compute per-constraint degree: sum of adjacency list lengths
 *      of body_a and body_b, minus 2 (for self-reference), capped
 *      to avoid double-counting shared neighbors.
 *   3. Sort constraint indices by ascending degree (insertion sort
 *      for small N, which is expected for per-island counts).
 *   4. Greedy assign: for each constraint in degree order, find the
 *      smallest color not used by any already-colored neighbor.
 *
 * All workspace is arena-allocated.
 *
 * 1 non-static function: phys_constraint_color
 */

#include "ferrum/physics/constraint_color.h"

#include <string.h>

#include "ferrum/physics/constraint.h"
#include "ferrum/physics/phys_pool.h"

/* ── Adjacency list (per-body) ─────────────────────────────────── */

/**
 * @brief Per-body list of constraint indices that reference this body.
 */
typedef struct body_adj {
    uint32_t *indices;   /**< Arena-allocated array of constraint indices. */
    uint32_t  count;     /**< Number of constraints referencing this body. */
    uint32_t  capacity;  /**< Allocated capacity. */
} body_adj_t;

/* ── Static helpers ────────────────────────────────────────────── */

/** Insertion sort of order[] by key[order[i]] ascending. */
static void sort_by_degree_(uint32_t *order, const uint32_t *degree,
                             uint32_t n) {
    for (uint32_t i = 1; i < n; ++i) {
        uint32_t val = order[i];
        uint32_t key = degree[val];
        uint32_t j = i;
        while (j > 0 && degree[order[j - 1]] > key) {
            order[j] = order[j - 1];
            --j;
        }
        order[j] = val;
    }
}

/** Find smallest color not present in the used_colors set. */
static uint32_t smallest_available_(const uint8_t *used, uint32_t max_check) {
    for (uint32_t c = 0; c < max_check; ++c) {
        if (!used[c]) { return c; }
    }
    return max_check;
}

/* ── Public API ────────────────────────────────────────────────── */

int phys_constraint_color(const phys_constraint_t *constraints,
                          uint32_t constraint_count,
                          uint32_t body_count,
                          phys_frame_arena_t *arena,
                          phys_color_result_t *result_out) {
    if (!arena || !result_out) { return -1; }

    /* Handle trivial case. */
    if (constraint_count == 0) {
        result_out->colors = NULL;
        result_out->num_colors = 0;
        result_out->count = 0;
        return 0;
    }
    if (!constraints) { return -1; }

    /* ── Step 1: Build per-body adjacency lists ──────────────── */

    /* Count how many constraints reference each body. */
    uint32_t *body_ref_count = phys_frame_arena_alloc(
        arena, body_count * sizeof(uint32_t), _Alignof(uint32_t));
    if (!body_ref_count) { return -1; }
    memset(body_ref_count, 0, body_count * sizeof(uint32_t));

    for (uint32_t ci = 0; ci < constraint_count; ++ci) {
        uint32_t a = constraints[ci].body_a;
        uint32_t b = constraints[ci].body_b;
        if (a < body_count) { body_ref_count[a]++; }
        if (b < body_count) { body_ref_count[b]++; }
    }

    /* Allocate per-body index arrays. */
    body_adj_t *adj = phys_frame_arena_alloc(
        arena, body_count * sizeof(body_adj_t), _Alignof(body_adj_t));
    if (!adj) { return -1; }

    for (uint32_t bi = 0; bi < body_count; ++bi) {
        uint32_t cap = body_ref_count[bi];
        adj[bi].count = 0;
        adj[bi].capacity = cap;
        if (cap > 0) {
            adj[bi].indices = phys_frame_arena_alloc(
                arena, cap * sizeof(uint32_t), _Alignof(uint32_t));
            if (!adj[bi].indices) { return -1; }
        } else {
            adj[bi].indices = NULL;
        }
    }

    /* Populate adjacency lists. */
    for (uint32_t ci = 0; ci < constraint_count; ++ci) {
        uint32_t a = constraints[ci].body_a;
        uint32_t b = constraints[ci].body_b;
        if (a < body_count && adj[a].count < adj[a].capacity) {
            adj[a].indices[adj[a].count++] = ci;
        }
        if (b < body_count && adj[b].count < adj[b].capacity) {
            adj[b].indices[adj[b].count++] = ci;
        }
    }

    /* ── Step 2: Compute per-constraint degree ───────────────── */

    uint32_t *degree = phys_frame_arena_alloc(
        arena, constraint_count * sizeof(uint32_t), _Alignof(uint32_t));
    if (!degree) { return -1; }

    for (uint32_t ci = 0; ci < constraint_count; ++ci) {
        uint32_t a = constraints[ci].body_a;
        uint32_t b = constraints[ci].body_b;
        /* Degree = number of other constraints sharing at least one body.
         * Upper bound: adj[a].count + adj[b].count - 2 (self refs).
         * May overcount if a neighbor shares both bodies, but that's
         * fine for greedy ordering — it's a heuristic. */
        uint32_t da = (a < body_count) ? adj[a].count : 0;
        uint32_t db = (b < body_count) ? adj[b].count : 0;
        uint32_t d = da + db;
        if (d >= 2) { d -= 2; } else { d = 0; } /* subtract self refs */
        degree[ci] = d;
    }

    /* ── Step 3: Sort by ascending degree ────────────────────── */

    uint32_t *order = phys_frame_arena_alloc(
        arena, constraint_count * sizeof(uint32_t), _Alignof(uint32_t));
    if (!order) { return -1; }

    for (uint32_t ci = 0; ci < constraint_count; ++ci) {
        order[ci] = ci;
    }
    sort_by_degree_(order, degree, constraint_count);

    /* ── Step 4: Greedy coloring ─────────────────────────────── */

    uint32_t *colors = phys_frame_arena_alloc(
        arena, constraint_count * sizeof(uint32_t), _Alignof(uint32_t));
    if (!colors) { return -1; }

    /* UINT32_MAX = uncolored sentinel. */
    for (uint32_t ci = 0; ci < constraint_count; ++ci) {
        colors[ci] = UINT32_MAX;
    }

    /* Temporary bitset for "used colors among neighbors".
     * Max possible colors = constraint_count, but in practice
     * much smaller.  We use a byte array capped at a reasonable
     * maximum (256 colors should cover any real scenario). */
    uint32_t max_colors = constraint_count;
    if (max_colors > 256) { max_colors = 256; }
    uint8_t *used = phys_frame_arena_alloc(
        arena, max_colors * sizeof(uint8_t), _Alignof(uint8_t));
    if (!used) { return -1; }

    uint32_t num_colors = 0;

    for (uint32_t oi = 0; oi < constraint_count; ++oi) {
        uint32_t ci = order[oi];
        memset(used, 0, max_colors * sizeof(uint8_t));

        /* Mark colors used by neighbors (constraints sharing a body). */
        uint32_t a = constraints[ci].body_a;
        uint32_t b = constraints[ci].body_b;

        if (a < body_count) {
            for (uint32_t k = 0; k < adj[a].count; ++k) {
                uint32_t ni = adj[a].indices[k];
                if (ni != ci && colors[ni] < max_colors) {
                    used[colors[ni]] = 1;
                }
            }
        }
        if (b < body_count) {
            for (uint32_t k = 0; k < adj[b].count; ++k) {
                uint32_t ni = adj[b].indices[k];
                if (ni != ci && colors[ni] < max_colors) {
                    used[colors[ni]] = 1;
                }
            }
        }

        uint32_t color = smallest_available_(used, max_colors);
        colors[ci] = color;
        if (color + 1 > num_colors) {
            num_colors = color + 1;
        }
    }

    /* ── Output ──────────────────────────────────────────────── */

    result_out->colors = colors;
    result_out->num_colors = num_colors;
    result_out->count = constraint_count;
    return 0;
}
