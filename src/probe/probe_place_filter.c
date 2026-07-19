/**
 * @file probe_place_filter.c
 * @brief Keep only probes inside a resident light-data chunk box (rpg-ft0g).
 *        A probe outside every resident chunk is not loaded/generated.
 */
#include <string.h>

#include "ferrum/memory/arena.h"
#include "ferrum/probe/probe_place.h"

/* True if p is inside any of the n chunk boxes. */
static bool in_any_chunk(const float p[3], const float *cmin, const float *cmax,
                         uint32_t n)
{
    for (uint32_t c = 0; c < n; ++c) {
        const float *lo = &cmin[c * 3], *hi = &cmax[c * 3];
        if (p[0] >= lo[0] && p[0] <= hi[0] && p[1] >= lo[1] && p[1] <= hi[1] &&
            p[2] >= lo[2] && p[2] <= hi[2])
            return true;
    }
    return false;
}

uint32_t probe_place_filter_chunks(const probe_set_t *set,
                                   const float *chunk_min, const float *chunk_max,
                                   uint32_t n_chunks, struct arena *arena,
                                   probe_set_t *out)
{
    if (out == NULL) return 0u;
    memset(out, 0, sizeof *out);
    if (set == NULL || set->positions == NULL || arena == NULL ||
        chunk_min == NULL || chunk_max == NULL || n_chunks == 0u)
        return 0u;

    /* Pass 1: count survivors. */
    uint32_t kept = 0;
    for (uint32_t i = 0; i < set->count; ++i)
        if (in_any_chunk(&set->positions[i * 3], chunk_min, chunk_max, n_chunks))
            kept++;
    if (kept == 0u) return 0u;

    uint32_t sh_c = (set->sh != NULL) ? set->sh_coeffs : 0u;
    float *pos = arena_alloc((arena_t *)arena, 16u, (size_t)kept * 3u * sizeof(float));
    float *sh = NULL;
    if (pos == NULL) return 0u;
    if (sh_c > 0u) {
        sh = arena_alloc((arena_t *)arena, 16u, (size_t)kept * sh_c * sizeof(float));
        if (sh == NULL) return 0u;
    }

    /* Pass 2: copy survivors (positions + baked SH, preserving order). */
    uint32_t k = 0;
    for (uint32_t i = 0; i < set->count; ++i) {
        if (!in_any_chunk(&set->positions[i * 3], chunk_min, chunk_max, n_chunks))
            continue;
        pos[k * 3 + 0] = set->positions[i * 3 + 0];
        pos[k * 3 + 1] = set->positions[i * 3 + 1];
        pos[k * 3 + 2] = set->positions[i * 3 + 2];
        if (sh != NULL)
            memcpy(&sh[k * sh_c], &set->sh[(size_t)i * sh_c], sh_c * sizeof(float));
        ++k;
    }
    out->count = kept;
    out->positions = pos;
    out->sh = sh;
    out->sh_coeffs = sh_c;
    /* Filtered subset -> unstructured point set. */
    return kept;
}
