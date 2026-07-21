/**
 * @file gi_runtime_bricks.c
 * @brief Brick sampling structure attachment for the GI runtime (rpg-pjkb).
 */
#include "ferrum/renderer/gi/gi_runtime.h"

bool gi_runtime_set_bricks(gi_runtime_t *gi, const probe_brick_data_t *bd,
                           const probe_brick_index_t *ix)
{
    if (gi == NULL) return false;
    if (!gi_brick_gpu_create(&gi->bricks, bd, ix)) return false;
    /* Hand the same GL objects to the probe COMPUTE so the recurrent field
     * gather samples the identical structure the forward pass does. */
    gi_probe_gpu_set_cbrick(&gi->gpu, gi->bricks.index_tex, gi->bricks.meta_tex,
                            gi->bricks.pidx_tex, gi->bricks.valid_tex,
                            gi->bricks.dim, gi->bricks.origin, gi->bricks.voxel);
    return true;
}
