/**
 * @file snap_surface_cast.c
 * @brief Raycast against all scene entities for surface snap targeting.
 *
 * Iterates entity array, skipping inactive/hidden/excluded entities.
 * For each candidate, computes model matrix and tests ray against
 * the entity's snap mesh from the cache. Lazily generates snap meshes
 * for primitive entities (BOX, SPHERE, CAPSULE) on first access.
 *
 * Non-static functions (2 / 4 limit):
 *   snap_surface_cast_ray
 *   snap_surface_cast_entity
 */

#include "ferrum/editor/viewport/snap/snap_surface_cast.h"
#include "ferrum/editor/viewport/snap/snap_mesh_cache.h"
#include "ferrum/editor/viewport/snap/snap_raycast.h"
#include "ferrum/editor/edit_entity.h"
#include "ferrum/editor/edit_entity_matrix.h"
#include "ferrum/math/mat4.h"
#include "ferrum/math/quat.h"

#include <math.h>
#include <string.h>

/**
 * @brief Ensure a primitive entity has a snap mesh in the cache.
 *
 * For BOX, SPHERE, and CAPSULE entities, generates the mesh on first access.
 * MESH entities get their snap mesh from FVMA loading; others are skipped.
 */
static void ensure_snap_mesh_(snap_mesh_cache_t *cache,
                                uint32_t entity_id, uint32_t entity_type) {
    if (snap_mesh_cache_has(cache, entity_id)) return;

    switch (entity_type) {
    case EDIT_ENTITY_TYPE_BOX:
        snap_mesh_retain_box(cache, entity_id);
        break;
    case EDIT_ENTITY_TYPE_SPHERE:
        snap_mesh_retain_sphere(cache, entity_id);
        break;
    case EDIT_ENTITY_TYPE_CAPSULE:
        snap_mesh_retain_capsule(cache, entity_id);
        break;
    default:
        break;
    }
}

/**
 * @brief Analytical ray-plane intersection for halfspace entities.
 *
 * A halfspace is an infinite plane defined by the entity's position
 * (a point on the plane) and its rotated +Y axis (the plane normal).
 * Returns true if the ray hits the plane in front of the ray origin.
 */
static bool ray_vs_halfspace_(vec3_t origin, vec3_t dir,
                                const edit_entity_t *entity,
                                float *out_t, vec3_t *out_normal) {
    /* Halfspace normal = entity orientation applied to +Y. */
    vec3_t up = {0.0f, 1.0f, 0.0f};
    vec3_t normal = quat_rotate_vec3(entity->orientation, up);

    /* Plane point = entity position. */
    float denom = dir.x * normal.x + dir.y * normal.y + dir.z * normal.z;
    if (fabsf(denom) < 1e-8f) return false; /* Ray parallel to plane. */

    float dx = entity->pos[0] - origin.x;
    float dy = entity->pos[1] - origin.y;
    float dz = entity->pos[2] - origin.z;
    float t = (dx * normal.x + dy * normal.y + dz * normal.z) / denom;

    if (t < 0.0f) return false; /* Behind ray origin. */

    *out_t = t;
    *out_normal = normal;
    return true;
}

void snap_surface_cast_entity(vec3_t origin, vec3_t dir,
                                const struct edit_entity *entity,
                                uint32_t entity_id,
                                struct snap_mesh_cache *cache,
                                snap_hit_t *out) {
    memset(out, 0, sizeof(*out));
    out->valid = false;

    if (!entity || !cache) return;

    /* Halfspace: analytical ray-plane intersection (no mesh). */
    if (entity->type == EDIT_ENTITY_TYPE_HALFSPACE) {
        float t;
        vec3_t normal;
        if (ray_vs_halfspace_(origin, dir, entity, &t, &normal)) {
            out->valid = true;
            out->entity_id = entity_id;
            out->face_index = 0;
            out->distance = t;
            out->normal = normal;
            out->position.x = origin.x + dir.x * t;
            out->position.y = origin.y + dir.y * t;
            out->position.z = origin.z + dir.z * t;
        }
        return;
    }

    /* Lazily generate snap mesh for primitive types. */
    ensure_snap_mesh_(cache, entity_id, entity->type);

    const snap_mesh_t *mesh = snap_mesh_cache_get(cache, entity_id);
    if (!mesh) return;

    mat4_t model = edit_entity_build_model_matrix(entity);

    float t;
    uint32_t face_idx;
    vec3_t normal;
    if (snap_ray_vs_snap_mesh(origin, dir, mesh, &model,
                               &t, &face_idx, &normal)) {
        out->valid = true;
        out->entity_id = entity_id;
        out->face_index = face_idx;
        out->distance = t;
        out->normal = normal;
        /* Compute world-space hit position: origin + dir * t. */
        out->position.x = origin.x + dir.x * t;
        out->position.y = origin.y + dir.y * t;
        out->position.z = origin.z + dir.z * t;
    }
}

void snap_surface_cast_ray(vec3_t origin, vec3_t dir,
                             const struct edit_entity *entities,
                             uint32_t entity_count,
                             struct snap_mesh_cache *cache,
                             uint32_t exclude_id,
                             snap_hit_t *out) {
    memset(out, 0, sizeof(*out));
    out->valid = false;

    if (!entities || entity_count == 0 || !cache) return;

    float best_t = 1e30f;

    for (uint32_t i = 0; i < entity_count; ++i) {
        const edit_entity_t *ent = &entities[i];

        /* Skip inactive, hidden, and excluded entities. */
        if (!ent->active) continue;
        if (ent->hidden) continue;
        if (i == exclude_id) continue;

        snap_hit_t hit;
        snap_surface_cast_entity(origin, dir, ent, i, cache, &hit);

        if (hit.valid && hit.distance < best_t) {
            best_t = hit.distance;
            *out = hit;
        }
    }
}
