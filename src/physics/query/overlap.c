/**
 * @file overlap.c
 * @brief Phase 5.2: Shape overlap queries.
 *
 * Non-static functions: phys_overlap (1).
 */

#include "ferrum/physics/overlap.h"

#include "ferrum/memory/arena.h"
#include "ferrum/physics/aabb.h"
#include "ferrum/physics/collider.h"
#include "ferrum/physics/collision/box_box.h"
#include "ferrum/physics/collision/box_capsule.h"
#include "ferrum/physics/collision/capsule_capsule.h"
#include "ferrum/physics/manifold.h"
#include "ferrum/physics/narrowphase.h" /* sphere-vs-* */
#include "ferrum/physics/spatial_grid.h"
#include "ferrum/physics/spatial_update.h"
#include "ferrum/physics/world.h"

#include <math.h>

/* Keep in sync with src/physics/world/tick.c spatial grid tuning. */
#define OVERLAP_GRID_CELL_COUNT 256u
#define OVERLAP_GRID_CELL_SIZE 2.0f

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

static uint32_t overlap_query_aabb_(const phys_world_t *world, const phys_collider_t *shape,
                                   phys_vec3_t position, phys_quat_t rotation,
                                   phys_aabb_t *out) {
    if (!world || !shape || !out) {
        return 0u;
    }

    phys_vec3_t center = phys_collider_world_center(shape, position, rotation);
    phys_quat_t rot = phys_collider_world_rotation(shape, rotation);

    if (shape->type == PHYS_SHAPE_SPHERE && shape->shape_index < world->sphere_count) {
        float r = world->spheres[shape->shape_index].radius;
        phys_aabb_from_sphere(out, center, r);
        return 1u;
    }
    if (shape->type == PHYS_SHAPE_BOX && shape->shape_index < world->box_count) {
        phys_vec3_t he = world->boxes[shape->shape_index].half_extents;
        phys_aabb_from_box(out, center, rot, he);
        return 1u;
    }
    if (shape->type == PHYS_SHAPE_CAPSULE && shape->shape_index < world->capsule_count) {
        phys_capsule_t cap = world->capsules[shape->shape_index];
        phys_aabb_from_capsule(out, center, rot, cap.radius, cap.half_height);
        return 1u;
    }

    return 0u;
}

static uint32_t overlap_prim_(const phys_world_t *world,
                             const phys_collider_t *a, phys_vec3_t pos_a, phys_quat_t rot_a,
                             const phys_collider_t *b, phys_vec3_t pos_b, phys_quat_t rot_b) {
    if (!world || !a || !b) {
        return 0u;
    }

    phys_contact_point_t cp;

    /* Compute world-space poses for both colliders. */
    phys_vec3_t ca = phys_collider_world_center(a, pos_a, rot_a);
    phys_quat_t qa = phys_collider_world_rotation(a, rot_a);
    phys_vec3_t cb = phys_collider_world_center(b, pos_b, rot_b);
    phys_quat_t qb = phys_collider_world_rotation(b, rot_b);

    if (a->type == PHYS_SHAPE_SPHERE && a->shape_index < world->sphere_count) {
        float ra = world->spheres[a->shape_index].radius;

        if (b->type == PHYS_SHAPE_SPHERE && b->shape_index < world->sphere_count) {
            float rb = world->spheres[b->shape_index].radius;
            return phys_sphere_vs_sphere(ca, ra, cb, rb, &cp) ? 1u : 0u;
        }
        if (b->type == PHYS_SHAPE_BOX && b->shape_index < world->box_count) {
            phys_vec3_t he = world->boxes[b->shape_index].half_extents;
            return phys_sphere_vs_box(ca, ra, cb, qb, he, &cp) ? 1u : 0u;
        }
        if (b->type == PHYS_SHAPE_CAPSULE && b->shape_index < world->capsule_count) {
            phys_capsule_t cap = world->capsules[b->shape_index];
            return phys_sphere_vs_capsule(ca, ra, cb, qb, cap.radius, cap.half_height, &cp) ? 1u : 0u;
        }
        return 0u;
    }

    if (a->type == PHYS_SHAPE_BOX && a->shape_index < world->box_count) {
        phys_vec3_t ha = world->boxes[a->shape_index].half_extents;

        if (b->type == PHYS_SHAPE_SPHERE && b->shape_index < world->sphere_count) {
            float rb = world->spheres[b->shape_index].radius;
            return phys_sphere_vs_box(cb, rb, ca, qa, ha, &cp) ? 1u : 0u;
        }
        if (b->type == PHYS_SHAPE_BOX && b->shape_index < world->box_count) {
            phys_vec3_t hb = world->boxes[b->shape_index].half_extents;
            phys_contact_point_t cps[1];
            return phys_box_vs_box(ca, qa, ha, cb, qb, hb, cps, 1) > 0 ? 1u : 0u;
        }
        if (b->type == PHYS_SHAPE_CAPSULE && b->shape_index < world->capsule_count) {
            phys_capsule_t cap = world->capsules[b->shape_index];
            return phys_box_vs_capsule(ca, qa, ha, cb, qb, cap.radius, cap.half_height, &cp) ? 1u : 0u;
        }
        return 0u;
    }

    if (a->type == PHYS_SHAPE_CAPSULE && a->shape_index < world->capsule_count) {
        phys_capsule_t cap_a = world->capsules[a->shape_index];

        if (b->type == PHYS_SHAPE_SPHERE && b->shape_index < world->sphere_count) {
            float rb = world->spheres[b->shape_index].radius;
            return phys_sphere_vs_capsule(cb, rb, ca, qa, cap_a.radius, cap_a.half_height, &cp) ? 1u : 0u;
        }
        if (b->type == PHYS_SHAPE_BOX && b->shape_index < world->box_count) {
            phys_vec3_t hb = world->boxes[b->shape_index].half_extents;
            return phys_box_vs_capsule(cb, qb, hb, ca, qa, cap_a.radius, cap_a.half_height, &cp) ? 1u : 0u;
        }
        if (b->type == PHYS_SHAPE_CAPSULE && b->shape_index < world->capsule_count) {
            phys_capsule_t cap_b = world->capsules[b->shape_index];
            return phys_capsule_vs_capsule(ca, qa, cap_a.radius, cap_a.half_height,
                                           cb, qb, cap_b.radius, cap_b.half_height, &cp) ? 1u : 0u;
        }
        return 0u;
    }

    return 0u;
}

