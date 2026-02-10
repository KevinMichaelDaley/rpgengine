/**
 * @file closest_point.c
 * @brief Phase 5.3: Closest point queries.
 *
 * Non-static functions: phys_closest_point (1).
 */

#include "ferrum/physics/closest_point.h"

#include "ferrum/memory/arena.h"
#include "ferrum/physics/aabb.h"
#include "ferrum/physics/collider.h"
#include "ferrum/physics/spatial_grid.h"
#include "ferrum/physics/spatial_update.h"
#include "ferrum/physics/world.h"

#include "ferrum/math/quat.h"
#include "ferrum/math/vec3.h"

#include <math.h>

/* Keep in sync with src/physics/world/tick.c spatial grid tuning. */
#define CLOSEST_GRID_CELL_COUNT 256u
#define CLOSEST_GRID_CELL_SIZE 2.0f

static uint32_t layer_bit_ok_(uint8_t layer_id, uint32_t mask) {
    if (layer_id >= 32u) {
        return 0u;
    }
    return (mask & (1u << layer_id)) ? 1u : 0u;
}

static void sort_u32_(uint32_t *v, uint32_t n) {
    for (uint32_t i = 1; i < n; i++) {
        uint32_t key = v[i];
        uint32_t j = i;
        while (j > 0 && v[j - 1] > key) {
            v[j] = v[j - 1];
            j--;
        }
        v[j] = key;
    }
}

