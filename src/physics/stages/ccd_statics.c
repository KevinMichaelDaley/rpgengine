/**
 * @file ccd_statics.c
 * @brief CCD sweep of dynamic bodies against ALL static colliders via GJK.
 *
 * Provides depenetration and swept-TOI tests for any dynamic convex
 * shape (sphere, capsule, box) against every static body regardless
 * of collider type (sphere, box, capsule, convex hull, compound).
 *
 * Non-static functions (2):
 *   1. ccd_depenetrate_vs_statics
 *   2. ccd_sweep_vs_statics
 */

#include "ferrum/physics/ccd.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "ferrum/math/quat.h"
#include "ferrum/math/vec3.h"
#include "ferrum/physics/body.h"
#include "ferrum/physics/collider.h"
#include "ferrum/physics/convex_compound.h"
#include "ferrum/physics/convex_hull.h"
#include "ferrum/physics/gjk_epa.h"

/* ── Support function helpers ─────────────────────────────────── */

typedef struct { phys_vec3_t center; float radius; } sphere_sd_t;
typedef struct { phys_vec3_t center; phys_quat_t rot; phys_vec3_t he; } box_sd_t;
typedef struct { phys_vec3_t center; phys_quat_t rot; float r; float hh; } cap_sd_t;
typedef struct {
    const phys_convex_hull_t *hull;
    phys_vec3_t center;
    phys_quat_t rot;
} hull_sd_t;

