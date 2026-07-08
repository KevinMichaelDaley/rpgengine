/**
 * @file srd_rules_room_annex.c
 * @brief Alcove, antechamber, and type conversion rules (Rules 12-16).
 *
 * Non-static functions (1): srd_rules_room_register_annex
 */
#include "ferrum/procgen/srd/srd_rules_room.h"

#include <string.h>

/* ── Shared: box-exists condition ──────────────────────────────────── */

static bool box_exists_cond(const srd_sdf_layout_t *layout,
                            const srd_selection_t *sel,
                            const void *userdata) {
    (void)userdata;
    if (!layout || !sel || sel->n < 1) return false;
    int i = sel->indices[0];
    return (i >= 0 && i < layout->n_boxes);
}

/* ── AddAlcove (Rule 12) ──────────────────────────────────────────── */

/**
 * Apply: spawn an epsilon-sized alcove adjacent to box[i] on the north side.
 * Both hw and hd are SRD_EPSILON.
 */
static int add_alcove_apply(srd_sdf_layout_t *layout,
                            const srd_selection_t *sel,
                            const void *userdata,
                            int *new_box_indices, int cap) {
    (void)userdata;
    if (!layout || !sel || sel->n < 1) return -1;
    int i = sel->indices[0];
    if (i < 0 || i >= layout->n_boxes) return -1;

    const srd_sdf_box_t *anchor = &layout->boxes[i];
    srd_sdf_box_t alcove;
    memset(&alcove, 0, sizeof(alcove));
    alcove.cx = anchor->cx;
    alcove.cz = anchor->cz - anchor->hd - SRD_EPSILON;
    alcove.hw = SRD_EPSILON;
    alcove.hd = SRD_EPSILON;
    alcove.type = SRD_ROOM_GENERIC;
    alcove.flags = SRD_BOX_EPSILON;

    int idx = srd_sdf_layout_add_box(layout, &alcove);
    if (idx < 0) return -1;
    srd_sdf_layout_set_adj(layout, i, idx, true);

    if (new_box_indices && cap >= 1) new_box_indices[0] = idx;
    return 1;
}

/* ── RemoveAlcove (Rule 13) ────────────────────────────────────────── */

/**
 * Condition: box has at most 1 neighbour (an alcove is a leaf node).
 */
static bool remove_alcove_cond(const srd_sdf_layout_t *layout,
                               const srd_selection_t *sel,
                               const void *userdata) {
    (void)userdata;
    if (!layout || !sel || sel->n < 1) return false;
    int j = sel->indices[0];
    if (j < 0 || j >= layout->n_boxes) return false;
    return srd_sdf_layout_adj_count(layout, j) <= 1;
}

/**
 * Apply: remove box[j] and its adjacency.
 */
static int remove_alcove_apply(srd_sdf_layout_t *layout,
                               const srd_selection_t *sel,
                               const void *userdata,
                               int *new_box_indices, int cap) {
    (void)userdata;
    (void)new_box_indices;
    (void)cap;
    if (!layout || !sel || sel->n < 1) return -1;
    int j = sel->indices[0];
    if (j < 0 || j >= layout->n_boxes) return -1;
    srd_sdf_layout_remove_box(layout, j);
    return 0;
}

/* ── AddAntechamber (Rule 14) ──────────────────────────────────────── */

/**
 * Apply: spawn an antechamber adjacent to box[i] on the north side.
 * Antechamber is wider than an alcove (hw = anchor->hw * 0.5) but
 * short (hd = SRD_EPSILON).
 */
