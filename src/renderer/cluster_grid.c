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

/* Precompute cap for probe view positions (kept off the frame-alloc path). */
#define CLUSTER_POINT_SCRATCH 1024u
/* Cap on the guaranteed-minimum K-nearest probe set per froxel. */
#define CLUSTER_MIN_PROBES_MAX 16u

void cluster_grid_build_points(cluster_grid_t *grid, const render_camera_t *camera,
                               const float *positions, uint32_t n_points,
                               uint32_t min_probes, float sphere_margin)
{
    if (grid == NULL || camera == NULL || grid->offsets == NULL) {
        return;
    }
    const cluster_config_t *c = &grid->config;

    /* Probe positions in VIEW space (same space as the froxel AABBs). */
    float vx[CLUSTER_POINT_SCRATCH], vy[CLUSTER_POINT_SCRATCH], vz[CLUSTER_POINT_SCRATCH];
    uint32_t np = n_points < CLUSTER_POINT_SCRATCH ? n_points : CLUSTER_POINT_SCRATCH;
    for (uint32_t p = 0; p < np; ++p) {
        float vp[3];
        view_transform(camera->view, &positions[p * 3], vp);
        vx[p] = vp[0]; vy[p] = vp[1]; vz[p] = vp[2];
    }
    uint32_t K = min_probes;
    if (K > CLUSTER_MIN_PROBES_MAX) K = CLUSTER_MIN_PROBES_MAX;
    if (K > np) K = np;

    grid->index_count = 0u;
    for (uint32_t s = 0; s < c->slices; ++s) {
        for (uint32_t ty = 0; ty < c->tiles_y; ++ty) {
            for (uint32_t tx = 0; tx < c->tiles_x; ++tx) {
                uint32_t ci = cluster_grid_index(grid, tx, ty, s);
                float lo[3], hi[3];
                cluster_aabb(grid, camera, tx, ty, s, lo, hi);
                float cx = 0.5f * (lo[0] + hi[0]), cy = 0.5f * (lo[1] + hi[1]),
                      cz = 0.5f * (lo[2] + hi[2]);
                /* Froxel bounding sphere: half the AABB diagonal, + margin. */
                float rx = 0.5f * (hi[0] - lo[0]), ry = 0.5f * (hi[1] - lo[1]),
                      rz = 0.5f * (hi[2] - lo[2]);
                float thr = sqrtf(rx * rx + ry * ry + rz * rz) + sphere_margin;
                float thr2 = thr * thr;

                /* One pass: distance of every probe to the froxel centre, tracking
                 * the K nearest (insertion-sorted) for the guaranteed minimum. */
                int nidx[CLUSTER_MIN_PROBES_MAX]; float nd2[CLUSTER_MIN_PROBES_MAX];
                for (uint32_t k = 0; k < K; ++k) { nidx[k] = -1; nd2[k] = 1e30f; }
                for (uint32_t p = 0; p < np; ++p) {
                    float dx = vx[p] - cx, dy = vy[p] - cy, dz = vz[p] - cz;
                    float d2 = dx * dx + dy * dy + dz * dz;
                    if (K > 0 && d2 < nd2[K - 1]) {
                        uint32_t j = K - 1;
                        while (j > 0 && nd2[j - 1] > d2) {
                            nd2[j] = nd2[j - 1]; nidx[j] = nidx[j - 1]; --j;
                        }
                        nd2[j] = d2; nidx[j] = (int)p;
                    }
                }

                grid->offsets[ci] = grid->index_count;
                uint32_t count = 0u;
                /* All probes inside the bounding sphere + margin. */
                for (uint32_t p = 0; p < np; ++p) {
                    float dx = vx[p] - cx, dy = vy[p] - cy, dz = vz[p] - cz;
                    if (dx * dx + dy * dy + dz * dz <= thr2 &&
                        grid->index_count < grid->index_capacity) {
                        grid->indices[grid->index_count++] = p; ++count;
                    }
                }
                /* Guarantee the K nearest even when they fall outside the sphere,
                 * so a froxel is never starved of probes as the camera moves. */
                for (uint32_t k = 0; k < K; ++k) {
                    if (nidx[k] < 0 || nd2[k] <= thr2) continue; /* already added. */
                    if (grid->index_count < grid->index_capacity) {
                        grid->indices[grid->index_count++] = (uint32_t)nidx[k]; ++count;
                    }
                }
                grid->counts[ci] = count;
            }
        }
    }
}
