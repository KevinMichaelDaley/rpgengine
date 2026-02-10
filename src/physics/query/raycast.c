/**
 * @file raycast.c
 * @brief Phase 5.1: Raycasts against primitive colliders.
 *
 * Non-static functions: phys_raycast, phys_raycast_all (2).
 */

#include "ferrum/physics/raycast.h"

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
#define RAYCAST_GRID_CELL_COUNT 256u
#define RAYCAST_GRID_CELL_SIZE 2.0f

static phys_vec3_t quat_rotate_vec3_(phys_quat_t q, phys_vec3_t v) {
    /* q assumed normalized. */
    phys_vec3_t u = (phys_vec3_t){q.x, q.y, q.z};
    float s = q.w;

    phys_vec3_t uv  = vec3_cross(u, v);
    phys_vec3_t uuv = vec3_cross(u, uv);

    uv  = vec3_scale(uv, 2.0f * s);
    uuv = vec3_scale(uuv, 2.0f);
    return vec3_add(v, vec3_add(uv, uuv));
}

static bool ray_sphere_hit_(phys_vec3_t origin, phys_vec3_t dir, float max_dist,
                           phys_vec3_t center, float radius,
                           float *t_out, phys_vec3_t *normal_out) {
    phys_vec3_t m = vec3_sub(origin, center);

    float b = vec3_dot(m, dir);
    float c = vec3_dot(m, m) - radius * radius;

    /* If origin outside sphere and pointing away, no hit. */
    if (c > 0.0f && b > 0.0f) {
        return false;
    }

    float disc = b * b - c;
    if (disc < 0.0f) {
        return false;
    }

    float t = -b - sqrtf(disc);
    if (t < 0.0f) {
        t = 0.0f;
    }

    if (t > max_dist) {
        return false;
    }

    phys_vec3_t p = vec3_add(origin, vec3_scale(dir, t));
    phys_vec3_t n = vec3_sub(p, center);
    n = vec3_normalize_safe(n, 1e-8f);

    if (t_out) {
        *t_out = t;
    }
    if (normal_out) {
        *normal_out = n;
    }
    return true;
}

static bool ray_box_hit_(phys_vec3_t origin, phys_vec3_t dir, float max_dist,
                        phys_vec3_t center, phys_quat_t rotation,
                        phys_vec3_t half_extents,
                        float *t_out, phys_vec3_t *normal_out) {
    phys_quat_t inv = PHYS_QUAT_FROM_QUAT(quat_conjugate(QUAT_FROM_PHYS_QUAT(rotation)));

    phys_vec3_t o_local = quat_rotate_vec3_(inv, vec3_sub(origin, center));
    phys_vec3_t d_local = quat_rotate_vec3_(inv, dir);

    float tmin = 0.0f;
    float tmax = max_dist;
    phys_vec3_t n_local = (phys_vec3_t){0, 0, 0};

    const float eps = 1e-8f;

    /* X slab */
    if (fabsf(d_local.x) < eps) {
        if (o_local.x < -half_extents.x || o_local.x > half_extents.x) {
            return false;
        }
    } else {
        float inv_d = 1.0f / d_local.x;
        float t1 = (-half_extents.x - o_local.x) * inv_d;
        float t2 = ( half_extents.x - o_local.x) * inv_d;
        float tn = fminf(t1, t2);
        float tf = fmaxf(t1, t2);
        if (tn > tmin) {
            tmin = tn;
            n_local = (phys_vec3_t){(t1 < t2) ? -1.0f : 1.0f, 0.0f, 0.0f};
        }
        tmax = fminf(tmax, tf);
        if (tmin > tmax) {
            return false;
        }
    }

    /* Y slab */
    if (fabsf(d_local.y) < eps) {
        if (o_local.y < -half_extents.y || o_local.y > half_extents.y) {
            return false;
        }
    } else {
        float inv_d = 1.0f / d_local.y;
        float t1 = (-half_extents.y - o_local.y) * inv_d;
        float t2 = ( half_extents.y - o_local.y) * inv_d;
        float tn = fminf(t1, t2);
        float tf = fmaxf(t1, t2);
        if (tn > tmin) {
            tmin = tn;
            n_local = (phys_vec3_t){0.0f, (t1 < t2) ? -1.0f : 1.0f, 0.0f};
        }
        tmax = fminf(tmax, tf);
        if (tmin > tmax) {
            return false;
        }
    }

    /* Z slab */
    if (fabsf(d_local.z) < eps) {
        if (o_local.z < -half_extents.z || o_local.z > half_extents.z) {
            return false;
        }
    } else {
        float inv_d = 1.0f / d_local.z;
        float t1 = (-half_extents.z - o_local.z) * inv_d;
        float t2 = ( half_extents.z - o_local.z) * inv_d;
        float tn = fminf(t1, t2);
        float tf = fmaxf(t1, t2);
        if (tn > tmin) {
            tmin = tn;
            n_local = (phys_vec3_t){0.0f, 0.0f, (t1 < t2) ? -1.0f : 1.0f};
        }
        tmax = fminf(tmax, tf);
        if (tmin > tmax) {
            return false;
        }
    }

    if (tmin > max_dist) {
        return false;
    }

    if (t_out) {
        *t_out = tmin;
    }
    if (normal_out) {
        *normal_out = vec3_normalize_safe(quat_rotate_vec3_(rotation, n_local), 1e-8f);
    }
    return true;
}

