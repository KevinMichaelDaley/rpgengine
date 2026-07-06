/**
 * @file srd_rules_corridor_graph.c
 * @brief Graph connectivity rules (Rules 25-30): BridgeComponents,
 *        AddLoop, RemoveLoop, ShortcutPath, RemoveShortcut, RerouteCorridor.
 *
 * Non-static functions (1): srd_rules_corridor_register_graph
 */
#include "ferrum/procgen/srd/srd_rules_corridor.h"

#include <math.h>
#include <string.h>

/* ── BFS helper: check if two boxes are in the same component ──────── */

/**
 * BFS from src, checking if dst is reachable. Optionally skips one box.
 * @param skip_idx  Index to skip (-1 to skip nothing).
 * @return true if dst is reachable from src.
 */
static bool bfs_reachable(const srd_sdf_layout_t *layout,
                          int src, int dst, int skip_idx) {
    if (src == dst) return true;
    if (layout->n_boxes == 0) return false;

    bool visited[SRD_MAX_BOXES];
    memset(visited, 0, sizeof(visited));
    int queue[SRD_MAX_BOXES];
    int head = 0, tail = 0;

    visited[src] = true;
    queue[tail++] = src;

    while (head < tail) {
        int cur = queue[head++];
        for (int j = 0; j < layout->n_boxes; j++) {
            if (j == skip_idx) continue;
            if (!visited[j] && srd_sdf_layout_get_adj(layout, cur, j)) {
                if (j == dst) return true;
                visited[j] = true;
                queue[tail++] = j;
            }
        }
    }
    return false;
}

/**
 * BFS shortest path distance from src to dst (skipping skip_idx).
 * Returns -1 if unreachable, 0 if src==dst.
 */
static int bfs_distance(const srd_sdf_layout_t *layout,
                        int src, int dst, int skip_idx) {
    if (src == dst) return 0;
    if (layout->n_boxes == 0) return -1;

    int dist[SRD_MAX_BOXES];
    memset(dist, -1, sizeof(dist));
    int queue[SRD_MAX_BOXES];
    int head = 0, tail = 0;

    dist[src] = 0;
    queue[tail++] = src;

    while (head < tail) {
        int cur = queue[head++];
        for (int j = 0; j < layout->n_boxes; j++) {
            if (j == skip_idx) continue;
            if (dist[j] < 0 && srd_sdf_layout_get_adj(layout, cur, j)) {
                dist[j] = dist[cur] + 1;
                if (j == dst) return dist[j];
                queue[tail++] = j;
            }
        }
    }
    return -1;
}

/* ── Shared corridor-add apply ─────────────────────────────────────── */

static int corridor_add_apply(srd_sdf_layout_t *layout,
                              const srd_selection_t *sel,
                              const void *userdata,
                              int *new_box_indices, int cap) {
    (void)userdata;
    if (!layout || !sel || sel->n < 2) return -1;
    int i = sel->indices[0], j = sel->indices[1];
    if (i < 0 || i >= layout->n_boxes) return -1;
    if (j < 0 || j >= layout->n_boxes) return -1;

    srd_sdf_box_t corr;
    memset(&corr, 0, sizeof(corr));
    corr.cx = (layout->boxes[i].cx + layout->boxes[j].cx) * 0.5f;
    corr.cz = (layout->boxes[i].cz + layout->boxes[j].cz) * 0.5f;
    corr.hw = SRD_EPSILON;
    corr.hd = SRD_EPSILON;
    corr.type = SRD_ROOM_CORRIDOR;
    corr.flags = SRD_BOX_EPSILON;

    int k = srd_sdf_layout_add_box(layout, &corr);
    if (k < 0) return -1;
    srd_sdf_layout_set_adj(layout, i, k, true);
    srd_sdf_layout_set_adj(layout, j, k, true);

    if (new_box_indices && cap >= 1) new_box_indices[0] = k;
    return 1;
}

/* ── Shared corridor-remove apply ──────────────────────────────────── */

static int corridor_remove_apply(srd_sdf_layout_t *layout,
                                 const srd_selection_t *sel,
                                 const void *userdata,
                                 int *new_box_indices, int cap) {
    (void)userdata; (void)new_box_indices; (void)cap;
    if (!layout || !sel || sel->n < 1) return -1;
    int k = sel->indices[0];
    if (k < 0 || k >= layout->n_boxes) return -1;
    srd_sdf_layout_remove_box(layout, k);
    return 0;
}

/* ── BridgeComponents (Rule 25) ────────────────────────────────────── */

