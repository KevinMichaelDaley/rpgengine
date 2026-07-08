---
id: rpg-wuo0
status: closed
deps: [rpg-x74u]
links: []
created: 2026-07-06T05:45:35Z
type: task
priority: 1
assignee: KMD
tags: [srd, voxel, rules]
parent: rpg-rtxv  # SRD-E9: Voxel SDF Rewrite Rules
---
# srd-vrule-04: corridor widen/narrow/curve rewrite rules

CorridorWiden/CorridorNarrow: adjust corridor width by param voxels. CorridorCurve: bend a straight corridor segment. Select corridor room_id. Widen<->narrow are inverse pairs. Curve<->straighten are inverse pairs.

## Acceptance Criteria

Tests: widen corridor, verify wider cross-section. Narrow back, verify round-trip. Curve a straight corridor, verify bend.