static bool ray_capsule_hit_(phys_vec3_t origin, phys_vec3_t dir, float max_dist,
                            phys_vec3_t center, phys_quat_t rotation,
                            float radius, float half_height,
                            float *t_out, phys_vec3_t *normal_out) {
    phys_quat_t inv = PHYS_QUAT_FROM_QUAT(quat_conjugate(QUAT_FROM_PHYS_QUAT(rotation)));

    phys_vec3_t o = quat_rotate_vec3_(inv, vec3_sub(origin, center));
    phys_vec3_t d = quat_rotate_vec3_(inv, dir);

    float best_t = INFINITY;
    phys_vec3_t best_n = (phys_vec3_t){0, 0, 0};

    /* Cylinder (x^2 + z^2 = r^2, y in [-h, h]). */
    float a = d.x * d.x + d.z * d.z;
    float b = 2.0f * (o.x * d.x + o.z * d.z);
    float c = o.x * o.x + o.z * o.z - radius * radius;
    if (a > 1e-12f) {
        float disc = b * b - 4.0f * a * c;
        if (disc >= 0.0f) {
            float sdisc = sqrtf(disc);
            float inv2a = 0.5f / a;
            float t0 = (-b - sdisc) * inv2a;
            float t1 = (-b + sdisc) * inv2a;
            if (t0 > t1) {
                float tmp = t0;
                t0 = t1;
                t1 = tmp;
            }

            float tc = t0;
            if (tc < 0.0f) {
                tc = t1;
            }
            if (tc >= 0.0f && tc <= max_dist) {
                float y = o.y + tc * d.y;
                if (y >= -half_height && y <= half_height) {
                    best_t = tc;
                    phys_vec3_t p = vec3_add(o, vec3_scale(d, tc));
                    best_n = vec3_normalize_safe((phys_vec3_t){p.x, 0.0f, p.z}, 1e-8f);
                }
            }
        }
    }

    /* Spherical caps. */
    float ts = 0.0f;
    phys_vec3_t ns = {0, 0, 0};
    if (ray_sphere_hit_(o, d, max_dist, (phys_vec3_t){0.0f, half_height, 0.0f}, radius, &ts, &ns)) {
        if (ts < best_t) {
            best_t = ts;
            best_n = ns;
        }
    }
    if (ray_sphere_hit_(o, d, max_dist, (phys_vec3_t){0.0f, -half_height, 0.0f}, radius, &ts, &ns)) {
        if (ts < best_t) {
            best_t = ts;
            best_n = ns;
        }
    }

    if (!isfinite(best_t) || best_t > max_dist) {
        return false;
    }

    if (t_out) {
        *t_out = best_t;
    }
    if (normal_out) {
        *normal_out = vec3_normalize_safe(quat_rotate_vec3_(rotation, best_n), 1e-8f);
    }
    return true;
}

