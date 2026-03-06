---
id: rpg-b8u1
status: closed
deps: []
links: []
created: 2026-03-06T07:54:03Z
type: task
priority: 2
assignee: KMD
---
# Blender fskel exporter: coordinate conversion + walk cycle visual test

Fix the Blender .fskel export script to convert from Blender Z-up to engine Y-up coordinate system. Then create a visual test that loads asset_src/humanoid.fskel with the full constraint list and physics, animating IK control bones (c_foot_ik.l, c_foot_ik.r, c_traj) to simulate a procedural walk cycle over a ground plane.

## Coordinate conversion in export_fskel.py
- Apply Z-up to Y-up conversion matrix to all transforms (rest_local, rest_world, IBMs)
- Conversion: swap Y<->Z, negate new Z (standard Blender to GL convention)
- Re-export humanoid.fskel with corrected coordinates
- Verify round-trip: exported file loads correctly, skeleton is upright

## Walk cycle visual test (p005_visual_walk_cycle.c)
- Load asset_src/humanoid.fskel via fskel_load()
- Render skeleton as debug lines (bone segments + joint points)
- Animate c_foot_ik.l (idx 248) and c_foot_ik.r (idx 229) with sinusoidal stride pattern
- Animate c_traj (idx 1) with forward translation
- Draw ground plane grid at y=0
- Run constraint solver each frame to propagate IK
- Orbit camera, video capture, PPM snapshots
- PASS: 90 frames, 0 GL errors

## Key bones
- c_foot_ik.l (index 248): left foot IK target
- c_foot_ik.r (index 229): right foot IK target
- c_traj (index 1): trajectory/root motion bone
- c_pos (index 0): position root

## Acceptance Criteria

- export_fskel.py applies Z-up to Y-up conversion to all matrices
- humanoid.fskel re-exported with correct coordinates
- Visual test loads skeleton, renders upright, animates walk cycle
- Video output shows recognizable walking motion
- All existing tests still pass

