/**
 * @file convex_decompose.c
 * @brief Top-level convex decomposition: voxelize → ACD → hull build.
 *
 * Orchestrates the full pipeline:
 *   1. Compute mesh AABB, set up voxel grid
 *   2. Voxelize (surface + flood-fill interior)
 *   3. Collect filled voxel centers
 *   4. BFS split concave clusters (ACD) for even spatial coverage
 *   5. Build convex hull of each cluster
 *
 * Non-static functions (2):
 *   1. phys_decompose_params_default
 *   2. phys_decompose_mesh
 */

#include "ferrum/physics/convex_decompose.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ── External voxelization and ACD functions ───────────────────── */

extern void voxelize_mesh(uint8_t *grid, uint32_t res,
                          phys_vec3_t min_corner, float cell_size,
                          const phys_triangle_t *triangles,
                          uint32_t tri_count);

extern float acd_measure_concavity(const phys_vec3_t *centers, uint32_t count,
                                    float cell_size);

extern bool acd_split_cluster(phys_vec3_t *centers, uint32_t count,
                               uint32_t *split_count);

/* ── Cluster work item for iterative splitting ─────────────────── */

typedef struct cluster_range {
    uint32_t start;   /**< Start index in centers array. */
    uint32_t count;   /**< Number of voxels in this cluster. */
} cluster_range_t;

/* ── Public API ────────────────────────────────────────────────── */

phys_decompose_params_t phys_decompose_params_default(void) {
    return (phys_decompose_params_t){
        .resolution = 48,
        .concavity_threshold = 0.05f,
        .max_hulls = 32,
        .min_voxels = 4,
    };
}

