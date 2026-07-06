/**
 * @file srd_sdf_layout_sdf.c
 * @brief SDF evaluation, union, rasteriser, overlap, containment, adj helpers.
 *
 * Non-static functions (4): srd_sdf_box_eval, srd_sdf_layout_union,
 *                            srd_sdf_box_overlap, srd_sdf_box_contains
 */
#include "ferrum/procgen/srd/srd_sdf_layout.h"

#include <math.h>

static float maxf(float a, float b) { return a > b ? a : b; }

float srd_sdf_box_eval(const srd_sdf_box_t *box, float qx, float qz) {
    if (!box) return 1e12f;
    float dx = fabsf(qx - box->cx) - box->hw;
    float dz = fabsf(qz - box->cz) - box->hd;
    return maxf(dx, dz);
}

float srd_sdf_layout_union(const srd_sdf_layout_t *layout,
                           float qx, float qz, float temperature) {
    if (!layout || layout->n_boxes == 0) return 1e12f;
    if (temperature < 1e-8f) temperature = 1e-8f;

    float inv_t = 1.0f / temperature;

    /* LogSumExp: -T * log( sum_i exp(-sdf_i / T) ) */
    /* For numerical stability: shift by max(-sdf_i/T) */
    float max_val = -1e12f;
    for (int i = 0; i < layout->n_boxes; i++) {
        float s = -srd_sdf_box_eval(&layout->boxes[i], qx, qz) * inv_t;
        if (s > max_val) max_val = s;
    }

    float sum_exp = 0.0f;
    for (int i = 0; i < layout->n_boxes; i++) {
        float s = -srd_sdf_box_eval(&layout->boxes[i], qx, qz) * inv_t;
        sum_exp += expf(s - max_val);
    }

    return -temperature * (logf(sum_exp) + max_val);
}

bool srd_sdf_box_overlap(const srd_sdf_box_t *a, const srd_sdf_box_t *b) {
    if (!a || !b) return false;
    float dx = fabsf(a->cx - b->cx);
    float dz = fabsf(a->cz - b->cz);
    return (dx < a->hw + b->hw) && (dz < a->hd + b->hd);
}

bool srd_sdf_box_contains(const srd_sdf_box_t *outer,
                          const srd_sdf_box_t *inner) {
    if (!outer || !inner) return false;
    return (inner->cx - inner->hw >= outer->cx - outer->hw) &&
           (inner->cx + inner->hw <= outer->cx + outer->hw) &&
           (inner->cz - inner->hd >= outer->cz - outer->hd) &&
           (inner->cz + inner->hd <= outer->cz + outer->hd);
}
