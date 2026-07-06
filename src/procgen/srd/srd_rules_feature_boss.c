/**
 * @file srd_rules_feature_boss.c
 * @brief Boss and treasure room rules (Rules 43-46): AddBossRoom,
 *        RemoveBossRoom, AddTreasureRoom, RemoveTreasureRoom.
 *
 * Non-static functions (1): srd_rules_feature_register_boss
 */
#include "ferrum/procgen/srd/srd_rules_feature.h"

#include <string.h>

/* ── AddBossRoom (Rule 43) ─────────────────────────────────────────── */

/** Cond: no existing BOSS box in layout and anchor box exists. */
static bool add_boss_cond(const srd_sdf_layout_t *layout,
                          const srd_selection_t *sel,
                          const void *userdata) {
    (void)userdata;
    if (!layout || !sel || sel->n < 1) return false;
    int i = sel->indices[0];
    if (i < 0 || i >= layout->n_boxes) return false;

    /* Check uniqueness: no BOSS room already exists */
    for (int k = 0; k < layout->n_boxes; k++) {
        if (layout->boxes[k].type == SRD_ROOM_BOSS) return false;
    }
    return true;
}

static int add_boss_apply(srd_sdf_layout_t *layout,
                          const srd_selection_t *sel,
                          const void *userdata,
                          int *new_box_indices, int cap) {
    (void)userdata;
    if (!layout || !sel || sel->n < 1) return -1;
    int i = sel->indices[0];
    if (i < 0 || i >= layout->n_boxes) return -1;

    const srd_sdf_box_t *anchor = &layout->boxes[i];
    srd_sdf_box_t boss;
    memset(&boss, 0, sizeof(boss));
    boss.cx = anchor->cx;
    boss.cz = anchor->cz + anchor->hd + SRD_EPSILON;
    boss.hw = SRD_EPSILON;
    boss.hd = SRD_EPSILON;
    boss.type = SRD_ROOM_BOSS;
    boss.flags = SRD_BOX_EPSILON;

    int idx = srd_sdf_layout_add_box(layout, &boss);
    if (idx < 0) return -1;
    srd_sdf_layout_set_adj(layout, i, idx, true);

    if (new_box_indices && cap >= 1) new_box_indices[0] = idx;
    return 1;
}

/* ── RemoveBossRoom (Rule 44) ──────────────────────────────────────── */

static bool remove_boss_cond(const srd_sdf_layout_t *layout,
                             const srd_selection_t *sel,
                             const void *userdata) {
    (void)userdata;
    if (!layout || !sel || sel->n < 1) return false;
    int j = sel->indices[0];
    if (j < 0 || j >= layout->n_boxes) return false;
    return layout->boxes[j].type == SRD_ROOM_BOSS;
}

static int remove_boss_apply(srd_sdf_layout_t *layout,
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

/* ── AddTreasureRoom (Rule 45) ─────────────────────────────────────── */

static bool box_exists_cond(const srd_sdf_layout_t *layout,
                            const srd_selection_t *sel,
                            const void *userdata) {
    (void)userdata;
    if (!layout || !sel || sel->n < 1) return false;
    int i = sel->indices[0];
    return (i >= 0 && i < layout->n_boxes);
}

static int add_treasure_apply(srd_sdf_layout_t *layout,
                              const srd_selection_t *sel,
                              const void *userdata,
                              int *new_box_indices, int cap) {
    (void)userdata;
    if (!layout || !sel || sel->n < 1) return -1;
    int i = sel->indices[0];
    if (i < 0 || i >= layout->n_boxes) return -1;

    const srd_sdf_box_t *anchor = &layout->boxes[i];
    srd_sdf_box_t treasure;
    memset(&treasure, 0, sizeof(treasure));
    treasure.cx = anchor->cx - anchor->hw - SRD_EPSILON;
    treasure.cz = anchor->cz;
    treasure.hw = SRD_EPSILON;
    treasure.hd = SRD_EPSILON;
    treasure.type = SRD_ROOM_TREASURE;
    treasure.flags = SRD_BOX_EPSILON;

    int idx = srd_sdf_layout_add_box(layout, &treasure);
    if (idx < 0) return -1;
    srd_sdf_layout_set_adj(layout, i, idx, true);

    if (new_box_indices && cap >= 1) new_box_indices[0] = idx;
    return 1;
}

/* ── RemoveTreasureRoom (Rule 46) ──────────────────────────────────── */

static bool remove_treasure_cond(const srd_sdf_layout_t *layout,
                                 const srd_selection_t *sel,
                                 const void *userdata) {
    (void)userdata;
    if (!layout || !sel || sel->n < 1) return false;
    int j = sel->indices[0];
    if (j < 0 || j >= layout->n_boxes) return false;
    return layout->boxes[j].type == SRD_ROOM_TREASURE;
}

static int remove_treasure_apply(srd_sdf_layout_t *layout,
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

/* ── Registration ──────────────────────────────────────────────────── */

int srd_rules_feature_register_boss(srd_rule_table_t *tbl) {
    if (!tbl) return -1;

    /* RemoveBossRoom first */
    srd_descent_rule_t rem_boss = {0};
    rem_boss.name = "RemoveBossRoom";
    rem_boss.inverse_rule_id = -1;
    rem_boss.n_select = 1;
    rem_boss.cond = remove_boss_cond;
    rem_boss.apply = remove_boss_apply;
    int rem_boss_idx = srd_rule_table_register(tbl, &rem_boss);
    if (rem_boss_idx < 0) return -1;

    /* AddBossRoom */
    srd_descent_rule_t add_boss = {0};
    add_boss.name = "AddBossRoom";
    add_boss.inverse_rule_id = rem_boss_idx;
    add_boss.n_select = 1;
    add_boss.jump_continuous = true;
    add_boss.cond = add_boss_cond;
    add_boss.apply = add_boss_apply;
    int add_boss_idx = srd_rule_table_register(tbl, &add_boss);
    if (add_boss_idx < 0) return -1;
    tbl->rules[rem_boss_idx].inverse_rule_id = add_boss_idx;

    /* RemoveTreasureRoom first */
    srd_descent_rule_t rem_tr = {0};
    rem_tr.name = "RemoveTreasureRoom";
    rem_tr.inverse_rule_id = -1;
    rem_tr.n_select = 1;
    rem_tr.cond = remove_treasure_cond;
    rem_tr.apply = remove_treasure_apply;
    int rem_tr_idx = srd_rule_table_register(tbl, &rem_tr);
    if (rem_tr_idx < 0) return -1;

    /* AddTreasureRoom */
    srd_descent_rule_t add_tr = {0};
    add_tr.name = "AddTreasureRoom";
    add_tr.inverse_rule_id = rem_tr_idx;
    add_tr.n_select = 1;
    add_tr.jump_continuous = true;
    add_tr.cond = box_exists_cond;
    add_tr.apply = add_treasure_apply;
    int add_tr_idx = srd_rule_table_register(tbl, &add_tr);
    if (add_tr_idx < 0) return -1;
    tbl->rules[rem_tr_idx].inverse_rule_id = add_tr_idx;

    return 4;
}