static float clampf_(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static phys_vec3_t quat_rotate_vec3_(phys_quat_t q, phys_vec3_t v) {
    phys_vec3_t u = (phys_vec3_t){q.x, q.y, q.z};
    float s = q.w;

    phys_vec3_t uv  = vec3_cross(u, v);
    phys_vec3_t uuv = vec3_cross(u, uv);

    uv  = vec3_scale(uv, 2.0f * s);
    uuv = vec3_scale(uuv, 2.0f);
    return vec3_add(v, vec3_add(uv, uuv));
}

static phys_vec3_t closest_point_sphere_(phys_vec3_t center, float radius, phys_vec3_t point) {
    phys_vec3_t d = vec3_sub(point, center);
    float len2 = vec3_dot(d, d);
    const float eps = 1e-12f;
    if (len2 <= eps) {
        return vec3_add(center, (phys_vec3_t){radius, 0.0f, 0.0f});
    }
    float inv_len = 1.0f / sqrtf(len2);
    return vec3_add(center, vec3_scale(d, radius * inv_len));
}

static phys_vec3_t closest_point_box_(phys_vec3_t center, phys_quat_t rot, phys_vec3_t he,
                                     phys_vec3_t point) {
    phys_vec3_t delta = vec3_sub(point, center);
    phys_quat_t inv = quat_conjugate(rot);
    phys_vec3_t local = quat_rotate_vec3_(inv, delta);

    phys_vec3_t clamped = (phys_vec3_t){
        clampf_(local.x, -he.x, he.x),
        clampf_(local.y, -he.y, he.y),
        clampf_(local.z, -he.z, he.z),
    };

    uint32_t inside =
        (fabsf(local.x) <= he.x) &&
        (fabsf(local.y) <= he.y) &&
        (fabsf(local.z) <= he.z);

    if (inside) {
        float dx = he.x - fabsf(local.x);
        float dy = he.y - fabsf(local.y);
        float dz = he.z - fabsf(local.z);

        if (dx <= dy && dx <= dz) {
            clamped.x = (local.x >= 0.0f) ? he.x : -he.x;
        } else if (dy <= dz) {
            clamped.y = (local.y >= 0.0f) ? he.y : -he.y;
        } else {
            clamped.z = (local.z >= 0.0f) ? he.z : -he.z;
        }
    }

    return vec3_add(center, quat_rotate_vec3_(rot, clamped));
}

static phys_vec3_t closest_point_capsule_(phys_vec3_t center, phys_quat_t rot,
                                         float radius, float half_height,
                                         phys_vec3_t point) {
    phys_vec3_t axis = quat_rotate_vec3_(rot, (phys_vec3_t){0.0f, 1.0f, 0.0f});
    phys_vec3_t p0 = vec3_add(center, vec3_scale(axis, -half_height));
    phys_vec3_t p1 = vec3_add(center, vec3_scale(axis,  half_height));

    phys_vec3_t seg = vec3_sub(p1, p0);
    float seg_len2 = vec3_dot(seg, seg);

    float t = 0.0f;
    if (seg_len2 > 1e-12f) {
        t = vec3_dot(vec3_sub(point, p0), seg) / seg_len2;
        t = clampf_(t, 0.0f, 1.0f);
    }

    phys_vec3_t closest_seg = vec3_add(p0, vec3_scale(seg, t));
    phys_vec3_t d = vec3_sub(point, closest_seg);
    float len2 = vec3_dot(d, d);

    if (len2 <= 1e-12f) {
        phys_vec3_t outward = quat_rotate_vec3_(rot, (phys_vec3_t){1.0f, 0.0f, 0.0f});
        return vec3_add(closest_seg, vec3_scale(outward, radius));
    }

    float inv_len = 1.0f / sqrtf(len2);
    return vec3_add(closest_seg, vec3_scale(d, radius * inv_len));
}

bool phys_closest_point(const struct phys_world *world, phys_vec3_t point,
                        float max_distance, phys_vec3_t *closest_out,
                        uint32_t *body_id_out, uint32_t layer_mask) {
    if (!world || !closest_out || !body_id_out) {
        return false;
    }
    if (layer_mask == 0) {
        return false;
    }
    if (!(max_distance >= 0.0f) || isinf(max_distance) || isnan(max_distance)) {
        return false;
    }

    float best_dist2 = max_distance * max_distance;
    uint32_t best_body = UINT32_MAX;
    phys_vec3_t best_point = (phys_vec3_t){0};

    phys_aabb_t query;
    query.min = vec3_sub(point, (phys_vec3_t){max_distance, max_distance, max_distance});
    query.max = vec3_add(point, (phys_vec3_t){max_distance, max_distance, max_distance});

    size_t mark = arena_mark(&world->frame_arena.arena);

    uint32_t body_cap = world->body_pool.capacity;
    phys_aabb_t *aabbs = phys_frame_arena_alloc((phys_frame_arena_t *)&world->frame_arena,
                                                body_cap * sizeof(phys_aabb_t),
                                                _Alignof(phys_aabb_t));
    if (!aabbs && body_cap > 0) {
        (void)arena_pop_to_mark(&world->frame_arena.arena, mark);
        return false;
    }

    phys_spatial_grid_t grid;
    phys_spatial_grid_init(&grid, CLOSEST_GRID_CELL_COUNT, CLOSEST_GRID_CELL_SIZE,
                           (phys_frame_arena_t *)&world->frame_arena);

    phys_stage_spatial_update(&(phys_spatial_update_args_t){
        .bodies      = world->body_pool.bodies_curr,
        .colliders   = world->colliders,
        .spheres     = world->spheres,
        .boxes       = world->boxes,
        .capsules    = world->capsules,
        .aabbs_out   = aabbs,
        .grid_out    = &grid,
        .active      = world->body_pool.active,
        .body_count  = body_cap,
    });

    uint32_t *candidates = phys_frame_arena_alloc((phys_frame_arena_t *)&world->frame_arena,
                                                  body_cap * sizeof(uint32_t),
                                                  _Alignof(uint32_t));
    if (!candidates && body_cap > 0) {
        (void)arena_pop_to_mark(&world->frame_arena.arena, mark);
        return false;
    }

    uint32_t cand_count = phys_spatial_grid_query(&grid, &query, candidates, body_cap);
    sort_u32_(candidates, cand_count);

    for (uint32_t ci = 0; ci < cand_count; ci++) {
        uint32_t body_id = candidates[ci];

        const phys_body_t *b = phys_body_pool_get_curr(&world->body_pool, body_id);
        if (!b) {
            continue;
        }

        const phys_collider_t *c = &world->colliders[body_id];
        if (!layer_bit_ok_(c->layer_id, layer_mask)) {
            continue;
        }

        phys_vec3_t center = phys_collider_world_center(c, b->position, b->orientation);
        phys_quat_t rot = phys_collider_world_rotation(c, b->orientation);

        phys_vec3_t cp;
        if (c->type == PHYS_SHAPE_SPHERE && c->shape_index < world->sphere_count) {
            float r = world->spheres[c->shape_index].radius;
            cp = closest_point_sphere_(center, r, point);
        } else if (c->type == PHYS_SHAPE_BOX && c->shape_index < world->box_count) {
            phys_vec3_t he = world->boxes[c->shape_index].half_extents;
            cp = closest_point_box_(center, rot, he, point);
        } else if (c->type == PHYS_SHAPE_CAPSULE && c->shape_index < world->capsule_count) {
            phys_capsule_t cap = world->capsules[c->shape_index];
            cp = closest_point_capsule_(center, rot, cap.radius, cap.half_height, point);
        } else {
            continue;
        }

        phys_vec3_t d = vec3_sub(point, cp);
        float dist2 = vec3_dot(d, d);

        if (dist2 <= best_dist2) {
            best_dist2 = dist2;
            best_body = body_id;
            best_point = cp;
        }
    }

    (void)arena_pop_to_mark(&world->frame_arena.arena, mark);

    if (best_body == UINT32_MAX) {
        return false;
    }

    *closest_out = best_point;
    *body_id_out = best_body;
    return true;
}
