/**
 * @file srd_rules_feature_special.c
 * @brief Special room rules (Rules 39-42): AddDeadEnd, RemoveDeadEnd,
 *        AddSecretRoom, RemoveSecretRoom.
 *
 * Non-static functions (1): srd_rules_feature_register_special
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

/* ── AddDeadEnd (Rule 39) ──────────────────────────────────────────── */

static int add_dead_end_apply(srd_sdf_layout_t *layout,
                              const srd_selection_t *sel,
                              const void *userdata,
                              int *new_box_indices, int cap) {
    (void)userdata;
    if (!layout || !sel || sel->n < 1) return -1;
    int i = sel->indices[0];
    if (i < 0 || i >= layout->n_boxes) return -1;

    const srd_sdf_box_t *anchor = &layout->boxes[i];
    srd_sdf_box_t de;
    memset(&de, 0, sizeof(de));
    de.cx = anchor->cx;
    de.cz = anchor->cz - anchor->hd - SRD_EPSILON;
    de.hw = SRD_EPSILON;
    de.hd = SRD_EPSILON;
    de.type = SRD_ROOM_DEAD_END;
    de.flags = SRD_BOX_EPSILON;

    int idx = srd_sdf_layout_add_box(layout, &de);
    if (idx < 0) return -1;
    srd_sdf_layout_set_adj(layout, i, idx, true);

    if (new_box_indices && cap >= 1) new_box_indices[0] = idx;
    return 1;
}

/* ── RemoveDeadEnd (Rule 40) ───────────────────────────────────────── */

static bool remove_dead_end_cond(const srd_sdf_layout_t *layout,
                                 const srd_selection_t *sel,
                                 const void *userdata) {
    (void)userdata;
    if (!layout || !sel || sel->n < 1) return false;
    int j = sel->indices[0];
    if (j < 0 || j >= layout->n_boxes) return false;
    return layout->boxes[j].type == SRD_ROOM_DEAD_END ||
           srd_sdf_layout_adj_count(layout, j) <= 1;
}

static int remove_dead_end_apply(srd_sdf_layout_t *layout,
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

/* ── AddSecretRoom (Rule 41) ───────────────────────────────────────── */

static int add_secret_room_apply(srd_sdf_layout_t *layout,
                                 const srd_selection_t *sel,
                                 const void *userdata,
                                 int *new_box_indices, int cap) {
    (void)userdata;
    if (!layout || !sel || sel->n < 1) return -1;
    int i = sel->indices[0];
    if (i < 0 || i >= layout->n_boxes) return -1;

    const srd_sdf_box_t *anchor = &layout->boxes[i];
    srd_sdf_box_t secret;
    memset(&secret, 0, sizeof(secret));
    secret.cx = anchor->cx + anchor->hw + SRD_EPSILON;
    secret.cz = anchor->cz;
    secret.hw = SRD_EPSILON;
    secret.hd = SRD_EPSILON;
    secret.type = SRD_ROOM_SECRET;
    secret.flags = SRD_BOX_EPSILON;

    int idx = srd_sdf_layout_add_box(layout, &secret);
    if (idx < 0) return -1;
    srd_sdf_layout_set_adj(layout, i, idx, true);

    if (new_box_indices && cap >= 1) new_box_indices[0] = idx;
    return 1;
}

/* ── RemoveSecretRoom (Rule 42) ────────────────────────────────────── */

static bool remove_secret_cond(const srd_sdf_layout_t *layout,
                               const srd_selection_t *sel,
                               const void *userdata) {
    (void)userdata;
    if (!layout || !sel || sel->n < 1) return false;
    int j = sel->indices[0];
    if (j < 0 || j >= layout->n_boxes) return false;
    return layout->boxes[j].type == SRD_ROOM_SECRET;
}

static int remove_secret_apply(srd_sdf_layout_t *layout,
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

int srd_rules_feature_register_special(srd_rule_table_t *tbl) {
    if (!tbl) return -1;

    /* RemoveDeadEnd first */
    srd_descent_rule_t rem_de = {0};
    rem_de.name = "RemoveDeadEnd";
    rem_de.inverse_rule_id = -1;
    rem_de.n_select = 1;
    rem_de.cond = remove_dead_end_cond;
    rem_de.apply = remove_dead_end_apply;
    int rem_de_idx = srd_rule_table_register(tbl, &rem_de);
    if (rem_de_idx < 0) return -1;

    /* AddDeadEnd */
    srd_descent_rule_t add_de = {0};
    add_de.name = "AddDeadEnd";
    add_de.inverse_rule_id = rem_de_idx;
    add_de.n_select = 1;
    add_de.jump_continuous = true;
    add_de.cond = box_exists_cond;
    add_de.apply = add_dead_end_apply;
    int add_de_idx = srd_rule_table_register(tbl, &add_de);
    if (add_de_idx < 0) return -1;
    tbl->rules[rem_de_idx].inverse_rule_id = add_de_idx;

    /* RemoveSecretRoom first */
    srd_descent_rule_t rem_sec = {0};
    rem_sec.name = "RemoveSecretRoom";
    rem_sec.inverse_rule_id = -1;
    rem_sec.n_select = 1;
    rem_sec.cond = remove_secret_cond;
    rem_sec.apply = remove_secret_apply;
    int rem_sec_idx = srd_rule_table_register(tbl, &rem_sec);
    if (rem_sec_idx < 0) return -1;

    /* AddSecretRoom */
    srd_descent_rule_t add_sec = {0};
    add_sec.name = "AddSecretRoom";
    add_sec.inverse_rule_id = rem_sec_idx;
    add_sec.n_select = 1;
    add_sec.jump_continuous = true;
    add_sec.cond = box_exists_cond;
    add_sec.apply = add_secret_room_apply;
    int add_sec_idx = srd_rule_table_register(tbl, &add_sec);
    if (add_sec_idx < 0) return -1;
    tbl->rules[rem_sec_idx].inverse_rule_id = add_sec_idx;

    return 4;
}
