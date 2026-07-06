/**
 * @file srd_rules_corridor_shape.c
 * @brief Corridor shape rules (Rules 21-24): Bend, Straighten, Split, Merge.
 *
 * Non-static functions (1): srd_rules_corridor_register_shape
 */
#include "ferrum/procgen/srd/srd_rules_corridor.h"

#include <string.h>

/* ── BendCorridor (Rule 21) ────────────────────────────────────────── */

static bool bend_cond(const srd_sdf_layout_t *layout,
                      const srd_selection_t *sel,
                      const void *userdata) {
    (void)userdata;
    if (!layout || !sel || sel->n < 1) return false;
    int k = sel->indices[0];
    if (k < 0 || k >= layout->n_boxes) return false;
    return layout->boxes[k].type == SRD_ROOM_CORRIDOR;
}

/**
 * Split corridor k into two corridor segments at the midpoint.
 * Both inherit k's adjacency, plus they are adjacent to each other.
 */
static int bend_apply(srd_sdf_layout_t *layout,
                      const srd_selection_t *sel,
                      const void *userdata,
                      int *new_box_indices, int cap) {
    (void)userdata;
    if (!layout || !sel || sel->n < 1) return -1;
    int k = sel->indices[0];
    if (k < 0 || k >= layout->n_boxes) return -1;

    srd_sdf_box_t parent = layout->boxes[k];

    /* Two child corridors splitting along X */
    srd_sdf_box_t c1 = parent;
    c1.hw = parent.hw * 0.5f;
    c1.cx = parent.cx - parent.hw + c1.hw;

    srd_sdf_box_t c2 = parent;
    c2.hw = parent.hw * 0.5f;
    c2.cx = parent.cx + parent.hw - c2.hw;

    int neighbours[SRD_MAX_BOXES];
    int n_nbrs = srd_sdf_layout_adj_list(layout, k, neighbours, SRD_MAX_BOXES);

    srd_sdf_layout_remove_box(layout, k);
    for (int i = 0; i < n_nbrs; i++)
        if (neighbours[i] > k) neighbours[i]--;

    int i1 = srd_sdf_layout_add_box(layout, &c1);
    int i2 = srd_sdf_layout_add_box(layout, &c2);
    if (i1 < 0 || i2 < 0) return -1;

    srd_sdf_layout_set_adj(layout, i1, i2, true);
    for (int i = 0; i < n_nbrs; i++) {
        srd_sdf_layout_set_adj(layout, i1, neighbours[i], true);
        srd_sdf_layout_set_adj(layout, i2, neighbours[i], true);
    }

    if (new_box_indices && cap >= 2) {
        new_box_indices[0] = i1;
        new_box_indices[1] = i2;
    }
    return 2;
}

/* ── StraightenCorridor (Rule 22) ──────────────────────────────────── */

static bool straighten_cond(const srd_sdf_layout_t *layout,
                            const srd_selection_t *sel,
                            const void *userdata) {
    (void)userdata;
    if (!layout || !sel || sel->n < 2) return false;
    int k1 = sel->indices[0], k2 = sel->indices[1];
    if (k1 < 0 || k1 >= layout->n_boxes) return false;
    if (k2 < 0 || k2 >= layout->n_boxes) return false;
    return layout->boxes[k1].type == SRD_ROOM_CORRIDOR &&
           layout->boxes[k2].type == SRD_ROOM_CORRIDOR &&
           srd_sdf_layout_get_adj(layout, k1, k2);
}

/**
 * Merge two adjacent corridor segments into one spanning both.
 */
