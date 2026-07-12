---
id: rpg-ca3y
status: open
deps: []
links: [rpg-t9l2, rpg-hf0x, rpg-goao, rpg-n1kq, rpg-k80x, rpg-qhya, rpg-sae6, rpg-z40l, rpg-rt38, rpg-p6bd, rpg-kbuf]
created: 2026-07-09T02:47:31Z
type: task
priority: 1
assignee: KMD
parent: rpg-9fkk
tags: [srd, procgen, rules]
---
# SDF rewrite rules: pilasters and wall buttresses

Wall-attached columns and buttresses that project from wall faces. Visible in ~50% of dwarven dataset rooms (shrine, library, grand gate hall, treasury, guard checkpoint, mine depot). Massive visual impact — transforms flat walls into articulated surfaces.

## Design

**srd_rules_pilaster.c** (4 functions, face=N/S/E/W):

1. **pilaster_row**: Stamp equally-spaced rectangular pilasters along a wall face, projecting inward. Spacing auto-computed from room width (3-5 per wall). Each pilaster: width = 2 voxels, depth = param voxels, full room height. param ∈ [1, 3].
2. **pilaster_row_remove**: Inverse — carve the pilasters back out.
3. **half_column_row**: Same spacing as pilasters but with half-cylinder profile (semicircular cross-section) instead of rectangular. More refined look. param ∈ [1, 3]: radius.
4. **half_column_row_remove**: Inverse.

These rules stamp SOLID geometry (CSG subtract) that projects into the room interior from the wall, then assign the voxels to room_id=0 (wall). The inverse carves them back out (CSG union) and reassigns to the room.

### Files
- include/ferrum/procgen/srd/srd_rules_pilaster.h
- src/procgen/srd/srd_rules_pilaster.c
- tests/srd_rules_pilaster_tests.c
- srd_voxel_rule_table.c: +4 entries

## Acceptance Criteria

- 4 rules compile, link, pass tests
- pilaster_row places evenly-spaced rectangular projections along selected wall face
- half_column_row places evenly-spaced semicircular projections
- Apply then remove restores original grid
- Pilasters do not block doorways (skip placement where adjacent room detected)

