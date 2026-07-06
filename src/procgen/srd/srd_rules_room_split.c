/**
 * @file srd_rules_room_split.c
 * @brief Room split/merge rules (Rules 1-3): SplitRoomH, MergeRooms, SplitRoomV.
 *
 * Non-static functions (1): srd_rules_room_register_split
 */
#include "ferrum/procgen/srd/srd_rules_room.h"

#include <math.h>
#include <string.h>

/* ── SplitRoomH (Rule 1) ──────────────────────────────────────────── */

/**
 * Condition: box[sel[0]] must have hw > 2 * SRD_EPSILON so both
 * children can be at least EPSILON wide.
 */
static bool split_h_cond(const srd_sdf_layout_t *layout,
                         const srd_selection_t *sel,
                         const void *userdata) {
    (void)userdata;
    if (!layout || !sel || sel->n < 1) return false;
    int i = sel->indices[0];
    if (i < 0 || i >= layout->n_boxes) return false;
    return layout->boxes[i].hw > 2.0f * SRD_EPSILON;
}

/**
 * Apply: remove box[i], insert two children split along X at frac=0.5.
 * Children cover exactly the same area as parent (jump continuity).
 *
 * Returns 2 (number of new boxes added).
 */
static int split_h_apply(srd_sdf_layout_t *layout,
                         const srd_selection_t *sel,
                         const void *userdata,
                         int *new_box_indices, int cap) {
    (void)userdata;
    if (!layout || !sel || sel->n < 1) return -1;
    int i = sel->indices[0];
    if (i < 0 || i >= layout->n_boxes) return -1;

    /* Save parent box and its adjacency */
    srd_sdf_box_t parent = layout->boxes[i];
    float frac = 0.5f;  /* Default split fraction */

    /* Compute children */
    srd_sdf_box_t a = parent;
    a.hw = parent.hw * frac;
    a.cx = parent.cx - parent.hw + a.hw;

    srd_sdf_box_t b = parent;
    b.hw = parent.hw * (1.0f - frac);
    b.cx = parent.cx + parent.hw - b.hw;

    /* Save parent's neighbours before removal */
    int neighbours[SRD_MAX_BOXES];
    int n_nbrs = srd_sdf_layout_adj_list(layout, i, neighbours, SRD_MAX_BOXES);

    /* Adjust neighbour indices for the removal shift */
    /* Remove parent */
    srd_sdf_layout_remove_box(layout, i);

    /* Shift neighbour indices that were > i */
    for (int k = 0; k < n_nbrs; k++) {
        if (neighbours[k] > i) neighbours[k]--;
    }

    /* Add children */
    int idx_a = srd_sdf_layout_add_box(layout, &a);
    int idx_b = srd_sdf_layout_add_box(layout, &b);
    if (idx_a < 0 || idx_b < 0) return -1;

    /* Set adjacency: children are adjacent to each other */
    srd_sdf_layout_set_adj(layout, idx_a, idx_b, true);

    /* Inherit parent's adjacency for both children */
    for (int k = 0; k < n_nbrs; k++) {
        srd_sdf_layout_set_adj(layout, idx_a, neighbours[k], true);
        srd_sdf_layout_set_adj(layout, idx_b, neighbours[k], true);
    }

    if (new_box_indices && cap >= 2) {
        new_box_indices[0] = idx_a;
        new_box_indices[1] = idx_b;
    }
    return 2;
}

/* ── MergeRooms (Rule 2) ──────────────────────────────────────────── */

/**
 * Condition: boxes must be adjacent and share a full edge
 * (their hd values match for H-merge, or hw values match for V-merge).
 */
