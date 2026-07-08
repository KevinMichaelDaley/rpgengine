---
id: rpg-pwyq
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
# srd-vrule-02: corner chamfer/round rewrite rules

CornerChamfer: 45-degree cut on a room corner (select room_id + corner NE/NW/SE/SW + cut size). CornerRound: round a corner with given radius. Both save original SDF values for inverse restoration.

## Acceptance Criteria

Tests: chamfer a corner, verify diagonal cut. Apply inverse, verify round-trip within epsilon.

