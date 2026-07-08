/**
 * @file srd_discrete_repair.h
 * @brief Post-rewrite repair rule application for SRD layouts.
 *
 * Applies repair rules in a fixed order (ClampToBounds, ResolveOverlap,
 * RepairContained, AlignWall, EnsureConnected) over multiple passes
 * until no rules fire or max passes reached.
 *
 * Non-static functions declared (1): srd_apply_repairs
 */
#ifndef FERRUM_PROCGEN_SRD_DISCRETE_REPAIR_H
#define FERRUM_PROCGEN_SRD_DISCRETE_REPAIR_H

#include "ferrum/procgen/srd/srd_descent_rules.h"
#include "ferrum/procgen/srd/srd_sdf_layout.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Apply all repair rules to a layout until convergence.
 *
 * Scans the rule table for repair rules (is_repair == true) and applies
 * them in registration order. Runs up to 4 passes; stops early if a
 * pass fires zero rules.
 *
 * Fixed application order per pass:
 *   1. ClampToBounds  (single-box, n_select=1)
 *   2. ResolveOverlap (pair-wise, n_select=2)
 *   3. RepairContained (pair-wise, n_select=2)
 *   4. AlignWall       (pair-wise, n_select=2)
 *   5. EnsureConnected (single-box, n_select=1)
 *
 * @param layout  Layout to repair in-place. Must not be NULL.
 * @param tbl     Rule table containing registered repair rules.
 * @return Total number of repairs fired across all passes, or -1 on error.
 *
 * @note Ownership: layout is modified in-place; caller owns it.
 * @note Side effects: may add, remove, or reposition boxes.
 */
int srd_apply_repairs(srd_sdf_layout_t *layout, const srd_rule_table_t *tbl);

#ifdef __cplusplus
}
#endif
#endif /* FERRUM_PROCGEN_SRD_DISCRETE_REPAIR_H */
