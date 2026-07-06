/**
 * @file srd_rules_repair.c
 * @brief Repair rules (5): ResolveOverlap, RepairContained, AlignWall,
 *        ClampToBounds, EnsureConnected.
 *
 * All repair rules have is_repair=true and inverse_rule_id=-1.
 * They are excluded from srd_rule_find_applicable.
 *
 * Non-static functions (1): srd_rules_repair_register
 */
#include "ferrum/procgen/srd/srd_rules_repair.h"

#include <math.h>
#include <string.h>

/** Alignment threshold: walls closer than this are snapped together. */
#define ALIGN_THRESHOLD 3.0f

/* ── ResolveOverlap (Repair 1) ─────────────────────────────────── */

/** Cond: two valid boxes that overlap. */
static bool resolve_overlap_cond(const srd_sdf_layout_t *layout,
                                 const srd_selection_t *sel,
                                 const void *userdata) {
    (void)userdata;
    if (!layout || !sel || sel->n < 2) return false;
    int i = sel->indices[0], j = sel->indices[1];
    if (i < 0 || i >= layout->n_boxes) return false;
    if (j < 0 || j >= layout->n_boxes) return false;
    if (i == j) return false;
    return srd_sdf_box_overlap(&layout->boxes[i], &layout->boxes[j]);
}

/** Apply: push boxes apart along the axis of least overlap. */
static int resolve_overlap_apply(srd_sdf_layout_t *layout,
                                 const srd_selection_t *sel,
                                 const void *userdata,
                                 int *new_box_indices, int cap) {
    (void)userdata; (void)new_box_indices; (void)cap;
    if (!layout || !sel || sel->n < 2) return -1;
    int i = sel->indices[0], j = sel->indices[1];
    if (i < 0 || i >= layout->n_boxes) return -1;
    if (j < 0 || j >= layout->n_boxes) return -1;

    srd_sdf_box_t *a = &layout->boxes[i];
    srd_sdf_box_t *b = &layout->boxes[j];

    /* Compute overlap on each axis */
    float overlap_x = (a->hw + b->hw) - fabsf(a->cx - b->cx);
    float overlap_z = (a->hd + b->hd) - fabsf(a->cz - b->cz);

    if (overlap_x <= 0.0f || overlap_z <= 0.0f) return 0; /* no overlap */

    /* Push apart along axis of minimum penetration */
    if (overlap_x <= overlap_z) {
        float half = overlap_x * 0.5f + 0.001f;
        if (a->cx <= b->cx) {
            a->cx -= half;
            b->cx += half;
        } else {
            a->cx += half;
            b->cx -= half;
        }
    } else {
        float half = overlap_z * 0.5f + 0.001f;
        if (a->cz <= b->cz) {
            a->cz -= half;
            b->cz += half;
        } else {
            a->cz += half;
            b->cz -= half;
        }
    }
    return 0;
}

/* ── RepairContained (Repair 2) ────────────────────────────────── */

/** Cond: inner box is fully contained within outer box. */
static bool repair_contained_cond(const srd_sdf_layout_t *layout,
                                  const srd_selection_t *sel,
                                  const void *userdata) {
    (void)userdata;
    if (!layout || !sel || sel->n < 2) return false;
    int outer = sel->indices[0], inner = sel->indices[1];
    if (outer < 0 || outer >= layout->n_boxes) return false;
    if (inner < 0 || inner >= layout->n_boxes) return false;
    if (outer == inner) return false;
    return srd_sdf_box_contains(&layout->boxes[outer],
                                &layout->boxes[inner]);
}

/**
 * Apply: if inner has no neighbours outside outer, remove it.
 * Otherwise, extend inner so it protrudes beyond outer.
 */
