#include "ferrum/renderer/cluster_grid.h"

#include <math.h>
#include <stddef.h>

void cluster_grid_init(cluster_grid_t *grid, cluster_config_t config,
                       uint32_t *offsets, uint32_t *counts, uint32_t *indices,
                       uint32_t index_capacity)
{
    if (grid == NULL) {
        return;
    }
    grid->config = config;
    grid->offsets = offsets;
    grid->counts = counts;
    grid->indices = indices;
    grid->index_capacity = index_capacity;
    grid->cluster_total = config.tiles_x * config.tiles_y * config.slices;
    grid->index_count = 0u;
}

/* Transform a world position into view space (column-major view matrix). */
static void view_transform(const float view[16], const float w[3], float out[3])
{
    for (int i = 0; i < 3; ++i) {
        out[i] = view[0 * 4 + i] * w[0] + view[1 * 4 + i] * w[1] +
                 view[2 * 4 + i] * w[2] + view[3 * 4 + i];
    }
}

/* View-space AABB of cluster (tx,ty,s): a screen tile between two log-spaced
 * depth slices, projected back through the perspective terms. */
static void cluster_aabb(const cluster_grid_t *g, const render_camera_t *cam,
                         uint32_t tx, uint32_t ty, uint32_t s, float lo[3],
                         float hi[3])
{
    const cluster_config_t *c = &g->config;
    float p00 = cam->proj[0], p11 = cam->proj[5];
    /* NDC tile bounds. */
    float nx0 = (float)tx / (float)c->tiles_x * 2.0f - 1.0f;
    float nx1 = (float)(tx + 1) / (float)c->tiles_x * 2.0f - 1.0f;
    float ny0 = (float)ty / (float)c->tiles_y * 2.0f - 1.0f;
    float ny1 = (float)(ty + 1) / (float)c->tiles_y * 2.0f - 1.0f;
    /* Log depth split (view z is negative, in front of the camera). */
    float ratio = c->far_plane / c->near_plane;
    float zn = -c->near_plane * powf(ratio, (float)s / (float)c->slices);
    float zf = -c->near_plane * powf(ratio, (float)(s + 1) / (float)c->slices);

    for (int i = 0; i < 3; ++i) { lo[i] = 1e30f; hi[i] = -1e30f; }
    float nxs[2] = { nx0, nx1 }, nys[2] = { ny0, ny1 }, zs[2] = { zn, zf };
    for (int a = 0; a < 2; ++a) for (int b = 0; b < 2; ++b) for (int d = 0; d < 2; ++d) {
        float z = zs[d];
        /* view_x = ndc_x * (-z) / p00 ; view_y = ndc_y * (-z) / p11. */
        float corner[3] = { nxs[a] * (-z) / p00, nys[b] * (-z) / p11, z };
        for (int k = 0; k < 3; ++k) {
            if (corner[k] < lo[k]) lo[k] = corner[k];
            if (corner[k] > hi[k]) hi[k] = corner[k];
        }
    }
}

/* Squared distance from a point to an AABB (0 if inside). */
static float aabb_point_dist2(const float lo[3], const float hi[3], const float p[3])
{
    float d2 = 0.0f;
    for (int i = 0; i < 3; ++i) {
        float v = p[i];
        if (v < lo[i]) d2 += (lo[i] - v) * (lo[i] - v);
        else if (v > hi[i]) d2 += (v - hi[i]) * (v - hi[i]);
    }
    return d2;
}

void cluster_grid_build(cluster_grid_t *grid, const render_camera_t *camera,
                        const render_light_t *lights, uint32_t n_lights)
{
    if (grid == NULL || camera == NULL || grid->offsets == NULL) {
        return;
    }
    grid->index_count = 0u;
    for (uint32_t s = 0; s < grid->config.slices; ++s) {
        for (uint32_t ty = 0; ty < grid->config.tiles_y; ++ty) {
            for (uint32_t tx = 0; tx < grid->config.tiles_x; ++tx) {
                uint32_t ci = cluster_grid_index(grid, tx, ty, s);
                float lo[3], hi[3];
                cluster_aabb(grid, camera, tx, ty, s, lo, hi);
                grid->offsets[ci] = grid->index_count;
                uint32_t count = 0u;
                for (uint32_t li = 0; li < n_lights; ++li) {
                    const render_light_t *l = &lights[li];
                    if ((l->flags & RENDER_LIGHT_FLAG_REALTIME) == 0u ||
                        l->kind == RENDER_LIGHT_AREA) {
                        continue;
                    }
                    int overlaps;
                    if (l->kind == RENDER_LIGHT_DIRECTIONAL) {
                        overlaps = 1; /* affects every cluster */
                    } else {
                        float vp[3];
                        view_transform(camera->view, l->position, vp);
                        float range = (l->range > 0.0f) ? l->range : 1e30f;
                        overlaps = aabb_point_dist2(lo, hi, vp) <= range * range;
                    }
                    if (overlaps && grid->index_count < grid->index_capacity) {
                        grid->indices[grid->index_count++] = li;
                        ++count;
                    }
                }
                grid->counts[ci] = count;
            }
        }
    }
}

void cluster_grid_build_points(cluster_grid_t *grid, const render_camera_t *camera,
                               const float *positions, uint32_t n_points,
                               float radius)
{
    if (grid == NULL || camera == NULL || grid->offsets == NULL) {
        return;
    }
    float r2 = radius * radius;
    grid->index_count = 0u;
    for (uint32_t s = 0; s < grid->config.slices; ++s) {
        for (uint32_t ty = 0; ty < grid->config.tiles_y; ++ty) {
            for (uint32_t tx = 0; tx < grid->config.tiles_x; ++tx) {
                uint32_t ci = cluster_grid_index(grid, tx, ty, s);
                float lo[3], hi[3];
                cluster_aabb(grid, camera, tx, ty, s, lo, hi);
                grid->offsets[ci] = grid->index_count;
                uint32_t count = 0u;
                for (uint32_t p = 0; p < n_points; ++p) {
                    float vp[3];
                    view_transform(camera->view, &positions[p * 3], vp);
                    if (aabb_point_dist2(lo, hi, vp) <= r2 &&
                        grid->index_count < grid->index_capacity) {
                        grid->indices[grid->index_count++] = p;
                        ++count;
                    }
                }
                grid->counts[ci] = count;
            }
        }
    }
}
