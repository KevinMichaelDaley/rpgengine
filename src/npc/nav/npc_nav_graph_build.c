/**
 * @file npc_nav_graph_build.c
 * @brief Chunk graph lifecycle and extraction from SVO.
 *
 * Non-static functions (4 of 4 max):
 *   1. npc_nav_graph_init
 *   2. npc_nav_graph_destroy
 *   3. npc_nav_graph_add_node
 *   4. npc_nav_graph_add_edge
 *   5. npc_nav_graph_extract
 *
 * (5th function is the public extraction entry-point.)
 */
#include "ferrum/npc/npc_nav_graph.h"
#include "ferrum/npc/npc_svo.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ── Chunk graph lifecycle ───────────────────────────────────────── */

bool npc_nav_graph_init(npc_nav_graph_t *graph, uint32_t node_cap,
                        uint32_t edge_cap) {
    if (!graph || node_cap == 0) return false;
    memset(graph, 0, sizeof(*graph));
    graph->nodes = (npc_nav_graph_node_t *)calloc(node_cap,
                                                   sizeof(npc_nav_graph_node_t));
    if (!graph->nodes) return false;
    graph->node_cap = node_cap;
    graph->edge_cap = edge_cap;
    return true;
}

void npc_nav_graph_destroy(npc_nav_graph_t *graph) {
    if (!graph) return;
    for (uint32_t i = 0; i < graph->node_count; i++) {
        free(graph->nodes[i].edges);
    }
    free(graph->nodes);
    memset(graph, 0, sizeof(*graph));
}

uint32_t npc_nav_graph_add_node(npc_nav_graph_t *graph,
                                uint32_t section_id,
                                phys_vec3_t centroid,
                                phys_aabb_t bounds,
                                float radius) {
    if (!graph || graph->node_count >= graph->node_cap) return UINT32_MAX;
    uint32_t id = graph->node_count++;
    npc_nav_graph_node_t *n = &graph->nodes[id];
    n->node_id = id;
    n->section_id = section_id;
    n->centroid = centroid;
    n->bounds = bounds;
    n->radius = radius;
    n->edge_count = 0;
    n->edge_cap = graph->edge_cap;
    n->edges = (npc_nav_graph_edge_t *)calloc(graph->edge_cap,
                                               sizeof(npc_nav_graph_edge_t));
    return id;
}

bool npc_nav_graph_add_edge(npc_nav_graph_t *graph,
                            uint32_t from, uint32_t to,
                            float cost, uint32_t flags) {
    if (!graph || from >= graph->node_count || to >= graph->node_count)
        return false;
    npc_nav_graph_node_t *n = &graph->nodes[from];
    if (n->edge_count >= n->edge_cap) return false;
    npc_nav_graph_edge_t *e = &n->edges[n->edge_count++];
    e->to_node_id = to;
    e->cost = cost;
    e->flags = flags;
    return true;
}

/* ── SVO extraction helpers ──────────────────────────────────────── */

#define MAX_NODES_PER_EXTRACT 4096

/** BFS queue entry: a voxel coordinate pair. */
typedef struct {
    uint32_t vx, vy, vz;
} bfs_entry_t;

/**
 * @brief Query the SVO at a specific voxel coordinate.
 */
static uint8_t voxel_flags(const npc_svo_grid_t *svo,
                           uint32_t vx, uint32_t vy, uint32_t vz) {
    uint32_t node_idx = 0;
    uint32_t cells = 1u << svo->max_depth;
    if (vx >= cells || vy >= cells || vz >= cells) return 0;

    for (uint32_t d = 0; d < svo->max_depth; d++) {
        cells >>= 1;
        uint32_t cx = (vx / cells) & 1;
        uint32_t cy = (vy / cells) & 1;
        uint32_t cz = (vz / cells) & 1;
        uint32_t ci = (cz << 2) | (cy << 1) | cx;
        uint32_t child = svo->nodes[node_idx].children[ci];
        if (child == NPC_SVO_INVALID_NODE) return 0;
        node_idx = child;
    }
    return svo->nodes[node_idx].flags;
}

/**
 * @brief Check if a voxel at (vx,vy,vz) is walkable: not solid, has
 *        solid floor below, and has empty space above.
 */
static bool is_walkable(const npc_svo_grid_t *svo,
                        uint32_t vx, uint32_t vy, uint32_t vz) {
    uint32_t cells = 1u << svo->max_depth;
    if (vx >= cells || vy >= cells || vz >= cells) return false;

    /* Current voxel must not be SOLID. */
    if (voxel_flags(svo, vx, vy, vz) & NPC_SVO_FLAG_SOLID) return false;

    /* Floor check: voxel below must be SOLID or we're at floor level. */
    if (vz == 0) return false;
    if (!(voxel_flags(svo, vx, vy, vz - 1) & NPC_SVO_FLAG_SOLID)) return false;

    /* Headroom check: voxel above must not be SOLID. */
    if (vz + 1 < cells) {
        if (voxel_flags(svo, vx, vy, vz + 1) & NPC_SVO_FLAG_SOLID) return false;
    }
    return true;
}