static int repair_contained_apply(srd_sdf_layout_t *layout,
                                  const srd_selection_t *sel,
                                  const void *userdata,
                                  int *new_box_indices, int cap) {
    (void)userdata; (void)new_box_indices; (void)cap;
    if (!layout || !sel || sel->n < 2) return -1;
    int outer_idx = sel->indices[0], inner_idx = sel->indices[1];
    if (outer_idx < 0 || outer_idx >= layout->n_boxes) return -1;
    if (inner_idx < 0 || inner_idx >= layout->n_boxes) return -1;

    /* Check if inner has neighbours other than outer */
    bool has_outside_neighbour = false;
    for (int k = 0; k < layout->n_boxes; k++) {
        if (k == outer_idx || k == inner_idx) continue;
        if (srd_sdf_layout_get_adj(layout, inner_idx, k)) {
            has_outside_neighbour = true;
            break;
        }
    }

    if (!has_outside_neighbour) {
        /* Remove the isolated inner box */
        srd_sdf_layout_remove_box(layout, inner_idx);
        return 0;
    }

    /* Extend inner so it protrudes beyond outer on the east side */
    srd_sdf_box_t *o = &layout->boxes[outer_idx];
    srd_sdf_box_t *in = &layout->boxes[inner_idx];

    float outer_right = o->cx + o->hw;
    float inner_right = in->cx + in->hw;
    if (inner_right <= outer_right) {
        /* Need to extend right past outer */
        float extend = (outer_right - inner_right) + 0.5f;
        in->hw += extend * 0.5f;
        in->cx += extend * 0.5f;
    }
    return 0;
}

/* ── AlignWall (Repair 3) ──────────────────────────────────────── */

/**
 * Check the minimum gap between any pair of facing walls of two
 * adjacent boxes. Returns the smallest gap found, or a large value
 * if no walls are close.
 */
static float min_wall_gap(const srd_sdf_box_t *a, const srd_sdf_box_t *b) {
    float best = 1e9f;

    /* A's right wall vs B's left wall */
    float gap = fabsf((a->cx + a->hw) - (b->cx - b->hw));
    if (gap < best) best = gap;

    /* A's left wall vs B's right wall */
    gap = fabsf((a->cx - a->hw) - (b->cx + b->hw));
    if (gap < best) best = gap;

    /* A's top wall vs B's bottom wall */
    gap = fabsf((a->cz + a->hd) - (b->cz - b->hd));
    if (gap < best) best = gap;

    /* A's bottom wall vs B's top wall */
    gap = fabsf((a->cz - a->hd) - (b->cz + b->hd));
    if (gap < best) best = gap;

    return best;
}

/** Cond: adjacent boxes have a wall gap within ALIGN_THRESHOLD but > 0. */
static bool align_wall_cond(const srd_sdf_layout_t *layout,
                            const srd_selection_t *sel,
                            const void *userdata) {
    (void)userdata;
    if (!layout || !sel || sel->n < 2) return false;
    int i = sel->indices[0], j = sel->indices[1];
    if (i < 0 || i >= layout->n_boxes) return false;
    if (j < 0 || j >= layout->n_boxes) return false;
    if (i == j) return false;
    if (!srd_sdf_layout_get_adj(layout, i, j)) return false;

    float gap = min_wall_gap(&layout->boxes[i], &layout->boxes[j]);
    return gap > 0.001f && gap <= ALIGN_THRESHOLD;
}