int phys_decompose_mesh(const phys_triangle_t *triangles,
                        uint32_t tri_count,
                        const phys_decompose_params_t *params,
                        phys_decompose_result_t *result) {
    /* ── Validate inputs ──────────────────────────────────────── */
    if (!params || !result) return -1;
    if (!triangles && tri_count > 0) return -1;
    if (tri_count == 0) return -1;

    uint32_t res = params->resolution;
    if (res < 2) res = 2;
    if (res > PHYS_DECOMPOSE_MAX_RESOLUTION) res = PHYS_DECOMPOSE_MAX_RESOLUTION;

    /* Adaptive resolution is computed after the AABB is known (see below).
     * Store the caller's requested resolution so we can take the max. */
    const uint32_t caller_res = res;

    uint32_t max_hulls = params->max_hulls;
    if (max_hulls < 1) max_hulls = 1;
    if (max_hulls > PHYS_DECOMPOSE_MAX_HULLS) max_hulls = PHYS_DECOMPOSE_MAX_HULLS;

    uint32_t min_voxels = params->min_voxels;
    if (min_voxels < 1) min_voxels = 1;

    memset(result, 0, sizeof(*result));

    /* ── Step 1: Compute mesh AABB ────────────────────────────── */
    phys_vec3_t lo = triangles[0].v[0], hi = triangles[0].v[0];
    for (uint32_t t = 0; t < tri_count; t++) {
        for (int v = 0; v < 3; v++) {
            if (triangles[t].v[v].x < lo.x) lo.x = triangles[t].v[v].x;
            if (triangles[t].v[v].y < lo.y) lo.y = triangles[t].v[v].y;
            if (triangles[t].v[v].z < lo.z) lo.z = triangles[t].v[v].z;
            if (triangles[t].v[v].x > hi.x) hi.x = triangles[t].v[v].x;
            if (triangles[t].v[v].y > hi.y) hi.y = triangles[t].v[v].y;
            if (triangles[t].v[v].z > hi.z) hi.z = triangles[t].v[v].z;
        }
    }

    /* Add a small margin around the AABB. */
    float dx = hi.x - lo.x;
    float dy = hi.y - lo.y;
    float dz = hi.z - lo.z;
    float max_extent = dx;
    if (dy > max_extent) max_extent = dy;
    if (dz > max_extent) max_extent = dz;
    if (max_extent < 1e-6f) max_extent = 1.0f;

    /* ── Adaptive voxel resolution ───────────────────────────────
     * Target a cell size of ~0.02 units (2 cm) so small or detailed
     * meshes get enough voxels to avoid gaps.  The caller can still
     * override by passing a higher value in params->resolution. */
    {
        const float target_cell_size = 0.02f;
        uint32_t adaptive_res = (uint32_t)ceilf(max_extent / target_cell_size);
        if (adaptive_res < 16) adaptive_res = 16;
        if (adaptive_res > PHYS_DECOMPOSE_MAX_RESOLUTION)
            adaptive_res = PHYS_DECOMPOSE_MAX_RESOLUTION;
        res = (caller_res > adaptive_res) ? caller_res : adaptive_res;
    }

    float margin = max_extent * 0.05f;
    lo.x -= margin; lo.y -= margin; lo.z -= margin;
    hi.x += margin; hi.y += margin; hi.z += margin;

    /* Recompute with margin. */
    dx = hi.x - lo.x;
    dy = hi.y - lo.y;
    dz = hi.z - lo.z;
    max_extent = dx;
    if (dy > max_extent) max_extent = dy;
    if (dz > max_extent) max_extent = dz;

    float cell_size = max_extent / (float)res;

    /* ── Step 2: Voxelize ─────────────────────────────────────── */
    uint32_t grid_total = res * res * res;
    uint8_t *grid = calloc(grid_total, sizeof(uint8_t));
    if (!grid) return -1;

    voxelize_mesh(grid, res, lo, cell_size, triangles, tri_count);

    /* ── Step 3: Collect filled voxel centers ──────────────────── */
    /* Count filled voxels. */
    uint32_t filled_count = 0;
    for (uint32_t i = 0; i < grid_total; i++) {
        if (grid[i]) filled_count++;
    }

    if (filled_count == 0) {
        /* No interior voxels — mesh is degenerate.
         * Fall back: build a single hull from triangle vertices. */
        free(grid);
        uint32_t nv = tri_count * 3;
        if (nv > PHYS_CONVEX_MAX_VERTS) nv = PHYS_CONVEX_MAX_VERTS;
        phys_vec3_t pts[PHYS_CONVEX_MAX_VERTS];
        uint32_t count = 0;
        for (uint32_t t = 0; t < tri_count && count < nv; t++) {
            for (int v = 0; v < 3 && count < nv; v++) {
                pts[count++] = triangles[t].v[v];
            }
        }
        if (count >= 4) {
            phys_convex_hull_build(&result->hulls[0], pts, count);
            result->hull_count = 1;
        }
        return 0;
    }

    /* Collect centers. */
    phys_vec3_t *centers = malloc(filled_count * sizeof(phys_vec3_t));
    if (!centers) { free(grid); return -1; }

    uint32_t ci = 0;
    for (uint32_t iz = 0; iz < res; iz++) {
        for (uint32_t iy = 0; iy < res; iy++) {
            for (uint32_t ix = 0; ix < res; ix++) {
                if (grid[iz * res * res + iy * res + ix]) {
                    centers[ci++] = (phys_vec3_t){
                        lo.x + ((float)ix + 0.5f) * cell_size,
                        lo.y + ((float)iy + 0.5f) * cell_size,
                        lo.z + ((float)iz + 0.5f) * cell_size,
                    };
                }
            }
        }
    }
    free(grid);

    /* ── Step 4: ACD — iterative splitting (BFS) ─────────────── */
    /* BFS ensures even spatial coverage when max_hulls caps the
     * number of splits.  DFS would fully refine one side of the
     * mesh while leaving the other as a single coarse cluster. */
    uint32_t queue_cap = max_hulls * 4;
    if (queue_cap < 256) queue_cap = 256;
    cluster_range_t *queue = malloc(queue_cap * sizeof(cluster_range_t));
    if (!queue) { free(centers); return -1; }

    /* Output clusters (final hulls). */
    cluster_range_t *final_clusters = malloc(max_hulls * sizeof(cluster_range_t));
    if (!final_clusters) { free(queue); free(centers); return -1; }

    /* Initialize: one cluster = all voxels. */
    uint32_t q_head = 0, q_tail = 0;
    uint32_t final_count = 0;

    queue[q_tail++] = (cluster_range_t){ .start = 0, .count = filled_count };

    while (q_head < q_tail && final_count < max_hulls) {
        cluster_range_t cluster = queue[q_head++];

        if (cluster.count < min_voxels) {
            /* Too small to split; keep as-is if it has enough points. */
            if (cluster.count >= 4) {
                final_clusters[final_count++] = cluster;
            }
            continue;
        }

        /* Measure concavity. */
        float concavity = acd_measure_concavity(
            centers + cluster.start, cluster.count, cell_size);

        if (concavity <= params->concavity_threshold ||
            final_count + 2 > max_hulls) {
            /* Approximately convex or no room for more hulls. */
            final_clusters[final_count++] = cluster;
            continue;
        }

        /* Split the cluster. */
        uint32_t split_n = 0;
        bool did_split = acd_split_cluster(
            centers + cluster.start, cluster.count, &split_n);

        if (!did_split || split_n == 0 || split_n == cluster.count) {
            /* Can't split further. */
            final_clusters[final_count++] = cluster;
            continue;
        }

        /* Enqueue two sub-clusters. */
        if (q_tail + 2 <= queue_cap) {
            queue[q_tail++] = (cluster_range_t){
                .start = cluster.start,
                .count = split_n,
            };
            queue[q_tail++] = (cluster_range_t){
                .start = cluster.start + split_n,
                .count = cluster.count - split_n,
            };
        } else {
            /* Queue full — keep unsplit. */
            final_clusters[final_count++] = cluster;
        }
    }

    /* Any remaining items in queue → add to final. */
    while (q_head < q_tail && final_count < max_hulls) {
        cluster_range_t cluster = queue[q_head++];
        if (cluster.count >= 4) {
            final_clusters[final_count++] = cluster;
        }
    }

    free(queue);

    /* ── Step 5: Build convex hull for each cluster ───────────── */
    /* Hull build now accepts up to 8192 input points and extracts
       the convex shell (output capped at PHYS_CONVEX_MAX_VERTS).
       Pass all cluster voxel centers directly. */
    for (uint32_t i = 0; i < final_count; i++) {
        cluster_range_t *cl = &final_clusters[i];
        uint32_t nv = cl->count;
        if (nv > 8192) nv = 8192;  /* BUILD_MAX_INPUT in hull_build */

        int rc = phys_convex_hull_build(&result->hulls[result->hull_count],
                                        centers + cl->start, nv);
        if (rc == 0) {
            result->hull_count++;
        }
    }

    /* If no hulls were produced, build a single hull from all voxels. */
    if (result->hull_count == 0 && filled_count >= 4) {
        uint32_t nv = filled_count;
        if (nv > 8192) nv = 8192;
        phys_convex_hull_build(&result->hulls[0], centers, nv);
        result->hull_count = 1;
    }

    free(final_clusters);
    free(centers);
    return 0;
}
