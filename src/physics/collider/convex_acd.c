/**
 * @file convex_acd.c
 * @brief Approximate convex decomposition of voxel clusters.
 *
 * Iteratively splits voxel clusters along their principal axis
 * until each cluster is approximately convex (measured by comparing
 * cluster volume to its convex hull volume).
 *
 * Non-static functions (2):
 *   1. acd_split_cluster
 *   2. acd_measure_concavity
 */

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "ferrum/physics/phys_types.h"

/* ── Cluster representation ────────────────────────────────────── */

/**
 * A cluster is a subset of voxels identified by a label in the
 * label grid.  We pass around voxel center arrays and counts.
 */

/* ── Concavity measurement ─────────────────────────────────────── */

/**
 * @brief Measure concavity of a voxel cluster.
 *
 * Concavity = 1 - (cluster_voxel_count / convex_hull_voxel_count)
 * where convex_hull_voxel_count is estimated from the AABB volume.
 *
 * A perfectly convex cluster has concavity ≈ 0.
 * A highly concave cluster has concavity close to 1.
 *
 * @param centers    Array of voxel center positions.
 * @param count      Number of voxels in this cluster.
 * @param cell_size  Voxel cell size (for volume estimation).
 * @return Concavity value in [0, 1].
 */
float acd_measure_concavity(const phys_vec3_t *centers, uint32_t count,
                             float cell_size) {
    if (count <= 4) return 0.0f;

    /* Compute AABB of the cluster. */
    phys_vec3_t lo = centers[0], hi = centers[0];
    for (uint32_t i = 1; i < count; i++) {
        if (centers[i].x < lo.x) lo.x = centers[i].x;
        if (centers[i].y < lo.y) lo.y = centers[i].y;
        if (centers[i].z < lo.z) lo.z = centers[i].z;
        if (centers[i].x > hi.x) hi.x = centers[i].x;
        if (centers[i].y > hi.y) hi.y = centers[i].y;
        if (centers[i].z > hi.z) hi.z = centers[i].z;
    }

    /* Estimate convex hull volume as AABB volume (overestimate). */
    float dx = (hi.x - lo.x) + cell_size;
    float dy = (hi.y - lo.y) + cell_size;
    float dz = (hi.z - lo.z) + cell_size;
    float aabb_vol = dx * dy * dz;
    float cluster_vol = (float)count * cell_size * cell_size * cell_size;

    if (aabb_vol < 1e-10f) return 0.0f;

    float fill_ratio = cluster_vol / aabb_vol;
    /* Concavity: how much of the AABB is NOT filled. */
    float concavity = 1.0f - fill_ratio;
    if (concavity < 0.0f) concavity = 0.0f;
    if (concavity > 1.0f) concavity = 1.0f;
    return concavity;
}

/* ── Cluster splitting ─────────────────────────────────────────── */

/**
 * @brief Split a voxel cluster into two halves along its longest axis.
 *
 * Partitions the voxel centers into two groups (a and b) by splitting
 * at the median along the axis of greatest extent.
 *
 * @param centers    Input voxel centers (reordered in place).
 * @param count      Number of voxels.
 * @param split_count  Output: number of voxels in the first half.
 * @return true if split was performed, false if cluster too small.
 *
 * Side effects: reorders centers[] so that the first split_count
 * entries are group A, and the rest are group B.
 */
bool acd_split_cluster(phys_vec3_t *centers, uint32_t count,
                        uint32_t *split_count) {
    if (count < 2) {
        *split_count = count;
        return false;
    }

    /* Find AABB and longest axis. */
    phys_vec3_t lo = centers[0], hi = centers[0];
    for (uint32_t i = 1; i < count; i++) {
        if (centers[i].x < lo.x) lo.x = centers[i].x;
        if (centers[i].y < lo.y) lo.y = centers[i].y;
        if (centers[i].z < lo.z) lo.z = centers[i].z;
        if (centers[i].x > hi.x) hi.x = centers[i].x;
        if (centers[i].y > hi.y) hi.y = centers[i].y;
        if (centers[i].z > hi.z) hi.z = centers[i].z;
    }

    float dx = hi.x - lo.x;
    float dy = hi.y - lo.y;
    float dz = hi.z - lo.z;

    /* Choose axis with greatest extent. */
    int axis = 0;
    if (dy > dx && dy > dz) axis = 1;
    else if (dz > dx && dz > dy) axis = 2;

    /* Find split plane at midpoint of the longest axis. */
    float mid;
    if (axis == 0) mid = (lo.x + hi.x) * 0.5f;
    else if (axis == 1) mid = (lo.y + hi.y) * 0.5f;
    else mid = (lo.z + hi.z) * 0.5f;

    /* Partition: move voxels with coord <= mid to front.
     * Simple two-pointer partition (like quicksort pivot). */
    uint32_t left = 0;
    for (uint32_t i = 0; i < count; i++) {
        float val;
        if (axis == 0) val = centers[i].x;
        else if (axis == 1) val = centers[i].y;
        else val = centers[i].z;

        if (val <= mid) {
            /* Swap to front. */
            phys_vec3_t tmp = centers[left];
            centers[left] = centers[i];
            centers[i] = tmp;
            left++;
        }
    }

    /* Handle degenerate case: all on one side. */
    if (left == 0) left = 1;
    if (left == count) left = count - 1;

    *split_count = left;
    return true;
}
