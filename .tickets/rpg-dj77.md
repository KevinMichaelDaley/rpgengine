---
id: rpg-dj77
status: closed
deps: [rpg-8hi9]
links: []
created: 2026-02-28T22:27:55Z
type: task
priority: 2
assignee: KMD
parent: rpg-37uq
tags: [editor, mesh, geometry, csg]
---
# Brush/plane-based mesh creation (mesh_create_from_brush)

Implement brush-style mesh creation from intersection of half-planes (TrenchBroom/Quake heritage).

mesh_create_from_brush: Create a convex mesh from the intersection of half-planes. Each plane is defined by a normal and distance. The convex hull of the plane intersection forms the mesh geometry.

This provides the 'brush' workflow from Quake/Source editors: define a convex volume by specifying boundary planes, and the engine computes the resulting solid mesh with correct vertices, edges, and faces.

Algorithm:
1. Start with a large bounding box
2. For each plane, clip the current mesh (using the clip operation)
3. The result is the convex intersection of all half-spaces
4. Generate normals, UVs (box projection), and indices

Args:
- mesh_create_from_brush: {"planes": [{"normal": [x,y,z], "dist": float}, ...]}

Files to create:
- src/editor/mesh/mesh_brush.c — plane intersection algorithm
- src/editor/commands/cmd_mesh_brush.c — command handler
- tests/editor/mesh_brush_tests.c

## Acceptance Criteria

- 6 axis-aligned planes create a box mesh
- Arbitrary plane count creates correct convex shape
- Resulting mesh is watertight (closed surface)
- Face normals match the defining planes
- UVs generated via box projection
- Degenerate planes (parallel, coincident) handled gracefully
- Tests: axis box (6 planes), wedge (5 planes), pyramid (5 planes), degenerate

