/**
 * @file ragdoll_sync.c
 * @brief Ragdoll transform synchronization (physics → bones).
 *
 * Reads body positions and orientations from the ragdoll's local
 * body array and writes them as bone world-space mat4 transforms.
 *
 * Non-static functions: 1 (ragdoll_sync_from_physics)
 */

#include "ferrum/animation/ragdoll.h"
#include "ferrum/math/mat4.h"
#include "ferrum/math/quat.h"

void ragdoll_sync_from_physics(ragdoll_t *ragdoll) {
    if (!ragdoll) return;
    for (uint32_t i = 0; i < ragdoll->bone_count; i++) {
        const phys_body_t *body = &ragdoll->bodies[i];

        /* Convert quaternion to rotation matrix. */
        mat4_t rot;
        quat_to_mat4(body->orientation, &rot);

        /* Set translation. */
        rot.m[12] = body->position.x;
        rot.m[13] = body->position.y;
        rot.m[14] = body->position.z;

        ragdoll->bone_world[i] = rot;
    }
}
