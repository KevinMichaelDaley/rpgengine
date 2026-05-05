/**
 * @file npc_pathfind_svo_astar.c
 * @brief SVO voxel-level A* pathfinding with min-heap.
 *
 * Non-static functions (1 of 4 max):
 *   1. npc_svo_astar
 */

#include "ferrum/npc/npc_pathfind.h"
#include "ferrum/npc/npc_svo.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ── Min-heap for A* open set ────────────────────────────────────── */

#define HEAP_MAX 32768

typedef struct {
    float    f;
    uint32_t vx, vy, vz;
} heap_entry_t;

typedef struct {
    heap_entry_t entries[HEAP_MAX];
    uint32_t     size;
} min_heap_t;

static void heap_init(min_heap_t *h) { h->size = 0; }

static void heap_push(min_heap_t *h, float f,
                      uint32_t vx, uint32_t vy, uint32_t vz) {
    if (h->size >= HEAP_MAX) return;
    uint32_t i = h->size++;
    while (i > 0) {
        uint32_t p = (i - 1) / 2;
        if (h->entries[p].f <= f) break;
        h->entries[i] = h->entries[p];
        i = p;
    }
    h->entries[i].f = f;
    h->entries[i].vx = vx; h->entries[i].vy = vy; h->entries[i].vz = vz;
}

static heap_entry_t heap_pop(min_heap_t *h) {
    heap_entry_t top = h->entries[0];
    if (h->size <= 1) { h->size = 0; return top; }
    heap_entry_t last = h->entries[--h->size];
    uint32_t i = 0;
    for (;;) {
        uint32_t left = 2 * i + 1, right = 2 * i + 2, smallest = i;
        if (left < h->size && h->entries[left].f < h->entries[smallest].f)
            smallest = left;
        if (right < h->size && h->entries[right].f < h->entries[smallest].f)
            smallest = right;
        if (smallest == i) break;
        h->entries[i] = h->entries[smallest];
        i = smallest;
    }
    h->entries[i] = last;
    return top;
}

/* ── Voxel walkability ───────────────────────────────────────────── */

static bool voxel_walkable(const npc_svo_grid_t *svo,
                            uint32_t vx, uint32_t vy, uint32_t vz,
                            float agent_radius, float agent_height) {
    uint32_t cells = 1u << svo->max_depth;
    if (vx >= cells || vy >= cells || vz >= cells) return false;
    if (vz == 0) return false;

    float vs = svo->voxel_size;
    float mx = svo->world_bounds.min.x;
    float my = svo->world_bounds.min.y;
    float mz = svo->world_bounds.min.z;

    /* Current cell must not be solid. */
    phys_vec3_t pc = {mx + ((float)vx + 0.5f) * vs,
                      my + ((float)vy + 0.5f) * vs,
                      mz + ((float)vz + 0.5f) * vs};
    uint8_t fc = npc_svo_query_point(svo, pc, NULL);
    if (fc & NPC_SVO_FLAG_SOLID) return false;

    /* Cell below must be solid (floor). */
    phys_vec3_t pb = {mx + ((float)vx + 0.5f) * vs,
                      my + ((float)vy + 0.5f) * vs,
                      mz + ((float)(vz - 1) + 0.5f) * vs};
    uint8_t fb = npc_svo_query_point(svo, pb, NULL);
    if ((fb & NPC_SVO_FLAG_SOLID) == 0) return false;

    /* Vertical headroom: agent_height/voxel_size voxels above must be empty. */
    uint32_t headroom = (uint32_t)(agent_height / vs);
    for (uint32_t i = 1; i <= headroom; i++) {
        if (vz + i >= cells) return false;
        phys_vec3_t pa = {mx + ((float)vx + 0.5f) * vs,
                          my + ((float)vy + 0.5f) * vs,
                          mz + ((float)(vz + i) + 0.5f) * vs};
        uint8_t fa = npc_svo_query_point(svo, pa, NULL);
        if (fa & NPC_SVO_FLAG_SOLID) return false;
    }

    /* Horizontal clearance: agent_radius/voxel_size neighbors in X and Y
     * must not be SOLID. */
    uint32_t horiz = (uint32_t)(agent_radius / vs);
    if (horiz > 0) {
    for (uint32_t i = 1; i <= horiz; i++) {
        if (vx >= i) {
            phys_vec3_t pxn = {mx + ((float)(vx - i) + 0.5f) * vs,
                               my + ((float)vy + 0.5f) * vs,
                               mz + ((float)vz + 0.5f) * vs};
            uint8_t fxn = npc_svo_query_point(svo, pxn, NULL);
            if (fxn & NPC_SVO_FLAG_SOLID) return false;
        }
        if (vx + i < cells) {
            phys_vec3_t pxp = {mx + ((float)(vx + i) + 0.5f) * vs,
                               my + ((float)vy + 0.5f) * vs,
                               mz + ((float)vz + 0.5f) * vs};
            uint8_t fxp = npc_svo_query_point(svo, pxp, NULL);
            if (fxp & NPC_SVO_FLAG_SOLID) return false;
        }
        if (vy >= i) {
            phys_vec3_t pyn = {mx + ((float)vx + 0.5f) * vs,
                               my + ((float)(vy - i) + 0.5f) * vs,
                               mz + ((float)vz + 0.5f) * vs};
            uint8_t fyn = npc_svo_query_point(svo, pyn, NULL);
            if (fyn & NPC_SVO_FLAG_SOLID) return false;
        }
        if (vy + i < cells) {
            phys_vec3_t pyp = {mx + ((float)vx + 0.5f) * vs,
                               my + ((float)(vy + i) + 0.5f) * vs,
                               mz + ((float)vz + 0.5f) * vs};
            uint8_t fyp = npc_svo_query_point(svo, pyp, NULL);
            if (fyp & NPC_SVO_FLAG_SOLID) return false;
        }
    }
    }

    return true;
}

