---
id: rpg-rt38
status: open
deps: []
links: [rpg-t9l2, rpg-ca3y, rpg-hf0x, rpg-goao, rpg-n1kq, rpg-k80x, rpg-qhya, rpg-sae6, rpg-z40l, rpg-p6bd, rpg-u4bb]
created: 2026-07-09T02:56:32Z
type: task
priority: 2
assignee: KMD
parent: rpg-9fkk
tags: [srd, procgen, rules]
---
# SDF rewrite rules: ceiling beams and vault ribs

Horizontal structural/decorative elements on ceilings. Visible in mead hall (exposed roof beams — parallel bars crossing the ceiling), great hall and crypt (groin vault ribs — diagonal arched bands on vault surfaces), forge (beam grid).

## Design

**srd_rules_ceiling_beam.c** (4 functions, face=CEIL):

1. **ceiling_beam_row**: Stamp parallel solid beams running across the ceiling perpendicular to the room's long axis. Auto-spaces 3-6 beams. Each beam: 1 voxel wide, param voxels deep (hanging down from ceiling), full span. Creates exposed beam / rib effect. param ∈ [1, 2]: beam depth.
2. **ceiling_beam_row_remove**: Inverse.
3. **ceiling_beam_grid**: Stamp crossing beams in both axes (grid pattern). Creates coffered ceiling / waffle-slab effect. param ∈ [1, 2]: beam depth.
4. **ceiling_beam_grid_remove**: Inverse.

Beams are solid projections downward from ceiling (CSG subtract, room_id=0). They occupy room interior air space.

### Files
- include/ferrum/procgen/srd/srd_rules_ceiling_beam.h
- src/procgen/srd/srd_rules_ceiling_beam.c
- tests/srd_rules_ceiling_beam_tests.c
- srd_voxel_rule_table.c: +4 entries

## Acceptance Criteria

- 4 rules compile, link, pass tests
- ceiling_beam_row creates parallel beams across short axis of room
- ceiling_beam_grid creates crossing beam pattern
- Beams hang from ceiling by param voxels
- Apply then remove restores original grid

