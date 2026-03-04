---
id: rpg-h66f
status: open
deps: [rpg-o0a7, rpg-hezs]
links: [rpg-d3ue, rpg-zryp, rpg-hezs, rpg-9y61, rpg-8ot1]
created: 2026-03-04T02:56:00Z
type: task
priority: 2
assignee: KMD
---
# Phase 3 visual test: scene graph hierarchy and transforms

End-to-end graphical test for Phase 3 LCRS scene graph. Renders a hierarchical scene where child entities inherit and compose parent transforms, verifying correct world-space positioning through the full render pipeline.

Test verifies:
- LCRS tree traversal produces correct world transforms via BFS dirty propagation
- Parent rotation propagates to children (rotating parent rotates child orbit)
- Parent scale propagates multiplicatively to children
- Deep hierarchy (4+ levels) composes transforms correctly
- Reparenting mid-frame updates child world position immediately
- Detaching a child preserves its current world transform
- Flat-array parallel storage matches tree traversal output
- Skeleton bone hierarchy renders as connected chain (bones as entities)

Scene layout: Solar system model — a central sphere (sun) with 3 orbiting spheres (planets), each with 1-2 orbiting smaller spheres (moons). Sun rotates slowly, planets orbit, moons orbit planets. Hierarchy: sun → planet → moon. Also includes a 6-bone arm skeleton as entities to verify bone chain rendering. Output: tests/output/phase3_scene_graph.mp4

File: tests/visual/p004_visual_scene_graph.c
Duration: 5 seconds at 30fps (150 frames) — longer to show orbital motion
Exit: PASS if hierarchy transforms are visually correct, frame count >= 150, no GL errors.

