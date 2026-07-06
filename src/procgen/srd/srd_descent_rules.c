/**
 * @file srd_descent_rules.c
 * @brief Rule table management: init, register, find_applicable, sample_selection.
 *
 * Non-static functions (4): srd_rule_table_init, srd_rule_table_register,
 *                            srd_rule_find_applicable, srd_rule_sample_selection
 */
#include "ferrum/procgen/srd/srd_descent_rules.h"

#include <assert.h>
#include <string.h>

/* ── xorshift32 RNG helper ──────────────────────────────────────── */

static uint32_t xorshift32(uint32_t *state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

/* ── Table lifecycle ────────────────────────────────────────────── */

void srd_rule_table_init(srd_rule_table_t *tbl) {
    if (!tbl) return;
    memset(tbl, 0, sizeof(*tbl));
}

int srd_rule_table_register(srd_rule_table_t *tbl,
                            const srd_descent_rule_t *rule) {
    if (!tbl || !rule) return -1;
    if (tbl->n_rules >= SRD_MAX_RULES_TABLE) return -1;

#ifndef NDEBUG
    /* Assert inverse rule is already registered (if specified) */
    if (rule->inverse_rule_id >= 0) {
        assert(rule->inverse_rule_id < tbl->n_rules &&
               "inverse_rule_id must refer to an already-registered rule");
    }
#endif

    int idx = tbl->n_rules;
    tbl->rules[idx] = *rule;
    tbl->n_rules++;
    return idx;
}

/* ── Applicable rules ───────────────────────────────────────────── */

int srd_rule_find_applicable(const srd_rule_table_t *tbl,
                             const srd_sdf_layout_t *layout,
                             int *out_rule_indices, int max_out,
                             uint32_t *rng_state) {
    if (!tbl || !layout || !out_rule_indices || !rng_state) return 0;

    int found = 0;
    for (int ri = 0; ri < tbl->n_rules && found < max_out; ri++) {
        const srd_descent_rule_t *rule = &tbl->rules[ri];
        /* Skip repair rules — they're applied unconditionally */
        if (rule->is_repair) continue;
        if (!rule->cond) continue;

        /* Try to find at least one valid selection */
        srd_selection_t sel;
        if (srd_rule_sample_selection(tbl, ri, layout, &sel, rng_state)) {
            out_rule_indices[found++] = ri;
        }
    }
    return found;
}

/* ── Selection sampling ─────────────────────────────────────────── */

bool srd_rule_sample_selection(const srd_rule_table_t *tbl,
                               int rule_idx,
                               const srd_sdf_layout_t *layout,
                               srd_selection_t *sel_out,
                               uint32_t *rng_state) {
    if (!tbl || !layout || !sel_out || !rng_state) return false;
    if (rule_idx < 0 || rule_idx >= tbl->n_rules) return false;

    const srd_descent_rule_t *rule = &tbl->rules[rule_idx];
    if (!rule->cond) return false;
    if (layout->n_boxes == 0) return false;

    int n_sel = rule->n_select;
    if (n_sel <= 0) n_sel = 0;
    if (n_sel > SRD_MAX_SELECT) n_sel = SRD_MAX_SELECT;

    /* Try up to 32 random selections */
    for (int attempt = 0; attempt < 32; attempt++) {
        srd_selection_t sel;
        memset(&sel, 0, sizeof(sel));
        sel.n = n_sel;

        /* Pick n_sel distinct random box indices */
        for (int s = 0; s < n_sel; s++) {
            int tries = 0;
            bool unique;
            do {
                sel.indices[s] = (int)(xorshift32(rng_state) %
                                       (uint32_t)layout->n_boxes);
                unique = true;
                for (int p = 0; p < s; p++) {
                    if (sel.indices[p] == sel.indices[s]) {
                        unique = false;
                        break;
                    }
                }
                tries++;
            } while (!unique && tries < 64);
        }

        if (rule->cond(layout, &sel, rule->userdata)) {
            *sel_out = sel;
            return true;
        }
    }
    return false;
}
