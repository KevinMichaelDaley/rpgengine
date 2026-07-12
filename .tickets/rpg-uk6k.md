---
id: rpg-uk6k
status: open
deps: []
links: []
created: 2026-07-09T02:38:57Z
type: task
priority: 2
assignee: KMD
parent: rpg-9fkk
tags: [srd, dataset, renderer]
---
# SRD screenshot tool (GL client render to image)

Standalone tool that generates SRD dungeon geometry from an ASCII floor plan, meshes it via procgen_mesh_from_svo(), renders one frame with the GL client renderer (whiteboxed, no lighting needed), and captures the framebuffer to a PPM/PNG via glReadPixels. Takes ASCII file + camera params (pos, dir, fov) as arguments. Used to generate the training dataset for the learned critic.

## Design

Based on existing patterns:
- SRD bridge (srd_generate_svo) for ASCII → SDF → SVO
- procgen_mesh_from_svo() for SVO → triangle mesh
- demo_client's --srd rendering path for GL mesh upload + draw
- Visual test save_ppm_() pattern for glReadPixels capture

Tool creates an SDL window + GL context (NOT headless — user needs to see output), generates the dungeon, sets up camera, renders one frame, captures to PPM, and optionally loops for multiple camera angles.

## Acceptance Criteria

- Takes an .asc file and camera parameters, outputs a PPM image
- Rendered geometry matches what demo_client shows for the same .asc
- Can generate batch of images from multiple camera angles in a single run
- Whiteboxed rendering (flat/unlit or basic diffuse)