uint32_t phys_overlap(const struct phys_world *world, const struct phys_collider *shape,
                      phys_vec3_t position, phys_quat_t rotation,
                      uint32_t *body_ids_out, uint32_t max_results,
                      uint32_t layer_mask) {
    if (!world || !shape || !body_ids_out || max_results == 0) {
        return 0;
    }
    if (layer_mask == 0) {
        return 0;
    }

    phys_aabb_t query_aabb;
    if (!overlap_query_aabb_(world, shape, position, rotation, &query_aabb)) {
        return 0;
    }

    size_t mark = arena_mark(&world->frame_arena.arena);

    const phys_spatial_grid_t *gridp = NULL;
    phys_spatial_grid_t grid_tmp;

    uint32_t body_cap = world->body_pool.capacity;
    if (world->query_grid_valid && world->query_grid.cells) {
        gridp = &world->query_grid;
    } else {
        phys_aabb_t *aabbs = phys_frame_arena_alloc((phys_frame_arena_t *)&world->frame_arena,
                                                    body_cap * sizeof(phys_aabb_t),
                                                    _Alignof(phys_aabb_t));
        if (!aabbs && body_cap > 0) {
            (void)arena_pop_to_mark(&world->frame_arena.arena, mark);
            return 0;
        }

        phys_spatial_grid_init(&grid_tmp, OVERLAP_GRID_CELL_COUNT, OVERLAP_GRID_CELL_SIZE,
                               (phys_frame_arena_t *)&world->frame_arena);

        phys_stage_spatial_update(&(phys_spatial_update_args_t){
            .bodies      = world->body_pool.bodies_curr,
            .colliders   = world->colliders,
            .spheres     = world->spheres,
            .boxes       = world->boxes,
            .capsules    = world->capsules,
            .aabbs_out   = aabbs,
            .grid_out    = &grid_tmp,
            .active      = world->body_pool.active,
            .body_count  = body_cap,
        });

        gridp = &grid_tmp;
    }

    uint32_t *candidates = phys_frame_arena_alloc((phys_frame_arena_t *)&world->frame_arena,
                                                  body_cap * sizeof(uint32_t),
                                                  _Alignof(uint32_t));
    if (!candidates && body_cap > 0) {
        (void)arena_pop_to_mark(&world->frame_arena.arena, mark);
        return 0;
    }

    uint32_t cand_count = phys_spatial_grid_query(gridp, &query_aabb, candidates, body_cap);
    sort_u32_(candidates, cand_count);

    uint32_t out_count = 0;
    for (uint32_t ci = 0; ci < cand_count && out_count < max_results; ci++) {
        uint32_t body_id = candidates[ci];

        const phys_body_t *b = phys_body_pool_get_curr(&world->body_pool, body_id);
        if (!b) {
            continue;
        }

        const phys_collider_t *bc = &world->colliders[body_id];
        if (!layer_bit_ok_(bc->layer_id, layer_mask)) {
            continue;
        }

        if (overlap_prim_(world, shape, position, rotation, bc, b->position, b->orientation)) {
            body_ids_out[out_count++] = body_id;
        }
    }

    (void)arena_pop_to_mark(&world->frame_arena.arena, mark);
    return out_count;
}
