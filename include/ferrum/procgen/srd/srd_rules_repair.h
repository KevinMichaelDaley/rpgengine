/**
 * @file srd_rules_repair.h
 * @brief Repair rules for SRD: project layouts back to feasibility.
 *
 * Repair rules (5 total) are not in the candidate set for stochastic
 * descent — they are applied deterministically after each rewrite step
 * to fix constraint violations (overlaps, containment, misalignment,
 * bounds violations, disconnected components).
 *
 * All repair rules have is_repair=true and inverse_rule_id=-1.
 *
 * Non-static functions (1): srd_rules_repair_register
 */
#ifndef FERRUM_PROCGEN_SRD_RULES_REPAIR_H
#define FERRUM_PROCGEN_SRD_RULES_REPAIR_H

#include "ferrum/procgen/srd/srd_descent_rules.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Register all 5 repair rules into the table.
 *
 * Rules registered:
 *   1. ResolveOverlap  (n_select=2): separate overlapping boxes
 *   2. RepairContained  (n_select=2): remove or extend contained box
 *   3. AlignWall        (n_select=2): snap close-but-not-flush walls
 *   4. ClampToBounds    (n_select=1): clamp box to layout bounds
 *   5. EnsureConnected  (n_select=1): add corridor to isolated box
 *
 * @param tbl  Rule table to populate. Must not be NULL.
 * @return 5 on success, -1 on failure.
 */
int srd_rules_repair_register(srd_rule_table_t *tbl);

#ifdef __cplusplus
}
#endif
#endif /* FERRUM_PROCGEN_SRD_RULES_REPAIR_H */
