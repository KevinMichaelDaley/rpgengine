/**
 * @file refl_index.c
 * @brief Coarse probe grid index (see refl_index.h).
 */
#include "ferrum/renderer/gi/refl_index.h"

#include <stddef.h>

uint32_t refl_index_build(const refl_probe_set_t *set, const float mn[3],
                          const float mx[3], float cell, int32_t *cells,
                          uint32_t cell_capacity, int32_t out_dims[3])
{
    if (set == NULL || mn == NULL || mx == NULL || cells == NULL ||
        out_dims == NULL || cell <= 0.0f)
        return 0u;
    int32_t d[3];
    for (int a = 0; a < 3; ++a) {
        float ext = mx[a] - mn[a];
        d[a] = 1;
        if (ext > 0.0f) {
            d[a] = (int32_t)(ext / cell);
            if ((float)d[a] * cell < ext - 1e-4f)
                d[a] += 1;              /* ceil: cover the full extent. */
            if (d[a] < 1)
                d[a] = 1;
        }
    }
    uint32_t n_cells = (uint32_t)d[0] * (uint32_t)d[1] * (uint32_t)d[2];
    if (n_cells == 0u || n_cells > cell_capacity)
        return 0u;
    for (uint32_t i = 0; i < n_cells * REFL_INDEX_PER_CELL; ++i)
        cells[i] = -1;
    for (uint32_t p = 0; p < set->count; ++p) {
        const float *pos = set->probes[p].pos;
        int32_t c[3];
        int in = 1;
        for (int a = 0; a < 3; ++a) {
            c[a] = (int32_t)((pos[a] - mn[a]) / cell);
            if (pos[a] < mn[a] || c[a] < 0 || c[a] >= d[a])
                in = 0;
        }
        if (!in)
            continue;
        uint32_t ci = ((uint32_t)c[2] * (uint32_t)d[1] + (uint32_t)c[1]) *
                          (uint32_t)d[0] + (uint32_t)c[0];
        int32_t *slot = &cells[(size_t)ci * REFL_INDEX_PER_CELL];
        /* Append, or replace the FARTHEST-from-centre entry when full. */
        float cc[3] = { mn[0] + ((float)c[0] + 0.5f) * cell,
                        mn[1] + ((float)c[1] + 0.5f) * cell,
                        mn[2] + ((float)c[2] + 0.5f) * cell };
        float pd = 0.0f;
        for (int a = 0; a < 3; ++a)
            pd += (pos[a] - cc[a]) * (pos[a] - cc[a]);
        int placed = 0;
        for (uint32_t k = 0; k < REFL_INDEX_PER_CELL && !placed; ++k)
            if (slot[k] < 0) {
                slot[k] = (int32_t)p;
                placed = 1;
            }
        if (!placed) {
            uint32_t worst = 0u;
            float wd = -1.0f;
            for (uint32_t k = 0; k < REFL_INDEX_PER_CELL; ++k) {
                const float *op = set->probes[slot[k]].pos;
                float od = 0.0f;
                for (int a = 0; a < 3; ++a)
                    od += (op[a] - cc[a]) * (op[a] - cc[a]);
                if (od > wd) {
                    wd = od;
                    worst = k;
                }
            }
            if (pd < wd)
                slot[worst] = (int32_t)p;
        }
    }
    for (int a = 0; a < 3; ++a)
        out_dims[a] = d[a];
    return n_cells;
}
