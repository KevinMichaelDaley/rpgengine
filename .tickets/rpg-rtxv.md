---
id: rpg-rtxv
status: closed
deps: [rpg-vy6w]
links: []
created: 2026-07-06T05:45:35Z
type: epic
priority: 1
assignee: KMD
tags: [srd, voxel, rules]
---
# SRD-E9: Voxel SDF Rewrite Rules

Local, jump-continuous, invertible rewrite rules that operate on the voxel SDF grid. Rules select a (room, face/corner/region) pair and modify SDF values locally. Every rule has a natural inverse. Rules must NOT add or remove entire rooms — they only reshape existing geometry. Categories: wall (push/pull/bevel/niche), corner (chamfer/round), height (ceiling raise/lower, floor step), corridor (widen/narrow/curve), feature (pillar, arch doorway, convert type). Selection model: srd_sdf_selection_t with room_id, face, corner, param fields.

