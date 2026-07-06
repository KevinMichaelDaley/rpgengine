/**
 * @file srd_rules_room_add.c
 * @brief Add/Remove room rules (Rules 4-8): AddRoomN/S/E/W, RemoveRoom.
 *
 * Non-static functions (1): srd_rules_room_register_add
 */
#include "ferrum/procgen/srd/srd_rules_room.h"

#include <string.h>

/* ── Shared condition: box exists ──────────────────────────────────── */

static bool box_exists_cond(const srd_sdf_layout_t *layout,
                            const srd_selection_t *sel,
                            const void *userdata) {
    (void)userdata;
    if (!layout || !sel || sel->n < 1) return false;
    int i = sel->indices[0];
    return (i >= 0 && i < layout->n_boxes);
}

/* ── RemoveRoom (Rule 8) ──────────────────────────────────────────── */

static int remove_room_apply(srd_sdf_layout_t *layout,
                             const srd_selection_t *sel,
                             const void *userdata,
                             int *new_box_indices, int cap) {
    (void)userdata;
    (void)new_box_indices;
    (void)cap;
    if (!layout || !sel || sel->n < 1) return -1;
    int i = sel->indices[0];
    if (i < 0 || i >= layout->n_boxes) return -1;
    srd_sdf_layout_remove_box(layout, i);
    return 0;
}

/* ── AddRoomN (Rule 4) ─────────────────────────────────────────────── */

static int add_room_n_apply(srd_sdf_layout_t *layout,
                            const srd_selection_t *sel,
                            const void *userdata,
                            int *new_box_indices, int cap) {
    (void)userdata;
    if (!layout || !sel || sel->n < 1) return -1;
    int i = sel->indices[0];
    if (i < 0 || i >= layout->n_boxes) return -1;

    const srd_sdf_box_t *anchor = &layout->boxes[i];
    srd_sdf_box_t room;
    memset(&room, 0, sizeof(room));
    room.cx = anchor->cx;
    room.cz = anchor->cz - anchor->hd - SRD_EPSILON;
    room.hw = anchor->hw;
    room.hd = SRD_EPSILON;
    room.type = SRD_ROOM_GENERIC;
    room.flags = SRD_BOX_EPSILON;

    int idx = srd_sdf_layout_add_box(layout, &room);
    if (idx < 0) return -1;
    srd_sdf_layout_set_adj(layout, i, idx, true);

    if (new_box_indices && cap >= 1) new_box_indices[0] = idx;
    return 1;
}

/* ── AddRoomS (Rule 5) ─────────────────────────────────────────────── */

static int add_room_s_apply(srd_sdf_layout_t *layout,
                            const srd_selection_t *sel,
                            const void *userdata,
                            int *new_box_indices, int cap) {
    (void)userdata;
    if (!layout || !sel || sel->n < 1) return -1;
    int i = sel->indices[0];
    if (i < 0 || i >= layout->n_boxes) return -1;

    const srd_sdf_box_t *anchor = &layout->boxes[i];
    srd_sdf_box_t room;
    memset(&room, 0, sizeof(room));
    room.cx = anchor->cx;
    room.cz = anchor->cz + anchor->hd + SRD_EPSILON;
    room.hw = anchor->hw;
    room.hd = SRD_EPSILON;
    room.type = SRD_ROOM_GENERIC;
    room.flags = SRD_BOX_EPSILON;

    int idx = srd_sdf_layout_add_box(layout, &room);
    if (idx < 0) return -1;
    srd_sdf_layout_set_adj(layout, i, idx, true);

    if (new_box_indices && cap >= 1) new_box_indices[0] = idx;
    return 1;
}

/* ── AddRoomE (Rule 6) ─────────────────────────────────────────────── */

