/**
 * @file npc_nav_graph_reduce.c
 * @brief Hierarchical graph reduction: partition chunk graph into levels.
 *
 * Non-static functions (4 of 4 max):
 *   1. npc_nav_hgraph_init
 *   2. npc_nav_hgraph_destroy
 *   3. npc_nav_hgraph_reduce
 *   4. npc_nav_hgraph_reduce_level (internal recursion)
 */

#include "ferrum/npc/npc_nav_graph.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ── Hierarchical graph lifecycle ────────────────────────────────── */

bool npc_nav_hgraph_init(npc_nav_hgraph_t *hg, uint32_t max_levels) {
    if (!hg || max_levels == 0 || max_levels > 8) return false;
    memset(hg, 0, sizeof(*hg));
    hg->max_levels = max_levels;
    return true;
}

void npc_nav_hgraph_destroy(npc_nav_hgraph_t *hg) {
    if (!hg) return;
    for (uint32_t i = 0; i < hg->level_count; i++) {
        npc_nav_hnode_t *nodes = hg->nodes_per_level[i];
        uint32_t count = hg->node_count_per_level[i];
        for (uint32_t j = 0; j < count; j++) {
            free(nodes[j].edges);
        }
        free(nodes);
    }
    memset(hg, 0, sizeof(*hg));
}

/* ── Reduction helpers ───────────────────────────────────────────── */

/**
 * @brief Simple 3D grid-based clustering: partition node centroids
 *        into a 3D grid buckets (X, Y, Z).
 */
static uint32_t simple_cluster(npc_nav_hnode_t *parent,
                                const npc_nav_graph_node_t *children,
                                uint32_t child_count,
                                uint32_t target_clusters,
                                uint32_t **out_child_to_cluster) {
    if (child_count == 0 || parent == NULL) return 0;

    *out_child_to_cluster = NULL;

    /* Find the overall AABB. */
    phys_vec3_t min = children[0].centroid;
    phys_vec3_t max = children[0].centroid;
    for (uint32_t i = 1; i < child_count; i++) {
        phys_vec3_t c = children[i].centroid;
        if (c.x < min.x) min.x = c.x;
        if (c.y < min.y) min.y = c.y;
        if (c.z < min.z) min.z = c.z;
        if (c.x > max.x) max.x = c.x;
        if (c.y > max.y) max.y = c.y;
        if (c.z > max.z) max.z = c.z;
    }

    float dx = max.x - min.x;
    float dy = max.y - min.y;
    float dz = max.z - min.z;
    if (dx < 0.001f) dx = 0.001f;
    if (dy < 0.001f) dy = 0.001f;
    if (dz < 0.001f) dz = 0.001f;

    /* 3D grid: reserve at least cbrt(target) bins for Z, then distribute
     * remaining X,Y bins proportionally. */
    uint32_t gz = (uint32_t)ceilf(cbrtf((float)target_clusters));
    if (gz < 1) gz = 1;
    uint32_t xy_target = target_clusters / gz;
    if (xy_target < 1) xy_target = 1;
    uint32_t gx = (uint32_t)ceilf(sqrtf((float)xy_target * dx / (dx + dy)));
    uint32_t gy = (uint32_t)ceilf(sqrtf((float)xy_target * dy / (dx + dy)));
    if (gx < 1) gx = 1;
    if (gy < 1) gy = 1;
    if (gx * gy * gz > child_count) {
        gx = child_count;
        gy = 1;
        gz = 1;
    }

    uint32_t grid_total = gx * gy * gz;
    uint32_t *bucket_count = (uint32_t *)calloc(grid_total, sizeof(uint32_t));
    uint32_t *bucket_cluster = (uint32_t *)calloc(grid_total, sizeof(uint32_t));
    uint32_t *assign = (uint32_t *)calloc(child_count, sizeof(uint32_t));
    if (!bucket_count || !bucket_cluster || !assign) {
        free(bucket_count); free(bucket_cluster); free(assign);
        return 0;
    }
    for (uint32_t b = 0; b < grid_total; b++) bucket_cluster[b] = UINT32_MAX;

    for (uint32_t i = 0; i < child_count; i++) {
        float u = (dx > 0.0f) ? (children[i].centroid.x - min.x) / dx : 0.0f;
        float v = (dy > 0.0f) ? (children[i].centroid.y - min.y) / dy : 0.0f;
        float w = (dz > 0.0f) ? (children[i].centroid.z - min.z) / dz : 0.0f;
        uint32_t bx = (uint32_t)(u * (float)gx);
        uint32_t by = (uint32_t)(v * (float)gy);
        uint32_t bz = (uint32_t)(w * (float)gz);
        if (bx >= gx) bx = gx - 1;
        if (by >= gy) by = gy - 1;
        if (bz >= gz) bz = gz - 1;
        uint32_t bid = bz * gx * gy + by * gx + bx;
        assign[i] = bid;
        bucket_count[bid]++;
    }

    uint32_t parent_count = 0;
    for (uint32_t b = 0; b < grid_total; b++) {
        if (bucket_count[b] == 0) continue;
        if (parent_count >= child_count) break;

        bucket_cluster[b] = parent_count;

        phys_vec3_t sum = {0, 0, 0};
        phys_aabb_t bounds = {{1e9f, 1e9f, 1e9f}, {-1e9f, -1e9f, -1e9f}};
        uint32_t first = UINT32_MAX;

        for (uint32_t i = 0; i < child_count; i++) {
            if (assign[i] != b) continue;
            if (first == UINT32_MAX) first = i;
            phys_vec3_t c = children[i].centroid;
            sum.x += c.x; sum.y += c.y; sum.z += c.z;
            if (children[i].bounds.min.x < bounds.min.x) bounds.min.x = children[i].bounds.min.x;
            if (children[i].bounds.min.y < bounds.min.y) bounds.min.y = children[i].bounds.min.y;
            if (children[i].bounds.min.z < bounds.min.z) bounds.min.z = children[i].bounds.min.z;
            if (children[i].bounds.max.x > bounds.max.x) bounds.max.x = children[i].bounds.max.x;
            if (children[i].bounds.max.y > bounds.max.y) bounds.max.y = children[i].bounds.max.y;
            if (children[i].bounds.max.z > bounds.max.z) bounds.max.z = children[i].bounds.max.z;
        }

        npc_nav_hnode_t *p = &parent[parent_count];
        p->level = 0; /* filled by caller */
        p->child_start = first;
        p->child_count = bucket_count[b];
        p->centroid.x = sum.x / (float)bucket_count[b];
        p->centroid.y = sum.y / (float)bucket_count[b];
        p->centroid.z = sum.z / (float)bucket_count[b];
        p->bounds = bounds;
        parent_count++;
    }

    /* Build child_to_cluster mapping. */
    uint32_t *c2c = (uint32_t *)calloc(child_count, sizeof(uint32_t));
    if (c2c) {
        for (uint32_t i = 0; i < child_count; i++) {
            c2c[i] = bucket_cluster[assign[i]];
        }
    }
    *out_child_to_cluster = c2c;

    free(bucket_count);
    free(bucket_cluster);
    free(assign);
    return parent_count;
}

