/**
 * @file srd_rules_feature.h
 * @brief Registration functions for feature rules (Rules 31-46).
 *
 * Feature rules cover doors, stairs, and special rooms. All Add* rules
 * spawn at SRD_EPSILON for jump continuity.
 *
 * Split into four registration groups to respect the 4-function rule:
 *   - door:    AddDoor, RemoveDoor, WidenDoor, NarrowDoor (Rules 31-34)
 *   - stair:   AddStairUp, AddStairDown, RemoveStair, RelocateStair (Rules 35-38)
 *   - special: AddDeadEnd, RemoveDeadEnd, AddSecretRoom, RemoveSecretRoom (Rules 39-42)
 *   - boss:    AddBossRoom, RemoveBossRoom, AddTreasureRoom, RemoveTreasureRoom (Rules 43-46)
 */
#ifndef FERRUM_PROCGEN_SRD_RULES_FEATURE_H
#define FERRUM_PROCGEN_SRD_RULES_FEATURE_H

#include "ferrum/procgen/srd/srd_descent_rules.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Register door rules (Rules 31-34).
 *
 * Registers RemoveDoor, AddDoor, NarrowDoor, WidenDoor.
 * AddDoor/RemoveDoor are mutual inverses; WidenDoor/NarrowDoor are
 * mutual inverses.
 *
 * Door wall sides: N=0, S=1, E=2, W=3. When userdata is NULL,
 * the default side is N (0).
 *
 * @param tbl  Rule table to register into.
 * @return Number of rules registered (4), or -1 on error.
 */
int srd_rules_feature_register_door(srd_rule_table_t *tbl);

/**
 * @brief Register stair rules (Rules 35-38).
 *
 * Registers RemoveStair, AddStairUp, AddStairDown, RelocateStair.
 * AddStairUp and AddStairDown inverse to RemoveStair.
 * RelocateStair is self-inverse.
 *
 * @param tbl  Rule table to register into.
 * @return Number of rules registered (4), or -1 on error.
 */
int srd_rules_feature_register_stair(srd_rule_table_t *tbl);

/**
 * @brief Register special room rules (Rules 39-42).
 *
 * Registers RemoveDeadEnd, AddDeadEnd, RemoveSecretRoom, AddSecretRoom.
 * Add/Remove pairs are mutual inverses.
 *
 * @param tbl  Rule table to register into.
 * @return Number of rules registered (4), or -1 on error.
 */
int srd_rules_feature_register_special(srd_rule_table_t *tbl);

/**
 * @brief Register boss and treasure room rules (Rules 43-46).
 *
 * Registers RemoveBossRoom, AddBossRoom, RemoveTreasureRoom,
 * AddTreasureRoom. Add/Remove pairs are mutual inverses.
 * AddBossRoom has a uniqueness constraint: no existing BOSS box
 * in layout.
 *
 * @param tbl  Rule table to register into.
 * @return Number of rules registered (4), or -1 on error.
 */
int srd_rules_feature_register_boss(srd_rule_table_t *tbl);

#ifdef __cplusplus
}
#endif
#endif /* FERRUM_PROCGEN_SRD_RULES_FEATURE_H */
