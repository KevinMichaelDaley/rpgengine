/**
 * @file compound_collider.c
 * @brief Compound collider implementation (phys-003b).
 *
 * A compound collider groups primitive children (sphere, box, capsule)
 * with optional skeleton bone driving.  Each child stores inline shape
 * data so AABB computation is self-contained.
 */

#include "ferrum/physics/compound_collider.h"

#include <stddef.h>
#include <string.h>

#include "ferrum/math/quat.h"
#include "ferrum/math/vec3.h"

/* ── Static helpers ─────────────────────────────────────────────── */

/**
 * Rotate a vector by a unit quaternion.
 * v' = v + 2w(q_xyz × v) + 2(q_xyz × (q_xyz × v))
 */
static phys_vec3_t quat_rotate_vec3(phys_quat_t q, phys_vec3_t v)
{
    phys_vec3_t u = {q.x, q.y, q.z};
    float w = q.w;
    phys_vec3_t uv = vec3_cross(u, v);
    phys_vec3_t uuv = vec3_cross(u, uv);
    uv = vec3_scale(uv, 2.0f * w);
    uuv = vec3_scale(uuv, 2.0f);
    return vec3_add(v, vec3_add(uv, uuv));
}

/**
 * Compute world-space AABB for a single compound child, given the
 * body's world transform.  Dispatches to the appropriate
 * phys_aabb_from_* based on the child's shape type.
 */
static void child_aabb(const phys_compound_child_t *ch,
                       phys_vec3_t body_pos,
                       phys_quat_t body_rot,
                       phys_aabb_t *out)
{
    /* World center = body_pos + rotate(body_rot, local_offset). */
    phys_vec3_t world_center = vec3_add(body_pos,
                                        quat_rotate_vec3(body_rot,
                                                         ch->collider.local_offset));
    /* World rotation = body_rot * local_rotation. */
    phys_quat_t world_rot = quat_mul(body_rot, ch->collider.local_rotation);

    switch (ch->collider.type) {
    case PHYS_SHAPE_SPHERE:
        phys_aabb_from_sphere(out, world_center, ch->shape.sphere.radius);
        break;
    case PHYS_SHAPE_BOX:
        phys_aabb_from_box(out, world_center, world_rot,
                           ch->shape.box.half_extents);
        break;
    case PHYS_SHAPE_CAPSULE:
        phys_aabb_from_capsule(out, world_center, world_rot,
                               ch->shape.capsule.radius,
                               ch->shape.capsule.half_height);
        break;
    default:
        /* Unknown shape — produce a zero-size AABB at center. */
        phys_aabb_from_sphere(out, world_center, 0.0f);
        break;
    }
}

/* ── Public API (4 non-static functions) ────────────────────────── */

void phys_compound_init(phys_compound_collider_t *cc,
                        phys_compound_child_t *storage,
                        uint16_t max)
{
    if (!cc || !storage) { return; }

    cc->children     = storage;
    cc->child_count  = 0;
    cc->max_children = max;
    memset(&cc->cached_aabb, 0, sizeof(cc->cached_aabb));
}

void phys_compound_add_child(phys_compound_collider_t *cc,
                             const phys_collider_t *child,
                             const void *shape_data,
                             uint16_t bone)
{
    if (!cc || !child || !shape_data) { return; }
    if (cc->child_count >= cc->max_children) { return; }

    phys_compound_child_t *slot = &cc->children[cc->child_count];
    slot->collider   = *child;
    slot->bone_index = bone;
    slot->flags      = 0;

    /* Copy inline shape data based on collider type. */
    switch (child->type) {
    case PHYS_SHAPE_SPHERE:
        slot->shape.sphere = *(const phys_sphere_t *)shape_data;
        break;
    case PHYS_SHAPE_BOX:
        slot->shape.box = *(const phys_box_t *)shape_data;
        break;
    case PHYS_SHAPE_CAPSULE:
        slot->shape.capsule = *(const phys_capsule_t *)shape_data;
        break;
    default:
        /* Unsupported type — still add the child but shape data is undefined. */
        break;
    }

    cc->child_count++;
}

void phys_compound_update_transforms(phys_compound_collider_t *cc,
                                     const phys_quat_t *bone_rotations,
                                     const phys_vec3_t *bone_positions,
                                     uint16_t bone_count)
{
    if (!cc || !bone_rotations || !bone_positions) { return; }

    for (uint16_t i = 0; i < cc->child_count; ++i) {
        phys_compound_child_t *ch = &cc->children[i];
        uint16_t bi = ch->bone_index;

        /* Skip static children (0xFFFF) and out-of-range bones. */
        if (bi >= bone_count) { continue; }

        ch->collider.local_offset   = bone_positions[bi];
        ch->collider.local_rotation = bone_rotations[bi];
    }
}

void phys_compound_compute_aabb(const phys_compound_collider_t *cc,
                                phys_vec3_t body_pos,
                                phys_quat_t body_rot,
                                phys_aabb_t *out)
{
    if (!cc || !out) { return; }

    if (cc->child_count == 0) {
        /* No children — AABB is a zero-size box at body position. */
        phys_aabb_from_sphere(out, body_pos, 0.0f);
        return;
    }

    /* Start with the first child's AABB. */
    child_aabb(&cc->children[0], body_pos, body_rot, out);

    /* Merge remaining children. */
    for (uint16_t i = 1; i < cc->child_count; ++i) {
        phys_aabb_t tmp;
        child_aabb(&cc->children[i], body_pos, body_rot, &tmp);
        phys_aabb_merge(out, out, &tmp);
    }
}
