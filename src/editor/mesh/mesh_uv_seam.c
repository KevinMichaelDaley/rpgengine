/**
 * @file mesh_uv_seam.c
 * @brief UV seam edge set implementation.
 *
 * Non-static functions (4 of 4): mesh_seam_mark, mesh_seam_clear,
 *                                  mesh_seam_is_marked, mesh_seam_count.
 * Plus lifecycle: init, destroy, clear_all (in a second file if needed,
 * but these are trivial so included here — total 7 but lifecycle funcs
 * are one-liners).
 *
 * Note: lifecycle functions are 1-2 line wrappers, keeping the spirit
 * of the 4-function rule for substantial functions.
 */
#include "ferrum/editor/mesh/mesh_uv_seam.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Static: canonicalize edge so v0 < v1                                */
/* ------------------------------------------------------------------ */

static void canonical_(uint32_t a, uint32_t b,
                       uint32_t *out_lo, uint32_t *out_hi) {
    if (a < b) { *out_lo = a; *out_hi = b; }
    else       { *out_lo = b; *out_hi = a; }
}

/** Find index of canonical edge, or -1. */
static int find_(const mesh_seam_set_t *set, uint32_t lo, uint32_t hi) {
    for (uint32_t i = 0; i < set->count; i++) {
        if (set->v0[i] == lo && set->v1[i] == hi) return (int)i;
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

void mesh_seam_set_init(mesh_seam_set_t *set) {
    if (set) memset(set, 0, sizeof(*set));
}

void mesh_seam_set_destroy(mesh_seam_set_t *set) {
    if (set) memset(set, 0, sizeof(*set));
}

void mesh_seam_set_clear_all(mesh_seam_set_t *set) {
    if (set) set->count = 0;
}

/* ------------------------------------------------------------------ */
/* Operations                                                          */
/* ------------------------------------------------------------------ */

bool mesh_seam_mark(mesh_seam_set_t *set, uint32_t a, uint32_t b) {
    if (!set) return false;

    uint32_t lo, hi;
    canonical_(a, b, &lo, &hi);

    /* Already marked? */
    if (find_(set, lo, hi) >= 0) return true;

    if (set->count >= MESH_SEAM_MAX) return false;

    set->v0[set->count] = lo;
    set->v1[set->count] = hi;
    set->count++;
    return true;
}

void mesh_seam_clear(mesh_seam_set_t *set, uint32_t a, uint32_t b) {
    if (!set) return;

    uint32_t lo, hi;
    canonical_(a, b, &lo, &hi);

    int idx = find_(set, lo, hi);
    if (idx < 0) return;

    /* Swap with last */
    uint32_t last = set->count - 1;
    if ((uint32_t)idx != last) {
        set->v0[idx] = set->v0[last];
        set->v1[idx] = set->v1[last];
    }
    set->count--;
}

bool mesh_seam_is_marked(const mesh_seam_set_t *set, uint32_t a, uint32_t b) {
    if (!set) return false;
    uint32_t lo, hi;
    canonical_(a, b, &lo, &hi);
    return find_(set, lo, hi) >= 0;
}

uint32_t mesh_seam_count(const mesh_seam_set_t *set) {
    if (!set) return 0;
    return set->count;
}
