---
id: rpg-t9l2
status: closed
deps: []
links: [rpg-ca3y, rpg-hf0x, rpg-goao, rpg-n1kq, rpg-k80x, rpg-qhya, rpg-sae6, rpg-z40l, rpg-rt38, rpg-p6bd, rpg-umku]
created: 2026-07-09T02:52:58Z
type: task
priority: 1
assignee: KMD
parent: rpg-9fkk
tags: [srd, procgen, rules]
---
# SDF rewrite rules: column grid with bases and capitals

Dense pillar grids with proper column profiles (base pedestal + shaft + capital). Visible in undercroft (4x6 grid of round columns with square bases), treasury (3x4 grid), mead hall, cistern. The current add_pillar rule only places ONE plain cylinder at room center. This ticket adds grid placement and column profiles.

## Design

**srd_rules_column_grid.c** (4 functions):

1. **column_grid**: Stamp a regular NxM grid of cylindrical columns inside the room. Grid spacing auto-computed from room bbox (2-6 columns per axis). Each column: cylinder radius = 1 voxel, with a wider square base (2x base, 2 voxels tall) and square capital (1.5x base, 1 voxel tall). Columns span from base to capital (floor to ceiling). face=NONE. param ∈ [1, 3]: column radius.
2. **column_grid_remove**: Inverse — carve all columns back out.
3. **column_row**: Like column_grid but places a single row of columns along one face (parallel to selected wall, inset by room_width/4). Visible in great hall (two rows of pillars flanking central aisle). face=N/S/E/W. param ∈ [1, 3]: column radius.
4. **column_row_remove**: Inverse.

Columns are stamped as solid (CSG subtract) with room_id=0. Base and capital use box SDF, shaft uses cylinder SDF.

### Files
- include/ferrum/procgen/srd/srd_rules_column_grid.h
- src/procgen/srd/srd_rules_column_grid.c
- tests/srd_rules_column_grid_tests.c
- srd_voxel_rule_table.c: +4 entries

## Acceptance Criteria

- 4 rules compile, link, pass tests
- column_grid places NxM grid of columns with bases and capitals
- column_row places a single row of columns parallel to selected wall
- Column profiles: wider base, cylindrical shaft, wider capital
- Grid spacing is proportional to room size (no columns outside room)
- Apply then remove restores original grid


## Notes

**2026-07-16T03:19:33Z**

OBSOLETE: SDF-rewrite-rules approach abandoned; replaced by Blender procgen (bpy/bmesh arch-mesh generators).
