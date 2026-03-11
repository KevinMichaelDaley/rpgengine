/**
 * @file constraint_color.c
 * @brief Greedy lowest-degree-first graph coloring for constraints.
 *
 * Algorithm:
 *   1. Build per-body adjacency lists: for each body, store the
 *      indices of all constraints that reference it.
 *   2. Compute per-constraint degree: sum of adjacency list lengths
 *      of body_a and body_b, minus 2 (for self-reference).
 *   3. Sort constraint indices by ascending degree (insertion sort
 *      for small N).
 *   4. Greedy assign: for each constraint in degree order, find the
 *      smallest color not used by any already-colored neighbor.
 *
 * All workspace is laid out in a caller-provided scratch buffer.
 * No arena or heap allocations.
 *
 * 2 non-static functions: phys_constraint_color_scratch_size,
 *                          phys_constraint_color
 */

#include "ferrum/physics/constraint_color.h"

#include <string.h>
#include <stddef.h>

#include "ferrum/physics/constraint.h"

/* ── Scratch layout ──────────────────────────────────────────────
 *
 * The scratch buffer is partitioned into:
 *   [0] body_ref_count : uint32_t[body_count]
 *   [1] adj_offsets    : uint32_t[body_count]   (start offset into adj_indices)
 *   [2] adj_fill       : uint32_t[body_count]   (current fill position)
 *   [3] adj_indices    : uint32_t[2 * constraint_count]  (flattened adjacency)
 *   [4] degree         : uint32_t[constraint_count]
 *   [5] order          : uint32_t[constraint_count]
 *   [6] colors         : uint32_t[constraint_count]  (output — survives)
 *   [7] used           : uint8_t[max_colors]
 */

/** Align a byte offset up to a given alignment. */
static size_t align_up_(size_t offset, size_t align) {
    return (offset + align - 1) & ~(align - 1);
}

