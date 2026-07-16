/**
 * @file gi_sdf.c
 * @brief Combined dynamic SDF sampling (see gi_sdf.h).
 */
#include "ferrum/renderer/gi/gi_sdf.h"

#include <math.h>
#include <stddef.h>

#define GI_SDF_FAR 1e30f

static float vlen3(float x, float y, float z) { return sqrtf(x*x + y*y + z*z); }

static float sdf_sphere(const gi_collider_t *c, const float p[3])
{
    return vlen3(p[0]-c->a[0], p[1]-c->a[1], p[2]-c->a[2]) - c->ext[0];
}

static float sdf_box(const gi_collider_t *c, const float p[3])
{
    /* q = |p - centre| - half; outside = |max(q,0)|, inside = min(max component,0). */
    float q[3], omax = 0.0f, ninside = -1e30f;
    for (int i = 0; i < 3; ++i) {
        q[i] = fabsf(p[i] - c->a[i]) - c->ext[i];
        float qo = q[i] > 0.0f ? q[i] : 0.0f;
        omax += qo * qo;
        if (q[i] > ninside) ninside = q[i];
    }
    float outside = sqrtf(omax);
    float inside = ninside < 0.0f ? ninside : 0.0f;
    return outside + inside;
}

static float sdf_capsule(const gi_collider_t *c, const float p[3])
{
    float pa[3], ba[3];
    for (int i = 0; i < 3; ++i) { pa[i] = p[i]-c->a[i]; ba[i] = c->b[i]-c->a[i]; }
    float bb = ba[0]*ba[0] + ba[1]*ba[1] + ba[2]*ba[2];
    float h = bb > 1e-12f ? (pa[0]*ba[0]+pa[1]*ba[1]+pa[2]*ba[2]) / bb : 0.0f;
    if (h < 0.0f) h = 0.0f; else if (h > 1.0f) h = 1.0f;
    return vlen3(pa[0]-ba[0]*h, pa[1]-ba[1]*h, pa[2]-ba[2]*h) - c->ext[0];
}

float gi_collider_distance(const gi_collider_t *c, const float p[3])
{
    if (c == NULL || p == NULL)
        return GI_SDF_FAR;
    switch (c->kind) {
        case GI_COLLIDER_SPHERE:  return sdf_sphere(c, p);
        case GI_COLLIDER_BOX:     return sdf_box(c, p);
        case GI_COLLIDER_CAPSULE: return sdf_capsule(c, p);
        default:                  return GI_SDF_FAR;
    }
}

float gi_sdf_baked_sample(const float *dist, const int32_t dims[3],
                          const float origin[3], float voxel, const float p[3])
{
    if (dist == NULL || dims == NULL || origin == NULL || voxel <= 0.0f)
        return GI_SDF_FAR;
    /* Continuous grid coordinate; the sample straddles [gi, gi+1]. */
    float g[3]; int32_t g0[3];
    for (int a = 0; a < 3; ++a) {
        g[a] = (p[a] - origin[a]) / voxel;
        if (g[a] < 0.0f || g[a] > (float)(dims[a] - 1))
            return GI_SDF_FAR;                /* outside the grid. */
        g0[a] = (int32_t)g[a];
        if (g0[a] > dims[a] - 2 && dims[a] >= 2) g0[a] = dims[a] - 2;
        if (dims[a] < 2) g0[a] = 0;
    }
    float f[3];
    for (int a = 0; a < 3; ++a) f[a] = dims[a] < 2 ? 0.0f : g[a] - (float)g0[a];

    int32_t sx = 1, sy = dims[0], sz = dims[0] * dims[1];
    int32_t base = g0[0]*sx + g0[1]*sy + g0[2]*sz;
    int32_t dx = dims[0] < 2 ? 0 : sx;
    int32_t dy = dims[1] < 2 ? 0 : sy;
    int32_t dz = dims[2] < 2 ? 0 : sz;

    /* Trilinear blend of the 8 corners. */
    float c000 = dist[base], c100 = dist[base+dx];
    float c010 = dist[base+dy], c110 = dist[base+dx+dy];
    float c001 = dist[base+dz], c101 = dist[base+dx+dz];
    float c011 = dist[base+dy+dz], c111 = dist[base+dx+dy+dz];
    float x00 = c000 + (c100-c000)*f[0], x10 = c010 + (c110-c010)*f[0];
    float x01 = c001 + (c101-c001)*f[0], x11 = c011 + (c111-c011)*f[0];
    float y0 = x00 + (x10-x00)*f[1], y1 = x01 + (x11-x01)*f[1];
    return y0 + (y1-y0)*f[2];
}

float gi_sdf_combined(const float *dist, const int32_t dims[3],
                      const float origin[3], float voxel,
                      const gi_collider_t *colliders, uint32_t n,
                      const float p[3])
{
    float d = gi_sdf_baked_sample(dist, dims, origin, voxel, p);
    for (uint32_t i = 0; i < n; ++i) {
        float dc = gi_collider_distance(&colliders[i], p);
        if (dc < d) d = dc;
    }
    return d;
}
