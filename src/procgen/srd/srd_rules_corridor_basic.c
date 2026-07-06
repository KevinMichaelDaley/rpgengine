/**
 * @file srd_rules_corridor_basic.c
 * @brief Basic corridor rules (Rules 17-20): Add, Remove, Widen, Narrow.
 *
 * Non-static functions (1): srd_rules_corridor_register_basic
 */
#include "ferrum/procgen/srd/srd_rules_corridor.h"

#include <string.h>

/* ── AddCorridor (Rule 17) ─────────────────────────────────────────── */

static bool add_corridor_cond(const srd_sdf_layout_t *layout,
                              const srd_selection_t *sel,
                              const void *userdata) {
    (void)userdata;
    if (!layout || !sel || sel->n < 2) return false;
    int i = sel->indices[0], j = sel->indices[1];
    if (i < 0 || i >= layout->n_boxes) return false;
    if (j < 0 || j >= layout->n_boxes) return false;
    return (i != j);
}

static int add_corridor_apply(srd_sdf_layout_t *layout,
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

/* ── RemoveCorridor (Rule 18) ──────────────────────────────────────── */

static bool remove_corridor_cond(const srd_sdf_layout_t *layout,
                                 const srd_selection_t *sel,
                                 const void *userdata) {
    (void)userdata;
    if (!layout || !sel || sel->n < 1) return false;
    int k = sel->indices[0];
    if (k < 0 || k >= layout->n_boxes) return false;
    return layout->boxes[k].type == SRD_ROOM_CORRIDOR;
}

static int remove_corridor_apply(srd_sdf_layout_t *layout,
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

/* ── WidenCorridor (Rule 19) ───────────────────────────────────────── */

static bool widen_corridor_cond(const srd_sdf_layout_t *layout,
                                const srd_selection_t *sel,
                                const void *userdata) {
    (void)userdata;
    if (!layout || !sel || sel->n < 1) return false;
    int k = sel->indices[0];
    if (k < 0 || k >= layout->n_boxes) return false;
    return layout->boxes[k].type == SRD_ROOM_CORRIDOR;
}

static int widen_corridor_apply(srd_sdf_layout_t *layout,
                                const srd_selection_t *sel,
                                const void *userdata,
                                int *new_box_indices, int cap) {
    (void)userdata; (void)new_box_indices; (void)cap;
    if (!layout || !sel || sel->n < 1) return -1;
    int k = sel->indices[0];
    if (k < 0 || k >= layout->n_boxes) return -1;
    layout->boxes[k].hw *= 1.5f;
    layout->boxes[k].hd *= 1.5f;
    return 0;
}

/* ── NarrowCorridor (Rule 20) ──────────────────────────────────────── */

static bool narrow_corridor_cond(const srd_sdf_layout_t *layout,
                                 const srd_selection_t *sel,
                                 const void *userdata) {
    (void)userdata;
    if (!layout || !sel || sel->n < 1) return false;
    int k = sel->indices[0];
    if (k < 0 || k >= layout->n_boxes) return false;
    return layout->boxes[k].type == SRD_ROOM_CORRIDOR &&
           layout->boxes[k].hw > SRD_EPSILON &&
           layout->boxes[k].hd > SRD_EPSILON;
}

static int narrow_corridor_apply(srd_sdf_layout_t *layout,
                                 const srd_selection_t *sel,
                                 const void *userdata,
                                 int *new_box_indices, int cap) {
    (void)userdata; (void)new_box_indices; (void)cap;
    if (!layout || !sel || sel->n < 1) return -1;
    int k = sel->indices[0];
    if (k < 0 || k >= layout->n_boxes) return -1;
    layout->boxes[k].hw /= 1.5f;
    layout->boxes[k].hd /= 1.5f;
    return 0;
}

/* ── Registration ──────────────────────────────────────────────────── */

int srd_rules_corridor_register_basic(srd_rule_table_t *tbl) {
    if (!tbl) return -1;

    /* RemoveCorridor first (inverse target for AddCorridor) */
    srd_descent_rule_t rem = {0};
    rem.name = "RemoveCorridor";
    rem.inverse_rule_id = -1;
    rem.n_select = 1;
    rem.cond = remove_corridor_cond;
    rem.apply = remove_corridor_apply;
    int rem_idx = srd_rule_table_register(tbl, &rem);
    if (rem_idx < 0) return -1;

    /* AddCorridor */
    srd_descent_rule_t add = {0};
    add.name = "AddCorridor";
    add.inverse_rule_id = rem_idx;
    add.n_select = 2;
    add.jump_continuous = true;
    add.cond = add_corridor_cond;
    add.apply = add_corridor_apply;
    int add_idx = srd_rule_table_register(tbl, &add);
    if (add_idx < 0) return -1;
    tbl->rules[rem_idx].inverse_rule_id = add_idx;

    /* NarrowCorridor first (inverse for Widen) */
    srd_descent_rule_t narrow = {0};
    narrow.name = "NarrowCorridor";
    narrow.inverse_rule_id = -1;
    narrow.n_select = 1;
    narrow.jump_continuous = true;
    narrow.cond = narrow_corridor_cond;
    narrow.apply = narrow_corridor_apply;
    int narrow_idx = srd_rule_table_register(tbl, &narrow);
    if (narrow_idx < 0) return -1;

    /* WidenCorridor */
    srd_descent_rule_t widen = {0};
    widen.name = "WidenCorridor";
    widen.inverse_rule_id = narrow_idx;
    widen.n_select = 1;
    widen.jump_continuous = true;
    widen.cond = widen_corridor_cond;
    widen.apply = widen_corridor_apply;
    int widen_idx = srd_rule_table_register(tbl, &widen);
    if (widen_idx < 0) return -1;
    tbl->rules[narrow_idx].inverse_rule_id = widen_idx;

    return 4;
}
