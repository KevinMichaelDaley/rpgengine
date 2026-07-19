/**
 * @file body_spawn_collider.c
 * @brief Build a canonical collider primitive from a decoded BODY_SPAWN (rpg-b5r3).
 *        Network channel (server-spawned dynamics).
 */
#include <string.h>

#include "ferrum/asset/collider_prim.h"
#include "ferrum/net/replication/body_spawn.h"
#include "ferrum/net/quantization.h"

void fr_collider_prim_from_body_spawn(const struct net_repl_body_spawn *spawn,
                                      fr_collider_prim_t *out)
{
    if (spawn == NULL || out == NULL) return;
    const net_repl_body_spawn_t *s = spawn;
    memset(out, 0, sizeof *out);
    out->bone = -1;
    out->rotation[3] = 1.0f;

    float hx = net_float16_to_float(s->half_x_f16);
    float hy = net_float16_to_float(s->half_y_f16);
    float hz = net_float16_to_float(s->half_z_f16);
    float ox = net_float16_to_float(s->off_x_f16);
    float oy = net_float16_to_float(s->off_y_f16);
    float oz = net_float16_to_float(s->off_z_f16);
    out->offset[0] = ox; out->offset[1] = oy; out->offset[2] = oz;

    switch (s->shape_type) {
    case 0: /* box */
        out->kind = FR_COLLIDER_PRIM_BOX;
        out->half_extents[0] = hx; out->half_extents[1] = hy; out->half_extents[2] = hz;
        break;
    case 1: /* sphere */
        out->kind = FR_COLLIDER_PRIM_SPHERE;
        out->radius = hx;
        break;
    case 2: /* capsule */
        out->kind = FR_COLLIDER_PRIM_CAPSULE;
        out->radius = hx; out->half_height = hy;
        break;
    case 3: /* mesh: geometry rides MESH_DATA keyed by body_id. */
        out->kind = FR_COLLIDER_PRIM_MESH;
        out->geom_asset = s->body_id;
        break;
    case 4: /* halfspace: normal in half_xyz, distance in off_x. */
        out->kind = FR_COLLIDER_PRIM_HALFSPACE;
        out->normal[0] = hx; out->normal[1] = hy; out->normal[2] = hz;
        out->plane_offset = ox;
        out->offset[0] = out->offset[1] = out->offset[2] = 0.0f;
        break;
    case 5: /* convex: hull points ride MESH_DATA keyed by body_id. */
        out->kind = FR_COLLIDER_PRIM_CONVEX;
        out->geom_asset = s->body_id;
        break;
    case 6: /* compound: decomposition rides MESH_DATA keyed by body_id. */
        out->kind = FR_COLLIDER_PRIM_COMPOUND;
        out->geom_asset = s->body_id;
        break;
    case 7: /* point */
        out->kind = FR_COLLIDER_PRIM_POINT;
        break;
    default:
        out->kind = FR_COLLIDER_PRIM_BOX;
        out->half_extents[0] = hx; out->half_extents[1] = hy; out->half_extents[2] = hz;
        break;
    }
}
