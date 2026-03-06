/**
 * @file bone_to_body.c
 * @brief Adapter: sync skeleton bone matrices to/from physics bodies.
 *
 * Extracts position and orientation from column-major 4×4 matrices
 * and writes them into phys_body_t fields, and vice versa.  Also
 * applies collider-derived mass, inertia, and flags when syncing
 * bones → bodies.
 *
 * Non-static functions: 2 (anim_bones_to_bodies, anim_bodies_to_bones)
 */

#include "ferrum/animation/bone_to_body.h"
#include "ferrum/math/quat.h"
#include <math.h>
#include <stddef.h>

void anim_bones_to_bodies(const mat4_t *world_pose,
                          const bone_collider_desc_t *colliders,
                          phys_body_t *bodies,
                          uint32_t count) {
    if (!world_pose || !bodies || count == 0) return;

    for (uint32_t i = 0; i < count; i++) {
        /* Extract position from column 3 (translation). */
        bodies[i].position = (phys_vec3_t){
            world_pose[i].m[12],
            world_pose[i].m[13],
            world_pose[i].m[14]
        };

        /* Extract orientation from upper-left 3×3. */
        bodies[i].orientation = quat_from_mat4(&world_pose[i]);

        /* Apply collider properties if provided. */
        if (!colliders) continue;
        const bone_collider_desc_t *col = &colliders[i];

        /* Mass and kinematic flag. */
        float body_mass = (col->mass > 0.0f) ? col->mass : 1.0f;
        if (col->is_kinematic) {
            bodies[i].flags |= PHYS_BODY_FLAG_KINEMATIC;
            bodies[i].inv_mass = 0.0f;
        } else {
            phys_body_set_mass(&bodies[i], body_mass);
        }

        /* CCD flag. */
        if (col->ccd_enabled) {
            bodies[i].flags |= PHYS_BODY_FLAG_CCD;
        }

        /* Shape-specific inertia. */
        switch (col->shape_type) {
        case BONE_COLLIDER_CAPSULE: {
            float r = col->params[0] > 0.0f ? col->params[0] : 0.05f;
            float h = col->params[1] > 0.0f ? col->params[1] : 0.2f;
            phys_body_set_capsule_inertia(&bodies[i], body_mass, r, h * 0.5f);
            break;
        }
        case BONE_COLLIDER_BOX: {
            phys_vec3_t he = {
                col->params[0] > 0.0f ? col->params[0] : 0.1f,
                col->params[1] > 0.0f ? col->params[1] : 0.1f,
                col->params[2] > 0.0f ? col->params[2] : 0.1f
            };
            phys_body_set_box_inertia(&bodies[i], body_mass, he);
            break;
        }
        case BONE_COLLIDER_SPHERE: {
            float r = col->params[0] > 0.0f ? col->params[0] : 0.1f;
            phys_body_set_sphere_inertia(&bodies[i], body_mass, r);
            break;
        }
        default:
            /* NONE or convex hull — default box approximation. */
            phys_body_set_box_inertia(&bodies[i], body_mass,
                                       (phys_vec3_t){0.1f, 0.5f, 0.1f});
            break;
        }
    }
}

void anim_bodies_to_bones(const phys_body_t *bodies,
                          mat4_t *world_pose,
                          uint32_t count) {
    if (!bodies || !world_pose || count == 0) return;

    for (uint32_t i = 0; i < count; i++) {
        /* Convert orientation quaternion → rotation matrix. */
        mat4_t rot;
        quat_to_mat4(bodies[i].orientation, &rot);

        /* Write translation from body position. */
        rot.m[12] = bodies[i].position.x;
        rot.m[13] = bodies[i].position.y;
        rot.m[14] = bodies[i].position.z;

        world_pose[i] = rot;
    }
}
