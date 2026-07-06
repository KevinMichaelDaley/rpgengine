/**
 * @file srd_rules_room_modify.c
 * @brief Room modification rules (Rules 9-11): TrimRoom, ExtendRoom, ScaleRoom.
 *
 * Non-static functions (1): srd_rules_room_register_modify
 */
#include "ferrum/procgen/srd/srd_rules_room.h"

#include <string.h>

/* ── Default trim/extend amount ────────────────────────────────────── */

/** Default amount for trim/extend when not specified via userdata. */
#define DEFAULT_TRIM_AMT 0.5f

/* ── TrimRoom (Rule 9) ─────────────────────────────────────────────── */

/**
 * Condition: box exists and is large enough that trimming by DEFAULT_TRIM_AMT
 * on any side leaves hw > SRD_EPSILON and hd > SRD_EPSILON.
 */
static bool trim_cond(const srd_sdf_layout_t *layout,
                      const srd_selection_t *sel,
                      const void *userdata) {
    (void)userdata;
    if (!layout || !sel || sel->n < 1) return false;
    int i = sel->indices[0];
    if (i < 0 || i >= layout->n_boxes) return false;

    const srd_sdf_box_t *box = &layout->boxes[i];
    float amt = DEFAULT_TRIM_AMT;
    /* Must be large enough to trim by amt/2 and still exceed EPSILON */
    return (box->hw - amt * 0.5f > SRD_EPSILON) &&
           (box->hd - amt * 0.5f > SRD_EPSILON);
}

/**
 * Apply: shrink box on a deterministic side (east by default).
 * side=E: hw -= amt/2; cx -= amt/2.
 */
static int trim_apply(srd_sdf_layout_t *layout,
                      const srd_selection_t *sel,
                      const void *userdata,
                      int *new_box_indices, int cap) {
    (void)userdata;
    (void)new_box_indices;
    (void)cap;
    if (!layout || !sel || sel->n < 1) return -1;
    int i = sel->indices[0];
    if (i < 0 || i >= layout->n_boxes) return -1;

    float amt = DEFAULT_TRIM_AMT;
    /* Trim east side: reduce hw, shift cx left */
    layout->boxes[i].hw -= amt * 0.5f;
    layout->boxes[i].cx -= amt * 0.5f;
    return 0;
}

/* ── ExtendRoom (Rule 10) ──────────────────────────────────────────── */

/**
 * Condition: box exists. Always valid — extending makes the room bigger.
 */
static bool extend_cond(const srd_sdf_layout_t *layout,
                        const srd_selection_t *sel,
                        const void *userdata) {
    (void)userdata;
    if (!layout || !sel || sel->n < 1) return false;
    int i = sel->indices[0];
    return (i >= 0 && i < layout->n_boxes);
}

/**
 * Apply: grow box on east side by DEFAULT_TRIM_AMT.
 * side=E: hw += amt/2; cx += amt/2.
 */
static int extend_apply(srd_sdf_layout_t *layout,
                        const srd_selection_t *sel,
                        const void *userdata,
                        int *new_box_indices, int cap) {
    (void)userdata;
    (void)new_box_indices;
    (void)cap;
    if (!layout || !sel || sel->n < 1) return -1;
    int i = sel->indices[0];
    if (i < 0 || i >= layout->n_boxes) return -1;

    float amt = DEFAULT_TRIM_AMT;
    layout->boxes[i].hw += amt * 0.5f;
    layout->boxes[i].cx += amt * 0.5f;
    return 0;
}

/* ── ScaleRoom (Rule 11) ──────────────────────────────────────────── */

/** Default scale factors. */
#define DEFAULT_SCALE_X 1.2f
#define DEFAULT_SCALE_Z 1.2f

/**
 * Condition: box exists and scaled result would still exceed EPSILON.
 */
static bool scale_cond(const srd_sdf_layout_t *layout,
                       const srd_selection_t *sel,
                       const void *userdata) {
    (void)userdata;
    if (!layout || !sel || sel->n < 1) return false;
    int i = sel->indices[0];
    if (i < 0 || i >= layout->n_boxes) return false;

    const srd_sdf_box_t *box = &layout->boxes[i];
    return (box->hw * DEFAULT_SCALE_X > SRD_EPSILON) &&
           (box->hd * DEFAULT_SCALE_Z > SRD_EPSILON);
}

/**
 * Apply: scale hw and hd by default factors. Centre unchanged.
 */
static int scale_apply(srd_sdf_layout_t *layout,
                       const srd_selection_t *sel,
                       const void *userdata,
                       int *new_box_indices, int cap) {
    (void)userdata;
    (void)new_box_indices;
    (void)cap;
    if (!layout || !sel || sel->n < 1) return -1;
    int i = sel->indices[0];
    if (i < 0 || i >= layout->n_boxes) return -1;

    layout->boxes[i].hw *= DEFAULT_SCALE_X;
    layout->boxes[i].hd *= DEFAULT_SCALE_Z;
    return 0;
}

/* ── Registration ──────────────────────────────────────────────────── */

int srd_rules_room_register_modify(srd_rule_table_t *tbl) {
    if (!tbl) return -1;

    /* Register TrimRoom first with -1 inverse (patched below) */
    srd_descent_rule_t trim = {0};
    trim.name = "TrimRoom";
    trim.inverse_rule_id = -1;
    trim.n_select = 1;
    trim.jump_continuous = true;
    trim.cond = trim_cond;
    trim.apply = trim_apply;
    int trim_idx = srd_rule_table_register(tbl, &trim);
    if (trim_idx < 0) return -1;

    /* ExtendRoom — inverse is TrimRoom */
    srd_descent_rule_t extend = {0};
    extend.name = "ExtendRoom";
    extend.inverse_rule_id = trim_idx;
    extend.n_select = 1;
    extend.jump_continuous = true;
    extend.cond = extend_cond;
    extend.apply = extend_apply;
    int extend_idx = srd_rule_table_register(tbl, &extend);
    if (extend_idx < 0) return -1;

    /* Patch TrimRoom inverse to point to ExtendRoom */
    tbl->rules[trim_idx].inverse_rule_id = extend_idx;

    /* ScaleRoom — self-inverse */
    srd_descent_rule_t scale = {0};
    scale.name = "ScaleRoom";
    scale.inverse_rule_id = -1;  /* Self-inverse conceptually */
    scale.n_select = 1;
    scale.jump_continuous = true;
    scale.cond = scale_cond;
    scale.apply = scale_apply;
    if (srd_rule_table_register(tbl, &scale) < 0) return -1;

    /* Patch ScaleRoom inverse to itself */
    tbl->rules[tbl->n_rules - 1].inverse_rule_id = tbl->n_rules - 1;

    return 3;
}
