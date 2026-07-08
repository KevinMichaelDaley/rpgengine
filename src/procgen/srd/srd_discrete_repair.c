/**
 * @file srd_discrete_repair.c
 * @brief Post-rewrite repair rule application for SRD layouts.
 *
 * Iterates repair rules over all applicable box selections in a fixed
 * order, repeating passes until no rules fire or max passes reached.
 *
 * Non-static functions (1): srd_apply_repairs
 */
#include "ferrum/procgen/srd/srd_discrete_repair.h"

#include <string.h>

/** Maximum repair passes before giving up. */
#define MAX_REPAIR_PASSES 4

/* ── Helpers ───────────────────────────────────────────────────── */

/**
 * @brief Try to apply a single-box repair rule to every box in the layout.
 *
 * @return Number of times the rule fired.
 */
static int apply_single_box_rule(srd_sdf_layout_t *layout,
                                 const srd_descent_rule_t *rule) {
    int fired = 0;
    for (int i = 0; i < layout->n_boxes; i++) {
        srd_selection_t sel;
        sel.n = 1;
        sel.indices[0] = i;

        if (rule->cond(layout, &sel, rule->userdata)) {
            int new_indices[SRD_MAX_SELECT];
            int result = rule->apply(layout, &sel, rule->userdata,
                                     new_indices, SRD_MAX_SELECT);
            if (result >= 0) {
                fired++;
                /* Box may have been removed or added; restart scan */
                i = -1;
            }
        }
    }
    return fired;
}

/**
 * @brief Try to apply a pair-wise repair rule to every box pair.
 *
 * @return Number of times the rule fired.
 */
static int apply_pair_rule(srd_sdf_layout_t *layout,
                           const srd_descent_rule_t *rule) {
    int fired = 0;
    bool any_fired;
    do {
        any_fired = false;
        for (int i = 0; i < layout->n_boxes && !any_fired; i++) {
            for (int j = i + 1; j < layout->n_boxes && !any_fired; j++) {
                srd_selection_t sel;
                sel.n = 2;
                sel.indices[0] = i;
                sel.indices[1] = j;

                if (rule->cond(layout, &sel, rule->userdata)) {
                    int new_indices[SRD_MAX_SELECT];
                    int result = rule->apply(layout, &sel, rule->userdata,
                                             new_indices, SRD_MAX_SELECT);
                    if (result >= 0) {
                        fired++;
                        any_fired = true; /* restart scan */
                    }
                }
            }
        }
    } while (any_fired);
    return fired;
}

/* ── Public API ────────────────────────────────────────────────── */

int srd_apply_repairs(srd_sdf_layout_t *layout, const srd_rule_table_t *tbl) {
    if (!layout || !tbl) return -1;

    /* Collect repair rule indices in registration order */
    int repair_indices[SRD_MAX_RULES_TABLE];
    int n_repairs = 0;
    for (int i = 0; i < tbl->n_rules; i++) {
        if (tbl->rules[i].is_repair) {
            repair_indices[n_repairs++] = i;
        }
    }

    int total_fired = 0;

    for (int pass = 0; pass < MAX_REPAIR_PASSES; pass++) {
        int pass_fired = 0;

        for (int r = 0; r < n_repairs; r++) {
            int idx = repair_indices[r];
            const srd_descent_rule_t *rule = &tbl->rules[idx];

            if (rule->n_select == 1) {
                pass_fired += apply_single_box_rule(layout, rule);
            } else if (rule->n_select == 2) {
                pass_fired += apply_pair_rule(layout, rule);
            }
        }

        total_fired += pass_fired;
        if (pass_fired == 0) break; /* converged */
    }

    return total_fired;
}
