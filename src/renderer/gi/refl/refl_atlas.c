/**
 * @file refl_atlas.c
 * @brief Atlas layout math (see refl_atlas.h).
 */
#include "ferrum/renderer/gi/refl_atlas.h"

#include <stddef.h>

void refl_atlas_dims(const refl_probe_set_t *set, uint32_t mip,
                     uint32_t *out_w, uint32_t *out_h)
{
    if (set == NULL)
        return;
    uint32_t m = mip;
    if (set->mips > 0u && m >= set->mips)
        m = set->mips - 1u;
    uint32_t tr = set->tile_res >> m;
    if (tr == 0u)
        tr = 1u;
    if (out_w != NULL)
        *out_w = set->tiles_x * tr;
    if (out_h != NULL)
        *out_h = set->tiles_y * tr;
}

bool refl_atlas_tile_rect(const refl_probe_set_t *set, uint32_t tile,
                          uint32_t mip, uint32_t *out_x, uint32_t *out_y,
                          uint32_t *out_res)
{
    if (set == NULL || set->tiles_x == 0u || set->tiles_y == 0u)
        return false;
    if (mip >= set->mips || tile >= set->tiles_x * set->tiles_y)
        return false;
    uint32_t tr = set->tile_res >> mip;
    if (tr == 0u)
        return false;
    if (out_x != NULL)
        *out_x = (tile % set->tiles_x) * tr;
    if (out_y != NULL)
        *out_y = (tile / set->tiles_x) * tr;
    if (out_res != NULL)
        *out_res = tr;
    return true;
}