static int add_antechamber_apply(srd_sdf_layout_t *layout,
                                 const srd_selection_t *sel,
                                 const void *userdata,
                                 int *new_box_indices, int cap) {
    (void)userdata;
    if (!layout || !sel || sel->n < 1) return -1;
    int i = sel->indices[0];
    if (i < 0 || i >= layout->n_boxes) return -1;

    const srd_sdf_box_t *anchor = &layout->boxes[i];
    srd_sdf_box_t ante;
    memset(&ante, 0, sizeof(ante));
    ante.cx = anchor->cx;
    ante.cz = anchor->cz - anchor->hd - SRD_EPSILON;
    ante.hw = anchor->hw * 0.5f;
    ante.hd = SRD_EPSILON;
    ante.type = SRD_ROOM_GENERIC;
    ante.flags = SRD_BOX_EPSILON;

    int idx = srd_sdf_layout_add_box(layout, &ante);
    if (idx < 0) return -1;
    srd_sdf_layout_set_adj(layout, i, idx, true);

    if (new_box_indices && cap >= 1) new_box_indices[0] = idx;
    return 1;
}

/* ── RemoveAntechamber (Rule 15) ───────────────────────────────────── */

/**
 * Condition: box has exactly 1 neighbour.
 */
static bool remove_antechamber_cond(const srd_sdf_layout_t *layout,
                                    const srd_selection_t *sel,
                                    const void *userdata) {
    (void)userdata;
    if (!layout || !sel || sel->n < 1) return false;
    int j = sel->indices[0];
    if (j < 0 || j >= layout->n_boxes) return false;
    return srd_sdf_layout_adj_count(layout, j) == 1;
}

/**
 * Apply: remove box[j].
 */
static int remove_antechamber_apply(srd_sdf_layout_t *layout,
                                    const srd_selection_t *sel,
                                    const void *userdata,
                                    int *new_box_indices, int cap) {
    (void)userdata;
    (void)new_box_indices;
    (void)cap;
    if (!layout || !sel || sel->n < 1) return -1;
    int j = sel->indices[0];
    if (j < 0 || j >= layout->n_boxes) return -1;
    srd_sdf_layout_remove_box(layout, j);
    return 0;
}

/* ── ConvertType (Rule 16) ─────────────────────────────────────────── */

/**
 * Condition: box exists (always valid for type conversion).
 */
static bool convert_type_cond(const srd_sdf_layout_t *layout,
                              const srd_selection_t *sel,
                              const void *userdata) {
    (void)userdata;
    if (!layout || !sel || sel->n < 1) return false;
    int i = sel->indices[0];
    return (i >= 0 && i < layout->n_boxes);
}

/**
 * Apply: cycle to the next room type. No geometry change.
 * In practice, the SRD loop would use userdata to specify the target type.
 */
static int convert_type_apply(srd_sdf_layout_t *layout,
                              const srd_selection_t *sel,
                              const void *userdata,
                              int *new_box_indices, int cap) {
    (void)userdata;
    (void)new_box_indices;
    (void)cap;
    if (!layout || !sel || sel->n < 1) return -1;
    int i = sel->indices[0];
    if (i < 0 || i >= layout->n_boxes) return -1;

    /* Cycle to next type, wrapping around */
    int next = ((int)layout->boxes[i].type + 1) % SRD_ROOM_TYPE_COUNT;
    layout->boxes[i].type = (srd_room_type_t)next;
    return 0;
}

/* ── Registration ──────────────────────────────────────────────────── */

int srd_rules_room_register_annex(srd_rule_table_t *tbl) {
    if (!tbl) return -1;

    /* Only ConvertType is registered for the descent loop.
     *
     * AddAlcove / RemoveAlcove / AddAntechamber / RemoveAntechamber
     * are excluded because they add or remove entire rooms, which
     * violates the local-rewrite invariant.  Descent rules must be
     * jump-continuous: they refine the seed geometry (split, resize,
     * retype) but never create or destroy map features. */

    /* ConvertType — self-inverse */
    srd_descent_rule_t conv = {0};
    conv.name = "ConvertType";
    conv.inverse_rule_id = -1;
    conv.n_select = 1;
    conv.jump_continuous = true;
    conv.cond = convert_type_cond;
    conv.apply = convert_type_apply;
    int conv_idx = srd_rule_table_register(tbl, &conv);
    if (conv_idx < 0) return -1;

    /* Self-inverse */
    tbl->rules[conv_idx].inverse_rule_id = conv_idx;

    return 1;
}