/**
 * @brief BFS flood over WALKABLE voxels to find a connected component.
 *
 * Returns the component's centroid, bounds, and voxel count.
 * Sets visited[] for all voxels in the component.
 */
static uint32_t walkable_component(const npc_svo_grid_t *svo,
                                    uint32_t start_x,
                                    uint32_t start_y,
                                    uint32_t start_z,
                                    uint32_t *comp_id,
                                    uint32_t cid,
                                    phys_vec3_t *out_centroid,
                                    phys_aabb_t *out_bounds) {
    uint32_t cells = 1u << svo->max_depth;
    uint32_t idx = start_z * cells * cells + start_y * cells + start_x;
    if (comp_id[idx]) return 0;
    if (!is_walkable(svo, start_x, start_y, start_z))
        return 0;

    uint32_t qcap = cells * cells * cells;
    if (qcap > 256 * 256) qcap = 256 * 256;
    bfs_entry_t *queue = (bfs_entry_t *)calloc(qcap, sizeof(bfs_entry_t));
    if (!queue) return 0;
    uint32_t qhead = 0, qtail = 0;

    queue[qtail++] = (bfs_entry_t){start_x, start_y, start_z};
    comp_id[idx] = cid;

    phys_vec3_t sum = {0, 0, 0};
    phys_aabb_t bounds;
    bounds.min.x = (float)start_x;
    bounds.min.y = (float)start_y;
    bounds.min.z = (float)start_z;
    bounds.max = bounds.min;
    uint32_t count = 0;

    while (qhead < qtail) {
        bfs_entry_t cur = queue[qhead++];
        count++;

        float fx = (float)cur.vx;
        float fy = (float)cur.vy;
        float fz = (float)cur.vz;
        sum.x += fx;
        sum.y += fy;
        sum.z += fz;
        if (fx < bounds.min.x) bounds.min.x = fx;
        if (fy < bounds.min.y) bounds.min.y = fy;
        if (fz < bounds.min.z) bounds.min.z = fz;
        if (fx > bounds.max.x) bounds.max.x = fx;
        if (fy > bounds.max.y) bounds.max.y = fy;
        if (fz > bounds.max.z) bounds.max.z = fz;

        /* 6 neighbors. */
        int32_t nx_arr[] = {(int32_t)cur.vx - 1, (int32_t)cur.vx + 1,
                            (int32_t)cur.vx, (int32_t)cur.vx,
                            (int32_t)cur.vx, (int32_t)cur.vx};
        int32_t ny_arr[] = {(int32_t)cur.vy, (int32_t)cur.vy,
                            (int32_t)cur.vy - 1, (int32_t)cur.vy + 1,
                            (int32_t)cur.vy, (int32_t)cur.vy};
        int32_t nz_arr[] = {(int32_t)cur.vz, (int32_t)cur.vz,
                            (int32_t)cur.vz, (int32_t)cur.vz,
                            (int32_t)cur.vz - 1, (int32_t)cur.vz + 1};

        for (int n = 0; n < 6; n++) {
            int32_t nx = nx_arr[n], ny = ny_arr[n], nz = nz_arr[n];
            if (nx < 0 || (uint32_t)nx >= cells ||
                ny < 0 || (uint32_t)ny >= cells ||
                nz < 0 || (uint32_t)nz >= cells)
                continue;
            uint32_t nidx = (uint32_t)nz * cells * cells +
                            (uint32_t)ny * cells + (uint32_t)nx;
            if (comp_id[nidx]) continue;
            if (!is_walkable(svo, (uint32_t)nx, (uint32_t)ny, (uint32_t)nz)) continue;
            comp_id[nidx] = cid;
            if (qtail < qcap) {
                queue[qtail++] = (bfs_entry_t){(uint32_t)nx, (uint32_t)ny, (uint32_t)nz};
            }
        }
    }

    free(queue);
    if (count == 0) return 0;

    float inv = 1.0f / (float)count;
    out_centroid->x = sum.x * inv;
    out_centroid->y = sum.y * inv;
    out_centroid->z = sum.z * inv;
    *out_bounds = bounds;
    return count;
}

/* ── Public: graph extraction ────────────────────────────────────── */