static int straighten_apply(srd_sdf_layout_t *layout,
                            const srd_selection_t *sel,
                            const void *userdata,
                            int *new_box_indices, int cap) {
    (void)userdata;
    if (!layout || !sel || sel->n < 2) return -1;
    int k1 = sel->indices[0], k2 = sel->indices[1];
    if (k1 < 0 || k1 >= layout->n_boxes) return -1;
    if (k2 < 0 || k2 >= layout->n_boxes) return -1;

    const srd_sdf_box_t *a = &layout->boxes[k1];
    const srd_sdf_box_t *b = &layout->boxes[k2];

    /* Union AABB */
    float min_x = (a->cx - a->hw < b->cx - b->hw) ? a->cx - a->hw : b->cx - b->hw;
    float max_x = (a->cx + a->hw > b->cx + b->hw) ? a->cx + a->hw : b->cx + b->hw;
    float min_z = (a->cz - a->hd < b->cz - b->hd) ? a->cz - a->hd : b->cz - b->hd;
    float max_z = (a->cz + a->hd > b->cz + b->hd) ? a->cz + a->hd : b->cz + b->hd;

    srd_sdf_box_t merged;
    memset(&merged, 0, sizeof(merged));
    merged.cx = (min_x + max_x) * 0.5f;
    merged.cz = (min_z + max_z) * 0.5f;
    merged.hw = (max_x - min_x) * 0.5f;
    merged.hd = (max_z - min_z) * 0.5f;
    merged.type = SRD_ROOM_CORRIDOR;

    /* Collect combined neighbours */
    int neighbours[SRD_MAX_BOXES];
    int n_nbrs = 0;
    for (int i = 0; i < layout->n_boxes; i++) {
        if (i == k1 || i == k2) continue;
        if (srd_sdf_layout_get_adj(layout, k1, i) ||
            srd_sdf_layout_get_adj(layout, k2, i))
            neighbours[n_nbrs++] = i;
    }

    int hi = (k1 > k2) ? k1 : k2;
    int lo = (k1 > k2) ? k2 : k1;
    srd_sdf_layout_remove_box(layout, hi);
    srd_sdf_layout_remove_box(layout, lo);

    for (int i = 0; i < n_nbrs; i++) {
        if (neighbours[i] > hi) neighbours[i]--;
        if (neighbours[i] > lo) neighbours[i]--;
    }

    int idx = srd_sdf_layout_add_box(layout, &merged);
    if (idx < 0) return -1;
    for (int i = 0; i < n_nbrs; i++)
        srd_sdf_layout_set_adj(layout, idx, neighbours[i], true);

    if (new_box_indices && cap >= 1) new_box_indices[0] = idx;
    return 1;
}

/* ── SplitCorridor (Rule 23) ───────────────────────────────────────── */

/**
 * Insert a waypoint room at the midpoint of corridor k.
 * Splits k into two corridor stubs connecting to the waypoint.
 */
static int split_corridor_apply(srd_sdf_layout_t *layout,
                                const srd_selection_t *sel,
                                const void *userdata,
                                int *new_box_indices, int cap) {
    (void)userdata;
    if (!layout || !sel || sel->n < 1) return -1;
    int k = sel->indices[0];
    if (k < 0 || k >= layout->n_boxes) return -1;

    srd_sdf_box_t parent = layout->boxes[k];

    /* Waypoint room at midpoint */
    srd_sdf_box_t waypoint;
    memset(&waypoint, 0, sizeof(waypoint));
    waypoint.cx = parent.cx;
    waypoint.cz = parent.cz;
    waypoint.hw = SRD_EPSILON;
    waypoint.hd = SRD_EPSILON;
    waypoint.type = SRD_ROOM_GENERIC;
    waypoint.flags = SRD_BOX_EPSILON;

    /* Two corridor stubs */
    srd_sdf_box_t c1 = parent;
    c1.hw = parent.hw * 0.5f;
    c1.cx = parent.cx - parent.hw + c1.hw;

    srd_sdf_box_t c2 = parent;
    c2.hw = parent.hw * 0.5f;
    c2.cx = parent.cx + parent.hw - c2.hw;

    int neighbours[SRD_MAX_BOXES];
    int n_nbrs = srd_sdf_layout_adj_list(layout, k, neighbours, SRD_MAX_BOXES);

    srd_sdf_layout_remove_box(layout, k);
    for (int i = 0; i < n_nbrs; i++)
        if (neighbours[i] > k) neighbours[i]--;

    int wi = srd_sdf_layout_add_box(layout, &waypoint);
    int ci1 = srd_sdf_layout_add_box(layout, &c1);
    int ci2 = srd_sdf_layout_add_box(layout, &c2);
    if (wi < 0 || ci1 < 0 || ci2 < 0) return -1;

    /* Waypoint connects to both corridor stubs */
    srd_sdf_layout_set_adj(layout, wi, ci1, true);
    srd_sdf_layout_set_adj(layout, wi, ci2, true);

    /* Stubs inherit parent's neighbours */
    for (int i = 0; i < n_nbrs; i++) {
        srd_sdf_layout_set_adj(layout, ci1, neighbours[i], true);
        srd_sdf_layout_set_adj(layout, ci2, neighbours[i], true);
    }

    if (new_box_indices && cap >= 3) {
        new_box_indices[0] = wi;
        new_box_indices[1] = ci1;
        new_box_indices[2] = ci2;
    }
    return 3;
}

