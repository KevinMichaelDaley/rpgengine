---
id: rpg-syut
status: closed
deps: [rpg-ma1t]
links: []
created: 2026-02-28T22:21:17Z
type: task
priority: 2
assignee: KMD
parent: rpg-caw8
tags: [editor, mesh, geometry]
---
# Mesh primitive creation (box, cylinder, sphere, plane)

Implement mesh primitive generation commands that populate a mesh_slot_t with indexed triangle geometry.

Commands:
- mesh_create_box: axis-aligned box with configurable size [x,y,z], segments [sx,sy,sz], and position
- mesh_create_cylinder: cylinder with radius, height, segment count, and axis
- mesh_create_sphere: UV sphere with radius and segment count
- mesh_create_plane: grid plane with size, segments, and axis orientation

Each primitive generator:
1. Calculates vertex count and index count from parameters
2. Reserves capacity in the target mesh_slot_t
3. Fills position, normal, and UV0 buffers
4. Fills index buffer with triangle winding (CCW)
5. Sets all polygroup IDs to 0 (single group)

The box generator is highest priority — it enables the basic editing workflow described in the spec's example session.

Files to create:
- include/ferrum/editor/mesh/mesh_primitives.h — generator function declarations
- src/editor/mesh/mesh_prim_box.c — box primitive (up to 4 functions)
- src/editor/mesh/mesh_prim_cylinder.c — cylinder primitive
- src/editor/mesh/mesh_prim_sphere.c — UV sphere primitive
- src/editor/mesh/mesh_prim_plane.c — plane primitive
- src/editor/commands/cmd_mesh_create.c — command handler dispatching to generators
- tests/editor/mesh_primitives_tests.c

## Acceptance Criteria

- Box: correct vertex count (depends on segments), all 6 faces have outward normals
- Box with segments=[1,1,1] produces 24 vertices, 36 indices (6 faces × 2 tris × 3)
- Cylinder: top/bottom caps, correct normal orientation, configurable segment count
- Sphere: UV sphere with poles, correct winding, no degenerate triangles
- Plane: grid with configurable subdivision, normal along specified axis
- All primitives have valid UV0 coordinates in [0,1] range
- All index references are within vertex count bounds
- Server command mesh_create_box registered and functional
- Tests: vertex/index counts, normal directions, UV ranges, index bounds