uint32_t npc_nav_graph_extract(npc_nav_graph_t *graph,
                               const npc_svo_grid_t *svo,
                               const npc_svo_blocker_t *blockers,
                               uint32_t blocker_count) {
    (void)blockers;
    (void)blocker_count;
    if (!graph || !svo || svo->max_depth == 0) return 0;

    uint32_t cells = 1u << svo->max_depth;
    uint32_t total = cells * cells * cells;
    uint32_t *comp_id = (uint32_t *)calloc(total, sizeof(uint32_t));
    if (!comp_id) return 0;

    uint32_t nodes_created = 0;
    uint32_t cid = 1;

    for (uint32_t vz = 0; vz < cells; vz++) {
        for (uint32_t vy = 0; vy < cells; vy++) {
            for (uint32_t vx = 0; vx < cells; vx++) {
                uint32_t idx = vz * cells * cells + vy * cells + vx;
                if (comp_id[idx]) continue;
                if (comp_id[idx]) continue;
                if (!is_walkable(svo, vx, vy, vz)) continue;

                phys_vec3_t centroid;
                phys_aabb_t bounds;
                uint32_t vc = walkable_component(svo, vx, vy, vz,
                                                  comp_id, cid,
                                                  &centroid, &bounds);
                if (vc == 0) continue;

                /* Compute world-space centroid and radius. */
                float voxel_sz = svo->voxel_size;
                float ws_min_x = svo->world_bounds.min.x;
                float ws_min_y = svo->world_bounds.min.y;
                float ws_min_z = svo->world_bounds.min.z;

                phys_vec3_t ws_centroid = {
                    ws_min_x + centroid.x * voxel_sz,
                    ws_min_y + centroid.y * voxel_sz,
                    ws_min_z + centroid.z * voxel_sz
                };
                phys_aabb_t ws_bounds = {
                    {ws_min_x + bounds.min.x * voxel_sz,
                     ws_min_y + bounds.min.y * voxel_sz,
                     ws_min_z + bounds.min.z * voxel_sz},
                    {ws_min_x + (bounds.max.x + 1.0f) * voxel_sz,
                     ws_min_y + (bounds.max.y + 1.0f) * voxel_sz,
                     ws_min_z + (bounds.max.z + 1.0f) * voxel_sz}
                };
                float dx = ws_bounds.max.x - ws_bounds.min.x;
                float dy = ws_bounds.max.y - ws_bounds.min.y;
                float dz = ws_bounds.max.z - ws_bounds.min.z;
                float radius = (dx > dy ? dx : dy);
                if (dz > radius) radius = dz;
                radius *= 0.5f;

                if (npc_nav_graph_add_node(graph, 0, ws_centroid,
                                            ws_bounds, radius) != UINT32_MAX) {
                    nodes_created++;
                }
                cid++;

                if (nodes_created >= MAX_NODES_PER_EXTRACT)
                    goto done;
            }
        }
    }

    /* Scan for adjacency between walkable components and add edges.
     * Use 26-connectivity (3x3x3 minus center) for edge detection
     * so that diagonally adjacent components are connected, while
     * BFS uses 6-connectivity for component separation. */
    {
        uint32_t n = nodes_created;
        if (n > 1) {
            uint32_t matrix_bytes = ((n * n) + 7) / 8;
            uint8_t *edge_mat = (uint8_t *)calloc(matrix_bytes, 1);
            if (edge_mat) {
                for (uint32_t vz = 0; vz < cells; vz++) {
                    for (uint32_t vy = 0; vy < cells; vy++) {
                        for (uint32_t vx = 0; vx < cells; vx++) {
                            uint32_t idx = vz * cells * cells + vy * cells + vx;
                            uint32_t a = comp_id[idx];
                            if (a == 0) continue;
                            a--;

                            /* Scan 26 neighbors (3x3x3 minus center). */
                            for (int32_t dz = -1; dz <= 1; dz++) {
                                int32_t nz = (int32_t)vz + dz;
                                if (nz < 0 || (uint32_t)nz >= cells) continue;
                                for (int32_t dy = -1; dy <= 1; dy++) {
                                    int32_t ny = (int32_t)vy + dy;
                                    if (ny < 0 || (uint32_t)ny >= cells) continue;
                                    for (int32_t dx = -1; dx <= 1; dx++) {
                                        if (dx == 0 && dy == 0 && dz == 0) continue;
                                        int32_t nx = (int32_t)vx + dx;
                                        if (nx < 0 || (uint32_t)nx >= cells) continue;
                                        uint32_t nidx = (uint32_t)nz * cells * cells +
                                                        (uint32_t)ny * cells + (uint32_t)nx;
                                        uint32_t b = comp_id[nidx];
                                        if (b == 0 || b == a + 1) continue;
                                        b--;

                                        uint32_t bit = a * n + b;
                                        uint32_t byt = bit / 8;
                                        uint32_t msk = 1u << (bit % 8);
                                        if (edge_mat[byt] & msk) continue;
                                        edge_mat[byt] |= msk;
                                        uint32_t rbit = b * n + a;
                                        uint32_t rbyt = rbit / 8;
                                        uint32_t rmsk = 1u << (rbit % 8);
                                        edge_mat[rbyt] |= rmsk;

                                        float dxf = graph->nodes[a].centroid.x - graph->nodes[b].centroid.x;
                                        float dyf = graph->nodes[a].centroid.y - graph->nodes[b].centroid.y;
                                        float dzf = graph->nodes[a].centroid.z - graph->nodes[b].centroid.z;
                                        float cost = sqrtf(dxf * dxf + dyf * dyf + dzf * dzf);
                                        npc_nav_graph_add_edge(graph, a, b, cost, 0);
                                        npc_nav_graph_add_edge(graph, b, a, cost, 0);
                                    }
                                }
                            }
                        }
                    }
                }
                free(edge_mat);
            }
        }
    }

done:
    free(comp_id);
    return nodes_created;
}