static phys_vec3_t sup_sphere(const void *d, phys_vec3_t dir) {
    const sphere_sd_t *s = d;
    float len = sqrtf(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
    if (len < 1e-10f) return s->center;
    float inv = s->radius / len;
    return (phys_vec3_t){
        s->center.x + dir.x * inv,
        s->center.y + dir.y * inv,
        s->center.z + dir.z * inv};
}

static phys_vec3_t sup_box(const void *d, phys_vec3_t dir) {
    const box_sd_t *b = d;
    phys_vec3_t ld = quat_inv_rotate_vec3(b->rot, dir);
    phys_vec3_t ls = {
        ld.x >= 0 ? b->he.x : -b->he.x,
        ld.y >= 0 ? b->he.y : -b->he.y,
        ld.z >= 0 ? b->he.z : -b->he.z};
    phys_vec3_t ws = quat_rotate_vec3(b->rot, ls);
    return (phys_vec3_t){
        b->center.x + ws.x, b->center.y + ws.y, b->center.z + ws.z};
}

static phys_vec3_t sup_capsule(const void *d, phys_vec3_t dir) {
    const cap_sd_t *c = d;
    phys_vec3_t ax = quat_rotate_vec3(c->rot, (phys_vec3_t){0, 1, 0});
    float dot = ax.x * dir.x + ax.y * dir.y + ax.z * dir.z;
    phys_vec3_t ep;
    if (dot >= 0)
        ep = (phys_vec3_t){
            c->center.x + ax.x * c->hh,
            c->center.y + ax.y * c->hh,
            c->center.z + ax.z * c->hh};
    else
        ep = (phys_vec3_t){
            c->center.x - ax.x * c->hh,
            c->center.y - ax.y * c->hh,
            c->center.z - ax.z * c->hh};
    float len = sqrtf(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
    if (len < 1e-10f) return ep;
    float inv = c->r / len;
    return (phys_vec3_t){
        ep.x + dir.x * inv, ep.y + dir.y * inv, ep.z + dir.z * inv};
}

static phys_vec3_t sup_hull(const void *d, phys_vec3_t dir) {
    const hull_sd_t *h = d;
    phys_vec3_t local_dir = quat_inv_rotate_vec3(h->rot, dir);
    phys_vec3_t local_sup = phys_convex_hull_support(h->hull, local_dir);
    phys_vec3_t ws = quat_rotate_vec3(h->rot, local_sup);
    return (phys_vec3_t){
        h->center.x + ws.x, h->center.y + ws.y, h->center.z + ws.z};
}

/* ── Packed shape for GJK queries ─────────────────────────────── */

typedef struct {
    phys_gjk_support_fn fn;
    /* Must be large enough for any support data struct. */
    char data[sizeof(hull_sd_t)];
} packed_shape_t;

/* ── Build support data for a static body's collider ──────────── */

static bool build_static_shape(
    const phys_ccd_args_t *args, uint32_t si, int hull_idx,
    packed_shape_t *out)
{
    const phys_collider_t *col = &args->colliders[si];
    const phys_body_t *body = &args->bodies_read[si];
    phys_vec3_t offset = quat_rotate_vec3(body->orientation, col->local_offset);
    phys_vec3_t center = vec3_add(body->position, offset);
    phys_quat_t rot = quat_mul(body->orientation, col->local_rotation);

    if (hull_idx >= 0) {
        if (!args->convex_hulls) return false;
        out->fn = sup_hull;
        hull_sd_t hd = {&args->convex_hulls[hull_idx], center, rot};
        memcpy(out->data, &hd, sizeof(hd));
        return true;
    }

    switch (col->type) {
    case PHYS_SHAPE_SPHERE: {
        const phys_sphere_t *s =
            &((const phys_sphere_t *)args->spheres)[col->shape_index];
        out->fn = sup_sphere;
        sphere_sd_t sd = {center, s->radius};
        memcpy(out->data, &sd, sizeof(sd));
        return true;
    }
    case PHYS_SHAPE_BOX: {
        const phys_box_t *b =
            &((const phys_box_t *)args->boxes)[col->shape_index];
        out->fn = sup_box;
        box_sd_t bd = {center, rot, b->half_extents};
        memcpy(out->data, &bd, sizeof(bd));
        return true;
    }
    case PHYS_SHAPE_CAPSULE: {
        const phys_capsule_t *c =
            &((const phys_capsule_t *)args->capsules)[col->shape_index];
        out->fn = sup_capsule;
        cap_sd_t cd = {center, rot, c->radius, c->half_height};
        memcpy(out->data, &cd, sizeof(cd));
        return true;
    }
    case PHYS_SHAPE_CONVEX: {
        if (!args->convex_hulls) return false;
        out->fn = sup_hull;
        hull_sd_t hd = {
            &args->convex_hulls[col->shape_index], center, rot};
        memcpy(out->data, &hd, sizeof(hd));
        return true;
    }
    default:
        return false;
    }
}

/* ── Build support data for a dynamic body at given pose ──────── */

static void build_dyn_shape(
    const phys_ccd_args_t *args, uint32_t di,
    phys_vec3_t pos, phys_quat_t orient,
    packed_shape_t *out)
{
    const phys_collider_t *col = &args->colliders[di];
    phys_vec3_t offset = quat_rotate_vec3(orient, col->local_offset);
    phys_vec3_t center = vec3_add(pos, offset);
    phys_quat_t rot = quat_mul(orient, col->local_rotation);

    switch (col->type) {
    case PHYS_SHAPE_CAPSULE: {
        const phys_capsule_t *c =
            &((const phys_capsule_t *)args->capsules)[col->shape_index];
        out->fn = sup_capsule;
        cap_sd_t cd = {center, rot, c->radius, c->half_height};
        memcpy(out->data, &cd, sizeof(cd));
        break;
    }
    case PHYS_SHAPE_BOX: {
        const phys_box_t *b =
            &((const phys_box_t *)args->boxes)[col->shape_index];
        out->fn = sup_box;
        box_sd_t bd = {center, rot, b->half_extents};
        memcpy(out->data, &bd, sizeof(bd));
        break;
    }
    default: {
        float radius = 0.0f;
        if (col->type == PHYS_SHAPE_SPHERE) {
            const phys_sphere_t *s =
                &((const phys_sphere_t *)args->spheres)[col->shape_index];
            radius = s->radius;
        }
        out->fn = sup_sphere;
        sphere_sd_t sd = {center, radius};
        memcpy(out->data, &sd, sizeof(sd));
        break;
    }
    }
}

/* ── GJK-based depenetration ──────────────────────────────────── */

static bool test_deepen_one(
    const packed_shape_t *dyn, const packed_shape_t *stat,
    float *depth_out, phys_vec3_t *normal_out)
{
    phys_gjk_result_t res;
    if (!phys_gjk_intersect(dyn->fn, dyn->data, stat->fn, stat->data, &res))
        return false;
    if (!phys_epa_penetration(dyn->fn, dyn->data, stat->fn, stat->data, &res))
        return false;
    *depth_out = res.penetration;
    *normal_out = res.normal;
    return true;
}

bool ccd_depenetrate_vs_statics(
    uint32_t di, const phys_ccd_args_t *args,
    float *depth_out, phys_vec3_t *normal_out)
{
    const phys_body_t *curr = &args->bodies_curr[di];
    packed_shape_t dyn;
    build_dyn_shape(args, di, curr->position, curr->orientation, &dyn);

    float deepest = 0.0f;
    phys_vec3_t best_n = {0, 1, 0};
    bool any = false;

    for (uint32_t si = 0; si < args->body_count; si++) {
        if (si == di) continue;
        const phys_body_t *sb = &args->bodies_read[si];
        if (!(sb->flags & PHYS_BODY_FLAG_STATIC)) continue;
        const phys_collider_t *col = &args->colliders[si];
        if (col->type == PHYS_SHAPE_MESH) continue;
        if (col->type == PHYS_SHAPE_HALFSPACE) continue;

        if (col->type == PHYS_SHAPE_COMPOUND) {
            if (!args->compounds || col->shape_index >= args->compound_count)
                continue;
            const phys_convex_compound_t *cc =
                &args->compounds[col->shape_index];
            for (uint32_t ci = 0; ci < cc->child_count; ci++) {
                packed_shape_t ss;
                if (!build_static_shape(
                        args, si, (int)cc->child_hull_indices[ci], &ss))
                    continue;
                float d; phys_vec3_t n;
                if (test_deepen_one(&dyn, &ss, &d, &n) && d > deepest) {
                    deepest = d; best_n = n; any = true;
                }
            }
        } else {
            packed_shape_t ss;
            if (!build_static_shape(args, si, -1, &ss)) continue;
            float d; phys_vec3_t n;
            if (test_deepen_one(&dyn, &ss, &d, &n) && d > deepest) {
                deepest = d; best_n = n; any = true;
            }
        }
    }
    if (any) { *depth_out = deepest; *normal_out = best_n; }
    return any;
}

/* ── GJK-based swept CCD (binary search TOI) ─────────────────── */

/**
 * March along the trajectory and find if ANY sample overlaps
 * the static shape.  If so, binary-search for the exact TOI.
 * This catches complete pass-through (where end position is clear).
 */
static bool march_and_bisect(
    const phys_ccd_args_t *args, uint32_t di,
    const phys_body_t *prev_b, const phys_body_t *curr_b,
    const packed_shape_t *stat,
    float *best_t, phys_vec3_t *best_n)
{
    phys_vec3_t disp = vec3_sub(curr_b->position, prev_b->position);

    /* Sample at 4 points along trajectory to detect pass-through. */
    static const float samples[] = {0.25f, 0.5f, 0.75f, 1.0f};
    bool hit = false;

    for (int s = 0; s < 4; s++) {
        phys_vec3_t pos = vec3_add(prev_b->position,
                                    vec3_scale(disp, samples[s]));
        packed_shape_t dyn_sample;
        build_dyn_shape(args, di, pos, curr_b->orientation, &dyn_sample);
        phys_gjk_result_t res;
        if (phys_gjk_intersect(dyn_sample.fn, dyn_sample.data,
                                stat->fn, stat->data, &res)) {
            hit = true;
            break;
        }
    }
    if (!hit) return false;

    /* Binary search to find precise TOI. */
    float lo = 0.0f, hi = 1.0f;
    for (int step = 0; step < 12; step++) {
        float mid = (lo + hi) * 0.5f;
        phys_vec3_t pos = vec3_add(prev_b->position,
                                    vec3_scale(disp, mid));
        packed_shape_t dyn_mid;
        build_dyn_shape(args, di, pos, curr_b->orientation, &dyn_mid);
        phys_gjk_result_t res;
        if (phys_gjk_intersect(dyn_mid.fn, dyn_mid.data,
                                stat->fn, stat->data, &res))
            hi = mid;
        else
            lo = mid;
    }

    if (lo < *best_t) {
        /* EPA at slightly past TOI for contact normal. */
        float t_epa = fminf(lo + 0.02f, 1.0f);
        phys_vec3_t epa_pos = vec3_add(prev_b->position,
                                        vec3_scale(disp, t_epa));
        packed_shape_t dyn_epa;
        build_dyn_shape(args, di, epa_pos, curr_b->orientation, &dyn_epa);
        phys_gjk_result_t epa_res;
        if (phys_gjk_intersect(dyn_epa.fn, dyn_epa.data,
                                stat->fn, stat->data, &epa_res) &&
            phys_epa_penetration(dyn_epa.fn, dyn_epa.data,
                                  stat->fn, stat->data, &epa_res)) {
            *best_n = epa_res.normal;
        } else {
            /* Fallback: normal from displacement direction. */
            float dlen = sqrtf(disp.x*disp.x + disp.y*disp.y + disp.z*disp.z);
            if (dlen > 1e-6f)
                *best_n = (phys_vec3_t){-disp.x/dlen, -disp.y/dlen, -disp.z/dlen};
            else
                *best_n = (phys_vec3_t){0, 1, 0};
        }
        *best_t = lo;
        return true;
    }
    return false;
}

bool ccd_sweep_vs_statics(
    uint32_t di, float bounding_radius, float margin,
    const phys_ccd_args_t *args,
    float *best_t_inout, phys_vec3_t *best_normal_out)
{
    const phys_body_t *prev_b = &args->bodies_prev[di];
    const phys_body_t *curr_b = &args->bodies_curr[di];
    bool any = false;
    float best_t = *best_t_inout;
    phys_vec3_t best_n = *best_normal_out;

    /* Sweep AABB for broad rejection. */
    float r = bounding_radius + margin;
    float sweep_min_x = fminf(prev_b->position.x, curr_b->position.x) - r;
    float sweep_min_y = fminf(prev_b->position.y, curr_b->position.y) - r;
    float sweep_min_z = fminf(prev_b->position.z, curr_b->position.z) - r;
    float sweep_max_x = fmaxf(prev_b->position.x, curr_b->position.x) + r;
    float sweep_max_y = fmaxf(prev_b->position.y, curr_b->position.y) + r;
    float sweep_max_z = fmaxf(prev_b->position.z, curr_b->position.z) + r;

    for (uint32_t si = 0; si < args->body_count; si++) {
        if (si == di) continue;
        const phys_body_t *sb = &args->bodies_read[si];
        if (!(sb->flags & PHYS_BODY_FLAG_STATIC)) continue;
        const phys_collider_t *col = &args->colliders[si];
        if (col->type == PHYS_SHAPE_MESH) continue;
        if (col->type == PHYS_SHAPE_HALFSPACE) continue;

        /* Broad AABB check against static body position. */
        float sx = sb->position.x, sy = sb->position.y, sz = sb->position.z;
        float broad_r = 20.0f;
        if (sx + broad_r < sweep_min_x || sx - broad_r > sweep_max_x) continue;
        if (sy + broad_r < sweep_min_y || sy - broad_r > sweep_max_y) continue;
        if (sz + broad_r < sweep_min_z || sz - broad_r > sweep_max_z) continue;

        if (col->type == PHYS_SHAPE_COMPOUND) {
            if (!args->compounds || col->shape_index >= args->compound_count)
                continue;
            const phys_convex_compound_t *cc =
                &args->compounds[col->shape_index];
            for (uint32_t ci = 0; ci < cc->child_count; ci++) {
                packed_shape_t ss;
                if (!build_static_shape(
                        args, si, (int)cc->child_hull_indices[ci], &ss))
                    continue;
                if (march_and_bisect(args, di, prev_b, curr_b, &ss,
                                      &best_t, &best_n))
                    any = true;
            }
        } else {
            packed_shape_t ss;
            if (!build_static_shape(args, si, -1, &ss)) continue;
            if (march_and_bisect(args, di, prev_b, curr_b, &ss,
                                  &best_t, &best_n))
                any = true;
        }
    }
    if (any) { *best_t_inout = best_t; *best_normal_out = best_n; }
    return any;
}
