/**
 * @file srd_rules_feature_stair.c
 * @brief Stair rules (Rules 35-38): AddStairUp, AddStairDown, RemoveStair, RelocateStair.
 *
 * Non-static functions (1): srd_rules_feature_register_stair
 */
#include "ferrum/procgen/srd/srd_rules_feature.h"

#include <string.h>

/* ── Shared: box exists condition ──────────────────────────────────── */

static bool box_exists_cond(const srd_sdf_layout_t *layout,
                            const srd_selection_t *sel,
                            const void *userdata) {
    (void)userdata;
    if (!layout || !sel || sel->n < 1) return false;
    int i = sel->indices[0];
    return (i >= 0 && i < layout->n_boxes);
}

/* ── RemoveStair (Rule 37) ─────────────────────────────────────────── */

static bool remove_stair_cond(const srd_sdf_layout_t *layout,
                              const srd_selection_t *sel,
                              const void *userdata) {
    (void)userdata;
    if (!layout || !sel || sel->n < 1) return false;
    int j = sel->indices[0];
    if (j < 0 || j >= layout->n_boxes) return false;
    return layout->boxes[j].type == SRD_ROOM_STAIR_UP ||
           layout->boxes[j].type == SRD_ROOM_STAIR_DOWN;
}

static int remove_stair_apply(srd_sdf_layout_t *layout,
                              const srd_selection_t *sel,
                              const void *userdata,
                              int *new_box_indices, int cap) {
    (void)userdata; (void)new_box_indices; (void)cap;
    if (!layout || !sel || sel->n < 1) return -1;
    int j = sel->indices[0];
    if (j < 0 || j >= layout->n_boxes) return -1;
    srd_sdf_layout_remove_box(layout, j);
    return 0;
}

/* ── AddStairUp (Rule 35) ──────────────────────────────────────────── */

static int add_stair_up_apply(srd_sdf_layout_t *layout,
                              const srd_selection_t *sel,
                              const void *userdata,
                              int *new_box_indices, int cap) {
    (void)userdata;
    if (!layout || !sel || sel->n < 1) return -1;
    int i = sel->indices[0];
    if (i < 0 || i >= layout->n_boxes) return -1;

    const srd_sdf_box_t *anchor = &layout->boxes[i];
    srd_sdf_box_t stair;
    memset(&stair, 0, sizeof(stair));
    stair.cx = anchor->cx;
    stair.cz = anchor->cz - anchor->hd - SRD_EPSILON;
    stair.hw = SRD_EPSILON;
    stair.hd = SRD_EPSILON;
    stair.type = SRD_ROOM_STAIR_UP;
    stair.flags = SRD_BOX_EPSILON;

    int idx = srd_sdf_layout_add_box(layout, &stair);
    if (idx < 0) return -1;
    srd_sdf_layout_set_adj(layout, i, idx, true);

    if (new_box_indices && cap >= 1) new_box_indices[0] = idx;
    return 1;
}

/* ── AddStairDown (Rule 36) ────────────────────────────────────────── */

static int add_stair_down_apply(srd_sdf_layout_t *layout,
                                const srd_selection_t *sel,
                                const void *userdata,
                                int *new_box_indices, int cap) {
    (void)userdata;
    if (!layout || !sel || sel->n < 1) return -1;
    int i = sel->indices[0];
    if (i < 0 || i >= layout->n_boxes) return -1;

    const srd_sdf_box_t *anchor = &layout->boxes[i];
    srd_sdf_box_t stair;
    memset(&stair, 0, sizeof(stair));
    stair.cx = anchor->cx;
    stair.cz = anchor->cz + anchor->hd + SRD_EPSILON;
    stair.hw = SRD_EPSILON;
    stair.hd = SRD_EPSILON;
    stair.type = SRD_ROOM_STAIR_DOWN;
    stair.flags = SRD_BOX_EPSILON;

    int idx = srd_sdf_layout_add_box(layout, &stair);
    if (idx < 0) return -1;
    srd_sdf_layout_set_adj(layout, i, idx, true);

    if (new_box_indices && cap >= 1) new_box_indices[0] = idx;
    return 1;
}

/* ── RelocateStair (Rule 38) ───────────────────────────────────────── */

static bool relocate_stair_cond(const srd_sdf_layout_t *layout,
                                const srd_selection_t *sel,
                                const void *userdata) {
    (void)userdata;
    if (!layout || !sel || sel->n < 2) return false;
    int j = sel->indices[0], target = sel->indices[1];
    if (j < 0 || j >= layout->n_boxes) return false;
    if (target < 0 || target >= layout->n_boxes) return false;
    if (j == target) return false;
    return layout->boxes[j].type == SRD_ROOM_STAIR_UP ||
           layout->boxes[j].type == SRD_ROOM_STAIR_DOWN;
}

static int relocate_stair_apply(srd_sdf_layout_t *layout,
                                const srd_selection_t *sel,
                                const void *userdata,
                                int *new_box_indices, int cap) {
    (void)userdata; (void)new_box_indices; (void)cap;
    if (!layout || !sel || sel->n < 2) return -1;
    int j = sel->indices[0], target = sel->indices[1];
    if (j < 0 || j >= layout->n_boxes) return -1;
    if (target < 0 || target >= layout->n_boxes) return -1;

    /* Clear all old adjacency for the stair */
    for (int k = 0; k < layout->n_boxes; k++) {
        if (k != j) srd_sdf_layout_set_adj(layout, j, k, false);
    }

    /* Move stair next to target */
    layout->boxes[j].cx = layout->boxes[target].cx;
    layout->boxes[j].cz = layout->boxes[target].cz -
                           layout->boxes[target].hd - SRD_EPSILON;
    srd_sdf_layout_set_adj(layout, j, target, true);
    return 0;
}

/* ── Registration ──────────────────────────────────────────────────── */

int srd_rules_feature_register_stair(srd_rule_table_t *tbl) {
    if (!tbl) return -1;

    /* RemoveStair first */
    srd_descent_rule_t rem = {0};
    rem.name = "RemoveStair";
    rem.inverse_rule_id = -1;
    rem.n_select = 1;
    rem.cond = remove_stair_cond;
    rem.apply = remove_stair_apply;
    int rem_idx = srd_rule_table_register(tbl, &rem);
    if (rem_idx < 0) return -1;

    /* AddStairUp */
    srd_descent_rule_t up = {0};
    up.name = "AddStairUp";
    up.inverse_rule_id = rem_idx;
    up.n_select = 1;
    up.jump_continuous = true;
    up.cond = box_exists_cond;
    up.apply = add_stair_up_apply;
    int up_idx = srd_rule_table_register(tbl, &up);
    if (up_idx < 0) return -1;
    tbl->rules[rem_idx].inverse_rule_id = up_idx;

    /* AddStairDown */
    srd_descent_rule_t down = {0};
    down.name = "AddStairDown";
    down.inverse_rule_id = rem_idx;
    down.n_select = 1;
    down.jump_continuous = true;
    down.cond = box_exists_cond;
    down.apply = add_stair_down_apply;
    if (srd_rule_table_register(tbl, &down) < 0) return -1;

    /* RelocateStair — self-inverse */
    srd_descent_rule_t reloc = {0};
    reloc.name = "RelocateStair";
    reloc.inverse_rule_id = -1;
    reloc.n_select = 2;
    reloc.cond = relocate_stair_cond;
    reloc.apply = relocate_stair_apply;
    int reloc_idx = srd_rule_table_register(tbl, &reloc);
    if (reloc_idx < 0) return -1;
    tbl->rules[reloc_idx].inverse_rule_id = reloc_idx;

    return 4;
}
