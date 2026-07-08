---
id: rpg-igs5
status: closed
deps: [rpg-nwqk, rpg-j4q1, rpg-as0e]
links: []
created: 2026-07-05T22:56:50Z
type: task
priority: 1
assignee: KMD
parent: rpg-3sff
tags: [srd, integration]
---
# srd-loop-02: srd_bridge.cpp — ASCII to SDF pipeline rewire

Rewrite srd_bridge.cpp srd_generate to use the new pipeline. Keep the existing ASCII line parser and srd_grid_parse call. Replace everything after grid parsing: call srd_sdf_layout_from_grid, set up rule table (srd_rule_table_register_builtins), create AnalyticalCritic from layout dimensions, build srd_descent_config_t from budget, call srd_descent_optimize, convert final layout to srd_tile_list_t.

## Design

srd_layout_to_tiles(layout, tiles, floor_h, ceil_h): for each box in layout, add floor tile at box bounds, ceiling tile, and wall tiles on each side where no adjacent box shares that wall. This replaces srd_grammar_collect_tiles. Remove all SymX includes. Remove LOSS: block parsing (critic handles constraints internally).

## Acceptance Criteria

srd_generate on the tower_dungeon.asc dataset completes within 30s budget; output tiles are non-empty; no SymX headers included; build succeeds without linking against SymX; demo_client.c compiles and runs without changes to its call site


## Notes

**2026-07-06T06:09:51Z**

DESIGN REVISION (2026-07-06): srd_bridge now: (1) parses ASCII → seed boxes, (2) calls srd_seed_to_grid to initialize the SDF grid + room map, (3) runs srd_descent_optimize on the grid, (4) calls srd_sdf_to_svo to convert final grid to npc_svo_grid_t. No tile output — SVO is the output format. Depends on srd-grid-03 and srd-grid-04.
