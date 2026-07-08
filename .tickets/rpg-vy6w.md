---
id: rpg-vy6w
status: closed
deps: []
links: []
created: 2026-07-06T05:44:35Z
type: epic
priority: 1
assignee: KMD
tags: [srd, voxel]
---
# SRD-E8: Voxel SDF Grid Foundation

Replace the box-array layout (srd_sdf_layout_t with srd_sdf_box_t) with a dense voxel SDF grid (srd_sdf_grid_t). The grid stores signed distance values (negative=inside, positive=outside) and supports CSG stamping of primitives (box, sphere). Room identity is tracked via a parallel uint8 grid of room IDs. Seed layouts from the grammar are stamped into the grid as box SDFs. Output path: booleanize SDF at threshold 0, feed into the existing SVO pipeline (npc_svo_grid_t).