static void sorted_insert_hit_(phys_raycast_hit_t *hits, uint32_t *count,
                              uint32_t max_hits, const phys_raycast_hit_t *h) {
    uint32_t n = *count;
    if (n < max_hits) {
        hits[n] = *h;
        n++;
    } else {
        if (h->distance >= hits[max_hits - 1].distance) {
            return;
        }
        hits[max_hits - 1] = *h;
        n = max_hits;
    }

    uint32_t i = n - 1;
    while (i > 0 && hits[i].distance < hits[i - 1].distance) {
        phys_raycast_hit_t tmp = hits[i - 1];
        hits[i - 1] = hits[i];
        hits[i] = tmp;
        i--;
    }

    *count = n;
}

static uint32_t layer_bit_ok_(uint8_t layer_id, uint32_t mask) {
    if (layer_id >= 32u) {
        return 0u;
    }
    return (mask & (1u << layer_id)) ? 1u : 0u;
}

bool phys_raycast(const struct phys_world *world, const phys_ray_t *ray,
                  phys_raycast_hit_t *hit, uint32_t layer_mask) {
    if (!world || !ray) {
        return false;
    }
    if (ray->max_distance <= 0.0f) {
        return false;
    }
    if (vec3_magnitude(ray->direction) <= 1e-8f) {
        return false;
    }

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
    phys_spatial_grid_init(&grid, RAYCAST_GRID_CELL_COUNT, RAYCAST_GRID_CELL_SIZE,
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

    phys_vec3_t end = vec3_add(ray->origin, vec3_scale(ray->direction, ray->max_distance));
    phys_aabb_t ray_aabb;
    ray_aabb.min = (phys_vec3_t){fminf(ray->origin.x, end.x), fminf(ray->origin.y, end.y), fminf(ray->origin.z, end.z)};
    ray_aabb.max = (phys_vec3_t){fmaxf(ray->origin.x, end.x), fmaxf(ray->origin.y, end.y), fmaxf(ray->origin.z, end.z)};

    const float pad = 1e-4f;
    ray_aabb.min.x -= pad; ray_aabb.min.y -= pad; ray_aabb.min.z -= pad;
    ray_aabb.max.x += pad; ray_aabb.max.y += pad; ray_aabb.max.z += pad;

    uint32_t *candidates = phys_frame_arena_alloc((phys_frame_arena_t *)&world->frame_arena,
                                                  body_cap * sizeof(uint32_t),
                                                  _Alignof(uint32_t));
    if (!candidates && body_cap > 0) {
        (void)arena_pop_to_mark(&world->frame_arena.arena, mark);
        return false;
    }

    uint32_t cand_count = phys_spatial_grid_query(&grid, &ray_aabb, candidates, body_cap);

    bool found = false;
    phys_raycast_hit_t best;
    best.distance = INFINITY;
    best.body_id = UINT32_MAX;

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

        float t = 0.0f;
        phys_vec3_t n = {0, 0, 0};
        bool hit_this = false;

        if (c->type == PHYS_SHAPE_SPHERE && c->shape_index < world->sphere_count) {
            float r = world->spheres[c->shape_index].radius;
            hit_this = ray_sphere_hit_(ray->origin, ray->direction, ray->max_distance, center, r, &t, &n);
        } else if (c->type == PHYS_SHAPE_BOX && c->shape_index < world->box_count) {
            phys_vec3_t he = world->boxes[c->shape_index].half_extents;
            hit_this = ray_box_hit_(ray->origin, ray->direction, ray->max_distance, center, rot, he, &t, &n);
        } else if (c->type == PHYS_SHAPE_CAPSULE && c->shape_index < world->capsule_count) {
            phys_capsule_t cap = world->capsules[c->shape_index];
            hit_this = ray_capsule_hit_(ray->origin, ray->direction, ray->max_distance, center, rot,
                                        cap.radius, cap.half_height, &t, &n);
        }

        if (!hit_this) {
            continue;
        }

        if (t < best.distance) {
            best.distance = t;
            best.point = vec3_add(ray->origin, vec3_scale(ray->direction, t));
            best.normal = n;
            best.body_id = body_id;
            found = true;
        }
    }

    if (found && hit) {
        *hit = best;
    }

    (void)arena_pop_to_mark(&world->frame_arena.arena, mark);
    return found;
}

