/**
 * @file gi_collider_pose.c
 * @brief Build posed world-space GI collider proxies from canonical primitives
 *        (rpg-85as). Pure math -- no GL.
 */
#include <string.h>

#include "ferrum/renderer/gi/gi_collider_pose.h"

/* Rotate v by quaternion q (x,y,z,w). */
static void quat_rot(const float q[4], const float v[3], float o[3])
{
    float x = q[0], y = q[1], z = q[2], w = q[3];
    float tx = 2.0f * (y * v[2] - z * v[1]);
    float ty = 2.0f * (z * v[0] - x * v[2]);
    float tz = 2.0f * (x * v[1] - y * v[0]);
    o[0] = v[0] + w * tx + (y * tz - z * ty);
    o[1] = v[1] + w * ty + (z * tx - x * tz);
    o[2] = v[2] + w * tz + (x * ty - y * tx);
}

/* Transform a point by a 4x4 column-major matrix (identity when m == NULL). */
static void xform_pt(const float *m, const float p[3], float o[3])
{
    if (m == NULL) { o[0] = p[0]; o[1] = p[1]; o[2] = p[2]; return; }
    o[0] = m[0] * p[0] + m[4] * p[1] + m[8]  * p[2] + m[12];
    o[1] = m[1] * p[0] + m[5] * p[1] + m[9]  * p[2] + m[13];
    o[2] = m[2] * p[0] + m[6] * p[1] + m[10] * p[2] + m[14];
}

/* Pick the world transform for a primitive: its posed bone, else the owner. */
static const float *pick_xform(const fr_collider_prim_t *pr, const float *owner,
                               const float *bones, uint32_t n_bones)
{
    if (pr->bone >= 0 && bones != NULL && (uint32_t)pr->bone < n_bones)
        return &bones[(uint32_t)pr->bone * 16u];
    return owner;
}

/* Box/convex/mesh -> world AABB proxy from the 8 local corners. */
static void build_box_aabb(const fr_collider_prim_t *pr, const float *m,
                           gi_collider_t *out)
{
    float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
    for (int c = 0; c < 8; ++c) {
        float local[3] = {
            pr->offset[0] + ((c & 1) ? pr->half_extents[0] : -pr->half_extents[0]),
            pr->offset[1] + ((c & 2) ? pr->half_extents[1] : -pr->half_extents[1]),
            pr->offset[2] + ((c & 4) ? pr->half_extents[2] : -pr->half_extents[2]),
        };
        /* Rotate the corner about the offset by the primitive's local rotation. */
        float rel[3] = { local[0] - pr->offset[0], local[1] - pr->offset[1],
                         local[2] - pr->offset[2] };
        float rr[3]; quat_rot(pr->rotation, rel, rr);
        float lc[3] = { pr->offset[0] + rr[0], pr->offset[1] + rr[1],
                        pr->offset[2] + rr[2] };
        float w[3]; xform_pt(m, lc, w);
        for (int a = 0; a < 3; ++a) {
            if (w[a] < lo[a]) lo[a] = w[a];
            if (w[a] > hi[a]) hi[a] = w[a];
        }
    }
    out->kind = GI_COLLIDER_BOX;
    for (int a = 0; a < 3; ++a) {
        out->a[a] = 0.5f * (lo[a] + hi[a]);
        out->ext[a] = 0.5f * (hi[a] - lo[a]);
    }
}

uint32_t gi_collider_pose_build(const fr_collider_prim_t *prims, uint32_t n_prims,
                                const float *owner_xform, const float *bone_xforms,
                                uint32_t n_bones, gi_collider_t *out,
                                uint32_t out_cap)
{
    if (prims == NULL || out == NULL) return 0u;
    uint32_t k = 0;
    for (uint32_t i = 0; i < n_prims && k < out_cap; ++i) {
        const fr_collider_prim_t *pr = &prims[i];
        const float *m = pick_xform(pr, owner_xform, bone_xforms, n_bones);
        gi_collider_t *o = &out[k];
        memset(o, 0, sizeof *o);

        if (pr->kind == FR_COLLIDER_PRIM_SPHERE) {
            xform_pt(m, pr->offset, o->a);
            o->kind = GI_COLLIDER_SPHERE;
            o->ext[0] = pr->radius;
        } else if (pr->kind == FR_COLLIDER_PRIM_CAPSULE) {
            float hlocal[3] = { 0.0f, pr->half_height, 0.0f };
            float hr[3]; quat_rot(pr->rotation, hlocal, hr);
            float la[3] = { pr->offset[0] - hr[0], pr->offset[1] - hr[1],
                            pr->offset[2] - hr[2] };
            float lb[3] = { pr->offset[0] + hr[0], pr->offset[1] + hr[1],
                            pr->offset[2] + hr[2] };
            xform_pt(m, la, o->a);
            xform_pt(m, lb, o->b);
            o->kind = GI_COLLIDER_CAPSULE;
            o->ext[0] = pr->radius;
        } else if (pr->kind == FR_COLLIDER_PRIM_BOX ||
                   pr->kind == FR_COLLIDER_PRIM_CONVEX ||
                   pr->kind == FR_COLLIDER_PRIM_MESH ||
                   pr->kind == FR_COLLIDER_PRIM_COMPOUND) {
            build_box_aabb(pr, m, o);
        } else {
            continue;   /* HALFSPACE (static) / POINT (volumeless) -> skip. */
        }
        ++k;
    }
    return k;
}
