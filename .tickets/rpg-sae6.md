---
id: rpg-sae6
status: open
deps: []
links: [rpg-t9l2, rpg-ca3y, rpg-hf0x, rpg-goao, rpg-n1kq, rpg-k80x, rpg-qhya, rpg-z40l, rpg-rt38, rpg-p6bd, rpg-jr2w]
created: 2026-07-09T02:53:47Z
type: task
priority: 1
assignee: KMD
parent: rpg-9fkk
tags: [srd, procgen, rules]
---
# SDF rewrite rules: stairs and ramps

Stepped and ramped floor connections between different levels within a room. Visible in throne room (steps up to dais), council chamber (tiered amphitheater seating), watchtower (spiral stair), quarry (stepped ledges), mine depot (loading ramps).

## Design

**srd_rules_stairs.c** (4 functions):

1. **stairs_against_wall**: Step sequence rising from floor toward the selected wall face. N steps (each 1 voxel high, param voxels deep), starting from the opposite wall and rising toward the selected face. Creates tiered/amphitheater seating or quarry stepped ledges. face=N/S/E/W. param ∈ [1, 4]: step depth in voxels.
2. **stairs_against_wall_remove**: Inverse.
3. **ramp**: Smooth linear ramp (actually fine-grained steps at sub-voxel resolution) rising from floor toward selected face. Uses SDF interpolation: target SDF is a tilted plane, lerp from flat floor. Creates loading ramps and gentle inclines. face=N/S/E/W. param ∈ [0.3, 1.0]: ramp steepness.
4. **ramp_remove**: Inverse.

Stairs are stamped as solid (CSG subtract, room_id=0) stacking from floor up. Each step is a box the full width of the room.

### Files
- include/ferrum/procgen/srd/srd_rules_stairs.h
- src/procgen/srd/srd_rules_stairs.c
- tests/srd_rules_stairs_tests.c
- srd_voxel_rule_table.c: +4 entries

## Acceptance Criteria

- 4 rules compile, link, pass tests
- stairs_against_wall creates N steps rising toward the selected wall
- ramp creates smooth incline toward selected wall
- Steps are full room width, each 1 voxel high
- Apply then remove restores original grid

