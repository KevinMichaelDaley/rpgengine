---
id: rpg-hf0x
status: closed
deps: []
links: [rpg-t9l2, rpg-ca3y, rpg-goao, rpg-n1kq, rpg-k80x, rpg-qhya, rpg-sae6, rpg-z40l, rpg-rt38, rpg-p6bd, rpg-iotp]
created: 2026-07-09T02:54:07Z
type: task
priority: 1
assignee: KMD
parent: rpg-9fkk
tags: [srd, procgen, rules]
---
# SDF rewrite rules: wall openings (windows, arrow slits, wall recesses)

Small openings in walls for windows, arrow slits, and regular storage recesses. Visible in watchtower (arrow slits — narrow vertical slots), library (clerestory windows above arches, tall shelf recesses), armory (weapon rack recesses), council chamber (upper arched windows).

## Design

**srd_rules_wall_opening.c** (4 functions, face=N/S/E/W):

1. **arrow_slit_row**: Carve narrow vertical slots (1 voxel wide, room_height/2 tall) through the selected wall face. Auto-spaces 2-4 slits evenly along the wall. Slits go fully through the wall (carve solid to air). param ∈ [1, 2]: slit width.
2. **arrow_slit_row_remove**: Inverse — fill slits back in.
3. **shelf_recess_row**: Carve regular rectangular recesses into the selected wall (not fully through). Each recess: width = spacing/2, height = 2/3 room height, depth = param voxels into the wall. Creates library shelf or armory rack appearance. param ∈ [1, 3]: recess depth.
4. **shelf_recess_row_remove**: Inverse.

Openings carve into solid wall (CSG union, set SDF negative). Slits go fully through; recesses stop at param depth. Opening voxels get room_id of the room.

### Files
- include/ferrum/procgen/srd/srd_rules_wall_opening.h
- src/procgen/srd/srd_rules_wall_opening.c
- tests/srd_rules_wall_opening_tests.c
- srd_voxel_rule_table.c: +4 entries

## Acceptance Criteria

- 4 rules compile, link, pass tests
- arrow_slit_row creates narrow vertical openings through the wall
- shelf_recess_row creates regular rectangular alcoves into the wall
- Recesses do not punch fully through wall (depth-limited)
- Apply then remove restores original grid


## Notes

**2026-07-16T03:19:32Z**

OBSOLETE: SDF-rewrite-rules approach abandoned; replaced by Blender procgen (bpy/bmesh arch-mesh generators).
