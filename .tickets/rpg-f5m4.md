---
id: rpg-f5m4
status: open
deps: [rpg-9ont]
links: []
created: 2026-07-13T06:41:34Z
type: task
priority: 2
assignee: KMD
parent: rpg-nt4y
---
# Heightmap -> cone/SDF RGBA map generator (depth/cone/SDF/complexity)

Offline preprocess: convert a heightmap into an RGBA map where R=depth(height), G=cone angle (widest empty cone above the texel, for cone stepping), B=signed distance (for relaxed/sphere-trace refinement), A=max step count / surface-complexity hint. Bake-time tool, like the existing material bakes.

## Design

Core-renderer-adjacent tool. Cone-map precompute (per-texel max non-intersecting cone) + SDF; complexity from local height variance / expected steps.

