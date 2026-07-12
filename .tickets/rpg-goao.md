---
id: rpg-goao
status: open
deps: []
links: [rpg-t9l2, rpg-ca3y, rpg-hf0x, rpg-n1kq, rpg-k80x, rpg-qhya, rpg-sae6, rpg-z40l, rpg-rt38, rpg-p6bd, rpg-k04n]
created: 2026-07-09T02:48:46Z
type: task
priority: 1
assignee: KMD
parent: rpg-9fkk
tags: [srd, procgen, rules]
---
# SDF rewrite rules: partition walls and arcade walls

Internal walls that subdivide rooms. Visible in prison block (solid cell dividers), guard checkpoint (arcade wall with arched openings), bathhouse (cross-shaped basin dividers), barracks. Critical for generating complex multi-zone rooms from single room regions.

Two variants:
- Solid partition: thin wall bisecting the room, with doorway gaps at ends
- Arcade wall: partition with regular arched openings (colonnade effect) — combines partition + arch rules

## Design

**srd_rules_partition.c** (4 functions):

1. **partition_wall**: Stamp thin solid wall (2 voxels thick) along the room's center perpendicular to the selected face normal. Leaves 2-voxel doorway gaps at each end for passage. face=N/S → wall runs E-W; face=E/W → wall runs N-S. param ∈ [1, 2]: wall thickness.
2. **partition_wall_remove**: Inverse — carve the partition back out.
3. **arcade_wall**: Same as partition_wall but with regular semicircular arched openings cut through it. Auto-spaces 2-4 arches along the wall length. Each arch: semicircular top, rectangular below, width = spacing/2. Creates the colonnade/arcade visible in guard checkpoint and cistern. param ∈ [1, 2]: wall thickness.
4. **arcade_wall_remove**: Inverse.

Partition walls assign voxels to room_id=0 (wall) and set SDF to positive. The gaps and arches remain as room interior.

### Files
- include/ferrum/procgen/srd/srd_rules_partition.h
- src/procgen/srd/srd_rules_partition.c
- tests/srd_rules_partition_tests.c
- srd_voxel_rule_table.c: +4 entries

## Acceptance Criteria

- 4 rules compile, link, pass tests
- partition_wall creates centered divider wall with doorway gaps at ends
- arcade_wall creates divider wall with evenly-spaced arched openings
- Partition walls bisect the room in the correct axis based on face selection
- Apply then remove restores original grid