/* ── Public: hierarchical reduction ──────────────────────────────── */

bool npc_nav_hgraph_reduce(npc_nav_hgraph_t *hg,
                           const npc_nav_graph_t *graph,
                           uint32_t max_levels) {
    if (!hg || !graph || max_levels > hg->max_levels) return false;

    uint32_t edge_cap = graph->edge_cap;

    /* Level 0: copy chunk graph nodes 1:1, including edges. */
    uint32_t n0 = graph->node_count;
    {
        npc_nav_hnode_t *l0 = (npc_nav_hnode_t *)calloc(n0, sizeof(npc_nav_hnode_t));
        if (!l0) return false;
        for (uint32_t i = 0; i < n0; i++) {
            l0[i].level = 0;
            l0[i].child_start = i;
            l0[i].child_count = 1;
            l0[i].centroid = graph->nodes[i].centroid;
            l0[i].bounds = graph->nodes[i].bounds;

            uint32_t ec = graph->nodes[i].edge_count;
            l0[i].edge_cap = (ec > edge_cap ? ec : edge_cap);
            if (l0[i].edge_cap > 0) {
                l0[i].edges = (npc_nav_hedge_t *)calloc(l0[i].edge_cap,
                                                         sizeof(npc_nav_hedge_t));
            }
            if (l0[i].edges) {
                for (uint32_t e = 0; e < ec; e++) {
                    l0[i].edges[e].to_node_id = graph->nodes[i].edges[e].to_node_id;
                    l0[i].edges[e].cost = graph->nodes[i].edges[e].cost;
                    l0[i].edges[e].flags = graph->nodes[i].edges[e].flags;
                    l0[i].edge_count++;
                }
            }
        }
        hg->nodes_per_level[0] = l0;
        hg->node_count_per_level[0] = n0;
        hg->level_count = 1;
    }

    /* Build higher levels by partitioning. */
    uint32_t target_per_cluster = 4;

    for (uint32_t lvl = 1; lvl < max_levels; lvl++) {
        uint32_t prev_count = hg->node_count_per_level[lvl - 1];
        if (prev_count < 64) break;

        uint32_t clusters = prev_count / target_per_cluster;
        if (clusters < 1) clusters = 1;
        npc_nav_hnode_t *level_nodes = (npc_nav_hnode_t *)calloc(
            prev_count, sizeof(npc_nav_hnode_t));
        if (!level_nodes) return false;

        npc_nav_graph_node_t *children = (npc_nav_graph_node_t *)calloc(
            prev_count, sizeof(npc_nav_graph_node_t));
        if (!children) {
            free(level_nodes);
            return false;
        }

        npc_nav_hnode_t *prev_nodes = hg->nodes_per_level[lvl - 1];
        for (uint32_t i = 0; i < prev_count; i++) {
            uint32_t child_idx = prev_nodes[i].child_start;
            children[i] = graph->nodes[child_idx];
        }

        uint32_t *child_to_cluster = NULL;
        uint32_t pcount = simple_cluster(level_nodes, children,
                                         prev_count, clusters,
                                         &child_to_cluster);
        free(children);

        for (uint32_t i = 0; i < pcount; i++) {
            level_nodes[i].level = lvl;
        }

        /* Allocate edge arrays for each cluster node. */
        for (uint32_t c = 0; c < pcount; c++) {
            level_nodes[c].edge_cap = edge_cap;
            if (edge_cap > 0) {
                level_nodes[c].edges = (npc_nav_hedge_t *)calloc(
                    edge_cap, sizeof(npc_nav_hedge_t));
            }
        }

        /* Compute edges between clusters at level lvl.
         * Cluster A and cluster B are connected if any child of A
         * has an edge to any child of B at level lvl-1. */
        if (pcount > 1 && child_to_cluster) {
            uint32_t matrix_bytes = ((pcount * pcount) + 7) / 8;
            uint8_t *edge_mat = (uint8_t *)calloc(matrix_bytes, 1);
            if (edge_mat) {
                for (uint32_t i = 0; i < prev_count; i++) {
                    uint32_t ci = child_to_cluster[i];
                    npc_nav_hnode_t *chi = &prev_nodes[i];
                    for (uint32_t je = 0; je < chi->edge_count; je++) {
                        uint32_t to = chi->edges[je].to_node_id;
                        if (to >= prev_count) continue;
                        uint32_t cj = child_to_cluster[to];
                        if (cj == ci) continue;

                        uint32_t a_idx = ci, b_idx = cj;
                        if (a_idx > b_idx) {
                            uint32_t tmp = a_idx; a_idx = b_idx; b_idx = tmp;
                        }
                        uint32_t bit = a_idx * pcount + b_idx;
                        uint32_t byt = bit / 8;
                        uint32_t msk = 1u << (bit % 8);
                        if (edge_mat[byt] & msk) continue;
                        edge_mat[byt] |= msk;
                        /* Reverse direction too. */
                        uint32_t rbit = b_idx * pcount + a_idx;
                        uint32_t rbyt = rbit / 8;
                        uint32_t rmsk = 1u << (rbit % 8);
                        edge_mat[rbyt] |= rmsk;

                        float dx2 = level_nodes[a_idx].centroid.x - level_nodes[b_idx].centroid.x;
                        float dy2 = level_nodes[a_idx].centroid.y - level_nodes[b_idx].centroid.y;
                        float dz2 = level_nodes[a_idx].centroid.z - level_nodes[b_idx].centroid.z;
                        float dist = sqrtf(dx2 * dx2 + dy2 * dy2 + dz2 * dz2);

                        if (level_nodes[a_idx].edge_count < level_nodes[a_idx].edge_cap) {
                            level_nodes[a_idx].edges[level_nodes[a_idx].edge_count].to_node_id = b_idx;
                            level_nodes[a_idx].edges[level_nodes[a_idx].edge_count].cost = dist;
                            level_nodes[a_idx].edges[level_nodes[a_idx].edge_count].flags = 0;
                            level_nodes[a_idx].edge_count++;
                        }
                        if (level_nodes[b_idx].edge_count < level_nodes[b_idx].edge_cap) {
                            level_nodes[b_idx].edges[level_nodes[b_idx].edge_count].to_node_id = a_idx;
                            level_nodes[b_idx].edges[level_nodes[b_idx].edge_count].cost = dist;
                            level_nodes[b_idx].edges[level_nodes[b_idx].edge_count].flags = 0;
                            level_nodes[b_idx].edge_count++;
                        }
                    }
                }
                free(edge_mat);
            }
        }
        free(child_to_cluster);

        hg->nodes_per_level[lvl] = level_nodes;
        hg->node_count_per_level[lvl] = pcount;
        hg->level_count = lvl + 1;

        if (pcount < 64) break;
    }

    return true;
}