/** Apply: snap the closest pair of facing walls together. */
static int align_wall_apply(srd_sdf_layout_t *layout,
                            const srd_selection_t *sel,
                            const void *userdata,
                            int *new_box_indices, int cap) {
    (void)userdata; (void)new_box_indices; (void)cap;
    if (!layout || !sel || sel->n < 2) return -1;
    int i = sel->indices[0], j = sel->indices[1];
    if (i < 0 || i >= layout->n_boxes) return -1;
    if (j < 0 || j >= layout->n_boxes) return -1;

    srd_sdf_box_t *a = &layout->boxes[i];
    srd_sdf_box_t *b = &layout->boxes[j];

    /* Find the closest wall pair and snap */
    float best_gap = 1e9f;
    int best_axis = 0; /* 0=X, 1=Z */
    int best_dir = 0;  /* 0=a_right-b_left, 1=a_left-b_right, 2=a_top-b_bot, 3=a_bot-b_top */

    float gaps[4];
    gaps[0] = fabsf((a->cx + a->hw) - (b->cx - b->hw));
    gaps[1] = fabsf((a->cx - a->hw) - (b->cx + b->hw));
    gaps[2] = fabsf((a->cz + a->hd) - (b->cz - b->hd));
    gaps[3] = fabsf((a->cz - a->hd) - (b->cz + b->hd));

    for (int k = 0; k < 4; k++) {
        if (gaps[k] < best_gap) {
            best_gap = gaps[k];
            best_dir = k;
            best_axis = (k < 2) ? 0 : 1;
        }
    }

    (void)best_axis;

    /* Snap: move both walls to their midpoint */
    switch (best_dir) {
    case 0: { /* a right wall, b left wall */
        float a_wall = a->cx + a->hw;
        float b_wall = b->cx - b->hw;
        float mid = (a_wall + b_wall) * 0.5f;
        a->cx = mid - a->hw;
        b->cx = mid + b->hw;
        break;
    }
    case 1: { /* a left wall, b right wall */
        float a_wall = a->cx - a->hw;
        float b_wall = b->cx + b->hw;
        float mid = (a_wall + b_wall) * 0.5f;
        a->cx = mid + a->hw;
        b->cx = mid - b->hw;
        break;
    }
    case 2: { /* a top wall, b bottom wall */
        float a_wall = a->cz + a->hd;
        float b_wall = b->cz - b->hd;
        float mid = (a_wall + b_wall) * 0.5f;
        a->cz = mid - a->hd;
        b->cz = mid + b->hd;
        break;
    }
    case 3: { /* a bottom wall, b top wall */
        float a_wall = a->cz - a->hd;
        float b_wall = b->cz + b->hd;
        float mid = (a_wall + b_wall) * 0.5f;
        a->cz = mid + a->hd;
        b->cz = mid - b->hd;
        break;
    }
    }
    return 0;
}

/* ── ClampToBounds (Repair 4) ──────────────────────────────────── */

/** Cond: any edge of the box is outside [0, bounds_w] x [0, bounds_h]. */
static bool clamp_bounds_cond(const srd_sdf_layout_t *layout,
                              const srd_selection_t *sel,
                              const void *userdata) {
    (void)userdata;
    if (!layout || !sel || sel->n < 1) return false;
    int i = sel->indices[0];
    if (i < 0 || i >= layout->n_boxes) return false;

    const srd_sdf_box_t *box = &layout->boxes[i];
    float left   = box->cx - box->hw;
    float right  = box->cx + box->hw;
    float bottom = box->cz - box->hd;
    float top    = box->cz + box->hd;

    if (left < 0.0f || right > layout->bounds_w) return true;
    if (bottom < 0.0f || top > layout->bounds_h) return true;
    /* Also trigger if box is wider/taller than bounds */
    if (box->hw * 2.0f > layout->bounds_w) return true;
    if (box->hd * 2.0f > layout->bounds_h) return true;
    return false;
}

/** Apply: shrink if needed, then clamp centre so all edges are in-bounds. */
static int clamp_bounds_apply(srd_sdf_layout_t *layout,
                              const srd_selection_t *sel,
                              const void *userdata,
                              int *new_box_indices, int cap) {
    (void)userdata; (void)new_box_indices; (void)cap;
    if (!layout || !sel || sel->n < 1) return -1;
    int i = sel->indices[0];
    if (i < 0 || i >= layout->n_boxes) return -1;

    srd_sdf_box_t *box = &layout->boxes[i];

    /* Shrink if wider/taller than bounds */
    if (box->hw * 2.0f > layout->bounds_w) {
        box->hw = layout->bounds_w * 0.5f;
    }
    if (box->hd * 2.0f > layout->bounds_h) {
        box->hd = layout->bounds_h * 0.5f;
    }

    /* Clamp centre so edges stay in bounds */
    if (box->cx - box->hw < 0.0f) box->cx = box->hw;
    if (box->cx + box->hw > layout->bounds_w) box->cx = layout->bounds_w - box->hw;
    if (box->cz - box->hd < 0.0f) box->cz = box->hd;
    if (box->cz + box->hd > layout->bounds_h) box->cz = layout->bounds_h - box->hd;

    return 0;
}

/* ── EnsureConnected (Repair 5) ────────────────────────────────── */

/** Cond: box has zero adjacency (isolated). */
static bool ensure_connected_cond(const srd_sdf_layout_t *layout,
                                  const srd_selection_t *sel,
                                  const void *userdata) {
    (void)userdata;
    if (!layout || !sel || sel->n < 1) return false;
    int i = sel->indices[0];
    if (i < 0 || i >= layout->n_boxes) return false;
    return srd_sdf_layout_adj_count(layout, i) == 0;
}

