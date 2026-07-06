/**
 * @file srd_rules_room.h
 * @brief Registration functions for room topology rules (Rules 1-16).
 *
 * Room topology rules handle splitting, merging, adding, removing,
 * trimming, extending, scaling rooms, plus alcoves, antechambers,
 * and type conversion. All Add* rules spawn at SRD_EPSILON for
 * jump continuity.
 *
 * Split into four registration groups to respect the 4-function rule:
 *   - split:  SplitRoomH, SplitRoomV, MergeRooms (Rules 1-3)
 *   - add:    AddRoomN/S/E/W, RemoveRoom (Rules 4-8)
 *   - modify: TrimRoom, ExtendRoom, ScaleRoom (Rules 9-11)
 *   - annex:  AddAlcove, RemoveAlcove, AddAntechamber,
 *             RemoveAntechamber, ConvertType (Rules 12-16)
 */
#ifndef FERRUM_PROCGEN_SRD_RULES_ROOM_H
#define FERRUM_PROCGEN_SRD_RULES_ROOM_H

#include "ferrum/procgen/srd/srd_descent_rules.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Register split/merge rules (Rules 1-3).
 *
 * Registers SplitRoomH, MergeRooms, SplitRoomV. Inverse IDs are
 * set up so SplitH/V point to Merge and Merge points to SplitH.
 *
 * @param tbl  Rule table to register into.
 * @return Number of rules registered (3), or -1 on error.
 */
int srd_rules_room_register_split(srd_rule_table_t *tbl);

/**
 * @brief Register add/remove room rules (Rules 4-8).
 *
 * Registers AddRoomN, AddRoomS, AddRoomE, AddRoomW, RemoveRoom.
 * RemoveRoom is registered first so Add* rules can reference it
 * as their inverse.
 *
 * @param tbl  Rule table to register into.
 * @return Number of rules registered (5), or -1 on error.
 */
int srd_rules_room_register_add(srd_rule_table_t *tbl);

/**
 * @brief Register modify rules (Rules 9-11).
 *
 * Registers TrimRoom, ExtendRoom, ScaleRoom.
 * Trim and Extend are mutual inverses; Scale is self-inverse.
 *
 * @param tbl  Rule table to register into.
 * @return Number of rules registered (3), or -1 on error.
 */
int srd_rules_room_register_modify(srd_rule_table_t *tbl);

/**
 * @brief Register alcove, antechamber, and type conversion rules (Rules 12-16).
 *
 * Registers AddAlcove, RemoveAlcove, AddAntechamber,
 * RemoveAntechamber, ConvertType.
 *
 * @param tbl  Rule table to register into.
 * @return Number of rules registered (5), or -1 on error.
 */
int srd_rules_room_register_annex(srd_rule_table_t *tbl);

#ifdef __cplusplus
}
#endif
#endif /* FERRUM_PROCGEN_SRD_RULES_ROOM_H */
