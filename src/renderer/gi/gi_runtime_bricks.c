/**
 * @file gi_runtime_bricks.c
 * @brief Brick sampling structure attachment for the GI runtime (rpg-pjkb).
 */
#include "ferrum/renderer/gi/gi_runtime.h"

bool gi_runtime_set_bricks(gi_runtime_t *gi, const probe_brick_data_t *bd,
                           const probe_brick_index_t *ix)
{
    if (gi == NULL) return false;
    return gi_brick_gpu_create(&gi->bricks, bd, ix);
}
