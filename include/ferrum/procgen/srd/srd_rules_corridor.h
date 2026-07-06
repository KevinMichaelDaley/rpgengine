/**
 * @file srd_rules_corridor.h
 * @brief Registration functions for corridor rules (Rules 17-30).
 *
 * Corridor rules handle adding/removing corridors, width changes,
 * bending/straightening, splitting/merging, graph connectivity
 * (BridgeComponents, loops, shortcuts), and rerouting.
 */
#ifndef FERRUM_PROCGEN_SRD_RULES_CORRIDOR_H
#define FERRUM_PROCGEN_SRD_RULES_CORRIDOR_H

#include "ferrum/procgen/srd/srd_descent_rules.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Register basic corridor rules (Rules 17-20).
 *
 * AddCorridor, RemoveCorridor, WidenCorridor, NarrowCorridor.
 *
 * @param tbl  Rule table to register into.
 * @return Number of rules registered (4), or -1 on error.
 */
int srd_rules_corridor_register_basic(srd_rule_table_t *tbl);

/**
 * @brief Register corridor shape rules (Rules 21-24).
 *
 * BendCorridor, StraightenCorridor, SplitCorridor, MergeCorridor.
 *
 * @param tbl  Rule table to register into.
 * @return Number of rules registered (4), or -1 on error.
 */
int srd_rules_corridor_register_shape(srd_rule_table_t *tbl);

/**
 * @brief Register graph connectivity rules (Rules 25-30).
 *
 * BridgeComponents, AddLoop, RemoveLoop, ShortcutPath,
 * RemoveShortcut, RerouteCorridor.
 *
 * @param tbl  Rule table to register into.
 * @return Number of rules registered (6), or -1 on error.
 */
int srd_rules_corridor_register_graph(srd_rule_table_t *tbl);

#ifdef __cplusplus
}
#endif
#endif /* FERRUM_PROCGEN_SRD_RULES_CORRIDOR_H */