size_t phys_constraint_color_scratch_size(uint32_t constraint_count,
                                           uint32_t body_count) {
    size_t off = 0;
    /* body_ref_count */
    off = align_up_(off, _Alignof(uint32_t));
    off += (size_t)body_count * sizeof(uint32_t);
    /* adj_offsets */
    off = align_up_(off, _Alignof(uint32_t));
    off += (size_t)body_count * sizeof(uint32_t);
    /* adj_fill */
    off = align_up_(off, _Alignof(uint32_t));
    off += (size_t)body_count * sizeof(uint32_t);
    /* adj_indices (each constraint references 2 bodies) */
    off = align_up_(off, _Alignof(uint32_t));
    off += (size_t)constraint_count * 2 * sizeof(uint32_t);
    /* degree */
    off = align_up_(off, _Alignof(uint32_t));
    off += (size_t)constraint_count * sizeof(uint32_t);
    /* order */
    off = align_up_(off, _Alignof(uint32_t));
    off += (size_t)constraint_count * sizeof(uint32_t);
    /* colors */
    off = align_up_(off, _Alignof(uint32_t));
    off += (size_t)constraint_count * sizeof(uint32_t);
    /* used (max 256 colors) */
    uint32_t max_colors = constraint_count;
    if (max_colors > 256) max_colors = 256;
    off += max_colors * sizeof(uint8_t);
    return off;
}

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
                          uint8_t *scratch,
                          size_t scratch_size,
                          phys_color_result_t *result_out) {
    if (!scratch || !result_out) { return -1; }

    /* Handle trivial case. */
    if (constraint_count == 0) {
        result_out->colors = NULL;
        result_out->num_colors = 0;
        result_out->count = 0;
        return 0;
    }
    if (!constraints) { return -1; }

    /* Verify scratch is large enough. */
    size_t needed = phys_constraint_color_scratch_size(
        constraint_count, body_count);
    if (scratch_size < needed) { return -1; }

    /* ── Partition scratch buffer ────────────────────────────── */
    size_t off = 0;

    off = align_up_(off, _Alignof(uint32_t));
    uint32_t *body_ref_count = (uint32_t *)(scratch + off);
    off += (size_t)body_count * sizeof(uint32_t);

    off = align_up_(off, _Alignof(uint32_t));
    uint32_t *adj_offsets = (uint32_t *)(scratch + off);
    off += (size_t)body_count * sizeof(uint32_t);

    off = align_up_(off, _Alignof(uint32_t));
    uint32_t *adj_fill = (uint32_t *)(scratch + off);
    off += (size_t)body_count * sizeof(uint32_t);

    off = align_up_(off, _Alignof(uint32_t));
    uint32_t *adj_indices = (uint32_t *)(scratch + off);
    off += (size_t)constraint_count * 2 * sizeof(uint32_t);

    off = align_up_(off, _Alignof(uint32_t));
    uint32_t *degree = (uint32_t *)(scratch + off);
    off += (size_t)constraint_count * sizeof(uint32_t);

    off = align_up_(off, _Alignof(uint32_t));
    uint32_t *order = (uint32_t *)(scratch + off);
    off += (size_t)constraint_count * sizeof(uint32_t);

    off = align_up_(off, _Alignof(uint32_t));
    uint32_t *colors = (uint32_t *)(scratch + off);
    off += (size_t)constraint_count * sizeof(uint32_t);

    uint32_t max_colors = constraint_count;
    if (max_colors > 256) max_colors = 256;
    uint8_t *used = scratch + off;

    /* ── Step 1: Build per-body adjacency (flattened) ────────── */

    memset(body_ref_count, 0, body_count * sizeof(uint32_t));
    for (uint32_t ci = 0; ci < constraint_count; ++ci) {
        uint32_t a = constraints[ci].body_a;
        uint32_t b = constraints[ci].body_b;
        if (a < body_count) { body_ref_count[a]++; }
        if (b < body_count) { body_ref_count[b]++; }
    }

    /* Prefix sum to get offsets into flattened adj_indices. */
    adj_offsets[0] = 0;
    for (uint32_t bi = 1; bi < body_count; ++bi) {
        adj_offsets[bi] = adj_offsets[bi - 1] + body_ref_count[bi - 1];
    }
    memcpy(adj_fill, adj_offsets, body_count * sizeof(uint32_t));

    /* Populate flattened adjacency. */
    for (uint32_t ci = 0; ci < constraint_count; ++ci) {
        uint32_t a = constraints[ci].body_a;
        uint32_t b = constraints[ci].body_b;
        if (a < body_count) {
            adj_indices[adj_fill[a]++] = ci;
        }
        if (b < body_count) {
            adj_indices[adj_fill[b]++] = ci;
        }
    }

    /* ── Step 2: Compute per-constraint degree ───────────────── */

    for (uint32_t ci = 0; ci < constraint_count; ++ci) {
        uint32_t a = constraints[ci].body_a;
        uint32_t b = constraints[ci].body_b;
        uint32_t da = (a < body_count) ? body_ref_count[a] : 0;
        uint32_t db = (b < body_count) ? body_ref_count[b] : 0;
        uint32_t d = da + db;
        if (d >= 2) { d -= 2; } else { d = 0; }
        degree[ci] = d;
    }

    /* ── Step 3: Sort by ascending degree ────────────────────── */

    for (uint32_t ci = 0; ci < constraint_count; ++ci) {
        order[ci] = ci;
    }
    sort_by_degree_(order, degree, constraint_count);

    /* ── Step 4: Greedy coloring ─────────────────────────────── */

    for (uint32_t ci = 0; ci < constraint_count; ++ci) {
        colors[ci] = UINT32_MAX;
    }

    uint32_t num_colors = 0;

    for (uint32_t oi = 0; oi < constraint_count; ++oi) {
        uint32_t ci = order[oi];
        memset(used, 0, max_colors * sizeof(uint8_t));

        /* Mark colors used by neighbors (constraints sharing a body). */
        uint32_t a = constraints[ci].body_a;
        uint32_t b = constraints[ci].body_b;

        if (a < body_count) {
            uint32_t start = adj_offsets[a];
            uint32_t end   = start + body_ref_count[a];
            for (uint32_t k = start; k < end; ++k) {
                uint32_t ni = adj_indices[k];
                if (ni != ci && colors[ni] < max_colors) {
                    used[colors[ni]] = 1;
                }
            }
        }
        if (b < body_count) {
            uint32_t start = adj_offsets[b];
            uint32_t end   = start + body_ref_count[b];
            for (uint32_t k = start; k < end; ++k) {
                uint32_t ni = adj_indices[k];
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
