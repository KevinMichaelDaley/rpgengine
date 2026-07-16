---
id: rpg-qhya
status: closed
deps: []
links: [rpg-t9l2, rpg-ca3y, rpg-hf0x, rpg-goao, rpg-n1kq, rpg-k80x, rpg-sae6, rpg-z40l, rpg-rt38, rpg-p6bd, rpg-xr08]
created: 2026-07-09T02:47:46Z
type: task
priority: 1
assignee: KMD
parent: rpg-9fkk
tags: [srd, procgen, rules]
---
# SDF rewrite rules: wall ledge and cornice

Horizontal bands projecting from walls at specific heights, creating visual layering and articulation. Visible in library (at arch spring height), council chamber (gallery ledge), guard checkpoint (top of half-wall), mine depot (platform edges). Transforms featureless walls into structured surfaces with clear horizontal rhythm.

## Design

**srd_rules_wall_ledge.c** (4 functions, face=N/S/E/W or CEIL):

1. **wall_ledge**: Stamp a thin horizontal band (1-2 voxels tall, param voxels deep) projecting from the selected wall face at 2/3 room height. Runs the full length of the face. Creates a cornice/string course. param ∈ [1, 3]: projection depth.
2. **wall_ledge_remove**: Inverse — carve ledge back out.
3. **ceiling_cornice**: Stamp a ledge where each wall meets the ceiling — an L-shaped molding at the wall-ceiling junction. Runs along ALL four walls of the room (not face-specific, uses face=CEIL). param ∈ [1, 2]: cornice depth.
4. **ceiling_cornice_remove**: Inverse.

These stamp solid geometry projecting from the wall face (CSG subtract) and mark as wall. The inverse carves back out (CSG union) and reassigns to room.

### Files
- include/ferrum/procgen/srd/srd_rules_wall_ledge.h
- src/procgen/srd/srd_rules_wall_ledge.c
- tests/srd_rules_wall_ledge_tests.c
- srd_voxel_rule_table.c: +4 entries

## Acceptance Criteria

- 4 rules compile, link, pass tests
- wall_ledge creates a horizontal projection along a wall face at 2/3 height
- ceiling_cornice creates ledges at all four wall-ceiling junctions
- Apply then remove restores original grid
- Ledges do not block doorways


## Notes

**2026-07-16T03:19:32Z**

OBSOLETE: SDF-rewrite-rules approach abandoned; replaced by Blender procgen (bpy/bmesh arch-mesh generators).
