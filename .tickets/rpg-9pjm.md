---
id: rpg-9pjm
status: closed
deps: [rpg-02fm]
links: []
created: 2026-07-05T22:54:03Z
type: epic
priority: 1
assignee: KMD
tags: [srd, rules]
---
# SRD-E2: Descent Rule Table and Built-in Rules

The custom function-pointer rule table and all 46 built-in dungeon layout rules. Rules must satisfy REVERSIBILITY (every non-repair rule has a registered inverse), JUMP CONTINUITY (Add* rules spawn at SRD_EPSILON), LOCAL GEOMETRIC CONTROL (BridgeComponents/AddDeadEnd/AddBossRoom etc. can fire on any box), and REPAIRABILITY (5 unconditional repair rules). Custom rules can be registered via srd_rule_table_register. See ref/srd_redesign_plan.md §Full Rule Set.

## Design

srd_descent_rule_t: {name, inverse_rule_id, n_select, locality_radius, is_repair, jump_continuous, cond fn, apply fn, userdata}. Table is a growable array. register_builtins wires all 46 rules with correct inverse IDs. Rules split across 4 source files to respect 4-function limit: srd_rules_room.c (1-16), srd_rules_corridor.c (17-30), srd_rules_feature.c (31-46), srd_rules_repair.c (repair 1-5).

## Acceptance Criteria

All 46 rules registered by register_builtins; every non-repair rule has a valid inverse_rule_id; round-trip test: apply rule then inverse restores layout to within SRD_EPSILON; jump-continuity test: Apply on any Add* rule changes rasteriser output by < 0.01 L2; repair rules excluded from srd_rule_find_applicable; custom rule registered by caller appears in applicable list

