---
id: rpg-p2xy
status: closed
deps: [rpg-tqr1, rpg-y5p7, rpg-4x04]
links: []
created: 2026-07-13T05:10:22Z
type: task
priority: 2
assignee: KMD
parent: rpg-zket
---
# Clustered/froxel light culling (grid + per-cluster light lists + GPU buffers)

Build the froxel/cluster grid over the view frustum, assign each realtime light to the clusters it overlaps, and upload per-cluster light index lists to GPU buffers (SSBO/UBO) so the shading pass reads only relevant lights per fragment.

## Design

Core renderer. Depends on light entities + scene interface + depth pre-pass. Cluster grid (e.g., 16x9xZ froxels), light->cluster assignment on CPU or compute, bounded per-cluster list.

