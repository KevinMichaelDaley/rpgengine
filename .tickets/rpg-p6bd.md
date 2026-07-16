---
id: rpg-p6bd
status: closed
deps: []
links: [rpg-t9l2, rpg-ca3y, rpg-hf0x, rpg-goao, rpg-n1kq, rpg-k80x, rpg-qhya, rpg-sae6, rpg-z40l, rpg-rt38, rpg-okya]
created: 2026-07-09T02:49:08Z
type: task
priority: 1
assignee: KMD
parent: rpg-9fkk
tags: [srd, procgen, rules]
---
# SDF rewrite rules: balcony, gallery, and mezzanine

Raised walkways running along room perimeters at mid-height, creating upper galleries/balconies overlooking the main floor. Prominent in council chamber (full wraparound gallery with arched openings below and above), guard checkpoint (half-wall balustrade), watchtower (spiral mezzanine). Creates dramatic vertical subdivision of room space.

## Design

**srd_rules_gallery.c** (4 functions):

1. **gallery**: Stamp a solid shelf projecting inward from a wall face at mid-height (Y = room_mid_y). Shelf width = param voxels, thickness = 2 voxels. Creates a walkable platform. Also stamps a low parapet (1 voxel) at the shelf edge. face=N/S/E/W selects which wall. param ∈ [2, 5]: shelf projection depth.
2. **gallery_remove**: Inverse — carve the gallery shelf back out.
3. **gallery_ring**: Full wraparound gallery on ALL four walls (face=CEIL, since it's whole-room). Same shelf+parapet as gallery but on all sides. Leaves corner connections. Creates the council-chamber effect. param ∈ [2, 5]: shelf depth.
4. **gallery_ring_remove**: Inverse.

Gallery voxels become solid (SDF positive, room_id=0) for the shelf structure, but the space above the shelf and below the ceiling remains air (room interior) for the upper walkway.

### Files
- include/ferrum/procgen/srd/srd_rules_gallery.h
- src/procgen/srd/srd_rules_gallery.c
- tests/srd_rules_gallery_tests.c
- srd_voxel_rule_table.c: +4 entries

## Acceptance Criteria

- 4 rules compile, link, pass tests
- gallery creates a solid shelf along one wall at mid-height with parapet
- gallery_ring creates full wraparound gallery on all 4 walls
- Space above shelf remains air (walkable), space below remains air (main floor)
- Apply then remove restores original grid


## Notes

**2026-07-16T03:19:32Z**

OBSOLETE: SDF-rewrite-rules approach abandoned; replaced by Blender procgen (bpy/bmesh arch-mesh generators).