static bool bridge_cond(const srd_sdf_layout_t *layout,
                        const srd_selection_t *sel,
                        const void *userdata) {
    (void)userdata;
    if (!layout || !sel || sel->n < 2) return false;
    int i = sel->indices[0], j = sel->indices[1];
    if (i < 0 || i >= layout->n_boxes) return false;
    if (j < 0 || j >= layout->n_boxes) return false;
    if (i == j) return false;
    /* Must be in different connected components */
    return !bfs_reachable(layout, i, j, -1);
}

/* ── AddLoop (Rule 26) ─────────────────────────────────────────────── */

static bool add_loop_cond(const srd_sdf_layout_t *layout,
                          const srd_selection_t *sel,
                          const void *userdata) {
    (void)userdata;
    if (!layout || !sel || sel->n < 2) return false;
    int i = sel->indices[0], j = sel->indices[1];
    if (i < 0 || i >= layout->n_boxes) return false;
    if (j < 0 || j >= layout->n_boxes) return false;
    if (i == j) return false;
    /* Must be reachable (same component) but NOT directly adjacent */
    return bfs_reachable(layout, i, j, -1) &&
           !srd_sdf_layout_get_adj(layout, i, j);
}

/* ── RemoveLoop (Rule 27) ──────────────────────────────────────────── */

static bool remove_loop_cond(const srd_sdf_layout_t *layout,
                             const srd_selection_t *sel,
                             const void *userdata) {
    (void)userdata;
    if (!layout || !sel || sel->n < 1) return false;
    int k = sel->indices[0];
    if (k < 0 || k >= layout->n_boxes) return false;
    if (layout->boxes[k].type != SRD_ROOM_CORRIDOR) return false;

    /* Removing k must NOT disconnect the graph.
     * Check: for each pair of k's neighbours, they are still
     * reachable from each other without k. */
    int neighbours[SRD_MAX_BOXES];
    int n_nbrs = srd_sdf_layout_adj_list(layout, k, neighbours, SRD_MAX_BOXES);
    if (n_nbrs < 2) return true;  /* Leaf corridor — safe to remove */

    /* Check all neighbours stay connected to the first one */
    for (int i = 1; i < n_nbrs; i++) {
        if (!bfs_reachable(layout, neighbours[0], neighbours[i], k))
            return false;
    }
    return true;
}

/* ── ShortcutPath (Rule 28) ────────────────────────────────────────── */

static bool shortcut_cond(const srd_sdf_layout_t *layout,
                          const srd_selection_t *sel,
                          const void *userdata) {
    (void)userdata;
    if (!layout || !sel || sel->n < 2) return false;
    int i = sel->indices[0], j = sel->indices[1];
    if (i < 0 || i >= layout->n_boxes) return false;
    if (j < 0 || j >= layout->n_boxes) return false;
    if (i == j) return false;
    /* Shortest path > 2 hops */
    int d = bfs_distance(layout, i, j, -1);
    return d > 2;
}

/* ── RemoveShortcut (Rule 29) ──────────────────────────────────────── */

/* Same condition as RemoveLoop — just needs to not disconnect */
static bool remove_shortcut_cond(const srd_sdf_layout_t *layout,
                                 const srd_selection_t *sel,
                                 const void *userdata) {
    return remove_loop_cond(layout, sel, userdata);
}

/* ── RerouteCorridor (Rule 30) ─────────────────────────────────────── */

/**
 * Cond: sel[0] is a corridor, sel[1] is a box that is not already
 * connected to sel[0].
 */
static bool reroute_cond(const srd_sdf_layout_t *layout,
                         const srd_selection_t *sel,
                         const void *userdata) {
    (void)userdata;
    if (!layout || !sel || sel->n < 2) return false;
    int k = sel->indices[0], new_j = sel->indices[1];
    if (k < 0 || k >= layout->n_boxes) return false;
    if (new_j < 0 || new_j >= layout->n_boxes) return false;
    if (k == new_j) return false;
    return layout->boxes[k].type == SRD_ROOM_CORRIDOR &&
           !srd_sdf_layout_get_adj(layout, k, new_j);
}

/**
 * Move corridor k's second endpoint from old_j to new_j.
 * Finds the "second" neighbour (not the first) and rewires.
 */