static int add_room_e_apply(srd_sdf_layout_t *layout,
                            const srd_selection_t *sel,
                            const void *userdata,
                            int *new_box_indices, int cap) {
    (void)userdata;
    if (!layout || !sel || sel->n < 1) return -1;
    int i = sel->indices[0];
    if (i < 0 || i >= layout->n_boxes) return -1;

    const srd_sdf_box_t *anchor = &layout->boxes[i];
    srd_sdf_box_t room;
    memset(&room, 0, sizeof(room));
    room.cx = anchor->cx + anchor->hw + SRD_EPSILON;
    room.cz = anchor->cz;
    room.hw = SRD_EPSILON;
    room.hd = anchor->hd;
    room.type = SRD_ROOM_GENERIC;
    room.flags = SRD_BOX_EPSILON;

    int idx = srd_sdf_layout_add_box(layout, &room);
    if (idx < 0) return -1;
    srd_sdf_layout_set_adj(layout, i, idx, true);

    if (new_box_indices && cap >= 1) new_box_indices[0] = idx;
    return 1;
}

/* ── AddRoomW (Rule 7) ─────────────────────────────────────────────── */

static int add_room_w_apply(srd_sdf_layout_t *layout,
                            const srd_selection_t *sel,
                            const void *userdata,
                            int *new_box_indices, int cap) {
    (void)userdata;
    if (!layout || !sel || sel->n < 1) return -1;
    int i = sel->indices[0];
    if (i < 0 || i >= layout->n_boxes) return -1;

    const srd_sdf_box_t *anchor = &layout->boxes[i];
    srd_sdf_box_t room;
    memset(&room, 0, sizeof(room));
    room.cx = anchor->cx - anchor->hw - SRD_EPSILON;
    room.cz = anchor->cz;
    room.hw = SRD_EPSILON;
    room.hd = anchor->hd;
    room.type = SRD_ROOM_GENERIC;
    room.flags = SRD_BOX_EPSILON;

    int idx = srd_sdf_layout_add_box(layout, &room);
    if (idx < 0) return -1;
    srd_sdf_layout_set_adj(layout, i, idx, true);

    if (new_box_indices && cap >= 1) new_box_indices[0] = idx;
    return 1;
}

/* ── Registration ──────────────────────────────────────────────────── */

int srd_rules_room_register_add(srd_rule_table_t *tbl) {
    if (!tbl) return -1;

    /* Register RemoveRoom first (so Add* can reference it as inverse) */
    srd_descent_rule_t remove = {0};
    remove.name = "RemoveRoom";
    remove.inverse_rule_id = -1;  /* Patched below */
    remove.n_select = 1;
    remove.cond = box_exists_cond;
    remove.apply = remove_room_apply;
    int remove_idx = srd_rule_table_register(tbl, &remove);
    if (remove_idx < 0) return -1;

    /* AddRoomN */
    srd_descent_rule_t add_n = {0};
    add_n.name = "AddRoomN";
    add_n.inverse_rule_id = remove_idx;
    add_n.n_select = 1;
    add_n.jump_continuous = true;
    add_n.cond = box_exists_cond;
    add_n.apply = add_room_n_apply;
    int add_n_idx = srd_rule_table_register(tbl, &add_n);
    if (add_n_idx < 0) return -1;

    /* Patch RemoveRoom's inverse to point to AddRoomN */
    tbl->rules[remove_idx].inverse_rule_id = add_n_idx;

    /* AddRoomS */
    srd_descent_rule_t add_s = {0};
    add_s.name = "AddRoomS";
    add_s.inverse_rule_id = remove_idx;
    add_s.n_select = 1;
    add_s.jump_continuous = true;
    add_s.cond = box_exists_cond;
    add_s.apply = add_room_s_apply;
    if (srd_rule_table_register(tbl, &add_s) < 0) return -1;

    /* AddRoomE */
    srd_descent_rule_t add_e = {0};
    add_e.name = "AddRoomE";
    add_e.inverse_rule_id = remove_idx;
    add_e.n_select = 1;
    add_e.jump_continuous = true;
    add_e.cond = box_exists_cond;
    add_e.apply = add_room_e_apply;
    if (srd_rule_table_register(tbl, &add_e) < 0) return -1;

    /* AddRoomW */
    srd_descent_rule_t add_w = {0};
    add_w.name = "AddRoomW";
    add_w.inverse_rule_id = remove_idx;
    add_w.n_select = 1;
    add_w.jump_continuous = true;
    add_w.cond = box_exists_cond;
    add_w.apply = add_room_w_apply;
    if (srd_rule_table_register(tbl, &add_w) < 0) return -1;

    return 5;
}