/* ── MergeCorridor (Rule 24) ───────────────────────────────────────── */

/**
 * Cond: sel has 3 indices: k1 (corridor), w (waypoint), k2 (corridor).
 * w has exactly 2 neighbours (k1 and k2).
 */
static bool merge_corridor_cond(const srd_sdf_layout_t *layout,
                                const srd_selection_t *sel,
                                const void *userdata) {
    (void)userdata;
    if (!layout || !sel || sel->n < 3) return false;
    int k1 = sel->indices[0], w = sel->indices[1], k2 = sel->indices[2];
    if (k1 < 0 || k1 >= layout->n_boxes) return false;
    if (w < 0 || w >= layout->n_boxes) return false;
    if (k2 < 0 || k2 >= layout->n_boxes) return false;
    return layout->boxes[k1].type == SRD_ROOM_CORRIDOR &&
           layout->boxes[k2].type == SRD_ROOM_CORRIDOR &&
           srd_sdf_layout_get_adj(layout, k1, w) &&
           srd_sdf_layout_get_adj(layout, w, k2) &&
           srd_sdf_layout_adj_count(layout, w) == 2;
}

/**
 * Merge k1, w, k2 into a single corridor spanning both endpoints.
 */
static int merge_corridor_apply(srd_sdf_layout_t *layout,
                                const srd_selection_t *sel,
                                const void *userdata,
                                int *new_box_indices, int cap) {
    (void)userdata;
    if (!layout || !sel || sel->n < 3) return -1;
    int k1 = sel->indices[0], w = sel->indices[1], k2 = sel->indices[2];

    /* Compute union AABB of all three */
    const srd_sdf_box_t *boxes[3] = {
        &layout->boxes[k1], &layout->boxes[w], &layout->boxes[k2]
    };
    float min_x = boxes[0]->cx - boxes[0]->hw;
    float max_x = boxes[0]->cx + boxes[0]->hw;
    float min_z = boxes[0]->cz - boxes[0]->hd;
    float max_z = boxes[0]->cz + boxes[0]->hd;
    for (int i = 1; i < 3; i++) {
        float lx = boxes[i]->cx - boxes[i]->hw;
        float rx = boxes[i]->cx + boxes[i]->hw;
        float tz = boxes[i]->cz - boxes[i]->hd;
        float bz = boxes[i]->cz + boxes[i]->hd;
        if (lx < min_x) min_x = lx;
        if (rx > max_x) max_x = rx;
        if (tz < min_z) min_z = tz;
        if (bz > max_z) max_z = bz;
    }

    srd_sdf_box_t merged;
    memset(&merged, 0, sizeof(merged));
    merged.cx = (min_x + max_x) * 0.5f;
    merged.cz = (min_z + max_z) * 0.5f;
    merged.hw = (max_x - min_x) * 0.5f;
    merged.hd = (max_z - min_z) * 0.5f;
    merged.type = SRD_ROOM_CORRIDOR;

    /* Collect neighbours of all three (excluding k1, w, k2) */
    int neighbours[SRD_MAX_BOXES];
    int n_nbrs = 0;
    int remove_set[3] = {k1, w, k2};
    for (int i = 0; i < layout->n_boxes; i++) {
        if (i == k1 || i == w || i == k2) continue;
        bool is_nbr = false;
        for (int r = 0; r < 3; r++) {
            if (srd_sdf_layout_get_adj(layout, remove_set[r], i)) {
                is_nbr = true;
                break;
            }
        }
        if (is_nbr) neighbours[n_nbrs++] = i;
    }

    /* Sort remove indices descending to avoid shift issues */
    int sorted[3] = {k1, w, k2};
    for (int i = 0; i < 2; i++)
        for (int j = i + 1; j < 3; j++)
            if (sorted[i] < sorted[j]) { int t = sorted[i]; sorted[i] = sorted[j]; sorted[j] = t; }

    for (int i = 0; i < 3; i++) {
        srd_sdf_layout_remove_box(layout, sorted[i]);
        /* Shift remaining remove indices and neighbour indices */
        for (int n = 0; n < n_nbrs; n++)
            if (neighbours[n] > sorted[i]) neighbours[n]--;
    }

    int idx = srd_sdf_layout_add_box(layout, &merged);
    if (idx < 0) return -1;
    for (int i = 0; i < n_nbrs; i++)
        srd_sdf_layout_set_adj(layout, idx, neighbours[i], true);

    if (new_box_indices && cap >= 1) new_box_indices[0] = idx;
    return 1;
}

