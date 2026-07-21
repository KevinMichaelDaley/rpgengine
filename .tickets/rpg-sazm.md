---
id: rpg-sazm
status: open
deps: [rpg-oda7]
links: []
created: 2026-07-19T21:26:57Z
type: task
priority: 1
assignee: KMD
parent: rpg-hjck
tags: [gi, streaming]
---
# Dual-output visibility prepass (SDF + lightmap chunk ids)

Extend gi_vis_prepass into ONE low-res geometry pass writing two chunk indices to different channels of an integer MRT: R=SDF chunk id (per-fragment from world pos vs SDF boxes, WORLD mode), G=lightmap chunk id (per-mesh uniform, MESH mode). Two async PBO readbacks -> visible SDF-chunk set + visible lightmap-chunk set. SDF and lightmap chunk boundaries are independent (voxels vs atlases), hence two index sets. Drives per-chunk residency/priority in the streamer.

## Design

See ref/gi_streaming_design.md 'Visibility prepass'. Today gi_vis_prepass has separate run_mesh/run_world; unify into a dual-output pass.

## Acceptance Criteria

One prepass yields both visible sets; lightmap + SDF chunks page independently by on-screen visibility.