/**
 * Apply: find nearest other box and insert a corridor connecting them.
 * If only 1 box total, just return success (nothing to connect to).
 */
static int ensure_connected_apply(srd_sdf_layout_t *layout,
                                  const srd_selection_t *sel,
                                  const void *userdata,
                                  int *new_box_indices, int cap) {
    (void)userdata;
    if (!layout || !sel || sel->n < 1) return -1;
    int i = sel->indices[0];
    if (i < 0 || i >= layout->n_boxes) return -1;

    if (layout->n_boxes < 2) return 0; /* nothing to connect to */

    /* Find nearest box by centre distance */
    const srd_sdf_box_t *src = &layout->boxes[i];
    float best_dist = 1e18f;
    int best_idx = -1;
    for (int k = 0; k < layout->n_boxes; k++) {
        if (k == i) continue;
        float dx = layout->boxes[k].cx - src->cx;
        float dz = layout->boxes[k].cz - src->cz;
        float dist = dx * dx + dz * dz;
        if (dist < best_dist) {
            best_dist = dist;
            best_idx = k;
        }
    }

    if (best_idx < 0) return -1;

    /* Insert a corridor at the midpoint between the two boxes */
    const srd_sdf_box_t *dst = &layout->boxes[best_idx];
    srd_sdf_box_t corr;
    memset(&corr, 0, sizeof(corr));
    corr.cx = (src->cx + dst->cx) * 0.5f;
    corr.cz = (src->cz + dst->cz) * 0.5f;
    corr.hw = SRD_EPSILON;
    corr.hd = SRD_EPSILON;
    corr.type = SRD_ROOM_CORRIDOR;
    corr.flags = SRD_BOX_EPSILON;

    int cidx = srd_sdf_layout_add_box(layout, &corr);
    if (cidx < 0) return -1;

    /* Connect corridor to both boxes */
    srd_sdf_layout_set_adj(layout, i, cidx, true);
    srd_sdf_layout_set_adj(layout, best_idx, cidx, true);

    if (new_box_indices && cap >= 1) new_box_indices[0] = cidx;
    return 1;
}

/* ── Registration ──────────────────────────────────────────────── */

int srd_rules_repair_register(srd_rule_table_t *tbl) {
    if (!tbl) return -1;

    /* ResolveOverlap */
    srd_descent_rule_t r1 = {0};
    r1.name = "ResolveOverlap";
    r1.inverse_rule_id = -1;
    r1.n_select = 2;
    r1.is_repair = true;
    r1.cond = resolve_overlap_cond;
    r1.apply = resolve_overlap_apply;
    if (srd_rule_table_register(tbl, &r1) < 0) return -1;

    /* RepairContained */
    srd_descent_rule_t r2 = {0};
    r2.name = "RepairContained";
    r2.inverse_rule_id = -1;
    r2.n_select = 2;
    r2.is_repair = true;
    r2.cond = repair_contained_cond;
    r2.apply = repair_contained_apply;
    if (srd_rule_table_register(tbl, &r2) < 0) return -1;

    /* AlignWall */
    srd_descent_rule_t r3 = {0};
    r3.name = "AlignWall";
    r3.inverse_rule_id = -1;
    r3.n_select = 2;
    r3.is_repair = true;
    r3.cond = align_wall_cond;
    r3.apply = align_wall_apply;
    if (srd_rule_table_register(tbl, &r3) < 0) return -1;

    /* ClampToBounds */
    srd_descent_rule_t r4 = {0};
    r4.name = "ClampToBounds";
    r4.inverse_rule_id = -1;
    r4.n_select = 1;
    r4.is_repair = true;
    r4.cond = clamp_bounds_cond;
    r4.apply = clamp_bounds_apply;
    if (srd_rule_table_register(tbl, &r4) < 0) return -1;

    /* EnsureConnected */
    srd_descent_rule_t r5 = {0};
    r5.name = "EnsureConnected";
    r5.inverse_rule_id = -1;
    r5.n_select = 1;
    r5.is_repair = true;
    r5.cond = ensure_connected_cond;
    r5.apply = ensure_connected_apply;
    if (srd_rule_table_register(tbl, &r5) < 0) return -1;

    return 5;
}
