/**
 * @file srd_descent_rules_builtins.c
 * @brief Built-in rule registration for SRD descent rules.
 *
 * Non-static functions (1): srd_rule_table_register_builtins
 *
 * TODO: Populated by srd-rules-02 through srd-rules-05 tickets.
 */
#include "ferrum/procgen/srd/srd_descent_rules.h"

void srd_rule_table_register_builtins(srd_rule_table_t *tbl) {
    if (!tbl) return;
    /* Rules will be registered here by subsequent tickets:
     * - srd-rules-02: Rules 1-16 (room topology)
     * - srd-rules-03: Rules 17-30 (corridors)
     * - srd-rules-04: Rules 31-46 (features)
     * - srd-rules-05: Repair rules 1-5
     */
}
