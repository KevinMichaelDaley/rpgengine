/**
 * @file probe_place_grid.c
 * @brief Default regular probe grid over an AABB (rpg-ft0g) -- the reusable,
 *        headless version of hall_lit_dynamic.c's inline placement.
 */
#include <string.h>

#include "ferrum/memory/arena.h"
#include "ferrum/probe/probe_place.h"

/* Clamp for the per-axis grid resolution (matches the demo's cap). */
#define PROBE_GRID_MIN_DIM 2
#define PROBE_GRID_MAX_DIM 40

static int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

bool probe_place_grid(const scene_desc_probes_t *spec, const float aabb_min[3],
                      const float aabb_max[3], struct arena *arena,
                      probe_set_t *out)
{
    if (aabb_min == NULL || aabb_max == NULL || arena == NULL || out == NULL)
        return false;
    memset(out, 0, sizeof *out);

    /* Layout constants lifted verbatim from hall_lit_dynamic.c: the grid is inset
     * horizontally and concentrated in the lower volume [fylo,fyhi] where the GI
     * gradient is steep (floor/wall contact). Horizontal spacing (spacing) and a
     * separate, tighter vertical spacing (vspacing) come from the descriptor. */
    const float ins = 0.04f, fylo = 0.01f, fyhi = 0.52f;
    float sp  = (spec != NULL && spec->spacing  > 0.0f) ? spec->spacing  : 2.0f;
    float vsp = (spec != NULL && spec->vspacing > 0.0f) ? spec->vspacing : 1.1f;
    if (sp  < 0.4f) sp  = 0.4f;
    if (vsp < 0.3f) vsp = 0.3f;

    float span[3];
    for (int a = 0; a < 3; ++a) span[a] = aabb_max[a] - aabb_min[a];

    int pnx = clampi((int)(span[0] * (1.0f - 2.0f * ins) / sp) + 1,
                     PROBE_GRID_MIN_DIM, PROBE_GRID_MAX_DIM);
    int pnz = clampi((int)(span[2] * (1.0f - 2.0f * ins) / sp) + 1,
                     PROBE_GRID_MIN_DIM, PROBE_GRID_MAX_DIM);
    int pny = clampi((int)(span[1] * (fyhi - fylo) / vsp) + 1,
                     PROBE_GRID_MIN_DIM, PROBE_GRID_MAX_DIM);

    out->grid_dim[0] = pnx; out->grid_dim[1] = pny; out->grid_dim[2] = pnz;
    out->grid_origin[0] = aabb_min[0] + span[0] * ins;
    out->grid_origin[1] = aabb_min[1] + span[1] * fylo;
    out->grid_origin[2] = aabb_min[2] + span[2] * ins;
    out->grid_cell[0] = span[0] * (1.0f - 2.0f * ins) / (float)(pnx - 1);
    out->grid_cell[1] = span[1] * (fyhi - fylo) / (float)(pny - 1);
    out->grid_cell[2] = span[2] * (1.0f - 2.0f * ins) / (float)(pnz - 1);

    uint32_t count = (uint32_t)pnx * (uint32_t)pny * (uint32_t)pnz;
    float *pos = arena_alloc((arena_t *)arena, 16u, (size_t)count * 3u * sizeof(float));
    if (pos == NULL) return false;

    /* Index order (z outer, y, x inner): probe = (z*pny + y)*pnx + x. */
    uint32_t k = 0;
    for (int iz = 0; iz < pnz; ++iz)
    for (int iy = 0; iy < pny; ++iy)
    for (int ix = 0; ix < pnx; ++ix) {
        pos[k * 3 + 0] = out->grid_origin[0] + out->grid_cell[0] * (float)ix;
        pos[k * 3 + 1] = out->grid_origin[1] + out->grid_cell[1] * (float)iy;
        pos[k * 3 + 2] = out->grid_origin[2] + out->grid_cell[2] * (float)iz;
        ++k;
    }
    out->count = count;
    out->positions = pos;
    return true;
}