/* ── Public: SVO A* ──────────────────────────────────────────────── */

bool npc_svo_astar(const npc_svo_grid_t *svo,
                   const npc_svo_blocker_t *blockers,
                   uint32_t blocker_count,
                   phys_vec3_t start,
                   phys_vec3_t goal,
                   phys_vec3_t *out_waypoints,
                   uint32_t *out_count,
                   uint32_t max_waypoints,
                   float agent_radius,
                   float agent_height) {
    if (!svo || !out_waypoints || !out_count || max_waypoints == 0) return false;

    uint32_t cells = 1u << svo->max_depth;
    uint32_t total = cells * cells * cells;

    /* Convert start/goal to voxel coordinates. */
    uint32_t sx, sy, sz, gx, gy, gz;
    if (!npc_svo_world_to_voxel(svo, start, &sx, &sy, &sz)) return false;
    if (!npc_svo_world_to_voxel(svo, goal, &gx, &gy, &gz)) return false;

    if (!voxel_walkable(svo, sx, sy, sz, agent_radius, agent_height)) return false;
    if (!voxel_walkable(svo, gx, gy, gz, agent_radius, agent_height)) return false;

    /* Allocate g-score and parent arrays. */
    float *g = (float *)malloc(total * sizeof(float));
    uint32_t *parent = (uint32_t *)malloc(total * sizeof(uint32_t));
    if (!g || !parent) { free(g); free(parent); return false; }
    for (uint32_t i = 0; i < total; i++) g[i] = INFINITY;
    memset(parent, 0xFF, total * sizeof(uint32_t));

    min_heap_t heap;
    heap_init(&heap);

    uint32_t start_idx = sz * cells * cells + sy * cells + sx;
    uint32_t goal_idx  = gz * cells * cells + gy * cells + gx;
    float voxel_sz = svo->voxel_size;

    g[start_idx] = 0.0f;
    heap_push(&heap, 0.0f, sx, sy, sz);
    bool reached = false;

    while (heap.size > 0) {
        heap_entry_t cur = heap_pop(&heap);
        uint32_t ci = cur.vz * cells * cells + cur.vy * cells + cur.vx;
        if (ci == goal_idx) { reached = true; break; }

        int32_t dx[] = {-1, 1, 0, 0, 0, 0};
        int32_t dy[] = {0, 0, -1, 1, 0, 0};
        int32_t dz[] = {0, 0, 0, 0, -1, 1};

        for (int n = 0; n < 6; n++) {
            int32_t nx = (int32_t)cur.vx + dx[n];
            int32_t ny = (int32_t)cur.vy + dy[n];
            int32_t nz = (int32_t)cur.vz + dz[n];
            if (nx < 0 || (uint32_t)nx >= cells ||
                ny < 0 || (uint32_t)ny >= cells ||
                nz < 0 || (uint32_t)nz >= cells)
                continue;

            if (!voxel_walkable(svo, (uint32_t)nx, (uint32_t)ny, (uint32_t)nz,
                                agent_radius, agent_height))
                continue;

            /* Check dynamic blockers. */
            if (blockers && blocker_count > 0) {
                phys_aabb_t va = npc_svo_voxel_aabb(svo, svo->max_depth,
                                                     (uint32_t)nx, (uint32_t)ny, (uint32_t)nz);
                if (npc_svo_aabb_blocked(svo, blockers, blocker_count, va, 0xFFFFFFFFu))
                    continue;
            }

            uint32_t ni = (uint32_t)nz * cells * cells + (uint32_t)ny * cells + (uint32_t)nx;
            float new_g = g[ci] + voxel_sz;

            if (new_g < g[ni]) {
                g[ni] = new_g;
                parent[ni] = ci;
                float dx_f = (float)((int32_t)nx - (int32_t)gx);
                float dy_f = (float)((int32_t)ny - (int32_t)gy);
                float dz_f = (float)((int32_t)nz - (int32_t)gz);
                float h = sqrtf(dx_f * dx_f + dy_f * dy_f + dz_f * dz_f) * voxel_sz;
                heap_push(&heap, new_g + h, (uint32_t)nx, (uint32_t)ny, (uint32_t)nz);
            }
        }
    }

    if (!reached) { free(g); free(parent); return false; }

    /* Reconstruct path. */
    uint32_t path_idx[256];
    uint32_t path_len = 0;
    uint32_t cur_idx = goal_idx;
    while (cur_idx != UINT32_MAX && path_len < 256) {
        path_idx[path_len++] = cur_idx;
        if (cur_idx == start_idx) break;
        cur_idx = parent[cur_idx];
    }

    *out_count = 0;
    for (int32_t i = (int32_t)path_len - 1; i >= 0 && *out_count < max_waypoints; i--) {
        uint32_t idx = path_idx[(uint32_t)i];
        uint32_t vz = idx / (cells * cells);
        uint32_t vy = (idx % (cells * cells)) / cells;
        uint32_t vx = (idx % (cells * cells)) % cells;
        out_waypoints[*out_count] = (phys_vec3_t){
            svo->world_bounds.min.x + ((float)vx + 0.5f) * voxel_sz,
            svo->world_bounds.min.y + ((float)vy + 0.5f) * voxel_sz,
            svo->world_bounds.min.z + ((float)vz + 0.5f) * voxel_sz
        };
        (*out_count)++;
    }

    free(g);
    free(parent);
    return true;
}
