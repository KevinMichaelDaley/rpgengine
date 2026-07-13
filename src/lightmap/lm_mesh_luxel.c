/**
 * @file lm_mesh_luxel.c
 * @brief Triangle lightmap-UV rasterization into luxels (see lm_mesh_luxel.h).
 */
#include "ferrum/lightmap/lm_mesh_luxel.h"

#include <math.h>
#include <string.h>

#include "ferrum/lightmap/lm_sh.h"

/* Barycentric weights of point (px,py) in triangle (a,b,c) given in the same
 * 2D space. Returns false if the triangle is degenerate. */
static bool lm_bary(float ax, float ay, float bx, float by, float cx, float cy,
                    float px, float py, float *wa, float *wb, float *wc)
{
    float det = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy);
    if (fabsf(det) < 1e-12f)
        return false;
    float inv = 1.0f / det;
    *wa = ((by - cy) * (px - cx) + (cx - bx) * (py - cy)) * inv;
    *wb = ((cy - ay) * (px - cx) + (ax - cx) * (py - cy)) * inv;
    *wc = 1.0f - *wa - *wb;
    return true;
}

/* Interpolate a vec3 attribute (stride-3 array) by barycentric weights. */
static vec3_t lm_bary_vec3(const float *attr, uint32_t i0, uint32_t i1,
                           uint32_t i2, float wa, float wb, float wc)
{
    vec3_t r;
    r.x = wa * attr[i0 * 3 + 0] + wb * attr[i1 * 3 + 0] + wc * attr[i2 * 3 + 0];
    r.y = wa * attr[i0 * 3 + 1] + wb * attr[i1 * 3 + 1] + wc * attr[i2 * 3 + 1];
    r.z = wa * attr[i0 * 3 + 2] + wb * attr[i1 * 3 + 2] + wc * attr[i2 * 3 + 2];
    return r;
}

uint32_t lm_mesh_luxelize(const lm_mesh_t *mesh, const lm_atlas_rect_t *rect,
                          uint32_t atlas_w, uint32_t atlas_h,
                          lm_luxel_t *out_luxels, uint32_t *out_ax,
                          uint32_t *out_ay, uint8_t *visited)
{
    (void)atlas_w;
    (void)atlas_h;
    memset(visited, 0, (size_t)rect->w * rect->h);
    uint32_t count = 0u;
    float rw = (float)rect->w, rh = (float)rect->h;

    for (uint32_t t = 0; t + 2 < mesh->index_count; t += 3) {
        uint32_t i0 = mesh->indices[t], i1 = mesh->indices[t + 1],
                 i2 = mesh->indices[t + 2];
        /* Triangle vertices in atlas-pixel space (uv1 [0,1] -> rect texels). */
        float ax = rect->x + mesh->uv1[i0 * 2] * rw;
        float ay = rect->y + mesh->uv1[i0 * 2 + 1] * rh;
        float bx = rect->x + mesh->uv1[i1 * 2] * rw;
        float by = rect->y + mesh->uv1[i1 * 2 + 1] * rh;
        float cx = rect->x + mesh->uv1[i2 * 2] * rw;
        float cy = rect->y + mesh->uv1[i2 * 2 + 1] * rh;

        /* Pixel bounding box clamped to the rect. */
        float lo_x = fminf(ax, fminf(bx, cx)), hi_x = fmaxf(ax, fmaxf(bx, cx));
        float lo_y = fminf(ay, fminf(by, cy)), hi_y = fmaxf(ay, fmaxf(by, cy));
        int x0 = (int)floorf(lo_x), x1 = (int)ceilf(hi_x);
        int y0 = (int)floorf(lo_y), y1 = (int)ceilf(hi_y);
        if (x0 < (int)rect->x) x0 = (int)rect->x;
        if (y0 < (int)rect->y) y0 = (int)rect->y;
        if (x1 > (int)(rect->x + rect->w)) x1 = (int)(rect->x + rect->w);
        if (y1 > (int)(rect->y + rect->h)) y1 = (int)(rect->y + rect->h);

        for (int py = y0; py < y1; ++py) {
            for (int px = x0; px < x1; ++px) {
                uint32_t lx = (uint32_t)px - rect->x, ly = (uint32_t)py - rect->y;
                uint8_t *vflag = &visited[ly * rect->w + lx];
                if (*vflag)
                    continue;
                float wa, wb, wc;
                if (!lm_bary(ax, ay, bx, by, cx, cy, (float)px + 0.5f,
                             (float)py + 0.5f, &wa, &wb, &wc))
                    continue;
                if (wa < -0.001f || wb < -0.001f || wc < -0.001f)
                    continue; /* pixel centre outside the triangle */
                *vflag = 1u;
                lm_luxel_t *lux = &out_luxels[count];
                lux->pos = lm_bary_vec3(mesh->positions, i0, i1, i2, wa, wb, wc);
                lux->normal = vec3_normalize_safe(
                    lm_bary_vec3(mesh->normals, i0, i1, i2, wa, wb, wc), 1e-6f);
                /* Diffuse reflectance + emissive from the material textures,
                 * sampled at the barycentric material-UV, times the mesh tint. */
                vec3_t alb = { 1.0f, 1.0f, 1.0f }, emi = { 1.0f, 1.0f, 1.0f };
                if (mesh->uv0 != NULL) {
                    float mu = wa*mesh->uv0[i0*2]   + wb*mesh->uv0[i1*2]   + wc*mesh->uv0[i2*2];
                    float mv = wa*mesh->uv0[i0*2+1] + wb*mesh->uv0[i1*2+1] + wc*mesh->uv0[i2*2+1];
                    if (mesh->albedo_image)   alb = lm_image_sample(mesh->albedo_image, mu, mv);
                    if (mesh->emissive_image) emi = lm_image_sample(mesh->emissive_image, mu, mv);
                }
                lux->albedo = (vec3_t){ alb.x*mesh->albedo.x, alb.y*mesh->albedo.y, alb.z*mesh->albedo.z };
                lux->emissive = (vec3_t){ emi.x*mesh->emissive.x, emi.y*mesh->emissive.y, emi.z*mesh->emissive.z };
                for (int c = 0; c < 3; ++c)
                    lm_sh9_zero(&lux->sh[c]);
                out_ax[count] = (uint32_t)px;
                out_ay[count] = (uint32_t)py;
                ++count;
            }
        }
    }
    return count;
}