static bool merge_cond(const srd_sdf_layout_t *layout,
                       const srd_selection_t *sel,
                       const void *userdata) {
    (void)userdata;
    if (!layout || !sel || sel->n < 2) return false;
    int i = sel->indices[0];
    int j = sel->indices[1];
    if (i < 0 || i >= layout->n_boxes) return false;
    if (j < 0 || j >= layout->n_boxes) return false;
    if (!srd_sdf_layout_get_adj(layout, i, j)) return false;

    const srd_sdf_box_t *a = &layout->boxes[i];
    const srd_sdf_box_t *b = &layout->boxes[j];

    /* Check if boxes share a full edge along X (side-by-side horizontally) */
    float edge_gap_x = fabsf((a->cx + a->hw) - (b->cx - b->hw));
    float edge_gap_x2 = fabsf((b->cx + b->hw) - (a->cx - a->hw));
    float min_gap_x = (edge_gap_x < edge_gap_x2) ? edge_gap_x : edge_gap_x2;
    bool h_aligned = (min_gap_x < 0.1f) &&
                     (fabsf(a->cz - b->cz) < 0.1f) &&
                     (fabsf(a->hd - b->hd) < 0.1f);

    /* Check if boxes share a full edge along Z (stacked vertically) */
    float edge_gap_z = fabsf((a->cz + a->hd) - (b->cz - b->hd));
    float edge_gap_z2 = fabsf((b->cz + b->hd) - (a->cz - a->hd));
    float min_gap_z = (edge_gap_z < edge_gap_z2) ? edge_gap_z : edge_gap_z2;
    bool v_aligned = (min_gap_z < 0.1f) &&
                     (fabsf(a->cx - b->cx) < 0.1f) &&
                     (fabsf(a->hw - b->hw) < 0.1f);

    return h_aligned || v_aligned;
}

/**
 * Apply: remove boxes i and j, insert one box whose bounds are
 * the union AABB. Inherits type of the larger box and combined adjacency.
 *
 * Returns 1 (one new box).
 */
static int merge_apply(srd_sdf_layout_t *layout,
                       const srd_selection_t *sel,
                       const void *userdata,
                       int *new_box_indices, int cap) {
    (void)userdata;
    if (!layout || !sel || sel->n < 2) return -1;
    int i = sel->indices[0];
    int j = sel->indices[1];
    if (i < 0 || i >= layout->n_boxes) return -1;
    if (j < 0 || j >= layout->n_boxes) return -1;

    const srd_sdf_box_t *a = &layout->boxes[i];
    const srd_sdf_box_t *b = &layout->boxes[j];

    /* Compute union AABB */
    float min_x = (a->cx - a->hw < b->cx - b->hw) ? a->cx - a->hw : b->cx - b->hw;
    float max_x = (a->cx + a->hw > b->cx + b->hw) ? a->cx + a->hw : b->cx + b->hw;
    float min_z = (a->cz - a->hd < b->cz - b->hd) ? a->cz - a->hd : b->cz - b->hd;
    float max_z = (a->cz + a->hd > b->cz + b->hd) ? a->cz + a->hd : b->cz + b->hd;

    /* Inherit type from larger box */
    float area_a = a->hw * a->hd;
    float area_b = b->hw * b->hd;
    srd_room_type_t type = (area_a >= area_b) ? a->type : b->type;

    srd_sdf_box_t merged;
    memset(&merged, 0, sizeof(merged));
    merged.cx = (min_x + max_x) * 0.5f;
    merged.cz = (min_z + max_z) * 0.5f;
    merged.hw = (max_x - min_x) * 0.5f;
    merged.hd = (max_z - min_z) * 0.5f;
    merged.type = type;

    /* Collect combined neighbours (excluding i and j themselves) */
    int neighbours[SRD_MAX_BOXES];
    int n_nbrs = 0;
    for (int k = 0; k < layout->n_boxes; k++) {
        if (k == i || k == j) continue;
        if (srd_sdf_layout_get_adj(layout, i, k) ||
            srd_sdf_layout_get_adj(layout, j, k)) {
            neighbours[n_nbrs++] = k;
        }
    }

    /* Remove the higher index first to avoid shifting issues */
    int hi = (i > j) ? i : j;
    int lo = (i > j) ? j : i;
    srd_sdf_layout_remove_box(layout, hi);
    srd_sdf_layout_remove_box(layout, lo);

    /* Shift neighbour indices to account for removals */
    for (int k = 0; k < n_nbrs; k++) {
        if (neighbours[k] > hi) neighbours[k]--;
        if (neighbours[k] > lo) neighbours[k]--;
    }

    /* Add merged box */
    int idx = srd_sdf_layout_add_box(layout, &merged);
    if (idx < 0) return -1;

    /* Restore adjacency */
    for (int k = 0; k < n_nbrs; k++) {
        srd_sdf_layout_set_adj(layout, idx, neighbours[k], true);
    }

    if (new_box_indices && cap >= 1) {
        new_box_indices[0] = idx;
    }
    return 1;
}

