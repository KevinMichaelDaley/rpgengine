---
id: rpg-q0c2
status: in_progress
deps: []
links: []
created: 2026-07-09T01:20:35Z
type: task
priority: 1
assignee: KMD
parent: rpg-9fkk
tags: [srd, renderer]
---
# CPU SDF raymarcher

Demoscene-style CPU sphere-tracing raymarcher operating directly on srd_sdf_grid_t. Renders smooth SDF surfaces (no voxel snapping — voxels are an implementation detail). Normals from SDF gradient via central differences. Blinn-Phong lighting with directional light + ambient. Used at training time to generate reward signal images.

## Design

API: srd_sdf_raycast(grid, config, rgb_out). Config holds camera pose, image dims, light params. Per-pixel: cast ray, sphere-trace through SDF grid with trilinear interpolation, on hit compute normal from central-difference gradient, shade with Blinn-Phong. Output: interleaved RGB uint8 buffer. Target: 64x64 or 128x128 images. Files: include/ferrum/procgen/srd/srd_sdf_raycast.h, src/procgen/srd/srd_sdf_raycast.c

## Acceptance Criteria

Renders a test SDF grid to a PPM file. Smooth surfaces, correct normals, visible lighting. No voxel snapping artifacts. Runs under 5ms for 128x128.