static int reroute_apply(srd_sdf_layout_t *layout,
                         const srd_selection_t *sel,
                         const void *userdata,
                         int *new_box_indices, int cap) {
    (void)userdata; (void)new_box_indices; (void)cap;
    if (!layout || !sel || sel->n < 2) return -1;
    int k = sel->indices[0], new_j = sel->indices[1];
    if (k < 0 || k >= layout->n_boxes) return -1;
    if (new_j < 0 || new_j >= layout->n_boxes) return -1;

    /* Find neighbours of k */
    int neighbours[SRD_MAX_BOXES];
    int n_nbrs = srd_sdf_layout_adj_list(layout, k, neighbours, SRD_MAX_BOXES);
    if (n_nbrs == 0) return -1;

    /* Disconnect last neighbour, connect new_j */
    int old_j = neighbours[n_nbrs - 1];
    srd_sdf_layout_set_adj(layout, k, old_j, false);
    srd_sdf_layout_set_adj(layout, k, new_j, true);

    /* Update corridor centre to midpoint of remaining neighbour and new_j */
    int first_nbr = (n_nbrs > 1) ? neighbours[0] : new_j;
    layout->boxes[k].cx = (layout->boxes[first_nbr].cx +
                           layout->boxes[new_j].cx) * 0.5f;
    layout->boxes[k].cz = (layout->boxes[first_nbr].cz +
                           layout->boxes[new_j].cz) * 0.5f;
    return 0;
}

/* ── Registration ──────────────────────────────────────────────────── */

int srd_rules_corridor_register_graph(srd_rule_table_t *tbl) {
    if (!tbl) return -1;

    /* We need RemoveCorridor's index for BridgeComponents inverse.
     * It was already registered by register_basic. Find it by name. */
    int rem_corr_idx = -1;
    for (int i = 0; i < tbl->n_rules; i++) {
        if (tbl->rules[i].name &&
            strcmp(tbl->rules[i].name, "RemoveCorridor") == 0) {
            rem_corr_idx = i;
            break;
        }
    }

    /* BridgeComponents — inverse is RemoveCorridor */
    srd_descent_rule_t bridge = {0};
    bridge.name = "BridgeComponents";
    bridge.inverse_rule_id = rem_corr_idx;
    bridge.n_select = 2;
    bridge.jump_continuous = true;
    bridge.cond = bridge_cond;
    bridge.apply = corridor_add_apply;
    if (srd_rule_table_register(tbl, &bridge) < 0) return -1;

    /* RemoveLoop first (inverse for AddLoop) */
    srd_descent_rule_t rem_loop = {0};
    rem_loop.name = "RemoveLoop";
    rem_loop.inverse_rule_id = -1;
    rem_loop.n_select = 1;
    rem_loop.cond = remove_loop_cond;
    rem_loop.apply = corridor_remove_apply;
    int rem_loop_idx = srd_rule_table_register(tbl, &rem_loop);
    if (rem_loop_idx < 0) return -1;

    /* AddLoop */
    srd_descent_rule_t add_loop = {0};
    add_loop.name = "AddLoop";
    add_loop.inverse_rule_id = rem_loop_idx;
    add_loop.n_select = 2;
    add_loop.jump_continuous = true;
    add_loop.cond = add_loop_cond;
    add_loop.apply = corridor_add_apply;
    int add_loop_idx = srd_rule_table_register(tbl, &add_loop);
    if (add_loop_idx < 0) return -1;
    tbl->rules[rem_loop_idx].inverse_rule_id = add_loop_idx;

    /* RemoveShortcut first (inverse for ShortcutPath) */
    srd_descent_rule_t rem_short = {0};
    rem_short.name = "RemoveShortcut";
    rem_short.inverse_rule_id = -1;
    rem_short.n_select = 1;
    rem_short.cond = remove_shortcut_cond;
    rem_short.apply = corridor_remove_apply;
    int rem_short_idx = srd_rule_table_register(tbl, &rem_short);
    if (rem_short_idx < 0) return -1;

    /* ShortcutPath */
    srd_descent_rule_t shortcut = {0};
    shortcut.name = "ShortcutPath";
    shortcut.inverse_rule_id = rem_short_idx;
    shortcut.n_select = 2;
    shortcut.jump_continuous = true;
    shortcut.cond = shortcut_cond;
    shortcut.apply = corridor_add_apply;
    int short_idx = srd_rule_table_register(tbl, &shortcut);
    if (short_idx < 0) return -1;
    tbl->rules[rem_short_idx].inverse_rule_id = short_idx;

    /* RerouteCorridor — self-inverse */
    srd_descent_rule_t reroute = {0};
    reroute.name = "RerouteCorridor";
    reroute.inverse_rule_id = -1;
    reroute.n_select = 2;
    reroute.cond = reroute_cond;
    reroute.apply = reroute_apply;
    int reroute_idx = srd_rule_table_register(tbl, &reroute);
    if (reroute_idx < 0) return -1;
    tbl->rules[reroute_idx].inverse_rule_id = reroute_idx;

    return 6;
}
