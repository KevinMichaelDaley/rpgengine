/**
 * @file render_forward_caustic_sdf.c
 * @brief Feed the resident SDF chunk set into the caustics bake (rpg-39mc).
 *
 * The GI runtime already owns the scene's SDF: gi_sdf_stream pages fine
 * chunks by visibility and keeps the coarse zone field always resident. This
 * setter is the ONLY caustics-side plumbing: the world glue calls it each
 * frame with the current resident set, and the bake re-runs only when the
 * set actually changed. NOTE the residency pool REUSES texture objects for
 * different chunks, so the change signature compares the chunk ORIGINS as
 * well as the texture ids.
 */
#include "ferrum/renderer/render_forward.h"

#include <string.h>

void render_forward_set_caustic_sdf(render_forward_t *fwd,
                                    const uint32_t *textures,
                                    const float (*origins)[3],
                                    const float (*dims)[3],
                                    const float *voxels, uint32_t count,
                                    uint32_t zone_tex,
                                    const float zone_origin[3],
                                    const float zone_dims[3],
                                    float zone_voxel)
{
    if (fwd == NULL || fwd->caustics.map_tex == 0u)
        return;
    shadow_caustics_t *c = &fwd->caustics;
    if (count > SHADOW_CAUSTICS_MAX_SDF)
        count = SHADOW_CAUSTICS_MAX_SDF;
    if (textures == NULL || origins == NULL || dims == NULL || voxels == NULL)
        count = 0;

    /* Signature compare: same chunk set in the same slots + same zone ->
     * keep the existing bake. */
    bool same = (count == c->sdf_count) &&
                ((zone_tex != 0u) == (c->zone_tex != 0u));
    for (uint32_t i = 0; same && i < count; ++i)
        same = c->sdf_tex[i] == textures[i] &&
               memcmp(c->sdf_origin[i], origins[i],
                      sizeof c->sdf_origin[i]) == 0;
    if (same && zone_tex != 0u)
        same = c->zone_tex == zone_tex &&
               zone_origin != NULL &&
               memcmp(c->zone_origin, zone_origin, sizeof c->zone_origin) == 0;
    if (same) {
        /* Set stable: if a change was pending, count down and commit the
         * re-bake only once residency has settled -- SDF paging churns every
         * frame while streaming/moving, and re-tracing per change would hitch
         * constantly. */
        if (fwd->caustic_settle > 0 && --fwd->caustic_settle == 0)
            fwd->caustics_baked = false;
        return;
    }

    shadow_caustics_set_sdf(c, textures, origins, dims, voxels, count);
    shadow_caustics_set_zone(c, zone_tex, zone_origin, zone_dims, zone_voxel);
    if (!fwd->caustics_baked)
        return;                    /* first bake still pending: nothing to redo. */
    fwd->caustic_settle = 30;      /* ~half a second at 60 fps. */
}