/* ── SplitRoomV (Rule 3) ──────────────────────────────────────────── */

/**
 * Condition: box[sel[0]] must have hd > 2 * SRD_EPSILON.
 */
static bool split_v_cond(const srd_sdf_layout_t *layout,
                         const srd_selection_t *sel,
                         const void *userdata) {
    (void)userdata;
    if (!layout || !sel || sel->n < 1) return false;
    int i = sel->indices[0];
    if (i < 0 || i >= layout->n_boxes) return false;
    return layout->boxes[i].hd > 2.0f * SRD_EPSILON;
}

/**
 * Apply: same as SplitRoomH but along Z axis.
 */
static int split_v_apply(srd_sdf_layout_t *layout,
                         const srd_selection_t *sel,
                         const void *userdata,
                         int *new_box_indices, int cap) {
    (void)userdata;
    if (!layout || !sel || sel->n < 1) return -1;
    int i = sel->indices[0];
    if (i < 0 || i >= layout->n_boxes) return -1;

    srd_sdf_box_t parent = layout->boxes[i];
    float frac = 0.5f;

    srd_sdf_box_t a = parent;
    a.hd = parent.hd * frac;
    a.cz = parent.cz - parent.hd + a.hd;

    srd_sdf_box_t b = parent;
    b.hd = parent.hd * (1.0f - frac);
    b.cz = parent.cz + parent.hd - b.hd;

    int neighbours[SRD_MAX_BOXES];
    int n_nbrs = srd_sdf_layout_adj_list(layout, i, neighbours, SRD_MAX_BOXES);

    srd_sdf_layout_remove_box(layout, i);

    for (int k = 0; k < n_nbrs; k++) {
        if (neighbours[k] > i) neighbours[k]--;
    }

    int idx_a = srd_sdf_layout_add_box(layout, &a);
    int idx_b = srd_sdf_layout_add_box(layout, &b);
    if (idx_a < 0 || idx_b < 0) return -1;

    srd_sdf_layout_set_adj(layout, idx_a, idx_b, true);
    for (int k = 0; k < n_nbrs; k++) {
        srd_sdf_layout_set_adj(layout, idx_a, neighbours[k], true);
        srd_sdf_layout_set_adj(layout, idx_b, neighbours[k], true);
    }

    if (new_box_indices && cap >= 2) {
        new_box_indices[0] = idx_a;
        new_box_indices[1] = idx_b;
    }
    return 2;
}

/* ── Registration ──────────────────────────────────────────────────── */

int srd_rules_room_register_split(srd_rule_table_t *tbl) {
    if (!tbl) return -1;

    /* Register MergeRooms first so SplitH/V can reference it as inverse */
    srd_descent_rule_t merge = {0};
    merge.name = "MergeRooms";
    merge.inverse_rule_id = -1;  /* Patched below to point to SplitH */
    merge.n_select = 2;
    merge.cond = merge_cond;
    merge.apply = merge_apply;
    int merge_idx = srd_rule_table_register(tbl, &merge);
    if (merge_idx < 0) return -1;

    /* SplitRoomH (idx base+1) — inverse is MergeRooms */
    srd_descent_rule_t split_h = {0};
    split_h.name = "SplitRoomH";
    split_h.inverse_rule_id = merge_idx;
    split_h.n_select = 1;
    split_h.jump_continuous = true;
    split_h.cond = split_h_cond;
    split_h.apply = split_h_apply;
    int split_h_idx = srd_rule_table_register(tbl, &split_h);
    if (split_h_idx < 0) return -1;

    /* SplitRoomV (idx base+2) — inverse is MergeRooms */
    srd_descent_rule_t split_v = {0};
    split_v.name = "SplitRoomV";
    split_v.inverse_rule_id = merge_idx;
    split_v.n_select = 1;
    split_v.jump_continuous = true;
    split_v.cond = split_v_cond;
    split_v.apply = split_v_apply;
    int split_v_idx = srd_rule_table_register(tbl, &split_v);
    if (split_v_idx < 0) return -1;

    /* Patch MergeRooms inverse to point to SplitRoomH */
    tbl->rules[merge_idx].inverse_rule_id = split_h_idx;

    (void)split_v_idx;
    return 3;
}
