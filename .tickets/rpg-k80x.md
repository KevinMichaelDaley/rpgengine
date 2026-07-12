---
id: rpg-k80x
status: open
deps: []
links: [rpg-t9l2, rpg-ca3y, rpg-hf0x, rpg-goao, rpg-n1kq, rpg-qhya, rpg-sae6, rpg-z40l, rpg-rt38, rpg-p6bd, rpg-g3qe]
created: 2026-07-09T02:58:32Z
type: task
priority: 2
assignee: KMD
parent: rpg-9fkk
tags: [srd, procgen, rules]
---
# SDF rewrite rules: circular room and apse

Round room shapes and semicircular room extensions. Visible in watchtower (fully circular room), council chamber (chamfered octagonal shape), ritual chamber (rounded corners making near-circular). The existing corner_round rule rounds individual corners with small radii. This adds whole-room rounding and semicircular apse extensions.

## Design

**srd_rules_round_room.c** (4 functions):

1. **round_room**: SDF interpolation from box room toward cylinder room. Compute cylinder SDF centered on room's XZ center, radius = min(half_w, half_d). Lerp ceiling and walls from box toward cylinder. face=NONE. param ∈ [0.3, 1.0]: roundness (0.3 = slightly rounded, 1.0 = full circle inscribed in bbox).
2. **round_room_remove**: Inverse — lerp back toward box.
3. **apse**: Stamp a semicircular (hemicylinder) extension on the selected wall face. Extends the room outward as a rounded bay. Uses cylinder SDF. face=N/S/E/W. param ∈ [2, 5]: apse depth in voxels.
4. **apse_remove**: Inverse.

round_room uses SDF interpolation on all wall voxels. apse uses CSG union to carve the semicircular extension, assigning to the room's id.

### Files
- include/ferrum/procgen/srd/srd_rules_round_room.h
- src/procgen/srd/srd_rules_round_room.c
- tests/srd_rules_round_room_tests.c
- srd_voxel_rule_table.c: +4 entries

## Acceptance Criteria

- 4 rules compile, link, pass tests
- round_room interpolates rectangular room toward cylindrical shape
- apse extends a semicircular bay from one wall
- Intermediate param values produce intermediate shapes
- Apply then remove restores original grid

