---
id: rpg-vfmi
status: closed
deps: []
links: []
created: 2026-07-20T00:13:08Z
type: task
priority: 2
assignee: KMD
parent: rpg-hjck
tags: [streaming, core]
---
# Unified fr_asset_stream residency budget (lightmap+SDF+probes)

CORE: one fr_asset_stream RAM/VRAM budget shared across ALL light-data chunk classes (lightmap SH, SDF/voxel, probes) so total residency is bounded regardless of world size. Today the lightmap chunk(s) + SDF chunks use SEPARATE fr_asset_streams/budgets (fine for one hall, wrong for a world). Merge into one stream with class-dispatched callbacks + a single budget; priority spans all classes (visible + near + server-suggested).

## Acceptance Criteria

One fr_asset_stream owns lightmap+SDF(+probe) chunks under a single RAM/VRAM budget; eviction spans classes by priority.

