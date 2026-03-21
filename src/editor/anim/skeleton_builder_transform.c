/**
 * @file skeleton_builder_transform.c
 * @brief Compute rest_local from head/tail positions, update tail positions.
 *
 * Non-static functions (2 / 4 limit):
 *   skeleton_builder_rest_local_from_head_tail
 *   skeleton_builder_update_tail_positions
 */

#include "ferrum/editor/anim/skeleton_builder.h"
#include "ferrum/animation/constraint_params.h"
#include "ferrum/math/mat4.h"
#include "ferrum/math/vec3.h"

#include <math.h>
#include <stdlib.h>

mat4_t skeleton_builder_rest_local_from_head_tail(
    vec3_t head_world,
    vec3_t tail_world,
    const mat4_t *parent_world_inv) {
    /* Compute bone direction in world space. */
    float dx = tail_world.x - head_world.x;
    float dy = tail_world.y - head_world.y;
    float dz = tail_world.z - head_world.z;
    float len = sqrtf(dx * dx + dy * dy + dz * dz);

    mat4_t world = mat4_identity();

    if (len > 1e-6f) {
        /* Bone Y axis = direction from head to tail (bone convention). */
        float inv_len = 1.0f / len;
        float yx = dx * inv_len;
        float yy = dy * inv_len;
        float yz = dz * inv_len;

        /* Choose a reference vector not parallel to Y for cross product. */
        float rx, ry, rz;
        if (fabsf(yy) < 0.99f) {
            rx = 0.0f; ry = 1.0f; rz = 0.0f; /* World up. */
        } else {
            rx = 1.0f; ry = 0.0f; rz = 0.0f; /* World right. */
        }

        /* Z = cross(Y_bone, ref) → normalized. */
        float zx = yy * rz - yz * ry;
        float zy = yz * rx - yx * rz;
        float zz = yx * ry - yy * rx;
        float zlen = sqrtf(zx * zx + zy * zy + zz * zz);
        if (zlen > 1e-6f) {
            float zi = 1.0f / zlen;
            zx *= zi; zy *= zi; zz *= zi;
        }

        /* X = cross(Y_bone, Z). */
        float xx = yy * zz - yz * zy;
        float xy = yz * zx - yx * zz;
        float xz = yx * zy - yy * zx;

        /* Build rotation matrix (column-major).
         * Column 0 = X, Column 1 = Y (bone dir), Column 2 = Z. */
        world.m[0]  = xx; world.m[1]  = xy; world.m[2]  = xz;
        world.m[4]  = yx; world.m[5]  = yy; world.m[6]  = yz;
        world.m[8]  = zx; world.m[9]  = zy; world.m[10] = zz;
    }

    /* Translation = head position in world space. */
    world.m[12] = head_world.x;
    world.m[13] = head_world.y;
    world.m[14] = head_world.z;

    /* Convert to parent-local space. */
    if (parent_world_inv) {
        return mat4_mul(*parent_world_inv, world);
    }
    return world;
}

void skeleton_builder_update_tail_positions(skeleton_def_t *skel) {
    if (!skel || skel->joint_count == 0) return;

    if (!skel->tail_positions) {
        skel->tail_positions = (float *)calloc(skel->joint_count * 3,
                                                 sizeof(float));
        if (!skel->tail_positions) return;
    }

    for (uint32_t i = 0; i < skel->joint_count; i++) {
        /* Default bone length: distance from this bone's rest_world
         * translation to the first child's rest_world translation.
         * If no child, use a default length of 0.5. */
        float bone_len = 0.5f;
        if (skel->parent_indices) {
            for (uint32_t c = 0; c < skel->joint_count; c++) {
                if (skel->parent_indices[c] == i) {
                    float cdx = skel->rest_world[c].m[12] -
                                skel->rest_world[i].m[12];
                    float cdy = skel->rest_world[c].m[13] -
                                skel->rest_world[i].m[13];
                    float cdz = skel->rest_world[c].m[14] -
                                skel->rest_world[i].m[14];
                    float cd = sqrtf(cdx*cdx + cdy*cdy + cdz*cdz);
                    if (cd > 0.01f) bone_len = cd;
                    break;
                }
            }
        }

        /* Tail = head + Y_axis * bone_length. */
        skel->tail_positions[i * 3 + 0] =
            skel->rest_world[i].m[12] + skel->rest_world[i].m[4] * bone_len;
        skel->tail_positions[i * 3 + 1] =
            skel->rest_world[i].m[13] + skel->rest_world[i].m[5] * bone_len;
        skel->tail_positions[i * 3 + 2] =
            skel->rest_world[i].m[14] + skel->rest_world[i].m[6] * bone_len;
    }
}
