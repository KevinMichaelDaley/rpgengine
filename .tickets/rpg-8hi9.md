---
id: rpg-8hi9
status: closed
deps: [rpg-clq6]
links: []
created: 2026-02-28T22:26:23Z
type: task
priority: 2
assignee: KMD
parent: rpg-37uq
tags: [editor, mesh, csg]
---
# Clip tool (split mesh by plane)

Implement the clip tool for splitting meshes along an arbitrary plane.

clip: Split mesh geometry by a plane defined by a point and normal. Three modes:
- 'front': keep geometry on the positive side of the plane
- 'back': keep geometry on the negative side
- 'both': split into two separate meshes (active + scratch slot)

Algorithm:
1. Classify each vertex as front/back/on relative to the plane
2. For triangles straddling the plane, compute intersection points on edges
3. Split straddling triangles into sub-triangles
4. Discard triangles on the removed side
5. Cap the cut surface with new triangles (fill the hole)

Args:
- clip: {"plane_point": [x,y,z], "plane_normal": [x,y,z], "keep": "front"|"back"|"both"}

Files to create:
- src/editor/mesh/mesh_clip.c — plane classification, edge intersection, triangle splitting
- src/editor/mesh/mesh_clip_cap.c — cap generation for cut surface
- src/editor/commands/cmd_clip.c — command handler
- tests/editor/mesh_clip_tests.c

## Acceptance Criteria

- Clip box in half: produces correct geometry on each side
- Cap surface fills the cut hole with valid triangles
- Clip with 'both' produces two valid meshes in separate slots
- Edge intersection points are geometrically correct
- No degenerate triangles at clip boundary
- Normals correct on cap faces
- Tests: clip box front/back/both, angled plane, vertex-on-plane edge case

