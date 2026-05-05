/**
 * @file npc_pathfind_shortcut.c
 * @brief Line-of-sight waypoint shortcut reduction.
 *
 * Non-static functions (1 of 4 max):
 *   1. npc_pathfind_shortcut
 */

#include "ferrum/npc/npc_pathfind.h"
#include "ferrum/npc/npc_svo.h"

#include <math.h>
#include <string.h>

/**
 * @brief Walk voxels between two world-space points using parametric
 *        stepping at half-voxel increments, checking every intermediate
 *        voxel for SOLID flag and dynamic blocker intersection.
 *
 * @param from           Start of the segment (excluded from check).
 * @param to             End of the segment (excluded from check).
 * @param svo            SVO grid (if NULL, static-geometry check skipped).
 * @param blockers       Active blocker array (may be NULL).
 * @param blocker_count  Number of active blockers.
 * @return true if no solid voxels or blockers lie between from and to.
 */
static bool line_of_sight_clear_(phys_vec3_t from, phys_vec3_t to,
                                 const npc_svo_grid_t *svo,
                                 const npc_svo_blocker_t *blockers,
                                 uint32_t blocker_count) {
    if (!svo) return true;

    float dx = to.x - from.x;
    float dy = to.y - from.y;
    float dz = to.z - from.z;
    float dist = sqrtf(dx * dx + dy * dy + dz * dz);
    if (dist < 0.0001f) return true;

    float step_dist = svo->voxel_size * 0.5f;
    if (step_dist < 0.0001f) step_dist = 0.0001f;

    float t = step_dist / dist;
    while (t < 1.0f) {
        phys_vec3_t p;
        p.x = from.x + dx * t;
        p.y = from.y + dy * t;
        p.z = from.z + dz * t;

        uint8_t flags = npc_svo_query_point(svo, p, NULL);
        if (flags & NPC_SVO_FLAG_SOLID) return false;

        uint32_t vx, vy, vz;
        if (npc_svo_world_to_voxel(svo, p, &vx, &vy, &vz)) {
            if (npc_svo_voxel_blocked(svo, blockers, blocker_count, vx, vy, vz)) {
                return false;
            }
        }

        t += step_dist / dist;
    }

    return true;
}

void npc_pathfind_shortcut(const phys_vec3_t *in, uint32_t in_count,
                           phys_vec3_t *out, uint32_t *out_count,
                           uint32_t max_out,
                           const npc_svo_grid_t *svo,
                           const npc_svo_blocker_t *blockers,
                           uint32_t blocker_count) {
    if (!in || !out || !out_count || in_count == 0 || max_out == 0) return;

    if (in_count == 1) {
        out[0] = in[0];
        *out_count = 1;
        return;
    }

    uint32_t write = 0;
    out[write++] = in[0];
    uint32_t anchor = 0;

    for (uint32_t i = 1; i + 1 < in_count; i++) {
        phys_vec3_t d1, d2;
        d1.x = in[i].x - in[anchor].x;
        d1.y = in[i].y - in[anchor].y;
        d1.z = in[i].z - in[anchor].z;
        d2.x = in[i + 1].x - in[anchor].x;
        d2.y = in[i + 1].y - in[anchor].y;
        d2.z = in[i + 1].z - in[anchor].z;

        float len1 = sqrtf(d1.x * d1.x + d1.y * d1.y + d1.z * d1.z);
        float len2 = sqrtf(d2.x * d2.x + d2.y * d2.y + d2.z * d2.z);
        float eps = 0.001f;
        if (len1 < eps || len2 < eps) {
            if (write < max_out) out[write++] = in[i];
            anchor = i;
            continue;
        }

        float dot = (d1.x * d2.x + d1.y * d2.y + d1.z * d2.z) / (len1 * len2);
        if (dot < 0.998f) {
            if (write < max_out) out[write++] = in[i];
            anchor = i;
            continue;
        }

        if (!line_of_sight_clear_(in[anchor], in[i + 1], svo, blockers, blocker_count)) {
            if (write < max_out) out[write++] = in[i];
            anchor = i;
        }
    }

    if (write < max_out) out[write++] = in[in_count - 1];
    *out_count = write;
}
