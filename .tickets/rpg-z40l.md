---
id: rpg-z40l
status: open
deps: []
links: [rpg-t9l2, rpg-ca3y, rpg-hf0x, rpg-goao, rpg-n1kq, rpg-k80x, rpg-qhya, rpg-sae6, rpg-rt38, rpg-p6bd, rpg-6kdy]
created: 2026-07-09T02:55:46Z
type: task
priority: 2
assignee: KMD
parent: rpg-9fkk
tags: [srd, procgen, rules]
---
# SDF rewrite rules: circular floor pits and channels

Circular sunken features in floors. Visible in smelting hall (grid of circular crucible pits connected by channels), bathhouse (rectangular basins divided by cross-walls), cistern (large central reservoir). The existing floor_pit rule only carves rectangular pits in the center. This adds circular pits, pit grids, and connecting channels.

## Design

**srd_rules_floor_pit_circ.c** (4 functions, face=FLOOR):

1. **circular_pit**: Carve a cylindrical pit in the center of the room floor. Uses sphere SDF for the pit bottom (bowl shape). Radius = room_min_dim/4. param ∈ [1, 3]: pit depth in voxels.
2. **circular_pit_fill**: Inverse.
3. **pit_grid**: Stamp a regular grid of smaller circular pits across the floor. Auto-computed 2x2 or 3x3 grid from room size. Each pit has cylindrical walls. Creates crucible/basin grids. param ∈ [1, 3]: pit depth.
4. **pit_grid_fill**: Inverse.

Pits carve into solid below the floor (CSG union, set SDF negative, assign room_id). Fill operations restore solid (CSG subtract).

### Files
- include/ferrum/procgen/srd/srd_rules_floor_pit_circ.h
- src/procgen/srd/srd_rules_floor_pit_circ.c
- tests/srd_rules_floor_pit_circ_tests.c
- srd_voxel_rule_table.c: +4 entries

## Acceptance Criteria

- 4 rules compile, link, pass tests
- circular_pit carves a bowl-shaped pit in the room center
- pit_grid creates NxM grid of circular pits evenly across the floor
- Apply then remove restores original grid