uint32_t phys_raycast_all(const struct phys_world *world, const phys_ray_t *ray,
                          phys_raycast_hit_t *hits, uint32_t max_hits,
                          uint32_t layer_mask) {
    if (!world || !ray || !hits || max_hits == 0) {
        return 0;
    }
    if (ray->max_distance <= 0.0f) {
        return 0;
    }
    if (vec3_magnitude(ray->direction) <= 1e-8f) {
        return 0;
    }

    for (uint32_t i = 0; i < max_hits; i++) {
        hits[i].distance = INFINITY;
        hits[i].body_id = UINT32_MAX;
    }

    size_t mark = arena_mark(&world->frame_arena.arena);

    uint32_t body_cap = world->body_pool.capacity;
    phys_aabb_t *aabbs = phys_frame_arena_alloc((phys_frame_arena_t *)&world->frame_arena,
                                                body_cap * sizeof(phys_aabb_t),
                                                _Alignof(phys_aabb_t));
    if (!aabbs && body_cap > 0) {
        (void)arena_pop_to_mark(&world->frame_arena.arena, mark);
        return 0;
    }

    phys_spatial_grid_t grid;
    phys_spatial_grid_init(&grid, RAYCAST_GRID_CELL_COUNT, RAYCAST_GRID_CELL_SIZE,
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

    phys_vec3_t end = vec3_add(ray->origin, vec3_scale(ray->direction, ray->max_distance));
    phys_aabb_t ray_aabb;
    ray_aabb.min = (phys_vec3_t){fminf(ray->origin.x, end.x), fminf(ray->origin.y, end.y), fminf(ray->origin.z, end.z)};
    ray_aabb.max = (phys_vec3_t){fmaxf(ray->origin.x, end.x), fmaxf(ray->origin.y, end.y), fmaxf(ray->origin.z, end.z)};

    const float pad = 1e-4f;
    ray_aabb.min.x -= pad; ray_aabb.min.y -= pad; ray_aabb.min.z -= pad;
    ray_aabb.max.x += pad; ray_aabb.max.y += pad; ray_aabb.max.z += pad;

    uint32_t *candidates = phys_frame_arena_alloc((phys_frame_arena_t *)&world->frame_arena,
                                                  body_cap * sizeof(uint32_t),
                                                  _Alignof(uint32_t));
    if (!candidates && body_cap > 0) {
        (void)arena_pop_to_mark(&world->frame_arena.arena, mark);
        return 0;
    }

    uint32_t cand_count = phys_spatial_grid_query(&grid, &ray_aabb, candidates, body_cap);

    uint32_t out_count = 0;
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

        float t = 0.0f;
        phys_vec3_t n = {0, 0, 0};
        bool hit_this = false;

        if (c->type == PHYS_SHAPE_SPHERE && c->shape_index < world->sphere_count) {
            float r = world->spheres[c->shape_index].radius;
            hit_this = ray_sphere_hit_(ray->origin, ray->direction, ray->max_distance, center, r, &t, &n);
        } else if (c->type == PHYS_SHAPE_BOX && c->shape_index < world->box_count) {
            phys_vec3_t he = world->boxes[c->shape_index].half_extents;
            hit_this = ray_box_hit_(ray->origin, ray->direction, ray->max_distance, center, rot, he, &t, &n);
        } else if (c->type == PHYS_SHAPE_CAPSULE && c->shape_index < world->capsule_count) {
            phys_capsule_t cap = world->capsules[c->shape_index];
            hit_this = ray_capsule_hit_(ray->origin, ray->direction, ray->max_distance, center, rot,
                                        cap.radius, cap.half_height, &t, &n);
        }

        if (!hit_this) {
            continue;
        }

        phys_raycast_hit_t h;
        h.distance = t;
        h.point = vec3_add(ray->origin, vec3_scale(ray->direction, t));
        h.normal = n;
        h.body_id = body_id;

        sorted_insert_hit_(hits, &out_count, max_hits, &h);
    }

    (void)arena_pop_to_mark(&world->frame_arena.arena, mark);
    return out_count;
}
