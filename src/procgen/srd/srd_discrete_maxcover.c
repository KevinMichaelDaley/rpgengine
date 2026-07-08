/**
 * @file srd_discrete_maxcover.c
 * @brief Compatibility graph and greedy max-cover for SRD candidates.
 *
 * Non-static functions (2): srd_build_compatibility, srd_greedy_max_cover
 */
#include "ferrum/procgen/srd/srd_discrete_maxcover.h"

#include <math.h>
#include <string.h>

/* ── Affected-box set ────────────────────────────────────────── */

/**
 * @brief Compute the centre of a candidate's selection.
 *
 * Averages the centres of all selected boxes.
 */
static void selection_centre(const srd_sdf_layout_t *layout,
                             const srd_selection_t *sel,
                             float *out_cx, float *out_cz) {
    float sx = 0.0f, sz = 0.0f;
    int count = 0;
    for (int i = 0; i < sel->n; i++) {
        int idx = sel->indices[i];
        if (idx >= 0 && idx < layout->n_boxes) {
            sx += layout->boxes[idx].cx;
            sz += layout->boxes[idx].cz;
            count++;
        }
    }
    if (count > 0) {
        *out_cx = sx / (float)count;
        *out_cz = sz / (float)count;
    } else {
        *out_cx = 0.0f;
        *out_cz = 0.0f;
    }
}

/**
 * @brief Check if a box is within locality_radius of a candidate's selection.
 */
static bool box_in_locality(const srd_sdf_layout_t *layout,
                            int box_idx,
                            float sel_cx, float sel_cz,
                            float radius) {
    if (box_idx < 0 || box_idx >= layout->n_boxes) return false;
    float dx = layout->boxes[box_idx].cx - sel_cx;
    float dz = layout->boxes[box_idx].cz - sel_cz;
    return (dx * dx + dz * dz) <= (radius * radius);
}

/* ── Public API ──────────────────────────────────────────────── */

void srd_build_compatibility(const srd_rule_table_t *tbl,
                             const srd_candidate_t *cands,
                             int n,
                             const srd_sdf_layout_t *layout,
                             uint8_t *compat,
                             int stride) {
    if (!tbl || !cands || !layout || !compat || n <= 0) return;

    /* Default all to compatible */
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            compat[i * stride + j] = (i != j) ? 1 : 0;
        }
    }

    /* For each pair, check if any box falls in both localities */
    for (int i = 0; i < n; i++) {
        float ci_cx, ci_cz;
        selection_centre(layout, &cands[i].sel, &ci_cx, &ci_cz);
        float ri = 0.0f;
        if (cands[i].rule_idx >= 0 && cands[i].rule_idx < tbl->n_rules)
            ri = tbl->rules[cands[i].rule_idx].locality_radius;
        /* If no locality_radius set, use the selection boxes directly */
        if (ri <= 0.0f) ri = 10.0f; /* default radius */

        for (int j = i + 1; j < n; j++) {
            float cj_cx, cj_cz;
            selection_centre(layout, &cands[j].sel, &cj_cx, &cj_cz);
            float rj = 0.0f;
            if (cands[j].rule_idx >= 0 && cands[j].rule_idx < tbl->n_rules)
                rj = tbl->rules[cands[j].rule_idx].locality_radius;
            if (rj <= 0.0f) rj = 10.0f;

            /* Check if any box is in both localities */
            bool conflict = false;
            for (int k = 0; k < layout->n_boxes && !conflict; k++) {
                if (box_in_locality(layout, k, ci_cx, ci_cz, ri) &&
                    box_in_locality(layout, k, cj_cx, cj_cz, rj)) {
                    conflict = true;
                }
            }

            /* Also: if they share any selected box index */
            if (!conflict) {
                for (int a = 0; a < cands[i].sel.n && !conflict; a++) {
                    for (int b = 0; b < cands[j].sel.n && !conflict; b++) {
                        if (cands[i].sel.indices[a] == cands[j].sel.indices[b])
                            conflict = true;
                    }
                }
            }

            if (conflict) {
                compat[i * stride + j] = 0;
                compat[j * stride + i] = 0;
            }
        }
    }
}

int srd_greedy_max_cover(const srd_candidate_t *cands,
                         int n,
                         const uint8_t *compat,
                         int stride,
                         int *out,
                         int max_out) {
    if (!cands || !compat || !out || n <= 0 || max_out <= 0) return 0;

    /* Build sorted index array (by delta_L descending).
     * Use simple insertion sort — N <= 512. */
    int sorted[SRD_K_MAX];
    int n_positive = 0;
    for (int i = 0; i < n && i < SRD_K_MAX; i++) {
        if (cands[i].delta_L > 0.0f) {
            sorted[n_positive++] = i;
        }
    }

    /* Insertion sort by delta_L descending */
    for (int i = 1; i < n_positive; i++) {
        int key = sorted[i];
        float key_val = cands[key].delta_L;
        int j = i - 1;
        while (j >= 0 && cands[sorted[j]].delta_L < key_val) {
            sorted[j + 1] = sorted[j];
            j--;
        }
        sorted[j + 1] = key;
    }

    /* Greedy selection */
    int n_selected = 0;
    for (int i = 0; i < n_positive && n_selected < max_out; i++) {
        int idx = sorted[i];

        /* Check compatibility with all already-selected */
        bool compatible = true;
        for (int s = 0; s < n_selected; s++) {
            if (!compat[idx * stride + out[s]]) {
                compatible = false;
                break;
            }
        }

        if (compatible) {
            out[n_selected++] = idx;
        }
    }

    return n_selected;
}