/* ── Registration ──────────────────────────────────────────────────── */

int srd_rules_corridor_register_shape(srd_rule_table_t *tbl) {
    if (!tbl) return -1;

    /* StraightenCorridor first (inverse for Bend) */
    srd_descent_rule_t straighten = {0};
    straighten.name = "StraightenCorridor";
    straighten.inverse_rule_id = -1;
    straighten.n_select = 2;
    straighten.cond = straighten_cond;
    straighten.apply = straighten_apply;
    int str_idx = srd_rule_table_register(tbl, &straighten);
    if (str_idx < 0) return -1;

    /* BendCorridor */
    srd_descent_rule_t bend = {0};
    bend.name = "BendCorridor";
    bend.inverse_rule_id = str_idx;
    bend.n_select = 1;
    bend.jump_continuous = true;
    bend.cond = bend_cond;
    bend.apply = bend_apply;
    int bend_idx = srd_rule_table_register(tbl, &bend);
    if (bend_idx < 0) return -1;
    tbl->rules[str_idx].inverse_rule_id = bend_idx;

    /* MergeCorridor first (inverse for Split) */
    srd_descent_rule_t merge = {0};
    merge.name = "MergeCorridor";
    merge.inverse_rule_id = -1;
    merge.n_select = 3;
    merge.cond = merge_corridor_cond;
    merge.apply = merge_corridor_apply;
    int merge_idx = srd_rule_table_register(tbl, &merge);
    if (merge_idx < 0) return -1;

    /* SplitCorridor */
    srd_descent_rule_t split = {0};
    split.name = "SplitCorridor";
    split.inverse_rule_id = merge_idx;
    split.n_select = 1;
    split.jump_continuous = true;
    split.cond = bend_cond;  /* Same cond: box is a corridor */
    split.apply = split_corridor_apply;
    int split_idx = srd_rule_table_register(tbl, &split);
    if (split_idx < 0) return -1;
    tbl->rules[merge_idx].inverse_rule_id = split_idx;

    return 4;
}
