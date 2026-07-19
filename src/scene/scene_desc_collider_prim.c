/**
 * @file scene_desc_collider_prim.c
 * @brief Build a canonical collider primitive from a scene-descriptor collider
 *        (rpg-b5r3). Asset/descriptor channel (level-authored objects, incl.
 *        dynamic ones that are NOT server-spawned).
 */
#include <string.h>

#include "ferrum/asset/collider_prim.h"
#include "ferrum/scene/scene_desc_collider.h"

void fr_collider_prim_from_desc(const struct scene_desc_collider *desc,
                                fr_collider_prim_t *out)
{
    if (desc == NULL || out == NULL) return;
    const scene_desc_collider_t *d = desc;
    memset(out, 0, sizeof *out);
    /* scene_desc_collider_kind_t and fr_collider_prim_kind_t share numbering. */
    out->kind = (fr_collider_prim_kind_t)d->kind;
    out->bone = d->bone;
    for (int i = 0; i < 3; ++i) {
        out->offset[i] = d->position[i];
        out->half_extents[i] = d->half_extents[i];
        out->normal[i] = d->normal[i];
    }
    for (int i = 0; i < 4; ++i) out->rotation[i] = d->rotation[i];
    out->radius = d->radius;
    out->half_height = d->half_height;
    out->plane_offset = d->plane_offset;
    out->geom_asset = d->geom_asset;
    out->solid = d->solid;
}
