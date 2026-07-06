/**
 * @file srd_rules_feature_door.c
 * @brief Door rules (Rules 31-34): AddDoor, RemoveDoor, WidenDoor, NarrowDoor.
 *
 * Non-static functions (1): srd_rules_feature_register_door
 */
#include "ferrum/procgen/srd/srd_rules_feature.h"

/** Default wall side when userdata is NULL. */
#define DEFAULT_SIDE 0  /* N=0, S=1, E=2, W=3 */

/* ── AddDoor (Rule 31) ─────────────────────────────────────────────── */

static bool add_door_cond(const srd_sdf_layout_t *layout,
                          const srd_selection_t *sel,
                          const void *userdata) {
    (void)userdata;
    if (!layout || !sel || sel->n < 1) return false;
    int i = sel->indices[0];
    if (i < 0 || i >= layout->n_boxes) return false;
    int side = DEFAULT_SIDE;
    /* No door already on that wall */
    return layout->boxes[i].door_width[side] == 0.0f;
}

static int add_door_apply(srd_sdf_layout_t *layout,
                          const srd_selection_t *sel,
                          const void *userdata,
                          int *new_box_indices, int cap) {
    (void)userdata; (void)new_box_indices; (void)cap;
    if (!layout || !sel || sel->n < 1) return -1;
    int i = sel->indices[0];
    if (i < 0 || i >= layout->n_boxes) return -1;
    layout->boxes[i].door_width[DEFAULT_SIDE] = SRD_EPSILON;
    return 0;
}

/* ── RemoveDoor (Rule 32) ──────────────────────────────────────────── */

static bool remove_door_cond(const srd_sdf_layout_t *layout,
                             const srd_selection_t *sel,
                             const void *userdata) {
    (void)userdata;
    if (!layout || !sel || sel->n < 1) return false;
    int i = sel->indices[0];
    if (i < 0 || i >= layout->n_boxes) return false;
    return layout->boxes[i].door_width[DEFAULT_SIDE] > 0.0f;
}

static int remove_door_apply(srd_sdf_layout_t *layout,
                             const srd_selection_t *sel,
                             const void *userdata,
                             int *new_box_indices, int cap) {
    (void)userdata; (void)new_box_indices; (void)cap;
    if (!layout || !sel || sel->n < 1) return -1;
    int i = sel->indices[0];
    if (i < 0 || i >= layout->n_boxes) return -1;
    layout->boxes[i].door_width[DEFAULT_SIDE] = 0.0f;
    return 0;
}

/* ── WidenDoor (Rule 33) ───────────────────────────────────────────── */

static bool widen_door_cond(const srd_sdf_layout_t *layout,
                            const srd_selection_t *sel,
                            const void *userdata) {
    (void)userdata;
    if (!layout || !sel || sel->n < 1) return false;
    int i = sel->indices[0];
    if (i < 0 || i >= layout->n_boxes) return false;
    return layout->boxes[i].door_width[DEFAULT_SIDE] > 0.0f;
}

static int widen_door_apply(srd_sdf_layout_t *layout,
                            const srd_selection_t *sel,
                            const void *userdata,
                            int *new_box_indices, int cap) {
    (void)userdata; (void)new_box_indices; (void)cap;
    if (!layout || !sel || sel->n < 1) return -1;
    int i = sel->indices[0];
    if (i < 0 || i >= layout->n_boxes) return -1;
    layout->boxes[i].door_width[DEFAULT_SIDE] *= 1.5f;
    return 0;
}

/* ── NarrowDoor (Rule 34) ──────────────────────────────────────────── */

static bool narrow_door_cond(const srd_sdf_layout_t *layout,
                             const srd_selection_t *sel,
                             const void *userdata) {
    (void)userdata;
    if (!layout || !sel || sel->n < 1) return false;
    int i = sel->indices[0];
    if (i < 0 || i >= layout->n_boxes) return false;
    return layout->boxes[i].door_width[DEFAULT_SIDE] > SRD_EPSILON;
}

static int narrow_door_apply(srd_sdf_layout_t *layout,
                             const srd_selection_t *sel,
                             const void *userdata,
                             int *new_box_indices, int cap) {
    (void)userdata; (void)new_box_indices; (void)cap;
    if (!layout || !sel || sel->n < 1) return -1;
    int i = sel->indices[0];
    if (i < 0 || i >= layout->n_boxes) return -1;
    layout->boxes[i].door_width[DEFAULT_SIDE] /= 1.5f;
    return 0;
}

/* ── Registration ──────────────────────────────────────────────────── */

int srd_rules_feature_register_door(srd_rule_table_t *tbl) {
    if (!tbl) return -1;

    srd_descent_rule_t rem = {0};
    rem.name = "RemoveDoor";
    rem.inverse_rule_id = -1;
    rem.n_select = 1;
    rem.cond = remove_door_cond;
    rem.apply = remove_door_apply;
    int rem_idx = srd_rule_table_register(tbl, &rem);
    if (rem_idx < 0) return -1;

    srd_descent_rule_t add = {0};
    add.name = "AddDoor";
    add.inverse_rule_id = rem_idx;
    add.n_select = 1;
    add.jump_continuous = true;
    add.cond = add_door_cond;
    add.apply = add_door_apply;
    int add_idx = srd_rule_table_register(tbl, &add);
    if (add_idx < 0) return -1;
    tbl->rules[rem_idx].inverse_rule_id = add_idx;

    srd_descent_rule_t narrow = {0};
    narrow.name = "NarrowDoor";
    narrow.inverse_rule_id = -1;
    narrow.n_select = 1;
    narrow.jump_continuous = true;
    narrow.cond = narrow_door_cond;
    narrow.apply = narrow_door_apply;
    int narrow_idx = srd_rule_table_register(tbl, &narrow);
    if (narrow_idx < 0) return -1;

    srd_descent_rule_t widen = {0};
    widen.name = "WidenDoor";
    widen.inverse_rule_id = narrow_idx;
    widen.n_select = 1;
    widen.jump_continuous = true;
    widen.cond = widen_door_cond;
    widen.apply = widen_door_apply;
    int widen_idx = srd_rule_table_register(tbl, &widen);
    if (widen_idx < 0) return -1;
    tbl->rules[narrow_idx].inverse_rule_id = widen_idx;

    return 4;
}
