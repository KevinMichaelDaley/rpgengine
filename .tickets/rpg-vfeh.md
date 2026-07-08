---
id: rpg-vfeh
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
# srd-vrule-05: pillar/arch/convert-type feature rules

AddPillar: subtract a cylindrical SDF into a room (makes solid). RemovePillar: inverse (carve it back out). ArchDoorway: reshape doorway top into an arch. SquareDoorway: inverse. ConvertType: cycle room type in room_map (self-inverse). Pillar uses stamp_sphere/subtract_sphere.

## Acceptance Criteria

Tests: add pillar, verify solid cylinder. Remove it, verify round-trip. Convert type, verify room_map updated.

